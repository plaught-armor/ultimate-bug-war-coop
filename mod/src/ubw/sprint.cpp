// Sprint: GA_Sprint counters (card 019), the client's move input on the server copy (card 019), the client
// movement attribute re-init (card 019), the GA_Sprint lifecycle and speed path (cards 019, 022 part B), and
// the player pawn body audit (card 022). Log only, except the card 019 fixes.

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

void c22_arm_map();
void c22_sprint_dump(const void* ability, const void* avatar, uint32_t who, float value); // card 022 part B

const char* const kSideLogWhat[kSprintSides] = {"card019 host-own sprint", "card019 server-copy sprint",
                                               "card019 client-own sprint", "card019 other sprint"};

SprintTrace g_sprint{};

void reset_card019_map() {
    g_sprint = SprintTrace{};
    g_own_rows_left = 0;
}

uint32_t g_player_pawn_logs = 0; // card 014: PostInitializeComponents / PossessedBy lines, per map

// Per-map reset (client_diag_load_map_enter): the caps and the row state start over.
void reset_card013_map() {
    g_chain = MoveChain{};
    g_chain.controller = -1;
    g_chain.snap_pending = true;
    reset_actions();
    g_collision_logs = 0;
    g_pawn_collision_logs = 0;
    g_dropship_enter_exit_logs = 0;
    g_player_pawn_logs = 0;
    g_dumped = DumpedActions{};
    g_action_mods = ActionModLog{};
    reset_card016_map();
    reset_card019_map();
    reset_c490_map(); // card 490 step 1
    reset_card020_map();
}

CmcTickFn g_orig_cmc_tick = nullptr;

constexpr size_t kPawnMoveCompPawnOwner = 0x180; // UPawnMovementComponent::PawnOwner (AddInputVector+0x4)

constexpr uint32_t kMirrorLogs = 12;
uint32_t g_mirror_logs = 0;

// Client input = acceleration / MaxAcceleration (the inverse of ScaleInputAcceleration). A move on
// a dynamic movement base has a base-relative acceleration; MoveAutonomous stores it in world space.
void mirror_client_input(const uint8_t* cmc) {
    auto* const pawn = static_cast<uint8_t*>(read_ptr(cmc, kPawnMoveCompPawnOwner));
    if (pawn == nullptr || pawn[kActorLocalRole] != kRoleAuthority || pawn[kActorRemoteRole] != kRoleAutonomousProxy) {
        return; // not a remote client's pawn: its input comes from a local controller or AI
    }
    float max_accel = 0.0f;
    memcpy(&max_accel, cmc + kCmcMaxAcceleration, sizeof(max_accel));
    const Vec3 accel = read_vec3(cmc, kCmcAcceleration);
    if (!(max_accel > 0.0f) || vec_len(accel) == 0.0) {
        return; // no input this tick, same as the client
    }
    const double input[3] = {accel.x / max_accel, accel.y / max_accel, accel.z / max_accel};
    g_add_movement_input(pawn, input, false); // void; the effect is ControlInputVector, logged below
    if (note_event_cap(g_mirror_logs, kMirrorLogs, "card019 input mirror")) {
        const Vec3 pending = read_vec3(pawn, kPawnControlInputVector);
        log::line("card019: input mirror: pawn %p acceleration (%.1f, %.1f, %.1f) / MaxAcceleration %.1f -> "
                  "ControlInputVector (%.3f, %.3f, %.3f)",
                  static_cast<void*>(pawn), accel.x, accel.y, accel.z, static_cast<double>(max_accel), pending.x,
                  pending.y, pending.z);
    }
}

void detour_cmc_tick(void* cmc, float delta, int32_t tick_type, void* tick_function) {
    if (!g_client_session) {
        mirror_client_input(static_cast<const uint8_t*>(cmc));
        c101_cmc_tick(cmc);
    }
    if (g_c132_fx_log) { // card 527 item 2: an NPC's movement tick is timed
        c527_component_tick(kC527TickMovement, g_orig_cmc_tick, cmc, delta, tick_type, tick_function);
        return;
    }
    g_orig_cmc_tick(cmc, delta, tick_type, tick_function);
}

void detour_server_move_perform(void* cmc, const void* move_data) {
    g_orig_server_move_perform(cmc, move_data);
    if (g_client_session) {
        return;
    }
    ++g_srv.moves;
    host_row_step(static_cast<const uint8_t*>(cmc));
}

void log_rot_controller(uint32_t k, const uint8_t* pc) {
    const void* const player = read_ptr(pc, kPcPlayer);
    const void* const camera = read_ptr(pc, kPcCameraManager);
    const Vec3 control = read_vec3(pc, kControllerControlRotation);
    const int player_ok = (player != nullptr && read_ptr(player, kPlayerPlayerController) == pc) ? 1 : 0;
    log::line("rot[%u]: pc %p state %s, Pawn==AcknowledgedPawn %d, Player->PlayerController==pc %d, camera "
              "manager %p %s, IgnoreLookInput %u, ControlRotation P %.2f Y %.2f R %.2f",
              k, static_cast<const void*>(pc), fname_text(pc + kControllerStateName).text,
              read_ptr(pc, kControllerPawn) == read_ptr(pc, kPcAcknowledgedPawn) ? 1 : 0, player_ok, camera,
              class_name(camera).text, pc[kControllerIgnoreLook], control.x, control.y, control.z);
    log::line("rot[%u]: since last row: HandleInputLook %u, InputAxis %u, SetControlRotation %u from UpdateRotation "
              "+ %u other, ReplicateMoveToServer %u, ClientAdjustPosition %u",
              k, g_counts.look, g_counts.axis, g_counts.set_rotation_frame, g_counts.set_rotation_other,
              g_counts.replicate_move, g_counts.adjust);
    log_key_counts("rot", k);
}

void log_rot_pawn(uint32_t k, const uint8_t* pc) {
    const auto* const pawn = static_cast<const uint8_t*>(read_ptr(pc, kControllerPawn));
    if (pawn == nullptr) {
        log::line("rot[%u]: no pawn", k);
        return;
    }
    const void* const root = read_ptr(pawn, kActorRootComponent);
    const Vec3 loc = root != nullptr ? read_vec3(root, kComponentTranslation) : Vec3{};
    // Card 011: ACharacter, not ASTPlayerCharacter: card 010's gate failed for BP_PlayerCharacterMI_C.
    const auto* const cmc = is_a(pawn, g_character_class())
                                ? static_cast<const uint8_t*>(read_ptr(pawn, kCharacterMovement))
                                : nullptr;
    log::line("rot[%u]: pawn %p %s, LocalRole %u RemoteRole %u, Controller==pc %d, yaw %.2f, location (%.1f, %.1f, "
              "%.1f), CMC %p %s NetworkSmoothingMode %u",
              k, static_cast<const void*>(pawn), class_name(pawn).text, pawn[kActorLocalRole], pawn[kActorRemoteRole],
              read_ptr(pawn, kPawnController) == pc ? 1 : 0, root != nullptr ? component_yaw(root) : 0.0, loc.x,
              loc.y, loc.z, static_cast<const void*>(cmc), class_name(cmc).text,
              cmc != nullptr ? cmc[kCmcSmoothingMode] : 0xFFu);
    log_cmc("rot", k, cmc, pawn);
    // Card 011: card 009's sync_client_input_receiver sets PlayerCharacter only for an ASTPlayerCharacter pawn.
    log::line("rot[%u]: pawn is ACharacter %d, ASTPlayerCharacter %d", k, is_a(pawn, g_character_class()) ? 1 : 0,
              is_a(pawn, g_player_character_class()) ? 1 : 0);
    const Vec3 a = g_client_move.accel;
    log::line("rot[%u]: last ReplicateMoveToServer acceleration (%.1f, %.1f, %.1f) |a| %.1f, delta %.4f", k, a.x, a.y,
              a.z, sqrt(a.x * a.x + a.y * a.y + a.z * a.z), static_cast<double>(g_client_move.delta));
}

// kRotRows rows, one per kRotRowIntervalMs, after a client map load. Game thread (UGameEngine::Tick).
void rotation_report_step(void* engine) {
    const ULONGLONG now = GetTickCount64();
    if (now < g_rot_next_ms) {
        return;
    }
    g_rot_next_ms = now + kRotRowIntervalMs;
    --g_rot_rows_left;
    const uint32_t k = g_rot_row++;
    void* const world = g_get_game_world(engine);
    const auto* const pc = world != nullptr ? static_cast<const uint8_t*>(own_pc(world)) : nullptr;
    if (pc == nullptr) {
        log::line("rot[%u]: no game world or PlayerController", k);
    } else {
        log_rot_controller(k, pc);
        log_rot_pawn(k, pc);
    }
    log_move_diag(k);
    log_card013_row("rot", k);
    log_sprint_row("rot", k, kSideClientOwn); // card 019
    log::line("rot[%u]: own pawn collision re-enables since last row %u (map total %u), bActorEnableCollision %d", k,
              g_pawn_watch.reenables_row, g_pawn_watch.reenables_map, g_pawn_watch.bit);
    g_pawn_watch.reenables_row = 0;
    g_counts = DiagCounts{};
    reset_move_diag_row();
}

// Card 014: the rows wait for the input window (open_input_window); a LoadMap stops any running rows.
void stop_rows() {
    g_rot_rows_left = 0;
    g_rot_row = 0;
    g_host_rows_left = 0;
    g_host_row = 0;
    g_counts = DiagCounts{};
}

