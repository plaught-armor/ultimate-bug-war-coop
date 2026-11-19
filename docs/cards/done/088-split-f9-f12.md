# Card 088: Split F9-F12: lobby UI, objectives, interact, test runner, grenades

Status: sent 2026-11-05 17:14 (senior). Part of the 19:00 batch (see card 084): lighter proof, no baseline runs.

Read:

- the card 087 Review;
- card 086 Part 0, whose rules apply here unchanged;
- `docs/split-map.md` rows F9-F12, and the note under the `ubw/` table about card 066's journal.

Repo root: `/path/to/ultimate-bug-war-coop`.

## The moves

| File | Blocks |
|---|---|
| `ubw/lobby_ui.cpp` | c031*, c052-c052.3 |
| `ubw/objectives.cpp` | c049, the host snapshot, c053, c065, c066, the client replay steps, c068, c070 |
| `ubw/interact.cpp` | c050.3, c056 |
| `ubw/testrun.cpp` | c063, c064, c067* |
| `ubw/grenades.cpp` | c073, c074* |

Cards 076-081 (mech fire, the downed-interact work and the other recent blocks) are not in the map. Put each one with the feature it extends:

- mech fire (c076) goes to `ubw/fire.cpp`;
- downed and revive (c079, c081) go to `ubw/death.cpp`;
- `detour_trace_interactable` (c081) goes to `ubw/interact.cpp`.

If a block fits no file, leave it in `hooks.cpp` and list it. Add every placement to the map's `ubw/` table.

## Proof (lighter)

- **Build** (no warnings), **fnlen** (0 over 60 in every file), and the **function count check** from card 086 Part 0.6.
- **Routes:**
  - the base routes `host-morita` (`DEPLOY=1`), `client-zipline`, `c47-gates` and `c75-solo` (covers F11, the c064 navigation; every route covers the c063 runner);
  - `c66-late-join` (covers F9 and the c066 replay);
  - `c56-rejoin` (covers F10: objectives and pickups);
  - `c74-grenades` (covers F12);
  - `c81-down-zipline` (covers the placed c079/c081 blocks).
- **Log check:** for `c66-late-join` and `c74-grenades`, compare the sorted unique `card0(3[1]|49|5[236]|6[5-8]|70|7[34]):` line texts, numbers replaced by `N`, against the newest earlier log of the same route. Explain any difference.
- `logsum` gives ERROR/FATAL 0, rc 0, and there is no new crash dump.

## Done when

- The five files exist.
- Every route passes, and the function count check holds.
- The Report gives:
  - the `hooks.cpp` line count before and after;
  - the line count of each new file;
  - a list of what is left in `hooks.cpp`, by block and line count.
- The Report ends with "Report complete."

## Report

### Method

- The same scripts as cards 086 and 087 (`c88/items.py`, `c88/split.py` in the scratchpad), run on the card 087 input (`hooks.cpp` before 086, anonymous namespace removed, `set_ops(chan_ops());`) with the F1-F8 ranges and the F9-F12 ranges together. The script output for guards, input_diag, pawn_collision, dropship, sprint, weapons, ammo, chan_ops, audit_npc, enemies, remote_body and restart is byte-identical to the files in the tree. `ubw/flags.cpp` is untouched. attributes, fire, vehicles and death changed only as listed under Deviations.
- Rule 5 is applied as in 087: the 185 functions that stayed in 087 stay unless every user is now in one file. None of them had to be re-placed by hand; the script places them all again, so card 089 starts from one list (below).
- Ranges (pre-086 line numbers, from the section headers):
  - `lobby_ui`: Card 031 (overlay, script, the record-only hooks into the existing detours); Card 052 to its test driver.
  - `objectives`: Card 049 to Card 070 (host snapshot, client replay steps 3-5, Card 053, Card 065, Card 066, Card 068); the Card 053 tail after the card 047 gates.
  - `interact`: the `TraceInteractable` detour from the card 081 block; Card 050 step 3a; Card 056.
  - `testrun`: Card 063, Card 064, Card 067 and Card 067 step 2.
  - `grenades`: Card 073 and Card 074 steps 1-3.
  - `fire` gains the card 076 mech fire check; `death` gains Card 079 and the card 081 state.
- A small check confirmed that no two ranges overlap (the script assigns each item to the first range that holds its first code line).

### Part 0

