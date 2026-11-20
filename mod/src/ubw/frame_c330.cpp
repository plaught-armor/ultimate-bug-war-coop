// Card 330 step 1 (diagnostic, both sides, always on): what the mod costs per frame and which step costs it.
//   - Every step call in ubw_post_tick runs inside C330_STEP: one QueryPerformanceCounter pair per call, added to the
//     step's slot (a fixed table indexed by the call site's registered id; no allocation).
//   - c23_collect reports each call (caller return address, actors walked, ticks) through c330_collect.
//   - log::line's time and line count come from ue::log's running totals.
//   - Every received channel frame's handler reports its time through c330_recv (chan_ops.cpp c252_ledger_recv). The
//     handlers run from the engine's RPC receive, outside ubw_post_tick, so this time is not part of "mod total".
//   - Every kC330WindowMs one window row: frames, mod total and its share of the window, longest mod tick, the top
//     kC330Top steps, log time and lines, collect calls, actors and time.
//   - At map end one row per step with time, longest first, and one collect-by-caller row; then the map totals start
//     over.
// Game thread only (ubw_post_tick, c23_collect callers and LoadMap enter all run there).

#include "ubw/hooks_internal.h"

#include <intrin.h>
#include <wchar.h>
#include <windows.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace ubw::hooks {
namespace {

constexpr uint16_t kC330Steps = 128;          // registered step call sites (ubw_post_tick has 58 today)
constexpr ULONGLONG kC330WindowMs = 30000;    // window row period
constexpr int32_t kC330Top = 5;               // steps named in the window row
constexpr int32_t kC330Callers = 48;          // c23_collect call sites told apart (31 today)
constexpr double kC330OverMs = 1.0;           // a step call over this counts in the map row
constexpr double kC330SlowMs = 5.0;           // b224: a step call over this gets its own row
constexpr uint32_t kC330SlowRows = 8;         // b224: slow-call rows per step per session
constexpr size_t kC330RowMax = 1400;          // characters of the collect-by-caller row
constexpr size_t kC330Ops = 96;               // channel ops told apart in the recv part (ChanOp::Count is below it)
constexpr int32_t kC330RecvTop = 3;           // ops named in the window row's recv part

struct C330Step {
    const char* name; // the call text (C330_STEP's #call); printed up to its '('
    int64_t win_ticks;
    int64_t win_max;
    int64_t map_ticks;
    int64_t map_max;
    uint64_t map_calls;
    uint32_t map_over; // calls over kC330OverMs this map
    uint32_t slow_rows; // slow-call rows printed this session (kept across maps)
};

struct C330Caller {
    const void* ret; // return address inside c23_collect's caller
    uint64_t calls;
    int64_t ticks;
};

struct C330Collect {
    uint64_t calls;
    uint64_t actors;
    int64_t ticks;
};

struct C330Recv {
    uint32_t calls;
    int64_t ticks;
    int64_t max;
};

struct C330 {
    C330Recv recv[kC330Ops];   // this window's received frames by op (slot 0: ops past the table)
    C330Step step[kC330Steps];
    uint16_t steps;
    uint16_t dropped;          // registrations past kC330Steps (their calls are not timed)
    int64_t tick_t0;           // QPC at the open tick's start, 0 between ticks
    int64_t win_mod;           // ticks inside ubw_post_tick this window
    int64_t win_max_tick;
    uint32_t win_frames;
    ULONGLONG win_start_ms;
    int64_t log_ticks_seen;    // ue::log totals at the last window row
    int64_t log_lines_seen;
    C330Collect win_collect;
    C330Caller caller[kC330Callers];
    int32_t callers;
    uint64_t caller_dropped;   // collect calls from call sites past kC330Callers (counted in the totals only)
    double freq;               // QPC ticks per ms
    // Card 409 batch: the ProcessEvent hook's own time per frame (time stamp counter ticks; any thread, folded at
    // ubw_post_tick end). The map's TSC and QPC at its start convert ticks to time at map end.
    uint64_t pe_frame;
    uint64_t pe_frame_calls;
    uint64_t pe_map;
    uint64_t pe_map_max;
    uint64_t pe_map_calls;
    uint32_t pe_frames;
    uint64_t pe_tsc0;
    int64_t pe_qpc0;
};
C330 g_c330 = {};

int64_t c330_now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

double c330_ms(int64_t ticks) {
    if (g_c330.freq == 0.0) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        g_c330.freq = static_cast<double>(f.QuadPart) / 1000.0;
    }
    return static_cast<double>(ticks) / g_c330.freq;
}

int c330_name_len(const char* name) {
    const char* const paren = strchr(name, '(');
    return static_cast<int>(paren != nullptr ? paren - name : static_cast<ptrdiff_t>(strlen(name)));
}

// The window's top steps by time, into `out` (ids, at most kC330Top); returns how many.
int32_t c330_top(uint16_t* out) {
    int32_t n = 0;
    for (uint16_t i = 0; i < g_c330.steps; ++i) {
        if (g_c330.step[i].win_ticks == 0) {
            continue;
        }
        int32_t at = n < kC330Top ? n++ : kC330Top;
        while (at > 0 && g_c330.step[out[at - 1]].win_ticks < g_c330.step[i].win_ticks) {
            if (at < kC330Top) {
                out[at] = out[at - 1];
            }
            --at;
        }
        if (at < kC330Top) {
            out[at] = i;
        }
    }
    return n;
}

