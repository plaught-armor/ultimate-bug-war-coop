// Card 137 step 4 (card 132 step 2a): the client change log. Log only: under host authority the host's copy is the
// truth, so nothing is sent back (senior answer 1). On the client, each change to a placed, non-replicated map actor at
// one of the card 098 chokepoints (bHidden, actor or component collision, destroy, pickup disabled, Health to 0) logs
//   card137: client change <class> <name> <field> <old> -> <new> by <cause> caller exe+0x...
// with the cause class:
//   host row:  while a host frame is being applied (between the ledger's Before and Ran/NoHandler callbacks, which
//              bracket every client handler) or in the card 132 ext enforce pass, which re-applies host values
//   own input: while the client's own pawn's UADInteractorComponent::Interact runs
//   own view:  the significance callbacks below, which each machine runs for its own camera (the host's copy runs
//              them for the host's view; c244b205c card098 host rows, same callers): viewer-local by design
//   local sim: anything else, the client's own game logic changing a map actor the host owns (a gap to list)
// Card 149: the mod's own applies of host rows call the originals past these detours, so each apply site counts its
// change through c137_host_apply ("host row"). Card 282: each row has a phase, load before this map's first Pong and
// play from kC137InitMs after it; a play-time local-sim change logs as a WARN (the client acting as a second owner).
// Card 483: the client's own map init runs right after the first Pong (c244b205c: 132 of 135 WARN rows at +201..203 ms,
// every (class, caller) also on the host during its load), so that window is load.
// A (class, field, cause, phase) table counts every game-thread change; the first kLinesPerRow per row are logged, and
// the table is dumped at each map arm and at session end. Off-thread calls (the loader's constructor writes) are only
// counted. A full table counts the overflow and says so; nothing is dropped silently.

#include <stdio.h>
#include <string.h>

#include <atomic>

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

namespace {

constexpr int32_t kChangeRows = 128;
constexpr uint32_t kLinesPerRow = 3;
constexpr uint32_t kChangeLines = 300; // per map
constexpr size_t kClassChars = 64;
constexpr ULONGLONG kC137InitMs = 1000; // card 483: the client's map init after its first Pong
constexpr const char* kCauseText[] = {"host row", "own input", "own view", "local sim"};
enum Cause : uint8_t { kHostRow, kOwnInput, kOwnView, kLocalSim };
// Card 483: the significance callbacks' collision call return RVAs (logs/pdb_publics.txt).
constexpr unsigned long long kViewCallers[] = {
    0x5165410, // ASTDamageableActorBase::OnSignificanceChanged (c244b205c: Bin_Large "3 -> 0" far from the camera)
    0x51D1C61, // ASTCustomPickupBase::OnSignificanceChanged
};

struct ChangeRow {
    char cls[kClassChars];
    const char* field; // a string literal from the caller
    Cause cause;
    bool play; // card 282: after this map's first Pong
    uint32_t count;
};
struct ChangeState {
    ChangeRow row[kChangeRows];
    int32_t n;
    uint32_t overflow;
    uint32_t lines;
    uint32_t host_frame; // depth: host frame handlers and the ext enforce pass
    uint32_t own_input;  // depth: the own pawn's Interact
};

ChangeState g_change{}; // game thread
std::atomic<uint32_t> g_change_off_thread{0}; // card 149: chokepoint changes on another thread (loader constructor writes)

ChangeRow* change_row(const char* cls, const char* field, Cause cause, bool play) {
    for (int32_t i = 0; i < g_change.n; ++i) {
        ChangeRow& r = g_change.row[i];
        if (r.cause == cause && r.play == play && strcmp(r.field, field) == 0 && strcmp(r.cls, cls) == 0) {
            return &r;
        }
    }
    if (g_change.n >= kChangeRows) {
        if (g_change.overflow++ == 0) {
            log::line("WARN card137: client change table full (%d rows): %s %s not tabled, counted as overflow",
                      kChangeRows, cls, field);
        }
        return nullptr;
    }
    ChangeRow& r = g_change.row[g_change.n++];
    snprintf(r.cls, sizeof(r.cls), "%s", cls);
    r.field = field;
    r.cause = cause;
    r.play = play;
    return &r;
}

bool view_caller(unsigned long long caller) {
    for (const unsigned long long v : kViewCallers) {
        if (caller == v) {
            return true;
        }
    }
    return false;
}

Cause change_cause(unsigned long long caller) {
    if (g_change.host_frame != 0) {
        return kHostRow;
    }
    if (g_change.own_input != 0) {
        return kOwnInput;
    }
    // Card 483: inside this machine's significance update (the host journals the same writes as local, door_c95.cpp
    // c95_relay_collision); the Blueprint SetCollisionEnabled rows (exe+0x37C2C74) of BP_OnSignificanceChanged had no
    // caller of their own to list.
    return view_caller(caller) || g_c95_significance_depth > 0 ? kOwnView : kLocalSim;
}

} // namespace

void c137_host_frame(bool begin) {
    if (!g_client_session) {
        return;
    }
    if (begin) {
        ++g_change.host_frame;
    } else if (g_change.host_frame != 0) {
        --g_change.host_frame;
    }
}

