# Card 061: Map `hooks.cpp` for the `ue/` and `ubw/` split (no code moves)

Status: sent 2026-11-05 11:52 to junior ultimate-bug-war-coop-bb, after card 075 was accepted. Drafted 2026-11-04 (senior), refreshed 2026-11-05. Cards 060, 047 and 056 are done.

Read `docs/STATE.md` (the "(planned)" split row and "Architecture (planned)"), `mod/CMakeLists.txt`, and the top of `mod/src/hooks.cpp` (globals and `HookSpec`). Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why

`mod/src/hooks.cpp` is 27.0k of the project's 30.5k lines (2026-11-05: 1385 functions, 222 `kHooks` entries, 119 of them gated by card 047). Every card reads into it, and a second Unreal game cannot reuse any of it while engine glue and game features share one file. The split moves code without changing behaviour, one feature per card. Before the first move, a map is needed: which line ranges are engine-generic, which are game-specific, and what each part depends on. Moves made without a map leave cycles between files.

## The rule for each side

- **`ue/`** holds engine-generic code: the proxy DLL loader, the hook table and installer, reflection (GUObjectArray, FName, `StaticFindObject`, `ProcessEvent`, property lookup), FString, TArray and TMap helpers, the listen, travel, join and possess glue, the net channel and its journals, and log and config. It must not name a UBW class, asset, offset or card feature. It stays tied to this one UE version until a second game exists.
- **`ubw/`** holds game features and their offsets: `offsets.generated.h`, weapons, abilities, objectives, pickups, interactions, death and checkpoint, the minimap, and the lobby UI.

## Do

1. **Section map** (`docs/split-map.md`). One row per contiguous block of `hooks.cpp`, with these columns:
   - line range and a short name;
   - side (`ue`, `ubw`, or `mixed`; a `mixed` row says what has to be cut apart);
   - the target file;
   - the globals it owns, and the globals it reads from other blocks.
2. **Dependency list.** Every place where an engine-generic block calls into or names game code, such as a `ue` helper that checks `g_player_mi_class()`. Each needs a seam: a callback, a table row, or moving the call up into `ubw/`. Propose the seam per row. Keep it to plain functions and tables: no interfaces with one implementation, and no registries.
3. **Move order.** The order of cards that moves leaves first: log, config, FString and reflection, then the hook table, then the net channel, then the game features one at a time. Name the accepted test routes each move must rerun.
4. **Build check only.** Do not move any code in this card. Confirm `mod/CMakeLists.txt` can take subdirectories (a source list change only) and say what it needs.

Use `grep` and `Read` over line ranges. Do not load the whole file at once. A single read-only subagent is fine if the map would pull more than about 50k tokens into context.

## Refresh (senior, 2026-11-05)

- The `kHooks` table now has a `gate` column (card 047). Map each gate bool to the side that owns its flag. The table itself, `HookSpec` and `install_hooks` are `ue/`. Most rows name `ubw/` detours, so propose how the table is assembled from per-side rows without a registry: for example, one `ue/` array of the engine hooks and one `ubw/` array, each installed by the same loop.
- Card 047's hazard applies to every move: code that calls a gated detour or its `g_orig_*` directly must stay with that detour. List each direct call between blocks that the map puts on different sides.
- New since the draft: cards 062 to 074 added the ammo, grenade, pickup and objective-marker relays. All of them are `ubw/` features that use the `ue/` channel and journal. Check that each one sends only through the channel's public calls.

## Done when

- `docs/split-map.md` covers every line of `hooks.cpp` with no gaps or overlaps (the Report states the check).
- Every `mixed` row and every dependency row has a proposed seam.
- The move order lists the cards, each with the routes to rerun.
- No source file has changed (the Report gives the hash of `build/dwmapi.dll` before and after, which must be the same).

## Report (junior, 2026-11-05)

Done. The map is `docs/split-map.md` (450 lines). No source file changed.

**Sizes, recounted after card 075:**

- `hooks.cpp` is 27626 lines, out of 31152 in `mod/src`. It has 1416 functions, none over 60 lines.
- `kHooks` has 225 rows: 220 `ubw` detours and 5 engine detours (Init, Browse, LoadMap, Listen, Tick). 116 rows are gated: 115 `ubw` rows, plus Listen, which is gated by `g_gate_diag`.
- `kCalls` has 211 rows: 152 `ubw` slots and 59 `ue` engine slots.
- There are 11 gate bools.

**Coverage check:** 106 contiguous blocks run from line 1 to line 27626. Each block starts on the line after the previous one ends, and the lengths sum to 27626. So there are no gaps and no overlaps. This was checked twice: by the scanner, and again by parsing the table in the finished doc.

