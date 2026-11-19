# Card 005: Client GameMode-null crash in SetupInputComponent, mech input guards, and an authority-getter sweep

Read `docs/STATE.md` first: the "Mod code rules" section and the "Result after card 004" entry. Then read the report in `docs/cards/004-setplayer-guard-and-sweep.md`, which has the SetPlayer map and the `find_lp_derefs.py` sweep output. Repo root: `/path/to/ultimate-bug-war-coop`.

## Why
After card 004, **the host survives the client joining**. The **client** now crashes on its own join path. The crash happens on the client's main thread (thread 884), with `EXCEPTION_ACCESS_VIOLATION` reading `0x10`:

```
UNetConnection::HandleClientPlayer
 → ASTPlayerController::SetPlayer        (original path: the client's player is a ULocalPlayer)
 → APlayerController::SetPlayer
 → APlayerController::InitInputSystem
 → ASTPlayerController::SetupInputComponent+0x63   <- fault
```

Dump (read-only): `prefix/drive_c/users/steamuser/AppData/Local/StarshipTroopers_UBW/Saved/Crashes/UECC-Windows-4BAAF5BF4FF864BD155EC8BED1B5B9D5_0000/UEMinidump.dmp`. Run `python3 -I tools/crash_stack.py` on it.

Here is what the senior read in `ASTPlayerController::SetupInputComponent` (VA 0x1451DC4A0, RVA 0x051DC4A0):
- It calls Super `APlayerController::SetupInputComponent`.
- It calls `GetLocalPlayer`, which is null-checked, then `GetSubsystem<UEnhancedInputLocalPlayerSubsystem>` (kept in r12), which is also null-checked.
- It calls `UGameplayStatics::GetGameMode(this)` (VA 0x143C50600), then `mov rbx,[rax+0x10]` **with no null check**. GameMode exists only on the server, so this read faults on every client.
- rbx (`GameMode->ClassPrivate`) is hashed and used as the key into a TMap at `this+0x838` (stride 0x38). The senior believes that map selects the input config for each game-mode class.

This is a new bug class: **code that runs on the client assumes an authority-only object exists.** Card 004 found the mirror case, server code that assumes a local player.

Card 004's sweep also found that `ASTPlayerMechBase::AddMechInputContext` and `RemoveMechInputContext` dereference the LocalPlayer subsystem (r15) without a check, through `lea rcx,[r15+0x30]; call [rax+0x98]` and `[rax+0xA0]`. Their callers are `OnEmbarkingComplete`, `ActorFullyLoaded` and `LeaveMech`. These will crash the server for a remote controller as soon as a mech is entered or left.

## Do
1. **Map SetupInputComponent completely,** the way card 004 mapped SetPlayer. Cover the start to the end, and above all everything after the GameMode lookup:
   - what the `this+0x838` TMap holds: key type, value type, and what the value is then used for;
   - what happens when the lookup misses;
   - which calls bind input that the client needs to play. Name the calls.

   Find where `this+0x838` is populated. It is probably a UPROPERTY TMap from the Blueprint CDO; say which property it is if the PDB or reflection data names it.
2. **Fix the client crash.** Pick the narrowest correct design and justify it from step 1. Candidates:
   - **(a)** Hook `UGameplayStatics::GetGameMode` **only if** you can show it has few callers and that a substitute is safe for all of them. This is probably wrong, because every authority-only caller would then get a non-null object.
   - **(b)** Detour `SetupInputComponent`. If GameMode is null, supply the key some other way, for example `AGameStateBase::GameModeClass`, which is replicated and present on clients. Find its offset from the PDB or disassembly and prove that it equals what `GameMode->ClassPrivate` would be on the host. Then run the rest of the function's work. If that requires reimplementing the tail, keep the reimplementation small and exact.
   - **(c)** A narrow code patch at +0x63, for example redirecting a null `rax` to the miss path. Justify that the miss path still binds the input the client needs; if it doesn't, (c) is wrong.

   Whatever you choose: the client must still get its input bindings, Super must run exactly once, and the host path must stay byte-for-byte unchanged in behavior. Logging uses the capped `note_skip` pattern. Add symbols to `tools/symbols.txt` and regenerate. Check prologues for MinHook. Hook order goes before Tick.
