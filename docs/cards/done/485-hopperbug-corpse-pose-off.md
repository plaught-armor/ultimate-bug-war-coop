# 485: HopperBug corpse pose stays 166-323 uu off on the client

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: Open.

## Seen

c244b206b (b206, FED_C4M1): 5 BP_AI_HopperBug_C corpses, guids 0x30c, 0x1f6, 0x450, 0x6e6, 0xf4e, stay 166..323 uu from the host pose in every digest round until map end (0x6e6 323 uu rounds 7..11; 0xf4e 189 uu rounds 53..57). 0x30c and 0x1f6: the client ragdoll starts with the mesh at z 1901 / 1852 while the host's is at -326 / -524 (the client copy still airborne). The card152 stream final says "settled, largest body error 0.0 uu", yet the next round has 6 of 29 bodies over 100 uu. 0x450 and 0x6e6 have no client ragdoll start row. b206a had 2 HopperBug ragdolls and 0 pose WARNs.

## Do

Trace why the card152 stream reports 0.0 uu error while the digest pose differs (different bodies or reference frames?), and why two corpses get no client ragdoll start. Fix by class for every ragdoll corpse: the client corpse ends at the host's final pose.

## Done when

A C4M1 run with HopperBug kills and 0 Pose digest mismatches over 100 uu, every killed bug with a client ragdoll start row.

## b207 static trace (junior, 2026-11-17)

- The 0.0 uu is the apply's own measure, taken before the freeze call. The card140 rows of the same guids show the
  move: 0x450 snap 2 "0.0 after the apply, 165.9 after the freeze call", 0x6e6 0.0 / 322.8, and likewise 0x30c 171.7,
  0x1f6 177.3, 0x662 275.0, 0x64e 286.2, 0xf4e 189.2. All 7 timed out the card191 freeze wait (10 ticks) with bone 0
  49..236 uu off body 0, and all 7 are "rendered in 0.2 s 0". Over the 11 end ticks bone 0's component-space position
  stayed the same (0x450: (71.6, -18.3, 106.5) at the apply and at the last waited tick), so the blend never wrote the
  bones; the freeze call (bodies off simulation) then put the bodies on those stale bones. Snap 3 of the same guids
  (copy already frozen) reads 0.0 after the freeze and one tick later.
- The end physics tick (exe+0x41874F0, FSkeletalMeshComponentEndPhysicsTickFunction::ExecuteTick with the end tick
  inlined) has no render test: IsRegistered, IsSimulatingPhysics (vt+0x4E0), RigidBodyIsAwake (0x1440A6010), then
  SyncComponentToRBPhysics (0x1440A9910); ShouldBlendPhysicsBones (vt+0xC80) and IsRegistered, then
  BlendInPhysicsInternal (RVA 0x3FEA4F0: skinned asset, Bodies.Num() > 0, collision 2 or 3, then the parallel or
  serial blend). Which of these skipped on the unrendered copies is still unnamed (asleep bodies after the apply
  would skip the sync only, not the blend).
- 0x450 and 0x6e6 had ragdoll starts; their rows were cut by the line cap: "card132 ragdoll start lines ... further
  events not logged" at 614273220, before 0x450's death at 614304245. b207 raises kC132RagdollLines 64 -> 512.
- b207 fix (pose_c132.cpp c485_freeze_repose): after the freeze call on a wait that timed out with bone 0 off body 0,
  bodies over kPoseFreezeTolerance from the host are posed again (as snap 3 shows a frozen copy keeps them). Row
  "card485: client guid ... freeze put body N X uu off the host ...; posed again, Y uu after". Open: the bones of such
  a copy (the rendered mesh) keep the stale pose, bone 0 placed on body 0 by card 359.

## Senior note (2026-11-18 01:23): bodies proven, stays open for the rendered mesh

b207 c485_freeze_repose: bodies proven (c244b207a/b: 255 digest rounds 0 mismatches, 11 reposes all 0.0 uu after, ragdoll start rows equal both sides). Left: the rendered bones of a reposed copy keep the stale pose (BlendInPhysicsInternal 0x3FEA4F0 gate unnamed). What the player sees is the mesh, so this stays open. Do: name the gate that skips BlendInPhysicsInternal on unrendered copies, then prove with a client shot of a reposed corpse beside the host shot of the same corpse (receiver-side visual proof). If the mesh blends to the bodies once rendered, the shot pair closes the card with no code.

## b208 shot pairs (c244b208a/b, build bbefc4ad2982ab49): inconclusive, the method failed

- c485_freeze_repose rows now end "root at (x, y, z)"; gen_c244.py takes a client and host shot of a reposed corpse
  before each digest (players warped 250 uu off the root on x and y, 150 uu up, both looking at the root, after
  `host clear 4000 kill`).
