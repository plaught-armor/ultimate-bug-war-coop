// Card 132 step 2: the world state digest, host versus client. Every kC132RoundMs (and on the `objectives` test
// command) the host digests every placed actor that does not replicate, every child actor, every runtime actor it
// relayed as a card 104 copy, and every objective. An entry's key is the FNV-1a of a name: the placed actor's name
// (c50_name, the wire's form), "parent/component" for a child actor, "#<host object index>" for a runtime actor and
// "O:<id>" for an objective, the same names the card 132 ledger keys its rows by. Entries are matched by key and
// level (names repeat across sublevels); the location (kC132LocStep steps, the child actor's parent's) is a field.
// The state is what a player can see or use: health, hidden, collision, the pickup's IsPickupEnabled and mesh, and
// per class the field its folded setter writes (kC132Ext).
// The host sends the round over ChanOp::Digest, one frame per kC132FrameMs (each frame is a reliable RPC that shares
// the connection's rate with actor replication: run nb2 sent all 30k entries, 2 MB, in ~740 frames every 30 s, and
// the 100 KB/s connection then stayed not net ready ~20 s, so NPC health and deaths reached the client in bursts).
// So the round carries no entries. Each entry belongs to a slot (its level index, kC132Buckets buckets by the key's
// top bits); the host sends each non-empty slot's entry count and the sum of its entries' hashes (class name, state,
// ext, location, key). The client sums its own digest the same way, its levels mapped to the host's, and asks for the
// entries of the slots that differ (ChanOp::DigestQuery "D", at most kC132DetailSlots); the host sends just those
// entries to that PC, and the client compares them entry by entry as before. A slot whose sums match is equal.
// The B frame is sent to each remote PC on its own with S, the last ledger seq sent to that PC before it, at the tick the
// host digest was built. The client builds its own digest once it has received every row up to S (at most
// kC132SnapWaitMs, then a WARN), and compares when the end frame arrives. Keys whose last received row is after S
// are left out of the entry compare and counted. Slots of a level only one side has loaded are skipped and counted
// (their entries). The client logs each mismatch with the key's last ledger row, a total line per round, and asks the
// host (ChanOp::DigestQuery "Q") for the names of the host-only keys, which the host logs. Duplicate keys are found
// in the client's whole digest and in the host's detailed slots.
// Card 329 step A: the round sent the level and class tables and every slot sum to every PC each time (2030 B a
// round, the same whether or not anything differed). Now the B frame carries the tables' hash T and the whole sum W
// (c132_whole over the slot sums). The tables (V, C) go only to a PC whose last tables were not T (the host keeps
// the T it last sent each PC); the client keeps them between rounds and checks them against T at E (on a mismatch it
// asks "T": the host forgets what it sent that PC, the next round sends the tables, this round is dropped). At E the
// client sums its digest; when its W equals the host's every slot is equal and the round ends; otherwise it asks
// for the slot sums ("H"), the host sends the H frames and a G frame to that PC only, and the slot compare, the
// detail ask and the entry compare run as before.
// Frames (printable ASCII, all numbers hex):
//   B<round> <entries> <classes> <levels> <S> <T> <tables> <W> <PW> <why>   round start, per PC; tables 1: V, C
//                                                           follow; PW: card 327, that PC's inventory hash
//   V<idx>=<level name>;...                                 level table (the level's outer, the map or sublevel)
//   C<idx>=<class name>;...                                 class table
//   E<round>                                                round end
//   H<slot4><count4><hash8>...                              slot sums, to the asking PC only
//   G<round>                                                slot sums end, to the asking PC only
//   P<key8><level2><class3><state2><ext4><x4><y4><z4>...    detail entries, to the asking PC only
//   F<round>                                                detail end, to the asking PC only
// Log only: nothing is applied from a digest.

#include "ubw/hooks_internal.h"

#include <intrin.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ubw::hooks {

// Rows 0-9 are filled by hooks_ubw.cpp's resolve table; row 10 (card 423) is a Blueprint class, found by path.
StaticClassFn g_c132_ext_class[kC132ExtClasses] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                                   nullptr, nullptr, nullptr, nullptr, c423_destroyable_class};

