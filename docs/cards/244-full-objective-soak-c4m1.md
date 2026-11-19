# 244: Full-objective soak, FED_C4M1

Same shape and rules as card 240 (read `docs/cards/done/240-*.md` or `docs/cards/240-*.md` first: no
`objective complete`, wait on the host Complete row and the client replay, both players in presence volumes,
objectives dumps at the end, faults become their own cards).

Goal (one): one scenario that plays FED_C4M1 from spawn to the mission end and completes every objective by
its own trigger.

Objective list M = 22, from the game's data table `DT_LevelObjectiveData_FED_C4M1` (keys
`FED_C4M1_<id>_NAME`; the runtime ids may differ in case):
BRIDGE_O1
BRIDGE_SO1
BRIDGE_SO2
BRIDGE_SO3
BRIDGE_SO4
LZ_O1
LZ_SO1
LZ_SO2
METRO_O1
METRO_SO1
METRO_SO2
METRO_SO3
METRO_SO4
METRO_SO5
PARK_O1
PARK_SO1
PARK_SO2
STADIUM_O1
STADIUM_SO1
STADIUM_SO2
STADIUM_SO3
STADIUM_SO4
The last soak (c127-c4m1-r2) sent 0 sub-objective completions: none

Step 1, static, no run, appended here as a table: each id in play order with parent, counter, real trigger
with level-script or class evidence (`FED_C4M1_Scripting`), the driver steps that fire it, the synced
follow-ups to wait on, and any id that is a marker only or on a branch the map never activates (state the
evidence; such an id leaves M only with the senior's agreement).

Step 2: `tools/scenarios/c244-c4m1.txt` (new file; older scenarios stay).

Step 3: run, budget 3 runs. Command:
`systemd-run --user --scope -q -p MemoryMax=12G env SCENARIO=tools/scenarios/c244-c4m1.txt DEPLOY=1 FAST=1 HOST_EXTRA_ENV="UBWCOOP_TRAVEL=/Game/Maps/FED/FED_C4M1?listen UBWCOOP_GOD=1 UBWCOOP_C132_FX=1 UBWCOOP_C136_LOG=1" CLIENT_EXTRA_ENV="UBWCOOP_C132_FX=1 UBWCOOP_C136_LOG=1" tools/run_pair.sh c244-c4m1 3600`

Files allowed: `tools/scenarios/c244-c4m1.txt`, this card. Driver verbs only after a report to the senior.
No other mod file. No build while a game runs.

Done when: final objectives dump shows `complete 1` for M of M on host and client, lists match, both sides
reach the next map (FED_C4M2), 0 crash, every digest round 0 mismatches, frame
windows at or over 500 outside load. Report: completed N of M, each fault with its lines, new WARN classes
against c127-c4m1-r2.

## Senior prep (2026-11-13): what the junior needs before step 1

Not started. Starts after the 2026-11-13 playtest, in card order, one map at a time.

- Model for step 1: the "Step 1 result" section of `docs/cards/243-full-objective-soak-c3m2.md` (line 80 on):
  objective table (id, parent and counter, real trigger with ubergraph offsets, driver steps, wait rows), then
  the encounter manager chains, presence volumes, switches with their stand side, timed holds and the mission
  end. Same columns here, from `FED_C4M1_Scripting` and `DT_LevelObjectiveData_FED_C4M1`.
- Rules that card 243 added and this card inherits: "Rules carried from cards 241 and 242" in card 243, and
  "Gameplay test scenario rules" in `docs/STATE.md`. In short: no `objective complete`; waits on log rows, never
  sleeps; `clear` before every press, aim, throw and damage step (card 265); a `shot` after every warp and look
  at a target, each one viewed (card 264); menus close by `winkey` or `ui continue`, never `key` (card 222).
- Scenario from a generator: copy `tools/gen_c243.py` to `tools/gen_c244.py` (it stamps the DLL hash guard and
  the map check); under 2000 steps; `python3 tools/drive.py --parse <file>` must report 0 bad before any run.
  The old route `tools/scenarios/c127-c4m1.txt` gives the walk order and the warp points only; its objective steps
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

## Step 1 result (junior, b205, 2026-11-17): triggers from FED_C4M1_Scripting

Source: kis.py listing of `FED_C4M1_Scripting.umap` (ubergraph `ExecuteUbergraph_FED_C4M1_Scripting`). Offsets
are ubergraph entry points; the calls are read linearly from the entry, so a row is a candidate until the discovery
run confirms it. Calls: AddObjective (imp850), AddSubObjective (imp851), AddCountSub (imp849, count arg),
AddCountSub2 (imp848, Stadium_SO4 count 2), CompleteSub (imp854), CompleteObjective (imp853), counter step (imp855).
Every id in M has an Add and a Complete call; none leaves M on static evidence.

| id | parent / counter | add | real trigger (entry offset) | scenario kind |
|---|---|---|---|---|
| LZ_O1 | top | Add Objectives 930 (CallFunctionWithDelay 3.0 at 18742) | CompleteObjective at 1261 / 7104 / 13310 (presence C_8 1150 path) | presence |
| LZ_SO1 | LZ_O1 | 987 | CompleteSub 19749 after presence C_255 19570 / C_256 19626, encounter EM_13 19611 | presence + hold |
| LZ_SO2 | LZ_O1 | Add LZ_SO2 19663 | presence C_257 19846 / C_3 19851, encounter EM_11 17938/19934 | presence + hold |
| PARK_O1 | top | 3385 (Park_Marker), AddObj 13755 | CompleteObjective 13262 | cascade |
| PARK_SO1 | Park_O1, count 3 (A,B,C) | Add Park_SO1 7338 | A 3596 (presence C_4 3465), C 4162, B 4204; imp852 on SatchelBuilding Flakbug C_0..2 (17397/17438/17479) | presence / satchel |
| PARK_SO2 | Park_O1, count 5 (A..E) | Add Park_SO2 7608 | Bugnest C_7 7170 (A), C_5 7212 (B), C_6 7254 (C), C_4 7296 (D); E 17818 after EM_8 17669 / Bugnest C_11 17860 | nest |
| STADIUM_O1 | top | AddObj 3192 | CompleteSub Stadium_Marker 9251; CompleteObjective 13334 | cascade |
| STADIUM_SO1 | Stadium_O1, count 4 (A..D) | Add Stadium_SO1 6135 (presence C_5 5729/5937) | A 4279, B 5109, C 5419, D 14698 (presence C_14 14586) | presence |
| STADIUM_SO2 | Stadium_O1 | Add Stadium_SO2 6586 (OnLevelObjectiveComplete bind 6476 on Stadium_SO1, 6640) | CompleteSub 8740 (in Add Stadium_SO4 8598) | cascade |
| STADIUM_SO3 | Stadium_O1 | Add Stadium_SO3 14248 (presence C_17 14243) | encounter EM_5 14382 / presence C_0 14474 (not linear-readable) | presence + hold |
| STADIUM_SO4 | Stadium_O1, count 2 | Add Stadium_SO4 8598 | counter step 8976 on encounter EM_2 8928 (twice) | hold |
| BRIDGE_O1 | top | AddObj 7128 (Bridge_Marker 3127) | CompleteObjective 12243 in Destroy Bridge 11957; also 13286 | cascade |
| BRIDGE_SO1 | Bridge_O1 | Add Bridge_SO1 10024 | CompleteSub 10606, presence C_9 10534/10539 | presence |
| BRIDGE_SO2 | Bridge_O1 | Add Bridge_SO2 10096 | CompleteSub 10896 | presence (open) |
| BRIDGE_SO3 | Bridge_O1 | Add Bridge_SO3 11078 (Bombing Run 11083) | CompleteSub 11725 | open: bombing run |
| BRIDGE_SO4 | Bridge_O1 | Add Bridge_SO4 12688 | Complete Bridge_SO4 12693 / Destroy Bridge 12206 | open |
| METRO_O1 | top | Add Metro Objective 12703 (presence C_12 12698) | after Metro_SO5 (no own complete read yet) | cascade |
| METRO_SO1 | Metro_O1 | Add Metro_SO1 12979 | CompleteSub 13377 | presence |
| METRO_SO2 | Metro_O1 | Add Metro_SO2 13179 | CompleteSub 14153 / 16672: presence C_13 14104, encounter EM_3 14119, presence C_191 16624/16660 | presence + hold |
| METRO_SO3 | Metro_O1 | Add Metro_SO3 13851 | CompleteSub 14205 / 16709: encounter EM_4 14191 | hold |
| METRO_SO4 | Metro_O1 | Add Metro_SO4 16425 | CompleteSub 16321 (presence C_70 16245 path); PanelSwitch C_0..3 15008..15023 | use |
| METRO_SO5 | Metro_O1 | Add Metro_SO5 16250 (presence C_70, encounter EM_7 16042) | not linear-readable | open |

Also bound, no objective call in the linear read: presence C_31 15028 (alarm sound), encounter EM_14 17865,
WeaponPickup_EPulse88 C_0 18778 (ADPickup), SatchelBuilding_Rubble C_2 24379, presence C_7 1366.

Step 2 (b205): `tools/gen_c244.py` writes the scenario objective-driven (card 284), with no id table: each of 60
blocks reads the live current step from the `objective now` summary row, `objective expect`, `clear 1500 kill`,
`objective go all`, shots, then the kind's action (use: client E at the card 284 use stand class; nest: two client
grenades with `hit=`), then `objective wait` with four wave-hold rounds and the client replay wait; with no current
step it waits for the next Add row. Digest round every 10 blocks. 1984 steps, parse 0 bad.

