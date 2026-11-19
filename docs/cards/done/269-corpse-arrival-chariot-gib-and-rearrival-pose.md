# 269 Client corpse arrival: gibbed ChariotBug corpses wait forever, re-arrived WarriorBug corpses keep a stale pose

Found by card 243 run 9 (`logs/c243-c3m2-r9-*`, FED_C3M2, DLL 75d9c5ce204aa049). Rows are quoted here
because the next run of that tag overwrites the logs. Two faults in one class, the client corpse arrival
path (`c132_pose_arrival`, `arrival_apply`, `arrival_tick` in `mod/src/ubw/pose_c132.cpp`, near line 2370;
the corpse adoption in `mod/src/ubw/enemies.cpp`, about lines 575 to 640, `c37_pawn`, `kC37SeenLiveMs`).

## Evidence A: ChariotBug corpses never get a host pose row

- 20 client lines of this shape:
  `WARN card137: client corpse arrival BP_AI_ChariotBug_C guid X not posed after ~9000 ms ... (no host pose
  row yet; Pong at 221114966) ... the death visual played`.
  - 0x27c at t=221203742. It arrived at map start with 0x2a6 and 0x2ae (221194740 to 221195242, Health
    -726, -593, -501, `card131 corpse first seen dead`).
  - 0x564, 0x55a, 0x550 at t=221436206.
  - 0x9c6 at t=222087186 (host death at 222000791), and again at t=222200934.
  - 0xa66 and 0xa7a at about t=222096000.
- The host log has 0 pose rows for BP_AI_ChariotBug_C. Host pose rows exist only for BP_AI_WarriorBug_C.
- ChariotBugs gib on overkill: `card042: host Gib BP_AI_ChariotBug_C ... by cell guid 0x0 cell 11204 sent to
  1 PCs`. The gib row carries guid 0x0, so the client cannot tie the gib to the corpse it is waiting on.
  Also: `card042: client Gib guid 0x32c cell 12730: guid route BP_AI_WarriorBug_C, cell route
  BP_AI_ChariotBug_C (routes differ)`.
- Cause: the client waits for a pose that the host will never send, because a gibbed corpse has no ragdoll
  bodies to pose. After `kArrivalWaitMs` it plays the death visual on a corpse the host has gibbed.

## Evidence B: re-arrived WarriorBug corpses keep their first snap

- Client `Pose` mismatch rounds (all classes): round 10 (t=221410531, 2), 13 (4), 15 (5), 16 (8), 17 (8),
  33 to 51 (1 to 2 each), 60 (t=222910900, 1).
- All mismatched guids are BP_AI_WarriorBug_C: 0x50c, 0x5ac, 0x616, 0x5f2, 0x59a, 0x642, 0x630, 0x64a,
  0x5ea, 0x660. No chariot.
- They arrive in one burst, client t=221398037 to 221398265 (`card137: client corpse arrival ... posed 0 ms
  after it arrived ... 0 uu from the host's root`). This is right after
  `pawn-collision: client PostInitializeComponents(... BP_PlayerCharacterMI_C, LocalRole 1 ...)` at
  t=221397989: the host's pawn came back into relevancy, and the corpses near it (about -57000, 32000)
  came with it.
  - Example: 0x50c died on the host at t=221301082. The client ragdoll started at t=221398038, 97 s later.
- The host then sends newer pose data:
  - `card152` stream sample 0 at t=221397983, just before the arrival.
  - `card146: client blocked the game's ragdoll start on host-posed guid 0x5ac (snap 1, simulating)` at
    t=221399567.
  - `card137: host pose unfreeze guid 0x5ac snap 2 sent at the game's SetAllBodiesSimulatePhysics(1)` at
    t=221411646.
  - Round 10 has 0x5ac 520 uu off with "simulating 0".
- Cause: arrival applies the snap it holds and marks the row applied. Later snaps and stream samples do not
  land on the re-arrived copy.
- Not linked to frame time: the 7696 ms host frame window is at t=224342741, after the last mismatch
  round.

## Step 1 (static, no run)

1. Host death path: name the point where the host knows a corpse was gibbed or has no physics bodies to
   pose. Check whether card042 `Gib` can carry the dead pawn's guid there.
2. Client: trace `arrival_apply` and `arrival_tick` for a guid whose row is already applied. A newer snap
   (card137 unfreeze) or stream sample (card152) arrives: which branch drops it?
3. Host: does a re-relevancy event (actor channel open for a corpse that already has a pose row) resend the
   latest snap? If not, name the hook point.

## Step 1 answers (static plus r9 logs, 2026-11-13)

### A: the cause is not gibbing

