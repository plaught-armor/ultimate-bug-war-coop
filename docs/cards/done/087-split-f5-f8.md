# Card 087: Split F5-F8: weapons, ammo, fire, chan handlers, NPC audit, enemies, remote body, vehicles, death, restart

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: sent 2026-11-05 16:58 (senior). Part of the 19:00 batch (see card 084): lighter proof, no baseline runs.

Read:

- the card 086 Review, and its Part 0, whose rules apply here unchanged;
- the card 085 Review;
- `docs/split-map.md` rows F5-F8.

Repo root: `/path/to/ultimate-bug-war-coop`.

## The moves

| File | Blocks |
|---|---|
| `ubw/weapons.cpp` | c021*, c050, c050.2, c050.4, c050.5, c057, c062 |
| `ubw/ammo.cpp` | c069, c071, c072 |
| `ubw/fire.cpp` | c024*, and the c026 ping/pong and edge-line code |
| `ubw/chan_ops.cpp` | the two chan tables and their per-op handlers and adapters from card 085, now that their c024/c026 dependencies have moved |
| `ubw/audit_npc.cpp` | c025, c027 (the walker itself is already in `ue/reflect`) |
| `ubw/enemies.cpp` | c023, c037-c041 |
| `ubw/remote_body.cpp` | c028, c028.2, c033, c034, c075 |
| `ubw/vehicles.cpp` | c029*, c032*, c075.2 |
| `ubw/death.cpp` | c030*, c043*, c044, c046, c048* |
| `ubw/restart.cpp` | c045 |

- **`ubw/chan_ops.cpp`:**
  - Its header, `ubw/chan_ops.h`, gets a `const ue::chan::ChanOps& chan_ops()` accessor, or exports the tables. `install()` keeps calling `ue::chan::set_ops` at the same point.
  - The receivers that a table points at stay with their own feature blocks. Only the table, the per-op handlers that were split out of `c26_*_receive`, and the adapters move here.
- **Update the map.** The split-map S7 row and the `ubw/chan_ops.cpp` row must say where the tables now live.

## Proof (lighter)

- **Build** (no warnings), **fnlen** (0 over 60 in every file), and the **function count check** from card 086 Part 0.6.
- **Routes:**
  - the base routes `host-morita` (`DEPLOY=1`), `client-zipline` and `c75-solo`;
  - `c47-gates` with `UBWCOOP_ENEMYAUDIT=1` on the host (covers F6);
  - `c72-mirror` (covers F5: ammo, fire relay and chan text);
  - `c75b-pods` (covers F7);
  - `c74-respawn` (covers F8).
- **Log check:** compare the sorted unique card-prefixed line texts, numbers replaced by `N`, against the newest earlier log of the same route:
  - `c72-mirror`: `card0(2[146]|50|57|69|7[12]):`;
  - `c74-respawn`: `card0(3[0]|4[3-8]):`.

  Explain any difference.
- `logsum` gives ERROR/FATAL 0, rc 0, and there is no new crash dump.

## Done when

- The ten files exist, and `c26_host_receive`/`c26_client_receive` are not brought back.
- Every route passes, and the function count check holds.
- The Report gives:
  - the `hooks.cpp` line count before and after;
  - the line count of each new file;
  - the shared functions that stayed in `hooks.cpp`;
  - any renames made to fix clashes.
- The Report ends with "Report complete."

## Report

### Method

- The same scripts as card 086 (`c87/items.py`, `c87/split.py` in the scratchpad), run on the card 086 input (`hooks.cpp` before 086, anonymous namespace removed) with the F1-F4 ranges and the F5-F8 ranges together. The script output for the six card 086 files is byte-identical to the files in the tree, so card 086 is unchanged and the header is regenerated in original order with the new entries.
- One input line differs from the 086 input: `install()` says `set_ops(chan_ops());` in place of `set_ops(kChanOps);`, so `kChanOps` and its tables do not cross into the header.
- One script fix: enumerators are tracked only for unscoped enums. `enum class` enumerators (`Wait`, `Done`, `None` ...) are scoped and were wrongly seen as clashes across files.
- Ranges (pre-086 line numbers, from the section headers):
  - `weapons`: Card 021 and its test loadout; Card 050 weapon sync and the client apply; Card 050 audit detours, Card 057, Card 062, the Card 050 test driver.
  - `ammo`: Card 069; Card 071 and Card 072.
  - `fire`: Card 024 and Card 026 up to `c26_pong`; `detour_logout` to `c24_host_guard`; `c24_step` to the end of Card 024 step 3.
  - `chan_ops`: from the host handlers (`c26_host_fire`) to `kChanOps`.
  - `audit_npc`: Card 025 and Card 027.
  - `enemies`: Card 023, 037, 040, 038, 039; Card 041.
  - `remote_body`: Card 028, 034, 033, 075 (remote mech body), the Card 028 test views.
  - `vehicles`: the Card 076 mech input contexts (see deviation 1); Card 029 (all parts); Card 032 (all parts); Card 075 co-op mechs to the end of the Card 076 fire check, before `kHooks`.
  - `death`: Card 030; Card 043, 044 and the Card 043 test driver; Card 046 and Card 048.
  - `restart`: Card 045.

