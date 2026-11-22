# 247: Full-objective soak, FED_C5M2
Status 2026-11-22: open, parked until after the user's playtest.

Same shape and rules as card 240 (read `docs/cards/done/240-*.md` or `docs/cards/240-*.md` first: no
`objective complete`, wait on the host Complete row and the client replay, both players in presence volumes,
objectives dumps at the end, faults become their own cards).

Goal (one): one scenario that plays FED_C5M2 from spawn to the mission end and completes every objective by
its own trigger.

Objective list M = 29, from the game's data table `DT_LevelObjectiveData_FED_C5M2` (keys
`FED_C5M2_<id>_NAME`; the runtime ids may differ in case):
ARRAY_O1
ARRAY_SO1
ARRAY_SO2
ARRAY_SO3
ARRAY_SO4
ASSASSIN_O1
ASSASSIN_SO1
ASSASSIN_SO2
ASSASSIN_SO3
FINALE_O1
FINALE_SO0
FINALE_SO1
FINALE_SO2
FINALE_SO3
FINALE_SO4
FINALE_SO5
FINALE_SO6
FINALE_SO7
OUTPOST_O1
OUTPOST_SO1
OUTPOST_SO2
OUTPOST_SO3
OUTPOST_SO4
OUTPOST_SO5
RESEARCH_O1
RESEARCH_SO1
RESEARCH_SO2
RESEARCH_SO3
RESEARCH_SO4
The last soak (c238-c5m2) sent 1 sub-objective completions: Array_SO1 

