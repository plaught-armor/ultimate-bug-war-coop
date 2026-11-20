// Card 132 step 2d: the state of the kC132Ext classes (digest_c132.cpp) is the same for every player.
// These actors are placed and do not replicate: each side's copy runs its own Blueprint, and their setters
// (SetLaserActive, SetHazardActive, SetSpawnerEnabled, Activate, SetTriggerEventsEnabled, NextFloor, ...) are folded
// field writes with no mod hook. So the host polls the fields and carries a change as a card 098 'X' row
// (c98_host_ext, journalled, one row per actor, the last state wins), and the client applies it.
//   value = row << 24 | dir << 16 | ext   row: kC132Ext index; ext: lo | hi << 8 (the digest's field); dir: the
//                                         elevator's move direction byte (+0x4E8, SetMoveDirection), 0 otherwise
// Poll: the host finds the ext actors by walking every level's actor list, kExtBudget actors per tick, from where the
// last tick stopped (the client does not sweep: it resolves each row's actor by name and place on arrival and in its
// enforce pass). The host keeps the first value it sees per actor as the baseline (both sides load the same map), and
// then reads every baseline's actor each tick (a weak pointer; a destroyed actor is skipped) and sends a row on each
// change. hb37 compared each actor's value only when the 4096-actor sweep reached it: 356 us per tick on the
// host for ~30k actors; the walk now only finds actors, so it runs at a quarter of that budget. The cost per tick and
// the sweep period are measured and logged.
// Client apply, per row:
//   byte rows (JumpPad, Spawner, Teleporter, ActivationVolume, EnemyPresence, PlayerPresence): the lo byte. Their
//      setters are each a plain byte write (Spawner: SetSpawnerEnabled 0x01975B10; ActivationVolume: Activate
//      0x05022080 `movb $1, 0x2eb(%rcx)`, Deactivate 0x05152940 `movb $0`). The client's spawner byte is read by
//      CanAcceptSpawnRequest and IsSpawnerEnabled, which Blueprints reach through their exec thunks, so it is kept.
//   Laser: SetLaserActive when +0x388 differs (the setter starts the beam FX).
//   Hazard: SetHazardActive when +0x2F8 differs; BeginMoveToClosedPosition / OpenPosition when the move state +0x2F9
//      is in the other group (closed {3 closing, 0 closed}, open {2 opening, 1 open}: the tick moves 3 to 0, 2 to 1).
//   LargeElevator: ASTLargeElevator has no replicated properties, so its motion is each side's own. When the floor
//      (+0x530) differs and our copy is not moving (state +0x300 <= 1), NextFloor or PrevFloor (one floor per call;
//      they set the lerp and the floor, the elevator's tick runs the motion); once on the host's floor and settled,
//      SetMoveDirection when +0x4E8 differs.
// The client keeps the host's last value per actor and enforces it every kExtApplyMs, so a client Blueprint that
// writes a field again is put back (counted as drift), and an elevator more than one floor away steps on.
// Card 423, row 10 BP_DestroyableActor_Base_C (cave gates, bug blockers, bridge parts, Plasma nests, shields): the
// level script destroys them by a Blueprint call of `LD: Destroy`, which no hook sees. The host polls IsDestroyed like
// any ext byte. Client (c423_client), host 1 and own 0:
//   live 'X' row: waits kC423WaitMs for the client's own level script, which usually runs the same branch from the
//      replayed objective; if IsDestroyed is still 0 then, calls the actor's `LD: Destroy` (Destruction FX, then Setup
//      Destroyed). `LD: Destroy` has no IsDestroyed guard, so calling it after the script did would play the
//      explosion, sound and shake twice: the wait and the own-byte check make that the order the script did not run.
//   join replay pair, or the enforce pass finding our copy alive again: IsDestroyed = 1 and `Setup Destroyed` (mesh
//      swap or HideActor, no effects: a joiner does not get every past explosion).
// Host 0 and own 1: nothing written, one WARN per actor per map.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

C132ExtSetFn g_c132_set_laser_active = nullptr;
C132ExtSetFn g_c132_set_hazard_active = nullptr;
C132ExtCallFn g_c132_hazard_close = nullptr;
C132ExtCallFn g_c132_hazard_open = nullptr;
C132ExtCallFn g_c132_elevator_next = nullptr;
C132ExtCallFn g_c132_elevator_prev = nullptr;
C132ExtDirFn g_c132_elevator_dir = nullptr;

