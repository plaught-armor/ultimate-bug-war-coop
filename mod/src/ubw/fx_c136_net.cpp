// Card 136 steps 2 to 4, the effects manager's wire, journal and client play (card 136 "Steps 2 to 6: design").
//
// Host. c136_fx_enter (fx_c136.cpp) calls c136_net_queue for each outermost event on the game thread; the one pending
// row takes the event's fields while its arguments live, and c136_fx_leave's c136_net_done completes it (the spawned
// component's seed is read then) and queues its E row. Each tick c136_step sweeps part of the journal and flushes the
// queue to every remote PC past its first Pong, packed into ChanOp::Fx blob frames (card 136 run 4: binary rows, the
// card's "Run 4 wire format" section has the layout):
//   A (1)  an asset table row: index, kind (path or cue tag), the text (per map; each PC gets the rows it has not had,
//          before any row that uses them)
//   N (2)  a name table row: index, the text (components, sockets, owner classes, placed key names; the same rule)
//   E (3)  an event: id, point, asset, a presence mask, then only the fields present (location, rotation, scale,
//          attach component, socket, key (a NetGUID or a placed name and location; neither is the world key), owner
//          class, look, seed, world location, of, flags, physical material, attenuation, concurrency)
//   R (4)  the same fields: a journal row replayed to a joining PC
//   X (5)  id: the host's spawn of that event ended (destroyed, garbage collected, or pooled again)
//   D (6)  point, id, of: a Deactivate of a journal row (one with no row goes as a full E row)
// A string is 1 to 255 bytes; an event naming a longer one is not sent (counted bad (too long)).
// A row is not sent to PC P with pawn W when the event's owner is W or P, the owner's owner is W or P, the attach
// component's owner is W, or it is a NonReplicated cue whose instigator is W: P's own client made that effect itself.
// Ownerless (no owner, no instigator): a located effect (a world location or an attach) is sent to every PC and each
// client blocks its own ownerless located copy; a 2D sound (no location, no attach) or an effect whose world context is
// a widget is local only, never sent and kept on the client (local_only).
// First person (fp_view): an effect attached to a player pawn's camera is not sent; one attached to its FirstPersonMesh
// or to a component of its weapon (weapon actors are first person only, card 029) is sent attached to the pawn's root
// component at the socket's world transform (KeepWorldPosition), keyed by the pawn.
//
// The journal holds the live spawns (a row per returned component; a pooled component spawned again replaces its row
// and ends the old one with X) and the placed components' last Activate. A Deactivate of a spawn names it (of) and
// removes its row; a Deactivate of a placed component removes its row and carries that row's id as of, so the client
// stops the copy it made. The sweep ends (X) and removes a row whose component is gone, or inactive after it was seen
// active (a finished one-shot or loop). A join replay sends the whole asset and name tables, then R rows for the journal rows
// alive and active at replay time, filtered for that PC.
//
// Client. c136_receive_fx parses the rows and plays each through the effect's original (no capture point detour), with
// c136_set_playing set so the effects it starts inside run unblocked. It keeps a live map host id -> component for the
// spawns it made, so a later Activate, Deactivate or X of that id acts on its copy. c136_client_own (the block
// decision, fx_c136.cpp) applies the send filter to the client's own pawn and PC.
//
// Lines: per map totals (always); with UBWCOOP_C136_LOG=1, capped,
//   card136: client fx <id> <point> <asset> owner <owner class> <result>

#include "ubw/hooks_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

