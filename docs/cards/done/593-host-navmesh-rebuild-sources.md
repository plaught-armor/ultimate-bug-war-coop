# Card 593: host navmesh tile rebuilds in combat (performance mod family)

Umbrella: card 573 (workstream D, worker threads). Found 2026-11-21 by the senior, from the c573b390b profile.

## Evidence

- In c573b390b host combat, Background Work has 15236 samples. The tile rebuild path card 577 found on the client
  holds about 2800 of them: `dtBuildTileCacheContours` 532, plus the two helpers that are misnamed
  `dtCrowd::getAgent` (1973), which the card 575 pdata remap placed under `dtBuildTileCachePolyMesh*`.
- Card 577 removed this cost on the client only. The host still runs every rebuild, 87 calls in c397b398a. The host
  needs a correct navmesh for its AI, so it cannot skip blindly.
- The cut is lossless only if the sources that dirty the tiles are things AI pathing does not need to avoid, such as
  corpses, gibs, dropped pickups, projectiles or debris. If a source is one of these, its components stop affecting
  navigation (`SetCanEverAffectNavigation(false)` at spawn, or an early-out in the add path for that class), and
  their rebuilds go away. Destructibles that open or close a real path (bug holes, doors, barricades) keep their
  rebuilds.

## Step 1: name the sources (diag, coded 2026-11-21, compiled into ubwperf.dll, no build number yet)

- In `perf_navgen_c577.cpp`, on the host, each `FRecastNavMeshGenerator::RebuildDirtyAreas` call tallies its dirty
  areas by the class of each area's source object. The source path is `OptionalSourceElement` at area +0x38, then
  the element's weak owner at +0x40. Both layouts were read from the exe and are listed in the file head.
- The card 577 row cadence is now calls 1 to 8, then every 16th (it was every 256th). After each host row comes:
  `card593: host dirty areas since last row: A areas in C rebuild calls, K classes, U untallied: <class> xN (max E
  uu, e.g. in <outer>), ...`. The row shows the top 6 classes, then the table restarts.
- The diag is always on. It adds no cost on the client: the client skips before the tally.

## Predictions (one combat run on the host, a bug wave or the C1M1 range)

1. Most dirty areas come from transient classes: bug corpse or gib meshes, physics debris, dropped pickups, or the
   mod's own relayed copies. They are not level geometry.
2. Few areas have no source (`(no source)`). Areas with no source come from AddDirtyArea calls made by
   bounds alone, for example level streaming.
3. If prediction 1 holds, step 2 turns navigation off for those classes at spawn. The proof is a host combat profile
   with the `dtBuildTileCache*` samples near 0, and AI that still paths around walls and holes (card 132 mismatches
   0, and bugs reach the players).
4. If the main sources are real path changers (destructible holes or doors), the rebuild is needed. The card then
   closes with that data, or moves to a cheaper rebuild (tile size or async settings) as its own measured step.

## Step 1 result: c590b434a (b434, FED_C3M1, junior 2026-11-21)

- 1032 host rows: 56703 dirty areas in 16384 rebuild calls, untallied 0 everywhere.
- Leading class by rows: `BoxComponent` in `BP_MountableTurret_Gunner_C` leads 780 rows (max extent 1133 uu). Next
  are `CapsuleComponent` in `BP_SatchelBuilding_Flakbug_C` (204 rows, 800 uu), `StaticMeshComponent` in
  `BP_TentSpawner_HumanGameplay_C` (37 rows), then `NavModifierComponent` in BP_MechDropPlatform_C and
  BP_AI_CannonTurret_C.
- The same batch is rebuilt 3 to 5 times in the same ms: the map has four RecastNavMesh data (Default, Small, Medium,
  Large), and each one rebuilds every dirty area.
- Prediction 1 failed as written. The sources are not corpses or gibs: they are turret and building collision
  components that dirty the navmesh again and again.

## Step 2: why the turret box re-dirties (diag, then the cut)

