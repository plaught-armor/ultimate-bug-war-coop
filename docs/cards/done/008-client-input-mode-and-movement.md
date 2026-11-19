# Card 008: Client cannot move: find out why, log it, then fix it

Read `docs/STATE.md` first: the "Mod code rules" section and the "Result after card 006" entry. Then read the reports in cards 005 (client input contexts) and 007. Repo root: `/path/to/ultimate-bug-war-coop`.

## Why
With cards 001 to 007 in place, both instances stay up with the client in the map. What the user sees (screenshot, 2026-11-02):
- **Host:** first-person view with the HUD. WASD and the mouse both work.
- **Client:** spawned at a different spot from the host, with a full HUD (health 100, weapon MORITA MK1), so the HUD has a pawn to read. But:
  - **WASD does nothing.**
  - **The mouse cursor is visible**, and the camera turns only while a mouse button is held and dragged.
  - A blue light pillar stands in the world, probably an objective or waypoint beacon. It is not a fault.
- Card 005's deferred path ran on the client: `ReceivedGameModeClass ... mapping found, 4 context(s) added, rebuild requested`.
- Neither player sees the other's body. Each sees only a dark shadow blob on the ground where the other might be. This is out of scope for this card; note anything you learn about it.

The visible cursor plus drag-to-look is what UE does when the input mode is `GameAndUI` or `UIOnly`, or when `bShowMouseCursor` is set: the viewport captures the mouse only while a button is down. WASD being dead while look works points to one of these:
1. **Ignored move input:** the `IgnoreMoveInput` counter on the PlayerController is above 0, for example from a cinematic or intro sequence that started and was never ended on the client.
2. **The movement context or action never reached the client.** For example, the movement mapping is added by the pawn or by code that card 003/004/005 skipped, while the look mapping comes from one of the 4 contexts.
3. **The client's possessed pawn isn't the HUD pawn**, or the client has no possession at all (`AcknowledgePossession` / `ClientRestart` never arrived), so `AddMovementInput` has no target.
4. **Input mode or cursor:** this is set on the host by mission-start code (GameMode, level Blueprint or encounter) that runs only on the server, or that card 006 now skips on the client.

## Do
1. **Static map.** Find who sets each of the following, and on which side (server, client, or both), in the game code and the paths the level Blueprint reaches:
   - the input mode: `UWidgetBlueprintLibrary::SetInputMode_*`, `APlayerController::SetInputMode`;
   - the cursor: `bShowMouseCursor` / `SetShowMouseCursor`;
   - `SetIgnoreMoveInput` / `SetCinematicMode`;
   - the movement action binding: which `UInputMappingContext` holds WASD, who adds it, and through which `ASTPlayerCharacter` / `ASTPlayerController` setup function (for example `SetupPlayerInputComponent` or a `PawnClientRestart` path).

   For each one, say whether a client PlayerController reaches it today, given the card 003–007 guards and the card 006 encounter skip.
2. **Diagnostic logging, client side only, capped, read-only.** Hook the smallest set that tells the four hypotheses apart, and log when each fires:
   - possession on the client, through `APlayerController::AcknowledgePossession` and/or `ClientRestart` / `OnRep_Pawn`, logging the PC, the pawn and the pawn class name if you can get it without the heap;
   - input-mode and cursor changes;
   - `SetIgnoreMoveInput`, logging the counter before and after;
   - Enhanced Input contexts added on the client, with the context object name if it can be logged without the heap.

   In addition, log one **snapshot** about 10 s after `LoadMap` returns on the client, from the game thread: the PC, the pawn, the `IgnoreMoveInput` / `IgnoreLookInput` counters, `bShowMouseCursor` and the viewport mouse capture mode. Prove every offset you read from a function in this build that reads it. Pick a game-thread tick you already hook, and keep it bounded and allocation-free.
3. **Fix, only if step 1 proves the cause.** Apply the narrowest fix that runs the missing step on the client for its own local PlayerController. Examples: run the input-mode / cursor reset the host gets, clear a stale ignore-move counter, or add the movement context. The local-player path on the host and single player must not change. If the cause is not proven statically, ship only the step 2 logging; the senior's runtime run will decide.
4. **Build and prove.** Use the command in STATE.md. Show:
   - a clean `/W4 /WX` build;
   - 84 exports;
   - imports only from `KERNEL32.dll`;
   - the RVAs, prologues and offset evidence;
   - what each new log line will say.

   Deploy is the senior's job.

