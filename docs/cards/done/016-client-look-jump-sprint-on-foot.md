# Card 016: Client on foot: why IA_Look never triggers, why jump does nothing, what sprint changes

Read `docs/STATE.md` first: the "Mod code rules" section, the "Runtime facts" line about `tools/run_pair.sh`, and rows 014 to 016. Then read card 015's report and its senior review. Repo root: `/path/to/ultimate-bug-war-coop`. Run logs: `logs/card015{k,s}-{host,client}.log`. Symbols: `tools/symbols.txt`.

## Why

Card 015 got both players on foot unattended, and client movement now works. The client's input rows on foot (runs card015k and card015s) show:

| Action | After modifiers | Trigger state | Binding Execute | Effect |
|---|---|---|---|---|
| IA_Look | max 0.35 to 0.41 (raw 5.0 times a 0.07 scale) | None on every evaluation, Triggered 0 | 0 | ControlRotation stays at Y 90; `HandleInputLook` 0 |
| IA_Jump | 1.0 | None, Triggered 0 | 2 | no PhysFalling, Z unchanged |
| IA_Sprint | 1.0 | Triggered 58 to 118 per row | same | MaxWalkSpeed stays 700 |

The IA_Look mapping has no triggers of its own, so the trigger state comes from the action asset's triggers or from the implicit actuation test. One unverified guess: the actuation threshold (often 0.5) is above the scaled scripted value. A real mouse moving tens of pixels per frame would pass it, so look may already work for a human on foot. Every earlier look test (cards 010 to 014) was inside the dropship, and every scripted look so far was injected at `InputKey`. This card simulates real mouse movement through the OS so the whole input path is tested unattended.

## Do

