# Card 018: Client mouse look works on foot (required this card); then bind the replicated ability specs to their input tags

Read `docs/STATE.md` first: the "Mod code rules" section, the "Runtime facts" line about `tools/run_pair.sh`, and rows 016 to 018. Then read card 017's report and its senior review, including both corrections. Repo root: `/path/to/ultimate-bug-war-coop`. Run logs: `logs/card017{b,c}-{host,client}.log`. Symbols: `tools/symbols.txt`.

**Priority.** Client mouse look has taken cards 009 to 017. This card is done only when a client run shows the camera turning from simulated mouse movement (SendInput) on foot. Do part A first and finish it before you start part B. If part B is not finished, report it as open; part A is not optional.

## Why

Look has two blockers left, and both are now located:

1. **The look multiplier is 0 on the client.** `CameraLookInputMultiplier` (`USTAttributeSet_PlayerCamera` +0x40, no `CPF_Net`, constructor default 0) is set to 1 by `AddAttributeSetFromData(PlayerCameraData_C)` (0x04A0F060) at PostInit. `OnRep_SpawnedAttributes` (0x0496CB90) then replaces that set with the server's replicated subobject, where the value is 0. `USTInputGASPlayerAttributeModifier` multiplies every look value by it.
2. **The client loses mouse capture.** In card017c, the `UBWCOOP_CLIENT_GAMEONLY` step set capture 0 -> 1 at 448688466 ms. 3.2 s later the game called `SetInputMode(..., net mode 3, UIOnly)` on the same client PC (caller exe+0x364C1EA). Capture was 0 at the mouse step, so the SendInput moves never reached the game. card017b did not get that UIOnly call, so it depends on timing. A UI screen is probably opening on the client after it joins (the brief or "press any key" screen, as on the host). A real player would close it with a key press, and the game would then restore GameOnly itself.

The input-tag map (jump, sprint and the other tagged abilities) is the second half. `GiveAbility` returns -1 on the client at PostInit, so `BindInputTagToAbilitySpec` (0x04A0F870) binds nothing, and the 16 specs that replicate later are never bound.

## Do

### Part A: mouse look (required)

1. **Camera set audit (log only, once per map).** List every `FGameplayAttributeData` in `USTAttributeSet_PlayerCamera` with its `CPF_Net` flag and its value on the client (after the swap) and on the server's copy.
2. **Multiplier fix.** On the client only (own pawn, LocalRole 2), once after `OnRep_SpawnedAttributes` has put the replicated camera set in place:
   - If no attribute in the camera set has `CPF_Net`, re-run the game's own `AddAttributeSetFromData(asc, PlayerCameraData_C)`. It returns the replicated set through `GetOrCreateAttributeSubobject` and copies the data-asset values into it.
   - If the set has a replicated attribute, restore only the non-replicated ones, and say how.
   - Log the before and after values once.
   - Do not touch the other four sets in this part.
3. **Capture.** Find what calls `SetInputMode(UIOnly)` on the client after it joins. Hook or log the BP caller of exe+0x364C1EA, and name the widget class and the function that opens it.
   - If it is a screen a player closes with a key (the memory note: movie cutscenes are skipped or advanced with Enter), have the autoinput script close it the way a player would, through the path the widget listens on. Check that the game then restores GameOnly and capture 1 on its own.
   - If no player action closes it, re-apply `FInputModeGameOnly` on the client after that UIOnly call (extend the existing `UBWCOOP_CLIENT_GAMEONLY` step from once per map load to "after the last UIOnly"), and say why.
   - Before the first SendInput move, the autoinput script must log capture mode 1 and the client in the foreground. If either check fails, log a WARN and say so in the report.
4. **Verify look.** In one pair run, on the client:
   - the modifier line reads `CameraLookInputMultiplier: in X -> out X`;
   - IA_Look Triggered > 0, `HandleInputLook` > 0, and ControlRotation changes under the SendInput steps (10 px and 30 px on X and on Y) and under the `InputKey` steps;
   - the host's own look is unchanged.

### Part B: input-tag map (after part A passes)

5. On the client only (own pawn), when a spec arrives (`OnGiveAbility` 0x04969EB0, or once after `OnRep_ActivateAbilities` 0x0496BFB0):
   - Call `BindInputTagToAbilitySpec` with the replicated spec's handle and the input tag that the pawn's definition gives that ability class.
   - Read the tags from the same definition data that `InitFeaturesFromDefinition` (0x04A10540) reads; do not hard-code them.
   - Bind each tag at most once per ASC. Log the binds once, with the map Num before and after.
   - Grant nothing on the client (no `GiveAbility`).
6. Verify in the same or a second run:
   - the client map Num equals the host's (8 tags);
   - SpaceBar causes PhysFalling and a Z change;
   - Shift+W raises speed above 700, or the report shows the sprint effect applied on the server and why the speed did not change;
   - the server copy of the client pawn shows the same ability activations.

