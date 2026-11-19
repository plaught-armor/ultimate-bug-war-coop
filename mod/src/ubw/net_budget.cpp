// Card 132 diagnostic: where the host's replication bandwidth to each client goes.
// A client saw NPC Health changes in bursts 20 s apart while the host damaged and killed those NPCs every second.
// UNetConnection sends only while IsNetReady (QueuedBits + SendBuffer bits <= 0), and Tick refills QueuedBits by
// CurrentNetSpeed * DeltaTime * 8, so a low CurrentNetSpeed starves the lower-priority actor channels.
// This file logs, every kNetLogMs on the host: each client connection's CurrentNetSpeed, how many frames it was not
// net ready, the bits UActorChannel::ReplicateActor wrote per actor class (its return value), and the bits each RPC
// added to the connection (QueuedBits + SendBuffer bits after ProcessRemoteFunctionForChannelPrivate minus before).
//
// Offsets (exe 0x140000000 base):
//   UPlayer::CurrentNetSpeed +0x38: APlayerController::SetNetSpeed (exe+0x405F4C0) clamps to [1800, MaxClientRate]
//     and stores it to PC->Player(+0x348)+0x38 and to NetDriver->ServerConnection(+0xF0)+0x38.
//   UNetDriver::MaxClientRate +0xC8: the same SetNetSpeed clamp, NetDriver read from World+0x38.
//   UNetConnection QueuedBits +0x200 (int32) and SendBuffer bit count +0x6C0 (int64): UNetConnection::IsNetReady
//     (exe+0x3E71B20) returns QueuedBits + bits <= 0; Tick (exe+0x3E87FF1) subtracts CurrentNetSpeed * dt * 8.
//   UActorChannel::Actor +0x68: ReplicateActor (exe+0x3B79DF0) reads it first; it returns int64 bits written.
//   UChannel::NumOutRec +0x4C (int32): the FOutBunch constructor (exe+0x3B5FC80) marks a reliable bunch overflowed at
//     0x1FF + bClose outstanding, and ProcessRemoteFunctionForChannelPrivate then closes the connection.
//
// Card 136 run 4: every window also logs, on both sides, the chan bytes sent and received per opcode (ue/chan
// counters, the Fx op first), and on the host the peak NumOutRec read after each RPC, with its channel's actor class.

#include <cmath>

#include "ubw/hooks_internal.h"
#include "ue/chan_pack.h"

namespace ubw::hooks {

ReplicateActorFn g_orig_net_replicate_actor = nullptr;
RemoteFunctionForChannelFn g_orig_net_remote_function = nullptr;

uint64_t c41_guid(const void* actor); // enemies.cpp
const char* c132_op_name(uint8_t op); // ledger_c132.cpp
bool c132_showing_static(const void* owner); // enemies.cpp

namespace {

constexpr size_t kPlayerCurrentNetSpeed = 0x38;
constexpr size_t kWorldNetDriver = 0x38;
constexpr size_t kDriverMaxClientRate = 0xC8;
constexpr size_t kConnQueuedBits = 0x200;
constexpr size_t kConnLastReceiveTime = 0x1D8; // card 391
constexpr size_t kConnLastTickTime = 0x1F8;    // card 391
constexpr size_t kConnOutTotalLost = 0x4F8;    // card 391: OutTotalPacketsLost (0 > 70 in the `c391b78` burst frame)
constexpr size_t kConnSendBufferBits = 0x6C0;
constexpr size_t kActorChannelActor = 0x68;

constexpr ULONGLONG kNetLogMs = 5000;
constexpr uint32_t kNetClasses = 192; // distinct replicated classes and RPC functions tracked per window
constexpr uint32_t kNetTopClasses = 14;
constexpr uint32_t kNetLogWindows = 240; // 20 minutes of lines per map
constexpr size_t kConnChannelNumOutRec = 0x4C;
constexpr uint32_t kNetTopOps = 10; // ops per direction in the chan bytes line, after Fx
constexpr size_t kOpSlots = ue::chan::kChanOpSlots;
constexpr size_t kFxOp = static_cast<size_t>(chan::ChanOp::Fx);

struct NetClassBits {
    const void* cls;  // the actor class (property replication) or the UFunction (RPC)
    const char* kind; // "props" or "rpc"