void client_diag_load_map_enter(const void* pending) {
    c42_release_pools(); // card 140: before the engine collects the old world
    if (g_client_session && pending == nullptr) {
        c137_change_dump("session end"); // card 149: the last map's play window, before the flag hides it
    }
    set_client_session(pending != nullptr); // a pending net game = joining a server
    if (!g_client_session) {
        c350_client_session_end(); // card 350: no stale frontend options into a later join
        c132_ledger_session_end(); // card 385
    }
    g_snapshot_armed = false;
    g_game_only_pending = false;
    stop_rows();
    g_input_window_armed = false;
    g_pawn_watch_armed = false;
    g_pawn_watch = kPawnWatchReset;
    // Card 012: per-map caps, so a mission's AddMappingContext lines are not used up by the front end.
    g_context_logs = 0;
    g_mapping_lines = 0;
    reset_move_diag_map();
    reset_card013_map();
    g_dropship_logs = 0;
    g_pause_logs = 0;
    g_c65_logs = 0;
    g_c65_activate_logs = 0;
    g_c65_shunt_logs = 0;
    g_c68_logs = 0;
    c69_reset_map();
    c73_reset_map();
    c158_reset_map();
    c169_reset_map();
    if (g_c63_on) {
        c67_reset_map();
    }
    g_dropship_watch_armed = false;
    g_auto_dropship_armed = false;
    reset_card017_map(false);
}

// Card 009: the snapshot now runs after every successful LoadMap, so the host's local PC can be
// compared with the client's.
void client_diag_load_map_return(bool ok, const void* pending) {
    if (!ok) {
        set_client_session(false);
        c350_client_session_end();
        c132_ledger_session_end();
        sync_map_failed(); // card 332: no arm follows a failed load
        return;
    }
    if (pending == nullptr) {
        set_client_session(false);
        c350_client_session_end();
        c132_ledger_session_end();
    }
    g_game_only_pending = g_client_game_only && g_client_session;
    g_snapshot_armed = true;
    g_snapshot_start_ms = GetTickCount64();
    stop_rows();
    g_input_window_armed = true; // card 014: the rows start on the first input
    g_pawn_watch = kPawnWatchReset;
    g_pawn_watch_armed = g_client_session;
    g_dropship_watch = DropshipWatch{};
    g_dropship_watch.start_ms = GetTickCount64();
    g_dropship_watch_armed = !g_client_session;
    g_auto = AutoDropship{0, 0, 0};
    g_auto_dropship_armed = g_auto_dropship && !g_client_session;
    g_c21_grant_armed = g_grant_weapon.row[0] != '\0';
    c25_arm_map();
    c27_arm_map();
    c29_arm_map();
    c22_arm_map();
    c28_arm_map();
    fx_watch_map("the map change"); // card 130
    c23_arm_map();
    c37_arm_map();
    c38_arm_map();
    c41_arm_map();
    c30_arm_map();
    c32_arm_map();
    c453_arm_map(); // card 453 step 1 diag
    sync_map_arm(); // card 326: objective, dropship, effects, script and player arms (card 332 teardown ends)
    c50_arm_map(); // card 050
    reset_card017_map(true);
    log::line("diag: %s map loaded; input snapshot in %llu ms; %s rows wait for the first non-mouse key%s",
              g_client_session ? "client" : "host/standalone", static_cast<unsigned long long>(kSnapshotDelayMs),
              g_client_session ? "rot" : "srv", g_client_session ? "" : " or remote client move");
}

void log_pc_snapshot(const uint8_t* pc) {
    if (pc == nullptr) {
        log::line("snapshot: world has no PlayerController");
        return;
    }
    const void* const pawn = read_ptr(pc, kControllerPawn);
    log::line("snapshot: pc=%p %s, net client %d, local player %p, Pawn %p %s", static_cast<const void*>(pc),
              class_name(pc).text, is_net_client(pc) ? 1 : 0, g_get_local_player(pc), pawn, class_name(pawn).text);
    log::line("snapshot: IgnoreMoveInput %u, IgnoreLookInput %u, bShowMouseCursor %u", pc[kControllerIgnoreMove],
              pc[kControllerIgnoreLook], cursor_bit(pc));
    if (is_a(pc, g_st_pc_class())) {
        log::line("snapshot: input receiver %p, player character %p", read_ptr(pc, kPcInputReceiver),
                  read_ptr(pc, kPcPlayerCharacter));
    }
}

constexpr size_t kAppliedContextPriority = 0x8;

constexpr size_t kPcPlayerInput = 0x420; // APlayerController::PlayerInput (see kTickLoadPlayerInput)

// Mappings of `context` keyed Mouse2D, MouseX or MouseY; -1 when the array is implausible.
int32_t context_mouse_axis_mappings(const void* context) {
    const auto* const data = static_cast<const uint8_t*>(read_ptr(context, kContextMappings));
    int32_t num = 0;
    memcpy(&num, static_cast<const uint8_t*>(context) + kContextMappings + sizeof(void*), sizeof(num));
    if (num < 0 || num > kMaxMappingsScanned || (num > 0 && data == nullptr)) {
        return -1;
    }
    int32_t found = 0;
    for (int32_t i = 0; i < num; ++i) {
        const ObjectName key = fname_text(data + static_cast<size_t>(i) * kMappingBytes + kMappingKeyName);
        found += (strcmp(key.text, "Mouse2D") == 0 || strcmp(key.text, "MouseX") == 0 ||
                  strcmp(key.text, "MouseY") == 0) ? 1 : 0;
    }
    return found;
}

void log_applied_contexts(const uint8_t* pc) {
    const void* const local_player = g_get_local_player(pc);
    auto* const subsystem = local_player != nullptr ? static_cast<uint8_t*>(g_get_enhanced_input(local_player)) : nullptr;
    if (subsystem == nullptr) {
        log::line("snapshot: %s pc=%p has no EnhancedInput subsystem; applied contexts not read", session_side(),
                  static_cast<const void*>(pc));
        return;
    }
    const void* const iface = subsystem + kSubsystemInputInterface;
    const auto get = reinterpret_cast<GetPlayerInputFn>((*static_cast<void* const* const*>(iface))[kIfaceGetPlayerInput]);
    const auto* const input = static_cast<const uint8_t*>(get(iface));
    SparseView v{};
    if (input == nullptr || !sparse_view(input + kPlayerInputAppliedContexts, kMaxAppliedContexts, v)) {
        log::line("snapshot: %s PlayerInput %p: applied contexts not read", session_side(),
                  static_cast<const void*>(input));
        return;
    }
    log::line("snapshot: %s PlayerInput %p %s (pc PlayerInput %p), applied context slots %d", session_side(),
              static_cast<const void*>(input), class_name(input).text, read_ptr(pc, kPcPlayerInput), v.count);
    for (int32_t i = 0; i < v.count; ++i) {
        if (!sparse_allocated(v, i)) {
            continue;
        }
        const uint8_t* const e = v.data + static_cast<size_t>(i) * kAppliedContextBytes;
        const void* const context = read_ptr(e, 0);
        int32_t priority = 0;
        memcpy(&priority, e + kAppliedContextPriority, sizeof(priority));
        log::line("snapshot: %s applied context %p %s priority %d, Mouse2D/MouseX/MouseY mappings %d", session_side(),
                  context, object_name(context).text, priority,
                  context != nullptr ? context_mouse_axis_mappings(context) : -1);
    }
}

// One snapshot per client map load, kSnapshotDelayMs after LoadMap returned. Game thread.
void client_snapshot_step(void* engine) {
    if (GetTickCount64() - g_snapshot_start_ms < kSnapshotDelayMs) {
        return;
    }
    g_snapshot_armed = false;
    void* const world = g_get_game_world(engine);
    if (world == nullptr) {
        log::line("snapshot: no game world");
        return;
    }
    const auto* const pc = static_cast<const uint8_t*>(own_pc(world));
    log_pc_snapshot(pc);
    if (pc != nullptr) {
        log_applied_contexts(pc);
    }
    const auto* const viewport = static_cast<const uint8_t*>(g_world_game_viewport(world));
    if (viewport == nullptr) {
        log::line("snapshot: no game viewport client");
        return;
    }
    log::line("snapshot: viewport client %p MouseCaptureMode %u, MouseLockMode %u", static_cast<const void*>(viewport),
              viewport[kViewportCaptureMode], viewport[kViewportLockMode]);
}

MovementLayout movement_layout(const void* set_class) {
    MovementLayout r{};
    const void* field = read_ptr(set_class, kStructChildProperties);
    for (int i = 0; i < kC18MaxProps && field != nullptr; ++i, field = read_ptr(field, kFieldNext)) {
        ++r.props;
        r.net_props += (read_u64(field, kPropertyFlags) & kCpfNet) != 0 ? 1 : 0;
        const ObjectName name = fname_text(static_cast<const uint8_t*>(field) + kFieldName);
        if (strcmp(name.text, "WalkSpeed") == 0 && read_i32(field, kPropertyOffset) == kWalkSpeedOffset) {
            r.walk_offset_ok = true;
        }
    }
    return r;
}

// The ASC's spawned movement set, or null.
const void* find_movement_set(const void* asc) {
    const int32_t num = read_i32(asc, kAscSpawnedAttributesNum);
    void* const* const data = static_cast<void* const*>(read_ptr(asc, kAscSpawnedAttributes));
    const int32_t n = (data == nullptr || num <= 0) ? 0 : (num < kC17SetsShown ? num : kC17SetsShown);
    for (int32_t i = 0; i < n; ++i) {
        if (data[i] != nullptr && is_a(data[i], g_movement_set_class())) {
            return data[i];
        }
    }
    return nullptr;
}