namespace {

constexpr ULONGLONG kC132RoundMs = 30000;
// Card 386: a build walks every actor in one tick on each side (23 ms on FED_C2M1, 30000 actors). The rounds are
// log only, so a play build runs them 4 times less often; test runs (UBWCOOP_TEST_SKIP, set by tools/run_pair.sh)
// keep kC132RoundMs.
constexpr ULONGLONG kC386PlayRoundMs = 120000;
constexpr ULONGLONG kC132SnapWaitMs = 5000; // client: rows up to S before the snapshot
// Card 232: host, after a round's end frame, no new round for this long, so the client's detail ask (22 ms after the
// end frame in c229-c1m1) reaches the round it belongs to.
constexpr ULONGLONG kC132SettleMs = 2000;
constexpr int32_t kC132Entries = 32768;     // per digest
constexpr int32_t kC132DigestClasses = 1024;
constexpr int32_t kC132DigestLevels = 255;  // kC132GlobalLevel is the 256th
constexpr ULONGLONG kC132FrameMs = 100;    // host: one digest frame (at most ~2.8 KB) per this, ~28 KB/s at most
constexpr int32_t kC132BucketBits = 6;
constexpr int32_t kC132Buckets = 1 << kC132BucketBits; // per level, by the key's top bits
constexpr int32_t kC132Slots = 256 * kC132Buckets;     // level index (kC132GlobalLevel is 255) * buckets + bucket
constexpr int32_t kC132SlotChars = 16;                 // an H item: slot4 count4 hash8
constexpr int32_t kC132DetailSlots = 32;               // slots a client asks the entries of per round
constexpr int32_t kC132Details = 4;                    // host: detail requests queued (one per client)
constexpr int32_t kC132FullLines = 64;      // mismatch lines in full per round; the rest counted per class
constexpr size_t kC132ActorNetFlags = 0x58;  // AActor net bitfield bytes, +0x58..+0x5B
constexpr int32_t kC132QueryKeys = 64;      // host-only keys asked about per round
constexpr int32_t kC132Adopts = 32;         // placed actors the host digests although they replicate here, per map
constexpr int32_t kC132ClassCounts = 256;   // mismatch classes counted per round
constexpr uint32_t kC341Rows = 16;          // card 341 rows per session
constexpr uint32_t kC341Adopt = 32;  // card 341: entries whose host steps the client's slot sum adopts
constexpr int32_t kC132LocTolerance = 2;    // location, in kC132LocStep steps: a mismatch above 100 uu
constexpr uint8_t kC132GlobalLevel = 0xFF;  // objectives: in no level
constexpr uint8_t kC132NoExt = 0xFF;
constexpr double kC132LocStep = 50.0;
constexpr uint32_t kC132Pool = 1u << 21;    // name chars per digest
constexpr int32_t kC132NameMax = 2 * kChanNameMax + 2;
constexpr int32_t kC132EntryChars = 31;
constexpr int32_t kC132AttachHint = 200;    // location gap in kC132LocStep steps (10000 uu): one side attached?

// Card 164: classes the client never reads (server-only nav data; spawn points, read only by the spawn path, which
// guards.cpp gates on the client). Left out of the digest on both sides, counted per row at each round.
constexpr const char* kC132ServerOnly[] = {
    "RecastNavMesh",       "RecastNavMesh_Small", "RecastNavMesh_Medium", "RecastNavMesh_Large",
    "RecastNavMesh_Gigantic", "BP_AISpawnPoint_C", "STSpawnPoint",
};
constexpr int32_t kC132ServerOnlyN = static_cast<int32_t>(sizeof(kC132ServerOnly) / sizeof(kC132ServerOnly[0]));

// Actor state bits.
constexpr uint8_t kC132Dead = 1u << 0;
constexpr uint8_t kC132Hidden = 1u << 1;
constexpr uint8_t kC132Collision = 1u << 2;
constexpr uint8_t kC132PickupOn = 1u << 3;
constexpr uint8_t kC132MeshVisible = 1u << 4;
constexpr uint8_t kC132MeshHiddenInGame = 1u << 5;
constexpr uint8_t kC132Child = 1u << 6;
constexpr uint8_t kC132Runtime = 1u << 7;
constexpr int32_t kC132ActorStateBits = 6; // bits compared; Child and Runtime are part of the key's kind
// Objective state bits (the entry's level is kC132GlobalLevel).
constexpr uint8_t kC132Complete = 1u << 0;
constexpr uint8_t kC132Marker = 1u << 1;
constexpr uint8_t kC132InList = 1u << 2;
const char* const kC132ActorBits[] = {"dead", "hidden", "collision", "pickup", "mesh visible", "mesh hiddeningame"};
const char* const kC132ObjectiveBits[] = {"complete", "marker", "inlist"};
static_assert(sizeof(kC132ActorBits) / sizeof(kC132ActorBits[0]) == kC132ActorStateBits, "one text per bit");

// AADPickupBase::IsPickupEnabled 0x04C61730 is vtable slot 236 (+0x760) of ??_7AADPickupBase@@6B@ (.rdata
// +0xE99618) and returns the byte +0x2A8: the logical bEnabled, which DisablePickup and the save state set. The
// ASTCustomPickupBase override 0x051CCF30 (same slot) ands it with two local streaming states: the
// IADPersistentActorInterface subobject's active byte (+0x408 + 0x18; PersistentActorActivate 0x051D25B0 adjusts this by
// -0x408) and ISTSignificanceObjectInterface::IsSignificant (+0x428, 0x0515F8C0). Run c132p1 showed ~100 pickups enabled
// on the host and not on the client from that alone (the client is far from them). So the digest's pickup bit is the
// byte +0x2A8, and the call (after IsA AADPickupBase) only says whether the pickup is live here: OnUpdatePickupStatus
// sets the mesh's visibility from the call's answer, so a pickup that is not live has a local mesh state, and the mesh
// visible bit then reads as the logical bEnabled.
// kPickupEnabledByte (+0x2A8) is in hooks_internal.h (card 137 step 3 reads it too).
constexpr size_t kPickupEnabledSlot = 0x760;
using PickupEnabledFn = bool (*)(const void* pickup);
// USceneComponent::IsVisible 0x03AE88D0 reads both. The setters write them: SetVisibility 0x03AF7060 does
// and r9b, -0x21 / or the new value << 5 into +0x1A0 (bVisible, bit 5), SetHiddenInGame 0x03AF2C50 does and r9b, -0x9 /
// or the new value << 3 into +0x1A1 (bHiddenInGame, bit 3), at +0x57 and +0x61 in each.
// kSceneVisibleByte / kSceneVisibleBit (bVisible) are in hooks_internal.h (card 145 reads them too).
constexpr size_t kSceneHiddenInGameByte = 0x1A1;
constexpr uint8_t kSceneHiddenInGameBit = 0x08;

// Card 132 step 2d: classes whose state a folded setter writes, with no mod mechanism. The field is read, not the
// setter detoured. Row order is g_c132_ext_class's (hooks_ubw.cpp registers each StaticClass); first match wins.
struct C132ExtRow {
    const char* cls;
    const char* field;
    uint16_t lo, hi; // byte offsets; hi 0: none. The entry's ext is lo | hi << 8.
};
const C132ExtRow kC132Ext[kC132ExtClasses] = {
    {"ASTLaser", "active", 0x388, 0},                    // SetLaserActive 0x05102A50 writes +0x388
    {"ASTHazardousZoneBase", "active/move", 0x2F8, 0x2F9}, // SetHazardActive 0x0516D340 +0x2F8; BeginMoveTo-
                                                           // ClosedPosition 0x0514CC80 / OpenPosition 0x0514CCF0
                                                           // write +0x2F9 = 3 / 2
    {"ASTJumpPadBase", "active", 0x2D8, 0},              // SetActive 0x0516CB60 writes +0x2D8
    {"ASTSpawnerBase", "enabled", 0x3FC, 0},             // SetSpawnerEnabled 0x01975B10 writes +0x3FC
    {"ASTTeleporterVolumeBase", "active", 0x2F0, 0},     // SetIsActive 0x019D7360 writes +0x2F0
    {"ASTActivationVolume", "active", 0x2EB, 0},         // Activate 0x05022080 writes 1, Deactivate 0x05152940 0
    {"ASTEnemyPresence", "triggers", 0x330, 0},          // SetTriggerEventsEnabled 0x05103770 writes +0x330
    {"ASTPlayerPresence", "triggers", 0x30A, 0},         // SetTriggerEventsEnabled 0x05103780 writes +0x30A
    {"ASTLargeElevator", "floor/move", 0x530, 0x300},    // NextFloor 0x050F5670 reads the floor int32 +0x530
                                                         // (low byte here) and the move state byte +0x300
    {"ASTSatchelPlacementSite", "neutralized", 0x50E, 0}, // card 292: NeutralizeSite 0x05161300 and Explode
                                                          // 0x051567E0 write 1 (with the site mesh +0x468 hidden),
                                                          // ResetSatchelSite_Implementation 0x0516C480 writes 0
    {"BP_DestroyableActor_Base_C", "destroyed", 0, 0},  // card 423: IsDestroyed; lo is c423_destroyed_offset()
};

// The row's low byte offset: fixed, except row 10's (card 423), which is the Blueprint property's.
uint16_t c132_row_lo(uint8_t row) {
    return row == kC132ExtDestroyable ? c423_destroyed_offset() : kC132Ext[row].lo;
}

struct C132Entry {
    uint32_t key;   // FNV-1a of the entry's name
    uint32_t lvl;   // FNV-1a of the level's name; 0 for objectives
    uint32_t name;  // offset of the name in the digest's pool (own digests; the host's are not sent)
    uint16_t cls;
    uint16_t ext;   // kC132Ext field, 0 with no row
    uint8_t level;  // index in the digest's level table, or kC132GlobalLevel
    uint8_t state;
    uint8_t ext_row; // kC132Ext row, or kC132NoExt
    int16_t loc[3];
};

struct C132Names {
    char name[kC132DigestClasses][kNameChars];
    int32_t n;
    uint32_t dropped;
};

struct C132Digest {
    C132Entry e[kC132Entries];
    int32_t n;
    uint32_t dropped, unnamed, children;
    uint32_t server_only[kC132ServerOnlyN]; // card 164: actors left out per kC132ServerOnly row
    uint32_t attached;                      // card 164: entries with an attach parent this round
    uint32_t attached_moved;                // card 164: of those, stepped world location changed since the last round
    char pool[kC132Pool];
    uint32_t pool_n;
    C132Names cls;
    C132Names lvl; // n <= kC132DigestLevels
};

enum class C132Phase : uint8_t { Idle, Begin, Levels, Classes, End };

// Per slot: the entry count and the sum of the entries' hashes (c132_entry_hash).
struct C132Sums {
    uint32_t hash[kC132Slots];
    uint16_t n[kC132Slots];
};

// Host: one client's ask for the entries of some slots of a round, or (card 329) for the round's slot sums.
struct C132Detail {
    void* pc;
    uint32_t round;
    bool sums;                       // card 329: the H frames and G, not the entries of `slots`
    uint64_t slots[kC132Slots / 64]; // bit set
    int32_t cursor;                  // next entry of the host digest
    int32_t frames;
};

struct C132Host {
    C132Digest d;
    C132Sums sums;
    uint32_t round;
    ULONGLONG next_ms;
    ULONGLONG next_frame_ms;
    C132Phase phase;
    bool inventory_due; // the map load inventory, on the first host tick of the map
    int32_t cursor;
    const char* why;
    int32_t frames, sent, to;
    C132Detail detail[kC132Details]; // FIFO, [0] in flight
    int32_t details;
    ULONGLONG settle_until_ms; // card 232: no round starts before this (the last round's end frame + kC132SettleMs)
    bool command_due;          // card 232: a command round asked for, started by the step
    uint32_t tables;           // card 329: c132_tables_hash of this round's digest
    uint32_t whole;            // card 329: c132_whole of `sums`
    void* tables_pc[kC17Slots];      // card 329: the PCs the tables were last sent to, and the T sent
    uint32_t tables_sent[kC17Slots];
    void* tables_to[kC17Slots];      // card 329: this round's PCs that get the V and C frames
    int32_t tables_to_n;
};

struct C132Client {
    C132Digest host; // received: the detail entries only
    C132Digest own;
    C132Sums host_sums; // received
    C132Sums own_sums;
    uint32_t round;
    uint32_t sent_s;  // the host's last seq to us before its B frame
    int32_t host_n;   // the host digest's entry count (B)
    int32_t own_n;    // the own digest's entry count, before the detail compare keeps the asked slots' entries
    C31Weak pc;       // own PC, between B and the compare (card 331: weak; a PC replaced mid-round reads null)
    ULONGLONG b_at;
    bool begun, snapped, ended, detail_asked;
    bool tables_follow, sums_asked; // card 329: this round's B said V and C follow; the slot sums were asked for
    uint32_t tables, whole;         // card 329: the host's T and W (B)
    uint32_t pw, pw_last_host, pw_last_own; // card 327: the host's PW (B); the last differing pair (c132_pw_compare)
    bool pw_told;                           // card 327: that pair was reported
    int32_t level_map[kC132DigestLevels]; // host level index to own, -1 not loaded here
    int32_t own_level_host[kC132DigestLevels]; // own level index to the host's, -1 not loaded there
    uint64_t asked[kC132Slots / 64];           // the slots asked for (bit set)
    int32_t asked_n, unasked;                  // slots asked for, and differing slots past kC132DetailSlots
};

C132Host g_c132h{};   // game thread
C132Client g_c132c{}; // game thread

uint32_t c132_fnv(const char* s) {
    uint32_t h = 2166136261u; // the ledger's (c132l_fnv): digest keys and ledger keys are one space
    for (int32_t i = 0; s[i] != '\0' && i < 256; ++i) {
        h = (h ^ static_cast<uint8_t>(s[i])) * 16777619u;
    }
    return h;
}

int16_t c132_step(double v) {
    const double r = floor(v / kC132LocStep + 0.5);
    return static_cast<int16_t>(r < -32768.0 ? -32768.0 : (r > 32767.0 ? 32767.0 : r));
}

// The index of `name` in `t`, added when new; -1 when full.
int32_t c132_name_index(C132Names& t, const char* name, int32_t cap) {
    for (int32_t i = 0; i < t.n; ++i) {
        if (strcmp(t.name[i], name) == 0) {
            return i;
        }
    }
    if (t.n >= cap) {
        ++t.dropped;
        return -1;
    }
    snprintf(t.name[t.n], kNameChars, "%s", name);
    return t.n++;
}

// `name` into the digest's pool; the offset, or the empty string's (0) when the pool is full.
uint32_t c132_pool_add(C132Digest& d, const char* name) {
    const size_t len = strlen(name);
    if (d.pool_n + len + 1 > kC132Pool) {
        return 0;
    }
    const uint32_t at = d.pool_n;
    memcpy(d.pool + at, name, len + 1);
    d.pool_n += static_cast<uint32_t>(len + 1);
    return at;
}

uint8_t c132_pickup_state(const void* actor) {
    if (g_c56_pickup_class == nullptr || !is_a(actor, g_c56_pickup_class())) {
        return 0;
    }
    const auto* const vtable = static_cast<const uint8_t*>(read_ptr(actor, 0));
    const auto enabled = vtable != nullptr ? reinterpret_cast<PickupEnabledFn>(read_ptr(vtable, kPickupEnabledSlot))
                                           : nullptr;
    const auto* const mesh = static_cast<const uint8_t*>(c31_object_prop(actor, "PickupMeshComponent"));
    const bool on = static_cast<const uint8_t*>(actor)[kPickupEnabledByte] != 0;
    const bool live = enabled != nullptr && enabled(actor) == on;
    const bool visible = live ? mesh != nullptr && (mesh[kSceneVisibleByte] & kSceneVisibleBit) != 0 : on;
    uint8_t s = on ? kC132PickupOn : 0u;
    s |= visible ? kC132MeshVisible : 0u;
    s |= mesh != nullptr && (mesh[kSceneHiddenInGameByte] & kSceneHiddenInGameBit) != 0 ? kC132MeshHiddenInGame : 0u;
    return s;
}

uint8_t c132_actor_state(const void* actor) {
    const auto* const a = static_cast<const uint8_t*>(actor);
    const void* const health = g_c130_health_class != nullptr ? find_component(actor, g_c130_health_class) : nullptr;
    uint8_t s = 0;
    // Card 460: a placed building loaded destroyed has its Blueprint Destroyed bool set while the load restores its
    // health (IsDead 0), the same test as c98_dead_now. The property lookup runs only when IsDead is false.
    s |= health != nullptr && g_c130_is_dead != nullptr && (g_c130_is_dead(health) || c98_bp_destroyed(actor))
             ? kC132Dead : 0u;
    s |= (a[kActorHiddenByte] & kActorHiddenBit) != 0 ? kC132Hidden : 0u;
    s |= (a[kActorCollisionFlags] & kActorEnableCollisionBit) != 0 ? kC132Collision : 0u;
    return static_cast<uint8_t>(s | c132_pickup_state(actor));
}

// Card 330 (run 11: c132_ext_step 7015.3 ms of the host's mod time, mean 252.7 us; its sweep asks this of 1024
// actors per tick, up to kC132ExtClasses is_a walks each). The row depends on the actor's class only, so it is kept
// per class: a direct-mapped table keyed by the UClass pointer and its name (a class freed and another made at its
// address has another name). Emptied per map (c132_arm_map).
constexpr int32_t kC132ClassSlots = 1024; // power of two
struct C132ClassSlot {
    const void* cls; // null: empty
    uint64_t name;   // the class's FName bytes
    uint8_t row;
};
C132ClassSlot g_c132_class_row[kC132ClassSlots];

uint8_t c132_class_ext(const void* actor) {
    for (int32_t i = 0; i < kC132ExtClasses; ++i) {
        const void* const cls = g_c132_ext_class[i] != nullptr ? g_c132_ext_class[i]() : nullptr;
        if (cls != nullptr && is_a(actor, cls)) { // card 423: row 10's class is null until its map loads it
            return static_cast<uint8_t>(i);
        }
    }
    return kC132NoExt;
}

// The kC132Ext row of `actor`, or kC132NoExt; its field into ext.
uint8_t c132_actor_ext(const void* actor, uint16_t& ext) {
    ext = 0;
    if (c220_per_player(actor)) {
        return kC132NoExt; // card 220: a prompt trigger's enabled byte is per player
    }
    const void* const cls = read_ptr(actor, kObjectClass);
    if (cls == nullptr) {
        return kC132NoExt;
    }
    uint64_t name = 0;
    memcpy(&name, static_cast<const uint8_t*>(cls) + kObjectName, sizeof(name));
    C132ClassSlot& slot = g_c132_class_row[(reinterpret_cast<uintptr_t>(cls) >> 4) & (kC132ClassSlots - 1)];
    if (slot.cls != cls || slot.name != name) {
        slot = C132ClassSlot{cls, name, c132_class_ext(actor)};
    }
    if (slot.row == kC132NoExt) {
        return kC132NoExt;
    }
    const auto* const a = static_cast<const uint8_t*>(actor);
    const C132ExtRow& r = kC132Ext[slot.row];
    ext = static_cast<uint16_t>(a[c132_row_lo(slot.row)] | (r.hi != 0 ? a[r.hi] << 8 : 0));
    if (slot.row == kC132ExtDestroyable && ext == 0 && c423_waiting(actor)) {
        ext = 1; // card 423: the host's value, which the waiting live row applies within kC423WaitMs
    }
    return slot.row;
}

// The runtime key of `actor` on this side, false when it is not a card 104 actor (host: relayed; client: a copy).
bool c132_runtime_id(const void* actor, uint32_t& id) {
    if (g_client_session) {
        return c104_copy_host_id(actor, id);
    }
    id = static_cast<uint32_t>(object_index(actor));
    return c104_is_relayed(actor);
}

bool c132_adopted(uint32_t key);
int32_t c132_adopt_count();

// A placed "_CAT_" actor c95_child_cac does not see as a child actor: what its root is attached to and who owns it,
// for the first kC132TemplateLines per map.
constexpr uint32_t kC132TemplateLines = 8;
uint32_t g_c132_template_lines = 0;

void c132_note_template(const void* actor) {
    if (g_c132_template_lines >= kC132TemplateLines) {
        return;
    }
    ++g_c132_template_lines;
    const void* const root = c31_object_prop(actor, "RootComponent");
    const void* const parent = root != nullptr ? c31_object_prop(root, "AttachParent") : nullptr;
    const void* const child = parent != nullptr ? c31_object_prop(parent, "ChildActor") : nullptr;
    const void* const owner = parent != nullptr ? read_ptr(parent, kComponentOwner) : nullptr;
    log::line("card132: %s template child %s: root %s (%s) attached to %s (%s), its ChildActor %s, its owner %s (%s)",
              side_text(), object_name(actor).text, object_name(root).text, class_name(root).text,
              object_name(parent).text, class_name(parent).text, child == actor ? "is this actor" : object_name(child).text,
              object_name(owner).text, class_name(owner).text);
}

// The digest name of `actor` (the ledger's key name) into out, the actor whose location is the entry's (a child
// actor's parent) and its kind bits (kC132Child, kC132Runtime). False when the digest leaves it out: it replicates,
// it is runtime and neither a child actor nor card 104, or its name does not fit (unnamed is set).
bool c132_actor_name(const void* actor, char* out, size_t cap, const void*& anchor, uint8_t& kind, bool& unnamed) {
    anchor = actor;
    kind = 0;
    unnamed = false;
    if ((static_cast<const uint8_t*>(actor)[kActorRepFlags] & 0x08u) != 0) {
        // The host's bReplicates decides: a placed actor the host digests (it does not replicate there) is in the
        // client's digest too once c132_resolve_host_only adopted its key, whatever the client's own flag says.
        return g_client_session && c132_adopt_count() > 0 && c50_placed(actor) &&
               c50_name(actor, out, static_cast<int32_t>(cap)) > 0 && c132_adopted(c132_fnv(out));
    }
    // A child actor saved with its level's parent is placed too (its name carries the "_CAT_" of the component's
    // child actor template): its location turns with the parent on each side by itself (the flak bug building's
    // SatchelSiteCAC), so it takes the child actor key and the parent's location below. The name check keeps the
    // reflected property lookups off the other placed actors. The check reads c50_name's form, which carries the FName
    // number ("..._CAT_1040"); object_name drops it ("..._CAT"), so run c132l1 never matched there.
    const bool placed = c50_placed(actor);
    const bool named = placed && c50_name(actor, out, static_cast<int32_t>(cap)) > 0;
    const bool template_child = named && text_contains(out, "_CAT_");
    const void* const cac = template_child || !placed ? c95_child_cac(actor) : nullptr;
    if (template_child && cac == nullptr) {
        c132_note_template(actor);
    }
    if (cac == nullptr && placed) {
        unnamed = !named;
        return named;
    }
    if (cac != nullptr) {
        anchor = c95_anchor(actor);
        char parent[kChanNameMax + 1];
        const int n = c50_name(anchor, parent, sizeof(parent)) > 0
                          ? snprintf(out, cap, "%s/%s", parent, object_name(cac).text)
                          : -1;
        unnamed = n <= 0 || static_cast<size_t>(n) >= cap;
        kind = kC132Child;
        return !unnamed;
    }
    uint32_t id = 0;
    if (!c132_runtime_id(actor, id)) {
        return false;
    }
    snprintf(out, cap, "#%u", id);
    kind = kC132Runtime;
    return true;
}

ObjectName c132_level_name(const void* level) {
    return object_name(read_ptr(level, kObjectOuter));
}

void c132_add_entry(C132Digest& d, const C132Entry& e) {
    if (d.n >= kC132Entries) {
        ++d.dropped;
        return;
    }
    d.e[d.n++] = e;
}

// Card 140 run 13 diag: an attached root whose stepped location changed since the last digest, logged on both sides
// with its parent, the parent's owner and class, and its relative location, so an attach offset between host and
// client (STAISpawner_5 and three more, exactly (-200, -200, 0) in one round) names its parent and rule. Keyed by
// the entry key; a full table logs nothing more. Card 164: returns kC140NotAttached, kC140Attached (first sighting,
// unchanged, or no free slot) or kC140Moved (the stepped world location changed since the last round).
constexpr int32_t kC140AttachSlots = 4096;
constexpr uint32_t kC140AttachLines = 160;
struct C140AttachSeen {
    uint32_t key;
    int16_t loc[3];
};
C140AttachSeen g_c140_attach[kC140AttachSlots];
uint32_t g_c140_attach_lines = 0;

constexpr int32_t kC140NotAttached = 0;
constexpr int32_t kC140Attached = 1;
constexpr int32_t kC140Moved = 2;

int32_t c140_attach_note(uint32_t key, const char* name, const int16_t loc[3], const void* actor) {
    const void* const root = read_ptr(actor, kActorRootComponent);
    const void* const parent = c140_attach_parent(root);
    if (parent == nullptr || key == 0) {
        return kC140NotAttached;
    }
    C140AttachSeen* slot = nullptr;
    for (int32_t i = 0; i < kC140AttachSlots; ++i) {
        C140AttachSeen& s = g_c140_attach[(key + static_cast<uint32_t>(i)) % kC140AttachSlots];
        if (s.key == key || s.key == 0) {
            slot = &s;
            break;
        }
    }
    if (slot == nullptr) {
        return kC140Attached;
    }
    const bool fresh = slot->key == 0;
    const bool moved = fresh || memcmp(slot->loc, loc, sizeof(slot->loc)) != 0;
    slot->key = key;
    memcpy(slot->loc, loc, sizeof(slot->loc));
    if (fresh || !moved) {
        return kC140Attached; // the first sighting is the baseline; only a change is logged
    }
    if (!note_event_cap(g_c140_attach_lines, kC140AttachLines, "card140 attach lines")) {
        return kC140Moved;
    }
    const void* const owner = read_ptr(parent, kComponentOwner);
    const Vec3 rel = read_vec3(root, 0x140); // USceneComponent::RelativeLocation (card034 reflected)
    const Vec3 pat = read_vec3(parent, kComponentTranslation);
    const Vec3 at = read_vec3(root, kComponentTranslation);
    log::line("card140: %s attached %s at (%.0f, %.0f, %.0f): parent %s of %s (%s) at (%.0f, %.0f, %.0f), relative "
              "(%.0f, %.0f, %.0f)", g_client_session ? "client" : "host", name, at.x, at.y, at.z,
              object_name(parent).text, owner != nullptr ? object_name(owner).text : "?",
              owner != nullptr ? class_name(owner).text : "?", pat.x, pat.y, pat.z, rel.x, rel.y, rel.z);
    return kC140Moved;
}

// Card 164: the kC132ServerOnly row of a class name, or -1.
int32_t c132_server_only(const char* cls) {
    for (int32_t i = 0; i < kC132ServerOnlyN; ++i) {
        if (strcmp(cls, kC132ServerOnly[i]) == 0) {
            return i;
        }
    }
    return -1;
}

// Card 164: the entry location of `anchor`. An attached root stores its RelativeLocation (card034 reflected, +0x140),
// so an actor riding a moving parent (a troop dropship's spawner, blocker or spawn point) compares by its offset on
// the parent, not by where each side's copy of the parent is at the snapshot. Otherwise the stepped world location.
void c132_entry_loc(const void* anchor, const int16_t world_loc[3], int16_t out[3]) {
    const void* const root = read_ptr(anchor, kActorRootComponent);
    if (root == nullptr || c140_attach_parent(root) == nullptr) {
        memcpy(out, world_loc, 3 * sizeof(int16_t));
        return;
    }
    const Vec3 rel = read_vec3(root, 0x140); // USceneComponent::RelativeLocation
    out[0] = c132_step(rel.x);
    out[1] = c132_step(rel.y);
    out[2] = c132_step(rel.z);
}

// Card 164: one line per side and round, the actors left out per kC132ServerOnly row.
void c132_server_only_line(const C132Digest& d, const char* side, uint32_t round) {
    char text[512];
    size_t at = 0;
    uint32_t total = 0;
    text[0] = '\0';
    for (int32_t i = 0; i < kC132ServerOnlyN && at < sizeof(text); ++i) {
        const int w = snprintf(text + at, sizeof(text) - at, " %s:%u", kC132ServerOnly[i], d.server_only[i]);
        at += w > 0 ? static_cast<size_t>(w) : 0;
        total += d.server_only[i];
    }
    log::line("card164: %s round %u left out %u actors of %d server-only classes:%s; attached %u, moved since the last "
              "round %u", side, round, total, kC132ServerOnlyN, text, d.attached, d.attached_moved);
}

// Card 386 step 1 (diagnostic): the time of one c132_build (QueryPerformanceCounter, always) and, with
// UBWCOOP_C386=1, its split over the per actor phases in rdtsc ticks, printed as shares of their sum.
enum C386Phase : int32_t { kC386Name, kC386Class, kC386Key, kC386State, kC386Ext, kC386Attach, kC386Tail, kC386Sort, kC386N };
const char* const kC386PhaseName[kC386N] = {"name", "class", "key", "state", "ext", "attach", "tail", "sort"};
struct C386 {
    uint64_t ticks[kC386N];
    uint32_t actors;
    int32_t on; // -1: the variable is not read yet
};
C386 g_c386{{}, 0, -1};

bool c386_on() {
    if (g_c386.on < 0) {
        wchar_t buf[4];
        g_c386.on = GetEnvironmentVariableW(L"UBWCOOP_C386", buf, 4) == 1 && buf[0] == L'1' ? 1 : 0;
    }
    return g_c386.on == 1;
}

// Adds the ticks since `t` to `phase` and restarts `t`; nothing with the split off (t == 0).
void c386_lap(uint64_t& t, C386Phase phase) {
    if (t == 0) {
        return;
    }
    const uint64_t now = __rdtsc();
    g_c386.ticks[phase] += now - t;
    t = now;
}

void c386_line(const C132Digest& d, int64_t qpc_ticks) {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    const double ms = static_cast<double>(qpc_ticks) * 1000.0 / static_cast<double>(f.QuadPart);
    char split[256];
    size_t at = 0;
    uint64_t sum = 0;
    split[0] = '\0';
    for (int32_t i = 0; i < kC386N; ++i) {
        sum += g_c386.ticks[i];
    }
    for (int32_t i = 0; sum != 0 && i < kC386N && at < sizeof(split); ++i) {
        const int w = snprintf(split + at, sizeof(split) - at, " %s %.1f%%", kC386PhaseName[i],
                               100.0 * static_cast<double>(g_c386.ticks[i]) / static_cast<double>(sum));
        at += w > 0 ? static_cast<size_t>(w) : 0;
    }
    log::line("card386: %s t=%llu digest build %.2f ms, %u actors walked, %d entries, %d classes;%s", side_text(),
              static_cast<unsigned long long>(GetTickCount64()), ms, g_c386.actors, d.n, d.cls.n,
              sum != 0 ? split : " split off");
}

void c132_add_actor(C132Digest& d, const void* actor, int32_t level, uint32_t lvl) {
    uint64_t t = c386_on() ? __rdtsc() : 0;
    ++g_c386.actors;
    char name[kC132NameMax + 1];
    const void* anchor = nullptr;
    uint8_t kind = 0;
    bool unnamed = false;
    if (!c132_actor_name(actor, name, sizeof(name), anchor, kind, unnamed)) {
        d.unnamed += unnamed ? 1u : 0u;
        c386_lap(t, kC386Name);
        return;
    }
    c386_lap(t, kC386Name);
    const ObjectName cname = class_name(actor);
    const int32_t skip = c132_server_only(cname.text);
    if (skip >= 0) {
        ++d.server_only[skip];
        c386_lap(t, kC386Class);
        return;
    }
    const int32_t cls = c132_name_index(d.cls, cname.text, kC132DigestClasses);
    c386_lap(t, kC386Class);
    if (cls < 0) {
        return;
    }
    const Vec3 at = actor_location(anchor);
    const int16_t world_loc[3] = {c132_step(at.x), c132_step(at.y), c132_step(at.z)};
    C132Entry e{};
    e.key = c132_fnv(name);
    e.lvl = lvl;
    e.name = c132_pool_add(d, name);
    e.cls = static_cast<uint16_t>(cls);
    e.level = static_cast<uint8_t>(level);
    c386_lap(t, kC386Key);
    e.state = static_cast<uint8_t>(c132_actor_state(actor) | kind);
    c386_lap(t, kC386State);
    e.ext_row = c132_actor_ext(actor, e.ext);
    c386_lap(t, kC386Ext);
    c132_entry_loc(anchor, world_loc, e.loc);
    d.children += kind == kC132Child ? 1u : 0u;
    const int32_t att = c140_attach_note(e.key, name, world_loc, actor); // the world location: a moving parent is logged
    d.attached += att != kC140NotAttached ? 1u : 0u;
    d.attached_moved += att == kC140Moved ? 1u : 0u;
    c132_add_entry(d, e);
    c386_lap(t, kC386Attach);
}

void c132_add_objectives(C132Digest& d, const void* world) {
    void* const subsystem = g_c49_get_subsystem != nullptr ? g_c49_get_subsystem(const_cast<void*>(world)) : nullptr;
    SparseView v{};
    if (subsystem == nullptr || !c49_objectives(subsystem, v)) {
        return;
    }
    for (int32_t i = 0; i < v.count; ++i) {
        const uint8_t* const o = sparse_allocated(v, i) ? static_cast<const uint8_t*>(read_ptr(
                                     v.data + static_cast<size_t>(i) * kObjectiveElementBytes, kObjectiveElementValue))
                                                        : nullptr;
        const int32_t cls = o != nullptr ? c132_name_index(d.cls, class_name(o).text, kC132DigestClasses) : -1;
        if (cls < 0) {
            continue;
        }
        char text[kNameChars + 2];
        snprintf(text, sizeof(text), "O:%s", fname_text(o + kObjectiveId).text);
        C132Entry e{};
        e.key = c132_fnv(text);
        e.name = c132_pool_add(d, text);
        e.cls = static_cast<uint16_t>(cls);
        e.level = kC132GlobalLevel;
        e.state = static_cast<uint8_t>((o[kObjectiveComplete] != 0 ? kC132Complete : 0u) |
                                       (o[kObjectiveMarker] != 0 ? kC132Marker : 0u) |
                                       (o[kObjectiveInList] != 0 ? kC132InList : 0u));
        e.ext_row = kC132NoExt;
        c132_add_entry(d, e);
    }
}

// Card 327: one global entry "P:<slot>" per player that is down (Downed, Out or Dead), on both sides, from the card
// 043 table (the host's own entries, the client's from each Down frame; an absent entry is Up on both). The ext is
// the life state; the bleedout seconds stay out (they tick between the two reads). The ledger keys each Down frame
// "P:<slot>" (c132l_player), so an entry with a Down in flight is excluded. PW (inventory) is not an entry: the client
// does not know its own host slot, so the host sends each PC its own hash in that PC's B frame (c132_pw_compare).
void c132_add_players(C132Digest& d) {
    const int32_t cls = c132_name_index(d.cls, "Player", kC132DigestClasses);
    for (int32_t i = 0; cls >= 0 && i < g_c43.count && i < kC43Max; ++i) {
        const C43Entry& p = g_c43.e[i];
        if (!c43_down_state(p.state)) {
            continue;
        }
        char text[16];
        snprintf(text, sizeof(text), "P:%u", p.slot);
        C132Entry e{};
        e.key = c132_fnv(text);
        e.name = c132_pool_add(d, text);
        e.cls = static_cast<uint16_t>(cls);
        e.level = kC132GlobalLevel;
        e.ext = static_cast<uint16_t>(p.state);
        e.ext_row = kC132NoExt;
        c132_add_entry(d, e);
    }
}

// Card 475: one global entry "Score:" with this side's FedScore total: the low 16 bits in the ext (compared exactly,
// printed in hex on a mismatch), the high 16 bits in loc[0] (a gap of a whole multiple of 65536 only shows there).
// Each side adds its own score events, so live drift between the two totals is a mismatch.
void c132_add_score(C132Digest& d, const void* world) {
    const int32_t total = c475_digest_total(world);
    const int32_t cls = total >= 0 ? c132_name_index(d.cls, "Score", kC132DigestClasses) : -1;
    if (cls < 0) {
        return;
    }
    const char text[] = "Score:";
    C132Entry e{};
    e.key = c132_fnv(text);
    e.name = c132_pool_add(d, text);
    e.cls = static_cast<uint16_t>(cls);
    e.level = kC132GlobalLevel;
    e.ext = static_cast<uint16_t>(static_cast<uint32_t>(total) & 0xFFFFu);
    e.ext_row = kC132NoExt;
    e.loc[0] = static_cast<int16_t>(static_cast<uint32_t>(total) >> 16);
    c132_add_entry(d, e);
}

// Card 323: one global entry per (kind, value) with its value in the ext, compared exactly. `cls` must be valid.
void c323_add_global(C132Digest& d, int32_t cls, const char* text, uint16_t ext) {
    C132Entry e{};
    e.key = c132_fnv(text);
    e.name = c132_pool_add(d, text);
    e.cls = static_cast<uint16_t>(cls);
    e.level = kC132GlobalLevel;
    e.ext = ext;
    e.ext_row = kC132NoExt;
    c132_add_entry(d, e);
}

uint16_t c323_whole(float v) {
    return static_cast<uint16_t>(v <= 0.0f ? 0.0f : (v >= 65535.0f ? 65535.0f : v + 0.5f));
}

// Card 323 (senior note 2026-11-17): the weapon a player pawn visibly holds. The client copy of the host's pawn keeps
// only Unarmed in its weapon inventory (GiveMorita runs host-own, card 463) yet shows the MoritaMk1 in the receiver
// shot (c448b202a), so the inventory's equipped weapon is not what a player sees. Read instead the weapon actors whose
// components hang in the pawn's attach tree (reflected USceneComponent::AttachChildren, breadth first from
// RootComponent, kC323Tree components at most), and take the first one with bVisible set: card 033 hides a remote
// pawn's weapon with bHiddenInGame on each machine, so bHiddenInGame is not compared. Null with no visible weapon.
// Each new (pawn, weapon component) pair logs one row, kC323WeaponRows per session, so a run shows which weapon
// actors each pawn's tree holds on each side as they change (c448b203a: a call cap spent its rows before GiveMorita),
// with each component's mesh asset and whether its weapon is the inventory's equipped one (b205).
// c448b204a: bVisible was 1 on every weapon mesh, the Unarmed one too, and the card 050 apply's removed weapons stay
// attached (RemoveWeaponByInstance does not destroy them), so a tree walk alone cannot tell the drawn weapon.
constexpr int32_t kC323Tree = 64;
constexpr uint32_t kC323WeaponRows = 64;
constexpr const char* kC323Visible[] = {"bVisible", nullptr};
constexpr const char* kC323MeshProps[] = {"SkeletalMeshAsset", "SkinnedAsset", "SkeletalMesh", "StaticMesh"};
struct C323Pair {
    int32_t pawn;
    int32_t comp;
};
C323Pair g_c323_pairs[kC323WeaponRows] = {};
uint32_t g_c323_weapon_rows = 0;

// True (and the pair kept) the first time this pawn holds this weapon component in its tree, while rows are left.
bool c323_new_pair(const void* pawn, const void* comp) {
    const C323Pair p{object_index(pawn), object_index(comp)};
    for (uint32_t i = 0; i < g_c323_weapon_rows; ++i) {
        if (g_c323_pairs[i].pawn == p.pawn && g_c323_pairs[i].comp == p.comp) {
            return false;
        }
    }
    if (g_c323_weapon_rows >= kC323WeaponRows) {
        return false;
    }
    g_c323_pairs[g_c323_weapon_rows++] = p;
    return true;
}

// The mesh asset a mesh component draws (its first set reflected mesh property), or null.
const void* c323_mesh_asset(const void* comp) {
    const void* const cls = read_ptr(comp, kObjectClass);
    for (const char* const name : kC323MeshProps) {
        const uint8_t* const prop = prop_find_prop(cls, name);
        const void* const asset = prop != nullptr ? read_ptr(comp, static_cast<size_t>(read_i32(prop, kPropertyOffset)))
                                                  : nullptr;
        if (asset != nullptr) {
            return asset;
        }
    }
    return nullptr;
}

// The drawn weapon mesh: the mesh asset of the first visible component in the pawn's attach tree (breadth first)
// whose owner is the pawn's equipped weapon (inventory), or null. Logs each new (pawn, weapon component) pair.
// b255 (c245b254a round 28, senior): a remote player's mech has no equipped pointer on a client (all "not equipped")
// while both sides draw SKM_RazrobackM7_CockpitWeapons. With a null equipped pointer, the key is the first visible
// weapon mesh in the tree, so a real visible difference still mismatches.
const void* c323_drawn_mesh(const void* pawn) {
    const void* const weapon_cls = g_c50_weapon_class != nullptr ? g_c50_weapon_class() : nullptr;
    void* const inventory = read_ptr(pawn, kMiWeaponInventory);
    const void* const equipped = inventory != nullptr ? g_get_equipped_weapon(inventory) : nullptr;
    const void* queue[kC323Tree] = {read_ptr(pawn, kActorRootComponent)};
    int32_t tail = queue[0] != nullptr ? 1 : 0;
    const void* found = nullptr;
    const void* first = nullptr; // the first visible weapon mesh, any owner weapon
    for (int32_t head = 0; weapon_cls != nullptr && head < tail; ++head) {
        const void* const comp = queue[head];
        const uint8_t* const prop = prop_find_prop(read_ptr(comp, kObjectClass), "AttachChildren");
        const size_t off = prop != nullptr ? static_cast<size_t>(read_i32(prop, kPropertyOffset)) : 0;
        const auto* const kids = prop != nullptr ? static_cast<void* const*>(read_ptr(comp, off)) : nullptr;
        const int32_t num = kids != nullptr ? read_i32(comp, off + kArrayNum) : 0;
        for (int32_t i = 0; i < num && tail < kC323Tree; ++i) {
            if (kids[i] != nullptr) {
                queue[tail++] = kids[i];
            }
        }
        const void* const owner = read_ptr(comp, kComponentOwner);
        if (owner == pawn || owner == nullptr || !is_a(owner, weapon_cls)) {
            continue;
        }
        const int visible = c29_bool(comp, kC323Visible);
        const void* const mesh = c323_mesh_asset(comp);
        if (c323_new_pair(pawn, comp)) {
            log::line("card323: %s pawn %s guid %llx tree weapon %s (%s, %s) component %s (%s) mesh %s, bVisible %d, "
                      "tree slot %d",
                      side(), object_name(pawn).text, static_cast<unsigned long long>(c41_guid(pawn)),
                      object_name(owner).text, class_name(owner).text, owner == equipped ? "equipped" : "not equipped",
                      object_name(comp).text, class_name(comp).text, mesh != nullptr ? object_name(mesh).text : "none",
                      visible, head);
        }
        if (found == nullptr && owner == equipped && visible == 1 && mesh != nullptr) {
            found = mesh;
        }
        if (first == nullptr && visible == 1 && mesh != nullptr) {
            first = mesh;
        }
    }
    return equipped != nullptr ? found : first;
}

// Card 323 digest kinds, log only. Each player soldier (the MI pawn class, both sides) gives three entries keyed by
// its NetGUID, which the host assigns and the client learns, so both sides key one pawn alike: "Pl:<guid>:hp" (Health,
// whole points), "Pl:<guid>:ar" (Armour, whole points) and "Pl:<guid>:wp" (the low 16 bits of the FNV-1a of the
// drawn weapon mesh's asset name, c323_drawn_mesh, 0 with none). A pawn with no NetGUID yet is left out.
// Downed stays in "P:<slot>" (card 327). Seat is not an entry yet: the client of a mech or turret a remote player sits in has no controller to
// read (controllers do not replicate to other clients), and no replicated occupant field is known (card 323).
// Host, card 323 b213: whether a remote PC's connection has no actor channel for the player pawn. Past the net cull
// distance (15000 uu) the engine closes the channel and the client destroys its copy (card098 "client destroy
// BP_PlayerCharacterMI_C ... net cull" row); c245b212a/c244b212a had 3 host-only PlayerPawn entries per round for the
// host's pawn about 15500 uu off the client. Such a pawn is left out of the digest, the same as every culled NPC (the
// digest names placed actors only). One digest goes to every client, so a pawn culled for any client is left out.
// Always false on a client: its only PC is its own.
bool c323_pawn_culled(const void* world, const void* pawn) {
    for (int s = 0; s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(world, s);
        if (pc != nullptr && c26_is_remote_pc(pc) && !c434_has_channel(pc, pawn)) {
            return true;
        }
    }
    return false;
}

void c323_add_player_pawn(C132Digest& d, const void* world, const void* actor) {
    if (g_player_mi_class == nullptr || !is_a(actor, g_player_mi_class())) {
        return;
    }
    // c245b196a: 3 host-only MI pawns per round (9 presence mismatches) without a player behind them.
    // A pawn counts only while a PlayerState is set: replicated to everyone, cleared on unpossess.
    if (read_ptr(actor, kPawnPlayerState) == nullptr) {
        return;
    }
    if (c323_pawn_culled(world, actor)) {
        static uint32_t lines = 0; // game thread; session total
        if (++lines <= 16) {
            log::line("card323: %s t=%llu digest leaves out player pawn %s guid %llx: a client has no channel for it "
                      "(net culled)",
                      side_text(), static_cast<unsigned long long>(GetTickCount64()), object_name(actor).text,
                      static_cast<unsigned long long>(c41_guid(actor)));
        }
        return;
    }
    const uint64_t guid = c41_guid(actor);
    const int32_t cls = guid != 0 ? c132_name_index(d.cls, "PlayerPawn", kC132DigestClasses) : -1;
    if (cls < 0) {
        return;
    }
    void* component = nullptr;
    const float health = c21_health(static_cast<const uint8_t*>(actor), &component);
    const float armour = component != nullptr ? c48_armour(component).armour : -1.0f;
    const void* const mesh = c323_drawn_mesh(actor); // senior note: key on the drawn mesh
    char text[48];
    snprintf(text, sizeof(text), "Pl:%llx:hp", static_cast<unsigned long long>(guid));
    c323_add_global(d, cls, text, c323_whole(health));
    snprintf(text, sizeof(text), "Pl:%llx:ar", static_cast<unsigned long long>(guid));
    c323_add_global(d, cls, text, c323_whole(armour));
    snprintf(text, sizeof(text), "Pl:%llx:wp", static_cast<unsigned long long>(guid));
    c323_add_global(d, cls, text,
                    mesh != nullptr ? static_cast<uint16_t>(c132_fnv(object_name(mesh).text) & 0xFFFFu) : 0);
}

// Card 323: one global entry "Lv:<level>" per level in the world's Levels (a streaming level joins that array when it
// is made visible and leaves it when hidden), so a level shown on one side only is a presence mismatch. Actor entries
// of a level the other side lacks are skipped (c132_host_level_here), which left that gap silent before.
void c323_add_level(C132Digest& d, const char* level) {
    const int32_t cls = c132_name_index(d.cls, "Level", kC132DigestClasses);
    if (cls < 0) {
        return;
    }
    char text[kNameChars + 4];
    snprintf(text, sizeof(text), "Lv:%s", level);
    c323_add_global(d, cls, text, 1);
}

int c132_entry_cmp(const void* a, const void* b) {
    const C132Entry& x = *static_cast<const C132Entry*>(a);
    const C132Entry& y = *static_cast<const C132Entry*>(b);
    if (x.key != y.key) {
        return x.key < y.key ? -1 : 1;
    }
    return x.lvl < y.lvl ? -1 : (x.lvl > y.lvl ? 1 : 0);
}

// The digest of `world` on this side, sorted by key and level.
void c132_build(C132Digest& d, const void* world) {
    LARGE_INTEGER q0;
    QueryPerformanceCounter(&q0);
    memset(g_c386.ticks, 0, sizeof(g_c386.ticks));
    g_c386.actors = 0;
    d.n = 0;
    d.dropped = 0;
    d.unnamed = 0;
    d.children = 0;
    memset(d.server_only, 0, sizeof(d.server_only));
    d.attached = 0;
    d.attached_moved = 0;
    d.pool[0] = '\0';
    d.pool_n = 1; // offset 0 is the empty name
    d.cls.n = 0;
    d.cls.dropped = 0;
    d.lvl.n = 0;
    d.lvl.dropped = 0;
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    for (int32_t l = 0; levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const auto* const actors =
            levels[l] != nullptr ? static_cast<void* const*>(read_ptr(levels[l], kLevelActors)) : nullptr;
        const int32_t num = levels[l] != nullptr ? read_i32(levels[l], kLevelActors + kArrayNum) : 0;
        const ObjectName lname = levels[l] != nullptr ? c132_level_name(levels[l]) : ObjectName{};
        const int32_t level = actors != nullptr ? c132_name_index(d.lvl, lname.text, kC132DigestLevels) : -1;
        const uint32_t lvl = c132_fnv(lname.text);
        if (level >= 0) {
            c323_add_level(d, lname.text);
        }
        for (int32_t a = 0; level >= 0 && a < num && a < kMaxLevelActors; ++a) {
            if (actors[a] != nullptr) {
                c132_add_actor(d, actors[a], level, lvl);
                c323_add_player_pawn(d, world, actors[a]);
            }
        }
    }
    uint64_t t = c386_on() ? __rdtsc() : 0;
    c132_add_objectives(d, world);
    c132_add_players(d);
    c132_add_score(d, world);
    c386_lap(t, kC386Tail);
    qsort(d.e, static_cast<size_t>(d.n), sizeof(C132Entry), c132_entry_cmp);
    c386_lap(t, kC386Sort);
    LARGE_INTEGER q1;
    QueryPerformanceCounter(&q1);
    c386_line(d, q1.QuadPart - q0.QuadPart);
}

// --- Host: the round, sent over ticks ---

int32_t c132_send(const void* world, const char* text) {
    const C95Fanout f = c95_send_all(world, ChanOp::Digest, text);
    ++g_c132h.frames;
    g_c132h.sent += f.sent;
    return f.to;
}

// Appends "<idx>=<name>;" items of `t` from cursor into one frame; returns the next cursor.
int32_t c132_names_frame(const C132Names& t, char tag, int32_t from, char* out, size_t cap) {
    size_t at = static_cast<size_t>(snprintf(out, cap, "%c", tag));
    int32_t i = from;
    for (; i < t.n; ++i) {
        char item[kNameChars + 8];
        const int w = snprintf(item, sizeof(item), "%x=%s;", i, t.name[i]);
        if (w <= 0 || at + static_cast<size_t>(w) >= cap) {
            break;
        }
        memcpy(out + at, item, static_cast<size_t>(w) + 1);
        at += static_cast<size_t>(w);
    }
    return i;
}

int32_t c132_slot(uint8_t level, uint32_t key) {
    return static_cast<int32_t>(level) * kC132Buckets + static_cast<int32_t>(key >> (32 - kC132BucketBits));
}

bool c132_slot_set(const uint64_t* bits, int32_t slot) {
    return (bits[slot / 64] >> (slot % 64) & 1u) != 0;
}

uint32_t c132_mix(uint32_t h, uint32_t w) {
    return (h ^ w) * 16777619u;
}

// One entry's hash: every field the entry compare reads (the level is the slot's).
uint32_t c132_entry_hash(const C132Entry& e, uint32_t cls_hash) {
    uint32_t h = c132_mix(2166136261u, e.key);
    h = c132_mix(h, cls_hash);
    h = c132_mix(h, static_cast<uint32_t>(e.state) | static_cast<uint32_t>(e.ext) << 8);
    h = c132_mix(h, static_cast<uint32_t>(static_cast<uint16_t>(e.loc[0])) |
                        static_cast<uint32_t>(static_cast<uint16_t>(e.loc[1])) << 16);
    h = c132_mix(h, static_cast<uint32_t>(static_cast<uint16_t>(e.loc[2])));
    h ^= h >> 16; // murmur3 fmix32: the slot hash is a sum, so spread every input bit
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    return h ^ (h >> 16);
}

// Card 341, client: the host's steps for an entry the compare accepted inside the tolerance but not equal. The
// client's slot sum uses them while its own steps stay inside the tolerance of them, so the slot's sums are equal
// from the next round and the pair costs one detail round, not one per round.
struct C341Adopt {
    uint32_t key, lvl; // key 0 = free
    int16_t loc[3];
};
C341Adopt g_c341_adopt[kC341Adopt] = {};
uint32_t g_c341_adopt_n = 0; // rows in use; 0 skips the lookup

void c341_adopt_set(const C132Entry& h, const C132Entry& c) {
    C341Adopt* row = nullptr;
    for (uint32_t i = 0; i < kC341Adopt; ++i) {
        C341Adopt& a = g_c341_adopt[i];
        if (a.key == c.key && a.lvl == c.lvl) {
            row = &a;
            break;
        }
        row = row == nullptr && a.key == 0 ? &a : row;
    }
    if (row == nullptr) {
        static uint32_t warned = 0;
        if (note_event_cap(warned, 4, "card341 table")) {
            log::line("WARN card341: adopt table full (%u), key %08x pays the detail round each round", kC341Adopt,
                      c.key);
        }
        return;
    }
    g_c341_adopt_n += row->key == 0 ? 1 : 0;
    row->key = c.key;
    row->lvl = c.lvl;
    memcpy(row->loc, h.loc, sizeof(row->loc));
}

// `e` with the adopted steps when it has a row and is still inside the tolerance of it; a row it left is freed.
C132Entry c341_adopted(const C132Entry& e) {
    C132Entry out = e;
    for (uint32_t i = 0; i < kC341Adopt; ++i) {
        C341Adopt& a = g_c341_adopt[i];
        if (a.key != e.key || a.lvl != e.lvl || a.key == 0) {
            continue;
        }
        int32_t far_off = 0;
        for (int32_t k = 0; k < 3; ++k) {
            far_off |= abs(a.loc[k] - e.loc[k]) > kC132LocTolerance ? 1 : 0;
        }
        if (far_off != 0) {
            a = C341Adopt{};
            --g_c341_adopt_n;
        } else {
            memcpy(out.loc, a.loc, sizeof(out.loc));
        }
        break;
    }
    return out;
}

// The slot sums of `d`. level_to maps d's level index to the slot's (the host's), -1 for a level left out; nullptr
// keeps d's own. Returns the entries left out.
uint32_t c132_sum(const C132Digest& d, const int32_t* level_to, C132Sums& s) {
    memset(&s, 0, sizeof(s));
    uint32_t cls_hash[kC132DigestClasses];
    for (int32_t i = 0; i < d.cls.n; ++i) {
        cls_hash[i] = c132_fnv(d.cls.name[i]);
    }
    uint32_t left_out = 0;
    for (int32_t i = 0; i < d.n; ++i) {
        const C132Entry& e = d.e[i];
        const int32_t level = e.level == kC132GlobalLevel || level_to == nullptr ? e.level : level_to[e.level];
        if (level < 0 || e.cls >= d.cls.n) {
            ++left_out;
            continue;
        }
        const int32_t slot = c132_slot(static_cast<uint8_t>(level), e.key);
        ++s.n[slot];
        s.hash[slot] += c132_entry_hash(g_c341_adopt_n != 0 ? c341_adopted(e) : e, cls_hash[e.cls]);
    }
    return left_out;
}

// Card 329: the hash of the level and class tables, in order (the host's T; the client checks its kept tables).
uint32_t c132_tables_hash(const C132Digest& d) {
    uint32_t h = c132_mix(2166136261u, static_cast<uint32_t>(d.lvl.n) | static_cast<uint32_t>(d.cls.n) << 16);
    for (int32_t i = 0; i < d.lvl.n; ++i) {
        h = c132_mix(h, c132_fnv(d.lvl.name[i]));
    }
    for (int32_t i = 0; i < d.cls.n; ++i) {
        h = c132_mix(h, c132_fnv(d.cls.name[i]));
    }
    return h;
}

// Card 329: the whole sum, every non-empty slot's index, count and hash (the host's W; the client's own the same).
uint32_t c132_whole(const C132Sums& s) {
    uint32_t h = 2166136261u;
    for (int32_t slot = 0; slot < kC132Slots; ++slot) {
        if (s.n[slot] != 0) {
            h = c132_mix(c132_mix(c132_mix(h, static_cast<uint32_t>(slot)), s.n[slot]), s.hash[slot]);
        }
    }
    return h;
}

// Appends "<slot4><count4><hash8>" items of the non-empty slots from cursor into one frame; returns the next cursor.
int32_t c132_sums_frame(const C132Sums& s, int32_t from, char* out, size_t cap) {
    size_t at = 1;
    out[0] = 'H';
    out[1] = '\0';
    int32_t i = from;
    for (; i < kC132Slots; ++i) {
        if (s.n[i] == 0) {
            continue;
        }
        if (at + kC132SlotChars >= cap) {
            break;
        }
        snprintf(out + at, cap - at, "%04x%04x%08x", static_cast<uint32_t>(i), s.n[i], s.hash[i]);
        at += kC132SlotChars;
    }
    return i;
}

// Appends the entries of the asked slots from cursor into one frame; returns the next cursor.
int32_t c132_entries_frame(const C132Digest& d, const uint64_t* slots, int32_t from, char* out, size_t cap) {
    size_t at = 1;
    out[0] = 'P';
    out[1] = '\0';
    int32_t i = from;
    for (; i < d.n; ++i) {
        const C132Entry& e = d.e[i];
        if (!c132_slot_set(slots, c132_slot(e.level, e.key))) {
            continue;
        }
        char item[kC132EntryChars + 1];
        const int w = snprintf(item, sizeof(item), "%08x%02x%03x%02x%04x%04x%04x%04x", e.key, e.level, e.cls, e.state,
                               e.ext, static_cast<uint16_t>(e.loc[0]), static_cast<uint16_t>(e.loc[1]),
                               static_cast<uint16_t>(e.loc[2]));
        if (w != kC132EntryChars || at + static_cast<size_t>(w) >= cap) {
            break;
        }
        memcpy(out + at, item, static_cast<size_t>(w) + 1);
        at += static_cast<size_t>(w);
    }
    return i;
}

// `pc` when it is still a remote PC in a player slot of `world`, else nullptr (it left since it asked).
void* c132_live_pc(const void* world, void* pc) {
    for (int s = 0; pc != nullptr && s < kC17Slots; ++s) {
        if (g_get_player_controller(world, s) == pc) {
            return c26_is_remote_pc(pc) ? pc : nullptr;
        }
    }
    return nullptr;
}

bool c132_send_one(void* pc, const char* text) {
    static wchar_t wire[kChanTextFrame];
    const WireString w = chan_text_frame(ChanOp::Digest, text, wire);
    return w.num > 0 && chan_send_client(pc, w);
}

// Card 329: whether `pc` needs this round's tables (the T last sent it is not this round's); records T as sent.
// A PC not in the record takes the slot of one no longer live.
bool c132_tables_due(const void* world, void* pc) {
    C132Host& h = g_c132h;
    int32_t at = -1;
    for (int32_t i = 0; i < kC17Slots && at < 0; ++i) {
        at = h.tables_pc[i] == pc ? i : -1;
    }
    if (at >= 0 && h.tables_sent[at] == h.tables) {
        return false;
    }
    for (int32_t i = 0; i < kC17Slots && at < 0; ++i) {
        at = c132_live_pc(world, h.tables_pc[i]) == nullptr ? i : -1;
    }
    if (at >= 0) {
        h.tables_pc[at] = pc;
        h.tables_sent[at] = h.tables;
    }
    return true; // with no record slot it gets the tables every round
}

// Card 329, host: a client's DigestQuery "T": its tables did not match T; the next round sends it the tables.
void c132_host_tables_ask(void* pc, const char* p) {
    C132Host& h = g_c132h;
    for (int32_t i = 0; i < kC17Slots; ++i) {
        h.tables_pc[i] = h.tables_pc[i] == pc ? nullptr : h.tables_pc[i];
    }
    log::line("WARN card329: host t=%llu digest round %s: %s tables did not match, sent again next round",
              static_cast<unsigned long long>(GetTickCount64()), p, object_name(pc).text);
}

// The B frame to each remote PC that has sent its first Pong (c95_send_all's set), each with S, the last ledger seq
// sent to it before this frame, (card 329) T, whether the tables follow, and W, and (card 327) that PC's own
// inventory hash PW (kC327NoHash: none). Returns the PCs it went to.
int32_t c132_send_begin(const void* world) {
    C132Host& h = g_c132h;
    int32_t to = 0;
    h.tables_to_n = 0;
    for (int s = 0; s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(world, s);
        const C50Copy* const c = pc != nullptr && c26_is_remote_pc(pc) ? c50_copy(object_index(pc), false) : nullptr;
        if (c == nullptr || !c->ponged) {
            continue;
        }
        ue::chan::chan_flush_client(pc); // card 151: this tick's rows get their seqs first, so S counts them
        const uint32_t seq = c132_ledger_sent_seq(pc);
        const bool tables = c132_tables_due(world, pc);
        if (tables) {
            h.tables_to[h.tables_to_n++] = pc; // at most kC17Slots: one per slot
        }
        char text[kChanTextMax + 1];
        snprintf(text, sizeof(text), "B%x %x %x %x %x %x %d %x %x %s", h.round, h.d.n, h.d.cls.n, h.d.lvl.n, seq,
                 h.tables, tables ? 1 : 0, h.whole, c327_host_hash(pc), h.why);
        static wchar_t wire[kChanTextFrame];
        const WireString w = chan_text_frame(ChanOp::Digest, text, wire);
        const bool sent = w.num > 0 && chan_send_client(pc, w, true); // card 151: B begins its own batch
        ++to;
        ++h.frames;
        h.sent += sent ? 1 : 0;
        log::line("card132: host t=%llu digest round %u B to %s with S=%u, tables %s%s",
                  static_cast<unsigned long long>(GetTickCount64()), h.round, object_name(pc).text, seq,
                  tables ? "follow" : "kept", sent ? "" : ": not sent");
    }
    return to;
}

// Card 329: one V or C frame to this round's PCs that need the tables (each still live).
void c132_send_tables(const void* world, const char* text) {
    C132Host& h = g_c132h;
    for (int32_t i = 0; i < h.tables_to_n; ++i) {
        void* const pc = c132_live_pc(world, h.tables_to[i]);
        ++h.frames;
        h.sent += pc != nullptr && c132_send_one(pc, text) ? 1 : 0;
    }
}

// One frame of the round in flight.
void c132_host_frame(const void* world) {
    char text[kChanTextMax + 1];
    const size_t cap = sizeof(text);
    C132Host& h = g_c132h;
    switch (h.phase) {
    case C132Phase::Begin:
        h.to = c132_send_begin(world);
        h.phase = h.to == 0 ? C132Phase::Idle : (h.tables_to_n > 0 ? C132Phase::Levels : C132Phase::End);
        h.cursor = 0;
        return;
    case C132Phase::Levels:
    case C132Phase::Classes: {
        const bool levels = h.phase == C132Phase::Levels;
        const C132Names& t = levels ? h.d.lvl : h.d.cls;
        if (h.cursor < t.n) {
            h.cursor = c132_names_frame(t, levels ? 'V' : 'C', h.cursor, text, cap);
            c132_send_tables(world, text);
        }
        if (h.cursor >= t.n) {
            h.phase = levels ? C132Phase::Classes : C132Phase::End;
            h.cursor = 0;
        }
        return;
    }
    case C132Phase::End:
        snprintf(text, cap, "E%x", h.round);
        c132_send(world, text);
        log::line("card132: host t=%llu digest round %u (%s) sent: %d entries (%u dropped), %d classes, %d levels, "
                  "%d frames, %d sends to %d clients (tables to %d); %u child actors; dropped %u classes, %u levels, "
                  "%u unnamed",
                  static_cast<unsigned long long>(GetTickCount64()), h.round, h.why, h.d.n, h.d.dropped, h.d.cls.n,
                  h.d.lvl.n, h.frames, h.sent, h.to, h.tables_to_n, h.d.children, h.d.cls.dropped, h.d.lvl.dropped,
                  h.d.unnamed);
        c132_server_only_line(h.d, "host", h.round);
        c132_proj_round(h.round);
        c140_pose_host_check(h.round);
        c132_decal_round(h.round);
        c137_pickup_probe(world, "digest round", false); // card 137 P7: placed pickup marks at each round
        h.settle_until_ms = GetTickCount64() + kC132SettleMs; // card 232
        h.phase = C132Phase::Idle;
        return;
    case C132Phase::Idle:
        return;
    }
}

void c132_host_start(const void* world, const char* why) {
    C132Host& h = g_c132h;
    if (h.phase != C132Phase::Idle || h.details > 0) {
        log::line("WARN card132: host digest round %u (%s) still sending: %s round not started", h.round, h.why, why);
        return;
    }
    c132_build(h.d, world);
    c132_sum(h.d, nullptr, h.sums);
    h.tables = c132_tables_hash(h.d); // card 329
    h.whole = c132_whole(h.sums);
    ++h.round;
    h.why = why;
    h.frames = 0;
    h.sent = 0;
    h.to = 0;
    h.phase = C132Phase::Begin;
}

// One frame of the detail at the queue's head, to its PC only; the F frame after the last entry, then the next.
// Card 329: a sums job sends the H frames and G instead, then holds the next round kC132SettleMs for the detail ask.
void c132_detail_frame(const void* world) {
    C132Host& h = g_c132h;
    C132Detail& q = h.detail[0];
    void* const pc = c132_live_pc(world, q.pc);
    const bool stale = pc == nullptr || q.round != h.round;
    const int32_t end = q.sums ? kC132Slots : h.d.n;
    char text[kChanTextMax + 1];
    if (!stale && q.cursor < end) {
        q.cursor = q.sums ? c132_sums_frame(h.sums, q.cursor, text, sizeof(text))
                          : c132_entries_frame(h.d, q.slots, q.cursor, text, sizeof(text));
        if (text[1] != '\0') {
            q.frames += c132_send_one(pc, text) ? 1 : 0;
        }
    }
    if (!stale && q.cursor < end) {
        return;
    }
    if (!stale) {
        snprintf(text, sizeof(text), "%c%x", q.sums ? 'G' : 'F', q.round);
        q.frames += c132_send_one(pc, text) ? 1 : 0;
    }
    if (q.sums) {
        h.settle_until_ms = GetTickCount64() + kC132SettleMs;
    }
    log::line("%scard132: host t=%llu digest round %u %s to %s: %d frames%s", stale ? "WARN " : "",
              static_cast<unsigned long long>(GetTickCount64()), q.round, q.sums ? "slot sums" : "detail",
              object_name(pc).text, q.frames, stale ? " (dropped: the PC left or a new round began)" : "");
    for (int32_t i = 1; i < h.details; ++i) {
        h.detail[i - 1] = h.detail[i];
    }
    --h.details;
}

// Card 329: "<round>": a client's ask for the round's slot sums (its whole sum differs from W).
void c132_host_sums_ask(void* pc, const char* p) {
    C132Host& h = g_c132h;
    const uint32_t round = static_cast<uint32_t>(strtoul(p, nullptr, 16));
    if (round != h.round || h.details >= kC132Details) {
        log::line("WARN card329: host digest round %u slot sums ask from %s dropped (%s)", round, object_name(pc).text,
                  round != h.round ? "not the current round" : "queue full");
        return;
    }
    C132Detail& d = h.detail[h.details++];
    memset(&d, 0, sizeof(d));
    d.pc = pc;
    d.round = round;
    d.sums = true;
    log::line("card329: host t=%llu digest round %u slot sums ask from %s",
              static_cast<unsigned long long>(GetTickCount64()), round, object_name(pc).text);
}

// "<round> <slot> ...": a client's ask for the entries of the slots whose sums differ.
void c132_host_detail_ask(void* pc, const char* p) {
    C132Host& h = g_c132h;
    char* q = nullptr;
    const uint32_t round = static_cast<uint32_t>(strtoul(p, &q, 16));
    if (round != h.round || h.details >= kC132Details) {
        log::line("WARN card132: host digest round %u detail ask from %s dropped (%s)", round, object_name(pc).text,
                  round != h.round ? "not the current round" : "queue full");
        return;
    }
    C132Detail& d = h.detail[h.details];
    memset(&d, 0, sizeof(d));
    d.pc = pc;
    d.round = round;
    int32_t n = 0;
    for (; n < kC132DetailSlots && q != nullptr && *q == ' '; ++n) {
        const unsigned long slot = strtoul(q + 1, &q, 16);
        if (slot >= static_cast<unsigned long>(kC132Slots)) {
            log::line("WARN card132: host digest round %u detail ask from %s: bad slot, dropped", round,
                      object_name(pc).text);
            return;
        }
        d.slots[slot / 64] |= 1ull << (slot % 64);
    }
    ++h.details;
    log::line("card132: host t=%llu digest round %u detail ask from %s: %d slots",
              static_cast<unsigned long long>(GetTickCount64()), round, object_name(pc).text, n);
}

bool c132_host_world(const void* world) {
    // Card 349: any PC in the world will do (net mode only); not the local PC.
    void* const pc = world != nullptr && !g_client_session ? g_world_first_pc(const_cast<void*>(world)) : nullptr;
    return pc != nullptr && net_mode(pc) == kNetModeListenServer;
}

// --- Client: the compare ---

struct C132ClassCount {
    char cls[kNameChars];
    uint32_t n;
};

struct C132Compare {
    uint32_t mismatches, lines;
    C132ClassCount count[kC132ClassCounts];
    int32_t classes;
    uint32_t query[kC132QueryKeys];
    int32_t queries;
    int32_t pending[kC132QueryKeys]; // host-only entries (host digest index), judged after c132_query_own
    int32_t pendings;
    uint32_t skip_host, skip_own, excluded;
    uint32_t duplicate; // entries left out: a duplicate key (C132Skip)
};

C132Compare g_c132x{}; // game thread; static: too large for the stack

void c132_count_class(const char* cls) {
    for (int32_t i = 0; i < g_c132x.classes; ++i) {
        if (strcmp(g_c132x.count[i].cls, cls) == 0) {
            ++g_c132x.count[i].n;
            return;
        }
    }
    if (g_c132x.classes < kC132ClassCounts) {
        C132ClassCount& c = g_c132x.count[g_c132x.classes++];
        snprintf(c.cls, sizeof(c.cls), "%s", cls);
        c.n = 1;
    }
}

// Keys left out of the compare for the rest of the map: a (key, level) pair that two entries of one digest share
// (two actors of one level with one name, or an FNV collision) cannot be paired. Each is logged once per map.
// Keys excluded for a late ledger row in a row of rounds: a key whose rows never stop would never be compared, so
// the third round in a row is a WARN.
constexpr int32_t kC132Dups = 64;
constexpr int32_t kC132Streaks = 64;
constexpr uint32_t kC132StreakWarn = 3;

struct C132Dup {
    uint32_t key;
    uint32_t lvl;
};

struct C132Streak {
    uint32_t key;
    uint32_t round; // the last round it was excluded in
    uint32_t streak;
};

struct C132Skip {
    C132Dup dup[kC132Dups];
    int32_t dups;
    uint32_t dup_over; // pairs past the table this map (compared as before)
    C132Streak streak[kC132Streaks];
    uint32_t adopt[kC132Adopts]; // placed actor keys the host digests and this side's bReplicates would leave out
    int32_t adopts;
};

C132Skip g_c132s{}; // client, game thread; reset per map (c132_arm_map)

bool c132_adopted(uint32_t key) {
    for (int32_t i = 0; i < g_c132s.adopts; ++i) {
        if (g_c132s.adopt[i] == key) {
            return true;
        }
    }
    return false;
}

int32_t c132_adopt_count() {
    return g_c132s.adopts;
}

bool c132_is_dup(uint32_t key, uint32_t lvl) {
    for (int32_t i = 0; i < g_c132s.dups; ++i) {
        if (g_c132s.dup[i].key == key && g_c132s.dup[i].lvl == lvl) {
            return true;
        }
    }
    return false;
}

const char* c132_level_text(const C132Digest& d, const C132Entry& e) {
    return e.level == kC132GlobalLevel ? "-" : (e.level < d.lvl.n ? d.lvl.name[e.level] : "?");
}

// One side's sorted digest: every (key, level) two entries share, into the table and the log (the host's names are
// not sent, so a host pair is also asked about: the host logs its actors' names).
void c132_find_dups(const C132Digest& d, bool host) {
    const int32_t step = static_cast<int32_t>(kC132LocStep);
    for (int32_t i = 1; i < d.n; ++i) {
        const C132Entry& a = d.e[i - 1];
        const C132Entry& b = d.e[i];
        if (a.key != b.key || a.lvl != b.lvl || c132_is_dup(a.key, a.lvl)) {
            continue;
        }
        if (g_c132s.dups >= kC132Dups) {
            ++g_c132s.dup_over;
            continue;
        }
        g_c132s.dup[g_c132s.dups++] = C132Dup{a.key, a.lvl};
        const char* const na = host ? "?" : (a.name < d.pool_n ? d.pool + a.name : "?");
        const char* const nb = host ? "?" : (b.name < d.pool_n ? d.pool + b.name : "?");
        log::line("WARN card132: duplicate key %08x in level %s on the %s: %s (%s) at (%d,%d,%d) and %s (%s) at "
                  "(%d,%d,%d); left out of the compare this map%s",
                  a.key, c132_level_text(d, a), host ? "host" : "client", na,
                  a.cls < d.cls.n ? d.cls.name[a.cls] : "?", a.loc[0] * step, a.loc[1] * step, a.loc[2] * step, nb,
                  b.cls < d.cls.n ? d.cls.name[b.cls] : "?", b.loc[0] * step, b.loc[1] * step, b.loc[2] * step,
                  host ? " (names: the host's query line)" : "");
        if (host && g_c132x.queries < kC132QueryKeys) {
            g_c132x.query[g_c132x.queries++] = a.key;
        }
    }
}

// The key's run of rounds excluded in a row; WARN on the kC132StreakWarn-th.
void c132_note_streak(uint32_t key, const C132LedgerKey& row) {
    const uint32_t round = g_c132c.round;
    C132Streak* slot = nullptr;
    for (C132Streak& t : g_c132s.streak) {
        if (t.key == key && t.streak != 0) {
            slot = &t;
            break;
        }
        if (slot == nullptr && (t.streak == 0 || t.round + 1 < round)) {
            slot = &t; // free, or stale (its run ended)
        }
    }
    if (slot == nullptr) {
        return; // every slot is in a live run: this key's run goes uncounted this round
    }
    if (slot->key == key && slot->streak != 0 && slot->round == round) {
        return; // the key's entry in another level, this round
    }
    if (slot->key != key || slot->round + 1 != round) {
        *slot = C132Streak{key, round, 1};
        return;
    }
    slot->round = round;
    ++slot->streak;
    if (slot->streak == kC132StreakWarn) {
        log::line("WARN card132: key %08x left out of the compare %u rounds in a row (round %u): its last ledger row "
                  "seq %u (%s) comes after S each round", key, kC132StreakWarn, round, row.seq, row.op);
    }
}

// True (and counted) when the key's last received ledger row came after S: the snapshots straddle it.
bool c132_excluded(uint32_t key) {
    C132LedgerKey row{};
    if (c132_ledger_key_row(key, row) && row.seq > g_c132c.sent_s) {
        ++g_c132x.excluded;
        c132_note_streak(key, row);
        return true;
    }
    return false;
}

// Card 237 (diagnostic): the keys of this round's field mismatches (not presence), looked up on each side for the
// actor's level, location, static mesh, hidden bit and collision. The client logs its copy at the compare and asks
// the host ("X<round> <key> ...") to log its own. Lines capped per side per map.
constexpr int32_t kC237Keys = 8;     // per round
constexpr uint32_t kC237Lines = 24;  // per side per map

struct C237Detail {
    uint32_t key[kC237Keys];
    int32_t n;
    uint32_t lines;
};
C237Detail g_c237{}; // game thread

void c237_note_key(uint32_t key) {
    for (int32_t i = 0; i < g_c237.n; ++i) {
        if (g_c237.key[i] == key) {
            return;
        }
    }
    if (g_c237.n < kC237Keys) {
        g_c237.key[g_c237.n++] = key;
    }
}

void c237_detail_line(const char* side, uint32_t round, uint32_t key, const char* name, const void* actor,
                      const void* level) {
    if (!note_event_cap(g_c237.lines, kC237Lines, "card237 mismatch detail")) {
        return;
    }
    const auto* const a = static_cast<const uint8_t*>(actor);
    const void* const root = read_ptr(actor, kActorRootComponent);
    const uint8_t* prop = nullptr;
    const void* const mesh = root != nullptr ? prop_object_field(root, "StaticMesh", &prop) : nullptr;
    const bool primitive = prop != nullptr; // a StaticMeshComponent root: BodyInstance is there to read
    const Vec3 at = actor_location(actor);
    log::line("card237: %s round %u mismatch key %08x is %s %s level %s at (%.0f, %.0f, %.0f) mesh %s hidden %d "
              "actor collision %d root %s (%s) collision %d%s",
              side, round, key, class_name(actor).text, name, c132_level_name(level).text, at.x, at.y, at.z,
              mesh != nullptr ? c74_asset_path(mesh).text : "-", (a[kActorHiddenByte] & kActorHiddenBit) != 0 ? 1 : 0,
              a[kActorCollisionFlags] & kActorEnableCollisionBit, root != nullptr ? object_name(root).text : "null",
              root != nullptr ? class_name(root).text : "-",
              primitive ? static_cast<int>(static_cast<const uint8_t*>(root)[kBodyCollisionEnabled]) : -1,
              c315_timeline_text(actor).text); // card 315 step 1
}

// Every actor of the world's levels whose digest key is one of keys, logged by c237_detail_line. Returns how many.
int32_t c237_walk(const void* world, const char* side, uint32_t round, const uint32_t* keys, int32_t n) {
    int32_t found = 0;
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    for (int32_t l = 0; levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const auto* const actors =
            levels[l] != nullptr ? static_cast<void* const*>(read_ptr(levels[l], kLevelActors)) : nullptr;
        const int32_t num = levels[l] != nullptr ? read_i32(levels[l], kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; actors != nullptr && a < num && a < kMaxLevelActors; ++a) {
            char name[kC132NameMax + 1];
            const void* anchor = nullptr;
            uint8_t kind = 0;
            bool unnamed = false;
            if (actors[a] == nullptr || !c132_actor_name(actors[a], name, sizeof(name), anchor, kind, unnamed)) {
                continue;
            }
            const uint32_t key = c132_fnv(name);
            for (int32_t k = 0; k < n; ++k) {
                if (keys[k] == key) {
                    c237_detail_line(side, round, key, name, actors[a], levels[l]);
                    if (!g_client_session) {
                        c256_heal_rest(actors[a], round); // card 256 class fix 2
                    }
                    ++found;
                }
            }
        }
    }
    return found;
}

// Client, after the compare: its own copies of the mismatch keys, then the ask for the host's.
void c237_client_detail(void* pc, uint32_t round) {
    const void* const world = g_c237.n > 0 ? g_actor_get_world(pc) : nullptr;
    if (world == nullptr) {
        return;
    }
    const int32_t found = c237_walk(world, "client", round, g_c237.key, g_c237.n);
    char text[kChanTextMax + 1];
    int at = snprintf(text, sizeof(text), "X%x", round);
    for (int32_t i = 0; i < g_c237.n; ++i) {
        at += snprintf(text + at, sizeof(text) - static_cast<size_t>(at), " %08x", g_c237.key[i]);
    }
    static wchar_t wire[kChanTextFrame];
    const WireString w = chan_text_frame(ChanOp::DigestQuery, text, wire);
    const bool sent = w.num > 0 && chan_send_host(pc, w);
    if (note_event_cap(g_c237.lines, kC237Lines, "card237 mismatch detail")) {
        log::line("%scard237: client round %u mismatch detail: %d keys, %d own actors; host asked: %s", sent ? "" : "WARN ",
                  round, g_c237.n, found, sent ? "yes" : "no");
    }
}

// Host: a client's "X<round> <key> ...": the host's actors of those keys.
void c237_host_detail(const void* world, char* p) {
    uint32_t keys[kC237Keys];
    int32_t n = 0;
    const uint32_t round = static_cast<uint32_t>(strtoul(p, &p, 16));
    while (n < kC237Keys && *p == ' ') {
        keys[n++] = static_cast<uint32_t>(strtoul(p + 1, &p, 16));
    }
    const int32_t found = c237_walk(world, "host", round, keys, n);
    if (note_event_cap(g_c237.lines, kC237Lines, "card237 mismatch detail")) {
        log::line("card237: host round %u mismatch detail: %d keys, %d actors found", round, n, found);
    }
}

void c132_mismatch(uint32_t key, const char* cls, const char* name, const char* field, const char* host,
                   const char* client) {
    ++g_c132x.mismatches;
    if (strcmp(field, "presence") != 0) {
        c237_note_key(key); // card 237
    }
    pl_count(PlCount::Mismatch); // card 155
    c132_count_class(cls);
    if (g_c132x.lines++ >= kC132FullLines) {
        return;
    }
    C132LedgerKey row{};
    char last[96];
    if (c132_ledger_key_row(key, row)) {
        snprintf(last, sizeof(last), "last row seq %u (%s) sent t=%u applied t=%u", row.seq, row.op, row.sent_ms,
                 row.at_ms);
    } else {
        snprintf(last, sizeof(last), "no row sent");
    }
    log::line("WARN card132: mismatch %s %s %s host %s client %s; %s", cls, name, field, host, client, last);
}

const char* c132_host_class(const C132Entry& e) {
    return e.cls < g_c132c.host.cls.n ? g_c132c.host.cls.name[e.cls] : "?";
}

const char* c132_own_name(const C132Entry& c) {
    return c.name < g_c132c.own.pool_n ? g_c132c.own.pool + c.name : "?";
}

void c132_compare_bits(const C132Entry& h, const C132Entry& c, const char* cls, const char* name) {
    const bool objective = h.level == kC132GlobalLevel;
    const char* const* bits = objective ? kC132ObjectiveBits : kC132ActorBits;
    const int32_t nbits = objective ? 3 : kC132ActorStateBits;
    for (int32_t b = 0; b < nbits; ++b) {
        const uint32_t hv = (h.state >> b) & 1u;
        const uint32_t cv = (c.state >> b) & 1u;
        if (hv != cv) {
            c132_mismatch(c.key, cls, name, bits[b], hv != 0 ? "1" : "0", cv != 0 ? "1" : "0");
        }
    }
    if (h.ext != c.ext) {
        char hv[8];
        char cv[8];
        snprintf(hv, sizeof(hv), "%04x", h.ext);
        snprintf(cv, sizeof(cv), "%04x", c.ext);
        c132_mismatch(c.key, cls, name, c.ext_row < kC132ExtClasses ? kC132Ext[c.ext_row].field : "ext", hv, cv);
    }
}

void c132_compare_pair(const C132Entry& h, const C132Entry& c) {
    const char* const cls = c132_host_class(h);
    const char* const name = c132_own_name(c);
    const char* const own_cls = g_c132c.own.cls.name[c.cls];
    if (strcmp(cls, own_cls) != 0) {
        c132_mismatch(c.key, cls, name, "class", cls, own_cls);
        return;
    }
    c132_compare_bits(h, c, cls, name);
    int32_t moved = 0;
    int32_t wide = 0;
    for (int32_t k = 0; k < 3; ++k) {
        moved |= abs(h.loc[k] - c.loc[k]) > kC132LocTolerance ? 1 : 0;
        wide |= abs(h.loc[k] - c.loc[k]) > kC132AttachHint ? 1 : 0;
    }
    if (moved != 0) {
        const int32_t step = static_cast<int32_t>(kC132LocStep);
        char hv[48];
        char cv[48];
        snprintf(hv, sizeof(hv), "(%d,%d,%d)", h.loc[0] * step, h.loc[1] * step, h.loc[2] * step);
        snprintf(cv, sizeof(cv), "(%d,%d,%d)", c.loc[0] * step, c.loc[1] * step, c.loc[2] * step);
        // Card 164: an attached entry stores its relative location, so an attach on one side only reads as a gap of
        // world size (over 10000 uu).
        c132_mismatch(c.key, cls, name, wide != 0 ? "location (one side attached?)" : "location", hv, cv);
        return;
    }
    // Card 341: unequal but inside the tolerance. The slot sum hashes the exact steps, so this pair makes its slot
    // differ every round with no mismatch.
    static uint32_t rows = 0;
    const bool unequal = h.loc[0] != c.loc[0] || h.loc[1] != c.loc[1] || h.loc[2] != c.loc[2];
    if (unequal) {
        c341_adopt_set(h, c);
    }
    if (unequal && note_event_cap(rows, kC341Rows, "card341")) {
        const int32_t step = static_cast<int32_t>(kC132LocStep);
        log::line("card341: client round %u key %08x %s %s within tolerance: host (%d,%d,%d) client (%d,%d,%d)",
                  g_c132c.round, c.key, cls, name, h.loc[0] * step, h.loc[1] * step, h.loc[2] * step,
                  c.loc[0] * step, c.loc[1] * step, c.loc[2] * step);
    }
}

bool c132_host_level_here(const C132Entry& h) {
    return h.level == kC132GlobalLevel || (h.level < kC132DigestLevels && g_c132c.level_map[h.level] >= 0);
}

bool c132_own_level_on_host(const C132Entry& c) {
    if (c.level == kC132GlobalLevel) {
        return true;
    }
    for (int32_t i = 0; i < g_c132c.host.lvl.n; ++i) {
        if (g_c132c.level_map[i] == c.level) {
            return true;
        }
    }
    return false;
}

void c132_host_only_mismatch(const C132Entry& h) {
    char name[16];
    snprintf(name, sizeof(name), "key=%08x", h.key);
    c132_mismatch(h.key, c132_host_class(h), name, "presence", "1", "0");
}

// A host-only entry: asked about (c132_send_query) and judged once c132_query_own looked for the key here; past the
// query table it is a mismatch at once.
void c132_host_only(const C132Entry& h, int32_t index) {
    if (!c132_host_level_here(h)) {
        ++g_c132x.skip_host;
        return;
    }
    if (g_c132x.queries >= kC132QueryKeys || g_c132x.pendings >= kC132QueryKeys) {
        c132_host_only_mismatch(h);
        return;
    }
    g_c132x.query[g_c132x.queries++] = h.key;
    g_c132x.pending[g_c132x.pendings++] = index;
}

bool c132_is_pending(uint32_t key) {
    for (int32_t i = 0; i < g_c132x.pendings; ++i) {
        if (g_c132c.host.e[g_c132x.pending[i]].key == key) {
            return true;
        }
    }
    return false;
}

// A host-only key whose actor is here, placed, and left out by this side's bReplicates alone: the host digests it,
// so it does not replicate there, and the host's flag decides (ALevelSequenceActor sets bReplicates in its
// constructor and only an authority's SetReplicates(bReplicatePlayback) clears it). It joins the client digest from
// the next round, and is no mismatch this round.
void c132_adopt(const void* actor, uint32_t key, const char* name) {
    if ((static_cast<const uint8_t*>(actor)[kActorRepFlags] & 0x08u) == 0 || !c50_placed(actor) ||
        !c132_is_pending(key) || c132_adopted(key)) {
        return;
    }
    if (g_c132s.adopts >= kC132Adopts) {
        // Not adopted: c132_judge_host_only counts it as a presence mismatch, and this line says why.
        log::line("WARN card132: client round %u key %08x %s %s replicates here but not on the host, and the adoption "
                  "table is full (%d this map): counted as a mismatch",
                  g_c132c.round, key, class_name(actor).text, name, kC132Adopts);
        return;
    }
    g_c132s.adopt[g_c132s.adopts++] = key;
    log::line("card132: client round %u key %08x %s %s replicates here but not on the host: the host's flag decides, "
              "in the client digest from the next round (%d adopted this map)",
              g_c132c.round, key, class_name(actor).text, name, g_c132s.adopts);
}

// The host-only entries c132_query_own did not adopt are presence mismatches.
void c132_judge_host_only() {
    for (int32_t i = 0; i < g_c132x.pendings; ++i) {
        const C132Entry& h = g_c132c.host.e[g_c132x.pending[i]];
        if (!c132_adopted(h.key)) {
            c132_host_only_mismatch(h);
        }
    }
}

void c132_own_only(const C132Entry& c) {
    if (!c132_own_level_on_host(c)) {
        ++g_c132x.skip_own;
        return;
    }
    c132_mismatch(c.key, g_c132c.own.cls.name[c.cls], c132_own_name(c), "presence", "0", "1");
}

void c132_map_levels() {
    for (int32_t i = 0; i < kC132DigestLevels; ++i) {
        g_c132c.level_map[i] = -1;
    }
    for (int32_t i = 0; i < g_c132c.host.lvl.n; ++i) {
        for (int32_t j = 0; j < g_c132c.own.lvl.n && g_c132c.level_map[i] < 0; ++j) {
            g_c132c.level_map[i] = strcmp(g_c132c.host.lvl.name[i], g_c132c.own.lvl.name[j]) == 0 ? j : -1;
        }
    }
    for (int32_t j = 0; j < kC132DigestLevels; ++j) {
        g_c132c.own_level_host[j] = -1;
    }
    for (int32_t i = 0; i < g_c132c.host.lvl.n; ++i) {
        if (g_c132c.level_map[i] >= 0) {
            g_c132c.own_level_host[g_c132c.level_map[i]] = i;
        }
    }
}

// Client, each round with host-only keys: every own actor whose name has one of those keys, the digest's filter
// left out, with its AActor net bitfield bytes. It tells an actor the client has but the digest skips (bReplicates
// set here only, or not placed) from one the client never loaded or destroyed (0 found).
void c132_query_own(const void* world) {
    int32_t found = 0;
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    for (int32_t l = 0; levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const auto* const actors =
            levels[l] != nullptr ? static_cast<void* const*>(read_ptr(levels[l], kLevelActors)) : nullptr;
        const int32_t num = levels[l] != nullptr ? read_i32(levels[l], kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; actors != nullptr && a < num && a < kMaxLevelActors; ++a) {
            char name[kC132NameMax + 1];
            if (actors[a] == nullptr || c50_name(actors[a], name, static_cast<int32_t>(sizeof(name))) <= 0) {
                continue;
            }
            const uint32_t key = c132_fnv(name);
            for (int32_t k = 0; k < g_c132x.queries; ++k) {
                if (g_c132x.query[k] != key) {
                    continue;
                }
                const auto* const flags = static_cast<const uint8_t*>(actors[a]) + kC132ActorNetFlags;
                log::line("card132: client round %u host-only key %08x here is %s %s level %s, placed %d, net flags "
                          "%02x %02x %02x %02x",
                          g_c132c.round, key, class_name(actors[a]).text, name, c132_level_name(levels[l]).text,
                          c50_placed(actors[a]) ? 1 : 0, flags[0], flags[1], flags[2], flags[3]);
                c132_adopt(actors[a], key, name);
                ++found;
            }
        }
    }
    log::line("card132: client round %u host-only keys: %d, %d own actors with one", g_c132c.round, g_c132x.queries,
              found);
}

void c132_send_query(void* pc) {
    char text[kChanTextMax + 1];
    int at = snprintf(text, sizeof(text), "Q%x", g_c132c.round);
    for (int32_t i = 0; i < g_c132x.queries; ++i) {
        at += snprintf(text + at, sizeof(text) - static_cast<size_t>(at), " %08x", g_c132x.query[i]);
    }
    static wchar_t wire[kChanTextFrame];
    const WireString w = chan_text_frame(ChanOp::DigestQuery, text, wire);
    if (!(w.num > 0 && chan_send_host(pc, w))) {
        log::line("WARN card132: client round %u: host-only key query not sent", g_c132c.round);
    }
}

void c132_round_line() {
    // Count the pose mismatches before the class list is built, or they show as a blank "(classes )" (hb47 round 4).
    const uint32_t pose_bad = c132_pose_check(g_c132c.round); // card 132 corpse poses, 100 uu tolerance
    for (uint32_t i = 0; i < pose_bad; ++i) {
        c132_count_class("Pose");
    }
    g_c132x.mismatches += pose_bad;
    pl_count(PlCount::Mismatch, pose_bad); // card 155
    char classes[900];
    size_t at = 0;
    classes[0] = '\0';
    for (int32_t i = 0; i < g_c132x.classes && at < sizeof(classes); ++i) {
        const int w = snprintf(classes + at, sizeof(classes) - at, "%s%s:%u", i == 0 ? "" : " ", g_c132x.count[i].cls,
                               g_c132x.count[i].n);
        at += w > 0 ? static_cast<size_t>(w) : 0;
    }
    const C132Digest& h = g_c132c.host;
    const C132Digest& c = g_c132c.own;
    const C132Client& cl = g_c132c;
    const bool warn = g_c132x.mismatches != 0 || cl.unasked != 0;
    log::line("%scard132: client round %u: actors %d host, %d client, mismatches %u (classes %s); slots differing %d "
              "(entries asked %d host, %d client), %d past the cap of %d; child actors %u client; excluded %u (a "
              "ledger row after S=%u), %u (a duplicate key: %d keys this map, %u past the table); skipped in levels "
              "loaded on one side: %u host, %u client; dropped %u host, %u client; unnamed %u client",
              warn ? "WARN " : "", cl.round, cl.host_n, cl.own_n, g_c132x.mismatches,
              g_c132x.classes != 0 ? classes : "-", cl.asked_n + cl.unasked, h.n, c.n, cl.unasked, kC132DetailSlots,
              c.children, g_c132x.excluded, cl.sent_s, g_c132x.duplicate, g_c132s.dups, g_c132s.dup_over,
              g_c132x.skip_host, g_c132x.skip_own, h.dropped, c.dropped, c.unnamed);
    c132_server_only_line(c, "client", cl.round);
    c132_proj_round(g_c132c.round);
}

// The client's snapshot, at B once every row up to S is in (or after kC132SnapWaitMs, with a WARN).
void c132_try_snap(ULONGLONG now) {
    C132Client& c = g_c132c;
    void* const pc = c31_weak_get(c.pc);
    const uint32_t rx = c132_ledger_recv_seq(pc);
    const bool late = now - c.b_at >= kC132SnapWaitMs;
    if (rx < c.sent_s && !late) {
        return;
    }
    const void* const world = pc != nullptr ? g_actor_get_world(pc) : nullptr;
    if (world == nullptr) {
        log::line("WARN card132: client round %u: no world to snapshot, round dropped", c.round);
        c.begun = false;
        return;
    }
    if (rx < c.sent_s) {
        log::line("WARN card132: client round %u: rows up to S=%u not all received after %llu ms (last %u); "
                  "snapshot taken anyway",
                  c.round, c.sent_s, static_cast<unsigned long long>(now - c.b_at), rx);
    }
    c132_build(c.own, world);
    c.snapped = true;
    log::line("card132: client t=%llu digest round %u snapshot: %d entries, last row in %u (S=%u), %llu ms after B",
              static_cast<unsigned long long>(now), c.round, c.own.n, rx, c.sent_s,
              static_cast<unsigned long long>(now - c.b_at));
    c137_pickup_probe(world, "digest round", false); // card 137 P7: placed pickup marks at each round
}

// The own entries of the asked slots, kept in order (the digest stays sorted).
void c132_keep_asked(C132Digest& c) {
    int32_t kept = 0;
    for (int32_t i = 0; i < c.n; ++i) {
        const C132Entry& e = c.e[i];
        const int32_t level = e.level == kC132GlobalLevel ? e.level : g_c132c.own_level_host[e.level];
        if (level >= 0 && c132_slot_set(g_c132c.asked, c132_slot(static_cast<uint8_t>(level), e.key))) {
            c.e[kept++] = e;
        }
    }
    c.n = kept;
}

// The entry compare of the asked slots (the F frame, or at once when no slot differs), then the round line.
void c132_compare() {
    C132Client& cl = g_c132c;
    cl.begun = false;
    cl.detail_asked = false;
    g_c237.n = 0; // card 237: this round's mismatch keys
    c132_keep_asked(cl.own);
    qsort(cl.host.e, static_cast<size_t>(cl.host.n), sizeof(C132Entry), c132_entry_cmp);
    const C132Digest& h = cl.host;
    const C132Digest& c = cl.own;
    c132_find_dups(h, true);
    int32_t i = 0;
    int32_t j = 0;
    while (i < h.n || j < c.n) { // bounded: each pass advances i or j
        const int order = j >= c.n ? -1 : (i >= h.n ? 1 : c132_entry_cmp(&h.e[i], &c.e[j]));
        const uint32_t key = order <= 0 ? h.e[i].key : c.e[j].key;
        const uint32_t lvl = order <= 0 ? h.e[i].lvl : c.e[j].lvl;
        if (c132_is_dup(key, lvl)) {
            ++g_c132x.duplicate;
            i += order <= 0 ? 1 : 0;
            j += order >= 0 ? 1 : 0;
        } else if (c132_excluded(key)) {
            i += order <= 0 ? 1 : 0;
            j += order >= 0 ? 1 : 0;
        } else if (order < 0) {
            c132_host_only(h.e[i], i);
            ++i;
        } else if (order > 0) {
            c132_own_only(c.e[j++]);
        } else {
            c132_compare_pair(h.e[i++], c.e[j++]);
        }
    }
    void* const pc = c31_weak_get(cl.pc); // card 331: null once the own PC is gone; the sends below then log no PC
    const void* const world = g_c132x.queries > 0 && pc != nullptr ? g_actor_get_world(pc) : nullptr;
    if (world != nullptr) {
        c132_query_own(world);
    }
    c132_judge_host_only();
    c132_round_line();
    if (g_c132x.queries > 0) {
        c132_send_query(pc);
    }
    if (pc != nullptr) {
        c237_client_detail(pc, cl.round);
    }
}

// "D<round> <slot> ...": the asked slots, to the host.
bool c132_send_ask() {
    char text[kChanTextMax + 1];
    int at = snprintf(text, sizeof(text), "D%x", g_c132c.round);
    for (int32_t slot = 0; slot < kC132Slots; ++slot) {
        if (c132_slot_set(g_c132c.asked, slot)) {
            at += snprintf(text + at, sizeof(text) - static_cast<size_t>(at), " %x", static_cast<uint32_t>(slot));
        }
    }
    static wchar_t wire[kChanTextFrame];
    const WireString w = chan_text_frame(ChanOp::DigestQuery, text, wire);
    return w.num > 0 && chan_send_host(c31_weak_get(g_c132c.pc), w);
}

// Card 329, at the E frame once snapped: the own slot sums (the host's level indexes) and their whole sum against the
// host's W. Equal: every slot is equal and the round ends here. Else the slot sums are asked for (DigestQuery "H");
// c132_slot_compare runs at the G frame. Own entries in a level the host has not loaded are left out (counted).
void c132_sum_compare() {
    C132Client& cl = g_c132c;
    c132_map_levels();
    g_c132x = C132Compare{};
    c132_find_dups(cl.own, false);
    cl.own_n = cl.own.n;
    g_c132x.skip_own = c132_sum(cl.own, cl.own_level_host, cl.own_sums);
    memset(cl.asked, 0, sizeof(cl.asked));
    cl.asked_n = 0;
    cl.unasked = 0;
    if (c132_whole(cl.own_sums) == cl.whole) {
        c132_compare();
        return;
    }
    char text[32];
    snprintf(text, sizeof(text), "H%x", cl.round);
    static wchar_t wire[kChanTextFrame];
    const WireString w = chan_text_frame(ChanOp::DigestQuery, text, wire);
    if (w.num <= 0 || !chan_send_host(c31_weak_get(cl.pc), w)) {
        log::line("WARN card329: client round %u: whole sum differs, slot sums ask not sent; round dropped", cl.round);
        cl.begun = false;
        return;
    }
    cl.sums_asked = true;
    log::line("card329: client t=%llu digest round %u: whole sum differs, slot sums asked",
              static_cast<unsigned long long>(GetTickCount64()), cl.round);
}

// At the G frame (card 329; before, at E): the slot sums, host versus own. Slots of a host level not loaded here are
// skipped (their entries counted). The differing slots are asked for (at most kC132DetailSlots); with none, the round
// ends here.
void c132_slot_compare() {
    C132Client& cl = g_c132c;
    for (int32_t slot = 0; slot < kC132Slots; ++slot) {
        const int32_t level = slot / kC132Buckets;
        const uint16_t hn = cl.host_sums.n[slot];
        if (level != kC132GlobalLevel && (level >= cl.host.lvl.n || cl.level_map[level] < 0)) {
            g_c132x.skip_host += hn;
            continue;
        }
        if (hn == cl.own_sums.n[slot] && cl.host_sums.hash[slot] == cl.own_sums.hash[slot]) {
            continue;
        }
        if (cl.asked_n >= kC132DetailSlots) {
            ++cl.unasked;
            continue;
        }
        cl.asked[slot / 64] |= 1ull << (slot % 64);
        ++cl.asked_n;
    }
    if (cl.asked_n == 0) {
        c132_compare();
        return;
    }
    if (!c132_send_ask()) {
        log::line("WARN card132: client round %u: detail ask for %d slots not sent", cl.round, cl.asked_n);
        c132_compare();
        return;
    }
    cl.detail_asked = true;
    log::line("card132: client t=%llu digest round %u: %d slots differ, %d asked",
              static_cast<unsigned long long>(GetTickCount64()), cl.round, cl.asked_n + cl.unasked, cl.asked_n);
}

// Card 329, at E: the tables (sent this round or kept) against the host's T. A mismatch asks the host for the
// tables next round (DigestQuery "T") and drops this round.
bool c132_tables_ok() {
    C132Client& cl = g_c132c;
    if (c132_tables_hash(cl.host) == cl.tables) {
        return true;
    }
    char text[32];
    snprintf(text, sizeof(text), "T%x", cl.round);
    static wchar_t wire[kChanTextFrame];
    const WireString w = chan_text_frame(ChanOp::DigestQuery, text, wire);
    const bool sent = w.num > 0 && chan_send_host(c31_weak_get(cl.pc), w);
    log::line("WARN card329: client round %u: tables (%d levels, %d classes, %s) do not match the host's; round "
              "dropped, tables asked again%s",
              cl.round, cl.host.lvl.n, cl.host.cls.n, cl.tables_follow ? "sent" : "kept", sent ? "" : ": not sent");
    cl.host.lvl.n = 0;
    cl.host.cls.n = 0;
    cl.begun = false;
    return false;
}

bool c132_parse_names(C132Names& t, const char* p) {
    for (int32_t guard = 0; *p != '\0' && guard < kC132DigestClasses; ++guard) {
        const char* const eq = strchr(p, '=');
        const char* const end = eq != nullptr ? strchr(eq, ';') : nullptr;
        const long idx = eq != nullptr ? strtol(p, nullptr, 16) : -1;
        if (end == nullptr || idx != t.n || t.n >= kC132DigestClasses || end - eq - 1 >= kNameChars) {
            return false;
        }
        snprintf(t.name[t.n], kNameChars, "%.*s", static_cast<int>(end - eq - 1), eq + 1);
        ++t.n;
        p = end + 1;
    }
    return true;
}

uint32_t c132_hex(const char* p, int32_t n) {
    uint32_t v = 0;
    for (int32_t i = 0; i < n; ++i) {
        const char ch = p[i];
        const uint32_t d = ch >= '0' && ch <= '9' ? static_cast<uint32_t>(ch - '0')
                                                  : (ch >= 'a' && ch <= 'f' ? static_cast<uint32_t>(ch - 'a' + 10) : 0u);
        v = (v << 4) | d;
    }
    return v;
}

bool c132_parse_entries(const char* p) {
    C132Digest& d = g_c132c.host;
    const size_t len = strlen(p);
    size_t at = 0;
    while (at + kC132EntryChars <= len) { // bounded by len
        const char* const q = p + at;
        C132Entry e{};
        e.key = c132_hex(q, 8);
        e.level = static_cast<uint8_t>(c132_hex(q + 8, 2));
        e.cls = static_cast<uint16_t>(c132_hex(q + 10, 3));
        e.state = static_cast<uint8_t>(c132_hex(q + 13, 2));
        e.ext = static_cast<uint16_t>(c132_hex(q + 15, 4));
        for (int32_t k = 0; k < 3; ++k) {
            e.loc[k] = static_cast<int16_t>(static_cast<uint16_t>(c132_hex(q + 19 + 4 * k, 4)));
        }
        e.ext_row = kC132NoExt;
        if (e.level != kC132GlobalLevel && e.level >= d.lvl.n) {
            return false; // the level table comes first
        }
        e.lvl = e.level == kC132GlobalLevel ? 0u : c132_fnv(d.lvl.name[e.level]);
        at += kC132EntryChars;
        c132_add_entry(d, e);
    }
    return at == len;
}

// H<slot4><count4><hash8>...
bool c132_parse_sums(const char* p) {
    const size_t len = strlen(p);
    size_t at = 0;
    for (; at + kC132SlotChars <= len; at += kC132SlotChars) { // bounded by len
        const uint32_t slot = c132_hex(p + at, 4);
        if (slot >= static_cast<uint32_t>(kC132Slots)) {
            return false;
        }
        g_c132c.host_sums.n[slot] = static_cast<uint16_t>(c132_hex(p + at + 4, 4));
        g_c132c.host_sums.hash[slot] = c132_hex(p + at + 8, 8);
    }
    return at == len;
}

// B<round> <entries> <classes> <levels> <S> <T> <tables> <W> <PW> <why>
bool c132_parse_begin(const char* t) {
    C132Client& c = g_c132c;
    char* p = nullptr;
    c.round = static_cast<uint32_t>(strtoul(t, &p, 16));
    c.host_n = p != nullptr && *p == ' ' ? static_cast<int32_t>(strtol(p + 1, &p, 16)) : -1;
    uint32_t f[7] = {}; // classes, levels, S, T, tables, W, PW
    int32_t n = 0;
    for (; n < 7 && p != nullptr && *p == ' '; ++n) {
        f[n] = static_cast<uint32_t>(strtoul(p + 1, &p, 16));
    }
    c.sent_s = f[2];
    c.tables = f[3];
    c.tables_follow = f[4] != 0;
    c.whole = f[5];
    c.pw = f[6];
    return n == 7 && p != nullptr && (*p == ' ' || *p == '\0');
}

// Card 327 PW, client, at each B: the host's hash of this client's inventory against its own. A difference is
// reported once it holds for two rounds in a row with the same two hashes (an inventory change in flight, a shot's
// ammo or a recharge, differs for one round at most); once per pair.
void c132_pw_compare(void* pc) {
    C132Client& c = g_c132c;
    static C327Texts own_texts;
    const uint32_t own = c.pw != kC327NoHash && pc != nullptr ? c327_own_hash(read_ptr(pc, kControllerPawn), own_texts)
                                                             : kC327NoHash;
    if (own == kC327NoHash || own == c.pw) {
        c.pw_last_host = kC327NoHash;
        c.pw_last_own = kC327NoHash;
        c.pw_told = false;
        return;
    }
    const bool held = c.pw == c.pw_last_host && own == c.pw_last_own;
    c.pw_told = held ? c.pw_told : false;
    c.pw_last_host = c.pw;
    c.pw_last_own = own;
    if (!held || c.pw_told) {
        return;
    }
    c.pw_told = true;
    log::line("WARN card327: client t=%llu digest round %u PW differs for two rounds: host %04x, client %04x; client "
              "weapons %s; ammo %s; charge %s",
              static_cast<unsigned long long>(GetTickCount64()), c.round, c.pw, own, own_texts.weapons,
              own_texts.ammo, own_texts.charge);
}

} // namespace

