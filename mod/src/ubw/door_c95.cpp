// Card 095: the level script's doors and collision toggles on the client. A placed ASTDoor does not replicate, and the level script (its
// level-trigger reactions, LR_Door_Open and friends) runs on the host only, so the host's OpenDoor reaches the host's
// copy only: the client's copy stays shut and its collision stops the client (the FED_C1M1 capture-the-flag gate).
// The host relays each OpenDoor, CloseDoor and SetDoorActive on a placed door by name; the client calls the same
// function on its own copy. The test command `door list|open|close` (UBWCOOP_CMD) lists the doors and the
// blocker-like actors near the pawn, or calls OpenDoor/CloseDoor on a named door as the level script does.
// The same holds for the script's collision writes (SetActorEnableCollision, SetCollisionEnabled) on placed actors:
// the host relays each real change on a placed, non-replicated actor by actor name, location (names repeat across
// sublevels) and component name; the client applies it to its nearest same-named copy.

#include "ubw/hooks_internal.h"

#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ubw::hooks {

StaticClassFn g_c95_door_class = nullptr; // ASTDoor
DoorFn g_orig_c95_open = nullptr;
DoorFn g_orig_c95_close = nullptr;
DoorActiveFn g_orig_c95_active = nullptr;
SignificanceChangedFn g_orig_c95_process_significance = nullptr;

int32_t g_c95_significance_depth = 0; // game thread: inside ISTSignificanceObjectInterface::ProcessSignificanceUpdate (card 483: change_c137.cpp reads it)
namespace {

constexpr int32_t kC95Doors = 64;      // journal rows per map
constexpr uint32_t kC95Lines = 200;    // event lines per map
constexpr char kC95ScriptNodePrefix[] = "NODE_Add"; // card 462: construction-script node component names
// b184: EComponentCreationMethod::UserConstructionScript (Native 0, SimpleConstructionScript 1, UserConstructionScript 2,
// Instance 3); UActorComponent::CreationMethod is found by reflection once.
constexpr uint8_t kC95UserConstructionScript = 2;
// b184: a runtime-made object with no given name is numbered down from INT_MAX (NiagaraComponent_2147476983); the
// FName's stored number is the shown suffix plus one. Placed and template names stay far below this.
constexpr int32_t kC95TransientNumber = 2000000000;
constexpr uint32_t kC95ListLines = 80; // lines per `door list`
constexpr double kC95NearUu = 3000.0;  // `door list`: blocker-like actors this close to the pawn

// The relayed calls. The wire carries the letter.
enum class DoorOp : char { Open = 'O', Close = 'C', Active = 'A', Inactive = 'I' };

struct C95Row {
    char name[kChanNameMax + 1];
    char open;   // 'O', 'C' or 0 (no call yet)
    char active; // 'A', 'I' or 0
};

struct C95State {
    C95Row row[kC95Doors];
    int32_t n;
    uint32_t lines, dropped, sent, applied, missed;
};

C95State g_c95{}; // game thread

// Collision journal rows per map (card 147: constructor writes are not rows). Card 219: 2048 (c147-r3 from-save
// FED_C2M1 refused 1087 writes at 256; rows are keyed, so play time adds none). About 300 bytes a row.
// Card 465: 4096 (c460b169a: 1459 rows after a FED_C3M1 checkpoint load, 71 % of 2048).
constexpr int32_t kC95Collisions = 4096;
// Card 219: the join replay sends this many collision rows per host tick (a 2048-row burst is not proven safe
// against the engine's reliable buffer; 256 in one burst arrived whole).
constexpr int32_t kC95ReplayPerTick = 128;
constexpr int32_t kC95ReplayCursors = kC17Slots; // joining PCs paced at once
constexpr uint32_t kC462SkipLines = 48; // card 462: client skipped CollisionOp rows per map, own cap
constexpr int32_t kC462DupLines = 8;     // card 462: host shared name + component rows named per replay
constexpr int32_t kC95TallyTop = 6;     // card 147: row classes and callers named in the replay and map totals lines
// UObjectBase::ObjectFlags RF_NeedInitialization: set until FObjectInitializer::PostConstructInit clears it (exe+0x1574909,
// after the constructor returns), so a write while it is set is the constructor's (card 147).
constexpr int32_t kRfNeedInitialization = 0x200;
constexpr char kC95ActorFlag[] = "-";
constexpr unsigned kC95MaxCollisionValue = 5; // ECollisionEnabled: NoCollision .. QueryAndProbe    // the component field of an actor-level write (bActorEnableCollision)

constexpr double kC95RowUu = 500.0; // card 147: a same-named actor this near a row's location is that row's actor
// Card 237: a component row with a collision profile sends its component field as "@<profile>:<component>" (both
// characters are invalid in object names, so no component name starts with '@' or holds ':').
constexpr char kC237Profile = '@';
constexpr char kC237ProfileEnd = ':';
constexpr uint32_t kC237Lines = 24; // per map: card237 current-value lines
uint32_t g_c237_lines = 0;         // game thread; cleared by c95_map_end

// One placed actor's (or its component's) last relayed collision value. Card 147: the key is the actor's name,
// component and location (names repeat across sublevels, so two same-named actors far apart keep two rows); no actor
// pointer is stored, so a load that respawns the actor reuses its row.
struct C95Collision {
    char name[kC95ActorNameMax + 1]; // card 283
    char component[kChanNameMax + 1];
    Vec3 at;
    uint8_t value;
    uint32_t caller; // card 147: exe RVA of the write that added the row
    uint64_t profile; // card 237: host FName of the component's last relayed collision profile, 0 when none was
};

struct C95Collisions {
    C95Collision row[kC95Collisions];
    int32_t n;
    int32_t peak; // card 147: most rows this map, against kC95Collisions
    uint32_t dropped, applied, missed;
    uint32_t local;        // host: changes made by the significance update, not relayed
    uint32_t construction; // host: writes on an actor still in its constructor, not journalled or relayed (card 147)
    uint32_t script_node;  // host: writes on construction-script node components, kept local (card 462)
    uint32_t user_script;  // host: writes on UserConstructionScript components, not relayed (b184)
    uint32_t transient;    // host: writes on components with a transient-range name number, not relayed (b184)
    uint32_t runtime;      // host: writes on components not loaded with the map (made at run time), not relayed (b184)
    uint32_t skip_lines;   // client: skipped-frame rows written this map (card 462)
    bool warned;           // card 465: the 75 % WARN was logged this map
};

C95Collisions g_c95c{}; // game thread

// Card 237: a row with a profile carries it in the component field ("@<profile>:<component>"); the client sets the
// profile first, then CollisionEnabled, so one row holds the last write of either kind.
int c95_collision_text(const C95Collision& r, char* out, size_t cap) {
    if (r.profile != 0) {
        return snprintf(out, cap, "%s %.0f %.0f %.0f %u %c%s%c%s", r.name, r.at.x, r.at.y, r.at.z, r.value, kC237Profile,
                        fname_text(&r.profile).text, kC237ProfileEnd, r.component);
    }
    return snprintf(out, cap, "%s %.0f %.0f %.0f %u %s", r.name, r.at.x, r.at.y, r.at.z, r.value, r.component);
}

// Length of a row name without its trailing "_<digits>" instance suffix (the class part of a placed actor's name).
size_t c95_prefix_len(const char* name) {
    size_t n = strlen(name);
    size_t k = n;
    while (k > 0 && name[k - 1] >= '0' && name[k - 1] <= '9') {
        --k;
    }
    return k < n && k > 0 && name[k - 1] == '_' ? k - 1 : n;
}

// Rows of the name prefix of row i, or 0 when an earlier row has that prefix (so each prefix is counted once).
int32_t c95_prefix_rows(int32_t i) {
    const char* const a = g_c95c.row[i].name;
    const size_t len = c95_prefix_len(a);
    int32_t count = 0;
    for (int32_t j = 0; j < g_c95c.n; ++j) {
        const char* const b = g_c95c.row[j].name;
        if (c95_prefix_len(b) == len && strncmp(a, b, len) == 0) {
            if (j < i) {
                return 0;
            }
            ++count;
        }
    }
    return count;
}

// Rows whose first write came from the caller of row i, or 0 when an earlier row has that caller.
int32_t c95_caller_rows(int32_t i) {
    const uint32_t a = g_c95c.row[i].caller;
    int32_t count = 0;
    for (int32_t j = 0; j < g_c95c.n; ++j) {
        if (g_c95c.row[j].caller == a) {
            if (j < i) {
                return 0;
            }
            ++count;
        }
    }
    return count;
}

// The kC95TallyTop rows i with the largest count(i), as row index and count, count descending (count 0 ends the list).
void c95_top_rows(int32_t (*count)(int32_t), int32_t* top, int32_t* top_n) {
    for (int32_t i = 0; i < g_c95c.n; ++i) {
        int32_t c = count(i);
        int32_t at = i;
        for (int32_t t = 0; t < kC95TallyTop && c > 0; ++t) { // insert, keeping top_n descending
            if (c > top_n[t]) {
                const int32_t sc = top_n[t], si = top[t];
                top_n[t] = c;
                top[t] = at;
                c = sc;
                at = si;
            }
        }
    }
}

// Card 147: the kC95TallyTop name prefixes with the most collision rows, as "prefix:count ...".
void c95_row_tally(char* out, size_t cap) {
    int32_t top[kC95TallyTop] = {};
    int32_t top_n[kC95TallyTop] = {};
    c95_top_rows(&c95_prefix_rows, top, top_n);
    size_t used = 0;
    out[0] = '\0';
    for (int32_t t = 0; t < kC95TallyTop && top_n[t] > 0 && used < cap; ++t) {
        const char* const name = g_c95c.row[top[t]].name;
        const int w = snprintf(out + used, cap - used, " %.*s:%d", static_cast<int>(c95_prefix_len(name)), name,
                               top_n[t]);
        used += w > 0 ? static_cast<size_t>(w) : cap;
    }
}

// Card 147: the kC95TallyTop first-write callers with the most collision rows, as "exe+0xRVA:count ...".
void c95_caller_tally(char* out, size_t cap) {
    int32_t top[kC95TallyTop] = {};
    int32_t top_n[kC95TallyTop] = {};
    c95_top_rows(&c95_caller_rows, top, top_n);
    size_t used = 0;
    out[0] = '\0';
    for (int32_t t = 0; t < kC95TallyTop && top_n[t] > 0 && used < cap; ++t) {
        const int w = snprintf(out + used, cap - used, " exe+0x%X:%d", g_c95c.row[top[t]].caller, top_n[t]);
        used += w > 0 ? static_cast<size_t>(w) : cap;
    }
}

bool c95_line() {
    return note_event_cap(g_c95.lines, kC95Lines, "card095") && pl_allow(PlClass::Door); // card 155: rate limited
}

const char* c95_op_text(char op) {
    switch (static_cast<DoorOp>(op)) {
    case DoorOp::Open:
        return "OpenDoor";
    case DoorOp::Close:
        return "CloseDoor";
    case DoorOp::Active:
        return "SetDoorActive(1)";
    case DoorOp::Inactive:
        return "SetDoorActive(0)";
    }
    return "?";
}

bool c95_replicated(const void* actor) {
    return (static_cast<const uint8_t*>(actor)[kActorRepFlags] & 0x08u) != 0;
}

// The root primitive's CollisionEnabled, or -1 without one.
int c95_root_collision(const void* actor) {
    const auto* const root = static_cast<const uint8_t*>(read_ptr(actor, kActorRootComponent));
    return root != nullptr ? root[kBodyCollisionEnabled] : -1;
}

void c95_log_actor(const char* what, const void* actor) {
    const Vec3 at = actor_location(actor);
    const auto* const a = static_cast<const uint8_t*>(actor);
    log::line("card095: %s t=%llu %s %s (%s) at (%.0f, %.0f, %.0f): placed %d, bReplicates %d, bActorEnableCollision "
              "%u, root %s CollisionEnabled %d",
              side(), static_cast<unsigned long long>(GetTickCount64()), what,
              prop_name(a + kObjectName, 8).text, class_name(actor).text, at.x, at.y, at.z, c50_placed(actor) ? 1 : 0,
              c95_replicated(actor) ? 1 : 0, a[kActorCollisionFlags] & kActorEnableCollisionBit,
              class_name(read_ptr(actor, kActorRootComponent)).text, c95_root_collision(actor));
}

C95Row* c95_row(const char* name, bool add) {
    for (int32_t i = 0; i < g_c95.n; ++i) {
        if (strcmp(g_c95.row[i].name, name) == 0) {
            return &g_c95.row[i];
        }
    }
    if (!add || g_c95.n >= kC95Doors) {
        g_c95.dropped += add ? 1u : 0u;
        return nullptr;
    }
    C95Row& r = g_c95.row[g_c95.n++];
    memcpy(r.name, name, strlen(name) + 1);
    r.open = 0;
    r.active = 0;
    return &r;
}

bool c95_send_text(void* pc, ChanOp op, const char* text) {
    static wchar_t wire[kChanTextFrame];
    const WireString w = chan_text_frame(op, text, wire);
    const bool sent = w.num > 0 && chan_send_client(pc, w);
    g_c95.sent += sent ? 1u : 0u;
    return sent;
}

bool c95_send(void* pc, const char* name, char op) {
    char text[kChanNameMax + 4];
    snprintf(text, sizeof(text), "%s %c", name, op);
    return c95_send_text(pc, ChanOp::DoorOp, text);
}

} // namespace