## Done when

- **Part A (required):** the client run's rows show ControlRotation changing under SendInput, with capture mode 1 and the foreground check logged, and the multiplier at 1. The capture-loss caller is named.
- Part B is done, or reported as open with how far it got.
- Clean build (`/W4 /WX`), 84 exports, KERNEL32-only imports. No new crash dumps in the runs.

## Runs

- Build: `python3 -I tools/gen_proxy_exports.py && python3 -I tools/pdb_rva.py gen && cmake --build build --clean-first`.
- Run: `DEPLOY=1 HOST_EXTRA_ENV=UBWCOOP_AUTODROPSHIP=1 CLIENT_EXTRA_ENV="UBWCOOP_AUTOINPUT=1 UBWCOOP_AUTOINPUT_DELAY_MS=40000" DONE_PATTERN='autoinput: script done' tools/run_pair.sh <tag> 240`. Never pass `-nosound`; it crashes the host. Audio goes to a null sink by default.

## Risks

- Re-initialising a replicated attribute on the client would desync it from the server with no correction; that is why step 2 has the `CPF_Net` check.
- `OnRep`, `OnGiveAbility` and UI calls can repeat (respawn, map change). Key the once-only state per ASC or per PC and reset it per map.
- Hooks taking UE by-value structs (`TSubclassOf`, `FGameplayAbilitySpec`) receive a hidden pointer. card017a crashed on exactly this, so check each new detour signature against the disassembly.

## Report

Runs:

- `card018a` (part A): ended by done after 126 s. Host rc 0, client rc 0, FATAL/ERROR/WARN 0, no new crash dumps.
- `card018b` (part A + part B): ended by done after 125 s. Host rc 0, client rc 0, FATAL/ERROR/WARN 0, no new crash dumps.
- Build: clean (`/W4 /WX`), 84 exports, KERNEL32-only imports, for both runs.

All new code is under "Card 018" in `mod/src/hooks.cpp` and `tools/symbols.txt`, plus the WARN prefix in `mod/src/autoinput.cpp` `log_mouse_state`. New symbols:

- hooks: `UAbilitySystemComponent::OnRep_SpawnedAttributes` (0x0496CB90), `UWidgetBlueprintLibrary::execSetInputMode_UIOnlyEx`, `UAbilitySystemComponent::OnRep_ActivateAbilities` (0x0496BFB0);
- call: `UAbilitySystemComponent::FindAbilitySpecFromClass` (0x0495B9E0; its `TSubclassOf` is passed by hidden pointer).

Per-map state (`C18State`) holds only FNames (ASC name, bound tag names) and is reset with card 017's in `reset_card017_map`.

### Part A: mouse look (passes)

**1. Camera set audit.** `USTAttributeSet_PlayerCamera` has 6 attributes. None has `CPF_Net`; all have flags 0x0020080000000015.

| Attribute | Offset | Client after `OnRep_SpawnedAttributes` | Client after re-init | Server copy (host) |
|---|---|---|---|---|
| CameraFieldOfView | 0x30 | 0.000 / 0.000 | 90.000 / 90.000 | 90.000 / 90.000 |
| CameraLookInputMultiplier | 0x40 | 0.000 / 0.000 | 1.000 / 1.000 | 1.000 / 1.000 |
| CameraPitchMax | 0x50 | 0.000 / 0.000 | 89.900 / 89.900 | 89.900 / 89.900 |
| CameraPitchMin | 0x60 | 0.000 / 0.000 | -89.900 / -89.900 | -89.900 / -89.900 |
| CameraYawMax | 0x70 | 0.000 / 0.000 | 359.000 / 359.000 | 359.000 / 359.000 |
| CameraYawMin | 0x80 | 0.000 / 0.000 | 0.000 / 0.000 | 0.000 / 0.000 |

Values are shown as base / current. The pitch and yaw limits were also 0 on the client, not only the multiplier.

**2. Multiplier fix.**

- Where it runs: `detour_onrep_spawned_attributes`. It runs on the client only, and only for a player-pawn ASC whose owner is LocalRole 2. If the role is not 2 yet, the ASC name is kept as pending and `card018_slot_step` retries the fix from the tick.
- What it does: it walks the camera set's `ChildProperties`. With no `CPF_Net` attribute present, it finds the `PlayerCameraData_C` entry in the pawn definition CDO's attribute-init array (the entry whose class at +0x28 is the camera set class). It then calls the game's own `AddAttributeSetFromData(asc, &init)` once per ASC per map.
- Result, from the log: `card018: client camera re-init AddAttributeSetFromData(PlayerCameraData_C) on AbilitySystemComponent (pawn BP_PlayerCharacterMI_C LocalRole 2): CameraLookInputMultiplier base 0.000 current 0.000 -> base 1.000 current 1.000, camera sets 1 -> 1`. It reuses the replicated subobject (still one camera set) and touches no other set.

