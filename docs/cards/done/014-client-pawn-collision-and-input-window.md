# Card 014: Re-enable collision on the client's own pawn; find what clears it; catch client input in the rows

Read `docs/STATE.md` first: the "Mod code rules" section and rows 011 to 014. Then read card 013 in full, including the report's decision tables and the senior review at the end. The diagnostics you will extend are in `mod/src/hooks.cpp` (card 013's block around `kActorCollisionFlags`, and the `rot`/`srv` row code from about line 2160). Repo root: `/path/to/ultimate-bug-war-coop`. Run logs: `logs/card013-{host,client}.log`.

## Why

**A. Movement, cause proven.** In card 013's run, the client's own pawn (`BP_PlayerCharacterMI_C`, LocalRole 2) has actor+0x5D bit 0 (`bActorEnableCollision`) clear. Its capsule has CollisionEnabled 3, but the effective collision is 0, query 0. The host's copy of the same pawn has bit 0 set, effective 3, query 1, and CurrentFloor flags 0x3. So on the client `PhysWalking` early-outs on every tick (120/s, `SafeMoveUpdatedComponent` 0). The pawn never moves locally, the server moves it, and corrections drag it, which the user sees as slow movement. Without a floor, jump also fails.

**The writer is not known.** In that run:
- no `SetActorEnableCollision` call hit any player pawn on either side;
- `EnterDropship` and `DescendToLandCompleted` never ran.

So one of these holds:
- (a) the class default (CDO) has the bit clear, and the host sets it by a path not hooked;
- (b) the client clears it by a direct bitfield write, not through `SetActorEnableCollision`.

**B. Input, inconclusive.** The 20 `rot` rows run 1 to 20 s after the client map load. The user moved the mouse on the host first and only used the client later, around 638 s against rows ending at 622 s. Every client row has `InputKey 0`.

The user confirms, with the client window focused:
- W moves slowly;
- mouse look is dead;
- sprint and jump are dead;
- "basically any other player input" is dead.

Jump may be the missing floor from A. Look is a separate fault: card 012 showed MouseX/MouseY reach `InputKey`, but `HandleInputLook` never runs.

## Do

1. **A. Find the writer, statically first.**
   - Search the exe for code that writes bit 0 of actor+0x5D other than `AActor::SetActorEnableCollision` 0x037F2280. Look for byte stores or `and`/`or`/`btr` at `[reg+5Dh]` that touch bit 0. Classify each hit as constructor or serialization, ST game code, or engine.
   - Find the pawn class default: how `BP_PlayerCharacterMI_C`'s CDO and `ASTPlayerCharacter`'s constructor set the bit.
   - Find what on the server side could set it to 1 for the remote client's pawn without calling `SetActorEnableCollision`: possession, `PossessedBy`, spawn, `PostInitializeComponents`, ST character code.
   - Report each candidate with its RVA and the instruction.
2. **A. Diagnostics, one-shot, capped.** On both sides, log bit 0 of +0x5D for player pawns (ASTPlayerCharacter, own or remote) at each of these points, with the pawn's role:
   - the class CDO value, once per class;
   - spawn or `PostInitializeComponents`, if it is cheap to hook;
   - `PossessedBy` on the host, and `OnRep_Pawn` / `AcknowledgePossession` / `ClientRestart` on the client. These hooks already exist; add the bit to their log line.
   - On the client, check the own pawn once per `UGameEngine::Tick`. When the bit changes, log old to new, the tick count since map load, and the last hooked function that ran. Log at most 8 changes per map load.
3. **A. Fix.** The host's copy has the bit set, so the client's own pawn must match.
   - On the client only (net mode client, own pawn, LocalRole AutonomousProxy, ASTPlayerCharacter), when the bit is clear, call the game's own `AActor::SetActorEnableCollision(pawn, true)` through the original function pointer from the existing hook. Do not flip the bit directly, so the components get their collision update.
   - Do it from the once-per-tick check in step 2. That also re-applies the fix if something clears the bit again. Log each re-enable, capped at 8 per map load, and count them in the rows.
   - If step 1 finds the exact point where the host gets the bit, and a matching client point exists, use that point instead and say why. Keep the per-tick check as a logged guard.
   - Do not touch remote players' pawns (simulated proxies), do not touch any other actors, and do not change the capsule's own CollisionEnabled.
