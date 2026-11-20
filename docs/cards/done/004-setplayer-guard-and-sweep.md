# Card 004: SetPlayer null-LocalPlayer guard, plus a static sweep for the same bug pattern

Read `docs/STATE.md` first (the "Mod code rules" section and the card 003 result), then the report in `docs/cards/003-remote-pc-crash-guard.md`. Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why
The card 003 guard works: on the host, `UpdateBindings` was skipped once and BeginPlay survived. The host then crashed one step later on the same join path: `UWorld::NotifyControlMessage` → `SpawnPlayActor` → `ASTPlayerController::SetPlayer+0x31`, `EXCEPTION_ACCESS_VIOLATION` reading 0x30.

Dump: `prefix/drive_c/users/steamuser/AppData/Local/StarshipTroopers_UBW/Saved/Crashes/UECC-Windows-39E8E08F4889ACB60F408A8372F2D7C9_0000/UEMinidump.dmp` (read-only). Run `python3 -I tools/crash_stack.py` on it.

`?SetPlayer@ASTPlayerController@@MEAAXPEAVUPlayer@@@Z` (RVA 0x051DBB90), as the senior read it:
```
call APlayerController::SetPlayer            ; Super
call APlayerController::GetLocalPlayer
test rax,rax / je +5                         ; skips only the GetSubsystem call
call ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>
lea rcx,[rax+0x30] / mov rax,[rax+0x30]      ; <- faults when rax == 0
```
This is the same single-player assumption as card 003. Fixing crash sites one at a time is slow, because each costs a full two-instance run. So this card also sweeps the binary for the pattern.

## Do
1. **Map SetPlayer completely.** Disassemble it from start to end and describe what runs after the faulting point: the loop over `this+0x828` / `+0x830`, and everything up to the return. Classify each block:
   - **local-only:** input mapping contexts, enhanced-input subsystem, HUD/UI, local settings;
   - **needed on the server for a remote PC:** anything replicated, gameplay state, or pawn/possession setup.

   Name the calls.
2. **Guard SetPlayer.** Choose the narrowest correct design from step 1 and justify it.
   - If everything after Super is local-only, hook `ASTPlayerController::SetPlayer`. Decide whether this is a remote PC **before** calling anything. One candidate: the `UPlayer*` argument is not a `ULocalPlayer` (check the `IsA` / `StaticClass` publics). Another candidate: check `GetLocalPlayer` after calling `APlayerController::SetPlayer` yourself. That version must call Super **exactly once** and must never also call the original, so prove from the disassembly that Super is the first call and has no side effects the original depends on.
   - If something after Super is needed on the server, guard at a lower layer instead (for example, a null-returning `GetSubsystem` path is not enough on its own, because the caller dereferences the result unconditionally). Explain the choice.
   - Logging follows the card 003 cap pattern (first 8, then one "further skips not logged" line). Hook order: before Tick. Add the symbol to `tools/symbols.txt` and regenerate. Check the prologue for MinHook.
   - No leak: if you skip work, show it allocated nothing that someone else frees later.
3. **Static sweep for the pattern** (new `tools/find_lp_derefs.py`, standard library, `python3 -I`). Find every `call` (E8 rel32) in `.text` to:
   - `APlayerController::GetLocalPlayer`;
   - each `ULocalPlayer::GetSubsystem<...>` instantiation and `GetSubsystemInternal` reached through `ULocalPlayer`. Find them through the publics, by name.

   For each call site, report the caller symbol and whether the returned `rax` is null-tested (`test rax,rax` / `jz|je`) before its **first** dereference within a small bounded window, for example 24 instructions. Any `[rax+disp]` access, `[rcx+disp]` after `mov rcx,rax`, or a pass to a call that dereferences it counts as a dereference. Keep the decoder minimal: you may shell out to `llvm-objdump` over the caller's range, which is simpler than writing x86 decode. Output one row per site: `caller+off  target  CHECKED|UNCHECKED|UNSURE`.
   - Restrict the **report** to game-module callers (symbols containing `ST`, `AD` or other non-Engine game prefixes), but list the full counts.
   - From the UNCHECKED game sites, name the ones reachable on the server for a remote PC: BeginPlay, SetPlayer, Possess/OnPossess, PostInitializeComponents, ClientRestart, replicated `OnRep_*`, and spawn and login paths. **Do not guard them yet.** List them ranked by likelihood. Card 005 decides which ones to guard.
