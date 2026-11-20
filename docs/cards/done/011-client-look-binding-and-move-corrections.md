# Card 011: Client IA_Look never fires; the server corrects every client move

Read `docs/STATE.md` first: the "Mod code rules" section and rows 008 to 011. Then read card 010 in full: the report and the senior review with its run results. The diagnostics you will extend are in `mod/src/hooks.cpp`. Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why
Card 010's run (`logs/card010-client.log`, `rot[0..19]`) shows two separate faults on the client.

**A. Look.** Raw mouse axes reach the client: `UGameViewportClient::InputAxis` runs 76 to 234 times per second while the mouse moves. But `ASTPlayerController::HandleInputLook` runs **0** times, and `ControlRotation` never changes. WASD's `HandleInputMove` does fire, so the Enhanced Input stack and the mapping contexts work for movement. Somewhere between the mouse key events and the IA_Look action binding, the chain is broken only for look. The host's look works.

**B. Movement lag.** The client's pawn is AutonomousProxy (LocalRole 2), and `ReplicateMoveToServer` runs at about 120 per second. But `ClientAdjustPosition` arrives about 20 times per second whenever the pawn moves, and none arrive when it is idle. Each correction puts the server's position about 30 to 35 units behind the client's, roughly where the client was one correction interval earlier. The server simulates the client's moves differently from the client, or later than it, and then snaps the client back.

## Do
1. **A. Find the look break statically.** Candidates:
   - Where IA_Look is bound: `ASTPlayerController::SetupInputComponent` (0x051DC4A0) and anything it calls. Find the `BindAction` for the look action, and any condition on it: net mode, authority, a pawn or character pointer that only `OnPossess` sets, or input settings or `UpdateBindings` (`USTPlayerUtilityLibrary_UpdateBindings`, 0x05255EB0, see the earlier cards).
   - Which mapping context maps the mouse to IA_Look (`IMC_Default_KBM`, `IMC_MI_KBM`, or a player-mappable config), and whether the client has it. Card 008 logged four AddMappingContext calls. The card 010 run also logged `IMC_DummyUI_KBM` at priority 2, after a menu opened.
   - Whether look reaches `HandleInputLook` through a different handler, for example on the pawn or in a Blueprint.

   Compare the client's path with the host's.
2. **B. Find why the server disagrees.** On the host, the server-side `UCharacterMovementComponent` for the remote client's pawn decides each correction. Candidates:
   - client-only or server-only speed values: `MaxWalkSpeed`, `MaxAcceleration`, or a speed multiplier set in `ASTPlayerCharacter::PossessedBy` / `OnPossess` paths on one side only;
   - the movement mode;
   - root motion;
   - a custom CMC (`USTPlayerMovementComponentBug`, vtable 0x080B73B8) whose saved-move or compressed-flag handling differs;
   - server time and timestamp handling.

   Read `ServerMove` handling (`ServerMovePacked_ServerReceive` / `MoveAutonomous` / `ServerCheckClientError`) for anything in the game's overrides.
3. **Diagnostics, so the next run decides.** Every log must be capped, or rate-limited to at most 1 line per second, and read-only.
   - **Host:** once per second for about 20 s after a client joins, log the remote client's pawn CMC:
     - `MaxWalkSpeed`, `MaxAcceleration` and `MovementMode`;
     - velocity;
     - the count of server moves received;
     - the count of corrections sent, plus the client-versus-server error of the last one.
   - **Client:** log the same CMC fields for its own pawn in the `rot[k]` rows.
   - **Look, both sides:** log, capped, which actions `SetupInputComponent` binds on each side, or which mapping contexts contain IA_Look. Also log the host's `HandleInputLook` count, so the host works as the control.
   - Fix the card 010 diagnostic bug: the CMC column reads `null` at `kCharacterMovement = 0x328`, while `ClientAdjustPosition` shows a real CMC owned by the same pawn. Prove the right offset or getter.
4. **Fix only what is proven statically**, the same way as cards 008 and 009: mirror the missing setup on the side that lacks it, at the point where the other side gets it. Do not disable server movement checks (no `bIgnoreClientMovementErrorChecksAndCorrection`, no `bServerAcceptClientAuthoritativePosition`); the cause must be fixed. If nothing is proven, ship only the diagnostics.
5. **Do not regress cards 006 to 010.** The `UBWCOOP_CLIENT_GAMEONLY` fallback stays as it is.
6. **Build and prove.** Use the build command in STATE.md. Show:
   - a clean `/W4 /WX` build;
   - 84 exports;
   - imports only from `KERNEL32.dll`;
   - the RVAs and prologues of every hooked function;
   - the offset evidence;
   - what each new log line will say.

   Deploy is the senior's job.