**Section map:**

- 4 blocks are `ue`, 57 are `ubw` and 45 are `mixed`.
- Each `mixed` row lists the definitions cut out to each `ue/` file.
- `khooks` and `kcalls` split row by row into `kHooksUe`/`kHooksUbw` and `kCallsUe`/`kCallsUbw`. One `install_hooks(span)` loop runs over both. One small order change: Tick is installed fifth instead of 78th. This is harmless, because install ends about 21 s before "UGameEngine::Init fired" (run c75s1).

**Dependencies:** the scanner found 36 `ue` definitions that use `ubw` names. Each has a seam:

- **`EngineSteps`**, a plain struct of 8 function pointers. The engine detours call the per-card steps through it, and `ubw/hooks_ubw.cpp` fills it with today's call bodies, verbatim.
- **`install` moved up:** `dllmain` calls `ubw::init` and then `ue::install(InstallSet)`. The PDB identity and the `Symbol` span are passed in.
- **Channel:** `ChanOp` and the per-op table (arg count, label, host handler, client handler) go to `ubw/chan_ops.cpp`. The `ue` dispatcher indexes the table. The channel keeps its own `ServerChangeName` FName cache.
- **Typedefs and engine constants** move with their slots.
- **Renames:** generic helpers that carry a card name (`c24_fname`, the c027 walker, `request_shot` and others) get `ue` names, listed in a rename table.

**Gates:** `g_gate_diag` is `ue`. It is set from `config::read_diag()` and also gates `UWorld_Listen`. The other ten gates go to `ubw/flags.cpp`.

**Card 047 hazard:**

- No direct call of a detour or a `g_orig_*` crosses the `ue`/`ubw` line in either direction.
- Inside `ubw`, three gated pairs cross files (c014, c019.2 and c019.4 against origs in input_diag). The rule for each: the orig moves into its detour's file.
- The ungated cross-file uses are listed in the doc.

**Relays:** every relay from cards 062 to 075 sends only through the channel's public calls (`chan_send_host`/`chan_send_client`, `chan_text`/`chan_text_frame`, `WireString`, `ChanOp`, `object_index`). The older leaks are fixed by card S7:

- c041 and c043 call `chan_to_client_args`;
- c049 and the replay steps use `kChanPrefix`/`kChanPrefixLen` and the two objective frame constants.

**Move order:** 19 cards.

- S1-S7: log, config, reflect, engine slots, hook table, net detours, channel.
- F1-F12: one feature group each.
- Each card names its routes, on top of the base set: `host-morita`, `client-zipline`, `c47-gates` and `c75-solo`.

**CMake:** add the `src/ue/*.cpp` and `src/ubw/*.cpp` files to the explicit source list, and add `target_include_directories(ubwcoop PRIVATE src)`. Nothing else changes.

**Hash:** `build/dwmapi.dll` sha256 was `4c6128e90f9d629d7f8e5b3a5d147c6b3e03bf3799326fbff4afe8027c87237f` both before and after. The md5s of all `mod/src` files match the baseline taken at the start of the card.

**Method caveat:** a name used by zero or one other block stays with its block, even when it is engine-generic. A move card may lift such a helper into `ue/` itself, and that adds no `ue` → `ubw` edge.

## Senior review (2026-11-05 12:15): accepted, with one correction to the map

- Coverage: I parsed the section map myself. It has 106 rows, each starting on the line after the previous row ends, from 1 to 27626, which matches `wc -l mod/src/hooks.cpp`. Sides: 4 `ue`, 57 `ubw`, 45 `mixed`.
- No source file changed: nothing under `mod/src` is newer than the card 075 review, and `build/dwmapi.dll` hashes to `4c6128e9…87237f` (11:38 build), as the Report says.
- Every route the move order names exists in `tools/scenarios/`.
- `sprint_side` reads only the actor roles, so moving it to `ue/net.cpp` as `role_side` fits the rule.
- **Correction:** the c016.3 row cut `kAscInputTagMap`, `kAscInputTagMapNum`, `kAscInputTagMapStride` and `kAscInputTagMapMax` to `ue/gas.cpp`, and the c017 row cut `kAscPendingBindNum`. These are offsets in the game's `UADAbilitySystemComponent` (`AbilityInputTagPressed`, `BindInputTagToAbilitySpec`), so `ue/` would name a UBW offset. I edited both rows to keep them in `ubw/`, and added a "Senior correction" section that tells card S4 to check every other `ue/gas.cpp` offset the same way.
- The S1-S7 and F1-F12 cards are not drafted yet. They are drafted one at a time from this map.