4. Build and prove: a clean build at `/W4 /WX`, exports identical (84), imports only `KERNEL32.dll`, the new RVAs, a prologue disassembly, and the sweep output pasted in (trimmed to the game rows plus the counts).

## Constraints
- Follow STATE.md's "Mod code rules": no heap use, every resource released, no stored game pointers.
- Do not launch the game, and do not touch `game/` or `logs/`. Under `prefix/`, only read the crash dumps.
- No subagents. No commits. If a delete or other action is denied, say so in the report; don't ask the senior to do it for you.

## Report
(append below)

### Junior report (2026-11-02)

Game not launched. Nothing under `game/`, `logs/` or `prefix/` written; the exe and PDB
were only read. No subagents, no commits, nothing denied. Fresh build went to a scratch dir;
`build/dwmapi.dll` was not rebuilt or deployed.

#### 1. ASTPlayerController::SetPlayer map

`?SetPlayer@ASTPlayerController@@MEAAXPEAVUPlayer@@@Z`, RVA 0x051DBB90..0x051DBF37.

| Offset | What | Class |
|---|---|---|
| +0x00..+0x10 | prologue, `mov rbx,rcx` | — |
| +0x13 | `APlayerController::SetPlayer` (Super, RVA 0x040601F0) | **server-needed** |
| +0x1B | `APlayerController::GetLocalPlayer` (RVA 0x04047B20); `test/je` skips only the next call | local-only |
| +0x28 | `ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>` (RVA 0x04461C80) | local-only |
| +0x2D | `lea rcx,[rax+0x30]; mov rax,[rax+0x30]; call [rax+0x18]` = IEnhancedInputSubsystemInterface slot 3 = `UEnhancedInputLocalPlayerSubsystem::GetUserSettings` (RVA 0x01A23760, checked against the `??_7UEnhancedInputLocalPlayerSubsystem@@6BIEnhancedInputSubsystemInterface@@@` vtable). **Fault site**: with rax = null it reads 0x30. | local-only |
| after | settings null → return. Else loop over `TArray<TSoftObjectPtr<UPlayerMappableInputConfig>>` at this+0x828 (count +0x830, stride 0x28): FSoftObjectPath copy, FWeakObjectPtr::Get / ResolveObject / LoadSynchronous, Cast to UPlayerMappableInputConfig, iterate its mapping-context TMap (+0x58), `UEnhancedInputUserSettings::IsMappingContextRegistered`, else vtable +0x360 = `UEnhancedInputUserSettings::RegisterInputMappingContext` (RVA 0x044789A0, verified). | local-only |

Everything server-relevant is Super. Super (`APlayerController::SetPlayer`) unconditionally
stores `Player = InPlayer` (`mov [rdi+0x348], rsi` at 0x14406024D), sets
`InPlayer->PlayerController`, branches on ULocalPlayer vs UNetConnection, calls
UpdateOwningNetConnection and FReplicationSystemUtil::UpdateSubObjectGroupMemberships; nothing
later in Super writes this+0x348. `GetLocalPlayer` is
`Player != null && FObjectPtr::IsA(&Player, Z_Construct_UClass_ULocalPlayer_NoRegister())`;
`FObjectPtr::IsA` (RVA 0x014CBD80) is a pure read (class at +0x10, depth/array compare).

#### 2. Guard

