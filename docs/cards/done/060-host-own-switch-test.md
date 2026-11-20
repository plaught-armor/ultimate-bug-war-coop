# Card 060: Prove the host's own weapon switch after the client has switched

Status: sent 2026-11-04 (senior), before the playtest, at the user's request. The Skip section does not apply.

## The play build stays in place (hard rule)

The user plays build `a4d3f58896432b28` at 8 pm. `tools/playtest.sh` deploys `build/dwmapi.dll` into the game on every launch, so whatever is in `build/dwmapi.dll` at 8 pm is what the user plays.

- A read-only copy of the play build is at `build/dwmapi-play-a4d3f58896432b28.dll`. Do not touch it.
- Build and test your change as usual. When the card is done, or by **19:30** at the latest even if it is not done, copy that file back to both `build/dwmapi.dll` and the game's `dwmapi.dll`. Then check that both hash to `a4d3f58896432b28` and put the two hashes in the Report.
- The new flag is off by default. Even so, the new code does not ship tonight: it goes into the first build after the playtest.

Read the card 057 Report and Senior review, the card 059 Addendum (card059d), and `docs/PLAYTEST.md` (the host-switch watch item). Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why

Card 057 makes the client's switch work by setting the switch ability's CDO `NetExecutionPolicy` to ServerOnly (3) on the host, in `c57_set_policy`. The CDO is shared, so the host's own pawn switches under policy 3 from then on. No run has proven that this still works.

Card059d showed that keys alone cannot reach that state with a second weapon in hand. The brief end is tied to the client's join, and the level script then runs `GiveUnarmedWeapon` on the host's own pawn. That leaves the pawn with Unarmed only, and the policy goes `0 -> 3` about 1 s later. The two placed `BP_WeaponPickup_MoritaMk1_C` are 150 and 200 m away.

## Skip

If the user picked up a Morita with the host's own pawn during the playtest, pressed 2, and the rifle drew on both screens, this card is done without code. Record the time and the log lines in the Report.

## Do