// The window's received frames: total, then the kC330RecvTop ops with the most handler time. Clears the table.
void c330_recv_text(char* out, size_t cap) {
    uint64_t calls = 0;
    int64_t ticks = 0;
    for (size_t i = 0; i < kC330Ops; ++i) {
        calls += g_c330.recv[i].calls;
        ticks += g_c330.recv[i].ticks;
    }
    int at = snprintf(out, cap, "%llu frames %.1f ms", static_cast<unsigned long long>(calls), c330_ms(ticks));
    for (int32_t k = 0; k < kC330RecvTop && at > 0 && static_cast<size_t>(at) < cap; ++k) {
        size_t best = kC330Ops;
        for (size_t i = 0; i < kC330Ops; ++i) {
            if (g_c330.recv[i].ticks > 0 && (best == kC330Ops || g_c330.recv[i].ticks > g_c330.recv[best].ticks)) {
                best = i;
            }
        }
        if (best == kC330Ops) {
            break;
        }
        const C330Recv& r = g_c330.recv[best];
        at += snprintf(out + at, cap - static_cast<size_t>(at), ", %s %u frames %.1f ms max %.2f",
                       best != 0 ? c132_op_name(static_cast<uint8_t>(best)) : "other", r.calls, c330_ms(r.ticks),
                       c330_ms(r.max));
        g_c330.recv[best] = C330Recv{};
    }
    memset(g_c330.recv, 0, sizeof(g_c330.recv));
}

// Card 252 step 2 (UBWCOOP_C132_FX=1 only, never the play build): once per window, the live object count and the
// counts of the component classes that cost render or game time, so a client count that grows with the frame time
// while the host's stays flat names the class. The scan's own time is printed.
constexpr const wchar_t* kC252Classes[] = {
    L"/Script/Niagara.NiagaraComponent",          L"/Script/Engine.ParticleSystemComponent",
    L"/Script/Engine.AudioComponent",             L"/Script/Engine.DecalComponent",
    L"/Script/Engine.SkeletalMeshComponent",      L"/Script/Engine.StaticMeshComponent",
    L"/Script/Engine.ProjectileMovementComponent",
};
constexpr int32_t kC252N = static_cast<int32_t>(sizeof(kC252Classes) / sizeof(kC252Classes[0]));

void c252_census_row(ULONGLONG now) {
    const void* cls[kC252N] = {};
    for (int32_t k = 0; k < kC252N; ++k) {
        cls[k] = find_class(kC252Classes[k]); // script classes: StaticFindObject, a few per 30 s
    }
    int32_t counts[kC252N] = {};
    const int64_t t0 = c330_now();
    const int32_t live = objects_census(cls, kC252N, counts);
    const double scan_ms = c330_ms(c330_now() - t0);
    char text[512];
    int at = 0;
    for (int32_t k = 0; k < kC252N && at >= 0 && static_cast<size_t>(at) < sizeof(text); ++k) {
        const wchar_t* const name = wcsrchr(kC252Classes[k], L'.') + 1;
        at += snprintf(text + at, sizeof(text) - static_cast<size_t>(at), "%s%ls %d%s", k == 0 ? "" : ", ", name,
                       counts[k], cls[k] == nullptr ? " (class not found)" : "");
    }
    log::line("card252: %s t=%llu census: objects %d; %s; scan %.2f ms", side(), static_cast<unsigned long long>(now),
              live, text, scan_ms);
}

// Card 526 step 1 (UBWCOOP_C132_FX=1 only): the census row's NiagaraComponents grouped, so a client count that grows
// while the host's stays flat names its asset and owner. Per side, after each census row: the top assets and owner
// classes by count, and the components by state (bIsActive) and by pool method (4 = FreeInPool, sitting in the
// world's FX pool). Keys are compared by pointer; a key past the table's size is counted as "other".
constexpr size_t kCompActiveByte = 0x96;     // UActorComponent: bIsActive is bit 4 (execIsActive 0x03A60C40)
constexpr uint8_t kCompActiveBit = 0x10;
constexpr size_t kC526Cap = 16384;           // components read per census (the c263L client peak was 7639)
constexpr int32_t kC526Keys = 64;            // distinct assets or owner classes kept per census
constexpr int32_t kC526TopAssets = 10;
constexpr int32_t kC526TopOwners = 6;
constexpr uint8_t kC526PoolMethods = 5;      // ENCPoolMethod: None, AutoRelease, ManualRelease, ManualRelease_OnComplete, FreeInPool
struct C526Key {
    const void* key;
    int32_t n;
};
struct C526Table {
    C526Key row[kC526Keys];
    int32_t used;
    int32_t other;
};
const void* g_c526_objs[kC526Cap];
C526Table g_c526_assets;
C526Table g_c526_owners;

void c526_count(C526Table& t, const void* key) {
    for (int32_t i = 0; i < t.used; ++i) {
        if (t.row[i].key == key) {
            ++t.row[i].n;
            return;
        }
    }
    if (t.used < kC526Keys) {
        t.row[t.used++] = C526Key{key, 1};
        return;
    }
    ++t.other;
}

