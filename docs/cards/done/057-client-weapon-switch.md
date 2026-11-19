# Card 057: The client can draw and fire a weapon it picked up

Status: ready 2026-11-04 (senior). Playtest blocker: send before 054. FED_C1M1 starts every player UNARMED, so a client that cannot draw a picked-up weapon has nothing to shoot with.

Read `docs/STATE.md`, the card 050 Report and Senior review, and the card 055 Report (step 3, first row). Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

Card 055 run B: the relay gave the client's server copy the LaserMorita, and the client's list matched the host's (Unarmed + LaserMorita, equipped index 0). The client's HUD stayed UNARMED (ScreenShot01321). Neither the server copy nor the client equipped the new weapon, and nobody pressed a switch key, so it is not yet known whether this is a bug or the game's normal pickup behaviour.

## Static facts (senior, 2026-11-04)

- The game does not auto-equip on pickup through `USTWeaponUtilityLibrary::CheckToAutoSwitch` 0x05233020. That function is the out-of-ammo switch: it returns early when `HasAmmunitionAvailable` is true, and it also checks `USTGameUserSettings::IsAutoSwitchWeaponEnabled`.
- The switch input actions are `IA_SwitchWeapon_Next`, `_Previous`, `_Slot1`, `_Slot2`, `_Slot3` and `_Underslung` (`/Game/Blueprints/Input/Actions/UpToDate/`). Keys, from the name table of `IMC_MI_KBM` (the mobile-infantry keyboard and mouse context; senior extract): `MouseScrollUp` and `MouseScrollDown` (next and previous), `One`, `Two` and `Three` (slots 1 to 3). The name table shows the keys but not which key goes with which action. Try `Two` first, since the LaserMorita should be the second weapon after Unarmed, then the wheel.
- The switch path is ability-based:
  - `ASTPlayerCharacter::EquipWeapon(int32)` 0x051C57A0;
  - `ASTPlayerCharacter::ConstructWeaponSwitchRequest(int32, int32, const AActor*)` 0x051C28A0, which returns an `FGameplayEventData`;
  - `USTWeaponSwitchAbility::BeginWeaponSwitch` 0x05118D70;
  - `UADWeaponInventory::CycleEquip(bool)` 0x04A2B140.
  So a client key press most likely sends a gameplay event that activates a predicted ability, which the engine also runs on the server copy (GAS `ServerTryActivateAbility`). The server copy also holds the weapon (the relay gave it), so this may already work.

## Do

1. **Audit (one run, log only).**
   - Hook `EquipWeapon` and `BeginWeaponSwitch` on both machines with log-only detours. Log the pawn (host-own, server-copy, client-own), the index and the caller. The card 050 `EquipWeaponByIndex`/`ByClass` logs already exist.
   - **Solo, `card057s`:** the host picks up a weapon (`UBWCOOP_C50_TEST` moves the player next to a case in solo too, or walk there with `autoinput`). Report whether the game equips it by itself. Then press the switch key and take a HUD shot.
   - **Two players, `card057x`:** the client picks up the LaserMorita (`UBWCOOP_C50_TEST`, as in card 055 run B), then presses the switch key (scripted key injection). Report which of the hooks run on which pawn, and take HUD shots on both screens.
2. **Fix, only if the audit shows the client's switch does not reach the server copy or is undone.**
   - Likely shapes:
     - the card 050 list apply re-equips index 0 after the client's switch, because the server copy never switched;
     - or the ability does not activate on the server copy.
   - Fix at the narrowest point and say which shape it was:
     - for the first, apply the host's equipped index only when it changed on the host;
     - for the second, relay the switch index from client to host over the channel and call `EquipWeapon` on the server copy.
   - If neither shape fits, stop and report.
3. **Fire.** With the weapon drawn, the client fires at a bug. The card 024 hit lands on the host (host log) and the bug dies on both screens. Take shots on both screens.
4. **PLAYTEST.md:** add the switch key to the controls, and note whether a pickup equips by itself.

## Done when

