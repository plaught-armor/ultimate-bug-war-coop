# UBW Co-op Mod — Project State

Read this first. It holds the goal, the rules, the build state and the open queue. Everything else is linked.
Keep it an index: replace lines when state changes, and put run results and history in the card.

## Goal
Online co-op mod for the Trooper campaign of Starship Troopers: Ultimate Bug War! (GOG 1.2.0, build 90228), for a host
and up to three clients. It is an injected proxy DLL (`dwmapi.dll`, MinHook, functions by RVA from the PDB) plus
`ubwperf.dll` (card 573). The host is a listen server and the authority. Bug mode is out of scope (user, 2026-11-19).
**Sync rule (user, 2026-11-05): everything about the world and the objectives is the same for every player**,
including clients that join later, and every visible effect the host plays reaches the clients. Prefer one mechanism
per class of change over a per-symptom relay. Exception (user, card 598): sounds past the net cull need not be audible.
Decisions: GOG 1.2.0 only · direct IP (UDP) · tested under umu-run/Proton on this Linux box · friends-only trust.
Tested player counts: two (nearly all runs) and three (card 502). Four were never run, by the user's test scope.

## Layout
- `mod/`: DLL source. `src/ue/` engine-generic, `src/ubw/` game features, `src/perf/` = `ubwperf.dll`.
- `tools/`: `run_pair.sh` (pair or three-instance runs), `drive.py` + `scenarios/`, `logsum.py`, `playtest.sh`,
  `make-package.sh`, `export-public.sh`, `pdb_rva.py` + `symbols.txt`, `crash_stack.py`, `iostore/`, `kismet/`, `pak/`.
- `game/`: lane 1, the play install (read-only except the deployed DLLs). `prefix/`: Proton prefix.
- `build/`: the promoted pair only. `build-b5/`: candidates and every kept build with its .map. `logs/`: run logs.
- `CHANGELOG.md`: player-facing changes per public release. `docs/guide/`: player and developer pages. The public
  README exists only in the public repo.
- `docs/cards/`: **open cards only**. `docs/cards/done/`: finished cards (status lines and text there are history).
  `docs/cards/parked/`: drafts not in the queue. `docs/CARDLOG.md`: one row per finished card.
- `docs/SYNC_AUDIT.md`: dated audit per sync class. `docs/PLAYTEST.md`: how to run a playtest.
  `docs/playtest-*.md`: findings of past playtests.
- `docs/reference/`, read on demand:
  - `engine-facts.md`, `runtime-facts.md`: static engine facts, Proton and harness facts.
  - `objective-routines.md`: objective routines and the run lessons (read before every scenario).
  - `objective-coop-audit.md`: the card 301 per-map objective audit.
  - `design-milestones.md`, `known-issues-2026-11-08.md`, `STATE-2026-11-08-full.md`, `playtest-builds-2026-11-13.md`:
    dated history.
  - `STATE-2026-11-22-full.md`: the full log that this file held until 2026-11-22. References to a dated STATE entry
    or an old STATE line point there.

## Build and verify
- Build: `nice -n 15 systemd-run --user --scope -q -p MemoryMax=12G cmake --build build-b5 -j2`. Never build into
  `build/` before promotion, because `tools/playtest.sh` deploys it at launch. Setup: `docs/guide/development.md`.
- Name a DLL by `sha256sum <file> | cut -c1-16` and keep each build with its .map.
- Run: `SCENARIO=tools/scenarios/<name>.txt DEPLOY=1 FAST=1 tools/run_pair.sh <tag>`; read it with
  `python3 -I tools/logsum.py <tag>`. A return code is no proof: judge by the scenario's end line, void and failed
  lines, CRASH rows, dumps, WARN classes and the predicted rows. Tests are windowed and muted, never `-nosound`.
- Never launch a game while another one runs that you did not start (`pgrep -fa Win64-Shipping`).
- Promotion copies the proven dwmapi and ubwperf pair to lane 1 and `build/`.
- The working folder is no git repository. Public source goes out through `tools/export-public.sh` into a fresh clone
  of the public repo, never into `../ultimate-bug-war-coop-public`, which holds untracked play logs.

## Build state (2026-11-22)
- **Play and release build: b465** (dwmapi `03df06297887feeb`, ubwperf `a59345153c6e5358`). Proof: c448b465a (card
  608). No whole-map soak and no human playtest on it. Rollback: the b463 pair in `build-b5/`.
