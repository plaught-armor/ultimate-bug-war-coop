// Card 408: the flak and plasma bug buildings (BP_SatchelBuilding_Flakbug_C and every child; _Plasmabug overrides only
// Fire Projectile) are placed and not replicated, so each side ran the whole attack state machine on its own: two
// random timer lengths and one RandomBool per turn made the client's building face another way and fire at another
// moment than the host's.
// The host drives the two timer events. Client: the timer-fired ProcessEvent of "Play Turn In Place" and "Timeout Fire
// Burst" on these buildings is refused (c408_pe), and the host's 'Q' world row calls the event instead. A turn row
// first sets the actor yaw to the host's and writes CurRightTurns to the bound that forces the host's direction
// (SelectRightOrLeftTurn: at or above +N forces left, at or below -N forces right, N = MaxdegreesfromOrigin / 45), then
// writes the host's CurRightTurns after the call. The rest of the chain (montages, windup, burst, Fire Projectile)
// runs on each side from the same facing and the same start.
// The host's TurnInPlace 'L' / 'B' rows of these buildings are not sent (world_c98.cpp c98_timeline_row asks
// c408_building), and their attack effects stay local on both sides (fx_c136 c136_attack_local asks c408_attack).

#include "ubw/hooks_internal.h"

#include <math.h>
#include <string.h>

