# Card 062: Why the host's own weapon switch is refused, and the fix

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: sent 2026-11-04 (senior). The playtest is postponed, so the code freeze is lifted. The play build `a4d3f58896432b28` stays saved, read-only, at `build/dwmapi-play-a4d3f58896432b28.dll`.

Read the card 060 Report and Senior review (including its correction), and the card 057 Report. Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

When the host's own pawn picks up a weapon and presses 2, the switch is refused (`card060c`, host log lines 2400 and 2401):
- `ConstructWeaponSwitchRequest(1, 0)` runs;
- then `InternalTryActivateAbility(handle 8) = 0`, with no `CanActivateAbility` in between.

The accepted `057t` host log shows the same pair. So the host may not be able to draw any picked-up weapon in co-op. That is a play bug, not only a test gap.

## Static facts (senior, 2026-11-04)

- "ability null" in the `jump:` line is only the out parameter. It is empty on any failure (`detour_internal_try_activate`, about `hooks.cpp:3032`).
- The refusal comes before `CanActivateAbility`. The UE 5 `InternalTryActivateAbility` steps before that call are: spec lookup, then the ability pointer, then actor info, then the net-execution-policy check against role and local control, then the active-instance check. The active-instance check refuses an instanced-per-actor ability while `Spec->IsActive()` holds and `bRetriggerInstancedAbility` is false.
- The pickup itself runs the switch twice on the host's own pawn, 2 ms apart: `ConstructWeaponSwitchRequest(1, 0)`, then `(0, 0)` from caller `0x522ACE7` (card060c lines 2007 and 2013). If the first switch never ends, the spec stays active and every later activation is refused before `CanActivate`.
- `kSpecActiveCount = 0x28` (uint8) and `kSpecBytes = 0xF0` already exist (`hooks.cpp`, about line 5144). There is a spec-lookup path near `kSpecAbility` (line 4044).

## Do

