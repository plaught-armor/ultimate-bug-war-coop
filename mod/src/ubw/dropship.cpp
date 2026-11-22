// Dropship: the host's dropship descent gate log, pause requests (card 015) and UBWCOOP_AUTODROPSHIP
// (card 015).

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

// --- Card 015: the host's dropship descent gate (log only) ---
// ASTProceduralDropship fields, each read off a function of this build (card 015 report).
constexpr size_t kDropshipSpline = 0x400;           // USplineComponent* (StartAction stack arg; TickBehaviours needs it)
constexpr size_t kDropshipNavDirection = 0x480;     // StartAction r8
constexpr size_t kDropshipStartup = 0x481;          // StartAction dl
constexpr size_t kDropshipTargetPoint = 0x482;      // StartAction r9: spline end 0 = descend, 1 = Destroy, 2 = SetState(2)
constexpr size_t kDropshipSplineEnded = 0x483;      // TickSplineTravel +0xD16..: 1 once the spline-end action ran
constexpr size_t kDropshipSplineDistance = 0x484;   // float, TickSplineTravel +0x69F
constexpr size_t kDropshipDescentZ = 0x498;         // double, TickMoveDescent
constexpr size_t kDropshipBehaviour = 0x538;        // TickBehaviours: 1 ascent, 2 descent, else spline travel
constexpr size_t kDropshipDescentTargetZ = 0x5F8;   // double; TickMoveDescent lands when it reaches +0x498
constexpr size_t kDropshipBeginPlayDone = 0x654;    // ASTPlayerTransportDropship::BeginPlay stores 1; TickDescent reads it

constexpr size_t kDropshipStartWithPlayer = 0x720;  // ASTPlayerTransportDropship::BeginPlay: EnterDropship when set
constexpr size_t kDropshipManualControl = 0x7AC;    // SetManualControl +0x14 store (free look)
constexpr uint32_t kDropshipLogs = 24;              // per map: StartAction / SetState / SkipFlight / descent lines
constexpr uint32_t kDropshipWatchLines = 60;        // per map
constexpr uint32_t kDropshipWatchFastLines = 30;    // 1 s apart, then kDropshipWatchSlowMs apart
constexpr ULONGLONG kDropshipWatchFastMs = 1000;
constexpr ULONGLONG kDropshipWatchSlowMs = 10000;

StartActionFn g_orig_start_action = nullptr;
SetStateFn g_orig_set_state = nullptr;
SkipFlightFn g_orig_skip_flight = nullptr;
SkipFlightFn g_call_skip_flight = nullptr;
DropshipFn g_orig_begin_descent = nullptr;
StaticClassFn g_transport_dropship_class = nullptr; // ASTPlayerTransportDropship
ConstBoolFn g_actor_tick_enabled = nullptr;          // AActor::IsActorTickEnabled

ConstIntFn g_world_num_pcs = nullptr;                // UWorld::GetNumPlayerControllers
constexpr size_t kActorPrimaryTick = 0x28;           // AActor::PrimaryActorTick (FActorTickFunction)
uint32_t g_dropship_logs = 0;                       // game thread only, per map

DropshipWatch g_dropship_watch{};

DropshipTickFn g_orig_transport_tick = nullptr;
DropshipTickFn g_orig_tick_behaviours = nullptr;
DropshipTickFn g_orig_tick_spline_travel = nullptr;

// Hot paths (every dropship, every frame): counters only, and only while the watch is armed.
void detour_transport_tick(void* dropship, float delta_seconds) {
    if (g_dropship_watch_armed) {
        ++g_dropship_watch.ticks;
        g_dropship_watch.last_delta = delta_seconds;
    }
    g_orig_transport_tick(dropship, delta_seconds);
}

void detour_tick_behaviours(void* dropship, float delta_seconds) {
    if (g_dropship_watch_armed && is_a(dropship, g_transport_dropship_class())) {
        ++g_dropship_watch.tick_behaviours;
    }
    g_orig_tick_behaviours(dropship, delta_seconds);
}

void detour_tick_spline_travel(void* dropship, float delta_seconds) {
    if (g_dropship_watch_armed && is_a(dropship, g_transport_dropship_class())) {
        ++g_dropship_watch.spline_travels;
    }
    g_orig_tick_spline_travel(dropship, delta_seconds);
}

void log_dropship_call(const char* what, const uint8_t* d, unsigned state_before, unsigned arg,
                       unsigned long long caller) {
    if (note_event_cap(g_dropship_logs, kDropshipLogs, "dropship call")) {
        log::line("dropship: %s %s(%p %s, LocalRole %u, %u) caller exe+0x%llX, state %u -> %u, behaviour %u", side_text(),
                  what, static_cast<const void*>(d), class_name(d).text, d[kActorLocalRole], arg, caller, state_before,
                  d[kDropshipState], d[kDropshipBehaviour]);
    }
}

void detour_start_action(void* dropship, uint8_t startup, uint8_t nav_direction, uint8_t target_point, void* spline) {
    const auto* const d = static_cast<const uint8_t*>(dropship);
    const unsigned before = d[kDropshipState];
    g_orig_start_action(dropship, startup, nav_direction, target_point, spline);
    if (note_event_cap(g_dropship_logs, kDropshipLogs, "dropship call")) {
        log::line("dropship: %s StartAction(%p %s, LocalRole %u, startup %u, nav %u, target point %u, spline %p) caller "
                  "exe+0x%llX, state %u -> %u, behaviour %u",
                  side_text(), dropship, class_name(d).text, d[kActorLocalRole], startup, nav_direction, target_point,
                  spline, exe_rva(_ReturnAddress()), before, d[kDropshipState], d[kDropshipBehaviour]);
    }
}

// --- Card 132: troop dropship state relay (visuals only) ---
// ASTProceduralDropship::SetState (exe+0x51DC3C0) writes +0x69B and fires the StateChange BlueprintNativeEvent. On
// BP_ProceduralDropshipTroops that event drives the doors, door steam, ground smoke, thrusters, the flares
// (NS_FX_WA_Flares at the landing spot) and the door and landing sounds. The flight (TickBehaviours) runs on the
// authority only, so a client copy never reaches a new state and plays none of it (hb54: the host played 16
// MS_EMIT_Dropship_Flare_Fire, the client 0). The host sends "<placed name> <state>" for every troop dropship state
// change; the client calls SetState on its placed copy of that name. The client's copy moves by replication, so
// only the state and its visuals are applied, no flight. The per-map journal keeps each dropship's last state for
// a PC that joins later (c132_dropship_replay from its first Pong).
StaticClassFn g_troops_dropship_class = nullptr; // ASTProceduralDropshipTroops
StaticClassFn g_spline_class = nullptr;          // card 236: USplineComponent

// Sets AActor::bAlwaysRelevant by reflection. Returns 1 when this call set it, 0 when it was already set, -1 when
// the reflected bool is missing (dropships, card 140; player pawns, card 554).
int32_t actor_always_relevant(void* actor) {
    const uint8_t* const field = prop_find_prop(read_ptr(actor, kObjectClass), "bAlwaysRelevant");
    if (field == nullptr || !prop_bool_ok(field)) {
        return -1;
    }
    uint8_t* const at = static_cast<uint8_t*>(actor) + read_i32(field, kPropertyOffset) + field[kBoolByteOffset];
    if ((*at & field[kBoolFieldMask]) != 0) {
        return 0;
    }
    *at = static_cast<uint8_t>(*at | field[kBoolFieldMask]);
    return 1;
}

namespace {

constexpr int32_t kDsRows = 64;       // per map; FED_C1M1 places 22 troop dropships (hb54 inventory)
constexpr uint32_t kDsLogLines = 48;  // per map, each side

struct DsRow {
    char name[kChanNameMax + 1];
    uint8_t state;
};

struct DsJournal {
    DsRow row[kDsRows];
    int32_t n;
    uint32_t changes, sent, full, unnamed, relevant; // host
    uint32_t received, applied, same, missed, bad;  // client
    uint32_t logs;
};

DsJournal g_ds{}; // game thread only

bool ds_frame(const char* name, unsigned state, char* out, size_t cap) {
    return snprintf(out, cap, "%s %u", name, state) > 0;
}

// Host: make a troop dropship always net relevant, once, at its first state change (card 140 run 7). The flight
// runs far from the players (the park spot is about 145000 uu out); past the default net cull distance (15000 uu)
// the client's copy stops getting location updates and freezes mid-flight, together with the spawners, spawn
// points and landing blocker attached to it, which then sit far from the host's. bAlwaysRelevant keeps the
// replicated movement flowing at any distance. Returns false when the reflected bool is missing.
bool ds_host_always_relevant(void* dropship) {
    return actor_always_relevant(dropship) >= 0;
}

// Host: journal the state, then send it to every remote PC past its first Pong (the rest get the replay).
void ds_host_note(void* dropship, uint8_t state) {
    char name[kChanNameMax + 1];
    if (!c50_placed(dropship) || c50_level_key(dropship, name, sizeof(name)) <= 0) { // card 426: per level
        ++g_ds.unnamed;
        if (note_event_cap(g_ds.logs, kDsLogLines, "card132 dropship")) {
            log::line("WARN card132: host dropship %s state %u: not a placed actor with a name, not relayed",
                      class_name(dropship).text, state);
        }
        return;
    }
    ++g_ds.changes;
    int32_t i = 0;
    while (i < g_ds.n && strcmp(g_ds.row[i].name, name) != 0) {
        ++i;
    }
    if (i == g_ds.n && g_ds.n < kDsRows) {
        memcpy(g_ds.row[i].name, name, sizeof(name));
        ++g_ds.n;
        const bool relevant = ds_host_always_relevant(dropship);
        g_ds.relevant += relevant ? 1u : 0u;
        log::line("%scard140: host dropship %s set always relevant: %s", relevant ? "" : "WARN ", name,
                  relevant ? "yes" : "no, bAlwaysRelevant not found (its copy can freeze past the net cull distance)");
    }
    if (i < g_ds.n) {
        g_ds.row[i].state = state;
    } else {
        ++g_ds.full; // the live send below still goes; only a late joiner misses this one
    }
    char text[kChanNameMax + 8];
    const C95Fanout f = ds_frame(name, state, text, sizeof(text))
                            ? c95_send_all(g_actor_get_world(dropship), ChanOp::Dropship, text)
                            : C95Fanout{};
    g_ds.sent += static_cast<uint32_t>(f.sent);
    if (note_event_cap(g_ds.logs, kDsLogLines, "card132 dropship")) {
        log::line("card132: host t=%llu dropship %s state %u: sent to %d of %d PCs",
                  static_cast<unsigned long long>(GetTickCount64()), name, state, f.sent, f.to);
    }
}

} // namespace

void detour_set_state(void* dropship, uint8_t state) {
    const auto* const d = static_cast<const uint8_t*>(dropship);
    const unsigned before = d[kDropshipState];
    g_orig_set_state(dropship, state);
    log_dropship_call("SetState", d, before, state, exe_rva(_ReturnAddress()));
    if (!g_client_session && g_troops_dropship_class != nullptr && is_a(dropship, g_troops_dropship_class())) {
        ds_host_note(dropship, state);
    }
}

// --- Card 159: the dropship join snapshot ---
// A joining PC's first Pong gets one frame per placed dropship (troop and transport, read live, not from the state
// journal: BeginPlay's SetState runs inside LoadMap, before c50_arm_map resets the journal, so a join after a quiet
// start replayed 0 rows). Frame: "<name> <state> <bHidden> <mesh> <x> <y> <z> <dist> <speed> <nav> <startup> <target> <ended>
// <pitch> <yaw> <roll> <frame ms> <kind> <spline>" (card 285: pitch, yaw and roll, the actor's world rotation in degrees;
// card 583: frame ms, the host's last frame time when the frame was built)
// (card 190: startup +0x481, target point +0x482, spline ended +0x483; see DsLook),
// mesh one letter
// per kDsMeshes entry (v visible, h bHiddenInGame, i bVisible 0, b both, - no component), dist the spline distance,
// speed its rate (per s, signed, 0 parked), nav the NavDirection byte (+0x480), spline "<owner>:<component>" (the
// placed names of the spline at +0x400 and its actor; - none, ? unnamed), kind J (apply), C (check only, sent kDsCheckMs after the J round) or L
// (live: the host's tracker sends it to every PC when a placed dropship moved or changed state, about once per
// second, and applies like J). Both dropship classes replicate, so the card 132 digest does not cover them.
// The client leads the host's distance by speed x (rtt / 2) and holds its copy at the spline end (0 or the spline
// length) until the next host frame: the spline-end action (StartAction, a new spline or Destroy) is authority-only,
// so a copy running on past the end goes off the host's path (c159b rejoin: -19612 against 2372). Card 165 run 1
// build (card 159 rest): the next leg's StartAction rewrites NavDirection (+0x480, c159c rejoin: C_3's copy flew back,
// 9139 uu off) and +0x400 (StartAction stores its spline argument, or with null the pick of vtable +0x720; its
// native callers exe+0x521C538 and exe+0x521E3FE, the second passing null). TickSplineTravel's own spline-end branch
// (exe+0x51E82B6..: Destroy, SetState(2) or the descent by +0x482, then +0x483 = 1) writes neither. So J and L carry
// both and the client writes them when they differ; the spline resolves by its actor's placed name and the class
// of the copy's current spline, refused (WARN) when the found component's name differs.
// State writers (+0x69B, full-exe sweep): SetState (exe+0x51DC3DA, relayed by card 132), StartAction (exe+0x51DDF31,
// 0), its callee exe+0x51C5970 (exe+0x51C5994, 0), DescendToLandCompleted (exe+0x51C2E8C, 1), exe+0x51C3027 (1), the
// constructor (exe+0x51B98B0). Several writers, so the tracker sends on any state change it sees, not one hook.
constexpr const char* kDsMeshes[] = {"DropshipMesh", "Troop Carrier Mesh"};
constexpr int32_t kDsMeshCount = 2;
constexpr double kDsTolerance = 100.0;     // uu: a client copy farther from the host's place is moved / reported
// Card 285: degrees, any axis. Off by more while the host's state is not in flight (0, IsPerformingActions: the
// client's own spline tick turns the actor there), the copy takes the host's rotation; a check over it is a WARN.
constexpr double kDsRotTolDeg = 2.0;
constexpr uint8_t kDsInFlight = 0;         // ESTDropshipState 0 = IsPerformingActions (TickSplineTravel flies it)
constexpr ULONGLONG kDsCheckMs = 5000;     // host: the check round follows the join round this long after
constexpr ULONGLONG kDsLiveMs = 1000;      // host: the tracker samples every placed dropship this often
constexpr double kDsMovedUu = 10.0;        // host: a sample this far from the last one is a move (an L frame)
constexpr float kDsJumpSpeed = 30000.0f;   // host: a faster distance change is a spline restart, speed 0
constexpr uint32_t kDsEdgeRows = 64;       // card342 rows per session
constexpr ULONGLONG kDsEdgeMs = 100;       // host: after a launch edge the next sample comes this soon (card 342)
constexpr double kDsSlackMs = 50.0;        // the moving tolerance's slack on top of rtt / 2
constexpr double kDsLeadCapMs = 250.0;     // card 159 reopen: the one-way lag the lead and tolerance use at most
constexpr ULONGLONG kDsLoadSettleMs = 10000; // card 159 reopen: client, no lead this long after its map load (c165e:
                                             // the 2056 ms pong came 3 s after the load)
constexpr int32_t kDsLiveRows = 32;        // tracked dropships per side (c159b: 4 loaded at once; 22 placed)
constexpr uint32_t kDsLiveLines = 24;      // per map: client L lines and hold lines
constexpr int32_t kDsSplineChars = 2 * kChanNameMax + 2; // "<owner>:<component>"
constexpr int32_t kDsFrameChars = kChanNameMax + kDsSplineChars + 144; // one frame's text (card 285: + rotation)
constexpr uint8_t kDsNoPropagation = 0;    // EVisibilityPropagation::NoPropagation
constexpr const char* kDsVisible[] = {"bVisible", nullptr};
constexpr const char* kDsHiddenInGame[] = {"bHiddenInGame", nullptr};

struct DsLook {
    uint8_t state;
    uint8_t hidden;
    char mesh[kDsMeshCount + 1];
    Vec3 at;
    float dist; // spline distance (TickSplineTravel advances it in states that fly the spline)
    uint8_t nav;                   // NavDirection (+0x480)
    // Card 190: the speed bytes TickSplineTravel reads each tick (StartAction writes them on the host only): startup
    // +0x481 (1 ramps up over the first settings+0x60 uu), target point +0x482 (0 slows down over the last
    // settings+0x70 uu and begins the descent at the end), and spline ended +0x483 (1 once the end action ran).
    uint8_t startup;
    uint8_t target;
    uint8_t ended;
    char spline[kDsSplineChars];   // the spline's identity (see ds_spline_id)
    Vec3 rot;                      // card 285: the root's world rotation (pitch, yaw, roll in degrees)
};

struct DsLiveRow { // host: the tracker's last sample; client: a copy the hold watches
    C31Weak ds;
    Vec3 at;
    float dist;
    float speed;
    const void* spline;
    ULONGLONG ms;
    uint8_t state;
    uint8_t held; // client: the hold logged since the last host frame
};