### Part 0

1. **Linkage.** No `namespace {` in `hooks.cpp` or `ubw/`. No clash, so no renames.
2. **`ubw/hooks_internal.h`** (1148 lines before, 2885 after): 431 `extern` variables (203 `g_orig_*`), 542 prototypes (202 detours, 340 helpers), 554 constants, structs, enums, aliases and usings moved whole. Moved, not copied, as in 086.
3. **One definition.** Each name is defined in one file. kHooks/kCalls are unchanged in `hooks.cpp`.
4. **No behaviour change.** Bodies are byte copies. Two non-copy lines: `install()`'s `set_ops(chan_ops())`, and `c43_shot`'s default argument, now only in the header prototype (`uint8_t flags = 0`), like `caller_chain` in 086.
5. **Shared functions that stayed in `hooks.cpp`.** The card 086 rule, unchanged, plus one reading of this card: `ubw/chan_ops.cpp` is not a block for rule 5, because the card says the receivers a table points at stay with their own feature blocks. So a receiver or helper called only from a table or handler moved with its block. The 40 from card 086 still stay. 145 more stay:
`c21_adopt_set`, `c21_health`, `c21_log_tag_pressed`, `c21_mark_health_net`, `c21_note_activation`, `c21_row_map`, `c21_row_tag`, `c21_spec_for`, `c21_weapon_table`, `c23_arm_map`, `c23_collect`, `c23_own_pawn`, `c24_aim_begin`, `c24_aim_end`, `c24_log_local_activation`, `c24_log_rpc`, `c24_release_pawn`, `c24_reset_map`, `c24_trace`, `c24_view_point`, `c24_weapon_asc`, `c25_arm_map`, `c25_collect`, `c25_dead`, `c25_face`, `c25_health_attribute`, `c25_move`, `c26_firing`, `c26_own_fire`, `c26_send_fire`, `c27_arm_map`, `c27_bit`, `c27_find_any`, `c27_is_sprite_data`, `c27_map_ok`, `c27_open`, `c27_sparse`, `c27_text`, `c27_verify`, `c27_write`, `c28_arm_map`, `c29_arm_map`, `c29_bool`, `c29_load_set`, `c29_note_activation`, `c29_server_copy`, `c29_sets`, `c30_arm_map`, `c30_find`, `c30_nearest_enemy`, `c30_note_input`, `c32_arm_map`, `c32_note_pe`, `c37_arm_map`, `c37_note_npc`, `c38_arm_map`, `c38_note_npc`, `c41_arm_map`, `c43_all_down`, `c43_arm_map`, `c43_client_host_pawn`, `c43_down_state`, `c43_enter_down`, `c43_find`, `c43_find_hold`, `c43_find_pawn`, `c43_interact_edge`, `c43_is_down`, `c43_life`, `c43_life_text`, `c43_logout`, `c43_max_health`, `c43_next`, `c43_refuse_tag`, `c43_remove`, `c43_roster`, `c43_set_health`, `c43_shot`, `c43_shot_step`, `c43_slot`, `c43_sprite_key`, `c43_totals`, `c43_transition`, `c44_aim`, `c44_context`, `c44_kids`, `c44_other_pawn`, `c44_press`, `c45_add_option`, `c45_arm_map`, `c45_browse_enter`, `c45_browse_return`, `c45_client_screen`, `c45_has_option`, `c45_log_url`, `c45_respawn_check`, `c45_test_on_foot`, `c45_test_restart`, `c46_add_follow`, `c46_find_warp`, `c46_hurt`, `c46_kill_z`, `c46_mark_warp`, `c46_move`, `c48_client_step`, `c48_inventory_text`, `c48_respawn`, `c50_after_load_asc`, `c50_arm_map`, `c50_chapter_frame`, `c50_copy`, `c50_line`, `c50_list`, `c50_logout`, `c50_name`, `c50_placed`, `c50_resolve`, `c50_step`, `c50_text`, `c56_disconnect`, `c62_after_try`, `c62_before_try`, `c62_note_cancel`, `c69_install`, `c69_reset_map`, `c69_step`, `c72_client_step`, `c72_mirror_apply`, `c72_seed`, `c75_count_step`, `c76_client_imc`, `c76_damage`, `c76_fire`, `c76_fx`, `c76_spawn`, `c76_step`, `c76_tag`, `c76_try`, `c79_install`, `c79_mech_death`, `c79_step`, `card019_sync_aggregators`, `fs_len`, `fs_text`, `trace_profile`
   Most are called from blocks still in `hooks.cpp` (lobby, objectives, interact, testrun, grenades) or from another new file (for example `c43_logout` from `detour_logout` in `fire.cpp`). Card 089 places them with the other 40.
