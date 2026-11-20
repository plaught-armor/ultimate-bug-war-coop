// Card 132: the corpse pose journal. A ragdoll simulates on each side on its own, so a client corpse comes to rest
// somewhere other than the host's (a different floor contact, an impulse the client never saw). The host keeps one row
// per corpse: tracking starts at its ragdoll start (detour_c132_set_simulate), and every kPoseStepMs the host samples
// the corpse's physics bodies. Once the corpse settles (no body awake, or no body moved more than kPoseStill uu for
// kPoseStillMs; after kPoseForceMs it is sent anyway, flagged unsettled) the host snapshots every body's world
// transform and sends it. A corpse that moves more than kPoseMoved uu after a send is snapshotted again on its next
// settle, and the new snapshot replaces the client's row. A corpse whose actor is gone or whose mesh is hidden
// (gibbed, destroyed) loses its row, and the client is told ("R"). A mesh that stops simulating counts as settled.
//
// Payload: binary, base64 on the wire. Root (body 0) world position as 3 float32, then per body (in Bodies order; one
// physics asset, so one order on both sides) the position relative to the root as 3 int16 in kPoseUnit uu and the
// rotation as 4 int16 (quaternion times 32767); frames carry the card 521 wire form (pose_wire_pack). Frames: "P <guid> <snap> <part> <parts> <bodies> <flags> <b64>",
// chunked to kChanTextMax; parts arrive in order on the reliable channel. Removal: "R <guid>".
//
// The client resolves the guid and, once its own copy simulates with the same body count, sets every body to the host's
// transform (FBodyInstance::SetBodyTransform, TeleportPhysics, its own scale), zeroes the velocities and puts the
// bodies to sleep. It logs the root distance before and after and the largest body position error read back. A row
// that cannot apply yet stays pending and is retried every step for kPosePendingMs (a row whose guid is not mapped
// yet: until the copy arrives or the host removes the row). At each digest round the client
// checks every applied corpse body to body against the host's last snap (c132_pose_check, kPoseTolerance uu).
// A late joiner gets the current rows on its first Pong (c132_pose_replay).
//
// Card 152, the fall stream. While a host corpse moves and no snap is due, each step's sample goes to every ponged PC
// as one frame with flag kPoseStream; the frame's snap field carries the row's sample number. Sample 0 goes out with
// the ragdoll start. Samples are encoded into their own buffer, never into the row's blob, so the join replay and the
// host self-check still see only the end state. At most kPoseStreamPerStep samples go per step (the rest wait a step,
// oldest sample first), and a sample is skipped while a PC's pace queue holds more than kPoseStreamBacklog frames.
// The client's streamed copy simulates with gravity off (r2: pose_stream_start, through the card 039/132 start) and
// its bodies are teleported each tick between the last two samples, velocities zeroed; an unfreeze row continues the
// stream. The final row (settled, frozen or unsettled) turns gravity back on and is posed by the snap path. The
// game's own ragdoll start on a copy with no row yet is held (a fixed wait table) until the stream row comes, or for
// kPoseStartWaitMs, after which the copy's own ragdoll starts with a WARN.

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

PoseBodyWorldFn g_pose_body_world = nullptr;
PoseBodySetFn g_pose_body_set = nullptr;
PoseMeshAwakeFn g_pose_mesh_awake = nullptr;
PoseMeshVoidFn g_pose_mesh_sleep = nullptr;
PoseMeshVelFn g_pose_mesh_linear = nullptr;
PoseMeshVelFn g_pose_mesh_angular = nullptr;
PoseMeshGravityFn g_pose_mesh_gravity = nullptr;

uint64_t c41_guid(const void* actor);             // enemies.cpp
void* c41_resolve_guid(void* pc, uint64_t guid);  // enemies.cpp
bool c39_mesh_simulating(const void* mesh);       // enemies.cpp
bool c38_is_marked(const void* actor);            // enemies.cpp
void c132_bp_text(char* out, size_t size);        // enemies.cpp: the Blueprint caller chain (card 291)
void detour_c132_set_simulate(void* mesh, bool simulate); // enemies.cpp

namespace {

constexpr int32_t kPoseRows = 128;           // corpses tracked per side; more are counted as dropped
constexpr int32_t kPoseBodies = 64;          // a bug's physics asset has a few dozen bodies
constexpr size_t kPoseBodyBytes = 14;        // 3 int16 position + 4 int16 rotation
constexpr size_t kPoseRootBytes = 12;        // root: 3 float32
// Card 359 (b136): then the host mesh component's world scale, 3 uint16 in steps of 1 / kPoseScaleSteps (0: unread).
// The game's RandomiseSize rolls each copy's scale on its own side (RandomFloatInRange(0.95, 1.05) per axis), and a
// copy posed from host bodies with another scale keeps its bone 0 off body 0 (b135b: 91 of 106 card191 rows).
constexpr size_t kPoseHeadBytes = kPoseRootBytes + 3 * sizeof(uint16_t);
constexpr double kPoseScaleSteps = 8192.0; // range up to 8.0, quantisation error under 0.0001
constexpr double kC359ScaleTolerance = 0.001;
constexpr size_t kPoseBlob = kPoseHeadBytes + kPoseBodies * kPoseBodyBytes;
constexpr size_t kPoseB64 = (kPoseBlob + 2) / 3 * 4;
constexpr size_t kPoseChunk = 1320;          // b64 chars per frame (a multiple of 4), under kChanTextMax with the head
static_assert(kPoseB64 <= kPoseChunk, "card 152: a stream sample must fit one frame");
constexpr double kPoseUnit = 0.25;           // uu per int16 step of a body's position relative to the root
constexpr double kPoseRelMax = 32767 * kPoseUnit;
constexpr ULONGLONG kPoseStepMs = 250;
constexpr double kPoseFreezeTolerance = 2.0; // uu: a frozen copy one tick after its freeze (card 140 run 13)
constexpr double kPoseStill = 2.0;           // uu per sample: a body that moved less is still
constexpr ULONGLONG kPoseStillMs = 1000;
constexpr ULONGLONG kPoseForceMs = 8000;     // a corpse still moving this long after its start is sent anyway
constexpr double kPoseMoved = 20.0;          // uu: a sent corpse whose root moved more is snapshotted again
constexpr uint8_t kPoseQueryAndPhysics = 3; // ECollisionEnabled::QueryAndPhysics, the Ragdoll profile's
constexpr ULONGLONG kPosePendingMs = 15000;  // client: a row that cannot apply this long gets a WARN and is dropped
constexpr double kPoseTolerance = 100.0;     // uu: digest check, each client body to the host's
constexpr int32_t kTeleportPhysics = 1;      // ETeleportType::TeleportPhysics
constexpr size_t kCharacterMesh = 0x328;     // ACharacter::Mesh
constexpr size_t kSkeletalBodies = 0xB80;    // USkeletalMeshComponent::Bodies (TArray<FBodyInstance*>)
constexpr size_t kCompHiddenByte = 0x1A1;    // bit 0x08 bHiddenInGame
constexpr size_t kKinematicBonesUpdateType = 0xA6C; // USkeletalMeshComponent, TEnumAsByte (1 = SkipAllBones)
constexpr uint8_t kCompHiddenBit = 0x08;
constexpr uint8_t kSkipAllBones = 1;          // EKinematicBonesUpdateToPhysics::SkipAllBones
constexpr size_t kBoolByteOffset = 0x71;      // FBoolProperty::ByteOffset (audit_npc.cpp layout note)
constexpr size_t kBoolFieldMask = 0x73;       // FBoolProperty::FieldMask
constexpr uint8_t kPoseUnsettled = 1;        // flags: sent at kPoseForceMs, still moving
// Card 137 Pose:1: the host's mesh does not simulate (the game's SetAllBodiesSimulatePhysics(0) froze the corpse,
// hb67 guid 0x1ac at exe+0x3B05087). The client poses its copy, then turns its simulation off too.
constexpr uint8_t kPoseFrozen = 2;
// Card 137 run 2: the snap the host sends at the game's SetAllBodiesSimulatePhysics(1) on a tracked corpse. The client
// counts every row with it that it applies, with its copy's simulating state after the apply (the senior's check
// against the host's unfreeze count; the old counter saw only frozen-to-live rows on the client and counted 0 in hb68).
constexpr uint8_t kPoseUnfreeze = 4;
constexpr uint32_t kPoseLines = 60;          // per side per map
constexpr uint32_t kPoseWarnLines = 20;
constexpr uint32_t kPoseSettleLines = 20; // host (card 203): moved-but-under-tolerance self-check lines per map
constexpr uint32_t kPoseFreezeBlockLines = 16; // client: card 152 r3 game-freeze block lines per map
constexpr uint32_t kPoseBlockLines = 40;      // client: card 146 block lines per map, apart from kPoseLines
constexpr uint32_t kPoseHiddenLines = 40;    // host: hidden-mesh ragdoll trace lines per map
constexpr double kPoseFellUu = 1000.0;       // host: body 0 this far below its start z fell through the world
constexpr uint8_t kPoseStream = 8;           // card 152: a stream sample, not a snap (the snap field is the sample number)
// Card 521 step 2: a stream sample sent as zigzag varint deltas from the row's previous sample (stream_base). Every
// kPoseWholeEvery-th sample goes whole, so a client that missed one waits at most that many samples.
constexpr uint8_t kPoseDelta = 16;
constexpr uint16_t kPoseWholeEvery = 8;
constexpr uint32_t kC524Rows = 80; // card 524: per-guid start offset rows per map
constexpr ULONGLONG kPoseLateMs = kPoseStepMs + 100; // card 523: a chained sample this late held the copy 100+ ms
constexpr uint32_t kPoseDeltaLines = 20;     // client: delta miss rows per map
constexpr int32_t kPoseStreamPerStep = 24;   // host: stream samples per step (card 152 cap arithmetic: 42.9 KB/s)
// Host: a PC whose pace queue holds more frames than this has the mod channel backed up; a stream sample is replaceable
// by the next one, so it is skipped for this step rather than queued behind the backlog. 8 frames is two steps of a
// 4-corpse fall, far over the 0 to 1 frames a paced PC holds between drains.
constexpr uint32_t kPoseStreamBacklog = 8;
constexpr int32_t kPoseWaits = 32;           // client: copies whose game ragdoll start waits for the stream row
// c152-r1: 2 of 3 fallbacks at 500 ms came from host lag (the host's sample 0 went about 600 ms after the client's
// death edge); r2 waits 2000 ms and logs each resolved wait's time.
constexpr ULONGLONG kPoseStartWaitMs = 2000; // client: no stream row by then: the copy's own ragdoll starts, WARN
// c152-r2: the client's 60 lines were used up during part 2b (95685494); r3's six real kills add 12 lines there.
constexpr uint32_t kPoseStreamLines = 120;   // card 152 lines per side per map
constexpr uint32_t kPoseBoneAgainLines = 24; // card 186: frozen-final bone checks one tick later, per map
constexpr uint32_t kC191Lines = 12;           // card 191: rows per map
constexpr uint8_t kC191Ticks = 2;             // card 191: end ticks logged after each final's freeze
constexpr size_t kC191Registered = 0x94;      // card 191: component byte, bit 0 tested by ExecuteTick 0x41874F0
constexpr size_t kC191TickTarget = 0x28;      // card 191: FSkeletalMeshComponentEndPhysicsTickFunction::Target
constexpr int32_t kPoseBoneRows = 3;         // client: held copies whose bone 0 is checked against body 0 (card 152 (a))
constexpr uint8_t kPoseBoneLines = 8;        // client: bone check lines per checked copy
constexpr double kPoseTargetUu = 10.0;       // client: body 0 this far from the last target set on it is a WARN
// USkeletalMeshComponent::EndPhysicsTickFunction.InternalData (bit 0 of the pointee: registered), read as
// RegisterEndPhysicsTick at exe+0x3AEF5D0 reads it; ShouldBlendPhysicsBones is vtable slot 0xC80 (exe+0x4014F40).
constexpr size_t kEndTickData = 0xD70;
constexpr size_t kShouldBlendSlot = 0xC80;
// Card 191 diagnostic: VisibilityBasedAnimTickOption (ShouldTickPose exe+0x3AF85C0 reads it, above 1 skips a mesh not
// recently rendered); the bits 0x08 and 0x10 at +0xA80 it also tests; RequiredBones (TArray, count at +8), passed to
// PerformBlendPhysicsBones by BlendInPhysicsInternal (exe+0x3FEAB45); ComponentToWorld scale.
constexpr size_t kMeshVisTickOption = 0x7BC;
constexpr size_t kMeshTickFlags = 0xA80;
constexpr size_t kMeshRequiredBones = 0xB60;
constexpr size_t kComponentScale = kComponentRotation + 0x40;

// Freezing (client, card 140 run 13): a frozen row's bodies are posed and still simulating; the freeze runs on the
// next client step, after physics has written the posed bodies into the bones.
enum class PoseState : uint8_t { Moving, Sent, Pending, Freezing, Applied };

struct alignas(16) Xform { // FTransform (double): rotation x y z w, translation, scale, each padded to 0x20
    double q[4];
    double t[4];
    double s[4];
};
static_assert(sizeof(Xform) == 0x60);

// Card 359 (b187), client: bone 0 in component space and the mesh's component transform, taken at a final's apply
// and after each waited end tick's blend (the last one stays), printed on the WARN card191 frozen row. A moved
// component with unchanged component-space bones means the blend did not write; both unchanged, a stale bone read.
struct C359Snap {
    float cs[3];      // bone 0, component space
    float at[3];      // component translation
    float q[4];       // component rotation (quat x, y, z, w)
    float sc[3];      // component scale
    int8_t rendered;  // the actor rendered within 0.2 s: 1, 0; -1 unread
    bool ok;          // taken (bone 0 and body 0 read)
};

struct C191Bone {
    Vec3 cs;  // bone 0 in component space
    Vec3 w;   // bone 0 in world space (card 359, b190)
    Vec3 b0;  // body 0 in world space (card 359, b191)
    double d; // world distance from bone 0 to body 0
    bool ok;
};

struct PoseRow {
    C31Weak actor;
    C31Weak pc;              // client: the PC the frames came in on (for c41_resolve_guid)
    uint64_t guid;
    PoseState state;
    uint8_t flags;
    uint16_t snap;           // host: snapshots sent; client: the snapshot held
    int32_t bodies;
    ULONGLONG start, still_ms, got;
    Vec3 sent_root;          // host: root at the last send; client: the host's root
    Vec3 last[kPoseBodies];  // host: body positions at the previous sample
    uint8_t blob[kPoseBlob]; // the encoded snapshot (host: last sent; client: received)
    size_t blob_n;
    char b64[kPoseB64 + 1];  // client: parts received so far
    size_t b64_n;
    int32_t parts_got, parts;
    double start_z;          // host: body 0 z at the first sample after the first ragdoll start
    bool have_start, fell;   // host: start_z set; the corpse passed the kPoseFellUu mark (card 189: traced once)
    bool c189_stopped;       // host (card 189): simulation stopped and held (no floor, or under the kill z)
    bool c189_check;         // host (card 189): body 0 one sample after the stop is still to be logged
    Vec3 c189_at;            // host (card 189): body 0 just after the stop call
    bool unfreeze_next;      // host: the next snapshot is the game's unfreeze (kPoseUnfreeze)
    bool owed;               // host: the last snap was made while the corpse had no NetGUID; sent once it has one
    ULONGLONG applied_at;    // client: when the row was last applied (card 140 first-drift diag)
    uint64_t posed_frame;    // client: the c132_pose_step call that posed a Freezing row; it freezes on a later one
    float err_apply;         // client, frozen row: worst body error just after the apply, while simulating
    float err_freeze;        // client, frozen row: worst body error just after the freeze call
    bool check_owed;         // client, frozen row: its error one tick after the freeze is still to be logged
    bool drift_logged;       // client: the frozen row's first-drift line was written
    bool held;               // pose_hold set bBlendPhysics and SkipAllBones (client: frozen copy; host: card 189)
    bool woke;               // host, card 339: a body moved kPoseStill or more since the last snap (any limb, not body 0 only)
    bool sim_skip_logged;    // host, card 339: this row's first skipped unchanged simulate call is logged
    void* c191_mesh;         // client, card 191: the final's mesh, armed at the apply, while its end ticks are logged
    uint8_t c191_ticks;      // client, card 191: end ticks logged since the freeze
    bool c191_frozen;        // client, card 191: pose_freeze_step ran since the apply that armed c191_mesh
    uint8_t c191_pre;        // client, card 191: end tick entries between the apply and the freeze
    uint8_t c191_seen;       // client, card 191: end tick entries since the freeze
    float c191_apply_before; // client, card 191 (arrival): bone 0 to body 0 just before the bodies were posed, -1 unread
    float c191_apply_after;  // client, card 191 (arrival): the same right after the pose
    float c359_before;       // client, card 359: bone 0 to body 0 before the last waited end tick's blend, -1 unread
    float c359_after;        // client, card 359: the same right after that blend
    float c359_weight;       // client, card 359: lowest body PhysicsBlendWeight at the first waited tick, -1 unread
    C359Snap c359_apply;     // client, card 359 (b187): at the final's apply
    C359Snap c359_last;      // client, card 359 (b187): after the last waited end tick's blend
    mutable bool c359_scale_warned; // client, card 359 step 5: the scale mismatch row was logged for this row
    bool held_blend;         // client: the copy's bBlendPhysics before the hold
    bool noskel_set;         // client, card 191: the freeze set bNoSkeletonUpdate on the copy (pose_release clears it)
    uint8_t freeze_waits;    // client, card 191: ticks the freeze waited for the bones to reach the posed bodies
    uint8_t held_update;     // client: the copy's KinematicBonesUpdateType before the hold
    int8_t cmc_tick_was;     // client, card 251: the movement component's tick before the hold (1 on, 0 off, -1 none)
    bool arrived;            // client, card 251: the copy arrived dead (card 137 arrival), for the card251 row
    uint16_t game_blocked;   // client: the game's own ragdoll starts blocked on this posed copy (card 146)
    // Card 152, the fall stream.
    bool stream_due;         // host: this step's sample moved and no snap is due (sent by pose_stream_send)
    ULONGLONG stream_at;     // host: when the row's last stream sample went (oldest first over the cap)
    uint16_t stream_seq;     // host: stream samples sent; client: the last one received
    // Card 521 step 2: the last stream sample in its 14 B form. Host: the last one broadcast, as the client decodes it
    // (wire-rounded). Client: the last one decoded, the base of the next delta (seq stream_seq + 1). Snaps leave it.
    uint8_t stream_base[kPoseBlob];
    size_t stream_base_n;
    bool streamed;           // client: a stream sample came; the copy simulates with gravity off while it streams
    bool live;               // client: r.blob is a stream sample, so pose_stream_tick moves the bodies toward it
    uint8_t prev[kPoseBlob]; // client: the sample before r.blob (same body count), where the move starts
    ULONGLONG cur_at;        // client: when r.blob came in
    uint8_t bone_lines;      // client: (a) check lines left for this copy (0: not checked)
    uint16_t bone_seq;       // client: the sample the last bone check was taken on
    uint64_t bone0;          // client: FName of body 0's bone (SkeletalBodySetups[0].BoneName)
    Vec3 hold_root;          // client: body 0 when the copy's stream started
    bool grav_off;           // client: pose_grav_off turned the streamed copy's gravity off; pose_grav_on undoes it
    bool own_fall;           // client (card 523): started at sample 0 with gravity on, falling on its own to sample 1
    Vec3 own_off;            // client (card 523 r2): sample 0's body 0 minus the copy's body 0 at the own-fall start
    Vec3 own_done;           // the part of own_off already moved (pose_own_shift)
    ULONGLONG own_at;        // the own-fall start
    bool sim_warned;         // client: the copy would not simulate at its stream start (one WARN per row)
    Vec3 target0;            // client: the last target pose_stream_move set on body 0
    bool bone_again;         // client (card 186): a frozen final's bone check is read once more on the next tick
    bool sim_at_final;       // client (card 186 r2): the copy simulated when its last snap row reached the apply
    bool sim_at_freeze;      // client (card 186 r2): the copy simulated at pose_freeze_step entry
    Vec3 bone0_cs;           // client (card 191): bone 0 in the mesh's component space at the stream start
    uint32_t rx_samples;     // client (card 269 B): stream samples received for this row
    uint32_t rx_chained;     // client (card 269 B): of those, samples that chained onto the one before
    bool c194_owed;          // client (card 194): re-posed after the game's own start; logged at the freeze check
    float c194_before;       // client (card 194): worst body error when the game's start was held again
    ULONGLONG c485_at;       // client (card 485 b209): when the freeze reposed the bodies; 0 = no first-render row owed
    Vec3 c485_cs;            // client (card 485 b209): bone 0 in component space at that repose
};

// Card 137 (hb70): client, a corpse whose copy arrived already dead (c38_death_edge, corpse). Its death is not played;
// the mesh is hidden until the host's pose is applied, so no frame shows it at the spot the copy spawned.
constexpr int32_t kArrivals = 32;
// No row by then: shown, the old death visual played, WARN. Card 146: the host's first snap of a corpse still in its
// ragdoll comes when it settles (3.4 to 7 s in the 2026-11-08 playtest host log) or at kPoseForceMs, so the wait
// covers kPoseForceMs plus a step and the trip.
constexpr ULONGLONG kArrivalWaitMs = kPoseForceMs + 1000;
// Card 146 (hb72 rejoin): the host's pose replay starts at the client's first Pong, which card 120 can hold for
// seconds after the copies arrive. kArrivalWaitMs counts from the later of the arrival and that Pong; while the Pong
// is held the arrival waits, up to this ceiling from the arrival.
constexpr ULONGLONG kArrivalHoldMaxMs = 30000;
struct PoseArrival {
    C31Weak actor;
    uint64_t guid;           // read again each tick while 0 (the first OnRep may come before the guid maps)
    ULONGLONG first;
    uint32_t hidden_ticks;
    bool hid;                // this code hid the mesh (a mesh the game had hidden already is left as it is)
};
struct PoseArrivals {
    PoseArrival a[kArrivals];
    int32_t n;
    uint32_t seen, posed, death_plays, gone, full; // per map
};
PoseArrivals g_arrive{}; // game thread

struct PoseJournal {
    PoseRow row[kPoseRows];
    int32_t n;
    ULONGLONG next_step;
    uint32_t hidden_lines;
    uint32_t tracked, dropped, no_guid, snaps, resnaps, unsettled, removed, frames, bytes, too_many; // host
    uint32_t received, applied, pending_give_up, bad, unmapped, removed_rx, checks, mismatches;    // client
    uint32_t started, started_failed, gave_up_round;                                              // client
    uint32_t freezes, unfreezes;          // host: frozen / unfrozen snaps sent at the game's call; client: applied
    uint32_t own_freezes, frozen_sim;     // client: the game's own freeze calls on a copy; frozen rows found simulating
    double freeze_err_max;                // client: worst body error one tick after a freeze (card 140 run 13)
    uint32_t game_blocked;                // client: the game's own ragdoll starts blocked on a host-posed copy
    uint32_t freeze_bad;                  // client: freeze checks over kPoseFreezeTolerance
    uint32_t unfreeze_still;              // client: unfreeze rows applied whose copy did not simulate after the apply
    uint32_t owed_guid, owed_sent;        // host: guid-0 rows that got a guid; owed snaps sent then
    uint32_t sim_unchanged;               // host: SetAllBodiesSimulatePhysics(1) on a simulating sent row, skipped (card 339)
    double err_max, after_max;                                                                   // client
    uint32_t lines, warn_lines, block_lines;
    uint32_t sim_skipped;                 // client: digest rows left out, simulating and not streamed (card 152)
    // Card 152, host: stream samples and their bytes; steps that hit the cap and the samples that waited a step;
    // samples skipped over the backlog; peaks of rows moving at once, samples in one step and the pace queue depth.
    uint32_t stream_lines, st_samples, st_bytes, st_cap_hits, st_deferred, st_backlog_skips, st_backlog_peak;
    int32_t st_moving_peak, st_step_peak;
    // Card 521 step 2. Host: samples sent as deltas and whole, with their wire bytes (before base64). Client: deltas
    // applied, deltas refused for a missing base (a gap in the sample numbers or a body count change), miss rows.
    uint32_t c521_deltas, c521_wholes, c521_delta_bytes, c521_whole_bytes, c521_rx_deltas, c521_misses, c521_lines;
    // Card 521 diag, host: over every sample that could go as a delta, the zigzag varint length of each field's
    // change (position fields [0], rotation fields [1]; 1, 2, 3 bytes), bodies whose quaternion flipped sign against
    // the base (every rotation field then moves by about twice its value), and samples sent whole because the delta
    // was not shorter.
    uint32_t c521_vlen[2][3], c521_flips, c521_body_n, c521_big;
    // Card 521 step 4 diag: rotation field deltas (in kPoseRotStep units) by size bucket, split by whether the body's
    // position moved more than 1 unit in the same delta (0 at rest, 1 moving), and the 2 B or larger ones by field.
    uint32_t c521_rot_hist[2][6], c521_rot_big_field[4];
    // Card 523, client: own-fall starts, hand-overs at sample 1 (and those that fell back to a teleport), the body 0
    // distance from the own fall to sample 1 at the hand-over (sum, worst), and chained samples that came more than
    // kPoseLateMs after the one before (the copy held still at the last sample meanwhile), with the longest gap.
    uint32_t c523_falls, c523_handovers, c523_teleports, c523_late, c523_gap_max;
    double c523_hand_sum, c523_hand_max;
    uint32_t c524_rows; // card 524: per-guid own-fall start offset rows written this map (capped)
    uint32_t c524_hand_rows; // card 524: per-guid hand-over rows written this map (capped)
    uint32_t c523_unfreeze_kept; // card 523 r3: unfreeze snaps that came during an own fall (the fall kept)
    double c523_off_sum, c523_off_max; // card 523 r2: the copy's body 0 distance from sample 0 at the own-fall start
    // Card 152, client: samples received; copies held; own ragdolls stopped for a hold; finals posed held; the game's
    // own SetAllBodiesSimulatePhysics(1) undone on a held copy; game starts blocked waiting for a row; bone checks.
    uint32_t st_rx, st_holds, st_stopped, st_finals, st_reholds, st_wait_blocks, st_bone_checks;
    int32_t bone_rows;
    uint32_t bone_again_lines; // client (card 186): frozen-final bone checks one tick later
    uint32_t c191_lines;       // client (card 191): end tick rows this map
    bool c191_any;             // client (card 191): a row may have c191_mesh set (cleared by a scan that finds none)
    double st_bone_max;
    // Card 152 r2, client: gravity turned off and back on (on with the copy gone counted apart); copies that would
    // not simulate at their stream start; unfreeze rows that continued a stream; body 0 against its last target.
    uint32_t st_frozen_reholds, st_grav_off, st_grav_on, st_grav_gone, st_no_sim, st_unfreezes, st_target_warns;
    double st_target_max;
    // Card 152 r3: client, the game's own SetAllBodiesSimulatePhysics(0) blocked on a streaming copy (and its lines);
    // host, rows left out of the round self-check while their stream is live, and stream finals sent (one per PC).
    uint32_t st_freeze_blocked, st_freeze_block_lines, st_check_skips, st_finals_sent;
    uint32_t settle_lines; // host (card 203): round self-check lines for a corpse moved over kPoseMoved, under tolerance
    uint32_t c269_lines;   // card 269 B: stream count lines (host at freeze and replay, client at freeze)
    uint32_t c359_host_lines; // card 359 step 4: host bone 0 rows
    uint32_t c359_world_lines; // card 359 b190: client bone 0 world rows at the freeze
    uint32_t c359_fixes, c359_fix_lines; // card 359 b191: client meshes moved onto body 0 at the freeze, and rows
    uint32_t c359_scaled, c359_scale_lines; // card 359 step 5, client: copies' mesh scale writes, and their rows
    uint32_t c291_lines;   // card 291 step 1b: host ragdoll start diagnostic lines
    uint64_t c291_tick_fname;    // b8: IsComponentTickEnabled, resolved once
    uint64_t c291_visible_fname; // b8: IsVisible
    uint32_t c251_lines;   // card 251: client corpse capsule / mesh / movement rows
    uint32_t c424_reposes; // card 424: held finals posed again because the held tick moved their bodies
    uint32_t c424_lines;   // card 424: repose rows
    uint32_t c485_reposes; // card 485: frozen copies' bodies posed again after the freeze call
    uint32_t c485_lines;   // card 485: repose rows
    uint32_t c485_render_lines; // card 485 b209: first-rendered rows of reposed copies
};

PoseJournal g_pose{}; // game thread; static: too large for the stack

constexpr uint32_t kC269StreamLines = 80; // card 269 B: per map, both sides
constexpr uint32_t kC291Lines = 60;        // card 291 step 1b: host ragdoll start lines per map

// Card 269 B, host: a row's stream samples sent so far and the last one's age, at the freeze or a replay. Decides
// "the host stopped streaming" against "the samples did not chain" (the client's line at its freeze).
void c269_stream_line(const PoseRow& r, ULONGLONG now, const char* when) {
    if (r.stream_seq == 0 || !note_event_cap(g_pose.c269_lines, kC269StreamLines, "card269 stream")) {
        return;
    }
    log::line("card269: host stream guid 0x%llx samples %u, last at %llu ms before %s",
              static_cast<unsigned long long>(r.guid), r.stream_seq,
              static_cast<unsigned long long>(now - r.stream_at), when);
}

// Card 152, client: a copy whose game ragdoll start was blocked before any stream row came. Found by guid; the
// fallback start after kPoseStartWaitMs resolves the copy again. `started`: the fallback ran; later starts pass.
struct PoseWait {
    uint64_t guid;
    ULONGLONG at;
    bool started;
    bool no_row; // card 291: held with no stream row yet (else a stream row not posed yet)
};
struct PoseWaits {
    PoseWait w[kPoseWaits];
    int32_t n, peak;
    uint32_t added, cleared, fallbacks, unresolved, full;
    ULONGLONG resolved_peak; // the longest wait a stream row ended (card 152 r2)
    bool full_warned;
};
PoseWaits g_wait{}; // game thread
C31Weak g_pose_pc{}; // client: the local PC, read each step (c41_resolve_guid for the wait table)

// Card 291 (b187), client: per guid, the host's death sequence (its `D` row) and the copy's death edge time, so the
// copy plays the host's death once: at the edge when the row came first (in place of the game's own pick), or on the
// row when the edge came first and its own pick differs. The oldest entry is reused when the table is full.
constexpr int32_t kC291Deaths = 64;
constexpr int32_t kC291Waits = 256;        // resolved ragdoll waits kept per map for the median
constexpr uint32_t kC291ClientLines = 80;  // card291 client rows per map
struct C291Death {
    uint64_t guid;
    uint64_t host_name; // 0: no row yet
    ULONGLONG edge_at;  // 0: no death edge yet
    bool sequence;      // the edge plays a death sequence (3D, no bug animator): a host row is expected
};
struct C291Client {
    C291Death d[kC291Deaths];
    int32_t next;
    uint32_t wait_ms[kC291Waits];
    int32_t wait_n;
    uint32_t lines, rows, same, replayed, from_row, unnamed, unresolved;
    // Card 524: death edges whose mesh was put back on its actor, those with a smoothing offset over kC524OffUu, the
    // largest offset, and the per-guid rows written.
    uint32_t c524_resets, c524_over, c524_lines;
    double c524_max;
};
C291Client g_c291{}; // game thread
constexpr double kC524OffUu = 30.0;
constexpr uint32_t kC524Lines = 60;

C291Death* c291_find(uint64_t guid) {
    for (int32_t i = 0; guid != 0 && i < kC291Deaths; ++i) {
        if (g_c291.d[i].guid == guid) {
            return &g_c291.d[i];
        }
    }
    return nullptr;
}

// The entry for `guid`, a fresh one (the oldest reused) when it has none.
C291Death& c291_entry(uint64_t guid) {
    C291Death* const found = c291_find(guid);
    if (found != nullptr) {
        return *found;
    }
    C291Death& d = g_c291.d[g_c291.next];
    g_c291.next = (g_c291.next + 1) % kC291Deaths;
    d = C291Death{guid, 0, 0, false};
    return d;
}

// Card 291: ms from the copy's death edge to now, or -1 when no edge is known for the guid.
long long c291_ms_after_edge(uint64_t guid, ULONGLONG now) {
    const C291Death* const d = c291_find(guid);
    return d != nullptr && d->edge_at != 0 ? static_cast<long long>(now - d->edge_at) : -1;
}

constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t b64_encode(const uint8_t* in, size_t n, char* out) {
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        const uint32_t v = static_cast<uint32_t>(in[i]) << 16 | (i + 1 < n ? static_cast<uint32_t>(in[i + 1]) << 8 : 0) |
                           (i + 2 < n ? in[i + 2] : 0);
        out[o++] = kB64[v >> 18 & 63];
        out[o++] = kB64[v >> 12 & 63];
        out[o++] = i + 1 < n ? kB64[v >> 6 & 63] : '=';
        out[o++] = i + 2 < n ? kB64[v & 63] : '=';
    }
    out[o] = '\0';
    return o;
}

int32_t b64_value(char c) {
    const char* const p = c != '\0' ? strchr(kB64, c) : nullptr;
    return p != nullptr ? static_cast<int32_t>(p - kB64) : -1;
}

// Decoded byte count, or 0 when the text is not base64 or does not fit cap.
size_t b64_decode(const char* in, size_t n, uint8_t* out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; i + 4 <= n; i += 4) {
        const int32_t a = b64_value(in[i]), b = b64_value(in[i + 1]);
        const int32_t c = in[i + 2] == '=' ? 0 : b64_value(in[i + 2]);
        const int32_t d = in[i + 3] == '=' ? 0 : b64_value(in[i + 3]);
        const size_t take = in[i + 2] == '=' ? 1 : in[i + 3] == '=' ? 2 : 3;
        if (a < 0 || b < 0 || c < 0 || d < 0 || o + take > cap) {
            return 0;
        }
        const uint32_t v = static_cast<uint32_t>(a) << 18 | static_cast<uint32_t>(b) << 12 |
                           static_cast<uint32_t>(c) << 6 | static_cast<uint32_t>(d);
        const uint8_t bytes[3] = {static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v)};
        memcpy(out + o, bytes, take);
        o += take;
    }
    return n % 4 == 0 ? o : 0;
}

int16_t pose_i16(double v) {
    const double r = v < -32767.0 ? -32767.0 : v > 32767.0 ? 32767.0 : v;
    return static_cast<int16_t>(lround(r));
}

// Card 521: the wire form of a blob (the row keeps the 14 B body form above; only frames use this one). The head goes
// as it is. Each body keeps its 3 int16 positions and sends its rotation as the three components other than the
// largest ("smallest three"), times kPoseWireScale: a unit quaternion's other components lie within 1 / sqrt(2). The
// largest is made positive (q and -q are one rotation; the stream's blend takes the shorter arc) and rebuilt from the
// other three. The dropped component's index follows the bodies, 2 bits per body, 4 bodies per byte. A gone body
// (pose_encode's all-zero marker) sends INT16_MIN as its first component, a value pose_i16 never makes.
constexpr size_t kPoseWireBodyBytes = 12;
constexpr double kPoseWireScale = 32767.0 * 1.4142135623730951;

size_t pose_wire_bytes(int32_t bodies) {
    const size_t n = static_cast<size_t>(bodies);
    return kPoseHeadBytes + n * kPoseWireBodyBytes + (n + 3) / 4;
}

// Host: blob (bodies in the 14 B form) to its wire form; returns the wire length.
size_t pose_wire_pack(const uint8_t* blob, int32_t bodies, uint8_t* out) {
    memcpy(out, blob, kPoseHeadBytes);
    uint8_t* const index = out + kPoseHeadBytes + static_cast<size_t>(bodies) * kPoseWireBodyBytes;
    memset(index, 0, (static_cast<size_t>(bodies) + 3) / 4);
    for (int32_t i = 0; i < bodies; ++i) {
        int16_t v[7];
        memcpy(v, blob + kPoseHeadBytes + static_cast<size_t>(i) * kPoseBodyBytes, sizeof(v));
        int16_t w[6] = {v[0], v[1], v[2], INT16_MIN, 0, 0};
        int32_t big = 0;
        double len = 0.0;
        for (int32_t k = 0; k < 4; ++k) {
            big = abs(v[3 + k]) > abs(v[3 + big]) ? k : big;
            len += static_cast<double>(v[3 + k]) * v[3 + k];
        }
        if (len > 0.0) {
            const double s = (v[3 + big] < 0 ? -1.0 : 1.0) / sqrt(len) * kPoseWireScale;
            for (int32_t k = 0, j = 3; k < 4; ++k) {
                w[j] = k == big ? w[j] : pose_i16(v[3 + k] * s);
                j += k == big ? 0 : 1;
            }
            index[i / 4] = static_cast<uint8_t>(index[i / 4] | big << (i % 4 * 2));
        }
        memcpy(out + kPoseHeadBytes + static_cast<size_t>(i) * kPoseWireBodyBytes, w, sizeof(w));
    }
    return pose_wire_bytes(bodies);
}

// Client: a wire form back to the 14 B body form; false when the length does not match the body count.
bool pose_wire_unpack(const uint8_t* in, size_t n, int32_t bodies, uint8_t* blob, size_t& blob_n) {
    if (bodies < 1 || bodies > kPoseBodies || n != pose_wire_bytes(bodies)) {
        return false;
    }
    memcpy(blob, in, kPoseHeadBytes);
    const uint8_t* const index = in + kPoseHeadBytes + static_cast<size_t>(bodies) * kPoseWireBodyBytes;
    for (int32_t i = 0; i < bodies; ++i) {
        int16_t w[6];
        memcpy(w, in + kPoseHeadBytes + static_cast<size_t>(i) * kPoseWireBodyBytes, sizeof(w));
        int16_t v[7] = {w[0], w[1], w[2], 0, 0, 0, 0};
        if (w[3] != INT16_MIN) {
            const int32_t big = index[i / 4] >> (i % 4 * 2) & 3;
            double c[3], sum = 0.0;
            for (int32_t j = 0; j < 3; ++j) {
                c[j] = w[3 + j] / kPoseWireScale;
                sum += c[j] * c[j];
            }
            const double large = sqrt(fmax(0.0, 1.0 - sum));
            for (int32_t k = 0, j = 0; k < 4; ++k) {
                v[3 + k] = pose_i16((k == big ? large : c[j]) * 32767.0);
                j += k == big ? 0 : 1;
            }
        }
        memcpy(blob + kPoseHeadBytes + static_cast<size_t>(i) * kPoseBodyBytes, v, sizeof(v));
    }
    blob_n = kPoseHeadBytes + static_cast<size_t>(bodies) * kPoseBodyBytes;
    return true;
}

