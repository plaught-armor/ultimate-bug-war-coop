// Card 531 (performance mod family, card 527): cache the save component behind USTSaveComponent::GetSaveId.
//
// GetSaveId (static FGuid GetSaveId(const AActor*), FGuid through a hidden out pointer) calls the actor's
// FindComponentByClass (vtable +0x720) with USTSaveComponent::StaticClass() and copies the FGuid at component +0x164.
// FindComponentByClass walks every owned component with IsA. The targeting code keys its per-target table by that
// GUID, so every target lookup pays one walk. c263L346 (perf -b, both sides): FindComponentByClass is 4.3% of the
// GameThread and 11% of Background Work, and GetSaveId is about 88% of the entries the branch stacks show (directly
// and through ACharacter::FindComponentByClass).
//
// The detour keeps a per-thread direct-mapped cache: actor -> its save component, the component held as a weak
// pointer. A hit needs the actor slot to match, the component's weak pointer to resolve, and the component's owner to
// still be the actor (since b361 the actor itself has no weak pointer; see c531_hit); the GUID is then read fresh from +0x164, so a load that rewrites it is seen. Anything else runs
// the same lookup the original does and stores the result. The result equals the original's on every call, so host
// decisions do not change (co-op safe). Every 1024th hit on a thread also runs the full lookup and counts a mismatch.
//
// Threads: the function runs on the game thread and on task workers. Each thread has its own cache (thread_local, as
// fx_c136.cpp), so there is no shared write. Counters are kept per thread and added to shared atomics every
// kC531FoldCalls calls. The game thread prints `card531:` totals every kC531ReportMs (c42_step).
// Off switch for A/B runs: UBWCOOP_C531_OFF=1 (the detour is then not installed).
//
// Not done: the original's incremental-GC barrier on the class pointer (it marks StaticClass's UClass reachable when
// incremental reachability is pending). A native UClass is never collected, so skipping it changes nothing.

#include "ubw/hooks_internal.h"

#include <atomic>
#include <string.h>