// The top `top` rows of `t` as "name n, ...", largest first (selection sort in place). Owner rows are keyed by the
// owner's class pointer, so object_name gives the class name there.
int c526_text(C526Table& t, int32_t top, char* out, size_t cap) {
    int at = 0;
    for (int32_t k = 0; k < top && k < t.used && at >= 0 && static_cast<size_t>(at) < cap; ++k) {
        int32_t best = k;
        for (int32_t i = k + 1; i < t.used; ++i) {
            best = t.row[i].n > t.row[best].n ? i : best;
        }
        const C526Key swap = t.row[k];
        t.row[k] = t.row[best];
        t.row[best] = swap;
        at += snprintf(out + at, cap - static_cast<size_t>(at), "%s%s %d", k == 0 ? "" : ", ",
                       t.row[k].key != nullptr ? object_name(t.row[k].key).text : "none", t.row[k].n);
    }
    if (t.other != 0 && at >= 0 && static_cast<size_t>(at) < cap) {
        at += snprintf(out + at, cap - static_cast<size_t>(at), ", other keys %d", t.other);
    }
    return at;
}

void c526_niagara_row() {
    const void* const cls = find_class(L"/Script/Niagara.NiagaraComponent");
    if (cls == nullptr) {
        return;
    }
    const int64_t t0 = c330_now();
    const int32_t total = objects_of_class(cls, g_c526_objs, static_cast<int32_t>(kC526Cap));
    const int32_t n = total < static_cast<int32_t>(kC526Cap) ? total : static_cast<int32_t>(kC526Cap);
    g_c526_assets = C526Table{};
    g_c526_owners = C526Table{};
    int32_t active = 0;
    int32_t pool[kC526PoolMethods + 1] = {}; // the last slot counts an unknown method value
    for (int32_t i = 0; i < n; ++i) {
        const void* const c = g_c526_objs[i];
        c526_count(g_c526_assets, read_ptr(c, kNiagaraAsset));
        const void* const owner = read_ptr(c, kComponentOwner);
        c526_count(g_c526_owners, owner != nullptr ? read_ptr(owner, kObjectClass) : nullptr);
        active += (*(static_cast<const uint8_t*>(c) + kCompActiveByte) & kCompActiveBit) != 0 ? 1 : 0;
        const uint8_t m = *(static_cast<const uint8_t*>(c) + kNiagaraPoolingMethod);
        ++pool[m < kC526PoolMethods ? m : kC526PoolMethods];
    }
    char assets[768];
    char owners[384];
    assets[0] = owners[0] = '\0';
    c526_text(g_c526_assets, kC526TopAssets, assets, sizeof(assets));
    c526_text(g_c526_owners, kC526TopOwners, owners, sizeof(owners));
    log::line("card526: %s niagara %d (%d read): by asset %s; by owner class %s; active %d, inactive %d; pool method "
              "none %d, auto %d, manual %d, manual on complete %d, free in pool %d, unknown %d; scan %.2f ms",
              side(), total, n, assets, owners, active, n - active, pool[0], pool[1], pool[2], pool[3], pool[4],
              pool[5], c330_ms(c330_now() - t0));
}

// Card 527 step 1 (UBWCOOP_C132_FX=1 only): live NPCs per significance level, with the 2D and the settings +0x29 forms
// (offsets from OnSignificanceChanged 0x0519DE50 and GetCurrentSignificanceLevelSettings 0x05178B80, see the card),
// and once per class the level table (each level's tick interval). The level index is the game's own:
// (int)(significance - 1.0), clamped to the last entry; an NPC whose interface flag is clear counts as level 0.
constexpr size_t kNpcSigIface = 0x760;       // ISTSignificanceObjectInterface subobject in the NPC
constexpr size_t kSigHas = 0x8;              // bool: false = always level 0
constexpr size_t kSigLevels = 0x10;          // TArray data: 16 B entries, settings pointer at +0x8
constexpr size_t kSigLevelCount = 0x18;      // int32
constexpr size_t kSigValue = 0x20;           // float significance
constexpr size_t kNpcLevelTick = 0x20;       // FSTNPCSignificanceLevel: tick interval (float)
constexpr size_t kNpcLevelFlag29 = 0x29;     // FSTNPCSignificanceLevel: bool read after the 2D test (static mesh form?)
constexpr size_t kNpcSprite = 0xA30;         // IsNPC2D: a UADSpriteAnimatorComponent here = 2D form
constexpr int32_t kC527Cap = 1024;           // NPCs read per census
constexpr int32_t kC527Levels = 8;           // level indexes counted (higher ones go in the last slot)
constexpr int32_t kC527Classes = 16;         // classes whose level table was logged, per process
const void* g_c527_objs[kC527Cap];
const void* g_c527_logged[kC527Classes];
int32_t g_c527_logged_n;

// A reflected property of `obj`'s class or its supers by name, or null.
const uint8_t* c527_field(const void* obj, const char* name) {
    const void* cls = obj != nullptr ? read_ptr(obj, kObjectClass) : nullptr;
    for (int s = 0; cls != nullptr && s < kPropMaxSupers; ++s, cls = read_ptr(cls, kStructSuper)) {
        const void* f = read_ptr(cls, kStructChildProperties);
        for (int i = 0; f != nullptr && i < kPropMaxFields; ++i, f = read_ptr(f, kFieldNext)) {
            const auto* const field = static_cast<const uint8_t*>(f);
            if (strcmp(fname_text(field + kFieldName).text, name) == 0) {
                return field;
            }
        }
    }
    return nullptr;
}

