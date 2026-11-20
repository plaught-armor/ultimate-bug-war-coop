// Card 534 (diag, card 527 family): who issues the world collision queries.
//
// c263L344 host profile: Chaos is about 26% of the GameThread (TAABBTree sweeps 7.8%, raycasts 3.0%, overlaps 1.8%,
// tree inserts 3.1%). The shipping exe has no frame pointers and the 16-entry branch stack is too short to reach the
// caller (perf_frame --callers: 99% "no entry seen"). These detours sit on the UWorld query entry points and on
// USceneComponent::UpdateOverlaps and time each game-thread call (inclusive), keyed by:
//   - the moved or tested component's owner class, for ComponentSweepMulti, ComponentOverlapMulti and UpdateOverlaps
//     (character movement sweeps the capsule through ComponentSweepMulti);
//   - the return address (exe RVA), for the others.
// Calls from other threads are counted per entry only. Every kC534WindowMs the game thread logs the top rows by time
// as `card534: <side> query` rows, then clears the table. Installed only with UBWCOOP_C132_FX=1 (hook table gate).
//
// Card 546 (same UpdateOverlaps detour): UpdateOverlaps by BP_PlayerCharacterMI_C is the largest host query row and
// a top client row, at 2.1-2.3% of the GameThread on both sides in every run since c263L356 (49 us mean in L365).
// UpdateOverlaps recurses into attached children through the same hooked function, so the owner row is inclusive and
// counts each nesting level again. For components whose owner is a pawn but not an NPC (players), a second table
// keys each call by the component itself and keeps inclusive and exclusive time (the call minus its hooked children).
// Every window logs the top rows as `card546: <side> player overlaps` with the component name and class, which tells
// which player components pay for overlaps and whether they need overlap events at all.
//
// Not hooked: UWorld::OverlapMultiByChannel (card 046 detours it) and UPrimitiveComponent::MoveComponentImpl (card 461
// detours it); a second detour on one function is not supported by the hook table.
//
// Every detour has the widest signature (this plus 8 integer or pointer arguments, bool result). All the hooked
// functions take only pointers, references and enums, so the arguments sit in the same registers and stack slots;
// a function with fewer arguments ignores the extra ones, which are read from the caller's frame and passed on unused.

#include "ubw/hooks_internal.h"

#include <algorithm> // std::sort
#include <atomic>
#include <intrin.h>
#include <stdio.h>

namespace ubw::hooks {

C534Fn g_orig_c534[kC534Entries] = {};

namespace {

constexpr ULONGLONG kC534WindowMs = 30000;
constexpr uint32_t kC534Slots = 1024; // power of two
constexpr uint32_t kC534Probe = 16;
constexpr int32_t kC534TopRows = 24;

// Per entry: its name and which argument (0 = this) is the component whose owner class keys the row (-1: caller).
struct C534Entry {
    const char* name;
    int32_t component_arg;
};
constexpr C534Entry kC534Entry[kC534Entries] = {
    {"SweepSingleByChannel", -1},  {"SweepMultiByChannel", -1},     {"SweepTestByChannel", -1},
    {"SweepSingleByObjectType", -1}, {"SweepMultiByObjectType", -1}, {"LineTraceSingleByChannel", -1},
    {"LineTraceMultiByChannel", -1}, {"LineTraceTestByChannel", -1}, {"OverlapAnyTestByChannel", -1},
    {"OverlapMultiByObjectType", -1}, {"ComponentSweepMulti", 2},    {"ComponentOverlapMulti", 2},
    {"UpdateOverlaps", 0},
};

constexpr int32_t kC534UpdateOverlaps = 12; // kC534Entry index of UpdateOverlaps (card 546)

struct C534Row {
    uint64_t key; // entry << 56 | (class pointer or return address); 0 = empty
    uint32_t calls;
    int64_t ticks;
    int64_t max;
    C31Weak cls; // component rows: the keyed class, so the report never names an unloaded (streamed BP) class
};

struct C534State {
    C534Row row[kC534Slots];
    uint32_t dropped;           // calls whose probe found no slot
    uint32_t calls[kC534Entries];
    int64_t ticks[kC534Entries];
    int64_t window_t0;
    ULONGLONG window_ms0;
};
C534State g_c534{};
std::atomic<uint32_t> g_c534_other[kC534Entries] = {};
// Set by c534_tick; 0 until the first frame, so earlier calls count as other threads. Read by every hooked call.
std::atomic<DWORD> g_c534_game_thread{0};

int64_t c534_now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

double c534_ms(int64_t ticks) {
    static const double kPerMs = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart) / 1000.0;
    }();
    return static_cast<double>(ticks) / kPerMs;
}