namespace {

// Card 481: an ext row's motion. Only the hazard (row 1, BeginMoveTo*Position) and the large elevator (row 8, its own
// lerp) move by their ext field, so ext_c132 owns their motion. Every other ext actor is also a card 140 placed actor:
// a level script K2_SetActorLocation on it (C4M2_MoveableRoad, row 10, raised at BrainBug arrival) is carried by the
// 'A' row like any placed actor's. c245b194b/c/d: both MoveableRoad copies stayed 4500 uu below the host's.
constexpr uint8_t kExtRowHazard = 1;
constexpr uint8_t kExtRowElevator = 8;

constexpr uint8_t kVisitPlaced = 1;
constexpr uint8_t kVisitExt = 2; // already a candidate by its ext row

bool ext_owns_motion(uint8_t row) {
    return row == kExtRowHazard || row == kExtRowElevator;
}

constexpr int32_t kExtBudget = 1024;     // actors looked at per tick (finding ext actors; ~30 ticks per sweep)
// Card 401: the sweeps between two full ones read a candidate list the full sweep recorded (the ext actors and the
// placed, not replicated actors with a Movable root) plus every slot past each level's recorded actor count, in place
// of every level actor. A quick sweep ends kExtQuickMs after its start, so the sweep period stays what it was.
constexpr int32_t kExtCands = 8192;
constexpr ULONGLONG kExtQuickMs = 450;
constexpr int32_t kExtQuickTicks = 24; // a quick sweep spreads its list rows over this many ticks (400 ms at 60 fps)
constexpr uint32_t kExtFullEvery = 16;   // card 330: every 16th sweep (about 8 s) reads every actor and records the list again
// Card 414: roots that are not Movable are visited every 64th sweep only (about 30 s; the first sweep of a map is
// one). The engine refuses to move such a root and no log holds the WARN for one that moved. Runs c414b108a and
// c414b109a: calls reaching c140_placed_entry fell from about 3700 to 2400 per sweep, the step mean from 73 to 65 us.
constexpr uint32_t kExtDeepEvery = 64;
constexpr int32_t kExtHostRows = 1024;   // host baselines per map
constexpr int32_t kExtClientRows = 1024; // client host values per map (card 137 3a: the join replay sends every host row)
constexpr ULONGLONG kExtApplyMs = 250;   // client enforce period
constexpr ULONGLONG kExtTotalsMs = 60000; // running totals line period
constexpr uint32_t kExtResolveTries = 240; // 60 s of tries before a WARN
constexpr uint32_t kExtLogs = 64;        // per-event log lines per map
constexpr uint32_t kExtSweepLogs = 3;
constexpr double kExtSpikeUs = 1000.0;   // a tick over this is counted (senior note 5: a recurring 1 ms spike needs a fix)
// Card 420: the join replay sends (name hash, value) pairs in 'x' frames in place of one 'X' row per actor.
constexpr int32_t kExtPackPairs = 55;  // pairs per frame: at most 22 chars each, under kChanTextMax
constexpr int32_t kExtOwn = 1024;      // client: its own ext actors (the host tracks at most kExtHostRows)
constexpr int32_t kExtPending = 256;   // client: pairs whose actor is not loaded yet
constexpr ULONGLONG kExtOwnMs = 1000;  // client: an older own-actor table is walked again before a frame's pairs
constexpr uint8_t kRowLaser = 0;
constexpr uint8_t kRowHazard = 1;
constexpr uint8_t kRowElevator = 8;
// Card 423: a live row waits this long for the client's own level script. Runs c423b125a and c423b126a: 10 of 10
// live rows with own 0 at arrival ended by LD: Destroy (the script never ran in the wait); when the script runs it
// runs before the row (own 1 at arrival). So one enforce period, for a script later in the same frame.
constexpr ULONGLONG kC423WaitMs = 250;
constexpr wchar_t kC423Class[] =
    L"/Game/Blueprints/EnvironmentElements/Buildings/ScriptedDestroyables/BP_DestroyableActor_Base."
    L"BP_DestroyableActor_Base_C";
constexpr size_t kElevatorFloor = 0x530;  // int32
constexpr size_t kElevatorFloors = 0x518; // int32 floor count
constexpr size_t kElevatorState = 0x300;  // 2/3 moving, EndMove sets 1/0
constexpr size_t kElevatorDir = 0x4E8;    // EMoveDirectionType

struct HostRow {
    const void* actor; // compared only (the sweep's lookup)
    C31Weak weak;      // the per-tick read
    uint32_t value;
    C132ExtField f; // card 411: the actor's row and byte offsets, fixed while `weak` holds (one object, one class)
};
struct ClientRow {
    char name[192];
    Vec3 at;
    C31Weak actor; // index 0 and serial 0: not resolved yet
    uint32_t value;
    uint32_t tries;
    bool warned;
    bool c423_warned;  // card 423: the "client destroyed, host alive" WARN was logged
    ULONGLONG c423_due; // card 423: a live row waiting for our own script until this time; 0: none
    ULONGLONG c423_alive_at; // card 423: our copy destroyed while the host's row said alive, since this time; 0: none
};
struct ExtSweep {
    int32_t level;
    int32_t index;
    uint32_t seen;      // ext actors this sweep
    uint32_t looked;    // actors this sweep
    uint32_t ticks;     // ticks this sweep
    ULONGLONG start_ms;
    double max_us;      // per tick, this map
    double max_poll_us; // senior note 5: that tick's poll part (the rest is the sweep chunk and the client enforce)
    int32_t max_level;  // the level the sweep chunk of that tick read (its index in UWorld::Levels)
    uint32_t over_ms;   // ticks over kExtSpikeUs this map
    double sum_us;
    uint64_t sum_ticks;
    uint32_t sweeps;
    // Card 330 split (diagnostic), host, since the last totals row: poll, the sweep's class lookup pass, its
    // c140_placed_visit pass, and the rest of the step; c140 calls, and the sweeps they span.
    double split_poll_us;
    double split_lookup_us;
    double split_visit_us;
    double split_sum_us;
    uint64_t split_ticks;
    uint64_t split_visits;
    uint32_t split_sweeps;
};
struct ExtCand {
    int32_t level; // index in UWorld::Levels
    int32_t index; // index in ULevel::Actors
    const void* actor; // compared with the slot, never read through, before use
};
struct ExtCands {
    ExtCand list[kExtCands];
    int32_t n;
    const void* level[kMaxLevels]; // the level at each index when the full sweep left it
    int32_t num[kMaxLevels];       // its actor count then; a quick sweep reads the slots from here on
    int32_t levels;
    int32_t next;        // quick sweep: the next list row
    bool ok;             // the list is a whole full sweep's
    bool over;           // the recording sweep ran out of rows: no quick sweeps until one fits
    bool quick;          // this sweep reads the list
    bool warned;
    uint32_t stale;      // list rows whose slot holds another actor or none, this map
    uint32_t quick_done; // quick sweeps this map
    uint32_t refill_seq; // card 402's ring: the rows already taken
    uint32_t refills;    // rows taken into the list, this map
};
struct ExtOwn { // card 420, client: one of our placed, unreplicated ext actors
    uint32_t hash; // ext_name_hash of its c50_name
    C31Weak actor;
};
struct ExtPending { // card 420, client: a host pair no copy of ours holds yet
    uint32_t hash, value, tries;
};
struct ExtOut { // card 420, host: one tracked actor at a join replay
    uint32_t hash, value;
    int32_t row; // its g_ext.host index
    bool dup;    // another tracked actor has the hash: sent as an 'X' row (name and place)
};
struct ExtPack { // card 420, client, this map
    ExtOwn own[kExtOwn];
    int32_t own_n;
    ULONGLONG own_ms; // the table's walk; 0: none yet
    bool own_full;
    ExtPending pending[kExtPending];
    int32_t pending_n;
    uint32_t frames, pairs, equal, bad, walks;
    double walk_us; // all walks
};
struct ExtState {
    ExtPack pack;
    ExtCands cands;
    HostRow host[kExtHostRows];
    int32_t host_n;
    ClientRow client[kExtClientRows];
    int32_t client_n;
    ExtSweep sweep;
    ULONGLONG next_apply_ms;
    ULONGLONG probe_start_ms; // card 137 2c: this map's first tick
    uint8_t probes;           // card 137 2c: pickup probes done this map
    ULONGLONG next_totals_ms; // the running totals line (a host killed at scenario end never reaches map end)
    uint32_t skipped_rep[kC132ExtClasses]; // host: placed but replicated, per row (logged once)
    uint32_t sent, received, applied, drift, missed, full, logs;
    bool totals_logged;
    const char* side; // latched at the first step of the map: side_text() reads the role after travel at map end
};
ExtState g_ext; // zeroed by c132_ext_map_end

// Card 423: this map's BP_DestroyableActor_Base_C and its IsDestroyed byte; zeroed by c132_ext_map_end (a map's
// Blueprint classes can be unloaded with it). The FNames stay: they do not depend on the map.
struct C423Class {
    void* cls;
    uint16_t lo;
    bool refused; // the property is not a native bool: no row this map (WARN logged)
};
C423Class g_c423;
uint64_t g_c423_fn_destroy = 0;
uint64_t g_c423_fn_setup = 0;

double ext_qpc_us(const LARGE_INTEGER& a, const LARGE_INTEGER& b) {
    static LARGE_INTEGER freq{};
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
    }
    return static_cast<double>(b.QuadPart - a.QuadPart) * 1e6 / static_cast<double>(freq.QuadPart);
}

