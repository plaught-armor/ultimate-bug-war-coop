# UBW Co-op Mod — Project State

Read this first. It holds the goal, the rules, the build state and the open queue. Everything else is linked.

## Goal
Online 2-4 player co-op mod for Starship Troopers: Ultimate Bug War! (GOG 1.2.0, build 90228), as an injected proxy
DLL (`dwmapi.dll`, MinHook, functions by RVA from the PDB). The host is a listen server and the authority.
**Sync rule (user, 2026-11-05): everything about the world and the objectives is the same for every player**,
including clients that join later, and every visible effect the host plays reaches the client. Prefer one mechanism
per class of change over a per-symptom relay.
Decisions: GOG 1.2.0 only · direct IP (UDP) · tested under umu-run/Proton on this Linux box · friends-only trust.

## Layout
- `mod/` — DLL source: `src/ue/` engine-generic, `src/ubw/` game features (68 files, about 66k lines).
- `tools/` — `run_pair.sh` (two-instance run), `drive.py` + `scenarios/` (scripted runs), `logsum.py`,
  `playtest.sh`, `pdb_rva.py` + `symbols.txt` (hook RVAs), `crash_stack.py`, `iostore/`, `pak/`.
- `game/` — extracted install (read-only except the deployed `dwmapi.dll`). `prefix/` — Proton prefix.
- `build/` — CMake build dir and saved DLLs (`dwmapi-play-<hash>.dll` are play builds). `logs/` — run logs.
- `docs/cards/` — **open cards only**, one file each. `docs/cards/done/` — finished cards (read one only when an
  open card names it). `docs/cards/parked/` — drafts not in the queue. `docs/CARDLOG.md` — one row per finished card.
- `docs/reference/` — facts moved out of this file, read on demand:
  - `engine-facts.md` — engine, replication, inventory, ability and interaction facts (static).
  - `runtime-facts.md` — Proton launch, run_pair/drive/logsum usage, screenshots, crash dumps, early card results.
  - `design-milestones.md` — milestones, downed/revive death flow, lobby overlay groundwork.
  - `known-issues-2026-11-08.md` — the old known-issues list with its fix history (not re-verified since).
  - `STATE-2026-11-08-full.md` — this file as it stood before the 2026-11-11 audit.
- `docs/PLAYTEST.md` — how to run a playtest. `docs/playtest-*.md` — findings per playtest.

## Build and verify
- Build: `systemd-run --user --scope -q -p MemoryMax=12G cmake --build build -j4` (configure line and the generator
  steps are in `reference/runtime-facts.md`). `/W4 /WX`; a clean build passed on 2026-11-11.
- Name a DLL by `sha256sum build/dwmapi.dll | cut -c1-16`. The hash differs between build directories, so compare
  hashes only for the same file.
- Run: `SCENARIO=tools/scenarios/<name>.txt DEPLOY=1 FAST=1 tools/run_pair.sh <tag>`; read it with
  `python3 -I tools/logsum.py <tag>`. Unattended runs set `UBWCOOP_AUTODROPSHIP=1`, audio to the null sink, never
  `-nosound`. There is no unit test suite: the build and a scripted pair run are the verification.
- Never launch a game while another one runs that you did not start (`pgrep -fa Win64-Shipping`).

## Build state (2026-11-12)
- **Play build: `3d3f70b0bb6ba6de`** (2026-11-12, `build/dwmapi-play-3d3f70b0bb6ba6de.dll`,
  `dist/UBW-Coop-20261112-2351.zip`), same DLL as the working tree: `83cf0b9cdcfe0cb0` plus card 255 (either
  player can board the FED_C2M1 mech; runs c255-c2m1-r2 and c257-c2m1, FED_C2M1 21 of 23, no crash). Previous:
  `83cf0b9cdcfe0cb0` (`dist/UBW-Coop-20261112-1757.zip`) = `d561aa4a8628ec3f` plus card 248 (satchel hold
  play blocker). On this DLL: c248-c1m1 and c240-c1m1-r7 (FED_C1M1, 26 of 27 objectives by real trigger, 0 digest
  mismatches, no crash), c240-c1m1-r8 (through level end and the frontend brief to FED_C2M1 on both sides), c248-c133 (client-run hold). Other
  maps soaked on `d561aa4a8628ec3f` (c238-c2m1, c235-c3m2, c237-c4m2, c238-c5m2) and `20cf859dd191a8fe` (c229-c3m1,
  c127-c4m1-r2, c234-c5m1), not yet by full objective chain (cards 241-247). Previous: `d561aa4a8628ec3f` (holds
  the satchel hold blocker), `20cf859dd191a8fe` (holds the card 238 crash), `2928654b3a96090d`, `8edaffb0745a50fe`
  (none play-tested), `7044ed8c10577a0c` (2026-11-08 playtest).
- **Play build: `5e2ce00acad8efa3` (build 4)** (packaged 15:33 2026-11-13, `dist/UBW-Coop-20261113-1713.zip (repackaged 17:13, same DLL)`, `build/dwmapi-play-5e2ce00acad8efa3.dll`; working tree and `game/` hold the same DLL). Proof runs: FED_C1M1 `c224-c1m1-b4d` and FED_C3M1 `c230-c3m1-b4e` done (all objectives, travel to FED_C3M2, 0 ERROR). FED_C3M2 `c243-c3m2-b4a` stopped at 538 s, 0 of N objectives: scenario fault, not a mod fault. The scenario file held 0 `ammo fill` steps (the generator emits them with `FILL = True`; the file was not made by the current `tools/gen_c243.py`), both players reached Morita 0/0/240 and the first nest (`Monorail_SO2_A`) never fell. In those 538 s: 0 ERROR, 0 FATAL, no crash dump, WARN classes all known (host card157 40, card273 3, card132 3; client card132 19, card152 12, card269 6, card140 1). No rerun before the playtest; the next FED_C3M2 run is batched with build 5 on a regenerated scenario. Previous play build: `3d3f70b0bb6ba6de` (line above).
- **Build 5 (working tree, not built into `build/`, compile-checked in `build-b5/` as `e428119d0689e9ce`):** cards 283, 281, 218 diagnostic, 276 press-end and reload rows coded and reviewed by the senior 2026-11-13 (static); scenario `tools/scenarios/c243-c3m2-b5.txt` regenerated. Playtest moved to later (user, 16:18): build 5 runs go now, staged from `build-b5/`; the play DLL is restored to `build/` and the game directory after the last run or by 18:00. Then one FED_C3M2 run (243, 276, 274, 281, 263) and one FED_C3M1 run (283), FED_C2M1 for 218. Also coded, compile-checked as `b2cd20ed2bd34cfe`: card 209 (player rows by NetGUID; senior review accepted, proof needs the 3-instance harness card) and card 153 (UPnP and external address; review fixes done and accepted; owes one manual host-only launch) and card 181 (per-round `ChanOp::Round`, host copy fires only reported rounds; senior review accepted; owes one FED_C1M1 FAST run in the card 180 shape). Current compile hash `faaa085d7714170b`. Run a (`c181-b5r3`): short presses exact; both holds fail (8 vs 1, 7 vs 1) on a card 276 re-press fault (tries 2 and 3 release the active fire ability, `fire.cpp:308`); fix spec and rerun predictions in card 181; fix goes into the next compile. **Build 6** `c328f2f6a759ece0` (compiled in `build-b5/`, kept as `build/dwmapi-b6-c328f2f6a759ece0.dll`): build 5 plus the card 276 re-press fix, `down client2` / `warp client2`, held Warp log fix, `clear <radius> kill`. Run b (283) accepted on logs, client shot owed. Run c on build 5 stopped (test players out of Morita reserve; clears now by code). Owed on build 6: 181 rerun `c181-b6`, C3M2 `c243-c3m2-b6`, headless probes, then play DLL `5e2ce00acad8efa3` restored. Open small item: stale header comment on `c265_clear_kill` (says unscanned bugs count as left; code does not).
- **Build 4 cards with no proof yet:** 200, 226, 252, 253, 193, 221, 239, 219 have 0 rows under their own `cardNNN` prefix in all three build 4 runs (`c224-c1m1-b4d`, `c230-c3m1-b4e`, `c243-c3m2-b4a`), host and client. Either the case did not occur or the fix logs under another prefix. For each, the next run report must quote the prediction row named in the card or say "case not reached"; none is closed. Refresh 2026-11-15: 253 has rows (`c333-soak-c2m1` client `card253: client count LOS_O02_SO05 1 to 2 before CompleteSubObjective`; `c243-c3m2-b6` `Monorail_SO2 3 to 4`); 221 has its row in `c276b67` (client `card221: client pickup BP_WeaponPickup_MoritaMk1_C_0 local enable set back` at 406937471, host `world E ... 1 ... applied` 47 ms later); 239's guard is coded for b68 (unproven); 200, 226, 252, 193, 219 still have 0 rows in every log.
- Previous working tree DLL `61cf21ee94ecdd3b` (2026-11-13). It differs from the play build `3d3f70b0bb6ba6de` only in the test kill helper (card 259); not repackaged. It holds card 255 and every card closed after
  the playtest (141, 142, 143, 144, 145, 146, 148, 152, 154-170, 172-176, 177, 178, 179, 180, 182, 183, 184, 185, 186-188, 192, 195, 196, 198, 199, 201, 204, 206, 208 in part, 210, 211, 212, 213, 214, 147 in part, 171, 222, 135 in part, 227, 229, 235, 238). Soaked and packaged 2026-11-12.
- No git repository (user decision 2026-11-11). The senior snapshots `mod/`, `tools/` and `docs/` before each card
  and reviews by `diff -ru` against the snapshot.

## Mod code rules (every card; the senior rejects violations)
- **No heap in the mod.** No `new`, `malloc`, `std::` containers or strings, and no `HeapAlloc`. Use fixed-size buffers and arrays with explicit caps, and bounds-check every write. A hook runs every frame on the game thread for the life of the process, so any per-call allocation leaks or fragments for hours.
- **Every acquired resource has one owner and one release path, including on error.** That covers handles, `LoadLibrary`, and threads (`CloseHandle` straight after `CreateThread`). For anything with a scope, use a small RAII guard or a single cleanup exit. Never write an early `return` that skips a release.
- **We never own game memory.** Never free, delete, or keep pointers to game objects (UObjects, FStrings, FURLs) across frames. Re-fetch them each time. Use them only on the game thread, inside the hook that received them.
- **Leave the game's ownership and ABI untouched.** Detours pass by-value arguments straight to the original, which still destroys them. If a detour skips the original, it must not leak what the original would have freed. Justify each skip in the card report.
- **No exceptions and no RTTI** (`/EHs-c- /GR-`). Build with `/W4 /WX`, including the linker. Every return value is checked and every failure is logged.
- **Hot paths:** no logging per frame, and logging per event is capped (log the first N, then one "further … not logged" line). Shared state is either written before the hook is enabled or touched only by the game thread. Anything else needs an explicit atomic and a comment saying why.
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
  sides and proves it with a client shot showing the HUD before the first objective step. Cards 241 and 242
  are re-run with the fixed start after 243; card 260's evidence is in doubt until then.
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
- **Objective steps run on live objective data (card 284, user rule 2026-11-13).** Each objective step starts with `host objective expect <id>`, places players with `host objective go`, and ends on `host objective wait <id>`; no hand-kept stand coordinates or actor names. Until card 284 is built, an objective soak result is not quoted as objective proof. A run in which the user played an instance is void for that side.
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
- **Check premises against this file.** A fix that rests on "the engine sends X" or "the client already does Y" names the line here or the disassembly that shows it. The card 132 round capture assumed cues reach the client by multicast; line 32 above already said pooled rounds and weapons do not replicate.
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

## Open queue (in order)

Playtest 2026-11-13 18:30 findings (fix first, one build with card 284): card 286 (the client's own grenade throw
shows two grenades: own launch plus the host's copy), card 288 (user rule: an entry door
closes only when every player is inside; C1M1 capture-the-flag door 0; step 1 needs the host log), card 287 (a rolling flare or other relayed body moves in 50 uu
steps on the client), card 285 (coded, reviewed, uncompiled), the client's transport dropship keeps
its flight tilt after the host skips the ride (rotation not in the dropship frame; traced, fix spec in the card),
card 289 (a secret the host takes is not credited on the client: only the `E` row is sent; step 1 static names the
credit function). card 290 (client sees "lost connection to host" at the host's level-end travel; client ends a level by its own script after a rejoin; part C, a 45 s host silence in C2M1, needs the host log), card 291 (dead bugs stand frozen up to 1616 ms: the client holds the ragdoll start for the host's stream row). card 292 (a dead flak bug keeps its satchel placement site on the client: the `S` row with value 0 does not neutralize the site). card 293 (host minimap shows enemy blips with no enemy: an NPC the game still counts as not dead; step 1 and a diagnostic first). card 294 (both players dead, host restarts: the client gets a second death screen, a respawn in the old map, a drop and a reconnect; one transition by the card 290 travel row). card 295 (client movement jitters after a revive: host and client each save and restore their own `MaxWalkSpeed`, 875 and 1524 seen; candidate cause, step 1 confirms). card 296 (nuke launcher does not fire for the client: four spool-ups, no fire ability after any; step 1 names the handover). card 297 (mech animations do not play for the client: mech controller reference null after the client's enter, no animation setup in the mod's enter path; candidate, step 1 confirms). card 298 (client cannot look down in the mech: attribute limits read -89.9 after re-init, lowest pitch in 27 mech shots -19.25; the real clamp is unnamed, step 1 finds it). card 299 (bug hole blown open with the mech still looks closed on the client, collision open: no world row and no digest entry for the hole cover; step 1 names the actor). card 300 (the tunnel doorway on FED_C2M1 collapsed with the client's trooper inside, near (144700, -37200, 10070); no world row names the closing actor; one-way closures must wait for every player by the card 288 test and reach the client as a world row; step 1 names the actor and its start condition). card 301 (user rule 2026-11-13: static audit of every objective on the seven Trooper maps for single-player assumptions, table in `docs/reference/objective-coop-audit.md`, gaps carded by class). card 302 (the host still hears some of the client's own non-spatial sounds; cards 099, 160, 173, 176 decide by context chain only; fix resolves the acting copy by the card 102 scope and hooks the unhooked sound entries; which sounds: user question open, host log after the playtest). card 303 (mount of mech or turret stutters about a second on the client: press to possession 915 ms at 64 ms ping, 737 ms with no log row before the possession frame, exit 804 ms; candidates client stall in the possession frame or late host possess; step 1 splits it). card 304 (client hears the host's EPulse44 from 500 m: 10 of 14 fire audio rows find no remote pawn and play at a world position with the first-person `ATT_WPN_Player_Fire`; 573 unresolved attached audio rows in the log; fix: attenuation decided on the sender, owner resolvable at any distance). Card 305 (playtest: Tango outpost on FED_C3M1 has no bugs; `docs/cards/305-tango-outpost-no-bugs.md`): the host announces the Tango stage (`Beach_SO5`) after a save load and a client rejoin and no wave spawns; candidates are a spawner left unarmed by the restore, a mod restart or join path, or host bugs not shown; also snapshot adds counted parents with the wrong kind, so `Beach_SO1_A/B/C` and `Resort_SO3_A/B/C` are missing on the client (7 digest mismatches). Second playtest report from the same cause: flak bug killed at 259105826, `Resort_SO3_B` completion found no objective on the client, count stays 0/3. Step 1 static, coded uncompiled, host log after the playtest. Card 306 (playtest: client fatal error; `docs/cards/306-client-crash-tac-support-swap.md`): **crash, first in the build after the playtest.** Client swapped White Phosphorus for a Sonic Blaster pickup at 259408308; the game drops the old support as a pickup, `c105_client_blocks` returns null, the game reads 0x5C of null at exe+0x37E14E8. Same class as the card 132 `SpawnWeaponPickup` crash. Fix: a refused client spawn runs and is made inert, never null; swap relayed to the host. Card 307 (playtest: client not spawned next to the host; `docs/cards/307-join-spawns-at-map-start-not-beside-host.md`): all three FED_C3M1 joins put the client at the PlayerStart, 20400 to 65200 uu from the host. `c46_spawn_spot` only moves the spawn when it fell back to the origin. Fix: every non-host player spawn goes beside an Up player. Card 305 part (a) closed by the user (Centurions were hiding); part (b) stays. Build b6 review (senior, 2026-11-13, `build-b5/dwmapi-b6-528d12b8fab569a7.dll`, not staged, no run yet): accepted 284, 285, 287, 289, 292, 305 (b), 307, 290 A/B; changes required before any run in 288 (the user saw the client's own presence copy start the close, shown 500 ms by card 270; the host hold alone does not fix it), 295 (negative saved speed written; client takes the host's speed from the Up frame), 294 (two confirmations), 286 (reads after the pool release), 306 (inert again after BeginPlay). 291 and 293 stay out of the build until their causes are settled. Causes settled from the host log: card 300 (one pawn closed the tunnel: host at (148672, -38721) when presences `C_78/79/81` shut at host 83752650, client 11k uu west) and card 304 (`g_pl.remote` 3000 ms expiry decides the attenuation); both released for coding. Card 303 is located (client stops processing its controller channel 0.74 to 0.97 s in six of six transitions; host not the delay) and gets a diagnostic only. Card 299: actor is `BP_DestructibleSpawner_Bugnest_HumanGameplay_C_5`; mechanism needs step 1b (disassembly). Card candidates from host WARN rows, trace first: journal full at host 83987850 (129 against cap 128), decal rows past 512 per tick. Senior static traces (2026-11-13, both logs, written into the cards). Settled and released for coding: card 296 host part (`c276_repress` re-presses a spool-up weapon after 100 ms; the release cancels the copy's spool and the new press fires at once: nuke 110 ms against the host's own 2.0 to 3.1 s windup; fix: no re-press while a `SpoolUp` spec is active, sibling every windup weapon) and card 302 pickup class (5 of 5 pickup sounds heard by the host are the client's pickups; `c160_copy_effect` ignores the card 102 acting copy; fix: fall back to `g_c102.acting` for 2D sounds, widgets and material parameters). Not settled, diagnostic or disassembly only: card 296 client part (why the client's own nuke never fires; presses bounded, rows needed), card 291 (wire is not the delay: host ragdoll notify 2350 ms after the death row against 723 ms on the client; step 1b reads the bug mesh tick options), card 297 (client mech `weak PC null` at +0xE30, written only by `EnterMech`; that animations read it is a candidate), card 298 (camera-set candidate refuted; client camera set read 0 at possession; step 1 disassembly plus view-limit row; sibling: two mech attribute sets get no client re-init), card 293 (diagnostic only). Hit markers in the host log are the host's own (0 of 55 tied to client fire only). Senior review of build b7 (2026-11-13, `build-b5/dwmapi-b7-4fe7e84dc3a69e26.dll`, every diff read in full: 288,
295, 286, 306, 294 doc, 302 G, 296 F, 304 B, 300 A, 303 C, 293 J, 287, 290, shared). Verdict: no blocking defect; the
launch gate (diffs of 288, 295, 294, 286, 306 read) is met; one combined run released. Attacked and held: 295 order
(`c295_host_walk` at `death.cpp:1511` before `c43_own_view` at `:1549`; second caller gets host 0 and falls to the
attribute); 288 reaction array restore after orig; 303 suspend window (no heap, lock or log between `SuspendThread`
and `ResumeThread`); 302 scope (site match only, so level-script and marker 2D effects inside an overlap scope stay
the host's). Open risks the run must show or clear:
- 288: a door reaction run deferred (a delay node) bypasses the array filter; the client-only WARN from `c161_own`
  would show it. Host `c288_hold` is time keyed (250 ms). Level-script doors are not covered.
- 306: a spawn is live between BeginPlay and `c306_step` in the same engine tick.
- 300: `kC300Closures` is a three-name list for `FED_C2M1_Scripting` only; the client runs the closure edge with its
  own pawn even when not past it. Not coded: placing a trapped player, the BP_DestroyableActor digest. Other maps
  need the card 301 audit before the list grows.
- 302: damage scope has no site, so kill and hit sounds get no fallback (matches the hit marker finding: host's own).
  The `ADAudioManager::PlaySound2D` card099 row lacks the acting text.
- 304: `WARN card304` fires on an unresolved owner even when the attenuation used is correct; read it with the
  tally row, not alone.
- 303: the watchdog is always on and suspends the game thread on frames over 200 ms. Diagnostic; it goes behind the
  test flag before a play build.
- 293: diagnostic only; `below-ground` is false positive under a roof (the trace starts 2000 uu above).

b7 combined run (2026-11-13, lane 2, `3abb7392d7e67d1d`): client crashed at C2M1, t=265073680,
`EXCEPTION_ACCESS_VIOLATION at ubwcoop+0x104500 ... reading 0x1B8`: card 306's `c50_send_use_name` passed a null
interact target (a press at nothing) to `actor_location`. The senior review missed it. Fixed at the helper
(`mod/src/ue/actor.cpp`, null actor gives `Vec3{}`); `object_name`, `class_name`, `object_index`, `is_a` already
guard null. Build b7b `build-b5/dwmapi-b7b-8704878f0f7556b8.dll` (hash checked; play DLL still `5e2ce00acad8efa3`).
Passed from logs: 288, 295, 286, 296, 304 (C1M1 tally `first-person attenuation used 0`; both rows `owner unresolved`).
Not shown: 306 (no map end), 302 (client downed before the ammo: no `clear` there), 300 and 294 (objective steps by
id did not match the map's state). One rerun on b7b released for these four, with objective-driven steps (card 284).

Senior verdicts on junior static work (2026-11-13, text in each card): 308 cause settled, cap 128 to 512 released,
Visibility-skip dropped, sibling is the card 098 world journal (`kC98WorldRows = 512` full at 83986119, 29 rows not
replayed), trace open; 309 cause settled, fix shape is a sent bit on the live decal row, sweep owed; 297 and 298 not
settled, diagnostic rows only. Card 300 closure cases unproven by test: a C2M1 step completed by command skips the
level script's own add and ends the chain (`logs/b7b-2-probe-host.log`); they go to the user's playtest. Run b7b-2
(`19701cf1398614b9`) covers 306, 302, 294, 304 extra and the press at nothing.

b7b-2 run (2026-11-13, `19701cf1398614b9`, last run of the b7 batch; logs `logs/b7b-2-{host,client}.log`): press at
nothing passes (no exception, `interactor target null (null) ... UseName not sent`). Card 294 failed with a settled
cause: the travel row is sent a tick after the blocking load and then waits in the pace FIFO behind 1868 frames,
while the line says "sent"; fix and sibling sweep in card 294. Card 304 fire sound passes (0 first-person of 23);
five non-weapon sounds flagged as player source after the reconnect are open, as is `owner unresolved` (card 304).
Not shown: 302 (client downed before the step), 306 swap (client held no support weapon), LOS_O01 by hand (state
bad after the failed restart). Senior static work the same day: card 098 world journal overflow traced (permanent
placed-actor rows fill it; cap 1024 plus per-kind counts, card 308), card 291 step 1b reviewed (two fields added).
Next batch is build b8: 294 fix, 308, 309, diagnostic rows for 291, 297, 298, card 284 tool follow-ups.

b8 review (2026-11-13, senior, static; DLL `build-b5/dwmapi-b8-5c1988ea9e92dd6c.dll`, all eight diffs read): released
for one combined run after three scenario changes (below). Code verdicts: 294 accepted (travel row sent from the load
hook before the load; `chan_send_client_now` skips the FIFO for V, Reload and Down only; queued frames are packed at
send time, so an out-of-order send cannot break the dictionary; priority sends skip the `kPaceRelHold` guard, accepted
because the three ops are a handful of frames per event). 309 accepted with one residual: M, T, E and R rows still
drop past 512 per tick; the run's `0 rows dropped` decides whether E needs the same sent-bit treatment. 308 accepted
(world journal 1024, objective journal 512, peaks and kind counts logged). 297, 298, 291, 306, 284, c126: diagnostic or
test-tool only, accepted. Junior static traces accepted: 294 (a) the client drop is the card 143 rejoin by design,
(b) the second death was bugs restored by the load, a scenario fault, (c) the card198 WARN is a read-back false alarm;
304 the five WARN rows are false (assets carry spatialized attenuation; at-location rows never name an owner), and the
player-source flag is too wide (any actor one Owner hop from the host pawn); 305 the 93 `objective missing` WARNs are
false (the host lacks the same objectives). Next code batch (b9, after the b8 run): 304 flag narrowed to pawn and
weapons plus honest tally; 305 host "existed" bit; card198 read-back names damage since the apply. Scenario changes
required before the run: (1) `host clear 6000 kill` directly after each `save engine load done` row and again after
the client's reconnect, before the wait steps; (2) the LOS_O01_SO01 step places both players by the objective verbs'
own stand, the hand coordinates only as a `try` fallback when the stand is none; (3) the nuke shot is aimed so the
blast cannot down either player, checked against the blast radius before launch; warp and continue sleeps become log
waits where a row exists.

b8 run 2 verdict and b9 review (2026-11-13, senior; logs `logs/b8-combined-host.log`, `-client.log`; b9
`build-b5/dwmapi-b9-fa30093eb1cec01b.dll`, hash checked, diff `b9.diff` read in full, accepted, not run). Budget: 2 of
3 runs used.
- 294: shown for the all-down case (the playtest case). Host `travel row sent ... sent now, ahead of 0 queued frames
  (in-place load)` at 270357896 and 270464308; `Reload ... sent now, ahead of 863` and `697 queued frames`; client
  `restart done in 8665 ms` and `7898 ms`, each `death screens built 1, respawns in the old world 0`. Not tested: host
  down while the client is alive (waits on the user's answer: pull the client or ask). No Down `sent now` row exists,
  so the Down priority send is unproven by log; b10 adds the row.
- 296: not a shown mod bug in this run. Client row: `spool 1702 ms, press -1249 ms and release 1700 ms`, ended by
  `fire input released inside the windup`; the scenario's 3000 ms hold ended inside a 2 to 3 s windup that began
  1249 ms after the press. Last run: one hold of 6000 ms or more, wait on the `card296: client ... spool-up` row.
- 302: attribution works (`acting copy BP_PlayerCharacterMI_C (overlap)` on `MS_ITEM_PickupAmmoHumanGenericMedium`),
  but the host stood about 1500 uu away; the far-host case is unproven. Last run: fixed warp, wait on the host log.
- 309: unproven. Host decal totals print only from `c50_arm_map`, never on an in-place load, and the run quit without
  a map end; no burst (`clear 20000 kill` killed 0). b10: host totals at the respawn-in-place reset.
- 308: C1M1 objective journal peak 40 of 512; world journal peak 3 of 1024, 0 dropped. Caps hold on C1M1; the
  whole-map soak decides the rest.
- 291: 57 rows, `mesh tick enabled 1, mesh visible 1` on all but one (`mesh visible 0`, guid 0xc6); significance
  never readable (`-`). Tick and visibility are refuted as the general cause; cause still open.
- 306: `inert spawns 0, null returned to game callers 0` on every LoadMap, both sides. 284/c126: stand path ran,
  SO01 completed by the level script.
- WARN `card209: host Down Up for pawn id N (slot -1) not sent: guid 0x0 not sendable` x4: explained, harmless. The
  in-place restart stands each entry up after the pawn was replaced (`restart: slot N ... -> Up (pawn replaced)`),
  `c43_broadcast` (`death.cpp:697`) looks the old pawn id up in the roster, finds no slot, guid 0. The client resets
  its own table at the same load (`card045: client ... new map: reset 043 (entries 2 ...)`), so the lost Up frame
  changes nothing. b10: no broadcast for a replaced pawn at the restart, plain row instead of the WARN.
- 297/298 not reached (scenario completed objectives that did not exist yet); dropped from the last run, 297 waits on
  the user's answer about the cockpit arms.
- b9 code: 304 (WARN only for an unspatialized sound with no attenuation; unresolved counted for guid and placed
  keys only; host row names owner and key kind), 305 (`kC49HostExistsBit` 1<<7 read before the original; client WARN
  only when the host held the objective, else `card305: ... absent on the host too`; flags 0x80 | 0x1F fits
  `kChanArgMax` 0x7FFF), card198 read-back (counts the game's Health changes on the applied pawn; WARN only when
  nothing changed). No defect found.
- Last run of the budget = b10 (b9 plus the 309 host totals, the card209 restart row, the Down `sent now` row), after
  the senior reads `b10.diff`. Predictions are in cards 294, 296, 302, 304, 305, 309.

b10 review (2026-11-13, senior, static; `build-b5/dwmapi-b10-5f25cb7b346c5527.dll`, hash checked, `b10.diff` read in
full): accepted, go given for the last budgeted run with `tools/scenarios/b10-last.txt`. Code: host decal totals also
at the respawn in place (`c132_decal_respawn_totals`, no reset); `c43_stand_all` sends Up only for a kept pawn and
counts replaced pawns in the restart row; `card294: host ... Down <state> ... sent now, ahead of N queued frames`
once per state change (cap 40 per map). Scenario deviations accepted: GOD=1 for the whole run (the driver cannot turn
god off; `down` and the all-down kill do not depend on it), host parked about 97000 uu off for 302, 296 hold 6500 ms
after the reload's AnimationEnded row. Not covered by this run: host down with the client alive, 297/298, a decal
burst past 512 rows.

b10 last run verdict (2026-11-13, senior; rows read in `logs/b10-last-host.log`, `logs/b10-last-client.log`; DLL
`5f25cb7b346c5527`; 135 s, FATAL 0, every wait passed). The 3-run budget of this batch is spent.
- 294 (all-down case): confirmed. Host `Down Downed/Out/Dead ... sent now, ahead of 0 queued frames` x4, travel row
  `sent now, ahead of 0 queued frames (in-place load)`, Reload `sent now, ahead of 857 queued frames`; client `second
  CompleteDeath skipped` and `restart done in 6528 ms: death screens built 1, respawns in the old world 0`. Open: host
  down with the client alive (user decision).
- 296: confirmed with a correct hold. Client `spool-up ... ended: AnimationEnded (spec inactive, spool 2019 ms ...);
  fire ability BP_WeaponFireAbility_NukeLauncher_C activated 1`; host held the re-press during the spool and the copy
  fired once (`FireProjectile` at 271399781, 2052 ms after the spool start). Row defect for the next build: a hold
  still down prints `release -271397729 ms`; print `held` instead.
- 302: confirmed with the host about 97000 uu away: 5 pickup sounds (`MS_ITEM_PickupAmmoHumanGenericMedium` x2,
  `SFX_Pickup_Negative` x3) `skipped ... acting copy BP_PlayerCharacterMI_C (overlap)`.
- 305: confirmed on C2M1: 0 `WARN card305` both sides, 28 `absent on the host too` rows.
- card209: 0 `not sendable` WARN; restart row `2 replaced pawns not sent`. card198 read-back `Health 100.0 (applied
  100.0); since the apply: 0 damage ..., life Up -> Up`.
- 309: host totals row now prints at the respawn in place: `0 rows dropped`, `most D rows in one tick 137`. A burst
  of more than 512 M, T, E or R rows in one tick did not occur; decided by the whole-map soak.
- 304: weak. Client tally `audio rows 2, owner unresolved 0`, no WARN; the playtest symptom (host fire heard far
  away) was not exercised. Stays open for the soak or the user's playtest.
- WARN diff against b8 run 2: host only `attach`; client `card152` bone check x13 (b8 x4; corpses guid 0xc6 and 0xa8,
  mesh component origin lags the simulated body) and `card140` hit drift x3 (b8 x5). No new kind. The card152 count
  rise is the same symptom as card 291's one `mesh visible 0` corpse (guid 0xc6 in both runs): trace together.
- Next: whole-map soak on b10 (rule "Soak first"), junior finishes the card 301 F/G columns first.

b11 review (2026-11-13): accepted, diagnostics only, not run. DLL `build-b5/dwmapi-b11-cf55155bc7a4dd94.dll` (hash verified), diff 138 lines read in full. Card 296: the spool row prints `release held` when no release followed the last press. Card 301: `c301_net_audit` prints one `card301: <side> net audit <class>` row per component class (faction +0x6B0, squad leader +0x868, TacticalSupportComponent) at possession, with bReplicates and the CPF_Net property list; the raw offsets are safe because `card017_step` passes only pawns that are `is_a(g_player_mi_class())`. The rows appear only inside the card 17 window after LoadMap. Card 291: the card152 bone line gains the mesh component scale. Predictions for the soak: scale off 1.000 only on corpses with a constant bone gap; class F and G cards are decided by the net audit rows. Next gate: soak scenario audit plus written predictions, read by the senior before the go.

b11 soak C1M1, discovery run 1 of 3 (2026-11-13): go given. Scenario `tools/scenarios/b11-soak-c1m1.txt` (295 lines, voidsoft, DLL and map guards, all 25 C1M1 step ids, 294 host-down block with the client alive, travel to FED_C2M1) read in full by the senior; predictions are in its head. Accepted for this run only, owed for the counting run 2: the client acts in one step only (run 2 swaps roles, client acts), hard-coded actor names and two look points (run 2 reads them live or from run 1 rows), ammunition not counted for the range, `host travel next` instead of the level-end button path, GOD=1. 309 is not provable on C1M1 (few bugs). C2M1 rewrite to card 284 shape runs in parallel, for review, not launch.

b11 soak C1M1 run 1 verdict (2026-11-13, 1714 s, FATAL 0, logs `logs/b11-soak-c1m1-{host,client}.log`; rows checked by the senior). Run 1 of 3 spent. Confirmed: 294 host down with the client alive (client `restart done in 4777 ms: death screens built 0, respawns in the old world 0`); 304 tally `audio rows 156, owner unresolved 0`; 308 `objective journal peak 234 of 512 frames, 0 not recorded` (growth is SetObjectiveVisibility toggles of the three `_SO0` rows, 82 each; card 066 compaction bounds it). Settled: card 301 net audit, both sides, ADFactionComponent, STSquadLeaderComponent, STTacticalSupportComponent all `bReplicates 0` with only `ActorComponent.bReplicates` and `ActorComponent.bIsActive` as CPF_Net: nothing the level script writes into them replicates, so class F and G need a host apply to every player pawn plus a mod relay where the client reads the value (senior traces, not coded yet). Not proven: the objective chain (root void: `c284_stand` kind Use found no nav point inside use range at Assault_SO4, every later step dead); 309 (no gore on C1M1, respawn totals row only in the stale branch); 291 (bone 0 is 0.0 to 0.3 uu from body 0 on all three corpses, scale off 1 on all three, no control; open question is host scale against client scale). New client WARN `card132: client digest round 48 cut by the map change` at the checkpoint load, under review. b12 (tooling and diagnostics): multi-direction Use stand, actions skipped after a failed gated look, shot name length check, host scale in card291, 309 totals rows on every in-place load. Run 2 scenario: explain the `_SO0` rows, client acts and host follows, level end by the button path, both-down case added.

Card 310 (2026-11-14, senior trace, cause settled): audit class F. Level scripts write per-player state on player 0
only (weapons, loose tags, tactical support default class and chargeable, faction, squad leader, item removal);
nothing replicates (b11 soak rows). Card 267 already mirrors weapon calls from the host's pawn to copies; 310 makes
that one manager for every such native, in both directions, with a client row where the client reads the value, plus
the client's tactical support across a host checkpoint load. Queue: after the b12 batch. Proof: one C1M1 run with
the client acting. Card: `docs/cards/310-level-script-per-player-writes.md`.

b12 (2026-11-14, senior review): `build-b5/dwmapi-b12-3b02e3e35c099b5a.dll` accepted for C1M1 soak run 2 of 3
(`tools/scenarios/b12-soak-c1m1.txt`, client acts, host follows). Diff read: card 284 use-stand search (7 tries, row
per try), drive.py look-gate skip of action steps and shot-name check, card 291 scale in the host row, card 309
respawn totals on every host in-place load and a host map row at 0 spawns. Run 1 open Assault_SO0 was a consequence
of Assault_SO4 (panel E), not its own fault; FED_C1M1 has 5 parents and 22 sub-objectives. Known before the run:
client-triggered script weapon and tac writes land on the copy only (card 310), so a missing `card267` row there is
not a new fault. C2M1 scenario reviewed; six fixes sent to the junior (mech leave step, satchel building target,
inventory after the checkpoint load, client card050 gives count, final sleep, DLL hash).

C1M1 soak run 2 of 3 (b12, 2026-11-14, senior verdict): void as a soak. Root: test tool, not the mod. The card 284
use-stand cap (150 uu) rejected every navmesh point at `BP_PanelSwitch_WithoutScreen_C` (nearest 189 uu; the real
press in `logs/c224-c1m1-b4d-host.log` was 204 uu from the pawn), so Assault_SO4 stayed open and every later section
voided. Second failure at this step: method changed for b13, the nearest navmesh try wins with no cap and the
client's `focus=` look gate (the game's own focus) decides whether E is pressed. Proven in run 2: card 294 case A
(host down, client alive: death screens built 0) and case B (both down: built 1, respawns in the old world 0), look
gate skips (16, each after a voided look), 309 respawn row on both in-place loads. Not reached: 308, 098, 132 map
totals, 291, level-end UI path, 309 burst, card 310 baseline. One mod fault found: the client keeps a pending card
070 names frame across a reconnect (`WARN card070 ... names frame for ShootingRange_SO3 ... dropped`, harmless here);
b13 clears it at the client map end. Run 3 is the last of the budget.

- **C1M1 soak run 3 (b13 `2208026de2fee4a3`): void at Assault_SO4 again; soak budget spent; method changed (b14, senior 2026-11-14).**
  Rows: host `c284: use stand ... winner yaw 189 uu (nearest)` (prediction held); client placed on ring slot 90 at
  (17399, 12832), look at the bounds centre, `aim: hit BP_PanelSwitch_WithoutScreen_C_0 ... 127 uu`, `focus none`; E
  skipped by the gate; map cascade-voided. Cause, static plus every past switch look in `logs/`:
  - `TraceForInteractable` (exe+0x4A20780, disassembled) is one line trace from the camera component (+0x108) along its
    forward for the range at +0xF8, on the interactor's own trace type (byte +0xFD), and the first hit actor must
    implement the interactable interface. No cone, no proximity test. It is not the Visibility trace the aim row uses
    (run 3 cmd 10: Visibility hit a StaticMeshActor while focus was the Morita pickup).
  - The switch's interact collider is smaller than its mesh and bounds. Same eye (17332, 12823, 294): pitch -11.8 (hit
    z 267) focus 2 of 2; pitch -23.x (z 242) focus 1 of 3; pitch -33 (z 241 front edge) 0 of 2; side slot at the bounds
    centre 0 of 1. A press 150 ms after a `focus none` look found a null target too, so it is not tick timing.
  - So bounds and origin offsets cannot aim a use press, and a ring slot is the wrong place for the presser.
  Method (b14, tool only): (1) kind use: the first client takes the stand point, the host the ring slot. (2) `look
  ... focus=` scans: the role sweeps a grid of aim points over the actor's bounds with its real camera, reads its own
  interactor focus (+0x100) after each, and settles on the centre of the points that give the wanted focus; row names
  hits, the chosen point, the interactor range and trace type. No hit: VOID, key skipped as now. (3) Proof is a short
  probe scenario to Assault_SO4 only, then the full soak. No coordinates in scenarios. Applies to the C2M1 satchel
  sites unchanged.
b14 review and card 310 step (a) (senior 2026-11-14): b14 diff (`17ac07260788d9b3`) accepted as written: focus
scan 26 points, read 2 runner ticks after each snap look (`c63_step` runs once per frame from `ubw_post_tick`),
centroid re-checked, first hit as fallback, 10 s cap, `seen:` row on zero hits; the `drive.py` pass-through of
`focus=` is needed and accepted. Probe `tools/scenarios/b14-probe-so4.txt` read, every state step waits on a log
line; go given for one run, and for the C1M1 soak on b14 only when every probe prediction holds. Card 310 (a)
accepted with the senior's wider sweep (card 310 "Step (a) result"); go for (b) code, no build while a game runs,
no run before the senior reads the diff.

b14 probe passed (senior checked both logs, 2026-11-14): `logs/b14-probe-so4-*`, 37.3 s, gate skips 0. Client on
the stand point (212 uu from target), host on slot 90; scan `hits 1 of 26, chosen 17309 12746 295 yaw -89.0 pitch
0.6, range 250 trace type 3, by centroid`; focus `BP_PanelSwitch_WithoutScreen_C_0`; E completed Assault_SO4 on the
host 13 ms later, then SO0 and Assault_O1. Interactor range is 250 uu, trace type 3; the switch's interact collider
sits at the top of the bounds (aim hit at 17312 12683 296). Open: 1 hit of 26 is a thin margin; if a soak site
gives 0 hits, add a finer second pass around the bounds top before any other change. C1M1 soak on b14 started
01:11. Card 310 `AddAmmunition`: card 071 already relays a copy's gain to its client, so the row is a host-side
mirror onto every other player pawn, no new client row (accepted).

C1M1 soak run 4 (b14 `17ac07260788d9b3`, 1932 s): tool fix held, map not finished; one mod bug carded (senior
2026-11-14). `logs/b14-soak-c1m1-*`.
- Assault_SO4 passed (card 284 focus scan). Stall at ShootingRange_SO3 0/10: every client look hit the host pawn
  about 60 uu in front of it (`objective go all` put both players on the marker), 13 gate skips. CTF, Asteroid and
  level end never ran.
- Scenario faults the junior's report left out: lines 114-145 `expect TacSupport_SO2/SO3/SO41/SO5/SO6` all VOID
  ("not under the active parent; doable: none"), line 167 `expect ShootingRange_SO1` VOID (already complete), line
  180 waits for an `AddCountSubObjective` row the host logged at 279104882, before the wait began.
- Mod bug, card 311: range target `Activate` (card 065) is a live-only relay, so a client that reloads the world
  on a checkpoint load keeps the target at rest; 26 `card132: mismatch` rows, rounds 40 on.
- Proven this run: 294 case A and B, 304 (0 unresolved), 070. Not exercised: 291, 308, 098 map end, 309 burst.
- Uncarded: host `WARN card141: copy weapon BP_Weapon_MoritaMk1_C underslung 0 (want 1) 600 ms after the Switch`,
  once.
- Method (fourth soak failure, scenario side): no further C1M1 soak until the junior sends a line-by-line audit
  of run 4's drive log (every FAIL, VOID and skip: cause and scenario change). Fire steps move only the shooter
  (`objective go client`); the other player stays out of the line. Next run is the b15 batch (310 + 311) with the
  soak folded in, not a soak alone.

Card 310 (b) diff review (senior 2026-11-14, `c310.diff` 1130 lines read whole; b15 `900aeda118d0b1a2` not to be
run): shape accepted, three changes before the rebuild with 311.
- Stale journal: `c50_arm_map` runs on real map loads only, so the host's card 310 journal survives an in-place
  checkpoint load while the save rolls the script back; a copy's new pawn would get a tag (`TutorialBlock`) the
  host no longer has. A new copy pawn takes its values from the host's live own pawn; journal only as fallback.
- `c310_apply_journal_to_copy`: a failed send is neither counted in `not_sent` nor logged.
- `c310_receive`: a failed apply still records the pawn id, so the kept row is never retried.
- Attacked and held: re-entry (kind cleared in every mirror, restored after), nested exec frames, client session
  guard, non-player owners (`c267_source`), item removal recursion through `detour_c73_remove`, tag text round
  trip (`c71_find_tag` is a plain FName find), ledger row count.
- Open by design: checkpoint-load tactical support keep (card "Done when") still owed; the default class compare
  cannot tell a level script write from another writer, proven or refuted by the run (pickup at the bombing run).

Card 312 (from the b14 soak's uncarded `WARN card141 ... underslung 0 (want 1)`): the client's Morita is in slot 1,
its copy's in slot 0, so the replayed `SendWeaponSwitchRequest(1, 0)` takes the slot-switch branch on the copy and
the underslung toggle is lost. Cards 141 and 057 relay machine-local slot indexes; card 050 compares the packed
list, not slots. Fix: slots in the card 050 text and apply, 141 replay uses the copy's equipped index, 057 Equip
resolves by class. Card `docs/cards/312-weapon-slot-index-differs-client-vs-copy.md`; goes into the 310 + 311 build.

- b15 review (senior, 2026-11-14), DLL `39818b2ae308abbf` (310 + 311 + 312), not launched. Diffs accepted. Four
  required changes before a run: (1) `c50_slot_text`, `c50_slot_of`, `c57_copy_slot` read the definition class
  without the `is_a` weapon guard, one guarded helper, rebuild; (2) `tools/scenarios/c310-proof-c1m1.txt` has no
  checkpoint load, so card 311 and the card 310 load check cannot be proven, add one after `ShootingRange_SO3`;
  (3) Asteroid fallback warps by actor, not by coordinates; (4) drive-log audit table of
  `logs/b14-soak-c1m1.drive` in the card 310 file. Junior sends one packet back; run waits on the senior's go.

- b15 rebuilt as `2fb0cc887419993d` (senior, 2026-11-14): the four required changes read and accepted (guard helper
  `c50_weapon` at six sites; checkpoint load after `ShootingRange_SO3`; Asteroid fallback by actor; drive-log audit
  in the card 310 file). Go given for one run of `tools/scenarios/c310-proof-c1m1.txt`; `39818b2ae308abbf` superseded,
  never run.

- b15 run 1 (`2fb0cc887419993d`, 2026-11-14): client crash 198 s in at the TacSupport_SO2 Morita pickup, executing
  0x0 in `c50_move` (card 312). Cause: `g_orig_give_by_instance` is the trampoline of a hook gated on `g_gate_diag`
  (`hooks_ubw.cpp:2002`), null in a non-diag run. Senior sweep of all 106 gated hook rows: one sibling,
  `ue::net::g_orig_listen` called from `c154_listen_here` (`lobby_ui.cpp:1204`). Also found: card 310 join rows
  dropped before the first pong (WARN, not queued); `drive.py` `{gN}` cannot carry a name; `Assault_SO3` completes by
  level script before the expect. b16 scope approved: resolved pointers for both calls, startup null-pointer check,
  join rows resent on the first pong (card 139 path), `drive.py` raw group text, `Assault_SO3` wait. 310, 311 and
  the 312 predictions not reached; run waits on the senior's go.

- b16 `4443e6922875cf3a` (2026-11-14): diffs read and accepted by the senior; go given for one run of
  `tools/scenarios/c310-proof-c1m1.txt`. Play build 4 ran with diag off: the hangar H listen (`c154_listen_here`)
  would have crashed it; no playtest log has a `card154: H:` row. The next play build must be b16 or later.

- b16 C1M1 proof run (2026-11-14, DLL `4443e6922875cf3a`, 2385 s, no crash; host log `UBWCoop-host-20261114-024347.log`).
  Held: no null-pointer WARN, card 312 slot apply, card 310 deferral, card 311 replay rows. Five failures, traced
  statically by the senior; four are scenario faults, one is a mod bug:
  - TacSupport_SO3 not completed: scenario fault. SO3 is "shoot the three warrior targets" (`TacSuppRangeTarget_C_1`,
    `_C_0`, `_C_3`, level script @26482); the scenario pressed key Four. The b14 pass was accidental (the copy was
    never set unchargeable and held 20000). Card 310's chargeable mirror matches the script. SO4 follows from SO3.
  - ShootingRange 0/10: scenario fault. Line 198 `host objective go host` ran after SO2 completed and put the host on
    the SO3 stand point, in the client's line of fire; the look gate voided and no shot was fired.
  - card141 underslung row missing: scenario fault. Card 312 put the client's Morita in slot 0, so key Two switched to
    an empty slot. The 312 underslung prediction is still unproven.
  - card 310 "kept rows" row missing: prediction fault. The journal is cleared at map load; the join rows are applied
    directly (`C 0 applied`, `D ...Config_TacSupp_Resupply_C applied`).
  - Range targets `_C_1`, `_C_2` mismatch in rounds 28 to 35 after the load: mod bug, card 313 (replayed timeline
    state is 228 ms stale, looping movers run behind the host).
  Open note, not carded: `SetTacSupportChargeable` is written by the tac area enter and leave events of player 0 and
  mirrored to all pawns, so one player leaving the area turns charging off for everyone.
  Batch run budget: 2 of 3 used (b15 crash, b16).
- b17 (DLL `2df06e3cf7129696`): card 313 code (replay bracket 'R' row carries the host sample time; the client ages
  a playing timeline's position by it and the card 155 clock offset) plus the four scenario fixes. Senior read both
  diffs 2026-11-14; run 3 of 3 of this batch is with the junior. Result not in yet.
- C1M1 scenario shortcut audit (junior, 2026-11-14), to card after the b17 run: presence gotos and pickups by fixed
  name, SO3 warrior target names hand-kept (card 284 rule), SO41 `goto actor BP_ResupplyRack_Level0_C` ("no such
  actor" in b16), constant slot keys (SO6 key Five, post-load key Four), `host travel save` / `checkpoint` console
  commands instead of the game's checkpoint path, Asteroid `host warp` fallbacks (lines 343, 346), GOD=1.
- Card 313 sibling sweep, not aged at join (to card after 313 is proven): card 136 looping FX restart at phase 0,
  card 161 door montage replays from its start, card 159 dropship frames lead by wire time only, card 132 decal age
  misses the queue delay; pose_c132 replays not inspected.
- User answers 2026-11-14 (playtest questions), each to be traced and carded by the senior:
  - Card 297 mech: the client pilot saw no cockpit arms at all. Settles candidate 1; fix released in the card
    (2026-11-14 verdict), not assigned yet.
  - Host restart while another player is up: allow it, no prompt, no block. Already the coded behaviour (card 294
    note); pause-menu restart with both alive proven in `c376b60` phase A and `c376b63` phase D.
  - Host quits to the main menu: the client follows to the frontend. Already the coded behaviour (card 290 `quit`
    row); one soak proof owed.
  - CTF AIs: frozen in place (no movement at all). Carded: 318.
  - Sounds: the host heard the client's pulse rifle fire. User rule: sweep every sound class; neither player should
    hear the other player's own-player sounds (weapon, voice, UI, movement) as their own. Class audit, not one fix.
  - Phantom radar blips: seen by the host; place unknown.
  - Revive jitter: the downed trooper. Fits card 295 (coded b6/b7); noted in the card.
  - "Dead bugs show late" question withdrawn: the user did not recognise the symptom.
Card 314 (2026-11-14, senior trace, not coded): the host hears the client's weapon fire "like my own gun". Cause
settled from the playtest host log: `card162: host fallback attenuation ... ATT_WPN_MoritaMK1 failed (... game instance
missing)` at 81350012; `SpawnSoundAttached` calls carry no context, the failure latches for the process, and every
later fire sound of the copy (shotgun 89 rows, EPulse88 72, underslung 17, turret 7, laser 5, nuke 4) plays with its
own unspatialized `ATT_WPN_Player_Fire`. Depends on which fire sound comes first in the session, so the soaks passed.
Fix: game instance from the attach component or local controller, no latch on that cause; class rule in the
PlayInternal detour for any other player's component with an unspatialized attenuation, plus a per-map tally. Client
side of the playtest symptom is card 304 (fixed b7). Open for the user after the fix: remote fire uses the
first-person asset with a 30000 uu falloff; the game's AI variants are shorter.

Run 3 of 3, b17 `2df06e3cf7129696`, C1M1 (2026-11-14 03:54 to 04:23, senior verdict): void from scenario line 195.
The client's bullets at `TacSuppRangeTarget_C_3` hit a rise in the ground 350 to 550 uu ahead of the muzzle (53
`miss Landscape` claims) while the camera aim check passed; `TacSupport_SO3` stayed open and every later step failed
in a chain (card 316, driver). Not proven in this run: 310 map totals, 312, SO3, SO4, range 10/10. Card 313: age row
holds (`4.24 + 343 ms = 4.58 of 6.00`), mismatches do not (38 rows, client about 1.1 s behind, 244 uu); supported
cause is a 1.06 s client frame stall right after the state row with the engine's frame delta cap (card 315, step 1
proves it). Batch budget spent; the next batch is 314 + 315 step 1 + 316, one build, one run.

- **Card 317 (carded 2026-11-14, not assigned):** a thrown flare's client copy launches itself along +x and the host
  sends no row while the flare flies; the copy is 1609 uu off until a digest heal or the first rest row (run 3 round
  7 mismatch, playtest "flare lags"). Step 1 is a one-row diagnostic, then the fix and sibling sweep. Goes to the
  junior with the b18 run verdict. `docs/cards/317-thrown-flare-copy-flies-its-own-way-until-the-host-rests.md`.

Card 293 addendum (2026-11-14, senior trace): minimap tick read in full (0x0521E460). Markers drop one tick after
`ShouldShowOnMinimap` turns false; the scan adds from a registry without asking it, so a dead NPC still registered may
be re-added every scan (blink). Existing rows cannot show dwell. Step 2 diagnostic specified in the card (dwell,
`re-added`, `no-guid`), rides the next build with card 317 step 1; no fix before its rows. Side findings: client
copies with health at or below 0 and `IsDead` 0 keep their marker (guids 428, 640, b10); one live host warrior bug
with guid 0. Not assigned yet: goes to the junior with the b18 run verdict.
- **Card 318, revived NPC copy stays Dead on the client (user: CTF AIs "frozen in place").** Cause settled from the
  playtest logs and the exe: `c131_client_revive` sets Aware only from Dying, but the copy is already Dead at the
  revive (190 of 191 client revives across C1M1, C2M1, C3M1 read `base state 4 -> 4`), and the client runs none of
  the host handler's other revive steps (`BP_OnRevived`, RVA 0x05193030 steps 3 to 6). Hits every reviving trooper
  class, not only the laser ones. Card has the fix, the sibling sweep and the CTF scenario gap (no scenario spawns
  the laser troopers). Not assigned yet: goes to the junior with the b18 run verdict.

- **Card 319, client minimap shows no bug blips (user, playtest client: "never saw the bug blips").** Path settled
  from the exe: blips come from a HUD scan, not `ShowOnMinimap`. The minimap tick asks
  `ASTHUDGameplay::GatherMinimapMarkers` (0x051FC5D0) for a sphere overlap around PC+0x2E8 (the pawn) with the
  HUD's object types, cached and refreshed by a timer. The failing client gate is not settled (test client drew
  blips on 2026-11-07; playtest origin was set). Step 1 row `card319:` rides the run 3 build and names the gate
  (HUD class, PC pawn, range, object types, gathered vs live NPCs in range, collision of missed copies). No fix
  before the row is read. Second run of run 2 (b18) is void: `objective go` put the client 10762 uu from the SO3
  targets, beyond the 10000 uu aim trace; scenario fixed before run 3 (last of 3).

Sync audit second pass 2026-11-14 (`docs/SYNC_AUDIT.md`, "Second pass: leaks"): cards 321 (host tick code asks
for player 0: level streaming, spawn sight, door path; step 1 is a senior trace), 322 (radio, subtitles, music,
time dilation run per process; diagnostic first), 323 (join replay gaps, digest blind spots; after 320). All
behind run 3. 320 (world starts when both players confirmed the title screen) is traced and written.

b18 run 3 review (senior, 2026-11-14): build `0c86ba1c6c2d79b0` diff and scenario read in full; released, last run
of the budget. Checked against the exe: host revive order in `HandleHealthValueChangedEvent` (0x05193030) is
SetCurrentBaseState, StopStateMachine_Dying, StartStateMachine_Core, BP_OnRevived, vfunc +0xA78, as card 318 mirrors;
StopStateMachine_Dying null-checks +0xA20; StartStateMachine_Core creates +0xA10 when null, so the client guard on a
non-null core machine is required. Predictions beyond the cards' own: 319 host rows `gameplay 1`, `pc pawn same`,
gathered = live; 317 host flare spawn `simulating 0` with a projectile movement component; 318 rows only if a copy
revives (a pooled bug reused), each without WARN; 297 no rows (C1M1 has no mech), zero `WARN card297`; 293 step 2
dwell rows on the host, `no-guid` count names the guid 0 NPC. Not proven by this run: 318 trooper revive, 297 arms
(both need the user's playtest or the C2M1 soak).

Card 320 written (senior trace, 2026-11-14): the title screen does not hold the world. Card 096/120 lift the host's
screen pause at the client's first Pong, and the client's screen comes only after the host's continue, so each
player reads its screen over a running world. Design: hold for both until every connected player confirms
(`docs/cards/320-world-held-until-every-player-confirms-the-title-screen.md`). Step 1 (senior, static: what a paused
listen server freezes on the client) is open; no code before it. Queue: behind run 3, with 323 after it.

Card 321 step 1 (senior, 2026-11-14): C1M1 streams only in the first 9 s after load (16 subsystem unpause rows,
none in play), so it cannot prove the streaming rule; a begin-play row naming volume count per map is added to the
card's diagnostic. Door crush part 2 reads player 0 only: a client's trooper is never damaged or crushed by a door.
The other L1 candidates are traced in the card. Practices from other games: `docs/SYNC_AUDIT.md`, section "Practices from other games" (state-not-events rule for card 323, digest repair proposal).

b18 run 3 (build `0c86ba1c6c2d79b0`, senior-verified 2026-11-14): ended at 446 s, both rc 0, digest 0 mismatches,
0 FATAL/ERROR. Held: 317 (host flare `simulating 0`, ProjectileMovementComponent velocity (-625, -3438, -194); the
copy flies its own way at (3500, 0, 0) yaw 0), 319 on the test client (gathered 179 of 181; does not reproduce the
playtest), 297 (0 rows), 310 (10 host, 9 client rows), 314 (5 rows), SO4 with the new ready regex, 293 step 2 (one
no-guid marker: a live friendly `BP_AI_Trooper_Medic_C`, 33417 ms). Not exercised: 318, 315, 313, 312 (0 rows).
Failed: range 0/10. Every client look gate on a range target voided, so no shot was fired; the range works in play
(`logs/playtest-host-20261107-1416-live.log`, counts 1 to 4 of 10), so this is the scenario: the gate traces
Visibility (look 39: target up, 1541 uu, "no hit within 10000 uu") and later looks ran while the target was down.
Third range failure: the method changes (gate on the weapon's channel or on a clear line to the bounds centre, wait
for the client `card140` row that places the target up, fire in that window, and stop the section at the first
target that cannot be shot). User saw in this run: the client's held flare invisible in the hand (card 324).
Next batch: 317 step 2, 324 step 1, 319 played path, range scenario, then 321, 322, 323, 320.

325 (senior audit, class 9): the effect journal is reset at map arm, after the load, so placed effects activated in BeginPlay never reach a joiner (played log: journal 199 at arm, replay 23). Fix: reset at map end only. Same build: rows naming the client's 153 target misses of 911. Sibling sweep of every arm-time reset. `docs/cards/325-effect-journal-wiped-after-map-load.md`.

- **C1M1 soak scenario shortcut audit (senior, 2026-11-14, `tools/scenarios/b14-soak-c1m1.txt`; apply to the next soak scenario before its run).** (1) `voidsoft` and `failsoft` are still at the head: a run with them is a discovery run, the clean soak drops both. (2) The DLL guard names an old hash; it is regenerated per build. (3) The host makes no real input in 390 lines (0 `host key`, 0 `host look`): every press, shot and throw is the client's, so nothing proves a host action reaching the client. Alternate the acting role per objective. (4) Downs are `host down <role>` and the restart is `host travel checkpoint`: the pause-menu quit and restart proofs stay owed as real UI steps. (5) The level-end button and the Enter key each have a six-try ladder; the report states how many tries were used, and more than one is a finding. (6) One `sleep 2` before `host quit`: accepted.

- **Cards 326 and 327, before the playtest (user decision 2026-11-14).** 326: one registry (`sync_registry.cpp`) owns map end, map arm, join replay, in-place load and quit for every state mechanism, with runtime WARN rows for a table that loses rows at map arm or holds rows with no join replay. 327: a per-player current-value row (life state, seat, inventory hash) that the join replay and the digest both read (`P:<slot>`, `PW:<slot>`). Both are batch 2 with card 323's remaining gaps and card 320's hold: step 1 tables from the junior first, senior review, then one build and one run. Batch 1 (cards 317, 319, 321, 324, 325) runs first, unchanged.

- **Batch 1 review (senior, 2026-11-14): changes required before the build.** (1) Card 319 scenario waits match `live npc in range [1-9]`, which friendly troopers satisfy (run 3: 181 in range, no bugs): the row and tally need a hostile count and the waits must use it. (2) `host down client` goes through `c43_hit` (`death.cpp:1920`), whose hits the test god puts back (`c132_god_block`, `death.cpp:397`; only `in_kill` is exempt), so steps B and D give up under `GOD=1`: the test's own hit must land. (3) Both `ui continue` steps need a wait on `card135: <side> ... screen prompt ready`. (4) Card 325 client niagara row cap of 4 frames can lose the final count. (5) `c310-proof` DLL guards name the old build. Signatures of the card 322 and door detours match `tools/symbols.txt`. Batch 2 step 1 tables accepted with one condition: a clear moved from map arm to map end needs proof per table that old-world teardown inside LoadMap writes no row, or a gate.
- **Card 328 (senior audit 2026-11-14): NPC copy z clamp repeats on one spot.** Played client log: 508 set-backs in one map, up to 14 on one standing bug 0.5 s apart, 205 to 216 uu, velocity 0 (`c132_drift_clamp`, `enemies.cpp:2264`). The clamp fights the client floor. Cause of the z gap not settled: step 1 rows (xy gap, floor z both sides, host mode, update age) ride the batch 2 build. `docs/cards/328-npc-copy-z-clamp-repeats.md`.
- Card 329 (senior, 2026-11-14, user: "We should look into the heaviest packets"): playtest host peak 96941 B/s of 250000, `not net ready` 1175 of 280173 frames. Mod channel is 53.1% of replicated bytes; Fx 40.3% of it by row count (279565 rows on one map), Digest 9.4% sent every 30 s unchanged, Pose 11.1% (4 of 5 snaps are resnaps). Step A (Digest sums only on a difference) and step B (Fx per-asset and per-distance row) ride a batch build after the playtest blockers; no Fx cut before step B's row. `docs/cards/329-heaviest-network-traffic.md`.

- **330 Per-frame cost of the mod** (efficiency audit, user 2026-11-14; `docs/cards/330-per-frame-cost-audit.md`). Settled: the play build times nothing (card 252 rows need `UBWCOOP_C132_FX`; 0 `mod ms:` rows in the playtest and c319-played logs); 58 steps per tick, 31 `c23_collect` call sites with no shared scan; playtest host log 115763 lines, 0.41 per frame. Step 1 (diagnostic, always on): per-step time, collect counts, log time, per 30 s and per map. Rides a batch build after the playtest blockers. No fix before the rows.

- **331 Host crash on checkpoint reload: effect watch reads a freed component** (play blocker, in play build 4; `docs/cards/331-host-crash-fx-watch-stale-pointer.md`). Batch 1 run 1: `CRASH 1/4 ... ubwcoop+0x116335` = `object_index` on a raw component pointer in `fx_watch_step` after the reload freed the world. Fix (weak pointer) is in the junior's tree; owed: sibling sweep of every stored UObject pointer, then the reload step again with no CRASH row.

- **Batch 1 run 3 void, batch 2 build plan** (2026-11-14). Run 3 (c319-played, b18r5b `e94555c8fd4ecc76`) void at line 52: harness faults only, no prediction tested. User decision: the harness takes focus (`run_pair.sh` activates the game window with xdotool when a test waits on input; real key presses stay). One batch 2 build carries: batch 2 items, r5b fixes, card 052 `play`/`join` modes, card 331 weak-pointer conversions, card 330 step 1, card 329 steps A and B. Budget 3 runs: c319-played (crash proof, D rows) and the batch 2 proof. No build before the senior reads the full diff and the scenario audit.

- **Batch 2 split (senior decision 2026-11-14, after the junior's report).** Build now: card 331 conversions, card
  330 finding 3 log gate (`grenades.cpp:1287`, `:1316`), card 052 `play`/`join` modes, run_pair MENU focus and client
  gate (gate failure must end the run), c310 map end / arm split with teardown count, `death.cpp` scan order,
  c310-proof line 137, drive.py `must` and `line=`. Reviewed against b18v1, no source defect. Runs: 1 c319-played
  (crash proof, D rows), 2 c310-proof (range), 3 reserve. Batch 3 (not written yet): cards 326, 327, 323 gaps, 320
  hold, 103 ring wrap, PickupGone, 328 step 1 rows, P-row ammo and charge wiring, 330 step 1, 329 steps A and B.

- **b19 run 1 (c319-played-c1m1, 2026-11-14): card 331 proven, card 319 does not reproduce.** No crash through the
  checkpoint reload. Card 319 D: first client sample after the respawn t=303958885 `pc pawn same`, hostile gathered =
  hostile; client tallies `hostile 5: gathered 5` and `hostile 12: gathered 12`, 0 samples with pc pawn not same. A
  and B held; C void (no spawns at the objective step). No more c319 runs: the playtest fault is not the played path,
  the death or the reload; decide statically what else differed in the playtest (long session, map travel, mech).
  Also proven: 0 `card074 ... FX` rows; host stays on FED_C1M1 in `play` mode; client gate 33 s. Untested: the
  focus activation (no hold happened). New, shutdown only: at quit the host sends Objective frames to its own PC
  (`seq gap 14-1509`, `no handler`, t=303974287): the send picks a local PC once the client copy is gone; card owed.

- **Card 332 (carded 2026-11-14, batch 3):** the host mirrors level-script calls made during world teardown and
  receives them on its own PC (`seq gap 14-1509`, 10 `no handler` rows at quit, b19 run 1). Fix class: one teardown
  state refuses host sends; sibling sweep over every EndPlay-driven sender. `docs/cards/332-host-sends-during-world-teardown.md`.
- **Card 319 (decided 2026-11-14):** b19 run 1 does not reproduce; play build 5 carries the always-on `card319`
  rows so the next playtest names the gate. Mech-seat sample pair owed in the C2M1 soak. Not a play blocker.

- **b19 run 2 (c310-proof-c1m1, 2026-11-14, senior-verified):** ended by `must` at line 326, 0 CRASH, lane 2
  `d781c1e804ad64d6`. Batch 2 accepted; no run 3. Proven: `must` and `line=` (drive.py), card 310 map arm rows
  (`writes 0, dropped from the world being left 0` on both sides; `C 0/1` rows applied on the client 8 times), real
  client fire counts the range (`count 1/10` t=304315995, `count 2/10` t=304322046).
  - Closed (see batch 3 run 2 findings: firing-line presence volumes), was open, range wave 2: no `Activate(1) ..._C_11` in 30 s after count 2/10. In the playtest it came 321 ms after
    count 2/10 (host t=81450607 then t=81450928), with the host at the range. Candidate: the next wave needs the
    host's pawn at the range (host stood about 12000 uu back). If so it is a co-op gap of the card 301 class.
    Batch 3 run: after count 2/10 wait 5 s for the Activate, then `host objective go host` and wait again; the row
    order names it.
  - Card 181 evidence: `card175: ... the copy fired 9 of the client's 27 rounds` on both 1500 ms holds; 31 `WARN
    card157 ... HitClaim ... (miss) found no round in 1500 ms: dropped`. Playtest: 288 of 396 rounds over 40 rows,
    40 dropped claims, all `(miss)`: no damage lost, the copy shows fewer rounds than the client fired.
  - Line 137: no NPC within 2500 uu at the FAST landing spot (`live npc in range 0` on both sides); gate correct.
  - Still untested: MENU focus activation (no hold occurred in either run).

- **Batch 3 (2026-11-14, build `e9aa849bb15eacf0`, lane 2):** one build carries cards 326 (registry), 327 (player
  rows, PW hash in the digest B frame), 332, 323 gaps (Down, seat, grenade, gib, hold replays), 320 (title-screen
  hold), 103 ring wrap, PickupGone removal, 328 step 1, 329 steps A and B, 330 step 1, 181 gap (senior: `c276_repress`
  re-presses while `c181_credits` is not 0). Senior read the whole diff; predictions are in the run message. Proof run
  `c310-proof-c1m1`, 3-run budget (the void-at-guard launch does not count).
  Open after batch 3: card 327 PW compare skipped in a mech, host logs the hash only, no PH row; hold-progress op;
  seat op (row only); card 320 waits only for clients that were sent IntroStart while the hold was on; card 329 W
  differs when streamed levels differ (round falls back to the "H" ask).
- **Path to play build 5 (user decision 2026-11-14):** batch 3 accepted, card 319 fix from its rows, soaks C1M1 and
  C2M1 only, then play build 5. Cards 324 and 322 and the C3M1 and C3M2 soaks move to after the playtest.
**Batch 3 run 1 findings (2026-11-14, `e9aa849bb15eacf0`)**: gate clean (digest rounds 1-8 mismatches 0, 0 CRASH). Card 333 (new, after playtest): client keeps a tactical support slot the host removed; `c104_receive_charge` skips host `-1` slots; found by the card 327 PW WARN (host f2fd, client f9bd), which is expected in `c310-proof-c1m1` after TacSupport_SO6 until 333 is fixed. Card 320: test hosts confirm the screen themselves (hold off after 2631 ms, client joined 7 s later, "not waited for"), so the wait on a client is proven only in play or on a load with the client connected (soft rows added at the checkpoint load). Card 319: 48 client samples at Assault_SO0 all `hostile 0: gathered 0`; sample now uses three code-spawned bugs. Run 1 failed at range wave 2 on a scenario defect (host sent to the SO3 marker, 12000 uu from the range), fixed.

**Batch 3 run 2 findings (2026-11-14, `e9aa849bb15eacf0`)**: gate failed on Pose only (corpse guid 0x1e2, a bug spawned by the run's own card 319 sample). Card 334 (new, after playtest): a client corpse copy that re-arrives with a settled, not frozen host pose keeps simulating and drifts (body 6 282 uu off at round 7). The sample is removed from `c310-proof-c1m1` for run 3. Card 319: second sample, no reproduction (client `hostile 3: gathered 3`, host `in ActiveMarkers 3`, red marks on both minimaps); play build 5 carries the always-on rows. Range waves: settled. Each wave is the LTOnEnter trigger of the next firing-line volume downrange, `BP_PlayerPresence_C_18` to `C_23` (x 5311, y 14811 to 22511); the level script only counts destroyed targets; the client pawn counts (it completed SO2 on C_17 alone). The scenario defect was both players staying at the first line; the client now walks to each line by actor name. No mod defect, no card 301 item. Card 181: range total `rounds 27`, `client rounds 3, copy rounds 3, refused 1, late-fired 1, unused 0`. Card 326: the 45 rows at host map arm are the new map's rows written during the load, intended.

Harness, goto goal projection (senior, 2026-11-14, source only, not built): `c64_plan` (`mod/src/ubw/testrun.cpp`)
queried the raw goal and returned "no path" when the goal lay off the navmesh; batch 3 runs 2, 3, 4 and 6 of
`c310-proof-c1m1` died on that at the range (run 6: goal (4581, 17871) is target C_4's slot). Now, when both
queries return 0 points, `c64_query_projected` takes the navmesh point nearest the goal within the goto radius
(`c64_project`, K2_ProjectPointToNavigation, half z 500) and queries the path to it; one `cmd: N goto: goal ... has
no path; nearest navmesh point ...` row. Goes in with the next build; `goto actor <volume> <radius>` then needs no
hand-picked coordinates. Not yet proven by a run.

Batch 3 run 7 finding (scenario, not a mod defect): range target C_5 was not killed because the scenario looked at
the bounds centre, 116 uu in front of the board; at yaw 36 from the firing line the shots passed about 160 uu
beside the board centre (half width 99). Host and client agreed on the target's phase. The range polls now aim at
the root + 60 as the solo proof `c65-range.txt` does. The run 2 entry's "walks to each line by actor name" is
superseded: the walks use the solo-proven points (x 4581, line y + 529 / + 569, then 4580 24500).

Batch 3 run 8 (build e9aa849bb15eacf0): range 10/10 with the root + 60 aim, ShootingRange_O1 completed and replayed,
digest rounds 1-6 mismatches 0. Then the client crashed at the first digest round after the travel checkpoint:
EXCEPTION_ACCESS_VIOLATION at ubwcoop+0x11B1E0 (read_i32 from c72_map_text from c327_inventory_hash from
c327_own_hash). Cause (card 327 PW, a batch 3 defect, fixed in source by the senior, not built): `c327_inventory_hash`
passed the WEAPON inventory (kMiWeaponInventory) to `c72_map_text`, which reads AmmoMap at + 0x118 of the
AMMUNITION inventory (kMiAmmoInventory). Before the reload the NumFree check happened to fail (ammo text empty, "ammo ;"
in the PW row); after it the stray memory passed and gave a data pointer 0x400000002. All three users
(`c327_row`, `c327_host_hash`, `c327_own_hash`) went through it. Fix: the function takes the soldier and reads each
text from its own inventory. Consequence: the PW hash never held real ammo, so the card 333 PW WARN (host f2fd,
client f9bd) is to be re-read after the fix; with real ammo in it the host copy and the client can differ for a
moment while the client fires (AmmoMirror lag), so a PW WARN is not a gate failure.

- **Batch 3 run 9 (`6ce017264c0f3c81`)**: card 327 crash fix holds (no crash; host 21 `card327` player rows, client
  0 PW WARN). Range 10/10. Must failure at CaptureTheFlag_SO4 was a scenario defect (focus gate named the owner actor,
  the focus row names the component `SatchelSiteCAC_...`); fixed in `c310-proof-c1m1.txt`. New finding, card 335
  (after the playtest): after `travel checkpoint` the 9 range targets and the grenade nest are dead on both sides but
  raised on the host (z 50 to 100) and lowered on the client (z -250 to -550); 10 digest mismatches per round, rounds
  7 to 16. Expected again in every run that loads a checkpoint until card 335 lands; not a gate failure for the soak.
  Goto goal projection: built in, no `has no path` row fired in run 9, still not proven by a run.

- **Card 318, run 9 read**: revive state fix holds (77 revives, all to Aware; copies move with the host). Open: 16 of
  29 read-backs show the animator still on `Death frame 11 of 12` 2 s after the revive on a moving copy; 13 others
  are re-deaths inside the window (false WARN). On-screen result not proven: needs one client screenshot of a revived
  laser trooper (scenario step to add after run 10; text in the card). The Pose `R ... skipped: no row` WARNs are
  benign (client dropped the row at the revive).

- **Batch 3 run 11 (`6ce017264c0f3c81`) = C1M1 soak PASSED**: scenario ran to the end and travelled to FED_C2M1,
  646 s, 17 of 17 objective waits done, 0 failed, no crash, `gate skips: 0`. Digest: rounds 1 to 6 and 17 to 20 clean;
  rounds 7 to 15 only the card 335 range targets (9 per round, 7 in round 15). Card 332 teardown rows all `0 frame(s)
  not sent`. Card 318: screenshot shows revived troopers upright (check is wrong, fix is right; card doc). Open, all
  after the playtest: one card 327 PW WARN after the travel (card 333 doc); card 320 on C2M1: host hold ran to its
  60000 ms timeout because the scenario has no client continue step after the travel (scenario gap, not a mod fault;
  the host autodropship `pause requester[0] of 0 is null` WARNs x7 in the same window are not traced); goto goal
  projection ran and found no mesh point inside 500 uu for BP_PlayerPresence_C_11 (centre z 520): `kC64ProjectHalfZ`
  raised to 1200 in source, not built. Dead time 136.5 s in 6 timed-out try waits (lines 1019, 1020, 1041, 1047,
  1146, 1148): trim before the next use of this scenario. C2M1 soak launched next by the junior.

- **C2M1 soak run 1 (`6ce017264c0f3c81`) stopped, harness fault**: blocked at LOS_O02_SO01 (actors 0, no marker actor:
  `objective go` failed `no actor and no objective marker actor`; both players already stood inside the presence
  volume when the step was added, 90 s open). Logs `logs/b11-soak-c2m1-r1-*`. Harness fix in
  `objective_c284.cpp` (test tool only): such a step stands at the BP_PlayerPresence_C nearest the host, and a pawn
  already inside the stand's volume is lifted out before the entry warp. Scenario line 109 waits on the client's
  card097 SO02 row (the host sends only the LOS_O01 parent row; both sides end equal). Run 2 launched on
  `f154573e89ec471a` (lane 2; also carries `kC64ProjectHalfZ` 1200). Prediction: SO01 done inside 10 s of the go.

- 2026-11-14, C2M1 soak run 2 (`b11-soak-c2m1`, DLL `f154573e89ec471a`): STOPPED at LOS_O02_SO01, prediction
  "done inside 10 s of the go" failed. Both players were lifted out of and re-entered `BP_PlayerPresence_C_1`
  (`inside 1`); the step stayed open 60 s. That presence had flipped `0x07000001 -> 0x07000000` 53 ms after
  LOS_O01_SO02 completed, so it was used up. `logs/c255-c2m1-r2-host.log` shows the real trigger: SO01 completed in
  the frame of `cmd: 59 warp` to (158906, 23488, 10615), and `BP_PlayerPresence_C_5` and `_10` flipped off 30 ms
  later, in the plasma area the sibling row `Plasma_Marker` (ActorReference_C_10) points at. Harness-only fault, no
  mod bug. Method changed (second failure): an actorless step now stands at the switched-on presence nearest the
  sibling marker row's actor, the next nearest on each further `objective go` for the same step, 3 at most
  (`c284_actorless_stand`, `objective_c284.cpp`); the scenario retries the go at LOS_O02_SO01, LOS_O04_SO01 and
  LOS_O03_SO01. Run 3 prediction: one `c284: actorless step stands at presence try N of 3` row names `_10`, `_4` or
  `_5` and SO01 completes inside 10 s of that go, by try 3 at the latest. Run 3 is the last of this batch's budget:
  a third failure at this step means the step is driven by the old coordinate point for the playtest gate and the
  class fix moves after the playtest.

- 2026-11-14, **play build 5 = `d20f47aa1b0383a5`** (installed in `game/`, package `dist/UBW-Coop-20261114-1132.zip`),
  built from the C2M1 run 3 source (`6e1ae8d6795e3eb8` plus nothing). C2M1 soak run 3: LOS_O02_SO01 completed 6 ms
  after try 1 (presence at (170999, 12157, 7995), 4000 uu from the sibling marker; my named presences were wrong,
  the method right); stopped at LOS_O02_SO02, line 128: `objective go` finds no navmesh point near marker
  ActorReference_C (152520, 23320, 10943) nor at the nearest presence (4193 uu). Harness gap, no mod fault; 3 of 20
  objective waits done; no CRASH, no new WARN class. The C2M1 soak is NOT passed: the user playtests on the C1M1
  soak plus this partial run (deadline set by the user). Soak C2M1 resumes after the playtest.
- 2026-11-14, card 330 cut tried (per-class ext row cache in `c132_actor_ext`, build `078779091777dea2`, C2M1 334 s):
  ext `cost avg` 475.5-534.3 us to 376.2-404.1 us per tick, `c132_ext_step` 960-1095 to 660-712 ms per 30 s window,
  mod share 6.45% to 4.97%; baselines 212, sweep 214 ext, digest unchanged. Missed my 300 us bar (prediction 150-250
  us was wrong: the rest is `c140_placed_visit` and the poll), so it is NOT in play build 5. Safe; rides the next
  build. Source: junior scratchpad `digest_c132-eff.cpp`. Next step: time `c140_placed_visit` inside the sweep.

- 2026-11-14, card 330 ext split (`9810dd43745dca7d`, `logs/eff-split-c2m1-host.log`, C2M1, us per tick): poll
  23.3-33.6, class lookup 35.9-49.6, `c140_placed_visit` 293.7-367.9 (80-82%), rest about 1; total 359.1-451.9.
  About 30.6k visits per sweep, 97% reach `c140_placed_entry` (`world_c98.cpp:2107-2130`): the early-out filters
  nothing. All three predictions held. Fix to trace (static, before any code): visit only actors whose root can
  move (root mobility Movable), kept as a member list built when a level's actor count changes, so the 0.5 s
  move poll of card 140 keeps its period on the few movers. Must keep every card 140 placed row of a soak unchanged.

- 2026-11-14 12:20, next lane 2 build (senior code, junior reviews, builds, runs; playtest moved to later today):
  (1) card 330: `c140_placed_visit(actor, full)` visits a root that is not Movable only every `kExtFullEvery` = 16th
  sweep (about 8 s), with a WARN if one moved all the same. Predicted C2M1: visit pass 293.7-367.9 to 60-130 us per
  tick, ext total under 300 us, no such WARN, same placed classes. (2) LOS_O02_SO02 stand: BP_PlayerPresence_C_5
  (156649, 23887, 10480) has no navmesh point at its centre; `c284_presence_stand` now tries 3 rings x 8 points
  inside the shape (c255-c2m1-r2 completed the step 2300 uu off the centre). (3) card 324 step 1 diagnostic.

- Card 336 (fix in lane 2 build 254148eb873d65d7, run pending): not a sync bug. `objective go` landed players inside
  BP_FallZones_C volumes (30 placed on C2M1); the zone hit (damage 20) moves the pawn to the game's fixed recovery
  point (227524, -17297, 6512). Fix in objective_c284.cpp: slots and lifts avoid fall zones; a stand inside one fails
  the command. Open: the host's own pawn was hit at cmd 144 and not moved.
- Card 337 (junior, card text): the client sees the open map for 2.3 s (C1M1) to 4.3 s (C2M1) before its loading
  screen, which is built only when the host's join-replay IntroStart arrives. User rule 2026-11-14: "the client
  should be watching the same cutscene as the host".
- Test dead time (user, 2026-11-14: "idling waiting for the cutscenes to end, and the standing around"): r6 spent 671 s
  of 920 s in 88 waits that ran to their timeout and 129 s in the C2M1 travel (card 320 hold ran to its 60 s timeout
  because the scenario pressed the client's continue after the host pawn wait; autodropship burned its 8 actions on
  the hold's pause and the flight ran unskipped). Done, not yet run: dropship.cpp (autodropship waits for the hold),
  drive.py dead-wait cap (group = header or `objective expect`; later waits 1 s after a failure), scenario travel
  block, replay waits 30 -> 8 s. Open: objective-driven loop step on the card 284 tree.
- b15 (lane 2 build 254148eb873d65d7, b11-soak-c2m1, stopped at 408 s in LOS_O03_SO03): travel fix passed (card 320
  hold off 9573 ms "all confirmed", 1 autodropship action, flight skipped; travel 49.2 s, was 129 s). Dead time left:
  142.8 s kill-round waits (now 3 s rounds), 77.3 s failed waits. Card 336 zone skip blocks real presences and satchel
  sites (4 "landing inside BP_FallZones_C"); junior traces the zone's overlap graph (falling-pawn hypothesis). Card 337
  traced (cover = SetEnableWorldRendering off on the client from LoadMap return to its title screen; see the card).
- b16 (lane 2 build 31440a3d3a96efcb, stopped at 288 s, LOS_O03 blocked): kill-round waits gone (3 s rounds); split
  work 145.3, other wait 80.7, failed wait 31.0, travel 29.6 s. Card 336 trace: ASTFallZonesBase::OnBeginOverlap has no
  falling check (hit row "mode 1, z velocity 0"); zone skip stays. LOS_O03 block: actorless stand picks a presence whose
  floor drop is a fall-zone chasm (junior fixes the presence choice); satchel aim hits terrain (aim points above floor).
- Card 338 (new, junior, real co-op bug, in play build 5): a fall zone has one timer handle (+0x3a8); a second player
  entering within the rate replaces the first player's RepositionPlayer, the first is never repositioned (r6 cmd 144:
  host hit, not moved). Open: state the first player is left in (input off?). Fix: host detour, per-pawn reposition.
- Card 337: being coded from the senior trace in the card (world rendering off on the client until its title screen).
- 2026-11-14 user: "it seems like we're just working on this soak, and not bugs that don't need the soak". Lane 2 build
  now carries bugs only: 338 fix, 337 fix, 324 and 322 step 1 rows; proof is tools/scenarios/c338-c337-c1m1.txt (under
  4 min), not the soak. c284 actorless stand and satchel aim are parked. Next build: card 334 (senior trace in the
  card, one condition in pose_try_apply), 335.
- 2026-11-14 short bug batch, runs 1 and 2 (`c338-c337`, b17 and b18, 93 s, both rc 0; logs `logs/c338-c337-*`):
  - 337 proven twice: client world rendering off at LoadMap return, on after 4066 / 3861 ms (run 1) and 3765 / 3971 ms
    (run 2), reason "title screen up", 0 WARN card337, `c337-client-load` shot black.
  - 338 proven in run 2: forced reposition row for the pending pawn when the second pawn armed (t=324284259), both 5 s
    rows `input 1, moved 538 / 534 uu, movement mode 1`, two client skip rows, 0 WARN card338. The warp column overlaps
    two zones, so the scenario's near checks were wrong and are removed for run 3.
  - 322 step 1: tallies differ by name. FED_C1M1: INTRO01, LANDED01 host only. FED_C2M1: DROPSITELANDING01,
    INTROFLIGHT01 host only (5 to 6 s after the client was in the map); FLAKBUGINTRO01 both. 322 needs its fix.
  - 324 not reproduced: 6 of 6 client rows FirstPersonMesh visible 1, hand_r on screen 1. Senior viewed the shots:
    `c324-a-300ms` shows the flare in the client's hand; `c324-b-800ms` shows the throw follow-through (hand open, the
    flare already thrown, supply pod landed ahead), so 800 ms is past the hold, not a missing flare. Needs the user's
    conditions or playtest logs.
  - b19 `40e935ba1137d921` (review findings + card 334 hold of a settled final) goes into run 3 with a 334 proof block.
- 2026-11-14 run 3 (`c338-c337`, b19 `40e935ba1137d921`, 118 s, rc 0): 337, 338, 334 accepted (CARDLOG). Host WARN
  13 -> 1, client 16 -> 18: new are 2 card152 bone checks on the held copy 0x238 (pelvis bone 27.5 uu from body 0,
  bodies 0.0 uu off; same bone-vs-body class as the 13 settled-final rows) and 1 card327 "PW differs for two rounds:
  host 8f9b, client 9ed1" (untraced, second sighting). Card 339 opened: host unfreeze snap on `1 -> 1` calls (16 rows,
  445 bytes each). Next lane 2 build: 339, then 322 fix (senior trace first), 335. Next card number: 340.
- **Lane 2 build b21 (assigned 2026-11-14, bug fixes only, no soak):** card 339 (skip plus the moved flag), card 322
  part A (host-owned conversation starts, `ChanOp::Radio`), card 335 (client stops a copy's timeline when the host's
  state is not playing; senior's hunk in `c185_client_state`). Runs: `c338-c337-c1m1.txt` (339, 322), then
  `c335-range-load.txt` (335, about 6 minutes). Predictions are in each card.

- **191 Held final leaves the bones off the bodies** (`docs/cards/done/191-frozen-final-bones-off-bodies.md`, section
  "Run 3 of c338-c337"). Settled finals have it too since card 334: 15 bone check WARN rows, 27.5 uu at the first
  final with body error 0.0. Prediction B ruled out; next static step named in the card.

- **b21 run 4 result (`c338-c337`, 2026-11-14):** 128 ok, 118 s, host WARN 1, client WARN 6 (1 attach, 4 card152
  bone check on guid 0x220, card 191; 1 card327 PW). 339 and 322 part A accepted. `c335-range-load.txt` run next.
- **Lane 2 build b22 (assigned 2026-11-14):** card 322 list-skip group fix (junior), card 327 order-free ammo hash
  (senior's hunk in `c327_inventory_hash`), card 191 `ExecuteTick` diagnostic. Proof: one run of
  `c338-c337-c1m1.txt`; predictions in cards 327, 322, 191.

- **b22 `0c40725031a4aede` result (`c338-c337`, 2026-11-14):** 327 PW WARN 0, 322 map-end WARN 0, card 336
  fall-zone check now honours the zone's +0x310 gate. Card 191 rows 0 (armed on held only; the corpse froze).
  Client WARN 7: 1 attach, 6 card152 on guid 0x21a, up to 306 uu on the re-arrival (card 191, now the top visible
  fault). `c335-range-load.txt` rerun on b22 in progress.
- **Lane 2 build b23 (assigned 2026-11-14):** card 191 diagnostic armed on every client final apply, plus arrival
  rows. Proof: one run of `c338-c337-c1m1.txt` (scenario lines 151, 152, 165, 176 fixed by the senior).

b23 result and b24 assignment (2026-11-14): b23 `0725fd1fbbe12914` gave the card 191 rows (cause in the card:
the blend copies only simulating bodies, and the animation refresh overwrites the bones after the freeze). b24, with
the junior: card 191 fix (freeze waits for the bones, `bNoSkeletonUpdate` on a held copy), goto landing wait
(`testrun.cpp`, c335 line 260), aim scan focus sweep already in b23 (c335 line 167 passes). Proof: `c338-c337-c1m1`
then `c335-range-load`; card 335 itself still unproven (no run has reached `travel checkpoint`).

b24 result and b25 assignment (2026-11-14): card 191 accepted on b24 `5dfc485c94735310` (card file, CARDLOG). The
c335 run voided at line 191: the goto ended with the pawn still sliding (about 420 uu/s), so the look's focus was
gone at the E press. b25 (junior building): goto ends only when the pawn stands still (3 ticks, 0.5 uu), with a
WARN and failure at the timeout; `freeze_waits` reset per apply; a row for a completed freeze wait. One run of
`c335-range-load.txt`. Card 335 is still unproven. Card 340 (dropship lead takes the upper median round trip, one
line in `pl_rtt_median`) is written and goes in the build after b25. Next card number: 341.

b25 result and b26 assignment (2026-11-14): card 335 accepted on b25 `c967354ff1db0488` (scenario passed, digest
rounds 7 to 10 after the load at 0 mismatches). The goto standing wait and landing wait both worked (lines 191 and
260 pass). b26 (junior): card 340 (`pl_rtt_median` lower middle) and a card 323 leftover (a grenade destroyed by a
checkpoint load in flight left a live row; the join replay now frees it, no WARN). One run of `c338-c337-c1m1`.
Card 341 written (digest slot sum differs on an entry inside the compare tolerance; diagnostic first, not in b26).
Next card number: 342.

b26 result and b27 assignment (2026-11-14): b26 `d05529011e95739a`, run 14 of `c338-c337`: 0 WARN card159, card191
`freeze waited 1 tick(s)` then 0.0 uu (the wait path now proven), 0 bone check WARN. Card 340's lower middle still
read a hitch (ring 1643, 30, 1106), so b27 takes the lowest of the ring (`pl_rtt_floor`). The run failed only on
scenario line 176 (`count=2` own-start skips; the client reached one), now one row. b27 also carries the card 341
diagnostic row and the junior's fix to the grenade row free (a cls-null row keeps its row). Runs: `c338-c337-c1m1`,
then `c335-range-load`.

b27 result and b28 assignment (2026-11-14): b27 `0ebdac007a685d9c`, runs 15 (`c338-c337`) and 16
(`c335-range-load`) both pass. Accepted: 340 (rtt 30 on every transport row after pong 2) and the card 323 grenade
row free (`1 gone (row freed)`). Card 341 step 1 named the entries (`TargetRangeTarger_C_3/_4` one round,
`TacSuppRangeTarget_C_24` every round after the load). b28 (senior's edits, uncompiled): card 342
`ds_host_launch_edge` (`dropship.cpp`: an L the frame a ship starts along its spline) and the card 341 adopt table
(`digest_c132.cpp`). Card 343 (traced, fix written in `round_c181.cpp` for b29, uncompiled): the client fires one more
round 14 ms after its release went out, the host copy has no press for it (`WARN card157` shot 108); a Round within
500 ms of the release now gets a one-round press. Next card number: 344.

- Card 344 (user decision 2026-11-14, written for b29, not compiled): no client confirm 60 s after the host's continue sends the host to the main menu (`ReturnToMainMenu`), the client follows; replaces host-goes-alone. Card file has the prediction. Next card number: 345.

- b28 `c18852793a3d72b2` result: 341 and 342 accepted (card files). b29 assignment: 343 (round after the release), 344 (no confirm to menu), 342 row instance name.

- Card 345 (user, 2026-11-14: the lobby must persist, the client disconnects after a match): the client's own
  level-end Next left the session (playtest log 2026-11-13). Fix in `restart.cpp`, `ue/net.{h,cpp}`, test command
  `travel end`, scenario `c345-level-end.txt`; uncompiled, rides b30. Card file has cause and prediction.
  Next card number: 346.

- **Card 346, persistent connection (user 2026-11-14: "No, just make persistent lobby.")**: host map changes with a
  client connected go through `UWorld::ServerTravel` with `bUseSeamlessTravel` (GameMode+0x328 bit 0) instead of a
  hard travel; map-change steps move to `UNetDriver::CleanupWorldForSeamlessTravel` and
  `PostSeamlessTravelGarbageCollect`. Trace and prediction in `docs/cards/done/346-persistent-connection-seamless-travel.md`.
  Not written. After card 345's run. Next card number: 347.

- **b29 `d63c0ff755c44609` / b30 `9e23007ab565017e` (2026-11-14)**: 343, 344, 345 accepted (CARDLOG). b31 with the
  junior: card 346, 345b (client's level-end Next waits for the host's relayed Next; panel stays with a waiting line),
  344 leftovers (no listen and no travel row for a valid-0 host URL; scenario mark, reconnect wait, later shots).
  Scenario `tools/scenarios/c346-persistent.txt` written (52 steps, never run, no dll pins).
- **Card 347, grenade thrown at a stale pitch (run 20, `logs/c335-range-{host,client}.log`)**: client looked to pitch
  -19.4 at t=331435352 and to -5.4 at 331435651, threw at 331437350; the host's server copy threw at aim P -19.43
  (velocity (2148,-1956,-748) against the client's (2218,-2020,-20)), 290 uu short, target not hit. b28 same step: host
  -5.50. The throw aims with the PC's `GetPlayerViewPoint`; on the host that is the camera cache fed by the client's
  `ServerUpdateCamera` (from the engine source as I remember it: unreliable, sent only after a view change). Two gaps:
  the host's view of a still client can stay stale, and the client's own grenade flies its own path instead of the
  host's. Trace not finished; fix direction: the client's view with the throw request, applied on the host before the
  throw. Senior owns the trace. Next card number: 348.

Card 347 update, 2026-11-14: traced and fixed in source, uncompiled (`docs/cards/done/347-grenade-stale-host-view.md`).
The client resends a still view to the host only after the engine timeout; `c347_client_step` lowers it to 0.2 s.
Rides b31; proof is the grenade step of `c335-range-load`.

b33 `100476d0085b4f43` (lane 2), 2026-11-14: cards 346, 345b, 344 leftovers and 347 accepted. `c346-persistent`,
`c345-level-end`, `c344-no-confirm` and `c335-range-load` (rerun) passed. The client's connection now survives
mission end, hangar and next mission; a card 344 return takes the client along (net mode 3, no reconnect).
Queue from this build: card 348 (old-PC traffic after a travel: stale first Pong, AmmoMirror), card 349 (52
GetFirstPlayerController sites), card 350 (photosensitivity WARNING screen on both sides at the card 344 return;
compare with single player first). Watch: `c335-range-load` line 271 TacSupport_SO6 failed once (client `card021
... handle 102: failed, spec present`), rerun passed; the new ServerEndAbility diag hook (`fire.cpp:24`) is a
candidate; card it on a second failure. Untriaged: c344 client WARN card322 x2, card159 dropship check x2.
Next card number: 351.

b34 `a4a709a1b9ed4c46` (lane 2, 2026-11-14): cards 348 and 349 accepted (`c345-level-end`, `c344-no-confirm`,
`c346-persistent` passed). `c346-persistent` now waits for the client's GameStarted row before the client's
continue. Test lane only: client `WARN card114 ... err 32` when host and client write
`UBWCoop-Difficulty.defaults.ini` in the shared prefix in the same millisecond. Triage of two old WARNs: card322 x2 is
the client's own scripting starting the LANDED01/02 radios 1 ms before the host's `travel next`; card159 x2 is
in-flight dropships drifting 3400 spline units between the join row and the check with 0 live dropship frames.
Next: card 350. Next card number: 351.

b36 `312bc9f472523c84` (lane 2, 2026-11-14): card 350 accepted (`c350-c344` on b35, `c350-c345` on b36). After a
mission both players now see the same hangar panel. Lane 2 holds 337/334/191/335/340/342/341/343/344/345/345b/346/
347/348/349/350 over play build 5. Next card number: 351.

b37 `f3d490b51e1e21f4` (lane 2, 2026-11-14, scenario `b37-c318-c324-c1m1`): card 318 closed (check change). Card 324
step 2 does not reproduce (flare visible in the hand on client and host with the Morita); parked for the playtest.
New: card 351 (card 073 grant wait warns when the item was removed in the same tick), card 352 (fire press during a
held support item reaches the weapon relay). Harness: client goto to `BP_WeaponPickup_LaserMorita_C_0` fails (goal
168 uu off the navmesh), the E press still reaches the pickup. Next card number: 353.

b38 `24c5e7454353752f` (lane 2, 2026-11-14, scenario `b38-c351-c352-c1m1`): cards 351 and 352 accepted. Unbuilt
senior edit in `fire.cpp`: the card 352 host backstop logs only. New: card 353 (client grenade count one low when
the host's count frame lands before the client's own consume; seen once in 2 runs). Next card number: 354.

b39 `f6cfe3ce7f7ad7a8` (lane 2, 2026-11-14): card 353 accepted (host frame is the only writer of the client's own
item counts; 3 skips, 0 local-consume writes in run `b39`); carries the card 352 host backstop as log-only (0 rows in
the run, path not exercised). Unbuilt senior edit for b40: untraced callers of the skipped removal log `WARN card353`.
Card 354 (item adds, same two-writer shape) assigned. Next card number: 355.

Card 355 (user rule 2026-11-14, "Host to main menu"): a client that leaves during the card 320 hold returns the
host to the main menu. Senior edit in `intro_c135.cpp`, unbuilt, rides b40; 1 run to prove. Next card number: 356.

b40 `81b66827ad3499bb` (lane 2, 2026-11-14): card 355 accepted (run `c355-b40`); card 354 pickup half accepted (run
`b40`), loadout half (`ResetInventory` skip) unrun: 1 run on FED_C2M1 with a client respawn mid-mission assigned.
Unbuilt senior edit: card 344/355 panel line. On watch: client audio mixer crash at quit (first sight); weapon
mismatch after a checkpoint reload that follows mission end (WARN card327, run `b40`).

b41 `9b2775ac3b120578` (lane 2, 2026-11-14): panel line text only. Run `b41` found card 356: on FED_C2M1 the
loadout's 2 frag grenades are on the host's copy of the client and never reach the client (count 0 against 2, no
join replay of item counts). Assigned, 1 run. Next card number: 357.

b42 `8965a327d495d442` (lane 2, 2026-11-14): card 356 accepted (join replay sends the server copy's whole item
inventory; client grenades 0 to 2 at first spawn and after a checkpoint reload), loadout half of card 354 proven.
Run `b42` found two client WARN kinds not in older C2M1 logs, both carded: 357 (client runs the game's
`StartOverkillState` when a host grenade overkills corpses, on top of the host's Gib signal) and 358 (`WARN card191`,
corpse 0x424 frozen 328.8 uu off its bodies after a checkpoint reload; 0x322 1.9 uu, three times). Next lane 2 batch:
357 fix + 358 trace. Next card number: 359.

b43 `8f7d7fc43a76c451` (lane 2, 2026-11-14): cards 357 and 358 accepted on run `b43r2` (5 native overkill skips,
0 WARN card038, 0 WARN card191). Card 359 (bone residue 1.9 to 3.3 uu, low) queued after the playtest. On watch from
`b43r2`: one `WARN card140` hit drift 372 uu on guid 0x398 (kHitDrift 300), first sight; Pose reset frames for
just-gibbed corpses logged "skipped: no row" (3). Next card number: 360.

b44 `d875ca838402bfc5` (lane 2, 2026-11-14): card 333 sibling sweep done (weapon slots and grenade rows no gap;
ammo map gap fixed: the client's own loadout `AddAmmo`, caller exe+0x51DA71D, is not run on its own pawn, the host's
AmmoSet is the only writer; run `b44r2` one skip of stale Laser 300, 0 WARN card327). Card 322 part B closed: music
rows equal, time dilation not in the Trooper playthrough, out-of-bounds overlaps run per process for both pawns with
the tint on the owner's screen only. Left over and carded: 360 (out-of-bounds expiry kills per process, no authority
check; senior static trace in the card). Next card number: 361.

b45 `1956b296fed986e9` (lane 2, 2026-11-14): card 360 accepted on run `b45r2` (out-of-bounds expiry: the host is
the only killer, the client skips `KillOwningActor` and gets the death by the Down frame). Sweep found the class:
card 361, one `InstaKillTarget` hook that skips player targets in a client session (fall zone, door, hazard crush,
dropship crush, teleporter, BP exec). On watch: `WARN card140` on the client in b43r2 (1) and b45r2 (2).
Next card number: 362.

b46 `3f7559c146eb5a81` (lane 2, 2026-11-14): card 361 accepted (client skips InstaKillTarget on player pawns; host
hazard crush half of card 321, latent because no shipped hazard crushes). Run b46: 2 client skips, matching host Down
frames, 0 WARN card361. Queued for the next lane 2 build: card 362 (WARN when a client skip gets no Down frame in
3 s). Next card number: 363.

b47 `faef8a0da0c13495` (lane 2, 2026-11-14): card 362 accepted; cards 355 and 350 now proven by run b47r2. Open:
card 363 (client decides corpse tickets alone: InstaKill of dying NPCs from HandleDying, static questions in the
card). Next card number: 364.

Card 363 closed 2026-11-14 (static, no behaviour change; caller table row rides the next lane 2 build). Next card
number: 364.

b48 `6225789a575f9809` (lane 2, 2026-11-14): card 329 step B per-PC Fx rows accepted (diagnostic only; C2M1 fight
3066 B/s of Fx, no fix on these numbers) and step C closed static (resnaps are corpses under fire; no fix). Both
written in the card. Lane 2 bug queue outside the playtest is empty. Next card number: 364.

b49 `aebfe1230e491c6d` (lane 2, 2026-11-14): card 329 step E NPC props rows accepted (diagnostic only; C2M1 fight
11450 B/s, far band 28%, no fix). Card 329 sibling sweep closed, no fix. Card 329 is parked for the playtest log
(read its card329 and netbudget rows). Lane 2 has no bug work left outside the playtest. Next card number: 364.

2026-11-15, **play build 6 = `aebfe1230e491c6d`** (user decision "6"; installed in `game/` and `build/dwmapi.dll`, copy
`build/dwmapi-play-aebfe1230e491c6d.dll`, package `dist/UBW-Coop-20261115-0710.zip`). It is b49: play build 5 plus
cards 344 to 363 and the card 329 step B and E rows. Lane 1 is now this build; lane 2 holds the same hash. After
the playtest: read both logs, playtest bugs first, then the card329 and netbudget rows. Next card number: 364.

2026-11-15, audit pass 3 (session lifecycle, `docs/SYNC_AUDIT.md` "Third pass"): card 364 (the title-screen hold
released while a connected client was still loading after a seamless travel; fix written by the senior in
`intro_c135.cpp`, not built yet) and card 365 (RESTART MISSION under seamless travel never run). Both go into one
lane 2 build and run. Play build 6 in lane 1 does not have the card 364 fix. Next card number: 366.

Audit pass 3, player state (2026-11-15): card 366 written and fixed in source by the senior (death.cpp
`c43_host_step`: a Downed or Out host whose last teammate leaves now runs its real death; before, it stayed down for
good). Not built yet: goes into the lane 2 build after b50. **Next card number: 367.**

b50 (`f63840e619112273`, lane 2) run 1, 2026-11-15: the first card 364 fix missed (a seamless-travel client has no
PC in the new world until its load ends; rows in the card). Second fix in source (travel-carried client count), with
card 366, assigned to the junior as b51: run A card 364, run B card 366. Card 365 result comes from b50 run 2. Play
build 6 has none of these fixes.

2026-11-15: cards 364, 365, 366 accepted on b51 `f46ae0eb6b1386d6` (lane 2; CARDLOG). Play build 6
(`aebfe1230e491c6d`) is still in lane 1 and `build/dwmapi.dll`: cutting play build 7 from b51 waits for the user's
word. Open from run b51b: `WARN card209 ... guid 0x0 not sendable` for the host's Dead frame when no client is
connected (log noise, not carded). **Next card number: 367.**

2026-11-15, efficiency audit (card 367, from b50r2 `card330:` rows): the client's card 037 scan walked every actor
of the map each frame (2118 calls, 1164.3 ms on C2M1; client mod 7.83 to 9.45% of frame time, host 3.99 to 4.62%).
In source for b52: the walk every 100 ms with weak refs between, and `C330_STEP` around the children of `c43_step`
and `c50_step` (client `c43_step` 367.5 us mean with no counted walk). Open in the card: `c28_step` own level walk
at 10 Hz, host turret walk at 10 Hz, 38 level walks the card 330 collect row does not count. Next card number: 368.

**2026-11-15, card 367 on b52 and card 368.** b52 (`e3d10276a0c5ff9b`) proved card 367 finding 1: client `c37_step`
1195.5 to 232.3 ms per C2M1 stay, collect calls 1797-1952 to 314-448 per 30 s, death edges 13 of 13, 0 `WARN card037`.
The "client mod under 6%" prediction missed (5.57-8.51%) because of the test-only tail of `c43_step`. Card 368 (source
in, not built) gives the remaining periodic walks one cached list per class and caches the dropship lookup; build b53
and one run are with the junior. Next card number: 369.

2026-11-15, card 368 accepted on lane 2 build b54 `cd14c3d92a441cf0` (b52 + card 368 + `kDsNamed` 32 + the
`detour_spawn_gore` audit gate from card 330). Lane 1 stays at play build 6 `aebfe1230e491c6d` until the user asks
for build 7. Efficiency audit state: cards 330 and 359 hold the b53 notes; the shared level walk is not carded (about
0.5% of frame time, revisit only if the playtest logs show walk hitches); card 329 waits for the playtest logs. Next
card number: 369.

- **2026-11-15, card 369 opened (senior trace).** The host's pause menu QUIT with a client connected was still a hard travel after card 346 (client dropped, card 045 follow, empty card 350 options on the client). Fix written by the senior in `restart.cpp` / `menu_c350.cpp` / `hooks_internal.h` for lane 2 build b55; scenario `tools/scenarios/c369-host-quit.txt`; predictions in `docs/cards/done/369-host-pause-quit-seamless.md`. Not built, not run yet. Next card number: 370.

- **2026-11-15, turret rows with the card 368 class cache proven (run `c125b54c`, b54, FED_C3M1).** 5 of 5 predictions pass on run 3 of 3 (2 void: scenario order at the C3M1 dropship start, `focus=` aim on turret bounds; `tools/scenarios/c125-proof.txt` fixed by the junior). Senior check of the logs: host row sent 40, client applied 40; client aim sent 20, host aim applied 20; FATAL/ERROR 0 both; WARN host 4, client 19 (card152/card191 on guid 0xd4/0xe8 noted in card 359). Results in card 330 "Results, run c125b54c". Closes the item left open at the card 368 accept.

2026-11-15, card 369 accepted on b55 `29ad572d08da2d85` (run `c369b55`, 6 of 6, senior viewed the client hangar shot: hangar, no WARNING screen). The host's pause menu QUIT keeps the client in the session. Lane 2 is b55; lane 1 is still play build 6 (`aebfe1230e491c6d`), which lacks cards 364 to 369; play build 7 waits for the user's word. Next card number: 370.

2026-11-15, host in-place load with a client re-proven on b55 (run `c143b55d`, scenario `c143-restore.txt`, 4 runs, the first 3 were scenario faults): Reload, reconnect (2501 ms), Restore applied, digest 0 mismatches, host stays on the map. The run's death screens came from the scenario's warp point under the world, not from the restore. Card 370 opened from it (kill floor warp picks a falling teammate; low, after the playtest). Next card number: 371.

2026-11-15, card 370 accepted on b56 `08265a76bc24632c` (run `c370b56`, 65 s, FATAL/ERROR 0 each side, no new WARN rows): kill floor warps only to a grounded teammate; with none, an Up pawn returns to the checkpoint anchor. Lane 2 is b56; lane 1 is still play build 6 (`aebfe1230e491c6d`, lacks cards 364-370). `tools/scenarios/c143-restore.txt` now uses grounded warp points (6554 11459 197 / 6654 11459 197), not run since. Next card number: 371.

2026-11-15, card 372 opened (static audit): `c46_rescue_spot` fallback took a host pawn below the kill floor; fix is in source (objectives.cpp), not built. Number 371 is reserved for a defect from the RESTART CHECKPOINT proof (`c371-restart-checkpoint`). Next card number: 373.

2026-11-15, cards 371 and 372 accepted on b57 `78025485639b5da3` (`build-b5/dwmapi-b57-78025485639b5da3.dll`, run `c371b57`). Lane 2 is b57; lane 1 is still play build 6 (`aebfe1230e491c6d`, lacks cards 364-372). The death-screen RESTART CHECKPOINT click path with a client is now proven by run. New from the run: STAISpawner_5 location mismatch after an in-place load on FED_C1M1 (2 of 2 runs), noted in card 218. Next card number: 373.

2026-11-15, host death-screen RESTART MISSION and Exit proven by real clicks with a client (run `c373b57b`, b57, no card): both travel seamlessly with the client (card346 rows, no disconnect, 0 card344/card355, FATAL/ERROR 0, digest 0 mismatches after the restart). Rule: on the seamless restart the client's death screen is cleared by the card045 "respawn in place" row; no card294 row is expected there. Exit opens `3D_Frontend?FromGame`: both sides land on the title screen, not the hangar (game's own route). Going on from that title screen into a mission as a pair is under test (Phase C).

2026-11-15, Phase C of the death-screen proof (runs `c373c57`, `c373c57b`, b57, no card): after the death-screen Exit the client is carried from the `FromGame` title screen into the next mission by seamless travel with no key press (the hangar launched the save's mission, FED_C3M2); digest rounds 2 and 3: 0 mismatches (27779 actors). First by-run proof of card 344's bound for a carried client: `WARN card344: host ... 1 client(s) did not confirm ... within 60000 ms`, both back in the hangar together. On-foot rows on C3M2 not reached (start dropship; scenario limit). Watch for the C3M2 soak: `WARN card159` on `BP_PlayerTransportDropship_C_4`, 84453 uu off with spline distance 241630/245030 and speed 0 under the card 320 hold, join check 13 of 14.

- 2026-11-15 card 373 opened (run `c374a57`, b57): a client's death-screen Exit Browses the frontend with `FromGame`
  and the card 045 follow pulled the player back in 5710 ms. Fix written by the senior in `restart.cpp`
  `c45_client_browse` (FromGame treated like FromLevelQuit, row `card373: client ... no reconnect`); waits for b58 and
  the `c374-client-exit-rejoin` rerun with a driver rejoin step through `c52_join`. Lane 1 play build 6 lacks 364-373.
  Next card number 374.

- 2026-11-15 card 373 accepted on b58 `30caa5dfea45b55b` (run `c374b58`, predictions all hit). Lane 2 is b58. Test
  rejoin step for scenarios: `client travel join` (calls `c52_join`, the lobby join path; client out of a session
  only). Card 218: three runs put the cause in the vanilla load path (host relative offset wrong after an in-place
  load), not sync; no fix planned before the playtest. Next card number 374.

- 2026-11-15 b59 `c0c1e733ab2bb380` (lane 2): b58 plus the card130 false-WARN fix (fx watch rows dropped at a
  respawn in place; `fx_watch_map(why)`). Runs `c371b59` (0 "component gone", row `5 checks dropped at the respawn in
  place`, card 371 rows as in `c371b57`) and `c346b59` (lobby play route, drive rows equal to the b34 baseline, 0
  follow rows, digest 0 mismatches, FATAL/ERROR 0, WARN class diff empty on the host). b59 is the candidate for play
  build 7 (cards 364 to 373 over play build 6); lane 1 stays at play build 6 until the user says so.

- 2026-11-15 run `c375a59` (b59): client rejoin while the host is on its death screen. Passed; host LoadLastSave
  recovers both, second Exit and rejoin work. Card 374 opened (low, parked): the late copy stays Up on the host while
  the client shows the death screen. Watch item: `WARN card322 ... no relay of it on this map` is a late-join tally
  artifact on a client that joined after the radio played. Next card number 375.

- 2026-11-15 10:50: card 374 accepted on b60 `a48aa4475e889bf7` (b59 + `c374_late_copies`), run `c375b60` (90 s). This replaces the earlier "low, parked" note for card 374. Lane 2 is b60; b60 is now the candidate for play build 7. Lane 1 stays play build 6 `aebfe1230e491c6d` (lacks cards 364-374) until the user says to cut. Next card number 375.

2026-11-15 11:00: card 375 opened (`docs/cards/done/375-hangar-load-with-client.md`): the host's load from the hangar
with a client in the session (persistent lobby) was never run or traced; Reload is sent with no pong gate and may
make the client rejoin during the title-screen hold (card 355 then returns the host to the menu). Probe run c377
(`tools/scenarios/c377-hangar-load.txt`) goes to the junior after c376. Next card number 376.

2026-11-15 11:05: run c376b60 (b60): pause-menu RESTART CHECKPOINT with both alive passed for the host (phase A:
travel row 1 of 1, card 371 Reload `none`, client reconnected try 1 in 3596 ms, restart done 10834 ms) and for the
client (phase B: load skipped, menu closed, no drop). This closes the "owed one soak proof" note for the pause-menu
restart. Phase C failed: card 376 opened (`docs/cards/done/376-client-restart-mission-drop.md`), a client's refused
RESTART MISSION Browse dropped it and the follow rejoined it in 9333 ms. Fix written in `restart.cpp` (refusal
returns success in session); build b61, then rerun phases C and D, then probe c377 (card 375). Next card number 377.

- 2026-11-15 11:10, run `c376b61` (b61 `bd0f098555615ca2`): card 376 part 1 proven (client RESTART MISSION refused
  with Success, no drop, no reconnect, same pawn). Second defect found: the pause menu's loading screen stays up on
  the client. Part 2 (remove it at the refusal) is in source for b62; phases C and D rerun and the c377 probe (card
  375) assigned to the junior on b62. Play build 7 candidate is b62 once accepted.

- 2026-11-15 11:35, card 376 accepted on b63 `e7cc13d4e1756cc2` (run `c376b63`): the client's RESTART MISSION is
  not run in the host's session; host RESTART MISSION seamless with the client following. The b62 widget removal
  missed (`c376b62`) and is gone. b63 is the play build 7 candidate; lane 1 stays at play build 6
  `aebfe1230e491c6d` until the user says to cut. Card 375: `c377b62` void by scenario order (host was in the
  dropship behind the hold, not pawnless); reordered probe rerun on b63 assigned.

2026-11-15 11:40, card 375 root cause and fix (b64 building). Run `c377b63` confirmed the defect: after a hangar
CONTINUE / LOAD GAME with a client the host sat in a parked dropship, save never applied. Cause: the deferred save
load runs only from `USTGameInstance::LoadComplete` (exe+0x50F2970), which a seamless travel never calls. Fix in
b64: host calls it at the seamless return when the deferred flag (gi+0x309) is set; `c143_after_load` sends Reload
only to ponged copies. Rerun `c377b64` assigned (batch 2, run 2 of 3). Card and SYNC_AUDIT rows updated.

Watch item from the sibling sweep (low, not carded): the other branch of `LoadComplete` calls
`USTDecalManager::PreallocateNewDecalList(false)` (exe+0x51351F0: `USTDecalPooler::PreAllocate` releases the old
lists, stores the new world at pooler+0x58, creates the cvar count of components). A seamless travel skips it on
both sides, so the pool keeps the previous world's entries. No crash seen: `b49` relayed 202 gore decals after a
seamless travel, and `Get`/`ReleaseAll` skip null and garbage-flagged entries. Not proven: that host gore decals
are visible after a seamless travel (needs one host shot of gore in such a run). Fix if not: call `LoadComplete`
at every seamless return on both sides, as `UEngine::LoadMap` does.

2026-11-15 11:58, card 375 accepted (run `c377b65`, b65 `3676f5d1b88c01de`): hangar save load with a client works; b65 is the play build 7 candidate (lane 1 still play build 6 `aebfe1230e491c6d`, waits for the user's word). Open, low: client saved state not restored on a hangar load; host pause-menu LOAD GAME to another map with a client never run (same code path).

2026-11-15 12:05, c378 predictions (`tools/scenarios/c378-mission-load-other-map.txt`, b65, host loads a FED_C1M1 save from the next map with a client; never run): (1) `travel checkpoint` stores deferred params and the Browse to FED_C1M1 `?FromSave` becomes a card 346 seamless travel, 0 client reconnects; (2) `card375 ... (flag 1 -> 0), host PC was at list slot 0` (both PCs keep `BP_PlayerController_C`, no swap, list stays [host, client]); (3) host stands at (6554, 11459, 97); (4) unknown, the point of the run: the client's kept PC gets its saved pawn at (6254, 11459, 97) and keeps it, or card 307 replaces it; either way client own pawn Up and Clock rows handled; (5) 0 FATAL/ERROR, digest mismatches only the card 218 class. If the newest save belongs to the next map, the load is in place and the run proves nothing.

2026-11-15 12:20, run `c378b65` proved nothing (the next map's first checkpoint saved 6.6 s after the travel, so the load ran in place on FED_C2M1; in-place rows as designed, client reconnected once, 0 FATAL/ERROR). Not chased further (scenario chasing rule): a host load of another map's save from inside a mission with a client stays unproven, low; it shares the proven card 375 path, minus the PC swap.

2026-11-15 12:20, c379 predictions (`tools/scenarios/c379-solo-load-coop-save.txt`, b65, host alone loads a save taken with a client; never run): (1) the load is in place, host stands at (6554, 11459, 97); (2) two `card048 LoadOwnerVariables_Pawn` rows; the row saved at (6254, 11459, 97) has no `PossessedBy` row after it (no PC at list index 1); (3) that pawn is not destroyed afterwards and shows in shot `h-orphan` as a standing trooper: a defect to card (fix sketch: after a load the host destroys saved player pawns left with no controller). If the pawn is absent, the engine or card 307/098 already cleans it and there is nothing to fix.

- **Run `c379b65` result (2026-11-15):** prediction confirmed. Host alone loading a save taken with a client keeps the
  client's saved pawn at (6254, 11459, 97): no `PossessedBy`, armed, never destroyed. Carded as **card 380**
  (`docs/cards/done/380-orphan-saved-player-pawn.md`); fix in `death.cpp` `detour_load_owner_pawn` (senior, syntax-checked),
  waits for b66 and `c379b66`. Run otherwise clean (in-place load, 0 FATAL/ERROR, no new WARN class).

- **Card 381 (2026-11-15, static audit):** a client's own hangar-menu level start would run `TransitionToLevel` /
  `TransitionToLatestLevel` (loading screen, then the card 154 refusal: stuck "LOADING...", card 376 sibling). Fix
  written in `restart.cpp` (both functions not run on a client in the host's session), syntax-checked; goes into the
  build after b66. Predictions in the card.

- **Card 382 (2026-11-15, static audit):** a save taken between the host's and the client's PC swap after a seamless
  travel stores the host's pawn with PC list index 1 (0.9 s margin in `c378b65`). Fix written in `save_c121.cpp`
  (host PC to slot 0 before every save), syntax-checked; goes into the build after b66 with card 381.

- **Run `c379b66` result (2026-11-15, b66 `4d6b30d4225de8b1`):** card 380 fix did not fire: the checkpoint load's
  archive has no fast-path buffer, the possessed-flag peek was unreadable (1 `WARN card380`, orphan still in the shot).
  No crash, 0 FATAL/ERROR. Fix 2 written (peek through the archive's Tell/Serialize/Seek); b67 carries it with cards
  381 and 382. Next: `c379b67`, then one hangar run for card 381.

- **Card 380 accepted (2026-11-15, run `c379b67`, b67 `cc1d4251720f389d`):** orphan saved pawn destroyed (1 `card380`
  row, 0 WARN), host pawn intact, 0 FATAL/ERROR. b67 also carries cards 381 and 382 (static fixes, not proven by a
  run; 0 `card382` rows in `c379b67` as predicted). Card 381 has no driveable click path (hangar computer widgets are
  outside `ui click`); no test hook added (scenario-chasing cap): it stays "static, unproven", to watch in the
  playtest. Next run: `c377` on b67 (card 380 prediction 4, card 382 window save if it fits). b67 is the play build 7
  candidate once `c377` passes; lane 1 is still play build 6 and waits for the user's word.

- **Run `c377b67` (2026-11-15, b67 `cc1d4251720f389d`): pass, same as `c377b65`** (hangar load with a client: host
  at the saved stand, client beside it, 0 reconnects, 1 digest mismatch of the card 218 class, 0 FATAL/ERROR, same
  WARN classes; 0 `card380` / `card381` / `card382` rows). Card 380 prediction 4 met. Cards 381 and 382 stay static,
  unproven by a run (382's window cannot be hit without harness work). **b67 is the play build 7 candidate**; lane 1
  is still play build 6 `aebfe1230e491c6d` and waits for the user's word.

2026-11-15 12:40, run `c376b67` (b67 `cc1d4251720f389d`, pause-menu scenario, phases C and D only): pass, same as
`c376b63`. Phase C: 2 `card376` not-run rows on the client, 0 `WARN card381` (the card 376 filter drops the click
first). Phase D: host RESTART MISSION by real click went through `detour_c381_transition` with no row and the
seamless travel ran (hold-off 6028 ms); 0 reconnects, digest 1494/1494 with 0 mismatches, 0 FATAL/ERROR, 0
`card380`/`card382` rows, no new WARN class. b67 batch: 3 of 3 runs used, all passed. b67 is the play build 7
candidate; lane 1 stays at play build 6 until the user says so.

2026-11-15, run `c383b67` (b67, `tools/scenarios/c383-restart-checkpoint-client-down.txt`, audit proof, no build):
pass. Client Downed, host Up, host pause-menu RESTART CHECKPOINT: in-place load, card371 Reload `none`, 1 reconnect
(3567 ms), host row "card043: host entry 122767 (Downed) left: pawn gone", 0 host "respawn in place" and 0 "-> Dead"
rows, client Up on a new pawn and walking, a second `down client` works on the new pawn, digest 1495/1495 with 1
mismatch (STAISpawner_5, card 218), 0 FATAL/ERROR, no new WARN class. SYNC_AUDIT player state row updated.

Run `c276b67` (b67, 2026-11-15 12:44): pass. Card 276 (client fire presses on the host copy): 20 taps and a 9 s hold
through a reload on the Morita Mk1, 86 client shots matched 86 host copy shots, 0 dropped, 0 `found no round`, claim
offset constant. The same-tick press with the ability still active was not reproduced; card 276 is a playtest watch
item (`found no round` rows in the host log). Card 239's spawn-window guard is coded in `weapons.cpp` for b68 (not built).

- 2026-11-15 card 384 (new, senior): a client pickup credited ammunition twice (run `c276b67`: one MoritaSmall
  of 120 took the client 0 to 240; local subclass credit plus the host's `Ammo` frame). Fix coded in `ammo.cpp`
  (`c384_pair`: the later credit of one take is reduced by the earlier one, 2000 ms window) for b68 together with
  the card 239 guard. Not built, not proven; scenario `tools/scenarios/c384-one-credit.txt`, run `c384b68`.

- 2026-11-15 run `c384b68` (b68 `efdda02128e72fdc` = b67 + card 384 + card 239 guard): card 384 proven and
  accepted (client Morita 0 to 120, host copy 0 to 120, frame `+120 (card 384: 120 already credited locally)`,
  FATAL/ERROR 0). Card 239 guard is in b68, unproven. b68 is the newest lane 2 build; play build 7 candidate is
  now b68 (1 run) over b67 (6 runs); lane 1 stays at play build 6 until the user says.

- **Card 239 guard proof not reached (2026-11-15, b68).** Three runs (c239b68, r2 void at the start; r3 reached the
  kill but the crate dropped nothing, 0 `world P` rows). No more runs; watch rows are in the card. Harness lesson for a
  direct FED_C2M1 start: the client's start screen exists only after the host's continue (join replay IntroStart), so a
  scenario waits for `card135: client t=N screen prompt ready`, then `client ui continue`, then the pawn waits;
  without the client's confirm card 344 returns the host to the menu after 60 s (worked as designed, twice).
  Accepted cards with a CARDLOG row (30 files) moved to `docs/cards/done/`.
  Side evidence from c239b68r3 (direct FED_C2M1 join, not from a save): card 219's rows read `165 of 165 calls, 165
  collision rows (peak 165 of 2048), 0 collision rows dropped` (host 408400715); client digest round 1 `mismatches 0`;
  FATAL/ERROR 0 both sides; WARN only attach and 3 card152 bone checks. Card 219's from-save prediction is still open.
  Card 256 rows from the same run: 21 death drops, 21 host and 21 client rest rows, client `moved` 0 to 10 uu, 0 pickup
  mismatches; evidence in the card, closes on the playtest logs.

- 2026-11-15, crate kill with no host drops (c239b68r3), traced from logs: not a defect. The drop roll is per kill
  and can be empty (old run c131k2, crate 06, also none). The host's roll is the only one applied; card 105 blocks the
  client's. Drops that fall through the floor on the host (3 of 4 in r3, 2 of 7 in c131k2, destroyed near z -7850)
  are destroyed on the client too; the mod writes no physics on host pickups. Detail in card 239.

- 2026-11-15, playtest WARN diff baseline: `logs/b49-warn-classes.txt` holds the WARN classes of run b49 (play build 6
  `aebfe1230e491c6d`), host 7 classes, client 9. Made per side with
  `grep WARN <log> | sed -E 's/^\[[^]]*\]\[[^]]*\] //; s/ t=[0-9]+//g; s/0x[0-9a-fA-F]+/G/g; s/([^d0-9])[0-9]+(\.[0-9]+)?/\1N/g' | cut -c1-90 | sort | uniq -c | sort -rn`.
  Run the same line on the playtest logs; a class not in the baseline is read first.
  Known false WARN outside the baseline: client `card156 ... back to Up (map load) +5 s ... bShowMouseCursor 1` in the
  hangar (3D_Frontend shows the cursor on purpose; `c346b59`, `c346b68`). The +5 s check in `death.cpp`
  `c156_client_check` does not exclude the hangar; fix with the next lane 2 build that has another reason to exist.
- 2026-11-15, static read of the player-count scale (cards 111 to 113, `bug_scale.cpp`) and the host pause skip
  (cards 096, 120, 320, `pause_c96.cpp`): no defect found. Attacked: count across a seamless travel (held 45 s, log
  `eff-verify-c2m1` shows `session players 2 held`), count after a session ends (a Standalone world resets it),
  pause with a client not yet ponged, client leaving while the host menu is open, unpause during the card 320 hold.
  One low residue, not carded: a host who starts a mission alone within 45 s of the client leaving gets the start
  waves at the 2-player scale (1.6x).

- 2026-11-15 13:17, run `c346b68` (b68 `efdda02128e72fdc`, `tools/scenarios/c346-persistent.txt` repinned to b68,
  run by the senior): pass in 86 s, 55 steps, same as `c346b59`. Level end, seamless travel to the hangar, seamless
  travel to FED_C2M1; client `joins 0, rejoins 0, mismatches 0`, 0 `session dropped`, 0 `reconnected`; host Logout
  rows 3 (launch and quit, as in b59); FATAL/ERROR 0; WARN classes equal to `c346b59` (the client `card156 ... back
  to Up (map load) +5 s` row in the hangar is in both). b68 now has 2 passing runs and the clean c239b68r3 logs; it
  stays a play build 7 candidate beside b67. Lane 1 unchanged (play build 6), waits for the user's word.

Unbuilt source edit (2026-11-15, senior): card 156 hangar false WARN. `C52State.menu_map` (set in `c52_new_world`, any net mode) makes `c156_client_check` log a plain `not checked on the menu map` row instead of the WARN. In `hooks_internal.h`, `lobby_ui.cpp`, `death.cpp`; not compiled yet, goes in the next lane 2 build. Proof: `c346-persistent` client log has 0 `WARN card156` and 1 `not checked on the menu map` row.

Static read of card 114 sync (`difficulty.cpp`, 2026-11-15): no defect. Attacked: client file edits surviving a host with none ("B" clears every want, apply then writes the game default back), seamless travel (wants and `from_host` persist, objects loaded anew are refreshed), return to solo (file restored on the first Standalone world), a second host without a solo world between ("B" clears). One gap closed in source, unbuilt: the client now logs WARN when it applied fewer values than the host's "E" count (host stops silently at 64 frames).

2026-11-15 13:25, `c374b68` predictions (b68, `c374-client-exit-rejoin`, regression of the path card 380 changed, not run since b58): as `c374b58` (card373 no-reconnect row once, client stays on 3D_Frontend, host respawn in place, rejoin by `client travel join`, card307 spawn beside the host, digest 0 mismatches apart from card 218's spawner), plus the host's lone RESTART CHECKPOINT destroys the client's saved pawn (one `card380` row) and the rejoined client gets exactly one pawn. FATAL/ERROR 0, no new WARN class.

Run `c374b68` (b68, 2026-11-15 13:22, 90 s): pass, same as `c374b58`. Client `card373 ... no reconnect` once, 0 `session dropped`/`reconnected`; host respawn in place at 409316368; rejoin `card374 travel join` at 409332119, one `card307` spawn beside the host at 409335367; digest round 2: 1494/1494 actors, 1 mismatch (card 218's STAISpawner, as b58); FATAL/ERROR 0; WARN classes equal to b58. Prediction miss, harmless: 0 `card380` rows, because this in-place load restored only the host's pawn (1 `LoadAbilitySystemComponent`), so the orphan path did not run; it stays proven by `c379b67` only. b68: 3 passing runs.

2026-11-15 13:24, `c375b68` predictions (b68, `c375-rejoin-host-dead`, not run since b60; LoadLastSave with a rejoined client is the path where card 380's orphan destroy could wrongly take the client's pawn): as `c375b60`; 0 `card380` rows (the rejoined PC sits at the saved list index); LoadLastSave brings both Up, the client possesses its pawn; FATAL/ERROR 0; no new WARN class.

Run `c375b68` (b68, 2026-11-15 13:24, 88 s): pass, predictions hit. 0 `card380` rows; two `card374 ... joined after the all-down: Out` rows (409452118, 409464752); LoadLastSave: `restart: slot 0 Dead -> Up` and `slot 1 Out -> Up` at 409473159, client own pawn Health 1.0 -> 100.0 at 409476579; digest round 2 1494/1494, 1 mismatch (card 218 spawner); FATAL/ERROR 0; host WARN diff vs `c375b60` empty; client diff = the known card 322 `own start skipped x1, no relay` map-end rows (4, the route's rejoins, noted in card 374). b68: 4 passing runs.

2026-11-15 13:27, `c355b68` predictions (b68, `c355-client-leaves-hold`, last run on b40, before the card 364 hold rewrite and card 346 seamless travel): the travel to FED_C2M1 carries the client (card346 rows, 0 reconnect); the hold waits for slot 1; after the host's continue and the client's quit at its prompt: `card320 ... left during the hold (waited for), clients 0`, `WARN card355 ... host returns to the main menu`, `card350 ... return to the menu`, host world 3D_Frontend, shot `c355-host-menu` shows the main menu. FATAL/ERROR 0.

Run `c355b68` (b68, 2026-11-15 13:26, 72 s): pass, predictions hit. Seamless travel to FED_C2M1 at 409621562 (client 0 `session dropped`/`reconnected`); `card320 ... left during the hold (waited for), clients 0` at 409625688; `WARN card355 ... host returns to the main menu` at 409625689; `card350 ... return to the menu` same ms; hold off after 4915 ms; host world 3D_Frontend at 409626591. Senior viewed `host-c355b68-c355-host-menu.png`: hangar, panel text "The other player left or did not finish loading: mission not started". FATAL/ERROR 0; host WARN classes: card355 (the rule's own row), card346 guard, card132 round cut by the map change, config, attach. Card 355 holds after the card 364 rewrite and card 346. b68: 5 passing runs.

2026-11-15 13:29, `c369b68` predictions (b68, `c369-host-quit`, last run b55): as `c369b55`: the host's pause QUIT is a seamless travel to 3D_Frontend, the client follows (seamless 1) with 0 NetworkFailure, 0 `session dropped`, 0 `reconnected`; host 0 `card140: host Logout` before the client's quit; the second travel to FED_C1M1 is seamless too; FATAL/ERROR 0; no new WARN class apart from the known client hangar `card156` false WARN.

Result `c369b68` (2026-11-15 13:31, b68 `efdda02128e72fdc`, 143 s, drive rc 0, scenario passed): predictions held, same as `c369b55`. Host pause QUIT is a seamless travel to 3D_Frontend at 409779411 (`Browse not run`, 2 players), client follows with 0 `NetworkFailure`, 0 `session dropped`, 0 `reconnected`, summary rows `joins 0, rejoins 0, mismatches 0`; second travel to FED_C1M1 at 409788374 seamless. Host `card140: host Logout` rows: 1 at 409739487 (the host's own listen Browse out of the first hangar, also in b55) and 2 at session end; none between. FATAL/ERROR 0 both sides; WARN class diff against `c369b55` empty on both sides.

Predictions `c345b68` (2026-11-15 13:36, `c345-level-end.txt` repinned to b68 `efdda02128e72fdc`, last run on `312bc9f472523c84`): as the first run. The client's own level-end Next is held (`card345b: ... own level-end Next not run: waiting for the host`, waiting line shown), the client stays in the session (Clock rows after it); the host's Next runs the client's handler, the travel to 3D_Frontend is seamless, client world row `net mode 3`, `card350 ... FromLevelComplete ... : 1`; 0 `session dropped`, 0 `reconnected`, 0 NetworkFailure on the client; FATAL/ERROR 0; no WARN class beyond the known client hangar `card156` false WARN.

Result `c345b68` (2026-11-15 13:35, 85 s, drive rc 0, scenario passed): predictions held. Client `own level-end Next not run: waiting for the host` at 410038772, waiting line at 410038774; host Next relayed at 410058996, client handler run at 410061039; host seamless travel to 3D_Frontend at 410061040, client `ClientTravel ... (seamless 1)`, `HasOption("", FromLevelComplete) ... : 1`; client 0 `session dropped`, 0 `reconnected`, 0 NetworkFailure; FATAL/ERROR 0. WARN: host classes as the first `c345-level-end` run (config, attach, card346 null camera manager guard, test-only `card130 ... WB_Cutscenes_C still the top widget after 3 calls` on the level-end cutscene); the client now has the same `card130` row once (0 in the first run; test auto-skip only, the play build skips nothing by itself).

Static read 2026-11-15 (`jump_c94.cpp`, `spawn_c169.cpp`, `pawn_collision.cpp`, `player_marker.cpp`), unbuilt edits:
- `jump_c94.cpp` `detour_c101_lerp_tick` (always installed): `pawn_state` read Character fields (`CharacterMovement`, movement mode) of the task's actor before the player-class test, on every lerp tick of any actor. Now only a player pawn is read, and only with `UBWCOOP_C94_LOG=1`.
- `jump_c94.cpp` `detour_c94_gameplay_event` (always installed for the card 104 policy): the tag name lookup ran on every gameplay event; now only with the log flag.
- `player_marker.cpp` `c110_step`: the world change was tested after the Standalone return and by address only. Host path at risk: mission (world A), menu in Standalone (not seen), next listen map allocated at A again: `built` stays true with a dead widget and no marker is shown for the whole map. Never seen in a log (every run so far passed through a seen world). Now the world compare runs in every net mode and a built overlay with a dead widget is rebuilt.
- `spawn_c169.cpp`, `pawn_collision.cpp`: no defect found (attacked: null avatar at destroy, held table full, map change with held entries, the per-move hot path, the input key detour cost).
- Observed, not a defect: the client builds one overlay in the `/Temp/Untitled` transition world (the old pawn's weak pointer is still live for up to 1 s); the next world removes it.

Predictions `c344b68` (2026-11-15 13:40, `c344-no-confirm.txt` repinned to b68, last run on `312bc9f472523c84`; client `UBWCOOP_C344_NOCONFIRM=1`): as the first run. Travel to FED_C2M1 seamless; the hold waits for the client; 60 s after the host's continue the host goes to the main menu by the QUIT path and the client follows (seamless, `net mode 3` in 3D_Frontend, host's hangar shows the card 355 "mission not started" line); client 0 `session dropped`, 0 `reconnected`; FATAL/ERROR 0; no WARN class beyond config, attach, card346 guard, card130 test skip, client hangar `card156` false WARN.

Result `c344b68` (13:36–13:42, b68, drive rc 1): the mod did what was predicted; the scenario failed on its own stale
wait at line 32 (card 364 changed the row to `did not confirm (0 not loaded, card 364) within 60000 ms`). Host continue
410203123, WARN card344 at 410263124 (60001 ms), card350 OpenLevel `FromLevelQuit`, card346 seamless travel, host
3D_Frontend net mode 2 at 410264287, client net mode 3 at 410264205, client `HasOption("", FromLevelQuit) ... : 1`.
FATAL/ERROR 0 both sides. Scenario line 32 fixed; rerun `c344b68r2` for the shots.

Finding from `c344b68` (card 159, fix written, unbuilt): the client logged 3 `WARN card159 ... check` rows and
`join check 22 of 25 match`. Cause: the host's check round goes kDsCheckMs (5000 ms) after the join round whatever the
title-screen hold does. Both worlds are paused in the hold; the client's ships had ticked one frame before its pause
(spline distance 102/0 and 323097/323199 against tolerance 100 at speed 0), and no L round corrects that before the
release. With auto-confirm the hold ends in about 2 s, so the check lands in flight and passes (c346b68: 25 of 25). A
human who takes more than about 5 s to press continue gets these rows in tonight's logs: on play build 6 they are
benign (the L rounds after the release resync, as c346b68 shows). Fix in `dropship.cpp` `c159_dropship_step`: while
`c320_holding()` the due time moves to now + kDsCheckMs, so the check follows the release.

Predictions `c344b68r2` (same b68, scenario fix only): scenario passes all 31 steps, shots `c344-host-menu` and
`c344-client-menu` show the main menu on both sides; the same WARN classes as run 1, including the 3 client card159
check rows (fix not built).

Unbuilt edits added 2026-11-15 afternoon (join the b69 batch): `player_c99.cpp` `detour_c99_get_pc` returns the
original on a client before the context walk (a client has no server copy and no enemy remap);
`detour_c102_overlap` passes straight through on a client; `script_c310.cpp` `c310_client_step` logs WARN when fewer
kept rows applied than were kept; `dropship.cpp` check round waits out the hold (above). Read with no defect found:
`marker_c166.cpp` (map bits, parse bounds, null pawn or NPC attacked), `cine_skip.cpp`, the rest of `script_c310.cpp`
(journal init, poll, join replay), the per-tick functions of `attributes.cpp`.

Result `c344b68r2` (2026-11-15, b68 `efdda02128e72fdc`, `c344-no-confirm.txt` with the card 364 wait text): scenario
passed in 132 s, drive rc 0. Host 7690 lines, FATAL/ERROR 0, WARN 5 (config, card346 guard x2, card344 no-confirm,
attach). Client 6866 lines, FATAL/ERROR 0, WARN 8 (card322 x2, card159 check rows x4 + totals x1 as predicted, attach).
Both shots show the hangar main menu. No crash dumps.

Static audit 2026-11-15 afternoon, clean verdicts: `sprint.cpp` (per-CMC cost is 1 pointer read + 2 role bytes; the
diag detours install only under `g_gate_diag`; `c22_step` walks level actors once per 1 s, already in the card 329
table), `lobby_ui.cpp` (`c52_step` returns at `!g_c52.at_frontend` on mission maps; `upnp::poll()` only at the
frontend). Card 330 rows of c346b68 read: host 2.48% of frame time, client 3.64%, top steps all already in cards 330,
367, 368. No new finding.

Predictions `playb68` (b68, `tools/scenarios/play-mode-start.txt`, `SKIP_MOVIES=0`, no FAST, GOD=1): the path the
playtest takes. (1) Scenario passes. (2) Host logs `hold off after N ms (all confirmed)` with N about 20000 to 27000
on FED_C1M1 and FED_C2M1. (3) Client logs `WARN card159` check rows on both mission maps (hold check timing, fix
unbuilt). (4) Host logs `WARN config: UBWCOOP_TEST_SKIP not "1"`; neither side logs `card130 ... still the top widget`.
(5) FATAL/ERROR 0 both sides; client 0 `session dropped` / `NetworkFailure`. (6) Shots: both title screens, both on
foot on both maps, both in the hangar. Open point: whether the level-end cutscene needs a key in play mode (scenario
presses Enter on the host after 40 s if the hangar has not loaded).

Result `playb68` (13:47, 102 s, drive rc 1): scenario fault, no mod fault. In play mode the mission intro video
(`WB_Cutscenes_C`, stacked at 410879618, 465 ms after the host's `screen prompt ready`) covers the title screen on each
side, so the host's Enter went to `WB_Cutscenes_C.OnKeyDown` and no `host continued` row followed within 15 s. Host and
client videos started 11 s apart (the client joined later). FATAL/ERROR 0 both sides, WARN 2 each (attach, config).
Predictions `playb68r2` (same build, scenario presses Enter up to 5 times per screen until the screen continues):
as `playb68` items 1 to 6; plus each side needs 2 or 3 presses on FED_C1M1 (prompt, skip, continue).

Result `playb68r2` (13:50, 85 s, drive rc 1): same scenario fault at line 31. The first posted Enter reached
`WB_Cutscenes_C.OnKeyDown` (411074796) and its graph and one animation ran (411075803, 411076318), but the video kept
playing through four more presses 4.4 s apart (fail shot shows the video). FATAL/ERROR 0. Two same failures, so the
method changes: `playb68r3` closes the brief with `ui continue` (the Blueprint functions behind the real key, card 067)
on each side, each side in its own step; the mod's play path (no automatic skip, hold until both confirm) is unchanged.
Open, low: whether a posted 165 ms Enter differs from a real key on the video (humans passed this screen in earlier
playtests). Predictions `playb68r3`: as `playb68` items 1 to 6.

Static audit 2026-11-15 afternoon, clean (attacks named): `attributes.cpp` 955 to 1320 (per-shot cost, held-use
dedupe reset at map end and respawn, stale relay id), `hold.cpp` (server copy's own completion, range and CanInteract
check of a client HoldDone, double hold on one site, table bounds), `fallzone_c338.cpp` (acting scope, non-own overlap,
null pawn), `radio_c322.cpp` (own start skipped without a relay; edge only: a list with a null picked element is not
relayed), `grenade_c158.cpp` (hot path is 8 pointer compares; function table kept across maps, a stale match can only
log a false WARN), `player0_c321.cpp` (host in the door volume wins, roster bounds), `render_c337.cpp` (every exit
uncovers; step runs in the play build; `playb68r2` client: off 4173 ms, on at the title screen), `remote_body.cpp` hot
paths only (100 ms step on the shared player list, frame-size detour).
Two findings, both edited, unbuilt (join the b69 batch, 10 groups now):
- `insta_c361.cpp` card 362: a caller that repeats each tick (CrushActor while the client's pawn stays in a moving
  zone) added one skip row per tick; with the table of 4 full, the uncapped `WARN card362 ... table full` row could
  print every frame. Fix: one waiting row per pawn.
- `world_c132.cpp`: the log-only inventory walks every actor at map load, +90 s (in play) and map end
  (30291 actors, 143 class rows in `c239b68r3`); its cost was never measured. The summary row now ends `pass <n> ms`
  (card 330 class: decide from the playtest number whether the +90 s pass stays in the play build).

**Run `playb68r3` (b68 `efdda02128e72fdc`, play-mode-start, SKIP_MOVIES=0, GOD=1, 2026-11-15 14:00): scenario passed**, 395.1 s, 0 FATAL/ERROR both sides. C1M1 title hold off after 36478 ms (all confirmed), real flight, `travel end`, level-end screen, Next relayed, level-end video played to its end (posted Enter does not skip it: test-only), hangar, seamless travel to FED_C2M1, hold off after 33706 ms (all confirmed), both on foot on C2M1 (shots viewed: objectives HUD on host, troopers and name tag on client). WARN classes: only known benign ones (host card346 null camera guard x2; client card159 title-hold check rows, card156 false WARN x1; fixes for both are in the unbuilt batch).

**Finding (level-end cutscene 2 s offset, fix written, unbuilt):** host `card345b ... relayed to 1` t=411321511; host `WB_Cutscenes_C` OnInitialized 411321513, `On Media Opened` 411323527 (2014 ms game-thread block on the video load); the chan frame left only with the tick's flush after the block, so the client row `host's level-end Next arrived` is at 411323545 and the client `On Media Opened` at 411325537: client video 2010 ms behind the host. Breaks "the client watches the same cutscene as the host". Fix: `net_flush_pc` (`fire.cpp`) called per client in `c345b_host_relay` (`objectives.cpp`); row now ends `relayed to N, flushed N`. Prediction for the b69 run: client arrival within about 50 ms of the host relay row, `On Media Opened` delta under about 100 ms.

**Efficiency findings (card 330 class, static, 2026-11-15):**
- `ledger_c132.cpp`: `c132l_decode` (frame text copy + snprintf) ran for every sent frame although only the capped log row reads its output. Fixed (unbuilt): decode on the send side only when the line cap lets the row print. The receive side keeps it (`c132l_note_key` needs `r.key`). The 4096-row `ring` is written and never read (only its count is logged); left alone, an 88-byte copy per frame.
- `net_budget.cpp`: the host runs, in the play build with no flag, per replicated actor channel `net_add` (linear scan, up to 192 classes) and for NPCs `net_note_npc` (linear scan, up to 256 rows) plus `c140_pace_step`/`net_npc_moved_step` per tick (up to 256 weak gets + atan2). Cost not measured; measure from card329 rows after the playtest before changing anything.
- `stall_c303.cpp`: play build never starts the watchdog thread; per tick cost is a few interlocked ops. Low defect: line 215 reads `class_name(pawn)` with `pawn` possibly null. `attack_pace.cpp`: all behind `UBWCOOP_C122_LOG=1`, nothing in play.

**b69 run plan and predictions (written before the build, 2026-11-15 14:05; 3-run budget):**
1. `play-mode-start` (SKIP_MOVIES=0): host row `card345b ... relayed to 1, flushed 1`; client `host's level-end Next arrived` within about 50 ms of it; `On Media Opened` host/client delta under about 100 ms (b68: 2010 ms); client 0 `WARN card159` and 0 `WARN card156`. If `flushed 0` or the delta stays near 2 s, the chan pace gate queued the frame: next method is to send before the handler runs or delay the host's own Next by one tick.
2. `c346-persistent`: client 0 `WARN card156`; pass as on b68.
3. `c344-no-confirm`: client 0 `WARN card159`; host to main menu as on b68.
`flags.cpp` audit: every flag is one environment read at attach; nothing per tick. Clean.

**b69 = `c1a60d5be3469055`** (lane 2 only, 12 senior fix groups, 0 warnings; junior review: one minor finding, card 362 no-Down-frame WARN repeats every 3 s for a per-tick caller; capped at 16 rows per map in `insta_c361.cpp`, unbuilt).
**Run `playb69` (play-mode-start, SKIP_MOVIES=0, 14:10): scenario passed**, 399.2 s, 0 FATAL/ERROR. Predictions held: host `card345b ... relayed to 1, flushed 1` t=411923365, client arrival t=411923369 (4 ms; b68 2034 ms); `On Media Opened` host 411925274, client 411925274 (0 ms; b68 2010 ms). Client 0 `WARN card159`, 0 `WARN card156` (b68: 5 and 1). Host WARN classes: card346 null camera guard x2 only. New `pass` figure: inventory pass 16 to 20 ms on FED_C2M1 (30297 to 31137 actors), once per 90 s.

**b69 runs 2 and 3 (14:12 to 14:16), both passed, predictions held.** `c346b69` (c346-persistent): 85.4 s, 0 FATAL/ERROR/CRASH; client WARN classes: attach only (0 `WARN card156`; b68 had 1); host: card346 guard x2, card130 top widget x1, config, attach. `c344b69` (c344-no-confirm): 141.6 s, 0 FATAL/ERROR/CRASH; host `WARN card344 ... 1 client(s) did not confirm (0 not loaded, card 364) within 60000 ms ... host returns to the main menu`; client 0 `WARN card159` (b68 had them), only card322 own start skipped x2, card132 digest cut, attach. b69 batch: 3 of 3 passed.
**Static audit, no defect found (2026-11-15 afternoon):** `playlog.cpp` (rate caps per class and global, tables cleared at map end; attacked: widget table stale pointers across maps, cleared by `pl_map`), `lobby.cpp` (ini save buffer 64 bytes against at most 39 written, address capped at 21 chars by `parse_address`), `upnp.cpp` (every recv bounded and NUL-terminated, URL and IPv4 parsers bounded, worker thread only), `crash_trace.cpp` (no heap, 4 block cap, no SEH probing in the mod that could use the blocks up; 0 CRASH rows in the b68 and b69 runs), `movieskip.cpp` (starts only with `UBWCOOP_TEST_SKIP`, 90 s ceiling), `galaxycfg.cpp` (startup only), `chan_pack.cpp` decode (attacked: varint overrun, decimal with 7 fraction digits, base64 1200 bytes into a 1604 char buffer, def length against the frame end, ref to an undefined id; all rejected as a bad frame), `remote_body.cpp`, `ue/log.cpp`, `frame_c330.cpp`, `fx_watch.cpp`.
**Per-frame mod cost (card330 rows, `playb68r3`, DIAG on):** host 1.5 to 3.5 % of frame time (top `c132_ext_step`, about 134 us mean), client 1.1 to 5.1 % (top `client_significance_step`, `c37_step`). No new card: card 330 covers it.

2026-11-15 14:20, b69 batch 2 predictions (regression of paths not run since b57/b58, which cards 374 to 384 and the b69 card156 exclusion touched): 1. `c373b69` (`c373-death-screen-buttons`): pass as `c373b57b`; RESTART MISSION and Exit travel seamlessly with the client, 0 `card344`/`card355` WARN, phase C client card156 +5 s row without WARN, 0 FATAL/ERROR/CRASH. 2. `c371b69` (`c371-restart-checkpoint`): pass as on b57; restart from checkpoint with both players Up afterwards. A fail here is a regression to trace statically before any rerun.

2026-11-15 14:27, b69 batch 2 results. `c371b69` passed (60.3 s, 0 FATAL/ERROR/CRASH, WARN classes all known). `c373b69`
void at line 128 (`host wait state pawn=host-own`, last `pawn=other`), phases A and B passed as on `c373b57b`. Not a b69
regression: `c373c57b` (b57) voided at the same line the same way; phase C has never passed, my prediction named the
wrong baseline. Cause, from the host log: on the hangar-launched FED_C3M2 `autodropship: action 1/8` pressed the host's
continue 2.2 s after `screen prompt ready`; the card 344/364 hold kept the world paused; the scenario waited on the host
pawn before the client's continue, the host PC's pawn is not a player character while the hold is on, so after 60000 ms
`WARN card344 ... did not confirm` sent the host to the main menu (mod behaved as designed). Scenario fixed: client
continues first, pawn waits after `hold off after`. Prediction for `c373b69b`: phase C passes; host `card320 ... hold off
after` inside 30 s of the client's continue; both pawns own after it; 0 `WARN card344`; client card156 +5 s `own pawn`
row; 0 FATAL/ERROR/CRASH. New test-tool WARN class, benign: `autodropship: pause requester[N] of N is N null, not
WB_Stack_C -- no action`.

Static audit 14:25, no defect: `ue/hooktable.cpp`, `ue/config.cpp`; uncached `prop_find_prop` callers traced to their
call rate: `c34_track_half_height` (per frame per remote pawn, field found at depth 0 in 1-2 compares), `c125_step`
(1 Hz per turret), `c73_step` (1 Hz), `c98_dead_now` (save load only), the rest log, event or once paths. No per-tick
deep walk found; no card.

2026-11-15 14:34, `c373b69b` (b69) passed, 142.4 s: phase C passed for the first time (host `card320 ... hold off
after 6496 ms (all confirmed)`, 0 `WARN card344`, client card156 +5 s `own pawn` row, 0 FATAL/ERROR on both).
Prediction held. b69 has 5 passing runs. WARN rows not in `logs/b49-warn-classes.txt`:
- client `WARN card313 ... Timeline ... age -2006 ms ... clock offset 2788 ms`: defect, card 385 (clock offset from a
  sample that waited out the map load; timeline about 0.8 s behind). Fix written by senior in `ledger_c132.cpp`,
  `sprint.cpp`, `hooks_internal.h`; syntax check passes; unbuilt. Goes into b70 with the card 362 WARN cap.
- host x2 `WARN card132: host hit BP_AI_HopperBug_C ... is 2051 uu from the NPC` (2008 uu the second): the known
  card 167 diagnostic class (`hit_c132.cpp:192`, first seen in hb47 at 4042 uu), capped per map; the event's point is
  not on the body on the host itself, so it is game behaviour, not a sync gap. No card.

2026-11-15 14:36, b69 batch 3 predictions (regression of the b68 passes, scenarios repinned to b69, no scenario
change): `c355b69` passes: client leaves during the card 344 hold, host logs the card 355 row and reaches the main
menu, 0 FATAL/ERROR/CRASH. `c369b69` passes: host quit, client back at its menu, 0 FATAL/ERROR/CRASH. Any WARN class
outside the known list gets a static look.

2026-11-15 14:40, b69 batch 3 results: `c355b69` passed (74.1 s; host `WARN card355 ... a waited-for client left
during the hold (host continued 1): host returns to the main menu`), `c369b69` passed (147.3 s). 0 FATAL/ERROR/CRASH
on both sides of both runs; WARN classes all in the known list. Predictions held. b69 has 7 passing runs (`playb69`,
`c346b69`, `c344b69`, `c371b69`, `c373b69b`, `c355b69`, `c369b69`).
Static audit 14:38, no defect: `mod/src/autoinput.cpp` (test script and test key events only; callers are
`testrun.cpp`, `autoplay.cpp`, the card 060 test in `weapons.cpp`), `mod/src/cmdfile.cpp` (opened only with a test
tag), rest of `ue/reflect.cpp`, `audit_npc.cpp` hot paths (`c37_note_npc` gated on `g_c37.audit`; `c293_step` and
`c319_step` measured by card 330 at 0.3 and 0.5 to 1.1 us mean per call in `c373b69b`).
b70 request sent to the junior: card 385 and the card 362 WARN cap, both senior-written and syntax-checked.

2026-11-15 14:42, b70 predictions (card 385 + card 362 cap, written before the build): `c373b70` passes as
`c373b69b` did, and its client log holds 0 `WARN card313 ... outside 0 to 2000` while the `card132: client clock`
round row after the second map start may still show a large per-PC value (that row is the round's own sample, no
longer the offset used). `playb70` (play-mode-start) and `c345b70` (level end) pass with 0 FATAL/ERROR/CRASH and WARN
classes in the known list. A fail of any of them keeps b69 as the play build 7 candidate.

2026-11-15 14:44, b70 = `6109d0898f23ed7d` (junior build, 77/77 steps, 0 warnings; lane 2 only). `c373b70` passed
(146.4 s), 0 FATAL/ERROR/CRASH on both sides, 0 `WARN card313`. The run does not prove card 385: no clock round in
it held a stalled sample (client rounds -12 to -21 ms), so the b69 code would have passed it too. Card 385 stays
"built, not proven"; it shows no regression. WARN rows: client `WARN cmd: 1 ui continue -> failed` is the scenario's
own `try client ui continue` at line 41 (test tool); client `WARN card191 ... frozen with bone` is in the b49
baseline; one host `card132: host hit BP_AI_HopperBug_C` far row (known class, see 14:34).

**2026-11-15 14:50 `playb70` result.** Passed, 391.2 s (`playb69` 399.2 s). 0 FATAL/ERROR/CRASH on both sides. WARN classes all in the known list (host: config, attach, 2 card346 null camera manager guard rows; client: config, attach). 0 `WARN card313`. Client clock rounds -3 to -28 ms, no stalled round, so card 385 stays "built in b70, not proven". b70 now has 2 passes (`c373b70`, `playb70`). Next: `c345b70`.

**2026-11-15 14:52 `c345b70` result, b70 batch 2 predictions.** `c345b70` passed, 84.9 s, 0 FATAL/ERROR/CRASH, WARN classes in the known list (config, attach, card346 guard, test-only card130 `WB_Cutscenes_C`). b70: 3 passes. Scenarios `c346`, `c344`, `c371`, `c355`, `c369` repinned to `6109d0898f23ed7d`. Predictions: `c346b70` and `c344b70` pass with 0 FATAL/ERROR/CRASH, WARN classes in the known list, `c346b70` client 0 NetworkFailure and 0 "session dropped"; a client clock round of 500 ms or more with 0 `WARN card313 ... outside` rows would prove card 385. A fail keeps b69 as the play build 7 candidate.

**2026-11-15 14:54 `c346b70` result.** Passed, 83.3 s (`c346b69` 85.4 s). 0 FATAL/ERROR/CRASH, WARN classes in the known list. Judge rows equal `c346b69` row for row: client 0 real NetworkFailure/"session dropped"/"reconnected" rows (the 2 grep hits are the hook install lines 456-457), host 3 `card140: host Logout: pace queue ... dropped with 0 frames` rows at the same log lines as b69 (1460, about 8197, about 8334). No stalled clock round. b70: 4 passes.

**2026-11-15 14:56 `c344b70` result, `c371b70` prediction.** `c344b70` passed, 127.5 s, 0 FATAL/ERROR/CRASH, WARN classes in the known list (host card344 "did not confirm" row expected; client 2 card322 "own start skipped" rows). No stalled clock round. b70: 5 passes (`c373b70`, `playb70`, `c345b70`, `c346b70`, `c344b70`). Prediction for `c371b70` (restart from checkpoint, `GOD=0`, `UBWCOOP_AUTODROPSHIP=1`): passes, 0 FATAL/ERROR/CRASH, 0 `WARN card313 ... outside` rows even if a client clock round shows `min recv - sent` of 500 ms or more after the restart (that pair would prove card 385).

**2026-11-15 14:57 `c371b70` result; card 385 second variant.** `c371b70` passed, 59.0 s, 0 FATAL/ERROR/CRASH, WARN classes in the known list (config, attach, card132 STAISpawner mismatch = card 218). b70: 6 passes. Clock rows after the restart: one round `offset -13 ms (min recv - sent 2 ms, rtt 31 ms)` and one `offset -993 ms (min recv - sent 1 ms, rtt 1988 ms)`; `c371b69` logged the same pair (`-992 ms`, rtt 1989 ms). This is a second variant of card 385: the stalled sample is the rtt, not the delta. On b69 and play build 6 that round's offset (-992 ms) was the one used for up to 30 s; the resulting card 313 age is about 980 ms too large but inside 0 to 2000, so no WARN is logged and a relayed timeline is placed about 1 s too far ahead. b70 uses the session's lowest rtt (31 ms), so this variant is covered by the built fix. Not proven by this run: 0 card313 rows in either log (no relayed timeline after the restart). Effect on the candidate: this is a reason to prefer b70 over b69 for play build 7.

**2026-11-15 15:01 b70 batch complete: 8 of 8 pass.** `c355b70` passed (68.2 s), `c369b70` passed (140.5 s); both 0 FATAL/ERROR/CRASH, WARN classes in the known list (host card355 "returns to the main menu" row expected in c355). b70 `6109d0898f23ed7d` passes: `c373b70` 146.4 s, `playb70` 391.2 s, `c345b70` 84.9 s, `c346b70` 83.3 s, `c344b70` 127.5 s, `c371b70` 59.0 s, `c355b70` 68.2 s, `c369b70` 140.5 s. **b70 is now the play build 7 candidate** (b69 + card 385 both variants + card 362 WARN cap). Card 385 remains "built, not proven" (no run relayed a timeline during a stalled round). Lane 1 stays at play build 6 `aebfe1230e491c6d` until the user says to cut.

**2026-11-15 15:02 b70 regression chain predictions.** Scenarios `c377`, `c374`, `c375`, `c383`, `c376`, `c379`, `c276`, `c384` repinned to `6109d0898f23ed7d` and run in that order by one sequential script (stops at the first run that does not pass, or at GPU busy 30 or more), tags `<card>b70`, envs from each scenario header. These paths (hangar load, client exit and rejoin, rejoin with host dead, restart with client down, pause restart, solo load of a co-op save, tap and hold, one credit) have not run since b67 or b68. Prediction: all 8 pass, 0 FATAL/ERROR/CRASH, WARN classes in the known list, 0 `WARN card313 ... outside` rows. `c377b65` had a stalled clock round, so `c377b70` may prove card 385 (stalled round plus card313 rows without the WARN).

- 2026-11-15 15:15 b70 regression chain result (lane 2, `logs/chain_b70.out`): 8 of 8 passed as predicted. `c377b70` 77.7 s, `c374b70` 86.8 s, `c375b70` 87.4 s, `c383b70` 86.9 s, `c376b70` 89.2 s, `c379b70` 41.0 s, `c276b70` 90.6 s, `c384b70` 58.5 s. 0 FATAL/ERROR/CRASH on both sides of every run; every WARN class is in the known list; 0 `WARN card313` rows. Card 385 second variant seen again in the per-round rows after a restart from checkpoint (`c375b70` client `offset -785 ms ... rtt 1573 ms`, `c383b70` client `offset -966 ms ... rtt 1936 ms`), each beside a second peer row with `rtt 30`/`31 ms`; b70 reads the session minimum (delta 1 ms, rtt 30 ms, offset about -14 ms), so the stalled round no longer feeds card 313. No timeline was relayed in those windows, so card 385 stays "built, not proven". b70 (`6109d0898f23ed7d`) now has 16 of 16 scenario passes and remains the play build 7 candidate.

- 2026-11-15 15:18 efficiency audit from `card330` rows: card 386 opened (`docs/cards/386-digest-build-one-tick.md`). The digest round builds the whole world digest in one tick every 30 s: host `c132_digest_step` longest call 20.34 ms (`c350-c344`), 15.04 ms (`c355b70`), 12.76 ms (`c276b70`) on FED_C2M1, 3.50 ms on FED_C1M1 (`playb70`); `playb70` on FED_C2M1 shows one 19 to 23 ms mod tick per window. The client's build runs in the frame receive path, untimed by `card330`. Not a play blocker (one long frame per 30 s per side); queued after the playtest with the card 330 leftovers. Also read, no defect: `c43_revivable_near` per-tick call from `detour_trace_interactable` (roster of at most `kC43Max` slots on the host, the shared card 368 list on the client; `c43_step` total is 2.3 ms per second of play in `playb70`, most of it elsewhere).

- 2026-11-15 15:29 card 226 root cause and fix, build b71 `61f3016d372e1413` (senior, `mod/src/ubw/net_budget.cpp` only; b70 kept as `build-b5/dwmapi-b70-6109d0898f23ed7d.dll`, still the play build 7 candidate until b71 passes). Finding: the engine refills `QueuedBits` by `CurrentNetSpeed * 8 / 120` bits per frame whatever the frame time, floor two refills. Evidence: gate rows `QueuedBits -33332 to -33332` at 25 frames per 5 s (`c127-c1m1`, busiest windows `25 frames, 52132 bytes`, 2085 B per frame) and at 300 frames per 5 s (`playb70`, `c276b70`, `c355b70`); all 649 gate rows of the 2026-11-13 playtest host log have the same floor, and its peak window (484709 bytes, 276 frames, 52 not ready) used 1756 B per frame of 2083. So the "250000 B/s" link carried 2083 B per host frame: 125 kB/s at 60 fps, 62.5 kB/s at 30 fps. Fix: `net_rate_frame` writes `250000 * frame time * 120` (frame time held in 1/120 to 1/20 s) each tick, so a frame's refill is 250000 B/s of real time. Predictions for `c373b71` then `c344b71` (60 fps cap, same envs as the b70 runs): both pass with 0 FATAL/ERROR/CRASH and WARN classes in the known list; host gate rows show `CurrentNetSpeed` about 250000 to between 450000 and 1500000 (about 500000 in steady frames, higher in long frames) and an idle `QueuedBits` floor near -66664 (2 x 33332) in place of -33332; not-ready frames per window at most the b70 counts (`c373b70` max 5 of 241, `c344b70` max 6 of 298); at most one `host rate cap` row per connection start (the engine's own value), not one per tick. If the floor stays -33332 the engine clamps the value elsewhere and b71 is rejected.

- 2026-11-15 15:36 b71 `61f3016d372e1413` (card 226 fix) result: `c373b71` passed (102 steps, 0 bad), `c344b71` passed (31 steps, 0 bad); 0 FATAL/ERROR/CRASH on both sides, WARN classes in the known list. Predictions held: steady gate rows `QueuedBits -74480 to -60124 ... CurrentNetSpeed 450939 to 558606` (floor doubled from -33332, about 500000 at 60 fps), 1500000 and floor -200000 after long frames; 1 `host rate cap` row (the engine's `100000 -> 1500000`). Pace, `c373` b70 against b71: bit holds 70 to 31, alone sends 17 to 0, longest wait 1145 ms to 416 ms; `c344`: bit holds 74 to 33, alone 9 to 0, longest wait 1075 ms to 500 ms. Window row of the restart window: `not net ready 29 of 241 frames` (b70) against `15 of 242` (b71), same bytes (123320 and 126734 replicated). One prediction missed: the gate row count in one window rose (c373 5 to 7 frames, c344 6 to 11) with `QueuedBits` up to 204045 and 346169: after a 333 ms frame the floor is -200000 (25 kB of headroom), so the engine sends its post-travel burst in one frame and then waits 7 to 11 frames at about 33000 bits per frame. Same bytes, sooner; watch item for the playtest (a burst of up to about 50 kB after a hitch frame). Next: the 8-scenario regression chain on b71 (`logs/chain_b71.out`, tags `<card>b71`, scenarios repinned), prediction 8 of 8 pass with 0 FATAL/ERROR/CRASH and known WARN classes; if so b71 replaces b70 as the play build 7 candidate (b70 fallback).

- 2026-11-15 15:49 b71 regression chain result (`logs/chain_b71.out`): 8 of 8 passed as predicted. `c377b71` 80.1 s, `c374b71` 96.6 s, `c375b71` 91.0 s, `c383b71` 98.2 s, `c376b71` 100.6 s, `c379b71` 43.5 s, `c276b71` 92.6 s, `c384b71` 58.8 s; 0 FATAL/ERROR/CRASH on both sides of every run; every WARN class also appears in the b70 runs (card159 dropship totals row: `c374b71` now, `c383b70` before). b71: 10 passes. Next: second chain (`logs/chain2_b71.out`): `playb71` (play mode, `SKIP_MOVIES=0`), `c345b71`, `c346b71`, `c371b71`, `c355b71`, `c369b71`; prediction 6 of 6 pass, 0 FATAL/ERROR/CRASH, known WARN classes, `playb71` within about 30 s of `playb70` (391.2 s). All 16 scenarios are now pinned to b71; if a run fails for a b71 reason, b70 stays the candidate and the pins go back.

**2026-11-15 16:03 b71 chain 2 result: `playb71` failed; cause and fix, build b72 `653620cb80ca5f6a`.**
Result: `playb71` FAIL at 467.0 s, `play-mode-start.txt` line 101 (`client GameStarted ... on FED_C2M1`, 180 s
timeout); the chain stopped, so c345, c346, c371, c355 and c369 did not run on b71. Prediction missed (pass within
30 s of `playb70`). 0 FATAL, ERROR or CRASH rows.
Logged cause (`logs/playb71-client.log`): after the seamless travel to FED_C2M1 the new PC and its `ClientRestart`
came at 418378120 with a null pawn; the old frontend PC was destroyed and the pawn spawned 167 ms later (418378287
to 418378290, tick 14357); no `AcknowledgePossession` followed. The card 156 map-load restore runs in the
`AcknowledgePossession` hook, so `g_game_only_pending` stayed set and `c135_step` never called GameStarted:
`WARN card135: client t=418441973 GameStarted not called after 60000 ms: PC 1, HUD BP_HUD_Manager_MI_C, pawn 1,
... card 156 restore pending 1`. The host hold (world paused) waited the full 60 s on this client.
In `playb69`, `playb70` and `c346b70` the pawn spawned in the same tick as the PC, then the frontend PC was
destroyed and `AcknowledgePossession` ran in the next tick. So the race (pawn after `ClientRestart`, host world
paused, no engine retry) exists in every build; b71's larger per-frame budget changed the packet order and exposed
it. A slow link could expose it on any build.
Fix (b72, `intro_c135.cpp` `c135_restart_step`): while the op waits with HUD, pawn and map ready and the card 156
restore still pending, a pawn the PC holds but has not acknowledged for 1000 ms gets the engine's own
`ClientRestart` (through `detour_client_restart`), at most 5 calls per op. No other change. Lane 2 has b72; the 16
scenario pins are on `653620cb80ca5f6a`.
Predictions for `playb72` and the 5 scenarios that follow (c345, c346, c371, c355, c369): 6 of 6 pass. Where the
race recurs the client log has `card135: client t=... pawn BP_PlayerCharacterMI_C not acknowledged 1000 ms after it
came: ClientRestart call 1 of 5`, then `possess: AcknowledgePossession`, the `card156 ... (map load): restore` row
and `card135: client GameStarted` within about 100 ms; no `GameStarted not called` WARN in any run. Where the race
does not recur there is no `ClientRestart call` row and the run looks like b70. A second `call 2 of 5` row means the
call did not acknowledge: reject b72 and b71, b70 stays the candidate.

**2026-11-15 16:18 b72 chain 2 result: 6 of 6 pass, the race did not recur.**
`playb72` 393.5 s, `c345b72` 84.9 s, `c346b72` 101.4 s, `c371b72` 60.7 s, `c355b72` 67.7 s, `c369b72` 141.7 s; 0
FATAL, ERROR or CRASH rows; 0 `GameStarted not called` rows; 0 `ClientRestart call` rows. In `playb72` the pawn
spawned (419207692) before the frontend PC's destroy and `AcknowledgePossession` (419209406), the b70 order. So the
runs show no regression from b72, and `c135_restart_step` is not proven by a run yet. Next: `playb72b`, the same
scenario once more (run 2 of 3); prediction unchanged from the 16:03 entry.

**2026-11-15 16:25 `playb72b`: pass in 388.9 s, the race did not recur again.**
Pawn spawn 420120338, frontend PC destroy 420122102, `AcknowledgePossession` 420122103, GameStarted +110 ms after
the op; 0 `ClientRestart call` rows, 0 FATAL, ERROR or CRASH. The race showed in 1 of 3 play runs on the card 226
rate (b71 1, b72 2 clean). `c135_restart_step` stays unproven by a run; no more runs spent on forcing it (watch
item for the playtest logs: a `ClientRestart call` row, or the `GameStarted not called` WARN on builds without it).
Next: the 10 FAST scenarios not yet run on b72 (chain 1 plus c373 and c344), prediction 10 of 10 pass, WARN classes
as b70.

**2026-11-15 16:42 b72 `653620cb80ca5f6a`: 16 of 16 scenarios pass (18 runs).**
Chain 1 plus c373 and c344, 10 of 10: `c377b72` 80.6 s, `c374b72` 88.3 s, `c375b72` 87.7 s, `c383b72` 87.5 s,
`c376b72` 90.1 s, `c379b72` 41.4 s, `c276b72` 92.5 s, `c384b72` 59.0 s, `c373b72` 139.0 s, `c344b72` 141.1 s. 0
FATAL, ERROR or CRASH; 0 `ClientRestart call` rows. WARN classes that b71 runs of the same scenario did not have
(card132 host hit far from the NPC, card159 dropship check and totals, card132 digest round cut by the map change,
`ui continue -> failed`) are all in b69 or b70 logs. Longest pace wait b70 to b72: play 1058 to 493 ms, c345 167 to
67 ms, c346 1060 to 483 ms, c371 2497 to 2605 ms (restart window, not bandwidth).
Play build 7 candidate: b72 (card 226 rate fix plus the possession restart guard; the guard is not run-proven).
Fallback b70 `6109d0898f23ed7d` (16 of 16, no card 226 fix, has the race like every earlier build). Lane 1 stays on
play build 6 until the user decides.

2026-11-15 16:50 playb72c: pass in 389.0 s (third b72 play-mode-start sample). Client pawn spawn 421613013, frontend PC destroy and AcknowledgePossession 421614812 (normal order), GameStarted +34 ms after the op. 0 `ClientRestart call` rows, 0 `GameStarted not called` rows, 0 FATAL/ERROR/CRASH on both sides. The possession race has now been seen in 1 of 4 play runs on the card 226 rate (playb71 only); the b72 guard (`c135_restart_step`) is still not run-proven. b72 `653620cb80ca5f6a` total: 19 runs, 16 of 16 scenarios pass. Play build 7 candidate b72, fallback b70 `6109d0898f23ed7d`; lane 1 stays at play build 6 until the user decides.

2026-11-15 16:53 card 386 step 1, build b73 `0cf31a2d208aba11` (diagnostic only, not a play build candidate): `c132_build` prints `card386: <side> ... digest build X ms, N actors walked ...` each build, and with `UBWCOOP_C386=1` the share per phase (name, class, key, state, ext, attach, tail, sort; rdtsc). Run `c276b73` (FED_C2M1, both sides `UBWCOOP_C386=1`). Predictions: (1) the scenario passes as on b72; (2) host and client rows both appear, the client build time within a factor of 2 of the host's (the client path was never timed); (3) host build 10 to 20 ms on FED_C2M1; (4) the card's suspected driver (class name scan) is under test: if `class` is under 30 percent the plan's step 2 is the wrong fix and the largest phase is fixed instead.

2026-11-15 16:58 card 386 step 1 result (`c276b73`, `c344b73`, both pass, 0 FATAL/ERROR/CRASH): prediction 1 held; 2 held (FED_C2M1 host 23.92/23.16 ms, client 23.08/22.80 ms); 3 missed slightly (23 ms, above the 10 to 20 ms band; `c276` does not reach a FED_C2M1 round, its 10.47 ms there is `c132_inventory` at map load); 4: `class` is 11 to 13 percent, so the card's step 2 is the wrong fix. No phase dominates (name 29, key 19, class 13, sort 12, state 9, attach 9, ext 9 percent). Revised plan in the card (longer round in the play build, or kept entries); decision after the playtest. Lane 2 and all 16 pins are back on b72 `653620cb80ca5f6a`; the source now carries the card 386 timers on top of b72, so the next build differs from b72 by them.

2026-11-15 16:59 WARN baseline for a b72 play build: `logs/b72-warn-classes.txt` (18 lane 2 run pairs, 13 host and 14 client classes; the stale `hb72` log of an older numbering is excluded). If play build 7 is cut from b72, diff the playtest logs against this file; play build 6 logs diff against `logs/b49-warn-classes.txt`.

2026-11-15 17:01 build b74 `ec9e23a2712063a1` = b72 + card 386 timers + a client test lever `UBWCOOP_C135_DROPRESTART=1` (drops the engine's ClientRestart for the own gameplay pawn while the card 156 restore is pending; the guard calls `client_restart_call`, which is not dropped). Purpose: run-prove the b72 possession guard, which never fired in 19 runs. Run `c346b74` with the lever on the client. Predictions: (1) client row `card135: client ... engine ClientRestart for BP_PlayerCharacterMI_C dropped` on each gameplay map load; (2) then `card135: client ... not acknowledged 1000 ms after it came: ClientRestart call 1 of 5`, an `AcknowledgePossession` for the pawn in the same tick, then `card135: client GameStarted`; 0 `GameStarted not called after 60000 ms`; (3) the scenario passes, within about 5 s of the 101.4 s of `c346b72`. If the guard row appears and no acknowledge follows, the guard is wrong and b72 is no better than b70 against the race.

2026-11-15 17:03 `c346b74` result: pass in 84.7 s, 0 FATAL/ERROR/CRASH; all three predictions held. The possession guard is now run-proven on both maps of the run. FED_C1M1: 4 engine ClientRestart calls dropped (422457866 to 422458955), `ClientRestart call 1 of 5` at 422459007, `AcknowledgePossession` for BP_PlayerCharacterMI_C in the same tick, GameStarted +1020 ms after the op. FED_C2M1 (world paused 1, the playb71 condition): 1 call dropped at 422502858, `ClientRestart call 1 of 5` at 422505564, GameStarted 1 ms later (+1095 ms after the op); no `GameStarted not called` row. The guard costs about 1 s on a raced load against the 60 s wait and the card 344 return to the main menu without it. The guard code of b74 and b72 is the same (b74 only routes its call through `client_restart_call` past the lever), so the proof covers b72. Play build 7 candidate stays b72 `653620cb80ca5f6a` (19 runs); b74 has 1 run and adds only the env-gated lever and the card 386 timer row. Lane 2 and all pins are back on b72.

2026-11-15 17:10 run `c387b72` (new scenario `tools/scenarios/c387-c2m1-to-c3m1.txt`, b72, lane 2): the third step of tonight's playthrough, never run since the persistent lobby (card 346): FED_C2M1 level end, hangar, next mission FED_C3M1 (starts in the dropship) by seamless travel, both confirm the hold. Predictions: (1) the hangar's Enter after the FED_C2M1 EndLevel browses to FED_C3M1 and `card346: host ... seamless travel to /Game/Maps/FED/FED_C3M1` follows; (2) the client keeps its connection (0 NetworkFailure, 0 `session dropped`, 0 `card140: host Logout`); (3) the hold row, host confirm, client prompt ready, client confirm, then both own pawns and a client Clock row; (4) 0 FATAL/ERROR/CRASH, 0 `GameStarted not called`. Expected wall about 3 min.

2026-11-15 17:18 `c387b72b` result: pass in 64.4 s, all four predictions held. Hangar Enter after the FED_C2M1 EndLevel browsed to FED_C3M1 (423198873), `card346 ... seamless travel` in the same tick, both on FED_C3M1 5 s later; hold row 423205625, host confirm 423205732, client prompt ready 423206670, client confirm 423206782, client GameStarted +9 ms after the op, both own pawns, client Clock row. 0 FATAL/ERROR/CRASH, no drop (the only in-session `host Logout` rows are the client's quit and the host's own). Shots: both on the beach with the same three objectives. Run 1 (`c387b72`) was VOID by my scenario: FED_C2M1 by direct travel starts in the dropship under the hold, the pawn wait held the client's continue and card 344 sent the host to the menu after 60000 ms (host WARN 422865382); start rewritten to the c125-proof order.
New WARN row, benign: host `WARN card132: seq gap 9-9 from BP_PlayerController_C (got 10)` at 423192069. The lost frame is the client's Pong 9 (sent 423191309) to the host's Ping 760 (423191295), in flight when the host's seamless travel to the hangar swapped the PC (423192069). Pings repeat every 4 to 6 s and the next map's first Pong arrived (recv 2 at 423196329), so nothing waits on the lost one. Expect this class in tonight's logs at map changes; not in `logs/b72-warn-classes.txt`. The 5 client `CollisionOp ... NODE_AddStaticMeshCompo ... no placed copy matches` rows are the FED_C3M1 join class already seen in `c125b54c` (same 5 keys).

2026-11-15 17:20 run `c388b72` (new scenario `tools/scenarios/c388-c3m1-to-c5m2.txt`, b72, lane 2): the rest of the Trooper chain in one run, FED_C3M1 to C3M2, C4M1, C4M2, C5M1, C5M2, each by EndLevel, Next, hangar, Enter, seamless travel, hold with both confirms. None of these five transitions has run since the persistent lobby. Predictions: (1) each hangar Enter browses to the next map in that order; (2) the client keeps one connection through all 10 travels (0 `session dropped`, no `host Logout` before the quit); (3) each map: hold row, both confirms, client Clock row; (4) 0 FATAL/ERROR/CRASH, 0 `GameStarted not called`; WARN rows only of known classes plus the travel `seq gap`. Unknowns a run must show: whether a chapter's last map shows something other than `WB_TrooperLevelEnd` at the end, and what a map's start pawn is (pawn waits are tries). Expected wall about 4 min.

**2026-11-15 17:25 `c388b72` result and `c389b72` predictions (senior).** `c388b72` passed (184.9 s): FED_C3M1 to
C3M2, C4M1, C4M2, C5M1, C5M2 by level end, hangar, Enter, each a seamless travel with the client kept; hold, both
confirms, pawns and a client Clock row on every map; 0 FATAL/ERROR/CRASH, no WARN class outside `logs/b72-warn-classes.txt`
(the 5 `TRY-FAIL` rows are the scenario's 3 s Next-click probes). **Finding (receiver-side shots):** on FED_C5M1 and
FED_C5M2 the client's view has no terrain (white plane, floating rocks). Cause from the host log: the client pawn spawns
on foot at the PlayerStart (-33920, 41090, -437) (`card307 ... kept PlayerStart: nobody Up`, the host rides the dropship),
and that spot has no ground: `card110: host slot 1 ... head` z -327, then -704, then -32279 within 15 s. On C3M1, C3M2,
C4M1, C4M2 the head z stays put (ground at the PlayerStart). Run `c389b72` (FED_C5M1, real ride, no autodropship)
predictions: (1) the client copy falls from the hold's end; (2) if it passes KillZ before the host lands, the host logs
`WARN card046 ... no grounded teammate Up and no checkpoint save yet ... the copy goes Out` and `kill floor: ... below
with nobody else Up` (the client is Out at mission start: a play bug, card 389); (3) if the host lands first, a
`kill floor warp` puts the client beside the host. Either way the client falls through a void for the ride.

### 2026-11-15 17:35 `c389b72` result, card 389, b75 and `c389b75` predictions (senior)

`c389b72` (FED_C5M1, real ride, b72 `653620cb80ca5f6a`): drive rc 0, no crash. **Defect, card 389, all builds, play
blocker for FED_C5M1 and FED_C5M2:** the client's pawn spawns on foot at the PlayerStart (-33920, 41090, -437)
(`card307 ... kept PlayerStart: nobody Up`; the host rides). That spot has no ground. Host `card110 ... slot 1 head` z:
-327, -3317, -23019 ... -163219 when the host landed (47.4 s after `hold off`), -583819 150 s in (about 4000 uu/s).
0 `card046` rows, 0 Warp frames: the chapter 5 kill height is never reached, so the client never joins the host.
My predictions 2 and 3 (a kill floor warp) were wrong.

Fix, b75 `d265c3dfb94c8feb` (`build-b5/dwmapi-b75-d265c3dfb94c8feb.dll`, in `game2`; b74 code plus this):
`c389_step` (`objectives.cpp`, called first in `c46_step`, host only, once a second). An Up player pawn, not attached
to a dropship, that reads Falling at every sample for 6000 ms is warped by `c46_warp` (no damage) beside a grounded Up
player who is not on a dropship; with none yet it logs one `WARN card389 ... no grounded Up player` row and warps at
the first sample after one lands. Leftover: the client still falls through the void for the ride (about 47 s).

Predictions for `c389b75` (same scenario, pins b75):
1. One `WARN card389: host ... no grounded Up player` row about 6 to 7 s after the client pawn starts falling.
2. Within 2 s of `card017: host landing slot 0`: one `card389: host ... kill floor warp beside` row and one
   `card046: host ... kill floor warp ... sent` row; no second `card389` warp row after it.
3. `c389-client-landed` shot shows terrain and the host nearby; slot 1 head z stays near the host's after the warp.
4. No new WARN class besides the `card389` row.

### 2026-11-15 17:37 `c389b75` result (senior)

`c389b75` (b75 `d265c3dfb94c8feb`): drive rc 0, 84 s, no crash, 0 FATAL/ERROR. Predictions 1 to 3 held, with one gap.
- `WARN card389 ... (slot 1) Falling for 6038 ms at z -6129, no grounded Up player off a dropship yet` at 424475482
  (3.0 s after `hold off`; the fall starts at the spawn, during the hold).
- `card017: host landing slot 0` at 424519923; `card389 ... Falling for 50485 ms at z -180336: kill floor warp beside`
  at 424519929 (6 ms later); `card046 ... warp +0.5 s ... gap 80 uu`; slot 1 head z 448 after. 2 `card389` rows only.
- `client-c389b75-c389-client-landed.png`: client stands on terrain facing the landed dropship.
- Gap: `card046 ... Warp frame NOT sent (target guid not sendable)`: the host's pawn has no sendable NetGUID in the
  tick it leaves the dropship. The client still arrived (the server move and its position correction). Not carded;
  check after `c388b75` whether a later warp in the same map sends.
- `KillZ` on FED_C5M1 prints -3.4e38 (no kill height set): why the kill floor never fires there.
`c388b75` (chain regression) running; prediction: passes as on b72, `card389` warp rows on C5M1 and C5M2 only.

### 2026-11-15 17:46 `c388b75` result, b76 and `c388b76` predictions (senior)

`c388b75` (b75, chain C3M1 to C5M2, FAST): passed, 169.3 s, 0 FATAL/ERROR/CRASH, no WARN class outside
`logs/b72-warn-classes.txt` (the listed rows differ only by actor name hash; `WARN card072 ... AmmoMirror ... no server
copy inventory` is 1 row here and 1 in `c388b72`). `card389` warp rows: FED_C5M1 (`Falling for 6752 ms at z -6195`)
and FED_C5M2 (`6449 ms at z -851`) only; none on C3M1, C3M2, C4M1, C4M2. Both `card046 ... warp +0.5 s` rows: gap
80 uu. Client shots `client-c388b75-c388-client-c5m1.png` and `-c5m2.png` show terrain and the objective panel.
C5M2: `Warp frame sent`, client `card046: client ... Warp 1 ... moved`. C5M1: `Warp frame NOT sent (target guid not
sendable)` again (host on the ground 6.7 s): the client arrives by the server move alone.

b76 `90aa405f6b2dadbb` (`build-b5/dwmapi-b76-90aa405f6b2dadbb.dll`, in `game2`; b75 plus): `c46_warp` sends
`ChanOp::WarpTo` with the copy's new position when the target pawn has no sendable NetGUID. Predictions `c388b76`:
1. Passes as b75; `card389` rows on C5M1 and C5M2 only.
2. C5M1 host row ends `not sendable by guid: WarpTo sent`; client `card146: client ... WarpTo ... moved 1`.
3. C5M2 host row `Warp frame sent` as before.

### 2026-11-15 17:49 `c388b76` result: card 389 accepted (senior)

`c388b76` (b76 `90aa405f6b2dadbb`): passed, 180.1 s, 0 FATAL/ERROR/CRASH. All 3 predictions held.
- `card389` rows: 2 (C5M1, C5M2). C5M1 host row: `Warp frame not sendable by guid: WarpTo sent`; client
  `card146: client ... WarpTo 1 (-39459, 62619, 376): own ... (-33920, 41090, -2774) -> (-39459, 62619, 376), moved 1`.
  C5M2: `Warp frame sent`; client `card046: client ... Warp 1 ... moved 1`, `warp +0.5 s ... gap 80 uu`.
- WARN classes as b75, counts within 1, except client `CollisionOp ... NODE_AddStaticMeshCompo ... no placed copy
  matches`: 10 rows (5 at the C3M1 join as before, 5 at the C5M2 start), 5 in `c388b72` and `c388b75`. The host sent
  the same 5 ops at the C5M2 start in both runs (b75 at +1.8 s, b76 at +1.8 s); on b76 the client had no copy yet.
  Same known class, a load-order race, not from the card 389 change (the ops precede the warp by 4.4 s). Watch.
Card 389 accepted on b76; card file moved to `docs/cards/done/`. Leftover (the 47 s fall during a real ride) stays in
the card. `game2` holds b76; scenario pins of `c388` and `c389` are b76; all other pins are b72. Play build 7
candidate stays b72 (16 of 16 suite); b76 = b74 code plus card 389 and has run only `c388` and `c389`.

2026-11-15 17:50 `c346b76` (regression, FED_C1M1 twice, pin b76): passed in 84.9 s, 0 FATAL/ERROR/CRASH, host WARN 5 and
client WARN 1 as `c346b74`, 0 `card389` rows (prediction held: no long-fall warp on a map with ground).

2026-11-15 17:52 `c276b76` (regression, FED_C1M1 pickups, pin b76): passed in 94.6 s, 0 FATAL/ERROR/CRASH, host WARN 3
and client WARN 1 as `c276b73`, 0 `TRY-FAIL`, 0 `card389` rows. b76 has now passed `c389`(b75), `c388`, `c346`, `c276`.

2026-11-15 17:54 card 389 sibling sweep (static, from logs): client head z at the PlayerStart is constant on FED_C1M1,
C2M1 (`c387b72`: 9202 for 50 s), C3M1, C3M2, C4M1, C4M2. Only FED_C5M1 and FED_C5M2 have no ground there. All 8 FED
maps checked.

2026-11-15 17:53 chain 1 regression on b76 started (`logs/chain_b76.out`, expected about 10 min): `c377`, `c374`, `c375`,
`c383`, `c376`, `c379`, `c384`, pins b76. Prediction: 7 of 7 pass with times within 10 s of the b72 chain, 0
FATAL/ERROR/CRASH, 0 `card389` rows (FED_C1M1 has ground; Down players are skipped), no WARN class outside the known
list.

### 2026-11-15 18:12 **play build 7 = b76 `90aa405f6b2dadbb`** (user decision "i want latest")

Chain 1 on b76 (`logs/chain_b76.out`): 7 of 7 passed as predicted: `c377b76` 83.9 s, `c374b76` 98.6 s, `c375b76` 89.9 s,
`c383b76` 88.7 s, `c376b76` 92.1 s, `c379b76` 42.9 s, `c384b76` 60.6 s. 0 FATAL/ERROR/CRASH, 0 crash dumps, 0 `card389`
rows. b76 total: 12 scenario passes (`c389`(b75), `c388`, `c346`, `c276`, chain 1); `c373`, `c344`, `c345`, `c371`,
`c387` and the play-mode start were not rerun on b76 (last passed on b72).

Installed in `game/` (lane 1) and `build/dwmapi.dll`, copy `build/dwmapi-play-90aa405f6b2dadbb.dll`, package
`dist/UBW-Coop-20261115-1812.zip`. Lane 2 holds the same hash. Build 7 = play build 6 plus cards 364 to 385, the
card 226 fix, the possession restart guard, card 386 timing rows (log only) and card 389. Scenario pins: `c388`,
`c389`, `c346`, `c276`, `c377`, `c374`, `c375`, `c383`, `c376`, `c379`, `c384` are b76; the rest are b72.

After the playtest: WARN diff of both logs against `logs/b72-warn-classes.txt`; the one new expected class is
`WARN card389 ... no grounded Up player off a dropship yet` (chapter 5 real ride only). Lane 2 stays idle until the
user says the playtest is over. Next card number: 390.

2026-11-15 18:20, card 390 (user: "so investigate that and fix it", packet sending efficiency). Static trace on
`logs/c388b76-host.log`: (1) the `QueuedBits` jump to 501,311 at a map start is the engine's own first actor
replication, not the mod: no mod frame went out in the 50 ms before it and the pacer held 129 frames; nothing to fix
(WRONG, corrected 18:35: see card 391).
(2) `Fx` rows are already binary blob frames filled to 2.6 kB. (3) Real waste: the card 132 ledger stamp
(`mod/src/ue/chan.cpp` `stamped`) wrote four 15-bit chars, so every stamped FString was serialized UCS-2, 2 bytes for
every char of every text frame (a 52-char `X` row cost 146 B). Fix in b77 `0f1e33f879593a62` (lane 2 only): a frame
that is all below 0x80 takes a narrow stamp (mark 0x1D, ten 6-bit chars + 0x20) and stays ANSI; wide frames keep
the old stamp; `frame_bits` now counts the real size. Host and client must run the same build. Predictions for
`c388b77` against `c388b76` (924,004 B `ClientMessage`, `WorldOp` 146 B/row, `CollisionOp` 165 B/row, `Dropship`
296 B/row): `WorldOp` under 90 B/row, `CollisionOp` under 100, `Dropship` under 170, `Fx` unchanged within 5 percent,
`ClientMessage` total under 700,000 B for the same chain, scenario pass, 0 ledger mismatches, no new WARN class.

2026-11-15 18:27, `c388b77` result (card 390, accepted): every prediction held. `ClientMessage` 924,004 to 682,547 B
(26 percent less), `WorldOp` 146 to 81 B/row, `CollisionOp` 164 to 90, `Dropship` 295 to 156, `Fx` 331,174 to 328,328 B,
top reliable bunch peak 256 to 203, `IsNetReady` false 82 of 4753 to 36 of 3910 frames, scenario pass 173.5 s, 0 ledger
WARN rows. One client `WARN card140: client hit ... 530 uu` row not seen in `c388b76` (watch). Lane 2 and all 11
scenario pins now b77 `0f1e33f879593a62`; lane 1 and `build/dwmapi.dll` stay play build 7 = b76. Card:
`docs/cards/done/390-ledger-stamp-doubles-text-frames.md`. Next card number 391.

- 2026-11-15 18:35 card 391 opened (senior, logs only, no run in the playtest window): the map-start `QueuedBits`
burst is not actor replication (`ReplicateActor` about 2.9 kB in the window). Each first `stall frame` comes 8 to
33 ms after a long client frame ends (732 to 2543 ms load hitch, 7 of 7 checked). Real first-stall `QueuedBits`:
b76 573995, 314113, 287070, 114959, 133383, 501311; b77 411484, 64960, 97208, 59435, 455620 (the 520,000 to 650,000
quoted before was the `CurrentNetSpeed` column). Hypothesis, unproven: client socket buffer overflows during the
hitch, NAK, host resends the lost reliable bunches in one frame. Predictions, diagnostic and fix candidates in
`docs/cards/391-map-start-burst-after-client-hitch.md`. Diagnostic build b78 only after the playtest. Next card 392.

- 2026-11-15 18:41 card 391 run `c391b78` (b78 `305457728d3bda1d`, slot-diff diagnostic): prediction 1 held. Burst
frame t=428211872: `QueuedBits` -51426 to 406622, conn +0x4B8 (OutBytes) +61,381 B, +0x4C8 (OutPackets) +63, +0x4F0
and +0x4F8 (OutPacketsLost, OutTotalPacketsLost) 0 to 70. So 70 packets were lost and resent in one frame. Run ended
at 98 s by a client crash (new, card 392: `EXCEPTION_ACCESS_VIOLATION` reading 0x0 at exe+0x510B401, in the NPC tick
path exe+0x51AB094, right after `seamless map change done: world Untitled` on the travel to the hangar after C4M1;
host-only diagnostic, not the cause). Fix build b79 `0780e449884acb02` (`chan.cpp` `PaceGate::Silent`,
`kPaceSilentMs = 100`; `net_budget.cpp` reads LastReceiveTime +0x1D8 and LastTickTime +0x1F8): mod frames wait in
the FIFO while the client has sent nothing for 100 ms. Predictions for `c391b79`: `packets lost` in stall lines stays
under 10 for the run; no first-stall `QueuedBits` above 150,000; pace line `silent client` holds above 0 at each map
start; `full sends 0`; scenario passes; 0 ledger WARN rows. 11 scenario pins now b79. Next card 393.

- 2026-11-15 18:49 run `c391b79` (b79): card 391 predictions met (0 `stall frame` lines, b77 36, b76 68; `silent client` holds 489; `full sends 0`; longest queue wait 1502 ms; 0 ledger WARN rows) but the scenario failed at line 152. Cause is card 393 (test cinematic skip repeated on the level-end cutscene inside the transition world, second travel), a timing race also present before b79. b80 `2e0c99e96d390d63` = b79 + card 392 (client NPC tick off in `Untitled`) + card 393; in `game2`, 11 pins moved. Predictions for `c392b80`: scenario passes; one 3D_Frontend travel per level end; no `try 2` on a level-end cutscene; `card392: client` line at each `Untitled` travel; no crash at exe+0x510B401; 0 stall frames; 0 ledger WARN rows. Next card 394.

- 2026-11-15 18:54 run `c392b80` (b80 `2e0c99e96d390d63`): all predictions met; scenario passed (drive rc 0), 0 stall frames, 0 FATAL, no dump, 10 `card392` lines, 5 frontend travels, no level-end `try 2`. Cards 391, 392, 393 accepted and moved to `cards/done`. Load holds were longer than `c388b77` (12975/6660/7129 ms against 9088/3915/4945 ms): host and client load frames were longer too (client 4496 and 4436 ms against 2959 and 2543 ms), and the client confirm still follows its last load frame by about 100 ms, so this is machine load, not the pacer. WARN classes: all seen before except 2 `card389 ... Falling ... no grounded Up player` rows (client pawn falling through the long load frames; card 389 leftover). Lane 2 = b80; lane 1 still b76 `90aa405f6b2dadbb`. To do in the next build: remove the card 391 slot-diff diagnostic `c391_sample`. Next card 394.
- 2026-11-15 card 394: card 391 slot-diff diagnostic removed from `mod/src/ubw/net_budget.cpp` (`C391Snap`, `g_c391`, `c391_diff_log`, `c391_sample` and its call in `net_gate_sample`); the fix stays (`kConnLastReceiveTime`, `kConnLastTickTime`, `silent_ms`, `silent client`, `kConnOutTotalLost` in the stall line). b81 `4ed3c7548739ccce` = b80 minus the diagnostic (84 exports, KERNEL32 only, 0 `card391` strings); in `game2`, 11 pins moved from `2e0c99e96d390d63`. Lane 1 still b76 `90aa405f6b2dadbb`. Predictions for `c394b81` (c388-c3m1-to-c5m2, run line as `c392b80`): scenario passes (drive rc 0); 0 `card391:` lines in both logs; 0 `stall frame` lines; `silent client` holds above 0; `full sends 0`; 10 `card392: client` lines; no level-end `try 2`; 0 FATAL, no dump; no new WARN class against `c392b80`.
- 2026-11-15 19:02 run `c394b81` (b81 `4ed3c7548739ccce`): predictions met. Scenario passed (drive rc 0, 210 s, 22 map loads each side, host rc 0, client rc 0); 0 `card391:` lines both logs (`c392b80` host 45); 0 `stall frame` lines; `silent client` above 0 on 12 of 20 pace lines, top 162 (`c392b80`: 12 of 19, top 91; same share of 0 rows); `full sends 0` 20 of 20 host pace lines; 10 `card392: client` lines; no level-end `try 2`; 0 FATAL/ERROR, no dump. WARN host 23, client 9. Against `c392b80` one class new to that run, seen before (c119g, c346-persistent): client `card114: cannot create ...UBWCoop-Difficulty.defaults.ini (err 32)`, a sharing violation on the shared Saved dir at start. 1 card389 `Falling` row on the host (`c392b80` 2, known card 389 leftover). Card 394 for senior review. Lane 2 = b81; lane 1 still b76 `90aa405f6b2dadbb`.
- 2026-11-15 19:08 card 395 (`docs/cards/395-fx-names-on-demand.md`): b82 `d4d25f54b139c092` = b81 + FX name rows on demand (`fx_c136_net.cpp`: per-peer bit per name row `names_had` in place of `names_sent`; queue and journal rows carry their up to 4 name rows; `frame_add_named` puts the missing name rows and the row in one frame and marks the names had only when that frame went out; join replay clears the bits as it does the asset table; the replay line prints name rows sent and the table size) + card 389 fall count restarts while `c320_holding()` (`objectives.cpp`). Client name table is a plain index array (`client_name_row`), sparse indices fine. In `game2`, 11 pins moved from `4ed3c7548739ccce`. Lane 1 still b76. Predictions for `c395b82` (c388-c3m1-to-c5m2, run line as `c394b81`): join replay name rows at most 4 times journal rows per FED map (FED_C3M1 at most 472; b81 1538); first-window Fx bytes FED_C3M1 under 30000 (b81 70754), FED_C5M2 under 25000 (b81 61112); client 0 `WARN card136` bad row lines; client FX replay counts per map as b81 within spread; digest mismatches 0; 0 `WARN card389 ... no grounded Up player` rows starting under a load hold; drive rc 0, 0 stall frames, 0 FATAL, no new WARN class against `c394b81`.
- 2026-11-15 19:13 run `c395b82` (b82 `d4d25f54b139c092`, lane 2, run line as `c394b81`): drive rc 0 in 180 s, host rc 0, client rc 0, 22 map loads each side, no dumps, 0 FATAL, 0 stall frames. Join replay name rows (table size) / journal rows / first-window Fx bytes (b81): FED_C3M1 103 (1538) / 118 / 13420 (70754); FED_C3M2 82 (892) / 84 / 10914 (38624); FED_C4M1 120 (806) / 124 / 17900 (38978); FED_C4M2 125 (483) / 205 / 16822 (26146); FED_C5M1 66 (589) / 75 / 8854 (28110); FED_C5M2 80 (1408) / 93 / 11274 (61112); hangar 22 (24) / 22 / about 2184 (2238). Every map under 4 times its journal rows; FED_C3M1 under 30000, FED_C5M2 under 25000. Client FX replay counts per map same as b81 (118, 22, 84, 22, 124, 22, 205, 22, 75, 22, 93). `WARN card136` 0 both sides; host fx map totals 45 lines all 0 bad; client misses key 176, target 109 (b81 179, 96; b80 181, 102), asset 0, unsupported 0, bad 0. Digest mismatches 0. `WARN card389` 0 (b81 1). WARN host 19, client 11. Class diff against `c394b81`: no new host class (gone: card132 host HopperBug hit row, card389 falling); client `WARN card140: client hit ... host location ... from the copy's ReplicatedMovement` (3 rows) is absent in `c394b81` but present in `c392b80` (4), `c391b78` and `c388b77`, so not new; gone on client: card114 cannot create (err 32). All predictions hold. Caveat (same as the asset table): names are marked had when `chan_send_client` accepts the frame; a frame lost after that would leave the peer without those names until the next map. Traced after the report (19:20): `chan_send_client` true means sent now or queued in the pace FIFO on a reliable ClientMessage; queued frames are dropped only when the PC is gone (`c140_pace_logout`, or no live connection at map change), and a returning PC gets `c136_join_replay`, which clears `names_had`; a map arm resets every peer. So an accepted frame is either delivered or its peer's bits are cleared before reuse; the caveat does not apply. The old code also marked names sent when `frame_add` failed. Card 395 for senior review. Lane 2 = b82; lane 1 still b76 `90aa405f6b2dadbb`.
- 2026-11-15 19:20 card 396 (`docs/cards/396-client-loading-screen-black-and-flicker.md`, code by the senior): b83 `c334b626d793c7b1` = b82 + IntroStart as a priority frame (`c135_send`), card 320 hold kept on while frames are still queued to the clients sent the op (`c396_queued`, at most 5 s), card 337 cover kept 500 ms after the screen is up (`render_c337.cpp`). Built clean, in `game2` only, 11 pins moved from `d4d25f54b139c092`. Lane 1 still b76. Predictions (card text, against `c395b82`): run 1 `c396b83` (c388-c3m1-to-c5m2): IntroStart ledger sent row within 50 ms of `join replay: IntroStart` on all 6 FED maps (b82 841 to 2462 ms), 6 `card396: host ... ahead of` rows; 6 client `world rendering on` rows, none WARN, each at least 500 ms shorter than b82 (3744, 5182, 6234, 6374, 5807, 5805), reason "screen confirmed"; host `hold stays on` then `queued frames waited for X ms, 0 left`, X under 5000; 6 `hold off ... (all confirmed)`; 0 `WARN card337/card396/card320/card344`, no new WARN class, digest mismatches 0, client FX replay counts as b82. Run 2 (FED_C3M1, `UBWCOOP_C135_KEEP_SCREEN=1`): client shots at `client GameStarted`, about 250 ms later, and after `world rendering on ... (title screen up)` show black or the loading screen, never the map; that row at least 500 ms after GameStarted.
- 2026-11-15 19:26 runs `c396b83` and `c396b83r2` (b83 `c334b626d793c7b1`, lane 2). Run 1 (c388-c3m1-to-c5m2): drive rc 0 in 182 s, host rc 0, client rc 0, 22 map loads each side, no dumps, 0 FATAL; WARN host 19, client 13 (b82 19, 11); no new WARN class against `c395b82` (diffs are hex guid normalization in card140 hit rows); 0 `WARN card337/card396/card320/card344`; digest mismatches 0; client FX replay counts identical to b82. Prediction 1 holds: IntroStart ledger sent row 0 ms after `join replay: IntroStart` on all 6 maps (b82 2462, 841, 961, 761, 829, 843 ms; ledger entry 77 to 96, b82 374 to 445), 6 `card396: host ... ahead of` rows (356, 329, 313, 278, 350, 342 queued frames). **Prediction 2 fails**: client black ms b82 to b83: FED_C3M1 3744 to 4089, C3M2 5182 to 5560, C4M1 6234 to 6809, C4M2 6374 to 6391, C5M1 5807 to 5381, C5M2 5805 to 5622 (reasons: 4 "screen confirmed", 2 "title screen up"; none WARN). Cause in the logs: IntroStart now reaches the client 528 to 1274 ms earlier (client IntroStart row after LoadMap return b82 3723, 5179, 6136, 6371, 5753, 5354; b83 3077, 4599, 5608, 5313, 4479, 4426), but it lands in the same client frame as the whole join replay burst, so GameStarted comes only after that frame: `card303: client ... frame of N ms` ending at GameStarted is 2512, 852, 1041, 817, 798, 850 ms on b83 against 309, 235, 353, 466, 285, 696 ms on b82 (on b82 the burst frame, 913 to 1194 ms, came before IntroStart arrived). GameStarted is +517 to +795 ms after the op on b83 (b82 +3 to +451). The 500 ms paint cover then adds on top. Prediction 3 not exercised: 0 `hold stays on` rows (the queue was always empty before the last confirm); 6 `hold off ... (all confirmed)` after 9539, 3976, 5093, 4985, 3833, 4420 ms (b82 9467, 3867, 4860, 4788, 4175, 4614). Prediction 4 holds. Run 2 (`tools/scenarios/c396-r2.txt`, FED_C3M1, keep screen both sides, no AUTODROPSHIP): drive rc 0 in 40 s; WARN host 2 (attach err 126, the expected SKIP_FLIGHT config row), client 1 (attach). GameStarted t=431028456 (+2315 ms after the op), `world rendering on after 4102 ms (title screen up)` at +506 ms after GameStarted (prediction 5 timing holds). Shots (`prefix2/.../Saved/Screenshots/Windows/client-c396b83r2-c396-c-started.png` +438 ms after GameStarted and before the cover lifted, `...-started2.png` +557 ms, `...-cover-off.png` +661 ms) all show the loading screen (OPERATION RAZORWIRE art, press any key); never the map, never black. The shot step's own latency is about 440 ms, so no shot at GameStarted itself. No "Player" nameplate or other mod widget over the screen; the third shot has the game's own loading tip fading in. Prediction 5 holds. Card 396 for senior review; prediction 2 failed. Lane 2 = b83; lane 1 still b76 `90aa405f6b2dadbb`.

- 2026-11-15 19:00 card 394 (junior): remove the card 391 diagnostic `c391_sample`, build b81, run `c394b81`. Senior trace of the 2 `WARN card389 ... no grounded Up player` rows in `c392b80`: both fell inside the card 320 load hold (429100915 to 429109473 for FED_C5M2; z constant at 499 and -437, world paused), where `c389_step` (`objectives.cpp`) counts wall-clock time; the kill floor warp ran 3.8 s later as designed. No player-visible fault. Fix for the build after b81 (not edited while the junior builds): start the card 389 fall count only once the load hold is off, so the row stays out of the WARN diff. Next card 395.

- 2026-11-15 19:06 card 394 accepted (`docs/cards/done/394-remove-card-391-slot-diff-diagnostic.md`): lane 2 = b81 `4ed3c7548739ccce`, `c394b81` passed, source and hashes checked by senior. Lane 1 and `build/dwmapi.dll` stay b76.
- 2026-11-15 19:06 efficiency audit, `c394b81` host rows. (1) Bytes: the FX join replay sends the whole name table, 1538 name rows for 118 journal rows on FED_C3M1, about 60 KB of the 107 KB first window; card 395 opened (`docs/cards/395-fx-names-on-demand.md`), predictions in the card, handed to the junior with the card 389 fall count fix, build b82. (2) Per frame: `c132_ext_step` 269 to 281 us mean on FED_C4M1/C4M2; the cost is bound by `kExtBudget` 1024 actors per tick at about 70 ns lookup plus 100 ns visit per actor (`playb68r3` split rows), under 2 % of a 16.6 ms frame; no card. Longest mod calls per map are the digest build (11.9 to 16.5 ms, card 386) and single 3 to 6 ms calls of `c28_step`, `c43_step`, `c140_pace_step`, `c159_dropship_step` at map start; not traced yet. Next card 396.

- 2026-11-15 19:07 efficiency audit, `c394b81`, measured and not carded (each under 2 % of a 16.6 ms frame). Mod steps sum to about 0.7 to 0.9 ms per frame on the host and 0.8 to 1.0 ms on the client. Client: `client_significance_step` 214 to 389 us mean is the game's own UpdateSignificanceManager (the host's game mode runs the same), nothing to cut; `c37_step` 108 to 250 us mean is its level walk every 100 ms at about 0.94 ms a walk (FED_C3M2: 162 calls 151.5 ms, `collect by caller` row); a second caller `mod+0x13b4ce` walks at about 2 Hz on both sides (68 to 90 calls, 51 to 105 ms a map). One shared class index per walk would remove about 1 ms frames 10 to 14 times a second on the client; candidate after the playtest. Card 390 leftover numbers (row batching at map start), FED_C3M1 first window: WorldOp 292 rows 23740 B, of them 220 'X' ext rows and 52 'K' rows, one RPC per row at 81 B; CollisionOp 135 rows 12107 B at 89 B.

**2026-11-15 19:18 card 396 open (user report: client black screen, then map flicker before the loading screen).** Trace and predictions in `docs/cards/396-client-loading-screen-black-and-flicker.md`. Code in the tree, not built: IntroStart is a priority frame, the hold waits for queued frames after the last confirm (5 s bound), the card 337 cover stays 500 ms after the screen is up. Backups of b82 sources: senior scratchpad `intro_c135.cpp.b82`, `render_c337.cpp.b82`. Junior builds b83 and runs the two proof runs. Card 397 (not written): loading screen for the rest of the black (host load, HUD and pawn wait). Card 395 review still owed. Next card 398.

**2026-11-15 19:19 card 395 accepted** (diff reviewed, card in `done/`, CARDLOG row). Lane 2 = b82 `d4d25f54b139c092`. Junior has card 396: build b83, runs `c396b83` and the kept-screen shot run.

- 19:22 Card 397 written (`docs/cards/397-client-loading-screen-from-loadmap-return.md`): client creates its own
  `WB_Loading_Screen_Final_C` at its LoadMap return (card 337 cover on), IDs from a new host op `TravelIds` sent in
  `c346_host_seamless` before ServerTravel; copy removed in `c337_uncover`. Trace static, 6 predictions in the card.
  Not coded: source stays untouched until the junior's card 396 report (b83) is in. Next free card number 398.

**2026-11-15 19:29 cards 396 and 397 accepted.** Lane 2 = b84 `3419c5f330a48d9c` (396 + 397); `c388-c3m1-to-c5m2` pins moved to b84, the junior's other 10 pins still say b83. Lane 1 stays b76 until the user says otherwise; the loading screen fix reaches play only with a new play build. Leftovers, not carded: (1) a direct join gets no copy (black about 4 s); (2) the client applies the whole join replay burst in one frame (798 to 2512 ms, card303), which delays GameStarted after the op. Next free card number 398.

- 2026-11-15 19:34 run `c397b84r2` (junior; card 397 prediction 5, the copy-to-game handover; b84 `3419c5f330a48d9c`, lane 2, `tools/scenarios/c397-r2.txt`: FED_C3M1, level end, hangar, seamless travel to FED_C3M2; client `UBWCOOP_C135_KEEP_SCREEN=1`, host AUTODROPSHIP=1). Drive rc 0 in 86 s, host rc 0, client rc 0, 6 map loads each side, no dumps, 0 FATAL; dead time 3 s (the c388 hangar try wait). Client FED_C3M2 rows: copy up t=431582394 (+1 ms after the cover), GameStarted t=431589463 (+221 ms after the op), then `card303: client t=431591168 frame of 1697 ms`, copy removed (title screen up) and `world rendering on after 8777 ms (title screen up)` both at t=431591170 (0 ms apart). Shots (`prefix2/.../Saved/Screenshots/Windows/client-c397b84r2-c397-c-*.png`, all viewed): (a) copy, cmd done 431582644: loading screen, OPERATION OBSIDIAN art, LOADING... icon; (b) started, cmd done 431591172, so its frame is after the removal (the 1697 ms frame held the step): same art, no icon, no prompt yet; (c) removed: same art, game's tip and PRESS ANY KEY fading in; (d) later: same, tip and prompt full. None black, none the map, no doubled or offset text, no mod widget; art identical in position across the handover. (c) and (d) differ from (b) only by the game's own fade-in of tip and prompt. No shot caught the copy and the game's screen in the same frame. Mismatches 0 (playlog summaries). WARN host 5 (attach, autodropship pause requester, card346 GetViewTarget guard x2, card072 AmmoMirror), client 1 (attach); only AmmoMirror is outside `logs/b72-warn-classes.txt` and it is in b80/b82/b83 too. Prediction 5 holds. End shot (after continue): client HUD on FED_C3M2, view tilted on a cliff edge (not checked).

**2026-11-15 19:37 card 398 accepted; 397 prediction 5 holds (c397b84r2).** Lane 2 = b85 `0bf0a7701e990303` (396 + 397 + 398), all scenario pins name b85. Lane 1 stays b76; the loading screen fixes (396 to 398) reach play only with a new play build on the user's word. Leftovers, not carded: (1) the client applies the whole join replay burst in one frame (798 to 2512 ms), which lengthens the loading screen and delays GameStarted; (2) BUG maps get no copy on a direct join; (3) the client's view during the engine's own LoadMap (before the cover) is not checked by a shot; (4) junior's side note: client view tilted on a cliff edge about 1.6 s after the screen closed on FED_C3M2 (`client-c397b84r2-c397-c-end.png`), not traced. Next free card number 399.

**2026-11-15 19:44 long client frames under the loading screen: engine code, not the mod's replay apply (static, run c398b85).** Each map shows two `card303` client frames before GameStarted: one of 1863 to 3785 ms with the stack `exe+0x1717ad5 < exe+0x3582d3c < exe+0x1661fc5 < exe+0x1736258 < exe+0x1726ec7 < exe+0x1735392` (the same chain on all 6 maps), then one of 711 to 1977 ms. Every sampled frame is in the game's exe, none in `dwmapi.dll`, so leftover (1) of the 19:4x entry ("the client applies the whole join replay burst in one frame") is not shown by the stacks. The chain is not resolved to names; it sits in the exe's low range (core object code), which fits blocking asset loads on the game thread. Before carding: resolve the six addresses, and split the frame by a `card303` row around the mod's own replay apply (rows applied, ms). Card 399 (client arrival on dropship-start maps, option A) is with the junior.

**2026-11-15 20:10 efficiency audit resumed (user asked).** (1) Card 386 step 2 coded: the digest round runs every 120 s in a play build, 30 s in test runs (`c386_round_ms`, `digest_c132.cpp`); built `3de22d10fb2861a3`, not installed, lane 2 stays b85. The change is in the source tree, so the junior's card 399 build carries it; its run must still show 30 s timer rounds. (2) `card329` FX rows of c398b85 (no combat): rows over 20000 uu from the receiving pawn are 83 to 225 journal rows (2.9 to 7.7 kB) and up to 139 one-shot rows (5.7 kB) per map start. Not carded: the client pawn stood at the far PlayerStart in that run (card 399), which inflates the band, and the totals are a few kB per map. Re-read after 399 with a combat run; one-shot rows over 20000 uu are the cull candidate (journal rows must stay for late arrival).

**2026-11-15 20:30, senior: card 399 accepted; efficiency audit round 2 (run `c399b87f`, b87).**
- Card 399 accepted (b88 `0e0874efd403979f` on lane 2, pins moved by the junior). Lane 1 stays b76. Junior works card
  400 (a client's trooper hidden and not hittable on the host until that client confirms the intro; user report).
- Host per-frame cost: `c132_ext_step` 192 to 265 us per frame on every mission map, the largest mod step. Card 401
  written (candidate list between full sweeps, expected 60 to 80 us), not coded.
- Client per-frame cost: `client_significance_step` 157 us (the game's own significance update, which the host's game
  mode runs too: not mod overhead) and `c37_step` 117 us (level walk every 100 ms, known, shared class index still
  the fix).
- Wire, measured and left as is: map start `WorldOp` 245 to 301 rows at 80 B and `CollisionOp` 93 to 375 rows at 87 to
  93 B (20 to 58 kB once per map, sent while the client loads; the 1683 ms queue wait is the silent-client hold, 88
  holds, not a rate limit). Packing them (card 150 codec) would save little: each row's actor name is unique and the
  4096-id dictionary is per process, so 300 names per map would fill it and push Fx asset paths inline.
- Wire in combat (5 s windows): Fx 2 to 5 kB/s in 65 to 202 rows at 93 to 125 B (already packed), GrenadeSpawn up to
  65 rows at 83 to 86 B, Anim 36 B rows. Fx one-shot rows over 20000 uu from the receiver: 30 to 90 rows, 1.1 to
  3.8 kB per map; journal rows over 20000 uu: 142 to 225 rows, 5 to 7.7 kB per map. Too small to cull by distance.
- Test only: `auto_dropship_step` one 904 ms call on FED_C5M1 (UBWCOOP_AUTODROPSHIP), not in play.

**2026-11-15 20:50, senior: card 402 written.** Level walks (`c23_collect`, 0.7 ms each) cost the client 172 us per
frame (largest client mod cost; 94% of `c37_step`) and the host 126 us per frame. Fix: a class index behind
`c23_collect` that scans only actors spawned since the last call, checked against a whole walk every 5 s. Cards 401
and 402 share the append assumption and one build and run, after card 400.

**2026-11-15 21:20, senior: audit round 3 (run `c399b87f`, host sent bytes over the 6-map chain).** Wire is not the
limit: 658795 B in 29 active 5 s windows (about 4.5 kB/s). Fx 269734 B (40.9%, 1737 frames, 155 B a frame), WorldOp
143743 B (21.8%), CollisionOp 94922 B (14.4%), GrenadeSpawn 38326 B, Dropship 31899 B, GrenadeExplode 23862 B, Anim
15556 B, all others under 2% each. Fx breakdown: event rows 173896 B (one-shot 40.1 B a row, journal 34.2, end 12.4),
15-bit blob coding about 11.6 kB, frame headers about 31 kB (18 B a frame, 3.5 rows a frame), the rest (about 53 kB)
name rows: each map's name table is new (FED_C3M1 1538 names, actor and component names, sent once per peer when a
row first uses one). Nothing here is worth a card: the CPU cards 401 and 402 are the gains.
- Seen, not carded: `c28_try_attach` loads the female trooper animation set (`c29_load_set`, synchronous) before it
  checks that the Morita template exists. In the transition map after a mission (no template) that load is one
  134 to 452 ms `c28_step` call on the client (3 of 6 mission exits), under the loading screen. Moving the load behind
  the template check may only move the hitch to the next mission's first attach; needs one run with the load timed
  on both paths before a change.

**2026-11-15 20:45, senior: card 400 accepted (b90 `318b8510208b97e0` on lane 2, 17 scenarios pinned).** Review
and leftovers in `docs/cards/done/400-unconfirmed-client-hidden.md`. Lane 1 stays b76. Next build b91: cards 401
(ext sweep candidate list) and 402 (level walk class index), one build and one run against the cards' predictions.

- 2026-11-15 21:05 cards 401 and 402, runs c401b91a and c401b92a: host `c132_ext_step` mean 98.6 to 118.3 us (was
  192 to 265), client `c37_step` mean 8.0 to 27.8 us (was 117 to 215). Both first cuts assumed a new actor is
  appended to `ULevel::Actors`; b92's diagnostic showed 10 actors in slots below the level's last count, missed for
  up to 5 s (402) or 7.2 s (401). b91 and b92 are not acceptable. b93 `2662d2d696538671` reworks 402 as a compare of
  a kept copy of every level's Actors array (no slot order, no weak refs) and feeds the refilled slots to 401's
  list. Run c401b93a is with the junior; predictions in its request and in both cards. Lane 2 holds b93 once
  installed; lane 1 stays b76.

- 2026-11-15 21:20 audit round 4, per-frame budget from run c401b92a on FED_C5M1 (sum of the `card330 ... step`
  means, test autopilot left out): host 315 us per frame over 67 steps, client 310 us over 70 steps, about 1.9% of a
  16.6 ms frame. Host: `c132_ext_step` 118 us, every other step under 20 us. Client: `client_significance_step`
  144 us (the game's own UpdateSignificanceManager, which the host pays inside its game mode tick; not ours to cut),
  every other step under 22 us. Single long calls: host `c132_digest_step` 12 to 16 ms once per map is the map-load
  inventory log (`inventory (map load) ... pass 14 ms`, diagnostic, about 200 log rows; remove with the card 293
  diagnostic removal); client `c28_step` 295 to 485 ms once on the transition map is the known `c28_try_attach` sync
  load. Nothing new carded: after cards 401 and 402 no step is worth more than 0.1% of a frame.

2026-11-15 21:38 cards 401 and 402, b93 runs. Lane 2 holds `cfdd06ae5462361c` (b93 plus the reset fix: `c402_slots_reset` drops every entry, because an entry's blueprint class is freed by the travel). Run c401b93c: 0 `WARN card402`, 0 `WARN card401`, 0 mismatches over four maps; numbers in the two cards. Open, blocks acceptance: at the FED_C5M1 level end the host's three Next presses brought no ServerTravel in 160 s. The logs show the score screen never typed: 0 `MS_UI_FedScore_Typewriter_Click` rows on host and client, no `Final_Score`, only `Initial_Score` 2 s after the first press; on every passing map (same run, and C5M1 in b92a) the typing starts within 12 ms of the level-end UI and the second press travels. The end came 12.6 s after LoadMap, the same as b92a (13.3 s), so it is not scenario timing. The mod has no code on the typewriter widgets; no cause is established and no link to cards 401 or 402 is shown or excluded. Scenario `c399-fast-chain.txt` now records, per level end, whether the typing starts (a `try` wait on the click row) and takes host and client shots of the score screen when three presses bring no travel. Next: one chain run on `cfdd06ae5462361c`; if the C5M1 score screen stays silent again, the same scenario on b90 `318b8510208b97e0` decides whether the build is the cause.

2026-11-15 21:43 cards 401 and 402 accepted on run c401b93d (`cfdd06ae5462361c`, lane 2; lane 1 still b76). Chain passed through FED_C5M2, 0 WARN of either card, 0 mismatches. Open after acceptance: (1) run c's FED_C5M1 score screen that never typed did not recur in run d and has no established cause; the scenario keeps its typewriter try-wait and score shots, so the next occurrence is recorded. (2) `c23_collect` is slower per call than before card 402 (76 to 137 us against 47 to 101 in b91, about 200 calls per map); the 5 s whole check and the per-minute rebuild are the suspects, unmeasured. A longer `kC402CheckMs` goes with the next lane 2 build. (3) Host ext poll is now the largest part of `c132_ext_step`: 42 to 45 us per tick over 225 baselines (run c, FED_C5M1), about 0.25% of a frame; not carded.

2026-11-16 05:58 efficiency audit round, cards 403 and 404 accepted. Lane 2 holds `6a8da3341521cae9` (b95 = `cfdd06ae5462361c` + card 403 receive timing + card 404 name matcher + card 386 step 2); every scenario pin updated. Lane 1 and `build/dwmapi.dll` still b76 `90aa405f6b2dadbb`.
- Wire (run c401b93d, 181 s chain): mod ops about 460 KB, about 1.45 KB/s in a 330 s play log. Fx event rows average 31 to 35 B; the 2 to 2.8 KB Fx frames are the per-map asset tables; 1302 of 1741 WorldOp rows are the ext join replay at each load (about 17 KB per load). Encoders are already packed: no wire card.
- Per frame in `ubw_post_tick`: host 0.56 to 0.81 % of frame time, client 0.81 to 1.6 %. Largest client step is `client_significance_step` (the engine's own significance update).
- Card 403 found the real client cost outside the tick: receive handlers, 2.3 to 6.5 s per 30 s on the chain. Card 404 cut it to 0.33 to 1.0 s (tables in `docs/cards/done/404-name-match-without-text.md`). In play this was a 2 to 5 s client stall at each map load (join replay rows) plus Fx rows in a fight.
- Not mod cost: client `EndLevel` 0.9 to 1.9 s once per map (the game's own EndLevel); `auto_dropship_step` 830 ms (test-only skip flight).
- Card 386 step 2 (digest rounds 120 s apart in play, 30 s in tests) is in this build; rounds ran 30 s apart in runs c403b94b and c404b95a with 0 mismatches.
- A run started without `HOST_EXTRA_ENV` (`c403b94a`) sat in the menu and is void; the chain needs the scenario header's full command.
- Open: per-map name index for by-name finds (0.4 ms a find, load-time only); Fx receive 1 to 2.5 ms per frame after 404 (asset first use, engine spawn) not split further; follow-ups from 2026-11-15 unchanged.
- Play build: 404 is a client-side load stall fix. Cutting a play build from b95 is the user's call.

2026-11-16 06:21 efficiency audit round 2, cards 405 and 406 accepted. Lane 2 holds `da9d5d719936c8e2` (b98 = b95 +
405 + 406 diagnostic); all 17 scenario pins updated; lane 1 still play build 7 (`90aa405f6b2dadbb`). Card 405:
by-name finds compare the FName pair, raw reads inline; client WorldOp receive 103-167 to 67-94 ms per 30 s, mod
share of CPU samples client 1.21 to 0.89 %, host 0.71 to 0.51 % (run c405b96a). Card 406: client Fx spikes are the
game's own play calls (join replay Niagara activates, first gameplay cues), no mod fix. Measured and left: per-map
name index (0.45 % of client CPU, load time), `prop_find_prop` on the host (0.04 %), log writes (32 to 67 ms per
30 s at 4500 to 6000 lines, DIAG on), join replay frame budget. Host top steps unchanged: `c132_actor_ext`,
`c140_placed_visit`.

2026-11-16 06:36 card 407 accepted (per-map name index for by-name finds). Lane 2 holds `fa232da123925f82` (b99 = b98
+ 407); all 17 scenario pins updated; lane 1 still play build 7 (`90aa405f6b2dadbb`). Run c407b99a: client mod share
of CPU samples 0.89 to 0.47 %, host 0.54 %; every mission map find answered by the index, 0 `WARN card407`. Next in
the audit: client `Objective` receive (58 to 88 ms per 30 s, 3 to 6 ms per frame with a row), then `prop_find_prop`
call sites and the client level walks (`c37_step`, `c43_step`, `c50_step`, `c22_step`, 17 to 48 ms per 30 s each).

2026-11-16 06:49 card 325 closed (target-miss rows read). Lane 2 holds `7a7505c44a494c79` (b101 = b99 + card 325
miss line with owner class, key actor and world point + `host_key` placed fallback); all 17 scenario pins updated;
lane 1 still play build 7 (`90aa405f6b2dadbb`). Fix: a replicated placed owner with no NetGUID yet (map load, before
the first join) takes the placed key; the dropships' 60 `GroundLandingSmoke` join replay rows now play on their
components instead of spawning 60 new systems at stale points (runs c325b100a, c325b101a, both passed, 22 rows
`mismatches 0`). Other miss classes closed in the card table. New cards: 408 (plasma bug projectile never on the
client), 409 (pooled world Niagara Activate sent without its spawn row or parameters; diagnostic first).

2026-11-16 06:57 card 410 accepted, b102 `a685603c45e393f5` on lane 2 (17 pins updated; lane 1 still b76). Run
`c410b102a`: `c22_step` host 61.2 to 6.5 ms and client 59.2 to 8.8 ms over the fast chain, no call over 1 ms left;
`c72_client_step` 54.2 to 31.6 ms; `c37_step` unchanged within noise (86.3 ms, open). 22 rounds `mismatches 0`, WARN
classes unchanged. Card 409 step 1 line ran: see the card.

2026-11-16 07:28: lane 2 holds b107 `86e7f6eefa55c9b9` (b105 + cards 412, 413; all 17 scenario pins say it). Lane 1
and `build/dwmapi.dll` still hold play build 7 (b76 `90aa405f6b2dadbb`). Cards 411, 412, 413 accepted, see
`docs/cards/done/`. Card 412 settles the earlier "+90 s pass" note: the pass is DIAG only now. Card 413 settles the
`c28_try_attach` synchronous load note below. Still temporary in source: `prop_find_count` (`reflect.cpp`) and the
prompt diagnostic in `c135_prompt_step`. Seen in 5 chain runs, not traced: `WARN card072: host: AmmoMirror from
BP_PlayerController_C with no server copy inventory`. `c399-fast-chain.txt` now takes two card 413 body shots on
FED_C3M1.

2026-11-16 07:50, card 414 (`docs/cards/done/414-audit-batch-plate-sweep-lookups.md`): lane 2 holds b109
`08f358d6ef15bd85` (all 17 scenario pins); lane 1 and `build/dwmapi.dll` still hold play build 7, b76. Run c414b109a
passed (22 `mismatches 0` each side). Mod CPU share by perf: host 0.54%, client 0.47%. Source holds two edits past
b109 (heat offset cache in `c125_attr`, `memcmp` fast path in `c402_slots_step`); the next build is b110. Perf runs:
`runperf.sh` in the session scratchpad picks host and client by `UBWCOOP_TRAVEL=...listen` in the process environ.
Owed: a client screenshot with the host in view, for the name plate. The AmmoMirror note above is settled (plain row).

2026-11-16 08:05, card 414, builds b110 and b111: lane 2 holds b111 `eb5786d9f7844b74` (all 17 scenario pins); lane 1
and `build/dwmapi.dll` still hold play build 7, b76. This replaces the 07:50 entry's build lines. Runs c414b110a and
c414b111a passed (22 `mismatches 0` each side, no new WARN class). b111 adds the client gib tick skip and per-class
caches for the `Widget Stack` and dropship look lookups; the card file has the numbers. Mod cost now: 0.51% (host)
and 0.47% (client) of CPU samples; 0.67% and 0.87% of frame time. Measured and left in this pass: the wire totals
(about 470 KB per fast chain, 1.7 KB/s, same rows as the earlier audit); the pace queue bursts at map start (peak
352 to 513 frames, longest wait 734 to 1650 ms; the holds are "silent client", 43 to 98 of them, not the rate cap, so
batching frames would not shorten them); the first-tick spikes behind the load fade (`c132_digest_step` 10 to 16 ms,
the card 412 inventory); play build log volume (worst 30 s window 47.5 ms). Next build is b112.

- 2026-11-16 08:40, card 416 opened and assigned to the junior (user request: minimap blip for the teammate, plus a cyan
  ring under the teammate; the name plate stays, it costs 6 to 11 us per frame). Trace in the card: the player
  character does not implement `ISTMinimapMarkerInterface`, so the five `Execute_*` wrappers are detoured for teammate
  pawns and the game's own `ShowOnMinimap` and tick do the rest; ring is a local static mesh component. Build b112,
  lane 2 only. Proof: one fast chain run with a screenshot on each side (also settles the name plate screenshot owed
  from card 414). No source edits by the senior while the junior builds.

2026-11-16 08:42, audit: host `ext_sweep_tick` (card 132 ext sweep) measured and left. Run c414b111a,
FED_C4M1: 1438 candidates (266 ext) spread over 24 ticks per 450 ms quick sweep, 60.8 us mean per tick over 884
ticks (53 ms per map, 0.36% of a 16.6 ms frame), 2 ticks over 1000 us (max 1240 us, the whole sweep chunk in
level 7). Non-ext candidates already resolve through the per-class row cache (`g_c132_class_row`), so storing the
kind in the candidate would save one cached lookup each, under 5 us per tick. The two long ticks are the whole
sweep (every 16th), already chunked by level. No card.

2026-11-16 09:04, card 416 accepted (teammate minimap blip and cyan ground ring). Build b112
`096b066185f2c0da` on lane 2, 17 pins updated; lane 1 still b76 `90aa405f6b2dadbb`. Run c416b112a (1 of 3): ring and
blip rows on both sides on all 6 mission maps (host 12 rows, client 17 with 5 "tinted late"), no card416 WARN, 22
`mismatches 0` each side, FATAL and ERROR 0, no new WARN class. Screenshots on C3M1 show the cyan ring under the
teammate's boots and a cyan arrow on the minimap on both sides, and the name plate (settles the card 414 screenshot).
`c110_step` mean up 2 to 4 us (host 7.8 to 11.2 us, client 10.4 to 12.4 us) and one call over 1 ms per map (the ring
build frame). Not proven: whether the ring draws through walls (GizmoMaterial); check on the next play log or
playtest.

- 2026-11-16 09:14 Card 408 traced and rewritten (`docs/cards/408-satchel-bug-buildings-simulate-per-player.md`).
  The client does spawn plasma shots: its placed `BP_SatchelBuilding_Flakbug_C` / `_Plasmabug_C` runs the whole
  turn-and-fire state machine itself (two random timers and one `RandomBool`), so facing and shot times differ per
  player, the turn timeline is driven twice (local plus the host `L` row) and host effect rows land as target
  misses. Fix: host relays the two timer events, client refuses its own. Assigned to the junior with card 409's
  diagnostic changes as build b113.

2026-11-16 09:21 Class sweep after card 408 (placed non-replicated Blueprints with random functions): `BP_MortarZone_C`, `BP_RandomEmitterVolume_C`, `BP_TacSupp_OrbitalStrike_ScriptedLauncher_C` disassembled. Gap found and carded as **417**: the client makes its own launches through `SpawnPooledProjectile` (`exe+0x52502AC`: MortarZone 16, AIMoritaRound 5, CannonTurretRound 2 in `c416b112a`) on top of the host's card 132 copies, so it sees doubled rounds; and the host's mortar zone keeps clear of the host's player only. Emitter volume is per-listener ambient sound (no gap). Scripted launcher is left to the 104/105 relay, to check on a map that has it. Card 417 goes into the build after b113.

2026-11-16 09:24 CollisionOp "no placed copy matches" WARN (5 rows per chain run, client, C5M2) traced: no state gap. The 5 rows are `BP_PylonSpline_C` `NODE_AddStaticMeshComponent-N` collision 3 -> 0 writes from the map-load script (caller `exe+0x37C2C74`, 114 of the 375 journal rows on C5M2). The client runs the same script on its own copies (`card137: client change BP_PylonSpline_C ... 3 -> 0 by local sim`, 479614288, before the replay at 479617483), so its state already equals the host's; the replayed row only fails the component-name lookup (script-added component names are not stable across sides). Not carded. Candidate saving, unmeasured: the host journals 114 map-load script rows per map that every client reproduces locally.

2026-11-16 09:25 `WARN card140 ... kHitDrift` rows (1 to 7 per chain run, 303 to 1817 uu, Warrior, Hopper, AI Trooper) have one cause: every row in c410b102a, c411b105a, c413b106a, c414b109a and c416b112a lands 0.9 to 2.6 s after a `cmd: travel end -> done` (level-end screen). The client applies a backlog of mod frames in one 2 to 4 ms burst there while the copy's `ReplicatedMovement.Location` has not moved (three hits on guid 0x418 with three host locations, one copy location). No drift row exists during play in these five runs. Not carded; a drift row outside the 3 s after a level end would be a new class.

2026-11-16 09:26 Receive-path crash-safety audit (static, read of all 52 `sscanf_s` frame parsers in `mod/src/ubw` and the one binary receive, `c136_receive_fx`): every `%s` and `%[` carries a width one under its buffer and the buffer size argument; every wire value used as an index is range checked before use at the sites read (decal material and target index, `kC161BufferMax`, `kC95MaxCollisionValue`, tac charge slot loop bounded by `kC104ChargeSlots`, marker value and tag counts); the Fx blob goes through `Reader` with a byte bound and drops the rest of a frame at a bad row. No finding. Not checked: non-finite coordinates (`nan`, `inf` parse as `%lf`) passed on to engine calls; left, as the session is friends-only.

- 2026-11-16 09:31 Placed level sequences traced (static plus c416b112a): no sync gap, not carded. The host inventory
  lists placed `LevelSequenceActor`s with no mechanism on FED_C3M1 (1), FED_C3M2 (4), FED_C4M2 (2) and FED_C5M1 (1).
  Their assets are `/Game/Maps/Sequences/FED_C3M1_Outro`, `FED_C3M2_Intro`, `FED_C3M2_NukeExplosion`,
  `FED_C3M2_TACFighter_01`/`_02`, `FED_C4M2_BrainBugIntro`/`_B` (C5M1's is not in its scripting sublevel). The
  disassembled level scripts of all four maps (`*_Scripting.umap`) hold no call that plays a sequence or touches a
  sequence actor: the C3M2 `CreateLevelSequencePlayer`/`GetSequencePlayer` names are unused locals, the nuke is Niagara
  plus sound plus camera shake (`NukeExplosion`, `NukeExplosion_Shake`), level ends go through
  `STGameInstance:EndLevel()`. c416b112a loaded all four maps on both sides and the `UMovieSceneSequencePlayer::Play`
  hook (`cine_skip.cpp`) logged 0 rows, so none auto-plays. Across every log the hook has only ever logged frontend
  sequences. Residual: the hook covers `Play` only; a `card130 ... level sequence Play` row on a mission map in a
  play log would be the first evidence that one of these is live.

- 2026-11-16 09:33 Collision journal join replay measured (closes the 09:24 candidate): c416b112a FED_C3M1 join window
  `netbudget` row shows CollisionOp 12065 B in 135 rows (89 B per row), beside WorldOp 24314 B / 301 rows and Fx
  17070 B / 29 rows. It is paid once per client per map; dropping the map-load script rows the client reproduces would
  save about 10 kB once. Not carded: no per-frame or steady cost.

2026-11-16 09:42 Blueprint effect thunks audited against the capture points (static: registration table, disassembly,
import sweep of 1679 Blueprint packages). Two script thunks inline the native body and bypass the detour while
Blueprints use them: `execSpawnSystemAtLocation` `exe+0x4849050` (8 Blueprints: flak and plasma buildings, missile
silo, command post, electric pylon, AI frag grenade, resupply rack, frontend spawner; this is card 409's "pooled"
Activate, step 2 design in the card) and `execSpawnSoundAtLocation` `exe+0x3C8A340` (4 Blueprints, card 418). The
other inlined thunks (`PlaySoundAtLocation`, both `SpawnEmitter`, `UGameplayStatics` decals) have no Blueprint user.
Attached Niagara, both `WithParams`, attached sound, ST sound library, ST decals and `STSpawnSystemAtLocation`
thunks call the detoured natives. Tool: scratchpad `tools/thunks.py` (name to thunk address).

2026-11-16 09:52 Full-hook thunk audit closed and b115 reviewed.
- Thunk audit over all 404 hooked natives (105 thunks reach the hook, 83 looked like misses, 216 have no same-named
  thunk): every apparent miss is a virtual or register dispatch, a wrapper that reaches the hook, or a same-named
  function of another class. No new capture bypass beyond cards 409 and 418. One latent case:
  `execSetGamePaused` `exe+0x3C85BA0` inlines the body and passes `detour_c96_set_game_paused`, but none of the 1980
  swept Blueprint packages imports `GameplayStatics:SetGamePaused`; widget Blueprints (WBP_*) were not in the swept
  set. `SetGlobalTimeDilation` is imported by `BP_BugGameplayCutscene` only (bug campaign, not hooked).
- b115 `d552217f60765a2b` (run c408b115a, 0 mismatches): the b114 host crash fix (`thread_local` ProcessEvent
  context with `C128PeScope`) is accepted; its per-ProcessEvent cost is to be measured in the next run. Card 417
  accepted (done). Card 408 stays open with findings F1 to F4 in its card. New card 419: plasma rounds burst about
  22000 uu apart per player (random lifetime and spread drawn per machine), low priority.
- Next lane 2 build (junior): card 409 step 2 + card 418 + card 408 F1 in one build, 3-run budget.

**2026-11-16 09:59 Efficiency audit: widget sweep, footsteps, WorldOp.** (1) Widget Blueprint sweep closes the
`execSetGamePaused` inlined-thunk question: 303 widget Blueprints (`W_`, `WBP_`, `UI_`), none imports `SetGamePaused`
or `SetGlobalTimeDilation` (matcher validated on a known positive); no Blueprint user in any swept set. (2) Bug
footsteps are not sent twice: the `GameplayCue.Impacts.Footsteps.BugWalk` row (43 B) and the `NS_FX_Bugs_WalkGround`
row (21 B) are two outermost events of one step, and of 16 logged client spawns 15 are unique places (a nested double
would pair every one). No change. The hit relay is 1680 B per run: no change. One-shot Fx rows beyond 8000 uu stay
as card 329 judged them (7%, too small). (3) New card 420: the ext join replay (`c132_ext_replay`) is 1300 rows,
about 104 kB of the run's 144 kB WorldOp and 219 of the 349 frames in the paced join burst (longest wait 1833 ms),
while 383 of 384 logged client applies are "already equal". Fix: packed (name hash, value) frames, client compares
locally. Goes in the lane 2 build after the junior's 409/418/408 batch.

**2026-11-16 Card 421 (same audit pass as 420).** The collision join replay is 1041 rows, 94 kB of the run's 95 kB
of CollisionOp; the client's own load makes the same writes and only 11 replayed rows changed its copy. Fix: the
client journals its own writes, the host sends packed (row hash, value) frames, the client asks for the rows that
differ. Cards 420 and 421 together remove about 198 kB of the 542 kB the host sent in c408b115a (36%) and 354
of the 356 frames paced in the map 1 join window (longest wait 1833 ms today).

**2026-11-16 10:35, senior: cards 420 and 421 accepted; lane 2 holds b119 `6a3adf36b52871d8`** (b116 plus cards 420
and 421; the 18 scenario pins are on it; lane 1 still b76). Runs c420b117a, c421b118a, c421b119a (c399 fast chain).
Host channel join cost against c408b115a: WorldOp 144430 B to 59286 B, CollisionOp 94909 B to 23326 B, map 1 join
queue peak 349 frames to 59, digest 0 mismatches, FATAL 0. Card 421's premise was wrong (client copies differ; the
`card095: client ... applied` change lines are rate limited by `c95_line`, so never count them as a total); the
accepted form writes pairs directly. Open from these runs: (1) `c399-fast-chain.txt` line 332 failed in 2 of 3 runs:
the card 408 plasma block warps to the building's `front`, which depends on its live yaw (-57.7 fell through the
world, -144.7 held); with the junior. (2) Paced frames still wait 800 to 1824 ms in windows with
`holds: ... silent client N`; not yet traced. Detail: `docs/cards/done/420-*.md`, `docs/cards/done/421-*.md`.

**2026-11-16 10:40 Efficiency audit: silent-client holds, frame prefix.** (1) The 650 to 1824 ms paced waits under
`silent client N` holds are the card 391 gate working: each window holds a client frame of that length (`card303:
client ... frame of 913 ms` and `976 ms` when the level-end screen is built, the map load hitch at map start), and
the host has a frame of the same size at level end (890, 1055 ms). The client reads nothing in that frame, so the
wait costs nothing. No change. The level-end frame is the game's own widget build on both sides. (2) New card 422:
the frame prefix is 6 units on every frame and one is enough; 5 units is 10 B per blob frame, about 1.2 MB of the
playtest's 11.35 MB of packed text frames. Patch written and held (senior scratchpad) until the junior's
408/409/418 batch reports. (3) The junior built `d74ad9c849fde45c` at 10:37 (not on lane 2, no report yet).

2026-11-16 10:43 Efficiency audit: Dropship, grenade and gib bytes (run c421b119a). No change.
- Dropship 43873 B is 276 rows at 159 B: 9 join (J) and 5 check (C) snapshot rounds of 18 dropships, one row per
  frame; only 19 live (L) frames. The C round is about 3 kB per map join and is the only source of the dropship
  mismatch WARN class in a play log, so it stays.
- GrenadeSpawn 17102 B / 193 rows = 89 B and GrenadeExplode 10746 B / 108 rows = 100 B: text rows whose asset paths
  already go through the card 150 dictionary (a raw mortar path alone is 112 chars). Together 7% of the run.
- GibPart 17798 B / 139 frames = 128 B, under its own 16384 B/s budget (card 140).
- Largest open byte item stays card 422 (frame prefix); Fx stays 45% of host bytes (card 329 judged its rows).

2026-11-16 10:44 Efficiency audit: whole wire and frame time (run c421b119a). No new card.
- Host wire by the engine's own counters: 1020095 B. `ClientMessage` (the mod channel) 430139 B in 2823 calls, 42%; the
  channel's own count is 378756 B, so the RPC layer adds about 18 B per frame. Card 422 takes 10 B more off each.
- `BP_FrontendTrooper_Base_C` and `_Unarmed_C` props, 199087 B (20%), are the 49 placed troopers of `3D_Frontend`:
  one 5 s window of about 50 kB per visit (initial replication, about 45 properties each), 0 not-net-ready frames.
  Engine replication of the lobby map, left alone.
- Mod frame time: host 0.58 to 0.80% (the 3.7% windows are the test-only `auto_dropship_step`), client 0.60 to
  1.09% with `client_significance_step` first (open user question). ProcessEvent hook 10 us per frame.
- Host `Pong` receive 9 to 15 ms is the join replay start, once per map. Client Fx receive 26 and 65 ms frames are
  card 406's rows (join replay of ambient bug splines, first gameplay cue). Digest 13 to 16 ms on both sides is card 386.

**Efficiency audit: send-to-apply latency (run c421b119a, 2026-11-16 10:48). No card.** Client `card132: recv`
rows, 1772 frames: p50 12 to 28 ms for every op. 164 frames applied 200 ms or more after the host's send stamp. 142
of them fall inside a logged long client frame (`card303: client ... frame of N ms`, 18 in the run: map joins of
about 1.9 s, level-end widget builds of about 0.9 s, both game-side). The other 22 sit in three bursts of 207 to
284 ms, each in the frame that runs the scenario's `shot` command (`cmd: N shot ... -> done`), below the 500 ms
watchdog line: test-only. No frame was late outside a client frame stall, so the channel adds no delay of its own.
Host side agrees: the 8 pace windows with a longest wait of 650 to 1824 ms are all `silent client` holds (38 to 109
per window, `bits` holds 0 to 5), that is, the host kept frames back while the client sent nothing.
The name index (card 407) answers every find (for example 261 of 261) and builds once per map in about 4.7 ms.

**Card 423 opened (2026-11-16 10:54, open-card sweep): `BP_DestroyableActor_*` has no sync on any map.** Cave gates,
hive entrances, bridge parts, Plasma nests and shields are destroyed by the level script (`LD: Destroy`, a
Blueprint-to-Blueprint call no hook sees); host inventory says `mechanism=none` for the whole class. A joiner or
reconnecting client loads them alive. Fix spec: an 11th `kC132Ext` row on the Blueprint bool `IsDestroyed` (host
poll, 'X' row, replay pack, digest), client applies with the actor's own `LD: Destroy` (live) or `Setup Destroyed`
(replay). Same batch: the card 299 nest diagnostic row, never coded. Done when: one FED_C2M1 run shows 3 host and
3 client `card423` rows, a client screenshot of an open gate after a reconnect, 0 digest mismatches.

**Open-card re-read (2026-11-16 10:56): cards 197, 269, 223. No new code.** 197: Chariot corpses far from the host are
covered by the card 269 rest rows (12 of 12 arrivals in `c243-c3m2-b6r2`, 0 not-posed WARN); Jumper non-gib and
M7Mech deaths still unseen in any log. 269: its read-back prediction failed (`IsPlayingAnimation 1` on all 13);
hold or loop is undecided, deciding step (frame at 1 s and 3 s plus a client shot) written in the card for the
next FED_C3M2 soak. 223: 49 of today's 51 hit drift WARN rows are applied right after a 1 to 2 s client frame at
a level end or join (the game's own hitch, same on the host), the other 2 are at the 300 uu line; the pace queue
is out of it. Note in each card.

2026-11-16 11:06 senior: junior batch reviewed (b120 `d74ad9c849fde45c`, b121 `821e36ce8e035086`, b122
`1f93bcc7ec325ff2` on lane 2, 18 scenario files pinned; lane 1 still b76 `90aa405f6b2dadbb`). Accepted: 408 F4/F5
(F5 kept), 409, 418. Card 422 stays open: Ping/Pong 19 B (was 24), Fx frame 165.0 to 160.0 B (prediction 2 was -10;
packed Fx frames are narrow, 1 B a unit), digest frames not exercised; proof comes with card 423's FED_C2M1 run.
Open items: white phosphorus Blueprint users of card 418 not traced; 51 card325 target misses on WarriorBug guid 2076
in the last 3 s before quit in c399b122a (never spawned on the client), watch in the next run.
New cards from the client WARN classes of c418b121a and c399b122a: 424 (held final with gravity on throws the
corpse; 3 bad of 10 against 0 of 44 with gravity off), 425 (collision rows drop the component FName number, 5 rows
fail at the C5M1 join), 426 (two `BP_ProceduralDropshipTroops_C_0` on C5M1 share one key, one copy flips rotation
every 5 s). Patch for 424 and the card 279 step 1 diagnostic handed to the junior as a script (anchors checked).

2026-11-16 11:20 senior: card 427 written (efficiency, low priority): GrenadeSpawn 194 B and GrenadeExplode 93 B a
row, 2.0 KB/s under mounted gun fire, from full asset paths and a `%.6f` quaternion. Dropship check rows measured:
one check round per join, 27336 B in the whole chain run, no card. Card 426 text corrected to match (one check round,
not periodic).

- 2026-11-16 11:13 card 359 raised: corpse bones frozen up to 278.7 uu off the bodies on the client (c418b116c
  0x12c; 6 copies above 20 uu in the last 60 client logs), drawn wrong. Bone 0 goes constant in component space
  during the stream while the bodies follow the host. Tick option, bone count, InitArticulated at death and the
  game's SetAllBodies call do not separate bad from good. Step 1 is a diagnostic on the WARN row (end tick count,
  tick function bytes); the fix follows its answer. Text in the card.

- 2026-11-16 11:16 open-card re-read, part 1: cards 305, 331 and 332 closed on today's logs (64 logs: 0 WARN rows of
  their classes, 0 `seq gap`, 0 `result no handler`), moved to `docs/cards/done/`, rows in CARDLOG. All WARN
  classes of today's logs have an owner: card346 guard rows (host, known), card132 `host hit ... uu from the NPC`
  (known, the event point is not on the body), `CollisionOp ... no placed copy matches` (last seen b117, gone with
  card 421), pose mismatches of c418b121a (card 424), card137 gib rest (card 279), card273 (open), card152 and
  card191 bone rows (card 359), card159 (cards 425, 426), card140 hit drift (card 223 leftover, patch
  `c223_patch.py` written for the next build), card389 falling rows (FAST drop, by design).

- 11:18 2026-11-16 open-card re-read part 2: cards 231 (not reproduced since card 152, recurrence goes to card 359) and 233 (class of card 271, proof rows owed there on the FED_C4M1 soak) closed. Checked and left open with the proof each still owes: 317 (thrown flare rows: no flare thrown in today's chain runs), 320 and 314 and 296 and 302 and 304 (user playtest), 321 (FED_C3M1 has 0 streaming volumes and 0 gated levels in `c399b122a`; the card needs a map that streams), 311 and 313 (range targets, FED_C1M1 reload run), 328 (map totals on a soak), 308 and 309 (soak peaks on C2M1).

- 11:24 2026-11-16 card 359: diagnostic read on b124 (`c423b124a`): end tick runs on all 90 bad copies (11 of 11), so the cause is the measuring point, not a missing blend: the step freezes with the animation's pose in the bones (worst 437 uu). Fix patch `c359_fix_patch.py` freezes right after the end tick's blend; proof = 0 `WARN card191 ... frozen with bone 0` rows on FED_C2M1 plus a client shot. Detail in the card.

- 2026-11-16 11:30 senior: new client WARN class `card132: recv ... text R <guid> result skipped: no row` (14 rows in
  `c423b124a`) traced: the host's pose table and rest table each send an `R` for one corpse and the client drops all
  three rows on the first. No visible effect. Card 428 opened with a patch script (`c428_patch.py`, one `R` per
  corpse); done when a FED_C2M1 run shows 0 such rows. The client ledger prints only part of its applied rows (94
  removals, 25 printed), so a missing "applied" row is not evidence that a frame did not arrive.

- 2026-11-16 11:37 card 359: b125 fix prediction failed (91 WARN rows, bone error equal before and after the end
  tick's blend, constant per guid). New hypothesis: body `PhysicsBlendWeight` (+0x134) below 1. Patch
  `c359_weight_patch.py` (weight 1 on every waited tick + weight in the rows) handed to the junior for b127; proof
  and refutation rows in the card. b126 `b72f9aeefee05d90` (junior, card 428 in) on lane 2, run `c423b126a`.

- 2026-11-16 11:39 card 423 review (c423b125a): 1 false digest mismatch while a live destroyable row waits
  `kC423WaitMs`; patch `c423_digest_patch.py` handed to the junior for b127 (finding and proof rows in the card).
  Shared key c30860a2 (cave gate and cliff) traced: compare is by (key, level), rows resolve by name + nearest; clean.

- 2026-11-16 11:42 card 269 open item: diagnostic patch `c269_readback_patch.py` handed to junior for b127 (two read-backs, 1 s and 3 s, each logs animator static mode, time, length). Decision rule in the card: same time at length = held, drop WARN; time moves = loop, stop the player on the last frame.

- 2026-11-16 11:46 junior report (b126 `b72f9aeefee05d90`, run c423b126a): digest 10 rounds 0 mismatches; card 428: 0 `no row` WARN (13 in b125), host-side count owed from b127 map-end totals; card 223: 0 drift rows; scenario line 426 failure = host warped onto the client's point and fell (scenario fixed). b127 `74c1fdca882992af` = b126 + c359 weight + c423 digest + c269 read-back + card 299 corpse-pile skip, run c423b127a live.
- 2026-11-16 11:46 card 279: step 1 rows read (misses only on parts with physics off by significance); step 2 patch `c279_physics_patch.py` and card 423 `c423_wait_patch.py` (wait 2000 to 250 ms) handed over for b128.

- 2026-11-16 11:50 senior: card 273 reopened by log: 16 of 19 host sticky WARN rows in c423b125a are state 0 (in
  flight, never hit), which waits the full 30 s for its R row; fix `c273_state0_patch.py` (state 0 rests when the body
  is asleep or not simulating). Card 299: nest Blueprint has the `STMeshUpdate` node but the lookup returns none;
  `c299_owned_patch.py` lists the nest's owned components by class. Both go in b128 with the card 279 and 423 patches.

- 2026-11-16 11:53 senior, efficiency audit (run c423b126a): card 429 written. The client re-places a flying
  dropship copy on 372 of 473 live frames, by 120 to 570 uu at 8500 uu/s (one to four frames of timing noise against
  a fixed 100 uu limit). Fix `c429_dropship_dist_patch.py`: limit grows by 50 ms of travel. Goes in b128. Sending
  fewer L frames was examined and rejected (speeds change between frames). Host WARN classes left in b126: card046
  rows are the scenario's fall under the map (scenario fixed by the junior), card238 1 row is the handled bad bone
  FName, card132 2 rows (impact 2060 uu from the NPC) not examined.

**2026-11-16 11:56 WARN review, run c423b126a: host `card132 ... impact is N uu from the NPC`. No card.** 2 rows, one
`BP_AI_WarriorBug_C` (guid 0x412), 2060 and 2348 uu, damage 8.0, hit bone None. The client applied both rows 34 ms
later at the same point (`card132: client hit ... OnDamage ran`), no `card140` drift row: the game's own event point
is off the body on the host too (hb47), and both sides place the same gore. The class is in
`logs/b72-warn-classes.txt` line 7. Card 273: only 1 row was state 2 (part 442, 30159 ms), not 3. The tick
(0x14513AE50) in state 2 counts +0x330 up to +0x334, then calls two component virtuals each tick with no state
write in that branch; whether the timer had ended is not known. Patch `c273_timers_patch.py` (b128) adds both
timers to the forced-rest row; done when one state 2 or 3 row shows them.

**Run c423b127a (b127 `74c1fdca882992af`) VOID from line 190, cause logged; card 430 opened (2026-11-16 12:03).**
The client's nuke launcher did not auto reload after shot 1 (client WindDown end 489964097, host copy reloaded
489964107, client reload only at 489975831, 7 ms after the host copy's phantom second SpoolDown ended). The next
6500 ms press was used up by that reload and an aborted windup (`WARN card296 ... released inside the windup`), nest
`_C_7` was never hit, `LOS_O02_SO05` stayed 1 of 2 and the later objective steps cascaded. Not environmental. Unknown:
tried-and-failed or never tried (card 021 rows capped). b128 gets `c430_reload_try` (patch
`c430_reload_try_patch.py`, check ok on b127 source) and the scenario waits on the reload's AnimationEnded row before
the second press. Tail fails 405-438 of that run not yet read apart from the cascade.

Run c423b127a tail fails 405-438 are cascade; card 427 re-measured (2026-11-16 12:06). Host log has no
`CompleteObjective(LOS_O02)`, so `AddObjective(LOS_O05)` (line 405) never came and the dead-wait cap cut later waits
to 1 s (421, 424, 425, 428-431, 438). No own cause. Card 427: GrenadeSpawn and GrenadeExplode are already packed ops;
b127 rows are 73-76 B and 76 B, so the path table fix is dropped; leftovers are in the card (explode token list,
`c74_find_asset` timing row, `%.4f` quaternion).

Efficiency audit: frame fixed cost measured, card 431 opened; card 269 pose hash diagnostic handed over (2026-11-16 12:09).
Every blob frame costs 22 B fixed, 10 B of it the ledger stamp: 9.0 % of mod bytes in c423b127b, 7.5 % in c399b122a.
Card 431 holds the two fixes (short stamp, or card 151 batching); parked until after the playtest. Digest is 86 B a
30 s round unless sums differ (card 329 step A holds). Fx is 47 % of mod bytes on FED_C2M1 (1.8 MB of 3.8 MB in
c423b127a); the per asset `card329: host fx map` rows need a run that ends the map (b128 c423 run does). Card 269:
b127 read-back rows ("static mode 0, time 0.00 of 0.37 s") decide nothing; patch `c269_posehash_patch.py` (b128)
adds a pose hash to both reads. Done when: equal hashes per guid on both reads plus one client shot of a rest corpse.

**2026-11-16 12:15 — card 432 opened (objective replay cost), chain c399b127a reviewed.** Client pays about 1 ms per replayed Objective row (c423b127b: 78 frames 81.8 ms max 10.95 ms; 155 frames 112.0 ms); C2M1 scripting repeats whole SetObjectiveVisibility bursts, 27 of 95 host rows equal the last row for the id. Patch `c432_objective_patch.py` (scratchpad, for b129): host sends no row when marker and list fields are the same before and after the call; client times the game's call per row. Done when a C2M1 run shows the skip count, fewer client Objective frames, digest 0 mismatches and the totals row says where the time goes. Chain c399b127a passed (all join checks match, WARN card159 0, digest 0 mismatches); card 424 proof is vacuous (0 "held one tick" rows) and card 279 had 0 rows. Open from the chain: on C5M2 the client had 14 `card325 target miss` rows in the 7 s before quit for NPC guids 2050 and 2168 it never held, both within 12,000 uu of the client pawn; card098 spawn rows are capped so they do not show whether the actors were late or never came. Next chain holds C5M2 for 40 s so a digest round runs there; prediction: misses stop within 5 s of the map's first NPC spawn and the round has 0 mismatches. A miss later than that gets carded.

**2026-11-16 12:20 card 433 opened (client fx play time by point, diagnostic), audit findings not carded.**
Card 433 (`docs/cards/433-client-fx-play-time-by-point.md`, patch `c433_fx_play_time_patch.py` in the senior
scratchpad, goes in b129 with card 432): the client times the game's own play call per fx point and prints
`card433: client fx play time` rows at map totals. Done when a C2M1 client log shows the rows; they decide
whether any fx class is worth a cull or a cheaper path. Findings measured but not carded (each waits for a number
that says it matters):
- Dropship: mod `Dropship` op is one 169 B `L` row per moving placed ship per second (`kDsLiveMs` 1000), 25 rows
  per 5 s at the C2M1 peak = 0.85 kB/s; engine props for `BP_ProceduralDropshipTroops_C` 2.2 kB/s. Link carries
  33 kB/s of 473 kB/s allowed. No change: the L rows are what keeps the client flight within 10 uu (card 342).
- Fx bytes on C2M1 (`c423b128a` host, `card329: host fx map`): 19,054 rows in 7,002 frames. Top assets: trooper
  rifle muzzle flash 3,904 rows 82 kB (21 B/row), rifle sound `MS_WPN_AI_Morita_Mk1` 1,955 rows 80 kB (41 B/row),
  `GameplayCue.Impacts.MoritaRound.AI` 2,040 rows 68 kB. One-shot rows by distance from the receiver: under 3000
  uu 1,898; 3000-8000 1,410; 8000-20000 3,157 (119 kB); over 20000 1,554 (57 kB).
- One-shot sounds sent past their own audible range (client `playlog: audio row` sample, 818 rows): all 86
  `MS_EMIT_Ambient_Morita_Fire` rows (attenuation `ATT_EMIT_Occluded_200m`) at 21,167 to 117,189 uu, median
  105,895; 30 of 31 `MS_EMIT_Plasma_Explosion_Distant` over 30,000; 15 of 16 `MS_EMIT_Dropship_Flare_Fire`
  (`ATT_EMIT_Generic_100m`) over 30,000; warrior bug barks and attacks at a median of 14,000 uu. A host cull of
  non-looping one-shot sound rows beyond FalloffDistance plus shape radius (read as `c162_log_asset` does, only
  when bAttenuate is 1) would change nothing audible. Bytes saved are small (about 40 B/row, an estimated 0.15
  kB/s); card it only if card 433 shows the Sound point costs real client time per row. Looping and journal rows
  must stay: the listener can walk into range.
- GrenadeSpawn receive about 190 us/row (pooled spawn); card 417 makes the client spawn its own launch, release
  it, then spawn the host copy: two spawns per client grenade, about 0.3 % of a frame. GibPart receive spikes
  8.78 and 11.15 ms (two rows in `c423b127b`). Host `c132_decal_step` scans the pool every frame (memcpy, qsort,
  bsearch per live decal), 20 ms rising to 72 ms per 30 s window as decals accumulate; `c132_ext_step` 75-98 ms
  per window. Whole mod on the host: 1.1 to 2.0 % of frame time.

2026-11-16 12:28: card 269 root cause found, card 434 opened, b128 reviews.
- Card 269: the timed play's float is the sequence's total play time (rate = length / time), not an offset; the host age
  stretched each rest death over 15-22 s. Fix plays over 0.05 s (patch for b129). Proof: read-back 1 `IsPlayingAnimation 0`,
  `still playing 0`, client shot with lying corpses.
- Card 434 (new): C5M2 misses (62 card325, 61 card041 in 22 s) are for NPCs 10.4-13.7 k uu away that the client does not
  have. Step 1 prints the net cull distance on card098 rows; step 2 (host skips Anim rows out of range) waits on it.
  Card 325's 5 s prediction failed for this reason; digest 0 mismatches held.
- b128 C2M1 (c423b128a): card 423 pass (destroy row 382-525 ms, 0 WARN); card 273 pass (1 sticky WARN, state 2); card 279
  accepted with residual 1 of 237 parts at 12.2 uu (physics switched off again after the snap); card 430 stays open (the
  build was diagnostic, the reload took 1 ms this run); line 290 fall-zone fix works.
- Card 429 chain (c399b128a): join checks 7/7, 14/14, 2/2, 6/6, 18/18, 13/13; WARN card159 0 both. C2M1 applied 219 of
  443 live frames (was 372 of 473): the "under a quarter" line is not met, the rest are acceleration phases. Accepted as
  is; the one C2M1 `WARN card159` was a check that raced a take-off state change (engine replication ahead of the poll).
- Host totals "owed 16, owed sent 0" (card269) and "5 owed (0 sent later)" (card177) are not a loss: the rows got their
  guid while no PC was ponged and the join replay sent them (12 of 12).
- New in the chain: client `WARN card152` x9 on C3M1 (guid 0x50 pelvis 162 uu from body 0); not traced yet.

- 2026-11-16 12:34 (senior): card 424 not proven; residual traced. On the C2M1 reconnect every corpse whose freeze
  waited a tick ends 300-1060 uu off the host (b128 5 of 7, b129 9 of 9); the b129 digest round 9 `Pose` x9 is exactly
  those 9 guids (not card 269). Bodies move during the waited simulating tick. Patch `c424_repose_patch.py` (pose the
  bodies again and wait one more tick) goes in the build after b130; prediction in the card. b129 (`674d1a093bbc6c6a`)
  C2M1: card 432 sends 66, journal replay 137 frames (was 323), client objective replay max 1.73 ms; card 433 client
  FX rows: SpawnSystemAttached 254 ms / 2389 rows and ExecuteGameplayCue_NonReplicated 225 ms / 1434 rows are the two
  largest, nothing over 6.6 ms per row. Card 430: reload after a pickup into an empty launcher is game behaviour
  (no auto reload); scenario presses R after the pickup.

- 2026-11-16 12:42 senior: correction, card 430 is NOT game behaviour (the 12:3x line above is void). b129 rows:
  the client's reload try at the nuke WindDown end returns 0 under `WeaponSystem.Blocked` + SpoolDown and the game
  did not try again for 27.7 s on shot 1 (own retry after 1017 ms on shot 2); host copy reloads 10 ms after its own
  WindDown end. Fix patch `c430_reload_retry_patch.py` (bounded client retry when Blocked clears: 5 tries, 100 ms
  apart, 3 s) for b131, prediction in the card. Card 359 step 3 refuted (91 WARN, weights 1.000); step 4 host-side
  bone 0 row `c359_host_bone_diag_patch.py` for b131, compares the offset by guid. Cards 425, 426 accepted on b128
  (`c399b128a`: 0 collision `no placed copy` WARN, C5M1 13 of 13), moved to done. b131 patch list: c424 repose, c430
  retry, c359 host bone diag, 432 totals count, 299 AudioComponent naming.

2026-11-16 12:48: b130 results. C2M1 (`c423b130a`) and the chain (`c399b130a`, passed in 215 s, rc 0) are in.
Card 269: the animation prediction failed; cause found in the exe (deferred static-to-skeletal swap, animator +0x15C
frame count, never ends on a copy that arrives dead, so the corpse's skeletal mesh stays bHiddenInGame). Fix patch
`c269_swap_back_patch.py`, prediction in the card. Card 434: step 1 failed (net cull 15000 uu on all 2899 rows); the
exe has no ReplicationGraph and no game IsNetRelevantFor override; on C5M2 4 WarriorBug guids at 9275 to 15104 uu
never reached the client in 12 s while the host sent Anim rows for them. Step 2 diagnostic
(`c434_no_guid_diag_patch.py`, `card434: host ...` rows) goes in b131. Card 430: b130 repeated the failed reload
(109 tries, no retry); the retry patch is in b131. WARN on the chain: host 346 x10, 132 x3 (HopperBug 0x216 impact
1525 to 1709 uu from the NPC); client 132 x1, 104 x1; no card152.

**2026-11-16 12:55 audit of b130 logs while b131 runs (senior).** Card 434 step 2b patch handed over
(`c434_view_point_patch.py`: view point distance, actor flags, root collision; card file has the readings). Card 148:
the client-only "asleep above the ground" WARN rows come from host and client probing from different bounds; patch
`c148_same_bounds_patch.py`, prediction in the card. Looked at and closed with no defect: host
`WARN card132 ... HopperBug guid 0x216 impact 1525-1709 uu from the NPC` (host-side AI trooper fire on a flying hopper;
the client's three `client hit` rows carry the same points and print no drift WARN, so both sides agree; hb47 0x138
class); client `WARN card322 ... no relay` x10 on C2M1 (second map load: the client's level script replays 10 radios
inside 0.5 s and each start is skipped, as wanted; late-join tally); one-shot sound distance (card329 buckets: 312 rows,
11996 bytes for the whole C5M2 map, nothing to cull); C5M1 step costs (host top real step `c132_ext_step` 52.8 us mean,
client `client_significance_step` 105.1 us mean, already an open user question). Round 1 launch tallies differ by side
(MortarZone 38 host, 34 client) only because the rows print 2.2 s apart and count from each side's own map load.

2026-11-16 13:03 b131 `86739fe09499bfb8` run `c423b131a` (C2M1) read; b133 batch handed over (senior)

- Run: 311 s, rc 0/0, 0 FATAL/ERROR, no dumps. b132 `eb258efd03a7a7cd` = b131 + card 434 view point fields; the
  junior runs chain `c399b132a` on it.
- Card 424 prediction met: 7 `posed again (0.0 uu after)` rows, 32 freezes with 0 over 2 uu, 0 `WARN card140` pose
  freeze rows (b130: 20).
- Card 432: host totals row present (`not sent 29 (changed nothing), sent 66`); client 192 replays, 46.6 ms, max 1.47 ms.
- Card 430: no retry was needed (0 `reload retry`, 0 `WARN card430`); the retry path is still unproven.
- Card 269: 32 `swap back run at once`, 0 `mesh already hidden`; host and client shots of the Chariot corpse match.
  The 28 `WARN card269` read-back rows are a false alarm: IsPlayingAnimation stays 1 on a held pose (same pose hash on
  both reads). b133 changes the WARN to "pose hash differs between read 1 and read 2".
- Card 359: 54 of 54 joined guids read bone 0 on body 0 on the host (0.0 uu), up to 52.6 uu off on the client with
  exact bodies and blend weight 1. Guid 0x4f0: host component 344 uu from body 0 on both snaps, client 1.1 uu at snap 2
  and 40.5 at snap 3. Step 5 (`c359_client_scale_patch.py`) adds the client's component distance, scale and body 0
  simulating to the WARN row. Reading to test: the offset grows with where the client's component sits.
- Card 148 fix (`c148_same_bounds_patch.py`): both sides probe from all-component bounds. Predicted: 0 `WARN card148`.
- Card 140 hit drift fix (`c140_posed_root_patch.py`): b130 C2M1 had 13 `WARN card140: client hit ... 324 uu from
  the copy's ReplicatedMovement.Location` rows on guid 0xd0, a corpse held by a host pose row. `hit_drift` used
  `c132_ragdoll_root`, which needs body 0 simulating; a held copy is kinematic, so it compared with the actor
  location. New `c132_posed_root` reads body 0 of a copy with a pose row. Predicted: 0 such rows on a posed corpse.
- Card 434 step 2 (b131): 80 host rows, all WarriorBug, RemoteRole 1, NetDormancy 1, net cull 15000; 70 over
  15000 uu from the remote pawn (never replicated: engine behaviour), 8 nearer rows inside one spawn wave (rows sent
  on the spawn frame, before the NetGUID exists). Client handles the FX misses by playing at the world location.
- New card 435: a rejoining client replayed every objective banner of the mission (journal replay keeps the popup
  bools and callouts). Fix written (`c435_join_popups_patch.py`).
- Closed with no defect: client decal pool WARN `cap grown to 501` (host pool at its cap 500, client one short for one
  round; rounds 6 to 10 all match).

- 2026-11-16 13:10 audit while b133 runs (senior). Bytes: C2M1 b131 numbers added to card 329, no fix (NPC props
  11.1 kB/s, mod channel about 14 kB/s, no row format or rate stands out). Per frame: no new step above the known
  ones. Card 424 accepted (CARDLOG). `c399b132a` card 434 rows: all 80 lines went on C3M1 and C3M2 (the cap was per
  run; b133 has it per map), so the C5M2 fields are still owed. New card 436: the Spawn sequence row goes by cell on
  the spawn frame, 42 of 53 miss on the client; fix `c436_anim_hold_patch.py` (host holds the row until the NPC has
  a NetGUID), for b134 with the six-map chain.

2026-11-16 13:25 b133 review, run `c423b133a` VOID from line 222 (senior):
- VOID cause: the 6500 ms nuke hold fired twice on the client (card 430 retry now reloads in the hold); the second
  shot had no host round and the launcher dropped. Carded as 437 (client-only nuke shot). Scenario holds are 3000 ms
  from b134 on. Rows before line 222 stand.
- Card 148 sub-fix MET: 0 `WARN card148` both sides (b131 client 6); client rows 24 (host flagged 24), 6 (6).
- Card 269 read-back MET: 0 `WARN card269` (b131 client 28); 12 read-back pairs, pose moved 0. The 15 predicted
  was a corpse count guess, not a gate.
- Card 140 step OWED: the aim hit a live bug (0x560). The one `WARN card140: client hit` row (0x5fe, 430 uu) is
  1 s after its rest row and before the pose unfreeze, not a ReplicatedMovement row. Step gets `host clear` first.
- Card 435 partial: host replay row has `7 Add(s) of complete objectives sent without popups, 2 callout frame(s)
  not sent`, but only 1 `host sent CalloutObjective` row in the run: the second dropped callout frame is not
  explained. Client shots showed the loading screen (continue step cut): banner proof OWED.
- Card 359 step 5 read: host bone 0 is 0.0 uu on all 57 finals; the client's 10 largest offsets (8 to 21 uu) all
  have a mesh component scale that differs per axis from the host's (e.g. 0x13e client (0.971, 0.958, 1.007), host
  (0.957, 1.034, 0.988)). The per-actor random scale is rolled on each side and not synced. Next: send the host's
  mesh scale per NPC and set it on the client copy (card 359 step 6).
- New host `WARN card132` x21 (b131: 0): `host hit ... impact is 1283..3394 uu from the NPC`, damage 8.0, during an
  idle wait. The impact points cluster at two places ((147200, 18400, 10330), (145100, 16600, 10400)) while the
  NPCs move, and the hit component is on the NPC: AI trooper fire whose damage event carries a point away from
  the body (same class as hb47 0x138, existing check). The client gets the same point as the host. No mod fault,
  no card.
- `WARN card157` x2 on the host belong to card 437.

**2026-11-16 13:28 card 437 part B traced, patch ready (senior).** Fire held across a weapon change: the host relay of a held primary press ends in `c24_host_guard` (`release (weapon changed)`) and the client sends a press only on a key edge, so the new weapon fires on the client with no press on the copy (`c423b133a`: Morita `shot c seq=3`, host `client rounds 1, copy rounds 0, unused 1`, `WARN card157 ... shot 3 ... dropped`). Fix = `scratchpad/c437b_carry_press_patch.py` (`--check` ok on the b134 tree): the guard notes the pawn at that release (`c437_note`), any client key edge drops the note (`c437_drop` in `c24_relay`), and the first Round within 5000 ms with no primary relay presses the copy's new weapon (`c437_carry_press` in `c181_receive_round`), then credits the round. Done when one run shows: a `card437: host ... press carried to the new weapon` row, Morita `shot s` rows equal to client `shot c` rows within 1, `press ended` with `unused 0`, 0 `WARN card157 ... found no round`. Known limit: before the copy's first round `c276_repress` tries only 3 times at 100 ms. Part A (nuke shot with the switch in the same frame) is not fixed; it waits on the junior's `Fire` safety read. Build: b135.

**2026-11-16 13:30 user decisions (senior).** (1) `client_significance_step` stays every frame (105 to 205 us; no throttle, closed). (2) Play build: "the latest, obviously": lane 1 follows the latest lane 2 build. Lane 1 `dwmapi.dll` is now b134 `517520517c356cdd` (was b76 `90aa405f6b2dadbb`, archived `build-b5/dwmapi-b76-90aa405f6b2dadbb.dll`). b134 has chain-run proof only, no whole-map soak; it carries the card 434 FX diagnostic rows. Each later build that passes its run goes to lane 1 by explicit `cp` of the archived file while no game runs, and the user is told the hash.

**2026-11-16 13:33 b134 chain `c399b134a` review (senior).** Card 436 MET: 49 Anim rows held and `sent by guid`, 0 guid-0 Anim rows, `not sent after 2000 ms` 0 (2 TurretTrooper `still no NetGUID` rows: not replicated, expected). Card 434: C5M2 still has 11 guids with 72 client `no NPC matched` rows and 0 played (60 % of Anim rows in the window); hidden and view-point readings ruled out by the rows; static read of APawn::IsNetRelevantFor found the movement-base delegation (NPC on a skeletal mesh actor is as relevant as that actor): step 3 fields `channel`, `relevant`, `base` go in b135 (card 434 file). New card 438: 99 attached FX rows sent with guid 0 in the spawn frame (95 `NS_Compute_DamageAndRadius` wound shading), design = host hold like card 436, one static question first (does the client spawn its own).

2026-11-16 13:38 card 438 patch ready: `c438_fx_hold_patch.py` in the scratchpad (`--check` ok on b134). Host holds an
attached spawn row whose replicated owner has NetGUID 0 (64 entries, 2000 ms), sends it with the guid key from
`c136_step`; a stop while held sends nothing. Goes into b135 with 437b and 434 step 3. Proof: `card438: host Fx map
totals` row (world 0, list full 0), 0 `card434: host FX ... (attached)` rows for replicated owners, client key misses
on C5M2 below 193, receiver screenshot of a wound on a bug that spawned in view.

2026-11-16 13:43 card 432 step 2 patch ready (`c432_step2_patch.py`, scratchpad). Client Objective receive in
`c399b134a`: 226.5 ms for 97 frames, 33.8 ms of it the game's calls; the rest is `c64_find_actor` in `c70_take`
(full level walk, about 3.8 ms a find, 46 finds). Patch routes the find through card 407's name index with a
16-answer walk compare. Goes into b135. Done when: Objective recv under 1 ms a frame, 0 `WARN card432: name index`.

2026-11-16 13:46 efficiency audit, b134 run `c399b134a`, measured and closed with no card:
- Host `Pong` receive 7 to 15 ms once per map (27 frames, 61 ms in the run): the first Pong runs every join replay
  (`sync_join_replay`). Load-time, one frame.
- Client `card120 ... Pong ... (held 1864 to 2358 ms)` on 8 mission maps: the origin is set 4 ms after the hold
  starts (`card058`); the rest is one client frame with no log line for 1825 ms (level BeginPlay). The host has the
  same gap at the same point (1727 ms + 863 ms). The game's own load frame, not a mod wait.
- `c104_in_world` (pointer walk of the level lists per pickup row) and the other level walks outside the timed
  collect are per-event and under 20 us; only `c64_find_actor` in `c70_take` was costly (card 432 step 2).

2026-11-16 13:50 card 439 opened (nuke fire ability stuck, client). Run `c423b134a` (b134) VOID: second nuke press
made no round. Cause from 4 runs' rows: a release inside the 1.00 s Fire animation (3000 ms hold = spool 2 s + Fire
1 s, one-frame race) cuts the animation with WindDown and the client's fire ability never ends (Fire tag stays
owned); the next press runs Fire, WindDown, SpoolDown, SpoolUp in one frame with no spool. Same in `c423b129a`
before the card 430 retry existed, so the retry is not the cause. Changes: scenario nuke holds 3300 ms until the
fix (then 2500 ms on purpose); junior reads the fire ability Blueprint for the missing end event. Done when: card
439 "Done when". Card 437 part A unblocked (junior: forced Fire on an inactive instance is safe). Card 436: the
wound compute system `NS_Compute_DamageAndRadius` has no renderer, host should not send it (junior finding).

2026-11-16 13:53 card 433 closed, no fix. Client Fx play calls: C2M1 `c423b133a` 3084.8 ms in 716 s (0.43 %),
top point ExecuteGameplayCue_NonReplicated 7247 rows 142 us mean; the mod's part per row is one cached name and
one indexed actor lookup, the rest is the game's cue and spawn. Diagnostic removal patch
`c433_remove_patch.py` handed to the junior for the next build; done when no `card433` row prints.

2026-11-16 13:57 card 439 corrected: the 13:50 entry's cause (trigger released inside the Fire animation) is wrong.
Runs c423b129a, c423b131a, c423b133a held 6500 ms and two of them show the fault. Fact from the 4 runs: in the bad
case the client's nuke SpoolDown activates 999 ms (60 frames) after its Fire, one frame before the 1.00 s Fire
animation ends, the WindDown start cuts the animation and the fire ability never ends; in the good case
(c423b131a) it activates at 1016 ms after the animation's own end. In all 3 bad runs that frame is the first
client frame after the host copy's SpoolDown. Two causes open: the host copy's SpoolDown is carried to the client
(no receive row logged, weapon actor does not replicate), or a 1.0 s game timer beats the animation on the 60th
frame. What changes: card renamed and rewritten; diagnostic patch c439_diag_patch.py (caller chain of the client's
nuke SpoolDown activations, 6 rows) for the next build; the 3300 ms hold change is withdrawn (holds do not decide
the case). Done when: the card439 rows name the caller and the fix by cause gives client rounds 1 on both presses.

2026-11-16 14:03 b135 run `c423b135b` read (senior; junior report pending). VOID at line 540: client look at the
nuke pickup hit `Actor_1 (Actor) guid 0x167` 267 uu away, at the SECOND pickup approach, after the rejoin (corrected 14:08: the first nuke step before the rejoin
passed, 2 shots, both presses `client rounds 1, copy rounds 1, refused 1, late-fired 1, unused 0`, so card 437 part B
worked on both; cause of the void is card 440, not an unknown actor). Met: 0 `WARN card432`, 0 `card433` rows, 0 `WARN card157`. Card 438 not met (132 held rows went
out at the world point after 2000 ms; numbers in the card). b135 does not go to lane 1 (run VOID); lane 1 stays b134
`517520517c356cdd`. Written for b136: `c437a_patch.py` (card 437 part A, forced fire, Round carries a weapon key),
`c438_wound_patch.py` (wound shading local on both sides, saves 757 rows 19936 bytes per C2M1 pass), `c439_diag_patch.py`.

2026-11-16 14:08 Card 440 opened (senior): a rejoining client gets the starting inventory on both sides; weapons
and ammo picked up before the disconnect are lost and the single-use pickup stays taken (b135b: nuke launcher gone
after the rejoin, which voided scenario line 540; junior moved that block before the rejoin). What changes: nothing
in b136; fix design in the card (host snapshot of the card 327 texts, give + ammo on the new copy, client give by an
existing op). Done when: rejoin run shows `card440 ... restored` and PW equal with weapons 2. Queue: after playtest.

**Card 434 step 3 result, step 4 diagnostic written (senior, 2026-11-16 14:14).** b135 runs: all 285 host
`far NPC` rows read `channel 1`; on C5M2 (c399b135a) 11 guids never resolved on the client for up to 23.7 s (75 miss
rows) while the host says `channel 1 relevant 1`, base none on 10 of 11. Movement base ruled out; the host has the
channel open and the client has no NPC for the guid. Step 4 is a client diagnostic (detours on
`UPackageMapClient::SerializeNewActor` and `UActorChannel::CleanUp`, one row per Anim miss) as scratchpad patch
`c434_step4_patch.py`, dry-checked against the b136 source, for the build after b136. Readings are in the card.
Proved done when each unresolved guid on C5M2 falls in one reading.

**Card 440 traced and coded as a patch (senior, 2026-11-16 14:19).** Card 143's held Restore already applies weapons
and ammo to a new copy and the client follows (cards 050, 072), so the fix is host only: `c327_row` keeps each copy's
weapon and ammo texts on a row change, and a new copy controller takes the entry of a gone controller as a held
Restore with no Health. Patch `c440_patch.py` (7 edits, dry-checked against the b136 source) goes in the build after
b136 with card 434 step 4. Limits (no player identity, crash rejoin before the timeout) are in the card. Proved done
by the rows in the card's "Done when".

**2026-11-16 14:25 Card 439 step 1 read (run c423b136a, b136 `7191143d5e2d10f1`).** The client's nuke SpoolDown is
started by a Blueprint ability graph calling `UGameplayAbility::SendGameplayEvent` (all 4 `card439` chains equal); no
net frame in the 14 frames, root not yet seen. The 999/1016 ms and log-order reading is withdrawn: press 1 (1015 ms)
left `WeaponSystem.EquippedWeapon.Fire` on the pawn ASC, press 3 (1035 ms) did not, with the same log order. In the
bad press the 3000 ms hold's release landed one frame before the SpoolDown. Press 2 then fired with no spool-up and
no round, `LOS_O02_SO05a` stayed open and the run's objective steps from line 236 failed: b136 does not go to lane 1
on this run. Next: `c439_step2_patch.py` (deep chain to the root, EndAbility rows for nuke abilities) rides b137 with
the card 434 step 4 and card 440 patches. Done when the card names the call that ends the fire ability in the good
press and what is missing in the bad one.

Wire audit and b136 review (2026-11-16 14:29, run c423b136a, b136 `7191143d5e2d10f1`): the run is VOID (nest
`LOS_O02_SO05a` never destroyed, card 439 shot 2), so b136 stays off lane 1; lane 1 keeps b134.
- Card 441 opened: AI round spawn and impact rows are text with asset paths, 12% of host bytes (466 kB of 3.89 MB,
  0.56 kB/s). Binary rows for rounds would save about 0.3 kB/s. After the playtest.
- Peak 5 s window (158 kB) is the join replay, paid once per join; no change. Objective journal: about 250 of 312
  frames are the game's own marker visibility toggles; compaction (card 066) bounds it; no change.
- Card 437: 0 `card437` rows; the 9000 ms hold never met a weapon change. The junior's trace shows a second gap: the
  client re-spools by itself while fire stays held (SpoolUp 499025261, round 2 at 499027260), the host copy does not,
  it only re-presses when Round 2 arrives (499027296) and the card 175 bound (300 ms after the release) ends the press
  before the copy's 2000 ms spool. Result `unused 1`. Fix owed: the copy must start its spool when the client does.
- Card 438: client FX rows naming `NS_Compute_DamageAndRadius` 0 (fixed). Held rows rose: 2439 held, 2 sent by guid,
  26 at the world point, 2411 ended while held (b135b: 494, 99, 132, 257). The patch does not touch the hold, so
  the rise is unexplained; next step is a per-system count of held and ended-while-held rows.
- Card 359: scale writes work (66 and 26 per map, 0 WARN). `card191` 82 rows over 42 guids (b135b 108 over 70),
  median 1.9 uu, p90 7.0: a 1 to 3 uu floor remains that scale does not explain. Outlier guid 0x3a8 warrior at
  1011.9 uu (rest row, Death_07) to trace.
- b137 (junior, building): card 434 step 4, card 440, card 439 step 2 diagnostics.

**2026-11-16 14:38, senior: b136 WARN traces, three patches queued for b138 (run c423b136a).**
- Card 428 second gap, fix written (`c428b_patch.py`): a rest row leaving Dead sent a bare `R` that dropped the
  client's live pose row; the pose row's own `R` then logged `no row` (guids 0x4b8, 0x4a4). Sprite and rest releases
  now name their table while another host table holds the guid. Proof: 0 `no row` WARN rows in a run with gibbed
  corpses. Text in card 428, "Second gap".
- Card 359 outlier 0x3a8 (bone 0 1011.9 uu off, twice): blend, component placement, hidden mesh and held ragdoll
  start ruled out; the log cannot say whether the measure or the corpse is wrong. `c359d_patch.py` (log only) adds
  the three world points to the WARN row. Text in card 359, "Outlier 0x3a8 and the floor".
- Card 437 part C (`c437c_patch.py`) is written up in card 437 with its prediction.
- Seen, not carded: the 8 client `WARN card322 ... own start skipped x1, no relay of it on this map` rows are the
  rejoin. The host relayed all four FLAKBUG conversations while the client was connected (499204166 to 499333884);
  the rejoined client's level script started them again at 499380332 and the skip was right. The WARN's per-map
  relay record starts empty at a rejoin, so the rows are noise there. A play log with these rows and no rejoin
  would be a real miss.
- Fx wire re-read: 32491 event rows, 36.0 B average, same as audit round 3 (2026-11-15 21:20). No card.

### 2026-11-16 14:51, senior: b137 reviewed (runs c399b137a, c423b137a), last nuke lost on the host

- b137 `53fc228af5d9521a`: both runs rc 0, no dumps, 0 FATAL/ERROR, every digest round 0 mismatches. Passing build;
  goes to lane 1 when no game runs.
- Card 434: the spawn bunch reaches the client and SerializeNewActor makes no actor (11 guids on C5M2, 1:1 with the
  host spawns). The "no spawn bunch seen" label is wrong. Next: read the archetype and level resolution.
- Card 440: the kept inventory is restored (`weapons from Restore: removed 0, given 0, matches the list`), but the
  client held only the Morita at the disconnect, so the launcher case is not proven yet.
- Card 439: the "no round" press did not reproduce. New: the client's last nuke makes no projectile on the host
  (`client rounds 1, copy rounds 0, refused 1, unused 1`). Cause not known from the log; diagnostic
  `c439d_patch.py` queued for b139. Details in the card.

- **2026-11-16 14:57, senior: b137 WARN classes and wire totals re-read (runs c399b137a, c423b137a).**
  - Card 140, new section "Hit drift rows during play": client `kHitDrift` rows of 460 and 566 uu on frozen corpses
    (guids 0x7ba, 0xde0) come from a measure asymmetry, the host used the actor location, the client body 0 of the
    posed copy. Patch `c140h_patch.py` (host uses `c132_posed_root` too) goes into b139. Done when a FED_C2M1 run
    with hits on frozen corpses shows no such WARN outside the 3 s after a level end.
  - Wire, c423b137a: 3.13 MB of mod channel rows over 74 windows; Fx 35.4% (105.9 B/row), Pose 24.3% (338.6),
    GibPart 9.7%, Dropship 5.3%, GrenadeSpawn 5.2%, GrenadeExplode 4.7%, WorldOp 3.8%, Digest 3.8% (874.8 B/row),
    Anim 3.2%, Decal 2.8%. Replicated median 21988 B/s, max 54010 B/s on a 478778 B/s link. c399b137a: 450 kB,
    Fx 45.1%. Every op in the list was audited before (card 427 and the earlier entries); no new card.
  - Host `WARN card103: map: pick ring wraps 345 (ring of 96 enemies, oldest replaced)`: read, no card. A replaced
    live enemy loses its row only until its next `SetPrimaryTarget`; until then `c103_player_slot` takes the nearest
    player. Rows of dead enemies are never dropped, which is why the ring wraps on every large map. The row is
    expected on C2M1 and larger; it is noise in the WARN diff.
  - Host card273 sticky part "not at rest after 30068 ms: rested where it is", client card152 bone check 2.8 uu,
    client card137 18.0 uu: all inside their known classes, no visible effect.

- **2026-11-16 15:01, senior: lane 1 = b137 `53fc228af5d9521a`; run c423b138a VOID (test aim).**
  - Lane 1 (`game/.../dwmapi.dll`) got b137 by explicit `cp` at 15:00 with no game running, hash checked. Lane 2
    holds b138 `f0f183a9a5fba91d`, not accepted yet.
  - c423b138a (b138) ended rc 3, 6 void lines, 15 failed lines, FATAL/ERROR 0. Logged cause: scenario line 216 aimed
    the second nest nuke at a body point 2695 uu away at pitch 28.9 (c423b137a: 1403 uu, pitch 60.3; the plasma bug
    nest turns), the nuke fired on both sides and did not destroy the nest, LOS_O02_SO05a stayed open and the later
    objective steps failed from that. Not a mod fault. The junior fixes the aim step (nearest clear sample or a
    second try while the objective is open) and reruns; b138's predictions are read on that run or on b139.
  - Card 434: step 4 result appended to the card; the 11 failing guids are plain WarriorBug and Morita/Shotgun
    trooper classes that spawn fine on other maps, so a class not loaded is unlikely; b139 carries the
    `SerializeObject` diagnostic (actor, archetype, level calls).

- **2026-11-16 15:06, senior: card 439 cause found, fix patch `c439f_patch.py` handed to the junior.**
  - Cause: the client's Equip (auto-switch inside Fire) leaves before the Round of the same Fire; the host cancels
    the copy's fire ability before the Round arrives. Fix: client holds that Equip until the Round is out; host
    fires one unspent credit before an equip. Text and done-when in the card file. Goes in b139 (or b140).
  - Card 438: 1471 of 1665 held Fx rows are enemy Morita muzzle flashes that end while held. Reading to prove:
    owners the client has no channel for (byte saving, no miss). Diagnostic asked: ended rows with the owner
    within 15000 uu of a client pawn; above 0 is a real miss.
  - b138 run c423b138a (VOID) still showed: card 359 c359d largest bone 0 offset 23.8 uu, no row over 100 uu;
    card 428 0 `no row` WARN but no frozen corpse gibbed, so c428b stays unproven; digest 26 rounds, 0 mismatches.

**15:12 2026-11-16, card 442 opened (gib parts with physics off leave the rest point).** Run `c423b138a` client: all
8 rest misses over 10 uu were parts with `physics off at the R row 1`; the snap lands, and at the 1 s check the part
is not simulating and back near its old place, which refutes card 279 step 2's prediction. The host's own parts
were frozen in the air (`rests with nothing within 40 uu below`). Mover not found statically (significance,
pooling, tick and floor code write no transform). Step 1 is patch `c279h_patch.py` (scratchpad, handed to the
junior for the next build): per-tick hold until the check plus a first-found row with the part's state. Done-when
and the three predictions are in the card.

**2026-11-16 15:19 senior, b139 run `c423b139a` (lane 2 `0512644b5a6033e3`, scenario VOID at line 506, a look gate).**
- Card 434, relevancy case solved: all 91 client Anim misses are NPCs whose channel closed by relevancy (past the 15000 uu
  net cull distance) while the host kept sending by-guid Anim rows. Fix `c434f_patch.py` (host skips the row for a PC with
  no actor channel), with the junior for the next build. The C5M2 "spawn bunch made no actor" case stays open. Open
  design point in the card: NPCs past 15000 uu from the client do not exist on the client.
- Card 439 sibling sweep: `CheckToAutoSwitch` also sends the underslung toggle inside Fire; `c439g_patch.py` holds that
  Switch like the Equip (card 439, "Sibling sweep").
- Card 152 WARN rise on b138 (2 to 13 rows) is no fault: scaled NPC meshes (component scale 1.02 to 1.05) put bone 0 a
  constant 4 to 6 uu from body 0, over the 1.0 uu WARN line. No change.

2026-11-16 15:26 senior: junior's b139 report accepted. c399b139a (C3M1 chain) passed; c423b139a VOID at line 506
was a scenario aim (a live warrior at the launcher pickup; fixed in the scenario for b140). b140 `c0f8ec554f4b60dc`
(c439f + c439g + c279h + card 438 near count) is running. Card 434 C5M2 case traced to the engine: seamless travel
cleans the client's guid cache but the host keeps export acks unless `net.ResetAckStatePostSeamlessTravel` is set
(default 0). Fix `c434h_patch.py` and diagnostic `c434g_patch.py` go into b141 with `c434f_patch.py` (card 434
file has the detail and done-when). The 10 client `WARN card322 ... no relay` rows of c423b139a all sit in the map
session after the c440 rejoin: expected, no change.

- 2026-11-16 15:31 senior: card 443 opened (host sprite death row outlives a trooper revive; host reads the stale `CurrentHealth`, client reads the attribute set; c423b139a host totals 61 rows, 0 revived). Fix `c177r_patch.py` (1 edit, `sprite_host_step`), goes into b141. The 3 host `WARN card132: host hit ... impact is N uu from the NPC` rows of c423b139a are the known hb47 class (event point off the body on the host too), no action.

2026-11-16 15:40 senior: b140 `c0f8ec554f4b60dc` run c423b140a VOID, cause traced: card 439 form 3, a lost client nuke
(not a regression of c439f/c439g/c279h/c438n; b140 does not go to lane 1 because its run is void).
- Client: the nest-a press (502859615) ran BP_WeaponFireAbility_NukeLauncher at once, FireProjectile 10 ms after the
  press, no SpoolUp activation. Every press after a card 430 reload retry that made the reload does this (5 of 5 runs,
  b133 to b140): the state before it is "fire ended while the key was still down" (b140: fire end 19 ms before the
  release; b139: release first, normal spool).
- Host: the copy got the same press and started its 2 s SpoolUp; Round 53 became a credit; the client's reload press
  and AmmoMirror (magazine 1 to 0 at 502861506) landed inside the windup, the copy's Fire at 502861573 had no ammo:
  `client rounds 1, copy rounds 0, unused 1`. LOS_O02_SO05a stayed 1/2, lines 236 on cascaded.
- Fix c439i (`c439i_patch.py`, round_c181.cpp `c437_force_fire`): a Round that finds the copy's SpoolUp active and
  its fire ability idle is fired at once, as during a weapon switch. For b142.
- Done when: host `card437: ... Round from ... during its spool-up (card 439 form 3) ...: fired at once` on a press
  with a client FireProjectile and no spool; that press row `client rounds 1, copy rounds 1 ... forced 1, unused 0`;
  nuke FireProjectile counts equal on both sides; normal spooled presses still `unused 0`.
- Open: whether the no-windup fire after a held-through shot is the game's own single-player behaviour or a side
  effect of the card 430 retry (`g_orig_try_activate` outside the weapon's own reload path). Not decided by logs.
- Other b140 results (junior): c439f hold works (Round before Equip); c279h `WARN card137 ... lies` 15 to 0;
  c440 line 506 passes; card 438 held near a client pawn 0 (byte saving only); digest 0 mismatches in 30 rounds.

2026-11-16 15:47 senior: card 439 client root cause proven (host carrier): the host copy's replicated tag map
(`FMinimalReplicationTagCountMap::UpdateOwnerTagMap`) put the copy's SpoolDown tag on the client's own pawn and
`UADWeaponGameplayAbility::OnInventoryOwnerTagCountChange` cancelled the client's own fire ability; Fire stayed on the
pawn, next press had no windup. Fix c439j (`c439j_patch.py`, scratchpad, `script_c310.cpp`): the client drops
replicated changes of its own weapon state tags on the own pawn. Sent to the junior for b142 (= b141 + c439i +
c439j). Done-when and open points in the card 439 file.

2026-11-16 15:48 senior: b141 `337930f774f96a5a` results (junior). c399b141a clean: c434h works (client `spawn bunch
made no actor` 11 -> 0, host cvar row once, digest 0 mismatches). c423b141a: client crash at the rejoin
(access violation in `c132_log_significance_view`, `client_significance_step` read ASTPlayerController fields at
+0xA70 off the engine's plain `APlayerController` during the transition; latent since b130, also in lane 1 b137).
Before the crash: c434f Anim unmatched 132 -> 0, c177r `revived` rows 7, 0 `no host row`. b141 does not go to
lane 1. b142 `d4d652b0e3abe035` = b141 + c439i + c132s (class check before the view log and the update call), run
c423b142a live. c439j is not in b142: it goes into b143.

**2026-11-16 15:50 senior: b143 and the card 132 plain-PC sibling sweep.** b143 `bf7d7d82aab68fc8` = b141 + c439i +
c132s + c439j is in lane 2 (run `c423b143a` live); b142 `d4d652b0e3abe035` was stopped 55 s in, never tested, superseded.
Sweep of the c132s crash class (ASTPlayerController-only fields or methods used on the engine's plain
`APlayerController`), static: every reader of `kPcMinimap`, `kPcInputReceiver`, `kPcPlayerCharacter`,
`kPcStartingMappings` is class-guarded or sits in an ASTPlayerController method hook; the PC 0 lookups in
`attributes.cpp`, `ammo.cpp`, `begin_c138.cpp`, `fire.cpp`, `enemies.cpp:5002`, `grenades.cpp`, `turret_sync.cpp`,
`fx_c136_net.cpp` and `c178_flush_release`/`request_shot` use base-class fields or functions only. One unguarded site:
host `detour_c132_update_significance` (`enemies.cpp`) reads and writes `host_pc + 0xA70` and calls
`AddAdditionalSignificanceLocation(host_pc)` with no class check. Not seen to fail (host PC 0 would have to be a plain
PC during a transition). Fix `c132t_patch.py` (senior scratchpad): pass through to the original when PC 0 is not an
ASTPlayerController. Goes into the next build after b143. Done when: build clean and the rejoin run shows no change in
host `card132` rows.

**2026-11-16 15:51 senior: Morita hold short by the bound (c423b141a, 58 of 61) loses no damage: closed, no card.**
Cause from the host log: the press at t=504147381 arrived while the copy had `WeaponSystem.Blocked` (weapon change),
the copy's first round came after two card 276 re-presses (+207 ms, two rounds behind); the client emptied its
magazine and its reload press (t=504153364) reached the copy with 2 rounds left, so the 300 ms hold could not fire
them. Damage: NPC claims with no copy round are served by a made round (card 281, `serve_made`); the run has 0
`found no round` and 0 `card281 ... no made round` rows. Left over: 2 tracers fewer on the host screen, once per
blocked press. No fix planned.

**2026-11-16 15:52 senior: card 444 queued (after playtest).** Client AmmoMirror is the top client upload op:
full ammo map as text, about 108 B per send, 4 Hz while firing (c423b141a: 2588 B in the peak 5 s window, 5818 B of
the run). Fix: binary changed-pairs frame. Card `docs/cards/444-ammo-mirror-sent-as-full-text.md`.

**2026-11-16 16:02, b143 `bf7d7d82aab68fc8` run `c423b143a` reviewed (VOID, 1 void line; not for lane 1).**
- c132s holds: rejoin ran to scenario line 547, 0 EXCEPTION rows, 2 `local PC PlayerController` rows. Digest 0 mismatches.
- c439j: 6 host tag map changes dropped on the own pawn (Reload, SpoolUp, Fire, SpoolDown); the fire tag no longer
  stays after a fire. One fire cancel is still host-driven (504730311, chain through `0x4A07914`, 15 ms after the
  fire's own 1000 ms) by a tag outside the list, expected `WeaponSystem.Blocked`. Diagnostic c439k names it in b144.
  The other 3 `cancelled 1` rows are the local SpoolDown activation after the natural end (present in b140, b141).
- c439i forced path not exercised (no form 3 press); all host press rows `forced 0, unused 0`.
- `WeaponSystem.Recoiling` stays on the client pawn after the first nuke fire: also in b140 and b141, not from c439j.
- Void line 511 cause: the look targets the map's own launcher pickup, which the host disabled at the first pickup
  and re-enables only on the game's timer (332 s in c423b140a). Host and client agree; no mod defect. The scenario
  must target the dropped copy (junior).
- Next: b144 = b143 + c132t + c439k, one c423 run; lane 1 gets it only if the run is clean.

**2026-11-16 16:06, host per-frame cost re-read on b143 (run `c423b143a`, FED_C2M1, `card330` step rows, about
20500 frames): no new card.** (1) `auto_dropship_step` shows one call of 1052 ms (929 ms b140; 987, 905, 947 ms on
C3M1, C3M2, C5M1 in `c399b141a`). It is the game's own `SkipFlight(to_landing 1)` call: the log is silent from
504579286 to 504580330 and the `autodropship: SkipFlight ... state 0 -> 1` row follows at 504580337. The step only
runs under `UBWCOOP_AUTODROPSHIP=1` with `UBWCOOP_SKIP_FLIGHT=1` (`flags.cpp`), which the test harness sets and a
play launch does not, so it is no play cost. (2) `c43_step` 18.62 ms max is one `c45_respawn_check` call (the
respawn itself, 18.60 ms, once). (3) `c132_digest_step` 20.72 ms max is the digest round, already card 386.
(4) All host steps together are about 5.5 s over the map including the one-off, about 220 us per frame without it
(1.3 % of a 16.6 ms frame); largest means: `c132_ext_step` 47.6 us, `c132_decal_step` 22.3 us, `c43_step` 21.6 us,
`c140_pace_step` 17.4 us. `c159_dropship_step` runs two whole-world class scans each second (55 calls over 1 ms,
2.37 ms max, 187 ms total): too small to card. (5) `WeaponSystem.Recoiling` staying on the client pawn after a
fire is by design: `GA_Weapon_RecoilAbility_C` activates on the first fire and stays active until the weapon
changes, on the host copy as well. Closed without a card.

**2026-11-16 16:19, b144 `b09caa0e7c1f80da` (b143 + c132t + c439k) run `c423b144a` reviewed: VOID from the
scenario only (void line 516, failed line 608); not for lane 1 yet.** 0 EXCEPTION, digest 0 mismatches, no new WARN
class (client card140, card137, card138, card104 and the decal cap row all occur in earlier logs; the card140 drift
rows come 2.7 s after EndLevel). c132t: host `card132` kinds unchanged, the new pass-through never fired. Card 439:
c439k names the one host-driven cancel as `WeaponSystem.Blocked` +1 (505672571), 16 ms before the fire's own end;
accepted with no code change, because the client's own SpoolDown cancels its own fire the same way at the natural
end (505595911, 505603677) and Blocked has host-only sources so it cannot be dropped. Passed tags in the run:
Blocked only (+1 once, -1 three times); form 3 rows 0. Scenario causes: a loaded dropped launcher despawns about
17 s after the drop and the map's `_0` pickup re-enables only 10.0 s after an EMPTY launcher drop (b10, b133, b140),
never after a loaded one, so the card 440 setup had nothing to pick up; line 608 is a third retry click on a widget
already gone at travel. Next: junior moves the card 440 setup to right after the part B drop, makes line 608 a
`try`, one more c423 run on the same DLL (third of the budget); clean run puts b144 on lane 1.

2026-11-16 16:21, dropped runtime weapon pickups disappearing (b143, b144): game behaviour, synced, no card. The "about 17 s after the drop" was the scenario's own timing, not a timer: in `c423b144a` the three runtime pickups on the map (two `BP_WeaponPickup_MoritaMk1_C`, the dropped `BP_WeaponPickup_NukeLauncher_C`) are destroyed by the game (caller `exe+0x37DC6BD`) at 505694840 to 505694844, 37 ms after `objective go` (cmd 148, 505694803) warped both players 41 km away; no earlier warp left a runtime pickup behind without a player near it. The mod has no pickup destroy path (grep of `mod/src/ubw`). The client's copies go through `card104 ... our copy destroyed` at 505694854, and digest round 7 has 0 mismatches. Consequence for scenarios only: a step that needs a dropped pickup must run before the next far warp.

2026-11-16 16:24, card 321 L1 candidates re-read (`docs/SYNC_AUDIT.md` L1 table updated): fall zone reposition is covered by card 338; teleporters, doors and streaming volumes are 0 on all eight FED maps (`card321: host map` rows); the threat level feeds music only (three `USTMusicManager` readers of the combat level), so no gameplay fix. Playtest check, hearing only: does the client get combat music in a fight it has alone, and does the host stay calm then. Left as candidates: troop dropship `CanTakeoff` and the ally nav filter (both cosmetic).

2026-11-16 16:27, cards 443 and 442 after b144 run `c423b144a`. Card 443 accepted (43 revived rows removed, 0
client `WARN card177`), card moved to done. Card 442 step 1 cut the worst physics-off rest miss from 102.9 to
16.3 uu; the rest is fall velocity kept across the hold's teleport (980 uu/s times one tick). Step 2 patch
`c279s_patch.py` (hold sleeps the body) waits for the next lane 2 build; proof is worst miss under 2 uu on a C5M2
run.

2026-11-16 16:30, lane 1 play build is now b144 `b09caa0e7c1f80da` (was b137). Run `c423b144b` (C2M1, 367 s):
scenario passed, 0 void, 0 EXCEPTION, digest 0 mismatches, moved-pickup launcher taken by the client, card 440
inventory equal before the leave and after the rejoin (weapons and ammo). One timed-out try (line 358, key One
109 ms into the launcher's equip ability: both sides refused the switch, no desync); scenario change owed: wait for
the client's `card439 ... EndAbility BP_Weapon_EquipAbility_NukeLauncher_C` row before key One. Open from the run:
the host player row's tactical charge goes `20000 1` to `0 0` across the rejoin (also in b143a, b144a).

2026-11-16 16:41 — Cards 445 and 446 (rejoin sibling sweep of card 440). 445: a rejoined client's tactical support
charge went `20000 1` to `0 0` (run `c423b144b`); the kept charge is now owed to the new pawn; in b145 (junior:
b144 + card 442 step 2 + card 445), proof rows in the card. 446: item counts (grenades) return to the starting loadout
on rejoin; patch `c446_patch.py` written and checked against the b145 tree, goes in b146 with a scenario grenade throw
before the leave. Rejoin limit stated in card 446: health and armour come back full. Card 382 prediction 1 holds
(10 saves each in `c423b144a`/`b`, 0 `card382` rows). Card 359 outlier is a real displaced corpse (388.1 uu, transient);
diagnostic row after the playtest.

2026-11-16 16:44 — b145 `851ac6a55c0aed2b` (b144 + card 442 step 2 + card 445) passed run `c423b145a` (C2M1 gates:
0 EXCEPTION, digest 0 mismatches, scenario passed) and is now in lane 1 and lane 2. Card 445 accepted. Card 442
step 2: client `card279` holds put back exactly the parts found off (2/2, 2/2, 0/0; b144b 14 for 1), card 137 worst
0.0 uu, 0 `WARN card137`; the earlier "C5M2" label on runs `c423b138a`/`c423b144a` was wrong, they are C2M1. The
scenario's working `key One` drops the nuke launcher (the game's heavy-weapon unequip rule; pickup reaches the client),
so its card 440 steps failed: scenario fix, not a mod defect. Known WARN classes, no action: client `card159` check
that races a dropship take-off (2205 uu, state applied after); host `card273` sticky gib part whose slide timer stays
0.00 (rested by the 30 s bound). Next: b146 = b145 + card 446.

- 2026-11-16 16:50 (senior): card 447 opened and coded as `c447_patch.py` (scratchpad, 3 edits, `--check` passes on
  the b146 tree), handed to the junior for the build after b146. Defect by static trace: a client that leaves the
  game while in a mech or turret has that seat as its PC pawn, the engine's `PawnLeavingGame` destroys it for
  everyone (no game override) and the soldier stays hidden without a controller. Fix: `c447_logout` runs the game's
  own leave first in `detour_logout`. Proof owed: one run with a leave from a piloted mech on FED_C2M1 (card rows).
  Cards closed from stored logs, no run: 275 (run `c243-c3m2-a1`: 16 slot reuse rows, 0 `slot table full`) and 266
  (no gap; runs c127-c2m1 and c243-c3m2-r5). Card 207 (corpses after an in-place load) stays open: the b145 rejoin
  replay sent 3 of 3 tracked rows, but no run since has a checkpoint load with corpses. b146 is `8852b98c1503ca7f`
  (cards 446), built 16:46, in lane 2, run not yet reported. Lane 1 still b145 `851ac6a55c0aed2b`.

- 2026-11-16 16:53: Cards 193 and 262 closed from stored logs, no run (193: effects go by the effects manager, WARN reworded, 0 rows in `c423b145a`; 262: every row is owned by card 193 or 424 or did not return). Run `c423b146a` (b146 `8852b98c1503ca7f`, card 446 proof) started 16:53.

- 2026-11-16 16:57: Card 448 opened and coded as a patch script (a rejoin was a full heal: the kept Restore now carries the leaving marine's Health and Armour, read at Logout). Goes in the build after card 447; proof is the `card448` and `card143 ... Restore applied` rows. Card 323: `Round` struck (client to host only); the card 447 run also rejoins the client while the host sits in a mech, for the seat row. Card 207: no stored run has a checkpoint load with corpses since card 269; one run of `tools/scenarios/c142-r4.txt` decides whether the rest row covers it.

**2026-11-16 17:02: card 446 accepted, b146 reviewed.** Run `c423b146a` (b146 `8852b98c1503ca7f`) met every card 446
prediction (rows in the card, now in `docs/cards/done/`). WARN diff against `c423b145a`: no new class. The rows new
against b145 all occur in earlier gate runs: host `WARN card132: seq gap 48-48` at the map end (b140, b144, b144b: one
client frame lost at travel), host `WARN card140 ... pose self-check ... 1 moved over 100 uu` (b144b), client
`WARN card138: client hold dropped at LoadMap` (b144a), client `WARN card296` spool-up without fire (b140, b144b; here 3
rows because the client holds the launcher during the short fire presses). Two scenario `try` steps (lines 584, 591)
miss because the rejoin pawn already has the launcher equipped: scenario artefact. Lane 1 copy of b146 waits for run
`c447b147a` to end (no copy while a game runs); if b147 passes, lane 1 takes b147 instead.

2026-11-16 17:10: card 447 failed in c447b147b, second fix written; card 448 blocked by the same cause; card 449 written.
- Cause: `APlayerController::Destroyed` runs `PawnLeavingGame` before Logout, so the PC's pawn is null at every
  Logout. The mech went with the player, and the pawn half of `detour_logout` (cards 024, 043, 448) never ran.
- Fix: `c447b_patch.py` hooks `PawnLeavingGame` and moves the seat leave and the pawn half there. b148 (card 448
  alone, `16859d08874d10af`) cannot pass its 448 proof; the next build takes `c447b_patch.py` and `c449_patch.py`.
- Card 449: troop dropship `CanTakeoff` asked for every player on the host, left to the host's frames on the client.
- b147 was built in `build/` by mistake; the junior restored `build/dwmapi.dll` to `90aa405f6b2dadbb`. Lane 1 was
  not touched and stays on b145 `851ac6a55c0aed2b`. b146 `8852b98c1503ca7f` is the latest passing build; its file
  must be found or rebuilt before it can go to lane 1 (build-b5 now holds b148).
- Proves done: card447 row with "the seat stays", card448 row with kept Health, card449 rows as in the cards.

2026-11-16 17:12: card 450 written (patch `c450_patch.py`, 1 edit): the card 327 player row asks the mech pilot
slots only for a pawn that is a mech. The slots are cleared only at map end, so a pawn at a destroyed mech's address
could hand back a freed soldier. Sweep of the other raw actor pointers held in tables (card 361 crushed pairs, card
136 client PC cache, card 050 moves, cards 157, 169, 130): each is compared only, weak-checked, or refreshed per
tick; no second case. Slot clear at LeaveMech is left to card 323.

**2026-11-16 17:18: card 451 written (bark range).** `RequestBark` ranges a bark from the host's view only; the sound is already relayed by card 136. Patch `c451_patch.py` (4 edits: symbol, hook row, header, `enemies.cpp`) widens the range for one call when a client is in range. Goes into the build after b149 with card 450. Proof: host `card451` row with the client at NPCs far from the host.

**2026-11-16 17:20: card 452 written (ENTER prompt on a piloted mech).** Mech `CanInteract` tests `APawn::Controller`, which the client does not have for the host's mech. Patch `c452_patch.py` (4 edits) refuses on the client when the mech has a `PlayerState`. Goes into the same build as cards 450 and 451; proof folds into the card 447 FED_C4M1 run (shot at the host-piloted mech, then enter after the host leaves).

- 2026-11-16 17:22: door audit closed into card 453. `ASTDoor::HandleCharactersInMovePath` blocks for every pawn in its sweep, but its crush step (damage or instant kill in the crush volume) reads `GetPlayerCharacter(0)` only, so a client is never crushed. Low priority, after playtest; fix is a host hook that applies the same result to remote pawns. Open before coding: which Trooper map doors have a crush volume.

- 2026-11-16 17:27: build b151 `7d8087dbc3cee8ab` (cards 447b, 449, 450, 451) passed run c448b151b on FED_C1M1 (209 s, both rc 0, FATAL/ERROR 0, WARN 2 per side, both known; 6 digest rounds, 0 mismatches). **Lane 1 now holds `7d8087dbc3cee8ab`** (copied 17:27, hash verified, no game running). Senior log read: card 448 passes (leave Up with Health 30.0 kept, rejoin row `Health 100.0 -> 30.0`; leave Downed keeps none, rejoin at 100.0). Card 449 passes its log predictions (client rows `landed copy leaves its takeoff to the host` and `host took off, StartAction run on the landed copy`, 0 `WARN card159`); the host row `takeoff held` was not reached (no client trooper stood on the ship). Card 451: hook runs, one `played 1` row; client hearing is a playtest check. Card 447 passed in run c447b150a. Cards 447, 448, 449 close when the junior's report with the viewed shots arrives. Card 450 regression check and card 452 wait on a FED_C4M1 run. Scenario fault, not a mod fault: `c448-c1m1.txt` line 73 waits on `card135 ... screen prompt ready`, which a mid-mission rejoin does not log.

- 2026-11-16 17:28: per-frame audit of run c448b151b (card 330 rows, FED_C1M1). Client `cine_skip_step` shows one call of 1.95 s per map load: it is the game's own `GameStarted` run by `c135_step` (row `card135: client GameStarted` logged 1.95 s after its start stamp), inside the load screen while world rendering is still off (card 337 turns it on right after). Game cost, not mod cost, not visible; no action. Next client steps: `client_significance_step` 63 to 103 us mean (kept every frame by user decision), `c37_step` 22 to 27 us, `c293_step` 18 us, `c43_step` 16 to 19 us (one 16 ms call at a down). ProcessEvent hook on the client: 26 to 31 us mean per frame, 0.13 us per call. Nothing new to card.

- 2026-11-16 17:34: **b152 `f2af555e538b37ac` (b151 + card 452) passed run c447b152a** (FED_C4M1, drive rc 0,
  3 digest rounds 0 mismatches, 0 EXCEPTION, WARN classes known: attach, card152, card191 corpse freeze as in
  earlier C4M1 runs). Lane 1 and lane 2 both hold b152. Cards 447, 448, 450, 452 accepted and moved to
  `cards/done`. Card 449 stays open: client rows proven (c448b151b), the host `takeoff held` row needs a warp
  onto the ship deck, not its origin. Scenario `c448-c1m1.txt` line 73 is now a `try` step (no
  `screen prompt ready` row on a mid-mission rejoin). Audit: world camera shakes (`PlayWorldCameraShake`)
  reach the client (client `card099` rows `Shake_Generic_Explosion*` in logs c133x6, c99p3); the client's own
  hurt shake (`ASTPlayerCharacter::HandleDamageEvent` -> `PlayHurtCameraShake`) is not yet shown to run on
  the client, check owed.

- 2026-11-16 17:45: b152 `f2af555e538b37ac` (lane 1 play build) has a host crash: run c207b152b, access violation at ubwcoop+0x129AAD, `c104_note_start` log row read animator +0x158 (a `USTSkeletalAnimatorComponent` field) on a smaller `UADSkeletalAnimatorComponent` base object, because the fixed-time start hook sits on the base class. Hits only while the first 60 card104 log lines are open and the allocation ends at a page edge. Fix in b153 `fc36155d97a81c9e` (`c104_static_mode`: reads the byte only when the class PropertiesSize is above 0x158); senior reviewed the code: correct, the only other animator read (+0xF8 mesh) is a base class field. b153 goes to lane 1 when run c207b153a passes. Card 449 host proof found: c207b152a host row `card449: host/standalone ... takeoff held` (the scenario regex missed it); card 449 closes with the junior's full report.

- 2026-11-16 17:49: Run c207b153a (b153 `fc36155d97a81c9e`): no crash, FATAL/ERROR 0, `card104 ... static-mesh mode -1` rows on the path that crashed b152; drive rc 2 from scenario steps issued while the client was downed (junior fixes them). Lane 1 and lane 2 now hold b153 (senior copied lane 1, hashes verified). Sibling sweep of the crash class for animators: clean (the base-class start guard reads only +0xF8; +0x158/+0x15C are read only in the `USTSkeletalAnimatorComponent` hook). Card 449 host row seen again (`takeoff held`); closes with the junior's report. New card 454: the client gets no hurt camera shake (9 own-pawn drops, no `Shake_Generic_Damaged*` row on either side); patch `patch_c454.py` handed to the junior for b154. SYNC_AUDIT rows added: jump pad overlap (covered by the card 102 overlap scope), ranged `SelectTarget` (covered by card 103 statically, runtime row owed), `GetPlayerPawn` (benchmark tool only).

**2026-11-16 18:01 — b154 `87e10a213592d53c` in lane 1 and lane 2; cards 449 and 454 closed.**
- Card 454 proven in run c207b154a: 7 client `card454` rows, each with a `card099 ... Shake_Generic_Damaged*_C ... (local)` row; no row on the revive heal, the checkpoint pawn, or the rejoin restore (2.3 s after the pawn, inside the 6 s settle). 0 EXCEPTION, digest rounds 0 mismatches. b154 also skips unread vitals (-1.0) and gives camera shake rows their own log budget (200 per map).
- Correction: run c207b153a proved nothing about the hurt shake. Sound rows used up the shared 300-row card 099 budget before the first hit on both sides.
- Card 449 closed on run c207b153a. The c207b154a miss of the `takeoff held` row was the scenario warping to a ship still settling; the deck warp must wait for the landed row.
- Card 207 open, confirmed twice (c207b153a, c207b154a): after an in-place checkpoint load the host sends no `card269` rest row and no pose row for corpses, so the client shows its own ragdolls (`WARN card152 ... no stream row`, then `WARN card137 ... not posed after 9002 ms`). Next fix.
- Known accepted limits of card 454: a hit in the first 6 s of a pawn gives no shake; an Armour gain and Health loss in the same tick can cancel.

**2026-11-16 18:05 — card 207 fix handed to the junior (b155); card 455 opened.**
- Card 207 trace: corpses the save spawns dead never reach `ReceiveBaseStateChanged`, so they get neither a rest row nor a pose row. Fix `c207_shown_corpse`: host, NPC mesh hidden 1 -> 0, owner dead and not simulating, no row: rest row through `c269_state_host`. Proof: 3 host `card207` rows after the load, rest replay `3 rows sent of 3`, 0 client `WARN card152 ... no stream row`.
- Card 455 (new): the client never hears its own hurt sounds (armour hit, armour break, health hit, pain bark). The host skips them as server copy sounds (card 160) and nothing relays them. Run c447b152a: 29 host skips, 0 client plays. Fix: prefix table relay to the owning PC. Card file has the proof rows.
- Judged, no action: `netbudget ... NPC guid 0x2ce moved and unsent 3151 ms` (c207b153a). The NPC was about 16000 uu from both pawns; it only ranks because it sent earlier. No such row in c207b154a.

**2026-11-16 18:11 — run c207b155a (b155 `09fa0ac06c00c9d1`, lane 2 only): card 207 half proven; b156 handed over.**
- Proven: host 3 `card207: host corpse shown` rows after LoadLatestSave, rest replay `3 rows sent of 3`, client 3 rest arrivals played. 0 EXCEPTION, 0 mismatches.
- Failed: the client's game starts a ragdoll on each copy 2.2 s later (caller exe+0x37C53CB); card 152 holds it, then releases it after 2000 ms (`WARN card152 ... its own ragdoll started` x3). b155 stays out of lane 1 (lane 1 = b154).
- b156 = b155 + card 207 step 2 (client blocks a game ragdoll start on a guid with a rest row and no pose row; wait fallback starts nothing when a rest row exists) + card 455 (`ChanOp::HurtSound`, rows `MS_GORE_Human_Player_`, `MS_PLAYERBARK_Pain`, `MS_ITEM_PickupAmmo`). Patches `patch_c207b.py`, `patch_c455.py` in the scratchpad. Predictions are in the junior's task and the cards.
- Diagnostic limit found: the client's card 099 sound budget (300 rows) is eaten by `MS_UI_FedScore_Typewriter_Click` (362 rows in c207b154a), so "client plays `MSS_FOL_Low_Ammo_Click` itself" is unverified (host mutes 8 for the copy). Candidate, low: exclude the typewriter click from the budget, then check.

**2026-11-16 18:12 — 2D sound audit notes.**
- Ammo pickup row of card 455 confirmed: at the host skip (510226927, c448b151b) the client's card 099 rows were still being written (36 sound rows in that 10 s, WeaponSwap rows 2 s later) and none is a pickup sound.
- Low-ammo click: the host mute row is a component sound (`kC160Mute`), a path with no card 099 row on either side, so logs cannot show the client's own click. The client's fire ability runs locally (card 173), so no gap is assumed. Corrects the "eaten budget" note above for this sound; the typewriter click still eats the budget for other 2D sounds.
- Armour tint: covered (card 170 calls `ArmourAttributeChanged` on the client, as card 037 does for health).
- Playtest check (new): does the client see the HUD damage direction indicator (`BP_DamageIndicatorEntries`, Blueprint-driven, no native function to trace) when hit from the side or behind?

- 2026-11-16 18:20: run c207b156a (b156 `97e63881d4a00093`, lane 2 only). Card 455 hurt rows proven (8 host relays,
  8 client plays, asset 1, 0 WARN). Card 207 step 2 holds for rest-row corpses, but 2 of 3 loaded corpses had no
  death name and got no rest row, and the blueprint's `SetAllBodiesSimulatePhysics(1)` still ran on the third.
  Step 3 handed to the junior for b157 (bare rest row `None`; client blocks that call), with card 455 step 2
  (prefix `MS_ITEM_Pickup`: satchel and tactical support pickup sounds are host-skipped and never played on the
  client, c418b121a). b156 stays out of lane 1 (card 207 proof failed); lane 1 is b154 `87e10a213592d53c`.

- 2026-11-16 18:27: run c207b157a passed (b157 `1e6244b6739bbcd0`). Card 207 step 3 proven in logs: 3 of 3 loaded
  corpses get a rest row (2 named, 1 bare), client shows them and blocks both game ragdoll starts, 0 WARN, 0 mismatches.
  Same-spot corpse shots still owed (card 207 stays open for that only). Card 455 hurt rows proven again (4 relays, 4
  plays); pickup rows (step 2) owed from c418-satchel-c3m1. Lane 1 and lane 2 both hold b157 `1e6244b6739bbcd0`
  (copy `build-b5/dwmapi-b157-1e6244b6739bbcd0.dll`).

- 2026-11-16 18:33: card 455 closed on c418b157a (pickup sound relayed and played once on the client; hurt rows 11 of 11).
  Open from runs c418b157a and c418b157b, not carded, cause not proven: a timed satchel hold did not complete, once on
  the client (run a, line 96, E held 3517 ms, no `card133: client ... own hold completed`) and once on the host's own
  hold (run b, line 79, E held 3216 ms). Both times the scenario's `aim: hit BP_SatchelBuilding_Flakbug_C ... 0 uu` row
  shows the player's camera inside the building mesh (warp `<site> 100`); in c418b121a the client stood at
  (1422, -7900) and the hold completed in 2.5 s. The host's own hold is game code with no card 207 or 455 path in it, so
  the b155 to b157 changes are not the suspect. Next: place both players outside the building mesh (the b121 spots) in
  `c418-satchel-c3m1.txt`, one run; if a hold still fails from there, card it as a mod bug with the rows. No more c418
  runs today.

2026-11-16 18:35 — Satchel timed hold failure, correction of the 18:33 entry (read-only log trace, no run).
- The "camera inside the building mesh" lead does not hold for the host: the host `aim: hit ... 0 uu` row is the same in c418b121a (pass), c418b157a (pass) and c418b157b (fail). It still separates the client cases: 116 uu at (1422, -7900) passed in b121a, 0 uu at (1482.7, -8206.2) failed in b157a.
- Host fail (c418b157b): press 514277761 with `MS_WPN_SatchelCharge_Prime`, `MS_WPN_SatchelCharge_Cancel` at 514278863 (1.1 s into the 2.5 s hold), no `card418 ... fuse` row. A passing hold plays the same Cancel sound at 2.5 s together with the fuse row (c418b157a 514090620), so an early Cancel sound is the game cancelling the hold.
- The repeated `HandleInputTag_Pressed InputTag.Use` + `IA_ExitVehicle` 1.0 s after the press is in passing holds too (b157a host, b121a client): not the cause.
- Only difference found in the failing host hold: two `InternalTryActivateAbility(handle 149) = 1, ability GA_ShuntAbilityCharacter_C` rows at 514278296 and 514278513, inside the hold, each on the same ms as a TurretTrooper `card114` damage row on a ChariotBug near the host. The passing host hold has no shunt row inside it (nearest 514085820 before, 514092753 after). Whose ability handle 149 is (host pawn or the bug) is not logged: correlation, not proven cause.
- Client fail (c418b157a): no shunt row inside the hold; 11 `ClientAdjustPosition` rows between the blast (514093632, client lost 74) and the release, two of them inside the hold (514098259, 514098359, 8.5 and 2.5 uu). The client never logged the Prime sound row (card304 logs host only), so whether its hold started at all is unknown.
- Not excluded: a regression in b122 to b157 (no run in between drove a client timed hold).
- Next (one run, after playtest or tomorrow): scenario places the client at the b121 spot (1422, -7900), adds `host clear 3000 kill` before each plant so no bug or turret fire is near, and the mod logs the owner pawn in the `InternalTryActivateAbility` row and a client-side hold start/cancel row (hook the game's hold-cancel next to `detour_c133_completed`). Prediction: with no bugs near, both holds complete; if a hold still cancels, the new rows name the cause and it is carded as a mod bug.

2026-11-16 18:36 — Card 456 opened (`docs/cards/456-satchel-timed-hold-cancels-early.md`): the satchel hold finding above is carded with a diagnostic step (hooks on `OnTimedInteractionInterrupted/Started/Released`, owner name in the `InternalTryActivateAbility` row, scenario with the b121 client spot and `host clear 3000 kill`). Queue: first card of the next session, before a playtest if one is planned, because a hold that cancels would block Resort_SO3 for players.

2026-11-16 18:44 — Card 456 cause found (logs + disassembly, card file has the rows): both failed satchel holds had a `card408 ... turn BP_SatchelBuilding_Flakbug_C_N` row inside the hold (b157b host 0.6 s in, b157a client 1.96 s in); the passing hold had none. The game interrupts a timed hold when the interactor trace stops hitting the site (`UADInteractorComponent::TickComponent` exe+0x4A20230), and the site turns with the flak bug. Game rule, same in solo; not a mod bug, not a regression. Run c418b158a (b158 `d8f61a2f0a8eb107`, log-only `card456` rows): host hold passed, client step VOID (fixed warp point cannot work: the site moves with the turns). Lane 1 stays b157 `1e6244b6739bbcd0`; b158 is not promoted (its run had a VOID step). Open: one run c418b158b with a site-relative client warp and plants timed after a turn; close 456 when the client hold completes and no client `interrupted` row lacks a host turn row. Open question: interactor +0xFC reads 1 after a release.

2026-11-16 18:50 — Card 456 closed, not a mod bug (run c418b158b passed: host and client satchel holds both completed with plants timed into the gap after a flak bug turn; no interrupted row inside a hold; 0 EXCEPTION, 0 mismatches). Lane 1 and lane 2 = b158 `d8f61a2f0a8eb107` (b157 + log-only `card456` rows and ASC owner in the `jump: InternalTryActivateAbility` row; copy `build-b5/dwmapi-b158-d8f61a2f0a8eb107.dll`). Playtest note: a satchel hold that cancels while the flak bug turns is the game's rule; players wait for the turn to end. To look at next session: client `WARN card148: client gib host part N ... asleep above the ground` rows (6 in c418b158b, far from the players at about (-73381, -53385)); card 207 same-spot corpse shots still owed.

2026-11-16 18:55 senior: user asked "prioritize what is necessary and go until 8". Triage of the open card files: cards 440 and 442 closed on proof already in the logs (`c423b144b`, `c423b145a`), moved to done with CARDLOG rows. Card 299: candidate (b) (BugBlocker gates) was fixed by card 423; candidate (a), the real nest `Bugnest_HumanGameplay_C_5`, is still unrun (the `card299` rows in the C2M1 runs name the plasma bug buildings `Bugnest_HumanGameplay_C_0`/`_7`, which have no STMeshUpdate component by design); it waits for a play log. Card files 430, 435, 437, 438 carry status lines older than the code in b158 (all four have rows in `c423b146a`); their done-when lists are read against the b158 regression runs the junior has now (`c423b158a` C2M1, `c448b158a` C1M1; b158 had C3M1 probe runs only).

- 2026-11-16 18:59 senior: card 298 read against `c447b152a` (appended to the card): the client's pitch attribute
  zeros are a row-order artefact (row printed before the re-init); client mech `Fuel`/`MaximumFuel` 0/0 is open
  (HUD and predicted dash may differ; host runs the abilities). Next lane 2 batch: host row, row order, client dash
  step in `c447-c4m1.txt`. Playtest check: client mech fuel bar and dash. `WARN card148 ... asleep above the ground`
  for parts about 70 000 uu from the client is known noise (card 148, appended); near parts stay a finding.

- 2026-11-16 19:17 senior review, run `c423b158a` (C2M1 whole map, b158 `d8f61a2f0a8eb107`, the play build): scenario
  passed in 323 s (474 ok steps, 0 FAIL), 0 EXCEPTION both sides, playlog mismatches 0, card132 rounds 0 mismatches,
  WARN classes a subset of `c423b146a` (host: cmd 34, card132 2, card346 1, card273 1, card103 1; client: card191 48,
  card322 10, card152 8, card296 3; no card148). card423: gates `_3/_5/_7` once on the host, twice on the client
  (live + rejoin), 0 WARN, same as b146. card456: 3 `hold interrupted ... completed 1` rows (end of a finished hold,
  not a cancel). card438 totals: held 429, by guid 14, world point after 2000 ms 6 (done-when wants 0: card stays
  open; the held ones are AI trooper muzzle flashes far from the client), list full 0. card276 nuke reload rows 4.
  Finding: card 435 leftover. Shot `client-c423b158a-c435-rejoin-3s.png` shows the banner "Cut Me Some Flak / Use C4
  satchels to Destroy Flak Bugs." for the long-complete LOS_O03 (no Touchdown, so the old check passed). Suspect: the
  join filter clears popups only on Add frames; the replayed `SetObjectiveVisibility(LOS_O03_SO03) flags 0x83` pops
  the banner. Junior has the fix order (same build as the card 298 fuel diagnostic). Cosmetic, about 5 s after a
  rejoin; not a play blocker, b158 stays the play build.

- 2026-11-16 19:18 senior review, run `c448b158a` (C1M1, b158 `d8f61a2f0a8eb107`): scenario passed in 127 s (72 ok
  steps, 0 FAIL; `c448b151b` failed line 73 at 178 s), 0 EXCEPTION both sides, 0 real mismatch rows, WARN classes
  equal to `c448b151b` (config, attach). With `c423b158a` the play build b158 has whole-map regression proof on C1M1
  and C2M1. Lane 1 = lane 2 = b158.

- 2026-11-16 19:22 senior review of `c447b159a` (C4M1, build `885a2976f01932ac`): passed, 84 ok steps, 0 EXCEPTION, digest 0
  mismatches. Card 298: host mech Fuel 100/100 (prediction a met), client pitch attributes -89.90/89.90 after the
  re-init on both mounts (prediction b met: look-down fix works by the rows). Client mech Fuel stays 0/0 and the client
  dash step is not decided: no host baseline for LeftShift in the mech. Next step in the card file (host baseline step,
  one run). Card 435 fix in this build reviewed statically: join frames carry bit 8, the client skips its own
  `CalloutObjective` inside them; no flag-bit collision found (bits 0-4 bools, 6 snapshot, 7 exists); side effect: a
  rejoiner also gets no banner for the current objective when its callout sits inside a replayed Complete. Proof owed
  by a `c423` run on this build.

- 2026-11-16 19:25 run `c423b159a` (C2M1, build `885a2976f01932ac`) ended by a host crash 44 s in: render thread,
  `RHIGetViewportBackBuffer` with a null viewport RHI, 16 ms after a checkpoint save (the game's save thumbnail has no
  null check). First sample of this address in `logs/`; not in the b159 diff's path. Card 457. Card 435 proof not
  reached; b159 does not go to lane 1 until a whole `c423` run passes. Lane 1 stays b158 `d8f61a2f0a8eb107`.

- 2026-11-16 19:32 senior review of `c423b159b` (C2M1 rerun, build `885a2976f01932ac`): passed in 370 s, 474 ok steps, 0 FAIL,
  0 crash, digest 12 rounds 0 mismatches. Card 435 closed (rejoin 3 s shot shows only the open objective's banner;
  host and client card435 rows as predicted). WARN classes beyond `c423b158a`: host `WARN autodropship: pause
  requester ... null` (in 86 earlier host logs) and client `WARN card137: client gib spawn of host part 469 failed:
  class not found` 0.5 s after the rejoin (in 4 earlier client logs, c423b134a, c423b140a, hb72): neither is new to
  this build. The card 457 crash did not repeat. Owed before lane 1 gets this build: `c448` on C1M1.

- 2026-11-16 19:34 senior review of `c447b159b` (C4M1, build `885a2976f01932ac`): passed, 84 ok steps, 0 FAIL, 0 crash. Card 298
  prediction (c) met: the client-driven mech's fast move reaches the host (`HandleSprintChange(3.000) on
  BP_BaseMech_C`, MaxWalkSpeed 750 -> 2250). Card 298 now waits only on the playtest (client fuel bar, look-down
  feel). Open sibling of card 435, not carded: a checkpoint snapshot replays Complete frames on the client
  (`c207b157a` client rows `subsystem ObjectiveComplete` just before `snapshot end`); no client callout row there,
  so no banner seen yet; check the next c207 run for a client `CalloutObjective` between snapshot begin and end.

- 2026-11-16 19:38 lane 1 (play build) now holds b159 `885a2976f01932ac` (was b158 `d8f61a2f0a8eb107`; kept copy
  `build-b5/dwmapi-b159-885a2976f01932ac.dll`). Gate: `c448b159a` C1M1 (72 ok steps, 0 FAIL, digest 5 rounds 0
  mismatches, WARN classes equal to `c448b158a`), `c423b159b` C2M1, `c447b159a`/`c447b159b` C4M1. Open: card 457
  (one host crash in `c423b159a`, the game's save thumbnail on a null viewport; 3 whole runs since with no repeat).
  Candidate, not carded: the card 323 gib join replay sent 1 of 109 S rows with class path `-` (part 469, data
  class without a path), so the rejoiner logs `WARN card137 ... class not found (-)`; the host should skip and
  count such a row.

- 2026-11-16 19:40 user asked why work waits on the playtest: most of it need not. Only hearing and feel checks (321, 451,
  454, 455, 295, 298 look-down) and playtest numbers (226 step 2, 319) need the user; everything else is worked now.
  Started: card 298 fuel (trace: `STPlayerMechAttributeSet` is not in `kC20ReinitSets`, attributes.cpp:357; fix
  ordered as b160 with run `c447b160a`, predictions in the junior message and card file), card 458 opened (gib join
  replay row without a data class).

- 2026-11-16 19:45 run `c447b160a` (C4M1, b160 `aba352e167b165bb`): 83 ok, 1 FAIL (line 111), EXCEPTION 0/0. Card 298 fuel
  fix works at the first client mech mount (client `card020 ... STPlayerMechAttributeSet re-init ... -> base 100.000`,
  camera re-init row reads Fuel 100/100). FAIL cause, from the log: the second mount (possession 518452092) got no
  `card018` camera re-init, so no card 019/020 fixes; client `MaxWalkSpeed now 0.0`; the client walk sent nothing and
  the host logged no server-copy `HandleSprintChange`. Not a b160 regression: `c447b150a` shows the same miss. Cause
  class: the re-init trigger keeps one pending ASC key (`g_c18.fix_pending_asc`), which any later player-ASC OnRep
  overwrites before the client possesses the pawn. Ordered as b161: per-tick own-pawn check (`key != fix_done_asc`),
  pending slot removed. Also in b161: card 445 unsigned age (`now - since_ms` wrapped to UINT64_MAX, owe dropped at
  1 ms; host `WARN card445` in this run). b160 does not go to lane 1. Open after b161: client mech Fuel shows the init
  value 100 while the host has 83 at the second enter; needs a host send (to card).

- 2026-11-16 19:45 next build batch after b161 (b162, all static-traced, one c423 + one c207 run):
  (1) card 435 sibling: `objectives.cpp:1436` counts `g_c435.join` only for `kC435JoinBit`; checkpoint snapshot
  frames (`kC49SnapshotBit`) replay Complete/CompleteSub through the same `kC49Replay` call, so a client that is
  behind would show old banners after a checkpoint load. Fix: treat `kC49SnapshotBit` the same in that line.
  `c207b157a` has no client callout inside a snapshot (clients were level), so proof needs a client that is behind.
  (2) card 458: skip and count gib join-replay rows whose class path is `-`, with the diagnostic row in the card.
  (3) card 457: log the three viewport pointers per save at the game-thread call.
  (4) card 459 diagnostic: client and host mech Fuel once per second while piloting.

- 2026-11-16 19:49 run `c447b161a` (C4M1, b161 `1dbd69c0ee8dba3a`): scenario passed, 84 ok, 0 FAIL, EXCEPTION 0/0. Senior
  read: predictions met. Both client mech mounts got `card020 ... STPlayerMechAttributeSet re-init` and
  `card018 ... camera re-init` (518850194, 518877468; about 1.1 s after possession, same lag as b159b); 5 camera
  re-init rows for 5 own-pawn possessions (one per possession, not per tick); host
  `server-copy HandleSprintChange(3.000)` at 518881092 (line 111 passes); no `WARN card445`. WARN classes all known
  (host card132 4, card273 6; client card140 3, card152 1, card191 6). Client Fuel after the second re-init reads 100
  while the host reads 77: card 459. Lane 1 gate: `c448` on b161 pending.

- 2026-11-16 19:53 lane 1 now b161 `1dbd69c0ee8dba3a` (kept copy `build-b5/dwmapi-b161-1dbd69c0ee8dba3a.dll`; previous b159
  `885a2976f01932ac`). Gate: `c448b161a` (C1M1) passed, 72 ok, 0 FAIL, 2 expected try-fails (lines 32, 74),
  EXCEPTION 0/0, WARN classes equal to `c448b159a`; `c447b161a` passed. Junior's diff reviewed: the client check
  moved out of `card018_slot_step` (which stops 300 s after LoadMap with `card017_step`) into
  `card018_client_step(engine)`, called every client tick from hooks_ubw.cpp:170; per tick it costs a world, PC and
  ASC lookup and one key compare. Accepted. That 300 s window also explains part of the old miss: before b161 a mech
  mounted later than 5 minutes into a map could never get its pending re-init from the tick. b161 holds: card 298
  fuel re-init and every-possession re-init, card 445 unsigned age fix. Next batch (b162) is the 19:45 entry.

- 2026-11-16 19:53 b162 ordered from the junior (items 1-4 of the 19:45 entry; runs `c423b162a`, `c447b162a`). Senior stops at
  20:00: the junior's b162 report is unreviewed until the next session; b162 does not go to lane 1 before that review.

**2026-11-16 19:58 — open-card sweep (senior).** Closed on existing run evidence, moved to `docs/cards/done/`, CARDLOG rows
added: card 428 (run `c423b159b`: 0 `text R ... no row` WARN, 2 applied `text R <guid> S|T` rows, digest 12 rounds 0
mismatches) and card 430 (all `c423` client logs b131 to b159: 40 of 40 client nuke shots reload within 119 ms, 0
`WARN card430`). Card 140: the host body-0 patch (`c140h_patch.py`) is already in `hit_c132.cpp:202`; the 3 client
`WARN card140 ... kHitDrift 300` rows in `c447b161a` are the known live-NPC movement lag class, no action. Lane 2 runs
`c423b162a` (junior, started about 19:56).

**2026-11-16 20:00 — open-card sweep, part 2 (senior).** Closed on run `c423b159b`: card 253 (three predicted
`card253: client count` rows) and card 232 (12 digest rounds, 2 command rounds, no gap, 0 WARN). Cards 236 and 263:
fix built, proof owed on a FED_C3M2 run (ride, FAST=0, for 236); batch with the C3M2 soak (card 243). Card 437: parts
A, B, C proven in `c423b159b`; new part D (carried press counts the old weapon's round; host may hold fire 300 ms long)
written in the card, fix goes in the build after b162.

**2026-11-16 20:02 — `c423b162a` senior read (b162 `68ab71ad2d89e6ef`, lane 2).** Passed, 474 ok, 0 FAIL, EXCEPTION 0/0.
Client `WARN card137 ... class not found (-)`: 0 (was 1 in `c423b159b`). The `card323 ... gib join replay` row now
ends `0 skipped without a class (card 458)`: the classless row did not occur in this rejoin (18 at rest, 1 in flight),
so the skip itself is unexercised; the card stays open as a watch on that count. Card 457 rows present (3 saves, all
four pointers non-null). One new host WARN: `WARN card455: host ... relayed hurt sound
MS_GORE_Human_Player_Armour_Damage to BP_PlayerController_C (not sent)` at 519596612, 251 ms before the rejoin's join
replay: a hurt sound for a client whose connection is not ready yet. Not from a b162 edit; to trace (does the send
path refuse a rejoining client by design). Client WARN classes otherwise a subset of `c423b159b`. `c447b162a` and the
junior's report still owed before the gate and the lane 1 copy.

**2026-11-16 20:05 — `c447b162a` read, gate and b163 ordered (message `722de0f7`).** `c447b162a` (b162, FED_C4M1):
passed, 84 ok, 0 FAIL, EXCEPTION 0/0, WARN classes known (host card132 5; client card152 2, card191 2). Card 459
confirmed: host Fuel drains 1 per second on both mechs (pc 1: 99, 98, ...), the client's own mech stays 100.00 for the
whole mount. The `WARN card455 ... (not sent)` row of `c423b162a` is `c95_send_one` refusing a PC before its join
replay (`player_c99.cpp:668`), no sound lost that the client could hear. Junior next: gate `c448b162a` (pins to
`68ab71ad2d89e6ef`), then b163, lane 2 only: (1) card 459 host fuel send to the mech's own client, (2) card 437 part D,
(3) card 455 row without WARN for a client that is not ready, (4) card 453 door diagnostic row per door class, (5)
card 289 proof step in the C3M2 run. Runs: `c423b163a`, `c447b163a`, one FED_C3M2 ride run that also carries the owed
proofs of cards 236, 263, 274/276. Lane 1 stays b161 until the b162 gate passes; b162 is unreviewed in source for
items 1 to 4 until the senior diff read below.

**2026-11-16 20:08 — lane 1 now b162 `68ab71ad2d89e6ef`.** Gate `c448b162a`: passed, 72 ok, 0 FAIL, the 2 expected
try-fails (lines 32, 74), EXCEPTION 0/0, digest 0 mismatches in 5 rounds, WARN classes equal to `c448b161a`. Runs
`c423b162a` and `c447b162a` passed (entries above); senior diff read of the b162 edits (cards 435 sibling, 457, 458,
459 diagnostic) accepted. Copied from lane 2 with no game running, hash verified; kept copy
`build-b5/dwmapi-b162-68ab71ad2d89e6ef.dll`. Previous lane 1: b161 `1dbd69c0ee8dba3a`. Card 455 `not sent` row: the
junior traced the refusal to the card 139 hold (`not ponged yet`), first Pong 233 ms after the send. The junior builds
b163 (lane 2 only).

- 2026-11-16 20:11 senior sweep part 3. Card 288: client-alone prediction row is in `b37` to `b40` (`1 door
  reactions left to the host's DoorOp, 0 run`), `b40` passed with 0 FAIL; the card stays open only for the "chain delay
  over 250 ms" case, which no map has shown. Cards 311, 313, 315: `b40` has the `card311 ... replayed Activate(1)` rows
  and 0 range-target mismatches in its 2 rounds after the load, but the load came after the range (timeline rows show
  `+ 0 ms`, position at the end), so the 313 age prediction and the 315 phase fix (not coded, step 1 only) are not
  proven; they need a load during the grenade range. Card 316 is a scenario card. Cards 300 (digest entry for
  `BP_DestroyableActor`), 306 (FinishSpawning hook), 307 (timed WARN follow-up): the "not coded" parts are diagnostics
  or a one-Tick window with no observed fault; no build item. New card 460: digest dead bit ignores the Blueprint
  `Destroyed` bool, permanent `BP_SatchelBuilding_CTF_C_0 dead host 0 client 1` mismatch after a checkpoint load on
  C1M1 (`b40`); fix goes in b164 with the card 453 crush fix.

- 2026-11-16 20:23 b163 `67d43ad88c956cf3` (lane 2) senior read. `c423b163a` passed (474 ok, 0 FAIL, EXCEPTION 0/0, 0
  mismatches): card 437 part D proven (`held release ... done: the copy fired 7 of the client's 7 rounds`, 0 `ended
  short by the bound`, host `press carried to the new weapon, 1 round(s)`); card 455 WARN gone, not-ready path not
  exercised. `c447b163a` passed (84 ok, 0 FAIL): card 459 fix works, client Fuel 76, 75, 74 ... follows the host's
  pc 1 rows on the second mount; open: the first client row after each mount still reads 100.00 for about 1 s (b164
  item). Card 453 scan prints `0 class(es) ... (no ASTDoor)` on C2M1 and C4M1; the C1M1 gate decides whether the scan
  or the maps are the reason. Ordered: gate `c448b163a`, then the FED_C3M2 run; b164 = card 459 first-second gap,
  card 460, autodropship `n == 0` row not a WARN, card 453 crush fix after door classes are known.

- 2026-11-16 20:28 gate `c448b163a` passed (72 ok, 0 FAIL, the 2 expected try-fails at lines 32 and 74, EXCEPTION 0/0,
  0 mismatches, WARN classes equal to `c448b162a`). Lane 1 now b163 `67d43ad88c956cf3` (copied from
  `build-b5/dwmapi-b163-67d43ad88c956cf3.dll`, hash verified; previous b162 `68ab71ad2d89e6ef`). Card 453 scan prints
  0 classes on C1M1 too, so the scan is wrong (b164). Card 459 first-second gap: the client's mech attribute set is
  made by the card 020 re-init about 1040 ms after `SetPawn` (rows 520922813 and 520923853), the same ms as the stale
  100.00 row.

- 2026-11-16 20:29 card 453 closed: no `ASTDoor` instance on any Trooper map (b163 scan rows and every `card321`
  map row, 8 maps); reopen with the bug campaign. b164 = card 459 first-second gap, card 460, autodropship row.
  Junior runs `c236b163a` (FED_C3M2 ride run: cards 289, 236, 263, 274, 276 proofs).

- 2026-11-16 20:31 `c236b163a` (b163, FED_C3M2 ride run) passed: 65 ok, 0 FAIL, EXCEPTION 0/0, 0 mismatches. Card 289
  closed (client `credited from the host row (count 0 -> 1)`). Card 236 closed (0 splines not resolved, join check 14
  of 14). Card 263: frame windows 593 to 601 per 10 s but table peak only 27; stays open for the C3M2 soak. Cards
  274/276: no rows in this run (no client fire step); watch stays.

- 2026-11-16 20:37 senior: **card 461 (play blocker on FED_C3M2 full ride)**. `c236b163a`: 10 ms after the card 399 release
  warp the client's server copy lands 1040 uu away with `DamageType.Fall` 205 (armour 100 -> 0, health to -5, only
  the test god mode kept it up), and 0.5 s later it is 5453 uu from the host. Static cause: the released pawn keeps
  the dropship as movement base and gets its flight velocity imparted when it starts falling. Fix and predictions in
  `docs/cards/461-ride-release-fling-c3m2.md`, ordered for b165. The `c418` 62092 uu gaps are scenario warps, not this.
  Card 236 close amended: `c236b163a` did not exercise the resolve path; any `not resolved` WARN reopens it.
  b164 `762d8c2f6aae855c` (cards 459, 460, autodropship row): `c447b164a` passed, card 459 accepted with the
  junior's prediction (receive applied 1, read back equals host; one tick of 12-17 ms before the first frame stays).

- 2026-11-16 20:43 senior: gate `c448b164a` passed (0 FAIL, 0 mismatches in 5 rounds, EXCEPTION 0/0, WARN set equal to
  `c448b163a`). **Lane 1 now holds b164 `762d8c2f6aae855c`** (kept copy `build-b5/dwmapi-b164-762d8c2f6aae855c.dll`,
  previous b163 `67d43ad88c956cf3`). Card 461 (C3M2 ride fling) is still in it; fix is b165.
  `c460b164a` (FED_C3M1 Flakbug route) ran clean but proves nothing for card 460: the load gave no c98 reconcile row
  for a satchel building. Proof owed stays the b40 CTF route (FED_C1M1 load after the CTF satchel blast).
  New WARN rows at the checkpoint load in `c460b164a`, not traced yet: host `WARN card130` x11 (grenade FX check on
  a destroyed component), client `WARN card132 ... CollisionOp NODE_AddStaticMeshCompo ... no placed copy matches`
  x38, client `WARN card177` sprite death with no host row for 3006 ms.

- 2026-11-16 20:44 senior: card 462 written: after the `c460b164a` checkpoint load every construction-component CollisionOp
  reached the client twice (38 applied + 38 skipped, hashes pair exactly); no state lost, 38 wasted frames per load.
  Diagnostic rows ordered for b165. The load's `WARN card130` (FX check on a destroyed component) and `WARN card177`
  (sprite death rested locally after 3006 ms) rows are load-boundary fallbacks working as built: no card.

- 2026-11-16 20:53 run `c236b165a` (b165 `1a92573ebf4a81b5`, lane 2, FED_C3M2 FAST=0): card 461 fix failed, cause wrong. The
  release base is ground geometry with velocity 0; the client's pawn is relocated 5382 uu inside one host frame
  (warp spot, then a landing 1040 uu east with Fall 205, then rest at (-46424, -23545, 2037)), by code no row names.
  b165 does not go to lane 1 (lane 1 stays b164 `762d8c2f6aae855c`). b166 (junior): SetBase clear removed, mover
  diagnostic (moves over 60 uu of the released pawn with a 10 frame stack, Fall damage stack) for 600 ms after a
  release; runs c236, gate c448, b40 (cards 460, 462). Card 461 result section has the rows.

- 2026-11-16 21:07 run `c236b166a` (b166 `d88eca03b8b88da0`, card 461 diagnostic): rc 0, no VOID. Moves of the
  released pawn are engine CharacterMovement steps under `ServerMove_PerformMovement`: depenetration
  from the host capsule (warp lands at host z+100), then Falling with velocity (49182, -12236, 0) =
  seat -> warp spot over one tick, fall damage 205, gap 6305 uu at +0.5 s. Candidate cause: imparted
  movement base velocity from the host pawn at the Walking -> Falling switch. b167 = confirm rows plus
  fix (clear the impart-base-velocity bits around the release warp). Third and last run of this batch.
  b166 is diagnostic-only for 461; it can go to lane 1 if gate `c448` and `b40` pass.

- 2026-11-16 21:14 b166 `d88eca03b8b88da0` accepted: gate `c448b166a` passed (0 FAIL, 0 mismatches, 0 EXCEPTION), `b40b166a`
  passed (423 s, rounds 1-13 0 mismatches). Lane 1 now holds b166 (copied from the hash-checked kept file, no game
  running; previous b164 `762d8c2f6aae855c`). Card 460 closed. Card 462: second defect found (two pickups share a
  journal key), C3M1 run still owed. New card 463 (level script weapon removal leaves the host's copy of the client
  armed after a load; also in the 2026-11-14 b40 run). b40 lines 1150 / 1151 (card 073 rows after the respawn)
  TRY-FAIL in both b40 runs, untraced. New scenario `c315-range-load-c1m1.txt` (load during the range, cards
  311 / 313 / 315), not yet run.

- 2026-11-16 21:18 run `c236b167a` (b167 `a183768a97f78666`): card 461 fling fixed (impart-base-velocity bits cleared around
  the release warp): no fall damage, Health 100, rest spot equal on both sides. Still `WARN card046` gap 437 uu
  (limit 200): the warp lands on the host's capsule. Follow-up in the next build: land beside the host. Gate and
  the c460 / c315 runs on b167 pending.

- 2026-11-16 21:22 gate `c448b167a` passed (0 FAIL, 0 mismatches, 0 EXCEPTION). Lane 1 now holds b167 `a183768a97f78666`
  (copied from the hash-checked kept file, hash verified after the copy, only lane 2 games running; previous b166
  `d88eca03b8b88da0`); card 461
  fling fix is in it, the 437 uu landing gap remains.

- 2026-11-16 21:33 Runs on b167 `a183768a97f78666`. `c460b167a` (card 462) VOID for its goal: failed at the satchel C plant
  before the load, logged cause a scenario fault (client hold started 427 ms after the warp while falling, trace
  lost 1.48 s in); scenario now waits `stand`; rerun `c460b167b` live. Card 462 "shares name + component" defect
  void by code read (journal and client lookup already use the spot). `c315b167a` passed 197 lines and proves card
  315 step 1: after the checkpoint load the client's range target timelines run 1.57 s behind (host 0.420, client
  4.850 of 6.00; one 1912 ms client frame right after the state row), 6 `WARN card132: mismatch` rows, none before
  the load. b168 batch: card 461 landing beside the target, card 463 (Restore skip mark, whole-list mirror, client
  script skip), card 315 fix (host repeats playing looping timeline state on the 3 s scan, client corrects > 100 ms).
  b40 lines 1150 / 1151 replaced by a wait on the host `items 0` row (0-grenade outro loadout, not a sync gap).

- 2026-11-16 21:37 senior: run `c460b167b` passed (139 ok, digest rounds 1-4 after the C3M1 checkpoint load 0 mismatches). Card 462 cause found: 29 client `WARN card095 ... skipped`, all `BP_PylonSpline_C_<n>` `NODE_AddStaticMeshComponent-3[_<k>]`; the host's in-place load re-runs the user construction script and journals the new component names, the client's fresh map has the first-run names. Fix (host skips construction-script writes) is in the b168 batch with 461 landing, 463, 315. Card 207 same-spot shots still owed: the client `c460-after-load` shot shows the operation title screen ("press any key") and the `c315` host shot looks at the ground. Scenario `c460-load-c3m1.txt` now dismisses the title screen on both sides before the shot (stale `card137 ... BP_SatchelBuilding` try wait removed); next c460 run gives the pair.

- 2026-11-16 21:39 senior: card 434 closed from logs on disk (`c399b141a`, `c423b163a`). Card 436 numbers met, client Spawn-sequence shot owed; card 438 wound shot owed. New scenario `tools/scenarios/c439-underslung-melee-c1m1.txt` (card 439 open items) rides with the b168 runs. Card 385 still unproven: no stalled clock round in `c315b167a`.

- 2026-11-16 22:29 b168 `0c097c2488d82959` (lane 2) senior read. Gate `c448b168a` passed (0 mismatches). Closed: card 461
  (`c236b168a`: gap 150 uu, 0 WARN card046, 0 fall damage), card 463 (`b40b168a`: 0 WARN card327, both copies
  Unarmed), card 315 (`c315b168a`: 1 correction per target after the reload, then in phase). Card 462 fix is inert
  (`c460b168a`: same 29 client skips, host construction tally 0); corrected cause in the card: construction-script
  node components (`NODE_Add...`) have per-process numbers, the client makes the same writes itself; b169 keeps
  those writes local. New card 464: a satchel 'S' journal row survives a checkpoint load that revived the building
  (`BP_SatchelBuilding_Flakbug_C_2` dead host 0 / client 1, rounds 3 and 4); `c98_load_actor` reconciles only K and
  H. Card 439: c439g proven live, melee burst owed. Cards 274 / 276 not proven on C3M2 (no bugs at the spot). Lane 1
  stays b167 `a183768a97f78666`: b168 has a failed proof (462). b169 batch: 462, 464 + sweep, card 436 no-key patch,
  card 315 WARN to plain row, c460 shared look target (card 207), c236 spot with bugs, c439 burst.

- 2026-11-16 22:30 card sweep: closed and moved to `done/` on existing run evidence: 311 and 313 (`c315b168a`: client
  `replayed Activate(1)` rows, aged timeline rows, 0 mismatches), 437 (part D, `c423b163a`), 459 (`c447b164a`).

- 22:38 2026-11-16 open-card sweep part 3 (senior, b160 to b169 logs, no run spent). Closed with CARDLOG rows: 307 (every join and rejoin spawns beside the Up player, `c448b168a`, `c460b168a`, `b40b168a`), 316 (driver card, muzzle trace in `tools/drive.py`), 309 (C2M1 whole map 532 decals, 0 rows dropped, `c423b163a`), 312 (client weapon list matches the host, underslung switch on the copy, `c439b168c`), 333 (0 `WARN card327`), 432 (29 unchanged visibility rows not sent, client 0.18 to 0.88 ms per Objective frame, was 2.3). Left open with a written next step: 385 (7 stalled-rtt rounds occurred in b161 to b168 but the offset in use is not logged; owed: session offset on the `client clock` row), 308 (C2M1 peak 372 of 1024 and 137 of 512, 0 dropped; C3M1, C3M2, C4M1 have no totals row because the scenarios end by `quit`; owed: totals rows at quit), 328 (step 1 rows read: host on its floor, location current, copy 201 to 209 uu above the host every 600 ms, client floor trace reports a blocking hit at z 0, so both step 1 predictions failed; step 2 names the body under the copy). First read of `c460b169a` (b169 `d99dcd4bd6123245`, junior's report still owed): drive passed, client 0 `WARN card095` (was 29), host `29` and `58 script-node component writes kept local`, host `load reconcile c98 S ... BP_SatchelBuilding_Flakbug_C_2: alive, row dropped`, digest 0 mismatches, 0 EXCEPTION. Second collision replay is still 1459 rows (destructibles re-set by the load), sent as 1440 pairs in 32 frames, so its wire cost is small; not carded.
  Owed for the build after b169 (diagnostics, one batch): card 385 session offset on the clock row; card 308 journal totals rows at scenario quit; card 328 step 2 floor-body names.

- 2026-11-16 22:48 2026-11-16 b169 `d99dcd4bd6123245` accepted (senior review of junior report, logs read). Runs: `c460b169a`,
  `c439b169a`, `c236b169a` passed; gate `c448b169a` failed at line 18 by a scenario race (`client ui continue` ran
  10 ms before the client's IntroStart; fixed with a wait on the `card135 ... IntroStart` row), `c448b169b` passed:
  0 failed lines, 0 EXCEPTION, WARN attach and GOD only, card 448 kept Health 30 restored, 5 digest rounds 0
  mismatches. **Lane 1 now holds b169 `d99dcd4bd6123245`** (copied from `build-b5/dwmapi-b169-d99dcd4bd6123245.dll`,
  no lane 1 game running, hash checked after the copy). Closed: 457 (one-off), 422, 439, 462, 464. `c236b169a`:
  copy shots 63, player hits 44, 0 `found no round`, card 289 secret credited on both sides, join check 14 of 14.
  Still open after b169: 436 (client totals 0 and 0, but the host totals row prints only at a host map change, and
  the Spawn-sequence shot is not reached), 438 wound shot, 315 plain row unproven (C1M1, C3M1, C3M2 have 0 looping
  timelines), card 207 same-spot shot (host pawn blocks the client view in c460: client warp needs a side offset).
  Load-stale candidates from the 464 sweep, not carded: L/B timeline position, T turret, F closure. New client
  `WARN card152 bone check` on spawned-then-killed warriors (ragdoll) is the known card 152 class.
  Next build batch: card 444 patch (scratchpad `c444_patch.py`), diagnostics 385, 308, 328 step 2, card 436 host
  totals at session end.

### 2026-11-16 22:59 open-card sweep while b170 builds (senior)

- Card 140: the remaining client `kHitDrift` WARN is a real bug. The client held the game's `SetSimulatePhysics`
  but not the direct `SetAllBodiesSimulatePhysics(1)` that follows it, so a dying bug's copy ragdolled on its own
  for up to 0.5 s (2 s max) and then jumped to the host's corpse (c423b163a: 21 of 23 held starts). Fix in
  scratchpad `c140w_patch.py`, for b171. Predictions in the card.
- Card 219 closed (c460b169a: from-save join 1459 of 1459 rows, 0 dropped). Card 465 opened: the journal peaks at
  71 % of its cap after a C3M1 load; cap 2048 to 4096 plus a 75 % WARN, for b171.
- Card 270: montage hold proven (b40b166a, b40b168a). The one remaining `client-only DoorState 1 -> 3` WARN is the
  door's own close delay firing on the client 8 ms before the host's row. Fix in scratchpad `c270b_patch.py`, for
  b171. Predictions in the card.

### 2026-11-16 23:07 — old cards closed from logs; b170 pre-review

- Closed (moved to `cards/done/`, CARDLOG rows added): 223 (late hit rows plain; c423b163a 24 hits, largest drift
  1 uu), 200 (c460b169a map arm keeps door 127, dropship 5; replay 127 of 127), 250 (fixed by card 350), 218 (flight
  cause fixed by 159/236; `STAISpawner_5` after a death-screen load is the game's own load), 197 (Chariot via 269;
  Jumper always gibs; M7Mech never died: watch).
- b170 `38a7191d84a4887c` pre-review: gate `c448b170a` passed (rounds 4, 5 mismatches 0; AmmoMirror 234/198/42 bytes
  per window). `c460b170a` scenario passed but client round 2 has `mismatches 2 (Pose:2)` and two `WARN card125`
  turret rows. Both traced to latent causes, not b170 changes: card 466 (pose row dropped the tick a corpse returns to
  relevancy over 15 s after its row; patch `c466_patch.py`) and card 467 (spawned turret row beats its copy after a
  rejoin; one-shot row lost). b170 goes to lane 1 once the junior's b170 report shows no failed proof; lane 1 stays
  b169 `d99dcd4bd6123245` until then.
- b171 batch: `c140w_patch.py` (140), `c270b_patch.py` (270), `c466_patch.py` (466), card 465 cap, card 467 table.

### 2026-11-16 23:08 — rejoin cards 190, 194, 203 closed from c423b163a

- 190: join check 25 of 25 and 24 of 24, 0 `WARN card159`, rounds 0 mismatches. 194: rounds after the rejoins 0
  corpses over 100 uu; the rehold path not reached in 59 logs (watch row in the card). 203: movers removed by cards
  152 and 269; same rounds. New cards 466, 467 are in the b171 batch (sent to the junior).

### 2026-11-16 23:15 — b170 accepted, lane 1 = b170 `38a7191d84a4887c`

- Junior's b170 report: c448b170a, c460b170a, c423b170a, c236b170a all pass, 0 dumps; c423 rounds 1 to 11 and c236
  round 4 mismatches 0. Card 444 proven (AmmoMirror peak 394 B per 5 s window, was 2588; magazine rows 58 to 0 over a
  7 s hold). Card 328 step 2 drift rows name the Landscape floor (20 rows, 0 WARN). 308/436 quit totals present.
  Card 315 plain row not reached (C2M1 has no looping timeline correction).
- The c460b170a Pose:2 and turret WARN rows are latent (cards 466, 467), not b170 changes: no regression, so b170
  was copied to lane 1 (hash checked after the copy, no game running).
- Card 385: the new session row shows a real fault after a rejoin (-982 ms for 3.1 s); `c385b_patch.py` added to
  b171. Card 436: 88 % of held Anim rows expire with no NetGUID (NPC not relevant to the client): as designed.
- Card 468 (low, cosmetic): after load and rejoin the host's objective rows carry the new-row highlight and the
  client shows the mission location card; senior viewed both shots.

### 2026-11-16 23:24 — playtest waits turned into proofs; card 470 flare hop (senior)

- User rule: only hearing (321, 451, 455, 314) waits on the playtest. Card 469 lists the measured proof for each
  other item (295, 298, 317, 319, 320, 454, 226 step 2) and what the logs already prove.
- Real bug from that pass: card 470, a thrown flare is set at once on every sample of its fast flight on the client
  (`b40b168a` 6 hops). Fix `c470_patch.py` (scratchpad, check ok), for the build after b172.
- Card 319: every `missed` NPC class on the client is missed on the host too (same capsule values): the game's own
  filter. Mech and turret samples still owed.

### 2026-11-16 23:26 — card 471: destroyed turret row replayed, turret table never frees (senior, from c460b172b)

- `WARN card467 ... no turret copy after 10000 ms` in c460b172b is not a late copy: the host destroyed that turret at
  the checkpoint load and its 'T' journal row stayed. Second defect, same table: `g_c125s.row[32]` never frees a slot,
  so after 32 turrets (placed + every respawn) a new turret gets no rows, silently.
- Fix `c471_patch.py` (scratchpad, check ok: 6 edits), card 471. Goes with card 470 in the build after b172.

**2026-11-17 07:45 — b173 `dedb14d000d6ec36` reviewed (senior).** Fixes in: 470 (flare hop), 471 (destroyed turret row, table
slot freed), 228 item 2 (mech counted at a gated presence; no Trooper route, kept as hardening). Runs: gate
c448b173a/b pass (0 mismatches; 385, 295 walk, 454 shake count proven), c460b173b pass (471 row seen), b40b173a
pass (470, 270), c423b173a card 140 hit drift 0 rows but client crash at the end-of-map travel: card 472 (kept NPC
ticks in the destination world, 1 in 166 travels since b80, not a b173 regression). Closed: 270, 385, 444, 465, 466,
467, 470, 471. New: 472 (fix in b174, junior), 473 (rested flare re-arms). Lane 1 stays b170 `38a7191d84a4887c`
until b174 passes c423 and the gate (a run with a crash does not go to lane 1). c447 on b173 running for card 469
(298, 319).

**2026-11-17 08:00 — b174 `4efbc7838d907625` in lane 1 and lane 2 (senior).** Card 472 fix (kept NPCs stilled again at the
seamless arrival). c423b174a pass, gate c448b174a pass (0 mismatches), no crash; c447b173a pass (cards 298, 319
proven). Lane 1 copied from `build-b5/dwmapi-b174-4efbc7838d907625.dll`, hash checked after the copy. Closed: 472,
473 (traced, no change). Card 469: only 320 shot pair and 226 at 20 fps owed. Open from c447b173a: 4
`WARN card140: client hit` rows on C4M1.

**2026-11-17 08:15 — card 469 proofs complete; card 474 found (senior).** c469b174a (client at 20 fps): no pace backlog in
play, card 226 closed. c469b174c: card 320 shot pairs on FED_C2M1, closed. New card 474: frames held in the host
pacer during a client hitch are stamped at send, read as fresh, and hit effects play up to 520 uu off the bug
(c447b173a). Fix in b175 (junior): enqueue-time stamp, late hits moved onto the copy.

**2026-11-17 08:17 — watch cards closed with measured proofs (senior).** 295, 317, 319, 274, 276, 458 moved to done; proofs in CARDLOG. Playtest-log watches for 274/276 (`found no round`) and 319 (gate row) stay.

### 2026-11-17 08:29 card 474 and 469 closed; lane 1 = b175

- b175 `63689adfcb1ec80e` (card 474: enqueue-time stamp with a held flag for paced frames; late or far hits on a
  live copy move with the copy). Gate `c448b175a` 0 mismatches; stall run `c474b175a`: held hits read frame age
  1153 and 913 ms, classed late, 0 `WARN card140`. Cards 474 and 469 closed (docs/cards/done).
- Lane 1 (`game/.../dwmapi.dll`) now holds b175 `63689adfcb1ec80e` (copied from the kept file, hash checked
  before and after, no lane 1 game running).
- Known limit kept: for one 30 s clock round after a reconnect the session offset is off by the one-way
  latency (low rtt 0, card 385 note); +11 ms in `c448b175a`.
- Card 303: the client's 1.1 to 1.2 s mount/leave frame is inside the `OnPossessedPawnChanged` broadcast at
  the end of `AController::OnRep_Pawn` (exe+0x3B3511C), by row order and disassembly. b176 `ac991b60d632da66`
  is the diagnostic build (three stack samples per long frame); run `c447b176a` names the handler.

### 2026-11-17 08:42 card 303 closed; lane 1 = b177

- Cause of the 1.1 s frame at every mech/turret mount and leave (and after each travel's HUD build), both
  machines: `USTSafeZoneScale::NativeConstruct` > `GetDisplayMetrics` > `GetSizeForDevID` (setupapi EDID read),
  about 1.1 s per call under Proton. Found by the b176 frame sampler (3 stack samples per long frame).
- b177 `e7197c28f8ab7bb8`: session cache of `GetSizeForDevID`, both roles. c447b177a: client mount/leave
  longest frame 9 to 18 ms; only map-load frames remain on either side. Gate c448b177a 0 mismatches.
- Lane 1 now holds b177 `e7197c28f8ab7bb8` (hash checked before and after, no lane 1 game running).
- Playtest-log check added: `WARN card303 ... display size not cached`, or a mount that still hitches, reopens.
- Not carded (game's own): host 221 ms checkpoint-save frame, 354 ms first-spawn synchronous class load.
- Label nit left as is: card303 rows say "host" on a client before its session starts.

2026-11-17 08:49 senior: cards 308 and 296 closed from existing logs (308: journal peaks on five maps, world 372 of 1024,
objective 149 of 512, 0 dropped; 296: c423b174a, 4 client launcher shots). Card 297: candidate 1 proven on c447b177a
(rows plus client cockpit shot); the mech-fire step for candidate 2 is added to `tools/scenarios/c447-c4m1.txt`
(rides the next c447 run). Card 299: every `card299` row so far was on the LOS_O02 satchel buildings (instances named
`..Bugnest_HumanGameplay_C_0/_7`, class `BP_SatchelBuilding_Plasmabug_C`, no STMeshUpdate); the playtest's nest is
`_C_5` (real class, 'K' path), never killed in a run. A card 299 block is added to
`tools/scenarios/c423-gates-c2m1.txt` (kill `_C_5` by code, rows and before/after shots on both sides); rides the
next c423 run, no build needed. Junior: runs A/B for the 294, 290, 283 close-out proofs on b177.

2026-11-17 08:50 senior: cards 294, 290, 283 closed on junior runs c294b177a and c283b177a (b177; rows and shots checked).
New WARN noise class: host `WARN card461 ... moved N uu ... not through MoveComponentImpl` at a ride release, vertical
and under 100 uu (note in done card 461). Next junior batch: c447 (card 297 mech fire step) and c423 (card 299 nest
`_C_5` block), scenario only, no build.

2026-11-17 08:54 senior: scenario proofs added, no build. Card 300: closure block at the end of `c423-gates-c2m1.txt` (host in a
closure presence alone, then the client; rows and two shot pairs). Card 304: new `tools/scenarios/c304-far-fire.txt`
(FED_C3M1, host Morita fire near and about 8600 uu away; client `audio row EPulse44Fire_MS` rows and the tally), run
c304b177a queued after the junior's c447b177b / c423b177a batch. K row relayed 4 times per nest kill (card 299 note):
traced, harmless (client `c98_client_kill` skips "already dead"), costs 3 rows per kill; fix for the next build batch:
`c98_host_kill` (`world_c98.cpp:790`) adds `c98_journal_row(actor, kC98KindKill) == nullptr` to its `relay` test (the
load-revive path at line 877 unjournals, so a revived building relays again).

2026-11-17 09:08 senior: junior runs c447b177b / c423b177a read (rc 0, 0 EXCEPTION, digest 0). 297: attack tag reaches the
client mech (tag rows), shot inside the burst owed (c447b177c, client UBWCOOP_C76_LOG=1). 299: nest rows equal on both
sides, shots owed (looks fixed). 300: no entry at 119-160 uu, retry at distance 0. New card 475: client FedScore
140000 against host 160000 after a rejoin; no score sync in the mod, fix = host total sent, client sets field +0x44
and broadcasts. Next batch: c447b177c, c423b177b, c304b177a.

- 2026-11-17 09:21 Junior batch 2 on b177 read. Card 297 closed (c447b177c: client shot rows every 160 ms, client shot shows the
  arm gun firing). Card 299: c423b177b host and client shots show the hole closed before and open after; rows equal;
  closing with the next passing c423 run. Card 304 measured: near row 149 uu and far row 8626 uu both with
  `attenuation override ATT_WPN_MoritaMK1` (host weapon is the Morita; scenario pattern fixed to MoritaMK1Fire_MS);
  0 bug-case rows; tally row owed (needs a map end or a tally at quit). Card 300 not reached.
  NEW play blocker, card 476: host crash at exe+0x14CBD9E when the departed client's frag exploded
  (`c99_copy_of` walks a freed instigator). b177 on lane 1 has it, as did every build since b170 at least; next
  build b178 = card 476 + K-row dedupe in `c98_host_kill`; card 475 (FedScore, design on the card) follows in b179.

- 2026-11-17 09:57 b178 `f6596be3fcb2d778` passed c423b178b/c, c304b178a and gate c448b178a; copied to lane 1 (hash verified),
  lane 2 same. Cards 476 and 299 closed. Card 304 measured part done (hearing waits). Card 300 needs the route
  driven to LOS_O05_SO04. Known WARN noise added: host `WARN card476 ... its instigator is freed, the causer stands
  in`. Next build b179: card 475 (FedScore) + card 476 refinement (replace only a freed, not a garbage-flagged,
  instigator).

- 2026-11-17 10:35 b179 `e1cde94de299e31b` passed c423b179a/b and gate c448b179a; copied to lane 1 (hash verified), lane 2
  same. Card 475 closed (FedScore sync at join, digest Score entry). Card 476 refinement in (only a freed instigator
  is replaced). Card 300: route now reaches LOS_O05_SO04 and the waiting row fires; new mod bug: a player piloting a
  mech is never counted inside a card 227 presence (`c227_count`), so closures and gated presences hold while a
  pilot is in a mech. b180 = that fix.

- 2026-11-17 11:03 b180 `4c41c9c8455f774f` passed c423b180c and gate c448b180a; copied to lane 1 (hash verified), lane 2
  same. Closed: 300 (mech pilot counted at card 227 presences and door counts), 306 (client support swap proven, no
  crash). Open for b181: `c327_own_hash` (weapons.cpp:1232) hashes the mech's inventory while the client pilots, the
  host hashes the pilot's soldier, so the digest prints a false `WARN card327 ... PW differs` (c423b179a/b); the
  client must return kC327NoHash for a mech pawn, as its comment says. c321 door crush and c361 hazard zone with a
  mech pilot: which pawn GetPlayerCharacter(0) gives the host while it pilots is not read yet.

2026-11-17 11:05: card 298 closed on its measured proofs (rows and client shots of c447b177c). Playtest log check kept: a mech mount with no `card298 ... camera re-init` row reopens it. b181 batch ordered from the junior: `c327_own_hash` mech fix, pilot pawn row for c321/c361, host-side shot of the card 306 drop.

2026-11-17 11:11: user rule: the backlog must shrink. Every build carries a batch of old open real-bug cards; new finds get a card and are fixed at once only when they are a crash or a play blocker. Closed on existing evidence (202 logs of b150 to b180): 189, 221, 249, 256, 271, 324, 336. Reopen rows: 189 a corpse pose WARN after a fall over 1000 uu; 221 a `pickup host 0 client 1` digest row; 249 `LZQueryFinished` status other than 1 with no client copy; 256 a `BP_WeaponPickup_*` location mismatch; 271 a `WARN card271` row; 324 a hand with no flare; 336 a pawn moved by a fall zone after an arrival. Next: one backlog proof run (shots and rows) for the cards whose fix shipped with no verdict: 220, 225, 285, 286, 287, 288, 292, 381, 382; then fix batches for 215, 237, 239, 251, 269, 291, 293, 328, 359, 419, 468, 224.

2026-11-17 11:12: card 287 closed (covered by 470 and 473). Card 292: 203 client rows all `visible 0, interaction 0`, 0 WARN; only the client shot of a dead flak building is owed (add to c423). Card 286: release rows in 21 logs; only a client mid-flight shot showing one grenade is owed (b40 scenario).

2026-11-17 11:27: b181 `2c293b1ccc9ad233` (lane 2 only, not copied to lane 1: host crash in c423b181b). c423b181a passed, gate c448b181a passed (9 digest rounds, 0 mismatches, 0 EXCEPTION). Card 327: 0 `WARN card327 ... PW differs` with the client piloting across 2 digest rounds; the false warning leaves the WARN noise list. Pilot check rows: `GetPlayerCharacter(1) BP_BaseMech_C, controller pawn BP_BaseMech_C, c75_pilot_body BP_PlayerCharacterMI_C`: the mech is an ACharacter and is the body the original tests, so c321 and c361 must accept mech roster pawns (b182). Card 306: host shot `host-c423b181b-c306-after-swap.png` shows the dropped pickup. Crash c423b181b host: EXCEPTION_ACCESS_VIOLATION at ntdll.dll+0x3A41C reading 0x8, stack through ubwcoop+0x5260, in the card 303 watchdog thread (`c303_step`, RtlVirtualUnwind with a frame register outside the stack copy); the watchdog starts only under `config::test_skip` (stall_c303.cpp:164), so the play build does not run it. Lane 1 stays b180 `4c41c9c8455f774f`. b182 ordered: watchdog walk guard, mech pawns in c321/c361, card 419 fix with a FED_C5M1 run, c423 shots for cards 292 and 286, card 288 rows.

2026-11-17 11:28: plan for b183 (after b182 passes): card 237 with the FED_C4M2 soak (card 245). Card 237 step 2 diagnostic is not built (`SetCollisionProfileName` has no hook; digest_c132.cpp has no profile field; the digest key is still the name only). Skip the diagnostic-only build and ship the class fix the card names: relay `SetCollisionProfileName` on placed actors beside the collision journal, replay the actor collision flag at join, level-qualified digest key (`<level>:<name>`). Proof in the c245 route: detail query after `BrainBug_SO1`, host and client `hidden 0 actor collision 1 root collision 3`, 0 `StaticMeshActor_52` mismatches. Soaks 243, 244, 246, 247 follow one per build.

2026-11-17 11:43: b182 `d3a6c76b270110c3` passed: c423b182a (19 digest rounds, 0 mismatches, 0 EXCEPTION) and gate c448b182a (9 rounds, 0 mismatches, 0 EXCEPTION). Copied to lane 1, hash after copy `d3a6c76b270110c3`. Card 303 watchdog walk guard (`c303_frame_ok`) held through the level-end map change. Mech pawns accepted in `c321_copy_in_way` and `c361_copy_in_zone` (`c321_player_body`); not exercised (no door or hazard on the c423 route). Open gap: client `detour_c361_insta_kill` treats only the trooper class as a player, so a client's InstaKill of its own mech runs locally (b183). Cards 292 and 286 closed. Card 288 not exercised by c423 or c448; needs a route with an entry door.

2026-11-17 11:47: Closed 239, 224, 381 under the backlog rule (fix or trace done, proof not reachable by the harness). Playtest log checks added: card 381 (a `WARN card381` row or a client stuck on the loading screen after a menu route reopens); card 239 (a pickup digest mismatch after a crate death reopens); card 224 item 3 stays on the list. Card 328: step 2 rows read, the blocking-body reading is refuted (floor is the landscape on 362 of 536 rows, 0 `start inside`); step 3 per-frame trace specced in the card for b184. Open cards: 60.

2026-11-17 12:14: b183 `d6a486b94456960e` (lane 2 only, NOT lane 1): c419b183c and gate c448b183a passed, 0 EXCEPTION, 0 mismatches. Cards 419 and 285 closed. Regression, so no lane 1 copy: the new SetCollisionProfileName relay (card 237 item 1) sends writes on components the client cannot name: 298 client `WARN card095 ... NiagaraComponent_2147476983: no placed actor (or component) matches, skipped` rows in 83 s on FED_C5M1, 6 on C1M1 (`BP_ZiplinePoint_C_N ... Spline Mesh`). b184 fixes the filter. Card 237 not proven: `objective complete` cannot reach the shield write (the level script adds Plaza, Sewers, Bridge and BrainBug only through the real trigger chain); b184 adds a test-only command that runs the level script's shield toggle. Card 361 mech fix in, not exercised. Cards 220 and 225: rows only, shots owed. Cards 288, 382: no route reaches them. Scenario note: under AUTODROPSHIP the host's pawn is the dropship until the continue; gate on the map only, then `host wait state pawn=host-own`. `warp actor X 0` is rejected (range 10 to 5000) and the parse check misses it. Open cards: 58. Lane 1 stays b182 `d3a6c76b270110c3`.

2026-11-17 12:32: b184 `c145bca0829a932e` (lane 2 only, NOT lane 1): relay filter fixed (0 `WARN card095` on C5M1 and C1M1; third test: a component not loaded with the map is not relayed), `levelcall` test command added, gate c448b184a passed. Client crash in c419b184a at the frontend load after the host quit: card 477 (stale `g_c408.projectile` in `c419_begin`, new in b183); b185 fixes it. Card 237 closed (c237b184a 5 rounds 0 mismatches). Scenario note: `host travel restart` logs TransitionToLevel but loads no map within 60 s on a direct-travel start. Open cards: 58 (237 closed, 477 opened). Lane 1 stays b182 `d3a6c76b270110c3`.

**b185 `41c369126e01b57f` (2026-11-17 12:49): lane 1 and lane 2.** Card 477 fixed and closed: map-end reset of the cached
map Blueprint classes (c408 plasma, c161 doors) at LoadMap enter. c419b185a: 0 EXCEPTION, client reached the
frontend, 0 mismatches. Gate c448b185a passed (10 rounds, 0 mismatches). Copied to lane 1 (hash checked after the
copy). Card 328 step 3 trace is in the build (0 set-backs in both runs, no rows yet). Card 220: the per-player filter
is proven by the walk; the prompt shot is owed (client must walk in, not be warped). Card 225 is not reached under
FAST=1. New card 478 (server-side move of a client's pawn fires no client overlaps). Card 245 route written
(`tools/gen_c245.py`, `tools/scenarios/c245-c4m2.txt`); it needs `clear kill` to spare the Brain Bug. Open cards: 58.

**Card 478 closed (2026-11-17 12:50), test tool only.** `host warp client` moves only the server copy; the client's own pawn
follows by engine correction and fires no client overlaps. Play-time moves (`c46_warp_apply`) run on the client.
Scenario rule: client-local triggers (training prompts) are entered with `client goto`. Open cards: 57.

**Card 251 closed (2026-11-17 12:51) on existing evidence** (c423b178a to c423b182a: 198 rounds 0 mismatches, 0 pose WARN,
every `card251` row `cmc tick 0`). Reopens on a `Pose:` mismatch. Open cards: 56.

**Card 478 correction (2026-11-17 12:53):** a server warp of the client's pawn does fire client overlaps in most cases
(c448b185a line 76: `_0` and `_7` passed on the client); only the line 68 warp missed `_9`. Card stays closed (test
tool only), text corrected in the done card.

**Card 269 closed (2026-11-17 12:53) on existing evidence** (c423b181a, c423b182a: swap-back rows, 0 hidden arrivals, 0
`not posed after`, 0 mismatches, corpse on both shots). Open cards: 55.

**Card 293 closed (2026-11-17 12:55):** dwell rows over six runs show no mod fault (dead markers leave in one sample; the
blips without a visible enemy are live bugs still underground, same on both screens). Playtest check added: a host
`card293 ... IsDead 1` dwell over 1000 ms with a guid, `re-added` above 3, or one-screen blips reopens. Open cards: 54.

**Card 291 traced further (2026-11-17 12:58):** c423b182a still has waits to 1630 ms. The host's death-to-ragdoll time is fixed
per sequence (Death_05 2340 ms, Death_01 1565, Death_07 1350, Death_03 898); the client copy plays its own variant
and reaches its ragdoll call about 833 ms after the death. Fix spec in the card (host sends the death name at once,
client plays it), for b187.

**Card 468 closed (2026-11-17 13:01):** traced from the cooked widgets. Objective row highlight is `WB_Objective_List`'s own fade
timer (restarted by add/complete/callout events; a list built from existing objectives gets none); the location
card is `WB_Intro_Text` starting at the local pawn change and removing itself. No fix. Reopen: rows differing in
text, order or count, or a card that stays. Open cards: 53.

**Card 215 closed (2026-11-17 13:02):** corpse redesign proposal, verdict "keep the present design". Its held cards (191, 194,
197, 203) are closed and the `Pose:` digest read 0 mismatches over 198 rounds. Left in the class: 207, 359, 291.
Reopen: two new simulate/held transition cards, or an unexplained `Pose:` mismatch. Open cards: 52.

**Card 140 closed (2026-11-17 13:02):** the early client ragdoll fix (`c140w`) met its predictions in six C2M1 runs (b180 to
b182): 0 `WARN card140: client hit`, 0 "already simulating", finals 0.0 uu. Open cards: 51.

**Card 181 closed (2026-11-17 13:03):** 138 `card181 ... press ended` rows over 25 host logs (b162 to b182) all read client
rounds == copy rounds, unused 0; 0 `found no round`. Playtest check: unequal rounds, unused above 0, or a `found
no round` row reopens. Open cards: 50.

**Backlog pass (2026-11-17 13:05):** closed 264, 265, 284 (test tools in standing use; reopen conditions in each card). Cards
280 and 282 (diagnostics never built) merged into card 149: one client-side diagnostic build item (cause bracket
fix, load/play phase split, script entry rows). Open cards: 45.

- 2026-11-17 13:07: card 225 closed on run `c225b186a` shots (press-to-start, brief video clean; tag in game) plus the
  earlier pause menu run; reopen on a tag seen over any full-screen widget. Card 220 stays open: triggers pass
  per own pawn on both sides, but no prompt text is on the host's or the client's shot; b187 gets a log-only row
  for the prompt line widget state. Open cards: 44.

- 2026-11-17 13:09: closed on existing evidence, each with a reopen condition in its Closed section: 148 (far gib WARN is
  noise; a row within 15 000 uu of a client pawn reopens), 436 (client `not ready` 0, `unmatched` 0 in
  `c423b182a`), 438 (hold totals of `c423b144a`), 382 (guard in since b144, 0 `card382` rows ever; the forced
  window run was never done and is recorded as not done). Playtest-log checks added: client Anim totals
  `not ready`/`unmatched` above 0 (436), FX hold `ended while held` near a client or `list full` above 0 (438), a
  `card382` row followed by a `card380` row for the host's pawn (382). Open cards: 40.

- 2026-11-17 13:09: closed 228 (hardening in since b173; no Trooper map puts a pilot at a live gate) and 386 (test digest
  rounds 30 s apart with 0 mismatches; playtest-log check: timer rounds 120 s apart in the play build). Merged,
  not fixed: 427 into 330 (byte goal met, CPU timing item moved) and 441 into 431 (not built, 4 % of host bytes).
  Open cards: 36.

- 2026-11-17 13:10: closed 207 (log proof of `c207b157a`; the same-spot shot pair was never taken, recorded as not done) and
  327 (row in since b22, 0 `PW differs` WARN through b186; playtest-log check: any `WARN card327` row). Open
  cards: 34.

- 2026-11-17 13:12: card 322 closed. Static trace of its last open step: the out-of-bounds expiry (exe+0x51CD6E0) is a tail
  jump to `InstaKillTarget` with no authority check, already guarded on the client by cards 360/361 (and watched
  by 362). Playtest-log checks: a `WARN card362 ... had no Down frame` row, music or radio on one side only. Open
  cards: 33.

- 2026-11-17 13:16: b186 `6bdc1fb92f415071` final report. `c448b186a` and `c225b186a` passed; card 288 closed (prediction
  (c) met). `c245a` (C4M2 discovery) ended at 429 s by a client crash at exe+0x3AF2CB8 in the mod's card 269 swap
  back (`c269_swap_back` -> `SetShowingStaticMesh_Internal(false, true)` -> `SetHiddenInGame` on a dangling
  animator+0x128): card 479, fix in b187. b186 does not go to lane 1 (crash in its run); lane 1 stays b185
  `41c369126e01b57f`. c245a completed 5 of 35 objectives; first block Plaza_SO3 (take the flamethrower,
  `BP_WeaponPickup_FlameThrower_C_0`): the driver has no kind row for a weapon pickup objective (card 245, b187).
  Playtest check 479: an EXCEPTION at exe+0x3AF2CB8, or a `WARN card479` row. Open cards: 33.

- 2026-11-17 13:19: card 326 closed (sync registry wired at all five sites, 0 `WARN sync:` in b186 runs; gaps stay with 323). Playtest check 326: any `WARN sync:` row. Open cards: 32.

- 2026-11-17 13:48 b187 `920b53a5d0bb7c2e` report checked and copied to lane 1 (hash verified after the copy; lane 1 was b185 `41c369126e01b57f`). Runs c423b187a (C2M1, passed), c448b187a (C1M1 gate, passed), c245b187a (C4M2, stopped at line 339 after 236 s). 0 EXCEPTION/FATAL in all six logs. Digests: c448 13x0, c245 9x0, c423 15x0 plus one round-9 `Score:1` mismatch. That mismatch is the card 440 rejoin window: the client held the journal score 140000 until the host's card 475 join replay sent 162000 (host 582811292, client applied 582811764); round 10 was 0. Not a drift, not a regression. Proof results: 291 sequence names match with 0 fallbacks, but ragdoll waits are 228 ms (limit 100 ms, miss). 359 has 91 frozen-final rows, shots inconclusive (next time place players on the same side, +250 x +250 y). 281 MISS: `record_round` stored nothing (0 made rounds), the FIFO served claims 16-18 from rounds 19-21, then 3 `WARN card157 found no round`. 220: 3 `WB_Tutorial_Prompt_Line` widgets, visibility Collapsed, list 0, text '' (placeholder Final Prompt Message; c448 warp standoff too small). 479: no crash past the satchel pickup, 0 card479 rows; placement sites and BridgeExplode not reached. 245 MISS: WeaponPickup kind row works, but `objective go all` found no navmesh stand point around a switch and the look hit `BP_Switch_C_0` 176 uu away, so E was skipped; the stand point and aim for a pickup must come from the pickup actor's bounds. Next build b188 carries: 281 record_round early-return diag row, 245 pickup stand and aim, 220 visibility path, 291 wait cause, 328 step 3. Open cards: 32.
- 2026-11-17 14:14 b188 `7921dd8832c458f7` checked and copied to lane 1 (hash verified; the junior had not kept the file, so I copied `build-b5/dwmapi.dll` to `build-b5/dwmapi-b188-7921dd8832c458f7.dll` first). Runs c448b188a (passed, 202 s), c423b188a (passed, 463 s), c245b188a (stopped at 166 s on Plaza_SO3). 0 EXCEPTION/FATAL. Digests 0 except c423 rounds 14 and 15, Score:1, the same entry as b187; the cause is now known (host score gains after the join replay are never sent) and is noted on card 323. Card 291 closed: worst wait 2 ms, 0 fallbacks. Card 281: every skip is "impact list data null" on the client copy at launch, so `record_round` needs another hit list source or a later read. Card 245: the pickup stand and aim now work; the client's relayed pickup gave it the weapon, but Plaza_SO3 did not complete (a single-player assumption, card 301 class). The cascade again stopped card 479 short of the placement sites. Card 220: the prompt event is bound and fires, but Turn On Prompt is gated before it sets Final Prompt Message, on host and client alike. Card 359 shots (new placement) again show the other trooper, not the corpse. Card 328: z climbs in Walking with a stale FloorDist; a capsule sweep row is next. Scenario: the c245 use-stand wait regex now also matches pickup rows (generator fixed, file regenerated). Open cards: 31.
- 2026-11-17 14:56 b189 `d4181cdc840b944a` checked and copied to lane 1 (hash verified). Runs c423b189a (C2M1, passed, 470 s), c448b189a (C1M1 gate, passed, 229 s), c245b189a (C4M2, stopped at 295 s on Plaza_SO3). 0 EXCEPTION/FATAL, no crash dumps. Digests 0 in every reported round, Score included: the host's FedScore total is now sent on every change (card 323 score part fixed). Card 245: the client's relayed pickup works, but the level's pickup handler never starts EM_47, so Plaza_SO3 stays open (handler gate not readable without a bytecode parser; next is a host-pickup run plus a broadcast row). Card 220: _0 and _7 are disabled by the game itself (Enabled?=0, placeholder text); the real gap is _9, whose client widget stays Collapsed; c448 start points moved outside the trigger boxes. Card 281: the made-round path was not exercised (the discarded shots were misses). Card 359: the residue is 1 to 3 uu; 6 corpses are 150 to 480 uu off, from the component offset, not the freeze. Card 448 gate holds. Open cards: 31.
- 2026-11-17 15:34 b190 `d451b08388a464db` (lane 2): three c423 runs; two were lost to launch settings (no C2M1 travel; GOD=0 without FAST). c423b190c ran 417 s with 2 hard FAILs, both in the card 281 block: the spawn took groups from a wait with none (scenario fixed, lines 740-742), and the digest wait FAILed by the runner's 1 s rule after a failed group. 0 EXCEPTION/FATAL. Card 359: the client's bone 0 stays 100 to 320 uu off its own body 0 for 2 to 5 corpses per run, while the host is at 0.0 uu: a visible placement bug, fix in b191. c448 and c245 were not run; I granted 3 more runs on the same dll. Lane 1 stays b189 until b190 passes the c448 gate. Open cards: 31.
- 2026-11-17 15:57 b190 `d451b08388a464db` passed the c448 gate (c448b190a, 219 s, 7 rounds 0 mismatches, 0 EXCEPTION/FATAL) and is copied to lane 1 (hash verified). c423b190d passed (432 s). c245b190b stopped at Plaza_SO3: StartEncounter spawned 2 hostile BP_AI_ControllerTrooper_C and the test clear spares every class containing "Trooper"; b191 clears live ControllerTroopers (test runner only). Cards 220 (the client's live prompt widget is Visible with the _9 text; the Collapsed object was the class template) and 281 (2 hit claims served by made rounds) closed. Card 359 fix is in b191 `be7204b0fef4a7b1` (AddWorldOffset puts bone 0 back on body 0 at the freeze), not run yet. Open cards: 29.
### b191 lane 1 (2026-11-17 16:16)

Lane 1 = b191 `be7204b0fef4a7b1` (copied from `build-b5/dwmapi-b191-be7204b0fef4a7b1.dll`, hash verified). c448 gate passed, no crash. Card 359 closed (c359join 93 guids, 0 over 50 uu, max 28.9). Card 245: clear fix proven, SO3/SO4 complete; now blocked at Plaza_SO5 (Plaza_O1 presence C_54), 7 of 18. Open cards: 28. Playtest waits only on hearing cards 302, 304, 314, 321, 451.

### b192 lane 1 (2026-11-17 16:37)

Lane 1 = b192 `ebf720195a29235d`, hash verified. c448b192a gate passed: 0 EXCEPTION/FATAL, WARN 2/2. Card 310 closed (chargeable both directions, join replay, sent equals applied). Card 431 merged into 151. Card 245: Plaza_O1 completes on C_54; now blocked at Bridge_SO2 (client satchel pickup; host-pickup A/B next). Card 209 three-instance 16G run approved by the user for that one run; it waits until 16G is free. Open cards: 26.

### b193 lane 1 (2026-11-17 17:18)

Lane 1 = b193 `870c5d84c1a9fec4` (archived by the senior to `build-b5/dwmapi-b193-870c5d84c1a9fec4.dll`, as the junior had not kept it; hash verified). c448b193a gate passed: 0 EXCEPTION/FATAL, WARN 2/2. Card 245: co-op satchel proof held. The client took 2 satchels and the host took 2, Satchels counted 1..4, and SO2 replayed on the client 11 ms after the 4th pickup. Each player's own placements completed (SO3a-d, SO3 count 4). Blocked at Bridge_SO4 by a test-side c284 stand bug: the nearest stand put the client behind the BP_Switch_C_3 panel. b194 ranks the face-side stand first. Open cards: 26.

### b194 c245 result (2026-11-17 17:35)

b194 `a119f90574be9a2d` (lane 2; lane 1 waits on its c448 gate). The c284 face-side stand fix worked: Bridge_SO4 and Bridge_O1 completed, 17 objective ids done. Card 479 closed on its guard (0 rows, 0 client crash in every run past the Bridge). C4M2 now stalls in Sewers on egg kills (A 6/17, B 13/34, C 16/30): 25 looks had no line of sight, and 22 client rounds did no damage. Those 22 are carded as 480. Per the clear-bugs-first rule, the scenario kills eggs by code. Open cards: 26 (479 closed, 480 opened).

### b194 lane 1 (2026-11-17 18:04)

Lane 1 = b194 `a119f90574be9a2d`, hash verified. c448b194a gate passed: 0 EXCEPTION/FATAL, WARN 2/2. c245b194b: 25 objective completions. Sewers is done by code kills (56 InstaKillTarget kills, area A 13, B 22, C 22). BrainBug_SO2_R2 never completes: the encounter end of BP_EM_1FactionSetWaves_C_6 did not come within 80 s with both players at the marker. The run 3 clears were void (clear radius over the 20000 cap); the generator is fixed. Card 480 closed as a test fact. Open cards: 25.

### b195 lane 1 (2026-11-17 18:21)

Lane 1 = b195 `51d165a755867924`, hash verified. c448b195a gate passed: 0 EXCEPTION/FATAL. C4M2 completes whole in co-op (c245b194d, 32 completions); card 245 closed. Card 149 closed (change log session totals and host-row cause). New card 481: MoveableRoad digest mismatch. Card 328 cause found: the climb is the penetration push from an encroaching set-back spot (adjustment z 199.8). The fix skips the set-back when the spot encroaches. Test item: c245 clears inside wave holds must not void on bugs left. Open cards: 24.

**2026-11-17 18:43 b196 lane 1 (d42815991b07ad70).** c448b196a gate PASS 214 s, 0 EXCEPTION/FATAL both runs. Lane 1 now b196. Closed 328 (encroach skip: 16 skips, 0 set backs) and 481 (ext actors get the placed visit; 0 MoveableRoad mismatches over 22 rounds). Open 22. Leftovers: standing digest mismatch PlayerPawn Pl:8e:wp host MoritaMk1 / client Unarmed rounds 2-7 (likely host pawn as simulated proxy on client, reader or sync gap open, in card 323); C4M2 PlayerPawn presence noise 9 per round (Pl filter on PlayerState in source for b197); c245 line 202 client E look hit StaticMeshActor_3135 not the FlameThrower pickup, line 732 boss look void (aim blocked). Card 209 not run, memory under 16G. Next b197: card 151 cross-op row batching + Pl filter.

**2026-11-17 19:52 b198 lane 1 (4ed937db0ddf506f).** Lane 2 at b202 f4c51d52fc950a22 but only b198 has a c448 gate pass (c448b198a, 222 s, rc 0/0, FATAL/ERROR 0); b202 adds test-command code only. Closed 151 (batching: 442 B/s against 467, seq gap 0). New 482 (client satchel E null interactor at load, play blocker, b200). Pl:wp digest mismatch is a reader gap (card 323). gen_c245: Plaza_SO3 waits for the StartEncounter row then clears (2 hostile ControllerTroopers); clear treats ControllerTrooper as target and counts left only at health >= 1.0; boss-loop `objective now` dropped; boss look still voids intermittently (c245b202a round 05). Test fact: InstaKill left 2 ChariotBug (75 hp), 1 HopperBug (250), 1 WarriorBug (400) alive inside a hold (b199, b202), test tool only. Digest Pose 1-2 mismatches in 3 of 22 rounds. Open 22.

**2026-11-17 20:15 b204 lane 1 (c31434fff95f7a37).** c448b204a gate pass (223 s, rc 0), c245b204a C4M2 all objective waits passed, digest 0 mismatches every round, boss look rescued by retry at the 900 uu stand, EXC/FATAL 0 all 3 runs. b203: Pl:wp reads the first visible AADWeapon component in the attach tree; does not fix C1M1: on the client the host pawn's tree holds only BP_Weapon_Unarmed_C, and own pawns hold Unarmed and MoritaMk1 both bVisible 1, so bVisible does not mark the drawn weapon. Next: log mesh asset and owner per tree component, key Pl:wp on the drawn mesh; whether host GiveMorita reaches the client copy at all is open (possible real sync gap, card 323). 482: collision race and crouch premises refuted (same load order b200/b202; crouched E works); b200 null target and an unsent IA_Crouch press unexplained; stays open for a repro. 216 and 217 need BUG runs: wait on the bug campaign (trooper first). Open 22.

**2026-11-17 20:57 b205 lane 1 (dd645e276480a2f4).** c448b205a gate pass (229 s, rc 0/0, FATAL/ERROR 0). Real sync gap fixed: the client copy of a peer's pawn drew only Unarmed (host GiveMorita reached no client actor); new ChanOp PeerWeapons (card 050) sends each slot's weapon list on change, new pawn and first Pong; client applies to the copy, never its own pawn. Client 8e Unarmed -> MoritaMk1, digest keyed on drawn mesh, Pl:wp WARNs gone, shot shows the rifle. Cosmetic: c50_peer_arm_map client totals row labelled "host". Card 244 C4M1 (gen_c244.py): 5 of 22 ids in 3 runs; Park_SO2_A..E completed by real fire and replayed; stuck at Park_SO1 satchel buildings (no kind row: satchel action for kind other now in gen, not run); Park_Marker left the doable list with no card049 sent row (open). New 483 (client card137 play-time collision WARNs 135, caller exe+0x3B3D1AE), 484 (gib not at rest 30 s, 21 rows). Open 24.

**2026-11-17 22:49 b206 lane 1 (a287f0f8996280df).** c448b206a gate pass (217 s, drive rc 0), 0 EXCEPTION/FATAL all 6 logs. c50 client label fixed. Closed 484 (rested-state fix; 2 true sliding rows left). 483 narrowed, stays open: client pickups 0->1 by local OnUpdatePickupStatus ~32 s after Pong, host reverts 14.5 s later (real difference), plus exe+0x37C2C74 rows. Card 244: Park_Marker is marker-only, hidden by SetObjectiveVisibility, replayed (no gap); satchel site needs a carried satchel: gen_c244 now takes the nearest BP_SatchelItemPickup_C first, not run. Unexplained: card456 "hold interrupted" 40 rows per side on SatchelSiteCAC with no E press (caller exe+0x4A202EA), in card 244. New 485 (HopperBug corpse pose 166-323 uu off, 2 with no client ragdoll start). Open 24.

**2026-11-18 01:23 b207 lane 1 (3a4e79683266b40d).** c448b207a gate pass (223 s, rc 0, client WARN 4 against 174 in b205a). Closed 483 (pickup collider follows bEnabled only). 485 bodies fixed (freeze repose on stale bones of unrendered copies; c485_freeze_repose reapplies host bodies; 0 pose mismatches in 255 C4M1 rounds); stays open for the rendered mesh shot pair. Card 244 satchel pickup works; site gate was our focus= prefix (child actor name SatchelSiteCAC_...), gen fixed, not run. Still 5 of 22. Open 23.

**2026-11-18 03:34 b208 lane 1 (bbefc4ad2982ab49).** c448b208a gate pass (228 s), c244b208a/b 3539 s and 3722 s, every digest round 0 mismatches, 0 EXCEPTION/FATAL. Card 244: satchel plants work (Flakbug C_0..2), 9 of 22 ids (Park_Marker, Park_SO1_A..C, Park_SO2_A..E); stall at LZ_SO1 (BP_SatchelBuilding_Rubble_C_2 at (23446, -85013)): only live pickup BP_SatchelItemPickup_C_1 is 15.7 km away, gotos fail at the navmesh path end 37 of 37. Card 485 shots inconclusive (wrong corpse picked, row cap 40); next: one client row per reposed guid when first rendered. 252/263 need fight runs on C2M1/C3M2 (no plain static cause). Open 23.

**2026-11-18 04:03 b209 lane 1 (7158152216d78105).** c448b209a gate pass (226 s, rc 0/0). Card 244 LZ_SO1: no script spawns satchels; placed pickups only, nearest to the LZ is C_1 at (27234,-99508), ~15 km off the navmesh path; Park blocks use each satchel they take, so the client reaches the LZ with 0. Site CAT_4 is off the navmesh; front warp gives focus. Next: warp to the nearest live pickup when goto fails (warp gets a `live` filter). Card 485: 0 render rows in the soak (reposed copies never on the client's screen); next a targeted kill-at-crosshair scenario. Still 9 of 22 on C4M1. Open 23.

**2026-11-18 04:27 b210 lane 1 (414fa857c0212604).** c448b210a gate pass (223 s, rc 0/0). Closed 485 (bodies proven; rendered-mesh blend unproven after 2 failed methods, reopen on any shot). Card 244: the pickup warp lands ~1 km below C_1 (b16_landing projects onto navmesh with kC64ProjectHalfZ 1200 and the pickup platform has none); fix (a) in b211: b16_landing refuses a navmesh point over 300 uu below the actor and falls back to the old landing. Open 22.

**2026-11-18 04:51 b211 lane 1 (24df8971ead8486b).** c448b211b gate pass (240 s, rc 0/0, mismatches 0). Card 244: warp landing fix proven (old landing when the navmesh point is over 300 uu below and a WorldStatic surface lies under it); LZ_SO1 completes; 13 native completes. New stall LZ_SO2: closes only through presence C_257 (location unknown); `objective now` picks the nearest presence C_3, which only triggers AssassinSpline_C_0. Card 301 sweep: C4M2 gap G (@20514-20656, BrainBug start tests IsOverlappingActor(GetPlayerCharacter(0)) after SetTriggerEventsEnabled; a client inside alone may stall) is real, fix in b212; C1M1 tutorial bomb site not plain; C4M1/C5M2 have no script dumps. Card 153 implemented, needs a real router test: moves to the playtest wait list. Not carded: card456 hold state stale on SatchelSiteCAC (rows from pawns 20k uu away), log only. Open 22.

**2026-11-18 05:35 b212 lane 1 (a8562ff63c709122).** c448b212a gate pass (231 s, rc 0/0). Card 301 gap G fixed by class: SetTriggerEventsEnabled (0x05103780) only stores +0x30A, so a pawn already inside never enters; detour_c301_exec_enable (exec thunk 0x0508B980, host) sends each other living pawn already overlapping through detour_c245_overlap; c245b212a client alone inside C_176: sent 1, SO1 completed at once. Open from that run: BrainBug_SO4 took 165 s (b204 17 s, 259 hits against 162), cause unknown, must be cleared before C4M2 counts; PlayerPawn digest false positive on net cull (8 rounds), Score:1 once. CLIENT CRASH c244b212a 1000 s: card 486, AV at ubwcoop+0x152C73 in object_present via c269_swap_back (card 269/479 path, pre-existing), animator+0x128 held 0x31F00000001. C1M1 bomb site (IsPointInBox player 0 then ApplyDamageAtLocation) not plain. C4M1 presence fallback fired on a satchel step (Park_SO1_A), LZ_SO1 not reached. Open 23.

**2026-11-18 06:22 b213 lane 1 (57aacb585ebdd2f1).** c448b213a gate pass (rc 0, 0 mismatches). 0 EXCEPTION/FATAL, 0 dumps in 3 runs. Card 486: c269 reads the animator fresh from npc+0xA30 (no cache); object_listed(animator) before reads, object_item rejects unaligned and non-user-space pointers; refuse row never fired, so stays open for an animator class check (a reused slot can be a listed object of another class). BrainBug_SO4 slowdown was scenario placement (clear radius from the SO2 marker), not b212: SO4 to SO5 11.1 s with clear 20000. Presence fallback only for bare-location markers. C4M1 17 of 22+ completes (LZ_SO1, LZ_O1, Bridge SO2-SO4/O1, Stadium_SO1_C/D new); stuck at Stadium_SO1_B (client look blocked by Actor_39 at 2761 uu). PlayerPawn culled pawns left out of the digest: 0 PlayerPawn mismatches. New 487: 7 destructible benches/bins in FED_C4M1_Art dead on host, alive on client (only H rows, no K row; lead: c98 journal keyed by name without level). Open 24.

- 2026-11-18 07:46 b214 (56b7f47d0bbc4783): gate c448b214a passed (rc 0/0, 0 mismatches, WARN baseline). Lane 1 is now b214. Closed 486 (animator IsA guard) and 487 (Blueprint deaths with no Health change now relay K; 0 digest mismatches in 68 and 82 rounds). Card 244: Stadium_SO1_B completes through the behind-warp fallback (21 and 20 unique completes); Stadium_SO1_A is still stuck (look voids on Actor_2/Actor_39). Test speed-up batch prepped for b215: stuck cap per block, warp-then-goto satchel walks, BrainBug code kill after 1 fire round, c448 sleeps 22 s to 9.5 s, negative probes timeout=3. Before times: c244 1557/1947/2364 s, c245 446 s, c448 239 s.
- 2026-11-18 08:48 b217 (5fa127ad28ff0c7b): gate c448b217a passed (167 s, rc 0/0, 0 mismatches, WARN 2/5). Lane 1 is now b217. Speed-up batch landed: c448 239 to 167 s, c244 1557 to 1331 s, c245 446 to 289 s (boss stages SO3..SO6 now complete through the 'kill actor <name> <percent>' verb). New cards 488 (audio mixer crash at quit), 489 (BrainBug_SO2_R2 marker step), 490 (infinite stamina setting; host sets it for both players; static check says stamina gates only sprint, run-time check pending).
- 2026-11-18 09:16 b218 (6b2ec154389371e7): c244b218a 1478 s, 0 mismatches in 6 digests, 26 unique completes, Stadium_SO1_A completes; mission end still not reached. Card 490 step 1 confirms stamina affects only sprint (speed 1312 to 1208 by stamina, sprint gate) and jump cost (5); server copies drain too. Option build approved for the next batch. No gate run on b218, so lane 1 stays b217.
- 2026-11-18 09:29 b219 (3a050e4f1afc39b7): gate c448b219a passed (131.7 s, rc 0/0, 0 mismatches in 10 rounds). Lane 1 is now b219. Carries card 490 (host stamina hold, lobby [I] option, `host stamina on|off`, ChanOp::Stamina join replay; off path proven in the gate), the 489 test-side fix in gen_c245, and 482 coverage in c245. The on-path proof for 490 runs in c244b219a.
- 2026-11-18 09:58 c244b219a (b219): card 490 on path proven (Stamina 100 on all three pawns, MaxWalkSpeed 1312, jumps free); closed with a reopen condition on corrections (the 16-55 per second burst was pawn contact on a shared goto path). Soak: 20 unique completes, 1637 s, 17 voids, 7 Score:1 rounds with 'no row sent' (old, noted on card 323). Open 24.
### 2026-11-18 10:05 c245b219a: cards 482 and 489 closed

c245b219a (b219) passed in 313 s with 0 voids. Card 489: all 4 BrainBug_SO2 subs, SO2 and BrainBug_O1 complete. Card 482: all 4 client satchel pickups and sites done, 0 null interactor rows. Score: 1 of 17 digest rounds mismatched (round 15, host 343500, client 358500 set down after its snapshot), the card 323 settle class; b220 carries the fix. User latency target for visible sync: under 100 ms feels instant, 100 to 300 ms is perceptible (Nielsen). Open count 22.

### 2026-11-18 10:59 b222 score settle, c244 skip ledger and log scan
b222 `de3cee44d5041dcc`: card 323 score settle cut from 1500 ms to 80 ms from measured lag (max 35 ms); raises apply at once (17 to 19 ms). c245b222a passed, 18 rounds mismatches 0. New card 491 (+15000 client over-count at BrainBug_SO2). Card 490 stays closed with a rewritten reopen condition (c244b220a 1.7 corrections per second, |e| at most 3.4 uu). drive.py: log reads incremental (`log_lines`, `log_scan`), about 0.2 s saved per step. c244 skip ledger (user rule): tools/scenarios/c244-passed.txt, 9 chain-free ids; chain parents stay played because `objective complete` skips the level script's add. c245 has no ledger (chained, under 300 s). Lane 1 waits for c448b222a. Open count 23.

### 2026-11-18 11:02 lane 1 is b222 `de3cee44d5041dcc`
c448b222a passed (169 s, rc 0/0, drive rc 0, 10 digest rounds mismatches 0, card323 WARN 0, no dumps). game/ dwmapi.dll copied from build-b5/dwmapi-b222-de3cee44d5041dcc.dll, hash checked. Carries the card 323 80 ms score settle and the card 490 stamina option. c244b222a running with the 9-id skip ledger.

### 2026-11-18 11:23 c244b222b: skip ledger breaks level script chains
c244b222b (b222, 9-id ledger) VOID: drive 1178 s (b220 1840 s, the log scan saving), mismatches 0 in 41 rounds, 38 voids (8 nest looks on Landscape, 30 use looks blocked by Actor_2). `objective complete` broke chains twice: Park_SO2_A and Park_Marker completed their parents, and Stadium_SO1_B/C/D by command never gave the Add of Stadium_SO2, so Stadium_SO1_A stuck to the end. Ledger cut to 4 ids (Bridge_SO4, Park_SO1_C, Park_SO2_B, Park_SO2_D). c244b222c is the last try: any parent row or missing Add empties the ledger for good. The two void classes are batched into that run.

### 2026-11-18 11:49 c244b222d: skip ledger dropped
c244b222d (b222) VOID with 4 voids, drive 1138 s, mismatches 0, WARN card284 0. Use-look fallback fixed the Actor_2 class (30 voids to 0). The 4 nest voids are looks at a nest already killed after the go; gen_c244 now gates the nest steps on that (2334 steps). Ledger failed a third time: with Park_SO2_B, Park_SO2_D and Park_SO1_C command-completed, the script never completed Park_Marker. c244-passed.txt is empty; `objective complete` is not used as a skip. A future skip must fire the real trigger. Batch run count is over the 3-run budget; c244b222e (no ledger) is the last of this batch.

### 2026-11-18 12:06 c244b222e: batch end
c244b222e (b222, no ledger) VOID: 1 void, drive 970 s, mismatches 0, EXC 0. Nest and use looks 0 voids. Left: line 471 satchel site look runs before its fallback (reorder), and Park_SO1_A stuck 42 blocks because the client's E found `interactor target null` with the aim on StaticMeshActor_6688 (eye about 380 uu from the site's bounds centre). Next build: gate the satchel E on the interactor target row, retry from the bounds centre (cap 2). A null target within 150 uu facing the site gets carded as a mod bug. Batch: 488, 491 (C99_LOG in c245), the two c244 fixes.

- 2026-11-18 12:29 senior: c245b223a line 659 was a test race. The Brain Bug spawn row came 2.9 s after SO2 (0.48 s in c245b222a), and the boss step ran at 0.9 s. gen_c245.py now waits for `card098: host spawn BP_AI_BrainBug_C` before the boss rounds. Card 491 static trace: the ObjectiveComplete score is paid in USTLevelObjectiveSubsystem::ObjectiveComplete (0x051613C0) when the objective is a sub-objective and +0x6A InList is set. Both sides broadcast the same 5 completes at SO2, but the client gains +15000 more; next build logs the score delta for each complete. Card 314 predictions hold in b223 (0 failed rows; remote fire plays with ATT_WPN_MoritaMK1); only the hearing check is left, which waits on the playtest.

- 2026-11-18 12:34 senior: card 321 narrowed. No Trooper map has streaming volumes, teleporters or doors. Hazard crush (361), fall zone (338) and CanTakeoff (449) were already fixed. Left: the threat level (music only; a combat-level change row is ordered for b224) and the ally nav filter. Card 330 b223 read: mod cost 0.6 to 2.6% of frame time, the boss fight included. Steps over 5 ms in play: c43 15.3 ms, c315 10.5 ms, client c37 9.4 ms. A per-call slow-step row is ordered for b224.

- **c244b223a stopped at Stadium_SO1_D (2026-11-18 12:38, senior).** Every block from scenario line 1252 to the end voided the PanelSwitch use look. The 350 behind warp landed 913 uu below the switch, and on the 350 front warp the host pawn on the stand blocked the aim at 103 uu. tools/gen_c244.py now tries behind first, then moves the host 350 behind and warps the client front before the gated look. Line 1246 (Bridge_Marker, expect void "complete") was a race with the level script; for b224, `objective expect` should log an already complete step and not void it.
- **Lane 1 on b224 `5ffd328c1386fa97` (2026-11-18 13:09).** Latest build with a c448 pass (c448b224a, c245b224a PASS). b225 `b75b744e33c41187` changes only the card 491 row (c245b225a PASS, no c448 run) and goes to lane 1 after its c448 pass.
- **c244 Stadium_SO1_D root cause (2026-11-18 13:09, senior).** Not the stadium script: c244b220a focused BP_PanelSwitch_WithoutScreen_C_3 from the same stand with the origin look (pitch -3.7, hit z -1104) and completed Stadium_SO1_A..D. The b223 bounds look aims at the bounds centre (pitch -26.6, hit z -1149): the line is clear, but focus is none. gen_c244 now puts the origin look first at each spot, with the bounds look as its fallback, and every later step runs only while focus is none. In b225 the card 491 row reads real scores. The bug itself is still open: at BrainBug_SO2 the host sends the nested parent complete before the child, the client cascades and scores +25000 against the host's +10000. The fix (child rows sent before the parent) goes into b226; see the card. Card 321: the client threat level stays 0 because the game mode exists only on the host; the fix is to send the host level. Card 330 b224: client cine_skip_step takes 394-399 ms (3 times), the largest spike.
- 2026-11-18 13:47 (senior): lane 1 is on b226 `e8b11c7f37a0e4db` (c448b226a PASS 131 s, c245b226a PASS), copied and hash checked. b227 `03c66555ce59fd68`: c245b227a VOID at line 660, the client's Brain Bug copy arrived 428 ms after the host spawn row and the warp ran first; tools/gen_c245.py now waits for the client's `card098: client collision BP_AI_BrainBug_C BP_AI_BrainBug_C ` row (the load-time Default__ rows do not match). b228 `11b7efb307ea9b4e`: c245b228a PASS 284 s; card 321 client music modes follow host level changes in 33-66 ms (closes after c244 on b228 shows no regression). Card 491 cause moved to a BP_PlayerCharacterMI ResupplyUpgraded award on the client (card note).

- 2026-11-18 14:07 (senior): c244b228a VOID, the run looped on Stadium_SO1_D from line 1188 and was ended at 1129 s. EXC/FATAL 0/0, STUCK 0, mismatches 0. Cause: the only DLL focus sweep (testrun.cpp, which runs only on a look with focus=) ran after the 350 front warp, 340 uu from the switch, past the 250 uu range. The stand bounds look aimed at pitch -26 with no sweep. Fix, in drive only: the `sweep` word (`look actor X 0 bounds sweep`) sends focus= with no void gate, and gen_c244 now uses it at the stand. Line 219 VOID: a WarriorBug spawned during the host nest steps and blocked the client's nest look; gen_c244 adds a second `host clear 1500 kill` before each client nest look (2568 steps). Card 321 closed (WARN321 0, 17 client combat level transitions). Card 330: the client cine_skip_step does not appear in the top 5; the client peak tick is 23.19 ms (c114_step 22.51, one spike), the mod is 1.29% of frame time. c448 on b228 is running.

- 2026-11-18 14:12 (senior): c448b228a PASS 141.5 s (rc 0, EXC/FATAL 0/0, mismatches 0; the same 5 TRY-FAILs and 2 client WARN card137 rows as b224a/b226a). Lane 1 moved to b228 `11b7efb307ea9b4e`, hash checked. b229 `3c4884176e618fde` adds the card 491 per-pawn rows only (`card491: <side> ... pawn ... own|proxy ..., difficulty ...`, USTDifficulty::GetCurrentDifficultyLevel 0x03775250); c245b229a runs. b230 batch sent: 323 Pl:wp reader on the attached weapon, 451 FADActiveBark client broadcast, 330 per-step over 5 ms row, 491 fix if the b229 rows settle it.

- 2026-11-18 14:33 (senior): b230 `3b8f2b0533a4ab95` VOID (host AV, the FADActiveBark Speaker read as a raw pointer; it is a TWeakObjectPtr). b231 `ba9d7b915f00ef61`: card 451 bark subtitles relayed (bark_c451.cpp, ChanOp::Bark; c448b231a PASS 278 s, host row to client row 20 ms, client shot shows "Non-Combat Trooper: I'm doing my part."; one bark on the route). Senior review attacked the %160s width (kC50PathMax 160, buffer 161, OK), an unknown guid or class on the client (dropped with WARN), a null speaker, and start/finish symmetry: no defect found. Lane 1 moved to b231, hash checked. Card 323 Pl:wp was already fixed in b205. Card 330: c114 22.5 ms is a load frame; host c43_step 13.87 ms is c45_respawn_check inside the +90 s card049 DIAG dumps (logging cost). c245b229a PASS 278.6 s. Card 491: see the card's c245b229a section.

- 2026-11-18 14:38 (senior): card 329 closed (byte audit done, no fix; reopen on a peak over half the limit or a one-op mix). Open count 19.

- 2026-11-18 14:39 (senior): c244b231a stopped by the junior at line about 464 (0 crash dumps). Two user finds. (1) The nest grenade aim is wrong: the steps aim at the actor origin z+200, so the grenade hits the mound side, and the nest dies only to a blast in its hole (card 241). This is the cause of every Park_SO2_* `objective wait` TRY-FAIL in b225, b228 and b231. Fix: the look target becomes the USTHitVolumeGrenadeHoleComponent world location (b232). (2) The client radar shows fewer hostiles than the host's: card 319 reopened, b232 adds an uncapped shot diag. Open count 20.

- 2026-11-18 14:43 senior: card 301 FED_C3M2 first pass written to docs/reference/objective-coop-audit.md (14 rows). New gap class H (closes behind): presence C_52 runs Setup Objective 3_02, which closes Gigantic doors C_8/C_7/C_10 (@9296-9368) for one pawn; Switch C_3 (@5595) shuts Gigantic C_2 and Medium C_1/C_2. Neither is under c227 or kC300Closures. Evac (class E): one player's entry request on transport C_2 ends the level after 21.5 s. Fix candidates listed in the doc; batch into the next class-A/H build. Open count 20.
- 2026-11-18 14:45 senior: card 301 FED_C4M2 first pass in docs/reference/objective-coop-audit.md (9 rows). Class H: presence C_73 (@10933 -> @9746) closes Plaza doors Medium C_1-C_4 for one pawn; Gigantic C_2 closes @5339 at Plaza_SO2a. Class G: BrainBug start tests player 0 against C_176 (@20547). Class F mild: Toggle Player Control restores player 0 only (@32943). Class H fix candidates across C3M2/C4M2: kC300Closures rows for C3M2 C_52 and C4M2 C_73. Open count 20.
- 2026-11-18 14:45 senior: card 301 FED_C5M1 sweep in docs/reference/objective-coop-audit.md (5 rows). Class H: Switch C_5 (@29072 -> @28419) closes Gigantic C_17/C_15/C_16. Class D check: TriggerBox_1 (@18894) moves and kills any trooper-tagged OtherActor. Class H total now 5 sites (C3M2 C_52, C3M2 Switch C_3, C4M2 C_73, C4M2 Gigantic C_2, C5M1 Switch C_5). Open count 20.
- 2026-11-18 14:46 senior: card 492 written (class H closes-behind, 5 sites). Batch the two kC300Closures rows into the build after b232. Open count 21.
Card 301 FED_C4M1 pass (2026-11-18 14:49, senior, static, `kis.py` dump 0 errors): 6 rows in docs/reference/objective-coop-audit.md. Class H at presences C_17 (metro doors) and C_255 (bridge door), and at the Stadium_SO2 objective event (3 doors close on the last kill). Class E at presence C_70 (EndLevel at once). Added to card 492 as fixes 4 and 5. Fix 4 needs a per-site group key in kC300Closures first: one group per map would let a player in C_17 release C_255. No player-0 reads or actor moves on C4M1. Open cards: 21.

Builds b232 and b233 (2026-11-18 14:49, junior report, senior read). b232 `744ca50da98ffb9b`, run c244b232a: 0 EXCEPTION/FATAL, stopped at the nest block because every hole look voided (the "GrenadeHole" class match found 0 of 22 nest components). Card 319 prediction falsified at the shots taken: both sides scan range 2500, host and client gathered and marked counts match (one shot differs by one warrior because the players stand at different spots); the radar gap did not reproduce, and the rows stay on for later shots. b233 `bcc073f92d883349`: hole match by class "GrenadeHole" or object name "GrenadeDamageVolume", plus kC300Closures rows C3M2 C_52 and C4M2 C_73 (one site per map, so the card 492 group-key change is not needed for these two). Run c244b233a is live. Lane 1 stays b231 until a build passes c448.

Card 301 FED_C5M2 pass (2026-11-18 14:50, senior, static): a full extract (7145313 bytes; the old one was truncated) decoded with 0 errors; 6 rows in docs/reference/objective-coop-audit.md. Class H: `BP_PanelSwitch_WithoutScreen_C_1` closes six Large doors (added to card 492 with the switch sites). Class G: the `Cave in` hazard blasts at player 0's location every 30 s, new card 493. Class E: transport C_2 PlayerEntering and PlayerSkippedFlight start the level end on one player. Satchel pickup moves are a class C check. All seven Trooper maps now have a first pass. Open cards: 22.

- 2026-11-18 15:04 senior: card 492 fix 2 traced. Switch closes run in the same call as SwitchPressedDelegate, so the fix sets
  the card 288 trigger (`c288_presence_fired(presser)`) on host switch presses (c102_interact, detour_c133_completed,
  c133_host_done). Card 288 already holds the presence-site doors (synchronous closes); 0 card288 rows in the C4M1/C4M2
  soaks are expected (players together). Sent to the junior for the next build.
- 2026-11-18 15:20 senior: c244b234a review. Host and client rc 0, 0 mismatches, 0 FATAL. Line 295/361 voids: the nest died before the look, fixed in b235. STUCK at line 1149: the CAT_2 satchel holds were interrupted by live bugs (aphid fire knocked the client back, a jumper bug came within 286 uu). The 'other' satchel block has no clear before its hold. Proposed to the junior: `host clear 1500 kill` before each `client key E 3200`. This is a scenario miss, not a mod bug. Junior also told the card 493 trace and card 492 fix 2.
**2026-11-18 15:30 card 247 step 1 (senior, static).** C5M2 play-order table for all 29 ids written into the card ("Step 1 result"): three opening objectives in any order, finale count at 3 opens Assassin_O1, Assassin_O1 end adds Finale. Finale_SO5 has no add in the script (only a complete @31007 from presence C_62): candidate to leave M after an objective-table check. Outpost_SO5 and Finale_SO7 close only through their parent's CompleteObjective. New, log only: `atWhiskey` (presence C_402) is cleared by any exit, so one player leaving stalls Outpost_SO4 until a re-entry. Next: tools/gen_c247.py from gen_c243.py. Open 22.

- 2026-11-18 15:32 card 247 step 2 (senior): `tools/gen_c247.py` ready (gen_c244 blocks, map FED_C5M2, end on Finale_O1 complete; 2485 steps, parse 0 bad). Junior runs it after c244b235b with the current build hash. Open 22.

- 2026-11-18 16:18 b237 `c92af11d2ae5bfac` (junior): 0 warnings, kept and on lane 2. c244b237a: rc 0, 0/0 EXCEPTION/FATAL, 0 STUCK. Card 494 closed (senior review): guard fired 2 times, weapon back on the arms; root cause not determined. Satchel: 4 client HoldDone plants. c244b236a's 488 stuck skips were scenario selection (Bridge_Marker picked after Stadium moved on), not a play bug. The junior edited objective_c284.cpp during c244b236a (rule break, no build then). c247b237a is running (card 493 proof); c245 next. Lane 1 stays on b231 until b237 passes c448. Open 22.

- 2026-11-18 16:24 user: "we should be looking at actual bugs" (third time; memory fix-bugs-first). From b238 each build is a batch of real open bug cards, and soaks only carry their proofs. **b238 batch:**
  1. Card 492: the door fix as specified in the card (kC300Closures rows with the per-site group key; the switch press calls `c288_presence_fired`).
  2. Card 319: guid diag (the c244-04-go client blips are 2, the host's about 5; the totals per run are even).
  3. Card 491: the next resupply diag in the card.
  No scenario or aim work in b238. Open 22.

- 2026-11-18 16:25 c247b237a VOID (0 EXCEPTION/FATAL, killed at 323 s): c126_pick took the last marker-on step, Research_SO3_C (a satchel site, added hidden, no focus until satchel carried), and `objective expect` kept it sticky for 6 blocks. Same class as the c244b236a Bridge_Marker 488 skips. Card 493 is not proven yet. Approved for b238 as proof plumbing only (no build of its own): a host verb `objective next` (next doable step in sparse order, wrapping; log row `cmd: N objective next: A -> B`) and the generators call `try host objective next` after a failed `objective wait`; go_n per step.

- 2026-11-18 16:33: **Lane 1 moves to b237 `c92af11d2ae5bfac`** (copied and verified).
  - c448b237a passed: drive rc 0, FATAL/ERROR 0, reconnects 6454 ms and 6726 ms. The rc 1 on both sides comes from the
    junior's kill after the last step.
  - WARN rows: host 2, client 3, all known kinds (DisableThreadLibraryCalls err=126, GOD off, a card137
    BlockingVolume collision 0->3 on the client).
  - b238 `02a6719c37e25ebb` is built and kept. It holds:
    - `objective next`: skips steps that are not doable, and skips a satchel site when CanInteract is false for every
      player;
    - the card 319 per-shot guid rows;
    - the card 491 threshold elements and pawn rows around every award;
    - the card 492 door fix.
  - c247b238a is running with UBWCOOP_C99_LOG=1 on both sides.
  - Junior predictions:
    - the PanelSwitch C_1 card492 row in block 03-05;
    - the card 493 cave-in in blocks 20-30, but only if the client completes the Research_SO3_C satchel (Research_O1
      gates the finale).

- 2026-11-18 16:48: b238 runs, b239 runs and the b240 fix.
  - **c247b238a void.** The `objective next` when-regex held a literal `{g1}`, which drive.py does not substitute. The
    generators now match `\w+`.
  - **b239 `96af74d1940dc977`: card 492 fix 2 was dead in b238.** The lookup used the type name
    SwitchPressedDelegate. The property is OnPressSuccessful.
  - **c247b239a.** objective next rotates. SO3_A, SO1_B, SO1_C and SO2 completed. card492 rows print on the SO1_B and
    SO1_C presses. card493 printed 0 rows (the cave-in was not reached). card491 printed 0 rows (0 awards).
  - **c247b239a host crash** at 361 s: EXCEPTION_ACCESS_VIOLATION reading 0x12 in
    ASTSatchelPlacementSite::CanInteract +0x11b.
    - Cause: hold.cpp c133_can_interact built FADInteractionFailed's FText at +0x00. The FText is at +0x08, after the
      8-byte gameplay tag.
    - **Lane 1 b237 (and every earlier build) carries this bug.** The host's check of a client HoldDone
      (hold.cpp:335) and vehicles.cpp:2035 call the same helper. The host crashes whenever the site's CanInteract
      takes its failure-copy branch.
    - Example: the client finishes a hold on a satchel site that the other player has already charged.
    - **b240 `e7a83a34d41871c6`** fixes it (kC133FailedText = 0x08). Reviewed: the buffer is 0x40, zeroed, and 0x18
      is used.
  - Lane 1 moves to b240 as soon as a c448 passes on it. This crash class is the reason; it does not wait for other
    cards.
  - Open finding: the client interactor never focuses Research_SO1_A (BP_PanelSwitch_WithoutScreen_C_2). Hypothesis:
    the switch stays disabled until the SO3 sites are done. The SO3 satchel-site looks are voided by
    StaticMeshActor_2112.

- 2026-11-18 17:02: **Lane 1 moves to b240 `e7a83a34d41871c6`** (copied and verified). c448b240a passed in 195 s, 0 EXCEPTION/FATAL.
  This fixes the host crash in the client HoldDone CanInteract path. The CanInteract-false row is not proven yet: it
  needs a scripted second hold on a placed site.
  - c247b240a had no crash and was stopped early.
  - The C_2 switch is enabled: the user's own E press completed SO1_A. The test's key press was skipped because the
    look found no focus.
  - **Test tool, approved:**
    - (a) The use-stand is measured from the bounds face on the yaw side, not from the root.
    - (b) When the sweep misses, the view returns to the best point.
    - The satchel step warps to the `objective now` actor point, not to the nearest site. C_5 was the wrong site.
  - **Test tool, rejected:** (c) a smooth fan to replace the snap looks.
  - **New card 495**, user report: the CTF door opens on the first laser pickup. Open 23.

- 2026-11-18 17:10: b241 `02ee9cd945d0a641`, built with 0 warnings. It holds:
  - the card 495 latency fix (the c50_host_nudge, so pickup weapon rows go out at once);
  - test fixes (a) and (b);
  - the satchel stand warp in the generators;
  - the scripted second hold. BLOCKS goes from 37 to 36 to stay under the step cap.

  Runs, in order:
  1. c448b241a: the gate and the card 495 timing.
  2. c247b241a: the test fixes and the CanInteract-false proof.

- 2026-11-18 17:20: **Lane 1 moves to b242 `2d310c1ec05b87fe`** (copied and verified).
  - c448b242a passed the gate: drive passed, rc 0, 0 EXCEPTION/FATAL, no dumps. Card 495 is not proven: no laser
    pickup happened, because the c448b240a pickup was an accidental host press.
  - **New user rule:** host and client each pick up the CTF laser before the door opens. The card 495 fix direction
    changes to a per-player pickup plus a gate (see card 495; memory each-player-pickup).
  - The junior traces the BP path. c247b242a is running.

- 2026-11-18 17:43: c247b242b: **client crash at 856 s**, in the bark subsystem (ProcessNextBarkIndex, stale class key).
  - The senior trace finds that BP_BarkType_sawbughopper_C unloaded on the client: the card451 `class 0` drop came 73
    s before the crash. The bark map (+0x70) is not a UPROPERTY.
  - **New card 496**, a crash: root each bark class in detour_c451_request_bark. It goes into b243 or b244.
  - Lane 1 b242 carries this crash, and so does every earlier build.
  - Open 24.

- 2026-11-18 18:05: **Lane 1 moves to b245 `c99dbdd0be94ef0b`** (copied and verified).
  - It carries the card 495 per-player pickup and the card 496 bark root.
  - c448b245d passed: rc 0, 186 s, 0 EXCEPTION/FATAL, 11 digests with 0 mismatches.
  - **Card 495 closed.** The laser and the Morita were each held 1 of 2, then released 2 of 2.
  - Card 496 stays open until a long c247 run.
  - c448b244a to c448b245c failed on scenario issues only: the pickup name, the stand clash, the time cap, and the
    whole-log wait.
  - Open 23.

- 2026-11-18 18:26 senior: c247b245a reviewed. Card 496 FAIL: client crash at the same FindIndexByHash site. Cause: in this
  UE 5.5+ build, `FUObjectItem::SetRootFlags` (0x0149F8E0) adds the item to the GC root list, and our bare RootSet
  fetch_or did not, so the rooted bark class was still collected (sawbughopper rooted twice, 685714345 and
  685916886). Fix for b246: `object_add_root` calls SetRootFlags. Card 492 switch row proved (BP_PanelSwitch_WithoutScreen_C_1,
  block 16). The hold rows wait on a player being outside. Card 493 not reached (Finale). New card 497: the client marks
  Research_SO2 and Research_Marker2 complete while the host does not (digest mismatch, seen since b242).

- 2026-11-18 18:28 senior: card 497 traced. The host sends a nested AddSubObjective before its outer CompleteObjective(Research_O1),
  so the client's replay cascade completes the new children. The fix extends the card 491 nested queue to all mirrored
  kinds. It is batched for the build after b246. b246 `dc88717c2305e075` (card 496 SetRootFlags) is in c247b246a.

- 2026-11-18 18:46 senior: card 496 closed into done/ (b246 SetRootFlags root proved in c247b246a). b247 `8028838839b8bdab` (card 497 nested-row order) reviewed clean: every mirrored detour takes a slot only under a complete, visibility no-change path leaves an empty slot that the flush skips and keeps depth balanced, the outermost complete flushes its own row first. Known residue: a complete nested in an Increment still sends before the Increment row (order as before b247). Proof waits on the next C5M2 soak: 0 Research_ mismatches, client card491 score equals host, card497 row present. Lane 1 stays on b245 until c448 passes on b247.

- 2026-11-18 18:52 senior: lane 1 is now b247 `8028838839b8bdab` (c448b247a passed: 200 s, rc 0/0, 0 FATAL/ERROR, the same 6 TRY-FAIL as b245d). The card 493 rerun (cave_c493.cpp) and the card 492 fix 5 site column are already in b240+; only their proofs are open (the 493 proof needs the C5M2 tunnel past BP_PlayerPresence_C_408). Card 492: fix 4 needs no code (the C4M2 Gigantic C_2 close comes from BP_Switch_C_0, covered by the switch trigger). Fix 6 (C4M1 stadium) is specced in the card for b248. Card 488 traced: an engine shutdown race at exit, no mod path; no fix, and a crash with that stack after `quit -> done` is known and does not void a run. User 2026-11-18: fix bugs, not soaks; proofs ride on the batch run.

- 2026-11-18 19:03 senior: card 319 reopened. The user says the bug display is the middle HUD (USTBugProximityAreaWidget), not the minimap. Its NativeOnInitialized binds OnNewMarker/OnRemoveMarker once to the PC's USTMinimapComponent (comp+0xB8, +0x180) and never retries when the PC or component is null. Diag specced in the card for b248 or the next build.

- 2026-11-18 19:50 senior: b248-b250 reviewed (junior). c448 passes on all three; lane 1 is now b250 `da0c7f12c62361a6`. Card 492 fix 6 proven in c492b250a with the b249 change (trigger side from the presence centre, `c288_trigger_at`); Gigantic C_1 not held, carded as 498. Card 319: the proximity widget binds a second PC STMinimapComponent that never gets OriginActor on the client, so its tick returns before broadcasting; fix specced in the card. Card 493: no first caller of `Cave in` in the Scripting package; next run logs every caller, else close as dead.

- 2026-11-18 20:36 senior: b251 `be4b80883eb2b57e` / b252 `3071a1fef0b49ebe` reviewed. Card 319 closed (client middle HUD bugs shown on 28 of 28 shots). Card 493 closed (no caller of `Cave in`). Card 488 closed (engine race, no fix). Card 498's presence test was wrong (C_5 box spans past the doors) and regresses the stadium, so lane 1 stays on b250; group half-plane test next. User approved one 16G scope run for card 209's three-instance proof.

- 2026-11-18 21:03 senior: b253 `168fd79b995e1d9c` reviewed. Closed 498 (group hold, c492b253a), 497 (c497b253a) and 153 (c153b253c). c448 on b253 is owed before lane 1 (lane 1 still b250). Card 209 run c209b253a void (stale scenario opening, fixed); a rerun in 16G needs the user's approval.

- 2026-11-18 22:23 senior: c448b253a passed (0 EXCEPTION/FATAL, all digest rounds 0; the 7th TRY-FAIL is card451 idle bark rolled played 0 six times, game random, also in b245c/b251a). Lane 1 now b253 `168fd79b995e1d9c`. b254 `41c0cdf8f75aa09c` (kC491Parts gets "Resupply" back) c245b254a VOID at the BP_Switch_C_0 use: the b237 step-in (kC284StepGapUu 60) put the client 50 uu off the panel, focus none; card 323 turret and mech rejoin rounds taken (digest 0 after the turret round; mech round 1 PlayerPawn Pl:7d2:wp mismatch, a reader blind spot, because the equipped pointer does not reach the client). Downed-host join needs a third Up player (card 366 all-down), so it rides c209. The user approved one more 16G c209 run. b255 = step-in stop at 3D 200 uu, wp key falls back to the visible weapon mesh, state checker counts a piloted mech as pawn=other; then c245 rerun, c448, c209 16G.

- 2026-11-18 22:42 senior: b255 `bb17337f62f491c1` c245b255a VOID again at the Plaza_SO2a BP_Switch_C_0 use. The senior's 3D-200 step-in rule was wrong (measured from the floor root; the pawn is blocked at 2D 144 anyway). The real cause is aim height: the bounds-centre aim at pitch -38 hits the non-focus base, while a panel-height aim from the same stand (pitch -3.6) gives focus. b256 = the aim scan for focus= looks ranks focus-giving hit points first (class fix), and the step-in goes back to the b254 rule. b255 fixed: the mech pawn=other state wait, and Pl:wp falls back to the visible mesh (round 28 mismatches 0; rounds 1-34 all 0). The c209 downed-host join block was added at the end of c209-three.txt. Order: c448b255a, c209b255a 16G, then b256 and c245b256a for 491/492.

- 2026-11-18 22:46 senior: c448b255a passed (0 EXCEPTION/FATAL, digest 0, drive rc 0). Lane 1 now b255 `bb17337f62f491c1`. Correction to the previous entry: the aim scan already ranks focus first. The Plaza_SO2a defect is b14_points: the eye is inside the box slab on the chosen axis, so the grid lies on the face 13 uu behind the eye. b256: when the eye is inside that slab, the grid lies on the plane through the centre (class fix for every bounds look and c423_near_sample); step-in back to the b254 rule plus a 250 uu reach guard. c209b255a 16G running.

- 2026-11-19 06:15 senior: c209b255a failed on scenario defects (overlapping host/client stand at P2, a stale card320 'clients 2' regex), 0 EXCEPTION/FATAL on 3 logs; P1/P3/P4 ok. The junior audited the whole scenario (51 waits, 8 warps; 3 fixed). The user approved a third 16G run: c209b256a. b256 `374c08bae8ace30d` c245b256a: Plaza_SO2a passes (focus sweep, card492 press, card491 complete on both sides); new stall at Bridge_SO4 BP_Switch_C_3 because the step-in measures from the root. Rejoin digest rounds 21-28 BlockingVolume_14..17 host 0 / client 1: the join replay sends state journals (CollisionOp) before event journals (closure F), so a replayed closure overwrites the final state (card 323 class fix). b257 = step-in from the nearest face (no move within 150 uu) + events-before-state replay order.

- 2026-11-19 06:26 senior: c209b256a failed on the scenario again (line 106 `host clear 1500 45` was a fire clear against refilling WarriorBugs; the host bled out, and the revive regex wanted Downed -> Up while both clients correctly logged Out -> Up). 0 EXCEPTION/FATAL on 3 logs. The clears are now `kill`. The user approved a fourth 16G run, c209b257a, after c245b257a and c448b257a; it is the last one before 209 is split. The lint gets a non-kill clear rule. b257 `6b5faf2a31aafe19` = step-in measured from the face, and the join replay sends world events before world state (card 323).

- 2026-11-19 06:31 senior: c245b257a review. Line 720 was the scenario order (junior moved the warp before the wait, c245-c4m2 regenerated). New card 499: the client completes BrainBug_O1 early because card 497 (b253) sends Complete SO2 before the nested Add SO3; the fix filters client native completes by the host's completion set. Card 491 still reproduces (client +25000 vs host +10000 at SO2, part of it the bad O1); the next c245 runs with UBWCOOP_C99_LOG=1. Lint: 19 superseded scenarios left flagged, not converted. Queue: c448b257a, c209b257a, then b258 with 499.

- 2026-11-19 06:41 senior: c448b257a passed and lane 1 is now on b257 `6b5faf2a31aafe19`. c209b257a failed at line 110 (scenario: the downed host landed 369 uu off the stand) but proved Down and Warp by GUID across three instances, so card 209 is closed and BugForm is split to card 500 (short bug-map scenario, needs a new 16G approval). b258 `e05cbb851810f799` (card 499) review: no defect found. Attacked: a dropped group row, set overflow, a full c97 queue, a nested Increment, the rejoin journal. Residue: a card 066 compaction snapshot carries no set, so a snapshot Complete can still cascade a parent on the client. Next: c245b258a with UBWCOOP_C99_LOG=1.

- 2026-11-19 07:23 senior: card 502 (four players) written. User asked for 4 player. Static scan: per-player tables are sized 8; bug scaling has a 4-player factor. Sites: the dropship start gate waits for 2 PCs (dropship.cpp:1178); c284 placement holds 3 pawns; c44_other_pawn returns the first copy; engine join limit unconfirmed. Harness needs CLIENT3. Next build after b260.
- 2026-11-19 07:45 senior: b259 runs checked in the logs. c497b259a PASS (client card499 blocked Research_SO2 and Research_Marker2, 0 mismatches). c244b259a VOID on 17 Stadium_SO1 aims (card 501 class); Park cascade PASS; Stadium unproven. c247b259a rc 0, 0 mismatches. Array_SO4 has never completed in any c247 run: the SetWaves C_1 encounter never ends, and the scenario classifies it as a marker step. Card 503.
- 2026-11-19 07:56 senior: card 491 FAIL 2 traced (BP_PlayerCharacterMI). ReceiveBeginPlay binds the BP event FedScoreGained to FedScoreSubsystem.OnFedScoreGained on every pawn, proxies included. FedScoreGained (ubergraph @7554) is only the upgrade step: if ResupplyLevel < 3 and FedScore >= FEDScoreThresholdsArray[level], it raises the level by 1 (the only BP write), calls AddFedScore(ResupplyUpgraded), calls Upgrade() on each WB_Tactical_Support_Prompt_Default_V_2 widget and plays MS_UI_FED_FedScore_Supply_Upgrade_0<level>. Gate for b262: the client drops the FedScoreGained ProcessEvent on the pawn, and the U row apply sets the level, calls widget Upgrade() and plays the sound for the local pawn. The proxy 1 -> 0 is not from the BP; it is still unknown. b261 = c491 nest fix + c502 (3) + c503 diag.

- 2026-11-19 08:10 senior: card 491 closed on b262 e138228037a8dede (c245b262a, checked: 3 host awards, 2 own-step sounds, 0 card132 mismatches, 0 raised at once). b262 still needs c448 before lane 1. Next: card 502 three-instance proofs, then the card 503 c247 diag.

- 2026-11-19 08:14 senior: update check (user asked). PLAY-COOP.sh reads build/RELEASE.txt (b<N>), asks GitHub releases/latest (5 s timeout), and when the tag is newer asks yes/no, downloads the release zip, moves PLAY-COOP.sh, HOW-TO-PLAY.txt, tools/playtest.sh and build/* into place, writes RELEASE.txt and starts the new script. Any failure, no network or no answer starts the installed version; UBW_NO_UPDATE=1 skips it. tools/make-package.sh takes TAG=b<N> to write RELEASE.txt. Tested in a scratch copy: yes (b100 to b257, DLL 6b5faf2a31aafe19), repeat (no ask), no, offline, no tty. b257 packages have no check: players get it with the next release.

Not carded yet: "capture the flag AIs are not moving" on the client (the client log shows the Red
and Blue laser troopers firing from many positions, dying and reviving, with only Attack01, Taunt, Flinch and Spawn
plays relayed; the cause needs the user's answer on what is seen, and the host log), and "grenade does not show in
the client's hand" (the log shows the client's own held grenade drawn; waiting for the user's answer on which hand).

Sync design audit (user decision 2026-11-13): all 11 classes done in `docs/SYNC_AUDIT.md` (ranked table at its end); entries 3 and 4 are traced (card 281 for entry 3; entry 4 fix rides with card 239 after the playtest), entry 5 waits on card 280 rows, diagnostics go to build 5 (card 280).

User 2026-11-13 ("we should be fixing bugs"): real mod bugs are fixed now in batches (one build, one targeted
proof run per touched map); soak runs are discovery mode only, at most 2 per map per day, and no run is spent
on a cleaner scenario pass while mod bugs are open. Batch 1 (junior): 267, 269, 270, 271. Batch 2 (senior does
step 1, junior codes): 260, 261, 254, 263; step 1 done 2026-11-13, each card holds its cause and fix spec
(260 `EndLevel` relay, 261 timeline play rate relay, 254 client skip of `Pickup_Implementation` for remote copies,
263 item 3 sweep once per tick, 258 client skip of local objective Adds, 274 copy magazine follows the client's
(client hits dropped while the copy reloads; 91 in card 243 run 10), 230 own bound for child-actor wire targets, 273 client stops running the sticky gib part state machine and the
host sends no rest row while a sticky part still slides or hangs, 275 per-pawn slot tables reuse the slot of
a gone pawn, 191 diagnostic rows only (frozen-final bone writer, card holds the two predictions);
262 rows closed or not reproduced in run 6). "After the playtest" in the rows below no longer holds for these.

| # | Card | Why now |
|---|---|---|
| 243-247 | Full-objective soak, one card per Trooper map: FED_C3M2 (243), C4M1, C4M2, C5M1, C5M2 (247); shape of cards 240 and 241, tail: Next click retried, then `winkey Enter` on the frontend brief | **user rule 2026-11-12: soak covers all objective completions**; in order. Done: C1M1 27 of 27 (240), C2M1 23 of 23 (241), C3M1 19 of 19 (242). **243 in work** |
| 262 | New WARN classes from the FED_C3M1 soak (242 run 5): late OnHit, fx watch full, missing pickup copy, decal pool cap, pose freeze, one missing `host sent Add` log row | after the playtest |
| 263 | Client frame collapse in the FED_C3M2 PowerPlant fight (10 to 168 frames per 10 s, fallback table at cap 512, 855 untracked; host fine), plus new WARN classes from 243 run 3 (door client-only DoorState, WorldOp NaN, gib asleep, hit location) | with 252, first after the playtest; step 1 decides if the play build is hit |
| 264 | Crosshair trace aim gate for test looks (user 2026-11-13: host looks at the sky in most tests): aim row names the hit actor and the E focus, driver voids on a miss, presence steps stop looking at volumes | **now**, before 243 run 5 and every later soak card |
| 265 | Test players kill the bugs around an objective before acting on it (user rule 2026-11-13): `clear <radius>` verb, kills by real weapon fire on host and client, before every press, aim, throw and damage step | **now**, built with 264 |
| 268 | CTF versus mode, one player per side (user 2026-11-13): static scoping, ends with a design note the user decides on | static research done 2026-11-13 (design note and card split A to E in the card); user decided 2026-11-13: laser rifle only, fenced course, rules set in a pre-game menu; code after the playtest |
| 272 | In-game arena editor for CTF versus (user 2026-11-13): layout files plus an in-game editor; static scoping, design note for the user | after versus cards A to E (268); static research by the senior while runs go |
| 276 | Host copy loses a fire press relayed in the tick of the previous release; the client's automatic reload is not relayed (32 dropped client hits in run A with 274 in) | Traced from run A 2026-11-13; spec in card. Build 4. Run c243-c3m2-b4a void (no ammo), partial counts in the card; proof in the next FED_C3M2 run with ammo; card 274 closes with it. |
| 237 | FED_C4M2: hidden sky boundary sphere (`StaticMeshActor_52`, level `FED_C4M2_Scripting`) has the actor collision bit off on host, on on client; root component is NoCollision on both, so no play effect. Also two actors share the name and one digest key | step 2 static trace done 2026-11-13: the mesh is the Brain Bug arena shield (blocks in the fight); diagnostic spec in card, proof run batched with 245 |
| 252 | Client frame time in heavy fights: 15-25 fps on the client in the FED_C2M1 plasma area and wave hold while the host holds 50-60 (found by 241 run 2); gate now reads client windows too | step 1 done 2026-11-13: logs cannot split the time; time-split diagnostic specified in the card, goes in build 4 |
| 249 | Tutorial support flare destroyed before rest: no copy on the client (found by 240 run 5) | Normal path held in build 4 run c224-c1m1-b4d 2026-11-13: client copy 9 ms after the host spawn, destroyed with the host row. Early-destroy case and status diagnostic not exercised. |
| 250 | 3D frontend after level end: client copies of `Widget_Holder_3DFrontend_C` keep collision off while the host turns it on (found by 240 run 7) | step 1 done 2026-11-13: client HUD has no game mode options, opens the main menu view; fix specified (client start state from the host), after the playtest |
| 251 | Frozen corpse moved on the host after its freeze snap, client copy stays 366-412 uu off (`Pose:1` in 3 digest rounds, found by 241 run 2) | step 1 done 2026-11-13: the client copy moves, only after a relevancy return; fix spec in the card, build 4 |
| 253 | Counted objective ends one short on the client (`LOS_O02_SO05` 1 of 2 against 2 of 2; client gets the parent complete before its last child; found by 241 run 3) | step 1 done 2026-11-13: nested rows are sent parent first; fix spec in the card, build 4 |
| 256 | Dropped weapon pickup rests 50-150 uu (once 2350 uu in z) off on the client: `BP_WeaponPickup_MoritaMk1_C` mismatches in 99 of 120 digest rounds (found by 241 run 4) | step 1 done 2026-11-13: intermittent (run 4 only), rows not provable from capped logs; diagnostic plus two class fixes specified, build 4 |
| 236 | Client cannot resolve the host's dropship spline on FED_C3M2 (card159 WARN) | step 1 done 2026-11-13: owner lookup capped at first 256 of all actors (`c50_find_placed` with `AActor`); fix spec in card, build 4, proof needs one ride run (FAST=0) |
| 239 | A killable crate's drops reach the client twice (own replayed death and the host's rows) | step 1 done 2026-11-13: not a double spawn; host copy taken by the client's own pawn inside its spawn (guard does not know the copy yet), double ammo credit below max; fix spec in card; spawn-window guard coded 2026-11-15 for b68 (weapons.cpp, not built, not proven); double credit below max stays with the SYNC_AUDIT entry 4 cut |
| 228 | Presence gate leftovers from 227: re-check when a player goes down, mech pilots, client copy's own reactions, lanes and script-only presences | step 1 done 2026-11-13: item 1 spec in card (poll re-check); item 2 spec in card (the mech is the pilot's body; proof in a FED_C2M1 run); item 3 closes with the card 254/258 class; item 4 from soak dumps |
| 283 | World ops and collision rows for an actor whose own name is over 64 characters miss on the client (FED_C3M1 pillar child bench, 82 characters: destroyed on the host, whole and solid on the client; 3 digest mismatches in 23 rounds of `c230-c3m1-b4e`) | Traced 2026-11-13: `c95_find_nearest` and the collision relay hold names in 64-character buffers. Class fix spec in card (one constant, `door_c95.cpp`). Build 5, proof rides a FED_C3M1 run. Not a play blocker. |
| 231 | One corpse settles 100 to 150 uu apart on FED_C3M1 (digest Pose:1) | closed 2026-11-13 without code: not reproduced on later builds (c229-c3m1, c242-c3m1: 0 over 100 uu); reopen rule in card |
| 232 | Digest timer round overlaps a command round (round cut, detail ask dropped); test instrument only | step 1 done 2026-11-13: host starts the next round before the client answers; spec in card (settle window, command latch), batched with a build |
| 233 | Turret rows for run-time turrets (TurretTrooper drops) match no turret on the client | step 1 done 2026-11-13: same class as card 271 (coded); closes on the FED_C4M1 soak (card 244) rows, no code |
| 224 | Client press-to-start screen on FED_C1M1: title, prompt and Enter in the play path | build 4 run c224-c1m1-b4d 2026-11-13: log row held (`chapter 0->0 level 0->0`). Screen not proven: the client joins after the host continued and never has the screen in this harness. Item 3 and the title shot go to the playtest check list. |
| 225 | The other player's name tag draws over full-screen widgets and cutscenes | build 4 run c224-c1m1-b4d 2026-11-13: pause-menu shot shows no tag, held. No log row (card110 diagnostic cap). Press-to-start and cutscene cases not exercised; playtest check list. |
| 151 | Smaller mod channel packets (takes card 150's open items) | bandwidth, not blocking |
| 149 | Client change log names the right cause | diagnostic, batch into any run |
| 153 | Host panel shows the external address, UPnP | play feature; port source, UPnP gating and file list in card addendum (2026-11-13) |
| 181 | The host copy fires exactly the client's rounds (jitter case left by 180) | rare extra tracer and claim shift; senior trace done 2026-11-13: gate `USTWeaponFireAbility::Fire` on the copy by per-round client reports, spec and predictions in card; after playtest |
| 200 | Seven first-tick resets not traced against rows written inside LoadMap | Step 1 traced 2026-11-13: collision journal (132 rows) and dropship journal (5 rows) wiped on first tick: move to LoadMap enter, with card 219 cap. Spec in card; build 4 candidate, proof from any map run. |
| 215 | A client corpse never simulates; it only shows the host's bodies | senior design review 2026-11-12: static design trace; cards 191, 194, 197, 203, 207 hold behind it |
| 189 | A corpse that falls more than 1000 uu is still handled as a corpse | Traced 2026-11-13: fell check stops long-fall corpses in the air at 1000 uu. Spec in card (down trace decides, hit distance from ragdoll root). Rides a FED_C3M1 run. Addendum 2026-11-13: fell-through corpses go out as frozen finals (client copy 0xe40 fell without end). Build 4 run c230-c3m1-b4e: fell-through fix held (6 guids frozen and held on the client); long-fall branch not exercised, stays open. |
| 191 | A frozen final leaves the bones on the bodies when the game's ragdoll start was blocked | bone 0 up to 316 uu off body 0 in c186-r2 (guid 0xf8); static first |
| 203 | A corpse re-created on the client stays where it was posed (split from 164) | c186-r1: 269 uu drift after the pose, host did not move it; may share card 191's cause |
| 193 | An OnHit with effects after GrenadeExplode reaches the client | Traced 2026-11-13: false WARN (client gets the effect by the card 136 manager, run 10 rows). Build 4: demote to note. |
| 194 | A corpse on a rejoining client lies where the host has it | Traced 2026-11-13: the rehold after the game's own ragdoll start does not re-pose the bodies. Spec in card (re-apply stored snap), build 4 candidate. |
| 207 | A corpse the host holds after an in-place load has a pose row | Traced 2026-11-13: save-spawned dead NPC is shown without a ragdoll start, so no pose row; spec in card (track at the mesh-shown edge). Build 4 candidate, proof on c142-r4 scenario with 194 and 190. |
| 197 | Every NPC class with no ragdoll start shows the host's corpse state | Traced 2026-11-13: ragdoll is gated by the host's significance level, so far deaths leave a corpse with no pose row (run A: 22 client WARN, Chariot corpses hidden 9 s). Spec in card: one function with 207, death edge plus mesh-shown edge. Build 4, proof in the FED_C3M2 rerun. |
| 190 | A client that joins while dropships fly sees them where the host has them | Traced 2026-11-13: copy lacks StartAction startup/target bytes (+0x481/+0x482), so no end slow-down; spec in card (frames carry them). Build 4 candidate, proof rides a rejoin run. |
| 209 | Player rows name any player's pawn (three and four players) | split from 208: BugForm and Down rows reach only own and host pawn; step 1 site table and NetGUID addressing proposal in card (2026-11-13); needs a three-instance run; after playtest |
| 216 | A player's Swoop effects do not all reach the other side | from c212-r1: client AirDash sound absent on host; Niagara and montage unproven |
| 217 | The client's Glide re-activates every 50 ms with no key held | from c212-r1: fills the card024 cap; step 1 evidence and ordered checks in card (likely the driver's key press with no release); bug campaign |
| 219 | The collision journal fills and drops rows | Traced 2026-11-13: rows are real save-load state (live crates 0 to 3). Spec in card: cap 2048 plus paced replay, build 4 with card 200. Proof: one from-save C2M1 join. |
| 218 | Dropship AI spawners sit at a different place on the client | Recurred in run B rounds 30 to 32 on three other script actors with one shared offset (host moved a landing zone group, client did not): real state. Parent is a troop dropship at a different place on the client (card 159 class). The deciding `card218` destroy row is not in build 4; its spec is in the card for build 5. |
| 220 | Training prompts never show on the client | build 4 run c224-c1m1-b4d 2026-11-13: per-player pass and skip rows held on both sides, host shot shows no prompt. Client prompt shot void (scenario left the pause menu up); playtest check list. |
| 279 | Gib rest snap does not hold on the client (118 of 2543 rest checks over 10 uu in c230-c3m1-d2, mostly non-sticky parts back at their place before the snap) | step 1 diagnostic row `card279` in the next build, read in the next FED_C3M1 run; fix follows the row; not a play blocker Build 4 run c230-c3m1-b4e: diagnostic not in build 4 (26 of 2601 over 10 uu); goes to build 5. |
| 280 | Level-script entries logged on both sides (diagnostic, build 5) | Sizes audit entry 5: client script runs from its own triggers |
| 281 | A client hit claim with no round makes its own round | Audit entry 3: damage must not need the copy to have fired; build 5: run c243-c3m2-b4a still dropped 4 claims with the client holding rounds |
| 282 | Client "local sim" change table split into load and play time (diagnostic, build 5) | Audit entries 6 and 9: lists every place the client still acts as a second owner, by class |
| 221 | A pickup that respawns and is taken in the same tick stays visible on the client | Traced 2026-11-13: both rows applied in order; the client's own respawn timer re-enables 2 ms later and its take is blocked. Spec in card: host rows are the only enable on the client. Build 4. |
| 223 | Hits applied to a late joiner land on copies up to 1804 uu from the host's bug | Traced 2026-11-13: hit rows waited 3520 ms in the pace queue behind the join replay; same root as 226. Closes with 226 plus one late-join run. Not a play blocker. |
| 226 | Host pace queue backs up 30 s at low host frame rate (acks late, client ragdolls fall back) | Traced 2026-11-13: drain is about 2 kB per frame whatever the frame time; one FIFO for all ops. Build 4 carries one diagnostic row (`card226: host pace hold`); fix after the playtest. |

Parked (`docs/cards/parked/`): 035 crouch frame, 036 pak mount probe, 126 autoplay (partial; SO2 open).