## Constraints
- Follow STATE.md's "Mod code rules": no heap use, every resource released, no stored game pointers beyond the current call, a skipped original must not leak, ≤60-line functions, bounded loops, capped logging, shared state touched only on the game thread.
- Do not launch the game, and do not touch `game/` (except reading the exe) or `logs/` (except reading the card 010 logs). Under `prefix/`, only read the crash dumps.
- No subagents. No commits. If a delete or other action is denied, say so in the report; don't ask the senior to do it for you.

## Report
(append below)

### Summary
- **Nothing proven well enough to fix, so this card ships diagnostics only.** There is one diagnostic bug fix: the CMC offset.
- **Movement: the card 010 reading is wrong.** The server does not trail the client. The client's own prediction moves the pawn **zero** distance. In `logs/card010-client.log` lines 103-120, every `ClientAdjustPosition` "client" location equals the previous correction's *server* location exactly. So the client re-sends the position the server last gave it, and the server, having moved the pawn by the input, corrects it again.
- Likely causes: client CMC with MaxWalkSpeed/MaxAcceleration 0, or a zero acceleration from the client's input. The new rows split these two.
- **Look:** host and client bind IA_Look and IA_Move **identically** in `SetupInputForConfig`. No side-specific binding is missing. The remaining hypothesis is a higher-priority context consuming mouse XY; the new logs name every mouse/Look mapping per context on both sides.

### Findings: movement (B)
**The card 010 CMC column was wrong in two ways.**
1. `kCharacterMovement = 0x328` is **Mesh**, not the CMC:
   - `ACharacter::GetMovementComponent` (0x039B19B0) is `mov rax,[rcx+0x330]; ret`;
   - `ACharacter::PossessedBy` (0x039BC520) uses +0x328 as Mesh.

   The offset is now `0x330`.
2. The column still read `null`, and with 0x328 it would have read Mesh, which is non-null. So `is_a(pawn, ASTPlayerCharacter)` **failed** for `BP_PlayerCharacterMI_C`. `ASTPlayerCharacterMI` is its own class (StaticClass 0x05083AB0, vs ASTPlayerCharacter 0x050847D0).
   - The gate is now `ACharacter` (`?StaticClass@ACharacter@@SAPEAVUClass@@XZ`, 0x03993510).
   - A new row line logs both class checks.
   - **Consequence for card 009:** `sync_client_input_receiver` (`hooks.cpp:1009`) sets `PlayerCharacter` only when `is_a(pawn, ASTPlayerCharacter)`. If the new row prints `ASTPlayerCharacter 0`, card 009 never sets PlayerCharacter for the MI pawn, and that is a fix candidate for card 012. I did not change it, because it is not proven at runtime.

**CMC offsets, from the exe:**

| Field | Offset | Proof |
|---|---|---|
| CharacterMovement | `pawn+0x330` | `GetMovementComponent` 0x039B19B0 |
| UpdatedComponent | `cmc+0xB8` | `UpdateComponentVelocity` 0x03AAD230 |
| Velocity (3 doubles) | `cmc+0xD0` | same function |
| MovementMode (u8) | `cmc+0x231` | `GetMaxSpeed` 0x03A317A0 jump-table index |
| MaxWalkSpeed (float) | `cmc+0x278` | `GetMaxSpeed` (walking case) |
| MaxAcceleration (float) | `cmc+0x28C` | `GetMaxAcceleration` 0x03A31680 |
| IgnoreClientErrors | `cmc+0x555` bit 1 | `ServerCheckClientError` +0x4 `test byte [rcx+0x555],2`, which returns false when set; matches `bIgnoreClientMovementErrorChecksAndCorrection`. Logged only, never written. |

**"Speeds are server-only attributes" is not proven.**
- The initial speeds come from `USTAttributeSet_Movement::InitFromData` (0x051CC0C0). Through `owner+0x330`, it writes:
  - CMC +0x1A8;
  - +0x278 (MaxWalkSpeed);
  - +0x1A0;
  - +0x28C (MaxAcceleration);
  - +0x294.
- The chain is `ASTCharacter::PostInitializeComponents` (0x050FC3F0, call at +0x1A7) → `UADGASBasicsUtilityLibrary::InitFeaturesFromDefinition` (0x04A10540) → `UADAbilitySystemComponent::AddAttributeSetFromData` (0x04A0F060) → `GetOrCreateAttributeSubobject` → vtbl+0x300 `InitFromData`.
- Nothing in the chain checks authority, so the initial values should also apply on the client.
- Later writers may still diverge if a gameplay effect changes speed only on the server: `PostAttributeChange` 0x051D7BB0 and `UpdateCharacterMovementMaxAcceleration` 0x051E9B60.
- The new rows show both sides' values side by side.