uint32_t ext_value(const void* actor, const C132ExtField& f) {
    const uint8_t dir = f.row == kRowElevator ? static_cast<const uint8_t*>(actor)[kElevatorDir] : 0;
    return static_cast<uint32_t>(f.row) << 24 | static_cast<uint32_t>(dir) << 16 | f.ext;
}

bool ext_log() {
    return g_ext.logs++ < kExtLogs;
}

// --- Host ---

// Host, sweep: a new ext actor gets its baseline row. A known one is read by ext_host_poll.
void ext_host_actor(const void* actor, const C132ExtField& f) {
    if (!c50_placed(actor)) {
        return;
    }
    if (c98_replicates(actor)) {
        if (g_ext.skipped_rep[f.row]++ == 0) {
            log::line("card132: host ext %s %s replicates, left to the engine", class_name(actor).text,
                      object_name(actor).text);
        }
        return;
    }
    for (int32_t i = 0; i < g_ext.host_n; ++i) {
        HostRow& old = g_ext.host[i];
        if (old.actor != actor) {
            continue;
        }
        if (c31_weak_get(old.weak) == nullptr) { // a new actor at a destroyed one's address: its own baseline
            old.value = ext_value(actor, f);
            old.f = f;
            c31_weak_set(old.weak, const_cast<void*>(actor));
        }
        return;
    }
    if (g_ext.host_n >= kExtHostRows) {
        if (note_event_cap(g_ext.full, 1, "card132 host ext baselines")) {
            log::line("WARN card132: host ext baselines full (%d), %s not tracked", kExtHostRows,
                      object_name(actor).text);
        }
        return;
    }
    HostRow& row = g_ext.host[g_ext.host_n++];
    row = HostRow{actor, {}, ext_value(actor, f), f}; // the baseline: both sides load the same map
    c31_weak_set(row.weak, const_cast<void*>(actor));
}

// Host: one changed value, carried as an 'X' row.
void ext_host_send(void* actor, HostRow& row, uint32_t value) {
    char name[192];
    if (c50_name(actor, name, sizeof(name)) <= 0) {
        return;
    }
    const Vec3 at = actor_location(actor);
    if (ext_log()) {
        log::line("card132: host ext %s %s at (%.0f, %.0f, %.0f) 0x%08x -> 0x%08x, X row sent", class_name(actor).text,
                  name, at.x, at.y, at.z, row.value, value);
    }
    if (row.f.row == kC132ExtDestroyable) {
        log::line("card423: host t=%llu %s destroyed %u -> %u", static_cast<unsigned long long>(GetTickCount64()), name,
                  row.value & 0xFFu, value & 0xFFu);
    }
    row.value = value;
    ++g_ext.sent;
    c98_host_ext(actor, name, at, value);
}

// Host, every tick: each baseline's actor, sending a row when its value changed.
void ext_host_poll() {
    for (int32_t i = 0; i < g_ext.host_n; ++i) {
        HostRow& row = g_ext.host[i];
        void* const actor = c31_weak_get(row.weak);
        if (actor == nullptr || c220_per_player(actor)) {
            continue; // destroyed (a streamed-out level); the sweep re-baselines a new actor at its address
        }
        // Card 411: the two bytes are read at the offsets kept with the baseline, with no class lookup per tick.
        const auto* const a = static_cast<const uint8_t*>(actor);
        C132ExtField f = row.f;
        f.ext = static_cast<uint16_t>(a[f.lo] | (f.hi != 0 ? a[f.hi] << 8 : 0));
        const uint32_t value = ext_value(actor, f);
        if (row.value != value) {
            ext_host_send(actor, row, value);
        }
    }
}

// --- Client ---

bool ext_hazard_open(uint8_t move) {
    return move == 1 || move == 2;
}

// Applies the host value to our copy; true when it wrote or called anything.
bool ext_apply(void* actor, uint32_t value) {
    auto* const a = static_cast<uint8_t*>(actor);
    const uint8_t row = static_cast<uint8_t>(value >> 24);
    const uint8_t dir = static_cast<uint8_t>(value >> 16);
    const uint8_t lo = static_cast<uint8_t>(value);
    const uint8_t hi = static_cast<uint8_t>(value >> 8);
    C132ExtField f{};
    if (!c132_ext_field(actor, f) || f.row != row || row == kC132ExtDestroyable) {
        return false; // card 423: row 10 is c423_client's (a call, not a byte)
    }
    if (row == kRowLaser) {
        const bool differs = a[f.lo] != lo && g_c132_set_laser_active != nullptr;
        if (differs) {
            g_c132_set_laser_active(actor, lo != 0);
        }
        return differs;
    }
    if (row == kRowHazard) {
        bool did = false;
        if (a[f.lo] != lo && g_c132_set_hazard_active != nullptr) {
            g_c132_set_hazard_active(actor, lo != 0);
            did = true;
        }
        if (ext_hazard_open(a[f.hi]) != ext_hazard_open(hi)) {
            C132ExtCallFn const move = ext_hazard_open(hi) ? g_c132_hazard_open : g_c132_hazard_close;
            if (move != nullptr) {
                move(actor);
                did = true;
            }
        }
        return did;
    }
    if (row == kRowElevator) {
        const int32_t floor = read_i32(actor, kElevatorFloor);
        const int32_t floors = read_i32(actor, kElevatorFloors);
        const bool settled = a[kElevatorState] <= 1;
        const int32_t want = static_cast<int32_t>(lo); // the host floor's low byte; floor counts are small
        if (want >= floors || !settled) {
            return false;
        }
        if (floor != want) {
            C132ExtCallFn const step = want > floor ? g_c132_elevator_next : g_c132_elevator_prev;
            if (step != nullptr) {
                step(actor);
            }
            return step != nullptr;
        }
        const bool turn = a[kElevatorDir] != dir && g_c132_elevator_dir != nullptr;
        if (turn) {
            g_c132_elevator_dir(actor, dir);
        }
        return turn;
    }
    const bool differs = a[f.lo] != lo;
    a[f.lo] = lo;
    return differs;
}

ClientRow* ext_client_row(const char* name, const Vec3& at) {
    for (int32_t i = 0; i < g_ext.client_n; ++i) {
        ClientRow& r = g_ext.client[i];
        if (strcmp(r.name, name) == 0 && vec_dist(r.at, at) < 1.0) {
            return &r;
        }
    }
    if (g_ext.client_n >= kExtClientRows) {
        return nullptr;
    }
    ClientRow& r = g_ext.client[g_ext.client_n++];
    r = ClientRow{};
    snprintf(r.name, sizeof(r.name), "%s", name);
    r.at = at;
    return &r;
}

