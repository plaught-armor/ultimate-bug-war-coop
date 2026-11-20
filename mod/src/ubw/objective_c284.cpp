// Card 284: objective-driven test steps (test runner only, UBWCOOP_CMD=1, host). The scenario asks the game which
// objective is active, where it is and what it needs, instead of a hand-kept table of stand points and actor names
// (user, 2026-11-13: "there needs to be some sort of way to be exactly at the correct objective and also have the
// information necessary to complete it").
//
//   objective now                          rows for every open step and container under the active parent
//   objective expect <id>                  passes once <id> is doable (within c.ms), and makes it `current`; else the
//                                          scenario voids, with the doable ids
//   objective go [host|client|client2|all] warps the players to `current`'s stand point (ring offsets), then after a
//                                          settle (c.ms) rows each pawn's settled position, drift and inside test
//   objective wait <id> [seconds]          ends when <id> is complete; fails at the time with count and current id
//
// Steps (senior review of the 5a-5d diff): a step is open, under the active parent (directly or under an open
// container), and has no children (a container is never a step). A step is doable when its marker is on, or when it
// sits under a container and a sibling under that container has its marker on (parallel steps: C3M2
// Monorail_SO2_A-D, markers off on A-C). A step directly under the active parent with its marker off is a future
// step. `current` is the step the last `expect` named; with none (or once it is no longer an open step), the
// picker's choice (c126_pick, autoplay's picker, one copy).

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

