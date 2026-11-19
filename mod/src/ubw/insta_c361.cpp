// Card 361 (always on, both sides): USTCombatUtilityLibrary::InstaKillTarget. Every process runs its own overlaps,
// timers and hazard moves, so without a guard a client kills its own copy of a trooper by itself. The host's kill of
// any trooper reaches the client by the player death sync (HandleDeathEvent, c43_death, the Down frame), so a client
// never InstaKills a player-class actor. Non-player targets (NPCs, corpses) run the original on both sides.
// The mod's own calls (g_insta_kill) use the trampoline and are not seen here.
// Rows:
//   card361: client t=<ms> InstaKill of <pawn> (<side>) by <caller>: skipped; the host's kill comes by the Down frame
//   WARN card361: client t=<ms> InstaKill of <pawn> (<side>) by exe+0x<rva>: caller not traced; skipped
//   card361: client t=<ms> InstaKill of <class> by <caller>: runs (first per caller per map)
//
// Card 321 hazard half (host): ASTHazardousZoneBase::TickMoveToNewPostion (0x05173AA0) crushes only player 0. With
// bCrushCharactersInPath (zone+0x315) set, path 1 is GetPlayerCharacter(world, 0), IsOverlappingActor(zone+0x320,
// p0), CrushActor(zone, p0, &dir), the direction living only in xmm8/6/7 and the caller's stack. When a server copy
// overlaps zone+0x320, the original runs in that copy's acting scope (card 102), so card 099's remap answers player 0
// with the copy and the game's own gate, overlap test, direction and CrushActor run for it. CrushActor has no repeat
// guard: a copy crushed by a zone is not offered to it again until it leaves the zone or the roster. When the host's
// own pawn is in the zone too, ticks alternate between the copy and the host.
//   card361: <side> t=<ms> hazard <zone> (<class>) moves, crush <0|1>   (first move tick per zone per map)
//   card361: host t=<ms> hazard <zone> crushed <pawn> (server-copy)
//
// Card 362 (client, log only): each card 361 skip of a player pawn waits for the host's Down frame for that pawn (any
// state but Up, matched by its NetGUID entry id). A frame within 3 s before or after the skip clears it; else one row.
// Guards the callers the host may never run (a blueprint execInstaKillTarget only the client replays, an untraced one).
//   WARN card362: client skip of <pawn> by <caller> had no Down frame in 3 s
//   WARN card362: client skip table of 4 full: skip of <pawn> by <caller> not watched

#include "ubw/hooks_internal.h"

#include <intrin.h>