**Game CMC net functions** (none overridden by ST classes):

| Function | RVA |
|---|---|
| ClientHandleMoveResponse | 0x03A223E0 |
| MoveAutonomous | 0x03A386D0 |
| SendClientAdjustment | 0x03A4A8D0 |
| ServerCheckClientError | 0x03A4CB30 |
| ServerExceedsAllowablePositionError | 0x03A4CBA0 (vtbl+0xAA0) |
| ServerMoveHandleClientError | 0x03A4D670 |
| ServerMovePacked_ServerReceive | 0x03A4EB60 |
| ServerMove_PerformMovement | 0x03A4F0B0 |

### Findings: look (A)
- `ASTPlayerController::SetupInputForConfig` (0x051DC870) binds the two handlers the same way. HandleInputMove (at +0x7A6) and HandleInputLook (at +0x7EA) both go through `BindNativeFunction` 0x051B6610, with trigger event Triggered, on the same EnhancedInputComponent `this+0x178`. The action tags come from globals 0x14943ACD0 and 0x14943ACE0. Move works on the client, so the binding path itself works there.
- The Look modifiers are not side-specific: AimSensitivity 0x0512E070, AimInversion 0x0512DFD0, TimeDilationLookScale 0x0512E260.
- Hypothesis: a context the client adds later at higher priority, such as `IMC_DummyUI_KBM` (prio 2 vs `IMC_MI_KBM` prio 1), maps Mouse X/Y and consumes the axes before IA_Look.
- Mapping layout, from `UInputMappingContext::MapKey` 0x04472DB0 and the `FEnhancedActionKeyMapping` ctor 0x044637A0:
  - Mappings TArray at context+0x30, element 0x50 bytes;
  - Triggers Num +0x8;
  - Modifiers Num +0x18;
  - Action +0x20;
  - Key FName +0x28.

### Changes
`tools/symbols.txt` adds three entries:
- `UCharacterMovementComponent_ServerCheckClientError` (0x03A4CB30);
- `UCharacterMovementComponent_ServerMove_PerformMovement` (0x03A4F0B0);
- `ACharacter_StaticClass` (0x03993510).

`mod/src/hooks.cpp` changes:
- `kCharacterMovement` is now 0x330 (comment explains the 0x328 = Mesh bug). The CMC/mapping offset constants are listed above.
- `detour_add_mapping_context` now logs on **both** sides (capped by `g_context_logs`) and dumps the context's mouse/Look mappings.
- `detour_handle_input_look` counts on the host too (the control). Its lines are prefixed with the side.
- `detour_replicate_move` keeps the last client acceleration/delta (game thread, value copy).
- New hooks; both call the original first and only read afterwards:
  - `ServerCheckClientError`: host only, counts checks and corrections; a capped line per correction;
  - `ServerMove_PerformMovement`: host only, counts moves. 20 rows at 1 s after the first server move following a host LoadMap.
- `rot[k]` rows: gate is ACharacter; CMC detail line; class-check line; last client acceleration line.
- All new loops are bounded (`kMaxMappingsScanned` 256, `kNameChars`). All new logs are capped or 1/s. There is no heap use and no stored game pointers. Functions are ≤60 lines.

### New log lines (what to read)
- `input-mode: client|host/standalone AddMappingContext(iface=…, context=… IMC_x, priority N) caller exe+0x…`
- `input-map: IMC_x [i] IA_y <- MouseX (triggers t, modifiers m)`: lists mouse-key or *Look* mappings only. Then `input-map: IMC_x: N mappings, M mouse/look`.
- `look: client|host/standalone HandleInputLook(pc=…) xN since last line, last value X … Y …; RotationInput …`
- `rot[k]: CMC … CharacterOwner …, MovementMode m, MaxWalkSpeed w, MaxAcceleration a, Velocity (…) speed s, IgnoreClientErrors bit b` (client)
- `rot[k]: pawn is ACharacter 1, ASTPlayerCharacter 0|1`
- `rot[k]: last ReplicateMoveToServer acceleration (…) |a| …, delta …`
- `srv: first ServerMove_PerformMovement after host LoadMap (cmc=…); 20 rows follow`
- `srv[k]: CMC … MovementMode/MaxWalkSpeed/MaxAcceleration/Velocity …` and `srv[k]: since last row: server moves N, ServerCheckClientError N, corrections N; last correction error (…) |e| …, client accel |a| …`
- `srv: correction cmc=… timestamp … delta … client mode m, server (…) client (…) error e, client accel |a| …` (capped)

