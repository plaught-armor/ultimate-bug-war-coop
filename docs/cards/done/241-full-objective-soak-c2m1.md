# 241: Full-objective soak, FED_C2M1

Same shape and rules as card 240 (read `docs/cards/240-full-objective-soak-c1m1.md` or
`docs/cards/done/240-*.md` first: no `objective complete`, wait on the host Complete row and the client
replay, both players in presence volumes, objectives dumps at the end, faults become their own cards).

Goal (one): one scenario that plays FED_C2M1 from spawn to the mission-end travel and completes every
objective by its own trigger.

Objective list M = 23 (junior step 1: LOS_O05 has SO01 to SO06; first count of 22 read string keys, not row ids), from the game's data table `DT_LevelObjectiveData_FED_C2M1` (keys
`FED_C2M1_<id>_NAME`): LOS_O01, LOS_O01_SO01, LOS_O01_SO02, LOS_O02, LOS_O02_SO01 to SO05, LOS_O03,
LOS_O03_SO01 to SO03, LOS_O04, LOS_O04_SO01, LOS_O04_SO02, LOS_O05, LOS_O05_SO01, SO02, SO04, SO07, SO08.
The last soak (c238-c2m1, scenario `c127-c2m1.txt`) completed 5: LOS_O01_SO01, LOS_O02_SO01, LOS_O02_SO02,
LOS_O03_SO01, LOS_O04_SO01; its part 5 notes that no run reached LOS_O03_SO02.

Step 1, static, no run, appended here as a table: each of the 22 in play order with parent, counter, real
trigger with level-script or class evidence (`FED_C2M1_Scripting`), the driver steps that fire it, the synced
follow-ups to wait on, and any id that is a marker only or belongs to a branch the map never activates (state
the evidence; such an id leaves M only with my agreement).

Step 2: `tools/scenarios/c241-c2m1.txt` (new file; `c127-c2m1.txt` stays).

Step 3: run, budget 3 runs. Command:
`systemd-run --user --scope -q -p MemoryMax=12G env SCENARIO=tools/scenarios/c241-c2m1.txt DEPLOY=1 FAST=1 HOST_EXTRA_ENV="UBWCOOP_TRAVEL=/Game/Maps/FED/FED_C2M1?listen UBWCOOP_GOD=1 UBWCOOP_C132_FX=1 UBWCOOP_C136_LOG=1" CLIENT_EXTRA_ENV="UBWCOOP_C132_FX=1 UBWCOOP_C136_LOG=1" tools/run_pair.sh c241-c2m1 3600`

Files allowed: `tools/scenarios/c241-c2m1.txt`, this card. Driver verbs only after a report to the senior.
No other mod file. No build while a game runs.

Done when: final objectives dump shows `complete 1` for M of M on host and client, lists match, both sides
reach the next map (FED_C3M1), 0 crash, every digest round 0 mismatches, frame windows at or over 500 outside
load. Report: completed N of M, each fault with its lines, new WARN classes against c238-c2m1.

## Step 1 result (static, junior)

Sources: `DT_LevelObjectiveData_FED_C2M1` and `FED_C2M1_Scripting.umap` (name maps, extracted with
`tools/iostore/iox.py`), `OBJ_StringTable`, `c127-c2m1.txt`, `logs/c238-c2m1-{host,client}.log`,
`logs/c227-c2m1-host.log`, and the user's real play `logs/playtest-menu-20261107-172301.log` (host, FED_C2M1 loaded
at 164868436) and `logs/playtest-menu-20261108-191343.log` (client of the same route). Times below are host ms from
the 172301 log.

**M is 23, not 22.** The data table has 23 LOS rows: LOS_O05 has six rows, `LOS_O05_SO01` to `LOS_O05_SO06`. The
card's "SO02, SO04, SO07, SO08" are string table keys (`FED_C2M1_LOS_O05_SO0N_NAME`, also Objective_05_Sub_07/08
gameplay tags), not row ids; the O05 rows reuse keys (SO02's NAME twice). Row to text for O05 is not readable from
the name map; the run's card070/card049 lines give it. The five `*_Marker` rows (CrashedDropship, FlakBug,
Flakbug, Plasma, RallyPoint) are markers, not in M. Runtime lettered children (LOS_O02_SO05a/b,
LOS_O03_SO02a/b/c, LOS_O03_SO03a/b/c) are counter children of SO05, O03_SO02, O03_SO03, not rows; their Complete
rows are waited on as follow-ups.

No user log and no test log ever reached LOS_O05 (both real play sessions ended at checkpoint reloads after
LOS_O03_SO03c at 166516719); its rows below come from level script names only, and their actors and positions come
from the run's card070 lines.

