// Vehicles: the weapon, mech and turret audit and test driver (card 029), the mech spawn and enter audit
// and host driver (card 032), co-op mechs (card 075) and a client pilot's mech input contexts
// (card 076).

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

void c32_mech_event(const char* what, const char* when, const void* mech, const void* pawn, unsigned long long caller);

// ---- Card 076: a client pilot's mech input contexts ----
// ASTPlayerMechBase::AddMechInputContext (0x050DB280) and RemoveMechInputContext (0x050FE920) run on the server. For a
// client's mech the server has no local controller, so card 005's guard skips them, and the client pilot keeps the
// soldier contexts on top: RMB stays InputTag.ADS instead of InputTag.MechSecondaryAbility. The client does what those
// natives do for its own pilot: on entering a mech, every context of the mech's input config (mech+0xE60) is added at
// priority 3 with IgnoreAllPressedKeysUntilRelease (the natives' (x & 0xF9) | 1); on leaving, the same contexts are
// removed. The contexts are kept here (the mech may be gone when its pilot leaves) and dropped on every map.
constexpr size_t kMechInputConfig = 0xE60;     // TSoftObjectPtr<UPlayerMappableInputConfig>

constexpr uint8_t kOptionsIgnorePressed = 0x1; // IgnoreAllPressedKeysUntilRelease
constexpr size_t kIfaceRemoveMappingContext = 0xA0 / sizeof(void*);

C76Imc g_c76_imc{-1, {}, 0};

void c76_imc_add(void* iface, const void* mech) {
    SparseView v{};
    if (!load_config_contexts(static_cast<const uint8_t*>(mech) + kMechInputConfig, "mech input config", v)) {
        return;
    }
    const auto add = reinterpret_cast<AddMappingContextFn>((*static_cast<void* const* const*>(iface))[kIfaceAddMappingContext]);
    const uint8_t options = kOptionsIgnorePressed;
    for (int32_t i = 0; i < v.count && g_c76_imc.count < kC76Contexts; ++i) {
        int32_t priority = 0;
        const void* const context = sparse_allocated(v, i) ? config_context(v, i, &priority) : nullptr;
        if (context != nullptr) {
            add(iface, context, kMechContextPriority, &options);
            g_c76_imc.context[g_c76_imc.count++] = context;
        }
    }
}

void c76_imc_remove(void* iface) {
    const auto remove =
        reinterpret_cast<RemoveMappingContextFn>((*static_cast<void* const* const*>(iface))[kIfaceRemoveMappingContext]);
    const uint8_t options = kOptionsIgnorePressed;
    for (int32_t i = 0; i < g_c76_imc.count; ++i) {
        remove(iface, g_c76_imc.context[i], &options);
    }
    g_c76_imc.count = 0;
}

StaticClassFn g_mech_class = nullptr;   // ASTPlayerMechBase
StaticClassFn g_turret_class = nullptr; // ASTMountableTurret
C29EquipFn g_equip_weapon = nullptr;    // ASTPlayerCharacter::EquipWeapon

C29RemoveFn g_remove_weapon = nullptr; // UADWeaponInventory::RemoveWeaponByInstance
C29SelfFn g_mech_request_leave = nullptr;
C29InteractFn g_mech_on_interaction = nullptr;
C29InteractFn g_turret_on_interaction = nullptr;
C29LookFn g_turret_look_h = nullptr;
C29LookFn g_turret_look_v = nullptr;
C29EnterFn g_orig_enter_mech = nullptr;
C29SelfFn g_orig_c255_start_boarding = nullptr;
C29SelfFn g_orig_leave_mech = nullptr;
C29EnterFn g_orig_mount_turret = nullptr;
C29SelfFn g_orig_leave_turret = nullptr;
bool g_c29_audit = false; // UBWCOOP_AUDIT_029; written in ubw::init before any hook

int32_t c29_offset_of(const void* ustruct, const char* name, const char* kind) {
    const uint8_t* const p = ustruct != nullptr ? prop_find_prop(ustruct, name) : nullptr;
    return prop_kind_is(p, kind) ? read_i32(p, kPropertyOffset) : -1;
}

bool c29_layout(const void* asset, C29Layout& L) {
    const uint8_t* const anims = prop_find_prop(read_ptr(asset, kObjectClass), "Animations");
    if (!prop_kind_is(anims, "MapProperty") || !c27_map_ok(anims)) {
        return false;
    }
    const auto* const value = static_cast<const uint8_t*>(read_ptr(anims, kMapPropValue));
    const void* const seq = prop_kind_is(value, "StructProperty") ? read_ptr(value, kStructPropStruct) : nullptr;
    const uint8_t* const frames = seq != nullptr ? prop_find_prop(seq, "Frames") : nullptr;
    const auto* const inner =
        prop_kind_is(frames, "ArrayProperty") ? static_cast<const uint8_t*>(read_ptr(frames, kArrayPropInner)) : nullptr;
    const void* const frame = prop_kind_is(inner, "StructProperty") ? read_ptr(inner, kStructPropStruct) : nullptr;
    if (frame == nullptr) {
        return false;
    }
    const uint8_t* const flag = prop_find_prop(seq, "bUseUniformFrameTime");
    L = C29Layout{read_i32(anims, kPropertyOffset), read_i32(anims, kMapValueOffset), read_i32(anims, kMapSparseSize),
                  read_i32(frames, kPropertyOffset), read_i32(inner, kPropertyElementSize),
                  c29_offset_of(frame, "TimeToDisplay", "FloatProperty"),
                  c29_offset_of(frame, "Materials", "ArrayProperty"),
                  c29_offset_of(seq, "UniformFrameTime", "FloatProperty"),
                  prop_kind_is(flag, "BoolProperty") && prop_bool_ok(flag) ? flag : nullptr};
    return L.time >= 0 && L.materials >= 0 && L.frame_size > 0 && L.stride > 0;
}

bool c29_flag(const uint8_t* base, const uint8_t* prop) {
    return prop != nullptr && (base[read_i32(prop, kPropertyOffset) + prop[kBoolByteOffset]] & prop[kBoolFieldMask]) != 0;
}

float c29_float(const uint8_t* base, int32_t offset) {
    float v = 0.0f;
    if (offset >= 0) {
        memcpy(&v, base + offset, sizeof(v));
    }
    return v;
}

// One line per key: frames, seconds (uniform time x frames, else the sum of TimeToDisplay), the last
// TimeToDisplay (in case the times are cumulative), frame 0's material count and first material with its package.
void c29_set_key(C27Out& out, const char* set, const uint8_t* elem, const C29Layout& L) {
    const uint8_t* const seq = elem + L.value;
    const auto* const data = static_cast<const uint8_t*>(read_ptr(seq, static_cast<size_t>(L.frames)));
    const int32_t n = read_i32(seq, static_cast<size_t>(L.frames) + kArrayNum);
    const int32_t count = data == nullptr || n < 0 ? 0 : n < kC27MaxElements ? n : kC27MaxElements;
    float sum = 0.0f;
    float last = 0.0f;
    for (int32_t i = 0; i < count; ++i) {
        last = c29_float(data + static_cast<size_t>(i) * L.frame_size, L.time);
        sum += last;
    }
    const bool uniform = c29_flag(seq, L.uniform_flag);
    const float frame_time = c29_float(seq, L.uniform);
    const float seconds = uniform ? frame_time * static_cast<float>(count) : sum;
    const int32_t mats = count > 0 ? read_i32(data, static_cast<size_t>(L.materials) + kArrayNum) : -1;
    const auto* const list = count > 0 ? static_cast<void* const*>(read_ptr(data, static_cast<size_t>(L.materials))) : nullptr;
    const void* const first = list != nullptr && mats > 0 ? list[0] : nullptr;
    const void* const outer = first != nullptr ? read_ptr(first, kObjectOuter) : nullptr;
    c27_write(out, "%s\t%s\tframes=%d\tseconds=%.3f\tsum=%.3f\tlast=%.3f\tuniform=%d(%.3f)\tmats0=%d\tfirst=%s\tpkg=%s", set,
              fname_text(elem).text, n, static_cast<double>(seconds), static_cast<double>(sum),
              static_cast<double>(last), uniform ? 1 : 0, static_cast<double>(frame_time), mats,
              object_name(first).text, object_name(outer).text);
}

void c29_dump_set(C27Out& out, const C29Set& set, C29Layout& L, bool& have_layout) {
    const char* how = nullptr;
    const void* const asset = c29_load_set(set, &how);
    if (asset != nullptr && !have_layout) {
        have_layout = c27_verify(asset) && c29_layout(asset, L);
        log::line("card029: sets layout from %s: %s (anims +0x%X value +0x%X stride 0x%X frames +0x%X frame 0x%X time "
                  "+0x%X materials +0x%X uniform +0x%X flag %s)",
                  set.name, have_layout ? "ok" : "FAILED", L.anims, L.value, L.stride, L.frames, L.frame_size, L.time,
                  L.materials, L.uniform, L.uniform_flag != nullptr ? "ok" : "none");
    }
    C27Sparse s{};
    const bool ok = asset != nullptr && have_layout && c27_sparse(static_cast<const uint8_t*>(asset) + L.anims, s);
    c27_write(out, "== %s (%s, %s, %d keys) ==", set.name, how, ok ? "ok" : "not dumped", ok ? s.live : -1);
    for (int32_t i = 0; ok && i < s.slots; ++i) {
        if (c27_bit(s, i)) {
            c29_set_key(out, set.name, s.data + static_cast<size_t>(i) * L.stride, L);
        }
    }
    log::line("card029: set %s: %s, %d keys", set.name, how, ok ? s.live : -1);
}

// ---- Card 029 audit state ----
constexpr ULONGLONG kC29PollMs = 500;          // equipped weapon poll and vehicle scan
constexpr ULONGLONG kC29StartMs = 10000;       // after the own pawn is first on foot
constexpr ULONGLONG kC29TickMs = 350;          // driver step: one press or one release
constexpr ULONGLONG kC29CopyWaitMs = 120000;     // host: longest wait for the client's pawn before weapons
constexpr ULONGLONG kC29ClientWeaponsMs = 90000; // client weapon stage: covers the host's whole row cycle
constexpr ULONGLONG kC29VehicleHoldMs = 8000;  // in the vehicle before leaving
constexpr ULONGLONG kC29TurnMs = 3000;         // HandleInputLookHorizontal while mounted
constexpr int32_t kC29MaxKeys = 8;             // weapon input tags pressed per weapon

constexpr uint32_t kC29EquipLines = 160;

constexpr uint32_t kC29VehicleLines = 300;
constexpr uint32_t kC29DriverLines = 200;
constexpr uint32_t kC29EventLines = 48;
// Rows driven: every row UBWCOOP_GRANT_WEAPON can give, except the two vehicle-mounted guns (RazorBackAutocannon,
// TurretMorita), which belong to the mech and the turret and are seen when those are entered.
constexpr const char* kC29Rows[] = {"MoritaMk1",   "MoritaMK1Scoped", "MoritaMK2",    "MoritaCarbine", "EPulse44",
                                    "Epulse88",    "NukeLauncher",    "Shotgun",      "VeteranRifle",  "Unarmed",
                                    "Flamethrower", "HighPower",      "Autocannon",   "LaserMorita",   "Railgun",
                                    "LaserMoritaRed"};
constexpr int32_t kC29RowCount = static_cast<int32_t>(sizeof(kC29Rows) / sizeof(kC29Rows[0]));

C29State g_c29{};

constexpr const char* kC29Replicates[] = {"bReplicates", nullptr};

constexpr const char* kC29Used[] = {"IsBeingUsed", "bIsBeingUsed", "bBeingUsed", "bInUse", nullptr};

// The pilot: ControllingPlayerCharacter (mech, object property) or ControllingCharacter (turret, a
// WeakObjectProperty, resolved through FWeakObjectPtr::Get).
const void* c29_controller(const void* vehicle) {
    const uint8_t* prop = nullptr;
    const void* const pilot = prop_object_field(vehicle, "ControllingPlayerCharacter", &prop);
    if (prop != nullptr) {
        return pilot;
    }
    prop = vehicle != nullptr ? prop_find_prop(read_ptr(vehicle, kObjectClass), "ControllingCharacter") : nullptr;
    if (prop == nullptr) {
        return nullptr;
    }
    const auto* const field = static_cast<const uint8_t*>(vehicle) + read_i32(prop, kPropertyOffset);
    return prop_kind_is(prop, "WeakObjectProperty") ? g_weak_get(field) : prop_object_field(vehicle, "ControllingCharacter", &prop);
}

// The reflected kind of a vehicle field ("none" when the class has no such UPROPERTY).
ObjectName c29_field_kind(const void* vehicle, const char* name) {
    const uint8_t* const prop = vehicle != nullptr ? prop_find_prop(read_ptr(vehicle, kObjectClass), name) : nullptr;
    ObjectName kind{};
    if (prop == nullptr) {
        snprintf(kind.text, sizeof(kind.text), "none");
        return kind;
    }
    prop_kind(prop, kind);
    return kind;
}

double c29_gun_yaw(const void* vehicle) {
    const uint8_t* prop = nullptr;
    const void* const gun = prop_object_field(vehicle, "GunHolderMesh", &prop);
    return gun != nullptr ? component_pitch_yaw(gun).y : 0.0;
}

// The equipped weapon poll: one line per pawn whenever its equipped weapon changes.
void c29_poll_pawn(const void* pawn) {
    const void* const inventory = read_ptr(pawn, kMiWeaponInventory);
    const void* const weapon = inventory != nullptr ? g_get_equipped_weapon(inventory) : nullptr;
    const int32_t id = object_index(pawn);
    uint32_t i = 0;
    while (i < g_c29.seen_count && g_c29.seen[i].pawn != id) {
        ++i;
    }
    if (i == g_c29.seen_count) {
        if (i >= kC29Pawns) {
            return;
        }
        g_c29.seen[g_c29.seen_count++] = C29Seen{id, -2};
    }
    if (g_c29.seen[i].weapon == object_index(weapon)) {
        return;
    }
    g_c29.seen[i].weapon = object_index(weapon);
    if (note_event_cap(g_c29.equip_lines, kC29EquipLines, "card029 equipped weapon")) {
        log::line("card029: %s equipped %s on %s (%s): %s (class %s), weapons %d", side(),
                  inventory == nullptr ? "no inventory" : "weapon", object_name(pawn).text,
                  kSprintSideText[sprint_side(pawn)], object_name(weapon).text, class_name(weapon).text,
                  inventory != nullptr ? g_get_weapon_count(inventory) : -1);
    }
}

C29Vehicle* c29_vehicle_slot(int32_t id) {
    for (uint32_t i = 0; i < g_c29.vehicle_count; ++i) {
        if (g_c29.vehicles[i].index == id) {
            return &g_c29.vehicles[i];
        }
    }
    if (g_c29.vehicle_count >= kC29Vehicles) {
        return nullptr;
    }
    C29Vehicle& v = g_c29.vehicles[g_c29.vehicle_count++];
    v = C29Vehicle{id, Vec3{}, 0.0, 0.0, -1, -2};
    return &v;
}

bool c29_moved(const C29Vehicle& v, const Vec3& loc, double yaw, double gun_yaw) {
    const double d = fabs(v.loc.x - loc.x) + fabs(v.loc.y - loc.y) + fabs(v.loc.z - loc.z);
    return d > 5.0 || fabs(v.yaw - yaw) > 1.0 || fabs(v.gun_yaw - gun_yaw) > 1.0;
}