## b206 findings (c244b206a, c244b206b)
- Park_Marker left the doable list with no card049 sent row because it is a marker-only id: the level script hides it
  with SetObjectiveVisibility, and that call is replayed on the client. Not a sync gap.
- Satchel step (kind other, Park_SO1 Flakbug C_0..2, Rubble C_2): c244b206a voided all 18 looks on `within=1600`
  (a focus-swept look prints no distance row). c244b206b without within= still voided every look: "focus none" at
  161..223 uu from the site. The C1M1 CTF satchel needs its SO3 pickup before the SO4 site (b13 soak), so
  gen_c244.py now picks up the nearest BP_SatchelItemPickup_C first (goto, look focus=, E), then the site; BLOCKS 49
  for the 2500 step cap (2455 steps). Unproven: next lane 2 build's C4M1 run.
- card456 logged 40 "hold interrupted ... started 0, progress 0.00, holding 1, trace now null" rows per side (cap) on
  SatchelSiteCAC_BP_SatchelPlacementSite_C_CAT, caller exe+0x4A202EA, with E never pressed.
- Both runs: 5 of 22 ids (Park_SO2_A..E), 0 EXCEPTION, no travel to FED_C4M2. c244b206a 139 digest rounds all
  mismatches 0. c244b206b: 13 rounds Pose:1..2, all BP_AI_HopperBug_C corpses 166..323 uu from the host's (new find).