void c137_own_input(bool begin) {
    if (begin) {
        ++g_change.own_input;
    } else if (g_change.own_input != 0) {
        --g_change.own_input;
    }
}

// Client: one change at a chokepoint. Only placed, non-replicated actors (the host-owned map state); old == new is
// not a change and is not counted.
void c137_client_change(const void* actor, const char* field, int32_t old_value, int32_t new_value,
                        unsigned long long caller) {
    if (!g_client_session || actor == nullptr || old_value == new_value) {
        return;
    }
    if (g_is_in_game_thread != nullptr && !g_is_in_game_thread()) {
        g_change_off_thread.fetch_add(1, std::memory_order_relaxed); // card 149: the table is game-thread only
        return;
    }
    if (!c50_placed(actor) || c98_replicates(actor)) {
        return;
    }
    const ObjectName cls = class_name(actor);
    const Cause cause = change_cause(caller);
    const ULONGLONG open = c120_pong_open_ms();
    const bool play = open != 0 && GetTickCount64() >= open + kC137InitMs;
    ChangeRow* const r = change_row(cls.text, field, cause, play);
    if (r == nullptr || ++r->count > kLinesPerRow || !note_event_cap(g_change.lines, kChangeLines, "card137 changes")) {
        return;
    }
    const Vec3 at = actor_location(actor);
    const bool gap = play && cause == kLocalSim; // card 282
    log::line("%scard137: client %schange %s %s at (%.0f, %.0f, %.0f) %s %d -> %d by %s caller exe+0x%llX",
              gap ? "WARN " : "", gap ? "play-time " : "", cls.text, object_name(actor).text, at.x, at.y, at.z, field,
              old_value, new_value, kCauseText[cause], caller);
}

// Card 149: a host row applied by the mod's own code past the detours (the originals are called directly).
void c137_host_apply(const void* actor, const char* field, int32_t old_value, int32_t new_value,
                     unsigned long long caller) {
    ++g_change.host_frame;
    c137_client_change(actor, field, old_value, new_value, caller);
    --g_change.host_frame;
}

// The table, one line per (class, field, cause); the local-sim rows are the gaps.
void c137_change_dump(const char* why) {
    if (!g_client_session ||
        (g_change.n == 0 && g_change.overflow == 0 && g_change_off_thread.load(std::memory_order_relaxed) == 0)) {
        return;
    }
    uint32_t by[sizeof(kCauseText) / sizeof(kCauseText[0])] = {};
    uint32_t sim_play = 0;
    for (int32_t i = 0; i < g_change.n; ++i) {
        const ChangeRow& r = g_change.row[i];
        by[r.cause] += r.count;
        sim_play += r.cause == kLocalSim && r.play ? r.count : 0u;
    }
    log::line("card137: client change totals (%s): %d rows, host row %u, own input %u, own view %u, local sim load %u, "
              "play %u, off thread %u, overflow %u", why, g_change.n, by[kHostRow], by[kOwnInput], by[kOwnView],
              by[kLocalSim] - sim_play,
              sim_play, g_change_off_thread.load(std::memory_order_relaxed), g_change.overflow);
    for (int32_t i = 0; i < g_change.n; ++i) {
        const ChangeRow& r = g_change.row[i];
        log::line("card137: client change row %s %s by %s (%s): %u", r.cls, r.field, kCauseText[r.cause],
                  r.play ? "play" : "load", r.count);
    }
}

void c137_change_arm_map() {
    c137_change_dump("map change");
    const uint32_t host_frame = g_change.host_frame; // a frame being handled across the map change keeps its bracket
    memset(&g_change, 0, sizeof(g_change));
    g_change_off_thread.store(0, std::memory_order_relaxed);
    g_change.host_frame = host_frame;
}

} // namespace ubw::hooks

namespace ubw::hooks {

// Card 137 step 6c (test only, log only): the host's hazardous zone (acid) damage. Each DamageActor on a player pawn
// logs the pawn's side and its health before and after, so a run shows the zone hurting the client's server copy.
namespace {
constexpr uint32_t kZoneLines = 40;
uint32_t g_zone_lines = 0;
uint32_t g_zone_calls[kSprintSides] = {};
} // namespace

ZoneDamageFn g_orig_c137_zone_damage = nullptr;

void detour_c137_zone_damage(void* zone, void* actor) {
    const uint32_t side = !g_client_session && actor != nullptr ? sprint_side(actor) : kSideOther;
    if (side == kSideOther) {
        g_orig_c137_zone_damage(zone, actor);
        return;
    }
    const auto* const pawn = static_cast<const uint8_t*>(actor);
    void* comp = nullptr;
    const float before = c21_health(pawn, &comp);
    g_orig_c137_zone_damage(zone, actor);
    const float after = c21_health(pawn, &comp);
    ++g_zone_calls[side];
    if (note_event_cap(g_zone_lines, kZoneLines, "card137 zone damage")) {
        log::line("card137: host zone %s DamageActor on %s %s (side %u, call %u): health %.1f -> %.1f",
                  class_name(zone).text, class_name(actor).text, object_name(actor).text, side, g_zone_calls[side],
                  before, after);
    }
}

} // namespace ubw::hooks
