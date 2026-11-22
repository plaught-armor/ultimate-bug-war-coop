// Card 132: the send ledger, both directions. Every frame the mod sends goes through the ue::chan send functions,
// which ask c132_ledger_send for its stamp (a per-direction seq, per remote PC on the host, and this side's send ms)
// and carry it in the frame; the receiving detours take it off and call c132_ledger_recv before and after the
// handler. So every op is covered with no per-site code. The op-specific part (the step 2 digest key, a field and a
// value) comes from kC132Ops, one row per ChanOp. Handlers of world-state ops report applied, skipped or refused
// with c132_ledger_result; a handler that reports nothing is recorded as handled.
// Lines (capped per op and direction per map, the caps reported):
//   card132: sent <seq> t=<ms> <op> <key> <field> <value> [to <pc>]            (seq 0: not sent: <reason>)
//   card132: recv <seq> sent t=<ms> applied t=<ms> <op> <key> result <result>[: <reason>]
//   WARN card132: seq gap <from>-<to>                                           (always logged)
//   card132: clock <pc> offset <ms> (min recv - sent <ms>, rtt <ms>)            (once per kC132LedgerRoundMs)
// The host acks the last client seq it received (ChanOp::LedgerAck, in its ping); the client WARNs on any row not
// acked within kC132AckMs.

#include "ubw/hooks_internal.h"

#include <stdio.h>
#include <string.h>

