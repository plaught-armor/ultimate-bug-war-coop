# Card 375: the host loads a save from the hangar with a client in the session

Status: accepted 2026-11-15 (run `c377b65`, b65 `3676f5d1b88c01de`).

## Why now

Since card 346 the client stays in the session in the hangar. The host's CONTINUE or LOAD GAME there is then a map
travel followed by a save load with a remote PC present. No log holds this route: every load so far was in place
(`travel checkpoint`, pause menu) or from the death screen, and the from-save starts of cards 137 and 147 had no
client connected. `restore_c143.cpp:268` still says "the frontend Continue route has no remote PCs", which card 346
made untrue. Likely action in the playtest.

## Static trace

1. Host Browse to the save's map: `c346_host_seamless` makes it a seamless ServerTravel and sets
   `c364_expect_clients(1)`.
2. After the host's map load the game runs Load and LoadFromSlot (hb72: Load caller exe+0x520B3EF, load pawns 1,
   destroy actors 1; LoadFromSlot caller exe+0x520ADDC).
3. `detour_c121_load` sends the card 290 travel row through `c95_send_all`, which sends only to ponged copies. The
   client's new copy has not ponged on this map, so the row goes to 0 of 0.
4. `c143_after_load` sends Reload to every server-copy PC with no pong gate (`chan_send_client_now`). The copy's pawn
   after a load with destroy actors 1 is unknown; card 371 saw none after the death-screen load.
5. Client, if the Reload arrives: `c143_receive_reload` starts the card 045 follow, so the client drops and rejoins
   while it is still in, or just out of, its own seamless travel.
6. Host, at the drop: `c320_logout`. With the hold on, `g_c320.left` is set and `c320_step` returns the host to the
   main menu (card 355). With the hold not on yet, the copy is gone, `g_c364_expect` stays 1 until a client is sent
   IntroStart, and the rejoined client either confirms or the card 344 bound returns the host to the menu.

## Predictions for run c377 (`tools/scenarios/c377-hangar-load.txt`)

The run uses `host travel checkpoint` (LoadLatestSave) in the hangar; no test command clicks the frontend's own
CONTINUE. If LoadLatestSave returns 0 or does not travel there, the run is void and the frontend button's function
has to be found first.

- P1 host: `card132: host travel checkpoint: LoadLatestSave returned 1`, then a `card346` seamless travel row to
  FED_C1M1.
