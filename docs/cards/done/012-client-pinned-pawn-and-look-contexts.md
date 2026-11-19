# Card 012: The client pawn is pinned between corrections; prove where client mouse look stops

Read `docs/STATE.md` first: the "Mod code rules" section and rows 008 to 012. Then read card 011 in full, including the senior review and its run results. The diagnostics you will extend are in `mod/src/hooks.cpp`. Repo root: `/path/to/ultimate-bug-war-coop`. Run logs: `logs/card011-{host,client}.log`.

## Why
Card 011's run disproved every earlier hypothesis.

**A. Movement.** The client and server CMCs agree: MaxWalkSpeed 700, MaxAcceleration 4096, MovementMode 1. The client sends acceleration |a| 4096 while keys are held. Its CMC Velocity reaches 624 to 700. But the location the client reports in its ServerMoves **does not change between corrections**. See host log `srv: correction` lines, timestamps 9.7962 to 9.8462: the client stays at exactly x -563.0 for four moves, while the server advances from -566.1 to -583.2. Then the client jumps to the corrected server position and holds again. So the client pawn is moved only by corrections, which is the lag the user sees.

Two possibilities remain, and the run must tell them apart:
1. The client's own move (`PerformMovement` → `PhysWalking` → `SafeMoveUpdatedComponent`) does not displace `UpdatedComponent`: blocked, zero time, a different component, or skipped.
2. The move does displace it, but something puts the pawn back to the last server position before the next move or send.

Note: in stock UE5, `PhysWalking` recomputes `Velocity` from the actual displacement. A non-zero Velocity with zero displacement therefore points at possibility 2, or at a move that runs on something other than the pawn's root. Do not trust this note; prove it from the exe.

**B. Look.** `IA_Look` is mapped only as `IA_Look <- Mouse2D` in `IMC_Default_KBM` (and `Gamepad_Right2D` in `IMC_Default_Gamepad`). `IMC_DummyUI_KBM` has no mouse mapping, so it is not the cause. On the client, mouse axes arrive (`InputAxis` 60 to 198/s), but `HandleInputLook` runs 0 times; on the host it runs about 83/s. Whether the client's mission adds `IMC_Default_KBM` is **unknown**: the `g_context_logs` cap of 16 was used up in the front-end, so the mission-phase AddMappingContext lines stopped after `IMC_Default_Gamepad` (client log line 117). The host's mission does add it.

## Do
1. **Fix the log caps first.** Reset `g_context_logs` and the mapping-line counter (`g_mapping_lines`) at each LoadMap, so every map gets its own budget. Keep them capped per map. Only reset counters on the game thread.
2. **A. Find who moves or pins the client pawn, statically and with diagnostics.**
   - Statically: read the client path from `UCharacterMovementComponent::TickComponent` through `ReplicateMoveToServer`, `PerformMovement`, `PhysWalking` and `SafeMoveUpdatedComponent` in this exe. Also check what can write the pawn's location on an autonomous proxy between moves: `ClientAdjustPosition` / `ClientUpdatePositionAfterServerUpdate` (saved-move replay), `OnRep_ReplicatedMovement` / `PostNetReceiveLocationAndRotation`, network smoothing, and any ST override (`ASTCharacter`, `ASTPlayerCharacter`, `ASTPlayerCharacterMI`, their CMC classes).
   - Diagnostics, client only, on the client's own pawn. Aggregate per second and print one line in each `rot[k]` row:
     - the number of `PerformMovement` calls (or whichever function proves to do the local move);
     - the summed |displacement| of `UpdatedComponent` across those calls (location after minus location before);
     - whether `UpdatedComponent` equals the pawn's RootComponent (prove the RootComponent offset);
     - the number of "pins": times the location at the start of a move differs from the location at the end of the previous move, plus the last pin's from/to and, if it can be found, the caller or function that did it.
   - If a hook on the writer is needed to name the pinning caller, choose one, prove its prologue is safe for MinHook, and log only when the target is the client's own pawn or its root component.
3. **B. Prove where client look stops.**
   - With the reset caps, the mission-phase AddMappingContext lines will now show whether the client adds `IMC_Default_KBM`.
   - Ten seconds after each map load, on both sides, log the contexts that are actually applied (the Enhanced Input player input's applied-contexts map, or the subsystem's equivalent; prove the offset) with their priorities, and whether any applied mapping uses `Mouse2D`.
   - Count, per second in the `rot[k]` rows, how often the client's Enhanced Input processes the `Mouse2D` key (or `MouseX`/`MouseY`, whichever the exe proves feeds `Mouse2D`), and how often `IA_Look` triggers versus `IA_Move`. Log the same on the host as the control.
