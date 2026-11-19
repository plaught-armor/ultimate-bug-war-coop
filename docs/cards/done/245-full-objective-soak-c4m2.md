# 245: Full-objective soak, FED_C4M2

Same shape and rules as card 240 (read `docs/cards/done/240-*.md` or `docs/cards/240-*.md` first: no
`objective complete`, wait on the host Complete row and the client replay, both players in presence volumes,
objectives dumps at the end, faults become their own cards).

Goal (one): one scenario that plays FED_C4M2 from spawn to the mission end and completes every objective by
its own trigger.

Objective list M = 27, from the game's data table `DT_LevelObjectiveData_FED_C4M2` (keys
`FED_C4M2_<id>_NAME`; the runtime ids may differ in case):
BRAINBUG_O1
BRAINBUG_SO1
BRAINBUG_SO2
BRAINBUG_SO3
BRAINBUG_SO4
BRAINBUG_SO5
BRAINBUG_SO6
BRAINBUG_SO7
BRIDGE_O1
BRIDGE_SO1
BRIDGE_SO2
BRIDGE_SO3
BRIDGE_SO4
PLAZA_O1
PLAZA_SO1
PLAZA_SO2A
PLAZA_SO2B
PLAZA_SO3
PLAZA_SO4
PLAZA_SO5
SEWERS_O1
SEWERS_SO0
SEWERS_SO1
SEWERS_SO2
SEWERS_SO3
TUNNEL_O1
TUNNEL_SO1
The last soak (c237-c4m2) sent 1 sub-objective completions: Plaza_SO1 