struct DsSnapState {
    C31Weak check_pc[kC17Slots]; // host: PCs owed a check round
    ULONGLONG check_due[kC17Slots];
    DsLiveRow rows[kDsLiveRows];
    int32_t n;
    ULONGLONG live_due;                                   // host
    uint32_t live_frames, live_sends, full;               // host
    uint32_t checks, matched, mismatched, applied_fields; // client
    uint32_t live_got, live_fields, holds, live_lines;    // client
    uint32_t navs, splines, spline_refused;               // client: NavDirection and spline writes, refusals
    uint32_t speed_bytes;                                 // client (card 190): startup, target and ended writes
    ULONGLONG load_ms;                                    // this side's map load (c132_dropship_arm_map)
    ULONGLONG step_ms;                                    // host (card 583): the last c159_dropship_step
    uint32_t frame_ms;                                    // host (card 583): the frame time before this step
};

DsSnapState g_ds_snap{}; // game thread only

// Card 414: ds_look runs each tick per tracked dropship, so the by-name lookups are kept per class: the mesh
// field offsets per dropship class, the two visibility bits per mesh component class (-1: no such field).
// c132_dropship_arm_map clears both, so a class freed with its map is never matched by address.
struct DsBit {
    int32_t byte; // offset of the byte in the component, -1 when the bool is absent
    uint8_t mask;
};
struct DsLookCache {
    const void* ds_class;
    int32_t mesh_offset[kDsMeshCount];
    const void* comp_class[kDsMeshCount];
    DsBit hidden[kDsMeshCount];
    DsBit visible[kDsMeshCount];
};
DsLookCache g_ds_look{}; // game thread only

DsBit ds_bit(const void* cls, const char* const* names) {
    const uint8_t* const p = c27_find_any(cls, names);
    if (!prop_kind_is(p, "BoolProperty") || !prop_bool_ok(p)) {
        return DsBit{-1, 0};
    }
    return DsBit{read_i32(p, kPropertyOffset) + p[kBoolByteOffset], p[kBoolFieldMask]};
}

void* ds_mesh(const void* ds, int32_t m) {
    const void* const cls = ds != nullptr ? read_ptr(ds, kObjectClass) : nullptr;
    if (cls == nullptr) {
        return nullptr;
    }
    if (cls != g_ds_look.ds_class) {
        for (int32_t i = 0; i < kDsMeshCount; ++i) {
            const uint8_t* const p = prop_find_prop(cls, kDsMeshes[i]);
            ObjectName kind{};
            const bool object = p != nullptr && prop_kind(p, kind) == PropKind::Object;
            g_ds_look.mesh_offset[i] = object ? read_i32(p, kPropertyOffset) : -1;
        }
        g_ds_look.ds_class = cls;
    }
    const int32_t offset = g_ds_look.mesh_offset[m];
    return offset >= 0 ? read_ptr(ds, static_cast<size_t>(offset)) : nullptr;
}

// `m` picks the cache row (the two meshes can be of different component classes).
char ds_mesh_letter(const void* comp, int32_t m) {
    if (comp == nullptr) {
        return '-';
    }
    const void* const cls = read_ptr(comp, kObjectClass);
    if (cls != g_ds_look.comp_class[m]) {
        g_ds_look.hidden[m] = ds_bit(cls, kDsHiddenInGame);
        g_ds_look.visible[m] = ds_bit(cls, kDsVisible);
        g_ds_look.comp_class[m] = cls;
    }
    const auto* const c = static_cast<const uint8_t*>(comp);
    const DsBit h = g_ds_look.hidden[m];
    const DsBit v = g_ds_look.visible[m];
    const bool hidden = h.byte >= 0 && (c[h.byte] & h.mask) != 0;
    const bool invisible = v.byte >= 0 && (c[v.byte] & v.mask) == 0;
    return hidden ? (invisible ? 'b' : 'h') : (invisible ? 'i' : 'v');
}

// "<owner>:<component>" placed names of a spline component and its actor; "-" for none, "?" when a name fails.
void ds_spline_id(const void* spline, char* out, size_t cap) {
    char owner[kChanNameMax + 1];
    char comp[kChanNameMax + 1];
    if (spline == nullptr) {
        snprintf(out, cap, "-");
        return;
    }
    const bool named = c50_name(read_ptr(spline, kComponentOwner), owner, sizeof(owner)) > 0 &&
                       c50_name(spline, comp, sizeof(comp)) > 0;
    if (!named || snprintf(out, cap, "%s:%s", owner, comp) <= 0) {
        snprintf(out, cap, "?");
    }
}

DsLook ds_look(const void* ds) {
    const auto* const d = static_cast<const uint8_t*>(ds);
    DsLook l{d[kDropshipState], static_cast<uint8_t>((d[kActorHiddenByte] & kActorHiddenBit) != 0 ? 1 : 0), {},
             actor_location(ds), 0.0f, d[kDropshipNavDirection], d[kDropshipStartup], d[kDropshipTargetPoint],
             d[kDropshipSplineEnded], {}, {}};
    memcpy(&l.dist, d + kDropshipSplineDistance, sizeof(l.dist));
    ds_spline_id(read_ptr(ds, kDropshipSpline), l.spline, sizeof(l.spline));
    for (int32_t m = 0; m < kDsMeshCount; ++m) {
        l.mesh[m] = ds_mesh_letter(ds_mesh(ds, m), m);
    }
    l.mesh[kDsMeshCount] = '\0';
    const void* const root = read_ptr(ds, kActorRootComponent);
    l.rot = root != nullptr ? component_rotator(root) : Vec3{};
    return l;
}

// This side's row for a dropship; add takes a free row (or one whose dropship is gone).
DsLiveRow* ds_row(const void* ds, bool add) {
    int32_t free_row = -1;
    for (int32_t i = 0; i < g_ds_snap.n; ++i) {
        const void* const at = c31_weak_get(g_ds_snap.rows[i].ds);
        if (at == ds) {
            return &g_ds_snap.rows[i];
        }
        free_row = at == nullptr && free_row < 0 ? i : free_row;
    }
    if (!add) {
        return nullptr;
    }
    if (free_row < 0 && g_ds_snap.n >= kDsLiveRows) {
        ++g_ds_snap.full;
        return nullptr;
    }
    DsLiveRow& r = g_ds_snap.rows[free_row >= 0 ? free_row : g_ds_snap.n++];
    r = DsLiveRow{};
    c31_weak_set(r.ds, const_cast<void*>(ds));
    return &r;
}

// The check and apply tolerance (uu): 100 parked; moving, plus the travel in the frame's one-way time and slack.
// Card 159 reopen: the one-way lag (ms), the lowest recent rtt / 2 (card 340) capped at kDsLeadCapMs. One raw sample (c165e: 2056 ms
// during the post-travel load) led C_20 8582 uu behind the host.
double ds_lag_ms() {
    const double lag = static_cast<double>(pl_rtt_floor()) / 2.0;
    return lag < kDsLeadCapMs ? lag : kDsLeadCapMs;
}

double ds_tolerance(float speed) {
    const double lag_ms = ds_lag_ms() + kDsSlackMs;
    return speed == 0.0f ? kDsTolerance : kDsTolerance + fabs(static_cast<double>(speed)) * lag_ms / 1000.0;
}

bool ds_frame(char* text, size_t cap, const char* name, const DsLook& l, float speed, char kind) {
    // The spline identity goes last and runs to the end of the text: placed component names can hold spaces
    // ("Default Spline" on the troop dropships, run c165b).
    const int n = snprintf(text, cap, "%s %u %u %s %.0f %.0f %.0f %.0f %.0f %u %u %u %u %.1f %.1f %.1f %u %c %s",
                           name, l.state, l.hidden, l.mesh, l.at.x, l.at.y, l.at.z, static_cast<double>(l.dist),
                           static_cast<double>(speed), l.nav, l.startup, l.target, l.ended, l.rot.x, l.rot.y, l.rot.z,
                           g_ds_snap.frame_ms, kind, l.spline);
    return n > 0 && static_cast<size_t>(n) < cap;
}

// Host: one snapshot round (kind J or C) of every placed dropship to one PC. Returns the frames sent.
int32_t ds_snapshot_send(void* pc, char kind, int32_t* found) {
    const StaticClassFn* const cls[] = {&g_troops_dropship_class, &g_transport_dropship_class};
    static C23List list;
    int32_t sent = 0;
    *found = 0;
    for (const StaticClassFn* c : cls) {
        if (*c == nullptr) {
            continue;
        }
        c23_collect(g_actor_get_world(pc), nullptr, (*c)(), list);
        for (uint32_t i = 0; i < list.count; ++i) {
            void* const ds = list.e[i].actor;
            char name[kChanNameMax + 1];
            if (!c50_placed(ds) || c50_level_key(ds, name, sizeof(name)) <= 0) { // card 426: per level
                continue;
            }
            ++*found;
            if (kind == 'J') {
                ds_host_always_relevant(ds);
            }
            const DsLiveRow* const r = ds_row(ds, false);
            char text[kDsFrameChars];
            sent += ds_frame(text, sizeof(text), name, ds_look(ds), r != nullptr ? r->speed : 0.0f, kind) &&
                            c95_send_one(pc, ChanOp::Dropship, text)
                        ? 1
                        : 0;
        }
    }
    return sent;
}

// Client: the host's spline on the copy: its actor by placed name (any class, c50_find_placed_any), the component by
// USplineComponent (FindComponentByClass; the base class finds any subclass), kept only when its name matches.
// Returns 1 when written. Cost (card 236): the whole-world search runs only while the two spline names differ; once
// the host's spline is written the names match and the function returns at its first test.
uint32_t ds_client_spline(void* ds, const DsLook& own, const DsLook& host, const void* world) {
    if (strcmp(own.spline, host.spline) == 0 || host.spline[0] == '-' || host.spline[0] == '?') {
        return 0;
    }
    char owner_name[kChanNameMax + 1] = {};
    const char* const colon = strchr(host.spline, ':');
    const size_t owner_len = colon != nullptr ? static_cast<size_t>(colon - host.spline) : 0;
    void* owner = nullptr;
    void* found = nullptr;
    if (owner_len > 0 && owner_len <= static_cast<size_t>(kChanNameMax) && g_spline_class != nullptr) {
        memcpy(owner_name, host.spline, owner_len);
        owner = c50_find_placed_any(world, owner_name);
        found = owner != nullptr ? find_component_of(owner, g_spline_class()) : nullptr;
    }
    char got[kDsSplineChars];
    ds_spline_id(found, got, sizeof(got));
    if (found == nullptr || strcmp(got, host.spline) != 0) {
        ++g_ds_snap.spline_refused;
        if (note_event_cap(g_ds_snap.live_lines, kDsLiveLines, "card159 dropship live")) {
            if (owner == nullptr) {
                log::line("WARN card159: client t=%llu dropship %s spline %s (host) not resolved: owner not found, "
                          "kept %s",
                          static_cast<unsigned long long>(GetTickCount64()), object_name(ds).text, host.spline,
                          own.spline);
            } else {
                log::line("WARN card159: client t=%llu dropship %s spline %s (host) not resolved: owner %s has no "
                          "spline %s (found %s), kept %s",
                          static_cast<unsigned long long>(GetTickCount64()), object_name(ds).text, host.spline,
                          owner_name, colon + 1, got, own.spline);
            }
        }
        return 0;
    }
    auto* const slot = static_cast<uint8_t*>(ds) + kDropshipSpline;
    memcpy(slot, &found, sizeof(found));
    ++g_ds_snap.splines;
    return 1;
}

// Client (card 190): the host's startup and target point bytes when they differ, and spline ended once the host's
// is 1 (the host's end action ran; the copy's own end branch must not run it again). Returns the fields written.
uint32_t ds_client_speed_bytes(void* ds, const DsLook& own, const DsLook& host) {
    auto* const d = static_cast<uint8_t*>(ds);
    uint32_t fields = 0;
    if (own.startup != host.startup) {
        d[kDropshipStartup] = host.startup;
        ++fields;
    }
    if (own.target != host.target) {
        d[kDropshipTargetPoint] = host.target;
        ++fields;
    }
    if (host.ended == 1 && own.ended != 1) {
        d[kDropshipSplineEnded] = 1;
        ++fields;
    }
    g_ds_snap.speed_bytes += fields;
    return fields;
}

// Card 285: the largest per-axis rotation difference (degrees, each wrapped to -180..180).
double ds_rot_off(const Vec3& a, const Vec3& b) {
    const double d[3] = {a.x - b.x, a.y - b.y, a.z - b.z};
    double worst = 0.0;
    for (const double v : d) {
        worst = fmax(worst, fabs(fmod(v + 540.0, 360.0) - 180.0));
    }
    return worst;
}

// --- Card 449: troop dropship takeoff is the host's decision, for every player ---
// ASTProceduralDropshipTroops::TickBehaviours runs on both sides and, landed with the doors shut, asks CanTakeoff
// (exe+0x51F5680): false while GetSTPlayerCharacter (player 0) stands on the ship, else true once no trooper is left
// in the bay; true runs StartAction(1, nav, 2, null). Run c423b145a: a rejoined client's copy took off 6.4 s before
// the host's (WARN card159 state 0/1, 2205 uu off). Host: the check also runs in each server copy's acting scope
// (card 102; GetSTPlayerCharacter goes through the card 099 GetPlayerController gate), so a client's trooper on the
// ship holds it as the host's does. Client: a copy with a host row never decides; c449_own_look runs the same
// StartAction when the host's frame shows the takeoff.
constexpr uint8_t kC449Landed = 1;  // ESTDropshipState 1: IsLanded (exe+0x51CCDB0) reads +0x69B == 1
constexpr uint8_t kC449Startup = 1; // the takeoff's StartAction arguments
constexpr uint8_t kC449Target = 2;
constexpr uint32_t kC449Rows = 48;  // per session
C449CanTakeoffFn g_orig_c449_can_takeoff = nullptr;
uint32_t g_c449_rows = 0;

// One row per ship while the same answer repeats each tick.
void c449_row(const void* ds, const char* text) {
    static const void* last = nullptr; // compared only, never read through
    if (last == ds || !note_event_cap(g_c449_rows, kC449Rows, "card449")) {
        return;
    }
    last = ds;
    char name[kChanNameMax + 1];
    if (c50_name(ds, name, sizeof(name)) <= 0) {
        snprintf(name, sizeof(name), "%s", object_name(ds).text);
    }
    log::line("card449: %s t=%llu dropship %s: %s", side_text(), static_cast<unsigned long long>(GetTickCount64()),
              name, text);
}

bool detour_c449_can_takeoff(void* dropship) {
    if (g_client_session) {
        if (ds_row(dropship, false) == nullptr) {
            return g_orig_c449_can_takeoff(dropship); // no host frame for this ship: its own check, as before
        }
        c449_row(dropship, "landed copy leaves its takeoff to the host");
        return false;
    }
    if (!g_orig_c449_can_takeoff(dropship)) {
        return false;
    }
    void* const world = g_actor_get_world(dropship);
    if (world == nullptr || g_player_character_class == nullptr) {
        return true;
    }
    const void* const cls = g_player_character_class();
    const C43Roster r = c43_roster(world);
    bool can = true;
    for (int32_t s = 0; s < kC43Max && can; ++s) {
        const void* const pawn = r.p[s].pawn;
        if (pawn == nullptr || !is_a(pawn, cls) || sprint_side(pawn) != kSideServerCopy) {
            continue;
        }
        const C102Scope saved = c102_enter(pawn, kC102Overlap, dropship);
        can = g_orig_c449_can_takeoff(dropship);
        c102_leave(saved);
    }
    if (!can) {
        c449_row(dropship, "a client's trooper stands on it: takeoff held");
    }
    return can;
}

// Client: the copy's look for a snapshot frame. On a J or L frame a landed copy whose host frame shows the takeoff
// (in flight, startup 1, target point 2) first runs the takeoff's own StartAction, so the ascent is the game's.
DsLook c449_own_look(void* ds, const char* name, const DsLook& host, char kind) {
    const DsLook own = ds_look(ds);
    const bool takeoff = kind != 'C' && own.state == kC449Landed && host.state == kDsInFlight &&
                         host.startup == kC449Startup && host.target == kC449Target;
    if (!takeoff || g_orig_start_action == nullptr) {
        return own;
    }
    g_orig_start_action(ds, kC449Startup, host.nav, kC449Target, nullptr);
    const DsLook after = ds_look(ds);
    if (note_event_cap(g_c449_rows, kC449Rows, "card449")) {
        log::line("card449: client t=%llu dropship %s: host took off, StartAction run on the landed copy (state %u "
                  "-> %u, startup %u, target %u)", static_cast<unsigned long long>(GetTickCount64()), name,
                  own.state, after.state, after.startup, after.target);
    }
    return after;
}

