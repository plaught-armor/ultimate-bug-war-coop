# Card 172: The client's support flare press reaches the host

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: REPORTED 2026-11-11. The cause was a scenario fault (no charge was earned). Run c172-r1 proved the client's press and the strike on both sides. The client sees no flare in its own hand: card 169's open item, proven by log and screenshot.

Read: logs/c162-r1.drive (line 55 FAIL), logs/c162-r1-{host,client}.log after the marks, tools/scenarios/c162-run1.txt
lines 44-55, docs/cards/done/169-ability-spawn-visuals.md "Card 162 run items", cards 100 and 104 in docs/cards/done/.

## Why
In c162-r1 the client pressed Four then LeftMouseButton with a flare charge, and the host never logged
"card100: host ... TacSupport slot ... HandleTacticalSupportInput run" within 8 s. Cards 100 and 104 proved this path
on 2026-11-06. Either a later card broke it or the scenario did not give the client a flare.

## Do
1. From the two logs: did the client hold a flare and send `ChanOp::TacSupport`? Name the first missing step.
2. If the scenario is at fault, fix the scenario. If the mod is, fix it where the path broke.
3. Also settle card 169's open prediction: the client sees a flare in its own hand during the throw.

## Done when
- The cause, named from a log line. One run where the host logs the card100 line for the client's press and the
  strike shows on both sides.
- The usual Report items. Ends "Report complete."

## Senior finding (2026-11-11, from the c162-r1 logs)
The press did reach the host. logs/c162-r1-host.log line 3023: "card100: host t=292781291 TacSupport slot 0 from
BP_PlayerController_C: server copy BP_PlayerCharacterMI_C ... HandleTacticalSupportInput run, charge 12500 -> 12500",
and line 3024 shows the TacSupport op handled. The scenario's wait demanded "charge 20000 -> ".
The charge was 12500 before and after, so nothing was spent. Host lines 2832 and 2951 show TacCharge 2500 then 12500
sent to the client; no later rise. The client log shows BP_TacSuppPickup_BombingRun_Tutorial_C already hidden with
collision off at line 1679, before the scenario warped the client to it.
So step 1 narrows to: the client never took the bombing-run pickup (it was gone before the warp), so it had 12500
of the charge the wait expects. Decide whether the scenario must take the pickup another way, or whether the pickup
was hidden on the client wrongly ("by local sim", card 144 class).

## Static trace (junior, 2026-11-11)

**The client's charge comes from objective rewards, not from the tutorial pickup.**

- The host hides the tutorial pickup at map load, in every run. Both lines come from the same caller, before any
  scenario step:
  - c162-r1 host line 1363: "card098: host hide BP_TacSuppPickup_BombingRun_Tutorial_C ... at (12519, 13417, 247)
    placed 1 replicates 0 detail null caller exe+0x380231B".
  - The working run c104e, host line 1014: the same line.
  - No later show or collision-on line exists in either host log.
  - The client's hide at c162-r1 client line 1679 is the same hide, with the same caller (exe+0x380231B). It is not
    a client-local error.
  - So in c104e as well, "warp client BP_TacSuppPickup_BombingRun_Tutorial_C 10" gave no charge.
