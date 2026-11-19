# Card 013: The client's local move has zero displacement; IA_Look never triggers on the client

Read `docs/STATE.md` first: the "Mod code rules" section and rows 010 to 013. Then read card 012 in full, including the senior review and its run results. The diagnostics you will extend are in `mod/src/hooks.cpp`. Repo root: `/path/to/ultimate-bug-war-coop`. Run logs: `logs/card012-{host,client}.log`.

## Why
Card 012's run narrowed both client faults to one function each.

**A. Movement.** On the client, `UCharacterMovementComponent::PerformMovement` (0x03A3B460) runs 120 times per second for the own pawn, called from `ReplicateMoveToServer` (exe+0x3A48912). **Every call leaves the pawn where it was**, even while W is held and the move sent to the server carries |a| 4096. The root is the UpdatedComponent and is Movable (Mobility 2), and nothing writes the location back between moves. The server, given the same input, moves the pawn, so it corrects every move. The early-out or the zero displacement is inside `PerformMovement` or the physics it calls.

Candidates, in stock UE5 order. Prove each against this exe, do not assume:
- `PhysWalking` returns at once when `!UpdatedComponent->IsQueryCollisionEnabled()`: the client capsule's collision may be off. The server's would be on.
- `PhysWalking` zeroes Acceleration and Velocity when the character has no Controller, unless the character is a simulated proxy. The `rot` rows say `Controller==pc 1`, but check what the CMC reads.
- `deltaTime < MIN_TICK_TIME`, or a time step that rounds to zero.
- Root motion, or an override velocity, that replaces the velocity with zero.
- A blocking hit at the start of every move: the capsule starts penetrating something that exists only on the client, so `SafeMoveUpdatedComponent` resolves to no movement.
- The movement mode or the physics function the client actually reaches (`StartNewPhysics`, `PhysWalking`, `MoveAlongFloor`, `PhysFalling`).

**B. Look.** The client applies `IMC_Default_KBM` at the same priority as the host, and no context has a higher priority. That context holds the only mouse mapping, `IA_Look <- Mouse2D` (triggers 0, modifiers 2). `UEnhancedPlayerInput::InputKey` sees MouseX and MouseY about 62 times per second. Still, `HandleInputLook` never runs, while `HandleInputMove` does. So Enhanced Input evaluates the mapping to no Triggered event. Candidates:
- the mapping's 2 modifiers (card 011 names `AimSensitivity` 0x0512E070, `AimInversion` 0x0512DFD0 and `TimeDilationLookScale` 0x0512E260) return zero on the client, for example a sensitivity or time-dilation value read from an object that only exists, or only has a value, on the server or in the host's local settings;
- the action's own modifiers or triggers;
- the Mouse2D pairing from MouseX/MouseY.

## Do
1. **A. Static read.** Walk `PerformMovement` 0x03A3B460 to the physics function the walking mode reaches, then `MoveAlongFloor` and `SafeMoveUpdatedComponent`. List every early return, with the field and offset it tests. For collision, prove how the exe reads `IsQueryCollisionEnabled` for the capsule (the BodyInstance or collision-enabled byte, with its offset).
2. **A. Diagnostics, client own pawn only.** Aggregate per second into the `rot[k]` rows:
   - the counts of the physics functions reached (at least `StartNewPhysics` or the walking function, and `SafeMoveUpdatedComponent` / `MoveUpdatedComponent`), so the first one that stops being reached is visible;
   - the delta time each received (min and max);
   - the capsule's collision-enabled value, and the CMC's view of the Controller;
   - for `SafeMoveUpdatedComponent` / `MoveUpdatedComponent`: the requested delta length, whether it returned a blocking hit, `bStartPenetrating`, and the hit actor's name and component name (the last one, or the most frequent);
   - Velocity and Acceleration before and after one `PerformMovement` call in each second.

   Log the same counts on the host for the remote client's pawn, as the control.