**3. UIOnly caller.**

- The new hook on `execSetInputMode_UIOnlyEx` names the Blueprint caller of exe+0x364C1EA. Two callers were seen:
  - `WB_SPCAMPOS_1_TitleScreenWidget_v2_C`, function `ExecuteUbergraph_WB_SPCAMPOS_1_TitleScreenWidget_v2`. This is the front-end title screen, net mode 0, on both instances before the session starts.
  - `BP_HUD_Manager_C` function `Set Input Mode UI`, on the level actor `BP_HUD_Manager_MI_C` (outer `PersistentLevel`). Seen on the host only, 2628 ms after LoadMap, with `bShowMouseCursor 0`.
- The card017c client call matches the second caller: same caller exe+0x364C1EA, `bShowMouseCursor 0`, about 3.3 s after the map loaded. So the likely culprit is the in-mission HUD manager's `Set Input Mode UI`, which opens the brief / any-key stack (`WB_Stack_C`, card 015).
- **It did not happen on the client in either card018 run.** The client logged only the title-screen call. The name above comes from the host side plus the card017c match; it was not observed on the client this card.
- Because of that, nothing was changed for it. No autoinput close step was added and no extra GameOnly re-apply was added: neither could be checked without the event. The hook stays in place to name the client call when it recurs.
- Minor log issue: the hook labels calls made before the session starts as "host", because `side()` is decided by `g_client_session`. The client's title-screen line reads "host".

**Capture and foreground check before the first SendInput step.** It is logged. A failing check now prints `WARN`:

`autoinput: mouse path: user32 resolved, foreground window pid 884, own pid 884 -> foreground, SendInput on; viewport ... capture mode 1 lock mode 1`

This line was logged in both runs. Every SendInput step reported `sent 239/240, skipped (not foreground) 0, failed 0`.

**4. Look verification (card018a client rows).**

- Modifier: `STInputGASPlayerAttributeModifier, attribute CameraLookInputMultiplier: in (1.4000, 0.0000, 0.0000) -> out (1.4000, 0.0000, 0.0000)`. In card017 the output was 0.

| Step | Rows | HandleInputLook per row | IA_Look action-level Triggered | ControlRotation |
|---|---|---|---|---|
| 0, W hold (baseline) | rot[0–2] | 0 | n/a | P 0.00 Y 90.00 |
| 1, InputKey MouseX 20 | rot[3–4] | 62, 120 | 62, 120 | Y 263.60, then 239.60 |
| 2, InputKey MouseX 50 | rot[5–7] | 60, 120, 118 | 60, 120, 118 | Y 56.00, 176.00, 282.00 |
| 3, SendInput X 10 px | rot[8–9] | 62, 121 (InputAxis 124, 242) | 62, 121 | Y 95.60, then 74.40 |
| 4, SendInput X 30 px | rot[10–12] | 60, 120, 117 | 60, 120, 117 | Y 259.20, 187.20, 90.00 |
| 5, SendInput Y 10 px | rot[13–14] | 63, 120 | 63, 120 | P 270.10 (= -89.9, the PitchMin clamp) |
| 6, SendInput Y 30 px | rot[15–17] | 60, 120, 117 | 60, 120, 117 | P 270.10 (held at the clamp) |

- Yaw turns under both the InputKey and the SendInput steps.
- Pitch moves under SendInput Y and stops at the now-correct -89.9 limit. Before the re-init, PitchMin/Max were 0, which would also have pinned pitch.
- Host look: the card 018 code changes nothing on the host. The host's only card018 lines are the server-copy audit and the UIOnly log, and the host received no look input. The host has no rot rows, so "unchanged" rests on that, not on a measured ControlRotation.

### Part B: input-tag map (done except sprint speed)

**Implementation.**

- Trigger: `detour_onrep_activate_abilities` runs on the client for a player-pawn ASC. If the pawn is LocalRole 2 it binds at once; otherwise it sets `bind_pending`, and `card018_slot_step` binds once the pawn is LocalRole 2 and `ActivatableAbilities` > 0.
- `card018_bind_specs` reads the pawn definition CDO's ability array, the same data `InitFeaturesFromDefinition` reads: +0x28 / Num +0x30, 0x18 per entry, ability class at +0, input tags `TArray<FGameplayTag>` at +0x8 / Num +0x10.
- For each entry it calls `FindAbilitySpecFromClass` and binds every non-zero tag not yet bound on this ASC through the game's `BindInputTagToAbilitySpec` (the existing card 017 detour, which logs each bind) with `spec + 0xC` (the handle).
- Tags already bound are kept by FName per ASC per map, so each tag binds at most once. Entries whose spec has not replicated yet are counted as waiting and are retried on the next OnRep.
- Nothing is hard-coded and the client calls no `GiveAbility`.

