# 321 Host tick and timer code that asks for "the player" gets the host only

Sync audit second pass, 2026-11-14 (`docs/SYNC_AUDIT.md` L1). Cards 099 and 102 remap "the player" for a server
copy's own context and inside overlap, damage, interaction and enemy scopes. Game code that runs from a tick or a
timer with a world context still resolves player 0.

## Settled (exe disassembly)

- `UADLevelStreamingSubsystem::ProcessVolumes` (0x0455F4B0): `UWorld::GetFirstPlayerController` (0x0433FE20), the
  camera manager's view target, `AVolume::EncompassesPoint` per volume, then `ULevelStreaming::SetShouldBeVisible`
  and `UpdateActiveActorsInAllActiveLevels` (0x04560170). On the host only the host's view loads a sublevel and
  keeps its persistent actors active.
- `ASTSpawnPoint::IsOutOfSightOfPlayer` calls `USTCommonUtilityLibrary::IsActorInSightOfPlayer` (player 0).
- `ASTDoor::MoveDoorToNewPostion` calls `HandleCharactersInMovePath`, which reads `GetPlayerCharacter(0)`.

## Not settled (step 1, senior, static, before any code)

1. Which Trooper maps place `AADLevelStreamingVolume` actors and which sublevels they gate (from the map assets or
   one host log row at `OnWorldBeginPlay`: volume count, level names).
2. What `HandleCharactersInMovePath` does to the character it finds (push, damage, reopen).
3. For each candidate in the L1 table (fall zone reposition, teleporter end, troop dropship `CanTakeoff`, threat
   level, moving hazard, ally nav filter): the call path and whether a scope is live when it runs.

## Step 1 findings (senior, 2026-11-14, exe disassembly and `logs/b14-soak-c1m1-host.log`)

- Streaming on C1M1: the subsystem is live, but only at map load. Its 16 `RequestGameUnpause` rows (caller
  exe+0x455F3E1, inside `ProcessLoadingLevels` 0x0455F200, the "loading list empty" branch) all fall between
  279096079 and 279104975 ms, the first 9 s after the map load at 279095670. None in the 31 minutes of whole-map
  soak after. So C1M1 has no streaming during play and cannot prove the streaming rule; no log names the volume
  count on any map (no row prints it). The first diagnostic row below settles item 1 per map.
- `UADLevelStreamingSubsystem::Tick` (0x0455FEB0) calls `ProcessVolumes` then `ProcessLoadingLevels` every tick,
  also while paused (`IsTickableWhenPaused`). A level that starts loading requests a game pause through
  `IADPausableObjectInterface` and releases it when the load list empties. On a map that streams in play, this
  pause is a second sync fault: card 096 skips it while a client is connected, so the host plays on while the
  level loads. Covered by the same rule (card 320 decides what a pause does in co-op).
- `ASTDoor::HandleCharactersInMovePath` (0x050EC950), two parts:
  1. A `SweepMultiByChannel` along the door's move. Every hit actor is tested by class (`APawn`,
     `ASTPlayerCharacter`, `ASTCharacter`); a player character hit with door+0x357 set and `IsActorCrushable` true
     marks the move blocked. This part is per actor and already right for a server copy.
  2. Only when the caller's flag argument is set: `GetPlayerCharacter(0)`, then
     `UPrimitiveComponent::IsOverlappingActor(door+0x360, player 0)`. If overlapping: with door+0x355 set, one
     `ApplyDamageAtLocation` with `USTDoorDamageData` (latched by door+0x4E0) and the move is blocked; otherwise,
     if crushable, `USTCombatUtilityLibrary::InstaKillTarget(player 0)`. Not overlapping: the latch clears.
     So a client's trooper standing in a door's crush volume takes no door damage, is never crushed, and does not
     block the door through this path. Gameplay row. Rule: "is any player in the way", run once per player.
     After it, `GetOverlappingActors` of `ASTNonPlayableCharacterBase` handles NPCs per actor (right as is).
- Item 3, the other L1 candidates (exe disassembly; "scope" = card 102's acting scope, which only an overlap,
  damage, interaction or enemy entry sets):

