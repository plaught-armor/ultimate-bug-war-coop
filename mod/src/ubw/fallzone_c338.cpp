// Card 338: a fall zone (ASTFallZonesBase, BP_FallZones_C) has one reposition timer and one end timer, so a second
// player replaces the first player's pending call.
//
// The game (static trace, b16):
// - OnBeginOverlap (0x05161B40) passes its gates, disables the player controller's input (ForceInputEnabled false),
//   cancels abilities, adds Tag_Actor_InFallZone, then arms zone+0x3A8: RepositionPlayer(weak pawn) after zone+0x358.
//   A second overlap re-arms the same handle for the second pawn, so the first is never repositioned.
// - RepositionPlayer (0x0516BA90) clears +0x3A8 and calls USTCommonUtilityLibrary::RepositionPlayer. That call adds a
//   tag, sets the movement mode to 5 (flying) and moves the pawn to the zone's point. It also gives player 0's
//   controller (UGameplayStatics::GetPlayerController(world, 0)) the zone's control rotation. It then arms zone+0x3B0:
//   EndReposition(weak pawn) after zone+0x370. A second reposition inside that window drops the first pawn's end.
// - EndReposition (0x05155620) removes Tag_Actor_InFallZone and calls PostReposition. PostReposition removes the
//   reposition tag, turns on input for GetSTPlayerController(zone) (player 0) and sets the movement mode to walking.
//
// Both timer callbacks run outside any overlap scope, so for a client's pawn, player 0 is the host. The host's
// camera takes the zone's rotation, and the host's input is turned on in place of the client's.
//
// Fix, host:
// - Both callbacks run in the pawn's acting scope (card 102), so player 0 is that pawn's controller.
// - An overlap by a second player while another pawn's reposition is pending runs that reposition first.
// - A reposition while another pawn's end is pending runs that end first.
// Fix, client:
// - The client's own copy of the zone runs OnBeginOverlap too (there is no authority gate). With another
//   player's pawn as the overlapper, its player 0 is the client's own pawn, so the original is skipped. The host
//   owns other players' fall zone reaction.
//
// The zone keeps the end in zone+0x3B0 (FTimerHandle); the table below mirrors which pawn each handle is for.
//
// Rows:
//   card338: host t=<ms> zone <zone> at (x, y, z): <pawn> repositioned to (x, y, z)[, pending, repositioned before <pawn2> armed],
//            rates <+0x358> <+0x370> s
//   card338: host t=<ms> <pawn> input <0|1>, moved <n> uu since the reposition, movement mode <m>   (5 s later)
//   card338: client t=<ms> zone <zone>: overlap by <pawn>, not the own pawn, skipped

#include "ubw/hooks_internal.h"

#include <string.h>