- P2 host, after the map load: `card121 ... save engine load runs (... load pawns 1 ...)`, `card290 ... travel row
  sent to 0 of 0 clients`, and one `card143: host ... Reload to` row for the client's copy (or `card371 ... has no
  player pawn after the load`).
- P3, the suspected defect, one of:
  (a) client `card143: client ... Reload received ...; rejoining`, host `card320 ... left during the hold` and
      `WARN card355 ... host returns to the main menu`;
  (b) the Reload is lost (not sendable or never received) and the client arrives with no pawn: `client wait state
      pawn=client-own` times out;
  (c) the client rejoins before the hold is on and the mission starts for both. Then the only cost is one needless
      reconnect.
- Clean result: no Reload needed, the client holds a pawn after its travel, both continue, client gets Clock rows.

## Fix candidates (after the run names the case)

- Reload only to ponged copies, as the travel row already is; an unponged copy gets the loaded state from the join
  replay at its first Pong. Enough only if the copy keeps or gets a pawn.
- If the load leaves the carried copy without a pawn: the host restarts that player itself at the copy's first Pong
  instead of making the client rejoin during the hold.

## Run c377b62 (b62 `6d664b81169d8df2`): void, scenario order; static trace corrected

Host rows 401487997 to 401488011: the save engine Load runs in the hangar, before the travel (context
`BP_PlayerController_3DFrontend_C`, load pawns 1, destroy actors 1); `card290 ... travel row sent to 1 of 1 clients`;
then Browse `FED_C1M1?FromSave` and `card346 seamless travel ... (2 players)`. No `card143` Reload, no `card371`. So
steps 2 to 5 of the trace above do not happen on this route: no Reload reaches the client and it does not rejoin.
On FED_C1M1 the host's pawn spawns (401489023), the host enters the dropship (401491802), the client gets its own
pawn and `IntroStart bFromSave 1`, and the hold waits for the client. The scenario then waited for the host's own
pawn before the client's continue, so the client never confirmed and card 344 returned the host to the menu at 60 s
(401554844), as designed. Not a mod defect. Scenario reordered; rerun on b63 owed.

Still unknown, the rerun shows it: whether the save's state (position, objectives) is applied on FED_C1M1 after both
continue with a client present, or whether the mission starts fresh from the dropship.

## Run c377b63 (b63 `e7cc13d4e1756cc2`): defect confirmed, root cause

Scenario lines 12-52 passed; line 53 (`host wait state map=FED_C1M1 pawn=host-own`) failed. Host: `card121` save
load runs and ends in the hangar (402319530/402319545), `card346` seamless travel to `FED_C1M1?FromSave`
(402319546), EnterDropship (402323319) with the dropship already in state 2, hold off after 3419 ms (all confirmed),
then `dropship-watch[0..42]` for 150 s: state 2, distance 0, fixed location. No `card121` load and no `card137
journal reconcile after LoadFromSlot` row on FED_C1M1. Client: on foot, 10 Clock rows, digest rounds 1-5 with 0
mismatches.

Cause (static, exe): `USTSaveSystemLibrary::Load` (exe+0x520A580) for a save on another level calls
`USTGameInstance::StoreDeferredLoadParams` (exe+0x51066F0) and opens `<map>?FromSave`.
`ApplyDeferredLoadParams` (exe+0x51F3A00) has one caller, `USTGameInstance::LoadComplete` (exe+0x50F2970): when
byte `gi+0x309` is set it applies the params at `gi+0x310` and clears the flag. `UEngine::LoadMap` calls
`LoadComplete` at its end; a seamless travel (card 346) never does. With `?FromSave` the game mode parks the
dropship and waits for the load to place the player, so the host stays in it.

## Fix (written 2026-11-15, for b64)

- A, `c375_seamless_load_complete` (`hooks_ubw.cpp`, from `c346_seamless_return`): host only, destination world
  only (not the transition world "Untitled"), only when the flag byte is set: call `LoadComplete(gi, 0, empty)`,
  log `card375: host ... deferred save load applied ... (flag 1 -> 0)`.
- B, `c143_after_load` (`restore_c143.cpp`): Reload only to ponged copies. A copy with no Pong on the new map takes
  the loaded state from the join replay; a Reload there would make the client drop and rejoin during the
  press-to-start hold, which card 355 answers with the host going to the main menu.
- Sibling fixed by A: host pause-menu LOAD GAME of a save on another map with a client (same deferred route).

Predictions for the rerun (c377 on b64, save taken at 6554 11459 197): host `card375 ... (flag 1 -> 0)` row right
after `seamless map change done: world FED_C1M1`; `card137 ... journal reconcile after LoadFromSlot` on FED_C1M1;
a `card375 ... no Reload` row for the client copy and no `card143` Reload; no client reconnect; hold off (all
confirmed); host on foot within 600 of the save position; client holds its own pawn; Clock rows; digest
mismatches 0. Open risks the run answers: whether LoadFromSlot (destroy actors 1, load pawns 1) destroys a client
pawn that already exists (card 371 class; then the host must restart that player at its first Pong), and where the
client pawn stands relative to the save position.

## Run c377b64 (b64 `2fbd3d05885beb55`, 2026-11-15): fix A + B pass, new defect at line 59

Lines 10 to 58 passed: `card375 ... no Reload to BP_PlayerController_3DFrontend_C`, `card375 ... deferred save load
applied ... FED_C1M1 (flag 1 -> 0)` at 403034116, `card137 ... journal reconcile after LoadFromSlot`, 0 client
reconnects, hold off after 5719 ms, host on foot, digest round 1 1495/1495 with 1 mismatch (STAISpawner, card 218 class),
0 FATAL/ERROR. Line 59 failed: the host stood at (-1619, 1820, 296), 12639 uu from the save spot.

Cause (traced in the exe, `logs/c377b64-host.log` 403031961 to 403035608):

- `USTSaveComponent::SaveOwnerVariables_Pawn` (exe+0x52170B0) stores, per player pawn, the index of the pawn's PC in
  `UWorld::GetPlayerControllerIterator` (UWorld+0x268). `LoadOwnerVariables_Pawn` (exe+0x520B820) gives the spawned
  pawn to the PC at that index (`AController::Possess`, exe+0x3B35CE0).
- The save was taken in a mission the host opened by LoadMap: list [host, client], host index 0.
- At the seamless return the host's PC was already swapped for a new `BP_PlayerController_C` (appended to the list),
  while the client's kept `BP_PlayerController_3DFrontend_C` waits for its own swap (it comes with the client's
  ServerNotifyLoadedWorld, here at 403034116). The list reads [client old, host new]. The host's saved pawn (save id
  `041E2C31`, at (6554, 11459, 97)) went to the client's old PC `49440BE0` and died with it at 403035608; the client's
  saved pawn (`2E68A277`, at (-1619, 1820, 296)) went to the host PC `7058A300`.
- The `card346 guard PC` rows read host first because they use `UGameplayStatics::GetPlayerController`, which lists
  local PCs first; they are not the raw list order.

## Fix C (written 2026-11-15, for b65)

`c375_host_pc_first` (`hooks_ubw.cpp`) moves the host's entry of the world's PC list to slot 0 right before the
LoadComplete call. The engine then pairs the rows itself, once. The order stays host first afterwards, which is the
order of every LoadMap-opened map.

Predictions for c377 on b65:

1. `card375 ... deferred save load applied ... (flag 1 -> 0), host PC was at list slot 1`.
2. `card048 LoadOwnerVariables_Pawn` for the row at (6554, 11459, 97) is followed by `PossessedBy(... controller <host
   PC>)`; line 59 (`host wait stand 6554 11459 97 600`) passes.
3. The client's saved pawn goes to the client's old PC and is destroyed with it at the client's PC swap; card 307 then
   spawns a fresh client pawn beside the host (near (6554, 11459)). Client own pawn Up, no reconnect.
4. No new WARN against c377b64 apart from the known 2x `GrenadeItem not sent`.

Known residual (not fixed here, low): the client's saved position, health and weapons are not restored on a hangar
load; the client starts with a fresh pawn beside the host. The same-level load (card 143) is not affected.

## Run c377b65 (b65 `3676f5d1b88c01de`, 2026-11-15): pass, card accepted

Drive rc 0 after 86 s. All 4 Fix C predictions held: `card375 ... (flag 1 -> 0), host PC was at list slot 1`; the saved
pawn at (6554, 11459, 97) was possessed by the host PC and line 59 passed (0 uu); the client's saved pawn went to its
old PC and was destroyed with it, card 307 spawned the client beside the host at (6704, 11459, 97), own pawn Up, 0
reconnects, hold off after 6601 ms (all confirmed); digest 1495/1495 with 1 mismatch (STAISpawner, card 218 class),
0 FATAL/ERROR, no new WARN class, the 2 `GrenadeItem not sent` rows of c377b64 gone. Residual above stays open (low).