Step 1, static, no run, appended here as a table: each id in play order with parent, counter, real trigger
with level-script or class evidence (`FED_C5M2_Scripting`), the driver steps that fire it, the synced
follow-ups to wait on, and any id that is a marker only or on a branch the map never activates (state the
evidence; such an id leaves M only with the senior's agreement).

Step 2: `tools/scenarios/c247-c5m2.txt` (new file; older scenarios stay).

Step 3: run, budget 3 runs. Command:
`systemd-run --user --scope -q -p MemoryMax=12G env SCENARIO=tools/scenarios/c247-c5m2.txt DEPLOY=1 FAST=1 HOST_EXTRA_ENV="UBWCOOP_TRAVEL=/Game/Maps/FED/FED_C5M2?listen UBWCOOP_GOD=1 UBWCOOP_C132_FX=1 UBWCOOP_C136_LOG=1" CLIENT_EXTRA_ENV="UBWCOOP_C132_FX=1 UBWCOOP_C136_LOG=1" tools/run_pair.sh c247-c5m2 3600`

Files allowed: `tools/scenarios/c247-c5m2.txt`, this card. Driver verbs only after a report to the senior.
No other mod file. No build while a game runs.

Done when: final objectives dump shows `complete 1` for M of M on host and client, lists match, both sides
reach the campaign end (credits or menu travel; state which in step 1), 0 crash, every digest round 0 mismatches, frame
windows at or over 500 outside load. Report: completed N of M, each fault with its lines, new WARN classes
against c238-c5m2.

## Step 1 result (2026-11-18, senior, static)

Source: `tools/kismet/kis.py` dump of the full FED_C5M2_Scripting extract (0 decode errors; card 301 pass). Offsets are
ubergraph offsets. "Hidden add" means the add call sits in the handler but is an unresolved import call, so the
compact grep does not show it. Three objectives open at once at the start (`Debug set objectives` / intro chain
@14898 -> @12008 -> @216 -> @334): Research_O1, Outpost_O1, Array_O1, in any order. Each one's completion calls
`Add to finale count` (@10974, `finaleReady`); at 3 (@6744) the script runs `Research collapse`,
`ResearchHoleTankerNuke` and, 8 s later, `Assassin Objective` (@6857). Assassin_O1's end adds Finale SO0.

| Order | Id | Parent / counter | Real trigger (offset) | Driver | Wait rows |
|---|---|---|---|---|---|
| R1 | RESEARCH_SO1 | Research_O1, count 3 (SO1_A/B/C) | PanelSwitch WithoutScreen C_2 @14739 -> SO1_A @9732 (+2 s `Tanker Door`), C_3 @14744 -> SO1_B, C_4 @14749 -> SO1_C | 3 switch presses | 3 child completes, then Research_SO1 |
| R2 | RESEARCH_SO2 | Research_O1 | bind on Research_SO1 complete (@10063): presence C_22 off, Research_Marker1 done, EM_Dropship_C_1 and EM loop C_10 start. Done when 3 satchel pickups counted: SatchelItemPickup C_1 @16718, C_2 @16972, C_3 @16977 -> @16858; then callout Research_SO3 | 3 pickups (E on each) | Research_SO2; callout SO3 |
| R3 | RESEARCH_SO3 | Research_O1, count 3 (SO3_A/B/C), hidden add at start @13257 | SatchelPlacementSite C_1 @17440 -> SO3_A, C_2 @17445 -> SO3_B, C_0 @17435 -> SO3_C (each spawns an AI spawner); count done enables PanelSwitch WithScreen C_3 @10756 | 3 satchel holds; **clear before each hold** (c244b234a lesson) | 3 child completes, Research_SO3 |
| R4 | RESEARCH_SO4 | Research_O1 | bind on Research_SO3 complete (@17545). PanelSwitch WithScreen C_3 @17723 -> destroy HiveEntrance C_7, +1.5 s C_5, +1.5 s C_9 -> @18456 SO4, @18493 Research_O1 | 1 switch press, wait about 3 s | Research_SO4, Research_O1, `Add to finale count` |
| A1 | ARRAY_SO1 | Array_O1 | presence C_18 enter @4407 (Add Array_SO2, then complete SO1 @4421) | walk/warp into C_18 | Array_SO1 |
| A2 | ARRAY_SO2 | Array_O1, count 2 (SO2_A/B) | PanelSwitch WithScreen C_2 @14348 -> SO2_A @14165; C_0 @14343 -> SO2_B @14254 | 2 switch presses | 2 child completes, Array_SO2 |
| A3 | ARRAY_SO3 | Array_O1 | bind on Array_SO2 complete (@11389) enables PanelSwitch WithoutScreen C_1 @11495. Press @14353: closes Large doors C_4/1/5/6/7/8, fall zones on, `Add Array_SO4` @5020, SO3 @4459, EM loops C_8/C_9 end | 1 switch press, **both players inside first** (card 492 site) | Array_SO3 |
| A4 | ARRAY_SO4 | Array_O1 | Add @11733 -> @4914: EM loop C_7 and SetWaves C_1 start. SetWaves C_1 encounter event @5127: cleanups, SO4 @5223, Array_O1 @5260, six doors open, fall zones off, MechDrop C_4, `Add to finale count` | survive / `clear` the C_1 waves | Array_SO4, Array_O1, doors open on both |
| O1 | OUTPOST_SO1 | Outpost_O1, count 4 (SO1_A..D) | nests: C_6 @21275 -> A, C_9 @20375 -> B, C_10 @21280 -> C, C_15 @21285 -> D | 4 nest kills | 4 child completes |
| O2 | OUTPOST_SO2 | Outpost_O1 | bind on Outpost_SO1 (@21088) -> @3146: EM_Dropship C_2/C_3 start, presence C_8 on. Presence C_8 enter @20246: `Add Outpost_SO3` (adds SO3, starts SetWaves C_9 @12504), SO2 @2670 | walk into C_8 | Outpost_SO2 |
| O3 | OUTPOST_SO3 | Outpost_O1 | SetWaves C_9 encounter event @20194: `Add Outpost_SO4`, SO3 @20208. (@3909 completes it again at the end.) | survive / clear C_9 waves | Outpost_SO3 |
| O4 | OUTPOST_SO4 | Outpost_O1 | Add @2417: EM loop C_2, cleanups C_11 / SetWaves C_2, +2 s `Outpost Bug Waves` @3285: **only if `atWhiskey`** (presence C_402 enter @32390 sets it, exit @32573 clears it; re-enter with `hordeReady` re-fires) -> SetWaves C_5, HordeZone C_1 KillEmAll. SetWaves C_5 event @21432: adds SO5, SO4 @21503 | both players inside C_402 and stay there; clear C_5 waves | Outpost_SO4 |
| O5 | OUTPOST_SO5 | Outpost_O1 | added @21452; no CompleteSubObjective in the script. Closed by CompleteObjective Outpost_O1 @3946, reached via `CompleteWhiskey` (+14 s after C_5, after Bombing Run +5 s and Airstrike Overkill +12 s) @35481 -> @3909 | wait about 15 s | Outpost_O1, `Add to finale count` |
| S1 | ASSASSIN_SO1 | Assassin_O1 | added by `Assassin Objective` @6894; presence C_17 enter @8100 (`Add Assassin_SO2`, SO1 @8114) | walk into C_17 | Assassin_SO1 |
| S2 | ASSASSIN_SO2 | Assassin_O1 | presence C_6 enter @3057 (`Add Assassin_SO3`, SO2 @3071, SO1 again @3108) | walk into C_6 | Assassin_SO2 |
| S3 | ASSASSIN_SO3 | Assassin_O1 | presence C_21 enter @8152 -> @6405: SetWaves C_7 and +5 s `Spawn Fodder`. SetWaves C_7 event @8157: +3 s `Trooper Wall Break`, SO3 @8230, Assassin_O1 @8267, `Add Finale SO0` @8327 | walk into C_21, clear C_7 waves | Assassin_SO3, Assassin_O1, Finale_O1 added |
| F0 | FINALE_SO0 | Finale_O1 (AddObjective @24709 with hidden SO1Dumb) | presence C_23 enter @8710: SO0, then `Add Finale_SO2/SO3/SO4` @8747-8775, SetWaves C_8 @8900 | walk into C_23 | Finale_SO0 |
| F2 | FINALE_SO2 | Finale_O1, count 4 (SO2a-d) | nests C_17 @24443 -> a, C_20 @24625 -> b, C_21 @24667 -> c, C_22 @24869 -> d; `FinaleBugNestsDestroyed` >= 4 @24576 -> SO2 @19923, `Check Finale SO1 Trigger` | 4 nest kills | 4 children, Finale_SO2 |
| F3 | FINALE_SO3 | Finale_O1, count 3 (SO3a/b/c) | satchel pickups C_4 @18605 -> a, C_6 @18870 -> b, C_5 @18912 -> c; `satchelCountFinale` = 3 -> SO3 @18782, callout SO4 | 3 pickups | Finale_SO3 |
| F4 | FINALE_SO4 | Finale_O1, count 3 (SO4a/b/c), hidden add @18954 | placement-site explosions C_4/C_5/C_6 @25863/25882/25901 -> DestroyPillar A/B/C -> SO4a/b/c; `PillarsDestroyed` >= 3 -> `Nuke Town 2`, SO4 @19473, `Check Finale SO1 Trigger` @19510 | 3 satchel holds; clear first | Finale_SO4 |
| F1 | FINALE_SO1 | Finale_O1, count = live assassins (`imp1054` add @25734, set by `Mark Assassins` after `Add Finale_SO1` @5075) | `Check Finale SO1 Trigger` @24945 (pillars >= 3 and nests done) -> SetWaves C_6 @5039. Done when `AssassinsEncountersEnded` >= 2 (@9126): SetWaves C_6 event @9030 and SetWaves C_8 event @25837 -> SO1 @9170 | clear C_6 and C_8 waves (C_8 started at F0) | Finale_SO1 |
| F6 | FINALE_SO6 | Finale_O1 | bind on Finale_SO1 (@19812, event named `Add Finale_SO5`) -> @7020 `Add Finale_SO6` (add @30519), transport dropship C_2 StartAction(0). Presence C_408 enter @30571: `Nuke Town`, `Add Finale_SO7` -> SO6 @30898, SO7 added @30955; arms `Cave in` | walk into C_408 | Finale_SO6 |
| F5 | FINALE_SO5 | Finale_O1 | **no add anywhere in the script** (only Bind, the `Add Finale_SO5` handler adds SO6). Complete call @31007 from presence C_62 @35214 (spawner C_42, `RockFall6`). Candidate to leave M: check the objective data table for an auto-add before agreeing | walk into C_62 | Finale_SO5 row if the id exists; else none |
| F7 | FINALE_SO7 | Finale_O1 | added @30955; explicit complete @22174 only from `Debug call dropship` @30494. Normally closed by CompleteObjective Finale_O1 @13949 (transport C_2 PlayerEntering), which starts `End The Level` +27 s @9208 (EndLevel @9540) | board dropship C_2 (E), both players | Finale_O1, travel |

Marker ids (not in M): Array_Marker @150, Outpost_Marker @236, Research_Marker1 @10216, Research_Marker2 @2728,
Finale_SO1Dumb @24746.

Mission end: CompleteObjective Finale_O1 on transport C_2 PlayerEntering @13949, or PlayerSkippedFlight @28639
-> `End The Level`, `STGameInstance:EndLevel` @9540. C5M2 is the last Trooper map: state in the step 2 report
whether EndLevel goes to credits or the menu.

Co-op checks for the run (already in the card 301 audit unless marked new):
- Class E: one player boarding transport C_2 completes Finale_O1 and starts the level end. Driver boards both.
- Class G: `Cave in` (@31328, every 30 s after C_408) blasts at player 0's location (card 493).
- Class H: PanelSwitch WithoutScreen C_1 closes six doors (card 492).
- New, not carded: `atWhiskey` is one bool set by any C_402 enter and cleared by any C_402 exit. If one player
  leaves C_402 while the other stays, `Outpost Bug Waves` pops and Outpost_SO4 waits until a player enters
  again. Recoverable by re-entry, so log only. The driver keeps both players inside C_402 from SO4 add to the
  C_5 clear.
- Bound counts by encounter event (Array_SO4, Outpost_SO3/SO4, Assassin_SO3, Finale_SO1): `clear <radius> kill`
  must end the wave set through the encounter manager, or the event does not fire. Wait on the objective row,
  not on the clear.

## Step 2 generator (2026-11-18 15:32, senior)

`tools/gen_c247.py` writes `tools/scenarios/c247-c5m2.txt`. It is `tools/gen_c244.py` with the map changed (same
objective-driven blocks: `objective now` / `expect` / `go all`, use, nest and satchel kinds, the b236 clear before each
satchel hold), the card 490 stamina block removed, and the end waiting on the Finale_O1 complete row (C5M2 is the last
FED mission, no next map). Skip ledger `tools/scenarios/c247-passed.txt` is empty. Class names checked against the
C5M2 scripting dump: Bugnest HumanGameplay, PanelSwitch With/WithoutScreen, SatchelItemPickup, SatchelPlacementSite
all match the generator. Dry run with a zero hash: 2485 steps, `drive.py --parse` 0 bad (cap 2600).
Usage: `python3 -I tools/gen_c247.py <dll hash16> > tools/scenarios/c247-c5m2.txt`, run line in the scenario head.

## Senior prep (2026-11-13): what the junior needs before step 1

Not started. Starts after the 2026-11-13 playtest, in card order, one map at a time.

- Model for step 1: the "Step 1 result" section of `docs/cards/243-full-objective-soak-c3m2.md` (line 80 on):
  objective table (id, parent and counter, real trigger with ubergraph offsets, driver steps, wait rows), then
  the encounter manager chains, presence volumes, switches with their stand side, timed holds and the mission
  end. Same columns here, from `FED_C5M2_Scripting` and `DT_LevelObjectiveData_FED_C5M2`.
- Rules that card 243 added and this card inherits: "Rules carried from cards 241 and 242" in card 243, and
  "Gameplay test scenario rules" in `docs/STATE.md`. In short: no `objective complete`; waits on log rows, never
  sleeps; `clear` before every press, aim, throw and damage step (card 265); a `shot` after every warp and look
  at a target, each one viewed (card 264); menus close by `winkey` or `ui continue`, never `key` (card 222).
- Scenario from a generator: copy `tools/gen_c243.py` to `tools/gen_c247.py` (it stamps the DLL hash guard and
  the map check); under 2000 steps; `python3 tools/drive.py --parse <file>` must report 0 bad before any run.
  The old route `tools/scenarios/c234-c5m2.txt` gives the walk order and the warp points only; its objective steps
  are not by real trigger.
- Start of every FED map: the client stays on the operation card until `host ui continue` and the client
  continue step (see the head of `tools/gen_c243.py`); keep that block.
- Run policy (user 2026-11-13): discovery mode, at most 2 runs per map per day, `voidsoft` after the DLL and map
  checks so one launch shows every fault of the route; no rerun for a cleaner pass while mod bugs are open.
  Each fault becomes its own card with its log lines; nothing is fixed inside this card.
- Before a run: GPU `cat /sys/class/drm/card1/device/gpu_busy_percent` under 30, no other game running, the
  `MemoryMax=12G` scope, test sound to the null sink.
- Ids that are markers only or never added leave M only with the senior's agreement: report them with the
  evidence (no Add or Complete call in the ubergraph) before the first run.

## c247b302a (b302 cc796efeb952a0f7, senior 2026-11-19)

- 558 s, scenario passed, 24 digest rounds at 0 mismatches. Finale_SO0 to SO4 (all children), SO6 and SO7
  completed; Finale_SO5 has no add (as the step 1 table says) and SO1Dumb stays open. Finale_O1 did not complete.
- Cause: the transport BP_PlayerTransportDropship_C_2 launched at the finale (card342 rows, host 20640037) and
  landed (DescendToLandCompleted, flag 0 -> 0, host 20649425) 3.5 s after SO7; the scenario never boarded it. It
  completes Finale_O1 only on PlayerEntering (@13949).
- Fix (gen_c247.py main): wait the landing row, then the gen_c243 `board()` recipe (client E first, EnterDropship
  row; host E until the Finale_O1 complete row, 3 tries), then wait for the level-end travel's card330 map rows, with a
  `host travel restart` if none came in 90 s, so the map-end rows (card523, card521 client) are written.

## Debrief exit (senior, 2026-11-19, after c247b302b)

- c247b302b: the level ended on both sides, but the fallback `host travel restart` at the debrief (card113
  TransitionToLevel(chapter 4, level 1)) loaded no map, so the map-end rows (card330, card523, card521 client) were
  never written.
- `gen_c247.py` now ends as `gen_c244.py` `tail()` does: after the card260 EndLevel row, `host ui click
  BndEvt__TrooperLevelEnd_WB_Button_Next` until the host frontend row (net mode 2), then the client frontend row
  (net mode 3), then the card330 `map FED_C5M2 step` rows on both sides. The gate is the card260 row, because the
  C5M2 debrief's input row comes from BP_HUD_Manager, not WB_TrooperLevelEnd.
- Regexes tested against real lines: card260 and the Finale_O1 done row (c247b302b host, 1 match each), both frontend
  rows and the card330 map row (c514a). Scenario regenerated with b303 pins (8ece51514ce72a9d). Backups
  `build/gen_c247.py.pre-debrief`, `build/c247-c5m2.txt.pre-debrief`.

## Run c247b303a (b303 8ece51514ce72a9d, junior run, senior read 2026-11-19)

- The level end passes: Finale_O1 sent, host EndLevel, debrief Next, host 3D_Frontend net mode 2, client net mode 3,
  card330 `map FED_C5M2 step` rows on both sides. 557 s, rc 0/0, scenario passed, 0 VOID, FATAL 0.
- Map-end rows are present: card521 client 1883 deltas, 0 misses; card523 (still r2 here) 132 own falls.
- One Pose mismatch in 26 digest rounds (round 13, AphidBug 0xfc0): a corpse that fell through the floor and streamed as
  a long fall. Cause and fix in card 525.
- Dead time: 168 s in 24 timed-out `try` waits (two of 15 s, many of 8 s). That is the next idle cut for this
  scenario.

## Idle cut after c247b303a (senior, 2026-11-19)

- The 8 s client `replayed Complete` timeouts came from marker steps ended by "guide marker ended by" (Outpost_SO4 by
  Outpost_SO2, 50 rows; Outpost_SO5, 5 rows): no Complete is sent for them. gen_c247.py now gates that wait on a done
  host wait with no marker-ended row in the block, and a marker-ended row runs `objective next`. Lesson 29 in
  docs/reference/objective-routines.md. Scenario regenerated for b307 d7043648110be420 (3239 lines).
- Predictions (next run): 0 client `replayed Complete` timeouts after a marker-ended row; Outpost_SO4 marker-ended rows
  down from 50 to at most 2; total timed-out `try` wait time well under 168 s; card 525: 0 Pose mismatches.

## Run c247b307a (b307 d7043648110be420, junior run, senior dig 2026-11-19)

- FAIL: all 42 blocks ran out at Array (Array_SO3 never reached), so Finale never started and the level did not end.
  21 digest rounds, 0 mismatches. No card 525 row (no corpse fall). Timed-out try waits 312.4 s, 290 s of them in
  the dropship tail after the blocks ran out.
- Cause: my `objective next` on a marker-ended row. Outpost_SO4 took 25 blocks (b303a: 11), 121 marker-ended rows.
  The real fault is under it: Outpost_SO4 is not a guide for Outpost_SO2. SO2 completed before SO4 was added, and SO4
  completes by its own trigger (table row O4). The wait called it done at once each time.
- Fix (after b307): `objective next` on marker-ended removed from gen_c247.py; objective_c284.cpp counts a marker as
  guide-ended only when a wait saw its sibling open while the marker was open (backup build/objective_c284.cpp.pre247g).
  The client replay-wait gate stays. Lesson 30. Scenario regenerated (3197 lines); its pins need the next build.
- Predictions (next build): 0 "guide marker ended by Outpost_SO2" rows for Outpost_SO4; Outpost_SO4 completes by
  CompleteSubObjective in at most 11 blocks (as b303a); the blocks reach Finale and the level ends; PowerPlant_Marker
  on C3M2 still guide-ends (c263 runs).

## Run c247b308a (b308, killed by the user, junior dig 2026-11-19)

- Outpost_SO4 passed (the b308 guide fix held). The run broke at Assassin: Array_SO4's presence try 3 (cmd 419,
  24542604 ms) picked BP_PlayerPresence_C_17, 52826 uu from Array's marker. C_17 is Assassin_SO1's trigger, so
  CompleteSubObjective(Assassin_SO1) was sent before Assassin_SO1 was added, C_17 switched off, and Assassin_SO1
  stayed open; its own tries then stood 46867 uu off (C_29).
- Fix, b309 a86a3943a7f0f8fe (junior): a next presence over 20000 uu from the marker actor is not entered. The largest
  pick seen in b303a and b308a that was not past it was 18140 uu (C_16). Lesson 31.
- Open: Array_SO4 needed 3 tries in b308a and tries 2 and 3 for an earlier step stood at the same presence
  (11087 uu) four times without completing it. c247b309a shows whether Array_SO4 completes within the bound.

## Run c247b309a (b309 a86a3943a7f0f8fe, junior run, senior dig 2026-11-19): level ended

- The level ended (Finale_O1 complete at 25604848 ms, EndLevel to the client at 25631868 ms), 666 s, rc 0/0, 0 FATAL
  or ERROR, 24 digest rounds at 0 mismatches. The 20000 uu cap fired once (Array_SO4 try 3, C_17) and Array_SO4 still
  completed. Outpost_SO4 took 7 blocks, with 0 "guide marker ended by" rows.
- Idle: timed-out try waits 64.5 s in 13 (b307 312 s). The biggest item is `clear kill`, 298 s over 243 calls; that is
  card 513 material.
