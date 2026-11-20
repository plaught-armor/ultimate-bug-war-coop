// Cards 536 and 537 (performance mod family, card 527): cache AActor::FindComponentByClass results per thread.
//
// AActor::FindComponentByClass(TSubclassOf<UActorComponent>) walks every owned component with IsA and returns the
// first match. ACharacter's override checks its mesh, movement and capsule members, then calls the AActor one
// (exe 0x1439ae084). c263L351 (b345, perf -b, host): after card 531, AActor::FindComponentByClass is still 8.5% of
// Background Work, 2.2% of Foreground Work and 1.4% of the GameThread.
//
// Card 536 (b347..b349) checked each hit with weak pointers. c263L353: the walk fell to 0.2% of Background Work, but
// the detour itself cost 2.2% (FWeakObjectPtr::Get calls into the exe per hit), and "none" results were never stored,
// so every lookup that found nothing still walked.
//
// Card 537 v1 (b350): the walk's result depends only on the actor's owned-component set (a component's class never
// changes). The set changes only through AActor::AddOwnedComponent, RemoveOwnedComponent and ResetOwnedComponents
// (static callers, c263L353 scan of the exe: UActorComponent PostInitProperties, PostRename, BeginDestroy and
// DestroyComponent, UCharacterMovementComponent::BeginDestroy, AActor PostInitProperties and PostLoadSubobjects,
// UEngine::CopyPropertiesForUnrelatedObjects, ALevelSequenceMediaController's constructor). Those three are detoured
// to bump a generation counter for the actor's object index, once before and once after the change. A cache entry
// keeps the actor address, class, object index and the generation read before its walk; a hit needs all four to
// match the current values. "None" results are stored too: a component added later bumps the generation.
// - A freed actor and a new one at the same address: AActor::PostInitProperties always calls ResetOwnedComponents,
//   which bumps the new actor's index. A different index fails the index compare.
// - A destroyed actor still in memory: the original walks its set as well, so the cached result is the same.
// - Threads: a change racing a walk on another thread bumps before and after, so the entry stored from that walk
//   holds an older generation and never hits.
// - Generations live in kC537GenSlots slots by index & mask. Two indexes sharing a slot only cost extra misses.
// - Known gap: an exe path that edits OwnedComponents without the three functions (an inlined copy), or garbage
//   collection clearing a set entry. The verify count watches it: every kC536VerifyEvery-th hit on a thread also
//   runs the original and counts a mismatch.
//
// Card 537 v2 (b351). c263L355 (b350): correct (mismatches 0), but the host hit rate fell to 96.18%, about 30
// misses per set change: one bump per actor dropped every cached class of that actor on every thread. A change can only
// alter the result for a class the changed component IsA (an add can make a new first match; a remove can only drop the
// removed one). So Add and Remove now bump one slot per class in the component's SuperStruct chain, keyed by
// (actor index, class); a hit checks the slot of its own (index, class). ResetOwnedComponents, and a null component,
// bump a per-index "all" generation that every entry of that index also checks. The report names the component
// classes that change most.
//
// Every kC536SampleClass-th call per thread counts its looked-up class; the report names the top ones (which
// components the game looks for most).
// "None" walks still sample their callers (every kC536SampleNone-th one per thread, RtlCaptureStackBackTrace).
// Threads: thread_local caches, counters folded into shared atomics every kC536FoldCalls calls (as card 531).
// Off switch for A/B runs: UBWCOOP_C536_OFF=1 (none of the four detours is installed then).

#include "ubw/hooks_internal.h"

#include <atomic>
#include <string.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace ubw::hooks {

FindComponentByClassFn g_orig_c536_find = nullptr;
OwnedComponentFn g_orig_c537_add = nullptr;
OwnedComponentFn g_orig_c537_remove = nullptr;
ResetOwnedComponentsFn g_orig_c537_reset = nullptr;
bool g_c536_on = true; // written in ubw::init before any hook (UBWCOOP_C536_OFF=1 turns it off)

