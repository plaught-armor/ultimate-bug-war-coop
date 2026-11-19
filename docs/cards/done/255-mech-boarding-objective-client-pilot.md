# 255 "Board Mechsuit" does not complete when the client boards the mech

Found by card 241 run 4 (`logs/c241-c2m1-r4-*`). **Play blocker candidate** on FED_C2M1: with the client in
the only mech, `LOS_O05_SO01` stays open and `LOS_O05_SO02` to `SO06` are never added, so the mission
cannot end until the client leaves and the host boards.

## Evidence

- Host 173696827: `AddObjective(LOS_O05)`, `AddSubObjective(LOS_O05, LOS_O05_SO01)` (actors 0),
  `RallyPoint_Marker` on `BP_MechDropPlatform_C_3`.
- Host 173699793: card075 `mechs 0 -> 1`, one `BP_BaseMech_C` at (220070, -17127, 5534). The map has one pad;
  card 075 keeps one mech per pad.
- Host 173769529-173769532: card075 `MechEnter` from the client's `BP_PlayerController_C`; mech roles 3/2,
  controller and weak PC are the client's PC. The enter itself works.
- After that: no `Complete` row for `LOS_O05_SO01` on either side for the rest of the run.

Run 5 (host boards first): the host boarding completes SO01 and SO02 to SO04 follow on both sides.

## Step 1 (static, no run)

Find what completes `LOS_O05_SO01` in `FED_C2M1_Scripting_C`: which event (mech delegate, `OnBoardingComplete`
0x050F6950, a poll of player 0's pawn) and which player it reads. List the same trigger on every other
Trooper map with a mech objective (FED_C4M1 and later) so the fix is by class.

## Step 2 (fix)

A mech boarding by any player gives the script the credit the host's own boarding gives, through the
script's own trigger (fire its event for the client's enter in `c75_receive_enter`), not by completing the
objective row from the mod.

Proof: c241 scenario LOS_O05 block with the client boarding first: host `Complete` row for `LOS_O05_SO01`,
client replay, `LOS_O05_SO02` added on both sides.

Out of scope, own card after the user decides: a second mech for the second player (one pad gives one mech).
Related: card 228 item 2 (mech pilots in presence gates).

Files: decided in step 1 (card 075 code, `mod/src/ubw/objectives.cpp`). Budget: 1 run.

## Step 1 lead (senior, static, 2026-11-12)

The PDB has `ASTPlayerController::StartBoardingTheMech()` and a multicast delegate property
`ASTPlayerController::OnWantsToBoardMech` (`FOnWantsToBoardMech`). Likely shape: the level script binds
`OnWantsToBoardMech` on player 0's controller; the host runs the client's enter with the client's
controller, so that controller's delegate fires and the script, bound to the host's controller only, hears
nothing. To confirm: who calls `StartBoardingTheMech` (expected: the `OnInteraction` / `EnterMech` path),
which controller it is called on in `c75_receive_enter`, and whether the script binds controller 0 only.
Fix shape if confirmed: after a client's enter succeeds on the host, broadcast `OnWantsToBoardMech` on the
controller(s) the script is bound to.

Confirmed by the card 138 table (`docs/cards/done/138-client-beginplay-before-pawn.md` line 64):
`FED_C2M1_Scripting` binds `GetSTPlayerController().OnWantsToBoardMech`, the local (host) controller only.
That table lists the level-script binds of the other maps: use it for the class sweep.

## Report

Junior, 2026-11-12. DLL `3d3f70b0bb6ba6de` (`sha256sum | cut -c1-16`).

### Step 1 findings

- `ASTPlayerController::OnWantsToBoardMech` is a sparse delegate at `ASTPlayerController+0x740`;
  `FOnWantsToBoardMech` Broadcast at RVA `0x051B68B0`.
