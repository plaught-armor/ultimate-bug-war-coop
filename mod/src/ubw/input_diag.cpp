// Input diagnostics: the client input receiver and input diagnostics (cards 008-009), the look and movement
// rows (card 010), mapping contexts and host server-move rows (card 011), the client's own local moves
// (card 012), the movement chain and collision writers (card 013), IA_Look / IA_Move evaluation, and the
// action-level modifiers, triggers and jump/sprint call chain (card 016). Log only, except the card 008-009
// input receiver and game-only fallback.

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

bool c65_refuse_client_try(const void* asc, int32_t handle, const void* event_data);
uint64_t c41_guid(const void* actor);       // enemies.cpp
bool c39_mesh_simulating(const void* mesh); // enemies.cpp
bool c38_is_marked(const void* actor);       // enemies.cpp
// Card 011: defined with the card 011 diagnostics below.
void log_context_mappings(const void* context);

SetPawnFn g_orig_pc_set_pawn = nullptr;
ControllerFn g_orig_on_rep_pawn = nullptr;
SetPawnFn g_orig_acknowledge_possession = nullptr;
SetPawnFn g_orig_client_restart = nullptr;
SetInputModeFn g_orig_set_input_mode = nullptr;
SetBoolFn g_orig_set_show_mouse_cursor = nullptr;
SetBoolFn g_orig_set_ignore_move_input = nullptr;
AddMappingContextFn g_orig_add_mapping_context = nullptr;
StaticClassFn g_input_receiver_class = nullptr;          // USTInputReceiverInterface
StaticClassFn g_player_character_class = nullptr;        // ASTPlayerCharacter
StaticClassFn g_character_class = nullptr;               // ACharacter (card 011)
StaticClassFn g_player_controller_class = nullptr;       // APlayerController (card 017)
StaticClassFn g_st_pc_class = nullptr;                   // ASTPlayerController
ControllerFn g_release_active_inputs = nullptr;          // ASTPlayerController::ReleaseActiveInputs

SetOriginActorFn g_set_minimap_origin = nullptr;         // USTMinimapComponent::SetOriginActor (card 058)
WorldGetFn g_world_first_pc = nullptr;                   // UWorld::GetFirstPlayerController
PcClientLoadedFn g_pc_client_loaded = nullptr;           // APlayerController::HasClientLoadedCurrentWorld (card 348)

ApplyUIInputConfigFn g_orig_apply_ui_input_config = nullptr;
bool g_client_game_only = false; // UBWCOOP_CLIENT_GAMEONLY; written in ubw::init before any hook
bool g_game_only_pending = false; // game thread only: armed per client LoadMap, cleared when used

constexpr size_t kLocalPlayerViewport = 0x78;   // ULocalPlayer::ViewportClient (ApplyUIInputConfig+0xF6)
constexpr size_t kRouterActiveConfig = 0x31;    // TOptional<FUIInputConfig> value (ApplyUIInputConfig+0x36)
constexpr size_t kRouterActiveConfigSet = 0x37; // its IsSet byte (ApplyUIInputConfig+0x7D / +0xE6)
constexpr size_t kUIInputConfigBytes = 6;

bool g_client_session = false;      // game thread only: the last LoadMap joined a server
ULONGLONG g_snapshot_start_ms = 0;  // game thread only
uint32_t g_receiver_logs = 0;       // game thread only, like every counter below
uint32_t g_possession_logs = 0;
uint32_t g_input_mode_logs = 0;
uint32_t g_cursor_logs = 0;
uint32_t g_ignore_move_logs = 0;
uint32_t g_context_logs = 0;
uint32_t g_ui_config_logs = 0;

bool is_client_st_pc(const void* controller) {
    return is_net_client(controller) && is_a(controller, g_st_pc_class());
}

// Card 058: OnPossess (server only) also sets the PC minimap's OriginActor to the pawn. A client PC
// kept it null, so its minimap stayed blank, and the first ShowOnMinimap (FED_C1M1's assault course
// markers) read null+0x1B8 in GetMinimapLocation and crashed the client. Same call, same pawn.
// Card 319 (b251): the PC owns a second USTMinimapComponent (the bug proximity area widget's, widget+0x358), and
// USTMinimapComponent::TickComponent returns at once while its OriginActor is null, so its markers and the OnNewMarker
// broadcast never ran on the client (c492b250a: 0 calls over 46 shots). Every minimap component whose Outer is this
// PC gets the pawn, one row each.
constexpr int32_t kMinimapScan = 64; // USTMinimapComponents in the client's world (its PC owns 2)

void sync_client_minimap_origin(uint8_t* pc, void* pawn) {
    if (pawn == nullptr) {
        return;
    }
    const void* comps[kMinimapScan] = {};
    const int32_t n = g_minimap_class != nullptr ? objects_of_class(g_minimap_class(), comps, kMinimapScan, true) : 0;
    int32_t set = 0;
    for (int32_t i = 0; i < n && i < kMinimapScan; ++i) {
        void* const minimap = const_cast<void*>(comps[i]);
        if (read_ptr(minimap, kObjectOuter) != pc) {
            continue;
        }
        void* const old_origin = read_ptr(minimap, kMinimapOrigin);
        g_set_minimap_origin(minimap, pawn);
        ++set;
        log::line("card058: client minimap %p%s OriginActor %p -> %p (%s)", minimap,
                  minimap == read_ptr(pc, kPcMinimap) ? " (pc+0x7B0)" : "", old_origin,
                  read_ptr(minimap, kMinimapOrigin), class_name(pawn).text);
    }
    if (set == 0) {
        log::line("WARN card058: client PC %p owns no minimap component (%d scanned, class %s)", pc, n,
                  g_minimap_class != nullptr ? "found" : "missing");
    }
}

// The fix: what ASTPlayerController::OnPossess stores, for the pawn SetPawn just stored.
void sync_client_input_receiver(uint8_t* pc) {
    void* const pawn = read_ptr(pc, kControllerPawn);
    void* const receiver = pawn != nullptr ? g_get_interface_address(pawn, g_input_receiver_class()) : nullptr;
    void** const receiver_slot = reinterpret_cast<void**>(pc + kPcInputReceiver);
    void** const character_slot = reinterpret_cast<void**>(pc + kPcPlayerCharacter);
    void* const old_receiver = *receiver_slot;
    void* const old_character = *character_slot;
    if (old_receiver != nullptr && old_receiver != receiver) {
        g_release_active_inputs(pc); // reads the old receiver; OnUnPossess calls it the same way
    }
    *receiver_slot = receiver;
    if (is_a(pawn, g_player_character_class())) {
        *character_slot = pawn;
    }
    sync_client_minimap_origin(pc, pawn);
    if (note_event(g_receiver_logs, "client-input receiver")) {
        log::line("client-input: SetPawn(pc=%p, pawn=%p %s): input receiver %p -> %p, player character %p -> %p",
                  static_cast<void*>(pc), pawn, class_name(pawn).text, old_receiver, receiver, old_character,
                  *character_slot);
    }
}

void detour_pc_set_pawn(void* pc, void* pawn) {
    g_last_hook = "SetPawn";
    g_orig_pc_set_pawn(pc, pawn);
    if (is_client_st_pc(pc) && g_get_local_player(pc) != nullptr) {
        c303_possession("SetPawn", pawn); // card 303
        sync_client_input_receiver(static_cast<uint8_t*>(pc));
    }
}

void log_possession(const char* what, const void* controller, const void* arg) {
    if (is_client_st_pc(controller)) {
        c303_possession(what, arg != nullptr ? arg : read_ptr(controller, kControllerPawn)); // card 303, uncapped
    }
    if (!is_client_st_pc(controller) || !note_event(g_possession_logs, "possession")) {
        return;
    }
    const auto* const c = static_cast<const uint8_t*>(controller);
    const auto* const pawn = static_cast<const uint8_t*>(read_ptr(c, kControllerPawn));
    log::line("possess: %s(pc=%p, arg=%p) local player %p, Pawn %p %s, input receiver %p, ignore move %u look %u, "
              "pawn bActorEnableCollision %d",
              what, controller, arg, g_get_local_player(controller), static_cast<const void*>(pawn),
              class_name(pawn).text, read_ptr(c, kPcInputReceiver), c[kControllerIgnoreMove], c[kControllerIgnoreLook],
              pawn != nullptr ? pawn[kActorCollisionFlags] & kActorEnableCollisionBit : -1);
}

void detour_on_rep_pawn(void* controller) {
    g_last_hook = "OnRep_Pawn";
    g_orig_on_rep_pawn(controller);
    log_possession("OnRep_Pawn", controller, nullptr);
}

void detour_acknowledge_possession(void* pc, void* pawn) {
    g_last_hook = "AcknowledgePossession";
    g_orig_acknowledge_possession(pc, pawn);
    log_possession("AcknowledgePossession", pc, pawn);
    if (g_game_only_pending && pawn != nullptr && is_client_st_pc(pc) && is_local_pc(pc)) {
        g_game_only_pending = false;
        client_force_game_only(pc);
        c156_client_up(pc, "map load", true, GetTickCount64()); // card 156: IMC_DummyUI_KBM outlives the map
    }
    // Only in a networked world (listen server or client): a standalone front end gets no keys.
    // Card 019: the host sprint script is armed by PossessedBy of the landed character instead.
    if (autoinput::enabled() && !autoinput::host_script() && pawn != nullptr && is_local_pc(pc) &&
        net_mode(pc) != kNetModeStandalone) {
        // Once per map; the script starts after the configured delay. Card 020: the client logs its own
        // pawn's rows from here (the host's rows start with the host script in PossessedBy).
        if (autoinput::arm(is_net_client(pc))) {
            start_card020_rows();
        }
    }
}

void client_restart_call(void* pc, void* pawn) {
    g_last_hook = "ClientRestart";
    g_orig_client_restart(pc, pawn);
    log_possession("ClientRestart", pc, pawn);
}

// Card 226 follow-up test lever: UBWCOOP_C135_DROPRESTART=1 on a client drops the engine's ClientRestart calls for
// the own pawn while the card 156 map-load restore is pending, which is what the possession race leaves behind (the
// pawn set, never acknowledged). The card 135 guard (c135_restart_step) calls client_restart_call and is not dropped.
bool c135_drop_restart(const void* pc, const void* pawn) {
    static int32_t on = -1;
    if (on < 0) {
        wchar_t buf[4];
        on = GetEnvironmentVariableW(L"UBWCOOP_C135_DROPRESTART", buf, 4) == 1 && buf[0] == L'1' ? 1 : 0;
    }
    if (on != 1 || !g_game_only_pending || pawn == nullptr || !is_client_st_pc(pc) || !is_local_pc(pc) ||
        text_contains(class_name(pawn).text, "Frontend")) {
        return false;
    }
    log::line("card135: client t=%llu engine ClientRestart for %s dropped (UBWCOOP_C135_DROPRESTART=1)",
              static_cast<unsigned long long>(GetTickCount64()), class_name(pawn).text);
    return true;
}

void detour_client_restart(void* pc, void* pawn) {
    if (c135_drop_restart(pc, pawn)) {
        return;
    }
    client_restart_call(pc, pawn);
}

const char* input_mode_name(const void* mode) {
    const void* const vtable = read_ptr(mode, 0);
    for (const InputModeName& m : kInputModes) {
        if (vtable == g_exe_base + m.vtable_rva) {
            return m.name;
        }
    }
    return "other";
}


void detour_set_input_mode(void* pc, const void* mode) {
    const unsigned long long caller = exe_rva(_ReturnAddress());
    g_orig_set_input_mode(pc, mode);
    c31_note_mode(pc, mode);
    if (is_local_pc(pc) && note_event(g_input_mode_logs, "SetInputMode")) {
        log::line("input-mode: SetInputMode(pc=%p, net mode %u, %s, vtable exe+0x%llX) caller exe+0x%llX, "
                  "bShowMouseCursor %u",
                  pc, net_mode(pc), input_mode_name(mode), exe_rva(read_ptr(mode, 0)), caller, cursor_bit(pc));
    }
}

void detour_set_show_mouse_cursor(void* pc, bool show) {
    const unsigned long long caller = exe_rva(_ReturnAddress());
    const unsigned before = cursor_bit(pc);
    g_orig_set_show_mouse_cursor(pc, show);
    if (is_local_pc(pc) && note_event(g_cursor_logs, "SetShowMouseCursor")) {
        log::line("input-mode: SetShowMouseCursor(pc=%p, net mode %u, %d) bShowMouseCursor %u -> %u, caller exe+0x%llX",
                  pc, net_mode(pc), show ? 1 : 0, before, cursor_bit(pc), caller);
    }
}