1. **Audit, log only.** At the host's own 2 press, before the original `InternalTryActivateAbility` runs, log handle 8's spec: whether it was found, its `Ability` class, its `ActiveCount`, and the switch ability's instancing policy and NetExecutionPolicy on both the CDO and the instance. Also log every `EndAbility` and `CancelAbility` of `GA_WeaponSwitchAbility_C` on the host's own pawn, with times.
2. **Solo baseline, `card062s`.** Host only, no client, with `UBWCOOP_C57_SOLO=1 UBWCOOP_C50_TEST=1`. The policy is never written. The pawn picks up a Morita, then presses 2. Does the switch run? What is `ActiveCount`?
3. **Pair run, `card062p`.** The card 060 flags (`UBWCOOP_C60_HOST_OWN=1`), with the audit.
4. **Fix, by cause:**
   - **The spec stays active** (`ActiveCount > 0` with no `EndAbility` after the pickup's switches): find out what normally ends the switch. Most likely it is a montage or Equip event that never arrives on the host's own pawn when the policy is 3 or when the card 050 apply interferes. Fix it at that cause. Do not reset `ActiveCount` by hand.
   - **The policy check refuses it** (policy 3 read on a locally controlled authority pawn should pass, so this is less likely): apply the policy per activation, not per CDO, as card 060 suggests.
   - **Solo fails too:** the game refuses a second switch on its own. Report that, and find the game's own way to switch, such as `CycleEquip` or the slot input action, before changing anything.
5. Rerun the `057t` route (the client switches) after the fix. The client's LocalOnly path must not regress.

## Done when

- In `card062p`, the host's own 2 press after a pickup ends in `EquipWeapon(1)` on the host's own pawn, and the HUD shows the Morita. The shot must be the host's own: rename it right after it is taken, since both games share the Screenshots folder.
- The `057t` route still passes.
- `logsum` gives ERROR/FATAL 0 on all runs, every WARN is explained, and every game ends with rc 0.
- `docs/PLAYTEST.md` has the watch item updated, and `docs/STATE.md` has a row for 062.

## Report

**Cause: not a co-op bug, and not the active-instance check.** The host's own 2 press is refused by the game's own `USTWeaponSwitchAbility::ShouldAbilityRespondToEvent` (exe 0x05137FB0), which `InternalTryActivateAbility` calls before `CanActivateAbility`. That function returns false when the asked index holds no weapon, when it is already equipped, or when `CanSwapToWeapon` refuses. After the Morita pickup the inventory holds one weapon:
- the pickup gives the Morita (weapons 2) and runs `EquipWeapon(1)`;
- 1 ms later the level script's `GiveMorita` (`FED_C1M1_Scripting_C`, caller `0x522AC1D`) leaves weapons 1 and runs `EquipWeapon(0)`;
- so the Morita is index 0, already drawn. The 2 press asks for index 1 (`ConstructWeaponSwitchRequest(1, 0)`), which is empty.

Solo fails the same way, so this is the game's normal behaviour. `ActiveCount` was 0 before every attempt, and every switch instance ended through `EndAbility` (`bWasCancelled 0`, caller `exe+0x492E5DF`); no `CancelAbility` of the switch was logged. The pickup's own two switches activated on the host's own pawn with the CDO already at policy 3, so the host-own switch under ServerOnly works when there is a weapon to switch to.

The Done line "the 2 press ends in `EquipWeapon(1)`" cannot happen at this pickup. No fix was made. The game's own way to switch is the slot key or `CycleEquip`; with one weapon both have nothing to switch to.

**Audit added (log only, `UBWCOOP_C50_TEST`, host's own pawn, `card062:` lines).**
- Before `InternalTryActivateAbility` for a switch spec: spec found, ability class and pointer, `ActiveCount`, InstancingPolicy (+0xCF) and NetExecutionPolicy (+0x198) on the CDO and on the instance, inventory list. After: result and `ActiveCount`.
- New hooks `USTWeaponSwitchAbility::ShouldAbilityRespondToEvent` and `USTWeaponSwitchAbility::EndAbility` (0x0511CF00; unique code, not a folded stub), added to `tools/symbols.txt`, offsets regenerated. `CancelAbility` of the switch is logged from the existing detour.
- The c60 press is split out as `c60_press_two`; the solo branch now calls it after `c57_test_solo`, and the press log guards a null `g_c57_cdo` (prints 255).
- Function lengths: `c62_line` 3, `c62_is_switch` 3, `c62_before_try` 25, `c62_after_try` 6, `detour_c62_respond` 12, `detour_c62_end` 12, `c62_note_cancel` 11, `c60_press_two` 16.
- Build clean (`/W4 /WX`), DLL `0e001bc4b25e00c1`. `build/dwmapi-play-a4d3f58896432b28.dll` is untouched (hash checked).

**Runs (muted, windowed 640x360, STAGGER=5).**
- `card062s` (solo, `C57_SOLO=1 C50_TEST=1`): 212 s, host rc 0, ERROR/FATAL 0, WARN 1 (`autodropship: 1 PlayerController(s)`, normal for solo). 2 press: `ShouldAbilityRespondToEvent = 0`, weapons 1, `InternalTryActivateAbility(handle 8) = 0`, ActiveCount 0 before and after. The CDO policy is never written in solo (NetExecutionPolicy 0).
- `card062p` (pair, `C60_HOST_OWN=1`): 323 s (cap), rc 0/0, ERROR/FATAL 0, WARN 0/0. The same refusal with the CDO at policy 3 (instance still at 0, as card 060 saw). Shots in `logs/card062-shots/`: `p_client_near_pickup.png`, `p_client_hud_after_use.png`, `p_host_after_own_two.png`. The host shot shows a mission comm video, no HUD.
- `card062t` (the 057t route): 302 s (cap), rc 0/0, ERROR/FATAL 0. The client switch passes: client `NetExecutionPolicy 0 -> 1`, host `0 -> 3`, host `EquipWeapon(1)` on the server copy; the client's relays took the bug to 0. The host's 2 press is refused the same way (weapons 1). WARNs:
  - host `card039: host probe table full (32)`: test noise, as in card 057;
  - client `card044: no CompleteDeath 8007 ms after StartDeath`: the bug killed the client's pawn before it died; the card 044 fallback built the death screen, as in card 059.

**Open.** A host switch between two real weapons in co-op is still untested, because C1M1 gives the host no second weapon at this point. One way: `UBWCOOP_GRANT_WEAPON=<row>` on the host only. That also grants to the server copy (the client's pawn), which may disturb the card 050 client test. Not run: it is a scope change for the senior to decide.

## Senior review (2026-11-04): accepted. Not a co-op bug

I checked these myself, in `logs/card062s-host.log` (solo, the policy never written):
- Line 852: `GiveWeaponByClass(BP_WeaponDefinition_MoritaMk1_C)` from the level script (`0x522AC1D`) leaves weapons 1.
- Line 890: the 2 press gets `ShouldAbilityRespondToEvent = 0`, weapons 1.
- Line 891: `InternalTryActivateAbility(handle 8) = 0`, with ActiveCount 0 after.

The same refusal in solo means this is the game's own behaviour. The active-instance hypothesis from my card 060 correction is ruled out.

**Decision on the Open item:** no `UBWCOOP_GRANT_WEAPON` run. Granting to both the host's own pawn and the server copy would test a state the game never builds, and it would disturb the card 050 route. The host's switch between two real weapons will be tested when card 064's runner can walk to a point in the level where the game gives a second weapon. Until then it stays a PLAYTEST watch item.
