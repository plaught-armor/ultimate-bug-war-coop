# Card 152: A falling corpse follows the host's ragdoll

Status: queued 2026-11-08, from card 140 run 15 (c140o). The user allowed this gap for the next playtest as a known
issue, to be fixed after it. Budget: three runs. Visible world state.

Read: card 140 Run 15, cards 137 and 146 (pose rows, freeze, the blocked game ragdoll restart), card 151 (packet
size).

## Why

c140o round 5, guid 0x382: after the host's unfreeze row (ragdoll start), each side simulated its own physics, and the
check 3.0 s later read 379 uu between them. The host froze it at +6 s and the client matched at 0.0 uu. For those
seconds the corpse is visibly in a different place on each screen. Until this card lands, the gate checker leaves out
bodies that are still simulating and counts them on a separate line.

## Do

1. Static: the host pose sample while a body simulates (which bodies matter for what a player sees: root, pelvis,
   head, limbs), the bytes per sample after card 150/151 encoding, and a rate (about 5 Hz) with a budget per window
   for a nuke's worth of corpses at once.
2. Host streams samples while the body simulates. The client keeps the body kinematic and interpolates between
   samples, then takes the freeze row as the final pose.
3. One run with a nuke and a fight at TD 1.0: the gate checker counts simulating bodies again, with the worst error
   while falling under a bound written first; the budget is held; the freeze is still 0.0 uu.

## Done when

- The trace, the change, the run lines. The usual Report items. It ends with "Report complete."

## Static trace: who restarts a corpse's simulation (from card 140 c140q, 2026-11-08)

In c140q, 14 corpses took 34 to 190 s to freeze, with 619 unfreezes between them, and each unfreeze was a
`SetAllBodiesSimulatePhysics(1)` from caller exe+0x3B05087. That address is
`USkeletalMeshComponent::execSetAllBodiesSimulatePhysics` + 0x77 (RVA 0x3b05010), the Blueprint thunk, so the caller
is Blueprint bytecode.

Search: the script import hash of `/Script/Engine.SkeletalMeshComponent:SetAllBodiesSimulatePhysics` was looked up in
all 2000 `/Blueprints/` packages and 582 other BP, ABP, AN, GA and WB packages from the game's IoStore (read-only; the
utoc hash was checked unchanged after). One package imports it: `/Game/Blueprints/Enemies/BP_AI_AttackSystemEnemy_Base`,
the parent of the warrior Blueprints.

Call sites in that package (byte offsets in each export's bytecode):

- `ExecuteUbergraph_BP_AI_AttackSystemEnemy_Base` at 0x23a8. Its neighbours, in order: `IsAbleToRagdoll` 0x22eb,
  `SetCollisionProfileName` 0x234d, `SetComponentTickEnabled` 0x237f, the call at 0x23a8, `WakeAllRigidBodies` 0x23cd,
  `BlendHeadChildren` 0x23d7, `SetEnableGravity` 0x23ff, then the Blueprint function `Wake Ragdoll` 0x2423. This is
  the game's "start or wake the ragdoll" block.
- The Blueprint function `PerformGroundLineTrace` at 0x4e1.

Nearby in the same ubergraph: `SleepRagdoll` 0x8c5 and 0x1ea3, `DisableRagdoll` 0x1edd, `EnableRagdoll` 0x1f04 and
0x331e, `Wake Ragdoll` 0x1f0e, 0x2423, 0x3222 and 0x333b, `CheckRagdollIsGrounded` 0x2049 and 0x20bc, and
`CheckToFreezeRagdoll` 0x20d8. The package names include `RagdollFreezeTimerHandle`, `RagdollTimeRemaining`,
`RagdollWakeRequested`, `RagdollRequested`, `RagdollEnabled`, `CanRagdoll`, `ApplySavedRagdollPose`,
`HasSavedRagdollPose` and `IsDead`. So the 6 s settle is a Blueprint timer (`RagdollFreezeTimerHandle`), and a wake
request starts the bodies again and re-arms it.

Not proven: which event enters the 0x23a8 block. The ubergraph's direct entry offsets found in the package all lie at
or below 0x18d3, so 0x23a8 is reached by a jump inside the ubergraph (a latent resume or a sequence), not as an event
entry of its own. The likely trigger is the two `HealthComponent` damage bound events
(`BndEvt__BP_AI_AttackSystemEnemy_Base_HealthComponent_K2Node_ComponentBoundEvent_3/4_HealthCompDamageEventDelegate__DelegateSignature`),
which fits the logged corpse health falling after death. A runtime check would settle it: log the Blueprint frame's
function and offset on each `SetAllBodiesSimulatePhysics(1)` from exe+0x3B05087.

What this means for this card:

- A corpse that is hit simulates again on the host through this block. The client copy is host-posed, and card 146
  blocks the client's own ragdoll start on it (that block came from exe+0x37C53CB, the "ragdoll left" path in the same
  class). So while the host re-simulates a corpse, the client holds the last pose until the next freeze row. The stream
  from Do step 2 has to cover a corpse that wakes again after a freeze, not only the first fall.
- The 0x23a8 block goes through the same thunk, exe+0x3B05087. If the client ever runs it on a host-posed copy, card
  146's block applies only when that start passes through `detour_c132_set_simulate` with a nonzero exe caller. Check
  this in the card 152 run: count client starts from caller exe+0x3B05087 on host-posed copies, expected 0 after a block.

## Playtest 2026-11-08 addition (finding 11)

Dead bugs hang in the air on the client. Measured case: WarriorBug 0x96a frozen on host pose snap 7 at client
t=259286902 ("simulating 0; held 1"); 0.45 s later all 22 bodies were over 100 uu from the host's, body 6 at z 7329
against 6902. The host's body kept falling after snap 7 and the client held it. Added scope: a frozen corpse whose
host body is still moving gets later rows until the host body sleeps, with a final rest row. Added proof: each frozen
corpse ends within a set distance of the host's final pose and on the ground.

## Senior trace and scope (2026-11-11)