- **Last candidate: b466** (dwmapi `c43256c1745ffb29`, ubwperf `ccb1fa91ca26e74c`), card 607 fix plus the card 608
  net budget name copy. It failed run c607b466a and is not promoted. The working tree is b466, so a new build from it
  contains both changes. The public source export is b465: it leaves both out, and a rebuild of it matches
  the b465 DLLs in every code and data section.
- Public releases: see `CHANGELOG.md`.

## Mod code rules (every card; the senior rejects violations)
- **No heap in the mod.** No `new`, `malloc`, `std::` containers or strings, and no `HeapAlloc`. Use fixed-size buffers and arrays with explicit caps, and bounds-check every write. A hook runs every frame on the game thread for the life of the process, so any per-call allocation leaks or fragments for hours.
- **Every acquired resource has one owner and one release path, including on error.** That covers handles, `LoadLibrary`, and threads (`CloseHandle` straight after `CreateThread`). For anything with a scope, use a small RAII guard or a single cleanup exit. Never write an early `return` that skips a release.
- **We never own game memory.** Never free, delete, or keep pointers to game objects (UObjects, FStrings, FURLs) across frames. Re-fetch them each time. Use them only on the game thread, inside the hook that received them.
- **Leave the game's ownership and ABI untouched.** Detours pass by-value arguments straight to the original, which still destroys them. If a detour skips the original, it must not leak what the original would have freed. Justify each skip in the card report.
- **No exceptions and no RTTI** (`/EHs-c- /GR-`). Build with `/W4 /WX`, including the linker. Every return value is checked and every failure is logged.
- **Hot paths:** no logging per frame, and logging per event is capped (log the first N, then one "further … not logged" line). Shared state is either written before the hook is enabled or touched only by the game thread. Anything else needs an explicit atomic and a comment saying why.
- **Detours stay tight (user, 2026-11-21: "always").** A detour's cheapest reject runs first (null, class, map not live), before any table scan, string work or log formatting. Hot detours, ranked by the card 571 counter (`UBWCOOP_C571=1`), are tightened first.
- Functions stay at 60 lines or fewer, and loops are bounded.
- **Hook gates (card 047):** a `kHooks` entry with a `gate` is not installed while its flag is off, so its `g_orig_*` stays null. Mod code must never call a gated detour, or its original, directly from code that runs while the gate is off. Card 047 found this with `bind_input_tag`, which crashed with an AV at 0x0. Gate a new hook only when everything it does is logging.

## Card proof and review rules (every card; from the 2026-11-07 playtest)
- **Size fixed tables for a full map, not the test.** Every fixed-capacity table logs its peak. Size it for the busiest full map plus margin. The first time it fills is a WARN, and a full table never silently drops world state. Review checks the logged peaks against capacity. Card 104 copies (64), card 038 dead set (128) and card 095 journal (256) all filled in real play.
- **Coverage run before playtest.** Targeted proof runs (FAST=1, one encounter) never reach table caps, other maps, checkpoint restarts or travel. Before a build goes to the user's playtest, one soak run plays a whole map with both players (card 127 shape) and reports the table peaks and WARN classes.
- **Visual relays prove the receiver shows it.** "spawned" or "played" in a log is not proof. A relayed visual (Niagara, mesh, widget, marker) logs on the receiving side that it is active and rendered (component active, visible, bounds on screen), or the proof run takes a screenshot on the receiving side. Card 074 grenade explosions logged spawned and were invisible.
- **No new WARN class passes review.** Review diffs the run's WARN classes against the last accepted run. A new class is fixed or carded; it is never only counted in the Report.
- **A world-sync card covers the whole class.** It lists every class of that kind on every map from the asset list, not only the ones the test hit. The Report states the classes covered and not covered. Card 098 missed the bug-hole spawners; card 104 missed the laser pickup through its cap.
- **Check the host side with a client in.** Proof runs check the host's own HUD, markers and menus while a client is connected, not only the client.
- **Keep both players' logs.** Each launch writes its own timestamped log, and after a playtest both the host and the client logs are collected.

- **Client must be in the game (found 2026-11-13 by the first client shots viewed).** The 241, 242 and 243
  scenarios dismissed the "PRESS ANY KEY TO CONTINUE" card only on the host; the client's screen stayed on it
  for the whole run (world ticking, rows replayed, key input dead). Every scenario dismisses the card on both
  sides and proves it with a client shot showing the HUD before the first objective step.
- **Aim proof per teleport (user rule 2026-11-13).** After every warp and look at an objective target (switch,
  pickup, hold site, nest, clasp, dropship), the scenario takes a `shot` before the action, named after the
  step, and the junior looks at each one: the target is in view at the crosshair, not a wall. The report lists
  each shot with what it shows; a shot of a wall voids that step's result. The senior spot-checks the shots.