// Host: the frame to every remote PC that has sent its first Pong this map (the others get it in their replay).
bool c95_send_one(void* pc, ChanOp op, const char* text) {
    return c95_send_text(pc, op, text);
}

C95Fanout c95_send_all(const void* world, ChanOp op, const char* text, bool priority) {
    static wchar_t wire[kChanTextFrame];
    C95Fanout f{};
    for (int s = 0; world != nullptr && s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(world, s);
        const C50Copy* const c = pc != nullptr && sprint_side(pc) == kSideServerCopy ? c50_copy(object_index(pc), false)
                                                                                     : nullptr;
        if (c != nullptr && c->ponged && priority) {
            ++f.to;
            const WireString w = chan_text_frame(op, text, wire);
            uint32_t ahead = 0;
            const bool sent = w.num > 0 && ue::chan::chan_send_client_now(pc, w, &ahead);
            f.sent += sent ? 1 : 0;
            f.ahead = ahead > f.ahead ? ahead : f.ahead;
            g_c95.sent += sent ? 1u : 0u;
        } else if (c != nullptr && c->ponged) {
            ++f.to;
            f.sent += c95_send_text(pc, op, text) ? 1 : 0;
        }
    }
    return f;
}

// Host: the remote PCs that have sent their first Pong this map (c95_send_all's set).
int32_t c95_ponged_count(const void* world) {
    int32_t n = 0;
    for (int s = 0; world != nullptr && s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(world, s);
        const C50Copy* const c = pc != nullptr && sprint_side(pc) == kSideServerCopy ? c50_copy(object_index(pc), false)
                                                                                     : nullptr;
        n += c != nullptr && c->ponged ? 1 : 0;
    }
    return n;
}

namespace {

// Host, after the original on a listen server: journal the placed door's new state and send it to every remote PC
// that has sent its first Pong (the others get it in their replay).
void c95_host_call(void* door, char op) {
    if (!c50_placed(door) || net_mode(door) != kNetModeListenServer || c95_replicated(door)) {
        return;
    }
    char name[kChanNameMax + 1];
    if (c50_name(door, name, sizeof(name)) <= 0) {
        log::line("WARN card095: host %s on %s: name not sendable", c95_op_text(op), object_name(door).text);
        return;
    }
    C95Row* const r = c95_row(name, true);
    if (r != nullptr) {
        (op == 'O' || op == 'C' ? r->open : r->active) = op;
    }
    char text[kChanNameMax + 4];
    snprintf(text, sizeof(text), "%s %c", name, op);
    const C95Fanout f = c95_send_all(g_actor_get_world(door), ChanOp::DoorOp, text);
    const bool teardown = f.sent != f.to && c332_on(); // card 346: refused and counted by the teardown row
    log::line("%scard095: host t=%llu %s %s: sent to %d of %d clients%s%s", f.sent == f.to || teardown ? "" : "WARN ",
              static_cast<unsigned long long>(GetTickCount64()), c95_op_text(op), name, f.sent, f.to,
              teardown ? " (card 332 teardown)" : "", r != nullptr ? "" : " (journal full: not replayed)");
}

// Both sides: one line per call on a door (the caller tells a script call from the relay or the door itself).
void c95_note(const char* what, const void* door, unsigned long long caller) {
    if (c95_line()) {
        char text[96];
        snprintf(text, sizeof(text), "%s, caller exe+0x%llX, on", what, caller);
        c95_log_actor(text, door);
    }
}

void c95_apply(void* door, char op) {
    if (op == 'O') {
        g_orig_c95_open(door);
    } else if (op == 'C') {
        g_orig_c95_close(door);
    } else {
        g_orig_c95_active(door, op == 'A');
    }
}

bool c95_is_blocker_like(const void* actor) {
    const ObjectName name = class_name(actor);
    const char* const cls = name.text;
    return text_contains(cls, "Block") || text_contains(cls, "Door") || text_contains(cls, "Gate") ||
           text_contains(cls, "Barrier") || text_contains(cls, "Wall");
}

} // namespace

// Card 161 (review after c161c): a BP_Door_Base door's every DoorState change, as the digit with its PawnBuffer
// CollisionEnabled ("<name> <digit> <buffer>"). The journal keeps the digit as the row's open state for the replay.
void c95_host_door_state(void* door, uint8_t state, int buffer) {
    if (!c50_placed(door) || net_mode(door) != kNetModeListenServer || c95_replicated(door) || state > 3) {
        return;
    }
    char name[kChanNameMax + 1];
    if (c50_name(door, name, sizeof(name)) <= 0) {
        log::line("WARN card161: host DoorState %u on %s: name not sendable", state, object_name(door).text);
        return;
    }
    const char op = static_cast<char>('0' + state);
    C95Row* const r = c95_row(name, true);
    if (r != nullptr) {
        r->open = op;
    }
    char text[kChanNameMax + 8];
    snprintf(text, sizeof(text), "%s %c %d", name, op, buffer);
    const C95Fanout f = c95_send_all(g_actor_get_world(door), ChanOp::DoorOp, text);
    log::line("%scard161: host t=%llu DoorState %u %s (PawnBuffer %d): sent to %d of %d clients%s",
              f.sent == f.to ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()), state, name, buffer,
              f.sent, f.to, r != nullptr ? "" : " (journal full: not replayed)");
}

// Card 137 step 3: host, after a save load (LoadFromSlot returned). ASTDoor::ActorFullyLoaded restores a door from its
// saved fields, maybe with no OpenDoor or CloseDoor call, so the door journal is rebuilt from DoorState (EMoveType:
// Closed 0, Open 1, Opening 2, Closing 3): a row whose open state differs takes the door's, and an open door with no
// row gets one (closed is the map's own state, so a closed door needs none). The active state has no field read here
// and is kept. Journal only: nothing is sent.
void c95_load_actor(const void* actor, C137LoadCounts& n) {
    char name[kChanNameMax + 1];
    if (g_c95_door_class == nullptr || !is_a(actor, g_c95_door_class()) || !c50_placed(actor) ||
        net_mode(actor) != kNetModeListenServer || c95_replicated(actor) || c50_name(actor, name, sizeof(name)) <= 0) {
        return;
    }
    const uint8_t* const f = prop_find_prop(read_ptr(actor, kObjectClass), "DoorState");
    if (f == nullptr) {
        if (c137_load_line(n)) {
            log::line("WARN card137: host load reconcile c95 %s: no DoorState property, row kept", name);
        }
        return;
    }
    const uint8_t state = static_cast<const uint8_t*>(actor)[read_i32(f, kPropertyOffset)];
    const char open = state == 1 || state == 2 ? 'O' : 'C';
    C95Row* r = c95_row(name, false);
    const bool add = r == nullptr && open == 'O';
    r = add ? c95_row(name, true) : r;
    if (r == nullptr || r->open == open || (r->open == 0 && open == 'C')) {
        return;
    }
    const char was = r->open;
    r->open = open;
    ++(add ? n.door_added : n.door_fixed);
    if (c137_load_line(n)) {
        log::line("card137: host load reconcile c95 %s: DoorState %u, row %c -> %c%s", name, state, was != 0 ? was : '-',
                  open, add ? " (added)" : "");
    }
}