namespace ubw::hooks {

GetSaveIdFn g_orig_c531_save_id = nullptr;
StaticClassFn g_c531_static_class = nullptr;
bool g_c531_on = true; // written in ubw::init before any hook (UBWCOOP_C531_OFF=1 turns it off)

namespace {

constexpr size_t kC531FindComponentVfn = 0x720; // AActor::FindComponentByClass(TSubclassOf<UActorComponent>)
constexpr size_t kC531Guid = 0x164;             // USTSaveComponent save FGuid
constexpr size_t kC531ObjectFlags = 0x08;       // UObjectBase::ObjectFlags
constexpr uint32_t kC531GarbageBit = 30;        // the bit GetSaveId tests on the actor and the component
// Per thread: kC531Sets sets of kC531Ways entries (2048 * 24 B = 48 KB). b342 was direct mapped with 512 slots: with
// about 300 live targets on the host, about 45% of actors shared a slot with another live one, and c263L348 showed a
// 63% host hit rate (client 99.6%). Two ways over 1024 sets: a miss needs three live actors in one set (about 3.5%).
constexpr uint32_t kC531Sets = 1024; // power of two
constexpr uint32_t kC531Ways = 2;
constexpr uint32_t kC531VerifyEvery = 1024;     // hits per thread between full-lookup checks
constexpr uint32_t kC531FoldCalls = 4096;       // calls per thread between folds into the shared totals
constexpr ULONGLONG kC531ReportMs = 60000;

struct C531Entry {
    const void* actor;
    C31Weak component_weak;
};

struct C531Counts {
    uint32_t hits, misses, none, checks, mismatches;
};

struct C531Thread {
    C531Entry e[kC531Sets][kC531Ways]; // way 0 holds the newest entry of the set
    C531Counts n;
    uint32_t calls;
    uint32_t verify;
};
thread_local C531Thread t_c531{};

struct C531Shared {
    std::atomic<uint64_t> hits, misses, none, checks, mismatches;
};
C531Shared g_c531{};
ULONGLONG g_c531_report_at = 0; // game thread

bool c531_garbage(const void* object) {
    uint32_t flags = 0;
    memcpy(&flags, static_cast<const uint8_t*>(object) + kC531ObjectFlags, sizeof(flags));
    return ((flags >> kC531GarbageBit) & 1) != 0;
}

// The original's lookup: the actor's save component, or null.
void* c531_find(const void* actor) {
    void* cls = g_c531_static_class();
    using FindFn = void* (*)(const void* self, void* const* cls);
    return reinterpret_cast<FindFn>(read_ptr(read_ptr(actor, 0), kC531FindComponentVfn))(actor, &cls);
}

void c531_fold(C531Thread& t) {
    g_c531.hits.fetch_add(t.n.hits, std::memory_order_relaxed);
    g_c531.misses.fetch_add(t.n.misses, std::memory_order_relaxed);
    g_c531.none.fetch_add(t.n.none, std::memory_order_relaxed);
    g_c531.checks.fetch_add(t.n.checks, std::memory_order_relaxed);
    g_c531.mismatches.fetch_add(t.n.mismatches, std::memory_order_relaxed);
    t.n = C531Counts{};
    t.calls = 0;
}

// The cached component when the slot still names this actor and the component is live and owned by it. The actor
// needs no weak check of its own (card 531 trim): a live component keeps its Outer, the actor, alive through GC, so
// while the component resolves the actor's address cannot have been reused, and the owner check catches a component
// moved to another actor. The caller has already rejected a garbage actor.
void* c531_hit(const C531Entry& e, const void* actor) {
    if (e.actor != actor) {
        return nullptr;
    }
    void* const component = c31_weak_get(e.component_weak);
    return component != nullptr && read_ptr(component, kComponentOwner) == actor ? component : nullptr;
}

} // namespace

uint32_t* detour_c531_save_id(uint32_t* out, const void* actor) {
    if (g_c531_static_class == nullptr || g_weak_get == nullptr) {
        return g_orig_c531_save_id(out, actor);
    }
    memset(out, 0, 16);
    if (actor == nullptr || c531_garbage(actor)) {
        return out; // the original's early out
    }
    C531Thread& t = t_c531;
    if (++t.calls >= kC531FoldCalls) {
        c531_fold(t);
    }
    C531Entry* const set = t.e[(reinterpret_cast<uintptr_t>(actor) >> 4) * 0x9E3779B97F4A7C15ULL >> 54 & (kC531Sets - 1)];
    void* component = c531_hit(set[0], actor);
    if (component == nullptr) {
        component = c531_hit(set[1], actor);
    }
    if (component != nullptr) {
        ++t.n.hits;
        if (++t.verify >= kC531VerifyEvery) {
            t.verify = 0;
            ++t.n.checks;
            t.n.mismatches += c531_find(actor) != component ? 1 : 0;
        }
    } else {
        component = c531_find(actor);
        ++t.n.misses;
        if (component == nullptr) {
            ++t.n.none;
            return out;
        }
        set[1] = set[0]; // the older entry leaves the set
        set[0].actor = actor;
        c31_weak_set(set[0].component_weak, component);
    }
    if (!c531_garbage(component)) {
        memcpy(out, static_cast<const uint8_t*>(component) + kC531Guid, 16);
    }
    return out;
}

// Game thread, per frame (c42_step): totals every kC531ReportMs while calls come in.
void c531_tick(ULONGLONG now) {
    if (!g_c531_on || now < g_c531_report_at) {
        return;
    }
    g_c531_report_at = now + kC531ReportMs;
    const uint64_t hits = g_c531.hits.load(std::memory_order_relaxed);
    const uint64_t misses = g_c531.misses.load(std::memory_order_relaxed);
    if (hits + misses == 0) {
        return;
    }
    log::line("card531: %s t=%llu GetSaveId cache: hits %llu, misses %llu (%.2f%% hit), no save component %llu, "
              "checks %llu, mismatches %llu (folded every %u calls per thread)",
              g_client_session ? "client" : "host", static_cast<unsigned long long>(now),
              static_cast<unsigned long long>(hits), static_cast<unsigned long long>(misses),
              100.0 * static_cast<double>(hits) / static_cast<double>(hits + misses),
              static_cast<unsigned long long>(g_c531.none.load(std::memory_order_relaxed)),
              static_cast<unsigned long long>(g_c531.checks.load(std::memory_order_relaxed)),
              static_cast<unsigned long long>(g_c531.mismatches.load(std::memory_order_relaxed)), kC531FoldCalls);
}

} // namespace ubw::hooks
