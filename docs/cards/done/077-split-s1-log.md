# Card 077: Split S1: create `src/ue/`, move the logger and the log-cap helpers

Status: sent 2026-11-05 13:06 to junior ultimate-bug-war-coop-bb, after card 076 was accepted. Drafted 2026-11-05 (senior). This is the first move card of the split (S1 in `docs/split-map.md`, "Move order").

Read `docs/split-map.md` ("Proposed files", "Move order", "CMake", "Renames when promoted to `ue/`"), `mod/src/log.h`, `mod/src/log.cpp` and `mod/CMakeLists.txt`. Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

`hooks.cpp` is 27.6k lines in one translation unit. The split moves leaves first, so each later card can include what it needs from `ue/`. The logger is the leaf every file uses, and it is the smallest move that sets up the directory, the shared header and the CMake change.

## Do

1. **Directory and CMake.**
   - Create `mod/src/ue/`.
   - Add `target_include_directories(ubwcoop PRIVATE src)` to `mod/CMakeLists.txt`.
   - Keep the explicit source list. Do not use a glob.
2. **Logger.**
   - Move `mod/src/log.h` and `log.cpp` to `mod/src/ue/log.h` and `ue/log.cpp`.
   - Rename the namespace `ubw::log` to `ue::log`, because `ue/` must not name UBW.
   - Update every `#include "log.h"`: `config.cpp`, `dllmain.cpp`, `proxy.cpp`, `lobby.cpp`, `cmdfile.cpp`, `autoinput.cpp`, `movieskip.cpp` and `hooks.cpp`.
   - Callers that write `log::line` inside `namespace ubw` need one alias each (`namespace log = ue::log;`). Put it at the top of each file's namespace, not in a header, so the alias is visible where it is used.
3. **Log-cap helpers.**
   - Move `note_skip`, `note_event_cap` and `note_event` (hooks.cpp ~523 and ~1174) and the constants they need (`kMaxLoggedSkips`, `kMaxLoggedEvents`) into `ue/log.h` and `ue/log.cpp`.
   - Their log text stays exactly the same.
4. **`ue/core.h`.** Create it as the internal header for later moves. For now it only includes `ue/log.h`. Do not add anything else to it in this card.
5. **No behaviour change.** Do not rename any other symbol, and do not reorder hooks.

## Proof

- **Build:** `systemd-run --user --scope -q -p MemoryMax=12G ninja -C build -j4`, with no warnings (`/W4 /WX`).
- **fnlen:** no function over 60 lines.
- **Base routes:** `host-morita`, `client-zipline`, `c47-gates` and `c75-solo` all pass, with `DEPLOY=1` on the first run.
- **Log check:** compare the `c75-solo` host log with the card 076 build's `c75-solo` host log. Strip the timestamps, thread ids, pointers and the per-run numbers (`t=`, `ms`, positions) first. The line texts must match; the Report shows the diff command and any lines that differ, with the reason.
- `logsum` gives ERROR/FATAL 0, rc 0, and there is no new dump.

## Done when

- `mod/src/log.*` no longer exist, and `mod/src/ue/log.*` and `ue/core.h` do.
- `grep -rn 'ubw::log' mod/src` finds nothing, and `grep -n 'bool note_skip\|bool note_event' mod/src/hooks.cpp` finds nothing.
- All four base routes pass, and the log check shows no text change.
- `hooks.cpp` line count before and after is in the Report.

## Report

Done (junior, 2026-11-05). Deployed DLL hash 5c06bc142cac5fe4.

### What changed

- `mod/src/ue/` was created, and `mod/src/log.h` / `log.cpp` were moved to `ue/log.h` / `ue/log.cpp`. The namespace is now `ue::log`.
- `mod/CMakeLists.txt`:
  - The source list entry is now `src/ue/log.cpp`. The list is still explicit, with no glob.
  - Added `target_include_directories(ubwcoop PRIVATE src)`.
- Every `#include "log.h"` is now `#include "ue/log.h"`. That covers `config.cpp`, `dllmain.cpp`, `proxy.cpp`, `lobby.cpp`, `cmdfile.cpp`, `autoinput.cpp`, `movieskip.cpp`, `hooks.cpp` and `ue/log.cpp` itself.
- Namespace aliases:
  - `namespace log = ue::log;` is the first line inside `namespace ubw::<name> {` in `config`, `proxy`, `lobby`, `cmdfile`, `autoinput`, `movieskip` and `hooks`.
  - `dllmain.cpp` has no `ubw` namespace. It writes `ue::log::` in full, in its 9 uses.
- `note_skip`, `note_event_cap` and `note_event`, with `kMaxLoggedSkips` (8) and `kMaxLoggedEvents` (16), moved into `ue/log.h` / `ue/log.cpp`. Their bodies and log text are the same: "guard: %s ... further skips not logged" and "diag: %s ... further events not logged".
  - `hooks.cpp` reaches them through `using ue::log::note_event; using ue::log::note_event_cap; using ue::log::note_skip;`, placed after the alias, before `namespace {`.
  - The two constants are used only inside the helpers, so `hooks.cpp` needs no declaration for them.
- `ue/core.h` was created. It only includes `ue/log.h`.
- No other symbol was renamed, and no hook was reordered.

### Checks

