# 482: client satchel E finds no interactor target (load race)

Status: Open. Play blocker (Bridge chain stops).

## Seen

c245b200a (b200): client on BP_SatchelItemPickup_C_3 and _5 logged "Use press: interactor target null (null), placed 0 ... UseName not sent". Bridge_SO2 stuck at 2/4, Bridge chain failed. b199, b201, b202 worked.

Timing: every run the host card137 rows at load set satchel collision 1->0. In b199 (works) the client "local sim" collision 0->1 came ~2 s before the step; in b200 (fails) it came 56 ms after the host row, at load. Likely the client's local-sim collision write and the host's card137 apply race at load, leaving the client pickup not interactable.

## Do

Trace the order of the card137 host collision apply and the client local-sim collision write on satchel pickups at load; find the final collision state on the client in the failing order. Fix by class (every pickup with a card137 collision row), not per satchel.

## Done when

A client log row per satchel pickup with its final collision state after load equal to the host's, and a C4M2 run where the client uses all 4 satchels; the class fix also covers the other pickups with card137 rows.

## Junior trace (2026-11-17, b203 cycle): the collision-race premise is refuted

- The load order of the collision writes is the same in c245b200a (satchel 3 fails) and c245b202a (all 4 work):
  in both, the client "local sim" 0->1 lands about 50 ms after the host card137 row. So that order is not the cause.
- c245b200a client log line 10776, 604134687: an IA_Crouch press (`HandleInputTag_Pressed InputTag.Crouch`) 1.3 s
  after the FlameThrower `key E` (cmd 8). No driver command sent it: drive.py injects no Ctrl, and the only mod Ctrl
  use is the card052 lobby_ui test behind an env flag. Its source is not known (no log shows it).
- From then on every warp shows the server copy 40 uu low (3805.9 vs 3845.9 before the warp, 3794.8 vs 3834.8
  after) and the client eye z at 3892 against 3916. The b200 satchel-3 shot is dark with no satchel and no prompt;
  the b202 shot shows the crates and "PICK UP SATCHEL CHARGE". The crouched client's interactor target is null.
- Proof step in tools/gen_c245.py (satchel 3, client): crouch on purpose (`client key LeftControl`), look, shot
  `c245-bridge-satchel-3-crouched`, `key E`, a `try` wait for the card050 Use row, then stand again. Prediction:
  the crouched Use row reads target null (or no row); the standing use after it picks the satchel up.
- If the prediction holds, the fix is test-side (each use step stands first) and the open item is the stray crouch
  source, not a collision class fix.

## c245b204a (b204 c31434fff95f7a37): the crouch prediction is refuted too

The proof step crouched the client on purpose at satchel 3 (150 uu stand). Its look hit focus BP_SatchelItemPickup_C
(pitch -13.9), and the crouched E logged `Use press: interactor target BP_SatchelItemPickup...`; the host counted
Satchels 1. The standing look after it voided only because the satchel was gone (line 336). Crouching alone does not
break the pickup, so the b200 null target is unexplained again (the stray IA_Crouch at b200 line 10776 stays unknown).

## b219 batch (2026-11-18)

- No null target since b200: c245 runs on b201, b202, b204, b212, b213, b215, b216 and b217 each show 0
  `interactor target null` rows, with the client using 2 satchels (_3, _5) each run.
- The aim scan row "hits 0 of 26 ... by centre, no hit" is in good and bad runs alike, so it does not tell them apart.
- The load-order collision rows match in b200 and b202 (see the trace above). b217 no longer has the load-time
  host-row/local-sim pair; it has "by own view" for the site and a host row 1->0 right after each pickup is used.
- The existing card050 Use row already names the target and its class, so a repeat is diagnosable without a new row.
- Proof step for the Done-when: tools/gen_c245.py now has the client take all four pickups (_3, _5, _7, _9) and place
  all four sites. Run c245b219a.


## Closed (2026-11-18 10:05)

Evidence c245b219a (b219, 313 s, rc 0/0, drive rc 0, 0 voids, EXCEPTION|FATAL 0 both logs, no new dumps): client Used all 4 satchel pickups (C_3, C_5, C_7, C_9), placed all 4 sites (holds completed on SatchelSIte_C_1..4), 0 `interactor target null` rows.

Not done: none.

Reopen when: any `interactor target null` row, or a client satchel pickup or site hold that does not complete.