// Card 521 step 2, the delta wire form of a stream sample: the head raw, then every int16 field of every body (14 B
// form, 7 per body) as the zigzag varint of (cur - base), 7 bits per byte, low first. |cur - base| <= 65535, so a
// field takes at most 3 bytes. A gone body is all zero in both forms and needs no marker.
// Card 521 step 3 (c263l: 72 percent of rotation fields took 2 B): the 4 rotation fields of a body go in units of
// kPoseRotStep, rounded, so the client's result lies within kPoseRotStep / 2 of the host's sample (about 0.014
// degrees). Before that, when the sample's quaternion points away from the base's (dot < 0), the sample's rotation
// is negated (q and -q are one rotation; pose_stream_tick blends on the shorter arc), so a sign flip costs no bytes.
// The host keeps as its next base the result the client decodes (pose_delta_unpack of the packed delta), so the
// rounding never builds up over a chain.
constexpr int32_t kPoseVarintMax = 3;
constexpr int32_t kPoseRotStep = 8;

// Host: true when cur's body quaternion points away from base's (dot < 0); the delta then sends cur's negation.
bool pose_delta_flip(const uint8_t* base, const uint8_t* cur, int32_t body) {
    int16_t b[7], c[7];
    memcpy(b, base + kPoseHeadBytes + static_cast<size_t>(body) * kPoseBodyBytes, sizeof(b));
    memcpy(c, cur + kPoseHeadBytes + static_cast<size_t>(body) * kPoseBodyBytes, sizeof(c));
    int64_t dot = 0;
    for (int32_t k = 3; k < 7; ++k) {
        dot += static_cast<int64_t>(b[k]) * c[k];
    }
    return dot < 0;
}

// Host: the zigzag value of field f (0 to 6) of a body, base b to cur c. Positions (f < 3) go in steps of 1.
// Rotations go in kPoseRotStep units, rounded half away from zero, from the negated c when flip; a result outside
// +-32767 moves one unit back toward the base (the unpack refuses anything outside int16). Inputs are within
// +-32767 (pose_i16 and this clamp), so -c fits.
uint32_t pose_delta_z(int16_t b, int16_t c, int32_t f, bool flip) {
    int32_t d = static_cast<int32_t>(c) - b;
    if (f >= 3) {
        const int32_t diff = (flip ? -static_cast<int32_t>(c) : static_cast<int32_t>(c)) - b;
        d = (diff >= 0 ? diff + kPoseRotStep / 2 : diff - kPoseRotStep / 2) / kPoseRotStep;
        const int32_t v = b + d * kPoseRotStep;
        d += v > 32767 ? -1 : (v < -32767 ? 1 : 0);
    }
    return d < 0 ? (static_cast<uint32_t>(-d) << 1) - 1 : static_cast<uint32_t>(d) << 1;
}

// Host: base and cur hold `bodies` bodies each. Returns the delta wire length, or 0 when it would reach `cap` (the
// whole wire length; the sample then goes whole).
size_t pose_delta_pack(const uint8_t* base, const uint8_t* cur, int32_t bodies, uint8_t* out, size_t cap) {
    memcpy(out, cur, kPoseHeadBytes);
    size_t at = kPoseHeadBytes;
    const int32_t fields = bodies * 7;
    bool flip = false;
    for (int32_t i = 0; i < fields && at < cap; ++i) {
        flip = i % 7 == 0 ? pose_delta_flip(base, cur, i / 7) : flip;
        int16_t b = 0, c = 0;
        memcpy(&b, base + kPoseHeadBytes + static_cast<size_t>(i) * 2, 2);
        memcpy(&c, cur + kPoseHeadBytes + static_cast<size_t>(i) * 2, 2);
        uint32_t z = pose_delta_z(b, c, i % 7, flip);
        for (int32_t k = 0; k < kPoseVarintMax && at < cap; ++k) {
            out[at++] = static_cast<uint8_t>((z & 0x7f) | (z > 0x7f ? 0x80 : 0));
            z >>= 7;
            k = z == 0 ? kPoseVarintMax : k;
        }
    }
    return at < cap ? at : 0;
}

// Host (card 521 diag): counts where a delta's bytes go, by field kind and varint length (the same values
// pose_delta_pack sends), and the bodies whose rotation was negated (dot < 0). Reads only; base and cur hold `bodies`
// bodies in the 14 B form.
void pose_delta_stats(const uint8_t* base, const uint8_t* cur, int32_t bodies) {
    for (int32_t b = 0; b < bodies; ++b) {
        const uint8_t* const pb = base + kPoseHeadBytes + static_cast<size_t>(b) * kPoseBodyBytes;
        const uint8_t* const pc = cur + kPoseHeadBytes + static_cast<size_t>(b) * kPoseBodyBytes;
        const bool flip = pose_delta_flip(base, cur, b);
        bool moving = false;
        for (int32_t f = 0; f < 7; ++f) {
            int16_t x = 0, y = 0;
            memcpy(&x, pb + f * 2, 2);
            memcpy(&y, pc + f * 2, 2);
            const uint32_t z = pose_delta_z(x, y, f, flip);
            ++g_pose.c521_vlen[f < 3 ? 0 : 1][z < 0x80 ? 0 : (z < 0x4000 ? 1 : 2)];
            moving = moving || (f < 3 && z > 2); // zigzag 3 and up: more than 1 unit either way
            if (f >= 3) {
                const uint32_t mag = (z + 1) >> 1; // |d| in kPoseRotStep units
                const int32_t k = mag < 64 ? 0 : (mag < 128 ? 1 : (mag < 256 ? 2 : (mag < 512 ? 3 : (mag < 1024 ? 4 : 5))));
                ++g_pose.c521_rot_hist[moving ? 1 : 0][k];
                g_pose.c521_rot_big_field[f - 3] += k > 0 ? 1u : 0u;
            }
        }
        g_pose.c521_flips += flip ? 1u : 0u;
    }
    g_pose.c521_body_n += static_cast<uint32_t>(bodies);
}

// Client: a delta wire form back to the 14 B form over `base` (`bodies` bodies). False on a truncated or long input,
// a varint over kPoseVarintMax bytes, or a field outside int16.
bool pose_delta_unpack(const uint8_t* base, const uint8_t* in, size_t n, int32_t bodies, uint8_t* blob, size_t& blob_n) {
    if (bodies < 1 || bodies > kPoseBodies || n < kPoseHeadBytes) {
        return false;
    }
    memcpy(blob, in, kPoseHeadBytes);
    size_t at = kPoseHeadBytes;
    const int32_t fields = bodies * 7;
    for (int32_t i = 0; i < fields; ++i) {
        uint32_t z = 0;
        bool more = true;
        for (int32_t k = 0; k < kPoseVarintMax && more; ++k) {
            if (at >= n) {
                return false;
            }
            z |= static_cast<uint32_t>(in[at] & 0x7f) << (7 * k);
            more = (in[at++] & 0x80) != 0;
        }
        int16_t b = 0;
        memcpy(&b, base + kPoseHeadBytes + static_cast<size_t>(i) * 2, 2);
        const int32_t d = (z & 1) != 0 ? -static_cast<int32_t>((z + 1) >> 1) : static_cast<int32_t>(z >> 1);
        const int32_t v = b + d * (i % 7 >= 3 ? kPoseRotStep : 1); // card 521 step 3: rotations in kPoseRotStep units
        if (more || v < INT16_MIN || v > INT16_MAX) {
            return false;
        }
        const int16_t c = static_cast<int16_t>(v);
        memcpy(blob + kPoseHeadBytes + static_cast<size_t>(i) * 2, &c, 2);
    }
    blob_n = kPoseHeadBytes + static_cast<size_t>(bodies) * kPoseBodyBytes;
    return at == n;
}

// The mesh's body array and count; count 0 when unreadable or out of range.
void* const* pose_bodies(const void* mesh, int32_t& count) {
    void* const* const bodies = static_cast<void* const*>(read_ptr(mesh, kSkeletalBodies));
    count = read_i32(mesh, kSkeletalBodies + 0x8);
    if (bodies == nullptr || count <= 0 || count > kPoseBodies) {
        count = 0;
    }
    return bodies;
}

bool pose_world(const void* body, Xform& x) {
    if (body == nullptr || g_pose_body_world == nullptr) {
        return false;
    }
    g_pose_body_world(body, &x, false, true);
    // A body with no physics actor returns the identity transform: the gore system broke that limb off (hb37 guid
    // 0x358 body 6, gibbed by a host shot after its settled snap). No live body sits exactly at the world origin.
    return x.t[0] != 0.0 || x.t[1] != 0.0 || x.t[2] != 0.0;
}

bool pose_fns() {
    return g_pose_body_world != nullptr && g_pose_body_set != nullptr && g_pose_mesh_awake != nullptr &&
           g_pose_mesh_sleep != nullptr && g_pose_mesh_linear != nullptr && g_pose_mesh_angular != nullptr &&
           g_body_simulating != nullptr && g_pose_mesh_gravity != nullptr;
}

void* pose_mesh(const PoseRow& r) {
    void* const actor = c31_weak_get(r.actor);
    return actor != nullptr ? read_ptr(actor, kCharacterMesh) : nullptr;
}

PoseRow* pose_find(uint64_t guid) {
    for (int32_t i = 0; guid != 0 && i < g_pose.n; ++i) {
        if (g_pose.row[i].guid == guid) {
            return &g_pose.row[i];
        }
    }
    return nullptr;
}

// Host: rows are keyed by actor, since a row tracked before any client connected has guid 0 until the corpse
// replicates (hb70: 11 corpses dead before the join, "0 rows (0 tracked)" in the join replay).

PoseWait* wait_find(uint64_t guid) {
    for (int32_t i = 0; guid != 0 && i < g_wait.n; ++i) {
        if (g_wait.w[i].guid == guid) {
            return &g_wait.w[i];
        }
    }
    return nullptr;
}

// Client (card 152): a copy's wait ends, its stream row posed it (or the host removed the row).
// Card 291: `by` names the event that ended the wait (the snap apply, the stream start, or the host's release row);
// `row_at` is when that row's newest sample arrived (0: no row).
void wait_clear(uint64_t guid, const char* by, ULONGLONG row_at) {
    PoseWait* const w = wait_find(guid);
    if (w == nullptr) {
        return;
    }
    if (!w->started) {
        const ULONGLONG ms = GetTickCount64() - w->at;
        ++g_wait.cleared;
        g_wait.resolved_peak = ms > g_wait.resolved_peak ? ms : g_wait.resolved_peak;
        if (g_c291.wait_n < kC291Waits) { // card 291: for the map median
            g_c291.wait_ms[g_c291.wait_n++] = static_cast<uint32_t>(ms);
        }
        if (note_event_cap(g_pose.stream_lines, kPoseStreamLines, "card152 stream")) {
            log::line("card152: client ragdoll wait for guid 0x%llx resolved after %llu ms",
                      static_cast<unsigned long long>(guid), static_cast<unsigned long long>(ms));
            const ULONGLONG now = GetTickCount64();
            log::line("card291: client ragdoll wait on guid 0x%llx sat on %s; ended by %s after %llu ms; death edge "
                      "%lld ms before the hold; newest stream sample %lld ms before the end",
                      static_cast<unsigned long long>(guid),
                      w->no_row ? "the first stream row (none at the hold)" : "the stream row's pose (row not posed)",
                      by, static_cast<unsigned long long>(ms), c291_ms_after_edge(guid, w->at),
                      row_at != 0 ? static_cast<long long>(now - row_at) : -1LL);
        }
    }
    *w = g_wait.w[--g_wait.n];
}

// Client (card 152): true when the copy's game ragdoll start is to wait for its stream row. An entry whose fallback
// already ran lets every later start through; a full table lets the start through with one WARN per map.
bool wait_add(uint64_t guid, ULONGLONG now, bool no_row) {
    PoseWait* const w = wait_find(guid);
    if (w != nullptr) {
        return !w->started;
    }
    if (g_wait.n >= kPoseWaits) {
        ++g_wait.full;
        if (!g_wait.full_warned) {
            g_wait.full_warned = true;
            log::line("WARN card152: client ragdoll wait table full (%d): guid 0x%llx starts its own ragdoll",
                      kPoseWaits, static_cast<unsigned long long>(guid));
        }
        return false;
    }
    g_wait.w[g_wait.n++] = PoseWait{guid, now, false, no_row};
    ++g_wait.added;
    g_wait.peak = g_wait.n > g_wait.peak ? g_wait.n : g_wait.peak;
    return true;
}

// Client (card 152, from c132_pose_game_ragdoll): a copy not posed yet. A streamed row (not posed yet) or no row at
// all for a copy whose death edge ran holds the start until the stream row poses it (pose_stream_apply) or
// kPoseStartWaitMs passes (pose_wait_tick). A row from a snap, not a stream, lets the start through as before.
bool pose_wait_gate(const PoseRow* r, uint64_t guid, uintptr_t caller_rva) {
    if (guid == 0 || (r != nullptr && !r->streamed)) {
        return false;
    }
    if (r == nullptr) {
        void* const pc = c31_weak_get(g_pose_pc);
        void* const npc = pc != nullptr ? c41_resolve_guid(pc, guid) : nullptr;
        if (npc == nullptr || !c38_is_marked(npc)) {
            return false;
        }
    }
    if (!wait_add(guid, GetTickCount64(), r == nullptr)) {
        return false;
    }
    ++g_pose.st_wait_blocks;
    if (note_event_cap(g_pose.stream_lines, kPoseStreamLines, "card152 stream")) {
        char bp[256]; // card 291: the Blueprint caller chain, as the host's ragdoll start row prints it
        c132_bp_text(bp, sizeof(bp));
        log::line("card152: client held the game's ragdoll start on guid 0x%llx (%s), caller exe+0x%llX; %d waiting; "
                  "card291: %lld ms after the death edge, bp %s",
                  static_cast<unsigned long long>(guid), r != nullptr ? "stream row not posed yet" : "no stream row yet",
                  static_cast<unsigned long long>(caller_rva), g_wait.n, c291_ms_after_edge(guid, GetTickCount64()),
                  bp);
    }
    return true;
}

} // namespace

namespace {
struct RestRow;                                    // card 269 (defined below the sprite rows)
RestRow* rest_find(uint64_t guid, const void* actor); // card 207: used by c132_pose_game_ragdoll
} // namespace

// Client (card 146, from detour_c132_set_simulate): the game is starting a ragdoll on a copy. A copy that holds an
// applied host pose (Applied or Freezing) is the host's corpse; the client's own start is blocked, as card 136 blocks
// client-own effects, so the copy's simulation follows only the host's rows (c140n: 20 such starts, all from the
// BP_AI_AttackSystem "ragdoll left" path at exe+0x37C53CB, within about 120 ms of the pose's own start). True when
// the caller must not start the ragdoll. Each block is logged per guid with its running count. Card 152: a copy not
// posed yet waits for its stream row (pose_wait_gate). Card 207: a copy with no pose row and a host rest row is a
// corpse the host never ragdolled (card 269 ruling: the rest row owns it); its start is blocked, not held, or the
// card 152 wait would end in the copy's own ragdoll over the rest pose (run c207b155a, 3 corpses).
bool c132_pose_game_ragdoll(uint64_t guid, uintptr_t caller_rva) {
    PoseRow* const r = guid != 0 ? pose_find(guid) : nullptr;
    if (r == nullptr && guid != 0 && rest_find(guid, nullptr) != nullptr) {
        ++g_pose.game_blocked;
        if (note_event_cap(g_pose.block_lines, kPoseBlockLines, "card146 pose block")) {
            log::line("card207: client blocked the game's ragdoll start on rest-row guid 0x%llx, caller exe+0x%llX",
                      static_cast<unsigned long long>(guid), static_cast<unsigned long long>(caller_rva));
        }
        return true;
    }
    if (r == nullptr || (r->state != PoseState::Applied && r->state != PoseState::Freezing)) {
        return pose_wait_gate(r, guid, caller_rva);
    }
    ++g_pose.game_blocked;
    ++r->game_blocked;
    if (note_event_cap(g_pose.block_lines, kPoseBlockLines, "card146 pose block")) {
        log::line("card146: client blocked the game's ragdoll start on host-posed guid 0x%llx (snap %u, %s), caller "
                  "exe+0x%llX; %u blocked for this guid", static_cast<unsigned long long>(guid), r->snap,
                  (r->flags & kPoseFrozen) != 0 ? "frozen" : "simulating", static_cast<unsigned long long>(caller_rva),
                  r->game_blocked);
    }
    return true;
}

