// Card 577 (performance mod family): the client skips runtime navmesh tile rebuilds.
//
// pdata mapping of the c573b390b client combat Background Work samples (2026-11-21): about 2300 samples sit in
// FRecastTileGenerator::GenerateNavigationDataLayer and the dtBuildTileCache* builders it calls; real crowd work is
// about 1000. The client runs no AI and no authoritative pathing, so it has no use for a rebuilt navmesh; the cooked
// navmesh stays loaded for the test driver's client goto.
//
// Hook: virtual FRecastNavMeshGenerator::RebuildDirtyAreas(const TArray<FNavigationDirtyArea>&). Its caller,
// UNavigationSystemV1::RebuildDirtyAreas(float), clears the array after the call, so a skipped call drops the areas
// and nothing grows. The outer function is never skipped, for that reason.
// Client: skip, count. Host: count, run. UBWCOOP_C577_OFF=1: not installed.
//
// Card 593 (host diag): the host still pays the same rebuilds (c573b390b host combat: about 2800 of 15236 Background
// Work samples). Each logged call also names what dirtied the tiles: the dirty areas since the last row, tallied by
// the class of each area's source object. Layout read from the exe (2026-11-21):
//   FNavigationDirtyArea, stride 0x50 (ctor 0x0380CBC0): FBox Bounds at 0x00 (Min 0x00, Max 0x18, IsValid 0x30),
//     TSharedPtr<const FNavigationElement> OptionalSourceElement at 0x38 (object) / 0x40 (ref count), Flags at 0x48.
//   FNavigationElement: TWeakObjectPtr owner at 0x40 (GetHandle 0x038266B0, GetPathName 0x03828060).
//
// Card 593 step 2 (host diag): b434 c590b434a put BP_MountableTurret_Gunner_C's BoxComponent first (780 rows, up to
// 1133 uu) and BP_SatchelBuilding_Flakbug_C's CapsuleComponent second (204). For each of those two (component class,
// actor class) pairs, one `card593: host watch` row on first sight names the component's navigation facts (attach
// parent, mobility, collision enabled, object type, ECC_Pawn response, bCanEverAffectNavigation, bDynamicObstacle),
// and the c593_row line adds how many of its areas came while the component had moved (location or rotation) since
// the last frame a rebuild carried one of its areas, against those where it had not. If the box blocks no Pawn, the
// lossless cut is navigation off for it (next build).
//
// Card 593 step 3 (b436, removed in b437): a move threshold on PropagateTransformUpdate's nav update skipped only 11%
// of moves (c594b436a), at a bounded carve lag; the senior dropped it. See docs/cards/593-host-navmesh-rebuild-sources.md.
//
// Card 593 step 5 (performance lever, host, b438): hold the rebuilds of any navmesh no agent has resolved on this map.
// c593b437ca/cb: C3M1 resolves Default, Small, Medium and Large, never Gigantic, and every dirty batch is rebuilt once
// per generator (five). A generator's navmesh is FRecastNavMeshGenerator::DestNavMesh (+0x1F8, read by the
// RebuildDirtyAreas body at 0x04AFF750). After kC593HoldAfterMs on a map (the in-use navmeshes all resolve within
// about 1 s of the first rebuild), a call for a navmesh with no resolve yet is skipped; the skip keeps the union box of
// the dropped areas and the OR of their flags. The first resolve to a held navmesh (c593_hold_use, the card 593 resolve
// hook, before it returns) replays that box as one dirty area through the original, logs the WARN, and the navmesh is
// rebuilt normally from then on. A first use on another thread leaves the replay to that generator's next call. The
// table resets when the navmesh's level changes (a new map). UBWCOOP_C593_HOLD_OFF=1: no hold.
#include "ubw/hooks_internal.h"

#include <atomic>
#include <string.h>

