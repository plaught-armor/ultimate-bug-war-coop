// Card 556 b441 diag (both sides, log only): why a resupply rack's leftover pickups stay at shelf height.
//
// 1. ASTCustomPickupBase::SetPhysicsStateOverride (0x051DBB70): the native returns at once when the stored state
//    (+0x460) already equals the argument; otherwise OnUpdatePickupStatus runs the sim setter (0x04C62370), which
//    returns at once when the pickup mesh (+0x2F0) already reports IsSimulatingPhysics equal to the request, so an
//    already simulating, sleeping body is never woken. One row per call for state 1 (the rack's Destroy loop) and the
//    first kC556SpawnRows state 2 calls (SpawnPickups):
//      card556: <side> t=<ms> override <class> id <n> state <s>: stored before <b>, mesh <name> sim before <0|1>,
//               after <0|1>, awake after <0|1>, mesh is root <0|1>
// 2. UKismetSystemLibrary::BoxTraceSingleByProfile (0x03D1CE40) with a resupply rack as world context: the rack's
//    TraceBelow (0.5 s timer from CheckBelow; a miss calls Lifetime, which InstaKills the rack). One row per rack at
//    its first trace and at every hit/miss change:
//      card556: <side> t=<ms> rack <name> trace below <hit|miss> (trace <n>, misses <m>): start (x, y, z) end (x, y,
//               z), profile <name>, hit <component> of <actor> at (x, y, z)
// 3. c556_rack_end_row, from c556_rack_destroy: the end path, from the rack's Duration and its trace record.
//      card556: <side> t=<ms> rack <name> end: <path>; first trace <ms> ago, Duration <s> (<property>), traces <n>,
//               misses <m>, last trace <hit|miss|none>, damage events <n>, last damage <ms> ago by causer <name>
//               (<class>), instigator <name> (<class>)
//    Duration is the rack class's first property whose name starts with "Duration" (a BP variable carries a suffix).
// 4. c556_rack_damage, from detour_c163_broadcast (every UADHealthComponent damage event), receiver a resupply rack;
//    kC556DamageRows per map:
//      card556: <side> t=<ms> rack <name> damage: causer <name> (<class>), instigator <name> (<class>), final <f>
//
// Card 556 fix (host): AADPickupBase::execOnHit (0x04C63100, OnHit inlined) freezes a pickup on its first upward
// blocking contact (+0x400 = 0, SetPhysicsEnabled(false)) and attaches it to the other actor with KeepWorld rules.
// When that actor is destroyed later (a resupply rack in its last 0.1 s after the Destroy loop's override(1), the
// pod it came in, a killable crate), UE detaches the pickup in place and it stays in the air with sim off. The detour
// returns without the freeze and attach when the other actor is such a holder (kC556Holders), or a pickup that still
// simulates or is attached to a holder; the pickup keeps simulating and falls once the holder is gone. Ground and
// world contacts run the original, so the vanilla rest is unchanged. Only calls through ProcessEvent (the hit
// delegate, FFrame::Code null) are read; a bytecode call runs the original. Rows (kC556SkipRows per map):
//      card556: host skipped rack attach: <pickup class> id <n> hit <other name> (<other class>, <why>), holder alive 1
//    and, for a pickup the Destroy loop released (override(1)), its first hit and its freeze (kC556FreezeRows per map):
//      card556: host t=<ms> released pickup <class> id <n> hit <other name> (<other class>): froze <0|1>, attach parent
//               <name>, rack destroyed <0|1>
// Sleep reading: right after the Destroy loop's override(1), the host wakes a body that simulates and sleeps
// (WakeRigidBody); an awake body is left alone. The override row then reads "woke <0|1|-1>".
#include "ubw/hooks_internal.h"

#include <string.h>

