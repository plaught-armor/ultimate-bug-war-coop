# Card 007: Host crash in the remote PlayerController's idle check (null PlayerInput)

Read `docs/STATE.md` first: the "Mod code rules" section and the "Result after card 006" entry. Card 004's report (`docs/cards/004-setplayer-guard-and-sweep.md`) shows the earlier remote-PC fix and the `tools/find_lp_derefs.py` sweep you will extend. Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why
Card 006 works: the client got into the map with input contexts added, and `StartEncounter` was skipped on the client 8 times, with no client crash. About 30 seconds after the join, the **host** crashed on its game thread (thread 360), reading `0x308` with `rcx == 0` at `UPlayerInput::IsPressed+0x35` (RVA 0x04313635).

Dump (read-only): `prefix/drive_c/users/steamuser/AppData/Local/StarshipTroopers_UBW/Saved/Crashes/UECC-Windows-EFDA5B834146BC766DDF49864C53982E_0000/UEMinidump.dmp`. Use `python3 -I tools/crash_stack.py <dmp> --depth 1400`.

Stack (call-preceded frames):
```
UWorld::Tick → FTickTaskManager → FActorTickFunction::ExecuteTick
 → APlayerController::TickActor
 → ASTPlayerController::Tick                 RVA 0x051E1480   (return address +0x122 = 0x051E15A2)
 → UPlayerInput::IsPressed(this = null, Key)  RVA 0x04313600   <- fault
```

What the senior read from `ASTPlayerController::Tick` (VA 0x1451E1480):
- It calls `AActor::Tick`. It returns early if `UGameplayStatics::IsGamePaused` is true, or if the byte at `this+0x7E0` is already set.
- Otherwise it adds `DeltaSeconds` to the float at `this+0x7E4`. Once that reaches `this+0x7E8`, it checks the three doubles at `this+0x528/0x530/0x538` with an fmod-style call (0x1411B5B90) against a constant. If all three come out as 0, it calls `PlayerInput->IsPressed(<static FKey at 0x14926FBB8>)` with `PlayerInput = *(this+0x420)`. If that returns false, it sets `this+0x7E0 = 1`.
- It then calls `ASTPlayerController::TickPlayerHover` (RVA 0x051E7220), followed by further code from `+0x138` on.

This looks like an idle or AFK timer. On the server, the remote client's PlayerController has no `PlayerInput`, because UE only creates one for local controllers. So the first time the timer expires while the remote player isn't moving or turning, the host dereferences null. Single player never hits this, because every PlayerController there is local.

## Do
1. **Prove the reading.**
   - Prove `APlayerController::PlayerInput` is at `+0x420` in this build, using an engine function that reads it, such as `APlayerController::InitInputSystem`, `IsInputKeyDown` or `GetInputKeyTimeDown`.
   - Identify the FKey at 0x14926FBB8 (likely `EKeys::AnyKey`) and the doubles at `+0x528..0x538`.
   - Identify what `this+0x7E0`, `0x7E4` and `0x7E8` are. Use the PDB field names if any; otherwise list their readers and writers.
   - Find who reads the `0x7E0` flag and what follows on the server when it is set: for example an idle animation, a replicated state, a kick, or a UI. This decides the gate's return value in step 3.
2. **Walk the rest of `ASTPlayerController::Tick`** from `+0x138` to its end, plus `TickPlayerHover`. List every dereference that is null or meaningless for a remote PlayerController on the server: `PlayerInput`, LocalPlayer, viewport, HUD, `PlayerCameraManager`, the widgets at `+0x7D8`, and so on. For each one, say whether it is guarded, and whether a remote PlayerController reaches it.
3. **Fix at the narrowest responsible layer.** Pick one option and justify it from steps 1 and 2:
   - **(a)** Hook `UPlayerInput::IsPressed`. With `this == null`, return a fixed value without calling the original. Choose `false` ("no key down", which is literally true for a controller with no input) or `true` (which keeps the host from marking a remote player idle), based on what step 1 found that the flag drives on the server. Gate it on `_ReturnAddress()` == `Tick+0x122`, as card 005 did, unless you show that every null-`this` caller wants the same value.
   - **(b)** Hook `ASTPlayerController::Tick`. For a PlayerController that is not local (no `ULocalPlayer`; reuse the test from card 004), apply the same skip only if step 2 shows that more than this one site breaks.

   Either option must follow these rules:
   - Do not change the local-player path or single player.
   - Log the skip with a capped counter.
   - Don't let the skip leak: show the original does no setup before the skipped call that it would undo after it.
   - Add new symbols to `tools/symbols.txt` and regenerate. Check the prologues for MinHook.