    uint64_t bits;
    uint32_t calls;
};

struct NetBudget {
    NetClassBits classes[kNetClasses];
    uint32_t class_count;
    uint64_t other_bits; // classes past the table
    uint32_t frames;
    uint32_t not_ready_frames;
    ULONGLONG next_log_ms;
    uint32_t windows;
    int32_t rel_peak;           // this window: the highest NumOutRec after an RPC
    // The channel's actor class names, copied when the peak is noted: the class can be gone by the time the line prints
    // (c166b: BP_PlayerController_C freed by the travel's LoadMap, then read by the map totals line, host crash).
    ObjectName rel_peak_class;
    int32_t rel_map_peak; // this map
    ObjectName rel_map_class;
};

NetBudget g_net{}; // game thread

// Card 140: the real gate, sampled at the start of ubw_post_tick (after the engine tick, before any mod step sends).
// UNetConnection::IsNetReady (exe+0x3E71B20, vtable +0x318): bit 2 of +0xA4 set -> ready; else byte +0x1E20 set ->
// FNetworkCongestionControl::IsReadyToSend; else QueuedBits + SendBuffer bits <= 0. ProcessPrioritizedActorsRange
// (exe+0x3E83C00) counts each not-ready connection in GNumSaturatedConnections (int32, exe+0x93E0238).
constexpr size_t kVtIsNetReady = 0x318;
constexpr uintptr_t kIsNetReadyRva = 0x3E71B20;
constexpr uintptr_t kNumSaturatedRva = 0x93E0238;
constexpr size_t kConnFlagsA4 = 0xA4;
constexpr size_t kConnCongestionSet = 0x1E20;
constexpr uint32_t kStallLinesWindow = 20;
constexpr uint32_t kStallLinesMap = 300;
using IsNetReadyFn = int32_t (*)(void* conn, int32_t saturate); // bool in al

struct NetGate {
    uint32_t frames;
    uint32_t real_not_ready;
    uint32_t flag_a4;
    uint32_t congestion;
    int32_t sat_start; // GNumSaturatedConnections at the window start
    int64_t queued_min;
    int64_t queued_max;
    int64_t sendbuf_max;
    int32_t speed_min;
    int32_t speed_max;
    uint32_t stall_lines;     // this window
    uint32_t stall_lines_map;
    int8_t vt_ok;             // -1 not checked this map, 0 wrong target (not called), 1 checked
};
NetGate net_gate_fresh(int32_t sat_start, uint32_t stall_lines_map, int8_t vt_ok) {
    NetGate g{};
    g.sat_start = sat_start;
    g.stall_lines_map = stall_lines_map;
    g.vt_ok = vt_ok;
    return g;
}
NetGate g_gate = net_gate_fresh(-1, 0, -1); // game thread

// The reliable bunches outstanding on a remote PC's channel after its last RPC (ClientMessage targets the PC).
struct NetPcRel {
    const void* pc;
    int32_t out;
    const void* channel; // the PC's actor channel at its last RPC; card 140 reads NumOutRec from it live
};
NetPcRel g_pc_rel[kC17Slots] = {}; // game thread

// The chan byte counters at the last window line (ue/chan counts from process start), per side's own step.
struct NetChanBytes {
    uint32_t rows[kOpSlots]; // host: frames sent to remote PCs (ue/chan counter) at the last line
    uint64_t sent[kOpSlots];
    uint64_t received[kOpSlots];
    ULONGLONG next_log_ms; // client side only (the host uses g_net's)
    uint32_t windows;      // client side only
};

NetChanBytes g_net_chan{}; // game thread

// Per NPC on the host: the longest gap between two ReplicateActor calls that wrote bits, per window. hb43 showed client
// copies whose ReplicatedMovement.Location matched the host's location of 1-2 s earlier while the WarriorBug class
// averaged ~9 writes per second per actor; this tells a host that skipped an actor from a client that applied late.
constexpr uint32_t kNetNpcs = 256;
constexpr uint32_t kNetTopGaps = 6;
// `actor` is compared by address only and never dereferenced after its ReplicateActor call (it may be destroyed since):
// guid, health and location are copied at each send.
struct NetNpcSend {
    const void* actor;
    C31Weak weak; // the actor, checked at each window log: a destroyed actor's row is removed (card 140)
    uint64_t guid;
    float health; // at the last send
    Vec3 loc;     // at the last send
    float net_update_frequency; // AActor::NetUpdateFrequency (reflected) at the last send, -1 when not found
    bool showing_static;        // the animator showed the static mesh at the last send (movesync field 15 bit 3)
    ULONGLONG last_ms;
    ULONGLONG max_gap_ms; // this window
    uint32_t sends;       // this window
    double yaw = 0.0;            // root component yaw (degrees) at the last send
    ULONGLONG moved_ms = 0;      // card 140 run 7: first tick the host actor moved since the last send, 0 when not
    ULONGLONG moved_max_ms = 0;  // this window: the longest time moved and unsent
};
NetNpcSend g_net_npc[kNetNpcs] = {}; // game thread
uint32_t g_net_npc_count = 0;

// AActor::NetUpdateFrequency's offset, read once from the first NPC's class (reflected); -1 when absent.
int32_t g_net_update_frequency_at = -2; // -2 not looked up yet

// Card 140 run 7: a gap counts only while the host's NPC has something to send. ReplicateActor writes no bits for an
// NPC that stood still, so the plain send gap ranked idle and attacking-in-place bugs as skipped.
constexpr double kNetMovedUu = 10.0;
constexpr double kNetTurnedDeg = 5.0;

double actor_yaw(const void* actor) {
    const void* const root = read_ptr(actor, kActorRootComponent);
    double q[4] = {0.0, 0.0, 0.0, 1.0}; // ComponentToWorld quat X, Y, Z, W
    if (root != nullptr) {
        memcpy(q, static_cast<const char*>(root) + kComponentRotation, sizeof(q));
    }
    return atan2(2.0 * (q[3] * q[2] + q[0] * q[1]), 1.0 - 2.0 * (q[1] * q[1] + q[2] * q[2])) * (180.0 / 3.14159265358979);
}

float net_update_frequency(const void* actor) {
    if (g_net_update_frequency_at == -2) {
        const uint8_t* const prop = prop_find_prop(read_ptr(actor, kObjectClass), "NetUpdateFrequency");
        g_net_update_frequency_at = prop != nullptr ? read_i32(prop, kPropertyOffset) : -1;
        log::line("netbudget: host AActor::NetUpdateFrequency at +0x%X", g_net_update_frequency_at);
    }
    return g_net_update_frequency_at >= 0 ? read_f32(actor, static_cast<size_t>(g_net_update_frequency_at)) : -1.0f;
}

void net_note_npc(const void* actor, ULONGLONG now) {
    uint32_t at = g_net_npc_count;
    for (uint32_t i = 0; i < g_net_npc_count; ++i) {
        at = g_net_npc[i].actor == actor ? i : at;
    }
    if (at == g_net_npc_count) {
        if (g_net_npc_count == kNetNpcs) {
            return;
        }
        g_net_npc[g_net_npc_count++] = NetNpcSend{actor, {}, c41_guid(actor), 0.0f, {}, -1.0f, false, now, 0, 0};
        c31_weak_set(g_net_npc[g_net_npc_count - 1].weak, const_cast<void*>(actor));
    }
    NetNpcSend& n = g_net_npc[at];
    const uint64_t guid = c41_guid(actor);
    if (n.guid != guid) { // a new NPC at a destroyed one's address
        n = NetNpcSend{actor, {}, guid, 0.0f, {}, -1.0f, false, now, 0, 0};
        c31_weak_set(n.weak, const_cast<void*>(actor));
    }
    n.health = c23_set_health(actor);
    n.loc = actor_location(actor);
    n.net_update_frequency = net_update_frequency(actor);
    n.showing_static = c132_showing_static(actor);
    const ULONGLONG gap = now - n.last_ms;
    n.max_gap_ms = gap > n.max_gap_ms ? gap : n.max_gap_ms;
    const ULONGLONG moved = n.moved_ms != 0 ? now - n.moved_ms : 0;
    n.moved_max_ms = moved > n.moved_max_ms ? moved : n.moved_max_ms;
    n.moved_ms = 0;
    n.yaw = actor_yaw(actor);
    n.last_ms = now;
    ++n.sends;
}

// Card 140 run 7, each pace step on the host: a tracked live NPC that moved more than kNetMovedUu or turned more than
// kNetTurnedDeg since its last send starts its moved-and-unsent clock (stopped by its next send, net_note_npc).
void net_npc_moved_step(ULONGLONG now) {
    for (uint32_t i = 0; i < g_net_npc_count; ++i) {
        NetNpcSend& n = g_net_npc[i];
        const void* const actor = n.moved_ms == 0 ? c31_weak_get(n.weak) : nullptr;
        if (actor == nullptr) {
            continue;
        }
        const Vec3 at = actor_location(actor);
        const double dx = at.x - n.loc.x;
        const double dy = at.y - n.loc.y;
        const double dz = at.z - n.loc.z;
        double turn = fabs(actor_yaw(actor) - n.yaw);
        turn = turn > 180.0 ? 360.0 - turn : turn;
        if (dx * dx + dy * dy + dz * dz > kNetMovedUu * kNetMovedUu || turn > kNetTurnedDeg) {
            n.moved_ms = now;
        }
    }
}

// The kNetTopGaps live NPCs with the longest moved-and-unsent time this window (card 140 run 7: the time the host's
// NPC had moved or turned past the thresholds with no send; one still unsent counts up to now), each with its plain
// send gap and sends, then the window's counters reset. An idle NPC that changed nothing no longer ranks.
// Card 140: ranked only over NPCs with a send this window or, at their last send, within kNetNearUu of a player pawn;
// the others (far idle bugs with nothing to send) are one count.
constexpr double kNetNearUu = 5000.0;
Vec3 g_net_pawns[kC17Slots] = {};
int32_t g_net_pawn_count = 0; // this window's last step

bool net_npc_eligible(const NetNpcSend& n) {
    if (n.sends != 0) {
        return true;
    }
    for (int32_t i = 0; i < g_net_pawn_count; ++i) {
        const double dx = n.loc.x - g_net_pawns[i].x;
        const double dy = n.loc.y - g_net_pawns[i].y;
        const double dz = n.loc.z - g_net_pawns[i].z;
        if (dx * dx + dy * dy + dz * dz < kNetNearUu * kNetNearUu) {
            return true;
        }
    }
    return false;
}

// Card 140 run 6: a row whose actor is gone (the weak pointer the pose table also uses, "actor gone") or dead (IsDead
// set) stops being an NPC with sends to make; it is removed so its last sent health never ranks it as alive.
uint32_t net_drop_dead_npcs() {
    uint32_t kept = 0;
    for (uint32_t i = 0; i < g_net_npc_count; ++i) {
        const void* const actor = c31_weak_get(g_net_npc[i].weak);
        float health = -1.0f;
        if (actor != nullptr && c25_dead(actor, &health) != 1) {
            g_net_npc[kept++] = g_net_npc[i];
        }
    }
    const uint32_t dropped = g_net_npc_count - kept;
    g_net_npc_count = kept;
    return dropped;
}

void net_log_npc_gaps(ULONGLONG now) {
    const uint32_t dropped = net_drop_dead_npcs();
    uint32_t over_500 = 0;
    uint32_t moved_over_300 = 0;
    uint32_t far_idle = 0;
    bool eligible[kNetNpcs] = {};
    for (uint32_t i = 0; i < g_net_npc_count; ++i) {
        eligible[i] = net_npc_eligible(g_net_npc[i]);
        far_idle += !eligible[i] && g_net_npc[i].health > 0.0f ? 1 : 0;
    }
    for (uint32_t i = 0; i < g_net_npc_count; ++i) {
        NetNpcSend& n = g_net_npc[i];
        const ULONGLONG open = now - n.last_ms;
        n.max_gap_ms = open > n.max_gap_ms ? open : n.max_gap_ms;
        over_500 += eligible[i] && n.max_gap_ms > 500 && n.health > 0.0f ? 1 : 0;
        const ULONGLONG moved = n.moved_ms != 0 ? now - n.moved_ms : 0;
        n.moved_max_ms = moved > n.moved_max_ms ? moved : n.moved_max_ms;
        moved_over_300 += eligible[i] && n.moved_max_ms > 300 && n.health > 0.0f ? 1 : 0;
    }
    log::line("netbudget: host NPC sends: %u NPCs tracked (%u gone or dead removed), %u near or sending alive with a "
              "gap over 500 ms this window, %u of them moved and unsent over 300 ms (moved over %.0f uu or turned over "
              "%.0f deg), %u alive far (over %.0f uu from every pawn) with 0 sends, not ranked",
              g_net_npc_count, dropped, over_500, moved_over_300, kNetMovedUu, kNetTurnedDeg, far_idle, kNetNearUu);
    for (uint32_t k = 0; k < kNetTopGaps; ++k) {
        uint32_t best = kNetNpcs;
        for (uint32_t i = 0; i < g_net_npc_count; ++i) {
            // A dead NPC (health <= 0, or -1 with no set) stops changing and stops writing bits; not a skipped send.
            if (eligible[i] && g_net_npc[i].sends != 0xFFFFFFFFu && g_net_npc[i].health > 0.0f &&
                (best == kNetNpcs || g_net_npc[i].moved_max_ms > g_net_npc[best].moved_max_ms)) {
                best = i;
            }
        }
        if (best == kNetNpcs || g_net_npc[best].moved_max_ms <= 300) {
            break;
        }
        NetNpcSend& n = g_net_npc[best];
        const Vec3 at = n.loc;
        log::line("netbudget: host   NPC guid 0x%llx moved and unsent %llu ms, max send gap %llu ms, %u sends, last "
                  "%llu ms ago, at (%.0f, %.0f, %.0f) at the last send, NetUpdateFrequency %.1f, static %u",
                  static_cast<unsigned long long>(n.guid), static_cast<unsigned long long>(n.moved_max_ms),
                  static_cast<unsigned long long>(n.max_gap_ms), n.sends,
                  static_cast<unsigned long long>(now - n.last_ms), at.x, at.y, at.z, n.net_update_frequency,
                  n.showing_static ? 1u : 0u);
        n.sends = 0xFFFFFFFFu; // printed
    }
    for (uint32_t i = 0; i < g_net_npc_count; ++i) {
        g_net_npc[i].max_gap_ms = 0;
        g_net_npc[i].moved_max_ms = 0;
        g_net_npc[i].sends = 0;
    }
}

void net_add(const void* cls, const char* kind, int64_t bits) {
    for (uint32_t i = 0; i < g_net.class_count; ++i) {
        if (g_net.classes[i].cls == cls) {
            g_net.classes[i].bits += static_cast<uint64_t>(bits);
            ++g_net.classes[i].calls;
            return;
        }
    }
    if (g_net.class_count == kNetClasses) {
        g_net.other_bits += static_cast<uint64_t>(bits);
        return;
    }
    g_net.classes[g_net.class_count++] = NetClassBits{cls, kind, static_cast<uint64_t>(bits), 1};
}

// Card 329 step E (diagnostic): the host's NPC property updates per class by distance band from the nearest player
// pawn (g_net_pawns: the host's and every server copy's, filled each tick by c140_pace_step), printed per map. The
// band is cached per actor and re-read at most every kNpcBandMs; per update the cost is one hash probe and two adds.
constexpr uint32_t kNpcBandRanges = 3;
constexpr double kNpcBandUu[kNpcBandRanges] = {3000.0, 8000.0, 20000.0};
constexpr uint8_t kNpcBandNoPawn = 4;
constexpr uint8_t kNpcBandNoSlot = 5;
constexpr uint32_t kNpcBands = 6; // the 4 distance bands, no player pawn, no slot
constexpr ULONGLONG kNpcBandMs = 250;
constexpr ULONGLONG kNpcBandStaleMs = 5000; // a slot unused this long is free for another actor
constexpr uint32_t kNpcBandSlots = 1024;    // power of 2
constexpr uint32_t kNpcBandProbe = 8;
constexpr uint32_t kNpcPropClasses = 48;
constexpr uint32_t kNpcPropTop = 10;
constexpr uint32_t kNpcNoClass = 0xFFFFFFFFu;

// `actor` and `cls` are compared by address only, never dereferenced from the slot.
struct NpcBandSlot {
    const void* actor;
    const void* cls;
    ULONGLONG next_ms; // the next band read
    uint32_t cls_row;  // index into NpcProps::classes, kNpcNoClass when the class table was full
    uint8_t band;
};

struct NpcPropClass {
    const void* cls;
    ObjectName name; // copied at insert: the class can be freed by the travel before the map print
    uint64_t bits[kNpcBands];
    uint32_t updates[kNpcBands];
};

struct NpcProps {
    NpcBandSlot slots[kNpcBandSlots];
    NpcPropClass classes[kNpcPropClasses];
    uint32_t class_count;
    uint64_t other_bits; // classes past the table
    uint32_t other_updates;
    uint32_t no_slot; // updates with no band slot
    ULONGLONG start_ms; // the first counted update this map
    bool warned_slots;
    bool warned_classes;
};

NpcProps g_npc_props{}; // game thread

uint8_t npc_band_read(const void* actor) {
    const Vec3 at = actor_location(actor);
    double best = -1.0;
    for (int32_t i = 0; i < g_net_pawn_count; ++i) {
        const double dx = at.x - g_net_pawns[i].x;
        const double dy = at.y - g_net_pawns[i].y;
        const double dz = at.z - g_net_pawns[i].z;
        const double d2 = dx * dx + dy * dy + dz * dz;
        best = best < 0.0 || d2 < best ? d2 : best;
    }
    if (best < 0.0) {
        return kNpcBandNoPawn;
    }
    uint8_t band = 0;
    while (band < kNpcBandRanges && best >= kNpcBandUu[band] * kNpcBandUu[band]) {
        ++band;
    }
    return band;
}

uint32_t npc_class_row(const void* cls) {
    for (uint32_t i = 0; i < g_npc_props.class_count; ++i) {
        if (g_npc_props.classes[i].cls == cls) {
            return i;
        }
    }
    if (g_npc_props.class_count == kNpcPropClasses) {
        if (!g_npc_props.warned_classes) {
            g_npc_props.warned_classes = true;
            log::line("WARN card329: host npc props class table full (%u classes): further classes count as other",
                      kNpcPropClasses);
        }
        return kNpcNoClass;
    }
    NpcPropClass& c = g_npc_props.classes[g_npc_props.class_count];
    c = NpcPropClass{};
    c.cls = cls;
    c.name = object_name(cls);
    return g_npc_props.class_count++;
}

// The actor's slot (found or made), or nullptr when its probe run is full of live slots.
NpcBandSlot* npc_band_slot(const void* actor, const void* cls, ULONGLONG now) {
    const uint64_t key = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(actor));
    const uint32_t h = static_cast<uint32_t>(((key >> 4) * 0x9E3779B97F4A7C15ull) >> 54); // 10 bits
    NpcBandSlot* free_slot = nullptr;
    for (uint32_t k = 0; k < kNpcBandProbe; ++k) {
        NpcBandSlot& s = g_npc_props.slots[(h + k) & (kNpcBandSlots - 1)];
        if (s.actor == actor) {
            if (s.cls != cls) { // another actor at a freed one's address
                s.cls = cls;
                s.cls_row = npc_class_row(cls);
                s.next_ms = 0;
            }
            return &s;
        }
        const bool open = s.actor == nullptr || now >= s.next_ms + kNpcBandStaleMs;
        free_slot = free_slot == nullptr && open ? &s : free_slot;
    }
    if (free_slot != nullptr) {
        *free_slot = NpcBandSlot{actor, cls, 0, npc_class_row(cls), kNpcBandNoPawn};
    }
    return free_slot;
}

void npc_props_add(const void* actor, const void* cls, int64_t bits, ULONGLONG now) {
    g_npc_props.start_ms = g_npc_props.start_ms == 0 ? now : g_npc_props.start_ms;
    NpcBandSlot* const s = npc_band_slot(actor, cls, now);
    uint32_t row = s != nullptr ? s->cls_row : npc_class_row(cls);
    uint8_t band = kNpcBandNoSlot;
    if (s != nullptr) {
        if (now >= s->next_ms) {
            s->band = npc_band_read(actor);
            s->next_ms = now + kNpcBandMs;
        }
        band = s->band;
    } else {
        ++g_npc_props.no_slot;
        if (!g_npc_props.warned_slots) {
            g_npc_props.warned_slots = true;
            log::line("WARN card329: host npc props band table full (%u slots): further NPCs count as no slot",
                      kNpcBandSlots);
        }
    }
    if (row == kNpcNoClass) {
        g_npc_props.other_bits += static_cast<uint64_t>(bits);
        ++g_npc_props.other_updates;
        return;
    }
    g_npc_props.classes[row].bits[band] += static_cast<uint64_t>(bits);
    ++g_npc_props.classes[row].updates[band];
}

// Card 329 step E: the map's top kNpcPropTop NPC classes by bytes, one row each, then reset.
void npc_props_print(ULONGLONG now) {
    NpcProps& p = g_npc_props;
    if (p.start_ms == 0) {
        return;
    }
    const ULONGLONG span_ms = now > p.start_ms ? now - p.start_ms : 1;
    uint64_t total_bits = 0;
    uint32_t total_updates = 0;
    uint64_t class_bits[kNpcPropClasses] = {};
    for (uint32_t i = 0; i < p.class_count; ++i) {
        for (uint32_t b = 0; b < kNpcBands; ++b) {
            class_bits[i] += p.classes[i].bits[b];
            total_updates += p.classes[i].updates[b];
        }
        total_bits += class_bits[i];
    }
    log::line("card329: host t=%llu npc props map: %llu s, %u classes, %llu B in %u updates, other %llu B in %u "
              "updates, no slot %u", static_cast<unsigned long long>(now),
              static_cast<unsigned long long>(span_ms / 1000), p.class_count,
              static_cast<unsigned long long>(total_bits / 8), total_updates,
              static_cast<unsigned long long>(p.other_bits / 8), p.other_updates, p.no_slot);
    bool done[kNpcPropClasses] = {};
    const double secs = static_cast<double>(span_ms) / 1000.0;
    for (uint32_t k = 0; k < kNpcPropTop; ++k) {
        uint32_t best = kNpcNoClass;
        for (uint32_t i = 0; i < p.class_count; ++i) {
            best = !done[i] && (best == kNpcNoClass || class_bits[i] > class_bits[best]) ? i : best;
        }
        if (best == kNpcNoClass) {
            break;
        }
        done[best] = true;
        const NpcPropClass& c = p.classes[best];
        uint32_t updates = 0;
        for (uint32_t b = 0; b < kNpcBands; ++b) {
            updates += c.updates[b];
        }
        log::line("card329: host t=%llu npc props %s: %llu B in %u updates (%llu B each); under 3000 %llu B %u "
                  "(%.1f/s); 3000-8000 %llu B %u (%.1f/s); 8000-20000 %llu B %u (%.1f/s); over 20000 %llu B %u "
                  "(%.1f/s); no pawn %llu B %u; no slot %llu B %u", static_cast<unsigned long long>(now), c.name.text,
                  static_cast<unsigned long long>(class_bits[best] / 8), updates,
                  static_cast<unsigned long long>(updates != 0 ? class_bits[best] / 8 / updates : 0),
                  static_cast<unsigned long long>(c.bits[0] / 8), c.updates[0], c.updates[0] / secs,
                  static_cast<unsigned long long>(c.bits[1] / 8), c.updates[1], c.updates[1] / secs,
                  static_cast<unsigned long long>(c.bits[2] / 8), c.updates[2], c.updates[2] / secs,
                  static_cast<unsigned long long>(c.bits[3] / 8), c.updates[3], c.updates[3] / secs,
                  static_cast<unsigned long long>(c.bits[kNpcBandNoPawn] / 8), c.updates[kNpcBandNoPawn],
                  static_cast<unsigned long long>(c.bits[kNpcBandNoSlot] / 8), c.updates[kNpcBandNoSlot]);
    }
    memset(&p, 0, sizeof(p));
}

// Appends " <op> <bytes>" for the kNetTopOps largest deltas in d (Fx excluded: the caller puts it first).
int net_append_ops(char* out, size_t cap, const uint64_t* d) {
    int at = 0;
    bool done[kOpSlots] = {};
    done[kFxOp] = true;
    for (uint32_t k = 0; k < kNetTopOps; ++k) {
        size_t best = kOpSlots;
        for (size_t i = 0; i < kOpSlots; ++i) {
            if (!done[i] && d[i] != 0 && (best == kOpSlots || d[i] > d[best])) {
                best = i;
            }
        }
        if (best == kOpSlots || static_cast<size_t>(at) >= cap) {
            break;
        }
        done[best] = true;
        const int n = snprintf(out + at, cap - static_cast<size_t>(at), ", %s %llu", c132_op_name(static_cast<uint8_t>(best)),
                               static_cast<unsigned long long>(d[best]));
        at = n > 0 ? at + n : at;
    }
    return at;
}

// Card 140 P4: host bytes per row by op this window (the kNetTopOps + 1 largest by bytes), to compare per event type.
void net_log_bytes_per_row(ULONGLONG now, const uint64_t* d, const uint32_t* rows) {
    char out[640];
    int at = 0;
    bool done[kOpSlots] = {};
    for (uint32_t k = 0; k <= kNetTopOps; ++k) {
        size_t best = kOpSlots;
        for (size_t i = 0; i < kOpSlots; ++i) {
            best = !done[i] && d[i] != 0 && (best == kOpSlots || d[i] > d[best]) ? i : best;
        }
        if (best == kOpSlots || static_cast<size_t>(at) >= sizeof(out)) {
            break;
        }
        done[best] = true;
        const int n = snprintf(out + at, sizeof(out) - static_cast<size_t>(at), ", %s %llu B / %u rows = %llu",
                               c132_op_name(static_cast<uint8_t>(best)), static_cast<unsigned long long>(d[best]),
                               rows[best], static_cast<unsigned long long>(rows[best] != 0 ? d[best] / rows[best] : 0));
        at = n > 0 ? at + n : at;
    }
    out[at < static_cast<int>(sizeof(out)) ? at : static_cast<int>(sizeof(out)) - 1] = '\0';
    log::line("netbudget: host t=%llu chan bytes per row this window%s", static_cast<unsigned long long>(now),
              at > 0 ? out : ": none");
}

// One line of this side's chan bytes per op since the last line: Fx first (bytes and B/s), then the largest others.
// Card 150: this window's packed row numbers (host: packing, client: unpacking), then reset.
void net_log_pack(const char* side) {
    const ue::chan::PackStats k = ue::chan::pack_window();
    if (k.frames != 0 || k.bad_frames != 0) {
        log::line("card150: %s pack: %u frames, %u raw, text %llu bytes, packed %llu bytes, %u defs, %u dictionary full; "
                  "bad frames %u, undefined refs %u", side, k.frames, k.raw,
                  static_cast<unsigned long long>(k.text_bytes), static_cast<unsigned long long>(k.packed_bytes), k.defs,
                  k.dict_full, k.bad_frames, k.undefined);
    }
    if (k.bad_frames != 0 || k.undefined != 0) {
        log::line("WARN card150: %u packed frames did not unpack (%u undefined refs): their rows were not applied",
                  k.bad_frames, k.undefined);
    }
}

void net_log_chan_bytes(const char* side, ULONGLONG now) {
    uint64_t ds[kOpSlots];
    uint64_t dr[kOpSlots];
    for (size_t i = 0; i < kOpSlots; ++i) {
        const uint64_t s = ue::chan::chan_bytes_sent(i);
        const uint64_t r = ue::chan::chan_bytes_received(i);
        ds[i] = s - g_net_chan.sent[i];
        dr[i] = r - g_net_chan.received[i];
        g_net_chan.sent[i] = s;
        g_net_chan.received[i] = r;
    }
    uint32_t rows[kOpSlots];
    for (size_t i = 0; i < kOpSlots; ++i) {
        const uint32_t r = ue::chan::chan_client_sends(i);
        rows[i] = r - g_net_chan.rows[i];
        g_net_chan.rows[i] = r;
    }
    if (!g_client_session) {
        net_log_bytes_per_row(now, ds, rows);
    }
    char sent[384];
    char recv[384];
    sent[0] = '\0';
    recv[0] = '\0';
    net_append_ops(sent, sizeof(sent), ds);
    net_append_ops(recv, sizeof(recv), dr);
    log::line("netbudget: %s t=%llu chan bytes by op this window: sent Fx %llu (%llu B/s)%s; received Fx %llu "
              "(%llu B/s)%s", side, static_cast<unsigned long long>(now), static_cast<unsigned long long>(ds[kFxOp]),
              static_cast<unsigned long long>(ds[kFxOp] * 1000 / kNetLogMs), sent,
              static_cast<unsigned long long>(dr[kFxOp]), static_cast<unsigned long long>(dr[kFxOp] * 1000 / kNetLogMs),
              recv);
}

void net_chan_snapshot() {
    for (size_t i = 0; i < kOpSlots; ++i) {
        g_net_chan.sent[i] = ue::chan::chan_bytes_sent(i);
        g_net_chan.received[i] = ue::chan::chan_bytes_received(i);
        g_net_chan.rows[i] = ue::chan::chan_client_sends(i);
    }
}

int32_t net_saturated_count() {
    return g_exe_base != nullptr ? *reinterpret_cast<const int32_t*>(g_exe_base + kNumSaturatedRva) : 0;
}

// Card 140: this window's real gate and pacing numbers, then both reset.
void net_log_gate(ULONGLONG now) {
    const int32_t sat = net_saturated_count();
    log::line("netbudget: host t=%llu gate: IsNetReady false %u of %u frames, GNumSaturatedConnections +%d, "
              "QueuedBits %lld to %lld, SendBuffer bits max %lld, CurrentNetSpeed %d to %d, +0xA4 bit 2 on %u, "
              "congestion control on %u, IsNetReady vtable %s", static_cast<unsigned long long>(now),
              g_gate.real_not_ready, g_gate.frames, g_gate.sat_start >= 0 ? sat - g_gate.sat_start : 0,
              static_cast<long long>(g_gate.queued_min), static_cast<long long>(g_gate.queued_max),
              static_cast<long long>(g_gate.sendbuf_max), g_gate.speed_min, g_gate.speed_max, g_gate.flag_a4,
              g_gate.congestion, g_gate.vt_ok == 1 ? "exe+0x3E71B20" : g_gate.vt_ok == 0 ? "OTHER (not called)" : "unchecked");
    const ue::chan::PaceStats p = ue::chan::chan_pace_window();
    static ULONGLONG s_pace_last = 0; // the previous pace line, for the rates
    const ULONGLONG span_ms = s_pace_last != 0 && now > s_pace_last ? now - s_pace_last : 0;
    s_pace_last = now;
    const long long rate_div = span_ms != 0 ? static_cast<long long>(span_ms) : 1;
    log::line("netbudget: host t=%llu pace rate: offered %lld bytes (%lld B/s), sent %lld bytes (%lld B/s), drained "
              "from the queue %lld bytes (%lld B/s) in %llu ms", static_cast<unsigned long long>(now),
              static_cast<long long>(p.offered_bytes), static_cast<long long>(p.offered_bytes) * 1000 / rate_div,
              static_cast<long long>(p.sent_bytes), static_cast<long long>(p.sent_bytes) * 1000 / rate_div,
              static_cast<long long>(p.drained_bytes), static_cast<long long>(p.drained_bytes) * 1000 / rate_div,
              static_cast<unsigned long long>(span_ms));
    log::line("netbudget: host t=%llu pace: drained %lld bytes, queue peak %u frames %lld bytes, longest wait %llu ms, "
              "%u paced, holds: bits %u, reliable %u, silent client %u; alone %u; peak reliable out %d, peak QueuedBits+SendBuffer %lld; full "
              "sends %u, no slot %u, no reading %u, dropped %u, most chan bytes in one frame %lld, priority %u, batches %u carrying %u rows",
              static_cast<unsigned long long>(now), static_cast<long long>(p.drained_bytes), p.peak_frames,
              static_cast<long long>(p.peak_bytes), static_cast<unsigned long long>(p.max_wait_ms), p.paced,
              p.holds_bits, p.holds_rel, p.holds_silent, p.alone_sends, p.rel_peak, static_cast<long long>(p.pending_peak), p.full_sends, p.no_slot,
              p.no_reading, p.dropped, static_cast<long long>(p.frame_bytes_max), p.priority, p.batches,
              p.batched_rows);
    if (p.full_sends != 0) {
        log::line("WARN card140: pace queue full: %u frames sent past the gate this window (never flushed, none dropped)",
                  p.full_sends);
    }
    const uint32_t map_lines = g_gate.stall_lines_map;
    const int8_t vt = g_gate.vt_ok;
    g_gate = net_gate_fresh(sat, map_lines, vt);
}

void net_log_window(const void* conn, const void* driver, ULONGLONG now) {
    uint64_t total = g_net.other_bits;
    for (uint32_t i = 0; i < g_net.class_count; ++i) {
        total += g_net.classes[i].bits;
    }
    const int32_t speed = conn != nullptr ? read_i32(conn, kPlayerCurrentNetSpeed) : -1;
    const int32_t max_rate = driver != nullptr ? read_i32(driver, kDriverMaxClientRate) : -1;
    log::line("netbudget: host t=%llu window %llu ms: CurrentNetSpeed %d B/s, MaxClientRate %d B/s, not net ready "
              "%u of %u frames, replicated %llu bytes (%llu B/s) in %u classes",
              static_cast<unsigned long long>(now), static_cast<unsigned long long>(kNetLogMs), speed, max_rate,
              g_net.not_ready_frames, g_net.frames, static_cast<unsigned long long>(total / 8),
              static_cast<unsigned long long>(total / 8 * 1000 / kNetLogMs), g_net.class_count);
    for (uint32_t k = 0; k < kNetTopClasses; ++k) {
        uint32_t best = kNetClasses;
        for (uint32_t i = 0; i < g_net.class_count; ++i) {
            if (g_net.classes[i].calls != 0 && (best == kNetClasses || g_net.classes[i].bits > g_net.classes[best].bits)) {
                best = i;
            }
        }
        if (best == kNetClasses) {
            break;
        }
        NetClassBits& c = g_net.classes[best];
        log::line("netbudget: host   %-5s %-48s %8llu bytes, %5u calls", c.kind, object_name(c.cls).text,
                  static_cast<unsigned long long>(c.bits / 8), c.calls);
        c.calls = 0; // printed
    }
    log::line("netbudget: host   reliable bunches outstanding: window peak %d (%s), map peak %d (%s), limit 511",
              g_net.rel_peak, g_net.rel_peak_class.text[0] != '\0' ? g_net.rel_peak_class.text : "none", g_net.rel_map_peak,
              g_net.rel_map_class.text[0] != '\0' ? g_net.rel_map_class.text : "none");
    net_log_gate(now);
    net_log_chan_bytes("host", now);
    net_log_pack("host");
    net_log_npc_gaps(now);
    g_net.rel_peak = 0;
    g_net.rel_peak_class = ObjectName{"none"};
    g_net.class_count = 0;
    g_net.other_bits = 0;
    g_net.frames = 0;
    g_net.not_ready_frames = 0;
}

} // namespace