namespace ubw::hooks {

uint64_t c41_guid(const void* actor);      // enemies.cpp
void c434_owner_row(const void* owner, const char* why, double min_far, bool fx); // world_c98.cpp
void* c41_package_map(const void* world); // enemies.cpp

C136SocketLocationFn g_c136_socket_location = nullptr; // card 136 fix 5 (kCallsUbw)
C136SocketRotationFn g_c136_socket_rotation = nullptr;

namespace {

constexpr size_t kNiagaraSeedOffset = 0x52C; // UNiagaraComponent::RandomSeedOffset (fx_c136.cpp)
constexpr size_t kCueLocation = 0x70;        // FGameplayCueParameters::Location (FVector)
constexpr size_t kCueNormal = 0x88;          // FGameplayCueParameters::Normal (FVector)
constexpr size_t kCuePhysMat = 0xB8;         // FGameplayCueParameters::PhysicalMaterial (TWeakObjectPtr)
constexpr size_t kCueParamsSize = 0x200;     // FGameplayCueParameters (about 0xD0) with room to spare
constexpr int32_t kAssets = 2048;            // asset table rows per map
constexpr int32_t kJournal = 4608;           // live spawns and placed components
// Rows queued per tick. hb62: a wave start and the map load each activated about 400 placed systems in one tick,
// past the old 512 with their end rows; a tick's rows are bounded by the journal (kJournal) and its end rows.
constexpr int32_t kQueue = 4608;
constexpr int32_t kRowBytes = 160;          // one binary row (the largest E row is about 140 bytes)
constexpr int32_t kNames = 4096;            // name table rows per map (card 229: FED_C3M1 needs about 2670)
constexpr int32_t kNameSlots = 8192;        // the host name table's hash slots (index + 1; 0 free)
constexpr int32_t kStrMax = 255;            // an asset or name row's string: 1 to 255 bytes
constexpr int32_t kSweepPerTick = 64;        // journal rows checked per tick
constexpr int32_t kClientLive = 4096;
constexpr int32_t kKeyChars = kC95TargetMax + 80; // card 230: a wire target name, then " x y z"

// Card 136 run 4 wire format (the card's "Run 4 wire format" section): rows by tag, an E row's fields by mask bit.
constexpr uint8_t kTagAsset = 1;
constexpr uint8_t kTagName = 2;
constexpr uint8_t kTagEvent = 3;
constexpr uint8_t kTagReplay = 4;
constexpr uint8_t kTagEnd = 5;
constexpr uint8_t kTagStop = 6;
constexpr uint16_t kBitLoc = 1u << 0;
constexpr uint16_t kBitRot = 1u << 1;
constexpr uint16_t kBitScale = 1u << 2;
constexpr uint16_t kBitComp = 1u << 3;
constexpr uint16_t kBitSocket = 1u << 4;
constexpr uint16_t kBitGuid = 1u << 5;
constexpr uint16_t kBitPlaced = 1u << 6;
constexpr uint16_t kBitOwnerClass = 1u << 7;
constexpr uint16_t kBitLook = 1u << 8;
constexpr uint16_t kBitSeed = 1u << 9;
constexpr uint16_t kBitWorld = 1u << 10;
constexpr uint16_t kBitOf = 1u << 11;
constexpr uint16_t kBitFlags = 1u << 12;
constexpr uint16_t kBitPhysMat = 1u << 13;
constexpr uint16_t kBitAtten = 1u << 14;
constexpr uint16_t kBitConc = 1u << 15;
constexpr uint16_t kNoAsset = 0xFFFF;
constexpr uint32_t kClientLines = 200000;
constexpr uint32_t kWarnLines = 8;
constexpr uint32_t kC304HostLines = 16; // b9: card304 host owner rows per map
constexpr int32_t kPassSlots = 13; // c136_pass_play's slot count (proj_c132.cpp kC132PassSlots)
constexpr uint8_t kAttachKeepRelative = 0; // EAttachLocation::KeepRelativeOffset
constexpr uint8_t kAttachKeepWorld = 1;    // EAttachLocation::KeepWorldPosition

bool is_spawn(C136Point p) {
    return p >= C136Point::Niagara && p <= C136Point::NiagaraAttachedParams;
}
bool is_decal(C136Point p) {
    return p == C136Point::DecalAt || p == C136Point::DecalAttached;
}
bool is_cue(C136Point p) {
    return p == C136Point::Cue || p == C136Point::CueContext || p == C136Point::CueNonReplicated;
}
bool is_stop(C136Point p) {
    return p == C136Point::NiagaraDeactivate || p == C136Point::NiagaraDeactivateImmediate;
}

// The send filter's inputs, kept with each row (compared by address only, never dereferenced).
struct FxFilter {
    const void* owner;
    const void* owner_owner;
    const void* attach_owner;
    const void* instigator;
};

FxFilter make_filter(const C136Call& c, const void* owner, const void* instigator) {
    FxFilter f{};
    f.owner = owner;
    f.owner_owner = owner != nullptr ? read_ptr(owner, kActorOwner) : nullptr;
    f.attach_owner = c.attach != nullptr ? read_ptr(c.attach, kComponentOwner) : nullptr;
    f.instigator = instigator;
    return f;
}

// True when the PC with this pawn made the effect itself (the row is not sent to it; on the client, it is kept).
// Run 6 (card item 2): by instigator for every capture kind when the event has one, so an effect on the client's pawn
// that someone else made (an archer's needle impact cue, hb65 19938) is sent. The owner rule is for events with none.
bool filter_hits(const FxFilter& f, const void* pc, const void* pawn) {
    if (f.instigator != nullptr) {
        return (pawn != nullptr && f.instigator == pawn) || (pc != nullptr && f.instigator == pc);
    }
    const bool by_pawn = pawn != nullptr && (f.owner == pawn || f.owner_owner == pawn || f.attach_owner == pawn);
    const bool by_pc = pc != nullptr && (f.owner == pc || f.owner_owner == pc);
    return by_pawn || by_pc;
}

// Run 6 fix C (card item 4): a trail component op on a projectile or a gore part. Each side has its own copy of the
// actor (the card 132 relay copy, the client's own gore parts), so each side runs its own trail; a host row would
// draw a static trail at the world point. Never sent; kept on the client.
bool trail_local(const C136Call& c, const void* owner) {
    const bool op = c.point == C136Point::NiagaraActivate || is_stop(c.point);
    if (!op || owner == nullptr || g_projectile_class == nullptr || g_gore_part_class == nullptr) {
        return false;
    }
    return is_a(owner, g_projectile_class()) || is_a(owner, g_gore_part_class());
}

// Card 438 step 2: the wound shading system (/Game/WoundShading/NS_Compute_DamageAndRadius) is a GPU compute
// simulation with no renderer; its caller feeds it the hit positions through user variables right after the spawn
// (exe+0x4BBA724), which a row does not carry. A host row therefore drew nothing on the client and cost 757 rows,
// 19936 bytes per C2M1 pass (card 329's heaviest asset), while the client's own spawn, with the right variables, was
// blocked. Each side runs its own: spawns and component ops of this asset are never sent and are kept on the client.
// The asset's name index is cached after the first match (one name read per event until then).
bool wound_local(const C136Call& c) {
    static int32_t wound_name = 0; // FName comparison index of the asset, 0 until seen (only ever one value written)
    if (c.asset == nullptr) {
        return false;
    }
    const int32_t name = read_i32(c.asset, kObjectName);
    if (wound_name == 0 && strcmp(object_name(c.asset).text, "NS_Compute_DamageAndRadius") == 0) {
        wound_name = name;
    }
    return wound_name != 0 && name == wound_name;
}

// The ownerless rule's local case: no owner, no instigator, and no world place (a sound with no location and no
// attach), or a widget world context. Never sent; kept on the client.
bool local_only(const C136Call& c, const void* owner, const void* instigator) {
    if (owner != nullptr || instigator != nullptr) {
        return false;
    }
    const bool ui = c.context != nullptr && g_user_widget_class != nullptr && is_a(c.context, g_user_widget_class());
    return ui || (!c.located && c.attach == nullptr && c.component == nullptr);
}

enum class FpKind : uint8_t { None, Remap, Drop };
constexpr size_t kPawnFirstPersonMesh = 0x848; // ASTPlayerCharacter::FirstPersonMesh (remote_body.cpp)

// A player pawn's first-person effect: Drop for one on its camera (screen effects) or a component op on its first
// person parts (no third-person twin to act on), Remap for one attached to its FirstPersonMesh or its weapon.
FpKind fp_view(const C136Call& c, const void** pawn_out) {
    const void* const comp = c.attach != nullptr ? c.attach : c.component;
    const void* const actor = comp != nullptr ? read_ptr(comp, kComponentOwner) : nullptr;
    if (actor == nullptr || g_player_mi_class == nullptr) {
        return FpKind::None;
    }
    const void* const player = g_player_mi_class();
    const bool on_pawn = is_a(actor, player);
    const void* const pawn = on_pawn ? actor : read_ptr(actor, kActorOwner); // a weapon: its owner
    if (pawn == nullptr || !is_a(pawn, player)) {
        return FpKind::None;
    }
    *pawn_out = pawn;
    if (comp == read_ptr(pawn, kCharacterCamera)) {
        return FpKind::Drop;
    }
    const bool fp = !on_pawn || comp == read_ptr(pawn, kPawnFirstPersonMesh);
    if (!fp) {
        return FpKind::None;
    }
    return c.attach != nullptr ? FpKind::Remap : FpKind::Drop;
}

Vec3 vec_add(const Vec3& a, const Vec3& b) {
    return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

// A rotator (pitch, yaw, roll in degrees, the Vec3's x, y, z) as its rotation matrix's X, Y, Z axes (FRotationMatrix).
struct Axes {
    Vec3 x, y, z;
};
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

Axes rot_axes(const Vec3& r) {
    const double sp = sin(r.x * kDegToRad), cp = cos(r.x * kDegToRad);
    const double sy = sin(r.y * kDegToRad), cy = cos(r.y * kDegToRad);
    const double sr = sin(r.z * kDegToRad), cr = cos(r.z * kDegToRad);
    return Axes{Vec3{cp * cy, cp * sy, sp}, Vec3{sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp},
                Vec3{-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp}};
}

// The local vector `v` in the axes' world frame.
Vec3 axes_apply(const Axes& a, const Vec3& v) {
    return Vec3{v.x * a.x.x + v.y * a.y.x + v.z * a.z.x, v.x * a.x.y + v.y * a.y.y + v.z * a.z.y,
                v.x * a.x.z + v.y * a.y.z + v.z * a.z.z};
}

double dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// The rotator of a rotation matrix's axes (FMatrix::Rotator).
Vec3 axes_rotator(const Axes& a) {
    const double pitch = atan2(a.x.z, sqrt(a.x.x * a.x.x + a.x.y * a.x.y)) / kDegToRad;
    const double yaw = atan2(a.x.y, a.x.x) / kDegToRad;
    const Axes no_roll = rot_axes(Vec3{pitch, yaw, 0.0});
    const double roll = atan2(dot(a.z, no_roll.y), dot(a.y, no_roll.y)) / kDegToRad;
    return Vec3{pitch, yaw, roll};
}

// The world transform of a relative (location, rotation) under a parent transform: parent_at + R(parent)·at, and
// R(parent)·R(rot) as a rotator. Scale is not applied (the attached effects' sockets carry unit scale).
void compose(const Vec3& parent_at, const Vec3& parent_rot, const Vec3& at, const Vec3& rot, Vec3& out_at,
             Vec3& out_rot) {
    const Axes p = rot_axes(parent_rot);
    const Axes r = rot_axes(rot);
    out_at = vec_add(parent_at, axes_apply(p, at));
    out_rot = axes_rotator(Axes{axes_apply(p, r.x), axes_apply(p, r.y), axes_apply(p, r.z)});
}

// --- Binary rows ---------------------------------------------------------------------------------------------

// One row being written. A write past the end sets bad and writes nothing (the row is then not sent).
struct Bytes {
    int32_t n;
    bool bad;
    uint8_t b[kRowBytes];
};

void put_raw(Bytes& o, const void* v, int32_t n) {
    if (o.bad || n < 0 || o.n + n > kRowBytes) {
        o.bad = true;
        return;
    }
    memcpy(o.b + o.n, v, static_cast<size_t>(n));
    o.n += n;
}
void put_u8(Bytes& o, uint8_t v) {
    put_raw(o, &v, 1);
}
void put_u16(Bytes& o, uint16_t v) {
    const uint8_t b[2] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)};
    put_raw(o, b, 2);
}
void put_varint(Bytes& o, uint64_t v) {
    for (int i = 0; i < 10; ++i) {
        const uint8_t low = static_cast<uint8_t>(v & 0x7Fu);
        v >>= 7;
        put_u8(o, static_cast<uint8_t>(low | (v != 0 ? 0x80u : 0u)));
        if (v == 0) {
            return;
        }
    }
}
void put_f32(Bytes& o, double v) {
    const float f = static_cast<float>(v);
    put_raw(o, &f, 4); // x64: little-endian, as the reader expects
}
void put_vec(Bytes& o, const Vec3& v) {
    put_f32(o, v.x);
    put_f32(o, v.y);
    put_f32(o, v.z);
}
// A rotator component: round(deg * 65536 / 360) & 0xFFFF.
void put_angle(Bytes& o, double deg) {
    put_u16(o, static_cast<uint16_t>(static_cast<int64_t>(lround(deg * 65536.0 / 360.0)) & 0xFFFF));
}
// A cue Normal component: int16 round(v * 32767), clamped to [-1, 1].
void put_unit(Bytes& o, double v) {
    const double c = v < -1.0 ? -1.0 : (v > 1.0 ? 1.0 : v);
    put_u16(o, static_cast<uint16_t>(static_cast<int16_t>(lround(c * 32767.0))));
}
void put_string(Bytes& o, const char* text, int32_t len) {
    if (len < 1 || len > kStrMax) {
        o.bad = true;
        return;
    }
    put_u8(o, static_cast<uint8_t>(len));
    put_raw(o, text, len);
}

// One frame being read. A read past the end sets bad and returns 0.
struct Reader {
    const uint8_t* b;
    int32_t n;
    int32_t at;
    bool bad;
};

bool get_raw(Reader& r, void* v, int32_t n) {
    if (r.bad || r.at + n > r.n) {
        r.bad = true;
        memset(v, 0, static_cast<size_t>(n));
        return false;
    }
    memcpy(v, r.b + r.at, static_cast<size_t>(n));
    r.at += n;
    return true;
}
uint8_t get_u8(Reader& r) {
    uint8_t v = 0;
    get_raw(r, &v, 1);
    return v;
}
uint16_t get_u16(Reader& r) {
    uint8_t b[2] = {};
    get_raw(r, b, 2);
    return static_cast<uint16_t>(b[0] | (b[1] << 8));
}
uint64_t get_varint(Reader& r) {
    uint64_t v = 0;
    for (int i = 0; i < 10; ++i) {
        const uint8_t b = get_u8(r);
        v |= static_cast<uint64_t>(b & 0x7Fu) << (7 * i);
        if ((b & 0x80u) == 0 || r.bad) {
            return v;
        }
    }
    r.bad = true; // more than 10 bytes: not a varint this format writes
    return 0;
}
double get_f32(Reader& r) {
    float f = 0.0f;
    get_raw(r, &f, 4);
    return static_cast<double>(f);
}
Vec3 get_vec(Reader& r) {
    Vec3 v{};
    v.x = get_f32(r);
    v.y = get_f32(r);
    v.z = get_f32(r);
    return v;
}
double get_angle(Reader& r) {
    const double deg = get_u16(r) * 360.0 / 65536.0;
    return deg > 180.0 ? deg - 360.0 : deg;
}
double get_unit(Reader& r) {
    return static_cast<int16_t>(get_u16(r)) / 32767.0;
}

bool is_zero(const Vec3& v) {
    return v.x == 0.0 && v.y == 0.0 && v.z == 0.0;
}
bool is_one(const Vec3& v) {
    return v.x == 1.0 && v.y == 1.0 && v.z == 1.0;
}

// --- Host ------------------------------------------------------------------------------------------------------

struct HostAsset {
    const void* object; // null for a cue tag, or a dead row (object and tag both 0: never matched)
    C31Weak weak;       // the object's index and serial: a reused address is a new object, so a new row
    uint64_t tag;
    char text[kAssetPathChars];
};
struct HostAssets {
    int32_t n;
    HostAsset rows[kAssets];
};

// The outermost event between its queue and its done: the fields read while the capture point's arguments live.
struct Pending {
    bool active;
    uint32_t id;
    C136Point point;
    uint8_t flags;
    uint32_t of;
    int32_t asset;
    int32_t physmat;
    int32_t atten; // sound attenuation override asset row, -1 none
    int32_t conc;  // sound concurrency override asset row, -1 none
    const void* component; // point 6: the component acted on
    FxFilter filter;
    Vec3 at, rot, scale, world;
    float look[3];
    bool attached;       // an attached point: the world location goes too (the client's key-miss fallback)
    bool too_long;       // a name over kStrMax bytes: the event is not sent ("bad (too long)")
    int32_t comp;        // name rows, -1 none: the attach or acted-on component, the socket, the owner's class
    int32_t socket;
    int32_t owner_class;
    uint64_t guid;       // key: the replicated owner's NetGUID, 0 none
    int32_t placed;      // key: the placed owner's wire name row, -1 none
    Vec3 placed_at;
};

// Card 395: the name rows an E or R row's body names (comp, socket, placed, owner class), -1 none. A peer is sent
// a name row only ahead of the first row for it that names it.
constexpr int32_t kRowNames = 4;
struct NameRefs {
    int32_t i[kRowNames];
};
constexpr NameRefs kNoNames{{-1, -1, -1, -1}};

// The name rows host_body writes for this event (placed only without a guid, as body_mask).
NameRefs name_refs(const Pending& p) {
    return NameRefs{{p.comp, p.socket, p.guid != 0 ? -1 : p.placed, p.owner_class}};
}

// Card 329 B: what a sent row is, for the map-end rows: a one-shot event, an event kept in the journal (and its
// join replay rows), or an end or stop of a journal row.
enum class FxKind : uint8_t { OneShot, Journal, End, Count };
constexpr const char* kFxKindText[] = {"one-shot", "journal", "end"};
struct FxMeta {
    int32_t asset; // host asset row, -1 none
    Vec3 at;       // world location, zero: none (the band is "no location")
    FxKind kind;
    uint32_t id; // a journal row's E or R row: that row's id (its send is marked per PC), 0 none
    uint32_t of; // an end row (D or X): the journal row it ends, 0 none (a stop sent as a full E row)
};

struct QueueRow {
    FxFilter filter;
    Bytes row; // a whole row, tag first
    FxMeta meta;
    NameRefs names; // card 395
};
struct HostQueue {
    int32_t n;
    QueueRow rows[kQueue];
};

struct JournalRow {
    uint32_t id;
    const void* comp;
    C31Weak weak;
    bool spawn; // a point 2 spawn; else a placed component's last Activate
    bool seen_active; // the sweep or the spawn's own bAutoActivate saw it active (an inactive row before is kept)
    bool runtime_target; // its target actor was spawned at run time and does not replicate: not replayed on a join
    FxFilter filter;
    Bytes body; // the E row's fields after the tag
    int32_t asset; // card 329 B: the event's asset row and world location, for the end and replay rows' counts
    Vec3 at;
    NameRefs names; // card 395: the name rows the body names
};
struct Journal {
    int32_t n;
    int32_t peak;
    int32_t cursor;
    JournalRow rows[kJournal];
};

struct HostName {
    uint32_t hash;
    int32_t len;
    char text[kStrMax + 1];
};
struct HostNames {
    int32_t n;
    int16_t slots[kNameSlots]; // row index + 1 by hash, linear probing; 0 free
    HostName rows[kNames];
};

struct Peer {
    int32_t key; // object_index(pc) + 1; 0: free
    int32_t table_sent;
    uint32_t names_had[kNames / 32]; // card 395: bit per name row this peer was sent in a frame that went out
    char name[64]; // card 329 B: the PC's name when the slot was taken, for the map-end rows
};

struct HostCounts { // per map
    uint32_t queued, rows_sent, frames, frames_failed, off_thread, overflow, assets_full, journal_full, gore, bad_rows,
        ended, replays, replay_rows, replay_skipped, assets_stale, local, fp_remap, fp_drop, fp_miss, finished, too_long,
        names_full, event_rows, trail, no_asset, c304_lines, attack, wound;
    uint64_t event_bytes; // E and R rows sent, tag included (per peer)
};

// Card 329 B (diagnostic, host, per map): rows and bytes sent to all PCs by asset, and by kind and distance band from
// the receiving PC's pawn (kFxBands bands, then "no location").
constexpr int32_t kFxBands = 4;
constexpr double kFxBandEdge[kFxBands - 1] = {3000.0, 8000.0, 20000.0};
constexpr const char* kFxBandText[kFxBands + 1] = {"under 3000", "3000 to 8000", "8000 to 20000", "over 20000",
                                                   "no location"};
constexpr int32_t kFxTopAssets = 20;
struct FxSent {
    uint32_t rows;
    uint64_t bytes;
};
struct FxStats { // per peer slot (g_host.peers index)
    FxSent asset[kAssets];
    FxSent no_asset;
    FxSent band[static_cast<int>(FxKind::Count)][kFxBands + 1];
    uint32_t end_sent;      // end rows of a journal row this PC was sent
    uint32_t end_never;     // end rows of a journal row this PC was never sent
    uint32_t end_untracked; // end rows whose journal row's slot in the sent table was taken by a later row
    uint32_t end_no_row;    // stops sent as full E rows (no journal row named)
};
// Card 329 B: which peer slots were sent a journal row's E or R row, by row id (slot id & mask; a slot names its id,
// so a later row that takes the slot makes the older row untracked rather than wrong).
constexpr uint32_t kFxSentSlots = 16384;
struct FxSentMark {
    uint32_t id;
    uint8_t peers; // bit per g_host.peers index
};

struct HostNet {
    Pending pending;
    HostAssets assets;
    HostNames names;
    HostQueue queue;
    Journal journal;
    Peer peers[kC17Slots];
    HostCounts counts;
    FxStats stats[kC17Slots];        // card 329 B, by peer slot
    FxSentMark sent[kFxSentSlots]; // card 329 B
    uint32_t warn_lines;
};
HostNet g_host{}; // game thread only

void host_warn_once(const char* what, uint32_t value) {
    if (g_host.warn_lines < kWarnLines) {
        ++g_host.warn_lines;
        log::line("WARN card136: host fx %s (%u)", what, value);
    }
}

// The asset table index of an object (or, object null, a cue tag); -1 for none or a full table.
int32_t host_asset(const void* object, uint64_t tag) {
    if (object == nullptr && tag == 0) {
        return -1;
    }
    HostAssets& a = g_host.assets;
    for (int32_t i = 0; i < a.n; ++i) {
        HostAsset& r = a.rows[i];
        if (r.object != object || (object == nullptr && r.tag != tag)) {
            continue;
        }
        if (object != nullptr && (g_weak_get == nullptr || g_weak_get(&r.weak) != object)) {
            r.object = nullptr; // freed and the address reused: this row is dead, the object gets a new one
            r.tag = 0;
            ++g_host.counts.assets_stale;
            continue;
        }
        return i;
    }
    if (a.n == kAssets) {
        ++g_host.counts.assets_full;
        host_warn_once("asset table full", static_cast<uint32_t>(kAssets));
        return -1;
    }
    HostAsset& r = a.rows[a.n];
    r.object = object;
    r.weak = C31Weak{};
    if (object != nullptr && g_weak_assign != nullptr) {
        void* const o = const_cast<void*>(object);
        g_weak_assign(&r.weak, &o);
    }
    r.tag = object != nullptr ? 0 : tag;
    snprintf(r.text, sizeof(r.text), "%s", object != nullptr ? c74_asset_path(object).text : fname_text(&tag).text);
    return a.n++;
}

uint32_t name_hash(const char* text, int32_t len) {
    uint32_t h = 2166136261u; // FNV-1a
    for (int32_t i = 0; i < len; ++i) {
        h = (h ^ static_cast<uint8_t>(text[i])) * 16777619u;
    }
    return h;
}

// The name table row of a string (1 to kStrMax bytes), added on first use; -1 for a full table.
int32_t host_name(const char* text, int32_t len) {
    HostNames& t = g_host.names;
    const uint32_t h = name_hash(text, len);
    uint32_t slot = h % kNameSlots;
    for (int32_t k = 0; k < kNameSlots; ++k, slot = (slot + 1) % kNameSlots) {
        const int32_t i = t.slots[slot] - 1;
        if (i < 0) {
            break;
        }
        const HostName& r = t.rows[i];
        if (r.hash == h && r.len == len && memcmp(r.text, text, static_cast<size_t>(len)) == 0) {
            return i;
        }
    }
    if (t.n == kNames || t.slots[slot] != 0) {
        ++g_host.counts.names_full;
        host_warn_once("name table full", static_cast<uint32_t>(kNames));
        return -1;
    }
    HostName& r = t.rows[t.n];
    r.hash = h;
    r.len = len;
    memcpy(r.text, text, static_cast<size_t>(len));
    r.text[len] = '\0';
    t.slots[slot] = static_cast<int16_t>(t.n + 1);
    return t.n++;
}

// The name row of an FName (an object's name is the FName at object + kObjectName), read at its full length: -1 for
// None or a full table, and a name over kStrMax bytes marks the event too long instead of being cut.
int32_t host_fname(Pending& p, const void* fname) {
    char text[kStrMax + 2];
    const uint32_t len = fname_into(fname, text, sizeof(text));
    if (len == 0 || strcmp(text, "None") == 0) {
        return -1;
    }
    if (len > static_cast<uint32_t>(kStrMax)) {
        p.too_long = true;
        return -1;
    }
    return host_name(text, static_cast<int32_t>(len));
}

// Card 425: the object's name with its number (c50_name): the client finds a component by StaticFindObject
// (c95_component), and a name without its number finds another component or none.
int32_t host_object_name(Pending& p, const void* object) {
    if (object == nullptr) {
        return -1;
    }
    char text[kStrMax + 1];
    const int32_t len = c50_name(object, text, sizeof(text));
    if (len <= 0) {
        p.too_long = true; // over kStrMax, or not printable: the row is refused as for a long name
        return -1;
    }
    return strcmp(text, "None") == 0 ? -1 : host_name(text, len);
}

// The owner's key: its NetGUID for a replicated owner, its wire name and anchor location for a placed one, else the
// world key (neither set).
void host_key(Pending& p, const char* kind, const void* owner) {
    bool placed = strcmp(kind, "placed") == 0;
    if (strcmp(kind, "guid") == 0) {
        p.guid = c41_guid(owner);
        // Card 325: a replicated placed owner has no NetGUID until a client has been sent it (map load, before the
        // first join: the troop dropships' ground smoke rows went out with no key and the join replay drew each at
        // its world point). Such a row takes the placed key.
        placed = p.guid == 0 && c50_placed(owner);
    }
    char target[kKeyChars];
    if (!placed || c95_wire_target(owner, target, sizeof(target)) <= 0) {
        return;
    }
    // "name x y z": the name may hold spaces, the three numbers do not.
    int32_t cut = static_cast<int32_t>(strlen(target));
    for (int k = 0; k < 3 && cut > 0; ++k) {
        do {
            --cut;
        } while (cut > 0 && target[cut] != ' ');
    }
    if (cut <= 0 || sscanf_s(target + cut + 1, "%lf %lf %lf", &p.placed_at.x, &p.placed_at.y, &p.placed_at.z) != 3) {
        host_warn_once("placed key does not parse", 0);
        return;
    }
    target[cut] = '\0';
    p.placed = host_name(target, cut);
}

// A first-person attached effect, sent attached to the pawn's root component (the third-person body every machine
// shows) at the socket's world transform: KeepWorldPosition, key the pawn's NetGUID. KeepRelativeOffset composes the
// relative location and rotation with the socket's transform (compose); the snap types drop them. Without a GUID or
// the socket calls it goes at world.
void host_fp_remap(Pending& p, const C136Call& c, const void* pawn) {
    const uint8_t loc = static_cast<uint8_t>((c.flags >> kC136FlagLocationShift) & kC136FlagLocationMask);
    Vec3 at = c.at;
    Vec3 rot = c.rot;
    if (loc != kAttachKeepWorld && g_c136_socket_location != nullptr && g_c136_socket_rotation != nullptr) {
        Vec3 socket_at{};
        Vec3 socket_rot{};
        g_c136_socket_location(c.attach, &socket_at, c.socket);
        g_c136_socket_rotation(c.attach, &socket_rot, c.socket);
        at = socket_at;
        rot = socket_rot;
        if (loc == kAttachKeepRelative) {
            compose(socket_at, socket_rot, c.at, c.rot, at, rot);
        }
    }
    p.at = at;
    p.rot = rot;
    p.world = at;
    p.attached = true;
    p.flags = static_cast<uint8_t>((p.flags & ~(kC136FlagLocationMask << kC136FlagLocationShift)) |
                                   (kAttachKeepWorld << kC136FlagLocationShift));
    const void* const root = read_ptr(pawn, kActorRootComponent);
    const uint64_t guid = c41_guid(pawn);
    p.placed = -1;
    if (root == nullptr || guid == 0) {
        ++g_host.counts.fp_miss;
        p.comp = -1;
        p.guid = 0;
    } else {
        ++g_host.counts.fp_remap;
        p.comp = host_object_name(p, root);
        p.guid = guid;
    }
    p.socket = -1;
}

void host_enqueue(const FxFilter& f, const Bytes& row, const FxMeta& meta, const NameRefs& names) {
    HostQueue& q = g_host.queue;
    if (q.n == kQueue) {
        ++g_host.counts.overflow;
        host_warn_once("queue full this tick, rows dropped", static_cast<uint32_t>(kQueue));
        return;
    }
    q.rows[q.n].filter = f;
    q.rows[q.n].row = row;
    q.rows[q.n].meta = meta;
    q.rows[q.n].names = names;
    ++q.n;
    ++g_host.counts.queued;
}

void host_enqueue_end(const JournalRow& r) {
    Bytes row{};
    put_u8(row, kTagEnd);
    put_varint(row, r.id);
    host_enqueue(r.filter, row, FxMeta{r.asset, r.at, FxKind::End, 0, r.id}, kNoNames);
    ++g_host.counts.ended;
}

bool journal_alive(const JournalRow& r) {
    return g_weak_get != nullptr && g_weak_get(&r.weak) == r.comp;
}

// UActorComponent::bIsActive of a journal component (all are UNiagaraComponents): 1, 0, or -1 (property not found).
// The property is found once by reflection and kept.
int journal_active(const void* comp) {
    static const uint8_t* prop = nullptr; // game thread only
    static bool tried = false;
    if (!tried) {
        tried = true;
        static constexpr const char* kNames[] = {"bIsActive", nullptr};
        const uint8_t* const p = c27_find_any(read_ptr(comp, kObjectClass), kNames);
        prop = prop_kind_is(p, "BoolProperty") && prop_bool_ok(p) ? p : nullptr;
        if (prop == nullptr) {
            log::line("WARN card136: host fx bIsActive not found on %s: finished rows end only when destroyed",
                      object_name(read_ptr(comp, kObjectClass)).text);
        }
    }
    return prop != nullptr ? (c29_flag(static_cast<const uint8_t*>(comp), prop) ? 1 : 0) : -1;
}

int32_t journal_find_comp(const void* comp) {
    const Journal& j = g_host.journal;
    for (int32_t i = 0; i < j.n; ++i) {
        if (j.rows[i].comp == comp) {
            return i;
        }
    }
    return -1;
}

int32_t journal_find_id(uint32_t id) {
    const Journal& j = g_host.journal;
    for (int32_t i = 0; i < j.n; ++i) {
        if (j.rows[i].id == id) {
            return i;
        }
    }
    return -1;
}

void journal_remove(int32_t i) {
    Journal& j = g_host.journal;
    j.rows[i] = j.rows[j.n - 1];
    --j.n;
}

// The actor a client copy of this row hangs on: the attach parent's owner, or for a placed Activate the component's
// owner. A world spawn has none. Read while the event's objects are live (from c136_net_done).
const void* row_target(const Pending& p, bool spawn) {
    if (p.filter.attach_owner != nullptr) {
        return p.filter.attach_owner;
    }
    return !spawn && p.component != nullptr ? read_ptr(p.component, kComponentOwner) : nullptr;
}

// Card 136 journal rule: a target spawned at run time (not loaded with the map) that does not replicate cannot exist
// on a client that loaded the map again, so its row is not replayed (gib trails on STGorePart actors).
bool runtime_target(const Pending& p, bool spawn) {
    const void* const target = row_target(p, spawn);
    return target != nullptr && !c50_placed(target) && !c98_replicates(target);
}

void journal_set(JournalRow& r, const Pending& p, const void* comp, bool spawn, const Bytes& body) {
    r.id = p.id;
    r.comp = comp;
    r.weak = C31Weak{};
    if (g_weak_assign != nullptr) {
        void* const object = const_cast<void*>(comp);
        g_weak_assign(&r.weak, &object);
    }
    r.spawn = spawn;
    r.seen_active = !spawn || (p.flags & kC136FlagAutoActivate) != 0;
    r.filter = p.filter;
    r.runtime_target = runtime_target(p, spawn);
    r.body = body;
    r.asset = p.asset;
    r.at = p.world;
    r.names = name_refs(p);
}

// Returns true when the row is kept (card 329 B counts the event as a journal row).
bool journal_upsert(const Pending& p, const void* comp, bool spawn, const Bytes& body) {
    Journal& j = g_host.journal;
    const int32_t at = journal_find_comp(comp);
    if (at >= 0) {
        if (j.rows[at].spawn) {
            host_enqueue_end(j.rows[at]); // a pooled component spawned again: the old spawn ended
        }
        journal_set(j.rows[at], p, comp, spawn, body);
        return true;
    }
    if (j.n == kJournal) {
        ++g_host.counts.journal_full;
        host_warn_once("journal full (peak = size), row not kept", static_cast<uint32_t>(j.peak));
        return false;
    }
    journal_set(j.rows[j.n], p, comp, spawn, body);
    ++j.n;
    j.peak = j.n > j.peak ? j.n : j.peak;
    return true;
}

// The journal after the event: a spawn adds its row, a placed Activate updates its row, a Deactivate removes the row.
// Returns true when the event is kept as a journal row.
bool journal_apply(const Pending& p, const void* made, const Bytes& body) {
    if (is_spawn(p.point)) {
        return made != nullptr && journal_upsert(p, made, true, body);
    }
    if (p.point == C136Point::NiagaraActivate) {
        return p.of == 0 && p.component != nullptr && journal_upsert(p, p.component, false, body);
    }
    if (!is_stop(p.point)) {
        return false;
    }
    const int32_t at = p.of != 0 ? journal_find_id(p.of) : journal_find_comp(p.component);
    if (at >= 0) {
        journal_remove(at);
    }
    return false;
}

// A Deactivate of a placed component names the row of its last Activate, so the client stops the copy it made.
uint32_t host_stop_of(const Pending& p) {
    if (!is_stop(p.point) || p.of != 0 || p.component == nullptr) {
        return p.of;
    }
    const int32_t at = journal_find_comp(p.component);
    return at >= 0 && !g_host.journal.rows[at].spawn ? g_host.journal.rows[at].id : 0u;
}

uint16_t body_mask(const Pending& p, int32_t seed, uint32_t of) {
    const Vec3 look{p.look[0], p.look[1], p.look[2]};
    uint16_t m = 0;
    m |= !is_zero(p.at) ? kBitLoc : 0;
    m |= !is_zero(p.rot) ? kBitRot : 0;
    m |= !is_one(p.scale) ? kBitScale : 0;
    m |= p.comp >= 0 ? kBitComp : 0;
    m |= p.socket >= 0 ? kBitSocket : 0;
    m |= p.guid != 0 ? kBitGuid : (p.placed >= 0 ? kBitPlaced : 0);
    m |= p.owner_class >= 0 ? kBitOwnerClass : 0;
    m |= !is_zero(look) ? kBitLook : 0;
    m |= seed != 0 ? kBitSeed : 0;
    m |= p.attached ? kBitWorld : 0;
    m |= of != 0 ? kBitOf : 0;
    m |= p.flags != 0 ? kBitFlags : 0;
    m |= p.physmat >= 0 ? kBitPhysMat : 0;
    m |= p.atten >= 0 ? kBitAtten : 0;
    m |= p.conc >= 0 ? kBitConc : 0;
    return static_cast<uint16_t>(m);
}

// An E row's fields after the tag (the card's run 4 wire format): id, point, asset, mask, then the present fields in
// mask-bit order. False when it does not fit (not expected: the largest is about 140 bytes).
bool host_body(const Pending& p, int32_t seed, uint32_t of, Bytes& o) {
    o = Bytes{};
    const uint16_t m = body_mask(p, seed, of);
    put_varint(o, p.id);
    put_u8(o, static_cast<uint8_t>(p.point));
    put_u16(o, p.asset >= 0 ? static_cast<uint16_t>(p.asset) : kNoAsset);
    put_u16(o, m);
    if ((m & kBitLoc) != 0) {
        put_vec(o, p.at);
    }
    if ((m & kBitRot) != 0) {
        void (*const put)(Bytes&, double) = is_cue(p.point) ? put_unit : put_angle; // a cue carries its Normal
        put(o, p.rot.x);
        put(o, p.rot.y);
        put(o, p.rot.z);
    }
    if ((m & kBitScale) != 0) {
        put_vec(o, p.scale);
    }
    if ((m & kBitComp) != 0) {
        put_varint(o, static_cast<uint32_t>(p.comp));
    }
    if ((m & kBitSocket) != 0) {
        put_varint(o, static_cast<uint32_t>(p.socket));
    }
    if ((m & kBitGuid) != 0) {
        put_varint(o, p.guid);
    }
    if ((m & kBitPlaced) != 0) {
        put_varint(o, static_cast<uint32_t>(p.placed));
        put_vec(o, p.placed_at);
    }
    if ((m & kBitOwnerClass) != 0) {
        put_varint(o, static_cast<uint32_t>(p.owner_class));
    }
    if ((m & kBitLook) != 0) {
        put_vec(o, Vec3{p.look[0], p.look[1], p.look[2]});
    }
    if ((m & kBitSeed) != 0) {
        put_raw(o, &seed, 4);
    }
    if ((m & kBitWorld) != 0) {
        put_vec(o, p.world);
    }
    if ((m & kBitOf) != 0) {
        put_varint(o, of);
    }
    if ((m & kBitFlags) != 0) {
        put_u8(o, p.flags);
    }
    if ((m & kBitPhysMat) != 0) {
        put_varint(o, static_cast<uint32_t>(p.physmat));
    }
    if ((m & kBitAtten) != 0) {
        put_varint(o, static_cast<uint32_t>(p.atten));
    }
    if ((m & kBitConc) != 0) {
        put_varint(o, static_cast<uint32_t>(p.conc));
    }
    return !o.bad;
}

// A whole row: the tag, then a body.
Bytes tagged(uint8_t tag, const Bytes& body) {
    Bytes row{};
    put_u8(row, tag);
    put_raw(row, body.b, body.n);
    return row;
}

void journal_sweep() {
    Journal& j = g_host.journal;
    for (int32_t k = 0; k < kSweepPerTick && j.n > 0; ++k) {
        if (j.cursor >= j.n) {
            j.cursor = 0;
        }
        JournalRow& r = j.rows[j.cursor];
        const bool alive = journal_alive(r);
        const int active = alive ? journal_active(r.comp) : 0;
        r.seen_active = r.seen_active || active == 1;
        if (alive && (active != 0 || !r.seen_active)) {
            ++j.cursor;
            continue;
        }
        g_host.counts.finished += alive ? 1u : 0u;
        host_enqueue_end(r);
        journal_remove(j.cursor); // the last row moved here: checked next
    }
}

// One PC's blob frames: whole rows back to back up to kChanBlobBytes.
// Card 395: the name rows in the frame are marked had by the peer only once the frame went out.
constexpr int32_t kFrameNames = kChanBlobBytes / 4 + 1; // a name row is at least 4 bytes
struct Frame {
    void* pc;
    Peer* peer;
    int32_t len;
    int32_t names_n;   // name rows in this frame, not yet marked
    int32_t names_out; // name rows in frames that went out (the join replay line)
    int16_t names[kFrameNames];
    uint32_t names_in[kNames / 32]; // bit per name row in names[]
    uint8_t bytes[kChanBlobBytes];
};

bool bit_has(const uint32_t* bits, int32_t i) {
    return (bits[i >> 5] >> (i & 31) & 1u) != 0;
}

void frame_send(Frame& fr) {
    if (fr.len == 0) {
        return;
    }
    static wchar_t wire[kChanBlobFrame]; // game thread only
    const WireString w = chan_blob_frame(static_cast<wchar_t>(ChanOp::Fx), fr.bytes, fr.len, wire);
    const bool sent = w.num > 0 && chan_send_client(fr.pc, w);
    ++(sent ? g_host.counts.frames : g_host.counts.frames_failed);
    for (int32_t k = 0; k < fr.names_n; ++k) {
        const int32_t i = fr.names[k];
        fr.names_in[i >> 5] &= ~(1u << (i & 31));
        if (sent && fr.peer != nullptr) {
            fr.peer->names_had[i >> 5] |= 1u << (i & 31);
        }
    }
    fr.names_out += sent ? fr.names_n : 0;
    fr.names_n = 0;
    fr.len = 0;
}

bool frame_add(Frame& fr, const Bytes& row) {
    if (row.bad || row.n == 0 || row.n > kChanBlobBytes) {
        ++g_host.counts.bad_rows;
        return false;
    }
    if (fr.len + row.n > kChanBlobBytes) {
        frame_send(fr);
    }
    memcpy(fr.bytes + fr.len, row.b, static_cast<size_t>(row.n));
    fr.len += row.n;
    ++g_host.counts.rows_sent;
    if (row.b[0] == kTagEvent || row.b[0] == kTagReplay) {
        ++g_host.counts.event_rows;
        g_host.counts.event_bytes += static_cast<uint64_t>(row.n);
    }
    return true;
}

// Card 395: `row` behind the name rows it names that this frame's peer has not had. The name rows and the row go into
// one frame (a fuller frame is sent first), so the row never reaches the peer ahead of its names; a name row is
// marked had only when its frame went out (frame_send). False, with nothing added, for a row that cannot go.
bool frame_add_named(Frame& fr, const NameRefs& names, const Bytes& row) {
    static Bytes name_rows[kRowNames]; // game thread only
    int32_t idx[kRowNames];
    int32_t n = 0;
    int32_t total = row.n;
    bool bad = row.bad || row.n == 0 || fr.peer == nullptr;
    for (int32_t k = 0; k < kRowNames && !bad; ++k) {
        const int32_t i = names.i[k];
        bool dup = i < 0;
        for (int32_t d = 0; d < n && !dup; ++d) {
            dup = idx[d] == i;
        }
        if (dup) {
            continue;
        }
        bad = i >= g_host.names.n; // a name row the table does not hold: the client could not resolve the row
        if (bad || bit_has(fr.peer->names_had, i) || bit_has(fr.names_in, i)) {
            continue;
        }
        Bytes& nr = name_rows[n];
        nr = Bytes{};
        put_u8(nr, kTagName);
        put_varint(nr, static_cast<uint32_t>(i));
        put_string(nr, g_host.names.rows[i].text, g_host.names.rows[i].len);
        bad = nr.bad;
        total += nr.n;
        idx[n++] = i;
    }
    if (bad || total > kChanBlobBytes) {
        ++g_host.counts.bad_rows;
        host_warn_once("event row and its name rows do not fit one frame, row not sent", static_cast<uint32_t>(total));
        return false;
    }
    if (fr.len + total > kChanBlobBytes || fr.names_n + n > kFrameNames) {
        frame_send(fr);
    }
    for (int32_t k = 0; k < n; ++k) {
        frame_add(fr, name_rows[k]); // fits: no send in between
        fr.names[fr.names_n++] = static_cast<int16_t>(idx[k]);
        fr.names_in[idx[k] >> 5] |= 1u << (idx[k] & 31);
    }
    return frame_add(fr, row);
}

// Card 329 B: one row of `bytes` sent to the PC in peer slot `slot`, whose pawn is at `pawn_at` (null: no pawn, the
// band is "no location"). A counter update only: no allocation, no log line.
void fx_stat(const FxMeta& m, int32_t bytes, const Vec3* pawn_at, int32_t slot) {
    if (slot < 0 || slot >= kC17Slots) {
        return;
    }
    FxStats& st = g_host.stats[slot];
    FxSent& a = m.asset >= 0 && m.asset < kAssets ? st.asset[m.asset] : st.no_asset;
    ++a.rows;
    a.bytes += static_cast<uint64_t>(bytes);
    int32_t band = kFxBands; // no location
    if (pawn_at != nullptr && !is_zero(m.at)) {
        const double d2 = (pawn_at->x - m.at.x) * (pawn_at->x - m.at.x) +
                          (pawn_at->y - m.at.y) * (pawn_at->y - m.at.y) + (pawn_at->z - m.at.z) * (pawn_at->z - m.at.z);
        band = 0;
        while (band < kFxBands - 1 && d2 >= kFxBandEdge[band] * kFxBandEdge[band]) {
            ++band;
        }
    }
    FxSent& b = st.band[static_cast<int>(m.kind)][band];
    ++b.rows;
    b.bytes += static_cast<uint64_t>(bytes);
    const uint8_t bit = static_cast<uint8_t>(1u << slot);
    if (m.id != 0) {
        FxSentMark& k = g_host.sent[m.id & (kFxSentSlots - 1)];
        k.peers = k.id == m.id ? static_cast<uint8_t>(k.peers | bit) : bit;
        k.id = m.id;
    }
    if (m.kind != FxKind::End) {
        return;
    }
    const FxSentMark& k = g_host.sent[m.of & (kFxSentSlots - 1)];
    const bool tracked = m.of != 0 && k.id == m.of;
    st.end_no_row += m.of == 0 ? 1u : 0u;
    st.end_untracked += m.of != 0 && !tracked ? 1u : 0u;
    st.end_sent += tracked && (k.peers & bit) != 0 ? 1u : 0u;
    st.end_never += tracked && (k.peers & bit) == 0 ? 1u : 0u;
}

// Card 329 B, one peer slot: the kFxTopAssets assets by rows sent, one band row per kind, and the end rows' row.
void fx_stats_peer(const char* when, int32_t slot) {
    const FxStats& st = g_host.stats[slot];
    const char* const pc = g_host.peers[slot].name;
    bool shown[kAssets] = {};
    const int32_t n = g_host.assets.n < kAssets ? g_host.assets.n : kAssets;
    for (int32_t row = 0; row < kFxTopAssets; ++row) {
        int32_t best = -1;
        for (int32_t i = 0; i < n; ++i) {
            const bool better = best < 0 || st.asset[i].rows > st.asset[best].rows;
            best = !shown[i] && st.asset[i].rows != 0 && better ? i : best;
        }
        if (best < 0) {
            break;
        }
        shown[best] = true;
        log::line("card329: host fx map (%s) pc %s asset %d %s: %u rows %llu bytes", when, pc, best,
                  g_host.assets.rows[best].text, st.asset[best].rows,
                  static_cast<unsigned long long>(st.asset[best].bytes));
    }
    log::line("card329: host fx map (%s) pc %s no asset: %u rows %llu bytes", when, pc, st.no_asset.rows,
              static_cast<unsigned long long>(st.no_asset.bytes));
    for (int32_t k = 0; k < static_cast<int32_t>(FxKind::Count); ++k) {
        char text[320];
        int at = 0;
        for (int32_t b = 0; b <= kFxBands && at >= 0 && static_cast<size_t>(at) < sizeof(text); ++b) {
            at += snprintf(text + at, sizeof(text) - static_cast<size_t>(at), "%s%s %u rows %llu bytes",
                           b == 0 ? "" : ", ", kFxBandText[b], st.band[k][b].rows,
                           static_cast<unsigned long long>(st.band[k][b].bytes));
        }
        log::line("card329: host fx map (%s) pc %s %s rows by distance from the receiving pawn: %s", when, pc,
                  kFxKindText[k], text);
    }
    log::line("card329: host fx map (%s) pc %s end rows: %u of a journal row sent to this pc, %u of a journal row "
              "never sent to this pc, %u untracked; %u stops with no journal row",
              when, pc, st.end_sent, st.end_never, st.end_untracked, st.end_no_row);
}

// Card 329 B, at map end (before the asset table resets): the rows of each peer slot that was sent a row; the stats
// start over.
void fx_stats_rows(const char* when) {
    int32_t pcs = 0;
    for (int32_t slot = 0; slot < kC17Slots; ++slot) {
        const FxStats& st = g_host.stats[slot];
        uint32_t rows = st.no_asset.rows;
        for (const auto& kind : st.band) {
            for (const FxSent& b : kind) {
                rows += b.rows;
            }
        }
        if (g_host.peers[slot].key == 0 || rows == 0) {
            continue;
        }
        ++pcs;
        fx_stats_peer(when, slot);
    }
    log::line("card329: host fx map (%s): %d receiving pcs with rows", when, pcs);
    for (FxStats& st : g_host.stats) {
        st = FxStats{};
    }
}

int32_t peer_slot(const Peer& peer) {
    return static_cast<int32_t>(&peer - g_host.peers);
}

Peer& host_peer(const void* pc) {
    const int32_t key = object_index(pc) + 1;
    Peer* free_slot = nullptr;
    for (Peer& p : g_host.peers) {
        if (p.key == key) {
            return p;
        }
        free_slot = free_slot == nullptr && p.key == 0 ? &p : free_slot;
    }
    Peer& p = free_slot != nullptr ? *free_slot : g_host.peers[0]; // full: slot 0 starts over (its table resent)
    p = Peer{key, 0, {}, {}};
    snprintf(p.name, sizeof(p.name), "%s", object_name(pc).text);
    return p;
}

// The asset rows this peer has not had yet, ahead of the rows that use them (card 395: name rows go with the rows
// that name them, frame_add_named).
void frame_assets(Frame& fr, Peer& peer) {
    const HostAssets& a = g_host.assets;
    for (int32_t i = peer.table_sent; i < a.n; ++i) {
        Bytes row{};
        put_u8(row, kTagAsset);
        put_varint(row, static_cast<uint32_t>(i));
        put_u8(row, a.rows[i].object != nullptr ? 0 : 1);
        put_string(row, a.rows[i].text, static_cast<int32_t>(strlen(a.rows[i].text)));
        frame_add(fr, row);
    }
    peer.table_sent = a.n;
}

void host_flush(const void* world) {
    HostQueue& q = g_host.queue;
    for (int s = 0; s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(world, s);
        const C50Copy* const copy = pc != nullptr && sprint_side(pc) == kSideServerCopy
                                        ? c50_copy(object_index(pc), false)
                                        : nullptr;
        if (copy == nullptr || !copy->ponged) {
            continue;
        }
        const void* const pawn = read_ptr(pc, kControllerPawn);
        const Vec3 at = pawn != nullptr ? actor_location(pawn) : Vec3{};
        const Vec3* const pawn_at = pawn != nullptr ? &at : nullptr;
        static Frame fr; // game thread only
        Peer& peer = host_peer(pc);
        fr.pc = pc;
        fr.peer = &peer;
        fr.len = 0;
        fr.names_n = 0;
        fr.names_out = 0;
        frame_assets(fr, peer);
        const int32_t slot = peer_slot(peer);
        for (int32_t i = 0; i < q.n; ++i) {
            if (!filter_hits(q.rows[i].filter, pc, pawn) && frame_add_named(fr, q.rows[i].names, q.rows[i].row)) {
                fx_stat(q.rows[i].meta, q.rows[i].row.n, pawn_at, slot);
            }
        }
        frame_send(fr);
    }
    q.n = 0;
}

// --- Client ----------------------------------------------------------------------------------------------------

struct ClientAsset {
    bool tag;
    bool tried;
    void* held;    // the found or loaded asset, held for the map in the game instance's ReferencedObjects
    C31Weak weak;  // guard: a held asset whose weak is dead was freed anyway (counted, should stay 0)
    uint64_t name; // a tag's FName
    char text[kAssetPathChars];
};
struct ClientLive {
    uint32_t id;
    void* comp;
    C31Weak weak;
};
struct ClientCounts { // per map
    uint32_t rows, played, replayed, ended, no_asset, no_key, no_target, no_setting, unsupported, bad_rows, live_full,
        bad_frames, asset_lost, asset_unheld;
    uint32_t miss_rows;     // card 325: target-miss rows written (kClientMissRows per map)
    uint32_t host_only;     // NiagaraActivate keyed world: a component of a host-only actor, played at the world point
    uint32_t niagara_rows;  // card 325: placed Niagara count rows written (kClientNiagaraRows per map)
    ULONGLONG replay_ms;    // card 325: when the last frame with replay rows arrived; 0 once its count row ran
};
struct ClientName {
    bool tried;
    uint64_t fname; // the socket FName, made once per row
    char text[kStrMax + 1];
};
// Effects the client played outside the manager, its own effects and the block (card 136 review 2 "extra"): the
// card 132 gore decal relay and ownerless point 6 component ops. Per map, by asset.
constexpr int32_t kClientExtras = 256;
struct ClientExtra { // keyed by asset path: a freed asset's address can be reused by another object
    uint32_t n;
    char text[kAssetPathChars];
};
struct ClientExtras {
    int32_t n;
    uint32_t full;     // extras whose asset found no free row
    uint32_t off_thread;
    ClientExtra rows[kClientExtras];
};
struct ClientNet {
    void* world;
    void* pc;
    void* pawn;
    ClientAsset assets[kAssets]; // by host index; text kept across maps (each map's rows overwrite the old ones), held assets let go at map unload
    ClientName names[kNames];    // the same, for name rows
    int32_t live_n;
    int32_t live_peak;
    ClientLive live[kClientLive];
    ClientCounts counts;
    ClientExtras extras;
    uint32_t lines;
    C31Weak gi; // the game instance the held assets are registered in (released into the same one)
};
ClientNet g_client{}; // game thread only (the cached pc and pawn are also read by c136_client_own)

// One parsed E or R row.
struct FxRow {
    uint32_t id;
    int32_t point;
    int32_t asset;
    uint32_t of;
    int32_t seed;
    uint32_t flags;
    int32_t physmat;
    int32_t atten;
    int32_t conc;
    int32_t comp; // name rows, -1 none
    int32_t socket;
    int32_t owner_class;
    int32_t placed; // key: a placed owner's wire name row, -1 none
    Vec3 at, rot, scale, world;
    float look[3];
    uint64_t guid; // key: a replicated owner's NetGUID, 0 none
    Vec3 placed_at;
};

const char* client_name(int32_t i) {
    return i >= 0 && i < kNames ? g_client.names[i].text : "-";
}

void client_line(const FxRow& r, const char* result) {
    if (!g_c136_log || !note_event_cap(g_client.lines, kClientLines, "card136 client fx lines")) {
        return;
    }
    const char* const asset = r.asset >= 0 && r.asset < kAssets ? g_client.assets[r.asset].text : "-";
    log::line("card136: client fx %u %s %s owner %s %s", r.id, c136_point_name(static_cast<C136Point>(r.point)), asset,
              r.owner_class >= 0 ? client_name(r.owner_class) : "-", result);
}

// A string row's text (u8 length, bytes) into out (cap > kStrMax); false when malformed.
bool client_string(Reader& in, char* out, size_t cap) {
    const int32_t len = get_u8(in);
    if (len < 1 || static_cast<size_t>(len) >= cap || !get_raw(in, out, len)) {
        in.bad = true;
        return false;
    }
    out[len] = '\0';
    return true;
}

// The game instance that holds the client's fx assets, or null.
void* client_game_instance() {
    void* const world = g_client.pc != nullptr ? g_actor_get_world(g_client.pc) : nullptr;
    return world != nullptr ? read_ptr(world, kWorldOwningGameInstance) : nullptr;
}

// Lets a row's asset go: out of the game instance's ReferencedObjects, and looked up again on its next use.
void client_asset_release(ClientAsset& a) {
    if (a.held != nullptr && g_gi_unregister_ref != nullptr) {
        void* const gi = c31_weak_get(g_client.gi);
        if (gi != nullptr) {
            g_gi_unregister_ref(gi, a.held);
        }
    }
    a.held = nullptr;
    a.weak = C31Weak{};
    a.tried = false;
}

// How many table rows hold an asset now (the client map totals line).
int32_t client_assets_held() {
    int32_t n = 0;
    for (const ClientAsset& a : g_client.assets) {
        n += a.held != nullptr ? 1 : 0;
    }
    return n;
}

bool client_asset_row(Reader& in) {
    const uint64_t i = get_varint(in);
    const uint8_t kind = get_u8(in);
    char text[kStrMax + 1];
    if (!client_string(in, text, sizeof(text)) || i >= static_cast<uint64_t>(kAssets) || kind > 1 ||
        strlen(text) >= kAssetPathChars) {
        return false;
    }
    ClientAsset& a = g_client.assets[i];
    client_asset_release(a); // the host reused the index for another asset
    a = ClientAsset{};
    a.tag = kind == 1;
    snprintf(a.text, sizeof(a.text), "%s", text);
    return true;
}

bool client_name_row(Reader& in) {
    const uint64_t i = get_varint(in);
    char text[kStrMax + 1];
    if (!client_string(in, text, sizeof(text)) || i >= static_cast<uint64_t>(kNames)) {
        return false;
    }
    ClientName& n = g_client.names[i];
    n = ClientName{};
    memcpy(n.text, text, sizeof(n.text));
    return true;
}

// The asset of a table row, found (or loaded, cls given) once per row and map; null when it is not there. An asset
// the client loaded itself has no root, and a raw pointer kept past a GC that freed it crashed c140g (a freed
// NS_FX_Gore_BloodDeath_Green01 in SpawnSystemAtLocation's CopyParametersFromAsset). So each asset found is held in
// the game instance's ReferencedObjects (UGameInstance::RegisterReferencedObject) until the map unloads
// (c136_net_arm_map) or its row is replaced: no reload, no load hitch in a fight. The weak ref is the guard: a held
// asset freed anyway is counted (asset_lost, should stay 0) and looked up again.
void* client_asset(int32_t i, void* cls) {
    if (i < 0 || i >= kAssets || g_client.assets[i].tag || g_weak_get == nullptr || g_weak_assign == nullptr) {
        return nullptr;
    }
    ClientAsset& a = g_client.assets[i];
    if (a.held != nullptr) {
        if (g_weak_get(&a.weak) == a.held) {
            return a.held;
        }
        ++g_client.counts.asset_lost;
        a.held = nullptr;
        a.weak = C31Weak{};
        a.tried = false;
    }
    if (a.tried || a.text[0] == '\0') {
        return nullptr;
    }
    a.tried = true;
    void* object = nullptr;
    if (cls != nullptr) {
        object = c74_find_asset(a.text, cls);
    } else {
        wchar_t wide[kAssetPathChars];
        object = swprintf(wide, kAssetPathChars, L"%hs", a.text) > 0 ? g_static_find_object(nullptr, nullptr, wide, false)
                                                                     : nullptr;
    }
    if (object == nullptr) {
        return nullptr;
    }
    void* const gi = client_game_instance();
    if (gi != nullptr && g_gi_register_ref != nullptr) {
        g_gi_register_ref(gi, object);
        c31_weak_set(g_client.gi, gi);
    } else {
        ++g_client.counts.asset_unheld;
    }
    a.held = object;
    g_weak_assign(&a.weak, &object);
    return object;
}

uint64_t client_tag(int32_t i) {
    if (i < 0 || i >= kAssets || !g_client.assets[i].tag) {
        return 0;
    }
    ClientAsset& a = g_client.assets[i];
    if (!a.tried && g_fname_ctor != nullptr) {
        a.tried = true;
        wchar_t wide[kAssetPathChars];
        if (swprintf(wide, kAssetPathChars, L"%hs", a.text) > 0) {
            g_fname_ctor(&a.name, wide, kFNameAdd);
        }
    }
    return a.name;
}

// The actor the row's key names, or null (a world key, or not found here).
void* client_key_actor(const FxRow& r) {
    if (r.guid != 0 && g_object_from_guid != nullptr && g_client.world != nullptr) {
        void* const map = c41_package_map(g_client.world);
        void* const o = map != nullptr ? g_object_from_guid(map, &r.guid, false) : nullptr;
        return o != nullptr && is_a(o, g_actor_class()) ? o : nullptr;
    }
    if (r.placed >= 0 && g_client.world != nullptr) {
        return c95_find_nearest(g_client.world, client_name(r.placed), r.placed_at);
    }
    return nullptr;
}

void* client_key_component(const FxRow& r) {
    void* const actor = client_key_actor(r);
    return actor != nullptr && r.comp >= 0 ? c95_component(actor, client_name(r.comp)) : nullptr;
}

int32_t client_live_find(uint32_t id) {
    for (int32_t i = 0; i < g_client.live_n; ++i) {
        if (g_client.live[i].id == id) {
            return i;
        }
    }
    return -1;
}

void client_live_remove(int32_t i) {
    g_client.live[i] = g_client.live[g_client.live_n - 1];
    --g_client.live_n;
}

bool client_live_alive(const ClientLive& l) {
    return g_weak_get != nullptr && g_weak_get(&l.weak) == l.comp;
}

void client_live_add(uint32_t id, void* comp) {
    if (comp == nullptr) {
        return;
    }
    if (g_client.live_n == kClientLive) {
        for (int32_t i = g_client.live_n - 1; i >= 0; --i) {
            if (!client_live_alive(g_client.live[i])) {
                client_live_remove(i);
            }
        }
    }
    if (g_client.live_n == kClientLive) {
        ++g_client.counts.live_full;
        if (g_client.counts.live_full == 1) {
            log::line("WARN card136: client fx live map full (%d, peak %d): copies not tracked", kClientLive,
                      g_client.live_peak);
        }
        return;
    }
    for (int32_t i = 0; i < g_client.live_n; ++i) {
        if (g_client.live[i].comp == comp && client_live_alive(g_client.live[i])) {
            g_client.live[i].id = id; // the same component again (pooled, or a placed one activated again)
            return;
        }
    }
    ClientLive& l = g_client.live[g_client.live_n++];
    g_client.live_peak = g_client.live_n > g_client.live_peak ? g_client.live_n : g_client.live_peak;
    l = ClientLive{id, comp, C31Weak{}};
    if (g_weak_assign != nullptr) {
        g_weak_assign(&l.weak, &comp);
    }
}

// The live copy of host event `id`, or null (dead copies leave the map here).
void* client_live_get(uint32_t id) {
    const int32_t i = client_live_find(id);
    if (i < 0) {
        return nullptr;
    }
    if (!client_live_alive(g_client.live[i])) {
        client_live_remove(i);
        return nullptr;
    }
    return g_client.live[i].comp;
}

uint64_t float_slot(float f) {
    uint32_t bits = 0;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}
uint64_t ptr_slot(const void* p) {
    return reinterpret_cast<uint64_t>(p);
}

bool flag(const FxRow& r, uint8_t f) {
    return (r.flags & f) != 0;
}
uint64_t attach_location(const FxRow& r) {
    return (r.flags >> kC136FlagLocationShift) & kC136FlagLocationMask;
}

// A spawn with the host's seed: spawned inactive, seeded, then activated (the seed is read at activation).
void* client_niagara_at(const FxRow& r, void* sys, const Vec3& at) {
    const bool seeded = r.seed != 0 && flag(r, kC136FlagAutoActivate);
    void* const comp = g_orig_c74_niagara(g_client.pc, sys, &at, &r.rot, &r.scale, flag(r, kC136FlagAutoDestroy),
                                          flag(r, kC136FlagAutoActivate) && !seeded, 0, true);
    if (comp != nullptr && seeded) {
        *reinterpret_cast<int32_t*>(static_cast<uint8_t*>(comp) + kNiagaraSeedOffset) = r.seed;
        g_orig_c136_activate(comp, true);
    }
    return comp;
}

// The row's socket FName (0: None), made once per name row.
uint64_t client_socket(const FxRow& r) {
    if (r.socket < 0 || r.socket >= kNames || g_fname_ctor == nullptr) {
        return 0;
    }
    ClientName& n = g_client.names[r.socket];
    if (!n.tried) {
        n.tried = true;
        wchar_t wide[kStrMax + 1];
        if (swprintf(wide, kStrMax + 1, L"%hs", n.text) > 0) {
            g_fname_ctor(&n.fname, wide, kFNameAdd);
        }
    }
    return n.fname;
}

const char* client_play_niagara(const FxRow& r) {
    void* const sys = client_asset(r.asset, g_niagara_system_class());
    if (sys == nullptr) {
        return "no asset";
    }
    const C136Point p = static_cast<C136Point>(r.point);
    void* comp = nullptr;
    const char* result = "played";
    if (p == C136Point::Niagara || p == C136Point::NiagaraParams) {
        comp = client_niagara_at(r, sys, r.at);
    } else {
        void* const parent = client_key_component(r);
        const bool seeded = r.seed != 0 && flag(r, kC136FlagAutoActivate);
        if (parent != nullptr) {
            const uint64_t socket = client_socket(r);
            const uint64_t slots[kPassSlots] = {ptr_slot(sys), ptr_slot(parent), socket, ptr_slot(&r.at),
                                                    ptr_slot(&r.rot), ptr_slot(&r.scale), attach_location(r),
                                                    flag(r, kC136FlagAutoDestroy) ? 1u : 0u, 0,
                                                    flag(r, kC136FlagAutoActivate) && !seeded ? 1u : 0u, 1, 0, 0};
            // The Scale overload's order: ..., Scale, EAttachLocation, bAutoDestroy, ENCPoolMethod (None),
            // bAutoActivate, bPreCullCheck.
            comp = c136_pass_play(C136Point::NiagaraAttachedScale, slots);
            if (comp != nullptr && seeded) {
                *reinterpret_cast<int32_t*>(static_cast<uint8_t*>(comp) + kNiagaraSeedOffset) = r.seed;
                g_orig_c136_activate(comp, true);
            }
        } else {
            ++g_client.counts.no_key;
            result = "played at world (key miss)";
            comp = client_niagara_at(r, sys, r.world);
        }
    }
    client_live_add(r.id, comp);
    return comp != nullptr ? result : "spawn returned null";
}

// A sound's attenuation or concurrency override (an asset row), or null: the sound's own settings. Found, not loaded:
// a sound's settings assets load with it.
void* client_setting(int32_t i) {
    if (i < 0) {
        return nullptr;
    }
    void* const o = client_asset(i, nullptr);
    g_client.counts.no_setting += o == nullptr ? 1u : 0u;
    return o;
}

const char* client_play_sound(const FxRow& r) {
    void* const sound = client_asset(r.asset, g_sound_base_class());
    if (sound == nullptr) {
        return "no asset";
    }
    const C136Point p = static_cast<C136Point>(r.point);
    const Vec3& where = p == C136Point::SoundAttached ? r.world : r.at;
    // Card 162: a remote player's sound with no attenuation plays with its fallback, not 2D at full volume.
    const bool player_source = flag(r, kC136FlagPlayerSource); // card 304: the host's word, not the remote list
    void* const atten =
        c162_remote_atten(g_client.pc, sound, client_setting(r.atten), client_key_actor(r), where, player_source);
    const char* const key = r.guid != 0 ? "guid" : (r.placed >= 0 ? "placed" : "world");
    c304_row(sound, player_source, key, client_key_actor(r) != nullptr, atten, where);
    void* const conc = client_setting(r.conc);
    // Card 155: the replay plays with the player controller as context, so the engine hooks never see the remote
    // owner; the playlog audio line for a remote player's sound comes from here (pl_sound drops any other owner).
    pl_sound(client_key_actor(r), sound, where, atten, -1);
    pl_sound_row(client_key_actor(r), sound, where, atten,
                 p == C136Point::SoundAttached); // card 162: every relayed row, world-keyed ones too
    if (p == C136Point::PlaySound) {
        g_orig_c74_play_sound(g_client.pc, sound, &r.at, &r.rot, r.look[0], r.look[1], r.look[2], atten, conc,
                              nullptr, nullptr);
        return "played";
    }
    if (p == C136Point::SoundAttached) {
        void* const parent = client_key_component(r);
        if (parent != nullptr) {
            const uint64_t slots[kPassSlots] = {
                ptr_slot(sound), ptr_slot(parent), client_socket(r), ptr_slot(&r.at), ptr_slot(&r.rot),
                attach_location(r), flag(r, kC136FlagStopWithParent) ? 1u : 0u, float_slot(r.look[0]),
                float_slot(r.look[1]), float_slot(r.look[2]), ptr_slot(atten), ptr_slot(conc),
                flag(r, kC136FlagAutoDestroy) ? 1u : 0u};
            return c136_pass_play(C136Point::SoundAttached, slots) != nullptr ? "played" : "spawn returned null";
        }
        ++g_client.counts.no_key;
        g_orig_c74_spawn_sound(g_client.pc, sound, &r.world, &r.rot, r.look[0], r.look[1], r.look[2], atten, conc,
                               true);
        return "played at world (key miss)";
    }
    void* const made = g_orig_c74_spawn_sound(g_client.pc, sound, &r.at, &r.rot, r.look[0], r.look[1], r.look[2],
                                              atten, conc, flag(r, kC136FlagAutoDestroy));
    return made != nullptr ? "played" : "spawn returned null";
}

const char* client_play_cue(const FxRow& r) {
    const uint64_t tag = client_tag(r.asset);
    if (tag == 0 || g_c136_cue_params_ctor == nullptr || g_c136_cue_params_dtor == nullptr) {
        return "no tag";
    }
    void* target = client_key_actor(r);
    const char* result = "played";
    if (target == nullptr) {
        ++g_client.counts.no_target;
        target = g_client.pc;
        result = "played on the pc (target miss)";
    }
    alignas(16) uint8_t params[kCueParamsSize];
    g_c136_cue_params_ctor(params);
    memcpy(params + kCueLocation, &r.at, sizeof(Vec3));
    memcpy(params + kCueNormal, &r.rot, sizeof(Vec3));
    void* physmat = client_asset(r.physmat, nullptr);
    if (physmat != nullptr && g_weak_assign != nullptr) {
        g_weak_assign(params + kCuePhysMat, &physmat);
    }
    g_orig_c136_cue_local(target, tag, params);
    g_c136_cue_params_dtor(params);
    return result;
}

const char* client_play_decal(const FxRow& r) {
    void* const mat = client_asset(r.asset, nullptr);
    void* const manager = mat != nullptr ? c132_decal_manager(g_client.pc) : nullptr;
    if (manager == nullptr) {
        return mat == nullptr ? "no asset" : "no decal manager";
    }
    if (static_cast<C136Point>(r.point) == C136Point::DecalAttached) {
        void* const parent = client_key_component(r);
        if (parent != nullptr) {
            g_orig_spawn_decal_attached(manager, mat, &r.scale, parent, client_socket(r), &r.at, &r.rot,
                                        static_cast<uint8_t>(attach_location(r)), r.look[0]);
            return "played";
        }
        ++g_client.counts.no_key;
        g_orig_spawn_decal_at(manager, g_client.pc, mat, &r.scale, &r.world, &r.rot, r.look[0]);
        return "played at world (key miss)";
    }
    g_orig_spawn_decal_at(manager, g_client.pc, mat, &r.scale, &r.at, &r.rot, r.look[0]);
    return "played";
}

// A NiagaraActivate whose component is not here: the copy of its spawn row is gone (completed or pooled while the host
// activates its component again), or the component is not found by name. Run 6 fix D: with the key actor here, a new
// copy attached to the actor's root at the host's world point; else one at the world point. The copy takes the spawn
// row's id when there is one, so later rows for that id act on it.
const char* client_play_component_miss(const FxRow& r) {
    void* const sys = client_asset(r.asset, g_niagara_system_class());
    if (sys == nullptr) {
        return "no component";
    }
    void* const actor = client_key_actor(r);
    void* const root = actor != nullptr ? read_ptr(actor, kActorRootComponent) : nullptr;
    void* made = nullptr;
    const char* result = "spawned at world (component miss)";
    if (root != nullptr) {
        const uint64_t slots[kPassSlots] = {ptr_slot(sys), ptr_slot(root), 0, ptr_slot(&r.world), ptr_slot(&r.rot),
                                            ptr_slot(&r.scale), kAttachKeepWorld,
                                            flag(r, kC136FlagAutoDestroy) ? 1u : 0u, 0, 1, 1, 0, 0};
        made = c136_pass_play(C136Point::NiagaraAttachedScale, slots);
        result = "spawned on the key actor (component miss)";
    } else {
        made = client_niagara_at(r, sys, r.world);
    }
    client_live_add(r.of != 0 ? r.of : r.id, made);
    return made != nullptr ? result : "no component";
}

const char* client_play_component(const FxRow& r) {
    void* comp = r.of != 0 ? client_live_get(r.of) : nullptr;
    comp = comp != nullptr ? comp : client_key_component(r);
    const C136Point p = static_cast<C136Point>(r.point);
    if (comp == nullptr) {
        if (p != C136Point::NiagaraActivate) {
            return "no component";
        }
        if (r.guid == 0 && r.placed < 0) {
            // Keyed world: the component's actor is host-only (c409b116a: BP_HazardousZone_Acid, the ArcherBug death
            // handler's deferred spawn, replicates 0; also AreaFireFast). The client never has the component, so no
            // lookup can find it: the effect is played at the host's world point and is not a target miss.
            ++g_client.counts.host_only;
            void* const sys = client_asset(r.asset, g_niagara_system_class());
            void* const made = sys != nullptr ? client_niagara_at(r, sys, r.world) : nullptr;
            client_live_add(r.of != 0 ? r.of : r.id, made);
            return made != nullptr ? "played at world (host-only component)" : "no component";
        }
        ++g_client.counts.no_target;
        return client_play_component_miss(r);
    }
    if (p == C136Point::NiagaraActivate) {
        g_orig_c136_activate(comp, flag(r, kC136FlagReset));
        if (r.of == 0) {
            client_live_add(r.id, comp); // a placed Activate: its Deactivate comes as a D row naming this id
        }
    } else if (p == C136Point::NiagaraDeactivate) {
        g_orig_c136_deactivate(comp);
    } else {
        g_orig_c136_deactivate_now(comp);
    }
    return "played";
}

using ClientPlayFn = const char* (*)(const FxRow&);
constexpr ClientPlayFn kClientPlay[static_cast<int>(C136Point::Count)] = {
    client_play_sound,     client_play_sound,     client_play_sound,     client_play_niagara,   client_play_niagara,
    client_play_niagara,   client_play_niagara,   client_play_niagara,   nullptr,               nullptr,
    nullptr,               client_play_cue,       client_play_cue,       client_play_cue,       client_play_decal,
    client_play_decal,     client_play_component, client_play_component, client_play_component, nullptr,
};

// An E or R row's fields after the tag (the card's run 4 wire format); false when malformed.
bool client_parse(Reader& in, FxRow& r) {
    r = FxRow{};
    r.scale = Vec3{1.0, 1.0, 1.0};
    r.comp = r.socket = r.owner_class = r.placed = -1;
    r.id = static_cast<uint32_t>(get_varint(in));
    r.point = get_u8(in);
    const uint16_t asset = get_u16(in);
    r.asset = asset != kNoAsset ? asset : -1;
    const uint16_t m = get_u16(in);
    r.physmat = r.atten = r.conc = -1;
    if (in.bad || r.point >= static_cast<int32_t>(C136Point::Count) || ((m & kBitGuid) != 0 && (m & kBitPlaced) != 0)) {
        return false;
    }
    r.at = (m & kBitLoc) != 0 ? get_vec(in) : Vec3{};
    if ((m & kBitRot) != 0) {
        double (*const get)(Reader&) = is_cue(static_cast<C136Point>(r.point)) ? get_unit : get_angle;
        r.rot.x = get(in);
        r.rot.y = get(in);
        r.rot.z = get(in);
    }
    r.scale = (m & kBitScale) != 0 ? get_vec(in) : r.scale;
    const auto name = [&in](uint16_t mask, uint16_t bit) -> int32_t {
        if ((mask & bit) == 0) {
            return -1;
        }
        const uint64_t i = get_varint(in);
        in.bad = in.bad || i >= static_cast<uint64_t>(kNames);
        return in.bad ? -1 : static_cast<int32_t>(i);
    };
    r.comp = name(m, kBitComp);
    r.socket = name(m, kBitSocket);
    r.guid = (m & kBitGuid) != 0 ? get_varint(in) : 0;
    r.placed = name(m, kBitPlaced);
    r.placed_at = (m & kBitPlaced) != 0 ? get_vec(in) : Vec3{};
    r.owner_class = name(m, kBitOwnerClass);
    const Vec3 look = (m & kBitLook) != 0 ? get_vec(in) : Vec3{};
    r.look[0] = static_cast<float>(look.x);
    r.look[1] = static_cast<float>(look.y);
    r.look[2] = static_cast<float>(look.z);
    if ((m & kBitSeed) != 0) {
        get_raw(in, &r.seed, 4);
    }
    r.world = (m & kBitWorld) != 0 ? get_vec(in) : r.at;
    r.of = (m & kBitOf) != 0 ? static_cast<uint32_t>(get_varint(in)) : 0;
    r.flags = (m & kBitFlags) != 0 ? get_u8(in) : 0;
    const auto asset_row = [&in](uint16_t mask, uint16_t bit) -> int32_t {
        if ((mask & bit) == 0) {
            return -1;
        }
        const uint64_t i = get_varint(in);
        in.bad = in.bad || i >= static_cast<uint64_t>(kAssets);
        return in.bad ? -1 : static_cast<int32_t>(i);
    };
    r.physmat = asset_row(m, kBitPhysMat);
    r.atten = asset_row(m, kBitAtten);
    r.conc = asset_row(m, kBitConc);
    return !in.bad;
}

// A D row: the point, the id, and the row it stops.
bool client_stop_parse(Reader& in, FxRow& r) {
    r = FxRow{};
    r.comp = r.socket = r.owner_class = r.placed = r.asset = r.physmat = r.atten = r.conc = -1;
    r.scale = Vec3{1.0, 1.0, 1.0};
    r.point = get_u8(in);
    r.id = static_cast<uint32_t>(get_varint(in));
    r.of = static_cast<uint32_t>(get_varint(in));
    return !in.bad && is_stop(static_cast<C136Point>(r.point)) && r.of != 0;
}

// Card 325 (diagnostic, client): a target miss names its row. A cue played on the PC (its key actor not found) or a
// NiagaraActivate whose component was not found (a new system spawned in its place). Capped per map.
constexpr uint32_t kClientMissRows = 64;

void client_miss_row(const FxRow& r, bool replay, const char* result) {
    if (!note_event_cap(g_client.counts.miss_rows, kClientMissRows, "card325 target miss")) {
        return;
    }
    char key[kStrMax + 16];
    if (r.guid != 0) {
        snprintf(key, sizeof key, "guid %llu", static_cast<unsigned long long>(r.guid));
    } else {
        snprintf(key, sizeof key, "%s", r.placed >= 0 ? client_name(r.placed) : "world");
    }
    const char* const asset = r.asset >= 0 && r.asset < kAssets ? g_client.assets[r.asset].text : "-";
    log::line("card325: client t=%llu target miss %s %s key %s comp %s owner class %s key actor %s at (%.0f, %.0f, %.0f) result %s%s",
              static_cast<unsigned long long>(GetTickCount64()), c136_point_name(static_cast<C136Point>(r.point)),
              asset, key, r.comp >= 0 ? client_name(r.comp) : "-",
              r.owner_class >= 0 ? client_name(r.owner_class) : "-", client_key_actor(r) != nullptr ? "here" : "none",
              r.world.x, r.world.y, r.world.z, result, replay ? " (join replay)" : "");
}

bool client_event_row(Reader& in, uint8_t tag) {
    FxRow r{};
    if (!(tag == kTagStop ? client_stop_parse(in, r) : client_parse(in, r))) {
        return false;
    }
    const bool replay = tag == kTagReplay;
    ++(replay ? g_client.counts.replayed : g_client.counts.rows);
    const ClientPlayFn play = kClientPlay[r.point];
    if (play == nullptr) {
        ++g_client.counts.unsupported;
        client_line(r, "unsupported");
        return true; // a well-formed row of a point the client does not play
    }
    const uint32_t misses = g_client.counts.no_target;
    // Card 406 (diagnostic): a row whose play takes kC406SlowMs or more is logged with its time. Run c405b96a: one
    // Fx frame took 10 to 69 ms per 30 s window; run c406b97a: no asset resolve took 2 ms, so it is not the asset load.
    constexpr double kC406SlowMs = 3.0;
    LARGE_INTEGER t0;
    LARGE_INTEGER t1;
    LARGE_INTEGER freq;
    QueryPerformanceCounter(&t0);
    c136_set_playing(true);
    const char* const result = play(r);
    c136_set_playing(false);
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&freq);
    const double ms = 1000.0 * static_cast<double>(t1.QuadPart - t0.QuadPart) / static_cast<double>(freq.QuadPart);
    if (ms >= kC406SlowMs) {
        log::line("card406: client fx slow row %.1f ms: %s %s key %s result %s%s", ms,
                  c136_point_name(static_cast<C136Point>(r.point)),
                  r.asset >= 0 && r.asset < kAssets ? g_client.assets[r.asset].text : "-",
                  r.placed >= 0 ? client_name(r.placed) : (r.guid != 0 ? "guid" : "world"), result,
                  replay ? " (join replay)" : "");
    }
    if (g_client.counts.no_target != misses) {
        client_miss_row(r, replay, result);
    }
    g_client.counts.played += strncmp(result, "played", 6) == 0 ? 1u : 0u;
    g_client.counts.no_asset += strncmp(result, "no asset", 8) == 0 ? 1u : 0u;
    client_line(r, result);
    return true;
}

