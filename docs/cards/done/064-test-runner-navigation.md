# Card 064: Test runner, part 2: walk to a place and aim at a thing

Status: sent 2026-11-04 22:25 to junior ultimate-bug-war-coop-bb, after card 063 was accepted.

Read the card 063 Report, and the card 059 Addendum (the 150 m walk that keys could not do). Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Runner API from card 063 (as built)

- The state line has the fields `t map role pawn x y z yaw pitch hp weapons equipped objective cmd_seq`. `pawn` is `host-own`, `client-own`, `other` or `none`. Add `goal=` and `dist=` to the end of the same line.
- Scenario steps are `<role> key|axis|shot|quit`, `<role> wait state k=v ... [timeout=S]`, `<role> wait log "<regex>" [timeout=S]` and `sleep <s>`. A command step waits 10 s by default for its `cmd: <seq> ... -> done|failed` line. A `goto` takes much longer than that, so drive.py needs a per-step `timeout=` for commands too, or a longer default for `goto`.
- run_pair passes `--shot-prefix <tag>`.
- **Known limit:** the host's brief movie stays on screen (card 063 Open gap; card 067 will fix it). The world runs behind it, so host-side proof here comes from log and state lines, not host shots.

## Why

Scripted runs can press keys, but they cannot get anywhere. To reach a pickup, a zipline or a range, a test either teleports (`SetActorLocation` in a per-card test flag) or a person walks. A teleport skips the triggers, volumes and level-script events on the way. That is why card059d could not reach the Morita, and why cards 054 and 058 never reached the zipline and satchel site.

## Do

1. **Prove navigation is available.**
   - `/Script/AIModule.AIBlueprintHelperLibrary` and its `SimpleMoveToLocation(Controller, Goal)` UFUNCTION: find them through `StaticFindObject` and the function's reflection data. Call them through `ProcessEvent` on the class default object, so no new RVA is needed.
   - Check that FED_C1M1 has a navmesh (`RecastNavMesh-Default`, or an `ANavigationData` actor).
   - Report both before building on them. If either is missing, use the fallback: steer with `axis MoveForward` and yaw toward the goal from the state file, with stuck detection (less than 50 uu of progress in 2 s means stuck: back off, turn 45 degrees, retry, at most 5 times).
2. **Commands.**
   - `goto <x> <y> <z> [radius]`.
   - `goto actor <placed name> [radius]`: the name is resolved in the current world.
   - `look actor <placed name>`: `SetControlRotation` toward the actor's location.
   - `look <yaw> <pitch>`.

   Each `goto` logs its progress every 2 s and ends `done` within the radius (default 150 uu), or `failed (stuck|timeout|no path)`. Each needs a timeout: the distance at walking speed plus 20 s.
3. **State.** Add `goal=` and `dist=` while a `goto` is running.
4. **Proof scenario, `tools/scenarios/host-morita.txt`.** This is the walk card059d could not do, with no teleport: after the brief, the host walks to the nearer placed `BP_WeaponPickup_MoritaMk1_C`, looks at it, presses Use, then 2, and takes a shot. It passes when the pickup and the `EquipWeapon` lines appear. If card 062 is not fixed yet, the 2 press may fail, and that alone does not fail this card.
5. **Second proof, client side.** The client walks to the zipline from card 054 and presses Use. This closes the card 054 and 058 "not reached" item.

## Senior research (2026-11-04, static)

