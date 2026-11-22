// Card 132: decals (gore, bullet holes, every material; card 566), one world for every player.
// USTDecalManager (the game instance's +0x1F0) keeps every decal it spawns in one FIFO pool (*(manager+0x40)):
//   +0x31 enabled byte, +0x34 int cap, +0x38/+0x40/+0x44 free TArray (data, num, max), +0x48/+0x50/+0x54 active TArray,
//   oldest first. SpawnDecalAttached / SpawnDecalAtLocation (exe+0x5138D60 / +0x5138C40) both end in exe+0x5120C60,
//   which releases active[0] first when the active count is at the cap, then reuses the free list front or creates a
//   new USTDecalComponent. USTDecalManager::RemoveDecal and USTDecalComponent::LifeSpanCallback (the fade out end,
//   SetFadeOut calls SetLifeSpan(start + duration)) both deactivate the decal, push it on the free list without
//   checking it was active, and drop it from the active list. So releasing a decal twice corrupts the pool: the client
//   checks a decal is in its active list before RemoveDecal.
// Hit decals and death decals share that FIFO, so a client that spawns its own and copies only some of the host's
// evicts different decals. Instead the host sends every gore decal it spawns, in host order, and the client spawns
// only those (its own gore decals are suppressed in the spawn detours, c132_decal_suppress):
//   "M <i> <path>"                    material table row, first use per map
//   "T <i> <component> <target>"      attach target table row: <target> is "@<guid hex>" (an NPC) or the placed
//                                     actor's c95_wire_target "name x y z"
//   "D <id> <m> <t> x y z pitch yaw roll sx sy sz <attach> <life> <sort> <screen> <fade out start> <duration>
//      <destroy> <fade in start> <duration> <age ms>"
//                                     a decal: attach 255 is SpawnDecalAtLocation, t -1 no target; the fade values are
//                                     the host decal's fields (seconds, already scaled by the host fade scale)
//   "E <id>"                          the host released that decal (FIFO eviction, fade end or RemoveDecal)
//   (B rows, the gore burst Niagara relay, are retired: card 136 sends the host's burst Niagara as effect rows)
//   "R <round> <live> <pool>"         digest round marker: the host's tracked decals and pool active count
//   "J <rows>"                        start of a journal replay (a joining PC's first Pong): the client ignores D and
//                                     E rows until it has one, then the replay brings every live decal
// Card 566: every decal is relayed (gore, bullet holes and every other material), not only the gore materials
// (MI_FX_Gore_AlienDecal, MI_FX_Gore_Human_Decal in hb34). "gore" in the names and log rows below means relayed. The journal is the live set, bounded by the pool cap (+0x34, logged).

#include <intrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

DecalRemoveFn g_decal_remove = nullptr;
DecalSetIntFn g_decal_set_sort = nullptr;
DecalSetFloatFn g_decal_set_screen = nullptr;
DecalFadeOutFn g_decal_fade_out = nullptr;
DecalFadeInFn g_decal_fade_in = nullptr;
DecalSetFloatFn g_decal_life_span = nullptr;
int32_t g_c132_decal_cap = 0;
StaticClassFn g_c566_material_class = nullptr; // UMaterialInterface (card 566 b378: client decal material load)

uint64_t c41_guid(const void* actor);                                    // enemies.cpp
void* c41_resolve_guid(void* pc, uint64_t guid);                         // enemies.cpp
void* c39_gore(const void* actor);                                       // enemies.cpp

