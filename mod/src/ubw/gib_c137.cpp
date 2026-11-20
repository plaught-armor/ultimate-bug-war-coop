// Card 137 step 6b: gib parts from the host only. Until now the client rolled its own chunks from the same
// FSTGoreResponse (c42_client_chunks): its own part count, float and launch lerps, so its gibs flew and lay elsewhere.
// Now:
//   host:   ASTGorePart*::ImpartInitialRotationAndImpulse (the launch every spawned part gets, pooled or not) records
//           one row: a new host id, the data class (part +0x310, written by InitialisePart), the spawn float
//           (+0x318), the part's location and the launch A/B/C. The part is then sampled every kGibSampleMs while it
//           moves more than kGibStillUu per sample; it ends in one rest row when it stops, when the pool takes it
//           back (gone or hidden), or after kGibMaxMs.
//   client: c42_client_chunks rolls nothing; each spawn row spawns exactly the host's part from the kind's pool and
//           launches it with the host's values; each sample teleports it to the host's location and rotation, and the
//           rest row snaps it and puts its body to sleep (so it does not roll on past the host's rest point).
// Keys: the host keys a row on its own id, and a table entry on the part actor; a pooled actor that is launched again
// gets a new id (its entry is reset at each Impart), so no stale key carries over. The client maps host id -> its own
// actor in a fixed table; a full table counts the overflow with one WARN line each, nothing is dropped silently.
// Frame (ChanOp::GibPart, text, rows ended by ';'):
//   S <id> <x> <y> <z> <f> <ax> <ay> <az> <bx> <by> <bz> <cp> <cy> <cr> <data class path>   spawn and launch
//   M <id> <x> <y> <z> <pitch> <yaw> <roll>                                                  in-flight sample
//   R <id> <x> <y> <z> <pitch> <yaw> <roll>                                                  final rest row
//   G <id>                                                     card 148: the host's part was taken back before it rested
// Card 148: a part the pool takes back (hidden root) or launches again before it rested ends in a G row, not an R row at
// its last sent (mid-flight) pose: the client slept those in the air (finding 14). The client hides the piece's root, the
// host's own taken-back state; the pool's next spawn of that actor shows it again.
// Proof lines (client): in-flight distance before each snap (histogram), and the distance from the host's rest point
// kGibCheckMs after the rest row (within 2 uu, within 10 uu, worst).
// Card 201 (a burst filled the S queue and showed pieces 13 s late):
//   - An S row still waiting when the pool launches its part again is overwritten in place by the new launch (same
//     FIFO place); a waiting S row whose part is gone is cancelled. Neither old id gets an S or a G row (counted).
//   - The S row carries the class's package path when the class is "<package>.<last segment>_C"; the client rebuilds
//     the full path. Else the full path goes (counted). The package path fits the card 150 dictionary (one def, then
//     refs), so the budget charges the wire through a gib-owned PackSet mirror (gib_cost), kept a subset of every
//     ponged connection's set: emptied at map start, at a ponged PC count change and after a fanout that did not
//     reach every ponged PC; a frame whose probe from the mirror at its start is raw takes its marks back; the frame's
//     probe trues up the budget when it costs more than its rows were charged.
//   - An S row sent more than kGibLateMs after its launch takes its part's current pose (an M row) with it.

#include <stdio.h>
#include <string.h>

#include "ubw/hooks_internal.h"
#include "ue/chan_pack.h"