namespace {

PoseRow* pose_find_actor(const void* npc) {
    for (int32_t i = 0; npc != nullptr && i < g_pose.n; ++i) {
        if (c31_weak_get(g_pose.row[i].actor) == npc) {
            return &g_pose.row[i];
        }
    }
    return nullptr;
}

// Card 152 r2, client: the one place a streamed copy's gravity goes off, and the one place it comes back (the final
// row, the wait fallback, a dropped row, a new copy of the guid). Both are counted for the map line.
void pose_grav_off(PoseRow& r, void* mesh) {
    g_pose_mesh_gravity(mesh, false);
    r.grav_off = true;
    ++g_pose.st_grav_off;
}

void pose_grav_on(PoseRow& r, void* mesh) {
    if (!r.grav_off) {
        return;
    }
    r.grav_off = false;
    if (mesh == nullptr) {
        ++g_pose.st_grav_gone; // the copy is gone: nothing to restore
        return;
    }
    g_pose_mesh_gravity(mesh, true);
    ++g_pose.st_grav_on;
}

void pose_remove(int32_t i) {
    pose_grav_on(g_pose.row[i], pose_mesh(g_pose.row[i])); // card 152 r2: a dropped streamed row
    g_pose.row[i] = g_pose.row[g_pose.n - 1];
    --g_pose.n;
}

// Host: encode every body's world transform. A gibbed body (no physics actor) encodes as the root with an all-zero
// rotation, the gone marker (a real rotation has norm 1). False when the root is gone or a body sits too far from it.
uint16_t pose_scale_u16(double s) {
    const double v = s * kPoseScaleSteps;
    return v < 1.0 ? 0 : v > 65535.0 ? 65535 : static_cast<uint16_t>(lround(v)); // 0: unread
}

bool pose_encode_to(const void* mesh, void* const* bodies, int32_t count, uint8_t* blob, size_t& blob_n,
                    Vec3& root_at) {
    Xform root{};
    if (!pose_world(bodies[0], root)) {
        return false;
    }
    const float head[3] = {static_cast<float>(root.t[0]), static_cast<float>(root.t[1]), static_cast<float>(root.t[2])};
    memcpy(blob, head, sizeof(head));
    const Vec3 sc = mesh != nullptr ? read_vec3(mesh, kComponentScale) : Vec3{};
    const uint16_t scale[3] = {pose_scale_u16(sc.x), pose_scale_u16(sc.y), pose_scale_u16(sc.z)};
    memcpy(blob + kPoseRootBytes, scale, sizeof(scale));
    for (int32_t i = 0; i < count; ++i) {
        Xform x{};
        if (!pose_world(bodies[i], x)) {
            x.t[0] = head[0];
            x.t[1] = head[1];
            x.t[2] = head[2];
            x.q[0] = x.q[1] = x.q[2] = x.q[3] = 0.0;
        }
        int16_t v[7];
        for (int32_t k = 0; k < 3; ++k) {
            const double rel = x.t[k] - head[k];
            if (fabs(rel) > kPoseRelMax) {
                return false;
            }
            v[k] = pose_i16(rel / kPoseUnit);
        }
        for (int32_t k = 0; k < 4; ++k) {
            v[3 + k] = pose_i16(x.q[k] * 32767.0);
        }
        memcpy(blob + kPoseHeadBytes + static_cast<size_t>(i) * kPoseBodyBytes, v, sizeof(v));
    }
    blob_n = kPoseHeadBytes + static_cast<size_t>(count) * kPoseBodyBytes;
    root_at = {root.t[0], root.t[1], root.t[2]};
    return true;
}

// Host: a snap into the row's blob (the end state the join replay and the self-check read).
bool pose_encode(const void* mesh, void* const* bodies, int32_t count, PoseRow& r) {
    if (!pose_encode_to(mesh, bodies, count, r.blob, r.blob_n, r.sent_root)) {
        return false;
    }
    r.bodies = count;
    return true;
}

struct PoseSend {
    int32_t frames, bytes, sent;
};

struct PoseHead { // a frame's head fields: guid, snap (card 152: the sample number for a stream frame), bodies, flags
    uint64_t guid;
    uint32_t snap;
    int32_t bodies;
    uint32_t flags;
};

// A wire form's frames (card 521: whole or delta), to one PC (pc set) or to every ponged PC (world set).
PoseSend pose_send_wire(const PoseHead& h, const uint8_t* wire, size_t wire_n, void* pc, const void* world) {
    static char b64[kPoseB64 + 1]; // game thread
    const size_t n = b64_encode(wire, wire_n, b64);
    const int32_t parts = static_cast<int32_t>((n + kPoseChunk - 1) / kPoseChunk);
    PoseSend s{};
    for (int32_t p = 0; p < parts; ++p) {
        char text[kChanTextMax + 1];
        const size_t at = static_cast<size_t>(p) * kPoseChunk;
        const size_t len = n - at < kPoseChunk ? n - at : kPoseChunk;
        snprintf(text, sizeof(text), "P %llx %x %d %d %d %u %.*s", static_cast<unsigned long long>(h.guid), h.snap, p,
                 parts, h.bodies, h.flags, static_cast<int>(len), b64 + at);
        const int32_t sent = pc != nullptr ? (c95_send_one(pc, ChanOp::Pose, text) ? 1 : 0)
                                           : c95_send_all(world, ChanOp::Pose, text).sent;
        s.frames += 1;
        s.bytes += static_cast<int32_t>(strlen(text));
        s.sent += sent;
    }
    return s;
}

// One blob's frames (14 B body form, sent in its whole wire form), to one PC or to every ponged PC.
PoseSend pose_send_blob(const PoseHead& h, const uint8_t* blob, size_t blob_n, void* pc, const void* world) {
    static uint8_t wire[kPoseBlob]; // game thread
    const size_t want = kPoseHeadBytes + static_cast<size_t>(h.bodies) * kPoseBodyBytes;
    if (h.bodies < 1 || h.bodies > kPoseBodies || blob_n != want) {
        log::line("card521: host pose send refused: %d bodies, blob %zu bytes", h.bodies, blob_n);
        return PoseSend{};
    }
    return pose_send_wire(h, wire, pose_wire_pack(blob, h.bodies, wire), pc, world);
}

// The row's frames (its last snap).
PoseSend pose_send(const PoseRow& r, void* pc, const void* world) {
    return pose_send_blob(PoseHead{r.guid, r.snap, r.bodies, r.flags}, r.blob, r.blob_n, pc, world);
}

double c191_body0_dist(const PoseRow& r, void* mesh);
bool c191_bone0_world(const PoseRow& r, void* mesh, Vec3& out);
double c359_mesh_to_body0(const PoseRow& r, void* mesh, double off); // card 359 b191, below

// Card 359 step 4 (diagnostic), host, at a settled or frozen final: bone 0 to body 0 on the host's own mesh, the
// distance the client's freeze reads as its error (b129: 91 client WARN rows, constant per guid, bodies exact, blend
// weight 1). The same distance here for the same guid means the offset belongs to the corpse (the blend keeps it on
// the host too) and the client's check should compare with this value, not with 0.
void c359_host_bone(const PoseRow& r, void* mesh, void* const* bodies) {
    constexpr uint32_t kC359HostLines = 200; // per map
    if ((r.flags & (kPoseUnsettled | kPoseUnfreeze | kPoseStream)) != 0 ||
        !note_event_cap(g_pose.c359_host_lines, kC359HostLines, "card359 host bone")) {
        return;
    }
    Xform body{};
    const bool ok = pose_world(bodies[0], body);
    const Vec3 at = read_vec3(mesh, kComponentTranslation);
    const Vec3 sc = read_vec3(mesh, kComponentScale);
    Vec3 w{}; // b190: world bone 0, joined offline by guid with the client's "bone 0 world" row at its freeze
    const bool wok = c191_bone0_world(r, mesh, w);
    log::line("card359: host guid 0x%llx snap %u %s final: bone 0 %.1f uu off body 0 (-1 = unread); mesh component "
              "%.0f uu from body 0, component scale (%.3f, %.3f, %.3f); bone 0 world %s(%.1f, %.1f, %.1f)",
              static_cast<unsigned long long>(r.guid), r.snap, (r.flags & kPoseFrozen) != 0 ? "frozen" : "settled",
              c191_body0_dist(r, mesh), ok ? vec_dist(at, Vec3{body.t[0], body.t[1], body.t[2]}) : -1.0, sc.x, sc.y,
              sc.z, wok ? "" : "unread ", w.x, w.y, w.z);
}

// Host: snapshot and send one settled (or forced) corpse.
void pose_snapshot(const void* world, PoseRow& r, void* const* bodies, int32_t count, ULONGLONG now) {
    const bool resnap = r.snap != 0;
    void* const mesh = pose_mesh(r);
    const bool frozen = !c39_mesh_simulating(mesh);
    r.flags = frozen ? kPoseFrozen : (r.still_ms >= kPoseStillMs || !g_pose_mesh_awake(mesh) ? 0 : kPoseUnsettled);
    r.flags |= r.unfreeze_next && !frozen ? kPoseUnfreeze : 0;
    r.unfreeze_next = false;
    if (!pose_encode(mesh, bodies, count, r)) {
        ++g_pose.too_many;
        r.start = now; // try again after another kPoseForceMs
        return;
    }
    r.woke = false; // card 339: this snap carries every body's pose
    ++r.snap;
    c359_host_bone(r, mesh, bodies);
    r.state = PoseState::Sent;
    r.owed = r.guid == 0; // no client has the corpse yet: pose_guid_check sends it once the corpse replicates
    const PoseSend s = r.owed ? PoseSend{} : pose_send(r, nullptr, world);
    // Card 152 r3: a snap after the row's first stream sample that is not an unfreeze is the client's stream final
    // (pose_try_apply, r.streamed): counted once per PC it went to (s.sent counts each part per PC).
    const bool st_final = r.stream_seq != 0 && (r.flags & kPoseUnfreeze) == 0 && s.frames != 0;
    g_pose.st_finals_sent += st_final ? static_cast<uint32_t>(s.sent / s.frames) : 0u;
    ++g_pose.snaps;
    g_pose.resnaps += resnap ? 1u : 0u;
    g_pose.unsettled += (r.flags & kPoseUnsettled) != 0 ? 1u : 0u;
    g_pose.frames += static_cast<uint32_t>(s.frames);
    g_pose.bytes += static_cast<uint32_t>(s.bytes);
    if (note_event_cap(g_pose.lines, kPoseLines, "card132 pose")) {
        log::line("card132: host pose %s guid 0x%llx snap %u: %d bodies, root (%.0f, %.0f, %.0f), %s after %llu ms; "
                  "%d bytes in %d frames to %d PCs%s", resnap ? "resnap" : "snap",
                  static_cast<unsigned long long>(r.guid), r.snap, count, r.sent_root.x, r.sent_root.y, r.sent_root.z,
                  (r.flags & kPoseFrozen) != 0 ? "FROZEN" : (r.flags & kPoseUnsettled) != 0 ? "UNSETTLED" : "settled",
                  static_cast<unsigned long long>(now - r.start), s.bytes, s.frames, s.sent,
                  r.owed ? " (no NetGUID yet: owed)" : "");
    }
}

// Host: a row made before its corpse had a NetGUID reads it again each step. Once the corpse has replicated, an owed
// snap goes to every ponged PC (a joining PC is ponged before its replay, so the replay and this cover it). True when
// the row now has a guid.
bool pose_guid_check(const void* world, PoseRow& r) {
    if (r.guid != 0) {
        return true;
    }
    void* const npc = c31_weak_get(r.actor);
    r.guid = npc != nullptr ? c41_guid(npc) : 0;
    if (r.guid == 0) {
        return false;
    }
    ++g_pose.owed_guid;
    if (!r.owed) {
        return true; // still moving: its first snap goes out as usual
    }
    r.owed = false;
    const PoseSend s = pose_send(r, nullptr, world);
    ++g_pose.owed_sent;
    if (note_event_cap(g_pose.lines, kPoseLines, "card132 pose")) {
        log::line("card137: host pose owed snap sent guid 0x%llx snap %u (%s): the corpse died before it had a NetGUID; "
                  "%d bytes in %d frames to %d PCs", static_cast<unsigned long long>(r.guid), r.snap,
                  object_name(npc).text, s.bytes, s.frames, s.sent);
    }
    return true;
}

// Host: one line per sample while a ragdoll's mesh is hidden: where body 0 goes and what its collision is.
void pose_hidden_trace(const PoseRow& r, void* mesh, void* const* bodies) {
    if (!note_event_cap(g_pose.hidden_lines, kPoseHiddenLines, "card132 pose hidden")) {
        return;
    }
    Xform x{};
    const double z = pose_world(bodies[0], x) ? x.t[2] : 0.0;
    const unsigned collision = g_collision_enabled != nullptr ? g_collision_enabled(mesh) : 255u;
    log::line("card132: host pose hidden guid 0x%llx: body 0 z %.0f (start %.0f), mesh collision %u, update type %u, "
              "awake %d", static_cast<unsigned long long>(r.guid), z, r.have_start ? r.start_z : 0.0, collision,
              static_cast<const uint8_t*>(mesh)[kKinematicBonesUpdateType], g_pose_mesh_awake(mesh) ? 1 : 0);
}

// Card 189: the floor trace channel is kEccWorldStatic (ue/actor.h, the card 264 trace_channel binding); the trace
// length when the chapter kill z is not known (c46_kill_z 0: no game instance).
constexpr double kC189NoKillZReach = 30000.0;
// Card 525: a floor this far under body 0 is not a landing. Host long-fall rows over all logs to c247b303a: real drops
// up to 25,162 uu, then nothing until 795,207 uu (geometry far under the map, about z -1.2M, hit by corpses that fell
// through the floor). Those kept streaming as long falls at up to about 30,000 uu/s.
constexpr double kC525MaxDropUu = 100000.0;
constexpr double kC189SnapUu = 10.0; // host: body 0 moved more than this one sample after the stop: WARN

void pose_hold(PoseRow& r, void* mesh); // below, with pose_release
void pose_release(PoseRow& r, void* mesh); // below, with pose_hold (card 189: host row removal)
C191Bone c191_bone(const PoseRow& r, void* mesh); // below, with the card 359 snapshot
void c485_render_note(PoseRow& r, ULONGLONG now); // below, after c191_bone

// Host (card 189): body 0's world position now; `fallback` when the mesh has no readable body 0.
Vec3 c189_body0(const void* mesh, const Vec3& fallback) {
    int32_t count = 0;
    void* const* const bodies = pose_bodies(mesh, count);
    Xform x{};
    return count > 0 && pose_world(bodies[0], x) ? Vec3{x.t[0], x.t[1], x.t[2]} : fallback;
}

// Host (card 189): a corpse with no floor under it, or under the kill z, stops simulating where it is, so the sample
// path sends a frozen final (kPoseFrozen) and the clients freeze their copies (pose_freeze_step); a settled final left
// the client's copy simulating with gravity, and a wake sent it down without end (c230-c3m1-d2 guid 0xe40). The
// client's hold bits follow the stop (pose_hold: bBlendPhysics and SkipAllBones), so the anim pose does not drive the
// now kinematic bodies (card 140 run 13 saw that snap on the client). Body 0 is logged before and after the call, and
// one sample later (pose_host_row).
void c189_stop(PoseRow& r, void* mesh, const char* why) {
    r.c189_stopped = true;
    g_orig_c132_all_bodies(mesh, false);
    pose_hold(r, mesh);
    r.c189_at = c189_body0(mesh, r.last[0]);
    r.c189_check = true;
    if (r.state == PoseState::Sent) { // the stop is a change the clients need: the frozen final goes this sample
        r.state = PoseState::Moving;
        r.start = GetTickCount64();
    }
    log::line("card189: host pose guid 0x%llx %s: simulation stopped and held; body 0 (%.0f, %.0f, %.0f) before the "
              "call, (%.0f, %.0f, %.0f) after, simulating %d",
              static_cast<unsigned long long>(r.guid), why, r.last[0].x, r.last[0].y, r.last[0].z, r.c189_at.x,
              r.c189_at.y, r.c189_at.z, c39_mesh_simulating(mesh) ? 1 : 0);
}

// Host (card 189): body 0 one sample (kPoseStepMs) after c189_stop, against where the stop left it.
void c189_check_line(PoseRow& r, void* mesh) {
    r.c189_check = false;
    const Vec3 now_at = c189_body0(mesh, r.last[0]);
    const double d = vec_dist(now_at, r.c189_at);
    log::line("%scard189: host pose guid 0x%llx one sample after the stop: body 0 (%.0f, %.0f, %.0f), %.1f uu from the "
              "stop, simulating %d", d > kC189SnapUu ? "WARN " : "", static_cast<unsigned long long>(r.guid),
              now_at.x, now_at.y, now_at.z, d, c39_mesh_simulating(mesh) ? 1 : 0);
}

// Host: a corpse whose body 0 dropped kPoseFellUu below its start either fell a long way or fell through the world
// (hb41 guid 0x1a4 fell to z -79697, resnapped every kPoseForceMs). Card 189: one trace straight down from body 0 to
// the chapter kill z tells them apart. A floor: a long fall, the corpse keeps streaming to its landing. No floor: the
// simulation stops (c189_stop). Every row: body 0 under the kill z stops too.
void pose_fell_check(PoseRow& r, void* mesh) {
    if (!r.have_start) {
        r.start_z = r.last[0].z;
        r.have_start = true;
        return;
    }
    void* const owner = read_ptr(mesh, kComponentOwner);
    const double kill_z = static_cast<double>(c46_kill_z(owner));
    const bool have_kill = kill_z < 0.0;
    if (have_kill && !r.c189_stopped && r.last[0].z < kill_z) {
        char why[64];
        snprintf(why, sizeof(why), "under the kill z %.0f", kill_z);
        c189_stop(r, mesh, why);
        return;
    }
    if (r.fell || r.c189_stopped || r.last[0].z > r.start_z - kPoseFellUu) {
        return;
    }
    r.fell = true;
    const Vec3 from = r.last[0];
    // c247b309a (FED_C5M2): the kill z there is about -3.4e38 (no kill plane), so the trace ran to it and the row
    // printed that number. A floor past kC525MaxDropUu counts as none anyway, so the trace stops there.
    const Vec3 to{from.x, from.y, have_kill ? fmax(kill_z, from.z - kC525MaxDropUu) : from.z - kC189NoKillZReach};
    const bool bound = owner != nullptr && g_line_trace_by_channel != nullptr && g_query_params_ctor != nullptr;
    const Vec3 floor = bound ? trace_channel(owner, from, to, kEccWorldStatic) : to;
    if (floor.z > to.z + 1.0 && from.z - floor.z <= kC525MaxDropUu) {
        log::line("card189: host pose guid 0x%llx long fall, floor at z %.0f (%.0f uu below); body 0 z %.0f, start %.0f",
                  static_cast<unsigned long long>(r.guid), floor.z, from.z - floor.z, from.z, r.start_z);
        return;
    }
    log::line("card132: host pose guid 0x%llx fell through the world: body 0 z %.0f, start %.0f; no floor down to z "
              "%.0f (%.0f uu)%s", static_cast<unsigned long long>(r.guid), from.z, r.start_z, to.z, from.z - to.z,
              bound ? "" : " (trace not bound)");
    c189_stop(r, mesh, "no floor");
}

void corpse_host_released(uint64_t guid); // card 428 (below the rest rows)

// Host: one row's sample. False when the row is to be removed.
bool pose_host_row(const void* world, PoseRow& r, ULONGLONG now) {
    r.stream_due = false;
    void* const mesh = pose_mesh(r);
    const auto* const m = static_cast<const uint8_t*>(mesh);
    int32_t count = 0;
    void* const* const bodies = mesh != nullptr ? pose_bodies(mesh, count) : nullptr;
    const bool hidden = mesh != nullptr && (m[kCompHiddenByte] & kCompHiddenBit) != 0;
    // A hidden mesh that still simulates is a bug the STSkeletalAnimator had in static-mesh mode when it died
    // (SetSkeletalMeshEnabled(false): skeletal mesh hidden, KinematicBonesUpdateType SkipAllBones). Its ragdoll keeps
    // running and the game shows it again later (hb35 guid 0x1d8, hb41 guid 0x1a4), so the row is kept, not sent.
    if (hidden && count > 0 && c39_mesh_simulating(mesh)) {
        pose_hidden_trace(r, mesh, bodies);
        return true;
    }
    pose_guid_check(world, r);
    if (mesh == nullptr || hidden || count == 0) {
        if (r.state == PoseState::Sent && r.guid != 0 && !r.owed) {
            char text[48];
            snprintf(text, sizeof(text), "R %llx", static_cast<unsigned long long>(r.guid));
            c95_send_all(world, ChanOp::Pose, text);
            corpse_host_released(r.guid); // card 428: the client drops all three rows on this one R
        }
        ++g_pose.removed;
        if (note_event_cap(g_pose.lines, kPoseLines, "card132 pose")) {
            log::line("card132: host pose remove guid 0x%llx (%s, %u snaps)", static_cast<unsigned long long>(r.guid),
                      mesh == nullptr ? "actor gone" : count == 0 ? "no bodies" : "mesh hidden", r.snap);
        }
        return false;
    }
    double moved = 0.0;
    for (int32_t i = 0; i < count; ++i) {
        Xform x{};
        const Vec3 at = pose_world(bodies[i], x) ? Vec3{x.t[0], x.t[1], x.t[2]} : r.last[i];
        moved = fmax(moved, vec_dist(at, r.last[i]));
        r.last[i] = at;
    }
    r.still_ms = moved < kPoseStill && r.bodies == count ? r.still_ms + kPoseStepMs : 0;
    r.woke = r.woke || (r.state == PoseState::Sent && moved >= kPoseStill); // card 339: cleared by pose_snapshot
    if (r.c189_check) {
        c189_check_line(r, mesh);
    }
    pose_fell_check(r, mesh);
    r.bodies = r.state == PoseState::Moving ? count : r.bodies;
    if (r.state == PoseState::Sent && vec_dist(r.last[0], r.sent_root) > kPoseMoved) {
        r.state = PoseState::Moving;
        r.start = now;
    }
    // A mesh that stopped simulating (the game froze the corpse) keeps its bodies where they are: settled.
    const bool settled = r.still_ms >= kPoseStillMs || !g_pose_mesh_awake(mesh) || !c39_mesh_simulating(mesh);
    // An unsettled snap (sent at kPoseForceMs) is followed by the settled pose, whatever body 0 did (hb36 guid 0xf0:
    // body 0 moved under kPoseMoved while the rest kept moving, so the client kept the unsettled pose).
    if (r.state == PoseState::Sent && (r.flags & kPoseUnsettled) != 0 && settled) {
        r.state = PoseState::Moving;
    }
    const bool snap_due = r.state == PoseState::Moving && (settled || now - r.start >= kPoseForceMs);
    // Card 152: a moving corpse with no snap due streams this sample (pose_stream_send, after every row's sample).
    r.stream_due = !snap_due && r.state == PoseState::Moving && moved >= kPoseStill && r.guid != 0;
    if (snap_due) {
        pose_snapshot(world, r, bodies, count, now);
    }
    return true;
}

// Host (card 152): the deepest pace queue over the remote PCs, in frames (the mod channel's backlog).
uint32_t pose_stream_backlog(const void* world) {
    uint32_t depth = 0;
    for (int s = 0; world != nullptr && s < kC17Slots; ++s) {
        const uint32_t d = chan_pace_depth(g_get_player_controller(world, s));
        depth = d > depth ? d : depth;
    }
    g_pose.st_backlog_peak = depth > g_pose.st_backlog_peak ? depth : g_pose.st_backlog_peak;
    return depth;
}

// Host (card 152): a row's bodies now, as one stream sample labelled r.stream_seq to every ponged PC, encoded into its
// own buffers (the row's blob keeps the last snap). Card 521 step 2: the sample goes as a delta from r.stream_base
// unless its seq is a whole one (kPoseWholeEvery), the body count changed or the delta is not shorter; r.stream_base
// then becomes this sample as the client decodes it. False when the bodies cannot be read or encoded.
bool pose_stream_emit(const void* world, PoseRow& r, PoseSend& s, Vec3& root, int32_t& count, bool& delta) {
    void* const mesh = pose_mesh(r);
    count = 0;
    delta = false;
    void* const* const bodies = mesh != nullptr ? pose_bodies(mesh, count) : nullptr;
    static uint8_t blob[kPoseBlob]; // game thread
    static uint8_t rt[kPoseBlob];
    static uint8_t wire[kPoseBlob];
    static uint8_t dwire[kPoseBlob];
    size_t n = 0, rt_n = 0;
    if (count == 0 || !pose_encode_to(mesh, bodies, count, blob, n, root) ||
        n != kPoseHeadBytes + static_cast<size_t>(count) * kPoseBodyBytes) {
        return false;
    }
    const size_t wn = pose_wire_pack(blob, count, wire);
    if (!pose_wire_unpack(wire, wn, count, rt, rt_n)) {
        return false;
    }
    const bool may = r.stream_seq % kPoseWholeEvery != 0 && r.stream_base_n == rt_n;
    const size_t dn = may ? pose_delta_pack(r.stream_base, rt, count, dwire, wn) : 0;
    if (may) {
        pose_delta_stats(r.stream_base, rt, count);
        g_pose.c521_big += dn == 0 ? 1u : 0u;
    }
    // Card 521 step 3: a delta's base for the next sample is what the client decodes from it (rotations rounded to
    // kPoseRotStep, maybe negated), not rt. A delta that does not decode goes whole.
    static uint8_t dec[kPoseBlob];
    size_t dec_n = 0;
    delta = dn != 0 && pose_delta_unpack(r.stream_base, dwire, dn, count, dec, dec_n) && dec_n == rt_n;
    const uint32_t flags = kPoseStream | (delta ? kPoseDelta : 0u);
    s = pose_send_wire(PoseHead{r.guid, r.stream_seq, count, flags}, delta ? dwire : wire, delta ? dn : wn, nullptr,
                       world);
    memcpy(r.stream_base, delta ? dec : rt, rt_n);
    r.stream_base_n = rt_n;
    (delta ? g_pose.c521_deltas : g_pose.c521_wholes) += 1;
    (delta ? g_pose.c521_delta_bytes : g_pose.c521_whole_bytes) += static_cast<uint32_t>(delta ? dn : wn);
    return true;
}

// Host (card 152): one stream sample of a row to every ponged PC. False when the bodies cannot be read or encoded.
bool pose_stream_one(const void* world, PoseRow& r, ULONGLONG now) {
    PoseSend s{};
    Vec3 root{};
    int32_t count = 0;
    bool delta = false;
    if (!pose_stream_emit(world, r, s, root, count, delta)) {
        return false;
    }
    r.stream_at = now;
    ++g_pose.st_samples;
    g_pose.st_bytes += static_cast<uint32_t>(s.bytes);
    if (r.stream_seq < 2 && note_event_cap(g_pose.stream_lines, kPoseStreamLines, "card152 stream")) {
        log::line("card152: host stream guid 0x%llx sample %u: %d bodies, root (%.0f, %.0f, %.0f); %d bytes to %d PCs",
                  static_cast<unsigned long long>(r.guid), r.stream_seq, count, root.x, root.y, root.z, s.bytes, s.sent);
    }
    ++r.stream_seq;
    return true;
}

// Host (card 152): the due row whose last sample is oldest, or null.
PoseRow* pose_stream_oldest() {
    PoseRow* best = nullptr;
    for (int32_t i = 0; i < g_pose.n; ++i) {
        PoseRow& r = g_pose.row[i];
        best = r.stream_due && (best == nullptr || r.stream_at < best->stream_at) ? &r : best;
    }
    return best;
}

// Host (card 152): this step's stream samples, oldest first. A sample is replaceable by the next one, so a backed-up
// channel skips the step's samples (counted) and samples over kPoseStreamPerStep wait a step (counted); a snap
// (pose_snapshot) is never held back by either.
void pose_stream_send(const void* world, ULONGLONG now) {
    int32_t due = 0;
    for (int32_t i = 0; i < g_pose.n; ++i) {
        due += g_pose.row[i].stream_due ? 1 : 0;
    }
    g_pose.st_moving_peak = due > g_pose.st_moving_peak ? due : g_pose.st_moving_peak;
    const bool backed_up = due != 0 && pose_stream_backlog(world) > kPoseStreamBacklog;
    int32_t sent = 0;
    for (int32_t k = 0; k < due; ++k) {
        PoseRow* const r = pose_stream_oldest();
        r->stream_due = false;
        if (backed_up || sent >= kPoseStreamPerStep) {
            g_pose.st_backlog_skips += backed_up ? 1u : 0u;
            g_pose.st_deferred += backed_up ? 0u : 1u;
            continue;
        }
        sent += pose_stream_one(world, *r, now) ? 1 : 0;
    }
    g_pose.st_cap_hits += !backed_up && due > kPoseStreamPerStep ? 1u : 0u;
    g_pose.st_step_peak = sent > g_pose.st_step_peak ? sent : g_pose.st_step_peak;
}

void pose_host_step(const void* world, ULONGLONG now) {
    for (int32_t i = g_pose.n - 1; i >= 0; --i) {
        if (!pose_host_row(world, g_pose.row[i], now)) {
            PoseRow& r = g_pose.row[i];
            void* const mesh = r.held ? pose_mesh(r) : nullptr;
            if (mesh != nullptr) { // card 189: an actor that outlives its row (revive, reuse) must not stay held
                pose_release(r, mesh);
                log::line("card189: host pose guid 0x%llx row removed while held: released",
                          static_cast<unsigned long long>(r.guid));
            }
            pose_remove(i);
        }
    }
    pose_stream_send(world, now);
}

// Body i's position in a blob (root plus the int16 offset).
Vec3 pose_want_from(const uint8_t* blob, int32_t i) {
    float head[3];
    memcpy(head, blob, sizeof(head));
    int16_t v[3];
    memcpy(v, blob + kPoseHeadBytes + static_cast<size_t>(i) * kPoseBodyBytes, sizeof(v));
    return {head[0] + v[0] * kPoseUnit, head[1] + v[1] * kPoseUnit, head[2] + v[2] * kPoseUnit};
}

// A row's body i position, decoded from its blob.
Vec3 pose_want(const PoseRow& r, int32_t i) {
    return pose_want_from(r.blob, i);
}

// Card 152: body i's rotation in a blob, normalized; false for the gone marker (all zero).
bool pose_quat_from(const uint8_t* blob, int32_t i, double q[4]) {
    int16_t v[7];
    memcpy(v, blob + kPoseHeadBytes + static_cast<size_t>(i) * kPoseBodyBytes, sizeof(v));
    double len = 0.0;
    for (int32_t k = 0; k < 4; ++k) {
        q[k] = v[3 + k] / 32767.0;
        len += q[k] * q[k];
    }
    len = sqrt(len);
    for (int32_t k = 0; k < 4 && len > 0.0; ++k) {
        q[k] /= len;
    }
    return len > 0.0;
}

// Card 152: the farthest any body moves from the row's previous stream sample to its current one (the digest's extra
// tolerance for a copy moving between the two).
double pose_stream_span(const PoseRow& r) {
    double span = 0.0;
    for (int32_t i = 0; r.live && i < r.bodies; ++i) {
        span = fmax(span, vec_dist(pose_want_from(r.prev, i), pose_want_from(r.blob, i)));
    }
    return span;
}

// True when the host's body i was gibbed (pose_encode's all-zero rotation marker).
bool pose_gone(const PoseRow& r, int32_t i) {
    int16_t v[7];
    memcpy(v, r.blob + kPoseHeadBytes + static_cast<size_t>(i) * kPoseBodyBytes, sizeof(v));
    return v[3] == 0 && v[4] == 0 && v[5] == 0 && v[6] == 0;
}

struct PoseErr {
    double before, after, err;
};

double c359_scale_off(const Vec3& a, const Vec3& b) {
    const double x = fabs(a.x - b.x), y = fabs(a.y - b.y), z = fabs(a.z - b.z);
    return x > y ? (x > z ? x : z) : (y > z ? y : z);
}

// Card 359 step 5, client: the copy's mesh world scale set to the host's (the blob head) before its bodies are
// posed. The relative scale is the host's world scale over the attach parent's (FTransform scales multiply per
// axis). A row per copy only when the scale still differs after the write; the first few writes are logged.
void c359_scale_apply(void* mesh, const PoseRow& r) {
    constexpr uint32_t kC359ScaleLines = 4; // per map, the writes that worked
    uint16_t s16[3];
    memcpy(s16, r.blob + kPoseRootBytes, sizeof(s16));
    if (s16[0] == 0 || s16[1] == 0 || s16[2] == 0) {
        return; // the host could not read it
    }
    const Vec3 want{s16[0] / kPoseScaleSteps, s16[1] / kPoseScaleSteps, s16[2] / kPoseScaleSteps};
    const Vec3 had = read_vec3(mesh, kComponentScale);
    if (c359_scale_off(had, want) <= kC359ScaleTolerance) {
        return;
    }
    const void* const parent = c140_attach_parent(mesh);
    const Vec3 ps = parent != nullptr ? read_vec3(parent, kComponentScale) : Vec3{1.0, 1.0, 1.0};
    const bool can = g_set_rel_scale != nullptr && ps.x != 0.0 && ps.y != 0.0 && ps.z != 0.0;
    if (can) {
        const double rel[3] = {want.x / ps.x, want.y / ps.y, want.z / ps.z};
        g_set_rel_scale(mesh, rel);
        ++g_pose.c359_scaled;
    }
    const Vec3 now = read_vec3(mesh, kComponentScale);
    const double off = c359_scale_off(now, want);
    if (off > kC359ScaleTolerance) {
        if (!r.c359_scale_warned) {
            r.c359_scale_warned = true;
            log::line("WARN card359: client guid 0x%llx mesh scale (%.3f, %.3f, %.3f) after the write, host (%.3f, %.3f, "
                      "%.3f), was (%.3f, %.3f, %.3f), parent %s scale (%.3f, %.3f, %.3f), setter %s",
                      static_cast<unsigned long long>(r.guid), now.x, now.y, now.z, want.x, want.y, want.z, had.x,
                      had.y, had.z, parent != nullptr ? object_name(parent).text : "none", ps.x, ps.y, ps.z,
                      g_set_rel_scale != nullptr ? "bound" : "missing");
        }
    } else if (note_event_cap(g_pose.c359_scale_lines, kC359ScaleLines, "card359 scale")) {
        log::line("card359: client guid 0x%llx mesh scale (%.3f, %.3f, %.3f) set to the host's (%.3f, %.3f, %.3f)",
                  static_cast<unsigned long long>(r.guid), had.x, had.y, had.z, now.x, now.y, now.z);
    }
}

// Client: pose the copy's bodies from the decoded blob, zero velocities, sleep, and read back.
PoseErr pose_apply_bodies(void* mesh, void* const* bodies, const PoseRow& r) {
    c359_scale_apply(mesh, r); // card 359 step 5: before the bodies, so the pose lands on a copy of the host's size
    float head[3];
    memcpy(head, r.blob, sizeof(head));
    const Vec3 root{head[0], head[1], head[2]};
    PoseErr e{};
    Xform x0{};
    e.before = pose_world(bodies[0], x0) ? vec_dist(Vec3{x0.t[0], x0.t[1], x0.t[2]}, root) : -1.0;
    Vec3 want[kPoseBodies];
    for (int32_t i = 0; i < r.bodies; ++i) {
        want[i] = pose_want(r, i);
        if (pose_gone(r, i)) {
            continue; // gibbed on the host; the gib relay breaks the client's limb off too
        }
        int16_t v[7];
        memcpy(v, r.blob + kPoseHeadBytes + static_cast<size_t>(i) * kPoseBodyBytes, sizeof(v));
        Xform x{};
        pose_world(bodies[i], x); // keeps the client's own scale
        double q[4], len = 0.0;
        for (int32_t k = 0; k < 4; ++k) {
            q[k] = v[3 + k] / 32767.0;
            len += q[k] * q[k];
        }
        len = len > 0.0 ? sqrt(len) : 1.0;
        for (int32_t k = 0; k < 4; ++k) {
            x.q[k] = q[k] / len;
        }
        x.t[0] = want[i].x;
        x.t[1] = want[i].y;
        x.t[2] = want[i].z;
        g_pose_body_set(bodies[i], &x, kTeleportPhysics, false);
    }
    const Vec3 zero{0.0, 0.0, 0.0};
    g_pose_mesh_linear(mesh, &zero, false);
    g_pose_mesh_angular(mesh, &zero, false);
    g_pose_mesh_sleep(mesh);
    for (int32_t i = 0; i < r.bodies; ++i) {
        Xform x{};
        if (pose_gone(r, i) || !pose_world(bodies[i], x)) {
            continue; // a gibbed limb, on either side, has no body to compare
        }
        e.err = fmax(e.err, vec_dist(Vec3{x.t[0], x.t[1], x.t[2]}, want[i]));
        e.after = i == 0 ? vec_dist(Vec3{x.t[0], x.t[1], x.t[2]}, root) : e.after;
    }
    return e;
}

PoseArrival* arrival_find(const void* npc) {
    for (int32_t i = 0; npc != nullptr && i < g_arrive.n; ++i) {
        if (c31_weak_get(g_arrive.a[i].actor) == npc) {
            return &g_arrive.a[i];
        }
    }
    return nullptr;
}

void arrival_remove(PoseArrival* a) {
    *a = g_arrive.a[--g_arrive.n];
}

// Client: show an arrival's mesh again. Called just before its pose apply (a mesh hidden when its ragdoll starts sinks
// through the floor, hb14) or when it gives up waiting.
void arrival_show(const PoseArrival& a, void* mesh) {
    if (a.hid && mesh != nullptr && g_orig_c132_set_hidden != nullptr) {
        g_orig_c132_set_hidden(mesh, false, 0);
    }
}

// Client: apply a complete row when the copy is ready. Returns the reason it is not, or null once applied.
uint64_t g_pose_frame = 0; // client: c132_pose_step calls, one per game tick (game thread only)

void pose_release(PoseRow& r, void* mesh); // below, with pose_hold
const char* pose_stream_apply(PoseRow& r, void* npc, void* mesh, void* const* bodies); // card 152, below
void pose_stream_final(PoseRow& r, const PoseErr& e, bool hold);           // card 152, below
void pose_bone_line(PoseRow& r, void* mesh, void* const* bodies, const char* what);       // card 152, below

// Client: the copy a row poses, its mesh and bodies; the reason it cannot be posed yet, or null.
const char* pose_copy(const PoseRow& r, void*& npc, void*& mesh, void* const*& bodies) {
    void* const pc = c31_weak_get(r.pc);
    npc = pc != nullptr ? c41_resolve_guid(pc, r.guid) : nullptr;
    mesh = npc != nullptr ? read_ptr(npc, kCharacterMesh) : nullptr;
    int32_t count = 0;
    bodies = mesh != nullptr ? pose_bodies(mesh, count) : nullptr;
    return npc == nullptr ? "guid unmapped" : mesh == nullptr ? "no mesh" : count != r.bodies ? "body count differs"
                                                                                               : nullptr;
}

// Client: an arrival posed (card 137), logged uncapped: one line per corpse that arrived dead.
void arrival_posed(PoseArrival* arrival, void* npc, const PoseRow& r, const PoseErr& e, bool started) {
    if (arrival == nullptr) {
        return;
    }
    ++g_arrive.posed;
    log::line("card137: client corpse arrival %s guid 0x%llx posed %llu ms after it arrived: hidden %u ticks, "
              "visible 0 frames before its pose, no death play; %.0f uu from the host's root after, largest body "
              "error %.1f uu%s", object_name(npc).text, static_cast<unsigned long long>(r.guid),
              static_cast<unsigned long long>(GetTickCount64() - arrival->first), arrival->hidden_ticks, e.after,
              e.err, started ? " (ragdoll started for it)" : "");
    arrival_remove(arrival);
}

// pose_try_apply's collision gate: nullptr when the copy's mesh has physics collision, else the reason.
// hb38: a late joiner's copy is posed only once its own death has given the mesh physics collision (the game's
// EnableRagdoll sets the Ragdoll profile, QueryAndPhysics, a few hundred ms after the copy arrives). Posed and
// simulating before that, with the living bug's query-only profile, the bodies have no floor and fall through the
// map (guid 0xe6, 0x8c: 8000 uu down by the next round); and EnableRagdoll, running after the pose, moved it again
// (0xd2, 744 uu). A copy that never gets there is dropped with a WARN after kPosePendingMs.
// hb40 (guid 0x18a): the copy's death edge and ragdoll start had run (collision 3), yet the gate saw other
// collision 4 s later; the reason now says which of the two holds, with the collision value, for the WARN.
const char* pose_collision_reason(void* npc, void* mesh) {
    const uint8_t collision = g_collision_enabled != nullptr ? g_collision_enabled(mesh) : 0;
    if (collision == kPoseQueryAndPhysics) {
        return nullptr;
    }
    static char reason[160]; // game thread only
    snprintf(reason, sizeof(reason), "copy has mesh collision %u, not physics (%s; simulating %d)", collision,
             c38_is_marked(npc) ? "its death edge ran, collision changed after" : "its death has not run",
             c39_mesh_simulating(mesh) ? 1 : 0);
    return reason;
}

// The apply line of pose_try_apply (capped) and, uncapped, the unfreeze line.
void pose_applied_log(const PoseRow& r, void* npc, void* mesh, const PoseErr& e, bool hold, bool started) {
    if (note_event_cap(g_pose.lines, kPoseLines, "card132 pose")) {
        log::line("card132: client pose apply %s guid 0x%llx snap %u%s%s: %d bodies, host root (%.0f, %.0f, %.0f), "
                  "%.0f uu from the client's before, %.0f after; largest body error %.1f uu; %llu ms after it came in",
                  object_name(npc).text, static_cast<unsigned long long>(r.guid), r.snap,
                  (r.flags & kPoseFrozen) != 0      ? " (host frozen, simulation off)"
                  : (r.flags & kPoseUnsettled) != 0 ? " (host unsettled)"
                  : hold                            ? " (host settled, held)"
                                                    : "",
                  started ? " (ragdoll started for it)" : "", r.bodies, r.sent_root.x, r.sent_root.y, r.sent_root.z,
                  e.before, e.after, e.err, static_cast<unsigned long long>(GetTickCount64() - r.got));
    }
    if ((r.flags & kPoseUnfreeze) != 0) { // logged uncapped: the host logs each unfreeze it sends
        const bool sim = c39_mesh_simulating(mesh);
        ++g_pose.unfreezes;
        g_pose.unfreeze_still += sim ? 0u : 1u;
        log::line("%scard137: client pose unfreeze guid 0x%llx snap %u applied: simulating %d after the apply",
                  sim ? "" : "WARN ", static_cast<unsigned long long>(r.guid), r.snap, sim ? 1 : 0);
    }
}

// Client: a copy not simulating gets its ragdoll started through the card 039/132 start; `started` says it did.
// False when the copy would not simulate (counted). Card 358: a copy already simulating with bBlendPhysics off (the
// game's SetSimulatePhysics was held by card 152 but its direct SetAllBodiesSimulatePhysics(1) was not) is started
// again: only the stock SetSimulatePhysics sets bBlendPhysics, and without it the bones never follow the bodies
// (guid 0x424 frozen 328.8 uu off in b42).
uint8_t* pose_blend_byte(void* mesh, uint8_t& mask); // defined below (card 140)
bool pose_start_ragdoll(void* mesh, bool& started) {
    uint8_t mask = 0;
    const uint8_t* const blend = pose_blend_byte(mesh, mask);
    const bool blending = blend == nullptr || (*blend & mask) != 0;
    started = !c39_mesh_simulating(mesh) || !blending;
    if (!started) {
        return true;
    }
    detour_c132_set_simulate(mesh, true);
    if (!c39_mesh_simulating(mesh)) {
        ++g_pose.started_failed;
        return false;
    }
    ++g_pose.started;
    return true;
}

// Client: a snap row's bookkeeping once its bodies are posed (split out of pose_try_apply, card 186 r2).
void pose_applied_set(PoseRow& r, const PoseErr& e, bool hold) {
    r.state = hold ? PoseState::Freezing : PoseState::Applied;
    r.applied_at = GetTickCount64();
    r.posed_frame = g_pose_frame;
    r.freeze_waits = 0; // card 191: a new apply gets the whole wait budget
    r.drift_logged = false;
    ++g_pose.applied;
    wait_clear(r.guid, "the snap apply", r.got); // card 152: a copy whose game start waited is posed
    g_pose.err_max = fmax(g_pose.err_max, e.err);
    g_pose.after_max = fmax(g_pose.after_max, e.after);
    r.err_apply = static_cast<float>(e.err);
    r.check_owed = false;
    if (r.streamed) {
        pose_stream_final(r, e, hold);
    }
}

double c191_body0_dist(const PoseRow& r, void* mesh); // card 191, below with the end tick detour
constexpr double kC191BoneTolerance = 1.0; // uu, bone 0 to body 0 at the freeze
constexpr uint8_t kC191FreezeWaits = 10;   // ticks the freeze waits for the blend

// Card 191, client: every held final (frozen or settled, first copy or arrival) arms its mesh's end tick rows at the
// apply; an arrival also records bone 0's distance from body 0 around the pose, logged at the freeze.
C359Snap c359_snap(const PoseRow& r, void* mesh); // below

void c191_arm(PoseRow& r, void* mesh, double before, double after) {
    r.c191_mesh = mesh;
    r.c191_ticks = 0;
    r.c191_frozen = false;
    r.c191_pre = 0;
    r.c191_seen = 0;
    r.c191_apply_before = static_cast<float>(before);
    r.c191_apply_after = static_cast<float>(after);
    r.c359_before = -1.0F;
    r.c359_after = -1.0F;
    r.c359_weight = -1.0F;
    r.c359_apply = c359_snap(r, mesh);
    r.c359_last = C359Snap{};
    g_pose.c191_any = true;
}

const char* pose_try_apply(PoseRow& r) {
    void* npc = nullptr;
    void* mesh = nullptr;
    void* const* bodies = nullptr;
    const char* const why = pose_copy(r, npc, mesh, bodies);
    if (why != nullptr) {
        return why;
    }
    // Card 152 r2: a stream sample or an unfreeze row starts or continues the stream; a settled, frozen or unsettled
    // row ends it: the snap path below poses (and freezes) the copy as for any snap. Card 186: a streaming copy's
    // frozen final is held one tick (gravity off, no stream move) so physics blends the posed bodies into the bones
    // before the freeze (pose_freeze_step), which turns gravity back on; a settled or unsettled final gets its gravity
    // back before the apply (below), so nothing touches the bodies after the apply's sleep.
    if (r.streamed && (r.flags & (kPoseStream | kPoseUnfreeze)) != 0) {
        return pose_stream_apply(r, npc, mesh, bodies);
    }
    r.own_fall = false; // card 523: a final ends an own fall too (its gravity is still on, as for an unsettled final)
    r.sim_at_final = c39_mesh_simulating(mesh); // card 186 r2: the frozen-final bone diagnostic
    r.live = false; // gravity stays as it is: only a streamed copy has it off; a frozen final holds it off a tick
    const char* const no_physics = pose_collision_reason(npc, mesh);
    if (no_physics != nullptr) {
        return no_physics;
    }
    // Card 137: a corpse that arrived dead and is held hidden is shown now, in the same tick as its pose below.
    PoseArrival* const arrival = arrival_find(npc);
    if (arrival != nullptr) {
        r.arrived = true; // card 251
        arrival_show(*arrival, mesh);
    }
    // A copy with physics collision that is not a ragdoll (a copy whose simulation the game already turned off)
    // gets its ragdoll started here, through the card 039/132 ragdoll start
    // (bodies moved to the current pose first), so the host's pose below is what it shows.
    pose_release(r, mesh);
    bool started = false;
    if (!pose_start_ragdoll(mesh, started)) {
        return "copy would not simulate";
    }
    c31_weak_set(r.actor, npc);
    // Card 334: a settled final is held as a frozen one is (posed simulating, frozen on the next client step by
    // pose_freeze_step), so client physics cannot move a corpse the host has at rest. The next stream sample releases
    // it (pose_stream_start calls pose_release). Unsettled, unfreeze and stream rows are not held.
    const bool hold = (r.flags & (kPoseUnsettled | kPoseUnfreeze | kPoseStream)) == 0;
    if (r.streamed && !hold) {
        // Card 186 r2 (the r2 order): gravity on before the apply. pose_apply_bodies ends in the bodies' sleep, and
        // SetEnableGravity after it woke them (c186-r1: settled finals 2.1 to 9.3 uu off one tick later).
        pose_grav_on(r, mesh);
    }
    if (hold && !r.grav_off) {
        // Card 424: a held final on a copy with no stream (a corpse back in relevancy) simulated its held tick with
        // gravity on: 3 of 10 such finals ended 296 to 345 uu off the host (c418b121a), 0 of 44 with gravity off.
        // Off before the apply (card 186 r2: a gravity set after it wakes the bodies); pose_freeze_step turns it on.
        pose_grav_off(r, mesh);
    }
    const double c191_before = hold && r.arrived ? c191_body0_dist(r, mesh) : -1.0;
    const PoseErr e = pose_apply_bodies(mesh, bodies, r);
    if (hold && g_orig_c191_end_tick != nullptr) { // card 191: only with the end tick detour installed
        c191_arm(r, mesh, c191_before, r.arrived ? c191_body0_dist(r, mesh) : -1.0);
    }
    // A frozen row: the bodies are posed first (above, simulating), then the copy stops as the host's did. This also
    // covers a copy the game froze itself at another pose before the row came: the row restarts it, poses, freezes.
    // Card 140 run 13 (guid 0x358, 775 uu): stopping the simulation in this same tick made the kinematic bodies follow
    // the bones, which still held the pose physics wrote last tick (the copy's own fallen pose), so the apply was
    // undone. The freeze waits for the next client step (pose_freeze_step).
    pose_applied_set(r, e, hold);
    arrival_posed(arrival, npc, r, e, started);
    pose_applied_log(r, npc, mesh, e, hold, started);
    return nullptr;
}

// Client: each body of an applied corpse against the host's body (decoded from the row). Returns the largest
// distance, the body it is at and how many bodies are over kPoseTolerance; -1 when a body is unreadable. Card 203:
// also body 0 on both sides (body0 -1 when it is gone or unreadable).
struct PoseFar {
    double worst;
    int32_t at, over, gone;
    Vec3 client, host;
    double body0;
    Vec3 client0, host0;
};
PoseFar pose_far(const PoseRow& r, void* const* bodies, int32_t count) {
    PoseFar f{-1.0, -1, 0, 0, {}, {}, -1.0, {}, {}};
    double body0 = -1.0;
    Vec3 client0{}, host0{};
    for (int32_t i = 0; i < count; ++i) {
        Xform x{};
        if (pose_gone(r, i) || !pose_world(bodies[i], x)) {
            ++f.gone; // gibbed: the host's gib relay breaks the same limb off (card042)
            continue;
        }
        const Vec3 c{x.t[0], x.t[1], x.t[2]};
        const Vec3 h = pose_want(r, i);
        const double d = vec_dist(c, h);
        f.over += d > kPoseTolerance ? 1 : 0;
        if (i == 0) {
            body0 = d;
            client0 = c;
            host0 = h;
        }
        if (d > f.worst) {
            f = PoseFar{d, i, f.over, f.gone, c, h, -1.0, {}, {}};
        }
    }
    f.body0 = body0;
    f.client0 = client0;
    f.host0 = host0;
    return f;
}

// Client (card 140 Pose diag): a frozen row found simulating again after its apply. Logged once per apply with the
// time since it, so a round mismatch can be told apart: the game restarted the copy's physics (drift) or the copy
// never matched (apply).
void pose_drift_note(PoseRow& r, ULONGLONG now) {
    if (r.drift_logged || (r.flags & kPoseFrozen) == 0) {
        return;
    }
    void* const mesh = pose_mesh(r);
    if (mesh == nullptr || !c39_mesh_simulating(mesh)) {
        return;
    }
    r.drift_logged = true;
    if (note_event_cap(g_pose.warn_lines, kPoseWarnLines, "card132 pose warn")) {
        int32_t count = 0;
        void* const* const bodies = pose_bodies(mesh, count);
        const PoseFar f = bodies != nullptr && count == r.bodies ? pose_far(r, bodies, count) : PoseFar{-1.0, -1, 0, 0, {}, {}, -1.0, {}, {}};
        log::line("WARN card140: client pose guid 0x%llx snap %u: frozen row simulating %llu ms after its apply; worst "
                  "body %d is %.0f uu from the host's; own freeze calls %u",
                  static_cast<unsigned long long>(r.guid), r.snap, static_cast<unsigned long long>(now - r.applied_at),
                  f.at, f.worst, g_pose.own_freezes);
    }
}

// Client: the worst body of a row's copy against the host's, -1 when the copy or its bodies are not readable.
PoseFar pose_far_now(const PoseRow& r, void*& mesh) {
    mesh = pose_mesh(r);
    int32_t count = 0;
    void* const* const bodies = mesh != nullptr ? pose_bodies(mesh, count) : nullptr;
    return bodies != nullptr && count == r.bodies ? pose_far(r, bodies, count) : PoseFar{-1.0, -1, 0, 0, {}, {}, -1.0, {}, {}};
}

// USkeletalMeshComponent::bBlendPhysics (a bitfield bool, found by reflection once): the bones are blended from the
// physics bodies whether they simulate or not. Null when the property is not found (logged once).
uint8_t* pose_blend_byte(void* mesh, uint8_t& mask) {
    static int32_t offset = -1;
    static uint8_t field_mask = 0;
    if (offset < 0) {
        const uint8_t* const f = prop_find_prop(read_ptr(mesh, kObjectClass), "bBlendPhysics");
        offset = f != nullptr ? read_i32(f, kPropertyOffset) + f[kBoolByteOffset] : 0;
        field_mask = f != nullptr ? f[kBoolFieldMask] : 0;
        log::line("%scard140: client bBlendPhysics at +0x%X mask 0x%02X", offset > 0 && field_mask != 0 ? "" : "WARN ",
                  static_cast<unsigned>(offset), field_mask);
    }
    mask = field_mask;
    return offset > 0 && field_mask != 0 ? static_cast<uint8_t*>(mesh) + offset : nullptr;
}

// Client, a frozen copy just after its freeze (card 140 run 14, guid 0x342: 0.0 uu after the freeze call, 994 uu one
// tick later with simulation off). Two settings hold the host's pose: bBlendPhysics writes the posed kinematic bodies
// into the bones every bone refresh, so the mesh shows the host's pose, and SkipAllBones stops the bones from moving
// the kinematic bodies back to the copy's own pose. The old values are put back by pose_release.
void pose_hold_bits(void* mesh) {
    uint8_t mask = 0;
    uint8_t* const blend = pose_blend_byte(mesh, mask);
    if (blend != nullptr) {
        *blend = static_cast<uint8_t>(*blend | mask);
    }
    static_cast<uint8_t*>(mesh)[kKinematicBonesUpdateType] = kSkipAllBones;
}

// Card 251: a frozen copy moved as a unit (capsule and mesh together) after its freeze. The copy's character movement
// component is the suspect: a frozen corpse has no movement to smooth, so the hold turns its tick off and the release
// restores what the hold found. UActorComponent::SetComponentTickEnabled is vtable +0x410 (UCharacterMovementComponent
// vtable slot 130 = ?SetComponentTickEnabled@UActorComponent@@UEAAX_N@Z, exe+0x3A50060); it calls
// FTickFunction::SetTickFunctionEnable on PrimaryComponentTick (+0x38), whose TickState is bits 2-3 of its byte +0xB.
constexpr size_t kCompSetTickEnabledVfn = 0x410;
constexpr size_t kCompTickStateByte = 0x38 + 0xB; // PrimaryComponentTick.TickState (0 disabled)
constexpr uint8_t kCompTickStateMask = 0x0C;
constexpr size_t kSceneRelativeLocation = 0x140; // USceneComponent::RelativeLocation (card034 reflected)
constexpr uint32_t kC251Lines = 80;              // per map

void* pose_cmc(const PoseRow& r) {
    void* const actor = c31_weak_get(r.actor);
    return actor != nullptr ? read_ptr(actor, kCharacterMovement) : nullptr;
}
bool pose_cmc_ticking(const void* cmc) {
    return (static_cast<const uint8_t*>(cmc)[kCompTickStateByte] & kCompTickStateMask) != 0;
}
void pose_cmc_tick(void* cmc, bool on) {
    using SetTickFn = void (*)(void*, bool);
    const auto set_tick = reinterpret_cast<SetTickFn>(read_ptr(read_ptr(cmc, 0), kCompSetTickEnabledVfn));
    set_tick(cmc, on);
}

// Card 251, client: the copy's capsule, mesh offset and movement state, at the freeze check and in pose_check_warn.
void c251_line(const PoseRow& r, const void* mesh, const char* when) {
    void* const actor = c31_weak_get(r.actor);
    const uint8_t* const cmc = static_cast<const uint8_t*>(pose_cmc(r));
    if (actor == nullptr || mesh == nullptr || !note_event_cap(g_pose.c251_lines, kC251Lines, "card251 corpse")) {
        return;
    }
    const Vec3 cap = actor_location(actor);
    const Vec3 rel = read_vec3(mesh, kSceneRelativeLocation);
    log::line("card251: client corpse guid 0x%llx capsule (%.0f, %.0f, %.0f) mesh rel (%.0f, %.0f, %.0f) cmc tick %d "
              "mode %d (%s, %s)",
              static_cast<unsigned long long>(r.guid), cap.x, cap.y, cap.z, rel.x, rel.y, rel.z,
              cmc != nullptr ? (pose_cmc_ticking(cmc) ? 1 : 0) : -1, cmc != nullptr ? cmc[kCmcMovementMode] : -1,
              r.arrived ? "arrival" : "first copy", when);
}

// Card 191: USkinnedMeshComponent::bNoSkeletonUpdate, by reflection as pose_blend_byte. Null (WARN once) when not found.
uint8_t* pose_noskel_byte(void* mesh, uint8_t& mask) {
    static int32_t offset = -1;
    static uint8_t field_mask = 0;
    if (offset < 0) {
        const uint8_t* const f = prop_find_prop(read_ptr(mesh, kObjectClass), "bNoSkeletonUpdate");
        offset = f != nullptr ? read_i32(f, kPropertyOffset) + f[kBoolByteOffset] : 0;
        field_mask = f != nullptr ? f[kBoolFieldMask] : 0;
        log::line("%scard191: client bNoSkeletonUpdate at +0x%X mask 0x%02X", offset > 0 && field_mask != 0 ? "" : "WARN ",
                  static_cast<unsigned>(offset), field_mask);
    }
    mask = field_mask;
    return offset > 0 && field_mask != 0 ? static_cast<uint8_t*>(mesh) + offset : nullptr;
}

void pose_hold(PoseRow& r, void* mesh) {
    void* const cmc = pose_cmc(r); // card 251: the movement tick off while held
    r.cmc_tick_was = static_cast<int8_t>(cmc == nullptr ? -1 : (pose_cmc_ticking(cmc) ? 1 : 0));
    if (r.cmc_tick_was == 1) {
        pose_cmc_tick(cmc, false);
    }
    uint8_t mask = 0;
    const uint8_t* const blend = pose_blend_byte(mesh, mask);
    r.held_blend = blend != nullptr && (*blend & mask) != 0;
    r.held_update = static_cast<const uint8_t*>(mesh)[kKinematicBonesUpdateType];
    pose_hold_bits(mesh);
    r.held = true;
}

// Client: a held copy gets its own settings back before any new apply (an unfreeze or a later snap).
void pose_release(PoseRow& r, void* mesh) {
    r.c485_at = 0; // card 485 b209: a new apply or an unfreeze ends the first-render watch of the last repose
    if (!r.held) {
        return;
    }
    r.held = false;
    void* const cmc = r.cmc_tick_was == 1 ? pose_cmc(r) : nullptr; // card 251: the movement tick as the hold found it
    if (cmc != nullptr) {
        pose_cmc_tick(cmc, true);
    }
    r.cmc_tick_was = -1;
    uint8_t mask = 0;
    if (r.noskel_set) { // card 191: the skeleton refresh back on (the freeze found it on, or it would not have set it)
        r.noskel_set = false;
        uint8_t* const noskel = pose_noskel_byte(mesh, mask);
        if (noskel != nullptr) {
            *noskel = static_cast<uint8_t>(*noskel & ~mask);
        }
    }
    uint8_t* const blend = pose_blend_byte(mesh, mask);
    if (blend != nullptr) {
        *blend = static_cast<uint8_t>(r.held_blend ? (*blend | mask) : (*blend & ~mask));
    }
    static_cast<uint8_t*>(mesh)[kKinematicBonesUpdateType] = r.held_update;
}

// Card 359 (b125 rows: 91 of 91 bad copies read the same bone 0 error before and after the end tick's blend, bodies
// exact, error constant per guid): the blend writes bone = lerp(animation pose, body, the body's PhysicsBlendWeight),
// so a weight below 1 keeps a fixed share of the animation pose in the bones. Sets weight 1 on every body (the store
// SetAllBodiesPhysicsBlendWeight, exe 0x4197D30, does) and returns the lowest weight found, -1 with no body.
constexpr size_t kBodyBlendWeight = 0x134; // FBodyInstance::PhysicsBlendWeight
float c359_blend_one(void* mesh) {
    int32_t count = 0;
    void* const* const bodies = pose_bodies(mesh, count);
    const float one = 1.0F;
    float low = -1.0F;
    for (int32_t i = 0; bodies != nullptr && i < count && i < kPoseBodies; ++i) {
        if (bodies[i] == nullptr) {
            continue;
        }
        const float w = read_f32(bodies[i], kBodyBlendWeight);
        low = low < 0.0F || w < low ? w : low;
        memcpy(static_cast<uint8_t*>(bodies[i]) + kBodyBlendWeight, &one, sizeof(one));
    }
    return low;
}

// Card 424 (c423b128a reconnect: 5 of 7 copies whose freeze waited a tick had bodies 294.7 to 674.7 uu off the host
// at the freeze, 0.0 at the apply): a held tick can move the posed bodies. True when the copy's bodies are off the
// host's pose; they are then posed again (teleport, zero velocity, sleep) so the next tick blends the host's pose.
constexpr uint32_t kC424Lines = 40; // per map
bool c424_repose(PoseRow& r, void* mesh) {
    void* at = mesh;
    const PoseFar f = pose_far_now(r, at);
    int32_t count = 0;
    void* const* const bodies = pose_bodies(mesh, count);
    if (f.worst <= kPoseFreezeTolerance || at != mesh || bodies == nullptr || count != r.bodies) {
        return false;
    }
    const PoseErr e = pose_apply_bodies(mesh, bodies, r);
    ++g_pose.c424_reposes;
    if (note_event_cap(g_pose.c424_lines, kC424Lines, "card424 repose")) {
        log::line("card424: client guid 0x%llx held tick moved its bodies %.1f uu off the host (body %d); posed again "
                  "(%.1f uu after), freeze wait %u, %u so far",
                  static_cast<unsigned long long>(r.guid), f.worst, f.at, e.err,
                  static_cast<unsigned>(r.freeze_waits), g_pose.c424_reposes);
    }
    return true;
}

// Card 359 step 5 (diagnostic, b131: 54 of 54 joined guids read bone 0 on body 0 on the host, up to 52.6 uu off on
// the client with exact bodies and blend weight 1): the client copy's own component scale, the component's distance
// to body 0 and whether body 0 itself simulates, to set beside the host's card359 row for the same guid. An error that
// grows with the client's distance and not the host's means the offset comes from where the client's component sits.
struct C359Text {
    char text[352];
};
uint64_t pose_bone0_name(const void* mesh); // below
struct C359SnapText {
    char text[576];
};
C359SnapText c359_snap_text(const PoseRow& r); // below
C359Text c359_client_text(void* mesh) {
    C359Text t{};
    int32_t count = 0;
    void* const* const bodies = pose_bodies(mesh, count);
    Xform body{};
    if (bodies == nullptr || count <= 0 || bodies[0] == nullptr || !pose_world(bodies[0], body)) {
        return t;
    }
    const Vec3 at = read_vec3(mesh, kComponentTranslation);
    const Vec3 sc = read_vec3(mesh, kComponentScale);
    // Outlier trace (b136 guid 0x3a8, bone 0 1011.9 uu off with exact bodies): where the three points are.
    const uint64_t name = pose_bone0_name(mesh);
    Vec3 bone{};
    if (name != 0 && g_c136_socket_location != nullptr) {
        g_c136_socket_location(mesh, &bone, name);
    }
    snprintf(t.text, sizeof(t.text), "; mesh component %.0f uu from body 0, component scale (%.3f, %.3f, %.3f), body 0 "
             "simulating %d of %d bodies; bone 0 name read %d at (%.0f, %.0f, %.0f), body 0 at (%.0f, %.0f, %.0f), "
             "component at (%.0f, %.0f, %.0f)",
             vec_dist(at, Vec3{body.t[0], body.t[1], body.t[2]}), sc.x, sc.y, sc.z,
             g_body_simulating != nullptr ? (g_body_simulating(bodies[0]) ? 1 : 0) : -1, count, name != 0 ? 1 : 0,
             bone.x, bone.y, bone.z, body.t[0], body.t[1], body.t[2], at.x, at.y, at.z);
    return t;
}

// Client: a frozen row posed on an earlier tick is frozen now that physics has written its bodies into the bones.
// The error is measured just after the freeze call and again one tick later (pose_freeze_check), so the log shows
// which step, if any, moves the bodies off the host's pose.
// Card 485 (c244b206b, HopperBug guids 0x450, 0x6e6 and 5 more): on a copy its own game had not rendered, 11 end ticks
// after the apply left the bones where they were (bone 0 component space unchanged, 64.8 and 236.4 uu off body 0), so
// the freeze call put the bodies on those bones: 165.9 and 322.8 uu off the host after it, while the apply's own measure
// read 0.0. The end physics tick (exe+0x41874F0) has no render test of its own; the blend that skipped is still
// unnamed. A frozen copy keeps bodies posed on it (snap 3 of both guids: 0.0 after the freeze and one tick later), so
// the bodies the freeze moved off the host are posed again. Only on a wait that timed out with bone 0 off body 0.
// b209: the rows are counted per map in g_pose (they were per process and capped at 40, c244b208b at 631080053, so
// the shot step after it had no row to aim at); the first-render watch (c485_render_note) is armed on every repose.
constexpr uint32_t kC485Lines = 400; // per map
void c485_freeze_repose(PoseRow& r, void* mesh, double bone_off) {
    if (bone_off <= kC191BoneTolerance) {
        return;
    }
    void* at = mesh;
    const PoseFar f = pose_far_now(r, at);
    int32_t count = 0;
    void* const* const bodies = pose_bodies(mesh, count);
    if (f.worst <= kPoseFreezeTolerance || at != mesh || bodies == nullptr || count != r.bodies) {
        return;
    }
    const PoseErr e = pose_apply_bodies(mesh, bodies, r);
    ++g_pose.c485_reposes;
    const C191Bone b = c191_bone(r, mesh);
    r.c485_at = b.ok ? GetTickCount64() : 0;
    r.c485_cs = b.cs;
    if (note_event_cap(g_pose.c485_lines, kC485Lines, "card485 freeze repose")) {
        log::line("card485: client guid 0x%llx snap %u freeze put body %d %.1f uu off the host (bone 0 %.1f uu off body "
                  "0 at the freeze); posed again, %.1f uu after; %u so far; root at (%.0f, %.0f, %.0f)",
                  static_cast<unsigned long long>(r.guid), r.snap, f.at, f.worst, bone_off, e.err, g_pose.c485_reposes,
                  r.sent_root.x, r.sent_root.y, r.sent_root.z); // root: card 485 b208 shot step aims at it
    }
}

void pose_freeze_step(PoseRow& r) {
    void* mesh = pose_mesh(r);
    r.state = PoseState::Applied;
    r.sim_at_freeze = mesh != nullptr && c39_mesh_simulating(mesh); // card 186 r2: the frozen-final bone diagnostic
    if (mesh == nullptr) {
        pose_grav_on(r, nullptr); // card 152 r3: a held final whose copy is gone
        return; // the copy is gone; the round check skips a row with no mesh
    }
    // Card 191 (b23 rows, guid 0x20a): the bones follow the bodies only through the end-of-physics blend, and that
    // blend copies a body only while it simulates. An arrival frozen with no end tick since its pose had bone 0
    // 395.3 uu off body 0; with one end tick, 0.0. So the freeze waits, simulating, until bone 0 is on body 0
    // (bounded; -1 = not measurable, no wait).
    const double off = c191_body0_dist(r, mesh);
    const bool can_wait = r.freeze_waits < kC191FreezeWaits && r.sim_at_freeze;
    const bool reposed = can_wait && c424_repose(r, mesh); // card 424: bodies off the host: posed again, one more tick
    if ((off > kC191BoneTolerance || reposed) && can_wait) {
        const float low = c359_blend_one(mesh); // every waited tick: a game ramp may write the weight again
        r.c359_weight = r.freeze_waits == 0 ? low : r.c359_weight;
        ++r.freeze_waits;
        r.state = PoseState::Freezing;
        r.posed_frame = g_pose_frame;
        return;
    }
    // Card 505: the mesh fix runs before the bone check, so the WARN names only a copy still off body 0 after it.
    const double after = c359_mesh_to_body0(r, mesh, off); // b191: before the world row, the host join reads it
    if (after > kC191BoneTolerance) {
        // Card 359: an end tick count of 0 over the waits means the blend never ran on this copy (its end physics
        // tick registered but not ticking); the tick function's group, flag and interval bytes say why.
        const uint8_t* const tf = static_cast<const uint8_t*>(mesh) + kEndTickData - 0x18;
        log::line("WARN card191: client guid 0x%llx frozen with bone 0 %.1f uu off body 0 after %u wait tick(s), "
                  "simulating %d; card359: %u end tick(s) since the final's apply, end tick function bytes +8 "
                  "%02x %02x %02x %02x, interval %.3f; at the last waited end tick bone 0 was %.1f uu off before the "
                  "blend and %.1f after (-1 = none seen); lowest body blend weight %.3f at the first wait, set to 1 on "
                  "every waited tick%s",
                  static_cast<unsigned long long>(r.guid), after, static_cast<unsigned>(r.freeze_waits),
                  r.sim_at_freeze ? 1 : 0, static_cast<unsigned>(r.c191_pre), tf[0], tf[1], tf[2], tf[3],
                  static_cast<double>(read_f32(tf, 4)), static_cast<double>(r.c359_before),
                  static_cast<double>(r.c359_after), static_cast<double>(r.c359_weight), c359_client_text(mesh).text);
        log::line("card359: client guid 0x%llx frozen final%s", static_cast<unsigned long long>(r.guid),
                  c359_snap_text(r).text); // b187: own row, the WARN row is near the log line limit
    } else if (off > kC191BoneTolerance) {
        log::line("card191: client guid 0x%llx frozen with bone 0 %.1f uu off body 0 after %u wait tick(s), %.1f after "
                  "the card359 mesh fix",
                  static_cast<unsigned long long>(r.guid), off, static_cast<unsigned>(r.freeze_waits), after);
    } else if (r.freeze_waits != 0) {
        log::line("card191: client guid 0x%llx freeze waited %u tick(s) for bone 0, now %.1f uu from body 0; card359: lowest "
                  "body blend weight %.3f at the first wait",
                  static_cast<unsigned long long>(r.guid), static_cast<unsigned>(r.freeze_waits), off,
                  static_cast<double>(r.c359_weight));
    }
    Vec3 w{}; // b190: the client's final bone 0, set beside the host's card359 row for the same guid
    if (c191_bone0_world(r, mesh, w) && note_event_cap(g_pose.c359_world_lines, 200, "card359 client world")) {
        log::line("card359: client guid 0x%llx snap %u bone 0 world (%.1f, %.1f, %.1f) at the freeze",
                  static_cast<unsigned long long>(r.guid), r.snap, w.x, w.y, w.z);
    }
    r.freeze_waits = 0;
    g_orig_c132_all_bodies(mesh, false);
    c485_freeze_repose(r, mesh, off);
    if (r.rx_samples != 0 && note_event_cap(g_pose.c269_lines, kC269StreamLines, "card269 stream")) {
        log::line("card269: client stream guid 0x%llx samples received %u, chained %u, last seq %u at the freeze",
                  static_cast<unsigned long long>(r.guid), r.rx_samples, r.rx_chained, r.stream_seq);
    }
    pose_grav_on(r, mesh); // card 152 r3: the held final's gravity, after its held tick (frozen: it does not fall)
    pose_hold(r, mesh);
    // Card 191 (b23 rows): after the freeze the animation refresh wrote its own pose into the bones every tick (bone 0
    // 0.0 uu off body 0 at the first end tick, 451.7 at the second) and the blend, which skips bodies that do not
    // simulate, never put the bodies back. bNoSkeletonUpdate stops the refresh, so the bones keep the pose the last
    // simulating blend gave them. Set only when it was off; pose_release clears it.
    uint8_t noskel_mask = 0;
    uint8_t* const noskel = pose_noskel_byte(mesh, noskel_mask);
    if (noskel != nullptr && (*noskel & noskel_mask) == 0) {
        *noskel = static_cast<uint8_t>(*noskel | noskel_mask);
        r.noskel_set = true;
    }
    if (r.c191_mesh != nullptr) { // card 191: armed at the apply; the next end ticks are logged
        r.c191_mesh = mesh;
        r.c191_frozen = true;
        r.c191_ticks = 0;
        r.c191_seen = 0;
        g_pose.c191_any = true;
        if (r.arrived && note_event_cap(g_pose.c191_lines, kC191Lines, "card191")) {
            log::line("card191: client t=%llu guid 0x%llx arrival apply: bone 0 %.1f uu from body 0 before the pose, %.1f "
                      "uu after (-1: not measured); end ticks between the apply and the freeze %u",
                      static_cast<unsigned long long>(GetTickCount64()), static_cast<unsigned long long>(r.guid),
                      r.c191_apply_before, r.c191_apply_after, static_cast<unsigned>(r.c191_pre));
        }
    }
    ++g_pose.freezes;
    r.err_freeze = static_cast<float>(pose_far_now(r, mesh).worst);
    r.check_owed = true;
    r.posed_frame = g_pose_frame;
}

// Client, one tick after the freeze: the frozen row's error, logged per row with the two earlier ones (card 140 run 13:
// the apply's own measure, taken before the freeze, read 0.0 while the copy sat 775 uu off).
void pose_freeze_check(PoseRow& r) {
    r.check_owed = false;
    void* mesh = nullptr;
    const PoseFar f = pose_far_now(r, mesh);
    if (g_orig_c191_end_tick != nullptr && r.c191_frozen && r.c191_mesh != nullptr && r.c191_seen == 0 && // hooked
        note_event_cap(g_pose.c191_lines, kC191Lines, "card191")) {
        log::line("card191: client t=%llu guid 0x%llx no end tick since the freeze",
                  static_cast<unsigned long long>(GetTickCount64()), static_cast<unsigned long long>(r.guid));
    }
    g_pose.freeze_err_max = fmax(g_pose.freeze_err_max, f.worst);
    const bool bad = f.worst > kPoseFreezeTolerance;
    g_pose.freeze_bad += bad ? 1u : 0u;
    log::line("%scard140: client pose freeze check guid 0x%llx snap %u: worst body error %.1f uu after the apply, %.1f "
              "after the freeze call, %.1f one tick later (body %d); simulating %d; held %d (update type %u, was %u; "
              "blend physics was %d)",
              bad ? "WARN " : "", static_cast<unsigned long long>(r.guid), r.snap, r.err_apply, r.err_freeze, f.worst,
              f.at, mesh != nullptr && c39_mesh_simulating(mesh) ? 1 : 0, r.held ? 1 : 0,
              mesh != nullptr ? static_cast<const uint8_t*>(mesh)[kKinematicBonesUpdateType] : 255u, r.held_update,
              r.held_blend ? 1 : 0);
    int32_t count = 0;
    void* const* const bodies = mesh != nullptr && r.streamed && r.bone0 != 0 ? pose_bodies(mesh, count) : nullptr;
    if (count > 0 && count == r.bodies) { // card 152: bone 0 on body 0 after the final, labelled by its kind
        pose_bone_line(r, mesh, bodies, (r.flags & kPoseFrozen) != 0       ? "frozen final"
                                        : (r.flags & kPoseUnsettled) != 0 ? "unsettled final"
                                                                          : "settled final");
        r.bone_again = (r.flags & kPoseFrozen) != 0; // card 186: read once more on the next tick
    }
    c251_line(r, mesh, "freeze check");
    if (r.c194_owed) {
        r.c194_owed = false;
        log::line("card194: client re-posed guid 0x%llx snap %u after the game's own start: body error %.1f before, "
                  "%.1f after", static_cast<unsigned long long>(r.guid), r.snap, r.c194_before, f.worst);
    }
    pose_grav_on(r, mesh); // card 152 r3: a settled or unsettled held final, after its held tick and its check
}

// Card 186, client: a frozen final's bone check once more, one tick after the first (capped per map). A bone that
// reaches body 0 now was blended a tick late; one still on the anim pose was overwritten (docs/cards/186).
void pose_bone_again(PoseRow& r) {
    r.bone_again = false;
    void* const mesh = pose_mesh(r);
    int32_t count = 0;
    void* const* const bodies = mesh != nullptr ? pose_bodies(mesh, count) : nullptr;
    if (count == 0 || count != r.bodies) {
        return; // the copy is gone or gibbed: nothing to read
    }
    if (note_event_cap(g_pose.bone_again_lines, kPoseBoneAgainLines, "card186 bone checks one tick later")) {
        pose_bone_line(r, mesh, bodies, "frozen final, one tick later");
        log::line("card186: client frozen final guid 0x%llx snap %u: simulating when the row reached the apply %d, at "
                  "the freeze step %d; card146 game ragdoll starts blocked on this guid %u",
                  static_cast<unsigned long long>(r.guid), r.snap, r.sim_at_final ? 1 : 0, r.sim_at_freeze ? 1 : 0,
                  static_cast<unsigned>(r.game_blocked));
    }
}

// Client, every tick: frozen rows to freeze and freezes to check, each one tick after the step before it.
void pose_freeze_tick() {
    for (int32_t i = 0; i < g_pose.n; ++i) {
        PoseRow& r = g_pose.row[i];
        if (r.posed_frame == g_pose_frame) {
            continue; // posed or frozen in this same tick
        }
        if (r.state == PoseState::Freezing) {
            pose_freeze_step(r);
        } else if (r.state == PoseState::Applied && r.check_owed) {
            pose_freeze_check(r);
        } else if (r.state == PoseState::Applied && r.bone_again) {
            pose_bone_again(r);
        }
    }
}

// Card 152 (a): the name of body 0's bone, from the mesh's physics asset (PhysicsAssetOverride, else the skeletal
// mesh's PhysicsAsset): SkeletalBodySetups[0].BoneName, the setup the mesh's Bodies[0] is made from. 0 when unread.
uint64_t pose_bone0_name(const void* mesh) {
    const uint8_t* prop = nullptr;
    const void* asset = prop_object_field(mesh, "PhysicsAssetOverride", &prop);
    if (asset == nullptr) {
        const void* skel = prop_object_field(mesh, "SkinnedAsset", &prop);
        skel = skel != nullptr ? skel : prop_object_field(mesh, "SkeletalMesh", &prop);
        asset = skel != nullptr ? prop_object_field(skel, "PhysicsAsset", &prop) : nullptr;
    }
    const uint8_t* const setups = asset != nullptr ? prop_find_prop(read_ptr(asset, kObjectClass), "SkeletalBodySetups")
                                                   : nullptr;
    const auto* const arr = setups != nullptr ? static_cast<const uint8_t*>(asset) + read_i32(setups, kPropertyOffset)
                                              : nullptr;
    void* const* const data = arr != nullptr && read_i32(arr, 8) > 0 ? static_cast<void* const*>(read_ptr(arr, 0))
                                                                     : nullptr;
    const void* const setup = data != nullptr ? data[0] : nullptr;
    const uint8_t* const bone = setup != nullptr ? prop_find_prop(read_ptr(setup, kObjectClass), "BoneName") : nullptr;
    return bone != nullptr ? read_u64(setup, static_cast<size_t>(read_i32(bone, kPropertyOffset))) : 0;
}

// Card 152 r2, client: the end-of-physics tick's registration and ShouldBlendPhysicsBones on a copy, the two
// conditions the bones-from-bodies blend runs on (exe+0x41874F0), read for the bone check line.
bool pose_end_tick_registered(const void* mesh) {
    const auto* const data = static_cast<const uint8_t*>(read_ptr(mesh, kEndTickData));
    return data != nullptr && (data[0] & 1) != 0;
}

bool pose_should_blend(void* mesh) {
    using ShouldBlendFn = bool (*)(void* mesh);
    const auto* const vt = static_cast<const uint8_t*>(read_ptr(mesh, 0));
    const auto fn = vt != nullptr ? reinterpret_cast<ShouldBlendFn>(read_ptr(vt, kShouldBlendSlot)) : nullptr;
    return fn != nullptr && fn(mesh);
}

// Card 191, client: a world point in mesh's component space (ComponentToWorld undone: translation, rotation, scale).
Vec3 pose_to_component(const void* mesh, const Vec3& p) {
    const auto* const m = static_cast<const uint8_t*>(mesh);
    double q[4] = {};
    memcpy(q, m + kComponentRotation, sizeof(q));
    const Vec3 at = read_vec3(mesh, kComponentTranslation);
    const Vec3 sc = read_vec3(mesh, kComponentScale);
    const double v[3] = {p.x - at.x, p.y - at.y, p.z - at.z};
    const double u[3] = {-q[0], -q[1], -q[2]}; // the inverse rotation: the conjugate quat
    const double t[3] = {2.0 * (u[1] * v[2] - u[2] * v[1]), 2.0 * (u[2] * v[0] - u[0] * v[2]),
                         2.0 * (u[0] * v[1] - u[1] * v[0])};
    const double r[3] = {v[0] + q[3] * t[0] + (u[1] * t[2] - u[2] * t[1]),
                         v[1] + q[3] * t[1] + (u[2] * t[0] - u[0] * t[2]),
                         v[2] + q[3] * t[2] + (u[0] * t[1] - u[1] * t[0])};
    return Vec3{sc.x != 0.0 ? r[0] / sc.x : r[0], sc.y != 0.0 ? r[1] / sc.y : r[1], sc.z != 0.0 ? r[2] / sc.z : r[2]};
}

// Card 152 (a), client: one bone check line. Bone 0 against body 0 (the bones were refreshed from the bodies set
// last tick), body 0 against the last target set on it (r2: a simulating copy that drifts from its teleport shows
// here), and the copy's collision, end tick registration and blend state.
void pose_bone_line(PoseRow& r, void* mesh, void* const* bodies, const char* what) {
    Xform x{};
    Vec3 bone{};
    if (g_c136_socket_location == nullptr || !pose_world(bodies[0], x)) {
        return;
    }
    g_c136_socket_location(mesh, &bone, r.bone0);
    const Vec3 body{x.t[0], x.t[1], x.t[2]};
    const double d = vec_dist(bone, body);
    const bool live = r.live && r.grav_off;
    const double t = live ? vec_dist(body, r.target0) : 0.0;
    const unsigned collision = g_collision_enabled != nullptr ? g_collision_enabled(mesh) : 255u;
    const double from_mesh = vec_dist(bone, read_vec3(mesh, kComponentTranslation)); // card 186: the anim pose's offset
    ++g_pose.st_bone_checks;
    g_pose.st_bone_max = fmax(g_pose.st_bone_max, d);
    g_pose.st_target_max = fmax(g_pose.st_target_max, t);
    g_pose.st_target_warns += t > kPoseTargetUu ? 1u : 0u;
    const auto* const m = static_cast<const uint8_t*>(mesh);
    const Vec3 at = read_vec3(mesh, kComponentTranslation);
    const Vec3 cs = pose_to_component(mesh, bone);
    const Vec3 sc = read_vec3(mesh, kComponentScale); // card 291: a scale off 1 would explain a constant bone 0 offset
    // Senior 2026-11-19 (c243b273a guid 0x100, 12.9 uu in 8 samples): bone 0 in component space unchanged since the
    // stream start while body 0 moved is a pose not refreshed off-screen, and card 359's scale set on that stale bone
    // is the offset. Plain line then; the target check still WARNs.
    const bool stale = vec_dist(cs, r.bone0_cs) < 0.1 && vec_dist(body, r.hold_root) > 1.0;
    log::line("%scard152: client bone check guid 0x%llx sample %u (%s): bone 0 (%s) at (%.0f, %.0f, %.0f) is %.1f uu "
              "from body 0 and %.1f uu from the mesh component; body 0 is %.1f uu from the last target set%s; body 0 "
              "moved %.0f uu since the stream start; simulating %d, collision %u, end tick registered %d, "
              "ShouldBlendPhysicsBones %d; card191: mesh component at (%.0f, %.0f, %.0f), bone 0 in component space "
              "(%.1f, %.1f, %.1f), at the stream start (%.1f, %.1f, %.1f); VisibilityBasedAnimTickOption %u, +0xA80 bit "
              "0x08 %d, bit 0x10 %d; required bones %d; card291: component scale (%.3f, %.3f, %.3f)%s",
              (d > 1.0 && !stale) || t > kPoseTargetUu ? "WARN " : "", static_cast<unsigned long long>(r.guid), r.stream_seq, what,
              prop_name(reinterpret_cast<const uint8_t*>(&r.bone0), 8).text, bone.x, bone.y, bone.z, d, from_mesh, t,
              live ? "" : " (not streaming)", vec_dist(body, r.hold_root), c39_mesh_simulating(mesh) ? 1 : 0,
              collision, pose_end_tick_registered(mesh) ? 1 : 0, pose_should_blend(mesh) ? 1 : 0, at.x, at.y, at.z,
              cs.x, cs.y, cs.z, r.bone0_cs.x, r.bone0_cs.y, r.bone0_cs.z, static_cast<unsigned>(m[kMeshVisTickOption]),
              (m[kMeshTickFlags] & 0x08) != 0 ? 1 : 0, (m[kMeshTickFlags] & 0x10) != 0 ? 1 : 0,
              read_i32(mesh, kMeshRequiredBones + 8), sc.x, sc.y, sc.z,
              stale && d > 1.0 ? "; stale pose: bone 0 in component space unchanged since the stream start" : "");
}

// Card 152 (a), client: the first kPoseBoneRows streamed copies check once per sample, half a step after it came in
// and before this tick's move.
void pose_bone_check(PoseRow& r, void* mesh, void* const* bodies, ULONGLONG now) {
    if (r.bone_lines == 0 || r.bone_seq == r.stream_seq || now - r.cur_at < kPoseStepMs / 2) {
        return;
    }
    r.bone_seq = r.stream_seq;
    --r.bone_lines;
    pose_bone_line(r, mesh, bodies, "live");
}

// Card 152, client: a streamed copy's bodies teleported toward the current sample, a fraction a of the way from the
// previous one (positions linear, rotations normalized linear on the shorter arc), with the copy's own scale. The
// copy simulates (gravity off): each teleport wakes its body, and the velocities are zeroed after (pose_stream_tick).
void pose_stream_move(PoseRow& r, void* const* bodies, double a) {
    for (int32_t i = 0; i < r.bodies; ++i) {
        double q0[4], q1[4];
        Xform x{};
        if (!pose_quat_from(r.blob, i, q1) || !pose_world(bodies[i], x)) {
            continue; // gibbed on the host or here
        }
        const bool from = pose_quat_from(r.prev, i, q0);
        const double dot = q0[0] * q1[0] + q0[1] * q1[1] + q0[2] * q1[2] + q0[3] * q1[3];
        const double sign = dot < 0.0 ? -1.0 : 1.0;
        double len = 0.0;
        for (int32_t k = 0; k < 4; ++k) {
            x.q[k] = from ? q0[k] + (sign * q1[k] - q0[k]) * a : q1[k];
            len += x.q[k] * x.q[k];
        }
        len = len > 0.0 ? sqrt(len) : 1.0;
        for (int32_t k = 0; k < 4; ++k) {
            x.q[k] /= len;
        }
        const Vec3 p0 = pose_want_from(r.prev, i), p1 = pose_want_from(r.blob, i);
        x.t[0] = p0.x + (p1.x - p0.x) * a;
        x.t[1] = p0.y + (p1.y - p0.y) * a;
        x.t[2] = p0.z + (p1.z - p0.z) * a;
        g_pose_body_set(bodies[i], &x, kTeleportPhysics, true);
        if (i == 0) {
            r.target0 = Vec3{x.t[0], x.t[1], x.t[2]};
        }
    }
}

// Card 523 r2, client, every tick of an own fall: the copy died where the client drew it, on average 150 uu from the
// host's body (c263k: WarriorBug xy median 148 uu, HopperBug up to 2680 uu high). Every body moves by the next part of
// own_off, linear over kPoseStepMs, with a physics teleport that keeps the bodies' velocities, so the copy keeps
// falling on its own while it closes on sample 0's place; the hand-over at sample 1 then meets only the fall's own
// difference. Rotations stay the copy's own.
void pose_own_shift(PoseRow& r, void* const* bodies, ULONGLONG now) {
    const double f = fmin(1.0, static_cast<double>(now - r.own_at) / static_cast<double>(kPoseStepMs));
    const Vec3 d{r.own_off.x * f - r.own_done.x, r.own_off.y * f - r.own_done.y, r.own_off.z * f - r.own_done.z};
    if (d.x == 0.0 && d.y == 0.0 && d.z == 0.0) {
        return;
    }
    for (int32_t i = 0; i < r.bodies; ++i) {
        Xform x{};
        if (!pose_world(bodies[i], x)) {
            continue; // gibbed here
        }
        x.t[0] += d.x;
        x.t[1] += d.y;
        x.t[2] += d.z;
        g_pose_body_set(bodies[i], &x, kTeleportPhysics, true);
    }
    r.own_done = Vec3{r.own_off.x * f, r.own_off.y * f, r.own_off.z * f};
}

// Card 152, client, every tick: a streamed row not posed yet is tried again (its copy's death edge may run on any
// tick), and each streaming row is checked (pose_bone_check), teleported (pose_stream_move), its velocities zeroed
// and its gravity held off. Gravity is set again each tick because the game's Wake Ragdoll turns it back on after
// its SetAllBodiesSimulatePhysics(1) (BP_AI_AttackSystemEnemy_Base ubergraph 1313 then 1396); the re-sets are not
// counted as pose_grav_off calls.
void pose_stream_tick(ULONGLONG now) {
    const Vec3 zero{0.0, 0.0, 0.0};
    for (int32_t i = 0; i < g_pose.n; ++i) {
        PoseRow& r = g_pose.row[i];
        if (r.streamed && r.state == PoseState::Pending && r.blob_n != 0) {
            pose_try_apply(r);
            continue;
        }
        const bool own = r.own_fall && r.live && r.state == PoseState::Applied && r.posed_frame != g_pose_frame;
        if (!own && (!r.grav_off || !r.live || r.state != PoseState::Applied || r.posed_frame == g_pose_frame)) {
            continue; // card 152 r3: a held final (live false) is not moved
        }
        void* const mesh = pose_mesh(r);
        int32_t count = 0;
        void* const* const bodies = mesh != nullptr ? pose_bodies(mesh, count) : nullptr;
        if (count == 0 || count != r.bodies) {
            continue;
        }
        if (own) {
            pose_own_shift(r, bodies, now); // card 523 r2: gravity stays on, no velocity zeroing
            continue;
        }
        pose_bone_check(r, mesh, bodies, now);
        const double a = static_cast<double>(now - r.cur_at) / static_cast<double>(kPoseStepMs);
        pose_stream_move(r, bodies, a < 1.0 ? a : 1.0);
        g_pose_mesh_linear(mesh, &zero, false);
        g_pose_mesh_angular(mesh, &zero, false);
        g_pose_mesh_gravity(mesh, false);
    }
}

// Card 152, client: a streamed copy's stream start. Its game start's wait ends, and the first kPoseBoneRows copies
// are picked for the (a) bone check.
void pose_stream_first(PoseRow& r, void* npc, void* mesh, void* const* bodies, bool already) {
    wait_clear(r.guid, "the stream start", r.got);
    ++g_pose.st_holds;
    Xform x{};
    r.hold_root = pose_world(bodies[0], x) ? Vec3{x.t[0], x.t[1], x.t[2]} : Vec3{};
    r.target0 = r.hold_root;
    if (g_pose.bone_rows < kPoseBoneRows && r.bone0 == 0) {
        ++g_pose.bone_rows;
        r.bone0 = pose_bone0_name(mesh);
        r.bone_lines = r.bone0 != 0 ? kPoseBoneLines : 0;
        r.bone_seq = 0xFFFF;
        Vec3 bone{};
        if (r.bone0 != 0 && g_c136_socket_location != nullptr) { // card 191: the anim pose before the stream moves it
            g_c136_socket_location(mesh, &bone, r.bone0);
            r.bone0_cs = pose_to_component(mesh, bone);
        }
        log::line("%scard152: client bone check picked guid 0x%llx: body 0 bone %s", r.bone0 != 0 ? "" : "WARN ",
                  static_cast<unsigned long long>(r.guid),
                  r.bone0 != 0 ? prop_name(reinterpret_cast<const uint8_t*>(&r.bone0), 8).text : "not read");
    }
    if (note_event_cap(g_pose.stream_lines, kPoseStreamLines, "card152 stream")) {
        log::line("card152: client stream start %s guid 0x%llx at sample %u (%s): simulating, %s, collision "
                  "%u%s", object_name(npc).text, static_cast<unsigned long long>(r.guid), static_cast<unsigned>(r.stream_seq),
                  (r.flags & kPoseUnfreeze) != 0 ? "unfreeze" : "live",
                  r.grav_off ? "gravity off" : "gravity on (card 523 own fall)", g_collision_enabled != nullptr ? g_collision_enabled(mesh) : 255u,
                  already ? "; it was already simulating" : "; ragdoll started for it");
    }
}

// Card 152, client: a streamed copy's final row (settled, frozen or unsettled), posed by the snap path. Card 186: a
// frozen one is held for one tick, simulating with gravity off (still off from the stream), so the end-physics tick
// blends the posed bodies into the bones; it is frozen on the next tick (pose_freeze_step), which turns gravity on,
// and checked one tick after. Card 334: a settled final is held the same way. An unsettled final is not held: its
// gravity came on before the apply (pose_try_apply), so it keeps falling as the host's does; it is checked on the next tick
// (pose_freeze_check, check_owed). Card 186 r2: nothing here touches the bodies. The apply ends in their sleep, and
// a velocity or gravity set after it woke them (c186-r1: 2.1 to 25.5 uu one tick later).
void pose_stream_final(PoseRow& r, const PoseErr& e, bool hold) {
    r.err_freeze = static_cast<float>(e.err);
    r.check_owed = !hold;
    ++g_pose.st_finals;
    if (note_event_cap(g_pose.stream_lines, kPoseStreamLines, "card152 stream")) {
        log::line("card152: client stream final guid 0x%llx snap %u (%s): posed by the snap path, %s (gravity %s); "
                  "%.0f uu from the client's before, largest body error %.1f uu",
                  static_cast<unsigned long long>(r.guid), r.snap,
                  (r.flags & kPoseFrozen) != 0 ? "frozen" : (r.flags & kPoseUnsettled) != 0 ? "unsettled" : "settled",
                  hold ? "held one tick" : "not held, gravity on before the pose", r.grav_off ? "off" : "on",
                  e.before, e.err);
    }
}

// Card 152 r2, client: a streamed copy's start. It simulates through the card 039/132 ragdoll start (the one
// pose_try_apply uses) with physics collision, then its gravity goes off. A frozen copy (card 140 hold) unfreezing
// is released first. Null once streaming, else why not (the row stays pending).
const char* pose_stream_start(PoseRow& r, void* npc, void* mesh, void* const* bodies, bool own) {
    const char* const no_physics = pose_collision_reason(npc, mesh);
    if (no_physics != nullptr) {
        return no_physics;
    }
    pose_release(r, mesh);
    const bool already = c39_mesh_simulating(mesh);
    if (!already) {
        detour_c132_set_simulate(mesh, true);
    }
    if (!c39_mesh_simulating(mesh)) {
        if (!r.sim_warned) {
            r.sim_warned = true;
            ++g_pose.st_no_sim;
            log::line("WARN card152: client stream start guid 0x%llx: copy would not simulate, collision %u",
                      static_cast<unsigned long long>(r.guid),
                      g_collision_enabled != nullptr ? g_collision_enabled(mesh) : 255u);
        }
        return "copy would not simulate";
    }
    g_pose.st_stopped += already ? 1u : 0u;
    if (!own) {
        pose_grav_off(r, mesh);
    }
    pose_stream_first(r, npc, mesh, bodies, already);
    return nullptr;
}

// Card 523, client: the next sample on a copy falling on its own since sample 0. Its bodies' current transforms become
// the move's start (r.prev), its gravity goes off and its velocities are zeroed, and pose_stream_tick moves it from
// there to this sample over kPoseStepMs, so it neither hangs at sample 0 nor jumps. A copy whose bodies cannot be read
// in the row's form (a body gone, or one too far from the root) is teleported to the sample as before.
void pose_stream_handover(PoseRow& r, void* npc, void* mesh, void* const* bodies) {
    r.own_fall = false;
    size_t n = 0;
    Vec3 root{};
    const bool read = pose_encode_to(mesh, bodies, r.bodies, r.prev, n, root) && n == r.blob_n;
    pose_grav_off(r, mesh);
    if (read) {
        const Vec3 zero{0.0, 0.0, 0.0};
        g_pose_mesh_linear(mesh, &zero, false);
        g_pose_mesh_angular(mesh, &zero, false);
        const double d = vec_dist(pose_want_from(r.prev, 0), pose_want_from(r.blob, 0));
        ++g_pose.c523_handovers;
        g_pose.c523_hand_sum += d;
        g_pose.c523_hand_max = fmax(g_pose.c523_hand_max, d);
        if (g_pose.c524_hand_rows < kC524Rows) { // card 524: the hand-over distance per guid, split xy and z
            ++g_pose.c524_hand_rows;
            const Vec3 a = pose_want_from(r.prev, 0), b = pose_want_from(r.blob, 0);
            log::line("card524: client hand-over %s guid 0x%llx: body 0 off sample %u xy %.1f uu, z %+.1f uu (host "
                      "minus client), %llu ms after the own-fall start",
                      object_name(npc).text, static_cast<unsigned long long>(r.guid), static_cast<unsigned>(r.stream_seq),
                      sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y)), b.z - a.z,
                      static_cast<unsigned long long>(GetTickCount64() - r.own_at));
        }
    } else {
        memcpy(r.prev, r.blob, r.blob_n);
        pose_apply_bodies(mesh, bodies, r);
        ++g_pose.c523_teleports;
    }
    r.target0 = pose_want_from(r.blob, 0);
    r.live = true;
    r.state = PoseState::Applied;
    r.posed_frame = g_pose_frame;
}