| # | Objective (parent), counter | Real trigger, evidence | Driver steps | Synced follow-ups to wait on |
|---|---|---|---|---|
| 1 | LOS_O01 (top) | completes with its last sub (real play 165244740, same ms as SO02) | none of its own | host `sent CompleteObjective(LOS_O01)`, client replay; then the Setup adds of O02, O03, O04 in the same ms (O02, O02_SO01, Plasma_Marker, SO05 count 2; O04, O04_SO01, CrashedDropship_Marker; O03, O03_SO01, FlakBug_Marker, O03_SO03 count 3) |
| 2 | LOS_O01_SO01 (O01) "Secure the LZ" | added at landing (ActorReference_C_16, 165035516), completes at the LZ clear (165179288); c238 completed it after bug deaths at the drop zone | both warp to the drop zone (229312 38956 / 229612 38956), spawn-and-kill rounds there until the Complete row | host Complete row, client replay; Add LOS_O01_SO02 (ActorReference_C_1) |
| 3 | LOS_O01_SO02 (O01) | Objective_01_End_TriggerVolume presence (Setup_Objective01_Subobjective_02); completed 65 s after its add in real play; never reached by a test | both warp short of ActorReference_C_1, goto onto it (presence volume) | host Complete rows SO02 and LOS_O01, client replays |
| 4 | LOS_O02_SO01 (O02) "Destroy Plasma Bugs using the Nuke Launcher" | completes at Plasma_AreaStart presence, same ms as Add O02_SO02 (165476705); c238/c117a: at the warp to 158606 23488 | host warp 158906 23488 10615, client 158606 23488 10615, goto on | Complete row, replay; Add O02_SO02 (ActorReference_C_15), BP_PlayerPresence_C_5 (156649 23887) armed |
| 5 | LOS_O02_SO02 (O02) "Reach and back up Romeo Squad" | presence at the Romeo point (BP_PlayerPresence_C_5); real play 165660679, 184 s after its add with fighting; c238 completed it at the same warp | both goto onto ActorReference_C_15 / BP_PlayerPresence_C_5; bug rounds while waiting | Complete row, replay; Add O02_SO03 (ActorReference_C_22), BP_PlayerPresence_C_10 (153129 14937) armed |
| 6 | LOS_O02_SO03 (O02) "Retrieve the Nuke Launcher" | E on BP_WeaponPickup_NukeLauncher_C at (142131, 11973, 10677): GiveWeaponByClass 165858283, SetObjectiveVisibility by BP_PickupTracker_C; its Complete row came via the subsystem at O02's completion (165875537) | host warp to the pickup, look actor, `key E` | card021 GiveWeaponByClass NukeLauncher on host; Complete row (may come with O02) |
| 7 | LOS_O02_SO05 (O02), count 2: SO05a = BP_DestructibleSpawner_Bugnest_HumanGameplay_C_7, SO05b = _C_0 | nuke fired at each nest: SO05b 165869022 (3.7 s after the fire), SO05a 165875537; LOS_O02_Sub05_OnLevelObjectiveComplete_Event | host look actor each nest, `key LeftMouseButton 3000` (2 s NukeLauncher windup, c132 hb44-49); E on BP_AmmoPickup_Nukes_C between shots if the launcher is empty | Complete rows SO05b, SO05a, SO05; replays |
| 8 | LOS_O02 (top) "Nuke 'Em All" | completes with SO05 (LOS_O02_0O5_Complete, 165875537) | none | Complete row, replay |
| 9 | LOS_O02_SO04 (O02) "Load the Nuke Launcher" | ammo pickup (BP_AmmoPickup_Nukes*); client log 259655568, about 20 s after O02 | E on the nearest BP_AmmoPickup_Nukes_C | Complete row, replay |
| 10 | LOS_O04_SO01 (O04) "Support troopers at crashed Viking Dropship" | CrashedDropship_Start presence; c238 at the host warp 196420 3390 7433; real play 166023744 | host warp 196420 3390 7433, client 196820 3390 7433, goto on | Complete row, replay; Add O04_SO02 (ActorReference_C_4), BP_PlayerPresence_C_11 (195890 1113) armed |
| 11 | LOS_O04_SO02 (O04) "Hold off incoming Arachnid forces" | wave defence; completed 121 s after its add (166144716) with LOS_O04 and CrashedDropship_Marker | both stay in BP_PlayerPresence_C_11; kill rounds on the spawned waves until the Complete row (timeout 300) | Complete rows SO02 and O04, replays |
| 12 | LOS_O04 (top) "Hard Landing" | completes with SO02 | none | as row 11 |
| 13 | LOS_O03_SO01 (O03) "Destroy the Flak Bug Emplacements" | Flakbug_Start presence; c238 at the host warp 228451 2890 9205; real play 166268174 | host warp 228451 2890 9205, client 228451 2790 7905, goto on | Complete row, replay; AddCount O03_SO02 (count 3; a/b/c = BP_SatchelItemPickup_C_2/_5/_3), BP_PlayerPresence_C_12 (230829 -15976) armed |
| 14 | LOS_O03_SO02 (O03), count 3 "Collect the C4 Satchels" | E on each BP_SatchelItemPickup_C (card043 interact press then card050 DisablePickup, 166319899); LOS_O03_0O2_Complete | host warp short of _2, _5, _3 in turn, look actor, `key E`; client warps along | Complete rows SO02a, SO02c, SO02b, SO02; replays (card056 PickupGone on client) |
| 15 | LOS_O03_SO03 (O03), count 3 "Use C4 satchels to destroy Flak Bugs" | E held about 3 s at each placement (166410329 press, 166413187 release, SO03a 166415837); LevelObjective_03_FlakBug01/02/03_Destroyed | host warp short of ActorReference_C_17, _19, _20 (BP_SatchelBuilding_Flakbug_01/02/03), look, `key E 3000` | Complete rows SO03a/b/c, SO03, replays; flak bug destroy effects on client |
| 16 | LOS_O03 (top) "Cut Me Some Flak" | completes with SO03 (LOS_O03_0O3_Complete) | none | Complete row, replay; then Setup_Objective05_BoardMechsuit adds LOS_O05 |
| 17 | LOS_O05_SO01 (O05) "Enter the M7 Razorback" (text SO01) | OnWantsToBoardMech / MechBoarded; mech at a BP_MechDropPlatform_C | warp short of the card070 actor, look actor the mech, `key E` (c127-c4m1 mech entry); client too | card075 MechEnter on host for the client; Complete row, replay |
| 18 | LOS_O05_SO02 (O05) "Reach the Rally Point" (text SO02, RallyPoint_Marker) | Setup_Objective06_ReachArea presence | warp short of the card070 actor, goto on | Complete row, replay |
| 19 | LOS_O05_SO03 (O05) "Make your way through the caves" (text SO03) | FlakBug_CaveTrigger / ActorReference_FinaleCave_01-03 presences | goto each card070 actor in turn | Complete row, replay |
| 20 | LOS_O05_SO04 (O05) "Fend off the Arachnid Forces to establish an LZ" | Setup_Objective08_DefendArea; FinalWaveCompleteCount / WaveCompleteCount | stay in the area, kill rounds until the Complete row (timeout 400) | Complete row, replay |
| 21 | LOS_O05_SO05 (O05) "Save Ethan" (text SO05 C4 / SO06 debris / SO07) | Setup_Objective06_SaveEthan, EthanFound, EthanDeath_Part1-6 presences, debris destroyed with C4 | E on satchel pickups, E hold at the debris placement, presences on the card070 actors | Complete row, replay |
| 22 | LOS_O05_SO06 (O05) "Get to the Dropship!" (text SO08) | Setup_Objective09_Evacuate, TriggerBox_EvacShip/EvacShip2, EvacTroopCount, BP_PlayerTransportDropship_EndLevel | after Evacuation1/2_OnLanded, both goto into the evac trigger at the card070 actor | Complete rows SO06, O05; EvacTakeOff; WB_TrooperLevelEnd |
| 23 | LOS_O05 (top) "Mission Abort" | completes with its last sub; level end | none | Complete row, replay; mission-end tail |