## Constraints
- Follow STATE.md's "Mod code rules": no heap use, every resource released, no stored game pointers beyond the current call, a skipped original must not leak, ≤60-line functions, bounded loops, capped logging, shared state touched only on the game thread.
- Do not launch the game, and do not touch `game/` or `logs/`. Under `prefix/`, only read the crash dumps.
- No subagents. No commits. If a delete or other action is denied, say so in the report; don't ask the senior to do it for you.

## Report
(append below)

### Summary
- **Cause of dead WASD on the client (proven statically):** every `ASTPlayerController::HandleInput*` action except Look goes through the pawn's `USTInputReceiverInterface`, which the PC caches at `+0x7C0`. The only writer of that field is `ASTPlayerController::OnPossess`, and `OnPossess` runs only with authority. The joining client's own PC gets its pawn by replication instead (`OnRep_Pawn` / `ClientRestart` → `APlayerController::SetPawn`). So on the client `+0x7C0` stays null, and `HandleInputMove` returns at its first test. Look works because `HandleInputLook` calls `AddYawInput`/`AddPitchInput` directly.
- **Fix:** hook `APlayerController::SetPawn`. Only on a network client's local `ASTPlayerController`, after the original runs, the hook stores what `OnPossess` would have stored for the pawn in `AController::Pawn`:
  - the input receiver at `+0x7C0`
  - the player character at `+0x7D8`
  - `ReleaseActiveInputs` before a non-null receiver is replaced, as `OnUnPossess` does
- **Input mode / cursor / ignore-move:** no native AST*/UST* code sets the input mode or ignore-move, and none sets the cursor in a way that would explain this. The real callers are Blueprint thunks and CommonUI. Their content is in Oodle-compressed IoStore assets, which can't be read statically, so I added client-only, capped logging detours and a one-shot snapshot 10 s after each client map load.
- Build: clean `/W4 /WX`, **84 exports**, the only import is **KERNEL32.dll** (`llvm-readobj --coff-imports` / `--coff-exports` on `build/dwmapi.dll`). Rebuilding after `touch hooks.cpp` printed no warnings.
- No launches, no subagents, no commits. Nothing under `game/`, `logs/` or `prefix/` was touched. No action was denied.

### Static map: who touches input on the client

| What | Who (native, this build) | Reaches the client's own PC? |
|---|---|---|
| `SetInputMode` (vtbl slot 0xD50) | No native AST*/UST* caller. Callers: BP exec thunks (`UWidgetBlueprintLibrary::SetInputMode_*`), `FInputMode*::ApplyInputMode`. `USTUIScreenMenu::NativeConstruct` calls `SetInputMode_UIOnlyEx`. | Yes, any UI menu on the client. Runtime log needed for which. |
| `bShowMouseCursor` (bit 0 of `PC+0x54C`) | `USTUIScreenMenu::NativeConstruct+0x53` (0x0520DC03) `or [pc+0x54C],1`, right after `SetInputMode_UIOnlyEx`. CommonUI `UCommonUIActionRouterBase::ApplyUIInputConfig` → `SetShowMouseCursor`. BP thunks. | Yes. |
| Other native `PC+0x54C` writes | `ASTPlayerController::GetAndApplyGameUserSettings+0x1A9..+0x1BB` and `ASTPlayerController::OnInputDeviceChanged+0x40..+0x50` only toggle bit 5 (0x20, gamepad is the current input), not the cursor. | n/a |
| Mouse capture / lock (`UGameViewportClient+0x3BA/+0x3BC`) | CommonUI `ApplyUIInputConfig` → `SetMouseCaptureMode` / `SetMouseLockMode`; `FInputMode*::ApplyInputMode`. | Yes. |
| `SetIgnoreMoveInput` / `SetCinematicMode` | No native AST*/UST* caller; BP thunks only. | Unknown, logged now. |
| WASD action binding | `ASTPlayerController::SetupInputComponent` binds IA → `HandleInputMove` etc. (card 006). `HandleInputMove` (0x051CB140): `mov r8,[rcx+0x7C0]; test r8,r8; je ret` … `call [[r8]+0xC0]`. | Bound, but it returns early: `+0x7C0` is null on the client. |
| `+0x7C0` input receiver | Written only by `ASTPlayerController::OnPossess` (0x051D17C0): `+0x2F..+0x3F` `GetInterfaceAddress(pawn, Z_Construct_UClass_USTInputReceiverInterface_NoRegister())`; `+0x161` sets 0 when there is no pawn. Cleared by `OnUnPossess` (0x051D20C0) after `ReleaseActiveInputs`. | **No.** `OnPossess` needs authority (`AController::Possess`). |
| `+0x7D8` player character | `OnPossess+0x46..+0x71`: stored only if the pawn IsA `ASTPlayerCharacter` (super-chain check); never cleared for other pawns. | **No**, same reason. |
| Look | `HandleInputLook` (0x051CA640) → `AddYawInput`/`AddPitchInput` (vtbl +0xD30/+0xD28) unconditionally. | Yes, which is why look works. |