Design: hook `ASTPlayerController::SetPlayer`. Detour:

- `InPlayer != null && FObjectPtr::IsA(&copy_of_InPlayer, ULocalPlayer::StaticClass())` →
  call the original (unchanged path for every local player).
- otherwise → call `APlayerController::SetPlayer` (Super) directly, exactly once, never the
  original; capped log
  `guard: ASTPlayerController::SetPlayer(pc=%p, player=%p) not a ULocalPlayer: Super only, input-mapping tail skipped`
  (first 8, then one `guard: ASTPlayerController::SetPlayer ... further skips not logged`).

Why this is the narrowest correct cut: the only server-needed work is Super, and it runs in
both paths. Because Super sets `Player = InPlayer` unconditionally, testing `InPlayer` before
the call with the same IsA test is exactly the original's post-Super `GetLocalPlayer()`, so
the guard skips precisely the case where the original faults (GetLocalPlayer null →
GetSubsystem skipped → null deref). Alternatives rejected: hooking GetSubsystem/GetUserSettings
would change behaviour for every caller of those engine functions; hooking GetLocalPlayer can't
help because the deref is on its null result.

Leak check of the skip: the skipped tail's only heap use is the per-iteration FSoftObjectPath
copy, which it frees inside the loop; LoadSynchronous results are GC-owned; the tail stores
nothing into the controller other than via RegisterInputMappingContext on local settings. So
skipping it leaves nothing to release. Super's own state changes all happen (it is called).

Changes:

- `tools/symbols.txt`: new "Card 004" section with `ASTPlayerController_SetPlayer` (hooked),
  `APlayerController_SetPlayer`, `ULocalPlayer_StaticClass`
  (`?Z_Construct_UClass_ULocalPlayer_NoRegister@@YAPEAVUClass@@XZ`), `FObjectPtr_IsA` (called).
- `mod/src/offsets.generated.h` regenerated (`python3 -I tools/pdb_rva.py gen`). Diff is
  four additions only; every existing RVA unchanged:
  `0x051DBB90 ASTPlayerController_SetPlayer`, `0x040601F0 APlayerController_SetPlayer`,
  `0x03DD2F60 ULocalPlayer_StaticClass`, `0x014CBD80 FObjectPtr_IsA`.
- `mod/src/hooks.cpp`:
  - `detour_st_set_player` + globals, with the reasoning above in a comment block.
  - Hook added to `kHooks` just before `UGameEngine_Tick` (Tick stays last).
  - New `note_skip(uint32_t& count, const char* what)` shared capped-logging helper;
    `detour_update_bindings` now uses it. Same behaviour as before: 8 detailed lines, one
    "further skips not logged", counter saturates at 9.
  - Called (non-hooked) functions moved into a `kCalls` table (`{Symbol*, void** slot}`)
    resolved by a bounded loop in `resolve_all`; GetGameWorld and SetClientTravel moved
    there too. All five resolve before MinHook is touched; any failure aborts install as before.
- No heap, no stored game pointers (pc/player only logged as `%p`), no new loops beyond
  the fixed-size `kCalls` walk, every function under 60 lines.

Prologue (MinHook relocates whole instructions covering ≥5 bytes, so 0x..B90–0x..B98):

```
1451dbb90: 40 55                    push rbp
1451dbb92: 53                       push rbx
1451dbb93: 57                       push rdi
1451dbb94: 48 8d 6c 24 b9           lea  rbp, [rsp - 0x47]
1451dbb99: 48 81 ec d0 00 00 00     sub  rsp, 0xd0
1451dbba0: 48 8b d9                 mov  rbx, rcx
1451dbba3: e8 48 46 e8 fe           call 0x1440601f0   ; APlayerController::SetPlayer
```

No RIP-relative operands and no branches in the copied 9 bytes. Lowest in-function branch
target is 0x1451DBBBD (+0x2D); zero branches target 0x1451DBB90..0x1451DBB98.