// Both sides, from the collision write detours: a write on a placed blocker-like actor (or its component), with the
// Blueprint function running it when known. One line each, capped with the door lines.
void c95_note_collision(const char* call, const void* owner, const void* component, unsigned before, unsigned after,
                        unsigned long long caller, const void* node) {
    if (owner == nullptr || !c50_placed(owner) || !c95_is_blocker_like(owner) || !c95_line()) {
        return;
    }
    const Vec3 at = actor_location(owner);
    log::line("card095: %s t=%llu %s on %s (%s) component %s (%s) at (%.0f, %.0f, %.0f): %u -> %u, caller exe+0x%llX, "
              "blueprint %s.%s",
              side(), static_cast<unsigned long long>(GetTickCount64()), call,
              prop_name(static_cast<const uint8_t*>(owner) + kObjectName, 8).text, class_name(owner).text,
              object_name(component).text, class_name(component).text, at.x, at.y, at.z, before, after, caller,
              object_name(node != nullptr ? read_ptr(node, kObjectOuter) : nullptr).text, object_name(node).text);
}

void detour_c95_open(void* door) {
    c95_note("OpenDoor", door, exe_rva(_ReturnAddress()));
    g_orig_c95_open(door);
    c95_host_call(door, 'O');
}

void detour_c95_close(void* door) {
    c95_note("CloseDoor", door, exe_rva(_ReturnAddress()));
    g_orig_c95_close(door);
    c95_host_call(door, 'C');
}

void detour_c95_active(void* door, bool active) {
    c95_note(active ? "SetDoorActive(1)" : "SetDoorActive(0)", door, exe_rva(_ReturnAddress()));
    g_orig_c95_active(door, active);
    c95_host_call(door, active ? 'A' : 'I');
}

// Client: the host's DoorOp frame. Our placed ASTDoor of that name gets the same call; a miss is logged.
void c95_receive(void* pc, const WireString* s) {
    char text[kChanTextMax + 1];
    char name[kChanNameMax + 1];
    char op = 0;
    if (chan_text(s, text, sizeof(text)) <= 0 ||
        sscanf_s(text, "%64s %c", name, static_cast<unsigned>(sizeof(name)), &op, 1u) != 2 ||
        (op != 'O' && op != 'C' && op != 'A' && op != 'I' && (op < '0' || op > '3'))) {
        log::line("WARN card095: client: bad DoorOp frame");
        c132_ledger_result(C132Result::Refused, "bad frame");
        return;
    }
    const StaticClassFn* const cls[] = {&g_c95_door_class};
    void* const door = c50_find_placed(g_actor_get_world(pc), cls, 1, name);
    if (door == nullptr && c161_receive(name, op, text)) { // card 161: a BP_Door_Base door of that name took it
        ++g_c95.applied;
        c132_ledger_result(C132Result::Applied, nullptr);
        return;
    }
    if (door == nullptr || (op >= '0' && op <= '3')) { // a DoorState digit is a BP_Door_Base door's only
        ++g_c95.missed;
        c132_ledger_result(C132Result::Skipped, "no placed copy matches (see the WARN line)");
        log::line("WARN card095: client t=%llu DoorOp %s %s: no placed ASTDoor has that name, skipped",
                  static_cast<unsigned long long>(GetTickCount64()), c95_op_text(op), name);
        return;
    }
    c95_log_actor(op == 'O' ? "relayed OpenDoor, before, on" : "relayed call, before, on", door);
    c95_apply(door, op);
    ++g_c95.applied;
    c132_ledger_result(C132Result::Applied, nullptr);
    if (pl_allow(PlClass::Door)) { // card 155
        log::line("card095: client t=%llu DoorOp %s %s (%s): applied", static_cast<unsigned long long>(GetTickCount64()),
                  c95_op_text(op), name, class_name(door).text);
    }
}

namespace {

// --- Card 421: the join replay as (actor name hash, value, component) pairs ---
// The client's own load makes most of the journalled writes, so the replay sends pairs. The client compares each
// with its own copy and asks (a CollisionOp frame "? hash ...", client to host) for the full rows of the names
// that differ or that it cannot place; the host answers with every journal row of that name hash. A row whose
// name hash and component another row shares, or whose component name has a space, goes whole from the start.
constexpr char kC421Pairs = '~';          // host to client: "~ hash value component hash value component ..."
constexpr char kC421Want = '?';           // client to host: "? hash hash ..."
constexpr int32_t kC421Own = 32768;       // client: placed, unreplicated actors by name hash (about 25100 a map)
constexpr ULONGLONG kC421OwnMs = 1000;    // client: an older table is walked again before a frame's pairs
constexpr uint32_t kC421WhyLines = 12;    // per map: the first asked-for pairs, each with its cause
constexpr int32_t kC421FramePairs = 256;  // parse bound: more pairs or hashes than one frame's text can hold

struct C421Own {
    uint32_t hash; // c421_hash of the actor's c50_name
    C31Weak actor;
};
struct C421State {
    C421Own own[kC421Own]; // client, sorted by hash
    int32_t own_n;
    ULONGLONG own_ms;              // when the table was walked (0: never this map)
    uint32_t hash[kC95Collisions]; // host: the name hash of each journal row (c421_hash_rows)
    uint32_t pair_frames, pairs, equal, wanted, want_frames; // client, this map
    uint32_t want_recv, want_rows, unknown;                  // host, this map
    uint32_t walks, own_full, bad;
    uint32_t no_actor, differ, many; // client: asked for with no copy, one copy that refused the write, 2+ copies
    double walk_us;
};
C421State g_c421{}; // game thread; cleared by c95_map_end

void c421_own_walk(const void* world);
int32_t c421_own_count(uint32_t hash, void*& last);

uint32_t c421_hash(const char* name) { // FNV-1a
    uint32_t h = 2166136261u;
    for (int32_t i = 0; i < kC95ActorNameMax && name[i] != '\0'; ++i) {
        h = (h ^ static_cast<uint8_t>(name[i])) * 16777619u;
    }
    return h;
}

void c421_hash_rows() {
    for (int32_t i = 0; i < g_c95c.n && i < kC95Collisions; ++i) {
        g_c421.hash[i] = c421_hash(g_c95c.row[i].name);
    }
}

// Host (after c421_hash_rows and a fresh c421_own_walk): the row cannot go as a pair. A pair names its actor by
// name hash alone, so the name must be one placed actor here (names can repeat across sublevels).
bool c421_whole(int32_t i) {
    const C95Collision& r = g_c95c.row[i];
    void* last = nullptr;
    // Card 237: a pair has no room for the profile.
    if (r.profile != 0 || strchr(r.component, ' ') != nullptr || c421_own_count(g_c421.hash[i], last) != 1) {
        return true;
    }
    for (int32_t j = 0; j < g_c95c.n; ++j) {
        if (j != i && g_c421.hash[j] == g_c421.hash[i] && strcmp(g_c95c.row[j].component, r.component) == 0) {
            return true;
        }
    }
    return false;
}

struct C421Frame {
    char text[kChanTextMax + 1];
    int32_t len, rows;
};

} // namespace

// Card 219: one joining PC's paced collision replay. Present in the table = in progress; `end` is the row count
// at the replay's start (rows added later go out live).
struct C95Cursor {
    uint64_t pc_weak; // FWeakObjectPtr to the PC: a PC that left fails it
    int32_t next, end;
    int32_t want, sent; // calls so far, doors included
    int32_t pairs, frames, full; // card 421: collision rows sent as pairs, the pair frames, rows sent whole
};
struct C95Cursors {
    C95Cursor c[kC95ReplayCursors];
    int32_t n;
};
C95Cursors g_c95r{}; // game thread; cleared by c95_map_end

void c421_flush(void* pc, C421Frame& f, C95Cursor& cur) {
    if (f.rows == 0) {
        return;
    }
    cur.sent += c95_send_text(pc, ChanOp::CollisionOp, f.text) ? f.rows : 0;
    cur.pairs += f.rows;
    ++cur.frames;
    f.len = 0;
    f.rows = 0;
}

// Row i joins the pair frame; a full frame goes out first.
void c421_pair(void* pc, C421Frame& f, C95Cursor& cur, int32_t i) {
    const C95Collision& r = g_c95c.row[i];
    char pair[kChanNameMax + 32];
    const int n = snprintf(pair, sizeof(pair), " %u %u %s", g_c421.hash[i], r.value, r.component);
    if (f.len + n > kChanTextMax) {
        c421_flush(pc, f, cur);
    }
    if (f.rows == 0) {
        f.text[0] = kC421Pairs;
        f.len = 1;
    }
    memcpy(f.text + f.len, pair, static_cast<size_t>(n) + 1);
    f.len += n;
    ++f.rows;
}

// Sends collision rows [cur.next, min(cur.next + kC95ReplayPerTick, cur.end)) to pc: pairs (card 421), or the whole
// row where a pair cannot name it.
void c95_replay_rows(void* pc, C95Cursor& cur) {
    const int32_t stop = cur.end < cur.next + kC95ReplayPerTick ? cur.end : cur.next + kC95ReplayPerTick;
    C421Frame f{};
    c421_hash_rows();
    const void* const world = g_actor_get_world(pc);
    if (world != nullptr && (g_c421.own_ms == 0 || GetTickCount64() - g_c421.own_ms >= kC421OwnMs)) {
        c421_own_walk(world);
    }
    for (; cur.next < stop && cur.next < g_c95c.n; ++cur.next) {
        ++cur.want;
        if (!c421_whole(cur.next)) {
            c421_pair(pc, f, cur, cur.next);
            continue;
        }
        char text[kC95ActorNameMax + kChanNameMax + 64]; // card 283
        c95_collision_text(g_c95c.row[cur.next], text, sizeof(text));
        cur.sent += c95_send_text(pc, ChanOp::CollisionOp, text) ? 1 : 0;
        ++cur.full;
    }
    c421_flush(pc, f, cur);
}