namespace {

constexpr double kC284UseUu = 100.0;      // use: the stand point this far in front of the switch (kC126UseUu 150 reach)
constexpr double kC284StandoffUu = 600.0; // nest, kill: the stand point this far from actor[0], toward the host
constexpr double kC284RingUu = 90.0;      // client and client2 offsets from the stand point
// Client ring slots, tried in this order (second client takes the next free one); after them, the stand point.
// With a shape, a slot counts only when the pawn's capsule there lies inside it (senior review 2, item 1).
constexpr int32_t kC284Slots = 3;
constexpr int32_t kC284RingDeg[kC284Slots] = {90, 270, 180};
constexpr int32_t kC284SlotStand = -1;    // the stand point itself (the host's slot, and the clients' fallback)
constexpr double kC284WarpUp = 2.0;       // over the pawn's standing center: no start inside the floor
constexpr double kC284DriftMaxUu = 50.0;  // go: a pawn further than this from its landing after the settle fails
constexpr int32_t kC284MaxActors = 8;     // actor rows per step
// b237 (user 2026-11-18, c244b235b Stadium_SO1_B: "client isn't close enough to terminal to press interact"): the
// navmesh stand of BP_PanelSwitch_WithoutScreen_C_1 projected 219 uu out (eye 265 uu, past the 250 uu interactor
// trace). The presser then steps toward the target with a swept move, as a player walks up until the panel blocks.
// b256 (c245b255a, senior): a 3D eye stop measured from the switch root (on the floor) stepped harder, and the
// focus miss was the aim scan's (testrun.cpp b14_points). A pawn whose eye is already within the interactor reach of
// the target does not step.
// b257 (c245b256a line 395, Bridge_SO4 BP_Switch_C_3): the 2D gap from the root (60 uu) put the eye 19 uu from the
// switch's bounds face and no look gave focus; b229 pressed it from 172 uu at pitch -5. The step is now measured in
// 2D from the nearest point of the target actor's bounds box (no step within 150 uu of that face).
// Card 512 (c504k1, Stadium_SO1_B-D): bounds can sit off the switch (face 100 uu, eye 369 uu from the target), so
// the face no longer gates the step: the eye distance to the target does, and the pawn stops kC284StepEyeUu off it.
constexpr double kC284StepEyeUu = 200.0;
constexpr int32_t kC284MaxRows = 32;      // step and container rows per `now`
constexpr int32_t kC284GoSteps = 16;      // b238: steps whose `go` count is kept (objective next cycles them)
constexpr double kC284EyeUu = 60.0;       // go: the view pitch is measured from this far over the pawn's center
constexpr uint8_t kC284NoMode = 0xFF;     // no CharacterMovement: mode unknown
// Card 513 (c504k2: 158 goes held the full 1 s settle, 38 s): the settle ends early once every placed pawn has been
// walking and within kC284StillUu of one point for kC284StillMs (the first window starts at the first settle tick,
// after the warp). The c.ms cap, the settle rows and the fail checks are unchanged. A client pawn is read on the host
// (server authoritative): a ServerMove the client sent before it saw the warp moves the host's copy and restarts the
// window; an idle client still sends moves at least every 1/12 s, so on a local pair those land well inside 300 ms.
constexpr double kC284StillUu = 5.0;
constexpr uint64_t kC284StillMs = 300;
constexpr uint8_t kC284MoveWalking = 1;   // EMovementMode MOVE_Walking
constexpr const char* kC284Who[3] = {"host", "client", "client2"};

// What the stand point is measured from and where it is.
struct C284Stand {
    C126Kind kind;
    void* actor;           // actor[0], or the marker actor (0 actors), or null
    const char* from;      // "shape center", "root", "actor", "marker"
    Vec3 target;           // the look and distance target
    Vec3 nav;              // the navmesh point the pawns stand on
    C126Shape shape;       // presence and count: the volume; kind None when there is none
    double yaw;            // facing the target from the stand point
};

enum class C284Placed : uint8_t { None, Ok, NotMoved };

constexpr int32_t kC284GuideIds = 32; // a map has a few marker steps; C5M2 and C3M2 have under ten
struct C284 {
    const void* world;      // key only: a new world drops everything below
    uint64_t current;       // the step `expect` named last, else the picker's; 0 none
    uint64_t go_ids[kC284GoSteps]; // b238: the steps `objective go` placed for, one slot each
    int32_t go_ns[kC284GoSteps];   // b238: per slot, how many `go` calls named that step, less one
    int32_t go_used;               // slots in use
    int32_t go_evict;              // a full table reuses this slot next (round robin)
    int32_t go_n;           // the last go's count for its step (the presence try of an actorless or marker step)
    C284Stand stand;        // go: the stand the pawns were placed on
    void* pawns[3];         // go: the pawns placed (host, client, client2), checked again at the settle
    Vec3 landed[3];         // go: each pawn's location right after SetActorLocation
    C284Placed placed[3];   // go: None, not placed
    int32_t slot[3];        // go: ring degrees, or kC284SlotStand
    Vec3 still_at[3];       // card 513: where each pawn's still window started
    uint64_t still_from[3]; // card 513: when it started (ms); 0 before the first settle tick
    uint64_t guide_ids[kC284GuideIds]; // c247b307a: marker steps seen open while their actor-sharing sibling was open
    int32_t guide_n;                   // ids in use; a full table records no more (the wait then runs to its time)
    uint64_t zone_ids[kC284GuideIds];  // card 516: steps a zone guide marker handed on to (the sole shown sibling)
    int32_t zone_n;                    // ids in use; a full table records no more
    uint64_t shown_ids[kC284GuideIds]; // card 516: steps whose marker was shown at one of their goes
    int32_t shown_n;                   // ids in use; a full table records no more
    uint64_t handed_id;                // card 516: the step whose handed-on condition held on the last wait tick
    int32_t handed_ticks;              // consecutive wait ticks it held
    uint64_t base_id;                  // card 516: the zone guide whose sibling counts are below; 0 none
    int32_t base_total;                // its parent's other children at the first wait tick on it
    int32_t base_done;                 // how many of those were complete then
};
C284 g_c284{};

char g_c284_why[512]; // one result per command: never two c284_fail calls in one expression

const char* c284_fail(const char* fmt, const char* a, const char* b) {
    snprintf(g_c284_why, sizeof(g_c284_why), fmt, a, b);
    return g_c284_why;
}

void c284_reset(const void* world) {
    if (g_c284.world != world) {
        g_c284 = C284{};
        g_c284.world = world;
    }
}

bool c284_has(const uint64_t* ids, int32_t n, uint64_t id) {
    for (int32_t k = 0; k < n && k < kC284GuideIds; ++k) {
        if (ids[k] == id) {
            return true;
        }
    }
    return false;
}

// Card 516: step `id`'s marker was shown at a go (c284_handed_on needs it shown once before it counts it hidden).
void c284_note_shown(uint64_t id) {
    if (!c284_has(g_c284.shown_ids, g_c284.shown_n, id) && g_c284.shown_n < kC284GuideIds) {
        g_c284.shown_ids[g_c284.shown_n++] = id;
    }
}

// b238: the `go` count of step `id`, 0 on its first go, counted per step: `objective next` moves between steps, and a
// step it comes back to resumes its presence tries (c284_marker_next_presence nth) instead of starting at the first.
// Before b238 the count reset whenever another step was placed in between.
int32_t c284_go_count(uint64_t id) {
    for (int32_t i = 0; i < g_c284.go_used && i < kC284GoSteps; ++i) {
        if (g_c284.go_ids[i] == id) {
            return ++g_c284.go_ns[i];
        }
    }
    int32_t slot = g_c284.go_used;
    if (slot < kC284GoSteps) {
        ++g_c284.go_used;
    } else {
        slot = g_c284.go_evict;
        g_c284.go_evict = (g_c284.go_evict + 1) % kC284GoSteps;
    }
    g_c284.go_ids[slot] = id;
    g_c284.go_ns[slot] = 0;
    return 0;
}

// pawns[0] the host's own, [1] and [2] the server copies of "client" and "client2" (card 502 finding 2: by the role
// each client sent); null: none.
void c284_pawns(void* pc, void* pawns[3]) {
    pawns[0] = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    pawns[1] = pawns[0] != nullptr ? c29_server_copy_role(pawns[0], 1) : nullptr;
    pawns[2] = pawns[0] != nullptr ? c29_server_copy_role(pawns[0], 2) : nullptr;
}

const uint8_t* c284_obj(const SparseView& v, int32_t i) {
    const uint8_t* const e = v.data + static_cast<size_t>(i) * kObjectiveElementBytes;
    return static_cast<const uint8_t*>(sparse_allocated(v, i) ? read_ptr(e, kObjectiveElementValue) : nullptr);
}

// True when `o` lies under the active parent (directly, or under an open sub of it).
bool c284_under(const uint8_t* o, const void* parent) {
    const auto* const up = static_cast<const uint8_t*>(read_ptr(o, kObjectiveParent));
    return up == parent || (up != nullptr && read_ptr(up, kObjectiveParent) == parent && up[kObjectiveComplete] == 0);
}

// A step: open, under the active parent, no children of its own.
bool c284_is_step(const void* world, const uint8_t* o, const void* parent) {
    return o != nullptr && parent != nullptr && o[kObjectiveComplete] == 0 && c284_under(o, parent) &&
           c126_children(world, o) == 0;
}

// A step with its marker on, or under a container with a sibling's marker on (parallel steps).
bool c284_doable(const void* world, const SparseView& v, const uint8_t* o, const void* parent) {
    if (!c284_is_step(world, o, parent)) {
        return false;
    }
    if (o[kObjectiveMarker] != 0) {
        return true;
    }
    const auto* const up = static_cast<const uint8_t*>(read_ptr(o, kObjectiveParent));
    // Card 511 (c504b4/b5, FED_C2M1 LOS_O03_SO02a-c, the satchel pickups): the steps' own markers stay off and their
    // open container LOS_O03_SO02, not the active parent, shows the place. A step under an open container with its
    // marker on is doable.
    if (up != nullptr && up != parent && up[kObjectiveComplete] == 0 && up[kObjectiveMarker] != 0) {
        return true;
    }
    // b273 (c243b272a, FED_C3M2 Monorail_SO1: actors 0, no marker actor, marker off, while its sibling Monorail_Marker
    // shows the place): directly under the parent, only an actorless step counts, and only by an open sibling marker
    // row with its marker on, the one c284_actorless_stand stands near. Any other marker-off step there is a future step.
    const bool actorless = c126_actor(o) == nullptr && c126_marker_of(world, o) == nullptr;
    if (up == parent && !actorless) {
        return false; // directly under the active parent with its marker off: a future step
    }
    for (int32_t i = 0; i < v.count; ++i) {
        const uint8_t* const s = c284_obj(v, i);
        if (s != nullptr && s != o && read_ptr(s, kObjectiveParent) == up && s[kObjectiveComplete] == 0 &&
            s[kObjectiveMarker] != 0 && (up != parent || c126_kind_of(world, s) == C126Kind::Marker)) {
            return true;
        }
    }
    return false;
}

const uint8_t* c284_find_text(const SparseView& v, const char* id_text) {
    for (int32_t i = 0; i < v.count; ++i) {
        const uint8_t* const o = c284_obj(v, i);
        if (o != nullptr && strcmp(fname_text(o + kObjectiveId).text, id_text) == 0) {
            return o;
        }
    }
    return nullptr;
}

// The doable ids, space separated ("none" when there are none); cut short at the buffer's end.
void c284_doable_list(const void* world, const SparseView& v, const void* parent, char* buf, size_t size) {
    size_t at = 0;
    buf[0] = '\0';
    for (int32_t i = 0; i < v.count; ++i) {
        const uint8_t* const o = c284_obj(v, i);
        if (!c284_doable(world, v, o, parent)) {
            continue;
        }
        const int n = snprintf(buf + at, size - at, "%s%s", at != 0 ? " " : "", fname_text(o + kObjectiveId).text);
        if (n < 0 || at + static_cast<size_t>(n) >= size) {
            break;
        }
        at += static_cast<size_t>(n);
    }
    if (at == 0) {
        snprintf(buf, size, "none");
    }
}

// b272 (c243b271c, FED_C3M2): the level script opens Silo_O1, PowerPlant_O1 and Monorail_O1 together with markers
// in all three, so c126_active_parent falls back to the lowest slot (Silo_O1) while the route works Monorail_O1:
// every Monorail and PowerPlant `expect` voided "not under the active parent" though the steps were doable. The step
// `expect` names (`named`, else the current one) sets the parent: its own open top-level objective. Without an open
// one, the active parent.
const void* c284_parent(const void* world, const SparseView& v, const uint8_t* named) {
    const uint8_t* const o = named != nullptr ? named : (g_c284.current != 0 ? c126_find(world, g_c284.current) : nullptr);
    const uint8_t* const root = o != nullptr && o[kObjectiveComplete] == 0 ? c126_root(o) : nullptr;
    return root != nullptr && root != o ? root : c126_active_parent(v);
}

// The current step: the one `expect` named, while it is still an open step; else the picker's choice. Null: none.
const uint8_t* c284_current(const void* world, const void* parent, const void* host_pawn) {
    c284_reset(world);
    if (g_c284.current != 0) {
        const uint8_t* const o = c126_find(world, g_c284.current);
        if (c284_is_step(world, o, parent)) {
            return o;
        }
    }
    C126Pick p{};
    const Vec3 from = host_pawn != nullptr ? actor_location(host_pawn) : Vec3{};
    if (!c126_pick(world, 0, false, false, from, p)) {
        g_c284.current = 0;
        return nullptr;
    }
    g_c284.current = read_u64(p.objective, kObjectiveId);
    return p.objective;
}

void c284_count_text(const uint8_t* o, char* buf, size_t size) {
    const bool count = g_c49_count_class != nullptr && is_a(o, g_c49_count_class());
    if (count) {
        snprintf(buf, size, "%d/%d", read_i32(o, kObjectiveCountCurrent), read_i32(o, kObjectiveCountTarget));
    } else {
        snprintf(buf, size, "-");
    }
}

double c284_yaw_to(const Vec3& from, const Vec3& to) {
    return atan2(to.y - from.y, to.x - from.x) * kRadToDeg;
}

// The host's view onto the target from a pawn centered at `at` (yaw and pitch: the E focus and the aim-proof shot).
void c284_aim(void* pc, const Vec3& at, const Vec3& target) {
    const double d2 = sqrt((target.x - at.x) * (target.x - at.x) + (target.y - at.y) * (target.y - at.y));
    c64_set_yaw(pc, c284_yaw_to(at, target), atan2(target.z - (at.z + kC284EyeUu), d2) * kRadToDeg);
}

double c284_half_height(const void* pawn) {
    const void* const cap = pawn != nullptr ? read_ptr(pawn, kActorRootComponent) : nullptr;
    return cap != nullptr ? c126_num(cap, "CapsuleHalfHeight", 0) : 0.0;
}

// A settle row's slot text: "slot 90", "slot stand point".
void c284_slot_text(int32_t slot, char* buf, size_t size) {
    if (slot == kC284SlotStand) {
        snprintf(buf, size, "slot stand point");
    } else {
        snprintf(buf, size, "slot %d", static_cast<int>(slot));
    }
}

// Card 336 (b22): the BP_FallZones_C placed in the world's levels, at most kC336Zones (C2M1 has 30). Placed level
// actors only (kMaxLevels x kMaxLevelActors bound, as c64_find_actor).
constexpr int32_t kC336Zones = 64;
constexpr size_t kZoneEnabled = 0x310;    // ASTFallZonesBase: bool, OnBeginOverlap's enabled gate (card 338 trace)
constexpr int32_t kC336Owned = 64;        // owned components walked per zone
constexpr int32_t kC336OtherNamed = 3;    // other shape components named in a zone row
constexpr uint32_t kC336RefuseRows = 32;  // refusal rows per map
constexpr size_t kC336OwnedSet = 0x230;  // AActor::OwnedComponents TSet (layout: testrun.cpp kActorOwnedComponents)
constexpr size_t kC336SetNum = 0x8;
constexpr size_t kC336SetBitsInline = 0x10;
constexpr size_t kC336SetBitsHeap = 0x20;
constexpr size_t kC336SetNumBits = 0x28;
constexpr size_t kC336SetElement = 16;
bool g_c336_logged = false;    // this map's zone rows are out
uint32_t g_c336_refuse_rows = 0;

int32_t c336_zones(const void* world, void* out[kC336Zones]) {
    int32_t n = 0;
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    for (int32_t l = 0; levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const auto* const actors =
            levels[l] != nullptr ? static_cast<void* const*>(read_ptr(levels[l], kLevelActors)) : nullptr;
        const int32_t num = levels[l] != nullptr ? read_i32(levels[l], kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; actors != nullptr && a < num && a < kMaxLevelActors && n < kC336Zones; ++a) {
            if (actors[a] != nullptr && strcmp(class_name(actors[a]).text, "BP_FallZones_C") == 0) {
                out[n++] = actors[a];
            }
        }
    }
    return n;
}

// The actor's OwnedComponents, at most `max`, into `out`; returns how many.
int32_t c336_owned(const void* actor, const void* out[], int32_t max) {
    const auto* const set = static_cast<const uint8_t*>(actor) + kC336OwnedSet;
    const auto* const data = *reinterpret_cast<const uint8_t* const*>(set);
    const int32_t num = *reinterpret_cast<const int32_t*>(set + kC336SetNum);
    const int32_t num_bits = *reinterpret_cast<const int32_t*>(set + kC336SetNumBits);
    const auto* const heap = *reinterpret_cast<const uint32_t* const*>(set + kC336SetBitsHeap);
    const auto* const bits = heap != nullptr ? heap : reinterpret_cast<const uint32_t*>(set + kC336SetBitsInline);
    int32_t n = 0;
    for (int32_t i = 0; data != nullptr && i < num && i < num_bits && n < max; ++i) {
        if ((bits[i >> 5] >> (i & 31)) & 1U) {
            const void* const c = *reinterpret_cast<void* const*>(data + static_cast<size_t>(i) * kC336SetElement);
            out[n] = c;
            n += c != nullptr ? 1 : 0;
        }
    }
    return n;
}

// One zone row: the enabled gate byte, the shape c126_shape_find picks (what the landing test uses), and the zone's
// other box, sphere and capsule components, so a false refusal can be matched to the shape the game overlaps.
void c336_zone_row(const char* side, int32_t index, const void* zone) {
    C126Shape sh{};
    const void* picked = nullptr;
    const bool found = c126_shape_find(zone, sh, &picked);
    const void* const root = read_ptr(zone, kActorRootComponent);
    const void* comps[kC336Owned] = {};
    const int32_t n = c336_owned(zone, comps, kC336Owned);
    char others[480] = "";
    size_t used = 0;
    int32_t other_n = 0;
    for (int32_t i = 0; i < n; ++i) {
        C126Shape o{};
        if (comps[i] == picked || !c126_shape_comp(comps[i], o)) {
            continue;
        }
        ++other_n;
        if (other_n > kC336OtherNamed || used + 1 >= sizeof(others)) {
            continue;
        }
        const int w = snprintf(others + used, sizeof(others) - used,
                               "; %s (%s) center (%.0f, %.0f, %.0f) extent (%.0f, %.0f, %.0f)",
                               object_name(comps[i]).text, class_name(comps[i]).text, o.center.x, o.center.y,
                               o.center.z, o.ext.x, o.ext.y, o.ext.z);
        used = w > 0 ? (used + static_cast<size_t>(w) < sizeof(others) ? used + static_cast<size_t>(w)
                                                                       : sizeof(others) - 1)
                     : used;
    }
    const Vec3 at = actor_location(zone);
    log::line("card336: %s zone %d %s at (%.0f, %.0f, %.0f): enabled %u, picked %s (%s, %s), center (%.0f, %.0f, "
              "%.0f), extent (%.0f, %.0f, %.0f), quat (%.3f, %.3f, %.3f, %.3f), owned components %d, other shape "
              "components %d%s",
              side, static_cast<int>(index), object_name(zone).text, at.x, at.y, at.z,
              static_cast<unsigned>(static_cast<const uint8_t*>(zone)[kZoneEnabled]),
              found ? object_name(picked).text : "none", found ? class_name(picked).text : "-",
              !found ? "-" : picked == root ? "root" : "property", sh.center.x, sh.center.y, sh.center.z, sh.ext.x,
              sh.ext.y, sh.ext.z, sh.q[0], sh.q[1], sh.q[2], sh.q[3], static_cast<int>(n),
              static_cast<int>(other_n), others);
}

// Once per map: a count row, then one row per zone.
void c336_zone_rows(const void* world) {
    g_c336_logged = true;
    void* zones[kC336Zones] = {};
    const int32_t n = c336_zones(world, zones);
    const char* const side = g_client_session ? "client" : "host";
    log::line("card336: %s map %s fall zones %d", side, object_name(world).text, static_cast<int>(n));
    for (int32_t i = 0; i < n; ++i) {
        c336_zone_row(side, i, zones[i]);
    }
}

// Card 336 (r6 C2M1, cmds 144/162/167): a warp whose lift or landing point overlaps a BP_FallZones_C gets the
// zone's 20 damage and the game moves the pawn to a fixed point ((227524, -17297, 6512) on LOS_O03), so the settle
// read a 3830 uu drift. True when the pawn's capsule centred at `pos` lies inside an enabled fall zone's overlap
// shape (b22: a zone whose +0x310 enabled byte is 0 never fires OnBeginOverlap, so it is skipped); the zone's name
// goes to `name`, and a refusal row names the zone index of the card 336 zone rows.
bool c284_in_fall_zone(const void* world, const void* pawn, const Vec3& pos, ObjectName& name) {
    if (!g_c336_logged) {
        c336_zone_rows(world);
    }
    void* zones[kC336Zones] = {};
    const int32_t n = c336_zones(world, zones);
    for (int32_t i = 0; i < n; ++i) {
        if (static_cast<const uint8_t*>(zones[i])[kZoneEnabled] == 0) {
            continue;
        }
        C126Shape sh{};
        if (c126_shape_find(zones[i], sh, nullptr) && c126_capsule_inside(sh, pawn, pos)) {
            name = object_name(zones[i]);
            if (note_event_cap(g_c336_refuse_rows, kC336RefuseRows, "card336 refusal")) {
                log::line("card336: %s zone %d %s refused (%.0f, %.0f, %.0f) for %s",
                          g_client_session ? "client" : "host", static_cast<int>(i), name.text, pos.x, pos.y, pos.z,
                          object_name(pawn).text);
            }
            return true;
        }
    }
    return false;
}

// A client's slot (senior review 2, item 1): the first free ring slot with a navmesh point and, for a shaped
// stand, the pawn's capsule inside the shape there; else the stand point. `taken` marks the slots used. Card 336:
// a slot whose capsule overlaps a fall zone is skipped (row names the zone).
int32_t c284_slot(const void* world, void* pawn, const C284Stand& s, bool taken[kC284Slots], Vec3& nav) {
    const bool shaped = s.shape.kind != C126ShapeKind::None;
    const double half = c284_half_height(pawn);
    for (int32_t k = 0; k < kC284Slots; ++k) {
        if (taken[k]) {
            continue;
        }
        const double a = (s.yaw + kC284RingDeg[k]) / kRadToDeg;
        const Vec3 want{s.nav.x + cos(a) * kC284RingUu, s.nav.y + sin(a) * kC284RingUu, s.nav.z};
        Vec3 p{};
        if (!c126_project(world, pawn, want, shaped ? &s.shape : nullptr, p, nullptr)) {
            continue;
        }
        if (shaped && !c126_capsule_inside(s.shape, pawn, Vec3{p.x, p.y, p.z + half})) {
            continue;
        }
        ObjectName zone{};
        if (c284_in_fall_zone(world, pawn, Vec3{p.x, p.y, p.z + half + kC284WarpUp}, zone)) {
            log::line("card336: ring slot %d at (%.0f, %.0f, %.0f) skipped: inside %s", kC284RingDeg[k], p.x, p.y,
                      p.z, zone.text);
            continue;
        }
        taken[k] = true;
        nav = p;
        return kC284RingDeg[k];
    }
    nav = s.nav;
    return kC284SlotStand;
}

// b7b rerun 2026-11-13: LOS_O01_SO01's marker (an ActorReference_C at z 9056) has no navmesh point within the
// projection box; the level script completes the step from the nearest BP_PlayerPresence_C (the wave spawns on its
// entry). Marker and other steps whose own stand fails stand at that presence's shape centre instead. Test tool only.
constexpr int32_t kC284PresenceRings = 3;  // ring radii: 1/4, 2/4, 3/4 of the shape's x, y reach
constexpr int32_t kC284PresenceSpokes = 8; // points per ring
constexpr double kC284PresenceNearUu = 5000.0; // b274: a presence further from a marker never stands in for it
void c284_off_mesh_floor(const void* pawn, Vec3& at);
bool c284_presence_stand(const void* world, void* host_pawn, C284Stand& s, void* presence = nullptr) {
    void* const pp = presence != nullptr ? presence : c64_find_actor(world, "BP_PlayerPresence_C", s.target);
    C126Shape sh{};
    if (pp == nullptr || !c126_shape_find(pp, sh, nullptr)) {
        return false;
    }
    Vec3 nav{};
    const char* from = "presence centre";
    const double half = c284_half_height(host_pawn);
    ObjectName zone{};
    // Full z extent (b7b-2 probe, LOS_O01_SO01). c423b127b line 290 (C2M1): the centre of a tall presence projected
    // to navmesh about 3200 uu below it, inside fall zone 24, and c284_go_start refused the landing; a centre point
    // in a fall zone falls through to the ring points, which skip fall zones too.
    bool centre = c126_project(world, host_pawn, sh.center, &sh, nav, nullptr);
    if (centre && c284_in_fall_zone(world, host_pawn, Vec3{nav.x, nav.y, nav.z + half + kC284WarpUp}, zone)) {
        log::line("card336: presence centre of %s at (%.0f, %.0f, %.0f) skipped: inside %s", object_name(pp).text,
                  nav.x, nav.y, nav.z, zone.text);
        centre = false;
    }
    // c247b295a (FED_C5M2 Finale_SO0): BP_PlayerPresence_C_23's centre projected to a navmesh point 513 uu off it and
    // outside its shape, so the players stood beside the presence that completes the step. The centre point counts
    // only with the pawn's capsule inside the shape; otherwise the ring points, then the static floor under the
    // centre (off the navmesh), and the outside centre point only as the last resort.
    const Vec3 outside = nav;
    const bool centre_outside = centre && !c126_capsule_inside(sh, host_pawn, Vec3{nav.x, nav.y, nav.z + half});
    if (centre_outside) {
        log::line("c284: presence centre of %s projects to (%.0f, %.0f, %.0f), outside its shape; trying ring points",
                  object_name(pp).text, nav.x, nav.y, nav.z);
        centre = false;
    }
    if (!centre) {
        // b11 C2M1 soak run 3 (LOS_O02_SO02, BP_PlayerPresence_C_5 at (156649, 23887, 10480)): no navmesh point at
        // the centre; c255-c2m1-r2 completed the step from (158906, 23488, 10615), 2300 uu off it. So: rings
        // around the centre inside the shape, the first point that projects with the pawn's capsule inside.
        const bool round = sh.kind == C126ShapeKind::Sphere || sh.kind == C126ShapeKind::Capsule;
        const double reach = round ? sh.ext.x : fmin(sh.ext.x, sh.ext.y);
        bool found = false;
        for (int32_t i = 0; i < kC284PresenceRings * kC284PresenceSpokes && !found; ++i) {
            const double r = reach * (i / kC284PresenceSpokes + 1) / (kC284PresenceRings + 1);
            const double a = 2.0 * 3.14159265358979323846 * (i % kC284PresenceSpokes) / kC284PresenceSpokes;
            const Vec3 want{sh.center.x + cos(a) * r, sh.center.y + sin(a) * r, sh.center.z};
            found = c126_project(world, host_pawn, want, &sh, nav, nullptr) &&
                    c126_capsule_inside(sh, host_pawn, Vec3{nav.x, nav.y, nav.z + half}) &&
                    !c284_in_fall_zone(world, host_pawn, Vec3{nav.x, nav.y, nav.z + half + kC284WarpUp}, zone);
        }
        from = "presence ring point";
        if (!found) {
            nav = sh.center;
            c284_off_mesh_floor(host_pawn, nav);
            found = c126_capsule_inside(sh, host_pawn, Vec3{nav.x, nav.y, nav.z + half});
            from = "presence centre (off the navmesh)";
        }
        if (!found && centre_outside) {
            nav = outside;
            found = true;
            from = "presence centre (outside the shape)";
        }
        if (!found) {
            return false;
        }
    }
    s.actor = pp;
    s.from = from;
    s.target = sh.center;
    s.shape = sh;
    s.nav = nav;
    return true;
}

constexpr int32_t kC284PresenceTries = 3; // actorless step: presences tried, one per `objective go` in a row
static_assert(kC284PresenceTries == 3, "c284_nth_presence compares three skip slots");
// The switched-on BP_PlayerPresence_C nearest `from` once the `nth` nearest presences of ALL (on or off) are left
// out, or null. Test tool only.
// Card 514 (c513a FED_C4M1 Metro_SO2 d1): the skip ranks used to count switched-on presences only. Try 1 stood in
// BP_PlayerPresence_C_13, whose entry runs imp858(false) on it (14000); try 2 (nth 1) then skipped the nearest live
// one, C_70 (the level end, 7722 uu), and stood at C_191 (8620), try 3 at (-2500, -95400). The ranks now count every
// presence, so a presence spent by an earlier try no longer shifts the next try past the live one after it.
// Card 516 (c244b322a, FED_C4M1 Bridge_SO1): ranking every presence repeated a pick. Nearest to the marker were C_9
// (try 1's own stand), C_256 (already switched off), C_255, then C_12 (Bridge_SO2's trigger). Try 2 skipped C_9 and
// stood in C_255; try 3 skipped C_9 and C_256 and stood in C_255 again, so C_12 was never entered and the step VOIDed
// after a 312 s stay. Now try n skips the nearest presence of all (try 1's) and the presences tries 2 to n-1 of the
// same step stood in, and takes the nearest switched-on one left: every try enters a new presence. The picks are
// remembered per `from` point; they are compared as addresses only, never read.
struct C284Picks {
    Vec3 from;
    void* picked[kC284PresenceTries];
};
C284Picks g_c284_picks{};

void* c284_nearest_presence(const void* world, const Vec3& from, void* const (&skip)[kC284PresenceTries + 1],
                            bool live_only) {
    void* pp = nullptr;
    double best = 0.0;
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    for (int32_t l = 0; levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const auto* const actors =
            levels[l] != nullptr ? static_cast<void* const*>(read_ptr(levels[l], kLevelActors)) : nullptr;
        const int32_t num = levels[l] != nullptr ? read_i32(levels[l], kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; actors != nullptr && a < num && a < kMaxLevelActors; ++a) {
            void* const x = actors[a];
            bool skipped = x == nullptr;
            for (int32_t k = 0; k <= kC284PresenceTries && !skipped; ++k) {
                skipped = skip[k] != nullptr && x == skip[k];
            }
            if (skipped || strcmp(class_name(x).text, "BP_PlayerPresence_C") != 0) {
                continue;
            }
            C132ExtField f{};
            if (live_only && (!c132_ext_field(x, f) || (f.ext & 1u) == 0)) {
                continue; // switched off: the level script used it up (run 2: BP_PlayerPresence_C_1)
            }
            const double d = vec_dist(actor_location(x), from);
            if (pp == nullptr || d < best) {
                pp = x;
                best = d;
            }
        }
    }
    return pp;
}

void* c284_nth_presence(const void* world, const Vec3& from, int32_t nth) {
    void* skip[kC284PresenceTries + 1] = {};
    if (nth <= 0) {
        return c284_nearest_presence(world, from, skip, true);
    }
    const bool same = vec_dist(g_c284_picks.from, from) < 1.0;
    if (nth == 1 || !same) {
        g_c284_picks = C284Picks{from, {}};
    }
    skip[0] = c284_nearest_presence(world, from, skip, false); // try 1's presence, on or off
    const int32_t prior = nth - 1 < kC284PresenceTries ? nth - 1 : kC284PresenceTries - 1;
    for (int32_t k = 0; k < prior; ++k) {
        skip[k + 1] = g_c284_picks.picked[k];
    }
    void* const pp = c284_nearest_presence(world, from, skip, true);
    if (prior < kC284PresenceTries) {
        g_c284_picks.picked[prior] = pp;
    }
    // card 514 review: a wrong rank shows here, not as a VOID
    log::line("c284: nth presence %d: skipped %s%s%s, picked %s", nth,
              skip[0] != nullptr ? prop_name(static_cast<const uint8_t*>(skip[0]) + kObjectName, 8).text : "none",
              skip[1] != nullptr ? ", " : "",
              skip[1] != nullptr ? prop_name(static_cast<const uint8_t*>(skip[1]) + kObjectName, 8).text : "",
              pp != nullptr ? prop_name(static_cast<const uint8_t*>(pp) + kObjectName, 8).text : "none");
    return pp;
}

// Card 244 b237 (c244b236a FED_C4M1, user "huge wait before client warped"): the map's hub shows Stadium_Marker,
// Park_Marker and Bridge_Marker at once (676792065, bools 11); reaching any one picks that path. The picker took
// Bridge_O1 (several marked parents: the lowest slot), whose presence BP_PlayerPresence_C_256 was spent (entered at
// 676764056, ext off 676792101, enabled 0), so 9 blocks of Bridge_Marker go + waits idled 210 s until try 3 landed by
// chance on BP_PlayerPresence_C_0 under Stadium_Marker's actor (677019851 OnTriggerEntered, Stadium_SO1 shown).
// A marker step left open after its first `go` therefore stands next at the live presence under another top-level
// objective's marker step (open, marker on, kind marker): the hub's other choices, objective-driven, no table.
constexpr double kC284HubPresenceUu = 5000.0; // a hub marker's presence lies within this of its actor (C_0: 2570)
constexpr int32_t kC284HubMax = 4;            // hub choices tried
constexpr size_t kC284PresenceEnabled = 0x30A; // bool: trigger events enabled (objectives.cpp kPresenceEnabled)

const uint8_t* c284_top(const uint8_t* o) {
    for (int32_t depth = 0; depth < 8 && o != nullptr; ++depth) {
        const auto* const up = static_cast<const uint8_t*>(read_ptr(o, kObjectiveParent));
        if (up == nullptr) {
            return o;
        }
        o = up;
    }
    return nullptr;
}

// The live presence of the `k`th hub choice besides `cur` (map slot order), or null; `step` gets the choice.
void* c284_hub_presence(const void* world, const uint8_t* cur, int32_t k, const uint8_t** step) {
    SparseView v{};
    if (cur == nullptr || !c49_objectives(g_c49_get_subsystem(world), v)) {
        return nullptr;
    }
    const uint8_t* const own = c284_top(cur);
    int32_t seen = 0;
    for (int32_t i = 0; i < v.count && seen < kC284HubMax; ++i) {
        const uint8_t* const o = c284_obj(v, i);
        if (o == nullptr || o == cur || o[kObjectiveComplete] != 0 || o[kObjectiveMarker] == 0 ||
            read_ptr(o, kObjectiveParent) == nullptr || c284_top(o) == own || c126_children(world, o) > 0 ||
            c126_kind_of(world, o) != C126Kind::Marker) {
            continue;
        }
        void* const actor = c126_actor(o) != nullptr ? c126_actor(o) : c126_marker_of(world, o);
        void* const pp = actor != nullptr ? c284_nth_presence(world, actor_location(actor), 0) : nullptr;
        if (pp == nullptr || static_cast<const uint8_t*>(pp)[kC284PresenceEnabled] == 0 ||
            vec_dist(actor_location(pp), actor_location(actor)) > kC284HubPresenceUu) {
            continue;
        }
        if (seen++ == k) {
            *step = o;
            return pp;
        }
    }
    return nullptr;
}

// b11 C2M1 soak runs 1 and 2, 2026-11-14 (LOS_O02_SO01: actors 0, no marker actor). The level script shows such a
// step's place with a sibling marker row (Plasma_Marker, FlakBug_Marker, CrashedDropship_Marker) and completes it
// from a presence volume near it (c255-c2m1-r2: SO01 completed in the frame the host was put at (158906, 23488,
// 10615), 2300 uu from BP_PlayerPresence_C_5; the presence nearest the HOST was the previous step's, used up). The
// objective names no presence, so each `objective go` in a row for the same step takes the next nearest presence
// to the sibling marker's actor (to the host with no such sibling), kC284PresenceTries at most. Test tool only.
// c247b288a (FED_C5M2, b288): Assassin_SO2 is actorless with no sibling marker; the level script completes it on
// entering BP_PlayerPresence_C_6 (card 247 table row S2, @3057), about 89000 uu from the host. The nearest-presence
// tries never reach it (50 failed waits, soak ended without Finale). A row here names the presence the script
// completes the step from, taken from a static script trace; it is tried before the nearest-presence tries.
// Names only, no coordinates. Test tool only.
struct C284PresenceHint {
    const char* objective;
    const char* presence;
};
constexpr C284PresenceHint kC284PresenceHints[] = {
    {"Assassin_SO2", "BP_PlayerPresence_C_6"}, // FED_C5M2, card 247 row S2
    // FED_C5M2, card 247 row S3: entering C_21 (@8152) starts SetWaves C_7; its end event completes SO3 (@8230).
    // c247b297a (b297): the nearest-presence try stood at C_617, 38284 uu off; SO3 stayed open, no Finale_O1.
    {"Assassin_SO3", "BP_PlayerPresence_C_21"},
    // FED_C5M2, card 247 row F6: entering C_408 (@30571) runs `Nuke Town` and completes SO6 (@30898).
    {"Finale_SO6", "BP_PlayerPresence_C_408"},
    // FED_C5M2, card 247 row F5: the complete call (@31007) comes from entering C_62 (@35214).
    {"Finale_SO5", "BP_PlayerPresence_C_62"},
};
constexpr int32_t kC284Hints = static_cast<int32_t>(sizeof(kC284PresenceHints) / sizeof(kC284PresenceHints[0]));

// The hinted presence of objective `o`, or null (no row, or no such placed actor on this map).
static void* c284_hint_presence(const void* world, const uint8_t* o, const Vec3& from) {
    const auto id = fname_text(o + kObjectiveId);
    for (int32_t i = 0; i < kC284Hints; ++i) {
        if (strcmp(id.text, kC284PresenceHints[i].objective) == 0) {
            return c64_find_actor(world, kC284PresenceHints[i].presence, from);
        }
    }
    return nullptr;
}

const char* c284_actorless_stand(const void* world, void* host_pawn, const uint8_t* o, C284Stand& s, int32_t nth) {
    if (host_pawn == nullptr) {
        return "no actor, no objective marker actor and no host pawn";
    }
    void* const hint = c284_hint_presence(world, o, actor_location(host_pawn));
    if (hint != nullptr) {
        s.target = actor_location(hint);
        if (!c284_presence_stand(world, host_pawn, s, hint)) {
            return "no actor, no objective marker actor and no navmesh point in the hinted presence";
        }
        const Vec3 at = actor_location(hint);
        log::line("c284: actorless step stands at hinted presence %s at (%.0f, %.0f, %.0f), %.0f uu from the host",
                  object_name(hint).text, at.x, at.y, at.z, vec_dist(at, actor_location(host_pawn)));
        s.from = "hinted presence (script trace)";
        s.yaw = c284_yaw_to(s.nav, s.target);
        return nullptr;
    }
    const void* const up = read_ptr(o, kObjectiveParent);
    void* mark = nullptr;
    SparseView v{};
    if (c49_objectives(g_c49_get_subsystem(const_cast<void*>(world)), v)) {
        for (int32_t i = 0; i < v.count && mark == nullptr; ++i) {
            const uint8_t* const sib = c284_obj(v, i);
            if (sib != nullptr && sib != o && read_ptr(sib, kObjectiveParent) == up && sib[kObjectiveComplete] == 0 &&
                c126_kind_of(world, sib) == C126Kind::Marker) {
                mark = c126_actor(sib);
            }
        }
    }
    const Vec3 base = actor_location(mark != nullptr ? mark : host_pawn);
    const int32_t n = nth % kC284PresenceTries;
    void* const pp = c284_nth_presence(world, base, n);
    s.target = base;
    if (pp == nullptr || !c284_presence_stand(world, host_pawn, s, pp)) {
        return "no actor, no objective marker actor and no presence with a navmesh point near the sibling marker";
    }
    const Vec3 at = actor_location(pp);
    log::line("c284: actorless step stands at presence try %d of %d: %s at (%.0f, %.0f, %.0f), %.0f uu from %s %s", n + 1,
              kC284PresenceTries, object_name(pp).text, at.x, at.y, at.z, vec_dist(at, base),
              mark != nullptr ? "sibling marker actor" : "the host,", mark != nullptr ? object_name(mark).text : "no sibling marker");
    s.from = "presence near the sibling marker";
    s.yaw = c284_yaw_to(s.nav, s.target);
    return nullptr;
}

// b12 item 1 (b11 C1M1 soak: Assault_SO4's switch had no navmesh point within use range along its root yaw):
// kind Use tries stand points around the switch in a fixed order, root yaw, yaw + 180, the four world axis
// directions, then the switch itself. b13 (b12 run 2: the nearest point, 189 uu, failed a 150 uu cap while the real
// press in c224 came from 204 uu): the try with the smallest 2D distance wins, ties to the earlier try, no cap. The
// client's focus= look gate decides whether E is pressed. Fails only when no try projects. Every try is projected
// so the row carries each distance. Test tool only.
// b194 (card 245, c245b193a Bridge_SO4): the nearest try put the client on BP_Switch_C_3's back side (switch 100 uu
// beat yaw 199 uu); the camera ray passed through the panel and focus stayed none. A switch's root yaw try (its face
// side, card 264 p3) now wins whenever it projects within kC284UseReachUu; nearest stays the fallback. Weapon
// pickups have no face side and keep nearest (c284_pickup_stand).
// b241 (c247b240a, user "not facing lever"): BP_PanelSwitch's root sits on one edge of its bounds, so tries measured
// from the root put the yaw stand 184..195 uu off the panel face, or inside the panel. A face-first (switch) stand now
// measures every try from the bounds face on the yaw side: the bounds centre plus the yaw direction scaled to the box
// edge, at the root's height. The look target (s.target) stays the root.
constexpr double kC284UseReachUu = 250.0; // the game interactor's trace length (+0xF8, read 250 in c245b193a cmd 28)
constexpr double kC284LeaveUu = 100000.0; // go: how far up a pawn already inside the volume is lifted before the entry warp
constexpr int32_t kC284UseTries = 7;
constexpr const char* kC284UseTryName[kC284UseTries] = {"yaw", "yaw+180", "+x", "+y", "-x", "-y", "switch"};

bool c284_use_stand(const void* world, void* host_pawn, C284Stand& s, bool face_first) {
    const void* const root = read_ptr(s.actor, kActorRootComponent);
    const double yaw = (root != nullptr ? component_yaw(root) : 0.0) / kRadToDeg; // its face side (card 264 p3)
    const double half = 180.0 / kRadToDeg;
    const double ang[kC284UseTries] = {yaw, yaw + half, 0.0, half * 0.5, half, half * 1.5, 0.0};
    s.from = "root";
    Vec3 o = s.target; // where every try and its distance are measured from
    if (face_first && g_actor_bounds != nullptr) {
        Vec3 centre{}, extent{};
        g_actor_bounds(s.actor, false, &centre, &extent, false);
        const double cx = cos(yaw), sy = sin(yaw);
        const double reach = fabs(cx) * extent.x + fabs(sy) * extent.y; // centre to the box edge along the yaw
        o = Vec3{centre.x + cx * reach, centre.y + sy * reach, s.target.z};
        log::line("c284: use stand %s from its bounds face: centre (%.0f, %.0f, %.0f), extent (%.0f, %.0f, %.0f), yaw "
                  "%.0f deg, face (%.0f, %.0f, %.0f), root (%.0f, %.0f, %.0f)", object_name(s.actor).text, centre.x,
                  centre.y, centre.z, extent.x, extent.y, extent.z, yaw * kRadToDeg, o.x, o.y, o.z, s.target.x,
                  s.target.y, s.target.z);
    }
    int32_t win = -1;
    double best = 0.0;
    char row[512];
    int32_t n = snprintf(row, sizeof row, "c284: use stand %s:", object_name(s.actor).text);
    for (int32_t i = 0; i < kC284UseTries; ++i) {
        const double off = i == kC284UseTries - 1 ? 0.0 : kC284UseUu;
        const Vec3 want{o.x + cos(ang[i]) * off, o.y + sin(ang[i]) * off, o.z};
        Vec3 nav{};
        double d2 = -1.0;
        if (c126_project(world, host_pawn, want, nullptr, nav, nullptr)) {
            d2 = sqrt((nav.x - o.x) * (nav.x - o.x) + (nav.y - o.y) * (nav.y - o.y));
            const bool face_held = face_first && win == 0 && best <= kC284UseReachUu; // the yaw try is index 0
            if (!face_held && (win < 0 || d2 < best)) {
                win = i;
                best = d2;
                s.nav = nav;
            }
        }
        if (n > 0 && n < static_cast<int32_t>(sizeof row)) {
            n += d2 < 0.0 ? snprintf(row + n, sizeof row - n, " %s none", kC284UseTryName[i])
                          : snprintf(row + n, sizeof row - n, " %s %.0f uu", kC284UseTryName[i], d2);
        }
    }
    if (win >= 0) {
        const bool face = face_first && win == 0 && best <= kC284UseReachUu;
        log::line("%s; winner %s %.0f uu (%s)", row, kC284UseTryName[win], best,
                  face ? "face side within reach" : "nearest");
    } else {
        log::line("%s; winner none (no try projects)", row);
    }
    if (win >= 0) {
        s.from = kC284UseTryName[win];
    }
    return win >= 0;
}

// Card 245 (b187 c245b187a: Plaza_SO3's BP_WeaponPickup_FlameThrower_C_0 at (-26434, 15135, 2766), every switch try
// "none"): a use target that is a weapon pickup takes its look target and stand point from its own bounds, as the
// test runner's `goto actor` / `look actor ... bounds` do for the Morita pickup in c448. The bounds' bottom is the
// floor it rests on; the switch tries run from there first. When none of those projects to the navmesh either, the
// players stand on that floor kC284UseUu from the pickup, on the host's side, off the navmesh (logged).
bool c284_is_pickup(const void* actor) {
    return strstr(class_name(actor).text, "WeaponPickup") != nullptr ||
           strstr(object_name(actor).text, "WeaponPickup") != nullptr;
}

// b274 (c243b273a Monorail_SO3: the face stand on the switch bounds' bottom, z 1620, sat 59 uu over the real floor; the
// client fell 68 uu after the warp and the go failed its drift check): an off-navmesh stand drops onto the static
// floor (kEccWorldStatic) found within kC284FloorDownUu under it, traced from kC284FloorUpUu over it. Without one
// (or a start inside geometry) the stand keeps its bounds height (logged).
constexpr double kC284FloorUpUu = 100.0;
constexpr double kC284FloorDownUu = 300.0;
void c284_off_mesh_floor(const void* pawn, Vec3& at) {
    bool inside = false;
    const Vec3 low{at.x, at.y, at.z - kC284FloorDownUu};
    const Vec3 hit = pawn != nullptr ? trace_channel(pawn, Vec3{at.x, at.y, at.z + kC284FloorUpUu}, low, kEccWorldStatic,
                                                     nullptr, nullptr, &inside)
                                     : low;
    const bool found = pawn != nullptr && !inside && hit.z > low.z + 1.0;
    log::line("c284: off-navmesh stand (%.0f, %.0f, %.0f): static floor %s (z %.0f)%s", at.x, at.y, at.z,
              pawn == nullptr ? "not traced (no host pawn)" : inside ? "inside geometry" : found ? "found" : "none",
              hit.z, found ? ": stands on it" : ": keeps the bounds height");
    if (found) {
        at.z = hit.z;
    }
}

bool c284_pickup_stand(const void* world, void* host_pawn, C284Stand& s) {
    if (g_actor_bounds == nullptr) {
        return false;
    }
    Vec3 centre{}, extent{};
    g_actor_bounds(s.actor, false, &centre, &extent, false);
    const Vec3 root = actor_location(s.actor);
    const Vec3 floor{centre.x, centre.y, centre.z - extent.z};
    s.target = floor; // the switch tries project around the floor point
    const bool on_mesh = c284_use_stand(world, host_pawn, s, false); // a pickup has no face side: nearest
    s.target = centre; // the look target: the pickup's box centre, not its root
    if (on_mesh) {
        log::line("c284: use stand %s from its bounds: centre (%.0f, %.0f, %.0f), extent (%.0f, %.0f, %.0f), root "
                  "(%.0f, %.0f, %.0f), navmesh stand (%.0f, %.0f, %.0f) %s", object_name(s.actor).text, centre.x,
                  centre.y, centre.z, extent.x, extent.y, extent.z, root.x, root.y, root.z, s.nav.x, s.nav.y, s.nav.z,
                  s.from);
        return true;
    }
    const Vec3 h = host_pawn != nullptr ? actor_location(host_pawn) : Vec3{floor.x + 1.0, floor.y, floor.z};
    const double a = atan2(h.y - floor.y, h.x - floor.x);
    const double out = kC284UseUu + (extent.x > extent.y ? extent.x : extent.y);
    s.nav = Vec3{floor.x + cos(a) * out, floor.y + sin(a) * out, floor.z};
    c284_off_mesh_floor(host_pawn, s.nav);
    s.from = "pickup bounds (off the navmesh)";
    log::line("WARN c284: use stand %s: no navmesh point near its bounds (centre (%.0f, %.0f, %.0f), extent (%.0f, "
              "%.0f, %.0f), root (%.0f, %.0f, %.0f)); stand off the navmesh at (%.0f, %.0f, %.0f), %.0f uu toward the "
              "host", object_name(s.actor).text, centre.x, centre.y, centre.z, extent.x, extent.y, extent.z, root.x,
              root.y, root.z, s.nav.x, s.nav.y, s.nav.z, out);
    return true;
}

// b272 (c243b271c Monorail_SO3: BP_WallSwitch_C at (-40294, 54387, 1720) with the host 11800 uu away, every switch
// try "none") while c243-c3m2-b6r2 stood the host at (-40178, 54416, 1739) and pressed it there. As the pickup
// fallback does: the players stand kC284UseUu out from the bounds face along the root yaw, on the bounds' bottom, off
// the navmesh (logged). False only without GetActorBounds.
bool c284_face_stand(const void* host_pawn, C284Stand& s) {
    if (g_actor_bounds == nullptr) {
        return false;
    }
    const void* const root = read_ptr(s.actor, kActorRootComponent);
    const double yaw = (root != nullptr ? component_yaw(root) : 0.0) / kRadToDeg;
    Vec3 centre{}, extent{};
    g_actor_bounds(s.actor, false, &centre, &extent, false);
    const double cx = cos(yaw), sy = sin(yaw);
    const double out = fabs(cx) * extent.x + fabs(sy) * extent.y + kC284UseUu; // the box edge, then kC284UseUu more
    s.nav = Vec3{centre.x + cx * out, centre.y + sy * out, centre.z - extent.z};
    c284_off_mesh_floor(host_pawn, s.nav);
    s.from = "switch face (off the navmesh)";
    log::line("WARN c284: use stand %s: no try projects to the navmesh; stand off the navmesh at (%.0f, %.0f, %.0f), "
              "%.0f uu out from the bounds centre (%.0f, %.0f, %.0f) along yaw %.0f deg", object_name(s.actor).text,
              s.nav.x, s.nav.y, s.nav.z, out, centre.x, centre.y, centre.z, yaw * kRadToDeg);
    return true;
}

// Card 244 b212 (c244b211a, FED_C4M1 LZ_SO2 at ActorReference_C_12): a marker step whose `objective go` left it open
// names no presence; the level script completes it from one the go stand does not reach (C_257 there). So each `go`
// in a row for the same marker step after the first stands at the next nearest switched-on presence to the marker
// actor (nth 1, then 2), as an actorless step does: objective-driven, no table. Null on success, else why not.
const char* c284_marker_next_presence(const void* world, void* host_pawn, C284Stand& s, int32_t nth,
                                      const uint8_t* cur) {
    void* const mark = s.actor;
    const Vec3 base = s.target;
    // The hub choices only when the marker's own presence (the nearest one) is spent: LZ_SO2's live C_257 path stays.
    const auto* const own = static_cast<const uint8_t*>(c64_find_actor(world, "BP_PlayerPresence_C", base));
    C132ExtField f{};
    const bool own_live = own != nullptr && own[kC284PresenceEnabled] != 0 && c132_ext_field(own, f) && (f.ext & 1u) != 0;
    const uint8_t* hub = nullptr;
    void* const hp = own_live ? nullptr : c284_hub_presence(world, cur, nth - 1, &hub);
    if (hp != nullptr && c284_presence_stand(world, host_pawn, s, hp)) {
        const Vec3 at = actor_location(hp);
        log::line("c284: marker step stands at hub choice %d: %s presence %s at (%.0f, %.0f, %.0f), %.0f uu from "
                  "marker actor %s", nth, fname_text(hub + kObjectiveId).text, prop_name(static_cast<const uint8_t*>(hp) +
                  kObjectName, 8).text, at.x, at.y, at.z, vec_dist(at, base), object_name(mark).text);
        s.from = "hub choice presence";
        s.yaw = c284_yaw_to(s.nav, s.target);
        return nullptr;
    }
    const int32_t n = nth % kC284PresenceTries;
    void* const pp = c284_nth_presence(world, base, n);
    // c247b308a (FED_C5M2): Array_SO4's try 3 had no presence left near its marker, so it stood in
    // BP_PlayerPresence_C_17, 52826 uu off: Assassin_SO1's trigger. The level script ran its entry before Assassin_SO1
    // was added (CompleteSubObjective flags 0x200) and switched C_17 off, so Assassin_SO1 could never complete; its
    // own tries then stood 46867 uu off. A next presence past kC284NextPresenceMaxUu belongs to another part of the
    // map: the step stays open instead (largest useful pick seen: 18140 uu, Array_SO4 C_16).
    constexpr double kC284NextPresenceMaxUu = 20000.0;
    if (pp != nullptr && vec_dist(actor_location(pp), base) > kC284NextPresenceMaxUu) {
        const Vec3 away = actor_location(pp);
        log::line("c284: marker step try %d: next presence %s at (%.0f, %.0f, %.0f) is %.0f uu from marker actor %s, "
                  "past %.0f uu: not entered", n + 1, prop_name(static_cast<const uint8_t*>(pp) + kObjectName, 8).text,
                  away.x, away.y, away.z, vec_dist(away, base), object_name(mark).text, kC284NextPresenceMaxUu);
        return "marker step left open: the next switched-on presence is too far from the marker actor";
    }
    if (pp == nullptr || !c284_presence_stand(world, host_pawn, s, pp)) {
        return "marker step left open: no next switched-on presence with a navmesh point near the marker actor";
    }
    const Vec3 at = actor_location(pp);
    log::line("c284: marker step stands at presence try %d of %d: %s at (%.0f, %.0f, %.0f), %.0f uu from marker actor "
              "%s", n + 1, kC284PresenceTries, object_name(pp).text, at.x, at.y, at.z, vec_dist(at, base),
              object_name(mark).text);
    s.from = "next presence near the marker";
    s.yaw = c284_yaw_to(s.nav, s.target);
    return nullptr;
}

// The navmesh point a step's players stand on (card 284 section 3). Null on success, else why not.
const char* c284_stand(const void* world, void* host_pawn, const uint8_t* o, C284Stand& s, int32_t nth = 0) {
    s = C284Stand{};
    s.kind = c126_kind_of(world, o);
    s.actor = c126_actor(o);
    s.from = "actor";
    if (s.actor == nullptr) {
        s.actor = c126_marker_of(world, o);
        s.from = "marker";
    }
    if (s.actor == nullptr) {
        return c284_actorless_stand(world, host_pawn, o, s, nth);
    }
    s.target = actor_location(s.actor);
    // Card 244 b213 (c244b212a): only a step whose actor is a bare location (a marker, or an ActorReference like LZ_SO2's
    // C_12) moves on to presences. A step whose own actor is the thing to act on (Park_SO1_A's
    // BP_SatchelBuilding_Flakbug_C_0) keeps its stand on every `go`, or the satchel plant never happens there.
    const bool bare_location = s.kind == C126Kind::Marker ||
                               (s.kind == C126Kind::Other && strstr(class_name(s.actor).text, "ActorReference") != nullptr);
    if (nth > 0 && bare_location) {
        return c284_marker_next_presence(world, host_pawn, s, nth, o);
    }
    // c247b292a (FED_C5M2 Finale_SO0): the marker ActorReference_C_8 sits at the centre of BP_PlayerPresence_C_23, and
    // the level script completes the step on entering C_23. Try 0 stood on the marker's own navmesh point, 532 uu off
    // and outside C_23; tries 1 and 2 (c284_marker_next_presence) then skip the nearest presence as already tried, so
    // C_23 was never entered. A bare location within kC284MarkerInPresenceUu of a switched-on presence's centre (so a
    // far presence never pulls the stand away from a marker whose wave spawns at the marker) stands inside that
    // presence first. c247b295a: the marker C_8 lay 404 uu from C_23's centre but outside its shape, so the old
    // "marker inside the shape" test skipped this path; nearness alone now decides.
    constexpr double kC284MarkerInPresenceUu = 1000.0;
    if (bare_location && host_pawn != nullptr) {
        void* const pp = c64_find_actor(world, "BP_PlayerPresence_C", s.target);
        C126Shape sh{};
        if (pp != nullptr && static_cast<const uint8_t*>(pp)[kC284PresenceEnabled] != 0 &&
            vec_dist(actor_location(pp), s.target) <= kC284MarkerInPresenceUu && c126_shape_find(pp, sh, nullptr) &&
            c284_presence_stand(world, host_pawn, s, pp)) {
            log::line("c284: marker %s lies %s %s; stand in the presence (%s)", object_name(s.actor).text,
                      c126_capsule_inside(sh, host_pawn, actor_location(s.actor)) ? "inside" : "near",
                      object_name(pp).text, s.from);
            s.yaw = c284_yaw_to(s.nav, s.target);
            return nullptr;
        }
    }
    Vec3 want = s.target;
    const bool volume = s.kind == C126Kind::Presence || s.kind == C126Kind::Count;
    if (volume && c126_shape_find(s.actor, s.shape, nullptr)) {
        s.from = "shape center";
        s.target = s.shape.center;
        want = s.target;
    } else if (s.kind == C126Kind::Use && c284_is_pickup(s.actor)) { // card 245
        if (!c284_pickup_stand(world, host_pawn, s)) {
            return "no GetActorBounds for the pickup";
        }
        s.yaw = c284_yaw_to(s.nav, s.target);
        return nullptr;
    } else if (s.kind == C126Kind::Use) {
        if (!c284_use_stand(world, host_pawn, s, true) && !c284_face_stand(host_pawn, s)) {
            return "no stand point around the switch projects to the navmesh, and no GetActorBounds";
        }
        s.yaw = c284_yaw_to(s.nav, s.target);
        return nullptr;
    } else if (s.kind == C126Kind::Nest || s.kind == C126Kind::Kill) {
        const Vec3 h = host_pawn != nullptr ? actor_location(host_pawn) : s.target;
        const double d = sqrt((h.x - s.target.x) * (h.x - s.target.x) + (h.y - s.target.y) * (h.y - s.target.y));
        const double ux = d > 1.0 ? (h.x - s.target.x) / d : 1.0, uy = d > 1.0 ? (h.y - s.target.y) / d : 0.0;
        want = Vec3{s.target.x + ux * kC284StandoffUu, s.target.y + uy * kC284StandoffUu, s.target.z};
    } // marker (review 4): on the anchor's own location
    if (!c126_project(world, host_pawn, want, s.shape.kind != C126ShapeKind::None ? &s.shape : nullptr, s.nav, nullptr)) {
        const bool marker = s.kind == C126Kind::Marker || s.kind == C126Kind::Other;
        if (!marker) {
            return "no navmesh point near the stand point";
        }
        // b274 (c243b273a FED_C3M2 Monorail_SO4: the marker ActorReference_C_9 at (-38208, 51466, 1730) had no
        // navmesh point; the nearest presence, BP_PlayerPresence_C_51, lay 9014 uu away, so the players stood there
        // and the wave at the marker that completes Monorail_O1 never met them; c243-c3m2-b6r2 completed it standing
        // at (-38231, 51499)). Only a presence within kC284PresenceNearUu of the marker stands in for it; otherwise
        // the players stand on the marker itself, on the static floor under it, off the navmesh (logged).
        void* const pp = c64_find_actor(world, "BP_PlayerPresence_C", s.target);
        const double off = pp != nullptr ? vec_dist(actor_location(pp), s.target) : -1.0;
        if (pp == nullptr || off > kC284PresenceNearUu || !c284_presence_stand(world, host_pawn, s, pp)) {
            s.nav = s.target;
            c284_off_mesh_floor(host_pawn, s.nav);
            s.from = "marker (off the navmesh)";
            log::line("WARN c284: marker stand: no navmesh point at (%.0f, %.0f, %.0f); nearest presence %s %.0f uu "
                      "off (near limit %.0f); stand off the navmesh at (%.0f, %.0f, %.0f)", s.target.x, s.target.y,
                      s.target.z, pp != nullptr ? object_name(pp).text : "none", off, kC284PresenceNearUu, s.nav.x,
                      s.nav.y, s.nav.z);
        }
    }
    s.yaw = c284_yaw_to(s.nav, s.target);
    return nullptr;
}

// Marker and other rows (review 4): the nearest BP_PlayerPresence_C, and whether a host pawn on the stand point
// would lie inside its shape (the presence the level script may complete the step by).
void c284_presence_row(const void* world, const void* host_pawn, const C284Stand& s, bool stood, uint32_t seq) {
    void* const pp = c64_find_actor(world, "BP_PlayerPresence_C", s.actor != nullptr ? s.target : Vec3{});
    if (pp == nullptr) {
        log::line("cmd: %u objective now:   nearest BP_PlayerPresence_C none", seq);
        return;
    }
    const Vec3 at = actor_location(pp);
    C126Shape sh{};
    const bool shaped = c126_shape_find(pp, sh, nullptr);
    const Vec3 body{s.nav.x, s.nav.y, s.nav.z + c284_half_height(host_pawn)};
    const char* const inside = !stood || !shaped || host_pawn == nullptr ? "-"
                               : c126_capsule_inside(sh, host_pawn, body) ? "1" : "0";
    // Card 245 (b192): the instance name (prop_name keeps the _NN number object_name drops) and the enabled byte.
    const auto* const pb = static_cast<const uint8_t*>(pp);
    log::line("cmd: %u objective now:   nearest presence %s at (%.0f, %.0f, %.0f), %.0f uu, stand inside %s, "
              "enabled %u",
              seq, prop_name(pb + kObjectName, 8).text, at.x, at.y, at.z,
              s.actor != nullptr ? vec_dist(at, s.target) : -1.0, inside, pb[0x30A]);
}

// One `now` row set for step `o`; tag current, doable or open. The stand rows for the current step only.
void c284_step_rows(const void* world, void* const pawns[3], const uint8_t* o, const char* tag, bool current,
                    uint32_t seq) {
    const uint64_t id = read_u64(o, kObjectiveId);
    const void* const parent = read_ptr(o, kObjectiveParent);
    const uint64_t pid = parent != nullptr ? read_u64(parent, kObjectiveId) : 0;
    char cnt[32];
    c284_count_text(o, cnt, sizeof(cnt));
    const C126Kind kind = c126_kind_of(world, o);
    log::line("cmd: %u objective now: parent %s; %s %s kind %s count %s marker %d", seq,
              pid != 0 ? fname_text(&pid).text : "-", tag, fname_text(&id).text, c126_kind_text(kind), cnt,
              o[kObjectiveMarker] != 0 ? 1 : 0);
    const int32_t n = array_num(o, kObjectiveActors);
    void* const* const data = static_cast<void* const*>(read_ptr(o, kObjectiveActors));
    for (int32_t k = 0; data != nullptr && k < n && k < kC284MaxActors; ++k) {
        if (data[k] == nullptr) {
            log::line("cmd: %u objective now:   actor[%d] null", seq, k);
            continue;
        }
        float health = 0.0f;
        const int dead = c25_dead(data[k], &health); // card 259: dead flag or health <= 0; -1 no health component
        const bool alive = !(dead == 1 || (dead == 0 && health <= 0.0f));
        const Vec3 at = actor_location(data[k]);
        log::line("cmd: %u objective now:   actor[%d] %s (%s) at (%.0f, %.0f, %.0f) alive %d", seq, k,
                  object_name(data[k]).text, class_name(data[k]).text, at.x, at.y, at.z, alive ? 1 : 0);
    }
    if (!current) {
        return;
    }
    C284Stand s{};
    const char* const why = c284_stand(world, pawns[0], o, s);
    double d[3] = {-1.0, -1.0, -1.0};
    for (int32_t i = 0; i < 3; ++i) {
        d[i] = pawns[i] != nullptr ? vec_dist(actor_location(pawns[i]), s.target) : -1.0;
    }
    if (why != nullptr) {
        log::line("cmd: %u objective now:   stand none from %s: %s", seq, s.from != nullptr ? s.from : "-", why);
    } else {
        log::line("cmd: %u objective now:   stand (%.0f, %.0f, %.0f) from %s %s ; host %.0f uu, client %.0f uu, "
                  "client2 %.0f uu", seq, s.nav.x, s.nav.y, s.nav.z, s.from, object_name(s.actor).text, d[0], d[1],
                  d[2]);
    }
    log::line("cmd: %u objective now:   needs: %s", seq, c126_kind_needs(kind));
    if (kind == C126Kind::Marker || kind == C126Kind::Other) {
        c284_presence_row(world, pawns[0], s, why == nullptr, seq);
    }
}

// What `o` is, for the expect VOID text.
const char* c284_state_text(const void* world, const uint8_t* o, const void* parent) {
    if (o == nullptr) {
        return "not in the map";
    }
    if (o[kObjectiveComplete] != 0) {
        return "complete";
    }
    if (parent == nullptr || !c284_under(o, parent)) {
        return "not under the active parent";
    }
    if (c126_children(world, o) > 0) {
        return "a container, not a step";
    }
    return "open, marker off (a future step)";
}

} // namespace