namespace {

constexpr size_t kManagerPool = 0x40;
constexpr size_t kPoolCap = 0x34;
constexpr size_t kPoolActive = 0x48;
constexpr size_t kPoolActiveNum = 0x50;
constexpr size_t kGameInstanceDecals = 0x1F0;
constexpr size_t kDecalSort = 0x248;      // UDecalComponent::SortOrder (SetSortOrder)
constexpr size_t kDecalScreen = 0x24C;    // FadeScreenSize (SetFadeScreenSize)
constexpr size_t kDecalFadeStart = 0x250; // FadeStartDelay, scaled (SetFadeOut)
constexpr size_t kDecalFadeDur = 0x254;   // FadeDuration, scaled
constexpr size_t kDecalFadeInDur = 0x258; // FadeInDuration (SetFadeIn)
constexpr size_t kDecalFadeInStart = 0x25C;
constexpr size_t kDecalFlags = 0x260;     // bit 0 bDestroyOwnerAfterFade
constexpr uint32_t kFadeScaleRva = 0x093C6518; // float* SetFadeOut multiplies both values by (exe+0x3AA7130)
constexpr uint8_t kAttachAt = 255;             // the row came from SpawnDecalAtLocation
constexpr uint8_t kMobilityMovable = 2;        // EComponentMobility::Movable
constexpr size_t kComponentScale = 0x220;      // ComponentToWorld scale (K2_GetComponentScale, remote_body.cpp)

constexpr int32_t kDecalMax = 512;    // live host decals tracked per side (the pool cap is logged against it)
constexpr int32_t kDecalEvents = 512; // host M, T, E and R rows per tick (card 309: D rows go from the live rows' sent bit)
constexpr int32_t kDecalMats = 16;
constexpr int32_t kDecalTargets = 64;
constexpr int32_t kPoolScan = 1024;   // active entries read per scan
constexpr int32_t kCallers = 16;      // distinct decal caller RVAs logged per map
constexpr int32_t kDecalRow = 320;    // one row's text (an M row holds a path of up to kAssetPathChars; card 230: a T row up to 257)
constexpr uint32_t kDecalLines = 30;  // per side per map for per-row lines
constexpr ULONGLONG kMinuteMs = 60000;
constexpr float kLifeMarginS = 2.0f;  // a client copy outlives the host's by this, so the E row releases it

struct DecalRow {
    uint32_t id;
    int16_t mat;
    int16_t target;
    Vec3 loc;
    Vec3 rot;
    Vec3 size;
    uint8_t attach;
    uint8_t destroy;
    bool fields; // the post-spawn fields below were read from the decal
    float life;
    int32_t sort;
    float screen;
    float fade_start;
    float fade_dur;
    float in_start;
    float in_dur;
    ULONGLONG born;
    void* decal;
    bool sent; // card 309, host: the D row went to the clients (host_flush sends every live row with it clear)
};

struct DecalEvent {
    char kind; // 'M', 'T', 'D', 'E', 'R'
    int32_t index; // M, T: table index; R: live count
    uint32_t round;
    int32_t pool;
    DecalRow row; // D: the decal; E: row.id
};

struct DecalMat {
    void* material;
    C74Path path;
    bool tried; // client: resolve attempted
};

struct DecalTarget {
    void* component; // client only: the resolved component (placed targets); card 206: the host keeps no pointer
    C31Weak component_weak; // card 331: component's object; a component gone since (sublevel unload) is resolved again
    char comp[kChanNameMax + 1];
    char where[kC95TargetMax + 48]; // "@guid" or "name x y z" (card 230: a wire target name)
    bool tried;
};

// Client: a parsed D row and the host decal's age when the row was sent.
struct ClientD {
    DecalRow r;
    uint64_t age;
};

// Client: a host decal's copy. Card 142: decal null is a held row, a D row whose '@guid' target is not mapped yet
// (the rejoin replay runs before the corpse copies are relevant to the new connection). It is retried each step
// (client_held_step), applied late with its wait added to its age, removed by its E row, dropped at its fade end.
struct DecalCopy {
    uint32_t id;
    void* decal;
    ClientD held;      // decal null: the row
    ULONGLONG arrived; // decal null: when the row came
};

struct DecalHost {
    DecalRow live[kDecalMax]; // spawn order
    int32_t live_n;
    DecalEvent ev[kDecalEvents];
    int32_t ev_n;
    DecalMat mats[kDecalMats];
    int32_t mat_n;
    DecalTarget targets[kDecalTargets];
    int32_t target_n;
    uint64_t callers[kCallers];
    int32_t caller_n;
    void* manager;
    uint32_t next_id;
    int32_t cap;
    // map totals
    uint32_t spawned, evicted_fifo, released, dropped, untracked_full, no_target, socket, rounds;
    uint32_t world_rows; // attached to fixed world geometry, sent as world-space rows (host_world_row)
    uint32_t e_sent;                     // E rows sent (releases)
    uint32_t d_sent;                     // card 309: D rows sent from the live rows' sent bit
    uint32_t coalesced;                  // card 309: decals released before their D row went (no D, no E row)
    uint32_t d_tick_peak;                // card 309: most D rows sent in one flush
    int32_t cap_set;                     // the test cap written this map, 0 none
    uint32_t frames, sent, rows, bytes;
    // per minute
    ULONGLONG minute_at;
    uint32_t min_rows, min_bytes, min_frames, min_d, min_e;
    uint32_t lines;
    uint32_t placed_lines; // rows attached to a placed actor (hb41 review note 7: the crate decal proof)
    uint32_t world_lines;  // card 206: world rows logged (host_world_pose), attach modes other than 0
    uint32_t world_lines_rel; // card 196 task A: attach-0 (KeepRelativeOffset) world rows logged, their own cap
};

struct DecalClient {
    DecalCopy copy[kDecalMax];
    int32_t copy_n;
    DecalMat mats[kDecalMats];
    DecalTarget targets[kDecalTargets];
    bool ready;      // a J row came this map
    bool applying;   // the relay's own spawn is running: not suppressed
    // map totals
    uint32_t received, applied, released, suppressed, ignored, dups, no_mat, no_target, at_fallback, nulls;
    uint32_t expired, no_decal, gone_early, full, bad, rounds, round_warn;
    // c263L336 follow-up (user rule): the client pool is plain FIFO, oldest first, host copies included; the cap
    // never grows. A copy the FIFO takes moves here (its host id) so the host's later E row is expected, not a WARN.
    uint32_t evicted[kDecalMax];
    int32_t evicted_n;
    uint32_t copies_evicted, evicted_released, evicted_lost; // FIFO took a copy; its E row came; ring full, oldest lost
    int32_t cap_set;                   // the test cap written this map, 0 none
    uint32_t lines;
    uint32_t placed_lines; // copies attached to a placed actor (hb41 review note 7: the crate decal proof)
    // card 142: held rows (DecalCopy decal null)
    C31Weak pc;            // the PC the rows came in on (c41_resolve_guid for the retry)
    uint32_t held, late, held_dropped, held_released, held_lines;
    uint32_t held_failed;  // a held row whose guid mapped but whose place failed (held = late + dropped + released +
                           // failed + pending)
    uint32_t mapped_lines; // the capped "now maps to" lines
};

DecalHost g_dh{};
DecalClient g_dc{};

// Card 566: every decal is relayed (was gore only). Bullet holes drawn on the host went out as card 136 effect rows
// (card 552), which no journal keeps, so a checkpoint restart (the client loads a fresh map, the host reloads in
// place) left the host's holes on the host only. One journal for every decal also keeps the shared FIFO in host order.
bool decal_relayed(const void* material) {
    return material != nullptr;
}

void* decal_pool(const void* manager) {
    return manager != nullptr ? read_ptr(manager, kManagerPool) : nullptr;
}

int32_t decal_pool_num(const void* pool) {
    return pool != nullptr ? read_i32(pool, kPoolActiveNum) : -1;
}

int cmp_ptr(const void* a, const void* b) {
    const uintptr_t x = *static_cast<const uintptr_t*>(a);
    const uintptr_t y = *static_cast<const uintptr_t*>(b);
    return x < y ? -1 : (x > y ? 1 : 0);
}

// The pool's active decals, sorted by address, into `out` (at most kPoolScan). Returns the count.
int32_t decal_active_sorted(const void* pool, void** out) {
    const int32_t num = decal_pool_num(pool);
    void* const* const data = pool != nullptr ? static_cast<void* const*>(read_ptr(pool, kPoolActive)) : nullptr;
    if (data == nullptr || num <= 0) {
        return 0;
    }
    const int32_t n = num < kPoolScan ? num : kPoolScan;
    memcpy(out, data, static_cast<size_t>(n) * sizeof(void*));
    qsort(out, static_cast<size_t>(n), sizeof(void*), cmp_ptr);
    return n;
}

bool decal_in(void* const* sorted, int32_t n, const void* decal) {
    return bsearch(&decal, sorted, static_cast<size_t>(n), sizeof(void*), cmp_ptr) != nullptr;
}

bool decal_active(const void* pool, const void* decal) {
    const int32_t num = decal_pool_num(pool);
    void* const* const data = pool != nullptr ? static_cast<void* const*>(read_ptr(pool, kPoolActive)) : nullptr;
    for (int32_t i = 0; data != nullptr && i < num && i < kPoolScan; ++i) {
        if (data[i] == decal) {
            return true;
        }
    }
    return false;
}

float decal_fade_scale() {
    const float* const p = g_exe_base != nullptr ? *reinterpret_cast<const float* const*>(g_exe_base + kFadeScaleRva)
                                                 : nullptr;
    return p != nullptr && *p > 1e-8f ? *p : 0.0f;
}

// ---- Host ----

DecalEvent* host_event(char kind) {
    if (g_dh.ev_n >= kDecalEvents) {
        ++g_dh.dropped;
        if (note_event_cap(g_dh.lines, kDecalLines, "card132 decal")) {
            log::line("WARN card132: host decal rows past %d this tick: a %c row dropped, the client will differ",
                      kDecalEvents, kind);
        }
        return nullptr;
    }
    DecalEvent* const e = &g_dh.ev[g_dh.ev_n++];
    *e = DecalEvent{};
    e->kind = kind;
    return e;
}

// Card 309: a decal whose D row has not gone yet is released with no row (the client never had it).
void host_release(int32_t i, bool fifo) {
    if (!g_dh.live[i].sent) {
        ++g_dh.coalesced;
    } else {
        DecalEvent* const e = host_event('E');
        if (e != nullptr) {
            e->row.id = g_dh.live[i].id;
        }
    }
    ++(fifo ? g_dh.evicted_fifo : g_dh.released);
    memmove(&g_dh.live[i], &g_dh.live[i + 1], static_cast<size_t>(g_dh.live_n - i - 1) * sizeof(DecalRow));
    --g_dh.live_n;
}

// Host: E rows for tracked decals no longer active (fade end, RemoveDecal, or a FIFO release already done).
void host_scan(const void* manager) {
    if (g_dh.live_n == 0) {
        return;
    }
    static void* sorted[kPoolScan];
    const int32_t n = decal_active_sorted(decal_pool(manager), sorted);
    for (int32_t i = g_dh.live_n - 1; i >= 0; --i) {
        if (!decal_in(sorted, n, g_dh.live[i].decal)) {
            host_release(i, false);
        }
    }
}

int16_t host_material(void* material) {
    for (int32_t i = 0; i < g_dh.mat_n; ++i) {
        if (g_dh.mats[i].material == material) {
            return static_cast<int16_t>(i);
        }
    }
    if (g_dh.mat_n >= kDecalMats) {
        return -1;
    }
    DecalMat& m = g_dh.mats[g_dh.mat_n];
    m.material = material;
    m.path = c74_asset_path(material);
    if (strcmp(m.path.text, "-") == 0) {
        return -1;
    }
    log::line("card132: host decal material %d %s: relayed", g_dh.mat_n, m.path.text);
    DecalEvent* const e = host_event('M');
    if (e != nullptr) {
        e->index = g_dh.mat_n;
    }
    return static_cast<int16_t>(g_dh.mat_n++);
}

// Card 206: an NPC is the only '@guid' target the client can resolve (c41_resolve accepts NPCs only).
bool host_npc(const void* owner) {
    return owner != nullptr && g_npc_class != nullptr && is_a(owner, g_npc_class());
}

// The attach target's wire text: an NPC by NetGUID, else the placed actor. Card 206: the component name keeps its
// FName number (c50_name), so a Landscape tile or an Actor_N mesh names the one the decal is on, not the number-0 one
// a bare name finds on the client. False when it has neither (an NPC with no NetGUID included).
bool host_target_text(void* component, DecalTarget& t) {
    void* const owner = read_ptr(component, kComponentOwner);
    if (owner == nullptr || c50_name(component, t.comp, static_cast<int32_t>(sizeof(t.comp))) <= 0) {
        return false;
    }
    if (host_npc(owner)) {
        const uint64_t guid = c41_guid(owner);
        snprintf(t.where, sizeof(t.where), "@%llx", static_cast<unsigned long long>(guid));
        return guid != 0;
    }
    return strchr(t.comp, ' ') == nullptr && c95_wire_target(owner, t.where, sizeof(t.where)) > 0;
}

// Senior note 7 (hb40: InstancedFoliageActor_0's FoliageInstancedStaticMeshComponent, at 0 0 0, not found by name on
// the client): a decal kept at its world position on geometry that never dies in a session (foliage instances,
// landscape, BSP) is the same decal as one spawned at that location, so it goes out as a world-space row and the
// client needs no lookup of the target at all. Only these owner classes: a placed actor the client can resolve
// (a satchel building, a killable crate, a static mesh actor) keeps its attached row, so its decal dies with it on
// both sides (hb41 review: a world-space copy would float after the 'S' or 'K' row).
// hb50: "Actor" is the plain native class: a placed Actor_N at 0 0 0 holding a Static InstancedStaticMeshComponent
// (merged level geometry, placed 1 on the host) has no gameplay that could destroy it, and the client resolved
// none of them (targets 2, 5, 7 not found), so it is world geometry too. Blueprint classes never match it.
constexpr const char* kWorldGeometryClasses[] = {"InstancedFoliageActor", "Landscape", "LandscapeStreamingProxy",
                                                 "Brush", "WorldSettings", "Actor"};

// Card 206: every attach mode (a KeepRelativeOffset row's pose is taken from the decal, host_world_pose). An NPC
// owner keeps its '@guid' row; any other owner's NetGUID is not a target (a static guid such as the Landscape's 0xc5
// maps on the client only when a replicated reference arrives).
bool host_world_row(const void* component, uint8_t attach) {
    if (component == nullptr || attach == kAttachAt) {
        return false;
    }
    const void* const owner = read_ptr(component, kComponentOwner);
    if (owner == nullptr) {
        return true;
    }
    if (host_npc(owner)) {
        return false;
    }
    // hb48, hb49: InstancedStaticMeshComponent of Actor_0 at 0 0 0 was not found on the client. An owner with no
    // NetGUID that was not loaded with a level was spawned at runtime on this machine only: the client resolves placed
    // actors alone (c95_find_nearest), so an attached row could never land and goes out world-space instead, whatever
    // the component's mobility (hb49: this check after the Movable test let the row through).
    if (!c50_placed(owner)) {
        return true;
    }
    if ((static_cast<const uint8_t*>(component)[kComponentMobility] & 0x0F) == kMobilityMovable ||
        (g_c130_health_class != nullptr && find_component(owner, g_c130_health_class) != nullptr)) {
        return false;
    }
    const ObjectName cls = class_name(owner);
    bool geometry = false;
    for (const char* const name : kWorldGeometryClasses) {
        geometry = geometry || strcmp(cls.text, name) == 0;
    }
    return geometry;
}

// Card 206: the table is keyed by the wire identity (component text and where), not by a component pointer, so a
// component freed and reused at the same address (a map load in place) cannot alias an old row.
int16_t host_target(void* component) {
    if (component == nullptr) {
        return -1;
    }
    DecalTarget t{};
    if (!host_target_text(component, t)) {
        return -1;
    }
    for (int32_t i = 0; i < g_dh.target_n; ++i) {
        if (strcmp(g_dh.targets[i].comp, t.comp) == 0 && strcmp(g_dh.targets[i].where, t.where) == 0) {
            return static_cast<int16_t>(i);
        }
    }
    if (g_dh.target_n >= kDecalTargets) {
        return -1;
    }
    g_dh.targets[g_dh.target_n] = t;
    DecalEvent* const e = host_event('T');
    if (e != nullptr) {
        e->index = g_dh.target_n;
    }
    // One line per new target (kDecalTargets bounds them): what the client must resolve.
    const void* const owner = read_ptr(component, kComponentOwner);
    log::line("card132: host decal target %d %s of %s: owner class %s, placed %d, mobility %d", g_dh.target_n,
              t.comp, t.where, class_name(owner).text, c50_placed(owner) ? 1 : 0,
              static_cast<const uint8_t*>(component)[kComponentMobility] & 0x0F);
    return static_cast<int16_t>(g_dh.target_n++);
}

// Card 206: a world row's pose is the spawned decal's ComponentToWorld, whatever the attach mode (a KeepRelativeOffset
// row's location and rotation are relative to the component). The client spawns its copy at scale 1, so the line
// carries the decal's world scale.
constexpr uint32_t kWorldLines = 8;

void host_world_pose(DecalRow& r, const void* component, uint8_t attach) {
    const auto* const d = static_cast<const uint8_t*>(r.decal);
    const Vec3 arg = r.loc;
    const Vec3 arg_rot = r.rot;
    Vec3 scale{};
    memcpy(&r.loc, d + kComponentTranslation, sizeof(r.loc));
    memcpy(&scale, d + kComponentScale, sizeof(scale));
    r.rot = component_rotator(r.decal);
    // Each cap on its own, so attach-1 rows cannot spend the lines a restore (attach 0) row needs.
    const bool rel = attach == 0;
    if (!note_event_cap(rel ? g_dh.world_lines_rel : g_dh.world_lines, kWorldLines,
                        rel ? "card206 world row (attach 0)" : "card206 world row (attach not 0)")) {
        return;
    }
    const void* const owner = read_ptr(component, kComponentOwner);
    char comp[kChanNameMax + 1];
    char name[kChanNameMax + 1];
    if (c50_name(component, comp, static_cast<int32_t>(sizeof(comp))) <= 0) {
        snprintf(comp, sizeof(comp), "-");
    }
    if (owner == nullptr || c50_name(owner, name, static_cast<int32_t>(sizeof(name))) <= 0) {
        snprintf(name, sizeof(name), "-");
    }
    log::line("card206: host decal %u world row from attach %u: owner %s %s, component %s, spawn arg (%.1f, %.1f, "
              "%.1f) rot (%.2f, %.2f, %.2f), world (%.1f, %.1f, %.1f) rot (%.2f, %.2f, %.2f), world scale (%.3f, "
              "%.3f, %.3f)", r.id, attach, owner != nullptr ? class_name(owner).text : "(none)", name, comp, arg.x,
              arg.y, arg.z, arg_rot.x, arg_rot.y, arg_rot.z, r.loc.x, r.loc.y, r.loc.z, r.rot.x, r.rot.y, r.rot.z,
              scale.x, scale.y, scale.z);
}

void host_caller(const void* material, const Vec3* size, uint64_t rva, bool gore) {
    for (int32_t i = 0; i < g_dh.caller_n; ++i) {
        if (g_dh.callers[i] == rva) {
            return;
        }
    }
    if (g_dh.caller_n >= kCallers) {
        return;
    }
    g_dh.callers[g_dh.caller_n++] = rva;
    log::line("card132: host decal caller exe+0x%llX: %s size (%.0f, %.0f, %.0f), %s",
              static_cast<unsigned long long>(rva), object_name(material).text, size->x, size->y, size->z,
              gore ? "relayed" : "not relayed, local");
}

void host_read_fields(DecalRow& r) {
    if (r.fields || r.decal == nullptr) {
        return;
    }
    r.sort = read_i32(r.decal, kDecalSort);
    r.screen = read_f32(r.decal, kDecalScreen);
    r.fade_start = read_f32(r.decal, kDecalFadeStart);
    r.fade_dur = read_f32(r.decal, kDecalFadeDur);
    r.in_dur = read_f32(r.decal, kDecalFadeInDur);
    r.in_start = read_f32(r.decal, kDecalFadeInStart);
    r.destroy = static_cast<uint8_t>(*(static_cast<const uint8_t*>(r.decal) + kDecalFlags) & 1u);
    r.fields = true;
}

int row_text(const DecalEvent& e, ULONGLONG now, bool replay, char* out, size_t cap) {
    const DecalRow& r = e.row;
    switch (e.kind) {
    case 'M':
        return snprintf(out, cap, "M %d %s;", e.index, g_dh.mats[e.index].path.text);
    case 'T':
        return snprintf(out, cap, "T %d %s %s;", e.index, g_dh.targets[e.index].comp, g_dh.targets[e.index].where);
    case 'D':
        return snprintf(out, cap, "D %u %d %d %.1f %.1f %.1f %.2f %.2f %.2f %.1f %.1f %.1f %u %.2f %d %.4f %.2f %.2f "
                        "%u %.2f %.2f %llu;", r.id, r.mat, r.target, r.loc.x, r.loc.y, r.loc.z, r.rot.x, r.rot.y,
                        r.rot.z, r.size.x, r.size.y, r.size.z, r.attach, r.life, r.sort, r.screen, r.fade_start,
                        r.fade_dur, r.destroy, r.in_start, r.in_dur,
                        static_cast<unsigned long long>(replay ? now - r.born : 0));
    case 'E':
        return snprintf(out, cap, "E %u;", r.id);
    case 'R':
        return snprintf(out, cap, "R %u %d %d;", e.round, e.index, e.pool);
    default:
        return 0;
    }
}

struct DecalOut {
    char frame[kChanTextMax + 1];
    size_t used;
    int32_t frames, sent, rows, bytes;
};

void out_send(DecalOut& o, const void* world, void* pc) {
    if (o.used == 0) {
        return;
    }
    o.sent += pc != nullptr ? (c95_send_one(pc, ChanOp::Decal, o.frame) ? 1 : 0)
                            : c95_send_all(world, ChanOp::Decal, o.frame).sent;
    o.bytes += static_cast<int32_t>(o.used);
    ++o.frames;
    o.used = 0;
}

void out_row(DecalOut& o, const DecalEvent& e, ULONGLONG now, bool replay, const void* world, void* pc) {
    char row[kDecalRow];
    const int n = row_text(e, now, replay, row, sizeof(row));
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(row)) {
        ++g_dh.dropped;
        log::line("WARN card132: host decal %c row %u does not fit", e.kind, e.row.id);
        return;
    }
    if (o.used + static_cast<size_t>(n) > static_cast<size_t>(kChanTextMax)) {
        out_send(o, world, pc);
    }
    memcpy(o.frame + o.used, row, static_cast<size_t>(n) + 1);
    o.used += static_cast<size_t>(n);
    ++o.rows;
}