- A component dirties again when it moves (the transform update on a nav-relevant component) or when it
  re-registers. A gunner turret that rotates every frame with a nav-relevant BoxComponent would explain 780 rows.
- Diag, host only: for the first `BP_MountableTurret_Gunner_C` BoxComponent and `BP_SatchelBuilding_Flakbug_C`
  CapsuleComponent seen in a tally, log once: component name, attach parent, mobility, collision enabled, response
  to ECC_Pawn, `bCanEverAffectNavigation`, `bDynamicObstacle`. Also log a count of how many of its dirty areas came
  in frames where its world location or rotation changed.
- The cut is lossless if the box does not block Pawn (an interaction or trigger volume): turn navigation off for that
  component class at BeginPlay. If it blocks Pawn and moves, keep it. A cheaper cut then is to dirty only when it
  moved more than a threshold, which is lossy and opt-in.
- Proof: card593 rows for the class fall to near 0, host `dtBuildTileCache*` samples fall, and bugs still path
  around the turret.

## Step 2 result: c594b435a (b435, junior 2026-11-21)

- Turret `BoxComponent` (attached to `CollisionCylinder`) and Flakbug `CapsuleComponent` (attached to `Box Component`):
  Mobility 2 (movable), CollisionEnabled 0 (no collision), Pawn response 0, `bCanEverAffectNavigation` 1,
  `bDynamicObstacle` 1. They are dynamic nav obstacles by design: they carve the navmesh so AI paths around them.
- Dirty areas in frames where the component moved: turret 41940 (8 not moved), Flakbug 9672 (27 not moved). Almost
  every rebuild is a move.
- Turning navigation off is not lossless (it removes the carve). The step 1 cut rule does not apply.

## Step 3: decision (senior 2026-11-21): move threshold below navmesh cell size

- Rate limiting (at most once every N frames) is rejected: a fast move leaves the carve behind by up to N frames.
- A move threshold goes in. Keep, per obstacle component, the world transform at its last accepted dirty. A new dirty
  from a move is skipped while the location delta is below a quarter of the navmesh `CellSize` and the rotation
  chord at the component's bounds extent (2 x extent x sin(angle / 2)) is below the same limit. The first dirty, a
  register, an unregister, and any dirty not caused by a move always pass.
- This is bounded, not identical: a sub-cell move can still flip one voxel at a cell edge. The carve can lag the
  component by at most a quarter cell. Default on; `UBWCOOP_C593_THRESHOLD_OFF=1` turns it off for A/B.
- The junior finds the hook statically (where a moved nav-relevant component calls `UpdateComponentInNavOctree` or the
  dirty-area add for an obstacle) and reads `CellSize` from the four RecastNavMesh data (use the smallest).
- Diag in the card593 row: `threshold: passed P, skipped S, delta histogram (in quarter cells) 0, 1, 2, 3, 4+`.
- Prediction (c594 scenario): skipped above 80 % of turret moves (the AI gunner sweeps in small steps), host
  `dtBuildTileCache*` samples fall by more than half, bugs still path around turrets and the Flakbug building, card 132
  mismatches 0.

## Step 3 in b436 (ubwperf `77c1879cadea3d20`), junior 2026-11-21

- Hook: MinHook on the tail `jmp FNavigationSystem::UpdateComponentData` at exe+0x3AEE01D inside
  `USceneComponent::PropagateTransformUpdate` (the only move caller; the other UpdateComponentData callers are
  non-move and stay unhooked). The site bytes are checked at load, and a mismatch gives a WARN and no hook. The
  detour runs on the caller's stack with rcx = the component. Host only: a client session always calls the original.
- Threshold: a quarter of the smallest host navmesh cell size (`CellSize` and every `NavMeshResolutionParams`
  CellSize, logged as `card593: host navmesh ... cell size`), read when the generator first rebuilds.
  - Each move is measured in quarter cells as max(location delta of the bounds origin, rotation chord at radius
    SphereRadius + |origin - pivot|) against the last transform that passed.
  - Under 1 quarter cell, the move is skipped.
  - A new record (the first dirty) always passes. So does a reused address, because the record is keyed by
    component and serial.