void c534_add(int32_t entry, uint64_t who, int64_t d) {
    g_c534.calls[entry] += 1;
    g_c534.ticks[entry] += d;
    const uint64_t key = static_cast<uint64_t>(entry + 1) << 56 | (who & 0x00FFFFFFFFFFFFFFULL);
    uint32_t i = static_cast<uint32_t>((key * 0x9E3779B97F4A7C15ULL) >> 54) & (kC534Slots - 1);
    for (uint32_t p = 0; p < kC534Probe; ++p, i = (i + 1) & (kC534Slots - 1)) {
        C534Row& r = g_c534.row[i];
        if (r.key == key || r.key == 0) {
            if (r.key == 0 && kC534Entry[entry].component_arg >= 0 && who != 0) {
                c31_weak_set(r.cls, reinterpret_cast<void*>(who));
            }
            r.key = key;
            ++r.calls;
            r.ticks += d;
            r.max = d > r.max ? d : r.max;
            return;
        }
    }
    ++g_c534.dropped;
}

// The row key for a call: the component's owner class (or the component's class when it has no owner), else the
// return address.
uint64_t c534_who(int32_t entry, const uintptr_t* args, const void* ret) {
    const int32_t a = kC534Entry[entry].component_arg;
    if (a < 0) {
        return reinterpret_cast<uintptr_t>(ret);
    }
    const void* const component = reinterpret_cast<const void*>(args[a]);
    if (component == nullptr) {
        return 0;
    }
    const void* const owner = read_ptr(component, kComponentOwner);
    return reinterpret_cast<uintptr_t>(read_ptr(owner != nullptr ? owner : component, kObjectClass));
}

void c534_row_text(const C534Row& r, char* out, size_t cap) {
    const int32_t entry = static_cast<int32_t>(r.key >> 56) - 1;
    const uint64_t who = r.key & 0x00FFFFFFFFFFFFFFULL;
    if (kC534Entry[entry].component_arg >= 0) {
        const bool live = who != 0 && reinterpret_cast<uintptr_t>(c31_weak_get(r.cls)) == who;
        snprintf(out, cap, "%s by %s", kC534Entry[entry].name,
                 live ? object_name(reinterpret_cast<const void*>(who)).text
                      : who != 0 ? "(class unloaded)" : "(null component)");
        return;
    }
    const uint64_t base = reinterpret_cast<uintptr_t>(ue::hooktable::g_exe_base);
    if (base != 0 && who >= base && who - base < 0x10000000ULL) {
        snprintf(out, cap, "%s from exe+0x%llx", kC534Entry[entry].name, static_cast<unsigned long long>(who - base));
    } else {
        snprintf(out, cap, "%s from %p", kC534Entry[entry].name, reinterpret_cast<const void*>(who));
    }
}

constexpr uint32_t kC546Slots = 1024; // power of two (c263L367: 128 slots dropped 9431 calls)
constexpr uint32_t kC546Probe = 16;
constexpr int32_t kC546TopRows = 16;
constexpr int32_t kC546MaxDepth = 16;

