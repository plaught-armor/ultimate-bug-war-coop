# Card 126: Autoplay: both test players play a mission on their own, like a human

Status: in progress, partial (2026-11-07). Phase 1 runs c126a-h: autoplay completes Assault_SO0 and SO1; SO2 is open. No play DLL change (play build stays 4b0362a16c4318ad). Next session: see the senior note at the end.

Read:

- card 124 Report (parallel steps, client navmesh paths, mouse-axis steering, stuck recoveries, `wait goto`);
- card 066 Report (`objectives` command, the objective list), card 095 Report (`objective complete`, doors);
- card 067 Report (`ui continue`), card 121 Report (`ui click`); memory rule: movie cutscenes advance on Enter;
- card 076 and card 125 Reports (fire, mech and turret use).

Repo root: `/path/to/ultimate-bug-war-coop`. DLL base: card 125's.

## Why

User (2026-11-07): "We should test drive the entire game. Properly." Card 127 plays the maps that cover the game in co-op (user: not every map). A
scenario per map written step by step (warps, `objective complete`, hand-placed gotos) is not proper play, and it
would not find what a player finds. So the test players need to play a mission themselves first: follow the
objective, fight the bugs, use what the mission asks for, through the card 124 human inputs only.

## Do

1. **Measure (static).** Where a player learns where to go: the active objective's world location (objective marker
   or waypoint actor, the HUD marker's target), and what each objective type asks for (go to, kill all, defend,
   interact, satchel, pickup, board the dropship). Give the table of types with how the game marks each done.