namespace ubw::hooks {

bool g_c408_pe = false; // a building of the class is on the map: detour_process_event calls c408_pe / c408_after_pe

namespace {

constexpr wchar_t kC408BuildingClass[] =
    L"/Game/Blueprints/EnvironmentElements/Buildings/ExplosiveChargeDestructibles/BP_SatchelBuilding_Flakbug."
    L"BP_SatchelBuilding_Flakbug_C";
constexpr wchar_t kC408ProjectileClass[] =
    L"/Game/Blueprints/EnvironmentElements/Buildings/ExplosiveChargeDestructibles/BP_Projectile_Plasmabug."
    L"BP_Projectile_Plasmabug_C";

enum C408Event : int32_t { kC408Turn = 0, kC408Timeout = 1, kC408Events = 2, kC408Burst = 3 };
constexpr const wchar_t* kC408Fn[kC408Events] = {L"Play Turn In Place", L"Timeout Fire Burst"};
constexpr const char* kC408EventText[kC408Events] = {"turn", "timeout"};

// DoATurn is not a member: it is SelectRightOrLeftTurn's out parameter, held in the ubergraph frame. The function sets
// it true only on the branch that moves CurRightTurns by one (bytecode offsets 329-398, 622-691) and false only when
// the max degrees are 0 (offset 48, CurRightTurns untouched), so a turn is "CurRightTurns changed across the event".
enum C408Prop : int32_t { kC408TurningRight = 0, kC408CurRightTurns, kC408MaxDegrees, kC408Props };
constexpr const char* kC408PropName[kC408Props] = {"TurningRight", "CurRightTurns",
                                                   "MaxdegreesfromOrigin (Rounds Up To Nearest 45)"};

constexpr ULONGLONG kC408ScanMs = 2000;   // class and actor scan while unarmed or armed (both sides)
constexpr int32_t kC408Classes = 4;       // building classes named in the class list, per map
constexpr uint32_t kC408Lines = 200;      // turn and timeout lines per side per map
constexpr uint32_t kC408PeLines = 4;      // client refusal lines per map
constexpr size_t kC408RelativeRotation = 0x158; // USceneComponent::RelativeRotation (remote_body.cpp, card 034a)
constexpr int32_t kC408DegreesPerTurn = 45;      // SelectRightOrLeftTurn: N = MaxdegreesfromOrigin / 45 (integer)
// Card 408 F5: the turn's timeline (Begin Turning Rootmotion plays it from the start; its update sets the yaw). A turn
// row handled late (c409b116a: FED_C3M1 Flakbug_C_1 273 ms, a client game thread stall of about 900 ms while the level
// end widget was built) left the client's turn that far behind the host's: yaw diff 1.09 at the timeout, and the
// timeout's yaw set is overwritten by the still-playing timeline's next update. The turn row moves the timeline on by
// the frame's age (at most kC408MaxAgeMs; older is applied as sent).
constexpr char kC408Timeline[] = "TurnInPlace";
constexpr int32_t kC408MaxAgeMs = 2000;

// Row value: bits 0-1 event, bit 2 DoATurn, bit 3 TurningRight, bits 8-15 CurRightTurns + 128, bits 16-31 the actor
// yaw in hundredths of a degree, 0 to 35999 (before the turn event's call; at the timeout event's call).
constexpr uint32_t kC408EventMask = 0x3u;
constexpr uint32_t kC408BitDoATurn = 0x4u;
constexpr uint32_t kC408BitRight = 0x8u;
constexpr int32_t kC408TurnsBias = 128;
constexpr double kC408YawScale = 100.0;

// Card 419: a plasma round (BP_Projectile_Plasmabug_C) bursts at its ubergraph entry 15, the resume point of the
// Delay(RandomFloatInRange(4.0, 5.5)) its ReceiveBeginPlay starts, so each side burst its own round at another time and
// place. The host's entry 15 sends a 'Q' row (event 3, `at` the burst location, named after the round's building). The
// client refuses its own entry 15 of a tracked round; the row moves its oldest live round of that building to the
// host's location and runs entry 15. A tracked round with no row after kC419FallbackMs runs its own (WARN).
constexpr int32_t kC419Entry = 15;
constexpr uint32_t kC419Rounds = 32;          // tracked rounds per side (a full table leaves new rounds untracked)
constexpr ULONGLONG kC419FallbackMs = 7000;
constexpr uint32_t kC419Lines = 40;           // burst lines per side per map
constexpr wchar_t kC419Uber[] = L"ExecuteUbergraph_BP_Projectile_Plasmabug";

struct C419Round {
    C31Weak round;
    C31Weak building; // the nearest building of the class at the round's ReceiveBeginPlay (it leaves the muzzle)
    ULONGLONG born;
};

struct C408Num {
    int32_t at;      // property offset, -1 absent
    uint8_t kind;    // 'i' IntProperty, 'b' ByteProperty, 'f' FloatProperty, 'd' DoubleProperty, 'B' BoolProperty
    uint8_t byte;    // bool: byte offset
    uint8_t mask;    // bool: field mask
};

struct C408Class {
    const void* cls;
    int32_t actors;
    bool own_fn; // FindFunction on this class gave other functions than the base class's (an override)
};

struct C408State {
    void* cls;          // BP_SatchelBuilding_Flakbug_C, found again each map
    void* projectile;   // BP_Projectile_Plasmabug_C, null when not loaded
    void* fn[kC408Events];
    C408Num prop[kC408Props];
    C408Class seen[kC408Classes];
    int32_t seen_n;
    ULONGLONG scan_due;
    bool resolved;      // fn and props looked up for this map's class
    bool calling;       // client: the mod's own call of an event
    const void* pre_actor[kC408Events]; // host: the actor whose event is running
    double pre_yaw[kC408Events];
    int32_t pre_cur[kC408Events];       // host: CurRightTurns before the call (a change is a turn)
    uint32_t lines, pe_lines, refused, host_rows, host_unsent, client_rows, client_missed, dir_mismatch;
    double yaw_diff_max; // client: the largest own-versus-host yaw difference at a timeout row
    double after_max;    // client: the largest one after the host yaw set (F1: 0 when the set took)
    uint32_t aged;       // client: turn rows whose timeline was moved on by the row's age (F5)
    int32_t age_max;     // client: the oldest turn row handled, ms (0: none measured)
    void* uber;          // card 419: the round class's ExecuteUbergraph (resolved at the first round's BeginPlay)
    C419Round rounds[kC419Rounds];
    uint32_t round_n, untracked, burst_rows, burst_unsent, burst_applied, burst_missed, fallback, burst_lines;
    double burst_diff_max; // client: the largest burst-versus-row distance after the move
    int32_t burst_age_max; // client: the oldest burst row handled, ms
};

C408State g_c408{};

C408Num c408_num(const void* cls, const char* name) {
    C408Num n{-1, 0, 0, 0};
    const uint8_t* const f = prop_find_prop(cls, name);
    if (f == nullptr) {
        return n;
    }
    n.at = read_i32(f, kPropertyOffset);
    n.kind = prop_kind_is(f, "IntProperty")      ? 'i'
             : prop_kind_is(f, "ByteProperty")   ? 'b'
             : prop_kind_is(f, "FloatProperty")  ? 'f'
             : prop_kind_is(f, "DoubleProperty") ? 'd'
             : prop_kind_is(f, "BoolProperty") && prop_bool_ok(f) ? 'B'
                                                                   : 0;
    n.byte = n.kind == 'B' ? f[kBoolByteOffset] : 0;
    n.mask = n.kind == 'B' ? f[kBoolFieldMask] : 0;
    if (n.kind == 0) {
        n.at = -1;
    }
    return n;
}

double c408_read(const void* actor, const C408Num& n) {
    const uint8_t* const p = static_cast<const uint8_t*>(actor);
    if (n.at < 0) {
        return 0.0;
    }
    switch (n.kind) {
    case 'i':
        return read_i32(p, static_cast<size_t>(n.at));
    case 'b':
        return p[n.at];
    case 'f':
        return read_f32(p, static_cast<size_t>(n.at));
    case 'd': {
        double v = 0.0;
        memcpy(&v, p + n.at, sizeof(v));
        return v;
    }
    case 'B':
        return (p[n.at + n.byte] & n.mask) != 0 ? 1.0 : 0.0;
    default:
        return 0.0;
    }
}

// Writes an integer value to an IntProperty or ByteProperty (CurRightTurns); false for any other kind.
bool c408_write_int(void* actor, const C408Num& n, int32_t v) {
    uint8_t* const p = static_cast<uint8_t*>(actor);
    if (n.at < 0 || (n.kind != 'i' && n.kind != 'b')) {
        return false;
    }
    if (n.kind == 'i') {
        memcpy(p + n.at, &v, sizeof(v));
    } else {
        p[n.at] = static_cast<uint8_t>(v);
    }
    return true;
}

double c408_yaw(const void* actor) {
    const void* const root = read_ptr(actor, kActorRootComponent);
    return root != nullptr ? read_vec3(root, kC408RelativeRotation).y : 0.0;
}

double c408_wrap(double yaw) {
    double w = fmod(yaw, 360.0);
    w = w < 0.0 ? w + 360.0 : w;
    return w > 180.0 ? w - 360.0 : w;
}

int32_t c408_event(const void* function) {
    return function == g_c408.fn[kC408Turn] ? kC408Turn : function == g_c408.fn[kC408Timeout] ? kC408Timeout : -1;
}

// The base class's two events and the four properties, once per map. False: the class misses one (logged WARN).
bool c408_resolve() {
    const void* const cdo = read_ptr(g_c408.cls, kClassDefaultObject);
    static uint64_t fname[kC408Events] = {};
    bool ok = cdo != nullptr;
    for (int32_t e = 0; e < kC408Events && ok; ++e) {
        g_c408.fn[e] = fname_cached(fname[e], kC408Fn[e]) != 0 ? g_find_function(cdo, fname[e]) : nullptr;
        ok = g_c408.fn[e] != nullptr;
    }
    for (int32_t p = 0; p < kC408Props; ++p) {
        g_c408.prop[p] = c408_num(g_c408.cls, kC408PropName[p]);
        ok = ok && g_c408.prop[p].at >= 0;
    }
    ok = ok && (g_c408.prop[kC408CurRightTurns].kind == 'i' || g_c408.prop[kC408CurRightTurns].kind == 'b');
    log::line("%scard408: %s building class %s: events %d %d, props TurningRight %c CurRightTurns %c "
              "MaxdegreesfromOrigin %c%s",
              ok ? "" : "WARN ", side(), object_name(g_c408.cls).text, g_c408.fn[kC408Turn] != nullptr,
              g_c408.fn[kC408Timeout] != nullptr, g_c408.prop[kC408TurningRight].kind ? g_c408.prop[kC408TurningRight].kind : '-',
              g_c408.prop[kC408CurRightTurns].kind ? g_c408.prop[kC408CurRightTurns].kind : '-',
              g_c408.prop[kC408MaxDegrees].kind ? g_c408.prop[kC408MaxDegrees].kind : '-',
              ok ? "" : ": not armed (one is missing)");
    return ok;
}

// The class list: each class of the map's buildings, once per map, and whether it overrides one of the two events.
void c408_note_class(const void* actor) {
    const void* const cls = read_ptr(actor, kObjectClass);
    for (int32_t i = 0; i < g_c408.seen_n; ++i) {
        if (g_c408.seen[i].cls == cls) {
            ++g_c408.seen[i].actors;
            return;
        }
    }
    if (g_c408.seen_n >= kC408Classes) {
        return;
    }
    static uint64_t fname[kC408Events] = {};
    bool own = false;
    for (int32_t e = 0; e < kC408Events; ++e) {
        own = own || (fname_cached(fname[e], kC408Fn[e]) != 0 && g_find_function(actor, fname[e]) != g_c408.fn[e]);
    }
    g_c408.seen[g_c408.seen_n++] = C408Class{cls, 1, own};
    log::line("%scard408: %s class list: %s (is a BP_SatchelBuilding_Flakbug_C)%s", own ? "WARN " : "", side(),
              object_name(cls).text, own ? ": overrides a timer event, its calls are not filtered" : "");
}

void c408_scan(const void* world) {
    g_c408.cls = g_c408.cls != nullptr ? g_c408.cls : find_class(kC408BuildingClass);
    if (g_c408.cls == nullptr) {
        return; // not loaded: this map has no such building yet
    }
    if (!g_c408.resolved) {
        g_c408.resolved = true;
        g_c408.projectile = find_class(kC408ProjectileClass);
        if (!c408_resolve()) {
            g_c408.fn[kC408Turn] = nullptr;
            g_c408.fn[kC408Timeout] = nullptr;
        }
    }
    if (g_c408.projectile == nullptr) {
        g_c408.projectile = find_class(kC408ProjectileClass); // loads with the first plasma building's shot
    }
    if (g_c408.fn[kC408Turn] == nullptr || g_c408_pe) {
        return; // armed stays armed for the map (the buildings stay placed after a kill)
    }
    static C23List list;
    c23_collect(world, nullptr, g_c408.cls, list);
    for (uint32_t i = 0; i < list.count; ++i) {
        c408_note_class(list.e[i].actor);
    }
    g_c408_pe = list.count > 0;
    if (g_c408_pe) {
        log::line("card408: %s t=%llu armed: %u buildings, projectile class %s", side(),
                  static_cast<unsigned long long>(GetTickCount64()), list.count,
                  g_c408.projectile != nullptr ? "loaded" : "not loaded yet");
    }
}

// Host: one 'Q' row after the event returned.
void c408_host_after(const void* actor, int32_t ev) {
    const double yaw = c408_wrap(g_c408.pre_yaw[ev]);
    const bool right = c408_read(actor, g_c408.prop[kC408TurningRight]) != 0.0;
    const int32_t cur = static_cast<int32_t>(c408_read(actor, g_c408.prop[kC408CurRightTurns]));
    const bool turn = ev == kC408Turn && cur != g_c408.pre_cur[ev];
    const double yaw_pos = yaw < 0.0 ? yaw + 360.0 : yaw;
    const uint32_t yaw_bits = static_cast<uint32_t>(lround(yaw_pos * kC408YawScale)) % 36000u;
    const uint32_t value = static_cast<uint32_t>(ev) | (turn ? kC408BitDoATurn : 0u) | (right ? kC408BitRight : 0u) |
                           (static_cast<uint32_t>((cur + kC408TurnsBias) & 0xFF) << 8) | (yaw_bits << 16);
    const bool sent = c408_host_row(actor, value);
    ++(sent ? g_c408.host_rows : g_c408.host_unsent);
    if (note_event_cap(g_c408.lines, kC408Lines, "card408 attack lines")) {
        char name[kNameChars] = "?";
        c50_name(actor, name, sizeof(name));
        log::line("%scard408: host t=%llu %s %s yaw %.2f DoATurn %d dir %c CurRightTurns %d (Max %.0f)%s",
                  sent ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()), kC408EventText[ev], name,
                  yaw, turn ? 1 : 0, right ? 'R' : 'L', cur, c408_read(actor, g_c408.prop[kC408MaxDegrees]),
                  sent ? "" : ": not relayed (not a placed, unreplicated actor)");
    }
}

// Client: calls the event with the filter let through. False when the parms are larger than an empty event's.
bool c408_call(void* actor, int32_t ev) {
    void* const fn = g_c408.fn[ev];
    const uint16_t size = *reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(fn) + kFunctionParmsSize);
    alignas(16) uint8_t parms[16] = {};
    if (size > sizeof(parms)) {
        return false;
    }
    g_c408.calling = true;
    g_orig_process_event(actor, fn, parms);
    g_c408.calling = false;
    return true;
}