The drag-to-look / cursor-visible symptom fits a UI-only or game-and-UI input mode, or CommonUI config, left over from the menu flow. That decision lives in BP/CommonUI content, so the new log lines below are what will show it.

### Fix detail (`mod/src/hooks.cpp`, section "Client input receiver and client input diagnostics (card 008)")
`detour_pc_set_pawn(pc, pawn)` → original first. Then it acts only if all three hold:
- `is_net_client(pc)`
- pc IsA `ASTPlayerController`
- `GetLocalPlayer(pc) != nullptr`

When they hold, `sync_client_input_receiver`:
1. Reads the pawn back from `AController::Pawn` (`+0x2E8`), i.e. what SetPawn actually stored.
2. Computes the receiver as `GetInterfaceAddress(pawn, USTInputReceiverInterface)`, or null without a pawn, as `OnPossess+0x161` does.
3. If the old `+0x7C0` is non-null and differs from the new receiver, calls `ReleaseActiveInputs(pc)` first. It reads the old receiver, as in `OnUnPossess`.
4. Stores the receiver at `+0x7C0`.
5. Stores the pawn at `+0x7D8` only if it IsA `ASTPlayerCharacter`.
6. Logs, capped.

What it leaves alone:
- Host, standalone and the listen server's PCs for remote players are not `NM_Client`, so they are untouched.
- No game ownership changes. The fields are plain cached pointers that `OnPossess` writes the same way, and no original is skipped.
- `ASTPlayerController` has no `SetPawn` override; the publics hold only `AController`, `APlayerController` and `AAIController` `SetPawn`. So the hook on `APlayerController::SetPawn` (0x0405FD90) sees every call for the PC.

**Not mirrored (open):** `OnPossess` also does these, and the hook does not:
- sets the idle threshold `+0x7E8` (CharacterData `TimeBeforeIdle`) and resets `+0x7E0/+0x7E4`
- `SetGameplayTagCount` on the ASC
- user-settings work
- minimap `SetOriginActor`
- `OnUnPossess` also clears `+0x7A8`

If idle animations or the minimap misbehave on the client, look there next. Some of these are server-side state anyway.

### Hooks and calls added (RVAs from `tools/symbols.txt` → `offsets.generated.h`, PDB GUID checked at startup)