// Card 299: the OwnedComponents walk for callers outside this file.
int32_t owned_components(const void* actor, const void* out[], int32_t max) {
    return c336_owned(actor, out, max);
}

const char* c284_now(void* world, void* pc, uint32_t seq) {
    if (g_client_session) {
        return "host only";
    }
    SparseView v{};
    if (world == nullptr || !c49_objectives(g_c49_get_subsystem(world), v)) {
        return "no objective map";
    }
    const void* const parent = c284_parent(world, v, nullptr);
    if (parent == nullptr) {
        return "no open top-level objective";
    }
    void* pawns[3] = {};
    c284_pawns(pc, pawns);
    const uint8_t* const cur = c284_current(world, parent, pawns[0]);
    int32_t rows = 0;
    for (int32_t i = 0; i < v.count && rows < kC284MaxRows; ++i) {
        const uint8_t* const o = c284_obj(v, i);
        if (o == nullptr || o[kObjectiveComplete] != 0 || !c284_under(o, parent)) {
            continue;
        }
        ++rows;
        if (c126_children(world, o) > 0) {
            const uint64_t id = read_u64(o, kObjectiveId);
            char cnt[32];
            c284_count_text(o, cnt, sizeof(cnt));
            log::line("cmd: %u objective now: container %s count %s marker %d", seq, fname_text(&id).text, cnt,
                      o[kObjectiveMarker] != 0 ? 1 : 0);
            continue;
        }
        const char* const tag = o == cur ? "current" : c284_doable(world, v, o, parent) ? "doable" : "open";
        c284_step_rows(world, pawns, o, tag, o == cur, seq);
    }
    char list[256];
    c284_doable_list(world, v, parent, list, sizeof(list));
    const uint64_t pid = read_u64(parent, kObjectiveId);
    log::line("cmd: %u objective now: %d row(s) under %s, current %s; doable: %s", seq, rows, fname_text(&pid).text,
              cur != nullptr ? fname_text(&g_c284.current).text : "none", list);
    return cur != nullptr ? nullptr : "no current step (no open step with its marker on)";
}

