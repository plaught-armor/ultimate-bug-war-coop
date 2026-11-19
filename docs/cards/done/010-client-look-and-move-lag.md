# Card 010: Client mouse look is dead, and client WASD lags

Read `docs/STATE.md` first: the "Mod code rules" section, plus the rows and results for cards 008 and 009. Then read the reports and senior reviews in cards 008 and 009, which own the client input code and logging you will extend. Repo root: `/path/to/ultimate-bug-war-coop`.

## Why
The card 009 run used `UBWCOOP_CLIENT_GAMEONLY=1` on the client. With it, the client's viewport has the same mouse capture and lock as the host's: capture 1 (CapturePermanently_IncludingInitialMouseDown), lock 1, `bShowMouseCursor 0`. Its input receiver and the four input mapping contexts are set (cards 008 and 009). Even so, the user reports:
1. **The client still cannot use the mouse.** The user confirms the cursor is hidden and the camera does not turn, so mouse capture works and the look chain fails after it.
2. **WASD on the client lags**: the response looks like network latency, not local input.

The host is fine on both counts. Both instances run on one machine, so real network latency is near zero.

Static facts already established by the senior:
- `ASTPlayerController::HandleInputLook` (0x051CA640) has no early return. It stores the look value at PC+0x788/+0x790. Only if the receiver at +0x7C0 is set does it run the extra block that uses +0x8C8/+0x8C0 and resets the idle timer at +0x7E0/+0x7E4. It then always calls PC vtbl +0xD30 and +0xD28 with the float X and Y. Identify these two slots (expected: `AddYawInput`/`AddPitchInput`, which feed `RotationInput` at +0x528/+0x530/+0x538 per card 007).
- The host's game-only mode comes from a Blueprint `SetInputMode_GameOnly` (exe+0x364BFF0). Whatever server-only Blueprint event calls it may also do other setup the client never gets.

## Do
1. **Mouse look: find where the chain breaks.** The chain is: raw mouse → Enhanced Input IA_Look → `HandleInputLook` → `RotationInput` → `UpdateRotation`/`ProcessPlayerInput` → `ControlRotation` → camera. Statically check each link for anything that is null or server-only on a net client's local PC, using the same style as card 008. Candidates:
   - whether IA_Look is bound in the mapping contexts the client has;
   - `UpdateRotation` / `ProcessPlayerInput` in an `ASTPlayerController` override;
   - a PlayerCameraManager, or a camera that reads a value set only in `OnPossess`;
   - pawn `bUseControllerRotation*` / spring arm `bUsePawnControlRotation`;
   - the server replicating rotation back over the client's own (`ClientSetRotation`, the replicated pawn rotation, or `ControlRotation` corrections).
2. **WASD lag: find why the client does not predict.** Check:
   - the client pawn's `LocalRole` and `RemoteRole`: the expected value is AutonomousProxy (2) for the client's own pawn;
   - whether movement goes through `UCharacterMovementComponent` prediction (`ServerMove*`) or through a custom server RPC;
   - `NetUpdateFrequency` and the network smoothing mode.

   A pawn that is SimulatedProxy on its owning client, or movement that is server-authoritative only, would explain the lag.
3. **Runtime diagnostics, client local PC only.** Every log must be capped, or rate-limited to at most 1 line per second, and read-only:
   - `HandleInputLook`: entry count and the last X/Y value, logged at most once per second while it fires;
   - once per second for about 20 s after the client's map load: PC `ControlRotation`, `RotationInput`, the pawn's actor rotation, the pawn's `LocalRole`/`RemoteRole`, and the CMC `NetworkSmoothingMode` (or whatever your static work shows matters);
   - any `ClientSetRotation` / `SetControlRotation` call on the client PC, with its caller.

   This is how the senior's next run will show whether look input arrives, whether rotation changes, and whether something overwrites it.