// Our copy of the row's actor, resolving it once (c95_find_nearest walks the world).
void* ext_client_actor(ClientRow& r, const void* world) {
    void* actor = r.actor.serial != 0 ? c31_weak_get(r.actor) : nullptr;
    if (actor != nullptr || world == nullptr || r.tries >= kExtResolveTries) {
        return actor;
    }
    ++r.tries;
    actor = c95_find_nearest(world, r.name, r.at);
    C132ExtField f{};
    if (actor != nullptr && c132_ext_field(actor, f) && f.row == static_cast<uint8_t>(r.value >> 24)) {
        c31_weak_set(r.actor, actor);
        return actor;
    }
    if (r.tries >= kExtResolveTries && !r.warned) {
        r.warned = true;
        ++g_ext.missed;
        log::line("WARN card132: client ext row %s at (%.0f, %.0f, %.0f) 0x%08x: no copy of that class after %u tries",
                  r.name, r.at.x, r.at.y, r.at.z, r.value, r.tries);
    }
    return nullptr;
}

void ext_client_line(const char* what, void* actor, const ClientRow& r, uint32_t was) {
    if (ext_log()) {
        log::line("card132: client ext %s %s %s at (%.0f, %.0f, %.0f) 0x%08x -> host 0x%08x", what,
                  class_name(actor).text, r.name, r.at.x, r.at.y, r.at.z, was, r.value);
    }
}

// --- Card 423, client: row 10 ---

enum class C423Via : uint8_t { Live, Pack, Enforce };

// The actor's own Blueprint function `name` (no parameters) by ProcessEvent; false (WARN) when it has none.
bool c423_call(void* actor, uint64_t& cache, const wchar_t* name) {
    const uint64_t fn_name = fname_cached(cache, name);
    void* const fn = fn_name != 0 ? g_find_function(actor, fn_name) : nullptr;
    if (fn == nullptr || (read_i32(fn, kFunctionParmsSize) & 0xFFFF) != 0) {
        log::line("WARN card423: client %s: no parameterless %ls function (%s), not called", object_name(actor).text,
                  name, fn == nullptr ? "absent" : "has parameters");
        return false;
    }
    alignas(16) uint8_t parms[16] = {};
    g_orig_process_event(actor, fn, parms);
    return true;
}

void c423_line(const ClientRow& r, uint8_t own, const char* by) {
    log::line("card423: client t=%llu %s destroyed %u -> %u by %s", static_cast<unsigned long long>(GetTickCount64()),
              r.name, own, r.value & 0xFFu, by);
}

// Applies the host's IsDestroyed to our copy (the file comment has the rules). True when it wrote or called.
bool c423_client(void* actor, ClientRow& r, C423Via via) {
    auto* const a = static_cast<uint8_t*>(actor);
    const uint16_t lo = c423_destroyed_offset();
    const uint8_t host = static_cast<uint8_t>(r.value) != 0 ? 1 : 0;
    const uint8_t own = lo != 0 && a[lo] != 0 ? 1 : 0;
    const ULONGLONG now = GetTickCount64();
    if (lo == 0 || host == own) {
        if (lo != 0 && host == 1 && (via != C423Via::Enforce || r.c423_due != 0)) {
            c423_line(r, own, "none"); // our own script ran (or the copy loaded destroyed)
        }
        r.c423_due = 0;
        r.c423_alive_at = 0;
        return false;
    }
    if (host == 0) {
        // Senior 2026-11-19 (c502b270a: 37 of 37 bridge parts had the host's destroyed row within 85 ms): our own
        // script runs first, so the WARN waits kC423WaitMs for the host's row.
        r.c423_alive_at = r.c423_alive_at != 0 ? r.c423_alive_at : now;
        if (!r.c423_warned && now - r.c423_alive_at >= kC423WaitMs) {
            r.c423_warned = true;
            log::line("WARN card423: client %s destroyed, host alive after %llu ms", r.name,
                      static_cast<unsigned long long>(now - r.c423_alive_at));
        }
        return false;
    }
    if (via == C423Via::Live) {
        r.c423_due = now + kC423WaitMs; // the enforce pass decides once our own script had its chance
        return false;
    }
    if (via == C423Via::Enforce && r.c423_due != 0) {
        if (now < r.c423_due) {
            return false;
        }
        r.c423_due = 0;
        const bool did = c423_call(actor, g_c423_fn_destroy, L"LD: Destroy");
        c423_line(r, own, did ? "LD: Destroy" : "none (call refused)");
        return did;
    }
    a[lo] = 1;
    const bool did = c423_call(actor, g_c423_fn_setup, L"Setup Destroyed");
    c423_line(r, own, did ? "Setup Destroyed" : "none (IsDestroyed written, call refused)");
    return true;
}

// --- Card 420, client: the host's (name hash, value) pairs ---

uint32_t ext_name_hash(const char* name) { // FNV-1a
    uint32_t h = 2166136261u;
    for (int32_t i = 0; i < 192 && name[i] != '\0'; ++i) {
        h = (h ^ static_cast<uint8_t>(name[i])) * 16777619u;
    }
    return h;
}

// The host's filter (ext_host_actor): a placed, unreplicated ext actor.
void ext_own_add(void* actor) {
    ExtPack& p = g_ext.pack;
    C132ExtField f{};
    char name[192];
    if (actor == nullptr || !c132_ext_field(actor, f) || !c50_placed(actor) || c98_replicates(actor) ||
        c50_name(actor, name, sizeof(name)) <= 0) {
        return;
    }
    if (p.own_n >= kExtOwn) {
        if (!p.own_full) {
            p.own_full = true;
            log::line("WARN card420: client own ext table full (%d), %s and later actors not compared", kExtOwn, name);
        }
        return;
    }
    ExtOwn& o = p.own[p.own_n++];
    o.hash = ext_name_hash(name);
    c31_weak_set(o.actor, actor);
}

// Every level actor once: our ext actors by name hash. The cost is summed for the totals row.
void ext_own_walk(const void* world) {
    ExtPack& p = g_ext.pack;
    LARGE_INTEGER t0;
    LARGE_INTEGER t1;
    QueryPerformanceCounter(&t0);
    p.own_n = 0;
    p.own_ms = GetTickCount64();
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = levels != nullptr ? read_i32(world, kWorldLevels + kArrayNum) : 0;
    for (int32_t l = 0; l < num_levels && l < kMaxLevels; ++l) {
        const void* const level = levels[l];
        const auto* const actors = level != nullptr ? static_cast<void* const*>(read_ptr(level, kLevelActors)) : nullptr;
        const int32_t num = actors != nullptr ? read_i32(level, kLevelActors + kArrayNum) : 0;
        for (int32_t i = 0; i < num && i < kMaxLevelActors; ++i) {
            ext_own_add(actors[i]);
        }
    }
    QueryPerformanceCounter(&t1);
    ++p.walks;
    p.walk_us += ext_qpc_us(t0, t1);
}