namespace ubw::hooks {

C361HazardTickFn g_orig_c361_hazard_tick = nullptr;
bool g_c361_oob_expiry = false;

namespace {

constexpr size_t kZoneCrushInPath = 0x315;          // ASTHazardousZoneBase::bCrushCharactersInPath
constexpr size_t kZoneHazardComponent = 0x320;      // ASTHazardousZoneBase::HazardousZone (UPrimitiveComponent*)
constexpr unsigned long long kCrushActorRet = 0x515270F; // return address of CrushActor's InstaKill call
constexpr uint32_t kC361Rows = 64;                  // card361 rows per map, per side
constexpr int32_t kC361Callers = 16;                // distinct non-player callers logged per map
constexpr int32_t kC361Zones = 64;                  // zones whose first move tick is logged per map
constexpr int32_t kC361Crushed = 16;                // (zone, copy) pairs crushed and still inside
constexpr int32_t kC362Skips = 4;                   // card 362: skips waiting for their Down frame
constexpr int32_t kC362Frames = 4;                  // card 362: recent Down frames (one may beat the skip)
constexpr unsigned long long kC362WaitMs = 3000;    // card 362: the Down frame's window around the skip

struct C361Caller {
    unsigned long long ret; // return RVA (site + 5)
    const char* name;
};
// InstaKillTarget call sites in the exe (E8 site + 5). The two tail jumps (TryTakeCorpseTicket+0x63, KillOwningActor)
// return into their own callers; KillOwningActor is named by g_c361_oob_expiry (card 360's detour).
const C361Caller kC361CallerTable[] = {
    {0x50421B0, "execInstaKillTarget (blueprint)"},
    {0x50ED23E, "ASTDoor::HandleCharactersInMovePath"},
    {0x50ED31E, "ASTDoor::HandleCharactersInMovePath"},
    {0x50ED502, "ASTDoor::HandleCharactersInMovePath"},
    {0x50F9536, "ASTPlayerMechBase::OnSelfDestructSequenceComplete"},
    {0x511A628, "ASTEncounterManagerBase::CleanUpEncounter"},
    {kCrushActorRet, "ASTHazardousZoneBase::CrushActor"},
    {0x5161F7D, "ASTFallZonesBase::OnBeginOverlap"},
    {0x51626AF, "ASTFallZonesBase::OnBeginOverlap"},
    {0x5170C95, "ASTTeleporterVolumeBase::StartPlayerTeleport"},
    // Card 363: TryTakeCorpseTicket's tail jump, so the return is HandleDying's. On a client (no game mode) it runs for
    // every corpse; the damage stops at the ability system's authority gate, so it changes nothing there.
    {0x5192EFE, "ASTNonPlayableCharacterBase::HandleDying via TryTakeCorpseTicket"},
    {0x519CF0E, "USTGrenadeDamageVolumeComponent::OnGrenadeExploded"},
    {0x51AAC68, "ASTNonPlayableCharacterBase::Tick"},
    {0x51DD9BF, "ASTProceduralDropship::ShuntAndCrushBelow"},
    {0x51DDAAC, "ASTProceduralDropship::ShuntAndCrushBelow"},
    {0x5209CB6, "USTCheatManager::KillAllNPCs"},
    {0x5211D93, "USTSaveSystemLibrary::PostSavedLevelLoadSetup"},
};

struct C361Pair {
    const void* zone;
    const void* pawn;
};

struct C361Map {
    uint32_t rows;
    int32_t callers_n;
    unsigned long long callers[kC361Callers];
    int32_t zones_n;
    const void* zones[kC361Zones];
    int32_t crushed_n;
    C361Pair crushed[kC361Crushed];
    bool flip; // host and copy both in a zone: alternate ticks
};
C361Map g_c361 = {};

// Card 362, client: a skip waiting for the Down frame, and the last few Down frames by entry id.
struct C362Skip {
    int32_t id;              // c43_client_id of the pawn (-1: none, never matched)
    unsigned long long ms;   // the skip's time
    unsigned long long ret;  // its caller's return RVA, when `caller` is null
    const char* caller;      // the traced caller's name, or null
    ObjectName pawn;
};
struct C362Frame {
    int32_t id;
    unsigned long long ms;
};
struct C362Map {
    int32_t skips_n;
    C362Skip skips[kC362Skips];
    int32_t frames_next; // ring index
    C362Frame frames[kC362Frames];
    uint32_t warn_lines; // no-Down-frame rows this map (a caller that repeats each tick re-adds its skip every 3 s)
};
constexpr uint32_t kC362WarnLines = 16;
C362Map g_c362 = {};

const void* g_c361_scoped = nullptr; // host: the copy the hazard tick runs for
bool g_c361_scoped_killed = false;   // host: CrushActor InstaKilled it inside that tick

const char* c361_caller(unsigned long long ret) {
    if (g_c361_oob_expiry) {
        return "KillOwningActor (out of bounds expiry)";
    }
    for (const C361Caller& c : kC361CallerTable) {
        if (c.ret == ret) {
            return c.name;
        }
    }
    return nullptr;
}

bool c361_caller_seen(unsigned long long ret) {
    for (int32_t i = 0; i < g_c361.callers_n; ++i) {
        if (g_c361.callers[i] == ret) {
            return true;
        }
    }
    if (g_c361.callers_n < kC361Callers) {
        g_c361.callers[g_c361.callers_n++] = ret;
    }
    return false;
}

// Card 362: watch this skip for the host's Down frame, unless one for the pawn arrived within the window already.
void c362_note_skip(const void* target, const char* caller, unsigned long long ret, unsigned long long now) {
    const int32_t id = c43_client_id(target);
    for (const C362Frame& f : g_c362.frames) {
        if (id >= 0 && f.id == id && f.ms != 0 && now - f.ms <= kC362WaitMs) {
            return;
        }
    }
    // A caller that repeats each tick (CrushActor while the pawn stays in the zone) keeps the pawn's one waiting row.
    for (int32_t i = 0; i < g_c362.skips_n; ++i) {
        if (id >= 0 && g_c362.skips[i].id == id) {
            return;
        }
    }
    const ObjectName name = object_name(target);
    if (g_c362.skips_n >= kC362Skips) {
        if (caller != nullptr) {
            log::line("WARN card362: client skip table of %d full: skip of %s by %s not watched", kC362Skips,
                      name.text, caller);
        } else {
            log::line("WARN card362: client skip table of %d full: skip of %s by exe+0x%llX not watched", kC362Skips,
                      name.text, ret);
        }
        return;
    }
    g_c362.skips[g_c362.skips_n++] = C362Skip{id, now, ret, caller, name};
}

void c361_client_player(const void* target, unsigned long long ret, unsigned long long now) {
    const char* const caller = c361_caller(ret);
    c362_note_skip(target, caller, ret, now);
    if (!note_event_cap(g_c361.rows, kC361Rows, "card361")) {
        return;
    }
    const ObjectName name = object_name(target); // held: .text lives in the struct
    const char* const side = kSprintSideText[sprint_side(target)];
    if (caller == nullptr) {
        log::line("WARN card361: client t=%llu InstaKill of %s (%s) by exe+0x%llX: caller not traced; skipped", now,
                  name.text, side, ret);
        return;
    }
    log::line("card361: client t=%llu InstaKill of %s (%s) by %s: skipped; the host's kill comes by the Down frame", now,
              name.text, side, caller);
}

void c361_zone_row(const void* zone) {
    for (int32_t i = 0; i < g_c361.zones_n; ++i) {
        if (g_c361.zones[i] == zone) {
            return;
        }
    }
    if (g_c361.zones_n >= kC361Zones) {
        return;
    }
    g_c361.zones[g_c361.zones_n++] = zone;
    if (note_event_cap(g_c361.rows, kC361Rows, "card361")) {
        const ObjectName name = object_name(zone);
        log::line("card361: %s t=%llu hazard %s (%s) moves, crush %u", g_client_session ? "client" : "host",
                  static_cast<unsigned long long>(GetTickCount64()), name.text, class_name(zone).text,
                  static_cast<const uint8_t*>(zone)[kZoneCrushInPath] != 0 ? 1u : 0u);
    }
}

bool c361_in_roster(const C43Roster& r, const void* pawn) {
    for (int32_t s = 0; s < kC43Max; ++s) {
        if (r.p[s].pawn == pawn) {
            return true;
        }
    }
    return false;
}

bool c361_crushed(const void* zone, const void* pawn) {
    for (int32_t i = 0; i < g_c361.crushed_n; ++i) {
        if (g_c361.crushed[i].zone == zone && g_c361.crushed[i].pawn == pawn) {
            return true;
        }
    }
    return false;
}

// Drops this zone's crushed pairs whose pawn left the roster or the zone.
void c361_prune(const void* zone, const void* component, const C43Roster& r) {
    int32_t keep = 0;
    for (int32_t i = 0; i < g_c361.crushed_n; ++i) {
        const C361Pair p = g_c361.crushed[i];
        const bool gone = p.zone == zone &&
                          (!c361_in_roster(r, p.pawn) || !g_c321_prim_overlapping(component, p.pawn));
        if (!gone) {
            g_c361.crushed[keep++] = p;
        }
    }
    g_c361.crushed_n = keep;
}

// Host: the server copy the tick should run for, or null (the original's player 0 is right: nobody, or the host).
const void* c361_copy_in_zone(const void* zone) {
    const void* const component = read_ptr(zone, kZoneHazardComponent);
    void* const world = component != nullptr ? g_actor_get_world(zone) : nullptr;
    if (world == nullptr || g_c321_prim_overlapping == nullptr) {
        return nullptr;
    }
    const C43Roster r = c43_roster(world);
    c361_prune(zone, component, r);
    bool host_in = false;
    const void* copy = nullptr;
    for (int32_t s = 0; s < kC43Max; ++s) {
        const void* const pawn = r.p[s].pawn;
        if (!c321_player_body(pawn) || !g_c321_prim_overlapping(component, pawn)) { // trooper or mech (b182)
            continue;
        }
        const uint32_t pawn_side = sprint_side(pawn);
        host_in = host_in || pawn_side == kSideHostOwn;
        copy = copy == nullptr && pawn_side == kSideServerCopy && !c361_crushed(zone, pawn) ? pawn : copy;
    }
    if (copy != nullptr && host_in) {
        g_c361.flip = !g_c361.flip;
        return g_c361.flip ? copy : nullptr;
    }
    return copy;
}

} // namespace

void detour_c361_insta_kill(void* target, bool overkill) {
    const unsigned long long ret = exe_rva(_ReturnAddress());
    if (!g_client_session) {
        g_c361_scoped_killed = g_c361_scoped_killed || (target != nullptr && target == g_c361_scoped &&
                                                        ret == kCrushActorRet);
        g_insta_kill(target, overkill);
        return;
    }
    const unsigned long long now = static_cast<unsigned long long>(GetTickCount64());
    const bool player = c321_player_body(target); // b183: a piloted mech is a player body too (card 361)
    if (player) {
        c361_client_player(target, ret, now);
        return;
    }
    if (target != nullptr && !c361_caller_seen(ret) && note_event_cap(g_c361.rows, kC361Rows, "card361")) {
        const char* const caller = c361_caller(ret);
        if (caller != nullptr) {
            log::line("card361: client t=%llu InstaKill of %s by %s: runs", now, class_name(target).text, caller);
        } else {
            log::line("card361: client t=%llu InstaKill of %s by exe+0x%llX: runs", now, class_name(target).text, ret);
        }
    }
    g_insta_kill(target, overkill);
}

void detour_c361_hazard_tick(void* zone) {
    c361_zone_row(zone);
    const bool crush = static_cast<const uint8_t*>(zone)[kZoneCrushInPath] != 0;
    const void* const copy = crush && !g_client_session ? c361_copy_in_zone(zone) : nullptr;
    if (copy == nullptr) {
        g_orig_c361_hazard_tick(zone);
        return;
    }
    g_c361_scoped = copy;
    g_c361_scoped_killed = false;
    const C102Scope saved = c102_enter(copy, kC102Overlap, zone);
    g_orig_c361_hazard_tick(zone);
    c102_leave(saved);
    g_c361_scoped = nullptr;
    if (!g_c361_scoped_killed) {
        return;
    }
    if (g_c361.crushed_n < kC361Crushed) {
        g_c361.crushed[g_c361.crushed_n++] = C361Pair{zone, copy};
    } else {
        log::line("WARN card361: host crushed table of %d full; %s may be crushed again", kC361Crushed,
                  object_name(copy).text);
    }
    const ObjectName zone_name = object_name(zone);
    const ObjectName pawn_name = object_name(copy);
    log::line("card361: host t=%llu hazard %s crushed %s (server-copy)", static_cast<unsigned long long>(GetTickCount64()),
              zone_name.text, pawn_name.text);
}

// Card 362, client: a Down frame (state not Up) for entry `id` clears its skips and is kept for a skip that follows.
void c362_down_frame(int32_t id, unsigned long long now) {
    g_c362.frames[g_c362.frames_next] = C362Frame{id, now};
    g_c362.frames_next = (g_c362.frames_next + 1) % kC362Frames;
    int32_t keep = 0;
    for (int32_t i = 0; i < g_c362.skips_n; ++i) {
        if (g_c362.skips[i].id != id) {
            g_c362.skips[keep++] = g_c362.skips[i];
        }
    }
    g_c362.skips_n = keep;
}

// Card 362, client, every engine tick: a skip with no Down frame in 3 s gets its row and leaves the table.
void c362_step() {
    if (!g_client_session || g_c362.skips_n == 0) {
        return;
    }
    const unsigned long long now = static_cast<unsigned long long>(GetTickCount64());
    int32_t keep = 0;
    for (int32_t i = 0; i < g_c362.skips_n; ++i) {
        const C362Skip& k = g_c362.skips[i];
        if (now - k.ms <= kC362WaitMs) {
            g_c362.skips[keep++] = k;
            continue;
        }
        if (!note_event_cap(g_c362.warn_lines, kC362WarnLines, "card362 no Down frame")) {
            continue;
        }
        if (k.caller != nullptr) {
            log::line("WARN card362: client skip of %s by %s had no Down frame in 3 s", k.pawn.text, k.caller);
        } else {
            log::line("WARN card362: client skip of %s by exe+0x%llX had no Down frame in 3 s", k.pawn.text, k.ret);
        }
    }
    g_c362.skips_n = keep;
}

// Both sides, at LoadMap enter: per-map caps, the crushed pairs and the card 362 tables start over.
void c361_map_end() {
    g_c361 = C361Map{};
    g_c362 = C362Map{};
    g_c361_scoped = nullptr;
    g_c361_scoped_killed = false;
}

} // namespace ubw::hooks