namespace {

constexpr uint32_t kC536Sets = 4096; // power of two; 8192 entries * 40 B = 320 KB per thread (card 538 size test)
constexpr uint32_t kC536VerifyEvery = 1024;
constexpr uint32_t kC536FoldCalls = 4096;
constexpr uint32_t kC536SampleNone = 64;
constexpr uint32_t kC536CallerRows = 128;
constexpr int32_t kC536TopCallers = 10;
constexpr ULONGLONG kC536ReportMs = 60000;
constexpr uint32_t kC537GenSlots = 1u << 17;  // power of two; (actor index, class) slots, 512 KB
constexpr uint32_t kC537AllSlots = 1u << 16;  // power of two; per actor index, for resets
constexpr int32_t kC537MaxSupers = 32;        // SuperStruct chain bound (as kPropMaxSupers)
constexpr uint32_t kC537ClassRows = 64;
constexpr int32_t kC537TopClasses = 5;
constexpr uint32_t kC536SampleClass = 1024; // every Nth call per thread counts its looked-up class
constexpr int32_t kC536TopLookups = 10;

// Bumped before and after each change that can alter the walk for (actor index, class).
std::atomic<uint32_t> g_c537_gen[kC537GenSlots] = {};
// Per actor index & mask: bumped before and after a reset of the whole set (or a change with no component).
std::atomic<uint32_t> g_c537_all[kC537AllSlots] = {};
std::atomic<uint64_t> g_c537_changes{0};

struct ClassRow {
    const void* cls;
    uint32_t n;
};
// Counts per class (open addressing, kC537ClassRows rows), shared by threads under `lock`.
struct ClassTable {
    SRWLOCK lock;
    ClassRow rows[kC537ClassRows];
    uint32_t dropped;
};
ClassTable g_c537_classes{SRWLOCK_INIT, {}, 0}; // owned-component set changes per component class
ClassTable g_c536_lookups{SRWLOCK_INIT, {}, 0}; // sampled FindComponentByClass calls per looked-up class

struct C536Entry {
    const void* actor; // null: empty
    const void* cls;
    void* result;      // null: the walk found none
    int32_t index;     // the actor's object index when stored
    uint32_t gen;      // g_c537_gen of (index, cls), read before the walk
    uint32_t all;      // g_c537_all of index, read before the walk
};

struct C536Counts {
    uint32_t hits, none_hits, misses, stale, none, checks, mismatches;
};

struct C536Thread {
    C536Entry e[kC536Sets][2]; // way 0 holds the newest entry of the set
    C536Counts n;
    uint32_t calls;
    uint32_t verify;
    uint32_t none_sample;
    uint32_t class_sample;
};
thread_local C536Thread t_c536{};

struct C536Shared {
    std::atomic<uint64_t> hits, none_hits, misses, stale, none, checks, mismatches;
};
C536Shared g_c536{};

struct C536Caller {
    uintptr_t frame[2];
    uint32_t samples;
};
SRWLOCK g_c536_lock = SRWLOCK_INIT;
C536Caller g_c536_callers[kC536CallerRows] = {}; // under g_c536_lock
uint32_t g_c536_callers_dropped = 0;             // under g_c536_lock
ULONGLONG g_c536_report_at = 0;                  // game thread

void c536_fold(C536Thread& t) {
    g_c536.hits.fetch_add(t.n.hits, std::memory_order_relaxed);
    g_c536.none_hits.fetch_add(t.n.none_hits, std::memory_order_relaxed);
    g_c536.misses.fetch_add(t.n.misses, std::memory_order_relaxed);
    g_c536.stale.fetch_add(t.n.stale, std::memory_order_relaxed);
    g_c536.none.fetch_add(t.n.none, std::memory_order_relaxed);
    g_c536.checks.fetch_add(t.n.checks, std::memory_order_relaxed);
    g_c536.mismatches.fetch_add(t.n.mismatches, std::memory_order_relaxed);
    t.n = C536Counts{};
    t.calls = 0;
}

std::atomic<uint32_t>& c537_gen_of(int32_t index, const void* cls) {
    const uint64_t key = static_cast<uint64_t>(static_cast<uint32_t>(index)) ^ (reinterpret_cast<uintptr_t>(cls) << 12);
    return g_c537_gen[static_cast<uint32_t>(key * 0x9E3779B97F4A7C15ULL >> 47) & (kC537GenSlots - 1)];
}

std::atomic<uint32_t>& c537_all_of(int32_t index) {
    return g_c537_all[static_cast<uint32_t>(index) & (kC537AllSlots - 1)];
}

// Diagnostics: one more count for `cls` in `table`.
void class_count(ClassTable& table, const void* cls) {
    uint32_t i = static_cast<uint32_t>((reinterpret_cast<uintptr_t>(cls) >> 4) * 0x9E3779B97F4A7C15ULL >> 58) &
                 (kC537ClassRows - 1);
    AcquireSRWLockExclusive(&table.lock);
    for (uint32_t p = 0; p < kC537ClassRows; ++p, i = (i + 1) & (kC537ClassRows - 1)) {
        ClassRow& r = table.rows[i];
        if (r.n == 0) {
            r.cls = cls;
        }
        if (r.cls == cls) {
            ++r.n;
            ReleaseSRWLockExclusive(&table.lock);
            return;
        }
    }
    ++table.dropped;
    ReleaseSRWLockExclusive(&table.lock);
}

// The owned-component set of `actor` is about to change (after == false), or has just changed, by adding or removing
// `component`. Bumps (index, class) for every class the component IsA, or the whole index when there is no component.
void c537_bump(const void* actor, const void* component, bool after) {
    if (actor == nullptr) {
        return;
    }
    const int32_t index = read_i32(actor, kObjectIndex);
    const void* cls = component != nullptr ? read_ptr(component, kObjectClass) : nullptr;
    if (cls == nullptr) {
        c537_all_of(index).fetch_add(1, std::memory_order_acq_rel);
    }
    if (after) {
        g_c537_changes.fetch_add(1, std::memory_order_relaxed);
        if (cls != nullptr) {
            class_count(g_c537_classes, cls); // both bumps of one change count once, on the "after" call
        }
    }
    for (int32_t depth = 0; cls != nullptr && depth < kC537MaxSupers; ++depth) {
        c537_gen_of(index, cls).fetch_add(1, std::memory_order_acq_rel);
        cls = read_ptr(cls, kStructSuper);
    }
}

bool c536_hit(const C536Entry& e, const void* actor, const void* cls, int32_t index) {
    return e.actor == actor && e.cls == cls && e.index == index &&
           e.gen == c537_gen_of(index, cls).load(std::memory_order_acquire) &&
           e.all == c537_all_of(index).load(std::memory_order_acquire);
}

// Logs the `limit` (at most kC536TopLookups) largest rows of `table` as "<card>: <side> t=<now> <what> <i>: <class>,
// <n> <unit> (<share>%)", then a WARN when counts were dropped.
void class_report(ClassTable& table, const char* card, const char* what, const char* unit, int32_t limit,
                  const char* side, ULONGLONG now) {
    ClassRow top[kC536TopLookups] = {};
    int32_t n = 0;
    uint64_t total = 0;
    limit = limit < kC536TopLookups ? limit : kC536TopLookups;
    AcquireSRWLockExclusive(&table.lock);
    for (const ClassRow& r : table.rows) {
        total += r.n;
        if (r.n == 0 || (n == limit && r.n <= top[n - 1].n)) {
            continue;
        }
        int32_t at = n < limit ? n++ : n - 1;
        for (; at > 0 && top[at - 1].n < r.n; --at) {
            top[at] = top[at - 1];
        }
        top[at] = r;
    }
    const uint32_t dropped = table.dropped;
    total += dropped;
    ReleaseSRWLockExclusive(&table.lock);
    for (int32_t i = 0; i < n; ++i) {
        // A blueprint class can be unloaded with its map: name it only while it is still listed.
        ObjectName name{};
        if (object_listed(top[i].cls)) {
            name = object_name(top[i].cls);
        } else {
            snprintf(name.text, sizeof(name.text), "(unloaded %p)", top[i].cls);
        }
        log::line("%s: %s t=%llu %s %d: %s, %u %s (%.1f%%)", card, side, static_cast<unsigned long long>(now), what,
                  i + 1, name.text, top[i].n, unit, 100.0 * top[i].n / static_cast<double>(total != 0 ? total : 1));
    }
    if (dropped != 0) {
        log::line("WARN %s: %s %u %s not keyed (class table of %u full)", card, side, dropped, unit, kC537ClassRows);
    }
}

// A "none" call's two callers above the detour, into the shared table. Not inlined, so skipping 2 frames (this
// function's return into the detour, the detour's into the game) leaves the detour's caller and its caller.
__declspec(noinline) void c536_sample_none() {
    void* frames[2] = {};
    if (RtlCaptureStackBackTrace(2, 2, frames, nullptr) == 0) {
        return;
    }
    const uintptr_t f0 = reinterpret_cast<uintptr_t>(frames[0]);
    const uintptr_t f1 = reinterpret_cast<uintptr_t>(frames[1]);
    uint32_t i = static_cast<uint32_t>(((f0 ^ (f1 << 1)) * 0x9E3779B97F4A7C15ULL) >> 57) & (kC536CallerRows - 1);
    AcquireSRWLockExclusive(&g_c536_lock);
    for (uint32_t p = 0; p < kC536CallerRows; ++p, i = (i + 1) & (kC536CallerRows - 1)) {
        C536Caller& r = g_c536_callers[i];
        if (r.samples == 0) {
            r.frame[0] = f0;
            r.frame[1] = f1;
        }
        if (r.frame[0] == f0 && r.frame[1] == f1) {
            ++r.samples;
            ReleaseSRWLockExclusive(&g_c536_lock);
            return;
        }
    }
    ++g_c536_callers_dropped;
    ReleaseSRWLockExclusive(&g_c536_lock);
}

void c536_where(uintptr_t a, char* out, size_t cap) {
    const uintptr_t exe = reinterpret_cast<uintptr_t>(ue::hooktable::g_exe_base);
    const uintptr_t mod = reinterpret_cast<uintptr_t>(&__ImageBase);
    if (exe != 0 && a >= exe && a - exe < 0x10000000ULL) {
        snprintf(out, cap, "exe+0x%llx", static_cast<unsigned long long>(a - exe));
    } else if (a >= mod && a - mod < 0x1000000ULL) {
        snprintf(out, cap, "mod+0x%llx", static_cast<unsigned long long>(a - mod));
    } else {
        snprintf(out, cap, "%p", reinterpret_cast<const void*>(a));
    }
}

void c536_report_callers(const char* side, ULONGLONG now) {
    C536Caller top[kC536TopCallers] = {};
    int32_t n = 0;
    uint32_t total = 0;
    uint32_t dropped = 0;
    AcquireSRWLockExclusive(&g_c536_lock);
    for (const C536Caller& r : g_c536_callers) {
        total += r.samples;
        if (r.samples == 0 || (n == kC536TopCallers && r.samples <= top[n - 1].samples)) {
            continue;
        }
        int32_t at = n < kC536TopCallers ? n++ : n - 1;
        for (; at > 0 && top[at - 1].samples < r.samples; --at) {
            top[at] = top[at - 1];
        }
        top[at] = r;
    }
    dropped = g_c536_callers_dropped;
    ReleaseSRWLockExclusive(&g_c536_lock);
    for (int32_t i = 0; i < n; ++i) {
        char a[48];
        char b[48];
        c536_where(top[i].frame[0], a, sizeof(a));
        c536_where(top[i].frame[1], b, sizeof(b));
        log::line("card536: %s t=%llu none caller %d: %s <- %s, %u of %u samples (%.1f%%)", side,
                  static_cast<unsigned long long>(now), i + 1, a, b, top[i].samples, total,
                  100.0 * top[i].samples / (total != 0 ? total : 1));
    }
    if (dropped != 0) {
        log::line("WARN card536: %s %u none samples not keyed (caller table of %u full)", side, dropped,
                  kC536CallerRows);
    }
}

} // namespace

