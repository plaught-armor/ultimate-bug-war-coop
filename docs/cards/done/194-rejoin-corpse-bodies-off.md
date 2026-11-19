# Card 194: A corpse on a rejoining client lies where the host has it

Status: queued 2026-11-12, from c177-r1c. Budget: three runs. Static first. Related: card 191 (bones off the
bodies on a rejoin copy), card 187 (done).

## Why

Client, c177-r1c, digest round 4 after the rejoin: "WARN card132: client round 4 pose guid 0x49a snap 1: body 18 at
(229620, 40254, 8646) is 785 uu from the host's (228842, 40251, 8539); 22 of 22 bodies over 100 uu, simulating 0"
and "1 corpses checked, 1 over 100 uu, 1 pending". A WarriorBug corpse replayed to the joining client rests 785 uu
from the host's.

## Do

1. Trace from the two logs, no run: guid 0x49a's host rows (snap numbers, frozen or moving at the replay, any
   later row) and the client's arrival, apply and freeze lines; name which row the client holds and why no later
   row corrected it ("1 pending").
2. Fix in the pose replay or the round correction, for the class.
3. One rejoin run; round after the join: 0 corpses over 100 uu.

## Done when

- Trace, fix, run lines. Ends with "Report complete."

## Step 1 answers (senior, `logs/c177-r1c-*` plus `pose_c132.cpp`, 2026-11-13)

- Host: guid 0x49a has one row, snap 1, frozen (`host pose replay row guid 0x49a snap 1 frozen`, 102483621). No
  later host row; none was owed (the corpse did not move on the host).
- Client: arrival at 102483589, posed to snap 1 at 102484440 (`0 uu from the host's root after, largest body
  error 0.0 uu`), freeze check at 102484463 `0.0 after the freeze call, 0.0 one tick later; simulating 0; held 1`.
  So the replay and the freeze are right.
- 643 ms later (102485106) the game's own ragdoll start ran on the copy: `card146: client blocked the game's
  ragdoll start` and `card140: client pose own SetAllBodiesSimulatePhysics(1) ... frozen copy held again`. Nothing
  is logged for the guid after that until round 4 (102509503): 22 of 22 bodies over 100 uu, body 18 at
  (229620, 40254, 8646), `simulating 0`, mesh component at (229344, 40522, 8487). All bodies moved together and
  lie around the mesh component, not around the host's root (229066, 40367, 8583).
- Mover, by the sequence: the rehold in `c132_pose_sim_change` (`pose_c132.cpp:2556`). It turns simulation off
  again and rewrites the hold bits, but does not put the bodies back on the stored snap and runs no check. Between
  the game's `SetAllBodiesSimulatePhysics(1)` and the rehold the bodies are the game's, started from the copy's
  own mesh pose. "1 pending" in the round is this row: the client holds the right snap number with wrong bodies,
  so no later row corrects it.
- Same sequence as card 203's frozen case (pose, card 146 block, bodies off, `simulating 0`). Card 191 is a
  different symptom (bodies exact, bones off) and stays its own card.

## Step 2 spec (build 4 candidate, `pose_c132.cpp` only)

- In the rehold branch: after the hold, mark the row so the stored snap is applied again on the next tick through
  the normal path (`pose_try_apply`, then `pose_freeze_step` with its check), for every held frozen copy. Count it
  (`st_frozen_reholds` exists) and log `card194: client re-posed guid ... snap N after the game's own start: body
  error E before, 0.0 after`.
- Prediction (rides the next run with a rejoin; no run of its own): each `frozen copy held again` row is followed
  within two ticks by a `card194` row; `E before` is over 100 uu on at least one (confirms the mover; if every
  `E before` is 0.0 the mover is elsewhere and the card reopens); the digest round after the join reads 0 corpses
  over 100 uu.

## Closed (2026-11-16)

Done-when met: `c423b163a` rounds after both rejoins read 3 to 24 corpses checked, 0 over 100 uu, 0 gave up, 0 Pose
mismatches. The rehold path of step 2 was not reached in any of the 59 client logs of builds b150 to b170 (0 `frozen
copy held again` rows): cards 146/152 now hold the game's ragdoll start before it touches a frozen copy, and card 140
(b171) blocks the direct call too. Watch: a `frozen copy held again` row with no `card194` row inside two ticks
reopens.