- After a client pickup and one switch key press, the client's HUD shows the weapon on both machines (the host sees it on the client's sprite). The client's shots hit, and the card 050 list still says "matches the host".
- rc 0 on all runs, no new dump, the build is clean with `/W4 /WX`, and every function is 60 lines or fewer.
- Use `STAGGER=5 tools/run_pair.sh` for `card057x` (header of `run_pair.sh`; untested until now). Keep it if the client connects, and say which you used.

## Report

**Launch.** Every run used `STAGGER=5 tools/run_pair.sh`, and the client connected each time (s, x, y, z, w, v, u). Keep STAGGER=5.

**Audit (card057s solo, card057x co-op).**
- A pickup does not equip itself, on either machine. The one exception is the FED_C1M1 level script: it calls EquipWeapon(1) and then EquipWeapon(0) for the host's player at the tactical case.
- Side effect: when the client opens the case, the host's player gets a laser too.
- On the client, the switch key runs `GA_WeaponSwitchAbility` as LocalPredicted. It sends `ServerTryActivateAbilityWithEventData`, but the event's target data has no NetSerialize. The engine treats that as fatal and the client crashes.
- This matches neither of the two shapes the card predicted. The dump is from card057x: `UECC-Windows-242F50D9489A6434F01950BA1DC65A15_0000`, taken before the fix.

**Fix (hooks.cpp, card 057 section).**
- The mod sets the `NetExecutionPolicy` byte (+0x198) on the `GA_WeaponSwitchAbility` CDO (`c57_set_policy`):
  - client: LocalOnly (1), so the ability makes no server RPC;
  - host: ServerOnly (3).
- The client's switch request (`detour_c57_request`) sends Equip over the mod channel. The host (`c57_receive_equip`) then calls EquipWeapon on the server copy.
- The card 050 weapon-list sync is unchanged.

**Test-only changes (all gated by `UBWCOOP_C50_TEST`).**
- The card 037 host burst is skipped, so only client relays damage the bug.
- The card 023 test bug is spawned in front of the server copy. `c57_clear_yaw` tries 8 yaws and keeps the first with a clear PlayerProjectile trace.
- `c57_place_host` moves the host's pawn behind the server copy, facing the bug.

**Results.**
- card057y, card057z: one press of `2` on the client shows TRAINING RIFLE (BLUE) on the client HUD. The host equips the same weapon on the server copy. No crash.
- card057w: the bug spawned behind a wall, which led to the clear-yaw trace above.
- card057v and card057u: the client fires the LaserMorita and the host relays the presses ("card024: host relay press ... BP_Weapon_LaserMorita_C").
  - On the host, card037 shows the WarriorBug's health dropping 50 per hit, from 400 to -30. Then come HandleDeathEvent and card038 TryPlayDeathAnimation (u: 582875813).
  - On the client, card023 shows the same health steps and a card038 death edge for the bug (u: 582876016, Health 20 -> -30).