// " name=..." for a struct property (its number members) or a one-byte property of `obj`; " name=?" when missing.
int c527_prop(const void* obj, const char* name, char* out, size_t cap) {
    const uint8_t* const f = c527_field(obj, name);
    const auto* const at = f != nullptr ? static_cast<const uint8_t*>(obj) + read_i32(f, kPropertyOffset) : nullptr;
    if (at == nullptr) {
        return snprintf(out, cap, " %s=?", name);
    }
    if (read_i32(f, kPropertyElementSize) == 1) {
        return snprintf(out, cap, " %s=%u", name, static_cast<unsigned>(at[0]));
    }
    const void* const st = read_ptr(f, kStructPropStruct);
    const int head = snprintf(out, cap, " %s=", name);
    if (st == nullptr || head <= 0 || static_cast<size_t>(head) >= cap) {
        return head;
    }
    return head + c491_struct_text(st, at, out + head, cap - static_cast<size_t>(head));
}

// Card 527 c263L315 next row, once per class with the level table: the NPC's actor tick function, its local role, and
// its CharacterMovement component's tick function, movement mode and network smoothing mode. Decides whether the
// client's 1.3 movement ticks per actor Tick are full simulated-proxy ticks every frame (interval 0) or substeps.
void c527_tick_setup(const void* npc) {
    char text[1024];
    size_t at = 0;
    const char* const actor_props[] = {"PrimaryActorTick", "Role", "RemoteRole"};
    for (const char* p : actor_props) {
        const int n = at + 1 < sizeof(text) ? c527_prop(npc, p, text + at, sizeof(text) - at) : 0;
        at = n > 0 ? std::min(sizeof(text) - 1, at + static_cast<size_t>(n)) : at;
    }
    const uint8_t* const cmc_field = c527_field(npc, "CharacterMovement");
    const void* const cmc =
        cmc_field != nullptr ? read_ptr(static_cast<const uint8_t*>(npc) + read_i32(cmc_field, kPropertyOffset), 0)
                             : nullptr;
    const char* const cmc_props[] = {"PrimaryComponentTick", "MovementMode", "NetworkSmoothingMode"};
    for (int i = 0; cmc != nullptr && i < 3; ++i) {
        const int n = at + 1 < sizeof(text) ? c527_prop(cmc, cmc_props[i], text + at, sizeof(text) - at) : 0;
        at = n > 0 ? std::min(sizeof(text) - 1, at + static_cast<size_t>(n)) : at;
    }
    text[at] = '\0';
    log::line("card527: %s npc tick setup %s:%s; movement component %s", side(), object_name(read_ptr(npc, kObjectClass)).text,
              text, cmc != nullptr ? object_name(read_ptr(cmc, kObjectClass)).text : "none");
}

// Card 527 whole-frame step: every number member of each level's settings, by reflection (FSTNPCSignificanceLevel and
// its super FSTSignificanceLevel: DistanceThreshold, ConeCheckDegrees, TickInterval, TickIntervalRandomisationValue,
// MovementComponentTickInterval, ControllerTickInterval, StateMachineTickInterval, TargetComponentTickInterval,
// MoveToTargetUpdateInterval). These are the game's own LOD tiers, the data a tier change in the perf mod would edit.
// The row's TickInterval must equal the raw +0x20 read above; if not, the settings pointer is not the struct start.
void c527_level_fields(const void* cls, const uint8_t* levels, int32_t count) {
    static const void* const st =
        g_static_find_object != nullptr
            ? g_static_find_object(nullptr, nullptr, L"/Script/Aloft.STNPCSignificanceLevel", false)
            : nullptr;
    if (st == nullptr || levels == nullptr) {
        log::line("card527: %s npc level fields %s: struct %s", side(), object_name(cls).text,
                  st == nullptr ? "not found" : "found, no levels");
        return;
    }
    for (int32_t k = 0; k < count && k < kC527Levels; ++k) {
        const uint8_t* const set = static_cast<const uint8_t*>(read_ptr(levels + 16 * k, 0x8));
        char text[768];
        text[0] = '\0';
        if (set != nullptr) {
            c491_struct_text(st, set, text, sizeof(text));
        }
        log::line("card527: %s npc level fields %s %d: %s", side(), object_name(cls).text, k,
                  set != nullptr ? text : "none");
    }
}

// Once per class: the NPC's level table as "tick interval, flag" per level.
void c527_level_table(const void* npc, const uint8_t* iface, int32_t count) {
    const void* const cls = read_ptr(npc, kObjectClass);
    for (int32_t i = 0; i < g_c527_logged_n; ++i) {
        if (g_c527_logged[i] == cls) {
            return;
        }
    }
    if (g_c527_logged_n >= kC527Classes) {
        return;
    }
    g_c527_logged[g_c527_logged_n++] = cls;
    const uint8_t* const levels = static_cast<const uint8_t*>(read_ptr(iface, kSigLevels));
    char text[384];
    int at = 0;
    for (int32_t k = 0; k < count && k < kC527Levels && levels != nullptr && at >= 0 &&
                        static_cast<size_t>(at) < sizeof(text); ++k) {
        const uint8_t* const set = static_cast<const uint8_t*>(read_ptr(levels + 16 * k, 0x8));
        at += snprintf(text + at, sizeof(text) - static_cast<size_t>(at), "%s%d: %s", k == 0 ? "" : "; ", k,
                       set == nullptr ? "none" : "");
        if (set != nullptr && at >= 0 && static_cast<size_t>(at) < sizeof(text)) {
            float tick = 0.0f;
            memcpy(&tick, set + kNpcLevelTick, sizeof(tick));
            at += snprintf(text + at, sizeof(text) - static_cast<size_t>(at), "tick %.3f s, flag29 %u", tick,
                           static_cast<unsigned>(set[kNpcLevelFlag29]));
        }
    }
    log::line("card527: %s npc levels %s: %d levels; %s", side(), object_name(cls).text, count,
              count > 0 ? text : "none");
    c527_level_fields(cls, levels, count);
    c527_tick_setup(npc);
}

