#include "ubw/init.h"

#include "autoinput.h"
#include "cmdfile.h"
#include "ue/config.h"
#include "ubw/flags.h"
#include "ubw/hooks_internal.h"
#include "lobby.h"
#include "ue/log.h"
#include "ue/reflect.h"
#include "ue/actor.h"
#include "ue/gas.h"
#include "ue/input.h"
#include "ue/widget.h"
#include "ue/hooktable.h"
#include "ue/net.h"
#include "ue/chan.h"
#include "ubw/chan_ops.h"
#include "offsets.generated.h"

#include <windows.h>

#include <MinHook.h>

#include <array>
#include <intrin.h>
#include <iterator>
#include <math.h>
#include <span>
#include <stdio.h>

#include <stdarg.h>
#include <stdint.h>
#include <string.h>

namespace ubw::hooks {

// --- Auto-travel state ------------------------------------------------------------------------
// Written by the worker thread before the Tick hook is enabled (MH_EnableHook suspends and
// resumes every other thread, which orders these writes); afterwards touched only on the game
// thread inside detour_tick.
ue::config::Travel g_travel{};
bool g_travel_armed = false; // true from install until the travel fires; false if not configured
bool g_world_seen = false;
ULONGLONG g_world_seen_ms = 0;

void travel_step(void* engine) {
    void* const world = g_get_game_world(engine);
    if (world == nullptr) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (!g_world_seen) {
        g_world_seen = true;
        g_world_seen_ms = now;
        log::line("travel: first game world %p, travelling in %u ms", world, g_travel.delay_ms);
        return;
    }
    if (now - g_world_seen_ms < g_travel.delay_ms) {
        return;
    }
    g_travel_armed = false; // at most once per process
    log::line("travel: SetClientTravel(world=%p, \"%ls\", TRAVEL_Absolute)", world, g_travel.url);
    g_set_client_travel(engine, world, g_travel.url, kTravelAbsolute);
    log::line("travel: SetClientTravel returned");
}

bool g_snapshot_armed = false;
uint32_t g_rot_rows_left = 0;
// Card 014: the rot (client) / srv (host) rows open on the first non-mouse key after LoadMap.
bool g_input_window_armed = false;
bool g_pawn_watch_armed = false;
uint32_t g_own_rows_left = 0;
uint32_t g_c20_rows_left = 0;
uint32_t g_c21_camera_logs = 0;     // card 021 step 4, reset per map in reset_card020_map
uint32_t g_c24_rpc_lines = 0;   // card 024 step 1, reset per map in reset_card020_map
uint32_t g_c24_local_lines = 0; // card 024 step 1, reset per map in reset_card020_map
bool g_c40_trace = false; // UBWCOOP_C40_DAMAGE_TRACE; written in ubw::init before any hook
bool g_c42_test = false; // UBWCOOP_C42_TEST
bool g_c43_test = false; // UBWCOOP_C43_TEST
uint32_t g_c43_start_ms = 20000; // UBWCOOP_C43_START_MS
bool g_c45_test = false; // UBWCOOP_C45_TEST
bool g_c46_test = false; // UBWCOOP_C46_TEST
bool g_c50_test = false; // UBWCOOP_C50_TEST (1 or 054)
bool g_c54_test = false; // UBWCOOP_C50_TEST=054: the card 054 steps instead of the card 050 weapon pickup
bool g_c57_solo = false; // UBWCOOP_C57_SOLO
bool g_c65_log = false;   // UBWCOOP_C65_LOG: card 065 step 1 level-trigger log (its detours are installed only then)
uint32_t g_c65_logs = 0;  // card 065 Trigger log lines this map (capped by note_event)
bool g_c68_log = false;   // UBWCOOP_C68_LOG: card 068 minimap chain log (its detours are installed only then)
uint32_t g_c68_logs = 0;  // card 068 event log lines this map (capped by note_event_cap)
bool g_c69_log = false;   // UBWCOOP_C69_LOG: card 069 ammo and reload log (its detours are installed only then)
bool g_c73_log = false;   // UBWCOOP_C73_LOG: card 073 grenade log (the ConsumeItem detour is installed only then)
bool g_c74_log = false;   // UBWCOOP_C74_LOG: card 074 grenade spawn log (its detours are installed only then)
bool g_c136_log = false;    // UBWCOOP_C136_LOG: card 136 host effect event and skip lines
bool g_c132_fx_log = false; // UBWCOOP_C132_FX: card 132 step 2e effect audit (installs the card 074 log detours too)
bool g_c76_log = false;   // UBWCOOP_C76_LOG: card 076 mech fire log (installs the card 074 log detours too)
bool g_c79_log = false;   // UBWCOOP_C79_LOG: card 079 mech destruction log (its detours are installed only then)
bool g_c79_kill = false;  // UBWCOOP_C79_KILL: card 079 test kill of a piloted mech (host)
int g_c79_down = 0;       // UBWCOOP_C79_DOWN: card 079 down test, 0 off, 1 the host's pawn, 2 the client's copy,
                          // 3 the client's copy at a zipline point (card 081)
uint32_t g_c65_activate_logs = 0; // card 065 Activate log lines this map (own cap: load-time activations)
uint32_t g_c65_shunt_logs = 0;    // card 065: client shunt refusal lines this map
bool g_c60_host_own = false; // UBWCOOP_C60_HOST_OWN
bool g_c63_on = false; // UBWCOOP_CMD: card 063 command and state files
bool g_c67_pe = false; // card 067 ProcessEvent gate (UBWCOOP_CMD=1): a mission-brief class is known
bool g_c74_pe = false; // card 074 ProcessEvent gate (UBWCOOP_C74_LOG=1): inside a PlayAnimation task DoAction
bool g_c60_mk1_only = false; // card 060: the host-own stages take a placed MoritaMk1 only (not the case laser)
// Card 062 audit (log only, with UBWCOOP_C50_TEST): the host's own weapon switch around InternalTryActivateAbility.
uint64_t g_c62_instance_weak = 0; // the last switch ability instance activated on the host's own pawn; per map
bool g_c44_pe = false;            // ProcessEvent gate: a death screen may construct
uint32_t g_c44_fires[4] = {};     // FireProjectile calls by sprint side (kSprintSides)
bool g_c39_overkill = false; // UBWCOOP_C39_OVERKILL: card 023's test kill is an overkill
config::SpawnMech g_c32_mode = config::SpawnMech::Off; // card 032, written in ubw::init before any hook
config::GrantWeapon g_grant_weapon{}; // written in ubw::init before any hook
bool g_c21_grant_armed = false;
bool g_dropship_watch_armed = false;
bool g_auto_dropship = false; // UBWCOOP_AUTODROPSHIP; written in ubw::init before any hook
bool g_auto_dropship_armed = false; // host/standalone, per map, while UBWCOOP_AUTODROPSHIP=1
bool g_skip_flight = false;  // UBWCOOP_SKIP_FLIGHT (card 090); written in read_card_flags before any hook
bool g_c17_armed = false;
const char* g_last_hook = "none"; // static strings only: the last cold hook entered (game thread)

// --- Engine detour steps (ue::net::EngineSteps, set in ubw::init) ------------------------------------
// The detours in ue/net.cpp call these at the points where their bodies used to make the same calls.

// After UGameEngine::Init returns.
void ubw_init_return() {
    if (g_c63_on) {
        c64_allow_client_nav(); // card 064: before any game map loads
    }
    c21_mark_health_net(); // before any connection replicates a health set (lobby included)
}

// Hot path: every frame. Idle cost is five loads and five branches.
// The 1 s lines of the optional log-only cards (068, 069, 073); each runs only when its switch is on.
void card_log_steps(void* engine) {
    if (g_c68_log) {
        c68_step(engine);
    }
    if (g_c69_log) {
        c69_step(engine);
    }
    if (g_c73_log) {
        c73_step(engine);
    }
}

// Before the engine Tick.
void ubw_pre_tick(void* engine) {
    if (autoinput::running()) {
        autoinput::tick(engine); // before the engine Tick: the keys are processed in this frame's PC tick
    }
    if (g_c63_on) {
        c63_step(engine); // card 063 test runner: same placement as autoinput
    }
}

// After the engine Tick.
void ubw_post_tick(void* engine) {
    const C252Timer timer(C252Class::Tick); // card 252 step 1b: the mod's per-tick step
    c330_tick_begin(); // card 330 step 1: every step below is timed by C330_STEP
    C330_STEP(c140_pace_step(engine)); // card 140: the chan pace queues drain before any step sends
    C330_STEP(c138_step()); // card 138: first, so the dispatch runs in the tick the pawn arrived
    // card 306: the inert own spawns of this frame, before any digest or relay step reads the world
    C330_STEP(c306_step());
    C330_STEP(c296_tick()); // card 296: the own spool's end row (client)
    C330_STEP(c303_tick()); // card 303: the frame watchdog's stamp and the long-frame row
    if (g_client_session) {
        C330_STEP(c297_tick(engine)); // card 297: the client pilot's arms swap and its rows
        C330_STEP(card018_client_step(engine)); // card 298: the own pawn's attribute re-init, every possession
    }
    C330_STEP(c459_step(engine)); // card 459: piloted mech Fuel sync and (diag) rows, both sides
    C330_STEP(c490_hold_step(engine)); // card 490: the host infinite stamina option, both sides
    if (g_gate_c62_diag) {
        C330_STEP(c490_step(engine)); // card 490 step 1: stamina rows
    }
    if (g_c220_due) {
        C330_STEP(c220_step()); // card 220 (b187): the prompt widget recheck rows
    }
    if (g_gate_c62_diag && !g_client_session) {
        C330_STEP(c453_step(engine)); // card 453 step 1: ASTDoor class rows, once per map
    }
    if (g_pawn_watch_armed) {
        C330_STEP(own_pawn_collision_step(engine));
    }
    if (g_dropship_watch_armed) {
        C330_STEP(dropship_watch_step(engine));
    }
    if (g_auto_dropship_armed) {
        C330_STEP(auto_dropship_step(engine));
    }
    if (g_c17_armed) {
        C330_STEP(card017_step(engine));
    }
    if (g_travel_armed) {
        C330_STEP(travel_step(engine));
    }
    if (g_snapshot_armed) {
        C330_STEP(client_snapshot_step(engine));
    }
    if (g_rot_rows_left != 0) {
        C330_STEP(rotation_report_step(engine));
    }
    if (g_own_rows_left != 0) {
        C330_STEP(own_rows_step(engine));
    }
    if (g_c20_rows_left != 0) {
        C330_STEP(card020_row_step(engine));
    }
    if (g_c21_grant_armed) {
        C330_STEP(c21_grant_step(engine));
    }
    C330_STEP(c21_parity_step(engine));
    C330_STEP(c141_step(engine));
    C330_STEP(c24_step(engine));
    C330_STEP(c25_step(engine));
    if (g_c37.audit) { // card 293 is closed; its marker traces cost about 1 s per C3M2 map on the host (c263L312)
        C330_STEP(c293_step(engine)); // card 293: minimap NPC markers no player can see
    }
    C330_STEP(c319_step(engine)); // card 319: client minimap scan gates, every 5 s
    C330_STEP(c321_step(engine)); // card 321: host begin-play count row, once per map
    C330_STEP(c338_step()); // card 338: the 5 s row after a fall zone reposition
    C330_STEP(c336_step(engine)); // card 336: one row per fall zone, once per map, both sides
    C330_STEP(c294_host_step(engine)); // card 294: the travel row for a host in-place load
    C330_STEP(c27_step(engine));
    C330_STEP(c29_step(engine));
    C330_STEP(c22_step(engine));
    C330_STEP(c28_step(engine));
    C330_STEP(c23_step(engine));
    C330_STEP(c37_step(engine));
    C330_STEP(c41_step(engine));
    C330_STEP(client_significance_step(engine)); // card 132: the client has no game mode to tick it
    C330_STEP(net_budget_step(engine)); // card 132 diagnostic: host bandwidth per class
    C330_STEP(c132_hit_step(engine)); // card 132: the tick's hits to the clients
    C330_STEP(c157_step(engine)); // card 157: client hit claims meet their rounds (host)
    C330_STEP(c161_step(engine)); // card 161: BP door state (host sends changes; both log them)
    C330_STEP(c315_host_step(engine)); // card 315: looping timeline phase rows every 3 s (host)
    C330_STEP(c487_host_step(engine)); // card 487: hide checks for deaths with no Health change (host)
    C330_STEP(c408_step(engine)); // card 408: flak and plasma bug building scan (arms the timer event filter)
    C330_STEP(c417_step()); // card 417: queued own Blueprint pooled launches back to the pool (one frame after)
    // card 159: dropship check rounds and tracker (host), spline-end hold (client)
    C330_STEP(c159_dropship_step(engine));
    C330_STEP(c136_step(engine)); // card 136: the tick's effect events to the clients (host), local pawn (client)
    C330_STEP(c132_pose_step(engine)); // card 132: corpse poses (host sends, client retries pending rows)
    C330_STEP(c132_decal_step(engine)); // card 132: gore decal rows (host) and copy checks (client)
    C330_STEP(c104_settle_step()); // card 132 note 2: relayed pickups' rest rows (host)
    C330_STEP(c287_client_step()); // card 287: relayed bodies blended between host rows (client)
    C330_STEP(c132_ext_step(engine)); // card 132 step 2d: persistent actor state (host polls and sends, client applies)
    C330_STEP(c42_step(engine));
    C330_STEP(c43_step(engine));
    C330_STEP(c45_step(engine));
    C330_STEP(c30_step(engine));
    C330_STEP(c32_step(engine));
    C330_STEP(c31_step(engine));
    C330_STEP(c110_step(engine));
    C330_STEP(c169_step());           // card 324: held visual view row
    C330_STEP(fx_watch_step(engine)); // card 130
    C330_STEP(c132_util_step());      // card 132 item 4a
    C330_STEP(cine_skip_step());      // card 130
    C330_STEP(c111_step(engine));
    C330_STEP(c122_step(engine));
    C330_STEP(c125_step(engine));
    C330_STEP(c114_step(engine));
    {
        const C252Timer digest(C252Class::DigestStep); // card 252 step 1b
        C330_STEP(c132_digest_step(engine)); // card 132
    }
    C330_STEP(c475_step(engine));     // card 323
    C330_STEP(card_log_steps(engine));
    C330_STEP(c151_flush_step()); // card 151: this tick's chan rows go out in batches, last
    c330_tick_end(); // card 330 step 1: the window row when due
}

// Before UEngine::LoadMap's enter line.
// Card 346: the engine of the last map change (seamless or LoadMap), for the GetViewTarget guard's world.
void* g_c346_engine = nullptr;

void ubw_load_map_enter(void* engine, void* pending) {
    g_c346_engine = engine;
    clear_pending_starting_mappings(); // a deferral never outlives the map it was made on
    c138_load_map();                   // card 138: a held BeginPlay dispatch never outlives its world
    c337_load_map_enter(engine); // card 337: the viewport flag outlives the map
    g_last_hook = "LoadMap";
    autoinput::load_map_enter(engine); // held scripted keys are released on the PC of the map being left
    void* const old_world = engine != nullptr ? g_get_game_world(engine) : nullptr;
    c132_inventory(old_world, "map end");  // card 132: old world, host
    c132_ledger_report("map end");         // card 132: both sides
    c132_static_map_end();                 // card 132: both sides
    c157_map_end(); // card 157: claim tables name the old world's projectiles
    c306_map_end(); // card 306: per-map inert spawn tally
    c293_map_end(); // card 293: per-map minimap marker totals
    c319_map_end(); // card 319: per-map minimap scan tally
    c322_map_end(); // card 322: per-map radio, music and dilation tally, both sides
    c361_map_end(); // card 361: per-map caps and crushed pairs, both sides
    c321_map_end(); // card 321: the next map logs its own count row
    c336_map_end(); // card 336: the next map logs its own fall zone rows
    c310_collision_map_end(); // c310 range: per-map KillableBuilding collision row cap, both sides
    c303_map_end(); // card 303: the long-frame tally, both sides
    c330_map_end(old_world); // card 330: per-step time and collect-by-caller rows, both sides
    c495_map_end(); // card 495: the per-player pickup table names the old world's pickups, host
    c227_map_end(); // card 227: the gated presence table names the old world's presences, both sides
    c39_map_end();  // card 200: card 039 probes and gib list name old-world actors
    sync_map_end(old_world); // card 326: every registered state mechanism's tables; card 332 teardown begins
    pl_map_end();   // card 182: playlog pawn, pc, weapon and remote pointers name old pawns, both sides
    c408_map_end(); // card 477: map Blueprint classes are freed with the old map; BeginPlay inside LoadMap tests them
    c161_map_end(); // card 477 sweep: the door class and door table, same reason
    c451_relay_summary(); // card 451 part 2: bark relay tally, both sides
    client_diag_load_map_enter(pending);
}

// After UEngine::LoadMap returns.
void ubw_load_map_return(bool ok, void* pending) {
    client_diag_load_map_return(ok, pending);
    c45_load_map_return(ok, pending);
    c52_load_map_return(ok, pending);
    c337_load_map_return(ok, pending); // card 337: after client_diag set g_client_session
}

// Card 346: a seamless travel's map change runs the LoadMap steps. The pending net game stands for "still in the
// host's session" (a seamless travel never leaves it); the steps only test it against null.
int g_c346_session_tag = 0;

void* c346_pending() {
    return g_client_session ? &g_c346_session_tag : nullptr;
}

void c346_seamless_enter(void* engine) {
    g_c346_engine = engine;
    ubw_load_map_enter(engine, c346_pending());
}

// The own (local) PC of the world, in iterator order; null when none. Card 346: after a seamless travel the host's
// UWorld::GetFirstPlayerController returns the remote old-class PC until the client acks the swap; this does not.
void* own_pc(const void* world) {
    for (int32_t s = 0; s < kC43Max; ++s) {
        void* const pc = g_get_player_controller(world, s);
        if (pc != nullptr && is_local_pc(pc)) {
            return pc;
        }
    }
    return nullptr;
}

GameInstanceLoadCompleteFn g_c375_load_complete = nullptr;

// Card 375 (run c377b64): USTSaveComponent::LoadOwnerVariables_Pawn hands each saved player pawn to the PC at the
// saved index of UWorld::GetPlayerControllerIterator (UWorld+0x268, TArray<TWeakObjectPtr<APlayerController>>).
// A save taken in a mission has the host at index 0. At the seamless return the host's PC was already swapped for a
// new one (appended), while the client's kept PC still waits for its own swap, so the list reads [client, host] and
// the host got the client's saved pawn. Moves the host's entry to slot 0 before the load; host first is also the
// list order of every map the host opened by LoadMap. Returns the slot the host's entry was at, -1 = not found.
constexpr size_t kWorldPlayerControllerList = 0x268;
constexpr int32_t kC375MaxPcs = 8;
int c375_host_pc_first(void* world) {
    const void* const own = own_pc(world);
    auto* const list = static_cast<C31Weak*>(read_ptr(world, kWorldPlayerControllerList));
    const int32_t count = read_i32(world, kWorldPlayerControllerList + 8);
    if (own == nullptr || list == nullptr || count <= 0 || count > kC375MaxPcs) {
        return -1;
    }
    const int32_t own_index = static_cast<int32_t>(object_index(own));
    for (int32_t i = 0; i < count; ++i) {
        if (list[i].index == own_index && list[i].serial != 0) {
            const C31Weak host = list[i];
            list[i] = list[0];
            list[0] = host;
            return i;
        }
    }
    return -1;
}

// Card 375 (run c377b63): a save load from another level (hangar CONTINUE / LOAD GAME) only stores deferred load
// params and travels with ?FromSave; the engine applies them from USTGameInstance::LoadComplete, which UEngine::LoadMap
// calls at its end and a seamless travel (card 346) never does. The host then sat in a parked dropship with no save
// applied. Host only, destination world only (the transition world gets a seamless return too), flag set only: the
// flag-clear branch of LoadComplete is not needed by a fresh seamless mission start.
void c375_seamless_load_complete(void* world) {
    void* const gi = !g_client_session && world != nullptr && g_c375_load_complete != nullptr
                         ? g_get_st_game_instance(world) : nullptr;
    if (gi == nullptr || strcmp(object_name(world).text, "Untitled") == 0) {
        return;
    }
    const uint8_t* const flag = static_cast<const uint8_t*>(gi) + kGameInstanceDeferredLoad;
    const int before = *flag;
    if (before == 0) {
        return;
    }
    const int host_slot = c375_host_pc_first(world);
    const FStringRaw none{nullptr, 0, 0};
    g_c375_load_complete(gi, 0.0F, &none);
    log::line("card375: host t=%llu deferred save load applied after the seamless travel to %s (flag %d -> %d), "
              "host PC was at list slot %d",
              static_cast<unsigned long long>(GetTickCount64()), object_name(world).text, before,
              static_cast<int>(*flag), host_slot);
}

// Card 392 (`c391b78` client crash, AV reading 0x0 at exe+0x510B401 from ASTNonPlayableCharacterBase::Tick
// exe+0x51AB08F): a client's seamless travel keeps every replicated actor it does not own (the engine leaves their
// destroy to the server), so the old map's bugs arrive in the transition world and tick there until the host's
// channel closes reach them. Their tick resolves a weak pointer (+0x520 of an object the bug holds) to a target the
// travel already destroyed and calls through the null result. No kept bug has anything to do: the host destroyed
// them all. Client, transition world only: every NPC there stops ticking.
constexpr size_t kC392SetTickEnabledVfn = 0x4A8; // AActor::SetActorTickEnabled(bool), as turret_sync.cpp

// Card 392 follow-up (c423b173a client crash, the same AV at exe+0x510B401 from the NPC tick, 3 ms after the arrival in
// 3D_Frontend): the kept NPCs are carried a second time, from Untitled into the destination world, and the actor tick
// registration there turns their tick back on (AActor::RegisterActorTickFunctions enables a tick that starts enabled).
// The NPCs stilled in Untitled are remembered here by weak ref and stilled again on the arrival. The arrival step runs
// when FSeamlessTravelHandler::Tick returns (ue/net.cpp), inside UEngine::TickWorldTravel, before UGameEngine::Tick
// ticks the new world, so no actor tick of the destination world runs first. Only remembered NPCs are touched: the
// destination map's own NPCs never were in Untitled.
// Card 528 (c263L315, c263L316): these kept NPCs are also the left map's corpses the client's scan re-saw as first
// sightings (Health <= 0, HandleDying called) in Untitled and in the destination world; no spawn bunch brought them
// (no card434 spawn row after the arm). The table therefore outlives the arrival: it is replaced at the next Untitled,
// and `c392_kept_npc` lets the scan skip them until the host's channel close destroys them.
constexpr uint32_t kC392Kept = 256;
struct C392Kept {
    C31Weak npc[kC392Kept];
    const void* raw[kC392Kept]; // identity only, compared before the weak ref is resolved
    uint32_t n;    // remembered
    uint32_t full; // stilled in Untitled but not remembered (table full)
    bool arrived;  // stilled again at the arrival (the table is kept for card 528)
};
C392Kept g_c392{}; // game thread; filled in Untitled, replaced at the next Untitled

void c392_still(void* npc) {
    using SetTickFn = void (*)(void*, bool);
    reinterpret_cast<SetTickFn>(read_ptr(read_ptr(npc, 0), kC392SetTickEnabledVfn))(npc, false);
}

// Arrival in the destination world: every remembered NPC still alive is stilled again; the table empties.
void c392_still_again(const void* world) {
    if (g_c392.arrived || (g_c392.n == 0 && g_c392.full == 0)) {
        return;
    }
    const void* const npc_class = g_npc_class();
    uint32_t stilled = 0;
    for (uint32_t i = 0; i < g_c392.n && i < kC392Kept; ++i) {
        void* const npc = c31_weak_get(g_c392.npc[i]);
        if (npc != nullptr && is_a(npc, npc_class)) {
            c392_still(npc);
            ++stilled;
        }
    }
    log::line("card392: client t=%llu seamless arrival: %u kept NPCs stilled again in %s (%u remembered, %u gone)",
              static_cast<unsigned long long>(GetTickCount64()), stilled, object_name(world).text, g_c392.n,
              g_c392.n - stilled);
    if (g_c392.full != 0) {
        log::line("WARN card392: client %u kept NPCs not remembered (table of %u full): their tick may run again in %s",
                  g_c392.full, kC392Kept, object_name(world).text);
    }
    g_c392.arrived = true;
}

// Card 533 (c263L342 client crash, AV reading 0x0 at exe+0x510B401, in world, not at travel): the NPC tick calls
// FSTBugProceduralAnimationData::UpdateFromNPC, which reads the NPC movement component's CurrentFloor. When
// CurrentFloor.HitResult.bBlockingHit is set (CMC+0x4F5 bit 0), it resolves the weak HitResult.Component (CMC+0x520)
// and calls GetCollisionObjectType (vfn +0x5D8) on the result without a null check. A bug standing on another bug
// that the client destroyed (net cull, `clear` kill) leaves that weak ref dead, and the call goes through null. Card
// 392 was the same AV from the travel path. The floor result is stale in that case, so the guard clears
// bBlockingHit before the original runs: the anim data then treats the NPC as not standing on a hit, and the CMC
// rewrites CurrentFloor at its next floor find. The function's other +0x520 read (exe+0x510B658) only compares
// pointers. Always on, both sides: a host bug can stand on a bug that was just destroyed too.
constexpr size_t kC533GetMovementVfn = 0x748;   // APawn::GetMovementComponent
constexpr size_t kC533FloorHitFlags = 0x4F5;    // CMC CurrentFloor (0x438) + HitResult (0x10) + bBlockingHit byte (0xAD)
constexpr size_t kC533FloorHitComponent = 0x520; // CMC CurrentFloor.HitResult.Component, FWeakObjectPtr
using C533UpdateFn = void (*)(void* data, const void* npc, float delta);
C533UpdateFn g_orig_c533_update = nullptr;
uint32_t g_c533_cleared = 0; // game thread; logged at 1, 2, 4, 8, ...

void detour_c533_update_from_npc(void* data, const void* npc, float delta) {
    using GetMovementFn = uint8_t* (*)(const void*);
    uint8_t* const mc = (npc != nullptr && g_weak_get != nullptr)
                            ? reinterpret_cast<GetMovementFn>(read_ptr(read_ptr(npc, 0), kC533GetMovementVfn))(npc)
                            : nullptr;
    if (mc != nullptr && (mc[kC533FloorHitFlags] & 1U) != 0U &&
        g_weak_get(mc + kC533FloorHitComponent) == nullptr) {
        mc[kC533FloorHitFlags] = static_cast<uint8_t>(mc[kC533FloorHitFlags] & ~1U);
        ++g_c533_cleared;
        if ((g_c533_cleared & (g_c533_cleared - 1U)) == 0U) {
            log::line("card533: t=%llu NPC %s floor component gone, blocking hit cleared before the anim update "
                      "(%u so far)",
                      static_cast<unsigned long long>(GetTickCount64()), object_name(npc).text, g_c533_cleared);
        }
    }
    g_orig_c533_update(data, npc, delta);
}

// Card 528: true for an NPC the client carried out of the left map by seamless travel (card 392's table). Client game
// thread only; a raw pointer match is confirmed through the weak ref, so a reused address is not a false match.
bool c392_kept_npc(const void* actor) {
    for (uint32_t i = 0; i < g_c392.n && i < kC392Kept; ++i) {
        if (g_c392.raw[i] == actor) {
            return c31_weak_get(g_c392.npc[i]) == actor;
        }
    }
    return false;
}

void c392_still_kept_npcs(void* world) {
    if (!g_client_session || world == nullptr || g_npc_class == nullptr) {
        return;
    }
    if (strcmp(object_name(world).text, "Untitled") != 0) {
        c392_still_again(world);
        return;
    }
    g_c392 = C392Kept{};
    const void* const npc_class = g_npc_class();
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = levels != nullptr ? read_i32(world, kWorldLevels + kArrayNum) : 0;
    uint32_t stilled = 0;
    for (int32_t l = 0; l < num_levels && l < kMaxLevels; ++l) {
        auto* const* const actors =
            levels[l] != nullptr ? static_cast<void* const*>(read_ptr(levels[l], kLevelActors)) : nullptr;
        const int32_t num = actors != nullptr ? read_i32(levels[l], kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; a < num && a < kMaxLevelActors; ++a) {
            if (actors[a] == nullptr || !is_a(actors[a], npc_class)) {
                continue;
            }
            c392_still(actors[a]);
            ++stilled;
            if (g_c392.n < kC392Kept) {
                g_c392.raw[g_c392.n] = actors[a];
                c31_weak_set(g_c392.npc[g_c392.n++], actors[a]);
            } else {
                ++g_c392.full;
            }
        }
    }
    log::line("card392: client t=%llu seamless travel: %u kept NPCs in the transition world, tick off",
              static_cast<unsigned long long>(GetTickCount64()), stilled);
}

void c346_seamless_return() {
    ubw_load_map_return(true, c346_pending());
    void* const world = g_c346_engine != nullptr ? g_get_game_world(g_c346_engine) : nullptr;
    c392_still_kept_npcs(world); // card 392: Untitled stills and remembers; the next arrival stills them again
    c375_seamless_load_complete(world);
    // Card 349: any PC in the world will do (net mode only); not the local PC.
    void* const pc = world != nullptr ? g_world_first_pc(world) : nullptr;
    void* const own = world != nullptr ? own_pc(world) : nullptr;
    log::line("card346: %s t=%llu seamless map change done: world %s, net mode %d, own PC %p class %s camera manager %p",
              side(), static_cast<unsigned long long>(GetTickCount64()),
              world != nullptr ? object_name(world).text : "none", pc != nullptr ? static_cast<int>(net_mode(pc)) : -1,
              own, class_name(own).text, own != nullptr ? read_ptr(own, kPcCameraManager) : nullptr);
}

// Card 346 (b31 crash, c346 run 1): UADLevelStreamingSubsystem::ProcessVolumes (0x14455F4B0, from OnWorldBeginPlay and
// its Tick) calls GetViewTarget on the world's first PC's camera manager (PC+0x360) with no null check. On the host's
// seamless arrive at 3D_Frontend that manager was null (AV reading 0xC20). A null `this` returns null: ProcessVolumes
// takes a null view target to its exit (je 0x14455FBB9) and reads nothing from it; its Tick runs it again. The first hit
// per world logs every PC so the log says whose manager was missing.
constexpr size_t kC346ObjectFlags = 0x8; // UObjectBase::ObjectFlags; bit 30 = being destroyed (see anim_c104.cpp)
constexpr uint32_t kC346DyingBit = 30;
using GetViewTargetFn = void* (*)(const void* camera);
GetViewTargetFn g_orig_c346_view_target = nullptr;
struct C346Guard {
    const void* world; // world of the last logged hit
    uint32_t hits;     // null-this calls this process
};
C346Guard g_c346_guard{};

void c346_guard_rows(const void* world, unsigned long long caller) {
    log::line("WARN card346: %s t=%llu guard: GetViewTarget on a null camera manager (caller exe+0x%llX), world %s, "
              "%u hits so far; PCs in iterator order:",
              side(), static_cast<unsigned long long>(GetTickCount64()), caller, object_name(world).text,
              g_c346_guard.hits);
    for (int32_t s = 0; s < kC43Max; ++s) {
        void* const pc = g_get_player_controller(world, s);
        if (pc == nullptr) {
            continue;
        }
        const uint32_t flags = static_cast<uint32_t>(read_i32(pc, kC346ObjectFlags));
        log::line("card346: %s guard PC %d %p class %s Player %p camera manager %p local %d pending kill %u (flags 0x%08X)",
                  side(), s, pc, class_name(pc).text, read_ptr(pc, kPcPlayer), read_ptr(pc, kPcCameraManager),
                  is_local_pc(pc) ? 1 : 0, (flags >> kC346DyingBit) & 1u, flags);
    }
}

void* detour_c346_view_target(const void* camera) {
    if (camera != nullptr) {
        return g_orig_c346_view_target(camera);
    }
    ++g_c346_guard.hits;
    void* const world = g_c346_engine != nullptr ? g_get_game_world(g_c346_engine) : nullptr;
    if (world != nullptr && world != g_c346_guard.world) {
        g_c346_guard.world = world;
        c346_guard_rows(world, exe_rva(_ReturnAddress()));
    }
    return nullptr;
}

// The step table the engine detours call (passed to ue::net in ubw::init). Browse and Listen need no
// wrapper: card 045's and card 052's handlers already have the step signatures.
constexpr EngineSteps kEngineSteps{.init_return = ubw_init_return,
                                   .pre_tick = ubw_pre_tick,
                                   .post_tick = ubw_post_tick,
                                   .browse_enter = c45_browse_enter,
                                   .browse_return = c45_browse_return,
                                   .load_map_enter = ubw_load_map_enter,
                                   .load_map_return = ubw_load_map_return,
                                   .listen_return = c52_listen,
                                   .seamless_enter = c346_seamless_enter,
                                   .seamless_return = c346_seamless_return};

// SetupInputComponent's hit-path loop: AddMappingContext for every context of the config.
// Returns the number added, or -1 if the config did not load or is not a mappable config (the
// original dereferences null there; this skips instead).
// The Contexts map of a soft UPlayerMappableInputConfig, loaded. False when it does not load as one (logged, naming
// `what`) or the map is out of bounds.
bool load_config_contexts(const void* soft_config, const char* what, SparseView& v) {
    void* const config = g_load_synchronous(soft_config);
    void* const slot = config;
    if (config == nullptr || !g_object_ptr_is_a(&slot, g_mappable_config_class())) {
        log::line("ERROR client-input: %s %p did not load as UPlayerMappableInputConfig", what, config);
        return false;
    }
    return sparse_view(static_cast<const uint8_t*>(config) + kConfigContexts, kMaxConfigContexts, v);
}
// Allocated element i of a Contexts view: its context, with the TObjectPtr read barrier the natives apply.
const void* config_context(const SparseView& v, int32_t i, int32_t* priority) {
    const uint8_t* const e = v.data + static_cast<size_t>(i) * kConfigContextStride;
    const void* const context = *reinterpret_cast<const void* const*>(e);
    if (*g_gc_barrier_flag != 0 && context != nullptr) {
        g_mark_as_reachable(context);
    }
    *priority = *reinterpret_cast<const int32_t*>(e + 0x8);
    return context;
}
bool is_net_client(const void* actor) {
    return (g_actor_net_mode(actor) & 0xFFu) == kNetModeClient;
}
// Card 009: any PC with a ULocalPlayer, i.e. the client's own PC and the host's (or standalone)
// local PC; the host's remote-client PCs have a UNetConnection and are skipped.
bool is_local_pc(const void* pc) {
    return g_get_local_player(pc) != nullptr;
}
const char* session_side() {
    return g_client_session ? "client" : "host/standalone";
}
void reset_move_diag_row() {
    MoveDiag& d = g_move_diag;
    const MoveDiag keep = d;
    d = MoveDiag{};
    d.updated_is_root = keep.updated_is_root;
    d.mobility = keep.mobility;
    d.have_end = keep.have_end;
    d.adjust_seen = keep.adjust_seen;
    d.last_end = keep.last_end;
}
const char* side_text() {
    return g_client_session ? "client" : "host/standalone";
}
void reset_actions() {
    g_actions = ActionTable{};
    memcpy(g_actions.other.text.text, "(other: table full)", sizeof("(other: table full)"));
}
const char* side() {
    return g_client_session ? "client" : "host";
}
// Axis tags fire every frame they are held and would spend the cap before the fire steps; they are not logged.
bool c21_is_axis_tag(const char* name) {
    return strncmp(name, "InputTag.Move", 13) == 0 || strcmp(name, "InputTag.Look") == 0 ||
           strcmp(name, "InputTag.Walk") == 0; // Move, MoveUp; held axes fire every frame
}
uint32_t sprint_side(const void* actor) {
    if (actor == nullptr) {
        return kSideOther;
    }
    const auto* const a = static_cast<const uint8_t*>(actor);
    if (a[kActorLocalRole] == kRoleAuthority) {
        return a[kActorRemoteRole] == kRoleAutonomousProxy ? kSideServerCopy : kSideHostOwn;
    }
    return a[kActorLocalRole] == kRoleAutonomousProxy ? kSideClientOwn : kSideOther;
}
// Card 015: EnterDropship (0x051C56C0) attaches the own pawn to the dropship and turns its
// collision off on purpose; DescendToLandCompleted (0x051C2D10) detaches it and turns collision back
// on at +0xC8, unless it returns early (+0x749 == 0 or no ST PC). The pawn is held while its attach
// parent is a transport dropship with the player-inside flag set; the guard leaves it alone then.
// Card 399: a rider (the server copy of another player's pawn, or the client's own pawn) attached to a transport
// dropship is held without the flag: the game sets it for player 0 only, and the client proxy never sets it.
const uint8_t* holding_dropship(const uint8_t* pawn) {
    const auto* const parent = static_cast<const uint8_t*>(g_get_attach_parent_actor(pawn));
    if (parent == nullptr || !is_a(parent, g_transport_dropship_class())) {
        return nullptr;
    }
    const uint32_t side = sprint_side(pawn);
    const bool rider = side == kSideServerCopy || side == kSideClientOwn;
    return parent[kDropshipPlayerInside] != 0 || rider ? parent : nullptr;
}
// Yaw in degrees of the component's world rotation quaternion (FQuat::Rotator's formula).
double component_yaw(const void* component) {
    double q[4] = {}; // X, Y, Z, W
    memcpy(q, static_cast<const uint8_t*>(component) + kComponentRotation, sizeof(q));
    return atan2(2.0 * (q[3] * q[2] + q[0] * q[1]), 1.0 - 2.0 * (q[1] * q[1] + q[2] * q[2])) * kRadToDeg;
}
// Card 206: the component's world rotation as an FRotator (pitch, yaw, roll in degrees): FQuat::Rotator's formula,
// gimbal-lock branches included.
Vec3 component_rotator(const void* component) {
    double q[4] = {}; // X, Y, Z, W
    memcpy(q, static_cast<const uint8_t*>(component) + kComponentRotation, sizeof(q));
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    constexpr double kSingularity = 0.4999995;
    const double test = z * x - w * y;
    Vec3 r{};
    r.y = atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z)) * kRadToDeg;
    if (test < -kSingularity || test > kSingularity) {
        const double sign = test < 0.0 ? -1.0 : 1.0;
        r.x = 90.0 * sign;
        r.z = fmod(sign * r.y - 2.0 * atan2(x, w) * kRadToDeg, 360.0); // FRotator::NormalizeAxis below
    } else {
        r.x = asin(2.0 * test) * kRadToDeg;
        r.z = atan2(-2.0 * (w * x + y * z), 1.0 - 2.0 * (x * x + y * y)) * kRadToDeg;
    }
    r.z = r.z > 180.0 ? r.z - 360.0 : (r.z <= -180.0 ? r.z + 360.0 : r.z);
    return r;
}
// UBWCOOP_CLIENT_GAMEONLY fallback: what UWidgetBlueprintLibrary::SetInputMode_GameOnly does
// (construct FInputModeGameOnly, APlayerController::SetInputMode), for the client's own PC,
// once per client map load. Called from AcknowledgePossession, i.e. once the client has its pawn.
void client_force_game_only(void* pc) {
    alignas(8) uint8_t mode[kInputModeGameOnlyBytes] = {};
    g_input_mode_game_only_ctor(mode); // vtable + bConsumeCaptureMouseDown = 1
    void* const world = g_actor_get_world(pc);
    const auto* const viewport =
        world != nullptr ? static_cast<const uint8_t*>(g_world_game_viewport(world)) : nullptr;
    const unsigned before = viewport != nullptr ? viewport[kViewportCaptureMode] : 0xFFu;
    g_orig_set_input_mode(pc, mode); // the trampoline: the hooked entry would log it a second time
    // ~FInputModeGameOnly only rewrites the vtable pointer (see kInputModeGameOnlyBytes): skipped.
    log::line("client-input: UBWCOOP_CLIENT_GAMEONLY: SetInputMode(pc=%p, GameOnly); viewport %p capture %u -> %u, "
              "lock %u, bShowMouseCursor %u",
              pc, static_cast<const void*>(viewport), before, viewport != nullptr ? viewport[kViewportCaptureMode] : 0xFFu,
              viewport != nullptr ? viewport[kViewportLockMode] : 0xFFu, cursor_bit(pc));
}
// FGameplayAttribute::AttributeName: FString at +0 (data, num including the terminator).
ObjectName attribute_name(const void* attribute) {
    constexpr int32_t kAttrNameMax = static_cast<int32_t>(kNameChars) - 1;
    ObjectName out{};
    const auto* const text = static_cast<const wchar_t*>(read_ptr(attribute, 0));
    const int32_t num = read_i32(attribute, 8);
    const int32_t n = (text == nullptr || num <= 1) ? 0 : (num - 1 < kAttrNameMax ? num - 1 : kAttrNameMax);
    for (int32_t i = 0; i < n; ++i) {
        const wchar_t c = text[i];
        out.text[i] = (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?';
    }
    out.text[n] = '\0';
    return out;
}
// The exe-relative return addresses of the hooked function's caller chain (frames 1..depth
// above the detour), formatted into `out`. RtlCaptureStackBackTrace is a KERNEL32 export.
void caller_chain(char* out, size_t cap, uint32_t depth) {
    void* frames[kMaxStackFrames + 1] = {};
    const DWORD want = (depth < kMaxStackFrames ? depth : kMaxStackFrames) + 1;
    const USHORT n = RtlCaptureStackBackTrace(1, want, frames, nullptr);
    size_t used = 0;
    out[0] = '\0';
    for (USHORT i = 1; i < n && used < cap; ++i) { // frame 0 is inside the detour; 1 is its caller
        const int w = snprintf(out + used, cap - used, "%s0x%llX", i == 1 ? "" : " < ", exe_rva(frames[i]));
        if (w < 0) {
            return;
        }
        used += static_cast<size_t>(w);
    }
}
void start_card020_rows() {
    g_c20 = C20State{};
    g_c20_rows_left = kC20Rows;
    g_c20.next_ms = GetTickCount64();
}
// The camera component's world location of an on-foot player character, or zeros.
Vec3 camera_location(const void* pawn) {
    const void* const cam = (pawn != nullptr && is_a(pawn, g_player_character_class()))
                                ? read_ptr(pawn, kCharacterCamera)
                                : nullptr;
    return cam != nullptr ? read_vec3(cam, kComponentTranslation) : Vec3{};
}
// The interactor's current target as its UObject (through the interface's own getter), or null.
// The UObject behind an IADInteractableInterface pointer (null stays null).
const void* iface_object(const void* iface) {
    if (iface == nullptr) {
        return nullptr;
    }
    const auto fn = reinterpret_cast<IfaceGetObjectFn>((*static_cast<void* const* const*>(iface))[kIfaceGetObject]);
    return fn(iface);
}
const void* interact_target(const void* interactor) {
    return iface_object(interactor != nullptr ? read_ptr(interactor, kInteractorTarget) : nullptr);
}

// The weapon DataTable, or null. The engine is the game instance's outer (UGameInstance::GetEngine).
const void* c21_weapon_table(const void* game_instance) {
    const void* const engine = game_instance != nullptr ? read_ptr(game_instance, kObjectOuter) : nullptr;
    const auto* const sub = static_cast<const uint8_t*>(engine != nullptr ? g_get_data_table_subsystem(engine) : nullptr);
    const void* const table = sub != nullptr ? g_weak_get(sub + kDataTableSubsystemWeapons) : nullptr;
    if (table == nullptr) {
        log::line("WARN card021: weapon DataTable not found (engine %p, subsystem %p)", engine, sub);
    }
    return table;
}
const uint8_t* c21_row_map(const void* table) {
    const auto get_row_map = reinterpret_cast<GetRowMapFn>(read_ptr(read_ptr(table, 0), kDataTableGetRowMap));
    return get_row_map(table);
}
// The tag of the weapon row named `row`, or null. Row elements: FName key at +0, row pointer at +8.
const uint8_t* c21_row_tag(const void* table, const char* row) {
    const uint8_t* const map = c21_row_map(table);
    const int32_t n = array_num(map, 0);
    const auto* const data = static_cast<const uint8_t*>(read_ptr(map, kArrayData));
    for (int32_t i = 0; i < n && i < kC21RowsListed && data != nullptr; ++i) {
        const uint8_t* const e = data + kRowMapElement * static_cast<size_t>(i);
        const auto* const r = static_cast<const uint8_t*>(read_ptr(e, 8));
        if (r != nullptr && strcmp(fname_text(e).text, row) == 0) {
            return r + kWeaponRowTag;
        }
    }
    return nullptr;
}
void* c21_character_ptr(const void* actor, size_t offset) {
    const bool character = actor != nullptr && g_st_character_class != nullptr && is_a(actor, g_st_character_class());
    return character ? read_ptr(actor, offset) : nullptr;
}

// Current Health of a character's health set, or < 0 when it has none or is not an ASTCharacter.
float c21_health(const uint8_t* pawn, void** component_out) {
    void* const component = c21_character_ptr(pawn, kCharacterHealthComponent);
    const void* const set = component != nullptr ? read_ptr(component, kHealthComponentSet) : nullptr;
    *component_out = component;
    return set != nullptr ? read_f32(set, kHealthSetHealth + kAttrDataCurrent) : -1.0f;
}
// The rule shared with card 037: repoint `component` only when its ASC's SpawnedAttributes holds exactly one health
// set and that set is not the component's. Returns the adopted set, else nullptr; `own_out` gets the set before.
const void* c21_adopt_set(uint8_t* component, const void** own_out) {
    const void* const asc = component != nullptr ? read_ptr(component, kHealthComponentAsc) : nullptr;
    const void* const own = component != nullptr ? read_ptr(component, kHealthComponentSet) : nullptr;
    *own_out = own;
    if (asc == nullptr || own == nullptr) {
        return nullptr;
    }
    int32_t found = 0;
    const void* const other = c21_spawned_health_set(asc, own, &found);
    if (other == nullptr || found != 1) {
        return nullptr; // own is already the spawned set, the replicated one is not here yet, or the choice is ambiguous
    }
    memcpy(component + kHealthComponentSet, &other, sizeof(other));
    return other;
}
const uint8_t* c21_spec_for(const void* asc, int32_t handle) {
    const uint8_t* const items = static_cast<const uint8_t*>(read_ptr(asc, kAscSpecs));
    const int32_t num = read_i32(asc, kAscAbilityNum);
    if (items == nullptr || num <= 0 || num > kC20MaxSpecs * 4) {
        return nullptr;
    }
    for (int32_t i = 0; i < num; ++i) {
        const uint8_t* const spec = items + static_cast<size_t>(i) * kSpecBytes;
        if (read_i32(spec, kSpecHandle) == handle) {
            return spec;
        }
    }
    return nullptr;
}
// One line for a server copy's pawn-ASC RPC body (host only; anything else is not logged).
void c24_log_rpc(const char* what, const void* asc, int32_t handle, int32_t extra) {
    const void* const owner = asc != nullptr ? read_ptr(asc, kComponentOwner) : nullptr;
    if (g_client_session || sprint_side(owner) != kSideServerCopy ||
        !note_event_cap(g_c24_rpc_lines, kC24RpcLines, "card024 server rpc")) {
        return;
    }
    const uint8_t* const spec = c21_spec_for(asc, handle);
    log::line("card024: host server-copy %s handle %d (%d) ability %s: ActiveCount %d, InputPressed %d", what, handle,
              extra, class_name(spec != nullptr ? read_ptr(spec, kSpecAbility) : nullptr).text,
              spec != nullptr ? static_cast<int>(spec[kSpecActiveCount]) : -1,
              spec != nullptr ? static_cast<int>(spec[kSpecFlags] & 1u) : -1);
}
// Card 175, client: a primary press marks the client-own round count; its release returns the rounds fired since,
// in bits 8-14 (the host holds its copy's release until the copy has fired as many). The press is sent before the
// original AbilityInputTagPressed. The release is read after the engine Tick of its frame (card 178,
// c178_defer_release), so a round the fire ability fires in that frame after the release counts too.
uint32_t c175_press_rounds(bool on) {
    const uint32_t fired = g_c44_fires[kSideClientOwn];
    if (on) {
        g_c24.press_rounds = fired;
        return 0u;
    }
    const uint32_t n = fired - g_c24.press_rounds;
    return (n < kFireRoundsMax ? n : kFireRoundsMax) << kFireRoundsShift;
}
// Sends one Fire edge of a fire kind: the client to the host, the host to every remote PC. world_context is any
// object in the world.
void c26_send_fire(const void* world_context, void* own_pc, int32_t kind, bool on, const char* why) {
    if (c178_defer_release(own_pc, kind, on, why)) {
        return; // card 178: sent by c178_flush_release after this frame's engine Tick
    }
    const uint32_t arg = c24_fire_arg(kind, on) | (g_client_session && kind == kFireAds ? c160_client_ads_bits(world_context) : 0u) |
                         (g_client_session && kind == kFirePrimary ? c175_press_rounds(on) : 0u);
    uint32_t sent = 0;
    if (g_client_session) {
        sent = chan_to_host(own_pc, ChanOp::Fire, arg) ? 1 : 0;
    } else {
        for (int s = 0; s < kC17Slots; ++s) {
            sent += chan_to_client(g_get_player_controller(world_context, s), ChanOp::Fire, arg) ? 1 : 0;
        }
    }
    if (sent != 0) {
        c26_edge_line(g_client_session ? "client sent Fire" : "host sent Fire", arg, why);
    }
}
constexpr uint32_t kC352ClientLines = 40; // card 352: client refused press rows per run
// Hook body for AbilityInputTagPressed/Released, both sides: one frame per press edge and one per release edge.
void c26_own_fire(const void* asc, const void* tag, bool pressed) {
    if (pressed) {
        c196_note_bug_press(asc, tag);
    }
    int32_t kind = kFirePrimary;
    const void* const pawn = c26_own_fire_pawn(asc, tag, &kind);
    if (pawn == nullptr) {
        return;
    }
    C26Own& own = g_c24.own[kind];
    if (pressed == own.held && (!pressed || own.pawn_id == object_index(pawn))) {
        own.refused = own.refused && pressed;
        return; // held: the pressed call repeats every frame
    }
    // Card 352: the game's own fire does not start while WeaponSystem.Blocked is on the pawn (a held support item),
    // so the press is not sent. own.held stays false: a press still held when the tag goes is sent then, as the game
    // starts fire then.
    const int32_t blocked = pressed && g_client_session ? c352_blocked(pawn, kind) : 0;
    if (blocked > 0) {
        if (!own.refused && note_event_cap(g_c24.c352_lines, kC352ClientLines, "card352 client refuse")) {
            log::line("card352: client t=%llu %s press not sent: WeaponSystem.Blocked %d on own pawn %s",
                      static_cast<unsigned long long>(GetTickCount64()), c24_kind_text(kind, true), blocked,
                      object_name(pawn).text);
        }
        own.refused = true;
        return;
    }
    if (own.refused && pressed) {
        log::line("card352: client t=%llu %s press sent: WeaponSystem.Blocked gone while held",
                  static_cast<unsigned long long>(GetTickCount64()), c24_kind_text(kind, true));
    }
    own.refused = false;
    own.held = pressed;
    own.pawn_id = object_index(pawn);
    if (kind == kFirePrimary && g_client_session) {
        c296_input(pressed); // card 296: the press and release times for the spool row
    }
    if (kind == kFireAds) {
        pl_ads_set(pressed ? 1 : 0); // card 160: the ADS edge, with the camera FOV now and once settled
    }
    c26_send_fire(pawn, read_ptr(pawn, kPawnController), kind, pressed,
                  pressed ? "press" : "AbilityInputTagReleased");
}
// Host: the server-side weapon ASC of a player pawn's (or a turret's) equipped weapon, or null.
// The weapon inventory of a player pawn, or (card 125) of a mounted turret (STMountableTurret.WeaponInventory).
// Card 196: true when `object`'s class or one of its supers has the native name `name` (no U/A prefix). The bug pawn
// and UADWeaponInventory have no StaticClass symbol here, so the class is matched by name.
bool c196_class_named(const void* object, const char* name) {
    const void* cls = object != nullptr ? read_ptr(object, kObjectClass) : nullptr;
    for (int depth = 0; depth < kPropMaxSupers && cls != nullptr; ++depth, cls = read_ptr(cls, kStructSuper)) {
        if (strcmp(object_name(cls).text, name) == 0) {
            return true;
        }
    }
    return false;
}
size_t c196_inventory_at(const void* pawn) {
    if (is_a(pawn, g_player_mi_class())) {
        return kMiWeaponInventory;
    }
    const bool bug = is_a(pawn, g_player_character_class()) && c196_class_named(pawn, "STPlayerCharacterBug");
    return bug ? kBugFormsInventory : 0;
}
constexpr uint32_t kC196InventoryWarns = 8; // per run
uint32_t g_c196_inventory_warns = 0;        // game thread
// Card 196: a player pawn's (MI or bug) weapon-class inventory, or (card 125) a mounted turret's.
const void* c24_inventory(const void* pawn) {
    if (is_a(pawn, g_turret_class())) {
        return read_ptr(pawn, kTurretWeaponInventory);
    }
    const size_t at = c196_inventory_at(pawn);
    const void* const inventory = at != 0 ? read_ptr(pawn, at) : nullptr;
    if (inventory == nullptr || c196_class_named(inventory, "ADWeaponInventory")) {
        return inventory;
    }
    if (note_event_cap(g_c196_inventory_warns, kC196InventoryWarns, "card196 inventory class")) {
        log::line("WARN card196: %s %s (%s) +0x%zX holds %s (%s), not a UADWeaponInventory; no inventory", side(),
                  object_name(pawn).text, class_name(pawn).text, at, object_name(inventory).text,
                  class_name(inventory).text);
    }
    return nullptr;
}
void* c24_weapon_asc(const void* pawn, const void** weapon_out) {
    const void* const inventory = c24_inventory(pawn);
    const void* const weapon = inventory != nullptr ? g_get_equipped_weapon(inventory) : nullptr;
    *weapon_out = weapon;
    return weapon != nullptr ? read_ptr(weapon, kWeaponAsc) : nullptr;
}
const void* weapon_pawn(const void* actor) {
    if (actor == nullptr) {
        return nullptr;
    }
    const void* const player = g_player_character_class != nullptr ? g_player_character_class() : nullptr;
    const void* const weapon_cls = g_c50_weapon_class != nullptr ? g_c50_weapon_class() : nullptr;
    if (player != nullptr && weapon_cls != nullptr && is_a(actor, weapon_cls)) {
        const void* const inventory = read_ptr(actor, kWeaponOwnerInventory);
        const void* const pawn = inventory != nullptr ? read_ptr(inventory, kComponentOwner) : nullptr;
        if (is_a(pawn, player)) {
            return pawn;
        }
    }
    return read_ptr(actor, kActorOwner);
}
// The view point of `pawn`'s controller, as a location and a unit forward vector. False with no controller.
bool c24_view_point(const void* pawn, ViewPoint& out) {
    const void* const pc = read_ptr(pawn, kPawnController);
    if (pc == nullptr || g_player_view_point == nullptr) {
        return false;
    }
    Vec3 rot{};
    g_player_view_point(pc, &out.loc, &rot);
    const double p = rot.x / kRadToDeg;
    const double y = rot.y / kRadToDeg;
    out.dir = Vec3{cos(p) * cos(y), cos(p) * sin(y), sin(p)};
    return true;
}
// One line trace from `start` to `end`, ignoring `pawn`: `call(world, hit, params)` runs the engine trace. Returns the
// blocking hit location, or `end` with no block. inside_out (card 328): the blocking hit started inside the body
// (FHitResult bStartPenetrating); false with no block. The params ctor may allocate its ignore arrays; they are freed here
// exactly as the original frees them.
template <class TraceCall>
Vec3 trace_run(const void* pawn, const Vec3& end, const void** actor_out, const void** comp_out, bool* inside_out,
               TraceCall call) {
    alignas(16) uint8_t hit[kHitSize] = {};
    alignas(16) uint8_t params[kParamsSize] = {};
    const float one = 1.0F;
    const int32_t none = -1;
    memcpy(hit + kHitTime, &one, sizeof(one));
    memcpy(hit + kHitItem, &none, sizeof(none));
    memcpy(hit + kHitMyItem, &none, sizeof(none));
    const uint64_t stat_id = 0;
    g_query_params_ctor(params, 0, &stat_id, true, pawn);
    // The original ignores the return value and reads bBlockingHit; so does this.
    call(g_actor_get_world(pawn), hit, params);
    void* const heap_b = read_ptr(params, kParamsHeapB);
    void* const heap_a = read_ptr(params, kParamsHeapA);
    if (heap_b != nullptr) {
        g_memory_free(heap_b);
    }
    if (heap_a != nullptr) {
        g_memory_free(heap_a);
    }
    const bool blocked = (hit[kHitFlags] & 1U) != 0;
    if (inside_out != nullptr) {
        *inside_out = blocked && (hit[kHitFlags] & 2U) != 0; // bit 1 bStartPenetrating (ue/actor.h kHitFlags)
    }
    if (actor_out != nullptr || comp_out != nullptr) { // card 264: the hit component (FHitResult Component weak
        // pointer, null when freed: the weak serial check) and its owner. c264-p1: the HitObjectHandle's first weak
        // pointer resolved to the StaticMeshComponent, not its actor.
        C31Weak w{};
        memcpy(&w, hit + hit_layout().comp, sizeof(w));
        const void* const comp = blocked ? c31_weak_get(w) : nullptr;
        if (actor_out != nullptr) {
            *actor_out = comp != nullptr ? read_ptr(comp, kComponentOwner) : nullptr;
        }
        if (comp_out != nullptr) {
            *comp_out = comp;
        }
    }
    return blocked ? read_vec3(hit, kHitLocation) : end;
}
// A line trace with collision profile `profile` (an FName); see trace_run.
Vec3 trace_profile(const void* pawn, const Vec3& start, const Vec3& end, uint64_t profile, const void** actor_out,
                   const void** comp_out) {
    return trace_run(pawn, end, actor_out, comp_out, nullptr, [&](const void* world, void* hit, const void* params) {
        (void)g_line_trace_by_profile(world, hit, &start, &end, profile, params);
    });
}
// Card 264: a line trace on collision channel `channel` with the channel's default responses; see trace_run.
Vec3 trace_channel(const void* pawn, const Vec3& start, const Vec3& end, int32_t channel, const void** actor_out,
                   const void** comp_out, bool* inside_out) {
    const void* const response = g_exe_base + offsets::FCollisionResponseParams_DefaultResponseParam.rva;
    return trace_run(pawn, end, actor_out, comp_out, inside_out, [&](const void* world, void* hit, const void* params) {
        (void)g_line_trace_by_channel(world, hit, &start, &end, channel, params, response);
    });
}
// The original's trace: profile PlayerProjectile.
Vec3 c24_trace(const void* pawn, const Vec3& start, const Vec3& end) {
    return trace_profile(pawn, start, end, fname_cached(g_c24_aim.profile, L"PlayerProjectile"));
}
// Every NPC in the world's levels; the first kC25MaxNpcs are kept, all are counted.
void c25_collect(const void* world, const Vec3& pawn_loc, const ViewPoint& view, C25List& out) {
    out.count = 0;
    out.total = 0;
    const void* const cls = g_npc_class();
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    for (int32_t l = 0; levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const void* const level = levels[l];
        const auto* const actors = level != nullptr ? static_cast<void* const*>(read_ptr(level, kLevelActors)) : nullptr;
        const int32_t num = level != nullptr ? read_i32(level, kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; actors != nullptr && a < num && a < kMaxLevelActors; ++a) {
            void* const actor = actors[a];
            if (actor == nullptr || !is_a(actor, cls)) {
                continue;
            }
            ++out.total;
            if (out.count < kC25MaxNpcs) {
                out.npc[out.count++] = c25_measure(actor, pawn_loc, view);
            }
        }
    }
}
// The IsDead flag of an NPC's health component, or -1 with no component.
int c25_dead(const void* actor, float* health) {
    void* component = nullptr;
    *health = c21_health(static_cast<const uint8_t*>(actor), &component);
    return component != nullptr ? static_cast<const uint8_t*>(component)[kHealthIsDead] : -1;
}
// Sets the controller's ControlRotation to yaw from `eye` toward `at`, at the card's fixed pitch.
void c25_face(void* pc, const Vec3& eye, const Vec3& at) {
    const double rot[3] = {kC25FacePitch, atan2(at.y - eye.y, at.x - eye.x) * kRadToDeg, 0.0};
    g_orig_set_control_rotation(pc, rot);
}
// Step 6: teleports the pawn to kC25MoveDistance from `target` on the line between them, then faces it.
bool c25_move(void* pawn, void* pc, const Vec3& target) {
    const Vec3 from = actor_location(pawn);
    const double dx = from.x - target.x;
    const double dy = from.y - target.y;
    const double h = sqrt(dx * dx + dy * dy);
    const double ux = h > 1.0 ? dx / h : 1.0;
    const double uy = h > 1.0 ? dy / h : 0.0;
    const Vec3 to{target.x + ux * kC25MoveDistance, target.y + uy * kC25MoveDistance,
                  from.z < target.z ? target.z + kC25MoveAbove : from.z};
    const bool moved = g_set_actor_location(pawn, &to, false, nullptr, kTeleportPhysics);
    const Vec3 now = actor_location(pawn);
    c25_face(pc, now, target);
    log::line("card025: %sMOVE pawn (%.0f, %.0f, %.0f) -> (%.0f, %.0f, %.0f) via AActor::SetActorLocation(sweep 0, "
              "TeleportPhysics) returned %d; pawn now (%.0f, %.0f, %.0f), %.0f from the target; control rotation "
              "pitch %.0f toward it",
              moved ? "" : "WARN ", from.x, from.y, from.z, to.x, to.y, to.z, moved ? 1 : 0, now.x, now.y, now.z,
              vec_dist(now, target), kC25FacePitch);
    return moved;
}
bool c27_map_ok(const uint8_t* field) {
    const auto* const key = static_cast<const uint8_t*>(read_ptr(field, kMapPropKey));
    const auto* const value = static_cast<const uint8_t*>(read_ptr(field, kMapPropValue));
    const int32_t value_offset = read_i32(field, kMapValueOffset);
    const int32_t size = read_i32(field, kMapSize);
    if (!c27_owned(key, field) || !c27_owned(value, field) || read_i32(value, kPropertyOffset) != value_offset) {
        return false;
    }
    return read_i32(field, kMapHashIndex) == read_i32(field, kMapHashNext) + 4 && value_offset > 0 &&
           size >= value_offset + read_i32(value, kPropertyElementSize) && read_i32(field, kMapSparseSize) >= size;
}
bool c27_bit(const C27Sparse& s, int32_t i) {
    return (s.bits[i >> 5] & (1u << (i & 31))) != 0;
}
// The sparse array of a script set/map at `addr`, checked: bit count = slots, set bits = slots - free.
bool c27_sparse(const uint8_t* addr, C27Sparse& s) {
    const void* const secondary = read_ptr(addr, kSparseBitsSecondary);
    s.data = static_cast<const uint8_t*>(read_ptr(addr, 0));
    s.bits = secondary != nullptr ? static_cast<const uint32_t*>(secondary)
                                  : reinterpret_cast<const uint32_t*>(addr + kSparseBitsInline);
    s.slots = read_i32(addr, kArrayNum);
    s.live = 0;
    const int32_t free_slots = read_i32(addr, kSparseNumFree);
    if (s.slots < 0 || s.slots > kC27MaxSparse || read_i32(addr, kSparseNumBits) != s.slots || free_slots < 0 ||
        free_slots > s.slots || (s.slots > 0 && s.data == nullptr) ||
        (s.slots > kSparseInlineBits && secondary == nullptr)) {
        return false;
    }
    for (int32_t i = 0; i < s.slots; ++i) {
        s.live += c27_bit(s, i) ? 1 : 0;
    }
    return s.live == s.slots - free_slots;
}
void c27_write(C27Out& out, const char* fmt, ...) {
    if (out.capped) {
        return;
    }
    if (out.lines + 1 >= kC27MaxLines) {
        char cap_line[96];
        const int n = snprintf(cap_line, sizeof(cap_line), "... output capped at %u lines\n", kC27MaxLines);
        c27_emit(out, cap_line, n > 0 ? static_cast<size_t>(n) : 0);
        out.capped = true;
        return;
    }
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    size_t len = n > 0 ? static_cast<size_t>(n) : 0;
    len = len < sizeof(buf) - 2 ? len : sizeof(buf) - 2;
    buf[len++] = '\n';
    c27_emit(out, buf, len);
    ++out.lines;
}
// The text of a leaf value (not a struct or container).
void c27_text(PropKind kind, const char* kind_name, const uint8_t* field, const uint8_t* addr, char* buf, size_t cap) {
    const int32_t size = read_i32(field, kPropertyElementSize);
    float f = 0.0f;
    double d = 0.0;
    const void* object = nullptr;
    switch (kind) {
    case PropKind::Bool:
        snprintf(buf, cap, "%s", !prop_bool_ok(field) ? "<bool layout check failed>"
                                 : (addr[field[kBoolByteOffset]] & field[kBoolFieldMask]) != 0 ? "true" : "false");
        return;
    case PropKind::Float: memcpy(&f, addr, sizeof(f)); snprintf(buf, cap, "%.9g", static_cast<double>(f)); return;
    case PropKind::Double: memcpy(&d, addr, sizeof(d)); snprintf(buf, cap, "%.9g", d); return;
    case PropKind::SInt: snprintf(buf, cap, "%lld", c27_sint(addr, size)); return;
    case PropKind::UInt: snprintf(buf, cap, "%llu", c27_uint(addr, size)); return;
    case PropKind::Name: snprintf(buf, cap, "%s", prop_name(addr, size).text); return;
    case PropKind::Str: c27_str(addr, buf, cap); return;
    case PropKind::Object:
        object = read_ptr(addr, 0);
        snprintf(buf, cap, "%s (%s)", object_name(object).text, object != nullptr ? class_name(object).text : "-");
        return;
    default: c27_hex(kind_name, addr, size, buf, cap); return;
    }
}
// Known-value checks of the subclass offsets on the asset's own Animations map: its layout and sparse array,
// its struct value (size = aligned PropertiesSize), the sequence's Frames array (inner owned by it) and a bool.
bool c27_verify(const void* asset) {
    static const char* const kBools[] = {"bUseUniformTime", "bUseUniformFrameTime", "bLoop", nullptr};
    const uint8_t* const anims = prop_find_prop(read_ptr(asset, kObjectClass), "Animations");
    C27Sparse s{};
    const bool map_ok = prop_kind_is(anims, "MapProperty") && c27_map_ok(anims) &&
                        c27_sparse(static_cast<const uint8_t*>(asset) + read_i32(anims, kPropertyOffset), s);
    const auto* const value = map_ok ? static_cast<const uint8_t*>(read_ptr(anims, kMapPropValue)) : nullptr;
    const void* const seq = prop_kind_is(value, "StructProperty") ? read_ptr(value, kStructPropStruct) : nullptr;
    const bool struct_ok = c27_struct_ok(value, seq);
    const uint8_t* const frames = struct_ok ? prop_find_prop(seq, "Frames") : nullptr;
    const bool array_ok = prop_kind_is(frames, "ArrayProperty") && c27_owned(read_ptr(frames, kArrayPropInner), frames);
    const uint8_t* const flag = struct_ok ? c27_find_any(seq, kBools) : nullptr;
    const bool bool_ok = prop_kind_is(flag, "BoolProperty") && prop_bool_ok(flag);
    log::line("card027: offset checks on %s: map %s (%d live of %d slots, value +0x%X, pair 0x%X, stride 0x%X), "
              "struct %s (%s, %d bytes), array %s (Frames), bool %s (%s size %u offset %u mask 0x%02X); "
              "set: none known, checked where met",
              object_name(asset).text, map_ok ? "ok" : "FAILED", s.live, s.slots,
              anims != nullptr ? read_i32(anims, kMapValueOffset) : -1, anims != nullptr ? read_i32(anims, kMapSize) : -1,
              anims != nullptr ? read_i32(anims, kMapSparseSize) : -1, struct_ok ? "ok" : "FAILED", object_name(seq).text,
              value != nullptr ? read_i32(value, kPropertyElementSize) : -1, array_ok ? "ok" : "FAILED",
              bool_ok ? "ok" : "FAILED", flag != nullptr ? fname_text(flag + kFieldName).text : "-",
              flag != nullptr ? flag[kBoolFieldSize] : 0u, flag != nullptr ? flag[kBoolByteOffset] : 0u,
              flag != nullptr ? flag[kBoolFieldMask] : 0u);
    return map_ok && struct_ok && array_ok && bool_ok;
}
// The set, already loaded (StaticFindFirstObject) or loaded now. `how` says which.
const void* c29_load_set(const C29Set& set, const char** how) {
    wchar_t wide[200];
    *how = "missing";
    if (swprintf(wide, 200, L"%hs", set.name) <= 0) {
        return nullptr;
    }
    const void* object = g_find_first_object(nullptr, wide, 0, 0, nullptr);
    if (c27_is_sprite_data(object)) {
        *how = "loaded";
        return object;
    }
    if (swprintf(wide, 200, L"/Game/%hs/%hs.%hs", set.dir, set.name, set.name) <= 0) {
        return nullptr;
    }
    object = g_static_load_object(g_sprite_data_class(), nullptr, wide, nullptr, kC29LoadFlags, nullptr, true, nullptr);
    *how = c27_is_sprite_data(object) ? "load-on-demand" : object != nullptr ? "wrong-class" : "missing";
    return c27_is_sprite_data(object) ? object : nullptr;
}
// A reflected bool of `object` by the first name found, or -1.
int c29_bool(const void* object, const char* const* names) {
    const uint8_t* const p = object != nullptr ? c27_find_any(read_ptr(object, kObjectClass), names) : nullptr;
    if (!prop_kind_is(p, "BoolProperty") || !prop_bool_ok(p)) {
        return -1;
    }
    return c29_flag(static_cast<const uint8_t*>(object), p) ? 1 : 0;
}
// The client's pawn on the host (server copy), or null. Same PlayerController walk as card 021's grant.
void* c29_server_copy(const void* own) {
    return c29_server_copy_nth(own, 0);
}
// Card 502 finding 2 (b263): the server copy whose client sent `role` (c502_receive_role). Test commands use it, so
// "client" stays the first client after a rejoin, whose new PC comes last in PlayerController order.
void* c29_server_copy_role(const void* own, uint32_t role) {
    for (int i = 0; i < kC17Slots && own != nullptr; ++i) {
        const void* const pc = g_get_player_controller(own, i);
        void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
        const C50Copy* const c = pc != nullptr ? c50_copy(object_index(pc), false) : nullptr;
        if (c != nullptr && c->role == role && sprint_side(pawn) == kSideServerCopy &&
            is_a(pawn, g_player_mi_class())) {
            return pawn;
        }
    }
    static uint32_t warns = 0; // game thread; session total
    if (own != nullptr && role != 0 && ++warns <= 8) {
        log::line("WARN card502: host t=%llu no PC has role %u stored; PlayerController order (server copy %u)",
                  static_cast<unsigned long long>(GetTickCount64()), role, role);
    }
    return role != 0 ? c29_server_copy_nth(own, static_cast<int>(role) - 1) : nullptr;
}

// Card 209: the nth server copy (0 first) in PlayerController order, or null; with two clients, n 1 is the second.
void* c29_server_copy_nth(const void* own, int n) {
    // GetPlayerController takes any world-context object; the pawn is one.
    for (int i = 0; i < kC17Slots && own != nullptr; ++i) {
        const void* const pc = g_get_player_controller(const_cast<void*>(own), i);
        void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
        if (sprint_side(pawn) == kSideServerCopy && is_a(pawn, g_player_mi_class()) && n-- == 0) {
            return pawn;
        }
    }
    return nullptr;
}
// "name=value, ..." for the component properties in kC22CompProps.
void c22_comp_text(const void* component, char* out, size_t cap) {
    size_t used = 0;
    out[0] = '\0';
    for (const char* name : kC22CompProps) {
        char value[kC27TextChars];
        c22_prop_text(component, name, value, sizeof(value));
        const int w = snprintf(out + used, cap - used, "%s%s=%s", used == 0 ? "" : ", ", name, value);
        if (w < 0 || static_cast<size_t>(w) >= cap - used) {
            return;
        }
        used += static_cast<size_t>(w);
    }
}
// Every leaf property of `object`'s class and its supers, down to (not including) the engine base classes.
void c22_own_props(const char* tag, const void* object, int max_supers) {
    const void* s = read_ptr(object, kObjectClass);
    for (int depth = 0; depth < max_supers && s != nullptr && !c22_is_base_class(s);
         ++depth, s = read_ptr(s, kStructSuper)) {
        const void* f = read_ptr(s, kStructChildProperties);
        for (int i = 0; i < kC22MaxOwnFields && f != nullptr; ++i, f = read_ptr(f, kFieldNext)) {
            ObjectName kind{};
            const PropKind k = prop_kind(f, kind);
            const auto* const prop = static_cast<const uint8_t*>(f);
            const int32_t off = read_i32(prop, kPropertyOffset);
            char value[kC27TextChars];
            if (prop_is_leaf(k)) {
                c27_text(k, kind.text, prop, static_cast<const uint8_t*>(object) + off, value, sizeof(value));
            } else {
                snprintf(value, sizeof(value), "<%s>", kind.text);
            }
            log::line("card022:     %s %s.%s +0x%X (%s) = %s", tag, object_name(s).text,
                      fname_text(prop + kFieldName).text, off, kind.text, value);
            if (k == PropKind::Map) {
                c22_log_map(fname_text(prop + kFieldName).text, prop, static_cast<const uint8_t*>(object) + off);
            }
        }
    }
}
// Card 402: the class index behind c23_collect. One whole walk read every actor of every level (28000 on a mission
// map, 0.7 ms) and ran 7 to 15 times a second. Now a copy of every ULevel::Actors array is kept and compared with the
// live one on each call (pointer compares only); a slot that holds a new actor is classified once, for every class
// entry. An entry's row is used only while its slot still holds its actor, so a destroyed actor (null slot) drops out.
// The first cut read only the slots past each level's last count and missed actors (run c401b92a: 10 WARN rows, the
// missed actor always in a slot below that count), so no slot order is assumed here. Every kC402CheckMs an entry is
// compared with a whole classification (WARN on a difference; the whole one wins).
constexpr int32_t kC402Classes = 48; // call sites pass about 20 classes
constexpr uint32_t kC402Max = 1024;
constexpr ULONGLONG kC402CheckMs = 5000;
constexpr uint32_t kC402WarnLines = 16;
constexpr int32_t kC402MissLines = 4;
constexpr uint32_t kC402FillLines = 4; // per world
struct C402Ref {
    void* actor; // used only while copy[level][index] still holds it
    int32_t level;
    int32_t index;
};
struct C402Index {
    const void* cls; // identity only
    C402Ref ref[kC402Max];
    uint32_t count;
    ULONGLONG check_ms; // when the next whole classification is due
    bool full;          // a match did not fit: the entry cannot answer, the whole walk does
};
struct C402Slots {
    const void* world;             // identity only
    const void* level[kMaxLevels]; // identity only
    void** copy[kMaxLevels];       // process heap, kept for the next world; the level's Actors as last read
    int32_t cap[kMaxLevels];
    int32_t num[kMaxLevels];
    int32_t levels;
    bool ok; // false: more levels than the table or no memory; every call walks the levels
    C402New ring[kC402Ring]; // actors that took a slot below the level's last count (card 401 reads these)
    uint32_t seq;            // ring rows ever written
    uint64_t appended; // diagnostic, this world: new actor past the last count
    uint64_t refilled; //   in a slot that was null
    uint64_t replaced; //   in a slot that held another actor
    uint64_t cleared;  //   slot went null
    uint32_t lines;
};
C402Index g_c402[kC402Classes]{}; // game thread only
C402Index g_c402_tmp{};           // the periodic check's whole classification
C402Slots g_c402_slots{};
int32_t g_c402_used = 0;
uint32_t g_c402_warns = 0;

// The whole walk, straight into the caller's list (the path before card 402). Returns the slots read.
uint64_t c23_walk_whole(const void* world, const Vec3& at, bool dist, const void* cls, C23List& out) {
    uint64_t walked = 0;
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    for (int32_t l = 0; levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const void* const level = levels[l];
        const auto* const actors = level != nullptr ? static_cast<void* const*>(read_ptr(level, kLevelActors)) : nullptr;
        const int32_t num = level != nullptr ? read_i32(level, kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; actors != nullptr && a < num && a < kMaxLevelActors; ++a) {
            void* const actor = actors[a];
            ++walked;
            if (actor == nullptr || !is_a(actor, cls)) {
                continue;
            }
            ++out.total;
            if (out.count < kC23Max) {
                const Vec3 loc = actor_location(actor);
                out.e[out.count++] = C23Enemy{actor, loc, dist ? vec_dist(loc, at) : -1.0};
            }
        }
    }
    return walked;
}

bool c402_valid(const C402Ref& r) {
    const C402Slots& s = g_c402_slots;
    return r.level < s.levels && r.level < kMaxLevels && r.index < s.num[r.level] && s.copy[r.level][r.index] == r.actor;
}

// Drop the entry's rows whose slot no longer holds their actor; returns how many are left.
uint32_t c402_compact(C402Index& ix) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < ix.count; ++i) {
        if (c402_valid(ix.ref[i])) {
            ix.ref[n++] = ix.ref[i];
        }
    }
    ix.count = n;
    return n;
}