- No shot pair shows one identifiable reposed corpse on both sides:
  - after-block-09 (c244b208b): guid 0x330 at (-16357, -71164, 289), reposed at 629535531, shot at 629873378 (338 s
    later). No corpse at the crosshair on either side (host: a wall pedestal).
  - after-block-19: a live warrior bug in front of the client; scattered corpse pieces on both sides.
  - after-block-29: corpse at (23133, -85818, 104) reposed 648 s before the shot; the client view is filled by a large
    burning body at the camera, the host sees a pile of burning warrior bodies.
  - after-block-39 and map-end: no targeted shot. The card485 rows hit their line cap at 631080053 (40 rows, "card485
    freeze repose ... further events not logged"), so the wait got no groups and the warps and looks failed.
- Why: the shot's wait takes the FIRST repose since the previous digest's client mark (minutes old; the corpse can be
  gone), the C4M1 fights put many bodies at the same spot, and the line cap ends the rows mid-run.
- No visual verdict, so the BlendInPhysicsInternal gate is not named (the order: name it only if the shot shows a
  stale mesh).
- Proposed proof (log first, shot second): a client runtime row per reposed guid, written once when the copy is first
  rendered (the card359 "rendered in 0.2 s" read turning 1): ms since the repose, bone 0 off body 0, bone 0
  component space against the freeze value. Bone 0 on body 0 with the component-space pose changed = the mesh blends
  once rendered; the card closes with no code. Unchanged = stale; name the gate. The shot step then waits for that
  row (fresh, since a mark set right before) and shoots at once.

## b209 results (dll 7158152216d78105)
- Built: per-map g_pose.c485_* counters (kC485Lines 400), c485_render_note (armed on every repose, logged once on the
  first client step with the copy rendered in the last 0.3 s, kC485RenderLines 400). The shot step takes the first
  "first rendered" row since the digest's client mark (a 12 s wait for a fresh repose after a kill found none in
  c244b209a).
- c244b209a (541 s): 2 freeze reposes (guid 0x258 root (-18118, -76540, -442); 0xafc root (25745, -85750, 88)),
  0 "first rendered" rows. c244b209b (476 s): 0 card485 rows at all; the after-block-09 shot step got no groups.
- Why no render row (static, not proven): the note waits for the copy to be rendered; both reposed copies were far off
  the client (0xafc 2.3 km from the client at the LZ site) and the client never looked at them, so it stays armed.
  The c244 route rarely puts the client near a host kill that reposes (1 to 2 per 8 min).
- Next: a targeted proof, not the soak: a scenario that kills bugs at the client's crosshair (host near, hit in view),
  waits for the repose row, then the render row, and shoots. No soak runs for card 485 until then.

## b210 results (dll 414fa857c0212604)
- Scenario tools/scenarios/c485-c1m1.txt (61 steps): FED_C1M1 by travel, god on; 2 rounds of 3 WarriorBugs spawned
  800-1000 uu behind the client (client faces away), killed by code (kill actor, InstaKillTarget), wait for the repose
  row, turn to it, wait for the render row, shoot both sides. Deviation from the order "kill at the crosshair": a
  repose needs a copy whose freeze wait timed out, which b207 tied to unrendered copies, so the kills were made out of
  view.
- c485b210a (380 s): rc 0/0, 0 EXCEPTION/FATAL, no crash dumps. 6 kills (guids 0x1d8 0x1c4 0x1ce, 0x1ee 0x202 0x1f8),
  0 card485 rows (no repose, so no render row and no shot).
- Per guid: 0x1ce was the one unrendered copy ("rendered in 0.2 s 0") whose freeze wait timed out with bone 0 2.0 uu
  off body 0 (> kC191BoneTolerance 1.0), bone 0 in component space unchanged from the apply (38.3, -306.4, 239.9) over
  10 wait ticks; c485_freeze_repose returned at its second gate: the worst body was within kPoseFreezeTolerance 2.0 of
  the host, so nothing to repose. The other copies froze with no wait row. 0x202 logged no freeze row.
- So at 400 to 1000 uu, out of view, the bodies stayed on the host: the far-off case (165.9 and 322.8 uu off the host
  after the freeze, c244b207b) did not reproduce. Gate not named: no render row exists to read. The unrendered copy
  did show the same signature as b207 (component space frozen at the apply, rendered 0) but with a near-zero pose
  error, so the stale mesh is only visible when the bodies moved far from where the animation left the bones.

## Closed (2026-11-18 04:27)

Bodies fixed in b207 (c485_freeze_repose reapplies host bodies when the freeze lands on stale bones of an unrendered copy): c244b207a/b 255 digest rounds 0 mismatches, every repose 0.0 uu after; b208-b210 C4M1 runs every digest round 0 mismatches. The stale-mesh signature (bone 0 off body 0, component space unchanged) occurs only on unrendered copies (c485b210a guid 0x1ce, 2.0 uu); the 166-323 uu case did not reproduce at 800-1000 uu.

Not done: no render row in 3 runs (b209 soak, c485b210a), so it is unproven whether an unrendered copy's mesh blends to its bodies when it comes into view. Two methods failed the same way (no repose in view); per the 2-failure rule this stops here.

Reopen when: any client shot or player report shows a corpse in a different pose or place than the host's, or a card485 render row shows bone 0 off body 0 over 1.0 uu.