bool client_end_row(Reader& in) {
    const uint32_t id = static_cast<uint32_t>(get_varint(in));
    if (in.bad) {
        return false;
    }
    void* const comp = client_live_get(id);
    if (comp == nullptr) {
        return true; // not copied here (filtered, or already gone)
    }
    c136_set_playing(true);
    g_orig_c136_deactivate_now(comp);
    c136_set_playing(false);
    client_live_remove(client_live_find(id));
    ++g_client.counts.ended;
    return true;
}

// One row by its tag; false when malformed (the rest of the frame cannot be found then).
bool client_row(Reader& in) {
    const uint8_t tag = get_u8(in);
    switch (tag) {
    case kTagAsset:
        return client_asset_row(in);
    case kTagName:
        return client_name_row(in);
    case kTagEvent:
    case kTagReplay:
    case kTagStop:
        return client_event_row(in, tag);
    case kTagEnd:
        return client_end_row(in);
    default:
        return false;
    }
}

// Card 325 (diagnostic, both sides): the placed actors' active UNiagaraComponents, counted in the world after the
// join replay (host: when it sends the replay; client: after each frame that held replay rows). A placed actor is one
// loaded with the map (c50_placed, RF_WasLoaded), the same set on both sides; its components are walked in
// AActor::OwnedComponents (the TSet layout card 310's collision row reads, testrun.cpp). Equal counts: every placed
// effect the host shows runs on the client too. A host count above the client's is a placed effect lost before or
// in the replay (card 325's wiped journal).
constexpr size_t kOwnedComponents = 0x230; // AActor::OwnedComponents TSet<UActorComponent*>
constexpr size_t kOwnedNum = 0x8;
constexpr size_t kOwnedBitsInline = 0x10;
constexpr size_t kOwnedBitsHeap = 0x20;
constexpr size_t kOwnedNumBits = 0x28;
constexpr size_t kOwnedElement = 16;
constexpr int32_t kOwnedMax = 256; // components walked per actor
constexpr uint32_t kClientNiagaraRows = 4;   // count rows per map: one per join replay burst
constexpr ULONGLONG kClientReplayQuietMs = 1500; // no replay rows for this long: the burst's last frame has arrived