## b207 findings (c244b207a, build 3a4e79683266b40d)
- Satchel pickup works: block 09 client goto reached BP_SatchelItemPickup_C_0 (28.8 s, 3 stucks freed), look focus
  BP_SatchelItemPickup_C_0 at 179 uu, E at 617817450: card020 Interact on the pickup, card073 "BP_SatchelInventoryItem_C
  1: our count 0 to 1, writer the host frame", pickup collider 1 -> 0 by host row.
- Site still no focus: goto SatchelSiteCAC_BP_SatchelPlacementSite_C_CAT_2 reached (dist 54, pawn -6529 -75407 -541).
  The bounds look (cmd 77) traced 0 of 26 points; the focus sweep stepped all 26 and ended at pitch +29.6, "no hit
  within 10000 uu", focus none (shot c244-09-satchel: sky). E 3200 never ran (focus= gate void). Later blocks: the
  pickup look aims at Landscape 1700-2600 uu away (C_0 consumed), site looks hit BP_SatchelBuilding_Flakbug_C_2 or
  nothing. Park_SO1_C stays current to the cap; 5 of 22 ids again (Park_SO2_A..E).
- The site actor moves: BP_SatchelBuilding_Flakbug_C_2 turns (card408 yaw 45 -> 74), so the child site's bounds centre
  (-6649 -75191) is 220 uu off the origin the goto resolved (-6579 -75387).
- card456 "hold interrupted on SatchelSiteCAC_..._CAT ... trace now null" starts 121 ms after the goto end, so the
  interactor held the site as its target on the walk in. gen_c244.py now looks at the site's actor origin (no
  `bounds`) with focus=. Run c244b207b tests it.