- `hooks.cpp` lines: **28094 before, 28063 after** (-31).
- The build (`systemd-run --user --scope -q -p MemoryMax=12G ninja -C build -j4`) is clean with `/W4 /WX`.
- fnlen:
  - `hooks.cpp`: 1439 functions (1442 before, minus the 3 moved), 0 over 60.
  - `ue/log.cpp`: 8 functions, 0 over 60.
- `grep -rn 'ubw::log' mod/src` finds nothing.
- `grep -n 'bool note_skip\|bool note_event' mod/src/hooks.cpp` finds nothing.
- `mod/src/log.*` are gone. `mod/src/ue/` holds `core.h`, `log.cpp` and `log.h`.

### Runs

| Run | Route | Result |
|---|---|---|
| c77a | `c75-solo` on the card 076 DLL (3150ef2db7259a92), before any change | passed, rc 0, ERROR/FATAL 0, WARN 0, no dump |
| c77b | `host-morita`, `DEPLOY=1` | deployed; rc 0/0, ERROR/FATAL 0, WARN 0, no dump; **scenario FAIL** at line 16 (EquipWeapon wait), see below |
| c77c | `host-morita` with `UBWCOOP_DIAG=1` on the host | passed, rc 0/0, ERROR/FATAL 0, WARN 0, no dump |
| c77d | `client-zipline` | passed, rc 0/0, ERROR/FATAL 0, WARN 0, no dump |
| c77e | `c47-gates` | passed, rc 0/0, ERROR/FATAL 0, host WARN 0, client WARN 1, no dump |
| c77f | `c75-solo` on the new DLL | passed, rc 0, ERROR/FATAL 0, WARN 0, no dump |

- **host-morita and the card 047 gate (pre-existing; nothing changed here).**
  - The scenario waits for "card057: host ASTPlayerCharacter::EquipWeapon(1) on .*host-own".
  - That line comes from `detour_c57_equip_weapon`, which is gated by `g_gate_diag` (`kHooks` row `ASTPlayerCharacter_EquipWeapon`, `&g_gate_diag`). It is in the c77b "card 047 skipped" list.
  - The last passing host-morita run, c64o, predates the card 047 gate.
  - With `UBWCOOP_DIAG=1` the route passes (c77c).
  - Suggestion: put `UBWCOOP_DIAG=1` in the scenario's Run line, or wait on the card021 Slot2 line only.
- **c47-gates client WARN.** It is "WARN card044: client no CompleteDeath 8006 ms after StartDeath ...". That is the card 043 test path, and the same line is in `c75a13-client.log` and `card047x-client.log`.

### Log check (c75-solo host log, c77a before vs c77f after)

The normaliser is in the scratchpad at `~/.cache/claude-tmp/claude-1000/-path-to-ultimate-bug-war-coop/50a11dc8-a0b2-4c6f-af73-8cd1b2a1b3e9/scratchpad/c77norm.sh`. It strips the `[ms][T]` prefix, pointers and hex, `t=`, `ms`, floats, and numbers of 4 or more digits:

```
sh c77norm.sh logs/c77a-host.log > a.n; sh c77norm.sh logs/c77f-host.log > f.n
diff a.n f.n                       # 159 lines: event order and counts (spawn points, bugs, troopers dying)
for r in a f; do sed -E 's/[0-9]+/N/g' $r.n | sort -u > $r.u; done
diff a.u f.u                       # the line texts
```

The ordered diff differs only where gameplay differs: which actors toggle collision, the order of bug spawns, how many troopers die near the pod, and the mech's walk positions. Compared as sorted line texts (740 vs 741 unique), there are two differences:

- the run tag in the `shot` command lines (`c77a-*` vs `c77f-*`);
- c77f has one extra "diag: card042 host gore parts ... further events not logged". This is the moved `note_event_cap` reaching card 042's cap of 48 (`kC42HostLines`). c77f logged 48 gore part lines, and c77a logged 47, which stays below the cap. More troopers died in c77f. The cap line's text is unchanged.

All other cap lines from the moved helpers are the same in both runs: "diag: card021 input tag pressed", "character collision write", "collision write" and "pause request ... further events not logged".

## Senior review (2026-11-05 13:20): accepted

- **Runs.** I checked c77a-c77f myself:
  - Every drive ends with `scenario passed`, except c77b.
  - The newest crash dump is still the 11:31 one from c75a12.
  - The deployed DLL matches `build/dwmapi.dll` (5c06bc142cac5fe4). It was deployed at 12:59:20, after c77a ended (12:58:55, on the card 076 DLL) and before c77d-f started.
- **Code.** `mod/src/log.*` are gone, and `mod/src/ue/` holds `core.h`, `log.cpp` and `log.h`. `core.h` only includes `ue/log.h`. Both grep checks are empty.
  - `hooks.cpp`: 28063 lines; fnlen 1439 functions, 0 over 60.
  - `ue/log.cpp`: fnlen 8 functions, 0 over 60.
- **Log check accepted.** The one extra cap line in c77f comes from a gameplay count (48 gore part lines against 47). Its text is unchanged.
- **host-morita (c77b).** The failure predates this card, and the junior's cause is right:
  - `card057: ... EquipWeapon(1)` comes from a `g_gate_diag` row.
  - The `card021: host AbilityInputTagPressed` lines log without the diag flag (4 in c77b).
  - Senior fix in `tools/scenarios/host-morita.txt`: the EquipWeapon wait is removed, with a comment, and a `sleep 2` comes before the Two press. The base route must pass with the default flags, so it does not set `UBWCOOP_DIAG=1`.
  - Card 078's first host-morita run proves the fixed scenario.
