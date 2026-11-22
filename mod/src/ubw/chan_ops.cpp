// Chan ops: the host and client handler tables of the mod channel by op, the per-op handlers split out
// of the old receive functions, and the adapters that fit each receiver to ChanHandler (split S7). The
// receivers stay with their own features.

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

// Host handlers: one well-formed frame from the client that owns pc. The relay target is always that PC's own pawn.
void c26_host_fire(void* pc, const WireString*, const ChanArgs& args) {
    const uint32_t arg = args.v[0];
    const void* const pawn = read_ptr(pc, kControllerPawn);
    const int32_t kind = static_cast<int32_t>((arg >> 1) & kFireKindMask);
    const bool on = (arg & kFireOnBit) != 0;
    c26_edge_line("host received Fire", arg, object_name(pc).text);
    if (kind == kFireAds) {
        c160_host_ads_bits(pc, arg); // before the relay press: the server copy's IronSights reads them on activation
    }
    if (kind == kFirePrimary) {
        c44_host_fire(pawn, arg);
    }
    const bool mech = is_a(pawn, g_mech_class());
    const bool mech_ok = kind == kFirePrimary || (kind == kFireMechAlt && mech) || ((kind == kFireAds || kind == kFireReload) && !mech);
    // card 196: an MI or bug player pawn (c196_inventory_at), or a mounted turret for the primary
    const bool rider = c196_inventory_at(pawn) != 0 || (kind == kFirePrimary && is_a(pawn, g_turret_class()));
    if (mech_ok && rider && sprint_side(pawn) == kSideServerCopy) { // card 125: a mounted turret the PC drives
        // card 109: a downed client's Fire 1 is relayed too (a downed player can shoot)
        // card 175: a soldier copy's release waits for the client's rounds; a mech or turret releases at once
        const bool soldier = kind == kFirePrimary && !on && !mech && is_a(pawn, g_player_mi_class());
        c24_relay(pawn, kind, on, soldier ? (arg >> kFireRoundsShift) & kFireRoundsMax : 0u);
    }
}

void c26_host_use(void* pc, const WireString*, const ChanArgs& args) {
    void* const pawn = read_ptr(pc, kControllerPawn);
    if (c196_inventory_at(pawn) != 0 && sprint_side(pawn) == kSideServerCopy) { // card 196: MI or bug
        c43_receive_use(pawn, args.v[0]);
    }
}

// Card 151 (c448b197a): the Pong is sent now, not batched. Batched, it waited for the tick's flush and the client's
// card132 round trip rose 30 -> 46 ms, about one 60 fps frame (session offset -6 -> -17 ms).
void c26_host_ping(void* pc, const WireString*, const ChanArgs& args) {
    uint32_t ahead = 0;
    ue::chan::chan_send_client_now(pc, chan_frame(ChanOp::Pong, ChanArgs{{args.v[0]}}), &ahead);
}

// Card 502 finding 2 (b263), host: a client's test role, stored on its PC's C50Copy; one row when it changes. An
// older PC holding the same role (the client's PC before a rejoin) gives it up.
void c502_receive_role(void* pc, uint32_t role) {
    if (sprint_side(pc) != kSideServerCopy || role == 0 || role > static_cast<uint32_t>(kC17Slots)) {
        return;
    }
    C50Copy* const c = c50_copy(object_index(pc), true);
    if (c == nullptr || c->role == role) {
        return;
    }
    for (int32_t i = 0; i < g_c50.copies; ++i) {
        g_c50.copy[i].role = g_c50.copy[i].role == role ? 0 : g_c50.copy[i].role;
    }
    c->role = role;
    char name[16];
    snprintf(name, sizeof(name), role == 1 ? "client" : "client%u", role);
    log::line("card502: host t=%llu PC %s is %s", static_cast<unsigned long long>(GetTickCount64()),
              object_name(pc).text, name);
}

void c26_host_pong(void* pc, const WireString*, const ChanArgs& args) {
    c26_pong(pc, args.v[0]);
    if (!c348_pong_current(args.v[0])) {
        return; // card 348: a Pong to a ping sent before this map's arm (old-map traffic) never opens the gate
    }
    const bool first = c50_first_pong(pc); // card 139: the send gate opens here, before any join replay
    if (first) {
        sync_join_replay(pc); // card 326: every registered mechanism's join replay, in the table's order
        pl_first_pong(pc);    // card 155: the join count and the first clock query
    }
}