// Client: a snapshot frame. J and L: log the copy's values against the host's, then apply what differs, the
// distance led by speed x (rtt / 2). C: compare only, within ds_tolerance(speed).
void ds_client_snapshot(void* ds, const char* name, const DsLook& host, float speed, char kind, uint32_t host_frame_ms,
                        const void* world) {
    const DsLook own = c449_own_look(ds, name, host, kind); // card 449
    const double off = vec_dist(own.at, host.at);
    const double tol = ds_tolerance(speed);
    const bool settling = GetTickCount64() - g_ds_snap.load_ms < kDsLoadSettleMs;
    const double lead_ms = settling ? 0.0 : ds_lag_ms();
    const float target = host.dist + speed * static_cast<float>(lead_ms / 1000.0);
    const double dist_off = fabs(static_cast<double>(own.dist) - static_cast<double>(target));
    // Card 429 (c423b125a, c423b126a): at a steady 8500 uu/s the copy was 120 to 570 uu (14 to 65 ms) off the led
    // target on most L frames, which is the frame's own timing noise (141 uu is one 60 fps frame), and the fixed
    // 100 uu limit re-placed it on 372 of 473 frames. The limit grows by kDsSlackMs of travel; parked it is 100.
    const double dist_tol = kDsTolerance + fabs(static_cast<double>(speed)) * kDsSlackMs / 1000.0;
    const bool same_fields = own.state == host.state && own.hidden == host.hidden &&
                             strcmp(own.mesh, host.mesh) == 0 && own.nav == host.nav && own.startup == host.startup &&
                             own.target == host.target && strcmp(own.spline, host.spline) == 0;
    const bool same = same_fields && off <= tol;
    // Card 583 part 1: a C frame built one long host frame before the compare is that frame's travel behind
    // (c551b384a: 3400 uu at 7413 uu/s, the 459 ms frame), so the check compares the led target's distance with
    // dist_tol plus the host's last frame of travel, not the location with ds_tolerance.
    const double check_tol = dist_tol + fabs(static_cast<double>(speed)) * host_frame_ms / 1000.0;
    const double rot_off = ds_rot_off(own.rot, host.rot);
    const bool at_rest = host.state != kDsInFlight;
    const bool rot_bad = at_rest && rot_off > kDsRotTolDeg; // card 285: in flight the spline tick owns it
    if (kind == 'C') {
        const bool match = same_fields && dist_off <= check_tol && !rot_bad;
        ++g_ds_snap.checks;
        (match ? g_ds_snap.matched : g_ds_snap.mismatched) += 1;
        log::line("%scard159: client t=%llu dropship %s check: state %u/%u hidden %u/%u mesh %s/%s, %.0f uu off "
                  "(tolerance %.0f, speed %.0f, rtt %u), rotation %.1f deg off (%s; pitch %.1f/%.1f yaw %.1f/%.1f "
                  "roll %.1f/%.1f), spline distance %.0f/%.0f startup %u/%u target %u/%u ended "
                  "%u/%u nav %u/%u spline %s/%s (client/host), led distance %.0f off (tolerance %.0f, host frame %u "
                  "ms): %s",
                  match ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()), name, own.state, host.state,
                  own.hidden, host.hidden, own.mesh, host.mesh, off, tol, static_cast<double>(speed), pl_rtt_floor(),
                  rot_off, at_rest ? "at rest" : "in flight, not compared", own.rot.x, host.rot.x, own.rot.y,
                  host.rot.y, own.rot.z, host.rot.z,
                  static_cast<double>(own.dist), static_cast<double>(host.dist), own.startup, host.startup,
                  own.target, host.target, own.ended, host.ended, own.nav, host.nav, own.spline, host.spline,
                  dist_off, check_tol, host_frame_ms, match ? "match" : "differs");
        return;
    }
    DsLiveRow* const row = ds_row(ds, true);
    if (row != nullptr) {
        row->held = 0;
    }
    uint32_t fields = 0;
    if (own.state != host.state) {
        g_orig_set_state(ds, host.state);
        ++fields;
    }
    if (own.hidden != host.hidden && g_orig_c98_hidden != nullptr) {
        g_orig_c98_hidden(ds, host.hidden != 0);
        ++fields;
    }
    for (int32_t m = 0; m < kDsMeshCount; ++m) {
        void* const comp = own.mesh[m] != host.mesh[m] && host.mesh[m] != '-' ? ds_mesh(ds, m) : nullptr;
        if (comp == nullptr) {
            continue;
        }
        const char h = host.mesh[m];
        if (g_scene_set_visibility != nullptr) {
            g_scene_set_visibility(comp, h == 'v' || h == 'h', kDsNoPropagation);
        }
        if (g_orig_c132_set_hidden != nullptr) {
            g_orig_c132_set_hidden(comp, h == 'h' || h == 'b', kDsNoPropagation);
        }
        ++fields;
    }
    fields += ds_client_spline(ds, own, host, world); // before the distance: the hold clamps to this spline's length
    if (own.nav != host.nav) {
        static_cast<uint8_t*>(ds)[kDropshipNavDirection] = host.nav;
        ++g_ds_snap.navs;
        ++fields;
    }
    fields += ds_client_speed_bytes(ds, own, host); // card 190
    if (dist_off > dist_tol) {
        memcpy(static_cast<uint8_t*>(ds) + kDropshipSplineDistance, &target, sizeof(target));
        ++fields;
    }
    // Card 285: location and rotation go together. At rest a rotation over kDsRotTolDeg moves the copy too (the
    // playtest copy was put on the pad by location with its mid-flight pitch and roll); in flight only the location.
    void* const root = read_ptr(ds, kActorRootComponent);
    bool rot_set = false;
    if (off > tol || rot_bad) {
        g_set_actor_location(ds, &host.at, false, nullptr, kTeleportPhysics);
        ++fields;
        if (at_rest && root != nullptr && g_set_world_rotation != nullptr) {
            const double rot[3] = {host.rot.x, host.rot.y, host.rot.z};
            g_set_world_rotation(root, rot, false, nullptr, kTeleportPhysics);
            rot_set = true;
            ++fields;
        }
    }
    const double rot_after = root != nullptr ? ds_rot_off(component_rotator(root), host.rot) : rot_off;
    const bool rot_warn = at_rest && rot_after > kDsRotTolDeg;
    const bool live = kind == 'L';
    (live ? g_ds_snap.live_fields : g_ds_snap.applied_fields) += fields;
    g_ds_snap.live_got += live ? 1 : 0;
    if (live && !rot_warn && !note_event_cap(g_ds_snap.live_lines, kDsLiveLines, "card159 dropship live")) {
        return; // card 285: a rotation still off at rest is rowed past the cap
    }
    log::line("%scard159: client t=%llu dropship %s %s: state %u/%u hidden %u/%u mesh %s/%s, %.0f uu off (tolerance "
              "%.0f), rotation %.1f deg off %s, %.1f after (pitch %.1f/%.1f yaw %.1f/%.1f roll %.1f/%.1f), spline "
              "distance %.0f/%.0f (client/host, led to %.0f at speed %.0f, rtt %u, lead %.0f ms), startup %u/%u "
              "target %u/%u ended %u/%u nav %u/%u, spline %s/%s: %s (%u fields applied)",
              rot_warn ? "WARN " : "", static_cast<unsigned long long>(GetTickCount64()), name, live ? "live" : "at join",
              own.state, host.state, own.hidden, host.hidden, own.mesh, host.mesh, off, tol, rot_off,
              rot_set ? "(set)" : (at_rest ? "(not set)" : "(in flight, not set)"), rot_after, own.rot.x, host.rot.x,
              own.rot.y, host.rot.y, own.rot.z, host.rot.z, static_cast<double>(own.dist),
              static_cast<double>(host.dist), static_cast<double>(target), static_cast<double>(speed), pl_rtt_floor(),
              lead_ms, own.startup, host.startup, own.target, host.target, own.ended, host.ended, own.nav, host.nav,
              own.spline, host.spline, same && dist_off <= dist_tol && !rot_bad ? "same" : "differs",
              fields);
}

// Card 368, client: the placed dropship a frame names, kept by weak ref. The L frames come each second for every
// dropship and each lookup was two level walks (200 walks, 111.7 ms on the C2M1 stay of run b52).
namespace {
constexpr int32_t kDsNamed = 32; // run b53: FED_C2M1 has 25 placed dropships; 8 left 120 walks. Full: walk as before
struct DsNamed {
    char name[kChanNameMax + 1]; // empty: free
    C31Weak weak;
};
DsNamed g_ds_named[kDsNamed]{};

void* ds_find_placed(const void* world, const char* name) {
    DsNamed* slot = nullptr; // where a found dropship is kept: its own entry, else a free or dead one
    for (int32_t i = 0; i < kDsNamed; ++i) {
        DsNamed& e = g_ds_named[i];
        void* const kept = e.name[0] != '\0' ? c31_weak_get(e.weak) : nullptr;
        if (kept != nullptr && strcmp(e.name, name) == 0 && g_actor_get_world(kept) == world) {
            return kept;
        }
        slot = slot == nullptr && kept == nullptr ? &e : slot;
    }
    const StaticClassFn* const cls[] = {&g_troops_dropship_class, &g_transport_dropship_class};
    void* const ds = c50_find_placed(world, cls, 2, name);
    if (ds != nullptr && slot != nullptr && g_weak_get != nullptr && g_weak_assign != nullptr) {
        strcpy_s(slot->name, sizeof(slot->name), name);
        c31_weak_set(slot->weak, ds);
    }
    return ds;
}
} // namespace

// Client: ChanOp::Dropship. Live: "<placed name> <state>" (troop class). Snapshot: see card 159 above.
void c132_dropship_receive(void* pc, const WireString* s) {
    char text[kChanTextMax + 1];
    char name[kChanNameMax + 1];
    char mesh[kDsMeshCount + 2] = {};
    char kind[2] = {};
    unsigned state = 0;
    unsigned hidden = 0;
    Vec3 at{};
    float dist = 0.0f;
    float speed = 0.0f;
    unsigned nav = 0;
    unsigned startup = 0;
    unsigned target = 0;
    unsigned ended = 0;
    char spline[kDsSplineChars] = {};
    Vec3 rot{};
    unsigned frame_ms = 0;
    ++g_ds.received;
    const int got = chan_text(s, text, sizeof(text)) > 0
                        ? sscanf_s(text, "%64s %u %u %3s %lf %lf %lf %f %f %u %u %u %u %lf %lf %lf %u %1s %129[^\n]",
                                   name, static_cast<unsigned>(sizeof(name)), &state, &hidden, mesh,
                                   static_cast<unsigned>(sizeof(mesh)), &at.x, &at.y, &at.z, &dist, &speed, &nav,
                                   &startup, &target, &ended, &rot.x, &rot.y, &rot.z, &frame_ms, kind,
                                   static_cast<unsigned>(sizeof(kind)), spline,
                                   static_cast<unsigned>(sizeof(spline)))
                        : 0;
    const bool snap = got == 19 && (kind[0] == 'J' || kind[0] == 'C' || kind[0] == 'L') &&
                      strlen(mesh) == kDsMeshCount && hidden <= 1 && nav <= 0xFF && startup <= 0xFF &&
                      target <= 0xFF && ended <= 0xFF;
    if ((got != 2 && !snap) || state > 0xFF) {
        ++g_ds.bad;
        log::line("WARN card132: client: bad Dropship frame");
        static uint32_t s_bad_lines = 0; // card 159: the raw text of the first refused frames, for the cause
        if (s_bad_lines < 4) {
            ++s_bad_lines;
            log::line("card159: client bad Dropship frame text (%d fields): \"%s\"", got, got > 0 ? text : "");
        }
        c132_ledger_result(C132Result::Refused, "bad frame");
        return;
    }
    void* const ds = g_troops_dropship_class != nullptr && g_transport_dropship_class != nullptr
                         ? ds_find_placed(g_actor_get_world(pc), name)
                         : nullptr;
    if (ds == nullptr) {
        ++g_ds.missed;
        c132_ledger_result(C132Result::Skipped, "no placed copy matches (see the WARN line)");
        if (snap && kind[0] == 'L' && !note_event_cap(g_ds_snap.live_lines, kDsLiveLines, "card159 dropship live")) {
            return; // card 159: L frames come each second; the map totals count every miss
        }
        log::line("WARN card132: client t=%llu dropship %s state %u: no placed dropship has that name, skipped",
                  static_cast<unsigned long long>(GetTickCount64()), name, state);
        return;
    }
    if (snap) {
        DsLook host{static_cast<uint8_t>(state), static_cast<uint8_t>(hidden), {}, at, dist, static_cast<uint8_t>(nav),
                    static_cast<uint8_t>(startup), static_cast<uint8_t>(target), static_cast<uint8_t>(ended), {}, {}};
        memcpy(host.mesh, mesh, sizeof(host.mesh));
        memcpy(host.spline, spline, sizeof(host.spline));
        host.rot = rot;
        ds_client_snapshot(ds, name, host, speed, kind[0], frame_ms, g_actor_get_world(pc));
        c132_ledger_result(C132Result::Applied, nullptr);
        return;
    }
    const unsigned before = static_cast<const uint8_t*>(ds)[kDropshipState];
    if (before == state) {
        ++g_ds.same;
        c132_ledger_result(C132Result::Applied, nullptr);
        return;
    }
    g_orig_set_state(ds, static_cast<uint8_t>(state));
    ++g_ds.applied;
    c132_ledger_result(C132Result::Applied, nullptr);
    if (note_event_cap(g_ds.logs, kDsLogLines, "card132 dropship")) {
        log::line("card132: client t=%llu dropship %s state %u -> %u: applied",
                  static_cast<unsigned long long>(GetTickCount64()), name, before, state);
    }
}

// Host: a remote PC's first Pong of this map gets the join snapshot; its check round follows kDsCheckMs later.
void c132_dropship_replay(void* pc) {
    if (g_client_session || pc == nullptr) {
        return;
    }
    int32_t found = 0;
    const int32_t sent = ds_snapshot_send(pc, 'J', &found);
    int32_t slot = 0;
    while (slot < kC17Slots && c31_weak_get(g_ds_snap.check_pc[slot]) != nullptr) {
        ++slot;
    }
    if (slot < kC17Slots) {
        c31_weak_set(g_ds_snap.check_pc[slot], pc);
        g_ds_snap.check_due[slot] = GetTickCount64() + kDsCheckMs;
    }
    log::line("%scard159: host dropship snapshot (join) to %s: %d of %d sent%s", sent == found ? "" : "WARN ",
              object_name(pc).text, sent, found, slot < kC17Slots ? "" : "; no check slot free, no check round");
}

namespace {

// Host: an L frame for a tracked dropship to every PC.
void ds_live_send(const void* world, const void* ds, const DsLook& l, float speed) {
    char name[kChanNameMax + 1];
    char text[kDsFrameChars];
    if (c50_level_key(ds, name, sizeof(name)) <= 0 || !ds_frame(text, sizeof(text), name, l, speed, 'L')) {
        return;
    }
    const C95Fanout f = c95_send_all(world, ChanOp::Dropship, text);
    ++g_ds_snap.live_frames;
    g_ds_snap.live_sends += static_cast<uint32_t>(f.sent);
}

// Host: one kDsLiveMs sample. Speed is the distance change per second; a state change, a new spline or a jump
// faster than kDsJumpSpeed (the spline-end restart) gives 0. True when it moved or changed state.
bool ds_host_sample(const void* ds, DsLiveRow& r, const DsLook& l, ULONGLONG now) {
    const void* const spline = read_ptr(ds, kDropshipSpline);
    const bool fresh = r.ms == 0;
    const bool changed = !fresh && (l.state != r.state || vec_dist(l.at, r.at) > kDsMovedUu);
    const float dt = static_cast<float>(now - r.ms) / 1000.0f;
    float speed = fresh || dt <= 0.0f ? 0.0f : (l.dist - r.dist) / dt;
    speed = l.state != r.state || spline != r.spline || fabsf(speed) > kDsJumpSpeed ? 0.0f : speed;
    r.at = l.at;
    r.dist = l.dist;
    r.speed = speed;
    r.spline = spline;
    r.ms = now;
    r.state = l.state;
    return changed;
}

// Host, card 342: a ship in flight with sampled speed 0 that starts along its spline sends an L that frame, and
// the next sample comes kDsEdgeMs later for its real speed. Without it the client's copy flew on its own timing
// until the next kDsLiveMs sample (run 15: 5244 uu behind at the first L, and a check frame with speed 0).
// Cost: one float read per such row per frame.
void ds_host_launch_edge(const void* world, ULONGLONG now) {
    for (int32_t i = 0; i < g_ds_snap.n; ++i) {
        DsLiveRow& r = g_ds_snap.rows[i];
        const void* const ds = r.ms != 0 && r.state == kDsInFlight && r.speed == 0.0f ? c31_weak_get(r.ds) : nullptr;
        if (ds == nullptr || fabsf(read_f32(ds, kDropshipSplineDistance) - r.dist) <= static_cast<float>(kDsMovedUu)) {
            continue;
        }
        const DsLook l = ds_look(ds);
        if (l.state != r.state) {
            continue; // the state loop above owns a state change
        }
        ds_host_sample(ds, r, l, now);
        ds_live_send(world, ds, l, r.speed);
        g_ds_snap.live_due = now + kDsEdgeMs < g_ds_snap.live_due ? now + kDsEdgeMs : g_ds_snap.live_due;
        static uint32_t rows = 0;
        if (!note_event_cap(rows, kDsEdgeRows, "card342")) {
            continue;
        }
        char name[kChanNameMax + 1];
        if (c50_name(ds, name, sizeof(name)) <= 0) {
            snprintf(name, sizeof(name), "%s", object_name(ds).text); // no instance name: the class-like name
        }
        log::line("card342: host t=%llu dropship %s launch edge: spline distance %.0f, speed %.0f",
                  static_cast<unsigned long long>(now), name, static_cast<double>(l.dist),
                  static_cast<double>(r.speed));
    }
}

// Host: tracked rows send an L the frame their state changes (several writers, see above); every placed dropship
// is sampled each kDsLiveMs and sends an L when it moved or changed state.
void ds_host_track(const void* world, ULONGLONG now) {
    for (int32_t i = 0; i < g_ds_snap.n; ++i) {
        DsLiveRow& r = g_ds_snap.rows[i];
        const void* const ds = c31_weak_get(r.ds);
        if (ds == nullptr || r.ms == 0 || static_cast<const uint8_t*>(ds)[kDropshipState] == r.state) {
            continue;
        }
        const DsLook l = ds_look(ds);
        r.state = l.state;
        r.speed = 0.0f;
        ds_live_send(world, ds, l, 0.0f);
    }
    ds_host_launch_edge(world, now);
    if (now < g_ds_snap.live_due) {
        return;
    }
    g_ds_snap.live_due = now + kDsLiveMs;
    const StaticClassFn* const cls[] = {&g_troops_dropship_class, &g_transport_dropship_class};
    static C23List list;
    for (const StaticClassFn* c : cls) {
        if (*c == nullptr) {
            continue;
        }
        c23_collect(world, nullptr, (*c)(), list);
        for (uint32_t i = 0; i < list.count; ++i) {
            void* const ds = list.e[i].actor;
            DsLiveRow* const r = c50_placed(ds) ? ds_row(ds, true) : nullptr;
            const DsLook l = ds_look(ds);
            if (r != nullptr && ds_host_sample(ds, *r, l, now)) {
                ds_live_send(world, ds, l, r->speed);
            }
        }
    }
}

// Client: a copy past its spline's end (below 0 or past the length) holds there until the next host frame.
void ds_client_hold() {
    if (g_spline_length == nullptr) {
        return;
    }
    for (int32_t i = 0; i < g_ds_snap.n; ++i) {
        DsLiveRow& r = g_ds_snap.rows[i];
        auto* const ds = static_cast<uint8_t*>(c31_weak_get(r.ds));
        const void* const spline = ds != nullptr ? read_ptr(ds, kDropshipSpline) : nullptr;
        if (spline == nullptr) {
            continue;
        }
        const float len = g_spline_length(spline);
        float dist = 0.0f;
        memcpy(&dist, ds + kDropshipSplineDistance, sizeof(dist));
        const float held = dist < 0.0f ? 0.0f : (dist > len ? len : dist);
        if (held == dist) {
            continue;
        }
        memcpy(ds + kDropshipSplineDistance, &held, sizeof(held));
        if (r.held != 0) {
            continue;
        }
        r.held = 1;
        ++g_ds_snap.holds;
        if (note_event_cap(g_ds_snap.live_lines, kDsLiveLines, "card159 dropship live")) {
            log::line("card159: client t=%llu dropship %s held at the spline end: distance %.0f -> %.0f (length %.0f)",
                      static_cast<unsigned long long>(GetTickCount64()), object_name(ds).text,
                      static_cast<double>(dist), static_cast<double>(held), static_cast<double>(len));
        }
    }
}

} // namespace