// One actor of the entry's class, seen in a slot. An actor already held (moved to another slot) keeps one row.
void c402_add(C402Index& ix, void* actor, int32_t level, int32_t index) {
    for (uint32_t i = 0; i < ix.count; ++i) {
        if (ix.ref[i].actor == actor) {
            ix.ref[i].level = level;
            ix.ref[i].index = index;
            return;
        }
    }
    if (ix.count == kC402Max) {
        (void)c402_compact(ix);
    }
    if (ix.count == kC402Max) {
        ix.full = true;
        return;
    }
    ix.ref[ix.count++] = C402Ref{actor, level, index};
}

// Classify every slot of the copy into `ix` (emptied first). Returns the slots read.
uint64_t c402_build(C402Index& ix) {
    const C402Slots& s = g_c402_slots;
    uint64_t walked = 0;
    ix.count = 0;
    ix.full = false;
    for (int32_t l = 0; l < s.levels && l < kMaxLevels; ++l) {
        for (int32_t a = 0; a < s.num[l]; ++a) {
            void* const actor = s.copy[l][a];
            ++walked;
            if (actor == nullptr || !is_a(actor, ix.cls)) {
                continue;
            }
            if (ix.count < kC402Max) {
                ix.ref[ix.count++] = C402Ref{actor, l, a};
            } else {
                ix.full = true;
            }
        }
    }
    return walked;
}