// Client handlers: one well-formed frame from the host on our PC.
void c26_client_fire(void*, const WireString*, const ChanArgs& args) {
    const uint32_t arg = args.v[0];
    if (((arg >> 1) & kFireKindMask) == static_cast<uint32_t>(kFirePrimary)) {
        g_c24.host_fire = (arg & kFireOnBit) != 0;
    }
    c26_edge_line("client received Fire", arg, "host fire");
}

// Card 120: the client's first Pong of a map starts every host join replay (the card 049 journal, the card 098
// world ops, the card 096 pause lift). A journal Add makes an objective marker, and the marker's
// ShowOnMinimap reads the PC minimap's OriginActor, which is null until the client's pawn is set up: a replay
// 71 ms after ClientRestart crashed in USTMinimapComponent::GetMinimapLocation. So the client answers no Ping
// until its minimap has an origin, once per world; after that it answers every Ping.
namespace {
struct C120Gate {
    const void* ready_world = nullptr; // the world whose minimap origin was seen; answers flow while it is current
    const void* held_world = nullptr;  // the world the hold below was logged for
    ULONGLONG held_ms = 0;             // first Ping held in held_world
    ULONGLONG open_ms = 0;             // card 146: when the first Pong of this map load went (0 while held)
    bool warned = false;
    bool ping_held = false; // a host Ping arrived while not ready; c120_client_step answers it once ready
    uint32_t held_arg = 0;  // that Ping's number
};
C120Gate g_c120;

constexpr ULONGLONG kC120WarnMs = 30000;

bool c120_client_ready(const void* pc, bool from_ping) {
    if (from_ping ? c138_hold_ping() : c138_holding()) {
        return false; // card 138: no join replay or live send before the level actors' BeginPlay (counted there)
    }
    const void* const world = g_actor_get_world(pc);
    if (world == nullptr || world == g_c120.ready_world) {
        return world != nullptr;
    }
    if (!is_a(pc, g_st_pc_class())) {
        g_c120.ready_world = world;
        g_c120.open_ms = GetTickCount64();
        return true; // +0x7B0 is an ASTPlayerController field; another PC class has no minimap, so no marker path
    }
    const void* const minimap = read_ptr(pc, kPcMinimap);
    const void* const origin = minimap != nullptr ? read_ptr(minimap, kMinimapOrigin) : nullptr;
    const ULONGLONG now = GetTickCount64();
    if (origin != nullptr) {
        g_c120.ready_world = world;
        g_c120.open_ms = now;
        log::line("card120: client t=%llu minimap origin %p (%s) set: Pong to the host from now on (held %llu ms)",
                  static_cast<unsigned long long>(now), origin, class_name(origin).text,
                  static_cast<unsigned long long>(g_c120.held_world == world ? now - g_c120.held_ms : 0));
        return true;
    }
    if (g_c120.held_world != world) {
        g_c120 = C120Gate{g_c120.ready_world, world, now, 0, false, false, 0};
        log::line("card120: client t=%llu Pong held: minimap %p has no origin yet", static_cast<unsigned long long>(now),
                  minimap);
    } else if (!g_c120.warned && now - g_c120.held_ms > kC120WarnMs) {
        g_c120.warned = true;
        log::line("WARN card120: client t=%llu Pong still held after %llu ms: minimap %p has no origin",
                  static_cast<unsigned long long>(now), static_cast<unsigned long long>(now - g_c120.held_ms), minimap);
    }
    return false;
}
} // namespace

// Both sides, at a real map load (c50_arm_map): readiness is per map load, not per world address, since a new world
// can be allocated where the old one was. The world check in c120_client_ready stays as a second guard.
void c120_arm_map() {
    g_c120 = C120Gate{};
}

// Card 146: the client's first Pong of this map load, which starts the host's join replays (the pose rows among
// them). 0 while the Pong is held, so a corpse arrival's fallback clock does not run before the replay can come.
ULONGLONG c120_pong_open_ms() {
    return g_c120.open_ms;
}