// Our live actor of that hash (the last one found); `count` gets how many hold it.
void* ext_own_find(uint32_t hash, int32_t& count) {
    const ExtPack& p = g_ext.pack;
    void* found = nullptr;
    count = 0;
    for (int32_t i = 0; i < p.own_n; ++i) {
        void* const actor = p.own[i].hash == hash ? c31_weak_get(p.own[i].actor) : nullptr;
        if (actor != nullptr) {
            found = actor;
            ++count;
        }
    }
    return found;
}

// One host pair. False when no copy of ours holds the hash yet (the caller keeps it pending); true when it is
// settled: the host value kept as that actor's client row and applied (nothing written when equal), or refused.
bool ext_pack_pair(uint32_t hash, uint32_t value) {
    int32_t count = 0;
    void* const actor = ext_own_find(hash, count);
    if (count == 0) {
        return false;
    }
    C132ExtField f{};
    char name[192];
    if (count > 1 || !c132_ext_field(actor, f) || f.row != static_cast<uint8_t>(value >> 24) ||
        c50_name(actor, name, sizeof(name)) <= 0) {
        ++g_ext.pack.bad;
        if (ext_log()) {
            log::line("WARN card420: client ext pair 0x%08x 0x%08x: %d copies hold the hash (%s), not applied", hash,
                      value, count, class_name(actor).text);
        }
        return true;
    }
    ClientRow* const r = ext_client_row(name, actor_location(actor));
    if (r == nullptr) {
        if (note_event_cap(g_ext.full, 1, "card132 client ext rows")) {
            log::line("WARN card132: client ext rows full (%d), %s refused", kExtClientRows, name);
        }
        return true;
    }
    r->value = value;
    r->tries = 0;
    r->warned = false;
    c31_weak_set(r->actor, actor);
    const uint32_t was = ext_value(actor, f);
    ++g_ext.applied;
    if (f.row == kC132ExtDestroyable) {
        g_ext.pack.equal += c423_client(actor, *r, C423Via::Pack) ? 0u : 1u;
        return true;
    }
    if (ext_apply(actor, value)) {
        ext_client_line("apply", actor, *r, was);
    } else {
        ++g_ext.pack.equal;
    }
    return true;
}

void ext_pending_add(uint32_t hash, uint32_t value) {
    ExtPack& p = g_ext.pack;
    if (p.pending_n >= kExtPending) {
        ++g_ext.missed;
        if (ext_log()) {
            log::line("WARN card420: client ext pending pairs full (%d), 0x%08x 0x%08x dropped", kExtPending, hash, value);
        }
        return;
    }
    p.pending[p.pending_n++] = ExtPending{hash, value, 0};
}

// Every kExtApplyMs while a pair waits for its actor (a level still streaming in): walk again, settle what resolves.
void ext_pending_step(const void* world) {
    ExtPack& p = g_ext.pack;
    if (p.pending_n == 0 || world == nullptr) {
        return;
    }
    ext_own_walk(world);
    int32_t keep = 0;
    for (int32_t i = 0; i < p.pending_n; ++i) {
        ExtPending e = p.pending[i];
        if (ext_pack_pair(e.hash, e.value)) {
            continue;
        }
        if (++e.tries >= kExtResolveTries) {
            ++g_ext.missed;
            log::line("WARN card420: client ext pair 0x%08x 0x%08x: no copy holds the hash after %u tries", e.hash,
                      e.value, e.tries);
            continue;
        }
        p.pending[keep++] = e;
    }
    p.pending_n = keep;
}

void ext_client_enforce(const void* world) {
    c137_host_frame(true); // card 137 2a: a host value re-applied
    ext_pending_step(world); // card 420
    for (int32_t i = 0; i < g_ext.client_n; ++i) {
        ClientRow& r = g_ext.client[i];
        void* const actor = ext_client_actor(r, world);
        C132ExtField f{};
        if (actor == nullptr || !c132_ext_field(actor, f)) {
            continue;
        }
        if (f.row == kC132ExtDestroyable) {
            const bool waited = r.c423_due != 0;
            const bool did = c423_client(actor, r, C423Via::Enforce);
            g_ext.drift += did && !waited ? 1u : 0u; // a live row's deferred call is not drift
            continue;
        }
        const uint32_t was = ext_value(actor, f);
        if (ext_apply(actor, r.value)) {
            ++g_ext.drift;
            ext_client_line("enforce", actor, r, was);
        }
    }
    c137_host_frame(false);
}

// --- Host: the sweep ---

void ext_cand_add(int32_t level, int32_t index, const void* actor) {
    ExtCands& c = g_ext.cands;
    if (c.n >= kExtCands) {
        c.over = true;
        return;
    }
    c.list[c.n++] = ExtCand{level, index, actor};
}

// Card 401, quick sweep: the list rows, up to `budget` of them. A row counts only while its slot still holds its
// actor (a destroyed actor leaves a null slot; a level swapped at that index fails the level compare).
int32_t ext_sweep_cands(void* const* levels, int32_t num_levels, int32_t budget) {
    ExtCands& c = g_ext.cands;
    ExtSweep& s = g_ext.sweep;
    LARGE_INTEGER ta;
    LARGE_INTEGER tb;
    QueryPerformanceCounter(&ta);
    for (; c.next < c.n && budget > 0; ++c.next, --budget) {
        const ExtCand& e = c.list[c.next];
        const void* const level = e.level < num_levels ? levels[e.level] : nullptr;
        const auto* const actors = level != nullptr && level == c.level[e.level]
                                       ? static_cast<void* const*>(read_ptr(level, kLevelActors))
                                       : nullptr;
        const int32_t num = actors != nullptr ? read_i32(level, kLevelActors + kArrayNum) : 0;
        if (e.index >= num || actors[e.index] != e.actor) {
            ++c.stale;
            continue;
        }
        ++s.looked;
        C132ExtField f{};
        if (c132_ext_field(e.actor, f)) {
            ++s.seen;
            ext_host_actor(e.actor, f);
            if (!ext_owns_motion(f.row)) {
                c140_placed_visit(e.actor, false); // card 481
            }
            continue;
        }
        ++s.split_visits;
        c140_placed_visit(e.actor, false);
    }
    QueryPerformanceCounter(&tb);
    s.split_visit_us += ext_qpc_us(ta, tb);
    return budget;
}