- Register and unregister (`FNavigationSystem::OnComponentRegistered` 0x0382B4A0 and `OnComponentUnregistered`
  0x0382B500) forget the record, then call the original. Non-move dirties do not go through this hook, so they
  always pass.
- `UBWCOOP_C593_THRESHOLD_OFF=1` keeps the counts and the histogram but skips nothing.
- Row: `threshold: passed P, skipped S, delta histogram (in quarter cells) 0,1,2,3,4+`, plus a forgets count.
- Run: c594b436a.

### Step 3 result: c594b436a/b (b436, junior 2026-11-21): prediction missed

- Threshold limit 2.0 uu (CellSize 8.0, 5 navmeshes). Passed 47980, skipped 5940 (11 %). Histogram in quarter cells:
  5940, 5923, 82, 1079, 40861. Most moves are real moves, far above a quarter cell.
- The Background Work rebuild samples were 43 + 170 (a) against 43 + 144 (b, same code), so the drop compared to
  c573b390b comes from the window (seat), not from the threshold.
- Senior decision: the threshold saves about 11 % of dirties at a bounded loss, which is not worth it. Remove it in the
  next build. A real cut must make the rebuild cheaper, not rarer: one generator instead of four or five for the same
  dirty batch, if the agent sizes allow it (a static trace of which navmesh data each AI agent uses comes first).

### Step 3 removed in b437 (dwmapi 7e66a453047ecd5f, ubwperf eac25743fab3a1d0, built 2026-11-21, not deployed)

- The threshold code, its three hooks (the PropagateTransformUpdate tail jump and FNavigationSystem
  OnComponentRegistered and OnComponentUnregistered), `UBWCOOP_C593_THRESHOLD_OFF` and the threshold row are gone.
  The two FNavigationSystem symbols are out of tools/symbols.txt (offsets regenerated). Step 1 and step 2 diag rows stay.

## Step 4 (static trace): which navmesh each AI agent uses

- Config (DefaultEngine.ini in pakchunk0-Windows.pak, `[/Script/NavigationSystem.NavigationSystemV1]`):
  `DataGatheringMode=Instant`, every RecastNavMesh class `RuntimeGeneration=DynamicModifiersOnly`. Five
  `SupportedAgents`, each with its own nav data class:

  | Agent | Radius | Height | Step | Nav data |
  |---|---|---|---|---|
  | Default | 75 | 350 | 30 | RecastNavMesh (`bCanBeMainNavData=False`) |
  | Small | 25 | 125 | 20 | RecastNavMesh_Small |
  | Medium | 175 | 350 | 60 | RecastNavMesh_Medium |
  | Large | 275 | 500 | 75 | RecastNavMesh_Large |
  | Gigantic | 660 | 800 | 150 | RecastNavMesh_Gigantic |

- Each dirty batch goes to every nav data in the nav system, so one batch gives one RebuildDirtyAreas call per
  navmesh: 5 in FED_C3M1 (all five actors are placed in the level, card132 inventory rows).
- How a bug picks its navmesh: `ASTNonPlayableCharacterBase::GetNavData` (exe 0x0518E720, disassembled). It takes
  the controller (+0x9C0) and its navigation system, then calls the nav system's virtual GetNavDataForProps (vtable
  +0x350) with the pawn's `GetNavAgentPropertiesRef()`. This is the stock pick: the smallest supported agent that fits
  the pawn's NavAgentProperties. Those come from the capsule (stock `bUpdateNavAgentWithOwnersCollision`), so the
  navmesh is fixed per pawn class by its capsule radius and height. There is no game-side size table.