4. **Fix only what is proven statically**, as in cards 008 and 009: mirror the missing setup on the side that lacks it, at the point where the other side gets it. Examples:
   - if the client's mission does not add `IMC_Default_KBM` and the exe shows why (a branch on net mode, authority, or a pointer only the server sets), add it at the equivalent point;
   - if a client-only writer pins the pawn, gate it so it does not act on the autonomous proxy.

   Do not disable server movement checks (no `bIgnoreClientMovementErrorChecksAndCorrection`, no `bServerAcceptClientAuthoritativePosition`). If nothing is proven, ship only the diagnostics.
5. **Do not regress cards 006 to 011.** The `UBWCOOP_CLIENT_GAMEONLY` fallback stays as it is. The card 011 `srv`/`rot` rows stay.
6. **Build and prove.** Use the build command in STATE.md. Show:
   - a clean `/W4 /WX` build;
   - 84 exports;
   - imports only from `KERNEL32.dll`;
   - the RVAs and prologues of every new hook;
   - the offset evidence;
   - what each new log line will say, and a decision table for the next run.

   Deploy is the senior's job.

## Constraints
- Follow STATE.md's "Mod code rules": no heap use, every resource released, no stored game pointers beyond the current call (comparing against the current pawn each call is fine), a skipped original must not leak, ≤60-line functions, bounded loops, capped or 1/s logging, shared state touched only on the game thread.
- Hooks on hot paths (per-move, per-key) must do no work unless the target is the client's own pawn or player input, and must not log per call.
- Do not launch the game, and do not touch `game/` (except reading the exe) or `logs/` (except reading the card 010 and 011 logs). Under `prefix/`, only read the crash dumps.
- No subagents. No commits. If a delete or other action is denied, say so in the report; don't ask the senior to do it for you.

## Report
(append below)

### Summary
Diagnostics only. Static reading found no client-only writer that pins the pawn and no missing look setup, so I made no fix. New code: per-map log caps; a client-only PerformMovement probe (move count, displacement, UpdatedComponent==Root, Mobility, callers, pins split by "ClientAdjustPosition since the last move" vs other); per-second Enhanced Input key counts (all keys, MouseX/MouseY/Mouse2D), IA_Look and IA_Move counts on both sides; and a dump of the applied mapping contexts, both sides, 10 s after each map load. Built clean. Not launched (per card).

### Static findings (exe + PDB, scratchpad dis.py / vt.py)
**A: movement path**
- CMC vtable `??_7UCharacterMovementComponent@@6BUObject@@@` 0x07955480. ReplicateMoveToServer's `call [rax+0xA30]` is slot 0xA30 = `PerformMovement` 0x03A3B460. Other vcalls in RMTS: 0xA80 CallServerMove, 0xA88 CallServerMovePacked, 0xAC0 CanDelaySendingMove, 0xAC8 GetClientNetSendDeltaTime, 0xB08 ShouldUsePackedMovementRPCs, 0x778 GetMaxAcceleration, 0x628 ComputeAnalogInputModifier, 0x6C0 SaveBaseLocation, 0x790 StepUp.
- The `PerformMovement` early-outs come before any move: vcall +0x668 (HasValidData), then GetWorld null (+0x4B/+0x54 to +0x756); `MovementMode (+0x231) == 0` (+0xBA); `UpdatedComponent->Mobility (+0x1A3) != 2` (+0xC7); `UpdatedComponent` vcall +0x4E0 (IsSimulatingPhysics) true (+0xDF). The last three jump to +0xDD3. It reads UpdatedComponent from `cmc+0xB8` and its location from `+0x200`, the same offsets as kMoveUpdatedComponent and kComponentTranslation. The new row prints Mobility and a "did not move" count, so a runtime early-out shows up.
- `ACharacter::PostNetReceiveLocationAndRotation` 0x039BC7B0 checks `LocalRole (+0x168) == 1` (SimulatedProxy) at +0x12, as stock UE does: replicated movement does not write the autonomous proxy's location. `ACharacter::OnRep_ReplicatedMovement` 0x039B8EC0 and `AActor::OnRep_ReplicatedMovement` 0x0382B5D0 show no ST change.
- The client CMC is plain `UCharacterMovementComponent` (card 011), so no ST CMC override is in play. Between moves, the only known writer of the autonomous-proxy location is ClientAdjustPosition / ClientUpdatePositionAfterServerUpdate (stock). That is why pins are split by whether an adjust arrived.
- Not done: the optional writer hook (SetWorldLocation-style). The pin split above has to show first whether an unexplained writer exists at all.

