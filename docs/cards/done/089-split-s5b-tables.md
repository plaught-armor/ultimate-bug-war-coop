# Card 089: Split S5b: split the hook tables, `InstallSet`, `ubw::init`, retire `hooks.cpp`

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: sent 2026-11-05 17:34 (senior). This is the last card of the 19:00 batch (see card 084): lighter proof, no baseline runs.

Read:

- the card 084 and 088 Reviews;
- the S5 row and the `install` / `g_gate_diag` rows of `docs/split-map.md`;
- the card 078 "Map changes";
- the list of what is left in `hooks.cpp` from the card 088 Report.

Repo root: `/path/to/ultimate-bug-war-coop`.

## Do

1. **Split the tables.**
   - Rows whose detour or call target is defined in `ue/` go to `kHooksUe`/`kCallsUe`, in `ue/hooktable.cpp` or the `ue/` file that owns the detour. All other rows go to `kHooksUbw`/`kCallsUbw`.
   - Keep each row unchanged: the same symbol, the same orig pointer and the same gate. Keep the relative order within each table.
2. **`InstallSet`** in `ue/hooktable.h`: `{ std::span<const HookSpec> hooks; std::span<const CallSpec> calls; }`.
   - `ue::install(...)` resolves and installs the `ue` set first, then the `ubw` set, using card 084's span functions.
   - The PDB identity and the diag value are passed in as today.
   - `g_gate_diag` moves to `ue/hooktable.cpp`.
3. **`ubw::init`** in `ubw/flags.cpp` (or `ubw/hooks_ubw.cpp`, see step 4) runs, in today's order:
   - `read_card_flags` and `set_hook_gates`, both moved to `ubw/flags.cpp`;
   - `ue::net::set_engine_steps`;
   - `ue::chan::set_ops`;
   - then `ue::install` with both sets.

   `dllmain.cpp` calls `ubw::init` where it called `install()` today.
4. **Retire `hooks.cpp`.**
   - Rename what remains to `ubw/hooks_ubw.cpp`: `kHooksUbw`, `kCallsUbw`, `install_card_logs`, the engine step functions (`ubw_*`) and the shared functions that cards 086-088 left behind.
   - If a leftover shared function has an obvious owner now that every block has moved, move it there. Otherwise it stays in `hooks_ubw.cpp`.
   - Delete `mod/src/hooks.cpp` and `hooks.h`, or reduce `hooks.h` to the `ubw::init` declaration and rename it to `ubw/init.h`.
   - Update CMake.
5. **Update the map.** Mark S5 done in `docs/split-map.md`, and add a short "Final layout" section that lists each file and its line count.

## No behaviour change

- The hook count, the call count and every gate are unchanged.
- The `hooks:` summary line counts are the same as in the card 088 runs, for both the default flags and `UBWCOOP_DIAG=1`.
- The skip lines are the same set. Their order may follow the new table order; if it does, say so.

## Proof (lighter)

- **Build** (no warnings), **fnlen** (0 over 60 in every file), and the **function count check** from card 086 Part 0.6.
- **Routes:**
  - the base routes `host-morita` (`DEPLOY=1`), `client-zipline`, `c47-gates` and `c75-solo`;
  - `c75-solo` with `UBWCOOP_DIAG=1`;
  - `c47-bug`;
  - `c75b-pods`.
- **Log check:** diff the `hooks:` lines (the summary line plus the sorted skip lines) of `c47-gates` and of the DIAG `c75-solo` against card 088's runs. Also diff the `config:` lines in order, as card 078 did.
- `logsum` gives ERROR/FATAL 0, rc 0, and there is no new crash dump.

## Done when

- `mod/src/hooks.cpp` is gone, and the tables are split.
- `grep -rn 'c[0-9][0-9]\|card' mod/src/ue/` finds nothing, apart from hits you list and explain.
- Every route passes, and the hook-line diff is empty.
- The Report gives the final file list with line counts, and ends with "Report complete."

## Report

Done 2026-11-05 17:57 (junior). DLL `5061be7ea09d50b8`. The pre-card tree is backed up in the scratchpad (`c89/bak/`).

### What changed