ULONGLONG g_c128_host_ping_ms = 0;

namespace {
// Card 502 finding 2 (b263): this instance's test role from its UBWCOOP_LOG_TAG (run_pair.sh sets "client" and
// "client2"): 1 "client", N "clientN" (2-9), 0 any other tag.
uint32_t c502_own_role() {
    const wchar_t* const t = g_c63.tag;
    constexpr wchar_t kWord[] = L"client";
    for (int i = 0; i < 6; ++i) {
        if (t[i] != kWord[i]) {
            return 0;
        }
    }
    if (t[6] == L'\0') {
        return 1;
    }
    return t[6] >= L'2' && t[6] <= L'9' && t[7] == L'\0' ? static_cast<uint32_t>(t[6] - L'0') : 0;
}

void c26_client_answer(void* pc, uint32_t arg) {
    c143_client_ping(pc); // card 143: a held Restore goes before the Pong that starts the host's join replays
    chan_to_host(pc, ChanOp::Pong, arg);
    const uint32_t role = g_c63_on ? c502_own_role() : 0;
    if (role != 0) {
        chan_to_host(pc, ChanOp::Role, role); // card 502 finding 2: with every Pong, since copies empty at each map
    }
    if (g_c42_test && (arg == kC42WalkPing || arg == kC42DonePing)) {
        c42_ping(pc, arg);
    }
    if (g_c28_shots && arg == kC26ShotPing) {
        request_shot(pc, false); // HUD proof: this shot is taken while a host frame is being handled
        log::line("card026: client t=%llu shot requested at host ping %u (HUD check)",
                  static_cast<unsigned long long>(GetTickCount64()), arg);
    }
}
} // namespace

void c26_client_ping(void* pc, const WireString*, const ChanArgs& args) {
    const uint32_t arg = args.v[0];
    g_c128_host_ping_ms = GetTickCount64();
    if (!c120_client_ready(pc, true)) {
        g_c120.ping_held = true; // answered by c120_client_step once ready, not at the host's next Ping
        g_c120.held_arg = arg;
        return;
    }
    g_c120.ping_held = false;
    c26_client_answer(pc, arg);
}

// Client, every engine tick: a Ping the gate held is answered as soon as the client is ready. Without this the
// first Pong of a map load waited for the host's next Ping, up to kC26PingMs (c145-r3: 5 s after the travel).
void c120_client_step(void* pc) {
    if (!g_c120.ping_held || pc == nullptr || !c120_client_ready(pc, false)) {
        return;
    }
    g_c120.ping_held = false;
    log::line("card157: client t=%llu held Ping %u answered at ready", static_cast<unsigned long long>(GetTickCount64()),
              g_c120.held_arg);
    c26_client_answer(pc, g_c120.held_arg);
}

void c26_client_pong(void* pc, const WireString*, const ChanArgs& args) {
    c26_pong(pc, args.v[0]);
}

// Split S7: the receivers keep their own signatures; these one-line adapters fit them to ChanHandler.
template <void (*F)(void*, const WireString*)>
void chan_pc_s(void* pc, const WireString* s, const ChanArgs&) { F(pc, s); }
template <void (*F)(const WireString*)>
void chan_s(void*, const WireString* s, const ChanArgs&) { F(s); }
template <void (*F)(void*, const ChanArgs&)>
void chan_pc_args(void* pc, const WireString*, const ChanArgs& args) { F(pc, args); }
template <void (*F)(void*, uint32_t)>
void chan_pc_arg0(void* pc, const WireString*, const ChanArgs& args) { F(pc, args.v[0]); }
// Card 252 step 1b: chan_pc_s with the receiver's time added to one class.
template <void (*F)(void*, const WireString*), C252Class C>
void chan_pc_s_timed(void* pc, const WireString* s, const ChanArgs&) {
    const C252Timer timer(C);
    F(pc, s);
}

using ChanTable = std::array<ChanHandler, kChanOpCount>;