void c407_reset();                                        // card 407 (below c50_name_match)
void c407_add(void* actor, int32_t level, int32_t index); // card 407

// A world or level set the copy does not describe: empty the copy and every entry (the next compare refills them).
void c402_slots_reset(const void* world, void* const* levels, int32_t num_levels) {
    C402Slots& s = g_c402_slots;
    if (s.world != nullptr) {
        log::line("card402: %s slots since the last reset: %llu appended, %llu refilled, %llu replaced, %llu cleared",
                  side(), s.appended, s.refilled, s.replaced, s.cleared);
    }
    s.world = world;
    s.levels = num_levels;
    s.ok = levels != nullptr && num_levels <= kMaxLevels;
    for (int32_t l = 0; l < kMaxLevels; ++l) {
        s.level[l] = s.ok && l < num_levels ? levels[l] : nullptr;
        s.num[l] = 0;
    }
    s.seq += kC402Ring + 1; // every reader sees an overflow
    s.appended = 0;
    s.refilled = 0;
    s.replaced = 0;
    s.cleared = 0;
    s.lines = 0;
    // Drop every entry, not only its rows: an entry's class can be a blueprint class of the old world's package, freed
    // by the travel's GC, and c402_slot_new tests each new actor against every entry's class (is_a reads the class).
    // Run c401b93b crashed there on the client's first compare of the seamless travel world. c402_entry rebuilds an
    // entry when a caller next asks with its live class.
    g_c402_used = 0;
    c407_reset();
}

// True when the copy still describes this world (same levels, none shorter than its copy).
bool c402_slots_current(const void* world, void* const* levels, int32_t num_levels) {
    const C402Slots& s = g_c402_slots;
    if (s.world != world || levels == nullptr || num_levels != s.levels) {
        return false;
    }
    for (int32_t l = 0; l < num_levels && l < kMaxLevels; ++l) {
        const void* const level = levels[l];
        const int32_t num = level != nullptr ? read_i32(level, kLevelActors + kArrayNum) : 0;
        if (level != s.level[l] || (num < s.num[l] && num < kMaxLevelActors)) {
            return false;
        }
    }
    return true;
}

// Room for `need` slots in level `l`'s copy. False (and the copy off) without memory.
bool c402_slots_room(int32_t l, int32_t need) {
    C402Slots& s = g_c402_slots;
    if (need <= s.cap[l]) {
        return true;
    }
    const int32_t want = need + need / 4 + 1024;
    const int32_t cap = want < kMaxLevelActors ? want : kMaxLevelActors;
    auto* const mem = static_cast<void**>(HeapAlloc(GetProcessHeap(), 0, static_cast<size_t>(cap) * sizeof(void*)));
    if (mem == nullptr) {
        s.ok = false;
        log::line("WARN card402: %s no memory for %d actor slots: c23_collect walks the levels", side(), cap);
        return false;
    }
    if (s.copy[l] != nullptr) {
        memcpy(mem, s.copy[l], static_cast<size_t>(s.num[l]) * sizeof(void*));
        HeapFree(GetProcessHeap(), 0, s.copy[l]);
    }
    s.copy[l] = mem;
    s.cap[l] = cap;
    return true;
}

// A slot now holds `actor`, which the copy did not have there: count it, note it for card 401, classify it.
void c402_slot_new(int32_t l, int32_t a, void* actor, const void* was, bool past) {
    C402Slots& s = g_c402_slots;
    if (past) {
        ++s.appended;
    } else {
        ++(was == nullptr ? s.refilled : s.replaced);
        s.ring[s.seq++ % kC402Ring] = C402New{l, a, actor};
        if (s.lines < kC402FillLines) {
            ++s.lines;
            log::line("card402: %s %s %s took level %d slot %d of %d, which held %s", side(), class_name(actor).text,
                      object_name(actor).text, l, a, s.num[l], was == nullptr ? "none" : "another actor");
        }
    }
    for (int32_t i = 0; i < g_c402_used; ++i) {
        if (!g_c402[i].full && is_a(actor, g_c402[i].cls)) {
            c402_add(g_c402[i], actor, l, a);
        }
    }
    c407_add(actor, l, a);
}

// Compare every level's Actors with the copy and take in what changed. Returns the slots read.
uint64_t c402_slots_step(const void* world) {
    C402Slots& s = g_c402_slots;
    auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = levels != nullptr ? read_i32(world, kWorldLevels + kArrayNum) : 0;
    if (!c402_slots_current(world, levels, num_levels)) {
        c402_slots_reset(world, levels, num_levels);
    }
    uint64_t walked = 0;
    for (int32_t l = 0; s.ok && l < num_levels; ++l) {
        const void* const level = levels[l];
        auto* const actors = level != nullptr ? static_cast<void* const*>(read_ptr(level, kLevelActors)) : nullptr;
        const int32_t num = actors != nullptr ? read_i32(level, kLevelActors + kArrayNum) : 0;
        const int32_t end = num < kMaxLevelActors ? (num > 0 ? num : 0) : kMaxLevelActors;
        if (!c402_slots_room(l, end)) {
            break;
        }
        void** const copy = s.copy[l];
        const int32_t had = s.num[l];
        s.num[l] = end; // before the loop: an entry row added below must pass c402_valid if its entry compacts
        walked += static_cast<uint64_t>(end);
        // Card 414: nearly every frame nothing changed, and one memcmp says so (run c414b109a: this step was the
        // client's top mod symbol, 69 of 600 samples, comparing slot by slot).
        if (had >= end && (end == 0 || memcmp(actors, copy, static_cast<size_t>(end) * sizeof(void*)) == 0)) {
            continue;
        }
        for (int32_t a = 0; a < end; ++a) {
            void* const actor = actors[a];
            const void* const was = a < had ? copy[a] : nullptr;
            if (a < had && actor == was) {
                continue;
            }
            copy[a] = actor;
            if (actor == nullptr) {
                s.cleared += was != nullptr ? 1u : 0u;
                continue;
            }
            c402_slot_new(l, a, actor, was, a >= had);
        }
    }
    return walked;
}

// Card 401's feed (see hooks_internal.h).
bool c402_refills(const void* world, uint32_t& seq, C402New* out, int32_t max, int32_t& n) {
    (void)c402_slots_step(world);
    const C402Slots& s = g_c402_slots;
    n = 0;
    if (!s.ok || s.seq - seq > kC402Ring) {
        seq = s.seq;
        return false;
    }
    for (; seq != s.seq && n < max; ++seq) {
        out[n++] = s.ring[seq % kC402Ring];
    }
    return true;
}

// The entry for `cls`, or null when the table is full (the caller walks the levels). A new entry is classified from
// the copy, so the copy must be current. Adds the slots read to *walked.
C402Index* c402_entry(const void* cls, uint64_t* walked) {
    for (int32_t i = 0; i < g_c402_used; ++i) {
        if (g_c402[i].cls == cls) {
            return &g_c402[i];
        }
    }
    if (g_c402_used == kC402Classes) {
        return nullptr;
    }
    C402Index& ix = g_c402[g_c402_used++];
    ix.cls = cls;
    ix.check_ms = GetTickCount64() + kC402CheckMs;
    *walked += c402_build(ix);
    return &ix;
}

// The periodic check: the entry against a whole classification of the copy, which then replaces it. A difference is a
// WARN with the first actors the entry lacked. Returns the slots read.
uint64_t c402_check(C402Index& ix) {
    const uint32_t held = c402_compact(ix);
    const bool was_full = ix.full;
    g_c402_tmp.cls = ix.cls;
    const uint64_t walked = c402_build(g_c402_tmp);
    if (!was_full && !g_c402_tmp.full && held != g_c402_tmp.count && g_c402_warns++ < kC402WarnLines) {
        log::line("WARN card402: %s class index held %u actors of %s, the whole walk finds %u (walk kept)", side(),
                  held, object_name(ix.cls).text, g_c402_tmp.count);
        int32_t lines = 0;
        for (uint32_t i = 0; i < g_c402_tmp.count && lines < kC402MissLines; ++i) {
            const C402Ref& r = g_c402_tmp.ref[i];
            bool in = false;
            for (uint32_t k = 0; k < held && !in; ++k) {
                in = ix.ref[k].actor == r.actor;
            }
            if (!in) {
                ++lines;
                log::line("WARN card402: %s missed %s %s: level %d slot %d of %d", side(), class_name(r.actor).text,
                          object_name(r.actor).text, r.level, r.index, g_c402_slots.num[r.level]);
            }
        }
    }
    memcpy(ix.ref, g_c402_tmp.ref, sizeof(C402Ref) * g_c402_tmp.count);
    ix.count = g_c402_tmp.count;
    ix.full = g_c402_tmp.full;
    ix.check_ms = GetTickCount64() + kC402CheckMs;
    return walked;
}

// Every actor of class `cls` in the world's levels (card 023: ASTEnemyBase; card 037: ASTCharacter).
void c23_collect(const void* world, const void* pawn, const void* cls, C23List& out) {
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0); // card 330 step 1
    uint64_t walked = 0;
    out.count = 0;
    out.total = 0;
    const Vec3 at = pawn != nullptr ? actor_location(pawn) : Vec3{};
    walked = c402_slots_step(world);
    C402Index* const ix = g_c402_slots.ok && cls != nullptr ? c402_entry(cls, &walked) : nullptr;
    if (ix != nullptr && GetTickCount64() >= ix->check_ms) {
        walked += c402_check(*ix);
    }
    const bool ok = ix != nullptr && !ix->full;
    if (ok) {
        const uint32_t live = c402_compact(*ix);
        for (uint32_t i = 0; i < live; ++i) {
            void* const actor = ix->ref[i].actor;
            ++out.total;
            if (out.count < kC23Max) {
                const Vec3 loc = actor_location(actor);
                out.e[out.count++] = C23Enemy{actor, loc, pawn != nullptr ? vec_dist(loc, at) : -1.0};
            }
        }
    } else {
        walked += c23_walk_whole(world, at, pawn != nullptr, cls, out);
    }
    LARGE_INTEGER t1;
    QueryPerformanceCounter(&t1);
    c330_collect(_ReturnAddress(), walked, t1.QuadPart - t0.QuadPart); // card 330 step 1
}
// Card 368 (see hooks_internal.h). Without the weak-ref functions every call walks the levels, as before the card.
C368Cache g_c368_players{};
C368Cache g_c368_turrets{};
uint32_t c368_actors(C368Cache& cache, const void* world, const void* cls, void* out[kC23Max]) {
    static C23List list; // game thread only; too large for the stack
    if (world == nullptr || cls == nullptr) {
        return 0;
    }
    const ULONGLONG now = GetTickCount64();
    if (world != cache.world || now >= cache.next_ms || g_weak_get == nullptr || g_weak_assign == nullptr) {
        c23_collect(world, nullptr, cls, list);
        cache.world = world;
        cache.next_ms = now + kC368WalkMs;
        cache.count = list.count;
        for (uint32_t i = 0; i < list.count; ++i) {
            c31_weak_set(cache.weak[i], list.e[i].actor);
            out[i] = list.e[i].actor;
        }
        return list.count;
    }
    uint32_t n = 0;
    for (uint32_t i = 0; i < cache.count; ++i) {
        void* const actor = c31_weak_get(cache.weak[i]);
        if (actor != nullptr) {
            out[n++] = actor;
        }
    }
    return n;
}
// This side's pawn when it is a player character on foot, else null.
const void* c23_own_pawn(void* pc) {
    const void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    const bool on_foot = pawn != nullptr && is_a(pawn, g_player_character_class()) &&
                         holding_dropship(static_cast<const uint8_t*>(pawn)) == nullptr;
    return on_foot ? pawn : nullptr;
}
// Player pawns in the world: the target (by side) and whether the host's pawn and a server copy are on foot.
C30Pawns c30_find(const void* world) {
    C30Pawns out{};
    const void* const cls = g_player_character_class();
    const uint32_t want = c30_target_side();
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    for (int32_t l = 0; levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const void* const level = levels[l];
        const auto* const actors = level != nullptr ? static_cast<void* const*>(read_ptr(level, kLevelActors)) : nullptr;
        const int32_t num = level != nullptr ? read_i32(level, kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; actors != nullptr && a < num && a < kMaxLevelActors && out.players < kC30MaxPlayers; ++a) {
            void* const actor = actors[a];
            if (actor == nullptr || !is_a(actor, cls)) {
                continue;
            }
            ++out.players;
            const uint32_t s = sprint_side(actor);
            const bool on_foot = holding_dropship(static_cast<const uint8_t*>(actor)) == nullptr;
            out.host_on_foot = out.host_on_foot || (s == kSideHostOwn && on_foot);
            out.copy_on_foot = out.copy_on_foot || (s == kSideServerCopy && on_foot);
            out.target = s == want && out.target == nullptr ? actor : out.target;
        }
    }
    return out;
}
// Called from detour_asc_tag_pressed and detour_interact: inside the window, every input tag and interact.
void c30_note_input(const char* what, const void* owner, const void* tag) {
    if (!g_c30_window || !c30_line()) {
        return;
    }
    log::line("card030: %s %s %s on %s (%s), %llu ms after Health 0", side(), what,
              tag != nullptr ? fname_text(tag).text : "-", object_name(owner).text, kSprintSideText[sprint_side(owner)],
              GetTickCount64() - g_c30.zero_ms);
}
// NewObject(outer, cls, NAME_None, RF_Transactional) through StaticConstructObject_Internal.
void* c31_construct(void* cls, void* outer) {
    alignas(16) uint8_t params[kScoParamsBytes] = {};
    g_sco_params_ctor(params, cls);
    memcpy(params + kScoOuter, &outer, sizeof(outer));
    memcpy(params + kScoSetFlags, &kRfTransactional, sizeof(kRfTransactional));
    void* const object = g_sco_internal(params);
    if (object == nullptr) {
        log::line("ERROR card031: StaticConstructObject_Internal(%s, outer %s) returned null", object_name(cls).text,
                  object_name(outer).text);
    }
    return object;
}
bool c43_down_state(C43Life s) {
    return s == C43Life::Downed || s == C43Life::Out || s == C43Life::Dead;
}
int32_t c43_find(int32_t id) {
    for (int32_t i = 0; i < g_c43.count; ++i) {
        if (g_c43.e[i].pawn_id == id) {
            return i;
        }
    }
    return -1;
}
// The entry of a player pawn on this machine, or -1.
int32_t c43_find_pawn(const void* pawn) {
    if (!g_client_session) {
        return c43_find(object_index(pawn));
    }
    const int32_t id = c43_client_id(pawn);
    return id >= 0 ? c43_find(id) : -1;
}
// Client (card 209): the entry id of a player pawn is its NetGUID, which fits int32 (c41_guid_args bounds it).
int32_t c43_client_id(const void* pawn) {
    uint32_t lo = 0;
    uint32_t hi = 0;
    const uint64_t guid = pawn != nullptr ? c41_guid(pawn) : 0;
    return c41_guid_args(guid, lo, hi) ? static_cast<int32_t>(guid) : -1;
}
// Downed, Out or Dead on this machine's view. Callers pass player pawns.
bool c43_is_down(const void* pawn) {
    if (g_c43.count == 0 || pawn == nullptr) {
        return false;
    }
    const int32_t i = c43_find_pawn(pawn);
    return i >= 0 && c43_down_state(g_c43.e[i].state);
}
// The life state text of a player pawn on this machine's view ("Up" when it has no entry).
const char* c43_life_text(const void* pawn) {
    const int32_t i = g_c43.count != 0 && pawn != nullptr ? c43_find_pawn(pawn) : -1;
    return i >= 0 ? kC43LifeText[static_cast<size_t>(g_c43.e[i].state)] : "Up";
}
void c43_remove(int32_t i) {
    g_c43.e[i] = g_c43.e[--g_c43.count];
}
C43Roster c43_roster(const void* world) {
    C43Roster r{};
    for (int32_t s = 0; s < kC43Max; ++s) {
        void* const pc = g_get_player_controller(world, s);
        void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
        r.p[s] = C43Player{pc, pawn, object_index(pawn)};
        r.players += pc != nullptr ? 1u : 0u;
    }
    return r;
}
int32_t c43_slot(const C43Roster& r, int32_t id) {
    for (int32_t s = 0; s < kC43Max && id >= 0; ++s) {
        if (r.p[s].pawn != nullptr && r.p[s].id == id) {
            return s;
        }
    }
    return -1;
}
// Health base value through the health component's ASC; the health hook skips the change it makes.
void c43_set_health(const void* pawn, float value) {
    void* component = nullptr;
    c21_health(static_cast<const uint8_t*>(pawn), &component);
    c43_set_component_health(component, value, pawn);
}
// The same on a health component found another way (card 131: a placed building's); `owner` names it in the log.
void c43_set_component_health(const void* component, float value, const void* owner) {
    void* const asc = component != nullptr ? read_ptr(component, kHealthComponentAsc) : nullptr;
    const GameplayAttributeRaw* const attribute = c25_health_attribute();
    if (asc == nullptr || attribute == nullptr) {
        log::line("WARN card043: %s Health of %s not set: asc %p, attribute %p", side(), object_name(owner).text, asc,
                  static_cast<const void*>(attribute));
        return;
    }
    g_c43.in_reset = true;
    g_set_numeric_attribute_base(asc, attribute, value);
    g_c43.in_reset = false;
}
float c43_max_health(const void* component) {
    if (!g_c43.max_known) {
        g_c43.max_offset = c30_set_offset("MaxHealth");
        g_c43.max_known = true;
    }
    const void* const set = component != nullptr ? read_ptr(component, kHealthComponentSet) : nullptr;
    if (set == nullptr || g_c43.max_offset < 0) {
        return -1.0f;
    }
    return read_f32(set, static_cast<size_t>(g_c43.max_offset) + kAttrDataCurrent);
}
void c43_transition(const C43Roster& r, C43Entry& e, const void* pawn, const char* why) {
    void* component = nullptr;
    const float health = c21_health(static_cast<const uint8_t*>(pawn), &component);
    const uint64_t guid = c41_guid(pawn); // card 509: a leaver's Up later sends this, not a closing connection's 0
    e.guid = guid != 0 ? guid : e.guid;
    c43_broadcast(r, e);
    ++g_c43.n.transitions;
    if (sprint_side(pawn) == kSideHostOwn) {
        c43_own_view(const_cast<void*>(pawn), c43_down_state(e.state));
    }
    log::line("card043: host t=%llu %s (%s, slot %u) -> %s (%s): Health %.1f of %.1f, bleedout %.1f s, autosave %d, "
              "Down sent %u",
              static_cast<unsigned long long>(GetTickCount64()), object_name(pawn).text,
              kSprintSideText[sprint_side(pawn)], e.slot, kC43LifeText[static_cast<size_t>(e.state)], why,
              static_cast<double>(health), static_cast<double>(c43_max_health(component)),
              static_cast<double>(e.secs_left), c43_autosave(pawn), g_c43.n.down_sent);
}
// Host: the pawn goes down (state Downed, or Out when nobody else is Up): abilities cancelled on the pawn's and its
// weapon's ASC, a held fire relay released, Health 1, MaxWalkSpeed 0.
void c43_enter_down(void* pawn, const C43Roster& r, uint32_t slot, C43Life state, ULONGLONG now) {
    if (is_a(pawn, g_mech_class())) {
        log::line("WARN card079: host %s (slot %u) is a mech: not a card 043 player, not downed",
                  object_name(pawn).text, slot);
        return;
    }
    pl_count(PlCount::Down); // card 155 (Downed and Out)
    if (g_c43.count >= kC43Max) {
        log::line("WARN card043: host entry table full; %s not downed", object_name(pawn).text);
        return;
    }
    void* const asc = c21_character_ptr(pawn, kCharacterAsc);
    const void* weapon = nullptr;
    void* const weapon_asc = c196_inventory_at(pawn) != 0 ? c24_weapon_asc(pawn, &weapon) : nullptr; // MI or bug
    if (asc != nullptr) {
        g_cancel_all_abilities(asc, nullptr);
    }
    if (weapon_asc != nullptr) {
        g_cancel_all_abilities(weapon_asc, nullptr);
    }
    c24_release_pawn(pawn, "release (downed)");
    c43_release_own_fire(pawn);
    C43Entry& e = g_c43.e[g_c43.count++];
    e = C43Entry{object_index(pawn), slot, state, state == C43Life::Downed ? kC43BleedS : 0.0f, c43_walk(pawn),
                 now, 0, 0};
    c43_set_health(pawn, 1.0f);
    c43_set_walk(pawn, 0.0f);
    c43_transition(r, e, pawn, "HandleDeathEvent skipped");
    log::line("card043: host CancelAllAbilities on pawn ASC %p and weapon ASC %p (%s); saved MaxWalkSpeed %.0f", asc,
              weapon_asc, object_name(weapon).text, static_cast<double>(e.saved_walk));
}
// Host, nobody Up: every downed copy is Out and the host's pawn is Dead (its real death runs or is pending).
void c43_all_down(const C43Roster& r, const void* host, const char* why) {
    g_c43.all_down = true;
    g_c43.holds = 0;
    const int32_t host_id = object_index(host);
    if (c43_find(host_id) < 0 && g_c43.count < kC43Max) {
        g_c43.e[g_c43.count++] = C43Entry{host_id, 0, C43Life::Up, 0.0f, -1.0f, GetTickCount64(), 0, 0};
    }
    for (int32_t i = 0; i < g_c43.count; ++i) {
        C43Entry& e = g_c43.e[i];
        const int32_t slot = c43_slot(r, e.pawn_id);
        e.state = e.pawn_id == host_id ? C43Life::Dead : C43Life::Out;
        e.secs_left = 0.0f;
        e.down_ms = e.down_ms != 0 ? e.down_ms : GetTickCount64();
        if (slot >= 0) {
            c43_transition(r, e, r.p[slot].pawn, why);
        }
    }
}
void c43_totals(const char* what) {
    log::line("card043: %s %s: Down sent %u received %u, Use sent %u received %u, transitions %u, entries %d, holds %d, "
              "all down %d",
              side(), what, g_c43.n.down_sent, g_c43.n.down_recv, g_c43.n.use_sent, g_c43.n.use_recv,
              g_c43.n.transitions, g_c43.count, g_c43.holds, g_c43.all_down ? 1 : 0);
}
void c43_shot(ULONGLONG at_ms, const char* what, uint8_t flags) {
    if (g_c43t.shots < kC43Shots) {
        g_c43t.shot[g_c43t.shots++] = C43Shot{at_ms, what, flags};
    } else {
        log::line("WARN card043: %s screenshot dropped (queue full): %s", side(), what);
    }
}
// Card 044: the view from `eye` (an actor location) turned to `target`'s feet.
double c44_aim(void* pc, const Vec3& eye, const Vec3& target) {
    const double dx = target.x - eye.x;
    const double dy = target.y - eye.y;
    const double h = sqrt(dx * dx + dy * dy);
    const double pitch = atan2((target.z - kC44FeetDown) - (eye.z + kC44EyeUp), h > 1.0 ? h : 1.0) * kRadToDeg;
    const double rot[3] = {pitch, atan2(dy, dx) * kRadToDeg, 0.0};
    g_orig_set_control_rotation(pc, rot);
    return pitch;
}
// The other player's pawn: on the client the host's, on the host the first server copy.
void* c44_other_pawn(const void* world) {
    if (g_client_session) {
        return c43_client_host_pawn(world);
    }
    const C43Roster r = c43_roster(world);
    for (int32_t s = 1; s < kC43Max; ++s) {
        if (r.p[s].pawn != nullptr && is_a(r.p[s].pawn, g_player_character_class())) {
            return r.p[s].pawn;
        }
    }
    return nullptr;
}
C43Life c43_life(const void* pawn) {
    const int32_t i = pawn != nullptr ? c43_find(object_index(pawn)) : -1;
    return i >= 0 ? g_c43.e[i].state : C43Life::Up;
}
// Client (card 209): every player pawn of sprint side "other" (the host's and other clients' copies), one level scan.
int32_t c43_client_other_pawns(const void* world, void* out[kC43Max]) {
    for (int32_t i = 0; i < kC43Max; ++i) {
        out[i] = nullptr;
    }
    static void* players[kC23Max]; // game thread only
    const void* const cls = world != nullptr ? g_player_character_class() : nullptr;
    const uint32_t count = c368_actors(g_c368_players, world, cls, players); // card 368: no level walk of its own
    int32_t n = 0;
    for (uint32_t i = 0; i < count && n < kC43Max; ++i) {
        if (sprint_side(players[i]) == kSideOther) {
            out[n++] = players[i];
        }
    }
    return n;
}
// Client: the first player pawn that is not this client's own (test drivers only; card 209 row 10).
void* c43_client_host_pawn(const void* world) {
    void* others[kC43Max];
    return c43_client_other_pawns(world, others) > 0 ? others[0] : nullptr; // card 368: the shared player list
}
// Card 326: the registry's "player" map end (and the respawn in place, death.cpp): the entries and holds go.
void c43_map_end() {
    if (g_c43.n.down_sent + g_c43.n.down_recv + g_c43.n.use_sent + g_c43.n.use_recv != 0) {
        c43_totals("map totals");
    }
    const uint64_t fire = g_c43.fire_tag;
    const uint64_t walk = g_c43.walk_tag;
    g_c43 = C43State{};
    g_c43.fire_tag = fire; // FName values outlive the map
    g_c43.walk_tag = walk;
}
int32_t c43_live() {
    return g_c43.count + g_c43.holds;
}
// The registry's "player" map arm: test state and the per-map arms of cards 046, 048, 130, 156 and 209.
void c43_arm_map() {
    g_c43t = C43Test{};
    c46_arm_map();
    c48_arm_map();
    c130_pickup_map();
    c156_arm_map();
    c209_arm_map();
}
int fs_len(const void* fs) {
    const auto* const s = static_cast<const FStringRaw*>(fs);
    return (s->data == nullptr || s->num <= 1 || s->num > kMaxLoggedFStringNum) ? 0 : s->num - 1;
}
const wchar_t* fs_text(const void* fs) {
    return fs_len(fs) > 0 ? static_cast<const FStringRaw*>(fs)->data : L"";
}
// Every new map, both sides: the 043 entries and holds and the 044 one-shot flags start clean.
void c45_arm_map() {
    log::line("card045: %s t=%llu new map: reset 043 (entries %d, holds %d, all down %d, client seen %d, death done %d) "
              "and 044 one-shots (StartDeath %d, CompleteDeath %d, fallback %d, death screen %d)",
              side(), static_cast<unsigned long long>(GetTickCount64()), g_c43.count, g_c43.holds,
              g_c43.all_down ? 1 : 0, g_c43.client_seen ? 1 : 0, g_c43.death_done ? 1 : 0, g_c44.start_ms != 0 ? 1 : 0,
              g_c44.complete_ms != 0 ? 1 : 0, g_c44.fallback_done ? 1 : 0, g_c44.screen_weak != 0 ? 1 : 0);
    g_c44.start_ms = 0;
    g_c44.complete_ms = 0;
    g_c44.window_ms = 0;
    g_c44.screen_weak = 0;
    g_c44.fallback_done = false;
    g_c44_pe = false;
    g_c45.pawn = nullptr;
    g_c45.screen_count = 0;
}