void detour_c537_add(void* actor, void* component) {
    c537_bump(actor, component, false);
    g_orig_c537_add(actor, component);
    c537_bump(actor, component, true);
}

void detour_c537_remove(void* actor, void* component) {
    c537_bump(actor, component, false);
    g_orig_c537_remove(actor, component);
    c537_bump(actor, component, true);
}

void detour_c537_reset(void* actor) {
    c537_bump(actor, nullptr, false);
    g_orig_c537_reset(actor);
    c537_bump(actor, nullptr, true);
}

void* detour_c536_find(const void* actor, void* const* cls_arg) {
    const void* const cls = cls_arg != nullptr ? *cls_arg : nullptr;
    if (cls == nullptr || actor == nullptr) {
        return g_orig_c536_find(actor, cls_arg);
    }
    C536Thread& t = t_c536;
    if (++t.calls >= kC536FoldCalls) {
        c536_fold(t);
    }
    if (++t.class_sample >= kC536SampleClass) {
        t.class_sample = 0;
        class_count(g_c536_lookups, cls);
    }
    const int32_t index = read_i32(actor, kObjectIndex);
    C536Entry* const set =
        t.e[((reinterpret_cast<uintptr_t>(actor) >> 4) ^ (reinterpret_cast<uintptr_t>(cls) >> 3)) *
                0x9E3779B97F4A7C15ULL >> 40 & (kC536Sets - 1)];
    const C536Entry* const hit = c536_hit(set[0], actor, cls, index)   ? &set[0]
                                 : c536_hit(set[1], actor, cls, index) ? &set[1]
                                                                       : nullptr;
    if (hit != nullptr) {
        void* const result = hit->result;
        ++(result != nullptr ? t.n.hits : t.n.none_hits);
        if (++t.verify >= kC536VerifyEvery) {
            t.verify = 0;
            ++t.n.checks;
            t.n.mismatches += g_orig_c536_find(actor, cls_arg) != result ? 1 : 0;
        }
        return result;
    }
    // A miss whose (actor, class) is still in the set failed on index or generation (stale); any other miss is cold
    // or evicted (the set was too small for the thread's working set).
    t.n.stale += ((set[0].actor == actor && set[0].cls == cls) || (set[1].actor == actor && set[1].cls == cls)) ? 1 : 0;
    const uint32_t gen = c537_gen_of(index, cls).load(std::memory_order_acquire);
    const uint32_t all = c537_all_of(index).load(std::memory_order_acquire);
    void* const result = g_orig_c536_find(actor, cls_arg);
    ++t.n.misses;
    if (result == nullptr) {
        ++t.n.none;
        if (++t.none_sample >= kC536SampleNone) {
            t.none_sample = 0;
            c536_sample_none();
        }
    }
    set[1] = set[0]; // the older entry leaves the set
    set[0] = C536Entry{actor, cls, result, index, gen, all};
    return result;
}

