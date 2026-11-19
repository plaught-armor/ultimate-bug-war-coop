# Card 175: Client miss claims find no round on the host after a flare throw

Status: queued 2026-11-11, from run c172-r1 (new WARN class). Budget: static first, then one run.

Read: mod/src/ubw/claim_c157.cpp around line 455; logs/c172-r1-host.log from t=83165935 to t=83168100;
docs/cards/done/165-*.md "Report".

## Why
c172-r1 host: "WARN card157: host t=83167921 HitClaim shot 1 from pc BP_PlayerController_C (miss) found no round in
1500 ms: dropped (1 so far)" and the same for shot 2. The client fired its Morita right after the flare throw
(ammo 60 to 57). Either the server copy did not fire those rounds (it was still in the throw), or the claim matching
missed them.

## Senior trace (2026-11-11, from logs/c172-r1-{host,client}.log)
The matching is right; the server copy fired fewer rounds than the client. Counts: client 3 `client-own
FireProjectile`, 3 claims sent; host 1 `server-copy FireProjectile`. Earlier runs are 1 to 1 (c165e 60 and 60,
c168-r3 13 and 13, c162-r3 13 and 13). Two causes, both verified from the log, mechanism of the first open:
- **A, double round (open).** Client t=83166332 and t=83166335: two FireProjectile calls 3 ms apart for one
  activation of handle 144, two `DoAction, key MoritaMk1_Fire` (count 1, count 2), playlog "shot c seq=1 ... k=2",
  and two claims both numbered shot 1. The host copy shows one DoAction and one round. This is the first shot after
  the flare throw; the weapons are re-created around it (card098 destroy BP_Weapon_Unarmed_C and
  BP_Weapon_MoritaMk1_C, client t=83166345, host t=83166291).
- **B, press length (verified).** Client press t=83166321 to release sent t=83166439 (118 ms) fired a round at
  +92 ms (t=83166413). The host applied Fire 1 at t=83166360 and Fire 0 at t=83166451 (91 ms), one round at
  t=83166371, so the copy's next round was not due before the release.
A dropped miss claim does no harm, but the same shortfall on an NPC claim loses the client's damage.

## Do
1. Static: why the client fires two rounds in one shot here (cause A). Check whether the fire ability or its
   DoAction notify is bound twice after the tactical-support re-equip, and whether any shot after any re-equip
   (weapon swap, pickup) does the same. Fix at the cause; the client must fire what the single-player game fires.
2. Cause B: make the copy fire as many rounds per press as the client did. Proposal to check against the code that
   relays Fire 0: the release carries the client's round count for that press, and the host holds the copy's release
   until the copy has fired that many or a bounded wait ends. No second relay; extend the Fire op.
3. Keep the WARN for NPC claims. A miss claim with no round stays logged, capped, without WARN only if 1 and 2
   leave a case that is harmless by design; say which.

## Done when
- Cause named from quoted log lines; the fix or the reclassification; a rerun of c172-flare.txt shows 0 of this WARN
  or the copy firing in step with the client. Ends "Report complete."

- Carried from card 173: in the same run the client empties most of a magazine; the host logs "card160: host ... skipped server copy 2D sound MSS_FOL_Low_Ammo_Click" and no "audio MSS_FOL_Low_Ammo_Click ... 2D" line, and the client still plays its own click.

## Files
mod/src/ubw/claim_c157.cpp, the file that relays Fire 1 and Fire 0 (card 024/026; name it in the trace first),
tools/scenarios/c172-flare.txt or one new scenario.

## Trace (junior, 2026-11-11)
No code written: card 174 met its stop condition in the same pair, and cause A's mechanism is not yet named from code.

**Cause A: not the flare throw. A shot while the removed weapon is still alive.**
- The tutorial pickup gives the Morita twice. Client c172-r1:
  - t=83164251 "card098: client spawn BP_Weapon_MoritaMk1_C ... caller exe+0x4A2D432", then "GiveWeaponByClass(...,
    slot -1, equip 0) = 1 ... weapons 2; callers 0x522AC1D < 0x51D3276 < 0x51D0695" (the pickup).
  - The level script then removes both weapons and gives a new Morita: "RemoveWeaponFromOwnerInventory
    BP_Weapon_Unarmed_C (1, pickup 0) = 1" (t=83164253), "RemoveWeaponFromOwnerInventory BP_Weapon_MoritaMk1_C" (t=83164254),
    "card098: client spawn BP_Weapon_MoritaMk1_C" (t=83164254) and "GiveWeaponByClass ... weapons 1; callers 0x522AC1D <
    0x50B88A5" (exe+0x50B88A5 is USTWeaponUtilityLibrary::execAddWeaponByTag+0xf5, a Blueprint call).
  - The host does the same: "card098: host spawn BP_Weapon_MoritaMk1_C" at t=83164279 and t=83164282.