struct C546Row {
    const void* comp; // nullptr = empty slot
    uint32_t calls;
    int64_t ticks;    // inclusive
    int64_t self;     // exclusive of hooked child UpdateOverlaps calls
    C31Weak weak;
};
struct C546State {
    C546Row row[kC546Slots];
    uint32_t dropped;
    int32_t depth;                     // hooked UpdateOverlaps nesting on the game thread
    int64_t child[kC546MaxDepth + 1];  // ticks of hooked children, per open level
};
C546State g_c546{};

bool c546_player_comp(const void* comp) {
    const void* const owner = read_ptr(comp, kComponentOwner);
    return owner != nullptr && g_pawn_class != nullptr && g_npc_class != nullptr && is_a(owner, g_pawn_class()) &&
           !is_a(owner, g_npc_class());
}

void c546_add(const void* comp, int64_t d, int64_t self) {
    uint32_t i = static_cast<uint32_t>((reinterpret_cast<uintptr_t>(comp) * 0x9E3779B97F4A7C15ULL) >> 54) &
                 (kC546Slots - 1);
    for (uint32_t p = 0; p < kC546Probe; ++p, i = (i + 1) & (kC546Slots - 1)) {
        C546Row& r = g_c546.row[i];
        if (r.comp == nullptr) {
            r.comp = comp;
            c31_weak_set(r.weak, const_cast<void*>(comp));
        } else if (r.comp != comp) {
            continue;
        }
        ++r.calls;
        r.ticks += d;
        r.self += self;
        return;
    }
    ++g_c546.dropped;
}

void c546_report(ULONGLONG now, double window) {
    const char* const side = g_client_session ? "client" : "host";
    C546Row* top[kC546TopRows] = {};
    int32_t n = 0;
    for (C546Row& r : g_c546.row) {
        if (r.comp == nullptr) {
            continue;
        }
        if (n < kC546TopRows) {
            top[n++] = &r;
        } else if (r.self > top[n - 1]->self) {
            top[n - 1] = &r;
        } else {
            continue;
        }
        std::sort(top, top + n, [](const C546Row* x, const C546Row* y) { return x->self > y->self; });
    }
    for (int32_t i = 0; i < n; ++i) {
        const C546Row& r = *top[i];
        const bool live = c31_weak_get(r.weak) == r.comp;
        log::line("card546: %s t=%llu player overlaps top %d: %s (%s): %u calls, self %.1f ms (%.2f%%), inclusive "
                  "%.1f ms, %.1f us self mean", side, static_cast<unsigned long long>(now), i + 1,
                  live ? object_name(r.comp).text : "(component gone)", live ? class_name(r.comp).text : "-",
                  r.calls, c534_ms(r.self), 100.0 * c534_ms(r.self) / window, c534_ms(r.ticks),
                  1000.0 * c534_ms(r.self) / static_cast<double>(r.calls));
    }
    if (g_c546.dropped != 0) {
        log::line("WARN card546: %s %u calls not keyed (table of %u full)", side, g_c546.dropped, kC546Slots);
    }
}

void c534_report(ULONGLONG now, int64_t t) {
    const char* const side = g_client_session ? "client" : "host";
    const double window = c534_ms(t - g_c534.window_t0);
    C534Row* top[kC534TopRows] = {};
    int32_t n = 0;
    for (C534Row& r : g_c534.row) {
        if (r.key == 0) {
            continue;
        }
        if (n < kC534TopRows) {
            top[n++] = &r;
        } else if (r.ticks > top[n - 1]->ticks) {
            top[n - 1] = &r;
        } else {
            continue;
        }
        std::sort(top, top + n, [](const C534Row* x, const C534Row* y) { return x->ticks > y->ticks; });
    }
    for (int32_t e = 0; e < kC534Entries; ++e) {
        const uint32_t other = g_c534_other[e].exchange(0, std::memory_order_relaxed);
        if (g_c534.calls[e] == 0 && other == 0) {
            continue;
        }
        log::line("card534: %s t=%llu query total %s: game thread %u calls %.1f ms (%.2f%% of %.0f ms); other "
                  "threads %u calls", side, static_cast<unsigned long long>(now), kC534Entry[e].name, g_c534.calls[e],
                  c534_ms(g_c534.ticks[e]), 100.0 * c534_ms(g_c534.ticks[e]) / window, window, other);
    }
    for (int32_t i = 0; i < n; ++i) {
        char what[160];
        c534_row_text(*top[i], what, sizeof(what));
        log::line("card534: %s t=%llu query top %d: %s: %u calls, %.1f ms (%.2f%%), %.1f us mean, %.1f us max", side,
                  static_cast<unsigned long long>(now), i + 1, what, top[i]->calls, c534_ms(top[i]->ticks),
                  100.0 * c534_ms(top[i]->ticks) / window,
                  1000.0 * c534_ms(top[i]->ticks) / static_cast<double>(top[i]->calls), 1000.0 * c534_ms(top[i]->max));
    }
    if (g_c534.dropped != 0) {
        log::line("WARN card534: %s %u calls not keyed (table of %u full)", side, g_c534.dropped, kC534Slots);
    }
    c546_report(now, window);
}

} // namespace

