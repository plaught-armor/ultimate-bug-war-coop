# Card 047: Install audit-only hooks only when their flag is on

Status: sent 2026-11-05 07:56 to junior ultimate-bug-war-coop-bb, after card 074 was accepted. Ready since 2026-11-04 (senior, revised: classification done, gate table below), refreshed 2026-11-05 (see "Refresh"). Not a playtest blocker.

Read `docs/STATE.md` first ("Mod code rules"). Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why

`kHooks` (`mod/src/hooks.cpp` ~22481; line numbers move, search for `kHooks[]`) had 200 entries early on 2026-11-04 and about 250 by 21:40 (recount when you start), and `install()` creates and enables every one of them on every run. Many belong to finished audit cards and only log behind an env flag, but their detours still sit on hot game paths:
- Card 040: 11 detours on the damage pipeline, including `GetGameplayAttributeValueOrDefault`, which runs on every attribute read. Each one checks `g_c40_trace` or `g_c40` state and calls the original.
- Card 041: `TryPlayAnimationAgumentedByTime`, `ProcessFrameAction`, `AnimationEnded` and the two `OnAnimationEnd` detours only log behind `g_c41_trace`. `detour_c41_try_play` is not audit-only: it calls `c41_send_attack` and must stay installed.
- Cards 015, 016, 022, 023, 029, 030, 032 and 037 are marked "log only" in their section comments. Some of them later gained real behaviour, e.g. card 030's death detours carry card 043/044 logic, so a comment is not proof.

Each installed hook is one more trampoline, one more crash surface, and one more thing that can break if the game is patched. A normal co-op run should install only the hooks that change behaviour.

## Refresh (senior, 2026-11-05)

- `kHooks` is now at hooks.cpp ~25457 and has 216 entries; `HookSpec` is at ~24107. All 102 detours in the gate table below are still in `kHooks`; none was removed or renamed.
- Cards 065, 068, 069 and 073 already install their log-only detours outside `kHooks`, through `install_card_logs` (~26017), only when their switch is on. Leave that path as it is; this card only gates `kHooks` entries.
- New audit detours have appeared in `kHooks` since the table was written. Classify each one with the same rule (doubt means functional), and add the log-only ones to `g_gate_diag`:
  - Card 057: `detour_c57_equip_weapon`, `_c57_can_activate`, `_c57_begin_switch` and `_c57_cycle` look log-only (each is a capped `c57_line()` log, then the original). `detour_c57_request` is functional, because it sets the policy and sends the equip. `detour_c57_activate` writes `g_c57_last_avatar` and `g_c62_instance`. Leave it ungated unless every reader of those two is gated or logging.
  - Card 062: `detour_c62_respond` and `_c62_end` log only behind `g_c50_test`. Gate them on a bool set from `g_c50_test`, not on `g_gate_diag`, because card 062's proof uses that switch.
  - Cards 048, 049 and 050 are accepted, so the "left ungated on purpose" list below is out of date. Check each of those detours again. If one is log-only today, gate it on `g_gate_diag`. Several of them gained real behaviour later (the card 049 journal, the card 050 and 056 pickup relays), so read each body; a name or comment is not proof.
- Recount the skip line after this; it will be above 102. Put the final table in the Report.

## Do

Senior revision (2026-11-04): the classification (old step 1) is done; it is below. Detour names are the ones in `kHooks` (hooks.cpp ~20592, 200 entries on 2026-11-04). Any entry not named below is *functional* and stays ungated.

