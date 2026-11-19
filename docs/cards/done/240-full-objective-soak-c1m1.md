# 240: Full-objective soak, FED_C1M1 (first of the Trooper campaign)

User rule 2026-11-12: "Soak needs to cover all objective completions". The c127/c234 soaks run 3 to 9 minutes
and reach only the first objectives; `c127-c1m1.txt` forces several with `objective complete` and never
completes `CaptureTheFlag_O1` (c238-c1m1 objectives dump: `CaptureTheFlag_O1 complete 0`,
`CaptureTheFlag_SO0 complete 0`). A map counts as soaked only when every objective and sub-objective the map
defines is completed by its real trigger with both players, replayed on the client, through to mission end.

Goal (one): one scenario that plays FED_C1M1 from spawn to the mission-end travel and completes every
objective by its own trigger.

Step 1, static, no run. Appended to this card as a table:
- The full objective list of FED_C1M1 from the game's data (the map's objective definitions, not what a short
  run added), in play order, with parent, counter (N of M) and which are markers only.
- For each: the real trigger (volume entered, kills, interact, timeline end, counter) with the level-script or
  class evidence, and the driver steps that fire it (warp, look, key, wait log). Name any trigger the driver
  cannot fire today and the smallest driver verb that would.
- What follows each completion that the mod syncs (doors, waves, cinematics, travel), so the scenario waits on
  those log lines too.

Step 2: `tools/scenarios/c240-c1m1.txt` (new file; `c127-c1m1.txt` stays). Rules:
- No `objective complete` command. A forced completion does not count.
- After each objective: `host wait log` for the host's Complete row and `client wait log` for its replay, never
  a sleep. Presence-gated triggers (card 227) get both players in the volume.
- Keep the c127 scenario's existing coverage blocks only where they sit on the route; this card is the
  objective chain, not a feature sweep.
- Ends with `host objectives`, `client objectives` and the mission-end travel reaching FED_C2M1 on both sides.

Step 3: run, budget 3 runs. Command:
`systemd-run --user --scope -q -p MemoryMax=12G env SCENARIO=tools/scenarios/c240-c1m1.txt DEPLOY=1 FAST=1 HOST_EXTRA_ENV="UBWCOOP_C132_FX=1 UBWCOOP_C136_LOG=1" CLIENT_EXTRA_ENV="UBWCOOP_C132_FX=1 UBWCOOP_C136_LOG=1" tools/run_pair.sh c240-c1m1 3600`

Files allowed: `tools/scenarios/c240-c1m1.txt`, this card. Driver verbs (`tools/drive.py`,
`mod/src/ubw/testrun.cpp`) only if step 1 names a trigger no verb can fire; say so in a report before
building. No other mod file: a sync fault found on the way is reported with its log lines and becomes its own
card, it is not fixed here. No build while a game runs.

Done when: the final objectives dump shows `complete 1` for M of M objectives on host and client, the lists
match, both sides reach FED_C2M1, 0 crash, every digest round 0 mismatches, frame windows at or over 500
outside load. Report: completed N of M, each fault found with its lines, new WARN classes against c238-c1m1.

Next cards, same shape, one per map: FED_C2M1, C3M1, C3M2, C4M1, C4M2, C5M1, C5M2.

## Step 1 result (junior): FED_C1M1 objective chain

Sources: the user's real play logs, no testrun commands: host `logs/playtest-host-20261107-1416-live.log` (H:line)
and client `logs/win64-old/UBWCoop-menu-20261108-191343.log` (same order, every host Complete row replayed).
Objective actors from the card070 lines of the host log. Host Complete rows are
`card049: host sent CompleteSubObjective(P, S)` / `CompleteObjective(X)`; client replays are
`card049: client t=N replayed ...`.

M = 27 objective entries on the play route: 26 complete with a Complete row, plus Asteroid_O1, which the game
never completes (the mission ends by travel from its presence volume). Report form: "26 of 27 by Complete row,
Asteroid_O1 by travel". DT_LevelObjectiveData_FED_C1M1 has 29 ids; the two outside M are TacSupport_SO1 and Traitor_O1 (see corrections below).

