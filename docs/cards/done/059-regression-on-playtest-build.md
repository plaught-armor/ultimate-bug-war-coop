# Card 059: Rerun the card 057, 054 and 053 routes on the playtest build

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: sent 2026-11-04 (senior). Code freeze: no source change, no rebuild, no deploy.

Repo root: `/path/to/ultimate-bug-war-coop`. Play build: `a4d3f58896432b28` (card 058).

## Why

Card 058 changed code after cards 057, 054 and 053 were proven: the overkill owner check, the client minimap origin, the weapon count and the dead look. Their test drivers are in the same DLL behind `UBWCOOP_*` flags. Rerunning them on the deployed build checks that the late fixes broke nothing. It costs no build.

## Do

1. Before each run, confirm that the game's `dwmapi.dll` still hashes to `a4d3f58896432b28`. If `run_pair.sh` would build or deploy, stop it from doing so (an env switch, or launch without its deploy step) and say how in the report. Never deploy a different DLL.
2. Run each with STAGGER=5 and the flags its card used:
   - `card059a`: the card057t route (client switch, fire, kill; then the host presses 2).
   - `card059b`: the card054x route (satchel site and zipline relay).
   - `card059c`: the card053x3 route (checkpoint rebuild, then one completion with no duplicate).
3. For each run, check rc, crash dumps, and `logsum` ERROR/FATAL and WARN against that card's accepted run. Note any difference.
4. A fault is a report for the user (what, where, a proposed narrow fix). Do not edit code.

## Done when

All three runs end rc 0 with no new dump, and their card lines match the accepted runs. Or else each difference is reported.

## Report

Done 2026-11-04 (junior). All three routes end rc 0/0 with no new dump (dumps stay at 23), and their card lines match the accepted runs. One difference in 059a2 is new behaviour, not a regression in the routes: details under "Difference".

### No deploy

- `run_pair.sh` deploys only when `DEPLOY=1`. It was unset for every run, so the script launched the game DLL as found. The `.run` files have no `deployed:` line, which confirms it.
- Before each run, `sha256sum game/StarshipTroopers_UBW/Binaries/Win64/dwmapi.dll` printed `a4d3f58896432b28` (checked before 059a, 059a2, 059b and 059c).
- No source change, no build.

### Runs

| Run | Route (accepted run) | End | rc host/client | Dumps | ERROR/FATAL | WARN host / client | Accepted WARN |
|---|---|---|---|---|---|---|---|
| card059a | 057t, flags only | cap 301 s | 0/0 | none | 0/0 | 1 / 0 | 1 / 0 |
| card059a2 | 057t, flags + keys | cap 241 s | 0/0 | none | 0/0 | 1 / 1 | 1 / 0 |
| card059b | 054x | done 161 s | 0/0 | none | 0/0 | 2 / 0 | 2 / 0 |
| card059c | 053x3 | done 242 s | 0/0 | none | 0/0 | 0 / 0 | 0 / 0 |

**card059a is invalid.** It ran the card057t flags but not the card057t keys. The 057t route needs manual keys: the host presses Enter/Space past the brief, the client presses 2, and the host presses 2 after the kill. Without them the client never switched and fired Unarmed. It is kept only as the rc/dump/WARN check (all clean; host WARN is the known `card039: host probe table full (32)`).

**card059a2** reran it with a key driver (scratchpad `drive57.sh`): it waits for `card050: client test done`, sends the host Return/space twice and the client 2, then waits for `card057: host test moves its own pawn` and sends the host 2 twelve seconds later. Card lines against 057t:

- `card057: client switch ability CDO NetExecutionPolicy 0 -> 1` and `card057: host switch ability CDO NetExecutionPolicy 0 -> 3`: same.
- `card057: client ... switch to weapon 1: Equip sent`, then host `Equip 1 from BP_PlayerController_C: EquipWeapon on the server copy`: same.
- Client `Weapons received (#3, 195 chars)`: same.
- Host `card024: host relay press (Fire 1) ... weapon BP_Weapon_LaserMorita_C`: same. The bug died (HandleDeathEvent, then 7 `card039` SpawnGoreResponse lines).
- Host-own `ConstructWeaponSwitchRequest(1, 0, ...)` after the host pressed 2: same. The accepted run has one more host-own line, `ConstructWeaponSwitchRequest(0, 0, BP_Weapon_Unarmed_C)`. That was a later manual switch back in 057t, outside this route.
- Host WARN: the same `card039: host probe table full (32)`.

**card059b** against 054x: steps 0 and 1 end with the same two host WARNs (`no relayed Use came` for the panel switch and the satchel pickup). Relayed Use #1 lands on `SatchelSiteCAC_BP_SatchelPlacementSite_Flag_C_CAT`, #2 on `BP_ZiplinePoint_C` with `+0x100 already set; switch enabled -1 -> -1`. The client pawn after the zipline Use is at (17420, 19661, 1208), same as 054x. The run ended on the done pattern 2 s sooner, so it has 6 of the 12 zipline position lines and no host `step 3 ends` line. That is the stop point, not a change.

**card059c** against 053x3: every card045, card049 and card053 line matches. Restart 1 runs, the client sees one death (StartDeath 1, CompleteDeath 1, fallback 0), `SetObjectiveMapPostSave with 14 objectives in the host's order (was 15)`, `DoesObjectiveExistForID true for 14 of 14, order matches`, panel `ObjectiveListItems 14` before and 1.5 s after the event. The one completion `CompleteSubObjective(TacSupport_01, TacSupport_SO0)` replays as `objectives 14 -> 14`, with no duplicate. `card045: client test done (restarts 0, reconnects 1)`. The client log has 494 more lines than 053x3. They are a `rot:` diagnostic block (`input window opens on key NumLock`, 28 rows, 17 dumps). A NumLock key event reached the client window during the run; the test did not send it. It is not a fault.