- The shipping exe has the reflection names `/Script/AIModule.AIBlueprintHelperLibrary`, `SimpleMoveToLocation`, `SimpleMoveToActor`, `FindPathToLocationSynchronously`, `PathPoints`, `GetPathLength` and `bAllowClientSideNavigation`. AIModule and NavigationSystem are compiled in.
- The persistent `FED_C1M1.umap` name map lists `NavMeshBoundsVolume`, `NavModifierVolume`, and the nav data actors `RecastNavMesh`, `RecastNavMesh_Small`, `_Medium`, `_Large` and `_Gigantic`. That is several agent sizes, probably for the bug types. Which one the player's pawn gets is chosen from the pawn's `NavAgentProperties`. Log the nav data actor that the path query uses.
- The `FED_C1M1_Scripting.umap` name map lists `BP_WeaponPickup_MoritaMk1` (two instances), `BP_ZiplinePoint` (instances 0, 2, 3 and 4) and `BP_FallZones_ZiplineEmergency`. Resolve the instance names at run time; the name map does not give their number suffixes reliably.
- **Client risk, confirmed (2026-11-04 21:55).** The shipped `StarshipTroopers_UBW/Config/DefaultEngine.ini` (pulled out of `pakchunk0-Windows.pak` and Oodle-decoded with `tools/iostore/oodec.exe`) has `[/Script/NavigationSystem.NavigationSystemV1] bAllowClientSideNavigation=False`. Stock UE then creates no navigation system on a client, and `SimpleMoveToLocation` returns without doing anything there, so the zipline proof (step 5) would fail without any error. Still confirm at run time that the client world's `NavigationSystem` property is null.
  - If it is null, the preferred fix is this: with `UBWCOOP_CMD=1` only, set the property to true on the `NavigationSystemV1` CDO before the map loads (at DLL init, through reflection). The client then builds its nav system from the navmesh that is saved in the level. Confirm that the client's nav system finds a `RecastNavMesh` after the load.
  - If that does not work, use the steering fallback on the client.
- **Nav data per agent (same ini).** `[/Script/NavigationSystem.RecastNavMesh] bCanBeMainNavData=False`, so the plain `RecastNavMesh` never serves as the main nav data. Six Aloft subclasses do: `RecastNavMesh_Small`, `_Medium`, `_Large`, `_Huge`, `_Gigantic` and `_GiganticTall`, all `RuntimeGeneration=DynamicModifiersOnly`, so the navmesh is baked into the level and not built at run time. `SupportedAgents` radii: Default 75, Small 25, Medium 175, Large 275, Gigantic 660. A path query with no agent context may pick the wrong mesh or none. Pass the player controller or pawn as `PathfindingContext` to `FindPathToLocationSynchronously`, and log the nav data class the query used. If it picks `_Small` (radius 25), the path can hug walls closer than the trooper's capsule, and the walker's stuck detection has to cover that.
- **Which mesh the pawn gets (same ini, `SupportedAgents`, 2026-11-04 22:00).** Agent `Default` (radius 75, height 350) uses `NavDataClass` and `PreferredNavData` `/Script/NavigationSystem.RecastNavMesh`, the plain class. `bCanBeMainNavData=False` keeps it from being the *main* nav data only. Stock `GetNavDataForProps` still returns it for a query whose agent props fit it best, and the persistent map holds a `RecastNavMesh` actor. Expected result: a query with the pawn as context gets the plain `RecastNavMesh` (radius 75) if the trooper capsule radius is 75 or less. A query with no context gets the main nav data, which is one of the Aloft classes, probably `_Small`. Log the pawn's `NavAgentProperties.AgentRadius` and the class that answered, to check this.
- `bShouldDiscardSubLevelNavData=True`. Earlier research says the nav data actors are in the persistent `FED_C1M1.umap`, so this flag should not matter. If the host finds no nav data, check this first.
- **One movement path for both roles.** Calling `SimpleMoveToLocation` is acceptable. A simpler shape that also covers the fallback: get the path points from `FindPathToLocationSynchronously` (or, with no nav system, a straight line to the goal) and steer along them with the existing `axis` input path. This gives one walker for both roles, and the moves go through real player input.

## Done when

- Both scenarios reach their targets twice in a row, with no teleport and no `xdotool`.
- The Report lists each `goto` with its distance, time and result.
- `logsum` gives ERROR/FATAL 0, every game ends with rc 0, and there is no new dump.
- fnlen reports no function over 60 lines.

## Report

Junior ultimate-bug-war-coop-bb, 2026-11-04. Build `ninja -C build -j4` is clean (`/W4 /WX`). Every game pair ran in `systemd-run --user --scope -p MemoryMax=12G`, windowed at 640x360, with the null sink. Env: `HOST_EXTRA_ENV="UBWCOOP_AUTODROPSHIP=1"` and no client extras. There is no test flag and no teleport. The xdotool shim on PATH logged no calls in any run.

### 1. Navigation proof (run c64a, then every run)

- `AIBlueprintHelperLibrary` was found at 00007FFFDE1637E0, and `SimpleMoveToLocation` at 00007FFFDE718D68, through StaticFindObject and FindFunction.
- **Host world FED_C1M1:**
  - `NavigationSystemV1` exists.
  - It has six nav data actors in level 0: `RecastNavMesh-Default` (AgentRadius 75), `_Small` (25), `_Medium` (175), `_Large` (275), `_Gigantic` (660) and `AbstractNavData-Default`.
  - `NavDataSet` holds 5 registered.
  - Pawn `NavAgentProps.AgentRadius` is 40.