float c46_kill_z(const void* context) {
    void* const gi = g_get_st_game_instance(context);
    return gi != nullptr ? g_get_chapter_kill_z(gi) : 0.0f;
}
// "weapons <n>, last equipped <i>, equipped <weapon> (<definition>), items <n>: <class>..." of a pawn.
void c48_inventory_text(const void* pawn, char* out, size_t cap) {
    const void* const inv = is_a(pawn, g_player_mi_class()) ? read_ptr(pawn, kMiWeaponInventory) : nullptr;
    if (inv == nullptr) {
        snprintf(out, cap, "no weapon inventory");
        return;
    }
    const void* const weapon = g_get_equipped_weapon(inv);
    const int32_t n = array_num(static_cast<const uint8_t*>(inv), kInventoryItems);
    const auto* const items = static_cast<void* const*>(read_ptr(inv, kInventoryItems));
    size_t used = static_cast<size_t>(snprintf(out, cap, "weapons %d, last equipped %d, equipped %s (%s), items %d:",
                                               g_get_weapon_count(inv), read_i32(inv, kInventoryLastEquipped),
                                               object_name(weapon).text,
                                               object_name(weapon != nullptr ? read_ptr(weapon, kWeaponDefinitionClass)
                                                                             : nullptr).text, n));
    for (int32_t i = 0; i < n && i < kC48ItemsListed && items != nullptr && used < cap; ++i) {
        used += static_cast<size_t>(snprintf(out + used, cap - used, " %s", object_name(items[i]).text));
    }
}
bool c50_line() {
    return note_event_cap(g_c50.lines, kC50Lines, "card050");
}
bool c50_placed(const void* object) {
    return object != nullptr && (static_cast<uint32_t>(read_i32(object, kC50ObjectFlags)) & kC50WasLoaded) != 0;
}
// Card 404: the FName number's text suffix ("_<Number - 1>", empty for number 0) into out, NUL-terminated; returns
// its length (at most kC50SuffixMax - 1).
constexpr int32_t kC50SuffixMax = 12; // '_' + 10 digits + NUL
int32_t c50_suffix(uint32_t number, char out[kC50SuffixMax]) {
    out[0] = '\0';
    if (number == 0) {
        return 0;
    }
    char digits[10];
    int32_t n = 0;
    uint32_t v = number - 1;
    do {
        digits[n++] = static_cast<char>('0' + v % 10);
        v /= 10;
    } while (v != 0 && n < 10);
    out[0] = '_';
    for (int32_t i = 0; i < n; ++i) {
        out[1 + i] = digits[n - 1 - i];
    }
    out[1 + n] = '\0';
    return 1 + n;
}
// The object's FName as text with its "_<Number - 1>" suffix, untruncated. Returns the length, or -1 (out empty) when
// it does not fit in cap - 1 chars or a char is not printable ASCII.
int32_t c50_name(const void* object, char* out, int32_t cap) {
    out[0] = '\0';
    const auto* const fname = object != nullptr ? static_cast<const uint8_t*>(object) + kObjectName : nullptr;
    const uint8_t* const entry = fname_entry_checked(fname); // card 238
    if (entry == nullptr) {
        return -1;
    }
    uint16_t header = 0;
    memcpy(&header, entry, sizeof(header));
    const bool wide = (header & 1u) != 0;
    const int32_t len = static_cast<int32_t>(header >> 6);
    if (len <= 0 || len >= cap) {
        return -1;
    }
    for (int32_t i = 0; i < len; ++i) {
        uint16_t c = entry[2 + i];
        if (wide) {
            memcpy(&c, entry + 2 + 2 * static_cast<size_t>(i), sizeof(c));
        }
        if (c < 0x20 || c > 0x7E) {
            out[0] = '\0';
            return -1;
        }
        out[i] = static_cast<char>(c);
    }
    out[len] = '\0';
    const auto number = static_cast<uint32_t>(read_i32(fname, 4));
    char suffix[kC50SuffixMax];
    const int32_t w = c50_suffix(number, suffix); // card 404: no snprintf, this runs once per actor of a scan
    if (w >= cap - len) {
        out[0] = '\0';
        return -1;
    }
    memcpy(out + len, suffix, static_cast<size_t>(w) + 1);
    return len + w;
}
// Card 404: true exactly when c50_name(object) gives `name` (name_len = strlen(name), printable ASCII), without
// building the text: the length is compared first, so nearly every actor of a by-name scan is refused on the entry
// header alone. Run c403b94b: the client's by-name finds cost 2 to 5 ms per received row (snprintf per actor).
bool c50_name_is(const void* object, const char* name, int32_t name_len) {
    const auto* const fname = object != nullptr ? static_cast<const uint8_t*>(object) + kObjectName : nullptr;
    const uint8_t* const entry = fname_entry_checked(fname);
    if (entry == nullptr) {
        return false;
    }
    uint16_t header = 0;
    memcpy(&header, entry, sizeof(header));
    const bool wide = (header & 1u) != 0;
    const int32_t len = static_cast<int32_t>(header >> 6);
    char suffix[kC50SuffixMax];
    const int32_t w = c50_suffix(static_cast<uint32_t>(read_i32(fname, 4)), suffix);
    if (len <= 0 || len + w != name_len || memcmp(name + len, suffix, static_cast<size_t>(w)) != 0) {
        return false;
    }
    if (!wide) {
        return memcmp(entry + 2, name, static_cast<size_t>(len)) == 0;
    }
    for (int32_t i = 0; i < len; ++i) {
        uint16_t c = 0;
        memcpy(&c, entry + 2 + 2 * static_cast<size_t>(i), sizeof(c));
        if (c != static_cast<uint8_t>(name[i])) {
            return false;
        }
    }
    return true;
}
// Card 405: a by-name scan's name, with the FName (pool index, number) it resolves to once that is known. An FName
// index names one pool entry for the life of the process and the pool holds each text once, so after the first text
// match the scan compares two integers per actor and reads no pool entry. g_c405 keeps the pair per name across scans
// (direct-mapped by the name's hash; a slot is reused by a later name). A name no actor ever carried stays on the
// text path. Game thread only, like the scans.
namespace {
constexpr uint32_t kC405Slots = 1024;
struct C405Slot {
    uint64_t hash; // 0 = empty
    uint32_t idx;
    uint32_t number;
};
C405Slot g_c405[kC405Slots];
} // namespace
C50NameRef c50_name_ref(const char* name, size_t max_len) {
    C50NameRef ref{};
    const size_t chars = strlen(name);
    ref.name = name;
    ref.name_len = chars <= max_len ? static_cast<int32_t>(chars) : -1; // over the caller's cap: matches nothing
    uint64_t h = 0xCBF29CE484222325ull; // FNV-1a 64
    for (size_t i = 0; i < chars; ++i) {
        h = (h ^ static_cast<uint8_t>(name[i])) * 0x100000001B3ull;
    }
    ref.hash = h != 0 ? h : 1;
    const C405Slot& slot = g_c405[ref.hash % kC405Slots];
    if (ref.name_len > 0 && slot.hash == ref.hash) {
        ref.idx = slot.idx;
        ref.number = slot.number;
        ref.known = true;
    }
    return ref;
}
bool c50_name_match(const void* object, C50NameRef& ref) {
    if (object == nullptr) {
        return false;
    }
    const auto* const fname = static_cast<const uint8_t*>(object) + kObjectName;
    const auto idx = static_cast<uint32_t>(read_i32(fname, 0));
    const auto number = static_cast<uint32_t>(read_i32(fname, 4));
    if (ref.known) {
        return idx == ref.idx && number == ref.number;
    }
    if (!c50_name_is(object, ref.name, ref.name_len)) {
        return false;
    }
    ref.idx = idx;
    ref.number = number;
    ref.known = true;
    g_c405[ref.hash % kC405Slots] = C405Slot{ref.hash, idx, number};
    return true;
}
// Card 407: the placed actors of the world by name, for c95_find_nearest. Run c405b96a: each by-name find still read
// the FName of every level actor (28000 on a mission map; c50_name_match, c50_name_is and fname_entry_checked were
// 511 of the client's 1025 mod samples). The index is filled from card 402's copy of the level arrays on the first
// find after a reset, then card 402's compare adds each new placed actor (c402_slot_new). A row answers only while
// its slot still holds its actor and the actor still carries the name, so a destroyed or renamed actor drops out.
// Rows are never removed; a full table turns the index off until the next reset. Game thread only.
namespace {
constexpr uint32_t kC407Slots = 131072; // power of two; FED_C2M1 has about 31000 level actors
constexpr uint32_t kC407Max = kC407Slots / 4 * 3;
constexpr int32_t kC407NameMax = 255;
struct C407Row {
    uint64_t hash; // FNV-1a 64 of the c50_name text (c50_name_ref's hash); 0 = empty
    void* actor;   // used only while card 402's copy holds it at (level, index)
    int32_t level;
    int32_t index;
};
struct C407Index {
    C407Row row[kC407Slots];
    uint32_t n;
    bool built;
    bool full;
    uint32_t finds, answered, builds; // since the last reset line
};
C407Index g_c407{};

uint64_t c407_hash(const char* text, int32_t len) {
    uint64_t h = 0xCBF29CE484222325ull; // as c50_name_ref
    for (int32_t i = 0; i < len; ++i) {
        h = (h ^ static_cast<uint8_t>(text[i])) * 0x100000001B3ull;
    }
    return h != 0 ? h : 1;
}

void c407_build() {
    const C402Slots& s = g_c402_slots;
    LARGE_INTEGER t0;
    LARGE_INTEGER t1;
    LARGE_INTEGER freq;
    QueryPerformanceCounter(&t0);
    memset(g_c407.row, 0, sizeof(g_c407.row));
    g_c407.n = 0;
    g_c407.full = false;
    g_c407.built = true;
    uint32_t slots = 0;
    for (int32_t l = 0; l < s.levels && l < kMaxLevels; ++l) {
        for (int32_t a = 0; a < s.num[l]; ++a) {
            ++slots;
            if (s.copy[l][a] != nullptr) {
                c407_add(s.copy[l][a], l, a);
            }
        }
    }
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&freq);
    if (g_c407.builds++ < 8) {
        log::line("card407: %s name index built: %u placed actors of %u slots in %.0f us%s", side(), g_c407.n, slots,
                  static_cast<double>(t1.QuadPart - t0.QuadPart) * 1e6 / static_cast<double>(freq.QuadPart),
                  g_c407.full ? ", full: the finds walk the levels" : "");
    }
}
} // namespace

void c407_reset() {
    if (g_c407.finds != 0) {
        log::line("card407: %s name index since the last reset: %u finds, %u answered, %u builds", side(),
                  g_c407.finds, g_c407.answered, g_c407.builds);
    }
    g_c407.built = false;
    g_c407.finds = 0;
    g_c407.answered = 0;
    g_c407.builds = 0;
}

// One actor seen in a slot: a placed one with a printable name gets a row, or its row the new slot.
void c407_add(void* actor, int32_t level, int32_t index) {
    C407Index& x = g_c407;
    if (!x.built || x.full || !c50_placed(actor)) {
        return;
    }
    char name[kC407NameMax + 1];
    const int32_t len = c50_name(actor, name, sizeof(name));
    if (len <= 0) {
        return;
    }
    const uint64_t hash = c407_hash(name, len);
    for (uint32_t i = 0; i < kC407Slots; ++i) {
        C407Row& r = x.row[(hash + i) & (kC407Slots - 1)];
        if (r.hash == hash && r.actor == actor) {
            r.level = level;
            r.index = index;
            return;
        }
        if (r.hash != 0) {
            continue;
        }
        if (x.n >= kC407Max) {
            x.full = true;
            return;
        }
        ++x.n;
        r = C407Row{hash, actor, level, index};
        return;
    }
}

int32_t c407_named(const void* world, const C50NameRef& ref, C402New* out, int32_t max) {
    C407Index& x = g_c407;
    if (world == nullptr || ref.name_len <= 0 || ref.name_len > kC407NameMax) {
        return -1;
    }
    (void)c402_slots_step(world);
    if (!g_c402_slots.ok) {
        return -1;
    }
    ++x.finds;
    if (!x.built) {
        c407_build();
    }
    if (x.full) {
        return -1;
    }
    int32_t n = 0;
    for (uint32_t i = 0; i < kC407Slots; ++i) {
        const C407Row& r = x.row[(ref.hash + i) & (kC407Slots - 1)];
        if (r.hash == 0) {
            break;
        }
        if (r.hash != ref.hash || !c402_valid(C402Ref{r.actor, r.level, r.index}) ||
            !c50_name_is(r.actor, ref.name, ref.name_len) || !c50_placed(r.actor)) {
            continue;
        }
        if (n == max) {
            return -1;
        }
        out[n++] = C402New{r.level, r.index, r.actor};
    }
    x.answered += n > 0 ? 1u : 0u;
    return n;
}
void* c50_weapon(void* entry) {
    return entry != nullptr && is_a(entry, g_c50_weapon_class()) ? entry : nullptr;
}
// The inventory's weapons in Items order (16-byte entries, the weapon pointer first), checked against AADWeapon.
C50List c50_list(void* inventory) {
    C50List out{};
    out.equipped = -1;
    const auto* const items = inventory != nullptr ? static_cast<const uint8_t*>(read_ptr(inventory, kInventoryItems))
                                                   : nullptr;
    const int32_t num = items != nullptr ? read_i32(inventory, kInventoryItems + kArrayNum) : 0;
    const void* const equipped = items != nullptr ? g_get_equipped_weapon(inventory) : nullptr;
    for (int32_t i = 0; i < num && i < kC50Items; ++i) {
        void* const w = c50_weapon(read_ptr(items, static_cast<size_t>(i) * kC50ItemStride));
        if (w == nullptr) {
            ++out.skipped;
            continue;
        }
        out.equipped = w == equipped ? out.count : out.equipped;
        out.weapon[out.count++] = w;
    }
    return out;
}
// The inventory as Weapons text (card 312): "<equipped slot or -1>|<slot>=<class path>|..." in Items order, at most
// kC50Weapons. A slot is the weapon's position in Items (the game's weapon slot; see c50_slot_text). Returns the
// length (0 = no inventory); a weapon whose path does not fit is left out and logged.
int32_t c50_text(void* inventory, char* out, int32_t cap) {
    out[0] = '\0';
    if (inventory == nullptr) {
        return 0;
    }
    const auto* const items = static_cast<const uint8_t*>(read_ptr(inventory, kInventoryItems));
    const int32_t num = items != nullptr ? read_i32(inventory, kInventoryItems + kArrayNum) : 0;
    const void* const equipped = items != nullptr ? g_get_equipped_weapon(inventory) : nullptr;
    char body[kChanTextMax + 1] = {};
    int32_t used = 0;
    int32_t kept = 0;
    int32_t equip = -1;
    for (int32_t i = 0; i < num && i < kC50Items && kept < kC50Weapons; ++i) {
        void* const w = c50_weapon(read_ptr(items, static_cast<size_t>(i) * kC50ItemStride));
        if (w == nullptr) {
            continue;
        }
        char path[kC50PathMax + 1];
        const void* const def = read_ptr(w, kWeaponDefinitionClass);
        const int32_t p = c50_class_path(def, path, sizeof(path));
        const int n = p > 0 ? snprintf(body + used, sizeof(body) - static_cast<size_t>(used), "|%d=%s", i, path) : -1;
        if (n <= 0 || used + n > kChanTextMax - 8) {
            body[used] = '\0';
            if (c50_line()) {
                log::line("WARN card050: %s weapon %s (definition %s) in slot %d left out of the list: path %d chars",
                          side(), object_name(w).text, object_name(def).text, i, p);
            }
            continue;
        }
        used += n;
        equip = w == equipped ? i : equip;
        ++kept;
    }
    const int w = snprintf(out, static_cast<size_t>(cap), "%d%s", equip, body);
    return w > 0 && w < cap ? w : 0;
}
// Card 312: SendWeaponSwitchRequest reads the equipped index through the inventory's interface at +0xE8, vtable
// +0x20 (UADWeaponInventory::GetEquippedItemIndex, EquippedItemIndex); the same call is made here.
constexpr size_t kC312InventoryInterface = 0xE8;
constexpr size_t kC312EquippedIndexSlot = 0x20;
using C312EquippedIndexFn = int32_t (*)(void* iface);
int32_t c50_equipped_slot(void* inventory) {
    void* const iface = inventory != nullptr ? static_cast<uint8_t*>(inventory) + kC312InventoryInterface : nullptr;
    const void* const vtable = iface != nullptr ? read_ptr(iface, 0) : nullptr;
    const auto fn = vtable != nullptr ? reinterpret_cast<C312EquippedIndexFn>(read_ptr(vtable, kC312EquippedIndexSlot))
                                      : nullptr;
    return fn != nullptr ? fn(iface) : -1;
}
// Card 312: Items positions are the weapon slots (GetWeaponBySlot is GetWeaponInstanceFromIndex); RemoveItem nulls
// an entry in place, so the positions hold across removals and c50_list (which skips nulls) cannot show them.
int32_t c50_slot_text(void* inventory, char* out, int32_t cap) {
    out[0] = '\0';
    const auto* const items = inventory != nullptr ? static_cast<const uint8_t*>(read_ptr(inventory, kInventoryItems))
                                                   : nullptr;
    const int32_t num = items != nullptr ? read_i32(inventory, kInventoryItems + kArrayNum) : 0;
    int w = snprintf(out, static_cast<size_t>(cap), "%d", c50_equipped_slot(inventory));
    for (int32_t i = 0; i < num && i < kC50Items && w > 0 && w < cap; ++i) {
        const void* const weapon = c50_weapon(read_ptr(items, static_cast<size_t>(i) * kC50ItemStride));
        const int n = weapon != nullptr ? snprintf(out + w, static_cast<size_t>(cap - w), "|%d=%s", i,
                                                   object_name(read_ptr(weapon, kWeaponDefinitionClass)).text)
                                        : 0;
        w = n >= 0 && n < cap - w ? w + n : cap; // a cut text ends at the last whole entry
    }
    return w > 0 && w < cap ? w : static_cast<int32_t>(strlen(out));
}
// Host: the copy entry for a server copy PC; created when create is set and a slot is free.
C50Copy* c50_copy(int32_t pc_id, bool create) {
    for (int32_t i = 0; i < g_c50.copies; ++i) {
        if (g_c50.copy[i].pc_id == pc_id) {
            return &g_c50.copy[i];
        }
    }
    if (!create || g_c50.copies >= kC17Slots) {
        return nullptr;
    }
    C50Copy& c = g_c50.copy[g_c50.copies++];
    c = C50Copy{};
    c.pc_id = pc_id;
    c.pawn_id = -1;
    return &c;
}
// --- Card 050 client: apply the host's list ---
void* c50_resolve(const char* path) {
    wchar_t w[kC50PathMax + 1];
    const size_t n = strlen(path);
    if (n == 0 || n > static_cast<size_t>(kC50PathMax)) {
        return nullptr;
    }
    for (size_t i = 0; i < n; ++i) {
        w[i] = static_cast<wchar_t>(path[i]);
    }
    w[n] = L'\0';
    void* const meta = g_uclass_class();
    void* cls = g_static_find_object(meta, nullptr, w, false);
    if (cls == nullptr && g_static_load_object != nullptr) {
        cls = g_static_load_object(meta, nullptr, w, nullptr, kC29LoadFlags, nullptr, true, nullptr);
    }
    return cls;
}
// --- Card 050 step 3a: the client's Use relayed to its server copy's Interact ---
// Card 054: the relay table row of `target`, or null when its class is not relayed.
const C54Relay* c54_relay_row(const void* target) {
    for (int32_t i = 0; target != nullptr && i < kC54RelayRows; ++i) {
        if (is_a(target, (*kC54Relays[i].cls)())) {
            return &kC54Relays[i];
        }
    }
    return nullptr;
}
void c54_track(void* pawn, const char* what) {
    const ULONGLONG now = GetTickCount64();
    g_c54_track = C54Track{pawn, {}, what, now + kC54TrackMs, now};
    c31_weak_set(g_c54_track.weak, pawn);
}
// Card 054: the switch's enabled byte (interface +0x40, card 054 Addendum 1); -1 when `row` is not a switch.
int32_t c54_switch_enabled(const C54Relay* row, const void* iface) {
    return row->kind == C54Kind::Switch ? static_cast<const uint8_t*>(iface)[kC54SwitchEnabled] : -1;
}
// The placed actor named `name` or, with none, the nearest actor of the class named `name`. Null when neither.
// A class name is a class search (the nearest), even when an actor's object name is that same text (a runtime
// spawn, card 125): an object-name hit would return the first such actor every time. live: skip an actor of the class
// whose health component reports IsDead or health <= 0 (cards 130, 259: `kill actor` on a corpse kills nothing), and
// a pickup whose bEnabled is 0 (taken; card 244 b208).
void* c64_find_actor(const void* world, const char* name, const Vec3& from, const void* skip, bool live) {
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    void* best = nullptr;
    double best_d = 0.0;
    for (int32_t l = 0; levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const auto* const actors = levels[l] != nullptr ? static_cast<void* const*>(read_ptr(levels[l], kLevelActors)) : nullptr;
        const int32_t num = levels[l] != nullptr ? read_i32(levels[l], kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; actors != nullptr && a < num && a < kMaxLevelActors; ++a) {
            if (actors[a] == nullptr || actors[a] == skip) {
                continue;
            }
            const bool of_class = strcmp(class_name(actors[a]).text, name) == 0;
            if (!of_class && strcmp(prop_name(static_cast<const uint8_t*>(actors[a]) + kObjectName, 8).text, name) == 0) {
                return actors[a];
            }
            float health = 0.0f;
            const int dead = live && of_class ? c25_dead(actors[a], &health) : -1;
            if (dead == 1 || (dead == 0 && health <= 0.0f)) {
                continue; // card 259: dead flag or health <= 0; -1 (no health component) stays a candidate
            }
            if (live && of_class && g_c56_pickup_class != nullptr && is_a(actors[a], g_c56_pickup_class()) &&
                static_cast<const uint8_t*>(actors[a])[kPickupEnabledByte] == 0) {
                continue; // card 244 b208: a taken pickup (AADPickupBase::bEnabled 0) is no goal
            }
            const double d = vec_dist(actor_location(actors[a]), from);
            if (of_class && (best == nullptr || d < best_d)) {
                best = actors[a];
                best_d = d;
            }
        }
    }
    return best;
}
// Card 047: the install gates of the audit-only kHooksUbw entries. Each is set once by set_hook_gates(), after the
// card flags are read and before the install loop; an entry whose gate is false is resolved but never hooked.
bool g_gate_c40 = false;      // UBWCOOP_C40_DAMAGE_TRACE
bool g_gate_c41 = false;      // UBWCOOP_C41_ANIM_TRACE
bool g_gate_c23 = false;      // UBWCOOP_ENEMYAUDIT
bool g_gate_c23_c25 = false;  // UBWCOOP_ENEMYAUDIT or UBWCOOP_KILL_TROOPER
bool g_gate_c30 = false;      // UBWCOOP_KILL_PLAYER set
bool g_gate_c32 = false;      // UBWCOOP_SPAWN_MECH set
bool g_gate_c29_c32 = false;  // UBWCOOP_AUDIT_029 or UBWCOOP_SPAWN_MECH set
bool g_gate_c62 = false;      // UBWCOOP_C50_TEST (card 062's proof logs)
bool g_gate_c191 = false;     // UBWCOOP_CMD, UBWCOOP_C50_TEST or UBWCOOP_DIAG (card 191 end tick diagnostic)
bool g_gate_c62_diag = false; // UBWCOOP_C50_TEST or UBWCOOP_DIAG (CancelAbility: card 062 log + card 019 sprint rows)