// The end of a sweep: its row, then the next sweep's kind. False while a quick sweep waits out its period.
bool ext_sweep_end(const void* world, int32_t num_levels) {
    ExtSweep& s = g_ext.sweep;
    ExtCands& c = g_ext.cands;
    const ULONGLONG now = GetTickCount64();
    if (c.quick && (c.next < c.n || now - s.start_ms < kExtQuickMs)) {
        return false;
    }
    if (s.start_ms != 0 && (c.quick ? c.quick_done < kExtSweepLogs : s.sweeps < kExtSweepLogs)) {
        log::line("card132: %s ext sweep %u: %u actors, %u ext, %u ticks, %llu ms, %s, %d candidates (%u stale, "
                  "%u from refilled slots)",
                  side_text(), s.sweeps + 1, s.looked, s.seen, s.ticks, now - s.start_ms, c.quick ? "quick" : "whole",
                  c.n, c.stale, c.refills);
    }
    if (c.quick) {
        ++c.quick_done;
    } else {
        c.ok = !c.over;
        c.levels = num_levels < kMaxLevels ? num_levels : kMaxLevels;
        if (c.over && !c.warned) {
            c.warned = true;
            log::line("WARN card401: %s ext candidates full (%d): every sweep reads every actor", side_text(), kExtCands);
        }
    }
    s.sweeps += s.start_ms != 0 ? 1u : 0u;
    s.split_sweeps += s.start_ms != 0 ? 1u : 0u;
    // Card 402 run c401b92a: a new actor can sit in a slot below the level's recorded count, which a quick sweep
    // never reads. Card 402's slot compare reports those; they join the list. Rows lost: this sweep reads every slot.
    C402New fresh[kC402Ring];
    int32_t fresh_n = 0;
    const bool kept = c402_refills(world, c.refill_seq, fresh, static_cast<int32_t>(kC402Ring), fresh_n);
    c.quick = c.ok && kept && s.sweeps % kExtFullEvery != 0;
    c.next = 0;
    for (int32_t i = 0; c.quick && i < fresh_n; ++i) {
        const C402New& f = fresh[i];
        if (f.level < c.levels && f.index < c.num[f.level]) { // a slot past the count is read by the sweep itself
            ext_cand_add(f.level, f.index, f.actor);
            ++c.refills;
        }
    }
    if (!c.quick) {
        c.ok = false; // recorded again by this sweep
        c.over = false;
        c.n = 0;
    }
    s.level = 0;
    s.index = 0;
    s.seen = 0;
    s.looked = 0;
    s.ticks = 0;
    s.start_ms = now;
    return true;
}

void ext_sweep_tick(const void* world) {
    ExtSweep& s = g_ext.sweep;
    ExtCands& c = g_ext.cands;
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = levels != nullptr ? read_i32(world, kWorldLevels + kArrayNum) : 0;
    int32_t budget = kExtBudget;
    if (c.quick && c.next < c.n) {
        (void)ext_sweep_cands(levels, num_levels, c.n / kExtQuickTicks + 1);
    }
    for (int32_t guard = 0; guard < kMaxLevels && budget > 0; ++guard) {
        if (s.level >= num_levels || s.level >= kMaxLevels) {
            (void)ext_sweep_end(world, num_levels);
            return;
        }
        const void* const level = levels[s.level];
        const auto* const actors = level != nullptr ? static_cast<void* const*>(read_ptr(level, kLevelActors)) : nullptr;
        const int32_t num = actors != nullptr ? read_i32(level, kLevelActors + kArrayNum) : 0;
        if (c.quick) {
            // card 401: only the slots added since the full sweep left this level (all of them when the level is new)
            const int32_t from = s.level < c.levels && c.level[s.level] == level ? c.num[s.level] : 0;
            s.index = s.index < from ? from : s.index;
        }
        // Card 330 split: a lookup pass (ext actors handled, the others marked), then a c140 visit pass over the same
        // index range, each timed as a whole.
        uint8_t visit[kExtBudget]; // 0 none, kVisitPlaced a placed actor, kVisitExt an ext actor (card 481)
        const int32_t first = s.index;
        LARGE_INTEGER ta;
        LARGE_INTEGER tb;
        LARGE_INTEGER tc;
        QueryPerformanceCounter(&ta);
        for (; s.index < num && s.index < kMaxLevelActors && budget > 0; ++s.index, --budget) {
            ++s.looked;
            C132ExtField f{};
            visit[s.index - first] = 0;
            if (actors[s.index] == nullptr) {
                continue;
            }
            if (!c132_ext_field(actors[s.index], f)) {
                visit[s.index - first] = kVisitPlaced;
                continue;
            }
            ++s.seen;
            ext_host_actor(actors[s.index], f);
            visit[s.index - first] = ext_owns_motion(f.row) ? 0 : kVisitExt;
            if (!c.quick) {
                ext_cand_add(s.level, s.index, actors[s.index]);
            }
        }
        QueryPerformanceCounter(&tb);
        for (int32_t i = first; i < s.index; ++i) {
            if (visit[i - first] != 0) {
                ++s.split_visits;
                // card 140: placed actors host logic moves; card 330, card 414: roots that are not Movable every
                // kExtDeepEvery-th sweep only (the first sweep of a map is one)
                c140_placed_visit(actors[i], s.sweeps % kExtDeepEvery == 0);
                if (!c.quick && visit[i - first] == kVisitPlaced && c140_placed_candidate(actors[i])) {
                    ext_cand_add(s.level, i, actors[i]);
                }
            }
        }
        QueryPerformanceCounter(&tc);
        s.split_lookup_us += ext_qpc_us(ta, tb);
        s.split_visit_us += ext_qpc_us(tb, tc);
        if (s.index >= num || s.index >= kMaxLevelActors) {
            if (!c.quick) {
                c.level[s.level] = level;
                c.num[s.level] = num < kMaxLevelActors ? num : kMaxLevelActors;
            }
            ++s.level;
            s.index = 0;
        }
    }
}

} // namespace

// Card 423: the class once it is loaded (a class-row cache miss asks, so about once per class per map while absent).
// IsDestroyed must be a native bool (FieldMask 0xFF): the row reads and writes it as a byte.
void* c423_destroyable_class() {
    if (g_c423.cls != nullptr || g_c423.refused) {
        return g_c423.cls;
    }
    void* const cls = find_class(kC423Class);
    if (cls == nullptr) {
        return nullptr;
    }
    const uint8_t* const p = prop_find_prop(cls, "IsDestroyed");
    const int32_t at = p != nullptr ? read_i32(p, kPropertyOffset) + p[kBoolByteOffset] : 0;
    if (p == nullptr || !prop_kind_is(p, "BoolProperty") || p[kBoolFieldMask] != 0xFF || at <= 0 || at > 0xFFFF) {
        g_c423.refused = true;
        log::line("WARN card423: %s BP_DestroyableActor_Base_C: IsDestroyed %s, no ext row this map", side_text(),
                  p == nullptr ? "absent" : "not a native bool");
        return nullptr;
    }
    g_c423.lo = static_cast<uint16_t>(at);
    g_c423.cls = cls;
    log::line("card423: %s BP_DestroyableActor_Base_C found, IsDestroyed at +0x%X", side_text(), at);
    return cls;
}

