// Card 125: a mounted turret's aim, state and heat the same on both machines (always on), and the measurement behind it
// (UBWCOOP_C125_LOG=1, both machines, log only).
//
// Sync (fire goes through the card 024 relay: fire.cpp, chan_ops.cpp):
// - Aim is the turret Camera's relative rotation (+0x770); HandleInputLookHorizontal/Vertical move it and steer the
//   gun after it (the gun target at +0x8B8, eased in Tick). The driving side owns it. The client that drives a turret
//   sends its camera yaw and pitch as TurretAim at most every kC125AimMs when they change; the host applies them.
// - The host sends every placed turret's state as a card 098 WorldOp 'T' row when it changes, at most every
//   kC125RowMs per turret: camera yaw and pitch, heat, IsBeingUsed (+0x848) and IsReadyToTakeInput (+0x849, the look
//   gate). Neither byte replicates, so the client's copy of its own turret would take no look input without them. The
//   client applies the bytes and heat to its copy, and the aim unless it drives that turret itself. The journal keeps
//   the last row per turret, so a late joiner gets every turret's rest aim.
// - Applying an aim: SetRelativeRotation on the Camera, then the game's HandleInputLookHorizontal and
//   HandleInputLookVertical with 0, which aim the gun after the camera exactly as look inputs do (they need the look
//   gate open). Each sets only its own axis of the gun target (+0x8B8) and keeps the other from the old target, so
//   both are needed: with Horizontal alone the host's copy kept a level barrel (pitch 0) under a client aiming down.
//
// Hooks (installed only with the flag), all on the turret's input-receiver subobject (turret + 0x750):
// - ASTMountableTurret::HandleInputTag_Pressed / _Released (0x051CB550 / 0x051CB7E0): one line per edge, capped.
// - ASTMountableTurret::HandleInputLookHorizontal / Vertical (0x051CA740 / 0x051CAC20): counted, summed per second.
// The step logs each turret once when first seen (root yaw, aim components), dumps the turret's and its subobjects'
// properties once per turret when it is first in use, and logs a 1 Hz line for every turret in use.

#include "ubw/hooks_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace ubw::hooks {

bool g_c125_log = false;
C125TagFn g_orig_c125_tag_pressed = nullptr;
C125TagFn g_orig_c125_tag_released = nullptr;
C29LookFn g_orig_c125_look_h = nullptr;
C29LookFn g_orig_c125_look_v = nullptr;