1. **Table split.** A row goes to the ue set when its detour (hooks) or its slot (calls) is defined in `ue/`. Rows are byte copies in their old relative order, with the one exception in deviation 1.
   - `kHooksUe` (7 rows): `UGameEngine_Init`, `UEngine_Browse`, `UEngine_LoadMap`, `UWorld_Listen` (gate `&g_gate_diag`), `UGameEngine_Tick`, `APlayerController_ServerChangeName_Implementation`, `APlayerController_ClientMessage_Implementation`.
   - `kCallsUe`: 57 rows.
   - `kHooksUbw`/`kCallsUbw`: 219 and 154 rows, with every section comment. Total: 226 hooks and 211 calls, as before.
   - The map's Sizes section predicted 5/59. The two chan detours count as ue because card 084 moved them to `ue/chan.cpp`. Two call slots count as ubw because their globals are defined in `ubw/`.
2. **`ue/hooktable.h`:** adds `InstallSet`, `InstallSteps` (`after_resolve`, `before_hooks`), `extern bool g_gate_diag`, and `ue::install(game_set, guid, age, steps)`.
   - `resolve_all`, `install_hooks` and `log_skipped_hooks` now take `std::span<const InstallSet>` and loop over the ue set and then the game set. There is still one summary line over all 226 hooks, and the skip text runs on across the two sets.
   - `ue::install` follows the old `install()` order exactly:
     1. the exe base / PE checks and their two log lines;
     2. the identity check;
     3. resolve every row;
     4. `g_exe_base`;
     5. `after_resolve`;
     6. `MH_Initialize`;
     7. `before_hooks`;
     8. hook the rows.

     The targets array is static (320 rows max, FATAL past that).
   - New functions in `ue/hooktable.cpp`: `hook_count`, `exe_image` (the two FATAL checks moved out of `install` to keep it under 60 lines) and `ue::install`. The file goes from 9 to 12 functions.
3. **`ubw::init`** (in `ubw/hooks_ubw.cpp`, declared in `ubw/init.h`, which replaces `hooks.h` and keeps its doc comment):
   - stores the eight dllmain flags plus `g_init_autoinput`;
   - calls `set_engine_steps(kEngineSteps)` and `set_ops(chan_ops())`;
   - calls `ue::install({kHooksUbw, kCallsUbw}, kPdbGuid, kPdbAge, {init_after_resolve, install_card_logs})`.

   `init_after_resolve` holds what the old `install()` did between resolving and `MH_Initialize`, copied verbatim: `read_card_flags()`, `c52_init`, `autoinput::init` and the four site resolvers. `dllmain` calls `ubw::init`.
4. **`read_card_flags` and `set_hook_gates`** moved to `ubw/flags.cpp`, in a `namespace ubw::hooks` block at the end. `set_hook_gates` still writes `g_gate_diag = config::read_diag()`; that variable is now `ue::hooktable::g_gate_diag`, defined in `ue/hooktable.cpp`. The 10 ubw `g_gate_*` stay in `hooks_ubw.cpp` and are extern in `hooks_internal.h`.
5. **`hooks.cpp` retired.** It was renamed to `ubw/hooks_ubw.cpp`, and `hooks.cpp` and `hooks.h` are deleted. CMake: `src/hooks.cpp` became `src/ubw/hooks_ubw.cpp`. 139 functions moved out:
   - **114 single-owner functions**, each to the only file that uses it besides the tables or the install path. This includes the 89 from the owners list. Of the 32 table/install-only and the 29 one-file-plus-tables functions, those whose only user turned out to be one file went to that file.
   - **23 by unique card owner:** functions whose card prefix is defined in exactly one ubw file and whose only other users are the tables, `flags.cpp` or that file:
     - `weapons`: `c21_mark_health_net`
     - `fire`: `c24_release_pawn`
     - `audit_npc`: `c25_health_attribute`, `c27_is_sprite_data`, `c27_find_any`
     - `restart`: `c45_has_option`, `c45_log_url`, `c45_add_option`, `c45_browse_enter`, `c45_browse_return`
     - `lobby_ui`: `c52_host_browse`, `c52_listen`
     - `testrun`: `c64_allow_client_nav`, `c63_step`, `c63_init`
     - `objectives`: `c68_step`, `c68_install`
     - `ammo`: `c69_step`, `c69_install`
     - `grenades`: `c73_step`, `c73_install`, `c74_install`
     - `death`: `c79_install`
   - **Totals by destination file:** attributes 11, audit_npc 7, death 29, dropship 2, fire 5, grenades 7, input_diag 10, objectives 8, pawn_collision 7, remote_body 2, sprint 28, testrun 5, vehicles 2, weapons 5, restart 5, lobby_ui 2, ammo 2.
   - Each block was appended with its lead comment, in source order, before its file's closing `} // namespace ubw::hooks`.
   - The compiler then named 49 declarations missing across files: 29 functions, 10 gates and 10 globals. Prototypes or externs for them were added to `hooks_internal.h`; nothing else was added.