namespace ubw::hooks {

C577RebuildFn g_orig_c577_rebuild = nullptr;
bool g_c577_on = false; // written in ubw::init before any hook
bool g_c593_hold_on = true; // read_perf_flags (UBWCOOP_C593_HOLD_OFF=1 turns the step 5 hold off)

namespace {

constexpr uint32_t kC577Rows = 8;     // every call logged up to here
constexpr uint32_t kC577Every = 16;   // then every Nth one (card 593: about 87 calls per run)
uint32_t g_c577_count = 0;            // game thread only (navigation system tick)

constexpr size_t kC593AreaStride = 0x50;
constexpr size_t kC593AreaMaxX = 0x18;
constexpr size_t kC593AreaElement = 0x38;
constexpr size_t kC593AreaFlags = 0x48;
constexpr size_t kC593ElementOwner = 0x40;
constexpr int32_t kC593AreasPerCall = 4096; // loop bound; a larger array is counted, its tail not tallied
constexpr uint32_t kC593Classes = 16;
constexpr uint32_t kC593Shown = 6;

struct C593Class {
    const void* cls;   // owner's class; null = area with no source element, or a source gone
    ObjectName name;   // cls named at the first tally: a row can come after map travel freed cls, so cls is compared only
    int32_t watch;     // step 2: index in g_c593_watch whose component class this is, or -1
    ObjectName outer;  // the first owner's outer (the actor, for a component), named at tally time
    uint32_t areas;
    double max_extent; // largest area edge in uu
};
struct C593 {
    C593Class c[kC593Classes];
    uint32_t n;
    uint32_t areas, untallied, calls;
};
C593 g_c593{};

// Step 2: the watched pairs. actor_cls is found by name on the first match, then compared by pointer.
struct C593Watch {
    const char* comp;
    const char* actor;
    const void* actor_cls;
    bool logged;
    uint32_t moved, still, first, untracked;
};
C593Watch g_c593_watch[] = {
    {"BoxComponent", "BP_MountableTurret_Gunner_C", nullptr, false, 0, 0, 0, 0},
    {"CapsuleComponent", "BP_SatchelBuilding_Flakbug_C", nullptr, false, 0, 0, 0, 0},
};
constexpr int32_t kC593Watches = static_cast<int32_t>(sizeof(g_c593_watch) / sizeof(g_c593_watch[0]));

// Step 2: each watched component's transform at the last frame a rebuild carried one of its areas.
constexpr uint32_t kC593Seen = 64;
constexpr size_t kC593Transform = 7 * sizeof(double); // quat X,Y,Z,W (kComponentRotation), then translation
struct C593Seen {
    const void* comp;
    double xf[7];
    uint32_t frame;
    bool moved; // this frame's answer, for the repeats of the same batch (one per RecastNavMesh generator)
};
C593Seen g_c593_seen[kC593Seen]{};
uint32_t g_c593_seen_n = 0;

int32_t c593_watch_of_class(const void* cls) {
    const ObjectName name = object_name(cls);
    for (int32_t w = 0; w < kC593Watches; ++w) {
        if (strcmp(name.text, g_c593_watch[w].comp) == 0) {
            return w;
        }
    }
    return -1;
}

// A reflected bool of `object`: 1, 0, or -1 when the class has no such bool.
int c593_bool(const void* object, const char* name) {
    const uint8_t* const p = prop_find_prop(read_ptr(object, kObjectClass), name);
    if (p == nullptr || !prop_bool_ok(p)) {
        return -1;
    }
    const uint8_t* const b = static_cast<const uint8_t*>(object) + read_i32(p, kPropertyOffset) + p[kBoolByteOffset];
    return (*b & p[kBoolFieldMask]) != 0 ? 1 : 0;
}

// A reflected one-byte field of `object` (an enum as byte), or -1 when the class has no such property.
int c593_byte(const void* object, const char* name) {
    const uint8_t* const p = prop_find_prop(read_ptr(object, kObjectClass), name);
    return p != nullptr ? static_cast<const uint8_t*>(object)[read_i32(p, kPropertyOffset)] : -1;
}

void c593_watch_facts(C593Watch& w, const void* comp, const void* actor) {
    w.logged = true;
    const uint8_t* prop = nullptr;
    const void* const parent = prop_object_field(comp, "AttachParent", &prop);
    const auto* const c = static_cast<const uint8_t*>(comp);
    log::line("card593: %s watch %s %s in %s: attach parent %s, Mobility %d, CollisionEnabled %u, ObjectType %u, "
              "ECC_Pawn response %u, bCanEverAffectNavigation %d, bDynamicObstacle %d",
              side(), w.actor, object_name(comp).text, object_name(actor).text,
              parent != nullptr ? object_name(parent).text : "none", c593_byte(comp, "Mobility"),
              c[kBodyCollisionEnabled], c[kBodyObjectType], c[kBodyResponses + 2],
              c593_bool(comp, "bCanEverAffectNavigation"), c593_bool(comp, "bDynamicObstacle"));
}

// Whether `comp` moved since the last frame one of its areas came; counted in `w`.
void c593_watch_move(C593Watch& w, const void* comp) {
    const uint32_t frame = perf_frame();
    double xf[7];
    memcpy(xf, static_cast<const uint8_t*>(comp) + kComponentRotation, kC593Transform);
    uint32_t i = 0;
    while (i < g_c593_seen_n && g_c593_seen[i].comp != comp) {
        ++i;
    }
    if (i == g_c593_seen_n) {
        if (g_c593_seen_n == kC593Seen) {
            ++w.untracked;
            return;
        }
        ++g_c593_seen_n;
        g_c593_seen[i] = C593Seen{comp, {}, frame, false};
        memcpy(g_c593_seen[i].xf, xf, kC593Transform);
        ++w.first;
        return;
    }
    C593Seen& s = g_c593_seen[i];
    if (s.frame != frame) {
        s.moved = memcmp(s.xf, xf, kC593Transform) != 0;
        s.frame = frame;
        memcpy(s.xf, xf, kC593Transform);
    }
    ++(s.moved ? w.moved : w.still);
}

void c593_watch_area(int32_t watch, const void* owner) {
    C593Watch& w = g_c593_watch[watch];
    const void* const actor = read_ptr(owner, kObjectOuter);
    const void* const actor_cls = actor != nullptr ? read_ptr(actor, kObjectClass) : nullptr;
    if (actor_cls == nullptr) {
        return;
    }
    if (w.actor_cls == nullptr && strcmp(object_name(actor_cls).text, w.actor) == 0) {
        w.actor_cls = actor_cls;
    }
    if (actor_cls != w.actor_cls) {
        return;
    }
    if (!w.logged) {
        c593_watch_facts(w, owner, actor);
    }
    c593_watch_move(w, owner);
}

void c593_tally(const uint8_t* area) {
    const auto* const min = reinterpret_cast<const double*>(area);
    const auto* const max = reinterpret_cast<const double*>(area + kC593AreaMaxX);
    double extent = 0.0;
    for (int k = 0; k < 3; ++k) {
        extent = max[k] - min[k] > extent ? max[k] - min[k] : extent;
    }
    const void* const element = read_ptr(area, kC593AreaElement);
    const void* const owner =
        element != nullptr ? c31_weak_get(*reinterpret_cast<const C31Weak*>(static_cast<const uint8_t*>(element) +
                                                                            kC593ElementOwner))
                           : nullptr;
    const void* const cls = owner != nullptr ? read_ptr(owner, kObjectClass) : nullptr;
    uint32_t i = 0;
    while (i < g_c593.n && g_c593.c[i].cls != cls) {
        ++i;
    }
    if (i == g_c593.n) {
        if (g_c593.n == kC593Classes) {
            ++g_c593.untallied;
            return;
        }
        C593Class& added = g_c593.c[g_c593.n++];
        added = C593Class{};
        added.cls = cls;
        added.name = cls != nullptr ? object_name(cls) : ObjectName{};
        const void* const outer = owner != nullptr ? read_ptr(owner, kObjectOuter) : nullptr;
        added.outer = outer != nullptr ? object_name(outer) : ObjectName{};
        added.watch = cls != nullptr ? c593_watch_of_class(cls) : -1;
    }
    C593Class& c = g_c593.c[i];
    if (c.watch >= 0) {
        c593_watch_area(c.watch, owner);
    }
    ++c.areas;
    c.max_extent = extent > c.max_extent ? extent : c.max_extent;
}

void c593_count(const void* dirty_areas) {
    const auto* const data = static_cast<const uint8_t*>(read_ptr(dirty_areas, 0));
    const int32_t num = *reinterpret_cast<const int32_t*>(static_cast<const uint8_t*>(dirty_areas) + 8);
    ++g_c593.calls;
    g_c593.areas += num > 0 ? static_cast<uint32_t>(num) : 0u;
    const int32_t walked = num < kC593AreasPerCall ? num : kC593AreasPerCall;
    g_c593.untallied += static_cast<uint32_t>(num - walked);
    for (int32_t a = 0; data != nullptr && a < walked; ++a) {
        c593_tally(data + static_cast<size_t>(a) * kC593AreaStride);
    }
}

// One row: the top classes by area count since the last row, then the table restarts.
void c593_row() {
    char text[1024];
    int len = 0;
    bool shown[kC593Classes] = {};
    for (uint32_t s = 0; s < kC593Shown && s < g_c593.n; ++s) {
        uint32_t best = kC593Classes;
        for (uint32_t i = 0; i < g_c593.n; ++i) {
            best = !shown[i] && (best == kC593Classes || g_c593.c[i].areas > g_c593.c[best].areas) ? i : best;
        }
        shown[best] = true;
        const C593Class& c = g_c593.c[best];
        const int w = snprintf(text + len, sizeof(text) - static_cast<size_t>(len), "%s%s x%u (max %.0f uu, e.g. in %s)",
                               s == 0 ? "" : ", ", c.cls != nullptr ? c.name.text : "(no source)",
                               c.areas, c.max_extent, c.outer.text[0] != '\0' ? c.outer.text : "-");
        len = w > 0 && len + w < static_cast<int>(sizeof(text)) ? len + w : len;
    }
    text[len] = '\0';
    log::line("card593: %s dirty areas since last row: %u areas in %u rebuild calls, %u classes, %u untallied: %s",
              side(), g_c593.areas, g_c593.calls, g_c593.n, g_c593.untallied, len > 0 ? text : "none");
    for (const C593Watch& w : g_c593_watch) {
        if (w.logged) {
            log::line("card593: %s watch %s %s areas so far: moved since its last rebuild frame %u, not moved %u, "
                      "first seen %u, untracked %u",
                      side(), w.actor, w.comp, w.moved, w.still, w.first, w.untracked);
        }
    }
    g_c593 = C593{};
}


// Step 5: one row per navmesh generator seen on this map. `nav` and `used` are read and set by resolves on any thread;
// everything else is written on the rebuild (game) thread only.
constexpr uint32_t kC593Held = 8;
constexpr size_t kC593GenNav = 0x1F8;           // FRecastNavMeshGenerator::DestNavMesh
constexpr ULONGLONG kC593HoldAfterMs = 30000;   // after the map's first rebuild call
struct C593Hold {
    std::atomic<const void*> nav;
    std::atomic<bool> used;
    void* generator;
    ObjectName name;
    uint32_t calls, areas; // held calls and their areas, this map
    uint32_t replays;
    bool pending;          // a held box not yet replayed
    double lo[3], hi[3];   // union of the held areas' bounds
    int32_t flags;         // OR of the held areas' flags
    uint8_t tmpl[kC593AreaStride]; // the first held area, the replay's template
};
C593Hold g_c593_hold[kC593Held];
std::atomic<uint32_t> g_c593_hold_n{0};
const void* g_c593_hold_level = nullptr;
ULONGLONG g_c593_hold_t0 = 0;
std::atomic<DWORD> g_c593_rebuild_thread{0};

void c593_hold_reset(const void* level) {
    g_c593_hold_n.store(0, std::memory_order_release);
    for (C593Hold& h : g_c593_hold) {
        h.nav.store(nullptr, std::memory_order_relaxed);
        h.used.store(false, std::memory_order_relaxed);
        h.generator = nullptr;
        h.calls = h.areas = h.replays = 0;
        h.pending = false;
        h.flags = 0;
    }
    g_c593_hold_level = level;
    g_c593_hold_t0 = GetTickCount64();
}

C593Hold* c593_hold_find(const void* nav) {
    const uint32_t n = g_c593_hold_n.load(std::memory_order_acquire);
    for (uint32_t i = 0; i < n && i < kC593Held; ++i) {
        if (g_c593_hold[i].nav.load(std::memory_order_relaxed) == nav) {
            return &g_c593_hold[i];
        }
    }
    return nullptr;
}

// The row for `nav`, added on first sight; null when the table is full (that navmesh is never held).
C593Hold* c593_hold_entry(const void* nav, void* generator) {
    C593Hold* h = c593_hold_find(nav);
    const uint32_t n = g_c593_hold_n.load(std::memory_order_relaxed);
    if (h == nullptr && n < kC593Held) {
        h = &g_c593_hold[n];
        h->name = object_name(nav);
        h->nav.store(nav, std::memory_order_relaxed);
        g_c593_hold_n.store(n + 1, std::memory_order_release);
    }
    if (h != nullptr) {
        h->generator = generator;
    }
    return h;
}

void c593_hold_add(C593Hold& h, const void* dirty_areas) {
    const auto* const data = static_cast<const uint8_t*>(read_ptr(dirty_areas, 0));
    const int32_t num = *reinterpret_cast<const int32_t*>(static_cast<const uint8_t*>(dirty_areas) + 8);
    const int32_t walked = num < kC593AreasPerCall ? num : kC593AreasPerCall;
    ++h.calls;
    for (int32_t a = 0; data != nullptr && a < walked; ++a) {
        const uint8_t* const area = data + static_cast<size_t>(a) * kC593AreaStride;
        double lo[3], hi[3];
        int32_t flags = 0;
        memcpy(lo, area, sizeof(lo));
        memcpy(hi, area + kC593AreaMaxX, sizeof(hi));
        memcpy(&flags, area + kC593AreaFlags, sizeof(flags));
        if (!h.pending) {
            memcpy(h.tmpl, area, sizeof(h.tmpl));
            memcpy(h.lo, lo, sizeof(lo));
            memcpy(h.hi, hi, sizeof(hi));
            h.pending = true;
        }
        for (int k = 0; k < 3; ++k) {
            h.lo[k] = lo[k] < h.lo[k] ? lo[k] : h.lo[k];
            h.hi[k] = hi[k] > h.hi[k] ? hi[k] : h.hi[k];
        }
        h.flags |= flags;
        ++h.areas;
    }
}

// The held box as one dirty area, through the original (rebuild thread only).
void c593_hold_replay(C593Hold& h, const char* when) {
    alignas(8) uint8_t area[kC593AreaStride];
    memcpy(area, h.tmpl, sizeof(area));
    memcpy(area, h.lo, sizeof(h.lo));
    memcpy(area + kC593AreaMaxX, h.hi, sizeof(h.hi));
    memset(area + kC593AreaElement, 0, 2 * sizeof(void*)); // no source element: the template's shared pointer is not ours
    memcpy(area + kC593AreaFlags, &h.flags, sizeof(h.flags));
    struct {
        void* data;
        int32_t num, max;
    } one{area, 1, 1};
    h.pending = false;
    ++h.replays;
    log::line("WARN card593: held navmesh %s first used, full rebuild (%s): %u calls, %u areas held, box (%.0f, %.0f, "
              "%.0f)-(%.0f, %.0f, %.0f), flags 0x%X",
              h.name.text, when, h.calls, h.areas, h.lo[0], h.lo[1], h.lo[2], h.hi[0], h.hi[1], h.hi[2],
              static_cast<unsigned>(h.flags));
    g_orig_c577_rebuild(h.generator, &one);
}

// True: skip this call (its navmesh is held).
bool c593_hold(void* generator, const void* dirty_areas) {
    g_c593_rebuild_thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    const void* const nav = read_ptr(generator, kC593GenNav);
    const void* const level = nav != nullptr ? read_ptr(nav, kObjectOuter) : nullptr;
    if (level == nullptr) {
        return false;
    }
    if (level != g_c593_hold_level) {
        c593_hold_reset(level);
        log::line("card593: %s hold table reset for level %s (%s)", side(), object_name(level).text,
                  g_c593_hold_on ? "hold on" : "hold off, UBWCOOP_C593_HOLD_OFF");
    }
    C593Hold* const h = c593_hold_entry(nav, generator);
    if (h == nullptr || !g_c593_hold_on) {
        return false;
    }
    if (h->used.load(std::memory_order_acquire)) {
        if (h->pending) {
            c593_hold_replay(*h, "on its next rebuild call, first use off this thread");
        }
        return false;
    }
    if (GetTickCount64() - g_c593_hold_t0 < kC593HoldAfterMs) {
        return false;
    }
    c593_hold_add(*h, dirty_areas);
    return true;
}

} // namespace

void c593_hold_use(const void* nav) {
    // Same level only: after travel and before the new map's first rebuild call the table still names the old map's
    // navmeshes, and a new one at a reused address must not replay into the old generator.
    C593Hold* const h = nav != nullptr && read_ptr(nav, kObjectOuter) == g_c593_hold_level ? c593_hold_find(nav)
                                                                                             : nullptr;
    if (h == nullptr || h->used.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    if (h->pending && GetCurrentThreadId() == g_c593_rebuild_thread.load(std::memory_order_relaxed)) {
        c593_hold_replay(*h, "in the resolve");
    }
}

namespace {

void c593_hold_rows() {
    const uint32_t n = g_c593_hold_n.load(std::memory_order_acquire);
    for (uint32_t i = 0; i < n && i < kC593Held; ++i) {
        const C593Hold& h = g_c593_hold[i];
        log::line("card593: %s hold %s: %s, %u calls skipped (%u areas), %u replays%s", side(), h.name.text,
                  h.used.load(std::memory_order_relaxed) ? "used" : "unused", h.calls, h.areas, h.replays,
                  h.pending ? ", box pending" : "");
    }
}

} // namespace

void detour_c577_rebuild(void* generator, const void* dirty_areas) {
    ++g_c577_count;
    const bool client = g_client_session;
    const bool held = !client && dirty_areas != nullptr && c593_hold(generator, dirty_areas);
    const bool row = g_c577_count <= kC577Rows || g_c577_count % kC577Every == 0;
    if (!client && dirty_areas != nullptr) {
        c593_count(dirty_areas);
    }
    if (row) {
        log::line("card577: %s t=%llu RebuildDirtyAreas #%u %s", side(),
                  static_cast<unsigned long long>(GetTickCount64()), g_c577_count,
                  client ? "skipped" : held ? "held (card 593)" : "ran");
        if (!client) {
            c593_row();
            c593_resolve_rows();
            c593_hold_rows();
        }
    }
    if (client || held) {
        return;
    }
    g_orig_c577_rebuild(generator, dirty_areas);
}

} // namespace ubw::hooks