3. **B. Static read and diagnostics.** Find how Enhanced Input evaluates a mapping: the key-state value for Mouse2D, the mapping modifiers, the action modifiers and triggers, then the trigger state that fires the Triggered binding. Then log, per second on both sides:
   - the raw Mouse2D value before modifiers (or the paired MouseX/MouseY values that make it), and the value after each modifier of the IA_Look mapping, naming the modifier class;
   - IA_Look's final value and trigger state, next to IA_Move's.

   If a modifier is the cause, log the inputs it reads (for example a sensitivity value or a time-dilation source) and where it gets them.
4. **Fix only what is proven statically**, as in cards 008 and 009: mirror the missing setup on the side that lacks it, at the point where the other side gets it. Examples:
   - client capsule collision off because a server-only path enables it: enable it at the equivalent client point;
   - a look modifier reading a value only the server or the host sets: give the client the same value where the host gets it.

   Do not disable server movement checks (no `bIgnoreClientMovementErrorChecksAndCorrection`, no `bServerAcceptClientAuthoritativePosition`). Do not add smoothing or interpolation to hide corrections; the client's own move must work. If nothing is proven, ship only the diagnostics.
5. **Do not regress cards 006 to 012.** The `UBWCOOP_CLIENT_GAMEONLY` fallback stays as it is.
6. **Build and prove.** Use the build command in STATE.md. Show:
   - a clean `/W4 /WX` build;
   - 84 exports;
   - imports only from `KERNEL32.dll`;
   - the RVAs and prologues of every new hook;
   - the offset evidence;
   - what each new log line will say, and a decision table for the next run.

   Deploy is the senior's job.

## Constraints
- Follow STATE.md's "Mod code rules": no heap use, every resource released, no stored game pointers beyond the current call, a skipped original must not leak, ≤60-line functions, bounded loops, capped or 1/s logging, shared state touched only on the game thread.
- Hot-path hooks (per move, per sweep, per key, per modifier) do no work unless the target is the own pawn, its CMC, or the local player input, and never log per call.
- Do not launch the game, and do not touch `game/` (except reading the exe) or `logs/` (except reading the card 010 to 012 logs). Under `prefix/`, only read the crash dumps.
- No subagents. No commits. If a delete or other action is denied, say so in the report; don't ask the senior to do it for you.

## Report
(append below)

### Summary

Static work is done and diagnostics for both questions are built; no fix is applied. Static
reading finds one concrete candidate for the zero-move bug that matches card 012's evidence.
`PhysWalking` returns early, without touching Velocity, when the capsule's effective
`GetCollisionEnabled()` has no query bit. The dropship path (`EnterDropship`) calls
`SetActorEnableCollision(false)` on the character. Only `DescendToLandCompleted` turns it back
on, and it does so only when `+0x749` is set and a local `ASTPlayerController` exists. That
fits "Velocity 700 frozen from ClientAdjustPosition, Acceleration -4096, did not move". It is
not proven: nothing in the card 010–012 logs records collision state, so the fix waits for one
run of this build. For look, the static reading shows that `Mouse2D` never gets its own
`InputKey` call (the 0 count is normal). The new rows show at which stage IA_Look's value dies.

### A. Static: PerformMovement down to the component move

All RVAs are image-relative (base 0x140000000) and come from `pdb_rva.py gen`. Offsets are the
instruction offsets inside each function.