- ChariotBug corpses die by the death animation and never ragdoll on the host. In r9 the host has 63
  `card132: host ragdoll start` rows, all `BP_AI_WarriorBug_C`, and 0 for 56 chariot deaths.
- `c132_pose_track` (pose_c132.cpp, about line 2446) opens a host pose row only at ragdoll start (called from
  enemies.cpp, about line 2695). A chariot therefore never has a pose row, and every chariot arrival waits
  `kArrivalWaitMs` for a row that never comes. In r9 that was 17 of 17 chariot arrivals; 43 of 43 warrior and 2 of 2
  hopper arrivals were posed.
- Example: guid 0x9c6 died on the host at t=222000791 with no `Gib` row. The client WARN fired 9 s later,
  then `c38_corpse_death_play` replayed the death.
- Of 9 chariot `Gib` rows, 5 carried a guid and 4 went by cell (guid 0 from `c41_guid`, enemies.cpp about line
  3018, when the actor has no NetGUID in the first PC's package map).
- The client death edge cannot tell the two kinds apart: in c240-c1m1-r8 chariots, warriors and Morita troopers
  all start with `TryPlayDeathAnimation`. Warriors ragdoll after it; chariots do not.
- Unknown, so no fix is written: the host-side point that knows "this death will never ragdoll". The death
  animation's end (or a timeout after the death row) is the candidate; it was not traced. The fix shape stays: the host
  sends a no-pose marker with the death row, and the arrival skips the wait.

### B: re-arrival

- `arrival_apply` (pose_c132.cpp, about line 1603) sets an Applied row back to Pending and applies the held blob.
  The blob is never stale on the client: every stream sample (`pose_stream_part`) and every snap
  (`pose_snap_part`) overwrites `r.blob`, whether or not the copy is relevant.
- Guid 0x50c on the host: ragdoll at t=221301700; unfreeze snaps 1 to 4 within 5 s; snap 6 at t=221342503;
  freeze snap 8 at t=221416527. So the host corpse was simulating for the whole 74 s window.
- Client: arrival at t=221398038 applied snap 7 "simulating 1, held 0". Round 10 (t=221410531) shows it 278 uu
  from its last stream sample. Snap 8 corrected it.
- So the re-arrived copy is applied through the stream path (row `streamed`, flags unfreeze), but no later
  sample corrects it.
- Unknown, so no fix is written: whether the host stopped streaming 0x50c (stream samples after sample 1 are not
  logged, `r.stream_seq < 2`) or the samples went to the client and did not chain (`chain` needs `grav_off` and
  Applied, pose_c132.cpp about line 1663). One host stream counter per guid in the next run decides it.

## Fix outline (class, no per-map code)

- A: the host marks each corpse's kind on death (gibbed, no physics bodies, pose to come) and sends it with
  the death row. Arrival does not wait for a pose on a gibbed corpse: the mesh stays hidden and no death
  visual plays. Card042 gib rows carry the guid.
- B: an applied arrival row takes newer snaps and stream samples, or the host resends the latest snap when
  the corpse becomes relevant again. One of the two, chosen in step 1.

## Proof (one run)

Same FED_C3M2 path as card 243. Expected: 0 `not posed after` WARNs for gibbed chariots, 0 `Pose`
mismatch rounds after a re-relevancy burst, gib rows with a non-zero guid. Prediction written before the
run. Budget: 1 run.

## Step 1 closed for A (static, senior, 2026-11-13)

- Death is driven by `FSM_Enemy_Dying` (Blueprint state machine, all NPC classes): `StartDyingState`, death
  animation, then the transition to Dead fires when `AnimatorComponent.IsPlayingAnimation(Death)` is false and
  `HasSavedRagdollPose` is false; it calls `ASTNonPlayableCharacterBase::StartDeadState` 0x51A8260 (state byte
  +0xAE8 = 4, then `ReceiveBaseStateChanged` 0x50CDB40 with old and new state).
- Ragdoll is a per-class Blueprint choice, so no native flag says "will never ragdoll": `BP_AI_WarriorBug`
  (`OnIsDying`, if falling) and `BP_AI_HopperBug` (landed) call `EnableRagdoll`; `BP_AI_JumperBug` overrides it
  empty; `BP_AI_ChariotBug`, `AphidBug`, `ArcherBug` never call it. `IsAllowedToRagdoll` 0x5196290 and
  `USTBugAnimPhysComponent::StartRagdoll` 0x51DF280 are reached from Blueprint only.
- Host point that knows: the Dead state reached with no pose row for the actor. `ReceiveBaseStateChanged` is not
  virtual and is called by all four state setters, so one detour sees every class.
- Client snap: `ASTNonPlayableCharacterBase::TryPlayAnimationAgumentedByTime(FName, float)` 0x51B0C90 (already in
  `tools/symbols.txt`, Blueprint is its only game caller). On a static-mesh animator the float is a start offset:
  `USTSkeletalAnimatorComponent::StartStaticMeshAnimation` 0x51060B0 sets the material time to world time minus
  the offset. `TryPlayDeathAnimation` 0x51B11E0 goes through `TryPlayAnimationSequence` (+0x2A), so the card 041
  detour already sees the death name on the host.

## Fix spec A (class: every 3D NPC that reaches Dead without a ragdoll)

- New symbol `ASTNonPlayableCharacterBase_ReceiveBaseStateChanged` 0x050CDB40, one hook row.
- Host, in `pose_c132.cpp` next to the card 177 sprite rows: on new state 4, not `IsNPC2D`, no pose row for the
  actor, not gibbed: add a rest row (guid, death name captured by the card 041 detour inside
  `TryPlayDeathAnimation`, death time) to a table of its own (peak logged), send it to every ponged PC and
  replay it to a joining or re-relevant PC as the sprite rows are. Row removed when the actor goes or revives.
  Log `card269: host rest row <class> guid 0x.. seq <name>, sent to N`.
- Client, `arrival_tick`: a guid with a rest row does not wait `kArrivalWaitMs`. The mesh is shown and the death
  name is played with `TryPlayAnimationAgumentedByTime(name, host age in seconds)`. Log `card269: client rest
  arrival <class> guid 0x.. seq <name> at <age> s, <ms> ms after it arrived`. No row after the wait: the old
  WARN stays.
- A live death (client saw the NPC alive) is unchanged.
- Card 042 gib rows with guid 0 (4 of 9 in r9) are a separate gap: the gib is sent before the actor has a
  NetGUID. Use the sprite rows' `owed` pattern (send when the guid exists). Same build, same file family
  (`enemies.cpp` `c41_guid` caller).
- Prediction: 0 `not posed after` WARN for ChariotBug; one `card269: client rest arrival` per chariot corpse
  arrival; receiver-side shot shows the corpse lying, not standing or replaying. Deciding row for the one thing
  only a run settles (an offset past the sequence end holds the last pose, not a loop or a T-pose): a read-back
  1 s after the play, `IsPlayingAnimation(Death)` 0 and base state 4, logged in the arrival row's follow-up.

## Fix spec B, step one (diagnostic only, same build)

- Host: per pose row, count stream samples sent and log the count at freeze and at each re-relevancy replay
  (`card269: host stream guid 0x.. samples N, last at t`). Client: count samples received and samples chained per
  guid, logged at freeze. Decides "host stopped streaming" against "samples did not chain"; the fix follows the
  row, no extra run.

## Open: the read-back prediction failed (senior, 2026-11-16, log `c243-c3m2-b6r2`)

Predicted `IsPlayingAnimation(Death)` 0 one second after the timed play; seen 1 on every arrival (13 `WARN
card269: client rest read-back ... IsPlayingAnimation 1, base state 4` for 12 arrivals, host age 9 to 60 s, far
past the sequence end). Two readings, not decided: (a) the dead copy's animator does not tick, so the player
stays on the sequence at the frame the age gave (last frame; right picture); (b) the player wraps or loops, so
the corpse replays its death. No receiver shot of a rest corpse exists. Deciding step, with the next FED_C3M2
soak and no run of its own: the read-back also logs the player's current frame, at 1 s and at 3 s (same frame
and the sequence's last: (a), drop the WARN; frame moves: (b), fix by stopping the player on the last frame),
plus one client shot aimed at a rest corpse.

## Read-back diagnostic (patch `c269_readback_patch.py`, 2026-11-16 11:42, for b127)

Evidence so far: 60 of 60 read-backs across all run logs are `BP_AI_ChariotBug_C seq Death: IsPlayingAnimation 1`;
in b125 all 18 came at the reconnect (host ages 12 to 33 s). One read cannot tell a held last frame from a loop.

Patch (pose_c132.cpp only): `RestRow.reads`; `rest_check_tick` reads twice (1 s after the play, then 2 s later) and
each row logs `read-back N ... animator static mode M, time T of L s` (animator at NPC+0xA30: mode byte +0x158,
time +0x14C, length +0x150). Map totals (`checks`, `still_playing`) count the first read only. `kRestWarnLines` 40.

Decision rule on the next reconnect run:
- (a) both reads show the same time, at the length: the pose is held; drop the WARN (IsPlayingAnimation is true
  for a held sequence) and close the item.
- (b) time moves or falls between the reads: the death loops or restarts; fix = stop the player on the last frame.
- static mode 1 with time 0 of 0: the corpse shows the static mesh; the sequence player is not what draws it, and
  the read-back must move to the static mesh animation instead.


## 2026-11-16: root cause of the upright corpses (run c423b128a, b128), fix for b129

The float of `TryPlayAnimationAgumentedByTime(name, t)` is not a start offset. The function passes it to the NPC's
`IADAnimatorInterface` slot 0, `StartAnimationSequenceFixedTimeCallback(name, t, ...)`, and
`UADSkeletalAnimatorComponent::StartAnimationSequenceFixedTime` (exe+0x4A24B90) starts the sequence at rate
`length / t` through vtable +0x4E8. `rest_arrival` passed the host age (15.4-21.8 s in the run), so each death was
stretched over that long: the corpse stood upright and sank over 20 s. This matches the read-backs (IsPlayingAnimation 1
at 1 s and 3 s, pose hash moving only on the two copies in view; the static time and length fields are not used in
skeletal mode, so "0.00 of 0.37 s" said nothing).

Fix (`c269_rest_play_patch.py`): play over `kRestPlayS` 0.05 s. The arrival row now prints `played over 0.05 s (host
age N s)`.

Prediction for the next C2M1 run: after the reconnect every read-back 1 prints `IsPlayingAnimation 0` (the death ended);
totals `still playing 0`; the client shot after the reconnect shows the chariot corpses lying down. If read-backs still
print 1, the mesh is not ticking on a copy out of view and the next step is a forced pose refresh.

Known side effect to watch: the death sequence's own notifies (sound) fire on each arrival, once.

## 2026-11-16 12:48: b130 result and cause (senior)

Prediction failed on b130 (`c423b130a`): every rest arrival played, 16 read-backs read IsPlayingAnimation 1 with one
pose hash, and the client shot shows no corpse. The junior's log lead: 26 arrivals read "(mesh already hidden)", each
after `card132: client static mesh ... ran show 1` on a copy whose health had not refreshed (so the card 132 refusal
did not fire).

Cause, from the exe: `SetShowingStaticMesh_Internal(show 0, immediate 0)` (exe+0x5102EB0) does not show the skeletal
mesh. It writes a frame count to animator +0x15C and calls `SetSkeletalMeshEnabled(false, true)`, which keeps
bHiddenInGame set. `USTSkeletalAnimatorComponent::TickComponent` (exe+0x5107F40) counts +0x15C down and, at 0, calls
`SetSkeletalMeshEnabled(true, true)` and `SetStaticMeshEnabled(false)`, then turns its own tick off. Static mode
(+0x158) already reads 0 during the count, which matches the read-backs. On a copy that arrives dead the count never
ends, so the skeletal mesh stays hidden and `arrival_show` (which only undoes its own hide) never shows it.

Fix (patch `c269_swap_back_patch.py`): `c269_swap_back(npc)` in enemies.cpp, called first in `c132_pose_arrival`. When
+0x158 is set or +0x15C is above 0 it calls the original with (show 0, immediate 1), clears +0x15C, and logs
`card269: client corpse arrival NAME guid 0x...: static mesh swap back run at once (showing static S, N frames of the
deferred swap left)`. The arrival then finds the mesh shown and hides and shows it itself as for any other corpse.
The swap's collision write (QueryOnly from exe+0x5103075) is already refused for a marked copy by
`c132_keep_ragdoll_collision`.

Prediction: on C2M1 after the reconnect, one swap-back row per arrival that read "(mesh already hidden)" on b130;
0 arrivals read "(mesh already hidden)"; the client shot at the corpse shows the body where the host shot does.
If arrivals still read hidden with no swap-back row, something else hides the mesh and the junior's read-back fields
(bHiddenInGame, bVisible, tick option) name it.

Not covered: a copy that dies live on the client during the frame count. No row shows it failing (the animator still
ticks then); the swap-back row count against live deaths would show it.

## Closed (2026-11-17 12:53)

The swap-back fix prediction is met on FED_C2M1 (`c423b182a`, b182; same in `c423b181a`): 27
`static mesh swap back run at once` rows, 0 arrivals read "(mesh already hidden)", 62 corpse arrivals, 0
`not posed after` rows (evidence A), 0 `client round N pose guid` rows and 19 digest rounds `mismatches 0`
(evidence B; 21 rounds in b181a). Shots `client-c423b182a-c269-chariot-corpse.png` and the host's show the corpse at
the same place on both screens.

Not covered, by design: a copy that dies live on the client during the swap frame count (no row shows it failing).

Reopens on: a `WARN card137 ... not posed after` row for a gibbed class, an arrival row reading "(mesh already
hidden)", or a `Pose:` digest mismatch after a relevancy return.
