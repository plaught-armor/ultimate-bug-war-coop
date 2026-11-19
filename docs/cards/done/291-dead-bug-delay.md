# 291 Dead bugs lag between host and client

Playtest 2026-11-13 (user plays as client): "there's a pretty big delay in dead bugs being sent from host to
client and back and forth".

## Trace (client log `UBWCoop-menu-20261113-184603.log`, FED_C2M1)

The death itself is not late. Host t = client t minus 173869236 ms.

| Step | Evidence | Delay |
|---|---|---|
| Host row to client | recv 2058 `sent t=82108822 applied t=255978069` | 11 ms |
| Client kill round trip | `HitClaim shot 4` sent 255985851; `rest row guid 0x106 seq Death_05` and `OnDeathCaused` 255985927 | 76 ms |
| Death edge after the row | row 255985927, `card038 death edge` 255985932 | 5 ms |

The body is late. After the death animation the game starts the copy's ragdoll; card 152 holds that start until
the host's pose stream row arrives (`pose_wait_gate`, `pose_c132.cpp:439`; fallback `kPoseStartWaitMs` 2000).

| Guid | Death row (ms) | Ragdoll held (ms) | Released after |
|---|---|---|---|
| 0x106 | 255985927 | 255986650 | 1616 ms |
| 0x346 | 256007706 | 256008597 | 565 ms |
| 0x322 | 256005950 | 256006668 | 646 ms |
| 0x14c | | 255995389 | 464 ms |

All 11 waits this map: 0, 8, 61, 85, 111, 188, 464, 565, 646, 1491, 1616 ms. For up to 1.6 s the dead bug stands
frozen in its last death-animation frame, then drops into the host's pose. Also 21
`blocked the game's ragdoll start on host-posed guid` rows, and sprite rows that arrive as updates with
`host age 16930 ms` and `18790 ms` (guid 0x124, 0x5a: a far bug's death sequence changed 17 to 19 s after its
death).

## Step 1 (static, junior, no run)

1. Host side: what starts the pose stream for a dead bug (the host's own ragdoll start), and why it can be 1.6 s
   later than the client's ragdoll start for the same death row. Candidates to confirm or reject from the code:
   the host's death animation is longer or starts later than the client's copy; the host starts the stream only at
   the next sample tick; the host's bug is not significant and delays its ragdoll.
2. The client's sample rate and the first-sample rule: does the first stream sample go out at the host's ragdoll
   start, or at the next period.
3. The two late sprite updates: what on the host changes a dead far bug's sequence 17 s after death.

Write the answers into this card before coding.

## Fix (direction; settle after step 1)

The client must not show a frozen body. Either the host sends the first stream sample in the same tick as its
ragdoll start and the death row carries the host's time to that start, so the client's copy reaches the end of its
death animation when the host's does; or the client's own ragdoll starts at once and the stream corrects it by
blending to the host's pose, never by a snap. Pick the one step 1 supports. The final resting pose stays the host's
(card 269 rest row). No workaround, no raised or lowered timeout as the fix.

- Rows: keep `card152: client ragdoll wait for guid <g> resolved after <ms> ms`; add the map total
  `card291: client ragdoll waits <n>, median <ms>, worst <ms>`.

## Sibling sweep (same class: client copy waits on a host row before it shows a death result)

| Item | Check |
|---|---|
| AI troopers (`BP_AI_Trooper_*`) death and ragdoll | same gate; wait times |
| Other bug classes (tanker, plasma, hopper, rippler, tiger) | same gate; any class with its own death path |
| Gib and overkill (card 039) | burst time against the death row |
| Destructible corpses (`BP_WarriorBug_Corpses_Destructible_*`, 223 `K` rows this map) | time from the host's hit to the client's change |
| Corpse removal and dead-set (card 140) | client removes a body when the host does |
| Client-killed bug the host has not confirmed | the bug keeps moving until the host's row; 76 ms measured, list only |

## Predictions (one run, after the playtest, batched)

- 20 warrior bug deaths in C2M1, half by the client: every `ragdoll wait` row at most 100 ms, or no wait rows
  if the client starts at once; zero `WARN card152: client no stream row`.
- Receiver-side screenshots at death +300 ms and +1000 ms: body falling or down, never upright in a frozen frame.
- Digest round after: 0 mismatches.

## Step 1 answers (static, junior, 2026-11-13)

1. **Host stream start.** The host's pose row and stream sample 0 start in `detour_c132_set_simulate`
   (`enemies.cpp:2677`), which calls `c132_pose_track`. That function sends sample 0 in the same call
   (`pose_c132.cpp`, "sample 0 goes out with the start"). The host's start is the game's own
   `SetSimulatePhysics(true)`. A host log (hb32) shows the chain: caller exe+0x37C53CB, then
   `BP_AI_AttackSystemEnemy_Base_C.ExecuteUbergraph` from `EnableRagdoll`, from `Received_Notify`. So the host's
   ragdoll starts at an **anim notify in the death animation**, not on the death event.
   - The client's copy reaches the same notify on its own copy of the death animation (card 038 plays
     `TryPlayDeathAnimation` on the death edge, 5 ms after the row). It is held at that notify.
   - The gap is therefore the difference between two independent animation clocks reaching one notify.
   - Host-side reasons its notify comes late:
     - The mesh is `SkeletalMeshComponentBudgeted`. The host's animation budget allocator throttles the tick of
       meshes that are far from or unseen by the host's camera, so the notify fires at a later throttled tick.
       The client sees the bug, so its copy ticks at full rate.
     - A host frame-rate dip delays the notify in the same way.
   - Rejected: the host starts the stream only at the next sample tick. Sample 0 goes in the start call.
   - Rejected: a different death animation. Card 269 rows carry the same sequence (`Death_05`) to the client.