bool c284_expect_step(void* world, void* /*pc*/, const char* id_text, bool timed_out, uint32_t seq, const char** why) {
    *why = nullptr;
    if (g_client_session) {
        *why = "host only";
        return true;
    }
    SparseView v{};
    const bool map = world != nullptr && c49_objectives(g_c49_get_subsystem(world), v);
    c284_reset(world);
    const uint8_t* const o = map ? c284_find_text(v, id_text) : nullptr;
    const void* const parent = map ? c284_parent(world, v, o) : nullptr;
    const bool kept = o != nullptr && read_u64(o, kObjectiveId) == g_c284.current && c284_is_step(world, o, parent);
    if (map && (kept || c284_doable(world, v, o, parent))) { // kept: a marker blink on the current step is no fail
        g_c284.current = read_u64(o, kObjectiveId);
        log::line("cmd: %u objective expect %s: %s (%s), current", seq, id_text, kept ? "current" : "doable",
                  c126_kind_text(c126_kind_of(world, o)));
        return true;
    }
    // b224 (c244b223a line 1246): the level script completed the step between `objective now` and this expect. The
    // step really completed, so no void; current stays as it was and the block's go and wait follow.
    if (o != nullptr && o[kObjectiveComplete] != 0) {
        log::line("cmd: %u objective expect %s: already complete", seq, id_text);
        return true;
    }
    if (!timed_out) {
        return false; // the level script may not have shown it yet (marker blink, the step before just completed)
    }
    char list[256];
    if (map && parent != nullptr) {
        c284_doable_list(world, v, parent, list, sizeof(list));
    } else {
        snprintf(list, sizeof(list), "none (%s)", map ? "no open top-level objective" : "no objective map");
    }
    snprintf(g_c284_why, sizeof(g_c284_why), "VOID objective expect %s: %s; doable: %s", id_text,
             c284_state_text(world, o, parent), list);
    *why = g_c284_why;
    return true;
}