struct NiagaraCount {
    uint32_t active, components, actors;
};

void niagara_count_actor(const void* actor, const void* niagara, NiagaraCount& c) {
    const auto* const set = static_cast<const uint8_t*>(actor) + kOwnedComponents;
    const auto* const data = *reinterpret_cast<const uint8_t* const*>(set);
    const int32_t num = *reinterpret_cast<const int32_t*>(set + kOwnedNum);
    const int32_t num_bits = *reinterpret_cast<const int32_t*>(set + kOwnedNumBits);
    const auto* const heap = *reinterpret_cast<const uint32_t* const*>(set + kOwnedBitsHeap);
    const auto* const bits = heap != nullptr ? heap : reinterpret_cast<const uint32_t*>(set + kOwnedBitsInline);
    bool has = false;
    for (int32_t i = 0; data != nullptr && i < num && i < num_bits && i < kOwnedMax; ++i) {
        const void* const comp = (bits[i >> 5] >> (i & 31)) & 1U
                                     ? *reinterpret_cast<void* const*>(data + static_cast<size_t>(i) * kOwnedElement)
                                     : nullptr;
        if (comp == nullptr || !is_a(comp, niagara)) {
            continue;
        }
        has = true;
        ++c.components;
        c.active += journal_active(comp) == 1 ? 1u : 0u;
    }
    c.actors += has ? 1u : 0u;
}