| # | Objective (parent) | Real trigger, evidence | Driver steps | Synced follow-ups to wait on |
|---|---|---|---|---|
| 1 | Assault_SO1 (Assault_O1) | presence BP_PlayerPresence_C_2, H:1837 | both `goto actor BP_PlayerPresence_C_2` | none |
| 2 | Assault_SO2 | presence _6, H:2089 | both goto actor _6 | none |
| 3 | Assault_SO3 | presence _9, H:2127 | both goto actor _9 | none |
| 4 | Assault_SO4 | E on BP_PanelSwitch_WithoutScreen_C_0, H:2424 | goto near, `look actor`, `key E` | panel door |
| 5 | Assault_SO0 (presence _2, marker parent) + Assault_O1 | complete with SO4, H:2430/2432 | none | TacSupport_01 added |
| 6 | TacSupport_SO2 | pick up BP_WeaponPickup_MoritaMk1_C_1 (~13239 13381 204), H:2723 | goto onto it / E | none |
| 7 | TacSupport_SO3 | not a wave (BP_EncounterManager_C_1 logged no card111/112 wave): real play completed it while the host fired the Morita near (11884, 14013), H:3233, and again on the resupply pod, H:3330; c238 completed it on the resupply call | warp 11700 13400 350, `look point 11700 14500 140`, `charge 0 20000 1`, key Four, LMB | resupply pod copy on client |
| 8 | TacSupport_SO4 | completes with SO3 (ActorReference_C_0), H:3334 | none | SO41 added |
| 9 | TacSupport_SO41 | completes by itself ~1 s after SO4 (the pod's BP_HealthPickup_Medium_C DisablePickup), H:3359; c238 the same | none | pickup copy on client |
| 10 | TacSupport_SO5 | E on BP_TacSuppPickup_BombingRun_Tutorial_C_2 (12519 13417 247), H:3447 | c127 bombing block: goto, look, key E | none |
| 11 | TacSupport_SO6 | call the bombing run: key Five + LMB (BP_SupportFlare_Tutorial_C), H:3467 | `look point 12665 14500 -482`, `charge 0 20000 1`, Five, LMB | flare copy on client |
| 12 | TacSupport_SO7 | bombing-run projectiles land (~10 s), H:3514 | wait log | BombingRun projectile copies |
| 13 | TacSupport_SO0 (presence _1) + TacSupport_01 | complete with SO7, H:3520/3522 | none | ShootingRange_O1 added |
| 14 | ShootingRange_SO1 | pick up BP_WeaponPickup_MoritaMk1_C_0, H:3887 | goto onto it / E | none |
| 15 | ShootingRange_SO2 | presence _17, right after SO1, H:3916 | both goto actor _17 | none |
| 16 | ShootingRange_SO3 | 10 hits on BP_KillableBuilding_Base_TargetRangeTarget_C (instances `TargetRangeTarger_C_N`), `IncrementObjectiveCount` H:3956-4283, no CompleteSub row of its own | from ~5830 12908 194: `look actor` each target + LMB; `kill actor` fallback | card097 count n/10 on client |
| 17 | ShootingRange_SO0 (presence _24) + ShootingRange_O1 | complete at count 10, H:4295/4297 | none | CTF door: card161 BP_Door_Sliding_Large_C_0 DoorState 2 -> 1 |
| 18 | CTF_SO1 | pick up BP_WeaponPickup_LaserMorita_C_0, H:4476 | goto onto it / E | SO2 added |
| 19 | CTF_SO2 | presence BP_PlayerPresence_C_8 (-8360 20950 680, script binds 1) at the satchel building BP_SatchelBuilding_CTF_C_0 (the SO2 objective actor, card070 H:4471): card054x completed it 2 ms after test step 2 moved the server copy next to the satchel placement site (-8237 21000), card054x-host:1593-1597. With it the door BP_Door_Sliding_Large_C_1 collision 0 -> 3 and a LaserTroopersRed wave (spawn -9124 4508 105), H:4600-4611 | both `goto actor BP_PlayerPresence_C_8` | door collision row, wave spawn copies on client |
| 20 | CTF_SO3 | E on BP_SatchelItemPickup_CTF_C_0 (-8350 1771 380), H:4889 | goto, look, key E | none |
| 21 | CTF_SO4 | E on BP_SatchelPlacementSite_Flag_C (component SatchelSiteCAC_..._CAT; -8357 21000 380), explosion ~2.5 s later, H:4976 | goto, look, key E | LaserTroopersRed wave, building destroy on client |
| 22 | CTF_SO0 (presence _7, gated: both players) + CaptureTheFlag_O1 | SO0 entered early (card 227 gate); parent rows complete with SO4, H:4982/4984 | both goto actor _7 before SO1 | door/wave per c227 |
| 23 | Asteroid_O1 | added as presence _11 (H:4997), no Complete row; ~25 s later the host Browses to 3D_Frontend?FromLevelComplete (H:5059), then FED_C2M1 (H:5884) | both goto actor _11 | card052 `world FED_C2M1 ... net mode 2` host, client reconnect `net mode 3` |

Triggers no driver verb can fire: none found. goto (walk, so overlaps start), look, key E/Five/LMB, charge
and kill cover every row. Open risks: the CTF_SO2 trigger (row 19) and whether presence _11 is gated.


### Step 1 corrections (junior, after the senior's review)

- TacSupport_SO1: in the data table but never added. `grep -c 'TacSupport_SO1\b'` over logs/*.log (every real play,
  host and client, and every test run) finds 0 AddSubObjective rows; FED_C1M1_Scripting_C adds SO0, SO2-SO7 and SO41
  only. Unused id, not reachable, out of M.
- Traitor_O1: added by FED_C1M1_Scripting_C (AddObjective caller exe+0x5081B44, bools 1100, actors 0) right after a
  player goes Downed: c146-r2-host.log:4018 after `cmd: 3 down host`; c168-r3-host.log:3826 after `cmd: 5 down host`
  with all players down; c70b-host.log:1235 and 2775 after card043 test hits put the host Downed. It is the downed
  branch (a player can reach it by going down), not a completion objective; out of M. The c240 scenario keeps both
  players in god mode so the run never adds it; a Traitor_O1 row in c240 is reported as a down, not a pass.
- Row 7 (TacSupport_SO3) and row 9 (SO41) rewritten above: resupply call, not a wave.
- Row 19 (CTF_SO2) trigger found: presence _8 at the satchel building (card054x). The first run walks both players in.

### Runs (junior, 2026-11-12; budget 3, all used)

| Run | Logs | Result |
|---|---|---|
| r1 | logs/c240-c1m1-r1-{host,client}.log, .drive | Host E on BP_PanelSwitch_WithoutScreen_C_0: interactor target null. Assault_SO4/SO0/O1 failed, so CaptureTheFlag_O1 was never added (CTF needs Assault_O1, TacSupport_01, ShootingRange_O1). Stopped at 15:44. |
| r2 | logs/c240-c1m1-r2-{host,client}.log, .drive | Client pressed from 156 uu aimed at the panel origin (pitch -51): target null twice; Use relay handled with no target. Stopped. |
| r3 | logs/c240-c1m1-r3-{host,client}.log, .drive | Panel fixed (c121r/c121i spot 17332 12821 213, aim 100 uu above origin): SO4 on the first press. 23 of 27 complete. Stopped after the end digest; travel could not happen. |

Run 3 completed, host Complete row and client replay for each: Assault_SO1-SO4, SO0, Assault_O1; TacSupport_SO2-SO7,
SO41, SO0, TacSupport_01; ShootingRange_SO1, SO2, SO3 (count 10/10, 10 client IncrementObjectiveCount replays), SO0,
ShootingRange_O1; CaptureTheFlag_SO1-SO3. 22 Complete rows on each side plus SO3 by count.

Run 3 not completed: CaptureTheFlag_SO4, CTF_SO0, CaptureTheFlag_O1, Asteroid_O1 (not added), travel to FED_C2M1.
- Line 249: presence _7 "fired after hold: 2 of 2" timed out. Client goto stuck on a wall at -3654 9300 250; host log
  "held: 1 of 2". CTF_SO1 was still added and completed (the gate does not block SO1).
- Line 269: door wait regex wrong (client logs `DoorOp state 3 BP_Door_Sliding_Large_C_1: applied`, "DoorState 3 -> 3").
  The door agreed on both sides (3, then 0 at host 159045202 / client 159045234).
- Lines 280/282: `SatchelSiteCAC_BP_SatchelPlacementSite_Flag_C_CAT` is a component name, not an actor: "no such
  actor". Row 21 corrected: the actor is BP_SatchelPlacementSite_Flag_C (n=1, -8357 21000 380). CTF_SO4, SO0 and O1
  failed in cascade.
- Line 300: both gotos to presence _11 (-8631 24011 520) "failed (no path)" from the 300/400 uu warp; no presence _11
  line in the host log. Lines 301/302: AddObjective(Asteroid_O1) timed out. Not yet known whether _11 also needs
  CaptureTheFlag_O1 first (real play: CTF_O1 at H:4984, Asteroid_O1 added at H:4997).

Gates in run 3: 0 crash; 17 client digest rounds, all 0 mismatches (end round 16: 1507 host, 1507 client actors);
host frame windows 583-585 after load (load window 41, first after load 497).

New WARN classes against c238-c1m1:
- host and client card046 warp gap / below KillZ (246 host WARN lines): range warp `5600 14400 -200` was under the
  floor; the host fell to -22026 (KillZ -22000) and the kill floor warp fired. Range hits still counted by the kill
  fallback.
- client `cmd goto gave up (stuck)` / `goto actor BP_PlayerPresence_C_7 ... failed (stuck)`.
- host `goto actor BP_PlayerPresence_C_11 80 -> failed (no path)`.

Fixes in the regenerated tools/scenarios/c240-c1m1.txt (349 lines), for a 4th run if granted:
- satchel site by class name BP_SatchelPlacementSite_Flag_C;
- door wait on the client `DoorOp state 3 BP_Door_Sliding_Large_C_1: applied` line;
- every presence (and _7) ends with a short warp onto the volume (host 40 uu, client 60 uu) after the goto;
- range firing spot 5311 13381 287 (client 5411 13381 287), next to presence _17 on the floor.

### Run 4 pre-launch audit (senior grant 2026-11-12: one run, tag c240-c1m1-r4)

Script check of the regenerated scenario (every `wait log` regex against the r3 logs, the real-play logs and any
logs/*.log; every actor name against r3 `cmd:` resolutions or the r3 inventory), then by hand for warp floors:
- Client Asteroid_O1 regex fixed: the real-play client logs its own `card049: client t=N AddObjective(Asteroid_O1)`
  (no "replayed"); the wait now takes `(replayed )?`.
- Presence _7 "fired after hold: 2 of 2" matches a c227 run line; r3 never reached it.
- Not resolved by any r3 cmd but present as actors in the r3 inventory: BP_PanelSwitch_WithoutScreen_C_0 (client
  look, passed in r3), BP_WeaponPickup_MoritaMk1_C_0 (skipped by `unless` in r3), BP_SatchelPlacementSite_Flag_C
  (class, n=1). The satchel site gets two more aims (60 uu up, bounds) as the panel did.
- Floors: range spot next to the floor at z 97 the r3 players walked from (presence _17); CTF spot -3654 9300 250
  where the r3 client stood; presence _11: the r3 host fell from 620 and landed at (-8627, 23711, 0), so the ground
  near the volume is at z 0. The r3 gotos to _11 found no path (the end shots show the area past the flag tower;
  assumed gated with CTF).
- Presence _11 steps are gated with `when host last "host sent CompleteObjective\(CaptureTheFlag_O1\)"`; no retry.

### Run 4 (c240-c1m1-r4, DLL d561aa4a8628ec3f)

- Ended by the scenario after 1211 s; host rc 0, client rc 0, no crash dumps, FATAL/ERROR 0 on both sides.
- 27 Complete rows on each side (host `sent`, client `replayed`); 46 digest lines on the client, all `mismatches 0`.
- Fixed since r3 and passed: the _7 warp fallback, the door regex, the range point (SO3 10/10, ShootingRange_O1 complete),
  the CTF chain SO1, SO2, SO3.
- One fault: CaptureTheFlag_SO4 (line 320). The satchel placement site is a hold interaction and `host key E` is a
  120 ms tap. r4 host 159837738 `own interact press` on SatchelSiteCAC_BP_SatchelPlacementSite_Flag_C_CAT (aim and
  distance right), sound MS_WPN_SatchelCharge_Prime; 159837858 `own interact release`, MS_WPN_SatchelCharge_Cancel.
  Real play (playtest-host-20261107-1416-live.log): press 153911315, SO4 complete 153913820 (2.5 s; press to release
  2874 ms).
- Cascade from SO4, not separate faults: lines 321-325 (SO4 client, SO0, CTF_O1), the _11 steps skipped by the gate as
  designed, 339/342 (no AddObjective(Asteroid_O1)), 353-355 (no travel).
- Other E interacts checked against real play press-to-release gaps: zipline 75-100, pickups 75-88, crates 66-183,
  CTF satchel pickup 565, unnamed 75-718 ms. Only the placement site (2874 ms) is a hold; every other E step passed in r4.
- Fix for r5 (senior grant): one `host key E 3200 timeout=20` at the site, the tap retries removed.

### Run 5 (c240-c1m1-r5, DLL d561aa4a8628ec3f)

- Ended by the scenario after 1218 s; host rc 0, client rc 0, no crash dumps, FATAL/ERROR 0. 27 Complete rows on each
  side; 46 client digest lines, all `mismatches 0`. Failed lines 145, 313-318, 332, 335, 346-348.
- Fault 1, mod (sync, reported, not fixed): the host's own satchel hold completed and card133 refused it.
  Host 161383313 `card020: host host-own UADInteractorComponent::Interact ... target SatchelSiteCAC_BP_SatchelPlacementSite_Flag_C_CAT`
  (aim 195 uu, `host key E 3200`); 161384556 `WARN card133: host t=161384556 hold completed on
  SatchelSiteCAC_BP_SatchelPlacementSite_Flag_C_CAT (satchel placement site) for server copy BP_PlayerCharacterMI_C:
  not run (the client's HoldDone is the event; card 054 does not start this hold)`; 161386521 release, SatchelCharge_Cancel.
  The client pressed nothing (no interact, hold or card133 line in its log 161382000-161388000). The client's server
  copy stood about 110 uu from the site (cmd 102 warp to (-8370, 20891), presence _8 is beside the site). The
  completion's interactor argument was the client copy's (sprint_side kSideServerCopy; host pawn audit is LocalRole 3
  RemoteRole 1), so detour_c133_completed (hold.cpp:202) returned before g_orig_c133_completed. Real play
  (2026-11-07) predates card133. In play, any host satchel placement with the client standing near the site loses SO4.
  Lines 313-318, 332/335 (no AddObjective(Asteroid_O1)), 346-348 (no travel) are its cascade.
- Fault 2, new in r5 (not in r4): line 145, the client never got a copy of BP_SupportFlare_Tutorial_C. Host spawned it at
  161245637 and destroyed it at 161246664, the same ms as `flare BP_SupportFlare_Tutorial_C LZQueryFinished`, at
  (12655, 14440, -53); no card104 copy was sent. In r4 the flare rested and was copied (client 159708319). The bombing
  run still reached the client (bomber copy 161245768, projectiles from 161256218) and SO6/SO7 completed on both sides.

### Runs 6 and 7 (c240-c1m1-r6, c240-c1m1-r7, DLL 83cf0b9cdcfe0cb0, scenario edits only)

Scenario changes (tools/scenarios/c240-c1m1.txt):
- _6 and _9: a distance warp left the client's start position unsettled (start z 826 and 595 in c248-c1m1), and the
  path query returned 0 points. The client is now warped to a fixed point next to the host's warp spot, then the
  scenario runs `client wait near` before the gotos. _6 uses 17001 28175 1464. _9 uses 17411 19994 1329: in r6, the
  point 17311 20094 1185 still gave 0 points from z 1159, while the host's query from 17311 19994 1329 gave 8 points.
- _7: the client starts at -3454 9450 248, beside the host. From -3654 9300 250 the client had moved 0 uu in 16 s.
- _11: the gotos go to the ground point -8631 24011 97. The volume centre at z 520 is 423 uu above the ground and
  does not project to the navmesh.
- Mission end: the scenario waits for the card018 `WB_TrooperLevelEnd` line on both sides. The host then clicks
  `BndEvt__TrooperLevelEnd_WB_Button_Next` every 3 s, gated on the host's `3D_Frontend ... net mode 2` world line, at
  most 8 times. In r6, a single click 109 ms after the screen opened did nothing. The widget has an `InputEnabled?`
  flag, and nothing is logged when it is set. In r7, the third click (about 9 s after the screen opened, the same
  delay as the real player's press) carried the host to 3D_Frontend.

r7 results:
- The run ended by the scenario after 851 s. Host rc 0, client rc 0, no crash dumps, FATAL/ERROR 0.
- Every presence walk arrived on both sides. The ten `try join` lines report done/done. There are no goto WARNs.
- 25 unique Complete rows on the host, the same set as c248-c1m1, plus ShootingRange_SO3 by count. The final card066
  objectives dumps (27 rows each) are equal on both sides: 26 complete, Asteroid_O1 open (it completes by travel).
  Client digest round 7 reports `actors 1519 host, 1519 client, mismatches 0`.
- Level end, client side: the client opened its own WB_TrooperLevelEnd 19 ms after the host
  (165526558/165526577). It needed no press. When the host Browsed, the client got FailureReceived, Browsed to its
  local 3D_Frontend, and the card045 follow reconnected it after 4799 ms (165540684). The client is then in the
  host's 3D_Frontend (net mode 3).
- Open: the host stayed in 3D_Frontend for 600 s, and FED_C2M1 was never loaded (lines 377-378 FAIL). In real play
  (playtest-host-20261107-1416-live.log), the host Browsed from the same frontend to FED_C2M1 14.7 s after it loaded,
  with no WB_Stack widget and no logged click. The frontend's input is a world widget: the host turns on collision
  for `Widget_Holder_3DFrontend_C_4` at (7258, 5796, -881) at load, in both runs. `ui click` only reaches WB_Stack_C
  widgets, so the driver has no verb for this step.
- New WARN class, client only, in the frontend: 40 `card132 mismatch Widget_Holder_3DFrontend_C ... collision` lines
  and 20 round lines (`mismatches 2`, the holders _1 and _4). The host enables collision on its frontend holders,
  and the client's copies do not follow. c248-c1m1 never reached the frontend. Frame spikes of 5837 and 4168 ms
  happened only at the start-up load.

Run B (c248-c133, tools/scenarios/c133-combined.txt unchanged) is reported under card 248.

## Final result (c240-c1m1-r8, DLL 83cf0b9cdcfe0cb0, scenario edit only)

The scenario runs FED_C1M1 from the dropship to FED_C2M1 on both sides. The client presses nothing at the mission end.

Result:
- The scenario ended the run after 343 s and passed; no line FAILED.
- Host rc 0, client rc 0, FATAL/ERROR 0/0, no crash dumps.
- 25 unique Complete rows. The final card066 dumps (27 rows) are equal on both sides: 26 complete, and Asteroid_O1 is
  open until the travel.
- Asteroid_O1 completes by travel: the host Browsed to FED_C2M1 at 167002031, and the client reached FED_C2M1 net
  mode 3 at 167010567.
- Client digest rounds 1-8 in C1M1: mismatches 0 in every round, actors 1517/1517 at the end.
- Every frame spike over 1 s ends at a map load. The largest are 6520 ms on the host and 4443 ms on the client, both
  at the C1M1 load.

Walk notes (presence volumes, see Runs 6 and 7):
- A distance warp can leave the client's pawn on an unsettled start point, and the path query from there returns 0
  points. The scenario warps the client to a fixed point beside the host's warp spot, then waits on `client wait
  near` before the gotos. The points are:
  - _6: 17001 28175 1464
  - _9: 17411 19994 1329, the host's start z
  - _7: -3454 9450 248
- _11: the volume centre is 423 uu above the ground, so both gotos go to the ground point -8631 24011 97.

Mission-end notes:
1. WB_TrooperLevelEnd opens on both sides about 16 s after AddObjective(Asteroid_O1). Its Next button ignores clicks
   until its `InputEnabled?` flag is set, and that flag logs nothing. In r6, a click 109 ms after the screen opened did
   nothing.
   - The host clicks `BndEvt__TrooperLevelEnd_WB_Button_Next` every 3 s, at most 8 times, until its log shows
     3D_Frontend net mode 2. In r7 and r8 the third click (about 9 s after the screen opened) worked.
   - The client's screen follows with no press. It drops with the host's Browse, loads its local 3D_Frontend, and
     the card045 follow reconnects it after about 4.7 s.
2. With FromLevelComplete, the 3D frontend shows the next mission's brief (WB_MissionBriefVideo_C), which waits for a
   key.
   - `ui continue` fails there with `no WB_Stack_C requested a pause on this map`.
   - `key Enter` (PC InputKey) logs `srv: input window opens on key Enter` but does not reach the brief.
   - `winkey Enter` (card 222, the OS key route) starts the Browse to FED_C2M1 on its first press.
   - The scenario keeps all three stages, gated with `unless` on the Browse line, so only the first one that works
     fires.
   - The client follows by a second card045 drop and reconnect: 8536 ms from the host's Browse to the client's
     FED_C2M1 world line.
3. Not in this card: the client's Widget_Holder_3DFrontend_C copies do not take the host's collision change in the
   frontend (card 250).