// A mech or turret: logged when first seen and whenever its location, yaw, gun yaw, controller or use changes.
void c29_scan_vehicle(const void* vehicle, bool mech) {
    C29Vehicle* const v = c29_vehicle_slot(object_index(vehicle));
    if (v == nullptr) {
        return;
    }
    const void* const root = read_ptr(vehicle, kActorRootComponent);
    const Vec3 loc = actor_location(vehicle);
    const double yaw = root != nullptr ? component_pitch_yaw(root).y : 0.0;
    const double gun_yaw = mech ? 0.0 : c29_gun_yaw(vehicle);
    const void* const controller = c29_controller(vehicle);
    const int used = c29_bool(vehicle, kC29Used);
    const bool first = v->used == -2;
    if (!first && !c29_moved(*v, loc, yaw, gun_yaw) && object_index(controller) == v->controller && used == v->used) {
        return;
    }
    *v = C29Vehicle{v->index, loc, yaw, gun_yaw, object_index(controller), used};
    if (!note_event_cap(g_c29.vehicle_lines, kC29VehicleLines, "card029 vehicle")) {
        return;
    }
    const auto* const a = static_cast<const uint8_t*>(vehicle);
    log::line("card029: %s %s %s %s (class %s): at (%.0f, %.0f, %.0f) yaw %.1f gun yaw %.1f, roles local %u remote %u, "
              "bReplicates %d, controller %s (%s), being used %d",
              side(), first ? "found" : "changed", mech ? "mech" : "turret", object_name(vehicle).text,
              class_name(vehicle).text, loc.x, loc.y, loc.z, yaw, gun_yaw, a[kActorLocalRole], a[kActorRemoteRole],
              c29_bool(vehicle, kC29Replicates), object_name(controller).text,
              controller != nullptr ? kSprintSideText[sprint_side(controller)] : "-", used);
}

// The levels' player pawns, mechs and turrets.
void c29_scan(const void* world) {
    const void* const pawn_cls = g_player_mi_class();
    const void* const mech_cls = g_mech_class();
    const void* const turret_cls = g_turret_class();
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    for (int32_t l = 0; levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const void* const level = levels[l];
        const auto* const actors = level != nullptr ? static_cast<void* const*>(read_ptr(level, kLevelActors)) : nullptr;
        const int32_t num = level != nullptr ? read_i32(level, kLevelActors + kArrayNum) : 0;
        for (int32_t i = 0; actors != nullptr && i < num && i < kMaxLevelActors; ++i) {
            const void* const actor = actors[i];
            if (actor == nullptr) {
                continue;
            }
            if (is_a(actor, pawn_cls)) {
                c29_poll_pawn(actor);
            } else if (is_a(actor, mech_cls)) {
                c29_scan_vehicle(actor, true);
            } else if (is_a(actor, turret_cls)) {
                c29_scan_vehicle(actor, false);
            }
        }
    }
}

// The nearest mech or turret to `pawn`, or null.
void* c29_nearest_vehicle(const void* world, const void* pawn, bool mech) {
    const void* const cls = mech ? g_mech_class() : g_turret_class();
    const Vec3 at = actor_location(pawn);
    void* best = nullptr;
    double best_d = 0.0;
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    for (int32_t l = 0; levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const void* const level = levels[l];
        const auto* const actors = level != nullptr ? static_cast<void* const*>(read_ptr(level, kLevelActors)) : nullptr;
        const int32_t num = level != nullptr ? read_i32(level, kLevelActors + kArrayNum) : 0;
        for (int32_t i = 0; actors != nullptr && i < num && i < kMaxLevelActors; ++i) {
            void* const actor = actors[i];
            const double d = actor != nullptr && is_a(actor, cls) ? vec_dist(actor_location(actor), at) : -1.0;
            if (d >= 0.0 && (best == nullptr || d < best_d)) {
                best = actor;
                best_d = d;
            }
        }
    }
    return best;
}

// ---- Card 029 vehicle detours (log only: the arguments go through unchanged) ----
void c29_vehicle_event(const char* what, const char* when, const void* vehicle, const void* pawn) {
    if (!g_c29_audit || !note_event_cap(g_c29.event_lines, kC29EventLines, "card029 vehicle event")) {
        return;
    }
    const void* const pilot = pawn != nullptr ? pawn : c29_controller(vehicle);
    const auto* const a = static_cast<const uint8_t*>(vehicle);
    log::line("card029: %s %s %s: vehicle %s (roles local %u remote %u), pawn %s (%s), controller %s, being used %d, "
              "pawn bHidden %d, pawn attached to %s; fields ControllingPlayerCharacter %s, ControllingCharacter %s",
              side(), what, when, object_name(vehicle).text, a[kActorLocalRole], a[kActorRemoteRole],
              object_name(pilot).text, pilot != nullptr ? kSprintSideText[sprint_side(pilot)] : "-",
              object_name(c29_controller(vehicle)).text, c29_bool(vehicle, kC29Used), c29_bool(pilot, kC29Hidden),
              object_name(pilot != nullptr ? g_get_attach_parent_actor(pilot) : nullptr).text,
              c29_field_kind(vehicle, "ControllingPlayerCharacter").text,
              c29_field_kind(vehicle, "ControllingCharacter").text);
}

// EnterMech and LeaveMech detours: card 075 block (co-op, always on).

void detour_mount_turret(void* turret, void* pawn) {
    c29_vehicle_event("MountTurret", "before", turret, pawn);
    g_orig_mount_turret(turret, pawn);
    c29_vehicle_event("MountTurret", "after", turret, pawn);
}

void c123_client_leave(void* turret);

// Card 123: always on. Client: the leave never runs locally (its DetachCharacterFromTurret calls AController::Possess
// with the turret's controller, null on a client, card 029 dump 3402DC26); the leave goes to the host. Host and
// standalone: the game's own run, logged by the card 029 audit when it is on.
void detour_leave_turret(void* turret) {
    if (g_client_session) {
        c123_client_leave(turret);
        return;
    }
    const void* const pilot = g_c29_audit ? c29_controller(turret) : nullptr;
    c29_vehicle_event("LeaveTurret", "before", turret, pilot);
    g_orig_leave_turret(turret);
    c29_vehicle_event("LeaveTurret", "after", turret, pilot);
}