- In c104e the server copy's charge rose with the client's own objective steps, and was full before the pickup warp:
  - "ObjectiveComplete(Assault_SO1)" at 14116098 (the server copy warped into BP_PlayerPresence_C_2) was followed by
    "TacCharge to BP_PlayerController_C (copy BP_PlayerCharacterMI_C): 10000 0".
  - "ObjectiveComplete(ShootingRange_SO1)" at 14127084 (the client's E on the range Morita) was followed by
    "TacCharge ... 20000 1 -1 0 -1 0 -1 0" at 14127203.
- c162-run1.txt skipped both of those steps. The server copy got:
  - 2500 after the host's own resupply strike (host line 2832).
  - 12500 after the forced TacSupport_SO41 and SO5 completions (host line 2951, "12500 0").
  - That is short of full, and the ready flag stayed 0.
- So the press ran on an ability that was not ready: "HandleTacticalSupportInput run, charge 12500 -> 12500" (host
  line 3023). No throw and no strike followed.

**A bombing-run flare costs a full charge of 20000** (the "20000 1" state). In c104e the charge went from "20000 1" to
"0 0" after the throw: host line 2335, "TacCharge ... 0 0 -1 0 -1 0 -1 0" at 14153137.

**Cause.** The scenario never earned the client its charge, because the c104e reward steps were dropped. The card
100, 104 and 54 mod paths are not at fault.

**Fix.** A new scenario, tools/scenarios/c172-flare.txt, contains the flare block alone:
- The client earns the charge on the c104e route (Assault_SO1, then ShootingRange_SO1). Each step waits for its log
  line, and the scenario waits for "20000 1" on both sides.
- Then the client presses Four, a screenshot is taken while the flare is held, and the client fires.

**The press line cannot show B < A.** The card100 line reads the charge right around the synchronous
HandleTacticalSupportInput call. The ability spends the charge later, so even the working c104e press reads "charge
20000 -> 20000" (host line 2284). The spend shows in the host's next TacCharge line, "0 0".

## Predictions, c172-r1 (tools/scenarios/c172-flare.txt, DLL 8dfc02cb3577e884, no mod change)

1. Charge:
   - The host logs "ObjectiveComplete(Assault_SO1)", then "ObjectiveComplete(ShootingRange_SO1)".
   - The server copy's TacCharge lines go 10000, then "20000 1".
   - The client logs "card104: client ... TacCharge 20000 1".
2. Press:
   - The host logs "card100: ... HandleTacticalSupportInput run, charge 20000 -> 20000" (the spend is asynchronous).
   - It is followed by "server copy DoAction, key Flare_Throw", "flare BP_SupportFlare_C ActivateFlare", OnAbilityUsed
     and "TacCharge ... 0 0".
3. Strike on the client:
   - "card104: client ... copy of host N: ...BP_SupportFlare_C", plus the strike's BP_TacSupp_* copies.
   - Then "TacCharge 0 0".
4. Card 169's open prediction:
   - The host logs "held ... of the server copy destroyed".
   - The client logs no "card169: client ... spawned" line, and screenshot client-c172-flare-held shows no flare in
     the client's hand.
   - So the client does not see its own flare. This is the gap card 169 predicted: only the server copy runs the
     ability.
5. rc 0 and 0. ERROR/FATAL 0. Only the two baseline WARN classes from c162-r3.

## Report

### Files changed

- tools/scenarios/c172-flare.txt (new). It runs the flare block alone:
  - The client earns its charge on the c104e route: Assault_SO1, then ShootingRange_SO1.
  - Every state step waits for its log line.
  - Then the client presses Four, a screenshot is taken while the flare is held, and the client clicks LeftMouseButton.
- docs/cards/172-client-flare-press.md: the trace, the predictions and this report.
- No mod change. The DLL is unchanged.

### Summary

**Cause.** The client's server copy had 12500 of the 20000 a bombing run needs, and its ready flag was 0. c162-r1 host
line 3023: "HandleTacticalSupportInput run, charge 12500 -> 12500".

The scenario tried to charge the client from BP_TacSuppPickup_BombingRun_Tutorial_C, but the host itself hides that
pickup at map load in every run. c162-r1 host line 1363 (and c104e host line 1014) reads "card098: host hide
BP_TacSuppPickup_BombingRun_Tutorial_C ... caller exe+0x380231B". The client's hide at line 1679 has the same caller,
so it is not a local-sim fault.

In the run that worked, c104e, the charge came from the client's own objective rewards (Assault_SO1 and
ShootingRange_SO1). c162-run1.txt skipped those steps.

**Fix.** A scenario fix only, tools/scenarios/c172-flare.txt. No mod path was at fault.

**The senior's "B < A" cannot hold for the card100 line.** It reads the charge around a synchronous call, and the
ability spends the charge later. The working c104e press reads "charge 20000 -> 20000" (host line 2284), and so does
this run. The spend proof is the TacCharge line that follows, "20000 1" then "0 0", on both sides.

**The throw does not wait for LeftMouseButton.** The flare left the hand 857 ms after Four:
- "card104: host t=83165935 server copy DoAction, key Flare_Throw".
- "held BP_FlareVisuals_C ... destroyed after 857 ms".

The scenario's LeftMouseButton came after the throw and fired the client's Morita: ammo 60 to 57 in the screenshots,
and "server copy StartAnimationSequenceFixedTime(MoritaMk1_Fire" at 83166360.

**Not done:** nothing in scope. The held-flare visual on the client is the gap that card 169 predicted. This card
proved it and did not fix it.

### Verification

No build was needed. DLL 8dfc02cb3577e884 ("deployed: 8dfc02cb3577e884", "lane 1: dll 8dfc02cb3577e884, build
8dfc02cb3577e884").

Run c172-r1: "run c172-r1: ended by scenario after 65 s; host rc 0, client rc 0".

Charge earned:
- "card097: host t=83163194 subsystem ObjectiveComplete(Assault_SO1)"
- "card097: host t=83164284 subsystem ObjectiveComplete(ShootingRange_SO1)"
- "card104: host t=83164289 TacCharge to BP_PlayerController_C (copy BP_PlayerCharacterMI_C): 20000 1 -1 0 -1 0 -1 0"
- "card104: client t=83164299 TacCharge 20000 1 -1 0 -1 0 -1 0: own slots 20000 1 -1 0 -1 0 -1 0 -> 20000 1 -1 0 -1 0 -1 0"

The press:
- Client: "card100: client t=83165066 own HandleTacticalSupportInput(slot 0), local charge 20000: not run here (its
  target data cannot be net-serialized); sent to the host's server copy (1 sent)"
