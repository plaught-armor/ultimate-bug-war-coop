# Card 186: The final row of a streamed corpse ends exact

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: queued 2026-11-11, from card 152 (c152-r3). Budget: three runs, shared build and run with card 187.

Read: `docs/cards/done/152-ragdoll-fall-stream.md` ("Run c152-r3" results, "Problems for the senior", "Senior
review"), `mod/src/ubw/pose_c132.cpp` (`pose_stream_final`, `pose_freeze_step`, `pose_freeze_check`, `pose_part`).

## Why
c152-r3 added a client WARN class: "card140 pose freeze check" x23. Nineteen are settled finals that read
simulating 1 and 2.5 to 4.0 uu one tick after a correct pose (leg bodies 14 to 18). One frozen final (guid 0x102)
showed its bone 413.7 uu from body 0. Guids 0xf8, 0x120, 0x16a and 0x1d4 flipped between final and unfreeze for up
to 62 snaps.

## Do
1. Trace first, in this card, before code; the senior reviews it:
   - 0x102: which space the bone read and the body read are in at the frozen final, and what set the bones
     (stream blend, posed bodies, or the hold). Name the cause or mark it open with what was ruled out.
   - The flapping guids: what the host's settle test reads on each flip, from the r3 host log.
2. Settled and unsettled finals go back to the r2 behaviour: gravity on at the final, no velocity zeroing, no held
   tick. Only a frozen final keeps a held tick, and only if the trace shows the bones need it.
3. `pose_part` back to 60 lines or fewer.
4. Drop the finals-count equality prediction. The end-state proof is the digest pose compare on streamed rows.

## Trace (Do 1, from c152-r3 and the code; no run)
0x102, the frozen final (client 97018740 apply, 97018753 check and bone line):
- Verified: both reads are world space. The bone read is `USceneComponent::GetSocketLocation` (resolved at
  hooks_ubw.cpp:2008, `g_c136_socket_location`), which returns the socket's world location (component-space bone
  times component-to-world). The body read is `pose_world(bodies[0])`, the body's world transform. So the 413.7 uu is
  not a space mismatch.
- Verified: the posed bodies were right. Body 0 was 0.0 uu from the frozen target; freeze check worst 0.2 uu one tick
  later; simulating 0; held 1 (update type 1 SkipAllBones, blend physics 1); ShouldBlendPhysicsBones 1; end tick
  registered 1.
- Verified: the stream blend was right while live. Samples 1 to 8 (97012983 to 97014764) all show bone 0 0.0 uu from
  body 0. No bone lines exist for samples 9 to 23 (the bone line budget), so the bone state over that gap is unknown.
- Ruled out as the difference: the card146 block. The game's ragdoll start was blocked on 0x10c too (one block each),
  and 0x10c's three frozen-final bone lines are all 0.0 uu (one with end tick registered 0, two with it at 1).
- Ruled out: the r3 held tick fixing the bones. 0x102 had it and still read 413.7 uu.
- Open: what set the bones. Bone 0 at (228907, 39581, 8588) is about 380 uu from the mesh component (229208, 39351,
  8618), the same scale as the anim pose's bodies centre offset at the ragdoll start ("326 uu from it"). So the bones
  look like an animation pose, not the bodies, but nothing in the r3 log proves it. Only 3 guids got bone checks
  (0x120, 0x102, 0x10c), with 1 bad frozen final out of 4.
- To close it in the run: the frozen-final bone line also logs bone 0's distance from the mesh component, and the
  bone is read once more one tick later. If the bone is still about 380 uu from body 0 but near the anim offset, the
  anim pose overwrote the bones. If it now matches body 0, the blend was a tick late.

The flapping guids (0xf8, 0x120, 0x16a, 0x1d4):
- Verified: the host's settle test never flipped. Every extra snap is the card137 unfreeze path. The game calls
  `SetAllBodiesSimulatePhysics(1)` on the dead corpse (caller exe+0x3B05087, IsDead 1, health falling 32 per call:
  -66, -98, -130, -162, -194, -226 on 0xf8 from 97031507 to 97035577). That path (pose_c132.cpp:1889) sets
  `start = now`, `still_ms = 0` and `unfreeze_next`. The snapshot that follows reads `still_ms 0 < kPoseStillMs` with
  the mesh awake, so it flags UNSETTLED|UNFREEZE ("UNSETTLED after 0 ms"). A settled snap follows once the corpse is
  still for 1000 ms, and the next hit starts the cycle again.