**B: look path**
- `IEnhancedInputSubsystemInterface::HasMappingContext(ctx, int&)` 0x04471130: interface vtable +0x10 returns the PlayerInput (+0x1A). Its `+0x538` is `AppliedInputContexts`, a TMap with 24-byte TSet elements: key at +0 and the int32 priority at +8 (`lea rcx,[rax+2*rax]; lea rdx,[rax+8*rcx]; ...[rdx+8]`, +0xCE..+0xED).
- `UEnhancedPlayerInput::InputKey(const FInputKeyEventArgs&)` 0x04472450: `lea rsi,[rbx+0x10]` passes `&args.Key` to FKey::IsButtonAxis, so the key's FName is at args+0x10.
- `ASTPlayerController::HandleInputMove` 0x051CB140: IA_Move handler (Move bound at SetupInputForConfig +0x7A6, card 011).
- Expectation: stock UE feeds MouseX/MouseY through InputKey and builds the paired `Mouse2D` inside UPlayerInput. So a working client should show MouseX/MouseY > 0 with Mouse2D possibly 0. The host row is the control.

### Changes (mod/src/hooks.cpp, tools/symbols.txt)
- `client_diag_load_map_enter` (hooks.cpp:1834) now resets `g_context_logs`, `g_mapping_lines` and the move probe at every LoadMap on the game thread. Caps are unchanged (16 events / 64 mapping lines per map).
- `MoveDiag` (1367) and `note_local_move` (1456) store values only (Vec3, counts, RVAs), no pointers. `detour_perform_movement` (1489): with no client session it passes straight through, and it also passes through unless `CharacterOwner->LocalRole == 2` (autonomous proxy, i.e. the client's own pawn). It measures UpdatedComponent's start and end and ignores a call where UpdatedComponent changed during the move. `note_client_adjust` sets `adjust_seen`.
- `detour_player_input_key` (1513) counts every key and compares the FName text with MouseX/MouseY/Mouse2D. It does not log per call, and it only runs for local players' PlayerInput.
- `detour_handle_input_move` (1522) counts.
- `log_applied_contexts` (1903) runs from `client_snapshot_step` on both sides. It reads the subsystem, then interface slot 2 for PlayerInput, cross-checks against `pc+0x420`, then lists each applied context with its priority and its count of Mouse2D/MouseX/MouseY mappings (`context_mouse_axis_mappings` 1887, bounded by kMaxMappingsScanned and kMaxAppliedContexts 64).
- `rotation_report_step` adds the move and key rows, then resets the row fields. `host_row_step` adds the key row and resets `g_counts`, which is now the host's only reset.
- symbols.txt: `UCharacterMovementComponent_PerformMovement`, `UEnhancedPlayerInput_InputKey`, `ASTPlayerController_HandleInputMove`, added to kHooks before UGameEngine_Tick.

### New log lines
```
rot[k]: since last row: EnhancedPlayerInput::InputKey N (MouseX a, MouseY b, Mouse2D c), HandleInputLook l, HandleInputMove m
rot[k]: PerformMovement (own pawn) n, did not move s, displacement sum D, UpdatedComponent==Root r, Mobility M, caller exe+0xX xA, other caller exe+0xY xB
rot[k]: pins p after ClientAdjustPosition + q other; last other pin (from) -> (to)
srv[k]: since last row: EnhancedPlayerInput::InputKey ... (host control; the first row covers LoadMap to the first ServerMove plus 1 s)
snapshot: <side> PlayerInput %p <class> (pc PlayerInput %p), applied context slots n
snapshot: <side> applied context %p <name> priority P, Mouse2D/MouseX/MouseY mappings k
```
The mission `input-mode: ... AddMappingContext` and `input-map:` lines are no longer used up by the front end.

### Decision table (next run)
| Observation | Meaning | Next |
|---|---|---|
| PerformMovement n ≈ ReplicateMoveToServer, D ≈ 0, "did not move" ≈ n | local move early-outs or produces no displacement | look at Mobility (≠2 = early-out), MovementMode, and whether Velocity/accel are nonzero; then read PhysWalking/MoveAlongFloor |
| Mobility ≠ 2 | static early-out at PerformMovement +0xC7 | find what makes the root non-Movable on the client |
| UpdatedComponent==Root 0 | client moves a different component | offset or setup bug in UpdatedComponent |
| D > 0, pins_other ≈ n | something writes the location back between moves | add the writer hook (SetWorldLocation / SetActorLocation for the own pawn's root) |
| D > 0, pins only after adjust, ~20/s | correction loop only: server and client sims disagree | compare server-side move inputs with the card 011 srv rows |
| PerformMovement n = 0 while RMTS > 0 | RMTS returns early (e.g. pending move combine/delay) | read RMTS +0x431..+0x5B5 |
| client MouseX/MouseY 0, InputAxis > 0 | mouse does not reach PlayerInput | trace PC InputKey / input stack |
| MouseX/MouseY > 0, applied list lacks IMC_Default_KBM | context missing on the client | mirror the host's add (fix card) |
| IMC_Default_KBM applied with the same priority as on host, mouse keys > 0, look 0 | EI does not evaluate the mapping (trigger/modifier/blocking context) | compare the higher-priority contexts against the host |
| HandleInputMove > 0 on client | IA_Move works; contrast with IA_Look | — |

### Build proof
- Command per STATE.md: `gen_proxy_exports.py`, `pdb_rva.py gen` (new: 0x03A3B460 PerformMovement, 0x04472450 UEnhancedPlayerInput_InputKey, 0x051CB140 HandleInputMove), cmake Ninja Release. Output: 3/3 steps, no warnings (/W4 /WX incl. linker).
- `build/dwmapi.dll` 204800 bytes. `llvm-readobj --coff-exports`: 84 exports. `llvm-objdump -p`: only `KERNEL32.dll` imported.
- Prologues (no RIP-relative bytes in the copied span):
  - PerformMovement `40 55 | 53 | 41 57 | 48 8D AC 24 00 F9 FF FF` (push rbp; push rbx; push r15; lea rbp,[rsp-0x700])
  - UEnhancedPlayerInput::InputKey `48 89 5C 24 08 | 48 89 6C 24 10` (mov [rsp+8],rbx; ...)
  - HandleInputMove `40 53 | 48 83 EC 30 | 4C 8B 81 C0 07 00 00` (push rbx; sub rsp,0x30; mov r8,[rcx+0x7C0])

### Risks
- PerformMovement and InputKey are hot hooks. On the host, PerformMovement costs one bool test. On the client, non-own characters cost two pointer reads and a role byte. InputKey does an FName lookup and three strcmp per local key event (~200/s), with no logging.
- The `MovementMode == 0` early-out is visible only through the existing CMC row (MovementMode), not counted separately.
- The applied-contexts read calls interface vtable slot 2; that slot was proven only via HasMappingContext. A null PlayerInput or an implausible TSet is logged, not dereferenced.
- No launch done (card rule); none of this has been seen running yet.

## Senior review (2026-11-02)
**Verified.**
- The prologues of the three new hooks match the report.
- Clean rebuild: 84 exports, KERNEL32 only.
- Deployed and run as host plus client, with `UBWCOOP_CLIENT_GAMEONLY=1`.
- The hot-path detours are cheap and filtered: PerformMovement measures only the autonomous-proxy pawn; InputKey counts and compares names only.

**Run results** (client `rot[k]` rows):
- **The local move never displaces the pawn.**
  - PerformMovement on the own pawn runs 120/s, all from caller exe+0x3A48912 (`ReplicateMoveToServer`).
  - **Every call reports "did not move"** and a displacement sum of 0.0, including `rot[19]`, where `HandleInputMove` ran 41 times and the sent acceleration was |a| 4096.
  - UpdatedComponent==Root is 1 and Mobility is 2, so these early-outs are ruled out: wrong component and non-Movable root.
  - Pins: 6 after ClientAdjustPosition, 0 others. Nothing writes the pawn back; the corrections are the only thing that moves it.
  - Decision-table row 1: the local move early-outs inside `PerformMovement`, or produces no displacement there.
- **Look: the context is present, the mouse keys arrive, and IA_Look still never fires.**
  - The client applies `IMC_MI_KBM` (1), `IMC_MI_Gamepad` (1), `IMC_Default_Gamepad` (0) and `IMC_Default_KBM` (0); `IMC_Default_KBM` carries the one Mouse2D mapping, at the same priority as on the host.
  - The client has no higher-priority context at all; the host has more.
  - `UEnhancedPlayerInput::InputKey` sees MouseX 62/s and MouseY 62/s. HandleInputLook stays 0, while HandleInputMove fires.
  - Decision-table row: Enhanced Input does not turn the mapping into a Triggered event. The remaining suspects are the mapping's 2 modifiers, the action's own modifiers or triggers, and the Mouse2D pairing.
  - Mouse2D 0 through InputKey may be normal (paired axes are updated in the key-state map, not re-entered through InputKey).
  - The host control had no mouse input during its window, so it shows no host comparison for this run; card 011 already proved the host's look works (about 83/s).

**Next:** card 013.