1. **Linkage.** No `namespace {` in `hooks.cpp` or `ubw/`. No clash, so no renames.
2. **`ubw/hooks_internal.h`** (2885 lines before, 3496 after): 543 `extern` variables (252 `g_orig_*`), 654 prototypes, 690 constants, structs, enums, aliases and usings moved whole; 178 forward declarations dropped because the header now declares them. Moved, not copied, as in 086 and 087. The header grew, not shrank: every name a new file shares with `hooks.cpp` now crosses a file boundary. It shrinks only when card 089 gives the shared functions owners.
3. **One definition.** Each name is defined in one file. kHooks/kCalls are unchanged in `hooks.cpp`.
4. **No behaviour change.** Bodies are byte copies. No non-copy line was added in this card.
5. **Shared functions that stayed in `hooks.cpp`.** 232 in all: the 185 from 086-087 plus 47 new ones that the five new files share with another file:
`c31_construct`, `c31_note_mode`, `c31_note_ui_only`, `c46_step`, `c49_arm_map`, `c49_logout`, `c49_snapshot`, `c49_step`, `c50_relay_use`, `c50_send_use_name`, `c52_host_browse`, `c52_listen`, `c52_swallow_key`, `c54_relay_row`, `c54_switch_enabled`, `c54_track`, `c54_track_step`, `c56_arm_map`, `c56_client_disabled`, `c56_host_disabled`, `c56_replay`, `c63_init`, `c63_step`, `c64_allow_client_nav`, `c64_find_actor`, `c65_install`, `c66_dump`, `c67_note_pause`, `c67_note_pe`, `c67_reset_map`, `c67_ui_close`, `c68_install`, `c68_step`, `c73_client_step`, `c73_drop_stale`, `c73_install`, `c73_reset_map`, `c73_step`, `c74_install`, `c74_note_pe`, `c75_leave_soldier`, `c76_mark_task`, `c79_note_enter`, `c79_refuse_enter`, `c81_refuse_press`, `iface_object`, `nearest_of_class`.
   None of the 185 earlier stays was lost or moved. The owners list for card 089 is below.
6. **Function count.** Before: 1451 (card 087). After:

   | File | Lines | Functions | Over 60 |
   |---|---|---|---|
   | `hooks.cpp` | 4180 | 243 | 0 |
   | `ubw/flags.cpp` | 546 | 41 | 0 |
   | `ubw/guards.cpp` | 419 | 25 | 0 |
   | `ubw/input_diag.cpp` | 1393 | 86 | 0 |
   | `ubw/pawn_collision.cpp` | 219 | 12 | 0 |
   | `ubw/dropship.cpp` | 316 | 17 | 0 |
   | `ubw/sprint.cpp` | 918 | 44 | 0 |
   | `ubw/attributes.cpp` | 949 | 43 | 0 |
   | `ubw/weapons.cpp` | 1373 | 83 | 0 |
   | `ubw/ammo.cpp` | 403 | 24 | 0 |
   | `ubw/fire.cpp` | 454 | 32 | 0 |
   | `ubw/chan_ops.cpp` | 141 | 12 | 0 |
   | `ubw/audit_npc.cpp` | 845 | 48 | 0 |
   | `ubw/enemies.cpp` | 3000 | 173 | 0 |
   | `ubw/remote_body.cpp` | 884 | 40 | 0 |
   | `ubw/vehicles.cpp` | 1605 | 79 | 0 |
   | `ubw/death.cpp` | 1912 | 101 | 0 |
   | `ubw/restart.cpp` | 205 | 9 | 0 |
   | `ubw/lobby_ui.cpp` | 1345 | 72 | 0 |
   | `ubw/objectives.cpp` | 2293 | 143 | 0 |
   | `ubw/interact.cpp` | 200 | 10 | 0 |
   | `ubw/testrun.cpp` | 1049 | 51 | 0 |
   | `ubw/grenades.cpp` | 1002 | 63 | 0 |
   | `ubw/hooks_internal.h` | 3496 | 0 | 0 |

   The total is 1451, the same as after 087.
7. **CMake:** the five files follow `src/ubw/restart.cpp` (after flags.cpp) in the order lobby_ui, objectives, interact, testrun, grenades.
8. **New files:** each starts with a comment naming its cards and what they do. The comments of fire, vehicles and death are updated for the moves below.

### Deviations