// Card 419: a round's ReceiveBeginPlay (both sides) enters it in the table with its building.
void c419_begin(void* round) {
    if (g_c408.projectile == nullptr && strcmp(class_name(round).text, "BP_Projectile_Plasmabug_C") == 0) {
        g_c408.projectile = find_class(kC408ProjectileClass); // the first round may begin before the 2 s scan finds it
    }
    if (g_c408.projectile == nullptr || !is_a(round, g_c408.projectile)) {
        return;
    }
    static uint64_t uber_name = 0;
    g_c408.uber = g_c408.uber != nullptr || fname_cached(uber_name, kC419Uber) == 0 ? g_c408.uber
                                                                                     : g_find_function(round, uber_name);
    static C23List list;
    c23_collect(g_actor_get_world(round), nullptr, g_c408.cls, list);
    const Vec3 at = actor_location(round);
    void* building = nullptr;
    double best = 0.0;
    for (uint32_t i = 0; i < list.count; ++i) {
        const double d = vec_dist(actor_location(list.e[i].actor), at);
        building = building == nullptr || d < best ? list.e[i].actor : building;
        best = building == list.e[i].actor ? d : best;
    }
    if (building == nullptr || g_c408.round_n >= kC419Rounds || g_c408.uber == nullptr) {
        ++g_c408.untracked;
        return;
    }
    C419Round& r = g_c408.rounds[g_c408.round_n++];
    c31_weak_set(r.round, round);
    c31_weak_set(r.building, building);
    r.born = GetTickCount64();
}

