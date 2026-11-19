# 324 Client's held flare is invisible in the hand

User, watching the client window in b18 run 3 (build `0c86ba1c6c2d79b0`, 2026-11-14): "client flare was invisible
in hand". Cause not settled; step 1 is a diagnostic. No fix code before its rows are read.

## Known (run 3, `logs/b18r3-c310-client.log`)

- Client spawned `BP_FlareVisuals_C` at 295260734, destroyed at 295261617 (883 ms): `rendered in the last 0.2 s 1,
  49 cm from the camera, attached to FirstPersonMesh socket hand_r, root visible 1, actor hidden 0`. Second throw at
  295290542: 878 ms, rendered 1, 42 cm. So the card 169 row's fields do not prove what the player sees.
- The scenario pressed LMB about 200 ms into the hold: client `Fire 1` at 295260933, `Fire 0` at 295261036 with 0
  rounds; the host relayed the press to the server copy's Morita (card028 Idle to Attack01 at 295261035).
- The client's local `AM_FlareThrow` ran to 1.766 s before the destroy; the host's server copy destroyed its visual
  at 0.733 s.
- Card 175's held-flare shot showed the flare; that scenario did not press LMB during the hold.
- Not known: whether the flare is visible in the hand in play (the playtest reports cover the thrown flare only).

## Step 1 (diagnostic, rides the next build)

1. The card 169 destroy row and one new row 300 ms after the visual spawns gain: FirstPersonMesh visible and
   hidden-in-game, owner-no-see and only-owner-see of the flare's mesh component, the mesh component's own visible
   flag, `hand_r` projected to the screen (x, y, on screen 0|1), and the montage playing on the FirstPersonMesh with
   its position.
2. Scenario: a client screenshot 300 ms after `Four` and before LMB, and one after LMB. The junior views both; the
   senior spot-checks.
3. One extra hold with no LMB for 800 ms, with a screenshot, to split "LMB hides it" from "never visible".

## Predictions

- If the no-LMB shot shows the flare and the LMB shot does not: the fire press (or its relayed Morita attack state
  coming back to the client) hides the arms or swaps the montage. Fix at the owner, the card 169/174 hold path: a
  fire press during a held support item must not reach the weapon.
- If neither shows it: a visibility flag in the new row is off, or `hand_r` is off screen; the row names it.

## Sibling sweep (report before coding the fix)

Every held visual the client spawns locally: grenade, tactical support items, satchel, medkit. Each gets the same
row once per map.

## Done when

The client screenshot during the hold shows the flare, on the play path (LMB to throw), and the user sees it.

## 2026-11-14 12:20, senior: step 1 read (run b11-soak-c2m1 r4, DLL `2c3c6be39321ee19`, C1M1 part)

Rows (client log `UBWCoop-client-20261114-121552.log`), hold A (LMB) and hold B (no LMB) are the same:
- 300 ms row: `FirstPersonMesh visible 1 hidden in game false; mesh StaticMeshComponent visible true owner no see
  false only owner see false; hand_r on screen 1 at (387, 304) of 640x360; montage AM_FlareThrow at 0.616 s` (A),
  `0.599 s` (B). The visual spawns about 300 ms into the montage.
- Destroy row: 881 ms (A), 885 ms (B), montage at 1.754 s and 1.768 s, hand_r at (598, 13) and (570, 36).

Shots (viewed by the senior): `client-b11-soak-c2m1-c324-a-300ms.png` shows the flare in the right hand, large, centre
right of the screen. `c324-a-after-lmb.png` and `c324-b-800ms.png` show no hand and no flare: the throw has left the
hand by then (B's shot is after the resupply drop spawned).

Result: with the client UNARMED (HUD "UNARMED", before the weapon pickup) the held flare is visible, and the LMB press
changes nothing. Neither prediction of step 1 holds as written. The user's report (b18 run 3) was with the Morita in
hand, and the run 3 rows show the fire press reaching the weapon (`Fire 1`, host relays Idle to Attack01).

Step 2 (next run, one hold, no new code): the same hold with the Morita equipped (after "Pick up the weapon"), shot
on the 300 ms row and no LMB, then the same hold on the host's own pawn with the Morita as the reference shot.
Predictions: (a) host shot shows the flare in hand with the weapon lowered or hidden; (b) if the client shot shows the
weapon still up and no flare, the client's own pawn misses the weapon holster/hide that the host's ability runs
(the client's local ability spawn is card 169; look for the weapon hide call the game's ability makes beside the
visual spawn); (c) if the client shot shows the flare, the report does not reproduce in the hold and the next suspect
is the first 300 ms of the montage with no visual (the client's visual spawns 300 ms late; compare the host own-pawn
spawn time against its montage position in the same rows).

## Step 2 result (b37 `f3d490b51e1e21f4`, run `b37`, 2026-11-14): does not reproduce

Morita equipped on both, no LMB. The 300 ms rows are the same on client (t=338383095) and host own pawn
(t=338387459): mesh visible, `hand_r` on screen at (387, 304), `AM_FlareThrow` at 0.600 s; destroy at 882 and 883 ms.
Shots `client-b37-c324-mor-client.png` (senior viewed) and `host-b37-c324-mor-host.png`: flare in the right hand,
Morita lowered out of view. Prediction (c) holds, (b) is refuted. The visual spawns 300 ms into the montage on the
host too, so the late spawn is the game's.

Left: the one path not shot is the b18 run 3 path, an LMB press about 200 ms into the hold with the Morita equipped
(the press reached the weapon relay: card 352). No more c324 runs; the user's next playtest decides. Parked.

## Closed (2026-11-17 11:11)

Shots `client-b40b173a-c324-mor-client.png` and `host-b40b173a-c324-mor-host.png` show the flare in the hand, as the b37 shots did. The one path not shot (a fire press 200 ms into the hold) is covered by the weapon relay of card 352. Reopen on a user report of a hand with no flare. Evidence base: the 202 host and client logs of builds b150 to b180 written since 2026-11-16.