namespace {

constexpr size_t kC125Recv = 0x750;          // input-receiver subobject (HandleInput* this)
constexpr size_t kC125ObjA = 0x680;          // HandleInputTag_Pressed: vfunc +0x970 target (this - 0xD0)
constexpr size_t kC125WeakC = 0x790;         // HandleInputTag_Pressed: weak ref resolved, vfunc +0x970 (this + 0x40)
constexpr size_t kC125AimYawComp = 0x770;    // HandleInputLookHorizontal: SetRelativeRotation target (this + 0x20)
constexpr size_t kC125AimAltComp = 0x768;    // the other SetRelativeRotation target
constexpr size_t kC125RelRot = 0x158;        // USceneComponent::RelativeRotation (FRotator, doubles P Y R)
constexpr size_t kC125InUse = 0x848;         // MountTurret's in-use byte
constexpr uint32_t kC125TagLines = 48;
constexpr uint32_t kC125StateLines = 240;
constexpr uint32_t kC125Turrets = 32;
constexpr ULONGLONG kC125PollMs = 1000;
constexpr ULONGLONG kC125AimMs = 50;    // client: TurretAim at most this often
// Card 128: no TurretAim while the host has not pinged for this long. TurretAim is reliable and goes every 50 ms
// while the aim moves; a stalled host acks nothing, and 256 outstanding bunches (about 13 s) close the connection
// with RPCReliableBufferOverflow. The aim is latest-value state, so the newest one goes when the host is back.
// Card 129: the host pings every kC26PingMs (5 s), so the hold starts only after a full interval plus half of one
// with no Ping; a shorter hold (2.5 s) fired between every two Pings and stalled the aim half the time. At most
// 7.5 s of aims (150 bunches) go out before the hold, which leaves room in the 256-bunch buffer.
constexpr ULONGLONG kC128AimHoldMs = kC26PingMs + kC26PingMs / 2;
static_assert(kC128AimHoldMs / kC125AimMs < 256, "TurretAims sent before the hold must fit the reliable buffer");
// Card 129: the host logs the first TurretAim after a gap this long (a host stall or a client hold).
constexpr ULONGLONG kC129AimGapMs = 2000;
// Card 129: after a TurretAim gap the host logs the aims applied in the next kC129AfterGapMs, at most this many
// (the reliable buffer holds 256). The aims the client sent before its hold come first, so the last line shows
// the host on the client's newest aim.
constexpr uint32_t kC128AfterGapLines = 256;
constexpr ULONGLONG kC129AfterGapMs = 2000;
constexpr ULONGLONG kC125RowMs = 100;   // host: 'T' rows at most this often
constexpr size_t kC125AlwaysTick = 0x82C;   // bAlwaysTick: LeaveTurret keeps the actor tick on when set
constexpr size_t kActorSetTickEnabledVfn = 0x4A8; // AActor::SetActorTickEnabled(bool): MountTurret exe+0x51CE883
constexpr size_t kStructScript = 0x60; // UStruct::Script (TArray<uint8>: data, then count at + 8)
constexpr size_t kC125Ready = 0x849;    // IsReadyToTakeInput: HandleInputLook* return at once while it is 0
constexpr int32_t kC125AimTenths = 1000; // aim values are tenths of a degree + this (camera clamps are 90 and 55)
constexpr int32_t kC125AimMax = 2 * kC125AimTenths;
constexpr uint32_t kC125LogLines = 60;
constexpr double kC125BugRange = 4000.0; // probe: enemies this close to a turret in use get a health line

struct C125Seen {
    int32_t turret; // object index
    bool dumped;
};

struct C125State {
    C125Seen seen[kC125Turrets];
    uint32_t seen_count;
    uint32_t tag_lines;
    uint32_t state_lines;
    uint32_t look_h;
    uint32_t look_v;
    double sum_h;
    double sum_v;
    ULONGLONG next_poll_ms;
};
C125State g_c125{};

// Card 125 sync state. Host: the last row sent per turret (object index). Client: the last aim sent.
struct C125Row {
    int32_t turret;
    uint32_t value;
};
struct C125Sync {
    const void* world; // rows are per map
    C125Row row[kC125Turrets];
    uint32_t rows;
    bool full_logged; // card 471: the table-full WARN went this map
    ULONGLONG next_row_ms;
    ULONGLONG next_aim_ms;
    uint32_t aim_sent; // client: the last TurretAim values, packed yaw | pitch << 16
    ULONGLONG aim_held_ms; // client: when the card 128 TurretAim hold began; 0 while not held
    bool aim_after_hold;   // client: the next TurretAim sent is the first after a hold (logged once)
    ULONGLONG aim_applied_ms; // host: when the last TurretAim was applied; 0 before the first
    uint32_t after_gap_lines; // host: TurretAims still to log after a gap (the last one is the client's newest)
    ULONGLONG gap_ms;         // host: when the last gap line was logged
    uint64_t rider_weak;   // client: our on-foot character, kept while we drive a turret (FWeakObjectPtr)
    bool arms_hidden;      // client: its 1P arms are hidden for the turret we drive
    uint64_t possessed_fname, unpossessed_fname;
    uint32_t lines;
    uint32_t sent_rows, sent_aims, applied_rows, applied_aims, missed;
};
C125Sync g_c125s{};

void* c125_turret(void* recv) { return static_cast<uint8_t*>(recv) - kC125Recv; }

Vec3 c125_rel(const void* turret, size_t field) {
    const void* const comp = read_ptr(turret, field);
    Vec3 r{};
    if (comp != nullptr) {
        memcpy(&r, static_cast<const uint8_t*>(comp) + kC125RelRot, sizeof(r));
    }
    return r;
}

void c125_tag_line(const char* edge, void* recv, uint64_t tag) {
    if (!note_event_cap(g_c125.tag_lines, kC125TagLines, "card125 turret tag")) {
        return;
    }
    const void* const turret = c125_turret(recv);
    const void* const weak_c = g_weak_get(static_cast<const uint8_t*>(turret) + kC125WeakC);
    log::line("card125: %s t=%llu turret %s %s %s (role %u, controller %s): +0x680 %s (%s), +0x790 %s (%s)", side(),
              static_cast<unsigned long long>(GetTickCount64()), object_name(turret).text, edge, fname_text(&tag).text,
              static_cast<const uint8_t*>(turret)[kActorLocalRole], object_name(read_ptr(turret, kPawnController)).text,
              object_name(read_ptr(turret, kC125ObjA)).text, class_name(read_ptr(turret, kC125ObjA)).text,
              object_name(weak_c).text, class_name(weak_c).text);
}

C125Seen* c125_seen(const void* turret) {
    const int32_t id = object_index(turret);
    for (uint32_t i = 0; i < g_c125.seen_count; ++i) {
        if (g_c125.seen[i].turret == id) {
            return &g_c125.seen[i];
        }
    }
    if (g_c125.seen_count >= kC125Turrets) {
        return nullptr;
    }
    const void* const root = read_ptr(turret, kActorRootComponent);
    const Vec3 at = actor_location(turret);
    const Vec3 a = c125_rel(turret, kC125AimYawComp);
    const Vec3 b = c125_rel(turret, kC125AimAltComp);
    log::line("card125: %s first seen turret %s at (%.0f, %.0f, %.0f): root yaw %.1f, gun yaw %.1f, +0x770 %s rel "
              "(%.1f, %.1f), +0x768 %s rel (%.1f, %.1f), role %u",
              side(), object_name(turret).text, at.x, at.y, at.z, root != nullptr ? component_yaw(root) : 0.0,
              c29_gun_yaw(turret), object_name(read_ptr(turret, kC125AimYawComp)).text, a.x, a.y,
              object_name(read_ptr(turret, kC125AimAltComp)).text, b.x, b.y,
              static_cast<const uint8_t*>(turret)[kActorLocalRole]);
    g_c125.seen[g_c125.seen_count] = C125Seen{id, false};
    return &g_c125.seen[g_c125.seen_count++];
}

// The turret's own reflected fields (its blueprint class and native supers, up to ASTMountableTurret's parent): name,
// offset, kind, and the value of number and bool fields only. The card 022 dump (c22_own_props) faulted at exe-side
// text of one of these fields in c125p, so this reads no strings or objects.
constexpr int kC125DumpDepth = 3;
constexpr int kC125DumpFields = 160;
void c125_dump(const void* turret) {
    const auto* const t = static_cast<const uint8_t*>(turret);
    const void* s = read_ptr(turret, kObjectClass);
    for (int depth = 0; depth < kC125DumpDepth && s != nullptr; ++depth, s = read_ptr(s, kStructSuper)) {
        const void* f = read_ptr(s, kStructChildProperties);
        for (int i = 0; i < kC125DumpFields && f != nullptr; ++i, f = read_ptr(f, kFieldNext)) {
            ObjectName kind{};
            const PropKind k = prop_kind(f, kind);
            const auto* const prop = static_cast<const uint8_t*>(f);
            const int32_t off = read_i32(prop, kPropertyOffset);
            double v = 0.0;
            if (k == PropKind::Float) {
                v = *reinterpret_cast<const float*>(t + off);
            } else if (k == PropKind::Double) {
                v = *reinterpret_cast<const double*>(t + off);
            } else if (k == PropKind::SInt) {
                v = *reinterpret_cast<const int32_t*>(t + off);
            } else if (k == PropKind::Bool) {
                v = prop_bool_ok(prop) ? ((t[off + prop[kBoolByteOffset]] & prop[kBoolFieldMask]) != 0 ? 1.0 : 0.0) : -1.0;
            }
            log::line("card125: %s field %s.%s +0x%X (%s) = %.3f", side(), object_name(s).text,
                      fname_text(prop + kFieldName).text, off, kind.text, v);
        }
    }
}

// The turret ASC's (+0x680) USTHeatingAttributeSet, found by class name in SpawnedAttributes: CurrentHeat and
// MaximumHeat (FGameplayAttributeData: BaseValue +0x8, CurrentValue +0xC). -1 when there is none.
constexpr int32_t kC125Sets = 16;
constexpr size_t kAttrCurrent = 0xC;
struct C125Heat {
    float current;
    float maximum;
    int32_t sets;
};
// Card 414: the offset is kept per name for the last set class (run c414b108a: 20 to 50 by-name lookups a second on
// the host, each a walk of the class's property chain); another class looks it up again.
struct C125AttrAt {
    const void* cls;
    int32_t offset; // -1: the class has no such property
};
float c125_attr(const void* set, const char* name, C125AttrAt& at) {
    const void* const cls = read_ptr(set, kObjectClass);
    if (at.cls != cls) {
        const uint8_t* const prop = prop_find_prop(cls, name);
        at = C125AttrAt{cls, prop != nullptr ? read_i32(prop, kPropertyOffset) : -1};
    }
    return at.offset >= 0 ? *reinterpret_cast<const float*>(static_cast<const uint8_t*>(set) + at.offset + kAttrCurrent)
                          : -1.0f;
}
// The USTHeatingAttributeSet in an ASC's SpawnedAttributes, or null; *sets gets the set count.
void* c125_heat_in(const void* asc, int32_t* sets) {
    const auto* const list = asc != nullptr ? static_cast<void* const*>(read_ptr(asc, kAscSpawnedAttributes)) : nullptr;
    const int32_t num = asc != nullptr ? read_i32(asc, kAscSpawnedAttributesNum) : 0;
    *sets += num;
    for (int32_t i = 0; list != nullptr && i < num && i < kC125Sets; ++i) {
        if (list[i] != nullptr && strstr(class_name(list[i]).text, "Heating") != nullptr) {
            return list[i];
        }
    }
    return nullptr;
}

// The turret's heat set: on its equipped weapon's ASC (WeaponInventory, the host's weapon), else on its own ASC.
void* c125_heat_set(const void* turret, int32_t* sets) {
    const void* weapon = nullptr;
    void* const set = c125_heat_in(c24_weapon_asc(turret, &weapon), sets);
    return set != nullptr ? set : c125_heat_in(read_ptr(turret, kC125ObjA), sets);
}

C125Heat c125_heat(const void* turret) {
    C125Heat h{-1.0f, -1.0f, 0};
    const void* const set = c125_heat_set(turret, &h.sets);
    if (set != nullptr) {
        static C125AttrAt current{}; // game thread
        static C125AttrAt maximum{};
        h.current = c125_attr(set, "CurrentHeat", current);
        h.maximum = c125_attr(set, "MaximumHeat", maximum);
    }
    return h;
}

void c125_state(const void* turret) {
    if (!note_event_cap(g_c125.state_lines, kC125StateLines, "card125 turret state")) {
        return;
    }
    const void* const root = read_ptr(turret, kActorRootComponent);
    const Vec3 a = c125_rel(turret, kC125AimYawComp);
    const Vec3 b = c125_rel(turret, kC125AimAltComp);
    const auto* const t = static_cast<const uint8_t*>(turret);
    const C125Heat h = c125_heat(turret);
    const void* weapon = nullptr;
    c24_weapon_asc(turret, &weapon);
    log::line("card125: %s t=%llu turret %s at (%.0f, %.0f) in use %u ready %u controller %s role %u: root yaw %.1f, "
              "gun yaw %.1f, +0x770 rel (%.1f, %.1f), +0x768 rel (%.1f, %.1f), +0x8B0 %u +0x8B8 (%.1f, %.1f, %.1f); "
              "look h %u (sum %.2f) v %u (sum %.2f); heat %.2f / %.2f (sets %d); inventory %s, weapon %s",
              side(), static_cast<unsigned long long>(GetTickCount64()), object_name(turret).text,
              actor_location(turret).x, actor_location(turret).y, t[kC125InUse], t[kC125Ready],
              object_name(c29_controller(turret)).text, t[kActorLocalRole], root != nullptr ? component_yaw(root) : 0.0,
              c29_gun_yaw(turret), a.x, a.y, b.x, b.y, t[0x8B0], *reinterpret_cast<const double*>(t + 0x8B8),
              *reinterpret_cast<const double*>(t + 0x8C0), *reinterpret_cast<const double*>(t + 0x8C8), g_c125.look_h,
              g_c125.sum_h, g_c125.look_v, g_c125.sum_v, h.current, h.maximum, h.sets,
              class_name(c24_inventory(turret)).text, class_name(weapon).text);
    // The local gunner's view against the turret camera's world spot (the client view sat in a dark box, c125y).
    const void* const pc = read_ptr(turret, kPawnController);
    const void* const cam = read_ptr(turret, kC125AimYawComp);
    if (pc != nullptr && is_local_pc(pc) && g_player_view_point != nullptr && cam != nullptr) {
        Vec3 loc{};
        Vec3 rot{};
        g_player_view_point(pc, &loc, &rot);
        const Vec3 c = read_vec3(cam, kComponentTranslation);
        log::line("card125: %s turret %s view at (%.0f, %.0f, %.0f) rot (%.1f, %.1f); camera at (%.0f, %.0f, %.0f); "
                  "view target %s", side(), object_name(turret).text, loc.x, loc.y, loc.z, rot.x, rot.y, c.x, c.y, c.z,
                  class_name(read_ptr(pc, kControllerPawn)).text);
    }
}

// Each enemy within kC125BugRange of a turret in use: its health, so a turret's kill shows in the log.
void c125_bugs(const void* world, const void* turret) {
    static C23List enemies;
    c23_collect(world, turret, g_enemy_class(), enemies);
    for (uint32_t i = 0; i < enemies.count; ++i) {
        if (enemies.e[i].dist >= 0.0 && enemies.e[i].dist < kC125BugRange) {
            log::line("card125: %s t=%llu enemy %s near turret at (%.0f, %.0f): dist %.0f health %.1f", side(),
                      static_cast<unsigned long long>(GetTickCount64()), object_name(enemies.e[i].actor).text,
                      actor_location(turret).x, actor_location(turret).y, enemies.e[i].dist,
                      c23_set_health(enemies.e[i].actor));
        }
    }
}

// ---- Sync ----

int32_t c125_tenths(double degrees) {
    const int32_t v = static_cast<int32_t>(lround(degrees * 10.0)) + kC125AimTenths;
    return v < 0 ? 0 : (v > kC125AimMax ? kC125AimMax : v);
}
double c125_degrees(uint32_t tenths) { return (static_cast<double>(tenths) - kC125AimTenths) / 10.0; }

// Packed 'T' value: yaw tenths (11 bits), pitch tenths (11), heat in 255ths of the maximum (8), in use, ready.
uint32_t c125_pack(const void* turret) {
    const Vec3 cam = c125_rel(turret, kC125AimYawComp); // x pitch, y yaw
    const C125Heat h = c125_heat(turret);
    const double frac = h.maximum > 0.0f && h.current >= 0.0f ? h.current / h.maximum : 0.0;
    const uint32_t heat = static_cast<uint32_t>(lround((frac > 1.0 ? 1.0 : frac) * 255.0));
    const auto* const t = static_cast<const uint8_t*>(turret);
    return static_cast<uint32_t>(c125_tenths(cam.y)) | static_cast<uint32_t>(c125_tenths(cam.x)) << 11 | heat << 22 |
           (t[kC125InUse] != 0 ? 1u : 0u) << 30 | (t[kC125Ready] != 0 ? 1u : 0u) << 31;
}

void c125_line(const char* fmt_side, const void* turret, const char* what, uint32_t value) {
    if (note_event_cap(g_c125s.lines, kC125LogLines, "card125 turret sync")) {
        log::line("card125: %s t=%llu turret %s at (%.0f, %.0f): %s yaw %.1f pitch %.1f heat %u/255 in use %u ready %u",
                  fmt_side, static_cast<unsigned long long>(GetTickCount64()), object_name(turret).text,
                  actor_location(turret).x, actor_location(turret).y, what, c125_degrees(value & 0x7FFu),
                  c125_degrees((value >> 11) & 0x7FFu), (value >> 22) & 0xFFu, (value >> 30) & 1u, value >> 31);
    }
}

// Both sides: the turret's camera to (pitch, yaw), and its gun after it as a look input would (look gate open only).
void c125_apply_aim(void* turret, double pitch, double yaw) {
    void* const camera = read_ptr(turret, kC125AimYawComp);
    if (camera == nullptr || g_set_rel_rotation == nullptr || g_turret_look_h == nullptr || g_turret_look_v == nullptr) {
        return;
    }
    const double rot[3] = {pitch, yaw, 0.0};
    g_set_rel_rotation(camera, rot, false, nullptr, 0);
    if (static_cast<const uint8_t*>(turret)[kC125Ready] != 0) {
        g_turret_look_h(static_cast<uint8_t*>(turret) + kC125Recv, 0.0f);
        g_turret_look_v(static_cast<uint8_t*>(turret) + kC125Recv, 0.0f);
    }
}

// Client: our copy of the host's heat (CurrentHeat base and current), when our copy has the set.
void c125_apply_heat(const void* turret, uint32_t heat255) {
    int32_t sets = 0;
    void* const set = c125_heat_set(turret, &sets);
    const uint8_t* const cur = set != nullptr ? prop_find_prop(read_ptr(set, kObjectClass), "CurrentHeat") : nullptr;
    static C125AttrAt maximum_at{}; // game thread
    const float maximum = set != nullptr ? c125_attr(set, "MaximumHeat", maximum_at) : -1.0f;
    if (cur == nullptr || maximum <= 0.0f) {
        return;
    }
    auto* const attr = static_cast<uint8_t*>(set) + read_i32(cur, kPropertyOffset);
    const float v = maximum * static_cast<float>(heat255) / 255.0f;
    memcpy(attr + kAttrCurrent - 4, &v, sizeof(v)); // BaseValue
    memcpy(attr + kAttrCurrent, &v, sizeof(v));     // CurrentValue
}

// Host, every kC125RowMs: a 'T' row for each placed turret whose packed state changed.
void c125_host_rows(void* world) {
    if (world != g_c125s.world) {
        g_c125s = C125Sync{};
        g_c125s.world = world;
    }
    static void* turrets[kC23Max]; // game thread only
    // Card 368: the cached turret list, not a level walk each kC125RowMs.
    const uint32_t count = c368_actors(g_c368_turrets, world, g_turret_class(), turrets);
    for (uint32_t i = 0; i < count; ++i) {
        const void* const turret = turrets[i];
        const int32_t id = object_index(turret);
        C125Row* r = nullptr;
        for (uint32_t k = 0; k < g_c125s.rows && r == nullptr; ++k) {
            r = g_c125s.row[k].turret == id ? &g_c125s.row[k] : nullptr;
        }
        if (r == nullptr && g_c125s.rows < kC125Turrets) {
            r = &g_c125s.row[g_c125s.rows++];
            *r = C125Row{id, ~0u};
        }
        if (r == nullptr && !g_c125s.full_logged) { // card 471: never silent
            g_c125s.full_logged = true;
            log::line("WARN card471: host turret table full (%u): %s at (%.0f, %.0f) gets no rows", kC125Turrets,
                      object_name(turret).text, actor_location(turret).x, actor_location(turret).y);
        }
        const uint32_t value = c125_pack(turret);
        if (r == nullptr || value == r->value || !c98_host_turret(turret, value)) {
            continue;
        }
        r->value = value;
        ++g_c125s.sent_rows;
        c125_line("host", turret, "row sent:", value);
    }
}

// The ubergraph entry point an event function's stub jumps to: its script is "local final call
// ExecuteUbergraph_<class>(int const <entry>)", bytes 46 <UFunction*> 1d <int32>. Returns the ubergraph through *uber.
int32_t c125_event_entry(const void* event_fn, void** uber) {
    const auto* const code = static_cast<const uint8_t*>(read_ptr(event_fn, kStructScript));
    const int32_t n = read_i32(event_fn, kStructScript + 8);
    for (int32_t i = 0; code != nullptr && i + 14 <= n && i < 256; ++i) {
        void* fn = nullptr;
        memcpy(&fn, code + i + 1, sizeof(fn));
        if (code[i] != 0x46 || code[i + 9] != 0x1d || fn == nullptr ||
            strncmp(object_name(fn).text, "ExecuteUbergraph_", 17) != 0) {
            continue;
        }
        int32_t entry = 0;
        memcpy(&entry, code + i + 10, sizeof(entry));
        *uber = fn;
        return entry;
    }
    return -1;
}

// Client: the turret blueprint's ReceivePossessed or ReceiveUnpossessed graph on our copy, as the host's Possess or
// Unpossess raised it there. Both events are BlueprintAuthorityOnly, so ProcessEvent drops them on a client; the
// graph behind them is run instead through ExecuteUbergraph at the event's entry point. That graph sets the hit box
// collision and overheat audio, and opens (or closes) the gate the blueprint tick moves the camera out to its gunner
// offset through: without it the client's view sat inside the turret mesh (c125w7).
void c125_client_possess_graph(void* turret, bool possessed) {
    const uint64_t name = possessed ? fname_cached(g_c125s.possessed_fname, L"ReceivePossessed")
                                    : fname_cached(g_c125s.unpossessed_fname, L"ReceiveUnpossessed");
    const void* const event_fn = name != 0 ? g_find_function(turret, name) : nullptr;
    void* uber = nullptr;
    const int32_t entry = event_fn != nullptr ? c125_event_entry(event_fn, &uber) : -1;
    if (entry < 0 || (read_i32(uber, kFunctionParmsSize) & 0xFFFF) != static_cast<int32_t>(sizeof(int32_t))) {
        ++g_c125s.missed;
        log::line("WARN card125: client turret %s (%s) has no %s graph to run (event %p, ubergraph %p)",
                  object_name(turret).text, class_name(turret).text, possessed ? "possessed" : "unpossessed", event_fn,
                  uber);
        return;
    }
    alignas(16) int32_t parms[4] = {entry, 0, 0, 0};
    g_orig_process_event(turret, uber, parms);
    log::line("card125: client turret %s at (%.0f, %.0f) ran its %s graph (%s entry %d), as the host's %s did",
              object_name(turret).text, actor_location(turret).x, actor_location(turret).y,
              possessed ? "possessed" : "unpossessed", object_name(uber).text, entry,
              possessed ? "Possess" : "Unpossess");
}

// Client: our rider's 1P arms, hidden while we drive a turret and shown again after, as the host's
// AttachCharacterToTurret and DetachCharacterFromTurret do on its copy (SetArmsVisbility is not replicated, so the
// arms filled the client's turret view, c125w11). The character is kept by weak pointer while we drive.
void c125_client_arms(const void* pawn, bool own_turret) {
    if (own_turret) {
        void* const rider = g_c125s.rider_weak != 0 ? g_weak_get(&g_c125s.rider_weak) : nullptr;
        if (g_c125s.arms_hidden || !is_a(rider, g_player_mi_class()) || g_set_arms_visibility == nullptr) {
            return;
        }
        g_set_arms_visibility(rider, false);
        g_c125s.arms_hidden = true;
        log::line("card125: client rider %s arms hidden on its turret, as on the host", object_name(rider).text);
        return;
    }
    if (!is_a(pawn, g_player_mi_class())) {
        return;
    }
    if (g_c125s.arms_hidden && g_set_arms_visibility != nullptr) {
        g_set_arms_visibility(const_cast<void*>(pawn), true);
        log::line("card125: client rider %s arms shown off its turret, as on the host", object_name(pawn).text);
    }
    g_c125s.arms_hidden = false;
    void* object = const_cast<void*>(pawn);
    g_weak_assign(&g_c125s.rider_weak, &object);
}

// Client, every kC125AimMs: the camera aim of the turret we drive, to the host when it changed.
void c125_client_aim(void* world) {
    void* const pc = g_get_player_controller(world, 0);
    const void* const turret = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    const bool own = is_a(turret, g_turret_class()) && sprint_side(turret) == kSideClientOwn;
    c125_client_arms(turret, own);
    if (!own) {
        g_c125s.aim_sent = 0;
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (now - g_c128_host_ping_ms > kC128AimHoldMs) {
        if (g_c125s.aim_held_ms == 0) {
            g_c125s.aim_held_ms = now;
            log::line("card128: client t=%llu TurretAim held: last host Ping %llu ms ago",
                      static_cast<unsigned long long>(now), static_cast<unsigned long long>(now - g_c128_host_ping_ms));
        }
        return; // card 128: host silent; aim_sent is kept, so the newest aim goes on the first tick it is back
    }
    if (g_c125s.aim_held_ms != 0) {
        log::line("card128: client t=%llu TurretAim hold ended after %llu ms: host Ping back",
                  static_cast<unsigned long long>(now), static_cast<unsigned long long>(now - g_c125s.aim_held_ms));
        g_c125s.aim_held_ms = 0;
        g_c125s.aim_after_hold = true;
    }
    const Vec3 cam = c125_rel(turret, kC125AimYawComp);
    ChanArgs args{};
    args.v[0] = static_cast<uint32_t>(c125_tenths(cam.y));
    args.v[1] = static_cast<uint32_t>(c125_tenths(cam.x));
    const uint32_t packed = args.v[0] | args.v[1] << 16;
    if (packed == g_c125s.aim_sent || !chan_send_host(pc, chan_frame(ChanOp::TurretAim, args))) {
        return;
    }
    g_c125s.aim_sent = packed;
    ++g_c125s.sent_aims;
    if (g_c125s.aim_after_hold) {
        g_c125s.aim_after_hold = false;
        log::line("card128: client t=%llu first TurretAim after the hold: yaw %.1f pitch %.1f",
                  static_cast<unsigned long long>(now), c125_degrees(args.v[0]), c125_degrees(args.v[1]));
    }
    c125_line("client", turret, "aim sent:", args.v[0] | args.v[1] << 11);
}

} // namespace

SetWorldRotationFn g_set_rel_rotation = nullptr; // card 125: USceneComponent::SetRelativeRotation
SetBoolFn g_set_arms_visibility = nullptr;       // card 125: ASTPlayerCharacterMI::SetArmsVisbility

void detour_c125_tag_pressed(void* recv, uint64_t tag) {
    c125_tag_line("pressed", recv, tag);
    g_orig_c125_tag_pressed(recv, tag);
}

void detour_c125_tag_released(void* recv, uint64_t tag) {
    c125_tag_line("released", recv, tag);
    g_orig_c125_tag_released(recv, tag);
}

void detour_c125_look_h(void* recv, float value) {
    ++g_c125.look_h;
    g_c125.sum_h += value;
    g_orig_c125_look_h(recv, value);
}

void detour_c125_look_v(void* recv, float value) {
    ++g_c125.look_v;
    g_c125.sum_v += value;
    g_orig_c125_look_v(recv, value);
}

// Host: TurretAim from the client that drives the turret its PC possesses.
void c125_host_aim(void* pc, const WireString*, const ChanArgs& args) {
    void* const turret = read_ptr(pc, kControllerPawn);
    if (!is_a(turret, g_turret_class()) || read_ptr(turret, kPawnController) != pc ||
        args.v[0] > static_cast<uint32_t>(kC125AimMax) || args.v[1] > static_cast<uint32_t>(kC125AimMax)) {
        ++g_c125s.missed;
        log::line("WARN card125: host TurretAim (%u, %u) from %s ignored: its pawn %s (%s) is not a turret it drives",
                  args.v[0], args.v[1], object_name(pc).text, object_name(turret).text, class_name(turret).text);
        return;
    }
    c125_apply_aim(turret, c125_degrees(args.v[1]), c125_degrees(args.v[0]));
    ++g_c125s.applied_aims;
    // Card 129: a gap longer than kC129AimGapMs means this is the first aim after a host stall or a
    // client hold; log it at once so a run shows the aim following the newest client aim.
    const ULONGLONG now = GetTickCount64();
    if (g_c125s.aim_applied_ms != 0 && now - g_c125s.aim_applied_ms > kC129AimGapMs) {
        log::line("card128: host t=%llu TurretAim after a %llu ms gap from %s: yaw %.1f pitch %.1f applied",
                  static_cast<unsigned long long>(now), static_cast<unsigned long long>(now - g_c125s.aim_applied_ms),
                  object_name(pc).text, c125_degrees(args.v[0]), c125_degrees(args.v[1]));
        g_c125s.after_gap_lines = kC128AfterGapLines;
        g_c125s.gap_ms = now;
    } else if (g_c125s.after_gap_lines != 0 && now - g_c125s.gap_ms <= kC129AfterGapMs) {
        --g_c125s.after_gap_lines;
        log::line("card128: host t=%llu TurretAim after the gap #%u: yaw %.1f pitch %.1f applied",
                  static_cast<unsigned long long>(now), kC128AfterGapLines - g_c125s.after_gap_lines,
                  c125_degrees(args.v[0]), c125_degrees(args.v[1]));
    }
    g_c125s.aim_applied_ms = now;
    c125_line("host", turret, "aim applied:", args.v[0] | args.v[1] << 11);
}

// Client: our copy of the turret ticks while the host's does. The host's MountTurret turns the actor tick on and its
// LeaveTurret turns it off (unless bAlwaysTick); both run only on the host, so a client's turret never ticked and its
// blueprint tick never put the camera at its gunner offset (the client's view sat inside the turret mesh, c125w4).
void c125_mirror_tick(uint8_t* turret) {
    if (g_actor_tick_enabled == nullptr) {
        return;
    }
    const bool want = turret[kC125InUse] != 0 || turret[kC125AlwaysTick] != 0;
    if (g_actor_tick_enabled(turret) == want) {
        return;
    }
    using SetTickFn = void (*)(void* actor, bool enabled);
    const auto set_tick = reinterpret_cast<SetTickFn>(read_ptr(read_ptr(turret, 0), kActorSetTickEnabledVfn));
    set_tick(turret, want);
    log::line("card125: client turret %s at (%.0f, %.0f) actor tick %s, as the host's", object_name(turret).text,
              actor_location(turret).x, actor_location(turret).y, want ? "on" : "off");
}

// Card 271: a turret the host spawns at run time (TurretTrooper posts on FED_C3M2) is replicated by the engine; its
// client copy is not placed and has another local name, so the placed-name lookup misses it. Turrets do not move, so
// the nearest non-placed turret within kC271TurretUu of the row's point is that copy. Each match is logged, at most
// kC271Lines per map (senior review, build 4: one logged id rewrote the line on every row of two alternating turrets).
constexpr double kC271TurretUu = 200.0;
constexpr uint32_t kC271Lines = 20;

void* c271_spawned_turret(const void* world, const char* name, const Vec3& at) {
    static C23List list;
    static const void* lines_world = nullptr; // the map the line count belongs to
    static uint32_t lines = 0;
    if (world != lines_world) {
        lines_world = world;
        lines = 0;
    }
    c23_collect(world, nullptr, g_turret_class(), list);
    void* best = nullptr;
    double best_d = kC271TurretUu;
    for (uint32_t i = 0; i < list.count; ++i) {
        const double d = vec_dist(list.e[i].loc, at);
        if (!c50_placed(list.e[i].actor) && d <= best_d) {
            best = list.e[i].actor;
            best_d = d;
        }
    }
    if (best != nullptr && note_event_cap(lines, kC271Lines, "card271 spawned turret matches")) {
        log::line("card271: client turret row %s at (%.0f, %.0f, %.0f) matched spawned turret %s at %.0f uu", name,
                  at.x, at.y, at.z, object_name(best).text, best_d);
    }
    return best;
}

// Client: our copy of the host's turret `name` at `at`, placed by name, else spawned by position (card 271); null
// when no turret copy is here.
uint8_t* c125_find_turret(const void* world, const char* name, const Vec3& at) {
    void* found = c95_find_nearest(world, name, at);
    found = found != nullptr ? found : c271_spawned_turret(world, name, at);
    return is_a(found, g_turret_class()) ? static_cast<uint8_t*>(found) : nullptr;
}

void c125_apply_row(uint8_t* turret, uint32_t value);

// Card 467: a 'T' row whose turret has no copy here yet waits in this table and is retried on the client's turret
// step for kC467WaitMs. c460b170a: after a checkpoint load and rejoin, the join replay's rows for the two spawned
// TurretTrooper turrets ran before their copies arrived, and the one-shot rows were skipped. Newest row per name wins.
constexpr uint32_t kC467Rows = 8;
constexpr ULONGLONG kC467WaitMs = 10000;
constexpr uint32_t kC467Lines = 16; // WARN rows per map (timeouts and table full)

struct C467Row {
    char name[kAssetPathChars]; // the host's name of the turret
    Vec3 at;
    uint32_t value;
    ULONGLONG since_ms; // when the first row for this name was held
};
struct C467Pending {
    C467Row row[kC467Rows];
    uint32_t n;
    uint32_t lines; // WARN rows this map
    uint32_t held, late, timed_out, full; // this map
};
C467Pending g_c467{}; // game thread; cleared by c467_map_end

// Card 467: the index of the held row for `name`, or kC467Rows.
uint32_t c467_find(const char* name) {
    for (uint32_t i = 0; i < g_c467.n && i < kC467Rows; ++i) {
        if (strcmp(g_c467.row[i].name, name) == 0) {
            return i;
        }
    }
    return kC467Rows;
}

void c467_remove(uint32_t i) {
    g_c467.row[i] = g_c467.row[--g_c467.n];
}

// Card 467: holds a row with no turret copy yet; false (WARN) when the table is full.
bool c467_hold(const char* name, const Vec3& at, uint32_t value) {
    uint32_t i = c467_find(name);
    if (i == kC467Rows && g_c467.n < kC467Rows) {
        i = g_c467.n++;
        C467Row& fresh = g_c467.row[i];
        snprintf(fresh.name, sizeof(fresh.name), "%s", name);
        fresh.since_ms = GetTickCount64();
        ++g_c467.held;
    }
    if (i == kC467Rows) {
        ++g_c467.full;
        if (note_event_cap(g_c467.lines, kC467Lines, "card467 turret row warnings")) {
            log::line("WARN card467: client turret row %s at (%.0f, %.0f, %.0f): no turret copy and the pending table "
                      "is full (%u rows), skipped", name, at.x, at.y, at.z, kC467Rows);
        }
        return false;
    }
    g_c467.row[i].at = at;
    g_c467.row[i].value = value;
    return true;
}

// Card 467: client turret step. Each held row whose turret copy is here now is applied; one past kC467WaitMs is
// dropped with a WARN.
void c467_retry(const void* world, ULONGLONG now) {
    uint32_t i = 0;
    for (uint32_t step = 0; step < kC467Rows && i < g_c467.n; ++step) {
        const C467Row& r = g_c467.row[i];
        uint8_t* const turret = c125_find_turret(world, r.name, r.at);
        if (turret != nullptr) {
            c125_apply_row(turret, r.value);
            ++g_c467.late;
            log::line("card467: client turret row %s at (%.0f, %.0f, %.0f) applied %llu ms late", r.name, r.at.x,
                      r.at.y, r.at.z, static_cast<unsigned long long>(now - r.since_ms));
            c467_remove(i);
        } else if (now - r.since_ms >= kC467WaitMs) {
            ++g_c467.timed_out;
            ++g_c125s.missed;
            if (note_event_cap(g_c467.lines, kC467Lines, "card467 turret row warnings")) {
                log::line("WARN card467: client turret row %s at (%.0f, %.0f, %.0f): no turret copy after %llu ms, "
                          "dropped", r.name, r.at.x, r.at.y, r.at.z, static_cast<unsigned long long>(kC467WaitMs));
            }
            c467_remove(i);
        } else {
            ++i;
        }
    }
}

// Client: the host's 'T' row on our copy of that turret (placed by name, else spawned by position, card 271). The aim
// is skipped on the turret we drive. Card 467: a row with no copy yet is held and retried.
void c125_client_turret(void* pc, const char* name, const Vec3& at, uint32_t value) {
    const void* const world = g_actor_get_world(pc);
    uint8_t* const turret = c125_find_turret(world, name, at);
    if (turret == nullptr) {
        const bool held = c467_hold(name, at, value);
        g_c125s.missed += held ? 0u : 1u;
        c132_ledger_result(held ? C132Result::Pending : C132Result::Skipped,
                           held ? "no turret copy yet: held (card 467)" : "no turret copy, card 467 table full");
        return;
    }
    const uint32_t held = c467_find(name);
    if (held != kC467Rows) {
        c467_remove(held); // this newer row replaces the held one
    }
    c125_apply_row(turret, value);
}

// The host's 'T' row value on our copy `turret`.
void c125_apply_row(uint8_t* turret, uint32_t value) {
    const uint8_t was_used = turret[kC125InUse];
    turret[kC125InUse] = static_cast<uint8_t>((value >> 30) & 1u);
    turret[kC125Ready] = static_cast<uint8_t>(value >> 31);
    c125_mirror_tick(turret);
    if (turret[kC125InUse] != was_used) {
        c125_client_possess_graph(turret, turret[kC125InUse] != 0);
    }
    const bool own = sprint_side(turret) == kSideClientOwn;
    if (!own) {
        c125_apply_aim(turret, c125_degrees((value >> 11) & 0x7FFu), c125_degrees(value & 0x7FFu));
    }
    c125_apply_heat(turret, (value >> 22) & 0xFFu);
    ++g_c125s.applied_rows;
    c125_line("client", turret, own ? "row applied (own turret, aim kept):" : "row applied:", value);
}

// Card 467: map end (the world relay's): held rows belong to the old map.
void c467_map_end() {
    const C467Pending& p = g_c467;
    if (p.held != 0 || p.full != 0) {
        log::line("%scard467: %s turret rows map totals: %u held, %u applied late, %u timed out, %u table full, %u "
                  "still held", p.timed_out == 0 && p.full == 0 ? "" : "WARN ", side(), p.held, p.late, p.timed_out,
                  p.full, p.n);
    }
    memset(&g_c467, 0, sizeof(g_c467));
}

void c125_step(void* engine) {
    const ULONGLONG now = GetTickCount64();
    void* const sync_world = now >= g_c125s.next_row_ms ? g_get_game_world(engine) : nullptr;
    if (sync_world != nullptr && !g_client_session) {
        g_c125s.next_row_ms = now + kC125RowMs;
        c125_host_rows(sync_world);
    } else if (sync_world != nullptr) {
        g_c125s.next_row_ms = now + kC125AimMs;
        c125_client_aim(sync_world);
        c467_retry(sync_world, now); // card 467
    }
    if (!g_c125_log) {
        return;
    }
    void* const world = now >= g_c125.next_poll_ms ? g_get_game_world(engine) : nullptr;
    if (world == nullptr) {
        return;
    }
    g_c125.next_poll_ms = now + kC125PollMs;
    static C23List list;
    c23_collect(world, nullptr, g_turret_class(), list);
    for (uint32_t i = 0; i < list.count; ++i) {
        const void* const turret = list.e[i].actor;
        C125Seen* const seen = c125_seen(turret);
        const bool used = static_cast<const uint8_t*>(turret)[kC125InUse] != 0 || c29_controller(turret) != nullptr ||
                          read_ptr(turret, kPawnController) != nullptr;
        if (seen == nullptr || !used) {
            continue;
        }
        if (!seen->dumped) {
            seen->dumped = true;
            c125_dump(turret);
        }
        c125_state(turret);
        c125_bugs(world, turret);
    }
    g_c125.look_h = 0;
    g_c125.look_v = 0;
    g_c125.sum_h = 0.0;
    g_c125.sum_v = 0.0;
}

// Card 471: host, from the DestroyActor detour. A destroyed turret's row slot and its 'T' journal row go. Before: the
// join replay after a checkpoint load named a turret the load had destroyed (c460b172b `WARN card467 ... no turret
// copy after 10000 ms`), and each dead spawned turret kept one of the kC125Turrets slots until the map ended.
void c125_host_forget(const void* actor) {
    const int32_t id = object_index(actor);
    for (uint32_t k = 0; k < g_c125s.rows; ++k) {
        if (g_c125s.row[k].turret == id) {
            g_c125s.row[k] = g_c125s.row[--g_c125s.rows];
            c98_forget_turret(actor);
            log::line("card471: host t=%llu turret %s destroyed: row slot and journal row dropped, %u slots in use",
                      static_cast<unsigned long long>(GetTickCount64()), object_name(actor).text, g_c125s.rows);
            return;
        }
    }
}

} // namespace ubw::hooks