1. **Add a gate to `HookSpec`** (hooks.cpp ~20585): a field `const bool* gate`, nullptr meaning always install. In the install loop, skip the entry when `gate != nullptr && !*gate`, and log one line with the count and names of the skipped hooks. `read_card_flags()` (~21062) runs before the loop (~21119), so the flags are set in time. `resolve_all` still resolves every symbol.
2. **Gate bools.** `HookSpec::gate` points at a `bool`, but some flags are enums or need an OR. In `read_card_flags()`, after the flags are read, set these file-scope bools once:
   - `g_gate_c40 = g_c40_trace`; `g_gate_c41 = g_c41_trace`;
   - `g_gate_c23 = g_c23_audit`; `g_gate_c23_c25 = g_c23_audit || g_c25_kill`;
   - `g_gate_c30 = g_c30_mode != config::KillPlayer::Off`;
   - `g_gate_c32 = g_c32_mode != config::SpawnMech::Off`; `g_gate_c29_c32 = g_c29_audit || g_gate_c32`;
   - `g_gate_diag`, from a new switch `UBWCOOP_DIAG` (config.cpp, the same shape as `read_switch(L"UBWCOOP_AUDIT_029", ...)`), default off. It is the one new flag: it gates the finished input and movement diagnostics, which today run ungated on hot paths (the CMC tick chain, every input key).
3. **Gate these entries** (all were checked: each only logs or counts, calls its original with the same arguments, and returns its result; no `g_orig_*` of these is used outside its detour):

| Gate | Detours |
|---|---|
| `g_gate_c40` | `detour_c40_process`, `_c40_base_modify`, `_c40_ai_modify`, `_c40_split`, `_c40_armour`, `_c40_health`, `_c40_attr`, `_c40_by_tags`, `_c40_modified`, `_c40_within`, `_c40_try_trigger` (11) |
| `g_gate_c41` | `detour_c41_try_timed`, `_c41_frame`, `_c41_ended`, `_c41_attack_end`, `_c41_action_end` (5). Not `detour_c41_try_play`: it sends attacks. |
| `g_gate_c23` | `detour_try_play_overkill`, `_update_colliders`, `_npc_end_play`, `_post_effect` (4) |
| `g_gate_c23_c25` | `detour_npc_health_changed`, `_npc_revived` (2) |
| `g_gate_c30` | `detour_set_auto_save` (1) |
| `g_gate_c32` | `detour_mech_on_interaction`, `_equip_tac_supp` (2) |
| `g_gate_c29_c32` | `detour_enter_mech`, `_leave_mech`, `_mount_turret`, `_leave_turret` (4) |
| `g_gate_diag` | Cards 010-016 and 019 movement and input diagnostics: `detour_set_ignore_move_input`, `_apply_ui_input_config`, `_handle_input_look`, `_input_axis`, `_perform_movement`, `_player_input_key`, `_handle_input_move`, `_client_set_rotation`, `_client_adjust_position`, `_replicate_move`, `_server_move_perform`, `_start_new_physics`, `_phys_walking`, `_safe_move`, `_move_impl`, `_exec_set_actor_collision`, `_start_action`, `_set_state`, `_skip_flight`, `_request_pause`, `_begin_descent`, `_process_mapping`, `_apply_modifiers`, `_modify_raw`, `_evaluate_triggers`, `_execute_value`, `_execute_void`, `_execute_dynamic`, `_evaluate_delegates`, `_transport_tick`, `_tick_behaviours`, `_tick_spline_travel`, `_listen`. Cards 019, 021 and 024 jump and ability logs: `detour_pc_tag_pressed`, `_pc_tag_released`, `_char_tag_pressed`, `_try_activate`, `_server_input_pressed`, `_server_input_released`, `_server_end_ability`, `_server_cancel_ability`, `_client_activate_failed`, `_client_activate_succeed`, `_character_jump`, `_mi_jump`, `_can_jump`, `_can_jump_internal`, `_check_jump_input`. Card 022 sprint: `detour_sprint_change`, `_sprint_activate`, `_sprint_end`, `_cancel_ability`, `_movement_post_change`, `_invoke`. Card 017: `detour_give_ability`, `_bind_input_tag`, `_init_features`, `_camera_post_change`. Card 021 grants: `detour_give_by_instance`, `_add_weapon`, `_load_inventory`, `_exec_add_weapon`, `_exec_add_weapon_by_tag`, `_exec_give_by_class`, `_exec_give_by_instance`. Card 038 counts: `detour_enemy_start_dying`, `_enemy_killed`, `_release_tickets`, `_deregister_ai`, `_corpse_ticket`, `_start_overkill`, `_release_corpse`, `_handle_dying`. (73) |

   Left ungated on purpose, because their logs are still evidence for open cards: card 048 `detour_load_owner_pawn`, `_actor_fully_loaded`, `_exec_complete_death`, `_add_objective`, `_load_settings`; card 049 `detour_c49_check_all`, `_c49_gate`, `_c49_save`; card 050 `detour_c50_remove_by_class`, `_c50_equip_by_index`, `_c50_save_checkpoint`, `_c50_pickup_weapon`, `_c50_pickup`, `_c50_disable_pickup`.

   Functional despite looking like audit (do not gate): `detour_load_asc` (calls `c50_after_load_asc`), `detour_set_objective_map` (calls `c49_snapshot`), `detour_c41_try_play`, `detour_internal_try_activate` and `detour_server_try_activate` (set `g_jump` and `g_c21_inner_seen`, which other paths read).

   Two checks while gating, since a missed reader would be a silent regression: grep that `g_jump.tag_window_until`, `g_input_window_armed` and the `g_c38.logic` counters are read only by gated detours or logging; if one is read by functional code, leave the detours that write it ungated and say so.