void niagara_count_row(const void* world, const char* who) {
    static void* const niagara = find_class(L"/Script/Niagara.NiagaraComponent");
    if (world == nullptr || niagara == nullptr) {
        log::line("card325: %s t=%llu placed niagara count skipped (%s)", g_client_session ? "client" : "host",
                  static_cast<unsigned long long>(GetTickCount64()), world == nullptr ? "no world" : "no class");
        return;
    }
    NiagaraCount c{};
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    for (int32_t l = 0; levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const void* const level = levels[l];
        const auto* const actors = level != nullptr ? static_cast<void* const*>(read_ptr(level, kLevelActors)) : nullptr;
        const int32_t num = level != nullptr ? read_i32(level, kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; actors != nullptr && a < num && a < kMaxLevelActors; ++a) {
            if (actors[a] != nullptr && c50_placed(actors[a])) {
                niagara_count_actor(actors[a], niagara, c);
            }
        }
    }
    log::line("card325: %s t=%llu placed niagara after the join replay (%s): %u active of %u components on %u actors",
              g_client_session ? "client" : "host", static_cast<unsigned long long>(GetTickCount64()), who, c.active,
              c.components, c.actors);
}

} // namespace

// --- Exported ----------------------------------------------------------------------------------------------------

// Card 438 (host): an attached effect spawned in a replicated NPC's first frame has an owner with no NetGUID yet
// (the first replication assigns it), so its row went out with the world key and the client drew it at the world
// point, not on the NPC (c399b134a: 95 NS_Compute_DamageAndRadius wound shading rows, 4 HopperBug flap sounds; the
// client blocks its own copy's effect). Such a spawn is held here, not journaled and not queued, until the owner has
// a NetGUID (card 436 measured 47 to 318 ms); then its row is built with the guid key. Other rows are not delayed.
// After kC438HoldMs, or with the list full, the row goes with the world key as before. A stop of a held component
// ends the entry and sends nothing (the client never had it); any other op that names it sends it first.
constexpr int32_t kC438Held = 64;
constexpr ULONGLONG kC438HoldMs = 2000;
struct C438Row {
    Pending p; // p.active: the entry is in use
    C31Weak owner;
    C31Weak made;
    int32_t seed;
    ULONGLONG at_ms;
};
struct C438 {
    C438Row rows[kC438Held];
    uint32_t want_id; // the pending event to hold at its done, 0 none
    C31Weak want_owner;
    uint32_t held, by_guid, at_world, ended, full, early;
    uint32_t ended_near; // card 438 diagnostic (b140): ended with the owner within kC438NearUu of a client pawn
    ULONGLONG longest_ms;
};
static C438 g_c438{}; // game thread only

