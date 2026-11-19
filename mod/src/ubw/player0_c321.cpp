// Card 321 diagnostic (log only, host): once per map, at the first engine tick with a game world, how many actors of
// each class whose host tick code asks for player 0 only the map places, and which sublevels the level streaming
// volumes gate. Settles card 321 step 1 item 1 per map (which maps stream, which places hazards, teleporters, fall
// zones and crushing doors) before any per-player fix.
//
//   card321: host map <name> streaming volumes <n>, gated levels <n>: <names>, hazard zones <n>, teleporters <n>,
//            fall zones <n>, doors <n>, doors with crush <n>
//
// AADLevelStreamingVolume has one reflected property, LevelName (PDB: NewProp_LevelName); its kind is read from
// reflection and named in the row when it is not an FName. ASTDoor: +0x355 bDamageCharactersInPath, +0x357 crush on
// sweep (HandleCharactersInMovePath 0x050EC950); a door counts as crushing when either is set.

#include "ubw/hooks_internal.h"

#include <string.h>

namespace ubw::hooks {

StaticClassFn g_c321_fall_zone_class = nullptr; // ASTFallZonesBase
StaticClassFn g_c321_streaming_class = nullptr; // AADLevelStreamingVolume
C321DoorPathFn g_orig_c321_door_path = nullptr;
C321PrimOverlapFn g_c321_prim_overlapping = nullptr;

namespace {

constexpr size_t kDoorDamageInPath = 0x355; // ASTDoor: damage characters in the move path
constexpr size_t kDoorCrushOnSweep = 0x357; // ASTDoor: crush a crushable character in the move path
constexpr int32_t kC132ExtHazard = 1;       // g_c132_ext_class[1]: ASTHazardousZoneBase
constexpr int32_t kC132ExtTeleporter = 4;   // g_c132_ext_class[4]: ASTTeleporterVolumeBase
constexpr size_t kC321Names = 480;          // gated level names in the row, truncated past this
constexpr size_t kDoorCrushVolume = 0x360;  // ASTDoor: the crush volume (UPrimitiveComponent*), part 2's overlap test
constexpr uint32_t kC321DoorRows = 32;      // card321 door rows per map

bool g_c321_logged = false; // the row ran this map
uint32_t g_c321_door_rows = 0;

uint32_t c321_count(const void* world, StaticClassFn cls) {
    if (cls == nullptr) {
        return 0;
    }
    static C23List list;
    c23_collect(world, nullptr, cls(), list);
    return list.total;
}

uint32_t c321_crush_doors(const void* world, uint32_t& doors) {
    doors = 0;
    if (g_c95_door_class == nullptr) {
        return 0;
    }
    static C23List list;
    c23_collect(world, nullptr, g_c95_door_class(), list);
    doors = list.total;
    uint32_t crush = 0;
    for (uint32_t i = 0; i < list.count; ++i) {
        const auto* const d = static_cast<const uint8_t*>(list.e[i].actor);
        crush += (d[kDoorDamageInPath] != 0 || d[kDoorCrushOnSweep] != 0) ? 1u : 0u;
    }
    return crush;
}

// The volumes' LevelName values, comma separated, into `out`; returns the number of volumes with a name.
uint32_t c321_gated(const void* world, uint32_t& volumes, char* out, size_t cap) {
    out[0] = '\0';
    volumes = 0;
    if (g_c321_streaming_class == nullptr) {
        return 0;
    }
    const void* const cls = g_c321_streaming_class();
    static C23List list;
    c23_collect(world, nullptr, cls, list);
    volumes = list.total;
    const uint8_t* const prop = prop_find_prop(cls, "LevelName");
    if (prop == nullptr) {
        snprintf(out, cap, "no LevelName property");
        return 0;
    }
    ObjectName kind{};
    if (prop_kind(prop, kind) != PropKind::Name) {
        snprintf(out, cap, "LevelName is %s", kind.text);
        return 0;
    }
    const int32_t at = read_i32(prop, kPropertyOffset);
    uint32_t named = 0;
    size_t len = 0;
    for (uint32_t i = 0; i < list.count && len + 1 < cap; ++i) {
        const ObjectName n = prop_name(static_cast<const uint8_t*>(list.e[i].actor) + at, 8);
        if (n.text[0] == '\0' || strcmp(n.text, "None") == 0) {
            continue;
        }
        ++named;
        const int w = snprintf(out + len, cap - len, "%s%s", len > 0 ? ", " : "", n.text);
        len = w > 0 ? len + static_cast<size_t>(w) : len;
    }
    return named;
}

// Host: the player part 2 of HandleCharactersInMovePath should test. Null when the host's own pawn is in the door's
// crush volume (or nobody is): the original's GetPlayerCharacter(0) already answers it. Otherwise the first server copy
// (a client's trooper or mech, c321_player_body) in the volume, in roster order.
const void* c321_copy_in_way(const void* door) {
    const void* const volume = read_ptr(door, kDoorCrushVolume);
    void* const world = volume != nullptr ? g_actor_get_world(door) : nullptr;
    if (world == nullptr || g_c321_prim_overlapping == nullptr || g_player_character_class == nullptr) {
        return nullptr;
    }
    const C43Roster r = c43_roster(world);
    const void* copy = nullptr;
    for (int32_t s = 0; s < kC43Max; ++s) {
        const void* const pawn = r.p[s].pawn;
        if (!c321_player_body(pawn) || !g_c321_prim_overlapping(volume, pawn)) {
            continue;
        }
        const uint32_t pawn_side = sprint_side(pawn);
        if (pawn_side == kSideHostOwn) {
            return nullptr; // the host is in the way: player 0 is already right
        }
        copy = copy == nullptr && pawn_side == kSideServerCopy ? pawn : copy;
    }
    return copy;
}

} // namespace

// Cards 321 / 361 (b182): the pawn the original's GetPlayerCharacter(slot) answers for a player: the trooper, or the
// mech while the player pilots it (the mech is an ACharacter, so GetPlayerCharacter(0) is the mech while the host
// pilots; b181 pilot check rows: slot 1 GetPlayerCharacter BP_BaseMech_C for a piloting client).
bool c321_player_body(const void* pawn) {
    return pawn != nullptr && ((g_player_character_class != nullptr && is_a(pawn, g_player_character_class())) ||
                               (g_mech_class != nullptr && is_a(pawn, g_mech_class())));
}

// Card 321 door part 2 (host): HandleCharactersInMovePath asks GetPlayerCharacter(0) whether a player stands in the
// door's crush volume (damage once per entry, latched by door+0x4E0, or an instant kill when crushable). The rule
// "is any player in the way": when a client's trooper is in the volume and the host's is not, the call runs once in
// that copy's acting scope (card 102), so card 099's remap answers player 0 with the copy. Part 1 (the sweep push)
// and the NPC part run once, as before. The latch stays one per door: a second trooper entering while the first is
// still inside takes no entry damage (the same as the original for one player).
void detour_c321_door_path(void* door, const void* from, const void* to, bool closing) {
    const void* const copy = closing && !g_client_session ? c321_copy_in_way(door) : nullptr;
    if (copy == nullptr) {
        g_orig_c321_door_path(door, from, to, closing);
        return;
    }
    if (note_event_cap(g_c321_door_rows, kC321DoorRows, "card321 door")) {
        log::line("card321: host t=%llu door %s (%s) crush volume holds %s (%s), not the host: part 2 runs for it",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(door).text, class_name(door).text,
                  object_name(copy).text, class_name(copy).text);
    }
    const C102Scope saved = c102_enter(copy, kC102Overlap, door);
    g_orig_c321_door_path(door, from, to, closing);
    c102_leave(saved);
}

// Card 321 / 361 pilot check (b181, diagnostic only): once per EnterMech on the host, per roster slot, what
// GetPlayerCharacter(slot) answers (UGameplayStatics: GetPlayerController(world, slot)->Character, the controller's
// pawn when it is an ACharacter; g_get_player_controller is the original, not the card 099 remap), the controller's
// pawn, and the soldier riding in it (c75_pilot_body). Both part 2 paths (door crush, hazard zone) take a mech pawn
// since b182 (c321_player_body).
void c321_pilot_check(const void* mech) {
    void* const world = mech != nullptr && !g_client_session ? g_actor_get_world(mech) : nullptr;
    if (world == nullptr || g_character_class == nullptr) {
        return;
    }
    const C43Roster r = c43_roster(world);
    const void* const character_cls = g_character_class();
    for (int32_t s = 0; s < kC43Max; ++s) {
        if (r.p[s].pc == nullptr) {
            continue;
        }
        const void* const pawn = r.p[s].pawn;
        const void* const character = pawn != nullptr && is_a(pawn, character_cls) ? pawn : nullptr;
        const void* const body = c75_pilot_body(pawn);
        log::line("card321: host t=%llu pilot check slot %d (%s, mech %s): GetPlayerCharacter(%d) %s, controller pawn "
                  "%s, c75_pilot_body %s",
                  static_cast<unsigned long long>(GetTickCount64()), s, object_name(r.p[s].pc).text,
                  object_name(mech).text, s, character != nullptr ? class_name(character).text : "none",
                  pawn != nullptr ? class_name(pawn).text : "none", body != nullptr ? class_name(body).text : "none");
    }
}

// Host, every engine tick: the row once per map, at the first tick with a game world.
void c321_step(void* engine) {
    if (g_c321_logged || g_client_session) {
        return;
    }
    void* const world = engine != nullptr ? g_get_game_world(engine) : nullptr;
    if (world == nullptr) {
        return;
    }
    g_c321_logged = true;
    uint32_t volumes = 0;
    char names[kC321Names];
    const uint32_t gated = c321_gated(world, volumes, names, sizeof names);
    uint32_t doors = 0;
    const uint32_t crush = c321_crush_doors(world, doors);
    log::line("card321: host map %s streaming volumes %u, gated levels %u: %s, hazard zones %u, teleporters %u, "
              "fall zones %u, doors %u, doors with crush %u",
              object_name(world).text, volumes, gated, names[0] != '\0' ? names : "none",
              c321_count(world, g_c132_ext_class[kC132ExtHazard]), c321_count(world, g_c132_ext_class[kC132ExtTeleporter]),
              c321_count(world, g_c321_fall_zone_class), doors, crush);
}

// Both sides, at LoadMap enter: the next map logs its own row.
void c321_map_end() {
    g_c321_logged = false;
    g_c321_door_rows = 0;
}

} // namespace ubw::hooks