4. **Fix only what is proven statically**, the same way as card 008: mirror the missing server-only setup for the client's local PC, at the point where the host gets it. If neither cause is proven, ship only the diagnostics.
5. **Do not regress cards 006 to 009.** The `UBWCOOP_CLIENT_GAMEONLY` fallback stays, and stays off by default.
6. **Build and prove.** Use the build command in STATE.md. Show:
   - a clean `/W4 /WX` build;
   - 84 exports;
   - imports only from `KERNEL32.dll`;
   - the RVAs, the prologues of every hooked function, and the offset evidence;
   - what each new log line will say.

   Deploy is the senior's job.

## Constraints
- Follow STATE.md's "Mod code rules": no heap use, every resource released, no stored game pointers beyond the current call, a skipped original must not leak, ≤60-line functions, bounded loops, capped logging, shared state touched only on the game thread.
- Do not launch the game, and do not touch `game/` (except reading the exe) or `logs/`. Under `prefix/`, only read the crash dumps.
- No subagents. No commits. If a delete or other action is denied, say so in the report; don't ask the senior to do it for you.

## Report
### Summary
Diagnostics only. Nothing about either symptom is proven statically, so no behaviour changes.
Six new log-only hooks plus a 20-row, 1 Hz client report. They tell apart the candidate breaks in
the look chain and show whether the client predicts movement or only follows the server. Build:
clean `/W4 /WX` (compiler and linker), 84 exports, imports `KERNEL32.dll` only. Cards 006–009 code
is untouched apart from the `object_name` refactor below. `UBWCOOP_CLIENT_GAMEONLY` stays, default off.

### Static findings: mouse look
The chain, each step read from this build:
1. `ASTPlayerController::HandleInputLook` (0x051CA640) reads FInputActionValue X `[rdx]` and Y `[rdx+8]`
   as doubles and stores them at PC+0x788/+0x790. It then calls vtbl+0xD30 `APlayerController::AddYawInput`
   (0x040330E0) with X and vtbl+0xD28 `AddPitchInput` (0x04031A40) with Y. Slots were checked in the
   `ASTPlayerController` vtable at 0x080AC900.
2. AddYawInput adds 0 when vtbl+0x868 `AController::IsLookInputIgnored` (0x03B338F0, not overridden;
   reads IgnoreLookInput +0x33A) is true. Otherwise it scales by InputYawScale (+0x540) only when the
   UInputSettings legacy flag (+0x48 & 0x40) is set, and adds to RotationInput.Yaw (+0x530).
3. `APlayerController::PlayerTick` (0x040566A0) runs TickPlayerInput first. Then it calls vtbl+0xC18
   UpdateRotation only if all of these hold:
   - Player (+0x348) is set.
   - Player->PlayerController (+0x30) == this.
   - Either StateName (+0x2E0) == Playing and Pawn (+0x2E8) == AcknowledgedPawn (+0x350),
     or StateName == Inactive and !IsFrozen.
4. `ASTPlayerController::UpdateRotation` (0x051EA5E0) does two things:
   - With a receiver at +0x7C0 (card 008), it routes through `ASTPlayerCharacter::HandleInputLookHorizontal/Vertical`.
     Those are thunks to `APawn::AddControllerYaw/PitchInput`, which call AddYawInput again for a locally
     controlled pawn.
   - It then tail-calls `APlayerController::UpdateRotation` (0x040686E0). That function reads
     RotationInput, gets the view rotation from GetControlRotation and passes it through
     `PlayerCameraManager(+0x360)->ProcessViewRotation`. At +0x1A6 it calls SetControlRotation
     (vtbl+0x798 = `AController::SetControlRotation` 0x03B3D1C0, not overridden; return site 0x0406888C).
     Finally it calls the pawn's FaceRotation.

None of this differs between the client and the host in code. The card 008 run showed this chain
working after input arrived: with capture 3, the mouse turned the camera while a button was held. In
the card 009 run (GameOnly fallback, capture 1) look is dead. So the break is either:
- (a) mouse axis events not reaching the engine input path,
- (b) HandleInputLook firing but RotationInput staying 0 (IgnoreLookInput, or scale 0),
- (c) UpdateRotation not running (a PlayerTick gate above fails), or
- (d) SetControlRotation running but something else (ClientSetRotation or another caller) resetting it.