void host_minute(ULONGLONG now) {
    if (g_dh.minute_at == 0) {
        g_dh.minute_at = now;
    }
    if (now - g_dh.minute_at < kMinuteMs) {
        return;
    }
    const void* const pool = decal_pool(g_dh.manager);
    log::line("card132: host decal minute: %u rows (%u D, %u E), %u bytes in %u frames; live %d, pool %d of cap %d",
              g_dh.min_rows, g_dh.min_d, g_dh.min_e, g_dh.min_bytes, g_dh.min_frames, g_dh.live_n,
              decal_pool_num(pool), g_dh.cap);
    g_dh.minute_at = now;
    g_dh.min_rows = g_dh.min_bytes = g_dh.min_frames = g_dh.min_d = g_dh.min_e = 0;
}

// Card 309: live decals whose D row has not gone yet.
int32_t host_unsent() {
    int32_t n = 0;
    for (int32_t i = 0; i < g_dh.live_n; ++i) {
        n += g_dh.live[i].sent ? 0 : 1;
    }
    return n;
}

// Card 309: a D row for every live decal with its sent bit clear, in spawn order, then the bit set. Sent after the
// tick's M, T, E and R rows: the tables a D row names go first, and an R row counts only sent decals.
void host_flush_d(DecalOut& o, ULONGLONG now, const void* world) {
    uint32_t n = 0;
    for (int32_t i = 0; i < g_dh.live_n; ++i) {
        DecalRow& r = g_dh.live[i];
        if (r.sent) {
            continue;
        }
        host_read_fields(r);
        DecalEvent e{};
        e.kind = 'D';
        e.row = r;
        out_row(o, e, now, false, world, nullptr);
        r.sent = true;
        ++n;
    }
    g_dh.min_d += n;
    g_dh.d_sent += n;
    g_dh.d_tick_peak = n > g_dh.d_tick_peak ? n : g_dh.d_tick_peak;
}