int64_t detour_net_replicate_actor(void* channel) {
    const int64_t bits = g_orig_net_replicate_actor(channel);
    const void* const actor = read_ptr(channel, kActorChannelActor);
    if (!g_client_session && bits > 0 && actor != nullptr) {
        net_add(read_ptr(actor, kObjectClass), "props", bits);
        if (g_npc_class != nullptr && is_a(actor, g_npc_class())) {
            const ULONGLONG now = GetTickCount64();
            net_note_npc(actor, now);
            npc_props_add(actor, read_ptr(actor, kObjectClass), bits, now); // card 329 step E
        }
    }
    return bits;
}

namespace {
int64_t net_pending_bits(const void* conn) {
    return static_cast<int64_t>(read_i32(conn, kConnQueuedBits)) +
           static_cast<int64_t>(read_u64(conn, kConnSendBufferBits));
}

void net_note_pc_rel(const void* target, const void* channel, int32_t out) {
    for (NetPcRel& r : g_pc_rel) {
        if (r.pc == target) {
            r.out = out;
            r.channel = channel;
            return;
        }
    }
}

// Host: the channel's outstanding reliable bunches after an RPC went out on it, into the window and map peaks.
void net_note_reliable(const void* channel) {
    const int32_t n = read_i32(channel, kConnChannelNumOutRec);
    if (n <= g_net.rel_peak && n <= g_net.rel_map_peak) {
        return;
    }
    const void* const actor = read_ptr(channel, kActorChannelActor);
    const void* const cls = actor != nullptr ? read_ptr(actor, kObjectClass) : nullptr;
    const ObjectName name = cls != nullptr ? object_name(cls) : ObjectName{"none"};
    if (n > g_net.rel_peak) {
        g_net.rel_peak = n;
        g_net.rel_peak_class = name;
    }
    if (n > g_net.rel_map_peak) {
        g_net.rel_map_peak = n;
        g_net.rel_map_class = name;
    }
}
} // namespace