3. **Guard `AddMechInputContext` and `RemoveMechInputContext`.** Map both. If everything they do is local-only (input mapping contexts), hook them and skip when the controller is not local, using the same `GetLocalPlayer`-based test as card 004 (StaticClass + `FObjectPtr::IsA`, or `GetLocalPlayer` directly if that is pure). If either function has server-relevant work, say so and guard only the unchecked dereference. Show that skipping leaks nothing.
4. **Sweep authority-only getters.** Extend `tools/find_lp_derefs.py`, or add a sibling tool if that is cleaner (justify it), to cover calls to:
   - `UGameplayStatics::GetGameMode`;
   - `UWorld::GetAuthGameMode` if it is not inlined (say if it is);
   - any other public that returns a GameMode or another server-only object you find, such as GameSession or `AGameModeBase`-derived getters.

   Use the same output format and CHECKED / UNCHECKED / UNSURE rules as card 004. From the UNCHECKED **game** sites, name the ones reachable on a **client**: input setup, BeginPlay, `OnRep_*`, `Client*` RPCs, HUD/UI, and the possession and restart paths. Rank them by likelihood. **Do not guard them yet,** except for the SetupInputComponent site this card fixes.
5. **Build and prove.** Use the command in STATE.md. Show:
   - a clean `/W4 /WX` build;
   - 84 exports;
   - imports only from `KERNEL32.dll`;
   - the new RVAs and prologue disassembly;
   - the sweep output, trimmed to game rows plus counts.

   Deploy is the senior's job: do not copy into `game/`.

## Constraints
- Follow STATE.md's "Mod code rules": no heap use, every resource released, no stored game pointers, a skipped original must not leak, ≤60-line functions, bounded loops.
- Do not launch the game, and do not touch `game/` or `logs/`. Under `prefix/`, only read the crash dumps.
- No subagents. No commits. If a delete or other action is denied, say so in the report; don't ask the senior to do it for you.

## Report
(append below)

### Report (junior, 2026-11-02)

**Result:** the client crash is fixed by a narrow `UGameplayStatics::GetGameMode` hook. It changes the result only when the call returns to `SetupInputComponent+0x5C` and the original result is null. AddMechInputContext and RemoveMechInputContext are guarded. `find_lp_derefs.py` gained `--targets authority`. The build is clean. The deferred-context path has **not been runtime-tested**. Nothing was deployed, and `game/`, `logs/` and `prefix/` were not touched; I only read the crash dump under `prefix/`. No subagents, no commits.

#### 1. Crash confirmed
`crash_stack.py` on the dump: AV read 0x10 at exe+0x51DC503 (`SetupInputComponent+0x63`), rax=0, thread 884. Stack: `OnActorChannelOpen` → `HandleClientPlayer+0x23A` → `ASTPlayerController::SetPlayer+0x18` → `APlayerController::SetPlayer+0x10D` → `InitInputSystem+0x156` → fault. This path runs when the PC's actor channel opens, so the GameState may not have replicated yet. The fix has to handle that case.

#### 2. SetupInputComponent map (RVA 0x51DC4A0..0x51DC870)
| Offset | What |
|---|---|
| +0x15 | Super `APlayerController::SetupInputComponent` |
| +0x1D | `GetLocalPlayer`; null → +0x36A |
| +0x36 | `GetSubsystem<UEnhancedInputLocalPlayerSubsystem>` → r12; null → +0x362 |
| +0x47..+0x51 | options byte = 0x4 (bNotifyUserSettings) |
| +0x57 | `call UGameplayStatics::GetGameMode` (return address +0x5C = `movzx ecx, byte [rip+d]`, the GC barrier flag `GIsIncrementalReachabilityPending`) |
| +0x63 | `mov rbx,[rax+0x10]`: **fault**. rax is not used again after this. |
| next | If the flag is set: `MarkAsReachable(rbx)`. Murmur hash of ptr>>4, then `TSet::FindIndexByHash` (0x51B6D40) on this+0x838. A miss or null element → +0x342. |
| hit | `FSoftObjectPtr::LoadSynchronous` on the value. Inline IsA `UPlayerMappableInputConfig`. Iterate config+0x58 Contexts (stride 0x18: ctx @0, int32 priority @8). For each: GC barrier, then `iface(r12+0x30)->AddMappingContext(ctx, prio, opts=0x4)` (vtable +0x98). **Original bug:** a null load or failed IsA still reads config+0x80 and faults. |
| +0x342 | opts = 0x3 (IgnoreAllPressedKeysUntilRelease \| ForceImmediately); `iface->RequestRebuildControlMappings(&opts, Rebuild)` (vtable +0xD8). Runs on hit and on miss. |
| +0x36A | Always runs, even without an LP: `LoadSynchronous(this+0x888)` (TSoftObjectPtr<UADInputConfigData>), IsA `UADInputConfigData`, `TSoftObjectPtr::Get`, **`SetupInputForConfig`** (0x51DC870; binds the input actions), then tail-jmp to **`GetAndApplyGameUserSettings`** (0x51C6E60). |

