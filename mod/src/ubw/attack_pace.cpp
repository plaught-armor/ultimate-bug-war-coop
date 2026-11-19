// Card 122 measurement (UBWCOOP_C122_LOG=1, host only, log only): what keeps a bug that is in the fight from attacking.
//
// Hooks (installed only with the flag):
// - ASTEnemyBase::CanPerformAttack (0x051806D0): an enemy that makes attack checks is in the fight.
// - USTAttackComponent::ChooseAvailableAttack (0x051820E0): the enemy picks an attack; false = none usable now.
// - USTAttackComponent::PerformAttack (0x0519E680): an attack starts.
// Every kC122SampleMs the step samples each enemy seen in the fight within kC122FightMs: attacking (bIsAttacking),
// global cooldown (GlobalCooldownTimer > 0), every attack on its own cooldown (AttackStateMap, the float
// IsAttackOnCooldown 0x05196D80 reads at element +0x1C), or ready, with the distance to its primary target (card 103
// pick) and that target's roster slot. Every kC122WindowMs one window line; once per enemy class its attack configs.

#include "ubw/hooks_internal.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace ubw::hooks {

bool g_c122_log = false;
C122CanFn g_orig_c122_can = nullptr;
C122ChooseFn g_orig_c122_choose = nullptr;
C122PerformFn g_orig_c122_perform = nullptr;
C122FitFn g_orig_c122_fit = nullptr;
C122SizeFn g_c122_size = nullptr;
StaticClassFn g_c122_target_class = nullptr;

