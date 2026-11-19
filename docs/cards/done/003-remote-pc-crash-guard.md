# Card 003: Guard the remote-PlayerController crash, plus a minidump symbolizer

Read `docs/STATE.md` first (especially the two-instance test result), then the senior reviews at the end of cards 001 and 002. Repo root: `/path/to/ultimate-bug-war-coop`.

## Why
In the card 002 runtime test the client connected to the listen host, and then the **host crashed**. When the server spawns a PlayerController for the remote client, `ASTPlayerController::BeginPlay` calls `GetAndApplyGameUserSettings`, which assumes the controller has a `ULocalPlayer`. A remote client's controller on the server has none. The result is `EXCEPTION_ACCESS_VIOLATION reading 0x148` inside `FSubsystemCollectionBase::GetSubsystemInternal`.

Symbolized stack from the senior's draft walker (innermost first):
`FindIndexByHash<TObjectPtr<const UWorld>>+0x18` ← `FSubsystemCollectionBase::GetSubsystemInternal` ← `USTGameUserSettings::GetSTGameUserSettings` / `USTPlayerUtilityLibrary::UpdateBindings` ← `ASTPlayerController::GetAndApplyGameUserSettings` ← `ASTPlayerController::BeginPlay` ← `DispatchBeginPlay` ← `PostActorConstruction` ← `FinishSpawning` ← `SpawnActor`.
The draft walker scans the stack for return addresses, so neighbouring frames can be stale. Treat it as a lead.

Evidence:
- Dump: `prefix/drive_c/users/steamuser/AppData/Local/StarshipTroopers_UBW/Saved/Crashes/UECC-Windows-D891BDFC4293DF9DE6AAC8B648417827_0000/UEMinidump.dmp` (read-only).
- Draft walker: `docs/mdwalk-senior-draft.py`, run as `python3 -I docs/mdwalk-senior-draft.py <dmp> <publics.txt> 1000`, where `publics.txt` is `llvm-pdbutil dump --publics` of the Shipping PDB.

## Already done (do not redo)
- Proxy, MinHook, log, env config, the travel hooks (cards 001–002). All work at runtime.
- These publics exist (exact decorated names):
  - `?GetAndApplyGameUserSettings@ASTPlayerController@@IEAAXXZ`: `void ASTPlayerController::GetAndApplyGameUserSettings()`
  - `?BeginPlay@ASTPlayerController@@MEAAXXZ`
  - `?GetLocalPlayer@APlayerController@@QEBAPEAVULocalPlayer@@XZ`: `ULocalPlayer* APlayerController::GetLocalPlayer() const`, non-virtual
  - `?IsLocalController@AController@@UEBA_NXZ`
  - `?UpdateBindings@USTPlayerUtilityLibrary@@SAXPEAVULocalPlayer@@PEBVUSTGameUserSettings@@@Z`: static `void UpdateBindings(ULocalPlayer*, const USTGameUserSettings*)`

## Do
1. **Pin the faulting call from the dump and the disassembly before writing the hook.**
   - Disassemble `GetAndApplyGameUserSettings` and list every call it makes.
   - Find which call leads into `GetSubsystemInternal` with a null-derived pointer. The bad address 0x148 is a field offset off null: identify the base register and where it was loaded from.
   - Disassemble `ASTPlayerController::BeginPlay` and list any **other** calls after `GetAndApplyGameUserSettings` that use `GetLocalPlayer()`, the `Player` field or LocalPlayer subsystems. These are the next likely crash sites. Report them; don't guard them yet unless the evidence is clear.
   - Find the callers of `GetAndApplyGameUserSettings` and of `UpdateBindings` (scan `.text` for `call rel32` to each RVA; a short script is fine). Report the counts and the caller symbols.
2. **Guard hook.** Choose the narrowest layer that the step 1 evidence supports, and justify it in the report:
   - (a) Hook `GetAndApplyGameUserSettings`. If `GetLocalPlayer(this)` is null, skip the original; otherwise call it.
   - (b) Hook `UpdateBindings`. If the `ULocalPlayer*` is null, skip; otherwise call the original.
   - If both are needed, do both.

   Requirements for whichever you choose:
   - Log skips, but never per frame and never unbounded: log the first 8 skips with the controller pointer, then one line `... further skips not logged`.
   - Keep the counter on the game thread only; there is no lock.
   - Install the hook before Tick, through the same table and checked path as card 002.
   - Add the symbols to `tools/symbols.txt` and regenerate `offsets.generated.h`.
   - Check each prologue for MinHook (at least 5 relocatable bytes, no jmp thunk).
3. **`tools/crash_stack.py`.** Turn the draft into a proper tool.
   - Arguments: `<dump> [--publics FILE | --pdb PDB]`. If `--publics` is not given, run `llvm-pdbutil dump --publics` itself, the same way `pdb_rva.py` does, and reuse its parsing if that's clean.
   - Read the module list from the minidump to get the Shipping exe's base address. Don't hard-code it.
   - Print: the exception code, the faulting address and the RIP symbol+offset, then the stack-scan candidates. Each candidate gets its stack offset, `symbol+off` and a marker `(call-preceded)` when the bytes before the return address are a `call` instruction. That filter cuts out stale frames. Keep the scan depth bounded (flag, default 1000 qwords).
   - It must run with `python3 -I`, standard library only.
   - Proof: run it on the dump above and paste the output into the report.
   - Delete `docs/mdwalk-senior-draft.py` once the tool replaces it.