// Host: the check rounds that are due, then the tracker. Client: the spline-end hold.
void c159_dropship_step(void* engine) {
    if (g_client_session) {
        ds_client_hold();
        return;
    }
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG gap = g_ds_snap.step_ms != 0 ? now - g_ds_snap.step_ms : 0; // card 583: once per frame
    g_ds_snap.frame_ms = static_cast<uint32_t>(gap < 0xFFFFFFFFull ? gap : 0xFFFFFFFFull);
    g_ds_snap.step_ms = now;
    for (int32_t i = 0; i < kC17Slots; ++i) {
        void* const pc = c31_weak_get(g_ds_snap.check_pc[i]);
        if (pc == nullptr || now < g_ds_snap.check_due[i]) {
            continue;
        }
        if (c320_holding()) {
            // Both worlds are paused at the title screen and one stray client frame is not caught up by any L round
            // until the release (c344b68: 3 of 25 off, 102 uu of spline against tolerance 100 at speed 0): the check
            // follows the release by kDsCheckMs instead.
            g_ds_snap.check_due[i] = now + kDsCheckMs;
            continue;
        }
        g_ds_snap.check_pc[i] = C31Weak{};
        int32_t found = 0;
        const int32_t sent = ds_snapshot_send(pc, 'C', &found);
        log::line("%scard159: host dropship snapshot (check) to %s: %d of %d sent", sent == found ? "" : "WARN ",
                  object_name(pc).text, sent, found);
    }
    const void* const world = engine != nullptr ? g_get_game_world(engine) : nullptr;
    if (world != nullptr) {
        ds_host_track(world, now);
    }
}

// Both: the card 159 totals, at the next map's arm and at the quit command.
void c159_dropship_report(const char* when) {
    if (g_ds_snap.checks != 0 || g_ds_snap.applied_fields != 0 || g_ds_snap.live_got != 0) {
        log::line("%scard159: client dropship totals (%s): join check %u of %u match (%u fields applied at join); %u "
                  "live frames (%u fields applied), %u spline-end holds, %u NavDirection and %u spline writes, %u "
                  "splines not resolved, %u startup/target/ended writes",
                  g_ds_snap.mismatched + g_ds_snap.spline_refused != 0 ? "WARN " : "", when, g_ds_snap.matched,
                  g_ds_snap.checks, g_ds_snap.applied_fields, g_ds_snap.live_got, g_ds_snap.live_fields,
                  g_ds_snap.holds, g_ds_snap.navs, g_ds_snap.splines, g_ds_snap.spline_refused, g_ds_snap.speed_bytes);
    }
    if (g_ds_snap.live_frames != 0 || g_ds_snap.full != 0) {
        log::line("%scard159: host dropship tracker (%s): %u L frames, %u sends, %u rows full",
                  g_ds_snap.full != 0 ? "WARN " : "", when, g_ds_snap.live_frames, g_ds_snap.live_sends,
                  g_ds_snap.full);
    }
}

// Card 200: at LoadMap enter (ubw_load_map_enter): totals, then the journal resets before the new world's BeginPlay
// rows (c242-c3m1: 5 journalled rows inside LoadMap were wiped on the first tick).
void c132_dropship_map_end() {
    if (g_ds.changes != 0 || g_ds.received != 0) {
        const bool warn = g_ds.full + g_ds.unnamed + g_ds.missed + g_ds.bad != 0;
        log::line("%scard132: %s dropship map totals: %u state changes, %u sends, %d journalled, %u journal full, %u "
                  "unnamed; received %u, applied %u, already in state %u, no copy %u, bad %u",
                  warn ? "WARN " : "", side_text(), g_ds.changes, g_ds.sent, g_ds.n, g_ds.full, g_ds.unnamed,
                  g_ds.received, g_ds.applied, g_ds.same, g_ds.missed, g_ds.bad);
    }
    c159_dropship_report("map end");
    g_ds = DsJournal{};
    g_ds_snap = DsSnapState{};
}

// First tick after LoadMap: the settle window counts from the end of the load.
void c132_dropship_arm_map() {
    g_ds_snap.load_ms = GetTickCount64();
    g_ds_look = DsLookCache{}; // card 414: classes of the last map may be freed
}

int32_t c132_dropship_live() {
    return g_ds.n;
}

void detour_skip_flight(void* dropship, bool to_landing) {
    const auto* const d = static_cast<const uint8_t*>(dropship);
    const unsigned before = d[kDropshipState];
    g_orig_skip_flight(dropship, to_landing);
    log_dropship_call("SkipFlight", d, before, to_landing ? 1u : 0u, exe_rva(_ReturnAddress()));
}

void detour_begin_descent(void* dropship) {
    const auto* const d = static_cast<const uint8_t*>(dropship);
    const unsigned before = d[kDropshipState];
    g_orig_begin_descent(dropship);
    log_dropship_call("ExecuteBeginDescentBehaviour", d, before, 0, exe_rva(_ReturnAddress()));
}

void log_dropship_fields(const uint8_t* pc, const uint8_t* d, void* world, ULONGLONG ms) {
    float distance = 0.0f;
    double z = 0.0;
    double target_z = 0.0;
    memcpy(&distance, d + kDropshipSplineDistance, sizeof(distance));
    memcpy(&z, d + kDropshipDescentZ, sizeof(z));
    memcpy(&target_z, d + kDropshipDescentTargetZ, sizeof(target_z));
    const void* const root = read_ptr(d, kActorRootComponent);
    const Vec3 loc = root != nullptr ? read_vec3(root, kComponentTranslation) : Vec3{};
    const auto* const viewport = static_cast<const uint8_t*>(g_world_game_viewport(world));
    log::line("dropship-watch[%u] t=%llus %p %s: state %u, behaviour %u, spline %p, distance %.0f, nav %u startup %u "
              "target point %u ended %u, descent Z %.0f target %.0f, location (%.0f, %.0f, %.0f), BeginPlay flag %u, "
              "start-with-player %u, player-inside %u, manual control %u | bShowMouseCursor %u, MouseCaptureMode %u, "
              "IgnoreMove %u IgnoreLook %u | Tick %u (dt %.4f), TickBehaviours %u, TickSplineTravel %u | tick enabled %u, "
              "world paused %u, PrimaryActorTick+8 %02X %02X %02X %02X %02X %02X %02X %02X",
              g_dropship_watch.lines, ms / 1000, static_cast<const void*>(d), class_name(d).text, d[kDropshipState],
              d[kDropshipBehaviour], read_ptr(d, kDropshipSpline), static_cast<double>(distance), d[kDropshipNavDirection],
              d[kDropshipStartup], d[kDropshipTargetPoint], d[kDropshipSplineEnded], z, target_z, loc.x, loc.y, loc.z,
              d[kDropshipBeginPlayDone], d[kDropshipStartWithPlayer], d[kDropshipPlayerInside],
              d[kDropshipManualControl], cursor_bit(pc), viewport != nullptr ? viewport[kViewportCaptureMode] : 0xFFu,
              pc[kControllerIgnoreMove], pc[kControllerIgnoreLook], g_dropship_watch.ticks,
              static_cast<double>(g_dropship_watch.last_delta), g_dropship_watch.tick_behaviours,
              g_dropship_watch.spline_travels, g_actor_tick_enabled(d) ? 1u : 0u, g_world_is_paused(world) ? 1u : 0u,
              d[kActorPrimaryTick + 8], d[kActorPrimaryTick + 9], d[kActorPrimaryTick + 10], d[kActorPrimaryTick + 11],
              d[kActorPrimaryTick + 12], d[kActorPrimaryTick + 13], d[kActorPrimaryTick + 14], d[kActorPrimaryTick + 15]);
}

constexpr size_t kPauseRequestersHard = 0x270;
constexpr uint32_t kPauseLogs = 16;
uint32_t g_pause_logs = 0; // per map (reset in client_diag_load_map_enter)

RequestPauseFn g_orig_request_pause = nullptr;
RequestUnpauseFn g_orig_request_unpause = nullptr;

void log_pause_call(const char* what, const uint8_t* self, const void* requester, unsigned flag,
                    unsigned long long caller) {
    if (note_event_cap(g_pause_logs, kPauseLogs, "pause request")) {
        log::line("pause: %s %s(requester %p %s, flag %u) caller exe+0x%llX, requesters now %d (flagged %d)", side_text(),
                  what, requester, class_name(requester).text, flag, caller, array_num(self, kPauseRequesters),
                  array_num(self, kPauseRequestersHard));
    }
}

void detour_request_pause(void* self, void* requester, bool flag) {
    g_orig_request_pause(self, requester, flag);
    log_pause_call("RequestGamePause", static_cast<const uint8_t*>(self), requester, flag ? 1u : 0u,
                   exe_rva(_ReturnAddress()));
    cine_note_pause(requester); // card 130: always, play build too
    c376_note_pause(requester, true);
    if (g_c63_on) {
        c67_note_pause(requester, true);
    }
}

void detour_request_unpause(void* self, void* requester) {
    g_orig_request_unpause(self, requester);
    log_pause_call("RequestGameUnpause", static_cast<const uint8_t*>(self), requester, 0u, exe_rva(_ReturnAddress()));
    c135_note_unpause(requester); // card 135: always, play build too
    c376_note_pause(requester, false);
    if (g_c63_on) {
        c67_note_pause(requester, false);
    }
}

// Host/standalone only (armed per non-client LoadMap), every UGameEngine::Tick. Lines are counted
// only while the first PC's pawn is a transport dropship; the first other pawn after one ends it.
void dropship_watch_step(void* engine) {
    DropshipWatch& w = g_dropship_watch;
    const ULONGLONG now = GetTickCount64();
    if (now < w.next_ms) {
        return;
    }
    w.next_ms = now + (w.lines < kDropshipWatchFastLines ? kDropshipWatchFastMs : kDropshipWatchSlowMs);
    void* const world = g_get_game_world(engine);
    const auto* const pc = world != nullptr ? static_cast<const uint8_t*>(own_pc(world)) : nullptr;
    const auto* const pawn = pc != nullptr ? static_cast<const uint8_t*>(read_ptr(pc, kControllerPawn)) : nullptr;
    if (pawn == nullptr || !is_a(pawn, g_transport_dropship_class())) {
        if (w.seen) {
            log::line("dropship-watch: t=%llus PC pawn is now %p %s; watch ends", (now - w.start_ms) / 1000,
                      static_cast<const void*>(pawn), class_name(pawn).text);
            g_dropship_watch_armed = false;
        }
        return;
    }
    if (!w.seen) {
        log::line("dropship-watch: local pawn is transport dropship %p; UBWCOOP_AUTODROPSHIP %s",
                  static_cast<const void*>(pawn), g_auto_dropship ? "on" : "off");
    }
    w.seen = true;
    log_dropship_fields(pc, pawn, world, now - w.start_ms);
    ++w.lines;
    w.ticks = 0;
    w.tick_behaviours = 0;
    w.spline_travels = 0;
    if (w.lines >= kDropshipWatchLines) {
        log::line("dropship-watch: %u lines this map ... further lines not logged", w.lines);
        g_dropship_watch_armed = false;
    }
}

constexpr char kPauseStackClass[] = "WB_Stack_C";
constexpr uint32_t kAutoDropshipActions = 8;
// The flight starts only once the client's PlayerController exists on the host (both players
// ride), or after kAutoDropshipWaitMs with the host alone (logged).
constexpr int32_t kAutoDropshipPlayers = 2;
constexpr ULONGLONG kAutoDropshipWaitMs = 120000;
constexpr ULONGLONG kAutoDropshipFirstMs = 3000; // after both PCs exist (or the wait ran out)
constexpr ULONGLONG kAutoDropshipGapMs = 4000;

AutoDropship g_auto = {0, 0, 0};

// The first requester in USTGameInstance's pause array, or null (logged) when it is not WB_Stack_C.
void* auto_dropship_requester(uint8_t* iface) {
    const int32_t n = array_num(iface, kPauseRequesters);
    void* const* const data = n > 0 ? static_cast<void* const*>(read_ptr(iface, kPauseRequesters + kArrayData)) : nullptr;
    void* const requester = data != nullptr ? data[0] : nullptr;
    const ObjectName cls = class_name(requester);
    if (n == 0) { // the world unpaused itself before this step: nothing to do
        log::line("autodropship: pause requesters 0 (the world unpaused first) -- no action");
        return nullptr;
    }
    if (requester == nullptr || strcmp(cls.text, kPauseStackClass) != 0) {
        log::line("WARN autodropship: pause requester[0] of %d is %p %s, not %s -- no action", n, requester, cls.text,
                  kPauseStackClass);
        return nullptr;
    }
    return requester;
}

// dropship is null for an on-foot pawn.
void auto_dropship_unpause(void* world, const void* dropship) {
    auto* const gi = static_cast<uint8_t*>(read_ptr(static_cast<const uint8_t*>(world), kWorldOwningGameInstance));
    if (gi == nullptr) {
        log::line("ERROR autodropship: UWorld has no owning game instance -- disarmed");
        g_auto_dropship_armed = false;
        return;
    }
    uint8_t* const iface = gi + kGameInstancePauseIface;
    ++g_auto.actions;
    void* const requester = auto_dropship_requester(iface);
    if (requester == nullptr) {
        return;
    }
    const bool closed = c67_ui_close(world, requester);
    if (!closed) {
        g_orig_request_unpause(iface, requester);
    }
    const auto* const d = static_cast<const uint8_t*>(dropship);
    log::line("autodropship: action %u/%u: %s, WB_Stack_C %p; dropship %p state %d behaviour %d, requesters now %d, world paused after: %u",
              g_auto.actions, kAutoDropshipActions,
              closed ? "closed the stack through ProcessEvent (the card 067 ui continue steps)"
                     : "USTGameInstance::RequestGameUnpause as the closing screen would",
              requester, dropship, d != nullptr ? d[kDropshipState] : -1,
              d != nullptr ? d[kDropshipBehaviour] : -1, array_num(iface, kPauseRequesters),
              g_world_is_paused(world) ? 1u : 0u);
}

// True once the unpause countdown runs: both PlayerControllers exist, or the wait ran out.
bool auto_dropship_ready(void* world, const void* pawn, ULONGLONG now) {
    if (g_auto.wait_end_ms == 0) {
        g_auto.wait_end_ms = now + kAutoDropshipWaitMs;
        log::line("autodropship: world paused with dropship pawn %p; waiting for %d PlayerControllers (now %d, at "
                  "most %llu ms)",
                  pawn, kAutoDropshipPlayers, g_world_num_pcs(world), kAutoDropshipWaitMs);
    }
    if (g_auto.next_ms == 0) {
        const int32_t pcs = g_world_num_pcs(world);
        if (pcs < kAutoDropshipPlayers && now < g_auto.wait_end_ms) {
            return false;
        }
        g_auto.next_ms = now + kAutoDropshipFirstMs;
        log::line("%sautodropship: %d PlayerController(s); unpause action in %llu ms", pcs < kAutoDropshipPlayers ? "WARN " : "",
                  pcs, kAutoDropshipFirstMs);
        return false;
    }
    return true;
}