- Host: "card100: host t=83165078 TacSupport slot 0 from BP_PlayerController_C: server copy BP_PlayerCharacterMI_C,
  TacticalSupportComponent TacticalSupportComponent; HandleTacticalSupportInput run, charge 20000 -> 20000". A equals B,
  as predicted; see the summary.

The strike on the host:
- "server copy DoAction, key Flare_Throw" at 83165935.
- "card104: host t=83167390 flare BP_SupportFlare_C ActivateFlare".
- "OnAbilityUsed(TacticalSupport.AbilityInUse, slot 0) on BP_PlayerCharacterMI_C" at 83167391.
- "card104: host t=83167393 TacCharge to BP_PlayerController_C (copy BP_PlayerCharacterMI_C): 0 0 -1 0 -1 0 -1 0".
- "flare BP_SupportFlare_C ExpireFlare" at 83172403.

The strike on the client:
- "card104: client t=83167398 copy of host 123862: /Game/Blueprints/Combat/BP_SupportFlare.BP_SupportFlare_C at
  (11869, 15160, -142)"
- "copy of host 123952: /Game/Blueprints/Combat/BP_TacSupp_DangerZone.BP_TacSupp_DangerZone_C"
- "copy of host 123957: /Game/Blueprints/Combat/BP_TacSupp_Pod_Resupply.BP_TacSupp_Pod_Resupply_C"
- The pod contents (Morita, ammo, health and frag pickups) were copied at 83168397.
- "card104: client t=83167416 TacCharge 0 0 -1 0 -1 0 -1 0: own slots 20000 1 -1 0 -1 0 -1 0 -> 0 0 -1 0 -1 0 -1 0"
- Screenshot client-c172-r1-c172-after-strike.png shows the landed resupply pod with its INSPECT marker, and the
  objective "Inspect the resupply drop".

Flare in hand (card 169's open prediction, held):
- The host holds it on the server copy: "card169: host t=83165935 held BP_FlareVisuals_C of the server copy destroyed
  during first person montage AM_FlareThrow at 1.747 s".
- The client has no "card169: client ... spawned" line after the mark (scenario line 48, TRY-FAIL as predicted).
- Screenshot client-c172-r1-c172-flare-held.png, taken 1 s after Four and before the throw, shows the client's
  first-person Morita and no flare.
- So the client does not see its own flare in its hand.

drive.py:
- "scenario c172-flare.txt: drive rc 0", "scenario passed".
- "dead time: sleeps 3.0 s, timed-out try waits 1.0 s in 1 (line 48 1 s)".

Counts:
- host: 3467 lines, FATAL/ERROR 0, WARN 4.
- client: 3741 lines, FATAL/ERROR 0, WARN 1.
- CRASH 0. "new crash dumps: none".

WARN classes, compared with c162-r3:
- "WARN attach: DisableThreadLibraryCalls failed, err=126": baseline, both sides.
- "WARN config: UBWCOOP_GOD not "1"": absent, because this run had "config: UBWCOOP_GOD=1" on both sides. That is a
  difference in the run setup, not in the code.
- New on the host: "WARN card169: host t=83165935 held BP_FlareVisuals_C (BP_FlareVisuals_C) destroyed after 857 ms:
  rendered in the last 0.2 s 0, 39 cm from the camera, attached to FirstPersonMesh socket hand_r, root visible 1, actor
  hidden 0". This is card 169's own diagnostic for a held visual, and c162-r3 had no flare throw.
- New on the host: "WARN card157: host t=83167921 HitClaim shot 1 from pc BP_PlayerController_C (miss) found no round
  in 1500 ms: dropped (1 so far)", and the same for shot 2. These come from the scenario's LeftMouseButton firing the
  Morita after the throw. c162-r3 had no client shots in this shape.

### Out of scope

- Card 169 gap: the client sees no flare in its own hand during the throw. The ability and BP_FlareVisuals_C run only
  on the host's server copy.
- The server copy's charge rose to "13500 0" at 83168416, about 1 s after the strike. The source is unknown (the
  client's HUD shows "+3,500 Kill"). The same class of event as the unexplained 2500 in c162-r1.
- The bombing-run throw fires on Four alone. Any scenario that presses LeftMouseButton after Four fires the weapon
  instead, which is what c162-run1.txt line 54 does.

Report complete.