1. **Card 076 mech fire check: `ubw/vehicles.cpp` to `ubw/fire.cpp`**, as the card says. The card 076 mech input contexts (`C76Imc` and the rest) stay in `ubw/vehicles.cpp`. `c76_mark_task` is now shared (fire and vehicles), so it stays in `hooks.cpp`.
2. **Card 079: `ubw/vehicles.cpp` to `ubw/death.cpp`.** `c79_note_enter` and `c79_refuse_enter` are called from vehicles code, so they stay in `hooks.cpp`.
3. **The card 081 block (pre-086 5363-5434) is split three ways.** `kC81Lines`, `C81State` and `g_c81` went to `ubw/death.cpp`. `TraceInteractableFn`, `g_orig_trace_interactable` and `detour_trace_interactable` went to `ubw/interact.cpp`. `detour_interact` and `detour_release_interaction` stay in `ubw/attributes.cpp`, where 087 put them. `c81_refuse_press` would go to death, but `detour_interact` calls it, so it stays in `hooks.cpp`.
4. **`ubw/attributes.cpp` changed** (1001 to 949 lines, 46 to 43 functions): the card 081 items left it, and `iface_object` is now shared with `ubw/interact.cpp`, so it stays in `hooks.cpp`; `kIfaceGetObject` and `IfaceGetObjectFn` moved to the header.
5. **The card 047 install gates** (`g_gate_*`, pre-086 23775-23788) sit between Card 053's two parts but belong to no card 088 block. They stay in `hooks.cpp`.

### Line counts

| File | Lines |
|---|---|
| `hooks.cpp` before (start of 088) | 10332 |
| `hooks.cpp` after | 4180 |
| `ubw/hooks_internal.h` before / after | 2885 / 3496 |
| `ubw/lobby_ui.cpp` | 1345 |
| `ubw/objectives.cpp` | 2293 |
| `ubw/interact.cpp` | 200 |
| `ubw/testrun.cpp` | 1049 |
| `ubw/grenades.cpp` | 1002 |
| `ubw/fire.cpp` before / after | 363 / 454 |
| `ubw/vehicles.cpp` before / after | 1903 / 1605 |
| `ubw/death.cpp` before / after | 1763 / 1912 |
| `ubw/attributes.cpp` before / after | 1001 / 949 |

### What is left in `hooks.cpp`

Of the 4180 lines, 889 are blocks that belong to no card 088 file and 3257 are the 232 shared functions (with their forward declarations). The other 34 are the includes, namespace lines and blank lines between items.

**Blocks:**

| Block | Lines | Items |
|---|---|---|
| Auto-travel state (the `ue/net.cpp` and `ubw/flags.cpp` cut in the section map) | 125 | 98 |
| Engine detour steps (`ue::net::EngineSteps`, set in `install()`) | 102 | 7 |
| Card 047 install gates (`g_gate_*`) | 13 | 11 |
| `kHooks` | 300 | 1 |
| `kCalls` | 293 | 4 |
| `install()` | 56 | 1 |
| **Total** | 889 | 122 |

**Shared functions, by the section they came from** (pre-086 section headers):