- Card 050 still logs "matches the host" after the switch (u: apply #3 and #4, Unarmed + LaserMorita on both machines).

**rc, dumps, build.**
- y, z, w, v and u: host rc 0, client rc 0, no new dump. The only dump is card057x's, from before the fix.
- The `/W4 /WX` build is clean.
- Every new or changed function is 60 lines or fewer: c23_spawn 31, c57_clear_yaw 20, c57_place_host 22, c57_set_policy 15, c57_receive_equip 11, detour_c57_request 16.

**Screenshots** (logs/card057-shots/).
- Switch:
  - y_client_before.png, y_client_after_two.png
  - v_client_after_two.png
  - u_client_before.png, u_client_after_two.png
  - In all of them, the HUD shows TRAINING RIFLE (BLUE), 60 rounds.
- Client fire:
  - u_client_kill1.png: muzzle flash, ammo 46.
  - u_client_kill2.png: ammo 37.
  - u_client_kill3.png, u_client_after.png
  - v_client_3.png, v_client_4.png: ammo 49.
- Host:
  - u_host_before.png, u_host_kill1..3.png, u_host_after.png
  - v_host_1..8.png, v_host_space2.png (the host in game after Space, in v)

**Open gaps (not proven by a screenshot).**
1. The host's screen never showed the client's sprite holding the rifle, or the bug dying. In u, the host window was still on the mission-brief video during every kill shot (u_host_kill1..3, u_host_after), although the game had already let the bug spawn. The kill on the host is proven by the log only.
2. On the client's screen, the bug is out of frame. The relayed hit is aimed by the server copy, which the test turns toward the bug, but the client's own camera still faces a wall.

A screenshot proof needs a run with:
- the host pressing Enter or Space until the brief video is gone, before the bug spawns;
- a client-side turn toward the bug.

I did not run that, to save time before the playtest.

## Senior review (2026-11-04)

Not accepted yet: one defect and one gap. Both go into one more run, which also gives the screenshot proof.

**Checked.**
- **Runs y, z, w, v and u:** 0 ERROR/FATAL on both sides, rc 0 on both, and "new crash dumps: none". The dump count is 21; the one new dump is card057x's, from before the fix.
- **Function length:** `fnlen` finds 1140 functions, 0 over 60 lines.
- **u log:** the host's CDO change `NetExecutionPolicy 0 -> 3` is at line 1134.
- **The policy choice is right.**
  - On the server, a LocalPredicted ability does not activate for a pawn that is not locally controlled (the server copy), so the host needs ServerOnly.
  - The client needs LocalOnly, so it makes no `ServerTryActivateAbilityWithEventData` RPC, which is the fatal one.

**Defect: `g_c57_policy_set` is set once per process (hooks.cpp ~21386).** The first side to set the policy fixes it for the rest of the process, so a game that changes role keeps the wrong one. The playtest changes roles through the panel: the user may host, quit, then join the other machine. Trace:
1. Process A hosts. The client switches, so the host sets ServerOnly on the CDO, and the flag is now true.
2. A quits and joins B as a client. A's first switch press calls `c57_set_policy(LocalOnly)`, which returns early on the flag.
3. The CDO stays ServerOnly. The client-side activation sends the server RPC with the event data, which is the same fatal NetSerialize crash as card057x.

The reverse case also fails: a process that joined first and hosts later keeps LocalOnly, and the server copy never switches.

Fix:
- Drop the once flag. Set the byte on every call: in `detour_c57_request` when `g_client_session` is set, and in `c57_receive_equip`. Log only when the value changes.
- Cache the CDO pointer once if you want to; the CDO lives for the whole process.
- ServerOnly is also fine in solo, because a standalone game has authority.

**Gap: the host's own switch under ServerOnly is untested.** In u, the only host-own `EquipWeapon` (line 959) ran while the policy was still 0.

**One run, `card057t`, with `STAGGER=5`.** It needs:
- the host presses Enter or Space until the brief is gone, before the bug spawns;
- the client turns toward the bug;
- the client presses `2` and kills the bug;
- after that (with the policy now ServerOnly), the host presses `2` on its own player, and its HUD shows the weapon;
- shots of both screens: the client's sprite holding the rifle on the host's screen, and the bug dying on both.

No role-swap run is needed. The fix is visible in the log, which must show the byte set again on each side's first switch.

## Report 2 (after the Senior review)

**Defect fixed.**
- `g_c57_policy_set` is gone. `c57_set_policy` now writes the byte on every call: from `detour_c57_request` when `g_client_session` is set, and from `c57_receive_equip` on the host.
- It logs only when the value changes. The CDO pointer is cached once (`g_c57_cdo`).
- In card057t, each side set the byte on its first switch:
  - client `NetExecutionPolicy 0 -> 1` (583214512);
  - host `0 -> 3` (583214515).

**Spawn check.**
- The u shots showed a wall between the client and the bug, while the PlayerProjectile trace called the line clear. So `c57_clear_yaw` now tries BlockAll first, then PlayerProjectile.
- `trace_profile` is split out of `c24_trace`, which now just calls it with PlayerProjectile.
- In t, BlockAll also called direction 8 of 8 clear. The client's and the game's screenshots still show a tall wall in that direction. So that wall does not block BlockAll line traces from the eye at +60 either, and the screenshot proof of the bug dying did not come out. The hits still land on the host.

**card057t (STAGGER=5, rc 0/0, no new dump).**
1. Client pressed `2`. Equip was sent, and the host ran EquipWeapon(1) on the server copy.
   - Shots: t_client_before.png, t_client_after_two.png (TRAINING RIFLE (BLUE)).
   - Card 050 says "matches the host" 4 times.
2. The host pressed Enter past the brief.
   - t_host_brief1.png shows the host in game.
3. The bug spawned. The host's pawn was placed 32 behind the server copy. The client's relays took the bug from 400 to 0, 50 per hit.
   - Host HandleDeathEvent at 583247282; client card038 death edge at 583247349.
   - t_host_kill1..3.png: the host's screen shows the client's sprite from behind, right in front of the camera. The rifle cannot be seen on the sprite from that angle.
   - t_client_kill1..3.png: rifle drawn, ammo 47; the bug is behind the wall.
4. The host pressed `2` on its own player under ServerOnly. `InternalTryActivateAbility(handle 8) = 0`, and the host HUD stays UNARMED (t_host_after_two.png). This test is inconclusive:
   - At on-foot (583227394), the FED_C1M1 level script calls `GiveUnarmedWeapon`. After that the host's player has 1 weapon (Unarmed), so slot 2 is empty.
   - Solo card057s logged the same `= 0` line under policy 0 (581343889).
   - Pressing `1` (the equipped slot) made only a ConstructWeaponSwitchRequest, with no activation (t_host_after_one.png).
   - To test the host's own switch, the host's player first needs a second weapon, which means the host takes the tactical support case. That would need another run.
   - Static view: GAS sends ServerOnly to the server only when the caller is not the authority; the host is the authority.

**Note.** On the client, the card038 death edges for about 24 troopers (Health 50 -> 0) come from the test's own `c23_clear_troopers` before the bug spawns. They are not a game event.

**Build.** `/W4 /WX` is clean. New and changed function lengths: trace_profile 22, c24_trace 3, c57_first_clear 13, c57_clear_yaw 20, c57_set_policy 14.

## Senior review 2 (2026-11-04): accepted

I checked these myself:
- **Logs.** card057t ran for 165 s and ended with host rc 0 and client rc 0. ERROR/FATAL was 0 on both sides, and there was no new dump. The only host WARN is card 039's probe table being full (32), which is test noise.
- **Fix.** `c57_set_policy` writes the CDO byte on every call and logs only when the value changes. The CDO pointer is cached for the whole process, which is safe because the CDO lives that long. In t, the client logged 0 -> 1 on its first switch and the host logged 0 -> 3 on the first Equip it received. A process that changes role now gets the right policy on its first switch.
- **Switch.** The client's ConstructWeaponSwitchRequest(1) sent Equip. The host ran EquipWeapon(1) on the server copy, and the copy's ability activated (handle 37). `t_client_after_two.png` shows TRAINING RIFLE (BLUE) and 60 rounds. The server call that crashed card057x did not happen.
- **Kill.** The kill is proven by both logs (400 -> 0, HandleDeathEvent on the host, and the card 039 probe of the dead bug), not by a screenshot. I accept that. The wall angle is test geometry, not a mod fault, and is not worth another run before the playtest.
- **Function length.** fnlen gives 1142 functions, 0 over 60 lines.

**The host's own switch under ServerOnly is untested but low risk.** The CanActivateAbility log lines read 0 because they read the ability instance, which was copied before the CDO changed. GAS decides the remote call from the spec's CDO (`Spec->Ability`). ServerOnly on an authority actor activates locally with no RPC, and the host's own pawn is authority on a listen server. The later host-own requests in t (indexes 1 and 0) ran without a fault. Watch item for the playtest: the host takes the tactical support case, then presses 2.