SprintActivateFn g_orig_sprint_activate = nullptr;
SprintEndFn g_orig_sprint_end = nullptr;
CancelAbilityFn g_orig_cancel_ability = nullptr;
PostAttributeChangeFn g_orig_movement_post_change = nullptr;
StaticClassFn g_sprint_ability_class = nullptr; // USTSprintAbility

constexpr size_t kSprintEffectClass = 0x3B0;
constexpr size_t kSprintSecondTag = 0x3C0;
constexpr uint32_t kSprintTagRva = 0x943A8C8;
constexpr uint32_t kSprintLines = 40;     // per who per map

uint32_t g_sprint_change_depth = 0; // inside HandleSprintChange: attribute changes there are its effect's

InvokeFn g_orig_invoke = nullptr;
constexpr size_t kFunctionReturnOffset = 0xB8;     // UFunction::ReturnValueOffset (uint16, 0xFFFF = none)
constexpr uint32_t kSprintBpLines = 60;            // per side per map
constexpr uint32_t kReturnBytesShown = 24;         // an FVector of doubles
uint32_t g_sprint_activate_depth = 0;
DWORD g_sprint_activate_thread = 0;
uint32_t g_sprint_activate_who = 0;

// Card 022 part B: the GA_Sprint blueprint's native calls before each HandleSprintChange, to find the input
// that picks its value. Calls are recognised by the class FName of the frame's object (no game pointer kept);
// the FName is taken from the ability on activation and on HandleSprintChange.
constexpr uint32_t kC22Ring = 16;
constexpr uint32_t kC22RetBytes = 16;
constexpr uint8_t kC22NoReturn = 0xFF;
struct C22Call {
    uint64_t node;     // FName of the blueprint function making the call
    uint64_t function; // FName of the called UFunction
    uint64_t outer;    // FName of the UFunction's class
    uint8_t ret[kC22RetBytes];
    uint8_t ret_len;   // kC22NoReturn when the function has no return value
};
struct C22Ring {
    uint64_t sprint_class; // FName of the GA_Sprint class; 0 until the first activation, reset per map
    C22Call call[kC22Ring];
    uint32_t total; // recorded since the last dump; the ring keeps the last kC22Ring
};
C22Ring g_c22_ring{};

bool c22_sprint_frame(const void* frame) {
    const void* const object = g_c22_ring.sprint_class != 0 ? read_ptr(frame, kFrameObject) : nullptr;
    const void* const cls = object != nullptr ? read_ptr(object, kObjectClass) : nullptr;
    return cls != nullptr && read_u64(cls, kObjectName) == g_c22_ring.sprint_class;
}

void c22_note_class(const void* ability) {
    g_c22_ring.sprint_class = read_u64(read_ptr(ability, kObjectClass), kObjectName);
}


float max_walk_speed(const void* actor) {
    if (actor == nullptr || !is_a(actor, g_character_class())) {
        return -1.0f;
    }
    const void* const cmc = read_ptr(actor, kCharacterMovement);
    return cmc != nullptr ? read_f32(cmc, kCmcMaxWalkSpeed) : -1.0f;
}

bool sprint_line(uint32_t who) {
    return note_event_cap(g_sprint.lines[who], kSprintLines, kSideLogWhat[who]);
}

void detour_sprint_activate(void* ability, int32_t handle, const void* actor_info, const void* activation_info,
                            const void* event_data) {
    void* const avatar = avatar_of(actor_info);
    const uint32_t who = sprint_side(avatar);
    ++g_sprint.row[who].activates;
    const float before = max_walk_speed(avatar);
    const uint32_t outer_who = g_sprint_activate_who;
    c22_note_class(ability);
    g_sprint_activate_who = who;
    g_sprint_activate_thread = GetCurrentThreadId();
    ++g_sprint_activate_depth;
    g_orig_sprint_activate(ability, handle, actor_info, activation_info, event_data);
    --g_sprint_activate_depth;
    g_sprint_activate_who = outer_who;
    if (sprint_line(who)) {
        char chain[96];
        caller_chain(chain, sizeof(chain));
        log::line("card019: %s %s GA_Sprint ActivateAbility(handle %d, ActivationMode %u) on %s: effect handle %lld, "
                  "MaxWalkSpeed %.1f -> %.1f, caller exe+0x%llX, chain %s",
                  side(), kSprintSideText[who], handle,
                  activation_info != nullptr ? static_cast<const uint8_t*>(activation_info)[0] : 0xFFu,
                  class_name(avatar).text, static_cast<long long>(read_i32(ability, kSprintEffectHandle)),
                  static_cast<double>(before), static_cast<double>(max_walk_speed(avatar)),
                  exe_rva(_ReturnAddress()), chain);
    }
}

void detour_sprint_end(void* ability, int32_t handle, const void* actor_info, const void* activation_info,
                       bool replicate, bool was_cancelled) {
    void* const avatar = avatar_of(actor_info);
    const uint32_t who = sprint_side(avatar);
    ++g_sprint.row[who].ends;
    g_sprint.row[who].cancelled_ends += was_cancelled ? 1u : 0u;
    const long long effect_before = static_cast<long long>(read_i32(ability, kSprintEffectHandle));
    const float before = max_walk_speed(avatar);
    g_orig_sprint_end(ability, handle, actor_info, activation_info, replicate, was_cancelled);
    if (sprint_line(who)) {
        char chain[96];
        caller_chain(chain, sizeof(chain));
        log::line("card019: %s %s GA_Sprint EndAbility(handle %d, bReplicate %d, bWasCancelled %d) on %s: effect "
                  "handle %lld -> %lld, MaxWalkSpeed %.1f -> %.1f, caller exe+0x%llX, chain %s",
                  side(), kSprintSideText[who], handle, replicate ? 1 : 0, was_cancelled ? 1 : 0,
                  class_name(avatar).text, effect_before, static_cast<long long>(read_i32(ability, kSprintEffectHandle)),
                  static_cast<double>(before), static_cast<double>(max_walk_speed(avatar)),
                  exe_rva(_ReturnAddress()), chain);
    }
}

void detour_cancel_ability(void* ability, int32_t handle, const void* actor_info, const void* activation_info,
                           bool replicate) {
    const bool sprint = is_a(ability, g_sprint_ability_class());
    void* const avatar = sprint ? avatar_of(actor_info) : nullptr;
    const uint32_t who = sprint_side(avatar);
    g_sprint.row[who].cancels += sprint ? 1u : 0u;
    if (!sprint) {
        c62_note_cancel(ability, handle, actor_info, replicate);
    }
    g_orig_cancel_ability(ability, handle, actor_info, activation_info, replicate);
    if (sprint && sprint_line(who)) {
        char chain[96];
        caller_chain(chain, sizeof(chain));
        log::line("card019: %s %s GA_Sprint CancelAbility(handle %d, bReplicate %d) on %s, caller exe+0x%llX, chain %s",
                  side(), kSprintSideText[who], handle, replicate ? 1 : 0, class_name(avatar).text,
                  exe_rva(_ReturnAddress()), chain);
    }
}

void detour_sprint_change(void* ability, float value) {
    const void* const info = read_ptr(ability, kAbilityActorInfo);
    void* const avatar = avatar_of(info);
    const uint32_t who = sprint_side(avatar);
    ++g_sprint.row[who].changes;
    g_sprint.row[who].last_value = value;
    c22_note_class(ability);
    c22_sprint_dump(ability, avatar, who, value);
    const long long effect_before = static_cast<long long>(read_i32(ability, kSprintEffectHandle));
    const float before = max_walk_speed(avatar);
    const bool logged = sprint_line(who);
    if (logged) {
        log::line("card019: %s %s HandleSprintChange(%.3f) on %s: GE %s, set-by-caller %s = %.3f and %s = 1.000, "
                  "caller exe+0x%llX",
                  side(), kSprintSideText[who], static_cast<double>(value), class_name(avatar).text,
                  object_name(read_ptr(ability, kSprintEffectClass)).text,
                  fname_text(g_exe_base + kSprintTagRva).text, static_cast<double>(value),
                  fname_text(static_cast<const uint8_t*>(ability) + kSprintSecondTag).text, exe_rva(_ReturnAddress()));
    }
    ++g_sprint_change_depth;
    g_orig_sprint_change(ability, value);
    --g_sprint_change_depth;
    if (logged) {
        log::line("card019: %s %s HandleSprintChange(%.3f) done: effect handle %lld -> %lld, MaxWalkSpeed %.1f -> %.1f",
                  side(), kSprintSideText[who], static_cast<double>(value), effect_before,
                  static_cast<long long>(read_i32(ability, kSprintEffectHandle)), static_cast<double>(before),
                  static_cast<double>(max_walk_speed(avatar)));
    }
}

// The return value bytes of a native call (none when the function has no return value).
void return_bytes(const void* function, const void* result, char* out, size_t cap) {
    const uint16_t ret = static_cast<uint16_t>(read_i32(function, kFunctionReturnOffset) & 0xFFFF);
    const uint16_t parms = static_cast<uint16_t>(read_i32(function, kFunctionParmsSize) & 0xFFFF);
    out[0] = '\0';
    if (result == nullptr || ret == 0xFFFF || parms <= ret) {
        snprintf(out, cap, "none");
        return;
    }
    const uint32_t size = parms - ret < kReturnBytesShown ? parms - ret : kReturnBytesShown;
    size_t used = 0;
    for (uint32_t i = 0; i < size && used < cap; ++i) {
        const int w = snprintf(out + used, cap - used, "%02X", static_cast<const uint8_t*>(result)[i]);
        if (w < 0) {
            return;
        }
        used += static_cast<size_t>(w);
    }
}

