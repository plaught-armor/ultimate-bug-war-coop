// Card 548 (performance mod family, card 527): which player components make the Camera UpdateOverlaps self time.
//
// c263L368: the player Camera components hold about 99% of player UpdateOverlaps self time (card 546, about 470 ms per
// 30 s host window at 213-463 us per call). Card 547 ruled out UpdatePhysicsVolume: the world has no non-default
// physics volume, and the whole host total is about 4 ms. In USceneComponent::UpdateOverlapsImpl (RVA 0x3afef70) the
// rest of the work is the loop over attached children. It calls each child's virtual UpdateOverlapsImpl (vtable
// +0x568) directly, not through the public UpdateOverlaps that card 534 hooks. So the camera's self time belongs to
// its attached primitives (first-person meshes, weapons and what they carry).
//
// This diag hooks the three primitive overrides (UPrimitiveComponent, USkinnedMeshComponent, USkeletalMeshComponent).
// The skinned one is folded with USceneComponent::UpdateOverlapsImpl (same RVA 0x3afef70), so the camera itself also
// gets a row, whose self time is what is left after its children.
// Each game-thread call on a component owned by a player pawn is a row keyed by component, with calls, inclusive and
// self time. Self excludes nested hooked calls on other components. A Super call on the same component (skeletal to
// skinned to primitive) is not a new level. Each 30 s window logs the top rows by self time. The counts change no
// result. Gate: UBWCOOP_C132_FX=1 (diag runs).

#include "ubw/hooks_internal.h"

#include <algorithm> // std::sort
#include <atomic>

namespace ubw::hooks {

C548ImplFn g_orig_c548_impl[kC548Entries] = {};

namespace {

constexpr ULONGLONG kC548WindowMs = 30000;
constexpr uint32_t kC548Slots = 1024; // power of two
constexpr uint32_t kC548Probe = 16;
constexpr int32_t kC548TopRows = 16;
constexpr int32_t kC548MaxDepth = 32;

struct C548Row {
    const void* comp; // nullptr = empty slot
    uint32_t calls;
    int64_t ticks; // inclusive
    int64_t self;  // exclusive of nested hooked calls on other components
    C31Weak weak;
};

struct C548Level {
    const void* comp;
    int64_t child; // ticks of nested levels
};

struct C548State {
    C548Row row[kC548Slots];
    uint32_t dropped;
    uint32_t calls;   // all timed game-thread calls (any owner)
    int64_t ticks;    // their outermost inclusive time
    int64_t window_t0;
    ULONGLONG window_ms0;
};
C548State g_c548{};
C548Level g_c548_stack[kC548MaxDepth + 1] = {}; // game thread; level 0 collects top-level time
int32_t g_c548_depth = 0;
std::atomic<DWORD> g_c548_game_thread{0};

int64_t c548_now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

double c548_ms(int64_t ticks) {
    static const double kPerMs = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart) / 1000.0;
    }();
    return static_cast<double>(ticks) / kPerMs;
}

bool c548_player_comp(const void* comp) {
    const void* const owner = read_ptr(comp, kComponentOwner);
    return owner != nullptr && g_pawn_class != nullptr && g_npc_class != nullptr && is_a(owner, g_pawn_class()) &&
           !is_a(owner, g_npc_class());
}