// Card 137 2c: the placed weapon pickups on this side, one line each (capped), so the run diffs the two sides. The
// persistent active byte (+0x408 + 0x18) and the IsPickupEnabled call (which ands bEnabled with it and significance)
// say whether the pickup is live here; the mesh bits and the interaction collider's collision say what a player sees.
// detail false: only the summary line, with one character per pickup in actor order (E enabled and visible, e
// enabled not visible, v visible not enabled, - neither), which a run diffs between the sides at each probe.
void c137_pickup_probe(const void* world, const char* when, bool detail) {
    constexpr int32_t kProbeLines = 48;
    constexpr size_t kPersistentActive = 0x408 + 0x18;
    const auto* const levels = world != nullptr ? static_cast<void* const*>(read_ptr(world, kWorldLevels)) : nullptr;
    const int32_t num_levels = levels != nullptr ? read_i32(world, kWorldLevels + kArrayNum) : 0;
    int32_t found = 0;
    int32_t live = 0;
    char marks[kProbeLines + 1] = {};
    for (int32_t l = 0; l < num_levels && l < kMaxLevels; ++l) {
        const void* const level = levels[l];
        const auto* const actors = level != nullptr ? static_cast<void* const*>(read_ptr(level, kLevelActors)) : nullptr;
        const int32_t num = actors != nullptr ? read_i32(level, kLevelActors + kArrayNum) : 0;
        for (int32_t i = 0; i < num && i < kMaxLevelActors; ++i) {
            const auto* const a = static_cast<const uint8_t*>(actors[i]);
            if (a == nullptr || g_c50_weapon_pickup_class == nullptr || !is_a(a, g_c50_weapon_pickup_class()) ||
                !c50_placed(a) || found++ >= kProbeLines) {
                continue;
            }
            const auto* const vtable = static_cast<const uint8_t*>(read_ptr(a, 0));
            const auto enabled = reinterpret_cast<PickupEnabledFn>(read_ptr(vtable, kPickupEnabledSlot));
            const auto* const mesh = static_cast<const uint8_t*>(c31_object_prop(a, "PickupMeshComponent"));
            const auto* const collider = static_cast<const uint8_t*>(c31_object_prop(a, "InteractionCollider"));
            const bool on = enabled != nullptr && enabled(a);
            const bool seen = mesh != nullptr && (mesh[kSceneVisibleByte] & kSceneVisibleBit) != 0;
            marks[found - 1] = on && seen ? 'E' : on ? 'e' : seen ? 'v' : '-';
            live += on && seen ? 1 : 0;
            if (!detail) {
                continue;
            }
            const Vec3 at = actor_location(a);
            log::line("card137: %s pickup probe %s %s at (%.0f, %.0f, %.0f): bHidden %d bEnabled %d IsPickupEnabled %d "
                      "persistent active %d mesh visible %d mesh hiddeningame %d collider collision %d",
                      side_text(), when, object_name(a).text, at.x, at.y, at.z,
                      (a[kActorHiddenByte] & kActorHiddenBit) != 0 ? 1 : 0, a[kPickupEnabledByte] != 0 ? 1 : 0,
                      enabled != nullptr && enabled(a) ? 1 : 0, a[kPersistentActive],
                      mesh != nullptr ? ((mesh[kSceneVisibleByte] & kSceneVisibleBit) != 0 ? 1 : 0) : -1,
                      mesh != nullptr ? ((mesh[kSceneHiddenInGameByte] & kSceneHiddenInGameBit) != 0 ? 1 : 0) : -1,
                      collider != nullptr ? static_cast<int>(collider[kBodyCollisionEnabled]) : -1);
        }
    }
    log::line("card137: %s pickup probe %s: %d placed weapon pickups (%d logged), %d enabled and visible, marks %s",
              side_text(), when, found, detail ? (found < kProbeLines ? found : kProbeLines) : 0, live, marks);
}