- AI classes in the c594b436a host log: BP_AI_WarriorBug_C, BP_AI_ChariotBug_C, BP_AI_AphidBug_C,
  BP_AI_TankerBug_C, BP_AI_HopperBug_C, BP_AI_CenturionBug_C, BP_AI_ArcherBug_C. Their capsule sizes are in the
  cooked BP CDOs, which use unversioned properties, so they cannot be read statically with the repo tools.
- Open: the class-to-navmesh map needs one host row per AI pawn class at first possession: capsule radius and half
  height, NavAgentProperties radius and height, and the GetNavData result name. One short run reads it (held:
  runs are not free yet).
- Cut shape once the map is known: keep the rebuild for each navmesh that a live or spawnable class uses. For a navmesh
  no class in the level resolves to (likely Default if no NPC trooper paths, maybe Gigantic), skip its
  RebuildDirtyAreas. To stay lossless, hold that navmesh's dirty areas and flush them the first time a pawn resolves
  to it. One generator per batch is possible only if every AI class in the level lands on the same navmesh.

## Step 4 diag in b437 (dwmapi `a7f6f78392eb5ddc`, ubwperf `158a39271e741780`), not run, not in the lanes

Code: `mod/src/perf/perf_navdata_c593.cpp` (ubwperf, on card 577's switch, host only). The earlier b437 archive
(`7e66a453047ecd5f` / `eac25743fab3a1d0`, threshold removal only) is superseded.

- Per AI class, one row at its first `ASTNonPlayableCharacterBase::PossessedBy` (after the original):
  `card593: host t=... ai class <cls>: capsule radius R half height H; NavAgentProps found radius r height h step s
  preferred names a/b/c; GetNavData <nav>`. The capsule values come from `CapsuleComponent` by reflection, and
  NavAgentProps from `CharacterMovement` by reflection. Preferred nav data prints the FNames at +0x8/+0x10/+0x18 of
  the TSoftClassPtr, which covers both soft pointer layouts. GetNavData is called once per class, and that call is not
  counted.
- Counter: a hook on `UNavigationSystemV1::GetNavDataForProps(const FNavAgentProperties&) const` (RVA 0x04A9E110).
  The non-const and location overloads are one 9-byte ICF thunk at 0x0356F530 (`mov rax,[rcx]; jmp [rax+0x358]`)
  that tail-jumps into this body, so every resolve shows here with its real caller's return address. Results are
  tallied by (nav data, caller RVA) on the host game thread, whose id is taken from the possession hook. Up to 48
  pairs; the rest count as overflow. Calls from other threads, or before the first AI possession, are counted
  separately. A `first resolve to <nav> from caller 0x...` row comes at each new pair. A `nav resolves so far` row
  and one row per nav data (total, then the callers) ride the card577 row cadence, and the table is never reset.

Predictions:
- Every C3M1 bug class resolves to the navmesh whose agent its capsule fits. The rows will show which of
  Default/Small/Medium/Large/Gigantic have zero class users.
- At least one navmesh (likely Gigantic or Small) shows zero resolves in the whole run. Under the cut rule, that
  navmesh is a candidate to hold, with its flush before the first resolve that names it (hook the resolve).

b437b (senior): nav resolve rows print a name stored at the first tally, so a freed navmesh after travel is compared,
never read. b437c (dwmapi `a7f6f78392eb5ddc`, ubwperf `9c36d98a12d46c95`): the same fix for the step 1 dirty area
table, whose `c593_row` read `object_name(c.cls)` up to 16 rebuild calls after the tally. `C593Class` now names its
class at the first tally. A navmesh with zero resolves gets no row; check against the five DefaultEngine.ini names.

### Step 4 rows: c593b437ca/b (b437c, FED_C3M1, junior 2026-11-21)

Logs: `logs/c593b437ca-host.log`, `logs/c593b437cb-host.log` (16 warrior bugs spawned around the host plus the map's AI).

- 11 AI classes possessed (`card593: host ... ai class` rows). Default: M7Mech (r 200, preferred RecastNavMesh), the
  Morita, Shotgun, Medic, RadioTrooper and SquadLeader troopers, TurretTrooper. Small: ChariotBug. Medium: WarriorBug
  (r 175), AphidBug (r 65). Large: CannonTurret (r 210, preferred None).
- Whole-run counter (a): 78619 game-thread resolves, 29 pairs, 0 overflow, 243 off the game thread or before the
  first possession. Medium 55940, Default 15032, Large 4428, Small 3219. Run b: 79178, 34 pairs, 0 overflow, 510 off.
- RecastNavMesh_Gigantic is placed in C3M1 (card132 inventory `n=1 placed=1`) but has no resolve row in either run.
  Cut candidate under the step 4 rule: zero class rows and zero counter users. The other four navmeshes all have users.

## Step 5: hold the Gigantic navmesh rebuilds (senior 2026-11-21, after c593b437ca/cb)

- C3M1: RecastNavMesh_Gigantic has no AI class row and no resolve in either run. The other four navmeshes are in use
  (Medium 55940, Default 15032, Large 4428, Small 3219 resolves), so only Gigantic can be cut. That saves about one
  fifth of the rebuild work, on a worker thread.
- Cut: while a nav data has had no resolve on this map, its generator's RebuildDirtyAreas is skipped. The skip only
  remembers that the navmesh is stale. On the first resolve to that nav data (the card 593 resolve hook, before it
  returns), the whole navmesh is marked dirty once, a WARN `card593: held navmesh <name> first used, full rebuild`
  is logged, and normal rebuilds resume. The state resets per map.
- Loss bound: none while unused. On first use, the async rebuild can briefly lag the first path queries. The WARN shows
  whether this ever happens; if a map logs it, that map's class goes on a keep list instead.
- `UBWCOOP_C593_HOLD_OFF=1` turns the cut off for A/B.
- Proof: a C3M1 run where the card593 row shows Gigantic skips, there is no held-first-use WARN, host Background Work
  `dtBuildTileCache*` samples are about 20 % lower against hold off, and card 132 mismatches are 0.

## b438 hold A/B (C3M1, 2026-11-21)

- Runs: c593b438a (hold on) and c593b438b (UBWCOOP_C593_HOLD_OFF=1). Both scenarios passed. card132 mismatches were 0
  in both rounds of each run.
- Run a: Gigantic was held as unused, with 175 calls skipped (403 areas) and 0 replays. Small, Medium, Large and
  Default were all used. There were 11 `held (card 593)` rows and no card593 WARN.
- Run b: the hold-off switch row appeared and there were 0 `held` rows.
- Host Background Work samples in the 20 s window (tools/perf_frame.py, both runs at 143/143 perf rc):

  | Run | `dtBuildTileCache*` + misnamed `dtCrowd::getAgent` | Background Work samples | Whole-window samples |
  |---|---|---|---|
  | a | 176 (142 + 30 Contours + 4 PolyMeshDetail) | 11441 | 33049 |
  | b | 196 (148 + 48 Contours) | 11194 | 33163 |

- Normalized to window samples, that is 0.53 % against 0.59 %, about 10 % lower (12 % against Background Work).
  The prediction was about 20 %, so it was not met. The counts are small, so noise covers most of the gap.
  dtSharedBoundary::CacheData (34 against 52) is crowd work, not tile builds, and is left out.
- Logs: logs/c593b438{a,b}-host.log, logs/c593b438{a,b}-gt/host-frame400.txt.

## Decision (senior 2026-11-21, after c593b438a/b)

- The hold stays in. It skips Gigantic's rebuilds by count (175 calls, 403 areas, 0 replays), has no first-use WARN, and card 132 mismatches are 0. The skip is deterministic, so the saving is real even though the profile difference (176 against 196 samples, 0.53 % against 0.59 %) is inside noise.
- The 20 % prediction missed. The rebuild cost is on Background Work, not GameThread, and is under 1 % of samples there, so no more work goes into navmesh rebuilds. Card closed.