1. **Look, at runtime first, with simulated real mouse movement.** The current script injects MouseX/MouseY at `APlayerController::InputKey`, which skips the OS, Slate and the viewport. Add a second look path to the autoinput script that moves the mouse the way a human does: `SendInput` with `MOUSEEVENTF_MOVE` (relative deltas), so the deltas arrive as raw input (`WM_INPUT`) and pass through the game's whole input path.
   - Resolve `SendInput` at runtime from `user32.dll` with `GetModuleHandleW` and `GetProcAddress`; the DLL must stay KERNEL32-only in its imports.
   - Send human-sized deltas: 10 px and 30 px per frame on X, then on Y, each for at least 1 s. Put the step list in the script as data.
   - `SendInput` goes to the foreground window. Log once, before the first move, whether the client window is the foreground window (`GetForegroundWindow` against the game's window) and the viewport's mouse capture state. If the client window is not in the foreground, say so and do not move the mouse.
   - Also rerun the existing `InputKey` path with MouseX at 20.0 and at 50.0 (1.4 and 3.5 after the scale), so both paths can be compared.
   - Report, per row and per path, IA_Look's raw and after-modifier values, trigger states, Triggered and Execute counts, `HandleInputLook` and ControlRotation.
2. **Look, statically.** Find where the trigger state for a mapping with no triggers is decided in `UEnhancedPlayerInput` (the evaluation your `detour_evaluate_triggers` already wraps, and its caller). Name the threshold it compares against and where that threshold comes from (the action asset, the mapping, or a default), with RVAs. Read IA_Look's action-level triggers and value type if they can be read at runtime from the `UInputAction` object, and log them once per map.
   - If step 1 triggers look, the threshold is the cause, and real mouse look should work on foot. Say so and stop there for look; do not change the game's scaling.
   - If look still does not trigger at 3.5, find which condition fails and report it. Do not patch the trigger evaluation yet.
3. **Jump.** Find what the two IA_Jump binding executions call, and why no jump follows. Check the BP jump handler path to `ACharacter::Jump` / `CanJump` / `CheckJumpInput` on the client pawn, and log once per press what each returns. If the trigger needs a longer or shorter press than the script's 100 ms, try one other press length and say which one works.
4. **Sprint.** Find what the IA_Sprint handler writes. Log, once per row while sprint is held, the client pawn's speed fields (`MaxWalkSpeed` and whatever field the handler writes) on the client and, from the host log, the server's copy. Say whether sprint is gated (stamina, state, authority) or applies to a field other than `MaxWalkSpeed`.
5. Keep every new log capped per map and log-only. Do not fix anything in this card unless the cause is proven and the fix is one call to a game function the player's own path would make; say why if you do.

## Done when

- The report shows IA_Look's rows for the `SendInput` path (10 px and 30 px) and the `InputKey` path (20.0 and 50.0), names the threshold and its source with RVAs, and gives a verdict on whether real mouse look works on foot.
- The report names what the IA_Jump bindings call and the first condition that stops the jump.
- The report names what IA_Sprint writes and why the speed does not change.
- Clean build (`/W4 /WX`), 84 exports, KERNEL32-only imports. No new crash dumps in the runs.

## Runs

- Build: `python3 -I tools/gen_proxy_exports.py && python3 -I tools/pdb_rva.py gen && cmake --build build --clean-first`.
- Run: `DEPLOY=1 HOST_EXTRA_ENV=UBWCOOP_AUTODROPSHIP=1 CLIENT_EXTRA_ENV="UBWCOOP_AUTOINPUT=1 UBWCOOP_AUTOINPUT_DELAY_MS=40000" DONE_PATTERN='autoinput: script done' tools/run_pair.sh <tag> 240`. Never pass `-nosound`; it crashes the host. Audio goes to a null sink by default.

## Risks

- `SendInput` moves the mouse for the whole Wine desktop. Both instances share one wineserver, so the moves go to whichever window has focus. The foreground check in step 1 must gate them.
- Larger look deltas may also rotate the pawn quickly and change the movement rows. Keep the look steps after the move steps, as now.
- The host still shows the brief widget after the autodropship unpause (STATE known issue). Do not work on it in this card.

## Report

Runs: `card016a`, `card016b`, `card016c` (the last with host autoinput as a baseline). All three exited rc 0 on both sides, with FATAL/ERROR 0, `new crash dumps: none`. Build is clean (`/W4 /WX`), 84 exports, `KERNEL32.dll` is the only import.

### Look: verdict, real mouse look does not work on foot on the client

The cause is not the threshold and not the input path. IA_Look's second action-level modifier multiplies the value by a GAS attribute that is 0 on the client.

- IA_Look has ValueType 2 (Axis2D), 0 action-level triggers, and 2 action-level modifiers:
  1. `USTInputModifierTimeDilationLookScale` (`ModifyRaw_Implementation` exe+0x0512E260). It is a passthrough unless time dilation is not 1.
  2. `USTInputGASPlayerAttributeModifier` (`ModifyRaw_Implementation` exe+0x0512DF10). It does the following:
     - calls exe+0x0523E0F0(PlayerInput), then reads `+0x680` of the result (the ASC);
     - builds the `FGameplayAttribute` from modifier `+0x28` (exe+0x04925050);
     - calls `GetGameplayAttributeValue(asc, attr, &bFound)` (exe+0x0495E020);
     - if the attribute is found, multiplies x, y and z by it.
- The attribute is `CameraLookInputMultiplier`.
  - Client (card016b, card016c): `attribute CameraLookInputMultiplier: in (1.4000, 0, 0) -> out (0.0000, 0, 0)`, on every evaluation.
  - Host (card016c, same script): `in (1.4000, 0, 0) -> out (1.4000, 0, 0)`.
- Threshold: for a mapping or action with no triggers, the state is decided by |v|² >= 1e-8 (a double at exe+0x07AFFA28). It is tested in `ProcessActionMappingEvent` (exe+0x04475850, compare at exe+0x04475A96) and in `EvaluateInputDelegates` (exe+0x0446B2B0, compare at exe+0x0446C073, on the value after the action modifiers).
  - The 0.5 seen before is `UInputTrigger::ActuationThreshold` (`+0x28`, set in ctor exe+0x044649B0, read by `IsActuated` exe+0x05129CB0). It applies only to explicit triggers.
  - The mapping-level `EvaluateTriggers` (called from exe+0x04475A5D) runs before NoTriggerState is set, so the mapping-level "None" counts are an artifact of the hook.
- Action-level order in `EvaluateInputDelegates`:
  1. modifiers;
  2. NoTriggerState from the modified value;
  3. `EvaluateTriggers` over the action triggers (call at exe+0x0446C0DB);
  4. min with the mapping state if `bMappingTriggerApplied`;
  5. the `bGamePaused` / `bTriggerWhenPaused` (`Action+0x40`) check.

  Because the modified value is exactly 0, NoTriggerState stays None. That gives no Triggered, no binding Execute and no `HandleInputLook`.

Rows (client; the same in every look row of both paths):

| path | step | IA_Look raw max | after mapping modifiers | after action modifiers | action-level states | NoTriggerState Triggered | binding Execute | HandleInputLook | ControlRotation |
|---|---|---|---|---|---|---|---|---|---|
| InputKey | MouseX 20.0 | 20.0 | 1.4 | 0.0 | None 62, Ongoing 0, Triggered 0 | 0 | 0 | 0 | P 0 Y 90 R 0 |
| InputKey | MouseX 50.0 | 50.0 | 3.5 | 0.0 | None 118, Ongoing 0, Triggered 0 | 0 | 0 | 0 | P 0 Y 90 R 0 |
| SendInput | X 10 px | 20.0 | 1.4 | 0.0 | None only | 0 | 0 | 0 | P 0 Y 90 R 0 |
| SendInput | X 30 px | 60.0 | 4.2 | 0.0 | None only | 0 | 0 | 0 | P 0 Y 90 R 0 |
| SendInput | Y 10 / 30 px | (MouseY) | nonzero | 0.0 | None only | 0 | 0 | 0 | P 0 Y 90 R 0 |

- SendInput (card016a/b): the client window was in the foreground, viewport capture mode 1, lock mode 1. Each step sent about 231–240 events, with 0 skipped and 0 failed. The events reach `APlayerController::InputKey` as MouseX/MouseY at about 120 per second, so the OS, Slate and viewport path works.
- In card016c the client was not in the foreground; the gate logged `NOT foreground` and sent nothing, as designed.
- An InputTag.Fire press/release was seen once on the client during the card016b mouse steps. The source is unknown; it is not investigated here.

### Jump: first failing condition is that the client ASC's input-tag map is empty

Chain (SpaceBar fires both InputTag.Jump and InputTag.MoveUp):

1. IA_Jump has 1 action trigger (`InputTriggerPressed`, threshold 0.5) and ValueType 0. Its action-level state is Triggered once per press. The binding Execute runs 2 times per press.
2. `ASTPlayerController::HandleInputTag_Pressed` (exe+0x051CB780). Its checks pass: input receiver `PC+0x7C0` is non-null, and flags `PC+0x618` = 0x08 (bit 3 set).
3. `UADAbilitySystemComponent::AbilityInputTagPressed` (exe+0x04A0EA00) on the pawn's ASC (`BP_PlayerCharacterMI_C`). It looks the tag up in a `TMap<FGameplayTag, TArray<FGameplayAbilitySpecHandle>>` at `ASC+0x1260` (Num `+0x1268`, free `+0x1294`, 0x20-byte elements, handles TArray at `+0x8`). It returns at exe+0x04A0EBCA / exe+0x04A0EBCF when the map is empty or the tag is missing.
4. **Client (card016c), every press:** `input tag map (+0x1260) Num 0 free 0, this tag's spec handles -1 (-1 = not in map)`. This holds for InputTag.Jump, MoveUp, Move and Walk alike.
5. After that, nothing downstream runs on the client: no `TryActivateAbility` (exe+0x04976FA0), `InternalTryActivateAbility` (exe+0x04963860), `ASTPlayerCharacterMI::Jump` (exe+0x051CD5A0), `ACharacter::Jump` (exe+0x039B5850), `CanJump` (exe+0x039A7610) or `CheckJumpInput` (exe+0x039A81C0). `bPressedJump` is never set.

Press length: both 100 ms and 500 ms give the same result, so press length is not the gate.

Host baseline, inconclusive: the host's PC `HandleInputTag_Pressed` fired for Jump, MoveUp, Move and Walk with the same flags. Its `AbilityInputTagPressed` hook did not log, though (no inner line), and no `TryActivate` was logged on the host either. So whether the host's map is filled is not shown by this run. That needs one more probe on the host's ASC (which ASC the host PC routes to).

### Sprint: no speed write is reached

- LeftShift fires InputTag.**Walk** (there is no "Sprint" tag). It goes through the same `AbilityInputTagPressed` path and hits the same empty map, so the sprint ability never activates on the client.
- What the ability would write: `USTSprintAbility::HandleSprintChange(float)` (exe+0x051272C0) never runs on the client. It does not write `MaxWalkSpeed` directly. It applies a set-by-caller GameplayEffect:
  - GE class at `ability+0x3B0`;
  - `MakeEffectContext` (ASC vtable `+0x6D8`) and `MakeOutgoingSpec` (`+0x6D0`);
  - `SetSetByCallerMagnitude` (exe+0x049E1020; tag global exe+0x0943A8C8, tag at `ability+0x3C0`);
  - `ApplyGameplayEffectSpecToSelf` (`+0x6B8`); the handle is stored at `ability+0x3B8`.
- Speed fields:
  - Client `MaxWalkSpeed` is 700.0 in every row, including rot[21–23] while LeftShift+W is held (velocity 700 forward).
  - Host srv rows: `MaxWalkSpeed` 700.0 throughout.
  - So sprint is gated before the ability, by the same empty input-tag map. It is not gated by stamina, state or authority checks.

### Common cause (hypothesis, not proven)

Both defects point at the client's pawn ASC lacking GAS state that it should receive by replication:

- the attribute `CameraLookInputMultiplier` is found but is 0 (a default, not the host's value);
- the ability input-tag map is empty, and that map is filled when ability specs are given or replicated to the owner.

The likely cause is that the client's ASC never got its granted abilities or initialized attribute set, either through replication or through a client-side init step (an `InitAbilityActorInfo` or ability-set grant that only runs on the server or the host's local path). Next probe, for a follow-up card: the client ASC's `ActivatableAbilities` item count and the attribute set list, against the server's copy of the same pawn.

### Fixes

None applied. Neither cause has a proven one-call fix:

- Setting the attribute or populating the map by hand would hide the replication fault, not fix it.

### Log notes

- All new lines are capped per map and log-only.
- A tag repeat filter (250 ms, 8 per-tag slots) stops held keys (Move, Walk) from using up the jump cap. `HandleInputTag_Pressed` fires every frame while a key is held.

## Senior review (2026-11-02)

**Verdict: accepted.** The look, jump and sprint causes are shown in the logs, the runs are clean, and nothing was patched. The common cause moves to card 017.

### Checked
- `card016c-client.log`: `STInputGASPlayerAttributeModifier, attribute CameraLookInputMultiplier: in (1.4000, 0, 0) -> out (0.0000, 0, 0)` on every evaluation. Every `AbilityInputTagPressed` line (Move, Jump, MoveUp, Walk) shows `input tag map (+0x1260) Num 0 free 0, this tag's spec handles -1`. FATAL/ERROR 0, WARN 0 on both sides.
- `card016b-host.log`: the same modifier gives `in (-0.1400, 0, 0) -> out (-0.1400, 0, 0)`, so the multiplier is 1 on the host.
- SendInput: in card016a and card016b the client owned the foreground window (pid match), capture mode 1, lock mode 1, and each step sent 231 to 240 events with 0 failed. In card016c the client was not in the foreground and the gate sent nothing, as the card asked.
- Code: user32 is resolved once with `GetModuleHandleW`/`GetProcAddress`, and a failed lookup is logged as an ERROR and disables the mouse steps. The DLL keeps 84 exports and KERNEL32-only imports. The tag-map probe is read-only and bounded to 256 entries. No function over 60 lines in `autoinput.cpp` or `hooks.cpp`.

### Correction
- The report says the host's `AbilityInputTagPressed` hook did not log. That is true for card016c only. In card016b the host hook logged 31 lines (InputTag.Look and InputTag.Move, on the pawn ASC and on a `BP_Weapon_Unarmed_C` ASC), but that build's line had no map fields yet. The host map count is still unmeasured; card 017 takes it.

### Not covered
- The `InputTag.Fire` press seen once on the client during the card016b mouse steps.
