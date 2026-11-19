// Card 503 (diag): why an encounter's wave never ends (FED_C5M2 Array_SO4: SetWaves C_1 started at the Array_SO3
// press and never ended in any c247 run). Host only; the client runs no encounters (card 006).
//
// Hooks on ASTEncounterManagerBase (each takes the wave the event is for):
// - OnWaveNPCSpawned(wave, npc), OnWaveNPCKilled(wave, npc): the wave's spawned and killed counts, and its alive set
//   (weak refs, existence-based: an NPC is alive while it is in the set); one row at a wave's first spawn only, the
//   counts ride on the wave rows below;
// - OnWaveSpawnComplete(wave): the wave has spawned all it will;
// - OnWaveEnd(wave), TriggerNextWave(), BP_OnEncounterEnded(): wave and encounter ends;
// and USTEncounterWaveBase::OnSpawnCancelled(result): a spawn the spawner gave up on (hypothesis A, card 111 budget
// past what the spawn points deliver).
// Each row carries GetCurrentWaveID. When `clear ... kill` leaves 0 in its radius while a wave is still open,
// c503_clear_report lists that wave's alive NPCs with class, health and distance from the host pawn (hypothesis B:
// bugs out of the radius; a class clear spares; C: a kill the wave never got).

#include "ubw/hooks_internal.h"

#include <math.h>
#include <string.h>