// Card 527 step 1 (UBWCOOP_C132_FX=1 only): NPC spawns through UWorld::SpawnActor (the c75 detour), per class:
// count, total and max ms, and the first call of the class on its own (a class load would show there). Map row.
constexpr int32_t kC527SpawnClasses = 24;
struct C527Spawn {
    const void* cls;
    uint32_t n;
    int64_t ticks, max, first;
};
C527Spawn g_c527_spawn[kC527SpawnClasses];
int32_t g_c527_spawn_n;
uint32_t g_c527_spawn_other; // NPC spawns past the class table

void c527_spawn_add(const void* cls, const void* spawned, int64_t ticks) {
    const void* const npc_cls = g_npc_class != nullptr ? g_npc_class() : nullptr;
    if (spawned == nullptr || npc_cls == nullptr || !is_a(spawned, npc_cls)) {
        return;
    }
    int32_t i = 0;
    while (i < g_c527_spawn_n && g_c527_spawn[i].cls != cls) {
        ++i;
    }
    if (i == g_c527_spawn_n) {
        if (g_c527_spawn_n >= kC527SpawnClasses) {
            ++g_c527_spawn_other;
            return;
        }
        g_c527_spawn[g_c527_spawn_n++] = C527Spawn{cls, 0, 0, 0, ticks};
    }
    C527Spawn& s = g_c527_spawn[i];
    ++s.n;
    s.ticks += ticks;
    s.max = ticks > s.max ? ticks : s.max;
}

void c527_spawn_row(const char* map) {
    for (int32_t i = 0; i < g_c527_spawn_n; ++i) {
        const C527Spawn& s = g_c527_spawn[i];
        log::line("card527: %s map %s spawns %s: %u, %.1f ms total, %.3f ms mean, %.2f ms max, first %.2f ms", side(),
                  map, object_name(s.cls).text, s.n, c330_ms(s.ticks), c330_ms(s.ticks) / s.n, c330_ms(s.max),
                  c330_ms(s.first));
    }
    if (g_c527_spawn_other != 0) {
        log::line("card527: %s map %s spawns past the class table: %u", side(), map, g_c527_spawn_other);
    }
    g_c527_spawn_n = 0;
    g_c527_spawn_other = 0;
}

void c527_census_row() {
    const void* const npc_cls = g_npc_class != nullptr ? g_npc_class() : nullptr;
    const void* const sprite_cls = find_class(L"/Script/ADAnimation.ADSpriteAnimatorComponent");
    if (npc_cls == nullptr) {
        return;
    }
    const int64_t t0 = c330_now();
    const int32_t total = objects_of_class(npc_cls, g_c527_objs, kC527Cap, true);
    const int32_t n = total < kC527Cap ? total : kC527Cap;
    int32_t level[kC527Levels] = {};
    int32_t flat[kC527Levels] = {}; // the 2D form, per level
    int32_t flag29[kC527Levels] = {};
    for (int32_t i = 0; i < n; ++i) {
        const void* const npc = g_c527_objs[i];
        const uint8_t* const iface = static_cast<const uint8_t*>(npc) + kNpcSigIface;
        const int32_t count = read_i32(iface, kSigLevelCount);
        int32_t k = 0;
        if (iface[kSigHas] != 0 && count > 0) {
            float sig = 0.0f;
            memcpy(&sig, iface + kSigValue, sizeof(sig));
            k = static_cast<int32_t>(sig - 1.0f);
            k = k < 0 || k >= count ? count - 1 : k; // the game's own clamp
        }
        const uint8_t* const levels = static_cast<const uint8_t*>(read_ptr(iface, kSigLevels));
        const uint8_t* const set =
            levels != nullptr && count > 0 ? static_cast<const uint8_t*>(read_ptr(levels + 16 * k, 0x8)) : nullptr;
        const int32_t slot = k < kC527Levels ? k : kC527Levels - 1;
        ++level[slot];
        const void* const sprite = read_ptr(npc, kNpcSprite);
        flat[slot] += sprite != nullptr && sprite_cls != nullptr && is_a(sprite, sprite_cls) ? 1 : 0;
        flag29[slot] += set != nullptr && set[kNpcLevelFlag29] != 0 ? 1 : 0;
        if (count >= 0 && count <= 64) {
            c527_level_table(npc, iface, count);
        }
    }
    char text[512];
    int at = 0;
    for (int32_t k = 0; k < kC527Levels && at >= 0 && static_cast<size_t>(at) < sizeof(text); ++k) {
        if (level[k] != 0) {
            at += snprintf(text + at, sizeof(text) - static_cast<size_t>(at), "%slevel %d: %d (2D %d, flag29 %d)",
                           at == 0 ? "" : ", ", k, level[k], flat[k], flag29[k]);
        }
    }
    log::line("card527: %s npc census %d (%d read): %s; sprite class %s; scan %.2f ms", side(), total, n,
              at > 0 ? text : "none", sprite_cls != nullptr ? "found" : "not found", c330_ms(c330_now() - t0));
}