// Card 423 (c423b125a round 4: "StaticMeshActor_95 destroyed host 0001 client 0000", row applied 0.8 s before
// the snapshot, `LD: Destroy` called 1.4 s after it): a live row's wait is a state the ledger calls applied, so
// the digest takes the host's value for such a copy. Always false on the host (no client rows).
bool c423_waiting(const void* actor) {
    for (int32_t i = 0; i < g_ext.client_n && i < kExtClientRows; ++i) {
        const ClientRow& r = g_ext.client[i];
        if (r.c423_due != 0 && r.actor.serial != 0 && c31_weak_get(r.actor) == actor) {
            return true;
        }
    }
    return false;
}

uint16_t c423_destroyed_offset() {
    return g_c423.cls != nullptr ? g_c423.lo : 0;
}

// Client: the host's 'X' row (world_c98.cpp c98_receive_world).
void c132_client_ext(void* pc, const char* name, const Vec3& at, uint32_t value) {
    ++g_ext.received;
    ClientRow* const r = ext_client_row(name, at);
    if (r == nullptr) {
        if (note_event_cap(g_ext.full, 1, "card132 client ext rows")) {
            log::line("WARN card132: client ext rows full (%d), %s refused", kExtClientRows, name);
        }
        c132_ledger_result(C132Result::Refused, "client ext table full");
        return;
    }
    r->value = value;
    r->tries = 0;
    r->warned = false;
    void* const actor = ext_client_actor(*r, g_actor_get_world(pc));
    C132ExtField f{};
    if (actor == nullptr || !c132_ext_field(actor, f)) {
        c132_ledger_result(C132Result::Pending, "no copy yet, retried every 250 ms");
        if (ext_log()) {
            log::line("card132: client ext row %s at (%.0f, %.0f, %.0f) 0x%08x: no copy yet, pending", name, at.x,
                      at.y, at.z, value);
        }
        return;
    }
    if (f.row == kC132ExtDestroyable) {
        c423_client(actor, *r, C423Via::Live);
        g_ext.applied += 1;
        c132_ledger_result(C132Result::Applied, nullptr);
        return;
    }
    const uint32_t was = ext_value(actor, f);
    const bool did = ext_apply(actor, value);
    g_ext.applied += 1;
    ext_client_line(did ? "apply" : "apply (already equal)", actor, *r, was);
    c132_ledger_result(C132Result::Applied, nullptr);
}

// Card 420: client. An 'x' frame's text after "x ": "n hash value hash value ..." (c132_ext_replay). Each pair is
// compared with our own actor of that name hash; the client row and the apply are those of an 'X' row.
void c420_client_ext_pack(void* pc, const char* pairs) {
    ExtPack& p = g_ext.pack;
    const void* const world = g_actor_get_world(pc);
    if (world != nullptr && (p.own_ms == 0 || GetTickCount64() - p.own_ms >= kExtOwnMs)) {
        ext_own_walk(world);
    }
    char* end = nullptr;
    const unsigned long n = strtoul(pairs, &end, 10);
    const char* at = end;
    unsigned long done = 0;
    for (; done < n && done < static_cast<unsigned long>(kExtPackPairs); ++done) {
        char* after_hash = nullptr;
        char* after_value = nullptr;
        const unsigned long hash = strtoul(at, &after_hash, 10);
        const unsigned long value = strtoul(after_hash, &after_value, 10);
        if (after_hash == at || after_value == after_hash) {
            break;
        }
        at = after_value;
        ++g_ext.received;
        ++p.pairs;
        if (!ext_pack_pair(static_cast<uint32_t>(hash), static_cast<uint32_t>(value))) {
            ext_pending_add(static_cast<uint32_t>(hash), static_cast<uint32_t>(value));
        }
    }
    ++p.frames;
    const bool ok = n > 0 && done == n && *at == '\0';
    if (!ok) {
        log::line("WARN card420: client: bad ext pair frame (%lu of %lu pairs read) \"%.80s\"", done, n, pairs);
    }
    c132_ledger_result(ok ? C132Result::Applied : C132Result::Refused, ok ? nullptr : "bad ext pair frame");
}

// The totals so far: "map" at map end, "running" every kExtTotalsMs.
// Card 137 2c: the pickup probe at 2 s and 12 s after this map's first ext tick, then a summary every 30 s (P7).
void c137_probe_tick(const void* world, ULONGLONG now) {
    constexpr ULONGLONG kProbeAtMs[2] = {2000, 12000};
    constexpr const char* kProbeWhen[2] = {"load", "load+10s"};
    constexpr ULONGLONG kProbeEveryMs = 30000; // then a summary line every 30 s (the digest round's pace), card 137 P7
    constexpr uint8_t kProbeMax = 60;          // 30 min of summaries per map
    if (g_ext.probe_start_ms == 0) {
        g_ext.probe_start_ms = now;
    }
    const ULONGLONG due = g_ext.probes < 2 ? kProbeAtMs[g_ext.probes]
                                          : kProbeAtMs[1] + kProbeEveryMs * static_cast<ULONGLONG>(g_ext.probes - 1);
    if (g_ext.probes < kProbeMax && now >= g_ext.probe_start_ms + due) {
        c137_pickup_probe(world, g_ext.probes < 2 ? kProbeWhen[g_ext.probes] : "running", g_ext.probes < 2);
        ++g_ext.probes;
    }
}

void ext_totals_line(const char* when) {
    const ExtSweep& s = g_ext.sweep;
    if (s.sum_ticks == 0) {
        return;
    }
    log::line("card132: %s ext %s totals: %d baselines, %d client rows, %u sent, %u received, %u applied, %u "
              "drift, %u missed, %u sweeps, cost avg %.1f us max %.1f us per tick over %llu ticks (max tick: poll %.1f us, "
              "sweep chunk in level %d; %u ticks over %.0f us)",
              g_ext.side != nullptr ? g_ext.side : side_text(), when, g_ext.host_n, g_ext.client_n, g_ext.sent,
              g_ext.received, g_ext.applied, g_ext.drift, g_ext.missed, s.sweeps,
              s.sum_us / static_cast<double>(s.sum_ticks), s.max_us, static_cast<unsigned long long>(s.sum_ticks),
              s.max_poll_us, s.max_level, s.over_ms, kExtSpikeUs);
    const ExtPack& p = g_ext.pack;
    if (p.frames != 0) {
        log::line("card420: client ext pairs %s: %u frames, %u pairs, %u equal, %u refused, %d pending, own table %d "
                  "actors, %u walks avg %.0f us",
                  when, p.frames, p.pairs, p.equal, p.bad, p.pending_n, p.own_n, p.walks,
                  p.walks != 0 ? p.walk_us / static_cast<double>(p.walks) : 0.0);
    }
    ExtSweep& w = g_ext.sweep;
    if (w.split_ticks != 0) {
        const double n = static_cast<double>(w.split_ticks);
        const double sweeps = w.split_sweeps != 0 ? static_cast<double>(w.split_sweeps) : 1.0;
        log::line("card330: %s ext split since the last row: %llu ticks, per tick poll %.1f us, class lookup %.1f us, "
                  "c140 visit %.1f us, rest %.1f us (total %.1f us); c140 calls %llu, per sweep %.0f calls, %.0f past "
                  "the placed/replicated early-out, %.0f reach c140_placed_entry (%u sweeps)",
                  g_ext.side != nullptr ? g_ext.side : side_text(), static_cast<unsigned long long>(w.split_ticks),
                  w.split_poll_us / n, w.split_lookup_us / n, w.split_visit_us / n,
                  (w.split_sum_us - w.split_poll_us - w.split_lookup_us - w.split_visit_us) / n, w.split_sum_us / n,
                  static_cast<unsigned long long>(w.split_visits), static_cast<double>(w.split_visits) / sweeps,
                  static_cast<double>(g_c140_visit_passed) / sweeps, static_cast<double>(g_c140_visit_entry) / sweeps,
                  w.split_sweeps);
    }
    w.split_poll_us = 0.0;
    w.split_lookup_us = 0.0;
    w.split_visit_us = 0.0;
    w.split_sum_us = 0.0;
    w.split_ticks = 0;
    w.split_visits = 0;
    w.split_sweeps = 0;
    g_c140_visit_passed = 0;
    g_c140_visit_entry = 0;
}