// Card 438 diagnostic (b138): held and ended-while-held per host asset row, and the owner class first seen for
// it. b136: held 2439, ended while held 2411 (b135b 494 / 257); this names the systems behind the rise.
struct C438BySystem {
    uint32_t held, ended;
    const void* owner_class; // a UClass, alive for the session
};
constexpr int32_t kC438TopSystems = 8;
static C438BySystem g_c438_sys[kAssets]{}; // game thread only, per map
static uint32_t g_c438_no_asset;           // held rows without an asset row

static void c438_sys_held(const Pending& p, const void* owner) {
    if (p.asset < 0 || p.asset >= kAssets) {
        ++g_c438_no_asset;
        return;
    }
    C438BySystem& s = g_c438_sys[p.asset];
    ++s.held;
    s.owner_class = s.owner_class == nullptr && owner != nullptr ? read_ptr(owner, kObjectClass) : s.owner_class;
}

static void c438_sys_ended(const Pending& p) {
    g_c438_sys[p.asset].ended += p.asset >= 0 && p.asset < kAssets ? 1 : 0;
}

static void host_emit(const Pending& p, const void* made, int32_t seed);

// From c136_net_queue, the owner live: true when this guid-0 event will be held at its done.
static bool c438_want(const Pending& p, const void* owner) {
    const bool want = p.attached && is_spawn(p.point) && p.filter.attach_owner == owner && c98_replicates(owner) &&
                      g_weak_assign != nullptr && g_weak_get != nullptr;
    if (want) {
        void* object = const_cast<void*>(owner);
        g_c438.want_id = p.id;
        g_c438.want_owner = C31Weak{};
        g_weak_assign(&g_c438.want_owner, &object);
    }
    return want;
}