| Function | What it reads | Scope when it runs | Effect for a client | Kind |
|---|---|---|---|---|
| `ASTHazardousZoneBase::TickMoveToNewPostion` (0x05173AA0) | `GetPlayerCharacter(0)`, `IsOverlappingActor(zone+0x320, player 0)`, then `CrushActor(player 0)`; after it `GetOverlappingActors` filtered to `ASTNonPlayableCharacterBase`, `CrushActor` each | none (tick) | A moving hazard never crushes a client's trooper. Same shape as the door. | any player in the way |
| `ASTProceduralDropshipTroops::CanTakeoff` (0x051F5680) | `GetSTPlayerCharacter(world)`, then bounds and a line trace around that character | none (tick behaviour) | The troop dropship's takeoff check sees the host only; it can take off through a client standing under it. | any player in the way |
| `ASTFallZonesBase::RepositionPlayer` (0x0516BA90) | Takes the fallen player as a weak pointer and repositions that character (`USTCommonUtilityLibrary::RepositionPlayer`), then `GetSTPlayerController(zone)` for the controller it uses in the timer that follows | none (deferred) | The right character is moved; the controller-side step (input or fade, by the timer) goes to the host. | the player this happens to |
| `ASTTeleporterVolumeBase::StartPlayerTeleport` (0x05170AC0) | Takes the character; `GetSTPlayerController(volume)` for `ForceInputEnabled(0)`; the character argument for the rest (tag, `SetActorLocation`, kill of what stands at the destination) | overlap scope if entered from the overlap; none from a timer | In scope: right. From a timer: the host's input is frozen for a client's teleport. | the player this happens to |
| `ASTTeleporterVolumeBase::EndPlayerTeleport` (0x05155240) | Same pair: `GetSTPlayerController(volume)` twice, `ForceInputEnabled`; called directly at the end of `StartPlayerTeleport` or from its timer | none on the timer path | The host's input is re-enabled; the client's stays off if the start was in scope. | the player this happens to |
| `USTNPCNavigationQueryFilter::InitializeFilter` (0x05195890) | `GetSTPlayerCharacter(querier)`, faction check, then the `USTPlayerDeterAllyNavArea` area cost | none (path query) | Allies are deterred from the host's area only; they may path through and push the client. | cosmetic to minor; last |
| `ASTGameplayGameMode::Tick_PlayerCombatTracker` (0x0513DAF0) | `ASTAIAwarenessSystem::CalculateThreatLevel` | none (tick) | `CalculateThreatLevel` (0x050DFF80) takes `GetSTPlayerController(system)`, its threat location (`ISTThreatLocationInterface`), then weighs enemies by attack target, faction, size and distance to that one location. A fight around the client alone reads as no threat: combat state and music mode stay calm for both. | how hard is the fight |

  Card 099's remap sits in `UGameplayStatics::GetPlayerController` (`detour_c99_get_pc`): a context that is a copy's
  own object, else the acting scope, else player 0. So every "none" row above gets the host.
- The door's flag argument: the only caller is `MoveDoorToNewPostion` (call at exe+0x50F49E5), flag = door state
  byte +0x300 equals 3 (taken to be "closing"; the state enum is not read). So part 2 runs on every closing move.
- Open in step 1: which Trooper maps have
  moving hazards, teleporters and fall zones (the begin-play row counts them: add `hazard zones <n>, teleporters
  <n>, fall zones <n>, doors with crush <n>` to it).

## Fix (class rule, after step 1)

One rule, not one hook per caller: a host call for "the player" with a world context and no scope is answered per
kind of question.
- "Is any player here / in sight / in the way" (streaming, spawn sight, door path, dropship takeoff, hazards):
  true if true for any player. Streaming keeps a level visible while any player's view target is in its volumes.
- "The player this happens to" (reposition, teleport end): the player the earlier scoped call was for, carried
  from that scope to the deferred call.
- "How hard is the fight" (threat level): the highest over players.

## Diagnostic (rides the build)