6. **What stays in `hooks_ubw.cpp`** (103 old functions plus `ubw::init` and `init_after_resolve`): the engine steps (`ubw_*`, `card_log_steps`, `travel_step`), `install_card_logs`, `c65_install` (two ubw files carry the c65 prefix), and the helpers that two or more ubw files share:
   - general: `side`, `sprint_side`, `session_side`, `is_net_client`, `is_local_pc`, `iface_object`, `nearest_of_class`, `fs_len`/`fs_text`;
   - families: the `c21_*`, `c24_*`, `c25_*`, `c27_*`, `c43_*`, `c50_*` and `c54_*` families;
   - others: `c64_find_actor`, `c72_mirror_apply`, `c73_drop_stale`, `c75_leave_soldier`, `c76_tag`.
7. **Stale comments updated.**
   - "hooks.cpp's kCalls table" became "the kCallsUe table in ue/hooktable.cpp" in `ue/actor.h`, `gas.h`, `input.h`, `reflect.h`, `widget.h` and `net.h`.
   - `kHooks`/`kCalls` became `kHooksUe`/`kCallsUe` in ue headers and `kHooksUbw` in ubw files.
   - `install()` / `hooks::install` became `ubw::init` in `autoinput.h`, `movieskip.h`, `ubw/chan_ops.h` and the "written in install()" comments of the ubw globals. `ue/net.h` and `ue/chan.h` now say "once, by the caller before ue::install".
   - `cmdfile.h`'s two `hooks.cpp` mentions and `autoinput.cpp`'s pointer to `hooks.cpp` now name the owning file.
   - `ubw/chan_ops.h` no longer says the tables wait in `hooks.cpp`. The `hooks_internal.h` header comment names `hooks_ubw.cpp`.

### Deviations

1. **Trailing card comments on 5 ue call rows were dropped** so that `ue/` greps clean:
   - `g_request_screenshot` `// card 063`
   - `g_add_movement_input` `// card 019`
   - `g_spawn_params_ctor` `// card 032`
   - `g_set_hidden_in_game` `// card 033`
   - `g_memory_malloc` `// card 045`

   The code of every row is unchanged. The table section comments (`// Card 039: ...` and others) all stay with the ubw tables.
2. **Order inside `ubw::init`.** The card asks for `read_card_flags`/`set_hook_gates` first, then the two `set_*` calls, then `ue::install`. I kept the old `install()` order instead:
   - The two `set_*` stores come first. They are plain pointer stores with no log.
   - `read_card_flags` (which calls `set_hook_gates`) runs inside `ue::install`, in its `after_resolve` step, after the identity check and the row resolution.

   That keeps the identity lines before the card-flag `config:` lines (host log lines 15-16 before 17-45), so the in-order `config:` diff is empty and a wrong exe still stops before any flag is read. The diag value reaches `ue/` as before, through `g_gate_diag`, which `set_hook_gates` writes before the first row is hooked.
3. **Tick is now the 5th hook installed** (it was row 78 of 226). The ChangeName/ClientMessage detours are now 6th and 7th (they were rows 108 and 109). So the "hooked at" lines are in a new order. The map already accepted this: install ends about 4 s after the identity line (c89c: 71995642 to 71999942 ms) and `UGameEngine::Init fired` comes 37 s later (72037267 ms). The old kHooks lead comment "Tick goes last" is replaced by a note on `kHooksUe`.
4. **DIAG baseline.** Card 088 had no `UBWCOOP_DIAG=1` run, so the DIAG diff is against `c86d` (the same c75-solo DIAG route on the 086 DLL). Cards 087 and 088 left the tables unchanged, so its `hooks:` lines are a valid baseline. The default-flag c75-solo was also diffed against `c88d`.

### Proof

