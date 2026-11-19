# 247: Full-objective soak, FED_C5M2

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

Source: `kis.py` dump of the full FED_C5M2_Scripting extract (0 decode errors; card 301 pass). Offsets are
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