4. Keep `/W4 /WX` clean. Each function stays ≤60 lines. Check every return value and log every failure.
5. Build and prove it: a clean build log, exports still identical (84), imports still only KERNEL32.dll, the new RVAs, and a prologue disassembly of each new target.

## Constraints
- Do not launch the game, and do not touch `game/` or `logs/`. Under `prefix/`, only read the crash dump. The senior runs the runtime test and deploys.
- No subagents. No commits.

## Report
(append below)

### Junior report (2026-11-02)

**Status:** done except one item. `docs/mdwalk-senior-draft.py` is **not deleted**: the `rm` was denied by the session's permission classifier. Senior: delete it by hand. `tools/crash_stack.py` replaces it.

#### 1. Faulting call, pinned
`GetAndApplyGameUserSettings` (RVA 0x051C6E60) makes these calls, in order:
- `GetSTGameUserSettings()`. The function returns early if this is null.
- **`GetLocalPlayer(this)` → `UpdateBindings(lp, settings)`**, at +0x32/+0x3D. This is gated only by a global bool (`mov rdx,[rip+…]; cmp byte [rdx],0`). There is **no null check on `lp`**.
- `PlayerInput` (`this+0x420`, null-checked): `FName` ctor, `UPlayerInput::GetInvertAxis` / `InvertAxis` ×2, `GetInvertMouseYAxis` / `GetInvertGamepadYAxis`.
- `this+0x7d8` (checked, plus the object-flags check) → `ASTPlayerCharacter::UpdateAbilitiesFromSettings`.
- `GetWorld` → `UGameplayStatics::GetGameInstance` → `GetSubsystem<UADInputDetectionSubsystem>` (GameInstance, not LocalPlayer) → stored at `this+0x8c8`, then `CurrentInputGamepad`.
- `this+0x2e8` (checked; presumably Pawn) → `GetSTGameUserSettings` → `GetComponentByClass<UADAbilitySystemComponent>` → `SetGameplayTagCount` ×2 (invulnerability, infinite ammo).

`UpdateBindings` (RVA 0x05255EB0) starts with `mov rsi,rdx; lea rdi,[rcx+0x138]` and then calls `GetSubsystemInternal(rcx=rdi, UEnhancedInputLocalPlayerSubsystem)`. `GetSubsystemInternal` does `mov rsi,rcx` and hashes into the map at `rsi+0x10`.
Dump registers at the fault: **`rsi=0x138`** and `rcx=0x140`, faulting read **0x148 = 0x138 + 0x10**. So the base is null `ULocalPlayer*` + 0x138 (its subsystem collection), loaded in UpdateBindings from its first argument, which is `GetLocalPlayer()` of the remote PC. `GetLocalPlayer` returns null when `Player` (`this+0x348`) is null or not a ULocalPlayer.

The tool's stack scan agrees, innermost first, all call-preceded: `GetSubsystemInternal+0xA5` ← `UpdateBindings+0x6F` ← `GetAndApplyGameUserSettings+0x42` ← `ASTPlayerController::BeginPlay+0x1E` ← `DispatchBeginPlay` ← `PostActorConstruction` ← `FinishSpawning` ← `SpawnActor` ← `SpawnPlayerControllerCommon` ← `SpawnPlayerController` ← `Login` ← `SpawnPlayActor` ← `UWorld::NotifyControlMessage`, which is the server Join path. The draft's `GetSTGameUserSettings+0x18` frame is stale: it is the return address of a call made earlier in GetAndApply.

`ASTPlayerController::BeginPlay` (0x051BE940) after GetAndApply: `GetSTGameUserSettings`, 3× `AddDynamic` on settings delegates (FName ctor + `wcsstr` of the function name), GameInstance `GetSubsystem<UADInputDetectionSubsystem>` (+0x8c8), a weak-ptr / `FindFunction` / delegate add, `USTCommonUtilityLibrary::GetDesignerSettings` (stored at +0x8d0) and `USTSaveSystemLibrary::GetSaveEngine` (delegate binds). **It makes no other call to `GetLocalPlayer`, reads no `Player` (+0x348), and touches no LocalPlayer subsystem.** I found no further crash sites in BeginPlay.

Callers (scan of `.text` for E8/E9 rel32):
- `UpdateBindings`: **1** site, `call` in `GetAndApplyGameUserSettings+0x3D`. It has no `exec` thunk (not BlueprintCallable).
- `GetAndApplyGameUserSettings`: **4** sites: `call` in `ASTPlayerController::BeginPlay+0x19`; `jmp` (tail call) in `execGetAndApplyGameUserSettings+0x15` (Blueprint/delegate path), `OnPostLoad+0x0` and `SetupInputComponent+0x3C7`.