// One GA_Sprint blueprint native call into the card 022 ring, with its first return bytes.
void c22_record(const void* function, const void* node, const void* result) {
    C22Call& c = g_c22_ring.call[g_c22_ring.total % kC22Ring];
    ++g_c22_ring.total;
    c.node = node != nullptr ? read_u64(node, kObjectName) : 0;
    c.function = read_u64(function, kObjectName);
    const void* const outer = read_ptr(function, kObjectOuter);
    c.outer = outer != nullptr ? read_u64(outer, kObjectName) : 0;
    const uint16_t ret = static_cast<uint16_t>(read_i32(function, kFunctionReturnOffset) & 0xFFFF);
    const uint16_t parms = static_cast<uint16_t>(read_i32(function, kFunctionParmsSize) & 0xFFFF);
    if (result == nullptr || ret == 0xFFFF || parms <= ret) {
        c.ret_len = kC22NoReturn;
        return;
    }
    c.ret_len = static_cast<uint8_t>(parms - ret < kC22RetBytes ? parms - ret : kC22RetBytes);
    memcpy(c.ret, result, c.ret_len);
}

void detour_invoke(void* function, void* object, void* frame, void* result) {
    const bool sprint_frame = c22_sprint_frame(frame);
    const bool activation = g_sprint_activate_depth != 0 && GetCurrentThreadId() == g_sprint_activate_thread;
    if (!sprint_frame && !activation) {
        g_orig_invoke(function, object, frame, result);
        return;
    }
    const void* const node = read_ptr(frame, kFrameNode); // the blueprint function making the call
    g_orig_invoke(function, object, frame, result);
    if (sprint_frame) {
        c22_record(function, node, result);
    }
    const uint32_t who = g_sprint_activate_who;
    if (!activation || !note_event_cap(g_sprint.bp_lines[who], kSprintBpLines, "card019 sprint blueprint call")) {
        return;
    }
    char bytes[2 * kReturnBytesShown + 8];
    return_bytes(function, result, bytes, sizeof(bytes));
    log::line("card019: %s %s GA_Sprint activation: %s calls %s.%s on %s, return 0x%s",
              side(), kSprintSideText[who], object_name(node).text, object_name(read_ptr(function, kObjectOuter)).text,
              object_name(function).text, class_name(object).text, bytes);
}

// The movement set's own properties (name, offset, CPF_Net, base/current), once per who per map.
void log_movement_props(const void* set, uint32_t who) {
    if (g_sprint.props_logged[who]) {
        return;
    }
    g_sprint.props_logged[who] = true;
    const void* field = read_ptr(read_ptr(set, kObjectClass), kStructChildProperties);
    for (int i = 0; i < kC18MaxProps && field != nullptr; ++i, field = read_ptr(field, kFieldNext)) {
        const uint64_t flags = read_u64(field, kPropertyFlags);
        const int32_t off = read_i32(field, kPropertyOffset);
        const bool in_set = off >= 0 && off < 0x400; // a FGameplayAttributeData slot in this small set
        log::line("card019: %s %s movement set %s.%s: offset 0x%X flags 0x%016llX CPF_Net %d, base %.3f current %.3f",
                  side(), kSprintSideText[who], class_name(set).text,
                  fname_text(static_cast<const uint8_t*>(field) + kFieldName).text, off,
                  static_cast<unsigned long long>(flags), (flags & kCpfNet) != 0 ? 1 : 0,
                  in_set ? read_f32(set, static_cast<size_t>(off) + kAttrDataBase) : 0.0f,
                  in_set ? read_f32(set, static_cast<size_t>(off) + kAttrDataCurrent) : 0.0f);
    }
}

void detour_movement_post_change(void* set, const void* attribute, float old_value, float new_value) {
    const void* const owner = read_ptr(set, kObjectOuter);
    const uint32_t who = sprint_side(owner);
    const ObjectName name = attribute_name(attribute);
    g_orig_movement_post_change(set, attribute, old_value, new_value);
    if (strcmp(name.text, "WalkSpeed") == 0) {
        ++g_sprint.row[who].walk_changes;
        g_sprint.row[who].last_walk = new_value;
    }
    log_movement_props(set, who);
    if (sprint_line(who)) {
        log::line("card019: %s %s USTAttributeSet_Movement::PostAttributeChange(%s) %.3f -> %.3f on %s%s: "
                  "MaxWalkSpeed now %.1f, caller exe+0x%llX",
                  side(), kSprintSideText[who], name.text, static_cast<double>(old_value),
                  static_cast<double>(new_value), class_name(owner).text,
                  g_sprint_change_depth != 0 ? " (inside HandleSprintChange: the GE's attribute)" : "",
                  static_cast<double>(max_walk_speed(owner)), exe_rva(_ReturnAddress()));
    }
}

// Card 019 host baseline rows: the host's own pawn CMC and its GA_Sprint counters, every 500 ms.
void own_rows_step(void* engine) {
    const ULONGLONG now = GetTickCount64();
    if (now < g_sprint.own_next_ms) {
        return;
    }
    g_sprint.own_next_ms = now + kOwnRowIntervalMs;
    --g_own_rows_left;
    const uint32_t k = g_sprint.own_row++;
    void* const world = g_get_game_world(engine);
    const void* const pc = world != nullptr ? own_pc(world) : nullptr;
    const void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    if (pawn == nullptr || !is_a(pawn, g_character_class())) {
        log::line("own[%u]: no local on-foot character (PC %p, pawn %p)", k, pc, pawn);
    } else {
        log_cmc("own", k, static_cast<const uint8_t*>(read_ptr(pawn, kCharacterMovement)), pawn);
    }
    log_sprint_row("own", k, kSideHostOwn);
}

// ---- Card 022: player pawn body audit (log only) and the GA_Sprint value's inputs ----
// Once per pawn per map, on both sides, when the pawn has been on foot for kC22DelayMs: roles, camera type,
// Mesh / FirstPersonMesh / ThirdPersonRenderer (by reflected name, offsets checked against the static ones),
// and every component the pawn owns, flagging classes named "Sprite". Pawns are keyed by object index.
constexpr uint32_t kC22MaxPawns = 8;
constexpr ULONGLONG kC22StepMs = 1000;
constexpr ULONGLONG kC22DelayMs = 5000;
constexpr int kC22MaxChildren = 32;     // per AttachChildren array
constexpr uint32_t kC22MaxComponents = 48; // component lines per pawn

constexpr int kC22MaxOwnSupers = 3;
constexpr uint32_t kC22SprintDumps = 6; // per side per map
constexpr int kC22SprintSupers = 2;     // GA_Sprint_C and USTSprintAbility
constexpr size_t kCharacterCameraType = 0x9E0; // ASTPlayerCharacter::CameraType (ChangeCameraType)

struct C22Slot {
    int32_t index;   // UObject index of the pawn
    ULONGLONG due_ms; // 0 until first seen on foot
    bool done;
};
struct C22State {
    C22Slot slot[kC22MaxPawns];
    uint32_t count;
    ULONGLONG next_ms;
    uint32_t dumps[kSprintSides];
};
C22State g_c22{};

struct C22Body {
    const char* name; // reflected property of ASTPlayerCharacter
    size_t expected;  // offset from the static work
};
constexpr C22Body kC22Bodies[] = {{"Mesh", 0x328}, {"FirstPersonMesh", 0x848}, {"ThirdPersonRenderer", 0x830}};

// The text of reflected leaf property `name` of `object`; "-" when the class has no such property.
void c22_prop_text(const void* object, const char* name, char* buf, size_t cap) {
    const uint8_t* const prop = prop_find_prop(read_ptr(object, kObjectClass), name);
    if (prop == nullptr) {
        snprintf(buf, cap, "-");
        return;
    }
    ObjectName kind{};
    const PropKind k = prop_kind(prop, kind);
    if (!prop_is_leaf(k)) {
        snprintf(buf, cap, "<%s>", kind.text);
        return;
    }
    const auto* const addr = static_cast<const uint8_t*>(object) + read_i32(prop, kPropertyOffset);
    c27_text(k, kind.text, prop, addr, buf, cap);
}

bool c22_is_base_class(const void* cls) {
    const ObjectName n = object_name(cls);
    return strcmp(n.text, "SceneComponent") == 0 || strcmp(n.text, "ActorComponent") == 0 ||
           strcmp(n.text, "PrimitiveComponent") == 0 || strcmp(n.text, "Actor") == 0 ||
           strcmp(n.text, "Object") == 0;
}

// The pairs of a map of leaf keys and values (a multiplier table), one line each.
void c22_log_map(const char* tag, const uint8_t* field, const uint8_t* addr) {
    C27Sparse sp{};
    const auto* const key = static_cast<const uint8_t*>(read_ptr(field, kMapPropKey));
    const auto* const value = static_cast<const uint8_t*>(read_ptr(field, kMapPropValue));
    ObjectName key_kind{};
    ObjectName value_kind{};
    const PropKind kk = key != nullptr ? prop_kind(key, key_kind) : PropKind::Other;
    const PropKind vk = value != nullptr ? prop_kind(value, value_kind) : PropKind::Other;
    if (!c27_map_ok(field) || !c27_sparse(addr, sp) || !prop_is_leaf(kk) || !prop_is_leaf(vk)) {
        log::line("card022:       %s map not shown (layout check failed or non-leaf key/value)", tag);
        return;
    }
    const int32_t stride = read_i32(field, kMapSparseSize);
    const int32_t value_offset = read_i32(field, kMapValueOffset);
    for (int32_t i = 0; i < sp.slots && i < kC27MaxElements; ++i) {
        if (!c27_bit(sp, i)) {
            continue;
        }
        const uint8_t* const pair = sp.data + static_cast<ptrdiff_t>(i) * stride;
        char k[kC27TextChars];
        char v[kC27TextChars];
        c27_text(kk, key_kind.text, key, pair, k, sizeof(k));
        c27_text(vk, value_kind.text, value, pair + value_offset, v, sizeof(v));
        log::line("card022:       %s[%s] = %s (%s -> %s)", tag, k, v, key_kind.text, value_kind.text);
    }
}