2. **Sample rate and first-sample rule.** The host samples every `kPoseStepMs` = 250 ms. Sample 0 goes out
   inside the ragdoll start call. There is one exception: when the deepest pace queue holds more than
   `kPoseStreamBacklog` (8) frames, sample 0 is skipped (`st_backlog_skips`) and the first sample is the next
   step's, up to 250 ms later per skip. The first sample also queues behind up to 8 paced frames. In busy fights
   this adds a few hundred ms on top of item 1. It does not explain 1.6 s alone.
3. **Late sprite updates (guid 0x124, 0x5a; also 0x26 at 17557 ms).**
   - These are card 177 sprite rows (2D NPCs, `IsNPC2D`), not bug ragdolls.
   - The host sends an update from only two places:
     - `sprite_host_update` at the rest read, at `ms + 1000` after the death, which is about 1 to 2 s;
     - `c177_sprite_death_host` "at a second death start": `TryPlayDeathAnimation` ran again on a dead sprite NPC.
   - 17 to 19 s is the second path. The animator picked a new variant (`Death01` and `Death02` swap). Host frame 0
     of 4 on 0x124 matches a sequence that has just restarted.
   - `TryPlayDeathAnimation` has no native callers (`callers.py` on 0x051B11E0 is empty). It is BlueprintCallable
     (`execTryPlayDeathAnimation`), so a blueprint calls it again on the corpse.
   - Which blueprint is not settled statically. The host log's `c38_host_line("TryPlayDeathAnimation")` line
     names it.
   - The client copy follows the row, so both sides stay in sync. It is a visible re-flip, not a desync.

**Direction.** Step 1 rules out (a):
- The host's ragdoll start is an anim notify that fires up to 1.6 s after the death row.
- The death row cannot carry the time to a start that has not happened.
- Sample 0 already goes out in the start call.

That leaves (b) or a host-side change:
- (b): the client's copy starts its own ragdoll at its own notify (no hold). The stream then blends the bodies to
  the host's samples, never snapping, and the final row stays the host's.
- Host-side change: a dying host mesh is taken off the animation budget, so the host's notify keeps its own
  time. This narrows the gap; it does not close it.

(b) is a rewrite of the client stream apply: gravity-off teleport between samples becomes a blend from a
simulating body. It is not coded in this uncompiled batch, and the senior decides. Only the row is coded.