bool c132_ext_field(const void* actor, C132ExtField& f) {
    f.row = c132_actor_ext(actor, f.ext);
    if (f.row == kC132NoExt) {
        return false;
    }
    f.lo = c132_row_lo(f.row);
    f.hi = kC132Ext[f.row].hi;
    return true;
}

// Card 386: the time between timer rounds, and from a map load to its first round.
static ULONGLONG c386_round_ms() {
    return config::test_skip() ? kC132RoundMs : kC386PlayRoundMs;
}

// Per map (c98_arm_map): the first round is c386_round_ms() after the load; a round in flight is dropped.
void c132_arm_map() {
    memset(g_c132_class_row, 0, sizeof(g_c132_class_row)); // card 330: classes of the map before may be gone
    if (g_c132h.phase != C132Phase::Idle) {
        log::line("WARN card132: host digest round %u (%s) cut by the map change", g_c132h.round, g_c132h.why);
    }
    if (g_c132c.begun) {
        log::line("WARN card132: client digest round %u cut by the map change", g_c132c.round);
    }
    if (g_c132h.command_due) {
        log::line("WARN card232: host digest command round dropped by the map change");
    }
    g_c132h.phase = C132Phase::Idle;
    g_c132h.details = 0;
    g_c132h.command_due = false;
    g_c132h.settle_until_ms = 0;
    g_c132h.next_ms = GetTickCount64() + c386_round_ms();
    g_c132h.inventory_due = true;
    c132_ledger_arm_map();
    c132_proj_map();
    g_c132s = C132Skip{};
    // Card 341: an adopted row whose key is not on the next map is never freed by c341_adopted; clear the table so
    // rows do not pile up to kC341Adopt over a playthrough.
    memset(g_c341_adopt, 0, sizeof(g_c341_adopt));
    g_c341_adopt_n = 0;
    g_c132_template_lines = 0;
    g_c237 = C237Detail{};
    g_c132c.begun = false;
    g_c132c.pc = C31Weak{};
}

