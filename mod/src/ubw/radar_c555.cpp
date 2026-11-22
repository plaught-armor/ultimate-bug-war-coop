// Card 555: no radar blip for a bug that is still underground after its spawn.
//
// The game marks a bug on the minimap from its spawn, while it is still in its spawn hole for 3 to 5 s (card 293:
// b182a, 3.1 to 4.7 s more than 300 uu below the ground). In co-op two players draw spawns in two areas, so both
// screens show more blips with no bug in sight (user report 2026-11-20). User choice 2026-11-21: hide a bug's blip
// until it has emerged.
//
// Each machine keeps its own table of young enemies. An enemy enters at AActor::DispatchBeginPlay (hidden at once,
// so no blip flashes before the first test). Every kC555StepMs the step traces each entry the way card 293 does (a
// visibility trace straight down from 2000 uu above the bug; a hit more than 300 uu above it means ground over the
// bug). The entry leaves the table, and the blip shows, at the first test that finds no ground over it, when the bug
// dies or is destroyed, or after kC555MaxMs (a bug that spawns under a roof or a bridge is not hidden for longer).
// ISTMinimapMarkerInterface::Execute_ShouldShowOnMinimap (detour_c416_show) answers false for a hidden entry; the
// minimap's own tick and gather then drop and skip its marker. Nothing is sent: both machines run the same rule on
// their own copy of the bug.

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

namespace {
constexpr uint32_t kC555Young = 256;      // enemies tested at once (a map load begins its placed enemies together);
                                          // a bug past this is not hidden (counted as full)
constexpr ULONGLONG kC555StepMs = 100;    // the latency target (card 555: player-visible delays under 100 ms)
constexpr ULONGLONG kC555MaxMs = 6000;    // longest hide; card 293 measured 3.1 to 4.7 s underground
constexpr double kC555BelowUu = 300.0;    // card 293 kC293BelowUu
constexpr double kC555TraceUp = 2000.0;   // card 293 kC293TraceUp

struct C555Entry {
    const void* actor; // compared only in detour_c416_show; the weak pointer is the validity check
    C31Weak weak;
    ULONGLONG begin_ms;
};
struct C555 {
    C555Entry e[kC555Young];
    uint32_t n;
    ULONGLONG next_ms;
    // per-map totals
    uint32_t added, emerged, aged, gone, full, traces;
    ULONGLONG longest_ms; // longest hide that ended with emerging
};
C555 g_c555{};

void c555_drop(uint32_t i) {
    g_c555.e[i] = g_c555.e[--g_c555.n]; // order does not matter
}

// True when ground lies more than kC555BelowUu over the bug.
bool c555_below(const void* pawn, const void* npc) {
    const Vec3 at = actor_location(npc);
    const void* hit_actor = nullptr; // the bug's own capsule or mesh is not ground
    const Vec3 hit = trace_channel(pawn, Vec3{at.x, at.y, at.z + kC555TraceUp}, at, kEccVisibility, &hit_actor);
    ++g_c555.traces;
    return hit_actor != npc && hit.z > at.z + kC555BelowUu; // card 293 c293_reasons, same test
}
} // namespace

// Both sides, after the actor's BeginPlay: a new enemy starts hidden.
void c555_begin_play(void* actor) {
    if (actor == nullptr || !c37_enemy(actor)) {
        return;
    }
    if (g_c555.n >= kC555Young) {
        ++g_c555.full;
        return;
    }
    C555Entry& e = g_c555.e[g_c555.n++];
    e.actor = actor;
    c31_weak_set(e.weak, actor);
    e.begin_ms = GetTickCount64();
    ++g_c555.added;
}

// Both sides, per tick: one ground test per entry every kC555StepMs.
void c555_step(void* engine) {
    const ULONGLONG now = GetTickCount64();
    if (g_c555.n == 0 || now < g_c555.next_ms) {
        return;
    }
    g_c555.next_ms = now + kC555StepMs;
    void* const world = g_get_game_world(engine);
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    const void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    uint32_t i = 0;
    for (uint32_t guard = 0; guard < kC555Young && i < g_c555.n; ++guard) {
        const C555Entry& e = g_c555.e[i];
        void* const npc = c31_weak_get(e.weak);
        float health = 0.0f;
        if (npc == nullptr || npc != e.actor || c25_dead(npc, &health) == 1) {
            ++g_c555.gone;
            c555_drop(i);
        } else if (now - e.begin_ms >= kC555MaxMs) {
            ++g_c555.aged;
            c555_drop(i);
        } else if (pawn != nullptr && !c555_below(pawn, npc)) { // no pawn to trace from: stays hidden until aged
            ++g_c555.emerged;
            g_c555.longest_ms = now - e.begin_ms > g_c555.longest_ms ? now - e.begin_ms : g_c555.longest_ms;
            c555_drop(i);
        } else {
            ++i;
        }
    }
}

// detour_c416_show, for every object that is not a teammate: true while the object is a hidden entry.
bool c555_hidden(const void* object) {
    for (uint32_t i = 0; i < g_c555.n; ++i) {
        if (g_c555.e[i].actor == object) {
            return true;
        }
    }
    return false;
}

void c555_map_end() {
    if (g_c555.added != 0) {
        log::line("card555: %s radar hide: %u enemies added, %u emerged (longest %llu ms), %u aged out at %llu ms, %u "
                  "gone or dead, %u past the %u-entry table, %u ground traces, %u still listed",
                  side_text(), g_c555.added, g_c555.emerged, static_cast<unsigned long long>(g_c555.longest_ms),
                  g_c555.aged, static_cast<unsigned long long>(kC555MaxMs), g_c555.gone, g_c555.full, kC555Young,
                  g_c555.traces, g_c555.n);
    }
    if (g_c555.full != 0) {
        log::line("WARN card555: %s radar hide table full: %u enemies kept their spawn blip", side_text(),
                  g_c555.full);
    }
    g_c555 = C555{};
}

} // namespace ubw::hooks