4. **Runs.**
   - `card047x`: a normal two-player run with no audit flags: the auto-travel, a bug kill seen on the client, one down and revive (card 043 test). Expect the skip line naming 102 hooks, and the same results as card 043.
   - `card047t`: the same run with `UBWCOOP_C40_DAMAGE_TRACE=1`, `UBWCOOP_C41_ANIM_TRACE=1` and `UBWCOOP_DIAG=1`. The card 040 and 041 trace lines and the movement diagnostic rows appear as before.
   - `card047s`: solo.

## Done when

- With no audit flags, the skip line names every gated hook, and the card 043 down and revive and the bug kill on the client still work.
- With the flags on, the card 040 and 041 traces are present.
- No crash or new dump. rc 0 on all runs. The build is clean with `/W4 /WX`, and every function is 60 lines or fewer.

## Risks

- A detour marked "log only" that is in fact needed (like `detour_c41_try_play`). Step 1's rule (doubt means functional) and the card 043 run cover this.
- MinHook's `MH_EnableHook(MH_ALL_HOOKS)` is not used here (each hook is enabled on its own), so skipping entries does not change the others.

## Report

### Change

- `HookSpec` has a new field, `const bool* gate`. nullptr means the entry is always installed.
- `install_hooks()` skips an entry when `gate != nullptr && !*gate` and installs the rest one by one. It then logs the count line, `hooks: card 047: N of 222 hooks not installed (audit flag off), M installed`, followed by `hooks: card 047 skipped: ...` lines that name every skipped hook.
- `resolve_all` is unchanged and still resolves every symbol.
- `read_card_flags()` ends with `set_hook_gates()`, which sets the gate bools once.
- New switch `UBWCOOP_DIAG` (`config::read_diag()`), default off.
- `install_card_logs` is untouched.

### Final gate table

There are 222 `kHooks` entries. 119 of them are gated. In the drive.py test runs, 118 are skipped with no audit flags, because `UBWCOOP_CMD` sets `g_c63_on` and that installs `request_pause`. A plain launch without the command file skips 119.

| Gate | Set from | Entries |
|---|---|---|
| `g_gate_c40` | `g_c40_trace` | 11, as in the table |
| `g_gate_c41` | `g_c41_trace` | 5 |
| `g_gate_c23` | `g_c23_audit` | 4 |
| `g_gate_c23_c25` | `g_c23_audit \|\| g_c25_kill` | 2 |
| `g_gate_c30` | `g_c30_mode != Off` | 1 |
| `g_gate_c32` | `g_c32_mode != Off` | 2 |
| `g_gate_c29_c32` | `g_c29_audit \|\| g_gate_c32` | 4 |
| `g_gate_diag` | `UBWCOOP_DIAG` | 86 |
| `g_gate_c62` | `g_c50_test` | 2: `detour_c62_respond`, `_c62_end` |
| `g_gate_c62_diag` | `g_c50_test \|\| diag` | 1: `detour_cancel_ability` |
| `g_gate_c67_diag` | `g_c63_on \|\| diag` | 1: `detour_request_pause` |