template <int K>
bool detour_c534(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6,
                 uintptr_t a7, uintptr_t a8) {
    const DWORD game_thread = g_c534_game_thread.load(std::memory_order_relaxed);
    if (game_thread == 0 || GetCurrentThreadId() != game_thread) {
        g_c534_other[K].fetch_add(1, std::memory_order_relaxed);
        return g_orig_c534[K](a0, a1, a2, a3, a4, a5, a6, a7, a8);
    }
    const void* const ret = _ReturnAddress();
    constexpr bool kOverlaps = K == kC534UpdateOverlaps;
    const bool nest = kOverlaps && g_c546.depth < kC546MaxDepth;
    if (nest) {
        g_c546.child[++g_c546.depth] = 0;
    }
    const int64_t t0 = c534_now();
    const bool r = g_orig_c534[K](a0, a1, a2, a3, a4, a5, a6, a7, a8);
    const int64_t d = c534_now() - t0;
    if (nest) {
        const int64_t self = d - g_c546.child[g_c546.depth--];
        g_c546.child[g_c546.depth] += d; // level 0 collects unused top-level time
        const void* const comp = reinterpret_cast<const void*>(a0);
        if (comp != nullptr && c546_player_comp(comp)) {
            c546_add(comp, d, self);
        }
    }
    const uintptr_t args[3] = {a0, a1, a2};
    c534_add(K, c534_who(K, args, ret), d);
    return r;
}

template bool detour_c534<0>(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                             uintptr_t);
template bool detour_c534<1>(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                             uintptr_t);
template bool detour_c534<2>(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                             uintptr_t);
template bool detour_c534<3>(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                             uintptr_t);
template bool detour_c534<4>(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                             uintptr_t);
template bool detour_c534<5>(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                             uintptr_t);
template bool detour_c534<6>(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                             uintptr_t);
template bool detour_c534<7>(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                             uintptr_t);
template bool detour_c534<8>(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                             uintptr_t);
template bool detour_c534<9>(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                             uintptr_t);
template bool detour_c534<10>(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                              uintptr_t);
template bool detour_c534<11>(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                              uintptr_t);
template bool detour_c534<12>(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                              uintptr_t);

// Game thread, per frame (c42_step).
void c534_tick(ULONGLONG now) {
    if (!g_c132_fx_log) {
        return;
    }
    g_c534_game_thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    const int64_t t = c534_now();
    if (g_c534.window_ms0 == 0) {
        g_c534.window_ms0 = now;
        g_c534.window_t0 = t;
        return;
    }
    if (now - g_c534.window_ms0 < kC534WindowMs) {
        return;
    }
    c534_report(now, t);
    g_c534 = C534State{};
    for (C546Row& row : g_c546.row) {
        row = C546Row{}; // depth and open levels stay: the tick runs outside any UpdateOverlaps
    }
    g_c546.dropped = 0;
    g_c534.window_ms0 = now;
    g_c534.window_t0 = t;
}

} // namespace ubw::hooks