namespace {

constexpr int32_t kC122Slots = 96;
constexpr int32_t kC122Classes = 16;
constexpr ULONGLONG kC122SampleMs = 100;
constexpr ULONGLONG kC122WindowMs = 5000;
constexpr ULONGLONG kC122FightMs = 1500;
constexpr uint32_t kC122Lines = 600;
constexpr size_t kC122StateMap = 0x120;       // USTAttackComponent::AttackStateMap (IsAttackOnCooldown reads +0x120)
constexpr size_t kC122StateBytes = 0x28;      // TSetElement: FName key, 0x18 value, hash next +0x20, hash index +0x24
constexpr size_t kC122StateCooldown = 0x1C;   // IsAttackOnCooldown: on cooldown while this float > 0
constexpr int32_t kC122MaxStates = 32;
constexpr int32_t kC122Bins = 4;              // ready distance: < 300, < 600, < 1200, >= 1200 cm
constexpr double kC122BinEdge[kC122Bins - 1] = {300.0, 600.0, 1200.0};
constexpr int32_t kC122Descs = 15;            // ESTAttackStateDescription, exe string table order
constexpr const char* kC122DescNames[kC122Descs] = {
    "Disabled", "DifficultyLevel", "OnCooldown", "OnGlobalCooldown", "NoAttackData", "OutOfAmmo", "NeedsArena",
    "NeedsNoArena", "TagConditionsFailed", "NoValidTarget", "UnableToSwitchTarget", "TargetNotInRange", "TargetNoLOS",
    "NoTicket", "Ready"};
constexpr int32_t kC122DescAttacks = 2;       // the attacks histogrammed: Melee, Lunge
constexpr const char* kC122DescAttack[kC122DescAttacks] = {"Melee", "Lunge"};
constexpr double kC122NearCm = 1200.0;        // idle bugs this close to a player get the histogram
constexpr int32_t kC122Zones = 48;
constexpr size_t kC122ZoneCapMax = 0x2C4;     // ASTArenaZoneBase::ZoneCapacityMax (BeginPlay copies it to +0x3CC)
constexpr size_t kC122ZoneCapNow = 0x3CC;     // remaining capacity: AddEnemyToZone subtracts GetSizeForZone, Remove adds
constexpr size_t kC122ZoneOccupiers = 0x370;  // TArray<ASTEnemyBase*> Occupiers (CanEnemyFitIntoZone scans it)
// UADTargetComponent::SelectPrimaryTarget (0x051A5940) gives "the player" (card 103: the enemy's own player) the
// AttackPlayerOverOtherTargetsThreatValue (+0xC8) on a roll < AttackPlayerOverOtherTargetsPercentage (+0xC4), only if
// its FADTargetData passes these checks, in this order.
constexpr int32_t kC122Gates = 8;
constexpr const char* kC122GateNames[kC122Gates] = {"unlisted", "+0x28 zero", "+0x29 is 4", "+0x4A not 2",
                                                    "+0x4B not 2", "+0x4C is 1", "+0x58 over +0x1A8", "pass"};
constexpr size_t kC122TdValid = 0x28;
constexpr size_t kC122TdState = 0x29;
constexpr size_t kC122TdA = 0x4A;
constexpr size_t kC122TdB = 0x4B;
constexpr size_t kC122TdC = 0x4C;
constexpr size_t kC122TdThreat = 0x50;
constexpr size_t kC122TdRange = 0x58;
constexpr size_t kC122TcRange = 0x1A8;

struct C122Enemy {
    const void* enemy;   // identity
    C31Weak weak;
    C31Weak comp;        // its USTAttackComponent, from the first ChooseAvailableAttack or PerformAttack
    ULONGLONG seen;      // last attack check or choice
    bool counted;        // counted in this window's fight set
    bool started;        // started an attack in this window
    bool bug;            // not an NPC trooper (class name without "Trooper"): troopers also make attack checks
};

struct C122Layout {
    bool ok;
    int32_t attacking;   // bIsAttacking byte offset
    uint8_t mask;
    int32_t global;      // GlobalCooldownTimer
    int32_t desc;        // FSTAttackState::Description in an AttackStateMap element (-1 unresolved)
};

struct C122Window {
    ULONGLONG start;
    uint32_t samples;
    uint32_t in_fight;           // distinct enemies in the fight this window
    uint32_t by_target[kC43Max + 1]; // last slot: not a player
    uint32_t starts;
    uint32_t starters;
    uint32_t choose_ok;
    uint32_t choose_none;
    uint32_t checks;
    uint32_t peak_attacking;
    uint64_t sum_attacking;      // over samples
    uint64_t sum_fight;
    uint64_t sum_global;
    uint64_t sum_all_cd;
    uint64_t sum_ready;
    uint64_t sum_bin[kC122Bins];
    uint64_t sum_attack_dist[kC122Bins];
    uint32_t peak_holders;
    uint32_t fit_ok;
    uint32_t fit_no;
    uint64_t sum_attacking_player; // attacking with a player pawn as primary target
    uint32_t peak_attacking_player;
    const void* other_cls[4];      // classes of non-player primary targets, with counts
    uint32_t other_num[4];
    uint32_t no_target;
    uint32_t desc[kC122DescAttacks][kC122Descs + 1]; // last: out of range value
    uint32_t gate[kC122Gates];     // (in-fight bug, player pawn) pairs per sample: first failing boost check
    uint32_t v4a[4];               // listed pairs: +0x4A value 0, 1, 2, other
    uint32_t v4b[4];
    double sum_threat;             // listed pairs: player threat (+0x50)
    uint32_t listed;
    float ex_td_range;             // one listed pair: +0x58 and the component's +0x1A8
    float ex_tc_range;
};

struct C122Zone {
    const void* zone;
    C31Weak weak;
};

struct C122State {
    C122Enemy e[kC122Slots];
    int32_t used;
    int32_t next;
    C122Layout layout;
    const void* classes[kC122Classes];
    int32_t class_count;
    ULONGLONG last_sample;
    C122Window w;
    uint32_t lines;
    uintptr_t world;
    C122Zone zones[kC122Zones];
    int32_t zone_count;
};
C122State g_c122{};

void c122_zone_line();
void c122_desc_line();
void c122_gate_line();

bool c122_line() {
    return note_event_cap(g_c122.lines, kC122Lines, "card122 log");
}

C122Enemy* c122_slot(const void* enemy, ULONGLONG now) {
    for (int32_t i = 0; i < g_c122.used; ++i) {
        if (g_c122.e[i].enemy == enemy) {
            return &g_c122.e[i];
        }
    }
    C122Enemy* const e = &g_c122.e[g_c122.used < kC122Slots ? g_c122.used++ : g_c122.next];
    if (g_c122.used == kC122Slots) {
        g_c122.next = (g_c122.next + 1) % kC122Slots;
    }
    *e = C122Enemy{enemy, {}, {}, now, false, false, strstr(class_name(enemy).text, "Trooper") == nullptr};
    c31_weak_set(e->weak, const_cast<void*>(enemy));
    return e;
}

void c122_put(char* buf, size_t cap, size_t& n, const char* fmt, ...) {
    if (n >= cap) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    const int k = vsnprintf(buf + n, cap - n, fmt, ap);
    va_end(ap);
    n = k < 0 ? cap : (n + static_cast<size_t>(k) < cap ? n + static_cast<size_t>(k) : cap);
}

// One reflected leaf field of `ustruct` at `base` as " name=value".
void c122_field(char* buf, size_t cap, size_t& n, const void* ustruct, const uint8_t* base, const char* name) {
    const uint8_t* const f = ustruct != nullptr ? prop_find_prop(ustruct, name) : nullptr;
    if (f == nullptr || base == nullptr) {
        c122_put(buf, cap, n, " %s=?", name);
        return;
    }
    const int32_t off = read_i32(f, kPropertyOffset);
    const int32_t size = read_i32(f, kPropertyElementSize);
    ObjectName kind{};
    switch (prop_kind(f, kind)) {
    case PropKind::Float:
        c122_put(buf, cap, n, " %s=%.3f", name, read_f32(base, static_cast<size_t>(off)));
        break;
    case PropKind::Bool:
        c122_put(buf, cap, n, " %s=%d", name,
                 prop_bool_ok(f) && (base[off + f[kBoolByteOffset]] & f[kBoolFieldMask]) != 0 ? 1 : 0);
        break;
    case PropKind::Name:
        c122_put(buf, cap, n, " %s=%s", name, prop_name(base + off, size).text);
        break;
    case PropKind::Object: {
        const void* const o = read_ptr(base, static_cast<size_t>(off));
        c122_put(buf, cap, n, " %s=%s", name, o != nullptr ? object_name(o).text : "none");
        break;
    }
    default:
        if (size == 1) {
            c122_put(buf, cap, n, " %s=%u", name, base[off]);
        } else if (size == 4) {
            c122_put(buf, cap, n, " %s=%d", name, read_i32(base, static_cast<size_t>(off)));
        } else {
            c122_put(buf, cap, n, " %s=<%s %d>", name, kind.text, size);
        }
        break;
    }
}

void c122_resolve_layout(const void* comp) {
    const void* const cls = read_ptr(comp, kObjectClass);
    const uint8_t* const a = prop_find_prop(cls, "bIsAttacking");
    const uint8_t* const g = prop_find_prop(cls, "GlobalCooldownTimer");
    const uint8_t* const m = prop_find_prop(cls, "AttackStateMap");
    g_c122.layout.ok = a != nullptr && g != nullptr && prop_bool_ok(a) && m != nullptr &&
                       read_i32(m, kPropertyOffset) == static_cast<int32_t>(kC122StateMap);
    if (a != nullptr && prop_bool_ok(a)) {
        g_c122.layout.attacking = read_i32(a, kPropertyOffset) + a[kBoolByteOffset];
        g_c122.layout.mask = a[kBoolFieldMask];
    }
    g_c122.layout.global = g != nullptr ? read_i32(g, kPropertyOffset) : 0;
    const uint8_t* const vp = m != nullptr ? static_cast<const uint8_t*>(read_ptr(m, kMapPropValue)) : nullptr;
    const void* const vs = vp != nullptr ? read_ptr(vp, kStructPropStruct) : nullptr;
    const uint8_t* const d = vs != nullptr ? prop_find_prop(vs, "Description") : nullptr;
    const uint8_t* const cd = vs != nullptr ? prop_find_prop(vs, "Cooldown") : nullptr;
    const int32_t voff = m != nullptr ? read_i32(m, kMapValueOffset) : 0;
    const bool cd_ok = cd != nullptr && voff + read_i32(cd, kPropertyOffset) == static_cast<int32_t>(kC122StateCooldown);
    g_c122.layout.desc = d != nullptr && cd_ok ? voff + read_i32(d, kPropertyOffset) : -1;
    if (c122_line()) {
        log::line("%scard122: AttackStateMap value +0x%X: Cooldown %s, Description +0x%X", cd_ok ? "" : "WARN ", voff,
                  cd_ok ? "+0x1C ok" : "BAD", g_c122.layout.desc);
    }
    if (c122_line()) {
        log::line("%scard122: attack component %s: bIsAttacking +0x%X mask 0x%X, GlobalCooldownTimer +0x%X, "
                  "AttackStateMap +0x%X (expected +0x%X), layout %s",
                  g_c122.layout.ok ? "" : "WARN ", class_name(comp).text, g_c122.layout.attacking, g_c122.layout.mask,
                  g_c122.layout.global, m != nullptr ? read_i32(m, kPropertyOffset) : -1,
                  static_cast<unsigned>(kC122StateMap), g_c122.layout.ok ? "ok" : "BAD");
    }
}

// Once per enemy class: the attack component's global cooldown and each attack config with its attack data.
// Once per class: the target component's player preference and target scoring (card 122 target-choice candidate).
void c122_log_target(const void* enemy) {
    const void* const t = g_c122_target_class != nullptr ? find_component(enemy, g_c122_target_class) : nullptr;
    constexpr size_t kCap = 700;
    char buf[kCap];
    size_t n = 0;
    c122_put(buf, kCap, n, "card122: class %s target component %s:", class_name(enemy).text,
             t != nullptr ? class_name(t).text : "none");
    const void* const tcls = t != nullptr ? read_ptr(t, kObjectClass) : nullptr;
    for (const char* f : {"AttackPlayerOverOtherTargetsPercentage", "AttackPlayerOverOtherTargetsThreatValue",
                          "TargetScoreDistanceValue", "SightRadius", "MaxUpdateTargetTime", "TargetPriorityLevel",
                          "TargetDataCachePeriod", "ThreatDepreciationDelay", "ThreatDepreciationInterval",
                          "ThreatDepreciationRate"}) {
        c122_field(buf, kCap, n, tcls, static_cast<const uint8_t*>(t), f);
    }
    if (c122_line()) {
        log::line("%s", buf);
    }
}

void c122_log_class(const void* enemy, const void* comp) {
    const void* const ecls = read_ptr(enemy, kObjectClass);
    for (int32_t i = 0; i < g_c122.class_count; ++i) {
        if (g_c122.classes[i] == ecls) {
            return;
        }
    }
    if (g_c122.class_count >= kC122Classes) {
        return;
    }
    g_c122.classes[g_c122.class_count++] = ecls;
    const void* const ccls = read_ptr(comp, kObjectClass);
    const uint8_t* const c = static_cast<const uint8_t*>(comp);
    constexpr size_t kCap = 1400;
    char buf[kCap];
    size_t n = 0;
    c122_put(buf, kCap, n, "card122: class %s attack component %s:", class_name(enemy).text, class_name(comp).text);
    c122_field(buf, kCap, n, ccls, c, "GlobalCooldown");
    c122_field(buf, kCap, n, ccls, c, "AttackPatternConfig");
    const uint8_t* const z = prop_find_prop(ecls, "CachedPreferredZone");
    c122_put(buf, kCap, n, "; GetSizeForZone %d; CachedPreferredZone %s",
             g_c122_size != nullptr ? g_c122_size(const_cast<void*>(enemy)) : -1,
             z != nullptr && read_ptr(enemy, static_cast<size_t>(read_i32(z, kPropertyOffset))) != nullptr ? "set"
                                                                                                            : "none");
    if (c122_line()) {
        log::line("%s", buf);
    }
    c122_log_target(enemy);
    const uint8_t* const arr = prop_find_prop(ccls, "AttackDataConfig");
    const uint8_t* const inner = arr != nullptr ? static_cast<const uint8_t*>(read_ptr(arr, kArrayPropInner)) : nullptr;
    const void* const st = inner != nullptr ? read_ptr(inner, kStructPropStruct) : nullptr;
    const int32_t stride = inner != nullptr ? read_i32(inner, kPropertyElementSize) : 0;
    const size_t aoff = arr != nullptr ? static_cast<size_t>(read_i32(arr, kPropertyOffset)) : 0;
    const uint8_t* const data = arr != nullptr ? static_cast<const uint8_t*>(read_ptr(c, aoff)) : nullptr;
    const int32_t num = arr != nullptr ? read_i32(c, aoff + kArrayNum) : 0;
    constexpr int32_t kMaxConfigs = 12;
    for (int32_t i = 0; data != nullptr && st != nullptr && stride > 0 && i < num && i < kMaxConfigs; ++i) {
        const uint8_t* const e = data + static_cast<size_t>(i) * static_cast<size_t>(stride);
        n = 0;
        c122_put(buf, kCap, n, "card122:   attack %d/%d:", i, num);
        for (const char* f : {"AttackName", "bIsEnabled", "CooldownTime", "bIsRandomiseInitialCooldown", "Priority",
                              "DifficultyLevel", "TargetConditionType", "AttackDataClass", "AttackData"}) {
            c122_field(buf, kCap, n, st, e, f);
        }
        const uint8_t* const ad = prop_find_prop(st, "AttackData");
        const void* obj = ad != nullptr ? read_ptr(e, static_cast<size_t>(read_i32(ad, kPropertyOffset))) : nullptr;
        if (obj == nullptr) {
            const uint8_t* const adc = prop_find_prop(st, "AttackDataClass");
            const void* const k = adc != nullptr ? read_ptr(e, static_cast<size_t>(read_i32(adc, kPropertyOffset)))
                                                 : nullptr;
            obj = k != nullptr ? read_ptr(k, kClassDefaultObject) : nullptr;
        }
        if (obj != nullptr) {
            c122_put(buf, kCap, n, " | data %s:", class_name(obj).text);
            const void* const ocls = read_ptr(obj, kObjectClass);
            for (const char* f : {"TicketType", "TicketCost", "MinimumRange", "MaximumRange", "CooldownBehaviour",
                                  "SafetyTimeout", "ArenaRequirement", "StartAttackMovementBehaviour",
                                  "bRequiresLineOfSight"}) {
                c122_field(buf, kCap, n, ocls, static_cast<const uint8_t*>(obj), f);
            }
        }
        if (c122_line()) {
            log::line("%s", buf);
        }
    }
}

void c122_note_comp(void* comp, ULONGLONG now) {
    void* const enemy = read_ptr(comp, kComponentOwner);
    if (enemy == nullptr) {
        return;
    }
    C122Enemy* const e = c122_slot(enemy, now);
    if (!e->bug) {
        return;
    }
    e->seen = now;
    if (e->comp.serial == 0) {
        c31_weak_set(e->comp, comp);
        if (!g_c122.layout.ok && g_c122.layout.attacking == 0) {
            c122_resolve_layout(comp);
        }
        c122_log_class(enemy, comp);
    }
    if (!e->counted) {
        e->counted = true;
        ++g_c122.w.in_fight;
    }
}

int32_t c122_bin(double d) {
    int32_t b = 0;
    while (b < kC122Bins - 1 && d >= kC122BinEdge[b]) {
        ++b;
    }
    return b;
}

// Description histogram of one AttackStateMap element (Melee or Lunge) of an idle bug near a player.
void c122_note_desc(const uint8_t* elem, double dist) {
    if (g_c122.layout.desc < 0 || dist < 0.0 || dist >= kC122NearCm) {
        return;
    }
    const ObjectName key = fname_text(elem);
    for (int32_t a = 0; a < kC122DescAttacks; ++a) {
        if (strcmp(key.text, kC122DescAttack[a]) == 0) {
            const uint8_t d = elem[g_c122.layout.desc];
            ++g_c122.w.desc[a][d < kC122Descs ? d : kC122Descs];
        }
    }
}

Vec3 c122_loc(const void* actor) {
    const void* const root = read_ptr(actor, kActorRootComponent);
    Vec3 v{};
    if (root != nullptr) {
        memcpy(&v, static_cast<const uint8_t*>(root) + kComponentTranslation, sizeof v);
    }
    return v;
}

// One (bug, player) pair: the first SelectPrimaryTarget boost check that fails, as the exe orders them.
void c122_note_gate(const void* enemy, const void* pawn) {
    C122Window& w = g_c122.w;
    const uint8_t* const tc = g_c122_target_class != nullptr
                                  ? static_cast<const uint8_t*>(find_component(enemy, g_c122_target_class))
                                  : nullptr;
    const uint8_t* const d = tc != nullptr && g_c103_find != nullptr
                                 ? static_cast<const uint8_t*>(g_c103_find(tc, pawn))
                                 : nullptr;
    if (tc == nullptr) {
        return;
    }
    int32_t g = 0;
    if (d != nullptr) {
        ++w.listed;
        w.sum_threat += read_f32(d, kC122TdThreat);
        ++w.v4a[d[kC122TdA] < 3 ? d[kC122TdA] : 3];
        ++w.v4b[d[kC122TdB] < 3 ? d[kC122TdB] : 3];
        w.ex_td_range = read_f32(d, kC122TdRange);
        w.ex_tc_range = read_f32(tc, kC122TcRange);
        g = d[kC122TdValid] == 0                                      ? 1
            : d[kC122TdState] == 4                                    ? 2
            : d[kC122TdA] != 2                                        ? 3
            : d[kC122TdB] != 2                                        ? 4
            : d[kC122TdC] == 1                                        ? 5
            : read_f32(tc, kC122TcRange) < read_f32(d, kC122TdRange) ? 6
                                                                      : 7;
    }
    ++w.gate[g];
}

void c122_sample(const C43Roster& roster) {
    C122Window& w = g_c122.w;
    const ULONGLONG now = GetTickCount64();
    uint32_t attacking = 0;
    uint32_t attacking_player = 0;
    uint32_t fight = 0;
    for (int32_t i = 0; i < g_c122.used; ++i) {
        C122Enemy& e = g_c122.e[i];
        if (now - e.seen > kC122FightMs) {
            continue;
        }
        const void* const enemy = c31_weak_get(e.weak);
        const uint8_t* const comp = static_cast<const uint8_t*>(c31_weak_get(e.comp));
        if (enemy == nullptr || comp == nullptr || !g_c122.layout.ok) {
            continue;
        }
        ++fight;
        const void* const target = c103_pick_of(enemy);
        double dist = -1.0; // to the nearest player pawn
        bool on_player = false;
        const Vec3 a = c122_loc(enemy);
        for (uint32_t p = 0; p < roster.players && p < kC43Max; ++p) {
            const void* const pawn = roster.p[p].pawn;
            if (pawn == nullptr) {
                continue;
            }
            on_player = on_player || pawn == target;
            c122_note_gate(enemy, pawn);
            const Vec3 b = c122_loc(pawn);
            const double d = sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
            dist = dist < 0.0 || d < dist ? d : dist;
        }
        if ((comp[g_c122.layout.attacking] & g_c122.layout.mask) != 0) {
            ++attacking;
            attacking_player += on_player ? 1 : 0;
            w.sum_attack_dist[c122_bin(dist < 0.0 ? 1e9 : dist)]++;
            continue;
        }
        if (read_f32(comp, static_cast<size_t>(g_c122.layout.global)) > 0.0f) {
            ++w.sum_global;
            continue;
        }
        SparseView v{};
        int32_t states = 0;
        int32_t on_cd = 0;
        if (sparse_view(comp + kC122StateMap, kC122MaxStates, v)) {
            for (int32_t s = 0; s < v.count && s < kC122MaxStates; ++s) {
                if (sparse_allocated(v, s)) {
                    c122_note_desc(v.data + static_cast<size_t>(s) * kC122StateBytes, dist);
                    ++states;
                    on_cd += read_f32(v.data, static_cast<size_t>(s) * kC122StateBytes + kC122StateCooldown) > 0.0f
                                 ? 1
                                 : 0;
                }
            }
        }
        if (states > 0 && on_cd == states) {
            ++w.sum_all_cd;
            continue;
        }
        ++w.sum_ready;
        w.sum_bin[c122_bin(dist < 0.0 ? 1e9 : dist)]++;
    }
    ++w.samples;
    w.sum_attacking += attacking;
    w.sum_fight += fight;
    w.peak_attacking = attacking > w.peak_attacking ? attacking : w.peak_attacking;
    w.sum_attacking_player += attacking_player;
    w.peak_attacking_player = attacking_player > w.peak_attacking_player ? attacking_player : w.peak_attacking_player;
    const int32_t holders = c111_holders(1); // ESTTicketType Melee
    w.peak_holders = holders > static_cast<int32_t>(w.peak_holders) ? static_cast<uint32_t>(holders) : w.peak_holders;
}

void c122_window(const C43Roster& roster, ULONGLONG now) {
    C122Window& w = g_c122.w;
    for (int32_t i = 0; i < g_c122.used; ++i) {
        C122Enemy& e = g_c122.e[i];
        if (!e.counted) {
            continue;
        }
        // Card 331: an enemy counted earlier in the window can be gone now; read it through its weak (null: no target).
        const void* const live = c31_weak_get(e.weak);
        const void* const target = live != nullptr ? c103_pick_of(live) : nullptr;
        uint32_t slot = kC43Max;
        for (uint32_t p = 0; target != nullptr && p < kC43Max; ++p) {
            slot = roster.p[p].pawn == target ? p : slot;
        }
        ++w.by_target[slot];
        if (slot == kC43Max && target == nullptr) {
            ++w.no_target;
        }
        const void* const cls = slot == kC43Max && target != nullptr ? read_ptr(target, kObjectClass) : nullptr;
        for (int32_t k = 0; cls != nullptr && k < 4; ++k) {
            if (w.other_cls[k] == nullptr || w.other_cls[k] == cls) {
                w.other_cls[k] = cls;
                ++w.other_num[k];
                break;
            }
        }
    }
    const double s = w.samples > 0 ? static_cast<double>(w.samples) : 1.0;
    if (w.in_fight > 0 && c122_line()) {
        log::line("card122: window %llu ms, %u players: in fight %u (target p0 %u p1 %u other %u), checks %u, "
                  "choose ok %u none %u, attack starts %u by %u bugs",
                  static_cast<unsigned long long>(now - w.start), roster.players, w.in_fight, w.by_target[0],
                  w.by_target[1], w.by_target[kC43Max], w.checks, w.choose_ok, w.choose_none, w.starts, w.starters);
        log::line("card122:   per sample (%u): in fight %.2f, attacking peak %u mean %.2f (dist <300 %.2f <600 %.2f "
                  "<1200 %.2f far %.2f), global cd %.2f, all attacks cd %.2f, ready %.2f (dist <300 %.2f <600 %.2f "
                  "<1200 %.2f far %.2f); Melee ticket holders peak %u",
                  w.samples, w.sum_fight / s, w.peak_attacking, w.sum_attacking / s, w.sum_attack_dist[0] / s,
                  w.sum_attack_dist[1] / s, w.sum_attack_dist[2] / s, w.sum_attack_dist[3] / s, w.sum_global / s,
                  w.sum_all_cd / s, w.sum_ready / s, w.sum_bin[0] / s, w.sum_bin[1] / s, w.sum_bin[2] / s,
                  w.sum_bin[3] / s, w.peak_holders);
        log::line("card122:   attacking a player: peak %u mean %.2f; non-player targets: none %u, %s %u, %s %u, %s %u, "
                  "%s %u",
                  w.peak_attacking_player, w.sum_attacking_player / s, w.no_target,
                  w.other_cls[0] != nullptr ? object_name(w.other_cls[0]).text : "-", w.other_num[0],
                  w.other_cls[1] != nullptr ? object_name(w.other_cls[1]).text : "-", w.other_num[1],
                  w.other_cls[2] != nullptr ? object_name(w.other_cls[2]).text : "-", w.other_num[2],
                  w.other_cls[3] != nullptr ? object_name(w.other_cls[3]).text : "-", w.other_num[3]);
    }
    if (w.in_fight > 0) {
        c122_zone_line();
        c122_desc_line();
        c122_gate_line();
    }
    for (int32_t i = 0; i < g_c122.used; ++i) {
        g_c122.e[i].counted = false;
        g_c122.e[i].started = false;
    }
    w = C122Window{};
    w.start = now;
}

void c122_gate_line() {
    const C122Window& w = g_c122.w;
    constexpr size_t kCap = 600;
    char buf[kCap];
    size_t n = 0;
    c122_put(buf, kCap, n, "card122:   player boost checks (bug, player) per sample:");
    for (int32_t g = 0; g < kC122Gates; ++g) {
        c122_put(buf, kCap, n, " %s %u", kC122GateNames[g], w.gate[g]);
    }
    c122_put(buf, kCap, n, "; listed %u: +0x4A 0/1/2/other %u/%u/%u/%u, +0x4B %u/%u/%u/%u, threat mean %.1f; "
             "example +0x58 %.1f +0x1A8 %.1f", w.listed, w.v4a[0], w.v4a[1], w.v4a[2], w.v4a[3], w.v4b[0], w.v4b[1],
             w.v4b[2], w.v4b[3], w.listed > 0 ? w.sum_threat / w.listed : 0.0, w.ex_td_range, w.ex_tc_range);
    if (c122_line()) {
        log::line("%s", buf);
    }
}

void c122_note_zone(const void* zone) {
    for (int32_t i = 0; i < g_c122.zone_count; ++i) {
        if (g_c122.zones[i].zone == zone) {
            return;
        }
    }
    if (g_c122.zone_count >= kC122Zones) {
        return;
    }
    C122Zone& z = g_c122.zones[g_c122.zone_count++];
    z.zone = zone;
    c31_weak_set(z.weak, const_cast<void*>(zone));
    const uint8_t* const f = prop_find_prop(read_ptr(zone, kObjectClass), "ZoneCapacityMax");
    if (c122_line()) {
        log::line("%scard122: zone %d %s: ZoneCapacityMax +0x%X (expected +0x%X) = %d, capacity now %d, occupiers %d",
                  f != nullptr && read_i32(f, kPropertyOffset) == static_cast<int32_t>(kC122ZoneCapMax) ? "" : "WARN ",
                  g_c122.zone_count - 1, object_name(zone).text, f != nullptr ? read_i32(f, kPropertyOffset) : -1,
                  static_cast<unsigned>(kC122ZoneCapMax), read_i32(zone, kC122ZoneCapMax),
                  read_i32(zone, kC122ZoneCapNow), read_i32(zone, kC122ZoneOccupiers + kArrayNum));
    }
}

// Window line for the zones the fight checked: total max and remaining capacity, occupiers, full zones.
void c122_zone_line() {
    int32_t live = 0, cap_max = 0, cap_now = 0, occ = 0, full = 0;
    for (int32_t i = 0; i < g_c122.zone_count; ++i) {
        const void* const z = c31_weak_get(g_c122.zones[i].weak);
        if (z == nullptr) {
            continue;
        }
        ++live;
        cap_max += read_i32(z, kC122ZoneCapMax);
        cap_now += read_i32(z, kC122ZoneCapNow);
        occ += read_i32(z, kC122ZoneOccupiers + kArrayNum);
        full += read_i32(z, kC122ZoneCapNow) <= 0 ? 1 : 0;
    }
    if (c122_line()) {
        log::line("card122:   zones: fit checks ok %u refused %u; %d zones: capacity max %d, left %d, occupiers %d, "
                  "full %d",
                  g_c122.w.fit_ok, g_c122.w.fit_no, live, cap_max, cap_now, occ, full);
    }
}

// Window line: per histogrammed attack, the sample count of each Description value seen on idle near bugs.
void c122_desc_line() {
    for (int32_t a = 0; a < kC122DescAttacks; ++a) {
        constexpr size_t kCap = 600;
        char buf[kCap];
        size_t n = 0;
        c122_put(buf, kCap, n, "card122:   idle within %.0f cm, %s state:", kC122NearCm, kC122DescAttack[a]);
        for (int32_t d = 0; d <= kC122Descs; ++d) {
            if (g_c122.w.desc[a][d] != 0) {
                c122_put(buf, kCap, n, " %s %u", d < kC122Descs ? kC122DescNames[d] : "other", g_c122.w.desc[a][d]);
            }
        }
        if (c122_line()) {
            log::line("%s", buf);
        }
    }
}

} // namespace