// UBWCOOP_SKIP_FLIGHT (card 090): once, on the host's own transport dropship, right after the unpause.
void auto_dropship_skip_flight(void* dropship) {
    const auto* const d = static_cast<const uint8_t*>(dropship);
    const unsigned before = d[kDropshipState];
    g_call_skip_flight(dropship, true);
    log::line("autodropship: SkipFlight(%p, to_landing 1): state %u -> %u", dropship, before, d[kDropshipState]);
}

// Every UGameEngine::Tick while armed (host/standalone, UBWCOOP_AUTODROPSHIP=1, once per map).
void auto_dropship_step(void* engine) {
    void* const world = g_get_game_world(engine);
    const auto* const pc = world != nullptr ? static_cast<const uint8_t*>(own_pc(world)) : nullptr;
    const void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    if (pawn == nullptr) {
        return;
    }
    const bool dropship = is_a(pawn, g_transport_dropship_class());
    const ULONGLONG now = GetTickCount64();
    if (!g_world_is_paused(world)) {
        // The game can unpause on its own once the wait began (before the first action): same finish, 0 actions.
        if (g_auto.actions > 0 || g_auto.wait_end_ms != 0) {
            log::line("autodropship: world unpaused after %u action(s); %s %p -- %s", g_auto.actions,
                      dropship ? "dropship" : "on-foot pawn", pawn,
                      dropship ? "it ticks from here, done" : "staying armed for later pauses");
            g_auto_dropship_armed = !dropship;
            g_auto = {0, 0, 0};
            if (dropship && g_skip_flight) {
                auto_dropship_skip_flight(const_cast<void*>(pawn));
            }
        }
        return;
    }
    if (!auto_dropship_ready(world, pawn, now)) {
        return;
    }
    if (now < g_auto.next_ms) {
        return;
    }
    // Card 320: after the host's continue the title-screen hold keeps the world paused until every client confirms.
    // That pause has no requester to close: an action here only burns the budget (c330cut run: 7 "no action" WARNs,
    // disarmed 32 s into the hold, the flight then ran unskipped). Wait for the hold; the unpaused branch finishes.
    if (g_auto.actions > 0 && c320_holding()) {
        return;
    }
    if (g_auto.actions >= kAutoDropshipActions) {
        log::line("WARN autodropship: world still paused after %u actions -- disarmed", g_auto.actions);
        g_auto_dropship_armed = false;
        return;
    }
    g_auto.next_ms = now + kAutoDropshipGapMs;
    auto_dropship_unpause(world, dropship ? pawn : nullptr);
}

// From the pause detours, after the game's own call; `requester` is live (it is the caller).
void c67_note_pause(void* requester, bool pause) {
    if (strcmp(class_name(requester).text, "WB_Stack_C") != 0) {
        return;
    }
    c67_dump(requester, pause ? "WB_Stack_C RequestGamePause" : "WB_Stack_C RequestGameUnpause");
    if (pause) {
        c67_ui_remember(requester);
    }
    g_c67.later_ms = pause ? GetTickCount64() + kC67LaterMs : 0;
}

// UBWCOOP_AUTODROPSHIP: the same steps, at once, on the live pause requester `stack` (a WB_Stack_C read from the
// game instance's array this tick). True when the stack no longer holds the pause; false leaves the caller's bare
// RequestGameUnpause to release it (an unknown top widget, e.g. a mid-mission screen; or a step that failed).
bool c67_ui_close(void* world, void* stack) {
    for (int32_t i = 0; i < kC67UiStepCount && g_find_function != nullptr && g_orig_process_event != nullptr; ++i) {
        int32_t num = 0;
        void* const top = c67_ui_top(stack, &num);
        const int32_t step = top != nullptr ? c67_ui_step_of(top) : kC67UiStepCount;
        if (step == kC67UiStepCount) {
            log::line("autodropship: stack %p top widget %s (%d in the stack): no continue step", stack,
                      class_name(top).text, num);
            break;
        }
        const char* const why = c67_ui_call(top, step);
        if (why != nullptr) {
            log::line("WARN autodropship: %s", why);
            break;
        }
    }
    return !c67_ui_held(world, stack);
}

// Card 218 diagnostic, both sides, from detour_c98_destroy before the original: a troop dropship's state as it is
// destroyed (no behaviour change). Attached actors: the distinct owners, other than the dropship, of the components
// attached to its RootComponent (reflected AttachChildren, bound kC218Children).
void c218_destroy_line(const void* actor, unsigned long long caller) {
    constexpr int32_t kC218Children = 32;
    if (actor == nullptr || g_troops_dropship_class == nullptr || !is_a(actor, g_troops_dropship_class())) {
        return;
    }
    const DsLook l = ds_look(actor);
    const void* const spline = read_ptr(actor, kDropshipSpline);
    const float length = spline != nullptr && g_spline_length != nullptr ? g_spline_length(spline) : 0.0f;
    const void* const root = read_ptr(actor, kActorRootComponent);
    const uint8_t* const prop =
        root != nullptr ? prop_find_prop(read_ptr(root, kObjectClass), "AttachChildren") : nullptr;
    const size_t off = prop != nullptr ? static_cast<size_t>(read_i32(prop, kPropertyOffset)) : 0;
    const auto* const kids = prop != nullptr ? static_cast<void* const*>(read_ptr(root, off)) : nullptr;
    const int32_t num = kids != nullptr ? read_i32(root, off + kArrayNum) : 0;
    const void* owners[kC218Children] = {};
    int32_t n = 0;
    for (int32_t i = 0; i < num && i < kC218Children; ++i) {
        const void* const owner = kids[i] != nullptr ? read_ptr(kids[i], kComponentOwner) : nullptr;
        bool seen = owner == nullptr || owner == actor;
        for (int32_t j = 0; j < n && !seen; ++j) {
            seen = owners[j] == owner;
        }
        if (!seen) {
            owners[n++] = owner;
        }
    }
    char held[16] = "";
    const DsLiveRow* const row = g_client_session ? ds_row(actor, false) : nullptr;
    if (row != nullptr) {
        snprintf(held, sizeof(held), ", held %u", static_cast<unsigned>(row->held != 0 ? 1 : 0));
    }
    log::line("card218: %s t=%llu dropship %s destroyed at (%.0f, %.0f, %.0f), spline %s distance %.0f of %.0f, target "
              "point %u, spline ended %u, %d attached actors%s%s, caller exe+0x%llX", side_text(),
              static_cast<unsigned long long>(GetTickCount64()), object_name(actor).text, l.at.x, l.at.y, l.at.z,
              spline != nullptr ? l.spline : "none", static_cast<double>(spline != nullptr ? l.dist : 0.0f),
              static_cast<double>(length), l.target, l.ended, n, prop != nullptr ? "" : " (no AttachChildren)", held,
              caller);
}

// --- Card 399: every player rides the host's transport dropship and lands beside the host ---
// EnterDropship and DescendToLandCompleted act on player 0 only (GetSTPlayerCharacter, GetPlayerController(world, 0)):
// on the server that is the host's pawn alone. The server boards every other player's pawn the way EnterDropship
// boards the host's (seat transform, attach, gravity, collision, hidden) and releases it beside the host's landed
// pawn. The attach and bHidden replicate; the client derives its own half (gravity, collision, input, camera) from
// the replicated attach. Possession does not move: every per-player system reads PC->Pawn.

C399SetTransformFn g_c399_set_transform = nullptr; // AActor::SetActorTransform
C399DetachFn g_c399_detach = nullptr;              // AActor::DetachFromActor
C399ViewBlendFn g_c399_view_blend = nullptr;       // APlayerController::SetViewTargetWithBlend
C399GetViewFn g_c399_get_view = nullptr;     // APlayerController::GetViewTarget
bool g_gate_c461_c539 = false; // set in read_card_flags: UBWCOOP_DIAG or ubwperf wants the row (cards 539/542)
C461MoveFn g_orig_c461_move = nullptr;       // UPrimitiveComponent::MoveComponentImpl (card 461, diagnostic)
bool g_c461_armed = false;                    // card 461: a release watch is open

