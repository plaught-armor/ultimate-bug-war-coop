# Card 603: a client hit on a warrior bug corpse whose ragdoll root sits far above the host location

## Finding

Two runs log `WARN card140: client hit BP_AI_WarriorBug_C ... is N uu from the copy's ragdoll root`, over kHitDrift 300:

- row: `[ 208228402 ms][T  540] WARN card140: client hit BP_AI_WarriorBug_C guid 0x49e: host location (-64533, 17186, 520) is 570 uu from the copy's ragdoll root (-64533, 17188, 1090), kHitDrift 300; the copy is at (-64592, 17255, 1312), local role 1; card223: frame age 31 ms`
- row: `[ 182802416 ms][T  608] WARN card140: client hit BP_AI_WarriorBug_C guid 0x18a: host location (-16820, -42247, 957) is 343 uu from the copy's ragdoll root (-16632, -42006, 1113), kHitDrift 300; the copy is at (-16818, -42239, 1022), local role 1; card223: frame age 45 ms`

In c601b455a the gap is all height: same x and y within 2 uu, the copy's ragdoll root at z 1090 against the host's 520.
The hit came during the 16-bug wave (tools/scenarios/c601-c3m1-b455.txt), after `both god all`, before the capture's
`clear 4000 kill`.

## Check (static first)

1. Which path writes a ragdoll root 570 uu above the host body: the card 189 fell check, a death impulse played on the
   copy only, or a corpse frozen in the air (card 474 held frames)?
2. Read the client card140 and card189 rows for guid 0x49e in logs/c601b455a-client.log before and after
   t=208228402.
3. If the copy's corpse is in a different place from the host's, that is a whole-world-sync gap (a visible corpse
   in another place) and gets a class fix. If only the hit check reads a stale root, fix the check.

## Static result (2026-11-22): a live-copy gap, fixed in b456

- **The corpse was not the cause.** The client's ragdoll started at its own mesh (z 1137) because the live copy's
  capsule stood at z 1312 while its ReplicatedMovement.Location was z 514 (`card524: client own-fall start ... guid
  0x49e: ... z -798.6 uu ... actor z 1312, replicated z 514`). Every other own-fall row in the run has actor z within
  11 uu of replicated z. The hit check read a real gap; the card 524 hand-over closed it 101 ms after the start.