bool detour_c122_fit(const void* zone, void* enemy) {
    const bool ok = g_orig_c122_fit(zone, enemy);
    if (!g_client_session && zone != nullptr) {
        c122_note_zone(zone);
        ++(ok ? g_c122.w.fit_ok : g_c122.w.fit_no);
    }
    return ok;
}

bool detour_c122_can(void* enemy, uint8_t type, bool dummy, int32_t cost) {
    const bool ok = g_orig_c122_can(enemy, type, dummy, cost);
    if (!g_client_session) {
        const ULONGLONG now = GetTickCount64();
        C122Enemy* const e = c122_slot(enemy, now);
        if (e->bug) {
            e->seen = now;
            ++g_c122.w.checks;
        }
    }
    return ok;
}

bool detour_c122_choose(void* comp, float dt) {
    const bool ok = g_orig_c122_choose(comp, dt);
    if (!g_client_session) {
        c122_note_comp(comp, GetTickCount64());
        ++(ok ? g_c122.w.choose_ok : g_c122.w.choose_none);
    }
    return ok;
}

void detour_c122_perform(void* comp) {
    g_orig_c122_perform(comp);
    if (g_client_session) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    void* const enemy = read_ptr(comp, kComponentOwner);
    if (enemy == nullptr || !c122_slot(enemy, now)->bug) {
        return;
    }
    c122_note_comp(comp, now);
    ++g_c122.w.starts;
    C122Enemy* const e = c122_slot(enemy, now);
    if (!e->started) {
        e->started = true;
        ++g_c122.w.starters;
    }
}

// Host, every tick with the flag: sample and close windows.
void c122_step(void* engine) {
    if (!g_c122_log || g_client_session) {
        return;
    }
    void* const world = g_get_game_world(engine);
    // Card 349: any PC in the world will do (net mode only); not the local PC.
    void* const pc = world != nullptr ? g_world_first_pc(world) : nullptr;
    if (pc == nullptr || net_mode(pc) == kNetModeStandalone) {
        return;
    }
    if (reinterpret_cast<uintptr_t>(world) != g_c122.world) {
        g_c122.world = reinterpret_cast<uintptr_t>(world);
        g_c122.used = 0;
        g_c122.next = 0;
        g_c122.w = C122Window{};
        g_c122.w.start = GetTickCount64();
        g_c122.zone_count = 0;
    }
    const ULONGLONG now = GetTickCount64();
    if (now - g_c122.last_sample < kC122SampleMs) {
        return;
    }
    g_c122.last_sample = now;
    const C43Roster roster = c43_roster(world);
    c122_sample(roster);
    if (now - g_c122.w.start >= kC122WindowMs) {
        c122_window(roster, now);
    }
}

} // namespace ubw::hooks