| Alias | RVA | Prologue (MinHook-safe) | Use |
|---|---|---|---|
| APlayerController_SetPawn | 0x0405FD90 | `mov rax,rsp; mov [rax+18h],rbx` | hook (fix) |
| AController_OnRep_Pawn | 0x03B35000 | `mov [rsp+18h],rbx; push …` | hook (log) |
| APlayerController_AcknowledgePossession | 0x04030800 | `mov [rsp+10h],rbx` | hook (log) |
| APlayerController_ClientRestart_Implementation | 0x0403C280 | `mov [rsp+10h],rbx` | hook (log) |
| APlayerController_SetInputMode | 0x0405F210 | `mov [rsp+8],rbx` | hook (log) |
| APlayerController_SetShowMouseCursor | 0x04060490 | `mov r8d,[rcx+54Ch]` (7 bytes) | hook (log) |
| AController_SetIgnoreMoveInput | 0x03B3E910 | `movzx eax,byte [rcx+339h]` (7 bytes) | hook (log) |
| IEnhancedInputSubsystemInterface_AddMappingContext | 0x04467980 | `test rdx,rdx; je rel32` (MinHook relocates the jcc) | hook (log) |
| UObjectBaseUtility_GetInterfaceAddress | 0x015706D0 | — | call |
| USTInputReceiverInterface_StaticClass (Z_Construct…NoRegister) | 0x0506AFB0 | — | call |
| ASTPlayerCharacter_StaticClass | 0x050847D0 | — | call |
| ASTPlayerController_StaticClass | 0x05084880 | — | call |
| ASTPlayerController_ReleaseActiveInputs | 0x051D98E0 | — | call |
| FName_GetDisplayNameEntry | 0x01334190 | — | call (pure; lazily inits FNamePool) |
| UWorld_GetFirstPlayerController | 0x0433FE20 | — | call (pure, weak ptr) |
| UWorld_GetGameViewport | 0x0433FE40 | — | call (via GEngine world context, ctx+0x200) |

`UGameEngine_Tick` stays last in `kHooks`. `UEngine::LoadMap` and `Tick` gained calls into the section:
- LoadMap: session flag on entry, snapshot arming on return.
- Tick: `if (g_snapshot_armed) client_snapshot_step(self)`, one load and one branch per frame when idle.

### Offset evidence

| Field | Offset | Read off |
|---|---|---|
| `AController::Pawn` | +0x2E8 | `AController::SetPawn` 0x03B3F680: `lea rdi,[rcx+2E8h]` … `mov [rdi],rbx` |
| `IgnoreMoveInput` (uint8 counter) | +0x339 | `SetIgnoreMoveInput` 0x03B3E910 adds `2*b-1`, clamped ≥0; `IsMoveInputIgnored` 0x03B33900 |
| `IgnoreLookInput` | +0x33A | `IsLookInputIgnored` 0x03B338F0 |
| `bShowMouseCursor` | bit 0 of dword +0x54C | `SetShowMouseCursor` 0x04060490 |
| `ASTPlayerController` input receiver | +0x7C0 | `OnPossess+0x3F` store, `HandleInputMove` load |
| `ASTPlayerController` player character | +0x7D8 | `OnPossess+0x71` store |
| `UGameViewportClient` MouseCaptureMode | byte +0x3BA | `SetMouseCaptureMode` 0x03C6C060. Cross-check: virtual `GetMouseCaptureMode` 0x0190BD30 reads +0x392 off the `FViewportClient` subobject at +0x28 (0x392+0x28 = 0x3BA). |
| `UGameViewportClient` MouseLockMode | byte +0x3BC | `SetMouseLockMode` 0x03C6C070 |
| `UObjectBase` NamePrivate | +0x18 | `UObjectBase::GetFName` 0x01232190 |
| FNameEntry | header uint16 (bit0 wide, len = hdr>>6), chars at +2 | `FNameEntry::GetPlainNameString` 0x013343D0 |

FInputMode vtables are hardcoded with a PDB comment; `pdb_rva.py` rejects non-function publics, and the build identity is already checked:
- `??_7FInputModeGameOnly@@6B@` 0x7B26340
- `GameAndUI` 0x7B26328
- `UIOnly` 0x7B26310
- (`FInputModeDataBase` 0x713C8B8 is not expected at runtime)

Anything else logs as `other`.

### New log lines
All are client only. Each counter logs its first 16 events, then once `diag: <what> ... further events not logged`. Names are the FName text (no number suffix); non-printable characters show as `?`.