// Card 152, client: a stream sample or an unfreeze row on a streamed guid. The copy's stream starts on the first
// (pose_stream_start), the bodies are set to the row, and pose_stream_tick moves them each tick from then on. Null
// once posed, else why not.
const char* pose_stream_apply(PoseRow& r, void* npc, void* mesh, void* const* bodies) {
    if (!c38_is_marked(npc)) {
        return "its death has not run";
    }
    if (c31_weak_get(r.actor) != npc) {
        pose_grav_on(r, nullptr); // the old copy is gone (back in relevancy): the new one starts its own stream
        r.held = false;
        r.own_fall = false;
        c31_weak_set(r.actor, npc);
    }
    PoseArrival* const arrival = arrival_find(npc);
    if (arrival != nullptr) {
        r.arrived = true; // card 251
        arrival_show(*arrival, mesh);
    }
    if (r.own_fall) {
        // Card 523 r3 (c263l): the host sends an unfreeze snap with sample 0 (same ms, flags 5); it is the same
        // moment, so the own fall goes on. Only the next stream sample hands over.
        if ((r.flags & kPoseStream) == 0) {
            ++g_pose.c523_unfreeze_kept;
            r.state = PoseState::Applied;
            r.posed_frame = g_pose_frame;
            return nullptr;
        }
        pose_stream_handover(r, npc, mesh, bodies);
        return nullptr;
    }
    if (!r.grav_off) {
        // Card 523: a live sample 0 on a copy that is on screen (not held hidden as a dead arrival) starts the copy's
        // ragdoll from its own animation pose with gravity on, as the host's started; the next sample takes it over.
        const bool own = (r.flags & kPoseUnfreeze) == 0 && r.stream_seq == 0 && arrival == nullptr;
        const char* const why = pose_stream_start(r, npc, mesh, bodies, own);
        if (why != nullptr) {
            return why;
        }
        if (own) {
            const Vec3 want = pose_want_from(r.blob, 0);
            const bool read = r.hold_root.x != 0.0 || r.hold_root.y != 0.0 || r.hold_root.z != 0.0; // body 0 read
            r.own_off = read ? Vec3{want.x - r.hold_root.x, want.y - r.hold_root.y, want.z - r.hold_root.z} : Vec3{};
            r.own_done = Vec3{};
            const double off = read ? vec_dist(want, r.hold_root) : 0.0;
            g_pose.c523_off_sum += off;
            g_pose.c523_off_max = fmax(g_pose.c523_off_max, off);
            if (read && g_pose.c524_rows < kC524Rows) { // card 524: body 0 to body 0, split xy and z, per guid
                ++g_pose.c524_rows;
                const double dx = want.x - r.hold_root.x, dy = want.y - r.hold_root.y;
                // The copy's actor z against its ReplicatedMovement.Location z (the host's last sent location): a
                // dead copy that stayed up while the host's capsule fell shows either a low replicated z the client
                // did not follow, or a replicated z as high as the actor (the host stopped sending).
                Vec3 rep{};
                const bool rep_ok = c132_rep_location(npc, &rep);
                const Vec3 at = c74_actor_loc(npc);
                log::line("card524: client own-fall start %s guid 0x%llx: body 0 off sample 0 xy %.1f uu, z %+.1f uu "
                          "(host minus client), applied %llu ms after sample 0 came; actor z %.0f, replicated z %s%.0f",
                          object_name(npc).text, static_cast<unsigned long long>(r.guid), sqrt(dx * dx + dy * dy),
                          want.z - r.hold_root.z, static_cast<unsigned long long>(GetTickCount64() - r.got), at.z,
                          rep_ok ? "" : "unread ", rep.z);
            }
            r.own_fall = true;
            r.live = true;
            r.state = PoseState::Applied;
            r.applied_at = GetTickCount64();
            r.own_at = r.applied_at;
            r.posed_frame = g_pose_frame;
            ++g_pose.c523_falls;
            return nullptr;
        }
    }
    r.live = true;
    const PoseErr e = pose_apply_bodies(mesh, bodies, r);
    r.target0 = pose_want_from(r.blob, 0); // card 152 r3: body 0's target is the sample just applied
    memcpy(r.prev, r.blob, r.blob_n); // the next sample moves from here
    r.state = PoseState::Applied;
    r.applied_at = GetTickCount64();
    r.posed_frame = g_pose_frame;
    r.drift_logged = false;
    ++g_pose.applied;
    arrival_posed(arrival, npc, r, e, false);
    if ((r.flags & kPoseUnfreeze) != 0) { // logged uncapped, as the snap path logs each unfreeze
        ++g_pose.unfreezes;
        ++g_pose.st_unfreezes;
        log::line("card152: client pose unfreeze guid 0x%llx snap %u continues the stream: simulating %d",
                  static_cast<unsigned long long>(r.guid), r.snap, c39_mesh_simulating(mesh) ? 1 : 0);
    }
    return nullptr;
}

