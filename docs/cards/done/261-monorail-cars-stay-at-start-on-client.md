# 261 FED_C3M1: monorail cars stay at their start point on the client

Found by card 242 run 5 (`logs/c242-c3m1-*`). Visible world gap for about 120 s, not a play blocker: the
state meets again by itself (round 14 on: 0 mismatches).

## Evidence

- Host 203095803: card098 `WorldOp L|Timeline 1` rows seq 24391-24393 and siblings for the 9
  `BP_SplineRiderMonorail_C` actors; client applies them 35-43 ms later (`result applied`).
- Client digest rounds 10-12 (203109183 to 203169176): 9 mismatches, class `BP_SplineRiderMonorail_C`,
  `location (one side attached?)`: host cars along the track (C_14 at -18850,-57400,1550), all client cars at
  one point (41850,-74750,1550). Round 13: 3 left. Round 14 (203228868): 0.
- Run 4 (stopped before the monorail part): 0 such rows.

So the timeline op arrives and reports applied, but the client cars do not move while the host's ride the
spline.

## Step 1 (static, no run)

Trace what moves a `BP_SplineRiderMonorail_C` on the host (timeline update, tick along the spline, attach to
a rider) and why the client's applied timeline does not: timeline play state after the op, a tick gate on
authority, or a start value the op does not carry. List every other spline rider class on the Trooper maps.

## Step 2 (fix by class)

Spline riders driven by a relayed timeline move on the client as on the host. One fix for the class.

Proof: c242 scenario monorail block: no `BP_SplineRiderMonorail_C` mismatch row in any digest round.

Files: decided in step 1. Budget: 1 run. After the playtest.

## Step 1 answers (static plus existing logs, senior, 2026-11-13)

- The title is wrong: the client cars are at the spline **end**, not the start. The `L` rows carry the placed
  positions (-19530, -42680 to -45930, 1530), and every client car sits at (41850, -74750, 1550) 23 s after the
  call while the host cars are still near (-14050, -62900). The mismatch ends by itself when the host cars arrive.
- `BP_SplineRiderMonorail_C` is a pure Blueprint (`/Game/Maps/Testing/LevelDesign/`, the only spline rider class in
  the game files; there is no native rider class). Its name map shows the mechanism: events `StartMoving` and
  `StopMoving`; variables `speed`, `initialOffset`, `SplinePath`; `GetSplineLength`, a divide, then
  `Timeline.SetPlayRate`; the timeline update lerps `Timeline_Alpha` along the spline and calls
  `K2_SetActorLocationAndRotation`.
- So the host's `StartMoving` sets the play rate from speed and spline length, then plays. The card 130 relay
  (`c98_host_timeline`, `mod/src/ubw/world_c98.cpp:571`) sends only the call (Play, PlayFromStart, Reverse,
  ReverseFromEnd, Stop). The client's timeline plays at its default rate and reaches the end in seconds.
- `UTimelineComponent::SetPlayRate` (0x42D9690, 0x10 bytes) has one caller, `execSetPlayRate` (0x37B3220), so every
  Blueprint rate change passes one point. The mod does not hook it.
- Same class, not hooked either: `SetNewTime` (0x42D9660), `SetLooping` (0x42D9640). No evidence yet that a Trooper
  map depends on them.

## Fix spec (step 2)

- New symbol `UTimelineComponent_SetPlayRate` in `tools/symbols.txt`, one hook-table row.
- Host detour in `mod/src/ubw/world_c98.cpp` next to the timeline detours: call the original, then relay under the
  same conditions as `c98_host_timeline` (placed owner, listen server, owner not replicated) as a new world kind
  (next free kind letter), name `<placed name>|<timeline name>`, value = the float's bits. Journalled per timeline,
  last value wins, a repeat of the journalled value is not sent. The row is sent before the Play row because the
  Blueprint calls it first; keep that order in the journal replay (rate rows before `L` rows of the same timeline).
- Client: find the timeline as `c98_client_timeline` does and call the original `SetPlayRate`. Log
  `card261: client world <kind> <name>|Timeline: play rate %.4f applied`; the host logs the same value on send.
- Card 185 late-join state (`c185_client_state`) needs no change if the rate row replays first; state that in the
  report with the replay order shown in code.
- Sweep, report only: list each Trooper map Blueprint whose name map holds `SetNewTime` or `SetLooping` on a placed,
  unreplicated actor's timeline. No code for them in this card.

Predicted rows (FED_C3M1 monorail block): host and client `card261` rows for the 9 cars with equal rates below 1.0;
no `BP_SplineRiderMonorail_C` mismatch row in any digest round after the `L` rows. Budget: 1 run.

## Run D (c230-c3m1-d2, build 3), senior verdict 2026-11-13

- Held: 9 client rows `card261: client world B BP_SplineRiderMonorail_C_{3,7..14}|Timeline: play rate 0.0096
  applied` (235284863 to 235284917), 9 matching host rows, 9 `world L` timeline rows applied after them.
- Held: no `BP_SplineRiderMonorail_C` mismatch in any digest round (the 5 mismatch rounds are `Pose:1` only).
- Also relayed by the same path: `BP_SatchelBuilding_Flakbug_C_0..2|TurnInPlace` 1.5,
  `BP_AssassinSpline_C_2|MoveAlongSPline` 0.0272, `BP_Radar_C_0|SpinDishTimeline` 0.05.
- Accepted. Closed.
