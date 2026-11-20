# Card 512: use step-in gates on the eye distance, not the bounds face

## Finding

c504k1 (FED_C4M1 soak, DLL 62e2e778930a4dd6): Stadium_SO1_B, C and D (BP_PanelSwitch_WithoutScreen_C) never completed,
and 519 s were lost. Every try logged `use step-in none (face 2D 100 uu from bounds, stop 150, eye 3D 369 uu, reach 250)`
(host log rows 76798 on). The panel's bounds box (centre (-10794, -15933, -1174), extent (134, 134, 99)) put the face
100 uu from the stand, so the b257 gate `f2 <= kC284StepFaceUu || d3 <= kC284UseReachUu` skipped the step. The eye was
369 uu from the switch root, past the reach, so the press found nothing (client hit 241 uu, sweep miss, focus none).

## Do

`mod/src/ubw/objective_c284.cpp` `c284_step_in`:
- No step only when the eye is within kC284UseReachUu (250) of the target in 3D, or already within the stop distance in 2D.
- Otherwise move in 2D toward the target until the eye is kC284StepEyeUu (200) from it in 3D
  (2D stop = sqrt(200² − dz²), 0 when the height alone is past it). The move is swept, so the panel or a wall stops it first.
- The face distance stays in both rows. kC284StepFaceUu is removed.

Stadium numbers: stand 334 uu off the root in 2D, dz about −157, so the 2D stop is about 124 uu and the eye ends about 200 uu away.

## Done when

- In the c504k2 C4M1 soak, Stadium_SO1_B, C and D complete on try 1.
- Their step-in rows show `moved 1` and an eye 3D after the move of 250 uu or less.
- No other use step regresses: every use step that passed in c504k1 passes again.

## Result

Passed in c504k2 (lane 2, DLL b285 e97501bbae25a601, FED_C4M1 soak, run to level end). Stadium_SO1_A–D all
completed on try 1, 9 s for the four (c504k1: B–D VOID, 519.2 s). Rows, host cmd 143/148/153/158:
`use step-in moved 1, face 2D 100 -> 0 uu from bounds (eye 3D 369 -> 215 uu)` (214, 214, 215 on the others). No use
step regressed. 0 client digest mismatches (round 42), 0 FATAL/ERROR on both sides.