void detour_set_ignore_move_input(void* controller, bool ignore) {
    const unsigned long long caller = exe_rva(_ReturnAddress());
    const auto* const c = static_cast<const uint8_t*>(controller);
    const unsigned before = c[kControllerIgnoreMove];
    g_orig_set_ignore_move_input(controller, ignore);
    if (is_net_client(controller) && note_event(g_ignore_move_logs, "SetIgnoreMoveInput")) {
        log::line("input-mode: SetIgnoreMoveInput(controller=%p %s, %d) IgnoreMoveInput %u -> %u, caller exe+0x%llX",
                  controller, class_name(controller).text, ignore ? 1 : 0, before, c[kControllerIgnoreMove], caller);
    }
}

void detour_add_mapping_context(void* iface, const void* context, int32_t priority, const uint8_t* options) {
    const unsigned long long caller = exe_rva(_ReturnAddress());
    g_orig_add_mapping_context(iface, context, priority, options);
    c44_context(context, priority);
    // Card 011: logged on the host too, with the context's mouse/look mappings.
    if (note_event(g_context_logs, "AddMappingContext")) {
        log::line("input-mode: %s AddMappingContext(iface=%p, context=%p %s, priority %d) caller exe+0x%llX%s",
                  session_side(), iface, context, object_name(context).text, priority, caller,
                  caller == 0 ? " (not the exe: this mod)" : "");
        log_context_mappings(context);
    }
}

// Viewport capture/lock of the router's local player, or 0xFF/0xFF without a viewport client.
struct CaptureLock {
    unsigned capture;
    unsigned lock;
};

CaptureLock router_viewport_modes(const void* router) {
    const void* const local_player = read_ptr(router, kObjectOuter);
    const auto* const viewport =
        local_player != nullptr ? static_cast<const uint8_t*>(read_ptr(local_player, kLocalPlayerViewport)) : nullptr;
    if (viewport == nullptr) {
        return {0xFFu, 0xFFu};
    }
    return {viewport[kViewportCaptureMode], viewport[kViewportLockMode]};
}

// Log only. Logs every call (CommonUI routers exist only for local players), capped.
void detour_apply_ui_input_config(void* router, const uint8_t* config, bool force_refresh) {
    const unsigned long long caller = exe_rva(_ReturnAddress());
    const auto* const r = static_cast<const uint8_t*>(router);
    uint8_t old_config[kUIInputConfigBytes];
    memcpy(old_config, r + kRouterActiveConfig, kUIInputConfigBytes);
    const unsigned old_set = r[kRouterActiveConfigSet];
    const CaptureLock before = router_viewport_modes(router);
    g_orig_apply_ui_input_config(router, config, force_refresh); // config is the caller's; not kept
    if (!note_event(g_ui_config_logs, "ApplyUIInputConfig")) {
        return;
    }
    const CaptureLock after = router_viewport_modes(router);
    log::line("input-mode: ApplyUIInputConfig(router=%p, local player %p, force %d) caller exe+0x%llX", router,
              read_ptr(router, kObjectOuter), force_refresh ? 1 : 0, caller);
    log::line("input-mode:   config ignoreMove %u ignoreLook %u mode %u capture %u lock %u hideCursor %u; "
              "previous (set %u) %u %u %u %u %u %u; viewport capture %u -> %u, lock %u -> %u",
              config[0], config[1], config[2], config[3], config[4], config[5], old_set, old_config[0], old_config[1],
              old_config[2], old_config[3], old_config[4], old_config[5], before.capture, after.capture, before.lock,
              after.lock);
}

HandleInputLookFn g_orig_handle_input_look = nullptr;
InputAxisFn g_orig_input_axis = nullptr;
SetControlRotationFn g_orig_set_control_rotation = nullptr;
ClientSetRotationFn g_orig_client_set_rotation = nullptr;
ClientAdjustPositionFn g_orig_client_adjust_position = nullptr;
ReplicateMoveFn g_orig_replicate_move = nullptr;

constexpr size_t kPcRotationInput = 0x528;           // FRotator RotationInput (AddYawInput adds at +0x530)

// UInputMappingContext::Mappings: TArray at +0x30 (MapKey+0x64), element 0x50 bytes (MapKey+0x12D,
// `lea rcx,[rax+4*rax]; shl rcx,4`). FEnhancedActionKeyMapping: Triggers TArray +0x0, Modifiers
// TArray +0x10, Action +0x20, Key (FName first) +0x28 (FEnhancedActionKeyMapping ctor 0x044637A0).
constexpr size_t kMappingTriggersNum = 0x8;
constexpr size_t kMappingModifiersNum = 0x18;
constexpr size_t kMappingAction = 0x20;
constexpr uint32_t kMaxMappingLines = 64;
// APlayerController::UpdateRotation+0x1A6: `call qword ptr [rax+0x798]` (SetControlRotation); checked at install.
constexpr uint32_t kUpdateRotationSetControlRotationRet = 0x0406888C;
constexpr ULONGLONG kLookLogIntervalMs = 1000;
constexpr uint32_t kMaxLookLines = 60;

DiagCounts g_counts{};

struct LookLog {
    uint32_t since_line;
    uint32_t lines;
    ULONGLONG last_ms;
    double x;
    double y;
};
LookLog g_look{};

ClientMove g_client_move{};

const void* g_update_rotation_set_rot_ret = nullptr; // set in ubw::init; null if the site check failed
uint32_t g_set_rotation_logs = 0;
uint32_t g_client_set_rotation_logs = 0;
uint32_t g_adjust_logs = 0;
uint32_t g_rot_row = 0;
ULONGLONG g_rot_next_ms = 0;

// Checks the UpdateRotation call site so per-frame SetControlRotation calls can be told apart.
// Diagnostic only: on a mismatch every call is counted as "other" and logged (capped).
void resolve_update_rotation_site() {
    static constexpr uint8_t kCall[] = {0xFF, 0x90, 0x98, 0x07, 0x00, 0x00}; // call qword ptr [rax+0x798]
    const uint8_t* const ret = g_exe_base + kUpdateRotationSetControlRotationRet;
    if (memcmp(ret - sizeof(kCall), kCall, sizeof(kCall)) != 0) {
        log::line("ERROR hooks: APlayerController::UpdateRotation+0x1A6 is not `call [rax+0x798]`; "
                  "SetControlRotation callers will not be classified");
        return;
    }
    g_update_rotation_set_rot_ret = ret;
}

// At most one line per kLookLogIntervalMs, kMaxLookLines lines per process, then one notice.
void look_log_step(const uint8_t* pc) {
    const ULONGLONG now = GetTickCount64();
    if (g_look.lines > kMaxLookLines || now - g_look.last_ms < kLookLogIntervalMs) {
        return;
    }
    g_look.last_ms = now;
    ++g_look.lines;
    if (g_look.lines > kMaxLookLines) {
        log::line("look: %s HandleInputLook ... further lines not logged", session_side());
        return;
    }
    const Vec3 input = read_vec3(pc, kPcRotationInput);
    log::line("look: %s HandleInputLook(pc=%p) x%u since last line, last value X %.4f Y %.4f; RotationInput after "
              "P %.4f Y %.4f R %.4f, IgnoreLookInput %u",
              session_side(), static_cast<const void*>(pc), g_look.since_line, g_look.x, g_look.y, input.x, input.y, input.z,
              pc[kControllerIgnoreLook]);
    g_look.since_line = 0;
}

// Card 011: counted and logged on the host too (the control for the client's count); the host
// never runs the client rows, so g_counts there is only reset by the next client LoadMap.
void detour_handle_input_look(void* pc, const void* value) {
    g_orig_handle_input_look(pc, value);
    ++g_counts.look;
    ++g_look.since_line;
    memcpy(&g_look.x, value, sizeof(double)); // FInputActionValue: X double at +0, Y at +8
    memcpy(&g_look.y, static_cast<const uint8_t*>(value) + sizeof(double), sizeof(double));
    look_log_step(static_cast<const uint8_t*>(pc));
}

bool detour_input_axis(void* viewport_client, const void* args) {
    if (g_client_session) {
        ++g_counts.axis;
    }
    return g_orig_input_axis(viewport_client, args);
}

void detour_set_control_rotation(void* controller, const double* rotation) {
    const void* const ret = _ReturnAddress();
    if (!g_client_session || !is_a(controller, g_st_pc_class())) {
        g_orig_set_control_rotation(controller, rotation);
        return;
    }
    const Vec3 before = read_vec3(controller, kControllerControlRotation);
    g_orig_set_control_rotation(controller, rotation);
    if (ret == g_update_rotation_set_rot_ret) {
        ++g_counts.set_rotation_frame;
        return;
    }
    ++g_counts.set_rotation_other;
    if (note_event(g_set_rotation_logs, "SetControlRotation")) {
        const Vec3 after = read_vec3(controller, kControllerControlRotation);
        log::line("look: SetControlRotation(pc=%p) caller exe+0x%llX: P %.2f Y %.2f R %.2f -> P %.2f Y %.2f R %.2f",
                  controller, exe_rva(ret), before.x, before.y, before.z, after.x, after.y, after.z);
    }
}

// FRotator by value arrives as a pointer to the caller's copy; it is read before the original runs.
void detour_client_set_rotation(void* controller, const double* rotation, bool reset_camera) {
    const unsigned long long caller = exe_rva(_ReturnAddress());
    if (g_client_session && note_event(g_client_set_rotation_logs, "ClientSetRotation")) {
        const Vec3 r = read_vec3(rotation, 0);
        log::line("look: ClientSetRotation_Implementation(controller=%p %s, P %.2f Y %.2f R %.2f, reset camera %d) "
                  "caller exe+0x%llX",
                  controller, class_name(controller).text, r.x, r.y, r.z, reset_camera ? 1 : 0, caller);
    }
    g_orig_client_set_rotation(controller, rotation, reset_camera);
}

MoveDiag g_move_diag{};

void note_client_adjust(const void* cmc, float timestamp, const double* new_loc, bool base_relative, uint8_t mode) {
    ++g_counts.adjust;
    g_move_diag.adjust_seen = true;
    if (!note_event(g_adjust_logs, "ClientAdjustPosition")) {
        return;
    }
    const Vec3 server = read_vec3(new_loc, 0);
    const void* const owner = read_ptr(cmc, kCmcCharacterOwner);
    const void* const root = owner != nullptr ? read_ptr(owner, kActorRootComponent) : nullptr;
    const Vec3 here = root != nullptr ? read_vec3(root, kComponentTranslation) : Vec3{};
    const double dx = server.x - here.x;
    const double dy = server.y - here.y;
    const double dz = server.z - here.z;
    log::line("move: ClientAdjustPosition(cmc=%p, owner %p, timestamp %.4f, mode %u, base relative %d) server "
              "(%.1f, %.1f, %.1f), client (%.1f, %.1f, %.1f), distance %.1f",
              cmc, owner, static_cast<double>(timestamp), mode, base_relative ? 1 : 0, server.x, server.y, server.z,
              here.x, here.y, here.z, sqrt(dx * dx + dy * dy + dz * dz));
}

// The by-value FVectors arrive as pointers to the caller's copies and are passed through as is.
void detour_client_adjust_position(void* cmc, float timestamp, const double* new_loc, const double* new_vel,
                                   void* new_base, uint64_t base_bone, bool has_base, bool base_relative,
                                   uint8_t movement_mode, const void* new_rotation) {
    if (g_client_session) {
        note_client_adjust(cmc, timestamp, new_loc, base_relative, movement_mode);
        c295_note_adjust(cmc, new_loc); // card 295
    }
    g_orig_client_adjust_position(cmc, timestamp, new_loc, new_vel, new_base, base_bone, has_base, base_relative,
                                  movement_mode, new_rotation);
}

void detour_replicate_move(void* cmc, float delta_seconds, const double* acceleration) {
    if (g_client_session) {
        ++g_counts.replicate_move;
        g_client_move.accel = read_vec3(acceleration, 0); // a copy; the caller's FVector is not kept
        g_client_move.delta = delta_seconds;
    }
    g_orig_replicate_move(cmc, delta_seconds, acceleration);
}

PerformMovementFn g_orig_perform_movement = nullptr;
PlayerInputKeyFn g_orig_player_input_key = nullptr;
HandleInputLookFn g_orig_handle_input_move = nullptr; // same signature as HandleInputLook

constexpr size_t kValueBytes = 0x20;                  // FInputActionValue: FVector Value, then ValueType at +0x18

PhysicsStepFn g_orig_start_new_physics = nullptr;
PhysicsStepFn g_orig_phys_walking = nullptr;
MoveComponentFn g_orig_safe_move = nullptr;
MoveComponentFn g_orig_move_impl = nullptr;
SetCollisionFn g_orig_set_actor_collision = nullptr;
ExecThunkFn g_orig_exec_set_actor_collision = nullptr;
SetCollisionEnabledFn g_orig_set_collision_enabled = nullptr;
SetCollisionProfileFn g_orig_set_collision_profile = nullptr; // card 237
DropshipFn g_orig_enter_dropship = nullptr;
DropshipFn g_orig_descend_completed = nullptr;
ProcessMappingFn g_orig_process_mapping = nullptr;
ApplyModifiersFn g_orig_apply_modifiers = nullptr;
ModifyRawFn g_orig_modify_raw = nullptr;
EvaluateTriggersFn g_orig_evaluate_triggers = nullptr;

