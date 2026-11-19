# Card 339: host sends an unfreeze snap for every SetAllBodiesSimulatePhysics(1), also when nothing changes

**Status:** open, carded 2026-11-14 from run 3 of `c338-c337` (b19 `40e935ba1137d921`, `logs/c338-c337-host.log`).
Efficiency (card 329 class): bytes and client apply churn. Not visible to players since card 334 holds settled finals.

**Rows (guid 0x238, a Warrior killed by code and at rest):** from t=324738942 the game's blueprint (caller
exe+0x3B05087) calls `SetAllBodiesSimulatePhysics(1)` on the corpse every 0.4 to 1.7 s. 16 `card137: host pose
unfreeze guid 0x238` rows, 15 of them with `body 0 simulating 1 -> 1`. Each one restarts the settle clock, sends an
unfreeze snap, and about 0.7 s later a settled resnap: snaps 4 to 30 at 445 bytes each, 14 client apply rows. The r6
FED_C2M1 soak had 44 such `1 -> 1` rows.

**Cause (static, `mod/src/ubw/pose_c132.cpp` `c132_pose_sim_change`, host branch, line 3191):** `simulate` alone sets
`start`, `still_ms`, `unfreeze_next` and takes a snapshot. The state before the call is not looked at.

**Fix:** the host branch takes the before state (the caller already logs `simulating a -> b`). When `simulate` is
true, body 0 was simulating before the call, and the row is not `c189_stopped` and not held, return before line 3191:
no clock restart, no snapshot, no row. A `0 -> 1` call and every `SetAllBodiesSimulatePhysics(0)` are unchanged.

**Presume-defect checks:** a `1 -> 1` call that wakes sleeping bodies and moves them is still sent: the host's
moved-body resnap (body 0 beyond `kPoseMoved`, line 813) and the stream cover it. A corpse the host stopped (card 189)
reads simulating 0 before, so it takes the unchanged path. A row with no snap sent yet (`state` not `Sent`) must
still get its first snapshot: skip only when the row has a snap out.

**Predictions:** same scenario block (card 334 block of `tools/scenarios/c338-c337-c1m1.txt`): at most 2 `host pose
unfreeze guid` rows for the corpse (the `0 -> 1` ones), a new total row or count for skipped `1 -> 1` calls of 10 or
more, client apply rows for the guid 4 or fewer, digest Pose mismatches 0, re-arrival rows as in run 3.

## Review of b20 (`17c4a1cd05bca494`, not run), 2026-11-14

The skip is in at `pose_c132.cpp` line 3195. Gap found by the junior and accepted: a `1 -> 1` call that wakes the
limbs while the pelvis moves under `kPoseMoved` (20 uu) leaves the row `Sent`, because the moved-body resnap tests
body 0 only. The skipped call was the only thing that restarted the clock for that case.

Change for b21: a bool on the pose row, set in `pose_step` where `moved` is computed when the state is `Sent` and
`moved >= kPoseStill` (2 uu), cleared in `pose_snapshot`. The skip requires it clear, so a corpse that moved since
its last snap takes the unchanged path. One row at a guid's first skip:
`card339: host guid 0x.. unchanged simulate call skipped (first)`.

Proof (b21, one run of `c338-c337-c1m1.txt`): the predictions above, with the skip count read from the
`sim_unchanged` total.

## Result (run 4 of c338-c337, b21 `ba009c82b58a8d90`)

- `card137: host pose unfreeze guid 0x220` rows: 3 (run 3, b19: 16). One `card339 ... skipped (first)` row at
  t=325765428; the junior counts about 25 further skipped calls.
- The prediction said at most 2. The third row is a real wake at the client re-arrival (snap 3 "UNSETTLED after
  0 ms"), not a `1 -> 1` repeat, so the skip is not at fault.
- 0 Pose mismatches in the digest rounds. Accepted 2026-11-14.