// Every tick (ubw_post_tick). Host: the map load inventory, the round timer and frames. Client: the snapshot and
// the compare of a round in flight.
void c132_digest_step(void* engine) {
    const ULONGLONG now = GetTickCount64();
    C132Client& c = g_c132c;
    if (g_client_session && c.begun) {
        if (!c.snapped) {
            c132_try_snap(now);
        }
        if (c.begun && c.snapped && c.ended && !c.detail_asked && !c.sums_asked) {
            c132_sum_compare();
        }
        return;
    }
    void* const world = engine != nullptr && !g_client_session ? g_get_game_world(engine) : nullptr;
    if (!c132_host_world(world)) {
        return;
    }
    if (g_c132h.inventory_due) {
        g_c132h.inventory_due = false;
        c132_inventory(world, "map load"); // card 132 step 1
    }
    C132Host& h = g_c132h;
    // Card 232: a round starts only when idle, no detail queued and the last round settled; a command goes first and
    // pushes the timer.
    const bool ready = h.phase == C132Phase::Idle && h.details == 0 && now >= h.settle_until_ms;
    if (ready && h.command_due) {
        h.command_due = false;
        h.next_ms = now + c386_round_ms();
        c132_host_start(world, "command");
    } else if (ready && h.next_ms != 0 && now >= h.next_ms) {
        h.next_ms = now + c386_round_ms();
        c132_host_start(world, "timer");
    }
    if (now < h.next_frame_ms) {
        return;
    }
    if (h.phase != C132Phase::Idle) {
        h.next_frame_ms = now + kC132FrameMs;
        c132_host_frame(world);
    } else if (h.details > 0) {
        h.next_frame_ms = now + kC132FrameMs;
        c132_detail_frame(world);
    }
}