// Installed after MH_Initialize, before the hook tables, and only with UBWCOOP_C65_LOG=1 (the Activate detour is in kHooksUbw).
bool c65_install(uint8_t* base, const IMAGE_NT_HEADERS64* nt) {
    const HookSpec hooks[] = {
        {&offsets::UADLevelTriggerReactionsComponent_Trigger, reinterpret_cast<void*>(&detour_c65_trigger),
         reinterpret_cast<void**>(&g_orig_c65_trigger)},
        {&offsets::UADLevelTriggerStatefulReactionsComponent_Trigger,
         reinterpret_cast<void*>(&detour_c65_stateful_trigger), reinterpret_cast<void**>(&g_orig_c65_stateful_trigger)},
        {&offsets::UADLevelTriggerComponent_execTrigger, reinterpret_cast<void*>(&detour_c65_exec_trigger),
         reinterpret_cast<void**>(&g_orig_c65_exec_trigger)},
        {&offsets::UActorComponent_execActivate, reinterpret_cast<void*>(&detour_c65_exec_activate),
         reinterpret_cast<void**>(&g_orig_c65_exec_activate)},
    };
    for (const HookSpec& h : hooks) {
        void* const target = code_address(base, nt, *h.sym);
        if (target == nullptr || !create_and_enable(target, h.detour, h.original, h.sym->name)) {
            return false;
        }
    }
    log::line("hooks: card 065 level-trigger log on (%zu detours)", sizeof(hooks) / sizeof(hooks[0]));
    return true;
}
// Client, before each AbilityInputTagPressed/Released: the host removes a spent grenade's spec, and that removal
// replicates, but the input tag bind c73_bind made does not go with it. AbilityInputTagPressed reads the spec of each
// bound handle without a null test (exe+0x4A0EB97), so handles of the tag with no spec are unbound first.
void c73_drop_stale(void* asc, const void* tag) {
    if (!g_client_session || asc == nullptr || tag == nullptr) {
        return;
    }
    const auto* const data = static_cast<const uint8_t*>(read_ptr(asc, kAscInputTagMap));
    const int32_t num = data != nullptr ? read_i32(asc, kAscInputTagMapNum) : 0;
    uint64_t want = 0;
    memcpy(&want, tag, sizeof(want));
    int32_t stale[kC73MaxBound];
    int32_t n_stale = 0;
    for (int32_t i = 0; i < num && i < kAscInputTagMapMax; ++i) {
        const uint8_t* const e = data + static_cast<size_t>(i) * kAscInputTagMapStride;
        uint64_t key = 0;
        memcpy(&key, e, sizeof(key));
        const auto* const handles = static_cast<const int32_t*>(read_ptr(e, 0x8));
        const int32_t count = key == want && handles != nullptr ? read_i32(e, 0x10) : 0;
        for (int32_t h = 0; h < count && h < kC73MaxBound && n_stale < kC73MaxBound; ++h) {
            if (c21_spec_for(asc, handles[h]) == nullptr) {
                stale[n_stale++] = handles[h];
            }
        }
    }
    for (int32_t i = 0; i < n_stale; ++i) {
        g_c73_unbind(asc, &stale[i]);
        log::line("card073: client t=%llu %s: handle %d has no spec (removed on the host), unbound",
                  static_cast<unsigned long long>(GetTickCount64()), fname_text(tag).text, stale[i]);
    }
}
// Host: a client's counts set on that client's own server copy only. Logged only when a count changed.
void c72_mirror_apply(void* pc, const char* text, const char* what) {
    void* const pawn = read_ptr(pc, kControllerPawn);
    const bool copy = is_a(pawn, g_player_mi_class()) && sprint_side(pawn) == kSideServerCopy;
    void* const inventory = copy ? read_ptr(pawn, kMiAmmoInventory) : nullptr;
    if (inventory == nullptr) {
        // No pawn at all is the map change race (the client mirrored from the old map's pawn after the host
        // travelled; 5 chain runs, the next map's pawn is seeded by card 073): a plain row. A pawn that is not a
        // server copy stays a WARN.
        log::line("%scard072: host: %s from %s with no server copy inventory (pawn %s)", pawn == nullptr ? "" : "WARN ",
                  what, object_name(pc).text, object_name(pawn).text);
        return;
    }
    char changes[512];
    c72_apply(inventory, text, true, what, changes, sizeof(changes));
    if (changes[0] != '\0') {
        log::line("card072: host t=%llu %s from %s (copy %s):%s", static_cast<unsigned long long>(GetTickCount64()),
                  what, object_name(pc).text, object_name(pawn).text, changes);
    }
}
// The nearest actor of class `cls` to loc, and its distance; null when none.
void* nearest_of_class(const void* world, const void* cls, const Vec3& loc, double* dist) {
    static C23List list;
    c23_collect(world, nullptr, cls, list);
    *dist = -1.0;
    void* best = nullptr;
    for (uint32_t i = 0; i < list.count; ++i) {
        const double d = vec_dist(list.e[i].loc, loc);
        if (best == nullptr || d < *dist) {
            best = list.e[i].actor;
            *dist = d;
        }
    }
    return best;
}
// The soldier for a leave "before" line: the mech's pilot property, else the remembered EnterMech soldier.
const void* c75_leave_soldier(const void* mech) {
    const void* const pilot = c29_controller(mech);
    for (int i = 0; i < kC75Pilots && pilot == nullptr; ++i) {
        if (g_c75_pilots[i].mech == mech) {
            return g_c75_pilots[i].soldier;
        }
    }
    return pilot;
}
// Both sides, from the AbilityInputTagPressed/Released detours: a mech input tag's press edge (the pressed call
// repeats every frame while held) and its release edge.
void c76_tag(const void* asc, const void* tag, bool pressed) {
    const void* const owner = read_ptr(asc, kComponentOwner);
    const void* const mech = c76_mech(owner);
    const ObjectName name = fname_text(tag);
    if (mech == nullptr || c21_is_axis_tag(name.text)) {
        return;
    }
    uint64_t key = 0;
    memcpy(&key, tag, sizeof(key));
    uint32_t slot = kC76Held;
    uint32_t free_slot = kC76Held;
    for (uint32_t i = 0; i < kC76Held; ++i) {
        slot = g_c76.held[i] == key ? i : slot;
        free_slot = g_c76.held[i] == 0 && free_slot == kC76Held ? i : free_slot;
    }
    if (pressed == (slot < kC76Held) || (pressed && free_slot == kC76Held)) {
        return; // a held press repeating, a release without a press, or too many tags held
    }
    g_c76.held[pressed ? free_slot : slot] = pressed ? key : 0;
    if (c76_note(kC76Tag)) {
        log::line("card076: %s t=%llu mech %s input tag %s %s (ASC owner %s)", side(), c76_now(),
                  object_name(mech).text, name.text, pressed ? "pressed" : "released", class_name(owner).text);
    }
}
// Installed after kHooksUe (ue/hooktable.cpp, whose last row is UGameEngine::Tick), each in this order.
const HookSpec kHooksUbw[] = {
    // Card 461 (diagnostic): the mover of a released rider's capsule; one compare of g_c461_armed outside its window.
    {&offsets::UPrimitiveComponent_MoveComponentImpl, reinterpret_cast<void*>(&detour_c461_move),
     reinterpret_cast<void**>(&g_orig_c461_move), &g_gate_c461_c539}, // card 539 counts ride on it
    // Card 416: the minimap marker interface wrappers; a teammate pawn is answered by the mod, all else unchanged.
    {&offsets::ISTMinimapMarkerInterface_Execute_GetIconTexture, reinterpret_cast<void*>(&detour_c416_icon),
     reinterpret_cast<void**>(&g_orig_c416_icon)},
    {&offsets::ISTMinimapMarkerInterface_Execute_GetWorldLocation, reinterpret_cast<void*>(&detour_c416_location),
     reinterpret_cast<void**>(&g_orig_c416_location)},
    {&offsets::ISTMinimapMarkerInterface_Execute_GetWorldYaw, reinterpret_cast<void*>(&detour_c416_yaw),
     reinterpret_cast<void**>(&g_orig_c416_yaw)},
    {&offsets::ISTMinimapMarkerInterface_Execute_ShouldRemainAtEdgeOfMinimap, reinterpret_cast<void*>(&detour_c416_edge),
     reinterpret_cast<void**>(&g_orig_c416_edge)},
    {&offsets::ISTMinimapMarkerInterface_Execute_ShouldShowOnMinimap, reinterpret_cast<void*>(&detour_c416_show),
     reinterpret_cast<void**>(&g_orig_c416_show)},
    {&offsets::USTPlayerUtilityLibrary_UpdateBindings, reinterpret_cast<void*>(&detour_update_bindings),
     reinterpret_cast<void**>(&g_orig_update_bindings)},
    {&offsets::ASTPlayerController_SetPlayer, reinterpret_cast<void*>(&detour_st_set_player),
     reinterpret_cast<void**>(&g_orig_st_set_player)},
    {&offsets::UGameplayStatics_GetGameMode, reinterpret_cast<void*>(&detour_get_game_mode),
     reinterpret_cast<void**>(&g_orig_get_game_mode)},
    {&offsets::ASTPlayerMechBase_AddMechInputContext, reinterpret_cast<void*>(&detour_add_mech_input),
     reinterpret_cast<void**>(&g_orig_add_mech_input)},
    {&offsets::ASTPlayerMechBase_RemoveMechInputContext, reinterpret_cast<void*>(&detour_remove_mech_input),
     reinterpret_cast<void**>(&g_orig_remove_mech_input)},
    {&offsets::ASTEncounterManagerBase_StartEncounter, reinterpret_cast<void*>(&detour_start_encounter),
     reinterpret_cast<void**>(&g_orig_start_encounter)},
    {&offsets::ASTEncounterManagerBase_ActivateCurrentWave, reinterpret_cast<void*>(&detour_activate_current_wave),
     reinterpret_cast<void**>(&g_orig_activate_current_wave)},
    // card 503 (diag): wave events
    {&offsets::ASTEncounterManagerBase_OnWaveNPCSpawned, reinterpret_cast<void*>(&detour_c503_spawned),
     reinterpret_cast<void**>(&g_orig_c503_spawned)},
    {&offsets::ASTEncounterManagerBase_OnWaveNPCKilled, reinterpret_cast<void*>(&detour_c503_killed),
     reinterpret_cast<void**>(&g_orig_c503_killed)},
    {&offsets::ASTEncounterManagerBase_OnWaveSpawnComplete, reinterpret_cast<void*>(&detour_c503_spawn_complete),
     reinterpret_cast<void**>(&g_orig_c503_spawn_complete)},
    {&offsets::ASTEncounterManagerBase_OnWaveEnd, reinterpret_cast<void*>(&detour_c503_wave_end),
     reinterpret_cast<void**>(&g_orig_c503_wave_end)},
    {&offsets::ASTEncounterManagerBase_TriggerNextWave, reinterpret_cast<void*>(&detour_c503_next_wave),
     reinterpret_cast<void**>(&g_orig_c503_next_wave)},
    {&offsets::ASTEncounterManagerBase_BP_OnEncounterEnded, reinterpret_cast<void*>(&detour_c503_encounter_ended),
     reinterpret_cast<void**>(&g_orig_c503_encounter_ended)},
    {&offsets::USTEncounterWaveBase_OnSpawnCancelled, reinterpret_cast<void*>(&detour_c503_cancelled),
     reinterpret_cast<void**>(&g_orig_c503_cancelled)},
    {&offsets::ASTSpawnerBase_PerformSpawn, reinterpret_cast<void*>(&detour_perform_spawn),
     reinterpret_cast<void**>(&g_orig_perform_spawn)},
    {&offsets::ASTSpawnerBase_QueueSpawnRequest, reinterpret_cast<void*>(&detour_queue_spawn),
     reinterpret_cast<void**>(&g_orig_queue_spawn)},
    {&offsets::UPlayerInput_IsPressed, reinterpret_cast<void*>(&detour_is_pressed),
     reinterpret_cast<void**>(&g_orig_is_pressed)},
    {&offsets::APlayerController_SetPawn, reinterpret_cast<void*>(&detour_pc_set_pawn),
     reinterpret_cast<void**>(&g_orig_pc_set_pawn)},
    {&offsets::AGameStateBase_OnRep_ReplicatedHasBegunPlay, reinterpret_cast<void*>(&detour_c138_on_rep),
     reinterpret_cast<void**>(&g_orig_c138_on_rep)}, // card 138: client BeginPlay dispatch waits for the pawn
    {&offsets::AController_OnRep_Pawn, reinterpret_cast<void*>(&detour_on_rep_pawn),
     reinterpret_cast<void**>(&g_orig_on_rep_pawn)},
    {&offsets::APlayerController_AcknowledgePossession, reinterpret_cast<void*>(&detour_acknowledge_possession),
     reinterpret_cast<void**>(&g_orig_acknowledge_possession)},
    {&offsets::APlayerController_ClientRestart_Implementation, reinterpret_cast<void*>(&detour_client_restart),
     reinterpret_cast<void**>(&g_orig_client_restart)},
    {&offsets::APlayerController_SetInputMode, reinterpret_cast<void*>(&detour_set_input_mode),
     reinterpret_cast<void**>(&g_orig_set_input_mode)},
    {&offsets::APlayerController_SetShowMouseCursor, reinterpret_cast<void*>(&detour_set_show_mouse_cursor),
     reinterpret_cast<void**>(&g_orig_set_show_mouse_cursor)},
    {&offsets::AController_SetIgnoreMoveInput, reinterpret_cast<void*>(&detour_set_ignore_move_input),
     reinterpret_cast<void**>(&g_orig_set_ignore_move_input), &g_gate_diag},
    {&offsets::IEnhancedInputSubsystemInterface_AddMappingContext, reinterpret_cast<void*>(&detour_add_mapping_context),
     reinterpret_cast<void**>(&g_orig_add_mapping_context)},
    {&offsets::UCommonUIActionRouterBase_ApplyUIInputConfig, reinterpret_cast<void*>(&detour_apply_ui_input_config),
     reinterpret_cast<void**>(&g_orig_apply_ui_input_config), &g_gate_diag},
    {&offsets::ASTPlayerController_HandleInputLook, reinterpret_cast<void*>(&detour_handle_input_look),
     reinterpret_cast<void**>(&g_orig_handle_input_look), &g_gate_diag},
    {&offsets::UGameViewportClient_InputAxis, reinterpret_cast<void*>(&detour_input_axis),
     reinterpret_cast<void**>(&g_orig_input_axis), &g_gate_diag},
    {&offsets::UCharacterMovementComponent_TickComponent, reinterpret_cast<void*>(&detour_cmc_tick),
     reinterpret_cast<void**>(&g_orig_cmc_tick)}, // card 019
    {&offsets::ASTNonPlayableCharacterBase_Tick, reinterpret_cast<void*>(&detour_c527_actor_tick),
     reinterpret_cast<void**>(&g_orig_c527_actor_tick), &g_c132_fx_log}, // card 527 item 2
    {&offsets::FSTBugProceduralAnimationData_UpdateFromNPC, reinterpret_cast<void*>(&detour_c533_update_from_npc),
     reinterpret_cast<void**>(&g_orig_c533_update)}, // card 533
    {&offsets::UADTargetComponent_TickComponent, reinterpret_cast<void*>(&detour_c532_tick),
     reinterpret_cast<void**>(&g_orig_c532_tick)}, // card 532: client target tick off
    {&offsets::UADTargetComponent_GetPrimaryTarget, reinterpret_cast<void*>(&detour_c532_get_primary),
     reinterpret_cast<void**>(&g_orig_c532_get_primary)}, // card 532: client shows the host's target
    {&offsets::USTSaveComponent_GetSaveId, reinterpret_cast<void*>(&detour_c531_save_id),
     reinterpret_cast<void**>(&g_orig_c531_save_id), &g_c531_on}, // card 531: save component cache (perf)
    {&offsets::UCharacterMovementComponent_FindFloor, reinterpret_cast<void*>(&detour_c535_find_floor),
     reinterpret_cast<void**>(&g_orig_c535_find_floor), &g_c535_on}, // card 535: client NPC floor skip (perf)
    // card 537: owned-component set changes bump a generation; these rows come before the cache row
    {&offsets::AActor_AddOwnedComponent, reinterpret_cast<void*>(&detour_c537_add),
     reinterpret_cast<void**>(&g_orig_c537_add), &g_c536_on},
    {&offsets::AActor_RemoveOwnedComponent, reinterpret_cast<void*>(&detour_c537_remove),
     reinterpret_cast<void**>(&g_orig_c537_remove), &g_c536_on},
    {&offsets::AActor_ResetOwnedComponents, reinterpret_cast<void*>(&detour_c537_reset),
     reinterpret_cast<void**>(&g_orig_c537_reset), &g_c536_on},
    {&offsets::AActor_FindComponentByClass, reinterpret_cast<void*>(&detour_c536_find),
     reinterpret_cast<void**>(&g_orig_c536_find), &g_c536_on}, // card 536: component lookup cache (perf)
    {&offsets::USTAttackComponent_DoesTargetMeetConditions, reinterpret_cast<void*>(&detour_c538_conditions),
     reinterpret_cast<void**>(&g_orig_c538_conditions), &g_c538_on}, // card 538: targeting repeat counts (perf diag)
    {&offsets::UWorld_SweepSingleByChannel, reinterpret_cast<void*>(&detour_c534<0>), reinterpret_cast<void**>(&g_orig_c534[0]),
     &g_c132_fx_log}, // card 534 diag
    {&offsets::UWorld_SweepMultiByChannel, reinterpret_cast<void*>(&detour_c534<1>), reinterpret_cast<void**>(&g_orig_c534[1]),
     &g_c132_fx_log}, // card 534 diag
    {&offsets::UWorld_SweepTestByChannel, reinterpret_cast<void*>(&detour_c534<2>), reinterpret_cast<void**>(&g_orig_c534[2]),
     &g_c132_fx_log}, // card 534 diag
    {&offsets::UWorld_SweepSingleByObjectType, reinterpret_cast<void*>(&detour_c534<3>), reinterpret_cast<void**>(&g_orig_c534[3]),
     &g_c132_fx_log}, // card 534 diag
    {&offsets::UWorld_SweepMultiByObjectType, reinterpret_cast<void*>(&detour_c534<4>), reinterpret_cast<void**>(&g_orig_c534[4]),
     &g_c132_fx_log}, // card 534 diag
    {&offsets::UWorld_LineTraceSingleByChannel, reinterpret_cast<void*>(&detour_c534<5>), reinterpret_cast<void**>(&g_orig_c534[5]),
     &g_c132_fx_log}, // card 534 diag
    {&offsets::UWorld_LineTraceMultiByChannel, reinterpret_cast<void*>(&detour_c534<6>), reinterpret_cast<void**>(&g_orig_c534[6]),
     &g_c132_fx_log}, // card 534 diag
    {&offsets::UWorld_LineTraceTestByChannel, reinterpret_cast<void*>(&detour_c534<7>), reinterpret_cast<void**>(&g_orig_c534[7]),
     &g_c132_fx_log}, // card 534 diag
    {&offsets::UWorld_OverlapAnyTestByChannel, reinterpret_cast<void*>(&detour_c534<8>), reinterpret_cast<void**>(&g_orig_c534[8]),
     &g_c132_fx_log}, // card 534 diag
    {&offsets::UWorld_OverlapMultiByObjectType, reinterpret_cast<void*>(&detour_c534<9>), reinterpret_cast<void**>(&g_orig_c534[9]),
     &g_c132_fx_log}, // card 534 diag
    {&offsets::UWorld_ComponentSweepMulti, reinterpret_cast<void*>(&detour_c534<10>), reinterpret_cast<void**>(&g_orig_c534[10]),
     &g_c132_fx_log}, // card 534 diag
    {&offsets::UWorld_ComponentOverlapMulti, reinterpret_cast<void*>(&detour_c534<11>), reinterpret_cast<void**>(&g_orig_c534[11]),
     &g_c132_fx_log}, // card 534 diag
    {&offsets::USceneComponent_UpdateOverlaps, reinterpret_cast<void*>(&detour_c534<12>), reinterpret_cast<void**>(&g_orig_c534[12]),
     &g_c132_fx_log}, // card 534 diag
    {&offsets::UKismetSystemLibrary_CapsuleTraceSingleForObjects, reinterpret_cast<void*>(&detour_c545_capsule),
     reinterpret_cast<void**>(&g_orig_c545_capsule), &g_c132_fx_log}, // card 545 diag
    {&offsets::UKismetSystemLibrary_SphereOverlapComponents, reinterpret_cast<void*>(&detour_c545_sphere),
     reinterpret_cast<void**>(&g_orig_c545_sphere), &g_c132_fx_log}, // card 545 diag
    {&offsets::UPrimitiveComponent_UpdateOverlapsImpl, reinterpret_cast<void*>(&detour_c548_prim),
     reinterpret_cast<void**>(&g_orig_c548_impl[0]), &g_c132_fx_log}, // card 548 diag
    {&offsets::USkinnedMeshComponent_UpdateOverlapsImpl, reinterpret_cast<void*>(&detour_c548_skinned),
     reinterpret_cast<void**>(&g_orig_c548_impl[1]), &g_c132_fx_log}, // card 548 diag
    {&offsets::USkeletalMeshComponent_UpdateOverlapsImpl, reinterpret_cast<void*>(&detour_c548_skeletal),
     reinterpret_cast<void**>(&g_orig_c548_impl[2]), &g_c132_fx_log}, // card 548 diag
    {&offsets::USkeletalMeshComponent_TickComponent, reinterpret_cast<void*>(&detour_c527_mesh_tick),
     reinterpret_cast<void**>(&g_orig_c527_mesh_tick), &g_c132_fx_log}, // card 527 item 2
    {&offsets::UADSpriteAnimatorComponent_TickComponent, reinterpret_cast<void*>(&detour_c527_sprite_tick),
     reinterpret_cast<void**>(&g_orig_c527_sprite_tick), &g_c132_fx_log}, // card 527 item 2
    {&offsets::UCharacterMovementComponent_PerformMovement, reinterpret_cast<void*>(&detour_perform_movement),
     reinterpret_cast<void**>(&g_orig_perform_movement), &g_gate_diag},
    {&offsets::UEnhancedPlayerInput_InputKey, reinterpret_cast<void*>(&detour_player_input_key),
     reinterpret_cast<void**>(&g_orig_player_input_key)},
    {&offsets::ASTPlayerController_HandleInputMove, reinterpret_cast<void*>(&detour_handle_input_move),
     reinterpret_cast<void**>(&g_orig_handle_input_move), &g_gate_diag},
    {&offsets::AController_SetControlRotation, reinterpret_cast<void*>(&detour_set_control_rotation),
     reinterpret_cast<void**>(&g_orig_set_control_rotation)},
    {&offsets::AController_ClientSetRotation_Implementation, reinterpret_cast<void*>(&detour_client_set_rotation),
     reinterpret_cast<void**>(&g_orig_client_set_rotation), &g_gate_diag},
    {&offsets::UCharacterMovementComponent_ClientAdjustPosition_Implementation,
     reinterpret_cast<void*>(&detour_client_adjust_position), reinterpret_cast<void**>(&g_orig_client_adjust_position)},
     // card 295: ungated, so the play build counts corrections after a revive
    {&offsets::UCharacterMovementComponent_ReplicateMoveToServer, reinterpret_cast<void*>(&detour_replicate_move),
     reinterpret_cast<void**>(&g_orig_replicate_move), &g_gate_diag},
    {&offsets::UCharacterMovementComponent_ServerCheckClientError,
     reinterpret_cast<void*>(&detour_server_check_client_error),
     reinterpret_cast<void**>(&g_orig_server_check_client_error)},
    {&offsets::UCharacterMovementComponent_ServerMove_PerformMovement,
     reinterpret_cast<void*>(&detour_server_move_perform), reinterpret_cast<void**>(&g_orig_server_move_perform), &g_gate_diag},
    // Card 013
    {&offsets::UCharacterMovementComponent_StartNewPhysics, reinterpret_cast<void*>(&detour_start_new_physics), reinterpret_cast<void**>(&g_orig_start_new_physics), &g_gate_diag},
    {&offsets::UCharacterMovementComponent_PhysWalking, reinterpret_cast<void*>(&detour_phys_walking), reinterpret_cast<void**>(&g_orig_phys_walking), &g_gate_diag},
    {&offsets::UMovementComponent_SafeMoveUpdatedComponent, reinterpret_cast<void*>(&detour_safe_move), reinterpret_cast<void**>(&g_orig_safe_move), &g_gate_diag},
    {&offsets::UMovementComponent_MoveUpdatedComponentImpl, reinterpret_cast<void*>(&detour_move_impl), reinterpret_cast<void**>(&g_orig_move_impl), &g_gate_diag},
    {&offsets::AActor_SetActorEnableCollision, reinterpret_cast<void*>(&detour_set_actor_collision), reinterpret_cast<void**>(&g_orig_set_actor_collision)},
    {&offsets::AActor_execSetActorEnableCollision, reinterpret_cast<void*>(&detour_exec_set_actor_collision), reinterpret_cast<void**>(&g_orig_exec_set_actor_collision), &g_gate_diag},
    {&offsets::UPrimitiveComponent_SetCollisionEnabled, reinterpret_cast<void*>(&detour_set_collision_enabled), reinterpret_cast<void**>(&g_orig_set_collision_enabled)},
    {&offsets::UPrimitiveComponent_SetCollisionProfileName, reinterpret_cast<void*>(&detour_set_collision_profile), reinterpret_cast<void**>(&g_orig_set_collision_profile)}, // card 237
    // Card 132
    {&offsets::UCharacterMovementComponent_SetMovementMode, reinterpret_cast<void*>(&detour_set_movement_mode), reinterpret_cast<void**>(&g_orig_set_movement_mode)},
    {&offsets::UCharacterMovementComponent_SimulateMovement, reinterpret_cast<void*>(&detour_simulate_movement), reinterpret_cast<void**>(&g_orig_simulate_movement)},
    {&offsets::ASTPlayerTransportDropship_EnterDropship, reinterpret_cast<void*>(&detour_enter_dropship), reinterpret_cast<void**>(&g_orig_enter_dropship)},
    {&offsets::ASTPlayerTransportDropship_DescendToLandCompleted, reinterpret_cast<void*>(&detour_descend_completed), reinterpret_cast<void**>(&g_orig_descend_completed)},
    {&offsets::ASTProceduralDropship_StartAction, reinterpret_cast<void*>(&detour_start_action), reinterpret_cast<void**>(&g_orig_start_action), nullptr}, // card 449: ungated, the client calls the original
    {&offsets::ASTProceduralDropship_SetState, reinterpret_cast<void*>(&detour_set_state), reinterpret_cast<void**>(&g_orig_set_state)}, // card 132: the dropship relay, always
    {&offsets::ASTProceduralDropship_SkipFlight, reinterpret_cast<void*>(&detour_skip_flight), reinterpret_cast<void**>(&g_orig_skip_flight), &g_gate_diag},
    {&offsets::UMovieSceneSequencePlayer_Play, reinterpret_cast<void*>(&detour_sequence_play),
     reinterpret_cast<void**>(&g_orig_sequence_play)}, // card 130: always, play build too
    {&offsets::USTGameInstance_RequestGamePause, reinterpret_cast<void*>(&detour_request_pause), reinterpret_cast<void**>(&g_orig_request_pause)},
    {&offsets::UGameplayStatics_SetGamePaused, reinterpret_cast<void*>(&detour_c96_set_game_paused), reinterpret_cast<void**>(&g_orig_c96_set_game_paused)},
    {&offsets::USTGameInstance_RequestGameUnpause, reinterpret_cast<void*>(&detour_request_unpause), reinterpret_cast<void**>(&g_orig_request_unpause)},
    {&offsets::USTGameInstance_EndLevel, reinterpret_cast<void*>(&detour_end_level), reinterpret_cast<void**>(&g_orig_end_level)}, // card 260: always
    {&offsets::ASTPlayerTransportDropship_Tick, reinterpret_cast<void*>(&detour_transport_tick), reinterpret_cast<void**>(&g_orig_transport_tick), &g_gate_diag},
    {&offsets::ASTProceduralDropship_TickBehaviours, reinterpret_cast<void*>(&detour_tick_behaviours), reinterpret_cast<void**>(&g_orig_tick_behaviours), &g_gate_diag},
    {&offsets::ASTProceduralDropship_TickSplineTravel, reinterpret_cast<void*>(&detour_tick_spline_travel), reinterpret_cast<void**>(&g_orig_tick_spline_travel), &g_gate_diag},
    {&offsets::ASTProceduralDropship_ExecuteBeginDescentBehaviour, reinterpret_cast<void*>(&detour_begin_descent), reinterpret_cast<void**>(&g_orig_begin_descent), &g_gate_diag},
    {&offsets::UEnhancedPlayerInput_ProcessActionMappingEvent, reinterpret_cast<void*>(&detour_process_mapping), reinterpret_cast<void**>(&g_orig_process_mapping), &g_gate_diag},
    {&offsets::UEnhancedPlayerInput_ApplyModifiers, reinterpret_cast<void*>(&detour_apply_modifiers), reinterpret_cast<void**>(&g_orig_apply_modifiers), &g_gate_diag},
    {&offsets::UInputModifier_ModifyRaw, reinterpret_cast<void*>(&detour_modify_raw), reinterpret_cast<void**>(&g_orig_modify_raw), &g_gate_diag},
    {&offsets::FTriggerStateTracker_EvaluateTriggers, reinterpret_cast<void*>(&detour_evaluate_triggers), reinterpret_cast<void**>(&g_orig_evaluate_triggers), &g_gate_diag},
    // Card 014
    {&offsets::ASTPlayerCharacter_PostInitializeComponents, reinterpret_cast<void*>(&detour_post_init), reinterpret_cast<void**>(&g_orig_post_init)},
    {&offsets::ASTPlayerCharacter_PossessedBy, reinterpret_cast<void*>(&detour_possessed_by), reinterpret_cast<void**>(&g_orig_possessed_by)},
    {&offsets::EIBinding_ExecuteValue, reinterpret_cast<void*>(&detour_execute_value), reinterpret_cast<void**>(&g_orig_execute_value)}, // card 155: ungated (pl_execute)
    {&offsets::EIBinding_ExecuteVoid, reinterpret_cast<void*>(&detour_execute_void), reinterpret_cast<void**>(&g_orig_execute_void)}, // card 155: ungated (pl_execute)
    {&offsets::EIBinding_ExecuteDynamic, reinterpret_cast<void*>(&detour_execute_dynamic), reinterpret_cast<void**>(&g_orig_execute_dynamic)}, // card 155: ungated (pl_execute)
    // Card 025
    {&offsets::ASTNonPlayableCharacterBase_HandleDeathEvent, reinterpret_cast<void*>(&detour_npc_death_event), reinterpret_cast<void**>(&g_orig_npc_death_event)},
    // Card 319 reopen (b249): the middle HUD bug display's marker handlers, counted per side
    {&offsets::USTBugProximityAreaWidget_OnNewMarker, reinterpret_cast<void*>(&detour_c319_new_marker), reinterpret_cast<void**>(&g_orig_c319_new_marker), &g_gate_diag},
    {&offsets::USTBugProximityAreaWidget_OnRemoveMarker, reinterpret_cast<void*>(&detour_c319_remove_marker), reinterpret_cast<void**>(&g_orig_c319_remove_marker), &g_gate_diag},
    {&offsets::ASTNonPlayableCharacterBase_StartDying, reinterpret_cast<void*>(&detour_npc_start_dying), reinterpret_cast<void**>(&g_orig_npc_start_dying)},
    {&offsets::ASTNonPlayableCharacterBase_HandleHealthValueChangedEvent, reinterpret_cast<void*>(&detour_npc_health_changed), reinterpret_cast<void**>(&g_orig_npc_health_changed)}, // card 506: ungated (host health edge)
    {&offsets::ASTNonPlayableCharacterBase_BP_OnRevived, reinterpret_cast<void*>(&detour_npc_revived), reinterpret_cast<void**>(&g_orig_npc_revived), &g_gate_c23_c25},
    // Card 065: the range-target relay (and its UBWCOOP_C65_LOG line)
    {&offsets::UActorComponent_Activate, reinterpret_cast<void*>(&detour_c65_activate),
     reinterpret_cast<void**>(&g_orig_c65_activate)},
    // Card 071: pickup ammo on a server copy goes to its client
    {&offsets::USTWeaponUtilityLibrary_AddAmmunitions, reinterpret_cast<void*>(&detour_c71_add_ammunitions),
     reinterpret_cast<void**>(&g_orig_c71_add_ammunitions)},
    // Card 073: a server copy's grenade items go to its client; the client binds its grenade input to the spec
    {&offsets::UADGASBasicsUtilityLibrary_AddAbilityToActor, reinterpret_cast<void*>(&detour_c73_ability),
     reinterpret_cast<void**>(&g_orig_c73_ability)},
    {&offsets::UADAbilityTask_FireProjectile_Activate, reinterpret_cast<void*>(&detour_c74_activate),
     reinterpret_cast<void**>(&g_orig_c74_activate)}, // card 074
    {&offsets::UADProjectilePooler_SpawnProjectile, reinterpret_cast<void*>(&detour_c74_spawn),
     reinterpret_cast<void**>(&g_orig_c74_spawn)}, // card 074; card 132 counts every launch
    {&offsets::USTGrenadeThrowGameplayAbility_OnGrenadeSpawned, reinterpret_cast<void*>(&detour_c74_spawned),
     reinterpret_cast<void**>(&g_orig_c74_spawned)}, // card 074
    {&offsets::UAbilityTask_SpawnActor_BeginSpawningActor, reinterpret_cast<void*>(&detour_c169_begin_spawning),
     reinterpret_cast<void**>(&g_orig_c169_begin_spawning)}, // card 169: the client's own held visuals
    {&offsets::AActor_K2_DestroyActor, reinterpret_cast<void*>(&detour_c169_destroy),
     reinterpret_cast<void**>(&g_orig_c169_destroy)}, // card 169: a held visual's on-screen state at its end
    {&offsets::USTGameInstance_TransitionToLevel, reinterpret_cast<void*>(&detour_c381_transition),
     reinterpret_cast<void**>(&g_transition_to_level)}, // card 381: not a client's to run in the host's session
    {&offsets::USTGameInstance_TransitionToLatestLevel, reinterpret_cast<void*>(&detour_c381_transition_latest),
     reinterpret_cast<void**>(&g_orig_c381_latest)}, // card 381
    // card 322 step 1 (diagnostic): radio, music, dilation and out-of-bounds rows, both sides; part A: the three
    // conversation hooks and their exec thunks are always on (host-owned conversation starts)
    {&offsets::USTConversationSubsystem_StartConversation, reinterpret_cast<void*>(&detour_c322_start_conversation),
     reinterpret_cast<void**>(&g_orig_c322_start_conversation), nullptr}, // card 322 part A: always on
    {&offsets::USTConversationSubsystem_AddConversationToQueue, reinterpret_cast<void*>(&detour_c322_add_conversation),
     reinterpret_cast<void**>(&g_orig_c322_add_conversation), nullptr}, // card 322 part A: always on
    {&offsets::USTConversationSubsystem_StartConversationFromList, reinterpret_cast<void*>(&detour_c322_start_from_list),
     reinterpret_cast<void**>(&g_orig_c322_start_from_list), nullptr}, // card 322 part A: always on
    {&offsets::USTMusicManager_TriggerMusic, reinterpret_cast<void*>(&detour_c322_trigger_music),
     reinterpret_cast<void**>(&g_orig_c322_trigger_music), &g_gate_c62_diag},
    {&offsets::UADTimeDilationSubsystem_PlayTimeDilation, reinterpret_cast<void*>(&detour_c322_play_dilation),
     reinterpret_cast<void**>(&g_orig_c322_play_dilation), &g_gate_c62_diag},
    {&offsets::UADGameplayAbility_TimeDilation_TimeDilationStart, reinterpret_cast<void*>(&detour_c322_dilation_start),
     reinterpret_cast<void**>(&g_orig_c322_dilation_start), &g_gate_c62_diag},
    {&offsets::USTOutOfBoundTriggerComponent_OnOutOfBoundsEntered, reinterpret_cast<void*>(&detour_c322_oob_entered),
     reinterpret_cast<void**>(&g_orig_c322_oob_entered), &g_gate_c62_diag},
    {&offsets::USTOutOfBoundTriggerComponent_OnOutOfBoundsExited, reinterpret_cast<void*>(&detour_c322_oob_exited),
     reinterpret_cast<void**>(&g_orig_c322_oob_exited), &g_gate_c62_diag},
    {&offsets::USTOutOfBoundTriggerComponent_KillOwningActor, reinterpret_cast<void*>(&detour_c360_kill_owning_actor),
     reinterpret_cast<void**>(&g_orig_c360_kill_owning_actor), nullptr}, // card 360: always on, the client skips its own expiry kill
    {&offsets::USTCombatUtilityLibrary_InstaKillTarget, reinterpret_cast<void*>(&detour_c361_insta_kill),
     reinterpret_cast<void**>(&g_insta_kill), nullptr}, // card 361: always on; the mod's own calls use the trampoline
    {&offsets::ASTHazardousZoneBase_TickMoveToNewPostion, reinterpret_cast<void*>(&detour_c361_hazard_tick),
     reinterpret_cast<void**>(&g_orig_c361_hazard_tick), nullptr}, // card 361: card 321 hazard half (host)
    {&offsets::ASTProceduralDropshipTroops_CanTakeoff, reinterpret_cast<void*>(&detour_c449_can_takeoff),
     reinterpret_cast<void**>(&g_orig_c449_can_takeoff), nullptr}, // card 449
    {&offsets::APlayerController_PawnLeavingGame, reinterpret_cast<void*>(&detour_c447_pawn_leaving),
     reinterpret_cast<void**>(&g_orig_c447_pawn_leaving), nullptr}, // card 447: seat leave before the pawn goes
    {&offsets::UADBarkSubsystem_RequestBark, reinterpret_cast<void*>(&detour_c451_request_bark),
     reinterpret_cast<void**>(&g_orig_c451_request_bark), nullptr}, // card 451: bark range from any player
    {&offsets::FBarkDelegate_Broadcast, reinterpret_cast<void*>(&detour_c451_broadcast),
     reinterpret_cast<void**>(&g_orig_c451_broadcast), nullptr}, // card 451 part 2: bark subtitles to clients
    {&offsets::ASTSaveableLevelScriptActor_CallFunctionWithDelay, reinterpret_cast<void*>(&detour_c493_delay),
     reinterpret_cast<void**>(&g_orig_c493_delay), nullptr}, // card 493: cave-in rerun re-arm skip
    {&offsets::GetSizeForDevID, reinterpret_cast<void*>(&detour_c303_get_size),
     reinterpret_cast<void**>(&g_orig_c303_get_size), nullptr}, // card 303 step 3: display size cached, always on
    {&offsets::USTConversationSubsystem_execStartConversation, reinterpret_cast<void*>(&detour_c322_exec<0>),
     reinterpret_cast<void**>(&g_orig_c322_exec[0]), nullptr}, // card 322 part A
    {&offsets::USTConversationSubsystem_execAddConversationToQueue, reinterpret_cast<void*>(&detour_c322_exec<1>),
     reinterpret_cast<void**>(&g_orig_c322_exec[1]), nullptr}, // card 322 part A
    {&offsets::USTConversationSubsystem_execStartConversationFromList, reinterpret_cast<void*>(&detour_c322_exec<2>),
     reinterpret_cast<void**>(&g_orig_c322_exec[2]), nullptr}, // card 322 part A
    {&offsets::USTMusicManager_execTriggerMusic, reinterpret_cast<void*>(&detour_c322_exec<3>), reinterpret_cast<void**>(&g_orig_c322_exec[3]),
     &g_gate_c62_diag},
    {&offsets::UADTimeDilationSubsystem_execPlayTimeDilation, reinterpret_cast<void*>(&detour_c322_exec<4>),
     reinterpret_cast<void**>(&g_orig_c322_exec[4]), &g_gate_c62_diag},
    {&offsets::UADTimeDilationBlueprintFunctionLibrary_execPlayTimeDilation, reinterpret_cast<void*>(&detour_c322_exec<5>),
     reinterpret_cast<void**>(&g_orig_c322_exec[5]), &g_gate_c62_diag},
    {&offsets::USTFedScoreSubsystem_execAddFedScore, reinterpret_cast<void*>(&detour_c322_exec<6>),
     reinterpret_cast<void**>(&g_orig_c322_exec[6]), &g_gate_c62_diag}, // card 491 b228: the Blueprint behind a score call
    {&offsets::UAbilitySystemComponent_ServerTryActivateAbilityWithEventData,
     reinterpret_cast<void*>(&detour_c174_server_try),
     reinterpret_cast<void**>(&g_orig_c174_server_try)}, // card 174: the client's own tactical support send skipped
    {&offsets::AADProjectile_LifeSpanExpired, reinterpret_cast<void*>(&detour_c74_life),
     reinterpret_cast<void**>(&g_orig_c74_proj[0])}, // card 074
    {&offsets::AADProjectile_ReleaseToPool, reinterpret_cast<void*>(&detour_c74_release),
     reinterpret_cast<void**>(&g_orig_c74_proj[2])}, // card 132: a pooled round leaves the round table
    {&offsets::AADProjectile_OnHit, reinterpret_cast<void*>(&detour_c132_on_hit),
     reinterpret_cast<void**>(&g_orig_c132_on_hit)}, // card 132: a relayed round's impact
    {&offsets::UPackageMapClient_SerializeNewActor, reinterpret_cast<void*>(&detour_c434_new_actor),
     reinterpret_cast<void**>(&g_orig_c434_new_actor)}, // card 434 step 4 (diagnostic)
    {&offsets::UActorChannel_CleanUp, reinterpret_cast<void*>(&detour_c434_cleanup),
     reinterpret_cast<void**>(&g_orig_c434_cleanup)}, // card 434 step 4 (diagnostic)
    {&offsets::UPackageMapClient_SerializeObject, reinterpret_cast<void*>(&detour_c434_serialize_object),
     reinterpret_cast<void**>(&g_orig_c434_serialize_object)}, // card 434 (diagnostic, b139)
    {&offsets::AADProjectile_execOnOverlapBegin, reinterpret_cast<void*>(&detour_c157_overlap),
     reinterpret_cast<void**>(&g_orig_c157_overlap)}, // card 157: a round's overlap deals its damage without OnHit
    {&offsets::ASTPlayerCharacter_OnDamageCaused, reinterpret_cast<void*>(&detour_c157_damage_caused),
     reinterpret_cast<void**>(&g_orig_c157_damage_caused)}, // card 157 diagnostic: the hit marker's event
    {&offsets::ASTPlayerCharacter_OnDeathCaused, reinterpret_cast<void*>(&detour_c166_death_caused),
     reinterpret_cast<void**>(&g_orig_c166_death_caused)}, // card 166: a server copy's kill marker
    {&offsets::UUserWidget_PlayAnimation, reinterpret_cast<void*>(&detour_c168_play_animation),
     reinterpret_cast<void**>(&g_orig_c168_play_animation)}, // card 168: the hit marker's visible change
    {&offsets::UAbilitySystemComponent_ExecuteGameplayCue, reinterpret_cast<void*>(&detour_c74_cue),
     reinterpret_cast<void**>(&g_orig_c74_cue)}, // card 132: the cue depth a round's capture skips
    {&offsets::USTParticleEffectManager_STSpawnSystemAtLocation, reinterpret_cast<void*>(&detour_c74_st_niagara),
     reinterpret_cast<void**>(&g_orig_c74_st_niagara)}, // card 074: the explosion effect the client replays
    {&offsets::USTCommonUtilityLibrary_STSpawnNiagraSystemAtLocation, reinterpret_cast<void*>(&detour_c132_util_niagara),
     reinterpret_cast<void**>(&g_orig_c132_util_niagara)}, // card 132 4a: the client spawns what the GameMode gate drops
    {&offsets::UGameplayStatics_PlaySoundAtLocation, reinterpret_cast<void*>(&detour_c74_play_sound),
     reinterpret_cast<void**>(&g_orig_c74_play_sound)}, // card 074; card 136 capture point 1
    {&offsets::IEnhancedInputSubsystemInterface_RemoveMappingContext, reinterpret_cast<void*>(&detour_pl_remove_mapping_context),
     reinterpret_cast<void**>(&g_orig_pl_remove_mapping_context)}, // card 155
    {&offsets::UUserWidget_AddToViewport, reinterpret_cast<void*>(&detour_pl_add_to_viewport),
     reinterpret_cast<void**>(&g_orig_pl_add_to_viewport)}, // card 155
    {&offsets::UWidget_RemoveFromParent, reinterpret_cast<void*>(&detour_pl_remove_from_parent),
     reinterpret_cast<void**>(&g_orig_pl_remove_from_parent)}, // card 155
    {&offsets::UAudioComponent_PlayInternal, reinterpret_cast<void*>(&detour_pl_play_internal),
     reinterpret_cast<void**>(&g_orig_pl_play_internal)}, // card 155: remote-player sounds
    {&offsets::UCharacterMovementComponent_HandleImpact, reinterpret_cast<void*>(&detour_pl_impact),
     reinterpret_cast<void**>(&g_orig_pl_impact)}, // card 155: door blocks
    {&offsets::UGameplayStatics_SpawnSoundAtLocation, reinterpret_cast<void*>(&detour_c74_spawn_sound),
     reinterpret_cast<void**>(&g_orig_c74_spawn_sound)}, // card 136 capture point 1 (was card 074 log only)
    {&offsets::UNiagaraFunctionLibrary_SpawnSystemAtLocation, reinterpret_cast<void*>(&detour_c74_niagara),
     reinterpret_cast<void**>(&g_orig_c74_niagara)}, // card 136 capture point 2 (was card 074 log only)
    {&offsets::UAbilitySystemComponent_ExecuteGameplayCueContext, reinterpret_cast<void*>(&detour_c136_cue_context),
     reinterpret_cast<void**>(&g_orig_c136_cue_context)}, // card 136 capture point 4
    {&offsets::UGameplayCueManager_ExecuteGameplayCue_NonReplicated, reinterpret_cast<void*>(&detour_c136_cue_local),
     reinterpret_cast<void**>(&g_orig_c136_cue_local)}, // card 136 capture point 4
    {&offsets::UGameplayCueManager_HandleGameplayCue, reinterpret_cast<void*>(&detour_c136_cue_handle),
     reinterpret_cast<void**>(&g_orig_c136_cue_handle)}, // card 136 run 6: engine cue copies (client), cue instigator
    {&offsets::UAnimInstance_TriggerSingleAnimNotify, reinterpret_cast<void*>(&detour_c136_anim_notify),
     reinterpret_cast<void**>(&g_orig_c136_anim_notify)}, // card 136: anim notify effect owner
    {&offsets::UNiagaraComponent_Activate, reinterpret_cast<void*>(&detour_c136_activate),
     reinterpret_cast<void**>(&g_orig_c136_activate)}, // card 136 capture point 6
    {&offsets::UNiagaraFunctionLibrary_execSpawnSystemAtLocation, reinterpret_cast<void*>(&detour_c409_spawn_thunk),
     reinterpret_cast<void**>(&g_orig_c409_spawn_thunk)}, // card 409 step 2
    {&offsets::UGameplayStatics_execSpawnSoundAtLocation, reinterpret_cast<void*>(&detour_c418_sound_thunk),
     reinterpret_cast<void**>(&g_orig_c418_sound_thunk)}, // card 418
    {&offsets::UNiagaraComponent_Deactivate, reinterpret_cast<void*>(&detour_c136_deactivate),
     reinterpret_cast<void**>(&g_orig_c136_deactivate)}, // card 136 capture point 6
    {&offsets::UNiagaraComponent_DeactivateImmediate, reinterpret_cast<void*>(&detour_c136_deactivate_now),
     reinterpret_cast<void**>(&g_orig_c136_deactivate_now)}, // card 136 capture point 6
    {&offsets::USTCombatUtilityLibrary_TrySpawnDamageZone, reinterpret_cast<void*>(&detour_c74_zone),
     reinterpret_cast<void**>(&g_orig_c74_zone)}, // card 074: the incendiary's fire zone the client copies
    {&offsets::USTItemInventory_AddInventoryItem_Internal, reinterpret_cast<void*>(&detour_c73_add),
     reinterpret_cast<void**>(&g_orig_c73_add)},
    {&offsets::USTItemInventory_RemoveItemByDefinition, reinterpret_cast<void*>(&detour_c73_remove),
     reinterpret_cast<void**>(&g_orig_c73_remove)},
    {&offsets::USTItemInventory_AddItemByDefinition, reinterpret_cast<void*>(&detour_c354_add),
     reinterpret_cast<void**>(&g_orig_c354_add)}, // card 354: the client's own pickup and loadout adds
    {&offsets::USTAmmunitionInventory_AddAmmo, reinterpret_cast<void*>(&detour_c69_add),
     reinterpret_cast<void**>(&g_orig_c69_add)}, // card 333: the client's own loadout ammo add (card 069 log too)
    // Card 038
    {&offsets::USTBugAnimPhysComponent_OnDeath, reinterpret_cast<void*>(&detour_bug_anim_death), reinterpret_cast<void**>(&g_orig_bug_anim_death)},
    {&offsets::USTGoreComponent_OnDeath, reinterpret_cast<void*>(&detour_gore_death), reinterpret_cast<void**>(&g_orig_gore_death)},
    {&offsets::USkeletalMeshComponent_SetSimulatePhysics, reinterpret_cast<void*>(&detour_c132_set_simulate), reinterpret_cast<void**>(&g_orig_c132_set_simulate)},
    {&offsets::ASTGorePartBase_Impart, reinterpret_cast<void*>(&detour_c137_impart_base), reinterpret_cast<void**>(&g_orig_c137_impart_base)}, // card 137 6b
    {&offsets::ASTGorePartMesh_Impart, reinterpret_cast<void*>(&detour_c137_impart_mesh), reinterpret_cast<void**>(&g_orig_c137_impart_mesh)}, // card 137 6b
    {&offsets::ASTHazardousZoneBase_DamageActor, reinterpret_cast<void*>(&detour_c137_zone_damage), reinterpret_cast<void**>(&g_orig_c137_zone_damage)}, // card 137 6c
    {&offsets::USTSkeletalAnimatorComponent_SetShowingStaticMesh_Internal, reinterpret_cast<void*>(&detour_c132_show_static), reinterpret_cast<void**>(&g_orig_c132_show_static)}, // card 132 note 3
    // hb51 senior point 3: who stops a dead NPC's body 0 simulating (log only)
    {&offsets::USkeletalMeshComponent_SetAllBodiesSimulatePhysics, reinterpret_cast<void*>(&detour_c132_all_bodies), reinterpret_cast<void**>(&g_orig_c132_all_bodies)},
    {&offsets::USkeletalMeshComponent_SetAllBodiesBelowSimulatePhysics, reinterpret_cast<void*>(&detour_c132_bodies_below), reinterpret_cast<void**>(&g_orig_c132_bodies_below)},
    {&offsets::USkeletalMeshComponent_ResetAllBodiesSimulatePhysics, reinterpret_cast<void*>(&detour_c132_reset_bodies), reinterpret_cast<void**>(&g_orig_c132_reset_bodies)},
    {&offsets::USkeletalMeshComponent_TermArticulated, reinterpret_cast<void*>(&detour_c132_term_articulated), reinterpret_cast<void**>(&g_orig_c132_term_articulated)},
    {&offsets::USkeletalMeshComponent_InitArticulated, reinterpret_cast<void*>(&detour_c132_init_articulated), reinterpret_cast<void**>(&g_orig_c132_init_articulated)},
    {&offsets::UPrimitiveComponent_execSetSimulatePhysics, reinterpret_cast<void*>(&detour_c132_exec_set_simulate), reinterpret_cast<void**>(&g_orig_c132_exec_set_simulate)}, // card 132
    {&offsets::USceneComponent_SetHiddenInGame, reinterpret_cast<void*>(&detour_c132_set_hidden), reinterpret_cast<void**>(&g_orig_c132_set_hidden)}, // card 132
    {&offsets::USceneComponent_execSetHiddenInGame, reinterpret_cast<void*>(&detour_c132_exec_set_hidden), reinterpret_cast<void**>(&g_orig_c132_exec_set_hidden)}, // card 132
    {&offsets::USTGoreComponent_OnDamage, reinterpret_cast<void*>(&detour_c132_gore_damage), reinterpret_cast<void**>(&g_orig_c132_gore_damage)}, // card 132 hit blood
    {&offsets::ASTNonPlayableCharacterBase_TryPlayDeathAnimation, reinterpret_cast<void*>(&detour_try_play_death), reinterpret_cast<void**>(&g_orig_try_play_death)},
    {&offsets::ASTNonPlayableCharacterBase_ReceiveBaseStateChanged, reinterpret_cast<void*>(&detour_c269_state), reinterpret_cast<void**>(&g_orig_c269_state)}, // card 269
    {&offsets::ASTEnemyBase_StartDying, reinterpret_cast<void*>(&detour_enemy_start_dying), reinterpret_cast<void**>(&g_orig_enemy_start_dying), &g_gate_diag},
    {&offsets::USTMetaGame_EnemyKilledByGameplayTag, reinterpret_cast<void*>(&detour_enemy_killed), reinterpret_cast<void**>(&g_orig_enemy_killed), &g_gate_diag},
    {&offsets::ASTEnemyBase_ReleaseAllTickets, reinterpret_cast<void*>(&detour_release_tickets), reinterpret_cast<void**>(&g_orig_release_tickets), &g_gate_diag},
    {&offsets::ASTAIAwarenessSystem_DeregisterAI, reinterpret_cast<void*>(&detour_deregister_ai), reinterpret_cast<void**>(&g_orig_deregister_ai), &g_gate_diag},
    {&offsets::ASTNonPlayableCharacterBase_TryTakeCorpseTicket, reinterpret_cast<void*>(&detour_corpse_ticket), reinterpret_cast<void**>(&g_orig_corpse_ticket), &g_gate_diag},
    // Card 039
    {&offsets::ASTNonPlayableCharacterBase_StartOverkillState, reinterpret_cast<void*>(&detour_start_overkill), reinterpret_cast<void**>(&g_orig_start_overkill)}, // card 357: always, the client skip
    {&offsets::USTCombatUtilityLibrary_SpawnGoreResponse, reinterpret_cast<void*>(&detour_spawn_gore), reinterpret_cast<void**>(&g_orig_spawn_gore)},
    {&offsets::USTCombatUtilityLibrary_SpawnGoreDecals, reinterpret_cast<void*>(&detour_spawn_gore_decals), reinterpret_cast<void**>(&g_orig_spawn_gore_decals)},
    {&offsets::USTDecalManager_SpawnDecalAttached, reinterpret_cast<void*>(&detour_spawn_decal_attached), reinterpret_cast<void**>(&g_orig_spawn_decal_attached)},
    {&offsets::USTDecalManager_SpawnDecalAtLocation, reinterpret_cast<void*>(&detour_spawn_decal_at), reinterpret_cast<void**>(&g_orig_spawn_decal_at)},
    {&offsets::UWorld_OverlapMultiByChannel, reinterpret_cast<void*>(&detour_c46_overlap_multi), reinterpret_cast<void**>(&g_orig_c46_overlap_multi)}, // card 046 diagnosis
    {&offsets::ASTNonPlayableCharacterBase_TryPlayOverkillAnimation, reinterpret_cast<void*>(&detour_try_play_overkill), reinterpret_cast<void**>(&g_orig_try_play_overkill), &g_gate_c23},
    {&offsets::ASTNonPlayableCharacterBase_ReleaseCorpseTicket, reinterpret_cast<void*>(&detour_release_corpse), reinterpret_cast<void**>(&g_orig_release_corpse), &g_gate_diag},
    {&offsets::ASTNonPlayableCharacterBase_HandleDying, reinterpret_cast<void*>(&detour_handle_dying), reinterpret_cast<void**>(&g_orig_handle_dying)}, // card 131: always, the client calls it
    {&offsets::ASTNonPlayableCharacterBase_PlayBark, reinterpret_cast<void*>(&detour_npc_play_bark), reinterpret_cast<void**>(&g_orig_npc_play_bark)}, // card 131
    {&offsets::USTCommonUtilityLibrary_PlayBarkOnNPCNearThePlayer, reinterpret_cast<void*>(&detour_bark_near_player), reinterpret_cast<void**>(&g_orig_bark_near_player)}, // card 131
    {&offsets::ASTNonPlayableCharacterBase_UpdateColliders, reinterpret_cast<void*>(&detour_update_colliders), reinterpret_cast<void**>(&g_orig_update_colliders), &g_gate_c23},
    {&offsets::UADHealthComponent_OverkillDamageEffectReceived, reinterpret_cast<void*>(&detour_overkill_effect), reinterpret_cast<void**>(&g_orig_overkill_effect)},
    {&offsets::USTGoreComponent_OnOverkill, reinterpret_cast<void*>(&detour_gore_overkill), reinterpret_cast<void**>(&g_orig_gore_overkill)},
    {&offsets::ASTNonPlayableCharacterBase_EndPlay, reinterpret_cast<void*>(&detour_npc_end_play), reinterpret_cast<void**>(&g_orig_npc_end_play), &g_gate_c23},
    // Card 016
    {&offsets::UEnhancedPlayerInput_EvaluateInputDelegates, reinterpret_cast<void*>(&detour_evaluate_delegates), reinterpret_cast<void**>(&g_orig_evaluate_delegates), &g_gate_diag},
    {&offsets::ASTPlayerController_HandleInputTag_Pressed, reinterpret_cast<void*>(&detour_pc_tag_pressed), reinterpret_cast<void**>(&g_orig_pc_tag_pressed), &g_gate_diag},
    {&offsets::ASTPlayerController_HandleInputTag_Released, reinterpret_cast<void*>(&detour_pc_tag_released), reinterpret_cast<void**>(&g_orig_pc_tag_released), &g_gate_diag},
    {&offsets::ASTPlayerCharacter_HandleInputTag_Pressed, reinterpret_cast<void*>(&detour_char_tag_pressed), reinterpret_cast<void**>(&g_orig_char_tag_pressed), &g_gate_diag},
    {&offsets::UADAbilitySystemComponent_AbilityInputTagPressed, reinterpret_cast<void*>(&detour_asc_tag_pressed), reinterpret_cast<void**>(&g_orig_asc_tag_pressed)},
    {&offsets::UAbilitySystemComponent_TryActivateAbility, reinterpret_cast<void*>(&detour_try_activate), reinterpret_cast<void**>(&g_orig_try_activate), &g_gate_diag},
    {&offsets::UAbilitySystemComponent_InternalTryActivateAbility, reinterpret_cast<void*>(&detour_internal_try_activate), reinterpret_cast<void**>(&g_orig_internal_try_activate)},
    // Card 095: placed doors opened by the level script, relayed to the clients by name
    {&offsets::ASTDoor_OpenDoor, reinterpret_cast<void*>(&detour_c95_open), reinterpret_cast<void**>(&g_orig_c95_open)},
    {&offsets::ASTDoor_CloseDoor, reinterpret_cast<void*>(&detour_c95_close), reinterpret_cast<void**>(&g_orig_c95_close)},
    {&offsets::ASTDoor_SetDoorActive, reinterpret_cast<void*>(&detour_c95_active), reinterpret_cast<void**>(&g_orig_c95_active)},
    // Card 094 (log only)
    {&offsets::UGameplayAbility_EndAbility, reinterpret_cast<void*>(&detour_c94_end_ability), reinterpret_cast<void**>(&g_orig_c94_end_ability), &g_gate_c94_diag},
    {&offsets::UGameplayAbility_CommitAbility, reinterpret_cast<void*>(&detour_c94_commit), reinterpret_cast<void**>(&g_orig_c94_commit), &g_gate_c94},
    {&offsets::USTAbilityTaskMantleSearch_SearchForMantleLocation, reinterpret_cast<void*>(&detour_c94_mantle_search), reinterpret_cast<void**>(&g_orig_c94_mantle_search), &g_gate_c94},
    {&offsets::UAbilitySystemComponent_HandleGameplayEvent, reinterpret_cast<void*>(&detour_c94_gameplay_event), reinterpret_cast<void**>(&g_orig_c94_gameplay_event)}, // card 104: always (server copy policy)
    {&offsets::UAbilitySystemComponent_MonitoredTagChanged, reinterpret_cast<void*>(&detour_c94_tag_changed), reinterpret_cast<void**>(&g_orig_c94_tag_changed), &g_gate_c94},
    {&offsets::UAbilitySystemComponent_ClientEndAbility_Implementation, reinterpret_cast<void*>(&detour_c94_client_end), reinterpret_cast<void**>(&g_orig_c94_client_end), &g_gate_c94},
    {&offsets::USTAbilityTaskLerpActorAlongCurve_Tick_LerpActorLocation, reinterpret_cast<void*>(&detour_c101_lerp_tick), reinterpret_cast<void**>(&g_orig_c101_lerp_tick)},
    {&offsets::USTAbilityTaskLerpActorAlongCurve_EndLocationLerp, reinterpret_cast<void*>(&detour_c101_lerp_end), reinterpret_cast<void**>(&g_orig_c101_lerp_end)}, // card 101: always (mantle window)
    {&offsets::UAbilitySystemComponent_InternalServerTryActivateAbility, reinterpret_cast<void*>(&detour_server_try_activate), reinterpret_cast<void**>(&g_orig_server_try_activate)},
    // Card 024
    {&offsets::UAbilitySystemComponent_ServerSetInputPressed_Implementation, reinterpret_cast<void*>(&detour_server_input_pressed), reinterpret_cast<void**>(&g_orig_server_input_pressed), &g_gate_diag},
    {&offsets::UAbilitySystemComponent_ServerSetInputReleased_Implementation, reinterpret_cast<void*>(&detour_server_input_released), reinterpret_cast<void**>(&g_orig_server_input_released), &g_gate_diag},
    {&offsets::UAbilitySystemComponent_ServerEndAbility_Implementation, reinterpret_cast<void*>(&detour_server_end_ability), reinterpret_cast<void**>(&g_orig_server_end_ability), &g_gate_diag},
    {&offsets::UAbilitySystemComponent_ServerCancelAbility_Implementation, reinterpret_cast<void*>(&detour_server_cancel_ability), reinterpret_cast<void**>(&g_orig_server_cancel_ability), &g_gate_diag},
    {&offsets::AGameModeBase_Logout, reinterpret_cast<void*>(&detour_logout), reinterpret_cast<void**>(&g_orig_logout)},
    {&offsets::ASTPlayerCharacterMI_GetTargetLocation_Implementation, reinterpret_cast<void*>(&detour_target_location), reinterpret_cast<void**>(&g_orig_target_location)},
    {&offsets::APlayerController_DeprojectScreenPositionToWorld, reinterpret_cast<void*>(&detour_deproject), reinterpret_cast<void**>(&g_orig_deproject)},
    {&offsets::UADAbilitySystemComponent_AbilityInputTagReleased, reinterpret_cast<void*>(&detour_asc_tag_released), reinterpret_cast<void**>(&g_orig_asc_tag_released)},
    {&offsets::UAbilitySystemComponent_ClientActivateAbilityFailed, reinterpret_cast<void*>(&detour_client_activate_failed), reinterpret_cast<void**>(&g_orig_client_activate_failed), &g_gate_diag},
    {&offsets::UAbilitySystemComponent_ClientActivateAbilitySucceed, reinterpret_cast<void*>(&detour_client_activate_succeed), reinterpret_cast<void**>(&g_orig_client_activate_succeed), &g_gate_diag},
    {&offsets::ACharacter_Jump, reinterpret_cast<void*>(&detour_character_jump), reinterpret_cast<void**>(&g_orig_character_jump), &g_gate_c94_diag},
    {&offsets::ASTPlayerCharacterMI_Jump, reinterpret_cast<void*>(&detour_mi_jump), reinterpret_cast<void**>(&g_orig_mi_jump), &g_gate_c94_diag},
    {&offsets::ACharacter_CanJump, reinterpret_cast<void*>(&detour_can_jump), reinterpret_cast<void**>(&g_orig_can_jump), &g_gate_c94_diag},
    {&offsets::ASTPlayerCharacter_CanJumpInternal, reinterpret_cast<void*>(&detour_can_jump_internal), reinterpret_cast<void**>(&g_orig_can_jump_internal), &g_gate_c94_diag},
    {&offsets::ACharacter_CheckJumpInput, reinterpret_cast<void*>(&detour_check_jump_input), reinterpret_cast<void**>(&g_orig_check_jump_input), &g_gate_c94_diag},
    {&offsets::USTSprintAbility_HandleSprintChange, reinterpret_cast<void*>(&detour_sprint_change), reinterpret_cast<void**>(&g_orig_sprint_change), &g_gate_diag},
    {&offsets::UAbilitySystemComponent_GiveAbility, reinterpret_cast<void*>(&detour_give_ability), reinterpret_cast<void**>(&g_orig_give_ability), &g_gate_diag},
    {&offsets::UADAbilitySystemComponent_BindInputTagToAbilitySpec, reinterpret_cast<void*>(&detour_bind_input_tag), reinterpret_cast<void**>(&g_orig_bind_input_tag)}, // card 047: ungated, card 018 and 073 bind through the detour
    {&offsets::UADGASBasicsUtilityLibrary_InitFeaturesFromDefinition, reinterpret_cast<void*>(&detour_init_features), reinterpret_cast<void**>(&g_orig_init_features), &g_gate_diag},
    {&offsets::UADAbilitySystemComponent_AddAttributeSetFromData, reinterpret_cast<void*>(&detour_add_attribute_set), reinterpret_cast<void**>(&g_orig_add_attribute_set)},
    {&offsets::USTAttributeSet_PlayerCamera_PostAttributeChange, reinterpret_cast<void*>(&detour_camera_post_change), reinterpret_cast<void**>(&g_orig_camera_post_change), &g_gate_diag},
    // Card 018
    {&offsets::UAbilitySystemComponent_OnRep_SpawnedAttributes, reinterpret_cast<void*>(&detour_onrep_spawned_attributes), reinterpret_cast<void**>(&g_orig_onrep_spawned_attributes)},
    {&offsets::UWidgetBlueprintLibrary_execSetInputMode_UIOnlyEx, reinterpret_cast<void*>(&detour_exec_ui_only), reinterpret_cast<void**>(&g_orig_exec_ui_only)},
    {&offsets::UAbilitySystemComponent_OnRep_ActivateAbilities, reinterpret_cast<void*>(&detour_onrep_activate_abilities), reinterpret_cast<void**>(&g_orig_onrep_activate_abilities)},
    // Card 019
    {&offsets::USTSprintAbility_ActivateAbility, reinterpret_cast<void*>(&detour_sprint_activate), reinterpret_cast<void**>(&g_orig_sprint_activate), &g_gate_diag},
    {&offsets::USTSprintAbility_EndAbility, reinterpret_cast<void*>(&detour_sprint_end), reinterpret_cast<void**>(&g_orig_sprint_end), &g_gate_diag},
    {&offsets::UGameplayAbility_CancelAbility, reinterpret_cast<void*>(&detour_cancel_ability), reinterpret_cast<void**>(&g_orig_cancel_ability), &g_gate_c62_diag},
    {&offsets::USTAttributeSet_Movement_PostAttributeChange, reinterpret_cast<void*>(&detour_movement_post_change), reinterpret_cast<void**>(&g_orig_movement_post_change), &g_gate_diag},
    {&offsets::UFunction_Invoke, reinterpret_cast<void*>(&detour_invoke), reinterpret_cast<void**>(&g_orig_invoke), &g_gate_diag},
    // Card 020
    {&offsets::USTWeaponUtilityLibrary_ApplyDynamicZeroThresholdAdjustment, reinterpret_cast<void*>(&detour_zero_adjust), reinterpret_cast<void**>(&g_orig_zero_adjust)},
    {&offsets::UADAbilityTask_FireProjectile_FireProjectile, reinterpret_cast<void*>(&detour_fire_projectile), reinterpret_cast<void**>(&g_orig_fire_projectile)},
    {&offsets::USTWeaponFireAbility_Fire, reinterpret_cast<void*>(&detour_weapon_fire), reinterpret_cast<void**>(&g_orig_weapon_fire)}, // card 181
    {&offsets::UADInteractorComponent_Interact, reinterpret_cast<void*>(&detour_interact), reinterpret_cast<void**>(&g_orig_interact)},
    {&offsets::UADInteractorComponent_ReleaseInteraction, reinterpret_cast<void*>(&detour_release_interaction), reinterpret_cast<void**>(&g_orig_release_interaction)},
    {&offsets::UADInteractorComponent_TraceForInteractable, reinterpret_cast<void*>(&detour_trace_interactable), reinterpret_cast<void**>(&g_orig_trace_interactable)}, // card 081
    // Card 021
    {&offsets::USTWeaponInventory_GiveWeaponByClass, reinterpret_cast<void*>(&detour_give_by_class), reinterpret_cast<void**>(&g_orig_give_by_class)},
    {&offsets::USTWeaponInventory_GiveWeaponByInstance, reinterpret_cast<void*>(&detour_give_by_instance), reinterpret_cast<void**>(&g_orig_give_by_instance), &g_gate_diag},
    {&offsets::USTWeaponUtilityLibrary_AddWeapon, reinterpret_cast<void*>(&detour_add_weapon), reinterpret_cast<void**>(&g_orig_add_weapon), nullptr}, // card 267: ungated
    {&offsets::USTWeaponInventory_LoadInventory, reinterpret_cast<void*>(&detour_load_inventory), reinterpret_cast<void**>(&g_orig_load_inventory), &g_gate_diag},
    {&offsets::USTWeaponUtilityLibrary_execAddWeapon, reinterpret_cast<void*>(&detour_exec_add_weapon), reinterpret_cast<void**>(&g_orig_exec_add_weapon), nullptr}, // card 267: ungated
    {&offsets::USTWeaponUtilityLibrary_execAddWeaponByTag, reinterpret_cast<void*>(&detour_exec_add_weapon_by_tag), reinterpret_cast<void**>(&g_orig_exec_add_weapon_by_tag), nullptr}, // card 267: ungated
    {&offsets::UADWeaponInventory_execGiveWeaponByClass, reinterpret_cast<void*>(&detour_exec_give_by_class), reinterpret_cast<void**>(&g_orig_exec_give_by_class), nullptr}, // card 267: ungated
    {&offsets::UADWeaponInventory_execGiveWeaponByInstance, reinterpret_cast<void*>(&detour_exec_give_by_instance), reinterpret_cast<void**>(&g_orig_exec_give_by_instance), &g_gate_diag},
    {&offsets::USTWeaponUtilityLibrary_execDropCurrentWeapon, reinterpret_cast<void*>(&detour_exec_drop_current_weapon), reinterpret_cast<void**>(&g_orig_exec_drop_current_weapon), nullptr}, // card 267
    {&offsets::USTWeaponUtilityLibrary_execDropWeapon, reinterpret_cast<void*>(&detour_exec_drop_weapon), reinterpret_cast<void**>(&g_orig_exec_drop_weapon), nullptr}, // card 267
    {&offsets::USTWeaponUtilityLibrary_execDropWeaponByTag, reinterpret_cast<void*>(&detour_exec_drop_weapon_by_tag), reinterpret_cast<void**>(&g_orig_exec_drop_weapon_by_tag), nullptr}, // card 267
    {&offsets::UADFactionComponent_execSetFactionTag, reinterpret_cast<void*>(&detour_c310_exec_faction), reinterpret_cast<void**>(&g_orig_c310_exec_faction), nullptr}, // card 310
    {&offsets::USTSquadLeaderComponent_execSetEnabled, reinterpret_cast<void*>(&detour_c310_exec_squad), reinterpret_cast<void**>(&g_orig_c310_exec_squad), nullptr}, // card 310
    {&offsets::USTTacticalSupportComponent_execSetCanTacticalSupportBeCharged, reinterpret_cast<void*>(&detour_c310_exec_charge), reinterpret_cast<void**>(&g_orig_c310_exec_charge), nullptr}, // card 310
    {&offsets::USTItemInventory_execRemoveItemByDefinition, reinterpret_cast<void*>(&detour_c310_exec_item), reinterpret_cast<void**>(&g_orig_c310_exec_item), nullptr}, // card 310
    {&offsets::USTWeaponUtilityLibrary_execAddAmmunition, reinterpret_cast<void*>(&detour_c310_exec_ammo), reinterpret_cast<void**>(&g_orig_c310_exec_ammo), nullptr}, // card 310
    {&offsets::UAbilitySystemBlueprintLibrary_execAddLooseGameplayTags, reinterpret_cast<void*>(&detour_c310_exec_tag_add), reinterpret_cast<void**>(&g_orig_c310_exec_tag_add), nullptr}, // card 310
    {&offsets::UAbilitySystemBlueprintLibrary_execRemoveLooseGameplayTags, reinterpret_cast<void*>(&detour_c310_exec_tag_remove), reinterpret_cast<void**>(&g_orig_c310_exec_tag_remove), nullptr}, // card 310
    {&offsets::UADFactionComponent_SetFactionTag, reinterpret_cast<void*>(&detour_c310_set_faction), reinterpret_cast<void**>(&g_orig_c310_set_faction), nullptr}, // card 310
    {&offsets::USTSquadLeaderComponent_SetEnabled, reinterpret_cast<void*>(&detour_c310_set_squad), reinterpret_cast<void**>(&g_orig_c310_set_squad), nullptr}, // card 310
    {&offsets::USTTacticalSupportComponent_SetCanTacticalSupportBeCharged, reinterpret_cast<void*>(&detour_c310_set_charge), reinterpret_cast<void**>(&g_orig_c310_set_charge), nullptr}, // card 310
    {&offsets::USTWeaponUtilityLibrary_AddAmmunition, reinterpret_cast<void*>(&detour_c310_add_ammo), reinterpret_cast<void**>(&g_orig_c310_add_ammo), nullptr}, // card 310
    {&offsets::FGameplayTagCountContainer_UpdateTagMap_Internal, reinterpret_cast<void*>(&detour_c310_count_update), reinterpret_cast<void**>(&g_orig_c310_count_update), nullptr}, // card 310
    {&offsets::UAbilitySystemComponent_UpdateTagMap_Internal, reinterpret_cast<void*>(&detour_c310_asc_update), reinterpret_cast<void**>(&g_orig_c310_asc_update), nullptr}, // card 310
    {&offsets::ASTPlayerCharacter_AssignStartingInventories, reinterpret_cast<void*>(&detour_assign_starting), reinterpret_cast<void**>(&g_orig_assign_starting)},
    // card 029: log-only vehicle detours
    {&offsets::ASTPlayerMechBase_EnterMech, reinterpret_cast<void*>(&detour_c75_enter_mech), reinterpret_cast<void**>(&g_orig_enter_mech), nullptr},
    {&offsets::ASTPlayerMechBase_LeaveMech, reinterpret_cast<void*>(&detour_c75_leave_mech), reinterpret_cast<void**>(&g_orig_leave_mech), nullptr},
    {&offsets::ASTPlayerController_StartBoardingTheMech, reinterpret_cast<void*>(&detour_c255_start_boarding), reinterpret_cast<void**>(&g_orig_c255_start_boarding), nullptr}, // card 255
    // card 032: mech spawn and enter audit (log only)
    {&offsets::ASTPlayerMechBase_CanInteract, reinterpret_cast<void*>(&detour_c452_mech_can_interact), reinterpret_cast<void**>(&g_orig_c452_mech_can_interact), nullptr}, // card 452
    {&offsets::ASTPlayerMechBase_OnInteraction, reinterpret_cast<void*>(&detour_c75_mech_on_interaction), reinterpret_cast<void**>(&g_orig_mech_on_interaction), nullptr},
    {&offsets::UWorld_SpawnActorTransform, reinterpret_cast<void*>(&detour_c75_world_spawn), reinterpret_cast<void**>(&g_orig_c75_world_spawn), nullptr}, // card 075
    {&offsets::USTWeaponUtilityLibrary_SpawnWeaponPickup, reinterpret_cast<void*>(&detour_c132_spawn_weapon_pickup), reinterpret_cast<void**>(&g_orig_c132_spawn_weapon_pickup), nullptr}, // card 132
    {&offsets::USTWeaponUtilityLibrary_RemoveWeaponFromOwnerInventory, reinterpret_cast<void*>(&detour_c132_remove_weapon), reinterpret_cast<void**>(&g_orig_c132_remove_weapon), nullptr}, // card 132
    {&offsets::UWorld_DestroyActor, reinterpret_cast<void*>(&detour_c98_destroy), reinterpret_cast<void**>(&g_orig_c98_destroy)}, // card 098
    {&offsets::AActor_SetActorHiddenInGame, reinterpret_cast<void*>(&detour_c98_hidden), reinterpret_cast<void**>(&g_orig_c98_hidden)},
    {&offsets::UTimelineComponent_Play, reinterpret_cast<void*>(&detour_c98_timeline_play), reinterpret_cast<void**>(&g_orig_c98_timeline_play)},
    {&offsets::UTimelineComponent_PlayFromStart, reinterpret_cast<void*>(&detour_c98_timeline_from_start), reinterpret_cast<void**>(&g_orig_c98_timeline_from_start)},
    {&offsets::UTimelineComponent_Reverse, reinterpret_cast<void*>(&detour_c98_timeline_reverse), reinterpret_cast<void**>(&g_orig_c98_timeline_reverse)},
    {&offsets::UTimelineComponent_ReverseFromEnd, reinterpret_cast<void*>(&detour_c98_timeline_reverse_from_end), reinterpret_cast<void**>(&g_orig_c98_timeline_reverse_from_end)},
    {&offsets::UTimelineComponent_Stop, reinterpret_cast<void*>(&detour_c98_timeline_stop), reinterpret_cast<void**>(&g_orig_c98_timeline_stop)},
    {&offsets::UTimelineComponent_SetPlayRate, reinterpret_cast<void*>(&detour_c261_set_play_rate), reinterpret_cast<void**>(&g_orig_c261_set_play_rate)}, // card 261
    {&offsets::AActor_AttachToComponent, reinterpret_cast<void*>(&detour_c98_attach), reinterpret_cast<void**>(&g_orig_c98_attach)},
    {&offsets::ASTSatchelBuildingBase_ReceiveOnSatchelExploded, reinterpret_cast<void*>(&detour_c98_satchel), reinterpret_cast<void**>(&g_orig_c98_satchel)},
    {&offsets::FHealthCompDamageEventDelegate_Broadcast, reinterpret_cast<void*>(&detour_c163_broadcast), reinterpret_cast<void**>(&g_orig_c163_broadcast)}, // card 163
    {&offsets::IADInteractableInterface_OnTimedInteractionCompleted, reinterpret_cast<void*>(&detour_c133_completed), reinterpret_cast<void**>(&g_orig_c133_completed)}, // card 133
    {&offsets::ASTSatchelPlacementSite_StartTimer, reinterpret_cast<void*>(&detour_c133_start), reinterpret_cast<void**>(&g_orig_c133_start)}, // card 418
    {&offsets::IADInteractableInterface_OnTimedInteractionStarted, reinterpret_cast<void*>(&detour_c456_started), reinterpret_cast<void**>(&g_orig_c456_started)}, // card 456
    {&offsets::IADInteractableInterface_OnTimedInteractionInterrupted, reinterpret_cast<void*>(&detour_c456_interrupted), reinterpret_cast<void**>(&g_orig_c456_interrupted)}, // card 456
    {&offsets::IADInteractableInterface_OnTimedInteractionReleased, reinterpret_cast<void*>(&detour_c456_released), reinterpret_cast<void**>(&g_orig_c456_released)}, // card 456
    {&kC248TickInteractable, reinterpret_cast<void*>(&detour_c248_tick), reinterpret_cast<void**>(&g_orig_c248_tick)}, // card 248
    {&kC191EndTick, reinterpret_cast<void*>(&detour_c191_end_tick), reinterpret_cast<void**>(&g_orig_c191_end_tick), &g_gate_c191}, // card 191 diagnostic
    // card 099: the player 0 gate; its original slot is the mod's own call slot, so mod calls reach the trampoline
    {&offsets::UGameplayStatics_GetPlayerController, reinterpret_cast<void*>(&detour_c99_get_pc), reinterpret_cast<void**>(&g_get_player_controller)},
    {&offsets::UPrimitiveComponent_BeginComponentOverlap, reinterpret_cast<void*>(&detour_c102_overlap), reinterpret_cast<void**>(&g_orig_c102_overlap)},
    {&offsets::ASTDoor_HandleCharactersInMovePath, reinterpret_cast<void*>(&detour_c321_door_path), reinterpret_cast<void**>(&g_orig_c321_door_path)}, // card 321
    {&offsets::ASTFallZonesBase_OnBeginOverlap, reinterpret_cast<void*>(&detour_c338_overlap), reinterpret_cast<void**>(&g_orig_c338_overlap)}, // card 338
    {&offsets::ASTFallZonesBase_RepositionPlayer, reinterpret_cast<void*>(&detour_c338_reposition), reinterpret_cast<void**>(&g_orig_c338_reposition)}, // card 338
    {&offsets::ASTFallZonesBase_EndReposition, reinterpret_cast<void*>(&detour_c338_end), reinterpret_cast<void**>(&g_orig_c338_end)}, // card 338
    {&offsets::AActor_DispatchBeginPlay, reinterpret_cast<void*>(&detour_c99_begin_play), reinterpret_cast<void**>(&g_orig_c99_begin_play)}, // card 130 item 13
    {&offsets::USTCombatUtilityLibrary_ApplyDamageFromImpactHitResult, reinterpret_cast<void*>(&detour_c74_damage), reinterpret_cast<void**>(&g_orig_c74_damage)},
    {&offsets::UADTargetComponent_SelectPrimaryTarget, reinterpret_cast<void*>(&detour_c103_select), reinterpret_cast<void**>(&g_orig_c103_select)}, // card 103 log, card 122 scale
    {&offsets::UADTargetComponent_SetPrimaryTarget, reinterpret_cast<void*>(&detour_c103_set), reinterpret_cast<void**>(&g_orig_c103_set)},
    {&offsets::USTEncounterWaveBase_FactorDifficultyIntoWaveBudget, reinterpret_cast<void*>(&detour_c111_factor), reinterpret_cast<void**>(&g_orig_c111_factor)},
    // card 114: always installed; it calls card 040's detour_c40_by_tags (whose trace logs only under its flag)
    {&offsets::UADDamageModifierSubsystem_GetModifiedDamageByTags, reinterpret_cast<void*>(&detour_c114_damage), reinterpret_cast<void**>(&g_orig_c40_by_tags)},
    {&offsets::USTDifficulty_GetAmmoReceivedMultiplier, reinterpret_cast<void*>(&detour_c114_ammo), reinterpret_cast<void**>(&g_orig_c114_ammo)},
    {&offsets::USTDifficulty_GetHealingPeriodMultiplier, reinterpret_cast<void*>(&detour_c114_heal), reinterpret_cast<void**>(&g_orig_c114_heal)},
    {&offsets::USTDifficulty_GetFedscoreMultiplier, reinterpret_cast<void*>(&detour_c114_fedscore), reinterpret_cast<void**>(&g_orig_c114_fedscore)},
    {&offsets::USTSelfHealAbility_OnGiveAbility, reinterpret_cast<void*>(&detour_c114_heal_give), reinterpret_cast<void**>(&g_orig_c114_heal_give)}, // card 117
    {&offsets::ASTTicketManager_CreateTicketContainer, reinterpret_cast<void*>(&detour_c111_create), reinterpret_cast<void**>(&g_orig_c111_create)},
    {&offsets::ASTTicketManager_TryRefreshTicketContainer, reinterpret_cast<void*>(&detour_c111_try_refresh), reinterpret_cast<void**>(&g_orig_c111_try_refresh)},
    {&offsets::ASTTicketManager_RequestTicket, reinterpret_cast<void*>(&detour_c111_request), reinterpret_cast<void**>(&g_orig_c111_request)},
    {&offsets::ASTEnemyBase_CanPerformAttack, reinterpret_cast<void*>(&detour_c122_can), reinterpret_cast<void**>(&g_orig_c122_can), &g_c122_log},
    {&offsets::USTAttackComponent_ChooseAvailableAttack, reinterpret_cast<void*>(&detour_c122_choose), reinterpret_cast<void**>(&g_orig_c122_choose), &g_c122_log},
    {&offsets::USTAttackComponent_PerformAttack, reinterpret_cast<void*>(&detour_c122_perform), reinterpret_cast<void**>(&g_orig_c122_perform), &g_c122_log},
    {&offsets::ASTArenaZoneBase_CanEnemyFitIntoZone, reinterpret_cast<void*>(&detour_c122_fit), reinterpret_cast<void**>(&g_orig_c122_fit), &g_c122_log},
    {&offsets::USTEncounterWaveBase_ActivateWave, reinterpret_cast<void*>(&detour_c112_activate), reinterpret_cast<void**>(&g_orig_c112_activate)},
    // card 160: the 2D sound rows gate a server copy's sounds on the host, so they are always installed
    {&offsets::UGameplayStatics_PlaySound2D, reinterpret_cast<void*>(&detour_c99_sound_2d), reinterpret_cast<void**>(&g_orig_c99_sound_2d)},
    {&offsets::UGameplayStatics_SpawnSound2D, reinterpret_cast<void*>(&detour_c99_spawn_sound_2d), reinterpret_cast<void**>(&g_orig_c99_spawn_sound_2d)},
    {&offsets::UADAudioManager_PlaySound2D, reinterpret_cast<void*>(&detour_c99_ad_sound_2d), reinterpret_cast<void**>(&g_orig_c99_ad_sound_2d)},
    {&offsets::UKismetMaterialLibrary_SetScalarParameterValue, reinterpret_cast<void*>(&detour_c160_mpc_scalar), reinterpret_cast<void**>(&g_orig_c160_mpc_scalar)},
    {&offsets::UWidgetBlueprintLibrary_execCreate, reinterpret_cast<void*>(&detour_c160_exec_create), reinterpret_cast<void**>(&g_orig_c160_exec_create)},
    {&offsets::UUserWidget_CreateWidgetInstanceWorld, reinterpret_cast<void*>(&detour_c160_widget_world), reinterpret_cast<void**>(&g_orig_c160_widget_world)},
    {&offsets::USTGameUserSettings_execIsToggleADSKBMEnabled, reinterpret_cast<void*>(&detour_c160_exec_toggle_kbm), reinterpret_cast<void**>(&g_orig_c160_exec_toggle_kbm)},
    {&offsets::USTGameUserSettings_execIsToggleADSControllerEnabled, reinterpret_cast<void*>(&detour_c160_exec_toggle_ctrl), reinterpret_cast<void**>(&g_orig_c160_exec_toggle_ctrl)},
    {&offsets::UADInputDetectionSubsystem_execCurrentInputGamepad, reinterpret_cast<void*>(&detour_c160_exec_gamepad), reinterpret_cast<void**>(&g_orig_c160_exec_gamepad)},
    {&offsets::USTCommonUtilityLibrary_PlaySoundAtLocationFNameWithAIAwareness, reinterpret_cast<void*>(&detour_c99_sound_fname), reinterpret_cast<void**>(&g_orig_c99_sound_fname), &g_c99_log},
    {&offsets::APlayerCameraManager_GetViewTarget, reinterpret_cast<void*>(&detour_c346_view_target), reinterpret_cast<void**>(&g_orig_c346_view_target)}, // card 346 b31 crash guard
    {&offsets::APlayerCameraManager_StartCameraShake, reinterpret_cast<void*>(&detour_c99_shake), reinterpret_cast<void**>(&g_orig_c99_shake), &g_c99_log},
    {&offsets::USTFedScoreSubsystem_AddFedScoreText, reinterpret_cast<void*>(&detour_c99_score_text), reinterpret_cast<void**>(&g_orig_c99_score_text)}, // card 491 b260: always (resupply)
    {&offsets::USTFedScoreSubsystem_AddFedScore, reinterpret_cast<void*>(&detour_c99_score), reinterpret_cast<void**>(&g_orig_c99_score)}, // card 491 b260: always (resupply)
    {&offsets::ASTPlayerMechBase_HandleInputExitVehicle_Pressed, reinterpret_cast<void*>(&detour_c75_exit_pressed), reinterpret_cast<void**>(&g_orig_c75_exit_pressed), nullptr},
    {&offsets::ASTPlayerController_HandleInputExitVehicle_Pressed, reinterpret_cast<void*>(&detour_c75_pc_exit), reinterpret_cast<void**>(&g_orig_c75_pc_exit), nullptr},
    {&offsets::USTTacticalSupportComponent_HandleTacticalSupportInput, reinterpret_cast<void*>(&detour_c100_tac_input), reinterpret_cast<void**>(&g_orig_c100_tac_input)},
    {&offsets::USTSkeletalAnimatorComponent_StartAnimationSequence, reinterpret_cast<void*>(&detour_c104_start), reinterpret_cast<void**>(&g_orig_c104_start)},
    {&offsets::UADSkeletalAnimatorComponent_StartAnimationSequenceFixedTime, reinterpret_cast<void*>(&detour_c104_start_fixed), reinterpret_cast<void**>(&g_orig_c104_start_fixed)},
    {&offsets::USTAbilityTask_PlayAnimation_DoAction, reinterpret_cast<void*>(&detour_c104_do_action), reinterpret_cast<void**>(&g_orig_c74_anim[1])},
    {&offsets::USTAbilityTask_PlayAnimation_AnimationEnded, reinterpret_cast<void*>(&detour_c104_anim_ended), reinterpret_cast<void**>(&g_orig_c74_anim[2])},
    {&offsets::ASTSupportFlare_RunLZQuery, reinterpret_cast<void*>(&detour_c104_flare_run), reinterpret_cast<void**>(&g_orig_c104_flare[0])},
    {&offsets::ASTSupportFlare_ActivateFlare, reinterpret_cast<void*>(&detour_c104_flare_activate), reinterpret_cast<void**>(&g_orig_c104_flare[1])},
    {&offsets::ASTSupportFlare_BeginPlay, reinterpret_cast<void*>(&detour_c104_flare_begin), reinterpret_cast<void**>(&g_orig_c104_flare_begin)}, // card 249
    {&offsets::ASTSupportFlare_ExpireFlare, reinterpret_cast<void*>(&detour_c104_flare_expire), reinterpret_cast<void**>(&g_orig_c104_flare[2])},
    {&offsets::ASTSupportFlare_LZQueryFinished, reinterpret_cast<void*>(&detour_c104_flare_lz), reinterpret_cast<void**>(&g_orig_c104_lz)},
    {&offsets::ASTSupportFlare_StartActivationTimer, reinterpret_cast<void*>(&detour_c104_flare_timer), reinterpret_cast<void**>(&g_orig_c104_flare_timer)},
    {&offsets::USTSaveSystemLibrary_PerformSave, reinterpret_cast<void*>(&detour_c121_perform_save), reinterpret_cast<void**>(&g_orig_c121_perform_save)}, // card 121
    {&offsets::USTSaveSystemLibrary_Load, reinterpret_cast<void*>(&detour_c121_load), reinterpret_cast<void**>(&g_orig_c121_load)}, // card 121
    {&offsets::ASTSaveSystemEngine_LoadFromSlot, reinterpret_cast<void*>(&detour_c137_load_slot), reinterpret_cast<void**>(&g_orig_c137_load_slot)}, // card 137 step 3
    {&offsets::ASTGameplayGameMode_UpdateSignificanceManager, reinterpret_cast<void*>(&detour_c132_update_significance), reinterpret_cast<void**>(&g_update_significance)}, // card 132
    {&offsets::ASTGameplayGameMode_Tick_PlayerCombatTracker, reinterpret_cast<void*>(&detour_c321_combat_tick), reinterpret_cast<void**>(&g_orig_c321_combat_tick)}, // card 321
    {&offsets::USTMusicManager_TickMusicModeManager, reinterpret_cast<void*>(&detour_c321_music_tick), reinterpret_cast<void**>(&g_orig_c321_music_tick)}, // card 321
    {&offsets::ASTNonPlayableCharacterBase_HandleDamageEvent, reinterpret_cast<void*>(&detour_c132_npc_damage), reinterpret_cast<void**>(&g_orig_c132_npc_damage), &g_c41_trace}, // card 132 diagnostic
    {&offsets::UActorChannel_ReplicateActor, reinterpret_cast<void*>(&detour_net_replicate_actor), reinterpret_cast<void**>(&g_orig_net_replicate_actor)}, // card 132 diagnostic: net_budget.cpp
    {&offsets::UNetDriver_ProcessRemoteFunctionForChannelPrivate, reinterpret_cast<void*>(&detour_net_remote_function), reinterpret_cast<void**>(&g_orig_net_remote_function)}, // card 132 diagnostic: net_budget.cpp
    {&offsets::USTTacticalSupportComponent_OnAbilityUsed, reinterpret_cast<void*>(&detour_c104_used), reinterpret_cast<void**>(&g_orig_c104_used)},
    {&offsets::USTTacticalSupportComponent_TryThrowFlare, reinterpret_cast<void*>(&detour_try_throw_flare), reinterpret_cast<void**>(&g_orig_try_throw_flare)},
    {&offsets::USTTacticalSupportComponent_EquipTacticalSupportAbility, reinterpret_cast<void*>(&detour_equip_tac_supp), reinterpret_cast<void**>(&g_orig_equip_tac_supp)}, // card 105: ungated, log only
    {&offsets::ASTMountableTurret_MountTurret, reinterpret_cast<void*>(&detour_mount_turret), reinterpret_cast<void**>(&g_orig_mount_turret), &g_gate_c29_c32},
    {&offsets::ASTMountableTurret_LeaveTurret, reinterpret_cast<void*>(&detour_leave_turret), reinterpret_cast<void**>(&g_orig_leave_turret), nullptr}, // card 123: always on
    {&offsets::ASTMountableTurret_OnInteraction, reinterpret_cast<void*>(&detour_c123_turret_on_interaction), reinterpret_cast<void**>(&g_orig_turret_on_interaction), nullptr}, // card 123
    {&offsets::ASTMountableTurret_HandleInputTag_Pressed, reinterpret_cast<void*>(&detour_c125_tag_pressed), reinterpret_cast<void**>(&g_orig_c125_tag_pressed), &g_c125_log}, // card 125
    {&offsets::ASTMountableTurret_HandleInputTag_Released, reinterpret_cast<void*>(&detour_c125_tag_released), reinterpret_cast<void**>(&g_orig_c125_tag_released), &g_c125_log}, // card 125
    {&offsets::ASTMountableTurret_HandleInputLookHorizontal, reinterpret_cast<void*>(&detour_c125_look_h), reinterpret_cast<void**>(&g_orig_c125_look_h), &g_c125_log}, // card 125
    {&offsets::ASTMountableTurret_HandleInputLookVertical, reinterpret_cast<void*>(&detour_c125_look_v), reinterpret_cast<void**>(&g_orig_c125_look_v), &g_c125_log}, // card 125
    // card 034: refit card 028 renderers after a frame-size change
    {&offsets::USTSpriteRenderComponent_AnimatorFrameSizeChanged, reinterpret_cast<void*>(&detour_frame_size_changed), reinterpret_cast<void**>(&g_orig_frame_size_changed)},
    // card 030: player death audit (log only)
    {&offsets::ASTPlayerCharacter_HandleDeathEvent, reinterpret_cast<void*>(&detour_player_death_event), reinterpret_cast<void**>(&g_orig_player_death_event)},
    // card 052: null-component sound guard (client crash after following the host to the next mission)
    {&offsets::USTCommonUtilityLibrary_SpawnSoundAttachedToComponentWithAIAwareness, reinterpret_cast<void*>(&detour_spawn_sound_attached), reinterpret_cast<void**>(&g_orig_spawn_sound_attached)},
    // card 117: null-mesh guard (client crash on an animation RPC right after a mid-map rejoin)
    {&offsets::UADSkeletalAnimatorComponent_StartAnimationSequence, reinterpret_cast<void*>(&detour_skel_anim_start), reinterpret_cast<void**>(&g_orig_skel_anim_start)},
    {&offsets::ReportCrash, reinterpret_cast<void*>(&detour_report_crash), reinterpret_cast<void**>(&g_orig_report_crash)},
    // card 350: the client answers the frontend HUD's empty options string from the host's travel URL
    {&offsets::APlayerController_ClientTravelInternal_Implementation, reinterpret_cast<void*>(&detour_c350_client_travel), reinterpret_cast<void**>(&g_orig_c350_client_travel)},
    {&offsets::UGameplayStatics_HasOption, reinterpret_cast<void*>(&detour_c350_has_option), reinterpret_cast<void**>(&g_orig_c350_has_option)},
    {&offsets::UGameplayStatics_ParseOption, reinterpret_cast<void*>(&detour_c350_parse_option), reinterpret_cast<void**>(&g_orig_c350_parse_option)},
    {&offsets::FWindowsErrorOutputDevice_HandleError, reinterpret_cast<void*>(&detour_handle_error),
     reinterpret_cast<void**>(&g_orig_handle_error)},
    // card 046: kill floor in co-op (warp to an Up player instead of the latch and StartDeath)
    {&offsets::USTCommonUtilityLibrary_IsBelowKillThreshold, reinterpret_cast<void*>(&detour_is_below_kill_threshold), reinterpret_cast<void**>(&g_orig_is_below_kill_threshold)},
    {&offsets::ASTPlayerCharacter_StartDeath, reinterpret_cast<void*>(&detour_start_death), reinterpret_cast<void**>(&g_orig_start_death)},
    {&offsets::ASTPlayerCharacterMI_StartDeath, reinterpret_cast<void*>(&detour_start_death_mi), reinterpret_cast<void**>(&g_orig_start_death_mi)},
    {&offsets::ASTPlayerCharacter_CompleteDeath, reinterpret_cast<void*>(&detour_complete_death), reinterpret_cast<void**>(&g_orig_complete_death)},
    {&offsets::ASTPlayerCharacterMI_LoadAbilitySystemComponent, reinterpret_cast<void*>(&detour_load_asc), reinterpret_cast<void**>(&g_orig_load_asc)}, // card 048
    {&offsets::ASTPlayerCharacterBug_LoadAbilitySystemComponent, reinterpret_cast<void*>(&detour_load_asc_bug), reinterpret_cast<void**>(&g_orig_load_asc_bug)}, // card 198
    {&offsets::USTSaveComponent_LoadOwnerVariables_Pawn, reinterpret_cast<void*>(&detour_load_owner_pawn), reinterpret_cast<void**>(&g_orig_load_owner_pawn), &g_gate_diag}, // card 048
    {&offsets::ASTCharacter_ActorFullyLoaded, reinterpret_cast<void*>(&detour_actor_fully_loaded), reinterpret_cast<void**>(&g_orig_actor_fully_loaded), &g_gate_diag}, // card 048
    {&offsets::ASTPlayerCharacter_execCompleteDeath, reinterpret_cast<void*>(&detour_exec_complete_death), reinterpret_cast<void**>(&g_orig_exec_complete_death), &g_gate_diag}, // card 048
    {&offsets::USTLevelObjectiveSubsystem_AddObjectiveObject, reinterpret_cast<void*>(&detour_add_objective), reinterpret_cast<void**>(&g_orig_add_objective), &g_gate_diag}, // card 048
    {&offsets::USTLevelObjectiveSubsystem_SetObjectiveMapPostSave, reinterpret_cast<void*>(&detour_set_objective_map), reinterpret_cast<void**>(&g_orig_set_objective_map)}, // card 048
    {&offsets::ASTGameplayGameMode_LoadSettingsFromSave, reinterpret_cast<void*>(&detour_load_settings), reinterpret_cast<void**>(&g_orig_load_settings), &g_gate_diag}, // card 048
    {&offsets::USTObjectiveUtilityLibrary_AddObjective, reinterpret_cast<void*>(&detour_c49_add), reinterpret_cast<void**>(&g_orig_c49_add)}, // card 049
    {&offsets::USTObjectiveUtilityLibrary_AddSubObjective, reinterpret_cast<void*>(&detour_c49_add_sub), reinterpret_cast<void**>(&g_orig_c49_add_sub)}, // card 049
    {&offsets::USTObjectiveUtilityLibrary_AddCountObjective, reinterpret_cast<void*>(&detour_c49_add_count), reinterpret_cast<void**>(&g_orig_c49_add_count)}, // card 049
    {&offsets::USTObjectiveUtilityLibrary_AddCountObjective_SubObjectives, reinterpret_cast<void*>(&detour_c49_add_count_subs), reinterpret_cast<void**>(&g_orig_c49_add_count_subs)}, // card 049
    {&offsets::USTObjectiveUtilityLibrary_AddCountSubObjective, reinterpret_cast<void*>(&detour_c49_add_count_sub), reinterpret_cast<void**>(&g_orig_c49_add_count_sub)}, // card 049
    {&offsets::USTObjectiveUtilityLibrary_AddCountSubObjective_SubObjectives, reinterpret_cast<void*>(&detour_c49_add_count_sub_subs), reinterpret_cast<void**>(&g_orig_c49_add_count_sub_subs)}, // card 049
    {&offsets::USTObjectiveUtilityLibrary_CompleteObjective, reinterpret_cast<void*>(&detour_c49_complete), reinterpret_cast<void**>(&g_orig_c49_complete)}, // card 049
    {&offsets::USTObjectiveUtilityLibrary_CompleteAnyObjective, reinterpret_cast<void*>(&detour_c49_complete_any), reinterpret_cast<void**>(&g_orig_c49_complete_any)}, // card 049
    {&offsets::USTObjectiveUtilityLibrary_CompleteSubObjective, reinterpret_cast<void*>(&detour_c49_complete_sub), reinterpret_cast<void**>(&g_orig_c49_complete_sub)}, // card 049
    {&offsets::USTObjectiveUtilityLibrary_IncrementObjectiveCount, reinterpret_cast<void*>(&detour_c49_increment), reinterpret_cast<void**>(&g_orig_c49_increment)}, // card 049
    {&offsets::USTLevelObjective_CompleteObjective, reinterpret_cast<void*>(&detour_c97_complete), reinterpret_cast<void**>(&g_orig_c97_complete)}, // card 097
    {&offsets::USTLevelObjectiveCount_IncrementObjectiveCount, reinterpret_cast<void*>(&detour_c97_increment), reinterpret_cast<void**>(&g_orig_c97_increment)}, // card 097
    {&offsets::USTLevelObjectiveSubsystem_ObjectiveComplete, reinterpret_cast<void*>(&detour_c97_subsystem_complete), reinterpret_cast<void**>(&g_orig_c97_subsystem_complete)}, // card 097
    {&offsets::USTObjectiveUtilityLibrary_SetObjectiveVisibility, reinterpret_cast<void*>(&detour_c49_visibility), reinterpret_cast<void**>(&g_orig_c49_visibility)}, // card 049
    {&offsets::USTObjectiveUtilityLibrary_CalloutObjective, reinterpret_cast<void*>(&detour_c49_callout), reinterpret_cast<void**>(&g_orig_c49_callout)}, // card 049
    {&offsets::USTObjectiveUtilityLibrary_CheckAllObjectivesComplete, reinterpret_cast<void*>(&detour_c49_check_all), reinterpret_cast<void**>(&g_orig_c49_check_all), &g_gate_diag}, // card 049
    {&offsets::USTObjectiveUtilityLibrary_ObjectivesCompletedGate, reinterpret_cast<void*>(&detour_c49_gate), reinterpret_cast<void**>(&g_orig_c49_gate), &g_gate_diag}, // card 049
    {&offsets::ASTSaveSystemEngine_Save, reinterpret_cast<void*>(&detour_c49_save), reinterpret_cast<void**>(&g_orig_c49_save), &g_gate_diag}, // card 049
    {&offsets::USTDebugSaveTracker_SetAutoSaveEnabled, reinterpret_cast<void*>(&detour_set_auto_save), reinterpret_cast<void**>(&g_orig_set_auto_save), &g_gate_c30},
    {&offsets::UADHealthComponent_HealthAttributeChanged, reinterpret_cast<void*>(&detour_health_changed), reinterpret_cast<void**>(&g_orig_health_changed)},
    {&offsets::UObject_ProcessEvent, reinterpret_cast<void*>(&detour_process_event), reinterpret_cast<void**>(&g_orig_process_event)},
    {&offsets::ProcessLocalScriptFunction, reinterpret_cast<void*>(&detour_c507_local_script), reinterpret_cast<void**>(&g_orig_c507_local_script)}, // card 507
    // card 037: host damage log (log only)
    {&offsets::UADHealthComponent_AttributeSetPostEffectApplied, reinterpret_cast<void*>(&detour_post_effect), reinterpret_cast<void**>(&g_orig_post_effect), &g_gate_c23},
    // Card 040: damage pipeline trace (log only, UBWCOOP_C40_DAMAGE_TRACE)
    // Card 041: bug attack animations (trace behind UBWCOOP_C41_ANIM_TRACE)
    {&offsets::ASTNonPlayableCharacterBase_TryPlayAnimationSequence, reinterpret_cast<void*>(&detour_c41_try_play), reinterpret_cast<void**>(&g_orig_c41_try_play)},
    {&offsets::ASTNonPlayableCharacterBase_TryPlayAnimationAgumentedByTime, reinterpret_cast<void*>(&detour_c41_try_timed), reinterpret_cast<void**>(&g_orig_c41_try_timed), &g_gate_c41},
    {&offsets::ASTNonPlayableCharacterBase_ProcessFrameAction, reinterpret_cast<void*>(&detour_c41_frame), reinterpret_cast<void**>(&g_orig_c41_frame), &g_gate_c41},
    {&offsets::ASTNonPlayableCharacterBase_AnimationEnded, reinterpret_cast<void*>(&detour_c41_ended), reinterpret_cast<void**>(&g_orig_c41_ended), &g_gate_c41},
    {&offsets::USTAttackComponent_OnAnimationEnd, reinterpret_cast<void*>(&detour_c41_attack_end), reinterpret_cast<void**>(&g_orig_c41_attack_end), &g_gate_c41},
    {&offsets::USTAttackActionPlayAnimation_OnAnimationEnd, reinterpret_cast<void*>(&detour_c41_action_end), reinterpret_cast<void**>(&g_orig_c41_action_end), &g_gate_c41},
    {&offsets::UADDamageHandler_Process, reinterpret_cast<void*>(&detour_c40_process), reinterpret_cast<void**>(&g_orig_c40_process)}, // card 155: ungated (player hits); the enemy trace keeps g_c40_trace
    {&offsets::UADDamageHandler_ModifyDamage, reinterpret_cast<void*>(&detour_c40_base_modify), reinterpret_cast<void**>(&g_orig_c40_base_modify), &g_gate_c40},
    {&offsets::USTAIDamageHandler_ModifyDamage, reinterpret_cast<void*>(&detour_c40_ai_modify), reinterpret_cast<void**>(&g_orig_c40_ai_modify), &g_gate_c40},
    {&offsets::UADDamageHandler_SplitDamage, reinterpret_cast<void*>(&detour_c40_split), reinterpret_cast<void**>(&g_orig_c40_split), &g_gate_c40},
    {&offsets::UADDamageHandler_ApplyDamageToArmour, reinterpret_cast<void*>(&detour_c40_armour), reinterpret_cast<void**>(&g_orig_c40_armour), &g_gate_c40},
    {&offsets::UADDamageHandler_ApplyDamageToHealth, reinterpret_cast<void*>(&detour_c40_health), reinterpret_cast<void**>(&g_orig_c40_health), &g_gate_c40},
    {&offsets::UADAbilitySystemComponent_GetGameplayAttributeValueOrDefault, reinterpret_cast<void*>(&detour_c40_attr), reinterpret_cast<void**>(&g_orig_c40_attr), &g_gate_c40},
    {&offsets::UADDamageModifierSubsystem_GetModifiedDamage, reinterpret_cast<void*>(&detour_c40_modified), reinterpret_cast<void**>(&g_orig_c40_modified), &g_gate_c40},
    {&offsets::USTHitVolumeComponentBase_IsWithinVolume, reinterpret_cast<void*>(&detour_c40_within), reinterpret_cast<void**>(&g_orig_c40_within), &g_gate_c40},
    {&offsets::USTHitVolumeComponentBase_TryTriggerVolume, reinterpret_cast<void*>(&detour_c40_try_trigger), reinterpret_cast<void**>(&g_orig_c40_try_trigger), &g_gate_c40},
    // card 050: weapon sync audit
    {&offsets::UADWeaponInventory_RemoveWeaponByClass, reinterpret_cast<void*>(&detour_c50_remove_by_class), reinterpret_cast<void**>(&g_orig_c50_remove_by_class), &g_gate_diag},
    {&offsets::UADWeaponInventory_EquipWeaponByIndex, reinterpret_cast<void*>(&detour_c50_equip_by_index), reinterpret_cast<void**>(&g_orig_c50_equip_by_index), &g_gate_diag},
    {&offsets::UADWeaponInventory_EquipWeaponByClass, reinterpret_cast<void*>(&detour_c50_equip_by_class), reinterpret_cast<void**>(&g_orig_c50_equip_by_class)},
    {&offsets::UADWeaponInventory_UnequipCurrentWeapon, reinterpret_cast<void*>(&detour_c50_unequip), reinterpret_cast<void**>(&g_orig_c50_unequip)},
    {&offsets::ASTPlayerCharacter_EquipWeapon, reinterpret_cast<void*>(&detour_c57_equip_weapon), reinterpret_cast<void**>(&g_orig_c57_equip_weapon), &g_gate_diag},
    {&offsets::USTWeaponSwitchAbility_ActivateAbility, reinterpret_cast<void*>(&detour_c57_activate), reinterpret_cast<void**>(&g_orig_c57_activate)},
    {&offsets::USTWeaponSwitchAbility_CanActivateAbility, reinterpret_cast<void*>(&detour_c57_can_activate), reinterpret_cast<void**>(&g_orig_c57_can_activate), &g_gate_diag},
    {&offsets::USTWeaponSwitchAbility_BeginWeaponSwitch, reinterpret_cast<void*>(&detour_c57_begin_switch), reinterpret_cast<void**>(&g_orig_c57_begin_switch), &g_gate_diag},
    {&offsets::ASTPlayerCharacter_ConstructWeaponSwitchRequest, reinterpret_cast<void*>(&detour_c57_request), reinterpret_cast<void**>(&g_orig_c57_request)},
    {&offsets::UADWeaponInventory_CycleEquip, reinterpret_cast<void*>(&detour_c57_cycle), reinterpret_cast<void**>(&g_orig_c57_cycle), &g_gate_diag},
    {&offsets::USTWeaponUnderslungAbility_ActivateAbility, reinterpret_cast<void*>(&detour_c141_activate), reinterpret_cast<void**>(&g_orig_c141_activate)}, // card 141
    {&offsets::USTWeaponUnderslungAbility_ReceiveEvent, reinterpret_cast<void*>(&detour_c141_receive), reinterpret_cast<void**>(&g_orig_c141_receive)}, // card 141
    {&offsets::USTWeaponSwitchAbility_ShouldAbilityRespondToEvent, reinterpret_cast<void*>(&detour_c62_respond), reinterpret_cast<void**>(&g_orig_c62_respond), &g_gate_c62},
    {&offsets::USTWeaponSwitchAbility_EndAbility, reinterpret_cast<void*>(&detour_c62_end), reinterpret_cast<void**>(&g_orig_c62_end), &g_gate_c62},
    {&offsets::USTSaveSystemLibrary_Save_CheckPoint, reinterpret_cast<void*>(&detour_c50_save_checkpoint), reinterpret_cast<void**>(&g_orig_c50_save_checkpoint), &g_gate_diag},
    {&offsets::ASTWeaponPickup_PickupWeapon, reinterpret_cast<void*>(&detour_c50_pickup_weapon), reinterpret_cast<void**>(&g_orig_c50_pickup_weapon), nullptr}, // card 495: always
    {&offsets::AADPickupBase_Pickup, reinterpret_cast<void*>(&detour_c50_pickup), reinterpret_cast<void**>(&g_orig_c50_pickup), &g_gate_diag},
    {&offsets::AADPickupBase_DisablePickup, reinterpret_cast<void*>(&detour_c50_disable_pickup), reinterpret_cast<void**>(&g_orig_c50_disable_pickup)},
    {&offsets::AADPickupBase_Pickup_Implementation, reinterpret_cast<void*>(&detour_c254_pickup_implementation), reinterpret_cast<void**>(&g_orig_c254_pickup_implementation)}, // card 254
    {&offsets::ASTSecretPickup_Pickup_Implementation, reinterpret_cast<void*>(&detour_c289_secret_pickup), reinterpret_cast<void**>(&g_orig_c289_secret_pickup)}, // card 289
    {&offsets::AADPickupBase_OnUpdatePickupStatus, reinterpret_cast<void*>(&detour_c145_update_status), reinterpret_cast<void**>(&g_orig_c145_update_status)}, // card 145
    {&offsets::UEngine_HandleNetworkFailure, reinterpret_cast<void*>(&detour_handle_network_failure),
     reinterpret_cast<void**>(&g_orig_handle_network_failure)}, // card 052
    {&offsets::UGameEngine_HandleNetworkFailure_NotifyGameInstance, reinterpret_cast<void*>(&detour_c290_notify), reinterpret_cast<void**>(&g_orig_c290_notify)}, // card 290
    {&offsets::UEngine_HandleTravelFailure, reinterpret_cast<void*>(&detour_handle_travel_failure),
     reinterpret_cast<void**>(&g_orig_handle_travel_failure)}, // card 052
    {&offsets::UNetConnection_Close, reinterpret_cast<void*>(&detour_c128_close),
     reinterpret_cast<void**>(&g_orig_c128_close)}, // card 128
    {&offsets::UIpConnection_HandleConnectionTimeout, reinterpret_cast<void*>(&detour_c128_timeout),
     reinterpret_cast<void**>(&g_orig_c128_timeout)}, // card 128
    {&offsets::UIpConnection_GetTimeoutValue, reinterpret_cast<void*>(&detour_c128_timeout_value),
     reinterpret_cast<void**>(&g_orig_c128_timeout_value)}, // card 128
    {&offsets::UNetConnection_CleanUp, reinterpret_cast<void*>(&detour_c128_cleanup),
     reinterpret_cast<void**>(&g_orig_c128_cleanup)}, // card 128
    {&offsets::AGameSession_KickPlayer, reinterpret_cast<void*>(&detour_c128_kick),
     reinterpret_cast<void**>(&g_orig_c128_kick)}, // card 128
    {&offsets::ISTSignificanceObjectInterface_ProcessSignificanceUpdate,
     reinterpret_cast<void*>(&detour_c95_process_significance),
     reinterpret_cast<void**>(&g_orig_c95_process_significance)}, // card 147 (card 095 relay)
    // Card 227: the presence gate (always on). BeginPlay's body is shared with ASTAnimatedLight,
    // ASTTeleporterVolumeBase and ASTJumpPadBase::BeginPlay; the detour filters by class.
    {&offsets::ASTPlayerPresence_BeginPlay, reinterpret_cast<void*>(&detour_c227_begin_play),
     reinterpret_cast<void**>(&g_orig_c227_begin_play)}, // card 227
    {&offsets::ASTPlayerPresence_OnTriggerEntered, reinterpret_cast<void*>(&detour_c227_entered),
     reinterpret_cast<void**>(&g_orig_c227_entered)}, // card 227
    {&offsets::ASTPlayerPresence_OnTriggerExited, reinterpret_cast<void*>(&detour_c227_exited),
     reinterpret_cast<void**>(&g_orig_c227_exited)}, // card 227
    {&offsets::ASTPlayerPresence_OnBeginOverlap, reinterpret_cast<void*>(&detour_c245_overlap),
     reinterpret_cast<void**>(&g_orig_c245_overlap)}, // card 245 b192: the gates before OnTriggerEntered
    {&offsets::ASTPlayerPresence_execSetTriggerEventsEnabled, reinterpret_cast<void*>(&detour_c301_exec_enable),
     reinterpret_cast<void**>(&g_orig_c301_exec_enable)}, // card 301 b212: gap G, players inside on enable
};

