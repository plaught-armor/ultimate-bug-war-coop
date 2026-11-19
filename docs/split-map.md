# Split map: `mod/src/hooks.cpp` into `ue/` and `ubw/` (card 061)

Map of 2026-11-05, taken after card 075. Nothing has moved yet. Each later move card moves one feature and changes no behaviour.

## Sizes

- `mod/src/hooks.cpp` has 27626 lines, out of 31152 in `mod/src`, and 1416 functions. None of them is over 60 lines.
- `namespace ubw::hooks` opens at line 22. The anonymous namespace runs from 23 to 27551. Lines 27552-27626 (`install_card_logs`, `install`) are outside it, and `ubw::hooks` closes at 27626.
- `kHooks` (26911-27211) has 225 rows:
  - 220 rows name `ubw` detours, and 115 of those are gated;
  - 5 rows name `ue` detours: GameEngine Init, Browse, LoadMap, Listen and Tick. Only Listen is gated (`g_gate_diag`).
  - So 116 rows are gated in all.
- `kCalls` (27212-27436) has 211 rows: 152 fill `ubw` slots and 59 fill `ue` engine-function slots.
- There are 11 gate bools.

## The rule

- **`ue/`** holds engine-generic code: the hook table and installer, reflection (GUObjectArray, FName, `StaticFindObject`, `ProcessEvent`, property walking), FString, TArray and TMap, the travel, listen, join and possess glue, the net channel, and log and config. It names no UBW class, asset, offset or card. Generic helpers whose names carry a card number are renamed when they move (see "Renames" below).
- **`ubw/`** holds game features and their offsets.

## Method (how this map was made)

The map was made with scratch scripts, which are not part of the repo:

1. A comment- and string-aware brace scanner listed 4058 top-level definitions of `hooks.cpp`, each with its name, kind and line range.
2. The file was cut into 106 contiguous blocks at card boundaries (mostly the `// Card NNN` banners).
3. For every definition, the scanner collected the top-level names it uses and the block that owns each one.

Each definition was given a side:

- **Whole-`ue` blocks.** Everything in `prologue`, `fstring-reading`, `engine-functions-we` and `detours` is `ue`.
- **Other blocks.** A name is `ue` if two or more other blocks use it and it does not carry a card marker (`cNN`, `CNN`, `kCNN`). Such names are engine function pointers, their typedefs, and engine layout constants.
- **Hand corrections.** A short list was moved each way by hand:
  - to `ue`: generic helpers that carry a card name, such as `c24_fname` and the card 027 property walker;
  - to `ubw`: names with game-specific text, such as `g_health_set_class` and `kGameInstanceChapter`.
- **Detours.** A `detour_*` or `g_orig_*` outside the `detours` block is `ubw`, because a feature detour owns its original.
- **Carried names.** A `ubw` type alias or `k` constant that only `ue` code uses goes with that code (49 names, all `*Fn` typedefs or engine constants).

A block whose definitions all land on one side is `ue` or `ubw`. A block with definitions on both sides is `mixed`, and its row says what is cut out to `ue/`.

Caveat: a name used by zero or one other block stays with its block, even when it is engine-generic. A move card that finds such a helper may lift it into `ue/` itself. This adds no dependency, because the helper is then `ue` code that `ubw` calls.

## Proposed files

`ue/` (shared declarations go in one internal header, `ue/core.h`, because today everything sits in one anonymous namespace):