Map-end row, host: `card321: unscoped player-0 calls by caller: <function> <n> ...; with a server copy alive <n>`.
Once at `OnWorldBeginPlay`, host: `card321: host map <name> streaming volumes <n>, gated levels <n>: <names>`.
Per streaming change: `card321: host level <name> visible <0|1>, host view inside <0|1>, copy views inside <n>`.
Also the L5 items: per opcode and side, frames dropped (malformed, no handler) at map end; remove `PickupGone`.

## Predictions (one run, C1M1, client walks alone to the far objective area while the host stays at the start)

- Before the fix: if the map streams, a `visible 0 ... copy views inside 1` row and a client-side `loaded on one
  side` count above 0. If no such row, C1M1 does not stream and the proof map is the first Trooper map step 1
  names.
- After: no level goes `visible 0` with a copy view inside; `loaded on one side: 0 host, 0 client` every round.

## Done when

Step 1's table is in this card, the rule covers every row marked gameplay, and the run's rows match.


## Step 1 data (senior, 2026-11-18 12:31, from every `card321: host map` row in logs/*-host.log)

| Map | Streaming volumes | Teleporters | Doors (crush) | Hazard zones | Fall zones |
|---|---|---|---|---|---|
| C1M1 | 0 | 0 | 0 | 1 | 4 |
| C2M1 | 0 | 0 | 0 | 32 | 30 |
| C3M1 | 0 | 0 | 0 | 126 | 27 |
| C3M2 | 0 | 0 | 0 | 89 | 19 |
| C4M1 | 0 | 0 | 0 | 62 | 8 |
| C4M2 | 0 | 0 | 0 | 68 | 46 |
| C5M1 | 0 | 0 | 0 | 84 | 16 |
| C5M2 | 0 | 0 | 0 | 129 | 15 |

No Trooper map has streaming volumes, teleporters or doors, so those rows are moot for the Trooper playthrough
(the streaming prediction above cannot fire on any of them). Of the gameplay rows, three are already fixed by
other cards: the hazard crush (`detour_c361_hazard_tick`, card 361), the fall zone reposition (`detour_c338_reposition`,
card 338) and CanTakeoff (`detour_c449_can_takeoff`, card 449, run per server copy in its acting scope).

Left on this card:
- Threat level. Disassembly of `Tick_PlayerCombatTracker` (0x0513DAF0): `CalculateThreatLevel(this, data+0x78,
  data+0x7C)` returns a float; it is compared with the thresholds in the data asset map at game mode +0x458 to give
  a level 0, 1 or 2, then held by the timers at +0x464 to +0x470 before game mode +0x460 changes. The only readers
  of +0x460 are in `USTMusicManager` (SYNC_AUDIT row), so this is music only, a hearing item. Each side runs its own
  tracker. Open question: does the client's tracker rise in a fight, since its enemies are replayed copies with no
  AI attack target? Diagnostic: one row per side when +0x460 changes (old, new, threat value), read after the
  original `Tick_PlayerCombatTracker`. If the client's level never leaves 0 during a fight while the host's does,
  the fix sends the host's level for the client to use (highest of the two); otherwise nothing to do.
- Nav filter (`USTNPCNavigationQueryFilter::InitializeFilter`): allies avoid the host's area only. Minor; last.

The map-end row `card321: unscoped player-0 calls by caller` named under "Diagnostic" is not in the source:
`player0_c321.cpp` (210 lines) prints only the host map, door and pilot check rows. With the rows above settled it
is no longer needed.

## Closed (2026-11-18 14:07)
Evidence: b228 `11b7efb307ea9b4e`. `c321_client_modes` ports the host's music-mode branches into the client tick post-hook. c245b228a: client music follows host combat level changes in 33-66 ms. c244b228a: WARN321 0 on both sides; the client logs 17 "host combat level" transitions (0->1, 1->2, 2->0 and others).
Not done: no hearing check (the user cannot test audio now; card 302 holds hearing).
Reopen when: a client hears combat music that differs from the host's, or a WARN321 row appears.