#### 3. Sweep: `tools/find_lp_derefs.py`

Stdlib-only, `python3 -I`, reuses `crash_stack`/`pdb_rva` for PE + PDB publics. Finds every
`E8` call to `APlayerController::GetLocalPlayer`, every
`ULocalPlayer::GetSubsystem<...>` instantiation, and `GetSubsystemInternal` calls whose rcx
was built as `lea [x+0x138]` (the LocalPlayer subsystem collection). For each site it follows
rax straight-line (window 24, in-function jmps followed), tracks copies/derived registers, and
classifies: deref before any null test → UNCHECKED; `test r,r` + jcc with a deref on the null
path → UNCHECKED; null test with clean null path → CHECKED; value passed to a direct callee is
analysed in the callee (up to 3 levels, 2x window). Anything it can't decide → UNSURE with a
reason. ~5 s per run. Usage: `python3 -I tools/find_lp_derefs.py [--publics FILE | --pdb PDB] [--all]`.

Output (game modules, default mode; identical with `--pdb` default and `--publics`):

```
ASTPlayerController::GetAndApplyGameUserSettings+0x32  APlayerController::GetLocalPlayer  UNCHECKED  ; passed to USTPlayerUtilityLibrary::UpdateBindings which derefs @rva 0x51C6E9D
ASTPlayerController::SetPlayer+0x1B  APlayerController::GetLocalPlayer  UNCHECKED  ; null test je @rva 0x51DBBB3 but null path derefs: mov rax, qword ptr [rax + 0x30] @rva 0x51DBBC1
ASTPlayerController::SetPlayer+0x28  ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>  UNCHECKED  ; mov rax, qword ptr [rax + 0x30] @rva 0x51DBBC1
ASTPlayerMechBase::AddMechInputContext+0x41  ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>  UNSURE  ; window of 24 ran out
ASTPlayerMechBase::RemoveMechInputContext+0x41  ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>  UNSURE  ; window of 24 ran out
ASTPlayerMechBase::AddMechInputContext+0x32  APlayerController::GetLocalPlayer  CHECKED  ; je @rva 0x50DB2BC (null path: value no longer in a register)
USTUIOptionsGamepad::GetDefaultActionKeyMappingForMappingName+0x47  ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>  CHECKED  ; je @rva 0x520026F (null path: value no longer in a register)
USTUIOptionsKeyboardControls::GetDefaultActionKeyMappingForMappingName+0x47  ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>  CHECKED  ; je @rva 0x52006EF (null path: value no longer in a register)
USTUIControlInputBase::GetPlayerMappedKey_Implementation+0x47  ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>  CHECKED  ; je @rva 0x520501F (null path: value no longer in a register)
USTCommonUtilityLibrary::GetSTLocalPlayer+0x4E  APlayerController::GetLocalPlayer  CHECKED  ; je @rva 0x523E053 (null path: value no longer in a register)
USTUIScreenMenu::IsActionKey_Implementation+0x70  ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>  CHECKED  ; je @rva 0x5208828 (null path: value no longer in a register)
USTUIControlInputBase::KeyChanged_Implementation+0x1C9  ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>  CHECKED  ; je @rva 0x52098C1 (null path: value no longer in a register)
ASTPlayerMechBase::RemoveMechInputContext+0x32  APlayerController::GetLocalPlayer  CHECKED  ; je @rva 0x50FE95C (null path: value no longer in a register)
USTGameUserSettings::ResetGamepadBindings+0x1D  ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>  CHECKED  ; je @rva 0x5213E35 (null path: returned)
USTGameUserSettings::ResetKBAMBindings+0x23  ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>  CHECKED  ; je @rva 0x521407B (null path: returned)
USTGameUserSettings::STResetAllToDefaults+0x692  ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>  CHECKED  ; je @rva 0x5215ABA (null path: returned)
ASTPlayerController::SetupInputComponent+0x1D  APlayerController::GetLocalPlayer  CHECKED  ; je @rva 0x51DC4C5 (null path: value no longer in a register)
ASTPlayerController::SetupInputComponent+0x36  ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>  CHECKED  ; je @rva 0x51DC4E1 (null path: value no longer in a register)
--- E8 byte hits on targets: 171; not a decoded call: 0; GetSubsystemInternal on non-LocalPlayer collections: 60
--- all sites: 111  CHECKED=77  UNCHECKED=30  UNSURE=4
--- game sites: 18  CHECKED=13  UNCHECKED=3  UNSURE=2
```