The new lines separate (a)–(d). One more static fact: the game's viewport class may be
`UCommonGameViewportClient`. Its `InputAxis` (0x04C259D0) offers each axis event to CommonUI's
override handler first. It calls `UGameViewportClient::InputAxis` (the hooked function) at +0x17B
only when that handler does not take the event. So "InputAxis 0 while the mouse moves" means CommonUI
consumes the events, or they never arrive.

### Static findings: WASD lag
- `APawn::PossessedBy` (0x03F69410) on the server sets the Controller (+0x2D8) and SetReplicates(true).
  For a non-local controller it calls SetAutonomousProxy. That is stock engine code, so the client
  pawn should be LocalRole 2 (AutonomousProxy).
- `ASTPlayerCharacter::PossessedBy` (0x051D7A10) adds UpdateAbilitiesFromSettings and SquadLeader
  SetEnabled. Both are server-only, so client and server can simulate the pawn differently. That makes
  frequent `ClientAdjustPosition` corrections a candidate.
- The client CMC predicts through `ReplicateMoveToServer` (0x03A48200) and `CallServerMovePacked`
  (0x03A1EF20). Neither is overridden: `UCharacterMovementComponent` vtable 0x07955480, slots
  0xA70/0xA88/0xB18/0xA68, same in `USTPlayerMovementComponentBug` 0x080B73B8.
- `SmoothClientPosition` (0x03A55810) smooths only if byte CMC+0x233 (NetworkSmoothingMode) != 0
  and the owner's LocalRole == 1 or RemoteRole == 2.
- The game also has `USTMovementControlComponent` / `USTGroundedMovementControl` classes, which may
  drive movement outside the CMC. Not traced. The CMC class name is now logged so a game subclass
  would show.

Hypotheses (unproven, the rows decide):
1. The pawn is not AutonomousProxy on the client: ReplicateMoveToServer 0/s, so movement comes only
   from replicated server position.
2. The pawn is autonomous but the server corrects it constantly: ClientAdjustPosition many/s, with the
   logged distance.
3. Neither of those: smoothing, or the game's movement-control components. Next card.

### Code changes
- `tools/symbols.txt`: six symbols (card 010 block).
- `mod/src/hooks.cpp`:
  - `fname_text(const void* fname)` split out of `object_name` so StateName can be printed;
    `object_name` behaviour is unchanged.
  - `#include <math.h>` (`atan2`, `sqrt`; static CRT, no new import).
  - New card 010 section (before the GameOnly fallback). Every detour calls its original exactly once,
    with the same arguments. Nothing is skipped, and nothing is allocated or kept across frames.
    Counters are game thread only.
  - Hooks (all inserted before UGameEngine::Tick in `kHooks`):
    - `ASTPlayerController::HandleInputLook`: count, plus a line at most once per second (60 lines max).
    - `UGameViewportClient::InputAxis`: count.
    - `AController::SetControlRotation`: count calls from the UpdateRotation site; log other callers (16 max).
    - `AController::ClientSetRotation_Implementation`: log (16 max).
    - `UCharacterMovementComponent::ClientAdjustPosition_Implementation`: count, and log 16 with
      server vs client location.
    - `UCharacterMovementComponent::ReplicateMoveToServer`: count.
  - Every hook is active only while `g_client_session`. SetControlRotation also requires an
    `ASTPlayerController`.
  - `rotation_report_step`, driven from `detour_tick`: 20 rows, 1 s apart, starting 1 s after a
    client LoadMap. Each row's counters are reset after it. It is disarmed at every LoadMap enter and
    armed only for a client load.
  - `resolve_update_rotation_site()` in install: checks the bytes before 0x0406888C are
    `ff 90 98 07 00 00`. This is diagnostic only: a mismatch logs an ERROR and every call counts as "other".
  - `detour_tick` gained one branch (`g_rot_rows_left != 0`).