**Decision table for the next run:**
- client `MaxWalkSpeed`/`MaxAcceleration` 0 or far below host `srv[k]` → speed init/replication gap (card 012: find the writer the client misses);
- client values equal, but `|a|` 0 while keys are held → input never reaches CMC acceleration on the client (the PlayerCharacter receiver / `ASTPlayerCharacter 0` lead);
- `|a|` > 0 with values equal but zero client movement → the client's local move simulation is skipped. Next step: hook `MoveAutonomous`/`PerformMovement` on the client;
- client `input-map` shows Mouse X/Y in a context above IMC_MI_KBM → that context consumes Look;
- host `look:` counts > 0 while client counts stay 0 confirms a client-only problem.

### Build proof
- The STATE.md build command is clean under `/W4 /WX` (linker included).
- `build/dwmapi.dll` is 197120 bytes, with 84 exports. The only import is `KERNEL32.dll`.
- `pdb_rva gen` resolved all new symbols.
- New hook prologues; the relocated bytes contain no RIP-relative instruction:
  - `ServerCheckClientError` 0x03A4CB30: `48 83 EC 58` (sub rsp,0x58), then `F6 81 55 05 00 00 02` (test byte [rcx+0x555],2), then jne at +0xB;
  - `ServerMove_PerformMovement` 0x03A4F0B0: `40 53` (push rbx), `57` (push rdi), `48 81 EC E8 0C 00 00` (sub rsp,0xCE8). The first RIP-relative mov is at +0xA, after the relocated bytes.
- Existing hooks are unchanged. `UGameEngine_Tick` stays last in `kHooks`.

### Risks
- The `srv` rows and counters are not per-connection. With more than one client they mix, which is fine for the 1-client test.
- The mapping layout (0x50 stride, offsets) comes from one ctor and MapKey. If it is wrong, the `input-map` names print as empty or garbage, but the reads stay bounded by `kMaxMappingsScanned` and the TArray Num.
- `AddMappingContext` now also logs on the host. That is capped, but the host log grows by up to the cap plus 64 mapping lines.
- No deletes or commits were needed; none were attempted.

## Senior review (2026-11-02)
**Verified.** The prologues of both new hooks match the report. Clean rebuild: 84 exports, KERNEL32 only. Deployed and run as host plus client with `UBWCOOP_CLIENT_GAMEONLY=1`. Both instances exited 0, and there was no new crash dump. Logs: `logs/card011-{host,client}.log`.

**Run results.**
- **Class check:** `pawn is ACharacter 1, ASTPlayerCharacter 1` on the client. `BP_PlayerCharacterMI_C` passes the ASTPlayerCharacter check, so the card 008 `sync_client_input_receiver` gate is fine; the null CMC column in card 010 came from the 0x328 offset alone. Drop the card 012 lead.
- **Speeds are equal:**
  - client: MaxWalkSpeed 700, MaxAcceleration 4096;
  - server: the same values.

  The client sends acceleration |a| 4096 while keys are held. Its CMC Velocity reaches 624 to 700. So the speed-init hypothesis and the zero-acceleration hypothesis are both disproven.
- **The client pawn does not move between corrections.**
  - Host `srv: correction` lines, timestamps 9.7962 to 9.8462: the client-reported location stays at exactly -563.0 for four moves while the server advances -566.1 → -583.2.
  - Then the client location jumps to the corrected server value, and holds again.
  - Corrections run at about 61/s (every ServerCheckClientError fails while moving); the client applies about 20/s.
  - So the client computes velocity but its pawn's location is either not displaced by its own move or is pinned back to the last server location before each send. This is the decision-table row "|a| > 0, values equal, zero client movement".
- **Look:**
  - `IA_Look` is mapped only as `IA_Look <- Mouse2D` in `IMC_Default_KBM` (and `Gamepad_Right2D` in `IMC_Default_Gamepad`).
  - `IMC_DummyUI_KBM` has 0 mouse/look mappings. That disproves the priority-consumption hypothesis.
  - Host control: `HandleInputLook` x83 per second while the mouse moves.
  - Client: HandleInputLook 0, InputAxis 60 to 198/s.
  - **Inconclusive:** on the client, the mission's AddMappingContext lines stop after `IMC_Default_Gamepad` because `g_context_logs` hit its cap of 16 (`diag: AddMappingContext ... further events not logged`, client log line 117). Whether the client's mission adds `IMC_Default_KBM` is unknown. The host's mission does add it.
- **User observation:** during the intro movies, clicking the client window does not capture the mouse, while on the host it does. The main menu works on both. Low priority; noted in STATE later issues.

**Code review.** No issues in the new hooks: they call the original first, read only afterwards, keep no game pointers, and their loops are bounded. The 16-event cap per process is too small for a two-map run: card 012 makes the counters per map.

**Next:** card 012.