4. **B. Input window.**
   - The client `rot` rows start on the first client `EnhancedPlayerInput::InputKey` call with a nonzero AmountDepressed for a key other than the mouse, after map load, not on map load itself. Do the same for the host `srv` rows with the host's own input.
   - Keep 20 rows.
   - Log one line when the window opens, with the key name.
5. **B. Look, jump, sprint per action.** Extend card 013's per-action stats from IA_Look and IA_Move to every input action evaluated on the client's player input, with a fixed table of at most 12 actions keyed by the `UInputAction` name; actions past 12 are counted as "other". For each action, per row, log:
   - mapping evaluations, raw nonzero, after-modifier nonzero, max |v|;
   - trigger state counts (None / Ongoing / Triggered);
   - how many Triggered events reached a bound handler, if you can see that cheaply. Find statically where `UEnhancedInputComponent` bindings dispatch.

   If the table shows actions Triggered with no handler running, find out why statically and report it. Do not fix look in this card unless it is proven statically.
6. **Do not regress cards 006 to 013.** The `UBWCOOP_CLIENT_GAMEONLY` fallback stays as it is. Do not disable server movement checks, and do not add smoothing.
7. **Build and prove.** Use the build command in STATE.md. Show:
   - a clean `/W4 /WX` build;
   - 84 exports;
   - imports only from `KERNEL32.dll`;
   - RVAs and prologues of every new hook;
   - offset evidence;
   - the new log lines;
   - a decision table for the next run, including what the senior should expect for client movement if the fix works: the client `chain` row shows `SafeMoveUpdatedComponent` > 0, and corrections stop while W is held.

   Deploy is the senior's job.

## Constraints
- Follow STATE.md's "Mod code rules":
  - no heap use;
  - every resource released;
  - no stored game pointers beyond the current call (compare the own pawn by identity within a tick only);
  - a skipped original must not leak;
  - functions of 60 lines or fewer;
  - bounded loops;
  - capped or 1/s logging;
  - shared state touched only on the game thread.
- Hot-path hooks never log per call. They do no work unless the target is the own pawn, its CMC, or the local player input.
- Do not launch the game. Do not touch `game/` (except reading the exe) or `logs/` (except reading the card 011 to 013 logs). Under `prefix/`, only read the crash dumps.
- No subagents. No commits. If a delete or other action is denied, say so in the report; don't ask the senior to do it for you.

## Addendum: scripted input for unattended runs (user request)

The user wants test runs to need no one at the keyboard. The senior now runs pairs with
`tools/run_pair.sh` (read it). Add an env-gated input script so a run exercises the client's
input with no human. Do this after steps 1 to 5, in a new file `mod/src/autoinput.cpp` (plus a
small header), not inside `hooks.cpp`'s diagnostics.

1. **Env.** `UBWCOOP_AUTOINPUT=1` turns it on. Parse it in `config.cpp` like
   `UBWCOOP_CLIENT_GAMEONLY`; any other value is off and logs a WARN. It acts per process, so it
   works on the host too.
2. **Where input enters.** Inject at the same layer real input enters. Find statically how
   `UGameViewportClient::InputKey` / `InputAxis` hand a key event to the local
   `APlayerController` and on to `UEnhancedPlayerInput::InputKey` (0x04472450, already hooked).
   Call the highest function on that path you can call safely with an `FInputKeyParams` you
   build: an FKey from an engine `FName`, null key details, event type, axis delta for mouse,
   NumSamples 1, and the frame's DeltaTime.
   - Prove every field offset and the FName constructor (RVA, signature) from the exe.
   - Mouse look must go through MouseX/MouseY as the viewport sends them, so that the engine's
     own Mouse2D pairing runs.
3. **When.** Run from `UGameEngine::Tick` on the game thread, before the original. Start 5000 ms
   after the local PC acknowledges its pawn, once per map load. Use a fixed `static const`
   step table: start ms, duration ms, key name, kind (button hold or axis per tick), value. The
   script:

   | t (ms) | input |
   |---|---|
   | 0–4000 | W held |
   | 4000–6000 | MouseX +5 per tick |
   | 6000–8000 | MouseY +3 per tick |
   | 8500 | SpaceBar tap (Pressed, Released 100 ms later) |
   | 9500–12500 | LeftShift and W held |
   | 13000–15000 | D held |
   | 15500 | LeftMouseButton tap |

   A button gets exactly one Pressed and one Released; release all held keys if the map
   unloads mid-script. Log one line per step start, and one "autoinput: script done" line.
