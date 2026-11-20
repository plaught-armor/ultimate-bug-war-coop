# 523: Client bug deaths stop, then drop (stream start freeze)

Opened 2026-11-19 by senior from a user report (client window): "arachnids aren't finishing their death animations
before stopping and falling dead in some cases". Real bug, play visible.

## Trace (static, `pose_c132.cpp`, plus the c263i b300 logs)

- The host's ragdoll start comes from the game, partway into the death animation: c263i host `card291: host ...
  ragdoll N ms after its death start`, 60 rows, median 835 ms, range 0 to 6583 ms (WarriorBug mostly 700 to 1600 ms).
  On the host the ragdoll takes over from the animation pose with the bodies' own motion, so the bug keeps falling.
- The host sends stream sample 0 at that ragdoll start (card 152). The client's stream start for a guid comes within
  32 ms of the host's gap after its own death start (29 guids matched: client gap minus host gap -32 to +29 ms, two
  outliers at +385 and +1552 ms where the host row said -1). So the client's animation is cut at the same point as
  the host's; the timing is not the fault.
- The fault is what the client does at sample 0. `pose_stream_apply` -> `pose_stream_start` starts the ragdoll,
  turns gravity off, then `pose_apply_bodies` teleports every body to sample 0 with zero velocity, and
  `memcpy(r.prev, r.blob, ...)`. `pose_stream_tick` moves the bodies from `r.prev` to `r.blob`; with both equal to
  sample 0, the copy hangs still in the air until sample 1 comes, at least kPoseStepMs (250 ms) later. Only then does
  it start to move, as an interpolation between the last two samples. That is the "stopping, then falling dead".
- The same hold repeats mid-stream whenever a sample is late: `a` is capped at 1.0, so the copy stops at the last
  sample until the next one arrives.
- Side note (not this card's fix): card 291 replays the host's death variant when the client picked another (c263i:
  28 of 89 deaths "replayed"); the replay restarts the animation from frame 0 at the row's arrival, 0 to 30 ms after
  the edge.

## Fix (design for review)

1. At sample 0 the client copy starts its ragdoll through the same start as now, but keeps gravity on and does not
   teleport or zero the bodies: it falls on its own from its own animation pose, as the host's does.
2. When sample 1 comes, the client reads its bodies' current transforms into `r.prev` (a fresh read, in the 14 B
   form), turns gravity off, and moves from there to sample 1 over kPoseStepMs. From sample 2 on, the stream runs as
   now.
3. An unfreeze row, a joiner's replayed sample and a stream with only one sample before its final keep today's path
   (teleport to the row), since there is no own fall to continue from.
4. Log per map: own-fall starts, the body 0 distance between the own fall and sample 1 at the hand-over (worst and
   median), and late-sample holds (ticks with `a` at 1.0 and the longest hold per row).

## Predictions (one lane 2 run with bug kills on the client screen, C3M2 c263 clears)

1. Client: 0 rows where the copy is posed still at sample 0 for more than one tick; own-fall starts about equal to the
   stream starts.
2. Hand-over body 0 distance median under 30 uu (both sides fall from about the same pose for 250 ms); worst logged.
3. Unchanged: card140 0 over 2 uu, `Pose:` digest 0 mismatches, 0 bad payloads.
4. Late-sample holds counted; if the longest hold is over 100 ms in more than a few rows, that is a follow-up (the
   latency rule), quoted here with numbers.

## Done when

Predictions 1 to 3 hold in one lane 2 run, and the user sees no stop on the client in the next playtest.

## In source (senior, 2026-11-19, not yet in a build)

- `pose_c132.cpp`: new row field `own_fall`. `pose_stream_apply`: a live sample 0 (not an unfreeze, no held-hidden
  arrival) starts the ragdoll through `pose_stream_start(..., own = true)`, which leaves gravity on; no teleport, no
  velocity zeroing; the row is Applied and live. The next sample goes through `pose_stream_handover`: the copy's
  current bodies are read into `r.prev` (`pose_encode_to`), gravity off, velocities zeroed, and `pose_stream_tick`
  moves it to that sample over kPoseStepMs. A failed read teleports to the sample as before.
- While falling on its own the row is left out of the digest (simulating, like an unstreamed fall), and the game's
  own SetAllBodiesSimulatePhysics(0) on it is blocked as on a streaming copy (`c152_pose_game_freeze`). A final or a
  new copy of the guid clears `own_fall`.
- New map row: `card523: client stream own falls: S started, H handed over at the next sample (body 0 mean M uu,
  worst W uu), T teleported; L chained samples over 350 ms late, longest gap G ms`.
- Object compiles clean (clang-cl, 0 warnings). Backup: `build/pose_c132.cpp.pre523`.

## Run 1 (c263k, b302 cc796efeb952a0f7, FED_C3M2, 344 s, junior run, senior dig 2026-11-19)

- Prediction 1 holds: `card523: client stream own falls: 60 started, 60 handed over`, 0 teleported.
- Prediction 2 fails: hand-over body 0 mean 327.9 uu, worst 3147.8 uu (predicted under 30). The copy no longer
  stops, but it slides that far over the 250 ms after sample 1.
- Prediction 3 holds: `card140` 124 freezes, 0 over 2 uu; 15 digest rounds at 0 mismatches; card521 client 668
  deltas, 0 misses.
- Late samples: 39 chained samples over 350 ms, longest gap 5791 ms. A host corpse at rest sends no sample, so a long
  gap is not always a visible hold; not chased.
- Cause of prediction 2: the client copy dies where the client drew it, not where the host's body was. Client
  `card132: client ragdoll start` mesh position against host `card152: host stream guid ... sample 0` root, 62 guids:
  WarriorBug xy median 148 uu (48 guids), HopperBug xy median 78 uu but up to 2680 uu off in height (14 guids). The
  old teleport hid that offset with a snap at sample 0; the own fall carries it to sample 1. (The mesh origin is not
  body 0, so the z part of that comparison is an offset, not a measure; the new row below measures body 0 to body 0.)

## Fix r2 in source (senior, 2026-11-19, after b302)

- `pose_own_shift`: on every tick of an own fall, every body moves by the next part of the offset between sample 0's
  body 0 and the copy's body 0 at the own-fall start. The move is linear over kPoseStepMs, with a physics teleport
  that keeps velocities, so the copy keeps falling on its own while it closes on sample 0's place. Rotations stay the
  copy's own. The `card523` row now also prints that start offset (mean, worst).
- The `card152: client stream start` row now says "gravity on (card 523 own fall)" for an own fall, where it said
  "gravity off" for every start.
- Object compiles clean. Backup `build/pose_c132.cpp.pre523b`.
- Predictions (next lane 2 run with client kills): hand-over body 0 mean under 30 uu, and the start offset mean about
  150 uu. If the start offset mean is over 100 uu, the client draws live bugs more than 100 ms behind the host, which
  breaks the latency rule. That gets its own card with the movement path traced, not a bigger shift here.

## Run 2 (c263l, b303 8ece51514ce72a9d, FED_C3M2, 340 s, junior run, senior dig 2026-11-19)

- Row: `card523: client stream own falls: 63 started (body 0 off sample 0 mean 442.6 uu, worst 4705.7 uu, closed over
  250 ms), 63 handed over at the next sample (body 0 mean 442.6 uu, worst 4705.7 uu), 0 teleported; 39 chained
  samples over 350 ms late, longest gap 45450 ms`. Prediction (hand-over mean under 30 uu) fails.
- Cause: the hand-over mean and worst equal the start offset to 0.1 uu over 63 rows, so the hand-over ran at sample
  0's own moment. Client guid 0x1fa at 22096308 ms: `P 1fa 0 0 1 22 8` (sample 0, Stream) and `P 1fa 1 0 1 22 5`
  (snap 1, Unsettled | Unfreeze, card 137's unfreeze snap) arrive in the same ms. The snap goes through
  `pose_try_apply`, which sends a streamed row's Unfreeze to `pose_stream_apply`; that saw `own_fall` and handed over
  at once, with the bodies not yet moved and the blob still at that moment. `pose_own_shift` never ran, and the copy
  slid from its own place to sample 0 over 250 ms, then held until sample 1. Run 1's 327.9 uu hand-over was the same
  fault.
- Rest unchanged: card140 131 freezes, 0 over 2 uu; 14 digest rounds, 0 mismatches.
- The start offset itself (mean 442.6 uu, worst 4705.7 uu) is over the 100 uu bound named in r2. It is measured
  body 0 to body 0 and is independent of the fault above, so it stands: see the follow-up card below.

## Fix r3 in source (senior, 2026-11-19, after b303)

- `pose_stream_apply`: during an own fall, a row without the Stream flag (the unfreeze snap) keeps the fall (state
  Applied, no hand-over) and is counted; only the next stream sample hands over. The card523 row adds `N unfreeze
  snaps kept it`. Backup `build/pose_c132.cpp.pre523c`. Object compiles clean, 0 warnings.
- Predictions (next lane 2 C3M2 run): unfreeze-kept about equal to the own-fall starts; hand-over body 0 mean under
  30 uu (well under the start offset); card140 0 over 2 uu; digest 0 mismatches.

## Run 3 (c263m, b306 1022ac44d36a3ed8, FED_C3M2, junior run, senior dig 2026-11-19)

- Row: `47 started (body 0 off sample 0 mean 401.5 uu, worst 2543.2 uu, closed over 250 ms; 49 unfreeze snaps kept
  it), 44 handed over at the next sample (body 0 mean 177.5 uu, worst 1298.2 uu), 0 teleported; 24 chained samples
  over 350 ms late, longest gap 77066 ms`.
- Unfreeze-kept 49 against 47 starts: the r3 fix holds. Hand-over mean 177.5 uu (was 442.6), over the 30 uu
  prediction. 3 starts were never handed over (no later sample).
- The start offsets split by class in card 524: HopperBug z median -795.6 uu, WarriorBug small. A copy shifted onto a
  sample 0 that is falling fast still starts its own fall from rest, so the gap grows again over 250 ms. The
  per-guid hand-over row (card 524 diag) tells whether the 177.5 uu is HopperBugs only. Card digest and card140
  unchanged at 0.

## Run 4 (c247b311a, b311 729695d7a1d46794, with the card 524 fix, junior run, senior read 2026-11-19)

- Row: 149 own falls started (body 0 off sample 0 mean 48.2 uu, worst 445.5 uu), 136 handed over at the next sample
  (mean 28.9 uu, worst 204.2 uu), 0 teleported; 54 chained samples over 350 ms late, longest gap 11500 ms.
- The hand-over mean is under the 30 uu prediction. The run 3 gap was card 524's mesh offset; with that fixed the
  start offset mean is 48.2 uu, under the 100 uu latency bound. Predictions 1 to 3 hold: done in lane 2, the
  user's playtest is the last check.