// Host (ServerChangeName) handlers by op; every op not set here is dropped.
consteval ChanTable chan_host_table() {
    ChanTable t{};
    t[static_cast<size_t>(ChanOp::Fire)] = c26_host_fire;
    t[static_cast<size_t>(ChanOp::Use)] = c26_host_use;
    t[static_cast<size_t>(ChanOp::Equip)] = chan_pc_s<c57_receive_equip>; // card 057 (text since card 312)
    t[static_cast<size_t>(ChanOp::Switch)] = chan_pc_arg0<c141_receive_switch>; // card 141
    t[static_cast<size_t>(ChanOp::Ping)] = c26_host_ping;
    t[static_cast<size_t>(ChanOp::Pong)] = c26_host_pong;
    t[static_cast<size_t>(ChanOp::UseName)] = chan_pc_s<c50_receive_use_name>; // card 050
    t[static_cast<size_t>(ChanOp::AmmoMirror)] = chan_pc_s<c72_receive_mirror>; // card 072
    t[static_cast<size_t>(ChanOp::MechEnter)] = chan_pc_s<c75_receive_enter>; // card 075
    t[static_cast<size_t>(ChanOp::MechLeave)] = chan_pc_s<c75_receive_leave>;
    t[static_cast<size_t>(ChanOp::TurretMount)] = chan_pc_s<c123_receive_mount>; // card 123
    t[static_cast<size_t>(ChanOp::TurretLeave)] = chan_pc_s<c123_receive_leave>;
    t[static_cast<size_t>(ChanOp::TurretAim)] = c125_host_aim; // card 125
    t[static_cast<size_t>(ChanOp::HoldDone)] = chan_pc_s<c133_host_done>; // card 133
    t[static_cast<size_t>(ChanOp::CollisionOp)] = chan_pc_s<c421_receive_want>; // card 421
    t[static_cast<size_t>(ChanOp::DigestQuery)] = chan_pc_s_timed<c132_host_query, C252Class::DigestRecv>; // card 132
    t[static_cast<size_t>(ChanOp::TacSupport)] = chan_pc_arg0<c100_receive_tac>; // card 100
    t[static_cast<size_t>(ChanOp::Clock)] = chan_pc_s<pl_clock_host>; // card 155
    t[static_cast<size_t>(ChanOp::Restore)] = chan_pc_s<c143_receive_restore>; // card 143
    t[static_cast<size_t>(ChanOp::HitClaim)] = chan_pc_s<c157_receive_claim>; // card 157 client hit claims (host side)
    t[static_cast<size_t>(ChanOp::Round)] = chan_pc_arg0<c181_receive_round>; // card 181
    t[static_cast<size_t>(ChanOp::IntroDone)] = chan_pc_arg0<c320_receive_done>; // card 320
    t[static_cast<size_t>(ChanOp::Role)] = chan_pc_arg0<c502_receive_role>; // card 502 finding 2
    return t;
}