4. **Rows.** With autoinput on, the step 4 input window opens on the first scripted key, so
   the 20 rows cover the whole script.
5. **Quit.** With `UBWCOOP_AUTOINPUT=1`, the client requests a normal engine exit 8000 ms
   after "script done", through the engine's own exit request (find the RVA). The host
   keeps running; the runner script ends it.
6. **Constraints.** Same mod code rules: no heap (FName and FKey built on the stack, with no
   destructor that frees game memory you didn't allocate; prove FKey's TSharedPtr is null and
   needs no release), and no stored game pointers. When the env is off, nothing changes.

Report the call path chosen and why, the struct layouts with evidence, and the expected log
lines. The senior will run it with
`CLIENT_EXTRA_ENV=UBWCOOP_AUTOINPUT=1 DONE_PATTERN='autoinput: script done' tools/run_pair.sh card014`.

## Report

Junior, 2026-11-02. Static work and build only. The game was not launched. No subagents, no commits, nothing denied.

### Files changed
- `tools/symbols.txt`: card 014 block, 9 symbols. `src/offsets.generated.h` was regenerated by `pdb_rva.py gen`.
- `mod/src/autoinput.{h,cpp}` (new): the scripted-input module.
- `mod/src/config.{h,cpp}`: `read_autoinput()`.
- `mod/src/dllmain.cpp`, `mod/src/hooks.h`: `install(travel, client_game_only, autoinput)`.
- `mod/CMakeLists.txt`: adds `src/autoinput.cpp`.
- `mod/src/hooks.cpp`:
  - collision watch and fix;
  - player-pawn cold-point lines;
  - action table with binding Execute counts;
  - input window;
  - autoinput wiring.

### Build proof
- The STATE.md build command builds clean. `build.ninja` carries `/W4` (6 rules) and `/WX` (7, including the link).
  - A forced rebuild of `hooks.cpp`, `autoinput.cpp`, `config.cpp` and `dllmain.cpp` gave no warnings.
- `gen_proxy_exports.py`: `84 exports (37 named)`. `llvm-readobj --coff-exports build/dwmapi.dll`: 84 ordinals.
- `llvm-readobj --coff-imports build/dwmapi.dll`: only `KERNEL32.dll`.
- `build/dwmapi.dll`: 240128 bytes, sha256 `fd7e09d66b91e7bccc9212466a6ddf51827d9e8bd945c352acd0b8585ee047f2`.
- No function in `hooks.cpp`, `autoinput.cpp` or `config.cpp` is longer than 60 lines (script check).
- Heap: none. Every buffer is fixed-size and on the stack or global: FKey 0x18, args 0x48, the 12-row action table, an 8-entry class table.

### New hooks and calls: RVAs and prologues (from the exe)
| Symbol | RVA | Prologue / evidence |
|---|---|---|
| `ASTPlayerCharacter::PostInitializeComponents` (hook) | 0x051D8B10 | `push rbx; sub rsp,20h; mov rbx,rcx; call ASTCharacter::PostInitializeComponents` |
| `ASTPlayerCharacter::PossessedBy` (hook) | 0x051D7A10 | `push rbx; sub rsp,20h; mov rbx,rcx; call ACharacter::PossessedBy` |
| `FEnhancedInputActionEventDelegateBinding<TDelegate<void(const FInputActionValue&)>>::Execute` (hook) | 0x051C57B0 | `push rbx; sub rsp,60h; mov rax,rdx; mov rbx,rcx; mov rcx,rax; lea rdx,[rsp+40h]` |
| `...<TDelegate<void()>>::Execute` (hook) | 0x051C5830 | `push rbx; sub rsp,20h; mov rax,[rcx+20h]; mov rbx,rcx; test rax,rax` |
| `...<FEnhancedInputActionHandlerDynamicSignature>::Execute` (hook) | 0x0446D9A0 | `mov rax,rsp; mov [rax+10h],rbx; mov [rax+18h],rsi; push rdi; sub rsp,0B0h` |
| `FKey::FKey(const TCHAR*)` (call) | 0x015B71F0 | `push rbx; sub rsp,20h; mov r8d,1; mov rbx,rcx; call FName::FName(const wchar_t*, EFindName) 0x0132DFB0`. Then `xor eax,eax` zeroes +8 and +0x10 (the TSharedPtr KeyDetails object and refcount), and it returns rbx. |
| `FInputKeyEventArgs::FInputKeyEventArgs(FViewport*, FInputDeviceId, const FKey&, EInputEvent, float, bool, uint64)` (call) | 0x03C9E340 | `mov [rsp+8],rbx; push rdi; sub rsp,20h; mov [rcx],rdx (viewport); mov ebx,r8d; mov [rcx+0Ch],ebx (device)` |
| `APlayerController::InputKey(const FInputKeyEventArgs&)` (address only) | 0x0404EB10 | `mov [rsp+20h],rbx; push rbp; push rsi; push rdi; sub rsp,180h; mov rdi,rdx` |
| `RequestEngineExit(const TCHAR*)` (call) | 0x012C2B70 | `sub rsp,28h; mov cl,1; call FGenericCrashContext::SetEngineExit; mov byte [GIsRequestingExit],1; ret`. This is a flag only, so the engine leaves its loop normally. |

