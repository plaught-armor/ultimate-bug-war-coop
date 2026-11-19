# 316 Test driver: the aim check traces from the camera, the bullets leave from the muzzle (run 3 void from line 195)

Run 3 of the b17 C1M1 proof, senior trace 2026-11-14. Scenario fault, not a mod fault. Everything after scenario
line 195 failed in a chain, so run 3 proves nothing for cards 310 (map totals), 312, SO3, SO4 and the range.

## Cause

`client look actor ...TacSuppRangeTarget_C_3 0 bounds hit=...` passed: `aim: hit ..._C_3 ... at 11069 14840 159,
4008 uu` (camera at 11783 18783 216). The client then fired 53 rounds from (11780, 18689, 201); every one is
`HitClaim ... miss Landscape at (11690..11717, 18142..18345, 187..199)`, a rise in the ground 350 to 550 uu in front
of the muzzle. Targets `_C_1` and `_C_0`, shot from the same point at another yaw, died in 2 and 1 bursts.
`TacSupport_SO3` stayed open, the range never started, and each later step failed on the step before it.

## Fix

1. `look ... hit=`: a second trace from the equipped weapon's muzzle to the aim point; a different first hit is
   `VOID look: muzzle line blocked by <actor> at <uu>`.
2. `objective go` for a fire step: when the muzzle line is blocked the driver walks (real input) along the stand
   area until both traces hit the target, then looks again. No coordinates in the scenario.
3. The scenario stops at the first failed objective wait (`abort-on objective`), so a void run costs 2 minutes, not
   28.

## Prediction

Same route: all three tac support targets die, `TacSupport_SO3` completes, no `HitClaim ... miss Landscape` within
1000 uu of the shooter during the fire steps.

## Closed 2026-11-16 (senior)

Scenario driver card. The muzzle trace is in the driver (`tools/drive.py` `MUZZLE_BLOCKED`, `mod/src/ubw/testrun.cpp`
`look ... hit=`), and objective scenarios abort at the first failed objective wait. No mod behaviour is owed.