#### 2. Guard: option (b), hook `UpdateBindings`
Why (b) and not (a):
- The fault is in UpdateBindings, and UpdateBindings has exactly one caller. Guarding it covers all four GetAndApply entry points, including the `exec` / settings-delegate path BeginPlay binds on every PC (a host settings change re-runs GetAndApply on the remote PC too).
- (a) would also skip the rest of GetAndApply for remote PCs: PlayerInput axis inversion (already null-checked), `UpdateAbilitiesFromSettings`, the input-detection subsystem and the ASC tags. Those are null-safe, so skipping them would change behaviour beyond the crash.
- Input bindings are LocalPlayer-only state, so a no-op is the correct result for a null LocalPlayer.

Implementation (`mod/src/hooks.cpp`): `detour_update_bindings`. A non-null LocalPlayer goes straight to the original. A null one logs `guard: UpdateBindings(LocalPlayer=null, settings=%p) skipped, caller exe+0x…` for the first 8 skips, then `guard: UpdateBindings ... further skips not logged` once, then nothing more (the counter saturates at 9). The counter is a plain `uint32_t` touched only on the game thread, with no lock.
- **Deviation from the card:** the card asks to log the controller pointer, but UpdateBindings does not receive the controller. I log the caller RVA (`_ReturnAddress() - exe base`) and the settings pointer instead. Expected caller: `exe+0x51C6EA2` (= GetAndApply+0x42).
- The hook is installed through the existing `kHooks` table (resolve_all → code_address checks → create_and_enable), before Tick, which stays last. `g_exe_base` is set in `install()` before `MH_Initialize`.
- `tools/symbols.txt`: added `USTPlayerUtilityLibrary_UpdateBindings`. Regenerated `offsets.generated.h` → RVA **0x05255EB0**. The other 7 RVAs are unchanged.

Prologue (MinHook needs ≥5 bytes):
```
145255eb0: 48 89 5c 24 08     mov qword ptr [rsp + 0x8], rbx
145255eb5: 48 89 74 24 10     mov qword ptr [rsp + 0x10], rsi
145255eba: 57                 push rdi
145255ebb: 48 83 ec 40        sub rsp, 0x40
```
The first instruction is exactly 5 bytes, with no RIP-relative operand and no jmp thunk. No internal branch targets bytes 0..4 (the earliest intra-function target is +0x3E).