The diag row has 86 entries: the table's 73, minus the 4 deviations listed below, plus 17 new entries.

### Deviations from the table

- **`detour_player_input_key`: ungated.** Card 052's `c52_swallow_key` runs in it, so it is functional.
- **`detour_request_pause`: gated on `g_c63_on || diag`.** Card 067's pause handling for the command runner needs it whenever `UBWCOOP_CMD` is on.
- **`detour_cancel_ability`: gated on `g_c50_test || diag`.** Card 062's proof logs through it under `g_c50_test`.
- **`detour_bind_input_tag`: ungated.** This one was found through a crash. Card 018's `bind_entry` and card 073's `c73_bind` call `detour_bind_input_tag` directly, not through the hook. With the entry gated off, `g_orig_bind_input_tag` stayed null and the client crashed with an AV at 0x0 (new dump UECC-...851A_0000). After ungating, no dump on any run.

  This is a new hazard class: any mod code that calls a detour function directly reaches a null original when that hook is gated. I swept for direct calls to gated detours, and this was the only one.

### Refresh classification

- **Card 057.**
  - Gated on diag: `detour_c57_equip_weapon`, `_c57_can_activate`, `_c57_begin_switch`, `_c57_cycle`. Each is a capped log, then the original.
  - Ungated: `detour_c57_request`, which is functional.
  - Ungated: `detour_c57_activate`. It writes `g_c62_instance`, and `c62_before_try` reads that from the ungated `detour_internal_try_activate`; that path is live under `g_c50_test`.
- **Card 062.** `detour_c62_respond` and `_c62_end` are on `g_gate_c62` (from `g_c50_test`).
- **Cards 048, 049 and 050.** I reread every body.
  - Log-only, now gated on diag:
    - 048: `detour_load_owner_pawn`, `_actor_fully_loaded`, `_exec_complete_death`, `_add_objective`, `_load_settings`.
    - 049: `detour_c49_check_all`, `_c49_gate`, `_c49_save`.
    - 050: `detour_c50_remove_by_class`, `_c50_equip_by_index`, `_c50_save_checkpoint`, `_c50_pickup_weapon`, `_c50_pickup`.
  - Kept ungated because they have real behaviour: `detour_c50_disable_pickup` (the card 050/056 pickup relay), the other card 049 entries (the journal), `detour_c50_equip_by_class` and `detour_c50_unequip`.

### Reader checks

- **`g_jump.tag_window_until`.** It is read only through `tag_window()`. Every caller uses it to decide whether to write a log line: the card 019 and jump lines, and the `watched` test in `detour_internal_try_activate`, which only picks a log line.
- **`g_input_window_armed`.** It is read only by `detour_player_input_key` and `note_input_window_key`, and only for the card 014 row logging. `detour_player_input_key` stays ungated for card 052 anyway.
- **`g_c38.logic`.** It is read only by `c38_log_counts` and by `c38_arm_map`'s "map totals" log test.
- **Result.** None of the three feeds functional code, so the detours that write them stay gated.

### Runs

All runs ended with host and client rc 0 and the drive scenario passing (drive rc 0). No FATAL/ERROR lines and no new crash dumps.

- **card047x: no audit flags.**
  - Scenario: `c47-gates.txt`, with host and client `UBWCOOP_C43_TEST=1`.
  - Skip line: 118 on both sides, with all 118 names in the skipped lines.
  - Down and revive: the host logged `card043: host test done` and the client logged `card044: client test done`.
  - Counts: zero card040, card041, card019 and card038 lines.