void detour_net_remote_function(void* driver, void* channel, const void* class_cache, const void* field_cache,
                                void* target, void* conn, void* function, void* parms, void* out_parms, void* stack,
                                bool is_server, int32_t send_policy, void* flags) {
    const bool measure = !g_client_session && conn != nullptr && function != nullptr;
    const int64_t before = measure ? net_pending_bits(conn) : 0;
    g_orig_net_remote_function(driver, channel, class_cache, field_cache, target, conn, function, parms, out_parms,
                               stack, is_server, send_policy, flags);
    if (measure) {
        const int64_t bits = net_pending_bits(conn) - before;
        net_add(function, "rpc", bits > 0 ? bits : 0);
    }
    if (!g_client_session && channel != nullptr) {
        net_note_reliable(channel);
        net_note_pc_rel(target, channel, read_i32(channel, kConnChannelNumOutRec));
    }
}

namespace {
// Client: the chan bytes line every kNetLogMs while in a game world, up to kNetLogWindows per map.
void net_budget_client_step(void* engine) {
    if (engine == nullptr || g_get_game_world(engine) == nullptr || g_net_chan.windows >= kNetLogWindows) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (g_net_chan.next_log_ms == 0) {
        g_net_chan.next_log_ms = now + kNetLogMs;
    }
    if (now >= g_net_chan.next_log_ms) {
        g_net_chan.next_log_ms = now + kNetLogMs;
        ++g_net_chan.windows;
        net_log_chan_bytes("client", now);
        net_log_pack("client");
    }
}
} // namespace

