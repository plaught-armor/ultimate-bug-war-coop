# Card 063: Test runner, part 1: drive the game through commands and state, not window keys

Status: sent 2026-11-04 21:40 to junior a3 (OOM-killed 21:45); resent 21:50 to d7 (lost in session restart 21:51, no work done); resent 21:53 to ultimate-bug-war-coop-bb. Card 064 (navigation and aim) builds on it.

Read `mod/src/autoinput.h` and `.cpp` (card 014, 016 and 020 scripted input), `tools/run_pair.sh`, and one per-card driver, such as `/home/king/.cache/claude-tmp/claude-1000/-mnt-based-backup-Repos-ultimate-bug-war-coop/9414029c-e29b-4cdf-960f-1b9c0331717a/scratchpad/drive62.sh`. Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Resume note (senior, 2026-11-04 21:50)

The first junior (a3) was killed by systemd-oomd at 21:45 partway through this card. Its partial work is on disk, not built and not reviewed: new `mod/src/cmdfile.h` and `cmdfile.cpp` (241 lines), plus edits at 21:43-21:45 to `mod/src/autoinput.h`/`.cpp`, `config.cpp`, `log.cpp` and `mod/CMakeLists.txt`. `build/dwmapi.dll` (21:16) predates all of it. Read those files first: keep what fits the spec above, fix or replace what doesn't. There is no `tools/drive.py` yet.

Memory: the whole machine has 32 GB and oomd kills the editor's cgroup (both sessions) at 90%. Run builds and game pairs inside `systemd-run --user --scope -p MemoryMax=12G <cmd>`, keep builds at `-j4` or less, and never leave a game pair running after its scenario ends.

## Why

Every two-player card ends up with a new throwaway bash driver. Each one does three things:
- It finds a window by process environment and focuses it with `xdotool windowactivate`.
- It sends keys with `xdotool keydown` and `keyup`.
- It waits with `sleep N` or a log grep.

That costs us on every card:
- **Focus stealing.** Keys land in whichever window has focus. Card 058's rejoin typed "my sore" into the join box. The user cannot use the desktop during a run.
- **Blind timing.** `sleep 20`, then press, then hope. Card059d's presses came 1 s after a re-possess and counted for nothing.
- **No world state.** The driver can only grep for log lines that were written for other reasons. It cannot ask where the pawn is, which weapon is equipped, or what the health is.
- **Mixed-up shots.** Both games write to one Screenshots folder, so card 060's shots could not be told apart.
- **A test flag per scenario.** `C50_TEST`, `C57_SOLO`, `C60_HOST_OWN` and the others are hard-coded routes in `hooks.cpp`, and each new route is another one.

## Do

