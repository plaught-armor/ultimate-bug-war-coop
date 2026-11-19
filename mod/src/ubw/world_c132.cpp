// Card 132 step 1: the host's world, inventoried per class. Every actor of every level is classified by what could
// change on it (a health component, a pickup, an objective actor, a timeline, the card 098 tally's change kinds) and
// by the mod mechanism that carries it to clients (the first row of kC132Mechs it matches). An actor with something
// that changes and no mechanism is a gap. One line per class, a summary and the objective system's line; logged at
// +kC49WorldCheckMs, at map end (LoadMap enter, the old world still alive) and by the `objectives` test command.
// Log only.

#include "ubw/hooks_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ubw::hooks {

StaticClassFn g_c132_timeline_class = nullptr;
StaticClassFn g_c132_satchel_class = nullptr;
StaticClassFn g_c132_custom_pickup_class = nullptr;

namespace {

constexpr int32_t kC132Classes = 384;          // class rows per inventory
constexpr int32_t kC132ObjectiveActors = 2048; // objective actor pointers collected
constexpr int32_t kC132PerObjective = 256;     // ObjectiveActors elements read per objective

// What an actor has; bit order is the line's field order.
enum C132Trait : uint32_t {
    kTraitRep = 1u << 0,
    kTraitHealth = 1u << 1,
    kTraitPickup = 1u << 2,
    kTraitObjective = 1u << 3,
    kTraitTimeline = 1u << 4,
    kTraitDoor = 1u << 5,
    kTraitSatchel = 1u << 6,
    kTraitTurret = 1u << 7,
    kTraitCustomPickup = 1u << 8,
    kTraitRelayed = 1u << 9,
    kTraitPlaced = 1u << 10,
    kTraitExt = 1u << 11, // card 423: a kC132Ext row's class (digest_c132.cpp)
};
constexpr int32_t kC132Traits = 12;
const char* const kC132TraitText[] = {"rep",    "health",  "pickup", "objective",     "timeline", "door",
                                      "satchel", "turret", "custompickup", "relayed", "placed", "ext"};
static_assert(sizeof(kC132TraitText) / sizeof(kC132TraitText[0]) == kC132Traits, "one text per trait");

// The mechanism condition table: an actor is carried by the first row whose traits it all has.
struct C132Mech {
    uint32_t need;
    const char* name;
};
const C132Mech kC132Mechs[] = {
    {kTraitRep, "engine"},                     // engine replication
    {kTraitPickup, "056"},                     // card 056: pickups taken
    {kTraitDoor, "095"},                       // card 095: doors
    {kTraitSatchel | kTraitPlaced, "098S"},    // card 098: satchel buildings
    {kTraitTurret | kTraitPlaced, "098T"},     // card 125: placed turret state
    {kTraitHealth | kTraitPlaced, "098K"},     // card 130 item 12 / 131: placed killables
    {kTraitTimeline | kTraitPlaced, "098L"},   // card 130 item 2: placed timelines
    {kTraitExt | kTraitPlaced, "132X"},        // card 132 step 2d / 423: ext rows (ext_c132.cpp)
    {kTraitObjective, "049"},                  // card 049: objective state
    {kTraitRelayed, "104"},                    // card 104: runtime spawns relayed as copies
};
constexpr int32_t kC132MechRows = static_cast<int32_t>(sizeof(kC132Mechs) / sizeof(kC132Mechs[0]));

// Traits that mean the actor's state can change during play (with the tally's change kinds).
constexpr uint32_t kC132Changing = kTraitHealth | kTraitPickup | kTraitObjective | kTraitTimeline;

struct C132Row {
    char cls[kNameChars];
    uint32_t n;
    uint32_t trait[kC132Traits];
    uint32_t mech[kC132MechRows];
    uint32_t none, gap; // actors with no mechanism; of those, actors with something that changes
    uint32_t changes;   // c98_class_changes bits
    uint32_t mesh_root, decals; // actors with a static or skeletal mesh component (any, not only the root); of those, bReceivesDecals set
};

constexpr size_t kPrimitiveDecalFlags = 0x26C; // UPrimitiveComponent bitfield byte holding bReceivesDecals (enemies.cpp)
constexpr uint8_t kPrimitiveReceivesDecals = 0x40;

struct C132State {
    C132Row row[kC132Classes];
    int32_t n, peak;
    uint32_t dropped, actors;
    const void* objective_actor[kC132ObjectiveActors];
    int32_t objective_n;
    uint32_t objective_dropped;
};

C132State g_c132{}; // game thread; static: too large for the stack

int c132_ptr_cmp(const void* a, const void* b) {
    const auto x = reinterpret_cast<uintptr_t>(*static_cast<const void* const*>(a));
    const auto y = reinterpret_cast<uintptr_t>(*static_cast<const void* const*>(b));
    return x < y ? -1 : (x > y ? 1 : 0);
}

// Every objective's ObjectiveActors, sorted for c132_is_objective_actor. Returns the objective count (-1 unread).
int32_t c132_collect_objective_actors(const void* world, int32_t& complete) {
    void* const subsystem = g_c49_get_subsystem != nullptr ? g_c49_get_subsystem(const_cast<void*>(world)) : nullptr;
    SparseView v{};
    complete = 0;
    if (subsystem == nullptr || !c49_objectives(subsystem, v)) {
        return -1;
    }
    int32_t objectives = 0;
    for (int32_t i = 0; i < v.count; ++i) {
        const uint8_t* const o = sparse_allocated(v, i) ? static_cast<const uint8_t*>(read_ptr(
                                     v.data + static_cast<size_t>(i) * kObjectiveElementBytes, kObjectiveElementValue))
                                                        : nullptr;
        if (o == nullptr) {
            continue;
        }
        ++objectives;
        complete += o[kObjectiveComplete] != 0 ? 1 : 0;
        const auto* const actors = static_cast<void* const*>(read_ptr(o, kObjectiveActors));
        const int32_t num = read_i32(o, kObjectiveActors + kArrayNum);
        for (int32_t a = 0; actors != nullptr && a < num && a < kC132PerObjective; ++a) {
            if (g_c132.objective_n >= kC132ObjectiveActors) {
                ++g_c132.objective_dropped;
            } else if (actors[a] != nullptr) {
                g_c132.objective_actor[g_c132.objective_n++] = actors[a];
            }
        }
    }
    qsort(g_c132.objective_actor, static_cast<size_t>(g_c132.objective_n), sizeof(void*), c132_ptr_cmp);
    return objectives;
}

bool c132_is_objective_actor(const void* actor) {
    return bsearch(&actor, g_c132.objective_actor, static_cast<size_t>(g_c132.objective_n), sizeof(void*),
                   c132_ptr_cmp) != nullptr;
}

bool c132_class_is(const void* actor, StaticClassFn cls) {
    return cls != nullptr && is_a(actor, cls());
}

uint32_t c132_traits(const void* actor) {
    uint32_t t = 0;
    t |= (static_cast<const uint8_t*>(actor)[kActorRepFlags] & 0x08u) != 0 ? kTraitRep : 0u;
    t |= g_c130_health_class != nullptr && find_component(actor, g_c130_health_class) != nullptr ? kTraitHealth : 0u;
    t |= c132_class_is(actor, g_c56_pickup_class) ? kTraitPickup : 0u;
    t |= c132_is_objective_actor(actor) ? kTraitObjective : 0u;
    t |= g_c132_timeline_class != nullptr && find_component(actor, g_c132_timeline_class) != nullptr ? kTraitTimeline
                                                                                                    : 0u;
    t |= c132_class_is(actor, g_c95_door_class) ? kTraitDoor : 0u;
    t |= c132_class_is(actor, g_c132_satchel_class) ? kTraitSatchel : 0u;
    t |= c132_class_is(actor, g_turret_class) ? kTraitTurret : 0u;
    t |= c132_class_is(actor, g_c132_custom_pickup_class) ? kTraitCustomPickup : 0u;
    t |= c104_is_relayed(actor) ? kTraitRelayed : 0u;
    t |= c50_placed(actor) ? kTraitPlaced : 0u;
    C132ExtField f{};
    t |= c132_ext_field(actor, f) ? kTraitExt : 0u;
    return t;
}

C132Row* c132_row(const char* cls) {
    for (int32_t i = 0; i < g_c132.n; ++i) {
        if (strcmp(g_c132.row[i].cls, cls) == 0) {
            return &g_c132.row[i];
        }
    }
    if (g_c132.n >= kC132Classes) {
        ++g_c132.dropped;
        return nullptr;
    }
    C132Row& r = g_c132.row[g_c132.n++];
    r = C132Row{}; // a reused row keeps the last pass's counts otherwise (c132l1: n 761, 1522, 2283)
    snprintf(r.cls, sizeof(r.cls), "%s", cls);
    r.changes = c98_class_changes(cls);
    return &r;
}

// The actor's first static mesh component, else its first skeletal mesh component, else null. Not only the root: the
// placed BP_KillableCrate_01_C has a non-mesh root (hb47 decals=0/0 with n=50).
const uint8_t* c132_mesh(const void* actor) {
    static void* static_mesh = nullptr;
    static void* skeletal_mesh = nullptr;
    static_mesh = static_mesh != nullptr ? static_mesh : find_class(L"/Script/Engine.StaticMeshComponent");
    skeletal_mesh = skeletal_mesh != nullptr ? skeletal_mesh : find_class(L"/Script/Engine.SkeletalMeshComponent");
    const void* mesh = find_component_of(actor, static_mesh);
    mesh = mesh != nullptr ? mesh : find_component_of(actor, skeletal_mesh);
    return static_cast<const uint8_t*>(mesh);
}

void c132_add(const void* actor) {
    C132Row* const r = c132_row(class_name(actor).text);
    if (r == nullptr) {
        return;
    }
    const uint32_t t = c132_traits(actor);
    const uint8_t* const mesh = c132_mesh(actor);
    r->mesh_root += mesh != nullptr ? 1u : 0u;
    r->decals += mesh != nullptr && (mesh[kPrimitiveDecalFlags] & kPrimitiveReceivesDecals) != 0;
    ++r->n;
    ++g_c132.actors;
    for (int32_t b = 0; b < kC132Traits; ++b) {
        r->trait[b] += (t >> b) & 1u;
    }
    int32_t m = 0;
    while (m < kC132MechRows && (t & kC132Mechs[m].need) != kC132Mechs[m].need) {
        ++m; // bounded by kC132MechRows
    }
    if (m < kC132MechRows) {
        ++r->mech[m];
        return;
    }
    ++r->none;
    r->gap += ((t & kC132Changing) != 0 || r->changes != 0) ? 1u : 0u;
}

void c132_walk(const void* world) {
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    for (int32_t l = 0; levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const auto* const actors =
            levels[l] != nullptr ? static_cast<void* const*>(read_ptr(levels[l], kLevelActors)) : nullptr;
        const int32_t num = levels[l] != nullptr ? read_i32(levels[l], kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; actors != nullptr && a < num && a < kMaxLevelActors; ++a) {
            if (actors[a] != nullptr) {
                c132_add(actors[a]);
            }
        }
    }
}

// "a,b" of the set bits, or "-".
void c132_bits_text(uint32_t bits, const char* const* names, int32_t count, char* out, size_t cap) {
    size_t at = 0;
    out[0] = '\0';
    for (int32_t b = 0; b < count && at < cap; ++b) {
        if ((bits >> b) & 1u) {
            const int w = snprintf(out + at, cap - at, "%s%s", at == 0 ? "" : ",", names[b]);
            at += w > 0 ? static_cast<size_t>(w) : 0;
        }
    }
    if (out[0] == '\0') {
        snprintf(out, cap, "-");
    }
}

void c132_row_line(const char* why, const C132Row& r) {
    char traits[160];
    char changes[96];
    char mech[128];
    uint32_t have = 0;
    for (int32_t b = 0; b < kC132Traits; ++b) {
        have |= r.trait[b] != 0 ? 1u << b : 0u;
    }
    c132_bits_text(have, kC132TraitText, kC132Traits, traits, sizeof(traits));
    c132_bits_text(r.changes, kC98KindText, static_cast<int32_t>(C98Kind::Count), changes, sizeof(changes));
    size_t at = 0;
    mech[0] = '\0';
    for (int32_t m = 0; m < kC132MechRows && at < sizeof(mech); ++m) {
        if (r.mech[m] != 0) {
            const int w = snprintf(mech + at, sizeof(mech) - at, "%s%s:%u", at == 0 ? "" : ",", kC132Mechs[m].name,
                                   r.mech[m]);
            at += w > 0 ? static_cast<size_t>(w) : 0;
        }
    }
    log::line("card132: host inventory (%s) class %s n=%u placed=%u rep=%u health=%u pickup=%u objective=%u "
              "timeline=%u decals=%u/%u traits=%s changes=%s mechanism=%s%snone:%u%s",
              why, r.cls, r.n, r.trait[10], r.trait[0], r.trait[1], r.trait[2], r.trait[3], r.trait[4], r.decals,
              r.mesh_root, traits, changes, mech, mech[0] != '\0' ? "," : "", r.none, r.gap != 0 ? " GAP" : "");
}

} // namespace