- These are the only three `FEnhancedInputActionEventDelegateBinding<...>::Execute` instantiations in the PDB, so the handled count covers every binding type in this build.
- Every hook is created through the existing `resolve_all`/`create_and_enable`, so a failure is FATAL-logged and install returns false. The card 014 hooks sit before Tick, so Tick stays last.

### Step 1: who writes bit 0 of actor+0x5D (static)
- **Writers found:**
  - `AActor::SetActorEnableCollision` 0x037F2280, at +0x50.
  - The AActor constructor's `InitializeDefaults` path, at +0xAB. This is the only constructor or serialization writer: it sets the bit to 1 as the class default.
- **ST game code calls `SetActorEnableCollision`; it does not write the bitfield directly.**
  - There are 62 callers.
  - None are in `PossessedBy`, `PostInitializeComponents`, or the spawn paths.
  - `ASTPlayerTransportDropship::EnterDropship` +0xB9 calls it with `false`.
    - Before that call, +0x90..+0xB9 does `rcx=[pawn+0x330]` (the CMC), `mov edx,[rcx+1A0h]`, `mov [dropship+74Ch],edx`, then `mov dword [rcx+1A0h],0`.
  - `DescendToLandCompleted` +0xC8 calls it with `true`. It returns before that point when dropship+0x749 == 0 or when there is no ST PC.
- **No direct `and`/`btr` on bit 0 of `[reg+5Dh]` outside those two.** So candidate (b), a hidden direct write on the client, has no static support. The remaining options:
  - a `SetActorEnableCollision(false)` caller whose log line was hidden. Card 013's client log line 155 shows the shared cap of 16 used up by `BP_AISpawnPoint_C`, so a later pawn write would not have been logged. Characters now have their own cap.
  - a blueprint caller (logged through `execSetActorEnableCollision`, with the BP node).
  - (a), a BP CDO with the bit clear. Now logged: the CDO is `UClass+0x110`, from `UClass::GetDefaultObject` `cmp qword [rcx+110h],0` at +0x6 and `mov rax,[rbx+110h]` at +0x1C.
- **No matching host and client point was found statically.** So the fix stays the per-Tick guard the card allows.

### Step 2: diagnostics added (capped)
- `pawn-collision: <side> class <C> CDO <p> bActorEnableCollision <0|1>`: once per player-pawn class per process (8 classes max).
- `pawn-collision: <side> PostInitializeComponents|PossessedBy(<pawn> <C>, LocalRole r RemoteRole r, net mode n, controller p) bActorEnableCollision a -> b`: both sides, 8 per map.
  - These are `ASTPlayerCharacter`'s own overrides, so every pawn they see is a player pawn.
- `possess: ...` lines (SetPawn, OnRep_Pawn, AcknowledgePossession, ClientRestart) now end in `, pawn bActorEnableCollision <0|1|-1>`.
- `pawn-collision: client own pawn <p> <C>[ (new pawn)]: bActorEnableCollision a -> b at tick N since map load, LocalRole r, last hook <H>, CMC <p> +0x1A0 <dword>`: client only, at most 8 per map.
  - The check runs once per `UGameEngine::Tick`, after the original.
  - Identity is the pawn's FName value, compared Tick to Tick. No pointer is kept.
  - `last hook` is the last cold hook entered. Possible values: SetPawn, OnRep_Pawn, AcknowledgePossession, ClientRestart, SetActorEnableCollision (characters), EnterDropship, DescendToLandCompleted, PostInitializeComponents, PossessedBy, LoadMap.