// Card 152, client, every tick: a copy whose game ragdoll start has waited kPoseStartWaitMs with no stream row
// starts its own ragdoll (through the card 039/132 start, which the gate lets through), with a WARN naming its class.
void pose_wait_tick(ULONGLONG now) {
    void* const pc = c31_weak_get(g_pose_pc);
    for (int32_t i = 0; i < g_wait.n; ++i) {
        PoseWait& w = g_wait.w[i];
        if (w.started || now - w.at < kPoseStartWaitMs) {
            continue;
        }
        w.started = true;
        ++g_wait.fallbacks;
        void* const npc = pc != nullptr ? c41_resolve_guid(pc, w.guid) : nullptr;
        void* const mesh = npc != nullptr ? read_ptr(npc, kCharacterMesh) : nullptr;
        PoseRow* const row = pose_find(w.guid);
        if (row != nullptr && mesh != nullptr) {
            pose_grav_on(*row, mesh); // card 152 r2: a streaming copy gets its gravity back before its own start
        }
        const bool rest = rest_find(w.guid, nullptr) != nullptr; // card 207: the rest row came during the wait
        if (mesh != nullptr && !rest) {
            detour_c132_set_simulate(mesh, true);
        }
        g_wait.unresolved += mesh != nullptr ? 0u : 1u;
        if (note_event_cap(g_pose.warn_lines, kPoseWarnLines, "card132 pose warn")) {
            // card 207 step 3: a rest row holding the copy is the expected path, not a warning
            log::line("%scard152: client no stream row for guid 0x%llx (%s) %llu ms after its game ragdoll start was "
                      "held: %s", mesh != nullptr && rest ? "" : "WARN ", static_cast<unsigned long long>(w.guid),
                      npc != nullptr ? object_name(read_ptr(npc, kObjectClass)).text : "unresolved",
                      static_cast<unsigned long long>(now - w.at),
                      mesh == nullptr ? "copy gone, nothing started"
                                      : rest ? "a rest row holds it, nothing started" : "its own ragdoll started");
        }
    }
}

void pose_client_step(ULONGLONG now) {
    for (int32_t i = g_pose.n - 1; i >= 0; --i) {
        PoseRow& r = g_pose.row[i];
        if (r.state == PoseState::Freezing) {
            continue; // pose_freeze_tick, every tick
        }
        if (r.state == PoseState::Applied) {
            pose_drift_note(r, now);
            c485_render_note(r, now);
            continue;
        }
        if (r.state != PoseState::Pending) {
            continue;
        }
        const char* const why = pose_try_apply(r);
        // A guid not mapped yet is a copy this client does not have (hb47 0x14c, 0x1ec: the journal replay after a
        // reconnect, for corpses not yet relevant to the new connection). It waits, with no deadline, until the copy
        // arrives or the host's R row removes it; only a copy that is here and never gets ready is given up on.
        const bool unmapped = why != nullptr && strcmp(why, "guid unmapped") == 0;
        if (unmapped) {
            // Card 466 (c460b170a 0xb4, 0x8c): the give-up counts from the copy's arrival, not the row's. A copy
            // back in relevancy over kPosePendingMs after its row lost the row on its first tick, before its death ran.
            r.got = now;
        }
        if (why != nullptr && !unmapped && now - r.got >= kPosePendingMs) {
            ++g_pose.pending_give_up;
            ++g_pose.gave_up_round;
            if (note_event_cap(g_pose.warn_lines, kPoseWarnLines, "card132 pose warn")) {
                log::line("WARN card132: client pose guid 0x%llx snap %u not applied after %llu ms: %s",
                          static_cast<unsigned long long>(r.guid), r.snap,
                          static_cast<unsigned long long>(now - r.got), why);
            }
            pose_remove(i);
        }
    }
}

// Client: apply the row for an arrival's guid. A row already applied is from a copy that left relevancy and came back
// (same NetGUID, new copy): its held pose is applied again. Null once applied, else the reason.
const char* arrival_apply(uint64_t guid) {
    PoseRow* const r = pose_find(guid);
    if (r == nullptr) {
        return "no host pose row yet";
    }
    if (r->state == PoseState::Applied || r->state == PoseState::Freezing) {
        r->state = PoseState::Pending;
        r->got = GetTickCount64(); // the kPosePendingMs give-up counts from here
    }
    return r->state == PoseState::Pending && r->blob_n != 0 ? pose_try_apply(*r) : "row not complete";
}

struct RestRow;                                                        // card 269 (below the sprite rows)
RestRow* rest_find(uint64_t guid, const void* actor);
void rest_arrival(const PoseArrival& a, void* npc, RestRow& r, ULONGLONG now);

// Client, per tick: each corpse held hidden. A complete row for it is applied at once (pose_try_apply shows the mesh);
// one gone is dropped; one with no applied row after kArrivalWaitMs is shown and plays the old death visual, a WARN.
void arrival_tick(ULONGLONG now) {
    for (int32_t i = g_arrive.n - 1; i >= 0; --i) {
        PoseArrival& a = g_arrive.a[i];
        void* const npc = c31_weak_get(a.actor);
        if (npc == nullptr) {
            ++g_arrive.gone;
            arrival_remove(&a);
            continue;
        }
        a.guid = a.guid != 0 ? a.guid : c41_guid(npc);
        const char* const why = arrival_apply(a.guid); // removes the arrival when applied
        if (why == nullptr) {
            continue;
        }
        RestRow* const rest = a.guid != 0 ? rest_find(a.guid, nullptr) : nullptr; // card 269: no wait
        if (rest != nullptr) {
            rest_arrival(a, npc, *rest, now);
            arrival_remove(&a);
            continue;
        }
        ++a.hidden_ticks;
        const ULONGLONG open = c120_pong_open_ms();
        const ULONGLONG from = open > a.first ? open : a.first;
        const bool held = open == 0 && now - a.first < kArrivalHoldMaxMs;
        if (held || (open != 0 && now - from < kArrivalWaitMs)) {
            continue;
        }
        void* const mesh = read_ptr(npc, kCharacterMesh);
        arrival_show(a, mesh);
        c38_corpse_death_play(npc);
        ++g_arrive.death_plays;
        log::line("WARN card137: client corpse arrival %s guid 0x%llx not posed after %llu ms, %llu ms after the first "
                  "Pong (%s; Pong at %llu): shown after %u hidden ticks, the death visual played",
                  object_name(npc).text, static_cast<unsigned long long>(a.guid),
                  static_cast<unsigned long long>(now - a.first),
                  static_cast<unsigned long long>(open != 0 ? now - from : 0), why,
                  static_cast<unsigned long long>(open), a.hidden_ticks);
        arrival_remove(&a);
    }
}

// Card 152, client: a stream sample (one frame). A streaming copy (simulating, gravity off) moves from the sample
// before it; a copy not streaming yet is posed by pose_try_apply.
// Client (card 521 step 2): a delta sample's base is the row's last decoded sample, one seq back, same body count.
// False (counted, a capped row) when it is not; the row then waits for the next whole sample.
bool pose_delta_base(const PoseRow& r, unsigned seq, int bodies) {
    const size_t want = kPoseHeadBytes + static_cast<size_t>(bodies) * kPoseBodyBytes;
    const bool ok = r.stream_base_n == want && static_cast<uint16_t>(r.stream_seq + 1) == static_cast<uint16_t>(seq);
    if (!ok) {
        ++g_pose.c521_misses;
        if (note_event_cap(g_pose.c521_lines, kPoseDeltaLines, "card521 delta")) {
            log::line("card521: client delta miss guid 0x%llx sample %u: base sample %u, base %zu bytes for %d bodies",
                      static_cast<unsigned long long>(r.guid), seq, r.stream_seq, r.stream_base_n, bodies);
        }
    }
    return ok;
}

C132Result pose_stream_part(void* pc, PoseRow& r, unsigned seq, int bodies, unsigned flags, const char* b,
                            const char*& reason) {
    static uint8_t blob[kPoseBlob]; // game thread
    static uint8_t wire[kPoseBlob];
    const bool delta = (flags & kPoseDelta) != 0;
    if (delta && !pose_delta_base(r, seq, bodies)) {
        reason = "delta miss";
        return C132Result::Skipped;
    }
    const size_t wn = b64_decode(b, strlen(b), wire, sizeof(wire));
    size_t n = 0;
    if (delta ? !pose_delta_unpack(r.stream_base, wire, wn, bodies, blob, n)
              : !pose_wire_unpack(wire, wn, bodies, blob, n)) {
        ++g_pose.bad;
        reason = "bad payload";
        return C132Result::Refused;
    }
    memcpy(r.stream_base, blob, n);
    r.stream_base_n = n;
    g_pose.c521_rx_deltas += delta ? 1u : 0u;
    const bool chain = r.grav_off && r.state == PoseState::Applied && r.bodies == bodies && r.blob_n == n;
    if (chain) { // card 523: the gap since the sample before, while the copy sat at that sample
        const ULONGLONG gap = GetTickCount64() - r.cur_at;
        g_pose.c523_late += gap > kPoseLateMs ? 1u : 0u;
        g_pose.c523_gap_max = gap > g_pose.c523_gap_max ? static_cast<uint32_t>(gap) : g_pose.c523_gap_max;
    }
    ++r.rx_samples;                         // card 269 B
    r.rx_chained += chain ? 1u : 0u;
    memcpy(r.prev, chain ? r.blob : blob, n);
    memcpy(r.blob, blob, n);
    float head[3];
    memcpy(head, blob, sizeof(head));
    r.sent_root = {head[0], head[1], head[2]};
    r.blob_n = n;
    r.bodies = bodies;
    r.flags = kPoseStream;
    r.stream_seq = static_cast<uint16_t>(seq);
    r.got = r.cur_at = GetTickCount64();
    r.streamed = true;
    r.live = true;
    r.b64_n = 0;
    r.parts_got = 0;
    c31_weak_set(r.pc, pc);
    ++g_pose.st_rx;
    if (chain) {
        reason = nullptr; // pose_stream_tick moves the streaming copy
        return C132Result::Applied;
    }
    r.state = PoseState::Pending;
    reason = pose_try_apply(r);
    return reason == nullptr ? C132Result::Applied : C132Result::Pending;
}

// Client: one part of a snapshot (card 186: split from pose_part). The parts are assembled in the row's b64 buffer;
// the last one decodes the snapshot and tries the apply.
C132Result pose_snap_part(void* pc, PoseRow& r, unsigned snap, int part, int parts, int bodies, unsigned flags,
                          const char* b, const char*& reason) {
    if (part == 0) { // a new snapshot replaces whatever the row held
        r.b64_n = 0;
        r.parts_got = 0;
        r.parts = parts;
        r.snap = static_cast<uint16_t>(snap);
    }
    const size_t len = strlen(b);
    if (part != r.parts_got || parts != r.parts || r.b64_n + len > kPoseB64) {
        reason = "part out of order";
        return C132Result::Refused;
    }
    memcpy(r.b64 + r.b64_n, b, len);
    r.b64_n += len;
    if (++r.parts_got < parts) {
        reason = "part buffered";
        return C132Result::Applied;
    }
    static uint8_t wire[kPoseBlob]; // game thread
    r.blob_n = 0;
    if (!pose_wire_unpack(wire, b64_decode(r.b64, r.b64_n, wire, sizeof(wire)), bodies, r.blob, r.blob_n)) {
        ++g_pose.bad;
        reason = "bad payload";
        return C132Result::Refused;
    }
    float head[3];
    memcpy(head, r.blob, sizeof(head));
    r.sent_root = {head[0], head[1], head[2]};
    r.bodies = bodies;
    r.flags = static_cast<uint8_t>(flags);
    r.got = GetTickCount64();
    r.state = PoseState::Pending;
    c31_weak_set(r.pc, pc);
    ++g_pose.received;
    reason = pose_try_apply(r);
    return reason == nullptr ? C132Result::Applied : C132Result::Pending; // retried by c132_pose_step
}

// Client: a "P" part. Returns the ledger result's reason.
C132Result pose_part(void* pc, const char* text, const char*& reason) {
    unsigned long long guid = 0;
    unsigned snap = 0, flags = 0;
    int part = 0, parts = 0, bodies = 0, at = 0;
    const int got = sscanf_s(text, "P %llx %x %d %d %d %u %n", &guid, &snap, &part, &parts, &bodies, &flags, &at);
    if (got != 6 || guid == 0 || parts <= 0 || part < 0 || part >= parts || bodies <= 0 || bodies > kPoseBodies) {
        reason = "bad head";
        return C132Result::Refused;
    }
    PoseRow* r = pose_find(guid);
    if (r == nullptr && (flags & kPoseDelta) != 0) { // card 521: a delta with no row has no base; make no empty row
        ++g_pose.c521_misses;
        reason = "delta miss";
        return C132Result::Skipped;
    }
    if (r == nullptr && g_pose.n >= kPoseRows) {
        ++g_pose.dropped;
        reason = "table full";
        return C132Result::Refused;
    }
    if (r == nullptr) {
        r = &g_pose.row[g_pose.n++];
        *r = PoseRow{};
        r->guid = guid;
    }
    if ((flags & kPoseStream) != 0) { // a stream sample is always one frame (kPoseB64 fits in kPoseChunk)
        if (parts != 1) {
            ++g_pose.bad;
            reason = "stream sample in parts";
            return C132Result::Refused;
        }
        return pose_stream_part(pc, *r, snap, bodies, flags, text + at, reason);
    }
    return pose_snap_part(pc, *r, snap, part, parts, bodies, flags, text + at, reason);
}

} // namespace

// Card 191 diagnostic (client, log only): the end-of-physics ticks of a held final's mesh, armed at the apply. Before
// pose_freeze_step they are counted; after it, the first kC191Ticks log the ExecuteTick conditions and bone 0 in
// component space with its distance to body 0 before and after the original (the bones-from-bodies blend). Capped
// kC191Lines rows per map. The end-of-physics tick runs on the game thread.
C191EndTickFn g_orig_c191_end_tick = nullptr;
SetRelScaleFn g_set_rel_scale = nullptr; // card 359: USceneComponent::SetRelativeScale3D
AddWorldOffsetFn g_add_world_offset = nullptr; // card 359 b191: USceneComponent::AddWorldOffset

namespace {

C191Bone c191_bone(const PoseRow& r, void* mesh) {
    int32_t count = 0;
    void* const* const bodies = pose_bodies(mesh, count);
    Xform x{};
    Vec3 bone{};
    const uint64_t name = r.bone0 != 0 ? r.bone0 : pose_bone0_name(mesh); // not stored: r.bone0 gates card152 rows
    if (bodies == nullptr || count == 0 || name == 0 || g_c136_socket_location == nullptr || !pose_world(bodies[0], x)) {
        return C191Bone{};
    }
    g_c136_socket_location(mesh, &bone, name);
    const Vec3 body0{x.t[0], x.t[1], x.t[2]};
    return C191Bone{pose_to_component(mesh, bone), bone, body0, vec_dist(bone, body0), true};
}

// Card 485 b209: does a reposed copy's mesh follow its bodies once its game renders it? The freeze ran with the bones
// still on the animation pose (c244b207b: bone 0 component space unchanged over 11 end ticks, "rendered in 0.2 s 0"),
// and the hold sets bNoSkeletonUpdate, so the bones may keep that pose for good. Logged once per repose, on the first
// client step that finds the copy rendered in the last 0.3 s: bone 0 off body 0 now, and bone 0 in component space
// now against the repose. Moved with bone 0 on body 0 = the mesh blends once rendered; unchanged = the mesh is stale.
constexpr uint32_t kC485RenderLines = 400; // per map
void c485_render_note(PoseRow& r, ULONGLONG now) {
    if (r.c485_at == 0 || g_actor_recently_rendered == nullptr) {
        return;
    }
    void* const npc = c31_weak_get(r.actor);
    void* const mesh = npc != nullptr ? read_ptr(npc, kCharacterMesh) : nullptr;
    if (mesh == nullptr) {
        r.c485_at = 0; // the copy is gone before it was seen
        return;
    }
    if (!g_actor_recently_rendered(npc, 0.3f)) { // 0.3 s: the client step runs every kPoseStepMs (250)
        return;
    }
    const ULONGLONG at = r.c485_at;
    r.c485_at = 0;
    const C191Bone b = c191_bone(r, mesh);
    if (!note_event_cap(g_pose.c485_render_lines, kC485RenderLines, "card485 first rendered")) {
        return;
    }
    const Vec3 was = r.c485_cs;
    log::line("card485: client guid 0x%llx first rendered %llu ms after its repose: bone 0 %.1f uu off body 0; bone 0 "
              "component space (%.1f, %.1f, %.1f), at the repose (%.1f, %.1f, %.1f), %.1f uu apart; bone read %d; "
              "body 0 at (%.0f, %.0f, %.0f)",
              static_cast<unsigned long long>(r.guid), static_cast<unsigned long long>(now - at), b.ok ? b.d : -1.0,
              b.cs.x, b.cs.y, b.cs.z, was.x, was.y, was.z, b.ok ? vec_dist(b.cs, was) : -1.0, b.ok ? 1 : 0, b.b0.x,
              b.b0.y, b.b0.z);
}

// Card 359 (b187): the bone 0 and component snapshot for the frozen row (ok false: bone 0 or body 0 not read).
C359Snap c359_snap(const PoseRow& r, void* mesh) {
    const C191Bone b = mesh != nullptr ? c191_bone(r, mesh) : C191Bone{};
    if (!b.ok) {
        return C359Snap{};
    }
    const auto* const m = static_cast<const uint8_t*>(mesh);
    double q[4] = {};
    memcpy(q, m + kComponentRotation, sizeof(q));
    const Vec3 at = read_vec3(mesh, kComponentTranslation);
    const Vec3 sc = read_vec3(mesh, kComponentScale);
    void* const npc = c31_weak_get(r.actor);
    const int8_t rendered =
        npc == nullptr || g_actor_recently_rendered == nullptr ? -1 : (g_actor_recently_rendered(npc, 0.2f) ? 1 : 0);
    return C359Snap{{static_cast<float>(b.cs.x), static_cast<float>(b.cs.y), static_cast<float>(b.cs.z)},
                    {static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z)},
                    {static_cast<float>(q[0]), static_cast<float>(q[1]), static_cast<float>(q[2]),
                     static_cast<float>(q[3])},
                    {static_cast<float>(sc.x), static_cast<float>(sc.y), static_cast<float>(sc.z)},
                    rendered,
                    true};
}

void c359_snap_part(char* out, size_t cap, const char* when, const C359Snap& s) {
    if (!s.ok) {
        snprintf(out, cap, "; card359 %s: not read", when);
        return;
    }
    snprintf(out, cap, "; card359 %s: bone 0 component space (%.1f, %.1f, %.1f), component at (%.1f, %.1f, %.1f) "
             "quat (%.4f, %.4f, %.4f, %.4f) scale (%.3f, %.3f, %.3f), rendered in 0.2 s %d", when, s.cs[0], s.cs[1],
             s.cs[2], s.at[0], s.at[1], s.at[2], s.q[0], s.q[1], s.q[2], s.q[3], s.sc[0], s.sc[1], s.sc[2],
             static_cast<int>(s.rendered));
}

C359SnapText c359_snap_text(const PoseRow& r) {
    C359SnapText t{};
    char apply[280];
    char last[280];
    c359_snap_part(apply, sizeof(apply), "at the apply", r.c359_apply);
    c359_snap_part(last, sizeof(last), "at the last waited tick", r.c359_last);
    snprintf(t.text, sizeof(t.text), "%s%s", apply, last);
    return t;
}

// The armed row whose mesh this tick is for; clears c191_any when no row is armed.
PoseRow* c191_row(const void* mesh) {
    bool any = false;
    for (int32_t i = 0; i < g_pose.n; ++i) {
        PoseRow& r = g_pose.row[i];
        any = any || r.c191_mesh != nullptr;
        if (r.c191_mesh == mesh && pose_mesh(r) == mesh) {
            return &r;
        }
    }
    g_pose.c191_any = any;
    return nullptr;
}

} // namespace

namespace {

double c191_body0_dist(const PoseRow& r, void* mesh) {
    const C191Bone b = c191_bone(r, mesh);
    return b.ok ? b.d : -1.0;
}

// Card 359 (b190): bone 0 in world space (false: bone 0 or body 0 not read).
bool c191_bone0_world(const PoseRow& r, void* mesh, Vec3& out) {
    const C191Bone b = c191_bone(r, mesh);
    out = b.w;
    return b.ok;
}

// Card 359 (b191; c423b190d join: 3 of 105 guids' final bone 0 over 50 uu from the host's, up to 286.9, each equal to
// the client's WARN card191 bone 0 to body 0 distance; body 0 itself matches the host): a copy frozen with bone 0
// over kC359FixUu off body 0 moves its mesh component by (body 0 - bone 0), TeleportPhysics, while its bodies still
// simulate (the freeze follows). One row per fix with bone 0 to body 0 before and after and how far body 0 moved.
// Card 505 (c243b273a: 25 of 33 corpses 5 to 50 uu off stayed drawn off their bodies): the fix starts at 5 uu.
// c502b278b: 22 client and 62 client2 WARN card191 frozen rows read 1.0 to 5.0 uu, between the freeze tolerance and that
// 5 uu start; the fix now starts at kC191BoneTolerance, so a copy the WARN would name is moved first.
// Returns bone 0 to body 0 after the fix (off, the step's reading, when nothing moved).
double c359_mesh_to_body0(const PoseRow& r, void* mesh, double off) {
    constexpr double kC359FixUu = kC191BoneTolerance;
    constexpr uint8_t kTeleportPhysics = 1;
    const C191Bone b = c191_bone(r, mesh);
    if (!b.ok || b.d <= kC359FixUu || g_add_world_offset == nullptr) {
        return off;
    }
    const double delta[3] = {b.b0.x - b.w.x, b.b0.y - b.w.y, b.b0.z - b.w.z};
    g_add_world_offset(mesh, delta, false, nullptr, kTeleportPhysics);
    ++g_pose.c359_fixes;
    const C191Bone a = c191_bone(r, mesh);
    if (note_event_cap(g_pose.c359_fix_lines, 200, "card359 mesh fix")) {
        log::line("card359: client guid 0x%llx snap %u mesh moved (%.1f, %.1f, %.1f): bone 0 %.1f uu off body 0 "
                  "before, %.1f after; body 0 moved %.1f uu (%u fixed)",
                  static_cast<unsigned long long>(r.guid), r.snap, delta[0], delta[1], delta[2], b.d,
                  a.ok ? a.d : -1.0, a.ok ? vec_dist(a.b0, b.b0) : -1.0, g_pose.c359_fixes);
    }
    return a.ok ? a.d : off;
}

// Card 359 (b124 rows: 90 of 90 bad copies had an end tick on every waited tick): on a copy whose animation still
// refreshes, the bones hold the animation's pose at the client step, so the step's wait never sees bone 0 on body 0
// and the freeze kept that pose (up to 437 uu from the bodies). Right after the end tick's blend the bones are the
// bodies; a waiting final is frozen here when that reads true. Otherwise the step keeps its bounded wait and its WARN
// row carries both distances.
void c359_freeze_after_blend(PoseRow& r, void* mesh, double before) {
    const double after = c191_body0_dist(r, mesh);
    r.c359_before = static_cast<float>(before);
    r.c359_after = static_cast<float>(after);
    r.c359_last = c359_snap(r, mesh); // card 359 (b187)
    if (after >= 0.0 && after <= kC191BoneTolerance) {
        pose_freeze_step(r); // logs the wait row, stops the bodies, sets bNoSkeletonUpdate
    }
}

} // namespace

void detour_c191_end_tick(void* self, float dt, int32_t tick_type, int32_t thread, const void* completion) {
    void* const mesh = g_client_session && g_pose.c191_any && self != nullptr ? read_ptr(self, kC191TickTarget) : nullptr;
    PoseRow* const r = mesh != nullptr ? c191_row(mesh) : nullptr;
    if (r == nullptr) {
        g_orig_c191_end_tick(self, dt, tick_type, thread, completion);
        return;
    }
    if (!r->c191_frozen) { // between the apply and the freeze: counted for the arrival row
        r->c191_pre = static_cast<uint8_t>(r->c191_pre < UINT8_MAX ? r->c191_pre + 1 : UINT8_MAX);
        const bool waiting = r->state == PoseState::Freezing && r->freeze_waits != 0; // the step found the bones off
        const double before = waiting ? c191_body0_dist(*r, mesh) : -1.0;
        g_orig_c191_end_tick(self, dt, tick_type, thread, completion);
        if (waiting) {
            c359_freeze_after_blend(*r, mesh, before);
        }
        return;
    }
    r->c191_seen = static_cast<uint8_t>(r->c191_seen < UINT8_MAX ? r->c191_seen + 1 : UINT8_MAX);
    const bool registered = (static_cast<const uint8_t*>(mesh)[kC191Registered] & 1) != 0;
    const bool simulating = c39_mesh_simulating(mesh);
    const int awake = g_pose_mesh_awake != nullptr ? (g_pose_mesh_awake(mesh) ? 1 : 0) : -1;
    const bool blend = pose_should_blend(mesh);
    const C191Bone before = c191_bone(*r, mesh);
    g_orig_c191_end_tick(self, dt, tick_type, thread, completion);
    const C191Bone after = c191_bone(*r, mesh);
    ++r->c191_ticks;
    r->c191_mesh = r->c191_ticks < kC191Ticks ? mesh : nullptr;
    if (!note_event_cap(g_pose.c191_lines, kC191Lines, "card191")) {
        return;
    }
    log::line("card191: client t=%llu guid 0x%llx end tick %u after the freeze: registered bit %d, simulating %d, awake "
              "%d, ShouldBlend %d; bone 0 before (%.1f, %.1f, %.1f) %.1f uu from body 0, after (%.1f, %.1f, %.1f) %.1f "
              "uu%s",
              static_cast<unsigned long long>(GetTickCount64()), static_cast<unsigned long long>(r->guid),
              static_cast<unsigned>(r->c191_ticks), registered ? 1 : 0, simulating ? 1 : 0, awake, blend ? 1 : 0,
              before.cs.x, before.cs.y, before.cs.z, before.d, after.cs.x, after.cs.y, after.cs.z, after.d,
              before.ok && after.ok ? "" : " (no body 0 or bone name: not measured)");
}