- **Build:** a clean rebuild (`ninja -t clean`, then the usual scope build) has 0 warnings and 0 errors.
- **fnlen:** 0 over 60 in every `.cpp` (61 files in `mod/src`).
- **Function count:** `hooks.cpp` + `ubw/*.cpp` counted 1451. `ubw/*.cpp` now counts 1452: the old `install` is gone (−1), and `ubw::init` and `init_after_resolve` are new (+2). `hooks_ubw.cpp` holds 105: 243 − 139 moved − `install` + the 2 new. `ue/hooktable.cpp` went from 9 to 12, as in What changed item 2. All of `mod/src` counts 1602.
- **`grep -rn 'c[0-9][0-9]\|card' mod/src/ue/`:** no hits.
  - `ue/core.h` said "(card 061 split) ... Later move cards"; it now says "Later moves".
  - `ue/reflect.h` said "(card 080 split, S3)"; it now says "(split S3)".
  - `ubw::offsets::Symbol` in `hooktable.h` does not match the pattern.
- **Routes:** every run has rc 0, FATAL/ERROR 0 and no new crash dump.

  | Run | Route | Time | Host | Client | WARN |
  |---|---|---|---|---|---|
  | c89a | host-morita, DEPLOY=1 | 102 s | 1294 lines | 1164 lines | none |
  | c89b | client-zipline | 125 s | 1477 lines | 1737 lines | none |
  | c89c | c47-gates (C43_TEST, ENEMYAUDIT) | 212 s | 3801 lines | 2369 lines | client: card044 "no CompleteDeath", also in c88c |
  | c89d | c75-solo, DEPLOY=1 | 85 s | 1132 lines | solo | none |
  | c89e | c75-solo, `UBWCOOP_DIAG=1` | 82 s | 1455 lines | solo | none |
  | c89f | c47-bug | 288 s | 3758 lines | 1730 lines | host: card039 "probe table full", also in c84g/c85e/c86e |
  | c89g | c75b-pods | 89 s | 1736 lines | 1980 lines | client: card034 "refit skipped", also in c85h/c87f |
- **Log check** (`c89/hdiff.sh`: the summary line, the sorted skip names, and the `config:` lines in order):

  | Diff | Summary | Skip names | `config:` lines (in order) |
  |---|---|---|---|
  | c88c vs c89c | identical: 109 of 226 not installed, 117 installed | identical (109) | identical (37) |
  | c86d vs c89e (DIAG) | identical: 28 of 226 not installed, 198 installed | identical (28) | identical (37) |
  | c88d vs c89d | identical | identical (115) | identical (37) |

  The unsorted skip lines are also in the same order in both pairs: the only gated ue row, Listen, was already among the first rows. For c88c vs c89c, every other `hooks:` line apart from "hooked at" lines and addresses is identical; only the two site-address lines differ, by ASLR.

### Final file list (`mod/src`, after card 089)

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

Total: 61 files, 34336 lines, 1602 functions. The same table is in the new "Final layout" section of `docs/split-map.md`, and the S5 row there is marked done (cards 088 and 089).

Report complete.

## Senior review

Senior, 2026-11-05 18:00. **Accepted.** This ends the 19:00 split batch (cards 084-089).

- Checked: `mod/src/hooks.cpp` and `hooks.h` are gone, and CMake builds `src/ubw/hooks_ubw.cpp`. `grep -rn 'c[0-9][0-9]\|card' mod/src/ue/` gives 0 hits. The seven drive logs (c89a-g) end with `scenario passed`, and logsum gives ERROR/FATAL 0 for each. The newest crash dump in both prefixes is still c79a's. The DLL in `build/`, `game/` and `game2/` is `5061be7ea09d50b8`.
- Deviation 1 (the five trailing card comments dropped from ue call rows) is accepted. The row code is unchanged.
- Deviation 2 (the old `install()` order kept, `read_card_flags` in the `after_resolve` step) is accepted and is better than the card's order: the identity check still comes before any flag is read, and the in-order `config:` diff is empty.
- Deviation 3 (Tick installed 5th) is the order change the map predicted.
- Deviation 4 (the DIAG baseline is c86d) is accepted, because cards 087 and 088 left the tables unchanged.
- Senior fix: the S5 row of `docs/split-map.md` said "cards 088 (S5a)". S5a was card 084, and the row now says so.
- Left for later, no card yet: `ubw/hooks_internal.h` is 3546 lines and `hooks_ubw.cpp` holds 103 helpers shared by two or more files. A follow-up card can give the `c21_*`/`c24_*`/`c43_*`/`c50_*`/`c54_*` families their own files, and the header can shrink to per-file headers.