// Game thread, per frame (c42_step): totals every kC536ReportMs while calls come in.
void c536_tick(ULONGLONG now) {
    if (!g_c536_on || now < g_c536_report_at) {
        return;
    }
    g_c536_report_at = now + kC536ReportMs;
    const uint64_t hits = g_c536.hits.load(std::memory_order_relaxed);
    const uint64_t none_hits = g_c536.none_hits.load(std::memory_order_relaxed);
    const uint64_t misses = g_c536.misses.load(std::memory_order_relaxed);
    const uint64_t calls = hits + none_hits + misses;
    if (calls == 0) {
        return;
    }
    const char* const side = g_client_session ? "client" : "host";
    log::line("card536: %s t=%llu FindComponentByClass cache: hits %llu, none hits %llu, misses %llu (%.2f%% hit, stale %llu), "
              "none walks %llu, checks %llu, mismatches %llu, set changes %llu (folded every %u calls per thread)",
              side, static_cast<unsigned long long>(now), static_cast<unsigned long long>(hits),
              static_cast<unsigned long long>(none_hits), static_cast<unsigned long long>(misses),
              100.0 * static_cast<double>(hits + none_hits) / static_cast<double>(calls),
              static_cast<unsigned long long>(g_c536.stale.load(std::memory_order_relaxed)),
              static_cast<unsigned long long>(g_c536.none.load(std::memory_order_relaxed)),
              static_cast<unsigned long long>(g_c536.checks.load(std::memory_order_relaxed)),
              static_cast<unsigned long long>(g_c536.mismatches.load(std::memory_order_relaxed)),
              static_cast<unsigned long long>(g_c537_changes.load(std::memory_order_relaxed)), kC536FoldCalls);
    c536_report_callers(side, now);
    class_report(g_c536_lookups, "card536", "lookup class", "samples", kC536TopLookups, side, now);
    class_report(g_c537_classes, "card537", "set change class", "changes", kC537TopClasses, side, now);
}

} // namespace ubw::hooks