// Card 462 (diagnostic): the journal's row count and the rows that share name and component with an earlier row
// (another location), with the first kC462DupLines of them, at each collision replay.
void c462_replay_row(const void* pc) {
    int32_t dups = 0;
    int32_t shown = 0;
    for (int32_t j = 1; j < g_c95c.n; ++j) {
        const C95Collision& b = g_c95c.row[j];
        for (int32_t i = 0; i < j; ++i) {
            const C95Collision& a = g_c95c.row[i];
            if (strcmp(a.name, b.name) != 0 || strcmp(a.component, b.component) != 0) {
                continue;
            }
            ++dups;
            if (shown < kC462DupLines) {
                ++shown;
                log::line("card462: host collision row %d %s %s at (%.0f, %.0f, %.0f) value %u shares name + component "
                          "with row %d at (%.0f, %.0f, %.0f) value %u", j, b.name, b.component, b.at.x, b.at.y, b.at.z,
                          b.value, i, a.at.x, a.at.y, a.at.z, a.value);
            }
            break;
        }
    }
    log::line("card462: host t=%llu collision replay to %s: %d journal rows, %d share name + component with an earlier "
              "row", static_cast<unsigned long long>(GetTickCount64()), object_name(pc).text, g_c95c.n, dups);
}

// The replay line, written when a PC's replay has sent its last row.
void c95_replay_done(void* pc, const C95Cursor& cur) {
    c462_replay_row(pc);
    const int32_t want = cur.want;
    const int32_t sent = cur.sent;
    if (want != 0) {
        log::line("card421: host collision replay to %s: %d rows as pairs in %d frames, %d whole rows",
                  object_name(pc).text, cur.pairs, cur.frames, cur.full);
        char tally[512];
        char callers[256];
        c95_row_tally(tally, sizeof(tally));
        c95_caller_tally(callers, sizeof(callers));
        log::line("%scard095: host t=%llu door and collision journal replay to %s (first Pong of the map): %d of %d calls, "
                  "%d collision rows (peak %d of %d), %u collision rows dropped (journal full), %u local, %u "
                  "construction, %u script-node component writes kept local; rows by name:%s; rows by first caller:%s",
                  sent == want && g_c95c.dropped == 0 ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()),
                  object_name(pc).text, sent, want, g_c95c.n, g_c95c.peak, kC95Collisions, g_c95c.dropped,
                  g_c95c.local, g_c95c.construction, g_c95c.script_node, tally, callers);
    }
}

// Host: a remote PC's first Pong of this map gets every journalled door's last active and open state, and the
// collision rows: the first kC95ReplayPerTick now, the rest from c95_replay_step (card 219).
void c95_replay(void* pc) {
    C95Cursor cur{0, 0, g_c95c.n, 0, 0, 0, 0, 0};
    for (int32_t i = 0; i < g_c95.n; ++i) {
        const C95Row& r = g_c95.row[i];
        const char ops[2] = {r.active, r.open};
        for (char op : ops) {
            cur.want += op != 0 ? 1 : 0;
            cur.sent += op != 0 && c95_send(pc, r.name, op) ? 1 : 0;
        }
    }
    c95_replay_rows(pc, cur);
    if (cur.next >= cur.end) {
        c95_replay_done(pc, cur);
        return;
    }
    if (g_c95r.n >= kC95ReplayCursors || g_weak_assign == nullptr) {
        log::line("WARN card219: host collision replay to %s stopped at row %d of %d: cursor table full (%d of %d)",
                  object_name(pc).text, cur.next, cur.end, g_c95r.n, kC95ReplayCursors);
        return;
    }
    g_weak_assign(&cur.pc_weak, &pc);
    g_c95r.c[g_c95r.n++] = cur;
}

// Host, every tick: the next kC95ReplayPerTick collision rows of each paced replay. A PC that left drops its cursor.
void c95_replay_step() {
    for (int32_t i = g_c95r.n - 1; i >= 0 && i < kC95ReplayCursors; --i) {
        C95Cursor& cur = g_c95r.c[i];
        void* const pc = g_weak_get != nullptr ? g_weak_get(&cur.pc_weak) : nullptr;
        if (pc == nullptr) {
            log::line("WARN card219: host collision replay stopped at row %d of %d: the PC left", cur.next, cur.end);
        } else {
            c95_replay_rows(pc, cur);
            if (cur.next < cur.end && cur.next < g_c95c.n) {
                continue;
            }
            c95_replay_done(pc, cur);
        }
        g_c95r.c[i] = g_c95r.c[--g_c95r.n];
    }
}

// Card 200: at LoadMap enter (ubw_load_map_enter), before the new world's BeginPlay rows: the old map's totals,
// then the door and collision journals and the replay cursors reset.
void c95_map_end() {
    if (g_c95.n != 0 || g_c95.missed != 0) {
        log::line("card095: %s map totals: %d doors, %u dropped (journal full), %u frames sent, %u applied, %u missed",
                  side(), g_c95.n, g_c95.dropped, g_c95.sent, g_c95.applied, g_c95.missed);
    }
    if (g_c95c.n != 0 || g_c95c.missed != 0 || g_c95c.local != 0 || g_c95c.construction != 0 ||
        g_c95c.script_node != 0 || g_c95c.user_script != 0 || g_c95c.transient != 0 || g_c95c.runtime != 0) {
        char tally[512];
        char callers[256];
        c95_row_tally(tally, sizeof(tally));
        c95_caller_tally(callers, sizeof(callers));
        log::line("card095: %s map totals: %d collision rows (peak %d of %d), %u dropped (journal full), %u applied, %u "
                  "missed, %u significance changes kept local, %u construction writes not journalled, %u script-node "
                  "component writes kept local, %u user-construction-script, %u transient-name and %u run-time "
                  "component writes not relayed; rows by name:%s; rows by first caller:%s",
                  side(), g_c95c.n, g_c95c.peak, kC95Collisions, g_c95c.dropped, g_c95c.applied, g_c95c.missed,
                  g_c95c.local, g_c95c.construction, g_c95c.script_node, g_c95c.user_script, g_c95c.transient,
                  g_c95c.runtime, tally, callers);
    }
    const C421State& p = g_c421;
    if (p.pair_frames != 0 || p.want_recv != 0 || p.bad != 0) {
        log::line("%scard421: %s map totals: client %u pair frames, %u pairs, %u applied here, %u asked for in %u frames; "
                  "host %u want frames, %u rows sent, %u unknown hashes; %u bad frames; own table %d (%u over), %u "
                  "walks avg %.0f us; asked for: %u with no actor, %u one copy refused, %u repeated names",
                  p.bad == 0 && p.unknown == 0 && p.own_full == 0 ? "" : "WARN ", side(), p.pair_frames, p.pairs,
                  p.equal, p.wanted, p.want_frames, p.want_recv, p.want_rows, p.unknown, p.bad, p.own_n, p.own_full,
                  p.walks, p.walks != 0 ? p.walk_us / p.walks : 0.0, p.no_actor, p.differ, p.many);
    }
    g_c95 = C95State{};
    memset(&g_c95c, 0, sizeof(g_c95c)); // card 465: about 1.2 MB, not through a stack temporary
    g_c421 = C421State{};
    g_c237_lines = 0;
    g_c95r = C95Cursors{};
}

int32_t c95_live() {
    return g_c95.n + g_c95c.n;
}

namespace {

// Host: the journal row for this actor name, component and location (card 147), added when new (null when the
// journal is full).
C95Collision* c95_collision_row(const char* name, const char* component, const Vec3& at, uint32_t caller) {
    for (int32_t i = 0; i < g_c95c.n; ++i) {
        const C95Collision& r = g_c95c.row[i];
        if (strcmp(r.name, name) == 0 && strcmp(r.component, component) == 0 && vec_dist(r.at, at) <= kC95RowUu) {
            return &g_c95c.row[i];
        }
    }
    if (g_c95c.n >= kC95Collisions) {
        ++g_c95c.dropped;
        return nullptr;
    }
    C95Collision& r = g_c95c.row[g_c95c.n++];
    g_c95c.peak = g_c95c.n > g_c95c.peak ? g_c95c.n : g_c95c.peak;
    if (!g_c95c.warned && g_c95c.n * 4 > kC95Collisions * 3) {
        g_c95c.warned = true; // card 465: once per map
        log::line("WARN card465: %s collision journal past 75 %% of its cap: %d of %d rows (this row %s %s, caller "
                  "exe+0x%X)", side(), g_c95c.n, kC95Collisions, name, component, caller);
    }
    r = C95Collision{};
    snprintf(r.name, sizeof(r.name), "%s", name);
    snprintf(r.component, sizeof(r.component), "%s", component);
    r.at = at;
    r.caller = caller;
    return &r;
}

} // namespace

namespace {

constexpr double kC95FallbackUu = 200.0; // card 133: a same-class actor this near `at` stands in for the name

// Card 133: the placed actor nearest `at`, within kC95FallbackUu, whose class name is part of `name` (a default
// name is <Class>_N, a child actor's <Component>_<Class>_CAT_N; the N and the CAT_N follow spawn order, so the same
// actor can have another name on the other side), or null. Logged.
void* c95_name_fallback(void* const* const* level_actors, const int32_t* level_nums, int32_t levels, const char* name,
                        const Vec3& at) {
    void* best = nullptr;
    double best_d = kC95FallbackUu;
    for (int32_t l = 0; l < levels; ++l) {
        for (int32_t a = 0; a < level_nums[l] && a < kMaxLevelActors; ++a) {
            void* const actor = level_actors[l][a];
            if (actor == nullptr || !c50_placed(actor)) {
                continue;
            }
            const double d = vec_dist(actor_location(actor), at);
            if (d <= best_d && strstr(name, class_name(actor).text) != nullptr) {
                best = actor;
                best_d = d;
            }
        }
    }
    char got[kC95ActorNameMax + 1]; // card 283
    if (best != nullptr && c50_name(best, got, sizeof(got)) > 0) {
        log::line("card133: name fallback %s -> %s %s %.0f uu", name, got, class_name(best).text, best_d);
    }
    return best;
}

} // namespace