- Counts from the host log (card137 unfreeze lines are not capped; the card132 pose lines capped at 60 by 97035577):
  0xf8 25 unfreezes, 0x120 43, 0x16a 60, 0x1d4 60.
- Open: who hits the corpses. Each hit is 32 damage, the same as a Morita round in the client hit lines, and AI
  trooper Morita launches are logged (`AIMoritaRound`, shooter null), so it is likely the AI troopers shooting dead
  bugs. The shooter is not logged.
- On the client, each unfreeze snap continues the stream ("pose unfreeze ... continues the stream"), and each settled
  snap after it is another settled final. That is why 0xf8 alone has 10 of the 23 freeze-check WARNs. Every WARN reads
  "simulating 1; held 0", 2.5 to 4.0 uu one tick later, with gravity still off and velocities zeroed: the r3 change.

## Decisions (senior go, 2026-11-11)
- Settled and unsettled finals: not held. Gravity comes on at the final (`pose_grav_on` in `pose_stream_final`),
  no velocity zeroing, `check_owed` kept, so the card140 check and the bone line still run one tick later.
- Frozen finals keep the r3 held tick: r2 without it had frozen-final bone gaps of 0 to 25.4 uu, r3 with it read
  0.0 uu on 0x10c. The velocity zeroing stays too. The freeze on the next tick does not make it pointless: the held
  tick still steps the bodies once with gravity off, and a stream velocity left on them would move them off the pose
  before the freeze stops them.
- 0x102 diagnostic: every bone line now also logs bone 0's distance from the mesh component, and a frozen final's
  bone line is read once more on the tick after its check ("frozen final, one tick later", capped at 24 a map,
  `pose_bone_again`). What each outcome means and the fix it calls for:
  - The second read is within 1.0 uu of body 0 (first read off): the blend reached the bones a tick late. Fix: the
    frozen final's check and bone line move one tick later (or the hold lasts one more tick) so they read the
    blended bones.
  - The second read is still far from body 0 (about the first read's distance) and about as far from the mesh
    component as the anim pose offset (about 380 uu in r3): the anim pose overwrote the bones and the blend never
    took. Fix: the hold (`pose_hold`, SkipAllBones and bBlendPhysics) is set before the frozen pose is applied, or
    the copy's anim instance is kept from ticking from the final on.
  - Both reads within 1.0 uu: 0x102 does not repeat in this run; the case stays open with the extra line in place.
- Flapping: no change. The shooter's class is not one argument away: the `SetAllBodiesSimulatePhysics` hook gets
  the mesh and the flag only, no instigator, so the host unfreeze line stays as it is.
- `pose_part` split: the multi-part snap assembly is `pose_snap_part`; `pose_part` is 30 lines.

## Run c186-r1 predictions (one run, shared with cards 187 and 188)
- 0 "card140: client pose freeze check" WARN lines on streamed rows (r3: 23, all on settled or unsettled finals).
- Every "card152: client stream final" line for a settled or unsettled final reads "not held, velocities kept
  (gravity on)"; every frozen one reads "held one tick, velocities zeroed (gravity off)".
- Every "card152: client bone check ... (frozen final)" line is at most 1.0 uu from body 0, and its "(frozen final,
  one tick later)" line is at most 1.0 uu as well. A frozen final over 1.0 uu is reported with both reads and the
  outcome above it matches.
- Live bone lines still 0.0 uu from body 0; 0 "simulating 0" bone lines during a stream.
- Digest rounds after the last final: 0 pose mismatches on streamed rows.
- WARN classes against c152-r3: the card140 freeze check class gone; no new class.