// The ragdoll's body 0 (root, pelvis) world position, which the pose apply sets; false when the mesh has no bodies, body
// 0 does not simulate or reads as the identity. A ragdoll leaves the mesh component's own location where the corpse
// died, so a check that reads the component measures a place nothing is drawn (hb46: 8 corpses "apart" by 452 to
// 660 uu with a body error of 0.0 after the apply).
namespace {

// Card 152 r2, client: gravity off calls against gravity on calls (each on the copy, or the copy gone first). The
// difference is the streaming copies still here; more than that is a copy left with gravity off.
void pose_grav_line() {
    uint32_t live = 0;
    for (int32_t i = 0; i < g_pose.n; ++i) {
        live += g_pose.row[i].grav_off ? 1u : 0u;
    }
    const uint32_t back = g_pose.st_grav_on + g_pose.st_grav_gone;
    const bool bad = g_pose.st_grav_off < back || g_pose.st_grav_off - back > live;
    log::line("%scard152: client stream gravity map totals: %u off, %u on, %u copies gone first, %u streaming copies "
              "still here", bad ? "WARN " : "", g_pose.st_grav_off, g_pose.st_grav_on, g_pose.st_grav_gone, live);
}

// Card 152 r3: every card 152 counter starts again once its map line is logged (no count runs across maps). The
// gravity counters stay: their balance holds streaming rows that live across the line (pose_grav_line).
void pose_stream_reset() {
    g_pose.st_samples = g_pose.st_bytes = g_pose.st_cap_hits = g_pose.st_deferred = g_pose.st_backlog_skips = 0;
    g_pose.st_backlog_peak = 0;
    g_pose.st_moving_peak = g_pose.st_step_peak = 0;
    g_pose.st_rx = g_pose.st_holds = g_pose.st_stopped = g_pose.st_finals = g_pose.st_reholds = 0;
    g_pose.st_wait_blocks = g_pose.st_bone_checks = g_pose.st_frozen_reholds = g_pose.st_no_sim = 0;
    g_pose.st_unfreezes = g_pose.st_target_warns = g_pose.sim_skipped = 0;
    g_pose.st_freeze_blocked = g_pose.st_freeze_block_lines = g_pose.st_check_skips = g_pose.st_finals_sent = 0;
    g_pose.st_bone_max = g_pose.st_target_max = 0.0;
    g_pose.c521_deltas = g_pose.c521_wholes = g_pose.c521_delta_bytes = g_pose.c521_whole_bytes = 0;
    g_pose.c521_rx_deltas = g_pose.c521_misses = g_pose.c521_lines = 0;
    memset(g_pose.c521_vlen, 0, sizeof(g_pose.c521_vlen));
    g_pose.c521_flips = g_pose.c521_body_n = g_pose.c521_big = 0;
    memset(g_pose.c521_rot_hist, 0, sizeof(g_pose.c521_rot_hist));
    memset(g_pose.c521_rot_big_field, 0, sizeof(g_pose.c521_rot_big_field));
    g_pose.c523_falls = g_pose.c523_handovers = g_pose.c523_teleports = g_pose.c523_late = g_pose.c523_gap_max = 0;
    g_pose.c523_hand_sum = g_pose.c523_hand_max = 0.0;
    g_pose.c523_off_sum = g_pose.c523_off_max = 0.0;
    g_pose.c523_unfreeze_kept = 0;
    g_pose.c524_rows = 0;
    g_pose.c524_hand_rows = 0;
}

// Card 152: one stream map line per side, for sizing kPoseStreamPerStep and kPoseStreamBacklog. Logged once a map, from
// c152_pose_wait_clear_all (LoadMap enter or the next arm, whichever runs first).
void pose_stream_map_line() {
    if (g_pose.st_samples != 0 || g_pose.st_backlog_skips != 0 || g_pose.st_finals_sent + g_pose.st_check_skips != 0) {
        log::line("card152: host pose stream map totals: %u samples, %u bytes, peak %d corpses moving at once, peak %d "
                  "samples in one step (cap %d), %u cap hits, %u deferred, %u backlog skips (over %u frames), backlog "
                  "peak %u frames; %u stream finals sent (per PC), %u round self-check rows skipped streaming",
                  g_pose.st_samples, g_pose.st_bytes, g_pose.st_moving_peak, g_pose.st_step_peak, kPoseStreamPerStep,
                  g_pose.st_cap_hits, g_pose.st_deferred, g_pose.st_backlog_skips, kPoseStreamBacklog,
                  g_pose.st_backlog_peak, g_pose.st_finals_sent, g_pose.st_check_skips);
    }
    if (g_pose.st_rx != 0 || g_pose.st_wait_blocks != 0 || g_pose.st_freeze_blocked != 0) {
        log::line("%scard152: client pose stream map totals: %u samples received, %u stream starts (%u already "
                  "simulating, %u would not simulate), %u finals, %u unfreezes continued, %u gravity off again after the "
                  "game's own start, %u card 140 frozen copies held again, %u game starts held, %u bone checks (worst "
                  "%.1f uu), body 0 worst %.1f uu from its target (%u over %.0f), %u simulating rows left out of the "
                  "digest, %u game freezes blocked on a streaming copy", g_pose.st_bone_max > 1.0 || g_pose.st_no_sim + g_pose.st_target_warns != 0 ? "WARN " : "",
                  g_pose.st_rx, g_pose.st_holds, g_pose.st_stopped, g_pose.st_no_sim, g_pose.st_finals,
                  g_pose.st_unfreezes, g_pose.st_reholds, g_pose.st_frozen_reholds, g_pose.st_wait_blocks,
                  g_pose.st_bone_checks, g_pose.st_bone_max, g_pose.st_target_max, g_pose.st_target_warns, kPoseTargetUu,
                  g_pose.sim_skipped, g_pose.st_freeze_blocked);
        pose_grav_line();
    }
    if (g_pose.c521_deltas + g_pose.c521_wholes != 0) { // card 521 step 2, own row (the card152 rows stay as they were)
        log::line("card521: host pose stream wire totals: %u delta samples (%u B), %u whole samples (%u B), whole every "
                  "%u", g_pose.c521_deltas, g_pose.c521_delta_bytes, g_pose.c521_wholes, g_pose.c521_whole_bytes,
                  static_cast<unsigned>(kPoseWholeEvery));
    }
    if (g_pose.c521_body_n != 0) {
        log::line("card521: host delta field sizes over %u bodies: position 1/2/3 B %u/%u/%u, rotation 1/2/3 B "
                  "%u/%u/%u; %u rotations negated (dot < 0); %u samples whole because the delta was not shorter",
                  g_pose.c521_body_n, g_pose.c521_vlen[0][0], g_pose.c521_vlen[0][1], g_pose.c521_vlen[0][2],
                  g_pose.c521_vlen[1][0], g_pose.c521_vlen[1][1], g_pose.c521_vlen[1][2], g_pose.c521_flips,
                  g_pose.c521_big);
        const uint32_t (&h)[2][6] = g_pose.c521_rot_hist;
        const uint32_t (&bf)[4] = g_pose.c521_rot_big_field;
        log::line("card521: host rotation delta spread (steps of %d, buckets <64/<128/<256/<512/<1024/more): at rest "
                  "%u/%u/%u/%u/%u/%u, moving %u/%u/%u/%u/%u/%u; 64 and up by field x/y/z/w %u/%u/%u/%u",
                  kPoseRotStep, h[0][0], h[0][1], h[0][2], h[0][3], h[0][4], h[0][5], h[1][0], h[1][1], h[1][2],
                  h[1][3], h[1][4], h[1][5], bf[0], bf[1], bf[2], bf[3]);
    }
    if (g_pose.c521_rx_deltas + g_pose.c521_misses != 0) {
        log::line("card521: client pose stream wire totals: %u deltas applied, %u delta misses",
                  g_pose.c521_rx_deltas, g_pose.c521_misses);
    }
    if (g_pose.c523_falls + g_pose.c523_late != 0) {
        log::line("card523: client stream own falls: %u started (body 0 off sample 0 mean %.1f uu, worst %.1f uu, closed over "
                  "%llu ms; %u unfreeze snaps kept it), %u handed over at the next sample (body 0 mean %.1f "
                  "uu, worst %.1f uu), %u teleported; %u chained samples over %llu ms late, longest gap %u ms",
                  g_pose.c523_falls, g_pose.c523_falls != 0 ? g_pose.c523_off_sum / g_pose.c523_falls : 0.0,
                  g_pose.c523_off_max, static_cast<unsigned long long>(kPoseStepMs), g_pose.c523_unfreeze_kept, g_pose.c523_handovers,
                  g_pose.c523_handovers != 0 ? g_pose.c523_hand_sum / g_pose.c523_handovers : 0.0,
                  g_pose.c523_hand_max, g_pose.c523_teleports, g_pose.c523_late,
                  static_cast<unsigned long long>(kPoseLateMs), g_pose.c523_gap_max);
    }
    pose_stream_reset();
}

// ---- Card 177: sprite NPC deaths (IsNPC2D: the laser, Morita and other 2D troopers) ----
// A sprite NPC has no physics bodies, so it never gets a pose row. Its death is a flipbook: TryPlayDeathAnimation plays
// "Death" (GetSpriteAnimation may remap it to a variant such as Death01 or Death02) and the non-looping sequence holds
// its last frame. The host keeps one sprite row per death (the name the animator resolved, the host time) and sends it
// as "S <guid> <host age ms> <host frame> <frames> <name>"; "R <guid>" removes it with the corpse, as for a pose row,
// and the join replay sends every row. A client copy that arrives already dead is not hidden: it plays the host's
// sequence and, when the host's death is older than the sequence, is set straight to its last frame (the rest state).
//
// UADSpriteAnimatorComponent (NPC+0xA30). StartAnimationSequence (RVA 0x04A249C0) stores the sequence at +0x120 and the
// resolved name at +0xE0, sets the frame elapsed time +0x110 to -1 and the rate at +0x130. TickComponent (RVA
// 0x04A24F40) writes the frame index +0x118 and the frame's display time +0x11C on a frame change and broadcasts the
// frame delegate at +0xF0 with the new FADAnimationFrame; at the last frame of a non-looping sequence it holds.
// FADSpriteAnimationSequence: +0 bUniformTime, +4 uniform frame time (s), +0x10 frames (FADAnimationFrame, 0x28 bytes,
// +0 TimeToDisplay in s), +0x18 frame count.
constexpr size_t kNpcSpriteAnimator = 0xA30;
constexpr size_t kAnimSeqName = 0xE0;       // FName: the sequence name StartAnimationSequence resolved
constexpr size_t kAnimFrameDelegate = 0xF0; // FADAnimationFrameDelegate
constexpr size_t kAnimElapsed = 0x110;      // float: time on the current frame (-1 before the first)
constexpr size_t kAnimFrame = 0x118;        // int32: current frame index
constexpr size_t kAnimFrameTime = 0x11C;    // float: the current frame's display time
constexpr size_t kAnimSeq = 0x120;          // FADSpriteAnimationSequence*
constexpr size_t kAnimRate = 0x130;         // float: play rate
constexpr size_t kSeqUniform = 0x0;
constexpr size_t kSeqUniformTime = 0x4;
constexpr size_t kSeqFrames = 0x10;
constexpr size_t kSeqFrameCount = 0x18;
constexpr size_t kFrameStride = 0x28;
constexpr int32_t kSpriteFramesMax = 512;   // a death set is 12 frames; more means a misread sequence
constexpr size_t kSpriteName = 24;          // sequence name chars with the terminator
// Rows per side: one per sprite corpse still in the world (the corpse ticket limit removes old ones). The busiest
// map seen (FED_C2M1 playtest, 2026-11-08) logged 85 trooper death edges, under the client edge line cap.
constexpr int32_t kSpriteRows = 256;
constexpr int32_t kSpriteWaits = 32;        // client: arrivals waiting for their row at once
constexpr ULONGLONG kSpriteWaitMs = 3000;   // client: from the later of the arrival and the first Pong
constexpr ULONGLONG kSpriteCheckMs = 1000;  // both sides: the rest state is read back this long after it is due
constexpr uint32_t kSpriteLines = 60;       // per side per map
constexpr uint32_t kSpriteWarnLines = 20;

enum SpriteReject : uint8_t {
    kSrNoAnimator, kSrNoSequence, kSrNotDeath, kSrNameLong, kSrFull, kSrBadHead, kSrNotPlayed,
    kSrFrameCount, kSrNoBroadcast, kSrWaitFull, kSrRejects
};
constexpr const char* kSrName[kSrRejects] = {"no animator", "no sequence playing", "not a death sequence",
                                             "name too long", "table full", "bad head",
                                             "sequence not played", "frame count out of range",
                                             "frame broadcast unresolved", "wait list full"};

struct SpriteRow {
    C31Weak actor;      // host: the corpse (gone: the row is removed)
    uint64_t guid;      // 0 on the host until the corpse has a NetGUID (sent then)
    ULONGLONG at;       // host: the death; client: when the row came in
    uint32_t age;       // client: the host's age of the death when the row was sent, ms
    uint32_t ms;        // host: the sequence length, ms
    int32_t frame;      // the host's frame when the row was sent
    int32_t frames;
    bool rest_read;     // host: the rest state was read back
    char seq[kSpriteName];
};
struct SpriteWait {     // client: a copy that arrived dead, waiting for its row, then for its read-back
    C31Weak actor;
    uint64_t guid;
    ULONGLONG first;
    ULONGLONG check_at; // 0 while waiting for the row
    bool rest;          // set to the rest state (false: played from the start)
    char seq[kSpriteName];
    int32_t frames;     // the host's frame count (0 for the local fallback)
};
constexpr uint32_t kSpriteReviveDrops = 16; // card 318: guids whose row a client revive dropped, awaiting the host's R
struct SpriteDeaths {
    SpriteRow rows[kSpriteRows];
    uint64_t revive_drops[kSpriteReviveDrops]; // client, ring; 0 = free
    uint32_t revive_next, revive_r;            // client: next ring slot; host R rows that matched a revive drop
    SpriteWait waits[kSpriteWaits];
    int32_t n, peak, waits_n, wait_peak;
    uint32_t added, sent, owed, owed_sent, removed, revived, updated, replayed, rest_reads;     // host
    uint32_t received, updates_rx, removed_rx, arrivals, rest, from_start, fallbacks, gone, checks, mismatches; // client
    uint32_t reject[kSrRejects];
    uint32_t lines, warn_lines;
    bool full_warned;
    const char* side; // side(), latched each pose tick while the map plays (the session flag is clear at map end)
};
SpriteDeaths g_sprite{}; // game thread

struct SpriteState {     // what the animator plays now
    SpriteReject why;    // kSrRejects when read
    char seq[kSpriteName];
    int32_t frame, frames;
    uint32_t ms;         // the whole sequence at its rate
};

using FrameBroadcastFn = void (*)(const void* delegate, const void* frame);

void sprite_reject(SpriteReject r, const void* npc, const char* side) {
    ++g_sprite.reject[r];
    if (note_event_cap(g_sprite.warn_lines, kSpriteWarnLines, "card177 sprite warn")) {
        log::line("WARN card177: %s sprite death %s %s: %s", side, npc != nullptr ? class_name(npc).text : "(none)",
                  npc != nullptr ? object_name(npc).text : "", kSrName[r]);
    }
}

// The sequence the sprite animator plays now: its resolved name (with the FName number), frame, count and length.
SpriteState sprite_read(const void* npc) {
    SpriteState s{};
    const void* const anim = read_ptr(npc, kNpcSpriteAnimator);
    const void* const seq = anim != nullptr ? read_ptr(anim, kAnimSeq) : nullptr;
    s.why = anim == nullptr ? kSrNoAnimator : seq == nullptr ? kSrNoSequence : kSrRejects;
    if (s.why != kSrRejects) {
        return s;
    }
    const auto* const a = static_cast<const uint8_t*>(anim);
    const int32_t number = read_i32(anim, kAnimSeqName + 4);
    char base[kSpriteName];
    const uint32_t len = fname_into(a + kAnimSeqName, base, sizeof(base));
    const int w = number != 0 ? snprintf(s.seq, sizeof(s.seq), "%s_%d", base, number - 1)
                              : snprintf(s.seq, sizeof(s.seq), "%s", base);
    s.frames = read_i32(seq, kSeqFrameCount);
    s.frame = read_i32(anim, kAnimFrame);
    s.why = len >= kSpriteName || w < 0 || static_cast<size_t>(w) >= sizeof(s.seq) ? kSrNameLong
            : s.frames < 1 || s.frames > kSpriteFramesMax                          ? kSrFrameCount
                                                                                    : kSrRejects;
    if (s.why != kSrRejects) {
        return s;
    }
    // Frame and count from the same sequence: an index past this sequence's count is the one the animator held for
    // the sequence before (TryPlayAnimationSequence switched it this tick, its first frame is next).
    s.frame = s.frame >= 0 && s.frame < s.frames ? s.frame : 0;
    const auto* const frames = static_cast<const uint8_t*>(read_ptr(seq, kSeqFrames));
    const bool uniform = static_cast<const uint8_t*>(seq)[kSeqUniform] != 0;
    double total = 0.0;
    for (int32_t i = 0; i < s.frames; ++i) {
        total += uniform || frames == nullptr ? read_f32(seq, kSeqUniformTime)
                                              : read_f32(frames + static_cast<size_t>(i) * kFrameStride, 0);
    }
    const float rate = read_f32(anim, kAnimRate);
    s.ms = static_cast<uint32_t>(total * 1000.0 / (rate > 0.0f ? rate : 1.0f));
    return s;
}

// The names a row may carry: the death set ("Death" and its variants) and "OverKill" (the gib switch, card 039).
bool sprite_death_name(const char* seq) {
    return strncmp(seq, "Death", 5) == 0 || strncmp(seq, "OverKill", 8) == 0;
}

// Client: a row's name is well formed: 1 to kSpriteName - 1 identifier characters (letters, digits, '_').
bool sprite_name_ok(const char* seq) {
    size_t i = 0;
    for (; i < kSpriteName && seq[i] != '\0'; ++i) {
        const char c = seq[i];
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) {
            return false;
        }
    }
    return i > 0 && i < kSpriteName;
}

SpriteRow* sprite_find(uint64_t guid, const void* actor) {
    for (int32_t i = 0; i < g_sprite.n; ++i) {
        SpriteRow& r = g_sprite.rows[i];
        if ((guid != 0 && r.guid == guid) || (actor != nullptr && c31_weak_get(r.actor) == actor)) {
            return &r;
        }
    }
    return nullptr;
}

void sprite_remove(SpriteRow* r) {
    *r = g_sprite.rows[--g_sprite.n];
}

SpriteRow* sprite_add() {
    if (g_sprite.n >= kSpriteRows) {
        if (!g_sprite.full_warned) {
            g_sprite.full_warned = true;
            log::line("WARN card177: sprite death table full (%d rows) for the first time this map", kSpriteRows);
        }
        return nullptr;
    }
    SpriteRow* const r = &g_sprite.rows[g_sprite.n++];
    *r = SpriteRow{};
    g_sprite.peak = g_sprite.n > g_sprite.peak ? g_sprite.n : g_sprite.peak;
    return r;
}

// Host: a row to one PC (pc) or to every ponged PC (world).
int32_t sprite_send(const SpriteRow& r, void* pc, const void* world, ULONGLONG now) {
    char text[96];
    void* const npc = c31_weak_get(r.actor);
    const SpriteState s = npc != nullptr ? sprite_read(npc) : SpriteState{kSrNoAnimator, {}, 0, 0, 0};
    const bool same = s.why == kSrRejects && strcmp(s.seq, r.seq) == 0; // frame and count from one sequence
    snprintf(text, sizeof(text), "S %llx %u %d %d %s", static_cast<unsigned long long>(r.guid),
             static_cast<unsigned>(now - r.at), same ? s.frame : r.frame, same ? s.frames : r.frames, r.seq);
    return pc != nullptr ? (c95_send_one(pc, ChanOp::Pose, text) ? 1 : 0) : c95_send_all(world, ChanOp::Pose, text).sent;
}

// Host, per pose step: a corpse gone loses its row ("R" to the clients); a row without a NetGUID sends once it has
// one; a row whose sequence has had time to end is read back once (the host's own rest state, for the client's).
// Card 428 second gap (c423b136a, guids 0x4b8 and 0x4a4): a rest row that left Dead (overkilled) sent a bare R while
// the corpse's pose row lived; the client dropped its pose row too and the pose row's own R 254 ms later found no
// row. A sprite ('S') or rest ('T') release sent while another host table still holds the guid names its table and
// the client drops only that row; the last table to go sends the bare R, which drops whatever is left.
void corpse_release_text(char* text, size_t cap, uint64_t guid, char table) {
    const bool other_side = table == 'S' ? rest_find(guid, nullptr) != nullptr : sprite_find(guid, nullptr) != nullptr;
    if (pose_find(guid) != nullptr || other_side) {
        snprintf(text, cap, "R %llx %c", static_cast<unsigned long long>(guid), table);
        return;
    }
    snprintf(text, cap, "R %llx", static_cast<unsigned long long>(guid));
}

void sprite_host_drop(SpriteRow& r, const void* world) {
    char text[48];
    corpse_release_text(text, sizeof(text), r.guid, 'S');
    if (r.guid != 0) {
        c95_send_all(world, ChanOp::Pose, text);
    }
    ++g_sprite.removed;
    sprite_remove(&r);
}

// Host: the animator plays another death-set sequence than the row's (OverKill after the death): the same row takes
// the new name, frame count and length, and is sent again. Returns the state read.
SpriteState sprite_host_update(SpriteRow& r, void* npc, const void* world, ULONGLONG now, const char* when) {
    const SpriteState s = sprite_read(npc);
    if (s.why != kSrRejects || strcmp(s.seq, r.seq) == 0) {
        return s;
    }
    if (!sprite_death_name(s.seq)) {
        sprite_reject(kSrNotDeath, npc, "host"); // the row stays as it was
        return s;
    }
    char was[kSpriteName];
    memcpy(was, r.seq, sizeof(was));
    memcpy(r.seq, s.seq, sizeof(r.seq));
    r.frames = s.frames;
    r.frame = s.frame;
    r.ms = s.ms;
    r.rest_read = false; // read back again once the new sequence has had time to end
    ++g_sprite.updated;
    const int32_t sent = r.guid != 0 ? sprite_send(r, nullptr, world, now) : 0;
    if (note_event_cap(g_sprite.lines, kSpriteLines, "card177 sprite")) {
        log::line("card177: host sprite row update %s guid 0x%llx seq %s -> %s (%d frames, %u ms), %s, sent to %d PCs",
                  class_name(npc).text, static_cast<unsigned long long>(r.guid), was, r.seq, r.frames, r.ms, when,
                  sent);
    }
    return s;
}

void sprite_host_step(const void* world, ULONGLONG now) {
    for (int32_t i = g_sprite.n - 1; i >= 0; --i) {
        SpriteRow& r = g_sprite.rows[i];
        void* const npc = c31_weak_get(r.actor);
        if (npc == nullptr) {
            sprite_host_drop(r, world);
            continue;
        }
        // The health attribute set, as the client's revive edge reads it (c37_pawn): CurrentHealth at
        // kNpcCurrentHealth stays 0 on a revived trooper (c423b139a: 61 rows, 0 revived, 9 second deaths).
        const float health = c23_set_health(npc);
        if (health > 0.0f) { // revived: no corpse; a later death adds a new row
            ++g_sprite.revived;
            if (note_event_cap(g_sprite.lines, kSpriteLines, "card177 sprite")) {
                log::line("card177: host sprite row removed %s guid 0x%llx seq %s: revived (Health %.1f), removal %s",
                          class_name(npc).text, static_cast<unsigned long long>(r.guid), r.seq, health,
                          r.guid != 0 ? "sent" : "not sent (no NetGUID)");
            }
            sprite_host_drop(r, world);
            continue;
        }
        if (r.guid == 0 && (r.guid = c41_guid(npc)) != 0) {
            g_sprite.owed_sent += sprite_send(r, nullptr, world, now) > 0 ? 1u : 0u;
        }
        if (r.rest_read || now - r.at < r.ms + kSpriteCheckMs) {
            continue;
        }
        r.rest_read = true;
        ++g_sprite.rest_reads;
        const SpriteState s = sprite_host_update(r, npc, world, now, "at the rest read");
        if (note_event_cap(g_sprite.lines, kSpriteLines, "card177 sprite")) {
            log::line("card177: host sprite rest %s guid 0x%llx seq %s frame %d of %d, %llu ms after the death "
                      "(row seq %s, %d frames, %u ms)%s%s", class_name(npc).text,
                      static_cast<unsigned long long>(r.guid), s.seq, s.frame, s.frames,
                      static_cast<unsigned long long>(now - r.at), r.seq, r.frames, r.ms,
                      s.why != kSrRejects ? ": " : "", s.why != kSrRejects ? kSrName[s.why] : "");
        }
    }
}

// Host: every row to a joining PC (from c132_pose_replay); a row without a NetGUID goes when it has one.
void sprite_replay(void* pc, ULONGLONG now) {
    int32_t sent = 0;
    int32_t owed = 0;
    for (int32_t i = 0; i < g_sprite.n; ++i) {
        const SpriteRow& r = g_sprite.rows[i];
        if (r.guid == 0) {
            ++owed;
            continue;
        }
        sent += sprite_send(r, pc, nullptr, now);
        if (note_event_cap(g_sprite.lines, kSpriteLines, "card177 sprite")) {
            log::line("card177: host sprite replay row guid 0x%llx seq %s, %llu ms after the death",
                      static_cast<unsigned long long>(r.guid), r.seq, static_cast<unsigned long long>(now - r.at));
        }
    }
    g_sprite.replayed += static_cast<uint32_t>(sent);
    log::line("card177: host sprite replay to a joining PC: %d rows sent of %d, %d without a NetGUID (sent when they "
              "have one)", sent, g_sprite.n, owed);
}

// A sequence name's row text as an FName (0: no FName constructor); as rest_name_text writes it.
uint64_t seq_fname(const char* seq) {
    wchar_t wide[kSpriteName];
    size_t i = 0;
    for (; i + 1 < kSpriteName && seq[i] != '\0'; ++i) {
        wide[i] = static_cast<wchar_t>(seq[i]);
    }
    wide[i] = L'\0';
    uint64_t name = 0;
    if (g_fname_ctor != nullptr) {
        g_fname_ctor(&name, wide, kFNameAdd);
    }
    return name;
}

// Client: play `seq` on the copy (forced, through the game's TryPlayAnimationSequence) and read what it plays.
SpriteState sprite_play(void* npc, const char* seq) {
    const uint64_t name = seq_fname(seq);
    if (name == 0 || g_orig_c41_try_play == nullptr) {
        return SpriteState{kSrNotPlayed, {}, 0, 0, 0};
    }
    g_orig_c41_try_play(npc, name, true);
    return sprite_read(npc);
}

// Client: the sequence just played (s, read) set straight to its last frame, as TickComponent shows a frame change:
// frame index, its display time, no time spent on it yet, and the frame delegate broadcast with that frame. The
// sequence does not loop, so the next ticks hold it. Returns the state read back.
// The state is read here, in the same call as the writes: a null animator, a null sequence and a frame count out of
// 1..kSpriteFramesMax end in that reject (counted by the caller) before any write or broadcast, and the last index
// comes from the sequence playing now, never from the host's row.
SpriteState sprite_set_rest(void* npc) {
    static FrameBroadcastFn broadcast = nullptr; // game thread
    broadcast = broadcast != nullptr || g_exe_base == nullptr ? broadcast
              : reinterpret_cast<FrameBroadcastFn>(const_cast<uint8_t*>(g_exe_base) + offsets::FADAnimationFrameDelegate_Broadcast.rva);
    const SpriteState s = sprite_read(npc);
    if (s.why != kSrRejects) {
        return s;
    }
    auto* const anim = static_cast<uint8_t*>(read_ptr(npc, kNpcSpriteAnimator));
    const void* const sq = read_ptr(anim, kAnimSeq);
    const auto* const frames = static_cast<const uint8_t*>(read_ptr(sq, kSeqFrames));
    if (broadcast == nullptr || frames == nullptr) {
        SpriteState out = s;
        out.why = kSrNoBroadcast;
        return out;
    }
    const uint8_t* const last = frames + static_cast<size_t>(s.frames - 1) * kFrameStride;
    const float elapsed = 0.0f;
    const int32_t index = s.frames - 1;
    const float time = static_cast<const uint8_t*>(sq)[kSeqUniform] != 0 ? read_f32(sq, kSeqUniformTime)
                                                                          : read_f32(last, 0);
    memcpy(anim + kAnimElapsed, &elapsed, sizeof(elapsed));
    memcpy(anim + kAnimFrame, &index, sizeof(index));
    memcpy(anim + kAnimFrameTime, &time, sizeof(time));
    broadcast(anim + kAnimFrameDelegate, last);
    return sprite_read(npc);
}

// Client: a row from the host ("S"). A row for a guid already held replaces it (the replay after a rejoin).
C132Result sprite_receive(const char* text, const char*& reason) {
    unsigned long long guid = 0;
    unsigned age = 0;
    int frame = 0;
    int frames = 0;
    char seq[kSpriteName] = {};
    const int got = sscanf_s(text, "S %llx %u %d %d %23s", &guid, &age, &frame, &frames, seq,
                             static_cast<unsigned>(sizeof(seq)));
    if (got != 5 || guid == 0 || frames < 1 || frames > kSpriteFramesMax || !sprite_name_ok(seq)) {
        sprite_reject(kSrBadHead, nullptr, "client");
        reason = kSrName[kSrBadHead];
        return C132Result::Refused;
    }
    SpriteRow* r = sprite_find(guid, nullptr);
    char was[kSpriteName] = {};
    if (r != nullptr) {
        memcpy(was, r->seq, sizeof(was));
    }
    r = r != nullptr ? r : sprite_add();
    if (r == nullptr) {
        sprite_reject(kSrFull, nullptr, "client");
        reason = kSrName[kSrFull];
        return C132Result::Refused;
    }
    const bool update = was[0] != '\0' && strcmp(was, seq) != 0;
    *r = SpriteRow{};
    r->guid = guid;
    r->at = GetTickCount64();
    r->age = age;
    r->frame = frame;
    r->frames = frames;
    memcpy(r->seq, seq, sizeof(seq));
    ++g_sprite.received;
    g_sprite.updates_rx += update ? 1u : 0u;
    for (int32_t i = 0; update && i < g_sprite.waits_n; ++i) { // the newest row applies: an applied arrival again
        SpriteWait& w = g_sprite.waits[i];
        w.check_at = w.guid == guid ? 0 : w.check_at;
    }
    if (note_event_cap(g_sprite.lines, kSpriteLines, "card177 sprite")) {
        log::line("card177: client sprite row guid 0x%llx seq %s, host age %u ms, host frame %d of %d%s%s%s", guid, seq,
                  age, frame, frames, update ? " (update, was " : "", update ? was : "", update ? ")" : "");
    }
    reason = nullptr;
    return C132Result::Applied;
}

// Card 318, client: a host R row for a guid whose row the client already dropped at a revive. Clears the entry.
bool sprite_revive_dropped(uint64_t guid) {
    for (uint64_t& g : g_sprite.revive_drops) {
        if (guid != 0 && g == guid) {
            g = 0;
            ++g_sprite.revive_r;
            return true;
        }
    }
    return false;
}

// Client ("R"): the host's corpse is gone. True when a row was held, or the client dropped it at a revive (card 318).
bool sprite_drop(uint64_t guid) {
    SpriteRow* const r = guid != 0 ? sprite_find(guid, nullptr) : nullptr;
    if (r == nullptr) {
        return sprite_revive_dropped(guid);
    }
    sprite_remove(r);
    ++g_sprite.removed_rx;
    return true;
}

void sprite_wait_remove(int32_t i) {
    g_sprite.waits[i] = g_sprite.waits[--g_sprite.waits_n];
}

// Client: an arrival's row came (or the wait ran out: row null, the local "Death" is set to rest, a WARN). Older than
// its sequence on the host: set to the rest state; younger: played from the start, so it ends with the host's.
void sprite_apply(SpriteWait& w, void* npc, const SpriteRow* r, ULONGLONG now) {
    const char* const seq = r != nullptr ? r->seq : "Death";
    const SpriteState played = sprite_play(npc, seq);
    const ULONGLONG host_age = r != nullptr ? r->age + (now - r->at) : 0;
    w.rest = r == nullptr || host_age >= played.ms;
    const SpriteState s = played.why == kSrRejects && w.rest ? sprite_set_rest(npc) : played;
    w.check_at = now + (w.rest ? 0 : played.ms - host_age) + kSpriteCheckMs;
    snprintf(w.seq, sizeof(w.seq), "%s", seq);
    w.frames = r != nullptr ? r->frames : 0;
    g_sprite.rest += w.rest ? 1u : 0u;
    g_sprite.from_start += w.rest ? 0u : 1u;
    if (s.why != kSrRejects) {
        sprite_reject(s.why, npc, "client");
    }
    if (r == nullptr) {
        ++g_sprite.fallbacks;
        log::line("WARN card177: client sprite death %s guid 0x%llx: no host row %llu ms after it arrived, local seq "
                  "%s set to its rest state (frame %d of %d)", class_name(npc).text,
                  static_cast<unsigned long long>(w.guid), static_cast<unsigned long long>(now - w.first), seq,
                  s.frame, s.frames);
        return;
    }
    if (note_event_cap(g_sprite.lines, kSpriteLines, "card177 sprite")) {
        log::line("card177: client sprite death %s guid 0x%llx seq %s, arrival, %s (host age %llu ms, sequence %u ms; "
                  "frame %d of %d, playing %s, %llu ms after it arrived)", class_name(npc).text,
                  static_cast<unsigned long long>(w.guid), seq, w.rest ? "rest state set" : "played from the start",
                  static_cast<unsigned long long>(host_age), played.ms, s.frame, s.frames, s.seq,
                  static_cast<unsigned long long>(now - w.first));
    }
}

// Client: the read-back, kSpriteCheckMs after the rest state was due: the copy's sequence and frame against the
// host's (its row's name, last frame). A difference is a WARN.
void sprite_check(const SpriteWait& w, void* npc) {
    const SpriteState s = sprite_read(npc);
    const int32_t want = w.frames != 0 ? w.frames - 1 : s.frames - 1;
    const bool bad = s.why != kSrRejects || strcmp(s.seq, w.seq) != 0 || s.frame != want ||
                     (w.frames != 0 && s.frames != w.frames);
    ++g_sprite.checks;
    g_sprite.mismatches += bad ? 1u : 0u;
    if (bad || note_event_cap(g_sprite.lines, kSpriteLines, "card177 sprite")) {
        log::line("%scard177: client sprite read-back %s guid 0x%llx: seq %s frame %d of %d, host seq %s frame %d of "
                  "%d%s%s", bad ? "WARN " : "", class_name(npc).text, static_cast<unsigned long long>(w.guid), s.seq,
                  s.frame, s.frames, w.seq, want, w.frames, s.why != kSrRejects ? ": " : "",
                  s.why != kSrRejects ? kSrName[s.why] : "");
    }
}

// Client, per tick: each waiting arrival. Its row applies at once; with no row by kSpriteWaitMs after the later of
// the arrival and the first Pong (held while the Pong is, up to kArrivalHoldMaxMs), the local fallback. Applied
// arrivals are read back at check_at and dropped.
void sprite_wait_tick(ULONGLONG now) {
    for (int32_t i = g_sprite.waits_n - 1; i >= 0; --i) {
        SpriteWait& w = g_sprite.waits[i];
        void* const npc = c31_weak_get(w.actor);
        if (npc == nullptr) {
            ++g_sprite.gone;
            sprite_wait_remove(i);
            continue;
        }
        if (w.check_at != 0) {
            if (now >= w.check_at) {
                sprite_check(w, npc);
                sprite_wait_remove(i);
            }
            continue;
        }
        w.guid = w.guid != 0 ? w.guid : c41_guid(npc);
        const SpriteRow* const r = sprite_find(w.guid, nullptr);
        const ULONGLONG open = c120_pong_open_ms();
        const ULONGLONG from = open > w.first ? open : w.first;
        const bool wait = open == 0 ? now - w.first < kArrivalHoldMaxMs : now - from < kSpriteWaitMs;
        if (r != nullptr || !wait) {
            sprite_apply(w, npc, r, now);
        }
    }
}

// Client: a sprite copy first seen already dead (from c132_pose_arrival). Not hidden: nothing is shown before the
// host's sequence plays on it, at once when its row is here.
void sprite_arrival(void* npc) {
    ++g_sprite.arrivals;
    if (g_sprite.waits_n >= kSpriteWaits) {
        sprite_reject(kSrWaitFull, npc, "client"); // applied now, with its row if here; not read back
        SpriteWait w{};
        c31_weak_set(w.actor, npc);
        w.guid = c41_guid(npc);
        w.first = GetTickCount64();
        sprite_apply(w, npc, sprite_find(w.guid, nullptr), w.first);
        return;
    }
    SpriteWait& w = g_sprite.waits[g_sprite.waits_n++];
    w = SpriteWait{};
    c31_weak_set(w.actor, npc);
    w.guid = c41_guid(npc);
    w.first = GetTickCount64();
    g_sprite.wait_peak = g_sprite.waits_n > g_sprite.wait_peak ? g_sprite.waits_n : g_sprite.wait_peak;
    sprite_wait_tick(w.first);
}

// Both sides, at the map's end: the totals, then the table is emptied.
void sprite_map_line() {
    if (g_sprite.added + g_sprite.received + g_sprite.arrivals == 0) {
        return;
    }
    char rejects[256];
    int32_t at = 0;
    uint32_t warn = 0;
    for (int32_t i = 0; i < kSrRejects && at < static_cast<int32_t>(sizeof(rejects)); ++i) {
        const int w = snprintf(rejects + at, sizeof(rejects) - static_cast<size_t>(at), "%s%s %u", i != 0 ? ", " : "",
                               kSrName[i], g_sprite.reject[i]);
        at += w > 0 ? w : 0;
        warn += g_sprite.reject[i];
    }
    const bool bad = warn + g_sprite.fallbacks + g_sprite.mismatches != 0 || g_sprite.full_warned;
    log::line("%scard177: %s sprite death map totals: %u rows added, %u sent, %u owed (%u sent later), %u removed (%u "
              "revived), %u updated, %u replayed, %u host rest reads; %u received (%u updates), %u removed by the host (%u "
              "after a client revive drop), "
              "%u arrivals (%u rest state set, %u played from the start, %u fallbacks, %u gone), %u read-backs (%u "
              "mismatches); rejects: %s; table peak %d of %d, wait peak %d of %d", bad ? "WARN " : "",
              g_sprite.side != nullptr ? g_sprite.side : side(), g_sprite.added, g_sprite.sent, g_sprite.owed,
              g_sprite.owed_sent, g_sprite.removed, g_sprite.revived, g_sprite.updated, g_sprite.replayed,
              g_sprite.rest_reads, g_sprite.received, g_sprite.updates_rx, g_sprite.removed_rx, g_sprite.revive_r, g_sprite.arrivals, g_sprite.rest,
              g_sprite.from_start, g_sprite.fallbacks, g_sprite.gone, g_sprite.checks, g_sprite.mismatches, rejects,
              g_sprite.peak, kSpriteRows, g_sprite.wait_peak, kSpriteWaits);
}

// ---- Card 269: 3D corpses that reach Dead with no ragdoll (ChariotBug, AphidBug, ArcherBug, JumperBug) ----
// Ragdoll is a per-class Blueprint choice, so such a corpse never gets a pose row (c132_pose_track opens one only at
// ragdoll start) and a client copy that arrives already dead used to wait kArrivalWaitMs and replay the death. The host
// notes each 3D NPC's death name at TryPlayDeathAnimation (the card 041 player's current sequence after the original);
// when ReceiveBaseStateChanged reports Dead (4) and the corpse has no pose row, a rest row (guid, name, death time) goes
// to every ponged PC as "T <guid> <host age ms> <name>", replayed to a joining PC; "R <guid>" removes it with the corpse
// or when the state leaves Dead (revive, overkill). Client: the rows are kept until "R" (a re-arrival finds its row).
// A copy arriving dead with a rest row is shown at once and plays the death name through
// TryPlayAnimationAgumentedByTime(name, kRestPlayS). The float is the sequence's total play time, not a start
// offset: the NPC's IADAnimatorInterface slot 0 is StartAnimationSequenceFixedTimeCallback, and
// UADSkeletalAnimatorComponent::StartAnimationSequenceFixedTime (exe+0x4A24B90) starts the sequence at rate
// length / time. The host age there (15-22 s in run c423b128a) stretched a death over that long, so the corpse
// stood upright. One read-back kRestCheckMs later logs
// IsPlayingAnimation(name) (sequence player vtbl+0x50) and the copy's base state byte.
constexpr int32_t kDeathNotes = 64;       // host: death starts noted, a ring (Dying to Dead takes seconds)
constexpr int32_t kRestRows = 256;        // per side: one per rest corpse still in the world
constexpr ULONGLONG kRestCheckMs = 1000;  // client: the read-back, this long after the play
constexpr char kRestBare[] = "None";      // card 207 step 3: row text of a corpse with no death name
constexpr float kRestPlayS = 0.05f;       // client: the death sequence's total play time on a rest arrival
constexpr ULONGLONG kRestCheck2Ms = 2000; // client: the second read-back, this long after the first
constexpr size_t kRestAnimator = 0xA30;   // the NPC's USTSkeletalAnimatorComponent (enemies.cpp kNpcAnimator)
constexpr size_t kRestStaticMode = 0x158; // its bShowingStaticMesh byte
constexpr size_t kRestStaticTime = 0x14C; // float: time on the static mesh animation
constexpr size_t kRestStaticLen = 0x150;  // float: that animation's length
constexpr size_t kRestSwapFrames = 0x15C; // int32: frames left of a deferred static-to-skeletal swap (senior, b131)
constexpr uint32_t kRestLines = 60;       // per side per map
constexpr uint32_t kRestWarnLines = 40; // two reads an arrival
constexpr uint8_t kBaseStateDead = 4;     // ESTBaseNPCState::Dead
constexpr size_t kRestSeqPlayer = 0xA38;  // the NPC's sequence player interface (card 041 kNpcSeqPlayer)
constexpr size_t kRestVtIsPlaying = 0x50; // UADAnimatorBaseComponent::IsPlayingAnimation(FName)

using SeqIsPlayingFn = bool (*)(void* player, uint64_t name);

struct DeathNote {
    C31Weak actor;
    uint64_t name; // the death FName the player was on after TryPlayDeathAnimation (0: none)
    ULONGLONG at;
};
struct RestRow {
    C31Weak actor;      // host: the corpse; client: the copy once it arrived (for the read-back)
    uint64_t guid;      // 0 on the host until the corpse has a NetGUID (sent then)
    ULONGLONG at;       // host: the death start; client: when the row came in
    ULONGLONG check_at; // client: the read-back is due (0: none)
    uint64_t played;    // client: the FName played at the arrival
    uint32_t age;       // client: the host's age of the death when the row was sent, ms
    uint8_t reads;      // client: read-backs done since the last play (two: kRestCheckMs, then kRestCheck2Ms later)
    uint64_t pose1;     // client: the pose hash at read-back 1 (b133: read-back 2 WARNs only when it differs)
    char seq[kSpriteName];
};
struct RestDeaths {
    DeathNote notes[kDeathNotes];
    int32_t notes_next;
    RestRow rows[kRestRows];
    int32_t n, peak, checks_due;
    uint32_t noted, added, no_name, owed, owed_sent, sent, removed, left_dead, replayed; // host
    uint32_t death_sent, death_unsent; // host, card 291: `D` rows sent to at least one PC; not sent (no guid or name)
    uint32_t received, bad, removed_rx, arrivals, played, not_played, checks, still_playing, pose_moved; // client
    uint32_t lines, warn_lines;
    bool full_warned;
};
RestDeaths g_rest{}; // game thread

RestRow* rest_find(uint64_t guid, const void* actor) {
    for (int32_t i = 0; i < g_rest.n; ++i) {
        RestRow& r = g_rest.rows[i];
        if ((guid != 0 && r.guid == guid) || (actor != nullptr && c31_weak_get(r.actor) == actor)) {
            return &r;
        }
    }
    return nullptr;
}

RestRow* rest_add() {
    if (g_rest.n >= kRestRows) {
        if (!g_rest.full_warned) {
            g_rest.full_warned = true;
            log::line("WARN card269: rest row table full (%d rows) for the first time this map", kRestRows);
        }
        return nullptr;
    }
    RestRow* const r = &g_rest.rows[g_rest.n++];
    *r = RestRow{};
    g_rest.peak = g_rest.n > g_rest.peak ? g_rest.n : g_rest.peak;
    return r;
}

void rest_remove(RestRow* r) {
    g_rest.checks_due -= r->check_at != 0 ? 1 : 0;
    *r = g_rest.rows[--g_rest.n];
}

// An FName as row text: its entry's characters plus "_<number - 1>" when it has a number, as sprite_read writes it.
// False when it does not fit or is not a well-formed name.
bool rest_name_text(uint64_t name, char* out, size_t cap) {
    char base[kSpriteName];
    const uint32_t len = fname_into(&name, base, sizeof(base));
    const int32_t number = static_cast<int32_t>(name >> 32);
    const int w = number != 0 ? snprintf(out, cap, "%s_%d", base, number - 1) : snprintf(out, cap, "%s", base);
    return name != 0 && len > 0 && len < kSpriteName && w > 0 && static_cast<size_t>(w) < cap && sprite_name_ok(out);
}

// Card 291, client: one death row. `own` 0: the copy's own pick was not played (the host's row came first).
void c291_line(uint64_t guid, uint64_t own, uint64_t host, const char* how) {
    if (!note_event_cap(g_c291.lines, kC291ClientLines, "card291 client death")) {
        return;
    }
    char own_text[kSpriteName];
    char host_text[kSpriteName];
    if (own == 0 || !rest_name_text(own, own_text, sizeof(own_text))) {
        snprintf(own_text, sizeof(own_text), "%s", own == 0 ? "not played" : "unreadable");
    }
    if (!rest_name_text(host, host_text, sizeof(host_text))) {
        snprintf(host_text, sizeof(host_text), "unreadable");
    }
    log::line("card291: client death guid 0x%llx: own sequence %s, host's %s, %s", static_cast<unsigned long long>(guid),
              own_text, host_text, how);
}