// b238 (proof plumbing for cards 492, 493, 319, 491; c247b237a): a satchel site step can be worked only by a player
// whose interactor passes the site's CanInteract (it carries a charge; hold.cpp c133_can_interact). Any other step:
// true. The failure text of the last player tried goes to `why`.
bool c284_site_ready(void* const pawns[3], const uint8_t* o, char* why, size_t cap) {
    void* const actor = c126_actor(o);
    snprintf(why, cap, "not a satchel site");
    if (actor == nullptr || g_c54_satchel_site_class == nullptr || !is_a(actor, g_c54_satchel_site_class())) {
        return true;
    }
    void* const iface = g_c50_interactable_class != nullptr ? g_get_interface_address(actor, g_c50_interactable_class())
                                                            : nullptr;
    snprintf(why, cap, "%s", iface == nullptr ? "no IADInteractableInterface" : "no player interactor");
    for (int32_t i = 0; iface != nullptr && i < 3; ++i) {
        void* const interactor = pawns[i] != nullptr ? read_ptr(pawns[i], kCharacterInteractor) : nullptr;
        if (interactor != nullptr && c133_can_interact(iface, interactor, why, cap)) {
            return true;
        }
    }
    return false;
}

// `objective next` (b238, c247b237a: the sticky current Research_SO3_C, a satchel site, held six blocks while the
// Research_SO1 PanelSwitches were never tried). Makes the next doable step after the current one current, in sparse
// order with wrap; c284_doable is the marker test c126_pick uses, and a satchel site no player can interact with is
// skipped (c284_site_ready). None: the current step stays. Each slot is looked at once (bounded by the map's count).
const char* c284_next(void* world, void* pc, uint32_t seq) {
    if (g_client_session) {
        return "host only";
    }
    SparseView v{};
    if (world == nullptr || !c49_objectives(g_c49_get_subsystem(world), v)) {
        return "no objective map";
    }
    const void* const parent = c284_parent(world, v, nullptr);
    if (parent == nullptr) {
        return "no open top-level objective";
    }
    void* pawns[3] = {};
    c284_pawns(pc, pawns);
    const uint8_t* const cur = c284_current(world, parent, pawns[0]);
    const uint64_t from = cur != nullptr ? g_c284.current : 0;
    int32_t at = -1;
    for (int32_t i = 0; cur != nullptr && i < v.count; ++i) {
        at = c284_obj(v, i) == cur ? i : at;
    }
    for (int32_t k = 1; k <= v.count; ++k) {
        const uint8_t* const o = c284_obj(v, (at + k) % v.count);
        if (o == nullptr || o == cur || !c284_doable(world, v, o, parent)) {
            continue;
        }
        char why[160];
        if (!c284_site_ready(pawns, o, why, sizeof(why))) {
            log::line("cmd: %u objective next: skip %s: satchel site, CanInteract false for every player (\"%s\")", seq,
                      fname_text(o + kObjectiveId).text, why);
            continue;
        }
        g_c284.current = read_u64(o, kObjectiveId);
        log::line("cmd: %u objective next: %s -> %s (%s)", seq, from != 0 ? fname_text(&from).text : "none",
                  fname_text(&g_c284.current).text, c126_kind_text(c126_kind_of(world, o)));
        return nullptr;
    }
    log::line("cmd: %u objective next: none doable, current %s kept", seq, from != 0 ? fname_text(&from).text : "none");
    return nullptr;
}

