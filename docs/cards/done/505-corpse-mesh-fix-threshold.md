# 505: Client corpses frozen 5 to 50 uu off their bodies are left drawn off the host's place

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: Opened 2026-11-19 by the senior, from a static trace of the c243b273a client log (b273).

## Finding

- The client logs `WARN card191: ... frozen with bone 0 X uu off body 0` 88 times in c243b273a (39 in c245b262a).
  The offsets have a median of 3.0 uu, a p90 of 19.1 uu and a max of 362.8 uu. 33 of the 88 are over 5 uu.
- Card 359's fix (`c359_mesh_to_body0`, pose_c132.cpp:2631) moves the mesh onto body 0 only when the offset is over
  `kC359FixUu = 50.0`. It fired 8 times, and every one logged "0.0 after". The card485 first-render rows for the
  same guids show bone 0 0.0 uu off body 0, so the corpses over 50 uu are drawn correctly. The stale-skeleton
  hypothesis (a corpse frozen off-screen keeps a bad pose) is ruled out.
- The 25 copies between 5 and 50 uu get no fix. The freeze sets bNoSkeletonUpdate, so the drawn corpse stays up to
  50 uu (half a meter) from where its bodies, and the host's corpse, are. Players see this: the corpse is not where the
  host's player sees it, which breaks [[full-world-sync]].

## Do

1. Lower `kC359FixUu` from 50.0 to 5.0 uu. The fix is the same TeleportPhysics move that already works over 50.
   It is bounded to once per freeze.
2. Only after the fix runs: keep the WARN when the offset after the fix is still over `kC191BoneTolerance`. Below that,
   log a plain card191 line with the offsets before and after. Today the WARN fires before the fix and reports a
   case that the fix then corrects.

## Done when

- One run that carries the next bug batch (no run of its own) logs card359 "mesh moved" rows for every freeze over
  5 uu, each with an offset after the fix under 1 uu.
- Client `WARN card191` count drops to the copies the fix could not move, with zero expected.

## Closed 2026-11-19 (senior, c502b280a on b280 c014bee01ec0e705)

The fix starts at kC191BoneTolerance (1 uu), not 5 uu: b278b left 84 frozen rows between 1 and 5 uu. c502b280a logged 148 "mesh moved" rows (46 client, 102 client2), with offsets from 1.0 to 325.3 uu before and 0.0 after every one. WARN card191: 0 on both clients. card137 "not posed after": 0.