| Step | RVA | Early returns that skip the move (offset) |
|---|---|---|
| `UCharacterMovementComponent::PerformMovement` | 0x03A3B460 | `HasValidData` (vt+0x668) false or no world → +0x756; MovementMode (+0x231) == 0, UpdatedComponent (+0xB8) Mobility (+0x1A3) != Movable(2), UpdatedComponent `IsSimulatingPhysics` (comp vt+0x4E0) → +0xDD3. Otherwise `StartNewPhysics(dt, 0)` via vt+0x6D0 at +0x71F |
| `StartNewPhysics` | 0x03A57470 | dt < MIN_TICK_TIME (1e-6), Iterations >= MaxSimulationIterations (+0x3E4), !HasValidData, IsSimulatingPhysics. Then switch on MovementMode: Walking → vt+0x8F8 `PhysWalking` |
| `PhysWalking` | 0x03A40290 | dt < MIN_TICK_TIME; CharacterOwner null → zero Velocity/Acceleration; Controller (+0x2D8) null and !bRunPhysicsWithNoController (CMC+0x2E9 bit 0x8) and no root motion (CMC+0xF20) and no override velocity and LocalRole != SimulatedProxy → zero and return; **UpdatedComponent `GetCollisionEnabled()` (vt+0x5C8) not in {1 QueryOnly, 3 QueryAndPhysics, 5 QueryAndProbe} → `SetMovementMode(Walking)` and return at +0xD81, Velocity left as is** |
| PhysWalking loop | — | `CalcVelocity` 0x03A1DB90 (slot 0x758), `MoveAlongFloor` 0x03A380A0 (slot 0x930), `FindFloor` 0x03A2B2A0 (slot 0x968), StartFalling/CheckFall (0x680/0x878), RevertMove 0x03A498A0 |
| `MoveAlongFloor` | 0x03A380A0 | `(CurrentFloor flags dword CMC+0x438 & 3) != 3` (bBlockingHit && bWalkableFloor) → return (+0x1E..+0x3A → +0x606) |
| `UMovementComponent::SafeMoveUpdatedComponent` | 0x03AA3860 | UpdatedComponent null → OutHit init (Time 1.0, MyItem −1 at +0xA4, InstanceIndex −1 at +0xD0), return false. Otherwise `MoveUpdatedComponent`; at +0x150 tests OutHit+0xAD bit 1 (bStartPenetrating; bit 0 bBlockingHit) for the depenetration retry |
| `UMovementComponent::MoveUpdatedComponentImpl` | 0x03A9EB50 | UpdatedComponent null → false; otherwise ConstrainDirectionToPlane (vt+0x5B8), then `UpdatedComponent->MoveComponentImpl` (comp vt+0x570) with MoveComponentFlags (+0xC8) |

How `IsQueryCollisionEnabled` is read: `UPrimitiveComponent::GetCollisionEnabled` (0x040972B0)
loads owner = comp+0xA0. If an owner exists and owner+0x5D bit 0 (`bActorEnableCollision`) is
clear, it returns 0 (NoCollision). Otherwise it returns the byte at comp+0x38F
(`BodyInstance` at +0x378, `.CollisionEnabled`). So `SetActorEnableCollision(false)` alone is
enough to make PhysWalking stop without changing Velocity.

Collision writers found:
- `AActor::SetActorEnableCollision` 0x037F2280, called from blueprints through
  `execSetActorEnableCollision` 0x03802210.
- `UPrimitiveComponent::SetCollisionEnabled` 0x040A6F50.
- `ASTPlayerTransportDropship::EnterDropship` 0x051C56C0. It sets +0x749 = 1, possesses the
  dropship, attaches the character, saves and zeroes the character's CMC+0x1A0, and calls
  `SetActorEnableCollision(false)`. Its callers are `BeginPlay`+0x50, `OnInteraction`+0x3A and
  `ActorFullyLoaded`+0x1F. The `BeginPlay` condition has no authority check, so it can run on
  the client as well.
- `DescendToLandCompleted` 0x051C2D10 (virtual, no direct callers). It runs only if +0x749 is
  set and `GetSTPlayerController(world)` is non-null. It detaches, restores CMC+0x1A0,
  teleports, possesses the character, calls `SetActorEnableCollision(true)` and clears +0x749.

### A. Diagnostics added

The watched pawn is chosen by `watched_pawn`:
- client: CharacterOwner LocalRole == AutonomousProxy (2);
- host: LocalRole == Authority (3) and RemoteRole == AutonomousProxy (2), i.e. the remote
  client's pawn. This is the control.

`detour_perform_movement` now runs on both sides. It sets `g_move_cmc` only for the extent of
the original call, so the hooks below it do work only for `this == g_move_cmc` and keep no
pointer past the call. Card 012's `note_local_move` stays client-only.

Hooks below PerformMovement (counts per row): StartNewPhysics (count, dt min/max), PhysWalking
(count), SafeMoveUpdatedComponent (count, false returns, requested |Delta| sum/max, blocking
and start-penetrating counts, last blocking hit component and owner via `FWeakObjectPtr::Get`
on Hit+0xD8 and component+0xA0), and MoveUpdatedComponentImpl (count, false returns).