**Result (card018b client).**

- `card018: client spec bind on AbilitySystemComponent (AD_MIPlayerAbilityDefinition_C, 16 definition entries): bound 8 tags, 0 waiting for their spec, input tag map Num 0 -> 8, ActivatableAbilities 16, pending binds 0`. Num 8 matches the host.
- Each tag went to the replicated handle: Walk 30, Jump 31, Taunt 33, SwitchTech 35, Skill.Ability 40, HumanMelee 42, Crouch 43, Objectives 44.
- The second Jump tag in the definition was skipped because the tag was already bound, as on the host (where the second bind also leaves Num at 8).
- `AbilityInputTagPressed` now reports `input tag map (+0x1260) Num 8`.

**Jump: works.**

- Steps 7 and 8 (SpaceBar) were followed by `ACharacter::Jump`, then `CheckJumpInput` with MovementMode 1 -> 3 (Falling).
- rot[18–20] show MovementMode 3 and Velocity Z 15.8, then 751.0, then -719.4. The pawn left the floor (`CurrentFloor flags 0x0`) and landed again (rot[21] MovementMode 1).
- The server shows the activation: `InternalServerTryActivateAbility(handle 31, input pressed 0)` then `InternalTryActivateAbility(handle 31) = 1, ability GA_HumanJump_C`, at both SpaceBar presses (450164071, 450165571 ms).

**Sprint: activates on the server, speed does not rise. Open.**

- During Shift+W (steps 9 and 10), the server logs `InternalTryActivateAbility(handle 30) = 1, ability GA_Sprint_C`. It fires repeatedly about every 8 ms with `input pressed 1`; the log cap stopped at 12 entries.
- Client rot[21–23] and host srv[22–23] both show `MaxWalkSpeed 700.0` and speed 700.0. There were no ClientAdjustPosition corrections in those rows.
- So the input path and the server activation now work, but the sprint effect does not reach `MaxWalkSpeed` on either side.
- Not established: whether the ability ends immediately (activating again every frame suggests so), and whether the host's own pawn reaches more than 700 when it sprints. No host-own sprint baseline exists.
- Suggested next step: hook `USTSprintAbility::HandleSprintChange` (exe+0x051272C0) and the GE apply on the server, and run a host-only Shift+W baseline.

### Done-when check

- Part A: passes. ControlRotation changes under SendInput, capture mode 1 and the foreground check are logged, and the multiplier is 1. The capture-loss caller is named as `BP_HUD_Manager_C.Set Input Mode UI` from the host log and the card017c match; it was not reproduced on the client this card.
- Part B: map bound (Num 8), jump works, and the server activations are shown. Sprint speed is open, as described above.
- Clean build, 84 exports, KERNEL32-only imports, no new crash dumps.

## Senior review (2026-11-03)

**Accepted. Part A passes; part B passes except sprint speed, which moves to card 019.**

Checked against the logs, not the report:

- Part A (card018a client): the re-init line shows `CameraLookInputMultiplier base 0.000 ... -> base 1.000`, with camera sets 1 -> 1. The modifier line reads `in (1.4000, ...) -> out (1.4000, ...)`. `capture mode 1 lock mode 1` and the foreground match are logged before the first SendInput step. ControlRotation yaw changes on every SendInput X row, and pitch stops at 270.1 (-89.9) on the Y rows. FATAL/ERROR/WARN 0 in both runs. **Client mouse look works.**
- The rot[12] yaw of exactly 90.00 is a coincidence of per-row deltas, which vary with frame time. It is not a stuck value.
- Part B (card018b): the client bind line shows `bound 8 tags ... input tag map Num 0 -> 8, ActivatableAbilities 16`. Jump: rot[18] has MovementMode 3, and the host logs `InternalTryActivateAbility(handle 31) = 1, ability GA_HumanJump_C` at 450164071 ms. **Client jump works.**
- Sprint: the host logs `InternalTryActivateAbility(handle 30) = 1, ability GA_Sprint_C` 12 times (the log cap), spaced 8 to 17 ms apart, which is once per server frame. MaxWalkSpeed stays 700.0 on both sides. A sprint ability that activates once per frame is probably ending or being cancelled each frame. That fits card 016's finding that `HandleInputTag_Pressed` fires every frame while the key is held. This is a lead, not a result; card 019 checks it.
- UIOnly capture loss: not reproduced on the client this card. The caller is named from the host log (`BP_HUD_Manager_C`, `Set Input Mode UI`) and matches card017c's client call by caller address and timing. The hook stays as the detector, and no fix was made. That is correct: no fix could have been checked.
- The pre-session lines labelled "host" on the client are a cosmetic log issue. Fix it when that code is next touched.
