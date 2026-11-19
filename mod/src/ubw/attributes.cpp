// Attributes: the client pawn's GAS state (card 017), the client camera attribute re-init and UIOnly caller
// (card 018), the client Health / WeaponOwner attribute re-init (card 020) and the server-side parity
// audit (card 020).

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

constexpr size_t kAscOwnerActor = 0x3E0;
constexpr size_t kAscAvatarActor = 0x3E8;
constexpr size_t kAscNetSimulated = 0x7C0;
constexpr size_t kAscScopeLock = 0x560;

constexpr size_t kCameraLookBase = 0x48;
constexpr size_t kCameraLookCurrent = 0x4C;

constexpr ULONGLONG kC17LandDelayMs = 10000; // third sample: 10 s after landing
constexpr ULONGLONG kC17WindowMs = 300000;   // the per-map watch stops after 300 s
constexpr uint32_t kC17HookLogs = 40;        // per hook, per map

GiveAbilityFn g_orig_give_ability = nullptr;
BindInputTagFn g_orig_bind_input_tag = nullptr;
InitFeaturesFn g_orig_init_features = nullptr;
AddAttributeSetFn g_orig_add_attribute_set = nullptr;
PostAttributeChangeFn g_orig_camera_post_change = nullptr;
StaticClassFn g_player_mi_class = nullptr;             // ASTPlayerCharacterMI
StaticClassFn g_camera_set_class = nullptr;            // USTAttributeSet_PlayerCamera
StaticClassFn g_movement_set_class = nullptr;          // USTAttributeSet_Movement (card 019)

C17State g_c17{};

C18State g_c18{};
void card018_slot_step(const uint8_t* pawn); // card 018, defined after the card 017 hooks
void card018_bind_specs(void* asc, const uint8_t* pawn);

bool player_asc(const void* asc) {
    return asc != nullptr && is_a(read_ptr(asc, kComponentOwner), g_player_mi_class());
}

struct C17Sets {
    char names[kC17SetsShown * 40];
    float look_base;
    float look_current;
    const void* camera_set;
    int32_t num;
    int32_t camera_sets;
};

// The ASC's spawned attribute sets: class names (first kC17SetsShown) and the camera set's look value.
C17Sets read_attribute_sets(const void* asc) {
    C17Sets r{};
    r.look_base = -1.0f;
    r.look_current = -1.0f;
    r.num = read_i32(asc, kAscSpawnedAttributesNum);
    void* const* const data = static_cast<void* const*>(read_ptr(asc, kAscSpawnedAttributes));
    const int32_t n = (data == nullptr || r.num <= 0) ? 0 : (r.num < kC17SetsShown ? r.num : kC17SetsShown);
    size_t used = 0;
    for (int32_t i = 0; i < n; ++i) {
        const void* const set = data[i];
        const int w = snprintf(r.names + used, sizeof(r.names) - used, "%s%s", i == 0 ? "" : " ",
                               class_name(set).text);
        used += (w > 0 && static_cast<size_t>(w) < sizeof(r.names) - used) ? static_cast<size_t>(w) : 0;
        if (set != nullptr && is_a(set, g_camera_set_class())) {
            ++r.camera_sets;
            r.camera_set = set;
            r.look_base = read_f32(set, kCameraLookBase);
            r.look_current = read_f32(set, kCameraLookCurrent);
        }
    }
    return r;
}

void log_gas_state(const char* point, int slot, const uint8_t* pawn) {
    if (!note_event_cap(g_c17.row_logs, kC17HookLogs, "card017 GAS state rows")) {
        return;
    }
    const void* const asc = g_get_asc(pawn, true);
    if (asc == nullptr) {
        log::line("card017: %s %s slot %d pawn %p %s LocalRole %u: no ASC", side(), point, slot,
                  static_cast<const void*>(pawn), class_name(pawn).text, pawn[kActorLocalRole]);
        return;
    }
    const auto* const a = static_cast<const uint8_t*>(asc);
    const C17Sets s = read_attribute_sets(asc);
    log::line("card017: %s %s slot %d pawn %p %s LocalRole %u: asc %p %s, owner-authoritative %d, "
              "ActivatableAbilities %d, spawned attribute sets %d [%s], camera sets %d (%p %s), CameraLookInputMultiplier "
              "base %.3f current %.3f, input tag map Num %d free %d, scope lock %d, pending binds %d",
              side(), point, slot, static_cast<const void*>(pawn), class_name(pawn).text, pawn[kActorLocalRole],
              asc, class_name(asc).text, a[kAscNetSimulated] == 0 ? 1 : 0, read_i32(asc, kAscAbilityNum), s.num,
              s.names, s.camera_sets, s.camera_set, object_name(s.camera_set).text, s.look_base, s.look_current,
              read_i32(asc, kAscInputTagMapNum), read_i32(asc, kAscInputTagMapFree), read_i32(asc, kAscScopeLock),
              read_i32(asc, kAscPendingBindNum));
}

// Server only, once per slot per map (at the possession sample).
void log_c17_ownership(int slot, const uint8_t* pawn) {
    const void* const controller = read_ptr(pawn, kPawnController);
    const bool is_pc = controller != nullptr && is_a(controller, g_player_controller_class());
    const void* const conn = is_pc ? read_ptr(controller, kPcNetConnection) : nullptr;
    const void* const player = is_pc ? read_ptr(controller, kPcPlayer) : nullptr;
    const auto* const asc = static_cast<const uint8_t*>(g_get_asc(pawn, true));
    log::line("card017: %s ownership slot %d pawn %p %s (LocalRole %u RemoteRole %u): Owner %p %s, Controller %p %s "
              "(PlayerController %d, NetConnection %p %s, Player %p %s); asc %p component owner %s, OwnerActor %s, "
              "AvatarActor %s, ReplicationMode %d",
              side(), slot, static_cast<const void*>(pawn), class_name(pawn).text, pawn[kActorLocalRole],
              pawn[kActorRemoteRole], read_ptr(pawn, kActorOwner), class_name(read_ptr(pawn, kActorOwner)).text,
              controller, class_name(controller).text, is_pc ? 1 : 0, conn, class_name(conn).text, player,
              class_name(player).text, static_cast<const void*>(asc),
              asc != nullptr ? class_name(read_ptr(asc, kComponentOwner)).text : "-",
              asc != nullptr ? class_name(read_ptr(asc, kAscOwnerActor)).text : "-",
              asc != nullptr ? class_name(read_ptr(asc, kAscAvatarActor)).text : "-",
              asc != nullptr ? static_cast<int>(asc[kAscReplicationMode]) : -1);
}

// --- Card 301 diagnostic: do the class F components replicate? -----------------------------------------
// The C1M1 level script writes per-player state into these pawn components (faction tag, squad leader, tactical
// support class and charge). One row per component class, the first time a player pawn holds it, on each side:
// the component's own bReplicates and every property with CPF_Net up its class chain. The rows decide whether a
// class F fix is host-side only or also needs a mod relay.
constexpr size_t kPawnFactionComponent = 0x6B0;  // UADFactionComponent (card 268)
constexpr size_t kPawnSquadLeaderComponent = 0x868; // USTSquadLeaderComponent (as save_c121.cpp kPawnSquadLeader)
constexpr uint32_t kC301Comps = 3;
constexpr size_t kC301NetText = 640;

struct C301Net {
    char names[kC301NetText];
    int32_t walked;
    int32_t net;
    bool truncated;
};

C301Net c301_net_props(const void* cls) {
    C301Net r{};
    size_t used = 0;
    const void* s = cls;
    for (int depth = 0; depth < kPropMaxSupers && s != nullptr; ++depth, s = read_ptr(s, kStructSuper)) {
        const void* f = read_ptr(s, kStructChildProperties);
        for (int i = 0; i < kPropMaxFields && f != nullptr; ++i, f = read_ptr(f, kFieldNext)) {
            ++r.walked;
            if ((read_u64(f, kPropertyFlags) & kCpfNet) == 0) {
                continue;
            }
            ++r.net;
            const int n = snprintf(r.names + used, sizeof(r.names) - used, "%s%s.%s", used == 0 ? "" : " ",
                                   object_name(s).text, fname_text(static_cast<const uint8_t*>(f) + kFieldName).text);
            r.truncated = r.truncated || n < 0 || used + static_cast<size_t>(n) >= sizeof(r.names);
            used = r.truncated ? used : used + static_cast<size_t>(n);
        }
    }
    return r;
}

// The component's bReplicates (reflected bool), -1 when the property is absent or not a plain bool.
int c301_component_replicates(const void* comp) {
    const uint8_t* const prop = prop_find_prop(read_ptr(comp, kObjectClass), "bReplicates");
    if (prop == nullptr || !prop_bool_ok(prop)) {
        return -1;
    }
    const uint8_t* const at =
        static_cast<const uint8_t*>(comp) + read_i32(prop, kPropertyOffset) + prop[kBoolByteOffset];
    return (*at & prop[kBoolFieldMask]) != 0 ? 1 : 0;
}

void c301_net_audit(const uint8_t* pawn) {
    static uint32_t done = 0; // bit per component slot, per process (one side per process)
    if (done == (1u << kC301Comps) - 1) {
        return;
    }
    const uint8_t* tac_prop = nullptr;
    const void* const comps[kC301Comps] = {read_ptr(pawn, kPawnFactionComponent),
                                           read_ptr(pawn, kPawnSquadLeaderComponent),
                                           prop_object_field(pawn, "TacticalSupportComponent", &tac_prop)};
    const char* const via[kC301Comps] = {"pawn +0x6B0", "pawn +0x868", "TacticalSupportComponent"};
    for (uint32_t i = 0; i < kC301Comps; ++i) {
        if ((done & (1u << i)) != 0 || comps[i] == nullptr) {
            continue;
        }
        done |= 1u << i;
        const void* const cls = read_ptr(comps[i], kObjectClass);
        const C301Net net = c301_net_props(cls);
        log::line("card301: %s net audit %s (%s, pawn %s): bReplicates %d, %d properties walked, %d CPF_Net%s: %s",
                  side(), class_name(comps[i]).text, via[i], class_name(pawn).text, c301_component_replicates(comps[i]),
                  net.walked, net.net, net.truncated ? " (list truncated)" : "", net.net > 0 ? net.names : "none");
    }
}

// One slot's samples: possession (a new on-foot pawn), landing (first tick not held by a dropship),
// and 10 s after landing. Only the pawn's FName is kept across ticks.
void c17_slot_step(int slot, const uint8_t* pawn, ULONGLONG now) {
    card018_slot_step(pawn);
    C17Slot& s = g_c17.slots[slot];
    uint64_t name = 0;
    memcpy(&name, pawn + kObjectName, sizeof(name));
    if (name != s.pawn_name) {
        s = C17Slot{name, 0, 1};
        log_gas_state("possession", slot, pawn);
        c301_net_audit(pawn); // card 301 diagnostic
        if (!g_client_session) {
            log_c17_ownership(slot, pawn);
        }
    }
    if ((s.done & 2u) == 0 && holding_dropship(pawn) == nullptr) {
        s.done |= 2u;
        s.land_ms = now;
        log_gas_state("landing", slot, pawn);
    }
    if ((s.done & 6u) == 2u && now - s.land_ms >= kC17LandDelayMs) {
        s.done |= 4u;
        log_gas_state("landing+10s", slot, pawn);
    }
}

// Both sides, every UGameEngine::Tick after the engine's Tick, for kC17WindowMs after LoadMap.
void card017_step(void* engine) {
    const ULONGLONG now = GetTickCount64();
    if (now - g_c17.start_ms > kC17WindowMs) {
        g_c17_armed = false;
        return;
    }
    void* const world = g_get_game_world(engine);
    if (world == nullptr) {
        return;
    }
    for (int i = 0; i < kC17Slots; ++i) {
        const void* const pc = g_get_player_controller(world, i);
        if (pc == nullptr) {
            break;
        }
        const auto* const pawn = static_cast<const uint8_t*>(read_ptr(pc, kControllerPawn));
        if (pawn != nullptr && is_a(pawn, g_player_mi_class())) {
            c17_slot_step(i, pawn, now);
        }
    }
}

void* detour_give_ability(void* asc, void* out_handle, const void* spec) {
    void* const ret = g_orig_give_ability(asc, out_handle, spec);
    if (player_asc(asc) && note_event_cap(g_c17.give_logs, kC17HookLogs, "card017 GiveAbility")) {
        log::line("card017: %s GiveAbility(%s) = handle %d, owner-authoritative %d, ActivatableAbilities %d, caller "
                  "exe+0x%llX",
                  side(), object_name(read_ptr(spec, kSpecAbility)).text, read_i32(out_handle, 0),
                  static_cast<const uint8_t*>(asc)[kAscNetSimulated] == 0 ? 1 : 0, read_i32(asc, kAscAbilityNum),
                  exe_rva(_ReturnAddress()));
    }
    return ret;
}