At PerformMovement entry the following are recorded: owner `bActorEnableCollision`, capsule
`CollisionEnabled` byte, effective value and whether it has a query bit,
`CharacterOwner->Controller != null`, MovementMode, and CurrentFloor flags. Velocity and
Acceleration are captured before and after one PerformMovement per row.

Collision writers: `SetActorEnableCollision` logs the side, actor class and pointer, roles, the
value, the bit before and after, the caller RVA and, when called from a blueprint, the running
UFunction and its outer (from `FFrame::Node` +0x10 captured in the exec thunk).
`SetCollisionEnabled` is logged only for a character's root component. EnterDropship and
DescendToLandCompleted log the +0x749 flag before and after. All of these share one cap per
map (`g_collision_logs`, `note_event`, 16).

### B. Static: how Enhanced Input evaluates a mapping

- `UPlayerInput::InputKey` 0x04310A10: FInputKeyEventArgs Key at +0x10, AmountDepressed float
  at +0x2C. Analog keys accumulate into their KeyState and then into the paired axis key's
  state, so `Mouse2D` is fed from MouseX/MouseY and never gets an `InputKey` call of its own.
- `UEnhancedPlayerInput::ProcessActionMappingEvent` 0x04475850: rdx points to the
  `TObjectPtr<UInputAction>`; the raw `FInputActionValue` (3 doubles plus ValueType at +0x18,
  0x20 bytes) is passed by pointer to a copy. It calls `ApplyModifiers` 0x044682F0 at +0x1D5,
  which calls `UInputModifier::ModifyRaw` 0x04473650 once per modifier, then
  `FTriggerStateTracker::EvaluateTriggers` 0x0446D780 at +0x20D (returns ETriggerState 0 None,
  1 Ongoing, 2 Triggered), then the actuation threshold check.
- With no triggers (IMC_Default_KBM's IA_Look mapping has 0 triggers, 2 modifiers), the
  implicit-down rule gives Triggered for any nonzero value. So `HandleInputLook 0` means either
  the value is zero by then or the action delegate never gets bound or fired.

### B. Diagnostics added

`ProcessActionMappingEvent` identifies IA_Look and IA_Move by the action's FName value (8 bytes
at +0x18). The first sighting is resolved through the name text, and other names are cached in
a fixed 64-slot array (at most 4096 text lookups per process). It sets `g_eval` for the call's
extent. `ApplyModifiers` (the outermost one inside that extent) records the value after the
mapping modifiers. `ModifyRaw` records, per modifier index (up to 4), the class name and the
input and output nonzero counts and max |v|. `EvaluateTriggers` counts None, Ongoing and
Triggered.

`detour_player_input_key` also counts MouseX/MouseY calls with a nonzero AmountDepressed and the
summed |amount|. These rows are logged on both sides; on the host they reflect the host's own
input, which is the control.

### Fix

None. The collision hypothesis fits card 012's numbers, but no log shows collision state, so
it is not proven. The look stage is not known. Card rule: fix only what is proven.

### Build and proof

- Build: `python3 -I tools/gen_proxy_exports.py && python3 -I tools/pdb_rva.py gen && cmake ... && cmake --build build`.
  Clean with /W4 /WX, linker included.
- `build/dwmapi.dll` is 225280 bytes, sha256 prefix d99f10f214c1e7c4.
- `llvm-readobj --coff-exports`: 84 exports. `llvm-objdump -p`: only `KERNEL32.dll` imported.
- 13 new symbols are in `tools/symbols.txt` under "Card 013", with 13 matching `kHooks` entries
  placed before `UGameEngine_Tick`. No new `kCalls`; `g_weak_get` is reused.

New hook prologues (none has a rip-relative operand in the bytes MinHook relocates, except that
`SetActorEnableCollision` has a rip-relative security-cookie load at +0xB, which is after the
first 5 bytes):