Scope after review: this card is one goal, **the client's corpse follows the host's while the host's bodies move**
(the first fall, a wake after a freeze, and finding 11's frozen corpse that keeps falling). The no-ragdoll corpses
(finding 5) moved to card 177.

What exists (pose_c132.cpp, all read 2026-11-11; verify the marked lines before code):

- Host row per corpse, made at the ragdoll start (`c132_pose_track`). `pose_host_row` samples every 250 ms
  (`kPoseStepMs`) and sends a snap only when the corpse is settled (still 1 s, asleep, or not simulating) or at 8 s
  (`kPoseForceMs`, flag `kPoseUnsettled`), and again when body 0 is over 20 uu from the sent root. The game's freeze
  and unfreeze send a snap at once (`c132_pose_sim_change`). Verified by reading.
- A snap is 12 + 14 bytes per body, base64 in `P` text frames of up to 1320 chars (`pose_send`). A 22-body warrior is
  320 bytes, about 430 chars, one frame. Verified by arithmetic; confirm the body counts per bug class from a log.
- Client: an unfrozen row is applied once and the copy then simulates on its own (`pose_try_apply`, state Applied).
  A frozen row is posed, then one tick later its simulation is turned off and it is held (`pose_freeze_step`,
  `pose_hold`: bBlendPhysics on, SkipAllBones). Card 146 blocks the game's own ragdoll start on a posed copy.
- So between the ragdoll start and the first snap (up to 8 s), and after any unsettled or unfreeze snap, the two sides
  simulate apart. That is the 379 uu of c140o. Verified by reading.
- Finding 11 (frozen on the client, host body still falling): open. The snap's frozen flag is
  `!c39_mesh_simulating(mesh)` at snap time. Find from the playtest host log for WarriorBug 0x96a which call made snap
  7 frozen while its bodies still moved, and whether a later row was sent. The fix below covers it only if the host's
  sample loop sees the movement; say which line shows that.

Design (one mechanism, in pose_c132.cpp; no second relay):

- Host: while a tracked corpse's bodies move (the `moved` value `pose_host_row` already computes, any flag state), it
  sends a stream sample every step, flagged as a stream row, until the corpse is still; the settled or frozen snap
  that ends it is the final pose, as now. A per-step budget caps stream samples (write the cap and the bytes per
  second it gives for a nuke's worth of corpses; corpses over the cap take turns, oldest sample first, and the cap
  being hit is logged once per window, not per corpse).
- Client: a copy that has had a stream row is kinematic and held (the frozen-row hold) from then on; each tick it is
  moved between the last two samples; the final row ends it as a frozen row does today. The copy does not simulate
  on its own again until an R row or map end.
- Open, to verify before code: (a) that a held, non-simulating copy shows a new pose when its bodies are set each
  tick (the frozen path sets bodies while simulating, then freezes one tick later: name the call that moves a
  kinematic body and the line that makes the bones follow); (b) the client's own death ragdoll start before the first
  stream row (card 146's block applies only to a posed copy: decide whether the copy may simulate for the first
  250 ms or is held from its death edge, and say why); (c) frame size against `kChanTextMax` for the largest body
  count; (d) the join replay (`c132_pose_replay`) sends end state only, never stream rows.
- The digest gate counts simulating corpses again (`g_pose.sim_skipped` goes away or reads 0).

Predictions to write before the run: the worst body error while a corpse falls, as a bound derived from the sample
interval, the measured rtt and the fall speed (write the arithmetic); 0.0 uu one tick after the final row; stream bytes
per second at the peak against the cap; 0 client ragdoll starts from caller exe+0x3B05087 on a streamed copy; 0 new
WARN classes; the table peaks (`kPoseRows` 128) logged.

Stop and report before code if (a) is false or the cap cannot hold a nuke's corpses under a rate you can justify.

Files: `mod/src/ubw/pose_c132.cpp`, `mod/src/ubw/hooks_internal.h`, `mod/src/ubw/digest_c132.cpp` (only the
simulating-row exclusion), one scenario, this card.

## Junior trace (2026-11-11)

No code and no run yet. Addresses are in the shipping exe (image base 0x140000000). Log lines come from the
2026-11-08 playtest: the host log is `client/UBWCoop-menu-20261108-190720.log` and the client log is
`logs/playtest-menu-20261108-191343.log`.

### (a) A held, non-simulating copy takes a new pose when its bodies are set: true for the bodies, true for the bones by engine logic (not seen in this binary)

**Body move: verified by disassembly.**

- `pose_apply_bodies` calls `g_pose_body_set(body, &x, kTeleportPhysics, false)`. That is FBodyInstance::SetBodyTransform
  at 0x143FC40B0, with `kTeleportPhysics = 1` (pose_c132.cpp:64).
- 0x143FC40B0 saves the teleport byte (r8b) at 0x18(%rsp). That is `0x30(%rbp)` after the prologue, and it is
  captured by address into the lambda's capture block (0x143FC41A5).
- 0x143FC4191 calls 0x1420C9A60, which reads the actor's object state byte at +0x18C and returns true when the state
  is not Static (3). The bodies of a ragdoll mesh are never Static, so the lambda at 0x143FA07D0 runs under the write
  lock (0x143FA84E0). It jumps to 0x143F95DE0.
- 0x143F95DE0 makes two calls. 0x1420C9AB0 returns true when the state is Kinematic (2). 0x140C7BC80 reports whether
  the body can simulate.
- If either call returns false (any body that is not sim-kinematic), the code jumps to 0x143F95E4A. That tail-calls
  0x1420CB3E0, the global-pose set with r8b = 1.
- If both return true (the body is sim-kinematic) and the teleport byte is not 0, the code calls 0x1420CBBA0 (the
  kinematic target) and then the same global-pose set (0x143F95E3E to 0x143F95E63).
- Only teleport type None takes the deferred-target path (0x144013BB0).
- So with TeleportPhysics, every body is placed at once in the same call, on a kinematic body as on a simulating one.
  The card 140 freeze checks ("0.0 uu after the apply, 0.0 after the freeze call, 0.0 one tick later", client log
  line 24005) are the runtime side of this for body positions.

**Bones follow: engine logic plus the card 140 hold. Not found by address in this binary.**

- The bone refresh does not move the bodies back. `pose_hold` sets KinematicBonesUpdateType (mesh +0xA6C) to
  SkipAllBones. With that setting, UpdateKinematicBonesToAnim (0x144019C30) returns at once unless a body simulates
  (exe 0x144019C66 to 0x144019C84, card 132 doc).
- The bones read the bodies through bBlendPhysics (set by `pose_hold`). In UE5, ShouldBlendPhysicsBones() is true
  when bBlendPhysics is set and the collision has physics. EndPhysicsTickComponent then runs BlendInPhysicsInternal,
  which writes every bone from its body's world transform at weight 1, whether or not any body simulates.
  SetAllBodiesSimulatePhysics(false), our freeze call, leaves the end-physics tick registered for that reason.
- I did not find EndPhysicsTickComponent or ShouldBlendPhysicsBones by address: neither has a symbol in
  offsets.generated.h, and the exe has no strings for them. The runtime evidence is indirect. Frozen copies held this
  way showed the host's fallen pose (card 140 run 15: 74 freeze checks at 0.0 uu, "blend physics was 1").
- Before the hold, the update type was 0 (SkipSimulatingBones), and run 14 showed the bodies being pulled back to the
  bones (994 uu).
- No test has yet moved a held copy to a second pose. The first stream run proves it: a per-sample line for the
  first 3 streamed copies gives the root bone's world location (GetBoneLocation of bone 0) against body 0 after the
  set, with a prediction of 0.0 uu one tick later. If it reads the old pose, (a) is false and I stop.

### (b) The client's own ragdoll start before the first stream row: the copy is held from its death edge

The client's own start is gated in one place, `detour_c132_set_simulate` (enemies.cpp:2678), which calls
`c132_pose_game_ragdoll`. Card 146 blocks it only when the guid's row is Applied or Freezing (pose_c132.cpp:273).

Decision: block every game start on an NPC copy that is streamed or about to be. Do not let the copy simulate for its
first 250 ms.

Why:
- The client's own ragdoll starts without the host's hit impulse, and from a body pose up to one trip old. Whatever
  it does in the first sample interval is wrong.
- The first stream row then teleports the copy back, which shows as a jump.
- In this design the first sample goes out in the same tick as the host's ragdoll start (`c132_pose_track` will send sample 0). It
  reaches the client one way later: rtt/2, a median of 29 ms and a p90 of 38 ms on the playtest (the host's
  "rtt N ms" lines: n 111, median 57, p90 75).
- So in the usual order the row arrives before, or within a few ms of, the client's replicated death. Holding costs
  at most a few ticks of a still death pose.

How, all in pose_c132.cpp:
- A start on a guid with no row yet is blocked, and it is noted in a fixed wait table (guid, weak mesh, time).
- A row arrives: it is applied as a stream row and the wait entry is cleared.
- No row within kPoseStartWaitMs = 500 ms (2 sample intervals): pose_c132 starts the ragdoll itself, through
  `detour_c132_set_simulate(mesh, true)`. Its caller is in the DLL, so card 146's gate lets it through. That path
  logs a WARN "no stream row".
- The table's peak is logged, and a full table is a WARN.
- No change to card 146's block beyond widening its state test.

Finding 11 (below) adds a second hole: the game's own SetAllBodiesSimulatePhysics(1) on a copy is not gated at all.
Under this design, `c132_pose_sim_change`'s client branch (called after the original, pose_c132.cpp:1084) turns a
held copy's simulation off again in the same call and re-holds it. No physics step runs in between. enemies.cpp is
not touched.

### (c) Frame size: one frame at the largest body count

- Body counts in the host log: 1192 snaps of 22 bodies and 4 of 15. No other count appears.
- At 22 bodies the blob is 12 + 22 × 14 = 320 bytes, which is 428 base64 chars. With a header of up to 19 chars
  ("P 29d8 20 0 1 22 9 "), a frame is about 447 chars against kChanTextMax 1400 (ue/chan.h:68).
- At the kPoseBodies cap of 64 bodies, the blob is 908 bytes and 1212 chars, still one frame under kPoseChunk 1320,
  plus a header of at most 37 chars.
- So a stream sample is always one frame and is never reassembled.

### (d) The join replay sends end state only: verified

- `c132_pose_replay` (pose_c132.cpp:1163) sends only rows whose state is Sent (line 1171 skips every other row), and
  it sends each row's `r.blob`.
- Stream samples are sent while the row stays Moving. They are encoded into a separate static buffer, never into
  `r.blob`, so a joiner gets only the final or frozen snap that ends the fall.
- A joiner during a fall gets that row from `c95_send_all` when it lands. That is end state, after the
  `pose_guid_check` owed rules, as today.
- `c140_pose_host_check` keeps reading `r.blob`, which is the last end-state snap.

### Finding 11: snap 7 came from the game's own freeze, and the host body did not keep falling

**Which call: verified.**
- Host line 42074 reads "card137: host pose freeze guid 0x96a snap 7 sent at the game's SetAllBodiesSimulatePhysics(0)".
  That is `c132_pose_sim_change` (pose_c132.cpp:1078), called from `detour_c132_all_bodies` (enemies.cpp:2535) after
  the game's own freeze.
- Snaps 1 to 5 were the game's SetAllBodiesSimulatePhysics(1) unfreezes (host lines 41556 to 41728).
- The client applied snap 7 at client t=259251770 (line 24005). With the clock offset of 249903283 ms, that is host
  t=9348487, 103 ms after the send.

**No later row: verified.**
- The host log has no 0x96a line after 42074. Its pose lines run on to line 105127 (other guids, snap 9 for 0x29ae),
  so the cap was not hit.
- A frozen host mesh is settled (`!c39_mesh_simulating`, pose_c132.cpp:497). It would be resnapped only if body 0
  moved over kPoseMoved (20 uu, line 491), and no snap 8 exists.
- So the host corpse stayed within 20 uu of snap 7.

**The playtest note's reading does not hold.**
- The round 41 WARN (client line 24728) compares the client's copy with the row's own decoded pose (`pose_want`, the
  snap 7 blob), not with the live host. The client's body 6 was at z 7329, 427 uu above the snap's 6902. The client
  copy moved off a held pose; the host's body did not keep falling.
- The sequence on the client:
  - The corpse re-arrived at t=259286896 ("card137: client corpse arrival ... posed 0 ms after it arrived").
  - card131 called HandleDying on it.
  - The snap 7 hold read 0.0 uu one tick later (line 24717).
  - 0.45 s later its bodies were off. At 807 ms it was simulating ("frozen row simulating 807 ms after its apply",
    line 24740). The round line says "own freeze calls 4" and "4 frozen rows simulating".
- The call that restarted it is not in the log: the card146 block lines were at their cap (40 of 40) and the card140
  own-call lines at kPoseLines 60.
- Candidate: the game's own SetAllBodiesSimulatePhysics(1) on an applied copy, which is not gated. Lines 14237 and
  14352 show the client game making that call on applied rows earlier in the map.
- Does the sample loop see host movement? Only body 0 over 20 uu, for a frozen or Sent row (line 491). In this case
  there was no host movement to see.
- The stream fixes finding 11 only through the hold-and-re-freeze of (b), which counts and undoes the client's own
  restart. It does not fix it through host samples.

### Cap arithmetic

- **Per corpse:** one 447-char frame per 250 ms step is 1788 B/s while it falls. Settle times from the host's snap
  lines (n 274): median 3042 ms, p90 4362 ms, max 7141 ms. That is about 12 samples (5.4 KB) for a median corpse and
  18 (8.0 KB) at p90.
- **Cap:** kPoseStreamPerStep = 24 samples per step, which is 96 frames/s and 42.9 KB/s at most.
- **Headroom:**
  - The playtest's peak 5-s windows were Digest 135456 B (27.1 KB/s, client receive), Fx 32270 B (6.5 KB/s) and
    Pose 20526 B (4.1 KB/s).
  - With the stream, that is about 80 KB/s against the connection's CurrentNetSpeed of 250000 B/s (host line 6371),
    32% of it.
  - Card 150's packing lowers the wire bytes. The text bytes are the conservative figure.
- **Over the cap:** corpses take turns, oldest sample first. Each corpse's sample interval becomes ceil(N / 24) steps:
  - N 24: 250 ms.
  - N 48: 500 ms.
  - N 64: 750 ms.
  - N 128 (the kPoseRows table): 1.5 s.

  One "cap hit" line per window.
- **A nuke's count is not measured.** The host snap lines give a peak of 5 corpses moving at once, but those lines
  are capped per map, so that is a floor. The run logs the falling-row peak.
- **Reliable channel:** 96 frames/s at an rtt of 57 ms is about 6 frames in flight. UE's reliable buffer of 256
  bunches per channel is the hard limit. That figure is from engine knowledge and is not verified in this binary; the
  run logs the in-flight peak.

### Error bound (predictions)

Inputs:
- Sample interval Δ = 0.25 s (under the cap).
- One way = rtt/2: 29 ms median, 38 ms p90.
- Gravity g = 980 uu/s² (testrun.cpp:208).
- Fall speed v = sqrt(2·g·h): 626 uu/s for a 2 m drop and 990 uu/s for 5 m. The bound uses v = 1000 uu/s.
- Quantization: kPoseUnit / 2 = 0.125 uu.

The client moves the copy between the last two samples, so it shows the host's pose from Δ + one way ago:
- Against the live host body: E ≤ v·(Δ + rtt/2) + g·Δ²/8 + 0.125 = 1000 × 0.288 + 7.7 + 0.1 = 296 uu at p90. That is
  288 uu at the median, and 180 uu for a 2 m drop.
- Against the host sample of the same host time (lag-matched): E ≤ g·Δ²/8 + 0.125 = 7.8 uu. That is the
  interpolation sag of a parabola; a tumbling limb adds its angular term.
- Over the cap (N 64, interval 0.75 s): live E ≤ 1000 × 0.788 + 980 × 0.5625 / 8 = 857 uu.

Predictions for the run:
1. Worst lag-matched body error on a falling streamed copy ≤ 8 uu, and worst live error ≤ 300 uu (Δ 0.25 s).
2. 0.0 uu one tick after the final row. The final row is today's frozen or settled snap, through the same hold.
3. Stream bytes per second at the peak ≤ 42.9 KB/s, with the peak falling-row count logged.
4. 0 client ragdoll starts from caller exe+0x3B05087 (or any exe caller) on a streamed copy. Every one is blocked
   and counted.
5. The game's own SetAllBodiesSimulatePhysics(1) on a held copy is undone in the same call, and counted.
6. 0 new WARN classes.
7. Table peaks logged: kPoseRows 128 and the start-wait table.

The digest gate counts streamed copies again (`sim_skipped` goes away):
- A streamed row is compared against its last received sample with tolerance kPoseTolerance + v_row·Δ, where v_row
  comes from that row's last two samples.
- A final row is compared at kPoseTolerance, as today.

## Run c152-r1 (written before the run, 2026-11-11)

### (a) Static line, as asked

No log proves that the bones follow a set body while simulation is off. The nearest existing line is card 140 run 15,
client line 24005: "0.0 one tick later ... held 1 ... blend physics was 1". That line measures the body positions
one tick after the freeze (pose_freeze_check reads the bodies). It never reads a bone, so it shows only that the
bodies stay where they were set. This run adds the bone check (below). If a held copy shows the old pose, the card
stops and reports. It does not try a second mechanism.

### Backlog measure (senior condition 2)

The mod channel has a backlog measure: `chan_pace_depth(pc)` (ue/chan.h), the frames waiting in that PC's pace queue.
Before a step's stream samples, the host reads the deepest queue over the remote PCs. Above
`kPoseStreamBacklog` = 8 frames (one step's pose frames for 8 corpses, about 3.5 KB), every stream sample of that
step is skipped and counted (`backlog skips`). The deepest queue seen is logged as `backlog peak`. Sample 0 at the
ragdoll start uses the same test. Final rows (the settled, frozen, unsettled and unfreeze snaps) never go through
this test. No new queue was built.

### Scenario

`tools/scenarios/c152-r1.txt`. The route is card 132's pose proof, parts 1 and 2: FED_C2M1, the checkpoint-field
slope at (229312, 38956). Bugs spawned at z 9000 drop onto the slope, are killed in the air and slide. The scenario
adds one round of card 140's NukeLauncher at the same field to reach the peak count of corpses moving at once. FAST=1.
Every state step waits on its log line.

The 2 s after each spawn group is the copy's replication time. A corpse killed before its copy has a NetGUID streams
nothing, which is the existing route's step. Part 3 of card 132's route (the gore autoplay with its fixed 120 s) is
dropped, since it is dead time for this card.

### Predictions

The log line that shows each prediction is named in brackets.

1. **Error while falling.** Every streamed row checked by a digest round during the fall is within kPoseTolerance
   plus the row's own span, the largest body distance between its last two samples [card132 round lines: 0 WARN
   card132 rows with "(card152 streamed, against its last sample)"].
   - The lag-matched 8 uu bound has no per-sample log line in this run.
   - The live ≤ 300 uu bound is not measured either: the host and client logs do not share a clock.
2. **Final rows.** 0.0 uu one tick after the final row [card140 freeze check line after each "card152: client stream
   final"; err_max in the client map line].
3. **Bytes and peaks.** Stream bytes ≤ 42.9 KB/s at the peak ["card152: host pose stream map totals": samples, bytes,
   moving peak, step peak, cap hits, deferred, backlog skips, backlog peak].
4. **Client ragdoll starts.** 0 client ragdoll starts on a streamed copy. Each one the game makes is held ["card152:
   client held the game's ragdoll start" lines; card146 block lines for Applied rows].
5. **Re-holds.** The game's own SetAllBodiesSimulatePhysics(1) on a held copy is turned off in the same call and
   counted ["card140: client pose own SetAllBodiesSimulatePhysics(1) ... card152 held copy held again"; "held again"
   in the client map line].
6. **WARNs.** 0 new WARN classes on either side; only the baseline WARNs from c183-r3.
7. **Table peaks.** The table peaks are logged: kPoseRows 128 in the card132 journal line, and the wait table
   ["card152: client ragdoll wait table map totals ... peak N of 32"].

Plus, from the senior's conditions:

- **0 fallbacks.** ["... wait table map totals ... 0 fallbacks"; no "WARN card152: client no stream row" lines]. A
  fallback names its class. If that class is card 177's (it never ragdolls on the host), the fix is left to card
  177.
- **The (a) check lines.**
  - Three rows are picked: "card152: client bone check picked guid".
  - Each logs up to 8 "card152: client bone check guid ... bone 0 (...) ... is X uu from body 0; body 0 moved M uu
    since the hold". These are taken half a step after each sample, before that tick's move.
  - The prediction is X = 0.0 uu with M > 0 (the capped lines).
  - X > kPoseFreezeTolerance with M > 0 means the bones kept the old pose. The card then stops and reports.
- **The digest.** The second card132 round line reads "0 simulating and not streamed" for streamed corpses. sim_skipped
  in the map line counts only rows that were never streamed: late-join replay rows and corpses with no NetGUID.
- **The late join (finding (d)).** The rejoined client gets only replay rows, with no "card152: client stream" lines
  for those guids.
- **No dead time.** No step waits past its line except the spawn replication waits named above.

Digest note: the simulating-row compare is in `c132_pose_check` in pose_c132.cpp. digest_c132.cpp only calls it, so
digest_c132.cpp is unchanged.

## Run c152-r1 result (2026-11-11)

Logs: `logs/c152-r1-host.log`, `logs/c152-r1-client.log`, `logs/c152-r1.drive`.

- **(a) failed.** All 24 bone check lines are WARN. Bone 0 stayed at the hold pose while body 0 was set to the
  samples: guid 0xda 77.5 to 105.4 uu, 0xee 364.9 to 534.5 uu, 0xe4 974.9 to 1018.9 uu, each with "simulating 0".
  The distance grows with "body 0 moved ... since the hold". A held, non-simulating copy shows its bones at the hold
  pose, so the kinematic hold mechanism is dropped (r2 below).
- **One fallback:** "client no stream row for guid 0xe4 (BP_AI_WarriorBug_C) 500 ms after its game ragdoll start was
  held". Its stream row came later. The 500 ms wait was too short.
- **Part 2 (nuke):** the four-corpse wait timed out (TRY-FAIL lines 45 and 46). No nuke kill was streamed, so the
  moving peak was not reached.
- **No map totals lines.** The run ended with a quit at the field, and the stream and wait totals are logged at map
  arm only. r2 also logs them on the LoadMap enter path and ends with a travel.

### The senior's cause line was wrong; the junior's disassembly stands

The senior's first cause line for (a) did not match the binary. The senior accepted the junior's disassembly below.
The bones of a skeletal mesh take their pose from the physics bodies only in the end-of-physics tick:

- End-physics `ExecuteTick` at exe+0x41874F0 runs `BlendInPhysicsInternal` (exe+0x3FEA4F0).
- The tick is registered by `RegisterEndPhysicsTick` (exe+0x3AEF5D0) from `UpdateEndPhysicsTickRegisteredState`
  (exe+0x3AFD900). It is registered when the pointer at mesh+0xD70 is non-null and its first byte has bit 0 set. The
  tick function is at mesh+0xD50.
- The blend runs only when `ShouldBlendPhysicsBones` (exe+0x4014F40, vtable slot 0xC80) is true. That needs
  Bodies > 0, collision 2 or 3 (physics), and either bBlendPhysics or a body blend weight above 0.
- `SetEnableGravity` on USkeletalMeshComponent is at rva 0x04198DB0 (offsets.generated.h).

Two causes for r1's frozen bones remain open:

- (i) the end tick is not registered, or does not run, on a copy that does not simulate;
- (ii) the copy's collision is not physics.

The static trace below makes (ii) unlikely for copies whose EnableRagdoll ran. r1's fallback copy already logged
"collision 3 profile Ragdoll" before its own start. r2's bone check line reads the end tick registration,
ShouldBlendPhysicsBones, collision and the simulating flag on every line, which separates the two.

### Static trace of the blocked game ragdoll start

The client's held start is the call at exe+0x37C53CB. That is the return address inside
`UPrimitiveComponent::execSetSimulatePhysics`, the blueprint thunk that calls vtable slot 0x6D8. The caller is the
blueprint BP_AI_AttackSystemEnemy_Base (its ubergraph, decompiled for this card).

EnableRagdoll (ubergraph 3792), in order:

1. IsAbleToRagdoll.
2. RagdollRequested = false.
3. Mesh.SetCollisionProfileName(Ragdoll, true). This runs before the held call, so a held copy already has the
   Ragdoll profile (collision 3).
4. Mesh.SetSimulatePhysics(true). This is the only call the hold blocks.
5. BlendHeadChildren.
6. CapsuleComponent.SetCollisionEnabled(0).
7. If not RagdollEnabled: Wake Ragdoll, then ApplySavedRagdollPose.
8. RagdollEnabled = true.

Wake Ragdoll (ubergraph 1178), in order:

1. IsAbleToRagdoll.
2. SetCollisionProfileName(Ragdoll).
3. SetComponentTickEnabled(true).
4. SetAllBodiesSimulatePhysics(true). This is finding 11's call, which our sim_change hook sees.
5. WakeAllRigidBodies.
6. BlendHeadChildren.
7. SetEnableGravity(true). This runs after our sim_change hook returns, so r2 sets gravity off again on every
   stream tick, not only in the hook.
8. bIsSkeletonFrozen = false, RagdollTimeRemaining = Freeze Time, then CheckToFreezeRagdoll (the SleepRagdoll timer).

Other paths in the same graph:

- At 371: SetEnableGravity(false), then Wake Ragdoll.
- At 441: profile Ragdoll, tick on, gravity true.
- DisableRagdoll (4450): SetSimulatePhysics(false), profile Corpse, capsule collision 3.

## Run c152-r2 (written before the run, 2026-11-11)

### Mechanism

A streamed copy simulates with gravity off. The bones then come from the bodies through the engine's normal
simulating path, which is the path the end-physics blend is built for.

- **Stream start.** `pose_stream_start` runs on the first stream sample or unfreeze row of a copy. It goes through
  the existing card 039/132 start, `detour_c132_set_simulate(mesh, true)`. There is no second start path. A copy that
  still does not simulate gets one WARN per row with its collision value ("copy would not simulate"), and is counted.
  Then `pose_grav_off` turns gravity off.
- **Every tick** (`pose_stream_tick`): the bodies are teleported to the lerp target between the last two samples with
  the wake flag set. Then linear and angular velocity are zeroed, and gravity is set off again. The re-set is
  uncounted; it is needed because Wake Ragdoll turns gravity on after its SetAllBodiesSimulatePhysics(1).
- **Gravity back on.** `pose_grav_on` runs at the final row (settled, frozen or unsettled; then the existing snap path
  poses the copy, and a frozen one is frozen by the card 140 path), at the wait fallback, at row removal, and when a
  new copy replaces the old one (counted as "gone"). Off and on calls are counted. The map line is a WARN when the off
  count exceeds on + gone by more than the streaming copies still present.
- **Finding 11.** The game's own SetAllBodiesSimulatePhysics(1) on a streaming copy is now wanted. The client hook only
  sets gravity off again and counts it ("gravity off again"). A card 140 frozen copy turned back on is still held
  again, with its own count and log text.
- **Removed.** The stream-time blend and SkipAllBones hold is gone from the stream path. The card 140 freeze hold
  (pose_hold, pose_hold_bits, pose_release) stays for frozen finals.
- **Unfreeze rows** continue the stream. They are not finals.
- **Wait.** kPoseStartWaitMs is 2000 ms. Each resolved wait logs its ms, and the map line gives the slowest.
- **Totals.** The stream and wait map lines are logged once a map, from `c152_pose_wait_clear_all`, which runs on the
  LoadMap enter path and at map arm, whichever comes first.

Deviation from the file list: hooks_ubw.cpp gains one resolver row in kCallsUbw (`SetEnableGravity` into
`g_pose_mesh_gravity`). The ordered gravity calls need it to resolve the symbol.

### Scenario

`tools/scenarios/c152-r2.txt`. Part 1 is r1's part 1, waiting on "stream start ... simulating, gravity off". Part 2a
replaces the nuke with rifle kills of a group: four WarriorBugs, the host fires the MoritaMk1
(`UBWCOOP_GRANT_WEAPON=MoritaMk1`). Part 2b is six kills in quick succession, for the moving peak. The cap of 24
samples per step cannot be reached in FAST=1 on this field; that is left to card 127. Part 3 is r1's late join.
Part 4 travels to the next map, so the LoadMap enter path logs the totals.

The digest has no on-demand trigger: its rounds run every 30 s. The run cannot place a round inside a live stream. If
a round falls during one, the card132 round line shows it; otherwise the report says that none did.

### Predictions

1. **Bone check, every line** ("card152: client bone check guid ... (live)"):
   - bone 0 within 1.0 uu of body 0, on 3 or more copies (the worst is in the map line);
   - body 0 within 10 uu of the last target set (otherwise a WARN with collision);
   - "simulating 1", "collision 3", "end tick registered 1", "ShouldBlendPhysicsBones 1".
2. **Stream starts:** 0 "copy would not simulate".
3. **Gravity line:** off = on + gone + the streaming copies still present. No WARN.
4. **Finals** equal the host's settled + frozen + unsettled count, which is host snaps minus unfreezes.
5. **Freeze check after a frozen final:** 0.0 uu, and its "frozen final" bone check line within 1.0 uu.
6. **Wait:** 0 fallbacks at 2000 ms. The slowest resolved wait is logged.
7. **Totals:** both sides' stream map lines appear at the travel.
8. **WARNs:** 0 new WARN classes besides the ones named under "Outside the goal".

### Outside the goal

These come from the FED_C2M1 runs and are not card 152's:

- digest mismatches on BP_AISpawner_C, BP_AISpawnPoint_C, BP_Dropship_LandingBlocker_C and NiagaraActor_14;
- the card140 318 uu client hit.

## Run c152-r2 result

DLL fb48f052114e5c00. Logs: `logs/c152-r2-host.log`, `logs/c152-r2-client.log`, `logs/c152-r2.drive`. The
scenario passed (exit 0) with three TRY-FAILs, all scenario artifacts (below). The map lines logged twice on the
client: at the reconnect LoadMap and at the travel. Starts, finals, unfreezes and the gravity counts are cumulative
across the two lines; samples, rx and wait blocks are reset by each line.

### The goal: met while the copy simulates

Every "(live)" bone check line on a simulating copy reads bone 0 at 0.0 uu from body 0, with "simulating 1",
"collision 3", "end tick registered 1" and "ShouldBlendPhysicsBones 1". Six copies show it: 0x10c, 0x116, 0x122,
0x1da, 0xa8 and 0xee. r1's gap (77.5 to 1018.9 uu with simulating 0) is gone on every simulating copy.

### Per prediction

1. **Bone check:** pass on simulating copies (above). Body 0 is within 10 uu of its target on every line except
   0xa8 sample 0 (17.6 uu, the one target WARN). That one is a logging artifact: `pose_stream_first` sets `target0`
   from the body before `pose_apply_bodies` moves it. Fail on one line: 0x1da sample 7 reads "simulating 0" and bone
   0 13.4 uu from body 0 while the stream continues (problem 1).
2. **Stream starts:** pass. 71 starts, 48 already simulating, 0 would not simulate.
3. **Gravity line:** pass. 71 off = 66 on + 2 gone + 3 still here. No WARN.
4. **Finals vs host:** not proven. The client logs 86 finals (cumulative). The host has 733 snaps and 577
   unfreezes (156), but the host count includes corpses sent before the client joined and during its disconnect, and
   the host's card132 snap lines hit their cap at 95671707 ms. A per-guid compare is needed; the logs cannot give it.
5. **Freeze check after a frozen final:** half pass. 45 freezes, worst 0.0 uu one tick after, 0 over 2 uu. The
   "frozen final" bone lines fail: 0 to 25.4 uu (0x1da 25.4, 0xa8 8.9, 0x10c 6.0 with simulating 0) (problem 2).
6. **Wait:** pass. 13 held, 13 cleared, 0 fallbacks, slowest 1635 ms (665 ms in the first segment), peak 1 of 32.
7. **Totals:** pass. Host: 550 samples, 244753 bytes, peak 7 moving at once, peak 7 per step (cap 24), 0 cap hits,
   1 backlog skip, backlog peak 19 frames. Client lines as quoted above, at both LoadMap paths.
8. **WARNs:** fail, one new class. Host "card140: host round" ×4 (2 rows, 2 summary lines): rows 0x102 and 0x418,
   "unsettled flag 1", body 172 and 136 uu from the last sent snap. These are streaming rows: the host self-check
   compares the live corpse to the last snap, but a streaming row's motion now goes out as samples (problem 3).
   Host "card046 FindTeleportSpot found no room" ×1 is the late-join spawn, not card 152's. Client "card140 client
   hit" ×3 (367 and 401 uu) is the class named under "Outside the goal". Client "card152 bone check" ×25 is problems 1
   and 2. Client "card152 map totals" ×2 follows from them.

Digest: no round fell inside a live stream; the final client line leaves 7 simulating rows out. 0 mismatches in 36
checks.

### TRY-FAILs (scenario artifacts)

- **Line 70** (six quick kills, count 6): only one host stream (0xbc). The host logs one HandleDeathEvent (call 11
  at 95680438 ms) for the six InstaKillTarget calls 110 ms apart. Kills 29 to 33 most likely hit the same dying bug:
  its health set does not read 0 that soon, so `c64_find_actor`'s live filter still picks it. The kill line prints
  the class twice, so the log cannot tell the targets apart. Scenario fix: wait for each death line between kills.
- **Line 82** (snap 1, count 2): the card132 pose line cap was hit at 95671707 ms, before part 3.
- **Line 84** (client pose apply, `new`): the replay applied at 95708079 ms, before the `new` wait began.

### New problems

1. **The game freezes streaming copies itself.** 57 "own SetAllBodiesSimulatePhysics(0)" calls on the client,
   some on streaming rows (0xee, 0x116, 0x1da). After one, the stream keeps setting body targets on a copy that no
   longer simulates, so the bones stop following (0x1da sample 7). Likely source: the Wake Ragdoll
   CheckToFreezeRagdoll / SleepRagdoll timer. Proposed fix, the finding 11 sibling: in `c132_pose_sim_change`, a
   simulate=false on a `grav_off` row starts simulation again through `detour_c132_set_simulate` and turns gravity
   off again, counted in the map line.
2. **"Frozen final" bone gap.** After the final pose is set and the copy frozen, the bones are not blended again, so
   they keep the last streamed blend. The label is also wrong: `pose_freeze_check` runs for every `check_owed`
   (settled finals and repeat freezes), so many "frozen final" lines are simulating 1. Proposed fix: on a frozen
   final, keep the copy simulating with gravity off and zero velocity for one blended tick, then freeze; and label the
   line by the final's kind.
3. **Host self-check on streaming rows.** `c140_pose_host_check` checks a row whose motion is being streamed. Proposed
   fix: skip a row with a live stream, as the client's digest already does, and count the skips in the round line.

Also: set `r.target0` from blob body 0 after the apply in `pose_stream_apply` (prediction 1 artifact).

## Run c152-r3 (written before the run, 2026-11-11)

DLL e8df0659744e9491. Scenario `tools/scenarios/c152-r3.txt`. This is the last run on the r2 mechanism; it fixes
r2's three new problems and its artifacts. Nothing else changes.

### Changes

1. **Game freeze on a streaming copy is blocked.** The game's freeze of a client copy is
   `SetAllBodiesSimulatePhysics(0)` from exe+0x3B05087 (a blueprint native call). It goes through
   `detour_c132_all_bodies` (enemies.cpp), not `SetSimulatePhysics`: r2 logged 57 "own
   SetAllBodiesSimulatePhysics(0)" lines and 0 "client simulate off" lines. So the block sits in
   `detour_c132_all_bodies`, not in `detour_c132_set_simulate` as the order named. On the client, a simulate=false
   call from the game (return address inside the exe) on a row with `grav_off` set, in state Applied or Freezing,
   returns before the original. It is counted in "game freezes blocked on a streaming copy" in the client map line;
   the first 16 per map are logged with the guid, the sample, live or held final, and the caller rva. The mod's own
   freezes call `g_orig_c132_all_bodies` directly and are not affected.
   **Skipping the original:** `USkeletalMeshComponent::SetAllBodiesSimulatePhysics` only sets each body
   instance's simulate flag and the physics state that follows from it; it allocates and frees nothing and has no
   return value. Skipping it leaves the copy simulating, which is the state the stream needs. The copy is frozen
   later by the mod's own path: the final row's freeze (`pose_freeze_step`) for a frozen final, or the card 140
   freeze path. The game's own timer (Wake Ragdoll CheckToFreezeRagdoll / SleepRagdoll) calls it again later, after
   the row is no longer streaming, and that call then goes through.
2. **Frozen final: one held tick.** r2's failing frozen-final lines all read "end tick registered 1,
   ShouldBlendPhysicsBones 1" with "simulating 0": 0x1da (25.4 uu), 0xa8 (8.9 uu), 0x10c (6.0 and 3.7 uu), 0xee
   (4.5 uu), 0x122 (1.8 uu). The blend was registered, but it does not run on a copy that no longer simulates, so the
   bones kept the last streamed blend (0x1da's bone 0 stayed at (230178, 39360, 8682) from sample 7 to the final).
   In r3, the final row is posed by the snap path while the copy still simulates with gravity off, and its linear
   and angular velocities are zeroed (`pose_stream_final`). Gravity stays off. A frozen final is frozen at the next
   client step (`pose_freeze_step`); gravity goes back on right after that freeze. A settled or unsettled final gets
   gravity back on after its bone check (`pose_freeze_check`). The bone check line is labelled by the final's kind:
   "frozen final", "unsettled final" or "settled final". In r2 every check was labelled "frozen final", including
   settled ones that were still simulating (up to 4.8 uu, 0xa8).
3. **Host self-check skips streaming rows.** `c140_pose_host_check` skips a row in state Sent whose last snap is a
   stream unfreeze (`stream_seq != 0` and the unfreeze flag). The client continues the stream on such a row
   (`pose_stream_apply`), so the last snap is not the client's end state. Both r2 WARNs were rows like this: 0x102
   (172 uu, right after sample 0) and 0x418 (136 uu, unsettled flag 1). The skips are counted in the host map line
   ("round self-check rows skipped streaming").
4. **target0** is set from blob body 0 after `pose_apply_bodies` in `pose_stream_apply`, not from the body before
   the apply (r2's 0xa8 sample 0, 17.6 uu).
5. **Finals are countable.**
   - Both sides' card 152 counters reset when their map line logs (`pose_stream_reset`). The gravity counters
     carry over, because a copy turned off in one window can be turned on in the next.
   - The host map line gains "stream finals sent (per PC)". A host snap counts as a stream final when its row has
     streamed (`stream_seq != 0`), it is not an unfreeze, and it was sent. It counts once per PC it went to. That is
     the same set the client counts: the client logs a final for a row it streamed (`r.streamed`) and then gets a
     non-unfreeze row for.
   - **The window compared.** The host logs its map line once for FED_C2M1, at the part 4 travel. The client logs
     two: one at the part 3 reconnect LoadMap (parts 1 to 2b), one at the travel (part 3 to the travel). The client
     is not connected for the whole host window. During its disconnect, host snaps go to no PC and count 0, and the
     replay to the rejoined PC does not go through `pose_snapshot`. So the comparison is: host "stream finals sent"
     for FED_C2M1 equals the sum of the client's finals on its two lines.
   - kPoseStreamLines is raised from 60 to 120. Both sides hit 60 in r2 (the client at 95685494 ms, during part 2b).
6. **Scenario.** Each kill in part 1 and part 2b waits for the next host "stream guid ... sample 0" line, with a
   rising count from one mark, before the next kill. r2's six kills 110 ms apart gave one death. The order named the
   death line, but the card038 game-logic lines are capped at 32 a map and r2 used all 32 by the end; at about 2
   lines a death, part 2b's sixth death lands at line 31 or 32. The sample 0 line (cap now 120; r2 used 60 for 31
   corpses) is also a better wait: it proves that a new corpse streams. The r2 line 82 wait (snap 1 on a capped line)
   is removed. The part 3 apply wait is marked with `client mark` right after `client disconnect`, so an apply that
   lands before the wait begins is still matched (r2 line 84 used `new`).
   **Risk:** if a kill picks a dying bug whose health does not yet read 0 (the r2 explanation for 1 death in 6), its
   wait times out at 8 s and the run reports it. The wait between kills should give the health time to drop.

### Predictions

1. **Freeze block:** 0 client bone check lines with "simulating 0" while the stream is live (labels "(live)").
   "game freezes blocked on a streaming copy" > 0 in a client map line, and its first lines are logged with the
   caller exe+0x3B05087.
2. **Frozen final:** bone 0 within 1.0 uu of body 0 on every final bone check line (frozen, settled and unsettled).
   The freeze check one tick after a frozen final reads 0.0 uu body error.
3. **Host self-check:** 0 "card140: host round" WARNs. The skip count is in the host map line.
4. **target0:** no "body 0 to target" value over 10 uu, so no card152 target WARN.
5. **Finals:** host "stream finals sent" for FED_C2M1 equals the client's finals on its reconnect line plus its
   travel line.
6. **Scenario:** 0 TRY-FAIL and no wait that times out.
7. **Unchanged:**
   - 0 wait fallbacks and 0 "copy would not simulate";
   - the gravity line balances (off = on + gone + the streaming copies still present), no WARN;
   - peaks logged: moving at once, per step, backlog.
   - The digest during a live stream is still unproven unless a 30 s round falls inside one; the report says
     whether one did.
8. **WARNs:** host classes are attach and card046 (the late-join spawn); the card140 host round class from r2 is
   gone. Client classes are attach and the card140 client hit class named under "Outside the goal"; card152 bone
   check and card152 map totals from r2 are gone.

## Run c152-r3 result

DLL e8df0659744e9491. Logs: `logs/c152-r3-host.log`, `logs/c152-r3-client.log`, `logs/c152-r3.drive`. The
scenario passed (exit 0, 117 s) with 0 TRY-FAILs. No crash dumps. Host 9722 lines, 3 WARN. Client 9671 lines, 36
WARN. The client logged its map lines twice: at the part 3 disconnect (97060944 ms, parts 1 to 2b) and at the travel
(97093634 ms, part 3 to the travel). The host logged once, at the travel.

### Per prediction

1. **Freeze block: pass.** 0 "(live)" bone check lines with "simulating 0" (24 live lines, all at 0.0 uu). 97 game
   freezes blocked in the first client window, all from caller exe+0x3B05087. Example: guid 0x10c sample 23 at
   97021372 ms.
2. **Frozen final: fail.**
   - Bone checks on finals: 0x10c frozen 0.0 uu, 0x10c settled 0.0, 0x120 settled 0.0. 0x102 frozen final
     (sample 24, 97018753 ms) reads bone 0 at (228907, 39581, 8588), 413.7 uu from body 0. Body 0 is at the host
     root (228938, 39187, 8709); the last live bone line (sample 8) was at (228995, 39174, 8724). So the bone is not
     the last streamed blend (r2's failure). It is 380 uu from the mesh component (229208, 39351, 8618) and below
     the floor (8615). Cause not proven.
   - The freeze check one tick after a frozen final is not 0.0. 0x102 is 0.2 uu. 0xf8 snap 14, 0xee snap 3 and
     0x174 snap 2 are 2.6, 2.9 and 3.7 uu, all "after the freeze call", so the held tick moved the bodies.
   - **Regression (new WARN class):** "card140 client pose freeze check" ×23, plus its map line ("12 freezes, worst
     4.0 uu, 23 over 2 uu"). r2 had 45 freezes and 0 over 2 uu. 19 of the 23 are settled finals with "simulating 1"
     (0xf8 ×8, 0x16a ×5, 0x174 ×2, 0xee ×2 and others). Each reads 0.0 uu after the apply and 2.5 to 4.0 uu one tick
     later, on a leg body (14 to 18). The frozen one, 0xf8 snap 14, logged "gravity on", so gravity is not the
     difference. The new step is the velocity zeroing in `pose_stream_final`: SetPhysicsLinearVelocity and
     SetPhysicsAngularVelocity wake the bodies. In r2 the teleported pose was not woken, and nothing moved in the
     tick after. Likely cause; not isolated by this run. Also, r3 holds settled and unsettled finals with gravity off
     over that tick, which the order did not ask for (it named frozen finals only).
3. **Host self-check: pass.** 0 "card140: host round" WARNs; 7 streaming rows skipped. Round 2 checked 5 sent corpses,
   0 moved over 100 uu.
4. **target0: pass.** Body 0 worst 9.3 uu from its target, 0 over 10, no target WARN.
5. **Finals: fail by 5.** Host: 71 stream finals sent for FED_C2M1. Client: 66 finals in window 1 and 0 in window 2,
   66 in all. Two candidate causes, neither provable from these logs:
   - The client line logged at 97060944 ms, but the host dropped the PC 119 ms later (97061063 ms, card049 "left").
     5 copies were still streaming at the client line, and a host final sent in that gap counts on the host only.
   - A final can be superseded by the next unfreeze row before the client applies it. Rows on 0xf8, 0x120, 0x16a and
     0x1d4 switch between final and unfreeze many times (up to 62 snaps on one guid).

   The client stream lines hit their cap of 120 at 97057713 ms (57 final lines logged out of 66 counted), so no
   per-guid compare is possible.
6. **Scenario: pass.** 0 TRY-FAIL, no wait timed out, run 117 s. Each per-kill sample 0 wait matched a new guid (part
   1: 0x120, 0x102, 0x10c; part 2b up to 0x134). One weak wait remains: part 2b's client "stream final count=6"
   matched at 97043281 ms on 0x120 snap 34, a re-final of an older corpse, before the 2b kills streamed.
7. **Unchanged:**
   - **Fallbacks: pass in window 1, fail in window 2.** Window 1 has 0 fallbacks (4 held, slowest 754 ms, peak 1 of
     32) and 0 "would not simulate". Window 2 has 2 fallbacks: part 3 corpses 0x160 and 0x226, killed while the
     client was away. The client rejoined 8.5 s after the drop, while they still moved. The game's start was held
     for a stream row that the host does not send to a rejoined PC (end state only). Then "card152 no stream row" ×2
     and "card137 corpse arrival not posed after 9004 ms" ×2 followed, plus the card137 map line. The travel came 4 s
     after the replay apply, before those corpses settled.
   - **Gravity: pass.** 64 off = 58 on + 1 gone + 5 still streaming, no WARN.
   - **Peaks: logged.** Host: 205 samples, 91216 bytes, peak 3 moving, 3 per step (cap 24), 0 cap hits, 4 backlog
     skips, backlog peak 79 frames (r2: 1 skip, 19 frames).
   - **Digest: proven during a live stream.** Client round 2 (97058895 ms) compared 12 streamed rows against their
     last sample: 0 mismatches over 30302 actors, 0 simulating rows left out.
8. **WARNs vs baseline** (r2 host: attach, card046, card140 host round; r2 client: attach, card140 client hit ×3,
   card152 bone ×25, card152 map ×2):
   - Host: attach ×1. card140 host round is gone. card046 is absent. New: "card132 OnHit of ADProjectile, whose
     GrenadeExplode already went" ×2 (97025119 ms, part 2a, a hit on Landscape); not card 152's.
   - Client: attach ×1, card140 client hit ×3 (unchanged, outside the goal), card152 bone ×1 (0x102), and card152
     pose stream map ×1 (from it). New: card140 pose freeze check ×23 + map ×1 (the regression in 2). Also new
     against r2: card152 no stream row ×2, card152 ragdoll wait table ×1 and card137 corpse arrival ×3 (the late
     join in 7). r1 had the no stream and ragdoll wait classes.

### Problems for the senior

1. **Held tick wakes the copy.** The velocity zeroing on the final wakes the bodies, so they move 2.5 to 4.0 uu in the
   tick after a correct pose. Proposed fix: drop the velocity zeroing, and keep gravity on for settled and unsettled
   finals (r2 behaviour). Keep only the frozen final's held tick with gravity off, if the bone gap still needs it.
2. **0x102 frozen-final bone 413.7 uu.** The bone pose is neither the stream blend nor the posed bodies. One case
   only; it needs the bone-space read (component vs world) at the final.
3. **Finals count not provable.** The client stream line cap was hit again. A host-side per-guid finals count is
   needed, or the compare must use a window that ends after the host's PC drop.
4. **Late join of a moving corpse.** The client holds the game's start for a stream it will never get. Candidate:
   do not hold the start for a copy whose host row came only through the replay. This is the late-join class from r1.

## Senior review (2026-11-11)

Accepted for its goal: a falling or re-woken corpse follows the host's stream. Diff read in full against the
pre-card snapshot; rebuild gave "ninja: no work to do."; DLL `e8df0659744e9491`. The `enemies.cpp` block in
`detour_c132_all_bodies` is outside the allowed file list and is accepted: the game's client freeze has no other
entry point.

Proven in c152-r2 and r3: live bone check 0.0 uu on simulating copies (24 live lines in r3), 0 simulating-0 lines
during a stream (97 game freezes blocked), host self-check WARN gone, body 0 at most 9.3 uu from its target, digest
compare during a live stream with 0 mismatches over 12 streamed rows, window 1 with 0 fallbacks.

Not proven, carded (the three-run budget is used):
- Final rows: new client WARN class "card140 pose freeze check" x23, frozen final of 0x102 with bone 413.7 uu off,
  final/unfreeze flapping, finals 71 against 66. Card 186.
- A client joining mid-stream: 2 fallbacks (0x160, 0x226). Card 187.
- New host WARN class "card132 OnHit of ADProjectile, whose GrenadeExplode already went" x2. Card 188.
- `pose_part` grew from 56 to 64 lines (rule: 60). Fixed in card 186.
- `kPoseStreamPerStep` 24 was never reached (peak 3). Checked in the soak (card 127).