PawnFn g_orig_post_init = nullptr;
PossessedByFn g_orig_possessed_by = nullptr;
BindingExecuteFn g_orig_execute_value = nullptr;
BindingExecuteFn g_orig_execute_void = nullptr;
BindingExecuteFn g_orig_execute_dynamic = nullptr;
autoinput::FKeyCtorFn g_fkey_ctor = nullptr;
autoinput::KeyArgsCtorFn g_key_args_ctor = nullptr;
autoinput::RequestExitFn g_request_exit = nullptr;

MoveChain g_chain{};
const void* g_move_cmc = nullptr; // the watched CMC, only during its PerformMovement call

void reset_move_chain_row() {
    const MoveChain keep = g_chain;
    g_chain = MoveChain{};
    g_chain.actor_collision = keep.actor_collision;
    g_chain.body_collision = keep.body_collision;
    g_chain.floor_flags = keep.floor_flags;
    g_chain.mode = keep.mode;
    g_chain.controller = keep.controller;
    g_chain.snap_pending = true;
}

void detour_start_new_physics(void* cmc, float delta_seconds, int32_t iterations) {
    if (cmc == g_move_cmc) {
        MoveChain& c = g_chain;
        c.dt_min = (c.new_physics == 0 || delta_seconds < c.dt_min) ? delta_seconds : c.dt_min;
        c.dt_max = (c.new_physics == 0 || delta_seconds > c.dt_max) ? delta_seconds : c.dt_max;
        ++c.new_physics;
    }
    g_orig_start_new_physics(cmc, delta_seconds, iterations);
}

void detour_phys_walking(void* cmc, float delta_seconds, int32_t iterations) {
    g_chain.walking += cmc == g_move_cmc ? 1u : 0u;
    g_orig_phys_walking(cmc, delta_seconds, iterations);
}

void note_safe_move_hit(const uint8_t* hit) {
    const uint8_t flags = hit[kHitFlags];
    g_chain.penetrating += (flags & kHitStartPenetrating) != 0 ? 1u : 0u;
    if ((flags & kHitBlocking) == 0) {
        return;
    }
    ++g_chain.blocking;
    const void* const component = g_weak_get(hit + kHitComponent);
    g_chain.hit_component = object_name(component);
    g_chain.hit_owner = object_name(component != nullptr ? read_ptr(component, kComponentOwner) : nullptr);
}

bool detour_safe_move(void* mc, const double* delta, const void* rotation, bool sweep, void* hit, uint8_t teleport) {
    const bool moved = g_orig_safe_move(mc, delta, rotation, sweep, hit, teleport);
    if (mc != g_move_cmc) {
        return moved;
    }
    const double len = vec_len(read_vec3(delta, 0));
    ++g_chain.safe_moves;
    g_chain.safe_false += moved ? 0u : 1u;
    g_chain.delta_sum += len;
    g_chain.delta_max = len > g_chain.delta_max ? len : g_chain.delta_max;
    note_safe_move_hit(static_cast<const uint8_t*>(hit)); // OutHit is a reference: never null
    return moved;
}

bool detour_move_impl(void* mc, const double* delta, const void* rotation, bool sweep, void* hit, uint8_t teleport) {
    const bool moved = g_orig_move_impl(mc, delta, rotation, sweep, hit, teleport);
    if (mc == g_move_cmc) {
        ++g_chain.moves;
        g_chain.move_false += moved ? 0u : 1u;
    }
    return moved;
}

// ECollisionEnabled: 1 QueryOnly, 3 QueryAndPhysics, 5 QueryAndProbe are the values PhysWalking
// accepts (PhysWalking+0xD81 returns otherwise, Velocity untouched).
bool collision_has_query(uint8_t type) {
    return type == 1 || type == 3 || type == 5;
}

void log_move_chain(const char* tag, uint32_t k) {
    const MoveChain& c = g_chain;
    const uint8_t effective = c.actor_collision != 0 ? c.body_collision : 0;
    log::line("%s[%u]: chain: StartNewPhysics %u (dt min %.4f max %.4f), PhysWalking %u, SafeMoveUpdatedComponent %u "
              "(false %u, blocking %u, start penetrating %u, requested |delta| sum %.2f max %.2f), "
              "MoveUpdatedComponentImpl %u (false %u)",
              tag, k, c.new_physics, static_cast<double>(c.dt_min), static_cast<double>(c.dt_max), c.walking,
              c.safe_moves, c.safe_false, c.blocking, c.penetrating, c.delta_sum, c.delta_max, c.moves, c.move_false);
    log::line("%s[%u]: chain: last PerformMovement entry: bActorEnableCollision %u, capsule CollisionEnabled %u, "
              "effective %u (query %d), CharacterOwner->Controller %d, MovementMode %u, CurrentFloor flags 0x%X; "
              "last blocking hit %s owner %s",
              tag, k, c.actor_collision, c.body_collision, effective, collision_has_query(effective) ? 1 : 0,
              c.controller, c.mode, c.floor_flags, c.hit_component.text, c.hit_owner.text);
    if (c.snap_done) {
        log::line("%s[%u]: chain: one PerformMovement: Velocity (%.1f, %.1f, %.1f) -> (%.1f, %.1f, %.1f), "
                  "Acceleration (%.1f, %.1f, %.1f) -> (%.1f, %.1f, %.1f)",
                  tag, k, c.v_before.x, c.v_before.y, c.v_before.z, c.v_after.x, c.v_after.y, c.v_after.z,
                  c.a_before.x, c.a_before.y, c.a_before.z, c.a_after.x, c.a_after.y, c.a_after.z);
    }
}

// --- Collision writers (cold paths; capped per map) ---
// Card 014: characters have their own cap, so other actors (card 013: BP_AISpawnPoint_C) cannot use it up.
uint32_t g_collision_logs = 0;
uint32_t g_pawn_collision_logs = 0;
const void* g_bp_node = nullptr; // the UFunction running execSetActorEnableCollision, during that call only

void detour_set_actor_collision(void* actor, bool enable) {
    const auto* const a = static_cast<const uint8_t*>(actor);
    const unsigned before = a[kActorCollisionFlags] & kActorEnableCollisionBit;
    const unsigned long long caller = exe_rva(_ReturnAddress());
    const void* const node = g_bp_node;
    const bool character = is_a(actor, g_character_class());
    if (character) {
        g_last_hook = "SetActorEnableCollision";
    }
    g_orig_set_actor_collision(actor, enable);
    c95_note_collision("SetActorEnableCollision", actor, nullptr, before, a[kActorCollisionFlags] & kActorEnableCollisionBit,
                       caller, node);
    c95_relay_collision(actor, nullptr, before, a[kActorCollisionFlags] & kActorEnableCollisionBit, caller);
    if (before != (a[kActorCollisionFlags] & kActorEnableCollisionBit)) {
        c98_note(C98Kind::Collision, actor, nullptr, caller); // card 098 measurement
        c137_client_change(actor, "actor collision", before != 0 ? 1 : 0,
                           (a[kActorCollisionFlags] & kActorEnableCollisionBit) != 0 ? 1 : 0, caller); // card 137 2a
    }
    if (!(character ? note_event(g_pawn_collision_logs, "character collision write")
                    : note_event(g_collision_logs, "collision write"))) {
        return;
    }
    const void* const node_outer = node != nullptr ? read_ptr(node, kObjectOuter) : nullptr;
    log::line("collision: %s SetActorEnableCollision(%p %s, LocalRole %u RemoteRole %u, %d) %u -> %u, caller exe+0x%llX, "
              "blueprint %s.%s",
              side_text(), actor, class_name(actor).text, a[kActorLocalRole], a[kActorRemoteRole], enable ? 1 : 0, before,
              a[kActorCollisionFlags] & kActorEnableCollisionBit, caller, object_name(node_outer).text,
              object_name(node).text);
}

void detour_exec_set_actor_collision(void* context, void* frame, void* result) {
    const void* const outer = g_bp_node;
    g_bp_node = read_ptr(frame, kFrameNode);
    g_orig_exec_set_actor_collision(context, frame, result);
    g_bp_node = outer;
}

// Card 132: USTSkeletalAnimatorComponent's mesh show/hide (exe+0x5103000) ends with
// Mesh->SetCollisionEnabled(bShow), so showing the skeletal mesh again always writes QueryOnly (1). When an NPC dies
// while it shows the static impostor, the swap back completes after the ragdoll start has set the Ragdoll profile's
// QueryAndPhysics (3), and that write takes the physics collision from simulating bodies: the corpse falls through the
// floor at its damped terminal speed (~480 uu/s; hb44 guids 0x25c, 0x264, 0x26c to z -89000). A downgrade from this
// call site on a mesh whose bodies simulate is skipped, so the ragdoll keeps colliding. So is one on a client NPC
// whose death edge has run (c38_is_marked) before its bodies simulate: hb47 guid 0x408 died at 205124105, had the
// Ragdoll profile's 3, and this call wrote 1 at 205124272 with simulating 0, so the host's pose was never applied
// (its gate wants physics collision) and the corpse stayed where the client's death left it.
constexpr unsigned long long kAnimatorShowCollisionRet = 0x5103075; // return address of the vcall in exe+0x5103000
constexpr uint32_t kRagdollKeepLines = 16;
uint32_t g_ragdoll_keep_lines = 0; // per process; game thread

bool c132_keep_ragdoll_collision(const void* component, unsigned before, uint8_t type, unsigned long long caller) {
    const void* const owner = read_ptr(component, kComponentOwner);
    const bool dead = g_client_session && owner != nullptr && c38_is_marked(owner); // the caller writes the Mesh
    if (caller != kAnimatorShowCollisionRet || type >= before || (!c39_mesh_simulating(component) && !dead)) {
        return false;
    }
    if (note_event_cap(g_ragdoll_keep_lines, kRagdollKeepLines, "card132 ragdoll collision kept")) {
        log::line("card132: %s kept ragdoll collision %u (not %u) on %s guid 0x%llx: the animator's skeletal-mesh "
                  "show would take physics from %s",
                  side_text(), before, type, class_name(owner).text,
                  static_cast<unsigned long long>(owner != nullptr ? c41_guid(owner) : 0),
                  c39_mesh_simulating(component) ? "simulating bodies" : "a dead copy before its ragdoll");
    }
    return true;
}

// Card 483: AADPickupBase::OnUpdatePickupStatus (exe+0x4C61870) writes its PlayerCollider (+0x300) collision from the
// virtual IsPickupEnabled (+0x760): setne dl, then the collider's vtable +0x768 (SetCollisionEnabled), return
// exe+0x4C618A1. The ASTCustomPickupBase override (0x051CCF30) ands bEnabled (+0x2A8) with two viewer-local states, the
// persistent active byte and IsSignificant, and ASTCustomPickupBase::OnSignificanceChanged (0x051D1C40) calls the
// status update through its vtable +0x768 for this machine's own camera. So each machine's collider followed its own
// view: on the client, BP_GrenadeAmmoPickup_Frag_C_0 had collision 0 from load until the client's significance turned
// it on 32 s into play, while the host held 1 (c448b206a), and on the host a pickup far from the host's camera had no
// collider for a client player's server copy to overlap. The collider follows bEnabled alone, on both sides: bEnabled
// is the host's state (card 145 E rows, card 221 set back), and the visuals keep the view's answer.
static constexpr unsigned long long kPickupStatusCollisionRet = 0x4C618A1;
static constexpr uint32_t kC483Lines = 24;
static uint32_t g_c483_lines = 0; // per process; game thread
static uint32_t g_c483_kept = 0;

static uint8_t c483_pickup_collision(const void* component, uint8_t type, unsigned long long caller) {
    const auto* const pickup = static_cast<const uint8_t*>(read_ptr(component, kComponentOwner));
    if (caller != kPickupStatusCollisionRet || pickup == nullptr) {
        return type;
    }
    const uint8_t want = pickup[kPickupEnabledByte] != 0 ? 1u : 0u;
    if (want == type) {
        return type;
    }
    ++g_c483_kept;
    if (note_event_cap(g_c483_lines, kC483Lines, "card483 pickup collider")) {
        log::line("card483: %s t=%llu pickup %s collider collision %u (bEnabled), not the view's %u (%u so far)",
                  side_text(), static_cast<unsigned long long>(GetTickCount64()), object_name(pickup).text, want, type,
                  g_c483_kept);
    }
    return want;
}