void c419_drop(uint32_t i) {
    g_c408.rounds[i] = g_c408.rounds[--g_c408.round_n]; // order is kept by `born`, not by slot
}

int32_t c419_find(const void* round) {
    for (uint32_t i = 0; i < g_c408.round_n; ++i) {
        if (c31_weak_get(g_c408.rounds[i].round) == round) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

void c419_burst(void* round) {
    alignas(16) int32_t parms[4] = {kC419Entry, 0, 0, 0};
    g_c408.calling = true;
    g_orig_process_event(round, g_c408.uber, parms);
    g_c408.calling = false;
}

// Card 419: the round's ubergraph call. Host: entry 15 of a tracked round sends the burst row. Client: true refuses
// its own entry 15 of a tracked round (the host's row runs it).
bool c419_pe(void* round, const void* parms) {
    const int32_t entry = parms != nullptr ? *static_cast<const int32_t*>(parms) : -1;
    const int32_t i = entry == kC419Entry && !g_c408.calling ? c419_find(round) : -1;
    if (i < 0) {
        return false; // another entry, the mod's own call, or an untracked round (it bursts on its own)
    }
    if (g_client_session) {
        return true;
    }
    void* const building = c31_weak_get(g_c408.rounds[i].building);
    const ULONGLONG life = GetTickCount64() - g_c408.rounds[i].born;
    c419_drop(static_cast<uint32_t>(i));
    const Vec3 at = actor_location(round);
    const bool sent = building != nullptr && c408_host_row(building, kC408Burst, &at);
    ++(sent ? g_c408.burst_rows : g_c408.burst_unsent);
    if (!sent || note_event_cap(g_c408.burst_lines, kC419Lines, "card419 burst lines")) {
        log::line("%scard419: host t=%llu burst %s of %s at (%.0f, %.0f, %.0f), life %llu ms%s", sent ? "" : "WARN ",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(round).text,
                  building != nullptr ? object_name(building).text : "gone", at.x, at.y, at.z,
                  static_cast<unsigned long long>(life), sent ? "" : ": not relayed");
    }
    return false;
}

// Card 419, client: the host's burst row on `building`: its oldest live tracked round moves to `at` and bursts.
const char* c419_client_burst(void* pc, const void* building, const Vec3& at) {
    int32_t oldest = -1;
    for (uint32_t i = 0; i < g_c408.round_n; ++i) {
        const bool mine = c31_weak_get(g_c408.rounds[i].round) != nullptr &&
                          c31_weak_get(g_c408.rounds[i].building) == building;
        oldest = mine && (oldest < 0 || g_c408.rounds[i].born < g_c408.rounds[oldest].born) ? static_cast<int32_t>(i)
                                                                                            : oldest;
    }
    if (oldest < 0) {
        ++g_c408.burst_missed;
        log::line("WARN card419: client t=%llu burst row on %s: no live own round of it",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(building).text);
        return "no live own round of that building";
    }
    void* const round = c31_weak_get(g_c408.rounds[oldest].round);
    const ULONGLONG life = GetTickCount64() - g_c408.rounds[oldest].born;
    c419_drop(static_cast<uint32_t>(oldest));
    const double moved = vec_dist(actor_location(round), at);
    g_set_actor_location(round, &at, false, nullptr, kTeleportPhysics);
    const double diff = vec_dist(actor_location(round), at);
    const int32_t age = c132_ledger_frame_age_ms(pc, kC408MaxAgeMs);
    c419_burst(round);
    ++g_c408.burst_applied;
    g_c408.burst_diff_max = diff > g_c408.burst_diff_max ? diff : g_c408.burst_diff_max;
    g_c408.burst_age_max = age > g_c408.burst_age_max ? age : g_c408.burst_age_max;
    if (note_event_cap(g_c408.burst_lines, kC419Lines, "card419 burst lines")) {
        log::line("card419: client t=%llu burst %s of %s at (%.0f, %.0f, %.0f): moved %.0f uu, burst diff %.1f uu, row "
                  "age %d ms, own life %llu ms",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(round).text,
                  object_name(building).text, at.x, at.y, at.z, moved, diff, age,
                  static_cast<unsigned long long>(life));
    }
    return nullptr;
}

// Card 419: dead rounds leave the table; client: a round with no host row after kC419FallbackMs bursts on its own.
void c419_step(ULONGLONG now) {
    for (uint32_t i = g_c408.round_n; i-- > 0;) {
        void* const round = c31_weak_get(g_c408.rounds[i].round);
        const bool late = round != nullptr && g_client_session && now - g_c408.rounds[i].born >= kC419FallbackMs;
        if (late) {
            ++g_c408.fallback;
            log::line("WARN card419: client t=%llu round %s: no host burst row after %llu ms, its own burst runs",
                      static_cast<unsigned long long>(now), object_name(round).text,
                      static_cast<unsigned long long>(now - g_c408.rounds[i].born));
        }
        if (round == nullptr || late) {
            c419_drop(i);
        }
        if (late) {
            c419_burst(round);
        }
    }
}

} // namespace

// Hot path (g_c408_pe). Client: true refuses the timer-fired event (the host's row calls it). Host: notes the actor
// and its yaw before the call for c408_after_pe.
bool c408_pe(void* object, const void* function, void* parms) {
    static uint64_t begin_play = 0;
    if (function == g_c408.uber && g_c408.uber != nullptr) {
        return c419_pe(object, parms); // card 419
    }
    const uint64_t begin_name = fname_cached(begin_play, L"ReceiveBeginPlay");
    if (begin_name != 0 && read_u64(function, kObjectName) == begin_name) {
        c419_begin(object); // card 419
        return false;
    }
    const int32_t ev = c408_event(function);
    if (ev < 0) {
        return false;
    }
    if (!g_client_session) {
        g_c408.pre_actor[ev] = object;
        g_c408.pre_yaw[ev] = c408_yaw(object);
        g_c408.pre_cur[ev] = static_cast<int32_t>(c408_read(object, g_c408.prop[kC408CurRightTurns]));
        return false;
    }
    if (g_c408.calling) {
        return false;
    }
    ++g_c408.refused;
    if (g_c408.pe_lines < kC408PeLines) {
        ++g_c408.pe_lines;
        char name[kNameChars] = "?";
        c50_name(object, name, sizeof(name));
        log::line("card408: client t=%llu own %s %s refused (the host's row calls it)",
                  static_cast<unsigned long long>(GetTickCount64()), kC408EventText[ev], name);
    }
    return true;
}

void c408_after_pe(const void* object, const void* function) {
    const int32_t ev = g_client_session ? -1 : c408_event(function);
    if (ev < 0 || g_c408.pre_actor[ev] != object) {
        return;
    }
    g_c408.pre_actor[ev] = nullptr;
    c408_host_after(object, ev);
}

// Client: the host's 'Q' row on our same-named placed copy. Null when applied, else why not.
const char* c408_client_row(void* pc, const char* name, const Vec3& at, uint32_t value) {
    void* const actor = c95_find_nearest(g_actor_get_world(pc), name, at);
    const int32_t ev = static_cast<int32_t>(value & kC408EventMask);
    if (ev == kC408Burst && actor != nullptr && g_c408.cls != nullptr && is_a(actor, g_c408.cls)) {
        return c419_client_burst(pc, actor, at); // card 419
    }
    if (actor == nullptr || g_c408.cls == nullptr || !is_a(actor, g_c408.cls) || ev >= kC408Events ||
        g_c408.fn[kC408Turn] == nullptr) {
        ++g_c408.client_missed;
        return actor == nullptr ? "no placed copy of that name" : "not an armed flak or plasma bug building";
    }
    const bool host_turn = (value & kC408BitDoATurn) != 0;
    const bool host_right = (value & kC408BitRight) != 0;
    const int32_t host_cur = static_cast<int32_t>((value >> 8) & 0xFF) - kC408TurnsBias;
    const double host_yaw = c408_wrap(static_cast<double>(value >> 16) / kC408YawScale);
    const double own_yaw = c408_wrap(c408_yaw(actor));
    const double diff = fabs(c408_wrap(own_yaw - host_yaw));
    void* const root = read_ptr(actor, kActorRootComponent);
    int32_t forced = 0;
    int32_t own_pre = static_cast<int32_t>(c408_read(actor, g_c408.prop[kC408CurRightTurns]));
    // Card 408 F1: both events start from the host's yaw (a timeout fires from it: the round leaves the muzzle in the
    // host's direction). `diff` above is the drift before this set; `after` checks the set took.
    if (root != nullptr) {
        const Vec3 rot = read_vec3(root, kC408RelativeRotation);
        const double set[3] = {rot.x, host_yaw, rot.z};
        g_set_world_rotation(root, set, false, nullptr, kTeleportPhysics);
    }
    if (ev == kC408Timeout) { // the timeout's turn count is the host's too (the next SelectRightOrLeftTurn reads it)
        own_pre = host_cur;
        c408_write_int(actor, g_c408.prop[kC408CurRightTurns], host_cur);
    }
    if (ev == kC408Turn && root != nullptr) {
        // SelectRightOrLeftTurn: RandomBool only while -N < CurRightTurns < N; else CurRightTurns < N turns right,
        // otherwise left. -N-1 forces right and N forces left, N = 0 included.
        const int32_t bound = static_cast<int32_t>(c408_read(actor, g_c408.prop[kC408MaxDegrees])) / kC408DegreesPerTurn;
        if (host_turn) {
            forced = host_right ? -bound - 1 : bound;
            own_pre = forced;
            c408_write_int(actor, g_c408.prop[kC408CurRightTurns], forced);
        }
    }
    const double after = fabs(c408_wrap(c408_wrap(c408_yaw(actor)) - host_yaw));
    if (!c408_call(actor, ev)) {
        ++g_c408.client_missed;
        return "event parms larger than the buffer";
    }
    const bool own_right = c408_read(actor, g_c408.prop[kC408TurningRight]) != 0.0;
    const int32_t own_cur = static_cast<int32_t>(c408_read(actor, g_c408.prop[kC408CurRightTurns]));
    const bool own_turn = ev == kC408Turn && own_cur != own_pre;
    int32_t age = -1;
    float aged = -1.0f;
    if (own_turn) { // card 408 F5: the turn starts where the host's is now
        const uint8_t* prop = nullptr;
        void* const timeline = const_cast<void*>(prop_object_field(actor, kC408Timeline, &prop));
        age = c132_ledger_frame_age_ms(pc, kC408MaxAgeMs);
        aged = age > 0 ? c313_advance_timeline(timeline, age) : -1.0f;
        g_c408.aged += aged >= 0.0f ? 1u : 0u;
        g_c408.age_max = age > g_c408.age_max ? age : g_c408.age_max;
    }
    if (ev == kC408Turn) {
        c408_write_int(actor, g_c408.prop[kC408CurRightTurns], host_cur);
    }
    const bool match = ev != kC408Turn || (own_turn == host_turn && (!host_turn || own_right == host_right));
    g_c408.dir_mismatch += match ? 0u : 1u;
    g_c408.yaw_diff_max = ev == kC408Timeout && diff > g_c408.yaw_diff_max ? diff : g_c408.yaw_diff_max;
    g_c408.after_max = after > g_c408.after_max ? after : g_c408.after_max;
    ++g_c408.client_rows;
    if (!match || note_event_cap(g_c408.lines, kC408Lines, "card408 attack lines")) {
        log::line("%scard408: client t=%llu %s %s yaw %.2f DoATurn %d dir %c CurRightTurns %d: own yaw %.2f (diff "
                  "%.2f, after the host yaw set %.2f), own call DoATurn %d dir %c CurRightTurns %d (forced %d): %s; "
                  "row age %d ms, timeline at %.3f",
                  match ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()), kC408EventText[ev], name,
                  host_yaw, host_turn ? 1 : 0, host_right ? 'R' : 'L', host_cur, own_yaw, diff, after, own_turn ? 1 : 0,
                  own_right ? 'R' : 'L', own_cur, forced, match ? "same" : "direction differs", age,
                  static_cast<double>(aged));
    }
    return nullptr;
}