// Host: the `objectives` test command's round (the scenario's map end round). Card 232: started by c132_digest_step
// once the host is idle and settled.
void c132_digest_now(const void* world) {
    if (c132_host_world(world)) {
        g_c132h.command_due = true;
    }
}

// Client: one ChanOp::Digest frame.
void c132_receive(void* pc, const WireString* s) {
    char text[kChanTextMax + 1];
    if (chan_text(s, text, sizeof(text)) <= 0) {
        return;
    }
    C132Client& c = g_c132c;
    bool ok = true;
    const ULONGLONG now = GetTickCount64();
    if (text[0] == 'B') {
        if (c.begun) {
            log::line("WARN card132: client round %u cut by the next B frame", c.round);
        }
        c.host.n = 0;
        c.host.dropped = 0;
        c.host.children = 0;
        memset(&c.host_sums, 0, sizeof(c.host_sums));
        c.detail_asked = false;
        c.sums_asked = false;
        c31_weak_set(c.pc, pc);
        c.b_at = now;
        c.snapped = false;
        c.ended = false;
        ok = c132_parse_begin(text + 1);
        c.begun = ok;
        if (!ok || c.tables_follow) { // card 329: kept from the last round unless they follow
            c.host.cls.n = 0;
            c.host.lvl.n = 0;
        }
        log::line("card132: client t=%llu digest round %u begins: %s", static_cast<unsigned long long>(now), c.round,
                  text + 1);
        if (ok) {
            c132_pw_compare(pc);
            c132_try_snap(now); // no wait when every row up to S is in, the usual case (the channel is ordered)
        }
    } else if (!c.begun) {
        return; // joined mid-round: wait for the next B
    } else if (text[0] == 'V') {
        ok = c.tables_follow && c132_parse_names(c.host.lvl, text + 1) && c.host.lvl.n <= kC132DigestLevels;
    } else if (text[0] == 'C') {
        ok = c.tables_follow && c132_parse_names(c.host.cls, text + 1);
    } else if (text[0] == 'H') {
        ok = c.sums_asked && c132_parse_sums(text + 1);
    } else if (text[0] == 'G') {
        ok = c.sums_asked && static_cast<uint32_t>(strtoul(text + 1, nullptr, 16)) == c.round;
        c.sums_asked = false;
        if (ok) {
            c132_slot_compare();
        }
    } else if (text[0] == 'E') {
        ok = !c.ended && static_cast<uint32_t>(strtoul(text + 1, nullptr, 16)) == c.round;
        c.ended = ok;
        if (ok && !c132_tables_ok()) {
            return; // dropped, logged
        }
        if (ok && c.snapped) {
            c132_sum_compare();
        }
    } else if (text[0] == 'P') {
        ok = c.detail_asked && c132_parse_entries(text + 1);
    } else if (text[0] == 'F') {
        ok = c.detail_asked && static_cast<uint32_t>(strtoul(text + 1, nullptr, 16)) == c.round;
        if (ok) {
            c132_compare();
        }
    }
    if (!ok) {
        c.begun = false;
        log::line("WARN card132: client round %u: malformed '%c' frame, round dropped", c.round, text[0]);
    }
}

