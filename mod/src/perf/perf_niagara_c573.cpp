// Card 573 (performance mod family), rank 2: FNiagaraSystemInstance::TickInstanceParameters_GameThread counts, before
// any cut.
//
// Callers (exe call scan, b457): FNiagaraSystemInstance::Tick_GameThread (exe+0x48D2A67), which
// FNiagaraSystemSimulation::Tick_GameThread_Internal calls for each system instance each frame (exe+0x48D30E3,
// +0x48D33FE) and Spawn_GameThread calls for new instances (exe+0x48CFA7C); and ReInitInternal (exe+0x48CA0CC) on a
// reset. So the call count is the live system instance count per frame. The function reads the instance's attach
// component (TWeakObjectPtr at instance +0x48, IsValid then Get) and copies its ComponentToWorld (+0x1E0) and owner
// velocity into the instance parameters.
//
// A lossless cut may only skip instances whose output nothing shows. This probe counts, per 10 s window and per
// Niagara system asset (UNiagaraComponent::Asset), on the game thread:
//   - calls and inclusive time;
//   - calls with no live attach component;
//   - calls whose component or owner is hidden (AActor bHidden, or the component's bVisible clear);
//   - calls whose owner is a dead or dying NPC.
// The counts change no result. Gate: on by default, UBWCOOP_C573_OFF=1 removes the hook (with the bone counts).

#include "ubw/hooks_internal.h"

#include <algorithm> // std::sort
#include <atomic>

namespace ubw::hooks {

NiagaraParamsFn g_orig_c573_niagara = nullptr;

namespace {

constexpr ULONGLONG kC573nWindowMs = 10000;
constexpr uint32_t kC573nSlots = 256; // power of two; one row per system asset
constexpr uint32_t kC573nProbe = 16;
constexpr int32_t kC573nTopRows = 12;
constexpr size_t kC573nAttach = 0x48;      // FNiagaraSystemInstance::AttachComponent (TWeakObjectPtr)
constexpr uint8_t kC573nStateDying = 3;    // ESTBaseNPCState: Dying 3, Dead 4, Overkilled 5
constexpr uint8_t kC573nStateOverkilled = 5;

struct C573nRow {
    const void* asset; // nullptr = empty slot
    uint32_t calls;
    uint32_t hidden;
    uint32_t dead;
    int64_t ticks;
    char name[64];  // asset
    char owner[64]; // first owner class seen this window
};

struct C573nState {
    C573nRow row[kC573nSlots];
    uint32_t calls;
    uint32_t no_component; // the weak pointer is stale or empty
    uint32_t dropped;
    uint32_t frames;
    int64_t ticks;
    int64_t window_t0;
    ULONGLONG window_ms0;
};
C573nState g_c573n{};
std::atomic<uint32_t> g_c573n_other_thread{0};
std::atomic<DWORD> g_c573n_game_thread{0};

int64_t c573n_now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

double c573n_ms(int64_t ticks) {
    static const double kPerMs = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart) / 1000.0;
    }();
    return static_cast<double>(ticks) / kPerMs;
}

C573nRow* c573n_row(const void* asset, const void* owner) {
    uint32_t i = static_cast<uint32_t>((reinterpret_cast<uintptr_t>(asset) * 0x9E3779B97F4A7C15ULL) >> 56) &
                 (kC573nSlots - 1);
    for (uint32_t p = 0; p < kC573nProbe; ++p, i = (i + 1) & (kC573nSlots - 1)) {
        C573nRow& r = g_c573n.row[i];
        if (r.asset == asset) {
            return &r;
        }
        if (r.asset == nullptr) {
            r.asset = asset;
            snprintf(r.name, sizeof(r.name), "%s", object_name(asset).text); // once per asset per window
            snprintf(r.owner, sizeof(r.owner), "%s", owner != nullptr ? class_name(owner).text : "-");
            return &r;
        }
    }
    ++g_c573n.dropped;
    return nullptr;
}

bool c573n_dead(const void* owner) {
    if (owner == nullptr || g_npc_class == nullptr || !is_a(owner, g_npc_class())) {
        return false;
    }
    const uint8_t state = static_cast<const uint8_t*>(owner)[kNpcBaseState];
    return state >= kC573nStateDying && state <= kC573nStateOverkilled;
}