Risks:
- O05 (rows 17 to 23): no log has ever reached it, so the order of SO01 to SO06 and their actors are known only at
  run time. The scenario reads each sub's actor from its card070 line (`{g1}`) and moves both players there; run 1
  is the first evidence. If a sub needs an action other than presence, E, E hold, fire or mech entry, I stop and
  report.
- Mech boarding (row 17): `key E` is the c127-c4m1 route for a placed BP_BaseMech_C; whether the M7 Razorback here is
  the same class is unknown until run 1.
- Satchels and flak bugs (rows 14, 15) were done by the host in real play; whether the client can also carry
  satchels is not tested here.
- Mission end: WB_TrooperLevelEnd is one shared asset (`.../LevelEndScreen/WB_TrooperLevelEnd.uasset`, only one in
  the pak), so the button function is `BndEvt__TrooperLevelEnd_WB_Button_Next` as in C1M1.

## Runs 1-3 (senior read-out)

| Run | Completed of 23 | Not completed, cause |
|---|---|---|
| 1 (`c241-c2m1`) | stopped early | loop on a wrong ammo actor name |
| 2 (`-r2`) | 15 | nukes fired from 24000+ uu; site b point; LOS_O05 group not driven |
| 3 (`-r3`) | 12 (adds LOS_O02, O02_SO03, O02_SO05) | O02_SO04: `warp actor ... 0` refused (dist 10-5000); O03_SO02: satchel letters follow collection order, blocks gated per letter; O03_SO03: site c moved with its building; LOS_O03 and all LOS_O05 blocked by these |

All causes are scenario faults. Mod findings: cards 251, 252, 253. No crash in any run.