// Host: the tick's rows, in host order, to every client.
void host_flush(const void* world) {
    const ULONGLONG now = GetTickCount64();
    DecalOut o{};
    for (int32_t i = 0; world != nullptr && i < g_dh.ev_n; ++i) {
        const DecalEvent& e = g_dh.ev[i];
        g_dh.min_e += e.kind == 'E' ? 1u : 0u;
        g_dh.e_sent += e.kind == 'E' ? 1u : 0u;
        out_row(o, e, now, false, world, nullptr);
    }
    if (world != nullptr) {
        host_flush_d(o, now, world);
    }
    out_send(o, world, nullptr);
    g_dh.ev_n = 0;
    g_dh.frames += static_cast<uint32_t>(o.frames);
    g_dh.sent += static_cast<uint32_t>(o.sent);
    g_dh.rows += static_cast<uint32_t>(o.rows);
    g_dh.bytes += static_cast<uint32_t>(o.bytes);
    g_dh.min_rows += static_cast<uint32_t>(o.rows);
    g_dh.min_bytes += static_cast<uint32_t>(o.bytes);
    g_dh.min_frames += static_cast<uint32_t>(o.frames);
    host_minute(now);
}

void host_cap(const void* manager) {
    if (g_dh.cap != 0) {
        return;
    }
    const void* const pool = decal_pool(manager);
    g_dh.cap = pool != nullptr ? read_i32(pool, kPoolCap) : 0;
    log::line("%scard132: host decal pool cap %d (journal bound %d), active %d at the first spawn this map",
              g_dh.cap > kDecalMax ? "WARN " : "", g_dh.cap, kDecalMax, decal_pool_num(pool));
}

// ---- Client ----

// Card 566 b378: run c551b376a found no MI_FX_BulletGround_Decal on the client (the host relays it, the client never
// loaded it), so a material the find misses is loaded. A loaded material goes into the game instance's
// ReferencedObjects so GC keeps it while rows point at it; the few relayed materials stay held for the session.
void* client_material(void* pc, int32_t m) {
    if (m < 0 || m >= kDecalMats) {
        return nullptr;
    }
    DecalMat& d = g_dc.mats[m];
    if (d.material != nullptr || d.tried || d.path.text[0] == '\0') {
        return d.material;
    }
    d.tried = true;
    wchar_t wide[kAssetPathChars];
    if (swprintf(wide, kAssetPathChars, L"%hs", d.path.text) <= 0) {
        log::line("WARN card132: client decal material %d path does not convert", m);
        return nullptr;
    }
    d.material = g_static_find_object(nullptr, nullptr, wide, false);
    if (d.material != nullptr || g_c566_material_class == nullptr || g_static_load_object == nullptr) {
        if (d.material == nullptr) {
            log::line("WARN card132: client decal material %d %s not found", m, d.path.text);
        }
        return d.material;
    }
    d.material = g_static_load_object(g_c566_material_class(), nullptr, wide, nullptr, kC29LoadFlags, nullptr, true,
                                      nullptr);
    void* const world = d.material != nullptr && pc != nullptr ? g_actor_get_world(pc) : nullptr;
    void* const gi = world != nullptr ? read_ptr(world, kWorldOwningGameInstance) : nullptr;
    if (gi != nullptr && g_gi_register_ref != nullptr) {
        g_gi_register_ref(gi, d.material);
    }
    log::line("%scard132: client decal material %d %s %s", d.material == nullptr ? "WARN " : "", m, d.path.text,
              d.material == nullptr ? "not found, load failed" : (gi != nullptr ? "loaded, held" : "loaded, not held"));
    return d.material;
}

void* client_target(void* pc, int32_t t) {
    if (t < 0 || t >= kDecalTargets || g_dc.targets[t].where[0] == '\0') {
        return nullptr;
    }
    DecalTarget& d = g_dc.targets[t];
    if (d.where[0] == '@') {
        void* const npc = c41_resolve_guid(pc, strtoull(d.where + 1, nullptr, 16));
        return npc != nullptr ? c95_component(npc, d.comp) : nullptr;
    }
    if (d.component != nullptr && c31_weak_get(d.component_weak) != d.component) {
        d.component = nullptr; // card 331: the cached component is gone; one more resolve by name
        d.tried = false;
    }
    if (d.component == nullptr && !d.tried) {
        d.tried = true;
        char name[kC95TargetMax + 1]; // card 230
        static_assert(sizeof(name) == 130, "card 230: the %129s width below");
        Vec3 at{};
        void* actor = nullptr;
        if (sscanf_s(d.where, "%129s %lf %lf %lf", name, static_cast<unsigned>(sizeof(name)), &at.x, &at.y, &at.z) == 4) {
            actor = c95_find_nearest(g_actor_get_world(pc), name, at);
            d.component = actor != nullptr ? c95_component(actor, d.comp) : nullptr;
            c31_weak_set(d.component_weak, d.component);
        }
        if (d.component == nullptr) { // card 206: which step failed, the actor (by name near x y z) or its component
            char found[kChanNameMax + 1];
            if (actor == nullptr || c50_name(actor, found, static_cast<int32_t>(sizeof(found))) <= 0) {
                snprintf(found, sizeof(found), "none");
            }
            log::line("WARN card132: client decal target %d %s of %s not found: actor %s %s, component found 0", t,
                      d.comp, d.where, found, actor != nullptr ? class_name(actor).text : "-");
        }
    }
    return d.component;
}