- **Cause: the card 132 drift clamp skipped its set-backs.** The run's totals: `drifted copies set back to the host's
  z 1 times, not set back (encroaching) 114 times`. All 16 logged drift rows (guid 3c2) show a walking copy with zero
  velocity climbing about 8.5 uu a frame above the host's still location (location age 883 to 1133 ms), each one
  `skipped (encroaching)` with adjustment (0.0, -0.1, 6.5): the host's own place touches the landscape by 6.5 uu, so
  card 328's encroachment test refused it. The copy kept climbing until the host sent a new location. Guid 0x49e, in
  the same wave, reached 798 uu above the host's body before it died. The user sees live bugs float above the ground.
- **Fix (class, every client NPC copy, enemies.cpp `c132_drift_clamp`).** When the set-back place encroaches and the
  engine's adjustment is at most kC603AdjustUu 50 uu on each axis, the copy is set back to the host's place plus that
  adjustment (the engine's free place next to it). A bigger adjustment (card 328's +199.8 push-out) is still skipped.
  The drift rows say `set back at the adjustment`; the card038 totals add `(N of them at the encroachment
  adjustment)`.
- **Hit check.** Unchanged: it compares against the copy's own ragdoll root, and its WARN found this gap.

## b456 run predictions

1. The client card038 totals show `not set back (encroaching)` at or under 5 per map, and most set-backs `at the
   encroachment adjustment`.
2. No `card132: client t=... drifted` row repeats for one guid more than 3 frames in a row (a set-back holds).
3. Every `card524: client own-fall start` row has |actor z - replicated z| under 200 uu.
4. No `WARN card140: client hit` row with a gap over 300 uu.

## b456 results (c602b456b, FED_C3M2, 2026-11-22)

1. **Not readable in this run.** The card038 totals print at map end (`map totals`, sprint.cpp) or after the last kill shot,
   and the run ended on the map. The logged drift rows stand in: all 16 (the log cap) say `set back at the
   adjustment`, none `skipped (encroaching)`. The skip path that caused the 114 in c601 is gone.
2. **Not met: a set-back does not hold.** Guid 0x600 (BP_AI_WarriorBug_C) has 14 of the 16 rows, 350 ms apart, from
   210590942 to 210595809 ms. The host's place stays still (-52454, 2675, 2029, location age 416 to 4749 ms), and each
   row sets the copy back to z 2036 (adjustment 7.4). Within the next 350 ms it climbs 208 uu again. The user sees a
   live warrior bob up 200 uu about three times a second.
3. **Met.** 31 own-fall start rows; the largest |actor z - replicated z| is 94 uu.
4. **Met.** 0 `WARN card140: client hit` rows.

### Cause lead for point 2 (static read of the card 328 trace rows)

The card 328 trace armed on guid 0x600 after the set-back logs 30 frames. Each frame the copy rises 9.5-9.6 uu in
Walking, while `LineDist -7.4` stays the same and `FloorDist` grows by the same 9.5 each frame (203.7 to 480.9).
The rise is exactly UCharacterMovementComponent::AdjustFloorHeight on a stale line-trace floor:

- MoveDist = (MIN_FLOOR_DIST 1.9 + MAX_FLOOR_DIST 2.4) / 2 - LineDist = 2.15 - (-7.4) = 9.55 uu a frame;
- after the move it adds MoveDist to `CurrentFloor.FloorDist`, but it does not refresh `LineDist`, so the next frame
  repeats the same move.

So `CurrentFloor` is never recomputed on this copy after the set-back. The engine's normal walking path would run FindFloor
each frame and settle at about 2.15 uu. The set-back moves the capsule but keeps the old CurrentFloor, which was taken
at the encroaching host place (7.4 uu into the floor).

**Fix to check statically before b457:** after a card 132 or card 603 set-back, refresh the copy's floor. Either call
FindFloor for the new place and store it in CurrentFloor, or clear CurrentFloor so the next simulated tick computes it.
Also find why the simulated tick on this copy skips FindFloor; bSimGravityDisabled is the first suspect. Prediction for
the fix run: no guid repeats a drift row after its set-back, and the card 328 trace frames show z flat within 3 uu.

c602b456c (same map, second run): 2 drift rows (guids 0x6b2 and 0x54c, one row each), so the set-backs held there;
0 `WARN card140: client hit` rows. The guid 0x600 re-climb in c602b456b stays the open item above.

## b457 trace and fix (2026-11-22, static)

**Why the copy's tick skips the floor sweep.** SimulateMovement does call FindFloor every frame, but with
`bCanUseCachedLocation = Velocity.IsZero()`. On these copies the velocity is zero: the host NPC stands still, its
replicated ground velocity is zero, and SimulateMovement zeroes Velocity for a simulated proxy on the ground with zero
replicated velocity (the drift rows show vel 0). bSimGravityDisabled zeroes it the same way, so it is not needed for
the skip. With a cached location allowed and a blocking movement base, FindFloor returns CurrentFloor unchanged and
sweeps nothing. The exe confirms the test order at 0x143a2b35b..0x143a2b37d: bAlwaysCheckFloor (+0x556 bit 2), the
cached flag, bForceNextFloorCheck (+0x2e9 bit 0x10), bJustTeleported (+0x554 bit 0x20). The cached floor stays
walkable, so the card 328 own-fall path (the card 132 Falling hold) never runs. AdjustFloorHeight then takes the
line-trace LineDist each frame, and the same 9.55 uu move repeats. The same loop explains the first climb before any
set-back (about 570 uu/s at 60 fps, against the "about 500 uu/s with zero velocity" in the c601b455a rows).

**Fix (enemies.cpp, `c603_floor_check` and `c603_force_floor`):**

- After the movement step, a walking client copy gets `bForceNextFloorCheck` when it has zero velocity and its
  CurrentFloor is a walkable line-trace floor with LineDist outside [1.9, 2.4]. Its next SimulateMovement then
  recomputes the floor at its real place (UpdateFloorFromAdjustment), and AdjustFloorHeight settles it.
- Every card 132 or card 603 set-back sets the bit as well, because CurrentFloor was taken at the old place.
- Row `card603: client ... at rest on a stale line-trace floor (LineDist ..., FloorDist ..., z ...): next floor check
  forced` (8 per map). The count goes into the card038 map totals as "card 603 stale floors forced N times".

**Predictions (b457 C4M1 run):**

1. No guid has more than one drift row (repeat drift rows 0), and every set-back holds.
2. Card 328 trace frames after a set-back show z flat within 3 uu, and LineDist changes after frame 1 (the floor is
   recomputed).
3. Any `card603: ... next floor check forced` row is followed by no climb of that guid (no later drift row for it).
4. 0 new WARN classes; mismatches 0.

## b458 results (c604b458a, FED_C4M1 to Stadium_O1, 2026-11-22)

The card 603 code is the same in b457, b458 and b459; b458 changed only the card 604 Stop detour.

1. **Met.** 0 `card132: client ... drifted` rows in the whole run. The card038 map totals: `drifted copies set back to
   the host's z 0 times ..., not set back (encroaching) 0 times`.