void detour_bind_input_tag(void* asc, const void* tag, const void* handle) {
    if (!player_asc(asc)) {
        g_orig_bind_input_tag(asc, tag, handle);
        return;
    }
    const int32_t before = read_i32(asc, kAscInputTagMapNum);
    g_orig_bind_input_tag(asc, tag, handle);
    if (note_event_cap(g_c17.bind_logs, kC17HookLogs, "card017 BindInputTagToAbilitySpec")) {
        log::line("card017: %s BindInputTagToAbilitySpec(%s, handle %d): scope lock %d, input tag map Num %d -> %d, "
                  "pending binds %d, caller exe+0x%llX",
                  side(), fname_text(tag).text, read_i32(handle, 0), read_i32(asc, kAscScopeLock), before,
                  read_i32(asc, kAscInputTagMapNum), read_i32(asc, kAscPendingBindNum), exe_rva(_ReturnAddress()));
    }
}

void detour_init_features(void* actor, void* const* definition, void* context) {
    const void* const def_class = definition != nullptr ? *definition : nullptr; // read before the call
    g_orig_init_features(actor, definition, context);
    if (is_a(actor, g_player_mi_class()) && note_event_cap(g_c17.init_logs, kC17HookLogs, "card017 InitFeatures")) {
        log::line("card017: %s InitFeaturesFromDefinition(%s, %s) returned, caller exe+0x%llX", side(),
                  class_name(actor).text, object_name(def_class).text, exe_rva(_ReturnAddress()));
        log_gas_state("after InitFeaturesFromDefinition", -1, static_cast<const uint8_t*>(actor));
    }
}

void detour_add_attribute_set(void* asc, void* const* init_data) {
    const void* const init_class = init_data != nullptr ? *init_data : nullptr; // read before the call
    g_orig_add_attribute_set(asc, init_data);
    if (player_asc(asc) && note_event_cap(g_c17.attr_logs, kC17HookLogs, "card017 AddAttributeSetFromData")) {
        const C17Sets s = read_attribute_sets(asc);
        log::line("card017: %s AddAttributeSetFromData(%s): spawned attribute sets %d, camera sets %d, "
                  "CameraLookInputMultiplier base %.3f current %.3f",
                  side(), object_name(init_class).text, s.num, s.camera_sets, s.look_base, s.look_current);
    }
}

void detour_camera_post_change(void* set, const void* attribute, float old_value, float new_value) {
    g_orig_camera_post_change(set, attribute, old_value, new_value);
    if (note_event_cap(g_c17.change_logs, kC17HookLogs, "card017 camera PostAttributeChange")) {
        log::line("card017: %s USTAttributeSet_PlayerCamera::PostAttributeChange(%s) %.3f -> %.3f on %s, "
                  "CameraLookInputMultiplier base %.3f current %.3f, caller exe+0x%llX",
                  side(), attribute_name(attribute).text, old_value, new_value,
                  class_name(read_ptr(set, kObjectOuter)).text, read_f32(set, kCameraLookBase),
                  read_f32(set, kCameraLookCurrent), exe_rva(_ReturnAddress()));
    }
}

constexpr int32_t kCameraLookOffset = 0x40;

constexpr uint32_t kC18AuditLogs = 24; // property rows per map (both sides)
constexpr uint32_t kC18UiOnlyLogs = 8;
constexpr uint32_t kC18FixLogs = 8;

OnRepSpawnedAttributesFn g_orig_onrep_spawned_attributes = nullptr;
ExecThunkFn g_orig_exec_ui_only = nullptr;

struct CameraLayout {
    int32_t props;
    int32_t net_props;
    bool look_offset_ok; // CameraLookInputMultiplier found at kCameraLookOffset
};

// Walks the camera set class's own properties: count, CPF_Net count, and the layout check.
CameraLayout camera_layout(const void* set_class) {
    CameraLayout r{};
    const void* field = read_ptr(set_class, kStructChildProperties);
    for (int i = 0; i < kC18MaxProps && field != nullptr; ++i, field = read_ptr(field, kFieldNext)) {
        ++r.props;
        r.net_props += (read_u64(field, kPropertyFlags) & kCpfNet) != 0 ? 1 : 0;
        const ObjectName name = fname_text(static_cast<const uint8_t*>(field) + kFieldName);
        if (strcmp(name.text, "CameraLookInputMultiplier") == 0 && read_i32(field, kPropertyOffset) == kCameraLookOffset) {
            r.look_offset_ok = true;
        }
    }
    return r;
}

// One row per camera set property (name, flags, CPF_Net, base/current). Capped per map.
void log_camera_audit(const char* point, const void* set) {
    const void* const set_class = read_ptr(set, kObjectClass);
    const CameraLayout layout = camera_layout(set_class);
    if (!layout.look_offset_ok) {
        if (note_event_cap(g_c18.audit_logs, kC18AuditLogs, "card018 camera audit")) {
            log::line("WARN card018: %s %s camera audit: CameraLookInputMultiplier not at +0x%X in %d properties -- "
                      "FProperty layout unverified, values not read",
                      side(), point, kCameraLookOffset, layout.props);
        }
        return;
    }
    const void* field = read_ptr(set_class, kStructChildProperties);
    for (int i = 0; i < kC18MaxProps && field != nullptr; ++i, field = read_ptr(field, kFieldNext)) {
        if (!note_event_cap(g_c18.audit_logs, kC18AuditLogs, "card018 camera audit")) {
            return;
        }
        const uint64_t flags = read_u64(field, kPropertyFlags);
        const int32_t off = read_i32(field, kPropertyOffset);
        log::line("card018: %s %s camera audit %s.%s %s: offset 0x%X flags 0x%016llX CPF_Net %d, base %.3f current %.3f",
                  side(), point, object_name(set).text, fname_text(static_cast<const uint8_t*>(field) + kFieldName).text,
                  class_name(read_ptr(set, kObjectOuter)).text, off, static_cast<unsigned long long>(flags),
                  (flags & kCpfNet) != 0 ? 1 : 0, read_f32(set, static_cast<size_t>(off) + kAttrDataBase),
                  read_f32(set, static_cast<size_t>(off) + kAttrDataCurrent));
    }
}

// --- Card 020: client Health / WeaponOwner attribute re-init -----------------------------------
// The card 020 parity audit found the client's ADAttributeSetHealth and STRangedWeaponOwnerAttributeSetBase
// at 0 (Health, MaxHealth, Accuracy, RecoilMultiplier, ...) while the server copy holds the init values.
// Same fix as cards 018/019, applied only to a set none of whose attributes has CPF_Net: a set that does
// replicate is left to replication and logged.
constexpr const char* kC20ReinitSets[] = {"ADAttributeSetHealth", "STRangedWeaponOwnerAttributeSetBase",
                                           "STPlayerMechAttributeSet"};

// The ASC's spawned set whose class is named class_text, or null.
const void* find_set_by_class_name(const void* asc, const char* class_text) {
    const int32_t num = read_i32(asc, kAscSpawnedAttributesNum);
    void* const* const data = static_cast<void* const*>(read_ptr(asc, kAscSpawnedAttributes));
    const int32_t n = (data == nullptr || num <= 0) ? 0 : (num < kC17SetsShown ? num : kC17SetsShown);
    for (int32_t i = 0; i < n; ++i) {
        if (data[i] != nullptr && strcmp(class_name(data[i]).text, class_text) == 0) {
            return data[i];
        }
    }
    return nullptr;
}

// Own properties of a set class: count and CPF_Net count (capped at kC18MaxProps, logged when hit).
CameraLayout net_layout(const void* set_class) {
    CameraLayout r{};
    const void* field = read_ptr(set_class, kStructChildProperties);
    for (int i = 0; i < kC18MaxProps && field != nullptr; ++i, field = read_ptr(field, kFieldNext)) {
        ++r.props;
        r.net_props += (read_u64(field, kPropertyFlags) & kCpfNet) != 0 ? 1 : 0;
    }
    if (field != nullptr) {
        log::line("WARN card020: property walk of %s hit the cap %d", object_name(set_class).text, kC18MaxProps);
        r.props = -1; // incomplete walk: never re-init on it
    }
    return r;
}

// First own attribute's base/current (the set's first FGameplayAttributeData), for the log.
void first_attr(const void* set, float* base, float* current) {
    const void* const field = read_ptr(read_ptr(set, kObjectClass), kStructChildProperties);
    const int32_t off = field != nullptr ? read_i32(field, kPropertyOffset) : -1;
    *base = off >= 0 ? read_f32(set, static_cast<size_t>(off) + kAttrDataBase) : -1.0f;
    *current = off >= 0 ? read_f32(set, static_cast<size_t>(off) + kAttrDataCurrent) : -1.0f;
}

// Client own pawn, from card018_camera_fix (same once-per-ASC point).
void card020_set_fix(void* asc, const uint8_t* pawn, const char* class_text) {
    const void* const set = find_set_by_class_name(asc, class_text);
    const void* const set_class = set != nullptr ? read_ptr(set, kObjectClass) : nullptr;
    const CameraLayout layout = set_class != nullptr ? net_layout(set_class) : CameraLayout{};
    const bool eligible = layout.props > 0 && layout.net_props == 0;
    void* init = eligible ? camera_init_data(pawn, set_class) : nullptr;
    if (init == nullptr) {
        log::line("card020: client %s re-init skipped on %s: set %p, CPF_Net attributes %d/%d, init data %s", class_text,
                  object_name(asc).text, set, layout.net_props, layout.props,
                  eligible ? "NOT FOUND in the pawn definition" : "not looked up");
        return;
    }
    float base_before = 0.0f;
    float current_before = 0.0f;
    first_attr(set, &base_before, &current_before);
    g_orig_add_attribute_set(asc, &init); // TSubclassOf by value: hidden pointer to the caller's copy
    const void* const after = find_set_by_class_name(asc, class_text);
    float base_after = -1.0f;
    float current_after = -1.0f;
    if (after != nullptr) {
        first_attr(after, &base_after, &current_after);
    }
    log::line("card020: client %s re-init AddAttributeSetFromData(%s) on %s (pawn %s LocalRole %u): set %p -> %p, "
              "first attribute base %.3f current %.3f -> base %.3f current %.3f",
              class_text, object_name(init).text, object_name(asc).text, class_name(pawn).text, pawn[kActorLocalRole],
              set, after, static_cast<double>(base_before), static_cast<double>(current_before),
              static_cast<double>(base_after), static_cast<double>(current_after));
    if (strcmp(class_text, "STPlayerMechAttributeSet") == 0) {
        c459_after_reinit(pawn); // card 459: the re-init set Fuel to its init value; the host's kept values go back
    }
}

// Card 058: the per-instance key of a player ASC. Every player ASC is named "AbilitySystemComponent" (number 0),
// so after RESTART CHECKPOINT the new pawn's ASC has the old one's FName and the once-per-ASC fixes skipped it
// (no camera re-init: CameraLookInputMultiplier 0, no mouse look). The owner pawn's FName differs per instance.
uint64_t player_asc_key(const void* asc) {
    const void* const owner = read_ptr(asc, kComponentOwner);
    return owner != nullptr ? read_u64(owner, kObjectName) : read_u64(asc, kObjectName);
}

// Client own pawn (LocalRole 2), once per ASC per map: re-runs AddAttributeSetFromData for the camera
// set when none of its attributes replicate, so CameraLookInputMultiplier gets its init value back.
void card018_camera_fix(void* asc, const uint8_t* pawn) {
    g_c18.fix_done_asc = player_asc_key(asc);
    const C17Sets before = read_attribute_sets(asc);
    if (before.camera_set == nullptr) {
        g_c18.fix_done_asc = 0; // the camera set has not replicated yet: card018_client_step retries next tick
        return;
    }
    log_camera_audit("client after OnRep_SpawnedAttributes", before.camera_set);
    card019_movement_fix(asc, pawn); // card 019: same once-per-ASC point
    for (const char* const set_name : kC20ReinitSets) {
        card020_set_fix(asc, pawn, set_name); // card 020: same once-per-ASC point
    }
    const void* const set_class = read_ptr(before.camera_set, kObjectClass);
    const CameraLayout layout = camera_layout(set_class);
    void* init = layout.look_offset_ok && layout.net_props == 0 ? camera_init_data(pawn, set_class) : nullptr;
    if (init == nullptr) {
        log::line("WARN card018: client camera re-init skipped on %s: layout ok %d, CPF_Net attributes %d/%d, "
                  "init data %s",
                  object_name(asc).text, layout.look_offset_ok ? 1 : 0, layout.net_props, layout.props,
                  layout.look_offset_ok && layout.net_props == 0 ? "NOT FOUND in the pawn definition" : "not looked up");
        return;
    }
    g_orig_add_attribute_set(asc, &init); // TSubclassOf by value: hidden pointer to the caller's copy
    const C17Sets after = read_attribute_sets(asc);
    if (note_event_cap(g_c18.fix_logs, kC18FixLogs, "card018 camera re-init")) {
        log::line("card018: client camera re-init AddAttributeSetFromData(%s) on %s (pawn %s LocalRole %u): "
                  "CameraLookInputMultiplier base %.3f current %.3f -> base %.3f current %.3f, camera sets %d -> %d",
                  object_name(init).text, object_name(asc).text, class_name(pawn).text, pawn[kActorLocalRole],
                  before.look_base, before.look_current, after.look_base, after.look_current, before.camera_sets,
                  after.camera_sets);
    }
    log_camera_audit("client after camera re-init", after.camera_set);
    c298_mount_row("camera re-init", pawn); // card 298 (diagnostic): controller and limits after the re-init
}

