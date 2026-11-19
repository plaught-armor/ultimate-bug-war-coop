# 346 The client's connection survives every host map change (seamless travel)

User, 2026-11-14: "The lobby also needs to be persistent because the client disconnects from host after a match",
then, asked whether a brief drop plus automatic rejoin is acceptable: "No, just make persistent lobby."

## Cause (design, card 154 option A)

Every host map change is a hard travel with `listen` added (`c45_host_browse`). The engine closes the net driver,
the client gets failures 5 and 3, lands in its own `3D_Frontend` and the card 045 follow joins again
(`c310-proof-c1m1`: 5057 ms into the hangar, 9101 ms into FED_C2M1). The connection drops at every map change; a
follow that does not land within 60 s leaves the client alone in its menu.

## Trace (static, lane 1 exe, image base 0x140000000)

- The engine's seamless travel is compiled in: `UWorld::ServerTravel` RVA 0x04355360, `AGameMode::CanServerTravel`
  0x03C07BC0, `AGameModeBase::ProcessServerTravel` 0x03C67370, `UWorld::SeamlessTravel` 0x04352930,
  `FSeamlessTravelHandler::StartTravel` 0x04356800, `AGameModeBase::ProcessClientTravel` 0x03C65960.
- `ProcessServerTravel` tests `bUseSeamlessTravel` at GameMode+0x328 bit 0 (0x143C673B8). With the bit clear it
  tells clients to hard travel, which is a reconnect.
- `ProcessClientTravel` sends remote players `ClientTravel` with travel type 2 (TRAVEL_Relative, `mov r8d,0x2` at
  0x143C65B11) whatever `bAbsolute` is. A client travels seamlessly only on a relative travel, so this is what we
  need. The host can therefore pass `bAbsolute = true` and carry no stale options (`FromLevelComplete`) from the old
  URL into the next one.
- From the engine source as I remember it, not read in this exe: an empty `UGameMapsSettings::TransitionMap` makes
  `StartTravel` create an empty transition world; `FSeamlessTravelHandler::Tick` never calls `UEngine::LoadMap`.

## Fix (not written)

Host, `c45_browse_enter` (`mod/src/ubw/restart.cpp`): a listen host with at least one client connection whose own
Browse goes to a local map does not run the Browse. It sets GameMode+0x328 bit 0 on the world's GameMode, calls
`UWorld::ServerTravel(url text with options, true, false)` and returns `kBrowseSuccess` through the card 345
not-run path (TravelURL emptied, FURL destroyed). The game's own steps before OpenLevel (level-end save, loading
screen request) have already run by then. With no client connected the hard travel plus `listen` stays as it is.
`ServerTravel` returning false: `WARN card346` and the Browse runs as today.

Map change steps without LoadMap. All per-map state hangs on `ubw_load_map_enter` and `ubw_load_map_return`
(`hooks_ubw.cpp`), which a seamless travel never reaches. New detours, both sides:

- `UNetDriver::CleanupWorldForSeamlessTravel`: the old world is still alive. Runs `ubw_load_map_enter(engine,
  pending)` with `pending` standing for "session kept" so `g_client_session` stays as it is.
- `UNetDriver::PostSeamlessTravelGarbageCollect`: the new world is current. Runs `ubw_load_map_return(true, ...)`.
- Both fire twice per travel (transition world, then destination). The transition world is a map with no pawns.

State that assumes a new connection per map, each to be handled in this card:

- Join replay: `sync_join_replay` runs on a PC's first pong (`chan_ops.cpp` `c26_host_pong`, `c50_first_pong`).
  After a seamless travel the first-pong gate must open again for the PC in the new world
  (`AGameModeBase::HandleSeamlessTravelPlayer` may swap the PC object when the class differs).
- Card 290 travel row and the card 045 follow: not sent and not armed for a seamless travel; they stay for the
  hard paths (host quits to its menu, host process exit, card 344).
- Card 052 panel, card 320 hold, card 344 limit: arm from the new map-arrive step, as from `c52_load_map_return`.
- Card 345: stays; the client's own level-end Browse is still not run.

Not settled by the trace, decided by the first run:

1. Whether the game's GameMode classes accept a seamless travel between mission and hangar (PC class swap, pawn
   spawn in `HandleSeamlessTravelPlayer`).
2. Whether the loading screen shows (movie player hooks `PreLoadMap`; `StartTravel` is expected to broadcast it).
3. Whether the destination's options reach `InitGame` (`ChapterID`, `LevelID`, `FromLevelComplete`).

## Prediction (scenario `c346-persistent`: C1M1, `host travel end`, host Next, hangar, next mission; 3-run budget)

- Client, whole run after the first join: 0 `NetworkFailure` rows, 0 `card045: client ... session dropped`,
  0 `reconnected`, 0 `card290` travel rows.
- Host: 0 `card140: host Logout` rows; one `card346: host ... seamless travel to "<map>"` row per map change (2).
- Client world rows: `3D_Frontend` net mode 3, then FED_C2M1 net mode 3, each followed by a join replay
  (`card326` replay rows) and a `Clock` row.