// Card 527 step 1 item 2 (UBWCOOP_C132_FX=1 only): game thread time of NPC ticks, per kind. The actor Tick of every
// ASTNonPlayableCharacterBase, and the movement, skeletal mesh and sprite TickComponent calls whose owner is one.
// A component tick that runs inside an NPC's actor Tick (the game's inlined ticks) is already in that Tick's time;
// it is counted on its own as "inside" and left out of its kind's total, so the kinds add up without overlap.
constexpr const char* kC527TickName[kC527TickKinds] = {"actor", "movement", "mesh", "sprite"};
struct C527TickSum {
    int64_t ticks[kC527TickKinds];
    uint64_t calls[kC527TickKinds];
    uint64_t inside[kC527TickKinds]; // component calls inside an NPC actor Tick (time not added to the kind)
};
C527TickSum g_c527_win = {};
C527TickSum g_c527_map = {};
bool g_c527_in_actor = false; // game thread: an NPC actor Tick is on the stack

void c527_tick_text(const C527TickSum& s, char* text, size_t size) {
    int at = 0;
    for (int32_t k = 0; k < kC527TickKinds && at >= 0 && static_cast<size_t>(at) < size; ++k) {
        at += snprintf(text + at, size - static_cast<size_t>(at), "%s%s %.1f ms %llu calls (%llu inside)",
                       k == 0 ? "" : ", ", kC527TickName[k], c330_ms(s.ticks[k]),
                       static_cast<unsigned long long>(s.calls[k]), static_cast<unsigned long long>(s.inside[k]));
    }
}

void c527_tick_window_row(ULONGLONG window) {
    char text[384];
    c527_tick_text(g_c527_win, text, sizeof(text));
    int64_t sum = 0;
    for (int32_t k = 0; k < kC527TickKinds; ++k) {
        sum += g_c527_win.ticks[k];
    }
    log::line("card527: %s npc tick window %llu ms: %s; all %.1f ms (%.2f%% of frame time)", side(),
              static_cast<unsigned long long>(window), text, c330_ms(sum),
              window != 0 ? c330_ms(sum) * 100.0 / static_cast<double>(window) : 0.0);
    g_c527_win = C527TickSum{};
}

void c527_tick_map_row(const char* map) {
    char text[384];
    c527_tick_text(g_c527_map, text, sizeof(text));
    log::line("card527: %s map %s npc tick: %s", side(), map, text);
    g_c527_map = C527TickSum{};
}

void c527_tick_add(int32_t kind, int64_t ticks, bool inside) {
    if (inside) {
        ++g_c527_win.inside[kind];
        ++g_c527_map.inside[kind];
        return;
    }
    g_c527_win.ticks[kind] += ticks;
    g_c527_map.ticks[kind] += ticks;
    ++g_c527_win.calls[kind];
    ++g_c527_map.calls[kind];
}

void c330_window_row(ULONGLONG now) {
    uint16_t top[kC330Top] = {};
    const int32_t n = c330_top(top);
    char steps[512];
    int at = 0;
    for (int32_t k = 0; k < n && at >= 0 && static_cast<size_t>(at) < sizeof(steps); ++k) {
        const C330Step& s = g_c330.step[top[k]];
        at += snprintf(steps + at, sizeof(steps) - static_cast<size_t>(at), "%s%.*s %.1f max %.2f", k == 0 ? "" : ", ",
                       c330_name_len(s.name), s.name, c330_ms(s.win_ticks), c330_ms(s.win_max));
    }
    int64_t log_ticks = 0;
    int64_t log_lines = 0;
    log::line_totals(&log_ticks, &log_lines);
    const ULONGLONG window = now - g_c330.win_start_ms;
    const double mod_ms = c330_ms(g_c330.win_mod);
    char recv[256];
    c330_recv_text(recv, sizeof(recv));
    log::line("card330: %s t=%llu window %llu ms: %u frames, mod %.1f ms total (%.2f%% of frame time), max tick %.2f "
              "ms; top %d steps %s; log %.1f ms %lld lines; collect %llu calls %llu actors %.1f ms; untimed steps %u; "
              "recv (outside mod total) %s",
              side(), static_cast<unsigned long long>(now), static_cast<unsigned long long>(window), g_c330.win_frames,
              mod_ms, window != 0 ? mod_ms * 100.0 / static_cast<double>(window) : 0.0,
              c330_ms(g_c330.win_max_tick), n, n != 0 ? steps : "none", c330_ms(log_ticks - g_c330.log_ticks_seen),
              static_cast<long long>(log_lines - g_c330.log_lines_seen),
              static_cast<unsigned long long>(g_c330.win_collect.calls),
              static_cast<unsigned long long>(g_c330.win_collect.actors), c330_ms(g_c330.win_collect.ticks),
              g_c330.dropped, recv);
    g_c330.log_ticks_seen = log_ticks;
    g_c330.log_lines_seen = log_lines;
    for (uint16_t i = 0; i < g_c330.steps; ++i) {
        g_c330.step[i].win_ticks = 0;
        g_c330.step[i].win_max = 0;
    }
    g_c330.win_mod = 0;
    g_c330.win_max_tick = 0;
    g_c330.win_frames = 0;
    g_c330.win_collect = C330Collect{};
    g_c330.win_start_ms = now;
    if (g_c132_fx_log) {
        c252_census_row(now);
        c526_niagara_row();
        c527_census_row();
        c527_tick_window_row(window);
    }
}

