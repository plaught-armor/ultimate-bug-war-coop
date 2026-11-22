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
// Card 561: a seat whose blueprint tick flies it along a "Spline" actor variable that only the host's spawn sets (the
// Door Gunner gunship, BP_MountableTurret_Gunner_C, from GA_TacSupp_DoorGunner's deferred spawn). The client copy has
// it null, so its tick ran the flight on no spline and moved the seat, and the client's view, to the world origin
// every frame. Such a copy never ticks here; its place comes from the host.
static bool c561_spline_missing(const uint8_t* turret) {
    const uint8_t* prop = nullptr;
    const void* const spline = prop_object_field(turret, "Spline", &prop);
    return prop != nullptr && spline == nullptr;
}

void c125_mirror_tick(uint8_t* turret) {
    if (g_actor_tick_enabled == nullptr) {
        return;
    }
    const bool in_use = turret[kC125InUse] != 0 || turret[kC125AlwaysTick] != 0;
    const bool no_spline = in_use && c561_spline_missing(turret);
    const bool want = in_use && !no_spline;
    if (no_spline && g_actor_tick_enabled(turret)) {
        log::line("card561: client turret %s at (%.0f, %.0f) keeps its actor tick off: its Spline is the host's "
                  "only (null here)", object_name(turret).text, actor_location(turret).x, actor_location(turret).y);
    }
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
// c578b425a: a TurretTrooper drop 15661 uu from the client's pawn, past net cull distance, never got a copy there; its
// row was dropped with a WARN at 10 s. A row that times out past kC467CullUu from the own pawn is an out-of-range hold
// (a plain row, no time cap): applied when the copy appears, dropped by the client destroy path or at map end. Back
// inside cull distance it gets a new kC467WaitMs window; a timeout there is a real missing copy (WARN).
constexpr uint32_t kC467Rows = 16;
constexpr ULONGLONG kC467WaitMs = 10000;
constexpr double kC467CullUu = 15000.0; // the engine's default net cull distance
constexpr uint32_t kC467Lines = 16;     // WARN rows per map (timeouts and table full)
constexpr uint32_t kC467FarLines = 8;   // out-of-range hold rows per map

struct C467Row {
    char name[kAssetPathChars]; // the host's name of the turret
    Vec3 at;
    uint32_t value;
    ULONGLONG since_ms; // when the first row for this name was held (or came back inside cull distance)
    bool out_of_range;  // an out-of-range hold: no time cap
};
// c587b422b: the host's last row for a Door Gunner seat (in use 0 at unpossess) is sent 7 ms before the seat is
// destroyed; the client's copy was gone when the row landed, so the row was held 10 s and dropped with a WARN. A
// spawned copy (card 271, not placed) that a row was applied to is remembered by the host's name; when it is
// destroyed here, a row for that name is dropped at once with a plain row: its turret no longer exists.
constexpr uint32_t kC467Seen = 16;
struct C467Seen {
    char name[kAssetPathChars]; // the host's name of the turret
    int32_t copy;               // our copy's object index; -1 once destroyed
    ULONGLONG gone_ms;          // when our copy was destroyed, else 0
};
struct C467Pending {
    C467Row row[kC467Rows];
    uint32_t n;
    uint32_t lines; // WARN rows this map
    uint32_t held, late, timed_out, full, gone; // this map
    uint32_t far_held, far_applied;            // this map: out-of-range holds, and of those applied later
    uint32_t far_lines;                        // out-of-range rows logged this map
    C467Seen seen[kC467Seen];
    uint32_t seen_n, seen_next; // used entries, the next one to overwrite once full
    uint32_t gone_lines;        // gone rows logged this map
};
C467Pending g_c467{}; // game thread; cleared by c467_map_end
constexpr uint32_t kC467GoneLines = 8;

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

// Card 467: the seen entry for host name `name`, or kC467Seen.
uint32_t c467_seen_find(const char* name) {
    for (uint32_t i = 0; i < g_c467.seen_n && i < kC467Seen; ++i) {
        if (strcmp(g_c467.seen[i].name, name) == 0) {
            return i;
        }
    }
    return kC467Seen;
}

// Card 467: client, a row was applied to `turret`, the copy of the host's `name`. Spawned copies only (placed ones
// are not destroyed in play); the oldest entry goes once the table is full.
void c467_seen_note(const uint8_t* turret, const char* name) {
    if (c50_placed(turret)) {
        return;
    }
    uint32_t i = c467_seen_find(name);
    if (i != kC467Seen) { // a copy that came back (net relevancy) after an earlier one went
        g_c467.seen[i].copy = object_index(turret);
        g_c467.seen[i].gone_ms = 0;
        return;
    }
    i = g_c467.seen_n;
    if (i < kC467Seen) {
        ++g_c467.seen_n;
    } else {
        i = g_c467.seen_next;
        g_c467.seen_next = (g_c467.seen_next + 1) % kC467Seen;
    }
    C467Seen& e = g_c467.seen[i];
    snprintf(e.name, sizeof(e.name), "%s", name);
    e.copy = object_index(turret);
    e.gone_ms = 0;
}

// Card 467: true (and a plain row) when `name`'s copy was destroyed here: the row has no turret to apply to.
bool c467_gone(const char* name) {
    const uint32_t i = c467_seen_find(name);
    if (i == kC467Seen || g_c467.seen[i].gone_ms == 0) {
        return false;
    }
    ++g_c467.gone;
    if (note_event_cap(g_c467.gone_lines, kC467GoneLines, "card467 destroyed turret rows")) {
        log::line("card467: client turret row %s: its copy was destroyed %llu ms before, row dropped", name,
                  static_cast<unsigned long long>(GetTickCount64() - g_c467.seen[i].gone_ms));
    }
    return true;
}

// Card 467: client, from the DestroyActor detour. A remembered copy going marks its host name gone, and a row held
// for that name is dropped.
void c467_client_forget(const void* actor) {
    const int32_t id = object_index(actor);
    for (uint32_t i = 0; i < g_c467.seen_n && i < kC467Seen; ++i) {
        C467Seen& e = g_c467.seen[i];
        if (e.copy != id) {
            continue;
        }
        e.copy = -1;
        e.gone_ms = GetTickCount64();
        const uint32_t held = c467_find(e.name);
        if (held != kC467Rows) {
            c467_remove(held);
            (void)c467_gone(e.name);
        }
        return;
    }
}

// Card 467: the index of the held row with the earliest since_ms (the table is not empty).
uint32_t c467_oldest() {
    uint32_t oldest = 0;
    for (uint32_t i = 1; i < g_c467.n && i < kC467Rows; ++i) {
        oldest = g_c467.row[i].since_ms < g_c467.row[oldest].since_ms ? i : oldest;
    }
    return oldest;
}

// Card 467: holds a row with no turret copy yet. A newer row for a held name replaces its state; a full table drops
// its oldest row (WARN) for the new one.
void c467_hold(const char* name, const Vec3& at, uint32_t value) {
    uint32_t i = c467_find(name);
    if (i == kC467Rows && g_c467.n >= kC467Rows) {
        const C467Row& old = g_c467.row[c467_oldest()];
        ++g_c467.full;
        if (note_event_cap(g_c467.lines, kC467Lines, "card467 turret row warnings")) {
            log::line("WARN card467: client turret pending table full (%u rows): oldest row %s%s at (%.0f, %.0f, %.0f) "
                      "dropped for %s", kC467Rows, old.name, old.out_of_range ? " (out of range)" : "", old.at.x, old.at.y,
                      old.at.z, name);
        }
        c467_remove(c467_oldest());
    }
    if (i == kC467Rows) {
        i = g_c467.n++;
        C467Row& fresh = g_c467.row[i];
        snprintf(fresh.name, sizeof(fresh.name), "%s", name);
        fresh.since_ms = GetTickCount64();
        fresh.out_of_range = false;
        ++g_c467.held;
    }
    g_c467.row[i].at = at;
    g_c467.row[i].value = value;
}

// Card 467: distance from the client's own pawn (any pawn, a mounted turret too) to `at`; negative with no pawn.
double c467_own_dist(const void* world, const Vec3& at) {
    void* const pc = own_pc(world);
    const void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    return pawn != nullptr ? vec_dist(actor_location(pawn), at) : -1.0;
}

// Card 467: a held row past its wait. Out of cull distance: an out-of-range hold (kept, true); an out-of-range hold
// with no own pawn stays. Inside it, or a first timeout with no own pawn: the WARN and the drop (false).
bool c467_keep_far(C467Row& r, double dist, ULONGLONG now) {
    if (dist > kC467CullUu) {
        if (!r.out_of_range) {
            r.out_of_range = true;
            ++g_c467.far_held;
            if (note_event_cap(g_c467.far_lines, kC467FarLines, "card467 out-of-range holds")) {
                log::line("card467: client turret row %s at (%.0f, %.0f, %.0f): no copy after %llu ms, %.0f uu from "
                          "the own pawn (net cull distance %.0f): out-of-range hold", r.name, r.at.x, r.at.y, r.at.z,
                          static_cast<unsigned long long>(now - r.since_ms), dist, kC467CullUu);
            }
        }
        return true;
    }
    if (r.out_of_range && dist < 0.0) { // no own pawn (dead, respawning): stays held until one can measure it
        return true;
    }
    if (r.out_of_range) { // back inside cull distance: the copy should arrive; a new wait window
        r.out_of_range = false;
        r.since_ms = now;
        return true;
    }
    ++g_c467.timed_out;
    ++g_c125s.missed;
    if (note_event_cap(g_c467.lines, kC467Lines, "card467 turret row warnings")) {
        log::line("WARN card467: client turret row %s at (%.0f, %.0f, %.0f): no turret copy after %llu ms, %.0f uu "
                  "from the own pawn, dropped", r.name, r.at.x, r.at.y, r.at.z,
                  static_cast<unsigned long long>(kC467WaitMs), dist);
    }
    return false;
}

// Card 467: client turret step. Each held row whose turret copy is here now is applied; one past kC467WaitMs is an
// out-of-range hold (c467_keep_far) or dropped with a WARN. Out-of-range holds are measured every step.
void c467_retry(const void* world, ULONGLONG now) {
    uint32_t i = 0;
    for (uint32_t step = 0; step < kC467Rows && i < g_c467.n; ++step) {
        C467Row& r = g_c467.row[i];
        uint8_t* const turret = c125_find_turret(world, r.name, r.at);
        if (turret != nullptr) {
            c125_apply_row(turret, r.value);
            ++g_c467.late;
            g_c467.far_applied += r.out_of_range ? 1u : 0u;
            log::line("card467: client turret row %s at (%.0f, %.0f, %.0f) applied %llu ms late%s", r.name, r.at.x,
                      r.at.y, r.at.z, static_cast<unsigned long long>(now - r.since_ms),
                      r.out_of_range ? " (out-of-range hold)" : "");
            c467_seen_note(turret, r.name);
            c467_remove(i);
        } else if ((r.out_of_range || now - r.since_ms >= kC467WaitMs) && !c467_keep_far(r, c467_own_dist(world, r.at), now)) {
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
    if (turret == nullptr && c467_gone(name)) {
        c132_ledger_result(C132Result::Skipped, "turret copy destroyed (card 467)");
        return;
    }
    if (turret == nullptr) {
        c467_hold(name, at, value);
        c132_ledger_result(C132Result::Pending, "no turret copy yet: held (card 467)");
        return;
    }
    const uint32_t held = c467_find(name);
    if (held != kC467Rows) {
        c467_remove(held); // this newer row replaces the held one
    }
    c467_seen_note(turret, name);
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
    if (p.held != 0 || p.full != 0 || p.gone != 0) {
        uint32_t far_now = 0;
        for (uint32_t i = 0; i < p.n && i < kC467Rows; ++i) {
            far_now += p.row[i].out_of_range ? 1u : 0u;
        }
        log::line("%scard467: %s turret rows map totals: %u held, %u applied late, %u timed out, %u table full, %u "
                  "still held, %u for destroyed copies; held out of range %u, applied later %u, still held %u",
                  p.timed_out == 0 && p.full == 0 ? "" : "WARN ", side(), p.held, p.late, p.timed_out, p.full, p.n,
                  p.gone, p.far_held, p.far_applied, far_now);
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


// ---- Card 561: a pawn blueprint's camera fade reaches only the screen of the pawn's own player ----
// The Door Gunner seat fades the screen through GetPlayerCameraManager(self, 0): black at its BeginPlay, fade-in at
// its possess. Index 0 is this machine's own player, so on the host a seat the client called blacked out the host's
// screen (playtest 2026-11-20: "they got a black screen"), and the host's own seat does the same on the client's
// copy. Class rule: a fade called from a pawn's graph runs only when that pawn's controller is this machine's own
// player controller; a pawn with no controller or a remote or AI one fades nothing here. The exec thunk still runs
// (it steps the Blueprint frame's parameters); only the native fade it calls is skipped.
struct C561State {
    const void* skip; // the pawn whose exec call is running and is not this machine's player's, else null
    void* forward;    // host: the remote player controller that pawn's fade belongs to, else null (b408)
    uint32_t lines;
    uint32_t forwarded; // host: fades sent to their client (b408)
    uint32_t applied;   // client: host fades run on the own camera manager (b408)
    uint32_t fade_lines; // forwarded or applied rows logged (b408)
};
static C561State g_c561{nullptr, nullptr, 0, 0, 0, 0}; // game thread
constexpr uint32_t kC561Lines = 24;   // skipped-fade rows per run
constexpr uint32_t kC561FadeLines = 16; // forwarded (host) or applied (client) rows per run
C561ExecFn g_orig_c561_exec_start_fade = nullptr;
C561ExecFn g_orig_c561_exec_manual_fade = nullptr;
C561StartFadeFn g_orig_c561_start_fade = nullptr;
C561ManualFadeFn g_orig_c561_manual_fade = nullptr;

// Card 561 b408: the player controller `who` stands for: `who` itself when it is a player controller, else the
// player controller driving `who` when it is a pawn; null for an AI, no controller, or another class.
static void* c561_pc_of(const void* who) {
    if (who == nullptr) {
        return nullptr;
    }
    if (is_a(who, g_player_controller_class())) {
        return const_cast<void*>(who);
    }
    if (!is_a(who, g_pawn_class())) {
        return nullptr;
    }
    void* const ctrl = read_ptr(who, kPawnController);
    return ctrl != nullptr && is_a(ctrl, g_player_controller_class()) ? ctrl : nullptr;
}

struct C561Verdict {
    const void* skip; // the calling pawn when its fade is not this machine's player's, else null
    void* forward;    // host: the connected client whose own pawn it is, else null
};

// The player controller an unpossessed pawn's fade belongs to.
// Card 561 b404: a pawn not possessed yet belongs to the player that spawned it. c372b402a: the host's own Door Gunner
// seat calls its BeginPlay black fade 1 ms after its spawn (GA_TacSupp_DoorGunner on the host-own pawn), before its
// possess, and b402 skipped it. b406: c372b404a showed the seat's Instigator is the seat itself and its Owner is null
// (the ability spawns it with no owner). The ability sets the seat's exposed `Target` variable to the caller's trooper
// before FinishSpawningActor, so `Target` names the player; Instigator, else Owner, stays the rule for a pawn without
// it. A client's seat on the host names the client's server copy (a remote controller), so it is still skipped there.
// c372b407a: `Target` is not replicated (net 0), so on the client it is null and the client's own seat is skipped
// there too; the host forwards that fade instead (b408).
static void* c561_spawner_pc(const void* caller) {
    const uint8_t* prop = nullptr;
    const void* const target = prop_object_field(caller, "Target", &prop);
    const void* const instigator = read_ptr(caller, kActorInstigator);
    const void* const who =
        target != nullptr ? target : (instigator != nullptr && instigator != caller ? instigator : read_ptr(caller, kActorOwner));
    void* const pc = c561_pc_of(who);
    if (note_event_cap(g_c561.lines, kC561Lines, "card561 skipped fade")) {
        log::line("card561: %s t=%llu unpossessed %s fade: Target %s (property %s, net %d), player %s, local %d",
                  side(), static_cast<unsigned long long>(GetTickCount64()), object_name(caller).text,
                  object_name(target).text, prop != nullptr ? "found" : "absent",
                  prop != nullptr && (read_u64(prop, kPropertyFlags) & kCpfNet) != 0 ? 1 : 0, object_name(who).text,
                  pc != nullptr && is_local_pc(pc) ? 1 : 0);
    }
    return pc;
}

// Card 561 b408: the ReceivePossessed fade-in of the Door Gunner seat runs only on the host (PossessedBy is
// authority-only), so a client's own seat fade never ran on the client: the host skipped it on its own screen and the
// client never called it. The host now sends each fade it skips for a connected client's own pawn to that client.
static C561Verdict c561_foreign_pawn(const void* frame) {
    const void* const caller = frame != nullptr ? read_ptr(frame, kFrameObject) : nullptr;
    if (caller == nullptr || !is_a(caller, g_pawn_class())) {
        return C561Verdict{nullptr, nullptr};
    }
    const void* const ctrl = read_ptr(caller, kPawnController);
    // Card 561 b402: the transport dropship has no controller and fades every rider's screen at the drop (both
    // players ride it), so its fades still run here; b371 skipped its 25 landing fades on the host. The rider may
    // already be detached when the fade runs, so the class is checked, not the attachment.
    if (ctrl == nullptr && is_a(caller, g_transport_dropship_class())) {
        return C561Verdict{nullptr, nullptr};
    }
    void* const pc = ctrl != nullptr ? c561_pc_of(ctrl) : c561_spawner_pc(caller);
    if (pc != nullptr && is_local_pc(pc)) {
        return C561Verdict{nullptr, nullptr};
    }
    void* const forward = !g_client_session && pc != nullptr && c26_is_remote_pc(pc) ? pc : nullptr;
    return C561Verdict{caller, forward};
}

static void c561_exec(C561ExecFn orig, void* context, void* frame, void* result) {
    const void* const outer = g_c561.skip;
    void* const outer_forward = g_c561.forward;
    const C561Verdict v = c561_foreign_pawn(frame);
    g_c561.skip = v.skip;
    g_c561.forward = v.forward;
    orig(context, frame, result);
    g_c561.skip = outer;
    g_c561.forward = outer_forward;
}

static bool c561_skipped(const char* fn) {
    if (g_c561.skip == nullptr) {
        return false;
    }
    if (note_event_cap(g_c561.lines, kC561Lines, "card561 skipped fade")) {
        const void* const ctrl = read_ptr(g_c561.skip, kPawnController);
        log::line("card561: %s t=%llu skipped %s from %s (controller %s, instigator %s, owner %s): not this machine's "
                  "player's pawn", side(), static_cast<unsigned long long>(GetTickCount64()), fn,
                  object_name(g_c561.skip).text, object_name(ctrl).text,
                  object_name(read_ptr(g_c561.skip, kActorInstigator)).text,
                  object_name(read_ptr(g_c561.skip, kActorOwner)).text);
    }
    return true;
}

// Card 561 b408: the CamFade blob. Byte 0 is the kind, byte 1 the flags, then from (or the manual amount), to,
// duration, and the color's r, g, b, a as floats.
enum C561Kind : uint8_t { kC561Start, kC561Manual, kC561Kinds };
const char* const kC561KindText[kC561Kinds] = {"StartCameraFade", "SetManualCameraFade"};
constexpr uint8_t kC561Audio = 1;
constexpr uint8_t kC561Hold = 2;
constexpr int32_t kC561Floats = 7;
constexpr int32_t kC561FadeBytes = 2 + kC561Floats * 4;
constexpr float kC561MaxDuration = 60.0f;

struct C561Fade {
    uint8_t kind;
    uint8_t flags;
    float v[kC561Floats]; // from, to, duration, r, g, b, a
};

static void c561_forward(const C561Fade& f) {
    if (g_c561.forward == nullptr) {
        return;
    }
    uint8_t bytes[kC561FadeBytes];
    bytes[0] = f.kind;
    bytes[1] = f.flags;
    memcpy(bytes + 2, f.v, sizeof(f.v));
    static wchar_t wire[kChanBlobFrame]; // game thread only
    const WireString w = chan_blob_frame(static_cast<wchar_t>(ChanOp::CamFade), bytes, kC561FadeBytes, wire);
    const bool sent = w.num > 0 && chan_send_client(g_c561.forward, w);
    ++g_c561.forwarded;
    if (!sent) {
        log::line("WARN card561: host CamFade %s from %s not sent to %s", kC561KindText[f.kind],
                  object_name(g_c561.skip).text, object_name(g_c561.forward).text);
    } else if (note_event_cap(g_c561.fade_lines, kC561FadeLines, "card561 forwarded fade")) {
        log::line("card561: host t=%llu forwarded %s (%.2f, %.2f, %.2f s) from %s to %s, %u forwarded",
                  static_cast<unsigned long long>(GetTickCount64()), kC561KindText[f.kind], static_cast<double>(f.v[0]),
                  static_cast<double>(f.v[1]), static_cast<double>(f.v[2]), object_name(g_c561.skip).text,
                  object_name(g_c561.forward).text, g_c561.forwarded);
    }
}

static C561Fade c561_fade(uint8_t kind, float from, float to, float duration, const void* color, bool audio, bool hold) {
    C561Fade f{kind, static_cast<uint8_t>((audio ? kC561Audio : 0) | (hold ? kC561Hold : 0)), {from, to, duration}};
    if (color != nullptr) {
        memcpy(f.v + 3, color, 4 * sizeof(float));
    }
    return f;
}

void detour_c561_exec_start_fade(void* context, void* frame, void* result) {
    c561_exec(g_orig_c561_exec_start_fade, context, frame, result);
}

void detour_c561_exec_manual_fade(void* context, void* frame, void* result) {
    c561_exec(g_orig_c561_exec_manual_fade, context, frame, result);
}

void detour_c561_start_fade(void* pcm, float from, float to, float duration, const void* color, bool audio, bool hold) {
    if (!c561_skipped("StartCameraFade")) {
        g_orig_c561_start_fade(pcm, from, to, duration, color, audio, hold);
        return;
    }
    c561_forward(c561_fade(kC561Start, from, to, duration, color, audio, hold));
}

void detour_c561_manual_fade(void* pcm, float amount, const void* color, bool audio) {
    if (!c561_skipped("SetManualCameraFade")) {
        g_orig_c561_manual_fade(pcm, amount, color, audio);
        return;
    }
    c561_forward(c561_fade(kC561Manual, amount, 0.0f, 0.0f, color, audio, false));
}

// Negated compares: a NaN fails every one of them.
static bool c561_fade_valid(const C561Fade& f) {
    if (f.kind >= kC561Kinds || (f.flags & ~(kC561Audio | kC561Hold)) != 0) {
        return false;
    }
    for (int32_t k = 0; k < kC561Floats; ++k) {
        const float lim = k == 2 ? kC561MaxDuration : 1.0f;
        if (!(f.v[k] >= 0.0f && f.v[k] <= lim)) {
            return false;
        }
    }
    return true;
}

// Client: ChanOp::CamFade from the host, a fade of this client's own pawn; runs on the own camera manager.
void c561_receive(void* pc, const WireString* s) {
    if (!g_client_session) {
        return;
    }
    static uint8_t bytes[kChanBlobBytes]; // game thread only
    const int32_t n = chan_blob(s, bytes, kChanBlobBytes);
    C561Fade f{};
    if (n == kC561FadeBytes) {
        f.kind = bytes[0];
        f.flags = bytes[1];
        memcpy(f.v, bytes + 2, sizeof(f.v));
    }
    void* const pcm = pc != nullptr ? read_ptr(pc, kPcCameraManager) : nullptr;
    if (n != kC561FadeBytes || !c561_fade_valid(f) || pcm == nullptr || g_orig_c561_start_fade == nullptr ||
        g_orig_c561_manual_fade == nullptr) {
        log::line("WARN card561: client CamFade dropped (%d bytes, kind %u, camera manager %s)", n,
                  static_cast<unsigned>(f.kind), pcm != nullptr ? "found" : "null");
        return;
    }
    const bool audio = (f.flags & kC561Audio) != 0;
    if (f.kind == kC561Start) {
        g_orig_c561_start_fade(pcm, f.v[0], f.v[1], f.v[2], f.v + 3, audio, (f.flags & kC561Hold) != 0);
    } else {
        g_orig_c561_manual_fade(pcm, f.v[0], f.v + 3, audio);
    }
    ++g_c561.applied;
    if (note_event_cap(g_c561.fade_lines, kC561FadeLines, "card561 applied fade")) {
        log::line("card561: client t=%llu applied host %s (%.2f, %.2f, %.2f s), %u applied",
                  static_cast<unsigned long long>(GetTickCount64()), kC561KindText[f.kind], static_cast<double>(f.v[0]),
                  static_cast<double>(f.v[1]), static_cast<double>(f.v[2]), g_c561.applied);
    }
}

} // namespace ubw::hooks
