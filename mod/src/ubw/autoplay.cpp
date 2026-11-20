// Card 126 phase 1: autoplay (test runner only, UBWCOOP_CMD=1). `autoplay on|off` makes the local player follow the
// active objective with the card 124 walk and the interact key, the way a player reads the objective marker. No fight
// logic yet, no warp, never completes an objective by command.
//
// Active objective (card 129 prep): a sub-objective (ParentObjective set) that is not complete and has its marker on
// (bShowMarkers). The goal is ObjectiveActors[0]'s location; the nearest active one wins. What to do there is keyed on
// that actor's class (kC126Kinds): walk into a BP_PlayerPresence_C volume; walk up to a switch, look at it and press E.
// Any other class is logged once as `autoplay: <side> blocked: ...`.

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

constexpr size_t kMarkerObjective = 0x2B0;  // ASTLevelObjectiveMarker::LevelObjective (reflected NewProp)
constexpr char kC126MarkerClass[] = "BP_ObjectiveMarkerActor_C";

constexpr ULONGLONG kC126DecideMs = 500;    // objective pick and the walk/press decision
constexpr ULONGLONG kC126ProgressMs = 10000;
constexpr ULONGLONG kC126HiddenMs = 3000;   // the worked-on marker off this long, another on: the script moved on
constexpr ULONGLONG kC126SettleMs = 4000;   // a new step takes over once picked this long (c121b: SO2 after 2.7 s in _2)
constexpr ULONGLONG kC126OverlapMs = 3000;  // at a presence goal with the objective still open: walk again after this
constexpr ULONGLONG kC126PressMs = 1500;    // between E presses at a switch
constexpr ULONGLONG kC126TapMs = 120;       // E held
constexpr int32_t kC126MaxWalks = 4;        // walks per objective before `blocked`
constexpr int32_t kC126MaxPresses = 8;      // E presses per objective before `blocked`
constexpr double kC126PresenceUu = 80.0;    // presence walk radius, halved on each retry
constexpr double kC126UseUu = 150.0;        // switch walk radius
constexpr double kC126ProjectDz = 150.0;    // the root's navmesh point: searched this far up and down (c126e: a floor 456 uu up)
// ...and this far in x and y, tried in turn (c126f: none within 150 of the _2 root, whose floor has a navmesh hole).
constexpr double kC126ProjectUu[] = {150.0, 400.0, 800.0};
constexpr int32_t kC126ProjectTries = static_cast<int32_t>(sizeof(kC126ProjectUu) / sizeof(kC126ProjectUu[0]));
constexpr double kC126ArriveDz = 150.0;     // at the goal: the pawn within this of the navmesh point's z, as well as the radius
constexpr size_t kC126ComponentScale = 0x220; // ComponentToWorld scale (remote_body.cpp kComponentScale)
constexpr double kC126SprintUu = 2000.0;    // LeftShift held on legs longer than this
constexpr double kC126AimDeg = 2.0;         // E only once the view is this close to the switch
// Look heights over the switch's root, tried in turn (c121b: the panel's use trace hit at some of these only).
constexpr double kC126UseDz[] = {100.0, 60.0, 140.0, 30.0};
constexpr int32_t kC126UseDzCount = static_cast<int32_t>(sizeof(kC126UseDz) / sizeof(kC126UseDz[0]));

enum class C126Phase : uint8_t { Walk, Aim, Blocked, Done }; // Done: stands still until the next step settles

// Card 284 5a: kind by ObjectiveActors[0]'s class (rows are data; the class lists are from the C1M1, C3M1, C3M2
// card 070 frames). Not in a row: an enemy pawn is Kill; no actor on a count objective with no sub-objectives is
// Count; no actor, no sub-objectives and an objective marker actor is Marker; all else Other (printed with its
// classes, never guessed).
struct C126KindRow {
    const char* class_part; // matched against the actor's class name
    C126Kind kind;
};
constexpr C126KindRow kC126Kinds[] = {
    {"PlayerPresence", C126Kind::Presence}, // BP_PlayerPresence_C
    {"Switch", C126Kind::Use},              // BP_Switch_C, BP_WallSwitch_C, BP_PanelSwitch_WithoutScreen_C
    {"Bugnest", C126Kind::Nest},            // BP_DestructibleSpawner_Bugnest_HumanGameplay_C
    {"ActorReference", C126Kind::Marker},   // ActorReference_C: the anchor the level script puts the marker on (card 140)
    {"WeaponPickup", C126Kind::Use},        // BP_WeaponPickup_FlameThrower_C (C4M2 Plaza_SO3, card 245): E interact
};

struct C126KindInfo {
    C126Kind kind;
    const char* text;
    const char* needs; // card 284: printed by `objective now`
};
constexpr C126KindInfo kC126KindInfo[] = {
    {C126Kind::Presence, "presence", "a player pawn capsule inside the volume"},
    {C126Kind::Use, "use", "stand within use range, look at it, press E"},
    {C126Kind::Nest, "nest", "the real blast (grenade or charge) on it"},
    {C126Kind::Kill, "kill", "kill those actors"},
    {C126Kind::Marker, "marker", "reach the marker; completed by the level script (see the presence row)"},
    {C126Kind::Count, "count", "the count advances by the level script; defend rounds use host clear 3000 kill"},
    {C126Kind::Other, "other", "no kind row for these classes: void"},
};