// The decal manager of this machine's game instance; `context` is any world context object (a PC or the world).
void* client_manager(const void* context) {
    void* const gi = context != nullptr ? g_get_st_game_instance(context) : nullptr;
    return gi != nullptr ? read_ptr(gi, kGameInstanceDecals) : nullptr;
}

int32_t client_copy(uint32_t id) {
    for (int32_t i = 0; i < g_dc.copy_n; ++i) {
        if (g_dc.copy[i].id == id) {
            return i;
        }
    }
    return -1;
}

void client_drop(int32_t i) {
    memmove(&g_dc.copy[i], &g_dc.copy[i + 1], static_cast<size_t>(g_dc.copy_n - i - 1) * sizeof(DecalCopy));
    --g_dc.copy_n;
}

// Client: copies the pool no longer holds (its own fade timer or FIFO went first), each a WARN.
void client_scan(void* manager, const char* when) {
    if (g_dc.copy_n == 0) {
        return;
    }
    static void* sorted[kPoolScan];
    const int32_t n = decal_active_sorted(decal_pool(manager), sorted);
    for (int32_t i = g_dc.copy_n - 1; i >= 0; --i) {
        if (g_dc.copy[i].decal == nullptr || decal_in(sorted, n, g_dc.copy[i].decal)) { // held rows have no decal
            continue;
        }
        ++g_dc.gone_early;
        if (note_event_cap(g_dc.lines, kDecalLines, "card132 decal")) {
            log::line("WARN card132: client copy of host decal %u left the pool before the host released it (%s)",
                      g_dc.copy[i].id, when);
        }
        client_drop(i);
    }
}

// Client: the evicted list holds host ids whose copy the client's FIFO took; the oldest goes when it is full.
void client_evicted_add(uint32_t id) {
    if (g_dc.evicted_n >= kDecalMax) {
        memmove(&g_dc.evicted[0], &g_dc.evicted[1], static_cast<size_t>(kDecalMax - 1) * sizeof(uint32_t));
        --g_dc.evicted_n;
        ++g_dc.evicted_lost;
    }
    g_dc.evicted[g_dc.evicted_n++] = id;
}

// Client: true (and the id leaves the list) when the host's E row names a copy the client's FIFO already took.
bool client_evicted_take(uint32_t id) {
    for (int32_t i = 0; i < g_dc.evicted_n; ++i) {
        if (g_dc.evicted[i] == id) {
            memmove(&g_dc.evicted[i], &g_dc.evicted[i + 1], static_cast<size_t>(g_dc.evicted_n - i - 1) * sizeof(uint32_t));
            --g_dc.evicted_n;
            return true;
        }
    }
    return false;
}

// Client, before any decal spawn: the pool's FIFO (exe+0x5120C60) releases active[0] when the active count is at the
// cap, whatever its age and whether it is a host copy or the client's own. A copy there moves to the evicted list.
void client_fifo_check(void* manager) {
    const void* const pool = decal_pool(manager);
    const int32_t cap = pool != nullptr ? read_i32(pool, kPoolCap) : -1;
    const int32_t num = decal_pool_num(pool);
    void* const* const data = pool != nullptr ? static_cast<void* const*>(read_ptr(pool, kPoolActive)) : nullptr;
    if (data == nullptr || num <= 0 || num < cap) {
        return;
    }
    for (int32_t i = 0; i < g_dc.copy_n; ++i) {
        if (g_dc.copy[i].decal == data[0]) {
            ++g_dc.copies_evicted;
            client_evicted_add(g_dc.copy[i].id);
            client_drop(i);
            return;
        }
    }
}

bool client_parse_d(const char* row, ClientD& d) {
    unsigned id = 0, attach = 0, destroy = 0;
    int mat = 0, target = 0, sort = 0;
    unsigned long long age = 0;
    DecalRow& r = d.r;
    const int n = sscanf_s(row, "D %u %d %d %lf %lf %lf %lf %lf %lf %lf %lf %lf %u %f %d %f %f %f %u %f %f %llu", &id,
                         &mat, &target, &r.loc.x, &r.loc.y, &r.loc.z, &r.rot.x, &r.rot.y, &r.rot.z, &r.size.x,
                         &r.size.y, &r.size.z, &attach, &r.life, &sort, &r.screen, &r.fade_start, &r.fade_dur,
                         &destroy, &r.in_start, &r.in_dur, &age);
    r.id = id;
    r.mat = static_cast<int16_t>(mat);
    r.target = static_cast<int16_t>(target);
    r.attach = static_cast<uint8_t>(attach);
    r.sort = sort;
    r.destroy = static_cast<uint8_t>(destroy);
    d.age = age;
    return n == 22;
}

float later(float v, float age_s) {
    return v - age_s > 0.0f ? v - age_s : 0.0f;
}

// Client: the host decal's post-spawn fields on the copy; a replayed one is `age_s` older.
void client_fields(void* decal, const DecalRow& r, float age_s) {
    const float scale = decal_fade_scale();
    const float start = later(r.fade_start, age_s);
    g_decal_set_sort(decal, r.sort);
    g_decal_set_screen(decal, r.screen);
    g_decal_fade_out(decal, scale > 0.0f ? start / scale : 0.0f, scale > 0.0f ? r.fade_dur / scale : 0.0f,
                     r.destroy != 0);
    g_decal_fade_in(decal, later(r.in_start, age_s), r.in_dur);
    const float life = r.life > 0.0f ? later(r.life, age_s) : 0.0f;
    const float fade_end = r.fade_start + r.fade_dur > 0.0f ? start + r.fade_dur : 0.0f;
    const float end = life > fade_end ? life : fade_end;
    if (end > 0.0f) {
        g_decal_life_span(decal, end + kLifeMarginS); // the host's E row releases it first
    }
}

// hb41 review note 7: a decal attached to a placed actor (not an NPC by NetGUID, not world geometry) is logged on
// both sides, a few per map, so a run can follow one onto a crate that is then killed.
constexpr uint32_t kPlacedLines = 6;

bool placed_target(const DecalTarget* targets, int16_t index) {
    return index >= 0 && index < kDecalTargets && targets[index].where[0] != '\0' && targets[index].where[0] != '@';
}

// Card 142: a row whose '@guid' target this client's package map (kConnPackageMap) has no object for yet (an NPC
// copy not relevant to the connection yet). It is held, not spawned. "Not mapped" means no object in the package map,
// not "no NPC" (c142-185-r1). A guid that maps to an object that is not an NPC (c142-185-r1: static guid 0xbf, a placed
// level actor under the start's gore) is not held: it takes the existing path (the world-position fallback for a
// KeepWorldPosition row), as before the card.
// Card 142: looks up a row's '@guid' target in the PC's package map. Returns false when the row has no '@guid' target
// or there is no map to look in; otherwise `object` is what the guid maps to (nullptr: not mapped yet).
bool client_guid_lookup(void* pc, const DecalRow& r, uint64_t& guid, void*& object) {
    guid = 0;
    object = nullptr;
    if (r.attach == kAttachAt || r.target < 0 || r.target >= kDecalTargets || g_dc.targets[r.target].where[0] != '@') {
        return false;
    }
    guid = strtoull(g_dc.targets[r.target].where + 1, nullptr, 16);
    const void* const conn = pc != nullptr ? read_ptr(pc, kPcNetConnection) : nullptr;
    void* const map = conn != nullptr ? read_ptr(conn, kConnPackageMap) : nullptr;
    if (guid == 0 || map == nullptr || g_object_from_guid == nullptr) {
        return false;
    }
    object = g_object_from_guid(map, &guid, false);
    return true;
}

bool client_guid_unmapped(void* pc, const DecalRow& r) {
    uint64_t guid = 0;
    void* object = nullptr;
    return client_guid_lookup(pc, r, guid, object) && object == nullptr;
}

bool client_expired(const DecalRow& r, float age_s) {
    return r.fade_start + r.fade_dur > 0.0f && age_s >= r.fade_start + r.fade_dur;
}

const char* client_where(int16_t t) {
    return t >= 0 && t < kDecalTargets ? g_dc.targets[t].where : "-";
}