- **The finding: `MainNavData` is null on both machines.**
  - Stock `FindPathToLocationSynchronously` returns no path object without a default nav data instance. Run c64b logged `path 0000000000000000` both with the pawn as `PathfindingContext` and with no context.
  - Fix, test runner only: on the first goto of a world, if `MainNavData` is null, `c64_adopt_main_nav` sets it to the placed `RecastNavMesh-Default`. That is the Default agent (radius 75), which covers the trooper's radius of 40. It logs `card064: MainNavData was null; set to RecastNavMesh-Default (RecastNavMesh) for the test runner`.
  - Path queries then return navmesh paths, with the pawn as `PathfindingContext`.
  - The class that answers a context query is not visible from the `UNavigationPath`. The line above names the main nav data; the per-agent pick inside the engine was not logged.
- **Client:** the `UBWCOOP_CMD=1` CDO flips run at `GameEngine::Init`:
  - `NavigationSystemV1 bAllowClientSideNavigation 0 -> 1`
  - `NavigationSystemModuleConfig bCreateOnClient 0 -> 1`

  The client world then has `NavigationSystemV1`, the same six actors and 5 registered, with a null `MainNavData`, which gets the same adoption. No run was made without the flip, so the stock client's null nav system was not observed again here; the ini research covers it.
- **Fallback:** the steering fallback is still built in. If the world has no nav system, or no main nav data can be adopted, the walker walks a straight line. It counts stuck at under 50 uu in 2 s, then backs off 0.6 s, turns 45° and retries, at most 5 times. Run c64c (before the adoption) reached the Morita this way: 12673 uu straight, 36.4 s, 3 stucks, inside a 38 s timeout.

### 2. Commands and state

- `goto x y z [r]`, `goto actor <name> [r]`, `look actor <name>` and `look <yaw> <pitch>`.
- Actor names match the full object name with its number suffix (`BP_ZiplinePoint_C_3`); otherwise the nearest actor of that class name is used.
- Goto timeout is the path length at `MaxWalkSpeed` (700) plus 20 s. Progress is logged every 2 s. A goto ends `done` within the radius, 2D, with dz ≤ 300, or `failed (stuck|timeout|no path)`.
- drive.py: command steps take `timeout=S`. The goto default is 620 s; the game ends a goto itself by its own timeout.
- The state line ends ` goal=x,y,z dist=D` while a goto runs, and ` goal=- dist=-` otherwise. Only the idle form was read back in these runs; no scenario step waits on the goal field.

### 3. Proof runs

All goto results below were taken from the navmesh path with the final DLL, except where noted.

| run | scenario | goto | path | time | result |
|---|---|---|---|---|---|
| c64d* | host-morita | (-1769 1820 296) to Morita (6445 11469 101), r 120 | 18 points, 13407 uu | 18.7 s | done, dist 119, 0 stucks |
| c64e* | host-morita | same | 13407 uu | 18.7 s | done |
| c64l | host-morita | same | 13407 uu | 18.7 s | done, dist 118 |
| c64m | host-morita | same | 13407 uu | 18.7 s | done, dist 115 |
| c64n | host-morita (final scenario) | same | 13407 uu | 18.7 s | done, dist 115, 0 stucks |
| c64o | host-morita (final scenario) | same | 13407 uu | 18.7 s | done, dist 120, 0 stucks |
| c64g | client to `BP_ZiplinePoint_C_1` | (-560 -1715 103) to (17300 19713 1383) | 29 points, 30670 uu | 63.8 s | failed (timeout): 2D dist 2, but on the ground at z 97 |
| c64h | client to 17420 19661 1208 (card 054 pose) | same start | 25 points, 30382 uu | 63.4 s | failed (timeout): ground under the platform |
| c64i | client to `BP_ZiplinePoint_C_3` | to (17337 14717 333) | 27 points, 26660 uu | 37.8 s | done, dist 145 (scenario regex fixed after) |
| c64j | client-zipline | same | 26660 uu | 37.7 s | done, dist 150, 0 stucks |
| c64k | client-zipline | same | 26660 uu | 37.7 s | done, dist 144, 0 stucks |