| File | Holds |
|---|---|
| `ue/hooktable.cpp` | `HookSpec`, `CallSpec`, `resolve_all`, `hook_gated_off`, `log_skipped_hooks`, `install_hooks`, `kHooksUe[]`, `kCallsUe[]`, `g_exe_base`, `g_last_hook`, PE/PDB identity check |
| `ue/reflect.cpp` | FString reading, FName (`g_fname_ctor`, `kFNameAdd`, cached-FName lookup), `StaticFindObject`/`FindFirstObject`/`StaticLoadObject`, `ProcessEvent` access, `FindFunction`, property walker (card 027's walker) |
| `ue/actor.cpp` | actor, component and level walking: `ActorGetWorld`, `SetActorLocation`, `AddComponentByClass`, the `ULevel` actor-array walk, `FindComponentByClass` |
| `ue/net.cpp` | the five engine detours (Init, Browse, LoadMap, Listen, Tick), travel (`g_travel`, `g_set_client_travel`, `g_get_game_world`), net mode, possess glue, `SprintSide` role helper |
| `ue/chan.cpp` | channel transport: `WireString`, `ChanArgs`, frame building and parsing, `chan_send_host`, `chan_send_client`, `chan_to_host`, `chan_to_client`, `chan_text`, `chan_text_frame`, `object_index`, the dispatcher |
| `ue/input.cpp` | Enhanced Input subsystem access, input-mode constructors, `FKey` constructor and destructor, console command, screenshot request |
| `ue/gas.cpp` | ASC lookup, `FindSpecFromClass`, `SetNumericAttributeBase`, `GameplayAttributeRaw` |
| `ue/widget.cpp` | UMG: `CreateWidgetInstance`, `AddToViewport`, `PanelAddChild`, `FText` conversion, visibility and focus |
| `ue/log.cpp` | the existing `mod/src/log.cpp`, plus skip-note helpers (`note_skip`) |

`ubw/` (each file is one feature. A `cNNN` in the target column refers to the card's block):

| File | Blocks |
|---|---|
| `ubw/flags.cpp` | gate bools, `set_hook_gates`, `read_card_flags`, auto-travel state |
| `ubw/hooks_ubw.cpp` | `kHooksUbw[]`, `kCallsUbw[]`, `install_card_logs`, the per-event step tables |
| `ubw/guards.cpp` | c003-c007, c052.4 |
| `ubw/input_diag.cpp` | c008, c010-c013, collision writers, IA look/move eval, c016*, c011 |
| `ubw/pawn_collision.cpp` | c014 |
| `ubw/dropship.cpp` | c015* |
| `ubw/sprint.cpp` | c019*, c022 |
| `ubw/attributes.cpp` | c017, c018, c020*, and the Interact detours (`detour_interact`, `detour_release_interaction`). In card 088 the card 081 state and `iface_object` left it: `iface_object` is shared with `ubw/interact.cpp`, so it stays in `hooks.cpp`. |
| `ubw/weapons.cpp` | c021*, c050, c050.2, c050.4, c050.5, c057, c062 |
| `ubw/ammo.cpp` | c069, c071, c072 |
| `ubw/fire.cpp` | c024*, the c026 ping/pong and edge-line code, and the card 076 mech fire check (log only; moved from `ubw/vehicles.cpp` in card 088) |
| `ubw/chan_ops.cpp` | the host and client handler tables (`kChanHost`, `kChanClient`, `kChanOps`, returned by `chan_ops()`), the per-op handlers and the adapters. The `ChanOp` enum and `kChanArgs` are in `ubw/chan_ops.h` (card 085). |
| `ubw/audit_npc.cpp` | c025, c027 (the walker itself goes to `ue/reflect.cpp`) |
| `ubw/vehicles.cpp` | c029*, c032*, c075.2, and the card 076 mech input contexts (in the c026 range). Card 079 moved to `ubw/death.cpp` and the card 076 mech fire check to `ubw/fire.cpp` in card 088. |
| `ubw/remote_body.cpp` | c028, c028.2, c033, c034, c075 |
| `ubw/enemies.cpp` | c023, c037-c041 |
| `ubw/death.cpp` | c030*, c043*, c044, c046, c048*, c079 (a mech is not a card 043 player) and the c081 state (`kC81Lines`, `C81State`, `g_c81`; `c81_refuse_press` stays in `hooks.cpp` because `ubw/attributes.cpp` calls it). Card 088. |
| `ubw/restart.cpp` | c045 |
| `ubw/lobby_ui.cpp` | c031*, c052-c052.3. Done in card 088. |
| `ubw/objectives.cpp` | c049, host snapshot, c053, c065, c066, client replay steps, c068, c070. Done in card 088. |
| `ubw/interact.cpp` | c050.3, c056, and the card 081 `TraceInteractable` detour (`detour_trace_interactable`, `g_orig_trace_interactable`). Done in card 088. |
| `ubw/testrun.cpp` | c063, c064, c067*. Done in card 088. |
| `ubw/grenades.cpp` | c073, c074*. Done in card 088. |

Card 066's objective journal and the client replay steps are mapped to `ubw/objectives.cpp`, because every frame they hold is an objective frame. If a second feature ever needs a journal, the replay ring can be lifted into `ue/chan.cpp` then. That is not part of this split.

## Section map

106 blocks. Columns:

- **Lines** and **Block**: the block's range and its short name.
- **Side**: `ue`, `ubw` or `mixed`. For a `mixed` row it gives the count of `ue` and `ubw` names and every name cut out to a `ue/` file. All other names in the block go to the `ubw/` target.
- **Target file(s)**: where the block's code goes.
- **Globals it owns**: the `g_*` variables the block defines.
- **Globals it reads from other blocks**: the `g_*` variables it uses that other blocks define.

Check: the blocks are contiguous, sorted and non-overlapping. They run from line 1 to line 27626, and the sum of their lengths is 27626, which is every line of the file once.

| Lines | Block | Side (mixed: what moves to ue) | Target file(s) | Globals it owns | Globals it reads from other blocks |
|---|---|---|---|---|---|
| 1-107 | prologue | ue | ue/hooktable.cpp | - | - |
| 108-135 | fstring-reading | ue | ue/reflect.cpp | - | - |
| 136-142 | engine-functions-we | ue | ue/net.cpp | `g_get_game_world` `g_set_client_travel` | - |
| 143-375 | auto-travel-state | mixed (3 ue / 47 ubw names). Cut to ue: ue/hooktable.cpp: g_last_hook ; ue/net.cpp: g_travel g_travel_armed | ubw/flags.cpp, ue/hooktable.cpp, ue/net.cpp | `g_auto_dropship` `g_auto_dropship_armed` `g_c17_armed` `g_c20_rows_left` `g_c21_camera_logs` `g_c21_grant_armed` `g_c24_local_lines` `g_c24_rpc_lines` `g_c32_mode` `g_c39_overkill` `g_c40_trace` `g_c42_test` `g_c43_start_ms` `g_c43_test` `g_c44_fires` `g_c44_pe` `g_c45_test` `g_c46_test` `g_c50_test` `g_c54_test` `g_c57_solo` `g_c60_host_own` `g_c60_mk1_only` `g_c62_instance` `g_c63_on` `g_c65_activate_logs` `g_c65_log` `g_c65_logs` `g_c65_shunt_logs` `g_c67_pe` `g_c68_log` `g_c68_logs` `g_c69_log` `g_c73_log` `g_c74_log` `g_c74_pe` `g_dropship_watch_armed` `g_grant_weapon` `g_input_window_armed` `g_last_hook` `g_own_rows_left` `g_pawn_watch_armed` `g_rot_rows_left` `g_snapshot_armed` `g_travel` `g_travel_armed` `g_world_seen` `g_world_seen_ms` | `g_get_game_world` `g_set_client_travel` |
| 376-507 | detours | ue | ue/net.cpp | `g_orig_browse` `g_orig_game_engine_init` `g_orig_listen` `g_orig_load_map` `g_orig_tick` | `g_auto_dropship_armed` `g_c17_armed` `g_c20_rows_left` `g_c21_grant_armed` `g_c63_on` `g_c68_log` `g_c69_log` `g_c73_log` `g_dropship_watch_armed` `g_last_hook` `g_own_rows_left` `g_pawn_watch_armed` `g_rot_rows_left` `g_snapshot_armed` `g_travel_armed` |
| 508-546 | c003 | mixed (3 ue / 4 ubw names). Cut to ue: ue/actor.cpp: kMaxLoggedSkips ; ue/hooktable.cpp: g_exe_base ; ue/log.cpp: note_skip | ubw/guards.cpp, ue/actor.cpp, ue/hooktable.cpp, ue/log.cpp | `g_exe_base` `g_orig_update_bindings` `g_update_bindings_skips` | - |
| 547-582 | c004 | mixed (3 ue / 6 ubw names). Cut to ue: ue/actor.cpp: ObjectPtrIsAFn ; ue/reflect.cpp: StaticClassFn g_object_ptr_is_a | ubw/guards.cpp, ue/actor.cpp, ue/reflect.cpp | `g_local_player_class` `g_object_ptr_is_a` `g_orig_st_set_player` `g_pc_set_player` `g_set_player_skips` | - |
| 583-862 | c005 | mixed (11 ue / 44 ubw names). Cut to ue: ue/actor.cpp: GetWorldFn GetLocalPlayerFn g_actor_get_world ; ue/input.cpp: g_get_local_player g_get_enhanced_input kSubsystemInputInterface clear_pending_starting_mappings ; ue/reflect.cpp: kObjectClass SparseView sparse_view sparse_allocated | ubw/guards.cpp, ue/actor.cpp, ue/input.cpp, ue/reflect.cpp | `g_actor_get_world` `g_client_input_logs` `g_fake_game_mode` `g_gc_barrier_flag` `g_get_enhanced_input` `g_get_local_player` `g_load_synchronous` `g_mappable_config_class` `g_mark_as_reachable` `g_orig_get_game_mode` `g_received_game_mode_class` `g_setup_input_component` `g_setup_input_get_game_mode_ret` `g_starting_mappings_pending` | `g_exe_base` `g_object_ptr_is_a` |
| 863-905 | c005.2 | mixed (2 ue / 9 ubw names). Cut to ue: ue/actor.cpp: WeakGetFn ; ue/reflect.cpp: g_weak_get | ubw/guards.cpp, ue/actor.cpp, ue/reflect.cpp | `g_mech_input_skips` `g_orig_add_mech_input` `g_orig_remove_mech_input` `g_weak_get` | `g_get_enhanced_input` `g_get_local_player` |
| 906-987 | c006 | mixed (6 ue / 18 ubw names). Cut to ue: ue/actor.cpp: ActorNetModeFn ; ue/net.cpp: g_actor_net_mode kNetModeClient kNetModeListenServer kNetModeStandalone is_net_client | ubw/guards.cpp, ue/actor.cpp, ue/net.cpp | `g_actor_net_mode` `g_encounter_skips` `g_orig_activate_current_wave` `g_orig_perform_spawn` `g_orig_queue_spawn` `g_orig_start_encounter` `g_spawn_skips` | - |
| 988-1067 | c007 | mixed (2 ue / 11 ubw names). Cut to ue: ue/actor.cpp: FKeyDestructorFn ; ue/input.cpp: g_fkey_destructor | ubw/guards.cpp, ue/actor.cpp, ue/input.cpp | `g_fkey_destructor` `g_is_pressed_skips` `g_orig_is_pressed` `g_st_pc_tick` `g_tick_is_pressed_ret` | `g_exe_base` `g_gc_barrier_flag` `g_setup_input_component` `g_setup_input_get_game_mode_ret` |
| 1068-1481 | c008 | mixed (33 ue / 62 ubw names). Cut to ue: ue/actor.cpp: GetInterfaceAddressFn GetNameEntryFn WorldGetFn kActorCollisionFlags kActorEnableCollisionBit kControllerPawn kPcCursorFlags kMaxLoggedEvents ; ue/hooktable.cpp: exe_rva ; ue/input.cpp: g_get_interface_address g_world_game_viewport g_input_mode_game_only_ctor g_client_game_only kControllerIgnoreMove kControllerIgnoreLook kViewportCaptureMode cursor_bit ; ue/log.cpp: note_event_cap note_event ; ue/net.cpp: g_world_first_pc g_client_session is_local_pc net_mode ; ue/reflect.cpp: g_name_entry kObjectName kObjectOuter kNameChars ObjectName fname_text object_name class_name read_ptr is_a | ubw/input_diag.cpp, ue/actor.cpp, ue/hooktable.cpp, ue/input.cpp, ue/log.cpp, ue/net.cpp, ue/reflect.cpp | `g_character_class` `g_client_game_only` `g_client_session` `g_context_logs` `g_cursor_logs` `g_game_only_pending` `g_get_interface_address` `g_ignore_move_logs` `g_input_mode_game_only_ctor` `g_input_mode_logs` `g_input_receiver_class` `g_name_entry` `g_orig_acknowledge_possession` `g_orig_add_mapping_context` `g_orig_apply_ui_input_config` `g_orig_client_restart` `g_orig_on_rep_pawn` `g_orig_pc_set_pawn` `g_orig_set_ignore_move_input` `g_orig_set_input_mode` `g_orig_set_show_mouse_cursor` `g_player_character_class` `g_player_controller_class` `g_possession_logs` `g_receiver_logs` `g_release_active_inputs` `g_set_minimap_origin` `g_snapshot_start_ms` `g_st_pc_class` `g_ui_config_logs` `g_world_first_pc` `g_world_game_viewport` | `g_actor_net_mode` `g_exe_base` `g_get_local_player` `g_last_hook` `g_object_ptr_is_a` |
| 1482-1690 | c010 | mixed (23 ue / 47 ubw names). Cut to ue: ue/actor.cpp: kPawnController kActorRootComponent kComponentRotation kComponentTranslation kCharacterMovement kCmcCharacterOwner kMoveUpdatedComponent kMoveVelocity kCmcMovementMode kCmcMaxWalkSpeed kCmcMaxAcceleration kRadToDeg Vec3 ; ue/input.cpp: kControllerControlRotation kContextMappings kMappingBytes kMappingKeyName kMaxMappingsScanned ; ue/net.cpp: kPcPlayer kActorRemoteRole kActorLocalRole session_side ; ue/reflect.cpp: read_vec3 | ubw/input_diag.cpp, ue/actor.cpp, ue/input.cpp, ue/net.cpp, ue/reflect.cpp | `g_adjust_logs` `g_client_move` `g_client_set_rotation_logs` `g_counts` `g_look` `g_orig_client_adjust_position` `g_orig_client_set_rotation` `g_orig_handle_input_look` `g_orig_input_axis` `g_orig_replicate_move` `g_orig_set_control_rotation` `g_rot_next_ms` `g_rot_row` `g_set_rotation_logs` `g_update_rotation_set_rot_ret` | `g_client_session` `g_exe_base` `g_st_pc_class` |
| 1691-1825 | c012 | mixed (2 ue / 17 ubw names). Cut to ue: ue/input.cpp: vec_len ; ue/net.cpp: kRoleAutonomousProxy | ubw/input_diag.cpp, ue/input.cpp, ue/net.cpp | `g_move_diag` `g_orig_handle_input_move` `g_orig_perform_movement` `g_orig_player_input_key` | `g_adjust_logs` `g_client_move` `g_client_session` `g_counts` `g_orig_client_adjust_position` `g_orig_replicate_move` |
| 1826-2038 | c013 | mixed (11 ue / 52 ubw names). Cut to ue: ue/actor.cpp: kComponentOwner kCmcAcceleration ; ue/input.cpp: kHitFlags kFrameNode kTrackedModifiers kInstanceSourceAction g_fkey_ctor g_key_args_ctor g_request_exit ; ue/net.cpp: kRoleAuthority ; ue/reflect.cpp: ExecThunkFn | ubw/input_diag.cpp, ue/actor.cpp, ue/input.cpp, ue/net.cpp, ue/reflect.cpp | `g_chain` `g_fkey_ctor` `g_key_args_ctor` `g_move_cmc` `g_orig_apply_modifiers` `g_orig_descend_completed` `g_orig_enter_dropship` `g_orig_evaluate_triggers` `g_orig_exec_set_actor_collision` `g_orig_execute_dynamic` `g_orig_execute_value` `g_orig_execute_void` `g_orig_modify_raw` `g_orig_move_impl` `g_orig_phys_walking` `g_orig_possessed_by` `g_orig_post_init` `g_orig_process_mapping` `g_orig_safe_move` `g_orig_set_actor_collision` `g_orig_set_collision_enabled` `g_orig_start_new_physics` `g_request_exit` | `g_client_session` `g_weak_get` |
| 2039-2140 | collision-writers-co | mixed (1 ue / 11 ubw names). Cut to ue: ue/net.cpp: side_text | ubw/input_diag.cpp, ue/net.cpp | `g_bp_node` `g_collision_logs` `g_dropship_enter_exit_logs` `g_pawn_collision_logs` | `g_character_class` `g_client_session` `g_last_hook` `g_orig_descend_completed` `g_orig_enter_dropship` `g_orig_exec_set_actor_collision` `g_orig_set_actor_collision` `g_orig_set_collision_enabled` |
| 2141-2371 | c015 | mixed (8 ue / 56 ubw names). Cut to ue: ue/actor.cpp: AttachParentFn g_get_attach_parent_actor g_world_is_paused ConstIntFn kPauseRequesters ; ue/net.cpp: g_world_num_pcs ; ue/reflect.cpp: kArrayNum array_num | ubw/dropship.cpp, ue/actor.cpp, ue/net.cpp, ue/reflect.cpp | `g_actor_tick_enabled` `g_dropship_logs` `g_dropship_watch` `g_get_attach_parent_actor` `g_orig_begin_descent` `g_orig_request_pause` `g_orig_request_unpause` `g_orig_set_state` `g_orig_skip_flight` `g_orig_start_action` `g_orig_tick_behaviours` `g_orig_tick_spline_travel` `g_orig_transport_tick` `g_pause_logs` `g_transport_dropship_class` `g_world_is_paused` `g_world_num_pcs` | `g_auto_dropship` `g_c63_on` `g_dropship_watch_armed` `g_get_game_world` `g_world_first_pc` `g_world_game_viewport` |
| 2372-2505 | c015.2 | mixed (1 ue / 14 ubw names). Cut to ue: ue/reflect.cpp: kArrayData | ubw/dropship.cpp, ue/reflect.cpp | `g_auto` | `g_auto_dropship_armed` `g_get_game_world` `g_orig_request_unpause` `g_transport_dropship_class` `g_world_first_pc` `g_world_is_paused` `g_world_num_pcs` |
| 2506-2641 | ia-look-ia-move-eval | ubw | ubw/input_diag.cpp | `g_actions` `g_eval` `g_eval_modifier` `g_in_mapping_modifiers` | `g_orig_apply_modifiers` `g_orig_execute_dynamic` `g_orig_execute_value` `g_orig_execute_void` `g_orig_process_mapping` |
| 2642-2729 | c016 | ubw | ubw/input_diag.cpp | `g_action_mods` | `g_eval` `g_eval_modifier` `g_in_mapping_modifiers` `g_orig_modify_raw` |
| 2730-2905 | c016.2 | ubw | ubw/input_diag.cpp | `g_action_eval_ret` `g_delegates_paused` `g_dumped` `g_orig_evaluate_delegates` | `g_actions` `g_client_session` `g_eval` `g_exe_base` `g_orig_evaluate_triggers` |
| 2906-3273 | c016.3 | mixed (1 ue / 76 ubw names). Cut to ue: ue/net.cpp: side | ubw/input_diag.cpp, ue/net.cpp | `g_c21_failed_logs` `g_c21_inner_seen` `g_c21_tag_logs` `g_jump` `g_orig_asc_tag_pressed` `g_orig_can_jump` `g_orig_can_jump_internal` `g_orig_char_tag_pressed` `g_orig_character_jump` `g_orig_check_jump_input` `g_orig_client_activate_failed` `g_orig_client_activate_succeed` `g_orig_internal_try_activate` `g_orig_mi_jump` `g_orig_pc_tag_pressed` `g_orig_pc_tag_released` `g_orig_server_try_activate` `g_orig_sprint_change` `g_orig_try_activate` `g_try_logs019` | `g_c65_shunt_logs` `g_client_session` |
| 3274-3358 | c019 | mixed (3 ue / 11 ubw names). Cut to ue: ue/net.cpp: SprintSide kSprintSideText sprint_side | ubw/sprint.cpp, ue/net.cpp | `g_player_pawn_logs` `g_sprint` | `g_action_mods` `g_chain` `g_collision_logs` `g_dropship_enter_exit_logs` `g_dumped` `g_own_rows_left` `g_pawn_collision_logs` |
| 3359-3636 | c014 | mixed (1 ue / 26 ubw names). Cut to ue: ue/reflect.cpp: kClassDefaultObject | ubw/pawn_collision.cpp, ue/reflect.cpp | `g_pawn_classes` `g_pawn_watch` | `g_character_class` `g_client_session` `g_counts` `g_dropship_watch` `g_get_attach_parent_actor` `g_get_game_world` `g_input_window_armed` `g_last_hook` `g_move_cmc` `g_move_diag` `g_orig_handle_input_move` `g_orig_perform_movement` `g_orig_player_input_key` `g_orig_possessed_by` `g_orig_post_init` `g_orig_set_actor_collision` `g_player_character_class` `g_player_controller_class` `g_player_pawn_logs` `g_transport_dropship_class` `g_world_first_pc` |
| 3637-3816 | c011 | ubw | ubw/input_diag.cpp | `g_correction_logs` `g_host_next_ms` `g_host_row` `g_host_rows_left` `g_mapping_lines` `g_orig_server_check_client_error` `g_orig_server_move_perform` `g_srv` | `g_client_session` `g_counts` `g_input_window_armed` `g_pawn_watch` `g_rot_next_ms` `g_rot_row` `g_rot_rows_left` |
| 3817-4158 | c019.2 | mixed (2 ue / 28 ubw names). Cut to ue: ue/actor.cpp: AddMovementInputFn ; ue/input.cpp: g_add_movement_input | ubw/sprint.cpp, ue/actor.cpp, ue/input.cpp | `g_add_movement_input` `g_mirror_logs` `g_orig_cmc_tick` | `g_actor_get_world` `g_auto` `g_auto_dropship` `g_auto_dropship_armed` `g_c21_grant_armed` `g_c63_on` `g_c65_activate_logs` `g_c65_logs` `g_c65_shunt_logs` `g_c68_logs` `g_character_class` `g_client_game_only` `g_client_move` `g_client_session` `g_context_logs` `g_counts` `g_dropship_logs` `g_dropship_watch` `g_dropship_watch_armed` `g_game_only_pending` `g_get_enhanced_input` `g_get_game_world` `g_get_local_player` `g_grant_weapon` `g_host_row` `g_host_rows_left` `g_input_mode_game_only_ctor` `g_input_window_armed` `g_mapping_lines` `g_orig_server_move_perform` `g_orig_set_input_mode` `g_pause_logs` `g_pawn_watch` `g_pawn_watch_armed` `g_player_character_class` `g_rot_next_ms` `g_rot_row` `g_rot_rows_left` `g_snapshot_armed` `g_snapshot_start_ms` `g_srv` `g_st_pc_class` `g_world_first_pc` `g_world_game_viewport` |
| 4159-4466 | c017 | mixed (11 ue / 47 ubw names). Cut to ue: ue/actor.cpp: kActorOwner GetAscFn GetPlayerControllerFn ; ue/gas.cpp: kAscAbilityNum kAscSpawnedAttributes kAscSpawnedAttributesNum kSpecAbility g_get_asc ; ue/net.cpp: g_get_player_controller ; ue/reflect.cpp: read_i32 read_f32 | ubw/attributes.cpp, ue/actor.cpp, ue/gas.cpp, ue/net.cpp, ue/reflect.cpp | `g_c17` `g_c18` `g_camera_set_class` `g_get_asc` `g_get_player_controller` `g_movement_set_class` `g_orig_add_attribute_set` `g_orig_bind_input_tag` `g_orig_camera_post_change` `g_orig_give_ability` `g_orig_init_features` `g_player_mi_class` | `g_c17_armed` `g_client_session` `g_get_game_world` `g_player_controller_class` |
| 4467-4575 | c018 | mixed (11 ue / 17 ubw names). Cut to ue: ue/gas.cpp: kAttrDataBase kAttrDataCurrent kCpfNet ; ue/reflect.cpp: kStructChildProperties kFieldNext kFieldName kPropertyFlags kPropertyOffset kFrameObject ExecThunkFn read_u64 | ubw/attributes.cpp, ue/gas.cpp, ue/reflect.cpp | `g_orig_exec_ui_only` `g_orig_onrep_spawned_attributes` | `g_c18` |
| 4576-4654 | c019.3 | ubw | ubw/sprint.cpp | - | `g_movement_set_class` `g_orig_add_attribute_set` |
| 4655-4944 | c020 | mixed (3 ue / 26 ubw names). Cut to ue: ue/actor.cpp: FindSpecFromClassFn ; ue/gas.cpp: kSpecHandle g_find_spec_from_class | ubw/attributes.cpp, ue/actor.cpp, ue/gas.cpp | `g_find_spec_from_class` `g_orig_onrep_activate_abilities` | `g_c17` `g_c18` `g_client_session` `g_get_asc` `g_orig_add_attribute_set` `g_orig_exec_ui_only` `g_orig_onrep_spawned_attributes` |
| 4945-5278 | c019.4 | mixed (5 ue / 43 ubw names). Cut to ue: ue/actor.cpp: kActorInfoAvatar ; ue/gas.cpp: kAbilityActorInfo kFunctionParmsSize avatar_of caller_chain | ubw/sprint.cpp, ue/actor.cpp, ue/gas.cpp | `g_c22_ring` `g_orig_cancel_ability` `g_orig_invoke` `g_orig_movement_post_change` `g_orig_sprint_activate` `g_orig_sprint_end` `g_sprint_ability_class` `g_sprint_activate_depth` `g_sprint_activate_thread` `g_sprint_activate_who` `g_sprint_change_depth` | `g_character_class` `g_exe_base` `g_get_game_world` `g_orig_sprint_change` `g_own_rows_left` `g_sprint` `g_weak_get` `g_world_first_pc` |
| 5279-5769 | c020.2 | mixed (8 ue / 83 ubw names). Cut to ue: ue/actor.cpp: kTransformTranslation component_pitch_yaw ; ue/gas.cpp: kAscSpecs kSpecBytes kSpecActiveCount ; ue/reflect.cpp: kStructSuper kFieldClassPrivate kPropertyElementSize | ubw/attributes.cpp, ue/actor.cpp, ue/gas.cpp, ue/reflect.cpp | `g_c20` `g_c21_grant` `g_c21_grant_lines` `g_c21_health_seen` `g_get_ammo_count` `g_get_base_aim` `g_get_equipped_item` `g_get_equipped_weapon` `g_get_eyes_view_point` `g_get_stack_count` `g_get_weapon_count` `g_orig_fire_projectile` `g_orig_interact` `g_orig_release_interaction` `g_orig_zero_adjust` | `g_actor_get_world` `g_c20_rows_left` `g_c21_camera_logs` `g_c21_grant_armed` `g_c24_local_lines` `g_c24_rpc_lines` `g_c44_fires` `g_get_asc` `g_get_game_world` `g_get_player_controller` `g_player_character_class` `g_player_mi_class` |
| 5770-5972 | c021 | mixed (2 ue / 46 ubw names). Cut to ue: ue/actor.cpp: kRowMapElement g_get_data_table_subsystem | ubw/weapons.cpp, ue/actor.cpp | `g_c21_rows_listed` `g_get_data_table_subsystem` `g_get_st_game_instance` `g_orig_add_weapon` `g_orig_assign_starting` `g_orig_exec_add_weapon` `g_orig_exec_add_weapon_by_tag` `g_orig_exec_give_by_class` `g_orig_exec_give_by_instance` `g_orig_give_by_class` `g_orig_give_by_instance` `g_orig_load_inventory` | `g_c21_grant_lines` `g_client_session` `g_get_weapon_count` `g_weak_get` |
| 5973-6367 | c021.2 | mixed (2 ue / 52 ubw names). Cut to ue: ue/gas.cpp: kAttributeChangeNew kAttributeChangeOld | ubw/weapons.cpp, ue/gas.cpp | `g_c21_adopt_logs` `g_c21_health_logs` `g_c21_in_notify` `g_c21_notify_ms` `g_c21_seen` `g_c21_set_logs` `g_health_attribute_changed` `g_health_set_class` `g_set_world_rotation` `g_weapon_def_from_tag` `g_weapon_index_from_tag` | `g_c21_camera_logs` `g_c21_grant` `g_c21_grant_armed` `g_c21_health_seen` `g_c21_inner_seen` `g_get_equipped_weapon` `g_get_game_world` `g_get_player_controller` `g_get_st_game_instance` `g_get_weapon_count` `g_grant_weapon` `g_orig_give_by_class` `g_player_mi_class` |
| 6368-6425 | c024 | ubw | ubw/fire.cpp | `g_orig_server_cancel_ability` `g_orig_server_end_ability` `g_orig_server_input_pressed` `g_orig_server_input_released` | `g_c24_local_lines` `g_c24_rpc_lines` `g_client_session` |
| 6426-7128 | c026 | mixed (22 ue / 77 ubw names). Cut to ue: ue/actor.cpp: FNameCtorFn ; ue/chan.cpp: WireString ChanOp kChanArgsNamed kChanArgs kChanOpAt kChanNameMax kChanArgMax kChanTextMax kChanTextFrame ChanArgs object_index chan_send_host chan_to_host chan_send_client chan_to_client chan_text chan_text_frame ; ue/reflect.cpp: kFNameAdd ProcessEventFn g_fname_ctor c24_fname | ubw/chan_ops.cpp, ue/actor.cpp, ue/chan.cpp, ue/reflect.cpp | `g_c24` `g_c24_name_lines` `g_c24_names_next_ms` `g_c28_shots` `g_chan_wire` `g_client_message` `g_find_function_checked` `g_fname_ctor` `g_get_weapon_by_slot` `g_orig_asc_tag_released` `g_orig_client_message` `g_orig_logout` `g_orig_server_change_name` | `g_c42_test` `g_client_session` `g_get_equipped_weapon` `g_get_game_world` `g_get_player_controller` `g_orig_asc_tag_pressed` `g_player_mi_class` |
| 7129-7270 | c024.2 | mixed (7 ue / 26 ubw names). Cut to ue: ue/actor.cpp: ViewPoint QueryParamsCtorFn LineTraceByProfileFn MemoryFreeFn g_query_params_ctor g_line_trace_by_profile ; ue/reflect.cpp: g_memory_free | ubw/fire.cpp, ue/actor.cpp, ue/reflect.cpp | `g_c24_aim` `g_line_trace_by_profile` `g_memory_free` `g_orig_deproject` `g_orig_target_location` `g_player_view_point` `g_query_params_ctor` | `g_actor_get_world` `g_client_session` |
| 7271-7822 | c025 | mixed (14 ue / 73 ubw names). Cut to ue: ue/actor.cpp: ConsoleCommandFn SetActorLocationFn SetNumericAttributeBaseFn g_set_actor_location kWorldLevels kLevelActors kC25MaxLevels kC25MaxActors kTeleportPhysics actor_location c25_dist ; ue/gas.cpp: g_set_numeric_attribute_base ; ue/input.cpp: g_pc_console_command c25_request_shot | ubw/audit_npc.cpp, ue/actor.cpp, ue/gas.cpp, ue/input.cpp | `g_c19_attrs` `g_c25` `g_c25_health_attr` `g_c25_health_attr_built` `g_c25_kill` `g_gameplay_attribute_ctor` `g_insta_kill` `g_npc_class` `g_orig_npc_death_event` `g_orig_npc_health_changed` `g_orig_npc_revived` `g_orig_npc_start_dying` `g_pc_console_command` `g_set_actor_location` `g_set_numeric_attribute_base` | `g_client_session` `g_get_game_world` `g_health_set_class` `g_memory_free` `g_orig_set_control_rotation` `g_player_character_class` `g_world_first_pc` |
| 7823-8602 | c027 | mixed (22 ue / 77 ubw names). Cut to ue: ue/actor.cpp: FindFirstObjectFn ; ue/reflect.cpp: g_find_first_object kStructPropStruct kArrayPropInner kArrayMax kMapPropValue kMapValueOffset kMapSparseSize kBoolByteOffset kBoolFieldMask kC27MaxSupers kC27MaxFields C27Kind C27KindRow kC27Kinds c27_kind c27_kind_is c27_is_leaf c27_find_prop c27_bool_ok c27_name c27_object_field | ubw/audit_npc.cpp, ue/actor.cpp, ue/reflect.cpp | `g_c27` `g_c27_dump` `g_find_first_object` `g_npc_data_class` `g_sprite_data_class` | `g_client_session` `g_get_game_world` `g_player_character_class` `g_world_first_pc` |
| 8603-8796 | c029 | mixed (2 ue / 31 ubw names). Cut to ue: ue/actor.cpp: StaticLoadObjectFn ; ue/reflect.cpp: g_static_load_object | ubw/vehicles.cpp, ue/actor.cpp, ue/reflect.cpp | `g_c29_audit` `g_equip_weapon` `g_mech_class` `g_mech_on_interaction` `g_mech_request_leave` `g_orig_enter_mech` `g_orig_leave_mech` `g_orig_leave_turret` `g_orig_mount_turret` `g_remove_weapon` `g_static_load_object` `g_turret_class` `g_turret_look_h` `g_turret_on_interaction` | `g_find_first_object` `g_sprite_data_class` |
| 8797-9041 | c029.2 | ubw | ubw/vehicles.cpp | `g_c29` | `g_c29_audit` `g_get_equipped_weapon` `g_get_weapon_count` `g_mech_class` `g_player_mi_class` `g_turret_class` `g_weak_get` |
| 9042-9075 | c029.3 | ubw | ubw/vehicles.cpp | - | `g_c29` `g_c29_audit` `g_player_mi_class` |
| 9076-9107 | c029.4 | ubw | ubw/vehicles.cpp | - | `g_c29` `g_c29_audit` `g_get_attach_parent_actor` `g_orig_leave_turret` `g_orig_mount_turret` |
| 9108-9361 | c029.5 | ubw | ubw/vehicles.cpp | - | `g_c29` `g_client_session` `g_equip_weapon` `g_get_equipped_weapon` `g_get_game_world` `g_get_player_controller` `g_get_st_game_instance` `g_mech_class` `g_mech_on_interaction` `g_mech_request_leave` `g_orig_asc_tag_pressed` `g_orig_asc_tag_released` `g_orig_give_by_class` `g_player_character_class` `g_player_mi_class` `g_remove_weapon` `g_turret_class` `g_turret_look_h` `g_turret_on_interaction` `g_weapon_def_from_tag` `g_weapon_index_from_tag` `g_world_first_pc` |
| 9362-9679 | c022 | ubw | ubw/sprint.cpp | `g_c22` | `g_c22_ring` `g_get_equipped_weapon` `g_get_game_world` `g_player_character_class` `g_player_mi_class` `g_st_pc_class` |
| 9680-9817 | c028 | mixed (8 ue / 45 ubw names). Cut to ue: ue/actor.cpp: AddComponentByClassFn FinishAddComponentFn FindComponentFn g_add_component_by_class g_finish_add_component kActorFindComponentByClass c28_find_component ; ue/net.cpp: kRoleSimulatedProxy | ubw/remote_body.cpp, ue/actor.cpp, ue/net.cpp | `g_add_component_by_class` `g_c28` `g_c28_shots` `g_finish_add_component` `g_sprite_render_class` `g_start_sequence` `g_variant_animator_class` | `g_c28_shots` |
| 9818-10328 | c034 | ubw | ubw/remote_body.cpp | `g_fit_mesh` `g_orig_frame_size_changed` | `g_add_component_by_class` `g_c28` `g_c28_shots` `g_client_session` `g_find_first_object` `g_finish_add_component` `g_orig_set_collision_enabled` `g_sprite_render_class` `g_st_pc_class` `g_start_sequence` `g_variant_animator_class` `g_weak_get` |
| 10329-10405 | c033 | mixed (2 ue / 11 ubw names). Cut to ue: ue/actor.cpp: SetHiddenInGameFn g_set_hidden_in_game | ubw/remote_body.cpp, ue/actor.cpp | `g_c33` `g_set_hidden_in_game` | `g_get_equipped_weapon` |
| 10406-10453 | c075 | ubw | ubw/remote_body.cpp | `g_c75_show_lines` | `g_mech_class` `g_set_hidden_in_game` `g_sprite_render_class` `g_variant_animator_class` |
| 10454-10653 | c028.2 | ubw | ubw/remote_body.cpp | - | `g_c28` `g_c28_shots` `g_c33` `g_client_session` `g_get_game_world` `g_orig_set_control_rotation` `g_player_character_class` `g_set_actor_location` `g_world_first_pc` |
| 10654-11162 | c023 | mixed (5 ue / 65 ubw names). Cut to ue: ue/actor.cpp: SpawnAiFromClassFn g_spawn_ai_from_class ; ue/net.cpp: kActorRepFlags kRepBit ; ue/reflect.cpp: g_uclass_class | ubw/enemies.cpp, ue/actor.cpp, ue/net.cpp, ue/reflect.cpp | `g_c23` `g_c23_audit` `g_c23_spawn` `g_c57_block_all` `g_enemy_class` `g_spawn_ai_from_class` `g_uclass_class` | `g_actor_get_world` `g_c24` `g_c24_aim` `g_c39_overkill` `g_c40_trace` `g_c50_test` `g_client_session` `g_get_game_world` `g_insta_kill` `g_line_trace_by_profile` `g_orig_set_control_rotation` `g_player_character_class` `g_query_params_ctor` `g_set_actor_location` `g_static_load_object` `g_world_first_pc` |
| 11163-11512 | c037 | ubw | ubw/enemies.cpp | `g_c37` `g_c37_off` `g_orig_post_effect` `g_st_character_class` | `g_c21_in_notify` `g_c23` `g_c23_audit` `g_c24` `g_c50_test` `g_client_session` `g_enemy_class` `g_get_game_world` `g_health_attribute_changed` `g_orig_asc_tag_pressed` `g_orig_asc_tag_released` `g_world_first_pc` |
| 11513-11966 | c040 | ubw | ubw/enemies.cpp | `g_c40` `g_orig_c40_ai_modify` `g_orig_c40_armour` `g_orig_c40_attr` `g_orig_c40_base_modify` `g_orig_c40_by_tags` `g_orig_c40_health` `g_orig_c40_modified` `g_orig_c40_process` `g_orig_c40_split` `g_orig_c40_try_trigger` `g_orig_c40_within` | `g_c23` `g_c23_spawn` `g_c24` `g_c37` `g_c40_trace` `g_client_session` `g_orig_asc_tag_pressed` `g_player_mi_class` `g_weak_get` |
| 11967-12237 | c038 | ubw | ubw/enemies.cpp | `g_bug_anim_class` `g_c38` `g_gore_class` `g_orig_bug_anim_death` `g_orig_corpse_ticket` `g_orig_deregister_ai` `g_orig_enemy_killed` `g_orig_enemy_start_dying` `g_orig_gore_death` `g_orig_release_tickets` `g_orig_try_play_death` | `g_c37` `g_client_session` `g_get_game_world` `g_npc_class` `g_world_first_pc` |
| 12238-12834 | c039 | mixed (3 ue / 100 ubw names). Cut to ue: ue/actor.cpp: ActorFloatFn g_spawn_niagara g_life_span | ubw/enemies.cpp, ue/actor.cpp | `g_c39` `g_c39_off` `g_collision_enabled` `g_collision_profile` `g_enable_corpse_box` `g_gore_location` `g_is_npc_2d` `g_life_span` `g_orig_gore_overkill` `g_orig_handle_dying` `g_orig_npc_end_play` `g_orig_overkill_effect` `g_orig_release_corpse` `g_orig_spawn_gore` `g_orig_start_overkill` `g_orig_try_play_overkill` `g_orig_update_colliders` `g_overkill_threshold` `g_spawn_niagara` | `g_actor_get_world` `g_c37` `g_c38` `g_client_session` `g_get_game_world` `g_gore_class` `g_npc_class` `g_orig_get_game_mode` `g_orig_gore_death` `g_orig_set_collision_enabled` `g_world_first_pc` |
| 12835-13112 | c032 | mixed (8 ue / 67 ubw names). Cut to ue: ue/actor.cpp: SpawnParamsCtorFn WorldSpawnActorFn g_spawn_params_ctor g_world_spawn_actor kSpawnParamsSize kSpawnCollisionOverride kSpawnAdjustAlways ; ue/reflect.cpp: g_find_function | ubw/vehicles.cpp, ue/actor.cpp, ue/reflect.cpp | `g_c32` `g_c32_pe` `g_find_function` `g_in_blocking_volume` `g_orig_equip_tac_supp` `g_orig_mech_on_interaction` `g_orig_try_throw_flare` `g_spawn_params_ctor` `g_world_spawn_actor` | `g_client_session` `g_get_data_table_subsystem` `g_get_st_game_instance` `g_mech_class` `g_weak_get` |
| 13113-13201 | c032.2 | ubw | ubw/vehicles.cpp | - | `g_c32` `g_c32_mode` `g_get_attach_parent_actor` `g_orig_equip_tac_supp` `g_orig_try_throw_flare` `g_weak_get` |
| 13202-13536 | c032.3 | ubw | ubw/vehicles.cpp | - | `g_add_movement_input` `g_c32` `g_c32_mode` `g_c32_pe` `g_client_session` `g_find_function` `g_fname_ctor` `g_get_game_world` `g_in_blocking_volume` `g_mech_class` `g_mech_on_interaction` `g_mech_request_leave` `g_orig_try_throw_flare` `g_spawn_params_ctor` `g_static_load_object` `g_uclass_class` `g_weak_get` `g_world_first_pc` `g_world_spawn_actor` |
| 13537-13880 | c030 | mixed (3 ue / 58 ubw names). Cut to ue: ue/reflect.cpp: ProcessEventFn kScriptDelegateBytes kScriptDelegateName | ubw/death.cpp, ue/reflect.cpp | `g_c30` `g_c30_mode` `g_c30_window` `g_deal_damage` `g_orig_complete_death` `g_orig_health_changed` `g_orig_player_death_event` `g_orig_process_event` `g_orig_set_auto_save` `g_orig_start_death` `g_orig_start_death_mi` | `g_client_session` `g_enemy_class` `g_get_game_world` `g_health_set_class` `g_player_character_class` `g_world_first_pc` |
| 13881-14044 | c030.2 | ubw | ubw/death.cpp | - | `g_c21_in_notify` `g_c21_notify_ms` `g_c30` `g_c30_window` `g_c32_pe` `g_c44_pe` `g_c67_pe` `g_c74_pe` `g_orig_complete_death` `g_orig_health_changed` `g_orig_player_death_event` `g_orig_process_event` `g_orig_set_auto_save` `g_orig_start_death` `g_orig_start_death_mi` `g_player_character_class` `g_weak_get` |
| 14045-14577 | c031 | mixed (22 ue / 83 ubw names). Cut to ue: ue/actor.cpp: FTextFromStringFn FTextToStringFn WidgetVisibilityFn WeakAssignFn CreateWidgetInstanceFn AddToViewportFn PanelAddChildFn UiOnlyExFn StaticFindObjectFn ; ue/reflect.cpp: g_weak_assign g_static_find_object ; ue/widget.cpp: g_ftext_from_string g_ftext_to_string g_text_block_class g_editable_text_box_class g_widget_set_visibility g_widget_set_keyboard_focus g_widget_remove_from_parent g_create_widget_instance g_add_to_viewport g_panel_add_child g_ui_only_ex | ubw/lobby_ui.cpp, ue/actor.cpp, ue/reflect.cpp, ue/widget.cpp | `g_add_to_viewport` `g_c31` `g_c31_mode` `g_c31_user32` `g_canvas_panel_class` `g_create_widget_instance` `g_editable_text_box_class` `g_ftext_from_string` `g_ftext_to_string` `g_input_mode_game_and_ui_ctor` `g_input_mode_ui_only_ctor` `g_panel_add_child` `g_panel_child_at` `g_panel_child_count` `g_panel_clear_children` `g_sco_internal` `g_sco_params_ctor` `g_slot_set_auto_size` `g_slot_set_position` `g_static_find_object` `g_text_block_class` `g_text_block_set_text` `g_ui_only_ex` `g_user_widget_class` `g_weak_assign` `g_widget_remove_from_parent` `g_widget_set_keyboard_focus` `g_widget_set_visibility` `g_widget_tree_class` | `g_input_mode_game_only_ctor` `g_orig_set_input_mode` `g_static_load_object` `g_uclass_class` `g_weak_get` |
| 14578-14745 | c031.2 | mixed (1 ue / 20 ubw names). Cut to ue: ue/input.cpp: c31_request_shot | ubw/lobby_ui.cpp, ue/input.cpp | `g_c31_script` | `g_c31_user32` `g_memory_free` `g_pc_console_command` `g_set_client_travel` |
| 14746-14811 | c031.3 | ubw | ubw/lobby_ui.cpp | - | `g_c31` `g_c31_mode` `g_exe_base` `g_get_game_world` `g_world_first_pc` |
| 14812-15928 | c041 | ubw | ubw/enemies.cpp | `g_c41` `g_c41_trace` `g_c41r` `g_c42_data_cls` `g_c42_part_cls` `g_c42_pooler_cls` `g_c42c` `g_c42g` `g_c42t` `g_get_net_guid` `g_object_from_guid` `g_orig_c41_action_end` `g_orig_c41_attack_end` `g_orig_c41_ended` `g_orig_c41_frame` `g_orig_c41_try_play` `g_orig_c41_try_timed` `g_pooler_init` | `g_actor_get_world` `g_c39` `g_c42_test` `g_client_session` `g_enemy_class` `g_get_game_world` `g_get_player_controller` `g_insta_kill` `g_npc_class` `g_set_actor_location` `g_spawn_ai_from_class` `g_static_load_object` `g_uclass_class` `g_weak_assign` `g_weak_get` `g_world_first_pc` |
| 15929-16685 | c043 | ubw | ubw/death.cpp | `g_add_local_offset` `g_c43` `g_cancel_all_abilities` `g_is_auto_save_enabled` `g_orig_release_interaction` | `g_actor_get_world` `g_c24` `g_client_session` `g_deal_damage` `g_get_player_controller` `g_get_st_game_instance` `g_orig_release_interaction` `g_player_character_class` `g_player_mi_class` `g_set_numeric_attribute_base` |
| 16686-16955 | c044 | ubw | ubw/death.cpp | `g_c44` `g_c45_phase` | `g_c43` `g_c44_fires` `g_c44_pe` `g_client_session` `g_find_function` `g_orig_process_event` `g_orig_start_death` `g_orig_start_death_mi` `g_player_mi_class` `g_weak_assign` `g_weak_get` |
| 16956-17510 | c043.2 | ubw | ubw/death.cpp | `g_c43t` | `g_c43` `g_c43_start_ms` `g_c43_test` `g_c44` `g_c44_fires` `g_c45_phase` `g_c45_test` `g_c46_test` `g_client_session` `g_deal_damage` `g_get_ammo_count` `g_get_game_world` `g_orig_set_control_rotation` `g_player_character_class` `g_set_actor_location` `g_world_first_pc` |
| 17511-17964 | c045 | mixed (2 ue / 53 ubw names). Cut to ue: ue/actor.cpp: MemoryMallocFn ; ue/reflect.cpp: g_memory_malloc | ubw/restart.cpp, ue/actor.cpp, ue/reflect.cpp | `g_c45` `g_furl_ctor` `g_furl_dtor` `g_memory_malloc` `g_memory_realloc` `g_widget_set_is_enabled` | `g_c43` `g_c43_test` `g_c44` `g_c44_pe` `g_c45_phase` `g_c45_test` `g_client_session` `g_get_game_world` `g_set_client_travel` `g_travel` `g_weak_assign` `g_weak_get` `g_widget_remove_from_parent` `g_world_first_pc` |
| 17965-17965 | c052 | ubw | ubw/lobby_ui.cpp | - | - |
| 17966-18253 | c052.2 | ubw | ubw/lobby_ui.cpp | `g_border_class` `g_border_set_brush_color` `g_border_set_padding` `g_box_set_text` `g_c52` `g_c52_test` `g_ftext_dtor` `g_orig_handle_network_failure` `g_orig_handle_travel_failure` `g_sizebox_class` `g_sizebox_set_height` `g_sizebox_set_width` `g_transition_to_level` `g_vbox_class` | `g_add_to_viewport` `g_create_widget_instance` `g_editable_text_box_class` `g_find_function` `g_ftext_from_string` `g_orig_process_event` `g_panel_add_child` `g_text_block_class` `g_travel` `g_widget_remove_from_parent` `g_widget_set_visibility` |
| 18254-18753 | c052.3 | ubw | ubw/lobby_ui.cpp | `g_c52_script` | `g_c31` `g_c31_user32` `g_c52` `g_c52_test` `g_client_game_only` `g_client_session` `g_ftext_to_string` `g_get_game_world` `g_get_st_game_instance` `g_memory_free` `g_orig_handle_network_failure` `g_orig_handle_travel_failure` `g_pc_console_command` `g_transition_to_level` `g_travel` `g_travel_armed` `g_ui_only_ex` `g_widget_set_keyboard_focus` `g_world_first_pc` |
| 18754-18780 | c052.4 | ubw | ubw/guards.cpp | `g_orig_spawn_sound_attached` `g_spawn_sound_skips` | `g_exe_base` |
| 18781-18915 | c046 | ubw | ubw/death.cpp | `g_c46` `g_get_chapter_kill_z` `g_orig_is_below_kill_threshold` | `g_c46_test` `g_get_st_game_instance` `g_set_actor_location` |
| 18916-19011 | c048 | ubw | ubw/death.cpp | `g_health_deal_damage` | `g_deal_damage` `g_exe_base` `g_memory_malloc` |
| 19012-19277 | c048.2 | ubw | ubw/death.cpp | `g_c48` `g_get_save_id` `g_orig_actor_fully_loaded` `g_orig_add_objective` `g_orig_exec_complete_death` `g_orig_load_asc` `g_orig_load_owner_pawn` `g_orig_load_settings` `g_orig_set_objective_map` `g_remove_mapping_context` | `g_actor_get_world` `g_client_session` `g_get_enhanced_input` `g_get_equipped_weapon` `g_get_local_player` `g_get_weapon_count` `g_orig_set_show_mouse_cursor` `g_player_character_class` `g_player_mi_class` `g_world_game_viewport` |
| 19278-19781 | c049 | ubw | ubw/objectives.cpp | `g_c49` `g_c49_cap` `g_c49_count_class` `g_c49_exists` `g_c49_get_subsystem` `g_c49_journal` `g_c49_library_class` `g_c49_manager_class` `g_c49_objective_class` `g_c49_test` `g_c49_test_id` `g_c49_wire` `g_c49m` `g_c53_after_ms` `g_c53_event_ms` `g_c53_rebuilt` `g_c66_saved` `g_orig_c49_add` `g_orig_c49_add_count` `g_orig_c49_add_count_sub` `g_orig_c49_add_count_sub_subs` `g_orig_c49_add_count_subs` `g_orig_c49_add_sub` `g_orig_c49_callout` `g_orig_c49_check_all` `g_orig_c49_complete` `g_orig_c49_complete_any` `g_orig_c49_complete_sub` `g_orig_c49_gate` `g_orig_c49_increment` `g_orig_c49_save` `g_orig_c49_visibility` | `g_actor_get_world` `g_client_session` `g_find_function` `g_fname_ctor` `g_get_player_controller` `g_get_st_game_instance` |
| 19782-19879 | host-snapshot-after | ubw | ubw/objectives.cpp | - | `g_c45_phase` `g_c49_count_class` `g_c49_journal` `g_c49_test` `g_c53_event_ms` `g_client_session` |
| 19880-19939 | c066 | ubw | ubw/objectives.cpp | - | `g_c49_cap` `g_c49_count_class` `g_c49_journal` `g_c66_saved` `g_client_session` |
| 19940-20394 | client-replay-steps | ubw | ubw/objectives.cpp | - | `g_actor_get_world` `g_c49` `g_c49_count_class` `g_c49_exists` `g_c49_get_subsystem` `g_c49_test` `g_c49m` `g_fname_ctor` `g_get_st_game_instance` `g_orig_c49_add` `g_orig_c49_add_count` `g_orig_c49_add_count_sub` `g_orig_c49_add_count_sub_subs` `g_orig_c49_add_count_subs` `g_orig_c49_add_sub` `g_orig_c49_callout` `g_orig_c49_complete` `g_orig_c49_complete_any` `g_orig_c49_complete_sub` `g_orig_c49_increment` `g_orig_c49_visibility` `g_weak_get` |
| 20395-20755 | c070 | ubw | ubw/objectives.cpp | `g_c70` | `g_actor_get_world` `g_c45_phase` `g_c49` `g_c49_count_class` `g_c49_get_subsystem` `g_c49_manager_class` `g_c49_test` `g_c49_test_id` `g_c49m` `g_c53_after_ms` `g_c53_rebuilt` `g_client_session` `g_fname_ctor` `g_orig_c49_add` `g_orig_c49_add_count` `g_orig_c49_add_count_sub` `g_orig_c49_add_count_sub_subs` `g_orig_c49_add_count_subs` `g_orig_c49_add_sub` `g_orig_c49_callout` `g_orig_c49_check_all` `g_orig_c49_complete` `g_orig_c49_complete_any` `g_orig_c49_complete_sub` `g_orig_c49_gate` `g_orig_c49_increment` `g_orig_c49_save` `g_orig_c49_visibility` |
| 20756-21540 | c053 | ubw | ubw/objectives.cpp | `g_c46t` | `g_actor_get_world` `g_c43` `g_c45_phase` `g_c46` `g_c46_test` `g_c49` `g_c49_cap` `g_c49_exists` `g_c49_journal` `g_c49_test` `g_c49_test_id` `g_c49m` `g_c53_after_ms` `g_c53_event_ms` `g_c53_rebuilt` `g_client_session` `g_find_function` `g_fname_ctor` `g_memory_free` `g_memory_malloc` `g_orig_is_below_kill_threshold` `g_orig_process_event` `g_orig_set_objective_map` `g_player_character_class` `g_set_actor_location` |
| 21541-21898 | c050 | ubw | ubw/weapons.cpp | `g_c50` `g_c50_interactable_class` `g_c50_item_pickup_class` `g_c50_tactical_pickup_class` `g_c50_weapon_class` `g_c50_weapon_pickup_class` `g_c54_limited_class` `g_c54_satchel_site_class` `g_c54_secret_class` `g_c54_switch_class` `g_c54_zipline_class` `g_c56_pickup_class` `g_orig_c50_disable_pickup` `g_orig_c50_equip_by_class` `g_orig_c50_equip_by_index` `g_orig_c50_pickup` `g_orig_c50_pickup_weapon` `g_orig_c50_remove_by_class` `g_orig_c50_save_checkpoint` `g_orig_c50_unequip` | `g_c62_instance` `g_client_session` `g_get_equipped_weapon` `g_get_player_controller` `g_name_entry` `g_player_mi_class` |
| 21899-22117 | c050.2 | ubw | ubw/weapons.cpp | - | `g_c50` `g_client_session` `g_get_equipped_weapon` `g_orig_c50_equip_by_class` `g_orig_c50_unequip` `g_orig_give_by_class` `g_player_mi_class` `g_remove_weapon` `g_static_find_object` `g_static_load_object` `g_uclass_class` |
| 22118-22317 | c050.3 | ubw | ubw/interact.cpp | `g_c54_track` | `g_actor_get_world` `g_c50` `g_c50_interactable_class` `g_client_session` `g_get_interface_address` `g_orig_interact` `g_orig_release_interaction` |
| 22318-22459 | c056 | ubw | ubw/interact.cpp | `g_c56` | `g_actor_get_world` `g_c56_pickup_class` `g_get_player_controller` `g_orig_c50_disable_pickup` |
| 22460-22538 | c050.4 | ubw | ubw/weapons.cpp | - | `g_c50` `g_client_session` `g_orig_c50_disable_pickup` `g_orig_c50_equip_by_class` `g_orig_c50_equip_by_index` `g_orig_c50_pickup` `g_orig_c50_pickup_weapon` `g_orig_c50_remove_by_class` `g_orig_c50_save_checkpoint` `g_orig_c50_unequip` |
| 22539-22693 | c057 | ubw | ubw/weapons.cpp | `g_c57_cdo` `g_c57_last_avatar` `g_c57_lines` `g_orig_c57_activate` `g_orig_c57_begin_switch` `g_orig_c57_can_activate` `g_orig_c57_cycle` `g_orig_c57_equip_weapon` `g_orig_c57_request` | `g_c50` `g_c62_instance` `g_client_session` `g_equip_weapon` `g_player_mi_class` `g_static_find_object` |
| 22694-22786 | c062 | ubw | ubw/weapons.cpp | `g_c62_lines` `g_orig_c62_end` `g_orig_c62_respond` | `g_c50_test` `g_c57_cdo` `g_c62_instance` `g_client_session` |
| 22787-23154 | c050.5 | ubw | ubw/weapons.cpp | `g_c50t` `g_c54t` `g_c60t` | `g_c43_test` `g_c46_test` `g_c50` `g_c50_interactable_class` `g_c50_item_pickup_class` `g_c50_test` `g_c50_weapon_pickup_class` `g_c54_satchel_site_class` `g_c54_switch_class` `g_c54_test` `g_c54_zipline_class` `g_c57_cdo` `g_c57_solo` `g_c60_host_own` `g_c60_mk1_only` `g_client_session` `g_get_interface_address` `g_get_player_controller` `g_player_mi_class` `g_set_actor_location` |
| 23155-23178 | c063 | mixed (2 ue / 7 ubw names). Cut to ue: ue/actor.cpp: RequestScreenshotFn ; ue/input.cpp: g_request_screenshot | ubw/testrun.cpp, ue/actor.cpp, ue/input.cpp | `g_c63` `g_request_screenshot` | - |
| 23179-23660 | c064 | mixed (1 ue / 44 ubw names). Cut to ue: ue/reflect.cpp: c64_find_class | ubw/testrun.cpp, ue/reflect.cpp | `g_c64` | `g_client_session` `g_find_function` `g_orig_process_event` `g_orig_set_control_rotation` `g_set_actor_location` `g_static_find_object` `g_uclass_class` |
| 23661-23995 | c067 | ubw | ubw/testrun.cpp | `g_c67` | `g_c67_pe` `g_get_game_world` `g_world_is_paused` |
| 23996-24384 | c067.2 | ubw | ubw/testrun.cpp | `g_c67ui` | `g_c63` `g_client_session` `g_find_function` `g_get_game_world` `g_orig_process_event` `g_player_character_class` `g_player_mi_class` `g_request_exit` `g_request_screenshot` `g_weak_assign` `g_weak_get` `g_world_first_pc` `g_world_is_paused` |
| 24385-24405 | hookspec | mixed (1 ue / 11 ubw names). Cut to ue: ue/hooktable.cpp: HookSpec | ubw/flags.cpp, ue/hooktable.cpp | `g_gate_c23` `g_gate_c23_c25` `g_gate_c29_c32` `g_gate_c30` `g_gate_c32` `g_gate_c40` `g_gate_c41` `g_gate_c62` `g_gate_c62_diag` `g_gate_c67_diag` `g_gate_diag` | - |
| 24406-24574 | c065 | ubw | ubw/objectives.cpp | `g_c65_node` `g_c65_object` `g_c65_trigger_class` `g_orig_c65_activate` `g_orig_c65_exec_activate` `g_orig_c65_exec_trigger` `g_orig_c65_stateful_trigger` `g_orig_c65_trigger` | `g_actor_get_world` `g_c65_activate_logs` `g_c65_log` `g_c65_logs` `g_client_session` `g_get_player_controller` `g_static_find_object` |
| 24575-24712 | c068 | ubw | ubw/objectives.cpp | `g_c68_next_ms` `g_c68_updates` `g_orig_c68_add` `g_orig_c68_init` `g_orig_c68_objective_updated` `g_orig_c68_remove` `g_orig_c68_show` `g_orig_c68_update` | `g_c68_logs` `g_get_game_world` `g_get_player_controller` `g_st_pc_class` |
| 24713-24901 | c069 | ubw | ubw/ammo.cpp | `g_c69_calls` `g_c69_get_ammo` `g_c69_get_max` `g_c69_next_ms` `g_orig_c69_add` `g_orig_c69_do_action` `g_orig_c69_perform` `g_orig_c69_request` `g_orig_c69_reset` `g_orig_c69_set` | `g_get_ammo_count` `g_get_equipped_weapon` `g_get_game_world` `g_get_player_controller` `g_player_mi_class` |
| 24902-25389 | c073 | ubw | ubw/grenades.cpp | `g_c73` `g_c73_add_item` `g_c73_find_item` `g_c73_unbind` `g_c73_wait` `g_c73_waits` `g_orig_c73_ability` `g_orig_c73_add` `g_orig_c73_consume` `g_orig_c73_remove` | `g_c73_log` `g_client_session` `g_find_spec_from_class` `g_get_asc` `g_get_game_world` `g_get_player_controller` |
| 25390-25480 | c074 | ubw | ubw/grenades.cpp | `g_c74_calls` `g_orig_c74_activate` `g_orig_c74_anim` `g_orig_c74_damage` `g_orig_c74_do_action` `g_orig_c74_explode` `g_orig_c74_init` `g_orig_c74_proj` `g_orig_c74_spawn` `g_orig_c74_spawned` `g_orig_c74_start_anims` `g_orig_c74_start_throw` `g_orig_c74_zone` | - |
| 25481-26067 | c074.2 | ubw | ubw/grenades.cpp | `g_c74_fx` `g_c74_fx_got` `g_c74_grenade` `g_c74_live` `g_c74_relayed` `g_c74_spawn_fn` `g_niagara_system_class` `g_orig_c74_cue` `g_orig_c74_niagara` `g_orig_c74_play_sound` `g_orig_c74_spawn_sound` `g_orig_c74_st_niagara` `g_set_life_span` `g_sound_base_class` | `g_actor_get_world` `g_c74_calls` `g_c74_log` `g_c74_pe` `g_client_session` `g_get_player_controller` `g_life_span` `g_orig_c74_activate` `g_orig_c74_anim` `g_orig_c74_damage` `g_orig_c74_do_action` `g_orig_c74_explode` `g_orig_c74_init` `g_orig_c74_proj` `g_orig_c74_spawn` `g_orig_c74_spawned` `g_orig_c74_start_anims` `g_orig_c74_start_throw` `g_orig_c74_zone` `g_orig_set_actor_collision` `g_spawn_niagara` `g_spawn_params_ctor` `g_static_find_object` `g_static_load_object` `g_uclass_class` `g_world_spawn_actor` |
| 26068-26221 | c071 | ubw | ubw/ammo.cpp | `g_c71_add_ammo` `g_orig_c71_add_ammunitions` | `g_c69_get_ammo` `g_c69_get_max` `g_client_session` `g_fname_ctor` `g_player_mi_class` |
| 26222-26438 | c072 | ubw | ubw/ammo.cpp | `g_c72` `g_c72_set_ammo` | `g_c69_get_ammo` `g_c69_get_max` `g_player_mi_class` |
| 26439-26910 | c075.2 | ubw | ubw/vehicles.cpp | `g_actor_class` `g_c75_client` `g_c75_lines` `g_c75_pilots` `g_c75c` `g_orig_c75_exit_pressed` `g_orig_c75_pc_exit` `g_orig_c75_world_spawn` | `g_actor_get_world` `g_client_session` `g_get_attach_parent_actor` `g_mech_class` `g_mech_on_interaction` `g_mech_request_leave` `g_orig_enter_mech` `g_orig_leave_mech` `g_orig_mech_on_interaction` `g_player_mi_class` `g_weak_get` |
| 26911-27211 | khooks | mixed: split row by row. The 5 rows with ue detours (GameEngine Init, Browse, LoadMap, Listen, Tick) go to kHooksUe[] in ue/hooktable.cpp, the other 220 to kHooksUbw[] in ubw/hooks_ubw.cpp | ue/hooktable.cpp, ubw/hooks_ubw.cpp | - | `g_gate_c23` `g_gate_c23_c25` `g_gate_c29_c32` `g_gate_c30` `g_gate_c32` `g_gate_c40` `g_gate_c41` `g_gate_c62` `g_gate_c62_diag` `g_gate_c67_diag` `g_gate_diag` `g_orig_acknowledge_possession` `g_orig_activate_current_wave` `g_orig_actor_fully_loaded` `g_orig_add_attribute_set` `g_orig_add_mapping_context` `g_orig_add_mech_input` `g_orig_add_objective` `g_orig_add_weapon` `g_orig_apply_modifiers` `g_orig_apply_ui_input_config` `g_orig_asc_tag_pressed` `g_orig_asc_tag_released` `g_orig_assign_starting` `g_orig_begin_descent` `g_orig_bind_input_tag` `g_orig_browse` `g_orig_bug_anim_death` `g_orig_c40_ai_modify` `g_orig_c40_armour` `g_orig_c40_attr` `g_orig_c40_base_modify` `g_orig_c40_by_tags` `g_orig_c40_health` `g_orig_c40_modified` `g_orig_c40_process` `g_orig_c40_split` `g_orig_c40_try_trigger` `g_orig_c40_within` `g_orig_c41_action_end` `g_orig_c41_attack_end` `g_orig_c41_ended` `g_orig_c41_frame` `g_orig_c41_try_play` `g_orig_c41_try_timed` `g_orig_c49_add` `g_orig_c49_add_count` `g_orig_c49_add_count_sub` `g_orig_c49_add_count_sub_subs` `g_orig_c49_add_count_subs` `g_orig_c49_add_sub` `g_orig_c49_callout` `g_orig_c49_check_all` `g_orig_c49_complete` `g_orig_c49_complete_any` `g_orig_c49_complete_sub` `g_orig_c49_gate` `g_orig_c49_increment` `g_orig_c49_save` `g_orig_c49_visibility` `g_orig_c50_disable_pickup` `g_orig_c50_equip_by_class` `g_orig_c50_equip_by_index` `g_orig_c50_pickup` `g_orig_c50_pickup_weapon` `g_orig_c50_remove_by_class` `g_orig_c50_save_checkpoint` `g_orig_c50_unequip` `g_orig_c57_activate` `g_orig_c57_begin_switch` `g_orig_c57_can_activate` `g_orig_c57_cycle` `g_orig_c57_equip_weapon` `g_orig_c57_request` `g_orig_c62_end` `g_orig_c62_respond` `g_orig_c65_activate` `g_orig_c71_add_ammunitions` `g_orig_c73_ability` `g_orig_c73_add` `g_orig_c73_remove` `g_orig_c74_activate` `g_orig_c74_play_sound` `g_orig_c74_proj` `g_orig_c74_spawned` `g_orig_c74_st_niagara` `g_orig_c74_zone` `g_orig_c75_exit_pressed` `g_orig_c75_pc_exit` `g_orig_c75_world_spawn` `g_orig_camera_post_change` `g_orig_can_jump` `g_orig_can_jump_internal` `g_orig_cancel_ability` `g_orig_char_tag_pressed` `g_orig_character_jump` `g_orig_check_jump_input` `g_orig_client_activate_failed` `g_orig_client_activate_succeed` `g_orig_client_adjust_position` `g_orig_client_message` `g_orig_client_restart` `g_orig_client_set_rotation` `g_orig_cmc_tick` `g_orig_complete_death` `g_orig_corpse_ticket` `g_orig_deproject` `g_orig_deregister_ai` `g_orig_descend_completed` `g_orig_enemy_killed` `g_orig_enemy_start_dying` `g_orig_enter_dropship` `g_orig_enter_mech` `g_orig_equip_tac_supp` `g_orig_evaluate_delegates` `g_orig_evaluate_triggers` `g_orig_exec_add_weapon` `g_orig_exec_add_weapon_by_tag` `g_orig_exec_complete_death` `g_orig_exec_give_by_class` `g_orig_exec_give_by_instance` `g_orig_exec_set_actor_collision` `g_orig_exec_ui_only` `g_orig_execute_dynamic` `g_orig_execute_value` `g_orig_execute_void` `g_orig_fire_projectile` `g_orig_frame_size_changed` `g_orig_game_engine_init` `g_orig_get_game_mode` `g_orig_give_ability` `g_orig_give_by_class` `g_orig_give_by_instance` `g_orig_gore_death` `g_orig_gore_overkill` `g_orig_handle_dying` `g_orig_handle_input_look` `g_orig_handle_input_move` `g_orig_handle_network_failure` `g_orig_handle_travel_failure` `g_orig_health_changed` `g_orig_init_features` `g_orig_input_axis` `g_orig_interact` `g_orig_internal_try_activate` `g_orig_invoke` `g_orig_is_below_kill_threshold` `g_orig_is_pressed` `g_orig_leave_mech` `g_orig_leave_turret` `g_orig_listen` `g_orig_load_asc` `g_orig_load_inventory` `g_orig_load_map` `g_orig_load_owner_pawn` `g_orig_load_settings` `g_orig_logout` `g_orig_mech_on_interaction` `g_orig_mi_jump` `g_orig_modify_raw` `g_orig_mount_turret` `g_orig_move_impl` `g_orig_movement_post_change` `g_orig_npc_death_event` `g_orig_npc_end_play` `g_orig_npc_health_changed` `g_orig_npc_revived` `g_orig_npc_start_dying` `g_orig_on_rep_pawn` `g_orig_onrep_activate_abilities` `g_orig_onrep_spawned_attributes` `g_orig_overkill_effect` `g_orig_pc_set_pawn` `g_orig_pc_tag_pressed` `g_orig_pc_tag_released` `g_orig_perform_movement` `g_orig_perform_spawn` `g_orig_phys_walking` `g_orig_player_death_event` `g_orig_player_input_key` `g_orig_possessed_by` `g_orig_post_effect` `g_orig_post_init` `g_orig_process_event` `g_orig_process_mapping` `g_orig_queue_spawn` `g_orig_release_corpse` `g_orig_release_interaction` `g_orig_release_tickets` `g_orig_remove_mech_input` `g_orig_replicate_move` `g_orig_request_pause` `g_orig_request_unpause` `g_orig_safe_move` `g_orig_server_cancel_ability` `g_orig_server_change_name` `g_orig_server_check_client_error` `g_orig_server_end_ability` `g_orig_server_input_pressed` `g_orig_server_input_released` `g_orig_server_move_perform` `g_orig_server_try_activate` `g_orig_set_actor_collision` `g_orig_set_auto_save` `g_orig_set_collision_enabled` `g_orig_set_control_rotation` `g_orig_set_ignore_move_input` `g_orig_set_input_mode` `g_orig_set_objective_map` `g_orig_set_show_mouse_cursor` `g_orig_set_state` `g_orig_skip_flight` `g_orig_spawn_gore` `g_orig_spawn_sound_attached` `g_orig_sprint_activate` `g_orig_sprint_change` `g_orig_sprint_end` `g_orig_st_set_player` `g_orig_start_action` `g_orig_start_death` `g_orig_start_death_mi` `g_orig_start_encounter` `g_orig_start_new_physics` `g_orig_start_overkill` `g_orig_target_location` `g_orig_tick` `g_orig_tick_behaviours` `g_orig_tick_spline_travel` `g_orig_transport_tick` `g_orig_try_activate` `g_orig_try_play_death` `g_orig_try_play_overkill` `g_orig_try_throw_flare` `g_orig_update_bindings` `g_orig_update_colliders` `g_orig_zero_adjust` |
| 27212-27436 | kcalls | mixed: split row by row. The 59 rows whose slot is a ue engine function go to kCallsUe[] in ue/hooktable.cpp, the other 152 to kCallsUbw[] in ubw/hooks_ubw.cpp | ue/hooktable.cpp, ubw/hooks_ubw.cpp | - | `g_actor_class` `g_actor_get_world` `g_actor_net_mode` `g_actor_tick_enabled` `g_add_component_by_class` `g_add_local_offset` `g_add_movement_input` `g_add_to_viewport` `g_border_class` `g_border_set_brush_color` `g_border_set_padding` `g_box_set_text` `g_bug_anim_class` `g_c42_data_cls` `g_c42_part_cls` `g_c42_pooler_cls` `g_c49_count_class` `g_c49_exists` `g_c49_get_subsystem` `g_c49_library_class` `g_c49_manager_class` `g_c49_objective_class` `g_c50_interactable_class` `g_c50_item_pickup_class` `g_c50_tactical_pickup_class` `g_c50_weapon_class` `g_c50_weapon_pickup_class` `g_c54_limited_class` `g_c54_satchel_site_class` `g_c54_secret_class` `g_c54_switch_class` `g_c54_zipline_class` `g_c56_pickup_class` `g_c65_trigger_class` `g_c69_get_ammo` `g_c69_get_max` `g_c71_add_ammo` `g_c72_set_ammo` `g_c73_add_item` `g_c73_find_item` `g_c73_unbind` `g_c74_spawn_fn` `g_camera_set_class` `g_cancel_all_abilities` `g_canvas_panel_class` `g_character_class` `g_client_message` `g_collision_enabled` `g_collision_profile` `g_create_widget_instance` `g_deal_damage` `g_editable_text_box_class` `g_enable_corpse_box` `g_enemy_class` `g_equip_weapon` `g_find_first_object` `g_find_function` `g_find_function_checked` `g_find_spec_from_class` `g_finish_add_component` `g_fit_mesh` `g_fkey_ctor` `g_fkey_destructor` `g_fname_ctor` `g_ftext_dtor` `g_ftext_from_string` `g_ftext_to_string` `g_furl_ctor` `g_furl_dtor` `g_gameplay_attribute_ctor` `g_get_ammo_count` `g_get_asc` `g_get_attach_parent_actor` `g_get_base_aim` `g_get_chapter_kill_z` `g_get_data_table_subsystem` `g_get_enhanced_input` `g_get_equipped_item` `g_get_equipped_weapon` `g_get_eyes_view_point` `g_get_game_world` `g_get_interface_address` `g_get_local_player` `g_get_net_guid` `g_get_player_controller` `g_get_save_id` `g_get_st_game_instance` `g_get_stack_count` `g_get_weapon_by_slot` `g_get_weapon_count` `g_gore_class` `g_gore_location` `g_health_attribute_changed` `g_health_deal_damage` `g_health_set_class` `g_in_blocking_volume` `g_input_mode_game_and_ui_ctor` `g_input_mode_game_only_ctor` `g_input_mode_ui_only_ctor` `g_input_receiver_class` `g_insta_kill` `g_is_auto_save_enabled` `g_is_npc_2d` `g_key_args_ctor` `g_life_span` `g_line_trace_by_profile` `g_load_synchronous` `g_local_player_class` `g_mappable_config_class` `g_mark_as_reachable` `g_mech_class` `g_mech_on_interaction` `g_mech_request_leave` `g_memory_free` `g_memory_malloc` `g_memory_realloc` `g_movement_set_class` `g_name_entry` `g_niagara_system_class` `g_npc_class` `g_npc_data_class` `g_object_from_guid` `g_object_ptr_is_a` `g_overkill_threshold` `g_panel_add_child` `g_panel_child_at` `g_panel_child_count` `g_panel_clear_children` `g_pc_console_command` `g_pc_set_player` `g_player_character_class` `g_player_controller_class` `g_player_mi_class` `g_player_view_point` `g_pooler_init` `g_query_params_ctor` `g_received_game_mode_class` `g_release_active_inputs` `g_remove_mapping_context` `g_remove_weapon` `g_request_exit` `g_request_screenshot` `g_sco_internal` `g_sco_params_ctor` `g_set_actor_location` `g_set_client_travel` `g_set_hidden_in_game` `g_set_life_span` `g_set_minimap_origin` `g_set_numeric_attribute_base` `g_set_world_rotation` `g_setup_input_component` `g_sizebox_class` `g_sizebox_set_height` `g_sizebox_set_width` `g_slot_set_auto_size` `g_slot_set_position` `g_sound_base_class` `g_spawn_ai_from_class` `g_spawn_niagara` `g_spawn_params_ctor` `g_sprint_ability_class` `g_sprite_data_class` `g_sprite_render_class` `g_st_character_class` `g_st_pc_class` `g_st_pc_tick` `g_start_sequence` `g_static_find_object` `g_static_load_object` `g_text_block_class` `g_text_block_set_text` `g_transition_to_level` `g_transport_dropship_class` `g_turret_class` `g_turret_look_h` `g_turret_on_interaction` `g_uclass_class` `g_ui_only_ex` `g_user_widget_class` `g_variant_animator_class` `g_vbox_class` `g_weak_assign` `g_weak_get` `g_weapon_def_from_tag` `g_weapon_index_from_tag` `g_widget_remove_from_parent` `g_widget_set_is_enabled` `g_widget_set_keyboard_focus` `g_widget_set_visibility` `g_widget_tree_class` `g_world_first_pc` `g_world_game_viewport` `g_world_is_paused` `g_world_num_pcs` `g_world_spawn_actor` |
| 27437-27626 | installer | mixed (5 ue / 3 ubw names). Cut to ue: ue/hooktable.cpp: resolve_all hook_gated_off log_skipped_hooks install_hooks install | ubw/flags.cpp, ue/hooktable.cpp | - | `g_auto_dropship` `g_c23_audit` `g_c23_spawn` `g_c25_kill` `g_c27_dump` `g_c28_shots` `g_c29_audit` `g_c30_mode` `g_c31_mode` `g_c32_mode` `g_c37_off` `g_c39_off` `g_c39_overkill` `g_c40_trace` `g_c41_trace` `g_c42_test` `g_c43_start_ms` `g_c43_test` `g_c45_test` `g_c46_test` `g_c49_test` `g_c49_test_id` `g_c50_test` `g_c52_test` `g_c54_test` `g_c57_solo` `g_c60_host_own` `g_c60_mk1_only` `g_c63_on` `g_c65_log` `g_c68_log` `g_c69_log` `g_c73_log` `g_c74_log` `g_client_game_only` `g_exe_base` `g_fkey_ctor` `g_fkey_destructor` `g_gate_c23` `g_gate_c23_c25` `g_gate_c29_c32` `g_gate_c30` `g_gate_c32` `g_gate_c40` `g_gate_c41` `g_gate_c62` `g_gate_c62_diag` `g_gate_c67_diag` `g_gate_diag` `g_get_game_world` `g_get_local_player` `g_grant_weapon` `g_key_args_ctor` `g_request_exit` `g_travel` `g_travel_armed` `g_world_first_pc` |

## Seams for the mixed rows

Each `mixed` row above names the `ue` definitions to cut out. The cut itself is the same in every row:

1. The listed definitions move to the named `ue/` file and are declared in `ue/core.h`.
2. Everything else in the block moves, in its current order, to the block's `ubw/` file.

None of the cut definitions calls back into the block it leaves. The remaining calls from `ue` code into `ubw` code are all in the dependency list below, each with its own seam. So a `mixed` row needs no seam beyond the cut. The three rows that need more are:

- **`khooks` and `kcalls`**: these are split row by row (see "Hook table assembly").
- **`installer`**: see the `install` row of the dependency list.
- **`c026`**: the channel. See the `c026` rows of the dependency list.

## Dependency list (every `ue` → `ubw` use) and seams

These are the places where a definition the map puts in `ue/` uses a name the map leaves in `ubw/`. The scanner found 36 such definitions. Seams use plain functions and tables only.

### Engine detours (block `detours`)

The five engine detours call per-card steps directly. The seam is one plain struct of function pointers, `EngineSteps`, declared in `ue/core.h`:

- `init_after()`
- `tick_before(engine)`, `tick_after(engine)`
- `browse_enter(engine, url)`, `browse_return(r)`
- `load_map_enter(engine, pending)`, `load_map_return(ok, pending)`
- `listen_return(ok)`

`ubw/hooks_ubw.cpp` defines one function per pointer. Its body is today's game calls, verbatim and in the same order, so the hot path still costs one indirect call per tick and the arming checks stay as they are. `dllmain` passes the struct to `ue` install, and a null pointer means no step.

| `ue` definition (line) | `ubw` names it uses today | Seam |
|---|---|---|
| `detour_game_engine_init` (389) | `g_c63_on`, `c64_allow_client_nav`, `c21_mark_health_net` | `EngineSteps.init_after`. `ubw` keeps the `g_c63_on` check inside its function. |
| `card_log_steps` (401) | `c68_step`, `c69_step`, `c73_step`, `g_c68_log`, `g_c69_log`, `g_c73_log` | Moves whole to `ubw/hooks_ubw.cpp` as the tail of `tick_after`. |
| `detour_tick` (413) | before `g_orig_tick`: `g_c63_on`, `c63_step`. After it: `own_pawn_collision_step`, `dropship_watch_step`, `auto_dropship_step`, `card017_step`, `client_snapshot_step`, `rotation_report_step`, `own_rows_step`, `card020_row_step`, `c21_grant_step`, `c21_parity_step`, `c22_step`-`c45_step` (16 steps), `c30_step`, `c32_step`, `c31_step`, and their arming globals | `tick_before` and `tick_after`. `autoinput::tick` stays in `ue` (`autoinput.cpp` is game-agnostic key injection). `travel_step` and `g_travel_armed` are `ue` (travel glue) and stay inline in the detour. |
| `detour_browse` (476) | `c45_browse_enter`, `c45_browse_return` | `browse_enter` and `browse_return`. |
| `detour_load_map` (486) | `clear_pending_starting_mappings` (card 005), `client_diag_load_map_enter` and `client_diag_load_map_return`, `c45_load_map_return`, `c52_load_map_return` | `load_map_enter` (clear the pending mappings, then client diag) and `load_map_return`. `g_last_hook`, `autoinput::load_map_enter` and the logs stay in `ue`. `clear_pending_starting_mappings` moves to `ubw/guards.cpp` with its global `g_starting_mappings_pending`. |
| `detour_listen` (501) | `c52_listen` | `listen_return`. |

### Installer (block `installer`)

| `ue` definition (line) | `ubw` names it uses today | Seam |
|---|---|---|
| `install` (27570) | its parameters are all `ubw` config (`Travel`, `AutoInput`, `GrantWeapon`, `auto_dropship`, `kill_trooper`, `dump_sprite_anims`, `audit_029`). It sets `g_auto_dropship`, `g_c25_kill`, `g_c27_dump`, `g_c29_audit` and `g_grant_weapon`, and it calls `read_card_flags`, `c52_init`, the four `resolve_*_site` functions (setup input, tick is-pressed, update rotation, action eval), `install_card_logs` and `set_hook_gates` | Move the calls up. `dllmain` calls the new `ubw::init(config...)`, which holds today's flag stores, `read_card_flags`, `c52_init`, the site resolvers and `set_hook_gates`. Then it calls `ue::install(const InstallSet&)`. `InstallSet` holds the hook spans (`kHooksUe`, `kHooksUbw`), the call spans (`kCallsUe`, `kCallsUbw`), the `EngineSteps`, the PDB identity, and a `post_install` pointer that runs `install_card_logs`. Order stays as today: identity check, `resolve_all`, flags and resolvers, `MH_Initialize`, card logs, `install_hooks`. To keep that order, `ubw::init` runs as a `pre_hooks` pointer inside `ue::install` after `resolve_all`, not before it. |
| `image_matches_pdb` (48) | `offsets::kPdbGuid`, `offsets::kPdbAge`, `offsets::Symbol` (and `Type`, which is the field `IMAGE_DEBUG_DIRECTORY::Type`, a scanner false positive) | `ue` takes the GUID, the age and the `Symbol` span through `InstallSet`. `offsets.generated.h` stays in `ubw/`. `ue/core.h` declares only `struct Symbol { const char* name; uint32_t rva; }`, the shape the generator already emits. |

### Channel (block `c026`)

| `ue` definition (line) | `ubw` names it uses today | Seam |
|---|---|---|
| `ChanOp` and `kChanArgs` (6459, 6465), `kChanArgsText` | the op list is game features (Fire, Gib, Weapons ... MechLeave) | The op list is a table row per op. `ubw/chan_ops.cpp` owns `enum class ChanOp : uint8_t` and `kChanOps[]`, whose rows are {arg count, label text, host handler, client handler}. `ue/chan.cpp` knows only `uint8_t op`, the frame layout and `kChanOps` through a span in `InstallSet` (or a `chan_init(span)` call from `ubw::init`). |
| dispatch if-chain (6898-7001) | every op handler (`c24_*`, `c43_*`, `c49_*` ... `c75_*`) | Indexed by the same table: `ue` checks `op < count` and the arg count, then calls the row's host or client handler. This is the only behavioural shape change in the split, so it is its own card (see the move order). |
| `kChanOpAt`, `ChanArgs`, `chan_to_host`, `chan_to_client`, `chan_text`, `chan_text_frame` | `kChanPrefixLen`, `kChanMaxArgs`, `chan_frame`, `chan_to_client_args`, `chan_text_chars`, `kChanPrefix` | These are channel internals that the scanner kept in the block (each has one user block). They move to `ue/chan.cpp` with their callers. `chan_to_client_args` becomes public, because c041 and c043 call it. |
| `object_index` (6587) | `kObjectIndex` | `kObjectIndex` is the `UObject::InternalIndex` offset. It moves to `ue/reflect.cpp`. |
| `chan_send_host` (6605) | `g_c24.change_name_fname` (via `c24_fname`), `g_find_function_checked`, `kVtProcessEvent` | `ue/chan.cpp` keeps its own `uint64_t g_chan_change_name_fname` cache. `c24_fname` is promoted (see "Renames"). `g_find_function_checked` and `kVtProcessEvent` move to `ue/reflect.cpp`. |
| `chan_send_client` (6633) | `g_client_message` | It is a `ue` engine slot (`APlayerController::ClientMessage`). It moves to `kCallsUe` with its hook row. |
| `sprint_side` (3312) | role read | Promoted as `SprintSide` and `kSprintSideText` into `ue/net.cpp`. It reads only the net role. It is renamed `role_side` (see "Renames"). |

### Engine typedefs and constants left in a block

These rows are `ue` engine slots whose typedef or constant sits in a `ubw` block that also uses it. The seam is the same for all of them: the typedef or constant moves with the `ue` slot into its `ue/` file, and the `ubw` user includes `ue/core.h`.

| `ue` definition (line) | Name it uses |
|---|---|
| `g_get_enhanced_input` (614), `g_get_data_table_subsystem` (5911) | `GetSubsystemFn` |
| `g_input_mode_game_only_ctor` (1124) | `InputModeCtorFn` |
| `g_world_is_paused` (2175) | `ConstBoolFn` |
| `caller_chain` (5046) | `kMaxStackFrames`, `kStackFrames` |
| `component_pitch_yaw` (5419) | `quat_pitch_yaw` |
| `SetNumericAttributeBaseFn` (7307) | `GameplayAttributeRaw` |
| `c27_bool_ok` (7984) | `kBoolFieldSize` |
| `g_spawn_niagara` (12275) | `SpawnNiagaraFn` |
| `g_find_function` (12853) | `FindFunctionFn` |
| `g_widget_set_keyboard_focus`, `g_widget_remove_from_parent` (14097-14098) | `WidgetFn` |

## Senior correction (card 061 review)

`kAscInputTagMap*` (row c016.3) and `kAscPendingBindNum` (row c017) are offsets in the game's `UADAbilitySystemComponent` (read by its `AbilityInputTagPressed` and `BindInputTagToAbilitySpec`), not the engine's `UAbilitySystemComponent`. They stay in `ubw/`, so those two rows now cut fewer names to `ue/gas.cpp`. Card S4 checks every other `ue/gas.cpp` offset the same way: an offset read only by an `AD`/`ST` class function stays in `ubw/`.

## Renames when promoted to `ue/`

These generic helpers carry a card name today. `ue/` must not name a card, so each move card renames its helper as it moves it.

| Today | In `ue/` |
|---|---|
| `c24_fname(cache, text)` | `fname_cached` (`ue/reflect.cpp`) |
| `c25_dist` | `vec_dist` (`ue/actor.cpp`) |
| `kC25MaxLevels`, `kC25MaxActors` | `kMaxLevels`, `kMaxLevelActors` (`ue/actor.cpp`) |
| `c25_request_shot`, `c31_request_shot` | `request_shot(pc, bool show_ui)`. The two are the same except for the console string (`shot` and `shot showui`), so they merge. |
| `c27_kind`, `c27_kind_is`, `c27_is_leaf`, `c27_find_prop`, `c27_bool_ok`, `c27_name`, `c27_object_field`, `C27Kind`, `C27KindRow`, `kC27Kinds`, `kC27MaxFields`, `kC27MaxSupers` | `prop_*`, `PropKind`, `PropKindRow`, `kPropKinds`, `kPropMaxFields`, `kPropMaxSupers` (`ue/reflect.cpp`) |
| `c28_find_component` | `find_component` (`ue/actor.cpp`) |
| `c64_find_class` | `find_class` (`ue/reflect.cpp`) |
| `sprint_side`, `SprintSide`, `kSprintSideText` | `role_side`, `RoleSide`, `kRoleSideText` (`ue/net.cpp`) |

The log lines keep their text. The log prefix is set by the `ubw` caller, so no route's `wait log` pattern changes.

## Gate bools

All 11 gates are declared at 24394-24404 and set once by `set_hook_gates` (27455) before the install loop.

| Gate | Set from | Rows | Side |
|---|---|---|---|
| `g_gate_diag` | `config::read_diag()` (`UBWCOOP_DIAG`) | 85 `ubw` rows and 1 `ue` row (`UWorld_Listen`) | `ue` (`ue/hooktable.cpp`). It is a generic diag switch. `ubw` reads it. Its reader `read_diag` is in `ubw/flags.cpp` (card 078: the reader's log text names card 047), so S5 passes the value into `ue::install`. |
| `g_gate_c40` | `g_c40_trace` | 11 | `ubw/flags.cpp` |
| `g_gate_c41` | `g_c41_trace` | 5 | `ubw/flags.cpp` |
| `g_gate_c23` | `g_c23_audit` | 4 | `ubw/flags.cpp` |
| `g_gate_c23_c25` | `g_c23_audit` or `g_c25_kill` | 2 | `ubw/flags.cpp` |
| `g_gate_c30` | `g_c30_mode != KillPlayer::Off` | 1 | `ubw/flags.cpp` |
| `g_gate_c32` | `g_c32_mode != SpawnMech::Off` | 1 | `ubw/flags.cpp` |
| `g_gate_c29_c32` | `g_c29_audit` or `g_gate_c32` | 2 | `ubw/flags.cpp` |
| `g_gate_c62` | `g_c50_test` | 2 | `ubw/flags.cpp` |
| `g_gate_c62_diag` | `g_c50_test` or `g_gate_diag` | 1 | `ubw/flags.cpp` (reads the `ue` `g_gate_diag`) |
| `g_gate_c67_diag` | `g_c63_on` or `g_gate_diag` | 1 | `ubw/flags.cpp` (reads the `ue` `g_gate_diag`) |

`set_hook_gates` splits the same way. `ue` sets `g_gate_diag` in `ue::install` before the `pre_hooks` pointer runs, and `ubw/flags.cpp` sets the other ten. `HookSpec.gate` stays a `const bool*`, so a row in either array can point at a gate from either side.

## Hook table assembly

`HookSpec`, `CallSpec`, `hook_gated_off`, `log_skipped_hooks`, `resolve_all` and `install_hooks` are `ue/hooktable.cpp`. The rows split into four arrays, each kept in today's order:

- `ue/hooktable.cpp`: `kHooksUe[]` (5 rows: GameEngine Init, Browse, LoadMap, Listen, Tick) and `kCallsUe[]` (59 engine-function rows).
- `ubw/hooks_ubw.cpp`: `kHooksUbw[]` (220 rows) and `kCallsUbw[]` (152 rows).

`install_hooks` becomes `install_hooks(span<const HookSpec>)`. `ue::install` calls it on `kHooksUe`, then on `kHooksUbw`. That is the same loop over two spans, with no registry. `resolve_all` does the same over the two call spans. One order change: today the first four rows are Init, Browse, LoadMap and Listen, but Tick is row 78 (line 27040). With the split, Tick is installed fifth. This is harmless, because `install` runs on the `dllmain` worker before the engine starts: the Init hook's "UGameEngine::Init fired" line comes after the install lines. The writes before the Tick hook is enabled (the comment at line 144) still happen in `install` before any row is enabled. S5 checks the log order.

Five `ubw` detours share the table with `ue` origs only through the `kHooks` rows, which carry `&g_orig_*`. No `ubw` code calls `g_orig_browse`, `g_orig_game_engine_init`, `g_orig_listen`, `g_orig_load_map` or `g_orig_tick` (checked by grep: their only uses outside `detours` are lines 26913-27040, the table). `g_orig_update_bindings` is used at 537 and in its table row, both in `ubw` blocks.

## Card 047 hazard: direct calls of detours and `g_orig_*` across sides

The check: every use of a `detour_*` or `g_orig_*` outside its defining block was listed, with its gate.

- **`ue` → `ubw`: none.** No definition the map puts in `ue/` calls a `ubw` detour or a `ubw` `g_orig_*` directly. The only `ubw` origs that `ue` code touches are the `&g_orig_*` pointers in the hook rows, which move to `kHooksUbw`.
- **`ubw` → `ue`: none.** See the previous section.

The hazard also applies inside `ubw/`, between `ubw` files. 27 uses of a gated detour or its orig sit in a block other than the one that defines it. Most stay in one `ubw` file:

- `c012`/`c010` and the IA-eval, `c016` and collision-writer blocks/`c013`: `ubw/input_diag.cpp`;
- `c029.4`/`c029`/`c029.5` and `c032.2`/`c032`: `ubw/vehicles.cpp`;
- `c030.2`/`c030`: `ubw/death.cpp`;
- `c070`/`c049`: `ubw/objectives.cpp`;
- `c050.4`/`c050`: `ubw/weapons.cpp`.

Three pairs cross `ubw` files. They all use `g_gate_diag`, and the rule for each is that the `g_orig_*` definition moves into the file of its detour:

| Detour (block, file) | Orig defined in (block, file) |
|---|---|
| `detour_perform_movement`, `detour_handle_input_move` (c014, `ubw/pawn_collision.cpp`) | `g_orig_perform_movement`, `g_orig_handle_input_move` (c012, `ubw/input_diag.cpp`) |
| `detour_server_move_perform` (c019.2, `ubw/sprint.cpp`) | `g_orig_server_move_perform` (c011, `ubw/input_diag.cpp`) |
| `detour_sprint_change` (c019.4, `ubw/sprint.cpp`) | `g_orig_sprint_change` (c016.3, `ubw/input_diag.cpp`) |

Ungated cross-file calls of a `g_orig_*` or `detour_*` are not a gate hazard, but they do decide move order: the caller's file must declare the orig from the owner's header. These are the ones the scanner found:

- `g_orig_give_by_class` (c021) from c029.5 and c050.2;
- `g_orig_set_control_rotation` (c010) from c025's `c25_face`, c028.2, c043.2 and c064;
- `g_orig_process_event` (c030) from c044, c052.2, c053, c064 and c067.2;
- `g_orig_asc_tag_pressed`/`g_orig_asc_tag_released` and `detour_asc_tag_pressed`/`detour_asc_tag_released` (c016.3, c026) from c029.5, c037, c040 and c043.2;
- `g_orig_set_collision_enabled` (c013) from c034 and c039;
- `g_orig_set_input_mode` and `g_orig_set_show_mouse_cursor` (c008) from c031 and c048.2;
- `g_orig_start_death`, `g_orig_start_death_mi` and `detour_complete_death` (c030, c030.2) from c044;
- `g_orig_set_objective_map` (c048.2) from c053;
- `detour_c49_complete` (c070) from c053;
- `detour_interact` and `detour_release_interaction` (c020.2) from c050.5;
- `g_orig_request_unpause` (c015) from c015.2, which is the same file.

These stay plain cross-file calls through each owner's `ubw/<file>.h`. The `asc_tag` pair is `ue`-shaped: its hook is `UAbilitySystemComponent` tag input. It stays `ubw` for now, because its detours carry card 026 and card 040 logic.

## Relay check (cards 062-075)

Each relay block's uses of the channel were listed, split into channel public calls (`ChanOp`, `ChanArgs`, `WireString`, `chan_send_host`, `chan_send_client`, `chan_to_host`, `chan_to_client`, `chan_text`, `chan_text_frame`, `object_index`, the `kChan*` size constants) and everything else.

| Block | Channel names used | Other names (not channel) |
|---|---|---|
| c062, c063, c066, c068, c069 | none | - |
| c064 | none | `c24_fname` (promoted to `fname_cached`) |
| c065 | `ChanOp`, `WireString`, `chan_send_client`, `chan_text`, `chan_text_frame`, `kChanTextFrame`, `kChanTextMax` | op `Activate` |
| c067.2 | none | op `Warp` (from the dispatcher), `c24_fname` |
| c070 | `ChanArgs`, `ChanOp`, `WireString`, `chan_text`, `chan_text_frame`, `kChanNameMax`, `kChanTextMax` | op `ObjectiveActors`, `g_fname_ctor`, `kFNameAdd` (`ue/reflect.cpp`). It sends through c049's `chan_send_client` (same file). |
| c071 | `ChanOp`, `WireString`, `chan_send_client`, `chan_text`, `chan_text_frame`, `kChanNameMax`, `kChanTextFrame`, `kChanTextMax`, `object_index` | op `Ammo`, `g_fname_ctor` |
| c072 | the same as c071, plus `chan_send_host` | ops `AmmoSet`, `AmmoMirror` |
| c073 | `ChanOp`, `WireString`, `chan_send_client`, `chan_text`, `chan_text_frame`, `kChanTextFrame`, `kChanTextMax` | op `GrenadeItem` |
| c074.2 | the same as c073, plus `object_index` | ops `GrenadeSpawn`, `GrenadeExplode` |
| c075 | `object_index` | - |
| c075.2 | `ChanOp`, `WireString`, `chan_send_host`, `chan_text`, `chan_text_frame`, `kChanTextFrame`, `kChanTextMax` | ops `MechEnter`, `MechLeave` |

Result: every relay from cards 062 to 075 sends only through the channel's public calls. The op names are `ChanOp` enumerators, which become `ubw/chan_ops.cpp` rows.

Older blocks do reach into channel internals, and the channel card fixes them:

- c041 and c043 call `chan_to_client_args`, which becomes public.
- c049 and the client replay steps use `kChanPrefix`, `kChanPrefixLen`, `kChanObjectiveHead` and `kChanObjectiveFrame`. The two prefix constants become public with the frame layout. The two objective constants are objective-frame sizes, so they move to `ubw/objectives.cpp`.
- c023, c037, c040 and c043 read `g_c24` (card 024's FName cache). That is fire-feature state in `ubw/fire.cpp`, not channel state.

## Move order

One card per row, leaves first. Every card:

- builds with `systemd-run --user --scope -q -p MemoryMax=12G ninja -C build -j4`;
- passes `fnlen` (no function over 60 lines);
- reruns the base routes plus the routes listed for that card.

**Base routes:**

- `host-morita` (pair, `tools/run_pair.sh`);
- `client-zipline` (pair);
- `c47-gates` (every gate path);
- `c75-solo` (solo host, the mech path).

| Card | Move | Routes beyond the base |
|---|---|---|
| S1 | `ue/core.h` and `ue/log.cpp`: move `mod/src/log.cpp` under `ue/`, add `note_skip`. Make the anonymous namespace a named internal namespace for the moved parts. | none |
| S2 | config (card 078): `config.cpp` splits in two. `ue/config.cpp` (`ue::config`) holds the environment primitives (`read_env`, `parse_u32`, `read_delay_ms`, `read_switch`, `is_tag_char`, now public), `LogTag`, `Travel`, `read_log_tag` and `read_travel`. `ubw/flags.cpp` keeps the namespace `ubw::config` and holds every reader named after a card, `read_diag` included (its log text names card 047). `read_card_flags` and `g_gate_diag` stay in `hooks.cpp` until S5. | `c47-bug` |
| S3 | `ue/reflect.cpp`: FString reading, FName, find/load object, `ProcessEvent`, `FindFunction`, `object_index`, the property walker (renamed `prop_*`), `fname_cached`, `find_class`. | `c66-connected`, `c56-solo` (objective and pickup names resolve through FName) |
| S4 | `ue/actor.cpp`, `ue/input.cpp`, `ue/gas.cpp`, `ue/widget.cpp`: the engine slots and their typedefs (the dependency rows above), `vec_dist`, `request_shot`, `find_component`. | `c65-range`, `c68-minimap`, `c74-grenades` (widgets, attributes, component lookup) |
| S5 | `ue/hooktable.cpp`: `HookSpec`, `CallSpec`, `install_hooks` and `resolve_all` over spans; `kHooksUe`/`kCallsUe` and `kHooksUbw`/`kCallsUbw`; `InstallSet`; `ubw::init`; the PDB identity passed in; `dllmain` calls `ue::install`. From S2: `read_card_flags` moves to `ubw/flags.cpp` as part of `ubw::init` (it writes about 35 globals of the `hooks.cpp` anonymous namespace, which the F cards export first), and `g_gate_diag` moves to `ue/hooktable.cpp`. Log check: the "installed N hooks" counts and the skip lines must match a baseline log line for line. Done in cards 084 (S5a, the machinery) and 089 (S5b): the tables split into `kHooksUe`/`kCallsUe` (7 hook rows, 57 call rows, in `ue/hooktable.cpp`) and `kHooksUbw`/`kCallsUbw` (219 and 154, in `ubw/hooks_ubw.cpp`); `ue::install` takes the game `InstallSet`, the PDB identity and an `InstallSteps` pair; `dllmain` calls `ubw::init`, which calls `ue::install`. `read_card_flags` and `set_hook_gates` are in `ubw/flags.cpp` and run in the `after_resolve` step. `hooks.cpp` and `hooks.h` are gone. | `c47-bug`, `c75b-pods`, plus one run with `UBWCOOP_DIAG=1` (all 86 diag rows) |
| S6 | `ue/net.cpp`: the five engine detours with `EngineSteps`, travel, `role_side`. | `c66-late-join`, `c70-late-join` (join), `c56-rejoin` (restart and travel), `c57` |
| S7 | `ue/chan.cpp` transport (frames, parse, sends, the two receiving detours, handler tables registered by `set_ops`); `ubw/chan_ops.h` is header only (the `ChanOp` enum, `kChanArgs`, typed wrappers). The handlers and the two tables moved to `ubw/chan_ops.cpp` in card 087; `ubw/chan_ops.h` declares `chan_ops()`, which returns them, and `ubw::init` passes it to `set_ops`. The if-chain becomes the indexed table, with no op renumbered. Done in card 085. | every relay route: `c57`, `c65-range`, `c66-connected`, `c69-reload`, `c71-host-first`, `c72-mirror`, `c73-client-grenade`, `c74-grenades`, `c74-respawn`, `c75b-pods`, `c56-rejoin` |
| F1 | `ubw/guards.cpp` (c003-c007, c052.4). Done in card 086. | `client-zipline` |
| F2 | `ubw/input_diag.cpp` and `ubw/pawn_collision.cpp`: c008-c016, with the three orig moves from the card 047 table. Done in card 086: the card 047 origs are defined in `ubw/input_diag.cpp` and extern in `ubw/hooks_internal.h`. | one `UBWCOOP_DIAG=1` pair run |
| F3 | `ubw/dropship.cpp` (c015*). Done in card 086. | `host-morita` |
| F4 | `ubw/sprint.cpp`, `ubw/attributes.cpp` (c017-c022). Done in card 086. | `c57` |
| F5 | `ubw/weapons.cpp`, `ubw/ammo.cpp`, `ubw/fire.cpp` (c021*, c024*, c050*, c057, c062, c069, c071, c072), and `ubw/chan_ops.cpp`. Done in card 087. | `c57`, `c69-reload`, `c71-host-first`, `c71-solo`, `c72-mirror` |
| F6 | `ubw/audit_npc.cpp`, `ubw/enemies.cpp` (c023, c025, c027, c037-c041). Done in card 087. | `c47-gates` with `UBWCOOP_ENEMYAUDIT=1` |
| F7 | `ubw/remote_body.cpp`, `ubw/vehicles.cpp` (c028*, c029*, c032*, c033, c034, c075*). Done in card 087: the card 076 mech input contexts went to `ubw/vehicles.cpp`. | `c75a-mech`, `c75b-pods`, `c75-solo` |
| F8 | `ubw/death.cpp`, `ubw/restart.cpp` (c030*, c043*-c048*, c045). Done in card 087. | `c74-respawn`, `c56-rejoin` |
| F9 | `ubw/lobby_ui.cpp` (c031*, c052*). Done in card 088. | `c66-late-join` (lobby to mission, late join) |
| F10 | `ubw/objectives.cpp`, `ubw/interact.cpp` (c049, c053, c056, c065, c066, c068, c070, client replay, c050.3). Done in card 088. | `c56-solo`, `c56-rejoin`, `c65-range`, `c66-connected`, `c66-late-join`, `c68-minimap`, `c70-late-join` |
| F11 | `ubw/testrun.cpp` (c063, c064, c067*). Done in card 088. | any scenario (they all use the c063 runner), `c75-solo` (c064 navigation) |
| F12 | `ubw/grenades.cpp` (c073, c074*). Done in card 088. | `c73-client-grenade`, `c73-diag`, `c74-grenades`, `c74-solo`, `c74-diag` |

Cards S1-S7 must run in order. F1-F12 depend only on S1-S7 and can run in any order, but F2 is easiest first because input_diag owns the origs that F4 and F5 call.

## CMake

`mod/CMakeLists.txt` lists the sources by hand:

```
add_library(ubwcoop SHARED src/config.cpp src/dllmain.cpp src/log.cpp src/proxy.cpp src/hooks.cpp
    src/autoinput.cpp src/cmdfile.cpp src/movieskip.cpp src/lobby.cpp src/dwmapi.def)
```

The flags are `/W4 /WX /EHs-c- /GR-`, and it links `minhook`. Subdirectories need only a source-list change:

- add each `src/ue/*.cpp` and `src/ubw/*.cpp` to `add_library` as it is created (keep the explicit list, not a glob, so a new file is a reviewed change);
- add `target_include_directories(ubwcoop PRIVATE src)`, so that `#include "ue/core.h"` and `#include "ubw/<file>.h"` resolve from either side.

Nothing else changes: the `.def`, the flags and the MinHook link stay. One build note: today `hooks.cpp` is one translation unit with internal linkage. After the split, the moved definitions get external linkage inside `namespace ubw::hooks::detail` or similar. Name clashes then show as link errors, not silent shadowing, which is the safer failure.

## Final layout

After card 089, `mod/src` has 61 files, 34336 lines and 1602 functions (none over 60 lines). `hooks.cpp` is gone: its tables went to `ue/hooktable.cpp` and `ubw/hooks_ubw.cpp`, and its functions to the file that is their only user. `ubw/hooks_ubw.cpp` keeps the game table set, the engine steps, the install gates, `install_card_logs`, `ubw::init`, and the helpers more than one `ubw/` file shares. The CMake list names each file by hand, as the section above says.

| File | Lines | Functions |
|---|---|---|
| `autoinput.cpp` | 465 | 23 |
| `autoinput.h` | 79 | 0 |
| `cmdfile.cpp` | 352 | 18 |
| `cmdfile.h` | 56 | 0 |
| `dllmain.cpp` | 76 | 3 |
| `dwmapi.def` | 88 | 0 |
| `lobby.cpp` | 219 | 9 |
| `lobby.h` | 40 | 0 |
| `movieskip.cpp` | 179 | 9 |
| `movieskip.h` | 16 | 0 |
| `offsets.generated.h` | 957 | 0 |
| `proxy.cpp` | 97 | 2 |
| `proxy.h` | 16 | 0 |
| `ubw/ammo.cpp` | 459 | 26 |
| `ubw/attributes.cpp` | 1122 | 54 |
| `ubw/audit_npc.cpp` | 953 | 55 |
| `ubw/chan_ops.cpp` | 141 | 12 |
| `ubw/chan_ops.h` | 64 | 0 |
| `ubw/death.cpp` | 2497 | 130 |
| `ubw/dropship.cpp` | 350 | 19 |
| `ubw/enemies.cpp` | 3000 | 173 |
| `ubw/fire.cpp` | 529 | 37 |
| `ubw/flags.cpp` | 608 | 43 |
| `ubw/flags.h` | 197 | 0 |
| `ubw/grenades.cpp` | 1136 | 70 |
| `ubw/guards.cpp` | 419 | 25 |
| `ubw/hooks_internal.h` | 3546 | 0 |
| `ubw/hooks_ubw.cpp` | 1955 | 105 |
| `ubw/init.h` | 26 | 0 |
| `ubw/input_diag.cpp` | 1568 | 96 |
| `ubw/interact.cpp` | 200 | 10 |
| `ubw/lobby_ui.cpp` | 1378 | 74 |
| `ubw/objectives.cpp` | 2424 | 151 |
| `ubw/pawn_collision.cpp` | 315 | 19 |
| `ubw/remote_body.cpp` | 906 | 42 |
| `ubw/restart.cpp` | 292 | 14 |
| `ubw/sprint.cpp` | 1219 | 72 |
| `ubw/testrun.cpp` | 1130 | 56 |
| `ubw/vehicles.cpp` | 1632 | 81 |
| `ubw/weapons.cpp` | 1460 | 88 |
| `ue/actor.cpp` | 67 | 7 |
| `ue/actor.h` | 140 | 0 |
| `ue/chan.cpp` | 214 | 18 |
| `ue/chan.h` | 111 | 0 |
| `ue/config.cpp` | 135 | 7 |
| `ue/config.h` | 68 | 0 |
| `ue/core.h` | 13 | 0 |
| `ue/gas.cpp` | 14 | 1 |
| `ue/gas.h` | 66 | 0 |
| `ue/hooktable.cpp` | 350 | 12 |
| `ue/hooktable.h` | 83 | 0 |
| `ue/input.cpp` | 37 | 3 |
| `ue/input.h` | 70 | 0 |
| `ue/log.cpp` | 134 | 8 |
| `ue/log.h` | 42 | 0 |
| `ue/net.cpp` | 95 | 8 |
| `ue/net.h` | 77 | 0 |
| `ue/reflect.cpp` | 237 | 22 |
| `ue/reflect.h` | 163 | 0 |
| `ue/widget.cpp` | 19 | 0 |
| `ue/widget.h` | 35 | 0 |