static bool c438_hold(const Pending& p, const void* made, int32_t seed) {
    C438& h = g_c438;
    const bool want = p.id != 0 && h.want_id == p.id && made != nullptr && !p.too_long;
    h.want_id = 0;
    for (int32_t i = 0; want && i < kC438Held; ++i) {
        C438Row& r = h.rows[i];
        if (r.p.active) {
            continue;
        }
        void* object = const_cast<void*>(made);
        r = C438Row{p, h.want_owner, C31Weak{}, seed, GetTickCount64()};
        r.p.active = true;
        g_weak_assign(&r.made, &object);
        ++h.held;
        c438_sys_held(p, g_weak_get(&h.want_owner));
        return true;
    }
    h.full += want ? 1 : 0;
    return false;
}

// Sends a held row: with the owner's NetGUID when it has one now, else with the world key it was captured with.
static void c438_release(C438Row& r, const void* owner, const void* made, uint32_t C438::* count) {
    r.p.active = false;
    Pending p = r.p;
    p.guid = c41_guid(owner);
    const ULONGLONG waited = GetTickCount64() - r.at_ms;
    g_c438.longest_ms = p.guid != 0 && waited > g_c438.longest_ms ? waited : g_c438.longest_ms;
    ++(g_c438.*(p.guid != 0 ? &C438::by_guid : count));
    host_emit(p, made, r.seed);
}

// Card 438 diagnostic (b140): a held row ended. 0 near rows over a run means the held path costs only bytes for
// far owners; a near row is an effect a client in range never got.
constexpr double kC438NearUu = 15000.0;
static void c438_count_ended(const void* owner) {
    ++g_c438.ended;
    const void* const world = owner != nullptr ? g_actor_get_world(owner) : nullptr;
    for (int s = 0; world != nullptr && s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(world, s);
        const void* const pawn =
            pc != nullptr && sprint_side(pc) == kSideServerCopy ? read_ptr(pc, kControllerPawn) : nullptr;
        if (pawn != nullptr && vec_dist(actor_location(owner), actor_location(pawn)) <= kC438NearUu) {
            ++g_c438.ended_near;
            return;
        }
    }
}

// An event that names a held row's component. True when it is a stop of one (nothing is sent for it).
static bool c438_op(const Pending& p, const void* made) {
    bool stopped = false;
    for (int32_t i = 0; i < kC438Held; ++i) {
        C438Row& r = g_c438.rows[i];
        const void* const comp = r.p.active ? g_weak_get(&r.made) : nullptr;
        const bool same = comp != nullptr && ((p.of != 0 && p.of == r.p.id) || comp == p.component || comp == made);
        const void* const owner = same ? g_weak_get(&r.owner) : nullptr;
        if (same && (is_stop(p.point) || is_spawn(p.point) || owner == nullptr)) {
            r.p.active = false; // stopped, or its pooled component spawned again
            c438_count_ended(owner);
            c438_sys_ended(r.p);
            stopped = stopped || is_stop(p.point);
        } else if (same) {
            c438_release(r, owner, comp, &C438::early);
        }
    }
    return stopped;
}

// Each host tick, before the flush.
static void c438_step() {
    const ULONGLONG now = GetTickCount64();
    for (int32_t i = 0; i < kC438Held; ++i) {
        C438Row& r = g_c438.rows[i];
        if (!r.p.active) {
            continue;
        }
        const void* const owner = g_weak_get(&r.owner);
        const void* const made = g_weak_get(&r.made);
        if (owner == nullptr || made == nullptr) {
            r.p.active = false;
            c438_count_ended(owner);
            c438_sys_ended(r.p);
        } else if (c41_guid(owner) != 0 || now - r.at_ms >= kC438HoldMs) {
            c438_release(r, owner, made, &C438::at_world);
        }
    }
}

// Card 438 diagnostic: the top systems by held count, one row, then the tallies start over.
static void c438_report_systems(const char* when) {
    char text[1024];
    size_t used = 0;
    bool taken[kAssets]{};
    int32_t top = -1;
    for (int32_t k = 0; k < kC438TopSystems; ++k) {
        int32_t best = -1;
        for (int32_t i = 0; i < g_host.assets.n && i < kAssets; ++i) {
            const bool better = !taken[i] && g_c438_sys[i].held != 0 &&
                                (best < 0 || g_c438_sys[i].held > g_c438_sys[best].held);
            best = better ? i : best;
        }
        if (best < 0) {
            break;
        }
        taken[best] = true;
        top = top < 0 ? best : top;
        const HostAsset& a = g_host.assets.rows[best];
        const void* const obj = a.object != nullptr && g_weak_get != nullptr
                                    ? g_weak_get(&a.weak) : nullptr;
        const int n = snprintf(text + used, sizeof(text) - used, "%s %s held %u ended %u", k == 0 ? "" : ",",
                               obj != nullptr ? object_name(obj).text : a.object != nullptr ? "(gone)" : "(tag)",
                               g_c438_sys[best].held, g_c438_sys[best].ended);
        used += n > 0 && static_cast<size_t>(n) < sizeof(text) - used ? static_cast<size_t>(n) : 0;
    }
    if (top >= 0 || g_c438_no_asset != 0) {
        const void* const cls = top >= 0 ? g_c438_sys[top].owner_class : nullptr;
        log::line("card438: host Fx held by system (%s):%s; no asset row %u; owner class of the top one %s", when,
                  used > 0 ? text : " none", g_c438_no_asset, cls != nullptr ? object_name(cls).text : "-");
    }
    memset(g_c438_sys, 0, sizeof(g_c438_sys));
    g_c438_no_asset = 0;
}

// Per map, before the journal and name table start over: held rows name the old map's rows and are dropped.
static void c438_report(const char* when) {
    C438& h = g_c438;
    uint32_t still = 0;
    for (int32_t i = 0; i < kC438Held; ++i) {
        still += h.rows[i].p.active ? 1 : 0;
        h.rows[i].p.active = false;
    }
    if (h.held + h.full != 0) {
        log::line("card438: host Fx map totals (%s): held for a NetGUID %u, sent by guid %u (longest wait %llu ms), sent "
                  "at the world point after %llu ms %u, sent early for an op %u, ended while held %u (owner within %.0f uu "
                  "of a client pawn %u), still held %u, list full (sent at the world point) %u", when, h.held,
                  h.by_guid, static_cast<unsigned long long>(h.longest_ms),
                  static_cast<unsigned long long>(kC438HoldMs), h.at_world, h.early, h.ended, kC438NearUu,
                  h.ended_near, still, h.full);
    }
    c438_report_systems(when); // card 438 diagnostic (b138)
    h.want_id = 0;
    h.held = h.by_guid = h.at_world = h.ended = h.full = h.early = h.ended_near = 0;
    h.longest_ms = 0;
}