// Engine functions the mod calls directly (not hooked). Each slot is written only through this
// table, as a raw address; the typed globals above alias these slots by reinterpret_cast.
const CallSpec kCallsUbw[] = {
    {&offsets::FGameplayCueParameters_GetInstigator, reinterpret_cast<void**>(&g_c136_cue_instigator)}, // card 136
    {&offsets::FGameplayCueParameters_Ctor, reinterpret_cast<void**>(&g_c136_cue_params_ctor)}, // card 136
    {&offsets::USceneComponent_GetSocketLocation, reinterpret_cast<void**>(&g_c136_socket_location)}, // card 136
    {&offsets::USceneComponent_GetSocketRotation, reinterpret_cast<void**>(&g_c136_socket_rotation)}, // card 136
    {&offsets::FGameplayCueParameters_CtorContext, reinterpret_cast<void**>(&g_c136_cue_params_from_context)},
    {&offsets::FGameplayCueParameters_Dtor, reinterpret_cast<void**>(&g_c136_cue_params_dtor)}, // card 136
    {&offsets::ASTProceduralDropship_SkipFlight, reinterpret_cast<void**>(&g_call_skip_flight)},
    {&offsets::C42Pooler_StickySprite, reinterpret_cast<void**>(&g_c42_pooler_cls[0])},
    {&offsets::C42Pooler_StickyStaticMesh, reinterpret_cast<void**>(&g_c42_pooler_cls[1])},
    {&offsets::C42Pooler_StickySkeletalMesh, reinterpret_cast<void**>(&g_c42_pooler_cls[2])},
    {&offsets::C42Pooler_NonStickySprite, reinterpret_cast<void**>(&g_c42_pooler_cls[3])},
    {&offsets::C42Pooler_NonStickyStaticMesh, reinterpret_cast<void**>(&g_c42_pooler_cls[4])},
    {&offsets::C42Pooler_NonStickySkeletalMesh, reinterpret_cast<void**>(&g_c42_pooler_cls[5])},
    {&offsets::C42Part_StickySprite, reinterpret_cast<void**>(&g_c42_part_cls[0])},
    {&offsets::C42Part_StickyStaticMesh, reinterpret_cast<void**>(&g_c42_part_cls[1])},
    {&offsets::C42Part_StickySkeletalMesh, reinterpret_cast<void**>(&g_c42_part_cls[2])},
    {&offsets::C42Part_NonStickySprite, reinterpret_cast<void**>(&g_c42_part_cls[3])},
    {&offsets::C42Part_NonStickyStaticMesh, reinterpret_cast<void**>(&g_c42_part_cls[4])},
    {&offsets::C42Part_NonStickySkeletalMesh, reinterpret_cast<void**>(&g_c42_part_cls[5])},
    {&offsets::C42Data_StickySprite, reinterpret_cast<void**>(&g_c42_data_cls[0])},
    {&offsets::C42Data_StickyStaticMesh, reinterpret_cast<void**>(&g_c42_data_cls[1])},
    {&offsets::C42Data_StickySkeletalMesh, reinterpret_cast<void**>(&g_c42_data_cls[2])},
    {&offsets::C42Data_NonStickySprite, reinterpret_cast<void**>(&g_c42_data_cls[3])},
    {&offsets::C42Data_NonStickyStaticMesh, reinterpret_cast<void**>(&g_c42_data_cls[4])},
    {&offsets::C42Data_NonStickySkeletalMesh, reinterpret_cast<void**>(&g_c42_data_cls[5])},
    {&offsets::USTActorPoolerMultiplatformBase_Initialise, reinterpret_cast<void**>(&g_pooler_init)},
    {&offsets::UPackageMapClient_GetNetGUIDFromObject, reinterpret_cast<void**>(&g_get_net_guid)},
    {&offsets::UPackageMapClient_GetObjectFromNetGUID, reinterpret_cast<void**>(&g_object_from_guid)},
    {&offsets::FStaticConstructObjectParameters_ctor, reinterpret_cast<void**>(&g_sco_params_ctor)},
    {&offsets::StaticConstructObject_Internal, reinterpret_cast<void**>(&g_sco_internal)},
    {&offsets::UUserWidget_StaticClass, reinterpret_cast<void**>(&g_user_widget_class)},
    {&offsets::UCanvasPanel_StaticClass, reinterpret_cast<void**>(&g_canvas_panel_class)},
    {&offsets::UWidgetTree_StaticClass, reinterpret_cast<void**>(&g_widget_tree_class)},
    {&offsets::UPanelWidget_ClearChildren, reinterpret_cast<void**>(&g_panel_clear_children)},
    {&offsets::UPanelWidget_GetChildrenCount, reinterpret_cast<void**>(&g_panel_child_count)},
    {&offsets::UPanelWidget_GetChildAt, reinterpret_cast<void**>(&g_panel_child_at)},
    {&offsets::UCanvasPanelSlot_SetPosition, reinterpret_cast<void**>(&g_slot_set_position)},
    {&offsets::UCanvasPanelSlot_SetAutoSize, reinterpret_cast<void**>(&g_slot_set_auto_size)},
    {&offsets::UTextBlock_SetText, reinterpret_cast<void**>(&g_text_block_set_text)},
    {&offsets::APlayerController_ProjectWorldLocationToScreen, reinterpret_cast<void**>(&g_project_to_screen)},
    {&offsets::APlayerController_GetViewportSize, reinterpret_cast<void**>(&g_c324_viewport_size)}, // card 324
    {&offsets::UWidgetLayoutLibrary_GetViewportScale, reinterpret_cast<void**>(&g_viewport_scale)},
    {&offsets::USTMinimapComponent_ShowOnMinimap, reinterpret_cast<void**>(&g_c416_show_on_minimap)}, // card 416
    {&offsets::UUserWidget_SetColorAndOpacity, reinterpret_cast<void**>(&g_c416_set_color)},
    {&offsets::UPrimitiveComponent_CreateDynamicMaterialInstance, reinterpret_cast<void**>(&g_c416_create_mid)},
    {&offsets::UMaterialInstanceDynamic_SetVectorParameterValue, reinterpret_cast<void**>(&g_c416_set_vector)},
    {&offsets::UStaticMeshComponent_SetStaticMesh, reinterpret_cast<void**>(&g_c416_set_mesh)},
    {&offsets::FInputModeGameAndUI_ctor, reinterpret_cast<void**>(&g_input_mode_game_and_ui_ctor)},
    {&offsets::FInputModeUIOnly_ctor, reinterpret_cast<void**>(&g_input_mode_ui_only_ctor)},
    {&offsets::APlayerController_SetPlayer, reinterpret_cast<void**>(&g_pc_set_player)},
    {&offsets::ULocalPlayer_StaticClass, reinterpret_cast<void**>(&g_local_player_class)},
    {&offsets::ASTPlayerController_SetupInputComponent, reinterpret_cast<void**>(&g_setup_input_component)},
    {&offsets::FSoftObjectPtr_LoadSynchronous, reinterpret_cast<void**>(&g_load_synchronous)},
    {&offsets::UPlayerMappableInputConfig_StaticClass, reinterpret_cast<void**>(&g_mappable_config_class)},
    {&offsets::GC_MarkAsReachable, reinterpret_cast<void**>(&g_mark_as_reachable)},
    {&offsets::APlayerController_ReceivedGameModeClass_Folded, reinterpret_cast<void**>(&g_received_game_mode_class)},
    {&offsets::ASTPlayerController_Tick, reinterpret_cast<void**>(&g_st_pc_tick)},
    {&offsets::USTInputReceiverInterface_StaticClass, reinterpret_cast<void**>(&g_input_receiver_class)},
    {&offsets::ASTPlayerCharacter_StaticClass, reinterpret_cast<void**>(&g_player_character_class)},
    {&offsets::ACharacter_StaticClass, reinterpret_cast<void**>(&g_character_class)},
    {&offsets::ASTPlayerController_StaticClass, reinterpret_cast<void**>(&g_st_pc_class)},
    {&offsets::ASTPlayerController_ReleaseActiveInputs, reinterpret_cast<void**>(&g_release_active_inputs)},
    {&offsets::USTMinimapComponent_SetOriginActor, reinterpret_cast<void**>(&g_set_minimap_origin)},
    {&offsets::APlayerController_GetPlayerViewPoint, reinterpret_cast<void**>(&g_player_view_point)},
    {&offsets::ASTNonPlayableCharacterBase_StaticClass, reinterpret_cast<void**>(&g_npc_class)},
    {&offsets::USTBugAnimPhysComponent_StaticClass, reinterpret_cast<void**>(&g_bug_anim_class)}, // card 038
    {&offsets::USTGoreComponent_StaticClass, reinterpret_cast<void**>(&g_gore_class)},
    // Card 039
    {&offsets::ASTNonPlayableCharacterBase_EnableCorpseBoxCollider, reinterpret_cast<void**>(&g_enable_corpse_box)},
    {&offsets::ASTNonPlayableCharacterBase_GetGoreLocation, reinterpret_cast<void**>(&g_gore_location)},
    {&offsets::ASTNonPlayableCharacterBase_IsNPC2D, reinterpret_cast<void**>(&g_is_npc_2d)},
    {&offsets::USkeletalMeshComponent_UpdateKinematicBonesToAnim, reinterpret_cast<void**>(&g_kinematic_bones)},
    {&offsets::USceneComponent_K2_SetRelativeLocation, reinterpret_cast<void**>(&g_set_relative_location)}, // card 524
    {&offsets::FBodyInstanceCore_ShouldInstanceSimulatingPhysics, reinterpret_cast<void**>(&g_body_simulating)},
    {&offsets::USkeletalMeshComponent_GetSkeletalCenterOfMass, reinterpret_cast<void**>(&g_skeletal_com)}, // card 132
    {&offsets::FBodyInstance_GetUnrealWorldTransform, reinterpret_cast<void**>(&g_pose_body_world)}, // card 132 pose
    {&offsets::USTDecalManager_RemoveDecal, reinterpret_cast<void**>(&g_decal_remove)}, // card 132 gore decals
    {&offsets::UDecalComponent_SetSortOrder, reinterpret_cast<void**>(&g_decal_set_sort)},
    {&offsets::UDecalComponent_SetFadeScreenSize, reinterpret_cast<void**>(&g_decal_set_screen)},
    {&offsets::UDecalComponent_SetFadeOut, reinterpret_cast<void**>(&g_decal_fade_out)},
    {&offsets::UDecalComponent_SetFadeIn, reinterpret_cast<void**>(&g_decal_fade_in)},
    {&offsets::UDecalComponent_SetLifeSpan, reinterpret_cast<void**>(&g_decal_life_span)},
    {&offsets::FBodyInstance_SetBodyTransform, reinterpret_cast<void**>(&g_pose_body_set)},
    {&offsets::USkeletalMeshComponent_IsAnyRigidBodyAwake, reinterpret_cast<void**>(&g_pose_mesh_awake)},
    {&offsets::USkeletalMeshComponent_PutAllRigidBodiesToSleep, reinterpret_cast<void**>(&g_pose_mesh_sleep)},
    {&offsets::UPrimitiveComponent_RigidBodyIsAwake, reinterpret_cast<void**>(&g_prim_awake)}, // card 132 note 2
    {&offsets::UPrimitiveComponent_IsSimulatingPhysics, reinterpret_cast<void**>(&g_prim_simulating)},
    {&offsets::UMovementComponent_StopMovementImmediately, reinterpret_cast<void**>(&g_c317_stop_movement)}, // card 317
    {&offsets::UMovementComponent_Deactivate, reinterpret_cast<void**>(&g_c317_deactivate)},                   // card 317
    {&offsets::UPrimitiveComponent_PutRigidBodyToSleep, reinterpret_cast<void**>(&g_prim_sleep)},
    {&offsets::USkeletalMeshComponent_SetAllPhysicsLinearVelocity, reinterpret_cast<void**>(&g_pose_mesh_linear)},
    {&offsets::USkeletalMeshComponent_SetAllPhysicsAngularVelocityInRadians, reinterpret_cast<void**>(&g_pose_mesh_angular)},
    {&offsets::USkeletalMeshComponent_SetEnableGravity, reinterpret_cast<void**>(&g_pose_mesh_gravity)}, // card 152 r2
    {&offsets::UPrimitiveComponent_GetCollisionObjectType, reinterpret_cast<void**>(&g_collision_type)}, // card 132
    {&offsets::UADHealthComponent_GetOverkillThreshold, reinterpret_cast<void**>(&g_overkill_threshold)},
    {&offsets::UPrimitiveComponent_GetCollisionEnabled, reinterpret_cast<void**>(&g_collision_enabled)},
    {&offsets::ASTPlayerCharacter_PlayHurtCameraShake, reinterpret_cast<void**>(&g_c454_hurt_shake)}, // card 454
    {&offsets::USTCombatUtilityLibrary_IsPlayer, reinterpret_cast<void**>(&g_c258_is_player)}, // card 258
    {&offsets::UPrimitiveComponent_GetCollisionProfileName, reinterpret_cast<void**>(&g_collision_profile)},
    {&offsets::UWorld_EncroachingBlockingGeometry, reinterpret_cast<void**>(&g_c46_encroaching)},
    {&offsets::UWorld_FindTeleportSpot, reinterpret_cast<void**>(&g_c46_teleport_spot)},
    {&offsets::UADSpriteAnimationData_StaticClass, reinterpret_cast<void**>(&g_sprite_data_class)},
    {&offsets::USTNPCAnimationData_StaticClass, reinterpret_cast<void**>(&g_npc_data_class)},
    {&offsets::USTOutOfBoundTriggerComponent_GetTimeLeft, reinterpret_cast<void**>(&g_c360_time_left)}, // card 360
    {&offsets::AActor_GetActorBounds, reinterpret_cast<void**>(&g_actor_bounds)}, // card 131
    {&offsets::UAbilitySystemComponent_CancelAllAbilities, reinterpret_cast<void**>(&g_cancel_all_abilities)},
    {&offsets::USTDebugSaveTracker_IsAutoSaveEnabled, reinterpret_cast<void**>(&g_is_auto_save_enabled)},
    {&offsets::USceneComponent_AddLocalOffset, reinterpret_cast<void**>(&g_add_local_offset)},
    {&offsets::FGameplayAttribute_CtorProperty, reinterpret_cast<void**>(&g_gameplay_attribute_ctor)},
    {&offsets::USTWeaponInventory_GetWeaponBySlot, reinterpret_cast<void**>(&g_get_weapon_by_slot)},
    {&offsets::USTGameUserSettings_GetSTGameUserSettings, reinterpret_cast<void**>(&g_c160_settings)}, // card 160
    {&offsets::USTGameUserSettings_IsToggleADSKBMEnabled, reinterpret_cast<void**>(&g_c160_toggle_kbm)},
    {&offsets::USTGameUserSettings_IsToggleADSControllerEnabled, reinterpret_cast<void**>(&g_c160_toggle_ctrl)},
    {&offsets::USTGameUserSettings_SetToggleADSKBM, reinterpret_cast<void**>(&g_c160_set_toggle_kbm)},
    {&offsets::UGameInstance_GetInputDetectionSubsystem, reinterpret_cast<void**>(&g_c160_input_subsystem)},
    {&offsets::USkeletalMeshComponent_GetAnimInstance, reinterpret_cast<void**>(&g_c169_anim_instance)},
    {&offsets::ASTWeapon_ShowWeapon, reinterpret_cast<void**>(&g_c494_show_weapon)},         // card 494
    {&offsets::ASTWeapon_StaticClass, reinterpret_cast<void**>(&g_c494_st_weapon_class)},    // card 494
    {&offsets::FGameplayEventData_Destructor, reinterpret_cast<void**>(&g_c174_event_data_dtor)}, // card 174
    {&offsets::USTUseTacticalSupportAbility_StaticClass, reinterpret_cast<void**>(&g_c174_tac_class)}, // card 174
    {&offsets::UAnimInstance_GetCurrentActiveMontage, reinterpret_cast<void**>(&g_c169_active_montage)},
    {&offsets::UAnimInstance_Montage_GetPosition, reinterpret_cast<void**>(&g_c169_montage_position)},
    {&offsets::UADInputDetectionSubsystem_CurrentInputGamepad, reinterpret_cast<void**>(&g_c160_gamepad)},
    {&offsets::ASTPlayerCharacterMI_StaticClass, reinterpret_cast<void**>(&g_player_mi_class)},
    {&offsets::ASTPlayerCharacterBug_StartDeath, reinterpret_cast<void**>(&g_bug_start_death)}, // card 196
    {&offsets::USTAttributeSet_PlayerCamera_StaticClass, reinterpret_cast<void**>(&g_camera_set_class)},
    {&offsets::USTAttributeSet_Movement_StaticClass, reinterpret_cast<void**>(&g_movement_set_class)},
    {&offsets::APlayerController_StaticClass, reinterpret_cast<void**>(&g_player_controller_class)},
    {&offsets::UWorld_GetFirstPlayerController, reinterpret_cast<void**>(&g_world_first_pc)},
    {&offsets::APlayerController_HasClientLoadedCurrentWorld, reinterpret_cast<void**>(&g_pc_client_loaded)}, // card 348
    {&offsets::FKey_CtorWide, reinterpret_cast<void**>(&g_fkey_ctor)},
    {&offsets::FInputKeyEventArgs_Ctor, reinterpret_cast<void**>(&g_key_args_ctor)},
    {&offsets::RequestEngineExit, reinterpret_cast<void**>(&g_request_exit)},
    {&offsets::ASTPlayerTransportDropship_StaticClass, reinterpret_cast<void**>(&g_transport_dropship_class)},
    {&offsets::ASTProceduralDropshipTroops_StaticClass, reinterpret_cast<void**>(&g_troops_dropship_class)}, // card 132
    {&offsets::USTSprintAbility_StaticClass, reinterpret_cast<void**>(&g_sprint_ability_class)}, // card 019
    {&offsets::UNiagaraSystem_StaticClass, reinterpret_cast<void**>(&g_niagara_system_class)}, // card 074
    {&offsets::UNiagaraComponent_IsComplete, reinterpret_cast<void**>(&g_niagara_is_complete)},             // card 130
    {&offsets::UNiagaraComponent_GetRequestedExecutionState, reinterpret_cast<void**>(&g_niagara_requested_state)}, // card 130
    {&offsets::AActor_StaticClass, reinterpret_cast<void**>(&g_actor_class)}, // card 075
    {&offsets::USplineComponent_StaticClass, reinterpret_cast<void**>(&g_spline_class)}, // card 236
    {&offsets::APawn_StaticClass, reinterpret_cast<void**>(&g_pawn_class)},   // card 099
    {&offsets::AADProjectile_StaticClass, reinterpret_cast<void**>(&g_projectile_class)},     // card 136 run 6
    {&offsets::ASTGorePartBase_StaticClass, reinterpret_cast<void**>(&g_gore_part_class)},   // card 136 run 6
    {&offsets::USoundBase_StaticClass, reinterpret_cast<void**>(&g_sound_base_class)},         // card 074
    {&offsets::AActor_SetLifeSpan, reinterpret_cast<void**>(&g_set_life_span)},                // card 074
    {&offsets::AActor_IsActorTickEnabled, reinterpret_cast<void**>(&g_actor_tick_enabled)},
    {&offsets::UWorld_GetNumPlayerControllers, reinterpret_cast<void**>(&g_world_num_pcs)},
    // Card 020
    {&offsets::APawn_GetBaseAimRotation, reinterpret_cast<void**>(&g_get_base_aim)},
    {&offsets::APawn_GetActorEyesViewPoint, reinterpret_cast<void**>(&g_get_eyes_view_point)},
    {&offsets::UADWeaponInventory_GetEquippedWeapon, reinterpret_cast<void**>(&g_get_equipped_weapon)},
    {&offsets::USTWeaponUtilityLibrary_GetMuzzleTransform, reinterpret_cast<void**>(&g_get_muzzle_transform)}, // card 316
    {&offsets::UADWeaponInventory_GetWeaponCount, reinterpret_cast<void**>(&g_get_weapon_count)},
    {&offsets::USTWeaponUtilityLibrary_GetWeaponDefinitionFromTag, reinterpret_cast<void**>(&g_weapon_def_from_tag)},
    {&offsets::UADWeaponInventory_GetWeaponInstanceFromClass, reinterpret_cast<void**>(&g_weapon_from_class)},
    {&offsets::USTWeaponUtilityLibrary_DoesWeaponAmmunitionTypeUseMagazine, reinterpret_cast<void**>(&g_ammo_uses_magazine)},
    {&offsets::USTWeaponUtilityLibrary_SetEquippedAmmunition, reinterpret_cast<void**>(&g_set_equipped_ammo)},
    {&offsets::USTWeaponUtilityLibrary_AddAmmunition, reinterpret_cast<void**>(&g_add_ammunition)},
    {&offsets::UAbilitySystemComponent_GetGameplayTagCount, reinterpret_cast<void**>(&g_c310_tag_count)}, // card 310
    {&offsets::UADWeaponInventory_GetWeaponIndexFromTag, reinterpret_cast<void**>(&g_weapon_index_from_tag)},
    {&offsets::USceneComponent_SetWorldRotation, reinterpret_cast<void**>(&g_set_world_rotation)},
    {&offsets::UADAttributeSetHealth_StaticClass, reinterpret_cast<void**>(&g_health_set_class)},
    {&offsets::UADHealthComponent_HealthAttributeChanged, reinterpret_cast<void**>(&g_health_attribute_changed)},
    {&offsets::UADHealthComponent_ArmourAttributeChanged, reinterpret_cast<void**>(&g_armour_attribute_changed)}, // card 170
    {&offsets::USTCommonUtilityLibrary_GetSTGameInstance, reinterpret_cast<void**>(&g_get_st_game_instance)},
    {&offsets::USTGameInstance_LoadComplete, reinterpret_cast<void**>(&g_c375_load_complete)}, // card 375
    {&offsets::USTWeaponUtilityLibrary_GetAmmoCountOfEquippedAmmunitionType, reinterpret_cast<void**>(&g_get_ammo_count)},
    {&offsets::USTWeaponUtilityLibrary_GetEquippedAmmunitionCount, reinterpret_cast<void**>(&g_c274_magazine_count)}, // card 274
    {&offsets::USTWeaponUtilityLibrary_GetEquippedAmmunitionType, reinterpret_cast<void**>(&g_c274_magazine_type)},   // card 274
    {&offsets::USTAmmunitionInventory_GetAmmoCount, reinterpret_cast<void**>(&g_c69_get_ammo)}, // cards 069, 071
    {&offsets::USTAmmunitionInventory_GetMaxAmmo, reinterpret_cast<void**>(&g_c69_get_max)},
    {&offsets::USTAmmunitionInventory_AddAmmo, reinterpret_cast<void**>(&g_c71_add_ammo)}, // card 071
    {&offsets::USTAmmunitionInventory_SetAmmoCount, reinterpret_cast<void**>(&g_c72_set_ammo)}, // card 072
    {&offsets::USTItemInventory_GetEquippedInventoryItem, reinterpret_cast<void**>(&g_get_equipped_item)},
    {&offsets::USTItemInventory_AddItemByDefinition, reinterpret_cast<void**>(&g_c73_add_item)},   // card 073
    {&offsets::USTItemInventory_FindItemByDefinition, reinterpret_cast<void**>(&g_c73_find_item)}, // card 073
    {&offsets::UADProjectilePooler_SpawnProjectile, reinterpret_cast<void**>(&g_c74_spawn_fn)},       // card 074
    {&offsets::UADAbilitySystemComponent_RemoveInputTagBindingForAbility, reinterpret_cast<void**>(&g_c73_unbind)}, // card 073
    {&offsets::FGameplayEffectSpec_GetStackCount, reinterpret_cast<void**>(&g_get_stack_count)},
    // card 029: weapon, mech and turret audit
    {&offsets::ASTEnemyBase_StaticClass, reinterpret_cast<void**>(&g_enemy_class)},
    {&offsets::UADTargetComponent_FindTargetData, reinterpret_cast<void**>(&g_c103_find)}, // card 103
    {&offsets::UADTargetComponent_FindPrimaryTargetData, reinterpret_cast<void**>(&g_c532_find_primary)}, // card 532
    {&offsets::ASTTicketManager_RefreshTicketLimits, reinterpret_cast<void**>(&g_c111_refresh)}, // card 111
    {&offsets::ASTEncounterManagerBase_GetCurrentWaveID, reinterpret_cast<void**>(&g_c503_wave_id)}, // card 503
    {&offsets::ASTEnemyBase_GetSizeForZone, reinterpret_cast<void**>(&g_c122_size)}, // card 122
    {&offsets::UADTargetComponent_StaticClass, reinterpret_cast<void**>(&g_c122_target_class)}, // card 122
    {&offsets::UADHealthComponent_StaticClass, reinterpret_cast<void**>(&g_c130_health_class)}, // card 130 item 12
    {&offsets::ASTNonPlayableCharacterBase_IsAware, reinterpret_cast<void**>(&g_c130_is_aware)}, // card 130 item 14
    {&offsets::ASTNonPlayableCharacterBase_BecomeAware, reinterpret_cast<void**>(&g_c130_become_aware)},
    {&offsets::ASTNonPlayableCharacterBase_SetForcedMoveToLocation, reinterpret_cast<void**>(&g_c130_set_forced_move)},
    {&offsets::ASTNonPlayableCharacterBase_EndForcedMoveToBehaviour, reinterpret_cast<void**>(&g_c130_end_forced)},
    {&offsets::ASTNonPlayableCharacterBase_GetIsPendingForcedMoveTo, reinterpret_cast<void**>(&g_c130_forced_pending)},
    {&offsets::ASTNonPlayableCharacterBase_GetIsMovingToForcedLocation, reinterpret_cast<void**>(&g_c130_forced_moving)},
    {&offsets::ASTNonPlayableCharacterBase_StopStateMachine_Aware, reinterpret_cast<void**>(&g_c130_stop_aware)},
    {&offsets::ASTNonPlayableCharacterBase_StartStateMachine_Aware, reinterpret_cast<void**>(&g_c130_start_aware)},
    {&offsets::ASTNonPlayableCharacterBase_BeginForcedMoveToBehaviour, reinterpret_cast<void**>(&g_c130_begin_forced)},
    {&offsets::USMInstance_GetActiveStateName, reinterpret_cast<void**>(&g_c130_sm_state_name)},
    {&offsets::USTAttackComponent_StaticClass, reinterpret_cast<void**>(&g_c130_attack_class)},
    {&offsets::USTAttackComponent_IsAttacking, reinterpret_cast<void**>(&g_c130_is_attacking)},
    {&offsets::USTAttackComponent_AbortAttack, reinterpret_cast<void**>(&g_c130_abort_attack)},
    {&offsets::ASTNonPlayableCharacterBase_IsOverkilledComplete, reinterpret_cast<void**>(&g_c130_overkilled)},
    {&offsets::UADHealthComponent_IsDead, reinterpret_cast<void**>(&g_c130_is_dead)},             // card 130 item 12
    {&offsets::ASTLaser_SetLaserActive, reinterpret_cast<void**>(&g_c132_set_laser_active)}, // card 132 step 2d
    {&offsets::ASTHazardousZoneBase_SetHazardActive, reinterpret_cast<void**>(&g_c132_set_hazard_active)}, // card 132
    {&offsets::ASTHazardousZoneBase_BeginMoveToClosedPosition, reinterpret_cast<void**>(&g_c132_hazard_close)}, // 132
    {&offsets::ASTHazardousZoneBase_BeginMoveToOpenPosition, reinterpret_cast<void**>(&g_c132_hazard_open)}, // card 132
    {&offsets::ASTLargeElevator_NextFloor, reinterpret_cast<void**>(&g_c132_elevator_next)}, // card 132 step 2d
    {&offsets::ASTLargeElevator_PrevFloor, reinterpret_cast<void**>(&g_c132_elevator_prev)}, // card 132 step 2d
    {&offsets::ASTLargeElevator_SetMoveDirection, reinterpret_cast<void**>(&g_c132_elevator_dir)}, // card 132 step 2d
    {&offsets::FHealthCompDamageEventDelegate_Broadcast, reinterpret_cast<void**>(&g_c131_broadcast_damage)}, // card 131
    {&offsets::ASTNonPlayableCharacterBase_SetCurrentBaseState, reinterpret_cast<void**>(&g_c131_set_base_state)}, // card 131
    {&offsets::ASTNonPlayableCharacterBase_StopStateMachine_Dying, reinterpret_cast<void**>(&g_c318_stop_dying_sm)}, // card 318
    {&offsets::ASTNonPlayableCharacterBase_StartStateMachine_Core, reinterpret_cast<void**>(&g_c318_start_core_sm)}, // card 318
    {&offsets::ASTNonPlayableCharacterBase_BP_OnRevived, reinterpret_cast<void**>(&g_c318_on_revived)}, // card 318
    {&offsets::USTCommonUtilityLibrary_GetSTHUD, reinterpret_cast<void**>(&g_c319_get_hud)}, // card 319
    {&offsets::ASTPlayerCharacter_SetMeshVisibility, reinterpret_cast<void**>(&g_c297_set_mesh_visibility)}, // card 297
    {&offsets::ASTPlayerController_FindLargestMinimapScanningRange, reinterpret_cast<void**>(&g_c319_scan_range)}, // card 319
    {&offsets::ASTHUDGameplay_StaticClass, reinterpret_cast<void**>(&g_c319_hud_gameplay_class)}, // card 319
    {&offsets::USTBugProximityAreaWidget_StaticClass, reinterpret_cast<void**>(&g_c319_area_class)}, // card 319 b249
    {&offsets::USTMinimapComponent_StaticClass, reinterpret_cast<void**>(&g_minimap_class)}, // card 319 b251
    {&offsets::USTBugProximityIconWidget_StaticClass, reinterpret_cast<void**>(&g_c319_icon_class)}, // card 319 b249
    {&offsets::ASTFallZonesBase_StaticClass, reinterpret_cast<void**>(&g_c321_fall_zone_class)}, // card 321
    {&offsets::USTMusicManager_SetAndPlayMusic, reinterpret_cast<void**>(&g_c321_set_and_play)}, // card 321
    {&offsets::USTMusicManager_SetToAuto, reinterpret_cast<void**>(&g_c321_set_to_auto)}, // card 321 b227
    {&offsets::USTMusicManager_TriggerMusic, reinterpret_cast<void**>(&g_c321_trigger_music)}, // card 321 b228
    {&offsets::UGameplayStatics_StaticClass, reinterpret_cast<void**>(&g_c337_statics_class)}, // card 337
    {&offsets::AADLevelStreamingVolume_StaticClass, reinterpret_cast<void**>(&g_c321_streaming_class)}, // card 321
    {&offsets::UPrimitiveComponent_IsOverlappingActor, reinterpret_cast<void**>(&g_c321_prim_overlapping)}, // card 321
    {&offsets::UEngineTypes_ConvertObjectTypeToChannel, reinterpret_cast<void**>(&g_c319_type_channel)}, // card 319
    {&offsets::UPrimitiveComponent_GetCollisionResponseToChannel, reinterpret_cast<void**>(&g_c319_response)}, // card 319
    {&offsets::FGameplayTagCountContainer_UpdateTagMap_Internal, reinterpret_cast<void**>(&g_c131_update_tag_map)}, // card 131
    {&offsets::USTFedScoreUtilityLibrary_GetFedScoreSubsystem, reinterpret_cast<void**>(&g_c131_score_subsystem)}, // card 131
    {&offsets::FText_GetEmpty, reinterpret_cast<void**>(&g_c475_text_empty)}, // card 475
    {&offsets::USTFedScoreSubsystem_GetFedScore, reinterpret_cast<void**>(&g_c131_fed_score)}, // card 131
    {&offsets::USTEncounterWaveBase_SetProceduralEnemies, reinterpret_cast<void**>(&g_c112_set_procedural)}, // card 112
    {&offsets::USTCommonUtilityLibrary_GetCurrentDifficultySettings, reinterpret_cast<void**>(&g_c114_current)}, // card 114
    {&offsets::USTDifficulty_LoadSettings, reinterpret_cast<void**>(&g_c114_load)}, // card 114
    {&offsets::USTDifficulty_GetCurrentDifficultyLevel, reinterpret_cast<void**>(&g_c491_difficulty_level)}, // card 491
    {&offsets::ASTCharacter_StaticClass, reinterpret_cast<void**>(&g_st_character_class)}, // card 037
    {&offsets::ASTPlayerMechBase_StaticClass, reinterpret_cast<void**>(&g_mech_class)},
    {&offsets::ASTMountableTurret_StaticClass, reinterpret_cast<void**>(&g_turret_class)},
    {&offsets::ASTPlayerCharacter_EquipWeapon, reinterpret_cast<void**>(&g_equip_weapon)},
    {&offsets::ASTPlayerCharacterMI_SendWeaponSwitchRequest, reinterpret_cast<void**>(&g_c141_send_switch)}, // card 141
    {&offsets::USTWeaponUtilityLibrary_IsWeaponInUnderslungState, reinterpret_cast<void**>(&g_c141_underslung_state)}, // card 141
    {&offsets::UADWeaponInventory_RemoveWeaponByInstance, reinterpret_cast<void**>(&g_remove_weapon)},
    {&offsets::USTWeaponInventory_GiveWeaponByInstance, reinterpret_cast<void**>(&g_give_by_instance)}, // b16: card 312
    {&offsets::ASTPlayerMechBase_RequestLeaveMech, reinterpret_cast<void**>(&g_mech_request_leave)},
    {&offsets::ASTPlayerMechBase_OnInteraction, reinterpret_cast<void**>(&g_mech_on_interaction)},
    {&offsets::USTTacticalSupportComponent_IsPlayerInBlockingVolume, reinterpret_cast<void**>(&g_in_blocking_volume)},
    {&offsets::USTTacticalSupportComponent_GetAbilityCharge, reinterpret_cast<void**>(&g_c100_charge)},
    {&offsets::USTTacticalSupportSlot_UpdateAbilityCharge, reinterpret_cast<void**>(&g_c104_update_charge)}, // card 104
    {&offsets::USTTacticalSupportSlot_SetIsAbilityReadyForUse, reinterpret_cast<void**>(&g_c104_set_ready)}, // card 104
    {&offsets::USTTacticalSupportSlot_DestroyTacticalSupportAbility, reinterpret_cast<void**>(&g_c104_destroy_slot)}, // card 333
    {&offsets::ASTMountableTurret_OnInteraction, reinterpret_cast<void**>(&g_turret_on_interaction)},
    {&offsets::ASTMountableTurret_HandleInputLookHorizontal, reinterpret_cast<void**>(&g_turret_look_h)},
    {&offsets::ASTMountableTurret_HandleInputLookVertical, reinterpret_cast<void**>(&g_turret_look_v)}, // card 125
    {&offsets::USceneComponent_SetRelativeRotation, reinterpret_cast<void**>(&g_set_rel_rotation)}, // card 125
    {&offsets::USceneComponent_SetRelativeScale3D, reinterpret_cast<void**>(&g_set_rel_scale)}, // card 359
    {&offsets::USceneComponent_AddWorldOffset, reinterpret_cast<void**>(&g_add_world_offset)}, // card 359 b191
    {&offsets::USTSaveSystemLibrary_IsLoadInProgress, reinterpret_cast<void**>(&g_c245_load_in_progress)}, // card 245
    {&offsets::ASTPlayerCharacterMI_SetArmsVisbility, reinterpret_cast<void**>(&g_set_arms_visibility)}, // card 125
    {&offsets::LexToString_ENetCloseResult, reinterpret_cast<void**>(&g_c128_lex)}, // card 128
    // card 028: remote player sprite body
    {&offsets::UADSpriteAnimatorComponent_StartAnimationSequence, reinterpret_cast<void**>(&g_start_sequence)},
    {&offsets::USTSpriteRenderComponent_StaticClass, reinterpret_cast<void**>(&g_sprite_render_class)},
    {&offsets::USTSpriteRenderComponent_FitMeshToCollider, reinterpret_cast<void**>(&g_fit_mesh)},
    {&offsets::USTSpriteVariantAnimatorComponent_StaticClass, reinterpret_cast<void**>(&g_variant_animator_class)},
    {&offsets::USTCombatUtilityLibrary_DealDamage, reinterpret_cast<void**>(&g_deal_damage)}, // card 030
    {&offsets::FURL_Ctor, reinterpret_cast<void**>(&g_furl_ctor)},
    {&offsets::FURL_Dtor, reinterpret_cast<void**>(&g_furl_dtor)},
    {&offsets::UWidget_SetIsEnabled, reinterpret_cast<void**>(&g_widget_set_is_enabled)},
    {&offsets::USTGameInstance_GetCurrentChapterKillZ, reinterpret_cast<void**>(&g_get_chapter_kill_z)}, // card 046
    {&offsets::UADHealthUtilityLibrary_DealDamageTags, reinterpret_cast<void**>(&g_health_deal_damage)}, // card 048
    {&offsets::USTSaveComponent_GetSaveId, reinterpret_cast<void**>(&g_get_save_id)}, // card 048
    {&offsets::USTSaveComponent_StaticClass, reinterpret_cast<void**>(&g_c531_static_class)}, // card 531
    {&offsets::IEnhancedInputSubsystemInterface_RemoveMappingContext, reinterpret_cast<void**>(&g_remove_mapping_context)}, // card 048
    {&offsets::UWorld_GetSubsystem_LevelObjective, reinterpret_cast<void**>(&g_c49_get_subsystem)}, // card 049
    {&offsets::USTLevelObjectiveSubsystem_DoesObjectiveExistForID, reinterpret_cast<void**>(&g_c49_exists)}, // card 049
    {&offsets::USTObjectiveUtilityLibrary_StaticClass, reinterpret_cast<void**>(&g_c49_library_class)}, // card 049
    {&offsets::USTLevelObjective_StaticClass, reinterpret_cast<void**>(&g_c49_objective_class)}, // card 049
    {&offsets::USTLevelObjectiveCount_StaticClass, reinterpret_cast<void**>(&g_c49_count_class)}, // card 049
    {&offsets::ASTLevelObjectiveManager_StaticClass, reinterpret_cast<void**>(&g_c49_manager_class)}, // card 049
    {&offsets::ASTWeaponPickup_StaticClass, reinterpret_cast<void**>(&g_c50_weapon_pickup_class)}, // card 050
    {&offsets::AADPickupBase_StaticClass, reinterpret_cast<void**>(&g_c56_pickup_class)}, // card 056
    {&offsets::AADPickupBase_GetOverlappingCharacter, reinterpret_cast<void**>(&g_c50_overlapping_character)}, // card 140
    {&offsets::AADPickupBase_SetPickupEnabled, reinterpret_cast<void**>(&g_c145_set_enabled)}, // card 145
    {&offsets::ASTDoor_StaticClass, reinterpret_cast<void**>(&g_c95_door_class)}, // card 095
    {&offsets::UTimelineComponent_StaticClass, reinterpret_cast<void**>(&g_c132_timeline_class)},           // card 132
    {&offsets::ASTSatchelBuildingBase_StaticClass, reinterpret_cast<void**>(&g_c132_satchel_class)},        // card 132
    {&offsets::ASTCustomPickupBase_StaticClass, reinterpret_cast<void**>(&g_c132_custom_pickup_class)},     // card 132
    {&offsets::ASTLaser_StaticClass, reinterpret_cast<void**>(&g_c132_ext_class[0])}, // card 132 step 2d
    {&offsets::ASTHazardousZoneBase_StaticClass, reinterpret_cast<void**>(&g_c132_ext_class[1])}, // card 132 step 2d
    {&offsets::ASTJumpPadBase_StaticClass, reinterpret_cast<void**>(&g_c132_ext_class[2])}, // card 132 step 2d
    {&offsets::ASTSpawnerBase_StaticClass, reinterpret_cast<void**>(&g_c132_ext_class[3])}, // card 132 step 2d
    {&offsets::ASTTeleporterVolumeBase_StaticClass, reinterpret_cast<void**>(&g_c132_ext_class[4])}, // card 132 step 2d
    {&offsets::ASTActivationVolume_StaticClass, reinterpret_cast<void**>(&g_c132_ext_class[5])}, // card 132 step 2d
    {&offsets::ASTEnemyPresence_StaticClass, reinterpret_cast<void**>(&g_c132_ext_class[6])}, // card 132 step 2d
    {&offsets::ASTPlayerPresence_StaticClass, reinterpret_cast<void**>(&g_c132_ext_class[7])}, // card 132 step 2d
    {&offsets::ASTPlayerPresence_StaticClass, reinterpret_cast<void**>(&g_c227_presence_class)}, // card 227
    {&offsets::AActor_IsOverlappingActor, reinterpret_cast<void**>(&g_c227_is_overlapping)}, // card 227
    {&offsets::ASTLargeElevator_StaticClass, reinterpret_cast<void**>(&g_c132_ext_class[8])}, // card 132 step 2d
    {&offsets::ASTSatchelPlacementSite_StaticClass, reinterpret_cast<void**>(&g_c132_ext_class[9])}, // card 292
    {&offsets::ASTItemPickup_StaticClass, reinterpret_cast<void**>(&g_c50_item_pickup_class)}, // card 050
    {&offsets::ASTTacticalSupportPickup_StaticClass, reinterpret_cast<void**>(&g_c50_tactical_pickup_class)}, // card 050
    {&offsets::AADWeapon_StaticClass, reinterpret_cast<void**>(&g_c50_weapon_class)}, // card 050
    {&offsets::UADInteractableInterface_StaticClass, reinterpret_cast<void**>(&g_c50_interactable_class)}, // card 050
    {&offsets::ASTSwitch_StaticClass, reinterpret_cast<void**>(&g_c54_switch_class)}, // card 054
    {&offsets::UADLevelTriggerComponent_StaticClass, reinterpret_cast<void**>(&g_c65_trigger_class)}, // card 065
    {&offsets::ASTZiplinePoint_StaticClass, reinterpret_cast<void**>(&g_c54_zipline_class)}, // card 054
    {&offsets::ASTSatchelPlacementSite_StaticClass, reinterpret_cast<void**>(&g_c54_satchel_site_class)}, // card 054
    {&offsets::ASTSecretPickup_StaticClass, reinterpret_cast<void**>(&g_c54_secret_class)}, // card 054
    {&offsets::USTCommonUtilityLibrary_GetSTMetaGame, reinterpret_cast<void**>(&g_c289_get_meta)}, // card 289
    {&offsets::USTMetaGame_SecretFound, reinterpret_cast<void**>(&g_c289_secret_found)}, // card 289
    {&offsets::USTSecretsUtilityLibrary_IsSecretLocked, reinterpret_cast<void**>(&g_c289_is_locked)}, // card 289
    {&offsets::USTSecretsUtilityLibrary_UnlockSecret, reinterpret_cast<void**>(&g_c289_unlock)}, // card 289
    {&offsets::ASTLimitedInteractionActor_StaticClass, reinterpret_cast<void**>(&g_c54_limited_class)}, // card 054
    // card 052: lobby panel
    {&offsets::UBorder_StaticClass, reinterpret_cast<void**>(&g_border_class)},
    {&offsets::UVerticalBox_StaticClass, reinterpret_cast<void**>(&g_vbox_class)},
    {&offsets::USizeBox_StaticClass, reinterpret_cast<void**>(&g_sizebox_class)},
    {&offsets::UBorder_SetBrushColor_WrapperImpl, reinterpret_cast<void**>(&g_border_set_brush_color)},
    {&offsets::UBorder_SetPadding_WrapperImpl, reinterpret_cast<void**>(&g_border_set_padding)},
    {&offsets::USizeBox_SetWidthOverride_WrapperImpl, reinterpret_cast<void**>(&g_sizebox_set_width)},
    {&offsets::USizeBox_SetHeightOverride_WrapperImpl, reinterpret_cast<void**>(&g_sizebox_set_height)},
    {&offsets::UEditableTextBox_SetText_WrapperImpl, reinterpret_cast<void**>(&g_box_set_text)},
    {&offsets::FText_dtor, reinterpret_cast<void**>(&g_ftext_dtor)},
    {&offsets::UGameplayStatics_OpenLevel, reinterpret_cast<void**>(&g_c350_open_level)}, // card 350
    {&offsets::USTSaveSystemLibrary_LoadLatestSave, reinterpret_cast<void**>(&g_load_latest_save)}, // card 132
    {&offsets::USTSaveSystemLibrary_Save_Quicksave, reinterpret_cast<void**>(&g_save_quicksave)}, // card 143
    {&offsets::USignificanceManager_Get, reinterpret_cast<void**>(&g_significance_get)}, // card 132
    {&offsets::ASTPlayerController_AddSignificanceLocation, reinterpret_cast<void**>(&g_add_significance_location)}, // card 132
    {&offsets::AActor_SetActorTransform, reinterpret_cast<void**>(&g_c399_set_transform)}, // card 399
    {&offsets::AActor_DetachFromActor, reinterpret_cast<void**>(&g_c399_detach)}, // card 399
    {&offsets::APlayerController_SetViewTargetWithBlend, reinterpret_cast<void**>(&g_c399_view_blend)}, // card 399
    {&offsets::APlayerController_GetViewTarget, reinterpret_cast<void**>(&g_c399_get_view)}, // card 399
    {&offsets::UNetConnection_FindActorChannelRef, reinterpret_cast<void**>(&g_c434_find_channel)}, // card 434
    {&offsets::FNetGUIDCache_GetCacheObject, reinterpret_cast<void**>(&g_c434_cache_object)}, // card 434
    {&offsets::USceneComponent_DestroyComponent, reinterpret_cast<void**>(&g_scene_destroy_component)}, // card 409
    {&offsets::UNiagaraComponent_ReleaseToPool, reinterpret_cast<void**>(&g_c409_release_to_pool)}, // card 409
};