void detour_onrep_spawned_attributes(void* asc, const void* previous) {
    g_orig_onrep_spawned_attributes(asc, previous);
    if (!g_client_session || !player_asc(asc)) {
        return;
    }
    if (player_asc_key(asc) == g_c18.fix_done_asc) {
        return;
    }
    // Not our pawn yet (or a simulated one): card018_client_step re-checks the own pawn every tick.
    const auto* const pawn = static_cast<const uint8_t*>(read_ptr(asc, kComponentOwner));
    if (pawn[kActorLocalRole] == kRoleAutonomousProxy) {
        card018_camera_fix(asc, pawn);
    }
}

// card017_step, per player pawn per tick. Server: the camera audit of each player pawn's set (capped per map).
void card018_slot_step(const uint8_t* pawn) {
    if (g_client_session || g_c18.audit_logs != 0) {
        return;
    }
    void* const asc = g_get_asc(pawn, true);
    const C17Sets s = asc != nullptr ? read_attribute_sets(asc) : C17Sets{};
    if (s.camera_set != nullptr) {
        log_camera_audit("server copy", s.camera_set);
    }
}

// Client, every tick (not limited to card 017's 300 s window): the own pawn (LocalRole 2) whose ASC has no re-init
// yet gets one (card 298: a mech the host possessed first had its OnRep_SpawnedAttributes before the client's
// possession, and the one pending slot this replaced was overwritten before the possession). fix_done_asc holds
// one key, so each possession of another own pawn re-inits it once.
void card018_client_step(void* engine) {
    void* const world = g_get_game_world(engine);
    const void* const pc = world != nullptr ? g_get_player_controller(world, 0) : nullptr;
    const auto* const pawn = pc != nullptr ? static_cast<const uint8_t*>(read_ptr(pc, kControllerPawn)) : nullptr;
    if (pawn == nullptr || pawn[kActorLocalRole] != kRoleAutonomousProxy || !is_a(pawn, g_player_mi_class())) {
        return;
    }
    void* const asc = g_get_asc(pawn, true);
    if (asc == nullptr || !player_asc(asc)) {
        return;
    }
    if (g_c18.bind_pending && read_i32(asc, kAscAbilityNum) > 0) {
        card018_bind_specs(asc, pawn);
    }
    if (player_asc_key(asc) != g_c18.fix_done_asc) {
        card018_camera_fix(asc, pawn);
    }
}

// Card 018 part B: bind the client's replicated ability specs to their input tags.
//   UADActorDefinition CDO: ability entries +0x28 / Num +0x30, 0x18 each: ability class +0, input tags
//   TArray<FGameplayTag> +0x8 / Num +0x10 (8 bytes each, a zero tag is skipped), as
//   InitFeaturesFromDefinition +0x307..+0x4D4 reads them before GiveAbility and BindInputTagToAbilitySpec.
//   FGameplayAbilitySpec::Handle +0xC. FindAbilitySpecFromClass takes the TSubclassOf by hidden pointer.
constexpr size_t kDefinitionAbilities = 0x28;
constexpr size_t kDefinitionAbilitiesNum = 0x30;
constexpr size_t kAbilityEntryBytes = 0x18;
constexpr size_t kAbilityEntryTags = 0x8;
constexpr size_t kAbilityEntryTagsNum = 0x10;
constexpr size_t kGameplayTagBytes = 0x8;
constexpr int32_t kC18MaxEntries = 16;
constexpr int32_t kC18MaxTags = 4;
constexpr uint32_t kC18BindLogs = 8;

OnRepActivateAbilitiesFn g_orig_onrep_activate_abilities = nullptr;

// One tag can drive several specs: InputTag.Jump binds both GA_HumanJump and GA_Mantling_Search (card 094), so a
// bind is known by its (tag, spec handle) pair, not by its tag.
bool tag_bound(uint64_t tag, int32_t handle) {
    for (uint32_t i = 0; i < g_c18.bound_tags_num; ++i) {
        if (g_c18.bound_tags[i] == tag && g_c18.bound_handles[i] == handle) {
            return true;
        }
    }
    return false;
}

struct BindCount {
    int32_t bound;
    int32_t missing; // definition entries whose spec has not replicated yet
};

// One definition entry: binds each of its tags to the entry's spec handle, unless that pair is already bound.
BindCount bind_entry(void* asc, const uint8_t* entry) {
    BindCount r{};
    const auto* const tags = static_cast<const uint8_t*>(read_ptr(entry, kAbilityEntryTags));
    const int32_t num = read_i32(entry, kAbilityEntryTagsNum);
    const int32_t n = (tags == nullptr || num <= 0) ? 0 : (num < kC18MaxTags ? num : kC18MaxTags);
    void* ability_class = read_ptr(entry, 0);
    const auto* const spec = ability_class != nullptr && n > 0
                                 ? static_cast<const uint8_t*>(g_find_spec_from_class(asc, &ability_class))
                                 : nullptr;
    for (int32_t t = 0; t < n; ++t) {
        const uint8_t* const tag = tags + static_cast<size_t>(t) * kGameplayTagBytes;
        const uint64_t tag_name = read_u64(tag, 0);
        if (tag_name == 0 || g_c18.bound_tags_num >= kC18MaxBoundTags) {
            continue;
        }
        if (spec == nullptr) {
            ++r.missing;
            continue;
        }
        const int32_t handle = read_i32(spec, kSpecHandle);
        if (tag_bound(tag_name, handle)) {
            continue;
        }
        detour_bind_input_tag(asc, tag, spec + kSpecHandle); // logs each bind (card 017 cap)
        g_c18.bound_tags[g_c18.bound_tags_num] = tag_name;
        g_c18.bound_handles[g_c18.bound_tags_num++] = handle;
        ++r.bound;
    }
    return r;
}

// Client own pawn: every definition entry's tags bound to the replicated spec of its class. No
// GiveAbility; each (tag, spec) pair at most once per ASC per map; entries whose spec is missing wait for the
// next OnRep.
void card018_bind_specs(void* asc, const uint8_t* pawn) {
    const uint64_t key = player_asc_key(asc);
    if (key != g_c18.bind_asc) {
        g_c18.bind_asc = key;
        g_c18.bound_tags_num = 0;
    }
    g_c18.bind_pending = false;
    const void* const def = read_ptr(pawn, kPawnDefinition);
    const void* const cdo = def != nullptr ? read_ptr(def, kClassDefaultObject) : nullptr;
    if (cdo == nullptr || read_ptr(cdo, kObjectClass) != def) {
        log::line("WARN card018: client spec bind: pawn %s definition %p has no matching CDO", class_name(pawn).text, def);
        return;
    }
    const auto* const entries = static_cast<const uint8_t*>(read_ptr(cdo, kDefinitionAbilities));
    const int32_t num = read_i32(cdo, kDefinitionAbilitiesNum);
    const int32_t n = (entries == nullptr || num <= 0) ? 0 : (num < kC18MaxEntries ? num : kC18MaxEntries);
    const int32_t before = read_i32(asc, kAscInputTagMapNum);
    BindCount total{};
    for (int32_t e = 0; e < n; ++e) {
        const BindCount c = bind_entry(asc, entries + static_cast<size_t>(e) * kAbilityEntryBytes);
        total.bound += c.bound;
        total.missing += c.missing;
    }
    if ((total.bound > 0 || total.missing > 0) && note_event_cap(g_c18.bind_logs, kC18BindLogs, "card018 spec bind")) {
        log::line("card018: client spec bind on %s (%s, %d definition entries): bound %d tags, %d waiting for their "
                  "spec, input tag map Num %d -> %d, ActivatableAbilities %d, pending binds %d",
                  object_name(asc).text, object_name(def).text, num, total.bound, total.missing, before,
                  read_i32(asc, kAscInputTagMapNum), read_i32(asc, kAscAbilityNum), read_i32(asc, kAscPendingBindNum));
    }
}

void detour_onrep_activate_abilities(void* asc) {
    g_orig_onrep_activate_abilities(asc);
    if (!g_client_session || !player_asc(asc)) {
        return;
    }
    const auto* const pawn = static_cast<const uint8_t*>(read_ptr(asc, kComponentOwner));
    if (pawn[kActorLocalRole] == kRoleAutonomousProxy) {
        card018_bind_specs(asc, pawn);
    } else {
        g_c18.bind_pending = true; // the tick binds once the pawn is LocalRole 2
    }
}


// Blueprint SetInputMode_UIOnlyEx: names the BP function and the object (widget) that called it.
void detour_exec_ui_only(void* context, void* frame, void* result) {
    const void* const node = read_ptr(frame, kFrameNode); // read before the call steps the frame
    const void* const object = read_ptr(frame, kFrameObject);
    g_orig_exec_ui_only(context, frame, result);
    c31_note_ui_only(object);
    if (note_event_cap(g_c18.ui_only_logs, kC18UiOnlyLogs, "card018 SetInputMode_UIOnlyEx")) {
        log::line("card018: %s SetInputMode_UIOnlyEx called by BP function %s (in %s) on %s %s (outer %s %s), "
                  "%llu ms after LoadMap",
                  side(), object_name(node).text, object_name(read_ptr(node, kObjectOuter)).text,
                  class_name(object).text, object_name(object).text, class_name(read_ptr(object, kObjectOuter)).text,
                  object_name(read_ptr(object, kObjectOuter)).text,
                  static_cast<unsigned long long>(GetTickCount64() - g_c17.start_ms));
    }
}

// ---- Card 020: server-side parity audit ----
// Host rows every 500 ms for the host's own pawn and the server copy of the client's pawn (input,
// aim, weapon, interaction, then GAS state logged in full once and as diffs after), plus capped
// fire and interact hooks on both processes. All log only.
constexpr size_t kPawnRemoteViewPitch = 0x2BA;        // uint16, degrees * 65536 / 360 (APawn::SetRemoteViewPitch)
constexpr size_t kPawnLastControlInput = 0x308;       // APawn::LastControlInputVector (ConsumeMovementInputVector)

constexpr size_t kCharacterItemInventory = 0x860;     // USTItemInventory (ASTPlayerCharacter::GetInventory)

constexpr size_t kInteractorTraceSource = 0x108;      // USceneComponent* the trace starts at (TraceForInteractable)
constexpr size_t kInteractorHasTarget = 0x110;        // bool (TickComponent)

// GameplayTagCountContainer.ExplicitTags (FGameplayTagContainer: TArray<FGameplayTag> data, Num +8, Max
// +0xC). GetOwnedGameplayTags reads +0xEF0 from its IGameplayTagAssetInterface `this`, which sits at
// ASC+0x130 (HasMatchingGameplayTag reads the count map at interface+0xE40, GetGameplayTagCount at
// ASC+0xF70).
constexpr size_t kAscOwnedTags = 0x1020;
constexpr size_t kAscActiveEffects = 0x9C0;           // ActiveGameplayEffects.GameplayEffects_Internal data, Num +8
constexpr size_t kActiveEffectBytes = 0x360;
constexpr size_t kActiveEffectHandle = 0xC;
constexpr size_t kActiveEffectSpec = 0x18;            // FGameplayEffectSpec; Def is its first member

constexpr int kC20MaxSets = 12;
constexpr int kC20MaxSupers = 4;
constexpr int kC20MaxFields = 48;
constexpr uint32_t kC20GasLines = 300;  // per side per map
constexpr uint32_t kC20FireLines = 120; // per hook per side per map (card 021: covers both 2 s fire steps)
constexpr uint32_t kC20InteractLines = 12;

GetBaseAimRotationFn g_get_base_aim = nullptr;
GetEyesViewPointFn g_get_eyes_view_point = nullptr;
GetEquippedWeaponFn g_get_equipped_weapon = nullptr;
GetMuzzleTransformFn g_get_muzzle_transform = nullptr; // card 316
GetWeaponCountFn g_get_weapon_count = nullptr;
GetAmmoCountFn g_get_ammo_count = nullptr;
GetEquippedItemFn g_get_equipped_item = nullptr;
GetStackCountFn g_get_stack_count = nullptr;
ZeroAdjustFn g_orig_zero_adjust = nullptr;
FireProjectileFn g_orig_fire_projectile = nullptr;
InteractFn g_orig_interact = nullptr;

C20State g_c20{};
uint32_t g_c21_grant_lines = 0;

float g_c21_health_seen[kSprintSides]; // last Health current value per side; < 0 = none yet (reset per map)
C21Grant g_c21_grant{}; // card 021 grant log lines this map; game thread
const char* const kC20ZeroWhat[kSprintSides] = {"card020 host-own zero adjust", "card020 server-copy zero adjust",
                                                "card020 client-own zero adjust", "card020 other zero adjust"};
const char* const kC20FireWhat[kSprintSides] = {"card020 host-own FireProjectile", "card020 server-copy FireProjectile",
                                                "card020 client-own FireProjectile", "card020 other FireProjectile"};