namespace ubw::hooks {

C503WaveNpcFn g_orig_c503_spawned = nullptr;
C503WaveNpcFn g_orig_c503_killed = nullptr;
C503WaveFn g_orig_c503_spawn_complete = nullptr;
C503WaveFn g_orig_c503_wave_end = nullptr;
EncounterFn g_orig_c503_next_wave = nullptr;
EncounterFn g_orig_c503_encounter_ended = nullptr;
C503CancelFn g_orig_c503_cancelled = nullptr;
C503WaveIdFn g_c503_wave_id = nullptr;

namespace {

constexpr int32_t kC503Waves = 32;       // open waves tracked at once (c111c: 23 waves on FED_C2M1, map-wide)
constexpr int32_t kC503Alive = 64;       // alive NPCs per wave (scaled ArrayBugmix 10 + Troopers_Large 13)
constexpr uint32_t kC503Lines = 600;     // per map
constexpr int32_t kC503ReportNpcs = 12;  // alive rows per clear report

struct C503Wave {
    C31Weak wave;
    C31Weak manager;
    int32_t spawned;
    int32_t killed;
    int32_t cancelled;
    bool spawn_complete;
    int32_t alive_n;
    C31Weak alive[kC503Alive];
};

struct C503State {
    uintptr_t world;  // a new world clears the table
    int32_t n;
    uint32_t lines;
    C503Wave w[kC503Waves];
};
C503State g_c503{};

bool c503_line() {
    return note_event_cap(g_c503.lines, kC503Lines, "card503 wave");
}

// The tracked row of `wave`, added when `add`; null when absent (or the table is full: one WARN through the cap).
// A manager in a new world clears the table first; a null manager (OnSpawnCancelled knows only the wave) does not.
C503Wave* c503_find(void* wave, void* manager, bool add) {
    const void* const world = manager != nullptr ? g_actor_get_world(manager) : nullptr;
    if (world != nullptr && reinterpret_cast<uintptr_t>(world) != g_c503.world) {
        g_c503 = C503State{};
        g_c503.world = reinterpret_cast<uintptr_t>(world);
    }
    for (int32_t i = 0; i < g_c503.n; ++i) {
        if (c31_weak_get(g_c503.w[i].wave) == wave) {
            return &g_c503.w[i];
        }
    }
    if (!add || wave == nullptr) {
        return nullptr;
    }
    if (g_c503.n >= kC503Waves) {
        if (c503_line()) {
            log::line("WARN card503: host wave table full (%d), %s not tracked", kC503Waves, object_name(wave).text);
        }
        return nullptr;
    }
    C503Wave& w = g_c503.w[g_c503.n++];
    w = C503Wave{};
    c31_weak_set(w.wave, wave);
    c31_weak_set(w.manager, manager);
    return &w;
}

void c503_drop(int32_t i) {
    g_c503.w[i] = g_c503.w[--g_c503.n];
}

int32_t c503_wave_id(void* manager) {
    return g_c503_wave_id != nullptr && manager != nullptr ? g_c503_wave_id(manager) : -2;
}

void c503_row(const char* what, void* manager, void* wave, const C503Wave* w, const void* npc) {
    if (!c503_line()) {
        return;
    }
    log::line("card503: host t=%llu %s: %s wave %s (id %d)%s%s; spawned %d, killed %d, alive %d, cancelled %d, "
              "spawn complete %d", static_cast<unsigned long long>(GetTickCount64()), what, object_name(manager).text,
              wave != nullptr ? object_name(wave).text : "-", c503_wave_id(manager), npc != nullptr ? ", npc " : "",
              npc != nullptr ? object_name(npc).text : "", w != nullptr ? w->spawned : -1,
              w != nullptr ? w->killed : -1, w != nullptr ? w->alive_n : -1, w != nullptr ? w->cancelled : -1,
              w != nullptr && w->spawn_complete ? 1 : 0);
}

void c503_alive_remove(C503Wave& w, const void* npc) {
    for (int32_t i = 0; i < w.alive_n; ++i) {
        if (c31_weak_get(w.alive[i]) == npc) {
            w.alive[i] = w.alive[--w.alive_n];
            return;
        }
    }
}

} // namespace

void detour_c503_spawned(void* manager, void* wave, void* npc) {
    g_orig_c503_spawned(manager, wave, npc);
    C503Wave* const w = is_net_client(manager) ? nullptr : c503_find(wave, manager, true);
    if (w == nullptr) {
        return;
    }
    ++w->spawned;
    if (w->alive_n < kC503Alive) {
        c31_weak_set(w->alive[w->alive_n++], npc);
    }
    if (w->spawned == 1) {
        c503_row("first NPC spawned", manager, wave, w, npc); // per-NPC counts ride on the wave rows
    }
}

void detour_c503_killed(void* manager, void* wave, void* npc) {
    C503Wave* const w = is_net_client(manager) ? nullptr : c503_find(wave, manager, true);
    if (w != nullptr) {
        ++w->killed;
        c503_alive_remove(*w, npc);
    }
    g_orig_c503_killed(manager, wave, npc);
}

void detour_c503_spawn_complete(void* manager, void* wave) {
    C503Wave* const w = is_net_client(manager) ? nullptr : c503_find(wave, manager, true);
    if (w != nullptr) {
        w->spawn_complete = true;
        c503_row("wave spawn complete", manager, wave, w, nullptr);
    }
    g_orig_c503_spawn_complete(manager, wave);
}

void detour_c503_wave_end(void* manager, void* wave) {
    if (!is_net_client(manager)) {
        C503Wave* const w = c503_find(wave, manager, false);
        c503_row("wave end", manager, wave, w, nullptr);
        if (w != nullptr) {
            c503_drop(static_cast<int32_t>(w - g_c503.w));
        }
    }
    g_orig_c503_wave_end(manager, wave);
}

void detour_c503_next_wave(void* manager) {
    if (!is_net_client(manager)) {
        c503_row("TriggerNextWave", manager, nullptr, nullptr, nullptr);
    }
    g_orig_c503_next_wave(manager);
}

void detour_c503_encounter_ended(void* manager) {
    if (!is_net_client(manager)) {
        c503_row("encounter ended", manager, nullptr, nullptr, nullptr);
    }
    g_orig_c503_encounter_ended(manager);
}

void detour_c503_cancelled(void* wave, const void* result) {
    void* const manager = read_ptr(wave, kObjectOuter); // card 112: waves are outered to a Package, not the manager
    C503Wave* const w = g_client_session ? nullptr : c503_find(wave, nullptr, false);
    if (w != nullptr) {
        ++w->cancelled;
    }
    if (!g_client_session && c503_line()) {
        log::line("card503: host t=%llu spawn cancelled: wave %s (outer %s), tracked %d, cancelled %d",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(wave).text,
                  object_name(manager).text, w != nullptr ? 1 : 0, w != nullptr ? w->cancelled : -1);
    }
    g_orig_c503_cancelled(wave, result);
}

// Host, after `clear ... kill` left 0 in its radius: each still-open wave's alive NPCs (the ones its kill delegate
// has not reported), so a wave held open by bugs out of range, a spared class or an uncredited kill shows by name.
void c503_clear_report(uint32_t seq, const void* pawn) {
    const Vec3 at = actor_location(pawn);
    for (int32_t i = 0; i < g_c503.n; ++i) {
        C503Wave& w = g_c503.w[i];
        void* const wave = c31_weak_get(w.wave);
        void* const manager = c31_weak_get(w.manager);
        c503_row("open at clear", manager, wave, &w, nullptr);
        for (int32_t k = 0; k < w.alive_n && k < kC503ReportNpcs; ++k) {
            const void* const npc = c31_weak_get(w.alive[k]);
            if (npc == nullptr || !c503_line()) {
                continue;
            }
            const Vec3 p = actor_location(npc);
            const double d = sqrt((p.x - at.x) * (p.x - at.x) + (p.y - at.y) * (p.y - at.y) +
                                  (p.z - at.z) * (p.z - at.z));
            log::line("card503: host cmd %u open wave %s alive %s (%s) health %g at %.0f uu (%.0f, %.0f, %.0f)", seq,
                      object_name(wave).text, object_name(npc).text, class_name(npc).text, c23_set_health(npc), d,
                      p.x, p.y, p.z);
        }
        int32_t freed = 0;
        for (int32_t k = 0; k < w.alive_n; ++k) {
            freed += c31_weak_get(w.alive[k]) == nullptr ? 1 : 0;
        }
        if (freed != 0 && c503_line()) {
            log::line("card503: host cmd %u open wave %s: %d alive entries freed with no kill event", seq,
                      object_name(wave).text, freed);
        }
    }
}

} // namespace ubw::hooks
