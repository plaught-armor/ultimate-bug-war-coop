# Card 086: Split F1-F4: open the anonymous namespace, add `ubw/hooks_internal.h`, move guards, input diag, pawn collision, dropship, sprint, attributes

Status: sent 2026-11-05 16:42 (senior). Part of the 19:00 batch (see card 084): lighter proof, no baseline runs.

Read:

- the card 085 Review;
- `docs/split-map.md`, the `ubw/` file table and rows F1-F4;
- the `hooks.cpp` prologue (lines 1-40) and epilogue (`} // namespace`, `install()`).

Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

Every F card moves card blocks from `hooks.cpp` into `ubw/` files. Today all of `hooks.cpp` (about lines 38-27817) is one anonymous namespace, so nothing in it can be seen from another file. This card opens that namespace once and sets the rules that cards 087 and 088 follow. It then does the first four groups.

## Part 0: the shared rules for every F card (086-088)

1. **Linkage.**
   - Delete the `namespace {` line and its `} // namespace` line in `hooks.cpp`. Everything stays in `namespace ubw::hooks`, now with external linkage.
   - If this causes a name clash or a new warning, fix it by renaming the clashing internal symbol. Say which ones in the Report.
   - Only the moved files may share these names. Nothing outside `ubw::hooks` uses them.
2. **`ubw/hooks_internal.h`** (namespace `ubw::hooks`). This is the one internal header. It holds only what crosses a file boundary:
   - `extern` declarations for globals that are defined in one `.cpp` and used in another;
   - declarations for functions that are called across files;
   - every `constexpr`, struct or typedef used by more than one file. Move each one there, and do not copy it.

   Each moved file includes it. `hooks.cpp` includes it.
3. **One definition.** Each global, function and constant is defined in exactly one file. A block moves with:
   - its own state, constants and log-cap counters;
   - its detours and its `g_orig_*` pointers. Those stay `extern`, resolved by the unchanged `kHooks`/`kCalls` rows, which stay in `hooks.cpp` until card 089.
4. **No behaviour change.**
   - Bodies are copied unchanged, and no function is renamed except to fix a clash.
   - No log text changes, and no hook row is reordered.
5. **Ownership.**
   - A function that is shared by blocks in different target files stays in `hooks.cpp`. Declare it in the header; do not guess an owner.
   - List the functions that stayed this way in the Report. Card 089 decides where they go.
6. **The function count check.** Before any move, record the fnlen function count of `hooks.cpp` plus every `ubw/*.cpp`. After the card, the same total must hold, give or take the adapters that card 085 added (state the number). This shows nothing was dropped or duplicated.
7. **CMake.** Add each new `src/ubw/*.cpp` to the explicit list, after `src/ubw/flags.cpp`.
8. **Per file.** Each new file starts with a short comment that names its cards and what they do. fnlen must show 0 functions over 60 lines in every file.

## Part 1: the moves (rows F1-F4 of the map)

| File | Blocks |
|---|---|
| `ubw/guards.cpp` | c003-c007, c052.4 |
| `ubw/input_diag.cpp` | c008, c010-c013, the collision writers, IA look/move eval, c016*, c011 |
| `ubw/pawn_collision.cpp` | c014 |
| `ubw/dropship.cpp` | c015* |
| `ubw/sprint.cpp` | c019*, c022 |
| `ubw/attributes.cpp` | c017, c018, c020* |

- **Finding the blocks.** Each block is found by its card comments and its log prefixes (`card0NN:`). Where a block's edges are unclear, the log prefix decides: the code that writes `cardNNN:` lines belongs to card NNN's block.
- **The three origs.** Do F2 first. `input_diag` owns the three origs from the card 047 table that F4 and F5 call, so they go in the header.
- **Auto-travel state** in the `ubw/flags.cpp` row stays where it is (card 089).

## Proof (lighter)

- **Build:** `systemd-run --user --scope -q -p MemoryMax=12G ninja -C build -j4`, with no warnings.
- **fnlen and the function count check** from Part 0.6.
- **Routes:**
  - the base routes `host-morita` (`DEPLOY=1`; it also covers F3, the dropship), `client-zipline` (covers F1) and `c47-gates`;
  - `c75-solo` with `UBWCOOP_DIAG=1` (covers F2; use the same method as card 084);
  - `c57` (covers F4, sprint and attributes).
- **Log check:** compare the sorted unique `card0(0[3-9]|1[0-9]|2[02]):` line texts of `c75-solo` with `UBWCOOP_DIAG=1`, numbers replaced by `N`, against card 084's DIAG run. Explain any difference.
- `logsum` gives ERROR/FATAL 0, rc 0, and there is no new crash dump.