6. **Function count.** Before: 1450 (`hooks.cpp` 1179 + the seven `ubw/*.cpp` files 271). After:

   | File | Lines | Functions | Over 60 |
   |---|---|---|---|
   | `hooks.cpp` | 10332 | 575 | 0 |
   | `ubw/flags.cpp` | 546 | 41 | 0 |
   | `ubw/guards.cpp` | 419 | 25 | 0 |
   | `ubw/input_diag.cpp` | 1393 | 86 | 0 |
   | `ubw/pawn_collision.cpp` | 219 | 12 | 0 |
   | `ubw/dropship.cpp` | 316 | 17 | 0 |
   | `ubw/sprint.cpp` | 918 | 44 | 0 |
   | `ubw/attributes.cpp` | 1001 | 46 | 0 |
   | `ubw/weapons.cpp` | 1373 | 83 | 0 |
   | `ubw/ammo.cpp` | 403 | 24 | 0 |
   | `ubw/fire.cpp` | 363 | 24 | 0 |
   | `ubw/chan_ops.cpp` | 141 | 12 | 0 |
   | `ubw/audit_npc.cpp` | 845 | 48 | 0 |
   | `ubw/enemies.cpp` | 3000 | 173 | 0 |
   | `ubw/remote_body.cpp` | 884 | 40 | 0 |
   | `ubw/vehicles.cpp` | 1903 | 101 | 0 |
   | `ubw/death.cpp` | 1763 | 91 | 0 |
   | `ubw/restart.cpp` | 205 | 9 | 0 |
   | `ubw/hooks_internal.h` | 2885 | 0 | 0 |

   The total is 1451: the 1450 from before plus the new `chan_ops()` accessor.
7. **CMake:** the ten files follow `src/ubw/attributes.cpp` in the order weapons, ammo, fire, chan_ops, audit_npc, enemies, remote_body, vehicles, death, restart.
8. **New files:** each starts with a comment naming its cards and what they do.

### `ubw/chan_ops.cpp`

- It holds `c26_host_fire`, `c26_host_use`, `c26_host_ping`, `c26_host_pong`, `c26_client_fire`, `c26_client_ping`, `c26_client_pong`, the four adapters, `ChanTable`, both `consteval` builders, `kChanHost`, `kChanClient`, `c26_is_remote_pc`, `c26_send_skipped` and `kChanOps`. These are byte copies.
- `ubw/chan_ops.h` declares `const ue::chan::ChanOps& chan_ops();` in `ubw::chan`. `chan_ops.cpp` defines it after the `ubw::hooks` block as `return ubw::hooks::kChanOps;`. `install()` calls `set_ops(chan_ops());` at the same point as before.
- `c26_pong`, the ping step and the edge lines are in `fire.cpp`. `c26_host_receive`/`c26_client_receive` are not back (`grep` over `mod/src` is empty).

### Deviations

1. **The card 076 mech input contexts** (`C76Imc`, `c76_imc_add`, `c76_imc_remove`, `c76_client_imc`) sit inside the map's c026 line range, between `c24_host_guard` and `c24_step`. They are neither chan tables nor fire code, so they went to `ubw/vehicles.cpp` with the other card 076 code (the mech fire check in the c075.2 range). `c76_client_imc` is called from `c24_step` (`fire.cpp`), so it stayed in `hooks.cpp` under rule 5. Only `C76Imc`, `g_c76_imc`, `c76_imc_add` and `c76_imc_remove` moved.
2. **The `chan_ops()` accessor** is one new function, so the count is 1451.