void c548_add(const void* comp, int64_t d, int64_t self) {
    uint32_t i = static_cast<uint32_t>((reinterpret_cast<uintptr_t>(comp) * 0x9E3779B97F4A7C15ULL) >> 54) &
                 (kC548Slots - 1);
    for (uint32_t p = 0; p < kC548Probe; ++p, i = (i + 1) & (kC548Slots - 1)) {
        C548Row& r = g_c548.row[i];
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
    ++g_c548.dropped;
}

void c548_report(ULONGLONG now, int64_t t) {
    const char* const side = g_client_session ? "client" : "host";
    const double window = c548_ms(t - g_c548.window_t0);
    log::line("card548: %s t=%llu primitive UpdateOverlapsImpl total: %u outermost calls, %.1f ms (%.2f%% of %.0f ms)",
              side, static_cast<unsigned long long>(now), g_c548.calls, c548_ms(g_c548.ticks),
              100.0 * c548_ms(g_c548.ticks) / window, window);
    C548Row* top[kC548TopRows] = {};
    int32_t n = 0;
    for (C548Row& r : g_c548.row) {
        if (r.comp == nullptr) {
            continue;
        }
        if (n < kC548TopRows) {
            top[n++] = &r;
        } else if (r.self > top[n - 1]->self) {
            top[n - 1] = &r;
        } else {
            continue;
        }
        std::sort(top, top + n, [](const C548Row* x, const C548Row* y) { return x->self > y->self; });
    }
    for (int32_t i = 0; i < n; ++i) {
        const C548Row& r = *top[i];
        const bool live = c31_weak_get(r.weak) == r.comp;
        log::line("card548: %s t=%llu player primitive overlaps top %d: %s (%s): %u calls, self %.1f ms (%.2f%%), "
                  "inclusive %.1f ms, %.1f us self mean", side, static_cast<unsigned long long>(now), i + 1,
                  live ? object_name(r.comp).text : "(component gone)", live ? class_name(r.comp).text : "-",
                  r.calls, c548_ms(r.self), 100.0 * c548_ms(r.self) / window, c548_ms(r.ticks),
                  1000.0 * c548_ms(r.self) / static_cast<double>(r.calls));
    }
    if (g_c548.dropped != 0) {
        log::line("WARN card548: %s %u calls not keyed (table of %u full)", side, g_c548.dropped, kC548Slots);
    }
}

template <int K>
bool c548_call(void* comp, const void* pending, bool notify, const void* at_end) {
    const DWORD game_thread = g_c548_game_thread.load(std::memory_order_relaxed);
    const bool super_call = g_c548_depth > 0 && g_c548_stack[g_c548_depth].comp == comp;
    if (game_thread == 0 || GetCurrentThreadId() != game_thread || super_call || g_c548_depth >= kC548MaxDepth ||
        comp == nullptr) {
        return g_orig_c548_impl[K](comp, pending, notify, at_end);
    }
    C548Level& level = g_c548_stack[++g_c548_depth];
    level.comp = comp;
    level.child = 0;
    const int64_t t0 = c548_now();
    const bool r = g_orig_c548_impl[K](comp, pending, notify, at_end);
    const int64_t d = c548_now() - t0;
    const int64_t self = d - g_c548_stack[g_c548_depth].child;
    --g_c548_depth;
    g_c548_stack[g_c548_depth].child += d;
    if (g_c548_depth == 0) {
        g_c548.calls += 1;
        g_c548.ticks += d;
    }
    if (c548_player_comp(comp)) {
        c548_add(comp, d, self);
    }
    return r;
}

} // namespace

bool detour_c548_prim(void* comp, const void* pending, bool notify, const void* at_end) {
    return c548_call<0>(comp, pending, notify, at_end);
}

bool detour_c548_skinned(void* comp, const void* pending, bool notify, const void* at_end) {
    return c548_call<1>(comp, pending, notify, at_end);
}

bool detour_c548_skeletal(void* comp, const void* pending, bool notify, const void* at_end) {
    return c548_call<2>(comp, pending, notify, at_end);
}

// Game thread, per frame (c42_step).
void c548_tick(ULONGLONG now) {
    if (!g_c132_fx_log) {
        return;
    }
    g_c548_game_thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    const int64_t t = c548_now();
    if (g_c548.window_ms0 == 0) {
        g_c548.window_ms0 = now;
        g_c548.window_t0 = t;
        return;
    }
    if (now - g_c548.window_ms0 < kC548WindowMs) {
        return;
    }
    c548_report(now, t);
    g_c548 = C548State{}; // the stack stays: the tick runs outside any UpdateOverlapsImpl
    g_c548.window_ms0 = now;
    g_c548.window_t0 = t;
}

} // namespace ubw::hooks