// `no_target` is set when the row's target did not resolve and the row has no world position to fall back to.
void* client_spawn(void* pc, void* manager, void* material, const DecalRow& r, float age_s, bool& no_target) {
    void* const component = r.attach != kAttachAt ? client_target(pc, r.target) : nullptr;
    no_target = component == nullptr && r.attach != kAttachAt && r.attach != 1;
    const float life = r.life > 0.0f ? later(r.life, age_s) + kLifeMarginS : r.life;
    g_dc.applying = true;
    void* decal = nullptr;
    if (component != nullptr) {
        decal = g_orig_spawn_decal_attached(manager, material, &r.size, component, 0, &r.loc, &r.rot, r.attach, life);
    } else if (r.attach == kAttachAt || r.attach == 1) { // world position: the same place without the target
        g_dc.at_fallback += r.attach != kAttachAt ? 1u : 0u;
        decal = g_orig_spawn_decal_at(manager, g_actor_get_world(pc), material, &r.size, &r.loc, &r.rot, life);
    } else {
        ++g_dc.no_target;
    }
    g_dc.applying = false;
    if (decal != nullptr) {
        c136_client_extra(material); // card 136 review 2: the relay's gore decals play outside the effects manager
    }
    if (component != nullptr && decal != nullptr && placed_target(g_dc.targets, r.target) &&
        note_event_cap(g_dc.placed_lines, kPlacedLines, "card132 placed-target decals")) {
        log::line("card132: client decal %u attached to placed %s %s at (%.0f, %.0f, %.0f)", r.id,
                  g_dc.targets[r.target].comp, g_dc.targets[r.target].where, r.loc.x, r.loc.y, r.loc.z);
    }
    return decal;
}

// Client: spawn the copy of a row `held_ms` after it came (0 for a row applied on arrival); its age is the row's
// age plus the wait, so the fade ends when the host's does. Returns true when the copy is spawned and tracked.
bool client_place(void* pc, const ClientD& d, ULONGLONG held_ms) {
    const uint64_t age = d.age + held_ms;
    const float age_s = static_cast<float>(age) / 1000.0f;
    void* const material = client_material(pc, d.r.mat);
    void* const manager = client_manager(pc);
    if (material == nullptr || manager == nullptr) {
        ++g_dc.no_mat;
        log::line("WARN card132: client decal %u not spawned: %s", d.r.id, material == nullptr ? "no material" : "no manager");
        return false;
    }
    client_scan(manager, "before a spawn");
    client_fifo_check(manager);
    bool no_target = false;
    void* const decal = client_spawn(pc, manager, material, d.r, age_s, no_target);
    if (decal == nullptr) {
        ++g_dc.nulls;
        log::line("WARN card132: client decal %u not spawned: %s (target %d %s)", d.r.id,
                  no_target ? "no target" : "engine null", d.r.target, client_where(d.r.target));
        return false;
    }
    client_fields(decal, d.r, age_s);
    if (g_dc.copy_n >= kDecalMax) {
        ++g_dc.full;
        log::line("WARN card132: client decal copies past %d: %u not tracked", kDecalMax, d.r.id);
        return false;
    }
    g_dc.copy[g_dc.copy_n++] = DecalCopy{d.r.id, decal, ClientD{}, 0};
    ++g_dc.applied;
    if (note_event_cap(g_dc.lines, kDecalLines, "card132 decal")) {
        log::line("card132: client decal %u spawned at (%.0f, %.0f, %.0f) size (%.0f, %.0f, %.0f) age %llu ms, %d live",
                  d.r.id, d.r.loc.x, d.r.loc.y, d.r.loc.z, d.r.size.x, d.r.size.y, d.r.size.z,
                  static_cast<unsigned long long>(age), g_dc.copy_n);
    }
    return true;
}

// Card 142: hold a row whose '@guid' target is not mapped yet (a plain capped line: it is a WARN only if dropped).
void client_hold(const ClientD& d) {
    if (g_dc.copy_n >= kDecalMax) {
        ++g_dc.full;
        log::line("WARN card132: client decal copies past %d: held %u not tracked", kDecalMax, d.r.id);
        return;
    }
    g_dc.copy[g_dc.copy_n++] = DecalCopy{d.r.id, nullptr, d, GetTickCount64()};
    ++g_dc.held;
    if (note_event_cap(g_dc.held_lines, kDecalLines, "card132 decal held")) {
        log::line("card132: client t=%llu decal %u held: no target %d %s (guid not mapped yet), age %llu ms",
                  static_cast<unsigned long long>(GetTickCount64()), d.r.id, d.r.target, client_where(d.r.target),
                  static_cast<unsigned long long>(d.age));
    }
}

void client_apply_d(void* pc, const char* row) {
    ClientD d{};
    if (!client_parse_d(row, d)) {
        ++g_dc.bad;
        return;
    }
    if (client_copy(d.r.id) >= 0) {
        ++g_dc.dups; // a live row sent before the replay snapshot, or a held row's repeat
        return;
    }
    if (client_expired(d.r, static_cast<float>(d.age) / 1000.0f)) {
        ++g_dc.expired;
        return;
    }
    c31_weak_set(g_dc.pc, pc);
    if (client_guid_unmapped(pc, d.r)) {
        client_hold(d);
        return;
    }
    client_place(pc, d, 0);
}

// Card 142: the capped line when a held row's guid first maps (each held row maps once: it leaves the hold then).
void client_held_mapped(const ClientD& d, uint64_t guid, const void* object) {
    if (object != nullptr && note_event_cap(g_dc.mapped_lines, kDecalLines, "card142 decal guid maps")) {
        log::line("card142: client decal %u guid 0x%llx now maps to %s %s", d.r.id,
                  static_cast<unsigned long long>(guid), class_name(object).text, object_name(object).text);
    }
}

// Card 142: a held row whose guid mapped but whose place failed (client_place logged its own WARN). One WARN per row.
void client_held_failed(void* pc, const ClientD& d, uint64_t guid, const void* object) {
    ++g_dc.held_failed;
    const char* const reason = object == nullptr                           ? "no '@guid' lookup"
                               : c41_resolve_guid(pc, guid) == nullptr ? "guid maps to a non-NPC"
                                                                        : "place failed on the NPC";
    log::line("WARN card142: client decal %u held then not placed: %s (target %d %s, guid 0x%llx maps to %s %s), "
              "%u failed", d.r.id, reason, d.r.target, client_where(d.r.target), static_cast<unsigned long long>(guid),
              object != nullptr ? class_name(object).text : "-", object != nullptr ? object_name(object).text : "-",
              g_dc.held_failed);
}

int32_t client_held_count() {
    int32_t n = 0;
    for (int32_t i = 0; i < g_dc.copy_n; ++i) {
        n += g_dc.copy[i].decal == nullptr ? 1 : 0;
    }
    return n;
}

// Card 142, per step: a held row whose copy is now mapped is applied late; one past its fade end is dropped (WARN).
void client_held_step(ULONGLONG now) {
    void* const pc = c31_weak_get(g_dc.pc);
    if (pc == nullptr) {
        return;
    }
    for (int32_t i = g_dc.copy_n - 1; i >= 0 && i < g_dc.copy_n; --i) {
        if (g_dc.copy[i].decal != nullptr) {
            continue;
        }
        const ClientD d = g_dc.copy[i].held;
        const ULONGLONG held_ms = now - g_dc.copy[i].arrived;
        if (client_expired(d.r, static_cast<float>(d.age + held_ms) / 1000.0f)) {
            ++g_dc.held_dropped;
            log::line("WARN card132: client decal %u dropped while held: its fade ended after %llu ms (target %d %s "
                      "never mapped), %u dropped", d.r.id, static_cast<unsigned long long>(held_ms), d.r.target,
                      client_where(d.r.target), g_dc.held_dropped);
            client_drop(i);
            continue;
        }
        uint64_t guid = 0;
        void* object = nullptr;
        if (client_guid_lookup(pc, d.r, guid, object) && object == nullptr) {
            continue;
        }
        client_drop(i);
        client_held_mapped(d, guid, object);
        if (!client_place(pc, d, held_ms)) {
            client_held_failed(pc, d, guid, object);
            continue;
        }
        ++g_dc.late;
        if (note_event_cap(g_dc.held_lines, kDecalLines, "card132 decal held")) {
            log::line("card132: client t=%llu decal %u applied late: target %d %s mapped after %llu ms held, age "
                      "%llu ms", static_cast<unsigned long long>(now), d.r.id, d.r.target, client_where(d.r.target),
                      static_cast<unsigned long long>(held_ms), static_cast<unsigned long long>(d.age + held_ms));
        }
    }
}