**this+0x838** is the UPROPERTY **`StartingPlayerInputMappings`**: `TMap<TSubclassOf<AGameModeBase>, TSoftObjectPtr<UPlayerMappableInputConfig>>`. Source: UHT statics `NewProp_StartingPlayerInputMappings` (FMapPropertyParams, NameUTF8 "StartingPlayerInputMappings", Offset 0x838, flags 0x17 Map). The key is a ClassProperty with metaclass `AGameModeBase`; the value is a SoftObject of `UPlayerMappableInputConfig`. The C++ constructor (0x51B9190) only zero-inits it, so it is filled from the BP CDO. The value selects the input mapping contexts (IMCs) for each game-mode class.

**Miss path:** only the IMCs are skipped. Rebuild, `SetupInputForConfig` (action bindings) and the user settings still run. But the bindings are useless without mapping contexts: no key reaches an action.

#### 3. Design: A′ (narrow GetGameMode hook plus deferral), not (b) and not (c)
- **(c) alone is wrong.** The miss path binds actions but adds no mapping contexts, so the client has no working keys.
- **(b) needs a reimplemented tail** (hash lookup, LoadSynchronous, sparse iteration, barrier, Super ordering) or a mid-function patch. Either is more code than A′, and Super-exactly-once would be harder to guarantee.
- **(a) as the card warns against is avoided.** The hook changes the result only when it is null **and** `_ReturnAddress()` equals `SetupInputComponent+0x5C`. Every other caller, and the host (where GameMode is non-null), gets the original result.
- **Which key the fake GameMode carries.** On that one path the hook returns a static `FakeGameMode` (16 pad bytes, then the key at +0x10; `static_assert`). The original then runs unmodified: Super once, lookup, contexts, rebuild, bindings.
  - **GameState already replicated:** the key is `World(+0x1B0 GameState)+0x2A8` = `AGameStateBase::GameModeClass`. It equals the host's `GameMode->ClassPrivate` because `AGameModeBase::InitGameState` (0x3C5A100) stores `[GameState+0x2A8] = GameMode->ClassPrivate`, and the property replicates (`OnRep_GameModeClass`, 0x3C62420).
  - **GameState not there yet** (the crash shows this can happen): the key is the PC's own class. It is a real UObject, so the barrier is safe, and it is never an AGameModeBase subclass, so the lookup misses and the contexts are deferred. `OnRep_GameModeClass` → `AGameStateBase/AGameState::ReceivedGameModeClass` (0x3C67D60 / 0x3C67CA0) calls PC vtable slot **0xEC0** (`APlayerController::ReceivedGameModeClass`) on every PC in the world.
- **The deferred apply.** In both the ASTPlayerController and APlayerController vtables, slot 0xEC0 is 0x1162D20: an ICF-folded `ret` (`rand_pool_cleanup`), so it can't be MinHooked. `arm_received_game_mode_class` patches the **ASTPC vtable slot** in .rdata. Each step is checked:
  - the slot is in a readable, non-executable exe section;
  - it holds the stub, or already holds the detour;
  - `VirtualProtect` to RW, store, `VirtualProtect` back.
  The detour calls the stub, then applies once if it is pending, the class is non-null, GetGameMode is null and an LP exists. The apply looks up the class in StartingPlayerInputMappings, AddMappingContexts each context (opts 0x4, with the same barrier) and requests the same Rebuild (opts 0x3).
  - AddMappingContext is refcounted, so a double add would leak a registration. The pending flag prevents it: it is cleared on apply, on a later non-deferred SetupInputComponent, and in `detour_load_map`, so a deferral never outlives its map.
  - A null-config or wrong-class load logs an ERROR and skips, where the original would fault.
- **Install-time validation.** `resolve_setup_input_sites` checks that +0x57 is `E8` targeting GetGameMode and that +0x5C is `0F B6 0D`. It derives the return address and the barrier-flag address from those bytes. On a mismatch it logs FATAL and installs nothing.
- **Logging:** capped `note_skip(g_client_input_logs, "client-input")`, plus one line per deferred apply and per vtable arm.