1. **Command file (DLL).** With `UBWCOOP_CMD=1`, each game polls `UBWCoop-<LOG_TAG>.cmd` next to its log every 100 ms, on the game thread, through the existing Tick hook. It reads only the lines appended since the last poll. One command per line; unknown or malformed lines get a `WARN cmd:` line and are skipped.
   - `key <FKeyName> [hold_ms]`: press and release, through the existing autoinput `send` path (the PC's `InputKey`). This works without focus, so it needs no `xdotool`.
   - `axis <FKeyName> <value> <ms>`: one axis event per tick for that long (move and look).
   - `shot <name>`: a screenshot named `<LOG_TAG>-<name>`, through the card 043 path.
   - `quit`: `request_exit`.
   - Each command logs `cmd: <seq> <line> -> done` or `-> failed (<why>)` when it finishes.
2. **State file (DLL).** With `UBWCOOP_CMD=1`, rewrite `UBWCoop-<LOG_TAG>.state` every 250 ms. Write a temporary file and rename it, so a reader never sees half a line. Use one `key=value` line, for example:
   `t=… map=… role=host|client pawn=host-own|server-copy|client-own|none x= y= z= yaw= pitch= hp= weapons=2 equipped=1 objective="…" cmd_seq=…`
   Use only fields the mod already reads somewhere. A field it cannot read is written as `-`, never guessed.
3. **Runner (`tools/drive.py`).** Run it with `python3 -I`, standard library only. It takes a scenario file of steps per role:
   - `<role> key Two`
   - `<role> wait state equipped=1 timeout=10`
   - `<role> wait log "switch to weapon 1: Equip sent" timeout=15`
   - `<role> shot after-switch`

   It writes the commands, waits on state or log with a timeout, and prints one line per step with its time and result. The first failed wait ends the scenario with a non-zero exit code and a shot from both roles. `run_pair.sh` gets `SCENARIO=<file>`, which runs the runner once both games are up, in place of an external driver.
4. **Proof.** Port the card 057 route (the client picks up the case laser, presses 2 and fires) to `tools/scenarios/c57.txt`, and run it with no `xdotool` while another window has focus. It must pass the card 057 Check unchanged.

## Not in this card

Navigation and aiming (card 064). Removing the old test flags; they go once each has a scenario. Any change to the shipped default: with `UBWCOOP_CMD` unset, the DLL behaves exactly as it does now.

## Done when

- `c57.txt` passes twice in a row with the focus on another window throughout.
- No `xdotool` call appears in the run.
- The shots are named per role.
- `logsum` gives ERROR/FATAL 0, both games end with rc 0, and there is no new dump.
- With `UBWCOOP_CMD` unset, a `tools/playtest.sh menu` launch logs no `cmd:` lines and writes no `.state` file.
- fnlen reports no function over 60 lines.

## Report

Junior ultimate-bug-war-coop-bb, 2026-11-04 22:20. Done, with one gap: on the host, the mission-brief movie stays on screen (see Open gap below).

**What was built.**
- **a3's files: kept as they were.**
  - `cmdfile.h`/`.cpp` (parse, poll and state write);
  - `autoinput::axis_event`;
  - `config::read_cmd`;
  - `log::path_next_to_exe`;
  - CMake entry;
  - `offsets::FScreenshotRequest_RequestScreenshot`.
  I reviewed them and they fit the spec. A malformed line uses up its seq and logs `WARN cmd: <seq> "<line>" skipped: <why>`.
- **`hooks.cpp`, card 063 section (before `struct HookSpec`).**
  - `c63_step` runs from `detour_tick` only when `g_c63_on` (`UBWCOOP_CMD=1` and `cmdfile::open` succeeded).
  - It polls every 100 ms into a queue of 16 and runs one command at a time.
  - A key press is never released in the tick that pressed it.
  - `shot` goes through `FScreenshotRequest::RequestScreenshot` (named, no number suffix, UI shown) and lands at `Saved/Screenshots/Windows/<tag>-<name>.png`.
  - `quit` goes through `g_request_exit`.
  - The state line is rewritten every 250 ms. Example: `t=… map=FED_C1M1 role=client pawn=client-own x=-4079 y=9223 z=300 yaw=179.7 pitch=-57.9 hp=100.0 weapons=2 equipped=0 objective=- cmd_seq=1`.
  - pawn is `host-own`, `client-own`, `other` (frontend pawn or dropship) or `none`.
  - `objective` is always `-`, since the mod reads no objective text today.
  - `server-copy` is never written, because the state describes the local controller's pawn.
- **`tools/drive.py`** (`python3 -I`, stdlib only).
  - It waits for `cmd: polling` in both logs, then appends commands and waits for each command's result line. Wait steps read the state or the log, and `sleep N` is a runner-only step.
  - It prints one line per step with the clock time, the time since start and the result.
  - The first failure takes a `fail-line<N>` shot on both roles and exits 1.
  - `--shot-prefix` (run_pair passes the run tag) gives shots like `client-c63e-after-switch.png`, so reruns keep their shots.
  - I tested it offline against a fake game before the real runs.
- **`tools/run_pair.sh SCENARIO=<file>`.**
  - It sets `UBWCOOP_CMD=1` on both games and starts the runner after launch, writing to `logs/<tag>.drive`.
  - The run ends when the runner exits. The games then get 30 s to quit by themselves (both did, rc 0), then `wineserver -k`.
  - It prints `scenario c57.txt: drive rc N`.
  - It also removes stale `.cmd`/`.state` files at the start of a run.
- **`tools/scenarios/c57.txt`.**
  - It ports drive57.sh: client test done, host Enter/Space, client `Two`, then `Equip sent` and `wait state equipped=1`.
  - Then it waits for the host's own move line and the WarriorBug gore line, presses host `Two` and checks the host-own `ConstructWeaponSwitchRequest(1, 0, …)`.
  - Then both games quit. Shots are taken along the way.

**Proof (final DLL 4d65103d90951a24, runs c63e and c63f back to back, `STAGGER=5`, inside `systemd-run --user --scope -p MemoryMax=12G`).**
- **Scenario result:** both runs ended "scenario passed" with drive rc 0, about 105 s each.
- **Card 057 Check lines:** all present in both runs.
  - Client: `NetExecutionPolicy 0 -> 1`, `Equip sent`, and `matches the host` ×4.
  - Host: `EquipWeapon on the server copy`, `0 -> 3`, `relay press (Fire 1) … LaserMorita`, the WarriorBug `SpawnGoreResponse`, and the host-own `ConstructWeaponSwitchRequest(1, 0, …)`.
  - The only WARN is the known `card039: host probe table full (32)`.
- **Logs, exit codes, dumps:** logsum ERROR/FATAL 0 on both sides; host rc 0, client rc 0; new crash dumps: none.
- **No xdotool:** PATH had an `xdotool` shim that logs every call; it logged none. No game window was focused or touched.
- **Display caveat:** this is Wayland, so I could not log which window had focus. The proof is that the runner never focuses a window, and the user's own window kept focus.
- **Shots:** 12 named per role and per run, for example `client-c63e-after-switch.png` (HUD shows the Training Rifle in slot 2, 60/0) and `host-c63e-host-switch.png`.
- **Earlier passes:** c63b and c63c passed on the DLL before the `winkey` try and revert. In c63a, a `host wait state equipped=1` step failed: the host has `weapons=1` (Unarmed only), and that step was not part of the card 057 Check, so I removed it. That run did show the fail path working: both `fail-line30` shots were taken, exit 1, then the games were killed with rc 0.
- **`UBWCOOP_CMD` unset:** a `tools/playtest.sh menu` launch (windowed, null sink, in the 12G scope; same source as the final DLL, before the revert) logged `config: UBWCOOP_CMD unset -- card 063 command and state files off`, no `cmd:` lines, and no `UBWCoop-menu.state`.
- **fnlen:** hooks 1191, cmdfile 12, autoinput 23, config 36, log 5, movieskip 9. None is over 60 lines.

**Open gap: the host's mission brief stays on screen.**
- AUTODROPSHIP unpauses the world, so the route and the log Check pass. But every host shot shows the brief movie, not the game.
- Injected `key Enter`/`SpaceBar` through InputKey does not close it, which matches card 015h.
- I also tried WM_KEYDOWN/UP posted to the game's own window, as movieskip does, as a `winkey` command (run c63d). It did not close the brief either, so I reverted it.
- drive57.sh's xdotool keys worked because the window had OS focus.
- A focus-free fix needs the brief widget closed directly, through the WB_Stack_C close or remove path that the card 015 unpause already finds. That is a follow-up card. Until then, a host's on-screen proof such as "client's sprite holding the rifle on the host screen" needs a manual look or that fix.

## Senior review (2026-11-04 22:25): accepted

Checked myself, not taken from the Report:

- DLL `4d65103d90951a24` is the same in `build/` and the game's `Win64/`. The play build `build/dwmapi-play-a4d3f58896432b28.dll` is untouched.
- `logs/c63e.drive` and `logs/c63f.drive` both end `scenario passed` (about 106 s). There are no xdotool calls; the word appears only in comments.
- logsum c63e and c63f: ERROR/FATAL 0 on both sides. The one host WARN is the existing `card039: host probe table full (32)`, which this card did not cause. Host cmd done lines 9, client 5, no `failed`.
- There are no new crash dumps: the newest `UECC-*` is from 15:28.
- fnlen: hooks.cpp 1191 functions, cmdfile.cpp 12, none over 60.
- The six `*-c63f-*.png` shots are written under the prefix's `Saved/Screenshots/Windows/`. `host-c63f-client-holds-rifle.png` shows the brief movie, which confirms the open gap.
- Code read:
  - c63 in hooks.cpp: a fixed queue with no heap. A key release never happens in its press tick (`c63_run` returns after `c63_start`). The state line is bounded and written only when it fits.
  - `RequestScreenshot` is in the required-resolve table.
  - `g_c63_on` needs both `read_cmd()` and a successful `c63_init()`.
  - run_pair's 30 s quit wait and the rc and dump report are correct.
  - drive.py matches results by seq, including `skipped` WARNs.

Follow-up: card 067 closes the host's brief widget, so host shots show the game. It goes before card 065, whose proof needs the host screen.