Budget extended by 2 runs (senior, 2026-11-12) with one goal: 23 of 23 and travel to FED_C3M1.
| 4 (`-r4`) | 16 (first run to reach LOS_O05) | client boarded the only mech first: `LOS_O05_SO01` open (card 255); run void for frame windows (other sessions' GPU jobs) |
| 5 (`-r5`) | 20, same set both sides | `LOS_O05_SO05` is a hold zone (`BP_HordeZone_C` at (155570, -43881, 9037)); the scenario left after 20 s; `SO06` and `LOS_O05` follow it; no level end, no FED_C3M1 travel |
| 6 (`c255-c2m1-r2`) | 18 | card 255 proof (client pilots); `LOS_O05_SO04` not entered: its trigger volume is 1000 uu from the marker (card 257) |
| 7 (`c257-c2m1`) | 21 | `LOS_O05_SO05` hold zone completed 945 s after the players entered it, the scenario waited 400 s and never drove `SO06` (marker `ActorReference_C_13`, added in the same tick); `LOS_O05`, level end and travel follow it |
| 8 (`c241-c2m1-r8`) | 20 | hold `LOS_O05_SO05` still open 1180 s after zone entry (early stop); 36 nearest-bug kills in the hold did not shorten it; `SO05`, `SO06`, `LOS_O05`, level end open. New digest class `STLevelObjective` in rounds 21, 22 (card 258) |
| 9 (`c241-c2m1-r9`) | stopped at 01:12 | stalled at `LOS_O04_SO02`: `kill actor` locked onto a Warrior corpse (guid 0x9ce), EM C_70 never ended, C_71 and the boss EM never started. Test-helper fault (card 259); run 10 runs under card 259 |
| 10 (`c241-c2m1-r10`, card 259 DLL `61cf21ee94ecdd3b`) | **23 of 23**, same set on the client | accepted 2026-11-13. O04 chain C_70, C_71, C_67 ran; `LOS_O05_SO05` 194490504, `SO06` and `LOS_O05` 194528438; host `WB_TrooperLevelEnd` 194561958; both in FED_C3M1 (host 194659693, client 194664486); 0 crash; digest 1 of 35 rounds with mismatches (Pose:4, card 251 class); client 644 frames over 50 ms (card 252). Open: no level-end screen on the client (card 260); 10 driver FAIL lines from the `unless ... when host last cmd` parse (fix moved into card 242) |

Run 5: no crash, digest 83 rounds with 1 mismatch (`Pose:1`, card 251), client 16 windows under 500 (card 252).
Mod findings from runs 3-5: cards 253, 254, 255, 256. The last 3 ids and the travel are driven in the
card 255 proof run (same scenario, one build and run for both).

## Run 8 step 1: O05 tail trace (static, junior)

Sources:
- K = kis listing of `FED_C2M1_Scripting_C` (offsets in `ExecuteUbergraph_FED_C2M1_Scripting`).
- H = `BP_HordeZone_C`.
- W = `BP_EM_1FactionSetWaves_C`.
- M = the instance data in `FED_C2M1_Scripting.umap` (unversioned bytes, read by hand).

### What completes `LOS_O05_SO05`

The objective completes through two encounter managers in sequence. No timer and no player check are involved.

1. K 21846, in the `Conversation_09_EthanDeath_PartThree` path (reached by `goto 21846` at 22340), runs these in order:
   - `BP_HordeZone_C_3.Begin KillEmAll(false)`
   - `Trigger Encounter Manager` on `BP_EM_1FactionSetWaves_C_72` and `_73`
   - `SetTriggerEventsEnabled(false)` on presence volumes 78, 79 and 81

   In c257 the presence flags dropped back at 185459910. That marks the start of EM_72: 44 s after the SO05 add (185416305) and before the players entered the zone (185491 s).
2. M, EM_72 (`BP_EM_1FactionSetWaves_Bugs_EndArea_Wave1`): `WaveData` holds 4 waves. The `WaveRemaining%Trigger` rows have `TriggerReason` 0 (W `SetupTriggerReason` 74: `SetWaveRemainingPercentTrigger`) with values 30, 50, 50 and 30. Each next wave starts when that share of the current wave is still alive, so the waves advance on kills only.
   - EM_73 (`Wave1_Support`, 3 waves, 50/50/50) has no bound event in K and does not gate SO05.
3. K, event `BndEvt..._BP_EM_1FactionSetWaves_C_72_..._EncounterDelegate`: when EM_72 ends, it runs `goto 21302`, `goto 18621`, which triggers EM_66 (18621; `Wave2`, 3 waves, 50/25/50, no bound event) and EM_0 (18657; `Bugs_EndArea_Boss`, 1 wave).
4. K, event `BndEvt..._BP_EM_1FactionSetWaves_C_0_..._EncounterDelegate`: when EM_0 ends, it runs 17661 to 17733 (`Trigger Encounter Manager` on the loops `BP_EM_1FactionLoop_C_22`, `_21` and `_10`), then 17769 `SetUpFinale()`, which reaches `goto 15704`. That block runs:
   - 15791 `Setup_Objective09_Evacuate()`: 15534 adds `LOS_O05_SO06` with `[ActorReference_C_13]`
   - 15805 `goto 15586`: `CompleteSubObjective(LOS_O05, LOS_O05_SO05)`

   So the SO06 add and the SO05 complete land in the same tick (c257: 186435443 and 186435444).

Players:
- No step checks for a player. EM_72 and EM_0 have no presence volume (78, 79 and 81 are switched off at the start).
- H has no completion logic. `Begin KillEmAll`, `Kill Em All Passive Setup` and `Update Progress Bar` only toggle fog, the hazard zones, collision, the boundary FX and the UI bar.
- Being inside the zone, alone or together, therefore neither starts, pauses nor resets the waves.

Scenario lines 1895-1910:
- The `kill actor` steps (InstaKillTarget on the nearest live `BP_AI_WarriorBug_C` or `BP_AI_CenturionBug_C`) speed SO05 up, but only when the bug killed belongs to an EM_72 or EM_0 wave. The host tracked 100 to 148 NPCs at the time, so the nearest bug is not always one of them.
- The re-warps neither speed it up nor slow it down. Nothing resets.

The 945 s:
- From the EM_72 start (185459.9 s) to the EM_0 end (186435.4 s) is 975 s. This is the time it took to kill 4 EM_72 waves and then the EM_0 boss wave.
- The scenario killed at most 12 bugs (6 rounds of 2) in the first ~5 min. After that it only waited (400 s, then 180 s).
- Allied troopers and stray fire did the rest. The host saw about 4 NPC deaths per 15 s window.

Not provable from the log:
- The mod has no log line for the EM_72 end or the EM_0 start.
- The card111 and card112 wave lines stop at 185155228 because the `c111_line` budget is used up.
- The wave classes are package imports I did not resolve.

### What completes `LOS_O05_SO06` and `LOS_O05`, and what raises `WB_TrooperLevelEnd`

The marker and the dropship:
- Marker `ActorReference_C_13` (M, DefaultSceneRoot): (147369, -38883, 9930).
- 15704 runs `BP_PlayerTransportDropship_C_4.StartAction(0,0,0,None)`. In c257 it landed at 186471388 (`collision: host/standalone DescendToLandCompleted(... BP_PlayerTransportDropship_C ...)`) at (143790, -43530, 11395), 36 s after the add.
- Event `..._C_4_..._45_DropshipEventDelegate` (landing) runs 23152 `SetIsInteractable(true)`.

What completes it: the event `..._C_4_..._52_PlayerEntering`, fired by the native `EnterDropship` on the authority when the player presses E on the dropship. It runs `goto 23186`, `goto 21429` (music `MSS_MUS_FlyingSequence`), `goto 21476`, `goto 15277`:
- 15277 `Conversation_010_Outro()`
- 15291 `BP_HordeZone_C_3.End KillEmAll()`
- 15327 `CompleteObjective(LOS_O05)`
- 15351 `goto 8168`: PrintString 'PLAYERS IN DROPSHIP', then 8254 `CallFunctionWithDelay(32.5, 'End Level')`, then 8284 dropship `StartAction(1,1,2)` (take-off)

No line completes `LOS_O05_SO06` by itself. It closes with its parent at 15327.

Who must board: one player is enough. No step counts players or checks for both. Reaching the marker does nothing; only boarding the dropship counts.

Level end: `End Level` (export at 23073) runs 23094 `STGameInstance:EndLevel()`, which raises `WB_TrooperLevelEnd` (card018 lines). Event `..._C_4_..._35_PlayerSkippedFlight` (23147) calls `End Level` at once. That is the skip-button path, so with the cinematic skip it can come before the 32.5 s.

Other timers:
- 15743 `CallFunctionWithDelay(10.0, 'EndLevelEvacuationDropships')` (19607) only starts dropships 17 and 18.
- `ActorReference_C_14` (SO05 marker) sits at (152520, 23320, 10943), not at the scenario's HORDE point. This does not matter, since SO05 does not depend on where the players are.

### Predictions for run 8

- **SO05:** at most 945 s after zone entry, the c257 baseline with almost no kills. With a kill loop that runs through the whole hold (nearest Warrior, Centurion, Chariot and Aphid every round), I predict at most 300 s.
  - Proof: the SO05 Complete row (host, then client), plus the `cmd: \d+ kill: InstaKillTarget(` lines between zone entry and that row.
  - Early stop: SO05 still open 450 s after zone entry.
- **SO06 add:** the same ms as the SO05 complete (+0 to 1 ms). The `card070: host t=\d+ LOS_O05_SO06 actors 1: LOS_O05_SO06 ActorReference_C_13` line is no later than the SO05 row.
- **Dropship landing:** about 36 s later, `collision: host/standalone DescendToLandCompleted(... BP_PlayerTransportDropship_C ...)`.
- **Boarding:** host E on `BP_PlayerTransportDropship_C` after the landing line gives:
  - `collision: host/standalone EnterDropship(... BP_PlayerTransportDropship_C ...) player-inside flag 0 -> 1`
  - in the same tick, the `LOS_O05` Complete row (SO06 may only show as closed with its parent)
- **Level end:** 32.5 s later at most, `card018: host SetInputMode_UIOnlyEx called by BP function ExecuteUbergraph_WB_TrooperLevelEnd` on the host and the client. Then travel to FED_C3M1, as in the existing END block.

Planned scenario change for step 2 (in `gen_c241.py`; the scenario must stay under 2000 steps, it has 1992 now):
- **ZONE:** each round becomes 2 warps, 4 nearest kills (Warrior, Centurion, Chariot, Aphid) and a 15 s wait on the SO05 row. I would trim the round count so the step total does not grow.
- **SO05 timeout:** set to 450 s from zone entry, followed by the `void` early-stop step.
- **Removed:** the 180 s `card070 SO06` wait (the add comes in the same tick) and `sweep('LOS_O05_SO06')` (3 steps per REFS candidate).
- **SO06 boarding block:** wait on the `DescendToLandCompleted` line (timeout 120), host warp to the dropship, host E, then wait on the `EnterDropship` line and the `LOS_O05` row (timeout 60).

## Run 9 step 1: SO05 hold, encounter accounting (static, junior)

Sources: the game exe disassembly (PDB names), the `FED_C2M1_Scripting.umap` instance data, the four wave-data
assets, and the host logs `logs/c257-c2m1-host.log` (c257) and `logs/c241-c2m1-r8-host.log` (r8).

### a) InstaKillTarget counts in the wave accounting

Yes. It counts only for a bug that the wave spawned.

- `USTCombatUtilityLibrary::InstaKillTarget` (0x05241580) calls `DealDamage` (0x05233f60), which uses the normal
  gameplay-effect damage path, the same one a gun kill takes.
- `UADHealthComponent::FatalDamageEffectReceived` (0x04a3b8f0) broadcasts the death delegate at health component +0xf8.
- `USTEncounterWaveBase::AddBindingsOnSpawnedNPC` (0x05116ac0) binds two delegates on every NPC the wave spawns:
  - +0xf8 to `OnNotifyEncounterEnemyKilled` (0x05131440)
  - +0x118 to `OnNotifyEncounterEnemyDamaged`
- `OnNotifyEncounterEnemyKilled` runs these steps:
  1. IsA `ASTNonPlayableCharacterBase`
  2. `GetNPCGuid`
  3. removes that guid from the wave's set
  4. `RemoveEnemyFromEncounter` (0x051366d0): `decl 0x350` (the alive count), then `IsWaveLossesThresholdMet` and
     `IsWaveRemainingThresholdMet`, and fires the trigger delegates
- A kill of a bug that does not belong to the active wave changes nothing. It does not stall the wave either.

### b) Spawners, positions, classes

| Encounter | Spawner | Position (approx) | Waves (base budget) |
|---|---|---|---|
| EM_72 Bugs_EndArea_Wave1 | BP_AISpawner_C_111 | (159673, -43227, 8851), 4.1k east of HORDE | WhiskeyWarriors 40, Warriors_ExtraLarge 32, Warriors_Large 24, Warriors_ExtraLarge 32 |
| EM_73 Wave1_Support (does not gate SO05) | BP_AISpawner_C_112 on BP_TentSpawner_HumanGameplay_C_2 | near (172117, -14043) | n/a |
| EM_66 Wave2 (does not gate SO05) | STAISpawner_7 | (164603, -3399, 10660), 40k north | WhiskeyWarriors, ExtraLarge, ExtraLarge |
| EM_0 Bugs_EndArea_Boss | STAISpawner_6 | (158570, -34162, 9490), 9.7k north | CenturionsVariable 12 (BP_AI_CenturionBug_C) |

- The Warriors data spawn the Warrior family. Near HORDE, r8 killed Warrior, Chariot and Aphid.
- EM_0 spawned 3 Centurions within 0.7 s in both runs:
  - c257: (154587, -33899), (159534, -37248), (154095, -44461)
  - r8: (154124, -33272), (159191, -37544), (154510, -43863)
- The Centurion of another encounter spawns earlier, at about (204k, 6k).

### c) Progress lines and times

| Marker | Line | c257 | r8 |
|---|---|---|---|
| EM_72 start | `card132: host ext BP_PlayerPresence_C ... 0x07000001 -> 0x07000000` | 185459910 | 189492732 |
| Zone entry | first `warp actor point 155570 -43881 9037` | about 185491000 | 189523090 |
| EM_72 end = EM_0 start | first `fx audit: SpawnSystemAttached NS_Compute_DamageAndRadius at (15xxxx, -3/4xxxx, ...) owner BP_AI_CenturionBug_C` | 185951475 (+460 s) | 189802560 (+279 s) |
| EM_0 end | 3rd `PlaySoundAtLocation MSS_BARK_CenturionBug_Death`, then the SO05 Complete row 13 ms later | 186435431 / 186435444 (+945 s) | not reached: 2 of 3 Centurions dead (190312430, 190711002) at log end 190744120 |

Why r8 was slower:
- r8 finished EM_72 sooner than c257 (310 s vs 491 s). Its 36 kills (12 rounds, 189524451 to 189744417) went in
  near the EM_72 spawner.
- The kill rounds ended 58 s before EM_0 started. The only Centurion kill fell on the other encounter's Centurion
  (destroyed at 189167929), so the later rounds logged "no such actor".
- During EM_0 the scenario only waited, and almost no allied troopers were near (footsteps: 7 in r8, 306 in c257).
  In c257 the allies killed the 3 Centurions in 484 s. In r8 they had killed 2 when the early stop hit.
- Both runs logged about 88 bug deaths (BloodDeath) before EM_0 started.

### d) The 1.6x wave budget scale (card 111)

| Encounter | 1 player | 2 players |
|---|---|---|
| EM_72 | 40+32+24+32 = 128 points | 64+51+38+51 = 204 points |
| EM_0 | 12 (about 2 Centurions) | about 19 (3 Centurions, about 6 points each) |

- One Warrior is about 1 point.
- With the remaining-percent triggers (30, 50, 50, 30), the hold needs about 79 kills for 1 player and about 126
  for 2 players. If every bug must die, it needs 128 and 204.
- The about 88 deaths seen in both runs fit the threshold reading, with some of the kills before the zone.
- The scale makes the hold about 1.6x longer whenever the bugs are not killed faster.

### Scenario change (gen_c241.py ZONE; 1990 steps, cap 2000)

- EM_0 start regex `EM0_RX`: the Centurion spawn fx at x 15xxxx, y -3xxxx/-4xxxx. In c257 and r8 it first matches
  at the EM_0 start (185951475, 189802560). It never matches the earlier Centurion at (204k, 6k).
- Phase 1, 7 rounds, skipped once SO05 is done or EM0_RX has matched:
  - 2 warps onto HORDE
  - 8 kills: Warrior x4, Chariot x2, Aphid x2. No Centurion kill, because EM_72 spawns none.
  - wait up to 20 s on EM0_RX
- Phase 2, skipped once SO05 is done:
  - wait up to 450 s on EM0_RX
  - host warp to HORDE, sleep 2
  - 2 rounds of: 3 Centurion kills, then wait up to 15 s on SO05
- Final: wait up to 150 s on SO05, the shot, then the void step.
- Early stop: at most about 800 s after zone entry (168 s of rounds, 450 s, about 35 s, 150 s).
- BOARD and END: unchanged from r8.

Prediction:
- EM_0 start: 120 to 250 s after zone entry (8 kills per round, against 4 in r8, which reached 279 s).
- SO05 Complete: within 10 s of the EM_0 start, so 130 to 260 s after zone entry.
- Proof lines in order:
  1. the phase 1 `cmd: \d+ kill: InstaKillTarget(BP_AI_WarriorBug_C|ChariotBug_C|AphidBug_C ...)` lines
  2. the first EM0_RX line
  3. 3 `kill: InstaKillTarget(BP_AI_CenturionBug_C ...)` lines
  4. the 3rd `MSS_BARK_CenturionBug_Death` line
  5. the SO05 Complete row within about 20 ms
  6. the SO06 add (`card070 ... LOS_O05_SO06`) in the same ms
- If EM_0 has not started by the end of phase 1, the 450 s wait still catches it, and the cause is the kill count.
  The first thing to check is how many phase 1 kills logged "no such actor".

Correction to the step 1 plan above: the final SO05 wait in the r9 scenario is 600 s, not 150 s.

## Run 9: O04 stall

Static trace written after r9 was stopped. Sources: the kis listing of FED_C2M1_Scripting_C, the BP_EM_1FactionSetWaves instance data, the native wave code, and the r5, c257, r8 and r9 logs.
r9 logs are kept under the r9 tag: `logs/c241-c2m1-r9-host.log`, `-client.log`, `.drive`, `-host.stdout`, `-client.stdout`.

### 1. The O04 chain from the SO02 add to the Centurion

The SO02 add (`Setup_Objective03_Subobjective_02_CrashedDropship`, fired from the `BP_PlayerPresence_C_11` event) does not trigger any encounter manager (EM).
The first EM, `BP_EM_1FactionSetWaves_C_9`, starts itself through its PlayerDetection component.
Each later EM is triggered from the previous EM's end, through `BndEvt_..._EncounterDelegate`:

| EM | Label | Spawner | Waves | On encounter end |
|---|---|---|---|---|
| C_9 | DropshipDown_WarriorBugs | BP_AISpawner_C_2 | 1 wave, Warriors_Medium | Trigger C_70; destroy StaticMeshActor_97 |
| C_70 | DropshipDown_WarriorBugs2 | C_108 | 1 wave, Warriors_Medium | Trigger C_71; destroy SMA_95 |
| C_71 | __DropshipDown_WarriorBugs3 | C_109 | 2 waves, Warriors_Large then Warriors_ExtraLarge | Trigger C_67; camera shake, audio, bark |
| C_67 | __DropshipDown_BossWave | C_73 | 1 wave, Centurion_Boss (explicit list, budget base 1) | CompleteObjective(LOS_O04), Conversation_06, checkpoint, Setup_Objective05 |

**Trigger reasons.**
`SetupTriggerReason` maps the enum values as follows:

| Value | Trigger |
|---|---|
| 0 | remaining percent |
| 1 | remaining count |
| 2 | boss health |
| 3 | bosses remaining |
| 4 | time |
| no entry | 25 percent remaining |

The per-wave threshold values are not decoded from the instance bytes.
In r5, c257 and r8, every EM end and every next-wave start fell in the same ms as a kill.
So the thresholds are kill-based (remaining or losses), not timers.

**Kill accounting.**
`OnNotifyEncounterEnemyKilled` calls `RemoveEnemyFromEncounter` (exe+0x051366d0).
That function decrements the wave's alive count at +0x350, then tests `IsWaveLossesThresholdMet` and `IsWaveRemainingThresholdMet`.

**What the boss wave needs.**
Enough C_70 warriors must die to meet C_70's remaining threshold.
Then C_71's two waves must meet theirs.
Only then does C_67 trigger and spawn the Centurion.
No timer anywhere in the chain advances it.

### 2. Wave budget

The budget is computed by `SetupReinforcements` (exe+0x05137a30):

`budget = max(MinWaveBudgetClamp[+0x180] - live[+0x1bc], min(ReinforcementWaveBudget[+0x184], WaveBudget[+0x178] - live))`

Each term is floored at 0.

**Inputs.**
- The wave data's budget fields.
- The EM's live count of its own enemies.

**Not inputs** at this point:
- player count
- distance to players
- spawn point validity
- ticket manager state

The result passes through `FactorDifficultyIntoWaveBudget` (exe+0x0511e8a0), which applies difficulty.
It has two call sites:
- return 0x5137BD0: reinforcement
- return 0x512940C: InitialiseWave

**Base 0** means the EM is at its cap: nothing to spawn now, and it retries on the next reinforcement tick (about 20 s).
It does not count as a started wave that can never finish.

**`Troopers_Medium`** is the allied trooper EM (C_12, delegate to 2945), which is not in the O04 chain.
- r8 logged base 10 seven times; r9 logged base 0 five times.
- The difference is that r9 had more live allied troopers, so that EM sat at its cap.
- This does not affect C_9, C_70, C_71 or C_67.

### 3. Mod code in the path

| Mod code | In the path? | Detail |
|---|---|---|
| Card 111 `bug_scale.cpp` `detour_c111_factor` (about line 383) | Yes (only scales the result) | Multiplies the original `FactorDifficultyIntoWaveBudget` result by 1.6 via `lroundf` (lines 179-181), so 0 stays 0. On the client it returns the original. It does not touch thresholds or triggers. |
| `c114_wave(data CDO)`, called at `bug_scale.cpp:384` (`difficulty.cpp:1066`) | Yes | Applies the difficulty file's section to the wave data CDO. The file is the same in every run, so it cannot make r8 and r9 differ. |
| `detour_c112_activate` (about line 429) | No | Log only. |
| Ticket limits, scaled by ceil(x1.6) | No | They affect attack tokens, not wave budget or wave thresholds. |
| Card 140 | No | Corpse/gib pose sync. It is not on the spawn or kill-count path. |
| Player count or client pawn position | No | Nothing in the mod is keyed on these in the budget or trigger path. |

**The mod is in the scenario's kill path.**
Scenario `kill actor X` goes through:
1. `cmdfile.cpp:264`
2. `testrun.cpp` `c98_kill`
3. `c64_find_actor(world, c.actor, actor_location(pawn), pawn, true)` (`hooks_ubw.cpp:1430`)

`c64_find_actor` picks the nearest actor of that class.
It skips an actor as dead only when `c23_set_health(actor) == 0.0f`.
`c23_set_health` is `c21_health` (`hooks_ubw.cpp:486`).
That returns the health set's current value, or -1 when the actor has no set, and -1 stays a candidate.
Then `InstaKillTarget` (exe+0x05241580) calls `DealDamage(actor, actor, health+armour, true)`.
This is test-driver code, not play-build behaviour.

### 4. First divergence between r8 and r9 after the SO02 add

Both runs used the same route and the same DLL, and the client was warped to (196420, 3390, 7433).
The SO02 add and presence_11 fired the same way in both runs.
The host stood at about (195.9k, 1.0k, 7645) through the fight.

**Before the divergence:**
- C_9 took 76 s in r9 against 31 s in r8. Its 26 kills matched 26 deaths.
- In r9, the allied troopers were at their cap (`Troopers_Medium` base 0).

**First divergent line.**
The C_71 trigger (`card111 ... Warriors_Large_C ... caller exe+0x512940C`) is in r8 at 188975706 and missing from r9.
In r9, C_70 triggered at 192000509 and had not ended when the log stopped at 192726121.

**Cause in r9.**
From 192044988 on, the scenario's `kill actor BP_AI_WarriorBug_C` targeted the corpse of the warrior it had just killed:
- Warrior 0x8f2 at (196047, 1009, 7668) was killed by kill 201 at 192044576 and ragdolled at 192045455.
- Next, warrior 0x9ce at (195981, 998, 7660) ragdolled at 192070285. From 192070208 to 192291373 it was the target of every kill, at 82 to 90 uu from the host.

**Kill counts:**

| Window | Kill commands | Deaths |
|---|---|---|
| Before the corpse lock | 58 | 15 |
| After it | 36 | 4 |

Both runs logged 58 `kill actor BP_AI_WarriorBug_C -> done` lines in the fight window. In r8 those kills ended the chain, and the Centurion kill succeeded once. In r9, all 29 Centurion kills failed with "no such actor".

The live filter did not skip the corpse, because its health set did not read 0.
Meanwhile live warriors kept hitting the host, for 25 damage about once a second (god mode).
So C_70's remaining threshold was never met, and C_71 and C_67 (the Centurion) never triggered.

### 5. Verdict

**Most likely a scenario fault**, from the test kill helper.
`c64_find_actor`'s live filter accepts a ragdolled corpse whose health set is not 0.
The helper then locks on that corpse, and live C_70 warriors farther away are never killed.
Real players would shoot the live warriors, so the chain would advance.
The wave, budget and trigger logic matched r8, and no mod budget code can hold a kill-based threshold.

**Alternative, not ruled out statically:** 0x9ce was alive but ragdolled and immune to `DealDamage`, and it still counted toward C_70's alive count.
In real play, that would be a blocker (game or mod), because a player could not kill it either.

**The single deciding line:** for each kill command, the target's guid, its health-set value before and after `InstaKillTarget`, and its dying/ragdoll state.
If the health set stays above 0 after the kill, the target is either a corpse that never had its health zeroed (scenario fault: the filter needs a dying check) or an immune live pawn (blocker).
An equivalent alternative is a per-enemy `RemoveEnemyFromEncounter` line with C_70's wave alive count (+0x350).
If 0x9ce's death was already removed from the count, it is a corpse and the fault is the scenario.