- RemoveWeaponFromOwnerInventory (exe+0x524b0b0) cancels the weapon's abilities (CancelAllAbilities at exe+0x524b1c4)
  and hides it. It does not destroy it. The removed weapons are destroyed about 2 s later on both sides:
  - client "card098: client destroy BP_Weapon_Unarmed_C" and "... BP_Weapon_MoritaMk1_C" at t=83166345, 2091 ms after
    the remove;
  - host at t=83166291 and t=83166292, 2010 ms after.
- The first shot fell inside that window on the client only:
  - client press t=83166321, "DoAction ... count 1" t=83166324, "DoAction ... count 2" t=83166335, two FireProjectile
    calls (tasks 8A744420 and 70DAFCA0), "shot c seq=1 ... k=2", then the old weapons destroyed at t=83166345;
  - the host's old copies were already gone (t=83166292) when the relayed Fire 1 was applied (t=83166360). It fired
    once.
- Every other run with this pickup fired its first shot after the old Morita was gone, and none has a k=2 shot (grep
  over all logs/*-client.log: "shot c seq=... k=2" appears in c172-r1 only). First shot minus old-weapon destroy, in
  ms: c157b +466, c157c +465, c157d +471, c159c +556, c163-r2m +38077, c163-r3m +467, c165a..e +466 to +568, c168-r1..r3 +465 to +38061, c169-r2 +16247, c173-r1 +53,
  c162-r3 +45122, c166b +2941, c166c +3861. c172-r1 is the only negative one: -10.
- The flare throw is not involved. In c172-r1 the scenario fired 2.08 s after the pickup, because of where the throw
  sits in the scenario.
- "Whether any other re-equip does it": only a re-equip that removes a weapon and leaves it alive. A plain switch
  (ConstructWeaponSwitchRequest, GA_WeaponSwitchAbility) does not remove a weapon. The removing paths are this level
  script's AddWeaponByTag and anything else that calls RemoveWeaponFromOwnerInventory (pickups that replace a weapon in
  a full slot are a candidate, not checked).
- Open, not named from code: which object fires the second round. The press activates one ability ("card021: client
  local activation handle 144: activated ... BP_WeaponFireAbility_MoritaMk1_C"), and the animator logs one start
  ("starts 5"). The anim_c104.cpp DoAction line prints neither the task, the ability nor the animator, so the log
  cannot tell whether the second DoAction comes from the removed weapon's still-bound task or from a second binding
  on the new one. One run with the task, ability and animator pointers in that line (anim_c104.cpp:96-106, a file not
  in this card's list) would name it. It may also be the single-player game's own behaviour in that 2 s window. No
  host-own shot in any log falls inside the window, so that is untested.

**Cause B: the relay code, and whether the proposal fits.**
- Files: the client sends Fire 1 and Fire 0 from c26_send_fire (mod/src/ubw/hooks_ubw.cpp:511-523, through
  c26_own_fire). The host receives them in c26_host_fire (mod/src/ubw/chan_ops.cpp:10-29), which calls c24_relay
  (mod/src/ubw/fire.cpp:178-203). A Fire 0 calls c24_release (fire.cpp:162-175) at once, which calls the original
  AbilityInputTagReleased on the pressed weapon's ASC.
- Wire: the Fire op is one u32. Its layout (hooks_internal.h:1427-1438) is bit 0 pressed, bits 1-7 the kind, bits 8-11
  the card 160 ADS bits. A Fire 0 of kFirePrimary has bits 8-31 free, so the client's round count for the press fits
  in bits 12-19 with no new field and no wire format change.
- Host hold: C24Relay (hooks_internal.h:1440-1444) gets the wanted count, the copy's fired count and a deadline. The
  release then waits in g_c24.relays until fired >= wanted or the deadline passes, checked from the per-frame step
  (fire.cpp:342 calls c26_ping_step each frame, a place for the check). c24_release_pawn (Logout) still releases at
  once.
- Counting rounds: both counts need detour_fire_projectile (mod/src/ubw/attributes.cpp:909). Today it keeps only one
  count per side (g_c44_fires), not per pawn. That file is not in this card's list. The client side also needs a
  per-press count of client-own FireProjectile calls.
- Cause A must be fixed first. A k=2 shot would make the client ask for one more round than the single-player game
  fires.
- Fit: yes, as one change to the existing Fire op and the existing relay table. It touches hooks_ubw.cpp (the send),
  chan_ops.cpp (the receive), fire.cpp (the hold), hooks_internal.h (C24Relay) and attributes.cpp (the counts). That
  is five files, against the card's "the file that relays Fire 1 and Fire 0".

## Cause A verdict (senior, 2026-11-11)
Game behaviour, covered by B. The double round is the removed weapon's still-live ability in the 2 s window before
the level script's removed weapons are destroyed; single-player has the same window. Not fixed, no diagnostic,
anim_c104.cpp not touched. With B the copy fires the client's count, so a k=2 shot is matched by the hold.

## Predictions (run c175-r1, with card 174)
1. Client Fire 0 of a primary press carries its rounds: "card026: client t=… client sent Fire <arg>" where
   (arg >> 12) & 0xFF is that press's client-own FireProjectile count (Fire 0 values 4096 for one round, and so on).
2. Host: a release whose copy is short logs "card175: host t=… release of BP_PlayerCharacterMI_C… held: the copy
   fired a of the client's b rounds", then "card175: host t=… held release of … done: the copy fired b of the
   client's b rounds (bound 300 ms)". A press whose copy is already at the count releases with no card175 line.
3. Whole run: the host's server-copy FireProjectile count equals the client's client-own FireProjectile count, and the
   host has 0 "found no round" WARN lines. If the first shot is k=2, its hold shows the copy reaching 2.
4. No "ended short by the bound" line, or each one has its cause quoted.
5. Carried from 173: the host logs "card160: host … skipped server copy 2D sound MSS_FOL_Low_Ammo_Click" and has no
   "audio MSS_FOL_Low_Ammo_Click … 2D" line. The client's own click is not shown in the log (no client line for its own
   2D sounds; c160_copy_effect returns false on the client).

## Report

### Files changed

- mod/src/ubw/hooks_internal.h:
  - kFireRoundsShift and kFireRoundsMax: bits 8-14 after run 1, with two static_asserts against ue::chan::kChanArgMax.
  - C24Relay gains fired, wanted and release_ms. C24State gains press_rounds and hold_lines.
  - New c24_relay signature, and the c175_copy_round declaration.
- mod/src/ubw/hooks_ubw.cpp: c175_press_rounds. c26_send_fire ORs in the client primary rounds.
- mod/src/ubw/chan_ops.cpp: c26_host_fire passes the rounds only for a soldier copy's primary Fire 0.
- mod/src/ubw/attributes.cpp: detour_fire_projectile counts server-copy rounds (c175_copy_round).
- mod/src/ubw/fire.cpp:
  - New: kC175HoldMs 300, kC175HoldLines 40, c175_release_or_hold, c175_hold_due, c175_end_hold and c175_copy_round.
  - c24_relay: hold on release; a press during a hold ends the hold first.
  - c24_host_guard ends due holds.
- tools/scenarios/c175-flare-fire.txt (new, shared with card 174).

### What was done

Cause B fix:
- The client's primary Fire 0 carries the number of rounds it fired since the press.
- The host holds the server copy's release until the copy has fired that many rounds, or until 300 ms have passed.
- The hold is for soldier copies only. Mech and turret copies release at once.
- Cause A is game behaviour, covered by B (senior verdict). anim_c104.cpp is not touched.

**Run 1 found a wire limit.** A fixed channel argument is sent `% kChanArgMax` (0x7FFF; ue/chan.cpp chan_frame), so the rounds in bits 12-19 wrapped:
- Client: "card026: client t=86477096 client sent Fire 188416 (AbilityInputTagReleased)" (46 rounds << 12).
- Host: "card026: host t=86477112 host received Fire 24581 (BP_PlayerController_C)". 188416 % 32767 = 24581, which the host read as kind 2 on, so it logged "card024: host relay press (ADS 1)".
- The primary release never reached the relay, so the copy kept firing: 60 server-copy rounds against the client's 46.

The fix moves the rounds to bits 8-14 (at most 127). A primary edge carries no card 160 ADS bits there. Two static_asserts keep the primary and ADS args below kChanArgMax. Run 2 used the fixed build.

### Predictions (run c175-r2)

1. Holds.
   - Client: "card026: client t=86831505 client sent Fire 11776 (AbilityInputTagReleased)". 11776 = 46 << 8.
   - Host: "card026: host t=86831521 host received Fire 11776 (BP_PlayerController_C)".
   - The earlier tap during the throw sent "Fire 0" (0 rounds; the client fired none in that 120 ms), and the host released it at once.
2. Not shown. The copy was never short at a release, so no hold was taken and no card175 line was logged.
   - The host's 46th server-copy FireProjectile was at 86831505, before the release arrived at 86831521, so `r.fired >= rounds` released at once.
   - No k=2 shot occurred.
3. Holds. 46 server-copy FireProjectile lines on the host and 46 client-own FireProjectile lines on the client.
   - Host first and last rounds: 86827016 and 86831505. Client: 86826996 and 86831488.
   - The client screenshot after the burst shows the magazine at 14 (60 - 46).
   - There are 0 "found no round" lines on the host, and 38 HitClaim lines.
4. Holds. There is no "ended short by the bound" line.
5. Carried from 173: not shown.
   - Drive line 53 timed out: "try host wait log card160: ... MSS_FOL_Low_Ammo_Click since-mark timeout=5 -> TRY-FAIL timeout after 5 s".
   - The host still logs "playlog: host t=86827018 audio MSS_FOL_Low_Ammo_Click from BP_Weapon_MoritaMk1_C at (0, 0, 0) 17791 uu, 2D, attenuation null", 3 times in the burst.
   - Logged cause: the click does not go through UGameplayStatics::SpawnSound2D. The host has 9 "card099 ... SpawnSound2D" lines, all STConversationSubsystem dialogue, and none for the click. Card099 lines were under their cap (233 of 300).
   - The playlog line comes from detour_pl_play_internal, which is UAudioComponent::PlayInternal on a component whose Owner is BP_Weapon_MoritaMk1_C. So the card 173 gate in detour_c99_spawn_sound_2d is not on the click's path.
   - The client's own click is not loggable.

### Skipped originals

None. The server copy's ASC release (AbilityInputTagReleased) is delayed up to 300 ms, not skipped:
- c24_host_guard releases a held relay when it is due.
- A new press ends the hold first.

### Build and runs (shared by cards 174 and 175)

- Build 1: 0 warnings, DLL 7c138d9577687993. Run c175-r1: "run c175-r1: ended by scenario after 75 s; host rc 0, client rc 0".
- Build 2 (the card 175 wire fix below): 0 warnings, DLL e6c02da7a4d52da1 ("deployed: e6c02da7a4d52da1", "lane 1: dll e6c02da7a4d52da1, build e6c02da7a4d52da1, port 7777").
- Run c175-r2: "run c175-r2: ended by scenario after 79 s; host rc 0, client rc 0".
  - host: 3876 lines, FATAL/ERROR 0, WARN 1. client: 4228 lines, FATAL/ERROR 0, WARN 1. "new crash dumps: none".
  - "scenario passed". "dead time: sleeps 2.0 s, timed-out try waits 5.1 s in 1 (line 53 5 s)". Line 53 is the carried card 173 click wait (see card 175).
- The second run was allowed by a cause quoted from the r1 log: the client logged "client sent Fire 188416" and the host logged "host received Fire 24581" for the same frame (card 175).
- WARN classes, compared with c173-r1: both runs have only "WARN attach: DisableThreadLibraryCalls failed, err=126" on each side, which is the c173-r1 baseline. There is no new class. The c172 host WARN "card169: host ... held BP_FlareVisuals_C ... destroyed" is gone.
- Screenshots: prefix/drive_c/users/steamuser/AppData/Local/StarshipTroopers_UBW/Saved/Screenshots/Windows/client-c175-r2-c175-flare-held.png, client-c175-r2-c175-after-strike.png and client-c175-r2-c175-after-burst.png (the r1 set is beside them).

### Out of scope

- Card 173 class: the host's server copy plays MSS_FOL_Low_Ammo_Click through UAudioComponent::PlayInternal on a weapon-owned component, not SpawnSound2D. The card 160 2D gate does not cover that path.
Report complete.

## Senior review (2026-11-11)
Accepted with one part unproven. Diff touches only the allowed files; two static_asserts guard the Fire argument against
the channel modulus. Run c175-r2: "client sent Fire 11776" and "host received Fire 11776"; 46 server-copy and 46
client-own FireProjectile; 0 "found no round"; baseline WARN only. A stale comment in hooks_ubw.cpp ("bits 12-19") was corrected to "bits 8-14" at review; the rebuilt DLL is `876c59da61692a92` (comment-only change, not re-run).
Not shown: the hold engaging (the copy was never short; the 120 ms press after the throw fired 0 rounds on both sides),
and card 173's low-ammo click skip (the click plays through UAudioComponent::PlayInternal, not SpawnSound2D). Both are
carried to card 176. Cause A (replaced weapon alive about 2 s after a swap) is game behaviour and stays unfixed.