void c132_ext_step(void* engine) {
    void* const world = g_get_game_world(engine);
    if (world == nullptr) {
        return;
    }
    LARGE_INTEGER t0;
    LARGE_INTEGER t1;
    QueryPerformanceCounter(&t0);
    if (g_ext.side == nullptr) {
        g_ext.side = side_text();
    }
    LARGE_INTEGER tp = t0;
    const int32_t level = g_ext.sweep.level;
    if (!g_client_session) {
        ext_host_poll();
        QueryPerformanceCounter(&tp);
        ext_sweep_tick(world); // the client only resolves the host's rows, on arrival and in the enforce pass
    }
    const ULONGLONG now = GetTickCount64();
    c137_probe_tick(world, now);
    if (g_client_session && now >= g_ext.next_apply_ms) {
        g_ext.next_apply_ms = now + kExtApplyMs;
        ext_client_enforce(world);
    }
    QueryPerformanceCounter(&t1);
    const double us = ext_qpc_us(t0, t1);
    ExtSweep& s = g_ext.sweep;
    ++s.ticks;
    if (us > s.max_us) {
        s.max_us = us;
        s.max_poll_us = ext_qpc_us(t0, tp);
        s.max_level = level;
    }
    s.over_ms += us > kExtSpikeUs ? 1u : 0u;
    s.sum_us += us;
    ++s.sum_ticks;
    if (!g_client_session) {
        s.split_poll_us += ext_qpc_us(t0, tp);
        s.split_sum_us += us;
        ++s.split_ticks;
    }
    if (g_ext.next_totals_ms == 0) {
        g_ext.next_totals_ms = now + kExtTotalsMs;
    } else if (now >= g_ext.next_totals_ms) {
        g_ext.next_totals_ms = now + kExtTotalsMs;
        ext_totals_line("running");
    }
}

// Card 420: an actor whose name hash no other tracked actor shares goes as a (hash, value) pair, kExtPackPairs per
// 'x' frame; the client compares each with its own copy. The others keep the 'X' row (name and place).
int32_t ext_replay_collect(ExtOut* out) {
    int32_t n = 0;
    for (int32_t i = 0; i < g_ext.host_n; ++i) {
        void* const actor = c31_weak_get(g_ext.host[i].weak);
        C132ExtField f{};
        char name[192];
        if (actor == nullptr || !c132_ext_field(actor, f) || c50_name(actor, name, sizeof(name)) <= 0) {
            continue;
        }
        out[n++] = ExtOut{ext_name_hash(name), ext_value(actor, f), i, false};
    }
    for (int32_t a = 0; a < n; ++a) {
        for (int32_t b = a + 1; b < n; ++b) {
            if (out[a].hash == out[b].hash) {
                out[a].dup = true;
                out[b].dup = true;
            }
        }
    }
    return n;
}

// One 'x' frame of the pairs from out[from] on. Returns the index after the last row it read.
int32_t ext_replay_frame(void* pc, const ExtOut* out, int32_t from, int32_t n, int32_t& sent, int32_t& frames) {
    char body[kChanTextMax + 1];
    int32_t len = 0;
    int32_t pairs = 0;
    int32_t i = from;
    body[0] = '\0';
    for (; i < n && pairs < kExtPackPairs && len < static_cast<int32_t>(sizeof(body)) - 24; ++i) {
        if (out[i].dup) {
            continue;
        }
        len += snprintf(body + len, sizeof(body) - static_cast<size_t>(len), " %u %u", out[i].hash, out[i].value);
        ++pairs;
    }
    char text[kChanTextMax + 1];
    snprintf(text, sizeof(text), "x %d%s", pairs, body);
    if (pairs > 0 && c98_send_ext_pack(pc, text)) {
        sent += pairs;
        ++frames;
    }
    return i;
}

// Card 137 3a: host. A joining PC gets the current value of every tracked ext actor, not only the changed ones: the
// host's baseline is its own first read, and a fresh client load can hold other per-instance values. The client's
// apply is idempotent (ext_apply writes or calls only on a difference), so a row equal to its copy does nothing.
void c132_ext_replay(void* pc) {
    static ExtOut out[kExtHostRows]; // game thread
    const int32_t live = ext_replay_collect(out);
    int32_t sent = 0;
    int32_t frames = 0;
    int32_t named = 0;
    for (int32_t i = 0; i < live; ++i) {
        void* const actor = out[i].dup ? c31_weak_get(g_ext.host[out[i].row].weak) : nullptr;
        char name[192];
        if (actor != nullptr && c50_name(actor, name, sizeof(name)) > 0 &&
            c98_send_ext(pc, name, actor_location(actor), out[i].value)) {
            ++named;
        }
    }
    for (int32_t at = 0, guard = 0; at < live && guard < kExtHostRows; ++guard) {
        at = ext_replay_frame(pc, out, at, live, sent, frames);
    }
    sent += named;
    log::line("%scard137: host ext join replay to %s: %d of %d live rows sent (%d tracked; card 420: %d pair frames, "
              "%d as name rows)",
              sent == live ? "" : "WARN ", object_name(pc).text, sent, live, g_ext.host_n, frames, named);
}

// Card 326: the registry's "ext" map end (was the arm).
void c132_ext_map_end() {
    ext_totals_line("map");
    memset(&g_ext, 0, sizeof(g_ext)); // POD; a temporary ExtState would be ~130 KB of stack
    g_c423 = C423Class{};
}

int32_t c132_ext_live() {
    return g_ext.host_n + g_ext.client_n;
}

} // namespace ubw::hooks