// Logged only for a character's root component (the capsule): other components are not counted.
void detour_set_collision_enabled(void* component, uint8_t type_in) {
    const auto* const c = static_cast<const uint8_t*>(component);
    const unsigned before = c[kBodyCollisionEnabled];
    const uint8_t type = c483_pickup_collision(component, type_in, exe_rva(_ReturnAddress()));
    if (c132_keep_ragdoll_collision(component, before, type, exe_rva(_ReturnAddress()))) {
        return;
    }
    g_orig_set_collision_enabled(component, type);
    const void* const owner = read_ptr(component, kComponentOwner);
    c95_note_collision("SetCollisionEnabled", owner, component, before, c[kBodyCollisionEnabled],
                       exe_rva(_ReturnAddress()), nullptr);
    c95_relay_collision(owner, component, before, c[kBodyCollisionEnabled], exe_rva(_ReturnAddress()));
    if (before != c[kBodyCollisionEnabled]) {
        c98_note(C98Kind::Collision, owner, component, exe_rva(_ReturnAddress())); // card 098 measurement
        c137_client_change(owner, "component collision", static_cast<int32_t>(before),
                           static_cast<int32_t>(c[kBodyCollisionEnabled]), exe_rva(_ReturnAddress())); // card 137 2a
    }
    c132_corpse_collision(owner, component, before, c[kBodyCollisionEnabled], exe_rva(_ReturnAddress()));
    if (owner == nullptr || read_ptr(owner, kActorRootComponent) != component || !is_a(owner, g_character_class()) ||
        !note_event(g_pawn_collision_logs, "character collision write")) {
        return;
    }
    const auto* const o = static_cast<const uint8_t*>(owner);
    log::line("collision: %s SetCollisionEnabled(%p %s, owner %p %s LocalRole %u RemoteRole %u, %u) %u -> %u, caller "
              "exe+0x%llX",
              side_text(), component, class_name(component).text, owner, class_name(owner).text, o[kActorLocalRole],
              o[kActorRemoteRole], type, before, c[kBodyCollisionEnabled], exe_rva(_ReturnAddress()));
}

// Card 237: a collision profile write sets the body's CollisionEnabled and responses inside the body instance, not
// through SetCollisionEnabled, so the collision journal never saw it (the FED_C4M2 Brain Bug arena shield turns on by
// SetActorEnableCollision plus SetCollisionProfileName(BlockAll)). The UStaticMeshComponent override calls this base
// first, so every caller passes here. A profile change is journalled with the component's row and relayed.
void detour_set_collision_profile(void* component, uint64_t profile, bool update_overlaps) {
    const auto* const c = static_cast<const uint8_t*>(component);
    const unsigned before = c[kBodyCollisionEnabled];
    uint64_t profile_before = 0;
    if (g_collision_profile != nullptr) {
        g_collision_profile(component, &profile_before);
    }
    g_orig_set_collision_profile(component, profile, update_overlaps);
    uint64_t profile_after = 0;
    if (g_collision_profile != nullptr) {
        g_collision_profile(component, &profile_after);
    }
    const void* const owner = read_ptr(component, kComponentOwner);
    const unsigned long long caller = exe_rva(_ReturnAddress());
    c95_note_collision("SetCollisionProfileName", owner, component, before, c[kBodyCollisionEnabled], caller, nullptr);
    c95_relay_collision(owner, component, before, c[kBodyCollisionEnabled], caller,
                        profile_after != profile_before ? profile_after : 0);
    if (before != c[kBodyCollisionEnabled]) {
        c98_note(C98Kind::Collision, owner, component, caller); // card 098 measurement
        c137_client_change(owner, "component collision", static_cast<int32_t>(before),
                           static_cast<int32_t>(c[kBodyCollisionEnabled]), caller); // card 137 2a
    }
}

// Own cap: AI troopers spend the shared character-collision cap long before the player lands.
constexpr uint32_t kDropshipEnterExitLogs = 8;
uint32_t g_dropship_enter_exit_logs = 0; // per map (reset with g_pawn_collision_logs)

void log_dropship(const char* what, const uint8_t* dropship, unsigned before, const void* pc) {
    if (!note_event_cap(g_dropship_enter_exit_logs, kDropshipEnterExitLogs, "dropship enter/exit")) {
        return;
    }
    const auto* const pawn = pc != nullptr ? static_cast<const uint8_t*>(read_ptr(pc, kControllerPawn)) : nullptr;
    log::line("collision: %s %s(%p %s, LocalRole %u) player-inside flag %u -> %u; controller %p now possesses %p %s, "
              "bActorEnableCollision %u",
              side_text(), what, static_cast<const void*>(dropship), class_name(dropship).text, dropship[kActorLocalRole],
              before, dropship[kDropshipPlayerInside], pc, static_cast<const void*>(pawn), class_name(pawn).text,
              pawn != nullptr ? (pawn[kActorCollisionFlags] & kActorEnableCollisionBit) : 0u);
}

// Card 015: on a client the transport dropship is a simulated proxy whose BeginPlay
// (start-with-player) still calls EnterDropship: it attaches the client's own pawn, turns its
// collision off and zeroes its CMC, while the server spawned that pawn on foot (the server's
// corrections pull it there). Nothing ends that hold on the client: DescendToLandCompleted runs
// only on the authority's ticking dropship and needs its possessing PC. The proxy's call is skipped.
void detour_enter_dropship(void* dropship) {
    const auto* const d = static_cast<const uint8_t*>(dropship);
    const unsigned before = d[kDropshipPlayerInside];
    if (d[kActorLocalRole] != kRoleAuthority) {
        if (note_event_cap(g_dropship_enter_exit_logs, kDropshipEnterExitLogs, "dropship enter/exit")) {
            log::line("collision: %s EnterDropship(%p %s, LocalRole %u) skipped: not the authority's dropship; the "
                      "server keeps this player's pawn on foot",
                      side_text(), dropship, class_name(dropship).text, d[kActorLocalRole]);
        }
        return;
    }
    g_last_hook = "EnterDropship";
    g_orig_enter_dropship(dropship);
    log_dropship("EnterDropship", d, before, read_ptr(d, kPawnController));
}

void detour_descend_completed(void* dropship) {
    const auto* const d = static_cast<const uint8_t*>(dropship);
    const unsigned before = d[kDropshipPlayerInside];
    g_last_hook = "DescendToLandCompleted";
    const void* const pc = read_ptr(d, kPawnController); // the dropship's possessor; it repossesses the pawn
    g_orig_descend_completed(dropship);
    log_dropship("DescendToLandCompleted", d, before, pc);
}

ActionTable g_actions{};
ActionEval* g_eval = nullptr; // the action being evaluated, during ProcessActionMappingEvent only
uint32_t g_eval_modifier = 0; // index of the next ModifyRaw inside the mapping's ApplyModifiers
bool g_in_mapping_modifiers = false;

// The action's row. A new row is taken only with add (the action is active this row); a full
// table gives the summed "other" row.
ActionStat* action_stat(const void* action, bool add) {
    uint64_t name = 0;
    memcpy(&name, static_cast<const uint8_t*>(action) + kObjectName, sizeof(name));
    for (uint32_t i = 0; i < g_actions.count; ++i) {
        if (g_actions.entry[i].name == name) {
            return &g_actions.entry[i];
        }
    }
    if (!add) {
        return nullptr;
    }
    if (g_actions.count == kTrackedActions) {
        return &g_actions.other;
    }
    ActionStat& stat = g_actions.entry[g_actions.count++];
    stat.name = name;
    stat.text = object_name(action);
    return &stat;
}

// FEnhancedInputActionEventBinding::Execute(const FInputActionInstance&): the bound handler runs
// inside. Counted before the original, so nothing is read after the handler ran.
void note_handled(const void* instance) {
    const void* const action = instance != nullptr ? read_ptr(instance, kInstanceSourceAction) : nullptr;
    ActionStat* const stat = action != nullptr ? action_stat(action, true) : nullptr;
    if (stat != nullptr) {
        ++stat->handled;
    }
}

void detour_execute_value(const void* binding, const void* instance) {
    if (g_gate_diag) {
        note_handled(instance);
    }
    pl_execute(instance); // card 155
    g_orig_execute_value(binding, instance);
}

void detour_execute_void(const void* binding, const void* instance) {
    if (g_gate_diag) {
        note_handled(instance);
    }
    pl_execute(instance); // card 155
    g_orig_execute_void(binding, instance);
}

void detour_execute_dynamic(const void* binding, const void* instance) {
    if (g_gate_diag) {
        note_handled(instance);
    }
    pl_execute(instance); // card 155
    g_orig_execute_dynamic(binding, instance);
}

double value_len(const void* value) {
    return vec_len(read_vec3(value, 0));
}

void detour_process_mapping(void* player_input, const void* const* action, float delta_seconds, bool paused,
                            const void* raw_value, uint8_t key_event, const void* modifiers, const void* triggers,
                            bool always_tick) {
    const double raw = *action != nullptr ? value_len(raw_value) : 0.0;
    ActionStat* const stat = *action != nullptr ? action_stat(*action, raw > 0.0) : nullptr;
    ActionEval* const eval = stat != nullptr ? &stat->eval : nullptr;
    if (eval != nullptr) {
        ++eval->mappings;
        eval->raw_nonzero += raw > 0.0 ? 1u : 0u;
        eval->raw_max = raw > eval->raw_max ? raw : eval->raw_max;
    }
    ActionEval* const outer = g_eval;
    g_eval = eval;
    g_orig_process_mapping(player_input, action, delta_seconds, paused, raw_value, key_event, modifiers, triggers,
                           always_tick);
    g_eval = outer;
}

void* detour_apply_modifiers(const void* player_input, void* ret, const void* modifiers, const void* value,
                             float delta_seconds) {
    if (g_eval == nullptr || g_in_mapping_modifiers) {
        return g_orig_apply_modifiers(player_input, ret, modifiers, value, delta_seconds);
    }
    ActionEval& e = *g_eval;
    g_in_mapping_modifiers = true;
    g_eval_modifier = 0;
    void* const out = g_orig_apply_modifiers(player_input, ret, modifiers, value, delta_seconds);
    g_in_mapping_modifiers = false;
    const double v = value_len(ret);
    e.modified_nonzero += v > 0.0 ? 1u : 0u;
    e.modified_max = v > e.modified_max ? v : e.modified_max;
    e.modifiers = g_eval_modifier > e.modifiers ? g_eval_modifier : e.modifiers;
    return out;
}

constexpr uint32_t kActionModLines = 6;
constexpr size_t kGasModAttribute = 0x28; // FGameplayAttribute: FString AttributeName first
constexpr int32_t kAttrNameChars = 64;

ActionModLog g_action_mods{};

uint32_t action_mod_slot(const void* modifier) {
    for (uint32_t i = 0; i < kActionModSlots; ++i) {
        if (g_action_mods.modifier[i] == modifier || g_action_mods.modifier[i] == nullptr) {
            g_action_mods.modifier[i] = modifier;
            return i;
        }
    }
    return kActionModSlots;
}

// FString AttributeName of a USTInputGASPlayerAttributeModifier, as ASCII (bounded).
ObjectName gas_attribute_name(const void* modifier) {
    ObjectName out{};
    const auto* const data = static_cast<const wchar_t*>(read_ptr(modifier, kGasModAttribute));
    int32_t num = 0;
    memcpy(&num, static_cast<const uint8_t*>(modifier) + kGasModAttribute + 8, sizeof(num));
    const int32_t n = (data == nullptr || num <= 0) ? 0 : (num < kAttrNameChars ? num : kAttrNameChars);
    const int32_t cap = static_cast<int32_t>(sizeof(out.text)) - 1;
    for (int32_t i = 0; i < n && i < cap && data[i] != 0; ++i) {
        out.text[i] = (data[i] >= 0x20 && data[i] < 0x7F) ? static_cast<char>(data[i]) : '?';
    }
    return out;
}

void log_action_modifier(const void* modifier, const double* in, const double* out) {
    const uint32_t slot = action_mod_slot(modifier);
    if (slot >= kActionModSlots || g_action_mods.lines[slot] >= kActionModLines) {
        return;
    }
    ++g_action_mods.lines[slot];
    const ObjectName cls = class_name(modifier);
    const bool gas = strcmp(cls.text, "STInputGASPlayerAttributeModifier") == 0;
    log::line("input: action-level modifier %p %s%s%s: in (%.4f, %.4f, %.4f) -> out (%.4f, %.4f, %.4f)", modifier,
              cls.text, gas ? ", attribute " : "", gas ? gas_attribute_name(modifier).text : "", in[0], in[1], in[2],
              out[0], out[1], out[2]);
}