struct C126 {
    bool on;
    const void* world;      // key only: a new world drops the objective worked on
    uint64_t id;            // FName of the objective worked on; 0 none
    int32_t slot;           // its slot in the objective map: the later-added objective is the next step
    C126Kind kind;
    C126Phase phase;
    Vec3 goal;              // ObjectiveActors[0]'s location (the switch's look target)
    Vec3 walk_to;           // its navmesh point on the root's floor (c126_project_goal): the walk's goal
    double radius;          // the walk's radius
    int32_t walks;
    int32_t presses;
    ULONGLONG started;      // when the objective became the target
    ULONGLONG wait_until;   // presence: at the goal, overlap awaited until then
    ULONGLONG hidden_since; // the worked-on objective's marker went off then; 0 while it is on
    uint64_t next_id;       // the step that would take over (c126_settled); 0 none
    ULONGLONG next_since;   // first picked then
    ULONGLONG next_decide;
    ULONGLONG next_progress;
    ULONGLONG next_press;
    ULONGLONG key_up;       // E release time; 0 none held
    bool none_logged;
    char actor[kNameChars]; // the goal actor's object name, for the log
    C126Shape shape;        // the presence actor's overlap component (c126_find_shape); kind None: radius arrival
};
C126 g_c126{};

const char* c126_side() {
    return g_client_session ? "client" : "host";
}