// One of Mesh / FirstPersonMesh / ThirdPersonRenderer: offset check, value, class, and its visibility props.
void c22_log_body(const uint8_t* pawn, const C22Body& b) {
    const uint8_t* const prop = prop_find_prop(read_ptr(pawn, kObjectClass), b.name);
    if (prop == nullptr) {
        log::line("card022:   .%s: no such reflected property (static offset +0x%zX holds %s)", b.name, b.expected,
                  object_name(read_ptr(pawn, b.expected)).text);
        return;
    }
    const int32_t off = read_i32(prop, kPropertyOffset);
    const void* const value = read_ptr(pawn, static_cast<size_t>(off));
    log::line("card022:   .%s +0x%X (static +0x%zX, %s; PropertyClass %s) = %s (%s), outer %s", b.name, off,
              b.expected, static_cast<size_t>(off) == b.expected ? "match" : "MISMATCH",
              object_name(read_ptr(prop, kObjectPropClass)).text, object_name(value).text,
              value != nullptr ? class_name(value).text : "-",
              value != nullptr ? object_name(read_ptr(value, kObjectOuter)).text : "-");
    if (value == nullptr) {
        return;
    }
    char text[kC22LineChars];
    c22_comp_text(value, text, sizeof(text));
    log::line("card022:     %s", text);
    if (strcmp(b.name, "ThirdPersonRenderer") == 0) {
        c22_own_props("ThirdPersonRenderer", value, kC22MaxOwnSupers);
    }
}

void c22_log_component(const char* via, const char* name, const void* component, uint32_t& lines) {
    if (!note_event_cap(lines, kC22MaxComponents, "card022 component")) {
        return;
    }
    const ObjectName cls = class_name(component);
    log::line("card022:   component %s %s: %s (%s)%s", via, name, object_name(component).text, cls.text,
              strstr(cls.text, "Sprite") != nullptr ? " SPRITE" : "");
}

// Object properties of the pawn whose value the pawn owns (outer == pawn): its components and subobjects.
void c22_owned_props(const uint8_t* pawn, uint32_t& lines) {
    const void* s = read_ptr(pawn, kObjectClass);
    for (int depth = 0; depth < kPropMaxSupers && s != nullptr; ++depth, s = read_ptr(s, kStructSuper)) {
        const void* f = read_ptr(s, kStructChildProperties);
        for (int i = 0; i < kPropMaxFields && f != nullptr; ++i, f = read_ptr(f, kFieldNext)) {
            ObjectName kind{};
            if (prop_kind(f, kind) != PropKind::Object || strcmp(kind.text, "ObjectProperty") != 0) {
                continue; // class and interface properties hold no owned subobject
            }
            const void* const value = read_ptr(pawn, static_cast<size_t>(read_i32(f, kPropertyOffset)));
            if (value != nullptr && read_ptr(value, kObjectOuter) == pawn) {
                const ObjectName name = fname_text(static_cast<const uint8_t*>(f) + kFieldName);
                c22_log_component("property", name.text, value, lines);
            }
        }
    }
}

// The scene components attached under RootComponent, two levels (reflected AttachChildren).
void c22_attach_tree(const uint8_t* pawn, uint32_t& lines) {
    const void* const root = read_ptr(pawn, kActorRootComponent);
    const void* level[kC22MaxChildren] = {};
    int32_t n = 0;
    const uint8_t* const prop = root != nullptr ? prop_find_prop(read_ptr(root, kObjectClass), "AttachChildren") : nullptr;
    if (prop == nullptr) {
        log::line("card022:   no RootComponent.AttachChildren (root %s)", object_name(root).text);
        return;
    }
    const size_t off = static_cast<size_t>(read_i32(prop, kPropertyOffset));
    c22_log_component("root", "RootComponent", root, lines);
    const auto* const kids = static_cast<void* const*>(read_ptr(root, off));
    const int32_t num = read_i32(root, off + kArrayNum);
    for (int32_t i = 0; kids != nullptr && i < num && i < kC22MaxChildren; ++i) {
        level[n++] = kids[i];
        c22_log_component("child of root", object_name(root).text, kids[i], lines);
    }
    for (int32_t j = 0; j < n; ++j) {
        const void* const parent = level[j];
        const auto* const sub = parent != nullptr ? static_cast<void* const*>(read_ptr(parent, off)) : nullptr;
        const int32_t sub_num = parent != nullptr ? read_i32(parent, off + kArrayNum) : 0;
        for (int32_t k = 0; sub != nullptr && k < sub_num && k < kC22MaxChildren; ++k) {
            c22_log_component("grandchild via", object_name(parent).text, sub[k], lines);
        }
    }
}

void c22_audit(const uint8_t* pawn) {
    const void* const controller = read_ptr(pawn, kPawnController);
    const bool local = controller != nullptr && is_a(controller, g_st_pc_class()) && is_local_pc(controller);
    const uint8_t* const cam_prop = prop_find_prop(read_ptr(pawn, kObjectClass), "CameraType");
    const int32_t cam_off = cam_prop != nullptr ? read_i32(cam_prop, kPropertyOffset) : -1;
    log::line("card022: %s audit %s (%s) index %d, %s: LocalRole %u RemoteRole %u, locally controlled %d "
              "(controller %s), CameraType reflected +0x%X = %d, byte +0x%zX = %u, MaxWalkSpeed %.1f",
              side(), object_name(pawn).text, class_name(pawn).text, object_index(pawn),
              kSprintSideText[sprint_side(pawn)], pawn[kActorLocalRole], pawn[kActorRemoteRole], local ? 1 : 0,
              object_name(controller).text, cam_off, cam_off >= 0 ? pawn[cam_off] : -1, kCharacterCameraType,
              pawn[kCharacterCameraType], static_cast<double>(max_walk_speed(pawn)));
    for (const C22Body& b : kC22Bodies) {
        c22_log_body(pawn, b);
    }
    uint32_t lines = 0;
    c22_owned_props(pawn, lines);
    c22_attach_tree(pawn, lines);
    log::line("card022: %s audit %s done", side(), object_name(pawn).text);
}

// The slot of `pawn`, added when new (card 275: a gone pawn's slot when the table is full); null when full.
C22Slot* c22_slot(const void* pawn) {
    static_assert(kC22MaxPawns <= kC28MaxPawns); // card 275: the weak handles are sized for kC28MaxPawns
    return c275_slot(C275Table::Sprint, g_c22.slot, g_c22.count, kC22MaxPawns, pawn, C22Slot{object_index(pawn), 0, false});
}

void c22_visit(const uint8_t* pawn, ULONGLONG now) {
    C22Slot* const s = c22_slot(pawn);
    if (s == nullptr || s->done || holding_dropship(pawn) != nullptr) {
        return;
    }
    if (s->due_ms == 0) {
        s->due_ms = now + kC22DelayMs;
        return;
    }
    if (now >= s->due_ms) {
        s->done = true;
        c22_audit(pawn);
    }
}

void c22_step(void* engine) {
    const ULONGLONG now = GetTickCount64();
    if (now < g_c22.next_ms) {
        return;
    }
    g_c22.next_ms = now + kC22StepMs;
    // The cached player list (card 368), not a walk of every actor of the map: the walk took up to 1.33 ms on the
    // client and 1.93 ms on the host once a second (run c325b101a).
    static void* players[kC23Max]; // game thread only
    const void* const world = g_get_game_world(engine);
    const uint32_t count = c368_actors(g_c368_players, world, g_player_character_class(), players);
    for (uint32_t i = 0; i < count; ++i) {
        if (players[i] != nullptr) {
            c22_visit(static_cast<const uint8_t*>(players[i]), now);
        }
    }
}

void c22_arm_map() {
    g_c22 = C22State{};
    g_c22_ring = C22Ring{};
}

// Part B: at HandleSprintChange, the blueprint calls that led to it, the ability's variables, and the weapon.
void c22_sprint_dump(const void* ability, const void* avatar, uint32_t who, float value) {
    if (!note_event_cap(g_c22.dumps[who], kC22SprintDumps, "card022 sprint dump")) {
        g_c22_ring.total = 0;
        return;
    }
    const uint32_t total = g_c22_ring.total;
    const uint32_t shown = total < kC22Ring ? total : kC22Ring;
    log::line("card022: %s %s HandleSprintChange(%.3f) on %s: last %u of %u GA_Sprint blueprint native calls:",
              side(), kSprintSideText[who], static_cast<double>(value), class_name(avatar).text, shown, total);
    for (uint32_t i = total - shown; i < total; ++i) {
        const C22Call& c = g_c22_ring.call[i % kC22Ring];
        char bytes[2 * kC22RetBytes + 8] = "none";
        for (uint32_t b = 0; c.ret_len != kC22NoReturn && b < c.ret_len; ++b) {
            snprintf(bytes + 2 * b, sizeof(bytes) - 2 * b, "%02X", c.ret[b]);
        }
        log::line("card022:   %s calls %s.%s, return 0x%s", fname_text(&c.node).text, fname_text(&c.outer).text,
                  fname_text(&c.function).text, bytes);
    }
    g_c22_ring.total = 0;
    c22_own_props("GA_Sprint", ability, kC22SprintSupers);
    const bool mi = avatar != nullptr && is_a(avatar, g_player_mi_class());
    const void* const weapons = mi ? read_ptr(avatar, kMiWeaponInventory) : nullptr;
    const void* const weapon = weapons != nullptr ? g_get_equipped_weapon(weapons) : nullptr;
    log::line("card022: %s %s equipped weapon %s (%s), inventory %s", side(), kSprintSideText[who],
              object_name(weapon).text, weapon != nullptr ? class_name(weapon).text : "-",
              mi ? object_name(weapons).text : "not an MI pawn");
}