## Sibling sweep answers

| Item | Answer |
|---|---|
| AI troopers | 3D troopers ragdoll through the same `detour_c132_set_simulate` gate (any guid marked by card 038), so the same wait applies. 2D troopers have no bodies; they use card 177 sprite rows with no hold, and the late re-flip is item 3. |
| Other bug classes | Every game `SetSimulatePhysics(true)` from the exe on a marked copy passes `c132_pose_game_ragdoll`, so every ragdolling class has the same gate. Classes that reach Dead without a ragdoll use card 269 rest rows and have no hold. |
| Gib and overkill | Card 137 spawn rows are sent at the host's burst. The client has no hold on its side; it spawns on the row (11 ms class). |
| Destructible corpses | `K` rows apply on arrival. Timing is not visible statically; the run's row times will show it. |
| Corpse removal (card 140) | The `R` row removes the body on arrival. No hold. |
| Client-killed bug | 76 ms round trip (trace). Listed only. |

## As coded (uncompiled, 2026-11-13)

- `pose_c132.cpp`: `PoseWaits` keeps up to `kC291Waits` (512) resolved wait times (`wait_clear`). At the map
  wait totals (`c152_pose_wait_clear_all`), `c291_wait_line` logs
  `card291: client ragdoll waits <n>, median <ms>, worst <ms>`. The `card152 ... resolved after` row is unchanged.
- No fix coded; see Direction.

Open points:
- The senior picks (b), the host budget change, or both.
- The host log's `TryPlayDeathAnimation` caller names the item 3 blueprint.

## Senior trace (both logs, 2026-11-13): located, cause of the late host notify not settled

Guid 0x106, the 1616 ms wait:

| Side | Death row | Ragdoll notify (`SetSimulatePhysics`, caller exe+0x37C53CB) | Death to notify |
|---|---|---|---|
| Host | 82116669 (`card269: host rest row ... seq Death_05`) | 82119019 (`card132: host ragdoll start`, stream sample 0 in the same ms) | 2350 ms |
| Client | 255985927 (host 82116691) | 255986650 (held) | 723 ms |

The same sequence reaches the same notify 1627 ms later on the host than on the client; the stream then starts in
the same millisecond as the host's notify, and the client releases 11 ms after that (255988266 is host 82119030).
So the wire and the stream are not the delay: the host's death animation ran about three times slower than the
client's, or stood still for 1.6 s. A tick throttle that keeps the animation's time (the budget allocator) cannot
produce that; an animation that does not advance while the host does not render the mesh can. Not settled: no host
row says whether the mesh was rendered or ticking between 82116669 and 82119019.

Step 1b (junior, static plus one diagnostic row, no fix code before it is read):

1. From the class defaults of the bug meshes (`SkeletalMeshComponentBudgeted` on `BP_AI_WarriorBug_C` and the
   other enemy classes): `VisibilityBasedAnimTickOption`, `bEnableUpdateRateOptimizations`, and the budget
   component's settings (`bAutoRegisterWithBudgetAllocator`, `bAutoCalculateSignificance`, reduced-work flags).
   Say which of them stops or slows animation time, not only the tick rate, on a mesh the local camera does not
   render.