2. **Not readable.** The card 328 trace never armed (it arms after a set-back, and there were none). The card603 rows
   stand in: every logged row for one guid has the same z (guid 4b2 at z -319.8 over six frames; guid a86 at z 426.1,
   then 424.6), so no copy climbed.
3. **Met.** No guid with a card603 row has a later drift row.
4. **Met.** Mismatches 0 in 28 rounds. No new WARN class from this card.

**New find (carded here, not chased): the force repeats every frame on some copies.** `card 603 stale floors forced 102
times`. The rows for guids 4b2 (LineDist 2.7, FloorDist -0.8) and a86 (LineDist 46.3, FloorDist -1.0) repeat about
every 33 ms with the same numbers, so the recomputed floor gives the same LineDist again and the test fires again. Here
LineDist outside [1.9, 2.4] is the engine's own steady result, not a stale floor. The cost is one floor sweep per frame
for each such copy; z stays flat. A tighter test would force only once per CurrentFloor: keep the last forced
(guid, LineDist, FloorDist) and skip when it is unchanged. Size it from the per-frame count first (tight-detours rule).

## b460 fix and predictions (c604b460a, FED_C4M1 to Stadium_O1)

Fix (b460): `c603_repeat` keeps the last forced (cmc, LineDist, FloorDist) in a 64-slot hashed table. A floor check
that finds the same values on the same copy is counted (`repeats not forced`) and not forced again. The set-back path
is unchanged.

Predictions:
1. 0 `card132: client ... drifted` rows; card038 `drifted copies set back to the host's z 0 times`.
2. `card 603 stale floors forced` falls from 180 (c604b459a) to under 40, and `repeats not forced` is above 0.
3. Mismatches 0; no new WARN class from this card.

## b460 results (c604b460a, C4M1, lane 2, dll 157b37d09efb2004)

1. Holds. 0 `card132: client ... drifted` rows. card038 totals: `drifted copies set back to the host's z 0 times`,
   with local Falling held at the host's mode 19989 times.
2. Partly holds. Floors forced fell from 180 to 52, with `108 repeats not forced`. That is above 0, but 52 misses the
   bar of under 40. Cause, from the logged rows: one copy (guid a90, BP_AI_WarriorBug_C at z 373.1) alternates between
   LineDist 11.6 and 11.7 with FloorDist 1.9. `c603_repeat` compares the floats exactly, so each 0.1 uu flip counts as
   a new floor state and forces again. Follow-up fix for the next batch: treat a state as a repeat when LineDist and
   FloorDist are each within 0.5 uu of the last forced state. It is a few lines in `c603_repeat`
   (mod/src/ubw/enemies.cpp), with no new state.
3. Holds. 0 mismatches. No new WARN class from this card.

## b461 (dwmapi 559ed50803f35572): repeat tolerance

`c603_repeat` now treats a floor state within `kC603RepeatUu` = 0.5 uu of the last forced one (LineDist and
FloorDist each) as a repeat. Exact compare made each 0.1 uu flip of a copy at rest force again (c604b460a: 52 forced).

Predictions (c604b461a, C4M1):
1. Forced under 40 for the run, with 0 drift rows. Then the card closes.
2. 0 mismatches; no new WARN class from this card.

Result (c604b461a, C4M1, 508 s, rc 0 both sides): 12 forced rows on the client (highest per map 8), 0 drift rows.
Prediction 1 met (bar under 40). 0 mismatches, no new WARN class. Card closed.