void net_budget_step(void* engine) {
    if (g_client_session) {
        net_budget_client_step(engine);
        return;
    }
    void* const world = engine != nullptr ? g_get_game_world(engine) : nullptr;
    if (world == nullptr || g_net.windows >= kNetLogWindows) {
        return;
    }
    const void* conn = nullptr;
    for (int i = 1; i < kC17Slots && conn == nullptr; ++i) {
        const void* const pc = g_get_player_controller(world, i);
        conn = pc != nullptr && c26_is_remote_pc(pc) ? read_ptr(pc, kPcNetConnection) : nullptr;
    }
    if (conn == nullptr) {
        return;
    }
    ++g_net.frames;
    const int64_t pending = static_cast<int64_t>(read_i32(conn, kConnQueuedBits)) + static_cast<int64_t>(read_u64(conn, kConnSendBufferBits));
    g_net.not_ready_frames += pending > 0 ? 1 : 0;
    const ULONGLONG now = GetTickCount64();
    if (g_net.next_log_ms == 0) {
        g_net.next_log_ms = now + kNetLogMs;
    }
    if (now >= g_net.next_log_ms) {
        g_net.next_log_ms = now + kNetLogMs;
        ++g_net.windows;
        net_log_window(conn, read_ptr(world, kWorldNetDriver), now);
    }
}