- `client-input: SetPawn(pc=…, pawn=… <pawn class>): input receiver <old> -> <new>, player character <old> -> <new>`. This is the fix firing. Expect new = non-null and character = the pawn after spawn.
- `possess: OnRep_Pawn|AcknowledgePossession|ClientRestart(pc=…, arg=…) local player …, Pawn … <class>, input receiver …, ignore move N look N`. Logged after the original returns.
- `input-mode: SetInputMode(pc=…, GameOnly|GameAndUI|UIOnly|other, vtable exe+0x…) caller exe+0x…, bShowMouseCursor N`
- `input-mode: SetShowMouseCursor(pc=…, 0|1) bShowMouseCursor A -> B, caller exe+0x…`
- `input-mode: SetIgnoreMoveInput(controller=… <class>, 0|1) IgnoreMoveInput A -> B, caller exe+0x…`
- `input-mode: AddMappingContext(iface=…, context=… <IMC name>, priority N) caller exe+0x…`. Logged only while the last LoadMap joined a server. A caller outside the exe (this mod's card 006 path) is printed as `exe+0x0 (not the exe: this mod)`.
- `diag: client map loaded; input snapshot in 10000 ms`. After a successful LoadMap with a pending net game.
- Snapshot, 10 s later, once per client map load:
  - `snapshot: pc=… <class>, net client 0|1, local player …, Pawn … <class>`
  - `snapshot: IgnoreMoveInput N, IgnoreLookInput N, bShowMouseCursor N`
  - `snapshot: input receiver …, player character …` (only if the PC is an ASTPlayerController)
  - `snapshot: viewport client … MouseCaptureMode N, MouseLockMode N`
  - or `snapshot: no game world` / `world has no PlayerController` / `no game viewport client`

  The capture/lock values are raw. Assumed UE enum order, not proven from this build: capture 0 NoCapture, 1 CapturePermanently, 2 CapturePermanently_IncludingInitialMouseDown, 3 CaptureDuringMouseDown, 4 CaptureDuringRightMouseDown; lock 0 DoNotLock, 1 LockOnCapture, 2 LockAlways, 3 LockInFullscreen.

"caller" is the return address minus the exe base. For a BP thunk this is the native exec thunk, not the Blueprint itself.

### Self-review (attacks tried)
- **SetPawn before the local player exists** (spawn order): skipped by the LocalPlayer test. `ClientRestart_Implementation` calls `SetPawn` again later, so the sync still runs.
- **SetPawn(null)** on unpossess or pawn death: receiver becomes null and `ReleaseActiveInputs` runs once on the old receiver while the pawn object is still alive (GC is later). `+0x7D8` is kept, as on the server.
- **Mech or other non-character pawn:** the receiver follows the pawn, and `+0x7D8` keeps the character, which is what `OnPossess` does.
- **Same pawn set twice:** receiver unchanged, so no release.
- **Null `AddMappingContext` context:** the name prints as `null`.
- **Null pawn or object in the name helpers:** handled.
- **Name length over 63:** truncated.
- **Log volume:** none per frame. Counters saturate at 17.
- **Threading:** all state is game thread only. The snapshot flag is written in the LoadMap/Tick detours.

Residual risks:
- `ReleaseActiveInputs` was only read statically. On the server it runs during `OnUnPossess`; whether its receiver calls do anything server-only on a client is unknown.
- The viewport enum order is assumed.

### Out of scope, noted
Neither player sees the other's body, only the shadow blob. Nothing found on it in this pass, and no static lead was pursued.

## Senior review (2026-11-02)
Accepted. The senior confirmed that `HandleInputMove` returns early when the receiver at `+0x7C0` is null, and that `ASTPlayerController::OnPossess` stores `+0x7C0` (from `GetInterfaceAddress`) and `+0x7D8` (after an inline IsA check). The rebuild is clean (84 exports, imports only from `KERNEL32.dll`) and is deployed.

Runtime:
- The `SetPawn` sync set the client's receiver from null to the `BP_PlayerCharacterMI_C` interface. `IMC_MI_KBM`, `IMC_MI_Gamepad`, `IMC_Default_Gamepad` and `IMC_Default_KBM` were added. The user confirmed that **WASD now works on the client**.
- The snapshot showed `IgnoreMoveInput 0`, `IgnoreLookInput 0` and `bShowMouseCursor 0`, but the viewport had `MouseCaptureMode 3` (CaptureDuringMouseDown) and `MouseLockMode 1`.
- No `SetInputMode` call was logged on the client after the map load. The cursor is still visible, and the mouse turns the camera only while a button is held.

Card 009 covers the input mode.

One gap: the `OnPossess` idle-timer reset (`+0x7E0`/`+0x7E4`) is not mirrored, which does no harm on the client.