Full counts (`--all`): 111 sites, CHECKED=77, UNCHECKED=30, UNSURE=4. 171 raw E8 hits, all
decoded as calls; 60 GetSubsystemInternal calls on non-LocalPlayer collections excluded.
Engine UNCHECKED are CommonUI (FUIActionBinding ctor, FCommonAnalogCursor, FActivatableTree*,
UCommonUIActionRouterBase, UCommonBoundActionBar), UEnhancedInputUserSettings
(AsyncSaveSettings, LoadOrCreateSettings, SaveSettings) and InAppPurchase proxies — UI and
settings code that only runs for a local player. Engine UNSURE: UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry, AGameModeBase::ShouldStartInCinematicMode.
Caveat: some callee names the tool prints come from identical-code-folded functions (e.g. a
tiny getter reported as `FSandboxPlatformFile::IsSandboxEnabled`); the address is right, the
name is whichever public the linker kept.

Manual follow-up on the two game UNSUREs (window ran out inside a TSet bit-iterator loop):
`ASTPlayerMechBase::AddMechInputContext` / `RemoveMechInputContext` keep the subsystem in r15
(null when GetLocalPlayer is null — the GetLocalPlayer null test only skips GetSubsystem), load
the mech's PlayerMappableInputConfig (soft ptr this+0xE60), then for each mapping context do
`lea rcx,[r15+0x30]; mov rax,[rcx]; call [rax+0x98]` (Add, 0x1450DB4BC) / `call [rax+0xA0]`
(Remove, 0x1450FEB5C). So they are **UNCHECKED**: they fault for a non-local controller
whenever the config has at least one mapping context. Callers have no IsLocallyControlled gate:
`OnEmbarkingComplete` (only `IsValid` on weak ptr this+0xE30) and `ActorFullyLoaded` call Add;
`LeaveMech` (only `IsValid` on this+0xE18) calls Remove.

#### 4. Remote-PC-reachable UNCHECKED game sites, ranked (not guarded here; card 005)

1. `ASTPlayerController::SetPlayer+0x1B/+0x28` — every remote join on the host. **Fixed by this card's guard.**
2. `ASTPlayerMechBase::AddMechInputContext+0x254` (via `OnEmbarkingComplete`, `ActorFullyLoaded`) — host faults when a remote player's mech finishes embarking, if those run on the server (no local-control gate seen; need runtime confirmation).
3. `ASTPlayerMechBase::RemoveMechInputContext+0x24E` (via `LeaveMech`) — same, on leaving the mech.
4. `ASTPlayerController::GetAndApplyGameUserSettings+0x32` — passes null into UpdateBindings; already covered by the card 003 UpdateBindings guard.

Remaining game sites are CHECKED and UI/settings-only.

#### 5. Build proof

Fresh configure + build into a scratch dir (`gen_proxy_exports.py`, `pdb_rva.py gen`, CMake
Ninja + clang-cl-xwin toolchain, Release): 11/11 steps, **0 warnings** under `/W4 /WX`
(compile and link). `dwmapi.dll` 147968 bytes. Exports: 84, identical name/ordinal list to
`build/dwmapi.dll` (`llvm-readobj --coff-exports` diff empty). Imports: `KERNEL32.dll` only.