### Difference (059a2 only): client death completed by the 8 s fallback

The accepted 057t ended at 165 s (the game was quit). 059a2 kept going to the 240 s cap, so it reached a state 057t never did:

- At about 594454872 a second warrior bug (alive at 425 uu at the end) downed the client. The host log shows `host-own, slot 0 -> Dead (all down: host death): Health -10.0`, so the team was all down and both screens showed the death screen.
- The host ran `CompleteDeath` on its own pawn at once.
- The client ran the real StartDeath (`TimeUntilDeathCompletion 0.00 -> 3.00`), but the game's own CompleteDeath never came. After 8 s the card044 fallback fired: `WARN card044: client no CompleteDeath 8000 ms after StartDeath (own pawn BP_PlayerCharacterMI_C, TimeUntilDeathCompletion 0.03): called here`. The fallback's CompleteDeath then built the death screen normally.

So nothing broke: rc 0, no dump, death screen on both sides. This is the first time the fallback has fired in any saved client log; card044's accepted run says it did not fire. Unconfirmed cause: the timer had counted down to 0.03 and stalled. The client window was in the background at the time (the driver had just focused the host window). A throttled background frame rate may stop that last tick. Card 053x3/059c also run the all-down death and the fallback did not fire there. Not proven, because nothing measured the client frame rate.

Proposed narrow follow-up (not done, code freeze): none needed for the playtest, since the fallback is the designed safety net. If wanted, a later card could log the client frame time in the 8 s window, to confirm the background-throttle cause before deciding whether the fallback delay should be shorter.

### Files

- `logs/card059a.run`, `card059a-{host,client}.log`: the invalid flags-only run.
- `logs/card059a2.run`, `.drive`, `card059a2-{host,client}.log`.
- `logs/card059b.run`, `card059b-{host,client}.log`.
- `logs/card059c.run`, `card059c-{host,client}.log`.

## Senior review (2026-11-04): accepted, one gap left open

I checked these myself:
- **Runs.** card059a2, card059b and card059c all ended rc 0/0 with no new dump, and the game's `dwmapi.dll` still hashes to `a4d3f58896432b28`.
- **WARNs.** They are the known ones: card 039's probe table, card 054's two test-step WARNs, and the card 044 fallback.
- **card 044 fallback.** It fired as designed and the death screen came. One fire with the client window in the background is no reason to change code under the freeze.

**The host's own switch under ServerOnly is still not proven.** In card059a2 the host-own `ActivateAbility` (handle 8, at 594390716 ms) ran before the host's CDO went 0 -> 3 (594404242 ms). After that the host-own pawn has no switch activation, only the level script's `GiveUnarmedWeapon`. So "matches 057t" is right, but 057t never proved it either.

**Run card059d**, only if it needs no code (keys through your key driver):
1. Start a pair. The client switches once, so the host's CDO goes to 3.
2. Then the host's own player picks up a weapon (the laser at the tactical case, or any rack weapon) and presses 2.
3. Pass: a host-own `ActivateAbility` logged after the `0 -> 3` line, and the HUD shows the weapon.

If the route needs a code change, skip it and leave the PLAYTEST watch item as it is.

## Addendum: card059d (host's own switch under ServerOnly), skipped: route needs a walk or code

Two keys-only tries, no code change. dwmapi.dll hashed `a4d3f58896432b28` before each; DEPLOY unset. Both ended rc 0/0, no new dump (23), ERROR/FATAL 0, WARN 0/0. Flags are 057t's without `UBWCOOP_SPAWN_BUG`. Driver: scratchpad `drive59d.sh`.

**Why the host has no second weapon when it presses 2.** The client's case pickup gives the host-own pawn the laser too (FED_C1M1 level script, `GiveWeaponByClass(BP_WeaponDefinition_LaserMorita_C)` plus host-own `ActivateAbility(handle 8)`). Then the brief ends and the level script runs `GiveUnarmedWeapon` on the host-own pawn, which leaves `weapons 1` (Unarmed only). In 059a2 the host's 2 press after that logged `InternalTryActivateAbility(handle 8) = 0, ability null`.

- `card059d` (STAGGER=5): I sent the host Return/space every 3 s from 25 s on, to end the brief before the client's case pickup. The keys did not end it. Case laser at 595410236, `GiveUnarmedWeapon` at 595436293, CDO `0 -> 3` at 595436943.
- `card059d2` (STAGGER=60, so the client joins later): the brief still ends about 31 s after the client joins. Client possessed at 595692984, case laser at 595701383, `GiveUnarmedWeapon` at 595724303, CDO `0 -> 3` at 595725156.

So the brief end is tied to the client's join, and the client's card050 case pickup always comes first. With keys only, the host-own pawn is always Unarmed-only once the policy is 3. In both runs the driver's host 2 press 3 s after `0 -> 3` logged no input line. It came about 1 s after the brief's re-possess, so the press was not even a valid test.

**To prove it, one of these is needed (not done):**
1. **A walk.** After the brief, the host walks to one of the two placed `BP_WeaponPickup_MoritaMk1_C` (logged 14929 and 20452 uu from the case), picks it up, and presses 2. A person can do this in the playtest. Scripted steering over 150 m is not reliable enough to count.
2. **Code (after the freeze).** A test flag that moves the host-own pawn next to a MoritaMk1 pickup after `GiveUnarmedWeapon`, the same way card050 moves the server copy.

The PLAYTEST watch item stays as it is.

Files: `logs/card059d{,2}.run`, `.drive`, `-{host,client}.log`.