2. Diagnostic row at the host's ragdoll start: `card291: host t=<ms> guid <g> ragdoll <ms> after its death row;
   mesh last rendered <ms> ago, tick option <n>, budget state <flags>, sequence position <s> of <s>`.
3. The same question for every gameplay anim notify on the host (attack hit notifies of a bug the host does not
   look at): if step 1 item 1 names a visibility gate, the class is "host animation of an NPC that only a client
   sees", and the fix is on the host for the whole class, not on the client's ragdoll hold.

Option (b) of the Direction section is not taken before this answer: it would hide a host-side fault that may
also delay attacks.

## Step 1b answers (static, junior, 2026-11-13)

1. Class defaults. The bug blueprints (`BP_AI_WarriorBug`, `TigerBug`, `ArcherBug`, `HopperBug` and their base
   `BP_AI_AttackSystemEnemy_Base`) were extracted and their name maps searched: none carries
   `VisibilityBasedAnimTickOption`, `bEnableUpdateRateOptimizations` or a budget property, so the native defaults
   of the mesh class apply. The live value comes from the bone-check rows (`logs/c230-c3m1-d2-client.log`, guid
   0x154 and the rest): tick option 1 (AlwaysTickPose) at the ragdoll start, sample 0; the game switches it to 3
   (OnlyTickPoseWhenRendered) from sample 1 and back to 1 on the frozen final (668 rows at 1, 125 at 3). So
   during the death animation the option is 1, and option 1 alone does not stop animation time. What can stop
   or slow it on a mesh the host's camera does not see:
   - the animation budget allocator (`SkeletalMeshComponentBudgeted`): it throttles ticks by significance and
     render state; with reduced work it can skip the tick and interpolate, which slows time only if a skipped
     tick's delta is dropped;
   - `BP_AI_AttackSystemEnemy_Base`: `BP_OnSignificanceChanged` calls `SetComponentTickEnabled`. A
     component whose tick is turned off advances no animation time at all; this matches "stood still for
     1.6 s";
   - the bugs' `AnimatorComponent` static-mesh mode (bone-animation textures: `TrySetupStaticMesh`,
     `SM_Warrior`, `M_Warrior_BoneAnim_Inst`): a far bug may play its sequence on the GPU, not on the skeletal
     mesh, so the skeletal pose and its notifies wait until it switches back.
   The mod never writes the bug mesh tick option. The host significance update adds the remote players' views
   (`enemies.cpp:3873 detour_c132_update_significance`), but render-based gates (`WasRecentlyRendered`, the
   budget allocator's rendered flag) use only the host's camera.
2. Diagnostic row, coded in `pose_c132.cpp` (`c291_host_line`, called from `c132_pose_track` at the host
   ragdoll start, 60 lines per map, uncompiled): `card291: host t=<ms> guid <g> <class> ragdoll <ms> ms after its
   death start; rendered in 0.2 s <0|1>, in 2 s <0|1>; mesh <class> tick option <n>, update rate optimisations
   <b>, budget allocator <b>, auto significance <b>, actor rendered flag <b>` (-1: no such property). The death
   start is the card 269 rest row's time. The sequence position is not readable from the host without a new
   offset and is left out. Reading it: a long delay with "rendered in 2 s 0" points at a render gate; a long
   delay while rendered points at the significance tick toggle or the static-mesh mode.
3. Class note. Card 104 is the precedent: a server copy whose tick option is above 1 never fires its montage
   notifies (`UBWCOOP_C104_TICK` forces 1). If the row confirms a render gate, the class is "host animation of
   an NPC that only a client sees": attack hit notifies of a bug the host does not look at are delayed or lost
   the same way, and the fix belongs on the host for every enemy mesh (render-independent ticking while a remote
   player is near), not on the client's ragdoll hold.

## Senior review of step 1b (2026-11-13)

Accepted as a diagnostic; cause still open, no fix code. The static part is sound: tick option 1 during the death
animation rules out the plain `OnlyTickPoseWhenRendered` gate, which leaves three candidates (budget allocator,
`BP_OnSignificanceChanged` turning the component tick off, the animator's static-mesh mode).

Gap in the row: it separates "render gate" from "not a render gate" but not candidate 2 from candidate 3. Add to
`c291_host_line` before the next build, read at the ragdoll start:

- the mesh component's tick enabled bit (`PrimaryComponentTick` enabled state), and the actor's significance value
  if the offset is already known;
- whether the skeletal mesh component is visible (the static-mesh mode hides it while the GPU copy plays).

Reading it then: delay with tick disabled is candidate 2 (the host significance step already adds remote views,
`enemies.cpp:3873`, so a disabled tick near a client means the Blueprint's own significance input ignores them);
delay with the skeletal mesh hidden is candidate 3; delay with both normal and "rendered in 2 s 0" is the budget
allocator. The class fix stays host-side for every enemy mesh, as written in item 3.

## Senior verdict, b8 run 2 (2026-11-13): tick and visibility refuted, cause open

57 rows: `mesh tick enabled 1, mesh visible 1` on all but one (`mesh visible 0`, guid 0xc6); significance unreadable
(`-`). Neither a disabled mesh tick nor a hidden mesh is the general cause. Still needed: the user's description of
the delay's shape.

## Junior static trace for b11 (2026-11-13): the card152 bone-check WARNs, `logs/b10-last-client.log`

The "mesh component lags the body" reading does not hold. The component follows body 0 rigidly, and the gap is a
fixed offset.

- Per corpse, `from the mesh component` is the same in every sample while body 0 moves: 0xbc 37.4 uu (body moved
  89-122 uu), 0xa8 194.0 uu (418-432), 0xc6 281.9 uu (222-309). For each corpse, `bone 0 in component space`
  equals its value `at the stream start` in every sample: 0xc6 (-116.2, -69.2, 255.5), 0xa8 (-86.6, 25.8, 165.7),
  0xbc (0.0, 0.0, 39.0). The component origin moves with the body, about 50 uu between 0xc6 samples 1 and 3.
- That offset is set at the ragdoll start. The `card132: client ragdoll start` rows put the bodies' centre 70, 175
  and 263 uu from the mesh, in the same order as 37, 194 and 282. 0xc6 waited 1110 ms hidden for its host pose
  (`card137 ... held hidden: no host pose row yet`, 44 ticks). The pose then put its bodies where the host corpse
  lay, 263 uu from the client copy's mesh, which was still at the client capsule (`card251 ... mesh rel (-183, -169,
  -405)`). From then on, the simulating component keeps the root body's component-space pose that was current at
  the start. This matches the engine's root body sync (`RootBodyData.TransformToRoot`, captured at the physics init
  or root-body set, with the component placed from body 0 on each physics end tick; engine behaviour, not read in
  this exe). The visible mesh is drawn from the bones, and bone 0 stays at body 0, so the offset does not show by
  itself. It moves only the component origin and the actor, which matter for bounds, culling and significance. That
  is where the b8 `mesh visible 0` would come from, but that row is from another run and guids are reused per run,
  so it is not the same corpse.
- The WARNs come from the other term, `bone 0 ... from body 0`: 0xc6 at 8.4 uu in all 8 live samples and 7.8 at
  its settled final; 0xa8 at 1.9 (samples 9-10) and 3.7 (final); 0xbc at 0.0. That is 12 WARNs, plus one
  map-totals WARN. A constant 8.4 uu while the body is nearly still (samples 5-8 moved 0-3 uu) is not a tick lag. It
  is a fixed transform mismatch between the bone written from the body and the component it is written through. A
  component scale off 1 fits: 0xc6's bone lies 289.1 uu from the origin in component space and 281.9 uu in world
  space, which gives scale about 0.975. A translation that is unscaled in the root sync and scaled in the bone
  readback would leave a few uu (7.2 uu at a uniform 0.975). The logs do not have the scale.
- Missing fact: the copy's component scale. b11 adds `card291: component scale (x, y, z)` to every card152 bone
  check line (`pose_c132.cpp` `pose_bone_line`). Prediction: 0xc6-like corpses (constant bone-to-body gap) show a
  scale off 1, and corpses at 0.0 uu show 1.000. If the scale is 1.000 on a corpse with a constant gap, the
  mismatch is in the bodies' setup, not the scale, and the next fact to read is the body 0 setup's bone and its
  offset.
- No fix proposed before that row exists. A fix for the component-origin offset (re-basing the component onto the
  host root at the stream start) would only be worth carding if the origin offset is shown to break culling or
  significance on the client.

## Senior trace (2026-11-17 12:58, run `c423b182a`, b182): the host is not late, the client is early

Still present: 25 waits on FED_C2M1, slowest 1630 ms. Per guid, host death (`host InitArticulated ... IsDead 1`) to
host ragdoll start, against client time from the same host death (rest row age + row to `held`):

| Guid | Host sequence | Host death to ragdoll | Client death to its own ragdoll call | Wait |
|---|---|---|---|---|
| 0x13e | Death_03 | 898 ms | 834 | 66 |
| 0x7c, 0x120, 0x148 | Death_07 | 1350, 1363, 1348 | 917, 835, 833 | 432, 180, 515 |
| 0x152, 0x15c | Death_01 | 1564, 1566 | 833, 735 | 732, 832 |
| 0xda, 0x116 | Death_05 | 2350, 2334 | 719, 1488 | 1630, 848 |

- The host's time is fixed per sequence (within 16 ms), so the host animation is not throttled and not stalled. The
  2026-11-13 reading (host animation three times slower) is withdrawn; 2350 ms for Death_05 is the same number as
  in the playtest log.
- The client's time does not follow the host's sequence: 833 to 835 ms on four of eight, whatever the host played.
  The client copy does not play the host's death. `c38_death_edge` (enemies.cpp:1656) calls the game's own
  `TryPlayDeathAnimation` on the copy, which picks its own variant (exe+0x51B11E0: FName, then the shared play
  helper exe+0x51B0F30 that hands a name list to the sequence player), and `c131_client_dying` runs HandleDying.
  The card 269 rest row (`T guid age name`) is used only for copies that arrive dead.
- So the wait is (host sequence's ragdoll time) minus (the client's own death timing), and the client also shows a
  different death animation than the host. Not known: whether the client's ragdoll call comes from its own
  variant's notify or from the HandleDying path (the held row prints no Blueprint chain).

### Step 2 (fix and one diagnostic, same build)

1. Host: at `c269_death_note` (pose_c132.cpp:3642, the death name is read there) send `D <guid> <name>` to every
   ponged PC at once, for every 3D NPC death, ragdoll or not.
2. Client: on `D`, when the copy's death edge already ran and its sequence player's current name differs, play the
   host's name on the copy through the card 041 Anim row play path (by name, from the start; the row is 10 to
   20 ms behind the edge). When `D` arrives before the edge, keep it (rest row table style) and `c38_death_edge`
   plays that name in place of `g_orig_try_play_death`. Never both.
3. Rows: `card291: client death guid <g>: own sequence <name>, host's <name>, <same|replayed|played from the row>`;
   on the `held the game's ragdoll start` row add the Blueprint caller chain (as the host's `ragdoll start` row
   prints it) and `<ms> ms after the death edge`. The map total `card291: client ragdoll waits n, median, worst`
   stays.