void reset_move_diag_map() {
    g_move_diag = MoveDiag{};
    g_move_diag.updated_is_root = -1;
}

// One row of the card 013/014 lines; resets the row counters. Shared by the client rot and host srv rows.
void log_card013_row(const char* tag, uint32_t k) {
    log_move_chain(tag, k);
    log_actions(tag, k);
    reset_move_chain_row();
    reset_actions();
}

void reset_card016_map() {
    g_jump = JumpTrace{};
}

// One row line of a side's GA_Sprint counters since its last row; resets them.
void log_sprint_row(const char* tag, uint32_t k, uint32_t side) {
    SprintCounts& c = g_sprint.row[side];
    log::line("%s[%u]: since last row (%s GA_Sprint): ActivateAbility %u, EndAbility %u (bWasCancelled %u), "
              "CancelAbility %u, HandleSprintChange %u (last value %.3f), WalkSpeed changes %u (last %.1f)",
              tag, k, kSprintSideText[side], c.activates, c.ends, c.cancelled_ends, c.cancels, c.changes,
              static_cast<double>(c.last_value), c.walk_changes, static_cast<double>(c.last_walk));
    c = SprintCounts{};
}

void log_move_diag(uint32_t k) {
    const MoveDiag& d = g_move_diag;
    log::line("rot[%u]: PerformMovement (own pawn) %u, did not move %u, displacement sum %.1f, UpdatedComponent==Root "
              "%d, Mobility %u, caller exe+0x%llX x%u, other caller exe+0x%llX x%u",
              k, d.calls, d.still, d.displacement, d.updated_is_root, d.mobility, d.caller_first, d.calls_first,
              d.caller_other, d.calls_other);
    log::line("rot[%u]: pins %u after ClientAdjustPosition + %u other; last other pin (%.1f, %.1f, %.1f) -> (%.1f, "
              "%.1f, %.1f)",
              k, d.pins_after_adjust, d.pins_other, d.pin_from.x, d.pin_from.y, d.pin_from.z, d.pin_to.x, d.pin_to.y,
              d.pin_to.z);
}

void log_key_counts(const char* tag, uint32_t k) {
    log::line("%s[%u]: since last row: EnhancedPlayerInput::InputKey %u (MouseX %u, MouseY %u, Mouse2D %u), "
              "HandleInputLook %u, HandleInputMove %u",
              tag, k, g_counts.keys, g_counts.mouse_x, g_counts.mouse_y, g_counts.mouse_2d, g_counts.look,
              g_counts.move);
    log::line("%s[%u]: since last row: MouseX nonzero %u (sum |amount| %.3f), MouseY nonzero %u (sum |amount| %.3f)",
              tag, k, g_counts.mouse_x_nonzero, g_counts.mouse_x_abs, g_counts.mouse_y_nonzero, g_counts.mouse_y_abs);
}

// Card 011: one line of CMC state. `owner` is the pawn the caller reached the CMC through (null
// when it came from the CMC itself); a mismatch with CharacterOwner is reported, fields still read.
void log_cmc(const char* tag, uint32_t k, const uint8_t* cmc, const void* owner) {
    if (cmc == nullptr) {
        log::line("%s[%u]: no CharacterMovementComponent", tag, k);
        return;
    }
    const void* const character = read_ptr(cmc, kCmcCharacterOwner);
    float walk = 0.0f;
    float accel = 0.0f;
    memcpy(&walk, cmc + kCmcMaxWalkSpeed, sizeof(walk));
    memcpy(&accel, cmc + kCmcMaxAcceleration, sizeof(accel));
    const Vec3 v = read_vec3(cmc, kMoveVelocity);
    log::line("%s[%u]: CMC %p %s CharacterOwner %p%s, MovementMode %u, MaxWalkSpeed %.1f, MaxAcceleration %.1f, "
              "Velocity (%.1f, %.1f, %.1f) speed %.1f, IgnoreClientErrors bit %u",
              tag, k, static_cast<const void*>(cmc), class_name(cmc).text, character,
              (owner == nullptr || owner == character) ? "" : " (NOT the pawn)", cmc[kCmcMovementMode],
              static_cast<double>(walk), static_cast<double>(accel), v.x, v.y, v.z,
              sqrt(v.x * v.x + v.y * v.y + v.z * v.z), (cmc[kCmcIgnoreClientErrors] & kCmcIgnoreClientErrorsBit) ? 1u : 0u);
}

// kRotRows rows, one per kRotRowIntervalMs, once the input window opened (card 014). Logged from
// inside the ServerMove call, so the CMC is only used while the engine is using it.
void host_row_step(const uint8_t* cmc) {
    const ULONGLONG now = GetTickCount64();
    if (g_host_rows_left == 0 || now < g_host_next_ms) {
        return;
    }
    g_host_next_ms = now + kRotRowIntervalMs;
    --g_host_rows_left;
    const uint32_t k = g_host_row++;
    log_cmc("srv", k, cmc, nullptr);
    log::line("srv[%u]: since last row: server moves %u, ServerCheckClientError %u, corrections %u; last correction "
              "error (%.1f, %.1f, %.1f) |e| %.1f, client accel |a| %.1f",
              k, g_srv.moves, g_srv.checks, g_srv.corrections, g_srv.last_error.x, g_srv.last_error.y,
              g_srv.last_error.z, vec_len(g_srv.last_error), vec_len(g_srv.last_accel));
    log_key_counts("srv", k); // card 012: the host's own input, the control for the client's rot rows
    log_card013_row("srv", k); // card 013: chain of the remote client's pawn; IA_Look/IA_Move of the host's input
    log_sprint_row("srv", k, kSideServerCopy); // card 019: GA_Sprint of the client's pawn, server copy
    g_srv = ServerDiag{};
    g_counts = DiagCounts{}; // the host never runs rot rows, so this is the only reset there
}

// Per LoadMap: enter disarms, a successful return re-arms with fresh caps and slots.
void reset_card017_map(bool arm) {
    g_c17 = C17State{};
    g_c18 = C18State{};
    g_c17.start_ms = GetTickCount64();
    g_c17_armed = arm;
}

void reset_card020_map() {
    g_c20 = C20State{};
    g_c21_grant_lines = 0;
    g_c20_rows_left = 0;
    g_c21_grant_armed = false;
    g_c21_grant = C21Grant{};
    g_c21_camera_logs = 0;
    g_c24_rpc_lines = 0;
    g_c24_local_lines = 0;
    c24_reset_map();
    for (float& h : g_c21_health_seen) {
        h = -1.0f;
    }
}

void c24_reset_map() {
    for (int32_t i = 0; i < g_c24.relays.count && i < kC24Relays; ++i) {
        if (g_c24.relays.item[i].kind == kFirePrimary) {
            c276_end_line(g_c24.relays.item[i], nullptr, "map reset"); // card 276 diag; the pawn may be gone
        }
    }
    g_c24.relays.count = 0;
    g_c76_imc = C76Imc{-1, {}, 0}; // card 076: a new map has a new input subsystem state
    for (C26Own& own : g_c24.own) {
        own.held = false;
    }
    g_c24.host_fire = false;
    // Card 180: a release held by card 178 does not outlive its map or session.
    g_c24.release_due = false;
    g_c24.release_flushing = false;
    g_c24.release_pc_id = -1;
    g_c24.release_fired_at = 0;
    g_c24.release_why = nullptr;
}

void c25_arm_map() {
    g_c25 = C25State{};
    g_c25.armed = g_c25_kill && !g_client_session;
}

void c27_arm_map() {
    g_c27 = C27State{};
    g_c27.armed = g_c27_dump && !g_client_session;
}

void c29_arm_map() {
    g_c29 = C29State{};
    g_c29.armed = g_c29_audit;
}

void c28_arm_map() {
    g_c28 = C28State{};
    g_c28.npc_renderer = -1;
    g_c33 = C33State{};
}

// Card 130 item 15: a respawn in place (RESTART CHECKPOINT) replaces every player pawn with no map load, and the
// per-pawn slot tables of cards 022, 028 and 033 (kC28MaxPawns, keyed by object index) were reset only by a map load,
// so after a few restarts on one map they filled and a new pawn got no sprint setup, body refit or hide. The old
// pawns are gone, so their slots go; a pawn still alive gets a fresh slot on its next visit, as after a map load.
void pawn_slots_respawn() {
    log::line("card130: %s respawn in place: pawn slots dropped (card022 %u, card028 %u, card033 %u)", side(),
              g_c22.count, g_c28.count, g_c33.count);
    g_c22.count = 0;
    g_c28.count = 0;
    g_c33.count = 0;
}

void c23_arm_map() {
    g_c23 = C23State{};
    g_c23.armed = g_c23_audit && g_enemy_class != nullptr;
    if (g_c23_audit && !g_c23.armed) {
        log::line("WARN card023: ASTEnemyBase::StaticClass unresolved -- enemy audit off");
    }
    const bool spawn_ok = g_uclass_class != nullptr && g_spawn_ai_from_class != nullptr && g_static_load_object != nullptr;
    if (g_c23_spawn && !g_client_session && !spawn_ok) {
        g_c23.spawn_tries = kC23SpawnTries;
        log::line("WARN card023: UClass::StaticClass, SpawnAIFromClass or StaticLoadObject unresolved -- no spawn");
    }
}