// Card 164: the objective keys among a Q frame's host-only keys (FNV-1a of "O:<id>", as c132_add_objectives makes
// them), logged with their name. Returns the keys found.
int32_t c132_query_objectives(const void* world, uint32_t round, const uint32_t* keys, int32_t n) {
    void* const subsystem = g_c49_get_subsystem != nullptr ? g_c49_get_subsystem(const_cast<void*>(world)) : nullptr;
    SparseView v{};
    if (subsystem == nullptr || !c49_objectives(subsystem, v)) {
        return 0;
    }
    int32_t found = 0;
    for (int32_t i = 0; i < v.count; ++i) {
        const uint8_t* const o = sparse_allocated(v, i) ? static_cast<const uint8_t*>(read_ptr(
                                     v.data + static_cast<size_t>(i) * kObjectiveElementBytes, kObjectiveElementValue))
                                                        : nullptr;
        if (o == nullptr) {
            continue;
        }
        char text[kNameChars + 2];
        snprintf(text, sizeof(text), "O:%s", fname_text(o + kObjectiveId).text);
        const uint32_t key = c132_fnv(text);
        for (int32_t k = 0; k < n; ++k) {
            if (keys[k] != key) {
                continue;
            }
            const uint32_t state = (o[kObjectiveComplete] != 0 ? kC132Complete : 0u) |
                                   (o[kObjectiveMarker] != 0 ? kC132Marker : 0u) |
                                   (o[kObjectiveInList] != 0 ? kC132InList : 0u);
            log::line("card132: host round %u host-only key %08x is objective %s (class %s, state %02x)", round, key,
                      text + 2, class_name(o).text, state);
            ++found;
        }
    }
    return found;
}