namespace {

constexpr size_t kDropshipSeat = 0x760;          // EnterDropship: seat component (TeleportPlayer, AttachToComponent)
constexpr size_t kComponentToWorld = 0x1E0;      // USceneComponent::ComponentToWorld (FTransform of doubles)
constexpr size_t kCmcGravityScale = 0x1A0;       // float; EnterDropship saves it at +0x74C, then zeroes it
constexpr size_t kCmcClientErrorFlags = 0x555;   // NewProp SetBit 0x039D22D0: or byte [+0x555], 0x02
constexpr uint8_t kCmcIgnoreClientErrors = 0x02; // bIgnoreClientMovementErrorChecksAndCorrection
constexpr size_t kEnableInputSlot = 0x330;       // vtable: AActor::EnableInput(APlayerController*) (APawn overrides)
constexpr size_t kDisableInputSlot = 0x340;      // vtable: AActor::DisableInput(APlayerController*)
constexpr uintptr_t kAttachRulesRva = 0x09020A00; // EnterDropship's FAttachmentTransformRules: KeepWorld x3, no weld
constexpr uintptr_t kDetachRulesRva = 0x09020A0C; // DescendToLandCompleted's FDetachmentTransformRules: KeepWorld x3
constexpr int32_t kC399TeleportPhysics = 1;       // ETeleportType::TeleportPhysics
constexpr ULONGLONG kC399StepMs = 100;            // host step period; the release lands within it of the landing
constexpr uint32_t kC399Lines = 96;               // card399 rows per side per session (a few per player per map)
constexpr uint32_t kC399ViewLines = 8;            // client: view target re-holds logged per session
constexpr uint32_t kC399HudLines = 8;             // client: HUD re-hides logged per session
constexpr uint32_t kC399GapLines = 32;            // client: ride position rows per session
constexpr ULONGLONG kC399GapMs = 1000;            // client: ride position check period
constexpr double kC399GapStep = 500.0;            // client: a ride position row when the gap moved this far (uu)
constexpr size_t kPcMyHud = 0x358;                // APlayerController::MyHUD (death.cpp kPcMyHud)
constexpr uint8_t kC399VisCollapsed = 1;          // ESlateVisibility::Collapsed

struct C399Rider { // host: one pawn riding the host's dropship; a row exists while the weak ref is set
    C31Weak pawn;
    float gravity; // GravityScale before the ride
};

struct C399Client { // client: its own pawn while it rides; reset whole when the ride ends
    C31Weak pawn;
    float gravity;
    C31Weak hud;          // the HUD manager's SpecificHUD hidden for the ride
    uint8_t hud_vis;      // its visibility before the ride
    ULONGLONG gap_ms;     // next ride position check
    double gap_logged;    // the own pawn to dropship gap last logged (-1 = none yet)
};

C399Rider g_c399[kC43Max] = {};
C399Client g_c399c{};
ULONGLONG g_c399_next_ms = 0;
uint32_t g_c399_lines = 0;
uint32_t g_c399_view_holds = 0; // client: view target re-holds, per session (log cap)
uint32_t g_c399_hud_holds = 0;  // client: HUD re-hides, per session (log cap)
uint32_t g_c399_gap_lines = 0;  // client: ride position rows, per session (log cap)

bool c399_line() {
    return note_event_cap(g_c399_lines, kC399Lines, "card399");
}

const void* c399_rva(uintptr_t rva) {
    return g_exe_base + rva;
}

void c399_vcall(void* pawn, size_t slot, void* pc) {
    using InputFn = void (*)(void* actor, void* pc);
    const auto vtable = static_cast<InputFn*>(read_ptr(pawn, 0));
    vtable[slot / sizeof(void*)](pawn, pc);
}

// The pawn's CMC: GravityScale to `gravity`, velocity zeroed, and (host) the client error checks off while riding.
void c399_set_cmc(void* pawn, float gravity, bool riding, bool host) {
    auto* const cmc = static_cast<uint8_t*>(read_ptr(pawn, kCharacterMovement));
    if (cmc == nullptr) {
        return;
    }
    memcpy(cmc + kCmcGravityScale, &gravity, sizeof(gravity));
    const double zero[3] = {};
    memcpy(cmc + kMoveVelocity, zero, sizeof(zero));
    if (host) {
        cmc[kCmcClientErrorFlags] = static_cast<uint8_t>(riding ? cmc[kCmcClientErrorFlags] | kCmcIgnoreClientErrors
                                                                : cmc[kCmcClientErrorFlags] & ~kCmcIgnoreClientErrors);
    }
}

float c399_gravity(const void* pawn) {
    const auto* const cmc = static_cast<const uint8_t*>(read_ptr(pawn, kCharacterMovement));
    float g = 0.0f;
    if (cmc != nullptr) {
        memcpy(&g, cmc + kCmcGravityScale, sizeof(g));
    }
    return g;
}

// Host: the transport dropship PC 0 possesses with the player-inside flag set (the host rides it), else null.
const uint8_t* c399_host_ride(const C43Roster& r) {
    const auto* const d = static_cast<const uint8_t*>(r.p[0].pawn);
    return d != nullptr && is_a(d, g_transport_dropship_class()) && d[kDropshipPlayerInside] != 0 ? d : nullptr;
}

int32_t c399_row(const void* pawn) {
    for (int32_t i = 0; i < kC43Max; ++i) {
        if (g_c399[i].pawn.serial != 0 && c31_weak_get(g_c399[i].pawn) == pawn) {
            return i;
        }
    }
    return -1;
}

int32_t c399_free_row() {
    for (int32_t i = 0; i < kC43Max; ++i) {
        if (g_c399[i].pawn.serial == 0) {
            return i;
        }
    }
    return -1;
}

void c399_board(void* pawn, const uint8_t* dropship, int32_t slot, ULONGLONG now) {
    void* const seat = read_ptr(dropship, kDropshipSeat);
    const int32_t row = c399_free_row();
    if (seat == nullptr || row < 0 || g_c399_set_transform == nullptr || g_orig_c98_attach == nullptr ||
        g_orig_c98_hidden == nullptr) {
        if (c399_line()) {
            log::line("WARN card399: host t=%llu %s (slot %d) not boarded on %s: seat %p, free row %d, calls %d",
                      static_cast<unsigned long long>(now), object_name(pawn).text, slot, object_name(dropship).text,
                      seat, row, g_c399_set_transform != nullptr && g_orig_c98_attach != nullptr);
        }
        return;
    }
    const Vec3 from = actor_location(pawn);
    g_c399_set_transform(pawn, static_cast<const uint8_t*>(seat) + kComponentToWorld, false, nullptr,
                         kC399TeleportPhysics);
    const bool attached = g_orig_c98_attach(pawn, seat, c399_rva(kAttachRulesRva), 0); // NAME_None socket
    c31_weak_set(g_c399[row].pawn, pawn);
    g_c399[row].gravity = c399_gravity(pawn);
    c399_set_cmc(pawn, 0.0f, true, true);
    g_orig_set_actor_collision(pawn, false);
    g_orig_c98_hidden(pawn, true);
    const Vec3 at = actor_location(pawn);
    if (c399_line()) {
        log::line("%scard399: host t=%llu board %s (slot %d) into %s seat: from (%.0f, %.0f, %.0f) to (%.0f, %.0f, "
                  "%.0f), attached %d, GravityScale %.2f saved, hold %d",
                  attached ? "" : "WARN ", static_cast<unsigned long long>(now), object_name(pawn).text, slot,
                  object_name(dropship).text, from.x, from.y, from.z, at.x, at.y, at.z, attached ? 1 : 0,
                  static_cast<double>(g_c399[row].gravity), c320_holding() ? 1 : 0);
    }
}

// --- Card 461: who moves a released rider (diagnostic) ---
// C3M2 runs: the client's pawn, warped beside the host at its release, is found 5000 uu away one frame later with zero
// velocity, after a Fall hit. b165 ruled out the movement base (ground, velocity 0). For kC461WatchMs after each release,
// host and client: one frame row per frame (location, velocity, mode, base), one row per move of the pawn's root of more
// than kC461MoveUu in one UPrimitiveComponent::MoveComponentImpl call with its caller stack (detour_c461_move), a row
// when the pawn jumps between frame rows with no move row, and the Fall damage rows (c461_fall_rows).
constexpr ULONGLONG kC461WatchMs = 600;  // frames watched after a release
constexpr uint32_t kC461WatchRows = 40;  // frame rows per release
constexpr uint32_t kC461MoveRows = 24;   // move rows per release
constexpr double kC461MoveUu = 60.0;     // a smaller move in one call is not logged
constexpr uint32_t kC461MoveFrames = 10; // caller frames per move row
constexpr size_t kC461BaseChars = 160;   // "<component> of <owner>"
constexpr size_t kC461StackChars = 256;  // 10 frames of " < 0x..."
constexpr size_t kC461FieldChars = 720;  // the reflected Fall/Air/Apex fields of one object
constexpr uint32_t kC461ConfirmRows = 8; // card 461 b167: base confirm rows per release
constexpr ULONGLONG kC461GapRowMs = 500; // b168: the frame row printed at this age (with the +0 row and the first row
                                         // after the restore); the other frames print only their WARN
constexpr double kC461SpeedLow = 100.0;  // a watched pawn's CMC speed under this before a move ...
constexpr double kC461SpeedHigh = 3000.0; // ... and over this after it prints the confirm rows
constexpr int kC461Walking = 1;          // EMovementMode MOVE_Walking
constexpr int kC461Falling = 3;          // EMovementMode MOVE_Falling
constexpr uint8_t kC461FloorWalkable = 2; // kCmcCurrentFloorFlags bit: bWalkableFloor
constexpr int32_t kC461ImpartBits = 4;   // X, Y, Z are cleared and restored; AngularVelocity is logged only
constexpr uint8_t kC461ImpartXyz = 0x7;  // bits 0-2 of a C461 impart mask: bImpartBaseVelocityX / Y / Z
const char* const kC461ImpartNames[kC461ImpartBits] = {"bImpartBaseVelocityX", "bImpartBaseVelocityY",
                                                       "bImpartBaseVelocityZ", "bImpartBaseAngularVelocity"};

struct C461Watch { // one released pawn under watch, by roster slot (the client watches its own pawn at slot 0)
    C31Weak pawn;
    const void* root; // the pawn's root component at the release: compared, never read, by the move hook
    ULONGLONG start;
    Vec3 last;        // location at the last frame row
    uint32_t rows;
    uint32_t moves;   // move rows of this release
    uint32_t moved;   // move rows since the last frame row
    uint8_t impart;   // the impart mask saved at the release (bits 0-3 as kC461ImpartNames)
    bool cleared;     // bImpartBaseVelocityX / Y / Z are cleared and not yet restored
    bool restore_row; // b168: the next frame row prints (the first after the restore)
    bool gap_row;     // b168: the +kC461GapRowMs frame row has printed
    bool confirmed;   // the move hook printed its confirm rows
    uint32_t confirms; // confirm rows of this release (cap kC461ConfirmRows)
    double speed;     // CMC speed after the last watched move
    char base[kC461BaseChars]; // the movement base before the last watched move that was not Falling
    Vec3 base_owner_vel; // that base owner's velocity (CMC Velocity of a character, else its root ComponentVelocity)
    Vec3 base_comp_vel;  // that base component's ComponentVelocity
};
C461Watch g_c461[kC43Max];
C31Weak g_c461_host; // host: the host's own pawn at the last release (confirm rows); unset on the client

struct C461ImpartLayout { // the CMC class's impart bits, found by reflection (byte -1 = not found)
    const void* cls;
    DsBit bit[kC461ImpartBits];
};
C461ImpartLayout g_c461_impart{}; // game thread only

// The pawn's movement base (APawn::GetMovementBase through the vtable; ACharacter overrides it), or null.
void* c461_base(const void* pawn) {
    const auto fn = reinterpret_cast<C434BaseFn>(read_ptr(read_ptr(pawn, 0), kPawnVtGetMovementBase));
    return fn(pawn);
}

// A component's reflected USceneComponent::ComponentVelocity, zero when there is no component or field.
Vec3 c461_component_velocity(const void* comp) {
    Vec3 v{};
    const uint8_t* const f =
        comp != nullptr ? prop_find_prop(read_ptr(comp, kObjectClass), "ComponentVelocity") : nullptr;
    if (f != nullptr) {
        memcpy(&v, static_cast<const uint8_t*>(comp) + read_i32(f, kPropertyOffset), sizeof(v));
    }
    return v;
}

// One row at the release: the base, its velocity and its owner root's velocity (the engine's fallback when the
// component's own velocity is zero).
void c461_release_row(const void* pawn, const char* side, ULONGLONG now) {
    const void* const base = c461_base(pawn);
    const void* const owner = base != nullptr ? read_ptr(base, kComponentOwner) : nullptr;
    const Vec3 v = c461_component_velocity(base);
    const Vec3 rv = c461_component_velocity(owner != nullptr ? read_ptr(owner, kActorRootComponent) : nullptr);
    char text[kC461BaseChars];
    c461_base_text(pawn, text, sizeof(text));
    log::line("card399: %s t=%llu release base %s, base velocity (%.0f, %.0f, %.0f), owner root velocity (%.0f, %.0f, "
              "%.0f), pawn %s", side, static_cast<unsigned long long>(now), text, v.x, v.y, v.z, rv.x, rv.y, rv.z,
              object_name(pawn).text);
}

int c461_mode(const void* pawn) {
    const auto* const cmc = static_cast<const uint8_t*>(read_ptr(pawn, kCharacterMovement));
    return cmc != nullptr ? static_cast<int>(cmc[kCmcMovementMode]) : -1;
}

Vec3 c461_velocity(const void* pawn) {
    const auto* const cmc = static_cast<const uint8_t*>(read_ptr(pawn, kCharacterMovement));
    Vec3 vel{};
    if (cmc != nullptr) {
        memcpy(&vel, cmc + kMoveVelocity, sizeof(vel));
    }
    return vel;
}

double c461_dist(const Vec3& a, const Vec3& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return sqrt(dx * dx + dy * dy + dz * dz);
}

// --- Card 461 b167: a released or warped pawn must not inherit the velocity of what it is dropped onto ---
// UCharacterMovementComponent::OnMovementModeChanged adds GetImpartedMovementBaseVelocity() when the mode becomes
// Falling. b166: the client's pawn, warped 100 uu above the host's pawn, went Walking -> Falling with a velocity of
// seat -> warp spot over one tick (49182, -12236, 0) and walked 5 km in one frame. The release clears
// bImpartBaseVelocityX / Y / Z on the pawn's CMC (host: the rider's server copy; client: its own pawn, so its
// prediction matches) and restores the saved bits once the pawn walks on a walkable floor not owned by a player
// character, or at the end of the watch window.

// The CMC class's impart bits, or null when reflection does not find all four (no offset is guessed).
const C461ImpartLayout* c461_impart_layout(const void* cmc) {
    const void* const cls = read_ptr(cmc, kObjectClass);
    if (cls != g_c461_impart.cls) {
        g_c461_impart.cls = cls;
        for (int32_t i = 0; i < kC461ImpartBits; ++i) {
            const char* const names[2] = {kC461ImpartNames[i], nullptr};
            g_c461_impart.bit[i] = ds_bit(cls, names);
        }
    }
    for (int32_t i = 0; i < kC461ImpartBits; ++i) {
        if (g_c461_impart.bit[i].byte < 0) {
            return nullptr;
        }
    }
    return &g_c461_impart;
}

// The CMC's impart mask (bit i set = kC461ImpartNames[i] true), or -1 when there is no CMC or no layout.
int32_t c461_impart_read(const void* cmc) {
    const C461ImpartLayout* const l = cmc != nullptr ? c461_impart_layout(cmc) : nullptr;
    if (l == nullptr) {
        return -1;
    }
    int32_t bits = 0;
    for (int32_t i = 0; i < kC461ImpartBits; ++i) {
        if ((static_cast<const uint8_t*>(cmc)[l->bit[i].byte] & l->bit[i].mask) != 0) {
            bits |= 1 << i;
        }
    }
    return bits;
}

// Writes bits 0-2 of `bits` to bImpartBaseVelocityX / Y / Z; the caller checked the layout with c461_impart_read.
void c461_impart_write(void* cmc, uint8_t bits) {
    auto* const p = static_cast<uint8_t*>(cmc);
    for (int32_t i = 0; i < 3; ++i) {
        const DsBit b = g_c461_impart.bit[i];
        p[b.byte] = static_cast<uint8_t>((bits & (1 << i)) != 0 ? p[b.byte] | b.mask : p[b.byte] & ~b.mask);
    }
}

void c461_bits_text(int32_t bits, char* buf, size_t cap) {
    if (bits < 0) {
        snprintf(buf, cap, "not found");
        return;
    }
    snprintf(buf, cap, "X %d Y %d Z %d Angular %d", bits & 1, (bits >> 1) & 1, (bits >> 2) & 1, (bits >> 3) & 1);
}

void c461_impart_restore(C461Watch& w, void* pawn, int32_t slot, ULONGLONG now, const char* why);

// At a release, before the warp: clears X / Y / Z and returns the saved mask, or -1 (WARN, nothing cleared). A watch
// of `slot` still holding cleared bits (a second release inside its window) restores them first, so the mask saved
// here is the pawn's own.
int32_t c461_impart_clear(void* pawn, int32_t slot, ULONGLONG now) {
    if (slot >= 0 && slot < kC43Max && g_c461[slot].cleared) {
        c461_impart_restore(g_c461[slot], c31_weak_get(g_c461[slot].pawn), slot, now, "a second release");
    }
    void* const cmc = read_ptr(pawn, kCharacterMovement);
    const int32_t saved = c461_impart_read(cmc);
    if (saved < 0) {
        log::line("WARN card461: %s t=%llu impart clear %s: bImpartBaseVelocityX / Y / Z / AngularVelocity not all "
                  "found by reflection on %s, nothing cleared", side(), static_cast<unsigned long long>(now),
                  object_name(pawn).text, cmc != nullptr ? class_name(cmc).text : "no CharacterMovement");
        return -1;
    }
    c461_impart_write(cmc, static_cast<uint8_t>(saved & ~kC461ImpartXyz));
    char was[48];
    char is[48];
    c461_bits_text(saved, was, sizeof(was));
    c461_bits_text(c461_impart_read(cmc), is, sizeof(is));
    log::line("card461: %s t=%llu impart clear %s: saved %s, now %s", side(), static_cast<unsigned long long>(now),
              object_name(pawn).text, was, is);
    return saved;
}

// Puts the saved X / Y / Z back once; `pawn` may be gone (then nothing is written).
void c461_impart_restore(C461Watch& w, void* pawn, int32_t slot, ULONGLONG now, const char* why) {
    w.cleared = false;
    void* const cmc = pawn != nullptr ? read_ptr(pawn, kCharacterMovement) : nullptr;
    char was[48];
    c461_bits_text(w.impart, was, sizeof(was));
    if (c461_impart_read(cmc) < 0) {
        log::line("WARN card461: %s t=%llu impart restore slot %d (%s): %s, saved %s not written", side(),
                  static_cast<unsigned long long>(now), slot, why, pawn != nullptr ? "no impart layout" : "pawn gone",
                  was);
        return;
    }
    c461_impart_write(cmc, w.impart);
    w.restore_row = true;
    char is[48];
    c461_bits_text(c461_impart_read(cmc), is, sizeof(is));
    log::line("card461: %s t=%llu impart restore %s (slot %d), %s, +%llu ms: saved %s, now %s", side(),
              static_cast<unsigned long long>(now), object_name(pawn).text, slot, why,
              static_cast<unsigned long long>(now - w.start), was, is);
}

// Restore point: Walking on a walkable floor whose base is not owned by a player character (b166's suspect base).
bool c461_landed(const void* pawn) {
    const auto* const cmc = static_cast<const uint8_t*>(read_ptr(pawn, kCharacterMovement));
    if (cmc == nullptr || cmc[kCmcMovementMode] != kC461Walking || (cmc[kCmcCurrentFloorFlags] & kC461FloorWalkable) == 0) {
        return false;
    }
    const void* const base = c461_base(pawn);
    const void* const owner = base != nullptr ? read_ptr(base, kComponentOwner) : nullptr;
    return owner == nullptr || !is_a(owner, g_player_character_class());
}

// An actor's velocity as AActor::GetVelocity gives it: a character's CMC Velocity, else its root ComponentVelocity.
Vec3 c461_owner_velocity(const void* owner) {
    if (owner == nullptr) {
        return Vec3{};
    }
    if (is_a(owner, g_player_character_class())) {
        return c461_velocity(owner);
    }
    return c461_component_velocity(read_ptr(owner, kActorRootComponent));
}

// Move hook, before a watched move that is not Falling: the base it stands on, for the confirm rows.
void c461_note_base(C461Watch& w, const void* pawn) {
    const void* const base = c461_base(pawn);
    const void* const owner = base != nullptr ? read_ptr(base, kComponentOwner) : nullptr;
    c461_base_text(pawn, w.base, sizeof(w.base));
    w.base_owner_vel = c461_owner_velocity(owner);
    w.base_comp_vel = c461_component_velocity(base);
}

// Host: the host's own pawn (location, CMC velocity, mode), at the release and with the confirm rows.
void c461_host_row(const void* host, const char* when, ULONGLONG now) {
    if (host == nullptr) {
        return;
    }
    const Vec3 at = actor_location(host);
    const Vec3 v = c461_velocity(host);
    log::line("card461: host t=%llu host pawn %s %s: at (%.0f, %.0f, %.0f), velocity (%.0f, %.0f, %.0f), mode %d",
              static_cast<unsigned long long>(now), object_name(host).text, when, at.x, at.y, at.z, v.x, v.y, v.z,
              c461_mode(host));
}

// The confirm rows (cap kC461ConfirmRows per release): the base before the last non-Falling move, its owner's and its
// own velocity, the base now, the impart bits, and on the host the host pawn's row.
void c461_confirm_rows(C461Watch& w, const void* pawn, int32_t slot, ULONGLONG now, const char* why) {
    if (w.confirms >= kC461ConfirmRows) {
        return;
    }
    w.confirms += 2;
    char base_now[kC461BaseChars];
    c461_base_text(pawn, base_now, sizeof(base_now));
    char bits[48];
    c461_bits_text(c461_impart_read(read_ptr(pawn, kCharacterMovement)), bits, sizeof(bits));
    const Vec3 v = c461_velocity(pawn);
    log::line("card461: %s t=%llu confirm %s (slot %d) at %s, +%llu ms: velocity (%.0f, %.0f, %.0f), speed before "
              "%.0f, mode %d; base before the last non-Falling move %s, owner velocity (%.0f, %.0f, %.0f), base "
              "ComponentVelocity (%.0f, %.0f, %.0f); base now %s; impart %s", side(),
              static_cast<unsigned long long>(now), object_name(pawn).text, slot, why,
              static_cast<unsigned long long>(now - w.start), v.x, v.y, v.z, w.speed, c461_mode(pawn),
              w.base[0] != '\0' ? w.base : "not seen", w.base_owner_vel.x, w.base_owner_vel.y, w.base_owner_vel.z,
              w.base_comp_vel.x, w.base_comp_vel.y, w.base_comp_vel.z, base_now, bits);
    c461_host_row(g_c461_host.serial != 0 ? c31_weak_get(g_c461_host) : nullptr, why, now);
}

void c461_frame_print(const C461Watch& w, const void* pawn, int32_t slot, ULONGLONG now, const Vec3& at);

// b168: three frame rows per watch print (the +0 row, the first after the restore, the first at +kC461GapRowMs);
// every frame still checks the jump below.
void c461_frame_row(C461Watch& w, const void* pawn, int32_t slot, ULONGLONG now) {
    const Vec3 at = actor_location(pawn);
    const bool gap = !w.gap_row && now - w.start >= kC461GapRowMs;
    if (w.rows <= 1 || w.restore_row || gap) {
        w.restore_row = false;
        w.gap_row = w.gap_row || gap;
        c461_frame_print(w, pawn, slot, now, at);
    }
    const double jump = c461_dist(at, w.last);
    if (w.rows > 1 && w.moved == 0 && jump > kC461MoveUu) {
        log::line("WARN card461: %s t=%llu %s (slot %d) moved %.0f uu since the last frame, (%.0f, %.0f, %.0f) -> "
                  "(%.0f, %.0f, %.0f), with no move row: not through MoveComponentImpl", side(),
                  static_cast<unsigned long long>(now), object_name(pawn).text, slot, jump, w.last.x, w.last.y,
                  w.last.z, at.x, at.y, at.z);
    }
    w.last = at;
    w.moved = 0;
}

void c461_frame_print(const C461Watch& w, const void* pawn, int32_t slot, ULONGLONG now, const Vec3& at) {
    const Vec3 vel = c461_velocity(pawn);
    char text[kC461BaseChars];
    c461_base_text(pawn, text, sizeof(text));
    log::line("card461: %s t=%llu release +%llu ms %s (slot %d): at (%.0f, %.0f, %.0f), velocity (%.0f, %.0f, %.0f), "
              "mode %d, base %s", side(), static_cast<unsigned long long>(now),
              static_cast<unsigned long long>(now - w.start), object_name(pawn).text, slot, at.x, at.y, at.z, vel.x,
              vel.y, vel.z, c461_mode(pawn), text);
}

// At a release (host: the rider's server copy; client: its own pawn at slot 0): the watch starts with the release
// frame's row and arms the move hook.
// `impart` is c461_impart_clear's saved mask (-1: nothing cleared, nothing to restore).
void c461_watch(void* pawn, int32_t slot, ULONGLONG now, int32_t impart) {
    if (slot < 0 || slot >= kC43Max) {
        log::line("WARN card461: %s release watch not started: slot %d out of range", side(), slot);
        return;
    }
    C461Watch& w = g_c461[slot];
    w = C461Watch{};
    c31_weak_set(w.pawn, pawn);
    w.root = read_ptr(pawn, kActorRootComponent);
    w.start = now;
    w.rows = 1;
    w.impart = static_cast<uint8_t>(impart >= 0 ? impart : 0);
    w.cleared = impart >= 0;
    w.speed = c461_dist(c461_velocity(pawn), Vec3{});
    c461_frame_row(w, pawn, slot, now);
    g_c461_armed = true;
}

// Every frame (host: c399_step; client: c399_client_step): one row per watched pawn until the window or the row cap
// ends its watch; the move hook disarms when no watch is left.
void c461_step(ULONGLONG now) {
    if (!g_c461_armed) {
        return;
    }
    bool open = false;
    for (int32_t s = 0; s < kC43Max; ++s) {
        C461Watch& w = g_c461[s];
        if (w.pawn.serial == 0) {
            continue;
        }
        void* const pawn = c31_weak_get(w.pawn);
        if (pawn == nullptr || now - w.start > kC461WatchMs || w.rows >= kC461WatchRows) {
            if (w.cleared) {
                c461_impart_restore(w, pawn, s, now, "window end");
            }
            w = C461Watch{};
            continue;
        }
        if (w.cleared && c461_landed(pawn)) {
            c461_impart_restore(w, pawn, s, now, "walking on a walkable floor");
        }
        ++w.rows;
        c461_frame_row(w, pawn, s, now);
        open = true;
    }
    g_c461_armed = open;
}

// The watch slot whose pawn root is `comp`, or -1.
int32_t c461_slot_of_root(const void* comp) {
    for (int32_t s = 0; s < kC43Max; ++s) {
        if (g_c461[s].root == comp && g_c461[s].pawn.serial != 0) {
            return s;
        }
    }
    return -1;
}

// "Name=value" of the reflected leaf fields of `object` (its class and supers) whose name holds Fall, Air or Apex.
void c461_fields(const void* object, char* buf, size_t cap) {
    size_t used = 0;
    buf[0] = '\0';
    const void* cls = object != nullptr ? read_ptr(object, kObjectClass) : nullptr;
    for (int d = 0; cls != nullptr && d < kPropMaxSupers; ++d, cls = read_ptr(cls, kStructSuper)) {
        const void* f = read_ptr(cls, kStructChildProperties);
        for (int n = 0; f != nullptr && n < kPropMaxFields && used + 1 < cap; ++n, f = read_ptr(f, kFieldNext)) {
            const ObjectName name = fname_text(static_cast<const uint8_t*>(f) + kFieldName);
            if (strstr(name.text, "Fall") == nullptr && strstr(name.text, "Air") == nullptr &&
                strstr(name.text, "Apex") == nullptr) {
                continue;
            }
            ObjectName kind;
            const PropKind k = prop_kind(f, kind);
            const uint8_t* const at = static_cast<const uint8_t*>(object) + read_i32(f, kPropertyOffset);
            const int32_t size = read_i32(f, kPropertyElementSize);
            char value[48];
            if (k == PropKind::Float) {
                snprintf(value, sizeof(value), "%.3f", static_cast<double>(read_f32(at, 0)));
            } else if (k == PropKind::Double) {
                double v = 0.0;
                memcpy(&v, at, sizeof(v));
                snprintf(value, sizeof(value), "%.3f", v);
            } else if (k == PropKind::Bool && prop_bool_ok(static_cast<const uint8_t*>(f))) {
                const auto* const fb = static_cast<const uint8_t*>(f);
                snprintf(value, sizeof(value), "%d", (at[fb[kBoolByteOffset]] & fb[kBoolFieldMask]) != 0 ? 1 : 0);
            } else if ((k == PropKind::SInt || k == PropKind::UInt) && (size == 1 || size == 4)) {
                snprintf(value, sizeof(value), "%d", size == 1 ? static_cast<int>(at[0]) : read_i32(at, 0));
            } else {
                snprintf(value, sizeof(value), "(%s)", kind.text);
            }
            const int w = snprintf(buf + used, cap - used, "%s%s=%s", used == 0 ? "" : " ", name.text, value);
            if (w < 0) {
                return;
            }
            used += static_cast<size_t>(w) < cap - used ? static_cast<size_t>(w) : cap - used - 1;
        }
    }
}

void c399_release(int32_t row, void* pawn, const C43Roster& r, int32_t slot, ULONGLONG now) {
    const float gravity = g_c399[row].gravity;
    g_c399[row] = C399Rider{};
    c461_release_row(pawn, "host", now);
    if (g_c399_detach != nullptr) {
        g_c399_detach(pawn, c399_rva(kDetachRulesRva));
    }
    c399_set_cmc(pawn, gravity, false, true);
    g_orig_set_actor_collision(pawn, true);
    const bool unconfirmed = c400_shielded(pawn); // card 400: its client is not in yet; card 400 shows it at the confirm
    if (!unconfirmed) {
        g_orig_c98_hidden(pawn, false);
    }
    void* const host = r.p[0].pawn;
    const bool beside = host != nullptr && is_a(host, g_player_character_class());
    if (c399_line()) {
        const void* const root = beside ? read_ptr(host, kActorRootComponent) : nullptr;
        log::line("card399: host t=%llu release %s (slot %d): detached %d, GravityScale %.2f, host %s yaw %.1f: %s",
                  static_cast<unsigned long long>(now), object_name(pawn).text, slot,
                  g_get_attach_parent_actor(pawn) == nullptr ? 1 : 0, static_cast<double>(gravity),
                  object_name(host).text, root != nullptr ? component_yaw(root) : 0.0,
                  beside ? "warp beside the host" : "no host pawn on foot, left at the seat");
        if (unconfirmed) {
            log::line("card400: host t=%llu released rider %s (slot %d) stays hidden: client not confirmed",
                      static_cast<unsigned long long>(now), object_name(pawn).text, slot);
        }
    }
    c31_weak_set(g_c461_host, beside ? host : nullptr);
    c461_host_row(beside ? host : nullptr, "at the release", now);
    if (beside) {
        c46_warp(pawn, host, r, slot, now); // card 461: c46_warp clears the impart bits and starts the watch
    }
}

// Host: rows whose pawn is gone (the client left, or its pawn was destroyed) are dropped.
void c399_prune(ULONGLONG now) {
    for (int32_t i = 0; i < kC43Max; ++i) {
        if (g_c399[i].pawn.serial != 0 && c31_weak_get(g_c399[i].pawn) == nullptr) {
            g_c399[i] = C399Rider{};
            if (c399_line()) {
                log::line("card399: host t=%llu rider row %d dropped: pawn gone", static_cast<unsigned long long>(now), i);
            }
        }
    }
}

// Client: the HUD manager's pawn-specific widget (BP_HUD_Manager "SpecificHUD", made from its Character Specifc
// Widget Map on OnPossessedPawnChanged): on foot the trooper HUD, which holds the crosshair, objectives, minimap,
// health and ammo. The host possesses the dropship for the ride, so its SpecificHUD becomes WB_PlayerDropship_HUD (an
// empty overlay); the client keeps its pawn, so its trooper HUD is hidden for the ride instead. Null when none yet.
void* c399_specific_hud(void* pc) {
    const void* const hud = read_ptr(pc, kPcMyHud);
    const uint8_t* prop = nullptr;
    const void* const widget = hud != nullptr ? prop_object_field(hud, "SpecificHUD", &prop) : nullptr;
    return const_cast<void*>(widget);
}

// Client, while riding: the SpecificHUD stays collapsed (a widget made after the ride on is hidden when it appears).
void c399_client_hold_hud(void* pc) {
    void* const widget = c399_specific_hud(pc);
    if (widget == nullptr || g_widget_set_visibility == nullptr) {
        return;
    }
    const uint8_t vis = static_cast<const uint8_t*>(widget)[kC67WidgetVisibility];
    if (vis == kC399VisCollapsed) {
        return;
    }
    const bool fresh = c31_weak_get(g_c399c.hud) != widget;
    if (fresh) {
        c31_weak_set(g_c399c.hud, widget);
        g_c399c.hud_vis = vis;
    }
    g_widget_set_visibility(widget, kC399VisCollapsed);
    if (fresh ? c399_line() : note_event_cap(g_c399_hud_holds, kC399HudLines, "card399 HUD hold")) {
        log::line("card399: client t=%llu ride HUD %s %s hidden (visibility %u %s)",
                  static_cast<unsigned long long>(GetTickCount64()), class_name(widget).text, object_name(widget).text,
                  static_cast<unsigned>(vis), fresh ? "saved" : "again, the game showed it");
    }
}

// Client: the ride state ends; the hidden SpecificHUD gets its visibility back.
void c399_client_end() {
    void* const widget = g_c399c.hud.serial != 0 ? c31_weak_get(g_c399c.hud) : nullptr;
    const uint8_t vis = g_c399c.hud_vis;
    g_c399c = C399Client{};
    if (widget != nullptr && g_widget_set_visibility != nullptr) {
        g_widget_set_visibility(widget, vis);
        if (c399_line()) {
            log::line("card399: client t=%llu ride HUD %s shown (visibility %u)",
                      static_cast<unsigned long long>(GetTickCount64()), class_name(widget).text,
                      static_cast<unsigned>(vis));
        }
    }
}

// Client, while riding, once a second: the own pawn's distance from the dropship, a row when it moved 500 uu since
// the last row (the server copy sits at the seat; on FED_C3M2 run c399b86a the own pawn was 5400 uu off at landing).
void c399_client_gap(const uint8_t* pawn, const void* dropship, ULONGLONG now) {
    if (now < g_c399c.gap_ms) {
        return;
    }
    g_c399c.gap_ms = now + kC399GapMs;
    const Vec3 a = actor_location(pawn);
    const Vec3 b = actor_location(dropship);
    const double gap = sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
    if (g_c399c.gap_logged >= 0.0 && fabs(gap - g_c399c.gap_logged) < kC399GapStep) {
        return;
    }
    g_c399c.gap_logged = gap;
    if (note_event_cap(g_c399_gap_lines, kC399GapLines, "card399 ride position")) {
        log::line("card399: client t=%llu ride position: own pawn (%.0f, %.0f, %.0f), dropship (%.0f, %.0f, %.0f), "
                  "gap %.0f", static_cast<unsigned long long>(now), a.x, a.y, a.z, b.x, b.y, b.z, gap);
    }
}

void c399_client_on(uint8_t* pawn, void* pc, void* dropship) {
    c31_weak_set(g_c399c.pawn, pawn);
    g_c399c.gravity = c399_gravity(pawn);
    c399_set_cmc(pawn, 0.0f, true, false);
    g_orig_set_actor_collision(pawn, false);
    c399_vcall(pawn, kDisableInputSlot, pc);
    if (g_c399_view_blend != nullptr) {
        g_c399_view_blend(pc, dropship, 0.0f, 0, 0.0f, false);
    }
    g_c399c.gap_logged = -1.0;
    void* const hud = c399_specific_hud(pc);
    if (c399_line()) {
        log::line("card399: client t=%llu ride on: own pawn %s attached to %s, GravityScale %.2f saved, input off, "
                  "view target %s, SpecificHUD %s", static_cast<unsigned long long>(GetTickCount64()),
                  object_name(pawn).text, object_name(dropship).text, static_cast<double>(g_c399c.gravity),
                  g_c399_get_view != nullptr ? object_name(g_c399_get_view(pc)).text : "unknown",
                  hud != nullptr ? class_name(hud).text : "none yet");
    }
}

void c399_client_off(uint8_t* pawn, void* pc) {
    const float gravity = g_c399c.gravity;
    c399_client_end();
    c461_release_row(pawn, "client", GetTickCount64());
    c399_set_cmc(pawn, gravity, false, false);
    g_orig_set_actor_collision(pawn, true);
    c399_vcall(pawn, kEnableInputSlot, pc);
    if (g_c399_view_blend != nullptr) {
        g_c399_view_blend(pc, pawn, 0.0f, 0, 0.0f, false);
    }
    const Vec3 at = actor_location(pawn);
    if (c399_line()) {
        log::line("card399: client t=%llu ride off: own pawn %s at (%.0f, %.0f, %.0f), GravityScale %.2f, input on, "
                  "view target %s", static_cast<unsigned long long>(GetTickCount64()), object_name(pawn).text, at.x,
                  at.y, at.z, static_cast<double>(gravity),
                  g_c399_get_view != nullptr ? object_name(g_c399_get_view(pc)).text : "unknown");
    }
}

// Client, while riding: the camera stays on the dropship (a later ClientRestart puts it back on the pawn).
void c399_client_hold_view(void* pc, void* dropship) {
    if (g_c399_get_view == nullptr || g_c399_view_blend == nullptr || g_c399_get_view(pc) == dropship) {
        return;
    }
    const void* const was = g_c399_get_view(pc);
    g_c399_view_blend(pc, dropship, 0.0f, 0, 0.0f, false);
    if (note_event_cap(g_c399_view_holds, kC399ViewLines, "card399 view hold")) {
        log::line("card399: client t=%llu view target %s put back on %s", static_cast<unsigned long long>(GetTickCount64()),
                  object_name(was).text, object_name(dropship).text);
    }
}

} // namespace