1. **A test flag, `UBWCOOP_C60_HOST_OWN`** (read in `config.cpp` like `read_c57_solo`; off by default; logged at startup like the other flags). It needs `UBWCOOP_C50_TEST=1` and a client.
   - Reuse `c57_test_solo` (`mod/src/hooks.cpp`, after `c54_track_step`): it already moves the own pawn next to the nearest placed weapon pickup, then runs the card 050 Look, Use and Release stages on it.
   - Start it only after both of these: `c50_test_host` has reached `C50Stage::Done` (the client's case pickup), and the CDO policy is 3. Then wait `kC57SoloSettleMs` on foot. `g_c50t` is shared by both tests, so they run one after the other, never interleaved. In `c50_step`, the host branch picks the host test first and the solo stage afterwards. Do not run them side by side.
   - After Release, press **2** on the host's own pawn through the existing key path (autoinput or the card 057 driver approach). One press, no retries.
2. **One pair run, `card060`**: `tools/run_pair.sh`, muted, windowed 640x360 side by side, with the card 057t flags plus `UBWCOOP_C60_HOST_OWN=1`.
3. **Check:**
   - The host log shows `ASTPlayerCharacter::EquipWeapon(1)` (or the index the 2 key asks for) on the host's own pawn, with the policy at 3.
   - Shots of both screens show the host holding the Morita.
   - `python3 -I tools/logsum.py card060` gives ERROR/FATAL 0, and every WARN is explained.

## Fix only if it fails

If the switch does not run under policy 3 on the host's own pawn, the likely fix is per side, not per CDO: keep policy 3 only for the server copy's activation. For example, set the policy around `c57_receive_equip`'s `g_equip_weapon` call and restore it after. Report the audit first. A fix that touches the client's LocalOnly path needs the card 057 route rerun too.

## Done when

- One `card060` run passes the Check, with rc 0 on both games and no new dump.
- The PLAYTEST watch item is removed or rewritten to match the result.
- `docs/STATE.md` has a row for 060.

## Report (junior, 2026-11-04 19:16): not done, the Check fails; the play build is back in place

**Play build restored at 19:15:54**, before the 19:30 deadline. Both copies hash to `a4d3f58896432b28`:
- `build/dwmapi.dll`
- `game/StarshipTroopers_UBW/Binaries/Win64/dwmapi.dll`

The read-only copy was not touched. The last test DLL (`3df78560d6711ace`) is saved in the junior scratchpad only.

**Code** (in source, not in any shipped DLL):
- `config.cpp`/`.h`: `read_c60_host_own` (`UBWCOOP_C60_HOST_OWN`), off by default, logged at startup.
- `hooks.cpp`: `c60_test_host_own` takes over in `c50_step`'s host branch, never beside the host test, once both of these hold:
  - the host test's last step is done (`c54_step() == nullptr`);
  - the CDO policy byte reads 3.
- Start sequence:
  1. Reset `g_c50t` and `g_c54t.step`.
  2. Wait `kC60StartDelayMs` (60 s).
  3. Run `c57_test_solo` on the own pawn, with the pick limited to `MoritaMk1` (`g_c60_mk1_only`; without it, run 1 picked the case LaserMorita and got no Use target).
  4. 2 s after Done, one `Two` press and release through `autoinput::key_event`, then a showui shot.

**Runs** (STAGGER=5, muted, 640x360). Flags were 057t's without `UBWCOOP_SPAWN_BUG` (as in 059d; the bug route moves the host pawn too), plus `UBWCOOP_C60_HOST_OWN=1`. The client 2 key came from the scratchpad driver `drive60.sh`. All three runs: rc 0/0, no new dump, `logsum` ERROR/FATAL 0 and WARN 0 on both sides.

| run | DLL | own pawn moved to | pickup | 2 press result |
|---|---|---|---|---|
| card060 | 1b2cdd27 | case LaserMorita | none (own target null) | `ConstructWeaponSwitchRequest(1,0)`, `InternalTryActivateAbility(handle 8) = 0, ability null` |
| card060b | 42514d50 | MoritaMk1 (after `GiveUnarmedWeapon`) | `PickupWeapon` host-own | same |
| card060c | 3df78560 | MoritaMk1, 60 s after the start | `PickupWeapon` host-own | same |

(card060's client 2 was sent by hand: the driver had a syntax error.)

**Findings (audit, no fix tried):**
1. **The host-own switch does not run.** The 2 press reaches `IA_SwitchWeapon_Slot2`, `AbilityInputTagPressed`, and `ConstructWeaponSwitchRequest(1, 0, BP_PlayerCharacterMI_C)`. Then `InternalTryActivateAbility(handle 8)` returns 0, and no `EquipWeapon` or `ActivateAbility` follows. The accepted 057t has the same pair of lines (host log 2869-2870), so 057t did not prove the host-own switch from the log either.
2. **The host-own ability instance is not at policy 3.** After the CDO went `0 -> 3`, the host-own `CanActivateAbility` on handle 8 (at the pickup) still logs `NetExecutionPolicy 0`. That byte is read from the ability object passed in, so this is a per-actor instance made before the CDO write; the write does not reach it. So the card's premise (the host's own pawn switches under 3) does not hold for this instance. The failure in finding 1 is something else: the cause of `= 0` is not known yet.
3. **The pickup equips and then equips 0.** The pickup gives `EquipWeapon(1)`, then `EquipWeapon(0)` 3 ms later on the host-own pawn. The second call has caller `0x522ACE7`, the same caller as the brief-end Unarmed equip. Which weapon is held after that is not proven.
4. **Shots are no proof yet.** Both games write to the one prefix's `Saved/Screenshots/Windows`. The post-press shots (01781, 01790) show a briefing video (the officer and the box art), not gameplay, so it is unclear whose screen or which moment they show.

Next: per Fix, audit why `InternalTryActivateAbility(handle 8)` returns 0 on the host-own pawn. Compare against a solo run (no client, policy 0), where `c57_test_solo` and a 2 press can show a working baseline. The PLAYTEST watch item stays; a person pressing 2 after a Morita pickup at the playtest answers it.

Files: `logs/card060{,b,c}.run`, `.drive` (b, c), `-{host,client}.log`.

## Senior review (2026-11-04): accepted as a failed Check. Follow-up card after the playtest

I checked these myself:
- **Play build.** `build/dwmapi.dll`, the game's `dwmapi.dll` and the read-only copy all hash to `a4d3f58896432b28`.
- **`logs/card060c-host.log`.** The CDO goes `0 -> 3` at line 1084. The pickup on the host's own pawn runs `ConstructWeaponSwitchRequest(1, 0)`, then `(0, 0)` 2 ms later from caller `0x522ACE7`; both `CanActivate` calls on handle 8 log policy 0. The 2 press at line 2392 reaches `ConstructWeaponSwitchRequest(1, 0)` at line 2400, then `InternalTryActivateAbility(handle 8) = 0, ability null` at line 2401, with no `CanActivate` in between. Correction, made after re-reading `detour_internal_try_activate`: "ability null" is only the out parameter, which is empty on any failure, so it does not mean the spec has no ability. The refusal comes before `CanActivateAbility`. UE 5 has a check there that refuses an instanced-per-actor ability whose instance is still active, so the switch from the pickup may never have ended. Card 062 tests this.

The follow-up card, after the playtest, needs:
- a solo baseline (no client, policy never written);
- why `InternalTryActivateAbility(handle 8)` refuses the second activation before `CanActivateAbility`;
- why the pickup's own switch to 1 is undone 2 ms later by caller `0x522ACE7`.

Tonight: the host's own switch with 2 after a pickup may do nothing. The PLAYTEST watch item stays.