// Both sides: a building of the class (its TurnInPlace timeline rows are not sent).
bool c408_building(const void* actor) {
    return g_c408_pe && actor != nullptr && is_a(actor, g_c408.cls);
}

// Both sides: a building of the class or the plasma projectile one spawns (their effects stay local).
bool c408_attack(const void* actor) {
    return g_c408_pe && actor != nullptr &&
           (is_a(actor, g_c408.cls) || (g_c408.projectile != nullptr && is_a(actor, g_c408.projectile)));
}

// Both sides: the plasma projectile a building spawns (it is simulated per side, so every effect it owns stays local).
bool c408_projectile(const void* actor) {
    return g_c408_pe && actor != nullptr && g_c408.projectile != nullptr && is_a(actor, g_c408.projectile);
}

void c408_step(void* engine) {
    void* const world = engine != nullptr ? g_get_game_world(engine) : nullptr;
    const ULONGLONG now = GetTickCount64();
    if (g_c408.round_n > 0) {
        c419_step(now); // card 419
    }
    if (world == nullptr || now < g_c408.scan_due) {
        return;
    }
    g_c408.scan_due = now + kC408ScanMs;
    c408_scan(world);
}

// Card 477: at LoadMap enter (and the test quit), before the new world's actors get BeginPlay. The cached classes
// are map Blueprints freed with the old map; c419_begin tests rounds of the new map against them during LoadMap,
// before c408_arm_map runs at LoadMap return (run c419b184a: client crash in IsChildOf at the frontend load).
void c408_map_end() {
    if (g_c408_pe) {
        log::line("card408: %s map totals: %d classes, %u host rows (%u not relayed), %u client refusals, %u client "
                  "rows applied (%u missed, %u direction differs), largest yaw difference at a timeout %.2f (after the "
                  "host yaw set, any row: %.2f), %u turn timelines moved on by the row age (oldest turn row %d ms)",
                  side(), g_c408.seen_n, g_c408.host_rows, g_c408.host_unsent, g_c408.refused, g_c408.client_rows,
                  g_c408.client_missed, g_c408.dir_mismatch, g_c408.yaw_diff_max, g_c408.after_max, g_c408.aged,
                  g_c408.age_max);
        log::line("card419: %s map totals: %u burst rows sent (%u not relayed), %u client bursts applied (%u missed, "
                  "largest burst diff %.1f uu, oldest row %d ms), %u fallback bursts, %u untracked rounds, %u left",
                  side(), g_c408.burst_rows, g_c408.burst_unsent, g_c408.burst_applied, g_c408.burst_missed,
                  g_c408.burst_diff_max, g_c408.burst_age_max, g_c408.fallback, g_c408.untracked, g_c408.round_n);
    }
    g_c408 = C408State{}; // the classes are looked up again: they may unload with the map
    g_c408_pe = false;
}

// The map's own arm: c408_map_end has already printed the totals and dropped the old map's classes; this reset
// covers the first map of a process, which has no LoadMap enter before it.
void c408_arm_map() {
    g_c408 = C408State{};
    g_c408_pe = false;
}

} // namespace ubw::hooks