// One frame's ProcessEvent hook time into the map totals (from c330_tick_end).
void c330_pe_fold() {
    if (g_c330.pe_tsc0 == 0) {
        g_c330.pe_tsc0 = __rdtsc();
        g_c330.pe_qpc0 = c330_now();
    }
    const uint64_t f = g_c330.pe_frame;
    g_c330.pe_map += f;
    g_c330.pe_map_max = f > g_c330.pe_map_max ? f : g_c330.pe_map_max;
    g_c330.pe_map_calls += g_c330.pe_frame_calls;
    ++g_c330.pe_frames;
    g_c330.pe_frame = 0;
    g_c330.pe_frame_calls = 0;
}

// Card 409 batch (efficiency audit, card 329 row shape): the map's ProcessEvent hook cost per frame, mean and max.
void c330_pe_row(const char* map) {
    const uint64_t tsc = __rdtsc() - g_c330.pe_tsc0;
    const double ms = c330_ms(c330_now() - g_c330.pe_qpc0);
    const double us_per_tsc = g_c330.pe_tsc0 != 0 && tsc != 0 ? ms * 1000.0 / static_cast<double>(tsc) : 0.0;
    const double frames = g_c330.pe_frames != 0 ? static_cast<double>(g_c330.pe_frames) : 1.0;
    const double calls = g_c330.pe_map_calls != 0 ? static_cast<double>(g_c330.pe_map_calls) : 1.0;
    log::line("card329: %s map %s ProcessEvent hook (mod time, the original excluded): %u frames, %.1f us mean per "
              "frame, %.1f us max, %llu calls (%.1f per frame, %.3f us each)", side(), map, g_c330.pe_frames,
              static_cast<double>(g_c330.pe_map) * us_per_tsc / frames,
              static_cast<double>(g_c330.pe_map_max) * us_per_tsc,
              static_cast<unsigned long long>(g_c330.pe_map_calls), static_cast<double>(g_c330.pe_map_calls) / frames,
              static_cast<double>(g_c330.pe_map) * us_per_tsc / calls);
    g_c330.pe_map = g_c330.pe_map_max = g_c330.pe_map_calls = 0;
    g_c330.pe_frames = 0;
    g_c330.pe_tsc0 = __rdtsc();
    g_c330.pe_qpc0 = c330_now();
}

void c330_caller_row(const char* map) {
    char text[kC330RowMax];
    int at = 0;
    const auto* const base = reinterpret_cast<const uint8_t*>(&__ImageBase);
    for (int32_t i = 0; i < g_c330.callers && at >= 0 && static_cast<size_t>(at) < sizeof(text); ++i) {
        const C330Caller& c = g_c330.caller[i];
        at += snprintf(text + at, sizeof(text) - static_cast<size_t>(at), "%smod+0x%llx %llu calls %.1f ms",
                       i == 0 ? "" : ", ",
                       static_cast<unsigned long long>(static_cast<const uint8_t*>(c.ret) - base),
                       static_cast<unsigned long long>(c.calls), c330_ms(c.ticks));
    }
    log::line("card330: %s map %s collect by caller: %s; other call sites %llu calls", side(), map,
              g_c330.callers != 0 ? text : "none", static_cast<unsigned long long>(g_c330.caller_dropped));
}

} // namespace

void c527_spawn(const void* cls, const void* spawned, int64_t ticks) {
    c527_spawn_add(cls, spawned, ticks);
}

C527ActorTickFn g_orig_c527_actor_tick = nullptr;
CmcTickFn g_orig_c527_mesh_tick = nullptr;
CmcTickFn g_orig_c527_sprite_tick = nullptr;

// Hooked only with UBWCOOP_C132_FX=1 (hook table gate), so the flag is set whenever these run.
void detour_c527_actor_tick(void* npc, float delta) {
    const bool outer = !g_c527_in_actor; // a nested NPC Tick (not expected) is timed by the outer one only
    g_c527_in_actor = true;
    const int64_t t0 = c330_now();
    g_orig_c527_actor_tick(npc, delta);
    const int64_t t1 = c330_now();
    if (outer) {
        g_c527_in_actor = false;
        c527_tick_add(kC527TickActor, t1 - t0, false);
    }
}

void c527_component_tick(int32_t kind, CmcTickFn orig, void* comp, float delta, int32_t tick_type,
                         void* tick_function) {
    const void* const npc_cls = g_npc_class != nullptr ? g_npc_class() : nullptr;
    const void* const owner = read_ptr(comp, kComponentOwner);
    if (!g_c132_fx_log || npc_cls == nullptr || owner == nullptr || !is_a(owner, npc_cls)) {
        orig(comp, delta, tick_type, tick_function);
        return;
    }
    const int64_t t0 = c330_now();
    orig(comp, delta, tick_type, tick_function);
    c527_tick_add(kind, c330_now() - t0, g_c527_in_actor);
}