void c37_arm_map() {
    if (g_c37.adopts + g_c37.notifies + g_c37.revive_skips + g_c37.revive_calls != 0) {
        log::line("card037: %s map totals: adopts %u, notifies %u, revive skips %u, revive-branch handler calls %u",
                  side(), g_c37.adopts, g_c37.notifies, g_c37.revive_skips, g_c37.revive_calls);
    }
    g_c37 = C37State{};
    g_c37.audit = g_c23_audit && g_st_character_class != nullptr;
    g_c37.armed = !g_c37_off && g_st_character_class != nullptr && g_health_attribute_changed != nullptr;
    if (!g_c37_off && !g_c37.armed) {
        log::line("WARN card037: ASTCharacter::StaticClass or HealthAttributeChanged unresolved -- enemy health off");
    }
}

void c38_arm_map() {
    if (g_c38.dead_peak + g_c38.logic[kC38NpcDeathEvent] != 0) {
        c38_log_counts("map totals");
    }
    c318_map_end(); // card 318: per-map revive tally (before card 038 clears its dead set)
    g_c38 = C38State{};
    c39_arm_map();
    c40_arm_map();
}

void c32_arm_map() {
    g_c32 = C32State{};
    g_c32.mech = -1;
    g_c32.pickup = -1;
    g_c32.thread = GetCurrentThreadId();
    const bool on = g_c32_mode != config::SpawnMech::Off;
    const bool ok = g_mech_class != nullptr && g_mech_on_interaction != nullptr && g_mech_request_leave != nullptr &&
                    g_static_load_object != nullptr && g_uclass_class != nullptr && g_spawn_params_ctor != nullptr &&
                    g_world_spawn_actor != nullptr && g_find_function != nullptr && g_orig_try_throw_flare != nullptr;
    g_c32.armed = on && ok;
    pe_gate_set(kPeGate_c32_pe, g_c32.armed);
    if (on && !ok) {
        log::line("WARN card032: a mech, spawn or tactical support symbol is unresolved -- mech audit off");
    }
}

void c30_arm_map() {
    pe_gate_set(kPeGate_c30_window, false);
    g_c30 = C30State{};
    g_c30.thread = GetCurrentThreadId();
    g_c30.armed = g_c30_mode != config::KillPlayer::Off;
    g_c30.max_health_offset = g_c30.armed ? c30_set_offset("MaxHealth") : -1;
    if (g_c30.armed) {
        log::line("%scard030: %s armed, target side %s, UADAttributeSetHealth.MaxHealth offset 0x%X",
                  g_c30.max_health_offset >= 0 ? "" : "WARN ", side(), kSprintSideText[c30_target_side()],
                  g_c30.max_health_offset);
    }
}

void c41_arm_map() {
    if (g_c41.seq_count != 0) {
        c41_dump("map totals", nullptr);
    }
    c41_relay_arm_map();
    g_c41 = C41State{};
    g_c41.next_dump_ms = GetTickCount64() + kC41DumpMs;
}

// Both sides, at a real map load only (not the respawn in place), after LoadMap returns. The world check names
// the new world; the journal and the synced PCs were reset at LoadMap enter (c49_map_end).
void c49_arm_map() {
    g_c49.world_check_ms = GetTickCount64() + kC49WorldCheckMs;
    g_c49.depth = 0;
}

// Card 199: both sides, at LoadMap enter. The host's journal and synced PCs, and both sides' test state, start over:
// a real map load means new PCs and a new objective map. The new world's level script adds its objectives inside
// LoadMap (BeginPlay), so a reset after LoadMap returned wiped them from the journal and the joining client never got
// them. Card 045's respawn in place does not come here (the snapshot covers it).
// Card 308: host, the objective journal totals row (when: "map end" at LoadMap enter, "quit" at the test quit): the
// map's measured need against the cap.
void c308_journal_totals(const char* when) {
    if (!g_client_session && g_c49_journal.peak > 0) {
        log::line("card308: host t=%llu %s: objective journal peak %d of %d frames, %d not recorded since the last snapshot",
                  static_cast<unsigned long long>(GetTickCount64()), when, g_c49_journal.peak, g_c49_cap,
                  g_c49_journal.dropped);
    }
}

void c49_map_end() {
    c308_journal_totals("map end");
    g_c49_journal.peak = 0;
    c49_journal_reset();
    g_c49_journal.compact_off = false;
    g_c49_cap = config::read_c66_cap();
    const bool delegates_logged = g_c49m.delegates_logged;
    const bool completed = g_c49m.completed && g_c45_phase == 1; // the test completes once, in phase 1
    g_c49m = C49Mirror{};
    g_c49m.delegates_logged = delegates_logged;
    g_c49m.completed = completed;
    c70_map_end(); // b13: a names frame pending across the travel is dropped here, with a row
}

int32_t c49_live() {
    return g_c49_journal.n;
}

void c50_arm_map() {
    c161_arm_map(); // card 161
    c408_arm_map(); // card 408
    c493_arm_map(); // card 493
    c417_arm_map(); // card 417
    c98_arm_map();
    c99_arm_map();
    c120_arm_map(); // card 120
    g_c62_instance_weak = 0; // the old map's pawn and its ability instances are gone
    c114_arm_map();
    if (g_c50.sends + g_c50.recvs + g_c50.relays != 0) {
        c50_totals("map totals");
    }
    g_c50.copies = 0;
    g_c50.c327_rows = 0; // card 327
    c440_arm_map(); // card 440
    g_c50.asc_pawn_id = -1;
    g_c50.use_name[0] = '\0';
    g_c50.relayed_id = -1;
    g_c50.text[0] = '\0';
    g_c50.have_text = false;
    g_c50.pending = false;
    g_c50.applying = false;
    g_c50.own_id = -1;
    g_c50.chapter_ms = 0;
    c50_peer_arm_map(); // card 050 b205
}

// From detour_cancel_ability (UGameplayAbility::CancelAbility, every non-sprint ability).
void c62_note_cancel(const void* ability, int32_t handle, const void* actor_info, bool replicate) {
    if (!g_c50_test || !c62_is_switch(ability) || !c62_line()) {
        return;
    }
    const void* const avatar = avatar_of(actor_info);
    log::line("card062: %s t=%llu UGameplayAbility::CancelAbility(%s %p, handle %d, bReplicate %d) on %s (%s); caller "
              "exe+0x%llX",
              side(), static_cast<unsigned long long>(GetTickCount64()), class_name(ability).text, ability, handle,
              replicate ? 1 : 0, object_name(avatar).text, kSprintSideText[sprint_side(avatar)],
              exe_rva(_ReturnAddress()));
}

// Per map (client_diag_load_map_enter): sightings of the old map are dropped and the classes resolved again.
void c67_reset_map() {
    for (int32_t k = 0; k < kC67Kinds; ++k) {
        g_c67.nseen[k] = 0;
        g_c67.cls[k] = nullptr;
    }
    pe_gate_set(kPeGate_c67_pe, false);
    g_c67.nlisted = 0;
    g_c67.ncalls = 0;
    g_c67.dumps = 0;
    g_c67.later_ms = 0;
    g_c67.map[0] = 0;
    c67_ui_forget();
}

void c69_reset_map() {
    for (uint32_t& n : g_c69_calls) {
        n = 0;
    }
}

void c73_reset_map() {
    memset(g_c73.calls, 0, sizeof(g_c73.calls));
    memset(g_c73_wait, 0, sizeof(g_c73_wait));
    if (g_c73_pending_count != 0) {
        log::line("card073: client %u pending GrenadeItem counts dropped at the map change", g_c73_pending_count);
    }
    g_c73_pending_count = 0;
    g_c73.last[0] = '\0';
    g_c73.last_copy[0] = '\0';
}

// Card 490 step 1 (diag runs only): the BP_PlayerCharacterMI Stamina variable and the Abilities.NoStamina tag count on
// each player pawn this side sees (host: own pawn plus the server copies; client: own pawn), sampled every
// kC490PeriodMs. A row is written when Stamina moves by kC490Step or more, or the tag count, MovementMode or
// MaxWalkSpeed changes, so the rows show which move spends stamina and whether an empty bar blocks it. Row:
// `card490: <side> t=T pc I <pawn> role R: Stamina S, NoStamina N, MovementMode M, MaxWalkSpeed W`.
constexpr ULONGLONG kC490PeriodMs = 100;
constexpr double kC490Step = 1.0;
constexpr uint32_t kC490Rows = 1500; // per map, about 150 s of a draining or refilling bar

struct C490Slot {
    const void* pawn;
    double stamina;
    int32_t tag;
    uint8_t mode;
    float walk;
};

struct C490State {
    C490Slot slot[kC17Slots];
    uint64_t tag_name;
    uint32_t rows;
    ULONGLONG next_ms;
};

C490State g_c490{};

void reset_c490_map() {
    g_c490 = C490State{};
    c490_hold_map_end();
}

// The pawn's Stamina (FloatProperty or DoubleProperty), or -1 when its class has none.
double c490_stamina(const void* pawn) {
    const uint8_t* const prop = prop_find_prop(read_ptr(pawn, kObjectClass), "Stamina");
    if (prop == nullptr) {
        return -1.0;
    }
    const auto* const at = static_cast<const uint8_t*>(pawn) + read_i32(prop, kPropertyOffset);
    if (prop_kind_is(prop, "DoubleProperty")) {
        double d = 0.0;
        memcpy(&d, at, sizeof(d));
        return d;
    }
    float f = 0.0f;
    memcpy(&f, at, sizeof(f));
    return static_cast<double>(f);
}