## Gameplay test scenario rules (every scenario; lessons of cards 124-265, collected 2026-11-13)
A run proves something only when the screen would convince a watching player. Each rule names the fault it came from.
- **Guard the launch.** The scenario head voids at once on a wrong DLL (`void unless <role> dll <hash16>`) and a wrong map or state (`wait state ... void`). A probe once ran 573 s on the old DLL and the wrong map.
- **Both players are in the game.** Dismiss the loading card on both sides and prove it with a client shot of the HUD before the first step (241-243 ran with the client on "PRESS ANY KEY").
- **Stands are points, proven at run time.** `warp ... point x y z` at a point a run has shown to be standable, then `wait stand`. Never `warp actor <name> <dist>` (the landing depends on the previous position; 243 run 4 fell 890 uu) and never a stand taken from a static table without checking the instance name (every old switch stand belonged to another switch).
- **Clear before acting (user rule, changed 2026-11-13 16:45).** In objective testing the bugs near an objective are killed by code, not by weapon fire: `clear <radius> kill` on the host (InstaKillTarget on every live bug in the radius, the card 098 `kill actor` path; the deaths replicate as any host kill). Radius about 1500 uu before every press, aim, throw or damage step. The clear takes one step, spends no ammunition and leaves the pawn on its stand. Clears by real fire stay only in scenarios whose subject is fire (cards 181, 276). Defend rounds too (user, 16:56, watching test players stand dry on a defend objective): each round is `host clear 3000 kill`; the waves die by the game's own InstaKillTarget. If a wave count does not advance on code kills, that objective is reported, not chased.
- **Aim is proven by the crosshair, not by arithmetic (user rule).** Looks aim at the target's bounds centre from in front of its face, never at actor origin plus a guessed height and never at a presence volume. The Visibility-channel trace row must name the target (`hit=`); an E use also needs the game's own focus row (`focus=`) on the same look. A distance gate or a focus row alone passed while the screen showed sky, floor or a wall.
- **Shot after every aim, viewed by a person.** A `shot` named after the step follows every gated look; the junior views each against its row and lists the ones it cannot map; the senior views a sample before telling the user anything was proven. Logs alone are never called screen proof.
- **Objectives complete by their real trigger.** No `objective complete`, no health writes on objective targets, no kill command on an objective's own target; presence steps walk in, nests take the real blast. Bugs that only stand in the way go by `clear <radius> kill`. A map is soaked at N of M with both players, through mission end and travel.
- **Objective steps run on live objective data (card 284, user rule 2026-11-13).** Each objective step starts with `host objective expect <id>`, places players with `host objective go`, and ends on `host objective wait <id>`; no hand-kept stand coordinates or actor names. A run in which the user played an instance is void for that side.
- **Steps wait on log rows, never on sleeps.** A step that depends on state waits for its row and voids on timeout; failsoft must not let long waits run out one after another.
- **Match rows that every class has.** A cross-side check (deaths, effects) uses a row both sides write for every class, keyed by guid. A guid of 0x0 on the host means "not yet replicated to the client". The ragdoll rows made 10 chariot deaths look unsynced.
- **Human inputs.** Real key and mouse paths at human rates (card 124); cinematics skipped by the game's own skip button.
- **One launch proves many things.** Batch probes and fixes per build and run; a new tag per run so logs are never overwritten; the report quotes rows, frame windows on both sides (at least 500 frames per 10 s outside load and join) and the WARN class diff.
- **Machine state.** GPU busy under 30 percent before a pair; runs pinned, niced and frame-capped while other work runs on the machine (`tools/run_pair.sh` footprint line); never two game pairs; no build while a game runs.
- **A gated look only where the target is live.** A switch has no focus before the objective that arms it (Switch_C_2 before PowerPlant_SO1). Gate a target inside its own objective step, after its Add row; do not probe it out of order.
- **Ammunition is part of the route.** Count rounds per kill against the reserve before a run; refill only from the game's own sources (racks, pods, pickups) as real steps; no ammo writes. Fire is held only while the crosshair trace names the target.
- **Death drops are the ammunition route.** A weapon pickup of a class the player holds credits ammunition (MoritaMk1 drop: Morita 60, Underslung 8; card 266, relayed to the client). Both roles walk over drops after a clear. The reserve floor (60) is judged on each clear's start row.
- **Never fight a live spawner.** Next to a nest or a running wave: radius 400, cap 15 s, then the gated look and the action with no step in between; the action ends the spawner. Only defend waves that the objective counts are killed out.
- **Stand z is measured.** `wait stand` gates on z only where a logged rest row gave it; otherwise `any` (x, y only). A warp never drops a pawn more than about 300 uu. No stand step after a clear.
- **A readout never acts.** A step that only reads state must not fire, move or carry another verb's void conditions.
- **Desk-check new step forms.** Every step form new to a generator is read against the code that executes it (end conditions, void conditions) and the scenario passes `drive.py --parse` before launch.
- **Discovery run first.** A new route runs once with `voidsoft` to show every fault in one launch; only a later run without any void counts as the soak.
- **Three probe failures: change method.** Stop probing and move the proof into the real scenario or re-scope.
- **Diagnostics can be the fault.** A cost seen only with a diagnostic switch on (card 263) is measured once with the switch off before it is called a game or mod problem.