### RVAs and prologues (all relocatable for MinHook, no RIP-relative bytes in the first 5)
| Hook | RVA | Prologue |
|---|---|---|
| ASTPlayerController::HandleInputLook | 0x051CA640 | `push rbx; sub rsp,50h; movaps [rsp+30h],xmm7` |
| UGameViewportClient::InputAxis | 0x03C5CCA0 | `push rbp; push rbx; push rsi; lea rbp,[rsp-47h]; sub rsp,0D0h` |
| AController::SetControlRotation | 0x03B3D1C0 | `mov [rsp+8],rbx; push rdi; sub rsp,0A0h` |
| AController::ClientSetRotation_Implementation | 0x03B1E2E0 | `mov [rsp+8],rbx; push rdi; sub rsp,40h` |
| UCharacterMovementComponent::ClientAdjustPosition_Implementation | 0x03A215A0 | `push rbp; push rsi; push rdi; push r12; push r15; lea rbp,[rsp-20h]` |
| UCharacterMovementComponent::ReplicateMoveToServer | 0x03A48200 | `push rbp; push rbx; push rdi; push r15; lea rbp,[rsp-708h]` |

ABI notes:
- By-value FRotator and FVector arrive as pointers to the caller's copy, and are passed through unchanged.
- ClientAdjustPosition has ten parameters. The FName by value is 8 bytes (one slot), followed by
  bool, bool, uint8 and the TOptional pointer. The function reads its fifth argument at `[rbp+0x70]`
  and its tenth at `[rbp+0x98]`, which matches.

### Layout evidence (new offsets)
| Offset | Field | Evidence |
|---|---|---|
| PC+0x2E0 | StateName | AController::IsInState 0x03B338A0 |
| PC+0x320 | ControlRotation | GetControlRotation 0x03B254D0 |
| PC+0x348 / +0x350 | Player / AcknowledgedPawn | PlayerTick |
| PC+0x360 | PlayerCameraManager | APlayerController::UpdateRotation+0x3F |
| PC+0x528 | RotationInput | AddYawInput adds at +0x530 |
| Player+0x30 | PlayerController | PlayerTick |
| Pawn+0x2D8 | Controller | APawn::PossessedBy |
| Actor+0x60 / +0x168 | RemoteRole / LocalRole (bytes) | `movzx eax, byte [rcx+0x60]` / `[rcx+0x168]` in execGetRemoteRole 0x037FDF70 / execGetLocalRole 0x037FDC20 |
| Actor+0x1B8 | RootComponent | execK2_GetActorLocation 0x03800370 +0x19 |
| Root+0x1E0 | world rotation quat | execK2_GetActorRotation 0x038003F0 |
| Root+0x200 | world translation | execK2_GetActorLocation +0x25 `movups xmm0,[rax+0x200]` |
| Character+0x328 | CharacterMovement | ACharacter::PossessedBy 0x039BC520 +0xE |
| CMC+0x198 | CharacterOwner | SmoothClientPosition +0x27 |
| CMC+0x233 | smoothing mode byte | SmoothClientPosition +0x1E `cmp byte [rbx+0x233],0`. The field name is inferred from the engine source. |

### New log lines (client session only)
- `look: HandleInputLook(pc=%p) x<N> since last line, last value X <x> Y <y>; RotationInput after P Y R, IgnoreLookInput <u>`
  - At most 1/s, 60 lines, then `look: HandleInputLook ... further lines not logged`.
- `look: SetControlRotation(pc=%p) caller exe+0x<rva>: P Y R -> P Y R`
  - Callers other than UpdateRotation only. 16 lines, then `diag: SetControlRotation ... further events not logged`.
- `look: ClientSetRotation_Implementation(controller=%p <class>, P Y R, reset camera <d>) caller exe+0x<rva>`
  - 16 lines, then the same capped notice.
- `move: ClientAdjustPosition(cmc=%p, owner %p, timestamp, mode, base relative) server (x,y,z), client (x,y,z), distance <d>`
  - 16 lines, then the capped notice.