// Card 291, client: the host's `D <guid> <name>` row (sent at its death start). Before the copy's death edge it is
// kept for c291_death_edge; after it, the host's name is played from the start when the copy's current sequence
// differs (the game picked another variant), through the card 041 play path.
C132Result c291_receive(void* pc, const char* text, const char*& reason) {
    unsigned long long guid = 0;
    char seq[kSpriteName] = {};
    const int got = sscanf_s(text, "D %llx %23s", &guid, seq, static_cast<unsigned>(sizeof(seq)));
    if (got != 2 || guid == 0 || !sprite_name_ok(seq)) {
        reason = "bad death row";
        return C132Result::Refused;
    }
    const uint64_t name = seq_fname(seq);
    if (name == 0) {
        ++g_c291.unnamed;
        reason = "death name not made";
        return C132Result::Refused;
    }
    ++g_c291.rows;
    C291Death& d = c291_entry(guid);
    d.host_name = name;
    if (d.edge_at == 0) {
        return C132Result::Applied; // kept: the death edge plays it
    }
    void* const npc = pc != nullptr ? c41_resolve_guid(pc, guid) : nullptr;
    if (npc == nullptr || g_orig_c41_try_play == nullptr) {
        ++g_c291.unresolved;
        reason = "death row copy not resolved";
        return C132Result::Skipped;
    }
    const uint64_t own = c41_current(npc);
    const bool same = own == name;
    if (!same) {
        g_orig_c41_try_play(npc, name, true);
    }
    g_c291.same += same ? 1u : 0u;
    g_c291.replayed += same ? 0u : 1u;
    c291_line(guid, own, name, same ? "same" : "replayed");
    return C132Result::Applied;
}

// The mesh's VisibilityBasedAnimTickOption byte and its address (null, -1: no mesh or no property), as card 104 reads it.
int32_t rest_tick_option(const void* mesh, uint8_t** addr) {
    const uint8_t* const prop =
        mesh != nullptr ? prop_find_prop(read_ptr(mesh, kObjectClass), "VisibilityBasedAnimTickOption") : nullptr;
    *addr = prop != nullptr ? static_cast<uint8_t*>(const_cast<void*>(mesh)) + read_i32(prop, kPropertyOffset)
                            : nullptr;
    return *addr != nullptr ? **addr : -1;
}

// Card 269 (b131 diagnostic): what can keep a rest copy's mesh from updating its pose (c399b130a: every read-back read
// playing with an unchanged hash): the tick option (above 1: no pose tick while not rendered), the mesh tick, recent
// rendering, the actor and mesh hidden flags (USceneComponent bVisible 0x1A0 bit 0x20, bHiddenInGame 0x1A1 bit 0x08)
// and how far the mesh sits from its actor.
constexpr size_t kRestCompVisibleByte = 0x1A0; // enemies.cpp kCompVisibleByte
constexpr uint8_t kRestCompVisibleBit = 0x20;
void rest_mesh_text(const void* npc, char* out, size_t cap) {
    const auto* const m = static_cast<const uint8_t*>(npc != nullptr ? read_ptr(npc, kCharacterMesh) : nullptr);
    if (m == nullptr) {
        snprintf(out, cap, "no mesh");
        return;
    }
    uint8_t* opt = nullptr;
    const int32_t option = rest_tick_option(m, &opt);
    const bool seen_02 = g_actor_recently_rendered != nullptr && g_actor_recently_rendered(npc, 0.2f);
    const bool seen_2 = g_actor_recently_rendered != nullptr && g_actor_recently_rendered(npc, 2.0f);
    snprintf(out, cap,
             "tick option %d, mesh tick enabled %d, rendered in 0.2 s %d, in 2 s %d, actor hidden %d, mesh visible %d, "
             "mesh hidden in game %d, mesh %.0f uu from the actor",
             option, pose_cmc_ticking(m) ? 1 : 0, seen_02 ? 1 : 0, seen_2 ? 1 : 0,
             (static_cast<const uint8_t*>(npc)[kActorHiddenByte] & kActorHiddenBit) != 0 ? 1 : 0,
             (m[kRestCompVisibleByte] & kRestCompVisibleBit) != 0 ? 1 : 0, (m[kCompHiddenByte] & kCompHiddenBit) != 0 ? 1 : 0,
             vec_dist(read_vec3(m, kComponentTranslation), actor_location(npc)));
}

// Host: a row to one PC (pc) or to every ponged PC (world).
int32_t rest_send(const RestRow& r, void* pc, const void* world, ULONGLONG now) {
    char text[64];
    snprintf(text, sizeof(text), "T %llx %u %s", static_cast<unsigned long long>(r.guid),
             static_cast<unsigned>(now - r.at), r.seq);
    const int32_t sent =
        pc != nullptr ? (c95_send_one(pc, ChanOp::Pose, text) ? 1 : 0) : c95_send_all(world, ChanOp::Pose, text).sent;
    // b131: the host corpse's own hidden flags, compared per guid with the client's read-back.
    if (note_event_cap(g_rest.lines, kRestLines, "card269 rest")) {
        char mesh[256];
        const void* const npc = c31_weak_get(r.actor);
        rest_mesh_text(npc, mesh, sizeof(mesh));
        log::line("card269: host rest row sent guid 0x%llx seq %s to %d PC(s), %s: %s",
                  static_cast<unsigned long long>(r.guid), r.seq, sent, npc != nullptr ? class_name(npc).text : "gone",
                  mesh);
    }
    return sent;
}

void rest_host_drop(RestRow& r, const void* world) {
    char text[48];
    corpse_release_text(text, sizeof(text), r.guid, 'T');
    if (r.guid != 0) {
        c95_send_all(world, ChanOp::Pose, text);
    }
    ++g_rest.removed;
    rest_remove(&r);
}

// Host, per pose step: a corpse gone, or neither in the Dead state nor dead by health (revived), loses its row ("R" to the
// clients); a row without a NetGUID sends once it has one.
void rest_host_step(const void* world, ULONGLONG now) {
    for (int32_t i = g_rest.n - 1; i >= 0; --i) {
        RestRow& r = g_rest.rows[i];
        void* const npc = c31_weak_get(r.actor);
        const uint8_t state = npc != nullptr ? static_cast<const uint8_t*>(npc)[kNpcBaseState] : kBaseStateDead;
        float health = 0.0f;
        // Card 506: a corpse dead by health keeps its row in any base state (the AA turret ends in state 3, not Dead).
        const bool dead = state == kBaseStateDead || (npc != nullptr && c25_dead(npc, &health) == 1);
        if (npc != nullptr && !dead) {
            ++g_rest.left_dead;
            if (note_event_cap(g_rest.lines, kRestLines, "card269 rest")) {
                log::line("card269: host rest row removed %s guid 0x%llx seq %s: base state %u, removal %s",
                          class_name(npc).text, static_cast<unsigned long long>(r.guid), r.seq, state,
                          r.guid != 0 ? "sent" : "not sent (no NetGUID)");
            }
        }
        if (npc == nullptr || !dead) {
            rest_host_drop(r, world);
            continue;
        }
        if (r.guid == 0 && (r.guid = c41_guid(npc)) != 0) {
            g_rest.owed_sent += rest_send(r, nullptr, world, now) > 0 ? 1u : 0u;
        }
    }
}

// Host: every row to a joining PC (from c132_pose_replay); a row without a NetGUID goes when it has one.
void rest_replay(void* pc, ULONGLONG now) {
    int32_t sent = 0;
    int32_t owed = 0;
    for (int32_t i = 0; i < g_rest.n; ++i) {
        const RestRow& r = g_rest.rows[i];
        owed += r.guid == 0 ? 1 : 0;
        sent += r.guid != 0 ? rest_send(r, pc, nullptr, now) : 0;
    }
    g_rest.replayed += static_cast<uint32_t>(sent);
    log::line("card269: host rest replay to a joining PC: %d rows sent of %d, %d without a NetGUID (sent when they "
              "have one)", sent, g_rest.n, owed);
}

// Client ("T"): a host rest row, kept until "R" (a newer row for the guid replaces it).
C132Result rest_receive(const char* text, const char*& reason) {
    unsigned long long guid = 0;
    unsigned age = 0;
    char seq[kSpriteName] = {};
    const int got = sscanf_s(text, "T %llx %u %23s", &guid, &age, seq, static_cast<unsigned>(sizeof(seq)));
    if (got != 3 || guid == 0 || !sprite_name_ok(seq)) {
        ++g_rest.bad;
        reason = "bad rest row";
        return C132Result::Refused;
    }
    RestRow* r = rest_find(guid, nullptr);
    r = r != nullptr ? r : rest_add();
    if (r == nullptr) {
        reason = "rest table full";
        return C132Result::Refused;
    }
    const ULONGLONG check_at = r->check_at; // a row being read back keeps its read-back
    const C31Weak actor = r->actor;
    *r = RestRow{};
    r->actor = actor;
    r->check_at = check_at;
    r->guid = guid;
    r->age = age;
    r->at = GetTickCount64();
    memcpy(r->seq, seq, sizeof(r->seq));
    ++g_rest.received;
    if (note_event_cap(g_rest.lines, kRestLines, "card269 rest")) {
        log::line("card269: client rest row guid 0x%llx seq %s, host age %u ms", guid, r->seq, age);
    }
    return C132Result::Applied;
}

// Host (card 428), from the pose row's R: the client's handler drops the guid's pose, sprite and rest rows on any R,
// so the host's sprite and rest rows for it go here, unsent. A second R found no row on the client (c423b124a: 14 WARNs).
void corpse_host_released(uint64_t guid) {
    SpriteRow* const s = sprite_find(guid, nullptr);
    if (s != nullptr) {
        ++g_sprite.removed;
        sprite_remove(s);
    }
    RestRow* const r = rest_find(guid, nullptr);
    if (r != nullptr) {
        ++g_rest.removed;
        rest_remove(r);
    }
}

// Client ("R"): the host's corpse is gone. True when a row was held.
bool rest_drop(uint64_t guid) {
    RestRow* const r = guid != 0 ? rest_find(guid, nullptr) : nullptr;
    if (r == nullptr) {
        return false;
    }
    rest_remove(r);
    ++g_rest.removed_rx;
    return true;
}

// Client, from arrival_tick: a copy that arrived dead and has a rest row. Shown now; the host's death name plays from
// the host's age (TryPlayAnimationAgumentedByTime: resolved by RVA when card 041's trace hook is off).
void rest_arrival(const PoseArrival& a, void* npc, RestRow& r, ULONGLONG now) {
    arrival_show(a, read_ptr(npc, kCharacterMesh));
    ++g_rest.arrivals;
    if (strcmp(r.seq, kRestBare) == 0) { // card 207 step 3: the host's corpse plays nothing; the copy is shown as it is
        c31_weak_set(r.actor, npc);
        log::line("card207: client rest arrival %s guid 0x%llx bare: shown as it is, %llu ms after it arrived",
                  class_name(npc).text, static_cast<unsigned long long>(r.guid),
                  static_cast<unsigned long long>(now - a.first));
        return;
    }
    wchar_t wide[kSpriteName];
    size_t i = 0;
    for (; i + 1 < kSpriteName && r.seq[i] != '\0'; ++i) {
        wide[i] = static_cast<wchar_t>(r.seq[i]);
    }
    wide[i] = L'\0';
    uint64_t name = 0;
    if (g_fname_ctor != nullptr) {
        g_fname_ctor(&name, wide, kFNameAdd);
    }
    const TryPlayTimedFn timed =
        g_orig_c41_try_timed != nullptr ? g_orig_c41_try_timed
        : g_exe_base != nullptr ? reinterpret_cast<TryPlayTimedFn>(const_cast<uint8_t*>(g_exe_base) +
                                                                   offsets::ASTNonPlayableCharacterBase_TryPlayAnimationAgumentedByTime.rva)
                                : nullptr;
    const float age_s = static_cast<float>(r.age + (now - r.at)) / 1000.0f;
    const bool play = name != 0 && timed != nullptr;
    uint8_t* opt = nullptr; // b131: logged only (above 1: no pose tick while the mesh is not rendered)
    const int32_t option = rest_tick_option(read_ptr(npc, kCharacterMesh), &opt);
    if (play) {
        timed(npc, name, kRestPlayS);
    }
    g_rest.played += play ? 1u : 0u;
    g_rest.not_played += play ? 0u : 1u;
    c31_weak_set(r.actor, npc);
    g_rest.checks_due += play && r.check_at == 0 ? 1 : 0;
    r.check_at = play ? now + kRestCheckMs : r.check_at;
    r.reads = play ? 0 : r.reads;
    r.played = name;
    if (!play) {
        log::line("WARN card269: client rest arrival %s guid 0x%llx seq %s not played (%s): shown as it is",
                  class_name(npc).text, static_cast<unsigned long long>(r.guid), r.seq,
                  name == 0 ? "no FName" : "TryPlayAnimationAgumentedByTime unresolved");
        return;
    }
    if (note_event_cap(g_rest.lines, kRestLines, "card269 rest")) {
        log::line("card269: client rest arrival %s guid 0x%llx seq %s played over %.2f s (host age %.2f s), %llu ms "
                  "after it arrived; tick option %d",
                  class_name(npc).text, static_cast<unsigned long long>(r.guid), r.seq, kRestPlayS, age_s,
                  static_cast<unsigned long long>(now - a.first), option);
    }
}

// Client (card 269 diagnostic): FNV-1a over the mesh's current component-space pose (USkinnedMeshComponent::
// ComponentSpaceTransformsArray[CurrentReadComponentTransforms]; FTransform is 0x60: rotation 4 doubles at 0,
// translation 3 at 0x20, scale 3 at 0x40; the padding is skipped). Equal hashes on both reads = a held pose.
// 0 with bones 0: no mesh or no pose. The offsets are enemies.cpp's kSkinnedSpaceTransforms / kSkinnedSpaceRead.
constexpr size_t kRestSpaceTransforms = 0x5F0;
constexpr size_t kRestSpaceRead = 0x638;
constexpr int32_t kRestPoseBonesMax = 512;
uint64_t rest_pose_hash(const void* mesh, int32_t& bones) {
    bones = 0;
    const int32_t read = mesh != nullptr ? read_i32(mesh, kRestSpaceRead) : -1;
    if (read != 0 && read != 1) {
        return 0;
    }
    const uint8_t* const arr = static_cast<const uint8_t*>(mesh) + kRestSpaceTransforms + 0x10 * read;
    const uint8_t* const data = static_cast<const uint8_t*>(read_ptr(arr, 0));
    const int32_t count = read_i32(arr, 0x8);
    if (data == nullptr || count <= 0 || count > kRestPoseBonesMax) {
        return 0;
    }
    static constexpr size_t kAt[3] = {0x0, 0x20, 0x40};
    static constexpr size_t kLen[3] = {32, 24, 24};
    uint64_t h = 0xcbf29ce484222325ull;
    for (int32_t i = 0; i < count; ++i) {
        for (int32_t part = 0; part < 3; ++part) {
            const uint8_t* const p = data + static_cast<size_t>(i) * 0x60 + kAt[part];
            for (size_t b = 0; b < kLen[part]; ++b) {
                h = (h ^ p[b]) * 0x100000001b3ull;
            }
        }
    }
    bones = count;
    return h;
}

// Client, per tick while a read-back is due: IsPlayingAnimation(name) and the base state, kRestCheckMs after the play.
// Predicted: not playing (an offset past the sequence end holds the last pose). Playing is a WARN (a loop or restart).
// Card 269 open item: every arrival read playing (60 of 60 rows, ChariotBug Death), so each read also logs the
// animator's static time and length, and a second read follows kRestCheck2Ms later: the same time at the length on
// both reads is a held last pose; a time that moved or fell is a loop or restart. Totals count the first read.
// b133 (senior, c423b131a): IsPlayingAnimation stays 1 on a held pose (15 of 15 read 1, same pose hash on both
// reads), so only a pose hash that differs between read 1 and read 2 is a WARN (a loop or restart).
void rest_check_tick(ULONGLONG now) {
    for (int32_t i = 0; i < g_rest.n; ++i) {
        RestRow& r = g_rest.rows[i];
        if (r.check_at == 0 || now < r.check_at) {
            continue;
        }
        r.check_at = 0;
        --g_rest.checks_due;
        void* const npc = c31_weak_get(r.actor);
        void* const player = npc != nullptr ? read_ptr(npc, kRestSeqPlayer) : nullptr;
        const auto is_playing = reinterpret_cast<SeqIsPlayingFn>(
            player != nullptr ? read_ptr(read_ptr(player, 0), kRestVtIsPlaying) : nullptr);
        if (is_playing == nullptr) {
            continue; // the copy or its player is gone: nothing to read
        }
        // Senior 2026-11-19 (c502b269a guid 0xf16: its ragdoll started about 2 s before read-back 2): a copy now in a
        // ragdoll or with a pose row moves from physics, so its read-back says nothing about a replayed death.
        void* const rest_mesh = read_ptr(npc, kCharacterMesh);
        if (pose_find(r.guid) != nullptr || (rest_mesh != nullptr && c39_mesh_simulating(rest_mesh))) {
            if (note_event_cap(g_rest.lines, kRestLines, "card269 rest")) {
                log::line("card269: client rest read-back %u skipped for guid 0x%llx: %s", static_cast<unsigned>(r.reads + 1),
                          static_cast<unsigned long long>(r.guid), pose_find(r.guid) != nullptr ? "pose row" : "mesh simulating");
            }
            continue;
        }
        const bool playing = is_playing(player, r.played);
        const bool first = r.reads == 0;
        g_rest.checks += first ? 1u : 0u;
        g_rest.still_playing += first && playing ? 1u : 0u;
        r.reads = static_cast<uint8_t>(r.reads + 1);
        if (first) {
            r.check_at = now + kRestCheck2Ms;
            ++g_rest.checks_due;
        }
        const void* const animator = read_ptr(npc, kRestAnimator);
        int32_t bones = 0;
        const uint64_t pose = rest_pose_hash(read_ptr(npc, kCharacterMesh), bones);
        char mesh[256];
        rest_mesh_text(npc, mesh, sizeof(mesh)); // b131
        // b131 (senior, card 269 cause): the deferred static-to-skeletal swap's frame count (animator +0x15C, counted
        // down by its TickComponent) and whether that component tick is enabled.
        char restored[64] = "";
        if (animator != nullptr) {
            snprintf(restored, sizeof(restored), "; animator swap frames left %d, animator tick enabled %d",
                     read_i32(animator, kRestSwapFrames), pose_cmc_ticking(animator) ? 1 : 0);
        }
        r.pose1 = first ? pose : r.pose1;
        const bool moved = !first && pose != r.pose1;
        g_rest.pose_moved += moved ? 1u : 0u;
        if (moved ? note_event_cap(g_rest.warn_lines, kRestWarnLines, "card269 rest warn")
                  : note_event_cap(g_rest.lines, kRestLines, "card269 rest")) {
            log::line("%scard269: client rest read-back %u %s guid 0x%llx seq %s: IsPlayingAnimation %d, base state %u; "
                      "animator static mode %d, time %.2f of %.2f s; pose hash %016llx over %d bones%s; %s%s",
                      moved ? "WARN " : "", static_cast<unsigned>(r.reads), class_name(npc).text,
                      static_cast<unsigned long long>(r.guid), r.seq, playing ? 1 : 0,
                      static_cast<const uint8_t*>(npc)[kNpcBaseState],
                      animator != nullptr ? static_cast<const uint8_t*>(animator)[kRestStaticMode] : -1,
                      animator != nullptr ? static_cast<double>(read_f32(animator, kRestStaticTime)) : -1.0,
                      animator != nullptr ? static_cast<double>(read_f32(animator, kRestStaticLen)) : -1.0,
                      static_cast<unsigned long long>(pose), bones, moved ? " (read-back 1 differs)" : "", mesh,
                      restored);
        }
    }
}

void rest_map_line() {
    if (g_rest.noted + g_rest.received + g_rest.arrivals == 0) {
        return;
    }
    log::line("%scard269: %s rest rows map totals: host noted %u, added %u (no name %u, owed %u, owed sent %u), sent "
              "%u, removed %u (left Dead %u), replayed %u; client received %u (bad %u), removed %u, arrivals %u, "
              "played %u (not played %u), read-backs %u (still playing %u, pose moved %u); peak %d of %d; card291 "
              "host death rows sent %u, not sent %u",
              g_rest.no_name + g_rest.not_played + g_rest.pose_moved + g_rest.bad != 0 || g_rest.full_warned
                  ? "WARN "
                  : "",
              g_sprite.side != nullptr ? g_sprite.side : side(), g_rest.noted, g_rest.added, g_rest.no_name, g_rest.owed, g_rest.owed_sent, g_rest.sent,
              g_rest.removed, g_rest.left_dead, g_rest.replayed, g_rest.received, g_rest.bad, g_rest.removed_rx,
              g_rest.arrivals, g_rest.played, g_rest.not_played, g_rest.checks, g_rest.still_playing, g_rest.pose_moved,
              g_rest.peak, kRestRows, g_rest.death_sent, g_rest.death_unsent);
}

// Card 291, client: the map's death rows and resolved ragdoll waits (median of the first kC291Waits), then reset.
// From c152_pose_wait_clear_all.
void c291_client_totals() {
    uint32_t no_row = 0;
    for (int32_t i = 0; i < kC291Deaths; ++i) {
        no_row += g_c291.d[i].sequence && g_c291.d[i].host_name == 0 ? 1u : 0u;
    }
    const int32_t n = g_c291.wait_n;
    if (n + static_cast<int32_t>(g_c291.rows + no_row) != 0) {
        uint32_t* const w = g_c291.wait_ms;
        for (int32_t i = 1; i < n; ++i) { // insertion sort, n <= kC291Waits
            const uint32_t v = w[i];
            int32_t j = i - 1;
            for (; j >= 0 && w[j] > v; --j) {
                w[j + 1] = w[j];
            }
            w[j + 1] = v;
        }
        log::line("card291: client ragdoll waits %d, median %u ms, worst %u ms; death rows %u: same %u, replayed %u, "
                  "played from the row %u, copy not resolved %u, no FName %u; death edges with no row (last %d) %u",
                  n, n > 0 ? w[n / 2] : 0u, n > 0 ? w[n - 1] : 0u, g_c291.rows, g_c291.same, g_c291.replayed,
                  g_c291.from_row, g_c291.unresolved, g_c291.unnamed, kC291Deaths, no_row);
    }
    if (g_c291.c524_resets != 0) {
        log::line("card524: client death edges: %u meshes put back on their actor, %u with a smoothing offset over %.0f "
                  "uu, largest %.1f uu", g_c291.c524_resets, g_c291.c524_over, kC524OffUu, g_c291.c524_max);
    }
    g_c291 = C291Client{};
}

} // namespace

bool c132_ragdoll_root(const void* mesh, Vec3& out) {
    int32_t count = 0;
    void* const* const bodies = mesh != nullptr ? pose_bodies(mesh, count) : nullptr;
    Xform x{};
    if (count <= 0 || g_body_simulating == nullptr || !g_body_simulating(bodies[0]) || !pose_world(bodies[0], x)) {
        return false;
    }
    out = Vec3{x.t[0], x.t[1], x.t[2]};
    return true;
}

// Client, card 140: body 0 of a copy a host pose row holds. Such a copy's bodies are kinematic (the row froze them),
// so c132_ragdoll_root says no while the corpse lies where the host's ragdoll went, not at the actor.
bool c132_posed_root(const void* npc, Vec3& out) {
    const void* const mesh = npc != nullptr ? read_ptr(npc, kCharacterMesh) : nullptr;
    int32_t count = 0;
    void* const* const bodies = mesh != nullptr && pose_find_actor(npc) != nullptr ? pose_bodies(mesh, count) : nullptr;
    Xform x{};
    if (bodies == nullptr || count <= 0 || !pose_world(bodies[0], x)) {
        return false;
    }
    out = Vec3{x.t[0], x.t[1], x.t[2]};
    return true;
}

bool c269_swap_back(void* npc); // enemies.cpp

// Card 137 (hb70): client, a copy first seen already dead (c38_death_edge, corpse, not gibbed). Its death is not played
// (the user saw the corpses that died before the join pile at the spawn and die again). The mesh is hidden and held
// until the host's pose row applies (at once when the row is already here), so it first shows where the host's lies.
void c132_pose_arrival(void* npc) {
    if (g_client_session && npc != nullptr && g_is_npc_2d != nullptr && g_is_npc_2d(npc)) {
        sprite_arrival(npc); // card 177: no physics bodies, so no pose row; the host's death sequence instead
        return;
    }
    void* const mesh = npc != nullptr ? read_ptr(npc, kCharacterMesh) : nullptr;
    if (!g_client_session || mesh == nullptr) {
        return;
    }
    ++g_arrive.seen;
    c269_swap_back(npc); // before the hidden read below
    if (g_arrive.n >= kArrivals) {
        ++g_arrive.full;
        c38_corpse_death_play(npc);
        ++g_arrive.death_plays;
        log::line("WARN card137: client corpse arrival %s: arrival list full (%d), the death visual played",
                  object_name(npc).text, kArrivals);
        return;
    }
    PoseArrival& a = g_arrive.a[g_arrive.n++];
    a = PoseArrival{};
    c31_weak_set(a.actor, npc);
    a.guid = c41_guid(npc);
    a.first = GetTickCount64();
    const bool hidden = (static_cast<const uint8_t*>(mesh)[kCompHiddenByte] & kCompHiddenBit) != 0;
    a.hid = !hidden && g_orig_c132_set_hidden != nullptr;
    if (a.hid) {
        g_orig_c132_set_hidden(mesh, true, 0);
    }
    const char* const why = arrival_apply(a.guid);
    if (why != nullptr && note_event_cap(g_pose.lines, kPoseLines, "card132 pose")) {
        log::line("card137: client corpse arrival %s guid 0x%llx held %s: %s", object_name(npc).text,
                  static_cast<unsigned long long>(a.guid), a.hid ? "hidden" : "(mesh already hidden)", why);
    }
}

// Card 318, client: a revived copy leaves its sprite death at once: its held row and its arrival wait are dropped
// without waiting for the host's "R" row (sent at the host's next pose step). Returns how many entries went.
uint32_t c318_sprite_revive(void* npc) {
    const uint64_t guid = c41_guid(npc);
    uint32_t dropped = 0;
    SpriteRow* const r = sprite_find(guid, npc);
    if (r != nullptr) {
        sprite_remove(r);
        ++g_sprite.removed_rx;
        ++dropped;
    }
    for (int32_t i = g_sprite.waits_n; i-- > 0;) {
        const SpriteWait& w = g_sprite.waits[i];
        if ((guid != 0 && w.guid == guid) || c31_weak_get(w.actor) == npc) {
            sprite_wait_remove(i);
            ++dropped;
        }
    }
    if (dropped != 0 && guid != 0) { // the host's R for it still comes (its next pose step): sprite_drop answers it
        g_sprite.revive_drops[g_sprite.revive_next++ % kSpriteReviveDrops] = guid;
    }
    return dropped;
}

// Card 318: the sequence a sprite NPC's animator plays now (or why it is unread) with its frame and count. Returns
// 1 for a death sequence, 0 for another, -1 when unread.
int c318_sprite_now(const void* npc, char* seq, size_t size, int32_t* frame, int32_t* frames) {
    const SpriteState s = sprite_read(npc);
    snprintf(seq, size, "%s", s.why == kSrRejects ? s.seq : kSrName[s.why]);
    *frame = s.frame;
    *frames = s.frames;
    return s.why != kSrRejects ? -1 : sprite_death_name(s.seq) ? 1 : 0;
}

// Card 177, host (detour_try_play_death, after the original): a sprite NPC's death sequence started. One row per
// death with the name the animator resolved (a variant pick included), sent to every ponged PC.
void c177_sprite_death_host(void* npc) {
    if (g_client_session || npc == nullptr || g_is_npc_2d == nullptr || !g_is_npc_2d(npc)) {
        return;
    }
    SpriteRow* const held = sprite_find(0, npc);
    if (held != nullptr) { // a death sequence started again on a corpse that has its row: the newest is sent
        sprite_host_update(*held, npc, g_actor_get_world(npc), GetTickCount64(), "at a second death start");
        return;
    }
    const SpriteState s = sprite_read(npc);
    const SpriteReject why = s.why != kSrRejects ? s.why : !sprite_death_name(s.seq) ? kSrNotDeath : kSrRejects;
    SpriteRow* const r = why == kSrRejects ? sprite_add() : nullptr;
    if (r == nullptr) {
        sprite_reject(why != kSrRejects ? why : kSrFull, npc, "host");
        return;
    }
    const ULONGLONG now = GetTickCount64();
    c31_weak_set(r->actor, npc);
    r->guid = c41_guid(npc);
    r->at = now;
    r->ms = s.ms;
    r->frame = s.frame;
    r->frames = s.frames;
    memcpy(r->seq, s.seq, sizeof(r->seq));
    ++g_sprite.added;
    g_sprite.owed += r->guid == 0 ? 1u : 0u;
    const int32_t sent = r->guid != 0 ? sprite_send(*r, nullptr, g_actor_get_world(npc), now) : 0;
    g_sprite.sent += sent > 0 ? 1u : 0u;
    if (note_event_cap(g_sprite.lines, kSpriteLines, "card177 sprite")) {
        const Vec3 at = actor_location(npc);
        log::line("card177: host sprite death %s guid 0x%llx seq %s (%d frames, %u ms), sent to %d PCs%s; Health %.1f "
                  "at %.0f %.0f %.0f", class_name(npc).text, static_cast<unsigned long long>(r->guid), r->seq,
                  r->frames, r->ms, sent, r->guid == 0 ? " (no NetGUID yet: owed)" : "",
                  read_f32(npc, kNpcCurrentHealth), at.x, at.y, at.z);
    }
}

// Card 269, host (detour_try_play_death, after the original): a 3D NPC's death name, the player's current sequence.
void c269_death_note(void* npc) {
    if (g_client_session || npc == nullptr || g_is_npc_2d == nullptr || g_is_npc_2d(npc)) {
        return;
    }
    c506_dead_note(npc, "death note"); // card 506
    DeathNote& d = g_rest.notes[g_rest.notes_next];
    g_rest.notes_next = (g_rest.notes_next + 1) % kDeathNotes;
    c31_weak_set(d.actor, npc);
    d.name = c41_current(npc);
    d.at = GetTickCount64();
    ++g_rest.noted;
    // Card 291 (b187): the death name goes to every ponged PC now, so the copy plays the host's variant.
    const uint64_t guid = c41_guid(npc);
    char seq[kSpriteName];
    char text[64];
    if (guid == 0 || !rest_name_text(d.name, seq, sizeof(seq))) {
        ++g_rest.death_unsent;
        return;
    }
    snprintf(text, sizeof(text), "D %llx %s", static_cast<unsigned long long>(guid), seq);
    g_rest.death_sent += c95_send_all(g_actor_get_world(npc), ChanOp::Pose, text).sent > 0 ? 1u : 0u;
}

// Card 291 (b187), client (c38_death_edge): the copy's death edge time, for the held ragdoll row. `play` (a 3D copy
// with no bug animator): true when the host's death row came first and its sequence was played here, from the start;
// the caller then skips the game's own pick (TryPlayDeathAnimation). False: the caller plays its own, and the row,
// when it comes, compares (c291_receive).
// Card 524, client (c291_death_edge): a dying copy's movement stops ticking, so SmoothClientPosition no longer decays
// the network smoothing offset that every later replicated move still adds (SmoothCorrection): the mesh stays where
// the copy was while the actor follows the host's corpse (c247b309a HopperBug mesh up to 2665 uu above its actor).
// At the edge the offset is zeroed, the mesh goes back to the character's base offset (a teleport, rotation kept),
// and smoothing is turned off (Disabled: a correction moves the capsule only), so the mesh rides its actor from here.
void c524_mesh_reset(void* npc, uint64_t guid) {
    auto* const mesh = static_cast<uint8_t*>(read_ptr(npc, kCharacterMesh));
    auto* const cmc = static_cast<uint8_t*>(read_ptr(npc, kCharacterMovement));
    if (mesh == nullptr || cmc == nullptr || g_set_relative_location == nullptr) {
        return;
    }
    auto* const data = static_cast<uint8_t*>(read_ptr(cmc, kCmcClientData));
    const Vec3 off = data != nullptr ? read_vec3(data, kClientMeshTranslationOffset) : Vec3{};
    const Vec3 base = read_vec3(npc, kCharacterBaseTranslation);
    const double actor_z = actor_location(npc).z;
    const double before = read_vec3(mesh, kComponentTranslation).z - actor_z;
    const double rel = vec_dist(read_vec3(mesh, kCompRelativeLocation), base);
    const uint8_t mode = cmc[kCmcSmoothingMode];
    if (data != nullptr) {
        memset(data + kClientMeshTranslationOffset, 0, sizeof(Vec3));
    }
    cmc[kCmcSmoothingMode] = 0;
    alignas(16) uint8_t hit[0x200] = {}; // FHitResult out (0xE8 here); unused with no sweep
    g_set_relative_location(mesh, &base, false, hit, true);
    const double after = read_vec3(mesh, kComponentTranslation).z - actor_z;
    const double len = vec_dist(off, Vec3{});
    ++g_c291.c524_resets;
    g_c291.c524_over += len > kC524OffUu ? 1u : 0u;
    g_c291.c524_max = fmax(g_c291.c524_max, len);
    if (g_c291.c524_lines < kC524Lines) {
        ++g_c291.c524_lines;
        log::line("card524: client death edge %s guid 0x%llx: smoothing mode %u, offset (%.0f, %.0f, %.0f)%s; mesh %.0f "
                  "uu from its base offset; mesh minus actor z %.0f before, %.0f after",
                  class_name(npc).text, static_cast<unsigned long long>(guid), mode, off.x, off.y, off.z,
                  data != nullptr ? "" : " (no client data)", rel, before, after);
    }
}

bool c291_death_edge(void* npc, bool play) {
    const uint64_t guid = g_client_session && npc != nullptr ? c41_guid(npc) : 0;
    if (guid == 0) {
        return false;
    }
    c524_mesh_reset(npc, guid);
    C291Death& d = c291_entry(guid);
    d.edge_at = GetTickCount64();
    d.sequence = play && g_is_npc_2d != nullptr && !g_is_npc_2d(npc);
    if (!d.sequence || d.host_name == 0 || g_orig_c41_try_play == nullptr) {
        return false;
    }
    g_orig_c41_try_play(npc, d.host_name, true);
    ++g_c291.from_row;
    c291_line(guid, 0, d.host_name, "played from the row");
    return true;
}

// Card 207 step 3: the row text of a corpse the host shows with no death animation and no ragdoll (spawned dead by a
// checkpoint load after a ragdoll death: c207b156a, 2 of 3 corpses had no current sequence). The client shows its
// copy as it is and starts nothing on it. kRestBare ("None") is never a death name.
void rest_host_row(void* npc, const char* seq, ULONGLONG at, ULONGLONG now);

// Card 269, host (detour_c269_state, after ReceiveBaseStateChanged): a 3D corpse reaching Dead with no pose row (no
// ragdoll) gets a rest row, sent to every ponged PC. The name and time come from its newest death note.
void c269_state_host(void* npc, uint8_t new_state) {
    if (g_client_session || npc == nullptr || new_state != kBaseStateDead || g_is_npc_2d == nullptr ||
        g_is_npc_2d(npc) || pose_find_actor(npc) != nullptr || rest_find(0, npc) != nullptr) {
        return;
    }
    const DeathNote* note = nullptr;
    for (int32_t k = 1; k <= kDeathNotes && note == nullptr; ++k) { // newest first
        const DeathNote& d = g_rest.notes[(g_rest.notes_next - k + kDeathNotes) % kDeathNotes];
        note = c31_weak_get(d.actor) == npc ? &d : nullptr;
    }
    const ULONGLONG now = GetTickCount64();
    const uint64_t name = note != nullptr && note->name != 0 ? note->name : c41_current(npc);
    char seq[kSpriteName] = {};
    if (!rest_name_text(name, seq, sizeof(seq))) {
        ++g_rest.no_name;
        if (note_event_cap(g_rest.warn_lines, kRestWarnLines, "card269 rest warn")) {
            log::line("WARN card269: host rest row %s guid 0x%llx not added: no death name (%s, FName 0x%llx)",
                      class_name(npc).text, static_cast<unsigned long long>(c41_guid(npc)),
                      note != nullptr ? "noted" : "no death note", static_cast<unsigned long long>(name));
        }
        return;
    }
    rest_host_row(npc, seq, note != nullptr ? note->at : now, now);
}

// Host: a new rest row for `npc` with death name text `seq` (kRestBare: none), started at `at`; sent to every ponged
// PC, or owed until the corpse has a NetGUID.
void rest_host_row(void* npc, const char* seq, ULONGLONG at, ULONGLONG now) {
    RestRow* const r = rest_add();
    if (r == nullptr) {
        return;
    }
    c31_weak_set(r->actor, npc);
    r->guid = c41_guid(npc);
    r->at = at;
    snprintf(r->seq, sizeof(r->seq), "%s", seq);
    ++g_rest.added;
    g_rest.owed += r->guid == 0 ? 1u : 0u;
    const int32_t sent = r->guid != 0 ? rest_send(*r, nullptr, g_actor_get_world(npc), now) : 0;
    g_rest.sent += sent > 0 ? 1u : 0u;
    if (note_event_cap(g_rest.lines, kRestLines, "card269 rest")) {
        log::line("card269: host rest row %s guid 0x%llx seq %s, sent to %d%s", class_name(npc).text,
                  static_cast<unsigned long long>(r->guid), r->seq, sent,
                  r->guid == 0 ? " (no NetGUID yet: owed)" : "");
    }
}