void* detour_modify_raw(const void* modifier, void* ret, const void* player_input, const void* value,
                        float delta_seconds) {
    if (!g_in_mapping_modifiers) {
        double in[3];
        memcpy(in, value, sizeof(in)); // a caller temporary: read before the call
        void* const out = g_orig_modify_raw(modifier, ret, player_input, value, delta_seconds);
        if (in[0] != 0.0 || in[1] != 0.0 || in[2] != 0.0) {
            double o[3];
            memcpy(o, ret, sizeof(o));
            log_action_modifier(modifier, in, o);
        }
        return out;
    }
    if (g_eval == nullptr || g_eval_modifier >= kTrackedModifiers) {
        g_eval_modifier += g_eval != nullptr ? 1u : 0u;
        return g_orig_modify_raw(modifier, ret, player_input, value, delta_seconds);
    }
    alignas(8) uint8_t in[kValueBytes];
    memcpy(in, value, sizeof(in)); // the value is passed by copy (a caller temporary): read it before the call
    void* const out = g_orig_modify_raw(modifier, ret, player_input, value, delta_seconds);
    const uint32_t i = g_eval_modifier++;
    ModifierEval& m = g_eval->mod[i];
    if (m.cls.text[0] == '\0') {
        m.cls = class_name(modifier);
    }
    const double a = value_len(in);
    const double b = value_len(ret);
    m.in_nonzero += a > 0.0 ? 1u : 0u;
    m.out_nonzero += b > 0.0 ? 1u : 0u;
    m.in_max = a > m.in_max ? a : m.in_max;
    m.out_max = b > m.out_max ? b : m.out_max;
    return out;
}

// --- Card 016: action-level trigger evaluation ---------------------------------------------
// UEnhancedPlayerInput::EvaluateInputDelegates (0x0446B2B0), per action instance (rdi = instance-8):
//   +0x446C073 NoTriggerState = |value|^2 >= 1e-8 (double at 0x147AFFA28) ? Triggered : None
//   +0x446C07E r14 = mapping-merged state (GetState of the tracker the mappings merged into)
//   +0x446C0DB call FTriggerStateTracker::EvaluateTriggers(instance+0x9, ..., action Triggers)  <- this site
//   +0x446C0E0 if bMappingTriggerApplied (instance+0x11): state = min(state, r14)
//   +0x446C0F6 if bGamePaused (arg 4) && !Action->bTriggerWhenPaused (+0x40): state = None
//   +0x446C178 TriggerEvent (instance+0x12) from (LastTriggerState instance+0x8, state); +0x446C1AC Last = state
// Log only: the return value is passed through unchanged.
constexpr uint32_t kActionEvalSiteRet = 0x0446C0E0;
constexpr size_t kTrackerInstanceOffset = 0x9;     // tracker = instance + 0x9 (lea rcx,[rdi+0x11])
constexpr size_t kInstanceLastTriggerState = 0x8;  // mov [rdi+0x10], sil at +0x446C1AC
constexpr size_t kTrackerMappingApplied = 0x8;     // cmp byte [rdi+0x19],0 at +0x446C0E0
constexpr size_t kActionTriggerWhenPaused = 0x40;  // cmp byte [rbx+0x40],0 at +0x446C103 (rbx = SourceAction)
constexpr size_t kActionValueType = 0x48;          // FInputActionInstance ctor 0x04463930
constexpr size_t kActionTriggers = 0x50;           // TArray<UInputTrigger*>; Num at +0x58
constexpr size_t kActionModifiers = 0x60;          // TArray<UInputModifier*>; Num at +0x68
constexpr size_t kTriggerActuationThreshold = 0x28; // UInputTrigger ctor 0x044649B0 stores 0.5f
constexpr size_t kTriggerAlwaysTick = 0x2C;

constexpr int32_t kDumpedEntries = 4;

EvaluateInputDelegatesFn g_orig_evaluate_delegates = nullptr;
const void* g_action_eval_ret = nullptr; // set in ubw::init; null if the site check failed
bool g_delegates_paused = false;         // bGamePaused of the EvaluateInputDelegates call in progress

DumpedActions g_dumped{}; // per map (reset_card013_map)

void resolve_action_eval_site() {
    static constexpr uint8_t kAfter[] = {0x80, 0x7F, 0x19, 0x00}; // cmp byte ptr [rdi+0x19], 0
    const uint8_t* const ret = g_exe_base + kActionEvalSiteRet;
    int32_t rel = 0;
    memcpy(&rel, ret - 4, sizeof(rel));
    const uint8_t* const target = ret + rel;
    if (ret[-5] != 0xE8 || target != g_exe_base + offsets::FTriggerStateTracker_EvaluateTriggers.rva ||
        memcmp(ret, kAfter, sizeof(kAfter)) != 0) {
        log::line("ERROR hooks: exe+0x446C0DB is not `call EvaluateTriggers; cmp byte [rdi+0x19],0`; action-level "
                  "trigger states will not be counted");
        return;
    }
    g_action_eval_ret = ret;
}

// The objects of a TArray<UObject*>: up to kDumpedEntries, "class(name)" each, with a trigger's threshold.
void log_action_entries(const uint8_t* action, size_t array_offset, const char* what, bool triggers) {
    const int32_t num = array_num(action, array_offset);
    const auto* const data = static_cast<const uint8_t* const*>(read_ptr(action, array_offset));
    for (int32_t i = 0; i < num && i < kDumpedEntries && data != nullptr; ++i) {
        const uint8_t* const o = data[i];
        float threshold = 0.0f;
        if (o != nullptr && triggers) {
            memcpy(&threshold, o + kTriggerActuationThreshold, sizeof(threshold));
        }
        log::line("input: action %s %s[%d] %p %s ActuationThreshold %.3f bShouldAlwaysTick %u",
                  object_name(action).text, what, i, static_cast<const void*>(o), class_name(o).text,
                  static_cast<double>(threshold), (o != nullptr && triggers) ? o[kTriggerAlwaysTick] : 0xFFu);
    }
}

// Once per action per map: value type, bTriggerWhenPaused, the action-level triggers and modifiers.
void dump_action_once(const uint8_t* action) {
    uint64_t name = 0;
    memcpy(&name, action + kObjectName, sizeof(name));
    for (uint32_t i = 0; i < g_dumped.count; ++i) {
        if (g_dumped.name[i] == name) {
            return;
        }
    }
    if (g_dumped.count == kDumpedActions) {
        return;
    }
    g_dumped.name[g_dumped.count++] = name;
    log::line("input: action %s (%s): ValueType %u, bTriggerWhenPaused %u, action Triggers %d, action Modifiers %d",
              object_name(action).text, g_client_session ? "client" : "host/standalone", action[kActionValueType],
              action[kActionTriggerWhenPaused], array_num(action, kActionTriggers), array_num(action, kActionModifiers));
    log_action_entries(action, kActionTriggers, "trigger", true);
    log_action_entries(action, kActionModifiers, "modifier", false);
}

void note_action_eval(const uint8_t* tracker, uint8_t state) {
    const uint8_t* const instance = tracker - kTrackerInstanceOffset;
    const auto* const action = static_cast<const uint8_t*>(read_ptr(instance, kInstanceSourceAction));
    if (action == nullptr) {
        return;
    }
    dump_action_once(action);
    const uint8_t last = instance[kInstanceLastTriggerState];
    const bool active = state != 0 || last != 0 || tracker[0] != 0;
    ActionStat* const stat = action_stat(action, active);
    if (stat == nullptr) {
        return;
    }
    ++stat->act_evals;
    stat->act_state[state < 3 ? state : 0] += state < 3 ? 1u : 0u;
    stat->last_state[last < 3 ? last : 0] += last < 3 ? 1u : 0u;
    stat->no_trigger_hit += tracker[0] == 2 ? 1u : 0u;
    stat->mapping_applied += tracker[kTrackerMappingApplied] != 0 ? 1u : 0u;
    stat->paused += g_delegates_paused ? 1u : 0u;
}

void detour_evaluate_delegates(void* player_input, const void* components, float delta_seconds, bool paused,
                               const void* key_states) {
    const bool outer = g_delegates_paused;
    g_delegates_paused = paused;
    g_orig_evaluate_delegates(player_input, components, delta_seconds, paused, key_states);
    g_delegates_paused = outer;
}

uint8_t detour_evaluate_triggers(void* tracker, const void* player_input, const void* triggers, const void* value,
                                 float delta_seconds) {
    const void* const ret = _ReturnAddress();
    const uint8_t state = g_orig_evaluate_triggers(tracker, player_input, triggers, value, delta_seconds);
    if (g_eval != nullptr && state < 3) {
        ++g_eval->state[state];
    }
    if (ret == g_action_eval_ret && g_action_eval_ret != nullptr) {
        note_action_eval(static_cast<const uint8_t*>(tracker), state);
    }
    return state;
}

void log_action_stat(const char* tag, uint32_t k, const ActionStat& stat) {
    const ActionEval& e = stat.eval;
    log::line("%s[%u]: action %s: mapping evaluations %u, raw nonzero %u (max |v| %.4f), after mapping modifiers "
              "nonzero %u (max |v| %.4f), mapping modifiers %u, mapping triggers None %u Ongoing %u Triggered %u, "
              "binding Execute %u",
              tag, k, stat.text.text, e.mappings, e.raw_nonzero, e.raw_max, e.modified_nonzero, e.modified_max,
              e.modifiers, e.state[0], e.state[1], e.state[2], stat.handled);
    if (stat.act_evals != 0) {
        log::line("%s[%u]: action %s: action-level EvaluateTriggers %u: None %u Ongoing %u Triggered %u; at entry "
                  "NoTriggerState Triggered %u, bMappingTriggerApplied %u, bGamePaused %u; previous final state None "
                  "%u Ongoing %u Triggered %u",
                  tag, k, stat.text.text, stat.act_evals, stat.act_state[0], stat.act_state[1], stat.act_state[2],
                  stat.no_trigger_hit, stat.mapping_applied, stat.paused, stat.last_state[0], stat.last_state[1],
                  stat.last_state[2]);
    }
    if (e.raw_nonzero == 0 || e.modified_nonzero != 0) {
        return; // modifier detail only where the mapping modifiers zeroed a nonzero raw value
    }
    const uint32_t n = e.modifiers < kTrackedModifiers ? e.modifiers : kTrackedModifiers;
    for (uint32_t i = 0; i < n; ++i) {
        const ModifierEval& m = e.mod[i];
        log::line("%s[%u]: action %s modifier %u %s: in nonzero %u (max |v| %.4f), out nonzero %u (max |v| %.4f)",
                  tag, k, stat.text.text, i, m.cls.text, m.in_nonzero, m.in_max, m.out_nonzero, m.out_max);
    }
}

void log_actions(const char* tag, uint32_t k) {
    const ActionStat& other = g_actions.other;
    const bool other_active = other.eval.mappings != 0 || other.handled != 0 || other.act_evals != 0;
    if (g_actions.count == 0 && !other_active) {
        log::line("%s[%u]: actions: none active since last row", tag, k);
        return;
    }
    for (uint32_t i = 0; i < g_actions.count; ++i) {
        log_action_stat(tag, k, g_actions.entry[i]);
    }
    if (other_active) {
        log_action_stat(tag, k, other);
    }
}

InputTagFn g_orig_pc_tag_pressed = nullptr;
InputTagFn g_orig_pc_tag_released = nullptr;
InputTagFn g_orig_char_tag_pressed = nullptr;
AscInputTagFn g_orig_asc_tag_pressed = nullptr;
TryActivateFn g_orig_try_activate = nullptr;
InternalTryActivateFn g_orig_internal_try_activate = nullptr;
ServerTryActivateFn g_orig_server_try_activate = nullptr;
ClientActivateFailedFn g_orig_client_activate_failed = nullptr;
ClientActivateSucceedFn g_orig_client_activate_succeed = nullptr;
CharacterFn g_orig_character_jump = nullptr;
CharacterFn g_orig_mi_jump = nullptr;
CharacterBoolFn g_orig_can_jump = nullptr;
CharacterBoolFn g_orig_can_jump_internal = nullptr;
CheckJumpInputFn g_orig_check_jump_input = nullptr;
SprintChangeFn g_orig_sprint_change = nullptr;

constexpr size_t kPcInputFlags = 0x618;        // byte; HandleInputTag_Pressed tests bit 3
constexpr size_t kCharacterJumpFlags = 0x450;  // ACharacter::Jump (0x039B5850): or byte [rcx+0x450], 8
constexpr uint8_t kCharacterPressedJump = 0x8;
constexpr size_t kCharacterJumpKeyHoldTime = 0x454; // ACharacter::Jump zeroes it

                                               // (FActiveGameplayEffectHandle: int32 Handle, then a bool)
constexpr uint32_t kJumpLines = 48;
constexpr uint32_t kCheckJumpLines = 12;
constexpr ULONGLONG kTagWindowMs = 1500;
constexpr ULONGLONG kTagRepeatMs = 250; // same tag again within this gap = held (Triggered every frame), not logged

JumpTrace g_jump{};

bool jump_line() {
    return g_jump.lines < kJumpLines ? (++g_jump.lines, true) : false;
}

bool tag_window() {
    return g_jump.in_server_try || GetTickCount64() < g_jump.tag_window_until;
}

