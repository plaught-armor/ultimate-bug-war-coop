# Card 575: client runs the detour crowd simulation and nav tile rebuilds for host-owned NPCs

Found in card 573 step 1 (run c569b390b, b390, combat window 09:51:48-52:08, perf by thread).

## Evidence

Samples in `Background Work`, per 20 s window:

| Window | dtCrowd | dtSharedBoundary | dtBuildTileCacheContours |
|---|---|---|---|
| host combat | 2269 | 731 | about 1060 |
| client combat | 2275 | 755 | about 1050 |
| host and client quiet | 0 | 0 | 0 |

The client's crowd and tile cache cost matches the host's almost exactly. NPC movement is the host's decision and
reaches the client by replication (card 132 pose and movement). A client-side crowd step for those agents does work
that nobody uses, about 2 to 3 CPU s per 20 s of combat.

## Trace before any change (static)

1. Who ticks the crowd on the client. `UCrowdManager::Tick` is driven by the navigation system. Find out whether the
   client has a full `UNavigationSystemV1` (the `bAllowClientSideNavigation` setting, or a mod path that creates it)
   and which agents are registered: `UCrowdFollowingComponent` on the client copies of AI controllers, or the card
   532/535 local NPC pieces.
2. Why the tile cache rebuilds on the client. Dynamic obstacles or nav modifiers on moving actors dirty tiles. A client
   that does not path does not need the rebuild.
3. Check whether any client feature reads the client's navmesh (the player's own path, the minimap, a scenario `goto`
   on the client). The test driver's client `goto` uses navmesh paths, so a cut must keep the navmesh query and drop
   only the crowd and the rebuild.

## Fix shape (after the trace)

Client only: skip the crowd step for agents whose controller has no authority, or unregister them; skip the tile
cache rebuild when nothing on the client paths. Host unchanged. Off switch `UBWCOOP_C575_OFF=1`.

## Proof

A matched combat window: client dtCrowd and tile cache samples near 0, and client card 132 pose mismatches 0. The
client scenario `goto` still arrives.

## Static trace, senior 2026-11-21 (exe call scan against the PDB publics)

- dtCrowd's update steps (`updateStepProximityData` 0x04A59AC0, `updateStepAvoidance` 0x04A583E0) have one caller
  each: `UCrowdManager::Tick` (0x04BAA7D0, +0x395 and +0x3EF).
- `UCrowdManager::RegisterAgent` (0x04BA4860) has two callers: `UCrowdFollowingComponent::RegisterCrowdAgent`
  (0x04B76F30) and `UCrowdFollowingComponent::SetCrowdSimulationState` (0x04B80360). No game class registers its own
  agent.
- So a client crowd cost needs client `UCrowdFollowingComponent`s, which normally live on AI controllers. Card 532
  says those exist only on the host. One of the two is wrong for this game.
- The profile's names are the nearest public symbol: `dtCrowd::getAgent` (a one-line getter) with 1964 samples is
  really unnamed Detour code after it. The row is "Detour crowd and nav code", not getAgent.

## b395 `c4c37e8155864617` (= b394 + step 1 diag, build-b5 copy kept)

`UBWCOOP_C575_LOG=1` installs a row on `RegisterCrowdAgent`:
`card575: <side> t=N RegisterCrowdAgent #K <component> outer <controller> (<class>)` for the first 24 registrations,
then every 64th.

Predictions: the client logs registrations whose outer is an AI controller class (the client spawns or keeps its own
controllers for NPCs), about as many as the host. If the client logs 0, the client crowd cost is not agents; then
the next check is UCrowdManager::Tick's caller on the client (the nav system tick) and the tile cache rebuild alone.

## Correction and b397 `f9db0cab8fd0c1e7`, senior 2026-11-21

c569b395a: 0 `RegisterCrowdAgent` calls on either side, so the b395 row hooked the wrong path.

The .pdata mapping (scratchpad `pdata_top.py`) of the c573b390b client combat Background Work samples renames the
row. 0x04a514c0 (1617 samples, labelled "dtCrowd::getAgent") is called only from `dtBuildTileCachePolyMeshDetail`.
0x04a527e0 is called from `dtBuildTileCachePolyMesh`. Both sit under
`FRecastTileGenerator::GenerateNavigationDataLayer` (0x04af4200). So about 2300 samples are runtime navmesh tile
rebuilds, now card 577. Real crowd work is about 1000 samples: `dtSharedBoundary::CacheData` 685 via
`updateStepProximityData`, 241 from `updateTopologyOptimization`, and `IsValid` 66. The b395a quiet window had 0
crowd or nav samples.

b397 hooks `UCrowdManager::RegisterAgent` (0x04BA4860) itself:
`card575: <side> t=N UCrowdManager::RegisterAgent #K agent <ptr> from exe+0x<caller rva>, manager <name>`.
It logs the first 24 calls, then every 64th, and only with `UBWCOOP_C575_LOG=1`. Prediction: the client logs
registrations, and the caller RVA names the path (SetCrowdSimulationState +off, or a caller the rel32 scan
missed). It needs a combat window.

## Result in run c397b398a (2026-11-21): closed by card 577

The RegisterAgent hook installed but logged 0 rows on both sides. With card 577, the client's crowd and tile
samples went to 0 in the combat window (client Background Work 2777 against the host's 6163). The client crowd cost
was the tile rebuild's own crowd calls, not separately registered agents. Card 575 is closed. The diagnostic hook
stays opt-in (`UBWCOOP_C575_LOG=1`).