const char* const kC20InteractWhat[kSprintSides] = {"card020 host-own Interact", "card020 server-copy Interact",
                                                    "card020 client-own Interact", "card020 other Interact"};

// Input and aim: the movement input vectors, ControlRotation, GetBaseAimRotation, RemoteViewPitch,
// the camera component and GetActorEyesViewPoint.
void log_c20_aim(uint32_t k, const char* who, const char* act, const uint8_t* pawn) {
    const Vec3 last = read_vec3(pawn, kPawnLastControlInput);
    const Vec3 control_in = read_vec3(pawn, kPawnControlInputVector);
    const void* const cmc = read_ptr(pawn, kCharacterMovement);
    const Vec3 accel = cmc != nullptr ? read_vec3(cmc, kCmcAcceleration) : Vec3{};
    const void* const controller = read_ptr(pawn, kPawnController);
    const Vec3 control = controller != nullptr ? read_vec3(controller, kControllerControlRotation) : Vec3{};
    Vec3 base{};
    g_get_base_aim(pawn, &base.x);
    Vec3 eyes{};
    Vec3 eyes_rot{};
    g_get_eyes_view_point(pawn, &eyes.x, &eyes_rot.x);
    uint16_t raw_pitch = 0;
    memcpy(&raw_pitch, pawn + kPawnRemoteViewPitch, sizeof(raw_pitch));
    const void* const cam = read_ptr(pawn, kCharacterCamera);
    const Vec3 cam_loc = cam != nullptr ? read_vec3(cam, kComponentTranslation) : Vec3{};
    const Vec3 cam_rot = cam != nullptr ? component_pitch_yaw(cam) : Vec3{};
    log::line("c20[%u] %s act=%s input: last (%.2f, %.2f, %.2f) control (%.2f, %.2f, %.2f) accel (%.1f, %.1f, %.1f); "
              "aim: ControlRotation P %.2f Y %.2f, BaseAim P %.2f Y %.2f, RemoteViewPitch %.2f, camera (%.1f, %.1f, "
              "%.1f) P %.2f Y %.2f, eyes (%.1f, %.1f, %.1f) P %.2f Y %.2f",
              k, who, act, last.x, last.y, last.z, control_in.x, control_in.y, control_in.z, accel.x, accel.y, accel.z,
              control.x, control.y, base.x, base.y, raw_pitch * 360.0 / 65536.0, cam_loc.x, cam_loc.y, cam_loc.z,
              cam_rot.x, cam_rot.y, eyes.x, eyes.y, eyes.z, eyes_rot.x, eyes_rot.y);
}

// Weapon (equipped weapon, ammo, weapon count, equipped item) and interaction (target, trace source).
void log_c20_weapon(uint32_t k, const char* who, const char* act, const uint8_t* pawn) {
    const bool mi = is_a(pawn, g_player_mi_class());
    const void* const weapons = mi ? read_ptr(pawn, kMiWeaponInventory) : nullptr;
    const void* const weapon = weapons != nullptr ? g_get_equipped_weapon(weapons) : nullptr;
    const void* const items = read_ptr(pawn, kCharacterItemInventory);
    const void* const item = items != nullptr ? g_get_equipped_item(items) : nullptr;
    const auto* const interactor = static_cast<const uint8_t*>(read_ptr(pawn, kCharacterInteractor));
    const void* const target = interact_target(interactor);
    const void* const source = interactor != nullptr ? read_ptr(interactor, kInteractorTraceSource) : nullptr;
    const Vec3 src = source != nullptr ? read_vec3(source, kComponentTranslation) : Vec3{};
    log::line("c20[%u] %s act=%s weapon: inventory %s, equipped %s, ammo %d, weapons %d, item %s; interact: target "
              "%s (%s), has-target %d, trace source %s (%.1f, %.1f, %.1f)",
              k, who, act, class_name(weapons).text, object_name(weapon).text,
              weapon != nullptr ? g_get_ammo_count(weapon) : -1, weapons != nullptr ? g_get_weapon_count(weapons) : -1,
              object_name(item).text, object_name(target).text, class_name(target).text,
              interactor != nullptr ? static_cast<int>(interactor[kInteractorHasTarget]) : -1,
              object_name(source).text, src.x, src.y, src.z);
}

// True while a side may log another GAS line this map.
bool c20_gas_line(C20Gas& g, const char* who) {
    return note_event_cap(g.lines, kC20GasLines, who);
}

// One attribute set's FGameplayAttributeData properties, its own and inherited (SuperStruct chain);
// compares each with the last row and logs new or changed ones. Returns the next attribute index.
int c20_set_attrs(uint32_t k, const char* who, const char* act, const void* set, C20Gas& g, int index) {
    const void* cls = read_ptr(set, kObjectClass);
    for (int depth = 0; depth < kC20MaxSupers && cls != nullptr; ++depth, cls = read_ptr(cls, kStructSuper)) {
        const void* field = read_ptr(cls, kStructChildProperties);
        for (int f = 0; f < kC20MaxFields && field != nullptr; ++f, field = read_ptr(field, kFieldNext)) {
            const int32_t off = read_i32(field, kPropertyOffset);
            const ObjectName kind = fname_text(read_ptr(field, kFieldClassPrivate));
            if (strcmp(kind.text, "StructProperty") != 0 || read_i32(field, kPropertyElementSize) != kAttributeDataBytes ||
                off < 0 || index >= kC20MaxAttrs) {
                continue;
            }
            const float base = read_f32(set, static_cast<size_t>(off) + kAttrDataBase);
            const float cur = read_f32(set, static_cast<size_t>(off) + kAttrDataCurrent);
            const bool changed = !g.seen || index >= g.attrs || base != g.base[index] || cur != g.current[index];
            if (changed && c20_gas_line(g, who)) {
                log::line("c20[%u] %s act=%s attr %s %s.%s base %.3f current %.3f", k, who, act,
                          g.seen ? "diff" : "init", class_name(set).text,
                          fname_text(static_cast<const uint8_t*>(field) + kFieldName).text, static_cast<double>(base),
                          static_cast<double>(cur));
            }
            g.base[index] = base;
            g.current[index] = cur;
            ++index;
        }
    }
    return index;
}

void c20_attrs(uint32_t k, const char* who, const char* act, const void* asc, C20Gas& g) {
    const int32_t num = read_i32(asc, kAscSpawnedAttributesNum);
    void* const* const data = static_cast<void* const*>(read_ptr(asc, kAscSpawnedAttributes));
    const int n = (data == nullptr || num <= 0) ? 0 : (num < kC20MaxSets ? num : kC20MaxSets);
    int index = 0;
    for (int i = 0; i < n; ++i) {
        index = data[i] != nullptr ? c20_set_attrs(k, who, act, data[i], g, index) : index;
    }
    if ((!g.seen || index != g.attrs) && c20_gas_line(g, who)) {
        log::line("c20[%u] %s act=%s attrs: %d sets, %d attributes (was %d)", k, who, act, num, index, g.attrs);
    }
    g.attrs = index;
}

void c20_specs(uint32_t k, const char* who, const char* act, const void* asc, C20Gas& g) {
    const int32_t num = read_i32(asc, kAscAbilityNum);
    const auto* const data = static_cast<const uint8_t*>(read_ptr(asc, kAscSpecs));
    const int32_t n = (data == nullptr || num <= 0) ? 0 : (num < kC20MaxSpecs ? num : kC20MaxSpecs);
    for (int32_t i = 0; i < n; ++i) {
        const uint8_t* const spec = data + static_cast<size_t>(i) * kSpecBytes;
        const uint8_t active = spec[kSpecActiveCount];
        const uint8_t flags = spec[kSpecFlags];
        const bool changed = !g.seen || i >= g.specs || active != g.spec_active[i] || flags != g.spec_flags[i];
        if (changed && c20_gas_line(g, who)) {
            log::line("c20[%u] %s act=%s spec %s [%d] handle %d %s: ActiveCount %u, InputPressed %u, flags 0x%02X", k,
                      who, act, g.seen ? "diff" : "init", i, read_i32(spec, kSpecHandle),
                      class_name(read_ptr(spec, kSpecAbility)).text, active, flags & 1u, flags);
        }
        g.spec_active[i] = active;
        g.spec_flags[i] = flags;
    }
    if ((!g.seen || n != g.specs) && c20_gas_line(g, who)) {
        log::line("c20[%u] %s act=%s specs: %d (was %d)", k, who, act, num, g.specs);
    }
    g.specs = n;
}

bool tag_in(const uint64_t* tags, int32_t n, uint64_t tag) {
    for (int32_t i = 0; i < n; ++i) {
        if (tags[i] == tag) {
            return true;
        }
    }
    return false;
}

void c20_tags(uint32_t k, const char* who, const char* act, const void* asc, C20Gas& g) {
    const int32_t num = read_i32(asc, kAscOwnedTags + sizeof(void*));
    const int32_t max = read_i32(asc, kAscOwnedTags + sizeof(void*) + sizeof(int32_t));
    const auto* const data = static_cast<const uint64_t*>(read_ptr(asc, kAscOwnedTags));
    if (num < 0 || num > max) {
        if (c20_gas_line(g, who)) {
            log::line("c20[%u] %s act=%s owned tags: bad TArray (Num %d, Max %d), skipped", k, who, act, num, max);
        }
        return;
    }
    const int32_t n = (data == nullptr || num == 0) ? 0 : (num < kC20MaxTags ? num : kC20MaxTags);
    for (int32_t i = 0; i < n; ++i) {
        if ((!g.seen || !tag_in(g.tag, g.tags, data[i])) && c20_gas_line(g, who)) {
            log::line("c20[%u] %s act=%s tag %s +%s", k, who, act, g.seen ? "diff" : "init", fname_text(&data[i]).text);
        }
    }
    for (int32_t i = 0; i < g.tags && g.seen; ++i) {
        if (!tag_in(data, n, g.tag[i]) && c20_gas_line(g, who)) {
            log::line("c20[%u] %s act=%s tag diff -%s", k, who, act, fname_text(&g.tag[i]).text);
        }
    }
    if ((!g.seen || num != g.tags) && c20_gas_line(g, who)) {
        log::line("c20[%u] %s act=%s owned tags: %d (was %d)", k, who, act, num, g.tags);
    }
    for (int32_t i = 0; i < n; ++i) {
        g.tag[i] = data[i];
    }
    g.tags = n;
}

void c20_effects(uint32_t k, const char* who, const char* act, const void* asc, C20Gas& g) {
    const int32_t num = read_i32(asc, kAscActiveEffects + sizeof(void*));
    const auto* const data = static_cast<const uint8_t*>(read_ptr(asc, kAscActiveEffects));
    const int32_t n = (data == nullptr || num <= 0) ? 0 : (num < kC20MaxEffects ? num : kC20MaxEffects);
    for (int32_t i = 0; i < n; ++i) {
        const uint8_t* const e = data + static_cast<size_t>(i) * kActiveEffectBytes;
        const int32_t handle = read_i32(e, kActiveEffectHandle);
        const int32_t stacks = g_get_stack_count(e + kActiveEffectSpec);
        const bool changed = !g.seen || i >= g.effects || handle != g.effect_handle[i] || stacks != g.effect_stacks[i];
        if (changed && c20_gas_line(g, who)) {
            log::line("c20[%u] %s act=%s effect %s [%d] handle %d %s stacks %d", k, who, act,
                      g.seen ? "diff" : "init", i, handle, class_name(read_ptr(e, kActiveEffectSpec)).text, stacks);
        }
        g.effect_handle[i] = handle;
        g.effect_stacks[i] = stacks;
    }
    if ((!g.seen || n != g.effects) && c20_gas_line(g, who)) {
        log::line("c20[%u] %s act=%s active effects: %d (was %d; the pending-add list is not walked)", k, who, act,
                  num, g.effects);
    }
    g.effects = n;
}

void log_c20_gas(uint32_t k, const char* who, const char* act, const uint8_t* pawn, C20Gas& g) {
    const void* const asc = g_get_asc(pawn, true);
    if (asc == nullptr) {
        log::line("c20[%u] %s act=%s gas: no ASC", k, who, act);
        return;
    }
    uint64_t name = 0;
    memcpy(&name, pawn + kObjectName, sizeof(name));
    if (name != g.pawn_name) {
        const uint32_t lines = g.lines;
        g = C20Gas{};
        g.pawn_name = name;
        g.lines = lines;
    }
    c20_attrs(k, who, act, asc, g);
    c20_specs(k, who, act, asc, g);
    c20_tags(k, who, act, asc, g);
    c20_effects(k, who, act, asc, g);
    g.seen = true;
}

void log_c20_pawn(uint32_t k, uint32_t side_index, const char* act, const uint8_t* pawn, C20Gas& gas) {
    const char* const who = kSprintSideText[side_index];
    if (pawn == nullptr || !is_a(pawn, g_player_character_class())) {
        log::line("c20[%u] %s act=%s: no on-foot player character (%p %s)", k, who, act,
                  static_cast<const void*>(pawn), class_name(pawn).text);
        return;
    }
    log_c20_aim(k, who, act, pawn);
    log_c20_weapon(k, who, act, pawn);
    log_c20_gas(k, who, act, pawn, gas);
}

