# 243: Full-objective soak, FED_C3M2
Status 2026-11-22: open, parked until after the user's playtest.

Same shape and rules as card 240 (read `docs/cards/done/240-*.md` or `docs/cards/240-*.md` first: no
`objective complete`, wait on the host Complete row and the client replay, both players in presence volumes,
objectives dumps at the end, faults become their own cards).

Goal (one): one scenario that plays FED_C3M2 from spawn to the mission end and completes every objective by
its own trigger.

Objective list M = 28, from the game's data table `DT_LevelObjectiveData_FED_C3M2` (keys
`FED_C3M2_<id>_NAME`; the runtime ids may differ in case):
HYDRODAM_O1
HYDRODAM_SO1
HYDRODAM_SO2
HYDRODAM_SO3
HYDRODAM_SO4
HYDRODAM_SO5
HYDRODAM_SO6
MONORAIL_O1
MONORAIL_SO1
MONORAIL_SO2
MONORAIL_SO3
MONORAIL_SO4
POWERPLANT_O1
POWERPLANT_SO1
POWERPLANT_SO2
POWERPLANT_SO3
POWERPLANT_SO4
POWERPLANT_SO5
POWERPLANT_SO6
SECRET_O1
SECRET_SO1
SILO_O1
SILO_SO1
SILO_SO2
SILO_SO3
SILO_SO4
SILO_SO5
SILO_SO6
The last soak (c235-c3m2) sent 0 sub-objective completions: none