bool pressed_jump(const void* character) {
    return (static_cast<const uint8_t*>(character)[kCharacterJumpFlags] & kCharacterPressedJump) != 0;
}

// True for the first call of a tag after a gap: a held tag (InputTag.Move fires every frame) logs once.
// Per tag (several held at once alternate, e.g. InputTag.Move and InputTag.Walk); slot 0 is reused when full.
bool new_tag_edge(TagEdge& edge, uint64_t tag) {
    const ULONGLONG now = GetTickCount64();
    uint32_t slot = 0;
    for (uint32_t i = 0; i < kTagEdgeSlots; ++i) {
        if (edge.tag[i] == tag || edge.tag[i] == 0) {
            slot = i;
            break;
        }
    }
    const bool repeat = edge.tag[slot] == tag && now - edge.last_ms[slot] < kTagRepeatMs;
    edge.tag[slot] = tag;
    edge.last_ms[slot] = now;
    return !repeat;
}

void log_pc_tag(const char* what, const void* pc, uint64_t tag) {
    g_jump.tag_window_until = GetTickCount64() + kTagWindowMs;
    if (jump_line()) {
        const auto* const p = static_cast<const uint8_t*>(pc);
        log::line("jump: %s ASTPlayerController::HandleInputTag_%s %s: pc %p, input receiver (+0x7C0) %p, flags "
                  "(+0x618) 0x%02X (bit 3 %u)",
                  side(), what, fname_text(&tag).text, pc, read_ptr(pc, kPcInputReceiver), p[kPcInputFlags],
                  (p[kPcInputFlags] >> 3) & 1u);
    }
}

void detour_pc_tag_pressed(void* pc, uint64_t tag) {
    const bool edge = new_tag_edge(g_jump.pressed, tag);
    if (edge) {
        log_pc_tag("Pressed", pc, tag);
    }
    g_jump.inner_tag_lines = edge;
    g_orig_pc_tag_pressed(pc, tag);
    g_jump.inner_tag_lines = false;
}

void detour_pc_tag_released(void* pc, uint64_t tag) {
    if (new_tag_edge(g_jump.released, tag)) {
        log_pc_tag("Released", pc, tag);
    }
    g_orig_pc_tag_released(pc, tag);
}

void detour_char_tag_pressed(void* receiver, uint64_t tag) {
    if (g_jump.inner_tag_lines && jump_line()) {
        log::line("jump: %s ASTPlayerCharacter::HandleInputTag_Pressed %s (interface this %p)", side(),
                  fname_text(&tag).text, receiver);
    }
    g_orig_char_tag_pressed(receiver, tag);
}

TagMapProbe probe_input_tag_map(const void* asc, const void* tag) {
    const auto* const a = static_cast<const uint8_t*>(asc);
    TagMapProbe r{0, 0, -1};
    memcpy(&r.num, a + kAscInputTagMapNum, sizeof(r.num));
    memcpy(&r.free, a + kAscInputTagMapFree, sizeof(r.free));
    const auto* const data = static_cast<const uint8_t*>(read_ptr(asc, kAscInputTagMap));
    uint64_t want = 0;
    memcpy(&want, tag, sizeof(want));
    const int32_t n = (data == nullptr || r.num <= 0) ? 0 : (r.num < kAscInputTagMapMax ? r.num : kAscInputTagMapMax);
    for (int32_t i = 0; i < n; ++i) {
        const uint8_t* const e = data + static_cast<size_t>(i) * kAscInputTagMapStride;
        uint64_t key = 0;
        memcpy(&key, e, sizeof(key));
        if (key == want) {
            memcpy(&r.handles, e + 0x10, sizeof(r.handles));
            break;
        }
    }
    return r;
}

// Card 021 step 3: own cap, so the fire steps are not hidden behind the spent jump caps. Logs every press
// (owner actor, tag, input tag map count, the tag's handles, pending binds), on both machines.
constexpr uint32_t kC21TagLogs = 24;
uint32_t g_c21_tag_logs = 0;

// Card 160 sweep: on the first press on each ASC (up to kC160Maps per session), every input tag in its map, so the
// client's own pawn and weapon maps compare with the host's own. Sparse array: with free slots, some keys are stale.
constexpr int32_t kC160Maps = 8;
struct C160MapDump {
    const void* asc[kC160Maps];
    int32_t n;
};
static C160MapDump g_c160_maps{};

static void c160_dump_tag_map(const void* asc) {
    for (int32_t i = 0; i < g_c160_maps.n; ++i) {
        if (g_c160_maps.asc[i] == asc) {
            return;
        }
    }
    if (g_c160_maps.n >= kC160Maps) {
        return;
    }
    g_c160_maps.asc[g_c160_maps.n++] = asc;
    const TagMapProbe m = probe_input_tag_map(asc, &asc); // num and free only (no key matches a pointer)
    const auto* const data = static_cast<const uint8_t*>(read_ptr(asc, kAscInputTagMap));
    const int32_t n = (data == nullptr || m.num <= 0) ? 0 : (m.num < kAscInputTagMapMax ? m.num : kAscInputTagMapMax);
    char text[900];
    int len = 0;
    text[0] = 0;
    for (int32_t i = 0; i < n && len >= 0 && static_cast<size_t>(len) < sizeof(text); ++i) {
        const uint8_t* const e = data + static_cast<size_t>(i) * kAscInputTagMapStride;
        int32_t handles = 0;
        memcpy(&handles, e + 0x10, sizeof(handles));
        len += snprintf(text + len, sizeof(text) - static_cast<size_t>(len), "%s%s:%d", i == 0 ? "" : ", ",
                        fname_text(e).text, handles);
    }
    const void* const owner = read_ptr(asc, kComponentOwner);
    const void* const holder = owner != nullptr ? weapon_pawn(owner) : nullptr;
    const void* const pawn = is_a(owner, g_player_mi_class()) ? owner : holder;
    log::line("card160: %s input tag map of %s (pawn %s, %s): Num %d free %d: %s", side(), class_name(owner).text,
              class_name(pawn).text, kSprintSideText[sprint_side(pawn)], m.num, m.free, text);
}

void detour_asc_tag_pressed(void* asc, const void* tag) {
    c160_dump_tag_map(asc);
    if (!c21_is_axis_tag(fname_text(tag).text) &&
        note_event_cap(g_c21_tag_logs, kC21TagLogs, "card021 input tag pressed")) {
        c21_log_tag_pressed(asc, tag, probe_input_tag_map(asc, tag));
    }
    if (g_jump.inner_tag_lines && jump_line()) {
        const TagMapProbe m = probe_input_tag_map(asc, tag);
        log::line("jump: %s UADAbilitySystemComponent::AbilityInputTagPressed %s: asc %p %s, owner %s; input tag map "
                  "(+0x1260) Num %d free %d, this tag's spec handles %d (-1 = not in map)",
                  side(), fname_text(tag).text, asc, class_name(asc).text,
                  class_name(read_ptr(asc, kComponentOwner)).text, m.num, m.free, m.handles);
    }
    if (c43_refuse_tag(asc, tag)) {
        return;
    }
    c73_drop_stale(asc, tag);
    if (g_c76_log) {
        c76_tag(asc, tag, true);
    }
    c26_own_fire(asc, tag, true);
    c30_note_input("input tag pressed", read_ptr(asc, kComponentOwner), tag);
    g_orig_asc_tag_pressed(asc, tag);
}

bool detour_try_activate(void* asc, int32_t handle, bool allow_remote) {
    const bool ok = g_orig_try_activate(asc, handle, allow_remote);
    if (tag_window() && jump_line()) {
        log::line("jump: %s UAbilitySystemComponent::TryActivateAbility(handle %d, allow remote %d) = %d", side(),
                  handle, allow_remote ? 1 : 0, ok ? 1 : 0);
    }
    return ok;
}

// Card 019 step 5: its own cap, so the card 016 jump cap (spent by then) does not hide crouch/melee.
// Host: only a client's request (inside InternalServerTryActivateAbility); client: after a local input tag.
constexpr uint32_t kTryLogs019 = 24;
uint32_t g_try_logs019 = 0;
// Card 021 step 3: set by c21_note_activation inside a client request, so the outer detour can tell a request
// the server rejected before InternalTryActivateAbility (unknown handle, net policy) from one it tried.
bool g_c21_inner_seen = false;
constexpr uint32_t kC21FailedLogs = 16;
uint32_t g_c21_failed_logs = 0;

// Card 065: a moving range target shunts the client's pawn through a gameplay event, and the client's own
// activation of GA_ShuntAbilityCharacter with that event data asserts (GameplayAbilityTargetTypes.cpp:240) when
// its target data is serialized. The client refuses it; the host still runs its own pawn's shunt.
bool c65_refuse_client_try(const void* asc, int32_t handle, const void* event_data) {
    if (!g_client_session || event_data == nullptr) {
        return false;
    }
    const uint8_t* const spec = c21_spec_for(asc, handle);
    const ObjectName ability = class_name(spec != nullptr ? read_ptr(spec, 0x10) : nullptr);
    if (strcmp(ability.text, "GA_ShuntAbilityCharacter_C") != 0) {
        return false;
    }
    if (note_event_cap(g_c65_shunt_logs, kC65ShuntLogs, "card065 shunt refusal")) {
        log::line("card065: client refused its own %s (handle %d, event data), owner %s", ability.text, handle,
                  class_name(read_ptr(asc, kComponentOwner)).text);
    }
    return true;
}

bool detour_internal_try_activate(void* asc, int32_t handle, const void* prediction_key, void** out_ability,
                                  void* on_ended, const void* event_data) {
    const unsigned long long caller = exe_rva(_ReturnAddress());
    void* local = nullptr;
    void** const out = out_ability != nullptr ? out_ability : &local;
    // InternalServerTryActivateAbility passes an uninitialized stack slot; the engine writes it only on success, so a
    // failed client request would leave garbage for the class_name reads below (card 101 crash, c101i).
    *out = nullptr;
    if (c65_refuse_client_try(asc, handle, event_data)) {
        return false;
    }
    const bool c62 = c62_before_try(asc, handle);
    // Card 101: a client's own request carries its prediction key, so its LocalPredicted ability starts on the server
    // copy as it is; read as ServerOnly (card 104) the host also tells the client to start it, which doubles it.
    C104Policy policy{};
    if (g_jump.in_server_try && !g_client_session) {
        c101_note_client_sent(handle);
    } else {
        c104_policy_set(asc, handle, "InternalTryActivateAbility", &policy);
    }
    const bool ok = g_orig_internal_try_activate(asc, handle, prediction_key, out, on_ended, event_data);
    c104_policy_restore(policy);
    if (c62) {
        c62_after_try(asc, handle, ok);
    }
    if (tag_window() && jump_line()) {
        // Card 456: the ASC's owner names whose ability ran (a shunt inside a hold had no owner).
        log::line("jump: %s UAbilitySystemComponent::InternalTryActivateAbility(handle %d) = %d, ability %s, owner %s",
                  side(), handle, ok ? 1 : 0, class_name(*out).text, object_name(read_ptr(asc, kComponentOwner)).text);
    }
    if (g_jump.in_server_try) {
        g_c21_inner_seen = true;
    }
    if (g_c76_log) {
        c76_try(asc, handle, *out, ok, g_jump.in_server_try);
    }
    c94_try(asc, handle, *out, ok, g_jump.in_server_try, caller);
    c430_reload_try(asc, handle, ok, g_jump.in_server_try);
    c105_activation(asc, handle, *out, ok, g_jump.in_server_try);
    c101_note_try(asc, *out, ok, g_jump.in_server_try);
    if (g_jump.in_server_try || g_client_session) {
        c21_note_activation(asc, handle, g_jump.in_server_try, *out, ok);
    }
    if (!g_jump.in_server_try) {
        c24_log_local_activation(asc, handle, *out, ok);
    }
    c29_note_activation(asc, *out, ok, g_jump.in_server_try);
    pl_activation(asc, *out, ok, g_jump.in_server_try); // card 155
    const bool watched = g_jump.in_server_try || (g_client_session && tag_window());
    if (watched && note_event_cap(g_try_logs019, kTryLogs019, "card019 try-activate")) {
        log::line("card019: %s InternalTryActivateAbility(handle %d)%s = %d, ability %s", side(), handle,
                  g_jump.in_server_try ? " for a client request" : "", ok ? 1 : 0, class_name(*out).text);
    }
    return ok;
}