// Every UGameEngine::Tick while rows are left: one row per kOwnRowIntervalMs for the local player's
// own pawn (host-own or client-own, labelled with its script action) and, on the host, the server copy
// of the client's pawn (labelled "client": the action comes from the client log's "autoinput: action"
// lines on the shared tick clock).
void card020_row_step(void* engine) {
    const ULONGLONG now = GetTickCount64();
    if (now < g_c20.next_ms) {
        return;
    }
    g_c20.next_ms = now + kOwnRowIntervalMs;
    --g_c20_rows_left;
    const uint32_t k = g_c20.row++;
    void* const world = g_get_game_world(engine);
    if (world == nullptr) {
        log::line("c20[%u]: no game world", k);
        return;
    }
    const uint8_t* own = nullptr;
    uint32_t own_side = kSideHostOwn;
    const uint8_t* copy = nullptr;
    for (int i = 0; i < kC17Slots; ++i) {
        const void* const pc = g_get_player_controller(world, i);
        const auto* const pawn = pc != nullptr ? static_cast<const uint8_t*>(read_ptr(pc, kControllerPawn)) : nullptr;
        const uint32_t s = sprint_side(pawn);
        const bool is_own = pawn != nullptr && (s == kSideHostOwn || s == kSideClientOwn) && own == nullptr;
        own_side = is_own ? s : own_side;
        own = is_own ? pawn : own;
        copy = (pawn != nullptr && s == kSideServerCopy && copy == nullptr) ? pawn : copy;
    }
    log_c20_pawn(k, own_side, autoinput::current_action(), own, g_c20.gas[0]);
    if (own_side == kSideHostOwn) {
        log_c20_pawn(k, kSideServerCopy, "client", copy, g_c20.gas[1]);
    }
}

// The pawn player 0 controls in `actor`'s world (the one GetTargetLocation and the zero-threshold
// deprojection read), or null.
const void* player0_pawn(const void* actor) {
    void* const world = actor != nullptr ? g_actor_get_world(actor) : nullptr;
    const void* const pc = world != nullptr ? g_get_player_controller(world, 0) : nullptr;
    return pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
}

// Fire origin: the muzzle transform before and after the zero-threshold adjustment, against the
// owner's camera and player 0's camera.
void detour_zero_adjust(const void* weapon, double* transform) {
    const Vec3 before = read_vec3(transform, kTransformTranslation);
    const Vec3 before_rot = quat_pitch_yaw(transform);
    const void* const owner = weapon != nullptr ? weapon_pawn(weapon) : nullptr;
    c24_aim_begin(owner);
    g_orig_zero_adjust(weapon, transform);
    c24_aim_end();
    const uint32_t who = sprint_side(owner);
    if (!note_event_cap(g_c20.zero_lines[who], kC20FireLines, kC20ZeroWhat[who])) {
        return;
    }
    const Vec3 after = read_vec3(transform, kTransformTranslation);
    const Vec3 after_rot = quat_pitch_yaw(transform);
    const void* const p0 = player0_pawn(owner);
    const Vec3 own_cam = camera_location(owner);
    const Vec3 p0_cam = camera_location(p0);
    log::line("card020: %s %s ApplyDynamicZeroThresholdAdjustment(%s, owner %s): muzzle (%.1f, %.1f, %.1f) P %.2f Y "
              "%.2f -> (%.1f, %.1f, %.1f) P %.2f Y %.2f; owner camera (%.1f, %.1f, %.1f), player 0 pawn %s camera "
              "(%.1f, %.1f, %.1f)%s",
              side(), kSprintSideText[who], object_name(weapon).text, object_name(owner).text, before.x, before.y,
              before.z, before_rot.x, before_rot.y, after.x, after.y, after.z, after_rot.x, after_rot.y, own_cam.x,
              own_cam.y, own_cam.z, object_name(p0).text, p0_cam.x, p0_cam.y, p0_cam.z,
              p0 == owner ? " (owner is player 0)" : " (owner is NOT player 0)");
}

// Card 024: a weapon's fire ability has the weapon as avatar, and the weapon does not replicate (its roles say
// host-own on the client), so the side is that of the weapon's Owner pawn. Card 181 shares it with the Fire hook.
const void* ability_shooter(const void* ability, const void** avatar_out) {
    const void* const actor_info = ability != nullptr ? read_ptr(ability, kAbilityActorInfo) : nullptr;
    const void* const avatar = avatar_of(actor_info);
    if (avatar_out != nullptr) {
        *avatar_out = avatar;
    }
    return is_a(avatar, g_player_mi_class()) || avatar == nullptr ? avatar : weapon_pawn(avatar);
}

void* detour_fire_projectile(void* ability, const double* transform, void* init_data_class) {
    void* const task = g_orig_fire_projectile(ability, transform, init_data_class);
    const void* avatar = nullptr;
    const void* const shooter = ability_shooter(ability, &avatar);
    const uint32_t who = sprint_side(shooter);
    ++g_c44_fires[who];
    if (who == kSideServerCopy) {
        c175_copy_round(shooter); // card 175: the per-pawn round count of a held relay
    }
    c76_mark_task(shooter, task);
    c132_proj_task(task, shooter);
    if (g_c76_log) {
        c76_fire(ability, avatar, transform, task);
    }
    if (note_event_cap(g_c20.fire_lines[who], kC20FireLines, kC20FireWhat[who])) {
        const Vec3 loc = read_vec3(transform, kTransformTranslation);
        const Vec3 rot = quat_pitch_yaw(transform);
        log::line("card020: %s %s FireProjectile(%s, avatar %s): origin (%.1f, %.1f, %.1f) P %.2f Y %.2f, task %p",
                  side(), kSprintSideText[who], class_name(ability).text, object_name(avatar).text, loc.x, loc.y,
                  loc.z, rot.x, rot.y, task);
    }
    return task;
}

void detour_interact(void* interactor) {
    c43_interact_edge(interactor, true);
    const void* const owner = read_ptr(interactor, kComponentOwner);
    if (c81_refuse_press(interactor, owner)) {
        return;
    }
    if (c130_is_revive_target(interact_target(interactor))) {
        return; // card 130 item 11: the revive prompt's press is only the card 043 hold, started above
    }
    const uint32_t who = sprint_side(owner);
    const bool logged = note_event_cap(g_c20.interact_lines[who], kC20InteractLines, kC20InteractWhat[who]);
    const void* const target = logged ? interact_target(interactor) : nullptr;
    c30_note_input("UADInteractorComponent::Interact", owner, nullptr);
    const bool own = g_client_session && who == kSideClientOwn; // card 137 2a: changes made by the own Interact
    if (own) {
        c137_own_input(true);
    }
    c102_interact(interactor);
    if (own) {
        c137_own_input(false);
    }
    if (logged) {
        log::line("card020: %s %s UADInteractorComponent::Interact(owner %s): target %s (%s), caller exe+0x%llX",
                  side(), kSprintSideText[who], object_name(owner).text, object_name(target).text,
                  class_name(target).text, exe_rva(_ReturnAddress()));
    }
}

// Card 043: the own pawn's interact release edge.
void detour_release_interaction(void* interactor) {
    c43_interact_edge(interactor, false);
    g_orig_release_interaction(interactor);
}

// The pawn definition's attribute init data class whose set class is set_class, or null.
void* camera_init_data(const void* pawn, const void* set_class) {
    const void* const def = read_ptr(pawn, kPawnDefinition);
    const void* const cdo = def != nullptr ? read_ptr(def, kClassDefaultObject) : nullptr;
    if (cdo == nullptr || read_ptr(cdo, kObjectClass) != def) {
        return nullptr;
    }
    const int32_t num = read_i32(cdo, kDefinitionAttrInitNum);
    void* const* const data = static_cast<void* const*>(read_ptr(cdo, kDefinitionAttrInit));
    const int32_t n = (data == nullptr || num <= 0) ? 0 : (num < kC18MaxInitData ? num : kC18MaxInitData);
    for (int32_t i = 0; i < n; ++i) {
        const void* const init_cdo = data[i] != nullptr ? read_ptr(data[i], kClassDefaultObject) : nullptr;
        if (init_cdo != nullptr && read_ptr(init_cdo, kInitDataSetClass) == set_class) {
            return data[i];
        }
    }
    return nullptr;
}

struct CapsuleSize {
    float radius;      // -1 = unresolved
    float half_height;
};

// Card 093: the pawn's root capsule size (reflected CapsuleRadius / CapsuleHalfHeight), -1 when unresolved.
CapsuleSize c93_capsule_size(const uint8_t* pawn) {
    const void* const root = read_ptr(pawn, kActorRootComponent);
    const void* const cls = root != nullptr ? read_ptr(root, kObjectClass) : nullptr;
    const uint8_t* const r = cls != nullptr ? prop_find_prop(cls, "CapsuleRadius") : nullptr;
    const uint8_t* const h = cls != nullptr ? prop_find_prop(cls, "CapsuleHalfHeight") : nullptr;
    if (r == nullptr || h == nullptr) {
        return CapsuleSize{-1.0f, -1.0f};
    }
    return CapsuleSize{read_f32(root, static_cast<size_t>(read_i32(r, kPropertyOffset))),
                       read_f32(root, static_cast<size_t>(read_i32(h, kPropertyOffset)))};
}

// Client own pawn, from card018_camera_fix (same once-per-ASC trigger, the sets have replicated).
void card019_movement_fix(void* asc, const uint8_t* pawn) {
    const void* const set = find_movement_set(asc);
    const void* const set_class = set != nullptr ? read_ptr(set, kObjectClass) : nullptr;
    const MovementLayout layout = set_class != nullptr ? movement_layout(set_class) : MovementLayout{};
    void* init = layout.walk_offset_ok && layout.net_props == 0 ? camera_init_data(pawn, set_class) : nullptr;
    if (init == nullptr) {
        log::line("WARN card019: client movement re-init skipped on %s: movement set %p, layout ok %d, CPF_Net "
                  "attributes %d/%d, init data %s",
                  object_name(asc).text, set, layout.walk_offset_ok ? 1 : 0, layout.net_props, layout.props,
                  layout.walk_offset_ok && layout.net_props == 0 ? "NOT FOUND in the pawn definition" : "not looked up");
        return;
    }
    const float base_before = read_f32(set, kWalkSpeedOffset + kAttrDataBase);
    const float current_before = read_f32(set, kWalkSpeedOffset + kAttrDataCurrent);
    g_orig_add_attribute_set(asc, &init); // TSubclassOf by value: hidden pointer to the caller's copy
    const void* const after = find_movement_set(asc);
    log::line("card019: client movement re-init AddAttributeSetFromData(%s) on %s (pawn %s LocalRole %u): "
              "movement set %p -> %p, WalkSpeed base %.3f current %.3f -> base %.3f current %.3f",
              object_name(init).text, object_name(asc).text, class_name(pawn).text, pawn[kActorLocalRole], set, after,
              static_cast<double>(base_before), static_cast<double>(current_before),
              after != nullptr ? static_cast<double>(read_f32(after, kWalkSpeedOffset + kAttrDataBase)) : -1.0,
              after != nullptr ? static_cast<double>(read_f32(after, kWalkSpeedOffset + kAttrDataCurrent)) : -1.0);
    if (after == nullptr) {
        return;
    }
    // Card 075: a pawn whose aggregators already exist (the mech: its GEs ran before the pilot's re-init) keeps
    // their base 0, so the next GE re-aggregation (InputTag.Move) set WalkSpeed 750 -> 0. Push every base through
    // SetNumericAttributeBase, which updates the aggregator too.
    const CapsuleSize capsule_before = c93_capsule_size(pawn);
    const int pushed = card019_sync_aggregators(asc, after);
    const CapsuleSize capsule_after = c93_capsule_size(pawn);
    log::line("card019: client movement bases pushed through SetNumericAttributeBase: %d attributes, WalkSpeed base "
              "%.3f current %.3f; capsule radius %.2f half height %.2f -> radius %.2f half height %.2f", pushed,
              static_cast<double>(read_f32(after, kWalkSpeedOffset + kAttrDataBase)),
              static_cast<double>(read_f32(after, kWalkSpeedOffset + kAttrDataCurrent)),
              static_cast<double>(capsule_before.radius), static_cast<double>(capsule_before.half_height),
              static_cast<double>(capsule_after.radius), static_cast<double>(capsule_after.half_height));
}

// Card 081: true = the press is refused (the owner is down); card 043's edge has already run.
bool c81_refuse_press(const void* interactor, const void* owner) {
    if (!c43_is_down(owner)) {
        return false;
    }
    ++g_c81.presses;
    if (note_event_cap(g_c81.press_lines, kC81Lines, "card081 press")) {
        log::line("card081: %s t=%llu Interact refused: %s (%s) is %s, target %s (refusal #%u)", side(),
                  static_cast<unsigned long long>(GetTickCount64()), object_name(owner).text,
                  kSprintSideText[sprint_side(owner)], c43_life_text(owner), object_name(interact_target(interactor)).text,
                  g_c81.presses);
    }
    return true;
}

void c24_aim_begin(const void* owner) {
    g_c24_aim.pawn = !g_client_session && sprint_side(owner) == kSideServerCopy ? owner : nullptr;
}