Step 1, static, no run, appended here as a table: each id in play order with parent, counter, real trigger
with level-script or class evidence (`FED_C3M2_Scripting`), the driver steps that fire it, the synced
follow-ups to wait on, and any id that is a marker only or on a branch the map never activates (state the
evidence; such an id leaves M only with the senior's agreement).

Step 2: `tools/scenarios/c243-c3m2.txt` (new file; older scenarios stay).

Step 3: run, budget 3 runs. Command:
`systemd-run --user --scope -q -p MemoryMax=12G env SCENARIO=tools/scenarios/c243-c3m2.txt DEPLOY=1 FAST=1 HOST_EXTRA_ENV="UBWCOOP_TRAVEL=/Game/Maps/FED/FED_C3M2?listen UBWCOOP_GOD=1 UBWCOOP_C132_FX=1 UBWCOOP_C136_LOG=1" CLIENT_EXTRA_ENV="UBWCOOP_C132_FX=1 UBWCOOP_C136_LOG=1" tools/run_pair.sh c243-c3m2 3600`

Files allowed: `tools/scenarios/c243-c3m2.txt`, this card. Driver verbs only after a report to the senior.
No other mod file. No build while a game runs.

Done when: final objectives dump shows `complete 1` for M of M on host and client, lists match, both sides
reach the next map (FED_C4M1), 0 crash, every digest round 0 mismatches, frame
windows at or over 500 outside load. Report: completed N of M, each fault with its lines, new WARN classes
against c235-c3m2.

## Rules carried from cards 241 and 242 (read `docs/cards/done/242-full-objective-soak-c3m1.md`)

- Step 1 also covers: each encounter manager chain (start trigger, end condition, spawners, start and end log
  rows), each presence or hold volume by actor name and position, each switch or panel with its interaction
  box and the side to stand on, each timed hold with its time and cancel conditions, and the mission end
  (what completes the last top objective, delay to `EndLevel`, level-end widget steps).
- Scenario from a generator, under 2000 steps, counted before the run. Every block has a fail-fast root: an
  unmet gate voids the run, it does not skip. "Added" gates wait on the `card049: host t=N Add...` row, never
  on the `host sent Add` row (line cap 240, card 262). Stands use `wait stand` (void on timeout).
- Before the first run: audit the whole scenario against the step 1 table and write the predictions.
- Probes: a run meant to void within 15 minutes does not count against the 3-run budget, at most 3, each with
  a logged cause and a changed prediction in this card.
- Digest: mismatches only of carded classes (251, 253, 254, 256, 258, 261). The client level-end screen is
  card 260. Turret rows `no turret matches` are card 233.
- **Aim proof per teleport (user rule 2026-11-13).** After every warp and look at an objective target (switch,
  pickup, hold site, nest, clasp, dropship), the scenario takes a `shot` before the action, named after the
  step, and the junior looks at each one: the target is in view at the crosshair, not a wall. The report lists
  each shot with what it shows; a shot of a wall voids that step's result. The senior spot-checks the shots.
- Report after every run in the full form, then go on with the open items.

## Step 1 result (static, no run; junior, 2026-11-13)

Evidence: `FED_C3M2_Scripting` ExecuteUbergraph disassembly (offsets below), `BP_MissleSilo`,
`BP_MissileSilo_Clasp`, `BP_DestructibleSpawner_Bugnest_HumanGameplay`, `BP_SecretPickup` (no bytecode),
the objective DT and the exe strings. Positions are static reads of root components in the scripting umap;
the scenario goes to actors by name, and the runtime look line checks each stand.

Play order: the three areas are open from the start (Monorail, PowerPlant, Silo); the Dam opens when all
three area O1 are complete (OnLevelObjectiveComplete_Event_0 at 6409 runs Setup 3 - The Dam).
Scenario order: Monorail, PowerPlant, Silo, Dam.

### Objective table

| # | Id | Parent, counter | Real trigger (evidence) | Driver steps | Synced follow-up / wait rows |
|---|---|---|---|---|---|
| 1 | MONORAIL_O1 | parent | added at start; completed at 15628 after EM SetWaves_C_23 ends (chain below) | survive chain, kill rounds | Complete Monorail_O1; SO4 by cascade |
| 2 | MONORAIL_SO1 | O1 | PlayerPresence_C_51 enter (20784) runs Objective_Monorail_SO2 (21180), completes SO1 at 21256 | presence C_51 both players | Complete SO1; SO2 count rows |
| 3 | MONORAIL_SO2 | O1, count 4 (A=Bugnest_C_0, B=C_9, C=C_1, D=C_5) | each nest BuildingDestroyed (2531/2444/10735/10773); count parent auto-completes | player fire / grenade at each nest's bounds, `nuke_shot` pattern from card 241 | 4 count rows, then Complete SO2 |
| 4 | MONORAIL_SO3 | O1 | added by Setup 3 - Monorail on SO2 complete (5261); completed by BP_WallSwitch_C_1 (23472, completes at 23486) | `use` (E) on BP_WallSwitch_C_1 (pickup-style collider, not `press`) | Add SO4; EM SetWaves_C_11 + Loop_C_15 start |
| 5 | MONORAIL_SO4 | O1 | added in Setup 4 by the wall switch; completed by cascade when O1 completes (15628) | encounter chain C_11, C_22, C_23 | Complete O1 row; cascade row SO4 |
| 6 | POWERPLANT_O1 | parent | added at start; TankersKilled >= 2 (11287 delegate, 10830) completes O1 at 11258 | kill rounds on tankers of EM C_50/C_30 | Complete O1; SO5 by cascade |
| 7 | POWERPLANT_SO1 | O1 | PlayerPresence_C_52 (24446, 15107) enables Switch_C_2, Setup 3_02 (10811) completes SO1 at 2373 | presence C_52 (about -31374,-5970,1930) | Add SO2 |
| 8 | POWERPLANT_SO2 | O1 | BP_Switch_C_2 OnPressSuccessful, Plant_Generator_Reset (14743) completes SO2 at 14848 | `press` Switch_C_2, stand (-64440,6149,1728) | Add SO3 count 2; Switch_C_6/C_8 enabled |
| 9 | POWERPLANT_SO3 | O1, count 2 (DoorSwitch1/2) | Switch_C_6 completes DoorSwitch1 (15145), Switch_C_8 DoorSwitch2 (15183); count completes | `press` Switch_C_6 stand (-11590,-1145,3369), Switch_C_8 stand (-40129,54428,1820) | 2 count rows; Complete SO3 (15316) |
| 10 | POWERPLANT_SO4 | O1 | step 2 correction: the Add (AddSubObjective PowerPlant_SO4 at 9122) is inside SetupObjective 3_04 (event entry 15584, goto 9102), and 3_04 has no caller anywhere (its name occurs once, the function header). The complete at 2482 is reached only from EM SetWaves_C_40 EncounterDelegate (6949), and C_40 is triggered only at 16823 by DoorsClosed_NextEncounterDelay, which is scheduled at 9173 inside 3_04. So SO4 is never added and never completed; old runs c234/c235 show only SetObjectiveVisibility(PowerPlant_SO4) | none (a 5 s try wait on its Add row records it) | open point to the senior: leaves M (M = 24) |
| 11 | POWERPLANT_SO5 | O1 | added in 3_05 (9782); 3_05 runs from OnLevelObjectiveComplete on SO3 (check at 15316, 15422), which also sets PowerPlant_RearComplete = true (15436); completed by cascade on O1 (11258) | presence C_53 (only when RearComplete) starts 18069 chain; kill tankers | cascade row SO5 |
| 12 | POWERPLANT_SO6 | O1 | never added: the ubergraph only sets marker visibility for it, no Add/Complete call | none | proposal: leaves M (marker only) |
| 13 | SILO_O1 | parent | panel press (27793), missile fire (15897), 10 s dialogue, CompleteObjective(Silo_O1) at 4531 | `press` PanelSwitch_WithoutScreen_C_1 stand (-24445,-13482,2030) | Complete O1; SO6 by cascade |
| 14 | SILO_SO1 | O1 | PlayerPresence_C_2 enter (2222) completes SO1 at 2148, Setup 2_02 adds SO2, EM C_20/C_32 | presence C_2 | Add SO2 |
| 15 | SILO_SO2 | O1 | BP_Switch_C_3 (5595): hatches open and jam, Setup 2_03 adds SO3 count 3, completes SO2 at 1467 | `press` Switch_C_3 (static transform unsure: Scene at -82064,3855,408 yaw -106.9; runtime look line decides) | Add SO3 count 3; SetWaves C_21/1/19/18 |
| 16 | SILO_SO3 | O1, count 3 (Lock1/2/3) | player damage on each silo's BP_MissileSilo_Clasp child: HealthComponent damage (export 1938), 1660 Open Hatch, "Hatched Opened" bound at 24649-24800 to MissleSilo_C_4/C_3/C_0, completes Silo_1/2/3 lock | fire at each `BP_MissileSilo_Clasp_C` bounds (LMB, `look actor ... bounds`) | 3 count rows; Silos complete check (6261) |
| 17 | SILO_SO4 | O1 | Setup 2_04 adds it and enables Switch_C_0; Switch_C_0 (6386) completes SO4 at 1505 | `press` Switch_C_0 stand (-53624,897,1956) | Add SO5; EMs C_45, Loop_C_21/C_0/C_22 |
| 18 | SILO_SO5 | O1 | timed hold: SilosStage2 45 s, Stage3 30 s, Stage4 15 s; Stage4 runs Setup 2_06 and completes SO5 at 2255 | `hold` 90 s near silos, gated on the Complete SO5 row | Add SO6; panel enabled |
| 19 | SILO_SO6 | O1 | added in 2_06; completed by cascade on Silo_O1 | panel press (row 13) | cascade row SO6 |
| 20 | HYDRODAM_O1 | parent | added at 6409 when all area O1 complete; evac dropship C_2 interact completes O1 at 13180 | `use` on BP_PlayerTransportDropship_C_2 (about -51630,39808,760) after it lands | Complete O1; SO6 by cascade; NukeExplosion 13.5 s; EndLevel 21.5 s |
| 21 | HYDRODAM_SO1 | O1 | Dropship_PowerPlant_Start / Dropship_Silo_Start add SO1 on dropship C_1 / C_0; boarding completes it | `use` on the dropship the Add row names; BOARD rows | Complete SO1 |
| 22 | HYDRODAM_SO2 | O1 | dropship landing: 116/336 path runs 4_03/4_04 and completes SO2 (298/393) | wait LANDED row | Complete SO2; Add SO3/SO4 |
| 23 | HYDRODAM_SO3 | O1 | left side: presence C_12 (about -16140,-29309,11626) starts EM C_51 and opens Door_Gigantic_C_12; EM end 12026/12098 completes SO3 (3727/12162) | presence C_12, kill rounds | Complete SO3 |
| 24 | HYDRODAM_SO4 | O1 | right side: presence C_14 (about -62225,4852,3113) starts EM C_53 and opens Door_C_9; EM end 12239/12311 completes SO4 (12375/12427) | presence C_14, kill rounds | Complete SO4 |
| 25 | HYDRODAM_SO5 | O1 | Dam_Warriors/Jumpers/Centurion1/2 dead (EM C_31/C_44/C_12/C_4, 12512-12607) completes SO5 at 12612 | kill rounds incl. two centurions | Complete SO5; Add SO6 (4_06) |
| 26 | HYDRODAM_SO6 | O1 | added in 4_06 on dropship C_2; completed by cascade on O1 (13180); evac arrives after EvactuationArrivalDelayedFunction (90 s, started in 4_05) | wait evac landed, then row 20 | cascade row SO6 |
| 27 | SECRET_O1 | none in script | no Add or Complete anywhere: absent from the scripting umap, the level umap and the exe; `BP_SecretPickup` has no bytecode (strings INTERACTMSG_SECRET, PICKUPMSG_Secret, tag Secret.BigBugs); the level's BP_SecretPickup_C_2 pickup delegate (16560) only triggers EM Loop_C_4/C_5 and audio; native ASTSecretPickup::OnInteraction goes to AADPickupBase::Pickup | none | proposal: leaves M (DT row only) |
| 28 | SECRET_SO1 | SECRET_O1 | same evidence as row 27 | none | proposal: leaves M |

Proposal for the senior: SECRET_O1, SECRET_SO1, POWERPLANT_SO6 leave M, so M = 25. The scenario still
picks up BP_SecretPickup_C_2 once (E) and logs the result, so a runtime Add would show.

### Encounter chains

| Area | Start | Chain | End action |
|---|---|---|---|
| Monorail | wall switch (23472): SetWaves_C_11 + Loop_C_15 | C_11 end (23560) triggers C_22; C_22 end (23597) triggers C_23; C_23 end (23634) ends Loop_C_15 | 15614: Dialogue02_Monorail_Complete, Complete Monorail_O1 (15628); 5 s MonorailComplete delay; C_51 disabled (11585) |
| PowerPlant | SO3 complete: SetWaves_C_10/C_9, stops Loop_C_6/C_11 | presence C_53 (RearComplete only): EM C_6, then 18069: SetWaves C_5, Loop_C_8, SetWaves C_29, C_25; C_29 end (21862) triggers tankers C_50 + C_30 | 2 tankers dead, Complete PowerPlant_O1 (11258) |
| Silo | SO1: C_20/C_32; SO2: C_21/1/19/18; SO4: C_45, Loop_C_21/C_0/C_22 | timed stages 45/30/15 s | panel enabled |
| Dam | presence C_12: EM C_51; presence C_14: EM C_53 | Warriors C_31, Jumpers C_44, Centurions C_12/C_4 | SO5, evac dropship C_2 |

### Volumes (static, scripting umap)

C_51 Monorail SO1 (about -25452,-7955,2598); C_52 PowerPlant SO1 (about -31374,-5970,1930); C_53
PowerPlant rear (about -35498,-47009,11151); C_2 Silo SO1 (about -31630,-5560,1597); C_12 Dam left
(about -16140,-29309,11626); C_14 Dam right (about -62225,4852,3113). Some of these reads look close to
each other (C_2 vs C_52); the scenario uses `presence` by actor name, not these numbers.

### Switches and stand side

Stand = origin + 170 times yaw forward, z + 100; card 242 void check on the eye from the look line.
Switch_C_2 yaw -181.7; Switch_C_6 yaw 181.2 (pitch 21.7 read, checked at runtime); Switch_C_8 yaw 14.1;
Switch_C_0 yaw -1.4; Panel C_1 yaw 90; Switch_C_3 unsure (see row 15); WallSwitch_C_1 by E.

### Holds

Silo stages 45 + 30 + 15 s (gate on Complete SO5, cap 150 s); Dam evac 90 s (gate on the evac landed row,
cap 180 s); Monorail 5 s MonorailComplete delay; PowerPlant DoorsClosed 5 s (9173).

### Mission end

Evac dropship C_2 interact, Complete HydroDam_O1, NukeExplosion at 13.5 s, EndLevel at 21.5 s; END block
from card 242: level-end Next clicks, frontend, Browse to FED_C4M1 on both sides, objectives dumps, digest.

### Not reused

c234-c3m2 has no objective steps; its turret warp is faulty and is not copied. Old runs c234/c235 logged
no objective completes.

## Left out of M (senior, 2026-11-13: M = 25)

SECRET_O1, SECRET_SO1 and POWERPLANT_SO6 leave M because the game never adds them. If any run logs an Add
for one of them, it returns to M and must be completed. Evidence lines (host logs c234-c3m2, c234-c3m2-r2,
c235-c3m2):

- `grep -ciE "Secret_(O|SO)1"`: 0 in each of the three logs (no Add, no Complete, no visibility row).
- PowerPlant_SO6 appears only as visibility rows, for example c235-c3m2-host.log:
  `card049: host t=155739435 SetObjectiveVisibility(PowerPlant_SO6) bools 11, count 0, actors -1, depth 0, ctx FED_C3M2_Scripting_C, objectives now 14`
- Add rows in all three logs are only: Monorail_O1, PowerPlant_O1, Silo_O1, the three areas' markers and SO1,
  Monorail_SO2 (count 4, `AddCountSubObjective_SubObjectives`) and Monorail_SO2_A to _D.

The scenario keeps one E pickup of BP_SecretPickup_C_2 and asserts before the mission end that no Add row
exists for the three (line `void unless host log "cmd: \d+ shot \S*c243-host-end" before host log ...`).

Open point (step 2, static): POWERPLANT_SO4 is never added either (row 10: SetupObjective 3_04 has no
caller). It stays in M until the senior decides; the runs record whether its Add row ever appears.

## Step 2 result (junior, 2026-11-13)

Generator: scratchpad `gen_c243.py` (from `gen_c242.py`); output `tools/scenarios/c243-c3m2.txt`,
**1620 steps** (drive.py `load()` accepts it, MAX_STEPS 2000). Kill rounds are 12 s each (sleep 12, card 242
uses 6) at half the count, to stay under the step cap with the same wall time.

Add rows are matched by `Add\w*Objectives?\(`: the count parents log `AddCountSubObjective_SubObjectives(`,
which `Add\w*Objective\(` does not match (found in the c235 log while writing step 2).

### Settle points (senior)

- PowerPlant RearComplete: set at 15436, inside the SO3-complete path (OnLevelObjectiveComplete check at
  15316, 15422 runs SetupObjective 3_05: Add SO5 at 9782, RearComplete = true). It is also set at 2519
  after the SO4 complete at 2482, but that path needs EM C_40, which never runs (row 10). Trigger row for
  the rear: Complete PowerPlant_SO3. SO4 inside 3_05: nothing completes it, because it is never added;
  the scenario does a 5 s try wait on its Add row and Complete row only to record it.
- Silo SO5 hold: timers only. 1772 CallFunctionWithDelay(45, SilosStage2), 3990 (30, Stage3), 4073
  (15, Stage4); no presence volume, no condition and no clear/cancel call on those timers, so nobody must
  stand anywhere and nothing resets it. Both players stay at the panel (BP_PanelSwitch_WithoutScreen_C_1)
  and clear waves (10 rounds of 12 s, gated on Complete Silo_SO5, wait cap 150 s).
- Silo clasps: player rifle fire. Host warps 900 uu from MissleSilo_C_4 / C_3 / C_0, `look actor
  BP_MissileSilo_Clasp_C 0 bounds` (nearest clasp, bounds centre), LMB 3000 ms, up to 5 tries per lock.
  The mod logs no separate "Hatched Opened" row; the delegate's follow-up is the card049 Complete row
  `CompleteSubObjective(Silo_SO3, Silo_SO3_LockN)`, so lock k waits for k rows matching `Silo_SO3_Lock\d`
  (the evmap Lock-to-silo pairing may be shifted by one, so the scenario counts rows instead of names).
- Dam: area dropship by CombatIslandCounter (Silo last = 30, Dropship_Silo_Start, BP_PlayerTransportDropship_C_0);
  evac BP_PlayerTransportDropship_C_2. Client boards first on both (`client key E`, then a 10 s wait on
  `collision: client EnterDropship` since its mark), then host E until the Complete row. A missing client
  row is reported with its lines (card 260 class), not worked around.
- Bugnests: card 241 nuke_shot pattern. Correction (run 5 audit): C3M2 has 2 placed BP_AmmoPickup_NukesSmall_C
  at (-63464, 33722, 693) and (-63478, 33702, 697), beside BP_WeaponPickup_NukeLauncher_C (-63424, 33781, 706);
  each gives 1 Nuke. The card098 tally placed=4 counts 2 colliders per pickup (PickupCollider + PlayerCollider).
  Other ammo: Autocannon/EPulse/Marksman/Railgun/Frag. Host warps 1500 uu short, `look actor <nest> 0 bounds`,
  LMB 3000 ms, `key G 200`, R; up to 6 tries per nest. Per nest the wait row is the Complete
  `Monorail_SO2_<letter>` row, sent directly by BuildingDestroyed (2531/2444/10735/10773).

### Scenario audit against the step 1 table

| Block (lines) | Rows driven | Fail-fast root (void check) | Rows waited on |
|---|---|---|---|
| Start (1-18) | setup | `c243-start-add-check`: Add Monorail_SO2 | Add Monorail_SO2 (120 s) |
| Monorail (19-493) | 1-5 | `nest-a` (Complete SO2_A), `mono-so2`, `mono-so3` | Complete SO1, SO2_A..D, SO2, Add SO3, Complete SO3, O1 (120 s), SO4 cascade; client replay of each |
| PowerPlant (494-815) | 6-11 | `plant-so1`, `plant-so2`, `plant-so3` | Complete SO1, SO2, DoorSwitch1/2, SO3; Add SO4 (5 s probe), Add SO5; Complete O1 (120 s), SO5 cascade |
| Silo (816-1133) | 13-19 | `silo-so1`, `silo-so2`, `clasp-1` (one Lock row), `silo-so3`, `silo-so4`, `silo-so5`, `silo-o1` | Complete SO1, Add SO2, SO2, Lock rows 1..3, SO3, Add SO4, SO4, SO5 (150 s), O1, SO6 cascade |
| Secret (1134-1142) | pickup | none (one try) | card020 Interact row naming SecretPickup |
| Dam (1143-1631) | 20-26 | `dam-add` (Add HydroDam_SO1), `dam-so1`, `dam-so2`, `dam-so5` | Add SO1, landed row since mark, client EnterDropship, Complete SO1, SO2 (300 s), SO3, SO4, SO5, evac landed, Complete O1, SO6 cascade |
| Tail (1632-1642) | dumps | end assert: no Add for Secret_O1/Secret_SO1/PowerPlant_SO6 | digest round sent and client round row, objectives dumps |
| End (1643-) | level end | host level-end widget wait (hard) | level-end rows, frontend, Browse to FED_C4M1, world FED_C4M1 on host and client |

Every kill-round and stand block is gated on its own Complete row (it stops once the row exists) and on
the objective's Add row (`when host last`), as in card 242.

### Predictions (run 1)

- Host and client: 24 of 25 complete; POWERPLANT_SO4 is not added (row 10), so it is missing on both sides,
  with no Add row. Every other id: host Complete row (or the card097 cascade row) and the client replay.
- No Add row for Secret_O1, Secret_SO1, PowerPlant_SO6 (end assert passes).
- Host level-end widget row, then both sides log world FED_C4M1; 0 crash.
- Digest rounds: mismatches only in carded classes (251, 253, 254, 256, 258, 261).
- Possible weak points: Silo Switch_C_3 stand (static transform unsure, the look-line void check decides);
  bugnest kills by rifle and grenade without nuke ammo; client dropship boarding row.

### Run 1 result (c243-c3m2, voided at line 86)

- 1 of 25 on both sides (Monorail_SO1). No level end, no travel, 0 crash. 5 digest rounds, 0 mismatches.
- Frame windows under 500 outside load: host 0, client 1 (485 at t=207891305). No new WARN classes.
- Void cause: `look actor BP_BugNest... 0 bounds` aims at the bounds centre, but the nest bounds extent is
  11077 uu, so the aim point was far off the mesh. Fix: `look actor <nest> 200` (origin + 200, where the
  NS_FX_BugsSpawnerSmallSpawning rows show the spawner).

### Probe 1 (c243-probe1: start + monorail to the SO2 fail-fast)

- Monorail_SO2_A (C_0) died on the first try from the -X warp at 1500 uu. Correction (probe 2/3 review):
  Monorail_SO2_C (C_1) did not complete in probe 1; only A did.
- Monorail_SO2_B (C_9, origin -36684,51368,773) and Monorail_SO2_D (C_5, origin -44607,40698,634) took 6
  tries each from the -X side with a correct aim (yaw -8 at 1503 uu for B) and lived; the spawn FX kept firing.
- Change: tries 2-6 shoot from +X, +Y, -Y at 900 uu (`warp point` at origin z + 300). Superseded by probes 2 and 3 below.

## Probe 2

- logs/c243-probe2-*.log: ended by scenario after 486 s, drive rc 2, 0 crash, WARN host 17 / client 29.
- Completed: Monorail_SO1, Monorail_SO2_A (grenade). B/C/D side tries were void: `host warp point x y z` is not
  a warp form (testrun.cpp: `warp actor|client point <x> <y> <z>`); fixed to `host warp actor point`.
- C try 2 (rifle): warrior bugs meleed and pushed the host; hits landed near the pawn. Rifle fire never kills a nest.

## Probe 3

- logs/c243-probe3-*.log: ended by scenario after 417 s, drive rc 0, 0 crash, WARN host 3 / client 14.
- Nest mechanics: a nest dies only to a blast in its hole. A died to the frag grenade from the +X stand at 900 uu
  (MS_GORE_BugNest_GrenadeHole_Sploosh, then EMIT_BugNestMedium_Destroyed 1 s later). The host has 2 grenades.
- Nuke launcher (BP_WeaponPickup_NukeLauncher_C, -63424,33781,706): picked up on the first E. On pickup both
  BP_AmmoPickup_NukesSmall_C beside it are taken at once (Nuke 0 to 1 to 2, max 2), so 3 nukes in all
  (1 loaded + 2). After the 3rd shot the empty launcher drops as a runtime pickup and the Morita returns.
  Later refill warps onto NukesSmall did nothing.
- B: shot 1 from the 3000 uu actor warp burst at (-39196,49729,929), 40 uu in front of the pawn (cover);
  shot 2 from the +X point stand at 2500 uu hit (-36504,51379,966) and killed B.
- C: shot 3 from the 3000 uu actor warp burst 200 uu in front of the pawn; out of nukes, then the Morita did
  nothing. D: no nuke left, not completed. Completed and replayed: Monorail_SO1, SO2_A, SO2_B.
- Change (scenario 1613 steps, client fallback added per senior): no 3000 uu actor warp for nukes. B, C, D: point stands +X, +Y, -Y at 2500 uu
  (origin z + 300), one shot each; then a grenade fallback: warp onto the nearest BP_GrenadeAmmoPickup_Frag_C
  (refills the grenades A used), G from the 900 uu stands +X and -Y. All probes (3 of 3) are used.
  Client fallback (senior, before run 2): the host warps the client to the 900 uu stands +X, -Y; the client aims by
  `client look point` at origin z + 200 and throws G. A kill there also proves client damage on a nest reaches the host.

## Run 2 (junior, 2026-11-13)

Scenario 1613 steps, client fallback, no aim shots (the shot rule came after its launch). Ended by scenario after
359 s, drive rc 1: VOID at line 239 (`void unless ... Monorail_SO2_D ...`). Crash 0, FATAL/ERROR 0, map loaded 2 per side,
no level end, no travel.

- Complete, host and client replayed: Monorail_SO1, Monorail_SO2_A (host frag from the 1500 uu warp), SO2_B (nuke
  from the +X 2500 stand), SO2_C (host frag from the -Y 900 stand). 4 of 25 per side.
- D void cause: the launcher held 1 nuke. The NukesSmall refill warp was dropped from the generator in the rewrite
  (no `AmmunitionType.Nuke` row in the host log). The D frag refill went to the nearest pickup, the used Frag_C_2,
  so G had no grenade.
- Client fallback: client `key G` reached the client (`rot: input window opens on key G`, `cmd: 2 key G 200 -> done`
  at 210015298, `cmd: 4 key G 200` at 210025665), but no grenade ability, throw or GrenadeSpawn on either side
  (host log 210014000 to 210032000). Client nest damage not proven; possible mod gap (client grenade throw), reported.
- Digest rounds 1 to 10: 0 mismatches, classes -. Round 8: 1 slot differing.
- Frame windows under 500 outside load: host 15 of 34 (256 to 404 frames, 209996610 to 210136784), client 18 of 33
  (61 to 337). Probe 3 on the same route: host 1 of 39. No logged cause found.
- New WARN classes against c242-c3m1 and c234-c3m2: host 0; client 1, `card152: client bone check ... (settled final)
  / (frozen final)` on guid 0x1fc (pelvis 87.7 uu from the mesh component).
- Shots (Saved/Screenshots/Windows): `host-c243-c3m2-c243-start-add-check.png`, `host-c243-c3m2-c243-nest-a-check.png`,
  `host-c243-c3m2-c243-mono-so2-check.png`, `host-c243-c3m2-fail-line239.png` (nest D under the crosshair, HUD "BUG
  NEST", grenades x0), `client-c243-c3m2-fail-line239.png` (client shows the OPERATION OBSIDIAN "press any key" card).

Fix for run 3 (gen_c243.py): after the launcher, two `try host warp actor BP_AmmoPickup_NukesSmall_C 10` and a wait on
`AmmunitionType.Nuke asked 1, 1 to 2`; the frag refill warps by point to both pickups (-31157,-4498) and
(-21840,-40283). Aim shots added per the 2026-11-13 rule: 161 shots, scenario 1877 steps.
A second aim proof (a log row naming the trace target under the crosshair) needs a mod change: testrun.cpp has no
crosshair trace row. Not added.

## Run 3 (junior, 2026-11-13) — VOID at line 628 (PowerPlant_SO2)

Client card fix: the HEAD now waits for `card135: client t=N screen prompt ready`, sends
`client ui continue` (the c135-r2 pattern), waits for `card067: dump 3 in FED_C3M2 (WB_Stack_C RequestGameUnpause)`
(client 210647854), and then takes `client shot c243-client-ingame`. That shot shows the HUD and the objectives with no card.
The void order held: client row 210647854 came before the shot cmd at 210647960.
A first launch of run 3 was stopped (senior order). It does not count; its fail-line239 shots show the client still on the card.

- Result: VOID after 468 s. drive rc 1. host rc 0, client rc 0, no crash dumps. FATAL/ERROR 0. WARN host 26, client 48.
- N of M (M = 25): host 10, client 10. These are Monorail_SO1, SO2_A, SO2_B, SO2_C, SO2_D, SO2, SO3, SO4, O1 and PowerPlant_SO1.
  The non-M rows Monorail_Marker and Silo_SO1 also completed on both sides.
- Level end, travel, FED_C4M1: not reached.
- FAIL: line 628 `void unless host log ...PowerPlant_SO2... before ...c243-plant-so2-check`.
  - Cause: the stand point (-64440, 6149, 1728) for BP_Switch_C_2 is wrong. At runtime, BP_Switch_C_2 sits at (-24445, -13652, ~1930):
    - host `cmd: 157 look: BP_Switch_C at -24445 -13652 2090 from -64440 6151 1807: ... 44631 uu`;
    - `cmd: 156 warp: ... 383 uu from BP_Switch_C_2` put the client at (-24521, -13289, 2030);
    - the objective marker in the shots reads ACTIVATE 446.56 m.
  - The host stood at the missile-launch console and pressed E twice. Both presses gave
    `card020: host host-own UADInteractorComponent::Interact(owner BP_PlayerCharacterMI_C): target null (null)`.
  - Scenario gate gap: the line 611 stand void checks only the eye (`from x y z`), not the `at` target position. That is why a look at a target 446 m away passed.
    Fix: stand at (-24445, -13482, 2030), which is the same origin as the Silo_O1 panel stand, and add an `at` span to the void.
    The other press stand points (_6, _8, _3, _0) come from the same source and are unchecked until a look row shows their `at`.
- Client inputs:
  - Client G: not reached in this run (no `key G` cmd on the client).
  - E on the dropship and the level-end screen: not reached.
- Digest: 13 client rounds, 0 mismatches each.
- Frame windows under 500 frames per 10 s, outside load:
  - host: 4 of 41 (421, 377, 367, 491);
  - client: 11 of 41 (456, 480, 487, 168, 126, 46, 14, 10, 20, 58, 118).
  - The client collapse starts at 210937633, after the client was warped to BP_Switch_C_2 (a warrior fight there). Its logged cause, in the client frame rows:
    - `util fallback live` rose to 267, against a window peak of 512 (the cap);
    - `untracked` rose 0 -> 855;
    - e.g. `frame window 10330 ms: 10 frames, mean 1033.0 ms ... util fallback live 267, window peak 512, map peak 512, untracked 500`.
- New WARN classes against c242-c3m1 and c234-c3m2:
  - host: `cardN: host hit BP_AI_WarriorBug_C ... impact ... uu from`, `cmd: kill actor <class> -> failed (no such actor)`,
    `attach: DisableThreadLibraryCalls failed`, `config: UBWCOOP_SKIP_FLIGHT needs UBWCOOP_AUTODROPSHIP -- off`.
  - client: `bone check` (frozen final / live / settled), `gib host part ... asleep above / lies N uu from`,
    `client hit <bug> ... host location ... is`, `door BP_Door_Sliding_Medium_C_N client-only DoorState`,
    `turret row BP_MountableTurret_C_N ...: no turret m...`, `recv ... WorldOp NaN ... result skipped: no place`, `attach: DisableThreadLibraryCalls failed`.

### Run 3 shot table (aim shot rule)

Directory: `prefix/drive_c/users/steamuser/AppData/Local/StarshipTroopers_UBW/Saved/Screenshots/Windows/`, files `<role>-c243-c3m2-<name>.png`.

| Step | Target | Shot | At the crosshair | Verdict |
|---|---|---|---|---|
| ingame | client card dismissed | client-…-c243-client-ingame | HUD, objectives, no card | ok |
| ingame | host | host-…-c243-host-ingame | HUD, plaza | ok |
| start | add check | host-…-c243-start-add-check | plaza, objectives listed | ok (check) |
| s004 | Bugnest_C_0 | host-…-c243-s004-nest0 | nest, DESTROY marker on it | ok |
| A check | — | host-…-c243-nest-a-check | nest bursting, 1/4 | ok (check) |
| s008 | nuke launcher | host-…-c243-s008-nukelauncher | launcher crate, PICK UP NUKE LAUNCHER | ok |
| s013 | Bugnest_C_9 | host-…-c243-s013-nest9 | nest in doorway, marker on it | ok |
| s020 | Bugnest_C_1 | host-…-c243-s020-nest1 | concrete wall, nest marker behind | WALL: void step |
| s021 | Bugnest_C_1 | host-…-c243-s021-nest1 | building structure, marker behind | WALL: void step |
| s022 | Bugnest_C_1 | host-…-c243-s022-nest1 | nest at the crosshair | ok |
| s023 | Bugnest_C_1 | host-…-c243-s023-nest1 | wall / dirt mound | WALL: void step |
| s024 | Bugnest_C_1 | host-…-c243-s024-nest1 | nest close, bugs beside it | ok |
| s027 | Bugnest_C_5 | host-…-c243-s027-nest5 | rock, marker behind | WALL: void step |
| s028 | Bugnest_C_5 | host-…-c243-s028-nest5 | nest at the crosshair | ok |
| s029 | Bugnest_C_5 | host-…-c243-s029-nest5 | nest at the crosshair (25 m) | ok |
| s030 | Bugnest_C_5 | host-…-c243-s030-nest5 | nest close | ok |
| SO2 check | — | host-…-c243-mono-so2-check | nest remains, Free the trapped MI | ok (check) |
| s032 | BP_WallSwitch_C_1 | host-…-c243-s032-bpwsc1 | switch panel, ACTIVATE + E INTERACT | ok |
| SO3 check | — | host-…-c243-mono-so3-check | switch, Survive the ambush | ok (check) |
| s035–s040 | arc9 defend point | host-…-c243-s035..s040-arc9 | ceiling above the DEFEND marker (1.3–1.8 m) | presence: not an aim step (defend volume, kill rounds); no aim verdict |
| end mono | — | host-/client-…-c243-end-mono | host: ceiling, trooper; client: plaza, bugs | ok (block end) |
| PP SO1 check | — | host-…-c243-plant-so1-check | plant interior, ACTIVATE 50 m | ok (check) |
| bpsc-2 stand | BP_Switch_C_2 | host-…-c243-bpsc-2-stand | missile console panel; target marker 446 m away | WRONG TARGET: void step |
| s056 | BP_Switch_C_2 | host-…-c243-s056-bpsc2 | same console panel | WRONG TARGET: void step |
| s057 | BP_Switch_C_2 | host-…-c243-s057-bpsc2 | same, ACTIVATE 446.29 m | WRONG TARGET: void step |
| SO2 check | — | host-…-c243-plant-so2-check | warrior bug on the host, ACTIVATE 446.56 m | fail (check) |
| fail 628 | — | host-/client-…-fail-line628 | host: console under attack; client: green-lit panel beside the real switch | fail |

Shot list (36 files, including the 2 from the stopped launch): `~/.cache/claude-tmp/claude-1000/-path-to-ultimate-bug-war-coop/6d801d14-53c9-4cec-bc27-ada000576173/scratchpad/r3-shots.txt`.

## Run 4 preparation (junior, 2026-11-13) — senior order: stand data, look gate, aim fixes

### Driver change (written before the edit, senior step 2)

The look-distance gate needs one void line per aim/press step otherwise (161 extra steps; the scenario is at 1897,
cap 2000), so it goes into `tools/drive.py`, limited to the look check:

- `look ... within=R` (role look actor/point): drive strips `within=R` before sending, waits for the look's
  result, reads the mod's own row `cmd: N look: <name> at X Y Z from X Y Z: yaw .. pitch .., D uu` for that seq
  and returns `VOID look N: D uu over within R` when D > R, or `VOID look N: no distance row (<detail>)` when the
  look has no row (no such actor, failed).
- `try <step>`: a VOID detail passes through as a failure (not TRY-FAIL), so a gated look voids under `try` and
  under failsoft (the main loop already ends the run on a VOID detail).
- No other driver change. The changed lines are listed in the run 4 report.

Spans per action class (D = eye to look point, the row's last number):

| Class | Steps | Run 3 D | Span |
|---|---|---|---|
| E use (switches, wall switch, launcher, secret pickup) | press/use looks | 155 (wall switch), 187 (launcher) | 250 |
| E on a dropship (client and host) | board looks | none yet | 800 |
| grenade stand (900 uu stands, nest A 1500 uu actor warp) | nest looks at 900/1500 | 900–958, 1499 | 1600 |
| nuke stand (2500 uu point stands) | nest looks at 2500 | 2498–2539 | 2700 |
| damage on clasps | clasp bounds looks | none yet (probe 4) | set from probe 4 |

Presence and kill-round looks (presence volumes, ActorReference_C_9, panel C_1 wave rounds) are not aim steps and
get no span.

Driver change made (`tools/drive.py`): usage line 21 (`look ... within=<R>`), new `look_within()` lines 173–186,
`try` VOID pass-through lines 415–416, `look` dispatch lines 444–446. Nothing else in the driver changed.

### Probe 4 (c243-p4, 152 s, host rc 0, client rc 0, 0 crashes): runtime origin and yaw per actor

One run, `host warp actor <A> 300 behind` (the warp row logs the actor yaw), `host look actor <A> 0` (the look row
logs the origin), one shot each; logs `logs/c243-p4-host.log`, drive `logs/c243-p4.drive`. Gate proof at its end:
`try host look actor BP_Switch_C_6 0 within=1000` -> `done, 302 uu within 1000`; `try host look actor BP_Switch_C_6 0
within=10` -> `VOID look 92: 306 uu over within 10`, the run ended there (drive rc 1, as written).

Finding: every static switch stand in runs 1–3 belonged to a different switch (the static rows were matched to the
wrong names): the old C_6 stand is C_8's, the old C_0 stand is C_3's, the old C_2 stand (run 3 VOID) is C_0's, the
old panel C_1 stand is C_2's. Stand = origin + forward x 170, z origin + 100 (the switch faces forward: the behind
shots of C_2, C_6, C_8 show the wall it is mounted on).

| Actor | Old stand | Runtime position (yaw) | New stand / use | Source row |
|---|---|---|---|---|
| BP_Switch_C_2 (PowerPlant_SO2) | (-64440, 6149, 1728) | (-24445, -13652, 1930), yaw 90 | (-24445, -13482, 2030) | p4 host cmd 3 warp, cmd 4 look; run 3 cmd 157 |
| BP_Switch_C_6 (PP_SO3_DoorSwitch1) | (-11590, -1145, 3369) | (-12839, -13534, 2054), yaw 90 | (-12839, -13364, 2154) | p4 cmd 9, 10 |
| BP_Switch_C_8 (PP_SO3_DoorSwitch2) | (-40129, 54428, 1820) | (-11420, -1149, 3269), yaw -178.8 | (-11590, -1153, 3369) | p4 cmd 15, 16 |
| BP_Switch_C_3 (Silo_SO2) | (-82113, 3692, 508) | (-53794, 901, 1856), yaw -1.4 | (-53624, 897, 1956) | p4 cmd 21, 22 |
| BP_Switch_C_0 (Silo_SO4) | (-53624, 897, 1956) | (-64270, 6144, 1628), yaw 178.3 | (-64440, 6149, 1728) | p4 cmd 27, 28 |
| BP_WallSwitch_C_1 (Monorail_SO3) | by name | proven run 3 (D 155, completed) | unchanged | run 3 cmd 78 |
| BP_PanelSwitch_WithoutScreen_C_1 (Silo_O1) | (-24445, -13482, 2030) | (-56958, 7645, 2087), yaw 0 | (-56788, 7645, 2187); wave rounds by name | p4 cmd 33, 34 |
| MissleSilo_C_4 / clasp | by name, clasp bounds | silo (-59357, 922, 1146) yaw -140.6; clasp bounds centre (-59879, 493, 1537), D 1681 | by name, clasp look dz 150 | p4 cmd 63, 64, 70 |
| MissleSilo_C_3 / clasp | by name | silo (-68641, 2614, 1152) yaw 180; clasp (-69317, 2614, 1541), D 1681 | same | p4 cmd 72, 73, 79 |
| MissleSilo_C_0 / clasp | by name | silo (-59220, 9422, 1152) yaw 129.4; clasp (-59649, 9944, 1541), D 1682 | same | p4 cmd 81, 82, 88 |
| BP_PlayerPresence_C_52 | by name | (-25452, -7955, 2598) | unchanged | run 3 cmd 152, card132 ext row |
| BP_PlayerPresence_C_53 | by name | (-31374, -5970, 1930) | unchanged | p4 cmd 39, 40 |
| BP_PlayerPresence_C_2 | by name | (-62225, 4852, 3113) | unchanged | card132 host ext row (C3M2 host logs) |
| BP_PlayerPresence_C_12 | by name | (-73290, -190, 1270) | unchanged | card132 host ext row |
| BP_PlayerPresence_C_14 | by name | (-53070, -19340, 1270) | unchanged | card132 host ext row |
| BP_PlayerTransportDropship_C_0 | by name | (-65387, -1006, 1465), yaw 42.2 (at map start) | unchanged, look within 800 | p4 cmd 51, 52 |
| BP_PlayerTransportDropship_C_2 | by name | (-29391, -34756, 10836), yaw 135 (at map start) | unchanged, look within 800 | p4 cmd 57, 58 |
| BP_SecretPickup_C_2 | by name | (-35576, -23236, 1101), yaw -40 | unchanged (shot: PICK UP SECRET ALL OUT WAR) | p4 cmd 45, 46 |
| Tankers (PowerPlant_O1) | class kills | no static stand | unchanged | — |
| Nests C_0/C_9/C_1/C_5 | probe 1 rows | unchanged (probe 1 card098 rows) | stands per aim fix below | probe 1 |

Clasp shots (p4 silo0/3/4-clasp): from the 900 uu warp the crosshair sits on the silo lid just under the two clasp
blocks, so the clasp look gets dz 150 (block centres about 150 uu above the bounds-centre line at 1681 uu). Unproven
until the run's clasp shots. Panel C_1: the behind warp fell 935 uu (open drop behind it); the front stand z 2187 is
unproven, the press eye void catches a fall there.

Spans set: E use 250, dropship 800 (D 371–383 from 300 uu), grenade 1600, nuke 2700, clasp 1800 (D 1681).

### Aim fixes (senior step 3)

- s020 (C_1 nuke +X), s021 (C_1 nuke +Y), s023 (C_1 grenade +X), s027 (C_5 nuke +X): wall stands removed. C_1 now
  nuke -Y (s022 clear), grenade -Y twice (s024 clear); C_5 nuke +Y, -Y (s028, s029 clear), grenade +X twice (s030
  clear). The client fallback throws use the same clear stands.
- bpsc-2-stand, s056, s057 (BP_Switch_C_2 wrong target): stand from probe 4 (table), and every press look carries
  `within=250`, so a look at a target 446 m away voids at once.
- s035–s040: relabelled presence in the run 3 shot table.

### Predictions for run 4 (full run, 1876 steps, 158 shots, 72 gated looks)

| Block | Rows, host | Rows, client | Notes |
|---|---|---|---|
| start | card135 client card closed, unpause row before c243-client-ingame shot | same | as run 3 |
| Monorail | SO1, SO2_A–D, SO2, SO3, SO4, O1 Complete | replayed, same ids | nest looks 900–2540 uu inside spans; no wall shots |
| PowerPlant | SO1; SO2 (C_2 press D about 200, card020 target non-null); SO3_DoorSwitch1 (C_6), DoorSwitch2 (C_8), SO3; SO5 Add; presence C_53 + rounds, tankers, O1, SO5 | replayed | POWERPLANT_SO4: Add row expected never logged (no caller, 9122); reported either way |
| Silo | SO1 (presence C_2); SO2 (C_3 press); 3 Silo_SO3_Lock rows, SO3 (clasp looks D about 1680); SO4 (C_0 press); SO5 (timers); O1 (panel C_1 press); SO6 cascade | replayed | panel C_1 stand is the one unproven stand |
| Secret | card020 interact on SecretPickup (no Secret Add row) | — | |
| HydroDam | SO1 (board C_0: client EnterDropship row + client shot, host E); SO2 (landing); SO3, SO4, SO5 (presence C_12/C_14 + rounds); O1 (board C_2: client EnterDropship row + shot); SO6 | replayed | client E on the evac dropship proven by `collision: client EnterDropship` |
| Tail / end | objectives list, digest round 0 mismatches; no Secret_O1/SO1/PowerPlant_SO6 Add; host WB_TrooperLevelEnd row; client level-end row (card018 client); both worlds FED_C4M1 | same | completed 24 of 25 per side if PowerPlant_SO4 stays un-added, else 25 of 25 |

Client inputs proven: grenade (client G rows only if a host nest throw misses), E on the evac dropship (EnterDropship
row + client shot), level-end screen (card018 client row + c243-client-end shot). A gated look over its span, a fall
off a press stand, or a missing chain row (stop_unless) voids the run; on a void the run stops and is reported.

## Run 4 (2026-11-13 06:34, voided at line 276)

Host log UBWCoop-host-20261113-063433.log, client UBWCoop-client-20261113-063435.log, copied to logs/c243-c3m2-{host,client}.log (this overwrote the run 3 copies under the same tag). Ended by scenario after 156 s; host rc 0, client rc 0; FATAL/ERROR 0/0; WARN 2 host, 6 client; new crash dumps none.

Logged cause (the new look gate, as designed):

- host cmd 42: `warp: own pawn ... -> (-40337.7, 54274.9, 1820.0), 156 uu from BP_WallSwitch_C_1`
- host cmd 44: `look: BP_WallSwitch_C at -40294 54387 1720 from -40423 54255 928: yaw 45.7 pitch 76.9, 813 uu`
- drive: `FAIL line 276: ... try host look actor BP_WallSwitch_C_1 within=250 -> VOID look 44: 813 uu over within 250`

The pawn fell about 890 uu below the warp point after `warp actor BP_WallSwitch_C_1 120` (eye z 928 vs switch z 1720). Run 3, same steps: cmd 76 warp to (-40323.4, 54270.3, 1820.0), cmd 78 look from z 1824, 155 uu. Same warp target, different landing: the stand at 120 uu is not safe ground. Shot c243-c3m2-fail-line276 taken. Per order, no further run.

Completed before the void, host sent and client replayed both: Monorail_SO1, Monorail_SO2_A, Monorail_SO2_B, Monorail_SO2_C, Monorail_SO2_D, Monorail_SO2 = 6 of 25 per side. POWERPLANT_SO4: not reached. Grenade / evac E / level end / FED_C4M1: not reached.

Digest: 4 rounds, mismatches 0 in all (slots differing 3/1/1/1, entries equal from round 2). Frame windows under 500: host one window (t=212069095, 9 frames, map load before the first Pong); client none. 11 distinct shots.

## Run 5 preparation (senior 2026-11-13): stands by proven point only

Senior finding from the run 4 rows: `warp actor <name> <dist>` lands dist uu short of the actor along the line from
where the pawn was, so every name-warp landing depends on the step before it (run 3 wall switch landed on the walkway,
run 4 from another origin landed 14 uu away, off the edge). Class fix: every stand before a press, aim, throw or damage
step becomes `warp actor|client point x y z` at a point a runtime row proves, followed by `wait stand`, then the
`look ... within=`, the shot, the action. Presence volumes keep their walk-in (`goto`) but their warps become points too.

No earlier log proves the forward side of BP_Switch_C_2/_6/_8/_3/_0, panel C_1 or the secret pickup (no log ever
completed those objectives; probe 4 stood only 316 uu behind, D 330-364, over E_SPAN 250; C_3 behind fell 303, panel
behind fell 748). Senior granted probe 5 (tag c243-p5) to land every planned stand once.

### Probe 5 candidates (written before the probe)

Host E stands: candidate 1 = origin + forward 170, z + 100; candidate 2 = forward 120, z + 100. Secret pickup: on the
probe 4 held side (toward its 316 uu point), 150 and 220 uu from the origin, z 1201. Client copies: forward 170 +
lateral 200 (z + 100), and the probe 4 behind 300 point. Each stand: point warp, `wait stand x y z 200000` (logs where
the pawn holds; the radius only records), host `look actor` without within (D recorded), shot. No E, G or fire.
Probe 5 also re-lands every other planned stand (nests, launcher, ammo, wall switch, clasps, frag refills, dropships)
and lands each presence / marker name warp once (300/400 and 40/60) to get its held point for run 5.

| site | role | shot | candidate |
|---|---|---|---|
| BP_Switch_C_2 | host | p5h22 | point -24445 -13482 2030 |
| BP_Switch_C_2 | host | p5h23 | point -24445 -13532 2030 |
| BP_Switch_C_2 | client | p5c16 | point -24645 -13482 2030 |
| BP_Switch_C_2 | client | p5c17 | point -24445 -13952 2030 |
| BP_Switch_C_6 | host | p5h24 | point -12839 -13364 2154 |
| BP_Switch_C_6 | host | p5h25 | point -12839 -13414 2154 |
| BP_Switch_C_6 | client | p5c18 | point -13039 -13364 2154 |
| BP_Switch_C_6 | client | p5c19 | point -12839 -13834 2154 |
| BP_Switch_C_8 | host | p5h26 | point -11590 -1153 3369 |
| BP_Switch_C_8 | host | p5h27 | point -11540 -1152 3369 |
| BP_Switch_C_8 | client | p5c20 | point -11586 -1353 3369 |
| BP_Switch_C_8 | client | p5c21 | point -11120 -1143 3369 |
| BP_Switch_C_3 | host | p5h32 | point -53624 897 1956 |
| BP_Switch_C_3 | host | p5h33 | point -53674 898 1956 |
| BP_Switch_C_3 | client | p5c26 | point -53619 1097 1956 |
| BP_Switch_C_3 | client | p5c27 | point -54094 908 1956 |
| BP_Switch_C_0 | host | p5h34 | point -64440 6149 1728 |
| BP_Switch_C_0 | host | p5h35 | point -64390 6148 1728 |
| BP_Switch_C_0 | client | p5c28 | point -64446 5949 1728 |
| BP_Switch_C_0 | client | p5c29 | point -63970 6135 1728 |
| BP_PanelSwitch_WithoutScreen_C_1 | host | p5h36 | point -56788 7645 2187 |
| BP_PanelSwitch_WithoutScreen_C_1 | host | p5h37 | point -56838 7645 2187 |
| BP_PanelSwitch_WithoutScreen_C_1 | client | p5c30 | point -56788 7845 2187 |
| BP_PanelSwitch_WithoutScreen_C_1 | client | p5c31 | point -57258 7645 2187 |
| BP_SecretPickup_C_2 | host | p5h41 | point -35691 -23140 1201 |
| BP_SecretPickup_C_2 | host | p5h42 | point -35744 -23095 1201 |
| BP_SecretPickup_C_2 | client | p5c35 | point -35806 -23043 1201 |

Full list of the 91 probe stands: ~/.cache/claude-tmp/claude-1000/-path-to-ultimate-bug-war-coop/6d801d14-53c9-4cec-bc27-ada000576173/scratchpad/r5/p5-sites.tsv. Scenario: ~/.cache/claude-tmp/claude-1000/-path-to-ultimate-bug-war-coop/6d801d14-53c9-4cec-bc27-ada000576173/scratchpad/r5/c243-p5.txt (429 steps).

## Run 5 ammo count (junior, 2026-11-13, static, no launch)

Senior order: there is no mod ammo source; the count is closed with what a real player uses. Run 5 launches only if
the count closes with margin.

### Observations carried from p3b

- Client E prompt: C_3 and C_8 showed the prompt on both roles. C_0 showed it only on the host (p3bh01 "Ready, Set,
  Boom"). On the client (p3bc01) there was no prompt, although focus named C_0. Run 5 reports, for every client
  press, whether the press took effect and whether the prompt is on the shot. A client press that fails with focus
  named is a mod card, not a scenario fault.
- BP_SecretPickup_C_2: `focus=` only, as an exception for this one target, because the pickup has no Visibility
  collision (Actor_16 row). The shot must show it at the crosshair.

### Supply sources (static)

- **Held-weapon pickup adds ammo (base game).** `ASTWeaponPickup::PickupWeapon` 0x51D2FD0 takes this branch when
  the player already holds the weapon:

  ```
  1451d30e0 call UADWeaponInventory::GetWeaponInstanceFromClass   ; pickup+0x4A8 definition class
  1451d30e8 je   1451d314e                                         ; not held -> AddWeapon path
  1451d30f6 mov  r9d,[rdi+0x4e0] ; lea r8,[rdi+0x4d0]              ; primary tag + count
  1451d310a call ASTWeaponPickup::AddAmmunitionTypeToInventory     ; -> USTWeaponUtilityLibrary::AddAmmunition
  1451d310f mov  r9d,[rdi+0x4ec] ; lea r8,[rdi+0x4e4]              ; secondary tag + count
  1451d3125 call ASTWeaponPickup::AddAmmunitionTypeToInventory
  1451d312a test ebx,ebx / jg ; test eax,eax / jle fail            ; nothing added -> pickup stays
  1451d3140 mov  byte [rdi+0x521],1 ; ret                          ; consumed
  ```

  `Pickup_Implementation` 0x51D42E0, the overlap path, does the same for owned weapons, so walking over a pickup is
  the real step. `AddAmmunitionTypeToInventory` 0x51BC280 checks `CanAcceptAmmo` and then calls the static
  `AddAmmunition` 0x5229E60. `TryToCopyWeaponDefinitionStartingValues` 0x51E9030, called from `PostInitProperties`,
  fills +0x4D0/+0x4E0 from the definition only when the pickup has no count of its own.

- **Pickup counts.** Read from each CDO export: the FGameplayTag name, then the count int32. The same read gives
  MoritaSmall 120 and MoritaLarge 240, which matches the card 071 "asked 120/240" rows.

  | Pickup | Ammo it gives |
  |---|---|
  | BP_WeaponPickup_MoritaMk1 (the runtime death drop) | Morita 60, UnderslungShotgun 8 |
  | MoritaCarbine | Morita 60 |
  | LaserMorita | Laser 60 (ResetInventory already gives every pawn Laser 300, card 069) |
  | MoritaMk1Scoped | Marksman 30 |
  | PumpShotgun | Shotgun 6 |
  | Railgun | Railgun 4 |
  | Autocannon | Autocannon 100 |
  | EPulse44 / EPulse88 | Epulse 25 / 100 |
  | AmmoPickup MarksmanLarge / Small | Marksman 60 / 30 |
  | AmmoPickup AutocannonLarge / Small | Autocannon 100 / 50 |
  | AmmoPickup EPulseSmall | Epulse 25 |
  | AmmoPickup NukesSmall | Nuke 1 (card098 tally placed=4 on C3M2; line 211 above says none, needs a position check) |

- **Death drops on C3M2.** Each run logged 27, 22, 17, 9 and 18 card107 `runtime pickup BP_WeaponPickup_MoritaMk1_C`
  rows. c234 had 27 drops against at least 42 kills, which is about 0.6 drops per kill, or about 38 Morita returned
  per kill. p3b spent about 14 rounds per kill. The Morita reserve cap is 240 (card 071 "max 240").
- **Racks and resupply pod:** none on C3M2 (0 rows).
- **Tactical support.** IMC_MI_KBM binds IA_TacticalSupport_Slot1 to `Four` and Slot2 to `Five`. The placed
  pickups are BombingRun, OrbitalStrike, GuidedMissile and MechSupport. Their aim with the 264 gate is not
  checked. They are not needed for the count below. MechSupport is left out (the card 075 one-mech-per-pad
  question).
- **Killable crates:** 2, at (-58248, 1312, 1508) and (-53767, -17840, 2295). Random drops, counted as a bonus only.
- **Underslung shotgun:** 8/8/8, already on the pawn.

### Client gap (static claim, found wrong: card 266)

Correction: no gap. The singular `AddAmmunition` calls the hooked plural at 0x5229F10, and c127-c2m1 logs
show a client MoritaMk1 drop credited and mirrored (card 266). The original text follows.

The held-weapon branch adds ammo through the static `AddAmmunition` (0x5229E60). The mod relays only
`AddAmmunitions` (ammo.cpp, card 071) and calls `AddAmmunition` only for the card 137 test grant. So when the
client walks over a death drop, the ammo lands on the host's server copy and is not sent to the client. The
card 072 mirror then writes the client's own count back onto the copy. Expected client gain from death drops: 0.
Not run-proven. The run would show it as the client's reserve not rising after an overlap on a MoritaMk1 drop.

### Demand per objective block (what each objective counts)

| Block | Completes on | Kills needed |
|---|---|---|
| Monorail_O1 | EM SetWaves C_11, then C_22, then C_23 end (all wave bugs dead, by anyone incl. AI troopers/turrets) | the three waves |
| PowerPlant_O1 | `TankersKilled >= 2` (C_29 end triggers tankers C_50/C_30) | wave C_29 and 2 tankers |
| Silo_SO5 | timers only (45+30+15 s) | 0. The 10 kill ROUNDs are cut |
| HydroDam_SO3 / SO4 | EM C_51 / C_53 end | those two encounters |
| HydroDam_SO5 | Dam_Warriors/Jumpers/Centurion1/2 dead | those groups, incl. 2 centurions |
| Per-target stands | no live bug within 400 uu of the stand, radius 600, max-left | only the bugs on the stand |
| Nests | building destroyed (nuke shot) | 0 kills; blasts as early as the order allows |

The wave sizes are level-instance data and are not read here. The partial C3M2 runs logged 42 to 49 spawns.

### Count

| | Host | Client |
|---|---|---|
| Start | Morita 60 + 240 | Morita 60 + 240 |
| Death drops (walk-over) | +60 each, cap 240. At about 0.6 per kill, about +38 per kill against about 14 spent, so self-sustaining | same as the host (card 266: no gap) |
| Second weapons on route | not needed | LaserMorita: Laser 300 + 60 magazine. Scoped: Marksman 30, plus placed Marksman pickups (60 / 30 each) |
| Demand | all defend-wave kills (god mode) plus every stand clear | none, if the host clears the client's stand too (a teammate's cover) |

Verdict (after card 266): both roles close by death drops, so no second weapons are needed. Both roles fire
their own clears and walk over the drops in reach after each clear. Run 5 (c243-c3m2-r5) tests this.

## Run 8 (c243-c3m2-r8): one floor on every clear's start row, plus desk checks

Run 7 voided at 108 s. The floor step was a readout `clear 100 10`. The DLL's quiet end and its at-cap void both use
kC265MeleeUu (400 uu), not the command's radius, so the readout could neither end quiet nor fire while a bug stood
100 to 400 uu away (WarriorBug 335 uu at the cap). Senior order: a readout must not be able to fight. The
`inventory` row prints no ammunition, and the host has no row with its own AmmoMap outside the clear rows, so the
floor moved onto the clear start row (printed before the clear fires). The floor steps and `floor=later` are gone.
The scenario is run 7 less those steps and gate words (diff checked): 2070 steps, 96 clears, 95 pickups, 118 shots.

### Desk checks: step forms new since p3b

| Form | Code path read | End conditions | What voids it |
|---|---|---|---|
| `<role> clear R S max-left=N` (spawner 400 15 99, non-spawner 600 45 2, defend 600 60 30) | testrun.cpp c265_start 972, c265_step 1164-1209; drive.py check_gates | quiet: left 0, no target and no live bug within kC265MeleeUu (400) for kC265QuietMs (3 s), then the end reload; cap S: end row | DLL: a live bug within 400 uu at the cap (failed row, driver VOID even under try). Driver: left over N, no end row, no `reserve R` |
| Start-row floor (default on every clear, r8) | drive.py check_gates, after the end row; row from c265_start (`ammo` from c69_map_text, `Type=map/count/max`) | judged once the clear has ended | count of AmmunitionType.Morita on `cmd: N clear: start` under 60, or the type missing from that row (only when the AmmoMap has free slots; 0 of 14 start rows since the AmmoMap switch) |
| `<role> pickup <class> <max>` (r7) | drive.py pickup; DLL goto start row (testrun.cpp 515) | path at most max: waits for the goto's result (20 s); longer, no start row or not done: `goto actor <class> 100000` ends it | none: always ok |
| `<role> wait stand x y z r` / `x y any r` (r5/r6) | drive.py wait_stand, two state reads 0.5 s apart | both reads within r (xy only with `any`) | not held within the timeout: VOID |
| `host warp client point x y z` (nuke after-step, r7) | drive.py misplaced_warp (host only); DLL warp of the server copy | done row | failed row (no server copy): plain FAIL, failsoft |
| `--parse` (r8) | drive.py lint/lint_step: load(), the prefixes, the command words, gate words, regexes, clear ranges (100-20000 uu, 10-90 s) | posts nothing | prints each bad line; r5, r6, r7 and r8 parse with 0 bad |

### Host fire outside clears (static, Morita magazine 60, the reserve drops only on a reload)

| Block | Count | Spend before the next start-row read | Drop walk before it |
|---|---|---|---|
| Nest A grenade try, `host key LeftMouseButton 1500` | 2 | at most one magazine refill (60) | not allowed (rule 3: the look follows the clear) |
| Clasp, `host key LeftMouseButton 3000` then `try host key R` | 15 | at most one magazine refill (60) | yes: clear() ends with drops() |
| Nuke shot, `host key LeftMouseButton 6000` | 6 | 0 Morita (the NukeLauncher is equipped; the frag fallback keys One before any host clear) | not needed |

None of these blocks runs dry silently. The next host clear reads the start row before it fires and voids under 60,
so a shortfall shows at the step that would run dry.

## Run 9 (c243-c3m2-r9): spawner sites end on the look; discovery mode

Run 8 voided at 107.6 s on drive line 57: `host wait stand -42976 50633 915 60 timeout=15 -> VOID stand ... not
held: last 68 uu`. The nest A spawner clear (started with Morita 240) dropped BP_AI_WarriorBug_C_2147476091 at
408 uu (radius 400) and ended at its 15 s cap with left 0. The same bug came back. From 0.6 s after the restand warp
until the fail shot it hit the host 22 times (`playlog: host ... hit slot 0 host-own: damage 25.00`, one every
0.07 to 1.2 s). The pawn never held still for two reads 0.5 s apart.

Senior order (option 1): at every spawner site the order is warp, wait stand, clear, then the gated look and the
action, with nothing in between. The post-clear warp and stand are dropped, and the look gate (`within=`, `hit=`)
judges the real pose.
- Nuke stands: the host holds the NukeLauncher, so the client clears. It now clears from 200 uu beside the host's
  stand, across the line to the nest (CLIENT_BESIDE), and stays there for the shot. The host stands first.
- The client grenade fallback (`mate`): the client's second warp and stand after the clear are gone.
- Non-spawner clears walk to drops, so they keep the restand.

Discovery mode (senior): the `voidsoft` line goes after the DLL and map checks.
- A void after it is logged as before (VOID row, fail shots) and the run goes on.
- The run ends "scenario finished VOID with N void line(s): ..." with driver rc 3.
- A run with any void proves nothing for the voided steps and is not the soak. One clean run without `voidsoft` is.

Scenario: 2019 steps, 118 shots, stand checks 173 3D / 46 xy-only / 102 lowered.

### Desk checks added in run 9

| Form | Code path read | End conditions | What voids it |
|---|---|---|---|
| `voidsoft` | drive.py main loop; TERMINAL_VOID | a void step logs `scenario: VOID line N ..., continuing (voidsoft)` after the fail shots | still terminal: `VOID <role> dll`, `VOID wait state` (map or pawn), a role's game gone |
| Spawner site order | gen_c243.py act(spawner), nest_fire aim() and mate | each of the 23 `clear 400 15 max-left=99` lines is followed directly by its look (14 host nest, 8 client point, 1 Switch_C) | the look's within=/hit= gate on the real pose |
| No stand right after a clear | static scan of r9 (prefixes stripped) | 0 `wait stand` lines directly after any clear; 0 warps between a spawner clear and its look | n/a (static) |
| `--parse` | drive.py lint | r9: 2019 steps, 0 bad. r5, r6, r8: 0 bad. r7: 171 bad, its `floor` steps (the form was removed in r8) | n/a |

### Run 9 finding: the goto stop never stopped anything (fixed in the driver)

The run 7 desk check above (pickup row) was wrong. `goto actor <class> 100000` is skipped by the DLL, which takes
radius 10 to 5000: `WARN cmd: 15 "goto actor BP_WeaponPickup_MoritaMk1_C 100000" skipped: bad actor name (1-63 chars
of [A-Za-z0-9_]) or radius (10-5000)`. The goto it was meant to end walked on in the background (host cmd 16, a
4744 uu path: `cmd: 16 goto: at -43952 49718 853 ... moved 1915`; client paths of 28744 uu). The next warps and stands
voided: drive lines 70, 71, 73, 85, 98 and 100.
The same form was the last line of every `drops()` block (156 lines in r9).

Fix (driver and generator only):
- New step `<role> halt`: a goto to the pawn's own point (from its state file), radius 150.
- The DLL ends a running goto ("replaced", testrun.cpp Op::Goto) before it starts the new one. So the walk stops
  even when the halt goto fails to plan.
- `halt` is ok unless the state file has no point; then it is VOID.
- `pickup` ends a long or slow walk with `halt`. `drops()` ends with `host halt` / `client halt`.

| Form | Code path read | End conditions | What voids it |
|---|---|---|---|
| `<role> halt` | drive.py halt; testrun.cpp Op::Goto (replace, then start), c64_walk_tick (reached when 2D dist <= radius at the goal's z) | the halt goto's result line (ok when reached on its first tick; a failed start is reported, the old walk is already ended) | no x y z in the role's state file |
| `<role> pickup <class> <max>` (corrected) | drive.py pickup | path at most max: waits for the goto's result (20 s); otherwise `halt` | halt's void only |
| `--parse` | drive.py lint | regenerated scenario (scratchpad): 2019 steps, 0 bad, 0 `100000` lines, 156 `halt` lines | n/a |

### Run 9 finding: Lock1 never ran (fixed in the generator)

One lock completion logs two rows that `DONE` matches (`card097 ... ObjectiveComplete(Silo_SO3_LockN)` and
`card049: host sent CompleteSubObjective(Silo_SO3, Silo_SO3_LockN)`). The clasp gates counted all Lock rows
(`count=k`), so after clasp 1 completed one lock the count was already 2 and the whole clasp 2 block
(MissleSilo_C_3) was skipped. Clasp 3 then completed a second lock and the run never fired at the third silo.

Host log, run 9: firing at the MissleSilo_C_4 clasp (cmd 217) completed Silo_SO3_Lock3 at 222832132; firing at the
MissleSilo_C_0 clasp (cmd 228) completed Silo_SO3_Lock2 at 222917565. MissleSilo_C_3 is therefore Lock1.

Fix (tools/gen_c243.py `clasp`): each clasp block gates and waits on its own lock's rows, `count=1`:
C_4 Lock3 (fail-fast check `clasp-3`), C_3 Lock1, C_0 Lock2. The shared `LOCK` pattern is gone. Desk check on the
run 9 host log: the per-lock regex matches Lock1 0, Lock2 2, Lock3 2 rows. No other `count=` above 1 remains in
the generated scenario. Silo_SO3/SO4/SO5/O1 and all HydroDam voids in run 9 follow from this (no HydroDam Add row).

## Run A (c243-c3m2-a1, 2026-11-13): VOID, ammo route failed a second time; method change (senior)

- 41 voids; only `Monorail_SO1` completed. The client fired 300 rounds in 150 s and had none left; drop walks
  failed (paths 1720 to 25663 uu, `no path` twice). Part of the ammo use is card 276 (dropped hits do no damage).
- Decision: test-only driver command `ammo fill` (both roles), through `c21_grant_ammo` on the caller's own pawn so
  `AmmoMirror` carries it as in play; logged `cmd: N ammo fill: <tag> a to b`. Used before each `clear` when the
  reserve is under one magazine. Pickup walks stay only where a pickup is the thing under test. Ships in build 4.
- No FED_C3M2 rerun before build 4 (276 and this command). Cards 271, 261 and 191/203 monorail rows stay owed.

## Run `c243-c3m2-b4a` (build 4, 2026-11-13): void, scenario fault

Stopped at 538 s at scenario line 86 of 568, 0 objectives. `tools/scenarios/c243-c3m2-b4.txt` held 0 `ammo fill`
steps, although `tools/gen_c243.py` emits one before every clear (`FILL = True`, `fill()` at lines 94, 118, 150,
473) and its clears read `max-left=99 calm=3`, which the generator's `CLEAR_AREA` does not produce. The file was
not made by the current generator. Both players reached Morita 0/0/240 (host by cmd 11, client by cmd 19) and the
`Monorail_SO2_A` nest never fell; four VOIDs at lines 58, 67, 76, 85 are that one cause.

Before the next run (batched with build 5, no rerun for this alone):

1. Find how the b4 file was made (generator arguments, hand edit, or an older copy of the generator) and state it.
2. Regenerate from the current generator; check `grep -c 'ammo fill'` is at least the count of `clear` steps.
3. Prediction: each `ammo fill` logs `cmd: N ammo fill: ... no fill` or `... A to B` on the role's own log; no
   clear starts with a reserve under 60; `Monorail_SO2_A` completes on the first attempt.

## Run `c243-c3m2-b5` (build 5, faaa085d7714170b): pre-run steps and predictions (junior, 2026-11-13)

Pre-run steps:

1. How b4 was made: `c243-c3m2-b4.txt` is `c243-c3m2-a1.txt` (11:36, made by a generator from before `FILL`; 535
   steps, 40 clears, 0 `ammo fill`) with the two DLL lines changed to 5e2ce00acad8efa3 and one hand-added block at
   line 146 (`client key R` plus a 3 s sleep, "build 4b run 3"). It was never regenerated after `fill()` went in.
2. `python3 -I tools/gen_c243.py` regenerated now is identical to `c243-c3m2-b5.txt` apart from the DLL hash (1034
   steps, 118 shots). `grep -c 'ammo fill'` 104, clear steps 104: one fill before every clear.
3. `tools/drive.py` clear floor: the floor now reads the first ammo type on the start row (the primary weapon's),
   not a fixed Morita; on this route that is still Morita (run c181-b5 voided on FED_C1M1, where it is Laser).

Predictions:

- 243: every `ammo fill` step logs `cmd: N ammo fill: ... a to b` or `... no fill` on its role's log; no clear
  starts with a Morita reserve under 60; `Monorail_SO2_A` completes on its first attempt.
- 276: lost presses (`active 1` rows) each get one `re-pressed` row; `the copy fired 0 of` rows only before a
  re-press; 0 `found no round` rows while the client has rounds; claim lag under 100 ms through a magazine.
- 274: one `card274: host copy magazine <tag> a to b` row after each client reload (client reload ability rows
  counted against it); 0 `found no round` rows while the copy holds rounds.
- 281: each `card281: ... served by a made round` row is followed by the `card165` health drop row for the same
  NPC; then the `card157` applied row.
- 263: no client frame window below 300 frames outside load and join.
- Both logs: WARN classes diffed against `c243-c3m2-b4a`; 0 digest mismatches.