void detour_server_try_activate(void* asc, int32_t handle, bool input_pressed, const void* prediction_key,
                                const void* event_data) {
    if (jump_line()) {
        log::line("jump: %s UAbilitySystemComponent::InternalServerTryActivateAbility(handle %d, input pressed %d) "
                  "asc %p owner %s",
                  side(), handle, input_pressed ? 1 : 0, asc, class_name(read_ptr(asc, kComponentOwner)).text);
    }
    c30_note_input("InternalServerTryActivateAbility (client request)", read_ptr(asc, kComponentOwner), nullptr);
    const bool outer = g_jump.in_server_try;
    g_jump.in_server_try = true;
    g_c21_inner_seen = false;
    g_orig_server_try_activate(asc, handle, input_pressed, prediction_key, event_data);
    g_jump.in_server_try = outer;
    c24_log_rpc("InternalServerTryActivateAbility", asc, handle, input_pressed ? 1 : 0);
    if (!g_c21_inner_seen) {
        c21_note_activation(asc, handle, true, nullptr, false); // rejected before InternalTryActivateAbility
    }
}

void detour_client_activate_failed(void* asc, int32_t handle, int16_t prediction_key) {
    if (note_event_cap(g_c21_failed_logs, kC21FailedLogs, "card021 activate-failed")) {
        log::line("card021: %s ClientActivateAbilityFailed(handle %d, key %d) owner %s", side(), handle, prediction_key,
                  class_name(read_ptr(asc, kComponentOwner)).text);
    }
    if (jump_line()) {
        log::line("jump: %s UAbilitySystemComponent::ClientActivateAbilityFailed(handle %d, key %d)", side(), handle,
                  prediction_key);
    }
    g_orig_client_activate_failed(asc, handle, prediction_key);
}

void detour_client_activate_succeed(void* asc, int32_t handle, const void* prediction_key) {
    if (jump_line()) {
        log::line("jump: %s UAbilitySystemComponent::ClientActivateAbilitySucceed(handle %d)", side(), handle);
    }
    g_orig_client_activate_succeed(asc, handle, prediction_key);
}

void log_jump_state(const char* what, const void* character) {
    const auto* const c = static_cast<const uint8_t*>(character);
    float hold = 0.0f;
    memcpy(&hold, c + kCharacterJumpKeyHoldTime, sizeof(hold));
    const auto* const cmc = static_cast<const uint8_t*>(read_ptr(character, kCharacterMovement));
    log::line("jump: %s %s: character %p %s, LocalRole %u, bPressedJump %u, JumpKeyHoldTime %.3f, MovementMode %u",
              side(), what, character, class_name(character).text, c[kActorLocalRole], pressed_jump(character) ? 1u : 0u,
              static_cast<double>(hold), cmc != nullptr ? cmc[kCmcMovementMode] : 0xFFu);
}

void detour_mi_jump(void* character) {
    g_orig_mi_jump(character);
    c94_jump_call("ASTPlayerCharacterMI::Jump", character, -1);
    if (jump_line()) {
        log_jump_state("ASTPlayerCharacterMI::Jump returned", character);
    }
}

void detour_character_jump(void* character) {
    g_orig_character_jump(character);
    c94_jump_call("ACharacter::Jump", character, -1);
    if (jump_line()) {
        log_jump_state("ACharacter::Jump returned", character);
    }
}

bool detour_can_jump(const void* character) {
    const bool ok = g_orig_can_jump(character);
    c94_jump_call("ACharacter::CanJump", character, ok ? 1 : 0);
    if ((pressed_jump(character) || tag_window()) && jump_line()) {
        log::line("jump: %s ACharacter::CanJump = %d (bPressedJump %u)", side(), ok ? 1 : 0,
                  pressed_jump(character) ? 1u : 0u);
    }
    return ok;
}

bool detour_can_jump_internal(const void* character) {
    const bool ok = g_orig_can_jump_internal(character);
    c94_jump_call("CanJumpInternal", character, ok ? 1 : 0);
    if ((pressed_jump(character) || tag_window()) && jump_line()) {
        log::line("jump: %s ASTPlayerCharacter::CanJumpInternal_Implementation = %d", side(), ok ? 1 : 0);
    }
    return ok;
}

void detour_check_jump_input(void* character, float delta_seconds) {
    const bool pressed = pressed_jump(character);
    g_orig_check_jump_input(character, delta_seconds);
    if (pressed) {
        c94_jump_call("CheckJumpInput (entered pressed)", character, pressed_jump(character) ? 1 : 0);
    }
    if (pressed && g_jump.check_lines < kCheckJumpLines) {
        ++g_jump.check_lines;
        log_jump_state("ACharacter::CheckJumpInput (entered with bPressedJump) returned", character);
    }
}

ServerCheckClientErrorFn g_orig_server_check_client_error = nullptr;
ServerMovePerformFn g_orig_server_move_perform = nullptr;

uint32_t g_mapping_lines = 0;

bool text_starts_with(const char* text, const char* prefix) {
    for (uint32_t i = 0; i < kNameChars; ++i) {
        if (prefix[i] == '\0') {
            return true;
        }
        if (text[i] != prefix[i]) {
            return false;
        }
    }
    return false;
}

bool text_contains(const char* text, const char* needle) {
    for (uint32_t i = 0; i < kNameChars && text[i] != '\0'; ++i) {
        if (text_starts_with(text + i, needle)) {
            return true;
        }
    }
    return false;
}

// Card 020: every mapping of a keyboard/mouse context (named *_KBM, except the dummy UI one), to find
// the fire, aim and interact keys for the parity script; at most kMaxMappingLines lines per process.
void log_context_mappings(const void* context) {
    if (context == nullptr) {
        return;
    }
    const auto* const data = static_cast<const uint8_t*>(read_ptr(context, kContextMappings));
    int32_t num = 0;
    memcpy(&num, static_cast<const uint8_t*>(context) + kContextMappings + sizeof(void*), sizeof(num));
    if (data == nullptr || num <= 0 || num > kMaxMappingsScanned) {
        log::line("input-map: %s: %d mappings (not listed)", object_name(context).text, num);
        return;
    }
    const ObjectName ctx = object_name(context);
    const bool listed = text_contains(ctx.text, "_KBM") && !text_contains(ctx.text, "DummyUI");
    uint32_t shown = 0;
    for (int32_t i = 0; i < num; ++i) {
        const uint8_t* const m = data + static_cast<size_t>(i) * kMappingBytes;
        const void* const action = read_ptr(m, kMappingAction);
        const ObjectName key = fname_text(m + kMappingKeyName);
        const ObjectName act = object_name(action);
        if (!listed) {
            continue;
        }
        ++shown;
        if (g_mapping_lines >= kMaxMappingLines) {
            continue; // the per-context summary line below still counts it
        }
        ++g_mapping_lines;
        int32_t triggers = 0;
        int32_t modifiers = 0;
        memcpy(&triggers, m + kMappingTriggersNum, sizeof(triggers));
        memcpy(&modifiers, m + kMappingModifiersNum, sizeof(modifiers));
        log::line("input-map: %s [%d] %s <- %s (triggers %d, modifiers %d)", object_name(context).text, i, act.text,
                  key.text, triggers, modifiers);
    }
    log::line("input-map: %s: %d mappings, %u listed", ctx.text, num, shown);
}

ServerDiag g_srv{};
uint32_t g_host_rows_left = 0;
uint32_t g_host_row = 0;
ULONGLONG g_host_next_ms = 0;
uint32_t g_correction_logs = 0;
uint32_t g_c101_correction_logs = 0;

// Card 014: the rows (client rot, host srv) start when input starts, not at map load. Resets the
// row counters so row 0 covers the first second of input.
void open_input_window() {
    g_input_window_armed = false;
    g_counts = DiagCounts{};
    reset_move_diag_row();
    reset_move_chain_row();
    reset_actions();
    g_pawn_watch.reenables_row = 0;
    const ULONGLONG next = GetTickCount64() + kRotRowIntervalMs;
    if (g_client_session) {
        g_rot_rows_left = kRotRows;
        g_rot_row = 0;
        g_rot_next_ms = next;
        return;
    }
    g_srv = ServerDiag{};
    g_host_rows_left = kRotRows;
    g_host_row = 0;
    g_host_next_ms = next;
}

void note_server_correction(const uint8_t* cmc, float timestamp, float delta, const double* accel,
                            const double* client_loc, uint8_t mode) {
    ++g_srv.corrections;
    const void* const updated = read_ptr(cmc, kMoveUpdatedComponent);
    const Vec3 server = updated != nullptr ? read_vec3(updated, kComponentTranslation) : Vec3{};
    const Vec3 client = read_vec3(client_loc, 0);
    g_srv.last_error = Vec3{server.x - client.x, server.y - client.y, server.z - client.z};
    g_srv.last_accel = read_vec3(accel, 0);
    if (c101_in_mantle()) { // card 101: corrections during a mantle, under their own cap
        if (!note_event_cap(g_c101_correction_logs, 60, "card101 mantle correction")) {
            return;
        }
        log::line("card101: host t=%llu correction during mantle: server (%.1f, %.1f, %.1f) client (%.1f, %.1f, %.1f) "
                  "error %.1f", static_cast<unsigned long long>(GetTickCount64()), server.x, server.y, server.z,
                  client.x, client.y, client.z, vec_len(g_srv.last_error));
        return;
    }
    if (!note_event(g_correction_logs, "ServerCheckClientError correction")) {
        return;
    }
    log::line("srv: correction cmc=%p timestamp %.4f delta %.4f client mode %u, server (%.1f, %.1f, %.1f) client "
              "(%.1f, %.1f, %.1f) error %.1f, client accel |a| %.1f",
              static_cast<const void*>(cmc), static_cast<double>(timestamp), static_cast<double>(delta), mode,
              server.x, server.y, server.z, client.x, client.y, client.z, vec_len(g_srv.last_error),
              vec_len(g_srv.last_accel));
}

// By-reference FVectors arrive as pointers and pass through; the FName and mode by value.
bool detour_server_check_client_error(void* cmc, float timestamp, float delta, const double* accel,
                                      const double* client_loc, const double* relative_client_loc, void* base,
                                      uint64_t base_bone, uint8_t mode) {
    const bool correct = g_orig_server_check_client_error(cmc, timestamp, delta, accel, client_loc,
                                                          relative_client_loc, base, base_bone, mode);
    if (g_client_session) {
        return correct;
    }
    if (g_input_window_armed && vec_len(read_vec3(accel, 0)) > 0.0) {
        // Card 014: an unattended host has no input of its own; the remote client's first move opens it.
        log::line("srv: input window opens on remote client acceleration |a| %.1f (cmc=%p), %u rows follow",
                  vec_len(read_vec3(accel, 0)), cmc, kRotRows);
        open_input_window();
    }
    ++g_srv.checks;
    if (correct) {
        note_server_correction(static_cast<const uint8_t*>(cmc), timestamp, delta, accel, client_loc, mode);
    }
    return correct;
}

// Card 021 step 3: one AbilityInputTagPressed call. The weapon actor does not replicate, so its own roles say
// nothing; the side is that of its Owner (AActor::Owner +0x158, the pawn).
void c21_log_tag_pressed(const void* asc, const void* tag, const TagMapProbe& m) {
    const void* const owner = read_ptr(asc, kComponentOwner);
    const void* const holder = owner != nullptr ? weapon_pawn(owner) : nullptr;
    const void* const pawn = is_a(owner, g_player_mi_class()) ? owner : holder;
    log::line("card021: %s AbilityInputTagPressed %s on asc %p, owner %s (pawn %s, %s); tag map Num %d free %d, "
              "this tag's handles %d (-1 = not in map), pending binds %d",
              side(), fname_text(tag).text, asc, class_name(owner).text, class_name(pawn).text,
              kSprintSideText[sprint_side(pawn)], m.num, m.free, m.handles, read_i32(asc, kAscPendingBindNum));
}

// Card 439 (diagnostic): the client's nuke launcher SpoolDown activations with their caller chain, to show whether
// an activation that cuts the Fire animation short comes from the network receive path or from the client's own
// timer or animation event. 6 rows.
constexpr uint32_t kC439Rows = 6;
constexpr uint32_t kC439Deep = 56; // frames captured for the deep chain row
uint32_t g_c439_rows = 0;
void c439_note_spool_down(void* asc, int32_t handle, bool ok) {
    if (!g_client_session || g_c439_rows >= kC439Rows) {
        return;
    }
    const uint8_t* const spec = c21_spec_for(asc, handle);
    const void* const cdo = spec != nullptr ? read_ptr(spec, kSpecAbility) : nullptr;
    if (cdo == nullptr || strstr(class_name(cdo).text, "SpoolDownAbility_NukeLauncher") == nullptr) {
        return;
    }
    ++g_c439_rows;
    char chain[512];
    caller_chain(chain, sizeof(chain), 14);
    log::line("card439: client t=%llu nuke SpoolDown handle %d = %d, callers %s", GetTickCount64(), handle,
              ok ? 1 : 0, chain);
    // Step 2: the 14 frames above end inside Blueprint script. Frames 8 and up, to the root of the call.
    void* frames[kC439Deep] = {};
    const USHORT n = RtlCaptureStackBackTrace(0, kC439Deep, frames, nullptr);
    char deep[kC439Deep * 14] = "";
    size_t used = 0;
    for (USHORT i = 8; i < n && used < sizeof(deep); ++i) {
        const int w = snprintf(deep + used, sizeof(deep) - used, "%s0x%llX", i == 8 ? "" : " < ", exe_rva(frames[i]));
        if (w < 0) {
            break;
        }
        used += static_cast<size_t>(w);
    }
    log::line("card439: client t=%llu nuke SpoolDown deep callers (%u frames, from frame 8) %s", GetTickCount64(),
              static_cast<unsigned>(n), deep);
}