namespace {
void* c95_child_actor(const void* world, const char* name, const Vec3& at);
} // namespace

// Card 133: the ChildActorComponent that spawned this actor, or null when the actor is not a child actor. A child
// actor's name and location differ per machine (the satchel site of a flak bug building is spawned by the building's
// "SatchelSiteCAC" and turns with it), so the wire names it by its parent and that component instead.
const void* c95_child_cac(const void* actor) {
    // Card 411: the digest round asks this for every actor, so no by-name property walk here. RootComponent and
    // AttachParent come from fixed and cached offsets; most actors are not attached and leave at once.
    const void* const root = actor != nullptr ? read_ptr(actor, kActorRootComponent) : nullptr;
    const void* const parent = c140_attach_parent(root);
    if (parent == nullptr || !text_contains(class_name(parent).text, "ChildActorComponent")) {
        return nullptr;
    }
    // UChildActorComponent::ChildActor is a native field: one offset for every subclass, found once.
    static int32_t child_actor = -1;
    if (child_actor < 0) {
        const uint8_t* const f = prop_find_prop(read_ptr(parent, kObjectClass), "ChildActor");
        if (f == nullptr) {
            return nullptr;
        }
        child_actor = read_i32(f, kPropertyOffset);
    }
    return child_actor > 0 && read_ptr(parent, static_cast<size_t>(child_actor)) == actor ? parent : nullptr;
}

// The actor the wire's location belongs to: a child actor's parent, else the actor itself.
const void* c95_anchor(const void* actor) {
    const void* const cac = c95_child_cac(actor);
    const void* const owner = cac != nullptr ? read_ptr(cac, kComponentOwner) : nullptr;
    return owner != nullptr ? owner : actor;
}

// Client to host, one target: "name x y z", or for a child actor "parent/component x y z" at the parent's location
// (c95_find_nearest resolves both). Returns the length written, 0 when the name does not fit.
int32_t c95_wire_target(const void* target, char* out, size_t cap) {
    const void* const cac = c95_child_cac(target);
    const void* const anchor = c95_anchor(target);
    char name[kChanNameMax + 1];
    if (c50_name(anchor, name, sizeof(name)) <= 0) {
        return 0;
    }
    // Card 230: the combined "parent/component" has its own bound; it fails only when a part is over kChanNameMax.
    char comp[kC95TargetMax + 1] = {}; // card 425: with its number; the client finds it by StaticFindObject
    const int32_t got = cac != nullptr ? c50_name(cac, comp, sizeof(comp)) : 0;
    const size_t comp_len = got >= 0 ? strlen(comp) : sizeof(comp); // unnamed: refused below as too long
    char full[kC95TargetMax + 1];
    const int n = cac != nullptr ? snprintf(full, sizeof(full), "%s/%s", name, comp)
                                 : snprintf(full, sizeof(full), "%s", name);
    if (n <= 0 || n > kC95TargetMax || comp_len > static_cast<size_t>(kChanNameMax)) {
        log::line("WARN card133: wire name of %s does not fit (component %zu > %d)", object_name(target).text, comp_len,
                  kChanNameMax);
        return 0;
    }
    const Vec3 at = actor_location(anchor);
    const int len = snprintf(out, cap, "%s %.1f %.1f %.1f", full, at.x, at.y, at.z);
    return len > 0 && static_cast<size_t>(len) < cap ? len : 0;
}

namespace {
void* c95_find_walk(const void* world, const char* name, const Vec3& at);

// Card 407: the nearest of the name index's actors (the walk's pick: on equal distance the lower level, then slot),
// or null when the index holds none or cannot answer.
void* c95_find_indexed(const void* world, const char* name, const Vec3& at) {
    const C50NameRef ref = c50_name_ref(name, kC95ActorNameMax);
    C402New hit[kC407Hits];
    const int32_t n = c407_named(world, ref, hit, kC407Hits);
    int32_t best = -1;
    double best_d = 0.0;
    for (int32_t i = 0; i < n; ++i) {
        const double d = vec_dist(actor_location(hit[i].actor), at);
        const bool before = best >= 0 && (hit[i].level < hit[best].level ||
                                          (hit[i].level == hit[best].level && hit[i].index < hit[best].index));
        if (best < 0 || d < best_d || (d == best_d && before)) {
            best = i;
            best_d = d;
        }
    }
    return best >= 0 ? hit[best].actor : nullptr;
}
} // namespace

// The placed actor with this name nearest to `at` (names repeat across sublevels), or null. Card 133: with no
// exact name, the nearest same-class actor within kC95FallbackUu (c95_name_fallback); "parent/component" names a
// child actor (c95_wire_target).
// Card 407: an exact name comes from the name index; its first answers are compared with the walk.
void* c95_find_nearest(const void* world, const char* name, const Vec3& at) {
    if (strchr(name, '/') != nullptr) {
        return c95_child_actor(world, name, at);
    }
    constexpr uint32_t kC407Checks = 32; // index answers compared with the walk, per process
    static uint32_t checks = 0;
    void* const indexed = c95_find_indexed(world, name, at);
    if (indexed == nullptr) {
        return c95_find_walk(world, name, at); // no exact name in the index: the walk, then its class fallback
    }
    if (checks < kC407Checks) {
        ++checks;
        void* const walked = c95_find_walk(world, name, at);
        if (walked != indexed) {
            log::line("WARN card407: name index gave %s %p for %s, the walk %s %p (walk kept)",
                      object_name(indexed).text, indexed, name, walked != nullptr ? object_name(walked).text : "none",
                      walked);
            return walked;
        }
    }
    return indexed;
}

namespace {
// The whole walk (the path before card 407), with card 133's fallback.
void* c95_find_walk(const void* world, const char* name, const Vec3& at) {
    const auto* const levels = static_cast<void* const*>(world != nullptr ? read_ptr(world, kWorldLevels) : nullptr);
    const int32_t num_levels = levels != nullptr ? read_i32(world, kWorldLevels + kArrayNum) : 0;
    C50NameRef ref = c50_name_ref(name, kC95ActorNameMax); // card 283's cap, card 405
    void* best = nullptr;
    double best_d = 0.0;
    static void* const* level_actors[kMaxLevels];
    static int32_t level_nums[kMaxLevels];
    int32_t levels_seen = 0;
    for (int32_t l = 0; l < num_levels && l < kMaxLevels; ++l) {
        auto* const* const actors = levels[l] != nullptr ? static_cast<void* const*>(read_ptr(levels[l], kLevelActors)) : nullptr;
        const int32_t num = actors != nullptr ? read_i32(levels[l], kLevelActors + kArrayNum) : 0;
        level_actors[levels_seen] = actors;
        level_nums[levels_seen] = num;
        levels_seen += actors != nullptr ? 1 : 0;
        for (int32_t a = 0; a < num && a < kMaxLevelActors; ++a) {
            if (actors[a] == nullptr || !c50_name_match(actors[a], ref) || !c50_placed(actors[a])) { // cards 404, 405
                continue;
            }
            const double d = vec_dist(actor_location(actors[a]), at);
            if (best == nullptr || d < best_d) {
                best = actors[a];
                best_d = d;
            }
        }
    }
    return best != nullptr ? best : c95_name_fallback(level_actors, level_nums, levels_seen, name, at);
}
} // namespace

// Client: the actor's component of this name (its subobject), or null.
void* c95_component(void* actor, const char* name) {
    constexpr size_t kMax = 255; // card 136 run 4: effects name rows carry up to 255 bytes
    wchar_t wide[kMax + 1];
    size_t i = 0;
    for (; name[i] != '\0' && i < kMax; ++i) {
        wide[i] = static_cast<wchar_t>(static_cast<unsigned char>(name[i]));
    }
    wide[i] = L'\0';
    void* const c = g_static_find_object(nullptr, actor, wide, false);
    return c != nullptr && read_ptr(c, kComponentOwner) == actor ? c : nullptr;
}

namespace {

// Host: "parent/component" from c95_wire_target. The parent is found by name nearest `at`, then its component's
// ChildActor is this machine's child actor.
void* c95_child_actor(const void* world, const char* name, const Vec3& at) {
    char parent_name[kChanNameMax + 1];
    const char* const slash = strchr(name, '/');
    const size_t n = static_cast<size_t>(slash - name);
    if (n == 0 || n > kChanNameMax || slash[1] == '\0') {
        log::line("WARN card133: child actor name %s is malformed", name);
        return nullptr;
    }
    memcpy(parent_name, name, n);
    parent_name[n] = '\0';
    void* const parent = c95_find_nearest(world, parent_name, at);
    void* const cac = parent != nullptr ? c95_component(parent, slash + 1) : nullptr;
    void* const child = cac != nullptr ? c31_object_prop(cac, "ChildActor") : nullptr;
    log::line("%scard133: child actor %s -> parent %s, component %s, child %s", child != nullptr ? "" : "WARN ", name,
              parent != nullptr ? object_name(parent).text : "(none)", cac != nullptr ? object_name(cac).text : "(none)",
              child != nullptr ? object_name(child).text : "(none)");
    return child;
}

} // namespace

namespace {

// Card 147: host, at a load. Each collision row's actor: the placed same-named actor nearest the row's location,
// within kC95RowUu, or null. The pointers live only for this call.
void c95_match_rows(const void* world, const void* best[], double best_d[]) {
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    for (int32_t l = 0; levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const auto* const actors = levels[l] != nullptr ? static_cast<void* const*>(read_ptr(levels[l], kLevelActors)) : nullptr;
        const int32_t num = actors != nullptr ? read_i32(levels[l], kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; a < num && a < kMaxLevelActors; ++a) {
            char got[kC95ActorNameMax + 1]; // card 283
            if (actors[a] == nullptr || !c50_placed(actors[a]) || c50_name(actors[a], got, sizeof(got)) <= 0) {
                continue;
            }
            for (int32_t i = 0; i < g_c95c.n; ++i) {
                if (strcmp(g_c95c.row[i].name, got) != 0) {
                    continue;
                }
                const double d = vec_dist(actor_location(actors[a]), g_c95c.row[i].at);
                if (d <= kC95RowUu && (best[i] == nullptr || d < best_d[i])) {
                    best[i] = actors[a];
                    best_d[i] = d;
                }
            }
        }
    }
}

// Card 147: the actor's current value for this row (the actor flag, or the named component's CollisionEnabled), or
// -1 when the component is gone.
int c95_current_value(const void* actor, const char* component) {
    if (strcmp(component, kC95ActorFlag) == 0) {
        return static_cast<const uint8_t*>(actor)[kActorCollisionFlags] & kActorEnableCollisionBit;
    }
    const void* const comp = c95_component(const_cast<void*>(actor), component);
    return comp != nullptr ? static_cast<const uint8_t*>(comp)[kBodyCollisionEnabled] : -1;
}

} // namespace