- Collision-writer lines for characters (`SetActorEnableCollision` on a character, capsule `SetCollisionEnabled`, dropship) have their own cap of 16 per map: `diag: character collision write ... further events not logged`. Other actors keep the old cap.

### Step 3: the fix
- In `own_pawn_collision_step`, the fix runs when all of these hold:
  - the bit is 0;
  - the pawn is the first PC's Pawn;
  - LocalRole == 2 (AutonomousProxy);
  - net mode is NM_Client;
  - the pawn `IsA ASTPlayerCharacter`.
- The fix calls the game's own `SetActorEnableCollision(pawn, true)` through the hook's trampoline (`g_orig_set_actor_collision`), so the components get their collision update.
- Log line, at most 8 per map: `pawn-collision: FIX client own pawn <p> <C>: SetActorEnableCollision(true), bActorEnableCollision 0 -> 1 at tick N (re-enable K this map)`. It is prefixed `WARN ` if the bit is still 0 afterwards.
- Each client rot row ends with: `rot[k]: own pawn collision re-enables since last row X (map total Y), bActorEnableCollision B`.
- Remote pawns, other actors and the capsule's own CollisionEnabled are not touched. Server movement checks are unchanged.

### Step 4: input window
- At a successful LoadMap return the rows are armed but not started:
  - `diag: client map loaded; input snapshot in 5000 ms; rot rows wait for the first non-mouse key`
  - `diag: host/standalone map loaded; ...; srv rows wait for the first non-mouse key or remote client move`
- The window opens on the first `UEnhancedPlayerInput::InputKey` with AmountDepressed != 0 whose key name has no "Mouse" in it: `rot: input window opens on key W (amount 1.000), 20 rows follow` (`srv:` on the host).
  - Opening resets every row counter, so row 0 covers the first second of input.
  - With `UBWCOOP_AUTOINPUT=1`, only a scripted key opens it, so the rows cover the script: it starts at W and lasts 15.6 s, inside the 20 rows.
- **Deviation, host:** an unattended host has no input of its own. So the srv window also opens on the first `ServerCheckClientError` with a nonzero client acceleration:
  - `srv: input window opens on remote client acceleration |a| X (cmc=p), 20 rows follow`.
  - The srv rows are still emitted from inside ServerMove, as in card 011.
  - The old "first ServerMove after host LoadMap" start is gone.

### Step 5: per-action table and dispatch
- The table replaces card 013's IA_Look/IA_Move pair. It holds up to 12 actions keyed by the UInputAction FName value; later actions are summed in `(other: table full)`.
  - A row is taken when an action has a nonzero raw value or a binding Execute.
- Per row and per active action:
  - `rot[k]: action IA_X: mapping evaluations, raw nonzero (max |v|), after mapping modifiers nonzero (max |v|), mapping modifiers, mapping triggers None/Ongoing/Triggered, binding Execute N`.
  - Modifier lines follow only when raw was nonzero and every modified value was zero.
  - `rot[k]: actions: none active since last row` when nothing was active.
- **Dispatch, from `UEnhancedPlayerInput::EvaluateInputDelegates`:**
  - +0x1037 walks InputComponent+0x140 (the binding array).
  - +0x1063 reads binding+0x10 (the weak UInputAction).
  - +0x10FC indexes PlayerInput+0x4E8 ActionInstanceData (0x70-byte elements, instance at +8).
  - +0x1134 compares binding+0x18 (the TriggerEvent) with instance+0x13.
  - +0x1165 clones the instance.
  - +0x1623 calls the binding's vtable +8, which is `Execute(const FInputActionInstance&)`.
  - The Dynamic Execute at +0x1D reads `[instance+0]` as the SourceAction. The handled count reads that before calling the original.
- "Triggered N, binding Execute 0" for an action means its trigger fired but no binding on the PC/pawn input component matched (no binding, or a TriggerEvent mismatch).