void detour_c527_mesh_tick(void* comp, float delta, int32_t tick_type, void* tick_function) {
    c527_component_tick(kC527TickMesh, g_orig_c527_mesh_tick, comp, delta, tick_type, tick_function);
}

void detour_c527_sprite_tick(void* comp, float delta, int32_t tick_type, void* tick_function) {
    c527_component_tick(kC527TickSprite, g_orig_c527_sprite_tick, comp, delta, tick_type, tick_function);
}

uint16_t c330_register(const char* name) {
    if (g_c330.steps >= kC330Steps) {
        ++g_c330.dropped;
        log::line("WARN card330: %s step %s not timed: table of %u full", side(), name, kC330Steps);
        return kC330Steps;
    }
    g_c330.step[g_c330.steps].name = name;
    return g_c330.steps++;
}

C330Span::C330Span(uint16_t step) : id(step), t0(step < kC330Steps ? c330_now() : 0) {}

C330Span::~C330Span() {
    if (t0 == 0) {
        return;
    }
    const int64_t d = c330_now() - t0;
    C330Step& s = g_c330.step[id];
    s.win_ticks += d;
    s.win_max = d > s.win_max ? d : s.win_max;
    s.map_ticks += d;
    s.map_max = d > s.map_max ? d : s.map_max;
    ++s.map_calls;
    const double ms = c330_ms(d);
    s.map_over += ms > kC330OverMs ? 1u : 0u;
    if (ms > kC330SlowMs && s.slow_rows < kC330SlowRows) {
        ++s.slow_rows;
        log::line("card330: %s t=%llu step %.*s took %.2f ms", side(), static_cast<unsigned long long>(GetTickCount64()),
                  c330_name_len(s.name), s.name, ms);
    }
}

void c330_tick_begin() {
    g_c330.tick_t0 = c330_now();
    if (g_c330.win_start_ms == 0) {
        g_c330.win_start_ms = GetTickCount64();
    }
}

void c330_tick_end() {
    if (g_c330.tick_t0 == 0) {
        return;
    }
    const int64_t d = c330_now() - g_c330.tick_t0;
    g_c330.tick_t0 = 0;
    c330_pe_fold();
    g_c330.win_mod += d;
    g_c330.win_max_tick = d > g_c330.win_max_tick ? d : g_c330.win_max_tick;
    ++g_c330.win_frames;
    const ULONGLONG now = GetTickCount64();
    if (now - g_c330.win_start_ms >= kC330WindowMs) {
        c330_window_row(now);
    }
}

void c330_pe(uint64_t tsc) {
    g_c330.pe_frame += tsc;
    ++g_c330.pe_frame_calls;
}

void c330_recv(wchar_t op, int64_t ticks) {
    C330Recv& r = g_c330.recv[static_cast<size_t>(op) < kC330Ops ? static_cast<size_t>(op) : 0];
    ++r.calls;
    r.ticks += ticks;
    r.max = ticks > r.max ? ticks : r.max;
}

void c330_collect(const void* ret, uint64_t actors, int64_t ticks) {
    ++g_c330.win_collect.calls;
    g_c330.win_collect.actors += actors;
    g_c330.win_collect.ticks += ticks;
    int32_t i = 0;
    while (i < g_c330.callers && g_c330.caller[i].ret != ret) {
        ++i;
    }
    if (i == g_c330.callers && g_c330.callers < kC330Callers) {
        g_c330.caller[g_c330.callers++] = C330Caller{ret, 0, 0};
    }
    if (i == kC330Callers) {
        ++g_c330.caller_dropped;
        return;
    }
    ++g_c330.caller[i].calls;
    g_c330.caller[i].ticks += ticks;
}

// The map's step rows, longest total first (one row per step with time), the collect-by-caller row; the map totals
// start over.
void c330_map_end(const void* old_world) {
    char map[128];
    snprintf(map, sizeof(map), "%s", old_world != nullptr ? object_name(old_world).text : "none");
    bool shown[kC330Steps] = {};
    for (uint16_t row = 0; row < g_c330.steps; ++row) {
        uint16_t best = kC330Steps;
        for (uint16_t i = 0; i < g_c330.steps; ++i) {
            const bool better = best == kC330Steps || g_c330.step[i].map_ticks > g_c330.step[best].map_ticks;
            best = !shown[i] && g_c330.step[i].map_ticks != 0 && better ? i : best;
        }
        if (best == kC330Steps) {
            break;
        }
        shown[best] = true;
        const C330Step& s = g_c330.step[best];
        log::line("card330: %s map %s step %.*s: %.1f ms total, %.1f us mean, %.2f ms max, %u calls over 1 ms", side(),
                  map, c330_name_len(s.name), s.name, c330_ms(s.map_ticks),
                  c330_ms(s.map_ticks) * 1000.0 / static_cast<double>(s.map_calls), c330_ms(s.map_max), s.map_over);
    }
    c330_caller_row(map);
    c330_pe_row(map);
    c527_spawn_row(map);
    if (g_c132_fx_log) {
        c527_tick_map_row(map);
    }
    for (uint16_t i = 0; i < g_c330.steps; ++i) {
        C330Step& s = g_c330.step[i];
        s = C330Step{s.name, s.win_ticks, s.win_max, 0, 0, 0, 0, s.slow_rows};
    }
    g_c330.callers = 0;
    g_c330.caller_dropped = 0;
}

} // namespace ubw::hooks