// Host: a client's ChanOp::DigestQuery. "D": the slots to send the entries of (c132_host_detail_ask). "Z": card 328's
// host row for an NPC the client clamped. "H" (card 329): the round's slot sums. "T" (card 329): the client's tables
// did not match, send them again. "Q": the host-only keys; each is looked up in the live world (the round's actors
// may be gone) and logged.
void c132_host_query(void* pc, const WireString* s) {
    char text[kChanTextMax + 1];
    const void* const world = g_actor_get_world(pc);
    if (chan_text(s, text, sizeof(text)) <= 0 || world == nullptr) {
        return;
    }
    if (text[0] == 'D') {
        c132_host_detail_ask(pc, text + 1);
        return;
    }
    if (text[0] == 'X') {
        c237_host_detail(world, text + 1); // card 237
        return;
    }
    if (text[0] == 'Z') {
        c328_host_row(pc, strtoull(text + 1, nullptr, 16)); // card 328
        return;
    }
    if (text[0] == 'H') {
        c132_host_sums_ask(pc, text + 1); // card 329
        return;
    }
    if (text[0] == 'T') {
        c132_host_tables_ask(pc, text + 1); // card 329
        return;
    }
    if (text[0] != 'Q') {
        return;
    }
    uint32_t keys[kC132QueryKeys];
    int32_t n = 0;
    char* p = text + 1;
    const uint32_t round = static_cast<uint32_t>(strtoul(p, &p, 16));
    while (n < kC132QueryKeys && *p == ' ') {
        keys[n++] = static_cast<uint32_t>(strtoul(p + 1, &p, 16));
    }
    int32_t found = 0;
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    for (int32_t l = 0; levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const auto* const actors =
            levels[l] != nullptr ? static_cast<void* const*>(read_ptr(levels[l], kLevelActors)) : nullptr;
        const int32_t num = levels[l] != nullptr ? read_i32(levels[l], kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; actors != nullptr && a < num && a < kMaxLevelActors; ++a) {
            char name[kC132NameMax + 1];
            const void* anchor = nullptr;
            uint8_t kind = 0;
            bool unnamed = false;
            if (actors[a] == nullptr || !c132_actor_name(actors[a], name, sizeof(name), anchor, kind, unnamed)) {
                continue;
            }
            const uint32_t key = c132_fnv(name);
            for (int32_t k = 0; k < n; ++k) {
                if (keys[k] != key) {
                    continue;
                }
                const Vec3 at = actor_location(anchor);
                // AActor bitfield bytes +0x58..+0x5B (bit 3 of +0x59 bNetLoadOnClient, bit 3 of +0x5B bReplicates)
                // tell an actor the engine never loads on a client from one the client lost.
                const auto* const flags = static_cast<const uint8_t*>(actors[a]) + kC132ActorNetFlags;
                log::line("card132: host round %u host-only key %08x is %s %s at (%.0f, %.0f, %.0f) state %02x level %s, "
                          "net flags %02x %02x %02x %02x",
                          round, key, class_name(actors[a]).text, name, at.x, at.y, at.z,
                          c132_actor_state(actors[a]) | kind, c132_level_name(levels[l]).text, flags[0], flags[1],
                          flags[2], flags[3]);
                ++found;
            }
        }
    }
    found += c132_query_objectives(world, round, keys, n); // card 164: objective keys too
    log::line("card132: host round %u host-only query: %d keys, %d found in the live world", round, n, found);
}

} // namespace ubw::hooks