\* c64d and c64e were run before the numbered-name fix. c64l to c64o used the final DLL.

- **Host Morita, c64n and c64o.** These two used the final scenario and passed back to back.
  - The pickup takes on overlap as the walker arrives, before the Use press: `GiveWeaponByClass(BP_WeaponDefinition_MoritaMk1_C, slot -1, equip 0) = 1 ... weapons 2`.
  - The game then equips it at once: `EquipWeapon(1) ... game`, which also logs `ConstructWeaponSwitchRequest(1, 0)`. A second `GiveWeaponByClass` follows with weapons 1, and the final state is `weapons=1 equipped=0`. Unarmed appears to be replaced.
  - The 2 press reaches `AbilityInputTagPressed InputTag.SelectWeapon.Slot2` and `ConstructWeaponSwitchRequest(1, 0)`, with no EquipWeapon after it. That is the card 062 gap, and per this card it does not fail the run.
  - The scenario now waits for each of these in order, so no wait matches a line logged earlier than the step it checks.
  - Host shots show the brief movie (card 067).
- **Client zipline, c64j and c64k.** Both passed back to back.
  - FED_C1M1 has four points: `_C_3` (17337 14717 333, ground), `_C_2` above it (17346 14722 1085), `_C_1` (17300 19713 1383, the card 054 target) and `_C_0` (17252 28454 1645).
  - The navmesh does not reach `_C_1` or its platform. Runs c64g and c64h end on the ground under it. `_C_3` is the walkable start of the chain, so the scenario uses it.
  - The client's Use is relayed: `card054: host relayed Use #1: BP_PlayerCharacterMI_C Interact on BP_ZiplinePoint_C (..., row zipline point)`.
  - **No ride:** the `c54_track` server copy stays at (17280 14699 97) for 12 s, as in card 054. Most likely the zipline's `CanInteract` is false this early, or a ride needs something the relay does not do. This closes the card 054 and 058 "not reached" item; the ride itself is still open.

### 4. Gates

- `logsum`: ERROR/FATAL 0 on both roles for c64j, c64k, c64l, c64m, c64n and c64o.
- Every game in every run ended with rc 0. No new crash dumps.
- fnlen: `hooks.cpp` 1214 functions, `cmdfile.cpp` 17, none over 60.

### Files

- `mod/src/hooks.cpp`: the card 064 block after `C63 g_c63{}`, the CDO flip in `detour_game_engine_init`, and the goto, look and state hooks in `c63_*`.
- `mod/src/cmdfile.{h,cpp}`: the goto and look parsers.
- `tools/drive.py`: `timeout=` on command steps.
- New: `tools/scenarios/host-morita.txt` and `tools/scenarios/client-zipline.txt`.

### Open

- The zipline ride (above).
- Adopting `MainNavData` is a write into the game's nav system, under `UBWCOOP_CMD=1` only. Why the game leaves it null was not traced; `bCanBeMainNavData=False` on `RecastNavMesh` is the likely reason.

## Senior review (2026-11-04 23:14): accepted

Checked by the senior, not taken from the Report:

- `logs/c64n.drive`, `c64o.drive` (host-morita) and `c64j.drive`, `c64k.drive` (client-zipline) each end `scenario passed`. Each pair is consecutive. The DLL in `Win64/` is from 22:57:27, before all four runs.
- No `teleport` and no `xdotool` in any of the four drive logs.
- `logsum` on all four: ERROR/FATAL 0, WARN 0 on both games. No crash folder newer than 15:28.
- fnlen on `mod/src/hooks.cpp`: 1214 functions, 0 over 60 lines.
- `c64_allow_client_nav` runs from `detour_game_engine_init` only when `g_c63_on` (`UBWCOOP_CMD=1`). `c64_adopt_main_nav` is reached only from the goto planner. With `UBWCOOP_CMD` unset, neither write happens.
- Shots: `client-c64k-zipline.png` shows the client at the zipline tower. `host-c64o-morita.png` still shows the brief movie, which is the known gap that card 067 fixes. In c64o the Morita pickup is proved by the log waits (`GiveWeaponByClass`, `EquipWeapon(1)`, `SelectWeapon.Slot2`), not by the shot.

Follow-ups: the zipline ride (card 054 gap), the `MainNavData` root cause (open, test runner only), and the Slot2 press with no `EquipWeapon` after it (card 062: the game's own refusal with one weapon). Next: card 067.