### Addendum: autoinput
- **Env:**
  - `UBWCOOP_AUTOINPUT` unset: `config: UBWCOOP_AUTOINPUT unset -- no scripted input`.
  - `=1`: `config: UBWCOOP_AUTOINPUT=1 -- scripted input after the local PC acknowledges its pawn`.
  - Any other value: a WARN, and the script stays off.
  - When off, the extra cost is one branch per Tick and one per AcknowledgePossession.
- **Call path:**
  - `UGameViewportClient::InputKey` +0x3CB..+0x3F4 and `::InputAxis` +0x283..+0x2AC run their console and UI checks, then call `[PC vtable + 0xDA8]` with an `FInputKeyEventArgs`.
  - That slot is `APlayerController::InputKey(const FInputKeyEventArgs&)` 0x0404EB10. `ASTPlayerController` has no override; the only InputKey overrides in the PDB are APlayerController's 0x0404EB10 (args) and 0x0404EE30 (params).
  - That function then calls `PlayerInput`(+0x420)`->InputKey`, which is `UEnhancedPlayerInput::InputKey` (hooked).
  - The script calls the PC slot: the highest point after the viewport's own input-mode checks.
  - MouseX/MouseY go in as `IE_Axis` (4) events with a per-tick delta, the same as `InputAxis` sends them, so the engine's Mouse2D pairing runs.
  - On first use the slot is checked against 0x0404EB10, once per process:
    - `autoinput: PC vtable +0xDA8 -> exe+0x404EB10 (APlayerController::InputKey exe+0x404EB10) match`;
    - or `ERROR autoinput: ... MISMATCH -- script stopped, nothing sent`.