| Hook | RVA | Prologue |
|---|---|---|
| StartNewPhysics | 0x03A57470 | `mov [rsp+10h],rbx; push rdi; sub rsp,30h` |
| PhysWalking | 0x03A40290 | `push rbp; push rdi; push r15; lea rbp,[rsp-400h]` |
| SafeMoveUpdatedComponent | 0x03AA3860 | `push rbp; push rsi; push rdi; push r12; push r15; sub rsp,50h` |
| MoveUpdatedComponentImpl | 0x03A9EB50 | `mov [rsp+8],rbx; mov [rsp+10h],rsi; push rdi; sub rsp,80h` |
| SetActorEnableCollision | 0x037F2280 | `mov r11,rsp; push rsi; sub rsp,100h; mov rax,[rip+…]` (cookie at +0xB) |
| execSetActorEnableCollision | 0x03802210 | `mov [rsp+8],rbx; mov [rsp+18h],rsi; push rdi; sub rsp,20h` |
| SetCollisionEnabled | 0x040A6F50 | `mov [rsp+8],rbx; mov [rsp+10h],rsi; push rdi; sub rsp,20h` |
| EnterDropship | 0x051C56C0 | `mov [rsp+10h],rbx; push rdi; sub rsp,20h` |
| DescendToLandCompleted | 0x051C2D10 | `mov rax,rsp; mov [rax+18h],rbx; push rbp; push rsi` |
| ProcessActionMappingEvent | 0x04475850 | `mov rax,rsp; push rbp; push rsi; push r15` |
| ApplyModifiers | 0x044682F0 | `mov r11,rsp; mov [r11+8],rbx; mov [r11+10h],rbp` |
| ModifyRaw | 0x04473650 | `mov [rsp+8],rbx; mov [rsp+10h],rbp; mov [rsp+18h],rsi; mov [rsp+20h],rdi` |
| EvaluateTriggers | 0x0446D780 | `mov r11,rsp; push rbx; sub rsp,80h` |

Offset evidence: each constant in the new block has a comment naming the instruction it comes
from (GetCollisionEnabled for +0x5D/+0xA0/+0x38F, MoveAlongFloor+0x1E for +0x438,
SafeMove+0x150 for +0xAD, MoveAlongFloor+0x39B for Hit+0xD8, the exec thunk for FFrame
+0x10, EnterDropship+0xD for +0x749, InputKey for +0x2C).

### New log lines (per 1 s row: `rot[k]` on the client, `srv[k]` on the host)

```
<tag>[k]: since last row: MouseX nonzero N (sum |amount| X), MouseY nonzero N (sum |amount| X)
<tag>[k]: chain: StartNewPhysics N (dt min .. max ..), PhysWalking N, SafeMoveUpdatedComponent N (false N, blocking N, start penetrating N, requested |delta| sum .. max ..), MoveUpdatedComponentImpl N (false N)
<tag>[k]: chain: last PerformMovement entry: bActorEnableCollision B, capsule CollisionEnabled C, effective E (query Q), CharacterOwner->Controller D, MovementMode M, CurrentFloor flags 0xF; last blocking hit <component> owner <actor>
<tag>[k]: chain: one PerformMovement: Velocity (..) -> (..), Acceleration (..) -> (..)
<tag>[k]: IA_Look: mapping evaluations N, raw nonzero N (max |v| ..), after mapping modifiers nonzero N (max |v| ..), mapping modifiers N, mapping triggers None N Ongoing N Triggered N
<tag>[k]: IA_Look modifier i <Class>: in nonzero N (max |v| ..), out nonzero N (max |v| ..)
<tag>[k]: IA_Move: ... (same shape)
collision: <side> SetActorEnableCollision(<ptr> <class>, LocalRole R RemoteRole R, v) b -> b, caller exe+0x.., blueprint <outer>.<function>
collision: <side> SetCollisionEnabled(<comp> <class>, owner <ptr> <class> LocalRole R RemoteRole R, t) b -> b, caller exe+0x..
collision: <side> EnterDropship|DescendToLandCompleted(<ptr> <class>, LocalRole R) player-inside flag b -> b
```

### Decision table for the next run