// Card 336: a pawn already inside the step's volume is moved out before the entry warp, or the volume's begin
// overlap never fires. The lift point must leave the volume and must not land in a BP_FallZones_C (r6 cmds 162/167:
// the 100000 uu lift overlapped a zone, the game moved the client to its fixed point). Tries straight up first (the
// card 284 behaviour), then a short hop up and four sideways hops just past the shape's largest extent; the first
// point outside both wins. None: WARN and no lift (the settle reports what follows).
constexpr int32_t kC284LiftTries = 6;
constexpr const char* kC284LiftName[kC284LiftTries] = {"up far", "up", "+x", "-x", "+y", "-y"};

void c284_lift(const void* world, void* pawn, const C126Shape& sh, const Vec3& was, uint32_t seq, const char* who) {
    const double m = fmax(sh.ext.x, fmax(sh.ext.y, sh.ext.z)) * 2.0 + 500.0;
    const Vec3 tries[kC284LiftTries] = {{was.x, was.y, was.z + kC284LeaveUu}, {was.x, was.y, was.z + m},
                                        {was.x + m, was.y, was.z},           {was.x - m, was.y, was.z},
                                        {was.x, was.y + m, was.z},           {was.x, was.y - m, was.z}};
    for (int32_t k = 0; k < kC284LiftTries; ++k) {
        ObjectName zone{};
        if (c126_capsule_inside(sh, pawn, tries[k])) {
            continue;
        }
        if (c284_in_fall_zone(world, pawn, tries[k], zone)) {
            log::line("card336: %s lift %s at (%.0f, %.0f, %.0f) skipped: inside %s", who, kC284LiftName[k],
                      tries[k].x, tries[k].y, tries[k].z, zone.text);
            continue;
        }
        const bool left = g_set_actor_location(pawn, &tries[k], false, nullptr, kTeleportPhysics);
        log::line("cmd: %u objective go: %s was inside the step's volume, lifted out first (%s, moved %d)", seq, who,
                  kC284LiftName[k], left ? 1 : 0);
        return;
    }
    log::line("WARN card336: %s inside the step's volume and no lift point clear of it and of every fall zone", who);
}