namespace ubw::hooks {

C556OverrideFn g_orig_c556_override = nullptr;
C556BoxTraceFn g_orig_c556_box_trace = nullptr;
void (*g_orig_c556_on_hit)(void* ctx, void* frame, void* result) = nullptr;
PrimBodySleepFn g_prim_wake = nullptr;

namespace {

constexpr size_t kPickupOverrideState = 0x460; // ASTCustomPickupBase stored ESTPhysicsOverrideState
constexpr size_t kPickupMesh = 0x2F0;          // the mesh the sim setter 0x04C62370 reads
constexpr size_t kFrameCode = 0x20;            // FFrame::Code (null when called through ProcessEvent, a delegate)
constexpr size_t kFrameLocals = 0x28;          // FFrame::Locals (the parms struct)
constexpr size_t kEventInstigator = 0x0;       // FADDamageEvent::Instigator (marker_c166.cpp layout)
constexpr size_t kEventCauser = 0x8;           // FADDamageEvent::Causer
constexpr size_t kEventReceiver = 0x10;        // FADDamageEvent::Receiver
constexpr size_t kEventFinalDamage = 0x184;    // FADDamageEvent::DamageProcessResult.FinalDamage
constexpr uint64_t kBoneNone = 0;              // FName None: the root body
constexpr uint32_t kC556OverrideRows = 256;    // state 1 rows per process
constexpr uint32_t kC556SpawnRows = 16;        // state 2 rows per process
constexpr uint32_t kC556TraceRows = 256;       // trace rows per process
constexpr uint32_t kC556SkipRows = 24;         // skip rows per map
constexpr uint32_t kC556FreezeRows = 48;       // released pickup hit rows per map
constexpr uint32_t kC556DamageRows = 64;       // rack damage rows per map
constexpr int32_t kC556Racks = 8;              // racks tracked at once (round robin)
constexpr int32_t kC556ClassCache = 32;        // actor classes already classified (world context, OnHit other)
constexpr int32_t kC556Released = 16;          // pickups the Destroy loop released, watched for their hits
constexpr int32_t kC556PropSupers = 8;         // Duration lookup bounds
constexpr int32_t kC556PropFields = 512;
constexpr ULONGLONG kC556DamageEndMs = 1000;   // a damage event this close before the end names the end path

// Actors that hold pickups on them and are destroyed (or hidden) later. A pickup frozen and attached to one stays at
// its height once the holder goes. Index 0 is the resupply rack (the trace and damage rows key on it).
struct C556Holder {
    const char* part; // class name substring
    const char* kind;
};
constexpr C556Holder kC556Holders[] = {
    {"ResupplyRack", "resupply rack"},     // BP_ResupplyRack_Level0/1/..: Destroy loop, then SetLifeSpan(0.1)
    {"TacSupp_Pod", "tac-support pod"},    // BP_TacSupp_Pod_*: the pod a rack or drop comes in, destroyed later
    {"KillableCrate", "killable crate"},   // BP_KillableCrate_*: destroyed by damage
};
constexpr int32_t kC556HolderKinds = static_cast<int32_t>(sizeof(kC556Holders) / sizeof(kC556Holders[0]));
constexpr int8_t kC556NotHolder = -1;

struct C556Trace {
    const void* rack[kC556Racks];
    ULONGLONG first_ms[kC556Racks];
    uint32_t traces[kC556Racks];
    uint32_t misses[kC556Racks];
    int8_t last[kC556Racks]; // -1 none, 0 miss, 1 hit
    uint32_t damages[kC556Racks];
    ULONGLONG damage_ms[kC556Racks]; // the last damage event (0: none)
    ObjectName causer[kC556Racks];
    ObjectName causer_cls[kC556Racks];
    ObjectName instigator[kC556Racks];
    ObjectName instigator_cls[kC556Racks];
    int32_t next;            // round-robin slot
};
C556Trace g_c556t{}; // game thread

const void* g_c556_cls[kC556ClassCache] = {};
int8_t g_c556_cls_holder[kC556ClassCache] = {}; // index into kC556Holders, or kC556NotHolder
int32_t g_c556_cls_n = 0;

// Host: the pickups the Destroy loop's override(1) released, until their freeze (or the next map).
struct C556Released {
    C31Weak weak[kC556Released];
    ULONGLONG at_ms[kC556Released];
    bool hit_logged[kC556Released]; // the first hit's row is out
    int32_t n;
};
C556Released g_c556_rel{}; // game thread
ULONGLONG g_c556_rack_end_ms = 0; // host: the last rack end row (the rack's DestroyActor)

// The OnHit UFunction seen last and its OtherActor parm offset (-1: not found).
const void* g_c556_hit_fn = nullptr;
int32_t g_c556_hit_other_at = -1;

uint32_t g_c556_override_rows = 0;
uint32_t g_c556_spawn_rows = 0;
uint32_t g_c556_trace_rows = 0;
uint32_t g_c556_skip_rows = 0;
uint32_t g_c556_freeze_rows = 0;
uint32_t g_c556_damage_rows = 0;

int c556_sim(const void* comp) {
    return comp != nullptr && g_prim_simulating != nullptr ? (g_prim_simulating(comp, 0) ? 1 : 0) : -1;
}

int c556_awake(const void* comp) {
    return comp != nullptr && g_prim_awake != nullptr ? (g_prim_awake(comp, 0) ? 1 : 0) : -1;
}

// Which kC556Holders kind `actor`'s class is, or kC556NotHolder; the class pointer is cached so the trace and OnHit
// hooks early-out on a pointer compare.
int8_t c556_holder(const void* actor) {
    const void* const cls = actor != nullptr ? read_ptr(actor, kObjectClass) : nullptr;
    if (cls == nullptr) {
        return kC556NotHolder;
    }
    for (int32_t i = 0; i < g_c556_cls_n; ++i) {
        if (g_c556_cls[i] == cls) {
            return g_c556_cls_holder[i];
        }
    }
    const ObjectName name = object_name(cls);
    int8_t kind = kC556NotHolder;
    for (int32_t h = 0; h < kC556HolderKinds && kind == kC556NotHolder; ++h) {
        kind = strstr(name.text, kC556Holders[h].part) != nullptr ? static_cast<int8_t>(h) : kC556NotHolder;
    }
    if (g_c556_cls_n < kC556ClassCache) {
        g_c556_cls[g_c556_cls_n] = cls;
        g_c556_cls_holder[g_c556_cls_n] = kind;
        ++g_c556_cls_n;
    }
    return kind;
}

bool c556_is_rack(const void* actor) {
    return c556_holder(actor) == 0;
}

int32_t c556_slot(const void* rack, bool add) {
    for (int32_t i = 0; i < kC556Racks; ++i) {
        if (g_c556t.rack[i] == rack) {
            return i;
        }
    }
    if (!add) {
        return -1;
    }
    const int32_t k = g_c556t.next;
    g_c556t.next = (k + 1) % kC556Racks;
    g_c556t.rack[k] = rack;
    g_c556t.first_ms[k] = GetTickCount64();
    g_c556t.traces[k] = 0;
    g_c556t.misses[k] = 0;
    g_c556t.last[k] = -1;
    g_c556t.damages[k] = 0;
    g_c556t.damage_ms[k] = 0;
    return k;
}

// The first property of `cls` (or a super) whose name starts with `prefix`: a BP variable's FName carries a suffix.
const uint8_t* c556_prop_prefix(const void* cls, const char* prefix) {
    const size_t len = strlen(prefix);
    const void* s = cls;
    for (int32_t depth = 0; depth < kC556PropSupers && s != nullptr; ++depth, s = read_ptr(s, kStructSuper)) {
        const void* f = read_ptr(s, kStructChildProperties);
        for (int32_t i = 0; i < kC556PropFields && f != nullptr; ++i, f = read_ptr(f, kFieldNext)) {
            if (strncmp(fname_text(static_cast<const uint8_t*>(f) + kFieldName).text, prefix, len) == 0) {
                return static_cast<const uint8_t*>(f);
            }
        }
    }
    return nullptr;
}

// Host, after the Destroy loop's override(1): wakes a body that simulates and sleeps. 1 woke, 0 left alone (awake,
// not simulating), -1 not read.
int c556_wake(void* mesh) {
    if (mesh == nullptr || g_prim_wake == nullptr || g_prim_simulating == nullptr || g_prim_awake == nullptr) {
        return -1;
    }
    if (!g_prim_simulating(mesh, kBoneNone) || g_prim_awake(mesh, kBoneNone)) {
        return 0;
    }
    g_prim_wake(mesh, kBoneNone);
    return 1;
}

void c556_release_add(void* pickup) {
    if (g_c556_rel.n >= kC556Released) {
        return;
    }
    const int32_t k = g_c556_rel.n++;
    g_c556_rel.weak[k] = C31Weak{};
    c31_weak_set(g_c556_rel.weak[k], pickup);
    g_c556_rel.at_ms[k] = GetTickCount64();
    g_c556_rel.hit_logged[k] = false;
}

int32_t c556_release_find(const void* pickup) {
    for (int32_t i = 0; i < g_c556_rel.n; ++i) {
        if (c31_weak_get(g_c556_rel.weak[i]) == pickup) {
            return i;
        }
    }
    return -1;
}

void c556_release_drop(int32_t k) {
    const int32_t last = --g_c556_rel.n;
    g_c556_rel.weak[k] = g_c556_rel.weak[last];
    g_c556_rel.at_ms[k] = g_c556_rel.at_ms[last];
    g_c556_rel.hit_logged[k] = g_c556_rel.hit_logged[last];
}

// The OnHit OtherActor from a ProcessEvent frame (Code null), or null (bytecode call, layout unknown).
const void* c556_hit_other(const void* frame) {
    const uint8_t* const locals = static_cast<const uint8_t*>(read_ptr(frame, kFrameLocals));
    if (read_ptr(frame, kFrameCode) != nullptr || locals == nullptr) {
        return nullptr;
    }
    const void* const fn = read_ptr(frame, kFrameNode);
    if (fn != g_c556_hit_fn) {
        g_c556_hit_fn = fn;
        const uint8_t* const prop = fn != nullptr ? prop_find_prop(fn, "OtherActor") : nullptr;
        g_c556_hit_other_at = prop != nullptr ? read_i32(prop, kPropertyOffset) : -1;
        log::line("%scard556: %s OnHit parms: OtherActor at %d", prop != nullptr ? "" : "WARN ", side(),
                  g_c556_hit_other_at);
    }
    return g_c556_hit_other_at >= 0 ? read_ptr(locals, static_cast<size_t>(g_c556_hit_other_at)) : nullptr;
}

// Why the freeze and attach on `other` is skipped, or null to run the original. The class compare comes first.
const char* c556_hit_skip(const void* other) {
    const int8_t kind = c556_holder(other);
    if (kind != kC556NotHolder) {
        return kC556Holders[kind].kind;
    }
    if (other == nullptr || g_c56_pickup_class == nullptr || !is_a(other, g_c56_pickup_class())) {
        return nullptr;
    }
    const void* const parent = g_get_attach_parent_actor != nullptr ? g_get_attach_parent_actor(other) : nullptr;
    if (parent != nullptr && c556_holder(parent) != kC556NotHolder) {
        return "pickup attached to a holder";
    }
    return c556_sim(read_ptr(other, kActorRootComponent)) == 1 ? "pickup still simulating" : nullptr;
}

void c556_trace_row(const void* rack, int32_t k, bool hit, const Vec3& start, const Vec3& end, uint64_t profile,
                    const uint8_t* out_hit) {
    if (g_c556_trace_rows >= kC556TraceRows) {
        return;
    }
    ++g_c556_trace_rows;
    const void* const comp = out_hit != nullptr && g_weak_get != nullptr ? g_weak_get(out_hit + kHitComponent) : nullptr;
    const void* const owner = comp != nullptr ? read_ptr(comp, kComponentOwner) : nullptr;
    const Vec3 at = out_hit != nullptr ? read_vec3(out_hit, kHitLocation) : Vec3{};
    log::line("card556: %s t=%llu rack %s trace below %s (trace %u, misses %u): start (%.0f, %.0f, %.0f) end (%.0f, "
              "%.0f, %.0f), profile %s, hit %s of %s at (%.0f, %.0f, %.0f)",
              side(), static_cast<unsigned long long>(GetTickCount64()), object_name(rack).text, hit ? "hit" : "miss",
              g_c556t.traces[k], g_c556t.misses[k], start.x, start.y, start.z, end.x, end.y, end.z,
              fname_text(&profile).text, object_name(comp).text, object_name(owner).text, at.x, at.y, at.z);
}

} // namespace

void detour_c556_override(void* pickup, uint8_t state) {
    const bool row = (state == 1 && g_c556_override_rows < kC556OverrideRows) ||
                     (state == 2 && g_c556_spawn_rows < kC556SpawnRows);
    const bool release = state == 1 && !g_client_session; // only the rack's Destroy loop sets state 1
    if (!row && !release) {
        g_orig_c556_override(pickup, state);
        return;
    }
    const uint8_t before = static_cast<const uint8_t*>(pickup)[kPickupOverrideState];
    void* const mesh = read_ptr(pickup, kPickupMesh);
    const int sim_before = c556_sim(mesh);
    g_orig_c556_override(pickup, state);
    const int woke = release ? c556_wake(mesh) : -1;
    if (release) {
        c556_release_add(pickup);
    }
    if (!row) {
        return;
    }
    g_c556_override_rows += state == 1 ? 1u : 0u;
    g_c556_spawn_rows += state == 2 ? 1u : 0u;
    log::line("card556: %s t=%llu override %s id %u state %u: stored before %u, mesh %s sim before %d, after %d, "
              "awake after %d, mesh is root %d, woke %d",
              side(), static_cast<unsigned long long>(GetTickCount64()), class_name(pickup).text,
              static_cast<unsigned>(object_index(pickup)), static_cast<unsigned>(state), static_cast<unsigned>(before),
              object_name(mesh).text, sim_before, c556_sim(mesh), c556_awake(mesh),
              mesh != nullptr && mesh == read_ptr(pickup, kActorRootComponent) ? 1 : 0, woke);
}

// See the file comment (card 556 fix).
void detour_c556_on_hit(void* ctx, void* frame, void* result) {
    const void* const other = !g_client_session && frame != nullptr ? c556_hit_other(frame) : nullptr;
    const char* const skip = other != nullptr ? c556_hit_skip(other) : nullptr;
    const int32_t k = !g_client_session && g_c556_rel.n > 0 ? c556_release_find(ctx) : -1;
    if (skip != nullptr) {
        if (g_c556_skip_rows < kC556SkipRows) {
            ++g_c556_skip_rows;
            log::line("card556: host t=%llu skipped rack attach: %s id %u hit %s (%s, %s), holder alive 1, released "
                      "%d",
                      static_cast<unsigned long long>(GetTickCount64()), class_name(ctx).text,
                      static_cast<unsigned>(object_index(ctx)), object_name(other).text, class_name(other).text, skip,
                      k >= 0 ? 1 : 0);
        }
        return;
    }
    const void* const root = k >= 0 ? read_ptr(ctx, kActorRootComponent) : nullptr;
    const int sim_before = k >= 0 ? c556_sim(root) : -1;
    g_orig_c556_on_hit(ctx, frame, result);
    if (k < 0) {
        return;
    }
    const bool froze = sim_before == 1 && c556_sim(root) == 0;
    if ((froze || !g_c556_rel.hit_logged[k]) && g_c556_freeze_rows < kC556FreezeRows) {
        ++g_c556_freeze_rows;
        const void* const parent = g_get_attach_parent_actor != nullptr ? g_get_attach_parent_actor(ctx) : nullptr;
        log::line("card556: host t=%llu released pickup %s id %u hit %s (%s) %llu ms after release: froze %d, attach "
                  "parent %s, rack destroyed %d",
                  static_cast<unsigned long long>(GetTickCount64()), class_name(ctx).text,
                  static_cast<unsigned>(object_index(ctx)), object_name(other).text, class_name(other).text,
                  static_cast<unsigned long long>(GetTickCount64() - g_c556_rel.at_ms[k]), froze ? 1 : 0,
                  object_name(parent).text, g_c556_rack_end_ms >= g_c556_rel.at_ms[k] ? 1 : 0);
    }
    g_c556_rel.hit_logged[k] = true;
    if (froze) {
        c556_release_drop(k);
    }
}

// See hooks_internal.h.
void c556_rack_damage(const void* event) {
    const void* const receiver = event != nullptr ? read_ptr(event, kEventReceiver) : nullptr;
    if (receiver == nullptr || !c556_is_rack(receiver)) {
        return;
    }
    const void* const causer = read_ptr(event, kEventCauser);
    const void* const instigator = read_ptr(event, kEventInstigator);
    const int32_t k = c556_slot(receiver, true);
    ++g_c556t.damages[k];
    g_c556t.damage_ms[k] = GetTickCount64();
    g_c556t.causer[k] = object_name(causer);
    g_c556t.causer_cls[k] = class_name(causer);
    g_c556t.instigator[k] = object_name(instigator);
    g_c556t.instigator_cls[k] = class_name(instigator);
    if (g_c556_damage_rows >= kC556DamageRows) {
        return;
    }
    ++g_c556_damage_rows;
    log::line("card556: %s t=%llu rack %s damage: causer %s (%s), instigator %s (%s), final %.1f", side(),
              static_cast<unsigned long long>(g_c556t.damage_ms[k]), object_name(receiver).text,
              g_c556t.causer[k].text, g_c556t.causer_cls[k].text, g_c556t.instigator[k].text,
              g_c556t.instigator_cls[k].text, static_cast<double>(read_f32(event, kEventFinalDamage)));
}

// See hooks_internal.h.
void c556_arm_map() {
    g_c556_rel.n = 0;
    g_c556_cls_n = 0; // a new map's classes can reuse the old ones' memory
    g_c556t = C556Trace{};
    g_c556_skip_rows = 0;
    g_c556_freeze_rows = 0;
    g_c556_damage_rows = 0;
}

bool detour_c556_box_trace(const void* ctx, const Vec3* start, const Vec3* end, const Vec3* half,
                           const void* orient, uint64_t profile, bool complex, const void* ignore, uint8_t draw,
                           void* out_hit, bool ignore_self, const void* color, const void* hit_color, float time) {
    const bool hit = g_orig_c556_box_trace(ctx, start, end, half, orient, profile, complex, ignore, draw, out_hit,
                                           ignore_self, color, hit_color, time);
    if (!c556_is_rack(ctx)) {
        return hit;
    }
    const int32_t k = c556_slot(ctx, true);
    ++g_c556t.traces[k];
    g_c556t.misses[k] += hit ? 0u : 1u;
    const int8_t now = hit ? 1 : 0;
    if (g_c556t.last[k] != now) {
        c556_trace_row(ctx, k, hit, *start, *end, profile, static_cast<const uint8_t*>(out_hit));
    }
    g_c556t.last[k] = now;
    return hit;
}

// See hooks_internal.h.
void c556_rack_end_row(const void* rack) {
    const int32_t k = c556_slot(rack, false);
    const uint8_t* const prop = c556_prop_prefix(read_ptr(rack, kObjectClass), "Duration");
    const bool is_float = prop != nullptr && prop_kind_is(prop, "FloatProperty");
    const bool is_double = prop != nullptr && prop_kind_is(prop, "DoubleProperty");
    const size_t off = prop != nullptr ? static_cast<size_t>(read_i32(prop, kPropertyOffset)) : 0;
    const double duration = is_float ? static_cast<double>(read_f32(rack, off))
                            : is_double ? *reinterpret_cast<const double*>(static_cast<const uint8_t*>(rack) + off)
                                        : -1.0;
    const ULONGLONG now = GetTickCount64();
    g_c556_rack_end_ms = now;
    const double age_s = k >= 0 ? static_cast<double>(now - g_c556t.first_ms[k]) / 1000.0 : -1.0;
    const int last = k >= 0 ? g_c556t.last[k] : -1;
    const bool damaged = k >= 0 && g_c556t.damage_ms[k] != 0;
    const long long damage_ago = damaged ? static_cast<long long>(now - g_c556t.damage_ms[k]) : -1;
    // TraceBelow's first call is 0.23 s after CheckBelow; the Lifetime timer runs Duration from the same BeginPlay.
    const char* const path = last == 0                                         ? "TraceBelow miss (CheckBelow)"
                             : damaged && damage_ago <= static_cast<long long>(kC556DamageEndMs) ? "damage"
                             : duration > 0.0 && age_s + 0.6 >= duration       ? "Lifetime timer"
                             : k < 0                                           ? "no trace seen"
                                                                               : "other (a kill command)";
    log::line("card556: %s t=%llu rack %s end: %s; first trace %.1f s ago, Duration %.1f s (%s), traces %u, misses "
              "%u, last trace %s, damage events %u, last damage %lld ms ago by causer %s (%s), instigator %s (%s)",
              side(), static_cast<unsigned long long>(now), object_name(rack).text, path, age_s, duration,
              prop != nullptr ? fname_text(prop + kFieldName).text : "not found", k >= 0 ? g_c556t.traces[k] : 0u,
              k >= 0 ? g_c556t.misses[k] : 0u, last == 1 ? "hit" : last == 0 ? "miss" : "none",
              k >= 0 ? g_c556t.damages[k] : 0u, damage_ago, damaged ? g_c556t.causer[k].text : "-",
              damaged ? g_c556t.causer_cls[k].text : "-", damaged ? g_c556t.instigator[k].text : "-",
              damaged ? g_c556t.instigator_cls[k].text : "-");
}

} // namespace ubw::hooks