// Card 147: host, after a save load (LoadFromSlot returned). The load sets collision from the save, maybe with no
// relayed write, so each collision row takes its actor's current value, and a row whose actor (or component) no longer
// exists is removed. Journal only: nothing is sent.
void c95_load_collisions(const void* world, C137LoadCounts& n) {
    // Card 465: 64 kB at 4096 rows, so static (game thread only), cleared per call.
    static const void* best[kC95Collisions];
    static double best_d[kC95Collisions];
    memset(best, 0, sizeof(best));
    memset(best_d, 0, sizeof(best_d));
    n.c95c_before = static_cast<uint32_t>(g_c95c.n);
    c95_match_rows(world, best, best_d);
    int32_t kept = 0;
    for (int32_t i = 0; i < g_c95c.n; ++i) {
        C95Collision r = g_c95c.row[i];
        const int value = best[i] != nullptr ? c95_current_value(best[i], r.component) : -1;
        if (value < 0) {
            ++n.c95c_removed;
            if (c137_load_line(n)) {
                log::line("card137: host load reconcile c95 collision %s %s: no actor or component, row removed",
                          r.name, r.component);
            }
            continue;
        }
        if (static_cast<unsigned>(value) != r.value) {
            ++n.c95c_fixed;
            if (c137_load_line(n)) {
                log::line("card137: host load reconcile c95 collision %s %s: value %u -> %d", r.name, r.component,
                          r.value, value);
            }
            r.value = static_cast<uint8_t>(value);
        }
        r.at = actor_location(best[i]);
        g_c95c.row[kept++] = r;
    }
    g_c95c.n = kept;
    n.c95c_after = static_cast<uint32_t>(kept);
}

namespace {

// Client: every placed, unreplicated actor by name hash (the host's filter in c95_relay_collision), sorted.
void c421_own_walk(const void* world) {
    C421State& p = g_c421;
    LARGE_INTEGER t0;
    LARGE_INTEGER t1;
    LARGE_INTEGER freq;
    QueryPerformanceCounter(&t0);
    p.own_n = 0;
    p.own_ms = GetTickCount64();
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = levels != nullptr ? read_i32(world, kWorldLevels + kArrayNum) : 0;
    for (int32_t l = 0; l < num_levels && l < kMaxLevels; ++l) {
        auto* const* const actors = levels[l] != nullptr ? static_cast<void* const*>(read_ptr(levels[l], kLevelActors)) : nullptr;
        const int32_t num = actors != nullptr ? read_i32(levels[l], kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; a < num && a < kMaxLevelActors; ++a) {
            char name[kC95ActorNameMax + 1];
            if (actors[a] == nullptr || !c50_placed(actors[a]) || c95_replicated(actors[a]) ||
                c50_name(actors[a], name, sizeof(name)) <= 0) {
                continue;
            }
            if (p.own_n >= kC421Own) {
                ++p.own_full;
                continue;
            }
            p.own[p.own_n].hash = c421_hash(name);
            c31_weak_set(p.own[p.own_n++].actor, actors[a]);
        }
    }
    std::sort(p.own, p.own + p.own_n, [](const C421Own& x, const C421Own& y) { return x.hash < y.hash; });
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&freq);
    ++p.walks;
    p.walk_us += static_cast<double>(t1.QuadPart - t0.QuadPart) * 1e6 / static_cast<double>(freq.QuadPart);
}

// The live actors of that name hash; `last`: one of them. A table that overflowed, or never walked, has none.
int32_t c421_own_count(uint32_t hash, void*& last) {
    const C421Own* const begin = g_c421.own;
    const C421Own* const end = begin + g_c421.own_n;
    const C421Own* it = std::lower_bound(begin, end, hash, [](const C421Own& o, uint32_t h) { return o.hash < h; });
    int32_t count = 0;
    last = nullptr;
    for (; g_c421.own_full == 0 && it != end && it->hash == hash; ++it) {
        void* const actor = c31_weak_get(it->actor);
        count += actor != nullptr ? 1 : 0;
        last = actor != nullptr ? actor : last;
    }
    return count;
}

// Card 237: splits a component field "@<profile>:<component>" into the profile's existing FName (0 when the field
// has no profile) and the component name. False when the field is malformed or names a profile this process lacks.
bool c237_split(const char* field, uint64_t& profile, const char*& component) {
    profile = 0;
    component = field;
    if (field[0] != kC237Profile) {
        return true;
    }
    const char* const end = strchr(field + 1, kC237ProfileEnd);
    const size_t len = end != nullptr ? static_cast<size_t>(end - field - 1) : 0;
    if (len == 0 || len > kChanNameMax || g_fname_ctor == nullptr) {
        return false;
    }
    wchar_t wide[kChanNameMax + 1];
    for (size_t i = 0; i < len; ++i) {
        wide[i] = static_cast<wchar_t>(static_cast<unsigned char>(field[1 + i]));
    }
    wide[len] = L'\0';
    g_fname_ctor(&profile, wide, 0); // EFindName::FNAME_Find: a profile is a config name both sides hold
    component = end + 1;
    return profile != 0;
}

// Client: writes the row's value to the actor flag or the named component (card 237: after the row's collision
// profile, when it has one); false when it cannot (no actor, no such component, a flag value over 1, an unknown
// profile). `text` names the row in the change line.
bool c95_client_apply(void* actor, const char* field, unsigned value, const char* text) {
    uint64_t profile = 0;
    const char* component = field;
    if (!c237_split(field, profile, component)) {
        log::line("WARN card237: client collision %s: bad or unknown profile field", text);
        return false;
    }
    const bool flag = strcmp(component, kC95ActorFlag) == 0;
    void* const comp = actor != nullptr && !flag ? c95_component(actor, component) : nullptr;
    if (actor == nullptr || (!flag && comp == nullptr) || (flag && value > 1) || (flag && profile != 0)) {
        return false;
    }
    const auto* const bytes = static_cast<const uint8_t*>(flag ? actor : comp);
    const unsigned before = flag ? (bytes[kActorCollisionFlags] & kActorEnableCollisionBit) : bytes[kBodyCollisionEnabled];
    uint64_t profile_before = 0;
    if (profile != 0 && g_collision_profile != nullptr) {
        g_collision_profile(comp, &profile_before);
    }
    if (flag) {
        g_orig_set_actor_collision(actor, value != 0);
    } else {
        if (profile != 0) {
            g_orig_set_collision_profile(comp, profile, true);
        }
        g_orig_set_collision_enabled(comp, static_cast<uint8_t>(value));
    }
    ++g_c95c.applied;
    const unsigned after = flag ? (bytes[kActorCollisionFlags] & kActorEnableCollisionBit) : bytes[kBodyCollisionEnabled];
    c137_host_apply(actor, flag ? "actor collision" : "component collision",
                    static_cast<int32_t>(flag ? (before != 0 ? 1u : 0u) : before),
                    static_cast<int32_t>(flag ? (after != 0 ? 1u : 0u) : after), 0); // card 149: the mod's apply
    // Only the writes that change our copy (the replay repeats load writes).
    if ((before != after || profile != profile_before) && c95_line()) {
        log::line("card095: client t=%llu collision %s (%s): applied, %u -> %u%s%s", static_cast<unsigned long long>(GetTickCount64()),
                  text, class_name(actor).text, before, after, profile != 0 ? ", profile was " : "",
                  profile != 0 ? fname_text(&profile_before).text : "");
    }
    return true;
}

struct C421Want {
    char text[kChanTextMax + 1];
    int32_t len, hashes;
    uint32_t last;
};

void c421_want_flush(void* pc, C421Want& w) {
    if (w.hashes == 0) {
        return;
    }
    static wchar_t wire[kChanTextFrame];
    const WireString frame = chan_text_frame(ChanOp::CollisionOp, w.text, wire);
    const bool sent = frame.num > 0 && chan_send_host(pc, frame);
    ++g_c421.want_frames;
    if (!sent) {
        ++g_c421.bad;
        log::line("WARN card421: client: want frame of %d name hashes not sent", w.hashes);
    }
    w.len = 0;
    w.hashes = 0;
}

void c421_want_add(void* pc, C421Want& w, uint32_t hash) {
    ++g_c421.wanted;
    if (w.hashes != 0 && w.last == hash) { // the same actor's next component: its rows are already asked for
        return;
    }
    if (w.len + 12 > kChanTextMax) {
        c421_want_flush(pc, w);
    }
    if (w.hashes == 0) {
        w.text[0] = kC421Want;
        w.len = 1;
    }
    w.len += snprintf(w.text + w.len, sizeof(w.text) - static_cast<size_t>(w.len), " %u", hash);
    w.last = hash;
    ++w.hashes;
}