- 0 EXCEPTION/FATAL; 128 digest rounds all mismatches 0; ended by the 3600 s cap at drive line 2384.

## b207 run 3 (c244b207b): the site gate, not the site
- The site does take focus: its focus name is the child actor's, SatchelSiteCAC_BP_SatchelPlacementSite_C_CAT_2, and
  `focus=` is a name prefix check, so `focus=BP_SatchelPlacementSite_C` voided every one. Focus rows on the site:
  c244b206b 8, c244b207a 2 (look 237, pitch 36.0), c244b207b 6 (looks 81, 113, 129, ...), each "focus
  SatchelSiteCAC_..._CAT_2, not focus=BP_SatchelPlacementSite_C", each E 3200 skipped. The site focuses with the view
  pitched up 30..36 (no trace hit), so the interactor's focus is not the Visibility trace.
- gen_c244.py now gates the site look on `focus=SatchelSiteCAC_`. The origin look (no bounds) of this run voided on
  the gate the same way, so either look form works once the gate is right.
- The "focus none" reading of c244b207a cmd 77 above was one look; the earlier conclusion that the site never takes
  focus is wrong.

## b208 (c244b208a, build bbefc4ad2982ab49): the satchel plants work; LZ_SO1 stalls at Rubble C_2

- New in b208: `goto actor ... live` skips a taken pickup (AADPickupBase::bEnabled 0, `c64_find_actor`), so later
  blocks walk to the nearest untaken BP_SatchelItemPickup_C instead of the consumed C_0.
- The site gate `focus=SatchelSiteCAC_` passed 3 times (aim hit BP_SatchelBuilding_Flakbug_C_0, C_1, C_2), each
  followed by E 3200: Park_SO1_A, B and C complete on the host (card049 sent), card133 HoldDone rows: host 6,
  client 3. Completes this run: Park_Marker, Park_SO1_A..C, Park_SO2_A..E (9; b207b: 5).
- 135 digest rounds, all mismatches 0; 0 EXCEPTION/FATAL either side; no crash dumps.
- Stall: LZ_SO1 (kind other, actor[0] BP_SatchelBuilding_Rubble_C_2 at (23446, -85013, 227)) held the next 39 blocks.
  The pickup goto resolved to BP_SatchelItemPickup_C_1 at (27234, -99508, 216), 15688 uu away, and failed every
  time "stuck on the leg past the navmesh path's end"; with no satchel the site look voided (aim hit the Rubble
  building at 93 uu, focus none). Open: where the Rubble satchel comes from (no untaken pickup near LZ_SO1 is
  resolved; the step-1 table names presence C_255/C_256 and encounter EM_13 for LZ_SO1, and the Rubble building
  as bound with no objective call at 24379).

## b208 run 3 (c244b208b, same build): same result, LZ_SO1 stall confirmed

- 3722 s, rc 0/0, 0 EXCEPTION/FATAL, no dumps, 141 digest rounds all mismatches 0. The same 9 completes; `objective
  now` read current LZ_SO1 in 74 rows (37 blocks).
- Every LZ_SO1 block: the pickup goto resolved to BP_SatchelItemPickup_C_1 (27234, -99508, 216) and failed "stuck on
  the leg past the navmesh path's end" (37 of 37); the site goto resolved to
  SatchelSiteCAC_BP_SatchelPlacementSite_C_CAT_4 at (23588, -85317, 201), 336 uu from the Rubble building, and failed
  the same way (37 of 37); the site look voided on the Rubble mesh at 93..109 uu, focus none.
- Next step for the stall (not chased in b208): the client carries no satchel at CAT_4. Either warp the client to the
  pickup (`host warp client BP_SatchelItemPickup_C_1 80`) instead of walking 15.7 km off the navmesh, or find the
  LZ_SO1 satchel source (presence C_255/C_256, encounter EM_13) statically first.

## b209 static trace: LZ_SO1 (senior b209 item 2)

- Trigger: FED_C4M1_Scripting BndEvt BP_SatchelBuilding_Rubble_C_2 BuildingDestroyed -> ubergraph 24379 -> goto
  19735 (Add LZ_SO2) -> 19749 CompleteSub(LZ_O1, LZ_SO1). LZ_SO1 is the Rubble satchel plant, nothing else.
  Presence C_255 (19570) only closes BP_Door_Sliding_Large_C_0; presence C_256 (19626) triggers BP_AssassinSpline_C_1;
  19611 is Add Bridge_SO4. The step-1 table row ("presence + hold", EM_13) was wrong for LZ_SO1.