void c136_net_queue(uint32_t id, const C136Call& c, const void* owner, const void* instigator, const char* key,
                    uint32_t of) {
    if (g_client_session) {
        return;
    }
    if (g_is_in_game_thread != nullptr && !g_is_in_game_thread()) {
        ++g_host.counts.off_thread; // the queue and journal are game thread only
        return;
    }
    if (is_decal(c.point) && c132_decal_gore(c.asset)) {
        ++g_host.counts.gore; // card 132's Decal rows relay the gore decals
        return;
    }
    if (local_only(c, owner, instigator)) {
        ++g_host.counts.local; // the ownerless rule: a 2D or widget effect stays on each machine
        return;
    }
    if (trail_local(c, owner)) {
        ++g_host.counts.trail; // run 6 fix C: each side runs its own projectile and gore part trails
        return;
    }
    if (c136_attack_local(c, owner)) {
        ++g_host.counts.attack; // card 408: each side runs its own flak and plasma bug building attack
        return;
    }
    if (wound_local(c)) {
        ++g_host.counts.wound; // card 438 step 2: each side runs its own wound shading compute system
        return;
    }
    if (c.asset == nullptr && c.tag == 0 && c.component == nullptr) {
        ++g_host.counts.no_asset; // run 6 fix F: no asset and no cue tag (a decal with no material): draws nothing
        return;
    }
    const void* fp_pawn = nullptr;
    const FpKind fp = fp_view(c, &fp_pawn);
    if (fp == FpKind::Drop) {
        ++g_host.counts.fp_drop; // camera (screen) effects and first-person component ops are not sent
        return;
    }
    Pending& p = g_host.pending;
    p = Pending{};
    p.active = true;
    p.id = id;
    p.point = c.point;
    p.flags = c.flags;
    const bool sound = c.point == C136Point::PlaySound || c.point == C136Point::SpawnSound ||
                       c.point == C136Point::SoundAttached;
    const void* const player = sound ? c304_player_pawn(owner) : nullptr;
    if (player != nullptr && sprint_side(player) == kSideHostOwn) {
        p.flags |= kC136FlagPlayerSource; // card 304: the host's own player's sound (its pawn or weapon)
        // b9: the owner's class and key kind, so a client row can be matched to its owner (capped per map).
        if (note_event_cap(g_host.counts.c304_lines, kC304HostLines, "card304 host lines")) {
            log::line("card304: host t=%llu player sound %s owner %s (%s) key %s",
                      static_cast<unsigned long long>(GetTickCount64()), object_name(c.asset).text,
                      object_name(owner).text, class_name(owner).text, key);
        }
    }
    p.of = of;
    p.asset = host_asset(c.asset, c.asset != nullptr ? 0 : c.tag);
    p.physmat = -1;
    p.atten = host_asset(c.atten, 0);
    p.conc = host_asset(c.conc, 0);
    p.component = c.component;
    p.filter = make_filter(c, owner, instigator);
    p.at = c.at;
    p.rot = c.rot;
    p.scale = c.scale;
    memcpy(p.look, c.look, sizeof(p.look));
    if (c.cue_params != nullptr) {
        p.at = read_vec3(c.cue_params, kCueLocation);
        p.rot = read_vec3(c.cue_params, kCueNormal);
        const void* const physmat =
            g_weak_get != nullptr ? g_weak_get(static_cast<const uint8_t*>(c.cue_params) + kCuePhysMat) : nullptr;
        p.physmat = host_asset(physmat, 0);
    }
    p.attached = c.attach != nullptr;
    p.world = c.attach != nullptr ? vec_add(read_vec3(c.attach, kComponentTranslation), c.at) : p.at;
    p.comp = host_object_name(p, c.attach != nullptr ? c.attach : c.component);
    p.socket = c.socket != 0 ? host_fname(p, &c.socket) : -1;
    p.owner_class = owner != nullptr ? host_object_name(p, read_ptr(owner, kObjectClass)) : -1;
    p.placed = -1;
    host_key(p, key, owner);
    if (strcmp(key, "guid") == 0 && p.guid == 0 && p.placed < 0 && owner != nullptr &&
        is_a(owner, g_actor_class()) && !c438_want(p, owner)) {
        // Card 434 step 2 (diagnostic): the row goes out with the world key; an attached effect is then drawn
        // at its world point on the client and does not follow the owner.
        char why[160];
        snprintf(why, sizeof(why), "FX %s (%s) owner has no NetGUID",
                 c.asset != nullptr ? object_name(c.asset).text : "tag", p.attached ? "attached" : "at a point");
        c434_owner_row(owner, why, 0.0, true);
    }
    if (fp == FpKind::Remap) {
        host_fp_remap(p, c, fp_pawn);
    }
}

void c136_net_done(uint32_t id, const void* made) {
    Pending& p = g_host.pending;
    if (g_client_session || !p.active || p.id != id) {
        return;
    }
    p.active = false;
    const int32_t seed = made != nullptr && is_spawn(p.point) ? read_i32(made, kNiagaraSeedOffset) : 0;
    if (c438_op(p, made) || c438_hold(p, made, seed)) { // card 438: a stop of a held row, or held now
        return;
    }
    host_emit(p, made, seed);
}

// The event's row: journal update, then the E, D or stop row to the queue.
static void host_emit(const Pending& p, const void* made, int32_t seed) {
    const uint32_t of = host_stop_of(p);
    static Bytes body; // game thread only
    body = Bytes{};
    if (p.too_long) {
        ++g_host.counts.too_long;
        host_warn_once("event names a string over 255 bytes, not sent (bad (too long))", p.id);
    } else if (!host_body(p, seed, of, body)) {
        ++g_host.counts.bad_rows;
        host_warn_once("event row does not fit", p.id);
    }
    const bool ok = !p.too_long && !body.bad;
    if (!ok) {
        if (is_stop(p.point)) {
            journal_apply(p, made, body); // a stop still ends its journal row
        }
        return;
    }
    const bool kept = journal_apply(p, made, body);
    if (is_stop(p.point) && of != 0) {
        Bytes row{}; // D row: the client stops its copy of row `of`
        put_u8(row, kTagStop);
        put_u8(row, static_cast<uint8_t>(p.point));
        put_varint(row, p.id);
        put_varint(row, of);
        host_enqueue(p.filter, row, FxMeta{p.asset, p.world, FxKind::End, 0, of}, kNoNames);
        return;
    }
    host_enqueue(p.filter, tagged(kTagEvent, body),
                 FxMeta{p.asset, p.world, kept ? FxKind::Journal : (is_stop(p.point) ? FxKind::End : FxKind::OneShot),
                        kept ? p.id : 0u, 0u},
                 name_refs(p));
}

void c136_client_extra(const void* asset) {
    ClientExtras& x = g_client.extras;
    if (g_is_in_game_thread != nullptr && !g_is_in_game_thread()) {
        ++x.off_thread; // the table is game thread only
        return;
    }
    ObjectName name{};
    snprintf(name.text, sizeof(name.text), "%s", asset != nullptr ? object_name(asset).text : "-");
    const char* const text = name.text; // keyed by name, not address: a freed asset's address can be reused
    for (int32_t i = 0; i < x.n; ++i) {
        if (strcmp(x.rows[i].text, text) == 0) {
            ++x.rows[i].n;
            return;
        }
    }
    if (x.n == kClientExtras) {
        ++x.full;
        return;
    }
    ClientExtra& r = x.rows[x.n++];
    r.n = 1;
    snprintf(r.text, sizeof(r.text), "%s", text);
}

// Ownerless located effects are the host's (no filter input hits, so they are blocked); 2D and widget ones are kept.
bool c136_client_own(const C136Call& c, const void* owner, const void* instigator) {
    if (local_only(c, owner, instigator) || trail_local(c, owner) || c136_attack_local(c, owner)) {
        return true; // card 408: the client's own building attack effects, from the host's synced turn and timeout
    }
    if (wound_local(c)) {
        static uint32_t kept = 0; // card 438 step 2: proof row, first 4 per process
        if (note_event_cap(kept, 4, "card438 client wound")) {
            log::line("card438: client t=%llu kept its own wound shading call (point %d) on %s",
                      static_cast<unsigned long long>(GetTickCount64()), static_cast<int>(c.point),
                      owner != nullptr ? object_name(owner).text : "no owner");
        }
        return true;
    }
    // Card 137 step 6a: the client's pooled gore parts carry the client pawn as owner or instigator, so their splats
    // (BloodGib Niagara, Splats_MS) passed filter_hits and played beside the host's rows. A gore part's located
    // effects are the host's; its trail ops stay local (trail_local, above).
    if (owner != nullptr && g_gore_part_class != nullptr && is_a(owner, g_gore_part_class())) {
        return false;
    }
    return filter_hits(make_filter(c, owner, instigator), g_client.pc, g_client.pawn);
}

void c136_step(void* engine) {
    void* const world = g_get_game_world != nullptr ? g_get_game_world(engine) : nullptr;
    if (g_client_session) {
        g_client.world = world;
        g_client.pc = world != nullptr ? g_get_player_controller(world, 0) : nullptr;
        g_client.pawn = g_client.pc != nullptr ? read_ptr(g_client.pc, kControllerPawn) : nullptr;
        // Card 325: one count row after the last frame of a join replay (the replay can span several frames), to
        // compare with the host's row at c136_join_replay.
        const ULONGLONG at = g_client.counts.replay_ms;
        if (at != 0 && GetTickCount64() - at >= kClientReplayQuietMs) {
            g_client.counts.replay_ms = 0;
            if (note_event_cap(g_client.counts.niagara_rows, kClientNiagaraRows, "card325 placed niagara")) {
                niagara_count_row(world, "last frame with replay rows");
            }
        }
        return;
    }
    if (world == nullptr) {
        return;
    }
    journal_sweep();
    c438_step();
    host_flush(world);
}

void c136_receive_fx(void* pc, const WireString* s) {
    if (!g_client_session) {
        return;
    }
    if (g_client.pc == nullptr) {
        g_client.pc = pc;
    }
    static uint8_t bytes[kChanBlobBytes]; // game thread only
    const int32_t n = chan_blob(s, bytes, kChanBlobBytes);
    if (n <= 0) {
        ++g_client.counts.bad_frames;
        return;
    }
    Reader in{bytes, n, 0, false};
    const uint32_t replayed = g_client.counts.replayed;
    for (int32_t k = 0; k < kChanBlobBytes && in.at < in.n; ++k) {
        if (!client_row(in)) {
            ++g_client.counts.bad_rows;
            if (g_client.counts.bad_rows <= kWarnLines) {
                log::line("WARN card136: client fx bad row at byte %d of %d (tag %u): rest of frame dropped", in.at,
                          in.n, static_cast<unsigned>(bytes[in.at < in.n ? in.at : in.n - 1]));
            }
            return;
        }
    }
    if (g_client.counts.replayed != replayed) {
        g_client.counts.replay_ms = GetTickCount64(); // c136_step writes the count row once the burst is over
    }
}

void c136_join_replay(void* pc) {
    if (g_client_session || pc == nullptr) {
        return;
    }
    Peer& peer = host_peer(pc);
    peer.table_sent = 0;
    memset(peer.names_had, 0, sizeof(peer.names_had)); // card 395: as the asset table, the names go again
    const void* const pawn = read_ptr(pc, kControllerPawn);
    const Vec3 at = pawn != nullptr ? actor_location(pawn) : Vec3{};
    const Vec3* const pawn_at = pawn != nullptr ? &at : nullptr;
    static Frame fr; // game thread only
    fr.pc = pc;
    fr.peer = &peer;
    fr.len = 0;
    fr.names_n = 0;
    fr.names_out = 0;
    frame_assets(fr, peer);
    const Journal& j = g_host.journal;
    uint32_t rows = 0;
    uint32_t skipped = 0;
    for (int32_t i = 0; i < j.n; ++i) {
        const JournalRow& r = j.rows[i];
        if (!journal_alive(r) || journal_active(r.comp) == 0 || filter_hits(r.filter, pc, pawn)) {
            continue;
        }
        if (r.runtime_target) {
            ++skipped;
            continue;
        }
        const Bytes& row = tagged(kTagReplay, r.body);
        if (frame_add_named(fr, r.names, row)) {
            fx_stat(FxMeta{r.asset, r.at, FxKind::Journal, r.id, 0}, row.n, pawn_at, peer_slot(peer));
        }
        ++rows;
    }
    frame_send(fr);
    ++g_host.counts.replays;
    g_host.counts.replay_rows += rows;
    g_host.counts.replay_skipped += skipped;
    log::line("card136: host fx join replay %s: %d asset rows, %d name rows (table %d), %u journal rows of %d, %u "
              "replay skipped (run-time target)", object_name(pc).text, g_host.assets.n, fr.names_out, g_host.names.n,
              rows, j.n, skipped);
    niagara_count_row(g_actor_get_world(pc), object_name(pc).text);
}

// Per map, from c136_report: the totals line, then the host's table, journal and peers start over (card 325: with
// keep_state, the host's map arm, only the counts start over). The client keeps its asset table (the host resends each
// map's rows by index before using them) and drops its live map.
void c136_net_arm_map(const char* when, bool keep_state) {
    if (!g_client_session) {
        c438_report(when);
        const HostCounts& k = g_host.counts;
        if (k.wound != 0) {
            log::line("card438: host wound shading calls kept local, not sent (%s): %u", when, k.wound);
        }
        log::line("card136: host fx net map totals (%s): %u rows queued, %u rows sent in %u frames (%u failed), %u assets, "
                  "journal %d (peak %d), %u ended, %u replays (%u rows, %u replay skipped (run-time target)), dropped: %u off thread, %u queue full, %u "
                  "asset table full, %u journal full, %u bad; %u gore decals left to card 132; %u stale asset rows, %u "
                  "local only, %u local trail, %u card 408 local, %u no asset, %u finished, first person %u remapped (%u at world), %u not "
                  "sent; %u bad (too long), %d names of %d (%u table full), %u event rows %llu bytes (average %.1f)",
                  when, k.queued, k.rows_sent, k.frames, k.frames_failed, g_host.assets.n, g_host.journal.n,
                  g_host.journal.peak, k.ended, k.replays, k.replay_rows, k.replay_skipped, k.off_thread, k.overflow, k.assets_full,
                  k.journal_full, k.bad_rows, k.gore, k.assets_stale, k.local, k.trail, k.attack, k.no_asset, k.finished,
                  k.fp_remap, k.fp_miss,
                  k.fp_drop, k.too_long, g_host.names.n, kNames, k.names_full, k.event_rows,
                  static_cast<unsigned long long>(k.event_bytes),
                  k.event_rows != 0 ? static_cast<double>(k.event_bytes) / k.event_rows : 0.0);
        fx_stats_rows(when);
        g_host.counts = HostCounts{};
        if (keep_state) {
            return;
        }
        g_host.pending = Pending{};
        g_host.assets.n = 0;
        g_host.names.n = 0;
        memset(g_host.names.slots, 0, sizeof(g_host.names.slots));
        g_host.queue.n = 0;
        g_host.journal.n = 0;
        g_host.journal.peak = 0;
        g_host.journal.cursor = 0;
        for (Peer& p : g_host.peers) {
            p = Peer{};
        }
        memset(g_host.sent, 0, sizeof(g_host.sent)); // card 329 B: the journal it marks is gone
        return;
    }
    const ClientCounts& k = g_client.counts;
    log::line("card136: client fx net map totals (%s): %u rows, %u replayed, %u played, %u ended, misses: %u asset, %u key, "
              "%u target, %u sound setting; %u unsupported, %u bad (%u bad frames), %u live full (peak %d); live %d; assets held %d, %u freed while held, %u not held (no game instance); %u host-only components played at world",
              when, k.rows, k.replayed, k.played, k.ended, k.no_asset, k.no_key, k.no_target, k.no_setting, k.unsupported,
              k.bad_rows, k.bad_frames, k.live_full, g_client.live_peak, g_client.live_n, client_assets_held(),
              k.asset_lost, k.asset_unheld, k.host_only);
    const ClientExtras& x = g_client.extras;
    log::line("card136: client fx extra: %d assets, %u table full, %u off thread", x.n, x.full, x.off_thread);
    for (int32_t i = 0; i < x.n; ++i) {
        log::line("card136: client fx extra %u %s", x.rows[i].n, x.rows[i].text);
    }
    for (ClientAsset& a : g_client.assets) { // the map is going: its assets are let go (the table rows stay)
        client_asset_release(a);
    }
    g_client.extras.n = 0;
    g_client.extras.full = 0;
    g_client.extras.off_thread = 0;
    g_client.live_n = 0;
    g_client.live_peak = 0;
    g_client.counts = ClientCounts{};
    // Card 331: the old world's PC and pawn. c136_receive_fx keeps a set g_client.pc, so a frame handled before the
    // new map's first c136_step would have used the freed PC as its world context; the frame's own PC is used instead.
    g_client.world = nullptr;
    g_client.pc = nullptr;
    g_client.pawn = nullptr;
}

} // namespace ubw::hooks