// Client: the host's pair frame (the text after its head char). A pair whose name we hold exactly once is written
// here; the rest are asked for by name hash and arrive as whole rows.
void c421_client_pairs(void* pc, const char* pairs) {
    static_assert(kChanNameMax == 64, "the %64s below is kChanNameMax");
    C421State& p = g_c421;
    const void* const world = g_actor_get_world(pc);
    if (world != nullptr && (p.own_ms == 0 || GetTickCount64() - p.own_ms >= kC421OwnMs)) {
        c421_own_walk(world);
    }
    ++p.pair_frames;
    C421Want w{};
    const char* at = pairs;
    for (int32_t k = 0; k < kC421FramePairs; ++k) {
        unsigned hash = 0;
        unsigned value = 0;
        char comp[kChanNameMax + 1];
        int used = 0;
        if (sscanf_s(at, " %u %u %64s%n", &hash, &value, comp, static_cast<unsigned>(sizeof(comp)), &used) != 3) {
            break;
        }
        at += used;
        ++p.pairs;
        void* actor = nullptr;
        const int32_t count = c421_own_count(hash, actor);
        char what[kC95ActorNameMax + kChanNameMax + 16];
        snprintf(what, sizeof(what), "%s pair %u %s", count == 1 ? object_name(actor).text : "?", value, comp);
        if (count == 1 && value <= kC95MaxCollisionValue && c95_client_apply(actor, comp, value, what)) {
            ++p.equal; // applied here: the host holds this name once, and so do we
            continue;
        }
        p.no_actor += count == 0 ? 1u : 0u;
        p.many += count > 1 ? 1u : 0u;
        p.differ += count == 1 ? 1u : 0u;
        if (p.wanted < kC421WhyLines) {
            log::line("card421: client asks for hash %u component %s host value %u: %d live copies (%s)", hash, comp,
                      value, count, what);
        }
        c421_want_add(pc, w, hash);
    }
    c421_want_flush(pc, w);
    const bool whole = at[strspn(at, " ")] == '\0';
    if (!whole) {
        ++p.bad;
        log::line("WARN card421: client: bad pair frame after %u pairs", p.pairs);
    }
    c132_ledger_result(whole ? C132Result::Applied : C132Result::Refused, whole ? nullptr : "bad pair frame");
}

// Host: every journal row of this name hash to pc, whole. The rows sent.
uint32_t c421_send_named(void* pc, uint32_t hash) {
    uint32_t rows = 0;
    for (int32_t i = 0; i < g_c95c.n && i < kC95Collisions; ++i) {
        if (g_c421.hash[i] != hash) {
            continue;
        }
        char text[kC95ActorNameMax + kChanNameMax + 64]; // card 283
        c95_collision_text(g_c95c.row[i], text, sizeof(text));
        rows += c95_send_text(pc, ChanOp::CollisionOp, text) ? 1u : 0u;
    }
    return rows;
}

// Card 237: one whole row of `actor`'s current values to pc: the actor flag (component null), or the component's
// CollisionEnabled with its collision profile. False when not sent.
bool c237_send_value(void* pc, const void* actor, const void* component) {
    C95Collision r{};
    if (c50_name(actor, r.name, sizeof(r.name)) <= 0 ||
        (component != nullptr && c50_name(component, r.component, sizeof(r.component)) <= 0)) {
        return false;
    }
    r.at = actor_location(actor);
    if (component == nullptr) {
        snprintf(r.component, sizeof(r.component), "%s", kC95ActorFlag);
        r.value = static_cast<const uint8_t*>(actor)[kActorCollisionFlags] & kActorEnableCollisionBit;
    } else {
        r.value = static_cast<const uint8_t*>(component)[kBodyCollisionEnabled];
        if (g_collision_profile != nullptr) {
            g_collision_profile(component, &r.profile);
        }
        r.profile = strlen(fname_text(&r.profile).text) + strlen(r.component) + 2 <= kChanNameMax ? r.profile : 0;
    }
    char text[kC95ActorNameMax + kChanNameMax + 64]; // card 283
    c95_collision_text(r, text, sizeof(text));
    const bool sent = c95_send_text(pc, ChanOp::CollisionOp, text);
    if (note_event_cap(g_c237_lines, kC237Lines, "card237 current value")) {
        log::line("%scard237: host t=%llu want for a name with no journal row: current %s sent %d",
                  sent ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()), text, sent ? 1 : 0);
    }
    return sent;
}

// Card 237: host, a want for a name hash no journal row holds (a write the journal missed, such as one made before the
// host listened, or a row removed since the pair went out): every placed actor of that hash answers with its current
// actor flag and its root primitive's CollisionEnabled and profile, as whole rows. The rows sent.
uint32_t c237_send_current(void* pc, uint32_t hash) {
    static void* primitive_cls = nullptr;
    primitive_cls = primitive_cls != nullptr ? primitive_cls : find_class(L"/Script/Engine.PrimitiveComponent");
    const void* const world = g_actor_get_world(pc);
    if (world != nullptr && (g_c421.own_ms == 0 || GetTickCount64() - g_c421.own_ms >= kC421OwnMs)) {
        c421_own_walk(world);
    }
    const C421Own* const begin = g_c421.own;
    const C421Own* const end = begin + g_c421.own_n;
    const C421Own* it = std::lower_bound(begin, end, hash, [](const C421Own& o, uint32_t h) { return o.hash < h; });
    uint32_t rows = 0;
    for (; g_c421.own_full == 0 && it != end && it->hash == hash; ++it) {
        void* const actor = c31_weak_get(it->actor);
        const void* const root = actor != nullptr ? read_ptr(actor, kActorRootComponent) : nullptr;
        rows += actor != nullptr && c237_send_value(pc, actor, nullptr) ? 1u : 0u;
        rows += root != nullptr && primitive_cls != nullptr && is_a(root, primitive_cls) &&
                        c237_send_value(pc, actor, root)
                    ? 1u
                    : 0u;
    }
    return rows;
}

} // namespace

// Host: a client's want frame "? hash hash ..." (card 421). A hash no journal row holds is counted, nothing else.
void c421_receive_want(void* pc, const WireString* s) {
    char text[kChanTextMax + 1];
    if (chan_text(s, text, sizeof(text)) <= 1 || text[0] != kC421Want) {
        ++g_c421.bad;
        log::line("WARN card421: host: bad want frame from %s", object_name(pc).text);
        c132_ledger_result(C132Result::Refused, "bad frame");
        return;
    }
    ++g_c421.want_recv;
    c421_hash_rows();
    const char* at = text + 1;
    for (int32_t k = 0; k < kC421FramePairs; ++k) {
        char* end = nullptr;
        const unsigned long hash = strtoul(at, &end, 10);
        if (end == at) {
            break;
        }
        at = end;
        uint32_t rows = c421_send_named(pc, static_cast<uint32_t>(hash));
        rows = rows != 0 ? rows : c237_send_current(pc, static_cast<uint32_t>(hash)); // card 237
        g_c421.want_rows += rows;
        g_c421.unknown += rows == 0 ? 1u : 0u;
    }
    c132_ledger_result(C132Result::Applied, nullptr);
}

namespace {

// b184 (card 237 profile relay WARN class): true, counted, when the client cannot name `component`. A
// UserConstructionScript component (the zipline BP_ZiplinePoint_C `Spline Mesh`) is made again by each machine's own
// construction script, and its name is no identity on the other machine; a component with a transient-range name
// number (NiagaraComponent_2147476983 on BP_SatchelBuilding_Flakbug_C) was spawned at run time on the host only. The
// client's lookup (c95_receive_collision, StaticFindObject under the placed actor) finds neither, so the relay would
// only log a WARN there. Both machines make these writes themselves.
// The CreationMethod test does not catch the zipline `Spline Mesh` (c237b184a: its 8 replay rows still WARNed): it is
// made at run time on each machine (the client's own write on it comes 23 ms after the skipped replay row, by the
// same caller exe+0x3B3D1AE as the host's), so it was not loaded with the map. A component without RF_WasLoaded (the
// owner's c50_placed test) is not in the client's copy of the package either, and is not relayed. Known limit: a
// gameplay write on such a component is not synced (as card 462).
bool c95_unnamed_component(const void* component) {
    static int32_t creation_offset = -2; // -2 unresolved, -1 no such property
    if (creation_offset == -2) {
        const uint8_t* const f = prop_find_prop(read_ptr(component, kObjectClass), "CreationMethod");
        creation_offset = f != nullptr ? read_i32(f, kPropertyOffset) : -1;
        log::line("%scard095: host UActorComponent::CreationMethod at +0x%X", f != nullptr ? "" : "WARN ",
                  static_cast<unsigned>(creation_offset));
    }
    if (creation_offset >= 0 &&
        static_cast<const uint8_t*>(component)[creation_offset] == kC95UserConstructionScript) {
        ++g_c95c.user_script;
        return true;
    }
    if (read_i32(component, kObjectName + 4) > kC95TransientNumber) {
        ++g_c95c.transient;
        return true;
    }
    if (!c50_placed(component)) {
        ++g_c95c.runtime;
        return true;
    }
    return false;
}

} // namespace