- Satchel source: no level script spawns satchels (the only satchel names in the listing are the four buildings);
  the BP_SatchelItemPickup_C are placed. c244b208b: the client already held 2 satchels at LZ_SO1 (card073 count 3 to
  2 after the last Flakbug plant at 630041787), so the pickup goto to C_1 (15 km off, "15296 uu past the navmesh
  end") was never needed.
- Site: the goto to SatchelSiteCAC_BP_SatchelPlacementSite_C_CAT_4 (23588, -85317, 201) ends at the navmesh end
  (23276, -84646), "goal 750 uu past the navmesh end: the goal is off the navmesh". The Rubble C_2 root (23446,
  -85013) lies on the line between that spot and the site, so the look hit the Rubble at 93 uu (aim range 250),
  focus none.
- gen_c244.py b209: (the held-skip of the pickup was reverted after c244b209a, see below; every block picks up); a
  failed site goto warps the client 200 uu in front of the site (`front`, its forward axis); a look ending focus none
  warps it 200 uu behind and looks again. BLOCKS 46 (2486 steps) with these and the card 485 shot steps.

## b209 results (dll 7158152216d78105)
- c244b209a (cut by the junior at 541 s; rc 0/0, 0 EXCEPTION/FATAL, no dumps, 17 digests all mismatches 0, 9
  completes: Park_SO2_A..E, Park_SO1_A..C, Park_Marker). The held-skip reached LZ_SO1 with 0 satchels (count 1 to 0 at
  the Park_SO1_A plant): the b208 spares came from repeat pickups. Reverted: every block picks up.
  LZ_SO1: the front warp landed the client at (23406.3, -85401.1, 301), 224 uu from CAT_4; the look hit CAT_4 at
  239 uu, focus SatchelSiteCAC_..._CAT_4; card020 Interact fired on the site, no satchel held, no plant.
- c244b209b (cut at 476 s; rc 0/0, 0 EXCEPTION/FATAL, no dumps, 17 client digests mismatches 0; same 9 completes).
  Satchel counts went 0 to 1 to 0 each Park block (one pickup, one plant); the client reached LZ_SO1 with 0 again.
  The pickup goto at the LZ fails "stuck on the leg past the navmesh path's end" (the nearest untaken pickup is C_1,
  (27234, -99508), 15 km off the LZ); front warps (cmd 170, 188) and behind warps (cmd 172) give focus on CAT_4, the
  look hits Rubble C_2 or an ArcherBug otherwise. LZ_SO1 stays open, 0 of 1.
- Proposal (not built, run budget spent): after a failed pickup goto, warp the client to the pickup
  (`when client last "cmd: \d+ goto actor BP_SatchelItemPickup_C 100 live -> " "failed" host warp client
  BP_SatchelItemPickup_C 80`), then the pickup look and E; the site goto then fails and the front warp brings it back.
  Caveat: warp has no "live" filter and may pick a taken pickup; a `live` option on warp would fix that.

## b210 results (dll 414fa857c0212604)
- Built: `warp ... live` (parse_warp strips `live`; c75_warp passes it to c64_find_actor, the same bEnabled +0x2A8 and
  dead test as goto). gen_c244: after the pickup goto, `when client last "cmd: \d+ goto actor BP_SatchelItemPickup_C
  100 live -> " "failed" host warp client BP_SatchelItemPickup_C 80 live`. BLOCKS 45, 2479 steps.
- C_1 nearest live pickup from the LZ: confirmed at runtime, not statically: in c244b209b the live goto from the LZ
  resolved BP_SatchelItemPickup_C_1 (27234, -99508, 216) twice, and the warp uses the same c64_find_actor.
- c244b210a (cut at 505 s, drive rc 143 by kill; host/client rc 0, 0 EXCEPTION/FATAL, no dumps): 17 client digests all
  mismatches 0; 9 completes (Park_SO2_A..E, Park_SO1_A..C, Park_Marker), same as b209b.
- The pickup warp fired both times (cmds 192 and 211) and landed wrong both times: b16_landing put the client at the
  navmesh point (27163, -99471, -842), 961 uu from C_1 and about 1058 uu under it. The look then hit
  StaticMeshActor_6380 above (pitch 71.4, 750 uu) and voided, so no E, no satchel. Cut after the second identical
  failure.
- Cause: b16_landing projects the actor onto navmesh with half height kC64ProjectHalfZ 1200, so a pickup that sits
  on a platform with no navmesh (the same reason the goto stops "past the navmesh path's end") projects to the floor
  far below, and the 50 uu path-end test passes against that floor point.
- Proposed fix: in b16_landing, return false (the old landing: dist uu short, kC75WarpAbove 100 over the actor's z)
  when the actor's navmesh point is more than a set height below it (for example 300 uu), with a log row. To check
  first: the old landing at C_1 drops the pawn onto the platform (80 uu off, z 316) rather than off its edge.

## b211 (24df8971ead8486b): fix (a)

- Static check (from c244b210a logs, not geometry): the old landing from the LZ side is about (27210, -99432, 316).
  That is inside C_1's bounds footprint (centre (27234, -99508, 216), extent 150 x 205 x 205, so bottom about z 11).
  A structure surface sits at about z 35 nearby (StaticMeshActor_6380 hit at 27361, -99341, 35). So the old landing
  drops the pawn onto C_1's support platform, not off an edge.
- A height threshold alone is wrong. The first b211 build (f133df84f5b1fbef, c448 gate only) used "actor over 800 uu
  above its navmesh point: old landing". The logs show C4M2's hanging egg sacs (c245b192c, c245b194a) are 850-1070 uu
  over their navmesh landings. Their old landing is in the air, so that rule would have broken card 245.
- Fix in `b16_landing` (`testrun.cpp`): an actor more than `kB16AboveMeshUu` (300) over its navmesh point gets a down
  trace (kEccWorldStatic) from the old landing to `kB16SupportUu` (300) under the actor's z. A surface found takes the
  old landing; no surface, or a start inside geometry, keeps the navmesh landing. The row is `warp: the actor's
  navmesh point (...) is N uu under it; surface under the old landing found|none|inside geometry (z Z)`. It is a class
  rule, with no coordinate table. `kEccWorldStatic` moved from pose_c132.cpp to ue/actor.h.
- Prediction for c244b211a at LZ_SO1: one `surface ... found ...: off the navmesh, on a platform; old landing` row for
  BP_SatchelItemPickup_C, the look hits C_1, E picks it up, the satchel count goes 0 to 1, then the site warp, the
  plant, and LZ_SO1 completes.

### c244b211a result (24df8971ead8486b, cut at 760 s on the LZ_SO2 stall)

- Fix (a) proven: `cmd: 234 warp: the actor's navmesh point (27234, -99508, -840) is 1056 uu under it; surface under
  the old landing found (z 105): off the navmesh, on a platform; old landing`, then `warp client
  BP_SatchelItemPickup_C 80 live -> done`, then native CompleteObjective LZ_O1/LZ_SO1 at 637695239.
- Completes in order: Park_SO2 A-E (5/5), Park_SO1 C, B, A (3/3), Park_Marker, Park_O1, LZ_SO1. 13 native completes.
- EXCEPTION/FATAL 0 on both logs; client round 22 mismatches 0; no crash dumps; drive rc 143 (cut).
- New stall, LZ_SO2 (shown twice: cmd 245 and 259 objective rounds, waits 250-258 all failed): both players stood
  256/271 uu from ActorReference_C_12; `objective now` named the nearest presence BP_PlayerPresence_C_3, which fired
  with 2 of 2 inside (card227 hold released, original ran) and then disabled itself; LZ_SO2 stayed open.
- Static (FED_C4M1_Scripting listing): presence C_3's bound event goes to ubergraph 19851, which only calls
  BP_AssassinSpline_C_0.Trigger(). LZ_SO2 closes only through presence C_257: its bound event goes to 19846 ->
  goto 1150 -> CompleteObjective(LZ_O1) at 1261 (then disables C_8 and C_257). No script line enables C_257 (only
  the disable at 1332), so it starts enabled. No run log ever named C_257 (not in any card245/card227 row), so no
  player has entered it; its location is not known yet (needs the umap export's transform). The step-1 table row
  "encounter EM_11" was a linear-read guess: 19934 is a switch on another bound event's index, not LZ_SO2.
- Not fixed in b211. Next: read C_257's transform from the umap (or log every presence's location at map load on
  the host), then have `objective now` pick the presence whose bound event reaches the objective's Complete call
  instead of the nearest one (class fix in the objective tool: presence-to-objective binding from the script).

### c244b212a result (b212 a8562ff63c709122, client crash at 1000 s)

- C_257 location logged once at map load: `card244: host presence BP_PlayerPresence_C BP_PlayerPresence_C_257 at
  18350 -74640 960: enabled 1, once enter 0`.
- LZ_SO2 not reached. Completes: Park_SO2 A-E, Park_SO1 C (639520606), B (639573341). Park_SO1_A stayed open from
  ~639600000 to the crash; LZ_O1/LZ_SO1 were added at 639318104 (early, as in b211) but LZ_SO1 never completed.
- The b212 presence fallback fired on the wrong step: the open step was Park_SO1_A, whose marker actor is
  BP_SatchelBuilding_Flakbug_C_0 (a satchel site, not a presence step). Rows `c284: marker step stands at presence
  try 1/2/3 of 3` cycled presences 19511-34583 uu from it; `objective go all -> done` 17 times; Park_SO1_A stayed
  open. In b211 Park_SO1_A completed (637647125) through the satchel path. So the fallback, or the gen_c244 block
  change around it, displaced the satchel plant for a satchel-site marker. Not traced yet: the fallback must skip
  steps whose marker actor is a satchel/destructible site (kind check in c284_marker_next_presence).
- Client crash at 1000 s: card 486 (pre-existing c269_swap_back path, not b212 code).

### b213 changes (57aacb585ebdd2f1)

- The c284 presence fallback (`objective go` with nth>0) now runs only for a bare location: kind Marker, or kind Other
  whose actor is an ActorReference. In c244b212a it ran for Park_SO1_A (kind Other, actor
  BP_SatchelBuilding_Flakbug_C_0), so repeated `go` moved the players to far presences and the satchel was never
  planted. The gen_c244 change itself did not move the plant.
- Card 486 crash fix: see that card.

### c244b213a result (b213 57aacb585ebdd2f1)
- 17 unique completes (b212: 7). New: Park_SO1_A (satchel planted), Park_Marker, LZ_SO1, LZ_O1, Bridge_SO2/SO3/SO4/O1,
  Stadium_SO1_C, Stadium_SO1_D.
- Stuck: Stadium_SO1_B (use). Stand picks BP_PanelSwitch_WithoutScreen_C; client look hits Actor_39 at 2761 uu
  (6857 -27481 -1111), focus none; every block voided. drive.py killed at ~1515 s (rc 143) after ~10 identical blocks.
- 0 EXCEPTION/FATAL, no dumps, 0 card486 rows. PlayerPawn mismatches gone (card323 cull skip).
- Digest: 31 rounds 0 mismatches, 22 rounds 8 mismatches: destructible dead flag, card 487.

## b214 (c244b214a, 1947 s, b214 scenario 41 blocks)
- Item 3 (use look voids on another actor): Stadium_SO1_B first look hit Actor_39, the front warp look hit Landscape,
  the behind warp look took focus BP_PanelSwitch_WithoutScreen_C_1; E completed it (client replayed
  CompleteSubObjective(Stadium_SO1, Stadium_SO1_B) at +961 s). Stadium_SO1_C, SO1_D and Bridge_Marker completed.
- 21 unique client-replayed completes. 0 EXCEPTION/FATAL, no dumps, 0 digest mismatches in 68 rounds.
- Drive rc 3 (VOID): 28 void lines, mostly use looks at lines 1337..1757 voiding on Actor_2 or a trooper.
- c244b214b (2364 s): 20 unique completes, 0 mismatches in 82 rounds. Stadium_SO1_A stuck both runs (14 blocks each):
  the use look voids on Actor_2/Actor_39 and the front/behind fallback did not get focus there. Mission end not
  reached; the end map waits idled 480 s (b215 gen: 90/60 s). After Stadium_SO2 "current none" blocks idle 31 s
  each: Stadium_Marker completed only at the digest clear (b215 gen: the none branch clears first).