// Card 461 b168: every c46 warp (host c46_warp; client Warp and WarpTo) clears the pawn's bImpartBaseVelocity X / Y / Z
// before it moves (c461_warp_clear) and watches it after (c461_warp_watch): one clear and one restore row per warp.
int32_t c461_warp_clear(void* pawn, int32_t slot, ULONGLONG now) {
    return c461_impart_clear(pawn, slot, now);
}
void c461_warp_watch(void* pawn, int32_t slot, ULONGLONG now, int32_t impart) {
    c461_watch(pawn, slot, now, impart);
}

// Card 595: OnInteraction (0x051D0470) boards PC 0 and starts the level script's end or ride for everyone, and card
// 399 then seats every other player wherever it stands. The host holds the press until every Up or Downed player has
// asked to board this dropship (its own press, or a client Use relayed through kC54Relays); the last request runs the
// original once. Out and dead players (and a slot with no pawn) do not block; card 399 carries them. User rule
// 2026-11-21: a player Downed during the hold (or at its start) blocks it, its card 043 bleedout is capped once to
// kC595BleedS, and its ask is cleared, so a revived player must press Use again; at Out the next step releases.
// The held press leaves the original unrun: no +0x748/+0x749 write, no broadcast, no possession, so no skip input.
C595InteractFn g_orig_c595_interaction = nullptr;

namespace {

constexpr size_t kC595Iface = 0x708; // OnInteraction's this: dropship + 0x708 (lea -0x708(%rcx) at +0x13)
constexpr uint32_t kC595HeldLines = 16;

struct C595Hold {
    C31Weak dropship; // serial 0: nothing held
    uint32_t asked;   // roster slot bits that asked to board
    uint32_t downed;  // roster slot bits seen Downed in this hold (their bleedout is capped)
    ULONGLONG since_ms;
};
C595Hold g_c595{}; // game thread, host
uint32_t g_c595_held_lines = 0;
uint32_t g_c595_client_lines = 0;
constexpr size_t kC595Interactable = 0x748; // bIsInteractable: CanInteract's byte, not replicated (no CPF_Net)
constexpr float kC595BleedS = 10.0f;        // user rule 2026-11-21: bleedout of a player Downed at the evac

uint32_t c595_bits(uint32_t mask) {
    uint32_t n = 0;
    for (int32_t s = 0; s < kC43Max; ++s) {
        n += (mask >> s) & 1u;
    }
    return n;
}

// The slots that must ask: a PlayerController with a pawn that is Up or Downed (not Out or Dead).
uint32_t c595_needed(const C43Roster& r) {
    uint32_t mask = 0;
    for (int32_t s = 0; s < kC43Max; ++s) {
        const C43Life life = r.p[s].pawn != nullptr ? c43_life(r.p[s].pawn) : C43Life::Dead;
        const bool blocks = r.p[s].pc != nullptr && (life == C43Life::Up || life == C43Life::Downed);
        mask |= blocks ? 1u << s : 0u;
    }
    return mask;
}

// Per slot in an active hold: a newly Downed player gets its card 043 bleedout capped once to kC595BleedS and its ask
// cleared; a Downed player that went Out or Dead leaves the downed bits. Returns whether one bled out this pass.
bool c595_scan_downed(void* dropship, const C43Roster& r, ULONGLONG now) {
    bool bled = false;
    for (int32_t s = 0; s < kC43Max; ++s) {
        const uint32_t bit = 1u << s;
        const C43Life life = r.p[s].pawn != nullptr ? c43_life(r.p[s].pawn) : C43Life::Dead;
        if (life != C43Life::Downed) {
            bled = bled || ((g_c595.downed & bit) != 0 && life != C43Life::Up);
            g_c595.downed &= ~bit;
            continue;
        }
        if ((g_c595.downed & bit) != 0) {
            continue;
        }
        g_c595.downed |= bit;
        g_c595.asked &= ~bit;
        const int32_t i = c43_find(object_index(r.p[s].pawn));
        const float before = i >= 0 ? g_c43.e[i].secs_left : -1.0f;
        if (i >= 0 && g_c43.e[i].secs_left > kC595BleedS) {
            g_c43.e[i].secs_left = kC595BleedS;
        }
        log::line("%scard595: host t=%llu evac %s: %s downed during the hold, bleedout capped to %.0f s (%.1f s left "
                  "before, slot %d ask cleared)",
                  i >= 0 ? "" : "WARN ", static_cast<unsigned long long>(now), object_name(dropship).text,
                  object_name(r.p[s].pawn).text, static_cast<double>(kC595BleedS), static_cast<double>(before), s);
    }
    return bled;
}

// Runs the held original once when no needed slot is left to ask; returns whether it ran.
// Card 601: after the host's original OnInteraction runs, every client runs its own copy's original. Card 595 skips
// the client's original at its press, so without this the client never broadcasts +0x728 and its level script
// outro chain (music, outro conversation, C3M2 nuke LevelSequence and shake) never plays. EnterDropship (card 015),
// EndLevel (card 260) and the objective calls (card 140) stay skipped on the client.
void c601_send_board(void* dropship, ULONGLONG now) {
    char name[kChanNameMax + 1];
    if (!c50_placed(dropship) || c50_level_key(dropship, name, sizeof(name)) <= 0) {
        log::line("WARN card601: host t=%llu %s boarded but has no placed name: clients keep no outro",
                  static_cast<unsigned long long>(now), object_name(dropship).text);
        return;
    }
    const C95Fanout f = c95_send_all(g_actor_get_world(dropship), ChanOp::DropshipBoard, name, true);
    log::line("%scard601: host t=%llu boarding of %s sent to %d of %d PCs", f.sent == f.to ? "" : "WARN ",
              static_cast<unsigned long long>(now), name, f.sent, f.to);
}

bool c595_try_release(void* dropship, void* interactor, const C43Roster& r, ULONGLONG now, const char* why) {
    const uint32_t needed = c595_needed(r);
    if ((needed & ~g_c595.asked) != 0) {
        return false;
    }
    log::line("card595: host t=%llu evac %s released: %u of %u living players asked (mask 0x%x of 0x%x) after %llu ms, "
              "%s; the original OnInteraction runs once",
              static_cast<unsigned long long>(now), object_name(dropship).text, c595_bits(g_c595.asked & needed),
              c595_bits(needed), g_c595.asked, needed, static_cast<unsigned long long>(now - g_c595.since_ms), why);
    g_c595 = C595Hold{};
    g_orig_c595_interaction(static_cast<uint8_t*>(dropship) + kC595Iface, interactor);
    c601_send_board(dropship, now);
    return true;
}

} // namespace