void client_apply_e(void* pc, const char* row) {
    unsigned id = 0;
    if (sscanf_s(row, "E %u", &id) != 1) {
        ++g_dc.bad;
        return;
    }
    const int32_t i = client_copy(id);
    if (i < 0 && client_evicted_take(id)) {
        ++g_dc.evicted_released; // the client's FIFO took this copy first
        return;
    }
    if (i >= 0 && g_dc.copy[i].decal == nullptr) {
        ++g_dc.held_released; // card 142: the host released it before its target copy arrived
        client_drop(i);
        return;
    }
    void* const manager = client_manager(pc);
    if (i < 0 || manager == nullptr || !decal_active(decal_pool(manager), g_dc.copy[i].decal)) {
        ++g_dc.no_decal;
        log::line("WARN card132: client decal release %u found no decal (%s)", id,
                  i < 0 ? "no copy" : (manager == nullptr ? "no manager" : "copy not in the pool"));
        if (i >= 0) {
            client_drop(i);
        }
        return;
    }
    g_decal_remove(manager, g_dc.copy[i].decal);
    client_drop(i);
    ++g_dc.released;
}

void client_apply_r(void* pc, const char* row) {
    unsigned round = 0;
    int live = 0, pool = 0;
    if (sscanf_s(row, "R %u %d %d", &round, &live, &pool) != 3) {
        ++g_dc.bad;
        return;
    }
    void* const manager = client_manager(pc);
    client_scan(manager, "round");
    ++g_dc.rounds;
    const int32_t held = client_held_count();
    // A held row is live on the host and pending here; an evicted copy is live on the host until its E row.
    const bool warn = g_dc.ready && live != g_dc.copy_n + g_dc.evicted_n;
    g_dc.round_warn += warn ? 1u : 0u;
    log::line("%scard132: client decal round %u: host live %d, client live %d; pool active host %d, client %d; "
              "E applied %u (released %u, no decal %u), D/E before the replay %u%s; held %u: pending %d, applied "
              "late %u, dropped %u, released while held %u, failed %u",
              warn ? "WARN " : "", round, live, g_dc.copy_n - held, pool, decal_pool_num(decal_pool(manager)),
              g_dc.released + g_dc.no_decal, g_dc.released, g_dc.no_decal, g_dc.ignored,
              g_dc.ready ? "" : " (no journal replay yet)", g_dc.held, held, g_dc.late, g_dc.held_dropped,
              g_dc.held_released, g_dc.held_failed);
}

void client_table(const char* row) {
    int i = -1;
    if (row[0] == 'M') {
        char path[kAssetPathChars];
        if (sscanf_s(row, "M %d %191[^;]", &i, path, static_cast<unsigned>(sizeof(path))) == 2 && i >= 0 && i < kDecalMats) {
            DecalMat& m = g_dc.mats[i];
            if (strcmp(m.path.text, path) != 0) {
                m = DecalMat{};
                snprintf(m.path.text, sizeof(m.path.text), "%s", path);
            }
            return;
        }
    } else {
        char comp[kChanNameMax + 1];
        char where[kC95TargetMax + 48]; // card 230
        static_assert(sizeof(comp) == 65 && sizeof(where) == 177, "card 230: the %64s and %176[^;] widths below");
        static_assert(2 + 11 + 1 + 64 + 1 + 176 + 2 <= kDecalRow, "card 230: the longest T row fits a row");
        if (sscanf_s(row, "T %d %64s %176[^;]", &i, comp, static_cast<unsigned>(sizeof(comp)), where,
                     static_cast<unsigned>(sizeof(where))) == 3 && i >= 0 && i < kDecalTargets) {
            DecalTarget& t = g_dc.targets[i];
            if (strcmp(t.comp, comp) != 0 || strcmp(t.where, where) != 0) {
                t = DecalTarget{};
                snprintf(t.comp, sizeof(t.comp), "%s", comp);
                snprintf(t.where, sizeof(t.where), "%s", where);
            }
            return;
        }
    }
    ++g_dc.bad;
}

void client_row(void* pc, const char* row) {
    ++g_dc.received;
    switch (row[0]) {
    case 'M':
    case 'T':
        client_table(row);
        return;
    case 'J':
        g_dc.ready = true;
        log::line("card132: client decal journal replay starts (%s), %d copies live", row, g_dc.copy_n);
        return;
    case 'D':
    case 'E':
        if (!g_dc.ready) {
            ++g_dc.ignored; // the replay snapshot holds it
            return;
        }
        row[0] == 'D' ? client_apply_d(pc, row) : client_apply_e(pc, row);
        return;
    case 'R':
        client_apply_r(pc, row);
        return;
    default:
        ++g_dc.bad;
    }
}

// UBWCOOP_DECAL_CAP, both sides, before any decal spawn: the pool cap is set once per map.
void decal_test_cap(void* manager, int32_t& cap_set) {
    void* const pool = decal_pool(manager);
    if (g_c132_decal_cap == 0 || cap_set != 0 || pool == nullptr) {
        return;
    }
    cap_set = g_c132_decal_cap;
    log::line("card132: %s decal pool test cap %d (was %d), active %d", side_text(), cap_set, read_i32(pool, kPoolCap),
              decal_pool_num(pool));
    *reinterpret_cast<int32_t*>(static_cast<uint8_t*>(pool) + kPoolCap) = cap_set;
}

} // namespace

// Client: its own decal spawns are suppressed (every material, card 566); the host's rows bring them (c132_receive_decal).
bool c132_decal_suppress(const void* material) {
    if (!g_client_session || g_dc.applying || !decal_relayed(material)) {
        return false;
    }
    ++g_dc.suppressed;
    return true;
}

// Before any decal spawn. Host: E rows for tracked decals gone since, then for the one the FIFO releases next.
// Client: client_fifo_check (plain FIFO, the cap never grows). Both: the UBWCOOP_DECAL_CAP test cap.
void c132_decal_before(void* manager) {
    if (manager == nullptr) {
        return;
    }
    decal_test_cap(manager, g_client_session ? g_dc.cap_set : g_dh.cap_set);
    if (g_client_session) {
        client_fifo_check(manager);
        return;
    }
    host_scan(manager);
    const void* const pool = decal_pool(manager);
    const int32_t cap = pool != nullptr ? read_i32(pool, kPoolCap) : -1;
    const int32_t num = decal_pool_num(pool);
    void* const* const data = pool != nullptr ? static_cast<void* const*>(read_ptr(pool, kPoolActive)) : nullptr;
    if (data == nullptr || num <= 0 || num < cap) {
        return;
    }
    for (int32_t i = 0; i < g_dh.live_n; ++i) {
        if (g_dh.live[i].decal == data[0]) {
            host_release(i, true);
            return;
        }
    }
}

// Host, after a decal spawn: a gore decal becomes a tracked row (attach kAttachAt for SpawnDecalAtLocation).
void c132_decal_spawned(void* manager, void* decal, void* material, const Vec3* size, void* component,
                        uint64_t socket, const Vec3* location, const Vec3* rotation, uint8_t attach, float life,
                        const void* caller) {
    if (g_client_session || manager == nullptr) {
        return;
    }
    const bool gore = decal_relayed(material);
    host_caller(material, size, exe_rva(caller), gore);
    if (!gore || decal == nullptr) {
        return;
    }
    g_dh.manager = manager;
    host_cap(manager);
    if (g_dh.live_n >= kDecalMax) {
        ++g_dh.untracked_full;
        log::line("WARN card132: host decal journal full (%d): a gore decal not relayed", kDecalMax);
        return;
    }
    for (int32_t i = 0; i < g_dh.live_n; ++i) {
        if (g_dh.live[i].decal == decal) {
            host_release(i, false); // a reused component the scan has not seen leave
            break;
        }
    }
    DecalRow r{};
    r.id = ++g_dh.next_id;
    r.mat = host_material(material);
    const bool world_row = host_world_row(component, attach);
    g_dh.world_rows += world_row ? 1u : 0u;
    r.target = attach != kAttachAt && !world_row ? host_target(component) : -1;
    r.loc = *location;
    r.rot = *rotation;
    r.size = *size;
    r.attach = world_row ? kAttachAt : attach;
    r.life = life;
    r.born = GetTickCount64();
    r.decal = decal;
    if (world_row) {
        host_world_pose(r, component, attach);
    }
    g_dh.no_target += component != nullptr && !world_row && r.target < 0 ? 1u : 0u;
    g_dh.socket += socket != 0 ? 1u : 0u;
    if (r.mat < 0) {
        log::line("WARN card132: host decal material %s has no table slot: not relayed", object_name(material).text);
        return;
    }
    g_dh.live[g_dh.live_n++] = r;
    ++g_dh.spawned;
    if (placed_target(g_dh.targets, r.target) &&
        note_event_cap(g_dh.placed_lines, kPlacedLines, "card132 placed-target decals")) {
        log::line("card132: host decal %u attached to placed %s %s at (%.0f, %.0f, %.0f)", r.id,
                  g_dh.targets[r.target].comp, g_dh.targets[r.target].where, r.loc.x, r.loc.y, r.loc.z);
    }
    // Card 309: no event row; host_flush sends the D row from the clear sent bit.
}