#### 4. AddMechInputContext and RemoveMechInputContext (0x50DB280 / 0x50FE920)
Both functions do the same steps:
1. `FWeakObjectPtr::Get(this+0xE30)` gives the PC. It is passed to `GetLocalPlayer` **without a null check**, which faults on a null PC.
2. LP non-null → `GetSubsystem` → r15; otherwise r15 = 0.
3. `LoadSynchronous(this+0xE60)`: the mech's PMIC, with the same null-config bug as above.
4. For each context: barrier, then `[r15+0x30]` vtable +0x98 (Add: priority 3, opts 0x4) or +0xA0 (Remove).

Everything they do is local input state; there is no server-relevant work. Each hook skips the original unless the weak PC is non-null, GetLocalPlayer is non-null and the subsystem is non-null; skips are logged through capped `note_skip`.
- **No leak:** the skipped original would only have loaded a GC-owned asset and called the subsystem.
- **Purity:** the guard calls only pure getters: weak Get, GetLocalPlayer (Player at +0x348 plus FObjectPtr::IsA), and GetSubsystem.

#### 5. New RVAs (`tools/symbols.txt` → `offsets.generated.h`)
| Symbol | RVA |
|---|---|
| UGameplayStatics_GetGameMode | 0x03C50600 |
| ASTPlayerController_SetupInputComponent | 0x051DC4A0 |
| AActor_GetWorld | 0x037E78F0 |
| APlayerController_GetLocalPlayer | 0x04047B20 |
| ULocalPlayer_GetEnhancedInputSubsystem | 0x04461C80 |
| FSoftObjectPtr_LoadSynchronous | 0x01516AE0 |
| UPlayerMappableInputConfig_StaticClass | 0x04459C40 |
| GC_MarkAsReachable | 0x01459BF0 |
| APlayerController_ReceivedGameModeClass_Folded | 0x01162D20 |
| ASTPlayerMechBase_AddMechInputContext | 0x050DB280 |
| ASTPlayerMechBase_RemoveMechInputContext | 0x050FE920 |
| FWeakObjectPtr_Get | 0x015859A0 |

**Prologues of the hooked functions.** None of the stolen bytes contains a RIP-relative or branch instruction.
```
GetGameMode           +0 sub rsp,0x28 / +4 mov rdx,rcx / +7 mov r8d,1          (MinHook copies 7 bytes)
AddMechInputContext   +0 mov [rsp+0x10],rbx / +5 mov [rsp+0x18],rsi / ...      (5 bytes)
RemoveMechInputContext+0 mov [rsp+0x10],rbx / +5 mov [rsp+0x18],rsi / ...      (5 bytes)
```
Hook order: GetGameMode, AddMech and RemoveMech are inserted before Tick, and Tick stays last.

#### 6. Sweep: `find_lp_derefs.py --targets authority`
I extended the existing tool rather than adding a sibling. The walker, classifier, call-site scan and report are the same; only the target set differs. A sibling would duplicate about 300 lines. `--targets lp` is still the default, and its output matches card 004 (111 sites; game 18: 13 / 3 / 2).

**Targets:**
- `UGameplayStatics::GetGameMode`.
- `USTCommonUtilityLibrary::GetSTGameplayGameMode`: GetGameMode plus a class check; it is null-safe itself.

**Not covered:**
- `UWorld::GetAuthGameMode` has **no public; it is inlined** as a load of World+0x1A8.
- `AGameModeBase::GameSession` is a plain field.
- Neither is a call, so the sweep can't see them, and the same goes for any direct World+0x1A8 read in game code.
- `CreateGameModeForURL` is a server-only factory, not a getter.
- Blueprint `GetGameMode` nodes return null into the BP VM ("Accessed None"), which does not crash.

```
ASTPlayerController::SetupInputComponent+0x57  UGameplayStatics::GetGameMode  UNCHECKED  ; mov rbx, qword ptr [rax + 0x10] @rva 0x51DC503
ASTPerformanceBenchmarkTool::DoPerfTestBeginSampling+0x55  USTCommonUtilityLibrary::GetSTGameplayGameMode  UNSURE  ; value no longer in a register
ASTEncounterManagerBase::HighlightEnemiesInActiveWave+0x2D  USTCommonUtilityLibrary::GetSTGameplayGameMode  UNSURE  ; window of 24 ran out
USTCommonUtilityLibrary::execGetSTGameplayGameMode+0x6D  USTCommonUtilityLibrary::GetSTGameplayGameMode  UNSURE  ; returned
(37 CHECKED rows omitted: every one is `test rax,rax; je` right after the call)
--- E8 byte hits on targets: 41; not a decoded call: 0; GetSubsystemInternal on non-LocalPlayer collections: 0
--- all sites: 41  CHECKED=37  UNCHECKED=1  UNSURE=3
--- game sites: 41  CHECKED=37  UNCHECKED=1  UNSURE=3
```
I resolved the three UNSURE rows by hand:
- **DoPerfTestBeginSampling+0x55:** the result is discarded; rax is overwritten at +0x5A. **Safe.**
- **execGetSTGameplayGameMode:** the BP thunk returns the value to the VM. **Safe.**
- **HighlightEnemiesInActiveWave+0x2D:** the result is kept in r15. For each enemy GUID in the wave it is passed as `this` to `ASTGameplayGameMode::GetActorByGUID`, whose first instruction reads `[rcx+0x43C]`, with no null check. **Effectively UNCHECKED.** Callers are `ASTEncounterManagerBase::ActivateWave+0x48` and `ToggleAllDebugEnemyHighlights+0x76`.