## Run c186-r1 outcome (reported to the senior)
- 34 card140 freeze-check WARNs: settled finals 2.1 to 9.3 uu one tick later, frozen finals 2.2 to 25.5 uu after the
  freeze call. Cause: `pose_apply_bodies` ends with zeroed velocities and `g_pose_mesh_sleep`; anything set on the
  bodies after it (`SetEnableGravity(true)` at the final, the frozen final's velocity zeroing) wakes them. r2 turned
  gravity on before the snap path and had 81 "simulating 1" checks at 0.0 uu.
- Frozen final 0xf8: bone 428.5 uu from body 0 on both reads, 522.4 uu from the mesh component, after a card146 block
  on that guid (0x102 in c152-r3 the same shape). Four frozen finals read "gravity on": the normal order, a settled
  final then a frozen final with no stream between.
- Digest: 0 pose mismatches on streamed rows. Live bone lines 0.0 uu.

## Change for run c186-r2 (senior go, 2026-11-11)
- Settled and unsettled finals: `pose_grav_on` runs before `pose_apply_bodies` (in `pose_try_apply`, streamed rows
  that are not frozen), as in r2. Nothing touches the bodies after the apply's sleep. `pose_stream_final` no longer
  sets gravity.
- Frozen finals: no velocity zeroing in `pose_stream_final`. The held tick stays (gravity stays as the stream left it
  until `pose_freeze_step`).
- Frozen final with gravity on (settled final then frozen final): no change; the log text now reads "held one tick
  (gravity on)" with no claim about velocities.
- `pose_try_apply` split: the applied-state bookkeeping is `pose_applied_set` (`pose_try_apply` 53 lines).
- Diagnostic for 0xf8 / 0x102, on the frozen-final bone line, after the "one tick later" read:
  "card186: client frozen final guid 0x... snap N: simulating when the row reached the apply A, at the freeze step B;
  card146 game ragdoll starts blocked on this guid C". A = `c39_mesh_simulating` when the frozen row reached the
  apply, B = the same at `pose_freeze_step` entry, C = `game_blocked` on the row. Read only on a bone line that is off
  (over 1.0 uu from body 0), against the clean frozen finals:
  - A 1, B 1, C 0: the copy was a live ragdoll throughout and nothing was blocked; the bones' source is outside this
    diagnostic. Open, for the senior's card.
  - A 1, B 1, C at least 1: same physics state as a clean final, so the block's effect is not on simulation; the
    likely path is the blocked start's other work (the anim pose or the mesh's bone update), not the bodies.
  - A 0, any B, C at least 1: the copy was not simulating when the frozen pose arrived. The blocked start left the
    copy kinematic, so the posed bodies do not drive the bones and the anim pose shows. Points at the card146 block
    with no stream to start the copy's physics.
  - A 1, B 0: something stopped the copy's simulation in the tick between the apply and the freeze step (the game's
    ragdoll path or a physics state call), and the bones fell back to the anim pose.
  - A 0, B 1: the copy began simulating after the frozen pose was applied (a late game ragdoll start), which
    re-seeds the bodies and bones from the anim pose.
  - Clean frozen finals reading the same A, B, C as an off one: the diagnostic does not separate them; the cause
    stays open and the senior cards it.

## Run c186-r2 predictions (the last run for cards 186 and 187)
- 0 "card140: client pose freeze check" WARN lines on streamed rows, settled and frozen (r1: 34).
- Every "card152: client stream final" line for a settled or unsettled final reads "not held, gravity on before the
  pose (gravity on)"; every frozen one reads "held one tick (gravity off)", or "(gravity on)" after a settled final
  with no stream between.
- Live bone lines 0.0 uu from body 0; 0 "simulating 0" bone lines during a stream.
- Digest rounds after the last final: 0 pose mismatches on streamed rows.
- 0xf8-type frozen final (bone far from body 0): reported with its card186 line and the reading above. No fix in r2.

## Files
`mod/src/ubw/pose_c132.cpp`, `tools/scenarios/c186-r1.txt` and `c186-r2.txt` (shared with 187 and 188), this card.

## Senior review (2026-11-11)

Accepted on c186-r2, DLL `50615a859586a9ad` (rebuild: no work to do). Diff against the pre-card snapshot read in
full: gravity comes on before `pose_apply_bodies` for settled and unsettled finals, `pose_stream_final` no longer
touches the bodies, the frozen final keeps its held tick. Checked in the logs: 0 "card140 pose freeze check" WARNs
(c186-r1: 34), digest rounds 1 and 2 with 0 mismatches, host WARN classes at the baseline, no new client class
against c152-r3. Two of three runs used (the r1 report's "2 of 3" was a miscount).
Left open, carded: one frozen final with the bones 316.2 uu off the bodies on a re-created copy (card 191). The
`sim_at_final` / `sim_at_freeze` diagnostic stays until card 191 closes. Host backlog peak 262 frames (r1: 74) goes
to the soak (card 127).