void c24_aim_end() {
    g_c24_aim.pawn = nullptr;
}

// Every own FGameplayAttributeData of the set: SetNumericAttributeBase(its current base), so an existing aggregator
// takes the re-init base. Returns how many were pushed; -1 when the functions are missing.
int card019_sync_aggregators(void* asc, const void* set) {
    if (g_set_numeric_attribute_base == nullptr || g_gameplay_attribute_ctor == nullptr) {
        return -1;
    }
    int pushed = 0;
    void* field = read_ptr(read_ptr(set, kObjectClass), kStructChildProperties);
    for (int f = 0; f < kC18MaxProps && field != nullptr; ++f, field = read_ptr(field, kFieldNext)) {
        const int32_t off = read_i32(field, kPropertyOffset);
        const ObjectName kind = fname_text(read_ptr(field, kFieldClassPrivate));
        if (strcmp(kind.text, "StructProperty") != 0 || read_i32(field, kPropertyElementSize) != kAttributeDataBytes ||
            off < 0) {
            continue;
        }
        const GameplayAttributeRaw* const attr = c19_attribute(field);
        if (attr == nullptr) {
            log::line("WARN card019: attribute cache full (%d)", kC18MaxProps);
            return pushed;
        }
        // Card 093: a zero base is skipped. An aggregator's base already starts at 0, so the push would change
        // nothing there, but it still runs PostAttributeChange: CapsuleCollision_Radius / _HalfHeight 0 resized the
        // on-foot trooper's capsule to 0 on the client, which then never found a floor (MOVE_Falling every frame).
        const float base = read_f32(set, static_cast<size_t>(off) + kAttrDataBase);
        if (base == 0.0f) {
            continue;
        }
        g_set_numeric_attribute_base(asc, attr, base);
        ++pushed;
    }
    return pushed;
}

// The object whose blueprint called SetInputMode_UIOnlyEx: the focus target when UIOnly is restored.
void c31_note_ui_only(const void* object) {
    if (object == nullptr) {
        return;
    }
    c31_weak_set(g_c31.game_focus, const_cast<void*>(object));
}

// UADInteractorComponent::Interact / ReleaseInteraction entry: this machine's own pawn's interact edges.
void c43_interact_edge(void* interactor, bool pressed) {
    void* const owner = read_ptr(interactor, kComponentOwner);
    const uint32_t s = sprint_side(owner);
    if ((s != kSideHostOwn && s != kSideClientOwn) || pressed == g_c43.own_held) {
        return;
    }
    g_c43.own_held = pressed;
    log::line("card043: %s t=%llu own interact %s (%s)", side(), static_cast<unsigned long long>(GetTickCount64()),
              pressed ? "press" : "release", pressed ? "Interact" : "ReleaseInteraction");
    if (s == kSideHostOwn && net_mode(owner) == kNetModeListenServer) {
        c43_use_edge(owner, pressed);
    } else if (s == kSideClientOwn && g_client_session) {
        if (pressed) {
            c50_send_use_name(interactor, read_ptr(owner, kPawnController)); // card 050: before the Use 1
            c123_client_press(interactor);                                     // card 123: turret CanInteract
        }
        g_c43.n.use_sent += chan_to_host(read_ptr(owner, kPawnController), ChanOp::Use, pressed ? 1 : 0) ? 1u : 0u;
    }
}

// Client, before its Use press goes: the placed pickup its interactor targets, by name (Addendum 5).
void c50_send_use_name(void* interactor, void* pc) {
    const void* const target = interact_target(interactor);
    char name[kChanNameMax + 1];
    const int32_t len = c50_name(target, name, sizeof(name));
    const bool placed = c50_placed(target);
    const bool relayable = c54_relay_row(target) != nullptr;
    bool sent = false;
    char text[kChanTextMax + 1];
    // Card 306: a runtime pickup's copy goes by the host's id ("#id x y z"), so the host takes the client's target
    // and not its copy pawn's own trace (a support swap there drops the old support for both).
    const uint32_t copy_id = !placed && relayable ? c104_copy_host_id(target) : 0;
    const Vec3 copy_at = actor_location(target);
    const bool by_id = copy_id != 0 && snprintf(text, sizeof(text), "#%u %.1f %.1f %.1f", copy_id, copy_at.x,
                                                copy_at.y, copy_at.z) > 0;
    if (len > 0 && (by_id || (placed && relayable && c95_wire_target(target, text, sizeof(text)) > 0))) { // card 133
        wchar_t wire[kChanTextFrame];
        const WireString w = chan_text_frame(ChanOp::UseName, text, wire);
        sent = w.num > 0 && chan_send_host(pc, w);
    }
    if (c50_line()) {
        log::line("card050: client t=%llu Use press: interactor target %s (%s), placed %d, relayed class %d: "
                  "UseName %s",
                  static_cast<unsigned long long>(GetTickCount64()), len > 0 ? name : object_name(target).text,
                  class_name(target).text, placed ? 1 : 0, relayable ? 1 : 0,
                  sent ? (by_id ? "sent by host id" : "sent")
                       : (relayable && (placed || by_id) ? "WARN not sent" : "not sent (not a placed relayed class or copy)"));
    }
}

// Card 125: a mounted turret's rounds are the same case as a mech's (pooled ADProjectile, not replicated), so its fire
// tasks take the same relay: the gunner's client is skipped, every other client gets a copy.
// Card 132 step 2b: a player's own weapons are the same case too (the host's NukeLauncher and rifles spawned nothing on
// the client), so a player shooter's tasks take this relay as well; a grenade throw among them still goes by the card
// 074 path (c74_relay checks it first).
void c76_mark_task(const void* shooter, const void* task) {
    if (!g_client_session && task != nullptr &&
        (is_a(shooter, g_mech_class()) || is_a(shooter, g_turret_class()) || is_a(shooter, g_player_mi_class()))) {
        g_c76_mech.task = task;
        c31_weak_set(g_c76_mech.pilot_pc, read_ptr(shooter, kPawnController));
        c31_weak_set(g_c76_mech.vehicle, const_cast<void*>(shooter));
    }
}

// Both sides, from the FireProjectile detour: a fire task whose avatar is a mech or a mech's weapon.
void c76_fire(const void* ability, const void* avatar, const double* transform, const void* task) {
    const void* const mech = c76_mech(avatar);
    c297_shot(mech); // card 297 candidate 2 (diagnostic): the attack tag at each client shot of its own mech
    if (mech == nullptr || !c76_note(kC76Fire)) {
        return;
    }
    const Vec3 at = read_vec3(transform, kTransformTranslation);
    const Vec3 rot = quat_pitch_yaw(transform);
    log::line("card076: %s t=%llu mech %s FireProjectile(%s, avatar %s): origin (%.1f, %.1f, %.1f) P %.2f Y %.2f, "
              "task %p", side(), c76_now(), object_name(mech).text, class_name(ability).text,
              object_name(avatar).text, at.x, at.y, at.z, rot.x, rot.y, task);
}

// ---- Cards 297 / 298 (b8, diagnostic only): the client pilot's mech state at the mount ----
// Card 297 (cockpit arms): FirstPersonMesh visibility, tick and anim instance at the possession, then every change of
// the mech ASC's owned tags while the client pilots (the arms' attack blend reads a gameplay tag: the row shows
// whether it ever arrives on the client). Card 298 (look down): the camera manager's pitch limits, the camera set's
// pitch attributes, the controller, and the mech set's fuel, at the possession and at the client camera re-init.
namespace {
constexpr size_t kCmViewPitchMin = 0x27AC; // APlayerCameraManager::ViewPitchMin (card 298 trace)
constexpr size_t kCmViewPitchMax = 0x27B0; // APlayerCameraManager::ViewPitchMax
constexpr int32_t kC297MaxTags = 32;
constexpr uint32_t kC297TagLines = 60;     // tag change rows per map
constexpr uint32_t kC298Lines = 12;        // mount rows per map
constexpr size_t kMechFirstPersonMesh = 0x848; // ASTPlayerCharacter FirstPersonMesh (SetMeshVisibility callers)
constexpr size_t kMechSecondMesh = 0xC20;      // the mech's other mesh: shown on foot, hidden while piloted
constexpr ULONGLONG kC297CheckMs = 1000;
constexpr uint32_t kC297FixLines = 40;
constexpr uint32_t kC297ShotLines = 40;
// The cockpit ABP (ABP_Player_RazorbackMech_Cockpit) blends its attack poses on HasMatchingGameplayTag; its only
// tag literals are WeaponSystem.EquippedWeapon.Fire and WeaponSystem.EquippedWeapon.Melee.
constexpr const char* kC297AttackTag = "WeaponSystem.EquippedWeapon.Fire";

struct C297State {
    C31Weak mech;                  // the client's own piloted mech, null when on foot
    uint64_t tags[kC297MaxTags];   // its owned tags at the last step
    int32_t tag_n;
    uint32_t tag_lines;
    uint32_t mount_lines;
    uint64_t tick_fname;
    uint64_t visible_fname;
    C31Weak pilot;                 // the mech the local PC's pawn was at the last step (card 297 fix), null on foot
    ULONGLONG check_ms;            // the next 1 s check of the piloted mech's arms
    uint32_t fix_lines;            // enter, exit and WARN rows per map
    uint32_t shot_lines;           // per-shot attack tag rows per map
};
C297State g_c297{};

// The ASC's owned tags (ExplicitTags), at most kC297MaxTags; 0 when the array reads bad.
int32_t c297_read_tags(const void* asc, uint64_t* out) {
    const int32_t num = asc != nullptr ? read_i32(asc, kAscOwnedTags + sizeof(void*)) : 0;
    const int32_t max = asc != nullptr ? read_i32(asc, kAscOwnedTags + sizeof(void*) + sizeof(int32_t)) : 0;
    const auto* const data = asc != nullptr ? static_cast<const uint64_t*>(read_ptr(asc, kAscOwnedTags)) : nullptr;
    const int32_t n = data == nullptr || num <= 0 || num > max ? 0 : (num < kC297MaxTags ? num : kC297MaxTags);
    if (n > 0) {
        memcpy(out, data, static_cast<size_t>(n) * sizeof(uint64_t));
    }
    return n;
}

void c297_tags_text(const uint64_t* tags, int32_t n, char* out, size_t cap) {
    size_t used = 0;
    out[0] = '\0';
    for (int32_t i = 0; i < n; ++i) {
        const int w = snprintf(out + used, cap - used, "%s%s", i == 0 ? "" : ", ", fname_text(&tags[i]).text);
        if (w < 0 || static_cast<size_t>(w) >= cap - used) {
            return;
        }
        used += static_cast<size_t>(w);
    }
}

// A reflected FGameplayAttributeData of `set` as "base/current", "-" when the set or the name is missing.
void c298_attr_text(const void* set, const char* name, char* out, size_t cap) {
    const uint8_t* const prop = set != nullptr ? prop_find_prop(read_ptr(set, kObjectClass), name) : nullptr;
    if (prop == nullptr) {
        snprintf(out, cap, "-");
        return;
    }
    const size_t off = static_cast<size_t>(read_i32(prop, kPropertyOffset));
    snprintf(out, cap, "%.2f/%.2f", read_f32(set, off + kAttrDataBase), read_f32(set, off + kAttrDataCurrent));
}

} // namespace

// Card 430 (c423b129a, client): the game's reload try at the nuke launcher's WindDown end returned 0 with
// WeaponSystem.Blocked and SpoolDown still owned by the pawn, and the game made no further try (28 s, until a key
// press); on other shots its own second try about 1 s later went through. A failed try of the own weapon's reload
// under WeaponSystem.Blocked is made again here once the tag is gone: at most kC430Tries tries, kC430GapMs apart,
// within kC430RetryMs. A try that failed with the tag absent (full magazine, no reserve) is not retried.
namespace {
constexpr ULONGLONG kC430RetryMs = 3000;
constexpr ULONGLONG kC430GapMs = 100;
constexpr uint8_t kC430Tries = 5;
struct C430Retry {
    C31Weak pawn;
    int32_t handle;
    ULONGLONG at, next;
    uint8_t tries;
    bool armed, in_try;
};
C430Retry g_c430{};

// After a reload try of the own weapon: a success ends the retry; a failure under the tag starts one.
void c430_note(const void* pawn, int32_t handle, bool ok, bool server_request, uint8_t active) {
    if (ok) {
        g_c430.armed = false;
        return;
    }
    if (g_c430.armed || g_c430.in_try || server_request || active != 0 || c352_blocked(pawn, kFireReload) <= 0) {
        return;
    }
    g_c430 = C430Retry{};
    c31_weak_set(g_c430.pawn, const_cast<void*>(pawn));
    g_c430.handle = handle;
    g_c430.at = GetTickCount64();
    g_c430.next = g_c430.at;
    g_c430.armed = true;
}
} // namespace