namespace ubw::hooks {

C338OverlapFn g_orig_c338_overlap = nullptr;
C338WeakFn g_orig_c338_reposition = nullptr;
C338WeakFn g_orig_c338_end = nullptr;

namespace {

constexpr size_t kZoneRepositionTimer = 0x3A8; // FTimerHandle: RepositionPlayer
constexpr size_t kZoneRepositionRate = 0x358;  // float s: overlap to reposition
constexpr size_t kZoneEndRate = 0x370;         // float s: reposition to end
constexpr size_t kPcInputFlags = 0x618;        // APlayerController: bInputEnabled is bit 3 (Enable/DisableInput)
constexpr uint32_t kPcInputEnabled = 0x8;
constexpr uint32_t kC338Zones = 32;            // zones with a pending call at once
constexpr uint32_t kC338Checks = 8;            // pending 5 s rows
constexpr ULONGLONG kC338CheckMs = 5000;
constexpr uint32_t kC338Rows = 64;             // card338 rows per session (zone repositions are rare)

struct C338Zone {
    C31Weak zone;
    C31Weak reposition; // pawn the zone's RepositionPlayer timer is armed for
    C31Weak end;        // pawn the zone's EndReposition timer is armed for
};

struct C338Check {
    C31Weak pawn;
    Vec3 at;           // where the reposition left it
    ULONGLONG due_ms;  // 0 = free
};

C338Zone g_c338_zones[kC338Zones] = {};
C338Check g_c338_checks[kC338Checks] = {};
uint32_t g_c338_rows = 0;

void* weak_obj(const C31Weak& w) {
    return w.serial != 0 ? c31_weak_get(w) : nullptr;
}

// The zone's row, or a free one (zone gone or no pending pawn) when `make`; null when the table is full.
C338Zone* c338_zone(void* zone, bool make) {
    C338Zone* free_row = nullptr;
    for (uint32_t i = 0; i < kC338Zones; ++i) {
        void* const z = weak_obj(g_c338_zones[i].zone);
        if (z == zone) {
            return &g_c338_zones[i];
        }
        // Free: its zone is gone, or neither of its timers is pending for a live pawn.
        const bool idle = z == nullptr || (weak_obj(g_c338_zones[i].reposition) == nullptr &&
                                           weak_obj(g_c338_zones[i].end) == nullptr);
        free_row = free_row == nullptr && idle ? &g_c338_zones[i] : free_row;
    }
    if (!make || free_row == nullptr) {
        if (make) {
            log::line("WARN card338: zone table full (%u), %s not tracked", kC338Zones, object_name(zone).text);
        }
        return nullptr;
    }
    *free_row = C338Zone{};
    c31_weak_set(free_row->zone, zone);
    return free_row;
}

C31Weak weak_of(uint64_t raw) {
    C31Weak w{};
    memcpy(&w, &raw, sizeof(w));
    return w;
}

uint64_t raw_of(const C31Weak& w) {
    uint64_t raw = 0;
    memcpy(&raw, &w, sizeof(raw));
    return raw;
}

// One pending 5 s row per pawn: a second reposition (two overlapping zones) re-arms the pawn's row from the newer
// point, so the row never measures from a point the pawn already left.
void c338_check_add(void* pawn) {
    C338Check* slot = nullptr;
    for (uint32_t i = 0; i < kC338Checks; ++i) {
        C338Check& c = g_c338_checks[i];
        if (c.due_ms != 0 && weak_obj(c.pawn) == pawn) {
            slot = &c;
            break;
        }
        slot = slot == nullptr && c.due_ms == 0 ? &c : slot;
    }
    if (slot == nullptr) {
        log::line("WARN card338: check table full (%u), %s has no 5 s row", kC338Checks, object_name(pawn).text);
        return;
    }
    c31_weak_set(slot->pawn, pawn);
    slot->at = actor_location(pawn);
    slot->due_ms = GetTickCount64() + kC338CheckMs;
}

// The pawn's end, in its acting scope; clears the zone row's end when it names the pawn.
void c338_end_for(void* zone, C338Zone* row, const C31Weak& pawn_weak) {
    void* const pawn = weak_obj(pawn_weak);
    const C102Scope saved = c102_enter(pawn, kC102Overlap, zone);
    g_orig_c338_end(zone, raw_of(pawn_weak));
    c102_leave(saved);
    if (row != nullptr && weak_obj(row->end) == pawn) {
        row->end = C31Weak{};
    }
}

// The pawn's reposition, in its acting scope; `before` names the pawn whose arm forced it (null: the zone's timer).
void c338_reposition_for(void* zone, const C31Weak& pawn_weak, const void* before) {
    void* const pawn = weak_obj(pawn_weak);
    C338Zone* const row = c338_zone(zone, true);
    void* const pending_end = row != nullptr ? weak_obj(row->end) : nullptr;
    if (pending_end != nullptr && pending_end != pawn) {
        c338_end_for(zone, row, row->end); // the zone's one end timer is about to be re-armed for this pawn
    }
    const C102Scope saved = c102_enter(pawn, kC102Overlap, zone);
    g_orig_c338_reposition(zone, raw_of(pawn_weak));
    c102_leave(saved);
    if (row != nullptr) {
        row->reposition = weak_obj(row->reposition) == pawn ? C31Weak{} : row->reposition;
        if (pawn != nullptr) {
            c31_weak_set(row->end, pawn);
        }
    }
    if (pawn == nullptr) {
        return;
    }
    c338_check_add(pawn);
    if (!note_event_cap(g_c338_rows, kC338Rows, "card338")) {
        return;
    }
    const auto* const z = static_cast<const uint8_t*>(zone);
    const Vec3 at = actor_location(pawn);
    const Vec3 zone_at = actor_location(zone); // every zone is named BP_FallZones_C; its location tells them apart
    char forced[160] = "";
    if (before != nullptr) {
        snprintf(forced, sizeof(forced), ", pending, repositioned before %s armed", object_name(before).text);
    }
    log::line("card338: host t=%llu zone %s at (%.0f, %.0f, %.0f): %s repositioned to (%.0f, %.0f, %.0f)%s, rates "
              "%.2f %.2f s",
              static_cast<unsigned long long>(GetTickCount64()), object_name(zone).text, zone_at.x, zone_at.y,
              zone_at.z, object_name(pawn).text, at.x, at.y, at.z, forced, read_f32(z, kZoneRepositionRate),
              read_f32(z, kZoneEndRate));
}

} // namespace

void detour_c338_overlap(void* zone, void* comp, void* other, void* other_comp, int32_t body, bool sweep,
                         const void* hit) {
    const bool player = other != nullptr && g_player_character_class != nullptr && is_a(other, g_player_character_class());
    if (player && g_client_session && sprint_side(other) != kSideClientOwn) {
        if (note_event_cap(g_c338_rows, kC338Rows, "card338")) {
            log::line("card338: client t=%llu zone %s: overlap by %s, not the own pawn, skipped",
                      static_cast<unsigned long long>(GetTickCount64()), object_name(zone).text, object_name(other).text);
        }
        return;
    }
    C338Zone* const row = player && !g_client_session ? c338_zone(zone, true) : nullptr;
    if (row != nullptr) {
        void* const pending = weak_obj(row->reposition);
        if (pending != nullptr && pending != other) {
            const C31Weak first = row->reposition;
            c338_reposition_for(zone, first, other);
        }
    }
    const uint64_t armed = read_u64(zone, kZoneRepositionTimer);
    g_orig_c338_overlap(zone, comp, other, other_comp, body, sweep, hit);
    const uint64_t now_armed = read_u64(zone, kZoneRepositionTimer);
    if (row != nullptr && now_armed != 0 && now_armed != armed) {
        c31_weak_set(row->reposition, other); // the original bound player 0 of the overlap scope: this pawn
    }
}

void detour_c338_reposition(void* zone, uint64_t pawn) {
    if (g_client_session) {
        g_orig_c338_reposition(zone, pawn);
        return;
    }
    c338_reposition_for(zone, weak_of(pawn), nullptr);
}

void detour_c338_end(void* zone, uint64_t pawn) {
    if (g_client_session) {
        g_orig_c338_end(zone, pawn);
        return;
    }
    c338_end_for(zone, c338_zone(zone, false), weak_of(pawn));
}

// Host tick: the 5 s row after each reposition.
void c338_step() {
    const ULONGLONG now = GetTickCount64();
    for (uint32_t i = 0; i < kC338Checks; ++i) {
        C338Check& c = g_c338_checks[i];
        if (c.due_ms == 0 || now < c.due_ms) {
            continue;
        }
        c.due_ms = 0;
        void* const pawn = weak_obj(c.pawn);
        if (pawn == nullptr) {
            log::line("WARN card338: host t=%llu a repositioned pawn is gone before its 5 s row",
                      static_cast<unsigned long long>(now));
            continue;
        }
        const auto* const pc = static_cast<const uint8_t*>(read_ptr(pawn, kPawnController));
        const auto* const cmc = static_cast<const uint8_t*>(read_ptr(pawn, kCharacterMovement));
        log::line("card338: host t=%llu %s input %d, moved %.0f uu since the reposition, movement mode %d",
                  static_cast<unsigned long long>(now), object_name(pawn).text,
                  pc != nullptr ? ((static_cast<uint32_t>(read_i32(pc, kPcInputFlags)) & kPcInputEnabled) != 0 ? 1 : 0) : -1,
                  vec_dist(actor_location(pawn), c.at), cmc != nullptr ? cmc[kCmcMovementMode] : -1);
    }
}

} // namespace ubw::hooks