double c126_dist2d(const Vec3& a, const Vec3& b) {
    return sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

// ObjectiveActors[0] of objective `o`, or null.
void* c126_actor(const uint8_t* o) {
    if (array_num(o, kObjectiveActors) < 1) {
        return nullptr;
    }
    void* const* const data = static_cast<void* const*>(read_ptr(o, kObjectiveActors));
    return data != nullptr ? data[0] : nullptr;
}

const C126KindInfo& c126_kind_info(C126Kind k) {
    for (const C126KindInfo& r : kC126KindInfo) {
        if (r.kind == k) {
            return r;
        }
    }
    return kC126KindInfo[sizeof(kC126KindInfo) / sizeof(kC126KindInfo[0]) - 1];
}

const char* c126_kind_text(C126Kind k) {
    return c126_kind_info(k).text;
}

const char* c126_kind_needs(C126Kind k) {
    return c126_kind_info(k).needs;
}

// Objectives in the world's objective map whose ParentObjective is `o`.
int32_t c126_children(const void* world, const uint8_t* o) {
    SparseView v{};
    if (!c49_objectives(g_c49_get_subsystem(world), v)) {
        return 0;
    }
    int32_t n = 0;
    for (int32_t i = 0; i < v.count; ++i) {
        const uint8_t* const e = v.data + static_cast<size_t>(i) * kObjectiveElementBytes;
        const auto* const c = static_cast<const uint8_t*>(sparse_allocated(v, i) ? read_ptr(e, kObjectiveElementValue) : nullptr);
        n += c != nullptr && read_ptr(c, kObjectiveParent) == o ? 1 : 0;
    }
    return n;
}

C126Kind c126_kind_of(const void* world, const uint8_t* o) {
    const void* const actor = c126_actor(o);
    if (actor != nullptr) {
        const ObjectName cn = class_name(actor);
        for (const C126KindRow& r : kC126Kinds) {
            if (strstr(cn.text, r.class_part) != nullptr) {
                return r.kind;
            }
        }
        return g_enemy_class != nullptr && is_a(actor, g_enemy_class()) ? C126Kind::Kill : C126Kind::Other;
    }
    if (c126_children(world, o) > 0) {
        return C126Kind::Other; // a container (card 284 review 2): `objective now` prints it as one, never a step
    }
    if (g_c49_count_class != nullptr && is_a(o, g_c49_count_class())) {
        return C126Kind::Count;
    }
    return c126_marker_of(world, o) != nullptr ? C126Kind::Marker : C126Kind::Other;
}

// The BP_ObjectiveMarkerActor_C whose LevelObjective is `o` (card 284: the stand of a step with no actors), or null.
void* c126_marker_of(const void* world, const uint8_t* o) {
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    for (int32_t l = 0; levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const auto* const actors = levels[l] != nullptr ? static_cast<void* const*>(read_ptr(levels[l], kLevelActors)) : nullptr;
        const int32_t num = levels[l] != nullptr ? read_i32(levels[l], kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; actors != nullptr && a < num && a < kMaxLevelActors; ++a) {
            if (actors[a] != nullptr && strcmp(class_name(actors[a]).text, kC126MarkerClass) == 0 &&
                read_ptr(actors[a], kMarkerObjective) == o) { // class first: +0x2B0 only on a marker actor
                return actors[a];
            }
        }
    }
    return nullptr;
}

// The objective with FName `id` in the world's objective map, or null.
const uint8_t* c126_find(const void* world, uint64_t id) {
    SparseView v{};
    if (!c49_objectives(g_c49_get_subsystem(world), v)) {
        return nullptr;
    }
    for (int32_t i = 0; i < v.count; ++i) {
        const uint8_t* const e = v.data + static_cast<size_t>(i) * kObjectiveElementBytes;
        const auto* const o = static_cast<const uint8_t*>(sparse_allocated(v, i) ? read_ptr(e, kObjectiveElementValue) : nullptr);
        if (o != nullptr && read_u64(o, kObjectiveId) == id) {
            return o;
        }
    }
    return nullptr;
}

// The open top-level objective above o (o itself for a root), or null. Bounded: objectives nest at most a few levels.
const uint8_t* c126_root(const uint8_t* o) {
    for (int depth = 0; depth < 8 && o != nullptr; ++depth) {
        const auto* const up = static_cast<const uint8_t*>(read_ptr(o, kObjectiveParent));
        if (up == nullptr) {
            return o[kObjectiveComplete] == 0 ? o : nullptr;
        }
        o = up;
    }
    return nullptr;
}

// The mission step now. The level script adds every parent at load, in its own order (FED_C1M1: Assault_O1,
// ShootingRange_O1, TacSupport_01, while the mission plays TacSupport before ShootingRange), and shows each parent's
// first step marker together between phases (TacSupport_SO0, ShootingRange_SO0, Assault_SO0). The live signal is the
// markers: when the open sub-objectives with their marker on all sit under one open top-level objective, that one is
// active. Otherwise (none, or several parents at once) the open top-level objective in the lowest map slot. Null when
// every top-level objective is complete.
const void* c126_active_parent(const SparseView& v) {
    const uint8_t* first = nullptr;
    const uint8_t* marked = nullptr;
    bool several = false;
    for (int32_t i = 0; i < v.count; ++i) {
        const uint8_t* const e = v.data + static_cast<size_t>(i) * kObjectiveElementBytes;
        const auto* const o = static_cast<const uint8_t*>(sparse_allocated(v, i) ? read_ptr(e, kObjectiveElementValue) : nullptr);
        if (o == nullptr || o[kObjectiveComplete] != 0) {
            continue;
        }
        if (read_ptr(o, kObjectiveParent) == nullptr) {
            first = first != nullptr ? first : o;
            continue;
        }
        const uint8_t* const root = o[kObjectiveMarker] != 0 ? c126_root(o) : nullptr;
        several = several || (root != nullptr && marked != nullptr && root != marked);
        marked = marked != nullptr ? marked : root;
    }
    return marked != nullptr && !several ? marked : first;
}

// Under the active parent: the open sub-objective with an actor added last (highest map slot), whose marker is on
// or (with `keep`) which is the one worked on. Card 284 5a: a step whose parent is an open sub-objective of the active
// parent counts too (count containers with no actor of their own: Beach_SO1, Resort_SO3, Monorail_SO2,
// PowerPlant_SO3 hold their steps one level down). need_actor false (card 284 verbs): steps with no actor count. The level script blinks markers between steps (FED_C1M1: Assault_SO0
// and SO2 swap SetObjectiveVisibility every ~1.5 s), so a marker off alone never ends a step; c126_decide drops the
// worked-on one (keep false) only once its marker has been off kC126HiddenMs. False when there is none.
bool c126_pick(const void* world, uint64_t current, bool keep, bool need_actor, const Vec3& from, C126Pick& out) {
    out = C126Pick{nullptr, nullptr, 0.0, -1};
    SparseView v{};
    if (!c49_objectives(g_c49_get_subsystem(world), v)) {
        return false;
    }
    const void* const parent = c126_active_parent(v);
    if (parent == nullptr) {
        return false;
    }
    for (int32_t i = 0; i < v.count; ++i) {
        const uint8_t* const e = v.data + static_cast<size_t>(i) * kObjectiveElementBytes;
        const auto* const o = static_cast<const uint8_t*>(sparse_allocated(v, i) ? read_ptr(e, kObjectiveElementValue) : nullptr);
        const auto* const up = static_cast<const uint8_t*>(o != nullptr ? read_ptr(o, kObjectiveParent) : nullptr);
        const bool under = up == parent || (up != nullptr && read_ptr(up, kObjectiveParent) == parent &&
                                            up[kObjectiveComplete] == 0);
        if (o == nullptr || !under || o[kObjectiveComplete] != 0) {
            continue;
        }
        const bool is_current = read_u64(o, kObjectiveId) == current;
        if ((is_current && !keep) || (o[kObjectiveMarker] == 0 && !is_current)) {
            continue;
        }
        void* const actor = c126_actor(o);
        if (actor == nullptr && need_actor) {
            continue;
        }
        if (!need_actor && c126_children(world, o) > 0) {
            continue; // card 284 review 2: a container is never a step (autoplay, need_actor true, unchanged)
        }
        const double dist = actor != nullptr ? c126_dist2d(from, actor_location(actor)) : 0.0;
        out = C126Pick{o, actor, dist, i}; // ascending i: the last one wins
    }
    return out.objective != nullptr;
}

// Card 126 open point 1: where the objective's marker actor sits against ObjectiveActors[0]'s root.
void c126_log_marker(const void* world, const uint8_t* o, const void* actor) {
    const Vec3 at = actor_location(actor);
    void* const m = c64_find_actor(world, kC126MarkerClass, at);
    if (m == nullptr) {
        log::line("autoplay: %s marker: no %s in the world", c126_side(), kC126MarkerClass);
        return;
    }
    const Vec3 ml = actor_location(m);
    const auto* const mo = static_cast<const uint8_t*>(read_ptr(m, kMarkerObjective));
    log::line("autoplay: %s marker %s (objective %s%s) at %.0f %.0f %.0f, actor root %.0f %.0f %.0f: offset %.0f %.0f "
              "%.0f (%.0f uu)", c126_side(), object_name(m).text, mo != nullptr ? fname_text(mo + kObjectiveId).text : "none",
              mo == o ? "" : ", not this one", ml.x, ml.y, ml.z, at.x, at.y, at.z, ml.x - at.x, ml.y - at.y, ml.z - at.z,
              vec_dist(ml, at));
}

void c126_release(void* pc) {
    if (g_c126.key_up != 0) {
        autoinput::key_event(pc, L"E", false);
        g_c126.key_up = 0;
    }
}

void c126_blocked(const char* why) {
    log::line("autoplay: %s blocked: objective %s (%s, %s): %s", c126_side(), fname_text(&g_c126.id).text,
              c126_kind_text(g_c126.kind), g_c126.actor, why);
    g_c126.phase = C126Phase::Blocked;
}

// Starts a walk to the goal (seq 0: the walk's end logs no command result).
void c126_walk(void* world, void* pc, const Vec3& pos, ULONGLONG now) {
    if (g_c126.walks >= kC126MaxWalks) {
        char why[96];
        snprintf(why, sizeof(why), "%d walks did not finish it, %.0f uu (2D) and %.0f in z from the goal", g_c126.walks,
                 c126_dist2d(pos, g_c126.walk_to), pos.z - g_c126.walk_to.z);
        c126_blocked(why);
        return;
    }
    cmdfile::Cmd c{};
    c.op = cmdfile::Op::Goto;
    c.at[0] = g_c126.walk_to.x;
    c.at[1] = g_c126.walk_to.y;
    c.at[2] = g_c126.walk_to.z;
    c.radius = static_cast<float>(g_c126.radius);
    c.sprint = c126_dist2d(pos, g_c126.walk_to) > kC126SprintUu;
    snprintf(c.text, sizeof(c.text), "autoplay goto %s", g_c126.actor);
    ++g_c126.walks;
    const char* const why = c64_goto_start(world, pc, c, now);
    log::line("autoplay: %s walk #%d to %s, radius %.0f%s%s%s", c126_side(), g_c126.walks, g_c126.actor, g_c126.radius,
              c.sprint ? ", sprint" : "", why != nullptr ? ": not started: " : "", why != nullptr ? why : "");
}

struct C126ShapeRow {
    const char* class_part;
    C126ShapeKind kind;
    const char* text;
};
constexpr C126ShapeRow kC126Shapes[] = {
    {"BoxComponent", C126ShapeKind::Box, "box"},
    {"SphereComponent", C126ShapeKind::Sphere, "sphere"},
    {"CapsuleComponent", C126ShapeKind::Capsule, "capsule"},
};
constexpr double kC126UprightQ = 0.02; // |quat x|, |quat y| under this: the shape's z axis is the world's (yaw only)

double c126_f64(const void* base, size_t off) {
    double v = 0.0;
    memcpy(&v, static_cast<const uint8_t*>(base) + off, sizeof(v));
    return v;
}

// Reflected float or double property `name` of `obj`; 0 if missing.
double c126_num(const void* obj, const char* name, size_t sub) {
    const uint8_t* const prop = prop_find_prop(read_ptr(obj, kObjectClass), name);
    if (prop == nullptr) {
        return 0.0;
    }
    const size_t off = static_cast<size_t>(read_i32(prop, kPropertyOffset)) + sub;
    ObjectName k{};
    prop_kind(prop, k);
    return strcmp(k.text, "FloatProperty") == 0 ? static_cast<double>(read_f32(obj, off)) : c126_f64(obj, off);
}

// Shape kind of component `c` (kC126Shapes), None if not a shape.
const C126ShapeRow* c126_shape_row(const void* c) {
    const ObjectName cn = class_name(c);
    for (const C126ShapeRow& r : kC126Shapes) {
        if (strstr(cn.text, r.class_part) != nullptr) {
            return &r;
        }
    }
    return nullptr;
}

// World shape of component `c` (row `r`): ComponentToWorld (quat, translation, scale) and the scaled extent.
C126Shape c126_shape_of(const void* c, const C126ShapeRow& r) {
    C126Shape sh{};
    sh.kind = r.kind;
    for (int i = 0; i < 4; ++i) {
        sh.q[i] = c126_f64(c, kComponentRotation + static_cast<size_t>(i) * sizeof(double));
    }
    sh.center = Vec3{c126_f64(c, kComponentTranslation), c126_f64(c, kComponentTranslation + 8),
                     c126_f64(c, kComponentTranslation + 16)};
    const Vec3 sc{fabs(c126_f64(c, kC126ComponentScale)), fabs(c126_f64(c, kC126ComponentScale + 8)),
                  fabs(c126_f64(c, kC126ComponentScale + 16))};
    if (r.kind == C126ShapeKind::Box) {
        sh.ext = Vec3{c126_num(c, "BoxExtent", 0) * sc.x, c126_num(c, "BoxExtent", 8) * sc.y,
                      c126_num(c, "BoxExtent", 16) * sc.z};
    } else if (r.kind == C126ShapeKind::Sphere) {
        sh.ext.x = c126_num(c, "SphereRadius", 0) * fmin(sc.x, fmin(sc.y, sc.z));
    } else {
        sh.ext.x = c126_num(c, "CapsuleRadius", 0) * fmin(sc.x, sc.y);
        sh.ext.z = c126_num(c, "CapsuleHalfHeight", 0) * sc.z;
    }
    return sh;
}

// Card 336: the world shape of component `c` when it is a box, sphere or capsule; false (out kind None) otherwise.
bool c126_shape_comp(const void* c, C126Shape& out) {
    const C126ShapeRow* const r = c != nullptr ? c126_shape_row(c) : nullptr;
    out = r != nullptr ? c126_shape_of(c, *r) : C126Shape{};
    return r != nullptr;
}

// The actor's overlap shape: its root if a shape, else the first shape component it owns (reflected properties).
// False (out kind None) when it has none; *comp: the component found (may be null).
bool c126_shape_find(const void* actor, C126Shape& out, const void** comp) {
    out = C126Shape{};
    const void* found = nullptr;
    const void* const root = read_ptr(actor, kActorRootComponent);
    if (root != nullptr && c126_shape_row(root) != nullptr) {
        found = root;
    }
    const void* s = read_ptr(actor, kObjectClass);
    for (int depth = 0; found == nullptr && depth < kPropMaxSupers && s != nullptr; ++depth, s = read_ptr(s, kStructSuper)) {
        const void* f = read_ptr(s, kStructChildProperties);
        for (int i = 0; found == nullptr && i < kPropMaxFields && f != nullptr; ++i, f = read_ptr(f, kFieldNext)) {
            ObjectName kind{};
            if (prop_kind(f, kind) != PropKind::Object || strcmp(kind.text, "ObjectProperty") != 0) {
                continue;
            }
            const void* const v = read_ptr(actor, static_cast<size_t>(read_i32(f, kPropertyOffset)));
            if (v != nullptr && read_ptr(v, kObjectOuter) == actor && c126_shape_row(v) != nullptr) {
                found = v;
            }
        }
    }
    if (comp != nullptr) {
        *comp = found;
    }
    if (found == nullptr) {
        return false;
    }
    out = c126_shape_of(found, *c126_shape_row(found));
    return true;
}

// Autoplay: the shape into g_c126.shape, logged once per objective pick (class, location, extent, rotation). None:
// kind None and a log line.
void c126_find_shape(const void* actor) {
    const void* found = nullptr;
    if (!c126_shape_find(actor, g_c126.shape, &found)) {
        log::line("autoplay: %s %s: no box, sphere or capsule component; arrival by radius", c126_side(), g_c126.actor);
        return;
    }
    const C126ShapeRow& r = *c126_shape_row(found);
    const C126Shape& sh = g_c126.shape;
    log::line("autoplay: %s %s overlap shape %s (%s, %s): at (%.0f, %.0f, %.0f), extent (%.0f, %.0f, %.0f), "
              "quat (%.3f, %.3f, %.3f, %.3f)%s", c126_side(), g_c126.actor, object_name(found).text,
              class_name(found).text, r.text, sh.center.x, sh.center.y, sh.center.z, sh.ext.x, sh.ext.y, sh.ext.z,
              sh.q[0], sh.q[1], sh.q[2], sh.q[3],
              fabs(sh.q[0]) > kC126UprightQ || fabs(sh.q[1]) > kC126UprightQ ? " NOT UPRIGHT: test uses yaw only" : "");
}

// `p` in the shape's frame: translation removed, then the inverse rotation (yaw-upright shapes, see kC126UprightQ).
Vec3 c126_local(const C126Shape& sh, const Vec3& p) {
    const double x = -sh.q[0], y = -sh.q[1], z = -sh.q[2], w = sh.q[3];
    const Vec3 v{p.x - sh.center.x, p.y - sh.center.y, p.z - sh.center.z};
    const Vec3 t{2.0 * (y * v.z - z * v.y), 2.0 * (z * v.x - x * v.z), 2.0 * (x * v.y - y * v.x)};
    return Vec3{v.x + w * t.x + (y * t.z - z * t.y), v.y + w * t.y + (z * t.x - x * t.z),
                v.z + w * t.z + (x * t.y - y * t.x)};
}

// Engine overlap of an upright capsule (radius cr, half height ch, center p) with the shape: the distance from the
// capsule's core segment to the shape's core (box volume, sphere point, capsule segment) within the radii.
bool c126_overlaps(const C126Shape& sh, const Vec3& p, double cr, double ch) {
    const Vec3 l = c126_local(sh, p);
    const double seg = fmax(ch - cr, 0.0); // the capsule's core segment: l.z +- seg
    double dx = 0.0, dy = 0.0, hz = 0.0, reach = cr;
    if (sh.kind == C126ShapeKind::Box) {
        dx = fmax(fabs(l.x) - sh.ext.x, 0.0);
        dy = fmax(fabs(l.y) - sh.ext.y, 0.0);
        hz = sh.ext.z;
    } else {
        dx = l.x;
        dy = l.y;
        hz = sh.kind == C126ShapeKind::Capsule ? fmax(sh.ext.z - sh.ext.x, 0.0) : 0.0;
        reach = cr + sh.ext.x;
    }
    const double dz = fmax(fabs(l.z) - seg - hz, 0.0); // gap between the z ranges
    return dx * dx + dy * dy + dz * dz <= reach * reach;
}

// The pawn's capsule at `pos` overlaps the shape (radius and half height from its root CapsuleComponent).
bool c126_capsule_inside(const C126Shape& sh, const void* pawn, const Vec3& pos) {
    const void* const cap = pawn != nullptr ? read_ptr(pawn, kActorRootComponent) : nullptr;
    if (cap == nullptr) {
        return false;
    }
    return c126_overlaps(sh, pos, c126_num(cap, "CapsuleRadius", 0), c126_num(cap, "CapsuleHalfHeight", 0));
}

// The local pawn's capsule overlaps the autoplay goal's shape.
bool c126_pawn_inside(const void* pc, const Vec3& pos) {
    return c126_capsule_inside(g_c126.shape, pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr, pos);
}

// Half height in world z of shape `sh` (0 for kind None). A tilted box gives its largest extent (conservative).
double c126_shape_dz(const C126Shape& sh) {
    if (sh.kind == C126ShapeKind::Box) {
        const bool upright = fabs(sh.q[0]) < kC126UprightQ && fabs(sh.q[1]) < kC126UprightQ;
        return upright ? sh.ext.z : fmax(sh.ext.x, fmax(sh.ext.y, sh.ext.z));
    }
    return sh.kind == C126ShapeKind::Sphere ? sh.ext.x : (sh.kind == C126ShapeKind::Capsule ? sh.ext.z : 0.0);
}

// The z half extent c126_project searches: kC126ProjectDz, or the shape's whole z extent when larger (b7b-2 probe:
// BP_PlayerPresence_C_2's walkable floor lies outside 150 uu of its centre).
double c126_project_dz(const C126Shape* sh) {
    return sh != nullptr ? fmax(kC126ProjectDz, c126_shape_dz(*sh)) : kC126ProjectDz;
}

// Navmesh point near `target`: kC126ProjectUu in x and y (tried in turn), c126_project_dz(sh) in z. False: none, or
// no pawn.
bool c126_project(const void* world, void* pawn, const Vec3& target, const C126Shape* sh, Vec3& at, double* within) {
    const double dz = c126_project_dz(sh);
    for (int32_t t = 0; pawn != nullptr && t < kC126ProjectTries; ++t) {
        const Vec3 extent{kC126ProjectUu[t], kC126ProjectUu[t], dz};
        if (c64_project(const_cast<void*>(world), pawn, target, extent, at)) {
            if (within != nullptr) {
                *within = kC126ProjectUu[t];
            }
            return true;
        }
    }
    return false;
}

// The walk goal: the goal actor's root projected onto the navmesh within c126_project_dz in z, so the path ends on the
// root's floor and not on a level over or under it (c126e). None there: the objective is blocked with that cause.
void c126_project_goal(const void* world, void* pc) {
    void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    const bool shaped = g_c126.shape.kind != C126ShapeKind::None;
    const Vec3 target = shaped ? g_c126.shape.center : g_c126.goal; // the shape's center, else the root
    Vec3 at{};
    double within = 0.0;
    if (!c126_project(world, pawn, target, shaped ? &g_c126.shape : nullptr, at, &within)) {
        char why[112];
        snprintf(why, sizeof(why), "no navmesh point within %.0f uu (x, y) and %.0f (z) of the root%s",
                 kC126ProjectUu[kC126ProjectTries - 1], c126_project_dz(shaped ? &g_c126.shape : nullptr), pawn == nullptr ? ", no pawn" : "");
        c126_blocked(why);
        return;
    }
    g_c126.walk_to = at;
    log::line("autoplay: %s %s %s (%.0f, %.0f, %.0f) projects to the navmesh at (%.0f, %.0f, %.0f) within %.0f uu "
              "(x, y): %.0f uu (2D), z %+.0f", c126_side(), g_c126.actor, shaped ? "shape center" : "root", target.x,
              target.y, target.z, at.x, at.y, at.z, within, c126_dist2d(at, target), at.z - target.z);
    if (!shaped) {
        return;
    }
    const void* const cap = read_ptr(pawn, kActorRootComponent);
    const double ch = cap != nullptr ? c126_num(cap, "CapsuleHalfHeight", 0) : 0.0;
    const Vec3 stand{at.x, at.y, at.z + ch}; // the pawn's center standing on that point
    if (c126_pawn_inside(pc, stand)) {
        log::line("autoplay: %s %s: the navmesh point lies inside the shape; walking to it", c126_side(), g_c126.actor);
        return;
    }
    g_c126.walk_to = target; // c64_plan: the navmesh path to its nearest point, then a straight leg to the goal
    log::line("autoplay: %s %s: the navmesh point lies outside the shape; walking to it, then a straight last leg to "
              "the shape center (%.0f, %.0f, %.0f)", c126_side(), g_c126.actor, target.x, target.y, target.z);
}

void c126_begin(const void* world, void* pc, const C126Pick& p, ULONGLONG now) {
    if (c64_walking()) {
        c64_end(pc, "autoplay: new objective", now);
    }
    c126_release(pc);
    g_c126.id = read_u64(p.objective, kObjectiveId);
    g_c126.slot = p.slot;
    g_c126.kind = c126_kind_of(world, p.objective);
    g_c126.goal = actor_location(p.actor);
    g_c126.radius = g_c126.kind == C126Kind::Use ? kC126UseUu : kC126PresenceUu;
    g_c126.walks = 0;
    g_c126.presses = 0;
    g_c126.started = now;
    g_c126.wait_until = 0;
    g_c126.hidden_since = 0;
    g_c126.phase = C126Phase::Walk;
    memcpy(g_c126.actor, object_name(p.actor).text, sizeof(g_c126.actor));
    log::line("autoplay: %s objective %s (slot %d) -> %s (%.0f, %.0f, %.0f): %s (%s), %.0f uu", c126_side(),
              fname_text(&g_c126.id).text, p.slot, c126_kind_text(g_c126.kind), g_c126.goal.x, g_c126.goal.y,
              g_c126.goal.z, g_c126.actor, class_name(p.actor).text, p.dist);
    c126_log_marker(world, p.objective, p.actor);
    if (g_c126.kind != C126Kind::Presence && g_c126.kind != C126Kind::Use) {
        c126_blocked("this kind is not in phase 1 (presence and use only)");
        return;
    }
    g_c126.shape = C126Shape{};
    if (g_c126.kind == C126Kind::Presence) {
        c126_find_shape(p.actor);
    }
    c126_project_goal(world, pc);
}

// The objective worked on is done (complete or gone): its time line. A marker off does not end it (see c126_pick).
void c126_check_done(const void* world, void* pc, ULONGLONG now) {
    const uint8_t* const o = c126_find(world, g_c126.id);
    const char* const why = o == nullptr ? "gone" : (o[kObjectiveComplete] != 0 ? "complete" : nullptr);
    if (why == nullptr) {
        return;
    }
    log::line("autoplay: %s objective %s done (%s) in %.1f s: %d walk(s), %d E press(es)", c126_side(),
              fname_text(&g_c126.id).text, why, static_cast<double>(now - g_c126.started) / 1000.0, g_c126.walks,
              g_c126.presses);
    g_c126.phase = C126Phase::Done;
    if (c64_walking()) {
        c64_end(pc, "autoplay: objective done", now);
    }
    c126_release(pc);
    g_c126.id = 0;
}

// Walk phase, at a decision: start or repeat the walk, or move on to the switch.
void c126_walk_step(void* world, void* pc, const Vec3& pos, ULONGLONG now) {
    if (c64_walking()) {
        return;
    }
    const bool shaped = g_c126.shape.kind != C126ShapeKind::None;
    // A shaped presence goal: arrival = the pawn's capsule overlaps the shape (the engine's test); else the radius.
    const bool at_goal = g_c126.walks > 0 &&
                         (shaped ? c126_pawn_inside(pc, pos)
                                 : c126_dist2d(pos, g_c126.walk_to) <= g_c126.radius * 1.5 &&
                                       fabs(pos.z - g_c126.walk_to.z) <= kC126ArriveDz);
    if (at_goal && g_c126.kind == C126Kind::Use) {
        g_c126.phase = C126Phase::Aim;
        g_c126.next_press = now;
        return;
    }
    if (at_goal && g_c126.wait_until == 0) {
        g_c126.wait_until = now + kC126OverlapMs;
        log::line("autoplay: %s at %s (%.0f uu, z %+.0f), objective %s still open: waiting %llu ms for the overlap",
                  c126_side(), g_c126.actor, c126_dist2d(pos, g_c126.walk_to), pos.z - g_c126.walk_to.z,
                  fname_text(&g_c126.id).text,
                  static_cast<unsigned long long>(kC126OverlapMs));
        return;
    }
    if (at_goal && now < g_c126.wait_until) {
        return;
    }
    if (at_goal) {
        g_c126.radius *= 0.5; // closer into the volume
        g_c126.wait_until = 0;
    }
    c126_walk(world, pc, pos, now);
}

// Aim phase, every tick: turn to the switch (the next look height), and press E once the view is on it.
void c126_aim_step(void* pawn, void* pc, const Vec3& pos, ULONGLONG now) {
    if (c126_dist2d(pos, g_c126.goal) > g_c126.radius * 3.0) {
        g_c126.phase = C126Phase::Walk; // pushed away: walk back
        return;
    }
    if (g_c126.presses >= kC126MaxPresses) {
        char why[64];
        snprintf(why, sizeof(why), "%d E presses did not complete it", g_c126.presses);
        c126_blocked(why);
        return;
    }
    ViewPoint view{};
    if (!c24_view_point(pawn, view)) {
        view.loc = pos;
    }
    const double dz = kC126UseDz[g_c126.presses % kC126UseDzCount];
    const Vec3 at{g_c126.goal.x, g_c126.goal.y, g_c126.goal.z + dz};
    const double yaw = atan2(at.y - view.loc.y, at.x - view.loc.x) * kRadToDeg;
    const double pitch = atan2(at.z - view.loc.z, c126_dist2d(at, view.loc)) * kRadToDeg;
    c124_turn(pc, yaw, pitch, now);
    const Vec3 rot = c124_view_rot(pc);
    const bool on = fabs(c124_wrap(yaw - rot.y)) <= kC126AimDeg && fabs(c124_wrap(pitch - rot.x)) <= kC126AimDeg;
    if (!on || now < g_c126.next_press || g_c126.key_up != 0) {
        return;
    }
    ++g_c126.presses;
    autoinput::key_event(pc, L"E", true);
    g_c126.key_up = now + kC126TapMs;
    g_c126.next_press = now + kC126PressMs;
    log::line("autoplay: %s interact %s: E press #%d, look %.0f uu over its root, %.0f uu away", c126_side(),
              g_c126.actor, g_c126.presses, dz, c126_dist2d(pos, g_c126.goal));
}

void c126_progress(const Vec3& pos, ULONGLONG now) {
    static const char* const kPhase[] = {"walk", "aim", "blocked"};
    log::line("autoplay: %s progress: objective %s, %s, %.0f uu from %s, %d walk(s), %d E press(es), %.1f s on it",
              c126_side(), g_c126.id != 0 ? fname_text(&g_c126.id).text : "none",
              g_c126.id != 0 ? kPhase[static_cast<int32_t>(g_c126.phase)] : "idle", c126_dist2d(pos, g_c126.goal),
              g_c126.id != 0 ? g_c126.actor : "-", g_c126.walks, g_c126.presses,
              g_c126.id != 0 ? static_cast<double>(now - g_c126.started) / 1000.0 : 0.0);
}

// The worked-on objective's marker has been off kC126HiddenMs (FED_C1M1: 2.7 s after Assault_SO1 the script hides
// SO2 and shows SO0's marker at BP_PlayerPresence_C_2 when nobody is there; c121b's SO2 completed at that spot).
bool c126_hidden(const void* world, ULONGLONG now) {
    const uint8_t* const o = g_c126.id != 0 ? c126_find(world, g_c126.id) : nullptr;
    if (o == nullptr || o[kObjectiveMarker] != 0) {
        g_c126.hidden_since = 0;
        return false;
    }
    if (g_c126.hidden_since == 0) {
        g_c126.hidden_since = now;
    }
    return now - g_c126.hidden_since >= kC126HiddenMs;
}

// The step `p` has been the takeover pick for kC126SettleMs. The level script shows the next step's marker while the
// player is still needed where they are (FED_C1M1: it shows Assault_SO2 when the player nears BP_PlayerPresence_C_2
// and hides it 1 s after they leave; c121b's SO2 completed after 2.7 s inside _2), so a new step waits.
bool c126_settled(const C126Pick& p, ULONGLONG now) {
    const uint64_t id = read_u64(p.objective, kObjectiveId);
    if (id != g_c126.next_id) {
        g_c126.next_id = id;
        g_c126.next_since = now;
    }
    return now - g_c126.next_since >= kC126SettleMs;
}

// One decision: the objective's done line, the pick, then the walk. The highest slot takes over, or any shown step
// once the script has hidden the worked-on one's marker (c126_hidden); either only once settled (c126_settled).
void c126_decide(void* world, void* pc, const Vec3& pos, ULONGLONG now) {
    if (g_c126.id != 0 && g_c126.phase != C126Phase::Done) {
        c126_check_done(world, pc, now);
    }
    C126Pick p{};
    bool take = false;
    if (c126_hidden(world, now) && c126_pick(world, g_c126.id, false, true, pos, p)) {
        take = true;
    } else if (!c126_pick(world, g_c126.id, true, true, pos, p)) {
        g_c126.next_id = 0;
        if (!g_c126.none_logged) {
            log::line("autoplay: %s no active objective with an actor", c126_side());
            g_c126.none_logged = true;
        }
        return;
    } else {
        take = read_u64(p.objective, kObjectiveId) != g_c126.id && (g_c126.id == 0 || p.slot > g_c126.slot);
    }
    g_c126.none_logged = false;
    if (!take) {
        g_c126.next_id = 0;
    } else if ((g_c126.id == 0 && g_c126.phase != C126Phase::Done) || c126_settled(p, now)) { // first pick: at once
        if (g_c126.hidden_since != 0) {
            log::line("autoplay: %s objective %s: its marker is off %.1f s, the script shows %s", c126_side(),
                      fname_text(&g_c126.id).text, static_cast<double>(now - g_c126.hidden_since) / 1000.0,
                      fname_text(p.objective + kObjectiveId).text);
        }
        g_c126.next_id = 0;
        c126_begin(world, pc, p, now);
    }
    if (g_c126.phase == C126Phase::Walk) {
        c126_walk_step(world, pc, pos, now);
    }
}

bool c126_on() {
    return g_c126.on;
}

const char* c126_set(void* pc, bool on, ULONGLONG now) {
    if (pc == nullptr) {
        return "no local player controller";
    }
    if (!on && c64_walking()) {
        c64_end(pc, "autoplay off", now);
    }
    c126_release(pc);
    g_c126 = C126{};
    g_c126.on = on;
    log::line("autoplay: %s %s", c126_side(), on ? "on" : "off");
    return nullptr;
}

// Game thread, every tick while on (from c63_step, after the walk's own tick).
void c126_tick(void* world, void* pc, ULONGLONG now) {
    void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    if (world == nullptr || pawn == nullptr) {
        return;
    }
    if (world != g_c126.world) {
        g_c126.world = world;
        g_c126.id = 0;
    }
    if (g_c126.key_up != 0 && now >= g_c126.key_up) {
        c126_release(pc);
    }
    const Vec3 pos = actor_location(pawn);
    if (now >= g_c126.next_decide) {
        g_c126.next_decide = now + kC126DecideMs;
        c126_decide(world, pc, pos, now);
    }
    if (g_c126.id != 0 && g_c126.phase == C126Phase::Aim) {
        c126_aim_step(pawn, pc, pos, now);
    }
    if (now >= g_c126.next_progress) {
        g_c126.next_progress = now + kC126ProgressMs;
        c126_progress(pos, now);
    }
}

} // namespace ubw::hooks