## Done when

- `hooks.cpp` has no anonymous namespace, and `ubw/hooks_internal.h` plus the six files exist.
- Every route passes, and the function count check holds.
- The Report gives:
  - the `hooks.cpp` line count before and after;
  - the line count of each new file;
  - the list of shared functions that stayed;
  - any renames made to fix clashes.
- The Report ends with "Report complete."

## Report

### Method

The split was made by a script, not by hand, so every body is a byte copy.
- `items.py` cuts the `ubw::hooks` body of `hooks.cpp` into its 3855 top-level items. Each item carries its lead comment, its kind (function, variable, constant, type, forward declaration) and the identifiers it uses. Member names after `.`, `->` and `::` are not counted as uses.
- `split.py` gives each item a file by the block ranges of the card. Ranges were matched to the split-map's old line numbers by their section headers, with the log prefix deciding unclear edges.
  - A name is crossing when a file other than its own uses it.
  - Each crossing variable gets an `extern` line in the header, and each crossing function its prototype.
  - Each crossing constant, struct, enum or alias moves whole into the header, along with what its text needs.
  - Header entries keep their original order, and every other item keeps its original order in its file.
- Forward declarations of a header-declared name were removed: the header carries them. A forward declaration of a function that is not crossing went to the file that defines it.
- Both scripts are in the scratchpad, `c86/items.py` and `c86/split.py`.

### Part 0

1. **Linkage.**
   - `namespace {` (old line 49) and its `} // namespace` (old 27410) are deleted. No `namespace {` is left in `hooks.cpp` or `ubw/*.cpp`.
   - Removing it gave no clash and no new warning, so nothing was renamed.
   - One signature change: `caller_chain` keeps its default argument only in the header prototype, `void caller_chain(char* out, size_t cap, uint32_t depth = kStackFrames);`. The definition now reads `uint32_t depth`, because C++ does not allow the default twice. The body is unchanged.
2. **`ubw/hooks_internal.h` (1148 lines).**
   - It holds the shared includes and the prologue's `namespace log = ue::log;` plus the twelve using-declarations and using-directives.
   - 186 `extern` variables: 93 `g_orig_*` and 93 states, flags and caps.
   - 208 function prototypes: 93 detours (the kHooks/kCalls rows) and 115 helpers called across files.
   - 198 constants, structs, enums and aliases, moved, not copied. Six came from the code that stays in `hooks.cpp` and are used by a moved file: `kC65ShuntLogs`, `kMapPropKey`, `kObjectPropClass`, `kC27MaxElements`, `kC27TextChars` and `C27Sparse`. The rest came from the moved blocks.
   - `hooks.cpp` and each new file include it.
3. **One definition.**
   - Each global, function and constant is defined in one file.
   - The kHooks/kCalls rows are unchanged and stay in `hooks.cpp`. They resolve the moved detours and origs through the header.
   - **Card 047 origs** (`g_orig_perform_movement`, `g_orig_handle_input_move`, `g_orig_server_move_perform`, `g_orig_sprint_change`): defined in `ubw/input_diag.cpp` and extern in the header, as the card says.
     - Their detours are in `ubw/pawn_collision.cpp` (the first two) and `ubw/sprint.cpp` (the other two).
     - The split-map section text says each orig moves to its detour's file. I followed the card, and the F2 row now says so.
4. **No behaviour change.**
   - Bodies are byte copies.
   - No log text changed and no hook row moved.
   - The only renamed or rewritten line is the `caller_chain` default above.
5. **Shared functions that stayed in `hooks.cpp` (40).**
   - These are non-detour functions of a moved block that a block of a different file calls, either another new file or a block still in `hooks.cpp`.
   - The hook/call tables, `install`, `ubw_post_tick` and `ubw_load_map_enter`/`_return` are not blocks. So a step function called only from the post-tick (for example `card017_step` or `dropship_watch_step`) moved with its block.
   - Detours always move with their block, even where other code calls them (`detour_asc_tag_pressed`, `detour_bind_input_tag`, `detour_interact`, `detour_release_interaction`).
   - All 40 are declared in the header:
     `attribute_name`, `c21_is_axis_tag`, `c22_comp_text`, `c22_own_props`, `caller_chain`, `camera_init_data`, `camera_location`, `card019_movement_fix`, `client_force_game_only`, `component_yaw`, `config_context`, `holding_dropship`, `host_row_step`, `interact_target`, `is_local_pc`, `is_net_client`, `load_config_contexts`, `log_card013_row`, `log_cmc`, `log_key_counts`, `log_move_diag`, `log_sprint_row`, `note_input_window_key`, `note_local_move`, `note_move_entry`, `note_move_exit`, `object_name_into`, `reset_actions`, `reset_card016_map`, `reset_card017_map`, `reset_card020_map`, `reset_move_diag_map`, `reset_move_diag_row`, `session_side`, `side`, `side_text`, `sprint_side`, `start_card020_rows`, `start_own_rows`, `watched_pawn`.
   - They stay at their old place in `hooks.cpp`, with their lead comments.