// Card 439 step 2 (diagnostic): each EndAbility of a nuke launcher ability on the client, with its caller chain, to
// show whether the fire ability ends after the SpoolDown event and who ends it. 24 rows.
constexpr uint32_t kC439EndRows = 24;
uint32_t g_c439_end_rows = 0;
void c439_note_end(const void* ability, int32_t handle, bool cancelled) {
    if (!g_client_session || g_c439_end_rows >= kC439EndRows) {
        return;
    }
    const ObjectName cls = class_name(ability);
    if (strstr(cls.text, "NukeLauncher") == nullptr) {
        return;
    }
    ++g_c439_end_rows;
    char chain[512];
    caller_chain(chain, sizeof(chain), kMaxStackFrames);
    log::line("card439: client t=%llu EndAbility %s (handle %d), cancelled %d, callers %s", GetTickCount64(),
              cls.text, handle, cancelled ? 1 : 0, chain);
}

void c21_note_activation(void* asc, int32_t handle, bool server_request, void* ability, bool ok) {
    c439_note_spool_down(asc, handle, ok);
    const int32_t stage = server_request ? (g_c21_inner_seen ? 2 : 1) : 0;
    if (!c21_first_seen(handle * 8 + stage * 2 + (ok ? 1 : 0))) {
        return;
    }
    const uint8_t* const spec = c21_spec_for(asc, handle);
    log::line("card021: %s %s handle %d: %s, spec %s, ability %s, owner %s", side(),
              server_request ? "client request" : "local activation", handle,
              stage == 1 ? "rejected before InternalTryActivateAbility" : (ok ? "activated" : "failed"),
              spec != nullptr ? "present" : "absent",
              class_name(spec != nullptr ? read_ptr(spec, kSpecAbility) : ability).text,
              class_name(read_ptr(asc, kComponentOwner)).text);
}

// Card 214 / 217 (b192): per handle (fixed table of kC24RunHandles), the run of same-result activations; entries 1,
// 10 and 100 of a run are logged, for successes and fails alike. The other result ends the run; another handle does
// not (card 217: Glide and the weapon handles interleave). A handle past the table is logged at no run entry.
constexpr uint32_t kC24RunHandles = 8;
struct C24Run {
    int32_t handle;
    bool ok;
    uint32_t run;
};
static C24Run g_c24_runs[kC24RunHandles] = {};
static uint32_t g_c24_run_count = 0;

// Returns the run length to log, or 0 when this activation is not one of the logged entries.
static uint32_t c24_run_entry(int32_t handle, bool ok) {
    uint32_t i = 0;
    while (i < g_c24_run_count && g_c24_runs[i].handle != handle) {
        ++i;
    }
    if (i == g_c24_run_count) {
        if (i >= kC24RunHandles) {
            return 0;
        }
        g_c24_runs[g_c24_run_count++] = C24Run{handle, ok, 0};
    }
    C24Run& r = g_c24_runs[i];
    if (r.ok != ok) {
        r.ok = ok;
        r.run = 0;
    }
    ++r.run;
    for (const uint32_t entry : kC24FailRunLogs) {
        if (r.run == entry) {
            return r.run;
        }
    }
    return 0;
}

// Client: each local activation of its own pawn or weapon ASC (owner pawn or weapon), capped.
void c24_log_local_activation(const void* asc, int32_t handle, const void* ability, bool ok) {
    if (!g_client_session) {
        return;
    }
    const uint32_t run = c24_run_entry(handle, ok);
    if (run == 0) {
        return;
    }
    if (!note_event_cap(g_c24_local_lines, kC24LocalLines, "card024 client activation")) {
        return;
    }
    const void* owner = asc != nullptr ? read_ptr(asc, kComponentOwner) : nullptr;
    log::line("card024: client local activation handle %d = %d, ability %s, owner %s (%s %u in a row, t=%llu)", handle,
              ok ? 1 : 0, class_name(ability).text,
              class_name(owner).text, ok ? "success" : "fail", run, static_cast<unsigned long long>(GetTickCount64()));
}

// ---- Card 029 activations: one line per (ability class, owner pawn side, server request, result) on first
// sight, then at 10, 100 and 1000 hits.
void c29_note_activation(const void* asc, const void* ability, bool ok, bool server_request) {
    if (!g_c29_audit || asc == nullptr) {
        return;
    }
    const void* const owner = read_ptr(asc, kComponentOwner);
    const void* const pawn = owner != nullptr && !is_a(owner, g_player_mi_class()) ? weapon_pawn(owner) : owner;
    if (!is_a(pawn, g_player_mi_class())) {
        return; // NPC abilities
    }
    const void* const cls = ability != nullptr ? read_ptr(ability, kObjectClass) : nullptr;
    const int32_t cls_id = object_index(cls);
    const uint32_t s = sprint_side(pawn);
    uint32_t i = 0;
    while (i < g_c29.act_count && !(g_c29.acts[i].ability_class == cls_id && g_c29.acts[i].pawn_side == s &&
                                    g_c29.acts[i].server == server_request && g_c29.acts[i].ok == ok)) {
        ++i;
    }
    if (i == g_c29.act_count) {
        if (i >= kC29Acts) {
            return;
        }
        g_c29.acts[g_c29.act_count++] = C29Act{cls_id, s, server_request, ok, 0};
    }
    const uint32_t n = ++g_c29.acts[i].count;
    if ((n == 1 || n == 10 || n == 100 || n == 1000) &&
        note_event_cap(g_c29.act_lines, kC29ActLines, "card029 activation")) {
        log::line("card029: %s activation %s = %d%s on %s (owner %s, pawn %s, %s), hit %u", side(),
                  object_name(cls).text, ok ? 1 : 0, server_request ? " for a client request" : "",
                  class_name(owner).text, object_name(owner).text, object_name(pawn).text, kSprintSideText[s], n);
    }
}

// The game's own SetInputMode on the local PC (the mod's own calls are skipped): the restore target.
void c31_note_mode(const void* pc, const void* mode) {
    if (g_c31.own_mode_call || !is_local_pc(pc)) { // always recorded: the card 052 panel restores it too
        return;
    }
    const void* const vtable = read_ptr(mode, 0);
    C31Kind kind = C31Kind::None;
    for (uint32_t i = 0; i < sizeof(kInputModes) / sizeof(kInputModes[0]); ++i) {
        kind = vtable == g_exe_base + kInputModes[i].vtable_rva ? static_cast<C31Kind>(i + 1) : kind;
    }
    if (kind == C31Kind::None) {
        log::line("WARN card031: SetInputMode with unknown vtable exe+0x%llX: not recorded", exe_rva(vtable));
        return;
    }
    const auto* const m = static_cast<const uint8_t*>(mode);
    g_c31.game_kind = kind;
    g_c31.game_lock = kind == C31Kind::GameOnly ? 0 : m[kInputModeLock];
    g_c31.game_flag = kind == C31Kind::GameOnly ? m[kInputModeConsumeMouse]
                                                : (kind == C31Kind::GameAndUI ? m[kInputModeHideCursor] : 0);
    if (c31_line()) {
        log::line("card031: game SetInputMode %s (lock %u, flag %u)%s", kC31KindText[static_cast<uint8_t>(kind)],
                  g_c31.game_lock, g_c31.game_flag, g_c31.shown ? " WHILE THE OVERLAY IS SHOWN" : "");
    }
}

// AbilityInputTagPressed: true = refused (Walk on a downed player's pawn or weapon ASC). Card 109: a downed player
// can shoot, so Fire is no longer refused.
bool c43_refuse_tag(void* asc, const void* tag) {
    if (g_c43.count == 0) {
        return false;
    }
    void* const owner = read_ptr(asc, kComponentOwner);
    const void* const cls = g_player_character_class();
    void* const pawn = owner != nullptr && is_a(owner, cls) ? owner : const_cast<void*>(weapon_pawn(owner));
    if (pawn == nullptr || !is_a(pawn, cls) || !c43_is_down(pawn)) {
        return false;
    }
    const uint64_t name = read_u64(tag, 0);
    const bool refuse = name == fname_cached(g_c43.walk_tag, L"InputTag.Walk");
    if (refuse && note_event_cap(g_c43.refuse_lines, kC43RefuseLines, "card043 refused tag")) {
        log::line("card043: %s refused %s on downed %s (%s)", side(), fname_text(tag).text, object_name(pawn).text,
                  kSprintSideText[sprint_side(pawn)]);
    }
    return refuse;
}

// AddMappingContext, both sides: logged inside the death window.
void c44_context(const void* context, int32_t priority) {
    const ULONGLONG now = GetTickCount64();
    if (g_c44.window_ms == 0 || now - g_c44.window_ms > kC44WindowMs) {
        return;
    }
    if (note_event_cap(g_c44.context_lines, kC44ContextLines, "card044 input context")) {
        log::line("card044: %s t=%llu AddMappingContext %s priority %d, %llu ms into the death window", side(),
                  static_cast<unsigned long long>(now), object_name(context).text, priority,
                  static_cast<unsigned long long>(now - g_c44.window_ms));
    }
}

// Host-own pawn ASC only: the spec of `handle`, its ability (the CDO), ActiveCount, and the policies on the CDO and
// on the last instance. True when the caller should log the result too.
bool c62_before_try(const void* asc, int32_t handle) {
    if (!g_c50_test || g_client_session || asc == nullptr) {
        return false;
    }
    const void* const owner = read_ptr(asc, kComponentOwner);
    const uint8_t* const spec = c21_spec_for(asc, handle);
    const auto* const cdo = static_cast<const uint8_t*>(spec != nullptr ? read_ptr(spec, kSpecAbility) : nullptr);
    if (sprint_side(owner) != kSideHostOwn || (cdo != nullptr && !c62_is_switch(cdo)) || !c62_line()) {
        return false;
    }
    const auto* const inst = static_cast<const uint8_t*>(
        g_c62_instance_weak != 0 && g_weak_get != nullptr ? g_weak_get(&g_c62_instance_weak) : nullptr);
    char inv[320];
    c48_inventory_text(owner, inv, sizeof(inv));
    log::line("card062: host t=%llu before InternalTryActivateAbility(handle %d) on %s: spec %s, Ability %s %p (switch "
              "CDO %p), ActiveCount %d; CDO InstancingPolicy %d NetExecutionPolicy %d; instance %p InstancingPolicy "
              "%d NetExecutionPolicy %d; %s",
              static_cast<unsigned long long>(GetTickCount64()), handle, object_name(owner).text,
              spec != nullptr ? "found" : "NOT found", class_name(cdo).text, static_cast<const void*>(cdo), g_c57_cdo,
              spec != nullptr ? static_cast<int>(spec[kSpecActiveCount]) : -1,
              cdo != nullptr ? static_cast<int>(cdo[kC62InstancingPolicy]) : -1,
              cdo != nullptr ? static_cast<int>(cdo[kC57NetExecutionPolicy]) : -1, static_cast<const void*>(inst),
              inst != nullptr ? static_cast<int>(inst[kC62InstancingPolicy]) : -1,
              inst != nullptr ? static_cast<int>(inst[kC57NetExecutionPolicy]) : -1, inv);
    return true;
}

void c62_after_try(const void* asc, int32_t handle, bool ok) {
    const uint8_t* const spec = c21_spec_for(asc, handle);
    log::line("card062: host t=%llu InternalTryActivateAbility(handle %d) = %d, ActiveCount after %d",
              static_cast<unsigned long long>(GetTickCount64()), handle, ok ? 1 : 0,
              spec != nullptr ? static_cast<int>(spec[kSpecActiveCount]) : -1);
}

// Both sides, after InternalTryActivateAbility: an activation on a mech's ASC.
void c76_try(const void* asc, int32_t handle, const void* ability, bool ok, bool server) {
    const void* const mech = c76_mech(read_ptr(asc, kComponentOwner));
    const void* const cls = ability != nullptr ? read_ptr(ability, kObjectClass) : nullptr;
    if (mech == nullptr || !c76_ability_note(cls) || !c76_note(kC76Try)) {
        return;
    }
    log::line("card076: %s t=%llu mech %s InternalTryActivateAbility(handle %d)%s = %d, ability %s", side(), c76_now(),
              object_name(mech).text, handle, server ? " for a client request" : "", ok ? 1 : 0,
              class_name(ability).text);
}

} // namespace ubw::hooks
