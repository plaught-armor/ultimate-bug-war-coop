# Card 006: Keep server-only encounter and spawn logic off the client

Read `docs/STATE.md` first: the "Mod code rules" section and the "Result after card 005" entry. Then read the report in `docs/cards/005-client-gamemode-and-mech-guards.md`, especially its section 7, which predicted this crash. Repo root: `/path/to/ultimate-bug-war-coop`.

## Why
Card 005 works: the client's deferred input path ran and added 4 contexts. The client then crashed on its main thread (thread 956), reading `0x28` at `UNavigationSystemV1::ProjectPointToNavigation+0x1C` with `this == null`.

Dump (read-only): `prefix/drive_c/users/steamuser/AppData/Local/StarshipTroopers_UBW/Saved/Crashes/UECC-Windows-5DBA4C3945ED6B2B59A163A869E17616_0000/UEMinidump.dmp`. Use `python3 -I tools/crash_stack.py <dmp> --depth 1400`; the default depth misses the upper frames.

Stack (from the senior's scan, call-preceded frames only):
```
AActor::ProcessEvent → Blueprint VM (ubergraph, execLocalVirtualFunction / execLocalFinalFunction)
 → ASTEncounterManagerBase::StartEncounter          RVA 0x05139A30
 → ASTEncounterManagerBase::ActivateCurrentWave     RVA 0x05115220
 → USTEncounterWaveBase::ActivateWave (virtual)      RVA 0x05115980
 → USTEncounterWaveBase::SpawnActor
 → ASTSpawnerBase::SpawnWithParameters              RVA 0x0516F660
 → ASTSpawnerBase::PerformSpawn                     RVA 0x051671F0
 → PerformSpawnAtPoint → ASTSpawnPoint::GetSpawnTransform → FindValidTransformForActorInBox
 → UNavigationSystemV1::ProjectPointToNavigation(this = null)   <- fault
```
The client is running encounter and enemy-spawn logic that only the server should run. Enemies should be spawned by the server and replicated to the client. Clients have no navigation system, which is the UE default. A Blueprint event, probably level or encounter BeginPlay, calls `StartEncounter` with no authority check. It works in single player because the only instance is the authority.

## Do
1. **Find the authority test.**
   - Locate `AActor::HasAuthority` / `GetLocalRole`, or the inlined `Role` field (`AActor::Role` byte offset), from the publics or from disassembly of a known `HasAuthority` user. Prove the offset, and check `ENetRole::ROLE_Authority == 3` in this build.
   - Encounter waves are `USTEncounterWaveBase` (a UObject, not an actor). The authority test needs its owning actor or the world's net mode instead: `UWorld::GetNetMode` or `UNetDriver` / `IsNetMode(NM_Client)`. Find what is callable and pure.
2. **Map the entry points.** For `StartEncounter`, `ActivateCurrentWave`, `ActivateWave`, `SpawnWithParameters` and `PerformSpawn`, say:
   - what each does;
   - what it returns (the `FSTSpawnResult` returns use a hidden pointer, so skipping them means you must construct a valid empty result: show its layout);
   - which side effects matter on the client, such as UI, objective text or encounter state the client HUD reads, and whether that state is replicated anyway.

   Find **every** caller reachable from Blueprint, including `exec*` thunks, so the gate is not bypassed by another entry.
3. **Gate at the narrowest layer that covers all client paths.** The likely candidates are `StartEncounter` (void, so a skip is simple) plus `ASTSpawnerBase::SpawnWithParameters` / `PerformSpawn` (because spawners may be triggered without an encounter). Justify the choice from step 2.
   - On a client (not authority), skip with capped `note_skip` logging. On the authority, or in single player (standalone), call the original unchanged.
   - The skip must not leak: show that nothing allocated before the skip point needs freeing, and that any returned struct is fully initialized.
   - If skipping `StartEncounter` on the client would break client-side state the HUD needs and that state is not replicated, say so in the report, but still gate. A crash-free client comes first. List the lost behaviour for card 007.
   - Hook order goes before Tick. Add the symbols to `tools/symbols.txt` and regenerate. Check the prologues for MinHook.
4. **Sweep for this class.**
   - Add a target set to `tools/find_lp_derefs.py`, for example `--targets nav`, covering `FNavigationSystem::GetCurrent`, `UNavigationSystemV1::GetCurrent` / `GetNavigationSystem` and any game-side nav-system getters you find. Report the unchecked game sites, as before.
   - Separately, list game `UFUNCTION`s that spawn actors (calls to `UWorld::SpawnActor` / `SpawnActorDeferred` / `ASTSpawnerBase::SpawnWithParameters`) and do not test authority or net mode before spawning. Rank them by how likely a client reaches them: BeginPlay, `OnRep_*`, multicast, overlap or trigger events. **Report only; don't gate them.**
5. **Build and prove.** Use the command in STATE.md. Show:
   - a clean `/W4 /WX` build;
   - 84 exports;
   - imports only from `KERNEL32.dll`;
   - the new RVAs, prologues and authority-offset evidence;
   - the trimmed sweep output.

   Deploy is the senior's job.

## Constraints
- Follow STATE.md's "Mod code rules": no heap use, every resource released, no stored game pointers, a skipped original must not leak, ≤60-line functions, bounded loops.
- Do not launch the game, and do not touch `game/` or `logs/`. Under `prefix/`, only read the crash dumps.
- No subagents. No commits. If a delete or other action is denied, say so in the report; don't ask the senior to do it for you.

## Report
(append below)

### Summary
Four new hooks gate the server-only encounter and spawn logic on the client: `ASTEncounterManagerBase::StartEncounter`, `ASTEncounterManagerBase::ActivateCurrentWave`, `ASTSpawnerBase::PerformSpawn` and `ASTSpawnerBase::QueueSpawnRequest`. Each one calls `AActor::InternalGetNetMode(this)` and skips the original only when that returns `NM_Client` (3). Standalone (0), dedicated server (1) and listen server (2) call the original unchanged. Each gate logs through a capped `note_skip` counter (`client encounter`, `client spawn`; 8 lines each, then one "further skips not logged" line). `--targets nav` was added to `tools/find_lp_derefs.py`. The build is clean. Nothing was deployed or launched.

### 1. Authority test
- **`AActor::Role` is at +0x168, and `ROLE_Authority == 3`.**
  - `AActor::execHasAuthority` (0x037FE4E0) does `cmp byte ptr [rcx+0x168], 0x3; sete al`.
  - `AActor::execGetLocalRole` (0x037FDC20) does `movzx eax, byte ptr [rcx+0x168]`.
  - `HasAuthority` itself is inlined everywhere. Its only public is the exec thunk.
- **Role is not used for the gate.** No game class is replicated (STATE: no `GetLifetimeReplicatedProps`). An actor that is not replicated, including a level-placed encounter manager or spawner, keeps `ROLE_Authority` on the client. That is standard UE behaviour, and it is how the client reached this code at all. A Role gate would therefore pass on the client.
- **Net mode is used instead.** `AActor::InternalGetNetMode` is at RVA 0x037EA080 (`?InternalGetNetMode@AActor@@AEBA?AW4ENetMode@@XZ`, private, called by address). It is pure:
  - it calls `GetWorld`;
  - if the actor's `NetDriverName` (+0x160) is `NAME_GameNetDriver`, it uses the world's NetDriver (World+0x38) and `UNetDriver::GetNetMode`; otherwise it uses `FindNamedNetDriver`;
  - it falls back to `UWorld::InternalGetNetMode`;
  - it returns 0 when there is no world.
- **The enum values come from `UNetDriver::GetNetMode`** (0x03E6B9D0): `IsServer()` (vtable +0x3E8) ? (`GIsClient` ? 2 : 1) : 3. So NM_Standalone=0, NM_DedicatedServer=1, NM_ListenServer=2 and **NM_Client=3**.
  - `UWorld::InternalGetNetMode` (0x04346750) gives the same values: 2 for a `listen` URL option and 3 while connecting. It builds and destroys a temporary FURL inside game code, so nothing leaks into the mod.
  - The detour masks the return to its low byte before comparing.
- Encounter waves (`USTEncounterWaveBase`) are UObjects, so no wave function is gated directly. Both gated owners, `ASTEncounterManagerBase` and `ASTSpawnerBase`, are AActors.

### 2. Entry points
| Function | RVA | Returns | What it does |
|---|---|---|---|
| `ASTEncounterManagerBase::StartEncounter` | 0x05139A30 | void | See below |
| `ASTEncounterManagerBase::ActivateCurrentWave` | 0x05115220 | void | See below |
| `ASTEncounterManagerBase::ActivateWave(int)` | 0x05115910 | void | Bounds check. Optional debug `HighlightEnemiesInActiveWave`. Sets +0x360, then calls `ActivateCurrentWave` |
| `USTEncounterWaveBase::ActivateWave` (virtual) | 0x05115980 | void | Broadcast; timers through `UWorld::GetTimerManager`/`InternalSetTimer`; `SpawnEnemyList`, `SpawnProceduralEnemyList`, `SpawnBossList` (each reaches `SpawnActor` → `SpawnWithParameters`) |
| `ASTSpawnerBase::SpawnWithParameters` | 0x0516F660 | `FSTSpawnResult` (hidden pointer) | If the delay (params+0x2C) ≤ 0, calls `PerformSpawn`; otherwise calls `QueueSpawnRequest` and builds a "queued" result |
| `ASTSpawnerBase::PerformSpawn` | 0x051671F0 | `FSTSpawnResult` (hidden pointer) | Validates, then `PerformSpawnAtPoint` → `GetSpawnTransform` → `FindValidTransformForActorInBox` → `ProjectPointToNavigation` (the crash) |
| `ASTSpawnerBase::QueueSpawnRequest` | 0x0516A850 | `FString` (hidden pointer) | Copies `FSTSpawnParameters` and adds it to the queue `TSet` |

**`StartEncounter`:**
- For each valid wave in the array at this+0x338 (count at +0x340), it binds or unbinds the manager's delegates on the wave's multicast lists.
- It resets the state: +0x34C=0, word +0x348=0 (finished and waiting-delay bytes), +0x360=0 (current wave).
- It broadcasts the delegate at +0x2E8 (OnEncounterStarted), calls `BP_OnEncounterStarted`, calls `ActivateCurrentWave`, and tail-jumps to `CheckHasBoundEvent`.

**`ActivateCurrentWave`:**
- It returns early if +0x348 is set or the index is invalid.
- It binds 5 wave delegates and copies the wave delay (wave+0x328) to +0x350.
- If the delay is positive, it arms the waiting flag (+0x349) for the manager Tick.
- Otherwise it calls wave vtable +0x2D8 (from the call shape, `ActivateWave`; not proven), then `BP_OnEncounterWaveStarted`, then this vtable +0x4A8 (likely `SetActorTickEnabled`; not proven).

**Callers.** These are all the E8/E9 references in the image, plus vtable entries.
- `StartEncounter`:
  - `execStartEncounter` (0x05055340, the Blueprint thunk);
  - the Blueprint VM (crash stack: `FLatentActionManager::TickLatentActionForObject` → `ProcessEvent` → ubergraph → `execLocalFinalFunction`, so a latent Delay in a level or encounter Blueprint).
- `ActivateCurrentWave`:
  - `StartEncounter`+0x6A7;
  - `ActivateWave(int)`+0x56;
  - `execActivateCurrentWave`+0x15;
  - `OnWaveEnd`+0x23B;
  - `ResumeEncounter`+0x10;
  - `TriggerNextWave`+0x1E.
  - The manager's `execActivateWave` (0x050538F0) reaches it through `ActivateWave(int)`.
- Wave `ActivateWave`:
  - vtable entries at .rdata 0x0804AB18 and 0x0804B0A0;
  - an ICF-folded exec thunk (0x039CA7C0) shared by three native tables.
- `SpawnWithParameters`:
  - `execSpawnWithParameters`+0xF6;
  - `ASTSpawnerBase::OnWorldBeginPlay`+0x3E (BeginPlay, so it runs on every instance);
  - `ASTSpawnerBase::Spawn`+0x10;
  - `USTAttackActionSpawnActor::Spawn`+0x10;
  - `USTEncounterWaveBase::SpawnActor`+0xA1;
  - `USTEncounterWaveDropship::TickTrySpawnActors`+0x18C;
  - `USTSpawnUtilityLibrary::Spawn`+0x1F4.
- `PerformSpawn`:
  - `SpawnWithParameters`+0x13;
  - `TickQueuedSpawnRequests`+0x3D9.
- `QueueSpawnRequest`:
  - `OnWorldBeginPlay`+0x70;
  - `SpawnWithDelay`+0x41;
  - `SpawnWithParameters`+0x5B;
  - `TickQueuedSpawnRequests`+0x412 (re-queue while a delay remains).

**`FSTSpawnResult` layout.** These are the writes in `PerformSpawn`'s own prologue (0x0516721B–0x05167254), with r12 = rdx = the result:
- +0x00 `FString` (data, num, max). It is assigned with `FString::AssignRange`.
- +0x10 `params+0x10` (a pointer, copied).
- +0x18 qword.
- +0x20 `TArray`, data pointer then num/max. `TickQueuedSpawnRequests` frees `result+0x20` with `FMemory::Free` after the call, and frees the +0x00 string later.
- +0x30 dword: three flag bytes, +0x30, +0x31 and +0x32.
- +0x34 qword.

The prologue zeroes 0x00–0x3B and then copies +0x10. The early-out path (`PerformSpawn`+0x70A, taken for empty params, a disabled spawner and similar cases) adds only `mov byte ptr [r12+0x32], 1` and returns r12. So +0x32 is the "failed / not spawned" flag. In the queued path, `SpawnWithParameters` writes 0x100 to +0x30, which sets +0x31.

**Client-side effects lost by the skip.** The game has no replication, so none of this reaches the client from the server either. These items are for card 007:
- the `OnEncounterStarted` broadcast (+0x2E8) and `BP_OnEncounterStarted`, possibly objective or HUD text driven from Blueprint;
- `BP_OnEncounterWaveStarted` and the per-wave delegates (wave end, enemy killed and so on), possibly a wave counter or music cues;
- the manager's wave index, finished flag and delay timer, which stay at their pre-start values on the client;
- the encounter's enemies themselves. The client sees none until the server's enemies are replicated (milestone 3);
- spawner results in Blueprint: `Spawn`/`SpawnWithParameters` now return a failed result on the client, so a Blueprint that reacts to a successful spawn takes its failure branch.

### 3. Gate choice and leak argument
- **`StartEncounter` and `ActivateCurrentWave`.** `ActivateCurrentWave` is the single function every wave start passes through: start, next wave, wave end, resume, `ActivateWave(int)` and both exec thunks. `StartEncounter` is gated as well, so the client does not bind wave delegates or broadcast "started" for an encounter that will never run. Both functions return void.
- **`PerformSpawn` and `QueueSpawnRequest`.** Together they cover every spawner path. `SpawnWithParameters` reaches one of the two. `OnWorldBeginPlay`, `SpawnWithDelay` and `TickQueuedSpawnRequests` call them directly. A spawn reached without an encounter (spawner BeginPlay, attack actions, Blueprint libraries) is therefore also stopped. A wave `ActivateWave` called directly from Blueprint (the folded exec thunk) still runs its timers and broadcasts, but its spawns end at the `PerformSpawn` gate. That is enough to prevent this crash. `SpawnWithParameters` itself is not hooked, because both of its outcomes are already gated.
- **No leak.** Every gate decides at entry, before the original runs any instruction. The only call before the decision is `InternalGetNetMode`, which is pure: its temporary FURL is created and destroyed inside game code. None of the four functions takes a by-value argument (`this`, a hidden return pointer, `const FSTSpawnParameters&`, and a float in xmm3). So a skip leaves nothing that the original would have freed.
- **Returned structs.**
  - The `PerformSpawn` skip writes exactly the bytes the original's early-out writes: it zeroes 0x00–0x3B, copies params+0x10 into +0x10, sets +0x32 = 1, and returns the result pointer. Both FString and TArray have a null data pointer, so the caller's frees are no-ops.
  - The `QueueSpawnRequest` skip zeroes the 16-byte `FString` (null data, 0/0) and returns the pointer. `SpawnWithParameters` and `TickQueuedSpawnRequests` then move or free an empty string.
- **No stored game pointers.** The detours keep only the `const` original-function pointers (written by MinHook before enable) and the counters, which are touched only on the game thread.
- **Hook order and MinHook.** The new entries sit before Tick in `kHooks`. None of the stolen bytes are RIP-relative or branches:
  - `StartEncounter`: `push rsi; push r12; push r13; sub rsp,0x80`.
  - `ActivateCurrentWave`: `push rbx; sub rsp,0x40; cmp byte [rcx+0x348],0`.
  - `PerformSpawn`: `push rbp; push rsi; push r12; push r13; push r14; push r15`. The first RIP-relative load is at +0x1A, well past the stolen bytes.
  - `QueueSpawnRequest`: `mov rax,rsp; mov [rax+8],rbx; mov [rax+0x10],rsi`.

### 4. Sweeps
**`--targets nav`** (`python3 -I tools/find_lp_derefs.py --targets nav`). The targets are `UNavigationSystemV1::GetCurrent(UWorld*)` and `UNavigationSystemV1::GetNavigationSystem(UObject*)`:
- `GetCurrent` reads `World+0x1A0` and does an IsA check.
- `FNavigationSystem::GetCurrent<T>` is inlined as the same load plus `Z_Construct_UClass_UNavigationSystemV1_NoRegister`. Those sites are listed as `INLINED` and are not classified, because the nav pointer is not in rax.
- The crash function `ASTSpawnPoint::FindValidTransformForActorInBox` appears in that list, which confirms the inlined pattern is the right signal.
```
ASTArenaZoneBase::GetPointAroundZone+0x15D  UNavigationSystemV1::GetCurrent  UNCHECKED  ; passed to UNavigationSystemV1::GetRandomReachablePointInRadius which derefs @rva 0x50EB1A4
USTEnvQueryTest_ProjectNPC::RunTest+0x2B9  UNavigationSystemV1::GetCurrent  UNSURE  ; window of 24 ran out
ASTNonPlayableCharacterBase::ChaseTarget+0x3D0                          INLINED
ASTSpawnPoint::FindValidTransformForActorInBox+0x1C2                     INLINED   (this crash)
ASTNonPlayableCharacterBase::GetNavData+0xF9                            INLINED
ASTCheckPointSaveZone::GetPathDistance+0x7F                             INLINED
USTGroundedMovementControl::GetRandomReachablePointInRadius+0x46        INLINED
USTAttackConditionTargetUnreachable::IsAttackActionValid+0x3E7          INLINED
ASTCompanionPointOfInterestZone::IsPointOfInterestLocationValid+0xAA    INLINED
ASTArenaBase::IsZoneReachableByRequester+0x89                           INLINED
USTMovementControlComponent::ProjectPointOnNavmesh+0x44                 INLINED
ASTNonPlayableCharacterBase::STTryGetNavmeshLocationForActorLocation+0x1A3  INLINED
USTFlyingMovementControl::TryCalculateFlyingNavmeshPath+0x193           INLINED
USTFlyingMovementControl::TryProjectPointToNavmesh+0xC3                 INLINED
ASTNonPlayableCharacterBase::CanReachMoveToActor+0x36  UNavigationSystemV1::GetNavigationSystem  CHECKED
--- E8 byte hits on targets: 109; not a decoded call: 0
--- all sites: 109  CHECKED=1  UNCHECKED=1  UNSURE=1  INLINED=106
--- game sites: 15  CHECKED=1  UNCHECKED=1  UNSURE=1  INLINED=12
```
Almost all of these are AI movement or arena code (NPC, bug, companion, flying, attack conditions). That code should not run on a client once AI is host-only, so the gate for that class belongs to milestone 3 (AI controller / possession on the server). `ASTCheckPointSaveZone::GetPathDistance` is the exception: checkpoints can run on both sides.

**Game functions that spawn without a net-mode or authority test (report only; nothing gated).** This is a direct-caller list for `UWorld::SpawnActor` (both overloads), `SpawnActorAbsolute`, `UGameplayStatics::BeginDeferredActorSpawnFromClass`, `UADActorPoolerBase::Spawn` and `ASTSpawnerBase::SpawnWithParameters`. A function counts as having a test only if its disassembly mentions `InternalGetNetMode`, `GetNetMode`, `IsNetMode`, `HasAuthority`, `GetLocalRole`, `IsServer` or a `+0x168` operand. The two `+0x168` hits (`USTSaveComponent::Load`, `USTTacticalSupportSlot::DeployFlare`) are unconfirmed and could be another struct's field. None of the others has any test. The ranking is by how likely a client reaches the function, judged from names and known event kinds, not traced:
1. `ASTSpawnerBase::OnWorldBeginPlay` (UFUNCTION, BeginPlay): runs on every instance. **Now covered** by the PerformSpawn/QueueSpawnRequest gate.
2. `UADWeaponInventory::SpawnWeaponInstance` (BeginPlay/possession): each instance spawns weapons for its own pawns. Once pawns replicate, expect duplicate weapon actors.
3. `ASTNonPlayableCharacterBase::SpawnDeathDrops_Internal` (death event): duplicate drops if the client simulates death locally.
4. `ASTDestroyableProp::AnimationDoAction_Implementation` (BlueprintNativeEvent, anim-driven): runs on both sides.
5. `ASTTacticalSupportPickup::PickupTacticalSupport` (overlap): the client spawns locally on overlap.
6. `USTTacticalSupportSlot::DeployFlare` and `ASTPlayerController::EjectPlayer` (player input): these need server RPCs, not a gate.
7. `USTWeaponUtilityLibrary::SpawnWeaponPickup` and `USTCombatUtilityLibrary::TrySpawnDamageZone` (Blueprint libraries, any event).
8. `USTAttackActionSpawnActor::Spawn` / `GenerateSpawner`, `USTEncounterWaveBase::SpawnActor`, `USTEncounterWaveDropship::TickTrySpawnActors`, `ASTSpawnerBase::Spawn`, `USTSpawnUtilityLibrary::Spawn` / `CreateSpawner` / `CreateSpawnPoint`: AI, encounter or spawner driven. Their spawner calls are now covered by the gate. `CreateSpawner`/`CreateSpawnPoint` still create the spawner actors themselves.
9. `ASTSaveableLevelScriptActor::TrySetQuietHellSecretEnabled` and `USTSaveComponent::Load` (level start, save restore): run on both sides.
10. `ASTLevelObjectiveMarker::CreateASTLevelObjectiveMarker` and `USTSaveSystemLibrary::GetOrCreateSaveEngine`: local UI and save state. Running these on the client is probably correct.
11. Pooler `Get` / `PreAllocate` / `Initialise` (`UADActorPoolerBase`, `USTActorPoolerBase`, `UADActorPoolerMultiplatformBase`): local pools, created at BeginPlay.
12. `ASTGameplayGameMode::BeginPlay` (server only by construction) and `USTCheatManager::CheatSpawn` (development only).

### 5. Build and proof
- The build used the STATE.md command, with `cmake --build build --clean-first`. There were **0 warnings** (`/W4 /WX`) and `build/dwmapi.dll` is 153600 bytes.
- `llvm-readobj --coff-exports`: **84 exports, 37 named**.
- `llvm-readobj --coff-imports`: **only `KERNEL32.dll`**.
- New `offsets.generated.h` rows:
```
0x05139A30  ASTEncounterManagerBase_StartEncounter       (hooked)
0x05115220  ASTEncounterManagerBase_ActivateCurrentWave  (hooked)
0x051671F0  ASTSpawnerBase_PerformSpawn                  (hooked)
0x0516A850  ASTSpawnerBase_QueueSpawnRequest             (hooked)
0x037EA080  AActor_InternalGetNetMode                    (called)
```
- Files changed:
  - `mod/src/hooks.cpp`: the gate block, 4 `kHooks` entries before Tick, and 1 `kCalls` entry;
  - `tools/symbols.txt`;
  - `mod/src/offsets.generated.h` (regenerated);
  - `tools/find_lp_derefs.py` (`--targets nav`, the `INLINED` verdict, and the docstring).

### 6. Risks and what to check at runtime
- **Expected client log:** `guard: ASTEncounterManagerBase::StartEncounter(...) skipped on client` once per encounter start, possibly `guard: ASTSpawnerBase::PerformSpawn/QueueSpawnRequest(...) skipped on client` from spawner BeginPlay, and no further crash on this stack.
- **Expected host log:** none of these lines on the host. Its net mode is 2, so the originals run.
- If the client log shows encounter skips but the host shows them too, net mode is reading wrong. In that case check World+0x38 and `NetDriverName` (actor +0x160).
- **Unverified** (not needed for the gate): wave vtable +0x2D8 = `ActivateWave` and manager vtable +0x4A8 = `SetActorTickEnabled`.
- The next likely client crashes are the nav sites above, reached through AI or arena code on client-side NPCs (for example level-placed companions or arena zones), and duplicate local spawns from the list in section 4.
- No action was denied. No subagents were used.

## Senior review (2026-11-02)
Accepted. The senior rebuilt the DLL (clean, 84 exports, imports only from `KERNEL32.dll`) and deployed it. In the runtime run the client joined and loaded the map, the card 005 deferred input path added 4 contexts, and `StartEncounter` was skipped on the client 8 times (8 log lines; the cap was reached at the eighth). The client did not crash. The `PerformSpawn`, `QueueSpawnRequest` and `ActivateCurrentWave` gates did not log, which is expected because `StartEncounter` now stops the chain above them. The next failure is on the host: `ASTPlayerController::Tick+0x122` calls `UPlayerInput::IsPressed` with a null `PlayerInput` on the remote PlayerController (dump `UECC-Windows-EFDA5B834146BC766DDF49864C53982E_0000`). Card 007 covers it.