// Client (ClientMessage) handlers by op; every op not set here is dropped.
consteval ChanTable chan_client_table() {
    ChanTable t{};
    t[static_cast<size_t>(ChanOp::Fire)] = c26_client_fire;
    t[static_cast<size_t>(ChanOp::Ping)] = c26_client_ping;
    t[static_cast<size_t>(ChanOp::Pong)] = c26_client_pong;
    t[static_cast<size_t>(ChanOp::Anim)] = chan_pc_args<c41_receive_anim>;
    t[static_cast<size_t>(ChanOp::Gib)] = chan_pc_args<c42_receive_gib>;
    t[static_cast<size_t>(ChanOp::Down)] = chan_pc_args<c43_receive_down>;
    t[static_cast<size_t>(ChanOp::BugForm)] = chan_pc_args<c208_receive>; // card 208
    t[static_cast<size_t>(ChanOp::IntroStart)] = chan_pc_s<c135_receive>; // card 135
    t[static_cast<size_t>(ChanOp::EndLevel)] = chan_pc_args<c260_receive>; // card 260
    t[static_cast<size_t>(ChanOp::Warp)] = chan_pc_args<c46_receive_warp>;
    t[static_cast<size_t>(ChanOp::WarpTo)] = chan_pc_s<c46_receive_warp_to>; // card 146
    t[static_cast<size_t>(ChanOp::Objective)] = c49_receive;
    t[static_cast<size_t>(ChanOp::Weapons)] = chan_pc_s<c50_receive>; // card 050
    t[static_cast<size_t>(ChanOp::PeerWeapons)] = chan_pc_s<c50_peer_receive>; // card 050 b205: other players
    t[static_cast<size_t>(ChanOp::Reload)] = chan_pc_s<c143_receive_reload>; // card 143
    t[static_cast<size_t>(ChanOp::Activate)] = chan_pc_s<c65_receive_activate>; // card 065
    t[static_cast<size_t>(ChanOp::Ammo)] = chan_pc_s<c71_receive>; // card 071
    t[static_cast<size_t>(ChanOp::AmmoSet)] = chan_pc_s<c72_receive_set>; // card 072
    t[static_cast<size_t>(ChanOp::ObjectiveActors)] = chan_s<c70_receive>; // card 070
    t[static_cast<size_t>(ChanOp::GrenadeItem)] = chan_pc_s<c73_receive>; // card 073
    t[static_cast<size_t>(ChanOp::ScriptWrite)] = chan_pc_s<c310_receive>; // card 310
    t[static_cast<size_t>(ChanOp::Radio)] = chan_pc_s<c322_receive>; // card 322 part A
    t[static_cast<size_t>(ChanOp::TravelIds)] = chan_pc_s<c397_receive>; // card 397
    t[static_cast<size_t>(ChanOp::HurtSound)] = chan_pc_s<c455_receive>; // card 455
    t[static_cast<size_t>(ChanOp::MechFuel)] = chan_pc_s<c459_receive>; // card 459
    t[static_cast<size_t>(ChanOp::Score)] = chan_pc_s<c475_receive>; // card 475
    t[static_cast<size_t>(ChanOp::DoorOp)] = chan_pc_s<c95_receive>; // card 095
    t[static_cast<size_t>(ChanOp::CollisionOp)] = chan_pc_s<c95_receive_collision>; // card 095
    t[static_cast<size_t>(ChanOp::WorldOp)] = chan_pc_s<c98_receive_world>; // card 098
    t[static_cast<size_t>(ChanOp::Digest)] = chan_pc_s_timed<c132_receive, C252Class::DigestRecv>; // card 132
    t[static_cast<size_t>(ChanOp::LedgerAck)] = chan_pc_arg0<c132_ledger_ack>;
    t[static_cast<size_t>(ChanOp::Hit)] = chan_pc_s<c132_receive_hit>; // card 132 hit blood
    t[static_cast<size_t>(ChanOp::Marker)] = chan_pc_s<c166_receive>; // card 166 shooter hit and kill markers
    t[static_cast<size_t>(ChanOp::Pose)] = chan_pc_s_timed<c132_receive_pose, C252Class::PoseApply>; // card 132 corpse poses
    t[static_cast<size_t>(ChanOp::Decal)] = chan_pc_s<c132_receive_decal>; // card 132 gore decals
    t[static_cast<size_t>(ChanOp::GibPart)] = chan_pc_s<c137_receive_gib>; // card 137 gib parts
    t[static_cast<size_t>(ChanOp::GibPose)] = chan_pc_s<c137_receive_gib_pose>; // card 519 gib part poses (binary)
    t[static_cast<size_t>(ChanOp::Clock)] = chan_pc_s<pl_clock_client>; // card 155
    t[static_cast<size_t>(ChanOp::Dropship)] = chan_pc_s<c132_dropship_receive>; // card 132 troop dropship states
    t[static_cast<size_t>(ChanOp::Fx)] = chan_pc_s_timed<c136_receive_fx, C252Class::FxApply>; // card 136 host effects
    t[static_cast<size_t>(ChanOp::TacCharge)] = chan_pc_s<c104_receive_charge>; // card 104
    t[static_cast<size_t>(ChanOp::Difficulty)] = chan_pc_s<c114_receive>; // card 114
    t[static_cast<size_t>(ChanOp::Stamina)] = chan_pc_arg0<c490_receive>; // card 490
    t[static_cast<size_t>(ChanOp::CombatLevel)] = chan_pc_arg0<c321_level_receive>; // card 321
    t[static_cast<size_t>(ChanOp::Bark)] = chan_pc_s<c451_receive>; // card 451 part 2
    t[static_cast<size_t>(ChanOp::NpcTarget)] = chan_pc_s<c532_receive>; // card 532
    t[static_cast<size_t>(ChanOp::CamFade)] = chan_pc_s<c561_receive>; // card 561 b408
    t[static_cast<size_t>(ChanOp::DropshipBoard)] = chan_pc_s<c601_board_receive>; // card 601
    t[static_cast<size_t>(ChanOp::SwitchPress)] = chan_pc_s<c602_press_receive>; // card 602
    t[static_cast<size_t>(ChanOp::LevelAudio)] = chan_pc_s<c604_receive>;        // card 604
    t[static_cast<size_t>(ChanOp::HordeZone)] = chan_pc_s<c606_receive>;         // card 606
    t[static_cast<size_t>(ChanOp::GrenadeSpawn)] = chan_pc_s<c74_receive>; // card 074
    t[static_cast<size_t>(ChanOp::GrenadeExplode)] = chan_pc_s<c74_receive_explode>; // card 074, card 132 rounds
    t[static_cast<size_t>(ChanOp::HoldDone)] = chan_pc_s<c133_client_arm>; // card 418: the host's satchel fuse start
    return t;
}