namespace ubw::hooks {

namespace {

constexpr int32_t kGibRows = 256;
constexpr ULONGLONG kGibSampleMs = 200;
constexpr ULONGLONG kGibFlushMs = 30; // card 519 step b: the host sends its pending gib frames at most this often
constexpr ULONGLONG kGibMaxMs = 10000;   // a part still moving after this rests where it is
constexpr ULONGLONG kGibStickyMaxMs = 3 * kGibMaxMs; // card 273: a sticky part not at rest by then rests where it is
constexpr ULONGLONG kGibCheckMs = 1000;
constexpr uint32_t kGibWindowLines = 200; // per map: budget windows that deferred or replaced a sample
constexpr double kGibMoveUu = 8.0;       // a sample goes out only past this move or kGibTurnDeg since the last sent row
constexpr double kGibTurnDeg = 5.0;
constexpr double kGibStillUu = 4.0;      // within this of its anchor for kGibStillMs: the part is at rest
constexpr ULONGLONG kGibStillMs = 1000;
// Card 273: ASTGorePartSticky's state byte (ASTGorePartSticky::Tick 0x513AE50): 1 idle, 2 wall slide, 3 ceiling (both
// move the part by its own timers), 4 physics. Pool kinds below kC42StickyKinds are the sticky ones.
constexpr size_t kGibStickyState = 0x340;
// c423b125a: 16 of 19 host `not at rest after 30000 ms` rows were state 0. The tick (0x14513AFC7) does nothing
// in state 0 but turn a sprite part on a timer; the state leaves 0 only on a component hit, which a part whose
// physics the significance manager switched off never gets. Its body decides, as in state 4.
constexpr uint8_t kGibStickyFlight = 0;
constexpr uint8_t kGibStickyIdle = 1;
constexpr uint8_t kGibStickyPhysics = 4;
constexpr int kGibStickySlide = 2;
constexpr int kGibStickyCeiling = 3;
constexpr size_t kGibSlideTimer = 0x330;   // Tick 0x14513AE50: state 2 counts +0x330 up to +0x334
constexpr size_t kGibCeilingTimer = 0x338; // state 3 counts +0x338 up to +0x33C
constexpr int32_t kC42StickyKinds = 3;
constexpr int32_t kGibBudgetBytes = 16384; // all GibPart rows (S, R, M) per 1 s window at most (card 140 run 2), in
                                           // packed bytes without the dictionary (card 150: an upper bound of the wire);
                                           // card 140 run 6: 4096 was sized for the text link, 16384 for the packed one
constexpr int32_t kGibSpawnChars = 512;   // one S row: kGibPathChars plus its numbers
// Card 140 run 6: sized from the c140e peak (one nuke: 612 S rows and 653 R rows waiting at once) with headroom. A full
// queue sends nothing past the budget: the row is not sent, counted with a WARN, and the run fails on that count.
constexpr int32_t kGibRestQ = 1024;       // rest rows waiting for budget: every table row plus pool reuses
constexpr int32_t kGibSpawnQ = 1024;      // S rows waiting for budget (card 140 run 3: a launch is never superseded)
constexpr size_t kPartDataClass = 0x310;   // TSubclassOf<data>, written by InitialisePart
constexpr size_t kPartSpawnFloat = 0x318;  // the pool spawn's float, written by InitialisePart
constexpr size_t kCompHiddenByte = 0x1A1;     // USceneComponent bHiddenInGame byte (card 042 track)
constexpr uint8_t kCompHiddenBit = 0x08;
constexpr size_t kRelativeRotation = 0x158; // USceneComponent::RelativeRotation (the root's world rotation)
constexpr int32_t kGibPathChars = 256;
// Card 148: a client row lives longer than the host's (S rows go before up to kGibRestQ waiting R rows, and a rested row
// is checked kGibCheckMs later), so the client table holds the host's rows, the waiting R rows and 1 s of checks. The
// play build's 256 overflowed 581 times (those host parts never spawned on the client).
constexpr int32_t kGibClientRows = kGibRows + kGibRestQ + kGibRows;
constexpr double kGibGroundUu = 40.0; // card 148: at rest, ground within this below the piece's bounds, else "above"
constexpr uint32_t kGibUnmappedLines = 8; // card 148: own caps, so expected unmapped G rows (a pool relaunch) do not
constexpr uint32_t kGibGroundLines = 24;  // use up the lines of the ground check (run c148-r1 lost all 8 that way)
constexpr uint32_t kGibLines = 120;
constexpr int32_t kGibBuckets = 8;
constexpr ULONGLONG kGibLateMs = 1000;   // card 201: an S row older than this goes with its part's current pose
constexpr int32_t kGibPathKinds = 64;    // card 201: class paths whose dictionary def the mirror holds (per reset)
constexpr uint32_t kGibPathLines = 8;    // card 201: host full-path lines
constexpr uint32_t kGibJoinNoClassLines = 16; // card 458: join rows skipped for no data class, per map
constexpr uint32_t kC279Lines = 20;     // card 279: R snap rows per map (parts over 10 uu off before the snap)
constexpr ULONGLONG kC279TickMs = 8;    // card 279: the read-back waits for a later tick, not the R row's own frame
constexpr uint32_t kC279HoldLines = 12; // card 279 step 3: first-move rows per map
constexpr size_t kGorePhysFlag = 0x31C; // ASTGorePartBase::bIsSignificancePhysicsEnabled (OnSignificanceChanged)
constexpr uint32_t kGibMissLines = 8;    // card 201 run 2: client rest misses (over 2 uu) with their row's history
constexpr double kGibBucketTop[kGibBuckets] = {2, 5, 10, 20, 40, 80, 150, 1e30};

enum GibState : uint8_t { kFree, kFlight, kCheck };

// Card 519: a GibPose row's tag byte. M a sample; L a sample sent right after its part's late S row (the card 201
// run 2 late_pose history); R0, R1 the rest row with the host's no-ground flag 0 or 1 (card 273); G taken back.
enum GibTag : uint8_t { kGibTagNone, kGibTagM, kGibTagL, kGibTagR0, kGibTagR1, kGibTagG, kGibTags };
// A row: tag 1, id zigzag varint (at most 5), 3 location zigzag varints of whole uu (at most 10 each), rotation 3.
constexpr int32_t kGibPoseRowMax = 1 + 5 + 30 + 3;

struct HostGib {
    C31Weak part;
    uint32_t id;
    Vec3 sent_at;  // the last row sent for this part
    Vec3 sent_rot;
    Vec3 anchor;   // the still check: where the part has been within kGibStillUu since anchor_ms
    ULONGLONG anchor_ms;
    ULONGLONG born_ms;
    ULONGLONG next_ms;
    bool live;
    bool pending;      // a sample was deferred by the budget and no newer row has gone out yet
    bool spawn_unsent; // its current id's S row waits in g_gib_spawn; no M row goes before it
    int32_t spawn_slot; // card 201: that S row's g_gib_spawn slot while spawn_unsent
    uint32_t s_seq;     // card 519 step b: the text frame its S row went into (GibHost::text_seq + 1 at the time)
};
struct GibRest {
    uint32_t id;
    char kind; // 'R' at rest (the pose is the part's), 'G' taken back before it rested (no pose)
    Vec3 at;
    Vec3 rot;
    bool no_ground; // card 273: the host's gib_no_ground result as the R row was queued (the R row's flag)
};
struct ClientGib {
    C31Weak part;
    uint32_t id;
    Vec3 rest_at;
    ULONGLONG check_ms;
    GibState state;
    int8_t kind; // card 201: its c42 pool kind (-1 none)
    // Card 201 run 2: the row's history for the rest miss line. late_pose: its first M row came right after its S row
    // in one frame (the host's late S row with its pose); rest_from: the piece's distance from the R point when the R
    // row was applied (before the snap).
    ULONGLONG spawn_ms, last_m_ms, rest_ms;
    double rest_from;
    uint32_t m_rows;
    bool late_pose;
    bool host_no_ground; // card 273: the R row's flag (the host found no ground below its own part)
    // Card 279: the R snap diagnostic (gib_c279_snap); its row is logged on the next tick (gib_c279_line).
    float c279_d0, c279_d1;
    bool c279_set, c279_owed;
    bool c279_was_off; // card 279 step 2: the root was not simulating at the R row (physics switched on for the snap)
    double rest_rot[3]; // card 279 step 3: the R row's rotation, for the hold
    uint32_t c279_holds; // card 279 step 3: times the hold put the part back before the check
};
struct GibHost {
    HostGib row[kGibRows];
    uint32_t next_id;
    uint32_t depth; // Impart nesting: only the outermost call records
    uint32_t spawns, samples, rests, overflow, frames, reused, deferred, replaced;
    uint32_t recorded, spawn_full, rest_full;   // S rows recorded; S and R rows not sent at a full queue (a fail)
    uint32_t rest_still, rest_max, rest_gone, rest_relaunch; // card 148: rest and G rows by reason
    uint32_t rest_above, above_lines;                        // card 148: R rows with no ground below the piece
    uint32_t sticky_lines;                                   // card 273: line cap of the sticky bound rows
    int32_t spawn_peak, rest_peak;              // the most S and R rows waiting at once
    ULONGLONG spawn_wait_max;                   // the longest an S row waited for budget
    int32_t spent_peak;                         // the most bytes spent in one 1 s window
    GibRest rest[kGibRestQ];                    // R rows waiting for budget, in order
    int32_t rest_head, rest_count;
    int32_t start;           // the tick's first row, rotated so a spent budget does not always starve the same parts
    uint64_t bytes;
    int32_t budget;          // bytes left this window for any row; card 201: a frame's true-up may take it below 0
    ULONGLONG budget_ms;     // when the budget was last refilled
    uint32_t win_samples, win_deferred, win_replaced, win_lines; // at the last refill; the window line's base
    void* world;
    char out[kChanTextMax + 1];
    int32_t used;
    // Card 201: S rows that never go (replaced at a relaunch, cancelled gone), S rows sent late; class path forms.
    uint32_t spawn_replaced, spawn_cancelled, spawn_late, path_pkg, path_full, path_lines;
    uint32_t join_noclass_lines; // card 458: per map
    // Card 201: the wire charge. pack: the ids every ponged connection has had a def for (a subset); paths: the class
    // path hashes whose def pack holds; frame_*: both at the pending frame's start; frame_charged: its rows' charges.
    ue::chan::PackSet pack, frame_pack;
    uint64_t paths[kGibPathKinds];
    int32_t npaths, frame_npaths, frame_charged, pack_pcs;
    uint32_t pack_resets, pack_pc_resets, pack_send_resets, pack_rollbacks, pack_commits, pack_untracked;
    uint64_t trueup_bytes;
    // Card 519: the pending GibPose blob frame (M, R and G rows in binary, gib_pose_put); its id and location deltas
    // run from the frame's previous row (0 at the frame's start); its rows by tag, bytes and frames sent.
    uint8_t pose[kChanBlobBytes];
    int32_t pose_used;
    uint32_t pose_last_id;
    int64_t pose_last[3];
    uint32_t pose_rows[kGibTags];
    uint64_t pose_bytes;
    uint32_t pose_frames, pose_partial;
    ULONGLONG flush_ms; // card 519 step b: when gib_host_tick last sent the pending frames
    uint32_t text_seq;  // card 519 step b: text frames ended (sent or not); the pending one is text_seq + 1
};
struct GibClient {
    ClientGib row[kGibClientRows];
    uint32_t spawns, spawn_fail, samples, unmapped, rests, overflow, lines, bad_rows;
    uint32_t gone, no_row, no_root, reused, empty, above; // card 148: G rows; unmapped by reason; pool reuse frees;
                                                          // empty frames; pieces with no ground below at the check
    uint32_t unmapped_lines, ground_lines;                // card 148: their own line caps
    uint32_t rebuilt, rebuilt_bad;                        // card 201: package paths rebuilt; of them, not resolved
    bool checks; // card 414: a row may be in kCheck (set with the state, cleared by a tick that finds none left)
    int32_t kind_peak[kC42Kinds];                         // card 201: most rows in use at once per pool kind
    uint32_t misses, miss_lines;                          // card 201 run 2: rest checks over 2 uu; their line cap
    uint32_t above_flagged;                               // card 273: of 'above', parts the host flagged too (no line)
    uint32_t hist[kGibBuckets];
    double flight_worst;
    uint32_t checked, within2, within10, check_gone;
    double rest_worst;
    uint32_t c279_lines; // card 279: R snap rows this map
    uint32_t c279_switched; // card 279 step 2: R rows that switched a part's physics on before the snap
    uint32_t c279_hold_lines, c279_held_parts, c279_holds; // card 279 step 3: line cap; parts held; re-snaps
};

GibHost g_gh{};   // game thread
struct GibSpawnQ { // S rows waiting for budget, in launch order; every S row recorded goes, none is replaced
    char text[kGibSpawnQ][kGibSpawnChars];
    uint32_t id[kGibSpawnQ];
    int32_t row[kGibSpawnQ]; // the table row it launched
    ULONGLONG born[kGibSpawnQ];
    int32_t head, count;
};
GibSpawnQ g_gib_spawn{}; // game thread
GibClient g_gc{}; // game thread

// Card 201: the mirror empty again (every class path's next S row is charged its def).
void gib_pack_reset() {
    g_gh.pack = ue::chan::PackSet{};
    g_gh.frame_pack = ue::chan::PackSet{};
    g_gh.npaths = 0;
    g_gh.frame_npaths = 0;
    ++g_gh.pack_resets;
}

// Card 201: the pending frame's wire bound, packed from the mirror as it was at the frame's start (every ponged
// connection's set holds at least those ids, so its real packing is no larger and no more often raw). A raw probe
// takes the frame's marks back (the wire may not carry its defs); a probe above the rows' charges is charged too.
void gib_frame_check() {
    static uint8_t bytes[kChanTextMax + 1];
    ue::chan::PackSet start = g_gh.frame_pack;
    const int32_t n = ue::chan::pack_text(g_gh.out, g_gh.used, &start, false, bytes, g_gh.used + 1);
    if (n <= 0 || n >= g_gh.used + 1) {
        g_gh.pack_rollbacks += g_gh.npaths != g_gh.frame_npaths ? 1u : 0u;
        g_gh.pack = g_gh.frame_pack;
        g_gh.npaths = g_gh.frame_npaths;
    }
    const int32_t extra = (n > 0 ? n : g_gh.used + 1) - g_gh.frame_charged;
    if (extra > 0) {
        g_gh.budget -= extra;
        g_gh.trueup_bytes += static_cast<uint64_t>(extra);
    }
}

// Card 519: the pending GibPose frame goes out to every ponged client, then starts empty (deltas from 0 again).
void gib_pose_flush() {
    if (g_gh.pose_used > 0 && g_gh.world != nullptr) {
        const C95Fanout f = c95_send_blob_all(g_gh.world, ChanOp::GibPose, g_gh.pose, g_gh.pose_used);
        ++g_gh.pose_frames;
        g_gh.pose_partial += f.sent < f.to ? 1u : 0u;
    }
    g_gh.pose_used = 0;
    g_gh.pose_last_id = 0;
    g_gh.pose_last[0] = g_gh.pose_last[1] = g_gh.pose_last[2] = 0;
}

// Host: the pending frame goes out (to every ponged client), then starts empty. Card 519: the text frame (S rows)
// goes before the GibPose frame, so a part's S row always reaches the client before its M, R or G rows (one FIFO
// pace queue per PC, card 519 ordering note).
void gib_flush() {
    if (g_gh.used > 0 && g_gh.world != nullptr) {
        gib_frame_check();
        const C95Fanout f = c95_send_all(g_gh.world, ChanOp::GibPart, g_gh.out);
        ++g_gh.frames;
        if (f.sent < f.to) { // card 201: a ponged connection missed this frame's defs
            gib_pack_reset();
            ++g_gh.pack_send_resets;
        }
    } else if (g_gh.used > 0) { // not sent: its marks go back
        g_gh.pack = g_gh.frame_pack;
        g_gh.npaths = g_gh.frame_npaths;
    }
    g_gh.text_seq += g_gh.used > 0 ? 1u : 0u;
    g_gh.used = 0;
    g_gh.out[0] = '\0';
    g_gh.frame_pack = g_gh.pack;
    g_gh.frame_npaths = g_gh.npaths;
    g_gh.frame_charged = 0;
    gib_pose_flush();
}

// A row's budget cost: its packed size against the mirror (card 201: a ref for a class path whose def every ponged
// connection has had, a def or inline otherwise), a probe that marks nothing.
int32_t gib_cost(const char* row) {
    static uint8_t bytes[kChanTextMax + 1];
    const int32_t len = static_cast<int32_t>(strlen(row));
    const int32_t n = len > 0 && len <= kChanTextMax
                          ? ue::chan::pack_text(row, len, &g_gh.pack, false, bytes, len + 1)
                          : 0;
    return n > 0 ? n : len;
}

uint64_t gib_hash(const char* text) {
    uint64_t h = 1469598103934665603ull;
    for (int32_t i = 0; i < kGibPathChars && text[i] != '\0'; ++i) {
        h = (h ^ static_cast<uint8_t>(text[i])) * 1099511628211ull;
    }
    return h;
}

// Card 201: an S row's class path def marked in the mirror, once per reset (the pack_text commit counts one card 150
// frame, so only the first row of each path commits). A full path table leaves it unmarked: charged again, never less.
void gib_pack_commit(const char* row) {
    const char* const sp = row[0] == 'S' ? strrchr(row, ' ') : nullptr;
    if (sp == nullptr) {
        return;
    }
    const uint64_t h = gib_hash(sp + 1);
    for (int32_t i = 0; i < g_gh.npaths; ++i) {
        if (g_gh.paths[i] == h) {
            return;
        }
    }
    if (g_gh.npaths >= kGibPathKinds) {
        ++g_gh.pack_untracked;
        return;
    }
    static uint8_t bytes[kChanTextMax + 1];
    const int32_t len = static_cast<int32_t>(strlen(row));
    ue::chan::pack_text(row, len, &g_gh.pack, true, bytes, len + 1);
    g_gh.paths[g_gh.npaths++] = h;
    ++g_gh.pack_commits;
}

void gib_append(const char* row) {
    const int32_t len = static_cast<int32_t>(strlen(row));
    if (g_gh.used + len > kChanTextMax) {
        gib_flush();
    }
    if (len > kChanTextMax) {
        return; // never: rows are under 512 chars
    }
    const int32_t cost = gib_cost(row);
    g_gh.budget -= cost;
    g_gh.frame_charged += cost;
    g_gh.bytes += static_cast<uint64_t>(len);
    gib_pack_commit(row);
    memcpy(g_gh.out + g_gh.used, row, static_cast<size_t>(len) + 1);
    g_gh.used += len;
}

// Card 519: GibPose bytes.
int32_t gib_put_varint(uint8_t* o, uint64_t v) {
    int32_t n = 0;
    for (; n < 10 && v >= 0x80u; ++n, v >>= 7) {
        o[n] = static_cast<uint8_t>(v | 0x80u);
    }
    o[n] = static_cast<uint8_t>(v);
    return n + 1;
}
int32_t gib_put_zigzag(uint8_t* o, int64_t v) {
    return gib_put_varint(o, (static_cast<uint64_t>(v) << 1) ^ static_cast<uint64_t>(v >> 63));
}
// One angle in 360/256 degree steps (1.4 degrees: under the kGibTurnDeg gate); NaN as 0.
uint8_t gib_angle_byte(double deg) {
    double d = deg == deg ? fmod(deg, 360.0) : 0.0;
    d = d < 0 ? d + 360.0 : d;
    return static_cast<uint8_t>(llround(d * 256.0 / 360.0) & 0xFF);
}
// Whole uu, clamped to the varint's comfortable range; NaN as 0.
int64_t gib_whole_uu(double v) {
    const double c = v == v ? (v < -1e9 ? -1e9 : (v > 1e9 ? 1e9 : v)) : 0.0;
    return llround(c);
}

// Card 519: a GibPose row into out (kGibPoseRowMax bytes) against the pending frame's deltas, as it would be written
// now; a row that would not fit the frame is written as the first row of the next one. commit: the frame takes it
// (flushing first when full) and its deltas move on. Returns its byte count.
int32_t gib_pose_put(uint8_t tag, uint32_t id, const Vec3& at, const Vec3& rot, bool commit) {
    const bool fresh = g_gh.pose_used + kGibPoseRowMax > kChanBlobBytes;
    if (fresh && commit) {
        gib_flush(); // the text frame first, then this frame (order), both empty after
    }
    const uint32_t last_id = fresh ? 0 : g_gh.pose_last_id;
    const int64_t zero[3] = {0, 0, 0};
    const int64_t* const last = fresh ? zero : g_gh.pose_last;
    uint8_t row[kGibPoseRowMax];
    int32_t n = 0;
    row[n++] = tag;
    n += gib_put_zigzag(row + n, static_cast<int64_t>(id) - static_cast<int64_t>(last_id));
    const int64_t p[3] = {gib_whole_uu(at.x), gib_whole_uu(at.y), gib_whole_uu(at.z)};
    if (tag != kGibTagG) {
        for (int32_t k = 0; k < 3; ++k) {
            n += gib_put_zigzag(row + n, p[k] - last[k]);
        }
        row[n++] = gib_angle_byte(rot.x);
        row[n++] = gib_angle_byte(rot.y);
        row[n++] = gib_angle_byte(rot.z);
    }
    if (!commit) {
        return n;
    }
    memcpy(g_gh.pose + g_gh.pose_used, row, static_cast<size_t>(n));
    g_gh.pose_used += n;
    g_gh.pose_last_id = id;
    if (tag != kGibTagG) {
        g_gh.pose_last[0] = p[0];
        g_gh.pose_last[1] = p[1];
        g_gh.pose_last[2] = p[2];
    }
    g_gh.budget -= n;
    g_gh.pose_bytes += static_cast<uint64_t>(n);
    ++g_gh.pose_rows[tag < kGibTags ? tag : 0];
    return n;
}

Vec3 gib_rotation(const void* part) {
    const void* const root = read_ptr(part, kActorRootComponent);
    return root != nullptr ? read_vec3(root, kRelativeRotation) : Vec3{};
}

// Whole uu and degrees: the rest bound is 10 uu, so rounding (0.5 at most) costs nothing and halves the row.
// Card 273: an R row ends in one flag digit (the host's no-ground result); flag < 0 (M rows) writes none.
int32_t gib_pose_text(char* row, size_t n, char kind, uint32_t id, const Vec3& at, const Vec3& rot, int flag = -1) {
    const int w = flag < 0 ? snprintf(row, n, "%c %u %.0f %.0f %.0f %.0f %.0f %.0f;", kind, id, at.x, at.y, at.z, rot.x,
                                      rot.y, rot.z)
                           : snprintf(row, n, "%c %u %.0f %.0f %.0f %.0f %.0f %.0f %d;", kind, id, at.x, at.y, at.z,
                                      rot.x, rot.y, rot.z, flag);
    return w > 0 ? w : 0;
}

// Card 273: a sticky part's state byte, or -1 for any other part (an instance of a sticky pool kind's part class).
int gib_sticky_state(const void* part) {
    for (int32_t k = 0; k < kC42StickyKinds && part != nullptr; ++k) {
        void* const cls = g_c42_part_cls[k] != nullptr ? g_c42_part_cls[k]() : nullptr;
        if (cls != nullptr && is_a(part, cls)) {
            return static_cast<const uint8_t*>(part)[kGibStickyState];
        }
    }
    return -1;
}

// Client, card 273: a host-driven sticky part does not run its own state machine (its Tick does nothing idle), so the
// host's rows are its only mover.
void gib_sticky_hold(void* part) {
    if (gib_sticky_state(part) >= 0) {
        static_cast<uint8_t*>(part)[kGibStickyState] = kGibStickyIdle;
    }
}

// An M row, only when it fits this window's budget (false: nothing sent).
// Card 519: a GibPose M row.
bool gib_send_pose(HostGib& r, const Vec3& at, const Vec3& rot) {
    if (gib_pose_put(kGibTagM, r.id, at, rot, false) > g_gh.budget) {
        return false;
    }
    gib_pose_put(kGibTagM, r.id, at, rot, true);
    r.sent_at = at;
    r.sent_rot = rot;
    if (r.pending) { // the deferred sample is replaced by this newer row, never sent late with its older pose
        r.pending = false;
        ++g_gh.replaced;
    }
    return true;
}

// Queues id's rest row (sent in order as the budget allows). A full queue does not send it: counted, a fail.
void gib_queue_rest(uint32_t id, char kind, const Vec3& at, const Vec3& rot, bool no_ground) {
    ++g_gh.rests;
    if (g_gh.rest_count >= kGibRestQ) {
        if (g_gh.rest_full++ < kGibLines) {
            log::line("WARN card140: host gib rest queue full (%d): R %u not sent (%u)", kGibRestQ, id, g_gh.rest_full);
        }
        return;
    }
    g_gh.rest[(g_gh.rest_head + g_gh.rest_count) % kGibRestQ] = GibRest{id, kind, at, rot, no_ground};
    ++g_gh.rest_count;
    g_gh.rest_peak = g_gh.rest_count > g_gh.rest_peak ? g_gh.rest_count : g_gh.rest_peak;
}

// The oldest waiting S row goes out (budget or not: the caller checks), and its part may send M rows.
void gib_spawn_pop(ULONGLONG now) {
    GibSpawnQ& q = g_gib_spawn;
    const int32_t at = q.head;
    gib_append(q.text[at]);
    HostGib& r = g_gh.row[q.row[at]];
    if (r.live && r.id == q.id[at]) { // card 201: always (a relaunch overwrites its slot, a gone part cancels it)
        r.spawn_unsent = false;
        r.s_seq = g_gh.text_seq + 1; // after gib_append, which may have ended the frame before this row
        r.next_ms = now + kGibSampleMs;
    }
    ++g_gh.spawns;
    const ULONGLONG wait = now - q.born[at];
    g_gh.spawn_wait_max = wait > g_gh.spawn_wait_max ? wait : g_gh.spawn_wait_max;
    q.head = (q.head + 1) % kGibSpawnQ;
    --q.count;
}

// Card 201: the oldest waiting S row if it fits the budget; one waiting over kGibLateMs takes its part's pose now (an
// M row in the same frame, both charged) so the client piece starts where the host's is. A cancelled slot (id 0) goes
// at no cost. False: no room, nothing sent.
bool gib_spawn_try(ULONGLONG now) {
    GibSpawnQ& q = g_gib_spawn;
    const int32_t at = q.head;
    if (q.id[at] == 0) {
        q.head = (q.head + 1) % kGibSpawnQ;
        --q.count;
        return true;
    }
    HostGib& r = g_gh.row[q.row[at]];
    const bool late = now - q.born[at] > kGibLateMs;
    void* const part = late && r.live && r.id == q.id[at] ? c31_weak_get(r.part) : nullptr;
    const Vec3 at_now = part != nullptr ? actor_location(part) : Vec3{};
    const Vec3 rot_now = part != nullptr ? gib_rotation(part) : Vec3{};
    const int32_t pose_cost = part != nullptr ? gib_pose_put(kGibTagL, r.id, at_now, rot_now, false) : 0;
    if (gib_cost(q.text[at]) + pose_cost > g_gh.budget) {
        return false; // S rows first: no R or M row overtakes a waiting spawn
    }
    gib_spawn_pop(now);
    g_gh.spawn_late += late ? 1u : 0u;
    if (part != nullptr) { // card 519: the L row, sent in the GibPose frame after this flush's S frame
        gib_pose_put(kGibTagL, r.id, at_now, rot_now, true);
        r.sent_at = at_now;
        r.sent_rot = rot_now;
    }
    return true;
}

// Waiting S rows, then waiting R rows (each in queue order), while each fits the budget. Bounded by both sizes.
void gib_send_waiting(ULONGLONG now) {
    for (int32_t k = 0; k < kGibSpawnQ && g_gib_spawn.count != 0; ++k) {
        if (!gib_spawn_try(now)) {
            return;
        }
    }
    for (int32_t k = 0; k < kGibRestQ && g_gh.rest_count != 0; ++k) {
        const GibRest& q = g_gh.rest[g_gh.rest_head];
        const uint8_t tag = q.kind == 'G' ? kGibTagG : (q.no_ground ? kGibTagR1 : kGibTagR0); // card 519
        if (gib_pose_put(tag, q.id, q.at, q.rot, false) > g_gh.budget) {
            return;
        }
        gib_pose_put(tag, q.id, q.at, q.rot, true);
        g_gh.rest_head = (g_gh.rest_head + 1) % kGibRestQ;
        --g_gh.rest_count;
    }
}

// The largest of the three angle changes, wrapped to [0, 180].
double gib_turn(const Vec3& a, const Vec3& b) {
    const double d[3] = {a.x - b.x, a.y - b.y, a.z - b.z};
    double worst = 0;
    for (double v : d) {
        double w = v < 0 ? -v : v;
        w = w > 360.0 ? w - 360.0 * static_cast<double>(static_cast<int64_t>(w / 360.0)) : w;
        w = w > 180.0 ? 360.0 - w : w;
        worst = w > worst ? w : worst;
    }
    return worst;
}

// Host: the part's table entry. Its own entry first (a pooled actor launched again: reset, new id), else a free one.
HostGib* gib_host_entry(const void* part) {
    HostGib* free_row = nullptr;
    for (int32_t i = 0; i < kGibRows; ++i) {
        HostGib& r = g_gh.row[i];
        void* const live = r.live ? c31_weak_get(r.part) : nullptr;
        if (live == part) {
            return &r;
        }
        free_row = free_row == nullptr && live == nullptr ? &r : free_row;
    }
    return free_row;
}

// Card 201: the part's data class as its package path when the class is "<package>.<last segment>_C" (the client
// rebuilds the rest), else the full path (counted, first lines logged). "-" when the class has no path.
void gib_class_path(const void* part, char* path, int32_t cap) {
    if (c50_class_path(read_ptr(part, kPartDataClass), path, cap) <= 0) {
        snprintf(path, static_cast<size_t>(cap), "-");
        return;
    }
    char* const dot = strrchr(path, '.');
    const char* const slash = strrchr(path, '/');
    const char* const seg = slash != nullptr ? slash + 1 : path;
    const size_t seg_len = dot != nullptr && dot > seg ? static_cast<size_t>(dot - seg) : 0;
    if (seg_len != 0 && strncmp(dot + 1, seg, seg_len) == 0 && strcmp(dot + 1 + seg_len, "_C") == 0) {
        *dot = '\0';
        ++g_gh.path_pkg;
        return;
    }
    ++g_gh.path_full;
    if (note_event_cap(g_gh.path_lines, kGibPathLines, "card201 host path lines")) {
        log::line("card201: host gib class path sent in full (not <package>.<last segment>_C): %s (%u)", path,
                  g_gh.path_full);
    }
}

// Card 201: r's S row into the queue: over its own waiting slot when replace (same FIFO place, a new id, text and
// time), else at the tail. A full queue does not send it and its part is not tracked (a fail, counted).
void gib_spawn_queue(HostGib& r, bool replace, const char* row, ULONGLONG now) {
    GibSpawnQ& q = g_gib_spawn;
    if (!replace && q.count >= kGibSpawnQ) {
        r.live = false;
        r.spawn_unsent = false;
        if (g_gh.spawn_full++ < kGibLines) {
            log::line("WARN card140: host gib spawn queue full (%d): S %u not sent (%u)", kGibSpawnQ, r.id,
                      g_gh.spawn_full);
        }
        return;
    }
    const int32_t slot = replace ? r.spawn_slot : (q.head + q.count) % kGibSpawnQ;
    q.count += replace ? 0 : 1;
    q.id[slot] = r.id;
    q.row[slot] = static_cast<int32_t>(&r - g_gh.row);
    q.born[slot] = now;
    snprintf(q.text[slot], kGibSpawnChars, "%s", row); // sent by gib_send_waiting when the budget has room
    r.spawn_slot = slot;
    r.spawn_unsent = true;
    g_gh.spawn_peak = q.count > g_gh.spawn_peak ? q.count : g_gh.spawn_peak;
}

// Card 201: an entry taken again while live. Its S row still waiting: that slot takes the new launch (no S and no G
// for the old id; counted replaced when the pool relaunched the same part, cancelled when the entry's part is gone).
// Sent: the old id ends in a G row (card 148: not an R row at the last sent, mid-flight pose), so the client hides its
// piece. True: replace the waiting slot.
bool gib_host_retake(HostGib& r, const void* part) {
    if (!r.live) {
        return false;
    }
    if (r.spawn_unsent && g_gib_spawn.id[r.spawn_slot] == r.id) {
        const bool same = c31_weak_get(r.part) == part;
        g_gh.spawn_replaced += same ? 1u : 0u;
        g_gh.spawn_cancelled += same ? 0u : 1u;
        return true;
    }
    gib_queue_rest(r.id, 'G', r.sent_at, r.sent_rot, false);
    ++g_gh.rest_relaunch;
    ++g_gh.reused;
    return false;
}

void gib_host_record(void* part, const Vec3* a, const Vec3* b, const Vec3* c) {
    void* const world = g_actor_get_world(part);
    if (world == nullptr || c95_ponged_count(world) <= 0) {
        return;
    }
    HostGib* const r = gib_host_entry(part);
    if (r == nullptr) {
        if (g_gh.overflow++ < kGibLines) {
            log::line("WARN card137: host gib table full (%d live parts): %s not sent (overflow %u)", kGibRows,
                      object_name(part).text, g_gh.overflow);
        }
        return;
    }
    char path[kGibPathChars];
    gib_class_path(part, path, kGibPathChars);
    const ULONGLONG now = GetTickCount64();
    const Vec3 at = actor_location(part);
    const bool replace = gib_host_retake(*r, part);
    c31_weak_set(r->part, part);
    r->id = ++g_gh.next_id;
    r->sent_at = at;
    r->sent_rot = gib_rotation(part);
    r->anchor = at;
    r->anchor_ms = now;
    r->born_ms = now;
    r->pending = false;
    r->next_ms = now + kGibSampleMs;
    r->live = true;
    g_gh.world = world;
    ++g_gh.recorded;
    char row[kGibSpawnChars];
    snprintf(row, sizeof(row), "S %u %.1f %.1f %.1f %.3f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %s;", r->id,
             at.x, at.y, at.z, read_f32(part, kPartSpawnFloat), a->x, a->y, a->z, b->x, b->y, b->z, c->x, c->y, c->z,
             path);
    gib_spawn_queue(*r, replace, row, now);
}

void gib_host_detour(ImpartFn orig, void* part, const Vec3* a, const Vec3* b, const Vec3* c) {
    ++g_gh.depth;
    orig(part, a, b, c);
    --g_gh.depth;
    if (!g_client_session && g_gh.depth == 0 && part != nullptr && a != nullptr && b != nullptr && c != nullptr) {
        gib_host_record(part, a, b, c);
    }
}

// Card 212: the bounds a ground probe started from. A gore part at a low significance level has physics and
// collision off (ASTGorePartBase::SetPhysicsEnabled(false) from OnSignificanceChanged). Card 148 (b133): both sides
// probe from the all-component bounds, then the actor location when those come back empty; entry 0 (colliding
// bounds) is no longer used.
constexpr const char* kGibBoundsText[] = {"colliding bounds", "all-component bounds", "the actor location"};
constexpr uint32_t kGibBoundsAll = 1;
constexpr uint32_t kGibBoundsActor = 2;

// Card 212: one fallback from the all-component bounds to the actor location, counted per side with its first lines.
void gib_bounds_fallback_line(const void* part, uint32_t used, const Vec3& from) {
    static uint32_t fallbacks = 0;
    static uint32_t lines = 0;
    ++fallbacks;
    if (note_event_cap(lines, kGibBoundsLines, "card212 gib bounds fallback lines")) {
        log::line("card212: %s gib part %s %s: no component bounds, ground probe from %s at %.0f %.0f %.0f "
                  "(fallback %u)", side(), object_name(part).text, class_name(part).text, kGibBoundsText[used],
                  from.x, from.y, from.z, fallbacks);
    }
}

bool gib_box_empty(const Vec3& extent) {
    return extent.x == 0.0 && extent.y == 0.0 && extent.z == 0.0;
}

// Card 148: true when nothing (BlockAll, the piece ignored) lies within kGibGroundUu below the bottom of the piece's
// all-component bounds (a large piece's pivot can sit higher than kGibGroundUu above the floor). *bottom gets that
// point, *bounds the bounds it came from (card 212: the actor location when the bounds come back empty).
bool gib_no_ground(void* part, Vec3* bottom, const char** bounds) {
    static uint64_t block_all = 0;
    const Vec3 at = actor_location(part);
    Vec3 centre = at;
    Vec3 extent{};
    uint32_t used = kGibBoundsActor;
    if (g_actor_bounds != nullptr) {
        // Run c423b130a: the host's part at rest has collision off (low significance) and probed from its
        // all-component bounds, the client's copy has it on (card 279 switches physics on for the R snap) and
        // probed from its colliding bounds; 9 of 38 client rows disagreed with the host's flag at the same pose.
        // Both sides now probe from the all-component bounds, so a flag that differs is a pose that differs.
        used = kGibBoundsAll;
        g_actor_bounds(part, false, &centre, &extent, false);
        if (gib_box_empty(extent)) {
            used = kGibBoundsActor;
            centre = at;
            extent = Vec3{};
            gib_bounds_fallback_line(part, used, centre);
        }
    }
    *bounds = kGibBoundsText[used];
    *bottom = Vec3{centre.x, centre.y, centre.z - extent.z};
    const Vec3 end{centre.x, centre.y, bottom->z - kGibGroundUu};
    const Vec3 hit = trace_profile(part, centre, end, fname_cached(block_all, L"BlockAll"));
    return hit.z <= end.z;
}

// Host, card 148: the same ground test on the host's own piece as its R row is queued, so a client piece "above the
// ground" is matched to the host's pose (both above: the host's rest point; client only: the sync).
// Card 273: returns that result (the R row's flag); the line carries the sticky state byte (-1 not sticky).
bool gib_host_ground(void* part, uint32_t id) {
    Vec3 bottom{};
    const char* bounds = "";
    if (!gib_no_ground(part, &bottom, &bounds)) {
        return false;
    }
    ++g_gh.rest_above;
    if (note_event_cap(g_gh.above_lines, kGibGroundLines, "card148 host ground lines")) {
        log::line("card148: host gib part %u %s rests with nothing within %.0f uu below %.0f %.0f %.0f (%s, above "
                  "%u); card273: sticky state %d", id, object_name(part).text, kGibGroundUu, bottom.x, bottom.y,
                  bottom.z, bounds, g_gh.rest_above, gib_sticky_state(part));
    }
    return true;
}

// Card 273: a sticky part moves by its own state machine while its body sleeps (state 2 slides down a wall, state 3
// drops off a ceiling on a timer), so it is at rest only idle (1), or in flight (0) or physics (4) with the body
// asleep. Any other
// part: always (the still and max-time tests decide).
bool gib_sticky_rested(void* part) {
    const int state = gib_sticky_state(part);
    if (state < 0 || state == kGibStickyIdle) {
        return true;
    }
    // c244b205c: all 21 host `not at rest after 30000 ms` rows were state 2 or 3 with that state's own timer at 0.00:
    // the tick that counts it (and moves the part) never ran, so the part cannot move; the still test decides. The
    // tick advances the timer on its first frame in the state, so a running slide or drop never reads 0 here past
    // kGibStillMs.
    if (state == kGibStickySlide || state == kGibStickyCeiling) {
        return read_f32(part, state == kGibStickySlide ? kGibSlideTimer : kGibCeilingTimer) == 0.0f;
    }
    if (state != kGibStickyPhysics && state != kGibStickyFlight) {
        return false;
    }
    static void* primitive = find_class(L"/Script/Engine.PrimitiveComponent");
    void* const root = read_ptr(part, kActorRootComponent);
    const bool prim = root != nullptr && g_prim_awake != nullptr && primitive != nullptr && is_a(root, primitive);
    return prim && !g_prim_awake(root, 0);
}

// Card 201: r's waiting S row cancelled (its part is gone before the row went): the slot is skipped at no cost, and
// the id gets no S and no G row (counted).
void gib_spawn_cancel(HostGib& r) {
    GibSpawnQ& q = g_gib_spawn;
    if (q.id[r.spawn_slot] == r.id) {
        q.id[r.spawn_slot] = 0;
    }
    r.live = false;
    r.spawn_unsent = false;
    ++g_gh.spawn_cancelled;
}

// Host, one live part's sample: its rest row when it is gone, still for kGibStillMs or past kGibMaxMs (always sent);
// else a sample if it moved or turned enough since the last sent row and the budget has room. A sample with no room is
// counted as deferred: the part's next sample carries its newer pose, so nothing is lost but intermediate points.
void gib_host_sample(HostGib& r, ULONGLONG now) {
    r.next_ms = now + kGibSampleMs;
    void* const part = c31_weak_get(r.part);
    const auto* const root = part != nullptr ? static_cast<const uint8_t*>(read_ptr(part, kActorRootComponent))
                                             : nullptr;
    const bool gone = root == nullptr || (root[kCompHiddenByte] & kCompHiddenBit) != 0;
    if (r.spawn_unsent) { // nothing for this id before its S row; card 201: gone before it went, it never goes
        if (gone) {
            gib_spawn_cancel(r);
        }
        return;
    }
    const Vec3 at = gone ? r.sent_at : actor_location(part);
    const Vec3 rot = gone ? r.sent_rot : gib_rotation(part);
    if (vec_dist(at, r.anchor) >= kGibStillUu) {
        r.anchor = at;
        r.anchor_ms = now;
    }
    const bool still = now - r.anchor_ms >= kGibStillMs;
    const ULONGLONG age = now - r.born_ms;
    const bool sticky_rested = gone || gib_sticky_rested(part);
    const bool forced = !sticky_rested && age > kGibStickyMaxMs; // card 273: bound on the sticky state machine
    const bool rested = gone || ((still || age > kGibMaxMs) && sticky_rested) || forced; // card 273
    if (forced) {
        if (note_event_cap(g_gh.sticky_lines, kGibGroundLines, "card273 host sticky lines")) {
            // Tick 0x14513AE50: state 2 counts +0x330 up to +0x334, state 3 counts +0x338 up to +0x33C.
            log::line("WARN card273: host gib part %u sticky state %d not at rest after %llu ms: rested where it is"
                      "; slide timer %.2f of %.2f s, ceiling timer %.2f of %.2f s", r.id, gib_sticky_state(part),
                      static_cast<unsigned long long>(age), read_f32(part, kGibSlideTimer), read_f32(part, 0x334),
                      read_f32(part, kGibCeilingTimer), read_f32(part, 0x33C));
        }
    }
    if (rested) { // card 148: gone is a G row (its pose is a mid-flight one)
        const bool no_ground = !gone && gib_host_ground(part, r.id);
        gib_queue_rest(r.id, gone ? 'G' : 'R', at, rot, no_ground);
        g_gh.rest_gone += gone ? 1u : 0u;
        g_gh.rest_still += !gone && still ? 1u : 0u;
        g_gh.rest_max += !gone && !still ? 1u : 0u;
        r.live = false;
        return;
    }
    if (vec_dist(at, r.sent_at) < kGibMoveUu && gib_turn(rot, r.sent_rot) < kGibTurnDeg) {
        return;
    }
    if (!gib_send_pose(r, at, rot)) {
        ++g_gh.deferred;
        r.pending = true;
        return;
    }
    ++g_gh.samples;
}

// S rows still to go (card 201: a cancelled slot, id 0, is not one).
uint32_t gib_spawns_waiting() {
    const GibSpawnQ& q = g_gib_spawn;
    uint32_t n = 0;
    for (int32_t k = 0; k < q.count && k < kGibSpawnQ; ++k) {
        n += q.id[(q.head + k) % kGibSpawnQ] != 0 ? 1u : 0u;
    }
    return n;
}

// Host, per tick: refill the budget once a second, send waiting S and R rows, sample each live part that is due,
// send the frame. Card 137 run 2 P4: one line per 1 s budget window that deferred or replaced a sample or left S or R
// rows waiting, so a run shows whether the budget starves the flight bound. Capped per map; the totals count every
// window, with the window peak (card 140 run 2: at most kGibBudgetBytes).
void gib_window_line(int32_t spent) {
    g_gh.spent_peak = spent > g_gh.spent_peak ? spent : g_gh.spent_peak;
    const uint32_t s_wait = gib_spawns_waiting();
    const uint32_t samples = g_gh.samples - g_gh.win_samples;
    const uint32_t deferred = g_gh.deferred - g_gh.win_deferred;
    const uint32_t replaced = g_gh.replaced - g_gh.win_replaced;
    g_gh.win_samples = g_gh.samples;
    g_gh.win_deferred = g_gh.deferred;
    g_gh.win_replaced = g_gh.replaced;
    const bool busy = deferred != 0 || replaced != 0 || s_wait != 0 || g_gh.rest_count != 0;
    if (busy && note_event_cap(g_gh.win_lines, kGibWindowLines, "card137 gib windows")) {
        log::line("card137: host gib window t=%llu: %u samples sent, %u deferred, %u replaced, %d of %d bytes spent; "
                  "waiting S %u, R %d", static_cast<unsigned long long>(g_gh.budget_ms), samples, deferred, replaced,
                  spent, kGibBudgetBytes, s_wait, g_gh.rest_count);
    }
}

void gib_host_tick(ULONGLONG now) {
    const int32_t pcs = c95_ponged_count(g_gh.world); // card 201: a PC left or joined; the mirror starts empty
    if (pcs != g_gh.pack_pcs) {
        gib_pack_reset();
        ++g_gh.pack_pc_resets;
        g_gh.pack_pcs = pcs;
    }
    if (now - g_gh.budget_ms >= 1000) {
        // card 201 run 2: the first refill after map start ends no real window (budget_ms 0, budget 0): spent 0
        gib_window_line(g_gh.budget_ms != 0 ? kGibBudgetBytes - g_gh.budget : 0);
        g_gh.budget_ms = now;
        g_gh.budget = kGibBudgetBytes;
    }
    gib_send_waiting(now);
    for (int32_t k = 0; k < kGibRows; ++k) {
        HostGib& r = g_gh.row[(g_gh.start + k) % kGibRows];
        if (r.live && now >= r.next_ms) {
            gib_host_sample(r, now);
        }
    }
    g_gh.start = (g_gh.start + 1) % kGibRows;
    // Card 519 step b (c263e: 5267 GibPose frames, about 14.6 B of chan framing each): the pending frames go out at
    // most every kGibFlushMs, as card 518 does for Fx. GetTickCount64 steps by about 15.6 ms, so a row waits under
    // 47 ms (latency target 100 ms). A full frame still goes at once (gib_append, gib_pose_put).
    if (now - g_gh.flush_ms >= kGibFlushMs) {
        gib_flush();
        g_gh.flush_ms = now;
    }
}

// Client: the mapped part of host id `id` (a flight row), or null.
ClientGib* gib_client_find(uint32_t id) {
    for (int32_t i = 0; i < kGibClientRows; ++i) {
        if (g_gc.row[i].state == kFlight && g_gc.row[i].id == id) {
            return &g_gc.row[i];
        }
    }
    return nullptr;
}

ClientGib* gib_client_free() {
    for (int32_t i = 0; i < kGibClientRows; ++i) {
        if (g_gc.row[i].state == kFree) {
            return &g_gc.row[i];
        }
    }
    return nullptr;
}

// Card 201: a package path ("/Game/.../Name", no '.') rebuilt to its class "/Game/.../Name.Name_C" in place; a full
// path is kept. False: no room (the caller fails the spawn, counted).
bool gib_client_path(char* path, size_t cap) {
    const char* const slash = strrchr(path, '/');
    if (slash == nullptr || strchr(path, '.') != nullptr) {
        return true;
    }
    char full[kGibPathChars];
    const int w = snprintf(full, sizeof(full), "%s.%s_C", path, slash + 1);
    if (w <= 0 || static_cast<size_t>(w) >= sizeof(full) || static_cast<size_t>(w) >= cap) {
        return false;
    }
    memcpy(path, full, static_cast<size_t>(w) + 1);
    ++g_gc.rebuilt;
    return true;
}

// Client: the rows the new piece's pool actor had (a pool actor taken back and reused) end here; returns the rows of
// the piece's kind still in use (card 201: the pool's live count, against kC42PoolMax).
int32_t gib_client_reuse(const void* part, int8_t kind) {
    int32_t in_use = 0;
    for (int32_t i = 0; i < kGibClientRows; ++i) {
        ClientGib& old = g_gc.row[i];
        const bool same = old.state != kFree && c31_weak_get(old.part) == part;
        g_gc.reused += same ? 1u : 0u; // card 148: counted; that row's later M, R or G rows are unmapped (no row)
        old.state = same ? kFree : old.state;
        in_use += old.state != kFree && old.kind == kind ? 1 : 0;
    }
    return in_use;
}

void gib_client_spawn_fail(uint32_t id, const char* why, const char* path) {
    ++g_gc.spawn_fail;
    if (note_event_cap(g_gc.lines, kGibLines, "card137 gib lines")) {
        log::line("WARN card137: client gib spawn of host part %u failed: %s (%s)", id, why, path);
    }
}

void gib_client_spawn(void* pc, const char* row) {
    uint32_t id = 0;
    Vec3 at{}, a{}, b{}, c{};
    float f = 0;
    char path[kGibPathChars] = {};
    const int got = sscanf_s(row, "S %u %lf %lf %lf %f %lf %lf %lf %lf %lf %lf %lf %lf %lf %255s", &id, &at.x, &at.y,
                             &at.z, &f, &a.x, &a.y, &a.z, &b.x, &b.y, &b.z, &c.x, &c.y, &c.z, path,
                             static_cast<unsigned>(sizeof(path)));
    ClientGib* const slot = got == 15 ? gib_client_free() : nullptr;
    if (got == 15 && slot == nullptr) {
        ++g_gc.overflow; // card 148: capped (the play build logged 581 of these)
        if (note_event_cap(g_gc.lines, kGibLines, "card137 gib lines")) {
            log::line("WARN card137: client gib table full (%d parts): host part %u not mapped (overflow %u)",
                      kGibClientRows, id, g_gc.overflow);
        }
        return;
    }
    const uint32_t rebuilt_before = g_gc.rebuilt;
    if (got != 15 || !gib_client_path(path, sizeof(path))) {
        gib_client_spawn_fail(id, got != 15 ? "bad row" : "rebuilt class path too long", path);
        return;
    }
    const bool rebuilt = g_gc.rebuilt != rebuilt_before;
    void* const data_cls = c74_find_asset(path, g_uclass_class());
    g_gc.rebuilt_bad += rebuilt && data_cls == nullptr ? 1u : 0u; // card 201: rejected, never a silent no-spawn
    void* const part = data_cls != nullptr ? c42_spawn_exact(pc, data_cls, at, f, a, b, c) : nullptr;
    if (part == nullptr) {
        gib_client_spawn_fail(id, data_cls != nullptr ? "no pool or no spawn"
                                  : (rebuilt ? "rebuilt class path not found" : "class not found"), path);
        return;
    }
    const int32_t k = c42_kind(data_cls);
    *slot = ClientGib{}; // a reused slot keeps no field of its old part (card 279: c279_owed)
    slot->kind = static_cast<int8_t>(k);
    const int32_t in_use = gib_client_reuse(part, slot->kind) + 1;
    if (k >= 0 && k < static_cast<int32_t>(kC42Kinds) && in_use > g_gc.kind_peak[k]) {
        g_gc.kind_peak[k] = in_use;
    }
    c31_weak_set(slot->part, part);
    gib_sticky_hold(part); // card 273
    slot->id = id;
    slot->state = kFlight;
    slot->spawn_ms = GetTickCount64();
    ++g_gc.spawns;
}

void gib_bucket(double d) {
    for (int32_t k = 0; k < kGibBuckets; ++k) {
        if (d <= kGibBucketTop[k]) {
            ++g_gc.hist[k];
            return;
        }
    }
}

// Client: a row for an id with no flight row, or whose part has no root (card 148: counted by reason, first lines logged).
void gib_unmapped(char kind, uint32_t id, ClientGib* r) {
    ++g_gc.unmapped;
    g_gc.no_row += r == nullptr ? 1u : 0u;
    g_gc.no_root += r != nullptr ? 1u : 0u;
    if (note_event_cap(g_gc.unmapped_lines, kGibUnmappedLines, "card148 unmapped lines")) {
        log::line("card148: client gib %c row for host part %u unmapped: %s (unmapped %u)", kind, id,
                  r == nullptr ? "no row" : "no root", g_gc.unmapped);
    }
    if (r != nullptr) {
        r->state = kFree;
    }
}

// Client, card 148: a 'G' row. The host's part was taken back before it rested, so the piece's root is hidden (the
// host's own taken-back state; the pool's next spawn shows it) and the row ends. It is not put to sleep in the air.
// Card 519: a G row's apply (text or GibPose).
void gib_client_gone_id(uint32_t id) {
    ClientGib* const r = gib_client_find(id);
    void* const part = r != nullptr ? c31_weak_get(r->part) : nullptr;
    void* const root = part != nullptr ? read_ptr(part, kActorRootComponent) : nullptr;
    if (root == nullptr || g_orig_c132_set_hidden == nullptr) {
        gib_unmapped('G', id, r);
        return;
    }
    g_orig_c132_set_hidden(root, true, 0); // EVisibilityPropagation::NoPropagation, as the dropship's hide
    r->state = kFree;
    ++g_gc.gone;
}

void gib_client_gone(const char* row) {
    uint32_t id = 0;
    if (sscanf_s(row, "G %u", &id) != 1) {
        ++g_gc.bad_rows;
        if (note_event_cap(g_gc.lines, kGibLines, "card137 gib lines")) {
            log::line("WARN card148: client gib row not parsed (bad rows %u): %.80s", g_gc.bad_rows, row);
        }
        return;
    }
    gib_client_gone_id(id);
}

// Client, card 279 step 1: an R row whose part lay over 10 uu from the rest point before the snap: what the snap
// returned and where the part is right after it. gib_c279_line adds where it is one tick later.
void gib_c279_snap(ClientGib& r, void* part, const Vec3& at, double before, bool set) {
    r.c279_owed = before > 10.0 && g_gc.c279_lines < kC279Lines;
    if (!r.c279_owed) {
        return;
    }
    ++g_gc.c279_lines;
    r.c279_d0 = static_cast<float>(before);
    r.c279_d1 = static_cast<float>(vec_dist(actor_location(part), at));
    r.c279_set = set;
}

// -1: the root is not a primitive, or the query is not resolved.
void gib_c279_line(ClientGib& r) {
    r.c279_owed = false;
    void* const part = c31_weak_get(r.part);
    void* const root = part != nullptr ? read_ptr(part, kActorRootComponent) : nullptr;
    if (root == nullptr) {
        log::line("card279: client gib R snap host part %u: gone one tick later", r.id);
        return;
    }
    static void* primitive = find_class(L"/Script/Engine.PrimitiveComponent");
    const bool prim = primitive != nullptr && is_a(root, primitive);
    const void* const parent = c140_attach_parent(root);
    log::line("card279: client gib R snap host part %u %s: %.1f uu before the snap, set returned %d, %.1f uu right "
              "after, %.1f uu one tick later; simulating %d, awake %d, root attach parent %s", r.id,
              object_name(part).text, r.c279_d0, r.c279_set ? 1 : 0, r.c279_d1,
              vec_dist(actor_location(part), r.rest_at),
              prim && g_prim_simulating != nullptr ? (g_prim_simulating(root, 0) ? 1 : 0) : -1,
              prim && g_prim_awake != nullptr ? (g_prim_awake(root, 0) ? 1 : 0) : -1,
              parent != nullptr ? object_name(parent).text : "none");
}

// Client, card 279 step 2. Runs c423b124a to b126a: every R snap that did not hold (5 rows) was on a part whose root
// was not simulating at the snap, and no snap on a simulating root missed (29 rows). A part at a low significance
// level has physics and collision off (ASTGorePartBase::SetPhysicsEnabled(false) from OnSignificanceChanged); the
// snap lands, and the part is back at its old place within 1 s. So the R row switches the part's physics on through
// the part's own virtual before the snap; the body is then put to sleep by the caller as on every R row.
constexpr size_t kGibVtSetPhysics = 0x778; // ASTGorePartBase::SetPhysicsEnabled(bool), as OnSignificanceChanged calls it
bool gib_c279_physics_on(void* part, void* root) {
    static void* primitive = find_class(L"/Script/Engine.PrimitiveComponent");
    void* const base = g_gore_part_class != nullptr ? g_gore_part_class() : nullptr;
    if (base == nullptr || !is_a(part, base) || primitive == nullptr || !is_a(root, primitive) ||
        g_prim_simulating == nullptr || g_prim_simulating(root, 0)) {
        return false;
    }
    using SetPhysicsFn = void (*)(void* part, bool enabled);
    const auto set = reinterpret_cast<SetPhysicsFn>(read_ptr(read_ptr(part, 0), kGibVtSetPhysics));
    if (set == nullptr) {
        return false;
    }
    set(part, true);
    ++g_gc.c279_switched;
    return true;
}

// Client, card 279 step 3. Run c423b138a: all 8 rest misses over 10 uu were parts with physics off at the R row;
// the snap landed and the part was back at its old place, not simulating, at the check. The mover is not known,
// so until the check a physics-off part found over 2 uu from its rest point is put back each tick, and the first
// time per part is logged with the part's state (capped). The check reads before this tick's hold, so a mover
// that acts every frame still shows as a miss.
void gib_c279_hold(ClientGib& r, ULONGLONG now) {
    void* const part = c31_weak_get(r.part);
    void* const root = part != nullptr ? read_ptr(part, kActorRootComponent) : nullptr;
    if (root == nullptr) {
        return;
    }
    const double d = vec_dist(actor_location(part), r.rest_at);
    if (d <= 2.0) {
        return;
    }
    if (r.c279_holds == 0) {
        ++g_gc.c279_held_parts;
        if (note_event_cap(g_gc.c279_hold_lines, kC279HoldLines, "card279 hold lines")) {
            const int sim = g_prim_simulating != nullptr ? (g_prim_simulating(root, 0) ? 1 : 0) : -1;
            const int awake = g_prim_awake != nullptr ? (g_prim_awake(root, 0) ? 1 : 0) : -1;
            log::line("card279: client gib host part %u %s first found %.1f uu off its rest point %llu ms after "
                      "the R row (%.1f uu before the snap): simulating %d, awake %d, significance physics flag "
                      "%d; put back", r.id, object_name(part).text, d,
                      static_cast<unsigned long long>(now - r.rest_ms), r.rest_from, sim, awake,
                      read_i32(part, kGorePhysFlag) & 0xFF);
        }
    }
    ++r.c279_holds;
    ++g_gc.c279_holds;
    g_set_actor_location(part, &r.rest_at, false, nullptr, kTeleportPhysics);
    g_set_world_rotation(root, r.rest_rot, false, nullptr, kTeleportPhysics);
    // Card 442 step 2 (run c423b144a): a held part was simulating and awake and fell again each tick; the teleport
    // keeps its velocity, so the miss at the check grew to one tick of fall at 980 uu/s (16.3 uu). Sleep zeroes it.
    // c279_was_off means gib_c279_physics_on saw a PrimitiveComponent root.
    if (g_prim_sleep != nullptr) {
        g_prim_sleep(root, 0);
    }
}

void gib_client_apply(char kind, uint32_t id, const Vec3& at, double* rot, int flag, bool late);

// Client: a sample ('M') or the rest row ('R'): teleport to the host's pose; the rest row also sleeps the body.
// after_s: the host id of the S row just before this row in the frame (0 none).
void gib_client_pose(const char* row, uint32_t after_s) {
    char kind = 0;
    uint32_t id = 0;
    Vec3 at{};
    double rot[3] = {};
    int flag = 0; // card 273: the R row's ninth field, the host's no-ground result
    const int got = sscanf_s(row, "%c %u %lf %lf %lf %lf %lf %lf %d", &kind, 1u, &id, &at.x, &at.y, &at.z, &rot[0],
                             &rot[1], &rot[2], &flag);
    if (got != (kind == 'R' ? 9 : 8)) { // card 148: 8 conversions; the old != 9 dropped every M and R row silently (hb72)
        ++g_gc.bad_rows;
        if (note_event_cap(g_gc.lines, kGibLines, "card137 gib lines")) {
            log::line("WARN card148: client gib row not parsed (bad rows %u): %.80s", g_gc.bad_rows, row);
        }
        return;
    }
    gib_client_apply(kind, id, at, rot, flag, id == after_s);
}

// Card 519: an M or R row's apply (text or GibPose). late: the row came right after its part's S row (card 201 run 2).
void gib_client_apply(char kind, uint32_t id, const Vec3& at, double* rot, int flag, bool late) {
    ClientGib* const r = gib_client_find(id);
    void* const part = r != nullptr ? c31_weak_get(r->part) : nullptr;
    void* const root = part != nullptr ? read_ptr(part, kActorRootComponent) : nullptr;
    if (root == nullptr) {
        gib_unmapped(kind, id, r);
        return;
    }
    const ULONGLONG now = GetTickCount64();
    const double d = vec_dist(actor_location(part), at);
    if (kind == 'M') {
        gib_bucket(d);
        g_gc.flight_worst = d > g_gc.flight_worst ? d : g_gc.flight_worst;
        ++g_gc.samples;
        r->late_pose = r->late_pose || (r->m_rows == 0 && late);
        ++r->m_rows;
        r->last_m_ms = now;
    }
    gib_sticky_hold(part); // card 273
    r->c279_was_off = kind == 'R' ? gib_c279_physics_on(part, root) : r->c279_was_off;
    const bool set = g_set_actor_location(part, &at, false, nullptr, kTeleportPhysics);
    g_set_world_rotation(root, rot, false, nullptr, kTeleportPhysics);
    if (kind == 'R') {
        gib_c279_snap(*r, part, at, d, set); // card 279
        r->host_no_ground = flag != 0;
        static void* primitive = find_class(L"/Script/Engine.PrimitiveComponent");
        if (g_prim_sleep != nullptr && primitive != nullptr && is_a(root, primitive)) {
            g_prim_sleep(root, 0); // NAME_None: the root body
        }
        r->rest_at = at;
        r->rest_rot[0] = rot[0];
        r->rest_rot[1] = rot[1];
        r->rest_rot[2] = rot[2];
        r->c279_holds = 0;
        r->rest_from = d;
        r->rest_ms = now;
        r->check_ms = now + kGibCheckMs;
        r->state = kCheck;
        g_gc.checks = true;
        ++g_gc.rests;
    }
}

// Client, card 148: the receiver-side ground check (gib_no_ground); no ground is a piece asleep above the ground
// (finding 14), counted with its first lines.
// Card 273: a part the host flagged (no ground below its own part either) is counted with no line.
void gib_ground_check(void* part, uint32_t id, bool host_no_ground) {
    Vec3 bottom{};
    const char* bounds = "";
    if (!gib_no_ground(part, &bottom, &bounds)) {
        return;
    }
    ++g_gc.above;
    if (host_no_ground) {
        ++g_gc.above_flagged;
        return;
    }
    if (note_event_cap(g_gc.ground_lines, kGibGroundLines, "card148 client ground lines")) {
        log::line("WARN card148: client gib host part %u %s asleep above the ground: nothing within %.0f uu below "
                  "%.0f %.0f %.0f (%s, above %u)", id, object_name(part).text, kGibGroundUu, bottom.x, bottom.y,
                  bottom.z, bounds, g_gc.above);
    }
}

// Client, card 201 run 2: a rest check over 2 uu, with its row's history (counted, own capped lines). The last M row's
// lead on the R row is -1 when the row had no M row; awake is -1 when the root is not a primitive.
void gib_miss_line(const ClientGib& r, void* part, double d, ULONGLONG now) {
    ++g_gc.misses;
    if (!note_event_cap(g_gc.miss_lines, kGibMissLines, "card201 client rest miss lines")) {
        return;
    }
    static void* primitive = find_class(L"/Script/Engine.PrimitiveComponent");
    void* const root = read_ptr(part, kActorRootComponent);
    const bool prim = root != nullptr && g_prim_awake != nullptr && primitive != nullptr && is_a(root, primitive);
    const int awake = prim ? (g_prim_awake(root, 0) ? 1 : 0) : -1;
    const int sim = prim && g_prim_simulating != nullptr ? (g_prim_simulating(root, 0) ? 1 : 0) : -1;
    const long long m_lead = r.m_rows != 0 ? static_cast<long long>(r.rest_ms - r.last_m_ms) : -1;
    log::line("card201: client gib rest miss host part %u %s: %.1f uu from the host's rest point at the check; S row "
              "applied %llu ms before the check, late pose %d; M rows %u, last M row %lld ms before the R row; %.1f uu "
              "from the R point when the R row was applied; body awake at the check %d (misses %u); card273: sticky state "
              "%d; card279: physics off at the R row %d (switched on for the snap, %u this map), simulating at the check %d",
              r.id, object_name(part).text, d, static_cast<unsigned long long>(now - r.spawn_ms), r.late_pose ? 1 : 0,
              r.m_rows, m_lead, r.rest_from, awake, g_gc.misses, gib_sticky_state(part), r.c279_was_off ? 1 : 0,
              g_gc.c279_switched, sim);
}

// Client, per tick: kGibCheckMs after a rest row, where the part lies against the host's rest point.
void gib_client_tick(ULONGLONG now) {
    // Card 414: with no row waiting for its rest check the table is not read (it was 1536 rows every frame; run
    // c414b109a: 24 of the client's 600 mod samples).
    if (!g_gc.checks) {
        return;
    }
    g_gc.checks = false;
    for (int32_t i = 0; i < kGibClientRows; ++i) {
        ClientGib& r = g_gc.row[i];
        if (r.state == kCheck && r.c279_owed && now >= r.rest_ms + kC279TickMs) {
            gib_c279_line(r); // card 279
        }
        if (r.state != kCheck || now < r.check_ms) {
            if (r.state == kCheck && r.c279_was_off && now > r.rest_ms) {
                gib_c279_hold(r, now); // card 279 step 3
            }
            g_gc.checks = g_gc.checks || r.state == kCheck;
            continue;
        }
        r.state = kFree;
        void* const part = c31_weak_get(r.part);
        if (part == nullptr) {
            ++g_gc.check_gone;
            continue;
        }
        const double d = vec_dist(actor_location(part), r.rest_at);
        ++g_gc.checked;
        g_gc.within2 += d <= 2.0 ? 1 : 0;
        g_gc.within10 += d <= 10.0 ? 1 : 0;
        g_gc.rest_worst = d > g_gc.rest_worst ? d : g_gc.rest_worst;
        if (d > 10.0 && note_event_cap(g_gc.lines, kGibLines, "card137 gib lines")) {
            log::line("WARN card137: client gib host part %u %s lies %.1f uu from the host's rest point", r.id,
                      object_name(part).text, d);
        }
        if (d > 2.0) {
            gib_miss_line(r, part, d, now);
        }
        gib_ground_check(part, r.id, r.host_no_ground);
    }
}

} // namespace

ImpartFn g_orig_c137_impart_base = nullptr;
ImpartFn g_orig_c137_impart_mesh = nullptr;

void detour_c137_impart_base(void* part, const Vec3* a, const Vec3* b, const Vec3* c) {
    gib_host_detour(g_orig_c137_impart_base, part, a, b, c);
}

void detour_c137_impart_mesh(void* part, const Vec3* a, const Vec3* b, const Vec3* c) {
    gib_host_detour(g_orig_c137_impart_mesh, part, a, b, c);
}

// Client: ChanOp::GibPart, rows split by ';'.
void c137_receive_gib(void* pc, const WireString* s) {
    char text[kChanTextMax + 1];
    if (chan_text(s, text, sizeof(text)) <= 0) { // card 148: counted, first lines logged
        ++g_gc.empty;
        if (note_event_cap(g_gc.lines, kGibLines, "card137 gib lines")) {
            log::line("WARN card148: client GibPart frame with no text (%u so far)", g_gc.empty);
        }
        return;
    }
    char* next = nullptr;
    uint32_t after_s = 0; // card 201 run 2: the id of the S row just before (a late S row's pose follows it)
    for (char* row = strtok_s(text, ";", &next); row != nullptr; row = strtok_s(nullptr, ";", &next)) {
        const uint32_t prev_s = after_s;
        after_s = 0;
        if (row[0] == 'S') {
            uint32_t s_id = 0;
            after_s = sscanf_s(row, "S %u", &s_id) == 1 ? s_id : 0;
            gib_client_spawn(pc, row);
        } else if (row[0] == 'M' || row[0] == 'R') {
            gib_client_pose(row, prev_s);
        } else if (row[0] == 'G') {
            gib_client_gone(row);
        } else {
            ++g_gc.bad_rows; // card 148: an unknown row kind is counted, never dropped silently
        }
    }
}

namespace {

// Card 519: GibPose reads; false at the frame's end or a varint over 10 bytes.
bool gib_get_varint(const uint8_t* b, int32_t n, int32_t& at, uint64_t& v) {
    v = 0;
    for (int32_t k = 0; k < 10 && at < n; ++k) {
        const uint8_t c = b[at++];
        v |= static_cast<uint64_t>(c & 0x7Fu) << (7 * k);
        if ((c & 0x80u) == 0) {
            return true;
        }
    }
    return false;
}
bool gib_get_zigzag(const uint8_t* b, int32_t n, int32_t& at, int64_t& v) {
    uint64_t u = 0;
    if (!gib_get_varint(b, n, at, u)) {
        return false;
    }
    v = static_cast<int64_t>(u >> 1) ^ -static_cast<int64_t>(u & 1u);
    return true;
}

// One GibPose row at bytes[at], applied; false: a malformed row (the rest of the frame is dropped).
bool gib_pose_row(const uint8_t* b, int32_t n, int32_t& at, uint32_t& last_id, int64_t* last) {
    const uint8_t tag = b[at++];
    int64_t did = 0;
    if (tag == kGibTagNone || tag >= kGibTags || !gib_get_zigzag(b, n, at, did)) {
        return false;
    }
    const uint32_t id = static_cast<uint32_t>(static_cast<int64_t>(last_id) + did);
    last_id = id;
    if (tag == kGibTagG) {
        gib_client_gone_id(id);
        return true;
    }
    int64_t p[3] = {};
    for (int32_t k = 0; k < 3; ++k) {
        if (!gib_get_zigzag(b, n, at, p[k])) {
            return false;
        }
        p[k] += last[k];
        last[k] = p[k];
    }
    if (at + 3 > n) {
        return false;
    }
    double rot[3];
    for (double& v : rot) {
        v = static_cast<double>(b[at++]) * 360.0 / 256.0;
        v = v > 180.0 ? v - 360.0 : v;
    }
    const Vec3 pos{static_cast<double>(p[0]), static_cast<double>(p[1]), static_cast<double>(p[2])};
    const bool rest = tag == kGibTagR0 || tag == kGibTagR1;
    gib_client_apply(rest ? 'R' : 'M', id, pos, rot, tag == kGibTagR1 ? 1 : 0, tag == kGibTagL);
    return true;
}

} // namespace

// Client, card 519: ChanOp::GibPose, binary M, L, R and G rows (gib_pose_put).
void c137_receive_gib_pose(void* pc, const WireString* s) {
    (void)pc;
    static uint8_t bytes[kChanBlobBytes]; // game thread only
    const int32_t n = ue::chan::chan_blob(s, bytes, kChanBlobBytes);
    if (n <= 0) {
        ++g_gc.empty;
        if (note_event_cap(g_gc.lines, kGibLines, "card137 gib lines")) {
            log::line("WARN card519: client GibPose frame with no bytes (%u so far)", g_gc.empty);
        }
        return;
    }
    uint32_t last_id = 0;
    int64_t last[3] = {0, 0, 0};
    int32_t at = 0;
    for (int32_t k = 0; k < kChanBlobBytes && at < n; ++k) {
        const int32_t row_at = at;
        if (!gib_pose_row(bytes, n, at, last_id, last)) {
            ++g_gc.bad_rows;
            if (note_event_cap(g_gc.lines, kGibLines, "card137 gib lines")) {
                log::line("WARN card519: client GibPose bad row at byte %d of %d (tag %u, bad rows %u): rest of frame "
                          "dropped", row_at, n, static_cast<unsigned>(bytes[row_at]), g_gc.bad_rows);
            }
            return;
        }
    }
}

void c137_gib_tick(ULONGLONG now) {
    if (g_client_session) {
        gib_client_tick(now);
    } else if (g_gh.world != nullptr) {
        gib_host_tick(now);
    }
}

namespace {

void gib_host_dump(const char* why) {
    uint32_t live = 0;
    uint32_t pending = 0;
    for (int32_t i = 0; i < kGibRows; ++i) {
        live += g_gh.row[i].live ? 1u : 0u;
        pending += g_gh.row[i].live && g_gh.row[i].pending ? 1u : 0u;
    }
    // live = parts whose R row is not sent yet (at quit: R unsent); pending = deferred samples not yet replaced
    log::line("card137: host gib totals (%s): %u spawn rows, %u samples, %u rest rows (%u at a pool reuse), "
              "%u frames, %llu bytes; samples deferred by the budget %u, replaced by a newer row %u, deferred "
              "unsent %u; R unsent (live) %u, overflow %u", why, g_gh.spawns, g_gh.samples, g_gh.rests,
              g_gh.reused, g_gh.frames, static_cast<unsigned long long>(g_gh.bytes), g_gh.deferred,
              g_gh.replaced, pending, live, g_gh.overflow);
    const uint32_t* const pr = g_gh.pose_rows;
    log::line("card519: host gib pose rows (%s): M %u, L %u, R %u (no ground %u), G %u; %llu bytes in %u frames "
              "(partial fanout %u); never sent (pending at the dump, card 519 step b): %d text, %d pose bytes", why,
              pr[kGibTagM], pr[kGibTagL], pr[kGibTagR0] + pr[kGibTagR1], pr[kGibTagR1], pr[kGibTagG],
              static_cast<unsigned long long>(g_gh.pose_bytes), g_gh.pose_frames, g_gh.pose_partial, g_gh.used,
              g_gh.pose_used);
    log::line("card148: host gib rest rows (%s): %u still, %u at the %llu ms bound, %u gone, %u at a pool relaunch; "
              "%u with no ground below", why, g_gh.rest_still, g_gh.rest_max,
              static_cast<unsigned long long>(kGibMaxMs), g_gh.rest_gone, g_gh.rest_relaunch, g_gh.rest_above);
    const uint32_t waiting = gib_spawns_waiting();
    // every S row recorded is sent, replaced at a relaunch, cancelled gone, waiting or not sent at a full queue: lost 0
    const uint32_t lost = g_gh.recorded - g_gh.spawns - g_gh.spawn_replaced - g_gh.spawn_cancelled - waiting -
                          g_gh.spawn_full;
    log::line("card140: host gib budget (%s): window peak %d of %d packed bytes, S recorded %u, sent %u (late %u), "
              "replaced at a relaunch %u, cancelled gone %u, waiting %u, lost %u, not sent at a full queue %u (peak "
              "waiting %d of %d), longest S wait %llu ms; R queued unsent %d, not sent at a full queue %u (peak "
              "waiting %d of %d)", why, g_gh.spent_peak, kGibBudgetBytes, g_gh.recorded, g_gh.spawns,
              g_gh.spawn_late, g_gh.spawn_replaced, g_gh.spawn_cancelled, waiting, lost, g_gh.spawn_full,
              g_gh.spawn_peak, kGibSpawnQ, static_cast<unsigned long long>(g_gh.spawn_wait_max), g_gh.rest_count,
              g_gh.rest_full, g_gh.rest_peak, kGibRestQ);
    log::line("card201: host gib class paths (%s): %u sent as a package path, %u sent in full; wire charge: mirror "
              "resets %u (PC count %u, send failed %u), frame rollbacks %u, path defs committed %u, untracked %u, "
              "true-up bytes %llu", why, g_gh.path_pkg, g_gh.path_full, g_gh.pack_resets, g_gh.pack_pc_resets,
              g_gh.pack_send_resets, g_gh.pack_rollbacks, g_gh.pack_commits, g_gh.pack_untracked,
              static_cast<unsigned long long>(g_gh.trueup_bytes));
}

void gib_client_dump(const char* why) {
    uint32_t live = 0;
    for (int32_t i = 0; i < kGibClientRows; ++i) {
        live += g_gc.row[i].state != kFree ? 1u : 0u;
    }
    log::line("card137: client gib table (%s): %u entries in use", why, live);
    uint32_t half = 0;
    int32_t median = 0;
    for (int32_t k = 0; k < kGibBuckets && half * 2 < g_gc.samples; ++k) {
        half += g_gc.hist[k];
        median = k;
    }
    log::line("card137: client gib totals (%s): %u spawned, %u spawn failures, %u samples, %u rest rows, %u unmapped "
              "rows, bad rows %u, overflow %u; in flight worst %.1f uu, median %s %.0f uu, over 150 uu %u; rest checked %u, within "
              "2 uu %u, within 10 uu %u, worst %.1f uu, gone before the check %u", why, g_gc.spawns, g_gc.spawn_fail,
              g_gc.samples, g_gc.rests, g_gc.unmapped, g_gc.bad_rows, g_gc.overflow, g_gc.flight_worst,
              median == kGibBuckets - 1 ? ">" : "<=",
              g_gc.samples == 0 ? 0.0 : kGibBucketTop[median == kGibBuckets - 1 ? median - 1 : median],
              g_gc.hist[kGibBuckets - 1], g_gc.checked, g_gc.within2,
              g_gc.within10, g_gc.rest_worst, g_gc.check_gone);
    log::line("card148: client gib rows (%s): %u gone rows hidden, unmapped %u (no row %u, no root %u), freed by a pool "
              "reuse %u, empty frames %u, overflow %u of %d rows; rest checked %u, above the ground %u (host flagged too %u)",
              why, g_gc.gone, g_gc.unmapped, g_gc.no_row, g_gc.no_root, g_gc.reused, g_gc.empty, g_gc.overflow,
              kGibClientRows, g_gc.checked, g_gc.above, g_gc.above_flagged);
    const int32_t* const pk = g_gc.kind_peak;
    log::line("card201: client gib pools (%s): package paths rebuilt %u, not resolved %u; rows in use at once per kind "
              "(sticky sprite, mesh, skeletal; non-sticky sprite, mesh, skeletal) %d %d %d %d %d %d of %d; rest checks over 2 uu %u", why,
              g_gc.rebuilt, g_gc.rebuilt_bad, pk[0], pk[1], pk[2], pk[3], pk[4], pk[5], kC42PoolMax, g_gc.misses);
    log::line("card279: client gib holds (%s): physics switched on at the R row %u, parts found off their rest "
              "point before the check %u, put back %u times", why, g_gc.c279_switched, g_gc.c279_held_parts,
              g_gc.c279_holds);
}

} // namespace

// Both tables, whichever is non-empty: g_client_session is already false when a dropped client reaches the frontend
// (hb68 lost the client totals that way).
void c137_gib_dump(const char* why) {
    if (g_gh.recorded != 0 || g_gh.overflow != 0) {
        gib_host_dump(why);
    }
    if (g_gc.spawns != 0 || g_gc.spawn_fail != 0 || g_gc.overflow != 0 || g_gc.unmapped != 0 || g_gc.bad_rows != 0 ||
        g_gc.empty != 0) {
        gib_client_dump(why);
    }
}

// Card 326: the registry's "gib" map end (was the arm).
void c137_gib_map_end() {
    c137_gib_dump("map change");
    g_gh = GibHost{};
    g_gib_spawn.head = 0; // its waiting S rows were for the old map's parts
    g_gib_spawn.count = 0;
    g_gc = GibClient{};
}

// Card 323 "gib" join replay (host): the joiner never got the S rows of parts launched before its first Pong, so every
// row whose part is still shown goes to it alone: an S row at the part's current location with no launch (A, B and C
// zero), then its pose: an R row (with the host's ground flag) for a part at rest, an M row for one in flight, whose
// later M and R rows reach the joiner with every other client. A row whose S row still waits needs nothing (the
// waiting S row goes to every ponged PC). Limits: a rested row stays only until gib_host_entry reuses it for a new
// part (kGibRows), and a host with no ponged PC records nothing (gib_host_record), so the first joiner gets only
// parts launched while another client was in.
namespace {
struct GibJoinOut {
    char text[kChanTextMax + 1];
    int32_t used;
    int32_t frames;
    int32_t failed;
};

void gib_join_append(void* pc, GibJoinOut& o, const char* row) {
    const int32_t len = static_cast<int32_t>(strlen(row));
    if (o.used + len > kChanTextMax && o.used > 0) {
        o.failed += c95_send_one(pc, ChanOp::GibPart, o.text) ? 0 : 1;
        ++o.frames;
        o.used = 0;
        o.text[0] = '\0';
    }
    if (len <= kChanTextMax) {
        memcpy(o.text + o.used, row, static_cast<size_t>(len) + 1);
        o.used += len;
    }
}
} // namespace

void c137_gib_join_replay(void* pc) {
    static GibJoinOut o;
    o = GibJoinOut{};
    int32_t rested = 0;
    int32_t flying = 0;
    int32_t no_class = 0;
    for (int32_t i = 0; i < kGibRows; ++i) {
        const HostGib& r = g_gh.row[i];
        // Card 519 step b: an S row still in the pending text frame reaches the joiner with that frame (the joiner
        // is ponged before this replay, card 139), so the replay would send it twice.
        const bool s_pending = g_gh.used > 0 && r.s_seq == g_gh.text_seq + 1;
        void* const part = r.id != 0 && !r.spawn_unsent && !s_pending ? c31_weak_get(r.part) : nullptr;
        const auto* const root = part != nullptr ? static_cast<const uint8_t*>(read_ptr(part, kActorRootComponent))
                                                 : nullptr;
        if (root == nullptr || (root[kCompHiddenByte] & kCompHiddenBit) != 0) {
            continue;
        }
        char path[kGibPathChars];
        gib_class_path(part, path, kGibPathChars);
        if (path[0] == '-' && path[1] == '\0') {
            // Card 458: no data class, so the joiner could only fail its spawn (class not found); the row names why.
            ++no_class;
            if (note_event_cap(g_gh.join_noclass_lines, kGibJoinNoClassLines, "card458 join rows without a class")) {
                const auto* const actor = static_cast<const uint8_t*>(part);
                log::line("card458: host gib join replay skips part %u: data class %p (+0x310), actor hidden %d, root "
                          "hidden %d, mesh %p (%s), %s",
                          r.id, read_ptr(part, kPartDataClass), (actor[kActorHiddenByte] & kActorHiddenBit) != 0 ? 1 : 0,
                          (root[kCompHiddenByte] & kCompHiddenBit) != 0 ? 1 : 0, static_cast<const void*>(root),
                          class_name(root).text, r.live ? "in flight" : "at rest");
            }
            continue;
        }
        const Vec3 at = actor_location(part);
        char row[kGibSpawnChars];
        snprintf(row, sizeof(row), "S %u %.1f %.1f %.1f %.3f 0 0 0 0 0 0 0 0 0 %s;", r.id, at.x, at.y, at.z,
                 read_f32(part, kPartSpawnFloat), path);
        gib_join_append(pc, o, row);
        Vec3 bottom{};
        const char* bounds = "";
        const int flag = r.live ? -1 : (gib_no_ground(part, &bottom, &bounds) ? 1 : 0);
        gib_pose_text(row, sizeof(row), r.live ? 'M' : 'R', r.id, at, gib_rotation(part), flag);
        gib_join_append(pc, o, row);
        rested += r.live ? 0 : 1;
        flying += r.live ? 1 : 0;
    }
    if (o.used > 0) {
        o.failed += c95_send_one(pc, ChanOp::GibPart, o.text) ? 0 : 1;
        ++o.frames;
    }
    log::line("%scard323: host t=%llu gib join replay to %s: %d at rest, %d in flight, %d frame(s), %d not sent, "
              "%d skipped without a class (card 458)",
              o.failed == 0 ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()), object_name(pc).text,
              rested, flying, o.frames, o.failed, no_class);
}

// Card 326: the host rows still tracked plus the S rows waiting (the registry's lost-rows check).
int32_t c137_gib_live() {
    int32_t n = g_gib_spawn.count;
    for (int32_t i = 0; i < kGibRows; ++i) {
        n += g_gh.row[i].live ? 1 : 0;
    }
    return n;
}

} // namespace ubw::hooks