// Client: ChanOp::DropshipBoard, the placed name of a transport the host boarded. Runs the client copy's original
// OnInteraction once (the detour is bypassed); a copy whose +0x749 (player inside) is already set has run it.
void c601_board_receive(void* pc, const WireString* s) {
    char name[kChanNameMax + 1];
    const ULONGLONG now = GetTickCount64();
    void* const ds = chan_text(s, name, sizeof(name)) > 0 && g_troops_dropship_class != nullptr &&
                             g_transport_dropship_class != nullptr
                         ? ds_find_placed(g_actor_get_world(pc), name)
                         : nullptr;
    if (ds == nullptr || !is_a(ds, g_transport_dropship_class()) || g_orig_c595_interaction == nullptr) {
        log::line("WARN card601: client t=%llu boarding of %s: no placed transport dropship has that name, no outro",
                  static_cast<unsigned long long>(now), ds == nullptr ? "(unread)" : object_name(ds).text);
        return;
    }
    if (static_cast<const uint8_t*>(ds)[kC595Interactable + 1] != 0) {
        log::line("card601: client t=%llu boarding of %s: own copy already has a player inside, not run again",
                  static_cast<unsigned long long>(now), name);
        return;
    }
    g_orig_c595_interaction(static_cast<uint8_t*>(ds) + kC595Iface, nullptr);
    log::line("card601: client t=%llu boarding of %s from the host: own original ran (outro chain)",
              static_cast<unsigned long long>(now), name);
}

void detour_c595_interaction(void* iface, void* interactor) {
    if (iface == nullptr) {
        g_orig_c595_interaction(iface, interactor);
        return;
    }
    if (g_client_session) {
        // Client: the original would also set +0x749 and broadcast +0x728 on the client's script copy, which starts
        // the local outro (C3M1 @18514: horde, music, objectives, StartAction) while the host holds. Only the prompt
        // clears here; the press reaches the host by the kC54Relays transport row (card 054 ChanOp::Use).
        void* const dropship = static_cast<uint8_t*>(iface) - kC595Iface;
        static_cast<uint8_t*>(dropship)[kC595Interactable] = 0;
        if (note_event_cap(g_c595_client_lines, kC595HeldLines, "card595 client")) {
            log::line("card595: client t=%llu Use on %s relayed, own original skipped",
                      static_cast<unsigned long long>(GetTickCount64()), object_name(dropship).text);
        }
        return;
    }
    void* const dropship = static_cast<uint8_t*>(iface) - kC595Iface;
    const ULONGLONG now = GetTickCount64();
    const C43Roster r = c43_roster(g_actor_get_world(dropship));
    void* const pawn = interactor != nullptr ? read_ptr(interactor, kComponentOwner) : nullptr;
    const int32_t slot = c43_slot(r, object_index(pawn));
    if (slot < 0) {
        log::line("WARN card595: host t=%llu evac %s: OnInteraction from %s (interactor %p) is no roster player's; the "
                  "original runs unheld",
                  static_cast<unsigned long long>(now), object_name(dropship).text, object_name(pawn).text, interactor);
        g_orig_c595_interaction(iface, interactor);
        c601_send_board(dropship, now);
        return;
    }
    if (c31_weak_get(g_c595.dropship) != dropship) {
        g_c595 = C595Hold{};
        c31_weak_set(g_c595.dropship, dropship);
        g_c595.since_ms = now;
    }
    const bool again = (g_c595.asked >> slot & 1u) != 0;
    g_c595.asked |= 1u << slot;
    c595_scan_downed(dropship, r, now); // the hold's start caps players already Downed
    if (c595_try_release(dropship, interactor, r, now, again ? "a repeated press" : "the last press")) {
        return;
    }
    const uint32_t needed = c595_needed(r);
    if (note_event_cap(g_c595_held_lines, kC595HeldLines, "card595 held")) {
        log::line("card595: host t=%llu evac %s held: %u of %u aboard (slot %d %s %s; mask 0x%x of 0x%x)",
                  static_cast<unsigned long long>(now), object_name(dropship).text, c595_bits(g_c595.asked & needed),
                  c595_bits(needed), slot, object_name(pawn).text, again ? "asked again" : "asked", g_c595.asked,
                  needed);
    }
}

void c595_step(const C43Roster& r, ULONGLONG now) {
    if (g_c595.dropship.serial == 0) {
        return;
    }
    void* const dropship = c31_weak_get(g_c595.dropship);
    if (dropship == nullptr) {
        log::line("card595: host t=%llu held evac dropship is gone (mask 0x%x asked); the hold is dropped",
                  static_cast<unsigned long long>(now), g_c595.asked);
        g_c595 = C595Hold{};
        return;
    }
    const bool bled = c595_scan_downed(dropship, r, now);
    c595_try_release(dropship, nullptr, r, now, // the original ignores rdx
                     bled ? "a downed player bled out" : "no other living player is left to ask");
}

// Host, per frame (c46_step): boards each other player's pawn while the host rides, releases it when the host is off.
void c399_step(void* world, ULONGLONG now) {
    if (g_client_session || world == nullptr) {
        return;
    }
    c461_step(now);
    if (now < g_c399_next_ms) {
        return;
    }
    g_c399_next_ms = now + kC399StepMs;
    c399_prune(now);
    const C43Roster r = c43_roster(world);
    c595_step(r, now); // card 595: a held boarding, before the ride check
    const uint8_t* const ride = c399_host_ride(r);
    for (int32_t s = 1; s < kC43Max; ++s) {
        void* const pawn = r.p[s].pawn;
        if (pawn == nullptr || !is_a(pawn, g_player_character_class()) || sprint_side(pawn) != kSideServerCopy) {
            continue;
        }
        const int32_t row = c399_row(pawn);
        if (row >= 0 && (ride == nullptr || g_get_attach_parent_actor(pawn) != ride)) {
            c399_release(row, pawn, r, s, now);
        } else if (row < 0 && ride != nullptr) {
            c399_board(pawn, ride, s, now);
        }
    }
    c400_step(r, now);
}

void c461_base_text(const void* pawn, char* buf, size_t cap) {
    const void* const base = pawn != nullptr ? c461_base(pawn) : nullptr;
    if (base == nullptr) {
        snprintf(buf, cap, "none");
        return;
    }
    snprintf(buf, cap, "%s of %s", object_name(base).text, object_name(read_ptr(base, kComponentOwner)).text);
}

// Card 461: every capsule move goes through here; while a release watch is open, a move of a watched root of more
// than kC461MoveUu in one call gets a row with its caller stack. Outside the window the cost is one compare.
bool detour_c461_move(void* comp, const void* delta, const void* rotation, bool sweep, void* hit, int32_t flags,
                      uint8_t teleport) {
    if (!g_c461_armed) {
        return perf_link_c461_move(comp, delta, rotation, sweep, hit, flags, teleport); // card 578: ubwperf 539/542
    }
    const int32_t s = c461_slot_of_root(comp);
    if (s < 0) {
        return g_orig_c461_move(comp, delta, rotation, sweep, hit, flags, teleport);
    }
    C461Watch& w = g_c461[s];
    void* const pawn = c31_weak_get(w.pawn);
    if (pawn != nullptr && c461_mode(pawn) != kC461Falling) {
        c461_note_base(w, pawn); // b167: the base before the switch to Falling (SetBase(null) runs at the switch)
    }
    const Vec3 from = read_vec3(comp, kComponentTranslation);
    const bool out = g_orig_c461_move(comp, delta, rotation, sweep, hit, flags, teleport);
    const Vec3 to = read_vec3(comp, kComponentTranslation);
    const double uu = c461_dist(from, to);
    const ULONGLONG now = GetTickCount64();
    if (pawn != nullptr) {
        const double speed = c461_dist(c461_velocity(pawn), Vec3{});
        const bool falling = c461_mode(pawn) == kC461Falling;
        if (!w.confirmed && (falling || (w.speed < kC461SpeedLow && speed > kC461SpeedHigh))) {
            w.confirmed = true;
            c461_confirm_rows(w, pawn, s, now, falling ? "the first Falling move" : "a speed jump");
        }
        w.speed = speed;
    }
    if (uu <= kC461MoveUu) {
        return out;
    }
    ++w.moved; // counted past the row cap too, so the frame row's no-move-row check stays true
    if (w.moves >= kC461MoveRows) {
        return out;
    }
    ++w.moves;
    char stack[kC461StackChars];
    caller_chain(stack, sizeof(stack), kC461MoveFrames);
    log::line("card461: %s t=%llu move (%.0f, %.0f, %.0f) -> (%.0f, %.0f, %.0f) (%.0f uu), teleport %u, sweep %d, mode "
              "%d, +%llu ms, slot %d, stack exe+%s", side(), static_cast<unsigned long long>(now), from.x, from.y,
              from.z, to.x, to.y, to.z, uu, static_cast<unsigned>(teleport), sweep ? 1 : 0,
              pawn != nullptr ? c461_mode(pawn) : -1, static_cast<unsigned long long>(now - w.start), s, stack);
    return out;
}

void c461_fall_rows(float damage, float out, const char* stack) {
    log::line("card461: %s t=%llu Fall damage %.3f -> %.3f in a release window, stack exe+%s", side(),
              static_cast<unsigned long long>(GetTickCount64()), static_cast<double>(damage), static_cast<double>(out),
              stack);
    for (int32_t s = 0; s < kC43Max; ++s) {
        const void* const pawn = g_c461[s].pawn.serial != 0 ? c31_weak_get(g_c461[s].pawn) : nullptr;
        if (pawn == nullptr) {
            continue;
        }
        c461_confirm_rows(g_c461[s], pawn, s, GetTickCount64(), "the Fall damage");
        const Vec3 vel = c461_velocity(pawn);
        const Vec3 at = actor_location(pawn);
        char fields[kC461FieldChars];
        c461_fields(pawn, fields, sizeof(fields));
        log::line("card461: %s Fall slot %d %s at (%.0f, %.0f, %.0f), velocity (%.0f, %.0f, %.0f), mode %d; pawn %s",
                  side(), s, object_name(pawn).text, at.x, at.y, at.z, vel.x, vel.y, vel.z, c461_mode(pawn),
                  fields[0] != '\0' ? fields : "no Fall/Air/Apex fields");
        c461_fields(read_ptr(pawn, kCharacterMovement), fields, sizeof(fields));
        log::line("card461: %s Fall slot %d CharacterMovement %s", side(), s,
                  fields[0] != '\0' ? fields : "no Fall/Air/Apex fields");
    }
}

// Client, every tick (own-pawn watch): its own pawn attached to a transport dropship rides; returns whether it does.
bool c399_client_step(uint8_t* pawn, void* pc) {
    c461_step(GetTickCount64());
    void* const parent = g_get_attach_parent_actor(pawn);
    const bool ride = parent != nullptr && is_a(parent, g_transport_dropship_class()) &&
                      sprint_side(pawn) == kSideClientOwn && is_a(pawn, g_player_character_class());
    void* const was = g_c399c.pawn.serial != 0 ? c31_weak_get(g_c399c.pawn) : nullptr;
    if (ride && was != pawn) {
        c399_client_on(pawn, pc, parent);
    } else if (!ride && was == pawn && pawn != nullptr) {
        c399_client_off(pawn, pc);
    } else if (was != pawn && g_c399c.pawn.serial != 0) {
        c399_client_end(); // the riding pawn is gone or no longer this PC's
    }
    if (ride) {
        c399_client_hold_view(pc, parent);
        c399_client_hold_hud(pc);
        c399_client_gap(pawn, parent, GetTickCount64());
    }
    return ride;
}

// Client, a Warp from the host while its own pawn still rides: the host's release detaches the server copy and sends
// the Warp in the same step, and the Warp arrives before the replicated detach (run c399b86a, every ride map), so the
// own pawn still counts as held. The ride ends here (detach with the game's rules, then gravity, collision, input,
// camera and HUD back) and the warp places it. Returns the own pawn, or null when it does not ride.
void* c399_client_land(void* pc) {
    void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    void* const was = g_c399c.pawn.serial != 0 ? c31_weak_get(g_c399c.pawn) : nullptr;
    if (pawn == nullptr || pawn != was || g_c399_detach == nullptr) {
        return nullptr;
    }
    g_c399_detach(pawn, c399_rva(kDetachRulesRva));
    if (c399_line()) {
        log::line("card399: client t=%llu Warp while riding: own pawn detached here, before the replicated detach",
                  static_cast<unsigned long long>(GetTickCount64()));
    }
    c399_client_off(static_cast<uint8_t*>(pawn), pc);
    return pawn;
}

// Client, after the landing warp: the control rotation takes the host pawn's yaw, level pitch (the host's comes from
// possessing its pawn at the dropship's exit).
void c399_client_face(void* pc, double yaw, const char* source) {
    if (g_orig_set_control_rotation == nullptr) {
        log::line("WARN card399: client landing facing not set: no SetControlRotation (%s)", source);
        return;
    }
    const double rot[3] = {0.0, yaw, 0.0};
    g_orig_set_control_rotation(pc, rot);
    if (c399_line()) {
        log::line("card399: client t=%llu landing facing: control rotation yaw %.1f (%s)",
                  static_cast<unsigned long long>(GetTickCount64()), yaw, source);
    }
}

// Card 400, host: remote player pawns hidden while their client is not in yet (card 320 sent IntroStart, no IntroDone).
// A row exists while its weak ref is set; the row is the one source of truth for the damage gate (death.cpp) and the
// enemy target gate (enemy_c103.cpp). Collision stays as it is (a rejoined pawn must stand on the ground).
namespace {

constexpr uint32_t kC400Lines = 64; // card400 rows per session (a hide and a show per player per map)
C31Weak g_c400[kC43Max] = {};
uint32_t g_c400_lines = 0;

bool c400_line() {
    return note_event_cap(g_c400_lines, kC400Lines, "card400");
}

int32_t c400_row(const void* pawn) {
    for (int32_t i = 0; pawn != nullptr && i < kC43Max; ++i) {
        if (g_c400[i].serial != 0 && c31_weak_get(g_c400[i]) == pawn) {
            return i;
        }
    }
    return -1;
}

void c400_hide(void* pawn, int32_t slot, bool riding, ULONGLONG now) {
    int32_t row = -1;
    for (int32_t i = 0; row < 0 && i < kC43Max; ++i) {
        row = g_c400[i].serial == 0 ? i : -1;
    }
    if (row < 0 || g_orig_c98_hidden == nullptr) {
        if (c400_line()) {
            log::line("WARN card400: host t=%llu %s (slot %d) not hidden: free row %d, SetActorHiddenInGame %d",
                      static_cast<unsigned long long>(now), object_name(pawn).text, slot, row,
                      g_orig_c98_hidden != nullptr ? 1 : 0);
        }
        return;
    }
    c31_weak_set(g_c400[row], pawn);
    g_orig_c98_hidden(pawn, true); // a rider is hidden already (card 399); setting it again changes nothing
    if (c400_line()) {
        log::line("card400: host t=%llu hide %s (slot %d): client not confirmed yet (%s); no damage, no enemy target",
                  static_cast<unsigned long long>(now), object_name(pawn).text, slot,
                  riding ? "riding the dropship" : "on foot");
    }
}

void c400_show(int32_t row, void* pawn, int32_t slot, bool riding, ULONGLONG now) {
    g_c400[row] = C31Weak{};
    if (!riding) {
        g_orig_c98_hidden(pawn, false);
    }
    if (c400_line()) {
        log::line("card400: host t=%llu show %s (slot %d): client confirmed%s",
                  static_cast<unsigned long long>(now), object_name(pawn).text, slot,
                  riding ? ", rides: stays hidden until the card 399 release" : "");
    }
}

} // namespace

bool c400_shielded(const void* pawn) {
    return !g_client_session && c400_row(pawn) >= 0;
}

void c400_step(const C43Roster& r, ULONGLONG now) {
    for (int32_t i = 0; i < kC43Max; ++i) {
        if (g_c400[i].serial != 0 && c31_weak_get(g_c400[i]) == nullptr) {
            g_c400[i] = C31Weak{};
            if (c400_line()) {
                log::line("card400: host t=%llu row %d dropped: pawn gone (logout or map end)",
                          static_cast<unsigned long long>(now), i);
            }
        }
    }
    for (int32_t s = 1; s < kC43Max; ++s) {
        void* const pawn = r.p[s].pawn;
        if (pawn == nullptr || !is_a(pawn, g_player_character_class()) || sprint_side(pawn) != kSideServerCopy) {
            continue;
        }
        const bool want = c320_unconfirmed(r.p[s].pc);
        const int32_t row = c400_row(pawn);
        const bool riding = c399_row(pawn) >= 0;
        if (want && row < 0) {
            c400_hide(pawn, s, riding, now);
        } else if (!want && row >= 0) {
            c400_show(row, pawn, s, riding, now);
        }
    }
}

} // namespace ubw::hooks