2. **Autoplay mode in the game (test runner only, `UBWCOOP_CMD=1`).** A `<role> autoplay on|off` command. While on,
   each tick the role:
   - **Moves** to the active objective's location with the card 124 walk (navmesh path, recoveries), sprinting on long
     legs, and stays near the other player when the objective is shared.
   - **Fights:** picks the nearest visible hostile in range, turns to it with the card 124 smooth mouse-axis turn, and
     holds fire while the aim is on target. It reloads with the reload key, throws a grenade at a group, and uses
     tactical support when charged (the game's own keys). It aims like a player: a turn rate cap, a short reaction
     delay, and some aim error that shrinks while tracking. It never reads or writes enemy health, and it never warps.
   - **Does the objective:** presses the interact key at switches, levers, turrets and pickups that the objective
     names, places satchels, boards the dropship at the mission end, and takes ammo, armour and health when low.
   - **Handles the game:** `ui continue` on briefs, Enter on movie cutscenes, respawn or revive when downed (card 043
     revive of the other player when they are down).
   - Logs one line per decision change (`autoplay: <role> objective <id> -> go to (x, y, z)`, `fight <enemy>`,
     `interact <actor>`, `stuck at ...`, `downed`) and a progress line every 10 s.
   - When the objective cannot be done by play (no location, no route, a blocker), it logs `autoplay: <role> blocked:
     <why>` once and keeps fighting. It never completes the objective by command.
3. **Driver.** `<role> wait autoplay map=<next map>|objective=<id> [timeout=S]` and a progress watchdog: no objective
   progress for 180 s fails the step with the last blocked line.
4. **Proof, one run.** FED_C1M1, both players in, both on autoplay from the start, FAST=1 off (the dropship ride is
   part of the game), cutscenes on, muted, windowed 640x360. The mission plays to the end on its own and the host
   travels to FED_C2M1; the client follows (card 045) and arrives.
   - No `objective complete`, no warp, no hand-placed goto in the scenario.
   - Per objective: the time it took and who did it. Every `blocked` line with its cause.
   - rc 0, ERROR/FATAL 0, no CRASH line, no new dump on either side.

Stop when that run plays FED_C1M1 through on autoplay; card 127 plays the coverage maps.

## Done when

The Report has the objective type table, the autoplay design (move, fight, objective, game handling) with the aim
numbers chosen, the driver syntax, the proof run's objective timeline and blocked lines, the run length, rc,
ERROR/FATAL, CRASH lines, dumps and the DLL hash (`sha256sum | cut -c1-16`) with the play DLL saved as
`build/dwmapi-play-<hash>.dll`. Ends with "Report complete."

## Prep (card 129)

Step 1, static only (no runs). Sources: the reflected property tables in the exe (`NewProp_*` FPropertyParams read
from the PE, offset = the high 16 bits of the last word), the existing objective code (`hooks_internal.h` kObjective*,
`objectives.cpp`), and the card 049 / card 070 lines in the FED_C1M1 host logs already on disk (`logs/*-host.log`).

### Where a player learns where to go

There is no separate waypoint actor and no "type" field. The level script (`FED_C1M1_Scripting_C`) calls the
objective library (`AddObjective`, `AddSubObjective`, `AddCountSubObjective`, ...) with an `ActorsToMark` array. The
objective keeps that array. Each objective init (0x051513E0 / 0x05151520 / 0x05151680) then spawns one
`ASTLevelObjectiveMarker` per element with `CreateASTLevelObjectiveMarker`. The HUD marker and the minimap marker
belong to that marker actor. So **the go-to location is the root location of `ObjectiveActors[i]`**, and the marker
only draws it.

| Object | Field | Offset | Note |
|---|---|---|---|
| world | objective subsystem | `g_c49_get_subsystem(world)` | `USTLevelObjectiveSubsystem` |
| USTLevelObjectiveSubsystem | LevelObjectivesMap | 0x60 | TMap<FName, USTLevelObjective*>, element 0x18 bytes, value at +0x8 (kSubsystemObjectives) |
| | OnLevelObjectiveAdded / CalledOut / Complete | 0x30 / 0x40 / 0x50 | multicast delegates |
| | MarkerParent | 0xB0 | the markers' parent object |
| USTLevelObjective | OnUpdated / OnCompleted / OnCallout | 0x28 / 0x38 / 0x48 | multicast delegates |
| | LevelObjectiveID | 0x58 | FName (kObjectiveId) |
| | DataID | 0x60 | FName |
| | bShowMarkers | 0x68 | bool: the marker is shown (kObjectiveMarker) |
| | complete | 0x69 | bool (IsComplete, kObjectiveComplete) |
| | bShowInObjectiveList | 0x6A | bool (kObjectiveInList) |
| | ParentObjective | 0x70 | USTLevelObjective* (null on a top-level objective) |
| | SubObjectives | 0x78 | TArray<USTLevelObjective*> |
| | **ObjectiveActors** | **0x88** | **TArray<AActor*>: the places to go and the things to use** |
| USTLevelObjectiveCount | TargetObjectiveCount / CurrentObjectiveCount | 0x98 / 0x9C | int32 |
| | bUseSubobjectivesCount | bool | counts its sub-objectives instead of increments |
| ASTLevelObjectiveMarker | LevelObjective | 0x2B0 | back-pointer to the objective |
| | MarkerWidgetInstance / LevelObjectiveMarkerWidgetInstance | 0x2C0 / 0x2C8 | HUD widgets |
| | MinimapMarker | 0x2D0 | |
| | bIsVisible | bool | |

**Active objective, for autoplay:** a sub-objective (ParentObjective != null) with complete = 0 and bShowMarkers = 1.
The level script turns the marker on when a step becomes current (`SetObjectiveVisibility(id) bools 11`) and off
when it is done (`bools 00`). Several can be active at once; pick the nearest. The target is
`ObjectiveActors[0]`'s location (every FED_C1M1 sub-objective has exactly one actor). A top-level objective
(`Assault_O1`, ...) has no actors. It is complete once its sub-objectives are done. Not checked at runtime: that the
marker sits at the actor's root location (and not at an offset). Read it once on the first autoplay run.

### FED_C1M1 objectives (from host logs, in mission order)

| Objective | Library call | Actor (`ObjectiveActors[0]`) | What it asks | How the game marks it done |
|---|---|---|---|---|
| Assault_O1 | AddObjective (bools 1101) | none | parent | `CompleteObjective` once its steps are done |
| Assault_SO0, SO1, SO2, SO3 | AddSubObjective | BP_PlayerPresence_C_2 / _2 / _6 / _9 | **go to**: walk into the volume | `ASTPlayerPresence::OnBeginOverlap` → the level script calls `CompleteSubObjective` |
| Assault_SO4 | AddSubObjective | BP_PanelSwitch_WithoutScreen_C_0 | **interact**: E at the panel | the switch's use event → `CompleteSubObjective` |
| ShootingRange_O1 | AddObjective | none | parent | `CompleteObjective` |
| ShootingRange_SO0 | AddSubObjective | BP_PlayerPresence_C_24 | go to | presence overlap |
| ShootingRange_SO1 | AddSubObjective | BP_WeaponPickup_MoritaMk1_C_0 | **pickup**: take the rifle | pickup → `CompleteSubObjective` |
| ShootingRange_SO2 | AddSubObjective | BP_PlayerPresence_C_17 | go to (the firing line) | presence overlap (with `CalloutObjective` first) |
| ShootingRange_SO3 | **AddCountSubObjective, count 10** | ActorReference_C_3 (a marker point at the targets) | **shoot 10 targets** | `IncrementObjectiveCount` per target hit, done at CurrentObjectiveCount (0x9C) = TargetObjectiveCount (0x98) |
| TacSupport_01 | AddObjective | none | parent | `CompleteObjective` |
| TacSupport_SO0 | AddSubObjective | BP_PlayerPresence_C_1 | go to | presence overlap |
| TacSupport_SO2 | AddSubObjective | BP_WeaponPickup_MoritaMk1_C_1 | pickup | pickup |
| TacSupport_SO3 | AddSubObjective | BP_EncounterManager_C_1 | **kill all**: the encounter's waves | `ASTEncounterManagerBase::OnWaveEnd` / last NPC killed → `CompleteSubObjective` |
| TacSupport_SO4, SO41, SO5, SO6, SO7 | AddSubObjective | ActorReference_C_0 / _0 / _5 / _1 / _1 | **tactical support / call-in at a point** (marker point only, each called out) | the level script completes each after its call-in step; the exact trigger (use of support, or a presence volume near the point) is not in the logs: read `CalloutObjective` → `CompleteSubObjective` timing on the first autoplay run |
| Traitor_O1 | AddObjective (bools 1100: no marker, in list) | none | story step, no place | completed by the level script (cutscene/dialogue) |
| CaptureTheFlag_O1 | AddObjective | none | parent | `CompleteObjective` |
| CaptureTheFlag_SO0 | AddSubObjective | BP_PlayerPresence_C_7 | go to | presence overlap |
| CaptureTheFlag_SO1 | (from a save: no Add line in the logs) | unknown | unknown | `CompleteSubObjective` just before SO2 is added |
| CaptureTheFlag_SO2 | AddSubObjective (bools 01110) | BP_SatchelBuilding_CTF_C_0 | **satchel**: place a satchel on the building | `ASTSatchelBuildingBase::OnSatchelExploded` → `CompleteSubObjective` |
| CaptureTheFlag_SO3 | AddSubObjective (bools 01110) | not logged (Adds came before card 070's actor line) | then the mission end (board the dropship) | `CompleteObjective(CaptureTheFlag_O1)`, then the travel to FED_C2M1 |

**Kinds, by the actor's class (the dispatch key for autoplay):**

| Actor class | Kind | Autoplay action |
|---|---|---|
| ASTPlayerPresence (BP_PlayerPresence_C) | go to | walk into it (card 124 walk); done by overlap |
| BP_PanelSwitch*, BP_Switch_C | interact | walk up, look at it, press E |
| BP_WeaponPickup_*, BP_SatchelItemPickup_C | pickup | walk over it / E |
| ASTEncounterManagerBase (BP_EncounterManager_C) | kill all | fight near it until the objective completes |
| ASTSatchelBuildingBase (BP_SatchelBuilding_*) | satchel | walk up, place satchel (the game's key), back off |
| BP_DestructibleSpawner_Bugnest_* | destroy | shoot or grenade it (other maps: Park_SO2) |
| ActorReference_C | marker point | go to it; with a count objective, shoot at the targets near it |
| none (top-level, Traitor_O1) | story | no action; wait for the script |

"Board the dropship" is not an objective with an actor in the logs I have: the mission end in FED_C1M1 is a script
step after `CaptureTheFlag_O1` completes. It needs a look on the first autoplay run.

## Report (phase 1)

Status: **not finished**. Autoplay does SO0 and SO1 on its own; it does not finish SO2. Five runs (c126a to c126e) on
FED_C1M1, FAST=1, host only. No warp, goto command or `objective complete` in the scenario. Every run: host rc 0,
FATAL/ERROR 0, no crash dumps. The drive fails at the SO2 wait (line 12, 120 s timeout).

### What works

- `host autoplay on|off` (console and scenario), one decision line per choice, a progress line every 4 s, and the time
  per objective at completion.
- Active objective: an objective counts only while its parent is active and not complete. Without the parent filter it
  picked TacSupport/ShootingRange steps (c126b).
- Pick rule: the highest slot whose marker is on wins and stays picked until it completes. The prep said "nearest";
  that was wrong (the hub steps are nearer than the Assault chain).
- Open point 1 is settled: `marker BP_ObjectiveMarkerActor_C (objective Assault_SO2) at 17241 28454 1718, actor root
  17241 28454 1718: offset 0 0 0 (0 uu)`. The marker sits at the actor root, so the go-to target is the root.
- Long navmesh walks with sprint (14k and 26k uu paths, 0 stuck).
- SO1 is completed by a real overlap: `CompleteSubObjective(Assault_O1, Assault_SO1)` at 149747242 (c126e), with the
  host walking in, about 1400 uu from the _2 root.

### Prep corrections

- The marker going off does not end a step. The level script shows and hides SO0/SO1/SO2 by where the player is:
  inside the _2 area it sets SO0 00, TacSupport_SO0 01, ShootingRange_SO0 01 and adds SO1; about 4 s after the player
  leaves it sets SO1 00, SO2 00 and SO0, TacSupport_SO0, ShootingRange_SO0 11 (c126e 149746674 and 149755610;
  c126d the same, every time the host crossed the edge).
- ObjectiveActors[0] is not always the trigger. SO2's actor is BP_PlayerPresence_C_6 (17241 28454 1718), but in c121b
  SO2 completed at 94273949 with the client pawn at (-528.9, 15156.5, 97.1), 33 uu from the _2 root, before anyone
  went near _6 (the warp to _6 is the next command). SO3 was added at the same time.

### Failing line and logged cause, per run

| Run | DLL | Failing line | Logged cause |
|---|---|---|---|
| c126a | (pre-filter) | SO0/SO1 pick flip-flop | nearest-first pick |
| c126b | (pre-filter) | picked TacSupport_SO0 / ShootingRange_SO0 | no active-parent filter |
| c126c | 8db2b7b82b15aada | `autoplay: host blocked: objective Assault_SO2 (go to, BP_PlayerPresence_C): 4 walks did not finish it, 8 uu from the goal` | walked to _6; the goto took the navmesh end on the ground (z ~97) as "reached" under the raised _6 (z 1718). Moot: _6 is not the SO2 trigger |
| c126d | 93a74d2138237d17 | `its marker is off 3.0 s, the script shows Assault_SO0`, repeating every ~4 s | the script hides SO2 when the host leaves _2 and shows it on return; the hidden-switch chased it |
| c126e | 8cb5c00b96823f9e | `autoplay: host objective Assault_SO0: its marker is off 4.5 s, the script shows Assault_SO2`, then a walk to _6 | the host stopped at `-490 15258 525`, 31 uu (2D) from the _2 root (z 69) but 456 uu above it. In c121b SO2 completed with the pawn at z 97. The host stood on raised geometry over the root, outside the inner presence check, so SO2 never completed |

### Cause of the SO2 block

SO2 needs the pawn at the _2 root on the ground floor, not above it in 2D. Two checks let the host count as "there"
while 456 uu up:

1. `c64` goto: `level = |pos.z - goal.z| <= 300 || |pos.z - floor_z| <= 300`, with floor_z = the navmesh path end z.
   The path ended on the upper level, so the floor_z branch passed.
2. Autoplay's "at BP_PlayerPresence_C (31 uu)" is a 2D distance.

Then the hidden-switch (marker off 3 s, another on) and the settle rule moved autoplay to _6, which the script never
completes from.

### Next step (proposed)

- Arrival for a go-to needs |pos.z - root.z| within a pawn height (about 150 uu) as well as the 2D radius. If the
  navmesh end is on another level, log it and walk the path that ends nearest the root in 3D.
- SO2's trigger: confirm on the next run that the pawn at ground level at the _2 root completes SO2 (c121b says yes).
  If it does, the go-to target for an objective the script adds inside a presence area while the player is in it is
  that area's root, not ObjectiveActors[0]. That needs one more rule, not a per-objective table.
- One run, same scenario, after the z fix.

DLLs: 8db2b7b82b15aada (c126c), 93a74d2138237d17 (c126d), 8cb5c00b96823f9e (c126e, current build). None is a play DLL;
the play DLL stays 4b0362a16c4318ad. Logs: logs/c126c-host.log, logs/c126d-host.log, logs/c126e-host.log,
logs/c126e-run.out.

### Follow-up: navmesh projection of the goal (c126f, c126g)

Change: the go-to goal is the actor's root projected onto the navmesh (`K2_ProjectPointToNavigation`, main nav data,
±150 uu in z), and arrival needs the pawn within 150 uu in z of that point as well as the 2D radius. If no point
projects, autoplay logs it and blocks the objective; it does not walk on. Arrival lines now print the z offset.

- c126f (019055a3f5ac308c): `autoplay: host blocked: objective Assault_SO0 (go to, BP_PlayerPresence_C): no navmesh
  point within 150 uu of the root (-528, 15189, 69)`. The call works (no WARN, MainNavData adopted); there is no
  navmesh within 150 uu of the _2 root. I stopped the run there.
- c126g (b94c806c67696973): the horizontal search is widened in steps (150, 400, 800 uu), with z still ±150.
  `root (-528, 15189, 69) projects to the navmesh at (-960, 15189, 5) within 800 uu (x, y): 432 uu (2D), z -64`.
  The host walked there on the ground floor (z 97, the same height as the c121b client) and stopped
  `(6 uu, z +92)` from the projected point. The script saw it enter the area (SO0 00, SO1 added at 150454485), but
  **SO1 never completed**: `blocked: objective Assault_SO1 (go to, BP_PlayerPresence_C): 4 walks did not finish it, 6 uu
  (2D) and 92 in z from the goal`. I stopped the run there. rc not collected (stopped); FATAL/ERROR 0, 1 WARN
  (autodropship unpause, test setup), no dumps.

What this shows:

- Around the _2 root there is no navmesh for at least 400 uu on the ground floor, and the upper level is 456 uu up.
- SO1 completed in c126e on the upper route, with the host at about (-786, 13784, 261) climbing, about 1400 uu from the
  root. In c126g, the ground route passed (-1231, 13782, 97), 2D-close to that point but 160 uu lower, and then stood
  432 uu from the root; SO1 did not complete. So _2's trigger is not a ground cylinder around the root. It reaches
  the upper level, and its shape and extent are unknown.
- c121b's SO2 completion with the client at the root on the ground (z 97) is the one point known to be inside the
  SO2 trigger. The pawn got there from a warp 300 uu out, off the navmesh.

The SO step failed twice more, so I am reporting and not rerunning. The c126g build regresses SO1 against c126e. Neither
build is a play DLL.

Proposed next: stop guessing the trigger from the root. Log the presence actor's overlap component (class, world
location, box extent or capsule size, rotation) when an objective is picked. Make the walk goal a navmesh point
inside that shape, and arrival "inside the shape" rather than a radius around the root. If no navmesh point lies
inside the shape, walk the last leg straight from the nearest navmesh point (c121b shows the root area is walkable
off the navmesh). One run then settles both SO1 and SO2.

### Follow-up: arrival by the overlap shape (c126h)

Build 53b0719d5c1cf4e6 (saved as build/dwmapi-c126h-53b0719d5c1cf4e6.dll). This was one run, same scenario (FED_C1M1, FAST=1, host only, autodropship). It was stopped by hand at 13:31 once SO2 blocked, so there is no rc line; the game process is gone. FATAL 0, ERROR 0, WARN 1 (the usual `autodropship: 1 PlayerController(s)`), no new crash dumps. Log: logs/c126h-host.log.

What changed (ubw/autoplay.cpp):
- On each presence objective pick, the actor's overlap component is found: its root if it is a box, sphere or capsule component, else the first one it owns among its reflected object properties. The log prints its class, world location, scaled extent and rotation quat once per pick.
- Arrival tests the pawn's capsule (CapsuleRadius and CapsuleHalfHeight from its root) against that shape the way the engine's overlap does. In the shape's local frame, the capsule's core segment must lie within the capsule radius of the box volume, or within the summed radii of the sphere point or capsule segment. The test assumes the shape is upright (yaw only); a pitched or rolled shape is flagged NOT UPRIGHT in the log. With no shape, the old radius and z test still applies.
- The walk goal is the shape center projected onto the navmesh. If the pawn standing on the projected point would overlap the shape, it walks there. If not, it walks to the shape center: c64_plan takes the navmesh path to the nearest point and then a straight last leg (it appends the goal when the path ends more than kC64NavGapUu short of it). Both cases are logged.

Result:

| Objective | Shape logged | Outcome |
|---|---|---|
| SO0, then SO1 (BP_PlayerPresence_C_2) | `<TriggerArea>` BoxComponent at (-528, 15189, 69), extent (1409, 338, 1250), quat (0, 0, -0.707, 0.707): yaw -90 | Projected point (-960, 15189, 5) is outside the box, so the walk goal was the center (navmesh path plus a straight last leg). `CompleteSubObjective(Assault_O1, Assault_SO1)` fired 12 s after the pick. **Passes.** |
| SO2 (BP_PlayerPresence_C_6) | `<TriggerArea>` BoxComponent at (17241, 28454, 1718), extent (375, 375, 375), same yaw | **Blocked at once:** "no navmesh point within 800 uu (x, y) and 150 (z) of the root". No walk was started. **Fails.** |

Why SO1 passed now and failed in c126g: the _2 box is 2818 x 676 uu with a half height of 1250, so it takes in the upper level too (the c126e completion at z 261). It is rotated 90 degrees, so its long side runs along world y. The c126g goal (-960, 15189) sat 432 uu out in world x, but the box only reaches 338 uu that way, so the pawn stood just outside it. Walking toward the center took the pawn inside. The log does not show which floor it entered on: when the wait line printed, the pawn stood 46 uu (2D) from the center and 418 uu above it, which is the upper level.

Why SO2 failed: the projection search still uses the fixed kC126ProjectDz = 150 in z and at most 800 in x and y. The _6 box has a half extent of 375 on every axis, and no navmesh point lies within ±150 z of its center. The query extent should come from the shape (its extent plus the pawn's half height in z) rather than the fixed constants. This is not fixed or tested here, per the one-run rule.

A side note on the log: the "at ... (46 uu, z +418) ... waiting" line was printed for SO0 after SO1 had already completed. The pawn stood inside the box, 418 uu above its center, but SO0 stays open: the level script completes SO1 there, not SO0. The pick moved to SO2 2.5 s later, as designed.

Next step, pending review: size the projection query from the shape's extent, then run the same scenario once.

The play DLL build/dwmapi-play-4b0362a16c4318ad.dll is back in game/StarshipTroopers_UBW/Binaries/Win64/dwmapi.dll (sha256 prefix checked: 4b0362a16c4318ad).

## Senior note for the next session (2026-11-07)

- Approved next fix: size the navmesh projection query from the overlap shape (its extent, plus the pawn's half height in z) instead of the fixed 150 uu.
- Check this before the next run: SO2 may not complete at _6 at all. In c121b, SO2 was added at 94271255 (actors _6) and completed 2.7 s later at 94273949 (caller exe+0x5233805, which then added SO3 at _9). At that time the client pawn was running on the ground (capsule bottom z 2.1), heading about +y near the _2 area, about 22k uu from _6. Resolve exe+0x5233805 with sym.py and read which overlap or event calls it before walking to _6. If SO2 completes from something inside or near the _2 area, the go-to rule for SO2 is wrong, not the projection.
- Per the run discipline, SO2 has now failed in four runs (c126c, c126e, c126g, c126h). The next run happens only after the exe+0x5233805 caller is known.