void c573n_count(const void* instance, int64_t d) {
    const void* const comp = g_weak_get != nullptr ? g_weak_get(static_cast<const uint8_t*>(instance) + kC573nAttach)
                                                   : nullptr;
    if (comp == nullptr) {
        ++g_c573n.no_component;
        return;
    }
    const void* const owner = read_ptr(comp, kComponentOwner);
    const void* const asset = read_ptr(comp, kNiagaraAsset);
    C573nRow* const r = c573n_row(asset != nullptr ? asset : comp, owner);
    if (r == nullptr) {
        return;
    }
    const bool owner_hidden =
        owner != nullptr && (static_cast<const uint8_t*>(owner)[kActorHiddenByte] & kActorHiddenBit) != 0;
    const bool comp_hidden = (static_cast<const uint8_t*>(comp)[kSceneVisibleByte] & kSceneVisibleBit) == 0;
    ++r->calls;
    r->hidden += owner_hidden || comp_hidden ? 1u : 0u;
    r->dead += c573n_dead(owner) ? 1u : 0u;
    r->ticks += d;
}

void c573n_report(ULONGLONG now, int64_t t) {
    const char* const s = side();
    const double window = c573n_ms(t - g_c573n.window_t0);
    uint32_t hidden = 0;
    uint32_t dead = 0;
    for (const C573nRow& r : g_c573n.row) {
        hidden += r.hidden;
        dead += r.dead;
    }
    log::line("card573: %s t=%llu Niagara TickInstanceParameters_GameThread total: %u game-thread calls over %u frames "
              "(%.1f per frame), %.1f ms (%.2f%% of %.0f ms), %u other-thread calls, %u with no attach component; "
              "hidden component or owner %u calls, dead owner %u calls",
              s, static_cast<unsigned long long>(now), g_c573n.calls, g_c573n.frames,
              g_c573n.frames != 0 ? static_cast<double>(g_c573n.calls) / g_c573n.frames : 0.0, c573n_ms(g_c573n.ticks),
              100.0 * c573n_ms(g_c573n.ticks) / window, window,
              g_c573n_other_thread.exchange(0, std::memory_order_relaxed), g_c573n.no_component, hidden, dead);
    C573nRow* top[kC573nTopRows] = {};
    int32_t n = 0;
    for (C573nRow& r : g_c573n.row) {
        if (r.asset == nullptr) {
            continue;
        }
        if (n < kC573nTopRows) {
            top[n++] = &r;
        } else if (r.ticks > top[n - 1]->ticks) {
            top[n - 1] = &r;
        } else {
            continue;
        }
        std::sort(top, top + n, [](const C573nRow* x, const C573nRow* y) { return x->ticks > y->ticks; });
    }
    for (int32_t i = 0; i < n; ++i) {
        const C573nRow& r = *top[i];
        log::line("card573: %s t=%llu Niagara params top %d: %s (owner %s): %u calls, %.2f ms, %.1f us mean; hidden %u, "
                  "dead owner %u",
                  s, static_cast<unsigned long long>(now), i + 1, r.name, r.owner, r.calls, c573n_ms(r.ticks),
                  1000.0 * c573n_ms(r.ticks) / static_cast<double>(r.calls), r.hidden, r.dead);
    }
    if (g_c573n.dropped != 0) {
        log::line("WARN card573: %s %u Niagara calls not keyed (table of %u full)", s, g_c573n.dropped, kC573nSlots);
    }
}

} // namespace

void detour_c573_niagara(void* instance, float dt) {
    const DWORD game_thread = g_c573n_game_thread.load(std::memory_order_relaxed);
    if (game_thread == 0 || instance == nullptr) {
        g_orig_c573_niagara(instance, dt);
        return;
    }
    if (GetCurrentThreadId() != game_thread) {
        g_orig_c573_niagara(instance, dt);
        g_c573n_other_thread.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const int64_t t0 = c573n_now();
    g_orig_c573_niagara(instance, dt);
    const int64_t d = c573n_now() - t0;
    ++g_c573n.calls;
    g_c573n.ticks += d;
    c573n_count(instance, d);
}

// Game thread, per frame (perf_tick).
void c573_niagara_tick(ULONGLONG now) {
    if (!g_c573_on) {
        return;
    }
    g_c573n_game_thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    const int64_t t = c573n_now();
    if (g_c573n.window_ms0 == 0) {
        g_c573n.window_ms0 = now;
        g_c573n.window_t0 = t;
        return;
    }
    ++g_c573n.frames;
    if (now - g_c573n.window_ms0 < kC573nWindowMs) {
        return;
    }
    c573n_report(now, t);
    g_c573n = C573nState{};
    g_c573n.window_ms0 = now;
    g_c573n.window_t0 = t;
}

} // namespace ubw::hooks