// ---- Card 029 test driver on the machine's own pawn ----
void c29_driver_line(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void c29_driver_line(const char* fmt, ...) {
    if (!note_event_cap(g_c29.driver_lines, kC29DriverLines, "card029 driver")) {
        return;
    }
    char text[384];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    log::line("card029: %s driver: %s", side(), text);
}

// Equip weapon row `row` on the own pawn: EquipWeapon when the inventory holds it, else grant it equipped.
void c29_equip(void* pawn, int32_t row) {
    void* const inventory = c192_mi_pawn(pawn, "c29 test equip") ? read_ptr(pawn, kMiWeaponInventory) : nullptr;
    const void* const table = inventory != nullptr ? c21_weapon_table(g_get_st_game_instance(pawn)) : nullptr;
    const uint8_t* const tag = table != nullptr ? c21_row_tag(table, kC29Rows[row]) : nullptr;
    const int32_t index = tag != nullptr ? g_weapon_index_from_tag(inventory, tag) : -1;
    const void* const definition = tag != nullptr && index < 0 ? g_weapon_def_from_tag(tag) : nullptr;
    void* cls = definition != nullptr ? read_ptr(definition, kObjectClass) : nullptr;
    bool ok = false;
    if (index >= 0) {
        g_equip_weapon(pawn, index);
        ok = true;
    } else if (cls != nullptr) {
        ok = g_orig_give_by_class(inventory, &cls, -1, true);
        void* const held = ok ? nullptr : g_get_equipped_weapon(inventory);
        if (held != nullptr && strstr(object_name(held).text, "Unarmed") == nullptr) { // its slot is taken: free it
            c29_driver_line("grant failed; removing %s to free its slot", object_name(held).text);
            g_remove_weapon(inventory, held);
            ok = g_orig_give_by_class(inventory, &cls, -1, true);
        }
    }
    c29_driver_line("row %s (tag %s): %s, index %d, ok %d, equipped now %s", kC29Rows[row],
                    tag != nullptr ? fname_text(tag).text : "not found",
                    index >= 0 ? "EquipWeapon" : cls != nullptr ? "GiveWeaponByClass" : "skipped", index, ok ? 1 : 0,
                    object_name(inventory != nullptr ? g_get_equipped_weapon(inventory) : nullptr).text);
}

// The FName of the `n`th non-axis input tag in a weapon ASC's input tag map, or 0.
uint64_t c29_weapon_tag(const void* asc, int32_t n) {
    const auto* const data = static_cast<const uint8_t*>(read_ptr(asc, kAscInputTagMap));
    const int32_t num = read_i32(asc, kAscInputTagMapNum);
    const int32_t count = data == nullptr || num <= 0 ? 0 : num < kAscInputTagMapMax ? num : kAscInputTagMapMax;
    int32_t seen = 0;
    for (int32_t i = 0; i < count; ++i) {
        uint64_t key = 0;
        memcpy(&key, data + static_cast<size_t>(i) * kAscInputTagMapStride, sizeof(key));
        // Only player input tags: pressing WeaponSystem.* event tags hung the game thread (card 029 run 1).
        if (key == 0 || strncmp(fname_text(&key).text, "InputTag.", 9) != 0 || c21_is_axis_tag(fname_text(&key).text)) {
            continue;
        }
        if (seen++ == n) {
            return key;
        }
    }
    return 0;
}

// Client weapon stage. Weapons a client grants itself exist only there, and firing one hit a fatal
// GameplayAbilityTargetTypes.cpp:240 on the client (card 029 pair run 1). So the client grants nothing: the host
// equips each row on the server copy, and the client presses the tags again whenever the replicated weapon changes.
void c29_client_weapon(const void* weapon, ULONGLONG now) {
    if (weapon != g_c29.weapon) {
        g_c29.weapon = weapon;
        g_c29.key = 0;
        c29_driver_line("equipped weapon is now %s; pressing its tags", object_name(weapon).text);
    }
    if (now - g_c29.stage_ms >= kC29ClientWeaponsMs) {
        g_c29.stage = C29Stage::Mech;
        g_c29.stage_ms = now;
    }
}

// One driver tick in the weapon stage: press the next tag, release it, then the next weapon.
void c29_weapon_tick(void* pawn, ULONGLONG now) {
    const void* weapon = nullptr;
    void* const asc = c24_weapon_asc(pawn, &weapon);
    if (g_c29.pressed != 0) {
        if (asc != nullptr) {
            g_orig_asc_tag_released(asc, &g_c29.pressed);
        }
        g_c29.pressed = 0;
        return;
    }
    const uint64_t tag = asc != nullptr && g_c29.key < kC29MaxKeys ? c29_weapon_tag(asc, g_c29.key) : 0;
    if (tag != 0) {
        ++g_c29.key;
        g_c29.pressed = tag;
        c29_driver_line("press %s on %s", fname_text(&tag).text, object_name(weapon).text);
        g_orig_asc_tag_pressed(asc, &g_c29.pressed);
        return;
    }
    if (g_client_session) {
        c29_client_weapon(weapon, now);
        return;
    }
    if (g_c29.row >= kC29RowCount) {
        g_c29.stage = C29Stage::Mech;
        g_c29.stage_ms = now;
        return;
    }
    g_c29.key = 0;
    c29_equip(pawn, g_c29.row);
    void* const copy = c29_server_copy(pawn);
    if (copy != nullptr) {
        c29_equip(copy, g_c29.row);
    }
    ++g_c29.row;
}

// Interactable interface subobject offsets: OnInteraction starts `lea rdi, [rcx-0x740]` (turret, exe+0x51D0421)
// and `lea rdi, [rcx-0xC08]` (mech, exe+0x50F7851) before calling MountTurret / EnterMech on rdi.
constexpr size_t kTurretInteractableIface = 0x740;
constexpr size_t kMechInteractableIface = 0xC08;
// HandleInputLookHorizontal overrides ISTInputReceiverInterface and uses `this` unadjusted: that subobject is at
// +0x750 (HandleInputUse_Pressed, exe+0x51CBA60, is `add rcx, -0x750; jmp LeaveTurret`).
constexpr size_t kTurretInputReceiverIface = 0x750;
constexpr size_t kTurretLookGate = 0xF9; // from the input-receiver subobject

// Enter the nearest mech (or mount the nearest turret) through its OnInteraction with the pawn's interactor.
void c29_vehicle_enter(const void* world, void* pawn, bool mech, ULONGLONG now) {
    void* const vehicle = c29_nearest_vehicle(world, pawn, mech);
    void* const interactor = read_ptr(pawn, kCharacterInteractor);
    g_c29.vehicle = object_index(vehicle);
    g_c29.stage = mech ? C29Stage::MechOut : C29Stage::TurretOut;
    g_c29.stage_ms = now;
    c29_driver_line("%s %s at %.0f units, interactor %s", mech ? "enter mech" : "mount turret",
                    object_name(vehicle).text,
                    vehicle != nullptr ? vec_dist(actor_location(vehicle), actor_location(pawn)) : -1.0,
                    object_name(interactor).text);
    if (vehicle == nullptr || interactor == nullptr) {
        g_c29.stage = mech ? C29Stage::Turret : C29Stage::Done;
        return;
    }
    // OnInteraction overrides an interface virtual: `this` is the interface subobject, not the actor.
    uint8_t* const iface = static_cast<uint8_t*>(vehicle) + (mech ? kMechInteractableIface : kTurretInteractableIface);
    (mech ? g_mech_on_interaction : g_turret_on_interaction)(iface, interactor);
}

// The vehicle the driver used, found again by object index (no pointer is kept across frames).
void* c29_driver_vehicle(const void* world, bool mech) {
    const void* const cls = mech ? g_mech_class() : g_turret_class();
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    for (int32_t l = 0; levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const void* const level = levels[l];
        const auto* const actors = level != nullptr ? static_cast<void* const*>(read_ptr(level, kLevelActors)) : nullptr;
        const int32_t num = level != nullptr ? read_i32(level, kLevelActors + kArrayNum) : 0;
        for (int32_t i = 0; actors != nullptr && i < num && i < kMaxLevelActors; ++i) {
            void* const actor = actors[i];
            if (actor != nullptr && object_index(actor) == g_c29.vehicle && is_a(actor, cls)) {
                return actor;
            }
        }
    }
    return nullptr;
}

void c29_vehicle_tick(const void* world, void* pawn, ULONGLONG now) {
    const bool mech = g_c29.stage == C29Stage::Mech || g_c29.stage == C29Stage::MechOut;
    if (g_c29.stage == C29Stage::Mech || g_c29.stage == C29Stage::Turret) {
        c29_vehicle_enter(world, pawn, mech, now);
        return;
    }
    void* const vehicle = c29_driver_vehicle(world, mech);
    if (!mech && vehicle != nullptr && now - g_c29.stage_ms < kC29TurnMs) {
        g_turret_look_h(static_cast<uint8_t*>(vehicle) + kTurretInputReceiverIface, 0.5f);
    }
    if (now - g_c29.stage_ms < kC29VehicleHoldMs) {
        return;
    }
    // HandleInputLookHorizontal returns at once while the input-receiver byte +0xF9 is 0 (exe+0x51CA74B).
    const uint8_t* const recv = vehicle != nullptr && !mech ? static_cast<const uint8_t*>(vehicle) + kTurretInputReceiverIface : nullptr;
    c29_driver_line("%s %s (gun yaw %.1f, look gate byte %d)", mech ? "leave mech" : "leave turret",
                    object_name(vehicle).text, vehicle != nullptr && !mech ? c29_gun_yaw(vehicle) : 0.0,
                    recv != nullptr ? recv[kTurretLookGate] : -1);
    // A client's local MountTurret never possesses, so its LeaveTurret calls AController::Possess on a null
    // controller (DetachCharacterFromTurret+0x1E5, card 029 pair run 2 dump): the client stays mounted.
    if (vehicle != nullptr && g_client_session) {
        c29_driver_line("client: LeaveTurret skipped (it crashes in AController::Possess after a local mount)");
    } else if (vehicle != nullptr) {
        (mech ? g_mech_request_leave : detour_leave_turret)(vehicle);
    }
    g_c29.stage = mech ? C29Stage::Turret : C29Stage::Done;
    g_c29.stage_ms = now;
}

void c29_drive(const void* world, ULONGLONG now) {
    void* const pc = own_pc(world);
    auto* const pawn = pc != nullptr ? static_cast<uint8_t*>(read_ptr(pc, kControllerPawn)) : nullptr;
    const bool own = pawn != nullptr && is_a(pawn, g_player_mi_class()) && sprint_side(pawn) != kSideOther;
    const bool on_foot = own && is_a(pawn, g_player_character_class()) && holding_dropship(pawn) == nullptr;
    if (g_c29.stage == C29Stage::Wait) {
        if (on_foot && g_c29.on_foot_ms == 0) {
            g_c29.on_foot_ms = now;
            c29_driver_line("own pawn %s on foot; driver starts in %llu ms", object_name(pawn).text,
                            static_cast<unsigned long long>(kC29StartMs));
        }
        // The host cycles weapons on the server copy too, so it waits for one (pair runs) up to kC29CopyWaitMs.
        const bool copy = g_client_session || (own && c29_server_copy(pawn) != nullptr);
        const ULONGLONG wait = copy ? kC29StartMs : kC29CopyWaitMs;
        if (g_c29.on_foot_ms != 0 && now - g_c29.on_foot_ms >= wait) {
            g_c29.stage = C29Stage::Weapons;
            g_c29.stage_ms = now;
        }
        return;
    }
    // While mounted the PlayerController's pawn may be the vehicle: the out stages do not need the own pawn.
    const bool out = g_c29.stage == C29Stage::MechOut || g_c29.stage == C29Stage::TurretOut;
    if (!(own || out) || g_c29.stage == C29Stage::Done || now < g_c29.next_tick_ms) {
        return;
    }
    g_c29.next_tick_ms = now + kC29TickMs;
    if (g_c29.stage == C29Stage::Weapons) {
        if (on_foot) {
            c29_weapon_tick(pawn, now);
        }
        return;
    }
    c29_vehicle_tick(world, pawn, now);
}

void c29_step(void* engine) {
    if (!g_c29.armed) {
        return;
    }
    void* const world = g_get_game_world(engine);
    if (world == nullptr) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    c29_drive(world, now);
    if (now >= g_c29.next_poll_ms) {
        g_c29.next_poll_ms = now + kC29PollMs;
        c29_scan(world);
    }
}

TryThrowFlareFn g_orig_try_throw_flare = nullptr;
EquipTacSuppFn g_orig_equip_tac_supp = nullptr;

// Card 306: host. The last EquipTacticalSupportAbility from PickupTacticalSupport (exe+0x51D2A9C), for the swap row.
struct C306Equip {
    char who[kChanNameMax + 1];
    char config[kChanNameMax + 1];
    ULONGLONG ms;
};
C306Equip g_c306_equip{}; // game thread

BlockingVolumeFn g_in_blocking_volume = nullptr;
C29InteractFn g_orig_mech_on_interaction = nullptr;
bool g_c32_pe = false; // ProcessEvent filter open: c32 armed on this map

constexpr size_t kDataTableSubsystemPickups = 0x58; // USTDataTableSubsystem pickups table (TWeakObjectPtr)
constexpr size_t kPickupRowSoftWeak = 0x08;    // row TSoftClassPtr: its TWeakObjectPtr (null until loaded)
constexpr size_t kPickupRowSoftPackage = 0x10; // row TSoftClassPtr: FSoftObjectPath PackageName FName
constexpr size_t kPickupRowSoftAsset = 0x18;   // and AssetName FName
constexpr int32_t kC32TableRows = 64; // DT_Allpickups holds 61
constexpr ULONGLONG kC32ScanMs = 1000;
constexpr ULONGLONG kC32StartMs = 20000;       // after both player pawns are on foot
constexpr ULONGLONG kC32WalkMs = 3000;         // pickup route: walk forward onto the pickup
constexpr ULONGLONG kC32ThrowWaitMs = 25000;   // after a throw, for a pod or mech before the next try
constexpr ULONGLONG kC32MechWaitMs = 90000;    // pad route: for the mech after the activation
constexpr ULONGLONG kC32SettleDropMs = 10000;  // a dropped mech: from first seen to the enter
constexpr ULONGLONG kC32SettleSpawnMs = 3000;  // a directly spawned mech
constexpr ULONGLONG kC32DriveMs = 10000;       // AddMovementInput on the PC's pawn after the enter
constexpr ULONGLONG kC32HostShotMs = 3000;     // after the enter
constexpr ULONGLONG kC32ClientInteractMs = 40000; // client: after it first sees a pilotable mech
constexpr ULONGLONG kC32ClientMovedShotMs = 6000; // client: after its first sample of the mech moving (its spawn drop), about 1 s after the host shot
constexpr ULONGLONG kC32ClientSeenShotMs = 25000; // client: after first seen, when it never moves there
constexpr ULONGLONG kC32ClientBlindShotMs = 60000; // client: after its start time, when it never sees a mech
constexpr ULONGLONG kC32AfterInteractMs = 4000;   // client: shot and state line after its interact
constexpr ULONGLONG kC32ClientDoneMs = 150000;    // client: after its start time, the run's end marker
constexpr uint32_t kC32ThrowTries = 3;
constexpr double kC32PickupAhead = 300.0;
constexpr double kC32MechAhead = 500.0;
constexpr double kC32PadAhead = 500.0;
constexpr double kC32PadDrop = -90.0;          // pawn centre to the floor
constexpr double kC32MechLift = 100.0;
constexpr double kC32MovedUnits = 30.0;

constexpr uint32_t kC32RowLines = 300;
constexpr uint32_t kC32EventLines = 64;
constexpr uint32_t kC32DriverLines = 120;

constexpr size_t kC32ParmsBytes = 512;         // zeroed parameter block for the pad event
constexpr const char* kC32Patterns[] = {"MechDropPlatform", "AIMechCallIn", "BaseMech", "M7Mech", "Pod_MechDrop",
                                        "MechSupport"};
constexpr const char* kC32PeWords[] = {"Mech", "Interact", "Embark", "Possess", "Flare", "TacticalSupport",
                                       "TacSupp", "Pilot"};
constexpr const wchar_t kC32PickupClass[] =
    L"/Game/Blueprints/Combat/MechAlly/BP_TacSuppPickup_MechSupport.BP_TacSuppPickup_MechSupport_C";
constexpr const wchar_t kC32MechClass[] = L"/Game/Blueprints/Player/Mechs/BP_BaseMech.BP_BaseMech_C";
constexpr const wchar_t kC32PadClass[] =
    L"/Game/Blueprints/EnvironmentElements/MechPad/BP_MechDropPlatform.BP_MechDropPlatform_C";
constexpr const wchar_t* kC32PadEvents[] = {L"Activated", L"Activate"};
constexpr const char* kC32PadCanActivate[] = {"CanBeActivated", "bCanBeActivated", nullptr};
constexpr const char* kC32PadOnStart[] = {"ActivateOnStart", "bActivateOnStart", nullptr};

C32State g_c32{}; // game thread; reset per map by c32_arm_map

void c32_line(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void c32_line(const char* fmt, ...) {
    if (!note_event_cap(g_c32.driver_lines, kC32DriverLines, "card032 driver")) {
        return;
    }
    char buf[768];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    log::line("card032: %s driver: %s", side(), buf);
}

bool c32_matches(const void* actor) {
    const ObjectName cls = class_name(actor);
    for (const char* p : kC32Patterns) {
        if (strstr(cls.text, p) != nullptr) {
            return true;
        }
    }
    return is_a(actor, g_mech_class());
}

// A reflected one-byte field (AutoPossessAI, an EnumProperty or ByteProperty), or -1 when the class has none.
int c32_byte_field(const void* object, const char* name) {
    const uint8_t* const prop = object != nullptr ? prop_find_prop(read_ptr(object, kObjectClass), name) : nullptr;
    return prop != nullptr ? static_cast<const uint8_t*>(object)[read_i32(prop, kPropertyOffset)] : -1;
}

void c32_row(const char* what, const void* actor, const Vec3& loc) {
    if (!note_event_cap(g_c32.row_lines, kC32RowLines, "card032 actor rows")) {
        return;
    }
    const auto* const a = static_cast<const uint8_t*>(actor);
    const int auto_possess = c32_byte_field(actor, "AutoPossessAI");
    const void* const controller = auto_possess >= 0 ? read_ptr(actor, kPawnController) : nullptr;
    const bool mech = is_a(actor, g_mech_class());
    const void* const weak_pc = mech ? g_weak_get(a + kMechController) : nullptr;
    const void* const owner = read_ptr(actor, kActorOwner);
    log::line("card032: %s %s %s %s idx %d at (%.0f, %.0f, %.0f) owner %s, roles local %u remote %u, bReplicates %d, "
              "AutoPossessAI %d, controller %s (%s), pilotable %d, weak PC %s, pilot %s",
              side(), what, class_name(actor).text, object_name(actor).text, object_index(actor), loc.x, loc.y, loc.z,
              object_name(owner).text, a[kActorLocalRole], a[kActorRemoteRole],
              (a[kActorRepFlags] & kRepBit) != 0 ? 1 : 0, auto_possess, object_name(controller).text,
              controller != nullptr ? class_name(controller).text : "-", mech ? 1 : 0, object_name(weak_pc).text,
              mech ? object_name(c29_controller(actor)).text : "-");
}

C32Track* c32_track_slot(const void* actor, bool* first) {
    const int32_t index = object_index(actor);
    *first = false;
    for (uint32_t i = 0; i < g_c32.track_count; ++i) {
        if (g_c32.track[i].index == index) {
            return &g_c32.track[i];
        }
    }
    if (g_c32.track_count >= kC32Tracked) {
        return nullptr;
    }
    *first = true;
    C32Track* const t = &g_c32.track[g_c32.track_count++];
    *t = C32Track{index, {}, strstr(class_name(actor).text, "MechDropPlatform") != nullptr,
                  is_a(actor, g_mech_class()), false, false};
    return t;
}

void c32_note_actor(const void* actor, ULONGLONG now) {
    bool first = false;
    C32Track* const t = c32_track_slot(actor, &first);
    if (t == nullptr) {
        return;
    }
    const Vec3 loc = actor_location(actor);
    const bool moved = !first && vec_dist(loc, t->loc) > kC32MovedUnits;
    t->live = true;
    if (first || moved) {
        c32_row(first ? "found" : "moved", actor, loc);
        t->loc = loc;
    }
    if (t->pilotable && g_c32.mech_seen_ms == 0 && (first || g_client_session)) {
        g_c32.mech_seen_ms = now; // the host's placed mechs at map start are reset by c32_host_spawn
        g_c32.mech = t->index;
    }
    // The client's target is the mech the host drives: the first pilotable one that moves.
    if (t->pilotable && moved && g_c32.mech_moved_ms == 0 && (g_client_session || t->index == g_c32.mech)) {
        g_c32.mech_moved_ms = now;
        g_c32.mech = t->index;
    }
}

// Every level actor that matches; rows for found and moved, then gone for tracked actors not listed.
void c32_scan(const void* world, ULONGLONG now) {
    for (uint32_t i = 0; i < g_c32.track_count; ++i) {
        g_c32.track[i].live = false;
    }
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    for (int32_t l = 0; levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const void* const level = levels[l];
        const auto* const actors = level != nullptr ? static_cast<void* const*>(read_ptr(level, kLevelActors)) : nullptr;
        const int32_t num = level != nullptr ? read_i32(level, kLevelActors + kArrayNum) : 0;
        for (int32_t i = 0; actors != nullptr && i < num && i < kMaxLevelActors; ++i) {
            if (actors[i] != nullptr && c32_matches(actors[i])) {
                c32_note_actor(actors[i], now);
            }
        }
    }
    for (uint32_t i = 0; i < g_c32.track_count; ++i) {
        C32Track& t = g_c32.track[i];
        if (!t.live && t.index >= 0 && note_event_cap(g_c32.row_lines, kC32RowLines, "card032 actor rows")) {
            log::line("card032: %s gone idx %d (last at (%.0f, %.0f, %.0f), pad %d, pilotable %d, spawned %d)", side(),
                      t.index, t.loc.x, t.loc.y, t.loc.z, t.pad ? 1 : 0, t.pilotable ? 1 : 0, t.spawned ? 1 : 0);
        }
        if (!t.live) {
            t.index = -1; // a gone actor's slot stays used; its index never matches again
        }
    }
}

// A level actor by object index, or null.
void* c32_find(const void* world, int32_t index) {
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    for (int32_t l = 0; index >= 0 && levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const void* const level = levels[l];
        const auto* const actors = level != nullptr ? static_cast<void* const*>(read_ptr(level, kLevelActors)) : nullptr;
        const int32_t num = level != nullptr ? read_i32(level, kLevelActors + kArrayNum) : 0;
        for (int32_t i = 0; actors != nullptr && i < num && i < kMaxLevelActors; ++i) {
            if (actors[i] != nullptr && object_index(actors[i]) == index) {
                return actors[i];
            }
        }
    }
    return nullptr;
}

// The pickups DataTable's rows; TacMechSupport with its soft class path.
void c32_log_table(const void* pawn) {
    g_c32.table_logged = true;
    const void* const gi = g_get_st_game_instance(pawn);
    const void* const engine = gi != nullptr ? read_ptr(gi, kObjectOuter) : nullptr;
    const auto* const sub = static_cast<const uint8_t*>(engine != nullptr ? g_get_data_table_subsystem(engine) : nullptr);
    const void* const table = sub != nullptr ? g_weak_get(sub + kDataTableSubsystemPickups) : nullptr;
    if (table == nullptr) {
        log::line("WARN card032: %s pickups DataTable not found (engine %p, subsystem %p)", side(), engine, sub);
        return;
    }
    const uint8_t* const map = c21_row_map(table);
    const int32_t n = array_num(map, 0);
    const auto* const data = static_cast<const uint8_t*>(read_ptr(map, kArrayData));
    log::line("card032: %s pickups table %s: %d rows", side(), object_name(table).text, n);
    for (int32_t i = 0; i < n && i < kC32TableRows && data != nullptr; ++i) {
        const uint8_t* const e = data + kRowMapElement * static_cast<size_t>(i);
        const auto* const row = static_cast<const uint8_t*>(read_ptr(e, 8));
        const ObjectName name = fname_text(e);
        if (row == nullptr) {
            continue;
        }
        log::line("card032: %s pickups row %d %s%s: soft class %s.%s (resolved %s)", side(), i, name.text,
                  strcmp(name.text, "TacMechSupport") == 0 ? " <-- MechSupport" : "",
                  fname_text(row + kPickupRowSoftPackage).text, fname_text(row + kPickupRowSoftAsset).text,
                  object_name(g_weak_get(row + kPickupRowSoftWeak)).text);
    }
}

// --- Card 032 detours: log only; every argument passes through unchanged. ---

void c32_mech_event(const char* what, const char* when, const void* mech, const void* pawn,
                    unsigned long long caller) {
    if (g_c32_mode == config::SpawnMech::Off ||
        !note_event_cap(g_c32.event_lines, kC32EventLines, "card032 mech events")) {
        return;
    }
    const auto* const m = static_cast<const uint8_t*>(mech);
    const void* const pc = g_weak_get(m + kMechController);
    const void* const pilot = pawn != nullptr ? pawn : c29_controller(mech);
    const void* const pilot_pc = pilot != nullptr ? read_ptr(pilot, kPawnController) : nullptr;
    log::line("card032: %s %s %s: mech %s (roles %u/%u), caller exe+0x%llX, weak PC %s (its pawn %s), mech controller "
              "%s; pilot %s (%s) bHidden %d attached to %s controller %s (its pawn %s)",
              side(), what, when, object_name(mech).text, m[kActorLocalRole], m[kActorRemoteRole], caller,
              object_name(pc).text, object_name(pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr).text,
              object_name(read_ptr(mech, kPawnController)).text, object_name(pilot).text,
              pilot != nullptr ? kSprintSideText[sprint_side(pilot)] : "-", c29_bool(pilot, kC29Hidden),
              object_name(pilot != nullptr ? g_get_attach_parent_actor(pilot) : nullptr).text,
              object_name(pilot_pc).text,
              object_name(pilot_pc != nullptr ? read_ptr(pilot_pc, kControllerPawn) : nullptr).text);
}

// The OnInteraction detour: card 075 block (co-op, always on).

void detour_try_throw_flare(void* component, int32_t index) {
    const bool on = g_c32_mode != config::SpawnMech::Off;
    const unsigned long long caller = exe_rva(_ReturnAddress());
    if (on) {
        log::line("card032: %s TryThrowFlare enter: component %p of %s, index %d, caller exe+0x%llX", side(), component,
                  object_name(read_ptr(component, kObjectOuter)).text, index, caller);
    }
    if (c174_block_throw(component)) {
        return; // card 174: the client's own local ability; the host's server copy throws
    }
    g_orig_try_throw_flare(component, index);
    if (on) {
        log::line("card032: %s TryThrowFlare return (void)", side());
    }
}

void detour_equip_tac_supp(void* component, void* const* config_class, bool flag) {
    static uint32_t lines = 0; // card 105: always logged (rare call), capped
    if (note_event_cap(lines, 64, "card105 equip")) {
        log::line("card032: %s EquipTacticalSupportAbility: component %p of %s, config class %s, flag %d, caller "
                  "exe+0x%llX",
                  side(), component, object_name(read_ptr(component, kObjectOuter)).text,
                  object_name(config_class != nullptr ? *config_class : nullptr).text, flag ? 1 : 0,
                  exe_rva(_ReturnAddress()));
    }
    if (exe_rva(_ReturnAddress()) == 0x51D2A9C) { // card 306: PickupTacticalSupport, for the host swap row
        const void* const owner = read_ptr(component, kObjectOuter);
        snprintf(g_c306_equip.who, sizeof(g_c306_equip.who), "%s", object_name(owner).text);
        snprintf(g_c306_equip.config, sizeof(g_c306_equip.config), "%s",
                 object_name(config_class != nullptr ? *config_class : nullptr).text);
        g_c306_equip.ms = GetTickCount64();
    }
    g_orig_equip_tac_supp(component, config_class, flag);
}

bool c32_pe_interesting(const void* function) {
    const ObjectName fn = object_name(function);
    const ObjectName outer = object_name(read_ptr(function, kObjectOuter));
    for (const char* w : kC32PeWords) {
        if (strstr(fn.text, w) != nullptr || strstr(outer.text, w) != nullptr) {
            return true;
        }
    }
    return false;
}

Vec3 c32_ahead(const void* pc, const void* pawn, double dist, double lift) {
    const Vec3 at = actor_location(pawn);
    const double yaw = read_vec3(pc, kControllerControlRotation).y / kRadToDeg;
    return Vec3{at.x + cos(yaw) * dist, at.y + sin(yaw) * dist, at.z + lift};
}

// StaticLoadObject (card 023's route) and UWorld::SpawnActor with AdjustIfPossibleButAlwaysSpawn, facing the pawn.
void* c32_spawn(void* world, const void* pc, const wchar_t* path, const Vec3& loc) {
    void* const cls = g_static_load_object(g_uclass_class(), nullptr, path, nullptr, kC29LoadFlags, nullptr, true,
                                           nullptr);
    if (cls == nullptr) {
        c32_line("WARN StaticLoadObject(UClass, %ls) returned null -- no spawn", path);
        return nullptr;
    }
    alignas(16) uint8_t params[kSpawnParamsSize] = {};
    g_spawn_params_ctor(params);
    params[kSpawnCollisionOverride] = kSpawnAdjustAlways;
    const Vec3 rot{0.0, read_vec3(pc, kControllerControlRotation).y + 180.0, 0.0};
    void* const actor = g_world_spawn_actor(world, cls, &loc, &rot, params);
    // The TFunction at +0x40 is never set, so it needs no destructor; say so loudly if that ever changes.
    if (read_ptr(params, kSpawnPreSpawnFn) != nullptr) {
        c32_line("WARN FActorSpawnParameters CustomPreSpawnInitalization is set after SpawnActor");
    }
    c32_line("%sSpawnActor(%s, (%.0f, %.0f, %.0f)) returned %p %s idx %d", actor != nullptr ? "" : "WARN ",
             class_name(cls).text, loc.x, loc.y, loc.z, actor, object_name(actor).text, object_index(actor));
    return actor;
}

void c32_mark_spawned(void* actor) {
    bool first = false;
    C32Track* const t = actor != nullptr ? c32_track_slot(actor, &first) : nullptr;
    if (t != nullptr) {
        t->spawned = true;
        t->loc = actor_location(actor);
        c32_row("spawned", actor, t->loc);
    }
}

// Calls the first pad event found (BP custom event "Activated", else "Activate") through ProcessEvent.
void c32_activate_pad(void* pad) {
    const int can = c29_bool(pad, kC32PadCanActivate);
    const int on_start = c29_bool(pad, kC32PadOnStart);
    for (const wchar_t* name : kC32PadEvents) {
        uint64_t fname = 0;
        g_fname_ctor(&fname, name, kFNameAdd);
        void* const fn = fname != 0 ? g_find_function(pad, fname) : nullptr;
        c32_line("pad %s idx %d: FindFunction(%ls) = %s; CanBeActivated %d, ActivateOnStart %d, MechToSpawn field %s",
                 object_name(pad).text, object_index(pad), name, object_name(fn).text, can, on_start,
                 c29_field_kind(pad, "MechToSpawn").text);
        if (fn == nullptr) {
            continue;
        }
        alignas(16) uint8_t parms[kC32ParmsBytes] = {};
        const auto process_event = reinterpret_cast<ProcessEventFn>(read_ptr(read_ptr(pad, 0), kVtProcessEvent));
        process_event(pad, fn, parms);
        c32_line("pad %s: ProcessEvent(%ls) returned", object_name(pad).text, name);
        return;
    }
    c32_line("WARN pad %s has no activation event -- no mech expected", object_name(pad).text);
}

// The nearest tracked placed pad to the pawn, or null.
void* c32_placed_pad(const void* world, const void* pawn) {
    const Vec3 at = actor_location(pawn);
    int32_t best = -1;
    double best_d = 0.0;
    for (uint32_t i = 0; i < g_c32.track_count; ++i) {
        const C32Track& t = g_c32.track[i];
        const double d = vec_dist(t.loc, at);
        if (t.pad && t.live && !t.spawned && (best < 0 || d < best_d)) {
            best = t.index;
            best_d = d;
        }
    }
    c32_line("nearest placed pad idx %d at %.0f units", best, best < 0 ? -1.0 : best_d);
    return c32_find(world, best);
}

void c32_host_spawn(void* world, void* pc, void* pawn, ULONGLONG now) {
    g_c32.stage_ms = now;
    if (g_c32_mode == config::SpawnMech::Pickup) {
        g_c32.pickup_loc = c32_ahead(pc, pawn, kC32PickupAhead, 0.0);
        void* const pickup = c32_spawn(world, pc, kC32PickupClass, g_c32.pickup_loc);
        c32_mark_spawned(pickup);
        g_c32.pickup = object_index(pickup);
        g_c32.mech = -1;
        g_c32.mech_seen_ms = 0;
        g_c32.stage = pickup != nullptr ? C32Stage::Walk : C32Stage::Done;
        return;
    }
    if (g_c32_mode == config::SpawnMech::Mech) {
        void* const mech = c32_spawn(world, pc, kC32MechClass, c32_ahead(pc, pawn, kC32MechAhead, kC32MechLift));
        c32_mark_spawned(mech);
        g_c32.mech = object_index(mech);
        g_c32.mech_seen_ms = now;
        g_c32.stage = mech != nullptr ? C32Stage::Settle : C32Stage::Done;
        return;
    }
    void* pad = c32_placed_pad(world, pawn);
    c32_line("pad route: %s", pad != nullptr ? "a placed pad" : "no placed pad loaded; spawning one");
    if (pad == nullptr) {
        pad = c32_spawn(world, pc, kC32PadClass, c32_ahead(pc, pawn, kC32PadAhead, kC32PadDrop));
        c32_mark_spawned(pad);
    }
    if (pad != nullptr) {
        c32_activate_pad(pad);
    }
    g_c32.mech = -1;
    g_c32.mech_seen_ms = 0;
    g_c32.stage = pad != nullptr ? C32Stage::AwaitMech : C32Stage::Done;
}

// Pickup route: walk toward the pickup (a retry: along the view, out of a blocking volume), then throw.
void c32_walk(const void* pc, void* pawn, ULONGLONG now) {
    const Vec3 at = actor_location(pawn);
    const double dx = g_c32.pickup_loc.x - at.x;
    const double dy = g_c32.pickup_loc.y - at.y;
    const double len = sqrt(dx * dx + dy * dy);
    const double yaw = read_vec3(pc, kControllerControlRotation).y / kRadToDeg;
    const bool toward = g_c32.throws == 0 && len > 1.0;
    const double dir[3] = {toward ? dx / len : cos(yaw), toward ? dy / len : sin(yaw), 0.0};
    g_add_movement_input(pawn, dir, false);
    if (now - g_c32.stage_ms < kC32WalkMs) {
        return;
    }
    c32_line("walk done at (%.0f, %.0f, %.0f), %.0f units from the pickup spot", at.x, at.y, at.z, len);
    g_c32.stage = C32Stage::Throw;
    g_c32.stage_ms = now;
}

// Pickup route: the pawn's TacticalSupportComponent throws its flare (slot 0) through the logging detour.
void c32_throw(const void* world, void* pawn, ULONGLONG now) {
    const uint8_t* prop = nullptr;
    void* const comp = const_cast<void*>(prop_object_field(pawn, "TacticalSupportComponent", &prop));
    const int blocked = comp != nullptr && g_in_blocking_volume != nullptr ? (g_in_blocking_volume(comp) ? 1 : 0) : -1;
    ++g_c32.throws;
    c32_line("throw %u: pickup %s, TacticalSupportComponent %s, IsPlayerInBlockingVolume %d", g_c32.throws,
             c32_find(world, g_c32.pickup) != nullptr ? "still in the world" : "gone (collected?)",
             object_name(comp).text, blocked);
    g_c32.stage = C32Stage::AwaitMech;
    g_c32.stage_ms = now;
    if (comp != nullptr) {
        detour_try_throw_flare(comp, 0);
    }
}

// A pilotable mech appeared: settle, then enter. Timeout: the pickup route retries in the open, else stops.
void c32_await(ULONGLONG now) {
    if (g_c32.mech >= 0) {
        c32_line("pilotable mech idx %d seen; settling", g_c32.mech);
        g_c32.stage = C32Stage::Settle;
        g_c32.stage_ms = now;
        return;
    }
    const bool pickup = g_c32_mode == config::SpawnMech::Pickup;
    if (now - g_c32.stage_ms < (pickup ? kC32ThrowWaitMs : kC32MechWaitMs)) {
        return;
    }
    const bool retry = pickup && g_c32.throws < kC32ThrowTries;
    c32_line("WARN no pilotable mech %llu ms after the %s (AI M7 rows above, if any); %s", now - g_c32.stage_ms,
             pickup ? "throw" : "pad activation", retry ? "walking on, then throwing again" : "stop");
    g_c32.stage = retry ? C32Stage::Walk : C32Stage::Done;
    g_c32.stage_ms = now;
}

void c32_enter(void* world, void* pc, void* pawn, ULONGLONG now) {
    void* const mech = c32_find(world, g_c32.mech);
    void* const interactor = read_ptr(pawn, kCharacterInteractor);
    c32_line("enter: mech %s at %.0f units, AutoPossessAI %d, mech controller %s, interactor %s, PC pawn %s",
             object_name(mech).text, mech != nullptr ? vec_dist(actor_location(mech), actor_location(pawn)) : -1.0,
             c32_byte_field(mech, "AutoPossessAI"), object_name(mech != nullptr ? read_ptr(mech, kPawnController) : nullptr).text,
             object_name(interactor).text, object_name(read_ptr(pc, kControllerPawn)).text);
    g_c32.stage = C32Stage::Drive;
    g_c32.stage_ms = now;
    if (mech == nullptr || interactor == nullptr) {
        g_c32.stage = C32Stage::Done;
        return;
    }
    g_mech_on_interaction(static_cast<uint8_t*>(mech) + kMechInteractableIface, interactor);
    c32_line("after OnInteraction: PC pawn %s (%s)", object_name(read_ptr(pc, kControllerPawn)).text,
             class_name(read_ptr(pc, kControllerPawn)).text);
}

// AddMovementInput along the control yaw on the PC's pawn every tick; shot, then RequestLeaveMech.
void c32_drive(void* world, void* pc, ULONGLONG now) {
    void* const pawn = read_ptr(pc, kControllerPawn);
    const double yaw = read_vec3(pc, kControllerControlRotation).y / kRadToDeg;
    const double dir[3] = {cos(yaw), sin(yaw), 0.0};
    if (pawn != nullptr) {
        g_add_movement_input(pawn, dir, false);
    }
    if (!g_c32.host_shot && now - g_c32.stage_ms >= kC32HostShotMs) {
        g_c32.host_shot = true;
        c32_line("host screenshot (in the mech); PC pawn %s", object_name(pawn).text);
        request_shot(pc, false);
    }
    if (now - g_c32.stage_ms < kC32DriveMs) {
        return;
    }
    void* const mech = c32_find(world, g_c32.mech);
    c32_line("drive done; RequestLeaveMech on %s; PC pawn %s", object_name(mech).text, object_name(pawn).text);
    // Card 075: the pilot may have left already (its own exit input); RequestLeaveMech is for a piloted mech.
    if (mech != nullptr && g_weak_get(static_cast<uint8_t*>(mech) + kMechController) != nullptr) {
        g_mech_request_leave(mech);
    }
    c32_line("after RequestLeaveMech: PC pawn %s", object_name(read_ptr(pc, kControllerPawn)).text);
    g_c32.stage = C32Stage::Done;
}

// Host: kC32StartMs after its own pawn and the client's server copy are both on foot.
void c32_host_step(void* world, void* pc, void* pawn, ULONGLONG now) {
    if (g_c32.start_ms == 0) {
        void* const copy = c29_server_copy(pawn);
        if (copy == nullptr || holding_dropship(static_cast<const uint8_t*>(copy)) != nullptr) {
            return;
        }
        g_c32.start_ms = now;
        g_c32.stage_ms = now;
        c32_line("both players on foot (client copy %s); spawn in %llu ms", object_name(copy).text, kC32StartMs);
    }
    switch (g_c32.stage) {
    case C32Stage::Wait:
        if (now - g_c32.stage_ms >= kC32StartMs) {
            c32_host_spawn(world, pc, pawn, now);
        }
        break;
    case C32Stage::Walk: c32_walk(pc, pawn, now); break;
    case C32Stage::Throw: c32_throw(world, pawn, now); break;
    case C32Stage::AwaitMech: c32_await(now); break;
    case C32Stage::Settle:
        if (now - g_c32.stage_ms >= (g_c32_mode == config::SpawnMech::Mech ? kC32SettleSpawnMs : kC32SettleDropMs)) {
            c32_enter(world, pc, pawn, now);
        }
        break;
    default: break;
    }
}

// Client: screenshots while the host drives (or blind), then its own OnInteraction on the mech.
void c32_client_step(void* world, void* pc, void* pawn, ULONGLONG now) {
    if (g_c32.start_ms == 0) {
        g_c32.start_ms = now;
        c32_line("client on foot");
    }
    const bool moved_shot = g_c32.mech_moved_ms != 0 && now - g_c32.mech_moved_ms >= kC32ClientMovedShotMs;
    const bool seen_shot = g_c32.mech_seen_ms != 0 && now - g_c32.mech_seen_ms >= kC32ClientSeenShotMs;
    const bool blind_shot = g_c32.mech_seen_ms == 0 && now - g_c32.start_ms >= kC32ClientBlindShotMs;
    if (!g_c32.client_shot && (moved_shot || seen_shot || blind_shot)) {
        g_c32.client_shot = true;
        c32_line("client screenshot (%s)", moved_shot ? "mech moving" : seen_shot ? "mech seen, never moved" : "no mech seen");
        request_shot(pc, false);
    }
    if (!g_c32.client_interacted && g_c32.mech_seen_ms != 0 && now - g_c32.mech_seen_ms >= kC32ClientInteractMs) {
        g_c32.client_interacted = true;
        g_c32.interact_ms = now;
        void* const mech = c32_find(world, g_c32.mech);
        void* const interactor = read_ptr(pawn, kCharacterInteractor);
        c32_line("client OnInteraction: mech %s at %.0f units, interactor %s", object_name(mech).text,
                 mech != nullptr ? vec_dist(actor_location(mech), actor_location(pawn)) : -1.0, object_name(interactor).text);
        if (mech != nullptr && interactor != nullptr) {
            g_mech_on_interaction(static_cast<uint8_t*>(mech) + kMechInteractableIface, interactor);
        }
    }
    if (g_c32.client_interacted && !g_c32.client_after && now - g_c32.interact_ms >= kC32AfterInteractMs) {
        g_c32.client_after = true;
        void* const mech = c32_find(world, g_c32.mech);
        c32_mech_event("client state", "after interact", mech, nullptr, 0);
        c32_line("client after interact: PC pawn %s, mech %s", object_name(read_ptr(pc, kControllerPawn)).text,
                 object_name(mech).text);
        request_shot(pc, false);
    }
    const bool finished = g_c32.client_after || (g_c32.mech_seen_ms == 0 && now - g_c32.start_ms >= kC32ClientDoneMs);
    if (finished && !g_c32.client_done && now - g_c32.interact_ms >= kC32AfterInteractMs + 2000) {
        g_c32.client_done = true;
        log::line("card032: client done");
    }
}

void c32_step(void* engine) {
    if (!g_c32.armed) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    void* const world = g_get_game_world(engine);
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    if (pc == nullptr) {
        return;
    }
    if (now >= g_c32.next_scan_ms) {
        g_c32.next_scan_ms = now + kC32ScanMs;
        c32_scan(world, now);
    }
    void* const pawn = const_cast<void*>(c23_own_pawn(pc));
    if (pawn != nullptr && !g_c32.table_logged) {
        c32_log_table(pawn);
    }
    if (pawn != nullptr && !g_c32.view_logged && g_c32.start_ms != 0) {
        g_c32.view_logged = true;
        const Vec3 at = actor_location(pawn);
        const Vec3 cam = camera_location(pawn);
        c32_line("view: own pawn %s at z %.1f, camera component z %.1f (camera - pawn %.1f)", object_name(pawn).text,
                 at.z, cam.z, cam.z - at.z);
    }
    if (g_c32.stage == C32Stage::Drive && !g_client_session) {
        c32_drive(world, pc, now); // the PC's pawn is the mech now, so not gated on foot
        return;
    }
    if (pawn == nullptr) {
        return;
    }
    if (g_client_session) {
        c32_client_step(world, pc, pawn, now);
    } else {
        c32_host_step(world, pc, pawn, now);
    }
}

C75WorldSpawnFn g_orig_c75_world_spawn = nullptr;
C29SelfFn g_orig_c75_exit_pressed = nullptr;
C29SelfFn g_orig_c75_pc_exit = nullptr;
StaticClassFn g_actor_class = nullptr; // AActor
constexpr size_t kMechExitInputThis = 0x748;  // HandleInputExitVehicle_Pressed's this: mech + 0x748 (its body)
constexpr double kC75EnterRadius = 300.0;     // host: the client's mech location to the host's mech
constexpr uint32_t kC75Lines = 128;
uint32_t g_c75_lines = 0; // game thread

bool c75_line() {
    return note_event_cap(g_c75_lines, kC75Lines, "card075 mech events");
}

// The nearest BP_MechDropPlatform to loc in world, and its distance; null when none.
const void* c75_nearest_pad(const void* world, const Vec3& loc, double* dist) {
    *dist = -1.0;
    const void* best = nullptr;
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    for (int32_t l = 0; levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const void* const level = levels[l];
        const auto* const actors = level != nullptr ? static_cast<void* const*>(read_ptr(level, kLevelActors)) : nullptr;
        const int32_t num = level != nullptr ? read_i32(level, kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; actors != nullptr && a < num && a < kMaxLevelActors; ++a) {
            const void* const actor = actors[a];
            if (actor == nullptr || strstr(class_name(actor).text, "MechDropPlatform") == nullptr) {
                continue;
            }
            const double d = vec_dist(actor_location(actor), loc);
            if (best == nullptr || d < *dist) {
                best = actor;
                *dist = d;
            }
        }
    }
    return best;
}

// The nearest ASTPlayerMechBase to loc, and its distance; null when none.
void* c75_nearest_mech(const void* world, const Vec3& loc, double* dist) {
    return nearest_of_class(world, g_mech_class(), loc, dist);
}

// The Blueprint SpawnActor node's native thunk (UGameplayStatics::execBeginDeferredActorSpawnFromClass). It inlines
// BeginDeferredActorSpawnFromClass and calls the core UWorld::SpawnActor directly, so the pod's spawn never passes a
// hook on BeginDeferredActorSpawnFromClass (c75b4: no call seen). Its size: up to the next PDB public (0x370).
constexpr unsigned long long kC75BpSpawnThunk = offsets::UGameplayStatics_execBeginDeferredActorSpawnFromClass.rva;
constexpr unsigned long long kC75BpSpawnThunkEnd = kC75BpSpawnThunk + 0x370;

// Client: an ASTPlayerMechBase class spawned by a Blueprint is the level's pod drop (pads and pods do not replicate,
// card 032), the duplicate of the host's replicated mech. The host's mech reaches the client through the net
// driver's own spawn, not a Blueprint.
bool c75_blocks_spawn(const void* cls, unsigned long long caller) {
    if (!g_client_session || cls == nullptr || caller < kC75BpSpawnThunk || caller >= kC75BpSpawnThunkEnd) {
        return false;
    }
    const void* const cdo = read_ptr(cls, kClassDefaultObject);
    return cdo != nullptr && is_a(cdo, g_mech_class());
}

// Card 105: a class whose Blueprint spawns the host relays as WorldOp 'P' copies, so the client's own Blueprint spawns
// of it are blocked (one copy, the host's): the strikes' actors (card 104) and what a resupply pod drops, the rack and
// its pickups (none replicate). A relayed pod copy on the client therefore spawns nothing of its own.
bool c105_relayed_class(const void* cls) {
    const void* const cdo = cls != nullptr ? read_ptr(cls, kClassDefaultObject) : nullptr;
    const ObjectName on = object_name(cls);
    const char* const name = on.text;
    return cdo != nullptr && (strncmp(name, kC104StrikePrefix, sizeof(kC104StrikePrefix) - 1) == 0 ||
                              strncmp(name, kC105RackPrefix, sizeof(kC105RackPrefix) - 1) == 0 ||
                              (g_c56_pickup_class != nullptr && is_a(cdo, g_c56_pickup_class())));
}

bool c105_bp_spawn(unsigned long long caller) {
    return caller >= kC75BpSpawnThunk && caller < kC75BpSpawnThunkEnd;
}

// Card 107: a runtime pickup (an AADPickupBase class that does not replicate) is the host's from any caller, not only
// the Blueprint thunk: a dead or downed pawn's weapon drop comes through UWorld::SpawnActor(UClass*, FVector*, ...)
// (return exe+0x3DCB50E). Placed pickups load with the level and never pass this spawn.
bool c107_runtime_pickup(const void* cls) {
    const void* const cdo = cls != nullptr ? read_ptr(cls, kClassDefaultObject) : nullptr;
    return cdo != nullptr && g_c56_pickup_class != nullptr && is_a(cdo, g_c56_pickup_class()) &&
           (static_cast<const uint8_t*>(cdo)[kActorRepFlags] & 0x08u) == 0;
}

// Card 105/107: a client's own spawn of a relayed class is refused (the host's copy is the one), for a Blueprint
// spawn of any relayed class and for a runtime pickup from any caller; the host's copies (c104_client_spawn) pass.
bool c105_client_blocks(const void* cls, unsigned long long caller) {
    if (!g_client_session || g_c107_copy_spawn || !c105_relayed_class(cls)) {
        return false;
    }
    return c105_bp_spawn(caller) || c107_runtime_pickup(cls);
}

void c105_log_block(const char* what, const void* cls, const Vec3& at) {
    log::line("card105: client t=%llu blocked %s spawn of %s at (%.0f, %.0f, %.0f): the host's copy is relayed",
              static_cast<unsigned long long>(GetTickCount64()), what, object_name(cls).text, at.x, at.y, at.z);
}

SpawnWeaponPickupFn g_orig_c132_spawn_weapon_pickup = nullptr;

// Card 132: USTWeaponUtilityLibrary::SpawnWeaponPickup, a dropped weapon's pickup (RemoveWeaponFromOwnerInventory's
// drop, and the Blueprint node). It spawns the definition's pickup class (+0x120) and then writes the ammo into the
// result with no null check (SetAmmoType: `mov %rdx, 0x4d0(%rcx)`), so the null that detour_c75_world_spawn returns
// for a client's runtime pickup crashed the client (hb39: a NukeLauncher drop, EXCEPTION_ACCESS_VIOLATION writing
// 0x4D0 at exe+0x51DB560). The same client drop is refused here, before the spawn; both callers take a null result
// (RemoveWeaponFromOwnerInventory ignores it, the Blueprint node stores it). The host's drop is relayed as before.
void* detour_c132_spawn_weapon_pickup(void* context, void* def, const double* loc, const double* rot, uint64_t tag1,
                                      int32_t count1, uint64_t tag2, int32_t count2) {
    const void* const cls = def != nullptr ? read_ptr(def, kC132WeaponDefPickupClass) : nullptr;
    if (c105_client_blocks(cls, 0)) { // caller 0: not the Blueprint thunk, so the runtime-pickup rule decides
        c105_log_block("weapon pickup", cls, loc != nullptr ? Vec3{loc[0], loc[1], loc[2]} : Vec3{});
        return nullptr;
    }
    return g_orig_c132_spawn_weapon_pickup(context, def, loc, rot, tag1, count1, tag2, count2);
}

RemoveWeaponFromOwnerFn g_orig_c132_remove_weapon = nullptr;

namespace {
constexpr int32_t kC132WeaponSlots = 8;             // as card 024's slot walk
uint32_t g_c132_remove_lines = 0;                   // whole session: drops are rare

// The inventory's slots as "slot:class" words, then the equipped weapon.
void c132_inventory_text(const void* inventory, char* out, size_t size) {
    int n = snprintf(out, size, "count %d:", inventory != nullptr ? g_get_weapon_count(inventory) : -1);
    for (int32_t slot = 0; inventory != nullptr && slot < kC132WeaponSlots && n > 0 && static_cast<size_t>(n) < size;
         ++slot) {
        const void* const w = g_get_weapon_by_slot(inventory, static_cast<uint8_t>(slot));
        if (w != nullptr) {
            n += snprintf(out + n, size - n, " %d:%s", slot, class_name(w).text);
        }
    }
    if (n > 0 && static_cast<size_t>(n) < size) {
        snprintf(out + n, size - n, "; equipped %s",
                 inventory != nullptr ? class_name(g_get_equipped_weapon(inventory)).text : "none");
    }
}
} // namespace

// Card 132 (senior note 4): a dropped weapon leaves its owner's inventory here, before SpawnWeaponPickup (the client's
// pickup spawn is refused there, the removal is not). Both sides log the inventory before and after, so a client's
// drop can be compared with the host's copy of the same pawn.
bool detour_c132_remove_weapon(void* weapon, bool arg2, bool spawn_pickup) {
    void* const inventory = weapon != nullptr ? read_ptr(weapon, kWeaponOwnerInventory) : nullptr;
    char before[384] = {};
    c132_inventory_text(inventory, before, sizeof(before));
    const ObjectName name = class_name(weapon);
    const void* const weapon_class = weapon != nullptr ? read_ptr(weapon, kObjectClass) : nullptr;
    if (c463_client_skip("RemoveWeaponFromOwnerInventory", weapon_class)) {
        return false; // card 463
    }
    const bool removed = g_orig_c132_remove_weapon(weapon, arg2, spawn_pickup);
    c50_host_nudge(); // card 495
    const void* const owner = inventory != nullptr ? read_ptr(inventory, kComponentOwner) : nullptr;
    const bool script_drop = g_c267_call == C267Call::Drop || g_c267_call == C267Call::Clear; // Clear: card 463
    if (removed && script_drop && c267_source(owner)) {
        c267_mirror_drop(owner, weapon_class, arg2); // card 267
    }
    if (note_event(g_c132_remove_lines, "card132 weapon removal")) {
        char after[384] = {};
        c132_inventory_text(inventory, after, sizeof(after));
        log::line("card132: %s t=%llu RemoveWeaponFromOwnerInventory %s (%d, pickup %d) = %d, inventory %p before [%s], "
                  "after [%s]",
                  side_text(), static_cast<unsigned long long>(GetTickCount64()), name.text, arg2 ? 1 : 0,
                  spawn_pickup ? 1 : 0, removed ? 1 : 0, inventory, before, after);
    }
    return removed;
}

// Card 306: client. A native caller's spawn the card 105/107 rule refuses still runs, and its result is made inert
// in the same call and destroyed after the engine Tick of that frame (c306_step), after the caller has finished with it.
// ASTTacticalSupportPickup::PickupTacticalSupport (0x051D2970) spawns the dropped old support deferred (bDeferConstruction,
// call at exe+0x51D2E28) and calls FinishSpawning (exe+0x51D2E88) on the result with no null check; a null result
// read 0x5C of null inside FinishSpawning (playtest 2026-11-13). The Blueprint thunk (execBeginDeferredActorSpawnFromClass)
// keeps the null result: the node stores it, and FinishSpawningActor and every node after it test for None.
constexpr int32_t kC306Inert = 16;
constexpr unsigned long long kC306SwapSpawnRet = 0x51D2E2D; // PickupTacticalSupport, after its SpawnActor call
struct C306Inert {
    C31Weak actor[kC306Inert];
    void* world[kC306Inert];
    int32_t n;
    uint32_t inert_total;   // this map
    uint32_t null_returned; // this map: a native caller got null from this detour
};
C306Inert g_c306{}; // game thread
constexpr size_t kActorSetTickEnabledVfn306 = 0x4A8; // AActor::SetActorTickEnabled(bool), as turret_sync.cpp

// Client: hidden, no collision, no tick. Run at the spawn and again just before the destroy, because the caller's
// FinishSpawning (BeginPlay) can turn tick and collision back on after the spawn returns.
void c306_make_inert(void* actor) {
    g_orig_c98_hidden(actor, true);
    g_orig_set_actor_collision(actor, false);
    using SetTickFn = void (*)(void*, bool);
    reinterpret_cast<SetTickFn>(read_ptr(read_ptr(actor, 0), kActorSetTickEnabledVfn306))(actor, false);
}

// Client: the spawn runs and the result is hidden, has no collision (no interaction trace or overlap finds it) and
// no tick; it is never relayed, noted by card 098 or counted by the digest, and is destroyed by c306_step.
void* c306_inert_spawn(void* world, void* cls, const double* transform, const uint8_t* params,
                       unsigned long long caller, const Vec3& at) {
    void* const spawned = g_orig_c75_world_spawn(world, cls, transform, params);
    if (spawned == nullptr) {
        ++g_c306.null_returned;
        log::line("WARN card306: client t=%llu own spawn of %s for caller exe+0x%llX failed: null to the game caller",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(cls).text, caller);
        return nullptr;
    }
    c306_make_inert(spawned);
    ++g_c306.inert_total;
    const bool queued = g_c306.n < kC306Inert;
    if (queued) {
        g_c306.actor[g_c306.n] = C31Weak{};
        c31_weak_set(g_c306.actor[g_c306.n], spawned);
        g_c306.world[g_c306.n++] = world;
    }
    log::line("%scard306: client t=%llu inert own spawn of %s at (%.0f, %.0f, %.0f) for caller exe+0x%llX, %s",
              queued ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()), object_name(cls).text, at.x,
              at.y, at.z, caller, queued ? "destroyed at frame end" : "queue full (kC306Inert): left hidden");
    return spawned;
}

// Called from ubw_post_tick (hooks_ubw.cpp), after UEngine::Tick has returned in the frame of the spawn, so after the
// caller's FinishSpawning and BeginPlay. Each inert spawn is made inert again (BeginPlay may have enabled its tick or
// collision) and destroyed past the card 098 destroy detour.
void c306_step() {
    for (int32_t i = 0; i < g_c306.n; ++i) {
        void* const actor = c31_weak_get(g_c306.actor[i]);
        if (actor != nullptr) {
            c306_make_inert(actor);
            g_orig_c98_destroy(g_c306.world[i], actor, false, true);
        }
    }
    g_c306.n = 0;
}

// Both sides at LoadMap enter: the per-map tally (target 0 for the second count).
void c306_map_end() {
    // b8: printed every map, zero counts too, so a run proves the tally ran (a missing row was ambiguous).
    log::line("card306: inert spawns %u, null returned to game callers %u", g_c306.inert_total, g_c306.null_returned);
    g_c306 = C306Inert{};
}

// UWorld::SpawnActor(UClass*, const FTransform*, const FActorSpawnParameters&), the core spawn. A blocked Blueprint
// spawn returns null, which the thunk stores as the node's result as on a failed spawn; the pod goes on without a
// mech. Card 107: a client's own runtime pickup is blocked from any caller; the host's copies (c104_client_spawn)
// pass. Card 306: a blocked native caller gets an inert actor, never null.
void* detour_c75_world_spawn(void* world, void* cls, const double* transform, const uint8_t* params) {
    const unsigned long long caller = exe_rva(_ReturnAddress());
    const Vec3 at = read_vec3(transform, kTransformTranslation);
    const bool relayed = c105_relayed_class(cls);
    const bool pickup = relayed && c107_runtime_pickup(cls);
    if (c105_client_blocks(cls, caller)) {
        const bool bp = c105_bp_spawn(caller);
        c105_log_block(bp ? "Blueprint" : "runtime pickup", cls, at);
        return bp ? nullptr : c306_inert_spawn(world, cls, transform, params, caller, at);
    }
    if (!c75_blocks_spawn(cls, caller)) {
        alignas(16) double moved[12];
        transform = c46_spawn_spot(world, cls, transform, caller, moved); // card 046: no player spawn at the origin
        void* const spawned = g_orig_c75_world_spawn(world, cls, transform, params);
        c98_note(C98Kind::Spawn, spawned, nullptr, caller); // card 098 measurement
        const bool strike = strncmp(object_name(cls).text, kC104StrikePrefix, sizeof(kC104StrikePrefix) - 1) == 0;
        if (spawned != nullptr && relayed && (strike || pickup || c105_bp_spawn(caller))) {
            if (pickup && !c105_bp_spawn(caller) && !g_client_session) {
                char chain[kMaxStackFrames * 24] = {};
                caller_chain(chain, sizeof(chain), 6);
                log::line("card107: host t=%llu runtime pickup %s at (%.0f, %.0f, %.0f), callers %s",
                          static_cast<unsigned long long>(GetTickCount64()), object_name(cls).text, at.x, at.y, at.z,
                          chain);
            }
            c104_relay_spawn(spawned, at); // card 104/105: still unplaced here
            if (caller == kC306SwapSpawnRet && !g_client_session) {
                const bool fresh = GetTickCount64() - g_c306_equip.ms < 1000;
                log::line("card306: host t=%llu support swap for %s: %s dropped as %u, %s equipped",
                          static_cast<unsigned long long>(GetTickCount64()), fresh ? g_c306_equip.who : "?",
                          object_name(cls).text, static_cast<uint32_t>(object_index(spawned)),
                          fresh ? g_c306_equip.config : "? (no PickupTacticalSupport equip within 1 s)");
            }
        }
        return spawned;
    }
    if (!c105_bp_spawn(caller)) {
        ++g_c306.null_returned; // card 306: a native caller of a blocked mech pod class
    }
    double pad_dist = -1.0;
    const void* const pad = c75_nearest_pad(world, at, &pad_dist);
    log::line("card075: client blocked Blueprint spawn of %s at (%.0f, %.0f, %.0f); nearest pad %s at %.0f units; "
              "the host's mech replicates", object_name(cls).text, at.x, at.y, at.z, object_name(pad).text, pad_dist);
    return nullptr;
}

// One enter or leave state line: the pawn, its controller, the mech's weak PC and controller, and the PC's pawn.
void c75_mech_line(const char* what, const char* when, const void* mech, const void* pawn) {
    if (!c75_line()) {
        return;
    }
    const auto* const m = static_cast<const uint8_t*>(mech);
    const void* const pc = g_weak_get(m + kMechController);
    const void* const pawn_pc = pawn != nullptr ? read_ptr(pawn, kPawnController) : nullptr;
    log::line("card075: %s %s %s: mech %s (roles %u/%u) controller %s, weak PC %s (its pawn %s); pawn %s (%s) "
              "controller %s (its pawn %s), bHidden %d, attached to %s",
              side(), what, when, object_name(mech).text, m[kActorLocalRole], m[kActorRemoteRole],
              object_name(read_ptr(mech, kPawnController)).text, object_name(pc).text,
              object_name(pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr).text, object_name(pawn).text,
              pawn != nullptr ? kSprintSideText[sprint_side(pawn)] : "-", object_name(pawn_pc).text,
              object_name(pawn_pc != nullptr ? read_ptr(pawn_pc, kControllerPawn) : nullptr).text,
              c29_bool(pawn, kC29Hidden),
              object_name(pawn != nullptr ? g_get_attach_parent_actor(pawn) : nullptr).text);
    const uint8_t* prop = nullptr;
    const void* const body = prop_object_field(mech, "Mech", &prop);
    log::line("card075: %s   mech bHidden %d, body %s bHiddenInGame %d", side(), c29_bool(mech, kC29Hidden),
              object_name(body).text, body != nullptr ? c29_bool(body, kC33Hidden) : -1);
}

// Client: the soldier and PC behind the last MechEnter or MechLeave, the mech it pilots (its autonomous proxy, seen
// at 1 Hz), and the mech a MechLeave went out for. Identity only: the "after" lines read the mech while it is in the
// mech list. IA_ExitVehicle repeats while E stays down, so MechLeave goes out once per possession.
// Card 331: soldier and pc are read on later ticks (the "after" lines), so they are weak: a soldier or PC freed in
// between (a map end, a respawn) reads null. mech is compared only, and named only while it is still in the list.
struct C75Client {
    C31Weak soldier;
    C31Weak pc;
    const void* mech;
    const void* leave_sent;
};
C75Client g_c75_client{}; // game thread

// Client: the location of the mech in a MechEnter or MechLeave frame to the host.
void c75_send(ChanOp op, const char* what, const void* mech, void* pc) {
    const Vec3 at = actor_location(mech);
    char text[96];
    snprintf(text, sizeof(text), "%.0f %.0f %.0f", at.x, at.y, at.z);
    static wchar_t wire[kChanTextFrame];
    const WireString w = chan_text_frame(op, text, wire);
    const bool sent = w.num > 0 && pc != nullptr && chan_send_host(pc, w);
    if (sent) {
        c303_press(what); // card 303: the mount row's start
    }
    log::line("card075: client t=%llu %s on %s at (%s): %s", static_cast<unsigned long long>(GetTickCount64()), what,
              object_name(mech).text, text, sent ? "sent" : "WARN not sent");
}

// `iface` is the mech's interactable subobject; the interactor component's outer is its pawn. Client: a host mech
// (simulated proxy) is entered on the host only. The client's own run (c75a: EnterMech on a proxy sets the weak PC,
// hides and attaches the soldier locally) left the client's input dead in the mech once the host's possession came.
void detour_c75_mech_on_interaction(void* iface, void* interactor) {
    const unsigned long long caller = exe_rva(_ReturnAddress());
    void* const mech = static_cast<uint8_t*>(iface) - kMechInteractableIface;
    const void* const pawn = interactor != nullptr ? read_ptr(interactor, kObjectOuter) : nullptr;
    if (g_client_session) {
        const uint8_t role = static_cast<const uint8_t*>(mech)[kActorLocalRole];
        if (role == kRoleSimulatedProxy) {
            void* const pc = pawn != nullptr ? read_ptr(pawn, kPawnController) : nullptr;
            c75_mech_line("MechEnter", "before", mech, pawn);
            c31_weak_set(g_c75_client.soldier, const_cast<void*>(pawn));
            c31_weak_set(g_c75_client.pc, pc);
            c75_send(ChanOp::MechEnter, "MechEnter", mech, pc);
            return;
        } else {
            log::line("WARN card075: client OnInteraction on %s with local role %u: not the host's mech, not sent",
                      object_name(mech).text, role);
        }
    }
    c32_mech_event("OnInteraction", "before", mech, pawn, caller);
    g_orig_mech_on_interaction(iface, interactor);
    c32_mech_event("OnInteraction", "after", mech, pawn, caller);
}

// Card 452 (client, always on). ASTPlayerMechBase::CanInteract (exe+0x50E0560) refuses a mech whose
// APawn::Controller (+0x2D8) is valid. A controller is replicated to its owner only, so on the client a mech the
// host pilots has none and the HUD offered ENTER on it (the host then ignored the MechEnter, "already piloted").
// APawn::PlayerState (+0x2C8, APawn::SetPlayerState) is replicated to everyone and is set while a player
// possesses the mech: with one set, the client's answer is no.
constexpr uint32_t kC452Rows = 4; // rows per process
C452CanInteractFn g_orig_c452_mech_can_interact = nullptr;
uint32_t g_c452_refused = 0;

bool detour_c452_mech_can_interact(void* iface, void* interactor, void* failed) {
    if (g_client_session && iface != nullptr) {
        const void* const mech = static_cast<const uint8_t*>(iface) - kMechInteractableIface;
        const void* const ps = read_ptr(mech, kPawnPlayerState);
        if (ps != nullptr) {
            if (g_c452_refused++ < kC452Rows) {
                log::line("card452: client CanInteract on %s refused: piloted (player state %s)",
                          object_name(mech).text, object_name(ps).text);
            }
            return false;
        }
    }
    return g_orig_c452_mech_can_interact(iface, interactor, failed);
}

// Client: one MechLeave per possession of `mech`; the held E's repeats after it are dropped here.
void c75_client_leave(void* mech, void* pc) {
    if (g_c75_client.leave_sent == mech) {
        return;
    }
    c75_mech_line("MechLeave", "before", mech, c31_weak_get(g_c75_client.soldier));
    c31_weak_set(g_c75_client.pc, pc);
    g_c75_client.leave_sent = mech;
    c75_send(ChanOp::MechLeave, "MechLeave", mech, pc);
}

// Client: the exit input of the mech it drives goes to the host, and the local run (RequestLeaveMech on a client,
// which cannot unpossess) is skipped. Host and standalone: the game's own run.
void detour_c75_exit_pressed(void* self) {
    void* const mech = static_cast<uint8_t*>(self) - kMechExitInputThis;
    if (!g_client_session) {
        g_orig_c75_exit_pressed(self);
        return;
    }
    c75_client_leave(mech, read_ptr(mech, kPawnController));
}

// ASTPlayerController::HandleInputExitVehicle_Pressed (IA_ExitVehicle): the PC hands the press to its input
// receiver (+0x7C0). Logged on both sides. Client in a mech: the press goes to the host as MechLeave and the local
// run is skipped (its mech handler needs the mech's weak PC, which only the host sets). Otherwise the game's own run.
// IA_ExitVehicle is a hold: it fires every tick while E stays down, also on foot after the leave, so only presses in
// a mech are logged (on-foot ones would use up the card075 line cap).
void detour_c75_pc_exit(void* pc) {
    void* const pawn = read_ptr(pc, kControllerPawn);
    const bool mech = pawn != nullptr && is_a(pawn, g_mech_class());
    if (mech && c75_line()) {
        log::line("card075: %s PC exit input: pawn %s, receiver %p (pawn + 0x748 %p)", side(), object_name(pawn).text,
                  read_ptr(pc, kPcInputReceiver),
                  static_cast<const void*>(static_cast<const uint8_t*>(pawn) + kMechExitInputThis));
    }
    if (!g_client_session || !mech) {
        g_orig_c75_pc_exit(pc);
        return;
    }
    c75_client_leave(pawn, pc);
}

C75Pilot g_c75_pilots[kC75Pilots]{}; // game thread

// Card 327: the soldier piloting `mech` now (host pilot slots), or null (not a piloted mech).
const void* c75_soldier_of(const void* mech) {
    for (int i = 0; mech != nullptr && i < kC75Pilots; ++i) {
        if (g_c75_pilots[i].mech == mech) {
            return g_c75_pilots[i].soldier;
        }
    }
    return nullptr;
}

// Card 300 (b180): the soldier riding inside `pawn` when it is a live mech a player pilots (host pilot slots), else
// null. The game reports a presence or volume overlap for that soldier, not for the mech (c423b179b: the client's
// mech 70 uu from a presence, IsOverlappingActor(presence, mech) false, the edge came from the soldier). The slots
// hold raw pointers (card 450), so the soldier must still be in the object list.
const void* c75_pilot_body(const void* pawn) {
    const bool is_mech = pawn != nullptr && g_mech_class != nullptr && is_a(pawn, g_mech_class());
    const void* const body = is_mech ? c75_soldier_of(pawn) : nullptr;
    return body != nullptr && object_listed(body) ? body : nullptr;
}

void c75_remember_pilot(const void* mech, const void* soldier) {
    int slot = -1; // this mech's slot, else the first free one
    for (int i = 0; i < kC75Pilots; ++i) {
        if (g_c75_pilots[i].mech == mech) {
            slot = i;
            break;
        }
        if (slot < 0 && g_c75_pilots[i].mech == nullptr) {
            slot = i;
        }
    }
    if (slot < 0) {
        log::line("WARN card075: no pilot slot for %s; slot 0 reused", object_name(mech).text);
        slot = 0;
    }
    g_c75_pilots[slot] = C75Pilot{mech, soldier};
}

// Card 132: at map load the pilot slots and the client's mech identity name the old world's pawns and PC; a
// stale mech match after the load would hand a freed soldier back (the hb57 crash class). Both sides.
void c75_map_end() {
    memset(g_c75_pilots, 0, sizeof(g_c75_pilots));
    g_c75_client = C75Client{};
}

// Card 323 "player" join replay, seats (host): not settled. No host-to-client seat op exists (MechEnter, MechLeave,
// TurretMount and TurretLeave go client to host only), and the card's step 1 table of what engine replication carries
// for a seat is not filled in. Until that is decided this only counts the mech pilot rows a joiner would need (the
// rows hold raw pointers, so nothing is read through them).
void c75_join_replay(void* pc) {
    int32_t seated = 0;
    for (int i = 0; i < kC75Pilots; ++i) {
        seated += g_c75_pilots[i].mech != nullptr ? 1 : 0;
    }
    log::line("card323: host t=%llu seat join replay to %s: %d mech pilot row(s), not sent (no seat op yet)",
              static_cast<unsigned long long>(GetTickCount64()), object_name(pc).text, seated);
}

// The PC's pawn once the leave is through (the soldier again), else `fallback`.
const void* c75_after_leave_pawn(const void* pc, const void* fallback) {
    const void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    return pawn != nullptr ? pawn : fallback;
}

// Card 255: the mech inside the EnterMech now running (game thread), for the mirror line; nullptr outside one.
const void* g_c255_boarding_mech = nullptr;

// Host: the game broadcasts OnWantsToBoardMech on the boarding pawn's own PC, so a client's boarding never reaches
// the level script bound to the host's local PC (FED_C2M1 LOS_O05_SO01). Mirror it there, once, at the same point.
void detour_c255_start_boarding(void* pc) {
    g_orig_c255_start_boarding(pc);
    if (g_client_session || pc == nullptr || is_local_pc(pc)) {
        return;
    }
    void* const world = g_actor_get_world(pc);
    void* const host_pc = world != nullptr ? own_pc(world) : nullptr;
    if (host_pc == nullptr || host_pc == pc || !is_a(host_pc, g_st_pc_class()) || !is_local_pc(host_pc)) {
        log::line("WARN card255: host OnWantsToBoardMech for %s not mirrored: host PC %s", object_name(pc).text,
                  object_name(host_pc).text);
        return;
    }
    log::line("card255: host t=%llu OnWantsToBoardMech mirrored to %s for %s, mech %s",
              static_cast<unsigned long long>(GetTickCount64()), object_name(host_pc).text, object_name(pc).text,
              object_name(g_c255_boarding_mech).text);
    g_orig_c255_start_boarding(host_pc);
}

void detour_c75_enter_mech(void* mech, void* pawn) {
    const unsigned long long caller = exe_rva(_ReturnAddress());
    if (c79_refuse_enter(mech, pawn)) {
        return;
    }
    c29_vehicle_event("EnterMech", "before", mech, pawn);
    c32_mech_event("EnterMech", "before", mech, pawn, caller);
    c75_mech_line("EnterMech", "before", mech, pawn);
    g_c255_boarding_mech = mech;
    g_orig_enter_mech(mech, pawn);
    g_c255_boarding_mech = nullptr;
    c29_vehicle_event("EnterMech", "after", mech, pawn);
    c32_mech_event("EnterMech", "after", mech, pawn, caller);
    c75_mech_line("EnterMech", "after", mech, pawn);
    c75_remember_pilot(mech, pawn);
    c79_note_enter(mech);
    c321_pilot_check(mech); // b181 diagnostic: after c75_remember_pilot, so c75_pilot_body sees the new pilot
}

void detour_c75_leave_mech(void* mech) {
    const unsigned long long caller = exe_rva(_ReturnAddress());
    const void* const pilot = c29_controller(mech);
    const void* const pc = g_weak_get(static_cast<const uint8_t*>(mech) + kMechController);
    const void* const soldier = c75_leave_soldier(mech);
    c29_vehicle_event("LeaveMech", "before", mech, pilot);
    c32_mech_event("LeaveMech", "before", mech, pilot, caller);
    c75_mech_line("LeaveMech", "before", mech, soldier);
    g_orig_leave_mech(mech);
    c29_vehicle_event("LeaveMech", "after", mech, pilot);
    c32_mech_event("LeaveMech", "after", mech, pilot, caller);
    c75_mech_line("LeaveMech", "after", mech, c75_after_leave_pawn(pc, soldier));
}

constexpr double kC75MoveLog = 100.0; // a piloted mech's location is logged again after this many units
constexpr uint32_t kC75MoveLines = 64;

C75Count g_c75c{}; // game thread
constexpr uint32_t kC75CmcLines = 24;

// Host, 1 Hz while a client pilots the mech (remote role 2): the server copy's movement state, to show whether the
// client's moves drive it.
void c75_server_cmc_line(const void* mech) {
    const auto* const m = static_cast<const uint8_t*>(mech);
    const auto* const cmc = static_cast<const uint8_t*>(read_ptr(mech, kCharacterMovement));
    if (g_client_session || m[kActorRemoteRole] != kRoleAutonomousProxy || cmc == nullptr ||
        !note_event_cap(g_c75c.cmc_lines, kC75CmcLines, "card075 server mech CMC")) {
        return;
    }
    const Vec3 v = read_vec3(cmc, kMoveVelocity);
    const Vec3 a = read_vec3(cmc, kCmcAcceleration);
    log::line("card075: host server mech CMC: MovementMode %u, MaxWalkSpeed %.1f, Velocity (%.1f, %.1f, %.1f), "
              "Acceleration (%.1f, %.1f, %.1f), floor flags 0x%X", cmc[kCmcMovementMode],
              static_cast<double>(read_f32(cmc, kCmcMaxWalkSpeed)), v.x, v.y, v.z, a.x, a.y, a.z,
              read_i32(cmc, kCmcCurrentFloorFlags));
}


// Card 258: a mech's presence-overlap state for the card 075 piloted mech row. BP_BaseMech Set3DMeshState gives the
// capsule collision 3 and the mesh overlap events only when USTCombatUtilityLibrary::IsPlayer is true. Writes
// "capsule collision <n>, mesh overlap events <0|1>, IsPlayer <0|1>" (-1: not readable).
IsPlayerFn g_c258_is_player = nullptr;
constexpr size_t kC258CharacterMesh = 0x328; // ACharacter::Mesh (claim_c157.cpp kCharacterMesh)

int c258_overlap_events(const void* mesh) {
    static const uint8_t* prop = nullptr; // game thread only
    static bool tried = false;
    if (mesh == nullptr) {
        return -1;
    }
    if (!tried) {
        tried = true;
        static constexpr const char* kNames[] = {"bGenerateOverlapEvents", nullptr};
        const uint8_t* const p = c27_find_any(read_ptr(mesh, kObjectClass), kNames);
        prop = prop_kind_is(p, "BoolProperty") && prop_bool_ok(p) ? p : nullptr;
        if (prop == nullptr) {
            log::line("WARN card258: bGenerateOverlapEvents not found on %s", object_name(read_ptr(mesh, kObjectClass)).text);
        }
    }
    return prop != nullptr ? (c29_flag(static_cast<const uint8_t*>(mesh), prop) ? 1 : 0) : -1;
}

void c258_mech_overlap_text(const void* mech, char* out, size_t cap) {
    const void* const capsule = read_ptr(mech, kActorRootComponent);
    const int collision = capsule != nullptr && g_collision_enabled != nullptr ? g_collision_enabled(capsule) : -1;
    const int is_player = g_c258_is_player != nullptr ? (g_c258_is_player(mech) ? 1 : 0) : -1;
    snprintf(out, cap, "capsule collision %d, mesh overlap events %d, IsPlayer %d", collision,
             c258_overlap_events(read_ptr(mech, kC258CharacterMesh)), is_player);
}

// The first mech in the list with a controller: its location, when it moved kC75MoveLog units since the last line
// (or another mech is piloted now). Shows that a pilot's input drives it, on the pilot's side and the other.
void c75_piloted_step(const C23List& list) {
    for (uint32_t i = 0; i < list.count; ++i) {
        const void* const m = list.e[i].actor;
        const void* const pc = read_ptr(m, kPawnController);
        if (pc == nullptr) {
            continue;
        }
        c75_server_cmc_line(m);
        if (m == g_c75c.piloted && vec_dist(list.e[i].loc, g_c75c.piloted_at) < kC75MoveLog) {
            return;
        }
        if (note_event_cap(g_c75c.move_lines, kC75MoveLines, "card075 piloted mech")) {
            char overlap[96];
            c258_mech_overlap_text(m, overlap, sizeof(overlap));
            log::line("card075: %s piloted mech %s (controller %s) at (%.0f, %.0f, %.0f); card258: %s", side(),
                      object_name(m).text, object_name(pc).text, list.e[i].loc.x, list.e[i].loc.y, list.e[i].loc.z,
                      overlap);
        }
        g_c75c.piloted = m;
        g_c75c.piloted_at = list.e[i].loc;
        return;
    }
    g_c75c.piloted = nullptr;
}

// Client: the "after" line of an enter or leave, when the mech it pilots (autonomous proxy) changes. Enter: the mech
// with the soldier that sent MechEnter. Leave: the old mech (still in the list) with the PC's new pawn.
void c75_client_possess_step(const C23List& list) {
    const void* now = nullptr;
    bool old_listed = false;
    for (uint32_t i = 0; i < list.count; ++i) {
        const auto* const m = static_cast<const uint8_t*>(list.e[i].actor);
        now = m[kActorLocalRole] == kRoleAutonomousProxy ? m : now;
        old_listed = old_listed || m == g_c75_client.mech;
    }
    if (now == g_c75_client.mech) {
        return;
    }
    if (g_c75_client.mech != nullptr && old_listed) {
        const void* const pc = c31_weak_get(g_c75_client.pc);
        c75_mech_line("MechLeave", "after", g_c75_client.mech, pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr);
    } else if (g_c75_client.mech != nullptr) {
        log::line("WARN card075: client lost mech %p from the list (destroyed?); no leave line", g_c75_client.mech);
    }
    if (now != nullptr) {
        c75_mech_line("MechEnter", "after", now, c31_weak_get(g_c75_client.soldier));
    }
    g_c75_client.mech = now;
    g_c75_client.leave_sent = nullptr;
}

// Host: the frame's mech location, or false (logged) for a bad frame.
bool c75_frame_loc(const WireString* s, const char* what, Vec3* at) {
    char text[kChanTextMax + 1];
    if (chan_text(s, text, sizeof(text)) <= 0 || sscanf_s(text, "%lf %lf %lf", &at->x, &at->y, &at->z) != 3) {
        log::line("WARN card075: host: bad %s frame", what);
        return false;
    }
    return true;
}

// Host: the sender's server copy on foot (alive, not downed), or null with the reason logged.
void* c75_sender_copy(void* pc, const char* what) {
    void* const pawn = read_ptr(pc, kControllerPawn);
    void* health_component = nullptr;
    const char* why = nullptr;
    if (!is_a(pawn, g_player_mi_class()) || sprint_side(pawn) != kSideServerCopy) {
        why = "the PC has no server copy on foot";
    } else if (c21_health(static_cast<const uint8_t*>(pawn), &health_component) <= 0.0f) {
        why = "its copy is dead";
    } else if (c43_is_down(pawn)) {
        why = "its copy is downed";
    }
    if (why != nullptr) {
        log::line("WARN card075: host %s from %s ignored: %s (PC pawn %s, %s)", what, object_name(pc).text, why,
                  object_name(pawn).text, class_name(pawn).text);
        return nullptr;
    }
    return pawn;
}

void c75_receive_enter(void* pc, const WireString* s) {
    Vec3 at{};
    void* const copy = c75_frame_loc(s, "MechEnter", &at) ? c75_sender_copy(pc, "MechEnter") : nullptr;
    void* const interactor = copy != nullptr ? read_ptr(copy, kCharacterInteractor) : nullptr;
    if (interactor == nullptr) {
        return;
    }
    double dist = -1.0;
    void* const mech = c75_nearest_mech(g_actor_get_world(copy), at, &dist);
    const void* const pilot_pc = mech != nullptr ? g_weak_get(static_cast<const uint8_t*>(mech) + kMechController) : nullptr;
    if (mech == nullptr || dist > kC75EnterRadius || pilot_pc != nullptr) {
        log::line("WARN card075: host MechEnter from %s at (%.0f, %.0f, %.0f) ignored: nearest mech %s at %.0f units%s",
                  object_name(pc).text, at.x, at.y, at.z, object_name(mech).text, dist,
                  pilot_pc != nullptr ? ", already piloted" : "");
        return;
    }
    log::line("card075: host MechEnter from %s: mech %s at %.0f units from the client's; OnInteraction with copy %s",
              object_name(pc).text, object_name(mech).text, dist, object_name(copy).text);
    c75_mech_line("MechEnter", "before", mech, copy);
    g_mech_on_interaction(static_cast<uint8_t*>(mech) + kMechInteractableIface, interactor);
    c75_mech_line("MechEnter", "after", mech, copy);
    c298_mount_row("host enter", mech); // card 298 (diagnostic): the host's Fuel and MaximumFuel for the same mech
}

void c75_receive_leave(void* pc, const WireString* s) {
    Vec3 at{};
    if (!c75_frame_loc(s, "MechLeave", &at)) {
        return;
    }
    void* const mech = read_ptr(pc, kControllerPawn);
    const bool drives = is_a(mech, g_mech_class()) &&
                        g_weak_get(static_cast<const uint8_t*>(mech) + kMechController) == pc;
    if (!drives) {
        log::line("WARN card075: host MechLeave from %s ignored: its pawn %s (%s) is not a mech it drives",
                  object_name(pc).text, object_name(mech).text, class_name(mech).text);
        return;
    }
    const void* const soldier = c75_leave_soldier(mech);
    log::line("card075: host MechLeave from %s: mech %s, %.0f units from the client's; RequestLeaveMech",
              object_name(pc).text, object_name(mech).text, vec_dist(actor_location(mech), at));
    c75_mech_line("MechLeave", "before", mech, soldier);
    g_mech_request_leave(mech);
    c75_mech_line("MechLeave", "after", mech, c75_after_leave_pawn(pc, soldier));
}

// ---- Card 123: turrets run on the host, as card 075 mechs do ----
// The turret's interactable subobject (kTurretInteractableIface) and MountTurret's in-use byte (`mov byte [rbx+0x848],
// 1` at exe+0x51CE7FD; LeaveTurret clears the word there at exe+0x51CD979). MountTurret possesses the turret with the
// pawn's controller (AController::Possess at exe+0x51CE849), so a host-run mount for a client's server copy hands the
// client's own controller the turret, and that possession replicates.
constexpr size_t kC123TurretInUse = 0x848;
constexpr ULONGLONG kC123LeaveRepeatMs = 1000; // client: a second leave press within this is dropped
constexpr ULONGLONG kC123MountRepeatMs = 1000; // client: OnInteraction runs twice per press (c123aa); one TurretMount
C29InteractFn g_orig_turret_on_interaction = nullptr;
ULONGLONG g_c123_leave_ms = 0; // game thread (client)
ULONGLONG g_c123_mount_ms = 0; // game thread (client)

// Client: a turret's OnInteraction goes to the host as TurretMount and never runs locally (a local mount attaches the
// soldier without possession; its leave then crashes). Host and standalone: the game's own run.
void detour_c123_turret_on_interaction(void* iface, void* interactor) {
    if (!g_client_session) {
        g_orig_turret_on_interaction(iface, interactor);
        return;
    }
    void* const turret = static_cast<uint8_t*>(iface) - kTurretInteractableIface;
    const void* const pawn = interactor != nullptr ? read_ptr(interactor, kObjectOuter) : nullptr;
    void* const pc = pawn != nullptr ? read_ptr(pawn, kPawnController) : nullptr;
    const uint8_t role = static_cast<const uint8_t*>(turret)[kActorLocalRole];
    log::line("card123: client t=%llu turret OnInteraction on %s (local role %u) by %s: local run skipped",
              static_cast<unsigned long long>(GetTickCount64()), object_name(turret).text, role, object_name(pawn).text);
    if (role != kRoleSimulatedProxy) {
        log::line("WARN card123: client: %s is not the host's turret (role %u), no TurretMount sent",
                  object_name(turret).text, role);
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (g_c123_mount_ms != 0 && now - g_c123_mount_ms < kC123MountRepeatMs) {
        return; // the same press's second OnInteraction: its TurretMount already went
    }
    g_c123_mount_ms = now;
    g_c123_leave_ms = 0;
    c75_send(ChanOp::TurretMount, "TurretMount", turret, pc);
}

// Client Use press on a turret: what the turret's CanInteract (the interface's vtable +0x30, through
// c133_can_interact; card 133: +0x28 is TryInteract, which Interact calls and which starts the interaction) reads: the in-use byte, the weak ref at
// turret +0x688 (exe+0x51C16B1) and the two root yaws its facing cone compares. One line per press.
constexpr size_t kC123TurretWeak = 0x688;
void c123_client_press(void* interactor) {
    void* const iface = interactor != nullptr ? read_ptr(interactor, kInteractorTarget) : nullptr;
    auto* const turret = static_cast<uint8_t*>(const_cast<void*>(iface_object(iface)));
    if (turret == nullptr || !is_a(turret, g_turret_class())) {
        return;
    }
    const void* const pawn = read_ptr(interactor, kObjectOuter);
    char why[kChanTextMax + 1];
    const bool can = c133_can_interact(iface, interactor, why, sizeof(why)); // interface vtable +0x30 (card 133)
    const void* const pawn_root = pawn != nullptr ? read_ptr(pawn, kActorRootComponent) : nullptr;
    const void* const turret_root = read_ptr(turret, kActorRootComponent);
    char name[kChanNameMax + 1];
    c50_name(turret, name, sizeof(name));
    const Vec3 at = actor_location(turret);
    log::line("card123: client t=%llu Use press on %s at (%.0f, %.0f, %.0f): CanInteract %d (in use %u, +0x688 %08x "
              "%08x, pawn yaw %.1f, turret yaw %.1f)",
              static_cast<unsigned long long>(GetTickCount64()), name, at.x, at.y, at.z,
              can ? 1 : 0, turret[kC123TurretInUse],
              *reinterpret_cast<const uint32_t*>(turret + kC123TurretWeak),
              *reinterpret_cast<const uint32_t*>(turret + kC123TurretWeak + 4),
              pawn_root != nullptr ? component_yaw(pawn_root) : 0.0,
              turret_root != nullptr ? component_yaw(turret_root) : 0.0);
}

// Client: the leave press of the turret it drives (autonomous proxy) goes to the host as TurretLeave, once per
// kC123LeaveRepeatMs. Any other local leave is dropped and logged.
void c123_client_leave(void* turret) {
    const uint8_t role = static_cast<const uint8_t*>(turret)[kActorLocalRole];
    void* const pc = read_ptr(turret, kPawnController);
    const ULONGLONG now = GetTickCount64();
    if (role != kRoleAutonomousProxy || pc == nullptr) {
        log::line("WARN card123: client t=%llu LeaveTurret on %s (role %u, controller %s) dropped: not driven here",
                  static_cast<unsigned long long>(now), object_name(turret).text, role, object_name(pc).text);
        return;
    }
    if (g_c123_leave_ms != 0 && now - g_c123_leave_ms < kC123LeaveRepeatMs) {
        return;
    }
    g_c123_leave_ms = now;
    g_c123_mount_ms = 0; // a mount right after this leave is a new press
    c75_send(ChanOp::TurretLeave, "TurretLeave", turret, pc);
}

// Host: the nearest ASTMountableTurret to `loc`, or null; `dist` gets its distance.
void* c123_nearest_turret(const void* world, const Vec3& loc, double* dist) {
    static C23List list;
    c23_collect(world, nullptr, g_turret_class(), list);
    void* best = nullptr;
    *dist = -1.0;
    for (uint32_t i = 0; i < list.count; ++i) {
        const double d = vec_dist(list.e[i].loc, loc);
        if (best == nullptr || d < *dist) {
            best = list.e[i].actor;
            *dist = d;
        }
    }
    return best;
}

// Host: TurretMount from a client. Its server copy (on foot, alive, not downed) mounts the turret nearest the client's
// location, through the turret's own OnInteraction, when that turret is within kC75EnterRadius and free.
void c123_receive_mount(void* pc, const WireString* s) {
    Vec3 at{};
    void* const copy = c75_frame_loc(s, "TurretMount", &at) ? c75_sender_copy(pc, "TurretMount") : nullptr;
    void* const interactor = copy != nullptr ? read_ptr(copy, kCharacterInteractor) : nullptr;
    if (interactor == nullptr) {
        return;
    }
    double dist = -1.0;
    void* const turret = c123_nearest_turret(g_actor_get_world(copy), at, &dist);
    const bool used = turret != nullptr && (static_cast<const uint8_t*>(turret)[kC123TurretInUse] != 0 ||
                                            read_ptr(turret, kPawnController) != nullptr);
    if (turret == nullptr || dist > kC75EnterRadius || used) {
        log::line("WARN card123: host TurretMount from %s at (%.0f, %.0f, %.0f) ignored: nearest turret %s at %.0f "
                  "units%s",
                  object_name(pc).text, at.x, at.y, at.z, object_name(turret).text, dist, used ? ", in use" : "");
        return;
    }
    g_orig_turret_on_interaction(static_cast<uint8_t*>(turret) + kTurretInteractableIface, interactor);
    log::line("card123: host TurretMount from %s: turret %s at %.0f units from the client's; OnInteraction with copy "
              "%s; PC pawn now %s, turret in use %u",
              object_name(pc).text, object_name(turret).text, dist, object_name(copy).text,
              object_name(read_ptr(pc, kControllerPawn)).text, static_cast<const uint8_t*>(turret)[kC123TurretInUse]);
}

// Host: TurretLeave from a client. Runs the game's LeaveTurret on the turret that client's PC possesses.
void c123_receive_leave(void* pc, const WireString* s) {
    Vec3 at{};
    if (!c75_frame_loc(s, "TurretLeave", &at)) {
        return;
    }
    void* const turret = read_ptr(pc, kControllerPawn);
    if (!is_a(turret, g_turret_class()) || read_ptr(turret, kPawnController) != pc) {
        log::line("WARN card123: host TurretLeave from %s ignored: its pawn %s (%s) is not a turret it drives",
                  object_name(pc).text, object_name(turret).text, class_name(turret).text);
        return;
    }
    c24_release_pawn(turret, "release (TurretLeave)"); // card 125: a held relay on the turret goes before its rider
    detour_leave_turret(turret);
    log::line("card123: host TurretLeave from %s: turret %s, %.0f units from the client's; LeaveTurret; PC pawn now %s, "
              "turret in use %u",
              object_name(pc).text, object_name(turret).text, vec_dist(actor_location(turret), at),
              object_name(read_ptr(pc, kControllerPawn)).text, static_cast<const uint8_t*>(turret)[kC123TurretInUse]);
}

// Card 447, host: APlayerController::PawnLeavingGame (exe+0x4055780) destroys the leaving PC's pawn (PC +0x2E8),
// and that pawn is the mech or turret while the player sits in one (the game has no override). It runs from the
// PC's Destroyed, before GameMode Logout (run c447b147b: the mech was gone 1 ms before the Logout hook). The
// game's own leave runs first here, so the soldier is the pawn that goes and the seat stays for the others.
C29SelfFn g_orig_c447_pawn_leaving = nullptr;
void c447_seat_leave(void* pc);

// Also the pawn half of the old Logout work (cards 024, 448, 043): at Logout the PC's pawn is already null.
void detour_c447_pawn_leaving(void* pc) {
    if (!g_client_session && pc != nullptr && !is_local_pc(pc)) {
        c447_seat_leave(pc);
        const void* const pawn = read_ptr(pc, kControllerPawn);
        if (pawn != nullptr) {
            c24_release_pawn(pawn, "release (leaving game)");
            c448_logout(pc, pawn); // card 448: before card 043 drops the pawn's life row
            c43_logout(pawn);
        }
    }
    g_orig_c447_pawn_leaving(pc);
}

void c447_seat_leave(void* pc) {
    if (is_local_pc(pc)) {
        return; // the host's own PC (AController::Destroyed runs Logout for it too): no leaving player
    }
    void* const seat = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    const bool mech = seat != nullptr && is_a(seat, g_mech_class());
    if (seat == nullptr || (!mech && !is_a(seat, g_turret_class()))) {
        return;
    }
    c24_release_pawn(seat, "release (leaving game, seat)"); // card 125: a held relay on the seat goes before its rider
    if (mech) {
        g_mech_request_leave(seat);
    } else {
        detour_leave_turret(seat);
    }
    const void* const after = read_ptr(pc, kControllerPawn);
    log::line("%scard447: host t=%llu %s leaves the game in %s %s: the game's leave run first, PC pawn now %s (%s)",
              after == seat ? "WARN " : "", static_cast<unsigned long long>(GetTickCount64()), object_name(pc).text,
              mech ? "mech" : "turret", object_name(seat).text, object_name(after).text,
              after == seat ? "leave refused, the seat goes with the player" : "the seat stays");
}

// Host: true = EnterMech is skipped because card 043 holds `pawn` Downed, Out or Dead (its entry stays).
bool c79_refuse_enter(const void* mech, const void* pawn) {
    if (g_client_session || !c43_is_down(pawn)) {
        return false;
    }
    const int32_t i = c43_find_pawn(pawn);
    if (note_event_cap(g_c79.refuse_lines, kC79RefuseLines, "card079 refusals")) {
        log::line("card079: host t=%llu EnterMech refused: %s (%s) is %s; mech %s stays empty, card 043 entry kept "
                  "(%d entries)",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(pawn).text,
                  kSprintSideText[sprint_side(pawn)], i >= 0 ? kC43LifeText[static_cast<size_t>(g_c43.e[i].state)] : "-",
                  object_name(mech).text, g_c43.count);
    }
    return true;
}

// Host test: the lethal hits start kC79KillDelayMs after an EnterMech.
void c79_note_enter(const void* mech) {
    if (!g_c79_kill || g_client_session) {
        return;
    }
    g_c79.kill_ms = GetTickCount64() + kC79KillDelayMs;
    g_c79.kill_tries = 0;
    log::line("card079: host test kill of %s in %llu ms", object_name(mech).text,
              static_cast<unsigned long long>(kC79KillDelayMs));
}

// Card 100: the client's tactical support slot input. HandleTacticalSupportInput sends a gameplay event to the pawn,
// and the client's own activation sends its target data (STGameplayAbilityTargetData_TacticalSupportInput) to the
// server; that struct has no native NetSerialize: LowLevelFatalError (GameplayAbilityTargetTypes.cpp:240). The
// server copy owns the flare, so the slot goes to the host. Card 174: the client also runs the original for its own
// pawn, so its local ability plays the throw with the flare in hand; that send (detour_c174_server_try) and its
// TryThrowFlare (c174_block_throw) are not made on the client.
constexpr uint32_t kC100TacSlots = 4;
struct C100Tac { uint32_t sent; uint32_t received; };
C100Tac g_c100_tac{}; // game thread
TryThrowFlareFn g_orig_c100_tac_input = nullptr;
C100ChargeFn g_c100_charge = nullptr;

// The slot's charge (what the HUD shows), or -1 without a component or the function.
int32_t c100_charge(void* component, int32_t slot) {
    return component != nullptr && g_c100_charge != nullptr ? g_c100_charge(component, slot) : -1;
}

void detour_c100_tac_input(void* component, int32_t slot) {
    void* const owner = read_ptr(component, kComponentOwner);
    const int32_t charge = c100_charge(component, slot);
    g_orig_c100_tac_input(component, slot);
    if (!g_client_session || sprint_side(owner) != kSideClientOwn) {
        return;
    }
    const bool sent = slot >= 0 && static_cast<uint32_t>(slot) < kC100TacSlots &&
                      chan_to_host(read_ptr(owner, kPawnController), ChanOp::TacSupport, static_cast<uint32_t>(slot));
    g_c100_tac.sent += sent ? 1 : 0;
    log::line("card100: client t=%llu own HandleTacticalSupportInput(slot %d), local charge %d: run here for the "
              "local throw (no server send, no local flare); %s to the host's server copy (%u sent)",
              static_cast<unsigned long long>(GetTickCount64()), slot, charge,
              sent ? "sent" : "WARN not sent", g_c100_tac.sent);
}

// Host: a client's tactical support slot input. Its server copy's TacticalSupportComponent runs the original.
void c100_receive_tac(void* pc, uint32_t index) {
    void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    const uint8_t* prop = nullptr;
    void* const comp = pawn != nullptr ? const_cast<void*>(prop_object_field(pawn, "TacticalSupportComponent", &prop))
                                       : nullptr;
    const bool run = comp != nullptr && index < kC100TacSlots && g_orig_c100_tac_input != nullptr;
    ++g_c100_tac.received;
    const int32_t slot = static_cast<int32_t>(index);
    const int32_t before = run ? c100_charge(comp, slot) : -1;
    if (run) {
        g_orig_c100_tac_input(comp, slot);
    }
    log::line("card100: host t=%llu TacSupport slot %u from %s: server copy %s, TacticalSupportComponent %s; %s, "
              "charge %d -> %d",
              static_cast<unsigned long long>(GetTickCount64()), index, object_name(pc).text, object_name(pawn).text,
              object_name(comp).text, run ? "HandleTacticalSupportInput run" : "WARN skipped", before,
              run ? c100_charge(comp, slot) : -1);
}

} // namespace ubw::hooks