constexpr ChanTable kChanHost = chan_host_table();
constexpr ChanTable kChanClient = chan_client_table();

bool c26_is_remote_pc(const void* pc) {
    return sprint_side(pc) == kSideServerCopy;
}

// Card 139, host: the one gate on every live frame to a client. Until the client's first Pong on this map (C50Copy
// ponged, set by c50_first_pong before any join replay runs) it gets nothing but the Ping/Pong exchange that makes
// that Pong and the ledger's acks: its world may not have run BeginPlay yet (card 138 holds it until the pawn is
// there), and what it missed comes from the join replays at the Pong. Copies are emptied at each map load and logout.
const char* c139_client_hold(const void* pc, wchar_t op) {
    const char* const teardown = c332_hold(op); // card 332: nothing goes while the world is torn down
    if (teardown != nullptr) {
        return teardown;
    }
    const auto o = static_cast<ChanOp>(op);
    if (o == ChanOp::Ping || o == ChanOp::Pong || o == ChanOp::LedgerAck) {
        return nullptr;
    }
    const C50Copy* const c = c50_copy(object_index(pc), false);
    return c != nullptr && c->ponged ? nullptr : "not ponged yet (card 139)";
}

void c26_send_skipped(unsigned op, const void* pc, const void* function, int32_t num) {
    log::line("card026: WARN client send of op %u skipped: pc %p, ServerChangeName function %p, num %d", op, pc,
              function, num);
}

// Card 252 step 1b: the ledger's on_recv brackets every received frame's handler (Before, then Ran or NoHandler);
// the span between is the channel class. Receives do not nest (one game thread, handlers do not receive).
void c252_ledger_recv(void* pc, wchar_t op, const ue::chan::ChanStamp& stamp, bool stamped, ue::chan::ChanRecv phase,
                      const WireString& s) {
    static int64_t t0 = 0;
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t); // card 330: always on, the window row's recv part
    if (phase == ue::chan::ChanRecv::Before) {
        t0 = t.QuadPart;
    } else if (t0 != 0) {
        if (g_c132_fx_log) {
            g_c252_ticks[static_cast<size_t>(C252Class::Chan)] += t.QuadPart - t0;
        }
        c330_recv(op, t.QuadPart - t0);
        t0 = 0;
    }
    c132_ledger_recv(pc, op, stamp, stamped, phase, s);
}

constexpr ChanOps kChanOps{kChanArgs,        kChanHost,       kChanClient,      &g_client_session,
                           c26_is_remote_pc, c26_send_skipped, c132_ledger_send, c252_ledger_recv,
                           c139_client_hold, c140_pace_live,   sync_frame_ok,
                           static_cast<wchar_t>(ChanOp::Batch)};

} // namespace ubw::hooks

namespace ubw::chan {

const ue::chan::ChanOps& chan_ops() {
    return ubw::hooks::kChanOps;
}

} // namespace ubw::chan