| Origin section (pre-086 header) | Lines | Items |
|---|---|---|
| Card 043: downed and revive (first pass) | 223 | 19 |
| Card 045: co-op restart after game over | 190 | 12 |
| Card 027: Morita trooper sprite animation dump (UBWCOOP_DUMP_SPRITE_AN | 149 | 10 |
| Card 050: weapon sync | 143 | 10 |
| Card 025: NPC list and a test kill of the nearest NPC trooper (UBWCOOP | 116 | 8 |
| Card 079: a mech is not a card 043 player | 106 | 6 |
| Card 076: mech fire in co-op, step 1 check (log only, UBWCOOP_C76_LOG= | 103 | 7 |
| Card 021: UBWCOOP_GRANT_WEAPON test loadout | 98 | 7 |
| Card 050 step 3a: the client's Use relayed to its server copy's Intera | 94 | 6 |
| Card 043 test driver (UBWCOOP_C43_TEST=1) | 89 | 8 |
| Card 073: where a client's grenade lives (log, UBWCOOP_C73_LOG=1) and  | 88 | 5 |
| Card 074 steps 2-3: the host's grenade throws are shown on every clien | 60 | 3 |
| Card 072: the client owns its ammunition counts; its server copy mirro | 60 | 3 |
| Card 030: player death audit (log only, plus a host test driver) | 59 | 3 |
| Card 069: why the client has no reserve ammo and cannot reload (log on | 59 | 3 |
| Card 046: kill floor in co-op | 53 | 5 |
| Card 056: a placed pickup disabled on the host is disabled on the clie | 53 | 4 |
| Card 020: server-side parity audit | 52 | 6 |
| Card 014: player-pawn collision bit at cold points; the client's own-p | 50 | 4 |
| Card 026: the card 024 fire relay over the mod channel | 48 | 4 |
| Card 024 step 3: aim a server copy's shots from that client's own view | 48 | 5 |
| Card 044: the client's game over, the death screen's buttons | 48 | 3 |
| Card 012: the client's own local moves | 46 | 3 |
| Card 067: the host's mission brief (UBWCOOP_CMD=1) | 46 | 3 |
| Card 067 step 2: `ui continue` | 46 | 3 |
| Card 062 audit (log only, UBWCOOP_C50_TEST): why the host's own switch | 45 | 3 |
| Card 068: why the client minimap shows no icons (log only, UBWCOOP_C68 | 45 | 2 |
| Card 076: a client pilot's mech input contexts | 44 | 3 |
| Card 048 step 1: checkpoint audit (log only) and step 3: the client's  | 44 | 3 |
| Card 023: enemy replication audit (UBWCOOP_ENEMYAUDIT=1, log only) | 43 | 3 |
| Card 053: the client's objective map and list rebuilt to the host's ch | 42 | 3 |
| Card 022: player pawn body audit (log only) and the GA_Sprint value's  | 39 | 2 |
| Card 052 test driver (UBWCOOP_C52_TEST): drives the panel with real ke | 39 | 3 |
| Card 019: client movement attribute re-init | 35 | 2 |
| Card 029: weapon, mech and turret audit (log only) | 34 | 2 |
| Card 013: the movement chain below PerformMovement, collision writers, | 33 | 3 |
| Card 029 activations: one line per (ability class, owner pawn side, se | 33 | 1 |
| Card 050 test driver (UBWCOOP_C50_TEST=1) | 33 | 1 |
| Card 037: enemy health on the client | 32 | 2 |
| Card 064: goto and look for the test runner (UBWCOOP_CMD=1) | 32 | 2 |
| Card 011: mapping-context contents, host-side server move rows | 31 | 2 |
| Card 031 hooks into the existing detours: record only, never change th | 31 | 2 |
| Client replay (steps 3-5) | 30 | 1 |
| Card 032 detours: log only; every argument passes through unchanged. | 28 | 1 |
| Client input receiver and client input diagnostics (card 008) | 27 | 2 |
| Card 048 step 0: the kill-floor hit ignores armour | 26 | 1 |
| Card 019: GA_Sprint counters per pawn side (state; the hooks are furth | 25 | 3 |
| Card 075: co-op mechs | 25 | 2 |
| Client GameMode-null guard for ASTPlayerController::SetupInputComponen | 24 | 2 |
| Card 019: the client's move input on the server copy of its pawn | 23 | 2 |
| Host snapshot after a checkpoint (step 5) | 23 | 1 |
| Card 017: the client pawn's GAS state (abilities, attribute sets, inpu | 21 | 2 |
| Card 024: client fire relay | 21 | 2 |
| Card 050 client: apply the host's list | 21 | 2 |
| Card 065 step 1: which level triggers fire on which machine (log only, | 21 | 1 |
| Card 018: client camera attribute re-init and the UIOnly caller | 18 | 1 |
| Card 019: GA_Sprint lifecycle, its gameplay effect and the speed path | 16 | 1 |
| Card 038: enemies die on the client | 16 | 2 |
| Card 032 host driver | 15 | 1 |
| Card 021: how weapons reach a player (log-only) | 14 | 2 |
| Card 031: lobby overlay render test (UBWCOOP_OVERLAY_TEST) | 13 | 1 |
| Card 029 audit state | 12 | 2 |
| Card 029 test driver on the machine's own pawn | 12 | 1 |
| Card 016: jump and sprint call chain (log only, capped per map) | 11 | 3 |
| Card 049: co-op objectives on the client | 11 | 1 |
| Card 030 detours: log only; every argument passes through unchanged. | 9 | 1 |
| Card 041: bug attack animations on the client | 8 | 1 |
| Card 016: action-level trigger evaluation | 7 | 1 |
| Card 028 test views (UBWCOOP_C28_SHOTS=1) | 5 | 1 |
| IA_Look / IA_Move evaluation (local player input only: PlayerInput exi | 4 | 1 |
| Client gate for server-only encounter and spawn logic (card 006) | 3 | 1 |
| Client look and movement diagnostics (card 010) | 3 | 1 |
| Collision writers (cold paths; capped per map) | 3 | 1 |
| **Total** | 3257 | 234 |

### Owners list for card 089

From `stay_users` in `c88/out/split.json`: for each stayed function, the files that use it. 89 of the 232 now have exactly one user file and no user left in `hooks.cpp`, so each can go to that file as it stands:

| Owner file | Count | Functions |
|---|---|---|
| `ubw/attributes.cpp` | 8 | `c24_aim_begin`, `c24_aim_end`, `c31_note_ui_only`, `c43_interact_edge`, `c76_fire`, `c76_mark_task`, `c81_refuse_press`, `card019_movement_fix` |
| `ubw/audit_npc.cpp` | 3 | `c29_sets`, `c37_note_npc`, `c38_note_npc` |
| `ubw/death.cpp` | 14 | `c30_nearest_enemy`, `c32_note_pe`, `c45_client_screen`, `c45_respawn_check`, `c45_test_on_foot`, `c45_test_restart`, `c46_step`, `c49_snapshot`, `c50_after_load_asc`, `c50_relay_use`, `c50_step`, `c67_note_pe`, `c74_note_pe`, `c79_mech_death` |
| `ubw/dropship.cpp` | 2 | `c67_note_pause`, `c67_ui_close` |
| `ubw/fire.cpp` | 4 | `c43_logout`, `c49_logout`, `c50_logout`, `c76_client_imc` |
| `ubw/grenades.cpp` | 4 | `c76_damage`, `c76_fx`, `c76_spawn`, `object_name_into` |
| `ubw/input_diag.cpp` | 10 | `c21_log_tag_pressed`, `c21_note_activation`, `c24_log_local_activation`, `c29_note_activation`, `c31_note_mode`, `c43_refuse_tag`, `c44_context`, `c62_after_try`, `c62_before_try`, `c76_try` |
| `ubw/objectives.cpp` | 5 | `c46_add_follow`, `c46_hurt`, `c46_mark_warp`, `c46_move`, `c50_chapter_frame` |
| `ubw/pawn_collision.cpp` | 7 | `c52_swallow_key`, `note_input_window_key`, `note_local_move`, `note_move_entry`, `note_move_exit`, `start_own_rows`, `watched_pawn` |
| `ubw/remote_body.cpp` | 2 | `c26_firing`, `c43_sprite_key` |
| `ubw/sprint.cpp` | 22 | `c23_arm_map`, `c25_arm_map`, `c27_arm_map`, `c28_arm_map`, `c29_arm_map`, `c30_arm_map`, `c32_arm_map`, `c37_arm_map`, `c38_arm_map`, `c41_arm_map`, `c49_arm_map`, `c50_arm_map`, `c62_note_cancel`, `c67_reset_map`, `c69_reset_map`, `c73_reset_map`, `host_row_step`, `log_move_diag`, `reset_card016_map`, `reset_card017_map`, `reset_card020_map`, `reset_move_diag_map` |
| `ubw/testrun.cpp` | 2 | `c56_disconnect`, `c66_dump` |
| `ubw/vehicles.cpp` | 2 | `c79_note_enter`, `c79_refuse_enter` |
| `ubw/weapons.cpp` | 4 | `c56_client_disabled`, `c56_host_disabled`, `c56_replay`, `c72_seed` |

Of the rest:

- 32 are used only from `hooks.cpp` itself: the `*_step` and `*_install` functions called from the engine step tables and `install()` (for example `c49_step`, `c63_init`, `c73_install`, `c79_step`), plus `c21_mark_health_net`, `c24_reset_map`, `c27_find_any`, `c43_find_pawn`, `c45_browse_enter`, `c45_browse_return`, `c50_send_use_name`, `c52_host_browse`, `c52_listen` and `card019_sync_aggregators`. They stay declared in the header and can go back to their feature file, since the tables only need the declaration.
- 29 have one `ubw/` user plus a user still in `hooks.cpp`. Same case: one owner file, called from the tables.
- 82 are used from two or more `ubw/` files and need a home chosen in card 089.

### Proof

- **Build:** `systemd-run --user --scope -q -p MemoryMax=12G ninja -C build -j4` gives rc 0 with no warnings (/W4 /WX).
- **fnlen and the count:** see Part 0 item 6.
- **`docs/split-map.md`:** rows F9-F12 say "Done in card 088". The `ubw/` table rows for the five new files, `ubw/fire.cpp`, `ubw/vehicles.cpp`, `ubw/death.cpp` and `ubw/attributes.cpp` say where c076 fire, c079, the card 081 parts and `detour_trace_interactable` now live.
- **Routes** (DLL `12e596c7a6f98b8f`, two lanes):

  | Run | Route | Time | rc host/client | ERROR/FATAL | WARN | Crash dumps |
  |---|---|---|---|---|---|---|
  | c88a | `host-morita` DEPLOY=1 | 112 s | 0 / 0 | 0 | 0 / 0 | none |
  | c88b | `client-zipline` | 155 s | 0 / 0 | 0 | 0 / 0 | none |
  | c88c | `c47-gates` (`UBWCOOP_C43_TEST`, `UBWCOOP_ENEMYAUDIT`) | 193 s | 0 / 0 | 0 | 0 / 1 | none |
  | c88d | `c75-solo` | 92 s | 0 / (solo) | 0 | 0 | none |
  | c88e | `c66-late-join` | 413 s | 0 / 0 | 0 | 1 / 0 | none |
  | c88f | `c56-rejoin` | 175 s | 0 / 0 | 0 | 0 / 0 | none |
  | c88g | `c74-grenades` | 184 s | 0 / 0 | 0 | 0 / 0 | none |
  | c88h | `c81-down-zipline` | 93 s | 0 / 0 | 0 | 0 / 0 | none |

  Every drive rc is 0. The c88c client WARN is the known card044 "client no CompleteDeath" (also in c86c). The c88e host WARN is `autodropship: 1 PlayerController(s); unpause action in 3000 ms`, the same line as in the c84i baseline (the client joins late).
- **Log check** (`card0(3[1]|49|5[236]|6[5-8]|70|7[34]):`, ids and counts normalised):
  - c88e against c84i (the newest earlier `c66-late-join`): client identical. Host has two extra lines, `card067: ProcessEvent WB_Cutscenes_C.SequenceEvent__ENTRYPOINTWB_Cutscenes` and `WB_Cutscenes_C.Finished_*`.
  - c88g against c85g (the newest earlier `c74-grenades`): host lacks the same two lines; client lacks one `card067: ProcessEvent WB_Stack_C.OnAnalogValueChanged`.
  - Both differences are the card 067 ProcessEvent log, which prints UI events as they fire. The intro cutscene sends its sequence and Finished events only if it plays to its end before it is destroyed. In c88g it was destroyed 2.8 s after its first Tick; in c85g and c88e it ran to Finished. Of 461 host logs in `logs/`, only some have these lines, so it changes from run to run. `OnAnalogValueChanged` is one analog input event on the main menu stack. The two runs differ in opposite directions, and every other line in the pattern is identical, so neither is a behaviour change.
  - Extra check: `card0(79|81):` in c88h against c81a (the card 081 route before the split) is identical on both sides.

### Review points

1. The header grew from 2885 to 3496 lines. It will shrink only in card 089, when the 232 shared functions get owners; the list above is the input for that.
2. Three placements were not in the card table: `c81_refuse_press` and `iface_object` stay in `hooks.cpp` under rule 5, and the card 047 install gates stay because no card 088 block owns them.
3. Auto-travel state and the engine steps (227 lines) still sit in `hooks.cpp`. The section map sends them to `ue/net.cpp`, `ue/hooktable.cpp` and `ubw/flags.cpp`, which no card has done yet.

Report complete.

## Senior review

Senior, 2026-11-05 17:34. **Accepted.**

- Checked: the eight drive logs (c88a-h) end with `scenario passed`, and logsum gives ERROR/FATAL 0 for each. The newest crash dump in both prefixes is still c79a's. The DLL in `build/`, `game/` and `game2/` is `12e596c7a6f98b8f`, built after the last source edit. There is no `namespace {` in `hooks.cpp` or `ubw/`.
- The card067 ProcessEvent difference in c88e/c88g is accepted as timing (the intro cutscene's Finished event), because the two runs differ in opposite directions.
- Deviations 1-5 are accepted, including the three-way split of the card 081 block and the card 047 gates staying in `hooks.cpp` (card 089 moves `g_gate_*` with the tables).
- Card 089 starts from the owners list in this Report: the 89 single-owner functions go to their files; the 32 used only from the tables/install and the 29 used by one ubw file plus the tables go to `hooks_ubw.cpp` or their ubw file, whichever the table split makes the only user.
