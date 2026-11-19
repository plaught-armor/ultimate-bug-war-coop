# Card 389: client falls forever at the FED_C5M1 and FED_C5M2 start

Status: accepted 2026-11-15 on b76 `90aa405f6b2dadbb` (runs `c389b75`, `c388b75`, `c388b76`; senior coded and proved). b76 also sends `ChanOp::WarpTo` from `c46_warp` when the target pawn has no sendable NetGUID.

## Defect

On a dropship-start map the host rides and the client's pawn spawns on foot at the PlayerStart
(`card307 ... kept PlayerStart: nobody Up`; the client's `EnterDropship` is skipped, card 015). On FED_C5M1 and
FED_C5M2 the PlayerStart (-33920, 41090, -437) has no ground. Run `c389b72`: slot 1 head z -327, -3317, -23019 ...
-163219 at the host landing (47.4 s after `hold off`), -583819 150 s in. No `card046` row and no Warp frame: the
chapter 5 kill height is never reached, so the kill floor never brings the client back. The client sees a white
plane and floating rocks (`client-c388b72-c388-client-c5m1.png`). Every build, play build 6 included.

Other FED maps have ground at the PlayerStart (C1M1, C3M1, C3M2, C4M1, C4M2 checked by head z; C2M1 one sample only).

## Fix

`c389_step(world, now)` in `mod/src/ubw/objectives.cpp`, called first in `c46_step` (`death.cpp`), host only, once a
second. An Up player pawn, not attached to a dropship, that reads Falling at every sample for 6000 ms is warped with
`c46_warp` (no damage) beside a grounded Up player who is not on a dropship. With no such player it logs one
`WARN card389 ... no grounded Up player` row per fall and warps at the first sample after one lands.

## Proof

1. `tools/scenarios/c389-c5m1-ride.txt` on b75: `card389 ... kill floor warp beside` and `card046 ... kill floor warp
   ... sent` within 2 s of `card017: host landing slot 0`; client shot shows terrain beside the host.
2. `tools/scenarios/c388-c3m1-to-c5m2.txt` on b75: passes; no `card389` warp row on maps with ground.

## Leftover

The client still falls through the void for the length of the ride (about 47 s). A hold at the PlayerStart, or a
client seat on the dropship, would remove it.