void c430_retry_step(ULONGLONG now) {
    if (!g_c430.armed) {
        return;
    }
    void* const pawn = c31_weak_get(g_c430.pawn);
    const void* weapon = nullptr;
    void* const asc = pawn != nullptr ? c24_weapon_asc(pawn, &weapon) : nullptr;
    const uint8_t* const spec = asc != nullptr ? c21_spec_for(asc, g_c430.handle) : nullptr;
    const void* const ability = spec != nullptr ? read_ptr(spec, 0x10) : nullptr;
    const bool same = ability != nullptr && strncmp(class_name(ability).text, "BP_WeaponReloadAbility", 22) == 0;
    if (!same || now - g_c430.at > kC430RetryMs || g_c430.tries >= kC430Tries) {
        log::line("%scard430: client t=%llu reload retry ended without a reload: %u tries in %llu ms (%s)",
                  same ? "WARN " : "", static_cast<unsigned long long>(now), static_cast<unsigned>(g_c430.tries),
                  static_cast<unsigned long long>(now - g_c430.at), same ? "tries or time used up" : "weapon changed");
        g_c430.armed = false;
        return;
    }
    if (now < g_c430.next || c352_blocked(pawn, kFireReload) != 0 || g_orig_try_activate == nullptr) {
        return;
    }
    g_c430.in_try = true;
    ++g_c430.tries;
    g_c430.next = now + kC430GapMs;
    const bool ok = g_orig_try_activate(asc, g_c430.handle, true);
    g_c430.in_try = false;
    log::line("card430: client t=%llu reload retry %u (handle %d) = %d, %llu ms after the try that failed under "
              "WeaponSystem.Blocked", static_cast<unsigned long long>(now), static_cast<unsigned>(g_c430.tries),
              g_c430.handle, ok ? 1 : 0, static_cast<unsigned long long>(now - g_c430.at));
    g_c430.armed = g_c430.armed && !ok;
}

// Card 430 (diagnostic), client, after InternalTryActivateAbility: the tries of the own soldier weapon's reload
// ability, with the result, the spec's ActiveCount and the owned tags of the weapon ASC and of the pawn ASC. A failed
// try has no ability out, so the class comes from the spec's ability. One row per change of that text (the repeat
// count of the row before it is on the next row), at most kC430Lines rows. Run c423b127a: no client reload followed
// the nuke launcher's WindDown end, and the capped card 021 rows could not tell "tried and failed" from "never tried".
void c430_reload_try(const void* asc, int32_t handle, bool ok, bool server_request) {
    constexpr uint32_t kC430Lines = 60;
    static uint32_t lines = 0;
    static uint32_t repeats = 0;
    static char last[768] = "";
    const uint8_t* const spec = g_client_session && asc != nullptr ? c21_spec_for(asc, handle) : nullptr;
    const void* const ability = spec != nullptr ? read_ptr(spec, 0x10) : nullptr;
    if (ability == nullptr || strncmp(class_name(ability).text, "BP_WeaponReloadAbility", 22) != 0) {
        return;
    }
    const void* const owner = read_ptr(asc, kComponentOwner);
    const void* const pawn = owner != nullptr ? weapon_pawn(owner) : nullptr;
    if (pawn == nullptr || sprint_side(pawn) != kSideClientOwn) {
        return;
    }
    c430_note(pawn, handle, ok, server_request, spec[kSpecActiveCount]);
    uint64_t tags[kC297MaxTags];
    char weapon_tags[256];
    char pawn_tags[256];
    const int32_t wn = c297_read_tags(asc, tags);
    c297_tags_text(tags, wn, weapon_tags, sizeof(weapon_tags));
    const int32_t pn = c297_read_tags(read_ptr(pawn, kCharacterAsc), tags);
    c297_tags_text(tags, pn, pawn_tags, sizeof(pawn_tags));
    char text[768];
    snprintf(text, sizeof(text), "handle %d %s = %d%s, ActiveCount %d; weapon ASC owned tags %d: %s; pawn ASC owned "
             "tags %d: %s", handle, class_name(ability).text, ok ? 1 : 0, server_request ? " (server request)" : "",
             static_cast<int>(spec[kSpecActiveCount]), wn, weapon_tags, pn, pawn_tags);
    if (strcmp(text, last) == 0) {
        ++repeats;
        return;
    }
    if (note_event_cap(lines, kC430Lines, "card430 reload try")) {
        log::line("card430: client t=%llu reload try %s (the row before repeated %u times)",
                  static_cast<unsigned long long>(GetTickCount64()), text, repeats);
    }
    snprintf(last, sizeof(last), "%s", text);
    repeats = 0;
}

// Cards 297 / 291 (diagnostic): a native bool getter (IsVisible, IsComponentTickEnabled) by ProcessEvent: 1 or 0, -1
// when the object has no such function with a one-byte parms block.
int32_t c297_bool_call(void* object, uint64_t& cache, const wchar_t* name) {
    const uint64_t fn_name = fname_cached(cache, name);
    void* const fn = object != nullptr && fn_name != 0 ? g_find_function(object, fn_name) : nullptr;
    if (fn == nullptr || (read_i32(fn, kFunctionParmsSize) & 0xFFFF) != 1) {
        return -1;
    }
    alignas(16) uint8_t parms[16] = {};
    g_orig_process_event(object, fn, parms);
    return parms[0] != 0 ? 1 : 0;
}

// Card 298: one mount row for a mech at `when`. Client: its own mech (possession, after the camera re-init). Host: the
// mech a client entered (card 075 MechEnter), for its Fuel and MaximumFuel; the camera manager there is the host's own.
void c298_mount_row(const char* when, const void* pawn) {
    if (pawn == nullptr || !is_a(pawn, g_mech_class()) ||
        !note_event_cap(g_c297.mount_lines, kC298Lines, "card298 mount row")) {
        return;
    }
    const void* const controller = read_ptr(pawn, kPawnController);
    const void* const pc = g_get_player_controller(pawn, 0);
    const void* const cm = pc != nullptr ? read_ptr(pc, kPcCameraManager) : nullptr;
    const void* const asc = read_ptr(pawn, kCharacterAsc);
    const void* const camera = asc != nullptr ? read_attribute_sets(asc).camera_set : nullptr;
    const void* const mech_set = asc != nullptr ? find_set_by_class_name(asc, "STPlayerMechAttributeSet") : nullptr;
    char pmin[32];
    char pmax[32];
    char fuel[32];
    char fuel_max[32];
    c298_attr_text(camera, "CameraPitchMin", pmin, sizeof(pmin));
    c298_attr_text(camera, "CameraPitchMax", pmax, sizeof(pmax));
    c298_attr_text(mech_set, "Fuel", fuel, sizeof(fuel));
    c298_attr_text(mech_set, "MaximumFuel", fuel_max, sizeof(fuel_max));
    log::line("card298: %s t=%llu mech %s %s: controller %s, camera manager %s pitch min %.2f max %.2f, camera set %s "
              "CameraPitchMin %s CameraPitchMax %s, mech set %s Fuel %s MaximumFuel %s",
              side(), static_cast<unsigned long long>(GetTickCount64()), object_name(pawn).text, when,
              controller != nullptr ? object_name(controller).text : "null", cm != nullptr ? object_name(cm).text : "null",
              cm != nullptr ? read_f32(cm, kCmViewPitchMin) : 0.0f, cm != nullptr ? read_f32(cm, kCmViewPitchMax) : 0.0f,
              camera != nullptr ? class_name(camera).text : "null", pmin, pmax,
              mech_set != nullptr ? class_name(mech_set).text : "null", fuel, fuel_max);
}

// Card 459 (rows: diagnostic, UBWCOOP_DIAG or UBWCOOP_C50_TEST): once a second, Fuel and MaximumFuel (base/current)
// of each mech pawn with an STPlayerMechAttributeSet: the client's own pawn, every PC's piloted mech on the host. The
// set has no replicated attribute (card020 row: CPF_Net 0), so the host sends each remote PC its piloted mech's
// current Fuel and MaximumFuel (ChanOp::MechFuel, 8 bytes) when they differ from the last values sent to that PC, and
// the client writes them into its own mech's set (base and current, the HUD delegate fires).
// b164 (card 459 first-second gap, c447b163a): the client keeps the last received values while its own pawn is a mech
// or none (cleared when it turns from a mech into a non-mech), writes them right after the card 020 re-init of the
// mech set (which sets Fuel to its init value 100), and from the step whenever the set's value differs from the kept
// one. The first frame of a mount can arrive after the re-init (it waited in the host's pace queue through the
// client's mount stall), so the step row says "host value none" until then.
namespace {
constexpr ULONGLONG kC459PeriodMs = 1000;
constexpr float kC459MaxFuel = 1.0e6f;     // sanity bound on a received value
constexpr int32_t kC459Bytes = 8;          // Fuel, MaximumFuel: two little-endian floats
// Host, per PC slot: the last values sent. pc/pawn are object indices; a change of either resets valid, so a mount
// and a rejoin both send at once. valid is set only when a send succeeds, so a refused send retries next second.
struct C459Sent {
    int32_t pc, pawn;
    float fuel, max;
    bool valid;
};
struct C459Kept { // client: the last received values for the own mech (valid until the own pawn leaves the mech)
    float fuel, max;
    bool valid;
};
C459Sent g_c459_sent[kC17Slots] = {};
C459Kept g_c459_kept = {};

float c459_current(const void* set, const char* name, bool* found) {
    const uint8_t* const prop = prop_find_prop(read_ptr(set, kObjectClass), name);
    *found = *found && prop != nullptr;
    return prop != nullptr ? read_f32(set, static_cast<size_t>(read_i32(prop, kPropertyOffset)) + kAttrDataCurrent)
                           : 0.0f;
}

// The pawn's STPlayerMechAttributeSet, or null.
const void* c459_set(const void* pawn) {
    const void* const asc = pawn != nullptr ? read_ptr(pawn, kCharacterAsc) : nullptr;
    return asc != nullptr ? find_set_by_class_name(asc, "STPlayerMechAttributeSet") : nullptr;
}

// Host: one PC's step. Sends when the values differ from the last sent to this PC (pc and pawn the same).
void c459_host_send(int32_t slot, void* pc, const void* pawn, const void* set) {
    C459Sent& last = g_c459_sent[slot];
    const int32_t pc_id = pc != nullptr ? object_index(pc) : -1;
    const int32_t pawn_id = set != nullptr ? object_index(pawn) : -1;
    if (last.pc != pc_id || last.pawn != pawn_id) {
        last = C459Sent{pc_id, pawn_id, 0.0f, 0.0f, false};
    }
    if (set == nullptr || !c26_is_remote_pc(pc)) {
        return;
    }
    bool found = true;
    const float fuel = c459_current(set, "Fuel", &found);
    const float max = c459_current(set, "MaximumFuel", &found);
    if (!found || (last.valid && last.fuel == fuel && last.max == max)) {
        return;
    }
    uint8_t bytes[kC459Bytes];
    memcpy(bytes, &fuel, sizeof(fuel));
    memcpy(bytes + sizeof(fuel), &max, sizeof(max));
    static wchar_t wire[kChanBlobFrame]; // game thread only
    const WireString w = chan_blob_frame(static_cast<wchar_t>(ChanOp::MechFuel), bytes, kC459Bytes, wire);
    if (w.num > 0 && chan_send_client(pc, w)) {
        last = C459Sent{pc_id, pawn_id, fuel, max, true};
    }
}

// Client: writes the kept values into the own mech's set (MaximumFuel first, so Fuel is not clamped to an old
// maximum). False when the own pawn is not a mech with the set or nothing is kept (the values stay kept), or an
// attribute is missing (WARN, the values are dropped).
bool c459_apply(const void* pawn) {
    const void* const set = pawn != nullptr && is_a(pawn, g_mech_class()) ? c459_set(pawn) : nullptr;
    if (set == nullptr || !g_c459_kept.valid) {
        return false;
    }
    void* const asc = read_ptr(pawn, kCharacterAsc);
    const char* const names[] = {"MaximumFuel", "Fuel"};
    const float values[] = {g_c459_kept.max, g_c459_kept.fuel};
    for (int32_t k = 0; k < 2; ++k) {
        const uint8_t* const prop = prop_find_prop(read_ptr(set, kObjectClass), names[k]);
        const GameplayAttributeRaw* const attr = prop != nullptr ? c19_attribute(const_cast<uint8_t*>(prop)) : nullptr;
        if (attr == nullptr || g_set_numeric_attribute_base == nullptr) {
            g_c459_kept.valid = false;
            log::line("WARN card459: client mech %s: %s not written (attribute %s)", object_name(pawn).text, names[k],
                      prop == nullptr ? "missing" : "cache full");
            return false;
        }
        g_set_numeric_attribute_base(asc, attr, values[k]);
    }
    return true;
}

// Client diag row of one write attempt: the kept values, the own pawn, applied 0/1 and the set's Fuel read back.
void c459_apply_row(const char* from, const void* pawn, bool applied) {
    if (!g_gate_c62_diag) {
        return;
    }
    const void* const set = pawn != nullptr && is_a(pawn, g_mech_class()) ? c459_set(pawn) : nullptr;
    char fuel[32] = "-";
    if (set != nullptr) {
        c298_attr_text(set, "Fuel", fuel, sizeof(fuel));
    }
    log::line("card459: client t=%llu %s: Fuel %.2f MaximumFuel %.2f, own pawn %s, applied %d, Fuel now %s",
              static_cast<unsigned long long>(GetTickCount64()), from, static_cast<double>(g_c459_kept.fuel),
              static_cast<double>(g_c459_kept.max), pawn != nullptr ? class_name(pawn).text : "none", applied ? 1 : 0,
              fuel);
}

// Client step: drops the kept values when the own pawn turns from a mech into a non-mech, and writes them when the
// own mech's set differs from them.
void c459_client_step(const void* pawn, const void* set) {
    static bool was_mech = false; // game thread
    const bool mech = set != nullptr;
    if (was_mech && pawn != nullptr && !is_a(pawn, g_mech_class())) {
        g_c459_kept.valid = false;
    }
    was_mech = pawn != nullptr ? is_a(pawn, g_mech_class()) : was_mech;
    if (!mech || !g_c459_kept.valid) {
        return;
    }
    bool found = true;
    const float fuel = c459_current(set, "Fuel", &found);
    const float max = c459_current(set, "MaximumFuel", &found);
    if (found && fuel == g_c459_kept.fuel && max == g_c459_kept.max) {
        return;
    }
    c459_apply_row("step write", pawn, c459_apply(pawn));
}
} // namespace