// Listen server only. world: the world to read (the old one at map end). why: the trigger, for the lines.
void c132_inventory(const void* world, const char* why) {
    // Card 349: any PC in the world will do (net mode only); not the local PC.
    void* const pc = world != nullptr && !g_client_session ? g_world_first_pc(const_cast<void*>(world)) : nullptr;
    if (pc == nullptr || net_mode(pc) != kNetModeListenServer) {
        return; // a client, the front end or a standalone world: nothing to carry
    }
    const ULONGLONG pass_start = GetTickCount64(); // card 330: this pass's cost, in the summary row
    g_c132.n = 0;
    g_c132.dropped = 0;
    g_c132.actors = 0;
    g_c132.objective_n = 0;
    g_c132.objective_dropped = 0;
    int32_t complete = 0;
    const int32_t objectives = c132_collect_objective_actors(world, complete);
    c132_walk(world);
    g_c132.peak = g_c132.n > g_c132.peak ? g_c132.n : g_c132.peak;
    uint32_t gap_classes = 0;
    uint32_t gap_actors = 0;
    for (int32_t i = 0; i < g_c132.n; ++i) {
        c132_row_line(why, g_c132.row[i]);
        gap_classes += g_c132.row[i].gap != 0 ? 1u : 0u;
        gap_actors += g_c132.row[i].gap;
    }
    for (int32_t i = 0; i < g_c132.n; ++i) {
        if (g_c132.row[i].gap != 0) {
            log::line("card132: host inventory (%s) gap %s: %u of %u actors with no mechanism", why,
                      g_c132.row[i].cls, g_c132.row[i].gap, g_c132.row[i].n);
        }
    }
    log::line("card132: host inventory (%s) objectives: %d objectives, %d complete, %d objective actors (%u dropped), "
              "mechanism 049",
              why, objectives, complete, g_c132.objective_n, g_c132.objective_dropped);
    const ULONGLONG pass_end = GetTickCount64();
    log::line("%scard132: host t=%llu inventory (%s): %u actors, %d classes (peak %d of %d, %u dropped), "
              "%u gap classes, %u gap actors, pass %llu ms",
              g_c132.dropped != 0 ? "WARN " : "", static_cast<unsigned long long>(pass_end), why, g_c132.actors,
              g_c132.n, g_c132.peak, kC132Classes, g_c132.dropped, gap_classes, gap_actors,
              static_cast<unsigned long long>(pass_end - pass_start));
}

} // namespace ubw::hooks
