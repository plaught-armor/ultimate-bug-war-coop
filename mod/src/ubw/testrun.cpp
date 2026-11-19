// Test runner: commands and state (card 063), goto and look (card 064), and the host's mission brief
// and `ui continue` (card 067).

#include "ubw/hooks_internal.h"
#include "winmsg.h"

namespace ubw::hooks {

constexpr size_t kC63ShotChars = ue::config::kMaxTagLen + 1 + cmdfile::kNameChars; // "<tag>-<name>", NUL included
constexpr size_t kC63StateChars = 512;

C63 g_c63{};

// --- Card 064: goto and look for the test runner (UBWCOOP_CMD=1) ------------------------------------------------
// `goto` asks the world's navigation system for a path (UNavigationSystemV1::FindPathToLocationSynchronously through
// ProcessEvent on the class default object, the pawn as PathfindingContext) and walks it with real input only (card
// 124): W held (LeftShift too with `sprint`), the view turned toward the next path point through MouseX axis events at
// a capped, eased turn rate; the control rotation is never written during a walk. A net client has its own navigation
// system and navmesh too (c64_allow_client_nav), so both roles walk navmesh paths; the straight line to the goal is
// only the fallback when a world has no main nav data (logged as a WARN).
// Card 124: a goto runs in the background. Its command logs "goto: started" and leaves the queue, so look, key, shot,
// axis and warp run while it walks; its result line ("-> done" or "-> failed (...)") comes at the walk's end. A new
// goto replaces the running one (that one fails "replaced"). Stuck (under kC64StuckUu of travel in one check) tries,
// in turn, a fresh path query from the current spot, a jump (SpaceBar), and a side step (S back, then A or D); the
// next check logs which one freed the pawn. A give-up names the spot and what the forward traces hit.
constexpr int32_t kC64MaxPoints = 64;
constexpr int32_t kC64MaxNavData = 8;         // nav data actors listed per map
constexpr int32_t kC64MaxStucks = 6;          // each of the three recoveries is tried twice
constexpr int32_t kC64Recoveries = 3;
constexpr ULONGLONG kC64CheckMs = 2000;       // progress line and stuck check
constexpr ULONGLONG kC64BackMs = 600;         // side step: S held first
constexpr ULONGLONG kC64SideMs = 800;         // side step: then A or D held
constexpr ULONGLONG kC64JumpTapMs = 100;      // SpaceBar held for a jump
constexpr ULONGLONG kC64SlackMs = 20000;      // timeout: path length at walk speed plus this
constexpr ULONGLONG kC64MaxTimeoutMs = 600000;
constexpr double kC64StuckUu = 50.0;
constexpr double kC64PointUu = 120.0;         // a path point within this (2D) is passed
constexpr double kC64GoalDz = 300.0;          // done: within the radius in 2D and this in z
constexpr double kC64MinSpeed = 100.0;        // uu/s floor for the timeout, when MaxWalkSpeed reads lower
constexpr double kC64WalkOffDeg = 75.0;       // W is let go while the view is this far off the path point...
constexpr double kC64WalkOnDeg = 45.0;        // ...and pressed again under this, so a corner never becomes an orbit
// Within kC64NearUu of the point steered to, W needs the view on it: the eased turn (4 deg/s per degree, at most 240)
// at 700 uu/s draws a circle of 170-330 uu, wider than a goal radius, so a close point is aimed at before walking
// (c124c: the pawn circled a radius-80 goal at 55-105 uu for 20 s).
constexpr double kC64NearUu = 500.0;
constexpr double kC64NearOffDeg = 25.0;
constexpr double kC64NearOnDeg = 10.0;
constexpr double kC64ProbeUu = 150.0;         // give-up: forward trace length
constexpr double kC64NavGapUu = 1.0;          // the goal is past the navmesh end by more than this
constexpr double kC64ProjectHalfZ = 1200.0; // run 11: BP_PlayerPresence_C_11 centre z 520, no mesh point inside 500
constexpr size_t kC64ParmsCap = 256;

constexpr wchar_t kC64NavDataClass[] = L"/Script/NavigationSystem.NavigationData";
constexpr wchar_t kC64AiLibClass[] = L"/Script/AIModule.AIBlueprintHelperLibrary";

enum class C64Phase : uint8_t { Walk, Back, Side };

// Card 124: turning the view through MouseX/MouseY like a hand on a mouse. The turn rate follows the angle left
// (kC124RatePerDeg per second, so the turn slows as it lands), capped at kC124MaxRate, and changes by at most
// kC124Accel per second per second (easing in and out). The axis value per event is the angle wanted this tick over
// the gain (degrees per axis unit per event), learned from what the view actually did over the last kC124AdaptUnits.
constexpr double kC124MaxRate = 240.0;        // deg/s: a calm human turn (flicks run 500+, tracking 100-300)
constexpr double kC124RatePerDeg = 4.0;       // 1/s
constexpr double kC124Accel = 1440.0;         // deg/s^2: 240 deg/s reached in about 1/6 s
// b16 (b15 user report: "when the client goes to pick something up they spin"): within kB16SlowUu of the path point
// a goto steers at most kB16SlowRate. Next to the point a small step sideways swings the bearing by tens of degrees,
// and the full rate turned that into fast spins at the pickups.
constexpr double kB16SlowUu = 200.0;
constexpr double kB16SlowRate = 90.0;          // deg/s
double g_c124_cap = kC124MaxRate;             // the turn cap for the current c124_turn call
constexpr double kC124GainYaw = -0.14;        // deg per MouseX unit per event, measured in c92c (+MouseX lowers yaw)
constexpr double kC124GainPitch = 0.14;       // first guess; the sign and size are learned
constexpr double kC124AdaptUnits = 40.0;      // axis units summed before the gain is re-measured
constexpr double kC124MinGain = 0.001;
constexpr double kC124MaxGain = 10.0;
constexpr double kC124DoneDeg = 1.0;          // look smooth: done within this, both axes
constexpr ULONGLONG kC124LookMs = 3000;       // look smooth: fails after this
constexpr ULONGLONG kC124MaxDtMs = 50;

struct C124Axis {
    double gain;         // deg per axis unit per event
    double rate;         // deg/s now
    double last;         // the angle at the last tick
    double sent;         // axis units sent since the gain was last measured
    double seen;         // degrees the view moved over those ticks
    bool sending;        // an event went out last tick
};

struct C124Turn {
    C124Axis yaw;
    C124Axis pitch;
    ULONGLONG last_ms;   // 0: the next tick starts a new turn
    int32_t events;      // axis events sent during the current walk or look
    double peak;         // deg/s, the highest rate in it
    int32_t rot_writes;  // control rotation writes during the walk (plain look); 0 is the goal
    double look_yaw;     // look smooth: the target
    double look_pitch;
};
C124Turn g_c124{{kC124GainYaw, 0, 0, 0, 0, false}, {kC124GainPitch, 0, 0, 0, 0, false}, 0, 0, 0, 0, 0, 0};

constexpr uint8_t kC64MoveFalling = 3; // EMovementMode::MOVE_Falling
constexpr double kC64StillUu = 0.5;     // per tick: under this the pawn counts as standing
constexpr uint8_t kC64StillTicks = 3;  // ticks in a row under kC64StillUu, not falling: the walk ends

struct C64Walk {
    Vec3 goal;
    Vec3 pts[kC64MaxPoints];
    int32_t npts;
    int32_t at;               // the path point steered to
    double radius;
    double speed;             // MaxWalkSpeed, for the timeout
    double length;            // the path length when queried
    double nav_gap;           // 2D uu from the navmesh path's end to the goal (0: the path reaches it)
    double floor_z;           // z of the navmesh path's end: an actor goal's root can sit far above or below the floor
    Vec3 last;                // pawn location at the last check
    ULONGLONG start_ms;
    ULONGLONG until;          // the walk's timeout
    ULONGLONG next_check;
    ULONGLONG phase_until;
    ULONGLONG jump_up;        // SpaceBar goes up at this time (0: not down)
    C64Phase phase;
    int32_t stucks;
    const char* trying;       // the recovery under test since the last stuck, or null
    bool running;
    bool w_down;
    bool sprint;
    bool landing;             // reached the goal: keys up, ends when the pawn stands still on the ground
    uint8_t still_ticks;      // ticks in a row the pawn moved under kC64StillUu while landing
    Vec3 settle_at;           // pawn location at the last landing tick
    uint32_t seq;             // the goto command's seq and line, for its result at the walk's end
    char text[cmdfile::kLineChars];
    double dist;              // 2D distance to the goal at the last tick, for the state line
    const char* source;       // "navmesh" or "straight line"
    const void* nav_world;    // the world whose nav setup was logged
    uint64_t find_path_fname;
    uint64_t project_fname;
    uint64_t simple_move_fname;
    uint64_t pawn_profile;
};
C64Walk g_c64{};

double c64_dist2d(const Vec3& a, const Vec3& b) {
    return sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

// The float property `name` of `object`, or -1 when absent.
float c64_float_field(const void* object, const char* name) {
    const uint8_t* const p = object != nullptr ? prop_find_prop(read_ptr(object, kObjectClass), name) : nullptr;
    return p != nullptr && prop_kind_is(p, "FloatProperty") ? read_f32(object, static_cast<size_t>(read_i32(p, kPropertyOffset)))
                                                           : -1.0f;
}

void* c64_movement(const void* pawn) {
    const uint8_t* prop = nullptr;
    return const_cast<void*>(prop_object_field(pawn, "CharacterMovement", &prop));
}

// NavAgentProps.AgentRadius of the pawn's movement component, or -1.
float c64_agent_radius(const void* pawn) {
    const void* const cmc = c64_movement(pawn);
    const uint8_t* const p = cmc != nullptr ? prop_find_prop(read_ptr(cmc, kObjectClass), "NavAgentProps") : nullptr;
    const void* const s = p != nullptr && prop_kind_is(p, "StructProperty") ? read_ptr(p, kStructPropStruct) : nullptr;
    const uint8_t* const r = s != nullptr ? prop_find_prop(s, "AgentRadius") : nullptr;
    if (r == nullptr) {
        return -1.0f;
    }
    return read_f32(cmc, static_cast<size_t>(read_i32(p, kPropertyOffset) + read_i32(r, kPropertyOffset)));
}

// The world's NavigationSystem (UWorld::NavigationSystem, reflected), or null.
void* c64_nav_system(const void* world) {
    const uint8_t* prop = nullptr;
    return const_cast<void*>(prop_object_field(world, "NavigationSystem", &prop));
}

// Test runner only (UBWCOOP_CMD=1, game thread): FED_C1M1 registers its nav data but leaves MainNavData null, and
// FindPathToLocationSynchronously returns no path object without a default nav data. Adopts the placed
// RecastNavMesh-Default (agent Default, radius 75, the trooper's agent) as MainNavData. Returns it, or null.
const void* c64_adopt_main_nav(const void* world, void* nav, const Vec3& from) {
    const uint8_t* prop = nullptr;
    const void* const main = nav != nullptr ? prop_object_field(nav, "MainNavData", &prop) : nullptr;
    if (main != nullptr || prop == nullptr) {
        return main;
    }
    void* const mesh = c64_find_actor(world, "RecastNavMesh-Default", from);
    if (mesh == nullptr || strcmp(object_name(mesh).text, "RecastNavMesh-Default") != 0) {
        log::line("WARN card064: no RecastNavMesh-Default to adopt as MainNavData");
        return nullptr;
    }
    memcpy(static_cast<uint8_t*>(nav) + read_i32(prop, kPropertyOffset), &mesh, sizeof(mesh));
    log::line("card064: MainNavData was null; set to %s (%s) for the test runner", object_name(mesh).text,
              class_name(mesh).text);
    return mesh;
}

// NavigationSystemV1::NavDataSet.Num() (the registered nav data), or -1 when the property is not found.
int32_t c64_registered_nav_data(const void* nav) {
    const uint8_t* const p = nav != nullptr ? prop_find_prop(read_ptr(nav, kObjectClass), "NavDataSet") : nullptr;
    return p != nullptr ? read_i32(nav, static_cast<size_t>(read_i32(p, kPropertyOffset)) + kArrayNum) : -1;
}

// Once per world: what the card asks to be proven before the walker relies on it.
void c64_log_nav(const void* world, const void* pawn) {
    g_c64.nav_world = world;
    void* const ai_lib = find_class(kC64AiLibClass);
    void* const ai_cdo = ai_lib != nullptr ? read_ptr(ai_lib, kClassDefaultObject) : nullptr;
    const uint64_t move = fname_cached(g_c64.simple_move_fname, L"SimpleMoveToLocation");
    const void* const move_fn = ai_cdo != nullptr && move != 0 ? g_find_function(ai_cdo, move) : nullptr;
    void* const nav = c64_nav_system(world);
    const uint8_t* prop = nullptr;
    const void* const main = nav != nullptr ? prop_object_field(nav, "MainNavData", &prop) : nullptr;
    log::line("card064: %s world %s: AIBlueprintHelperLibrary %p, SimpleMoveToLocation %p; NavigationSystem %p (%s), "
              "MainNavData %s (%s); pawn NavAgentProps.AgentRadius %.0f",
              g_client_session ? "client" : "host", object_name(world).text, ai_lib, move_fn, nav,
              nav != nullptr ? class_name(nav).text : "-", main != nullptr ? object_name(main).text : "-",
              main != nullptr ? class_name(main).text : (prop != nullptr ? "null" : "no such property"),
              c64_agent_radius(pawn));
    const void* const cmc = c64_movement(pawn);
    const float jump = c64_float_field(cmc, "JumpZVelocity");
    const float grav = c64_float_field(cmc, "GravityScale");
    const double apex = grav > 0.0f ? static_cast<double>(jump) * jump / (2.0 * 980.0 * grav) : -1.0;
    log::line("card124: %s pawn CharacterMovement MaxStepHeight %.1f, JumpZVelocity %.1f, GravityScale %.2f (jump apex "
              "%.0f uu at world gravity 980), MaxWalkSpeed %.0f", g_client_session ? "client" : "host",
              c64_float_field(cmc, "MaxStepHeight"), jump, grav, apex, c64_float_field(cmc, "MaxWalkSpeed"));
    const void* const cls = find_class(kC64NavDataClass);
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    int32_t listed = 0;
    for (int32_t l = 0; cls != nullptr && levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const auto* const actors = levels[l] != nullptr ? static_cast<void* const*>(read_ptr(levels[l], kLevelActors)) : nullptr;
        const int32_t num = levels[l] != nullptr ? read_i32(levels[l], kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; actors != nullptr && a < num && a < kMaxLevelActors && listed < kC64MaxNavData; ++a) {
            if (actors[a] != nullptr && is_a(actors[a], cls)) {
                ++listed;
                log::line("card064: nav data actor %s (%s) in level %d, AgentRadius %.0f", object_name(actors[a]).text,
                          class_name(actors[a]).text, l, c64_float_field(actors[a], "AgentRadius"));
            }
        }
    }
    log::line("card064: %d nav data actor(s) listed (class NavigationData %p); NavDataSet holds %d registered", listed,
              cls, c64_registered_nav_data(nav));
}

// FindPathToLocationSynchronously(pawn, from, to, pawn) into g_c64.pts. Returns the point count, 0 for no path,
// -1 when the call could not be made (logged).
int32_t c64_query(void* pawn, void* pf_ctx, const Vec3& from, const Vec3& to) {
    void* const cls = find_class(kC64NavSysClass);
    void* const cdo = cls != nullptr ? read_ptr(cls, kClassDefaultObject) : nullptr;
    const uint64_t name = fname_cached(g_c64.find_path_fname, L"FindPathToLocationSynchronously");
    void* const fn = cdo != nullptr && name != 0 ? g_find_function(cdo, name) : nullptr;
    const uint8_t* const p_ctx = fn != nullptr ? prop_find_prop(fn, "WorldContextObject") : nullptr;
    const uint8_t* const p_start = fn != nullptr ? prop_find_prop(fn, "PathStart") : nullptr;
    const uint8_t* const p_end = fn != nullptr ? prop_find_prop(fn, "PathEnd") : nullptr;
    const uint8_t* const p_pf = fn != nullptr ? prop_find_prop(fn, "PathfindingContext") : nullptr;
    const uint8_t* const p_ret = fn != nullptr ? prop_find_prop(fn, "ReturnValue") : nullptr;
    const int32_t size = fn != nullptr ? (read_i32(fn, kFunctionParmsSize) & 0xFFFF) : 0;
    if (p_ctx == nullptr || p_start == nullptr || p_end == nullptr || p_pf == nullptr || p_ret == nullptr ||
        size <= 0 || static_cast<size_t>(size) > kC64ParmsCap) {
        log::line("WARN card064: FindPathToLocationSynchronously not callable: class %p cdo %p fn %p parms %d", cls, cdo,
                  fn, size);
        return -1;
    }
    alignas(16) uint8_t parms[kC64ParmsCap] = {};
    memcpy(parms + read_i32(p_ctx, kPropertyOffset), &pawn, sizeof(pawn));
    memcpy(parms + read_i32(p_start, kPropertyOffset), &from, sizeof(from));
    memcpy(parms + read_i32(p_end, kPropertyOffset), &to, sizeof(to));
    memcpy(parms + read_i32(p_pf, kPropertyOffset), &pf_ctx, sizeof(pf_ctx));
    g_orig_process_event(cdo, fn, parms);
    const void* path = nullptr;
    memcpy(&path, parms + read_i32(p_ret, kPropertyOffset), sizeof(path));
    const uint8_t* const p_pts = path != nullptr ? prop_find_prop(read_ptr(path, kObjectClass), "PathPoints") : nullptr;
    const size_t off = p_pts != nullptr ? static_cast<size_t>(read_i32(p_pts, kPropertyOffset)) : 0;
    const auto* const data = p_pts != nullptr ? static_cast<const Vec3*>(read_ptr(path, off)) : nullptr;
    const int32_t num = p_pts != nullptr ? read_i32(path, off + kArrayNum) : 0;
    g_c64.npts = 0;
    for (int32_t i = 0; data != nullptr && i < num && g_c64.npts < kC64MaxPoints; ++i) {
        g_c64.pts[g_c64.npts++] = data[i];
    }
    if (num <= 0) {
        log::line("card064: path query (%.0f %.0f %.0f) to (%.0f %.0f %.0f), context %s: path %p (%s), PathPoints %p, %d point(s)",
                  from.x, from.y, from.z, to.x, to.y, to.z, pf_ctx != nullptr ? "pawn" : "none", path,
                  path != nullptr ? class_name(path).text : "-", p_pts, num);
    }
    return g_c64.npts;
}

// K2_ProjectPointToNavigation(pawn, point, out, main nav data, no filter, extent): the navmesh point within `extent`
// (half sizes) of `point`. False when none (logged by the caller) or the call could not be made (logged here).
bool c64_project(void* world, void* pawn, const Vec3& point, const Vec3& extent, Vec3& out) {
    if (c64_adopt_main_nav(world, c64_nav_system(world), point) == nullptr) {
        return false; // logged by c64_adopt_main_nav
    }
    void* const cls = find_class(kC64NavSysClass);
    void* const cdo = cls != nullptr ? read_ptr(cls, kClassDefaultObject) : nullptr;
    const uint64_t name = fname_cached(g_c64.project_fname, L"K2_ProjectPointToNavigation");
    void* const fn = cdo != nullptr && name != 0 ? g_find_function(cdo, name) : nullptr;
    const uint8_t* const p_ctx = fn != nullptr ? prop_find_prop(fn, "WorldContextObject") : nullptr;
    const uint8_t* const p_point = fn != nullptr ? prop_find_prop(fn, "Point") : nullptr;
    const uint8_t* const p_out = fn != nullptr ? prop_find_prop(fn, "ProjectedLocation") : nullptr;
    const uint8_t* const p_ext = fn != nullptr ? prop_find_prop(fn, "QueryExtent") : nullptr;
    const uint8_t* const p_ret = fn != nullptr ? prop_find_prop(fn, "ReturnValue") : nullptr;
    const int32_t size = fn != nullptr ? (read_i32(fn, kFunctionParmsSize) & 0xFFFF) : 0;
    if (p_ctx == nullptr || p_point == nullptr || p_out == nullptr || p_ext == nullptr || p_ret == nullptr ||
        size <= 0 || static_cast<size_t>(size) > kC64ParmsCap) {
        log::line("WARN card126: K2_ProjectPointToNavigation not callable: class %p cdo %p fn %p parms %d", cls, cdo,
                  fn, size);
        return false;
    }
    alignas(16) uint8_t parms[kC64ParmsCap] = {};
    memcpy(parms + read_i32(p_ctx, kPropertyOffset), &pawn, sizeof(pawn));
    memcpy(parms + read_i32(p_point, kPropertyOffset), &point, sizeof(point));
    memcpy(parms + read_i32(p_ext, kPropertyOffset), &extent, sizeof(extent));
    g_orig_process_event(cdo, fn, parms); // NavData and FilterClass stay null: the main nav data, default filter
    memcpy(&out, parms + read_i32(p_out, kPropertyOffset), sizeof(out));
    return parms[read_i32(p_ret, kPropertyOffset)] != 0;
}

// The goal itself has no path (it lies off the navmesh: an actor's root, a volume's centre over a blocked cell).
// Queries the path to the navmesh point nearest the goal instead, within the goto radius, so the walk still ends
// inside the radius the command asked for. Returns c64_query's count; 0 when no navmesh point lies that near.
int32_t c64_query_projected(void* world, void* pawn, const Vec3& from) {
    const Vec3 extent{g_c64.radius, g_c64.radius, kC64ProjectHalfZ};
    Vec3 on_mesh{};
    if (!c64_project(world, pawn, g_c64.goal, extent, on_mesh)) {
        log::line("cmd: %u goto: goal (%.0f %.0f %.0f) has no path and no navmesh point within %.0f uu", g_c64.seq,
                  g_c64.goal.x, g_c64.goal.y, g_c64.goal.z, g_c64.radius);
        return 0;
    }
    const int32_t n = c64_query(pawn, pawn, from, on_mesh);
    log::line("cmd: %u goto: goal (%.0f %.0f %.0f) has no path; nearest navmesh point (%.0f %.0f %.0f), %.0f uu away: "
              "%d point(s)", g_c64.seq, g_c64.goal.x, g_c64.goal.y, g_c64.goal.z, on_mesh.x, on_mesh.y, on_mesh.z,
              c64_dist2d(on_mesh, g_c64.goal), n);
    return n;
}

// Fills the path from the pawn's location: the navmesh path, or (a WARN) the straight line with no main nav data.
const char* c64_plan(void* world, void* pawn) {
    const Vec3 from = actor_location(pawn);
    g_c64.at = 0;
    g_c64.source = "straight line (no main nav data)";
    g_c64.npts = 0;
    g_c64.nav_gap = 0.0;
    g_c64.floor_z = g_c64.goal.z;
    if (c64_adopt_main_nav(world, c64_nav_system(world), from) != nullptr) {
        int32_t n = c64_query(pawn, pawn, from, g_c64.goal);
        if (n == 0) {
            n = c64_query(pawn, nullptr, from, g_c64.goal); // the main nav data, for comparison
        }
        if (n == 0) {
            n = c64_query_projected(world, pawn, from);
        }
        if (n <= 0) {
            return n < 0 ? "no path (query not callable)" : "no path";
        }
        g_c64.source = "navmesh";
        g_c64.nav_gap = c64_dist2d(g_c64.pts[g_c64.npts - 1], g_c64.goal);
        g_c64.floor_z = g_c64.pts[g_c64.npts - 1].z;
    } else {
        log::line("WARN card124: %s world %s has no main nav data; the walk falls back to a straight line to the goal",
                  g_client_session ? "client" : "host", object_name(world).text);
    }
    if (g_c64.npts == 0 || c64_dist2d(g_c64.pts[g_c64.npts - 1], g_c64.goal) > kC64NavGapUu) {
        g_c64.pts[g_c64.npts < kC64MaxPoints ? g_c64.npts++ : kC64MaxPoints - 1] = g_c64.goal; // walk on to the goal
    }
    double len = 0.0;
    Vec3 prev = from;
    for (int32_t i = 0; i < g_c64.npts; ++i) {
        len += vec_dist(prev, g_c64.pts[i]);
        prev = g_c64.pts[i];
    }
    g_c64.length = len;
    return nullptr;
}

// A fresh path from where the pawn stands; on failure (a WARN) the straight line to the goal.
void c64_replan(void* world, void* pawn, const char* when) {
    const char* const why = c64_plan(world, pawn);
    if (why != nullptr) {
        log::line("WARN card064: re-query %s failed (%s); the walk goes straight to the goal", when, why);
        g_c64.pts[0] = g_c64.goal;
        g_c64.npts = 1;
        g_c64.at = 0;
        return;
    }
    log::line("cmd: %u goto: re-query %s: %s of %d points, %.0f uu", g_c64.seq, when, g_c64.source, g_c64.npts,
              g_c64.length);
}

// Plain `look` (a setup step) writes the control rotation at once; during a walk the write is counted.
void c64_set_yaw(void* pc, double yaw, double pitch) {
    const double rot[3] = {pitch, yaw, 0.0};
    if (g_c64.running) {
        ++g_c124.rot_writes;
    }
    g_orig_set_control_rotation(pc, rot);
}

double c124_wrap(double deg) {
    const double d = fmod(deg, 360.0);
    return d > 180.0 ? d - 360.0 : (d <= -180.0 ? d + 360.0 : d);
}

double c124_clamp(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// One axis for one tick: re-measures the gain from what the view did, then returns the axis value that moves it
// this tick's share of the way toward `want` (0: no event).
float c124_axis_step(C124Axis& a, double now_deg, double want, double dt, bool fresh) {
    if (!fresh && a.sending) {
        a.seen += c124_wrap(now_deg - a.last);
    }
    a.last = now_deg;
    if (fabs(a.sent) >= kC124AdaptUnits) {
        const double g = a.seen / a.sent;
        if (fabs(g) >= kC124MinGain && fabs(g) <= kC124MaxGain) {
            a.gain = g;
        }
        a.sent = 0.0;
        a.seen = 0.0;
    }
    const double err = c124_wrap(want - now_deg);
    const double target = c124_clamp(err * kC124RatePerDeg, -g_c124_cap, g_c124_cap);
    const double step = kC124Accel * dt;
    a.rate += c124_clamp(target - a.rate, -step, step);
    double delta = a.rate * dt;
    if (fabs(delta) > fabs(err)) {
        delta = err; // never past the target
    }
    const double v = delta / a.gain;
    a.sending = fabs(v) >= 0.001;
    a.sent += a.sending ? v : 0.0;
    return a.sending ? static_cast<float>(v) : 0.0f;
}

// The view rotation the mouse moves: the control rotation, or on a mounted turret (card 125) the view point's, since
// a turret's look input turns its Camera component and leaves the control rotation alone.
Vec3 c124_view_rot(void* pc) {
    Vec3 rot = read_vec3(pc, kControllerControlRotation); // pitch, yaw, roll
    Vec3 loc{};
    if (is_a(read_ptr(pc, kControllerPawn), g_turret_class()) && g_player_view_point != nullptr) {
        g_player_view_point(pc, &loc, &rot);
    }
    return rot;
}

// One tick of a turn toward `want_yaw` (and `want_pitch`, unless NAN) through MouseX/MouseY axis events. Returns
// the yaw angle still to turn (degrees, before this tick's event lands).
double c124_turn(void* pc, double want_yaw, double want_pitch, ULONGLONG now) {
    const Vec3 rot = c124_view_rot(pc);
    const bool fresh = g_c124.last_ms == 0;
    const ULONGLONG ms = fresh ? 1 : (now - g_c124.last_ms < 1 ? 1 : (now - g_c124.last_ms > kC124MaxDtMs ? kC124MaxDtMs : now - g_c124.last_ms));
    g_c124.last_ms = now;
    if (fresh) {
        g_c124.yaw.rate = 0.0;
        g_c124.pitch.rate = 0.0;
        g_c124.yaw.sending = false;
        g_c124.pitch.sending = false;
    }
    const double dt = static_cast<double>(ms) / 1000.0;
    const float vx = c124_axis_step(g_c124.yaw, rot.y, want_yaw, dt, fresh);
    const float vy = isnan(want_pitch) ? 0.0f : c124_axis_step(g_c124.pitch, rot.x, want_pitch, dt, fresh);
    if (vx != 0.0f && autoinput::axis_event(pc, L"MouseX", vx)) {
        ++g_c124.events;
    }
    if (vy != 0.0f && autoinput::axis_event(pc, L"MouseY", vy)) {
        ++g_c124.events;
    }
    const double r = fabs(g_c124.yaw.rate) > fabs(g_c124.pitch.rate) || isnan(want_pitch) ? fabs(g_c124.yaw.rate)
                                                                                             : fabs(g_c124.pitch.rate);
    g_c124.peak = r > g_c124.peak ? r : g_c124.peak;
    return c124_wrap(want_yaw - rot.y);
}

void c124_reset_counts() {
    g_c124.last_ms = 0;
    g_c124.events = 0;
    g_c124.peak = 0.0;
    g_c124.rot_writes = 0;
}

// A `look ... smooth` is turning the view: the walk leaves the mouse alone this tick.
bool c124_look_running() {
    return g_c63.count > 0 && g_c63.started && g_c63.q[0].op == cmdfile::Op::Look && g_c63.q[0].smooth;
}

// Releases every key a walk can hold; safe to call with none down.
void c64_keys_up(void* pc) {
    const wchar_t* const keys[] = {L"W", L"S", L"A", L"D", L"SpaceBar", L"LeftShift"};
    for (const wchar_t* k : keys) {
        autoinput::key_event(pc, k, false);
    }
    g_c64.w_down = false;
    g_c64.jump_up = 0;
}

// The goal of `c`: its point, or the actor's location. False (*why set) when no such actor. The walker's own pawn is
// skipped, as look and warp do (card 319 run 1: `host goto actor BP_PlayerCharacterMI_C` resolved to the host's own
// pawn, walked 0 uu and left the client out of revive range).
bool c64_goal(void* world, const cmdfile::Cmd& c, const void* pawn, const Vec3& from, const char** why) {
    if (c.actor[0] == '\0') {
        g_c64.goal = Vec3{c.at[0], c.at[1], c.at[2]};
        return true;
    }
    void* const actor = c64_find_actor(world, c.actor, from, pawn, c.live);
    if (actor == nullptr) {
        *why = "no such actor";
        return false;
    }
    g_c64.goal = actor_location(actor);
    log::line("cmd: %u goto: actor %s resolved to %s at %.0f %.0f %.0f", c.seq, c.actor,
              prop_name(static_cast<const uint8_t*>(actor) + kObjectName, 8).text, g_c64.goal.x, g_c64.goal.y, g_c64.goal.z);
    return true;
}

// Starts a goto in the background. Null when under way, else the failure reason (nothing held).
const char* c64_goto_start(void* world, void* pc, const cmdfile::Cmd& c, ULONGLONG now) {
    void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    if (world == nullptr || pawn == nullptr) {
        return "no local pawn";
    }
    if (g_c64.nav_world != world) {
        c64_log_nav(world, pawn);
    }
    const Vec3 from = actor_location(pawn);
    const char* why = nullptr;
    if (!c64_goal(world, c, pawn, from, &why)) {
        return why;
    }
    g_c64.radius = c.radius;
    g_c64.seq = c.seq; // the plan's rows name this command
    why = c64_plan(world, pawn);
    if (why != nullptr) {
        return why;
    }
    const float walk = c64_float_field(c64_movement(pawn), "MaxWalkSpeed");
    g_c64.speed = walk > kC64MinSpeed ? walk : kC64MinSpeed;
    const ULONGLONG t = static_cast<ULONGLONG>(g_c64.length / g_c64.speed * 1000.0) + kC64SlackMs;
    const ULONGLONG timeout_ms = t < kC64MaxTimeoutMs ? t : kC64MaxTimeoutMs;
    g_c64.last = from;
    g_c64.start_ms = now;
    g_c64.until = now + timeout_ms;
    g_c64.next_check = now + kC64CheckMs;
    g_c64.phase = C64Phase::Walk;
    g_c64.landing = false;
    g_c64.still_ticks = 0;
    g_c64.phase_until = 0;
    g_c64.stucks = 0;
    g_c64.trying = nullptr;
    g_c64.sprint = c.sprint;
    g_c64.seq = c.seq;
    memcpy(g_c64.text, c.text, sizeof(g_c64.text));
    g_c64.dist = c64_dist2d(from, g_c64.goal);
    c124_reset_counts();
    log::line("cmd: %u goto: from %.0f %.0f %.0f to %.0f %.0f %.0f, %s of %d points, %.0f uu, walk %.0f uu/s, "
              "timeout %llu s%s", c.seq, from.x, from.y, from.z, g_c64.goal.x, g_c64.goal.y, g_c64.goal.z,
              g_c64.source, g_c64.npts, g_c64.length, g_c64.speed, static_cast<unsigned long long>(timeout_ms / 1000),
              c.sprint ? ", sprint" : "");
    if (g_c64.nav_gap > kC64NavGapUu) {
        log::line("cmd: %u goto: the goal is %.0f uu (2D) past the navmesh path's end", c.seq, g_c64.nav_gap);
    }
    if (!autoinput::key_event(pc, L"W", true)) {
        return "no local player controller";
    }
    g_c64.w_down = true;
    g_c64.running = true;
    if (c.sprint) {
        autoinput::key_event(pc, L"LeftShift", true);
    }
    return nullptr;
}

void c63_result(uint32_t seq, const char* text, const char* why);

// Ends the walk: keys up, the end line, the steering line, then the goto command's result line.
void c64_end(void* pc, const char* why, ULONGLONG now) {
    c64_keys_up(pc);
    g_c64.running = false;
    log::line("cmd: %u goto: end (%s): dist %.0f, path %.0f uu, %.1f s, %d stuck(s)", g_c64.seq,
              why != nullptr ? why : "reached", g_c64.dist, g_c64.length,
              static_cast<double>(now - g_c64.start_ms) / 1000.0, g_c64.stucks);
    log::line("cmd: %u goto: steering: %d mouse axis event(s), peak turn rate %.0f deg/s (cap %.0f), MouseX gain "
              "%.3f deg/unit, control rotation writes %d", g_c64.seq, g_c124.events, g_c124.peak, kC124MaxRate,
              g_c124.yaw.gain, g_c124.rot_writes);
    if (g_c64.seq != 0) { // card 126: an autoplay walk (seq 0) has no command waiting for a result
        c63_result(g_c64.seq, g_c64.text, why);
    }
}

bool c64_walking() {
    return g_c64.running;
}

// Forward trace length to a hit at `dz` over the pawn's centre along `f`, or -1 when clear.
double c64_probe(const void* pawn, const Vec3& pos, const Vec3& f, double dz, uint64_t profile) {
    const Vec3 a{pos.x, pos.y, pos.z + dz};
    const Vec3 b{a.x + f.x * kC64ProbeUu, a.y + f.y * kC64ProbeUu, a.z};
    const double d = vec_dist(a, trace_profile(pawn, a, b, profile));
    return d < kC64ProbeUu - 0.5 ? d : -1.0;
}

// The give-up line: where, facing what, and what the Pawn-profile traces in front hit.
void c64_blocked(void* pc, const void* pawn, const Vec3& pos, const char* why) {
    const double y = read_vec3(pc, kControllerControlRotation).y / kRadToDeg;
    const Vec3 f{cos(y), sin(y), 0.0};
    const uint64_t prof = fname_cached(g_c64.pawn_profile, L"Pawn");
    const double knee = c64_probe(pawn, pos, f, -45.0, prof);
    const double waist = c64_probe(pawn, pos, f, 0.0, prof);
    const double head = c64_probe(pawn, pos, f, 60.0, prof);
    const Vec3 a{pos.x + f.x * 100.0, pos.y + f.y * 100.0, pos.z};
    const Vec3 b{a.x, a.y, a.z - 400.0};
    const Vec3 floor = trace_profile(pawn, a, b, prof);
    const double drop = vec_dist(a, floor) < 399.5 ? a.z - floor.z : -1.0;
    const bool off_mesh = g_c64.at >= g_c64.npts - 1 && g_c64.nav_gap > g_c64.radius;
    const char* const what = off_mesh                                      ? "the goal is off the navmesh"
                             : knee >= 0.0 && waist >= 0.0 && head >= 0.0 ? "a wall"
                             : knee >= 0.0 && head < 0.0                   ? "a low obstacle (knee hit, head clear)"
                             : knee >= 0.0 || waist >= 0.0 || head >= 0.0 ? "a partial blocker"
                             : drop < 0.0                                  ? "a ledge (no floor 100 uu ahead)"
                                                                           : "nothing in the forward traces";
    log::line("WARN cmd: %u goto: gave up (%s) at %.0f %.0f %.0f, yaw %.0f, point %d/%d, goal %.0f uu past the "
              "navmesh end: %s; Pawn traces %.0f uu ahead: knee %.0f, waist %.0f, head %.0f (-1 clear), floor drop "
              "100 uu ahead %.0f", g_c64.seq, why, pos.x, pos.y, pos.z, y * kRadToDeg, g_c64.at + 1, g_c64.npts,
              g_c64.nav_gap, what, kC64ProbeUu, knee, waist, head, drop);
}

constexpr const char* kC64RecoveryNames[kC64Recoveries] = {"re-path", "jump", "side step"};

// The 2 s check: a progress line; a recovery that got the pawn moving again is named; a stuck (under kC64StuckUu
// travelled while walking) starts the next recovery. Returns "stuck" after kC64MaxStucks, else null.
const char* c64_check(void* world, void* pc, void* pawn, const Vec3& pos, ULONGLONG now) {
    const double moved = c64_dist2d(pos, g_c64.last);
    log::line("cmd: %u goto: at %.0f %.0f %.0f, dist %.0f, point %d/%d, moved %.0f, %.1f s", g_c64.seq, pos.x, pos.y,
              pos.z, g_c64.dist, g_c64.at + 1, g_c64.npts, moved, static_cast<double>(now - g_c64.start_ms) / 1000.0);
    g_c64.last = pos;
    g_c64.next_check = now + kC64CheckMs;
    if (g_c64.phase != C64Phase::Walk) {
        return nullptr;
    }
    if (moved >= kC64StuckUu) {
        if (g_c64.trying != nullptr) {
            log::line("cmd: %u goto: freed by %s (moved %.0f)", g_c64.seq, g_c64.trying, moved);
            g_c64.trying = nullptr;
        }
        return nullptr;
    }
    if (g_c64.trying != nullptr) {
        log::line("cmd: %u goto: %s did not free it", g_c64.seq, g_c64.trying);
    }
    if (g_c64.stucks >= kC64MaxStucks) {
        return "stuck";
    }
    // b16 (b15 cmd 51: a 2008 uu path ending 190 uu short of a satchel below a ledge, six recoveries for 18.8 s): on
    // the leg past the navmesh path's end, with the goal farther than the radius beyond it, no recovery makes a path.
    if (g_c64.at == g_c64.npts - 1 && g_c64.nav_gap > g_c64.radius) {
        return "stuck on the leg past the navmesh path's end";
    }
    ++g_c64.stucks;
    const int32_t r = (g_c64.stucks - 1) % kC64Recoveries;
    g_c64.trying = kC64RecoveryNames[r];
    log::line("cmd: %u goto: stuck %d of %d at %.0f %.0f %.0f, point %d/%d, trying %s", g_c64.seq, g_c64.stucks,
              kC64MaxStucks, pos.x, pos.y, pos.z, g_c64.at + 1, g_c64.npts, g_c64.trying);
    if (r == 0) {
        c64_replan(world, pawn, "from the stuck spot");
    } else if (r == 1) {
        autoinput::key_event(pc, L"SpaceBar", true); // W stays down: a running jump
        g_c64.jump_up = now + kC64JumpTapMs;
    } else {
        autoinput::key_event(pc, L"W", false);
        autoinput::key_event(pc, L"S", true);
        g_c64.w_down = false;
        g_c64.phase = C64Phase::Back;
        g_c64.phase_until = now + kC64BackMs;
        g_c64.next_check = now + kC64BackMs + kC64SideMs + kC64CheckMs; // the next window starts walking
    }
    return nullptr;
}

// The side step's key changes: S back, then A (first round) or D (second), then W and a fresh path.
void c64_phase_step(void* world, void* pc, void* pawn, ULONGLONG now) {
    if (g_c64.phase == C64Phase::Back && now >= g_c64.phase_until) {
        autoinput::key_event(pc, L"S", false);
        autoinput::key_event(pc, g_c64.stucks > kC64Recoveries ? L"D" : L"A", true);
        g_c64.phase = C64Phase::Side;
        g_c64.phase_until = now + kC64SideMs;
    } else if (g_c64.phase == C64Phase::Side && now >= g_c64.phase_until) {
        autoinput::key_event(pc, L"A", false);
        autoinput::key_event(pc, L"D", false);
        g_c64.w_down = autoinput::key_event(pc, L"W", true);
        g_c64.phase = C64Phase::Walk;
        c64_replan(world, pawn, "after the side step");
    }
}

// This tick's keys and mouse: the jump release, the side step, then the view toward the next path point with W
// let go on a sharp turn.
void c64_steer(void* world, void* pc, void* pawn, const Vec3& pos, ULONGLONG now) {
    if (g_c64.jump_up != 0 && now >= g_c64.jump_up) {
        autoinput::key_event(pc, L"SpaceBar", false);
        g_c64.jump_up = 0;
    }
    c64_phase_step(world, pc, pawn, now);
    for (int32_t i = 0; i < kC64MaxPoints && g_c64.at < g_c64.npts - 1; ++i) {
        if (c64_dist2d(pos, g_c64.pts[g_c64.at]) >= kC64PointUu) {
            break;
        }
        ++g_c64.at;
    }
    if (g_c64.phase != C64Phase::Walk || c124_look_running()) {
        if (g_c64.phase != C64Phase::Walk) {
            g_c124.last_ms = 0; // the turn starts fresh after the side step
        }
        return;
    }
    const Vec3& to = g_c64.pts[g_c64.at];
    g_c124_cap = c64_dist2d(pos, to) < kB16SlowUu ? kB16SlowRate : kC124MaxRate; // b16
    const double err = fabs(c124_turn(pc, atan2(to.y - pos.y, to.x - pos.x) * kRadToDeg, NAN, now));
    g_c124_cap = kC124MaxRate;
    const bool close_in = c64_dist2d(pos, to) < kC64NearUu; // close: aim first, or the turn circle orbits the point
    if (g_c64.w_down && err > (close_in ? kC64NearOffDeg : kC64WalkOffDeg)) {
        autoinput::key_event(pc, L"W", false);
        g_c64.w_down = false;
    } else if (!g_c64.w_down && err < (close_in ? kC64NearOnDeg : kC64WalkOnDeg)) {
        g_c64.w_down = autoinput::key_event(pc, L"W", true);
    }
}

// One tick of the running goto (game thread, every tick while it runs, whatever the queue is doing).
void c64_walk_tick(void* world, void* pc, ULONGLONG now) {
    void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    if (pawn == nullptr) {
        c64_end(pc, "no local pawn", now);
        return;
    }
    const Vec3 pos = actor_location(pawn);
    g_c64.dist = c64_dist2d(pos, g_c64.goal);
    // Reached: within the radius in 2D, at the goal's height or at the floor the navmesh gives under it (c124d: a
    // BP_PlayerPresence_C root 379 uu above the walkable floor).
    const bool level = fabs(pos.z - g_c64.goal.z) <= kC64GoalDz || fabs(pos.z - g_c64.floor_z) <= kC64GoalDz;
    // A walk that reaches the goal ends only when the pawn stands still on the ground, so the next command starts
    // from a standing eye at a fixed place. Two voids: c335-range-load on b23, line 260 (reached mid-jump, the look
    // began 117 ms later with the eye 150 uu high, 0 of 26 points hit) and on b24, line 191 (the pawn still slid at
    // about 420 uu/s after the end, the look's focus was gone at the E press 270 ms later). Bounded by the timeout.
    const auto* const cmc = static_cast<const uint8_t*>(read_ptr(pawn, kCharacterMovement));
    const bool falling = cmc != nullptr && cmc[kCmcMovementMode] == kC64MoveFalling;
    if (g_c64.landing || (g_c64.dist <= g_c64.radius && level)) {
        if (g_c64.trying != nullptr) {
            log::line("cmd: %u goto: freed by %s (reached the goal)", g_c64.seq, g_c64.trying);
            g_c64.trying = nullptr;
        }
        if (!g_c64.landing) {
            g_c64.landing = true;
            g_c64.still_ticks = 0;
            g_c64.settle_at = pos;
            c64_keys_up(pc);
            if (falling) {
                log::line("cmd: %u goto: reached in the air at %.0f %.0f %.0f, waiting for the landing", g_c64.seq,
                          pos.x, pos.y, pos.z);
            }
            return;
        }
        const bool still = !falling && vec_dist(pos, g_c64.settle_at) <= kC64StillUu;
        g_c64.still_ticks = still ? static_cast<uint8_t>(g_c64.still_ticks + 1) : 0;
        g_c64.settle_at = pos;
        if (g_c64.still_ticks >= kC64StillTicks) {
            c64_end(pc, nullptr, now);
        } else if (now >= g_c64.until) {
            log::line("WARN cmd: %u goto: reached the goal but not standing at the timeout (falling %d, dist %.0f)",
                      g_c64.seq, falling ? 1 : 0, g_c64.dist);
            c64_end(pc, "not standing at the timeout", now);
        }
        return;
    }
    if (g_c43.view_down) { // card 043: a downed pawn cannot walk (walk speed 0); no recovery can free it
        log::line("WARN cmd: %u goto: the pawn is downed at %.0f %.0f %.0f (card 043 downed view, walk speed 0)",
                  g_c64.seq, pos.x, pos.y, pos.z);
        c64_end(pc, "downed", now);
        return;
    }
    const char* const why = now >= g_c64.until ? "timeout" : (now >= g_c64.next_check ? c64_check(world, pc, pawn, pos, now) : nullptr);
    if (why != nullptr) {
        c64_blocked(pc, pawn, pos, why);
        c64_end(pc, why, now);
        return;
    }
    c64_steer(world, pc, pawn, pos, now);
}

Vec3 c423_near_sample(const void* pawn, const void* actor, const Vec3& centre, const Vec3& extent, uint32_t seq);

// b232 (user: "grenades to destroy bug hole need to be aimed at yellow center"): the world location of the owned
// component of `actor` whose class name has "GrenadeHole" (USTHitVolumeGrenadeHoleComponent: the blast volume a
// nest dies to, card 241) nearest `from`, in *at. One `cmd: N look: hole` row names it, or none was found.
constexpr int32_t kC232HoleOwned = 128; // components read per actor
// b233: c244b232a matched 0 of a nest's 22 components on the class name alone; card 299 names the blast volumes
// STGrenadeDamageVolume* (object name), so either name marks a hole.
bool c64_is_hole(const void* comp) {
    return strstr(class_name(comp).text, "GrenadeHole") != nullptr ||
           strstr(object_name(comp).text, "GrenadeDamageVolume") != nullptr;
}
bool c64_hole_point(const void* actor, const Vec3& from, uint32_t seq, Vec3* at) {
    static void* const scene = find_class(L"/Script/Engine.SceneComponent");
    const void* comps[kC232HoleOwned] = {};
    const int32_t n = owned_components(actor, comps, kC232HoleOwned);
    const void* best = nullptr;
    int32_t holes = 0;
    for (int32_t i = 0; i < n; ++i) {
        if (scene == nullptr || !is_a(comps[i], scene) || !c64_is_hole(comps[i])) {
            continue;
        }
        ++holes;
        const Vec3 p = read_vec3(comps[i], kComponentTranslation);
        if (best == nullptr || vec_dist(p, from) < vec_dist(*at, from)) {
            best = comps[i];
            *at = p;
        }
    }
    if (best == nullptr) {
        log::line("WARN cmd: %u look: hole: %s has no grenade hole component (%d owned, scene class %d)", seq,
                  object_name(actor).text, n, scene != nullptr ? 1 : 0);
        static bool listed = false; // the owned list once per process
        for (int32_t i = 0; !listed && i < n; ++i) {
            log::line("WARN cmd: %u look: hole: owned %d: %s (%s)", seq, i, object_name(comps[i]).text,
                      class_name(comps[i]).text);
        }
        listed = true;
        return false;
    }
    log::line("cmd: %u look: hole %s (%s) at %.0f %.0f %.0f, nearest of %d", seq, object_name(best).text,
              class_name(best).text, at->x, at->y, at->z, holes);
    return true;
}

// `look <yaw> <pitch>`, `look actor <name> [dz]` (from the view point to the actor's location, z + dz) or
// `look point <x> <y> <z>`. Null when done; with `smooth` (card 124) it only sets g_c124's target, and
// c124_look_step turns there.
const char* c64_look(void* world, void* pc, const cmdfile::Cmd& c) {
    void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    if (pawn == nullptr || world == nullptr) {
        return "no local pawn";
    }
    double yaw = c.at[0];
    double pitch = c.at[1];
    if (c.actor[0] != '\0' || c.point) {
        ViewPoint view{};
        if (!c24_view_point(pawn, view)) {
            view.loc = actor_location(pawn);
        }
        void* const actor = c.point ? nullptr : c64_find_actor(world, c.actor, view.loc, pawn, c.live);
        if (!c.point && actor == nullptr) {
            return "no such actor";
        }
        Vec3 at = c.point ? Vec3{c.at[0], c.at[1], c.at[2]} : actor_location(actor);
        if (!c.point && c.bounds) { // card 131: the bounds centre (a nest's mesh sits far from its origin)
            Vec3 extent{};
            if (g_actor_bounds == nullptr) {
                return "no GetActorBounds";
            }
            g_actor_bounds(actor, false, &at, &extent, false);
            log::line("cmd: %u look: %s bounds centre %.0f %.0f %.0f, extent %.0f %.0f %.0f", c.seq, object_name(actor).text,
                      at.x, at.y, at.z, extent.x, extent.y, extent.z);
            at = c423_near_sample(pawn, actor, at, extent, c.seq);
        }
        if (!c.point && c.hole && !c64_hole_point(actor, view.loc, c.seq, &at)) {
            return "no grenade hole component";
        }
        at.z += c.point ? 0.0 : c.at[2];
        yaw = atan2(at.y - view.loc.y, at.x - view.loc.x) * kRadToDeg;
        const double raw = atan2(at.z - view.loc.z, c64_dist2d(at, view.loc)) * kRadToDeg;
        pitch = raw > 89.0 ? 89.0 : (raw < -89.0 ? -89.0 : raw);
        log::line("cmd: %u look: %s at %.0f %.0f %.0f from %.0f %.0f %.0f: yaw %.1f pitch %.1f, %.0f uu", c.seq,
                  c.point ? "point" : object_name(actor).text, at.x, at.y, at.z, view.loc.x, view.loc.y, view.loc.z, yaw,
                  pitch, vec_dist(at, view.loc));
    }
    if (!c.smooth) {
        c64_set_yaw(pc, yaw, pitch);
        return nullptr;
    }
    g_c124.look_yaw = yaw;
    g_c124.look_pitch = pitch;
    g_c124.last_ms = 0;
    if (!g_c64.running) {
        c124_reset_counts();
    }
    return nullptr;
}

// One tick of `look ... smooth`: true when it ended (*why null: within kC124DoneDeg on both axes).
bool c124_look_step(void* pc, ULONGLONG now, ULONGLONG until, const char** why) {
    *why = nullptr;
    if (pc == nullptr) {
        *why = "no local player controller";
        return true;
    }
    const Vec3 rot = c124_view_rot(pc);
    const double ey = c124_wrap(g_c124.look_yaw - rot.y);
    const double ep = c124_wrap(g_c124.look_pitch - rot.x);
    if (fabs(ey) <= kC124DoneDeg && fabs(ep) <= kC124DoneDeg) {
        log::line("cmd: %u look: smooth turn done: %d mouse axis event(s) so far, peak %.0f deg/s, gain MouseX %.3f "
                  "MouseY %.3f deg/unit", g_c63.q[0].seq, g_c124.events, g_c124.peak, g_c124.yaw.gain, g_c124.pitch.gain);
        return true;
    }
    if (now >= until) {
        *why = "smooth turn not within 1 degree in 3 s";
        log::line("WARN cmd: %u look: yaw %.1f off, pitch %.1f off at the timeout", g_c63.q[0].seq, ey, ep);
        return true;
    }
    c124_turn(pc, g_c124.look_yaw, g_c124.look_pitch, now);
    return false;
}

// ---- Card 264: the crosshair trace after every look ----
// The placed name with its instance number (BP_Switch_C_2; object_name prints BP_Switch_C), "null" for none.
ObjectName c264_name(const void* o) {
    return o != nullptr ? prop_name(static_cast<const uint8_t*>(o) + kObjectName, 8) : ObjectName{"null"};
}

// One Visibility-channel trace from the view point along the control rotation (the crosshair line), 10000 uu,
// ignoring the own pawn: what the player sees (c264-c265 probe: the BlockAll profile stopped on invisible
// BlockingVolumes and nameless actors in front of visible targets). Card 265 uses the same trace to see a bug.
constexpr double kC264TraceUu = 10000.0;
constexpr ULONGLONG kC264SettleMs = 250; // a snap look: the interactor's focus is its next TickComponent's

// The hit actor from `from` to `to` (null: no hit, or a component without an owner); *at_out the hit point or the
// trace's end; *comp_out (optional) the hit component.
const void* c264_trace(const void* pawn, const Vec3& from, const Vec3& to, Vec3* at_out,
                       const void** comp_out = nullptr) {
    const void* actor = nullptr;
    *at_out = trace_channel(pawn, from, to, kEccVisibility, &actor, comp_out);
    return actor;
}

// The view point (the pawn's location without a camera).
Vec3 c264_eye(const void* pawn) {
    ViewPoint view{};
    return c24_view_point(pawn, view) ? view.loc : actor_location(pawn);
}

// The crosshair line's end: kC264TraceUu from `eye` along the controller's view rotation.
Vec3 c264_view_end(void* pc, const Vec3& eye) {
    const Vec3 rot = c124_view_rot(pc);
    const double p = rot.x / kRadToDeg;
    const double y = rot.y / kRadToDeg;
    return Vec3{eye.x + cos(p) * cos(y) * kC264TraceUu, eye.y + cos(p) * sin(y) * kC264TraceUu,
                eye.z + sin(p) * kC264TraceUu};
}

// Card 316: the bullets leave from the muzzle, not the camera (run 3: the camera saw the target over a rise of the
// ground the muzzle shot into). The equipped weapon's muzzle (USTWeaponUtilityLibrary::GetMuzzleTransform, the fire
// ability's origin before the zero-threshold adjustment); false without a weapon.
bool c316_muzzle(const void* pawn, Vec3* out) {
    const void* const weapons = is_a(pawn, g_player_mi_class()) ? read_ptr(pawn, kMiWeaponInventory) : nullptr;
    const void* const weapon = weapons != nullptr ? g_get_equipped_weapon(weapons) : nullptr;
    if (weapon == nullptr || g_get_muzzle_transform == nullptr) {
        return false;
    }
    alignas(16) double transform[12] = {}; // FTransform<double>: quat, translation, scale (each padded to 32 bytes)
    g_get_muzzle_transform(transform, weapon);
    *out = read_vec3(transform, kTransformTranslation);
    return true;
}

constexpr double kC316PastUu = 20.0;  // the muzzle line ends this far past the camera's hit point (reaches the surface)
constexpr double kC316SlackUu = 30.0; // a first hit this close to the camera's hit point counts as the same surface

// Card 316: after a camera hit on `target` at `at`, one Visibility trace from the muzzle through `at`:
// `cmd: N aim: muzzle at X Y Z, line clear to <target>, D uu` or `cmd: N aim: muzzle at X Y Z, line blocked by
// <actor> at D uu` (D from the muzzle); `cmd: N aim: muzzle none` without an equipped weapon. `label` names the row
// ("muzzle"; card 310 range: "centre muzzle", the line to the looked-at actor's bounds centre).
void c316_muzzle_row(const void* pawn, uint32_t seq, const void* target, const Vec3& at,
                     const char* label = "muzzle") {
    Vec3 muzzle{};
    if (!c316_muzzle(pawn, &muzzle)) {
        log::line("cmd: %u aim: %s none", seq, label);
        return;
    }
    const double len = vec_dist(muzzle, at);
    const double k = len > 1.0 ? (len + kC316PastUu) / len : 1.0;
    const Vec3 end{muzzle.x + (at.x - muzzle.x) * k, muzzle.y + (at.y - muzzle.y) * k, muzzle.z + (at.z - muzzle.z) * k};
    Vec3 hit{};
    const void* comp = nullptr;
    const void* const actor = c264_trace(pawn, muzzle, end, &hit, &comp);
    const double d = vec_dist(muzzle, hit);
    if ((actor != nullptr && actor == target) || d >= len - kC316SlackUu) {
        log::line("cmd: %u aim: %s at %.0f %.0f %.0f, line clear to %s, %.0f uu", seq, label, muzzle.x, muzzle.y,
                  muzzle.z, target != nullptr ? c264_name(target).text : "(no owner)", len);
        return;
    }
    const ObjectName who = actor != nullptr ? c264_name(actor) : comp != nullptr ? c264_name(comp) : ObjectName{"null"};
    log::line("cmd: %u aim: %s at %.0f %.0f %.0f, line blocked by %s at %.0f uu", seq, label, muzzle.x, muzzle.y,
              muzzle.z, who.text, d);
}

// The aim rows: `cmd: N aim: hit <actor> (<class>) guid 0x.. at X Y Z, D uu, pitch P` (a component without an owner:
// `hit <component> (no owner)`, a freed one `hit (no component)`) or `no hit within 10000 uu, pitch P`;
// then (after a hit) the card 316 muzzle row, then `cmd: N aim: focus <actor>|none`, the interactable the pawn's interactor focuses (what the E prompt offers).
void c264_aim_rows(void* pc, uint32_t seq) {
    const void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    if (pawn == nullptr) {
        log::line("WARN cmd: %u aim: no local pawn", seq);
        return;
    }
    const Vec3 eye = c264_eye(pawn);
    const Vec3 rot = c124_view_rot(pc);
    const Vec3 end = c264_view_end(pc, eye);
    Vec3 at{};
    const void* comp = nullptr;
    const void* const actor = c264_trace(pawn, eye, end, &at, &comp);
    const double d = vec_dist(eye, at);
    if (d >= kC264TraceUu - 0.5) {
        log::line("cmd: %u aim: no hit within %.0f uu, pitch %.1f", seq, kC264TraceUu, c124_wrap(rot.x));
    } else if (actor != nullptr) {
        log::line("cmd: %u aim: hit %s (%s) guid 0x%llx at %.0f %.0f %.0f, %.0f uu, pitch %.1f", seq,
                  c264_name(actor).text, class_name(actor).text, static_cast<unsigned long long>(c41_guid(actor)),
                  at.x, at.y, at.z, d, c124_wrap(rot.x));
    } else {
        log::line("cmd: %u aim: hit %s (%s) at %.0f %.0f %.0f, %.0f uu, pitch %.1f", seq,
                  comp != nullptr ? c264_name(comp).text : "(no component)", comp != nullptr ? "no owner" : "freed",
                  at.x, at.y, at.z, d, c124_wrap(rot.x));
    }
    if (d < kC264TraceUu - 0.5) {
        c316_muzzle_row(pawn, seq, actor, at); // card 316
    }
    const void* const interactor = read_ptr(pawn, kCharacterInteractor);
    const void* const focus = interact_target(interactor);
    log::line("cmd: %u aim: focus %s", seq, focus != nullptr ? c264_name(focus).text : "none");
}

// Card 310 range (the drive's line= gate): at the end of a `look actor <name> [dz] bounds`, the card 316 muzzle
// line to the actor's bounds centre (+ dz), found again by name (the look's actor may be gone):
// `cmd: N aim: centre muzzle at X Y Z, line clear to <actor>, D uu` or `... line blocked by <actor> at D uu`.
// The camera's Visibility hit is not needed: the bullets leave from the muzzle (card 316), and a target whose
// Visibility response differs from the weapon trace's still shows here as clear.
// ---- c310 range: KillableBuilding collision row, both sides (senior batch 1 item 3) ----
// One row per primitive component of the target: collision enabled, object type, and the response to all 32
// channels as a digit string (0 ignore, 1 overlap, 2 block; index = ECollisionChannel), since the weapon's trace
// channel is not known. The client row comes at the end of each range look, the host row at each relayed Activate.
// A client copy whose collision differs from the host's is a mod bug.
// AActor::OwnedComponents is a TSet<UActorComponent*> at actor+0x230 (ForEachComponent_Internal<UActorComponent>,
// exe+0x301BC40: `add $0x230,%r15`): the sparse array data at +0x0, its num at +0x8, the allocation bits inline at
// +0x10 (heap pointer at +0x20 when set), the bit count at +0x28; each element is 16 bytes, the pointer first.
constexpr size_t kActorOwnedComponents = 0x230;
constexpr size_t kSetNum = 0x8;
constexpr size_t kSetBitsInline = 0x10;
constexpr size_t kSetBitsHeap = 0x20;
constexpr size_t kSetNumBits = 0x28;
constexpr size_t kSetElement = 16;
constexpr int32_t kC310OwnedMax = 256;     // components walked per actor
constexpr int32_t kC310Channels = 32;      // ECollisionChannel values 0..31
constexpr uint32_t kC310CollisionRows = 64; // rows per side per map
uint32_t g_c310_collision_rows = 0;

void c310_collision_map_end() {
    g_c310_collision_rows = 0;
}

void c310_collision_row(const void* actor, const char* stage) {
    if (actor == nullptr || !note_event_cap(g_c310_collision_rows, kC310CollisionRows, "c310 collision")) {
        return;
    }
    static void* primitive = find_class(L"/Script/Engine.PrimitiveComponent");
    const auto* const set = static_cast<const uint8_t*>(actor) + kActorOwnedComponents;
    const auto* const data = *reinterpret_cast<const uint8_t* const*>(set);
    const int32_t num = *reinterpret_cast<const int32_t*>(set + kSetNum);
    const int32_t num_bits = *reinterpret_cast<const int32_t*>(set + kSetNumBits);
    const auto* const heap = *reinterpret_cast<const uint32_t* const*>(set + kSetBitsHeap);
    const auto* const bits = heap != nullptr ? heap : reinterpret_cast<const uint32_t*>(set + kSetBitsInline);
    const char* const side = g_client_session ? "client" : "host";
    int32_t prims = 0;
    for (int32_t i = 0; data != nullptr && i < num && i < num_bits && i < kC310OwnedMax; ++i) {
        const void* const comp = (bits[i >> 5] >> (i & 31)) & 1U
                                     ? *reinterpret_cast<void* const*>(data + static_cast<size_t>(i) * kSetElement)
                                     : nullptr;
        if (comp == nullptr || primitive == nullptr || !is_a(comp, primitive)) {
            continue;
        }
        ++prims;
        char resp[kC310Channels + 1];
        for (int32_t ch = 0; ch < kC310Channels; ++ch) {
            const int32_t r = g_c319_response != nullptr ? g_c319_response(comp, ch) : -1;
            resp[ch] = r >= 0 && r <= 2 ? static_cast<char>('0' + r) : '?';
        }
        resp[kC310Channels] = '\0';
        log::line("c310: %s collision %s %s (%s) %s (%s): enabled %d object type %d responses %s",
                  side, stage, object_name(actor).text, class_name(actor).text, object_name(comp).text,
                  class_name(comp).text, g_collision_enabled != nullptr ? g_collision_enabled(comp) : -1,
                  g_collision_type != nullptr ? g_collision_type(comp) : -1, resp);
    }
    log::line("c310: %s collision %s %s: %d primitive of %d owned components", side, stage, object_name(actor).text,
              prims, num);
}

void c310_centre_row(void* world, void* pc, const cmdfile::Cmd& c) {
    if (c.point || !c.bounds || c.actor[0] == '\0') {
        return;
    }
    const void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    void* const actor =
        pawn != nullptr && world != nullptr ? c64_find_actor(world, c.actor, c264_eye(pawn), pawn, c.live) : nullptr;
    if (actor == nullptr || g_actor_bounds == nullptr) {
        log::line("cmd: %u aim: centre muzzle none (%s)", c.seq, actor == nullptr ? "no such actor" : "no GetActorBounds");
        return;
    }
    Vec3 at{};
    Vec3 extent{};
    g_actor_bounds(actor, false, &at, &extent, false);
    at.z += c.at[2];
    c316_muzzle_row(pawn, c.seq, actor, at, "centre muzzle");
    if (strstr(class_name(actor).text, "KillableBuilding") != nullptr) {
        c310_collision_row(actor, "look");
    }
}

// ---- b14: `look actor <name> ... focus=<prefix>`, the focus scan (senior, b13 run 3 Assault_SO4) ----
// The interactor's TraceForInteractable (exe+0x4A20780) is one line trace from its camera component (+0x108) along
// the forward axis, range +0xF8, trace type +0xFD; the first hit must be the interactable. A switch's interact
// collider is smaller than its bounds, so a look at the bounds centre can miss it. The scan tests the bounds centre,
// then a 5x5 grid on the bounds face toward the eye, each by a trace without turning (b16, b14_trace_points), turns
// once to the centroid of the points whose line hit the actor, and reads the own interactor's focus kB14WaitFrames
// ticks later (the first hit point when the centroid misses, the centre when none hit). Then the card 264 aim rows
// from the settled view, so the drive's focus= gate reads them as before. The look's dz and `bounds` words are not
// used by the scan.
constexpr int32_t kB14Grid = 5;
constexpr int32_t kB14Points = 1 + kB14Grid * kB14Grid; // the centre, then the face grid
constexpr double kB14Frac[kB14Grid] = {-0.8, -0.4, 0.0, 0.4, 0.8}; // of the face's half extents, inset from the edges
constexpr int32_t kB14WaitFrames = 2;       // ticks between a snap look and the focus read
constexpr ULONGLONG kB14CapMs = 10000;      // the whole scan; at 60 fps it takes about 1 s
constexpr int32_t kB14Seen = 8;             // other focus names kept for the `seen:` row
constexpr size_t kInteractorRange = 0xF8;   // float, the trace length (TraceForInteractable)
constexpr size_t kInteractorTraceType = 0xFD; // uint8, the trace type (TraceForInteractable)

enum class B14Phase : uint8_t { Centroid, Final, Sweep, Back };

struct B14Scan {
    Vec3 pts[kB14Points];
    int32_t n;
    int32_t k;            // the point being traced
    int32_t frames;       // ticks since the last snap look
    int32_t hits;
    int32_t first_hit;    // index of the first point that gave the wanted focus, -1 none
    int32_t sweep;        // the point the focus sweep looks at (phase Sweep)
    Vec3 sum;             // of the hit points
    Vec3 chosen;
    const char* how;      // "centroid", "first hit", "centre, no hit"
    B14Phase phase;
    char actor[cmdfile::kActorChars];
    char seen[kB14Seen][cmdfile::kActorChars];
    int32_t nseen;
};
B14Scan g_b14{};

// Snap look from the pawn's eye at `pt`; *yaw_out and *pitch_out the angles written.
void b14_aim(void* pc, const void* pawn, const Vec3& pt, double* yaw_out, double* pitch_out) {
    const Vec3 eye = c264_eye(pawn);
    const double yaw = atan2(pt.y - eye.y, pt.x - eye.x) * kRadToDeg;
    const double raw = atan2(pt.z - eye.z, c64_dist2d(pt, eye)) * kRadToDeg;
    const double pitch = raw > 89.0 ? 89.0 : (raw < -89.0 ? -89.0 : raw);
    c64_set_yaw(pc, yaw, pitch);
    *yaw_out = yaw;
    *pitch_out = pitch;
}

// The centre, then the grid on the bounds face the eye sees most of (the axis with the largest offset in extents).
// b256 (c245b255a line 66, BP_Switch_C_0): the eye stood inside the bounds on every axis, so the x face was 13 uu
// behind it and all 26 sweep looks turned away (focus none); a look from there at panel height (cmd 7, pitch -3.6)
// had focus. When the eye is inside the slab of the chosen axis, the grid is the plane through the centre instead.
int32_t b14_points(const Vec3& centre, const Vec3& extent, const Vec3& eye, Vec3* pts) {
    const double c[3] = {centre.x, centre.y, centre.z};
    const double e[3] = {extent.x, extent.y, extent.z};
    const double d[3] = {eye.x - centre.x, eye.y - centre.y, eye.z - centre.z};
    int32_t axis = 0;
    double best = -1.0;
    for (int32_t i = 0; i < 3; ++i) {
        const double r = fabs(d[i]) / (e[i] > 1.0 ? e[i] : 1.0);
        if (r > best) {
            best = r;
            axis = i;
        }
    }
    const int32_t u = (axis + 1) % 3;
    const int32_t v = (axis + 2) % 3;
    pts[0] = centre;
    int32_t n = 1;
    for (int32_t a = 0; a < kB14Grid; ++a) {
        for (int32_t b = 0; b < kB14Grid; ++b) {
            double p[3] = {};
            const bool inside = fabs(d[axis]) < e[axis];
            p[axis] = inside ? c[axis] : c[axis] + (d[axis] >= 0.0 ? e[axis] : -e[axis]);
            p[u] = c[u] + kB14Frac[a] * e[u];
            p[v] = c[v] + kB14Frac[b] * e[v];
            pts[n++] = Vec3{p[0], p[1], p[2]};
        }
    }
    return n;
}

void b14_note(const void* hit, bool is_target);
void b14_settle(void* pc, const void* pawn, const Vec3& pt, const char* how, B14Phase phase);

// b16 (b15 user report: the scan's 26 snap looks in about 0.9 s read as the player spinning): each point is tested
// by one Visibility trace from the eye, kB16PastUu past the point, without turning the view; a point whose first hit
// is the look's actor is a hit. The view then turns once, to the centroid (and once more to the first hit point
// when the interactor's focus misses there), and the focus read there is the gate as before.
constexpr double kB16PastUu = 30.0;
void b14_trace_points(const void* pawn, const void* actor, const Vec3& eye) {
    for (g_b14.k = 0; g_b14.k < g_b14.n; ++g_b14.k) {
        const Vec3& pt = g_b14.pts[g_b14.k];
        const double len = vec_dist(eye, pt);
        const double f = len > 1.0 ? (len + kB16PastUu) / len : 1.0;
        const Vec3 end{eye.x + (pt.x - eye.x) * f, eye.y + (pt.y - eye.y) * f, eye.z + (pt.z - eye.z) * f};
        Vec3 at{};
        const void* const hit = c264_trace(pawn, eye, end, &at);
        b14_note(hit, hit != nullptr && hit == actor);
    }
}

// c423b138a VOID: the bounds centre of the turning C2M1 nest (BP_SatchelBuilding_Plasmabug_C) moved between runs, so
// the same look aimed at a part 2695 uu away (b137: 1403 uu), the nuke took no destroy event and LOS_O02_SO05a
// stayed open. A `bounds` look aims at the sample (the centre and the b14 face grid) whose Visibility trace from the
// eye first hits the actor nearest the eye, each traced as the b16 scan does; the centre when none hits.
Vec3 c423_near_sample(const void* pawn, const void* actor, const Vec3& centre, const Vec3& extent, uint32_t seq) {
    Vec3 pts[kB14Points];
    const Vec3 eye = c264_eye(pawn);
    const int32_t n = b14_points(centre, extent, eye, pts);
    int32_t best = -1;
    int32_t hits = 0;
    double best_uu = 0.0;
    for (int32_t i = 0; i < n; ++i) {
        const double len = vec_dist(eye, pts[i]);
        const double f = len > 1.0 ? (len + kB16PastUu) / len : 1.0;
        const Vec3 end{eye.x + (pts[i].x - eye.x) * f, eye.y + (pts[i].y - eye.y) * f, eye.z + (pts[i].z - eye.z) * f};
        Vec3 at{};
        const void* const hit = c264_trace(pawn, eye, end, &at);
        if (hit == nullptr || hit != actor) {
            continue;
        }
        ++hits;
        const double uu = vec_dist(eye, at);
        best = best < 0 || uu < best_uu ? i : best;
        best_uu = best == i ? uu : best_uu;
    }
    log::line("cmd: %u look: nearest sample %d (%d of %d hit the actor), hit %.0f uu from the eye", seq,
              static_cast<int>(best), static_cast<int>(hits), static_cast<int>(n), best_uu);
    return best >= 0 ? pts[best] : centre;
}

// The look's actor, its bounds and the points, traced without turning; the one turn to the chosen point. Null when
// started, else why not.
const char* b14_start(void* world, void* pc, const cmdfile::Cmd& c) {
    void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    if (pawn == nullptr || world == nullptr) {
        return "no local pawn";
    }
    if (g_actor_bounds == nullptr) {
        return "no GetActorBounds";
    }
    const Vec3 eye = c264_eye(pawn);
    void* const actor = c64_find_actor(world, c.actor, eye, pawn, c.live);
    if (actor == nullptr) {
        return "no such actor";
    }
    Vec3 centre{};
    Vec3 extent{};
    g_actor_bounds(actor, false, &centre, &extent, false);
    g_b14 = B14Scan{};
    g_b14.first_hit = -1;
    snprintf(g_b14.actor, sizeof(g_b14.actor), "%s", c264_name(actor).text);
    g_b14.n = b14_points(centre, extent, eye, g_b14.pts);
    log::line("cmd: %u aim scan: %s bounds centre %.0f %.0f %.0f, extent %.0f %.0f %.0f, eye %.0f %.0f %.0f, %d points",
              c.seq, g_b14.actor, centre.x, centre.y, centre.z, extent.x, extent.y, extent.z, eye.x, eye.y, eye.z,
              static_cast<int>(g_b14.n));
    b14_trace_points(pawn, actor, eye);
    if (g_b14.hits == 0) {
        b14_settle(pc, pawn, g_b14.pts[0], "centre, no hit", B14Phase::Final);
        return nullptr;
    }
    const double h = static_cast<double>(g_b14.hits);
    b14_settle(pc, pawn, Vec3{g_b14.sum.x / h, g_b14.sum.y / h, g_b14.sum.z / h}, "centroid", B14Phase::Centroid);
    return nullptr;
}

// Records point k: a hit, or the name the line hit instead for the `seen:` row.
void b14_note(const void* hit, bool is_target) {
    if (hit == nullptr) {
        return;
    }
    const ObjectName name = c264_name(hit);
    if (is_target) {
        ++g_b14.hits;
        g_b14.sum = Vec3{g_b14.sum.x + g_b14.pts[g_b14.k].x, g_b14.sum.y + g_b14.pts[g_b14.k].y,
                         g_b14.sum.z + g_b14.pts[g_b14.k].z};
        g_b14.first_hit = g_b14.first_hit < 0 ? g_b14.k : g_b14.first_hit;
        return;
    }
    for (int32_t i = 0; i < g_b14.nseen; ++i) {
        if (strcmp(g_b14.seen[i], name.text) == 0) {
            return;
        }
    }
    if (g_b14.nseen < kB14Seen) {
        snprintf(g_b14.seen[g_b14.nseen++], cmdfile::kActorChars, "%s", name.text);
    }
}

// The scan row, the `seen:` row when no point matched, then the card 264 aim rows from the settled view.
void b14_rows(void* pc, const void* interactor, uint32_t seq, double yaw, double pitch) {
    const auto* const ic = static_cast<const uint8_t*>(interactor);
    float range = 0.0f;
    if (ic != nullptr) {
        memcpy(&range, ic + kInteractorRange, sizeof(range));
    }
    // b256: the focus sweep's looks that ran (all N when it turned back, none when no sweep ran).
    const int32_t swept = g_b14.phase == B14Phase::Sweep  ? g_b14.sweep + 1
                          : g_b14.phase == B14Phase::Back ? g_b14.n
                                                          : 0;
    log::line("cmd: %u aim scan: %s hits %d of %d, chosen %.0f %.0f %.0f yaw %.1f pitch %.1f, range %.0f trace type "
              "%d, by %s, sweep look %d of %d", seq, g_b14.actor, static_cast<int>(g_b14.hits),
              static_cast<int>(g_b14.n), g_b14.chosen.x, g_b14.chosen.y, g_b14.chosen.z, yaw, pitch,
              static_cast<double>(range), ic != nullptr ? static_cast<int>(ic[kInteractorTraceType]) : -1, g_b14.how,
              static_cast<int>(swept), static_cast<int>(g_b14.n));
    if (g_b14.hits == 0) {
        char list[kB14Seen * cmdfile::kActorChars + 16] = "none";
        size_t at = 0;
        for (int32_t i = 0; i < g_b14.nseen; ++i) {
            at += static_cast<size_t>(snprintf(list + at, sizeof(list) - at, "%s%s", i > 0 ? ", " : "", g_b14.seen[i]));
        }
        log::line("cmd: %u aim scan: seen: %s", seq, list);
    }
    c264_aim_rows(pc, seq);
}

// Aims at `pt` for the next phase.
void b14_settle(void* pc, const void* pawn, const Vec3& pt, const char* how, B14Phase phase) {
    double yaw = 0.0;
    double pitch = 0.0;
    g_b14.chosen = pt;
    g_b14.how = how;
    g_b14.phase = phase;
    g_b14.frames = 0;
    b14_aim(pc, pawn, pt, &yaw, &pitch);
}

// One tick of the scan: true when it ended (*why null on success; a gate miss is the drive's focus= void).
bool b14_step(void* pc, const cmdfile::Cmd& c, ULONGLONG now, const char** why) {
    *why = nullptr;
    void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    if (pawn == nullptr) {
        *why = "no local pawn";
        return true;
    }
    if (now >= g_c63.until) {
        *why = "aim scan not done in 10 s";
        return true;
    }
    if (++g_b14.frames < kB14WaitFrames) {
        return false;
    }
    const void* const interactor = read_ptr(pawn, kCharacterInteractor);
    const void* const focus = interact_target(interactor);
    const bool match = focus != nullptr && strncmp(c264_name(focus).text, c.want, strlen(c.want)) == 0;
    if (g_b14.phase == B14Phase::Centroid && !match) {
        b14_settle(pc, pawn, g_b14.pts[g_b14.first_hit], "first hit (centroid missed)", B14Phase::Final);
        return false;
    }
    // The Visibility trace does not predict the interactor's focus (c335-range-load on b22, line 167: the one traced
    // hit was the bounds centre, focus none there; the b15 per-point looks found focus at a grid point at eye
    // height). So when the traced choice misses, the view steps through every point and stops at the first focus.
    // b241 (c247b240a, user "client keeps spinning" at a satchel site): a sweep that missed every point left the view
    // on the last grid point, a face corner. It now turns back to the best point, the first traced hit (the centre
    // when none hit), and the rows are read from there.
    if (!match && g_b14.phase != B14Phase::Back) {
        const int32_t next = g_b14.phase == B14Phase::Sweep ? g_b14.sweep + 1 : 0;
        if (next < g_b14.n) {
            g_b14.sweep = next;
            b14_settle(pc, pawn, g_b14.pts[next], "focus sweep (traced choice missed)", B14Phase::Sweep);
            return false;
        }
        const int32_t best = g_b14.first_hit >= 0 ? g_b14.first_hit : 0;
        b14_settle(pc, pawn, g_b14.pts[best], g_b14.first_hit >= 0 ? "first hit (sweep missed)" : "centre (sweep missed)",
                   B14Phase::Back);
        return false;
    }
    const Vec3 rot = c124_view_rot(pc);
    b14_rows(pc, interactor, c.seq, c124_wrap(rot.y), c124_wrap(rot.x));
    return true;
}

// ---- Card 265: `clear <radius>`, the own pawn kills the bugs around it with its weapon ----
// Each tick: card 023's enemy table (AI troopers skipped), live = Health over 0 (read only). The nearest live bug
// the card 264 trace sees is the target; the card 124 smooth look turns to its bounds centre; LeftMouseButton is held
// through the real input path while the crosshair trace names the target. No Health drop for kC265DryMs while firing
// taps R (reload by the real key). A target not dead in kC265TargetMs is given up for kC265SkipMs. Ends after kC265QuietMs
// with no live bug within the radius, or at the command's seconds (default kC265CapMs). No damage call anywhere: every
// kill is weapon fire. Fire is held only while the crosshair trace names the target (c264-p2: 240 rounds for 18 kills
// was fire off target). Every clear that ends on its own taps R and waits kC265ReloadMs, so the next action starts
// loaded; the end row carries the equipped type's reserve and the rounds the clear drew from it (the driver voids a
// reserve under its floor).
// Card 264 p3b (c264-p3 line 215: a WarriorBug at the client's eye was the target 5.6 s, never fired at, and stood on
// the stand when the clear ended): the fire gate is the crosshair trace alone (the old 3-degree turn check aimed at
// the bounds centre, which has no stable direction for a bug at the eye, while the trace names a bug the eye is
// inside at 0 uu); a bug within kC265MeleeUu of the pawn outranks every other target, skip or not, also mid-target;
// a target that leaves the radius is dropped at once; the clear ends quiet only with no live bug within kC265MeleeUu
// of its stand (the pawn's start point), and at the cap a bug there ends it with a WARN row naming it (a VOID).
void c63_finish(const char* why); // the command runner below ends a clear through it

constexpr ULONGLONG kC265QuietMs = 3000;
constexpr ULONGLONG kC265TargetMs = 6000;
constexpr ULONGLONG kC265SkipMs = 5000;
constexpr ULONGLONG kC265CapMs = 90000;
constexpr ULONGLONG kC265DryMs = 1500;
constexpr ULONGLONG kC265ReloadMs = 2500; // after the R tap, before the next dry check
constexpr ULONGLONG kC265TapMs = 100;
constexpr double kC265MeleeUu = 400.0;
constexpr uint32_t kC265Skips = 8;
constexpr uint32_t kC265Looks = 8; // visibility traces per pick
constexpr size_t kC265AmmoChars = 256;

struct C265State {
    const void* target;
    const void* skip[kC265Skips];
    ULONGLONG skip_until[kC265Skips];
    ULONGLONG start_ms;
    ULONGLONG target_ms;
    ULONGLONG quiet_ms;   // 0: a live bug is within the radius
    ULONGLONG drop_ms;    // the target's last Health drop (or the fire start, or the reload's end)
    ULONGLONG r_up_ms;    // 0: R is up
    uint64_t target_guid;
    float target_hp;
    int32_t killed;
    int32_t gave_up;
    int32_t reloads;
    int32_t left;
    int32_t reserve0;     // the equipped type's reserve at the start (-1: no equipped weapon)
    Vec3 stand;           // the pawn at the start: the point the next action stands on
    ULONGLONG end_ms;     // 0: running; else the end reload's wait ends here
    bool fire;
    char ammo0[kC265AmmoChars];
};
C265State g_c265{};
C23List g_c265_list{};

constexpr ULONGLONG kC265WatchMs = 100;
struct C265Watched {
    const void* actor; // identity only, between two scans
    uint64_t guid;
};
struct C265Watch {
    C265Watched live[kC23Max];
    uint32_t count;
    ULONGLONG next_ms;
    bool armed; // host, from the first clear on
};
C265Watch g_c265w{};
C23List g_c265w_list{};

void c265_ammo(const void* pawn, char* out) {
    // The reserve is the ASTPlayerCharacterMI USTAmmunitionInventory (card 069), not the weapon inventory c24_inventory
    // returns: c265 probe rows printed an empty ammo text from that map.
    c69_map_text(is_a(pawn, g_player_mi_class()) ? read_ptr(pawn, kMiAmmoInventory) : nullptr, out, kC265AmmoChars);
}

// The equipped weapon's ammunition-type reserve (GetAmmoCountOfEquippedAmmunitionType), -1 without one.
int32_t c265_reserve(const void* pawn) {
    const void* const weapons = is_a(pawn, g_player_mi_class()) ? read_ptr(pawn, kMiWeaponInventory) : nullptr;
    void* const weapon = weapons != nullptr ? g_get_equipped_weapon(weapons) : nullptr;
    return weapon != nullptr ? g_get_ammo_count(weapon) : -1;
}

void c265_fire(void* pc, bool down) {
    if (g_c265.fire != down) {
        autoinput::key_event(pc, L"LeftMouseButton", down);
        g_c265.fire = down;
    }
}

bool c265_skipped(const void* a, ULONGLONG now) {
    for (uint32_t i = 0; i < kC265Skips; ++i) {
        if (g_c265.skip[i] == a && now < g_c265.skip_until[i]) {
            return true;
        }
    }
    return false;
}

Vec3 c265_centre(const void* bug) {
    Vec3 c = actor_location(bug);
    Vec3 e{};
    if (g_actor_bounds != nullptr) {
        g_actor_bounds(bug, false, &c, &e, false);
    }
    return c;
}

// The crosshair trace from `eye` to the bug's bounds centre reaches it: the trace hits the bug, or nothing blocks
// short of the centre (a channel the bug's own collision ignores still sees it through open air).
constexpr double kC265SeeSlackUu = 60.0;
bool c265_sees(const void* pawn, const Vec3& eye, const void* bug) {
    Vec3 at{};
    const Vec3 centre = c265_centre(bug);
    return c264_trace(pawn, eye, centre, &at) == bug || vec_dist(eye, at) >= vec_dist(eye, centre) - kC265SeeSlackUu;
}

const char* c265_start(void* pc, const cmdfile::Cmd& c, ULONGLONG now) {
    const void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    if (pawn == nullptr || g_enemy_class == nullptr) {
        return "no local pawn or no enemy class";
    }
    g_c265 = C265State{};
    g_c265.start_ms = now;
    g_c265.reserve0 = c265_reserve(pawn);
    g_c265.stand = actor_location(pawn);
    c265_ammo(pawn, g_c265.ammo0);
    g_c124.last_ms = 0;
    g_c265w.armed = g_c265w.armed || net_mode(pc) == kNetModeListenServer;
    log::line("cmd: %u clear: start, radius %.0f uu, cap %.0f s, ammo%s", c.seq, c.radius,
              static_cast<double>(c.ms != 0 ? c.ms : kC265CapMs) / 1000.0, g_c265.ammo0);
    return nullptr;
}

// Card 265 (host): a death row every enemy class has, keyed by NetGUID. From the first clear on, every kC265WatchMs,
// an enemy that was live and is now at Health 0 gets `clear watch: <name> (<class>) guid 0x.. dead`, one that left
// the world `clear watch: guid 0x.. gone`. The client's clear rows name its kills by the same guid; the card 132
// ragdoll rows exist only for classes with a ragdoll (the c264-c265 chariots had none).
void c265_watch(void* world, void* pc, ULONGLONG now) {
    if (!g_c265w.armed || now < g_c265w.next_ms || world == nullptr || g_enemy_class == nullptr) {
        return;
    }
    g_c265w.next_ms = now + kC265WatchMs;
    c23_collect(world, pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr, g_enemy_class(), g_c265w_list);
    bool seen[kC23Max] = {};
    C265Watched next[kC23Max] = {};
    uint32_t n = 0;
    for (uint32_t i = 0; i < g_c265w_list.count; ++i) {
        const void* const a = g_c265w_list.e[i].actor;
        if (c23_trooper(a)) {
            continue;
        }
        uint32_t j = 0;
        while (j < g_c265w.count && g_c265w.live[j].actor != a) {
            ++j;
        }
        const bool was = j < g_c265w.count;
        if (was) {
            seen[j] = true;
        }
        if (c23_set_health(a) > 0.0f) {
            const uint64_t guid = was && g_c265w.live[j].guid != 0 ? g_c265w.live[j].guid : c41_guid(a);
            next[n++] = C265Watched{a, guid};
        } else if (was) {
            log::line("clear watch: %s (%s) guid 0x%llx dead", c264_name(a).text, class_name(a).text,
                      static_cast<unsigned long long>(g_c265w.live[j].guid));
        }
    }
    for (uint32_t j = 0; j < g_c265w.count && g_c265w_list.total == g_c265w_list.count; ++j) {
        if (!seen[j]) { // the actor may be freed: the guid only
            log::line("clear watch: guid 0x%llx gone", static_cast<unsigned long long>(g_c265w.live[j].guid));
        }
    }
    memcpy(g_c265w.live, next, sizeof(next));
    g_c265w.count = n;
}

bool c265_live(const C23Enemy& e) {
    return !c23_trooper(e.actor) && c23_set_health(e.actor) > 0.0f;
}

// The live bugs within the radius (count) and the nearest one the crosshair trace sees that is not skipped (a bug
// within kC265MeleeUu is never skipped).
const C23Enemy* c265_scan(void* world, const void* pawn, double radius, ULONGLONG now, int32_t* live) {
    c23_collect(world, pawn, g_enemy_class(), g_c265_list);
    *live = 0;
    const Vec3 eye = c264_eye(pawn);
    bool tried[kC23Max] = {};
    uint32_t looks = 0;
    for (uint32_t i = 0; i < g_c265_list.count; ++i) {
        const C23Enemy& e = g_c265_list.e[i];
        if (e.dist >= 0.0 && e.dist <= radius && c265_live(e)) {
            ++*live;
        } else {
            tried[i] = true;
        }
    }
    while (looks < kC265Looks) {
        int32_t best = -1;
        for (uint32_t i = 0; i < g_c265_list.count; ++i) {
            if (!tried[i] && (best < 0 || g_c265_list.e[i].dist < g_c265_list.e[best].dist)) {
                best = static_cast<int32_t>(i);
            }
        }
        if (best < 0) {
            return nullptr;
        }
        tried[best] = true;
        const C23Enemy* const e = &g_c265_list.e[best];
        if (e->dist > kC265MeleeUu && c265_skipped(e->actor, now)) {
            continue;
        }
        ++looks;
        if (c265_sees(pawn, eye, e->actor)) {
            return e;
        }
    }
    return nullptr;
}

const C23Enemy* c265_entry(const void* bug) {
    for (uint32_t i = 0; i < g_c265_list.count; ++i) {
        if (g_c265_list.e[i].actor == bug) {
            return &g_c265_list.e[i];
        }
    }
    return nullptr;
}

// The nearest live bug within kC265MeleeUu of the stand (null: none), and its distance.
const C23Enemy* c265_on_stand(double* d) {
    const C23Enemy* close = nullptr;
    for (uint32_t i = 0; i < g_c265_list.count; ++i) {
        const C23Enemy& e = g_c265_list.e[i];
        const double de = vec_dist(e.loc, g_c265.stand);
        if (de <= kC265MeleeUu && (close == nullptr || de < *d) && c265_live(e)) {
            close = &e;
            *d = de;
        }
    }
    return close;
}

// The end row, then the command's finish. The time is the clear's own (the end reload's wait not counted).
void c265_end(void* pc, const void* pawn, const cmdfile::Cmd& c, ULONGLONG now, const char* why) {
    c265_fire(pc, false);
    if (g_c265.r_up_ms != 0) {
        autoinput::key_event(pc, L"R", false);
        g_c265.r_up_ms = 0;
    }
    char ammo[kC265AmmoChars] = {};
    c265_ammo(pawn, ammo);
    const int32_t reserve = pawn != nullptr ? c265_reserve(pawn) : -1;
    const ULONGLONG ran = (g_c265.end_ms != 0 ? g_c265.end_ms - kC265ReloadMs : now) - g_c265.start_ms;
    log::line("%scmd: %u clear: killed %d, left %d within %.0f uu after %.1f s (ammo%s), reloads %d, gave up %d, "
              "reserve %d, rounds used %d, ammo at the start%s", why != nullptr ? "WARN " : "", c.seq, g_c265.killed,
              g_c265.left, c.radius, static_cast<double>(ran) / 1000.0, ammo, g_c265.reloads, g_c265.gave_up, reserve,
              reserve >= 0 && g_c265.reserve0 >= 0 ? g_c265.reserve0 - reserve : -1, g_c265.ammo0);
    c63_finish(why);
}

// A clear that ends on its own: fire up, tap R, and end after kC265ReloadMs (the next action starts loaded).
void c265_end_reload(void* pc, ULONGLONG now) {
    c265_fire(pc, false);
    if (g_c265.r_up_ms == 0) {
        autoinput::key_event(pc, L"R", true);
        g_c265.r_up_ms = now + kC265TapMs;
    }
    g_c265.end_ms = now + kC265ReloadMs;
}

// The held target, each tick: dead or gone (a kill), out of the radius (dropped at once), timed out (given up for
// kC265SkipMs), outranked by a nearer bug within kC265MeleeUu (switched), else a Health drop resets the dry clock.
void c265_hold(void* pc, const void* pawn, const cmdfile::Cmd& c, ULONGLONG now, const C23Enemy* pick) {
    const C23Enemy* const t = c265_entry(g_c265.target);
    const float hp = t != nullptr ? c23_set_health(t->actor) : 0.0f;
    const auto name = c264_name(g_c265.target);
    const unsigned long long guid = static_cast<unsigned long long>(g_c265.target_guid);
    if (t == nullptr || hp <= 0.0f) {
        ++g_c265.killed;
        log::line("cmd: %u clear: %s guid 0x%llx %s after %.1f s", c.seq, name.text, guid, t != nullptr ? "dead" : "gone",
                  static_cast<double>(now - g_c265.target_ms) / 1000.0);
    } else if (t->dist < 0.0 || t->dist > c.radius) {
        log::line("cmd: %u clear: dropped %s guid 0x%llx (Health %.0f): %.0f uu, out of the %.0f uu radius", c.seq,
                  name.text, guid, hp, t->dist, c.radius);
    } else if (pick != nullptr && pick->actor != g_c265.target && pick->dist <= kC265MeleeUu &&
               pick->dist < t->dist) {
        log::line("cmd: %u clear: %s guid 0x%llx at %.0f uu (within %.0f) outranks %s guid 0x%llx at %.0f uu",
                  c.seq, c264_name(pick->actor).text, static_cast<unsigned long long>(c41_guid(pick->actor)),
                  pick->dist, kC265MeleeUu, name.text, guid, t->dist);
    } else if (now - g_c265.target_ms >= kC265TargetMs) {
        const uint32_t i = static_cast<uint32_t>(g_c265.gave_up) % kC265Skips;
        g_c265.skip[i] = g_c265.target;
        g_c265.skip_until[i] = now + kC265SkipMs;
        ++g_c265.gave_up;
        log::line("cmd: %u clear: gave up on %s guid 0x%llx (Health %.0f) after %.0f s: timeout, %.0f uu (in range), "
                  "line of sight %s", c.seq, name.text, guid, hp, static_cast<double>(kC265TargetMs) / 1000.0, t->dist,
                  c265_sees(pawn, c264_eye(pawn), g_c265.target) ? "yes" : "no");
    } else {
        if (hp < g_c265.target_hp) {
            g_c265.target_hp = hp;
            g_c265.drop_ms = now;
        }
        return;
    }
    g_c265.target = nullptr;
    c265_fire(pc, false);
}

void c265_step(void* world, void* pc, const cmdfile::Cmd& c, ULONGLONG now) {
    const void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    if (pawn == nullptr || world == nullptr) {
        c265_end(pc, pawn, c, now, "no local pawn");
        return;
    }
    if (g_c265.end_ms != 0 && g_c265.r_up_ms == 0 && now >= g_c265.end_ms) { // the end reload's wait is over
        c265_end(pc, pawn, c, now, nullptr);
        return;
    }
    if (g_c265.r_up_ms != 0) { // the R tap
        if (now >= g_c265.r_up_ms) {
            autoinput::key_event(pc, L"R", false);
            g_c265.r_up_ms = 0;
        }
        return;
    }
    if (g_c265.end_ms != 0) {
        return; // waiting out the end reload
    }
    const C23Enemy* const pick = c265_scan(world, pawn, c.radius, now, &g_c265.left);
    if (g_c265.target != nullptr) {
        c265_hold(pc, pawn, c, now, pick);
    }
    double close_d = 0.0;
    const C23Enemy* const close = c265_on_stand(&close_d);
    if (g_c265.left == 0 && g_c265.target == nullptr && close == nullptr) {
        g_c265.quiet_ms = g_c265.quiet_ms == 0 ? now : g_c265.quiet_ms;
        if (now - g_c265.quiet_ms >= kC265QuietMs) {
            c265_end_reload(pc, now);
            return;
        }
    } else {
        g_c265.quiet_ms = 0;
    }
    if (now - g_c265.start_ms >= (c.ms != 0 ? c.ms : kC265CapMs)) {
        if (close != nullptr) { // card 264 p3b: the next action cannot stand there
            static char why[kC265AmmoChars];
            snprintf(why, sizeof(why), "bug %s (%s) guid 0x%llx %.0f uu from the stand at the cap (within %.0f)",
                     c264_name(close->actor).text, class_name(close->actor).text,
                     static_cast<unsigned long long>(c41_guid(close->actor)), close_d, kC265MeleeUu);
            c265_end(pc, pawn, c, now, why);
            return;
        }
        c265_end_reload(pc, now); // the driver voids on left over max-left
        return;
    }
    if (g_c265.target == nullptr && pick != nullptr) {
        g_c265.target = pick->actor;
        g_c265.target_ms = now;
        g_c265.drop_ms = now;
        g_c265.target_hp = c23_set_health(pick->actor);
        g_c265.target_guid = c41_guid(pick->actor); // card 265: host and client rows name the same bug by its NetGUID
        log::line("cmd: %u clear: target %s (%s) guid 0x%llx Health %.0f, %.0f uu, %d live within %.0f uu", c.seq,
                  c264_name(pick->actor).text, class_name(pick->actor).text,
                  static_cast<unsigned long long>(g_c265.target_guid), g_c265.target_hp, pick->dist, g_c265.left,
                  c.radius);
    }
    if (g_c265.target == nullptr) {
        c265_fire(pc, false);
        return;
    }
    const Vec3 eye = c264_eye(pawn);
    const Vec3 at = c265_centre(g_c265.target);
    const double yaw = atan2(at.y - eye.y, at.x - eye.x) * kRadToDeg;
    const double pitch = atan2(at.z - eye.z, c64_dist2d(at, eye)) * kRadToDeg;
    c124_turn(pc, yaw, pitch, now);
    Vec3 hit_at{};
    const bool on = c264_trace(pawn, eye, c264_view_end(pc, eye), &hit_at) == g_c265.target; // the crosshair names it
    if (on && !g_c265.fire) {
        g_c265.drop_ms = now;
    }
    c265_fire(pc, on);
    if (g_c265.fire && now - g_c265.drop_ms >= kC265DryMs) { // firing, no Health drop: an empty magazine
        c265_fire(pc, false);
        autoinput::key_event(pc, L"R", true);
        g_c265.r_up_ms = now + kC265TapMs;
        g_c265.drop_ms = now + kC265ReloadMs;
        ++g_c265.reloads;
        log::line("cmd: %u clear: reload (R) after %.1f s of fire with no Health drop on %s", c.seq,
                  static_cast<double>(kC265DryMs) / 1000.0, c264_name(g_c265.target).text);
    }
}

// b16 (b15 cmds 51 and 60: `warp client BP_SatchelItemPickup_C 500` put the client on a ledge 170 uu over the
// satchel, whose walk was 2008 uu and ended 190 uu short): a `warp ... <name> <dist>` lands on the navmesh point
// nearest one of kB16Dirs points dist uu from the actor (the first toward the pawn, then every 45 degrees) whose path
// to the actor's navmesh point is the shortest, and logs it. False (the old landing) with no main nav data, no
// navmesh point near the actor, a walk running (the path query shares its point buffer) or no direction with a path.
constexpr int32_t kB16Dirs = 8;
constexpr double kB16ActorMeshUu = 150.0; // the actor's navmesh point: within this (2D) of its root
constexpr double kB16PathEndUu = 50.0;    // a path that ends this close to that point reaches it
// Card 244 b211 (c244b210a cmds 192, 211): BP_SatchelItemPickup_C_1 (z 216) sits on a platform with no navmesh, and the
// projection (half height kC64ProjectHalfZ 1200) found the floor 1058 uu under it, so the warp landed the client there.
// An actor more than kB16AboveMeshUu over its navmesh point gets a down trace (kEccWorldStatic) from the old landing
// (dist uu short, kC75WarpAbove over its z); a surface within kB16SupportUu under the actor's z takes the old landing.
// Height alone does not separate the cases: C4M2's hanging egg sacs (c245b194a) are 850-1070 uu over their navmesh
// landings and their old landing is in the air, so they keep the navmesh landing (no surface found).
constexpr double kB16AboveMeshUu = 300.0;
constexpr double kB16SupportUu = 300.0;
bool b16_landing(void* world, void* pawn, const Vec3& at, const Vec3& from, const Vec3& old, double dist, uint32_t seq,
                 Vec3* out) {
    if (c64_walking() || c64_adopt_main_nav(world, c64_nav_system(world), at) == nullptr) {
        return false;
    }
    Vec3 goal{};
    if (!c64_project(world, pawn, at, Vec3{kB16ActorMeshUu, kB16ActorMeshUu, kC64ProjectHalfZ}, goal)) {
        log::line("cmd: %u warp: no navmesh point within %.0f uu of the actor; old landing", seq, kB16ActorMeshUu);
        return false;
    }
    if (at.z - goal.z > kB16AboveMeshUu) {
        const Vec3 down{old.x, old.y, at.z - kB16SupportUu};
        bool inside = false;
        const Vec3 floor = trace_channel(pawn, old, down, kEccWorldStatic, nullptr, nullptr, &inside);
        const bool support = !inside && floor.z > down.z + 1.0;
        log::line("cmd: %u warp: the actor's navmesh point (%.0f, %.0f, %.0f) is %.0f uu under it; surface under the old "
                  "landing %s (z %.0f)%s", seq, goal.x, goal.y, goal.z, at.z - goal.z,
                  inside ? "inside geometry" : support ? "found" : "none", floor.z,
                  support ? ": off the navmesh, on a platform; old landing" : "");
        if (support) {
            return false;
        }
    }
    const double base = atan2(from.y - at.y, from.x - at.x);
    const double half = dist * 0.5 > kB16ActorMeshUu ? dist * 0.5 : kB16ActorMeshUu;
    double best = -1.0;
    int32_t best_dir = -1;
    for (int32_t d = 0; d < kB16Dirs; ++d) {
        const double a = base + d * (6.283185307179586 / kB16Dirs); // 2 pi / kB16Dirs
        const Vec3 cand{at.x + cos(a) * dist, at.y + sin(a) * dist, at.z};
        Vec3 mesh{};
        if (!c64_project(world, pawn, cand, Vec3{half, half, kC64ProjectHalfZ}, mesh)) {
            continue;
        }
        const int32_t n = c64_query(pawn, pawn, mesh, goal);
        if (n <= 0 || vec_dist(g_c64.pts[n - 1], goal) > kB16PathEndUu) {
            continue;
        }
        double len = 0.0;
        Vec3 prev = mesh;
        for (int32_t i = 0; i < n; ++i) {
            len += vec_dist(prev, g_c64.pts[i]);
            prev = g_c64.pts[i];
        }
        if (best < 0.0 || len < best) {
            best = len;
            best_dir = d;
            *out = mesh;
        }
    }
    g_c64.npts = 0;
    if (best_dir < 0) {
        log::line("WARN cmd: %u warp: no navmesh point %.0f uu from the actor has a path to it; old landing", seq,
                  dist);
        return false;
    }
    log::line("cmd: %u warp: navmesh landing (%.0f, %.0f, %.0f), direction %d of %d, path to the actor %.0f uu", seq,
              out->x, out->y, out->z, best_dir + 1, kB16Dirs, best);
    return true;
}

// Card 075 test: `warp actor|client <name> [dist]` teleports the local pawn (client: the host's server copy of the
// client's pawn, which replicates) to dist uu short of the nearest such actor, kC75WarpAbove over its z (card 106:
// `point x y z` puts it exactly there, no lift). The pod
// mechs on FED_C4M1 sit past a navmesh gap that a goto cannot cross. Card 123: `behind` puts it dist uu behind the
// actor along the actor's forward axis instead (a turret's CanInteract wants the pawn facing the turret's forward).
// Card 264: `front` is the same with the sign flipped, dist uu along the forward axis (a switch's face side). The row
// logs the actor yaw and the landing point. Null when moved.
constexpr double kC75WarpAbove = 100.0;
const char* c75_warp(void* world, void* pc, const cmdfile::Cmd& c) {
    void* const own = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    void* const pawn = c.copy != 0 && own != nullptr ? c29_server_copy_nth(own, c.copy - 1) : own; // card 209: 2
    if (world == nullptr || pawn == nullptr) {
        return c.copy == 2 ? "no second server copy (client2)" : c.copy != 0 ? "no server copy of the client's pawn"
                                                                             : "no local pawn";
    }
    const Vec3 from = actor_location(pawn);
    void* const actor = c.point ? nullptr : c64_find_actor(world, c.actor, from, pawn, c.live); // b210: live
    if (!c.point && actor == nullptr) {
        return "no such actor";
    }
    const Vec3 at = c.point ? Vec3{c.at[0], c.at[1], c.at[2]} : actor_location(actor);
    const void* const root = c.behind || c.front ? read_ptr(actor, kActorRootComponent) : nullptr;
    const double side = c.front ? 0.0 : 180.0; // card 123: seat side; card 264: face side
    const double back = root != nullptr ? (component_yaw(root) + side) / kRadToDeg : 0.0;
    const double dx = root != nullptr ? cos(back) * 2.0 : from.x - at.x; // length 2: a unit vector, past the h > 1 test
    const double dy = root != nullptr ? sin(back) * 2.0 : from.y - at.y;
    const double h = sqrt(dx * dx + dy * dy);
    Vec3 to = c.point ? at
                      : Vec3{at.x + (h > 1.0 ? dx / h : 1.0) * c.radius, at.y + (h > 1.0 ? dy / h : 0.0) * c.radius,
                             at.z + kC75WarpAbove};
    Vec3 mesh{};
    if (!c.point && root == nullptr && c.radius > 0.0 && b16_landing(world, pawn, at, from, to, c.radius, c.seq, &mesh)) {
        to = Vec3{mesh.x, mesh.y, mesh.z + kC75WarpAbove}; // b16: a landing with a walk to the actor
    }
    const bool moved = g_set_actor_location(pawn, &to, false, nullptr, kTeleportPhysics);
    const Vec3 now = actor_location(pawn);
    char name[kChanNameMax + 1] = "the point";
    if (!c.point) {
        c50_name(actor, name, sizeof(name));
    }
    log::line("cmd: %u warp: %s %s (%.1f, %.1f, %.1f) -> (%.1f, %.1f, %.1f), %.0f uu from %s (yaw %.1f); "
              "SetActorLocation %d",
              c.seq, c.copy == 2 ? "server copy 2" : c.copy != 0 ? "server copy" : "own pawn", object_name(pawn).text, from.x, from.y, from.z, now.x, now.y,
              now.z, vec_dist(now, at), name, root != nullptr ? component_yaw(root) : 0.0, moved ? 1 : 0);
    return moved ? nullptr : "SetActorLocation failed";
}

// Card 130 test: `speed <x>` writes AWorldSettings::TimeDilation (UWorld::PersistentLevel -> ULevel::WorldSettings,
// both reflected). The property replicates, so a client runs the same rate. Null on success, else why not.
const char* c130_speed(void* world, double factor) {
    const uint8_t* prop = nullptr;
    const void* const level = prop_object_field(world, "PersistentLevel", &prop);
    void* const ws = level != nullptr ? const_cast<void*>(prop_object_field(level, "WorldSettings", &prop)) : nullptr;
    const uint8_t* const td = ws != nullptr ? prop_find_prop(read_ptr(ws, kObjectClass), "TimeDilation") : nullptr;
    if (td == nullptr) {
        return ws == nullptr ? "no world settings" : "no TimeDilation property";
    }
    const float value = static_cast<float>(factor);
    memcpy(static_cast<uint8_t*>(ws) + read_i32(td, kPropertyOffset), &value, sizeof(value));
    log::line("card130: host world TimeDilation set to %.2f", factor);
    return nullptr;
}

// Card 132 note 4 test: `inventory` logs the weapon inventory of the own pawn and, on the host, of the server copy
// of the client's pawn, so a switch can be checked on both copies of the client's pawn (client log: its own pawn;
// host log: the server copy). Null when the own pawn has one.
const char* c132_inventory(void* pc, uint32_t seq) {
    void* const own = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    if (own == nullptr) {
        return "no local pawn";
    }
    void* const copy = g_client_session ? nullptr : c29_server_copy(own);
    void* const pawns[2] = {own, copy};
    for (void* const pawn : pawns) {
        if (pawn == nullptr) {
            continue;
        }
        char inv[512];
        c48_inventory_text(pawn, inv, sizeof(inv));
        log::line("cmd: %u inventory: %s %s (%s): %s", seq, g_client_session ? "client" : "host", object_name(pawn).text,
                  kSprintSideText[sprint_side(pawn)], inv);
    }
    return nullptr;
}

// Card 243 run A decision, test only: `ammo fill` gives the caller's own soldier pawn its equipped weapon's starting
// ammunition through c21_grant_ammo, so the client's AmmoMirror carries the gain as in play. For a magazine type
// (the Morita) that sets the magazine only and adds no reserve (run c243-c3m2-b5: "46 to 46"); objective scenarios
// now clear by `clear <radius> kill` and need no fill. The row: the equipped primary ammunition tag and the reserve before and after. Null when done.
constexpr int32_t kC243FillBelow = 60; // one Morita magazine, the driver's reserve floor (drive.py RESERVE_FLOOR)
const char* c243_ammo_fill(void* pc, uint32_t seq) {
    void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    void* const inventory = is_a(pawn, g_player_mi_class()) ? read_ptr(pawn, kMiWeaponInventory) : nullptr;
    void* const weapon = inventory != nullptr && g_get_equipped_weapon != nullptr ? g_get_equipped_weapon(inventory)
                                                                                   : nullptr;
    void* const cls = weapon != nullptr ? read_ptr(weapon, kWeaponDefinitionClass) : nullptr;
    if (cls == nullptr) {
        return weapon == nullptr ? "no own soldier pawn with an equipped weapon" : "the weapon has no definition class";
    }
    uint64_t tag = 0;
    if (g_c274_magazine_type != nullptr) {
        g_c274_magazine_type(&tag, weapon, 1);
    }
    const int32_t before = c265_reserve(pawn);
    if (before >= kC243FillBelow) { // "used before each clear when the reserve is under one magazine"
        log::line("cmd: %u ammo fill: %s %d, not under %d, no fill", seq, fname_text(&tag).text, before, kC243FillBelow);
        return nullptr;
    }
    c21_grant_ammo(pawn, inventory, cls);
    log::line("cmd: %u ammo fill: %s %d to %d", seq, fname_text(&tag).text, before, c265_reserve(pawn));
    return nullptr;
}

// Card 098 test: `kill actor <name>` calls USTCombatUtilityLibrary::InstaKillTarget on the nearest such actor, so a
// world change (a bug hole's death) can be measured without the weapon that normally causes it. A satchel building
// (bug hole) gets ReceiveOnSatchelExploded(false) through the card 098 relay path instead. Null when called.
// b184, test only (UBWCOOP_TEST_SKIP): `levelcall <name>` on the host runs the level script function `name` (a
// Blueprint function or custom event, spaces kept: "Toggle Brain Bug Boundary") on the first level script actor of the
// world's loaded levels (persistent first, then the streamed ones) that has it, by ProcessEvent. The function must take
// no parameters. Row: `cmd: N levelcall <name> on <actor>: run`; the failure reason otherwise. Null when run.
const char* c237_levelcall(void* world, const cmdfile::Cmd& c) {
    static void* level_script_class = nullptr;
    if (level_script_class == nullptr) {
        level_script_class = find_class(L"/Script/Engine.LevelScriptActor");
    }
    if (!config::test_skip() || g_client_session || world == nullptr || level_script_class == nullptr ||
        g_fname_ctor == nullptr || g_find_function == nullptr || g_orig_process_event == nullptr) {
        return "levelcall runs on a test host (UBWCOOP_TEST_SKIP) with the level script class and ProcessEvent";
    }
    wchar_t wname[cmdfile::kActorChars];
    const size_t n = strnlen(c.actor, cmdfile::kActorChars - 1);
    for (size_t i = 0; i <= n; ++i) {
        wname[i] = static_cast<wchar_t>(i < n ? c.actor[i] : '\0');
    }
    uint64_t fname = 0;
    g_fname_ctor(&fname, wname, kFNameAdd);
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    for (int32_t l = 0; levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const auto* const actors = levels[l] != nullptr ? static_cast<void* const*>(read_ptr(levels[l], kLevelActors)) : nullptr;
        const int32_t num = levels[l] != nullptr ? read_i32(levels[l], kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; actors != nullptr && a < num && a < kMaxLevelActors; ++a) {
            if (actors[a] == nullptr || !is_a(actors[a], level_script_class)) {
                continue;
            }
            void* const fn = g_find_function(actors[a], fname);
            if (fn == nullptr) {
                continue;
            }
            const uint16_t parms = *reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(fn) + kFunctionParmsSize);
            if (parms != 0) {
                log::line("WARN cmd: %u levelcall %s on %s: takes %u bytes of parameters, not run", c.seq, c.actor,
                          object_name(actors[a]).text, static_cast<unsigned>(parms));
                return "the function takes parameters";
            }
            log::line("cmd: %u levelcall %s on %s: run", c.seq, c.actor, object_name(actors[a]).text);
            alignas(16) uint8_t none[16] = {};
            g_orig_process_event(actors[a], fn, none);
            return nullptr;
        }
    }
    log::line("WARN cmd: %u levelcall %s: no level script actor of the %d loaded levels has it", c.seq, c.actor,
              num_levels);
    return "no level script actor has the function";
}

// b216 (card 245 boss stages, senior order): `kill actor <name> <percent>` deals percent of the actor's MaxHealth as
// one USTCombatUtilityLibrary::DealDamage hit, the actor its own causer with the damage modifiers ignored (cards 043 and
// 079: the proven self hit; c245b216a with the own pawn as causer and the modifiers kept: 6000 -> 6000), so the game's
// own Health change (and a boss's damage-driven stage events) runs. Rows: `cmd: N kill: DealDamage(<class>
// <name>) P% of MaxHealth M = D, health H dead X` and `cmd: N kill: after DealDamage health H dead X`. Null when called.
const char* c98_hurt(void* actor, const cmdfile::Cmd& c, float before, int dead_before) {
    void* component = nullptr;
    c21_health(static_cast<const uint8_t*>(actor), &component);
    const float max_health = c43_max_health(component);
    if (g_deal_damage == nullptr || max_health <= 0.0f) {
        log::line("WARN cmd: %u kill: DealDamage on %s not run: DealDamage %p, MaxHealth %.1f", c.seq,
                  object_name(actor).text, reinterpret_cast<void*>(g_deal_damage), static_cast<double>(max_health));
        return "no DealDamage or no MaxHealth";
    }
    const float amount = max_health * c.value / 100.0f;
    log::line("cmd: %u kill: DealDamage(%s %s) %.1f%% of MaxHealth %.1f = %.1f, health %.1f dead %d", c.seq,
              class_name(actor).text, object_name(actor).text, static_cast<double>(c.value),
              static_cast<double>(max_health), static_cast<double>(amount), static_cast<double>(before), dead_before);
    g_deal_damage(actor, actor, amount, true);
    float after = 0.0f;
    const int dead_after = c25_dead(actor, &after);
    log::line("cmd: %u kill: after DealDamage health %.1f dead %d", c.seq, static_cast<double>(after), dead_after);
    return nullptr;
}

const char* c98_kill(void* world, void* pc, const cmdfile::Cmd& c) {
    void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    if (world == nullptr || pawn == nullptr || g_insta_kill == nullptr) {
        return "no local pawn or no InstaKillTarget";
    }
    void* const actor = c64_find_actor(world, c.actor, actor_location(pawn), pawn, true); // card 130: live only
    if (actor == nullptr) {
        return "no such actor";
    }
    if (text_contains(class_name(actor).text, "SatchelBuilding")) { // a bug hole dies only to a satchel's event
        log::line("cmd: %u kill: ReceiveOnSatchelExploded(%s %s)", c.seq, class_name(actor).text, object_name(actor).text);
        detour_c98_satchel(actor, false);
        return nullptr;
    }
    float before = 0.0f;
    const int dead_before = c25_dead(actor, &before);
    if (c.value > 0.0f) { // b216: one self hit of a share of MaxHealth, the damage modifiers ignored
        return c98_hurt(actor, c, before, dead_before);
    }
    log::line("cmd: %u kill: InstaKillTarget(%s %s) guid 0x%llx health %.1f dead %d", c.seq, class_name(actor).text,
              object_name(actor).text, static_cast<unsigned long long>(c41_guid(actor)), before, dead_before);
    g_insta_kill(actor, false);
    float after = 0.0f;
    const int dead_after = c25_dead(actor, &after);
    log::line("cmd: %u kill: after InstaKillTarget health %.1f dead %d", c.seq, after, dead_after); // card 259
    return nullptr;
}

// User rule 2026-11-13 (objective tests kill the bugs near an objective by code, not by fire), test only:
// `clear <radius> kill` on the host calls InstaKillTarget, as `kill actor` does, on every live bug within radius of
// the own pawn (one c23_collect scan, at most kC23Max bugs). No fire, no ammunition, the pawn stays on its stand. Row:
// `cmd: N clear kill: K killed, L left (radius R uu, S scanned of T)` (L: scanned bugs within radius still above
// Health 0 after the call; bugs past the scan's kC23Max cap are not counted as left, only shown by S of T). Null when
// done.
// Card 245 (b191, c245b190b and c245b189a rows): FED_C4M2's Plaza_SO3 wave (BP_EM_1FactionSetWaves_C, wave data
// BP_PWD_ControllerTrooper) is two hostile BP_AI_ControllerTrooper_C that shoot the players. c265_live skips every
// "Trooper" class (friendly AI troopers are ASTEnemyBase too), so clear never killed them, the wave never ended and
// Plaza_SO3 stayed open. Clear also kills a live ControllerTrooper; the other troopers stay spared.
// c245b199a (b199 survivor rows): 49 "left BP_AI_ControllerTrooper_C health 0.0" rows were trooper bodies the health
// read keeps just above 0 after death (card293: IsDead 1, health 0.0), so every later clear counted them as left
// (the 3 to 14 "left" in c245b194b..b198a). A ControllerTrooper is a target only while its health component's IsDead
// byte is not 1 -- b200 (c98_dead_now) and b201 (c25_dead) both read such a body as alive: c245b201a cmds 106..116
// rows "health 3.8147e-06, IsDead 0". The body keeps a 3.8e-06 health residue, so a ControllerTrooper (and a bug after
// InstaKillTarget) counts as alive only at kC265AliveHp or more; a live trooper starts at 60 (card318).
constexpr float kC265AliveHp = 1.0f;

bool c265_clear_target(const C23Enemy& e) {
    if (c265_live(e)) {
        return true;
    }
    return strstr(class_name(e.actor).text, "ControllerTrooper") != nullptr && c23_set_health(e.actor) >= kC265AliveHp;
}

const char* c265_clear_kill(void* world, void* pc, const cmdfile::Cmd& c) {
    const void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    if (world == nullptr || pawn == nullptr || g_enemy_class == nullptr || g_insta_kill == nullptr) {
        return "no local pawn, enemy class or InstaKillTarget";
    }
    if (g_client_session || net_mode(pc) != kNetModeListenServer) {
        return "clear kill runs on the listen host";
    }
    c23_collect(world, pawn, g_enemy_class(), g_c265_list);
    uint32_t killed = 0;
    // left: scanned bugs within the radius that survived. Bugs past the kC23Max scan are unmeasured (the row's
    // "scanned of" figures show them), not counted as left, so a big map's far bugs do not void the gate.
    uint32_t left = 0;
    // spared: card 245 (b186): the FED_C4M2 Brain Bug (BP_AI_BrainBug_C, exact name; the order's "Brain_Bug" names
    // no class) is its objective's own target and dies by fire only.
    uint32_t spared = 0;
    for (uint32_t i = 0; i < g_c265_list.count; ++i) {
        const C23Enemy& e = g_c265_list.e[i];
        if (e.dist < 0.0 || e.dist > c.radius || !c265_clear_target(e)) {
            continue;
        }
        if (strcmp(class_name(e.actor).text, "BP_AI_BrainBug_C") == 0) {
            ++spared;
            continue;
        }
        g_insta_kill(e.actor, false);
        const float hp = c23_set_health(e.actor);
        if (hp >= kC265AliveHp) {
            // Card 245 (c245b198a line 313: "1 killed, 3 left"): every c245 run leaves 1-14 bugs alive after
            // InstaKillTarget and the row did not say which; the first four survivors are named with their health.
            if (left < 4) {
                log::line("cmd: %u clear kill: left %s (%s) health %g at %.0f uu", c.seq, object_name(e.actor).text,
                          class_name(e.actor).text, hp, e.dist);
            }
            ++left;
        } else {
            ++killed;
        }
    }
    log::line("cmd: %u clear kill: %u killed, %u left, %u spared (radius %.0f uu, %u scanned of %u)", c.seq, killed,
              left, spared, c.radius, g_c265_list.count, g_c265_list.total);
    return nullptr;
}

// Card 113: `travel next|restart` on the host: the game's own TransitionToLevel to the next mission or the current
// one again, as the mission end or RESTART MISSON would. Card 045 adds `listen`; the client drops and reconnects.
const char* c113_travel(void* world, bool next) {
    // Card 349: any PC in the world will do (net mode only); not the local PC.
    void* const pc = g_world_first_pc(world);
    void* const gi = pc != nullptr && net_mode(pc) == kNetModeListenServer ? g_get_st_game_instance(world) : nullptr;
    if (gi == nullptr) {
        return "travel runs on a listen host with an ST game instance";
    }
    const auto* const g = static_cast<const uint8_t*>(gi);
    const int32_t chapter = read_i32(g, kGameInstanceChapter);
    const int32_t level = read_i32(g, kGameInstanceLevel) + (next ? 1 : 0);
    FStringRaw empty{}; // passed by value: the callee destroys it (empty, nothing to free)
    log::line("card113: host t=%llu travel %s: TransitionToLevel(chapter %d, level %d)",
              static_cast<unsigned long long>(GetTickCount64()), next ? "next" : "restart", chapter, level);
    g_transition_to_level(gi, chapter, level, &empty);
    return nullptr;
}

// Card 132 point 1: `travel checkpoint` on the host: USTSaveSystemLibrary::LoadLatestSave, the in-place load RESTART
// CHECKPOINT makes (card 045), with no map load. card121 logs the engine Load it reaches. Null when called.
const char* c132_checkpoint(void* world) {
    // Card 349: any PC in the world will do (net mode only); not the local PC.
    void* const pc = world != nullptr ? g_world_first_pc(world) : nullptr;
    if (pc == nullptr || net_mode(pc) != kNetModeListenServer || g_load_latest_save == nullptr) {
        return "checkpoint runs on a listen host";
    }
    log::line("card132: host t=%llu travel checkpoint: LoadLatestSave", static_cast<unsigned long long>(GetTickCount64()));
    const bool ok = g_load_latest_save(pc);
    log::line("%scard132: host travel checkpoint: LoadLatestSave returned %d", ok ? "" : "WARN ", ok ? 1 : 0);
    return ok ? nullptr : "LoadLatestSave returned false";
}

// Card 143 test: `travel save` on the host: USTSaveSystemLibrary::Save_Quicksave, a save of the current state that
// LoadLatestSave (`travel checkpoint`) loads back. card121 logs the engine save it reaches. Null when called.
const char* c143_quicksave(void* world) {
    // Card 349: any PC in the world will do (net mode only); not the local PC.
    void* const pc = world != nullptr ? g_world_first_pc(world) : nullptr;
    if (pc == nullptr || net_mode(pc) != kNetModeListenServer || g_save_quicksave == nullptr) {
        return "save runs on a listen host";
    }
    log::line("card143: host t=%llu travel save: Save_Quicksave", static_cast<unsigned long long>(GetTickCount64()));
    g_save_quicksave(pc);
    return nullptr;
}

// Card 374 test: `travel join` on a client out of a session (its frontend after the death screen Exit): the lobby
// join button's path, c52_join, with the UBWCOOP_TRAVEL address the run gave it. Null when the join is armed.
const char* c374_join() {
    if (g_client_session) {
        return "travel join runs on a client out of a session";
    }
    wchar_t address[lobby::kAddrChars];
    if (!lobby::parse_address(g_travel.url, address)) {
        return "travel join needs a client UBWCOOP_TRAVEL address";
    }
    log::line("card374: client t=%llu travel join: c52_join(\"%ls\")", static_cast<unsigned long long>(GetTickCount64()),
              address);
    c52_join(address);
    return nullptr;
}

// Card 345 test: `travel end` on the host: USTGameInstance::EndLevel through the mod's detour, as the level script's
// own call at the last objective (the level-end screen on the host, the card 260 relay to every client).
const char* c345_end_level(void* world) {
    // Card 349: any PC in the world will do (net mode only); not the local PC.
    void* const pc = world != nullptr ? g_world_first_pc(world) : nullptr;
    void* const gi = pc != nullptr && net_mode(pc) == kNetModeListenServer ? g_get_st_game_instance(world) : nullptr;
    if (gi == nullptr) {
        return "end runs on a listen host with an ST game instance";
    }
    log::line("card345: host t=%llu travel end: EndLevel", static_cast<unsigned long long>(GetTickCount64()));
    detour_end_level(gi);
    return nullptr;
}

// Card 109 test: `down host|client` (host only) puts roster slot 0 or 1 into card 043's Downed, the same entry a
// lethal hit takes, so Downed behaviour can be proven without waiting for bugs to win. Card 209: `down client2` is
// roster slot 2. Null when called.
const char* c109_down(void* world, const cmdfile::Cmd& c) {
    if (g_client_session || world == nullptr) {
        return "host only";
    }
    const C43Roster r = c43_roster(world);
    const uint32_t slot = c.at[0] == 2.0 ? 2u : c.at[0] != 0.0 ? 1u : 0u;
    void* const pawn = r.players > slot ? r.p[slot].pawn : nullptr;
    if (r.players < 2 || pawn == nullptr || c43_is_down(pawn)) {
        return "no second player, no pawn in that slot, or already down";
    }
    log::line("cmd: %u down: slot %u %s", c.seq, slot, object_name(pawn).text);
    c43_enter_down(pawn, r, slot, C43Life::Downed, GetTickCount64());
    return c43_is_down(pawn) ? nullptr : "not downed (refused by card 043)";
}

// " goal=x,y,z dist=D" while a goto runs, else " goal=- dist=-".
int c64_state_text(char* out, size_t cap) {
    if (!g_c64.running) {
        return snprintf(out, cap, " goal=- dist=-");
    }
    return snprintf(out, cap, " goal=%.0f,%.0f,%.0f dist=%.0f", g_c64.goal.x, g_c64.goal.y, g_c64.goal.z, g_c64.dist);
}

// Sets the bool property `name` on the CDO of the class at `path`. False (logged) when either is missing.
bool c64_set_cdo_bool(const wchar_t* path, const char* name) {
    void* const cls = find_class(path);
    auto* const cdo = cls != nullptr ? static_cast<uint8_t*>(read_ptr(cls, kClassDefaultObject)) : nullptr;
    const uint8_t* const p = cls != nullptr ? prop_find_prop(cls, name) : nullptr;
    if (cdo == nullptr || p == nullptr || !prop_kind_is(p, "BoolProperty") || p[kBoolFieldMask] == 0) {
        log::line("WARN card064: %ls CDO %p, property %s %p: not set", path, cdo, name, p);
        return false;
    }
    uint8_t* const b = cdo + read_i32(p, kPropertyOffset) + p[kBoolByteOffset];
    const bool before = (*b & p[kBoolFieldMask]) != 0;
    *b = static_cast<uint8_t>(*b | p[kBoolFieldMask]);
    log::line("card064: %ls CDO %s %d -> 1", path, name, before ? 1 : 0);
    return true;
}

// --- Card 067: the host's mission brief (UBWCOOP_CMD=1) ---
// Step 1 (log only). ProcessEvent sightings: the first call of each function on an object of one of kC67Classes
// (pointers are kept as keys only, never read again). Dumps walk live roots only: the WB_Stack_C that is calling
// RequestGamePause/RequestGameUnpause (dumped inside the detour), or 5 s after a pause the WB_Stack_C requesters
// in the game instance's pause array. From a root: its "Widget Stack" elements, then their WB_* object properties
// (two levels), each with its visibility; each class's own UFUNCTIONs and properties once per class pointer.
// (c67a: a pointer kept across the map change was read after the front end's WB_Stack_C was freed.)
constexpr size_t kStructChildren = 0x48;    // UStruct::Children (UField*: the UFunctions)
constexpr size_t kUFieldNext = 0x28;        // UField::Next (UObjectBase is 0x28 bytes)

constexpr size_t kFunctionNumParms = 0xB4;  // UFunction::NumParms (uint8), before ParmsSize at 0xB6

constexpr int32_t kC67Cutscenes = 4;

constexpr size_t kGameInstanceLoadingWorld = 0x2F8; // USTGameInstance: the async loading screen's world (senior, static)

constexpr int32_t kC67MaxFns = 160;      // UFunctions listed per class
constexpr int32_t kC67MaxProps = 120;    // properties per class

constexpr int32_t kC67MaxRequesters = 8;
constexpr int32_t kC67MaxElements = 8;   // per object array
constexpr uint32_t kC67MaxDumps = 12;    // per map
constexpr uint32_t kC67Depth = 2;        // WB_* object properties below a stack element
constexpr ULONGLONG kC67ResolveMs = 500;

const wchar_t* const kC67Classes[kC67Kinds] = {
    L"/Game/Blueprints/UI_Aloft/Gameplay_HUD/WB_Loading_Screen_Final.WB_Loading_Screen_Final_C",
    L"/Game/Blueprints/UI_Aloft/WidgetComponents/WB_MissionBriefVideo.WB_MissionBriefVideo_C",
    L"/Game/Blueprints/UI_Aloft/WidgetComponents/WB_Stack.WB_Stack_C",
    L"/Game/Blueprints/UI_Aloft/WidgetComponents/WB_StackElement.WB_StackElement_C",
    nullptr, // WB_Cutscenes_C: c67_object takes its class from a live stack element
};

C67 g_c67{};

int32_t c67_kind_of(const void* object) {
    const void* const cls = object != nullptr ? read_ptr(object, kObjectClass) : nullptr;
    for (int32_t k = 0; k < kC67Kinds; ++k) {
        if (cls != nullptr && cls == g_c67.cls[k]) {
            return k;
        }
    }
    return -1;
}

// True when (k, object) is new and was kept. Pointer compare only.
bool c67_remember(int32_t k, const void* object) {
    for (int32_t i = 0; i < g_c67.nseen[k]; ++i) {
        if (g_c67.seen[k][i] == object) {
            return false;
        }
    }
    if (g_c67.nseen[k] >= kC67Seen) {
        return false;
    }
    g_c67.seen[k][g_c67.nseen[k]++] = object;
    return true;
}

// Every 500 ms until all four classes exist (they load with the map that uses them).
void c67_resolve(ULONGLONG now) {
    if (now < g_c67.next_resolve) {
        return;
    }
    g_c67.next_resolve = now + kC67ResolveMs;
    for (int32_t k = 0; k < kC67Kinds; ++k) {
        if (g_c67.cls[k] == nullptr && kC67Classes[k] != nullptr) {
            g_c67.cls[k] = find_class(kC67Classes[k]);
            if (g_c67.cls[k] != nullptr) {
                log::line("card067: class %ls at %p", kC67Classes[k], g_c67.cls[k]);
                g_c67_pe = true;
            }
        }
    }
}

// One UFunction line: name, flags, parameter count and size, and each parameter's name and kind.
void c67_fn_line(const char* cls, const uint8_t* fn) {
    char parms[256] = "";
    size_t len = 0;
    const void* f = read_ptr(fn, kStructChildProperties);
    for (int32_t i = 0; i < 16 && f != nullptr && len < sizeof(parms) - 1; ++i, f = read_ptr(f, kFieldNext)) {
        const int n = snprintf(parms + len, sizeof(parms) - len, "%s%s:%s", i > 0 ? ", " : "",
                               fname_text(static_cast<const uint8_t*>(f) + kFieldName).text,
                               fname_text(read_ptr(f, kFieldClassPrivate)).text);
        len = n > 0 ? len + static_cast<size_t>(n) : sizeof(parms);
    }
    uint32_t flags = 0;
    memcpy(&flags, fn + kFunctionFlags, sizeof(flags));
    uint16_t size = 0;
    memcpy(&size, fn + kFunctionParmsSize, sizeof(size));
    log::line("card067: %s fn %s flags 0x%08X parms %u size %u (%s)", cls, object_name(fn).text, flags,
              fn[kFunctionNumParms], size, parms);
}

// One BP class's own UFunctions, then its own properties.
void c67_list_one(const void* cls, const char* cn) {
    const uint8_t* fn = static_cast<const uint8_t*>(read_ptr(cls, kStructChildren));
    int32_t fns = 0;
    for (; fns < kC67MaxFns && fn != nullptr; ++fns, fn = static_cast<const uint8_t*>(read_ptr(fn, kUFieldNext))) {
        if (strcmp(class_name(fn).text, "Function") != 0) {
            log::line("WARN card067: %s child %d is a %s, not a Function -- list stops", cn, fns, class_name(fn).text);
            break;
        }
        c67_fn_line(cn, fn);
    }
    const void* p = read_ptr(cls, kStructChildProperties);
    int32_t props = 0;
    for (; props < kC67MaxProps && p != nullptr; ++props, p = read_ptr(p, kFieldNext)) {
        log::line("card067: %s prop %s %s @0x%X", cn, fname_text(static_cast<const uint8_t*>(p) + kFieldName).text,
                  fname_text(read_ptr(p, kFieldClassPrivate)).text, read_i32(p, kPropertyOffset));
    }
    log::line("card067: %s: %d functions, %d properties listed", cn, fns, props);
}

// The object's class chain while it is a BP class (name ends in _C), each listed once per class pointer.
void c67_list_class(const void* cls) {
    for (int32_t depth = 0; depth < 3 && cls != nullptr; ++depth, cls = read_ptr(cls, kStructSuper)) {
        const ObjectName cn = object_name(cls);
        const size_t n = strlen(cn.text);
        if (n < 2 || strcmp(cn.text + n - 2, "_C") != 0) {
            return;
        }
        bool done = false;
        for (int32_t i = 0; i < g_c67.nlisted; ++i) {
            done = done || g_c67.listed[i] == cls;
        }
        if (!done && g_c67.nlisted < kC67MaxListed) {
            g_c67.listed[g_c67.nlisted++] = cls;
            c67_list_one(cls, cn.text);
        }
    }
}

bool c67_is_wb(const void* v) {
    return v != nullptr && strncmp(class_name(v).text, "WB_", 3) == 0;
}

void c67_object(const void* o, uint32_t depth, const char* via);

// An object array property of `o`: count, each element; WB_* elements are walked (depth + 1).
void c67_array(const uint8_t* p, const uint8_t* addr, uint32_t depth) {
    const auto* const inner = static_cast<const uint8_t*>(read_ptr(p, kArrayPropInner));
    const auto* const data = static_cast<void* const*>(read_ptr(addr, kArrayData));
    const int32_t num = read_i32(addr, kArrayNum);
    const bool objects = prop_kind_is(inner, "ObjectProperty") || prop_kind_is(inner, "ObjectPtrProperty");
    if (!objects || num <= 0 || data == nullptr || num > read_i32(addr, kArrayMax)) {
        return;
    }
    log::line("card067: %*s.%s array Num %d", static_cast<int>(depth * 2), "", fname_text(p + kFieldName).text, num);
    for (int32_t i = 0; i < num && i < kC67MaxElements; ++i) {
        char via[96];
        snprintf(via, sizeof(via), "%s[%d]", fname_text(p + kFieldName).text, i);
        c67_object(data[i], depth + 1, via);
    }
}

// The BP-declared object and array properties of `o` (class chain while _C): WB_* values are walked.
void c67_props(const void* o, uint32_t depth) {
    const void* cls = read_ptr(o, kObjectClass);
    for (int32_t s = 0; s < 3 && cls != nullptr; ++s, cls = read_ptr(cls, kStructSuper)) {
        const ObjectName cn = object_name(cls);
        const size_t n = strlen(cn.text);
        if (n < 2 || strcmp(cn.text + n - 2, "_C") != 0) {
            return;
        }
        const uint8_t* p = static_cast<const uint8_t*>(read_ptr(cls, kStructChildProperties));
        for (int32_t i = 0; i < kC67MaxProps && p != nullptr; ++i, p = static_cast<const uint8_t*>(read_ptr(p, kFieldNext))) {
            const uint8_t* const addr = static_cast<const uint8_t*>(o) + read_i32(p, kPropertyOffset);
            if (prop_kind_is(p, "ArrayProperty")) {
                c67_array(p, addr, depth);
            } else if ((prop_kind_is(p, "ObjectProperty") || prop_kind_is(p, "ObjectPtrProperty")) &&
                       c67_is_wb(read_ptr(addr, 0))) {
                c67_object(read_ptr(addr, 0), depth + 1, fname_text(p + kFieldName).text);
            }
        }
    }
}

// True the first time `o` is printed in this dump. Takes WB_Cutscenes_C's class from the first live one.
bool c67_visit(const void* o) {
    for (int32_t i = 0; i < g_c67.nvisited; ++i) {
        if (g_c67.visited[i] == o) {
            return false;
        }
    }
    if (g_c67.nvisited >= kC67MaxVisited) {
        return false;
    }
    g_c67.visited[g_c67.nvisited++] = o;
    if (g_c67.cls[kC67Cutscenes] == nullptr && strcmp(class_name(o).text, "WB_Cutscenes_C") == 0) {
        g_c67.cls[kC67Cutscenes] = read_ptr(o, kObjectClass);
        g_c67_pe = true;
        log::line("card067: class WB_Cutscenes_C at %p (from a live stack element)", g_c67.cls[kC67Cutscenes]);
    }
    return true;
}

// One live object: name, visibility, outer; its class's functions (once); below kC67Depth, its WB_* properties.
void c67_object(const void* o, uint32_t depth, const char* via) {
    if (o == nullptr) {
        log::line("card067: %*s%s = null", static_cast<int>(depth * 2), "", via);
        return;
    }
    if (!c67_visit(o)) {
        log::line("card067: %*s%s = %s (%p), seen above", static_cast<int>(depth * 2), "", via, class_name(o).text, o);
        return;
    }
    const auto* const b = static_cast<const uint8_t*>(o);
    log::line("card067: %*s%s = %s %s (%p): Visibility %u, outer %s (%s)", static_cast<int>(depth * 2), "", via,
              class_name(o).text, prop_name(b + kObjectName, 8).text, o, b[kC67WidgetVisibility],
              object_name(read_ptr(o, kObjectOuter)).text, class_name(read_ptr(o, kObjectOuter)).text);
    c67_list_class(read_ptr(o, kObjectClass));
    if (depth <= kC67Depth) {
        c67_props(o, depth);
    }
}

// A dump from a live WB_Stack_C (the caller of a pause detour, or a held pause requester).
void c67_dump(const void* stack, const char* why) {
    if (g_c67.dumps >= kC67MaxDumps) {
        return;
    }
    ++g_c67.dumps;
    g_c67.nvisited = 0;
    log::line("card067: dump %u in %s (%s)", g_c67.dumps, g_c67.map[0] != 0 ? g_c67.map : "?", why);
    c67_object(stack, 0, "stack");
}

// The WB_Stack_C requesters the game instance holds now (live while held), each dumped.
void c67_dump_held(void* world, const char* why) {
    auto* const gi = world != nullptr ? static_cast<uint8_t*>(read_ptr(world, kWorldOwningGameInstance)) : nullptr;
    if (gi == nullptr) {
        log::line("card067: %s: no game instance -- no dump", why);
        return;
    }
    const uint8_t* const iface = gi + kGameInstancePauseIface;
    const int32_t n = array_num(iface, kPauseRequesters);
    void* const* const data = n > 0 ? static_cast<void* const*>(read_ptr(iface, kPauseRequesters + kArrayData)) : nullptr;
    log::line("card067: %s: %d pause requester(s) held, world paused %u", why, n, g_world_is_paused(world) ? 1u : 0u);
    const void* const lw = read_ptr(gi, kGameInstanceLoadingWorld);
    log::line("card067: %s: GI+0x2F8 (loading screen world) %p %s (%s)", why, lw, lw != nullptr ? object_name(lw).text : "-",
              lw != nullptr ? class_name(lw).text : "-");
    for (int32_t i = 0; data != nullptr && i < n && i < kC67MaxRequesters; ++i) {
        if (strcmp(class_name(data[i]).text, "WB_Stack_C") == 0) {
            c67_dump(data[i], why);
        }
    }
}

// From c63_step, every tick.
void c67_step(void* engine, ULONGLONG now) {
    c67_resolve(now);
    if (g_c67.map[0] == 0) {
        void* const world = g_get_game_world(engine);
        if (world != nullptr) {
            snprintf(g_c67.map, sizeof(g_c67.map), "%s", object_name(world).text);
        }
    }
    if (g_c67.later_ms != 0 && now >= g_c67.later_ms) {
        g_c67.later_ms = 0;
        c67_dump_held(g_get_game_world(engine), "5 s after the WB_Stack_C pause");
    }
}

// --- Card 067 step 2: `ui continue` ---
// Closes the mission brief the way one real key does (c67e reference: the key down reaches WB_Cutscenes_C.OnKeyDown,
// which skips the video and destructs it; the any-key loading screen takes focus; the key up reaches
// WB_Loading_Screen_Final_C.OnKeyUp, which removes it, empties the stack, and WB_Stack_C calls RequestGameUnpause).
// Injected InputKey never reaches those Slate focus handlers, so this calls the BP function behind each step through
// ProcessEvent on the stack's top widget, one step per change of top widget (again after kC67UiRetryMs).
// Done: the WB_Stack_C's "Widget Stack" is empty and it holds no pause. The stack is the last WB_Stack_C that
// requested a pause on this map (a weak pointer, so a freed stack reads as null).
constexpr ULONGLONG kC67UiTimeoutMs = 5000;
constexpr ULONGLONG kC67UiRetryMs = 1000;
constexpr uint16_t kC67UiParmsBytes = 256;

struct C67Ui {
    uint64_t stack_weak; // FWeakObjectPtr: the last WB_Stack_C pause requester this map
    uint64_t fname[kC67UiStepCount];
    const void* poked;   // the top widget the last step was called on
    ULONGLONG next_ms;   // a repeat call on the same top widget waits until then
    uint32_t calls;
    char why[160];       // the failure text handed to c63_finish
};
C67Ui g_c67ui{};

// From c67_note_pause: the stack that requested the pause (live: it is the caller).
void c67_ui_remember(void* stack) {
    if (g_weak_assign != nullptr) {
        g_weak_assign(&g_c67ui.stack_weak, &stack);
    }
}

// The stack's "Widget Stack" array, or null. Card 414: callers ask every frame, so the offset is kept per stack
// class (0: no such field).
const uint8_t* c67_stack_array(const void* stack) {
    static const void* cached_class = nullptr;
    static int32_t cached_offset = 0;
    const void* const cls = read_ptr(stack, kObjectClass);
    if (cls != cached_class) {
        const uint8_t* const p = prop_find_prop(cls, "Widget Stack");
        cached_offset = p != nullptr ? read_i32(p, kPropertyOffset) : 0;
        cached_class = cls;
    }
    return cached_offset > 0 ? static_cast<const uint8_t*>(stack) + cached_offset : nullptr;
}

// The stack's "Widget Stack" count and top element (null when empty or unreadable).
void* c67_ui_top(const void* stack, int32_t* num) {
    const uint8_t* const arr = c67_stack_array(stack);
    *num = arr != nullptr ? read_i32(arr, kArrayNum) : -1;
    void* const* const data = arr != nullptr ? static_cast<void* const*>(read_ptr(arr, kArrayData)) : nullptr;
    return data != nullptr && *num > 0 && *num <= read_i32(arr, kArrayMax) ? data[*num - 1] : nullptr;
}

// True while `stack` is one of the game instance's pause requesters.
bool c67_ui_held(void* world, const void* stack) {
    auto* const gi = world != nullptr ? static_cast<uint8_t*>(read_ptr(world, kWorldOwningGameInstance)) : nullptr;
    const uint8_t* const iface = gi != nullptr ? gi + kGameInstancePauseIface : nullptr;
    const int32_t n = iface != nullptr ? array_num(iface, kPauseRequesters) : 0;
    void* const* const data = n > 0 ? static_cast<void* const*>(read_ptr(iface, kPauseRequesters + kArrayData)) : nullptr;
    for (int32_t i = 0; data != nullptr && i < n && i < kC67MaxRequesters; ++i) {
        if (data[i] == stack) {
            return true;
        }
    }
    return false;
}

// One step on `top`: its BP function through ProcessEvent. Null, or why it could not be called.
const char* c67_ui_call(void* top, int32_t step) {
    const C67UiStep& s = kC67UiSteps[step];
    void* const fn = fname_cached(g_c67ui.fname[step], s.fn) != 0 ? g_find_function(top, g_c67ui.fname[step]) : nullptr;
    uint16_t size = 0;
    if (fn != nullptr) {
        memcpy(&size, static_cast<const uint8_t*>(fn) + kFunctionParmsSize, sizeof(size));
    }
    const void* const first = fn != nullptr ? read_ptr(fn, kStructChildProperties) : nullptr;
    if (fn == nullptr || size > kC67UiParmsBytes || (s.first_true && first == nullptr)) {
        snprintf(g_c67ui.why, sizeof(g_c67ui.why), "%s has no %ls (UFunction %p, parms %u bytes)", s.cls, s.fn, fn, size);
        return g_c67ui.why;
    }
    alignas(16) static uint8_t parms[kC67UiParmsBytes];
    memset(parms, 0, sizeof(parms));
    if (s.first_true) {
        parms[read_i32(first, kPropertyOffset) & (kC67UiParmsBytes - 1)] = 1;
    }
    ++g_c67ui.calls;
    log::line("card067: ui continue: ProcessEvent(%s %p, %ls%s)", s.cls, top, s.fn, s.first_true ? ", true" : "");
    g_orig_process_event(top, fn, parms);
    return nullptr;
}

// The kC67UiSteps row for `top`'s class, or kC67UiStepCount.
int32_t c67_ui_step_of(const void* top) {
    const ObjectName cls = class_name(top);
    int32_t step = 0;
    while (step < kC67UiStepCount && strcmp(kC67UiSteps[step].cls, cls.text) != 0) {
        ++step;
    }
    return step;
}

// One tick of `ui continue`. Returns true when finished; *why is null (done) or the failure.
bool c67_ui_tick(void* world, ULONGLONG now, const char** why) {
    void* const stack = g_weak_get != nullptr && g_c67ui.stack_weak != 0 ? g_weak_get(&g_c67ui.stack_weak) : nullptr;
    if (stack == nullptr && g_c67ui.stack_weak != 0) {
        log::line("card067: ui continue: this map's WB_Stack_C is gone (freed): nothing to close");
        *why = nullptr;
        return true;
    }
    *why = stack == nullptr ? "no WB_Stack_C requested a pause on this map" : nullptr;
    if (stack == nullptr || g_find_function == nullptr || g_orig_process_event == nullptr) {
        *why = *why != nullptr ? *why : "FindFunction or ProcessEvent not resolved";
        return true;
    }
    int32_t num = 0;
    void* const top = c67_ui_top(stack, &num);
    const bool held = c67_ui_held(world, stack);
    if (num == 0 && !held) {
        log::line("card067: ui continue: WB_Stack_C %p empty and holds no pause after %u call(s); world paused %u",
                  stack, g_c67ui.calls, g_world_is_paused(world) ? 1u : 0u);
        return true;
    }
    if (top == nullptr || (top == g_c67ui.poked && now < g_c67ui.next_ms)) {
        return false; // waiting: an emptied stack still releasing its pause, or the last step still working
    }
    const int32_t step = c67_ui_step_of(top);
    if (step == kC67UiStepCount) {
        snprintf(g_c67ui.why, sizeof(g_c67ui.why), "top widget %s (%d in the stack) has no continue step",
                 class_name(top).text, num);
        *why = g_c67ui.why;
        return true;
    }
    g_c67ui.poked = top;
    g_c67ui.next_ms = now + kC67UiRetryMs;
    *why = c67_ui_call(top, step);
    return *why != nullptr;
}

// The timeout text: what the stack still shows.
const char* c67_ui_timeout(void* world) {
    void* const stack = g_weak_get != nullptr && g_c67ui.stack_weak != 0 ? g_weak_get(&g_c67ui.stack_weak) : nullptr;
    int32_t num = 0;
    void* const top = stack != nullptr ? c67_ui_top(stack, &num) : nullptr;
    snprintf(g_c67ui.why, sizeof(g_c67ui.why), "5 s: top widget %s, %d in the stack, pause held %u, %u call(s)",
             class_name(top).text, num, stack != nullptr && c67_ui_held(world, stack) ? 1u : 0u, g_c67ui.calls);
    return g_c67ui.why;
}

// Per map: the stack is gone with it.
void c67_ui_forget() {
    g_c67ui.stack_weak = 0;
}

// Per command.
void c67_ui_start() {
    g_c67ui.poked = nullptr;
    g_c67ui.next_ms = 0;
    g_c67ui.calls = 0;
}

// --- Card 121: `ui click <prefix>` ---
// A menu button click the way card 067 closes the brief: injected InputKey never reaches Slate, so this calls the BP
// function the button's OnClicked is bound to (e.g. WB_Pause_Menu_C's BndEvt__WB_Pause_Menu_QUIT_button_...)
// through ProcessEvent. The widget is the topmost one in this map's WB_Stack_C whose own BP class (or BP parent) has
// a parameterless function named with that prefix. One call; null when done, or why it could not be made.
constexpr int32_t kC121UiMaxStack = 16;

const uint8_t* c121_ui_fn(const void* widget, const char* prefix) {
    const size_t n = strlen(prefix);
    const void* cls = read_ptr(widget, kObjectClass);
    for (int32_t depth = 0; depth < 3 && cls != nullptr; ++depth, cls = read_ptr(cls, kStructSuper)) {
        const uint8_t* fn = static_cast<const uint8_t*>(read_ptr(cls, kStructChildren));
        for (int32_t i = 0; i < kC67MaxFns && fn != nullptr; ++i, fn = static_cast<const uint8_t*>(read_ptr(fn, kUFieldNext))) {
            if (strcmp(class_name(fn).text, "Function") == 0 && fn[kFunctionNumParms] == 0 &&
                strncmp(object_name(fn).text, prefix, n) == 0) {
                return fn;
            }
        }
    }
    return nullptr;
}

// The topmost widget in the game's WB_Stack_C with a parameterless function named `prefix`* (nullptr: none, or no
// live stack). `num` gets the stack size, `index` the widget's place (both for the caller's log line).
const uint8_t* c121_ui_find(const char* prefix, void** widget, int32_t* num, int32_t* index) {
    *widget = nullptr;
    *num = 0;
    *index = -1;
    void* const stack = g_weak_get != nullptr && g_c67ui.stack_weak != 0 ? g_weak_get(&g_c67ui.stack_weak) : nullptr;
    if (stack == nullptr) {
        return nullptr;
    }
    const uint8_t* const arr = c67_stack_array(stack);
    *num = arr != nullptr ? read_i32(arr, kArrayNum) : 0;
    void* const* const data = arr != nullptr ? static_cast<void* const*>(read_ptr(arr, kArrayData)) : nullptr;
    for (int32_t i = *num - 1; data != nullptr && i >= 0 && i >= *num - kC121UiMaxStack; --i) {
        const uint8_t* const fn = data[i] != nullptr ? c121_ui_fn(data[i], prefix) : nullptr;
        if (fn != nullptr) {
            *widget = data[i];
            *index = i;
            return fn;
        }
    }
    return nullptr;
}

// Card 345b: through detour_process_event, as a real button click goes, so the mod's gates see drive clicks too.
const char* c121_ui_click(const char* prefix) {
    if (g_orig_process_event == nullptr) {
        return "ProcessEvent not resolved";
    }
    void* widget = nullptr;
    int32_t num = 0;
    int32_t index = -1;
    const uint8_t* const fn = c121_ui_find(prefix, &widget, &num, &index);
    if (fn == nullptr) {
        snprintf(g_c67ui.why, sizeof(g_c67ui.why), "no widget of the %d in the WB_Stack_C has a function %s*", num,
                 prefix);
        return g_c67ui.why;
    }
    log::line("card121: ui click: ProcessEvent(%s %p, %s), stack element %d of %d", class_name(widget).text, widget,
              object_name(fn).text, index, num);
    alignas(16) static uint8_t parms[kC67UiParmsBytes];
    memset(parms, 0, sizeof(parms));
    detour_process_event(widget, const_cast<uint8_t*>(fn), parms);
    return nullptr;
}

// A command's result line (why == null: done), which the driver waits for.
void c63_result(uint32_t seq, const char* text, const char* why) {
    if (why == nullptr) {
        log::line("cmd: %u %s -> done", seq, text);
    } else {
        log::line("WARN cmd: %u %s -> failed (%s)", seq, text, why);
    }
    g_c63.last_seq = seq;
}

// Pops q[0] without a result line (a goto that went on in the background).
void c63_pop() {
    g_c63.started = false;
    --g_c63.count;
    memmove(&g_c63.q[0], &g_c63.q[1], sizeof(g_c63.q[0]) * static_cast<size_t>(g_c63.count));
}

// Pops q[0] and logs its result (why == null: done).
void c63_finish(const char* why) {
    const cmdfile::Cmd& c = g_c63.q[0];
    c63_result(c.seq, c.text, why);
    g_c63.started = false;
    --g_c63.count;
    memmove(&g_c63.q[0], &g_c63.q[1], sizeof(g_c63.q[0]) * static_cast<size_t>(g_c63.count));
}

// "<tag>-<name>" through FScreenshotRequest, with the UI, no number suffix (the card 043 path takes "shot showui",
// which cannot name the file). The engine writes Saved/Screenshots/Windows/<tag>-<name>.png on its next frame.
const char* c63_shot(const char* name) {
    wchar_t file[kC63ShotChars];
    const int n = swprintf(file, kC63ShotChars, L"%ls-%hs", g_c63.tag[0] != L'\0' ? g_c63.tag : L"UBWCoop", name);
    if (n <= 0) {
        return "shot name does not fit";
    }
    const FStringRaw s{file, n + 1, n + 1};
    g_request_screenshot(&s, true, false, false);
    return nullptr;
}

// Card 222: `winkey <K> [hold_ms]` posts WM_KEYDOWN, then WM_KEYUP hold_ms later, to the game window: the OS route a
// real key takes (PumpMessages, FSlateApplication::OnKeyDown), so a key reaches the focused UMG widget (a menu's
// NativeOnKeyDown) where `key` (the PlayerController's InputKey) does not. With the game window not active, the
// game's Slate preprocessor ADInputDetectionProcessor::HandleKeyDownEvent (exe+0x04C5ED40) eats every key down unless
// the handle-input-when-not-active flag is set, so winkey sets it for the length of the press and then puts back the
// value it read. Key ups are not gated. Game thread only (c63_run).
struct C222VKey {
    const wchar_t* name; // the FKey name (`winkey` takes the same names as `key`)
    uint8_t vk;
    uint8_t scan;
    bool extended; // lParam bit 24
};
constexpr C222VKey kC222Keys[] = {
    {L"Escape", VK_ESCAPE, 0x01, false}, {L"Enter", VK_RETURN, 0x1C, false}, {L"SpaceBar", VK_SPACE, 0x39, false},
    {L"Tab", VK_TAB, 0x0F, false},       {L"BackSpace", VK_BACK, 0x0E, false}, {L"Up", VK_UP, 0x48, true},
    {L"Down", VK_DOWN, 0x50, true},      {L"Left", VK_LEFT, 0x4B, true},       {L"Right", VK_RIGHT, 0x4D, true},
};
constexpr size_t kC222AppActive = 0x1E0;      // FSlateApplication::bAppIsActive (IsActive, exe+0x017F7DC0)
constexpr size_t kC222NotActiveInput = 0x504; // the flag HandleKeyDownEvent and FCommonInputPreprocessor read
constexpr ULONGLONG kC222RestoreMs = 100;     // the flag goes back this long after the key up was posted
struct C222 {
    winmsg::User32 u;
    bool resolved;
    bool owned;       // this press set the flag; cleared when it is put back
    uint8_t old_flag; // the flag's value read at the key down
    bool up_posted;
    const C222VKey* key;
    HWND window;
    uint8_t* slate;
    ULONGLONG down_at;
    ULONGLONG up_at;
};
C222 g_c222{};

const C222VKey* c222_vkey(const wchar_t* name) {
    for (const C222VKey& k : kC222Keys) {
        if (wcscmp(k.name, name) == 0) {
            return &k;
        }
    }
    return nullptr;
}

LPARAM c222_lparam(const C222VKey& k, bool up) {
    const LPARAM down = 1 | (static_cast<LPARAM>(k.scan) << 16) | (k.extended ? (static_cast<LPARAM>(1) << 24) : 0);
    return up ? down | static_cast<LPARAM>(0xC0000000u) : down;
}

// Puts the flag back when this press set it; returns the flag as it is now.
uint8_t c222_restore() {
    if (g_c222.owned) {
        g_c222.slate[kC222NotActiveInput] = g_c222.old_flag;
        g_c222.owned = false;
    }
    return g_c222.slate[kC222NotActiveInput];
}

// The key down of q[0] (WinKey). Null: posted; else why it failed (nothing left set).
const char* c222_down(const cmdfile::Cmd& c, ULONGLONG now) {
    if (g_c222.owned) {
        return "a winkey still holds the not-active input flag";
    }
    g_c222.key = c222_vkey(c.key);
    if (g_c222.key == nullptr) {
        return "winkey knows Escape, Enter, SpaceBar, Tab, BackSpace, Up, Down, Left, Right";
    }
    if (!g_c222.resolved && !winmsg::resolve(&g_c222.u, "card222", "no winkey")) {
        return "user32 not resolved";
    }
    g_c222.resolved = true;
    g_c222.window = winmsg::own_window(g_c222.u);
    g_c222.slate = *reinterpret_cast<uint8_t* const*>(g_exe_base + offsets::FSlateApplication_CurrentApplication.rva);
    if (g_c222.window == nullptr || g_c222.slate == nullptr) {
        return g_c222.window == nullptr ? "no visible game window" : "no FSlateApplication";
    }
    const uint8_t active = g_c222.slate[kC222AppActive];
    g_c222.old_flag = g_c222.slate[kC222NotActiveInput];
    if (active == 0 && g_c222.old_flag == 0) {
        g_c222.slate[kC222NotActiveInput] = 1;
        g_c222.owned = true;
    }
    if (g_c222.u.post(g_c222.window, WM_KEYDOWN, g_c222.key->vk, c222_lparam(*g_c222.key, false)) == 0) {
        c222_restore();
        return "PostMessageW(WM_KEYDOWN) failed";
    }
    g_c222.down_at = now;
    g_c222.up_posted = false;
    log::line("card222: %s t=%llu winkey %ls down: VK 0x%02X, hold %u ms, window %p, Slate active %u, not-active input "
              "%u -> %u", side(), now, g_c222.key->name, g_c222.key->vk, c.ms, static_cast<void*>(g_c222.window),
              active, g_c222.old_flag, g_c222.slate[kC222NotActiveInput]);
    return nullptr;
}

// One tick of a started WinKey: the key up at the hold's end, the flag back kC222RestoreMs later. True when done.
bool c222_step(ULONGLONG now, const char** why) {
    if (!g_c222.up_posted) {
        if (now < g_c63.until) {
            return false;
        }
        g_c222.up_posted = true;
        g_c222.up_at = now;
        if (g_c222.u.post(g_c222.window, WM_KEYUP, g_c222.key->vk, c222_lparam(*g_c222.key, true)) == 0) {
            *why = "PostMessageW(WM_KEYUP) failed";
            c222_restore();
            return true;
        }
        log::line("card222: %s t=%llu winkey %ls up after %llu ms", side(), now, g_c222.key->name,
                  now - g_c222.down_at);
        return false;
    }
    if (now < g_c222.up_at + kC222RestoreMs) {
        return false;
    }
    const bool was_owned = g_c222.owned;
    const uint8_t flag = c222_restore();
    log::line("card222: %s t=%llu winkey %ls: not-active input %s %u -- done", side(), now, g_c222.key->name,
              was_owned ? "restored to" : "left at", flag);
    return true;
}

// Starts q[0]. Returns true when it also finished (its result is logged).
bool c63_start(void* world, void* pc, ULONGLONG now) {
    cmdfile::Cmd& c = g_c63.q[0];
    switch (c.op) {
    case cmdfile::Op::Key:
        if (!autoinput::key_event(pc, c.key, true)) {
            c63_finish("no local player controller");
            return true;
        }
        break;
    case cmdfile::Op::WinKey: { // card 222: the key up and the flag's restore follow in c63_continue
        const char* const why = c222_down(c, now);
        if (why != nullptr) {
            c63_finish(why);
            return true;
        }
        break;
    }
    case cmdfile::Op::Axis:
        break; // the first axis event goes out in c63_continue this tick
    case cmdfile::Op::Shot:
        c319_shot_row(world, c.name); // b232: the radar gates at every shot, both sides
        c494_weapon_row(g_get_player_controller(world, 0), c.name); // card 494: the drawn weapon, both sides (card 495)
        c63_finish(c63_shot(c.name));
        return true;
    case cmdfile::Op::Look: {
        if (c.want[0] != '\0') { // b14: the focus scan, then the aim rows
            const char* const scan_why = b14_start(world, pc, c);
            if (scan_why != nullptr) {
                c63_finish(scan_why);
                return true;
            }
            g_c63.started = true;
            g_c63.until = now + kB14CapMs;
            return false;
        }
        const char* const why = c64_look(world, pc, c);
        if (why != nullptr) {
            c63_finish(why);
            return true;
        }
        g_c63.started = true; // card 124: c124_look_step turns the view from the next tick; card 264: a snap waits
        g_c63.until = now + (c.smooth ? kC124LookMs : kC264SettleMs); // kC264SettleMs, then the aim rows
        return false;
    }
    case cmdfile::Op::Clear: { // card 265
        const char* const why = c265_start(pc, c, now);
        if (why != nullptr) {
            c63_finish(why);
            return true;
        }
        g_c63.started = true;
        return false;
    }
    case cmdfile::Op::Warp:
        c63_finish(c75_warp(world, pc, c));
        return true;
    case cmdfile::Op::Kill:
        c63_finish(c98_kill(world, pc, c));
        return true;
    case cmdfile::Op::Spawn:
        c63_finish(g_client_session ? "spawn is host only"
                                    : c125_spawn_bug(world, Vec3{c.at[0], c.at[1], c.at[2]}, c.radius, c.actor));
        return true;
    case cmdfile::Op::Down:
        c63_finish(c109_down(world, c));
        return true;
    case cmdfile::Op::Travel:
        c63_finish(c.at[0] == 5.0   ? c374_join()
                   : c.at[0] == 4.0 ? c345_end_level(world)
                   : c.at[0] == 3.0 ? c143_quicksave(world)
                   : c.at[0] == 2.0 ? c132_checkpoint(world)
                                    : c113_travel(world, c.at[0] != 0.0));
        return true;
    case cmdfile::Op::Speed:
        c63_finish(g_client_session ? "speed is host only" : c130_speed(world, c.at[0]));
        return true;
    case cmdfile::Op::Inventory:
        c63_finish(c132_inventory(pc, c.seq));
        return true;
    case cmdfile::Op::ClearKill:
        c63_finish(c265_clear_kill(world, pc, c)); // user rule 2026-11-13, test only
        return true;
    case cmdfile::Op::AmmoFill:
        c63_finish(c243_ammo_fill(pc, c.seq)); // card 243, test only
        return true;
    case cmdfile::Op::God:
        g_c132_god_host_only = c.at[0] != 0.0;
        log::line("card132: %s god %s: %s", side(), g_c132_god_host_only ? "host" : "all",
                  g_c132_god_host_only ? "the client's pawn takes damage, its Health changes logged"
                                       : "every player pawn under UBWCOOP_GOD");
        c63_finish(g_c132_god || g_client_session ? nullptr : "god needs UBWCOOP_GOD=1 on the host");
        return true;
    case cmdfile::Op::Stamina: // card 490
        if (!g_client_session) {
            c490_set_host(world, c.at[0] != 0.0, "cmd");
        }
        c63_finish(g_client_session ? "stamina is host only" : nullptr);
        return true;
    case cmdfile::Op::GrenadeFill:
        c63_finish(c234_grenade_fill(world, static_cast<int32_t>(c.at[0]), c.seq)); // b234, test only
        return true;
    case cmdfile::Op::Discard:
        c63_finish(c281_discard_arm(static_cast<uint32_t>(c.at[0]), c.seq)); // card 281 (b187), test only
        return true;
    case cmdfile::Op::OffShot:
        c63_finish(g_client_session ? "offshot is host only" : c132_off_shot_arm());
        return true;
    case cmdfile::Op::Autoplay:
        c63_finish(c126_set(pc, c.at[0] != 0.0, now));
        return true;
    case cmdfile::Op::Charge: { // card 324: `host charge client ...` sets the server copy's slot (test only)
        void* const own = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
        void* const pawn = c.copy == 0 ? own : !g_client_session && own != nullptr ? c29_server_copy_nth(own, 0) : nullptr;
        c63_finish(pawn != nullptr ? c106_set_charge(pawn, static_cast<int32_t>(c.at[0]), static_cast<int32_t>(c.at[1]),
                                                     c.at[2] != 0.0)
                   : c.copy != 0   ? "charge client is host only and needs the server copy of the client's pawn"
                                   : "no local pawn");
        return true;
    }
    case cmdfile::Op::Goto: { // card 124: the walk goes on in the background; its result comes at its end
        if (g_c64.running) {
            c64_end(pc, "replaced", now);
        }
        const char* const why = c64_goto_start(world, pc, c, now);
        if (why != nullptr) {
            c64_keys_up(pc);
            c63_finish(why);
            return true;
        }
        log::line("cmd: %u goto: started", c.seq);
        c63_pop();
        return true;
    }
    case cmdfile::Op::Ui: {
        if (c.actor[0] != '\0') {
            c63_finish(c121_ui_click(c.actor));
            return true;
        }
        c67_ui_start();
        const char* why = nullptr;
        if (c67_ui_tick(world, now, &why)) {
            c63_finish(why);
            return true;
        }
        g_c63.started = true;
        g_c63.until = now + kC67UiTimeoutMs;
        return false;
    }
    case cmdfile::Op::Quit:
        log::line("cmd: %u quit: RequestEngineExit", c.seq);
        sync_quit(world); // card 326/332: the teardown state, then the effects, gib and dropship totals before the exit
        g_request_exit(L"UBWCoop card 063 quit command");
        c63_finish(nullptr);
        return true;
    case cmdfile::Op::Crash: {
        // Card 118, test only: write through a null-based pointer so the crash trace logs a real fault.
        log::line("cmd: %u crash av: writing to address 0x118 (test crash)", c.seq);
        log::flush();
        volatile uint32_t* const bad = reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(0x118));
        *bad = 118u;
        c63_finish("crash av returned");
        return true;
    }
    case cmdfile::Op::Disconnect:
        log::line("cmd: %u disconnect: console command \"disconnect\"", c.seq);
        c63_finish(c56_disconnect(pc) ? nullptr : "no player controller");
        return true;
    case cmdfile::Op::Objectives:
        c98_dump("objectives command");
        c132_inventory(world, "objectives command"); // card 132 step 1
        c132_digest_now(world);                      // card 132 step 2
        c132_ledger_report("objectives command");    // card 132 ledger
        c104_dump();
        c63_finish(c66_dump(world, c.seq));
        return true;
    case cmdfile::Op::Complete:
        c63_finish(c95_complete(world, pc, c.actor));
        return true;
    case cmdfile::Op::ObjectiveNow: // card 284
        c63_finish(c284_now(world, pc, c.seq));
        return true;
    case cmdfile::Op::ObjectiveNext: // b238
        c63_finish(c284_next(world, pc, c.seq));
        return true;
    case cmdfile::Op::ObjectiveExpect: { // doable now: passes now; else c63_continue polls it until c.ms is up
        const char* why = nullptr;
        if (c284_expect_step(world, pc, c.actor, false, c.seq, &why)) {
            c63_finish(why);
            return true;
        }
        break;
    }
    case cmdfile::Op::ObjectiveGo: { // warped: c63_continue rows the settled positions once c.ms is up
        const char* const why = c284_go_start(world, pc, c.copy, c.seq);
        if (why != nullptr) {
            c63_finish(why);
            return true;
        }
        break;
    }
    case cmdfile::Op::ObjectiveWait: { // complete already: done now; else c63_continue polls it until c.ms is up
        const char* why = nullptr;
        if (c284_wait_step(world, pc, c.actor, false, &why)) {
            c63_finish(why);
            return true;
        }
        break;
    }
    case cmdfile::Op::Door:
        c63_finish(c95_cmd(world, read_ptr(pc, kControllerPawn), c.actor, c.door));
        return true;
    case cmdfile::Op::LevelCall: // b184
        c63_finish(c237_levelcall(world, c));
        return true;
    }
    g_c63.started = true;
    g_c63.until = now + c.ms;
    g_c63.chord = 0;
    return false;
}

// Card 107: the chord's second key goes down at_ms into the hold and up kC107TapMs later, or at the hold's end
// (whichever is first; a late tick can press and release it together). Called each tick of a started chord, before the main key's release check.
constexpr ULONGLONG kC107TapMs = 100;
void c107_chord_step(void* pc, const cmdfile::Cmd& c, ULONGLONG now) {
    const ULONGLONG down_at = g_c63.until - c.ms + c.at_ms;
    if (g_c63.chord == 0 && now >= down_at) {
        g_c63.chord = 1;
        if (!autoinput::key_event(pc, c.key2, true)) {
            log::line("WARN card107: chord key down failed (no local player controller)");
        }
    }
    if (g_c63.chord == 1 && (now >= down_at + kC107TapMs || now >= g_c63.until)) {
        g_c63.chord = 2;
        if (!autoinput::key_event(pc, c.key2, false)) {
            log::line("WARN card107: chord key up failed (no local player controller)");
        }
    }
}

// One tick of the started q[0]: an axis event, or the key release once its hold is over.
void c63_continue(void* world, void* pc, ULONGLONG now) {
    const cmdfile::Cmd& c = g_c63.q[0];
    if (c.op == cmdfile::Op::Look && c.want[0] != '\0') { // b14: the focus scan
        const char* why = nullptr;
        if (b14_step(pc, c, now, &why)) {
            c63_finish(why);
        }
        return;
    }
    if (c.op == cmdfile::Op::Look) { // card 124: look ... smooth; card 264: the aim rows when the look ends
        const char* why = nullptr;
        if (!c.smooth ? now >= g_c63.until : c124_look_step(pc, now, g_c63.until, &why)) {
            c264_aim_rows(pc, c.seq);
            c310_centre_row(world, pc, c);
            c63_finish(why);
        }
        return;
    }
    if (c.op == cmdfile::Op::Clear) { // card 265
        c265_step(world, pc, c, now);
        return;
    }
    if (c.op == cmdfile::Op::Ui) {
        const char* why = nullptr;
        if (c67_ui_tick(world, now, &why)) {
            c63_finish(why);
        } else if (now >= g_c63.until) {
            c63_finish(c67_ui_timeout(world));
        }
        return;
    }
    if (c.op == cmdfile::Op::ObjectiveExpect) { // card 284 review 1
        const char* why = nullptr;
        if (c284_expect_step(world, pc, c.actor, now >= g_c63.until, c.seq, &why)) {
            c63_finish(why);
        }
        return;
    }
    if (c.op == cmdfile::Op::ObjectiveGo) { // card 284 review 3: the settle
        if (now >= g_c63.until) {
            c63_finish(c284_go_settle(world, pc, c.seq));
        }
        return;
    }
    if (c.op == cmdfile::Op::ObjectiveWait) { // card 284
        const char* why = nullptr;
        if (c284_wait_step(world, pc, c.actor, now >= g_c63.until, &why)) {
            c63_finish(why);
        }
        return;
    }
    if (c.op == cmdfile::Op::WinKey) {
        const char* why = nullptr;
        if (c222_step(now, &why)) {
            c63_finish(why);
        }
        return;
    }
    if (c.op == cmdfile::Op::Axis) {
        if (now >= g_c63.until) {
            c63_finish(nullptr);
        } else if (!autoinput::axis_event(pc, c.key, c.value)) {
            c63_finish("no local player controller");
        }
        return;
    }
    if (c.key2[0] != L'\0') {
        c107_chord_step(pc, c, now);
    }
    if (now >= g_c63.until) {
        c63_finish(autoinput::key_event(pc, c.key, false) ? nullptr : "no local player controller at the release");
    }
}

void c63_run(void* world, void* pc, ULONGLONG now) {
    for (int32_t i = 0; i < kC63Queue && g_c63.count > 0; ++i) {
        if (g_c63.started) {
            c63_continue(world, pc, now);
            return; // a started command spans ticks; the next one starts next tick at the earliest
        }
        if (!c63_start(world, pc, now)) {
            if (g_c63.q[0].op == cmdfile::Op::Axis) {
                c63_continue(world, pc, now); // the first axis event; a key is released on a later tick, never this one
            }
            return;
        }
    }
}

// The local pawn's part of the state line: kind, location, view, health, weapons. Unknown fields are "-".
int c63_pawn_text(void* pc, char* out, size_t cap) {
    const void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    if (pawn == nullptr) {
        return snprintf(out, cap, "pawn=none x=- y=- z=- yaw=- pitch=- hp=- weapons=- equipped=-");
    }
    // b255 (c245b254a mech round): BP_BaseMech_C derives from the player character class, so a piloted mech read as
    // host-own; a seat (mech or turret) is pawn=other.
    const bool mech = g_mech_class != nullptr && is_a(pawn, g_mech_class());
    const bool player = !mech && is_a(pawn, g_player_character_class());
    const Vec3 at = actor_location(pawn);
    const Vec3 rot = read_vec3(pc, kControllerControlRotation); // pitch, yaw, roll
    void* component = nullptr;
    const float hp = player ? c21_health(static_cast<const uint8_t*>(pawn), &component) : -1.0f;
    void* const inventory = is_a(pawn, g_player_mi_class()) ? read_ptr(pawn, kMiWeaponInventory) : nullptr;
    const C50List l = inventory != nullptr ? c50_list(inventory) : C50List{};
    char hp_text[16] = "-";
    char weapons[32] = "weapons=- equipped=-";
    if (hp >= 0.0f) {
        snprintf(hp_text, sizeof(hp_text), "%.1f", hp);
    }
    if (inventory != nullptr) {
        snprintf(weapons, sizeof(weapons), "weapons=%d equipped=%d", l.count, l.equipped);
    }
    const char* const kind = !player ? "other" : (g_client_session ? "client-own" : "host-own");
    return snprintf(out, cap, "pawn=%s x=%.0f y=%.0f z=%.0f yaw=%.1f pitch=%.1f hp=%s %s", kind, at.x, at.y, at.z,
                    rot.y, rot.x, hp_text, weapons);
}

void c63_write_state(void* engine, ULONGLONG now) {
    void* const world = g_get_game_world(engine);
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    char pawn[kC63StateChars];
    if (c63_pawn_text(pc, pawn, sizeof(pawn)) <= 0) {
        memcpy(pawn, "pawn=-", 7);
    }
    char goal[96];
    if (c64_state_text(goal, sizeof(goal)) <= 0) {
        memcpy(goal, " goal=- dist=-", 15);
    }
    char text[kC63StateChars];
    const int n = snprintf(text, sizeof(text), "t=%llu map=%s role=%s %s objective=- cmd_seq=%u%s\n",
                           static_cast<unsigned long long>(now), world != nullptr ? object_name(world).text : "-",
                           g_client_session ? "client" : "host", pawn, g_c63.last_seq, goal);
    if (n > 0 && static_cast<size_t>(n) < sizeof(text)) {
        cmdfile::write_state(text, static_cast<size_t>(n));
    }
}

// Card 056 test: the PC's ConsoleCommand("disconnect"); on a client, card 045's follow then reconnects.
bool c56_disconnect(void* pc) {
    static const wchar_t kDisconnect[] = L"disconnect";
    const FStringRaw cmd{kDisconnect, 11, 11};
    FStringRaw out{};
    if (pc == nullptr) {
        return false;
    }
    g_pc_console_command(pc, &out, &cmd, false);
    if (out.data != nullptr) {
        g_memory_free(const_cast<wchar_t*>(out.data));
    }
    return true;
}

// Card 066 test command `objectives`, both sides: the list panel's items (as c53_dump logs them) and, in map order,
// every objective's id, parent, completion and count, so the runner can compare host and client. Null or why not.
const char* c66_dump(void* world, uint32_t seq) {
    const void* const subsystem = world != nullptr ? g_c49_get_subsystem(world) : nullptr;
    SparseView v{};
    if (!c49_objectives(subsystem, v)) {
        return "no objective map";
    }
    const uint8_t* const panel = c49_panel(static_cast<const uint8_t*>(subsystem));
    const uint8_t* const box = panel != nullptr ? static_cast<const uint8_t*>(read_ptr(panel, 0x358)) : nullptr;
    log::line("card066: %s t=%llu cmd %u objectives: ObjectiveListItems %d, box slots %d, objectives %d", side(),
              static_cast<unsigned long long>(GetTickCount64()), seq, panel != nullptr ? read_i32(panel, 0x370 + kArrayNum) : -1,
              box != nullptr ? read_i32(box, 0x168 + kArrayNum) : -1, c49_objective_count(subsystem));
    if (box != nullptr) {
        c49_item_lines(box, 0);
    }
    for (int32_t i = 0; i < v.count; ++i) {
        const uint8_t* const element = v.data + static_cast<size_t>(i) * kObjectiveElementBytes;
        const auto* const o = static_cast<const uint8_t*>(sparse_allocated(v, i) ? read_ptr(element, kObjectiveElementValue) : nullptr);
        if (o == nullptr) {
            continue;
        }
        const auto* const parent = static_cast<const uint8_t*>(read_ptr(o, kObjectiveParent));
        const bool count = is_a(o, g_c49_count_class());
        log::line("card066: %s objective %s: parent %s, complete %u, count %d of %d", side(), fname_text(o + kObjectiveId).text,
                  parent != nullptr ? fname_text(parent + kObjectiveId).text : "none", o[kObjectiveComplete],
                  count ? read_i32(o, kObjectiveCountCurrent) : -1, count ? read_i32(o, kObjectiveCountTarget) : -1);
    }
    return nullptr;
}

// Card 284 (b7b-2 probe): a named sub-objective completed by command whose parent completes with it was the parent's
// last open sub-objective. In real play a script-chained step (the C2M1 LOS chain) has its level script add the next
// sub-objective in the same tick as the completion, so the parent stays open; the command skips that add, and the
// chain ends. The WARN names the skip so a scenario failure later on points here.
static void c284_warn_chain_skip(void* world, uint64_t parent_id, uint64_t id) {
    SparseView v{};
    if (!c49_objectives(world != nullptr ? g_c49_get_subsystem(world) : nullptr, v)) {
        return;
    }
    for (int32_t i = 0; i < v.count; ++i) {
        const uint8_t* const e = v.data + static_cast<size_t>(i) * kObjectiveElementBytes;
        const auto* const o = static_cast<const uint8_t*>(sparse_allocated(v, i) ? read_ptr(e, kObjectiveElementValue) : nullptr);
        if (o == nullptr || read_u64(o, kObjectiveId) != parent_id) {
            continue;
        }
        if (o[kObjectiveComplete] != 0) {
            log::line("WARN card284: host t=%llu cmd objective complete %s also completed its parent %s: the level script's "
                      "own next add for a script-chained step did not run (completed by command), so the chain may end here",
                      static_cast<unsigned long long>(GetTickCount64()), fname_text(&id).text, fname_text(&parent_id).text);
        }
        return;
    }
}

// Card 095 test command `objective complete <id>|all`, host only: completes the named incomplete objective, or every
// incomplete one (sub-objectives first, then the top-level ones), through the card 049 detours, so each call runs and
// is mirrored like the level script's own. A sub-objective goes through CompleteSubObjective(parent, id) (see
// c49_test_step). Null or why not.
const char* c95_complete(void* world, void* pc, const char* id_text) {
    if (g_client_session) {
        return "host only";
    }
    SparseView v{};
    if (!c49_objectives(world != nullptr ? g_c49_get_subsystem(world) : nullptr, v)) {
        return "no objective map";
    }
    const bool all = strcmp(id_text, "all") == 0;
    wchar_t wide[kChanNameMax + 1];
    size_t w = 0;
    for (; id_text[w] != '\0' && w < kChanNameMax; ++w) {
        wide[w] = static_cast<wchar_t>(static_cast<unsigned char>(id_text[w]));
    }
    wide[w] = L'\0';
    uint64_t want = 0;
    g_fname_ctor(&want, wide, kFNameAdd);
    constexpr int32_t kMax = 64;
    uint64_t ids[kMax][2]; // {parent id or 0, id}; collected first: a completion can change the map
    bool parent_open[kMax]; // card 284: the parent was incomplete before the command
    int32_t n = 0;
    int32_t done_already = 0; // the named id, complete before the command (the level script got there first)
    for (int32_t pass = 0; pass < 2; ++pass) { // pass 0 sub-objectives, pass 1 top-level
        for (int32_t i = 0; i < v.count && n < kMax; ++i) {
            const uint8_t* const e = v.data + static_cast<size_t>(i) * kObjectiveElementBytes;
            const auto* const o = static_cast<const uint8_t*>(sparse_allocated(v, i) ? read_ptr(e, kObjectiveElementValue) : nullptr);
            const void* const parent = o != nullptr ? read_ptr(o, kObjectiveParent) : nullptr;
            if (o != nullptr && !all && pass == 0 && o[kObjectiveComplete] != 0 && read_u64(o, kObjectiveId) == want) {
                ++done_already;
            }
            if (o == nullptr || o[kObjectiveComplete] != 0 || (parent != nullptr) != (pass == 0) ||
                (!all && read_u64(o, kObjectiveId) != want)) {
                continue;
            }
            parent_open[n] = parent != nullptr && static_cast<const uint8_t*>(parent)[kObjectiveComplete] == 0;
            ids[n][0] = parent != nullptr ? read_u64(parent, kObjectiveId) : 0;
            ids[n++][1] = read_u64(o, kObjectiveId);
        }
    }
    for (int32_t i = 0; i < n; ++i) {
        log::line("card095: host t=%llu cmd objective complete: %s(%s%s%s)", static_cast<unsigned long long>(GetTickCount64()),
                  ids[i][0] != 0 ? "CompleteSubObjective" : "CompleteObjective", ids[i][0] != 0 ? fname_text(&ids[i][0]).text : "",
                  ids[i][0] != 0 ? ", " : "", fname_text(&ids[i][1]).text);
        (ids[i][0] != 0) ? detour_c49_complete_sub(pc, ids[i][0], ids[i][1]) : detour_c49_complete(pc, ids[i][1]);
        if (!all && parent_open[i]) {
            c284_warn_chain_skip(world, ids[i][0], ids[i][1]);
        }
    }
    // Card 140 run 7: a scenario's fallback complete can land just after the level script's own (c140g: the rack
    // resupply completed TacSupport_SO41 99 ms before cmd 13). The objective is complete either way: done.
    log::line("card095: host cmd objective complete %s: %d completed, %d complete already", id_text, n, done_already);
    return n != 0 || done_already != 0 ? nullptr : "no incomplete objective with that id";
}

// UBWCOOP_CMD=1, after UGameEngine::Init (before any game map loads): a net client then builds a navigation system
// from the level's baked navmesh. The shipped DefaultEngine.ini has bAllowClientSideNavigation=False; the module
// config copies it into bCreateOnClient when a map's world settings load, so both CDOs are set.
void c64_allow_client_nav() {
    c64_set_cdo_bool(kC64NavSysClass, "bAllowClientSideNavigation");
    c64_set_cdo_bool(L"/Script/NavigationSystem.NavigationSystemModuleConfig", "bCreateOnClient");
}

// Game thread, every tick with UBWCOOP_CMD=1, before the engine Tick (keys land in this frame's PC tick).
void c63_step(void* engine) {
    const ULONGLONG now = GetTickCount64();
    c67_step(engine, now);
    if (now >= g_c63.next_poll) {
        g_c63.next_poll = now + kC63PollMs;
        g_c63.count += cmdfile::poll(&g_c63.q[g_c63.count], kC63Queue - g_c63.count);
    }
    if (g_c63.count > 0 || g_c64.running || c126_on() || g_c265w.armed) {
        void* const world = g_get_game_world(engine);
        void* const pc = world != nullptr ? own_pc(world) : nullptr;
        c265_watch(world, pc, now); // card 265: host death rows by guid, every class, whatever the queue is doing
        if (g_c64.running) {
            c64_walk_tick(world, pc, now); // card 124: the walk, whatever the queue is doing
        }
        if (c126_on()) {
            c126_tick(world, pc, now); // card 126: autoplay picks the objective and starts the walks
        }
        c63_run(world, pc, now);
    }
    if (now >= g_c63.next_state) {
        g_c63.next_state = now + kC63StateMs;
        c63_write_state(engine, now);
    }
}

// From read_card_flags: opens the files for this process's log tag. False leaves the runner off.
bool c63_init() {
    const ue::config::LogTag tag = ue::config::read_log_tag();
    if (tag.status == ue::config::TagStatus::Ok) {
        memcpy(g_c63.tag, tag.text, sizeof(g_c63.tag));
    }
    return cmdfile::open(tag.status == ue::config::TagStatus::Ok ? tag.text : nullptr);
}

} // namespace ubw::hooks