// Host, after a collision write on a listen server: a real change on a placed, non-replicated actor (component null
// for the actor-level flag) is journalled and sent to every ponged client.
void c95_relay_collision(const void* owner, const void* component, unsigned before, unsigned after,
                         unsigned long long caller, uint64_t profile) {
    if ((before == after && profile == 0) || owner == nullptr || net_mode(owner) != kNetModeListenServer || !c50_placed(owner) ||
        c95_replicated(owner)) {
        return;
    }
    // The significance update turns collision on and off by distance to this machine's own viewpoints (card 128: the
    // host's dropship flight swept about 40 such changes a second over the map, filling the journal and, on the
    // reliable ClientMessage channel, the client's reliable buffer within 4 s of a stall). Each machine runs its own
    // significance update for its own view, so these changes are local state, not world state.
    if (g_c95_significance_depth > 0) {
        ++g_c95c.local;
        return;
    }
    // Card 147: the constructor (and its components' constructors) writes the same value on both machines.
    if ((read_i32(owner, kC50ObjectFlags) & kRfNeedInitialization) != 0) {
        ++g_c95c.construction;
        return;
    }
    char name[kC95ActorNameMax + 1]; // card 283
    // Card 425: the component's name with its number (c50_name): the client finds it by StaticFindObject, and five
    // construction-script components NODE_AddStaticMeshComponent-3_<n> sent without it matched none (c399b122a).
    char comp[kChanNameMax + 1];
    if (c50_name(owner, name, sizeof(name)) <= 0 ||
        (component != nullptr && c50_name(component, comp, sizeof(comp)) <= 0)) {
        log::line("WARN card095: host collision write on %s: name not sendable", object_name(owner).text);
        return;
    }
    // Card 462: a construction-script node component (NODE_Add...) is numbered by a per-process counter, so its name
    // is no identity on the other machine (BP_PylonSpline_C: -3_0, -3_6.. at the first replay, -3, -3_0.. after the
    // host's in-place load, others on the client's fresh map; 29 client skipped rows, c460b168a). Both machines make
    // these writes themselves at their own map load (client "by local sim: 29"), so they stay local. Known limit: a
    // gameplay write on such a component is not synced (none observed).
    if (component != nullptr && strncmp(comp, kC95ScriptNodePrefix, sizeof(kC95ScriptNodePrefix) - 1) == 0) {
        ++g_c95c.script_node;
        return;
    }
    if (component != nullptr && c95_unnamed_component(component)) {
        return;
    }
    const char* const comp_key = component != nullptr ? comp : kC95ActorFlag;
    const Vec3 at = actor_location(owner);
    C95Collision tmp{};
    C95Collision* const r = c95_collision_row(name, comp_key, at, static_cast<uint32_t>(caller));
    C95Collision& row = r != nullptr ? *r : tmp;
    if (r == nullptr) {
        snprintf(row.name, sizeof(row.name), "%s", name);
        snprintf(row.component, sizeof(row.component), "%s", comp_key);
    }
    row.at = at;
    row.value = static_cast<uint8_t>(after);
    // Card 237: the profile's text and the component name must fit the client's component field together.
    if (profile != 0 && strlen(fname_text(&profile).text) + strlen(comp_key) + 2 > kChanNameMax) {
        log::line("WARN card237: host profile %s on %s %s: too long for the component field, not relayed",
                  fname_text(&profile).text, name, comp_key);
        profile = 0;
    }
    row.profile = profile != 0 ? profile : row.profile;
    char text[kC95ActorNameMax + kChanNameMax + 64]; // card 283
    c95_collision_text(row, text, sizeof(text));
    const C95Fanout f = c95_send_all(g_actor_get_world(owner), ChanOp::CollisionOp, text);
    if (f.to > 0 && c95_line()) { // map load writes come before any client: journalled, not logged
        const bool teardown = f.sent != f.to && c332_on(); // card 346: refused and counted by the teardown row
        log::line("%scard095: host t=%llu collision %s %u -> %u (%s, caller exe+0x%llX): sent to %d of %d clients%s%s",
                  f.sent == f.to || teardown ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()), text,
                  before, after, class_name(owner).text, caller, f.sent, f.to, teardown ? " (card 332 teardown)" : "",
                  r != nullptr ? "" : " (journal full: not replayed)");
    }
}

// Client: the host's CollisionOp frame "name x y z value component|-", applied to our nearest same-named copy.
void c95_receive_collision(void* pc, const WireString* s) {
    static_assert(kC95ActorNameMax == 191, "card 283: the %191s below is kC95ActorNameMax");
    char text[kChanTextMax + 1];
    C95Collision r{};
    double x = 0.0, y = 0.0, z = 0.0;
    unsigned value = 0;
    const bool got = chan_text(s, text, sizeof(text)) > 0;
    if (got && text[0] == kC421Pairs) { // card 421: no actor name starts with it
        c421_client_pairs(pc, text + 1);
        return;
    }
    if (!got ||
        // Card 235: the component name is the rest of the text; the game may name a component with a space.
        sscanf_s(text, "%191s %lf %lf %lf %u %64[^\n]", r.name, static_cast<unsigned>(sizeof(r.name)), &x, &y, &z, &value,
                 r.component, static_cast<unsigned>(sizeof(r.component))) != 6 ||
        value > kC95MaxCollisionValue) {
        log::line("WARN card095: client: bad CollisionOp frame");
        c132_ledger_result(C132Result::Refused, "bad frame");
        return;
    }
    void* const actor = c95_find_nearest(g_actor_get_world(pc), r.name, Vec3{x, y, z});
    if (!c95_client_apply(actor, r.component, value, text)) {
        ++g_c95c.missed;
        c132_ledger_result(C132Result::Skipped, "no placed copy matches (see the WARN line)");
        // Card 462: the skipped text under its own cap, so a spent c95_line cap no longer hides it.
        if (g_c95c.skip_lines < kC462SkipLines) {
            ++g_c95c.skip_lines;
            log::line("WARN card095: client t=%llu collision %s: no placed actor (or component) matches, skipped",
                      static_cast<unsigned long long>(GetTickCount64()), text);
        }
        return;
    }
    c132_ledger_result(C132Result::Applied, nullptr);
}

// `door list`: every loaded ASTDoor, and blocker-like actors (class has Block, Door, Gate, Barrier or Wall) within
// kC95NearUu of the pawn. Null or why not.
const char* c95_list(void* world, const void* pawn) {
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    const Vec3 from = pawn != nullptr ? actor_location(pawn) : Vec3{};
    uint32_t lines = 0;
    for (int32_t l = 0; levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const auto* const actors = levels[l] != nullptr ? static_cast<void* const*>(read_ptr(levels[l], kLevelActors)) : nullptr;
        const int32_t num = levels[l] != nullptr ? read_i32(levels[l], kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; actors != nullptr && a < num && a < kMaxLevelActors && lines < kC95ListLines; ++a) {
            const void* const actor = actors[a];
            if (actor == nullptr) {
                continue;
            }
            const bool door = is_a(actor, g_c95_door_class());
            if (door || (pawn != nullptr && vec_dist(actor_location(actor), from) <= kC95NearUu &&
                         c95_is_blocker_like(actor))) {
                c95_log_actor(door ? "list door" : "list near", actor);
                ++lines;
            }
        }
    }
    log::line("card095: %s door list: %u lines (cap %u), pawn at (%.0f, %.0f, %.0f)", side(), lines, kC95ListLines,
              from.x, from.y, from.z);
    return nullptr;
}

// Test command: `door list`, or (op 'O'/'C') the named placed door's OpenDoor/CloseDoor through the hooked path, as
// the level script calls it. Null or why not.
const char* c95_cmd(void* world, const void* pawn, const char* name, uint8_t op) {
    if (world == nullptr) {
        return "no world";
    }
    if (op == 0) {
        return c95_list(world, pawn);
    }
    const StaticClassFn* const cls[] = {&g_c95_door_class};
    void* const door = c50_find_placed(world, cls, 1, name);
    if (door == nullptr) {
        return c161_cmd(name, op); // card 161: a BP_Door_Base door
    }
    log::line("card095: %s t=%llu cmd door %s %s", side(), static_cast<unsigned long long>(GetTickCount64()),
              op == 'O' ? "open" : "close", name);
    (op == 'O' ? detour_c95_open : detour_c95_close)(door);
    return nullptr;
}

// Host and client: the significance update for this machine's own viewpoints (card 147: it calls the class's
// OnSignificanceChanged, then BP_OnSignificanceChanged); collision writes inside are its view LOD. A counter, not a
// flag, so a nested call keeps the outer one counted.
void detour_c95_process_significance(void* self) {
    ++g_c95_significance_depth;
    g_orig_c95_process_significance(self);
    --g_c95_significance_depth;
}

// Card 453 step 1 (diagnostic, host, UBWCOOP_DIAG or UBWCOOP_C50_TEST): kC453DelayMs after each map arm, one row per
// ASTDoor class in the world's levels: its doors, how many have the crush volume (door+0x360, the box
// ASTDoor::HandleCharactersInMovePath tests the host player against), the damage flag (door+0x355: USTDoorDamageData
// damage instead of InstaKillTarget) and the byte at +0x357. A sublevel streamed in after the scan is not counted.
namespace {
constexpr ULONGLONG kC453DelayMs = 5000;
constexpr int32_t kC453Classes = 16;
constexpr size_t kC453DamageFlag = 0x355;
constexpr size_t kC453Byte357 = 0x357;
constexpr size_t kC453CrushVolume = 0x360;
struct C453Class {
    const void* cls;
    int32_t doors, crush, damage, b357;
};
ULONGLONG g_c453_arm_ms = 0; // 0 = scanned (or not armed); game thread
} // namespace

void c453_arm_map() {
    g_c453_arm_ms = GetTickCount64();
}

void c453_step(void* engine) {
    if (g_c453_arm_ms == 0 || GetTickCount64() - g_c453_arm_ms < kC453DelayMs || g_c95_door_class == nullptr) {
        return;
    }
    g_c453_arm_ms = 0;
    const void* const world = g_get_game_world(engine);
    const auto* const levels = world != nullptr ? static_cast<void* const*>(read_ptr(world, kWorldLevels)) : nullptr;
    const int32_t num_levels = world != nullptr ? read_i32(world, kWorldLevels + kArrayNum) : 0;
    C453Class t[kC453Classes] = {};
    int32_t n = 0;
    int32_t dropped = 0;
    for (int32_t l = 0; levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const auto* const actors = levels[l] != nullptr ? static_cast<void* const*>(read_ptr(levels[l], kLevelActors)) : nullptr;
        const int32_t num = levels[l] != nullptr ? read_i32(levels[l], kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; actors != nullptr && a < num && a < kMaxLevelActors; ++a) {
            const auto* const door = static_cast<const uint8_t*>(actors[a]);
            if (door == nullptr || !is_a(door, g_c95_door_class())) {
                continue;
            }
            const void* const cls = read_ptr(door, kObjectClass);
            int32_t k = 0;
            while (k < n && t[k].cls != cls) { // bounded: n <= kC453Classes
                ++k;
            }
            if (k == n && n == kC453Classes) {
                ++dropped;
                continue;
            }
            n += k == n ? 1 : 0;
            t[k].cls = cls;
            ++t[k].doors;
            t[k].crush += read_ptr(door, kC453CrushVolume) != nullptr ? 1 : 0;
            t[k].damage += door[kC453DamageFlag] != 0 ? 1 : 0;
            t[k].b357 += door[kC453Byte357] != 0 ? 1 : 0;
        }
    }
    for (int32_t k = 0; k < n; ++k) {
        log::line("card453: host door class %s: %d doors, crush volume %d, damage flag %d, +0x357 set %d",
                  object_name(t[k].cls).text, t[k].doors, t[k].crush, t[k].damage, t[k].b357);
    }
    log::line("card453: host t=%llu door scan: %d class(es) in %d level(s)%s, %d door(s) of further classes not "
              "counted", static_cast<unsigned long long>(GetTickCount64()), n, num_levels, n == 0 ? " (no ASTDoor)" : "",
              dropped);
}

} // namespace ubw::hooks