- `rot[k]: pc %p state <StateName>, Pawn==AcknowledgedPawn d, Player->PlayerController==pc d, camera manager %p <class>, IgnoreLookInput u, ControlRotation P Y R`
- `rot[k]: since last row: HandleInputLook n, InputAxis n, SetControlRotation n from UpdateRotation + n other, ReplicateMoveToServer n, ClientAdjustPosition n`
- `rot[k]: pawn %p <class>, LocalRole u RemoteRole u, Controller==pc d, yaw <deg>, location (x,y,z), CMC %p <class> NetworkSmoothingMode u` (or `rot[k]: no pawn`)
- `rot[k]: no game world or PlayerController`
- `ERROR hooks: APlayerController::UpdateRotation+0x1A6 is not ...`
  - Only if the site check fails.
- `hooks: <alias> hooked at %p` for the six new hooks.

### How to read the run (move the mouse and hold W during the 20 rows)
| Rows show | Meaning |
|---|---|
| InputAxis 0 | mouse events never reach the engine path, or CommonUI eats them: case (a) |
| InputAxis > 0, HandleInputLook 0 | Enhanced Input mapping / IA_Look not triggering |
| HandleInputLook > 0 but RotationInput after == 0 | IgnoreLookInput or scale: case (b) |
| SetControlRotation from UpdateRotation 0 | UpdateRotation gate fails; the state / Pawn==AcknowledgedPawn / Player fields show which: case (c) |
| ControlRotation changes but snaps back, `other` callers or ClientSetRotation lines | case (d) |
| ControlRotation turns but view doesn't | camera manager / view target. Next card. |
| LocalRole != 2 | hypothesis 1 |
| ReplicateMoveToServer ≈ frame rate and ClientAdjustPosition many/s with large distance | hypothesis 2 |

### Suggested test (senior)
Same two-instance run as card 009. Run once with `UBWCOOP_CLIENT_GAMEONLY` unset, and once with it
set to 1. Then grep the client log for `look:`, `move:` and `rot[`.

### Risks / open
- Hook count is now 30. SetControlRotation and ReplicateMoveToServer run every frame on the client.
  The per-call overhead in a client session is an `IsA` check and a counter.
- The ClientAdjustPosition location comparison is meaningless when `base relative 1` (NewLoc is
  relative to the base).
- Not traced: the PlayerCameraManager class and any AST override of ProcessViewRotation, the game's
  movement-control components, and NetUpdateFrequency. The row's camera-manager class name will tell
  whether the first is worth tracing.


## Senior review (2026-11-02)
Accepted as diagnostics.
- Six hook prologues re-disassembled; they match the report.
- Clean rebuild: 84 exports, imports only from `KERNEL32.dll`. Deployed.

The run used `UBWCOOP_CLIENT_GAMEONLY=1` on the client. Logs are in `logs/card010-*`. Both instances exited 0, with no new crash dump.

Results, `rot[0..19]` on the client:
- **Look, case "input arrives, action never fires".** `InputAxis` counts 76–234/s while the user moved the mouse, but `HandleInputLook` is **0 in every row**. `ControlRotation` stays at Y 90 the whole time, set once by `ClientSetRotation` at spawn. `SetControlRotation` runs about 120/s, only from `UpdateRotation`, with no stray writer. Raw mouse axes reach the viewport and PlayerInput, but no IA_Look handler fires. WASD's `HandleInputMove` does fire, so the input stack and mapping contexts work for movement.
- **Lag, hypothesis 2: server corrections.** The pawn is `LocalRole 2` (AutonomousProxy), `RemoteRole 3`. `ReplicateMoveToServer` runs at about 120/s, and `ClientAdjustPosition` arrives about 19–20/s while moving, 0 when idle. Each correction puts the server position about 30–35 units behind the client's: the server lags the client's prediction by about one correction interval. Prediction runs, but the server disagrees on every move, so it keeps snapping the client back.
- **Diagnostic bug.** The CMC column reads `null` / smoothing `255`, but `ClientAdjustPosition` shows the CMC at a non-null address owned by the same pawn. So `kCharacterMovement = 0x328` is wrong, or is read from the wrong object. Fix it in card 011.