4. **Sweep this class.** Add `--targets playerinput` to `tools/find_lp_derefs.py` for loads of `APlayerController::PlayerInput` (`+0x420` from a PlayerController) in game code (`ST*`/`AST*`/`UST*`), followed by an unchecked dereference or call. Report the unchecked sites, ranking first the ones that run on the server for every PlayerController: Tick, BeginPlay, possession or `OnRep`. **Report only; don't gate them.**
5. **Build and prove.** Use the command in STATE.md. Show:
   - a clean `/W4 /WX` build;
   - 84 exports;
   - imports only from `KERNEL32.dll`;
   - the RVAs, prologues and offset evidence;
   - the trimmed sweep output.

   Deploy is the senior's job.

## Constraints
- Follow STATE.md's "Mod code rules": no heap use, every resource released, no stored game pointers, a skipped original must not leak, ≤60-line functions, bounded loops, shared state touched only on the game thread.
- Do not launch the game, and do not touch `game/` or `logs/`. Under `prefix/`, only read the crash dumps.
- No subagents. No commits. If a delete or other action is denied, say so in the report; don't ask the senior to do it for you.

## Report
(append below)

### Summary
I chose option (a). `UPlayerInput::IsPressed` is now hooked. Only a call with `this == null` that returns to `ASTPlayerController::Tick+0x122` is changed. That call destroys the by-value FKey, logs the skip with a capped counter, and returns `false`. At install, the hook checks `Tick+0xEB` (`mov rcx, [rbx+0x420]`), `Tick+0x11D` (`call IsPressed`) and `Tick+0x122` (`test al, al`); if any of them differs, the install fails. Step 2 found no other crash site in the rest of Tick or in TickPlayerHover. It found one piece of meaningless data, which is not a crash: the hover trace for a remote PC is aimed with the host's camera (listed under "Open" below). `--targets playerinput` has been added to the sweep. Its only true unchecked PC site is `Tick+0xEB`. The build is clean, with 84 exports and KERNEL32 as the only import. Nothing was launched, nothing was committed, and `game/`, `logs/` and `prefix/` were only read.

### Step 1: the reading, proven (Shipping exe, image base 0x140000000)
- **PlayerInput is at +0x420.** `APlayerController::InitInputSystem` (RVA 0x0404C630) tests `cmp qword ptr [rcx+0x420], 0` at VA 0x14404C64D and creates the object when it is null. It stores the new object with `mov [rdi+0x420], rbx` at VA 0x14404C76B. Only local controllers run InitInputSystem, so a remote PC on the server keeps the field null.
- **The FKey at 0x14926FBB8 is `EKeys::AnyKey`** (public `?AnyKey@EKeys@@2UFKey@@B`, RVA 0x0926FBB8). The FKey is 0x18 bytes: an FName at +0, a details pointer at +8, and a shared-reference controller at +0x10. Tick copies all three qwords to `[rsp+0x20]` and adds a shared reference with `lock inc [ctrl+8]`. `IsPressed` itself compares the key against `AnyKey` before it faults (`cmp rbx, [rip→0x14926FBB8]`, VA 0x144313622).
- **The doubles at +0x528/+0x530/+0x538 are `APlayerController::RotationInput`** (Pitch, Yaw and Roll). They are not ControlRotation, which is at +0x320 per `AController::GetControlRotation`. The writers are:
  - `AddPitchInput` (0x04031A40) adds into +0x528 (scale at +0x544);
  - `AddYawInput` (0x040330E0) adds into +0x530 (scale at +0x540);
  - `AddRollInput` (0x04031D30) adds into +0x538 (scale at +0x548).

  On the server, a remote PC's RotationInput stays 0, so `fmod(x, 360.0)` (FGenericPlatformMath::Fmod 0x011B5B90, constant `__real@4076800000000000`) is 0 for all three. IsPressed is therefore reached every time the timer expires.