// Card 207, host (detour_c132_set_hidden, an NPC mesh shown again): a corpse the game shows with no ragdoll and no
// death edge in this session (spawned dead by a checkpoint load) reaches no ReceiveBaseStateChanged, so it gets its
// rest row here. c269_state_host skips a 2D NPC and a corpse that already has a pose or rest row.
void c207_shown_corpse(void* npc, const void* mesh) {
    if (g_client_session || npc == nullptr || g_npc_class == nullptr || !is_a(npc, g_npc_class()) ||
        g_is_npc_2d == nullptr || g_is_npc_2d(npc)) {
        return; // a 2D NPC's corpse is a sprite row (card 177), never a rest row
    }
    float health = 0.0f;
    if (c25_dead(npc, &health) != 1 || c39_mesh_simulating(mesh) || pose_find_actor(npc) != nullptr ||
        rest_find(0, npc) != nullptr) {
        return;
    }
    char seq[kSpriteName] = {};
    const bool named = rest_name_text(c41_current(npc), seq, sizeof(seq));
    if (named) {
        c269_state_host(npc, kBaseStateDead);
    } else {
        const ULONGLONG now = GetTickCount64();
        rest_host_row(npc, kRestBare, now, now); // step 3: no death name, the row still owns the copy
    }
    if (note_event_cap(g_rest.lines, kRestLines, "card269 rest")) {
        log::line("card207: host corpse shown without a ragdoll or a death edge: %s guid 0x%llx, base state %u, "
                  "health %.1f, rest row %d, death name %d",
                  class_name(npc).text, static_cast<unsigned long long>(c41_guid(npc)),
                  static_cast<const uint8_t*>(npc)[kNpcBaseState], static_cast<double>(health),
                  rest_find(0, npc) != nullptr ? 1 : 0, named ? 1 : 0);
    }
}

// Card 506 (c243b273a: a host AA turret died with no TryPlayDeathAnimation, no Dead base state and no ragdoll, so it
// had no pose or rest row; its copy, sent again dead, was hidden 9 s and then played its death again). Host: a 3D NPC
// dead by health is noted here (health edge, death event, start dying, death note); kC506SettleMs later, still dead
// with no pose row (a ragdoll), no rest row (the Dead state's) and a mesh not simulating, it gets a rest row: its
// death name when it has one (c269_state_host), else kRestBare.
namespace {
constexpr int32_t kC506Pending = 32;
constexpr ULONGLONG kC506SettleMs = 5000; // the Dead state's own row and a ragdoll's pose row come well before this
struct C506Pending {
    C31Weak actor;
    ULONGLONG at;
    const char* path;
};
C506Pending g_c506[kC506Pending] = {};
int32_t g_c506_n = 0;
uint32_t g_c506_full = 0;
} // namespace

void c506_dead_note(void* npc, const char* path) {
    float health = 0.0f;
    if (g_client_session || npc == nullptr || g_is_npc_2d == nullptr || g_is_npc_2d(npc) ||
        c25_dead(npc, &health) != 1 || pose_find_actor(npc) != nullptr || rest_find(0, npc) != nullptr) {
        return;
    }
    for (int32_t i = 0; i < g_c506_n; ++i) {
        if (c31_weak_get(g_c506[i].actor) == npc) {
            return; // noted already
        }
    }
    if (g_c506_n == kC506Pending) {
        ++g_c506_full;
        return;
    }
    C506Pending& p = g_c506[g_c506_n++];
    c31_weak_set(p.actor, npc);
    p.at = GetTickCount64();
    p.path = path;
}

namespace {
// Host, each pose step: due notes become rest rows or are dropped; one row per add.
void c506_pending_step(ULONGLONG now) {
    for (int32_t i = g_c506_n - 1; i >= 0; --i) {
        C506Pending& p = g_c506[i];
        void* const npc = c31_weak_get(p.actor);
        if (npc != nullptr && now - p.at < kC506SettleMs) {
            continue;
        }
        const char* const path = p.path;
        g_c506[i] = g_c506[--g_c506_n];
        float health = 0.0f;
        if (npc == nullptr || c25_dead(npc, &health) != 1 || pose_find_actor(npc) != nullptr ||
            rest_find(0, npc) != nullptr || read_ptr(npc, kCharacterMesh) == nullptr ||
            c39_mesh_simulating(read_ptr(npc, kCharacterMesh))) {
            continue;
        }
        char seq[kSpriteName] = {};
        const bool named = rest_name_text(c41_current(npc), seq, sizeof(seq));
        if (named) {
            c269_state_host(npc, kBaseStateDead);
        }
        if (rest_find(0, npc) == nullptr) {
            rest_host_row(npc, kRestBare, p.at, now); // no death name (or none from the note): the row owns the copy
        }
        log::line("card506: host rest row added by %s: %s guid 0x%llx, base state %u, health %.1f, seq %s, %u note(s) "
                  "lost to a full table", path, class_name(npc).text, static_cast<unsigned long long>(c41_guid(npc)),
                  static_cast<const uint8_t*>(npc)[kNpcBaseState], static_cast<double>(health),
                  rest_find(0, npc) != nullptr ? rest_find(0, npc)->seq : "none", g_c506_full);
    }
}
} // namespace

// Card 291 step 1b: a reflected bool of `object` as 0 or 1, or -1 when the class has no such bool property.
int32_t c291_bool(const void* object, const char* name) {
    const uint8_t* const f = prop_find_prop(read_ptr(object, kObjectClass), name);
    if (f == nullptr || !prop_bool_ok(f)) {
        return -1;
    }
    const auto* const base = static_cast<const uint8_t*>(object) + read_i32(f, kPropertyOffset);
    return (base[f[kBoolByteOffset]] & f[kBoolFieldMask]) != 0 ? 1 : 0;
}

// Card 291 step 1b, host: at the ragdoll start (the death animation's EnableRagdoll notify), how long after the
// death start it came and whether the host's own view could have held the death animation back: rendered within
// 0.2 s and 2 s, the tick option (above 1: no pose tick when not rendered), update rate optimisation and the
// animation budget flags. -1: the property is not on this mesh class.
void c291_host_line(const void* npc, const void* mesh, uint64_t guid, ULONGLONG now) {
    if (!note_event_cap(g_pose.c291_lines, kC291Lines, "card291 ragdoll start")) {
        return;
    }
    const RestRow* const rest = rest_find(0, npc);
    const uint8_t* const opt = prop_find_prop(read_ptr(mesh, kObjectClass), "VisibilityBasedAnimTickOption");
    const int32_t tick_option =
        opt != nullptr ? static_cast<const uint8_t*>(mesh)[read_i32(opt, kPropertyOffset)] : -1;
    const bool seen_02 = g_actor_recently_rendered != nullptr && g_actor_recently_rendered(npc, 0.2f);
    const bool seen_2 = g_actor_recently_rendered != nullptr && g_actor_recently_rendered(npc, 2.0f);
    // b8 (senior review of step 1b): tick disabled is candidate 2 (significance), the skeletal mesh hidden is
    // candidate 3 (static-mesh mode). The significance value has no known offset; a reflected property of that name
    // is shown when the class has one ("-" otherwise).
    void* const m = const_cast<void*>(mesh);
    const int32_t tick_on = c297_bool_call(m, g_pose.c291_tick_fname, L"IsComponentTickEnabled");
    const int32_t visible = c297_bool_call(m, g_pose.c291_visible_fname, L"IsVisible");
    char significance[64];
    c22_prop_text(npc, "Significance", significance, sizeof(significance));
    const Vec3 sc = read_vec3(mesh, kComponentScale); // b12 item 4: compared per guid with the client card152 scale
    log::line("card291: host t=%llu guid 0x%llx %s ragdoll %lld ms after its death start; rendered in 0.2 s %d, in 2 "
              "s %d; mesh %s tick option %d, update rate optimisations %d, budget allocator %d, auto significance %d, "
              "actor rendered flag %d; mesh tick enabled %d, mesh visible %d, significance %s; component scale "
              "(%.3f, %.3f, %.3f)",
              static_cast<unsigned long long>(now), static_cast<unsigned long long>(guid), class_name(npc).text,
              rest != nullptr ? static_cast<long long>(now - rest->at) : -1LL, seen_02 ? 1 : 0, seen_2 ? 1 : 0,
              class_name(mesh).text, tick_option, c291_bool(mesh, "bEnableUpdateRateOptimizations"),
              c291_bool(mesh, "bAutoRegisterWithBudgetAllocator"), c291_bool(mesh, "bAutoCalculateSignificance"),
              c291_bool(mesh, "bShouldUseActorRenderedFlag"), tick_on, visible, significance, sc.x, sc.y, sc.z);
}

// Host: a mesh started its ragdoll (detour_c132_set_simulate). One row per corpse; a second start replaces nothing.
void c132_pose_track(void* mesh) {
    void* const npc = mesh != nullptr ? read_ptr(mesh, kComponentOwner) : nullptr;
    if (g_client_session || npc == nullptr || read_ptr(npc, kCharacterMesh) != mesh || !pose_fns()) {
        return;
    }
    // A corpse with no NetGUID yet (it died before any client connected) is tracked by its actor; pose_guid_check
    // reads the guid again each step.
    const uint64_t guid = c41_guid(npc);
    PoseRow* r = pose_find_actor(npc);
    if (r == nullptr && g_pose.n >= kPoseRows) {
        ++g_pose.dropped;
        return;
    }
    const bool fresh = r == nullptr;
    if (fresh) {
        r = &g_pose.row[g_pose.n++];
        *r = PoseRow{};
        r->guid = guid;
        c31_weak_set(r->actor, npc);
        ++g_pose.tracked;
        g_pose.no_guid += guid == 0 ? 1u : 0u;
    }
    r->state = PoseState::Moving; // a repeat start resends on its next settle
    r->start = GetTickCount64();
    r->still_ms = 0;
    c291_host_line(npc, mesh, guid, r->start);
    // Card 152: sample 0 goes out with the start (the client holds its copy's own start for it), unless the channel is
    // backed up; a row with no NetGUID yet goes to no client.
    const void* const world = g_actor_get_world(npc);
    if (guid != 0 && pose_stream_backlog(world) <= kPoseStreamBacklog) {
        pose_stream_one(world, *r, r->start);
    } else {
        g_pose.st_backlog_skips += guid != 0 ? 1u : 0u;
    }
}

// Card 194, client: a held frozen copy the game started again moved with the game's physics until the rehold; the
// stored snap is applied again on the next client step through the normal path (pose_try_apply, then
// pose_freeze_step and its check, which logs the card194 line).
void c194_repose(PoseRow& r, void* mesh) {
    if (r.state != PoseState::Applied || r.blob_n == 0) {
        return; // a row in flight is applied by its own path
    }
    void* m = mesh;
    r.c194_before = static_cast<float>(pose_far_now(r, m).worst);
    r.c194_owed = true;
    r.state = PoseState::Pending;
    r.got = GetTickCount64(); // the kPosePendingMs give-up counts from here
}

// Card 137: the game turned a corpse's simulation off or on (detour_c132_all_bodies, after the call). Host: a tracked
// row is snapped and sent at once, frozen or not (pose_snapshot reads the flag from the mesh), and the snap replaces
// the row's blob in g_pose.row, which the join replay sends. An unfreeze snap goes out unsettled and the settled one
// follows. Client: the game's own freeze of a copy is counted (hb67: 0 such calls; the host's freeze is host logic).
// Card 339, host: `was_simulating` is body 0's flag before the call (1, 0, -1 none). A (1) on a row whose body 0 was
// already simulating, with a snap out, not stopped or held by card 189, changes nothing the client has: no clock
// restart, no snap (run 3: a blueprint called it on a settled corpse every 0.4 to 1.7 s, two snaps each). Bodies
// the call wakes and moves are sent by pose_step's moved-root resnap and the card 152 stream. A row whose bodies moved
// kPoseStill or more since its last snap (r->woke, any limb) is not skipped: the call's snap carries the limbs.
void c132_pose_sim_change(void* mesh, bool simulate, int was_simulating) {
    void* const npc = mesh != nullptr ? read_ptr(mesh, kComponentOwner) : nullptr;
    if (npc == nullptr || read_ptr(npc, kCharacterMesh) != mesh || !pose_fns()) {
        return;
    }
    if (g_client_session) {
        g_pose.own_freezes += simulate ? 0u : 1u;
        PoseRow* const own = pose_find_actor(npc);
        // Card 152 r2, finding 11: the game's own SetAllBodiesSimulatePhysics(1) on a streaming copy is wanted (the
        // copy simulates); its gravity is set off again, since the game's Wake Ragdoll turns it on. A card 140 frozen
        // copy turned back on is held again (simulation off, the hold's settings rewritten; the settings saved at the
        // first hold are kept for pose_release).
        const bool regrav = simulate && own != nullptr && own->grav_off;
        const bool rehold = simulate && own != nullptr && !own->grav_off && own->held;
        if (regrav) {
            g_pose_mesh_gravity(mesh, false);
            ++g_pose.st_reholds;
        }
        if (rehold) {
            g_orig_c132_all_bodies(mesh, false);
            pose_hold_bits(mesh);
            ++g_pose.st_frozen_reholds;
            c194_repose(*own, mesh);
        }
        if (note_event_cap(g_pose.lines, kPoseLines, "card132 pose")) {
            log::line("card140: client pose own SetAllBodiesSimulatePhysics(%d) on %s guid 0x%llx (row %s)%s",
                      simulate ? 1 : 0, object_name(npc).text,
                      static_cast<unsigned long long>(own != nullptr ? own->guid : 0),
                      own == nullptr ? "none" : own->state == PoseState::Applied ? "applied" : "not applied",
                      regrav       ? "; card152 streaming copy, gravity off again"
                      : rehold     ? "; card140 frozen copy held again"
                                   : "");
        }
        return;
    }
    PoseRow* const r = pose_find_actor(npc);
    int32_t count = 0;
    void* const* const bodies = r != nullptr ? pose_bodies(mesh, count) : nullptr;
    if (bodies == nullptr || count <= 0 || count > kPoseBodies) {
        return;
    }
    if (simulate && was_simulating == 1 && r->state == PoseState::Sent && !r->c189_stopped && !r->held && !r->woke) {
        ++g_pose.sim_unchanged;
        if (!r->sim_skip_logged) {
            r->sim_skip_logged = true;
            log::line("card339: host guid 0x%llx unchanged simulate call skipped (first)",
                      static_cast<unsigned long long>(r->guid));
        }
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (simulate) {
        r->start = now;
        r->still_ms = 0;
        r->unfreeze_next = true;
        pose_release(*r, mesh); // card 189: a corpse the host stopped and held gets its own settings back
        r->c189_stopped = false;
    }
    pose_snapshot(g_actor_get_world(npc), *r, bodies, count, now);
    if (r->state == PoseState::Sent) {
        g_pose.freezes += simulate ? 0u : 1u;
        g_pose.unfreezes += simulate ? 1u : 0u;
        log::line("card137: host pose %s guid 0x%llx snap %u sent at the game's SetAllBodiesSimulatePhysics(%d)",
                  simulate ? "unfreeze" : "freeze", static_cast<unsigned long long>(r->guid), r->snap,
                  simulate ? 1 : 0);
        if (!simulate) {
            c269_stream_line(*r, now, "the freeze"); // card 269 B
        }
    }
}

// Card 152 r3, client (detour_c132_all_bodies, before the call): the game's own SetAllBodiesSimulatePhysics(0) on a
// copy whose stream is live (gravity off, posed: Applied, or Freezing on a held final). c152-r2 guid 0x1da: the
// blueprint native freeze at exe+0x3B05087 stopped the copy between two samples, and its bones stayed where that tick
// left them while the stream moved the bodies (25.4 uu at the frozen final). The host's own freeze reaches the copy
// as its frozen final, so the client's call is not passed to the original. The original only sets the bodies'
// simulate flags (nothing it allocates or frees is skipped). True when the caller must not run the original.
bool c152_pose_game_freeze(void* mesh, uintptr_t caller_rva) {
    void* const npc = mesh != nullptr ? read_ptr(mesh, kComponentOwner) : nullptr;
    PoseRow* const r = npc != nullptr && read_ptr(npc, kCharacterMesh) == mesh ? pose_find_actor(npc) : nullptr;
    if (r == nullptr || (!r->grav_off && !r->own_fall) ||
        (r->state != PoseState::Applied && r->state != PoseState::Freezing)) {
        return false;
    }
    ++g_pose.st_freeze_blocked;
    if (note_event_cap(g_pose.st_freeze_block_lines, kPoseFreezeBlockLines, "card152 freeze block")) {
        log::line("card152: client blocked the game's SetAllBodiesSimulatePhysics(0) on streaming guid 0x%llx (sample "
                  "%u, %s), caller exe+0x%llX; %u blocked this map", static_cast<unsigned long long>(r->guid),
                  r->stream_seq, r->live ? "live" : "held final", static_cast<unsigned long long>(caller_rva),
                  g_pose.st_freeze_blocked);
    }
    return true;
}

// Card 140, client (detour_c132_all_bodies, before the call): the game's own SetAllBodiesSimulatePhysics(1) on a copy
// whose SetSimulatePhysics start waits for its stream row (card 152 wait table, fallback not run). The blueprint
// calls both in a row (ubergraph 1313 then 1396); with only the first held, the second set the bodies simulating
// from where they were last updated (no c39_sync_mesh ran), and the copy ragdolled on its own until the stream row
// (c423b163a guid 0x148: 515 ms, body 0 707 uu from the actor and 990 uu from the host's sample 0 root; 21 of 23
// held starts in that run were "already simulating" at their stream start). The stream start or the
// kPoseStartWaitMs fallback starts the copy through the card 039/132 start. True when the caller must not run the
// original.
bool c152_pose_game_start(void* mesh, uintptr_t caller_rva) {
    void* const npc = mesh != nullptr ? read_ptr(mesh, kComponentOwner) : nullptr;
    if (npc == nullptr || read_ptr(npc, kCharacterMesh) != mesh) {
        return false;
    }
    const uint64_t guid = c41_guid(npc);
    const PoseWait* const w = guid != 0 ? wait_find(guid) : nullptr;
    if (w == nullptr || w->started) {
        return false;
    }
    ++g_pose.st_wait_blocks;
    if (note_event_cap(g_pose.block_lines, kPoseBlockLines, "card146 pose block")) {
        log::line("card140: client blocked the game's SetAllBodiesSimulatePhysics(1) on waiting guid 0x%llx, caller "
                  "exe+0x%llX", static_cast<unsigned long long>(guid), static_cast<unsigned long long>(caller_rva));
    }
    return true;
}

// Card 207 step 3, client (detour_c132_all_bodies, before the call): the game's own SetAllBodiesSimulatePhysics(1) on
// a copy with a host rest row and no pose row. c207b156a guid 0x29c: the SetSimulatePhysics start was blocked, but
// the blueprint native call at exe+0x3B05087 set the bodies simulating for 6 s and the rest pose moved (read-back 2
// differed). The host's corpse does not simulate, so the call is not passed on. True when the caller must not run
// the original.
bool c207_pose_game_unfreeze(void* mesh, uintptr_t caller_rva) {
    void* const npc = mesh != nullptr ? read_ptr(mesh, kComponentOwner) : nullptr;
    if (npc == nullptr || read_ptr(npc, kCharacterMesh) != mesh || pose_find_actor(npc) != nullptr) {
        return false;
    }
    const uint64_t guid = c41_guid(npc);
    if (guid == 0 || rest_find(guid, nullptr) == nullptr) {
        return false;
    }
    ++g_pose.game_blocked;
    if (note_event_cap(g_pose.block_lines, kPoseBlockLines, "card146 pose block")) {
        log::line("card207: client blocked the game's SetAllBodiesSimulatePhysics(1) on rest-row guid 0x%llx, caller "
                  "exe+0x%llX", static_cast<unsigned long long>(guid), static_cast<unsigned long long>(caller_rva));
    }
    return true;
}

void c132_pose_step(void* engine) {
    const C252Timer timer(C252Class::PoseStep); // card 252 step 1b: pose apply retries and the stream tick
    const ULONGLONG now = GetTickCount64();
    g_sprite.side = side(); // card 177: the map's side for its totals line
    ++g_pose_frame;
    if (g_client_session && g_pose.n != 0 && pose_fns()) {
        pose_freeze_tick(); // per tick, before this tick's applies: rows posed on an earlier tick
    }
    if (g_client_session && g_arrive.n != 0 && pose_fns()) {
        arrival_tick(now); // per tick: the hidden ticks are counted and a ready row is applied at once
    }
    if (g_client_session && g_sprite.waits_n != 0) {
        sprite_wait_tick(now); // card 177, per tick: a sprite arrival's row applies at once
    }
    if (g_client_session && g_rest.checks_due > 0) {
        rest_check_tick(now); // card 269: the read-back one second after a rest play
    }
    if (g_client_session && pose_fns()) { // card 152, per tick: the local PC, held starts, streamed copies
        c31_weak_set(g_pose_pc, g_get_player_controller(g_get_game_world(engine), 0));
        pose_wait_tick(now);
        pose_stream_tick(now);
    }
    if (now < g_pose.next_step || !pose_fns()) {
        return;
    }
    g_pose.next_step = now + kPoseStepMs;
    if (g_client_session) {
        pose_client_step(now);
        return;
    }
    const void* const world = g_get_game_world(engine);
    if (world != nullptr) {
        pose_host_step(world, now);
        sprite_host_step(world, now); // card 177
        if (g_c506_n != 0) {
            c506_pending_step(now); // card 506
        }
        if (g_rest.n != 0) {
            rest_host_step(world, now); // card 269
        }
    }
}

void c132_receive_pose(void* pc, const WireString* s) {
    char text[kChanTextMax + 1];
    if (chan_text(s, text, sizeof(text)) <= 0) {
        ++g_pose.bad;
        c132_ledger_result(C132Result::Refused, "unreadable");
        return;
    }
    if (text[0] == 'R') {
        unsigned long long guid = 0;
        const bool read = sscanf_s(text, "R %llx", &guid) == 1;
        const size_t len = strlen(text);
        const char last = len >= 4 && text[len - 2] == ' ' ? text[len - 1] : '\0';
        const char table = last == 'S' || last == 'T' ? last : '\0'; // card 428: one table's release; none: all
        PoseRow* const r = read && table == '\0' ? pose_find(guid) : nullptr;
        if (table == '\0') {
            wait_clear(guid, "the host's release row", 0); // card 152
        }
        if (r != nullptr) {
            pose_remove(static_cast<int32_t>(r - g_pose.row));
            ++g_pose.removed_rx;
        }
        const bool sprite = table != 'T' && sprite_drop(guid); // card 177
        const bool rest = table != 'S' && rest_drop(guid);     // card 269
        c132_ledger_result(r != nullptr || sprite || rest ? C132Result::Applied : C132Result::Skipped,
                           r != nullptr || sprite || rest ? nullptr : "no row");
        return;
    }
    const char* reason = nullptr;
    const C132Result res = text[0] == 'P'   ? pose_part(pc, text, reason)
                           : text[0] == 'S' ? sprite_receive(text, reason)    // card 177
                           : text[0] == 'T' ? rest_receive(text, reason)      // card 269
                           : text[0] == 'D' ? c291_receive(pc, text, reason)  // card 291
                                            : C132Result::Refused;
    c132_ledger_result(res, text[0] == 'P' || text[0] == 'S' || text[0] == 'T' || text[0] == 'D' ? reason
                                                                                                   : "unknown frame");
}

namespace {

// Card 187, host: a moving row that is streaming sends its bodies now to the joining PC only, labelled with its last
// sample's number (no new sample: the row's seq, stream time and counters stay as they are). The stream's next sample
// goes to every ponged PC, this one included. A row with no NetGUID yet (pose_guid_check resolves it each step) or
// not streaming yet is skipped; false then.
bool pose_replay_stream(const void* world, PoseRow& r, void* pc) {
    pose_guid_check(world, r);
    if (r.guid == 0 || r.stream_seq == 0 || r.stream_base_n <= kPoseHeadBytes) {
        return false;
    }
    // Card 521 step 2: the last broadcast sample itself (r.stream_base), whole, so the joiner's delta base is every
    // other PC's. Its quaternions may differ from theirs by 1 int16 step (pack then unpack again), until the next
    // whole sample.
    const int32_t count = static_cast<int32_t>((r.stream_base_n - kPoseHeadBytes) / kPoseBodyBytes);
    const uint16_t seq = static_cast<uint16_t>(r.stream_seq - 1);
    const PoseSend s =
        pose_send_blob(PoseHead{r.guid, seq, count, kPoseStream}, r.stream_base, r.stream_base_n, pc, world);
    if (s.frames == 0) {
        return false;
    }
    float head[3];
    memcpy(head, r.stream_base, sizeof(head));
    const Vec3 root{head[0], head[1], head[2]};
    if (note_event_cap(g_pose.stream_lines, kPoseStreamLines, "card152 stream")) {
        log::line("card152: host replay stream guid 0x%llx sample %u to the joining PC: %d bodies, root (%.0f, %.0f, "
                  "%.0f); %d bytes in %d frames, %d sent", static_cast<unsigned long long>(r.guid), seq, count, root.x,
                  root.y, root.z, s.bytes, s.frames, s.sent);
    }
    c269_stream_line(r, GetTickCount64(), "a replay"); // card 269 B
    return true;
}

} // namespace

// Host: the current rows to a joining PC.
void c132_pose_replay(void* pc) {
    if (g_client_session || pc == nullptr) {
        return;
    }
    int32_t rows = 0;
    PoseSend all{};
    const void* const world = g_actor_get_world(pc);
    int32_t owed = 0;
    int32_t streams = 0;
    for (int32_t i = 0; i < g_pose.n; ++i) {
        if (g_pose.row[i].state == PoseState::Moving) {
            streams += pose_replay_stream(world, g_pose.row[i], pc) ? 1 : 0;
            continue;
        }
        if (g_pose.row[i].state != PoseState::Sent) {
            continue;
        }
        // A corpse that died before it had a NetGUID: if it has one now, pose_guid_check sends its owed snap to every
        // ponged PC (this one included); if not, it is sent when it does.
        const bool was_owed = g_pose.row[i].owed;
        if (!pose_guid_check(world, g_pose.row[i]) || was_owed) {
            owed += 1;
            continue;
        }
        const PoseSend s = pose_send(g_pose.row[i], pc, nullptr);
        if (note_event_cap(g_pose.lines, kPoseLines, "card132 pose")) {
            const uint8_t f = g_pose.row[i].flags;
            log::line("card137: host pose replay row guid 0x%llx snap %u %s",
                      static_cast<unsigned long long>(g_pose.row[i].guid), g_pose.row[i].snap,
                      (f & kPoseFrozen) != 0 ? "frozen" : (f & kPoseUnsettled) != 0 ? "unsettled" : "settled");
        }
        ++rows;
        all.frames += s.frames;
        all.bytes += s.bytes;
        all.sent += s.sent;
    }
    log::line("card132: host pose replay to a joining PC: %d rows (%d tracked), %d bytes in %d frames, %d sent; card137 "
              "%d owed rows (no NetGUID at the death: sent to all PCs when the corpse has one); card152 %d moving rows "
              "sent their current stream sample", rows, g_pose.n, all.bytes, all.frames, all.sent, owed, streams);
    sprite_replay(pc, GetTickCount64()); // card 177
    rest_replay(pc, GetTickCount64());   // card 269
}

// Host (card 140 Pose diag): each sent corpse against its own last sent snap, at each digest round. A body far from
// the snap means the host's corpse moved after the send and the client holds a stale pose (a send gap, not a client
// apply fault). Returns the rows over kPoseTolerance.
uint32_t c140_pose_host_check(uint32_t round) {
    uint32_t over = 0, checked = 0;
    for (int32_t i = 0; i < g_pose.n; ++i) {
        const PoseRow& r = g_pose.row[i];
        // Card 152 r3: an unfreeze snap after the row's first stream sample continues the client's stream
        // (pose_stream_apply), so the snap is not the client's end state: the row is left out and counted.
        if (r.state == PoseState::Sent && r.stream_seq != 0 && (r.flags & kPoseUnfreeze) != 0) {
            ++g_pose.st_check_skips;
            continue;
        }
        // A guid-0 row has gone to no client yet (pose_guid_check sends it once the corpse has a NetGUID).
        void* const mesh = r.state == PoseState::Sent && r.blob_n != 0 && r.guid != 0 ? pose_mesh(r) : nullptr;
        int32_t count = 0;
        void* const* const bodies = mesh != nullptr ? pose_bodies(mesh, count) : nullptr;
        if (bodies == nullptr || count != r.bodies) {
            continue;
        }
        const PoseFar f = pose_far(r, bodies, count);
        if (f.worst < 0.0) {
            continue;
        }
        ++checked;
        if (f.worst <= kPoseTolerance) {
            // Card 203: moved past the re-arm distance but under the tolerance (no WARN): body 0 decides the re-arm.
            if (f.worst > kPoseMoved && note_event_cap(g_pose.settle_lines, kPoseSettleLines, "card203 host settle")) {
                log::line("card203: host round %u pose guid 0x%llx snap %u: largest body distance from the last sent "
                          "snap %.0f uu (body %d), body 0 %.0f uu (re-arm at %.0f uu on body 0), simulating %d",
                          round, static_cast<unsigned long long>(r.guid), r.snap, f.worst, f.at, f.body0, kPoseMoved,
                          c39_mesh_simulating(mesh) ? 1 : 0);
            }
            continue;
        }
        ++over;
        if (note_event_cap(g_pose.warn_lines, kPoseWarnLines, "card132 pose warn")) {
            log::line("WARN card140: host round %u pose guid 0x%llx snap %u: body %d is %.0f uu from the last sent snap "
                      "(%d of %d over %.0f uu), simulating %d, unsettled flag %d; card203: body 0 at (%.0f, %.0f, %.0f) "
                      "is %.0f uu from the snap's (%.0f, %.0f, %.0f)",
                      round, static_cast<unsigned long long>(r.guid), r.snap, f.at, f.worst, f.over, count,
                      kPoseTolerance, c39_mesh_simulating(mesh) ? 1 : 0, (r.flags & kPoseUnsettled) != 0 ? 1 : 0,
                      f.client0.x, f.client0.y, f.client0.z, f.body0, f.host0.x, f.host0.y, f.host0.z);
        }
    }
    log::line("%scard140: host round %u pose self-check: %u sent corpses checked, %u moved over %.0f uu since the send",
              over != 0 ? "WARN " : "", round, checked, over, kPoseTolerance);
    return over;
}

// Client, at a digest round: each applied corpse, body to body against the host's last snap. Returns the mismatches.
namespace {

// c132_pose_check's capped WARN for one row over its tolerance.
void pose_check_warn(uint32_t round, const PoseRow& r, void* mesh, const PoseFar& f, int32_t count, double tol) {
    if (!note_event_cap(g_pose.warn_lines, kPoseWarnLines, "card132 pose warn")) {
        return;
    }
    char comp[512];
    c39_comp_text(mesh, comp, sizeof(comp));
    log::line("WARN card132: client round %u pose guid 0x%llx snap %u: body %d at (%.0f, %.0f, %.0f) is %.0f uu "
              "from the host's (%.0f, %.0f, %.0f); %d of %d bodies over %.0f uu, simulating %d%s; mesh %s; card203: body 0 "
              "at (%.0f, %.0f, %.0f) is %.0f uu from the host's (%.0f, %.0f, %.0f)",
              round, static_cast<unsigned long long>(r.guid), r.snap, f.at, f.client.x, f.client.y, f.client.z,
              f.worst, f.host.x, f.host.y, f.host.z, f.over, count, tol, c39_mesh_simulating(mesh) ? 1 : 0,
              r.streamed ? " (card152 streamed, against its last sample)" : "", comp, f.client0.x, f.client0.y,
              f.client0.z, f.body0, f.host0.x, f.host0.y, f.host0.z);
    c251_line(r, mesh, "pose warn");
}

} // namespace

uint32_t c132_pose_check(uint32_t round) {
    uint32_t bad = 0, applied = 0, pending = 0, gone = 0, frozen_sim = 0, simulating = 0, streamed = 0;
    for (int32_t i = 0; i < g_pose.n; ++i) {
        const PoseRow& r = g_pose.row[i];
        pending += r.state == PoseState::Pending ? 1u : 0u;
        void* const mesh = r.state == PoseState::Applied ? pose_mesh(r) : nullptr;
        int32_t count = 0;
        void* const* const bodies = mesh != nullptr ? pose_bodies(mesh, count) : nullptr;
        if (count == 0 || count != r.bodies) {
            continue;
        }
        const bool sim_here = c39_mesh_simulating(mesh);
        if ((r.flags & kPoseFrozen) != 0 && sim_here) {
            ++frozen_sim;
        }
        // A corpse simulating on either side and not streamed (here, or the host's last snap an unfreeze or an
        // unsettled send) is still falling on both screens from the same start: left out and counted. Its freeze snap
        // is checked one tick later (card140 freeze check) and here after it. Card 152: a streamed row is compared
        // against its last sample, with the tolerance widened by the distance the copy is still moving toward it.
        if ((!r.streamed || r.own_fall) && (sim_here || (r.flags & (kPoseUnfreeze | kPoseUnsettled)) != 0)) {
            ++simulating;
            continue;
        }
        streamed += r.streamed ? 1u : 0u;
        const double tol = kPoseTolerance + (r.streamed && r.live ? pose_stream_span(r) : 0.0);
        const PoseFar f = pose_far(r, bodies, count);
        gone += static_cast<uint32_t>(f.gone);
        if (f.worst < 0.0) {
            continue;
        }
        ++applied;
        if (f.worst <= tol) {
            continue;
        }
        ++bad;
        pose_check_warn(round, r, mesh, f, count, tol);
    }
    // A row dropped since the last check never reached its corpse: that corpse differs from the host's.
    const uint32_t gave_up = g_pose.gave_up_round;
    g_pose.gave_up_round = 0;
    g_pose.checks += applied;
    g_pose.mismatches += bad + gave_up;
    g_pose.frozen_sim += frozen_sim;
    g_pose.sim_skipped += simulating;
    log::line("%scard132: client round %u pose: %u corpses checked, %u over %.0f uu, %u pending, %u gave up; %u "
              "bodies gibbed (not compared); %u frozen rows simulating; frozen applies %u, unfreezes %u (%u not "
              "simulating), own freeze calls %u",
              bad + gave_up + frozen_sim != 0 ? "WARN " : "", round, applied, bad, kPoseTolerance, pending, gave_up,
              gone, frozen_sim, g_pose.freezes, g_pose.unfreezes, g_pose.unfreeze_still, g_pose.own_freezes);
    log::line("card132: client round %u pose: %u simulating and not streamed (left out of the compare; %u this map), "
              "card152 %u streamed rows compared against their last sample", round, simulating, g_pose.sim_skipped,
              streamed);
    return bad + gave_up;
}

// Card 326: the registry's "pose" map end (was the arm).
void c132_pose_map_end() {
    if (g_pose.tracked != 0 || g_pose.received != 0) {
        log::line("card132: %s pose journal map totals: %u tracked, %u dropped, %u without guid, %u snaps (%u resnaps, "
                  "%u unsettled), %u removed, %u frames, %u bytes, %u unencodable; %u received, %u applied, %u gave up, "
                  "%u bad, %u removed by the host, %u checks, %u mismatches, largest body error %.1f uu, largest root "
                  "distance after %.1f uu; %u ragdolls started for a pose, %u would not simulate; card137 freezes %u, unfreezes %u, "
                  "own freeze calls %u, frozen rows simulating %u; card339 %u unchanged simulate calls skipped; card359 "
                  "mesh scale writes %u", g_pose.received != 0 ? "client" : "host", g_pose.tracked, g_pose.dropped, g_pose.no_guid, g_pose.snaps,
                  g_pose.resnaps, g_pose.unsettled, g_pose.removed, g_pose.frames, g_pose.bytes, g_pose.too_many,
                  g_pose.received, g_pose.applied, g_pose.pending_give_up, g_pose.bad, g_pose.removed_rx, g_pose.checks,
                  g_pose.mismatches, g_pose.err_max, g_pose.after_max, g_pose.started, g_pose.started_failed,
                  g_pose.freezes, g_pose.unfreezes, g_pose.own_freezes, g_pose.frozen_sim,
                  g_pose.sim_unchanged, g_pose.c359_scaled);
    }
    if (g_pose.received != 0 && g_pose.freezes != 0) { // client side; the session flag is already clear at map end
        log::line("%scard140: client pose freeze map totals: %u freezes, worst body error one tick after %.1f uu, %u over "
                  "%.0f uu; game ragdoll starts blocked on a host-posed copy %u", g_pose.freeze_bad != 0 ? "WARN " : "",
                  g_pose.freezes, g_pose.freeze_err_max, g_pose.freeze_bad, kPoseFreezeTolerance, g_pose.game_blocked);
    }
    if (g_arrive.seen != 0) {
        log::line("%scard137: client corpse arrival map totals: %u arrived dead, %u posed, %u death plays, %u gone, "
                  "%u not held (list full)", g_arrive.death_plays + g_arrive.full != 0 ? "WARN " : "", g_arrive.seen,
                  g_arrive.posed, g_arrive.death_plays, g_arrive.gone, g_arrive.full);
    }
    if (g_pose.owed_guid != 0) {
        log::line("card137: host pose map totals: %u rows got a NetGUID after their death, %u owed snaps sent",
                  g_pose.owed_guid, g_pose.owed_sent);
    }
    for (int32_t i = 0; i < g_arrive.n; ++i) { // the map is going: nothing stays hidden into the next one
        void* const npc = c31_weak_get(g_arrive.a[i].actor);
        arrival_show(g_arrive.a[i], npc != nullptr ? read_ptr(npc, kCharacterMesh) : nullptr);
    }
    c152_pose_wait_clear_all(); // logs the stream totals before the journal is emptied
    sprite_map_line();          // card 177
    rest_map_line();            // card 269
    c192_arm_map();             // card 192
    g_pose = PoseJournal{};
    g_arrive = PoseArrivals{};
    g_sprite = SpriteDeaths{};
    g_rest = RestDeaths{}; // card 269
}

int32_t c132_pose_live() {
    return g_pose.n;
}

// Card 152: the wait table names the old world's copies; its totals are logged (once) and it is emptied. From
// c132_pose_map_end and ubw_load_map_enter, whichever runs first logs.
void c152_pose_wait_clear_all() {
    pose_stream_map_line();
    if (g_wait.added != 0 || g_wait.full != 0) {
        log::line("%scard152: client ragdoll wait table map totals: %u held, %u cleared by a stream row or removal "
                  "(slowest %llu ms), %u fallbacks (own start after %llu ms), %u unresolved, %u full, peak %d of %d",
                  g_wait.fallbacks + g_wait.full != 0 ? "WARN " : "", g_wait.added, g_wait.cleared,
                  static_cast<unsigned long long>(g_wait.resolved_peak), g_wait.fallbacks,
                  static_cast<unsigned long long>(kPoseStartWaitMs), g_wait.unresolved, g_wait.full, g_wait.peak,
                  kPoseWaits);
    }
    c291_client_totals();
    g_wait = PoseWaits{};
    c31_weak_set(g_pose_pc, nullptr);
}

} // namespace ubw::hooks