void net_budget_arm_map() {
    if (g_net.rel_map_peak > 0) {
        log::line("netbudget: host map totals: reliable bunches outstanding peak %d (%s), limit 511", g_net.rel_map_peak,
                  g_net.rel_map_class.text);
    }
    npc_props_print(GetTickCount64()); // card 329 step E
    g_net = NetBudget{};
    g_net_chan = NetChanBytes{};
    g_gate = net_gate_fresh(-1, 0, -1);
    net_chan_snapshot();
    g_net_npc_count = 0;
}

} // namespace ubw::hooks

namespace ubw::hooks {

namespace {
// Card 140: one sample of the client connection's real gate; per-frame stall lines while it is not ready.
void net_gate_sample(void* conn, const void* pc, ULONGLONG now) {
    if (g_gate.vt_ok == -1) {
        const void* const fn = read_ptr(read_ptr(conn, 0), kVtIsNetReady);
        g_gate.vt_ok = g_exe_base != nullptr && fn == g_exe_base + kIsNetReadyRva ? 1 : 0;
        log::line("netbudget: host IsNetReady vtable +0x318 of the client connection: exe+0x%llX (%s)",
                  static_cast<unsigned long long>(static_cast<const uint8_t*>(fn) - g_exe_base),
                  g_gate.vt_ok == 1 ? "expected" : "NOT exe+0x3E71B20, not called");
    }
    const auto ready_fn = reinterpret_cast<IsNetReadyFn>(read_ptr(read_ptr(conn, 0), kVtIsNetReady));
    const bool ready = g_gate.vt_ok == 1 ? (ready_fn(conn, 0) & 0xFF) != 0 : true;
    const int64_t queued = read_i32(conn, kConnQueuedBits);
    const int64_t sendbuf = static_cast<int64_t>(read_u64(conn, kConnSendBufferBits));
    const int32_t speed = read_i32(conn, kPlayerCurrentNetSpeed);
    const bool a4 = ((static_cast<uint32_t>(read_i32(conn, kConnFlagsA4)) >> 2) & 1u) != 0;
    const bool cc = (static_cast<const uint8_t*>(conn)[kConnCongestionSet]) != 0;
    if (g_gate.sat_start < 0) {
        g_gate.sat_start = net_saturated_count();
    }
    const bool first = g_gate.frames++ == 0;
    g_gate.real_not_ready += ready ? 0 : 1;
    g_gate.flag_a4 += a4 ? 1 : 0;
    g_gate.congestion += cc ? 1 : 0;
    g_gate.queued_min = first || queued < g_gate.queued_min ? queued : g_gate.queued_min;
    g_gate.queued_max = first || queued > g_gate.queued_max ? queued : g_gate.queued_max;
    g_gate.sendbuf_max = sendbuf > g_gate.sendbuf_max ? sendbuf : g_gate.sendbuf_max;
    g_gate.speed_min = first || speed < g_gate.speed_min ? speed : g_gate.speed_min;
    g_gate.speed_max = first || speed > g_gate.speed_max ? speed : g_gate.speed_max;
    if (!ready && g_gate.stall_lines < kStallLinesWindow && g_gate.stall_lines_map < kStallLinesMap) {
        ++g_gate.stall_lines;
        ++g_gate.stall_lines_map;
        int32_t rel = -1;
        for (const NetPcRel& r : g_pc_rel) {
            rel = r.pc == pc ? r.out : rel;
        }
        log::line("netbudget: stall frame t=%llu: CurrentNetSpeed %d, QueuedBits %lld, SendBuffer bits %lld, "
                  "reliable bunches out %d, +0xA4 bit 2 %u, congestion control %u, GNumSaturatedConnections %d, "
                  "pace queue %u frames, packets lost %d", static_cast<unsigned long long>(now), speed,
                  static_cast<long long>(queued), static_cast<long long>(sendbuf), rel, a4 ? 1u : 0u, cc ? 1u : 0u,
                  net_saturated_count(), ue::chan::chan_pace_depth(pc), read_i32(conn, kConnOutTotalLost));
    }
}
} // namespace

// Card 140 run 6 (user decision 2026-11-08): the host's link to each client at 250000 B/s. UNetConnection::Tick refills
// QueuedBits from UPlayer::CurrentNetSpeed (conn+0x38) every frame, and APlayerController::SetNetSpeed clamps that
// value to UNetDriver::MaxClientRate (driver+0xC8), so both are set here, each tick, before the gate sample and the
// pace drain read them; a value the engine set since the last tick is logged with its connection.
// Card 226: the engine's refill is CurrentNetSpeed * 8 / 120 bits per frame whatever the frame time (floor two refills;
// logs: QueuedBits -33332 at 5 fps and at 60 fps), so a fixed 250000 gives 2083 B per frame: 125 kB/s at 60 fps,
// 62.5 kB/s at 30 fps. The value written is therefore 250000 * frame time * 120, frame time held in 1/120 to 1/20 s,
// so one frame's refill is 250000 B/s of real time (250000 to 1500000 written).
namespace {
constexpr int32_t kNetRateCap = 250000;
constexpr double kNetRefillHz = 120.0;   // the engine's fixed bandwidth step, 1/120 s
constexpr double kNetRateMaxScale = 6.0; // 1/20 s frame
constexpr uint32_t kNetRateLines = 16;
uint32_t g_net_rate_lines = 0;
int32_t g_net_rate_now = kNetRateCap; // this tick's value, from net_rate_frame
int32_t g_net_rate_last = 0;          // last tick's value, what the engine should still hold
LONGLONG g_net_rate_qpc = 0;

// Once per tick: this tick's rate from the time since the last tick.
void net_rate_frame() {
    LARGE_INTEGER now{};
    LARGE_INTEGER freq{};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    const double dt = g_net_rate_qpc != 0 && freq.QuadPart > 0
        ? static_cast<double>(now.QuadPart - g_net_rate_qpc) / static_cast<double>(freq.QuadPart) : 0.0;
    g_net_rate_qpc = now.QuadPart;
    double scale = dt * kNetRefillHz;
    scale = scale < 1.0 ? 1.0 : scale;
    scale = scale > kNetRateMaxScale ? kNetRateMaxScale : scale;
    g_net_rate_last = g_net_rate_now;
    g_net_rate_now = static_cast<int32_t>(static_cast<double>(kNetRateCap) * scale);
}

void net_rate_cap(void* world, void* conn) {
    auto* const driver = static_cast<uint8_t*>(world != nullptr ? read_ptr(world, kWorldNetDriver) : nullptr);
    const int32_t speed = read_i32(conn, kPlayerCurrentNetSpeed);
    const int32_t max_rate = driver != nullptr ? read_i32(driver, kDriverMaxClientRate) : -1;
    const int32_t want = g_net_rate_now;
    const bool engine_set = (speed != g_net_rate_last && speed != want)
        || (driver != nullptr && max_rate != g_net_rate_last && max_rate != want);
    memcpy(static_cast<uint8_t*>(conn) + kPlayerCurrentNetSpeed, &want, sizeof(want));
    if (driver != nullptr) {
        memcpy(driver + kDriverMaxClientRate, &want, sizeof(want));
    }
    if (engine_set && note_event_cap(g_net_rate_lines, kNetRateLines, "netbudget rate cap")) {
        log::line("netbudget: host rate cap: connection %p CurrentNetSpeed %d -> %d B/s, MaxClientRate %d -> %d B/s",
                  conn, speed, want, max_rate, want);
    }
}
} // namespace

// Card 151, host, last in ubw_post_tick: the rows queued this tick go out in batch frames (under the pace gate).
// Rows sent during the world tick wait for this flush (or the next tick's pace step): one host frame later than unbatched.
void c151_flush_step() {
    if (!g_client_session) {
        ue::chan::chan_flush_all();
    }
}

// Card 140, host, first in ubw_post_tick: samples the gate and refills and drains the chan pace queues for the live
// remote PCs (any PC not listed this tick loses its queue, logged).
void c140_pace_step(void* engine) {
    if (g_client_session) {
        return;
    }
    void* const world = engine != nullptr ? g_get_game_world(engine) : nullptr;
    ue::chan::PaceConn conns[kC17Slots] = {};
    NetPcRel rels[kC17Slots] = {};
    int32_t n = 0;
    g_net_pawn_count = 0;
    net_npc_moved_step(GetTickCount64());
    net_rate_frame();
    for (int i = 0; world != nullptr && i < kC17Slots; ++i) {
        void* const pc = g_get_player_controller(world, i);
        const void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
        if (pawn != nullptr) {
            g_net_pawns[g_net_pawn_count++] = actor_location(pawn);
        }
        void* const conn = pc != nullptr && c26_is_remote_pc(pc) ? read_ptr(pc, kPcNetConnection) : nullptr;
        if (conn == nullptr) {
            continue;
        }
        net_rate_cap(world, conn);
        NetPcRel rel{pc, 0, nullptr};
        for (const NetPcRel& r : g_pc_rel) {
            rel = r.pc == pc ? r : rel;
        }
        conns[n] = ue::chan::PaceConn{pc};
        rels[n++] = rel;
        if (n == 1) {
            net_gate_sample(conn, pc, GetTickCount64());
        }
    }
    for (int32_t i = 0; i < kC17Slots; ++i) { // rows of this tick's live remote PCs only
        g_pc_rel[i] = i < n ? rels[i] : NetPcRel{};
    }
    const uint32_t dropped = ue::chan::chan_pace_step(conns, n, GetTickCount64());
    if (dropped != 0) {
        log::line("card140: host pace queue dropped %u frames of a PC no longer in the world", dropped);
    }
}

// Card 140, host: chan's live pace gate reading for pc (ChanOps::pace_live). g_pc_rel holds the PC channel's NumOutRec
// after its last RPC (detour_net_remote_function), so it counts the chan frames sent this tick.
bool c140_pace_live(const void* pc, ue::chan::PaceLive* out) {
    const void* const conn = pc != nullptr ? read_ptr(pc, kPcNetConnection) : nullptr;
    if (conn == nullptr || out == nullptr) {
        return false;
    }
    int32_t rel = 0;
    for (const NetPcRel& r : g_pc_rel) {
        // live from the channel while it is still the PC's (acks lower it between RPCs); else the last RPC's count
        // card 476: the channel kept from an earlier tick is read only while it is still a live object
        const bool own = r.pc == pc && r.channel != nullptr && object_listed(r.channel) &&
                         read_ptr(r.channel, kActorChannelActor) == pc;
        rel = r.pc == pc ? (own ? read_i32(r.channel, kConnChannelNumOutRec) : r.out) : rel;
    }
    // Card 391: UNetConnection::LastReceiveTime +0x1D8 and LastTickTime +0x1F8, doubles in seconds (`c391b78` slot
    // diffs: +0x1D8 moves only on a tick that read a client packet, +0x1F8 on every tick).
    double recv_s = 0.0;
    double tick_s = 0.0;
    memcpy(&recv_s, static_cast<const uint8_t*>(conn) + kConnLastReceiveTime, sizeof(recv_s));
    memcpy(&tick_s, static_cast<const uint8_t*>(conn) + kConnLastTickTime, sizeof(tick_s));
    const double silent_s = tick_s - recv_s;
    const int32_t silent_ms = silent_s > 0.0 && silent_s < 3600.0 ? static_cast<int32_t>(silent_s * 1000.0) : 0;
    *out = ue::chan::PaceLive{net_pending_bits(conn), rel, read_i32(conn, kPlayerCurrentNetSpeed), silent_ms};
    return true;
}

void c140_pace_logout(const void* pc) {
    const uint32_t n = ue::chan::chan_pace_drop(pc);
    for (NetPcRel& r : g_pc_rel) {
        r = r.pc == pc ? NetPcRel{} : r;
    }
    log::line("card140: host Logout: pace queue of PC %p dropped with %u frames", pc, n);
}

} // namespace ubw::hooks