- `ASTPlayerController::StartBoardingTheMech` (RVA `0x051DE360`) is `add rcx,0x740; jmp Broadcast`.
  Its only caller is `EnterMech` (RVA `0x050E65F0`), called on the boarding pawn's controller before
  possess and the montage. `c75_receive_enter` reaches `EnterMech` synchronously through
  `g_mech_on_interaction`, so on the host the broadcast fires on the client's controller, which the
  level script does not bind.
- Other maps: the card 138 table lists `OnWantsToBoardMech` binds only in `FED_C2M1_Scripting`.
  `publics.txt` has no other `ASTPlayerController` sparse delegate and no other mech function on it.
  A full sweep of the FED_C4M1 and later level scripts was not done.

### Files changed

- `tools/symbols.txt`: `ASTPlayerController_StartBoardingTheMech`; `mod/src/offsets.generated.h` regenerated.
- `mod/src/ubw/hooks_ubw.cpp`: hook row for `detour_c255_start_boarding`.
- `mod/src/ubw/hooks_internal.h`: `g_orig_c255_start_boarding`, `detour_c255_start_boarding`.
- `mod/src/ubw/vehicles.cpp`: `detour_c255_start_boarding` (host: a non-local controller's broadcast
  is repeated on the local host controller, `g_world_first_pc`); `detour_c75_enter_mech` records the
  mech for the log line.
- `tools/scenarios/c241-c2m1.txt`: client boards the mech (host block removed), waits for the card255
  line; SO05 zone rounds replace the Ethan block (step cap 2000).

### Runs

- `c255-c2m1`: void. The scenario had 2848 steps, over the driver's 2000 cap; it stopped at parse after
  31 s, with no gameplay.
- `c255-c2m1-r2`: the gameplay run.

### Predictions (r2)

1. Pass. Host 180757793 `card075 host MechEnter from BP_PlayerController_C`, then 180757795
   `card255: host t=180757795 OnWantsToBoardMech mirrored to BP_PlayerController_C for
   BP_PlayerController_C, mech BP_BaseMech_C`. The line appears once.
2. Pass. Host 180757797 `ObjectiveComplete(LOS_O05_SO01)` and `card049: host sent
   CompleteSubObjective(LOS_O05, LOS_O05_SO01)`, with SO02 added in the same tick. The client adds SO02
   at 180757839 and replays the SO01 Complete at 180757840. SO02 completes at 180810396 (client
   180810442) and SO03 at 180822443, when SO04 is added.
3. Fail. SO04 never completes, so SO05, SO06 and LOS_O05 are never added. 18 of 23 on both sides.
4. Fail. WB_TrooperLevelEnd does not open, and FED_C3M1 is not reached (follows from 3).
5. Pass. No FATAL and no crash. The digest has one round with `mismatches 2 (classes Pose:2)` (card 251);
   the other rounds have 0. No new WARN classes compared with c241 r5 on either side.

### Not completed

- `LOS_O02_SO04`: never completes, the same as r5 (pre-existing).
- `LOS_O05_SO04`: new blocker, a sibling of this card's class. In r5 (host piloted) the self-destruct
  countdown ran at 177706521, LeaveMech at 177716529, the mech was destroyed at 177720434, and SO04
  completed at 177720655 through native CompleteObjective (library depth 1), with SO05 added in the same
  tick. In r2 (client piloted) the countdown ran at 180856797, LeaveMech at 180866800, and the destroy at
  180869808, but SO04 never completed. SO04's completion depends on the host's own controller or
  character being the pilot, through a link that was not found statically: there is no other
  controller sparse delegate, and the umap strings give no bytecode.
- `LOS_O05_SO05`, `LOS_O05_SO06`, `LOS_O05`: blocked by SO04.

### Frame windows (late r2)

Host: about 1000 frames per 10 s, mean 9.4 to 9.9 ms, 0 frames over 50 ms. Client: 295 to 400 frames
per 10 s, mean 25 to 34 ms, 22 to 36 frames over 50 ms.
