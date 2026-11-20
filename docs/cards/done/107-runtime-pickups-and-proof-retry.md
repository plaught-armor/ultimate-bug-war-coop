# Card 107: Every runtime pickup is the host's, and retry the mantle and pod pickup proofs

Status: written 2026-11-06 (senior); sent and accepted 2026-11-06 (mantle and pod pickup go to the user playtest).

Read:

- card 106 Report (rows for mantle, pod pickup, drops);
- card 105 Report (`c105_relayed_class`, `c105_bp_spawn`, 'P'/'D' rows, journal).

Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`. DLL base: `1623a6ff252f3e69` (card 106: 533e5 plus the
`warp ... point` and `charge` test commands).

## Why

1. **Drops.** c106b: the host spawned `BP_WeaponPickup_MoritaMk1_C` at (224358, 40874, 8556) about 6 s after the
   client went Downed, from `UWorld::SpawnActor(UClass*, FVector*, FRotator*, ...)` (exe+0x3DCB440, return address
   exe+0x3DCB50E), not from the Blueprint spawn thunk. Card 105 relays a pickup only when the thunk spawns it, so this
   drop never reached the client. User rule: all world state the same for every player, one mechanism per class.
2. **Mantle.** At the known spot both runs ran the same jump and `GA_Mantling_Search` at (-503.1, 21795.9, 143). In
   c101g, `GA_Mantling_Move` started 1080 ms later. In c106c the search ended at 1084 ms with no move. Difference
   (senior, from the drive logs): runner `key` presses are asynchronous. In c101g `W 1000` was pressed 0.2 s before
   Space, so W was still held about 0.8 s into the search. In c106c `W 400` was released 0.2 s after Space. The search
   very likely needs forward input toward the ledge when it decides.
3. **Pod pickup.** `warp client <pickup> 80` moved the server copy, but the client's own pawn stayed 370 to 450 uu
   away, so its E found no target.

## Do

1. **Relay pickups by class, from any caller.** On the host, every runtime spawn (not placed, not replicated) of an
   `AADPickupBase` subclass is relayed as a 'P' row, whatever the caller. On the client, every runtime spawn of that
   class is blocked except the relayed copies, whatever the caller. Keep the card 105 strike and rack rules. Name
   the function one level above exe+0x3DCB50E that spawned the MoritaMk1 (the downed or dead pawn's weapon drop?) in
   the Report. Check that level load and the placed pickups are not affected (placed=1 or spawned before the map is
   ready).
2. **One retry run on FED_C1M1** (scenario c106-combined.txt, edited):
   - Mantle: `client key W 2000`, then 200 ms later `client key SpaceBar 100`, so W is held through the search. Three
     tries from the point warp. If try 1 still fails, log the search task's result fields and stop the mantle part.
   - Pod pickup: `host warp client point` to 60 uu short of the host pickup's location (read it from the 'P' row),
     wait for the client's own pawn to arrive within 100 uu (log), then `client look point` at the pickup and E.
3. **One short FED_C2M1 run** (c106b route): the client goes Downed and the MoritaMk1 drop appears on the host. The
   client log shows the relayed copy at the same location.

Stop when the fix builds and these runs show it; the user will playtest. A row that still fails is reported, not
chased.

## Done when

The Report has the drop spawner, the relay change, and one row each for drop (host and client), mantle (3 tries,
corrections, end gap) and pod pickup (host shows it taken, client gets the item), with run lengths, rc, ERROR/FATAL
and dumps, and the DLL hash. Ends with "Report complete."

## Report

DLL `bbee121209725fa4` (`build/dwmapi-c107-bbee121209725fa4.dll`). Run c107a used `6b1891f3d9ac6aec`, the same code
before the chord key and the client-side pod warp.

### Drop spawner and relay change

- The MoritaMk1 drop is the **enemy death drop**, not the downed pawn's weapon. The host's `card107:` caller chain on
  C2M1 (c107c), resolved against the PDB publics:
  `UWorld::SpawnActor+0xce` (exe+0x3DCB50E) < **`ASTNonPlayableCharacterBase::SpawnDeathDrops_Internal`**`(FSTDeathPickup
  const&, float, FVector)+0x1ad` < `ASTNonPlayableCharacterBase::SpawnDeathDrops+0x1b7` <
  `ASTNonPlayableCharacterBase::Tick+0x418` < `ASTEnemyBase::Tick` < `ASTAttackSystemEnemyBase::Tick`. The second
  drop came 85 ms before the client went Downed, which is why c106b looked like a downed drop.
- `vehicles.cpp` `detour_c75_world_spawn`: new `c107_runtime_pickup(cls)`. It is true when the class CDO is_a
  `AADPickupBase` and the CDO does not replicate. Such a spawn now relays and blocks from any caller, not only the BP
  thunk.
  - Host: a spawned non-placed, non-replicated pickup sends a 'P' row. A pickup spawned outside the thunk also logs
    `card107: host … runtime pickup <class> at (…), callers …`, with 6 frames.
  - Client: a runtime pickup spawn is blocked (`card105: client … blocked runtime pickup spawn of …`), except the
    relayed copy. `c104_client_spawn` sets `g_c107_copy_spawn` around its own `g_world_spawn_actor` call. That call
    goes through the same detour with caller exe+0x3DCB50E.
  - The card 105 strike and rack rules are unchanged.
- Level load and placed pickups are unaffected:
  - C1M1 (c107a, c107b): 0 `card107:` lines and 0 `blocked runtime pickup` lines.
  - Client tally: placed pickups are still placed=1 (e.g. `BP_WeaponPickup_MoritaMk1_C collision n=4 placed=4`).
  - The C1M1 pod pickup (caller exe+0x3C78588, `UGameplayStatics::execBeginDeferredActorSpawnFromClass`, BP thunk)
    relayed as before.
  - C2M1 (c107c): the only `card107:` lines are the 2 death drops.

### Test harness changes

- Chord key: `key <K> <hold_ms> <K2> <at_ms>` (`cmdfile.cpp/.h`, `testrun.cpp` `c107_chord_step`, C63 `chord`).
  K2 goes down at_ms into K's hold and comes up 100 ms later, or at the hold's end.
  - Card premise corrected: runner keys are **not** async. The game-side queue runs one command at a time, and
    `key W 2000` finishes only after W is released. So "W 2000, then Space" always released W first, in c106c and
    c107a alike.
- `drive.py`:
  - `try <step>` turns a failure into `TRY-FAIL detail` and the scenario goes on.
  - `{gN}` / `{gN±k}` take groups from the last `wait log` match.
  - `wait near <x> <y> <z> <radius>` reads the state file's x y z.
- Scenario `tools/scenarios/c107-combined.txt` (C1M1).

### Rows

| Row | Run | Result |
|---|---|---|
| Drop, host | c107c | Pass. 2 death drops: `card107: host … runtime pickup BP_WeaponPickup_MoritaMk1_C at (221404, 35731, 8642)` and at (222130, 36075, 8680). Each sent a `world P … sent to 1 of 1 clients` row. |
| Drop, client | c107c | Pass. `copy of host 239446 … at (221404, 35731, 8642)` 3 ms later, and `copy of host 239603 … at (222130, 36075, 8680)` 2 ms later. Same locations. Client blocks: 0. The client's own death-drop spawns don't happen at all. |
| Mantle (3 tries) | c107b | **Fail**, 3 of 3. Each try ran `key W 2000 SpaceBar 1000`: the jump went at +1000 ms with W held, `GA_Mantling_Search_C` ran (`SearchForMantleLocation … mode 1`), and no lerp or mantle window opened. In every try the search ran from exactly the warp point (-503.1, 21795.9, 143.0): the pawn had not moved during 1 s of W. Afterwards the server copy ended at (-503.1, 22499.6, 142.9) in tries 1 and 2: it walked 704 uu +Y after the jump and stopped there. Corrections and end gap could not be read: the client's rot rows stop at rot[27], before the tries. Result fields: **not logged.** The Shipping PDB has publics only (TPI stream: 0 records), so the `USTAbilityTaskMantleSearch` layout needs disassembly of its Activate/tick. Mantle part stopped there, per the card. |
| Pod pickup | c107b | **Fail.** The client did reach the point: the client-side `warp actor point` gave `0 uu from the point`. Then the pod's `GA_ShuntAbilityCharacter_C` threw it back. That shunt starts on the host at the same ms as the pickup spawn (44619283) and ends 400 ms later. 230 ms after the warp the client was at (12719, 13308, 389), falling. `wait near` ended 443 uu off, E found no target, and there was no `DisablePickup`. Likely fix, untested: `sleep 1` after the 'P' row, so the warp lands after the shunt. |

### Runs

| Run | Map | Length | Host rc | Client rc | ERROR/FATAL | WARN | Dumps | Drive |
|---|---|---|---|---|---|---|---|---|
| c107a | C1M1 | 117 s | 0 | 0 | 0 / 0 | 0 / 0 | none | rc 0; pod, mantle ×3 TRY-FAIL; throw pass |
| c107b | C1M1 | 117 s | 0 | 0 | 0 / 0 | 0 / 0 | none | rc 0; pod, mantle ×3 TRY-FAIL; throw pass |
| c107c | C2M1 | 98 s | 0 | 0 | 0 / 0 | 0 / 0 | none | rc 0; 0 fails |

### Open

- Mantle: is the ledge at yaw 90 from (-503.1, 21795.9, 143) within the search's reach? The copy walked on to
  y 22499.6 and stopped there, so the wall may be about 700 uu further than the warp point assumed. Also, the pawn did
  not move during the first 1 s of W after the warp.
- Pod: is the shunt timing the only blocker? (Delay the warp past the shunt.)

Report complete.