### Line counts

| File | Lines |
|---|---|
| `hooks.cpp` before (start of 087) | 22456 |
| `hooks.cpp` after | 10332 |
| `ubw/hooks_internal.h` before / after | 1148 / 2885 |
| `ubw/weapons.cpp` | 1373 |
| `ubw/ammo.cpp` | 403 |
| `ubw/fire.cpp` | 363 |
| `ubw/chan_ops.cpp` | 141 |
| `ubw/audit_npc.cpp` | 845 |
| `ubw/enemies.cpp` | 3000 |
| `ubw/remote_body.cpp` | 884 |
| `ubw/vehicles.cpp` | 1903 |
| `ubw/death.cpp` | 1763 |
| `ubw/restart.cpp` | 205 |

### Proof

- **Build:** `systemd-run --user --scope -q -p MemoryMax=12G ninja -C build -j4` gives rc 0 with no warnings (/W4 /WX).
- **fnlen and the count:** see Part 0 item 6.
- **`docs/split-map.md`:** rows F5-F8 say "Done in card 087". The S7 row and the `ubw/chan_ops.cpp`, `ubw/fire.cpp` and `ubw/vehicles.cpp` rows of the file table say where the tables, the c026 remainder and the card 076 contexts now live.
- **Routes:** DLL `bcfc3f2aade607c2` on both lanes. Each route gives rc 0 on host and client (c75-solo has no client), ERROR/FATAL 0, and no new crash dump.

  | Run | Route | Lane | Time | WARN |
  |---|---|---|---|---|
  | c87a | `host-morita` (`DEPLOY=1`) | 1 | 109 s | 0 |
  | c87b | `client-zipline` | 1 | 146 s | 0 |
  | c87c | `c47-gates` (`UBWCOOP_ENEMYAUDIT=1` on the host) | 1 | 215 s | 1: card044 "client no CompleteDeath 8008 ms after StartDeath", also in c86c on the same route |
  | c87d | `c75-solo` (`SOLO=1 DEPLOY=1`) | 2 | 90 s | 0 |
  | c87e | `c72-mirror` | 2 | 140 s | 0 |
  | c87f | `c75b-pods` | 2 | 105 s | 1: card034 "client refit of BP_PlayerCharacterMI_C skipped ...", the known one from card 075, also in c85h |
  | c87g | `c74-respawn` | 2 | 214 s | 0 |

  c87c's host log has 1377 card023 audit lines, so the F6 audit ran.
- **Log check** (sorted unique card-prefixed texts, numbers as `N`):
  - `c72-mirror`, `card0(2[146]|50|57|69|7[12]):`, c85f against c87e: identical on host and client. The raw line counts are host 248/249 and client 355/354. The unique texts are the same; only the repeat counts differ.
  - `c74-respawn`, `card0(3[0]|4[3-8]):`, c81e against c87g: identical on host and client (95/97 and 50/50 lines).

  So there is no difference to explain.

### Review points

1. The card 076 mech input contexts went to `ubw/vehicles.cpp`, and `c76_client_imc` stayed in `hooks.cpp` (deviation 1).
2. `ubw/chan_ops.cpp` is not a block for rule 5, so receivers called only from the tables moved with their own blocks (Part 0 item 5).
3. The new accessor makes the total 1451, and `c43_shot` keeps its default argument only in the header.

Report complete.

## Senior review

Senior, 2026-11-05 17:14. **Accepted.**

- Checked: the seven drive logs end with `scenario passed`, and logsum gives ERROR/FATAL 0 for each. The newest crash dump in both prefixes is still c79a's. Both lanes and `build/` hold DLL `bcfc3f2aade607c2`. `c26_host_receive`/`c26_client_receive` are gone from `mod/src`.
- Regenerating the 086 files byte-identically from the same scripts is a good check.
- Point 1 (the card 076 contexts to `vehicles.cpp`, `c76_client_imc` stays) is accepted.
- Point 2 (`chan_ops.cpp` is not a block for rule 5) is accepted.
- Point 3 (the `c43_shot` default only in the header, total 1451) is accepted.
- Card 089 must place the 185 shared functions, and `hooks_internal.h` (2885 lines) should shrink as they find owners.