// b237: the presser of a use step moves toward `target` in 2D (z kept), swept so a wall or the panel stops it.
// b255: it stops where its eye is kC284StepEyeUu from the target in 3D (2D = sqrt(200^2 - dz^2), 0 when the height
// alone is past it), and a pawn already inside that eye distance does not move. One row with 2D and 3D before/after.
// b257 gated it on the bounds face instead; card 512 restored the eye gate (reach kC284UseReachUu) and keeps the
// face distance in the rows only.
namespace {
// b257: the 2D distance from `p` to the nearest point of the box (centre c, extent e); `face` gets that point.
double c284_face_2d(const Vec3& p, const Vec3& c, const Vec3& e, Vec3& face) {
    face = Vec3{fmin(fmax(p.x, c.x - e.x), c.x + e.x), fmin(fmax(p.y, c.y - e.y), c.y + e.y), c.z};
    const double fx = face.x - p.x;
    const double fy = face.y - p.y;
    return sqrt(fx * fx + fy * fy);
}
} // namespace

void c284_step_in(void* pawn, const void* actor, const Vec3& target, uint32_t seq) {
    const Vec3 from = actor_location(pawn);
    const double dx = target.x - from.x;
    const double dy = target.y - from.y;
    const double dz = target.z - (from.z + kC284EyeUu);
    const double d3 = sqrt(dx * dx + dy * dy + dz * dz);
    Vec3 centre = target;
    Vec3 extent{0.0, 0.0, 0.0};
    const bool boxed = actor != nullptr && g_actor_bounds != nullptr;
    if (boxed) {
        g_actor_bounds(actor, false, &centre, &extent, false);
    }
    Vec3 face{};
    const double f2 = c284_face_2d(from, centre, extent, face);
    // Card 512 (c504k1, FED_C4M1 Stadium_SO1_B-D, BP_PanelSwitch_WithoutScreen_C): the bounds face was 100 uu off in
    // 2D while the eye was 369 uu from the target point, past the 250 uu reach, so the b257 face gate skipped the
    // step and every press missed. The eye distance to the target alone decides now: inside the reach, no move;
    // otherwise the pawn moves in 2D toward the target until its eye is kC284StepEyeUu from it in 3D (swept, so the
    // panel or a wall stops it first). The face distance stays in the rows.
    const double d2 = sqrt(dx * dx + dy * dy);
    const double r2 = fabs(dz) < kC284StepEyeUu ? sqrt(kC284StepEyeUu * kC284StepEyeUu - dz * dz) : 0.0;
    if (d3 <= kC284UseReachUu || d2 <= r2 || d2 < 1.0) {
        log::line("cmd: %u objective go: use step-in none (face 2D %.0f uu from %s, eye 3D %.0f uu, reach %.0f, 2D %.0f, "
                  "stop 2D %.0f)", seq, f2, boxed ? "bounds" : "target", d3, kC284UseReachUu, d2, r2);
        return;
    }
    const double k = (d2 - r2) / d2;
    const Vec3 to{from.x + dx * k, from.y + dy * k, from.z};
    alignas(16) uint8_t hit[kHitSize] = {};
    const bool moved = g_set_actor_location(pawn, &to, true, hit, kTeleportPhysics);
    const Vec3 at = actor_location(pawn);
    Vec3 face_at{};
    const double f2_at = c284_face_2d(at, centre, extent, face_at);
    const double ax = target.x - at.x;
    const double ay = target.y - at.y;
    const double az = target.z - (at.z + kC284EyeUu);
    log::line("%scmd: %u objective go: use step-in moved %d, face 2D %.0f -> %.0f uu from %s (eye 3D %.0f -> %.0f uu)",
              moved ? "" : "WARN ", seq, moved ? 1 : 0, f2, f2_at, boxed ? "bounds" : "target", d3,
              sqrt(ax * ax + ay * ay + az * az));
}

const char* c284_go_start(void* world, void* pc, uint8_t copy, uint32_t seq) {
    const uint8_t who = copy & static_cast<uint8_t>(~cmdfile::kGoHome);
    const bool home = (copy & cmdfile::kGoHome) != 0;
    if (g_client_session) {
        return "host only";
    }
    SparseView v{};
    if (world == nullptr || !c49_objectives(g_c49_get_subsystem(world), v)) {
        return "no objective map";
    }
    void* pawns[3] = {};
    c284_pawns(pc, pawns);
    const uint8_t* const cur = c284_current(world, c284_parent(world, v, nullptr), pawns[0]);
    for (int32_t i = 0; i < 3; ++i) {
        g_c284.placed[i] = C284Placed::None;
        g_c284.pawns[i] = nullptr;
        g_c284.still_from[i] = 0;
    }
    if (cur == nullptr) {
        return "no current step";
    }
    C284Stand& s = g_c284.stand;
    if (cur[kObjectiveMarker] != 0) {
        c284_note_shown(g_c284.current); // card 516: before the warp; a re-go starts inside the zone, marker hidden
    }
    g_c284.go_n = home ? 0 : c284_go_count(g_c284.current);
    const char* const why = c284_stand(world, pawns[0], cur, s, g_c284.go_n);
    if (home && why == nullptr && s.actor != nullptr) { // card 514 review: the home row names its volume
        log::line("cmd: %u objective go: home stand at %s (%s)", seq,
                  prop_name(static_cast<const uint8_t*>(s.actor) + kObjectName, 8).text, s.from);
    }
    if (why != nullptr) {
        return c284_fail("%s: %s", fname_text(&g_c284.current).text, why);
    }
    // Senior review 2, item 2: a presence step is placed by its volume; without one the step cannot be proven.
    if (s.kind == C126Kind::Presence && s.shape.kind == C126ShapeKind::None) {
        return c284_fail("%s: presence actor has no shape", fname_text(&g_c284.current).text, "");
    }
    // Item 3: kind other is placed as marker (actor[0], else the marker actor; c284_stand) and named in every
    // row; the run reads the `objective now` class rows afterwards. No action is derived from the kind.
    for (int32_t i = 0; i < 3; ++i) { // every named pawn exists before any warp
        if ((who == 3 || who == i) && pawns[i] == nullptr && !(who == 3 && i == 2)) {
            return c284_fail("no pawn for %s%s", kC284Who[i], "");
        }
    }
    // b14 (senior, run 3 Assault_SO4): kind use puts the first client, the presser, on the stand point and the host
    // on the first ring slot (client2 the next); every other kind keeps the host on the stand point.
    const int32_t on_stand = s.kind == C126Kind::Use && pawns[1] != nullptr ? 1 : 0;
    bool taken[kC284Slots] = {};
    Vec3 to[3] = {};
    bool go[3] = {};
    for (int32_t i = 0; i < 3; ++i) {
        g_c284.slot[i] = kC284SlotStand;
        if ((who != 3 && who != i) || pawns[i] == nullptr) {
            if (who == 3 && i == 2 && pawns[i] == nullptr) {
                log::line("cmd: %u objective go: client2 none (no second server copy)", seq);
            }
            continue;
        }
        Vec3 nav = s.nav; // the pawn on the stand point stands on it itself
        if (i != on_stand) {
            g_c284.slot[i] = c284_slot(world, pawns[i], s, taken, nav);
        }
        to[i] = Vec3{nav.x, nav.y, nav.z + c284_half_height(pawns[i]) + kC284WarpUp};
        go[i] = true;
        ObjectName zone{};
        if (c284_in_fall_zone(world, pawns[i], to[i], zone)) { // card 336: the game would move the pawn off the landing
            return c284_fail("%s: landing inside %s", kC284Who[i], zone.text);
        }
    }
    for (int32_t i = 0; i < 3; ++i) { // every landing is clear of the fall zones before the first warp (review 336)
        if (!go[i]) {
            continue;
        }
        // b11 C2M1 soak 2026-11-14: a pawn already inside the step's volume when the step is added gets no begin
        // overlap (LOS_O02_SO01 stayed open 90 s with both players inside). Such a pawn is lifted out first, so the
        // warp below is an entry. SetActorLocation updates overlaps before it returns.
        const Vec3 was = actor_location(pawns[i]);
        if (s.shape.kind != C126ShapeKind::None && c126_capsule_inside(s.shape, pawns[i], was)) {
            c284_lift(world, pawns[i], s.shape, was, seq, kC284Who[i]);
        }
        const bool moved = g_set_actor_location(pawns[i], &to[i], false, nullptr, kTeleportPhysics);
        g_c284.pawns[i] = pawns[i];
        g_c284.landed[i] = actor_location(pawns[i]);
        g_c284.placed[i] = moved ? C284Placed::Ok : C284Placed::NotMoved;
        if (moved && i == on_stand && s.kind == C126Kind::Use) {
            c284_step_in(pawns[i], s.actor, s.target, seq);
            g_c284.landed[i] = actor_location(pawns[i]);
        }
        if (i == 0 && pc != nullptr) {
            c284_aim(pc, g_c284.landed[0], s.target); // the host's own view only
        }
    }
    return nullptr; // c63_continue calls c284_go_settle once the settle (c.ms) is up
}

bool c284_go_still(void* world, void* pc, uint64_t now, uint32_t seq) {
    if (g_c284.world != world) {
        return true; // c284_go_settle rows the world change
    }
    void* pawns[3] = {};
    c284_pawns(pc, pawns);
    uint64_t still_ms = UINT64_MAX;
    int32_t placed = 0;
    for (int32_t i = 0; i < 3; ++i) {
        if (g_c284.placed[i] == C284Placed::None) {
            continue;
        }
        ++placed;
        if (pawns[i] != g_c284.pawns[i]) {
            return true; // c284_go_settle rows the changed pawn
        }
        const Vec3 at = actor_location(pawns[i]);
        const auto* const cmc = static_cast<const uint8_t*>(read_ptr(pawns[i], kCharacterMovement));
        const bool walking = cmc != nullptr && cmc[kCmcMovementMode] == kC284MoveWalking;
        if (g_c284.still_from[i] == 0 || !walking || vec_dist(at, g_c284.still_at[i]) > kC284StillUu) {
            g_c284.still_at[i] = at;
            g_c284.still_from[i] = now;
        }
        const uint64_t held = now - g_c284.still_from[i];
        still_ms = held < still_ms ? held : still_ms;
    }
    if (placed == 0 || still_ms < kC284StillMs) {
        return false;
    }
    log::line("cmd: %u objective go: settle ended early, %d pawn(s) walking and still %llu ms", seq, placed,
              static_cast<unsigned long long>(still_ms));
    return true;
}