- Both sides: `map end` inventory rows for each world left; 0 new WARN kinds.
- A crash or a stuck transition in run 1 is unknown 1: read the crash stack or the last handler row before any
  change.

## Run 1 (b31 `b5ba648e7aea018b`, `c346-persistent`, 2026-11-14): host crash in the second step

- First leg works: host `card346: host t=332764467 seamless travel to "/Game/Maps/3D_Frontend/3D_Frontend?
  FromLevelComplete?ChapterID=0?LevelID=0?ToBonus=0" started by ServerTravel, Browse not run (2 players,
  bUseSeamlessTravel was 0)`; both sides `seamless map change done: world Untitled`; the client also reached
  `world 3D_Frontend, net mode 3` with 0 `NetworkFailure` and 0 `session dropped` rows.
- Host crash 58 s in: access violation reading 0xC20 at exe+0x404B30F, `APlayerCameraManager::GetViewTarget` with a
  null `this`, called from `UADLevelStreamingSubsystem::ProcessVolumes` +0x68, from `OnWorldBeginPlay`, from
  `FSeamlessTravelHandler::Tick` +0x1296.
- `ProcessVolumes` (RVA 0x0455F4B0) takes `UWorld::GetFirstPlayerController`, calls `GetViewTarget` on PC+0x360 with
  no null check, and leaves cleanly on a null result (0x14455F51B).
- Unknown 1 answered in part: the PC class swaps (`BP_PlayerController_C` to `BP_PlayerController_3DFrontend_C`) for
  both players. One PlayerCameraManager is destroyed at the first new PC's location; whose it was is not proven.
- Corrections to the fix text above: the arrive step runs after `FSeamlessTravelHandler::Tick` returns
  (`PostSeamlessTravelGarbageCollect` at 0x144358D01 runs before the GameMode spawn and BeginPlay, too early).
  `StartTravel` with an empty TransitionMap calls CreateWorld at 0x144356BB7 (verified): world `Untitled`.

Run 2 (assigned): guard detour on `GetViewTarget` (null `this` returns null, one row), plus identity rows: the
camera manager destroy row prints its PCOwner; on the guard's first hit one row per PlayerController (class, Player,
camera manager, pending kill); the arrive row prints the host's own PC class and camera manager. If the destroyed
camera manager was the host's new PC's, stop and report before any respawn fix.

## Run 2 (b32 `da8ef61dd31c3710`, 2026-11-14): no crash, connection kept through both map changes

- Guard holds. The null camera manager is the old-class remote PC kept until the client acks the swap (host log
  lines 4580, 5838); the host's own PC has a camera manager on every arrive. Hits stop at 13. No further fix.
- Client: 0 `NetworkFailure`, 0 `session dropped`, 0 `reconnected`, 0 card290 rows; worlds Untitled, 3D_Frontend,
  Untitled, FED_C2M1, all net mode 3; Clock rows after each arrive. Host: 2 travel rows, 0 Logout during travels.
- Join replay after each arrive (`sync: host ... join replay to <PC class>`, lines 4908 and 7713). The C2M1 one
  sent door 165, world 231, dropship 22, effects 12 and IntroStart: the same state the card 332 teardown refused
  during the new world's BeginPlay (441 frames). Not a gap; the refusal stays. Same design as the hard path.
- Scenario VOID at line 52 (host is inside the dropship behind PressToContinue, `pawn=host-own` cannot hold); fixed
  by the junior. The run did not reach the C2M1 shots.
- Open for run 3: card098/card095 teardown rows as plain rows; card337 client fallback WARN x2 (rendering turned on
  by the fallback, 192 ms and 2423 ms); host `ERROR card110: overlay not built` at C2M1; host WARN card130 in
  3D_Frontend, client WARN card156, host WARN card320 to the old-class PC: judged from run 3.

## Run 3 (b33 `100476d0085b4f43`, 2026-11-14): accepted

- `c346-persistent` passed to its end (87.1 s). Client: 0 network failure rows, 0 `session dropped`, 0 `reconnected`,
  0 card290 rows. Host: 2 seamless travel rows, 0 Logout between the join and the client's quit, 0 ERROR.
- Shots viewed: client in the hangar (mission select), host in the hangar (difficulty page), client in FED_C2M1 with
  HUD and weapon.
- b33 changes: card337 skips the cover for the `Untitled` world (0 WARN); the card110 overlay is built for the
  host's own PC (0 ERROR); 236 teardown rows are plain rows.
- WARN left: host card130 (`WB_Cutscenes_C` still the top widget, 3D_Frontend), host card320 (IntroStart to the
  old-class PC, resent by the join replay), client card156, the 2 guard rows.
- Follow-ups: card 348 (traffic from the old PC after a travel), card 349 (GetFirstPlayerController sites),
  card 350 (photosensitivity screen at the card 344 return). The two hangar screens are each player's own menu page;
  not synced, not carded.