- **card047b: client bug kill.**
  - Scenario: `c47-bug.txt`. Only the host has `UBWCOOP_ENEMYAUDIT=1 UBWCOOP_SPAWN_BUG=1`, so the client is fully gated.
  - Skip line: host 112, client 118.
  - Kill: the host logged `SpawnGoreResponse BP_AI_WarriorBug_C`, and the client logged `card039: client edge ... WarriorBug`.
  - The no-flag run has no bug because card 023 by design spawns the debug bug only under ENEMYAUDIT. That is why the bug kill is a separate run.
  - Its card041 lines are the functional `detour_c41_try_play` anim relay, which stays ungated.
- **card047t: flags on.**
  - Scenario: `c47-gates.txt` with `UBWCOOP_C40_DAMAGE_TRACE=1 UBWCOOP_C41_ANIM_TRACE=1 UBWCOOP_DIAG=1` on both sides.
  - Skip line: 15 on both sides.
  - Lines present, host versus card047x:
    - card041 768 against 0;
    - card038 64 against 0;
    - card019 48 against 0;
    - jump 14, input 6, dropship 24, all against 0;
    - card017 146 against 68.
  - There are no card040 lines because no bug is in this run.
- **card047v: card 040 damage trace.**
  - Scenario: `c57.txt` (the client's case laser kills the debug bug), with ENEMYAUDIT, SPAWN_BUG, C50_TEST and the three flags.
  - Skip line: 7 on both sides.
  - Host counts: 1071 card040 lines, including 8 traced hits (`card040: hit 1 begin: BP_AI_WarriorBug_C ... Health 400.0 ... origin relayed`), and 586 card041 lines.
  - Earlier attempts using the host's own unarmed burst, melee or grenade produced no damage events. Since cards 057/062 the host lands unarmed, so the hits come from the client's relayed fire. The `c47-trace.txt` scenario for that path was deleted.
- **card047s: solo.**
  - Scenario: `c71-solo.txt` (Morita pickup, fire and reload), with `SOLO=1` and `UBWCOOP_AUTODROPSHIP=1`.
  - Skip line: 118.

### Baseline warnings (all pre-existing)

- Client: `WARN card044: client no CompleteDeath ~8000 ms after StartDeath ... called here`. Also seen in c74p and c73a.
- Host in card047b and card047v: `WARN card039: host probe table full (32) -- further deaths not probed`.
- Host in card047s: `WARN autodropship: 1 PlayerController(s); unpause action in 3000 ms`.

### Checks

- `ninja -C build` builds clean with /W4 /WX.
- fnlen: hooks.cpp has 1385 functions and config.cpp 43; none is over 60 lines.
- New scenarios: `tools/scenarios/c47-gates.txt` and `tools/scenarios/c47-bug.txt`.

## Senior review (2026-11-05 09:10): accepted

- Checked card047x, card047b, card047t, card047v and card047s myself. All five drives end with `scenario passed`. logsum gives ERROR/FATAL 0. The only WARNs are the known baseline lines: the card044 `CompleteDeath` fallback, the card039 probe table being full, and the solo `autodropship` line.
- All runs started after the DLL build and deploy at 07:58:53. hooks.cpp was last written at 07:58:44, so the build includes it.
- The skip lines match the Report: 118 on both sides with no flags, 112 on the host with ENEMYAUDIT, 15 with C40+C41+DIAG, and 7 on the card 057 route.
- The only new dump, `UECC-...851A_0000` at 07:53, is the `bind_input_tag` crash the Report describes, from before the fix. No dump came after the deploy.
- fnlen: 1385 functions, 0 over 60 lines.
- I ran my own sweep for the new hazard class. I parsed the `kHooks` gate column, which gave 119 gated entries. I then checked two things:
  - No gated detour has its address taken, or is called directly, outside its table entry. The one textual hit is a comment.
  - Every use of a gated entry's `g_orig_*` pointer sits inside that entry's own detour, apart from two calls between gated detours that are always installed together:
    - `detour_c57_equip_weapon` calls `g_orig_c57_can_activate`; both are on diag.
    - `detour_c62_respond` calls `g_orig_c62_end`; both are on `g_gate_c62`.
  - The `bind_input_tag` case is the only one, as the Report says.
- All four deviations are accepted, with the reasons the Report gives. I added the hazard to the STATE mod code rules.
