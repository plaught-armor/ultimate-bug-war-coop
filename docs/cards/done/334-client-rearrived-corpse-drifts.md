# Card 334: client corpse that re-arrives with a settled pose keeps simulating and drifts

**Status:** accepted 2026-11-14 (fixed in lane 2 b19 `40e935ba1137d921`, run 3 of `c338-c337`: guid 0x238 re-arrived held, 0 Pose mismatches; see CARDLOG). Was: open, queued after the playtest (cosmetic: corpse pose after the client leaves and re-enters relevance;
not a play blocker). Found by the Pose digest gate, batch 3 run 2 (`e9aa849bb15eacf0`,
`logs/c310-proof-c1m1-*.log`). Not a batch 3 regression: the path is card 140/146/152 code.

**Repro (run 2 scenario, removed for run 3):** host spawns a Warrior by code 1500 uu from the client, kills it
(`clear 3000 kill`), the client is warped about 17000 uu away (copy torn down) and the copy re-arrives.

**Rows (guid 0x1e2):**
- First copy: stream plus settled snap 2 applied, 0.0 uu error.
- 308890844 client copy torn down (relevance loss). 308896015 new copy arrives with alive defaults; host-dead state
  applied; snap 2 (settled) applied at arrival, "posed by the snap path, not held", `simulating 1; held 0`; card152
  bone check WARN 92.4 uu.
- 308898367 `card146: client blocked the game's ragdoll start on host-posed guid 0x1e2 (snap 2, simulating)`, then
  `card140: client pose own SetAllBodiesSimulatePhysics(1) ... (row applied)` (caller exe+0x3B05087) passes: neither
  `regrav` (`grav_off` false) nor `rehold` (`held` false).
- Digest round 1: body 9 176 uu off, 5 of 22 bodies over 100 uu. Round 7: body 6 282 uu off, 11 of 22. Mesh rel
  (-0, 0, -174) at arrival, (-2, 6, -281) later.
- Siblings in the same run: 0x1ec, 0x1f6 card152 bone-check WARNs (0x1f6 four times, frozen final 108 uu).

**Cause (static, `mod/src/ubw/pose_c132.cpp`):** a settled, not frozen host final is applied by the snap path and left
simulating on the client (`held 0`). On a fresh copy the bodies are then moved by client physics: the client's own
`SetAllBodiesSimulatePhysics(1)` passes untouched in `c132_pose_sim_change` because the row is neither `grav_off` nor
`held`. The host corpse is at rest and sends nothing more, so the client pose drifts for good. Not determined: whether
the drift starts at the arrival apply (92.4 uu WARN) or at the later wake.

**Fix (class):** a client copy whose last applied row is a settled final is held (same hold as a frozen final) until
the host sends a new stream sample, unsettled snap or resnap for that guid (the host already resnaps, `g_pose.resnaps`;
the junior confirms the host resnaps a settled corpse that is disturbed, else the hold would pin a corpse the host
moved). Arrival of a copy with a settled row: apply after the copy's alive defaults are replaced, then hold, then
bone-check one tick later.

**Sibling sweep:** every arrival path of a posed actor (join replay, relevance re-arrival, checkpoint load), frozen
and unsettled finals on a fresh copy, gib/destroyed rows on a fresh copy.

**Proof:** one run: spawn, kill, wait settled snap, warp the client out past relevance and back, digest round after
re-arrival: Pose mismatches 0, card152 bone check under 10 uu on the re-arrived copy.

## Senior trace 2026-11-14 (static, `pose_c132.cpp`): fix is one condition, host side needs nothing

- Host resnap of a disturbed settled corpse is confirmed: `pose_sample` puts a `Sent` row back to `Moving` when body 0
  is more than `kPoseMoved` from `sent_root` (line 813), stream samples then go out (`stream_due`, line 826) and a new
  snap follows. Limit, unchanged by this fix: a limb that moves while body 0 stays inside `kPoseMoved` is not resent.
- A held client row is released by the next stream sample: `pose_stream_apply` calls `pose_stream_start` when
  `grav_off` is false, and that calls `pose_release` before it turns simulation on (lines 1738, 1700). So a hold on a
  settled final cannot pin a corpse the host moves.
- The game's own `SetAllBodiesSimulatePhysics(1)` on a held row is already undone (`rehold`, line 3156).

**Fix:** in `pose_try_apply` (line 1191) the freeze-and-hold path is taken by every final that is not unsettled:
`const bool hold = (r.flags & kPoseUnsettled) == 0;` used where `frozen` is used below it (gravity-on skip, line 1192;
`pose_applied_set`, `pose_applied_log`). Log texts keep reading `r.flags`, so a settled final still prints "settled";
`pose_stream_final`'s "not held" text takes the hold flag. `bone_again` stays frozen-only.

**Presume-defect checks for the junior's diff:** a streamed settled final arrives with `grav_off` true (held one tick
with gravity off, then `pose_freeze_step` turns it on: same as frozen, fine); a non-streamed settled snap on a fresh
copy (the 334 case) goes `Freezing` then `pose_freeze_step`; an unsettled final is untouched and its settled follow-up
then holds; the digest's Pose check (line 3460) skips rows by flag, not by hold, so it still compares held settled
rows.

**Predictions:** re-arrived copy row reads `held 1`, `simulating 0`; card140 freeze check under 10 uu one tick later;
0 `card140: client pose own SetAllBodiesSimulatePhysics(1) ... (row applied)` without "held again"; digest Pose
mismatches 0 from round 1; a host-side shove of the settled corpse (grenade) still moves the client copy (stream rows
after the hold).