const char* c284_go_settle(void* world, void* pc, uint32_t seq) {
    if (g_c284.world != world) {
        return "the world changed during the settle";
    }
    void* pawns[3] = {};
    c284_pawns(pc, pawns);
    const C284Stand& s = g_c284.stand;
    const bool shaped = s.shape.kind != C126ShapeKind::None;
    char first[192] = "";
    for (int32_t i = 0; i < 3; ++i) {
        if (g_c284.placed[i] == C284Placed::None) {
            continue;
        }
        if (pawns[i] != g_c284.pawns[i]) { // left or respawned during the settle: never read the old pointer
            log::line("cmd: %u objective go: %s kind %s pawn changed during the settle", seq, kC284Who[i],
                      c126_kind_text(s.kind));
            if (first[0] == '\0') {
                snprintf(first, sizeof(first), "%s: pawn changed during the settle", kC284Who[i]);
            }
            continue;
        }
        const Vec3 at = actor_location(pawns[i]);
        const double drift = vec_dist(at, g_c284.landed[i]);
        const auto* const cmc = static_cast<const uint8_t*>(read_ptr(pawns[i], kCharacterMovement));
        const unsigned mode = cmc != nullptr ? cmc[kCmcMovementMode] : kC284NoMode;
        const bool inside = shaped && c126_capsule_inside(s.shape, pawns[i], at);
        if (i == 0 && pc != nullptr) {
            c284_aim(pc, at, s.target);
        }
        char slot[32];
        c284_slot_text(g_c284.slot[i], slot, sizeof(slot));
        log::line("cmd: %u objective go: %s kind %s %s at (%.0f, %.0f, %.0f), %.0f uu from target, moved %.0f uu "
                  "since warp, mode %u, inside %s%s", seq, kC284Who[i], c126_kind_text(s.kind), slot, at.x, at.y,
                  at.z, vec_dist(at, s.target), drift, mode, shaped ? (inside ? "1" : "0") : "-",
                  g_c284.placed[i] == C284Placed::NotMoved ? ", SetActorLocation false" : "");
        if (first[0] != '\0') {
            continue; // every pawn is rowed; the first failure is the result
        }
        if (g_c284.placed[i] == C284Placed::NotMoved) {
            snprintf(first, sizeof(first), "%s: SetActorLocation failed", kC284Who[i]);
        } else if (drift > kC284DriftMaxUu) {
            snprintf(first, sizeof(first), "%s moved %.0f uu after the warp (mode %u)", kC284Who[i], drift, mode);
        } else if (shaped && !inside) {
            snprintf(first, sizeof(first), "%s not inside the %s volume after the settle", kC284Who[i],
                     c126_kind_text(s.kind));
        }
    }
    return first[0] != '\0' ? c284_fail("%s%s", first, "") : nullptr;
}

// c263e line 46 (also c263b, c263d; FED_C3M2): PowerPlant_Marker and PowerPlant_SO1 share ActorReference_C_1. The
// level script never completes the marker step: it completes SO1 on entering SO1's presence (15430613, try 2) and
// about 16 s later hides the marker and shows SO2..SO6 (15446462), so three waits on the marker failed and the group
// went VOID. A marker step is a guide for the sibling step that shares its actor: it ends when that sibling completes.
// c247b307a (FED_C5M2): Outpost_SO4 is a marker step on the same actor as Outpost_SO2, but SO2 completed (22499822)
// before SO4 was added (22500980); SO4 completes later by its own trigger (both players in C_402, the C_5 waves
// cleared). Ending it on SO2 made every wait on it "done" at once, 121 times. So a marker is a guide only when a wait
// saw its sibling still open while the marker was open (guide_ids); a sibling complete from the first look is not
// the step's guide. The sibling that ended it, or null.
namespace {
const uint8_t* c284_guide_done_by(const void* world, const SparseView& v, const uint8_t* o) {
    if (o == nullptr || o[kObjectiveComplete] != 0 || c126_kind_of(world, o) != C126Kind::Marker) {
        return nullptr;
    }
    const void* const actor = c126_actor(o);
    const void* const up = read_ptr(o, kObjectiveParent);
    if (actor == nullptr || up == nullptr) {
        return nullptr;
    }
    const uint64_t id = read_u64(o, kObjectiveId);
    bool seen = false;
    for (int32_t k = 0; k < g_c284.guide_n; ++k) {
        seen = seen || g_c284.guide_ids[k] == id;
    }
    for (int32_t i = 0; i < v.count; ++i) {
        const uint8_t* const sib = c284_obj(v, i);
        if (sib == nullptr || sib == o || read_ptr(sib, kObjectiveParent) != up || c126_actor(sib) != actor) {
            continue;
        }
        if (sib[kObjectiveComplete] == 0) {
            if (!seen && g_c284.guide_n < kC284GuideIds) {
                g_c284.guide_ids[g_c284.guide_n++] = id;
            }
            return nullptr;
        }
        if (seen) {
            return sib;
        }
    }
    return nullptr;
}

// Card 516: the other children of o's parent `up`: how many, how many complete, how many open with their marker
// shown, and the first such.
struct C284Sibs {
    int32_t total;
    int32_t done;
    int32_t shown;
    const uint8_t* shown_one;
};

C284Sibs c284_siblings(const SparseView& v, const uint8_t* o, const void* up) {
    C284Sibs s{};
    for (int32_t i = 0; i < v.count; ++i) {
        const uint8_t* const sib = c284_obj(v, i);
        if (sib == nullptr || sib == o || read_ptr(sib, kObjectiveParent) != up) {
            continue;
        }
        ++s.total;
        s.done += sib[kObjectiveComplete] != 0 ? 1 : 0;
        if (sib[kObjectiveComplete] == 0 && sib[kObjectiveMarker] != 0) {
            s.shown_one = s.shown == 0 ? sib : s.shown_one;
            ++s.shown;
        }
    }
    return s;
}

// Card 516 (c244b319a, FED_C4M1): Bridge_Marker and Stadium_Marker are zone guides. The level script hides the guide
// and shows its siblings while the players stand in the zone (Bridge_Marker bools 01 with Bridge_SO1 bools 11,
// 57733895) and flips back when they leave; the guide never completes on its own (it closes with Bridge_O1), so every
// wait on it failed and Bridge_SO1 started 90 s after the Add. Rule: an open kind-marker step whose own marker is off
// while an open sibling's marker is on was handed on, if its marker was shown at one of its goes (shown_ids: a step
// added hidden, Stadium_SO2 bools 01110 at 57910532, has not been handed anything). Held for kC284HandTicks wait
// ticks: Stadium_SO2 (a real flag step, lesson 18) is hidden and shown again inside 3 ms when the players cross the
// zone edge (57947014-57947017), and must not end on that. The sibling it was handed on to, or null.
constexpr int32_t kC284HandTicks = 30; // about half a second of engine ticks
const uint8_t* c284_handed_on(const void* world, const SparseView& v, const uint8_t* o) {
    if (o == nullptr || o[kObjectiveComplete] != 0 || o[kObjectiveMarker] != 0 ||
        c126_kind_of(world, o) != C126Kind::Marker) {
        g_c284.handed_ticks = 0;
        return nullptr;
    }
    const void* const up = read_ptr(o, kObjectiveParent);
    const C284Sibs s = up != nullptr ? c284_siblings(v, o, up) : C284Sibs{};
    const uint64_t id = read_u64(o, kObjectiveId);
    if (s.shown == 0 || !c284_has(g_c284.shown_ids, g_c284.shown_n, id)) {
        g_c284.handed_ticks = 0;
        return nullptr;
    }
    g_c284.handed_ticks = g_c284.handed_id == id ? g_c284.handed_ticks + 1 : 1;
    g_c284.handed_id = id;
    if (g_c284.handed_ticks < kC284HandTicks) {
        return nullptr;
    }
    const uint64_t to = read_u64(s.shown_one, kObjectiveId);
    if (s.shown == 1 && !c284_has(g_c284.zone_ids, g_c284.zone_n, to) && g_c284.zone_n < kC284GuideIds) {
        g_c284.zone_ids[g_c284.zone_n++] = to; // the sole shown sibling is the zone's own guide (Bridge_SO1)
    }
    g_c284.handed_ticks = 0;
    return s.shown_one;
}

// Card 516 (c244b319a): Bridge_SO1, the guide Bridge_Marker hands on to, never completes on its own either; the
// zone's real work is hidden (Bridge_SO2, closed by presence _12 at 57840983, lesson 13) and then added (Bridge_SO3 at
// 57840981). So a zone guide's wait ends when its parent gained a child or a child completed since the first wait
// tick on it. Only steps recorded as a sole handed-on sibling are zone guides: Stadium_Marker hands on to four shown
// siblings at once (SO1..SO4), so Stadium_SO2, a real step whose siblings can progress around it, is never one.
// True when the parent moved on.
bool c284_zone_moved(const SparseView& v, const uint8_t* o, int32_t* total, int32_t* done) {
    const uint64_t id = o != nullptr ? read_u64(o, kObjectiveId) : 0;
    const void* const up = o != nullptr ? read_ptr(o, kObjectiveParent) : nullptr;
    if (up == nullptr || o[kObjectiveComplete] != 0 || !c284_has(g_c284.zone_ids, g_c284.zone_n, id)) {
        return false;
    }
    const C284Sibs s = c284_siblings(v, o, up);
    *total = s.total;
    *done = s.done;
    if (g_c284.base_id != id) {
        g_c284.base_id = id;
        g_c284.base_total = s.total;
        g_c284.base_done = s.done;
        return false;
    }
    return s.total != g_c284.base_total || s.done != g_c284.base_done;
}
} // namespace

// One tick of `objective wait <id>`: true when it ends (*why null: complete; else the failure).
bool c284_wait_step(void* world, void* pc, const char* id_text, bool timed_out, const char** why) {
    *why = nullptr;
    if (g_client_session) {
        *why = "host only";
        return true;
    }
    SparseView v{};
    if (world == nullptr || !c49_objectives(g_c49_get_subsystem(world), v)) {
        *why = timed_out ? "no objective map" : nullptr;
        return timed_out;
    }
    const uint8_t* const found = c284_find_text(v, id_text);
    if (found != nullptr && found[kObjectiveComplete] != 0) {
        return true;
    }
    if (const uint8_t* const by = c284_guide_done_by(world, v, found); by != nullptr) {
        const uint64_t bid = read_u64(by, kObjectiveId);
        log::line("c284: objective wait %s: guide marker ended by %s (complete, shares actor %s)", id_text,
                  fname_text(&bid).text, object_name(c126_actor(found)).text);
        return true;
    }
    // Card 516: distinct rows; drive.py marker_held matches only "guide marker ended by ", so these end the routine.
    if (const uint8_t* const to = c284_handed_on(world, v, found); to != nullptr) {
        const uint64_t tid = read_u64(to, kObjectiveId);
        log::line("c284: objective wait %s: guide marker handed on to %s (own marker hidden, that one shown)",
                  id_text, fname_text(&tid).text);
        return true;
    }
    int32_t total = 0;
    int32_t done = 0;
    if (c284_zone_moved(v, found, &total, &done)) {
        log::line("c284: objective wait %s: zone guide passed, parent moved on (children %d to %d, complete %d to %d)",
                  id_text, g_c284.base_total, total, g_c284.base_done, done);
        return true;
    }
    if (!timed_out) {
        return false;
    }
    char cnt[32] = "-";
    if (found != nullptr) {
        c284_count_text(found, cnt, sizeof(cnt));
    }
    void* pawns[3] = {};
    c284_pawns(pc, pawns);
    const uint8_t* const cur = c284_current(world, c284_parent(world, v, nullptr), pawns[0]);
    char state[kNameChars + 64];
    snprintf(state, sizeof(state), "%s, count %s, current %s", found == nullptr ? "not in the map" : "open", cnt,
             cur != nullptr ? fname_text(&g_c284.current).text : "none");
    *why = c284_fail("%s: %s at the time", id_text, state);
    return true;
}

// Card 336, every engine tick, both sides: the fall zone rows once per map, at the first tick with a game world
// (c338-c337 has no `objective go`, so the landing test alone would never print them).
void c336_step(void* engine) {
    void* const world = !g_c336_logged && engine != nullptr ? g_get_game_world(engine) : nullptr;
    if (world != nullptr) {
        c336_zone_rows(world);
    }
}

// Both sides, at LoadMap enter: the next map logs its own zone rows.
void c336_map_end() {
    g_c336_logged = false;
    g_c336_refuse_rows = 0;
}

} // namespace ubw::hooks