| Client rows show | Cause | Fix direction |
|---|---|---|
| effective has no query (query 0), PhysWalking > 0, SafeMove 0, Velocity unchanged before → after | collision off, PhysWalking early return | Find the writer in the `collision:` lines (expected: a client EnterDropship or SetActorEnableCollision(false) with no matching true). Restore collision on the client's own pawn at the point the host does it, without touching movement checks |
| StartNewPhysics 0 | PerformMovement early return (mode 0, Mobility, simulating physics, HasValidData) | Read MovementMode/Mobility from the entry line |
| PhysWalking > 0, query 1, Controller 0 | no-controller zeroing (Velocity → 0 after) | possession on the client |
| query 1, floor flags != 3, SafeMove 0 | MoveAlongFloor floor gate | FindFloor result / base |
| SafeMove > 0, |delta| > 0, blocking ≈ SafeMove, start penetrating > 0 | stuck in geometry; hit component names it | depenetration / spawn position |
| SafeMove > 0, false ≈ SafeMove, no hits | MoveComponentImpl refuses | component state |
| Host `srv` row for the same pawn | control: expected query 1, PhysWalking ≈ SafeMove, moves | — |

| IA_Look rows show | Stage where look dies |
|---|---|
| mapping evaluations 0 | IA_Look mapping not processed (context or mapping not live) |
| raw nonzero 0 while MouseX/MouseY nonzero > 0 | key → mapping value (the paired axis or the mapping's key) |
| raw nonzero > 0, a modifier's out nonzero 0 | that modifier class zeroes it |
| after modifiers nonzero > 0, Triggered 0 | triggers |
| Triggered > 0, HandleInputLook 0 | action binding or delegate (not in the mapping pipeline) |

### Risks

- The hooks below PerformMovement and `ModifyRaw`/`EvaluateTriggers` run for every caller. They
  only compare against `g_move_cmc` / `g_eval` and return, with no logging and no reads.
- PerformMovement now reads CharacterOwner and its roles on the host too, which is two loads
  per character move.
- `ProcessActionMappingEvent` reads 8 bytes at action+0x18 per call. Text lookups are bounded.
- IA_Look and IA_Move rows on the host show the host's own input, not the remote client's.
- The signatures of SafeMove/MoveImpl (5th and 6th arguments) and of ProcessActionMappingEvent
  (TObjectPtr passed by pointer, value by pointer) come from disassembly. Every detour passes
  its arguments through unchanged, so the original's behavior does not change. But a wrong
  position would make the detour read a bad pointer: the hit at SafeMove's 5th argument, `*action`, or
  the 0x20-byte value copy in ModifyRaw. A crash dump at one of these detours points there first.
- No launch, no deploy, no commit. Nothing was denied.

## Senior review (2026-11-02)

Accepted as diagnostics. Build checked (84 exports, KERNEL32 only), prologues checked, run clean
(both exit 0, no FATAL/ERROR, no new crash dump). Logs: `logs/card013-{host,client}.log`.

Movement cause proven. Same pawn, same rows:

| | client (own pawn, AutonomousProxy) | host (remote client's pawn) |
|---|---|---|
| bActorEnableCollision (+0x5D bit 0) | 0 | 1 |
| capsule CollisionEnabled / effective | 3 / 0 (query 0) | 3 / 3 (query 1) |
| CurrentFloor flags | 0x0 | 0x3 |
| PhysWalking / SafeMoveUpdatedComponent | 120 / 0 per s | 60 / 0 per s (no input) |

`PhysWalking` early-outs on the client because the effective collision has no query bit, so
the client never moves itself; the server moves the pawn and corrections drag it (the user's
"slow movement"). No floor also explains jump failing (CanJump needs walking on ground).

Not found: the writer. No `SetActorEnableCollision` call for any player pawn on either side,
and `EnterDropship` / `DescendToLandCompleted` never ran. So the bit is either 0 in the class
default and set on the host by some path not hooked, or cleared on the client by a direct
bitfield write.

Look: inconclusive. The 20 rows ran 1–20 s after map load (client 603–622 s), but the user
moved the mouse on the host first and only used the client later (ClientAdjustPosition burst
at 638 s). Every client row has `InputKey 0`. Rows must start on the first client input, not
on map load. The user confirms the client window had focus and that look, sprint and jump are
all dead; W moves slowly.