// The optional log-only detours (cards 065, 068, 069, 073, 074, 076, 079); each is installed only when its switch is on.
bool install_card_logs(uint8_t* base, const IMAGE_NT_HEADERS64* nt) {
    if (g_c65_log && !c65_install(base, nt)) {
        return false;
    }
    if (g_c68_log && !c68_install(base, nt)) {
        return false;
    }
    if (g_c69_log && !c69_install(base, nt)) {
        return false;
    }
    if (g_c73_log && !c73_install(base, nt)) {
        return false;
    }
    if (g_c79_log && !c79_install(base, nt)) {
        return false;
    }
    // card 136 capture points 1, 2 and 3: always installed (the card 132 audit lines stay behind UBWCOOP_C132_FX=1)
    if (!c132_fx_install(base, nt)) {
        return false;
    }
    if (g_c136_log && !c136_setvar_install(base, nt)) { // card 136 step 7 measurement
        return false;
    }
    // card 076 lines ride the card 074 log detours (the Niagara and sound spawns are in kHooksUbw since card 136)
    return !(g_c74_log || g_c76_log) || c74_install(base, nt);
}

// UBWCOOP_AUTOINPUT, kept by ubw::init for init_after_resolve.
config::AutoInput g_init_autoinput{};

// ue::install's after_resolve step: the card flags and their install gates, the lobby, the scripted input and the
// call sites the detours patch. It runs once every table row is resolved and before MH_Initialize, where the old
// install() ran them.
bool init_after_resolve(uint8_t* base, const IMAGE_NT_HEADERS64* nt) {
    (void)nt;
    install_crash_trace(); // card 118: first, so a fault in the rest of start-up is traced too
    read_card_flags();
    c52_init(g_travel.enabled);
    autoinput::init({base, offsets::APlayerController_InputKey.rva, g_get_game_world, own_pc,
                     g_get_local_player, g_fkey_ctor, g_fkey_destructor, g_key_args_ctor, g_request_exit},
                    g_init_autoinput.enabled, g_init_autoinput.delay_ms, g_init_autoinput.host_sprint);
    if (!resolve_setup_input_sites() || !resolve_tick_is_pressed_site()) {
        return false;
    }
    resolve_update_rotation_site(); // diagnostic only (card 010): a mismatch is logged, not fatal
    resolve_action_eval_site();     // diagnostic only (card 016): a mismatch is logged, not fatal
    return true;
}

} // namespace ubw::hooks

namespace ubw {

bool init(const ue::config::Travel& travel, bool client_game_only, const config::AutoInput& autoinput,
          bool auto_dropship, const config::GrantWeapon& grant, bool kill_trooper, bool dump_sprite_anims,
          bool audit_029) {
    using namespace hooks;
    g_travel = travel;
    g_travel_armed = travel.enabled;
    g_client_game_only = client_game_only;
    g_auto_dropship = auto_dropship;
    g_grant_weapon = grant;
    g_c25_kill = kill_trooper;
    g_c27_dump = dump_sprite_anims;
    g_c29_audit = audit_029;
    g_init_autoinput = autoinput;
    set_engine_steps(kEngineSteps);
    set_ops(chan_ops());
    const ue::hooktable::InstallSet game{kHooksUbw, kCallsUbw};
    return ue::install(game, offsets::kPdbGuid, offsets::kPdbAge,
                       {.after_resolve = init_after_resolve, .before_hooks = install_card_logs});
}

} // namespace ubw
