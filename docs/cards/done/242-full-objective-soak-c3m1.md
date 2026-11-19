# 242: Full-objective soak, FED_C3M1

Same shape and rules as card 240 (read `docs/cards/done/240-*.md` or `docs/cards/240-*.md` first: no
`objective complete`, wait on the host Complete row and the client replay, both players in presence volumes,
objectives dumps at the end, faults become their own cards).

Goal (one): one scenario that plays FED_C3M1 from spawn to the mission end and completes every objective by
its own trigger.

Objective list M = 19, from the game's data table `DT_LevelObjectiveData_FED_C3M1` (keys
`FED_C3M1_<id>_NAME`; the runtime ids may differ in case):
BEACH_O1
BEACH_SO1
BEACH_SO2
BEACH_SO3
BEACH_SO4
BEACH_SO5
MONORAIL_O1
MONORAIL_SO1
MONORAIL_SO2
MONORAIL_SO3
RESORT_O1
RESORT_SO1
RESORT_SO2
RESORT_SO3
RESORT_SO4
SATCOM_O1
SATCOM_SO1
SATCOM_SO2
SATCOM_SO3
The last soak (c229-c3m1) sent 0 sub-objective completions: none

Step 1, static, no run, appended here as a table: each id in play order with parent, counter, real trigger
with level-script or class evidence (`FED_C3M1_Scripting`), the driver steps that fire it, the synced
follow-ups to wait on, and any id that is a marker only or on a branch the map never activates (state the
evidence; such an id leaves M only with the senior's agreement).

Step 1 also covers, from card 241 (9 runs lost to these):

- Every objective that ends on an encounter: the encounter manager chain in order, each manager's start
  trigger and end condition (kill count, remaining percent, boss), spawner positions, and the log line that
  marks its start and end. The scenario drives kills at those spawners and waits on those lines.
- Every presence or hold volume: actor name, position and distance from the objective marker (a marker is
  not the trigger; card 257).
- The mission end: what completes the last top objective (card 241: boarding the landed dropship), the delay
  to `EndLevel`, and the level-end widget steps.
- `kill actor` skips dead actors since card 259 and logs guid, health and dead flag per kill.
- The driver stops at 2000 steps: generate long kill loops (as `gen_c241.py`) and count the steps before
  the run.

- 241 run 10 had 10 driver FAIL lines of one kind: `unless host log "<regex with capture groups>" ... when
  host last ... {g1}` failed with "could not convert string to float" once the `unless` pattern had matched.
  Step 1 states from `tools/drive.py` whether a matched `unless` should have skipped the step (driver
  fault: report, then fix in `tools/drive.py` only with the senior's go) or the `unless` groups shadow the
  `when` groups (scenario fault: use `(?:...)` in `unless` patterns). No such FAIL line in a 242 run.
- The client may get no `WB_TrooperLevelEnd` (card 260). Step 1 states for C3M1 whether the mission end
  runs on the client by itself. The client wait on it is a `try` step; report the result, do not fix.

Step 2: `tools/scenarios/c242-c3m1.txt` (new file; older scenarios stay).

Step 3: run, budget 3 runs. Command:
`systemd-run --user --scope -q -p MemoryMax=12G env SCENARIO=tools/scenarios/c242-c3m1.txt DEPLOY=1 FAST=1 HOST_EXTRA_ENV="UBWCOOP_TRAVEL=/Game/Maps/FED/FED_C3M1?listen UBWCOOP_GOD=1 UBWCOOP_C132_FX=1 UBWCOOP_C136_LOG=1" CLIENT_EXTRA_ENV="UBWCOOP_C132_FX=1 UBWCOOP_C136_LOG=1" tools/run_pair.sh c242-c3m1 3600`

Files allowed: `tools/scenarios/c242-c3m1.txt`, this card. Driver verbs only after a report to the senior.
No other mod file. No build while a game runs.

Done when: final objectives dump shows `complete 1` for M of M on host and client, lists match, both sides
reach the next map (FED_C3M2), 0 crash, digest mismatches only of carded classes (251, 253, 254, 256, 258), frame
windows at or over 500 outside load. Report: completed N of M, each fault with its lines, new WARN classes
against c229-c3m1.

## Step 1 result (static, junior, 2026-11-13)

Sources: the `FED_C3M1_Scripting` ubergraph listing (offsets below are `ExecuteUbergraph_FED_C3M1_Scripting`
statement offsets), the class listings of `BP_EM_1FactionSetWaves`, `BP_PlayerTransportDropship` and
`BP_SatchelBuilding_Flakbug`, and the c229-c3m1 host log (card070 adds). The c229 card070 lines confirm the runtime
names: Beach_SO1_A/B/C = `BP_Switch_C_4` / `BP_Switch_C_0` / `BP_Switch_C_6`, Resort_SO3_A/B/C =
`BP_SatchelBuilding_Flakbug_C_0` / `_C_2` / `_C_1`, Beach_SO2 = `ActorReference_C_12`, Beach_Marker1/2 =
`ActorReference_C_1` / `_C_2`, Resort_Marker = `ActorReference_C_0`, Monorail_Marker = `ActorReference_C_5`.

### Start

The FAST start runs the `BP_PlayerTransportDropship_C_3` DropshipEventDelegate (5405). It adds Beach_O1 with
Beach_Marker1, Beach_Marker2, Beach_SO1 and Beach_SO2; Resort_O1 with Resort_Marker, Resort_SO1 and Resort_SO3;
Monorail_O1 with Monorail_Marker and Monorail_SO1. It also triggers EM `SetWaves_C_12` (ambient). c229 logged all of
these adds at t0. Satcom_O1 is added later, when the other three top objectives are done.

### Table (play order)

| # | Id | Parent, counter | Real trigger (evidence) | Driver steps | Synced follow-up to wait on |
|---|---|---|---|---|---|
| 1 | Beach_SO1 | Beach_O1, count 3 (A/B/C) | Each child completes when its switch is pressed: `BP_Switch_C_4` (9746, closes ramp doors), `BP_Switch_C_0` (21758, closes Gigantic_C_1), `BP_Switch_C_6` (9894, closes Gigantic_C_2). The parent auto-completes at 3 of 3, as LOS_O03_SO03 did in c241. | `use()` per switch: warp short, look, E; wait on the child's Complete row. | Host and client Complete rows for Beach_SO1_A/B/C, then Beach_SO1 (card097 cascade line); then the card049 Add row for Beach_SO3. |
| 2 | Beach_SO3 | Beach_O1 | OnLevelObjectiveComplete(Beach_SO1) at 9968 adds Beach_SO3 (marker `ActorReference_C_10`) and triggers EM `SetWaves_C_4` and `SetWaves_C_17`. The C_4 EncounterDelegate (16414) waits 2.0 s ('Tanker spawn delay'), then adds Beach_SO4 and completes Beach_SO3. | Kill rounds at `ActorReference_C_10` (-52103 -19810 2880), gated on the Beach_SO3 row. | Beach_SO3 Complete row; Beach_SO4 Add row about 2 s before it. |
| 3 | Beach_SO4 | Beach_O1 | The Beach_SO4 add (6128) waits 1.0 s, then triggers EM `SetWaves_C_7` (tanker), destroys the floor and shows the tanker hole. The C_7 EncounterDelegate (15985) completes Beach_SO4 and Beach_Marker1, opens the doors and calls Beach Substage completed (13642). | Kill rounds with `BP_AI_TankerBug_C` first, then `BP_AI_WarriorBug_C`, gated on the Beach_SO4 row. | Beach_SO4 and Beach_Marker1 Complete rows. |
| 4 | Beach_SO2 | Beach_O1 | Picking up `BP_WeaponPickup_EPulse88_C_1` (21699 → 18585): adds Beach_SO5 (marker `ActorReference_C_2`), completes Beach_SO2 and triggers EM `SetWaves_C_3`. The `_C_0` pickup only sets EPulsePickedUp (18795). | `use('BP_WeaponPickup_EPulse88_C_1')`. | Beach_SO2 Complete row and Beach_SO5 Add row. |
| 5 | Beach_SO5 | Beach_O1 | The EM `SetWaves_C_3` EncounterDelegate (16373) completes Beach_SO5 and Beach_Marker2 and calls Beach Substage completed. | Kill rounds at `ActorReference_C_2` (-25980 -9660 1090), gated on the Beach_SO5 row. | Beach_SO5 Complete row. |
| 6 | Beach_O1 | top | Beach Substage completed: beachCount reaches 2 → CompleteObjective(Beach_O1), then Add to objective count (972). | None (follows rows 3 and 5). | Beach_O1 Complete row. |
| 7 | Resort_SO1 | Resort_O1 | BndEvt `BP_PlayerPresence_C_8` enter (7447): adds Resort_SO2 (marker `ActorReference_C_9`), then completes Resort_SO1. | `presence('BP_PlayerPresence_C_8')`. | Resort_SO1 Complete row and Resort_SO2 Add row. |
| 8 | Resort_SO2 | Resort_O1 | Picking up `BP_SatchelItemPickup_C_1`, `_C_2` and `_C_3`. At satchelCount 3 (16734) it calls Callout Resort_SO3 (flakbugsReady, Spawner_24), then completes Resort_SO2. | `use()` per pickup. | Resort_SO2 Complete row. |
| 9 | Resort_SO3 | Resort_O1, count 3 (A/B/C) | Each child completes on its flakbug building's BuildingDestroyed after the satchel hold: A `BP_SatchelBuilding_Flakbug_C_0`, B `_C_2`, C `_C_1`. The parent auto-completes. OnLevelObjectiveComplete(Resort_SO3) (1735) adds Resort_SO4 (count 2: A = `ActorReference_C_3`, B = `ActorReference_C_4`), sets dropshipsReady and enables presence `C_16` and `C_22`. | Per building: warp to the building by name, look, `host key E 3200 timeout=20` (card 241 hold); wait on the child row. | Resort_SO3_A/B/C rows, Resort_SO3, then the Resort_SO4 Add row. |
| 10 | Resort_SO4 | Resort_O1, count 2 (A/B) | A: `BP_PlayerPresence_C_22` enter (14839, needs dropshipsReady) triggers EM `SetWaves_C_2`. Its EncounterDelegate (16027 → 15838) completes Resort_SO4_A. B: `BP_PlayerPresence_C_16` enter (14887) triggers EM `SetWaves_C_5`. Its EncounterDelegate (15956 → 15897) completes Resort_SO4_B. At count 2 (2615) Resort_Marker and Resort_O1 complete. | Presence on C_22, then kill rounds gated on Resort_SO4_A; presence on C_16, then kill rounds gated on Resort_SO4_B. | Resort_SO4_A, Resort_SO4_B, Resort_SO4 (cascade) and Resort_O1 rows. |
| 11 | Resort_O1 | top | See row 10 (2615), then Add to objective count. | None. | Resort_O1 Complete row. |
| 12 | Monorail_SO1 | Monorail_O1 | `BP_PlayerPresence_C_4` enter (13183 → 5773): adds Monorail_SO2 (target `BP_PanelSwitch_WithoutScreen_C_1`), completes Monorail_SO1. | `presence('BP_PlayerPresence_C_4')`. | Monorail_SO1 row, Monorail_SO2 Add row. |
| 13 | Monorail_SO2 | Monorail_O1 | Pressing `BP_PanelSwitch_WithoutScreen_C_1` (17695 → 11156): Finale Bug Fight (Loop_6; after 35 s '2nd wave'; triggers EM `SetWaves_C_15`), adds Monorail_SO3 (marker `ActorReference_C_5`), completes Monorail_SO2. | `use('BP_PanelSwitch_WithoutScreen_C_1')`. | Monorail_SO2 row, Monorail_SO3 Add row. |
| 14 | Monorail_SO3 | Monorail_O1 | No explicit Complete in the script. It completes by the parent cascade: CompleteObjective(Monorail_O1) completes open children (c241-c2m1-r10: Plasma_Marker completed in the same ms as LOS_O02, with no call of its own). | None beyond row 15. | card097 ObjectiveComplete(Monorail_SO3) in the same ms as Monorail_O1. |
| 15 | Monorail_O1 | top | '2nd wave' (11269) needs inMonorailArea and triggers EM `SetWaves_C_19`; 20 s later 'Final Wave' (11001) needs inMonorailArea and triggers EM `SetWaves_C_0`. Each retries every 20 s / 15 s while false. inMonorailArea is set by `BP_PlayerPresence_C_19` enter (11531) and cleared by its exit (11751). The C_0 EncounterDelegate (16219) completes Monorail_O1 (4751), starts the monorails and calls Add to objective count. | `presence('BP_PlayerPresence_C_19')` for both players, then kill rounds inside the volume only (no warps out), gated on the Monorail_O1 row. | Monorail_O1 row (plus Monorail_SO3 and Monorail_Marker cascade lines). |
| 16 | Satcom_SO1 | Satcom_O1 | Add to objective count reaching 3 (972) adds Satcom_O1 + Satcom_Marker (`BP_Radar_C_0`) and Satcom_SO1 (3243, target `BP_Switch_C_3`, made pressable). Pressing `BP_Switch_C_3` (13600 → 3504) adds Satcom_SO2 (marker `ActorReference_C_11`), completes Satcom_SO1, and after 0.5 + 1.0 s triggers EM `SetWaves_C_9` and `C_14` and HordeZone_C_1 KillEmAll. | Wait for the Satcom_SO1 Add row, then `use('BP_Switch_C_3')`. | Satcom_O1 / Satcom_SO1 Add rows, Satcom_SO1 Complete row, Satcom_SO2 Add row. |
| 17 | Satcom_SO2 | Satcom_O1 | The EM `SetWaves_C_9` EncounterDelegate (13188) cleans up C_9, C_14 and Loop_7, then (6344) starts `BP_PlayerTransportDropship_C_1` StartAction(0,0,0), adds Satcom_SO3 (target dropship C_1) and completes Satcom_SO2. | Kill rounds at `ActorReference_C_11` (-69827 -42524 1064), gated on the Satcom_SO2 row. | Satcom_SO2 row, Satcom_SO3 Add row. |
| 18 | Satcom_SO3 | Satcom_O1 | Dropship C_1 lands; its DropshipEventDelegate (18480) sets SetIsInteractable(true). PlayerEntering on C_1 (18514 → 4448) completes Satcom_SO3 and Satcom_O1, starts StartAction(1,1,2) and calls CallFunctionWithDelay(13.0, 'End the level') → STGameInstance:EndLevel (12337). | Card 241 BOARD shape: wait on the C_1 DescendToLandCompleted collision line, warp beside the ship, E; then client E. | Satcom_SO3 and Satcom_O1 rows; host `card018 ... WB_TrooperLevelEnd` within about 13 s. |
| 19 | Satcom_O1 | top | Row 18. | None. | Satcom_O1 row. |

Not in M: Beach_Marker1/2, Resort_Marker, Monorail_Marker, Satcom_Marker (markers), and the lettered children
Beach_SO1_A/B/C, Resort_SO3_A/B/C, Resort_SO4_A/B (counters of their parents). Monorail_SO4 has visibility calls in
the script but no AddObjective, so it is never added. No id of M is a marker only or on a branch the map never
activates: no id needs to leave M.

Debug key events (InpActEvt K/Up/Right/Delete/O/N) jump to objective paths. The scenario never presses them.

### Encounters

`BP_EM_1FactionSetWaves` moves wave to wave on a per-wave trigger from its `WaveRemaining%Trigger` array: remaining
percent, boss remaining health, bosses remaining, or time (SetupTriggerReason). The encounter ends natively after
the last wave; EncounterDelegate is that end. The mod logs each wave start (card111 `wave budget ... caller
exe+0x512940C`, then card112 `activate`). Those lines do not name the manager, so the scenario cannot tell one
encounter's waves from another's. It does not wait on them. Each encounter's end is marked by the Complete row its
delegate sends:

| Encounter | Started by | Spawner (first ref) | Ended by → row |
|---|---|---|---|
| SetWaves_C_4 | Beach_SO1 complete | `BP_AISpawner_C_31` | Beach_SO3 Complete (+2 s) |
| SetWaves_C_7 (tanker) | Beach_SO4 add + 1 s | `STAISpawner_4` | Beach_SO4 Complete |
| SetWaves_C_3 | Beach_SO2 complete | `BP_AISpawner_C_0` | Beach_SO5 Complete |
| SetWaves_C_2 | presence C_22 (after Resort_SO3) | `BP_AISpawner_C_34` | Resort_SO4_A Complete |
| SetWaves_C_5 | presence C_16 (after Resort_SO3) | `BP_AISpawner_C_35` | Resort_SO4_B Complete |
| SetWaves_C_15 / C_19 | panel switch + 35 s / '2nd wave' | `STAISpawner_14` / `STAISpawner_16` | no row of their own (feed 'Final Wave') |
| SetWaves_C_0 | 'Final Wave', inMonorailArea | `STAISpawner_5` | Monorail_O1 Complete |
| SetWaves_C_9 (+C_14) | Switch_C_3 + 1.5 s | `BP_AISpawner_C_49` | Satcom_SO2 Complete (dropship C_1 starts) |

Classes in C3M1 runs (c229, c127): `BP_AI_WarriorBug_C` (most), `BP_AI_ChariotBug_C`, `BP_AI_AphidBug_C`,
`BP_AI_TankerBug_C`, `BP_AI_HopperBug_C`, `BP_AI_CenturionBug_C`, `BP_AI_ArcherBug_C`. The kill rounds kill the
nearest live bug of each class (`kill actor` skips the dead, card 259).

### Volumes and markers

The scenario moves both pawns onto each presence volume by its actor name (`warp actor` / `goto actor` resolve
the placed name: `c64_find_actor` matches the object name exactly, `hooks_ubw.cpp:1430`). It never targets a marker
for a presence trigger. The static volume positions read from the umap are not reliable (the component offsets do
not decode to world positions for all of them), so run 1 logs each volume's world position with `look actor`
before entering it, and the report gives the distance to its marker from that line. Marker positions (umap
DefaultSceneRoot): ActorReference_C_0 (-72610 -10 2440), C_1 (-76010 -10 2110), C_2 (-25980 -9660 1090), C_3
(-14480 -15430 1190), C_4 (-16700 -45520 1680), C_5 (-19534 -47084 1995), C_10 (-52103 -19810 2880), C_11 (-69827
-42524 1064), C_12 (-69303 -42610 1270). Volumes: C_8 (Resort_SO1), C_22 / C_16 (Resort_SO4_A / B), C_4
(Monorail_SO1), C_19 (inMonorailArea, must hold both players through the monorail waves).

### Mission end

The last top objective is Satcom_O1. It completes on PlayerEntering of dropship `BP_PlayerTransportDropship_C_1`
(row 18). 13.0 s later the level script calls `STGameInstance:EndLevel`, then the WB_TrooperLevelEnd widget shows.
The host clicks `BndEvt__TrooperLevelEnd_WB_Button_Next` until `card052 ... 3D_Frontend ... net mode 2`, then the
frontend stages, then FED_C3M2. PlayerSkippedFlight on C_1 (19002) goes to EndLevel directly.

Whether the client runs the mission end by itself: no. EndLevel is called from the level script on the server only.
The client has no level-end widget of its own (card 260) and follows the host by the card045 reconnect. The client
wait on its card018 line is a `try` step.

### Driver `unless` guard (card 241 run 10 FAIL lines)

Driver fault. `run_step` (`tools/drive.py:361`) substitutes `{gN}` in the whole line before it dispatches to
`run_unless` / `run_when`. A step whose `unless` pattern has matched should be skipped, but the substitution runs
first and fails. In 241 run 10, the last successful `wait log` was the objective wait (its groups are the
alternation of the objective regex), so `{g1}` held the whole card097 line and the step failed with "could not
convert string to float". `(?:...)` in the scenario does not cure it: with no groups, substitute raises "no group 1"
just the same. Fix (needs the senior's go): substitute only in the branch that runs a command, i.e. move the
`substitute(words)` call below the `unless` / `when` / `void` dispatch (those call `run_step` again on their inner
step, which then substitutes). The 242 scenario uses no `{gN}` at all (actors are reached by name), so the fault
cannot fire in a 242 run either way.

## Step 2 (junior, 2026-11-13)

`tools/scenarios/c242-c3m1.txt`, 1764 steps (cap 2000), generated by the junior's `gen_c242.py`. No `{gN}` in any step.
Kill rounds: nearest live bug of each C3M1 class, then 6 s, gated on the encounter's Complete row (20 rounds per
encounter, 15 for the tanker, 25 for Satcom_SO2). Monorail: both players are warped back onto
`BP_PlayerPresence_C_19` before each of 30 rounds.

`tools/drive.py` (senior go, 2026-11-13): `run_step` substitutes `{gN}` only when the step is not `unless` or `when`;
those two substitute in their inner step through the recursive `run_step`. `void` and all other steps unchanged.

## Run 1 predictions (written before the run)

1. P1: the three switch presses log Beach_SO1_A/B/C rows, then the Beach_SO1 cascade line and the Beach_SO3 Add row.
2. P2: Beach_SO3 completes about 2 s after its Beach_SO4 Add row; Beach_SO4 completes after a TankerBug kill.
3. P3: the EPulse88_C_1 pickup logs Beach_SO2 Complete and Beach_SO5 Add; Beach_O1 follows Beach_SO5.
4. P4: presence C_8 logs Resort_SO1 Complete and Resort_SO2 Add; three satchel pickups give Resort_SO2.
5. P5: each flakbug satchel hold logs its Resort_SO3 child row; Resort_SO4 Add follows Resort_SO3.
6. P6: presence C_22 and C_16 each lead to their Resort_SO4 child row after kill rounds; Resort_O1 follows.
7. P7: Monorail_SO1 on presence C_4; Monorail_SO2 on the panel switch; Monorail_O1 with Monorail_SO3 in the same
   ms (cascade) while both stay in C_19.
8. P8: Satcom_SO1 Add after the third top objective; Switch_C_3 gives Satcom_SO1; Satcom_SO2 after kill rounds; the
   C_1 landing line, then E gives Satcom_SO3 and Satcom_O1; the host card018 level-end line about 13 s later.
9. P9: no drive FAIL line with "could not convert" or "no group".
10. P10: the client card018 level-end line does not appear (card 260); the client reaches FED_C3M2 by card045.

## Run 1 result (cap 3600, 3622 s)

5 of 19 completed (Beach_SO2, Beach_SO5, Resort_SO1, Resort_SO2, Monorail_SO1), client same set, no level end,
0 crashes, digest 119 rounds all `mismatches 0`. P3, P4, P7 partly or fully confirmed, P9 confirmed, the rest open.
Causes: switch presses aimed at the actor origin gave card020 target null (C_4 warp left the pawn below the switch);
satchel holds aimed at the flakbug actor, while the real target is its child `BP_SatchelPlacementSite_C`; card020
Interact lines are capped after 12 events, so later presses (panel switch, Switch_C_3) show no Interact result.

## Run 2 interaction spots (evidence)

Positions from the run 1 host log (c242-c3m1-host.log, `cmd: N warp:` / `look:` lines). The aim point is the actor
origin + dz; dz is tried in order 100, 60, 140, 30 for switches, 0 then 60 for placement sites.

| Actor | Actor position | Stand point (run 2) | Aim point | Proof of the recipe |
|---|---|---|---|---|
| BP_Switch_C_4 (Beach_SO1_A) | -74944 2733 1570 | first exact point -74773 2668 1670, then warp 120 behind, 120 | z 1670/1630/1710/1600 | no C3M1 hit yet. Run 1 cmd 2/6: warp 120 put the pawn at (-74911 2848 1670), it fell to z 882 (look cmd 4, 771 uu, pitch +63): target null. The first point mirrors the C_6 stand point that held (cmd 18: -74867 -2768 1670, stayed at z 1748 eye) |
| BP_Switch_C_0 (Beach_SO1_B) | -70282 -38 1200 | warp 120 (run 1: -70381 29 1300, held), then 120 behind | z 1300/1260/1340/1230 | run 1 cmd 12/16: aim at origin from eye -70381 116 1378 (256 uu, pitch -44): target null |
| BP_Switch_C_6 (Beach_SO1_C) | -74968 -2833 1570 | warp 120 (run 1: -74867 -2768 1670, held), then 120 behind | z 1670/1630/1710/1600 | run 1 cmd 20/24: aim at origin from eye -74799 -2766 1748 (255 uu, pitch -44): target null |
| BP_PanelSwitch_WithoutScreen_C_1 (Monorail_SO2) | -18370 -50000 1550 | warp 120 (run 1: -18414 -49888 1650, held), then 120 behind | z 1650/1610/1690/1580 | same class proven by scripted client presses: logs/c121r-client.log 2113/2115/2127 (`look actor BP_PanelSwitch_WithoutScreen_C_0 100`, card050 + card020 target BP_PanelSwitch_WithoutScreen_C), logs/c248-c1m1-client.log 3875/3878/3891 (dz 140 after null at 100/60), logs/c240-c1m1-r8-client.log 4110 (dz 140). Correction: the earlier c121b-host 2030/2052 cite was a human press, not scripted. Run 1 cmd 743: aim at origin, Interact not logged (card020 cap) |
| BP_Switch_C_3 (Satcom_SO1) | -50980 -19600 2000 | warp 120 (run 1: -50889 -19678 2100, held), then 120 behind | z 2100/2060/2140/2030 | run 1 cmd 1057: aim at origin, Interact not logged (card020 cap) |
| BP_SatchelPlacementSite_C on Flakbug_C_0/_2/_1 | flakbugs -22620 5730 121 / 2004 -7999 224 / -14437 -27152 143; site C_2 seen at 1572 -8252 339 (turns with the flakbug) | warp flakbug 600, then warp site 100 | site origin z+0, then z+60 | c240: `warp actor BP_SatchelPlacementSite_Flag_C 100`, `look actor` (dz 0), `key E 3200` (logs/c240-c1m1-r8.drive lines 264-267) gave card020 target SatchelSiteCAC_BP_SatchelPlacementSite_Flag_C_CAT (c240-c1m1-r8-host.log line 14795; also r4 14334). Run 1 aimed at the flakbug (cmd 385: 236 uu, pitch -50): target null |

Because card020 is capped after 12 events, every pass waits on the game-side result row (the objective Complete row),
not on the card020 line. Each spot is warped to once per pass (run 1's two SetActorLocation FAILs were second warps
onto the pawn's own spot; `use` and the plant now warp once, then retry only look and E).

## Run 2 order and fail fast (senior run 2 conditions)

Order: Beach_SO1 switches, then Resort_SO1/SO2/SO3, then Monorail_SO1/SO2 and Monorail_O1, then Beach rest, Resort
rest, Satcom. After each chain root a `host shot c242-<root>-check` and a `void unless host log "<root Complete>"
before host log "<that shot>"` end the run when the row is missing: Beach_SO1, Resort_SO3, Monorail_SO2, and the
Satcom_SO1 Add row. Every kill block runs only `when host last "<its Add row>"` (Beach_SO3, Beach_SO4, Beach_SO5,
Resort_SO4, Satcom_SO2).

## Run 2 predictions (written before the run)

1. P1: Beach_SO1_A/B/C each complete within their press block; the Beach_SO1 row follows (no void at beach-so1).
2. P2: each placement-site hold logs its Resort_SO3_A/B/C Complete row; Resort_SO3 follows (no void at resort-so3).
3. P3: Monorail_SO2 completes on the aimed panel-switch press (no void at monorail-so2); Monorail_O1 and SO3 follow
   with both players in C_19.
4. P4: Beach_SO3, Beach_SO4, Beach_SO5 and Beach_O1 complete after the Beach rest kill rounds.
5. P5: Resort_SO4_A/B after presence and kill rounds; Resort_SO4 and Resort_O1 complete.
6. P6: Satcom_SO1 Add row after the third top objective (no void at satcom-add); Switch_C_3 gives Satcom_SO1; the
   Satcom chain ends with Satcom_O1.
7. P7: host level end (card018), both sides reach FED_C3M2, 0 crashes.
8. P8: no drive FAIL line with "could not convert", "no group" or "SetActorLocation failed"; digest mismatches only
   carded classes.

## Run 2 result

Ended by the scenario at the Beach_SO1 void (scenario line 190) after 442 s. Host rc 0, client rc 0, no FATAL/ERROR, no
crash dump. Beach_SO1_A/B/C were added at 199570891 with actors BP_Switch_C_4/_0/_6. All 12 scripted switch presses
gave card020 target null, including the exact-point stand on C_4 (eye about 189-239 uu from the origin, pitch -12.5 to
-39.3, dz 100/60/140/30). The 120 and 120-behind stands on C_4 dropped the pawn to z 875. No log in logs/ has ever
shown a BP_Switch_C interactor target or a Beach_SO1_A complete, so the beach switch class has no scripted proof.

## Run 3 step 1 (static trace, written before run 3)

**What fires the completion.** FED_C3M1_Scripting binds SwitchPressedDelegate per switch: ActorBoundEvent_31 on
BP_Switch_C_4, _3 on C_0, _33 on C_6, _15 on C_3, and _14 on the panel C_1. Exports 2547-2551 share class import
0x80000016000000aa and template 0x80000016000001eb. All of them are BP_PanelSwitch_WithoutScreen_C, the same class
whose scripted presses are proven on FED_C1M1. The only path into that delegate is BP_Switch_Base's
OnPressSuccessful_Event, reached through the Interactable interface (UADInteractorComponent::Interact on the
interactor's traced target).

**What does not fire it.** BP_Switch_Base and BP_PanelSwitch_WithoutScreen hold no Damage, Hit or Overlap name. So no
shot, melee, grenade or overlap path exists. The receiver is the InteractionBox component. Its response names are
Pawn, Pawn_NoDamage and PlayerProjectile. The full collision profile is not decoded.

**Enable gates.** The level script calls STSwitch:SetSwitchExternallyEnabled(true) only for C_3, at offset 3314. C_4,
C_0 and C_6 have no enable call, so they run on the class default, the same default as the C1M1 panel. That default
holds bExternallyEnabled, ShouldStartInactive, CanOnlyPressOnce and bIsInteractionActive; their byte values are not
decoded. No squad, encounter or distance name exists in either class.

**Where the InteractionBox sits.** The panel's InteractionBox_GEN_VARIABLE override is:

- RelativeLocation (59.3, -10.0, 162.9);
- BoxExtent (30, 40, 20);
- rotation -50 on the first rotator field.

So the box sits 59 uu in front of the origin along the actor forward axis (+X local), 163 uu up, tilted. The
BP_Switch_Base default extent is (100, 100, 100). The interactor trace can reach the box only from the front.

**Why every run 2 stand missed.** All 12 run 2 stands were beside or behind the switch. In testrun.cpp:808-814, the
plain `warp actor X d` places the pawn on the side it came from (from - at). `behind` uses root yaw + 180, which is
the back of the panel. On FED_C1M1 the proven press stood on the panel's forward side: actor (17309, 12643, 116),
yaw 90, eye (17332, 12819, 294).

**Mod side.** The mod has no class filter on the press path:

- detour_interact (attributes.cpp:937) refuses only while the owner is down (c81_refuse_press), and skips only the
  card130 revive actor;
- c102_interact passes every target except the transport dropship;
- detour_trace_interactable (interact.cpp:80) returns null only while the owner is down, and swaps in the revive
  interactable only near a downed mate.

So the run 2 nulls are the game trace finding no InteractionBox, not a mod hook.

**Predicted stands.** Each stand is the origin + forward x 170 at z = origin + 100, set by `host warp actor point`.
Then `host look actor <switch> 160`, then E, with a retry at dz 140. Each eye is about z origin + 164, close to the
box centre at dz 163.

| Switch | Yaw, forward | Stand point | Box centre (world) |
|---|---|---|---|
| C_4 | -90, -Y | -74944 2563 1670 | (-74954, 2674, 1733) |
| C_0 | 0, +X | -70112 -38 1300 | (-70222, -48, 1363) |
| C_6 | 90, +Y | -74968 -2663 1670 | (-74958, -2774, 1733) |

The C_6 identity is from the Scripting transform list. Its bind is ActorBoundEvent_33.

**Risk.** The warp log `now` z shows whether the front point has ground. The run 2 back-side stands on C_4 fell to
z 875, so the back is a ledge; the front side is not known.

**Proof line.** One host line proves an activation:
`card020: host host-own UADInteractorComponent::Interact(owner BP_PlayerCharacterMI_C): target BP_Switch_C (BP_PanelSwitch_WithoutScreen_C)`.
object_name drops the instance number. After it comes the Beach_SO1_A, B or C complete row (card049 or card097).

**No-path condition.** If the front stand also logs target null with the pawn standing (now z near origin + 100),
then the class default is inactive. In that case no game path enables C_4, C_0 or C_6 in this session, and I report
it with that line.

## Run 3 stands (senior go, written before the launch)

**Identity correction.** Each component export's outer is the owning export's 0-based index, not that index plus 1.
With that read, the FED_C3M1_Scripting roots match the run 2 runtime warp lines (C_4 at (-74943.8, 2732.9) yaw -90,
C_0 at (-70281.7, -38.0) yaw 0, C_6 at (-74968.0, -2832.6) yaw 90).

**Stands.** Each stand is origin + forward x 170, z origin + 100, on the side the InteractionBox faces (+X local, 59 uu
out, dz 163):

| Switch | Origin | Yaw | Stand (host warp actor point) |
|---|---|---|---|
| BP_Switch_C_4 (Beach_SO1_A) | (-74943.8, 2732.9, 1570) | -90 | -74944 2563 1670 |
| BP_Switch_C_0 (Beach_SO1_B) | (-70281.7, -38.0, 1199.9) | 0 | -70112 -38 1300 |
| BP_Switch_C_6 (Beach_SO1_C) | (-74968.0, -2832.6, 1570) | 90 | -74968 -2663 1670 |
| BP_PanelSwitch_WithoutScreen_C_1 (Monorail_SO2) | (-18370, -50000, 1550) | -180 | -18540 -50000 1650 |
| BP_Switch_C_3 (Satcom_SO1) | (-50980, -19600, 2000) | 0 (no rotation property) | -50810 -19600 2100 |

The C_1 and C_3 rows come from the umap only; no run has warped to them yet.

**Satchel sites.** BP_SatchelPlacementSite_C's Box is extent (50, 50, 60) with no location override, so it faces no
side. The stand stays at `host warp actor BP_SatchelPlacementSite_C 100` (it rides the flakbug, so it has no fixed
point).

**Fall check.**
- Switches: the warp line logs right after the teleport, before any fall, so the check is the look line one second
  later. Its eye must be within 30 uu of the stand in x and y, at stand z + 50..120 (run 2 standing eye: + 81). If it
  is not, a `void unless ... before host log "shot c242-<switch>-stand"` ends the run before any press.
- Satchel: drive has no relative-z void, so the check is `host wait near` of the warp target (60 uu) one second later.
  A fall shows as a FAILED line; it does not stop the press.

**Aim.** dz 160, then 140. Fail-fast roots and Add guards are unchanged from run 2.

## Run 3 predictions

1. P1: each beach stand holds (no stand void). card020 then gives `target BP_Switch_C (BP_PanelSwitch_WithoutScreen_C)`
   at dz 160 or 140, and Beach_SO1_A/B/C each complete. Beach_SO1 follows.
2. P2: if a stand holds and the target is still null, the run stops there. I report that line; no workaround.
3. P3: Monorail_SO2 completes on the front press of the C_1 panel.
4. P4: Satcom_SO1 completes on the front press of C_3.
5. P5: the run 2 predictions P2 and P4 to P8 stand unchanged.
6. Budget: a void at a fail-fast root within 15 min is a probe (at most 2). Each probe gets a logged cause and a
   changed prediction here before the next launch.

## Run 3 result (probe 1)

Run 3 (DLL 61cf21ee94ecdd3b, lane 1) was voided by the scenario at 312 s, so it counts as probe 1. Both games ended
with rc 0. FATAL/ERROR 0 on both sides, WARN 46 on host and 42 on client, no crash dumps. Root: drive line 188,
`void unless ... ObjectiveComplete(Resort_SO3) before ... shot c242-resort-so3-check`.

- P1 confirmed: every beach stand held, card020 gave the panel target, and Beach_SO1_A, B and C completed at
  200885707, 200887847 and 200889962. Beach_SO1 completed, the client replayed each one, and the beach-so1 void
  passed.
- Resort_SO1 completed at 200890695.
- Resort_SO3_A completed at 200967684 and Resort_SO3_B at 200980102. Each one followed a press on the plant site with
  card020 `target SatchelSiteCAC_BP_SatchelPlacementSite_C_CAT`, the `MS_WPN_SatchelCharge_Prime` sound, a 3.2 s
  hold, and then the flakbug's death.
- Resort_SO2 did not complete. Resort_SO3_C did not complete, so Resort_SO3 did not complete either.
- P3 and P4 were not reached.

Cause (logged):

1. Satchel pickups C_1 and C_2: card020 targeted each pickup, and card095 logged `PlayerCollider 1 -> 0` and
   `InteractionCollider 1 -> 0`, so both were taken.
2. Pickup C_3 (host line 24861): card020 targeted `BP_SatchelItemPickup_C`, but no card095 or card098 line for
   `BP_SatchelItemPickup_C_3` followed anywhere in the log. The pickup was refused and stayed in the world.
3. The level script (FED_C3M1_Scripting ubergraph 16734 to 16830) does `satchelCount += 1` on each
   ADPickupDelegate and compares the result `== 3`. The third pickup never fired, so Resort_SO2 never completed.
4. Plant C, second press (dz 60, host line 55064): card020 gave target `SatchelSiteCAC_BP_SatchelPlacementSite_C_CAT`
   and card043 logged press and release 3.2 s apart, but no `MS_WPN_SatchelCharge_Prime` sound fired. Plants A and B
   each primed on the press. The pawn had used the two satchels it carried on A and B. Reading: the pawn carries at
   most two satchels at once, so the third pickup is refused while it holds two.
5. Plant C, first press (dz 0): `target null`. The pawn fell after the site warp: the warp `now` z was 367.9, the look
   eye was at z 320, and the eye was 341 uu from the site.
6. A scenario defect in this card's file: the satchel stand check never ran. The warp capture regex expected
   `from BP_SatchelPlacementSite`, but the warp line names the component actor:
   `from SatchelSiteCAC_BP_SatchelPlacementSite_C_CAT_416`. The capture wait was FAILED, and so was the
   `wait near {g1} {g2} {g3}` that depends on it. Both are logged-only steps.

## Run 4 predictions (probe 2, changed after probe 1)

Order change (scenario only): pick up C_1 and C_2, plant A and B, pick up C_3, then plant C. The capture regex now
reads `from SatchelSiteCAC_BP_SatchelPlacementSite\S*`. I tested it against host line 47681 of run 3, and it captured
(-14376.2, -26694.4, 367.9).

1. Q1: the C_3 pickup after plants A and B is taken (card095 `BP_SatchelItemPickup_C_3 ... 1 -> 0`). Resort_SO2
   completes on that pickup, because satchelCount reaches 3.
2. Q2: plant C primes (`MS_WPN_SatchelCharge_Prime`), Resort_SO3_C completes, then Resort_SO3 completes, and the
   resort-so3 void passes.
3. Q3: if C_3 is still refused while the pawn holds no satchel, the carry-cap reading is wrong. Run 4 then voids at
   resort-so3 as probe 2, and I stop and report.
4. Q4: each satchel stand check (`wait near` 60) now runs. A fall shows as a logged FAILED line, not a void.
5. P3, P4 and P5 from run 3 stand unchanged.

## Satchel pickups, run 1 vs run 3 (written during run 4)

Run 1 (logs/win64-old/UBWCoop-host-20261113-015913.log): one host pawn took all three
pickups, no client involved, no plant between them:

| pickup | look (ms) | press card043 (ms) | card050 DisablePickup | card020 Interact |
|---|---|---|---|---|
| C_1 | 196572102 | 196572831 | 196572831 (line 150493) | 196572832 |
| C_2 | 196578943 | 196579686 | 196579686 (line 151696) | 196579686 |
| C_3 | 196586078 | 196586797 | 196586797 (line 153185) | 196586807 |

Resort_SO2 completed at 196586807. The pawn held two satchels at the C_3 press, and the
pickup was taken.

Run 3 (logs/win64-old/UBWCoop-host-20261113-032716.log): the warp and look positions match
run 1 to within 3 uu at all three pickups.

| pickup | look (ms) | press (ms) | card050 | card020 |
|---|---|---|---|---|
| C_1 | 200892243 | 200892444 | 200892444 (line 20965) | 200892445 |
| C_2 | 200897237 | 200897436 | 200897436 (line 22606) | 200897436 |
| C_3 | 200902087 | 200902296 | none | 200902296 (line 24861), target BP_SatchelItemPickup_C |

At C_3 in run 3, Interact reached a BP_SatchelItemPickup_C, but DisablePickup never ran.
card020 logs only the class, so the instance it hit is unknown. Two things differ from
run 1:
- The press came about 209 ms after the look, against about 720 ms in run 1. C_1 and C_2
  in run 3 also used about 200 ms and worked, so timing alone does not explain it.
- Aphid hits landed on the host at 200902092 and 200902182, between the look and the press.

Carry cap: **not 2**. Run 1 shows one pawn holding two satchels taking a third. No log line
shows a cap or a refused pickup, and I did not read an attribute max. The cap is at least 3,
because the level script completes Resort_SO2 at satchelCount == 3. The probe 1 reading
("third pickup refused while the pawn holds two") is withdrawn.

Solo host, the game's own order (three pickups, then plants): run 1 did it. Resort_SO2
completed and SO3_A/B/C were planted from that state, so a solo host can complete Resort_SO2
and SO3. The run 3 C_3 miss happened once, its cause is open, and it is not a carry cap.

Effect on run 4: run 4 moves C_3 after plants A and B (the probe 1 order). Q-predictions that
lean on the cap no longer have a cause behind them. Run 4 still tests whether C_3 is taken
on a later press.

## Run 4 result (probe 2): void at drive line 188, Resort_SO3

Ended by the scenario after 289 s. rc 0 on host and client, FATAL/ERROR 0, WARN 47/45, new crash
dumps none, map loaded 2/2. Host archive: logs/win64-old/UBWCoop-host-20261113-033546.log (moved at
next launch); copies in logs/c242-c3m1-host.log / -client.log.

Objectives completed (host card097, client replays matched): Beach_SO1_A, Beach_SO1_B, Beach_SO1_C,
Beach_SO1, Resort_SO1, Resort_SO3_B, Resort_SO2, Resort_SO3_C. That is **8 of 19**. Root: line 188
`void unless host log ... Resort_SO3 ...` because Resort_SO3_A never completed.

- Q1 **confirmed**. C_3 taken after plants A and B: card043 press 201519905, card050 DisablePickup
  201519905, card095 `BP_SatchelItemPickup_C_3 ... InteractionCollider 1 -> 0` 201519913,
  `ObjectiveComplete(Resort_SO2)` 201519912. The press came 319 ms after its look.
- Q2 **partly**. Plant B primed 201512037, Cancel 201514529 (+2.49 s), SO3_B 201517534. Plant C
  primed 201525902, Cancel 201528396 (+2.49 s), SO3_C 201531407. Plant A primed 201416059, but
  `MS_WPN_SatchelCharge_Cancel` came at 201417298 (**+1.24 s**) while E was still held (release
  201419272), and SO3_A never completed. A completed plant in runs 3 and 4 always cancels at about +2.49 s
  (the end of the hold), then the objective follows about 3 s later. The A hold was cut at half time.
  - The plant A retry `look actor BP_SatchelPlacementSite_C 60` turned to another site point 574 uu away
    (-22931 5998 319); Interact gave **target null** (201434902). That is the stop rule's
    "target null" case, logged here for the senior.
- Q3: not reached (C_3 was taken).
- Q4 **confirmed as written**. All three stand checks ran and failed as logged lines, not voids:
  - line 118 (A): last 248 uu of (-22996.9, 5571.0, 368.7)
  - line 139 (B): last 225 uu of (1610.7, -8113.5, 435.0)
  - line 171 (C): last 215 uu of (-14613.0, -26699.4, 416.5)

  The own pawn at plant A was at (-23170, 5479, 216) (card132 significance view, 201416335), so it
  was 196 uu sideways and 152 uu below the warp point. The stand does not hold: the pawn slides off the warp
  point at all three sites. B and C still primed from where the pawn stopped. At A the pawn was most
  likely still moving at +1.24 s and the movement cut the hold (not proven: no velocity line in
  that window). In run 3 the site A warp went to a different point (-22850.6, 5930.9, 358.2),
  and the plant completed.
- P3–P5 unchanged; not reached past line 188.

Carry cap: see the run 1 vs run 3 section. Run 1 shows one pawn taking a third satchel while
holding two; run 4 took C_3 while holding one. No cap below 3 is seen in any run.

Budget: 2 probes used (runs 3 and 4). Stopped; no third probe.

Fix candidates for the next card (none applied):
1. Satchel site warp: wait for the stand (`wait near` ... 60 with a longer timeout) before the
   look and E. Or warp to the site point the pawn settled on in run 3 for A.
2. Retry look: aim at the same site instance as the first look (the 60 uu radius picked another
   point 574 uu away), not the nearest site point to a moved pawn.

## Hold and Interact cancel conditions (static, written before probe 3)

Sources: exe disassembly (image base 0x140000000), pdb names, extracted assets
(BP_SatchelBuilding_Flakbug, FED_C3M1, FED_C3M1_Scripting), mod hooks of cards 043, 133, 248.

UADInteractorComponent (on ASTPlayerCharacter only; AI troopers have none):
- InteractionDistance +0xf8 = 10000.0, bRequiresConstantLOS +0xfc = 0.
- TickComponent 0x4A20230 locks the focus (+0x100) to the hold target while a hold runs. It ends
  a hold on exactly three paths:
  1. Distance: pawn to target beyond 10000 uu (the LOS test is skipped, flag 0).
  2. Completion: TickInteractable 0x4A206D0 adds dt to +0xc; at 1.0 progress
     OnTimedInteractionCompleted 0x4A20020 clears the flags, calls OnInteraction, broadcasts ended.
  3. ReleaseInteraction 0x4A200F0 (vtable 0x4f0): key release, OnDisableComponent 0x4A1FF30
     (SetActive(false) then release). Card 043 detours the function itself, so every release
     path logs "card043: ... own interact release (ReleaseInteraction)".

Answers to the senior's list:
- Owner damage or stagger: no cancel path in C++ or in the satchel / pickup blueprints.
- Owner movement: cancels only beyond 10000 uu. A walk or knockback inside the site does not.
- Loss of the trace target: no cancel. LOS is off and the focus is locked for the hold.
- Second interactor: only player pawns have one. A server-copy tick on the host is dropped and
  logged by card 248 ("card248: ... dropped"); a server-copy completion is a card 133 WARN.
- Per-site override: none of InteractionTime, MasterSite, LinkedSites,
  bIsSiteControlledExternally appears in the level or scripting assets; all three sites are the
  same SatchelSiteCAC child-actor template, so one InteractionTime (2.49 s seen at B and C).

Plant A, run 4 (host log), checked against this list:
- 201416059 press, Prime at pawn (-23170, 5479, 216); 201416060 card020 target
  SatchelSiteCAC_BP_SatchelPlacementSite_C_CAT; 201417055 IA_ExitVehicle first tabled (E is bound
  to both IA_ExitVehicle and IA_Interact; it also fires on B and C, so it does not set A apart);
  201417298 Cancel = +1.24 s.
- No pawn hit between press and Cancel. No card043 release until 201419272. No card248 and no
  card133 lines in the run. Client pawn 1199 uu away. No slomo or time dilation in scenario or log.
- No explosion followed; flakbug_0 still idle at 201421184 and 201423812.
- B and C: Cancel at +2.49 s, then the charge (B: FX at the site +3.0 s, flakbug destroyed,
  CompleteSubObjective Resort_SO3_B).
- Result: the hold ended at almost exactly half the site time with the pawn still, unhurt, the
  target held, and none of the three end paths logged except an ended broadcast. Per the senior's
  rule this is reported as a likely mod or game fault, not worked around. Remaining static
  candidates (unproven): progress added twice per frame for A (two ticks on the host-own
  component), or OnInteraction refusing the place (the satchel OnInteraction 0x5163E00 failure
  paths are not yet read).

## Plant A, two-interactor check (senior ask, logs only, no run)

- Client: 2 `cmd:` lines in the whole run 4 log (polling, the fail shot at 201572380); no E, no card020 / 043 /
  248 / 133 line. Pawn at (-21999, 5226, 167) at 201415362 and 201505393 (1199 uu from the host at A; about 25000 uu
  from site B). At C it stood at (-14395, -26297) at 201525403, 491 uu from the host pawn, and C ran the full 2.49 s.
- Card 248: `c54_relay_row` (hooks_ubw.cpp:1410) is `is_a` against ASTSatchelPlacementSite (hooks_ubw.cpp:2348), so
  the C3M1 sites are relay rows and a server-copy tick would be dropped and logged once per target. Host log: 0
  card248 lines; card133 only the wire-name WARNs at 201338900. No second interactor ticked site A.
- The one difference, A against B and C: flakbug_0 turned during the hold (client card098
  `BP_SatchelBuilding_Flakbug_C_0|TurnInPlace` call 1 at 201416755, +0.70 s; call 4 at 201417179; ChargeUp at
  201417197; host Cancel at 201417298). Flakbugs _2 (B) and _1 (C) logged no TurnInPlace in their hold windows. Site A
  is the SatchelSiteCAC child actor of flakbug_0; the flakbug BP calls K2_SetActorRotation and K2_AttachToComponent
  (SatchelSiteSocket). Which call ends the hold when the parent turns is not proven.
- Diag line proposed to the senior (host, satchel-site targets, every TickInteractable in detour_c248_tick): t, site,
  interactor owner, side, dt, progress +0xc before and after, started +0x9, completed +0xa, site location and yaw,
  pawn to site distance.

## Probe 3 (senior go 2026-11-13; written before the run)

Working cause (accepted, not carded unless this probe contradicts it): the flakbug's TurnInPlace moves or re-attaches
its child site and ends the hold; base game behaviour, the player presses again. No diag build.

Recipe per plant (gen_c242.py `plant`, `plant_try`):
- Warp near the flakbug, client 800 uu off; two kill rounds on the bug classes (never the flakbug).
- Up to 5 tries, each gated on the site's Complete row:
  - warp onto the site by its own name (A `..._CAT_289` under Flakbug_C_0, B `..._CAT_0` under Flakbug_C_2,
    C `..._CAT_416` under Flakbug_C_1; same suffixes in every C3M1 log: c123*, c125*, c242 run 4), so each try
    uses the site's fresh position;
  - `host wait stand <warp target> 300 timeout=4` (VOID on timeout). Radius 300: in run 4 the pawn settled
    215-248 uu off the warp target (target 150 uu up, pawn slid off the site box), so 60/80 would void every try;
  - look at the same instance by name, hold E 3.2 s, wait on the prime row (card020 Interact), then the Complete row;
  - a cut try waits on the turn-end row before the next try (no sleep).
- Turn rows: host `card132: sent N t=T WorldOp <id> L|TurnInPlace <op>`, op 1 = PlayFromStart (turn start),
  op 4 = Stop (turn end, the named "turn-end" row); client `card098: client t=T world L <flakbug>|TurnInPlace ...
  applied timeline call <op>` names the flakbug. Run 4 plant A: op 1 at +0.69 s, op 4 at +1.11 s, Cancel at +1.24 s.
- Note: the Cancel sound also plays on a completed hold (run 4 B and C: Cancel at +2.49 s after Prime). A cut is
  a Cancel before +2.49 s with no Complete row.

Predictions:
1. A try with no TurnInPlace row (op 1 or 4, client row naming that flakbug) between its Prime and +2.49 s
   completes at Prime +2.49 s (Cancel sound at +2.49 s, Complete row).
2. A try with a turn in that window is cut (Cancel before +2.49 s, no Complete row); the next try completes.
3. Contradiction, void, stop, report (then the senior cards the diag): a cut with no turn row in its window, or
   5 cut tries on one site.
4. If Resort_SO3 completes, the run is the full run: it goes on through the chain with the fail-fast roots.

## Probe 3 result, full run (run 5, dll 61cf21ee94ecdd3b, logs c242-c3m1-{host,client}.log)

Plants: prediction 1 confirmed 3 of 3, first try each; prediction 2 not exercised (no turn fell in a hold window);
no contradiction, so no diag card.

| Site | Stand | Prime (host t) | Cancel/complete | Complete row | Turn rows near the window |
|---|---|---|---|---|---|
| A `_CAT_289` | 261 uu | 202894818 | +2496 ms | 202900323 | flakbug_2 op 1 at -533 ms, op 4 at -119 ms; none inside |
| B `_CAT_0` | 225 uu | 202919191 | +2494 ms | 202924693 | flakbug_1 op 1 at -952 ms, op 4 at -532 ms; none inside |
| C `_CAT_416` | 136 uu | 202946174 | +2495 ms | 202951675 | none |

Objectives: 16 of 19 complete, host and client identical. Done: Beach_O1, Beach_Marker1/2, Beach_SO1 (A/B/C),
Beach_SO2-SO5, Resort_O1, Resort_Marker, Resort_SO1, Resort_SO2, Resort_SO3 (A/B/C), Resort_SO4 (A/B),
Monorail_O1, Monorail_Marker, Monorail_SO1-SO3, Satcom_SO1. Not done: Satcom_SO2, Satcom_SO3, Satcom_O1
(Satcom_Marker is hidden, not completed); no level end, no travel to FED_C3M2.

Satcom stall, root: the scenario gate. The `BP_Switch_C_3` press (host t=203544799) added Satcom_SO2 (host
`card049: host t=203544801 AddSubObjective(Satcom_O1, Satcom_SO2) bools 01110`; card132 sent 68505 `Objective kind 1
flags 0xE`; the client replayed it at t=203544818, objectives 30 -> 31) and completed Satcom_SO1, and the HordeZone
waves started (card112 activate rows at t=203546314). But the host printed no `card049: host sent
AddSubObjective(Satcom_O1, Satcom_SO2)` row, the only Add of the 28 in this run without one. The Satcom_SO2 kill
rounds are gated `when host last "card049: host sent Add...Satcom_SO2"`, so every round skipped, the horde
(SetWaves_C_9/C_14) was never cleared, and its end (which starts dropship C_1, adds SO3, completes SO2) never came.
The missing row: in `c49_emit` (mod/src/ubw/objectives.cpp:357) the line follows `c49_send`; the send went out
(68505), so either the line was skipped or the nested Satcom_SO1 complete in the same tick ran between the send and
the line. Reported, not fixed (mod source).

Drive failures: line 363 (shot name `c242-bp-panelswitch-withoutscreen-c-1-stand` over 40 chars, generator bug);
1939 (WB_TrooperLevelEnd never shown), 1941-1955 (Next button absent), 1957, 1958, 1969, 1970 all follow from the
stall. 0 VOID rows.

New WARNs against run 4: client `card132: mismatch BP_SplineRiderMonorail_C ... location (one side attached?)` 30
rows (9 per round, e.g. C_14 host (-18850,-57400,1550) client (41850,-74750,1550), t=203109183), run 4 had 0; host
`card132: OnHit of ADProjectile, whose GrenadeExplode already went` 9; host `card130: host fx watch full (64)` 1;
client card145/card104 no copy of host 245419 (BP_GrenadeAmmoPickup_Incendiary) 8; client card140 pose freeze 1.

After run 5: gen_c242.py switch shot tags are now class initials + instance number (`bppswsc-1`, `bpsc-6`), and
both shot helpers assert the prefixed name fits 40 chars; the regenerated scenario differs from run 5's only in those
10 lines. The Satcom_SO2 gate change (key "added" on the `card049: host t=\d+ Add...` row) waits for the senior.

## Run 6 (senior go 2026-11-13; written before the run)

Generator changes only (gen_c242.py; no mod change, no build):
- `ADDED(i)` keys on the host's own row `card049: host t=\d+ Add\w*Objective\((?:\w+, )?i(?:, \w+)?\)`, never on the
  `host sent` row (that row is capped by `c49_line()`, card 262). The scenario gates on the Adds of 7 objectives
  (Beach_SO3/SO4/SO5, Resort_SO4, Satcom_SO1/SO2/SO3, 863 lines); no line keys on `host sent Add` any more. Checked on
  run 5's host log: one row each for Beach_SO3/4/5, Satcom_SO1/SO2; Resort_SO4 matches as the parent slot of
  `AddSubObjective(Resort_SO4, Resort_SO4_A)` (it has no Add of its own; the old regex matched it the same way).
- Shot tags: class initials + instance number, asserted to fit 40 chars with the run prefix (longest now 35).
- Satcom: the kill rounds are no longer `when`-gated on the Add row; a missing Add row voids the run first.

Satcom block, each step and the row it waits on (table rows 16-19):

| Step | Action | Waits on (host unless said) | Missing row |
|---|---|---|---|
| S1 | none | `card049: host t=\d+ Add...(Satcom_O1, Satcom_SO1)` timeout 60 | void (`satcom-add`) |
| S2 | stand + look + E on `BP_Switch_C_3` | Satcom_SO1 Complete row (card049 sent / card097 subsystem); client replay row | press retries |
| S3 | none | `card049: host t=\d+ Add...(Satcom_O1, Satcom_SO2)` timeout 10 | void (`satcom-so2-add`) |
| S4 | both to `ActorReference_C_11`, 25 kill rounds (SetWaves_C_9/C_14 + horde) | each round gated on the Satcom_SO2 Complete row; then that row timeout 120, client replay | void (`satcom-so2`) |
| S5 | none (wave end: C_9 EncounterDelegate starts dropship C_1) | `card049: host t=\d+ Add...(Satcom_O1, Satcom_SO3)` timeout 10 | void (`satcom-so3-add`) |
| S6 | none | `collision: host/standalone DescendToLandCompleted(... BP_PlayerTransportDropship_C, LocalRole 3) player-inside flag 0 -> 0` timeout 120 (the opening ride logs `1 -> 0`, so it cannot match) | fail, continue |
| S7 | `host mark`; up to 3 x (host warp/look/E on `BP_PlayerTransportDropship_C_1`, client warp 600) | Satcom_SO3 Complete row, timeout 10 each | next try |
| S8 | none | `collision: host/standalone EnterDropship(... BP_PlayerTransportDropship_C, LocalRole 3) player-inside flag 0 -> 1` since-mark | fail, continue |
| S9 | none | Satcom_SO3, Satcom_O1 Complete rows + client replays | fail, continue |
| S10 | client look + E on the ship | client `collision: client EnterDropship` or client WB_TrooperLevelEnd row, since-mark, timeout 15 | fail, continue |
| End | objectives + digest round, then END | host `card018: host SetInputMode_UIOnlyEx ... WB_TrooperLevelEnd` (EndLevel = CallFunctionWithDelay 13.0 after Satcom_O1), Next until `card052 ... 3D_Frontend`, client reconnect, Browse FED_C3M2, both `card052 ... world FED_C3M2` | fail, continue |

Predictions run 6:
1. 19 of 19 objectives complete on host and client (rows on both sides).
2. Host shows WB_TrooperLevelEnd (card018 row), about 13 s after the Satcom_O1 row.
3. Both host and client log `card052: t=... world FED_C3M2`.
4. Digest mismatches only of the classes carded in 251, 253, 254, 256, 258 and 261.
5. No VOID row; plants as in run 5 (prediction 1 of probe 3).

## Run 6 result (dll 61cf21ee94ecdd3b, logs c242-c3m1-{host,client}.log, drive logs/c242-c3m1.drive)

Ended by the scenario after 1031 s, `scenario passed`, drive rc 0; host rc 0, client rc 0; FATAL/ERROR 0;
WARN host 234, client 134; no dumps; 0 VOID rows.

Predictions:
1. Confirmed. 19 of 19 on both sides; the card097 `ObjectiveComplete` sets are identical (32 names incl. lettered
   children and markers, `diff` empty), each host Complete has a client `replayed` row.
2. Confirmed. Host `card018 ... WB_TrooperLevelEnd` at t=206201972, 14.1 s after Satcom_O1 (206187866).
3. Confirmed. `card052 world FED_C3M2`: host t=206269554 (net mode 2), client t=206274615 (net mode 3).
4. Confirmed. Mismatches only in digest rounds 10-12, 9 each, all `BP_SplineRiderMonorail_C` location (card 261);
   every other round 0.
5. Confirmed. No VOID; plants A/B/C each complete on the first E hold (Resort_SO3_A/B/C at host t=205375726,
   205399303, 205425863).

Satcom chain, frame windows (host sent t / client applied t):

| Row | Host | Frame | Client |
|---|---|---|---|
| Add Satcom_SO1 | 206000298 | 64366 | 206000360 |
| Press BP_Switch_C_3: Add SO2 | 206059846 | 68305 | 206059864 |
| Complete SO1 | 206059847 | 68306 | 206059865 |
| Wave end: Add SO3 | 206152300 | 78665 | 206152328 |
| Complete SO2 | 206152301 | 78666 | 206152328 |
| C_1 landed (`DescendToLandCompleted ... 0 -> 0`) | 206173943 | - | - |
| Host boards (`EnterDropship ... 1 -> 1`) | 206187871 | - | - |
| Complete SO3 | 206187866 | 82026 | 206187880 |
| Complete Satcom_O1 | 206187866 | 82027 | 206187881 |
| WB_TrooperLevelEnd | 206201972 | - | none (card 260) |

Satcom_SO2 kill phase 92.5 s (206059847 to 206152301). The card049 line cap hit again at host t=206053115 (line
257185); after it no `host sent Add` row for SO2/SO3 although frames 68305 and 78665 went out and the client replayed
both (card 262 answer holds).

Drive TRY-FAILs other than kill tries on absent actors: line 1936 (S8) waited for `player-inside flag 0 -> 1`, the
host logged `1 -> 1` at the boarding (controller possesses the ship; my S8 regex was wrong, not a mod fault); lines
1944/1956: the client got no EnterDropship and no WB_TrooperLevelEnd (client E at 206201684, after the host had
boarded; card 260 class); lines 1975-1978: Next/continue ladder rungs, Browse FED_C3M2 at 206264777; lines 100/109:
Resort_SO2 retry rungs (complete at 205402290).

WARN classes: of the card 262 rows, host `OnHit of ADProjectile, whose GrenadeExplode already went` 9 and client
`decal pool at its cap` 1 again; `fx watch full`, `no copy of host 245419` and the pose freeze row are absent. Host
WARN +14 against run 5 come from the Satcom kill rounds (`kill actor ... no such actor`, 176 rows in all). Run 5's
logs were overwritten by run 6 (same path), so the per-class diff against run 5 uses the run 5 list in this card.