namespace ubw::hooks {

namespace {

using ue::chan::ChanStamp;
using ue::chan::kChanArgMax;
using ue::chan::kChanNamedHead;
using ue::chan::kChanOpAt;
using ue::chan::kChanStampMsMask;
using ue::chan::kChanTextMax;
using ue::chan::WireString;

constexpr int32_t kC132Ring = 4096;          // ledger rows, both directions
constexpr int32_t kC132Peers = 8;            // PCs seen (host: remote PCs; client: its own)
constexpr int32_t kC132KeySlots = 8192;      // receiver: the last row per digest key (power of 2)
constexpr int32_t kC132KeyProbe = 32;        // linear probe bound
constexpr uint32_t kC132LinesLoud = 20;      // per op and direction per map: high volume ops
constexpr uint32_t kC132LinesQuiet = 600;    // the rest
constexpr ULONGLONG kC132AckMs = 5000;
constexpr ULONGLONG kC132NoAckMs = 3 * kC26PingMs; // client: rows pending and no ack this long
constexpr ULONGLONG kC132LedgerRoundMs = 30000;
constexpr uint8_t kC132NoPeer = 0xFF;

enum class C132KeyKind : uint8_t { None, Args, World, Door, Collision, Name, Objective, Text, Player };

struct C132OpInfo {
    const char* name;
    C132KeyKind key;
    bool loud; // high volume: the low line cap
};

// By ChanOp. Ping, Pong, Fire, Anim, Use (and TurretAim) carry no world key: seq and op only.
constexpr C132OpInfo kC132Ops[] = {
    {"None", C132KeyKind::None, false},          {"Fire", C132KeyKind::None, true},
    {"Ping", C132KeyKind::None, true},           {"Pong", C132KeyKind::None, true},
    {"Gib", C132KeyKind::Args, false},           {"Anim", C132KeyKind::None, true},
    {"Down", C132KeyKind::Player, false},        {"Use", C132KeyKind::None, false},
    {"Warp", C132KeyKind::Args, false},          {"Objective", C132KeyKind::Objective, false},
    {"UseName", C132KeyKind::Name, false},       {"Weapons", C132KeyKind::Text, false},
    {"Equip", C132KeyKind::Args, false},         {"Activate", C132KeyKind::Text, false},
    {"Ammo", C132KeyKind::Text, true},           {"AmmoSet", C132KeyKind::Text, false},
    {"AmmoMirror", C132KeyKind::Text, true},     {"ObjectiveActors", C132KeyKind::Text, false},
    {"GrenadeItem", C132KeyKind::Text, false},
    {"GrenadeSpawn", C132KeyKind::Text, false},  {"GrenadeExplode", C132KeyKind::Text, false},
    {"MechEnter", C132KeyKind::Text, false},     {"MechLeave", C132KeyKind::Text, false},
    {"DoorOp", C132KeyKind::Door, false},        {"CollisionOp", C132KeyKind::Collision, false},
    {"WorldOp", C132KeyKind::World, false},      {"TacSupport", C132KeyKind::Args, false},
    {"TacCharge", C132KeyKind::Text, false},     {"Difficulty", C132KeyKind::Text, false},
    {"TurretMount", C132KeyKind::Text, false},   {"TurretLeave", C132KeyKind::Text, false},
    {"TurretAim", C132KeyKind::None, true},      {"HoldDone", C132KeyKind::Name, false},
    {"Digest", C132KeyKind::Text, true},         {"DigestQuery", C132KeyKind::Text, false},
    {"LedgerAck", C132KeyKind::Args, true},      {"Hit", C132KeyKind::None, true}, // hit blood: an event, no world state
    {"Pose", C132KeyKind::Text, false},
    {"Decal", C132KeyKind::None, true}, // gore decal rows: the R rows check the live set
    {"Dropship", C132KeyKind::Name, false}, // "name state": the last state per troop dropship
    {"Fx", C132KeyKind::None, true}, // card 136 effect rows: events; the join replay carries the live ones
    {"GibPart", C132KeyKind::None, true}, // card 137 gib part rows: events (spawn, samples, rest)
    {"Clock", C132KeyKind::None, true}, // card 155 clock exchange: an event, no world state
    {"Switch", C132KeyKind::Args, false}, // card 141 switch request, like Equip
    {"Reload", C132KeyKind::Text, false}, // card 143 restored copy state before the rejoin
    {"Restore", C132KeyKind::Text, false}, // card 143 the same state back after the reconnect
    {"WarpTo", C132KeyKind::Text, false}, // card 146 all-down kill floor return, like Warp
    {"HitClaim", C132KeyKind::None, true}, // card 157 client hit claims: events, no world state
    {"Marker", C132KeyKind::None, true}, // card 166 shooter hit and kill markers: events, no world state
    {"BugForm", C132KeyKind::Args, false}, // card 208 a bug pawn's equipped form, like Equip
    {"IntroStart", C132KeyKind::Text, false}, // card 135 the host left its press-to-start screen
    {"EndLevel", C132KeyKind::None, true}, // card 260 the host's level end: an event, once per map
    {"Round", C132KeyKind::None, true}, // card 181 one client round: an event, no world state
    {"ScriptWrite", C132KeyKind::Text, false}, // card 310 level script writes: absolute "T", "C", "D" rows
    {"IntroDone", C132KeyKind::None, true}, // card 320 a client left its press-to-start screen: an event
    {"Radio", C132KeyKind::None, true}, // card 322 the host's conversation starts: events
    {"TravelIds", C132KeyKind::None, true}, // card 397 the next map's chapter and level IDs: an event
    {"HurtSound", C132KeyKind::None, true}, // card 455 a server copy's hurt sound: an event, no world state
    {"MechFuel", C132KeyKind::None, true}, // card 459 the piloted mech's fuel: one PC's own state, not world state
    {"Score", C132KeyKind::None, true}, // card 475 the host's score rows at a join replay: the digest's Score: entry checks them
    {"PeerWeapons", C132KeyKind::Text, false}, // card 050 b205 another player's weapon list on each client copy, like Weapons
    {"Batch", C132KeyKind::None, true}, // card 151 the batch frame: the ledger counts its rows, never the frame
    {"Stamina", C132KeyKind::None, true}, // card 490 the host infinite stamina setting: a session option, not world state
    {"CombatLevel", C132KeyKind::None, true}, // card 321 the host's combat level (music): the latest value wins, no world state
    {"Bark", C132KeyKind::None, true}, // card 451 a trooper bark's subtitle start or finish: an event, no world state
    {"Role", C132KeyKind::None, true}, // card 502 a client's run role (client, client2): its own PC's tag, not world state
    {"GibPose", C132KeyKind::None, true}, // card 519 gib part M, R, G rows in binary: events, like GibPart
    {"NpcTarget", C132KeyKind::None, true}, // card 532 NPC primary target rows in binary: many NPCs per frame, no one key
    {"CamFade", C132KeyKind::None, true}, // card 561 a camera fade of the client's own pawn: an event on one PC's screen
    {"DropshipBoard", C132KeyKind::None, true}, // card 601 the host ran a held transport boarding: an event, run once
    {"SwitchPress", C132KeyKind::None, true}, // card 602 a switch press broadcast on the host: an event, run once
    {"LevelAudio", C132KeyKind::None, true},  // card 604 a placed component play/stop or level-script music call: an event
    {"HordeZone", C132KeyKind::None, true},   // card 606 a horde zone Begin / End KillEmAll the host ran: an event
};
static_assert(sizeof(kC132Ops) / sizeof(kC132Ops[0]) == kChanOpCount);

enum C132Dir : uint8_t { kC132Sent, kC132Recv, kC132Dirs };
const char* const kC132DirText[kC132Dirs] = {"sent", "recv"};
enum C132Res : uint8_t { kC132ResSent, kC132ResNotSent, kC132ResApplied, kC132ResSkipped, kC132ResRefused,
                         kC132ResHandled, kC132ResNoHandler, kC132ResUnstamped, kC132ResPending, kC132Results };
const char* const kC132ResText[kC132Results] = {"sent",    "not sent", "applied",    "skipped",
                                                "refused", "handled",  "no handler", "unstamped", "pending"};

struct C132Row {
    uint32_t seq;     // 0: not sent
    uint32_t ms;      // the sender's stamp ms
    ULONGLONG at;     // this side's ms: the send, or the handler's end
    uint32_t key;     // the step 2 digest key; 0 = none
    uint8_t op;
    uint8_t dir;
    uint8_t peer;
    uint8_t result;
    char field[24];
    char value[32];
};

struct C132Peer {
    const void* pc;   // compared only, never read: it may be gone
    char name[48];
    uint32_t tx;      // the last seq sent to it
    uint32_t rx;      // the last seq received from it
    uint32_t acked;   // client: the last own seq the host acked
    uint32_t warned;  // client: unacked rows WARNed up to here
    ULONGLONG ack_at; // client: the last ack's arrival
    int32_t min_delta; // received ms - sent ms, the round's minimum
    bool delta_set;
    uint32_t rtt;     // card 026, the last
};

struct C132KeyRow {
    uint32_t key; // 0 = free
    uint32_t seq;
    uint32_t ms;
    ULONGLONG at;
    uint8_t op;
};

struct C132Ledger {
    C132Row ring[kC132Ring];
    uint32_t rows;    // total recorded (the ring index is rows % kC132Ring)
    C132Peer peer[kC132Peers];
    uint8_t next_peer; // replaced when the table is full
    uint32_t count[kC132Dirs][kChanOpCount][kC132Results];
    uint32_t lines[kC132Dirs][kChanOpCount];
    uint32_t capped[kC132Dirs][kChanOpCount];
    uint32_t gaps;
    uint32_t gap_rows;
    C132KeyRow keys[kC132KeySlots];
    uint32_t key_peak;
    uint32_t key_full;
    C132Row cur;       // the frame being received
    bool cur_open;     // card 408 F5: between c132l_recv_begin and c132l_recv_end (cur is the handler's frame)
    bool cur_reported;
    char cur_reason[64];
    ULONGLONG round_at;
    // Card 385, client: the session's lowest (received ms - sent ms) and lowest measured round trip over every PC of
    // the host. Both clocks are tick counts, so their difference is fixed for the session; a sample can only be late.
    int32_t low_delta;
    bool low_set;
    uint32_t low_rtt; // 0 = none measured yet
    ULONGLONG tx_at[kC132Ring]; // client: own send ms by seq % kC132Ring (unacked check)
};

C132Ledger g_c132l{}; // game thread

uint32_t c132l_fnv(const char* s, int32_t n) {
    uint32_t h = 2166136261u;
    for (int32_t i = 0; i < n && s[i] != '\0' && i < 256; ++i) {
        h = (h ^ static_cast<uint8_t>(s[i])) * 16777619u;
    }
    return h;
}

uint8_t c132l_peer(const void* pc) {
    if (pc == nullptr) {
        return kC132NoPeer;
    }
    for (uint8_t i = 0; i < kC132Peers; ++i) {
        if (g_c132l.peer[i].pc == pc) {
            return i;
        }
    }
    const uint8_t i = g_c132l.next_peer;
    g_c132l.next_peer = static_cast<uint8_t>((i + 1) % kC132Peers);
    C132Peer& p = g_c132l.peer[i];
    p = C132Peer{};
    p.pc = pc;
    snprintf(p.name, sizeof(p.name), "%s", object_name(pc).text); // pc is live: it is sending or receiving now
    return i;
}

// The text of a text frame, or the args "a,b,c" of an args frame, into out.
void c132l_frame_text(const WireString& s, bool args, char* out, int32_t cap) {
    int32_t n = 0;
    out[0] = '\0';
    for (int32_t i = kChanOpAt + 1; i < s.num - 1 && i - kChanOpAt <= kChanTextMax && n < cap - 8; ++i) {
        const uint32_t v = static_cast<uint32_t>(s.data[i]) - 1;
        if (args) {
            n += snprintf(out + n, static_cast<size_t>(cap - n), "%s%u", n > 0 ? "," : "", v);
            continue;
        }
        out[n++] = static_cast<char>(v);
        out[n] = '\0';
    }
}

void c132l_set(C132Row& r, const char* key_name, int32_t key_len, const char* field, const char* value) {
    r.key = key_name != nullptr && key_len > 0 ? c132l_fnv(key_name, key_len) : 0;
    snprintf(r.field, sizeof(r.field), "%s", field);
    snprintf(r.value, sizeof(r.value), "%s", value);
}

// WorldOp "kind name x y z value": the key is the placed actor (before any "|component"), or "#<value>" for a
// card 104 copy (P, D, and M, whose name is the rest rotation "pitch,yaw,roll"); the field is the kind (with the
// timeline component). hb49: M was keyed by its rotation text, so a copy's rest row after S never matched the copy's
// digest key and the round compared a moving copy (NukeLauncher pickup #239853, M seq 100 after S=85).
// Card 235: the name may hold spaces (MissleSilo_C_1|Fire Bomb), so it is everything between the kind and the last
// four fields, the way c98_receive_world splits the same text.
bool c132l_world_split(const char* t, char& kind, char (&name)[160], unsigned& value) {
    const size_t len = strnlen(t, kChanTextMax + 1);
    size_t cut = 0;
    int32_t spaces = 0;
    for (size_t i = len; i-- > 2 && spaces < 4;) {
        if (t[i] == ' ' && ++spaces == 4) {
            cut = i;
        }
    }
    if (len < 3 || t[1] != ' ' || cut <= 2 || cut - 2 >= sizeof(name)) {
        return false;
    }
    kind = t[0];
    memcpy(name, t + 2, cut - 2);
    name[cut - 2] = '\0';
    return sscanf_s(t + cut, " %*f %*f %*f %u", &value) == 1;
}

void c132l_world(C132Row& r, const char* t) {
    char kind = 0;
    char name[160] = {};
    unsigned value = 0;
    if (!c132l_world_split(t, kind, name, value)) {
        c132l_set(r, nullptr, 0, "world?", t);
        return;
    }
    char field[24];
    char v[16];
    snprintf(v, sizeof(v), "%u", value);
    const char* const bar = strchr(name, '|');
    snprintf(field, sizeof(field), "%c%s%s", kind, bar != nullptr ? "|" : "", bar != nullptr ? bar + 1 : "");
    if (kind == 'P' || kind == 'D' || kind == 'M') {
        char id[16];
        const int n = snprintf(id, sizeof(id), "#%u", value);
        c132l_set(r, id, n, field, kind == 'D' ? v : name);
        return;
    }
    c132l_set(r, name, bar != nullptr ? static_cast<int32_t>(bar - name) : static_cast<int32_t>(strlen(name)), field,
              v);
}

// DoorOp "name op", CollisionOp "name x y z value comp", and the name-first ops ("name ...").
void c132l_named(C132Row& r, C132KeyKind kind, const char* op_name, const char* t) {
    const char* const sp = strchr(t, ' ');
    const int32_t n = sp != nullptr ? static_cast<int32_t>(sp - t) : static_cast<int32_t>(strlen(t));
    if (kind == C132KeyKind::Collision) {
        unsigned value = 0;
        char comp[64] = {};
        char v[16];
        const bool ok = sp != nullptr && sscanf_s(sp, " %*f %*f %*f %u %63s", &value, comp, static_cast<unsigned>(sizeof(comp))) == 2;
        snprintf(v, sizeof(v), "%u", value);
        c132l_set(r, t, n, ok ? comp : "collision?", ok ? v : t);
        return;
    }
    c132l_set(r, t, n, kind == C132KeyKind::Door ? "door" : op_name, sp != nullptr ? sp + 1 : "");
}

// Objective (named frame): the key "O:<name 1>", the header as the value.
void c132l_objective(C132Row& r, const WireString& s) {
    const int32_t head = kChanOpAt + 1;
    if (s.num < head + kChanNamedHead + 1) {
        c132l_set(r, nullptr, 0, "objective?", "");
        return;
    }
    const uint32_t l1 = static_cast<uint32_t>(s.data[head + 3]) - 1;
    char key[ue::chan::kChanNameMax + 3] = "O:";
    for (uint32_t i = 0; i < l1 && i < ue::chan::kChanNameMax && head + kChanNamedHead + static_cast<int32_t>(i) < s.num;
         ++i) {
        key[2 + i] = static_cast<char>(static_cast<uint32_t>(s.data[head + kChanNamedHead + i]) - 1);
        key[3 + i] = '\0';
    }
    char v[32];
    snprintf(v, sizeof(v), "kind %u flags 0x%X count %u", static_cast<unsigned>(s.data[head]) - 1,
             static_cast<unsigned>(s.data[head + 1]) - 1, static_cast<unsigned>(s.data[head + 2]) - 1);
    c132l_set(r, key, static_cast<int32_t>(strlen(key)), "objective", v);
}

// Card 327: Down args "lo,hi,state,secs,slot" (c43_down_args): the key "P:<slot>", the field "life", the value the
// state, so the digest's P:<slot> entry is excluded while a Down is in flight.
void c132l_player(C132Row& r, const char* args) {
    unsigned v[5] = {};
    if (sscanf_s(args, "%u,%u,%u,%u,%u", &v[0], &v[1], &v[2], &v[3], &v[4]) != 5) {
        c132l_set(r, nullptr, 0, "down?", args);
        return;
    }
    char key[16];
    char value[12];
    const int n = snprintf(key, sizeof(key), "P:%u", v[4]);
    snprintf(value, sizeof(value), "%u", v[2]);
    c132l_set(r, key, n, "life", value);
}

// Fills r's key, field and value from the frame, by kC132Ops.
void c132l_decode(C132Row& r, const WireString& s) {
    if (r.op >= kChanOpCount) {
        c132l_set(r, nullptr, 0, "op?", "");
        return;
    }
    const C132OpInfo& info = kC132Ops[r.op];
    if (info.key == C132KeyKind::None) {
        c132l_set(r, nullptr, 0, "-", "-");
        return;
    }
    if (info.key == C132KeyKind::Objective) {
        c132l_objective(r, s);
        return;
    }
    static char text[kChanTextMax + 1];
    c132l_frame_text(s, info.key == C132KeyKind::Args || info.key == C132KeyKind::Player, text, sizeof(text));
    if (info.key == C132KeyKind::Player) {
        c132l_player(r, text);
    } else if (info.key == C132KeyKind::Args || info.key == C132KeyKind::Text) {
        c132l_set(r, nullptr, 0, info.key == C132KeyKind::Args ? "args" : "text", text);
    } else if (info.key == C132KeyKind::World) {
        c132l_world(r, text);
    } else {
        c132l_named(r, info.key, info.name, text);
    }
}

void c132l_record(const C132Row& r) {
    g_c132l.ring[g_c132l.rows % kC132Ring] = r;
    ++g_c132l.rows;
    if (r.op < kChanOpCount) {
        ++g_c132l.count[r.dir][r.op][r.result];
    }
}

// True when this op and direction still has log lines this map; counts the cap otherwise.
bool c132l_line_ok(uint8_t dir, uint8_t op) {
    if (op >= kChanOpCount) {
        return true;
    }
    if (g_c132l.lines[dir][op] >= (kC132Ops[op].loud ? kC132LinesLoud : kC132LinesQuiet)) {
        ++g_c132l.capped[dir][op];
        return false;
    }
    ++g_c132l.lines[dir][op];
    return true;
}

const char* c132l_op_name(uint8_t op) {
    return op < kChanOpCount ? kC132Ops[op].name : "?";
}

const char* c132l_peer_name(uint8_t peer) {
    return peer < kC132Peers ? g_c132l.peer[peer].name : "-";
}

// Receiver: the last row per key, for the digest's mismatch lines.
void c132l_note_key(const C132Row& r) {
    if (r.key == 0) {
        return;
    }
    for (int32_t i = 0; i < kC132KeyProbe; ++i) {
        C132KeyRow& k = g_c132l.keys[(r.key + static_cast<uint32_t>(i)) & (kC132KeySlots - 1)];
        if (k.key == 0 || k.key == r.key) {
            g_c132l.key_peak += k.key == 0 ? 1 : 0;
            k = C132KeyRow{r.key, r.seq, r.ms, r.at, r.op};
            return;
        }
    }
    ++g_c132l.key_full;
}

// Receiver: a seq that does not follow the peer's last; 1 restarts the peer (a new PC after a reconnect).
void c132l_check_gap(C132Peer& p, uint32_t seq) {
    if (seq == 1 && p.rx > 0) {
        log::line("card132: %s seq restart from %s after %u", side(), p.name, p.rx);
    } else if (seq != p.rx + 1) {
        ++g_c132l.gaps;
        g_c132l.gap_rows += seq > p.rx ? seq - p.rx - 1 : 0;
        log::line("WARN card132: seq gap %u-%u from %s (got %u)", p.rx + 1, seq > 0 ? seq - 1 : 0, p.name, seq);
    }
    p.rx = seq;
}

void c132l_clock_sample(C132Peer& p, uint32_t sent_ms) {
    const uint32_t now = static_cast<uint32_t>(GetTickCount64()) & kChanStampMsMask;
    uint32_t d = (now - sent_ms) & kChanStampMsMask;
    const int32_t delta = d >= (1u << 29) ? static_cast<int32_t>(d) - (1 << 30) : static_cast<int32_t>(d);
    p.min_delta = !p.delta_set || delta < p.min_delta ? delta : p.min_delta;
    p.delta_set = true;
    if (!g_client_session) { // host: nothing reads the session minimum
        return;
    }
    g_c132l.low_delta = !g_c132l.low_set || delta < g_c132l.low_delta ? delta : g_c132l.low_delta;
    g_c132l.low_set = true;
}

void c132l_recv_begin(void* pc, uint8_t op, const ChanStamp& stamp, bool stamped) {
    C132Row& r = g_c132l.cur;
    r = C132Row{};
    r.seq = stamp.seq;
    r.ms = stamp.ms;
    r.op = op;
    r.dir = kC132Recv;
    r.peer = c132l_peer(pc);
    g_c132l.cur_open = stamped;
    g_c132l.cur_reported = false;
    g_c132l.cur_reason[0] = '\0';
    if (r.peer >= kC132Peers) {
        return;
    }
    C132Peer& p = g_c132l.peer[r.peer];
    if (!stamped) {
        log::line("WARN card132: %s recv op %s from %s with no stamp", side(), c132l_op_name(op), p.name);
        return;
    }
    c132l_check_gap(p, stamp.seq);
    if (!stamp.held) { // card 474: a held frame's ms is its enqueue time; its delta carries the queue wait
        c132l_clock_sample(p, stamp.ms);
    }
}

void c132l_recv_end(const WireString& s, bool stamped, bool ran) {
    C132Row& r = g_c132l.cur;
    g_c132l.cur_open = false;
    r.at = GetTickCount64();
    c132l_decode(r, s);
    r.result = !stamped ? kC132ResUnstamped
                        : (!ran ? kC132ResNoHandler : (g_c132l.cur_reported ? r.result : kC132ResHandled));
    c132l_record(r);
    c132l_note_key(r);
    const bool bad = r.result != kC132ResApplied && r.result != kC132ResHandled && r.result != kC132ResPending;
    if (!c132l_line_ok(kC132Recv, r.op) && !bad) {
        return;
    }
    log::line("%scard132: recv %u sent t=%u applied t=%llu %s %08x %s %s result %s%s%s", bad ? "WARN " : "", r.seq,
              r.ms, static_cast<unsigned long long>(r.at & kChanStampMsMask), c132l_op_name(r.op), r.key, r.field,
              r.value, kC132ResText[r.result], g_c132l.cur_reason[0] != '\0' ? ": " : "", g_c132l.cur_reason);
}

// Client: rows the host has not acked kC132AckMs after they were sent, from an ack that arrived at `now`.
void c132l_check_acks(C132Peer& p, ULONGLONG now) {
    uint32_t from = 0;
    uint32_t to = 0;
    uint32_t first = p.acked > p.warned ? p.acked : p.warned;
    first = p.tx > first + kC132Ring ? p.tx - kC132Ring : first;
    for (uint32_t seq = first + 1; seq <= p.tx; ++seq) {
        if (now - g_c132l.tx_at[seq % kC132Ring] <= kC132AckMs) {
            break;
        }
        from = from == 0 ? seq : from;
        to = seq;
    }
    if (from != 0) {
        log::line("WARN card132: client rows %u-%u not acked by the host within %llu ms (acked %u)", from, to,
                  static_cast<unsigned long long>(kC132AckMs), p.acked);
        p.warned = to;
    }
}

void c132l_clock_round(ULONGLONG now) {
    if (now < g_c132l.round_at) {
        return;
    }
    g_c132l.round_at = now + kC132LedgerRoundMs;
    for (C132Peer& p : g_c132l.peer) {
        if (p.pc != nullptr && p.delta_set) {
            // Card 385: the session offset c132_ledger_clock_offset returns (client; set from the lowest delta), and
            // its two inputs. The value printed is the function's own, so the row and its readers cannot differ.
            char session[96] = "";
            int32_t offset = 0;
            if (c132_ledger_clock_offset(p.pc, &offset)) {
                snprintf(session, sizeof(session), "; session offset %d ms (low delta %d, low rtt %u)", offset,
                         g_c132l.low_delta, g_c132l.low_rtt);
            }
            log::line("card132: %s clock %s offset %d ms (min recv - sent %d ms, rtt %u ms)%s", side(), p.name,
                      p.min_delta - static_cast<int32_t>(p.rtt / 2), p.min_delta, p.rtt, session);
            p.delta_set = false;
        }
    }
}

} // namespace

// The op's name from kC132Ops ("?" past the table), for net_budget.cpp's chan bytes lines.
const char* c132_op_name(uint8_t op) {
    return c132l_op_name(op);
}

// Card 474: a frame the host's pace queue held is stamped with its enqueue time enq_ms (0: not queued), so the
// client's frame age (c132_ledger_frame_age_ms) covers the wait in the queue: a frame held for a stalled client read
// as fresh before. Such a frame is marked held and takes no part in the client's clock offset (c132l_recv_begin).
// GetTickCount64 is monotonic and the queue's entries are stamped at their push, so enq_ms <= r.at: the stamp only
// moves earlier, never later than the send. A dropped queue (PC gone, chan_pace_drop) sends none of its frames, and a
// reconnect gets a new queue, so no stale enqueue time is sent.
ue::chan::ChanStamp c132_ledger_send(void* pc, const WireString& s, bool to_host, const char* unsent, uint64_t enq_ms) {
    C132Row r{};
    r.op = static_cast<uint8_t>(s.num > kChanOpAt ? s.data[kChanOpAt] : 0);
    r.dir = kC132Sent;
    r.peer = c132l_peer(pc);
    r.at = GetTickCount64();
    const bool held = enq_ms != 0 && enq_ms < r.at;
    r.ms = static_cast<uint32_t>(held ? enq_ms : r.at) & kChanStampMsMask;
    r.result = unsent != nullptr ? kC132ResNotSent : kC132ResSent;
    if (unsent == nullptr && r.peer < kC132Peers) {
        C132Peer& p = g_c132l.peer[r.peer];
        r.seq = ++p.tx;
        if (to_host) {
            g_c132l.tx_at[r.seq % kC132Ring] = r.at;
        }
    }
    c132l_record(r);
    if (c132l_line_ok(kC132Sent, r.op)) {
        c132l_decode(r, s); // only the log row reads a sent row's key, field and value: past the line cap, skip it
        char queued[40] = "";
        if (held) {
            snprintf(queued, sizeof(queued), " (card474: queued %llu ms)",
                     static_cast<unsigned long long>(r.at - enq_ms));
        }
        log::line("card132: sent %u t=%u %s %08x %s %s to %s%s%s%s", r.seq, r.ms, c132l_op_name(r.op), r.key, r.field,
                  r.value, c132l_peer_name(r.peer), unsent != nullptr ? ": not sent: " : "",
                  unsent != nullptr ? unsent : "", queued);
    }
    return ChanStamp{r.seq, r.ms, held};
}

void c132_ledger_recv(void* pc, wchar_t op, const ChanStamp& stamp, bool stamped, ue::chan::ChanRecv phase,
                      const WireString& s) {
    const uint8_t o = static_cast<uint8_t>(static_cast<size_t>(op) < kChanOpCount ? op : 0);
    c137_host_frame(phase == ue::chan::ChanRecv::Before); // card 137 2a: changes made by a host frame's handler
    if (phase == ue::chan::ChanRecv::Before) {
        c132l_recv_begin(pc, o, stamp, stamped);
        return;
    }
    c132l_recv_end(s, stamped, phase == ue::chan::ChanRecv::Ran);
}

void c132_ledger_result(C132Result result, const char* reason) {
    g_c132l.cur_reported = true;
    g_c132l.cur.result = result == C132Result::Applied   ? kC132ResApplied
                         : result == C132Result::Skipped ? kC132ResSkipped
                         : result == C132Result::Pending ? kC132ResPending
                                                         : kC132ResRefused;
    snprintf(g_c132l.cur_reason, sizeof(g_c132l.cur_reason), "%s", reason != nullptr ? reason : "");
}

void c132_ledger_rtt(const void* pc, uint32_t rtt) {
    const uint8_t i = c132l_peer(pc);
    if (i < kC132Peers) {
        g_c132l.peer[i].rtt = rtt;
    }
    if (g_client_session && rtt != 0 && (g_c132l.low_rtt == 0 || rtt < g_c132l.low_rtt)) {
        g_c132l.low_rtt = rtt;
    }
}

// Host, in its ping to pc: the last seq received from pc.
void c132_ledger_ack_to(void* pc) {
    for (const C132Peer& p : g_c132l.peer) {
        if (p.pc == pc && p.rx > 0) {
            chan_to_client(pc, ChanOp::LedgerAck, p.rx % kChanArgMax);
            return;
        }
    }
}

// Client: ChanOp::LedgerAck. The full seq is the latest own seq at or below the last sent with that remainder.
void c132_ledger_ack(void* pc, uint32_t ack) {
    const uint8_t i = c132l_peer(pc);
    if (i >= kC132Peers) {
        return;
    }
    C132Peer& p = g_c132l.peer[i];
    const uint32_t back = (p.tx % kChanArgMax + kChanArgMax - ack % kChanArgMax) % kChanArgMax;
    p.acked = p.tx >= back ? p.tx - back : 0;
    p.ack_at = GetTickCount64();
    c132_ledger_result(C132Result::Applied, nullptr);
    c132l_check_acks(p, p.ack_at);
}

// Both sides, from the card 026 ping step: the clock round, and (client) rows with no ack at all.
void c132_ledger_step(void* pc0, ULONGLONG now) {
    c132l_clock_round(now);
    if (!g_client_session || pc0 == nullptr) {
        return;
    }
    for (C132Peer& p : g_c132l.peer) {
        const bool pending = p.pc == pc0 && p.tx > p.acked && p.tx > p.warned;
        if (pending && now - (p.ack_at != 0 ? p.ack_at : now) > kC132NoAckMs) {
            log::line("WARN card132: client rows %u-%u pending and no ack from the host for %llu ms", p.acked + 1, p.tx,
                      static_cast<unsigned long long>(now - p.ack_at));
            p.warned = p.tx;
        }
    }
}

uint32_t c132_ledger_sent_seq(const void* pc) {
    for (const C132Peer& p : g_c132l.peer) {
        if (p.pc == pc && pc != nullptr) {
            return p.tx;
        }
    }
    return 0;
}

uint32_t c132_ledger_recv_seq(const void* pc) {
    for (const C132Peer& p : g_c132l.peer) {
        if (p.pc == pc && pc != nullptr) {
            return p.rx;
        }
    }
    return 0;
}

// Card 385: the client left its session; the next host is another machine with another tick count.
void c132_ledger_session_end() {
    g_c132l.low_set = false;
    g_c132l.low_rtt = 0;
}

// Card 313: client, own clock minus the host's clock in ms.
// Card 385: from the session's lowest sample and lowest round trip, not pc's last round. A new PC's first round after
// a map load held one sample that waited out the load (c373b69b: 2788 ms, no round trip yet, true offset about 5 ms),
// and the join replay's 'L' rows arrive in exactly that window.
bool c132_ledger_clock_offset(const void* pc, int32_t* offset) {
    if (pc == nullptr || !g_c132l.low_set) {
        return false;
    }
    // Card 385 follow-up (c448b170a: first round trip after a reconnect 1973 ms, a load hitch; offset -982 ms for
    // 3.1 s, the join replay's window). A round trip over kC385RttMaxMs is a stall, not the wire: it counts as not
    // measured, and the offset is then off by the one-way latency at most.
    constexpr uint32_t kC385RttMaxMs = 400;
    const uint32_t rtt = g_c132l.low_rtt <= kC385RttMaxMs ? g_c132l.low_rtt : 0;
    *offset = g_c132l.low_delta - static_cast<int32_t>(rtt / 2);
    return true;
}

// Card 408 F5: client, the age of the frame being handled: now minus its stamp's send time minus the clock offset
// (own clock minus the host's). It covers the host's send queue (card 474: a queued frame's stamp is its enqueue
// time), the wire and a client frame stall before the handler ran. All ms are 30-bit and the difference folds to a
// signed value at 2^29, so the tick count's wrap past 2^30 ms reads right. -1: no frame in handling, an unstamped frame, no offset yet, or an age outside [0, max_ms].
int32_t c132_ledger_frame_age_ms(const void* pc, int32_t max_ms) {
    int32_t offset = 0;
    if (!g_c132l.cur_open || g_c132l.cur.seq == 0 || !c132_ledger_clock_offset(pc, &offset)) {
        return -1;
    }
    const uint32_t now = static_cast<uint32_t>(GetTickCount64()) & kChanStampMsMask;
    const uint32_t d = (now - g_c132l.cur.ms - static_cast<uint32_t>(offset)) & kChanStampMsMask;
    const int32_t age = d >= (1u << 29) ? static_cast<int32_t>(d) - (1 << 30) : static_cast<int32_t>(d);
    return age >= 0 && age <= max_ms ? age : -1;
}

bool c132_ledger_key_row(uint32_t key, C132LedgerKey& out) {
    for (int32_t i = 0; i < kC132KeyProbe && key != 0; ++i) {
        const C132KeyRow& k = g_c132l.keys[(key + static_cast<uint32_t>(i)) & (kC132KeySlots - 1)];
        if (k.key == 0) {
            return false;
        }
        if (k.key == key) {
            out = C132LedgerKey{k.seq, k.ms, static_cast<uint32_t>(k.at & kChanStampMsMask), c132l_op_name(k.op)};
            return true;
        }
    }
    return false;
}

void c132_ledger_arm_map() {
    memset(g_c132l.keys, 0, sizeof(g_c132l.keys));
    memset(g_c132l.lines, 0, sizeof(g_c132l.lines));
    memset(g_c132l.capped, 0, sizeof(g_c132l.capped));
    g_c132l.key_peak = 0;
    g_c132l.key_full = 0;
}

// The ledger table: every op, both directions, with zeros.
void c132_ledger_report(const char* why) {
    log::line("card132: ledger %s: %s ring %d rows %u peak %u, key table %u of %d (%u full), gaps %u (%u rows)", why,
              side(), kC132Ring, g_c132l.rows, g_c132l.rows < kC132Ring ? g_c132l.rows : kC132Ring, g_c132l.key_peak,
              kC132KeySlots, g_c132l.key_full, g_c132l.gaps, g_c132l.gap_rows);
    for (size_t op = 1; op < kChanOpCount; ++op) {
        for (uint8_t d = 0; d < kC132Dirs; ++d) {
            const uint32_t* const c = g_c132l.count[d][op];
            log::line("card132: ledger %s %s %s: sent %u not-sent %u applied %u skipped %u refused %u handled %u "
                      "no-handler %u unstamped %u pending %u; lines capped %u",
                      why, kC132Ops[op].name, kC132DirText[d], c[kC132ResSent], c[kC132ResNotSent], c[kC132ResApplied],
                      c[kC132ResSkipped], c[kC132ResRefused], c[kC132ResHandled], c[kC132ResNoHandler],
                      c[kC132ResUnstamped], c[kC132ResPending], g_c132l.capped[d][op]);
        }
    }
}

} // namespace ubw::hooks