Step 1, static, no run, appended here as a table: each id in play order with parent, counter, real trigger
with level-script or class evidence (`FED_C4M2_Scripting`), the driver steps that fire it, the synced
follow-ups to wait on, and any id that is a marker only or on a branch the map never activates (state the
evidence; such an id leaves M only with the senior's agreement).

Step 2: `tools/scenarios/c245-c4m2.txt` (new file; older scenarios stay).

Step 3: run, budget 3 runs. Command:
`systemd-run --user --scope -q -p MemoryMax=12G env SCENARIO=tools/scenarios/c245-c4m2.txt DEPLOY=1 FAST=1 HOST_EXTRA_ENV="UBWCOOP_TRAVEL=/Game/Maps/FED/FED_C4M2?listen UBWCOOP_GOD=1 UBWCOOP_C132_FX=1 UBWCOOP_C136_LOG=1" CLIENT_EXTRA_ENV="UBWCOOP_C132_FX=1 UBWCOOP_C136_LOG=1" tools/run_pair.sh c245-c4m2 3600`

Files allowed: `tools/scenarios/c245-c4m2.txt`, this card. Driver verbs only after a report to the senior.
No other mod file. No build while a game runs.

Done when: final objectives dump shows `complete 1` for M of M on host and client, lists match, both sides
reach the next map (FED_C5M1), 0 crash, every digest round 0 mismatches, frame
windows at or over 500 outside load. Report: completed N of M, each fault with its lines, new WARN classes
against c237-c4m2.

## Senior prep (2026-11-13): what the junior needs before step 1

Not started. Starts after the 2026-11-13 playtest, in card order, one map at a time.

- Model for step 1: the "Step 1 result" section of `docs/cards/243-full-objective-soak-c3m2.md` (line 80 on):
  objective table (id, parent and counter, real trigger with ubergraph offsets, driver steps, wait rows), then
  the encounter manager chains, presence volumes, switches with their stand side, timed holds and the mission
  end. Same columns here, from `FED_C4M2_Scripting` and `DT_LevelObjectiveData_FED_C4M2`.
- Rules that card 243 added and this card inherits: "Rules carried from cards 241 and 242" in card 243, and
  "Gameplay test scenario rules" in `docs/STATE.md`. In short: no `objective complete`; waits on log rows, never
  sleeps; `clear` before every press, aim, throw and damage step (card 265); a `shot` after every warp and look
  at a target, each one viewed (card 264); menus close by `winkey` or `ui continue`, never `key` (card 222).
- Scenario from a generator: copy `tools/gen_c243.py` to `tools/gen_c245.py` (it stamps the DLL hash guard and
  the map check); under 2000 steps; `python3 tools/drive.py --parse <file>` must report 0 bad before any run.
  The old route `tools/scenarios/c237-c4m2.txt` gives the walk order and the warp points only; its objective steps
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

## Step 1, part A (senior, 2026-11-17 11:59): Add and Complete call sites, static

Source: the `FED_C4M2_Scripting` ExecuteUbergraph disassembly (offsets are ubergraph offsets). 38 ids have an
Add or Complete call.

| Id | Parent | Add at | Complete at |
|---|---|---|---|
| BrainBug_O1 | top | 20671 | 1743 |
| BrainBug_SO1 | BrainBug_O1 | 20728 | 15457 |
| BrainBug_SO2 | BrainBug_O1 | 15668 | 17576 |
| BrainBug_SO2_L1 | BrainBug_SO2 | 15744 | 17078 |
| BrainBug_SO2_L2 | BrainBug_SO2 | 15815 | 17614 |
| BrainBug_SO2_R1 | BrainBug_SO2 | 15886 | 17036 |
| BrainBug_SO2_R2 | BrainBug_SO2 | 15957 | 16994 |
| BrainBug_SO3 | BrainBug_O1 | 17525 | 18071 |
| BrainBug_SO4 | BrainBug_O1 | 26628 | 16768 |
| BrainBug_SO5 | BrainBug_O1 | 16717 | 18145 |
| BrainBug_SO6 | BrainBug_O1 | 26704 | 13952 |
| BrainBug_SO7 | BrainBug_O1 | 13901 | none |
| Bridge_Marker | Bridge_O1 | 8477 | none |
| Bridge_O1 | top | 31382 | 3629 |
| Bridge_SO1 | Bridge_O1 | 31419 | 9127 |
| Bridge_SO2 | Bridge_O1 | 9076 | 19572 |
| Bridge_SO3 | Bridge_O1 | 19476 | 19128 |
| Bridge_SO3a | Bridge_SO3 | 18545 | 18743 |
| Bridge_SO3b | Bridge_SO3 | 18616 | 18847 |
| Bridge_SO3c | Bridge_SO3 | 18687 | 18795 |
| Bridge_SO3d | Bridge_SO3 | 18473 | 18899 |
| Bridge_SO4 | Bridge_O1 | 19077 | 3592 |
| Plaza_Marker | Plaza_O1 | 8405 | none |
| Plaza_O1 | top | 31289 | 27090 |
| Plaza_SO1 | Plaza_O1 | 31326 | 9628 |
| Plaza_SO2a | Plaza_O1 | 9577 | 5117 |
| Plaza_SO2b | Plaza_O1 | 5066 | 4188 |
| Plaza_SO3 | Plaza_O1 | 4137 | 21784 |
| Plaza_SO4 | Plaza_O1 | 12796 | 12407 |
| Plaza_SO5 | Plaza_O1 | 23227 | none |
| Sewers_Marker | Sewers_O1 | 8689 | none |
| Sewers_O1 | top | 31475 | 7014 |
| Sewers_SO0 | Sewers_O1 | 31512 | 8968 |
| Sewers_SO1 | Sewers_O1 | 8775 | 15 |
| Sewers_SO2 | Sewers_O1 | 8846 | 441 |
| Sewers_SO3 | Sewers_O1 | 8917 | 1233 |
| Tunnel_O1 | top | 31068 | 1569 |
| Tunnel_SO1 | Tunnel_O1 | 31125 | none |

Read from the table:

- Three ids of the 27 have an Add and no Complete call in the level script: BrainBug_SO7, Plaza_SO5, Tunnel_SO1.
  Each should complete by cascade when its parent completes (the card 243 Monorail_SO4 pattern); the run's
  `cascade` rows confirm or refute it.
- Eight runtime ids are not in the list of 27: BrainBug_SO2_L1, _L2, _R1, _R2 (children of the count objective
  BrainBug_SO2) and Bridge_SO3a to SO3d (children of Bridge_SO3). The soak verdict counts them: M is 35 unless the
  final dump shows otherwise.
- Bridge_Marker, Plaza_Marker and Sewers_Marker are added (see the table) and have no Complete call, only
  SetObjectiveVisibility: map markers. They are not in the data table's 27; whether the final dump lists them as
  open rows is for the run to show, and they do not count toward M.

Still owed for step 1 (part B): the real trigger of each Complete offset (the event that reaches it), presence
volumes, switches and satchel sites (4 `BP_LevelObjectiveSatchelSIte_C` bound events), and the play order.

## Step 1, part B (senior, 2026-11-17 12:36): the event behind each Add and Complete, static

Method: every function stub of `FED_C4M2_Scripting` enters the ubergraph at one offset; the flow from each entry was
followed (goto, push_flow, conditional goto, direct calls, `CallFunctionWithDelay`, delegate binds) and each Add and
Complete call was assigned to the bound events that reach it. Names are the level's actor names; `#n` is the bound
event number.

Play order and triggers:

1. Start: `ReceiveLevelStart` adds Tunnel_O1 and Tunnel_SO1. Entering `BP_PlayerPresence_C_6` (#3) completes
   Tunnel_O1 (Tunnel_SO1 has no call: cascade) and runs Add Objective 2, 3 and 4: Plaza_O1 + Plaza_SO1 +
   Plaza_Marker, Bridge_O1 + Bridge_SO1 + Bridge_Marker + Bridge_SO3a to SO3d, Sewers_O1 + Sewers_SO0 +
   Sewers_Marker. The three islands are then open in any order.
2. Plaza: `BP_PlayerPresence_C_24` (#27) completes SO1, adds SO2a. `BP_Switch_C_0` (#9) completes SO2a, adds SO2b.
   Encounter end of `BP_EM_1FactionSetWaves_C_39` (#24) or `C_40` (#2) completes SO2b (C_39 adds SO3).
   `C_47` (#106), 0.5 s later: completes SO3, adds SO4. `C_45` (#36), 1.5 s later: completes SO4, adds SO5.
   `BP_PlayerPresence_C_54` (#15) completes Plaza_O1 (SO5 has no call: cascade) and counts one island.
3. Bridge: `BP_PlayerPresence_C_23` (#25) completes SO1, adds SO2. Any of the four `BP_SatchelItemPickup_C_3/5/7/9`
   pickups completes SO2. The four `BP_LevelObjectiveSatchelSIte_C_1/2/3/4` placement events complete SO3a, SO3b,
   SO3c, SO3d (site 1 = a, 2 = b, 3 = c, 4 = d). The `Bridge_SO3_Complete` delegate (bound in BeginPlay) completes
   SO3 and adds SO4. `BP_Switch_C_3` (#83), 3 s later (`BridgeExplode`): completes SO4 and Bridge_O1, counts one
   island.
4. Sewers: `BP_PlayerPresence_C_19` (#1) completes SO0, adds SO1, SO2, SO3. Egg destroyed events count per area:
   SO1 completes at 13 of 17 eggs (area A), SO2 at 22 of 34 (area B), SO3 at 22 of 30 (area C). With all three
   complete, `EggsComplete?` completes Sewers_O1 and counts one island.
5. Brain bug: the third island (`CombatIslands == 3`) starts `SetUpBrainBug` 5 s later, which adds BrainBug_O1 and
   SO1. `BP_PlayerPresence_C_176` (#42) completes SO1, adds SO2 and its four children, sets
   `BugArenaShieldEnabled = true` and runs `Toggle Brain Bug Boundary`. Encounter ends of
   `BP_EM_1FactionSetWaves_C_18` (#45), `C_1` (#1), `C_12` (#3), `C_6` (#1) complete SO2_L1, L2, R1, R2. The level
   objective complete event for BrainBug_SO2 completes SO2 and adds SO3. `Brainbug Damaged` (bound at the brain
   bug's spawn) runs the stage setups: Controlled Troopers completes SO3 and adds SO4; `C_19` (#21), `C_20` (#55),
   `BP_EM_Dropship_C_1` (#135) or `C_7` (#136) complete SO4 (C_19 adds SO5); Centurion setup completes SO5 and
   adds SO6; `C_31` (#48) completes SO6 and adds SO7. `BrainBugDead` completes BrainBug_O1 (SO7 has no call:
   cascade) and ends the level 21 s later.

Debug keys J, K, L, O skip Bridge, Plaza, Sewers and Tunnel; the route does not use them.

Route consequences:

- Presence volumes in order: C_6, then per island C_24 and C_54 (Plaza), C_23 (Bridge), C_19 (Sewers), then C_176.
- Two switches (C_0 Plaza, C_3 Bridge), one satchel pickup, four satchel sites, 57 eggs at least (13 + 22 + 22).
- Encounter-end steps: clearing bugs by code must end the wave managers C_39 or C_40, C_47, C_45 (Plaza) and the
  boss stage managers; each step waits on its objective's log row.
- The brain bug stages are driven by damage to the brain bug, so the boss needs real damage or a damage command;
  this is the part no earlier soak covered.
- The shield's enable branch (hide off, actor collision on, profile BlockAll) runs at C_176: the run's digest
  round after C_176 is the card 237 proof that was still owed.


## Step 2: route written (2026-11-17)

`tools/gen_c245.py <dll hash16>` writes `tools/scenarios/c245-c4m2.txt` (953 steps, passes `drive.py --parse`),
a `voidsoft` discovery scenario in the card 284 objective-driven form. The only table is `tools/c245_eggs.txt`:
the 81 egg actor names by count area (A 17, B 34, C 30), read from the level script's `EggDestroyed` bound events
(each event's ubergraph entry leads to the `EggsA`, `EggsB` or `EggsC` add).

Step forms: presence (expect, go all, wait), switch use (client `key E` from the card 284 use stand, `focus=` gate),
encounter-end holds (`host clear 4000 kill` until the id completes), satchel pickup and four placements, eggs
(client warped 500 uu short, `look ... bounds hit=<egg>`, shot, 2 s of real fire, `ammo fill` every 8 eggs), boss
rounds (client fires 5 s per round at `BP_AI_Brain_Bug_C`, host clears stage encounters by code). Digest rounds at
map start, after each island, after `BP_PlayerPresence_C_176` (card 237 on branch) and at map end.

Needed before the run: `clear <radius> kill` (`c265_clear_kill`, testrun.cpp) kills every enemy-class actor in
radius, so it would kill the Brain Bug by code. The tool must spare actors whose class name contains `Brain_Bug`
and print the spared count in its row. Ordered with the build after b185.

Predictions are in the scenario header. Open in the discovery run: the damage one egg needs, whether the Brain Bug
takes damage before `BrainBug_SO3`, and the stage thresholds of `Brainbug Damaged`.

## Run `c245a` (b186 `6bdc1fb92f415071`, 2026-11-17) and scenario changes
- Ended at 429 s by the client crash of card 479. Completed 5 of 35: Tunnel_O1, Plaza_SO1, Plaza_SO2a, Plaza_SO2b,
  Bridge_SO1. Digest rounds 1-14: 0 mismatches. Host EXCEPTION 0.
- Plaza_SO2b completed about 120 s after its add (the 30-round hold and 20 s wait were too short): the generator now
  holds 60 rounds and waits 30 s.
- Plaza_SO3 is not an encounter end: its actor is `BP_WeaponPickup_FlameThrower_C_0` (take the flamethrower). The
  driver had no kind row (`objective now`: "no kind row for these classes: void"). b187 adds
  `{"WeaponPickup", C126Kind::Use}`; the generator now drives it with the `use` step (stand, look, shot, E).
- Plaza_SO4 "not in the map" and the Bridge_SO2/SO3a VOIDs followed from Plaza_SO3 staying current; not judged yet.
- Next run (b187) predictions: Plaza_SO3 completes within 30 s of the client's E with a shot showing the pickup
  prompt; Plaza_SO4 is then added; no client EXCEPTION past the Bridge satchel warp (card 479).

## Build b187 `920b53a5d0bb7c2e` (2026-11-17)
The WeaponPickup kind row works ("doable (use)"), but `objective go all` found no navmesh stand point around a switch and the look hit `BP_Switch_C_0` 176 uu away, so E was skipped. b188: a pickup target takes its stand point and aim from the pickup actor's bounds. Plaza_SO4/SO5 and Bridge not reached.

## Build b189 `d4181cdc840b944a` (2026-11-17)
Run c245b189a. The pickup stand, aim and E press all work: the host's card050 runs `ASTWeaponPickup::PickupWeapon` on the server copy of `BP_WeaponPickup_FlameThrower_C_0` (class `BP_WeaponPickup_VeteranRifle_C`) and DisablePickup, and the client equips the Veteran rifle. The level then never moves on: no EM_47 or encounter rows, and Plaza_SO3 stays open. The actor name says FlameThrower, but the class is the Veteran rifle; that is the level designer's data, not a mod bug. The level handler `BndEvt__FED_C4M2_Scripting_BP_WeaponPickup_FlameThrower_C_0_..._ADPickupDelegate` takes `PickupPlayer`. The name map of `FED_C4M2_Scripting.umap` (extracted with tools/iostore) also holds `PlayerHasControl`, a Blueprint-only name (not in the exe or in BP_PlayerCharacterMI or BP_WeaponPickup), so the gate cannot be read without a bytecode parser. Next: a run where the host takes the pickup (EM_47 fires means the handler accepts only the host's own player), plus a row at the pickup delegate broadcast naming the bound handlers and the PickupPlayer argument.

## b191 note (2026-11-17 16:16)

The clear fix (`c265_clear_target` also takes live `ControllerTrooper` with health above 0) is proven: Plaza_SO3 and Plaza_SO4 complete. The run now stops at Plaza_SO5: line 318 "host objective wait Plaza_O1 60 -> failed (Plaza_O1: open, current Plaza_SO5)". Plaza_SO5 has no Complete call of its own; it completes by cascade when Plaza_O1 completes, and Plaza_O1 is completed by `BP_PlayerPresence_C_54` (#15). `objective go all` placed both players in a presence at (-21876, 12238, 2850) four times with no completion, and the "nearest presence" row prints only the class, so it is not known whether that presence is C_54. A dropship wave (BP_EM_Dropship_C, ControlledTroopers_Medium) left survivors ("2 left", "5 left"). Objectives completed: 7 of 18. Card 479 sites (satchels, BridgeExplode) are still not reached.

Next (b192): print the instance name in the "nearest presence" row, log the C_54 enter at run time to find its gate, then fix the Plaza_SO5 step.

## b192 prediction (junior, written before the run, 2026-11-17)

New rows: `card245: <side> ... presence <instance> begin overlap by <actor> ...: gate <Gn|passed>` for every player-body
overlap of an `ASTPlayerPresence` (gates read from `OnBeginOverlap` 0x050F6880: G1 enabled byte +0x30A, G2 byte +0x5D
& 0x18 == 0x10, G3 byte +0x2C0, G4 `IsLoadInProgress`, G5 other component garbage, G6 other component byte +0x97 bit
0x20), then a `card245: ... OnTriggerEntered by ...: <outcome>` row (card 220 refused, client closure, card 227 held,
original runs). The "nearest presence" row now prints the instance name and the enabled byte.

New Plaza_SO5 step: up to 10 rounds of `clear 4000 kill` plus a 3 s wait for Plaza_O1, then both players warped into
`BP_PlayerPresence_C_54` by name (two more rounds after that).

Predictions:
1. While the SO5 dropship encounter has survivors, the overlap rows for `BP_PlayerPresence_C_54` say `gate G1 enabled
   byte 0`; the level script enables C_54 when the encounter ends. After the clears, a C_54 row says `gate passed`,
   its OnTriggerEntered row says "original runs", and Plaza_O1 completes (Plaza_SO5 by cascade).
2. If the b191 go spot (-21876, 12238, 2850) is not C_54, the nearest presence row names another instance; the C_54
   warp still completes Plaza_O1.
3. The run then reaches Bridge_SO2 (satchel pickup), the four Bridge_SO3 sites (card 479) and BridgeExplode
   (Bridge_SO4, BP_Switch_C_3).
Failure reading: a C_54 row with `gate passed` and "held by the card 227 gate" points at the gate's living-player
count; a row with G2 to G6 names a gate this prediction does not cover.

## b192 note (2026-11-17 16:37)

The OnBeginOverlap hook (ASTPlayerPresence::OnBeginOverlap, RVA 0x050F6880, gates G1-G6) showed that C_54 passes every gate and completes Plaza_O1 once both players stand on it. The G1 prediction was wrong; b191's `go` placed the players on a different presence. Bridge_SO1 is done. Bridge_SO2 is blocked: the client's E on BP_SatchelItemPickup_C_9 reached the host (card050 match, AddInventoryItem satchel, DisablePickup, 3 OnPickup bindings including the level script's BndEvt ... ActorBoundEvent_56, PlayerHasControl true, PickupPlayer -), but SO2 never completed. The scenario also used a wrong site class; it is fixed to BP_LevelObjectiveSatchelSIte_C. Next: run the host taking the satchel. If SO2 completes then, the client pickup is a co-op bug in the level script's handler (PickupPlayer / player 0, the same question as b189).

## b192 run 3 prediction (junior, written before the run, 2026-11-17)

Change: the host takes the satchel and places it. The Bridge_SO2 step warps the host to `BP_SatchelItemPickup_C`,
walks it in (goto 100), looks at it and presses E; the client is warped 500 from the same target. Each Bridge_SO3a-d step warps
the host to `BP_LevelObjectiveSatchelSIte_C_1/2/3/4` by name, looks at that site and holds E for 3500 ms. If
Bridge_SO2 is still open after the pickup, one extra step places at site C_1 and waits 20 s for Bridge_SO2, so the
placement rows are in the log either way.

Predictions:
1. The host pickup row reads `card245: host ... pickup BP_SatchelItemPickup_C_<n> ... taker <host pawn> ...
   PlayerHasControl true, PickupPlayer <host pawn>` (not `-`), and Bridge_SO2 completes within 15 s of the E. That
   would make the b192 client pickup a co-op bug: the level script's ActorBoundEvent_56 handler reads PickupPlayer or
   player 0, and the client's pickup sets neither.
2. The four placements complete Bridge_SO3a to SO3d (site 1 = a ... 4 = d), the `Bridge_SO3_Complete` delegate
   completes Bridge_SO3, `BP_Switch_C_3` (the client's E) completes Bridge_SO4 and, 3 s later (BridgeExplode),
   Bridge_O1.
Failure reading: if Bridge_SO2 stays open after the host pickup, the gate is not the taker. Then the extra
placement step shows whether SO2 completes at placement, and its rows (card050 / card245 / card049) say what the
placement did.

## b192 run 3 note (junior, c245b192c, 2026-11-17)

- Prediction refuted. Host took BP_SatchelItemPickup_C_9 at 594379986 (Interact target logged, DisablePickup ran, OnPickup bindings 3 incl. FED_C4M2_Scripting_C BndEvt ActorBoundEvent_56). The card245 "after the base call" row still reads `taker null (other); level PlayerHasControl true, PickupPlayer -`. Bridge_SO2 did not complete in 15 s.
- So the taker identity is not the gate: the host pickup also leaves PickupPlayer empty and no SO2 completion. Gate is inside the ActorBoundEvent_56 handler or what it reads (the bound event fires with a taker our row reads as null).
- Placement at site C_1 (594395720): card456 hold started, MS_WPN_SatchelCharge_Prime, at 594398206 CompleteSubObjective(Bridge_SO3, Bridge_SO3a), StartEncounter BP_EM_1FactionSetWaves_C, Cancel sound, hold released completed 1. Bridge_SO2 still open after placement (wait 20 failed).
- Sites C_2/C_3/C_4: Interact on the site logged, hold released `completed 0`. Likely one satchel per pickup; the scenario needs a pickup before each site. Not verified.
- Bridge_SO3 1/4, Bridge_SO4 never added; Sewers onward blocked (current objective Bridge_SO3d).
- 0 EXCEPTION/FATAL both logs, drive rc 3, WARN 199 host / 168 client, no dumps.

## Bridge_SO2 cause (junior, static, 2026-11-17)

- Correction to the run 3 note: "taker null" in the "after" row is GetOverlappingCharacter after DisablePickup turned the collider off; the "before" row names the host pawn. "PickupPlayer -" means the level class has no such variable. Neither is a null player argument.
- (1) The OnPickup argument is the pickup itself. ASTItemPickup::Pickup_Implementation (0x51D3EF0) reads GetOverlappingCharacter, adds the item to that character's inventory, then calls AADPickupBase::Pickup_Implementation (0x4C61DC0), which runs DisablePickup and OnPickup.Broadcast(this) (0x4C61EED, delegate at +0x370, one argument). The level handler's parameter is `Pickup` (BndEvt ..._C_9_..._ActorBoundEvent_56 stores `K2Node_ActorBoundEvent_Pickup_5 = Pickup` and enters the ubergraph at 19796).
- (2) The driver's E is the real path: IA_Interact press, InputTag.Use, UADInteractorComponent::Interact on BP_SatchelItemPickup_C, same as a key press.
- (3) Nothing in the mod nulls anything. Cause: the four pickup handlers (C_3/5/7/9, entries 19641..19796) all jump to ubergraph 0x4cb9: `Satchels = Satchels + 1; if Satchels == 4 { CompleteSubObjective(Bridge_O1, Bridge_SO2); BridgeSatchelSitesReady = true; ... }`. Bridge_SO2 needs all four pickups. The play-order trace step 3 ("any of the four") was wrong. Not a co-op bug.
- Fix (scenario): gen_c245 takes C_3, C_5, C_7, C_9 in turn (host), then waits for Bridge_SO2, then places at sites 1-4. The card245 pickup row now prints the level's `Satchels` instead of `PickupPlayer`.
- Prediction for the b193 run: the four pickup rows read `Satchels 0..3` before and `1..4` after the base call; Bridge_SO2 completes within 1 s of the fourth pickup; sites 1-4 complete SO3a-d (one carried satchel each; the inventory holding four is assumed from the level design, not checked), then Bridge_SO3, SO4 via switch C_3, Bridge_O1.

## b193 prediction (junior, written before the run, 2026-11-17; dll 870c5d84c1a9fec4)

Scenario change (senior order): co-op split. Client takes BP_SatchelItemPickup_C_3 and C_5, host takes C_7 and C_9; client places at sites 1 and 2, host at sites 3 and 4; client presses switch C_3.
- The host's card245 pickup rows read `Satchels 1`, `2`, `3`, `4` after the base call, across both players' takes (client takes reach the host's server copy).
- Bridge_SO2 completes within 1 s of the 4th pickup (host C_9).
- Each placement logs card456 `hold released ... completed 1` with the placer's own satchels: client sites 1-2, host sites 3-4. This answers the inventory question (one player carries two).
- If a client placement logs `completed 0` while the host's pass, that is a co-op bug (satchel in the server copy's inventory but not the client's, or the reverse): card it, do not chase in this build.
- Then Bridge_SO3, Bridge_SO4 via client E on BP_Switch_C_3, Bridge_O1.

## b193 result (junior, 2026-11-17; run c245b193a, dll 870c5d84c1a9fec4)

Run: ended after 361 s (drive.py killed at line 414, blocked behind Bridge_SO4); host rc 0, client rc 0,
FATAL/ERROR 0, EXCEPTION/FATAL 0 both logs, WARN 70 host / 116 client, no crash dumps, detach 0.
Plaza VOIDs (soft clears 119, 275-281), line 64 / 200 / 306 fails: the same as b192c (baseline).

Predictions:
- Satchels 1..4 across both players: HELD. Client C_3 -> Satchels 1, client C_5 -> 2, host C_7 -> 3, host C_9 -> 4
  (drive lines 325, 331, 339, 347).
- SO2 within 1 s of the 4th pickup: HELD. 4th pickup host t=596066808, client replayed
  CompleteSubObjective(Bridge_O1, Bridge_SO2) t=596066819 (11 ms).
- Each placer's placement completed 1: HELD. Client sites 1, 2: hold released completed 1 (client log 12845,
  12957); host sites 3, 4: completed 1 (host log 14138, 14376). Bridge_SO3 replayed on the client with count 4.
  So the inventory question is answered: each player places with its own satchels. No co-op bug.
- SO4 (client presses BP_Switch_C_3): FAILED, not a co-op bug; a stand-point bug in c284_use_stand.
  `c284: use stand BP_Switch_C: yaw 199 uu yaw+180 100 uu ... switch 100 uu; winner switch 100 uu (nearest)`.
  The nearest try wins, so the client stands on the panel's back side (eye -34396 -33180, switch centre
  -34257 -33150). Client aim scan hit 1 of 26 bounds points, the camera ray passed through to Actor_0 at
  4140 uu, interactor focus none; key E skipped by the look gate. b192c's C_3 look also voided (different
  stand). Fix proposal (not made): in c284_use_stand rank the face-side try (yaw) first when it projects within
  use reach, nearest only as the fallback. The scenario's `c284: use stand BP_Switch_C_3:` regex also never
  matches: the row prints object_name `BP_Switch_C`.
- Bridge_O1 and every later objective: not reached.

## b194 prediction (junior, 2026-11-17; written before run c245b194a, dll a119f90574be9a2d)

Change: `c284_use_stand` gives a switch's root yaw try (its face side) the win whenever it projects within
kC284UseReachUu = 250 uu (the interactor trace length); nearest stays the fallback; weapon pickups keep nearest.
Scenario: the use() wait regex now matches the object_name form (`BP_Switch_C`) and runs since-mark after a `host mark`.

1. Bridge_SO4 stand row: `use stand BP_Switch_C: ... winner yaw 199 uu (face side within reach)`.
2. The client's aim scan on BP_Switch_C_3 hits more than 13 of 26 bounds points; focus is BP_Switch_C_3.
3. Key E completes Bridge_SO4; BridgeExplode (card 479) logs its rows; Bridge_O1 completes about 3 s after SO4.
4. Plaza_SO2a (BP_Switch_C_0) still passes (yaw 196 ties switch 196, yaw now wins: same side). Plaza_SO3 (pickup)
   unchanged (nearest).
5. Then Sewers and BrainBug as far as the run goes. 0 EXCEPTION/FATAL on both logs.

## b194 result (junior, 2026-11-17; run c245b194a, dll a119f90574be9a2d)

Ended by the scenario after 688 s (drive VOID, 75 void lines), rc 0/0. FATAL/ERROR 0, EXCEPTION/FATAL 0 on both logs,
WARN 189 host / 244 client, no crash dumps. 17 unique objective completions sent (Tunnel_O1, Plaza_O1 + SO1..SO4,
Bridge_O1 + SO1..SO4 + SO3a..d, Sewers_SO0). Digest: 21 rounds at 0, 6 rounds Pose-only (1 or 4), same class as b193.

1. HELD. `use stand BP_Switch_C: yaw 199 uu yaw+180 100 uu +x 200 uu +y 135 uu -x 103 uu -y 139 uu switch 100 uu;
   winner yaw 199 uu (face side within reach)`. The since-mark regex matched it.
2. PARTLY. Aim scan `BP_Switch_C_3 hits 2 of 26`, not more than 13; the focus sweep found it ("traced choice
   missed"), aim hit at 172 uu, `focus BP_Switch_C_3`, Use press sent with interactor target BP_Switch_C_3.
3. HELD for SO4, MISSED for the +3 s: Bridge_SO4 at host t=596732169, 3.0 s after the client's press; Bridge_O1
   completed in the same ms (cascade, no 3 s gap). Card 479: 0 card479 rows on both sides; the client lived through
   the bridge and on into Sewers (no corpse-arrival crash).
4. HELD. Plaza_SO2a and Plaza_SO3 completed as in b193.
5. Sewers stalled: Sewers_SO0 done; SO1/SO2/SO3 open at the end (current Sewers_SO3). BrainBug not reached
   (BrainBug_O1 not in the map). Unique eggs killed per area: A 6 of 17 (needs 13), B 13 of 34 (needs 22),
   C 16 of 30 (needs 22).
   - One VeteranRifle round kills an egg (Health 200.0 -> -50.0); the client fires 1 round per 2 s press.
   - 25 egg looks VOID: the aim hit Landscape_1 (28 tries), Actor_7 (10) or Actor_1 (4) before the egg; the stand
     points have no line of sight.
   - 22 eggs with an ok look and a fired round took no damage (no Health row). The host aimed at the egg point
     (e.g. BP_BugEggPile_Destructible_01_C_0: target 5063 -2490 882, aim hit 5063 -2491 882). By class:
     BP_DestructibleEggs_GlowingEmissive_Base_C 0 of 7 killed (possibly immune to rifle fire); the rest are mixed.

## b195 result, run c245b194b (b194 `a119f90574be9a2d`, eggs by code)
- 640 s, rc 0/0, 0 EXCEPTION/FATAL, drive rc 3 (VOID: 8 void lines, Plaza clears at 124 and 275-287; 4 failed: 202 Plaza_SO3 wait, 308 digest, 620 BrainBug_SO2, 792 BrainBug_O1).
- 25 unique completions (b194: 17). Sewers SO0, SO1, SO2, SO3 and O1 all complete. `host kill actor` went through InstaKillTarget 56 times (`after InstaKillTarget health -1.0 dead -1` each), plus the card 480 egg by fire: area A 12 + 1, B 22, C 22, matching the 13/22/22 thresholds exactly.
- Brain bug: SO1 complete, SO2 children R1 (+3 s), L1 (+96 s), L2 (+115 s) complete; R2 (encounter end of BP_EM_1FactionSetWaves_C_6, ActorReference_C_34) never completed under 4000 uu and 3000 uu clears at the arena, so SO2 stayed 3/4 and BrainBug_SO3 onward never started.
- Run c245b194c (same build): the BrainBug_SO2 hold goes to the live current objective (`host objective go all`) and clears 60000 uu each round.

## b195 result, run c245b194c (b194, SO2 hold with go)
- 585 s, rc 0/0, 0 EXCEPTION/FATAL, drive rc 3. 25 unique completions again; R1, L1, L2 complete, R2 open.
- My error: the 30 `clear 60000 kill` lines voided (`clear takes <radius> (100-20000)`), so the hold only moved players. `host objective go all` put both at the R2 marker (16357, -40770, 4777); 26 done, 12 failed on "moved N uu after the warp". R2 still did not end in about 80 s there without a clear.
- gen_c245.py now uses radius 20000 for that hold (30 lines, regenerated for `a119f90574be9a2d`). Not yet run.

## Next-batch prediction (run c245b194d, b194, `clear 20000` at BrainBug_SO2)
- The wave encounter BP_EM_1FactionSetWaves_C_6 ends only when its spawned waves are dead. The 20000 uu clears after `objective go all` kill them, so BrainBug_SO2_R2 completes, then SO2, and SO3 is added.
- If R2 stays open while the clear rows read 0 left, C_6's end waits on something else: its wave count, or a spawner that never fires. Trace that from the rows.

## Result c245b194d (b194 a119f90574be9a2d, 2026-11-17 18:05)

548 s, host rc 0, client rc 0, 0 EXCEPTION/FATAL, no crash dumps. Drive rc 3: VOID, 21 void lines, 2 failed.

- Prediction held: `clear 20000 kill` at BrainBug_SO2_R2 (after `objective go all`) killed 2 + 2, 0 left;
  `CompleteSubObjective(BrainBug_SO2, BrainBug_SO2_R2)` at 599425118. C_6 ends when its waves are dead.
- Whole map complete: 32 unique completions, every O1 (Sewers, Tunnel, Bridge, Plaza, BrainBug) and every SO,
  BrainBug_SO1..SO6 and `CompleteObjective(BrainBug_O1)` at 599629152.
- Void lines are all `clear` lines with bugs left over max-left=0 while a wave fight runs (Plaza_SO2b line 164,
  Plaza_O1 277-287, BrainBug_O1 boss arena 691-742: 1 to 7 left per clear). Failed: line 202 (Plaza_SO3 wait 30 s;
  it completes later) and 308 (digest wait cut to 1 s by the dead-wait cap after the failure).
- Digest: MoveableRoad (`BP_DestroyableActor_C4M2_MoveableRoad_C`) 2 to 3 mismatches in 9 rounds (also in
  c245b194b 15, c245b194c 13); Pose 1 to 3 in 3 rounds.
- Generator change for the next run: clears inside a wave hold should allow bugs left (max-left) or not void,
  since new waves spawn during the hold.

## Closed (2026-11-17 18:21)

c245b194d (b194 `a119f90574be9a2d`): the whole map completes in co-op, with 32 unique completions. Every O1 (Tunnel, Plaza, Bridge, Sewers, BrainBug) and every SO completed, ending with `CompleteObjective(BrainBug_O1)` at 599629152. 0 EXCEPTION/FATAL on both sides. The proofs found on the way:
- Plaza_O1 completes on C_54.
- Bridge_SO2 completes at Satchels == 4 across both players' pickups, and each player places its own satchels.
- The switch is pressed by the client.
- Sewers completes by egg counts.
- BrainBug_SO2_R2 completes at its wave end.
Real fixes the card produced: the b188 pickup stand, the b191 clear of hostile ControllerTrooper, and c284 face-side stands (b194).

Not done:
- The FED_C5M1 travel and the frame windows were not checked in this run.
- The 3 no-call ids (BrainBug_SO7, Plaza_SO5, Tunnel_SO1) were counted by cascade, not by a dump row of M of M.
- The digest MoveableRoad mismatches are carded as 481.
- The generator still voids clears inside wave holds. That is a test fault; it is a one-line STATE item for the next c245 edit.

Reopen when: a C4M2 objective stays open in co-op that completes in the same scenario solo, or a C4M2 run shows a client and host objective list that differ.

### c245b212a SO4 slowdown (b213 review)

- SO4 took 165 s and 259 hits in c245b212a, against 17 s and 162 hits in c245b204a. SO4 is the Archer phase (level script
  @16279-16768): each of BP_EM_1FactionLoop_C_6/4/2/1 ending its encounter turns a searchlight off and adds 1 to
  Archers Completed; at 4 the script runs End Archer Phase, adds SO5 and completes SO4. Boss health stayed flat at
  4458 through SO4 in both runs.
- Not b212 mod code: detour_c301_exec_enable fired once in c245b212a (C_176, before SO3), never in the SO4 window,
  and no wave started twice.
- Cause, scenario placement: `host clear 3000 kill` is centred on the host pawn. In c245b204a the host stood at about
  (16512,-41327), 5000 uu from the boss, and each round killed 1-13. In c245b212a an extra hold-loop `objective go`
  (SO1 completed early through gap G) left the host at the SO2 marker (5254,-41821), about 15500 uu from the boss
  (20544,-44432); rounds killed 0-4, so the archer encounters ended slowly.
- b213 fix: the boss rounds clear at 20000 uu, the hold's radius (gen_c245.py); the Brain Bug is spared by class.
- The same host position put the host's pawn past the 15000 uu net cull for the client: card098 destroyed the client's
  copy, and the digest showed 3 host-only PlayerPawn entries per round. b213 leaves a pawn out of the digest while a
  client has no channel for it (card 323).

### c245b213a result (b213 57aacb585ebdd2f1)
- SO4 added -> SO5 added 11.1 s (b212 165 s) with boss clear 20000. 32 unique completes, same set as b204.
- VOID 3 soft lines: 549 (SO1 completed before expect, generator fixed), 663 ("no such actor" BP_AI_BrainBug_C on first
  boss round), 696 (aim hit Actor_7, not the boss). 0 EXCEPTION/FATAL, no dumps, 0 card486 rows.
- Digest: 20 rounds 0 mismatches, 1 transient Score:1 round.