// Per tick. Host: the tick's rows out (after the fade-end scan). Client: copies gone without a host row.
void c132_decal_step(void* engine) {
    if (g_client_session) {
        void* const world = g_dc.copy_n != 0 && engine != nullptr ? g_get_game_world(engine) : nullptr;
        if (world != nullptr) {
            client_scan(client_manager(world), "tick");
            client_held_step(GetTickCount64());
        }
        return;
    }
    if (g_dh.manager != nullptr) {
        host_scan(g_dh.manager);
    }
    if (g_dh.ev_n == 0 && host_unsent() == 0) {
        return;
    }
    host_flush(engine != nullptr ? g_get_game_world(engine) : nullptr);
}

// Host, at a digest round end: an R row with the live count, in decal row order.
void c132_decal_round(uint32_t round) {
    if (g_client_session) {
        return;
    }
    if (g_dh.manager != nullptr) {
        host_scan(g_dh.manager);
    }
    const int32_t pool = decal_pool_num(decal_pool(g_dh.manager));
    DecalEvent* const e = host_event('R');
    if (e != nullptr) {
        e->round = round;
        // Card 309: only decals whose D row went; this tick's unsent D rows follow the R row in host_flush.
        e->index = g_dh.live_n - host_unsent();
        e->pool = pool;
    }
    ++g_dh.rounds;
    log::line("card132: host decal round %u: live %d, pool active %d of cap %d; E sent %u", round, g_dh.live_n,
              pool, g_dh.cap, g_dh.e_sent);
}

// Host: a joining PC's first Pong: pending rows out first, then J, the tables and every live decal with its age.
void c132_decal_replay(void* pc) {
    if (g_client_session || pc == nullptr) {
        return;
    }
    const void* const world = g_actor_get_world(pc);
    if (g_dh.manager != nullptr) {
        host_scan(g_dh.manager);
    }
    host_flush(world);
    const ULONGLONG now = GetTickCount64();
    DecalOut o{};
    DecalEvent e{};
    e.kind = 'J';
    char head[32];
    snprintf(head, sizeof(head), "J %d;", g_dh.mat_n + g_dh.target_n + g_dh.live_n);
    memcpy(o.frame, head, strlen(head) + 1);
    o.used = strlen(head);
    for (int32_t i = 0; i < g_dh.mat_n; ++i) {
        out_row(o, DecalEvent{'M', i, 0, 0, {}}, now, true, world, pc);
    }
    for (int32_t i = 0; i < g_dh.target_n; ++i) {
        out_row(o, DecalEvent{'T', i, 0, 0, {}}, now, true, world, pc);
    }
    for (int32_t i = 0; i < g_dh.live_n; ++i) {
        host_read_fields(g_dh.live[i]);
        e.kind = 'D';
        e.row = g_dh.live[i];
        out_row(o, e, now, true, world, pc);
    }
    out_send(o, world, pc);
    log::line("card132: host decal replay to a joining PC: %d live decals, %d materials, %d targets, %d bytes in %d "
              "frames, %d sent", g_dh.live_n, g_dh.mat_n, g_dh.target_n, o.bytes, o.frames, o.sent);
}

// Client: ChanOp::Decal, rows split by ';'.
void c132_receive_decal(void* pc, const WireString* s) {
    char text[kChanTextMax + 1];
    if (chan_text(s, text, sizeof(text)) <= 0) {
        ++g_dc.bad;
        return;
    }
    char* row = text;
    for (int32_t i = 0; i < kChanTextMax && *row != '\0'; ++i) {
        char* const end = strchr(row, ';');
        if (end != nullptr) {
            *end = '\0';
        }
        client_row(pc, row);
        row = end != nullptr ? end + 1 : row + strlen(row);
    }
}

namespace {

// Host: the decal totals row. `when` names the point: "map" at the map end, the respawn in place otherwise.
void host_totals(const char* when) {
    log::line("%scard132: host decal %s totals: %u decals relayed, %u FIFO releases, %u other releases, %u "
              "rounds, %u rows dropped, %u not tracked, %u without a target, %u world-space (fixed "
              "geometry), %u with a socket; %u rows, "
              "%u bytes in %u frames, %u sent; %d live, cap %d; card309: %u D rows sent, %u released before "
              "sending (no row), most D rows in one tick %u",
              g_dh.dropped + g_dh.untracked_full != 0 ? "WARN " : "", when, g_dh.spawned, g_dh.evicted_fifo,
              g_dh.released, g_dh.rounds, g_dh.dropped, g_dh.untracked_full, g_dh.no_target,
              g_dh.world_rows, g_dh.socket, g_dh.rows, g_dh.bytes, g_dh.frames, g_dh.sent, g_dh.live_n, g_dh.cap,
              g_dh.d_sent, g_dh.coalesced, g_dh.d_tick_peak);
}

} // namespace

// Card 309, host: the totals so far at an in-place load (RESTART CHECKPOINT, quickload). The load reloads in place
// with no map end, so c132_decal_map_end does not run there; the counts are not reset (the map goes on). b12 item 5:
// called from detour_c121_load on every host in-place load (was the card 045 stale branch only, which a load with
// one player down never reaches).
void c132_decal_respawn_totals() {
    if (!g_client_session) {
        host_totals("respawn in place (this map so far)");
    }
}

// Card 326: the registry's "decal" map end (was the arm).
void c132_decal_map_end() {
    if (!g_client_session) {
        host_totals("map"); // b12 item 5: also with 0 spawned, so every map end has a host row
    }
    if (g_dc.received != 0 || g_dc.suppressed != 0) {
        const uint32_t bad = g_dc.no_decal + g_dc.gone_early + g_dc.nulls + g_dc.no_mat + g_dc.no_target +
                             g_dc.full + g_dc.bad + g_dc.round_warn + g_dc.held_dropped + g_dc.held_failed +
                             g_dc.evicted_lost;
        log::line("%scard132: client decal map totals: %u rows, %u spawned, %u released, %u own decals "
                  "suppressed, %u ignored before the replay, %u duplicates, %u expired on arrival, %u world-position "
                  "fallbacks; %u releases with no decal, %u gone early, %u null, %u without material or "
                  "manager, %u without target, %u past the table, %u bad rows, %u round mismatches of %u; "
                  "%d live; FIFO took %u host copies (E rows after %u, %d pending, %u lost from the full list), "
                  "test cap %d; held %u (applied late %u, dropped %u, released while held %u, failed %u, %d still held)",
                  bad != 0 ? "WARN " : "", g_dc.received, g_dc.applied, g_dc.released,
                  g_dc.suppressed, g_dc.ignored, g_dc.dups, g_dc.expired, g_dc.at_fallback, g_dc.no_decal,
                  g_dc.gone_early, g_dc.nulls, g_dc.no_mat, g_dc.no_target, g_dc.full, g_dc.bad,
                  g_dc.round_warn, g_dc.rounds, g_dc.copy_n, g_dc.copies_evicted, g_dc.evicted_released,
                  g_dc.evicted_n, g_dc.evicted_lost, g_dc.cap_set, g_dc.held, g_dc.late, g_dc.held_dropped,
                  g_dc.held_released, g_dc.held_failed, client_held_count());
    }
    g_dh = DecalHost{};
    g_dc = DecalClient{};
}

int32_t c132_decal_live() {
    return g_dh.live_n;
}

// Card 136: the effects manager leaves every decal to this relay (card 566) (its FIFO eviction, pool order and fade).
bool c132_decal_relayed(const void* material) {
    return decal_relayed(material);
}

// Card 136 step 3, client: the decal manager a host decal row plays through.
void* c132_decal_manager(const void* context) {
    return client_manager(context);
}

} // namespace ubw::hooks