void c490_sample(int32_t i, const uint8_t* pawn, ULONGLONG now) {
    C490Slot& s = g_c490.slot[i];
    const auto* const cmc = static_cast<const uint8_t*>(read_ptr(pawn, kCharacterMovement));
    const void* const asc = g_get_asc != nullptr ? g_get_asc(pawn, true) : nullptr;
    const uint64_t tag = fname_cached(g_c490.tag_name, L"Abilities.NoStamina");
    const double stamina = c490_stamina(pawn);
    const int32_t count = asc != nullptr && tag != 0 && g_c310_tag_count != nullptr ? g_c310_tag_count(asc, tag) : -1;
    const uint8_t mode = cmc != nullptr ? cmc[kCmcMovementMode] : 0xFFu;
    float walk = 0.0f;
    if (cmc != nullptr) {
        memcpy(&walk, cmc + kCmcMaxWalkSpeed, sizeof(walk));
    }
    const bool same = s.pawn == pawn && fabs(stamina - s.stamina) < kC490Step && count == s.tag && mode == s.mode &&
                      walk == s.walk;
    if (same || !note_event_cap(g_c490.rows, kC490Rows, "card490 stamina rows")) {
        return;
    }
    s = C490Slot{pawn, stamina, count, mode, walk};
    log::line("card490: %s t=%llu pc %d %s role %d: Stamina %.1f, NoStamina %d, MovementMode %u, MaxWalkSpeed %.0f",
              side(), static_cast<unsigned long long>(now), i, object_name(pawn).text,
              static_cast<int>(pawn[kActorLocalRole]), stamina, count, static_cast<unsigned>(mode),
              static_cast<double>(walk));
}

// Game thread, every ubw_post_tick in diag runs.
void c490_step(void* engine) {
    const ULONGLONG now = GetTickCount64();
    if (now < g_c490.next_ms) {
        return;
    }
    g_c490.next_ms = now + kC490PeriodMs;
    void* const world = g_get_game_world(engine);
    const int32_t pcs = g_client_session ? 1 : kC17Slots;
    for (int32_t i = 0; world != nullptr && i < pcs; ++i) {
        void* const pc = g_get_player_controller(world, i);
        const auto* const pawn = pc != nullptr ? static_cast<const uint8_t*>(read_ptr(pc, kControllerPawn)) : nullptr;
        if (pawn != nullptr && is_a(pawn, g_character_class())) {
            c490_sample(i, pawn, now);
        }
    }
}

// Card 490 (b219): the host's infinite stamina option (lobby panel I, saved as `stamina=` in UBWCoop.ini, off by
// default; the test command `stamina on|off` sets it for the session only). When on, the host writes every player
// pawn's Stamina back to its maximum after each world tick (it has authority over all of them), and each client writes
// its own pawn's, so both copies of a client's pawn keep the full sprint speed and the jump cost never shows. The
// Abilities.NoStamina tag is never added or removed: with a full bar the game never applies it. The host sends the
// value (ChanOp::Stamina, 0 or 1) at each join replay and to every client when it changes.
constexpr double kC490DefaultMax = 100.0; // the b218 rows' full bar when the class has no MaxStamina variable

// Card 562: the Stamina offsets of one pawn class. The pawns filled each tick alternate between classes (the trooper
// and the mech, turret or missile its player drives), so a one-class cache rebound and logged every frame (11629 WARN
// rows in the 2026-11-20 playtest host log). Each class is bound and logged once per map.
struct C490Class {
    const void* cls;
    int32_t at;            // Stamina's offset, -1 when the class has none (a valid state: no stamina bar)
    bool dbl;              // Stamina is a DoubleProperty
    int32_t max_at;        // MaxStamina's offset, -1 when absent (kC490DefaultMax)
    bool max_dbl;
};
constexpr int32_t kC490Classes = 16;

struct C490Hold {
    bool host_on;          // host: the option
    bool from_host;        // client: the last value the host sent
    C490Class cls[kC490Classes];
    int32_t classes;       // bound entries in cls
    uint32_t writes;       // this map's refills, logged at the map end
};

C490Hold g_c490_hold{false, false, {}, 0, 0};

double c490_read(const uint8_t* at, bool dbl) {
    if (dbl) {
        double d = 0.0;
        memcpy(&d, at, sizeof(d));
        return d;
    }
    float f = 0.0f;
    memcpy(&f, at, sizeof(f));
    return static_cast<double>(f);
}

void c490_write(uint8_t* at, bool dbl, double v) {
    if (dbl) {
        memcpy(at, &v, sizeof(v));
        return;
    }
    const auto f = static_cast<float>(v);
    memcpy(at, &f, sizeof(f));
}

// The Stamina and MaxStamina offsets of `cls`, looked up once per class and logged; nullptr when the table is full
// (logged once, that class is not filled).
const C490Class* c490_bind(const void* cls) {
    C490Hold& h = g_c490_hold;
    for (int32_t i = 0; i < h.classes; ++i) {
        if (h.cls[i].cls == cls) {
            return &h.cls[i];
        }
    }
    if (h.classes >= kC490Classes) {
        static bool full_logged = false;
        if (!full_logged) {
            full_logged = true;
            log::line("WARN card490: %s pawn class table full (%d): %s and later classes are not filled", side(),
                      kC490Classes, object_name(cls).text);
        }
        return nullptr;
    }
    C490Class& c = h.cls[h.classes++];
    const uint8_t* const p = prop_find_prop(cls, "Stamina");
    const uint8_t* const m = prop_find_prop(cls, "MaxStamina");
    c.cls = cls;
    c.at = p != nullptr ? read_i32(p, kPropertyOffset) : -1;
    c.dbl = p != nullptr && prop_kind_is(p, "DoubleProperty");
    c.max_at = m != nullptr ? read_i32(m, kPropertyOffset) : -1;
    c.max_dbl = m != nullptr && prop_kind_is(m, "DoubleProperty");
    if (c.at < 0) {
        log::line("card490: %s pawn class %s has no Stamina: not filled", side(), object_name(cls).text);
    } else {
        log::line("card490: %s pawn class %s: Stamina at %d (%s), MaxStamina at %d%s", side(), object_name(cls).text,
                  c.at, c.dbl ? "double" : "float", c.max_at, c.max_at < 0 ? " (absent: full bar is 100)" : "");
    }
    return &c;
}

void c490_fill(uint8_t* pawn) {
    const C490Class* const hp = c490_bind(read_ptr(pawn, kObjectClass));
    if (hp == nullptr || hp->at < 0) {
        return;
    }
    const C490Class& h = *hp;
    const double max = h.max_at >= 0 ? c490_read(pawn + h.max_at, h.max_dbl) : kC490DefaultMax;
    if (max > 0.0 && c490_read(pawn + h.at, h.dbl) < max) {
        c490_write(pawn + h.at, h.dbl, max);
        ++g_c490_hold.writes;
    }
}

// Game thread, every ubw_post_tick: host, every player pawn; client, its own pawn. Nothing while the option is off.
void c490_hold_step(void* engine) {
    const bool on = g_client_session ? g_c490_hold.from_host : g_c490_hold.host_on;
    void* const world = on ? g_get_game_world(engine) : nullptr;
    const int32_t pcs = g_client_session ? 1 : kC17Slots;
    for (int32_t i = 0; world != nullptr && i < pcs; ++i) {
        void* const pc = g_get_player_controller(world, i);
        auto* const pawn = pc != nullptr ? static_cast<uint8_t*>(read_ptr(pc, kControllerPawn)) : nullptr;
        if (pawn != nullptr && is_a(pawn, g_character_class())) {
            c490_fill(pawn);
        }
    }
}

// Host: the option to one client PC (join replay, or a change).
void c490_send(void* pc) {
    if (g_client_session || pc == nullptr || !c26_is_remote_pc(pc)) {
        return;
    }
    const bool ok = chan_to_client(pc, ChanOp::Stamina, g_c490_hold.host_on ? 1u : 0u);
    log::line("%scard490: host sent stamina %d to %s", ok ? "" : "WARN ", g_c490_hold.host_on ? 1 : 0,
              object_name(pc).text);
}

// Host: the option set (`why`: ini, panel or cmd); every client PC in `world_ctx`'s world gets the new value.
void c490_set_host(const void* world_ctx, bool on, const char* why) {
    const bool changed = g_c490_hold.host_on != on;
    g_c490_hold.host_on = on;
    log::line("card490: host stamina %s (%s)%s", on ? "on" : "off", why, changed ? "" : ", unchanged");
    for (int32_t i = 1; world_ctx != nullptr && !g_client_session && changed && i < kC17Slots; ++i) {
        c490_send(g_get_player_controller(world_ctx, i));
    }
}

// Client: the host's value, held until the next one (each map's join replay sends it again).
void c490_receive(void*, uint32_t on) {
    if (!g_client_session) {
        return;
    }
    const bool was = g_c490_hold.from_host;
    g_c490_hold.from_host = on != 0;
    log::line("card490: client host stamina %u%s", on, was == (on != 0) ? ", unchanged" : "");
}

// Map end: the hold's writes on this map, when the option was on.
void c490_hold_map_end() {
    if (g_c490_hold.writes != 0) {
        log::line("card490: %s stamina refilled %u times on this map", side(), g_c490_hold.writes);
    }
    g_c490_hold.writes = 0;
    g_c490_hold.classes = 0; // card 562: a Blueprint class can unload with its map and its address be reused
}

} // namespace ubw::hooks