## Card scope and investigation rules (every card; from card 132, 2026-11-08)
Card 132 ran from the evening of 2026-11-07 through hb58 the next day, one new gap per run. These rules apply to every card from now on.
- **Trace before you run.** For any sync or relay item, write the whole path into the card before code: the host-side call (address), what carries it (UE replication, naming the owning actor or ASC and its bReplicates, or a mod relay op), the client-side entry (address), and a verdict, each line marked verified or open. Sources: the PDB, disassembly, kis.py for Blueprint bytecode, and this file. The senior reviews the trace before code.
- **A run checks written predictions.** Before a run, the card lists what each fix must show (a count, a log line, a screenshot). A run is never the way to find a mechanism. If a run surprises us, the trace was wrong: go back to the code, not to another run.
- **Check premises against this file.** A fix that rests on "the engine sends X" or "the client already does Y" names the line here or the disassembly that shows it. The card 132 round capture assumed cues reach the client by multicast; this file already said (now `reference/engine-facts.md`) that pooled rounds and weapons do not replicate.
- **One goal per card, three runs.** A card has one goal and a budget of three runs. A finding outside the goal becomes a new card; review never widens the card in flight. When the budget runs out, stop and re-scope with the senior (and the user if a user rule is involved).
- **One manager per class.** When a second relay of the same kind is proposed (a second effects relay, a second journal), extend or build the one mechanism that owns the class instead (card 136, the effects manager).
- **Batch fixes per run.** Every finished fix goes into the next run together. No run proves one fix while others wait.
- **Test and audit code is held to the same bar as mod code.** It must never crash the game:
  - Any UObject pointer kept across frames is an FWeakObjectPtr, or its table is cleared at map load and on pooled reuse.
  - Validity is never checked by reading the object itself.
  - No hook that a relay or journal depends on sits behind a diagnostic gate. Each Report lists the "hooks: skipped" line and confirms that.
- **Spawn-then-hit ordering.** A relay that numbers something at spawn and reads it at hit or death must handle a hit in the same frame as the spawn (the card 132 point-blank nuke).
- **Replays set state.** A late-join or reconnect replay applies the end state (destroyed, dead, on or off). It never re-runs one-shots: death broadcasts, explosions, sounds, debris, decals.
- **No dead time.** drive.py prints a "dead time:" line after every run. Any timed-out wait is fixed or removed before the next run, and sleeps are the minimum.
- **Numbers come from the log.** Every count in a Report is read from a log line or a tool's output and quoted. A claim found wrong is corrected in the next message.

## Open queue (2026-11-22)
Work stopped at b465 by the user's order until the user's playtest. New finds are carded only. Next card number: 611.

| Card | What is open | Waits on |
|---|---|---|
| 607 | A self-kill or friendly fire that only downs starts Traitor and blocks FED_C1M1. Fix in b466, proof failed at step 3 | after playtest |
| 610 | InstaKillTarget at revive Health left a player at 0 and alive in b466. Cause and player impact unknown | after playtest |
| 608 | The crash fix is in b465. The net budget name copy is only in b466 | ships with the next promoted build |
| 609 | Effect journal may keep a class pointer past its life (static, never seen) | after playtest |
| 573 | Perf mod: step C, then further cuts by GameThread ms | after playtest |
| 301 | Objective co-op audit: every map has a full pass, some checks open | after playtest |
| 243, 244, 246, 247 | Whole-objective soaks: C3M2, C4M1, C5M1, C5M2 | after playtest |
| 302, 304, 314 | Sound fixes measured, hearing check open | playtest with sound |
| 268, 272 | CTF versus mode and arena editor, scoping | after the co-op playtest |

Parked drafts are in `docs/cards/parked/`.