#### 3. `tools/crash_stack.py`
- Usage: `python3 -I tools/crash_stack.py <dump> [--publics FILE | --pdb PDB] [--exe EXE] [--depth N]`, standard library only.
- It reuses `pdb_rva.py`'s `run`, `check_identity`, `RE_PUB` / `RE_ADDR` and the default paths. It imports via an explicit `sys.path` insert of `tools/`, because `-I` drops the script dir.
- It maps segments to VAs from the exe's own PE section headers, so `--publics` mode needs no PDB.
- Module base comes from the minidump module list (here `0x6FFFF37D0000`, not hard-coded).
- **Build identity:** the Proton/Wine dump's module record has an **empty CodeView record**. The tool compares GUID/age when a CodeView record is present; otherwise it falls back to PE TimeDateStamp + SizeOfImage (both match here). The CodeView branch is untested: this is the only dump and it has no CodeView record.
- `call-preceded` means the bytes before the return address decode as `E8 rel32`, or as `FF /2` with a full ModRM/SIB/disp length check and an optional REX prefix. It reads the bytes from the exe on disk.
- Memory: supports MemoryListStream and Memory64ListStream. Depth is bounded (default 1000, 1..1048576).
- Proof: `--pdb` (default) and `--publics` runs give byte-identical output. `--depth 0` is rejected with exit 1. Output on the card's dump:
```
build identity: dump module has no CodeView record; TimeDateStamp and SizeOfImage match
exe: StarshipTroopers_UBW-Win64-Shipping.exe base 0x6FFFF37D0000 size 0x9DDE000 (613521 function publics)
exception: code=0xC0000005 thread=360 at 0x6FFFF67B1F08
faulting address: 0x148 (reading)
RIP 0x6FFFF67B1F08 StarshipTroopers_UBW-Win64-Shipping.exe+0x2FE1F08 ??$FindIndexByHash@U?$TObjectPtr@$$CBVUWorld@@@@@?$TSet@U?$TTuple@U?$TObjectPtr@VUWorld@@@@PEAVFAnimationBudgetAllocator@@@@U?$TDefaultMapHashableKeyFuncs@U?$TObjectPtr@VUWorld@@@@PEAVFAnimationBudgetAllocator@@$0A@@@VFDefaultSetAllocator@@@@AEBAHIAEBU?$TObjectPtr@$$CBVUWorld@@@@@Z+0x18
  rax=0x00000000F0962D58  rcx=0x0000000000000140  rdx=0x0000000019192A21  rbx=0x00007FFFDE1115E0
  rsp=0x0000000000C9CDD0  rbp=0x0000000000C9CF20  rsi=0x0000000000000138  rdi=0x00007FFFDE1115E0
   r8=0x0000000000C9CE48   r9=0x00006FFFFB87C900  r10=0x0000000044ECFBB8  r11=0x0000000000C9CE38
  r12=0x0000000000000000  r13=0x0000000000000000  r14=0x00006FFFFC771400  r15=0x0000000066CD0150
stack scan from rsp, 1000 qwords max (dumped range ends at +0x3230):
  rsp+0x0018  StarshipTroopers_UBW-Win64-Shipping.exe+0x123CA9A ?Rebuild@FText@@AEBAXXZ+0x2A  (call-preceded)
  rsp+0x0038  StarshipTroopers_UBW-Win64-Shipping.exe+0x42A66C5 ?GetSubsystemInternal@FSubsystemCollectionBase@@IEBAPEAVUSubsystem@@PEAVUClass@@@Z+0xA5  (call-preceded)
  rsp+0x0068  StarshipTroopers_UBW-Win64-Shipping.exe+0x5255F1F ?UpdateBindings@USTPlayerUtilityLibrary@@SAXPEAVULocalPlayer@@PEBVUSTGameUserSettings@@@Z+0x6F  (call-preceded)
  rsp+0x0088  StarshipTroopers_UBW-Win64-Shipping.exe+0x5205CE8 ?GetSTGameUserSettings@USTGameUserSettings@@SAPEAV1@XZ+0x18  (call-preceded)
  rsp+0x0098  StarshipTroopers_UBW-Win64-Shipping.exe+0x4034858 ?BeginPlay@APlayerController@@MEAAXXZ+0x148  (call-preceded)
  rsp+0x00B8  StarshipTroopers_UBW-Win64-Shipping.exe+0x51C6EA2 ?GetAndApplyGameUserSettings@ASTPlayerController@@IEAAXXZ+0x42  (call-preceded)
  rsp+0x00E8  StarshipTroopers_UBW-Win64-Shipping.exe+0x51BE95E ?BeginPlay@ASTPlayerController@@MEAAXXZ+0x1E  (call-preceded)
  rsp+0x0138  StarshipTroopers_UBW-Win64-Shipping.exe+0x3EBEBD2 ?UpdateSubObjectGroupMemberships@FReplicationSystemUtil@Net@UE@@SAXPEBVAPlayerController@@@Z+0x12  (call-preceded)
  rsp+0x0148  StarshipTroopers_UBW-Win64-Shipping.exe+0x40348BE ?BeginReplication@APlayerController@@MEAAXXZ+0x1E  (call-preceded)
  rsp+0x0178  StarshipTroopers_UBW-Win64-Shipping.exe+0x37DD566 ?DispatchBeginPlay@AActor@@QEAAX_N@Z+0x96  (call-preceded)
  rsp+0x01A8  StarshipTroopers_UBW-Win64-Shipping.exe+0x37ECF8B ?PostActorConstruction@AActor@@QEAAXXZ+0x5EB  (call-preceded)
  rsp+0x01D8  StarshipTroopers_UBW-Win64-Shipping.exe+0x172CC0E ?OnPaint@SPanel@@UEBAHAEBVFPaintArgs@@AEBUFGeometry@@AEBVFSlateRect@@AEAVFSlateWindowElementList@@HAEBVFWidgetStyle@@_N@Z+0x1DE  (call-preceded)
  rsp+0x02C8  StarshipTroopers_UBW-Win64-Shipping.exe+0x37E2B41 ?FinishSpawning@AActor@@QEAAXAEBU?$TTransform@N@Math@UE@@_NPEBVFComponentInstanceDataCache@@W4ESpawnActorScaleMethod@@@Z+0x1681  (call-preceded)
  rsp+0x0428  StarshipTroopers_UBW-Win64-Shipping.exe+0x3DCB429 ?SpawnActor@UWorld@@QEAAPEAVAActor@@PEAVUClass@@PEBU?$TTransform@N@Math@UE@@AEBUFActorSpawnParameters@@@Z+0x10A9  (call-preceded)
  rsp+0x04F8  StarshipTroopers_UBW-Win64-Shipping.exe+0x1736258 ?Steal@?$TAttribute@U?$TVector2@N@Math@UE@@@@QEAA?AV?$TVariant@U?$TVector2@N@Math@UE@@V?$TDelegate@$$A6A?AU?$TVector2@N@Math@UE@@XZUFDefaultDelegateUserPolicy@@@@@@XZ+0x138  (call-preceded)
  rsp+0x06B8  StarshipTroopers_UBW-Win64-Shipping.exe+0x13079C3 ?IsParentPathOf@FPathViews@@SA_NV?$TStringView@_W@@0@Z+0x33  (call-preceded)
  rsp+0x0708  StarshipTroopers_UBW-Win64-Shipping.exe+0x13DD1BC ?TryFindClosestPath@FTreeNode@?$TDirectoryTree@PEAUFMountPoint@@@@QEAAPEAPEAUFMountPoint@@V?$TStringView@_W@@PEAV?$TStringBuilderBase@_W@@_W@Z+0x7C  (call-preceded)
  rsp+0x0798  StarshipTroopers_UBW-Win64-Shipping.exe+0x13DD27F ?TryFindClosestPath@FTreeNode@?$TDirectoryTree@PEAUFMountPoint@@@@QEAAPEAPEAUFMountPoint@@V?$TStringView@_W@@PEAV?$TStringBuilderBase@_W@@_W@Z+0x13F  (call-preceded)
  rsp+0x0868  StarshipTroopers_UBW-Win64-Shipping.exe+0x1277B22 ?Quaternion@?$TRotator@N@Math@UE@@QEBA?AU?$TQuat@N@23@XZ+0x132  (call-preceded)
  rsp+0x0938  StarshipTroopers_UBW-Win64-Shipping.exe+0x1277B22 ?Quaternion@?$TRotator@N@Math@UE@@QEBA?AU?$TQuat@N@23@XZ+0x132  (call-preceded)
  rsp+0x09C8  StarshipTroopers_UBW-Win64-Shipping.exe+0x3C6FC80 ?SpawnPlayerControllerCommon@AGameModeBase@@MEAAPEAVAPlayerController@@W4ENetRole@@AEBU?$TVector@N@Math@UE@@AEBU?$TRotator@N@56@V?$TSubclassOf@VAPlayerController@@@@@Z+0x140  (call-preceded)
  rsp+0x09D8  StarshipTroopers_UBW-Win64-Shipping.exe+0x118A693 ?ConstructFromPtrSize@FString@@SA?AV1@PEB_WH@Z+0x93  (call-preceded)
  rsp+0x09F8  StarshipTroopers_UBW-Win64-Shipping.exe+0x11D26FE ?Free@FMallocBinned2@@UEAAXPEAX@Z+0x2E  (call-preceded)
  rsp+0x0A78  StarshipTroopers_UBW-Win64-Shipping.exe+0x119F328 ?ResizeAllocation@ForAnyElementType@?$TSizedHeapAllocator@$0CA@UFMemory@@@@QEAAXHH_KI@Z+0x38  (call-preceded)
  rsp+0x0AF8  StarshipTroopers_UBW-Win64-Shipping.exe+0x11F036E ?Free@FMemory@@SAXPEAX@Z+0x2E  (call-preceded)
  rsp+0x0B08  StarshipTroopers_UBW-Win64-Shipping.exe+0x3C6FB40 ?SpawnPlayerControllerCommon@AGameModeBase@@MEAAPEAVAPlayerController@@W4ENetRole@@AEBU?$TVector@N@Math@UE@@AEBU?$TRotator@N@56@V?$TSubclassOf@VAPlayerController@@@@@Z+0x0
  rsp+0x0B28  StarshipTroopers_UBW-Win64-Shipping.exe+0x3C6F9EE ?SpawnPlayerController@AGameModeBase@@UEAAPEAVAPlayerController@@W4ENetRole@@AEBU?$TVector@N@Math@UE@@AEBU?$TRotator@N@56@@Z+0x5E  (call-preceded)
  rsp+0x0B78  StarshipTroopers_UBW-Win64-Shipping.exe+0x3C60D72 ?Login@AGameModeBase@@UEAAPEAVAPlayerController@@PEAVUPlayer@@W4ENetRole@@AEBVFString@@2AEBUFUniqueNetIdRepl@@AEAV5@@Z+0xD2  (call-preceded)
  rsp+0x0BD8  StarshipTroopers_UBW-Win64-Shipping.exe+0x3C60CA0 ?Login@AGameModeBase@@UEAAPEAVAPlayerController@@PEAVUPlayer@@W4ENetRole@@AEBVFString@@2AEBUFUniqueNetIdRepl@@AEAV5@@Z+0x0
  rsp+0x0BE8  StarshipTroopers_UBW-Win64-Shipping.exe+0x3DCC116 ?SpawnPlayActor@UWorld@@QEAAPEAVAPlayerController@@PEAVUPlayer@@W4ENetRole@@AEBUFURL@@AEBUFUniqueNetIdRepl@@AEAVFString@@E@Z+0x106  (call-preceded)
  rsp+0x0C78  StarshipTroopers_UBW-Win64-Shipping.exe+0x434AA9A ?NotifyControlMessage@UWorld@@UEAAXPEAVUNetConnection@@EAEAVFInBunch@@@Z+0xEEA  (call-preceded)
  rsp+0x0CD8  StarshipTroopers_UBW-Win64-Shipping.exe+0x1232347 ?GetTextRevisions@FTextLocalizationManager@@QEBAXAEBVFTextId@@AEAG1@Z+0x67  (call-preceded)
  rsp+0x0D58  StarshipTroopers_UBW-Win64-Shipping.exe+0x12304E7 ?GetLocalHistoryRevisionForText@FTextSnapshot@@CAGAEBVFText@@@Z+0x17  (call-preceded)
  rsp+0x0EF8  StarshipTroopers_UBW-Win64-Shipping.exe+0x19B477A ?OnPaint@SViewport@@UEBAHAEBVFPaintArgs@@AEBUFGeometry@@AEBVFSlateRect@@AEAVFSlateWindowElementList@@HAEBVFWidgetStyle@@_N@Z+0x7FA  (call-preceded)
  rsp+0x0FA8  StarshipTroopers_UBW-Win64-Shipping.exe+0x1708EFB ??$?BPEBVSWidget@@V?$TAttribute@VFText@@@@@?$TKeyInitializer@AEBQEBVSWidget@@@@QEBA?AU?$TTuple@PEBVSWidget@@V?$TAttribute@VFText@@@@@@XZ+0xE5B  (call-preceded)
  rsp+0x1018  StarshipTroopers_UBW-Win64-Shipping.exe+0x123CA9A ?Rebuild@FText@@AEBAXXZ+0x2A  (call-preceded)
  rsp+0x1048  StarshipTroopers_UBW-Win64-Shipping.exe+0x132B009 ??$EqualsSameDimensions@$00@@YA_NAEBUFNameEntry@@UFNameStringView@@@Z+0xC9  (call-preceded)
  rsp+0x10B8  StarshipTroopers_UBW-Win64-Shipping.exe+0x164D08D ?AddWidget@FHittestGrid@@QEAAXPEBVSWidget@@HHUFSlateInvalidationWidgetSortOrder@@@Z+0x58D  (call-preceded)
  rsp+0x10D8  StarshipTroopers_UBW-Win64-Shipping.exe+0x11D4AE4 ?Realloc@FMallocBinned2@@UEAAPEAXPEAX_KI@Z+0x54  (call-preceded)
  rsp+0x1128  StarshipTroopers_UBW-Win64-Shipping.exe+0x11D4AE4 ?Realloc@FMallocBinned2@@UEAAPEAXPEAX_KI@Z+0x54  (call-preceded)
  rsp+0x1148  StarshipTroopers_UBW-Win64-Shipping.exe+0x18475C2 ?GetFocusedWidget@FSlateUser@@QEBA?AV?$TSharedPtr@VSWidget@@$00@@XZ+0x62  (call-preceded)
  rsp+0x11A8  StarshipTroopers_UBW-Win64-Shipping.exe+0x1729F78 ?OnArrangeChildren@SPopupLayer@@EEBAXAEBUFGeometry@@AEAVFArrangedChildren@@@Z+0x9A8  (call-preceded)
  rsp+0x1238  StarshipTroopers_UBW-Win64-Shipping.exe+0x172E18F ?Paint@SWidget@@QEBAHAEBVFPaintArgs@@AEBUFGeometry@@AEBVFSlateRect@@AEAVFSlateWindowElementList@@HAEBVFWidgetStyle@@_N@Z+0x90F  (call-preceded)
  rsp+0x1278  StarshipTroopers_UBW-Win64-Shipping.exe+0x172E801 ?Prepass_ChildLoop@SWidget@@IEAAXMPEAVFChildren@@@Z+0xE1
  rsp+0x1288  StarshipTroopers_UBW-Win64-Shipping.exe+0x18106F0 ?ShowUserFocus@FSlateApplication@@MEBA_NV?$TSharedPtr@$$CBVSWidget@@$00@@@Z+0x0
  rsp+0x1318  StarshipTroopers_UBW-Win64-Shipping.exe+0x3B752F2 ?ReceivedBunch@UControlChannel@@UEAAXAEAVFInBunch@@@Z+0x892  (call-preceded)
  rsp+0x14C8  StarshipTroopers_UBW-Win64-Shipping.exe+0x16A6595 ?Init@FSlateDrawElement@@AEAAXAEAVFSlateWindowElementList@@W4EElementType@@IAEBUFPaintGeometry@@W4ESlateDrawEffect@@@Z+0xD5  (call-preceded)
  rsp+0x14F8  StarshipTroopers_UBW-Win64-Shipping.exe+0x16A703C ?MakeBoxInternal@FSlateDrawElement@@CAAEAV1@AEAVFSlateWindowElementList@@IAEBUFPaintGeometry@@PEBUFSlateBrush@@W4ESlateDrawEffect@@AEBUFLinearColor@@@Z+0x21C  (call-preceded)
  rsp+0x1538  StarshipTroopers_UBW-Win64-Shipping.exe+0x16ABAF2 ?SetUserFocus@FReply@@QEAAAEAV1@V?$TSharedRef@VSWidget@@$00@@W4EFocusCause@@_N@Z+0x3F2  (call-preceded)
  rsp+0x1578  StarshipTroopers_UBW-Win64-Shipping.exe+0x16A6DF7 ?MakeBox@FSlateDrawElement@@SAXAEAVFSlateWindowElementList@@IAEBUFPaintGeometry@@PEBUFSlateBrush@@W4ESlateDrawEffect@@AEBUFLinearColor@@@Z+0x67  (call-preceded)
  rsp+0x15B8  StarshipTroopers_UBW-Win64-Shipping.exe+0x172C7F9 ?OnPaint@SImage@@UEBAHAEBVFPaintArgs@@AEBUFGeometry@@AEBVFSlateRect@@AEAVFSlateWindowElementList@@HAEBVFWidgetStyle@@_N@Z+0x2F9  (call-preceded)
  rsp+0x1658  StarshipTroopers_UBW-Win64-Shipping.exe+0x1695536 ??R<lambda_1>@?1???$AddBoxElements@UFSlateBoxElement@@@FSlateElementBatcher@@AEAAXAEBV?$TArray@UFSlateBoxElement@@V?$TSizedDefaultAllocator@$0CA@@@@@@Z@QEBA@AEAVFSlateRenderBatch@@AEBUFSlateBoxElement@@@Z+0x4FD6  (call-preceded)
  rsp+0x17B8  StarshipTroopers_UBW-Win64-Shipping.exe+0x3B76D15 ?ReceivedNextBunch@UChannel@@AEAA_NAEAVFInBunch@@AEA_N@Z+0x855  (call-preceded)
  rsp+0x17C8  StarshipTroopers_UBW-Win64-Shipping.exe+0x11D4AE4 ?Realloc@FMallocBinned2@@UEAAPEAXPEAX_KI@Z+0x54  (call-preceded)
  rsp+0x1818  StarshipTroopers_UBW-Win64-Shipping.exe+0x119F328 ?ResizeAllocation@ForAnyElementType@?$TSizedHeapAllocator@$0CA@UFMemory@@@@QEAAXHH_KI@Z+0x38  (call-preceded)
  rsp+0x1858  StarshipTroopers_UBW-Win64-Shipping.exe+0x3B7787F ?ReceivedRawBunch@UChannel@@QEAAXAEAVFInBunch@@AEA_N@Z+0x1EF  (call-preceded)
  rsp+0x1888  StarshipTroopers_UBW-Win64-Shipping.exe+0x13247B6 ?SerializeBits@FBitReader@@UEAAXPEAX_J@Z+0xB6  (call-preceded)
  rsp+0x18C8  StarshipTroopers_UBW-Win64-Shipping.exe+0x3E66B76 ?DispatchPacket@UNetConnection@@UEAAXAEAUFBitReader@@HAEA_N1@Z+0x1636  (call-preceded)
  rsp+0x1A58  StarshipTroopers_UBW-Win64-Shipping.exe+0x132B009 ??$EqualsSameDimensions@$00@@YA_NAEBUFNameEntry@@UFNameStringView@@@Z+0xC9  (call-preceded)
  rsp+0x1A98  StarshipTroopers_UBW-Win64-Shipping.exe+0x133DAC5 ?Resolve@FLazyName@@QEBA?AVFName@@XZ+0x3D5  (call-preceded)
  rsp+0x1AC8  StarshipTroopers_UBW-Win64-Shipping.exe+0x133DAC5 ?Resolve@FLazyName@@QEBA?AVFName@@XZ+0x3D5  (call-preceded)
  rsp+0x1AD8  StarshipTroopers_UBW-Win64-Shipping.exe+0x133DAC5 ?Resolve@FLazyName@@QEBA?AVFName@@XZ+0x3D5  (call-preceded)
  rsp+0x1AF8  StarshipTroopers_UBW-Win64-Shipping.exe+0x133DAC5 ?Resolve@FLazyName@@QEBA?AVFName@@XZ+0x3D5  (call-preceded)
  rsp+0x1B28  StarshipTroopers_UBW-Win64-Shipping.exe+0x133DAC5 ?Resolve@FLazyName@@QEBA?AVFName@@XZ+0x3D5  (call-preceded)
  rsp+0x1B38  StarshipTroopers_UBW-Win64-Shipping.exe+0x133DAC5 ?Resolve@FLazyName@@QEBA?AVFName@@XZ+0x3D5  (call-preceded)
  rsp+0x1B48  StarshipTroopers_UBW-Win64-Shipping.exe+0x1929FA9 ?ComputeDesiredSize@SSafeZone@@UEBA?AU?$TVector2@N@Math@UE@@M@Z+0x179  (call-preceded)
  rsp+0x1B58  StarshipTroopers_UBW-Win64-Shipping.exe+0x133DAC5 ?Resolve@FLazyName@@QEBA?AVFName@@XZ+0x3D5  (call-preceded)
  rsp+0x1B68  StarshipTroopers_UBW-Win64-Shipping.exe+0x1626EFB ??$Concatenate@V?$TTransform2@M@@@@YA?AV?$TTransform2@M@@AEBV0@0@Z+0x10B  (call-preceded)
  rsp+0x1B78  StarshipTroopers_UBW-Win64-Shipping.exe+0x1626EFB ??$Concatenate@V?$TTransform2@M@@@@YA?AV?$TTransform2@M@@AEBV0@0@Z+0x10B  (call-preceded)
  rsp+0x1B98  StarshipTroopers_UBW-Win64-Shipping.exe+0x1626EFB ??$Concatenate@V?$TTransform2@M@@@@YA?AV?$TTransform2@M@@AEBV0@0@Z+0x10B  (call-preceded)
  rsp+0x1BB8  StarshipTroopers_UBW-Win64-Shipping.exe+0x1626EFB ??$Concatenate@V?$TTransform2@M@@@@YA?AV?$TTransform2@M@@AEBV0@0@Z+0x10B  (call-preceded)
  rsp+0x1BC8  StarshipTroopers_UBW-Win64-Shipping.exe+0x132B009 ??$EqualsSameDimensions@$00@@YA_NAEBUFNameEntry@@UFNameStringView@@@Z+0xC9  (call-preceded)
  rsp+0x1BD8  StarshipTroopers_UBW-Win64-Shipping.exe+0x11D4AE4 ?Realloc@FMallocBinned2@@UEAAPEAXPEAX_KI@Z+0x54  (call-preceded)
  rsp+0x1BE8  StarshipTroopers_UBW-Win64-Shipping.exe+0x11D4AE4 ?Realloc@FMallocBinned2@@UEAAPEAXPEAX_KI@Z+0x54  (call-preceded)
  rsp+0x1C38  StarshipTroopers_UBW-Win64-Shipping.exe+0x3E79A4E ?ReadPacketInfo@UNetConnection@@AEAA_NAEAUFBitReader@@_NW4Type@FEngineNetworkCustomVersion@@@Z+0x35E  (call-preceded)
  rsp+0x1CE8  StarshipTroopers_UBW-Win64-Shipping.exe+0x3E79C33 ?ReceivedPacket@UNetConnection@@UEAAXAEAUFBitReader@@_N1@Z+0x1D3  (call-preceded)
  rsp+0x1D98  StarshipTroopers_UBW-Win64-Shipping.exe+0x1325927 ?SetVersion@FCustomVersionContainer@@QEAAXUFGuid@@HVFName@@@Z+0x37  (call-preceded)
  rsp+0x1DB8  StarshipTroopers_UBW-Win64-Shipping.exe+0x11656F5 ??$ReallocGrow@$02VForAnyElementType@?$TSizedHeapAllocator@$0CA@UFMemory@@@@@Private@Core@UE@@YAHIIHAEAVForAnyElementType@?$TSizedHeapAllocator@$0CA@UFMemory@@@@AEAH1@Z+0xA5  (call-preceded)
  rsp+0x1DC8  StarshipTroopers_UBW-Win64-Shipping.exe+0x132E065 ??0FName@@QEAA@PEB_WW4EFindName@@@Z+0xB5  (call-preceded)
  rsp+0x1DD8  StarshipTroopers_UBW-Win64-Shipping.exe+0x1320E12 ?GetFriendlyName@FCustomVersion@@QEBA?BVFName@@XZ+0xA2  (call-preceded)
  rsp+0x1E48  StarshipTroopers_UBW-Win64-Shipping.exe+0x3E7A838 ?ReceivedRawPacket@UNetConnection@@UEAAXPEAXH@Z+0x428  (call-preceded)
  rsp+0x1E88  StarshipTroopers_UBW-Win64-Shipping.exe+0x19E1037 ?SetAnyIPv4Address@FInternetAddrBSD@@UEAAXXZ+0x47  (call-preceded)
  rsp+0x1F08  StarshipTroopers_UBW-Win64-Shipping.exe+0x19DD725 ?GetDefaultSocketProtocolFamily@FSocketSubsystemBSD@@UEBA?AVFName@@XZ+0x15  (call-preceded)
83 candidates
```
The filter is weak on this dump: 79 of 83 candidates are call-preceded. Most stale entries are real return addresses from earlier, deeper calls (Slate, FString, allocator), so `(call-preceded)` is necessary but not sufficient. Read the chain top-down until it stops making call-graph sense.

#### 4–5. Build proof
- Fresh build dir (scratchpad `b3`), same commands as STATE: **0 warnings** under `/W4 /WX`.
- Exports: **84**, `diff` against the previous `build/dwmapi.dll` export list is identical. Imports: **KERNEL32.dll only**.
- `build/dwmapi.dll` was **not** rebuilt and nothing was deployed. Senior: rebuild into `build/` (or copy) before the runtime test.
- New functions are all ≤60 lines; every struct read in the tool goes through `u()`, which dies on truncation.

#### For senior / next likely issues
- With the guard, GetAndApply on a remote PC still runs the non-LocalPlayer parts with the **host's** local settings: invulnerability and infinite-ammo gameplay tags on the remote player's pawn ASC (`this+0x2e8`), and `UpdateAbilitiesFromSettings` on `this+0x7d8`. At BeginPlay those are probably null, but host settings changes re-fire via the bound delegates. This is a gameplay-correctness issue for later, not a crash.
- The global bool gating the UpdateBindings call (`[rip+…]` → `0x14943b500`) is unidentified.
- The runtime proof expects one `guard: … skipped, caller exe+0x51C6EA2` line on the host when the client joins, and then whatever crashes next.