#### 7. Client-reachable UNCHECKED game sites, ranked (not guarded, except #1)
1. `ASTPlayerController::SetupInputComponent+0x57`: every client join. **Fixed by this card.**
2. `ASTEncounterManagerBase::HighlightEnemiesInActiveWave+0x2D` via `ActivateWave`: faults on a client if ActivateWave runs there (for example from replication or a multicast) and the wave has at least one enemy GUID. Whether it runs on the client is unconfirmed and needs a runtime check. Encounter and wave logic is probably authority-side, but nothing in the disassembly shows a `HasAuthority` gate.
3. Inlined `GetAuthGameMode` (World+0x1A8) and direct `GameSession` reads in game code are not swept. This is a known blind spot; a byte scan for `[reg+0x1A8]` loads would be too noisy to be useful without a world-typed dataflow.

All other GetGameMode and GetSTGameplayGameMode sites are CHECKED.

#### 8. Build proof
- I ran the STATE.md command (`gen_proxy_exports.py`, `pdb_rva.py gen`, configure, build) into `build/`, then did `cmake --build build --clean-first`: **0 warnings** under `/W4 /WX`.
- `build/dwmapi.dll` is 153088 bytes. It has 84 exports (37 named) and imports only `KERNEL32.dll`.
- The longest function is `install` at 42 lines; the limit is 60.
- **Not deployed:** copying into `game/` is the senior's job.

#### 9. Self-review: what I attacked, and the residual risks
**Attacked:**
- **GameState present but GameModeClass still null:** the hook takes the deferred path, and the detour ignores a null class while staying pending.
- **ReceivedGameModeClass before SetupInputComponent:** SetupInputComponent then reads the class directly and nothing is pending.
- **Travel with a pending deferral:** cleared in `LoadMap`.
- **Host:** GetGameMode is non-null, so every path is unchanged. The vtable slot is never armed, and the detour also re-checks `GetGameMode(pc)`.
- **Mech guard on the host's own mech:** passes through to the original.
- **`sparse_view` with more than 128 bits and a null secondary pointer:** the inline bit words would be read out of bounds. Found in this review and now rejected.
- **`in_exe_rdata`:** requires the whole 8-byte slot to lie inside the section.

**Residual risks:**
1. The deferred path has not run, because I could not launch the game.
2. The vtable patch writes to .rdata, shared by every ASTPlayerController. It is idempotent and validated against the expected stub.
3. The pending flag is a single global, which is correct for one local PC per process (no split-screen).
4. The original's null-config fault in the hit path is still present on the non-deferred path. That is game behaviour, and it fires only if the BP asset is broken.
5. Other GetGameMode callers are unchanged by design.

### Senior review (2026-11-02): accepted

- **Checked against the disassembly:**
  - `rax` from GetGameMode is used only by the `+0x63` load. `rbx` (the key) then goes to `MarkAsReachable` and into the hash, so a real UClass as the fake key is safe.
  - `AGameStateBase::ReceivedGameModeClass` and `AGameState::ReceivedGameModeClass` call PC vtable `+0xEC0` with `rdx = &stack copy`, so the detour's `const void* const*` signature is correct.
- **Rebuilt it myself:** clean, 84 exports, only `KERNEL32.dll` imported. Deployed.
- **Runtime (two windowed instances):**
  - The deferred path ran on the client: slot armed, key was the PC class (a miss), then `ReceivedGameModeClass` found the mapping and added 4 contexts.
  - The host guards fired as before; the user closed the host.
  - The client then crashed in server-only encounter spawn logic (the `ActivateWave` path that section 7 ranked #2). That is card 006.
- **Not yet observed:** the mech guards, and whether client input actually works in-game. Both need a client that survives.