// Client: ChanOp::MechFuel from the host, the piloted mech's current Fuel and MaximumFuel.
void c459_receive(void* pc, const WireString* s) {
    if (!g_client_session) {
        return;
    }
    static uint8_t bytes[kChanBlobBytes]; // game thread only
    const int32_t n = chan_blob(s, bytes, kChanBlobBytes);
    float fuel = 0.0f;
    float max = 0.0f;
    if (n == kC459Bytes) {
        memcpy(&fuel, bytes, sizeof(fuel));
        memcpy(&max, bytes + sizeof(fuel), sizeof(max));
    }
    // Negated compares: a NaN fails every one of them.
    if (n != kC459Bytes || !(max > 0.0f && max <= kC459MaxFuel) || !(fuel >= 0.0f && fuel <= kC459MaxFuel)) {
        log::line("WARN card459: client MechFuel frame dropped (%d bytes, Fuel %.2f MaximumFuel %.2f)", n, fuel, max);
        return;
    }
    // A frame sent before the host saw the dismount must not outlive it: c459_client_step clears the kept values on the
    // mech-to-foot edge, and a late frame stored after that would come back on the next mech. Pawn none still stores
    // (the mech is not possessed yet).
    const void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    if (pawn != nullptr && !is_a(pawn, g_mech_class())) {
        log::line("card459: client t=%llu receive dropped: Fuel %.2f MaximumFuel %.2f, own pawn %s is not a mech",
                  static_cast<unsigned long long>(GetTickCount64()), static_cast<double>(fuel),
                  static_cast<double>(max), class_name(pawn).text);
        return;
    }
    g_c459_kept = C459Kept{fuel, max, true};
    c459_apply_row("receive", pawn, c459_apply(pawn));
}

// Client: card 020 just re-ran AddAttributeSetFromData on the own mech's STPlayerMechAttributeSet (Fuel back to its
// init value): the kept host values go back in.
void c459_after_reinit(const void* pawn) {
    if (g_client_session && g_c459_kept.valid) {
        c459_apply_row("after the card020 re-init", pawn, c459_apply(pawn));
    }
}

void c459_step(void* engine) {
    static ULONGLONG next_ms = 0; // game thread
    const ULONGLONG now = GetTickCount64();
    if (now < next_ms) {
        return;
    }
    next_ms = now + kC459PeriodMs;
    void* const world = g_get_game_world(engine);
    const int32_t pcs = g_client_session ? 1 : kC17Slots;
    for (int32_t i = 0; world != nullptr && i < pcs; ++i) {
        void* const pc = g_get_player_controller(world, i);
        const auto* const pawn = pc != nullptr ? static_cast<const uint8_t*>(read_ptr(pc, kControllerPawn)) : nullptr;
        const bool mech = pawn != nullptr && is_a(pawn, g_mech_class());
        const void* const set = mech ? c459_set(pawn) : nullptr;
        if (g_client_session) {
            c459_client_step(pawn, set);
        } else {
            c459_host_send(i, pc, pawn, set);
        }
        if (set == nullptr || !g_gate_c62_diag) {
            continue;
        }
        char fuel[32];
        char fuel_max[32];
        c298_attr_text(set, "Fuel", fuel, sizeof(fuel));
        c298_attr_text(set, "MaximumFuel", fuel_max, sizeof(fuel_max));
        log::line("card459: %s t=%llu mech %s (pc %d, role %d): Fuel %s MaximumFuel %s%s", side(),
                  static_cast<unsigned long long>(now), object_name(pawn).text, i, static_cast<int>(pawn[kActorLocalRole]),
                  fuel, fuel_max, g_client_session && !g_c459_kept.valid ? "; host value none" : "");
    }
}

// Card 297: the client possessed `pawn` after a mount press: the enter row for a mech, and the tag watch armed.
void c297_possessed(const void* pawn) {
    const bool mech = pawn != nullptr && is_a(pawn, g_mech_class());
    c31_weak_set(g_c297.mech, mech ? const_cast<void*>(pawn) : nullptr);
    g_c297.tag_n = 0;
    if (!mech) {
        return;
    }
    void* const arms = c31_object_prop(pawn, "FirstPersonMesh");
    const void* const anim = arms != nullptr ? c31_object_prop(arms, "AnimScriptInstance") : nullptr;
    char props[kC22LineChars];
    if (arms != nullptr) {
        c22_comp_text(arms, props, sizeof(props));
    } else {
        snprintf(props, sizeof(props), "-");
    }
    g_c297.tag_n = c297_read_tags(read_ptr(pawn, kCharacterAsc), g_c297.tags);
    char tags[512];
    c297_tags_text(g_c297.tags, g_c297.tag_n, tags, sizeof(tags));
    log::line("card297: %s t=%llu mech %s enter: FirstPersonMesh %s IsVisible %d IsComponentTickEnabled %d, anim "
              "instance %s (%s); %s; owned tags %d: %s",
              side(), static_cast<unsigned long long>(GetTickCount64()), object_name(pawn).text,
              arms != nullptr ? object_name(arms).text : "null",
              c297_bool_call(arms, g_c297.visible_fname, L"IsVisible"),
              c297_bool_call(arms, g_c297.tick_fname, L"IsComponentTickEnabled"),
              anim != nullptr ? object_name(anim).text : "null", anim != nullptr ? class_name(anim).text : "-", props,
              g_c297.tag_n, tags);
}

// Card 297, each tick on the client: a row per owned tag added to or removed from the piloted mech's ASC.
void c297_tags_step();

SetMeshVisibilityFn g_c297_set_mesh_visibility = nullptr;

namespace {

// How many of the mech ASC's owned tags are kC297AttackTag (an owned tag appears once, so 0 or 1).
int32_t c297_attack_count(const void* mech) {
    uint64_t tags[kC297MaxTags];
    const int32_t n = c297_read_tags(read_ptr(mech, kCharacterAsc), tags);
    int32_t count = 0;
    for (int32_t i = 0; i < n; ++i) {
        count += strcmp(fname_text(&tags[i]).text, kC297AttackTag) == 0 ? 1 : 0;
    }
    return count;
}

// Card 297 fix: the exe's own mesh swap on `mech`, as OnBoardingComplete+0x19A (enter: arms shown, other mesh
// hidden) and LeaveMech+0x5E4 (exit: the reverse) do it, then the row. SetMeshVisibility is visual only.
void c297_swap(void* mech, bool enter, const char* why) {
    void* const arms = read_ptr(mech, kMechFirstPersonMesh);
    void* const body = read_ptr(mech, kMechSecondMesh);
    const int32_t arms_before = c297_bool_call(arms, g_c297.visible_fname, L"IsVisible");
    const int32_t body_before = c297_bool_call(body, g_c297.visible_fname, L"IsVisible");
    if (g_c297_set_mesh_visibility != nullptr) {
        g_c297_set_mesh_visibility(mech, arms, enter);
        g_c297_set_mesh_visibility(mech, body, !enter);
    }
    if (!note_event_cap(g_c297.fix_lines, kC297FixLines, "card297 fix row")) {
        return;
    }
    const void* const anim = arms != nullptr ? c31_object_prop(arms, "AnimScriptInstance") : nullptr;
    log::line("card297: client t=%llu mech %llu %s (%s): FirstPersonMesh visible %d -> %d, second mesh visible %d -> "
              "%d, anim instance %s, attack tag %s count %d%s",
              static_cast<unsigned long long>(GetTickCount64()), static_cast<unsigned long long>(c41_guid(mech)),
              enter ? "enter" : "exit", why, arms_before, c297_bool_call(arms, g_c297.visible_fname, L"IsVisible"),
              body_before, c297_bool_call(body, g_c297.visible_fname, L"IsVisible"),
              anim != nullptr ? class_name(anim).text : "null", kC297AttackTag, c297_attack_count(mech),
              g_c297_set_mesh_visibility == nullptr ? " (WARN SetMeshVisibility unbound: not applied)" : "");
}

} // namespace

// Card 297, each tick on the client: the local PC's pawn against the mech it piloted at the last step. Every path into
// or out of the client's own mech (mount, join or reconnect while piloting, respawn into a mech, a load) passes here,
// so the swap does not depend on which replicated chain ran. A mech destroyed under the pilot is gone: no exit swap.
void c297_tick(void* engine) {
    void* const world = engine != nullptr ? g_get_game_world(engine) : nullptr;
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    void* const mech = pawn != nullptr && is_a(pawn, g_mech_class()) ? pawn : nullptr;
    void* const was = g_c297.pilot.serial != 0 ? c31_weak_get(g_c297.pilot) : nullptr;
    const ULONGLONG now = GetTickCount64();
    if (mech != was) {
        if (was != nullptr) {
            c297_swap(was, false, "left");
        }
        if (mech != nullptr) {
            c297_swap(mech, true, "possessed");
        }
        c31_weak_set(g_c297.pilot, mech);
        c31_weak_set(g_c297.mech, mech); // the tag watch follows every path in, not only the mount press
        g_c297.tag_n = mech != nullptr ? c297_read_tags(read_ptr(mech, kCharacterAsc), g_c297.tags) : 0;
        g_c297.check_ms = now + kC297CheckMs;
    } else if (mech != nullptr && now >= g_c297.check_ms) {
        g_c297.check_ms = now + kC297CheckMs;
        if (c297_bool_call(read_ptr(mech, kMechFirstPersonMesh), g_c297.visible_fname, L"IsVisible") == 0) {
            log::line("WARN card297: client pilots mech %llu, FirstPersonMesh hidden",
                      static_cast<unsigned long long>(c41_guid(mech)));
            c297_swap(mech, true, "1 s check");
        }
    }
    c297_tags_step();
}

// Card 297 candidate 2 (diagnostic, not fixed): a client shot from its own piloted mech, with the attack tag count the
// cockpit ABP reads at that moment.
void c297_shot(const void* mech) {
    if (!g_client_session || mech == nullptr || g_c297.pilot.serial == 0 || c31_weak_get(g_c297.pilot) != mech ||
        !note_event_cap(g_c297.shot_lines, kC297ShotLines, "card297 shot row")) {
        return;
    }
    log::line("card297: client t=%llu mech %llu shot: attack tag %s count %d",
              static_cast<unsigned long long>(GetTickCount64()), static_cast<unsigned long long>(c41_guid(mech)),
              kC297AttackTag, c297_attack_count(mech));
}

// Card 297 (diagnostic): a row per owned tag added to or removed from the piloted mech's ASC.
void c297_tags_step() {
    const void* const mech = g_c297.mech.serial != 0 ? c31_weak_get(g_c297.mech) : nullptr;
    if (mech == nullptr) {
        return;
    }
    uint64_t now_tags[kC297MaxTags];
    const int32_t n = c297_read_tags(read_ptr(mech, kCharacterAsc), now_tags);
    for (int32_t i = 0; i < n; ++i) {
        if (!tag_in(g_c297.tags, g_c297.tag_n, now_tags[i]) &&
            note_event_cap(g_c297.tag_lines, kC297TagLines, "card297 tag row")) {
            log::line("card297: %s t=%llu mech %s tag +%s (%d owned)", side(),
                      static_cast<unsigned long long>(GetTickCount64()), object_name(mech).text,
                      fname_text(&now_tags[i]).text, n);
        }
    }
    for (int32_t i = 0; i < g_c297.tag_n; ++i) {
        if (!tag_in(now_tags, n, g_c297.tags[i]) &&
            note_event_cap(g_c297.tag_lines, kC297TagLines, "card297 tag row")) {
            log::line("card297: %s t=%llu mech %s tag -%s (%d owned)", side(),
                      static_cast<unsigned long long>(GetTickCount64()), object_name(mech).text,
                      fname_text(&g_c297.tags[i]).text, n);
        }
    }
    memcpy(g_c297.tags, now_tags, static_cast<size_t>(n) * sizeof(uint64_t));
    g_c297.tag_n = n;
}

void c297_map_end() {
    g_c297 = C297State{};
}

} // namespace ubw::hooks