6. **Function count.** Before: `hooks.cpp` 1409 + `ubw/flags.cpp` 41 = 1450 (the four 085 adapters are already in the 1409). After:

   | File | Functions | Over 60 |
   |---|---|---|
   | `hooks.cpp` | 1179 | 0 |
   | `ubw/flags.cpp` | 41 | 0 |
   | `ubw/guards.cpp` | 25 | 0 |
   | `ubw/input_diag.cpp` | 86 | 0 |
   | `ubw/pawn_collision.cpp` | 12 | 0 |
   | `ubw/dropship.cpp` | 17 | 0 |
   | `ubw/sprint.cpp` | 44 | 0 |
   | `ubw/attributes.cpp` | 46 | 0 |
   | `ubw/hooks_internal.h` | 0 | 0 |

   The total is 1450, the same as before.
7. **CMake:** the six files follow `src/ubw/flags.cpp` in the order guards, input_diag, pawn_collision, dropship, sprint, attributes.
8. **New files:** each starts with a comment naming its cards and what they do.

### Line counts

| File | Lines |
|---|---|
| `hooks.cpp` before (start of 086) | 27490 |
| `hooks.cpp` after | 22456 |
| `ubw/hooks_internal.h` | 1148 |
| `ubw/guards.cpp` | 419 |
| `ubw/input_diag.cpp` | 1393 |
| `ubw/pawn_collision.cpp` | 219 |
| `ubw/dropship.cpp` | 316 |
| `ubw/sprint.cpp` | 918 |
| `ubw/attributes.cpp` | 1001 |

### Proof

- **Build:** `systemd-run --user --scope -q -p MemoryMax=12G ninja -C build -j4` gives rc 0 with no warnings (/W4 /WX).
- **fnlen and the count:** see Part 0 item 6.
- **`docs/split-map.md`:** rows F1-F4 say "Done in card 086".
- **Routes:** DLL b6c6bb4cb730fc1a, with lane 1 and lane 2 running at the same time and the default delays (1000 / 8000). Every run has drive rc 0, host and client rc 0, logsum ERROR/FATAL 0 and no new crash dump.

  | Tag | Lane | Route | Result |
  |---|---|---|---|
  | c86a | 1 | host-morita, `DEPLOY=1` | 104 s, WARN 0 / 0 |
  | c86b | 1 | client-zipline | 134 s, WARN 0 / 0 |
  | c86c | 1 | c47-gates (`UBWCOOP_C43_TEST=1`) | 181 s; client WARN 1, `card044: client no CompleteDeath 8002 ms after StartDeath`, the same line as c85c (8009 ms) |
  | c86d | 2 | c75-solo, `UBWCOOP_DIAG=1`, `DEPLOY=1` | 86 s; `hooks: 28 of 226 hooks not installed (gate off), 198 installed`, the same as c84h |
  | c86e | 2 | c57 | 109 s; host WARN 1, `card039: host probe table full (32)`, the same line as c85e |

- **Log check, c84h against c86d:**
  - The method is the same as card 084's: sorted unique `card0(0[3-9]|1[0-9]|2[02]):` texts on the host, with the text after the card tag normalised (0x addresses, 8+ hex runs and numbers become N). The script is `c86diff.sh`.
  - The result is identical: there is no difference to explain. Both runs have 331 such lines, which normalise to 170 unique texts: card017 93, card018 8, card019 50, card020 1, card022 179.
  - Solo, so there is no client log.

Report complete.

## Senior review

Senior, 2026-11-05 16:58. **Accepted.**

- Checked: the five drive logs end with `scenario passed`, and logsum gives ERROR/FATAL 0 for each. The newest crash dump in both prefixes is still c79a's. No `namespace {` is left in `hooks.cpp` or `ubw/`. Both lanes and `build/` hold DLL `b6c6bb4cb730fc1a`. The function count is 1450 before and after.
- Splitting by script with byte-copied bodies is the right method. 087 and 088 should reuse `items.py`/`split.py`.
- Point 1 (the card 047 origs in `input_diag`, F2 row updated) is accepted.
- Point 2 (tables, `install` and engine steps are not blocks) is accepted. Card 089 places the 40 shared functions that stayed.
- Point 3 (the `caller_chain` default only in the prototype) is accepted.