- **+0x7E0, +0x7E4 and +0x7E8** are an idle latch (bool), an idle timer (float) and an idle threshold (float). None of them is a UPROPERTY, so there is no PDB field name.
  - Writers:
    - The constructor stores the qword at +0x7E8 = 0x40A00000, which sets the threshold to 5.0f and +0x7EC to 0.
    - `ASTPlayerController::OnPossess` (0x051D17C0) clears +0x7E0 and +0x7E4. It sets +0x7E8 = `TimeBeforeIdle` (a float at +0x28 of the CDO of the pawn's `CoreCharacterDataClass`), falling back to 5.0 (`__real@40a00000`).
    - Every `HandleInput*` handler (Fire, Look, Move, Melee, Use, Taunt, Quicksave, Quickload, the weapon selects, ExitVehicle) clears +0x7E0 and +0x7E4.
    - Only `Tick` sets +0x7E0 = 1.
  - Readers: only `Tick`. I searched the whole exe for byte reads at `+0x7E0`. The only other game-code hits are `USTGlyphManager::GetGlyph` and the `USTCheatManager::Show*Glyphs` functions, and those read a different object. A `+0x7E4`/`+0x7E8` search finds only `ASTNonPlayableCharacterBase` floor-trace code and `ASTPlayerCharacter::ActorFullyLoaded`, which are other classes with their own fields there. Because the flag is not a UPROPERTY, Blueprint cannot read it.
  - **What the flag drives on the server: nothing.** It is a write-only latch that only stops Tick from checking again until the next input or possession. No animation, replication, kick or UI reads it.

### Step 2: rest of Tick (+0x12D to the end, 0x051E1646) and TickPlayerHover (0x051E7220)

| Site | What it dereferences | Guarded? | Reached by a remote PC on the server? |
|---|---|---|---|
| Tick+0xEB / +0x11D | `PlayerInput` (+0x420), then `IsPressed` reads +0x308 | no | **yes, this is the crash** (now gated) |
| Tick+0x154 | `[this+0x7D8]`: ControlledPlayerCharacter (pawn), not a widget; OnPossess sets it | null-checked | yes, and it is the server's pawn, so it is valid |
| Tick+0x160 | `[pawn+0x8FA]` byte | after the pawn check | yes, harmless |
| Tick+0x169..0x176 | `AActor::GetWorld`, then `USTCommonUtilityLibrary::GetSTGameInstance`, then `[GI+0x1D0]` | **GI is not null-checked**; `[GI+0x1D0]` is checked | yes, once (latched by +0x7F0). GI is a USTGameInstance on the host too, so it is non-null. The local path has the same exposure, so this is not specific to remote PCs. The callee at 0x01162D20 is a folded `ret 0` stub. |
| Tick+0x1A4 | `GetMappedAccelerationZoneSpeedIncrease` (0x051C89E0): `[this+0x8D0]` null-checked, then `USTGameUserSettings::GetSTGameUserSettings()` null-checked, then `GetAccelerationZoneSpeedIncrease` | yes | only while +0x7F1 is set. Those are the host's own user settings, which is meaningless for a remote player but safe. |
| Hover+0x13 | `[this+0x2E8]` (`AController::Pawn`: OnPossess and NotifyServerReceivedClientData read it), with GC barrier `MarkAsReachable` | null-checked; IsA `ASTCharacter`; pending-kill check | yes, valid |
| Hover+0x95 | `[this+0x7D8]` | null-checked and pending-kill-checked (re-read at +0x204 inside the guarded block) | yes |
| Hover+0xB5 | pawn vtable slot 0x9E0 = `ASTPlayerCharacter::IsDead` | — | yes, harmless |
| Hover+0x183 | pawn vtable slot 0x9F0 = `ASTCharacter::GetHUDRaycastStartAndDirection`, which is `jmp USTCommonUtilityLibrary::GetCenterScreenWorldPostionDirection(pawn, …)`: `UGameplayStatics::GetPlayerController(pawn, 0)` (null-checked, IsA ASTPlayerController), then `GetViewportSize`, then `DeprojectScreenPositionToWorld` | yes | yes. **Meaningless:** index 0 is the host's own local PC, so a remote PC's hover trace starts from the host's screen centre. Both engine calls handle a missing LocalPlayer or viewport, so it does not crash. |
| Hover+0x25F..0x2E4 | `UKismetSystemLibrary::LineTraceSingle` (world context = this PC), `FActorInstanceHandle::FetchActor` on the hit | yes | yes, and it is valid on the server |
| Hover+0x349..0x366 | stores the hovered actor at +0x7A8 and broadcasts the delegate at +0x768 when it changes | — | yes, with the host-camera result above |

No HUD, `PlayerCameraManager` or LocalPlayer dereference is reached directly in either function. The only null crash is the IsPressed site, so option (b), a wider Tick gate, is not justified.

### Step 3: the fix (option a)
- **Code:** `mod/src/hooks.cpp`, section "Remote-PC idle check guard (card 007)".
  - `detour_is_pressed(player_input, key)`. If `player_input != nullptr` or `_ReturnAddress() != Tick+0x122`, it tail-calls the original unchanged, so every local, single-player and other caller is untouched. Otherwise it calls `g_fkey_destructor(key)`, does `note_skip(g_is_pressed_skips, "remote-PC IsPressed")` (first 8 skips logged, then one "further skips not logged" line), and returns `false`.
  - `resolve_tick_is_pressed_site()` runs in `install()` before MinHook. It checks the 7 bytes `48 8B 8B 20 04 00 00` at Tick+0xEB, `E8` at Tick+0x11D with a rel32 that lands on `IsPressed`, and `84 C0` at Tick+0x122. Then it records the return address. A mismatch logs a FATAL line and fails the install, the same pattern as card 005.
  - In the tables, IsPressed is added to `kHooks` before `UGameEngine_Tick`, which stays last. `ASTPlayerController_Tick` and `FKey_Destructor` are added to `kCalls`.
- **Why `false`:** step 1 shows the latch has no consumer, so either value is safe. `false` is the literal truth for a PC with no local input. It also sets the latch, so Tick stops calling IsPressed for that remote PC until the next `OnPossess`: one skip per possession. `true` would leave the latch clear while the timer (+0x7E4) is never reset (only `HandleInput*` and `OnPossess` reset it, and neither runs for a remote PC's input on the host). That would mean a skip every frame after 5 s.
- **No leak:** `IsPressed` takes `FKey` by value through a pointer, and the **callee** destroys it: its epilogue releases the shared reference at key+0x10. Before the call, Tick does nothing except copy the FKey and add that one reference (`lock inc [ctrl+8]`), and after the call it never touches the copy. The skip therefore runs `FKey::~FKey` on the key, which balances that reference exactly. Nothing else is set up, so nothing else needs undoing.
  - `??1FKey@@QEAA@XZ` (RVA 0x015B75F0, ICF-folded with `~FInputChord` and `~FSpline`) loads `[rcx+0x10]`, then does `lock xadd -1` on +8 (calling the destroy-object virtual when it reaches 0) and on +0xC (calling the delete-controller virtual when it reaches 0). That is exactly the TSharedPtr release.
- **The mod code rules:**
  - no heap;
  - no game pointer is kept (the saved return address is our own code address inside the exe image, set once at install);
  - the counter is touched only on the game thread (actor tick);
  - all functions are 60 lines or fewer;
  - there are no loops.

#### RVAs and prologues
| Alias | Symbol | RVA | Use |
|---|---|---|---|
| `UPlayerInput_IsPressed` | `?IsPressed@UPlayerInput@@QEBA_NUFKey@@@Z` | 0x04313600 | hooked |
| `ASTPlayerController_Tick` | `?Tick@ASTPlayerController@@MEAAXM@Z` | 0x051E1480 | site validated, not hooked |
| `FKey_Destructor` | `??1FKey@@QEAA@XZ` | 0x015B75F0 | called |

IsPressed prologue (MinHook copies the first 5 bytes or more; these are plain relocatable instructions with no rip-relative operand or branch in the first 0x18 bytes):
```
144313600: 48 89 5c 24 08     mov qword ptr [rsp+0x8], rbx
144313605: 55 56 57 41 56 41 57   push rbp/rsi/rdi/r14/r15
14431360c: 48 8d 6c 24 c9     lea rbp, [rsp-0x37]
144313611: 48 81 ec 90 00 00 00   sub rsp, 0x90
14431361b: 48 8d b9 e0 02 00 00   lea rdi, [rcx+0x2e0]
144313635: 44 8b 5f 28        mov r11d, dword ptr [rdi+0x28]   <- fault (0x2E0+0x28 = 0x308)
```
Tick call site:
```
1451e156b: 48 8b 8b 20 04 00 00   mov rcx, qword ptr [rbx+0x420]   (Tick+0xEB)
1451e1598: 48 8d 54 24 20         lea rdx, [rsp+0x20]
1451e159d: e8 5e 20 13 ff         call 0x144313600 (IsPressed)     (Tick+0x11D)
1451e15a2: 84 c0                  test al, al                       (Tick+0x122)
1451e15a4: 75 07                  jne +0x12D
1451e15a6: c6 83 e0 07 00 00 01   mov byte ptr [rbx+0x7e0], 1
```

### Step 4: the sweep (`--targets playerinput`)
- **How it finds sites:** the new mode finds sites by byte pattern: REX.W `8B`, ModRM mod=10, an optional SIB byte, and disp32 `0x420`. It confirms each one is a decoded `mov <reg>, qword ptr [... + 0x420]`, then follows that register with the existing Walker.
- **How it ranks rows:**
  1. PC members the server runs for every PC (Tick, BeginPlay, possession, SetPlayer, OnRep_*, GetAndApplyGameUserSettings, and so on);
  2. other PC members;
  3. game classes that are not PCs (an unrelated field at +0x420), marked "not a PC".
- **Other modes:** `lp`, `authority` and `nav` print the same totals as cards 004, 005 and 006 (18/41/15 game sites).
- **Output** (`python3 -I tools/find_lp_derefs.py --publics <publics> --targets playerinput`, about 2 s):
```
ASTPlayerController::GetAndApplyGameUserSettings+0x83  PlayerInput(+0x420)  UNCHECKED  ; passed to UPlayerInput::InvertAxis which derefs @rva 0x51C6F07
ASTPlayerController::GetAndApplyGameUserSettings+0xAC  PlayerInput(+0x420)  UNCHECKED  ; passed to UPlayerInput::GetInvertAxis which derefs @rva 0x51C6F30
ASTPlayerController::GetAndApplyGameUserSettings+0xE4  PlayerInput(+0x420)  UNCHECKED  ; passed to UPlayerInput::InvertAxis which derefs @rva 0x51C6F68
ASTPlayerController::Tick+0xEB  PlayerInput(+0x420)  UNCHECKED  ; passed to UPlayerInput::IsPressed which derefs @rva 0x51E159D
ASTPlayerController::GetAndApplyGameUserSettings+0x42  PlayerInput(+0x420)  CHECKED  ; je @rva 0x51C6EAC (null path: value no longer in a register)
ASTShootableButton::OnDamageTaken+0x48  PlayerInput(+0x420) not a PC  UNCHECKED  ; mov rax, qword ptr [rcx] @rva 0x50F701F
USTCombatUtilityLibrary::ApplyDamage+0x2E6  PlayerInput(+0x420) not a PC  UNSURE  ; value no longer in a register
ASTDamageableActorCorpseBase::BeginPlay+0x1B7  PlayerInput(+0x420) not a PC  CHECKED  ; je @rva 0x514D862 (null path: value no longer in a register)
ASTEncounterManagerBase::GetDebugString+0x133B  PlayerInput(+0x420) not a PC  CHECKED  ; je @rva 0x5123095 (null path: value no longer in a register)
--- +0x420 load byte patterns: 309; outside game code (skipped): 300; not a decoded qword mov: 0
--- rank: server-every-PC=5  other PC=0  not a PC=4
```
- **Reading it by hand:**
  - `Tick+0xEB` is the card's crash, now gated.
  - The three `GetAndApplyGameUserSettings` reloads (+0x83, +0xAC, +0xE4) are **false positives**. They lie inside the block that the `test rbx, rbx; je +0x10D` after the +0x42 load skips when PlayerInput is null. Between the loads the code only calls an FName constructor, `UPlayerInput::GetInvertAxis` and the user-settings getters, none of which writes PC+0x420. The tool follows each load on its own and cannot see that an earlier test dominates it; I left it a heuristic rather than add dataflow.
  - The "not a PC" rows read other classes' fields.
  - **So the only real unchecked PlayerInput site in game code is `Tick+0xEB`.** Engine accessors such as `IsInputKeyDown` and `GetInputKeyTimeDown` null-check PlayerInput themselves, and there are no other game-code loads.

### Step 5: build
```
$ python3 -I tools/gen_proxy_exports.py && python3 -I tools/pdb_rva.py gen && cmake -G Ninja ... && cmake --build build
84 exports (37 named) -> dwmapi.def, proxy_exports.generated.inc
wrote .../mod/src/offsets.generated.h
[1/3] Building CXX object CMakeFiles/ubwcoop.dir/src/proxy.cpp.obj
[2/3] Building CXX object CMakeFiles/ubwcoop.dir/src/hooks.cpp.obj
[3/3] Linking CXX shared library dwmapi.dll          (no warnings; /W4 /WX /EHs-c- /GR-)
$ llvm-readobj --coff-exports build/dwmapi.dll | grep -c Ordinal:   -> 84
$ llvm-readobj --coff-imports build/dwmapi.dll                        -> KERNEL32.dll only
```
Expected new log lines at install: `hooks: ASTPlayerController::Tick IsPressed return site <exe+0x51E15A2>`. On the host, after a remote player has idled for 5 s: `guard: UPlayerInput::IsPressed(this=null) from ASTPlayerController::Tick: returned false`, about once per possession.

### Files changed
- `mod/src/hooks.cpp`: the guard, its site check, and the table rows.
- `tools/symbols.txt`: the Card 007 block. `mod/src/offsets.generated.h` was regenerated.
- `tools/find_lp_derefs.py`: the `playerinput` mode, a `classify_value` helper (`classify` now delegates to it), and a rank column in the rows.

### Open (not fixed, report only)
- **Hover for remote PCs uses the host's camera.** `TickPlayerHover` on the host gets its trace start from `GetPlayerController(pawn, 0)`, which is the host's local PC. The remote PC's `+0x7A8` hovered actor and its +0x768 broadcast therefore carry host-camera results. It doesn't crash, and the gameplay effect depends on who binds that delegate on the server. That is not traced here.
- **Accel-zone value comes from the host's settings.** `GetMappedAccelerationZoneSpeedIncrease` reads the host's `USTGameUserSettings` for a remote PC (meaningless, safe).

## Senior review (2026-11-02)
Accepted. The senior confirmed the callee owns the by-value FKey: `IsPressed` releases the shared pointer at `key+0x10` on both its paths and calls `FKey::~FKey` (0x015B75F0) on `r15`. Tick does not release that pointer after the call. The return-address gate and the site validation in `install()` both run before MinHook starts. The rebuild is clean (84 exports, imports only from `KERNEL32.dll`) and is deployed. Runtime: the guard fired once on the host, about 20 s after the join, and the host kept running. The client still has no WASD, and the mouse turns the camera only while a button is held; on the host, WASD and mouse both work. Card 008 covers this.