Predictions (FED_C2M1, the c423 route): every `card291: client death` row names the host's sequence as played;
`ragdoll wait` rows at most 100 ms (median under 50); 0 fallbacks; digest 0 mismatches. If the names match and
waits stay long, the client's ragdoll call does not come from the sequence's notify: the caller chain on the held
row names its source, and that path is the next fix (the hold stays until then).

## Build b187 `920b53a5d0bb7c2e` (2026-11-17)
Host sequence names match, 0 fallbacks, but ragdoll waits are 228 ms against the 100 ms limit (miss). b188 adds a row naming the event the wait sits on and when it arrives.

## Build b188 `7921dd8832c458f7` (2026-11-17)
Run c423b188a (FED_C2M1): map 1 had 3 ragdoll waits, median 2 ms, worst 2 ms; 102 death rows all "played from the row". Map 2 had 0 waits and 22 rows played from the row. 0 fallbacks, 0 edges with no row. The one wait row: "client ragdoll wait on guid 0x4b6 sat on the stream row's pose (row not posed); ended by the stream start after 2 ms". The b187 228 ms waits did not recur. Digest: 17 rounds 0; the only nonzero rounds are the Score gap now under card 323, not a death row.

## Closed (2026-11-17 14:14)
Evidence: the step 2 predictions are met in c423b188a. Every client death row plays the host's sequence, ragdoll waits are at most 2 ms (limit 100 ms, median under 50), and there are 0 fallbacks.
Not done: the b187 run's 228 ms waits had no cause row, so their source is unknown; they did not recur in b188.
Reopen when: a `card291: client ragdoll waits` map total shows a worst wait over 100 ms, any fallback row, or a playtest report of corpses lagging behind the host.