- **Layouts:**
  - FKey is 0x18 bytes: FName at +0, then the TSharedPtr<FKeyDetails> (object +8, controller +0x10). The wide ctor leaves the TSharedPtr null, so `FKey::~FKey` (0x015B75F0, already used by card 010) has no reference to release.
  - FInputKeyEventArgs is 0x48 bytes. The ctor writes:
    - +0 viewport (null, as for a pointer-less device);
    - +8 ControllerId;
    - +0xC device id (0);
    - +0x10 an FKey copy (destroyed after the call);
    - +0x28 event (0 Pressed, 1 Released, 4 Axis);
    - +0x2C AmountDepressed;
    - +0x30 DeltaTime (the ctor's 1/60);
    - +0x34 NumSamples (1);
    - +0x38 bIsTouchEvent;
    - +0x40 timestamp (the QPC value passed in).
  - `UEnhancedPlayerInput::InputKey` reads the key at +0x10 and the amount at +0x2C, which matches the card 012 offsets.
- **Timing:**
  - The script is armed by AcknowledgePossession of the local PC with a pawn, once per map, and only in a networked world: the net mode must not be NM_Standalone, so the front-end menu never gets keys.
  - It runs from `UGameEngine::Tick` before the original and starts 5000 ms after arming.
  - LoadMap entry sends Released for every held key and disarms.
- **Step table:** as in the card.
  - Each step start logs a line, for example `autoinput: step 0 W hold 1.0 for 4000 ms`.
  - At the end: `autoinput: script done (N InputKey calls, M returned false); net client exits in 8000 ms`.
  - 8000 ms later on the client: `autoinput: RequestEngineExit`. The host keeps running.
  - Every button gets exactly one Pressed and one Released.

### Decision table for the card 014 run
| Observation | Meaning | Next |
|---|---|---|
| `class ... CDO ... bActorEnableCollision 0` for BP_PlayerCharacterMI_C | (a): the class default is off and the host turns it on somewhere unhooked | look for the host point in the PostInit/PossessedBy lines; the guard covers the client |
| client `own pawn ... 1 -> 0` with `last hook SetActorEnableCollision` plus a character collision line | a hooked caller clears it; the caller RVA and BP node are in that line | fix at that caller in card 015 |
| client `own pawn ... (new pawn) -1 -> 0` at a low tick, no writer line | the replicated spawn arrives with the bit 0 (CDO or spawn path) | the guard is the fix |
| `FIX ... 0 -> 1` once, then rows show `re-enables since last row 0` | the fix holds | check movement below |
| `FIX` re-enables every row | something clears it again each tick | the change line's `last hook` names it |
| `WARN pawn-collision: FIX ... 0 -> 0` | SetActorEnableCollision did not stick | static follow-up |
| **Fix works**: the client `chain` rows while W is held (rows 0–3, 9–12) show `SafeMoveUpdatedComponent` > 0, `rot` ClientAdjustPosition near 0, and srv corrections near 0 | movement fixed | jump: check IA_Jump below |
| `action IA_Look ... Triggered > 0, binding Execute 0` | look trigger fires, no handler bound or matched | static: the PC's binding list (InputComponent+0x140) |
| `action IA_Look ... raw nonzero > 0, after modifiers nonzero 0` | a modifier zeroes look; the modifier lines name it | static: that modifier |
| no `action IA_Look` line during rows 4–7 | MouseX/MouseY never become an IA_Look raw value (mapping missing) | card 012 mapping lines |
| `autoinput: ... MISMATCH` | vtable slot differs; the script sent nothing | static recheck of +0xDA8 |
| no `rot: input window opens` line | no scripted key reached EnhancedPlayerInput (PC Pawn/PlayerInput missing) | `autoinput: armed` / `WARN autoinput: no local PlayerController` lines |

### Risks
- Re-enabling collision while the pawn is meant to be inside a dropship. EnterDropship deliberately disables it. Card 013 saw no EnterDropship, but if one runs on the client, the guard will undo it every Tick (visible as FIX lines after an `EnterDropship` collision line).
- The guard calls `SetActorEnableCollision` every Tick while the bit will not stay set. That cost is bounded; logging is capped at 8.
- An unattended host only gets srv rows if a client move with nonzero acceleration arrives; scripted W gives that.
- The autoinput timestamp is QPC, not `FPlatformTime::Cycles64` scaled. Only Enhanced Input's key-hold timing reads it.

## Senior review (2026-11-02)

Code reviewed (autoinput.cpp, the guard, the new hooks): nothing blocking. Clean rebuild: 84 exports, KERNEL32-only imports.

Runs (`tools/run_pair.sh`, logs `logs/card014{,b,c,d}-*`):

- `card014`: the host crashed after LoadMap in `USTConversationSubsystem::StartStage` → `TMulticastScriptDelegate::CompactInvocationList` (read of 0xA78, null owner). There were no mod frames on the stack. The cause is `-nosound`: three later runs with an audio device (routed to a PulseAudio null sink) did not crash. `run_pair.sh` no longer passes `-nosound`.
- `card014b`: the client script ran 5 s after the acknowledge, while the client was still in the dropship intro.
  - `EnterDropship` ran on the client for the replicated host dropship (LocalRole 1). It disabled the client's own pawn collision (caller exe+0x51C577E, inside `ASTPlayerTransportDropship::EnterDropship`). This is the bActorEnableCollision 0 writer card 013 was looking for.
  - The guard re-enabled collision at tick 4 (the risk this card names).
  - Even with collision on: PerformMovement 0, StartNewPhysics 0, ReplicateMoveToServer 0, MovementMode 5. IA_Move and IA_Look are actuated (raw nonzero) but never reach Triggered. IA_Sprint reaches Triggered with binding Execute 0. 480 of 492 InputKey calls returned false.
- `card014c`: client script delayed 200 s with `UBWCOOP_AUTOINPUT_DELAY_MS` (new, see below). The host idled 240 s in `BP_PlayerTransportDropship_VerySlow_C` (host PC's Pawn is the dropship, bShowMouseCursor 1, MouseCaptureMode 0). `DescendToLandCompleted` never ran on either side. Client rows were identical to `card014b`.
- `card014d`: the host ran the script too (30 s delay: W, mouse, Space, Shift+W, D, LMB through `APlayerController::InputKey`). The dropship still did not land in about 165 s.

Conclusion: every input row so far was taken during the dropship intro, where move and look are expected to be blocked. Whether client input works on foot is still unknown. The unattended loop is stuck until something advances the host's dropship. Card 015 takes this on.

Senior change: `UBWCOOP_AUTOINPUT_DELAY_MS` (config.cpp/.h, autoinput.cpp/.h, hooks.cpp/.h, dllmain.cpp). It sets the script start delay after the acknowledge; the default is 5000 and values are clamped to 600000. The travel-delay parser now takes the variable name as a parameter.

Status: diagnostics done; the guard stays for now, but card 015 must make it dropship-aware.
