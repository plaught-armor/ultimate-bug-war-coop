# 524: Client draws live bugs far from the host's place (death start offset)

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: Opened 2026-11-19 by senior from the c263l (b303) card523 row, per the latency rule (player-visible sync under
100 ms).

## Finding

- At each own-fall start (card 523), the client copy's body 0 is far from the host's sample 0 body 0: 63 deaths,
  mean 442.6 uu, worst 4705.7 uu (`card523: client stream own falls: 63 started (body 0 off sample 0 mean 442.6 uu,
  worst 4705.7 uu ...)`, c263l client log).
- c263k's mesh-to-root comparison gave the same picture: WarriorBug xy median 148 uu (48 guids), HopperBug xy median
  78 uu but up to 2680 uu off in height (14 guids).
- A WarriorBug runs about 600 to 900 uu/s, so 150 uu is roughly 200 ms of lag; 442 uu mean is well over the 100 ms
  bound. The worst rows (thousands of uu, in height) look like HopperBugs drawn on the ground while the host's is in
  a jump, or a copy not moved since its last relevancy.

## Next (static first, no run until the trace is written)

1. Trace the client's live bug movement path: what moves a client copy (replicated movement, the mod's own
   correction, card 434 spawn bunches) and at what rate; where the delay comes from.
2. Per-guid split of the c263l offsets by class and by z versus xy (client `card132: client ragdoll start` rows
   against host `card152: host stream guid ... sample 0` rows), to see whether the worst are jump arcs.
3. Then write the fix and its predictions here.

## Done when

The card523 start offset mean is under 70 uu (about 100 ms of a running bug) in one lane 2 C3M2 run, worst
explained per class.

## Trace so far (senior, 2026-11-19)

- Host side looks fine: NPC NetUpdateFrequency 100.0 (`netbudget` rows), and over 58 windows only 19 NPCs moved
  over 10 uu and went unsent for more than 300 ms.
- Mesh against host body root (c263l, 131 rows): WarriorBug xy median 211 uu, worst 496; HopperBug xy median 126,
  worst 1886, client mesh 672 uu above the floor at the median (host body 104). That comparison mixes the capsule
  origin with the pelvis: the client's own bodies sit 135 uu from its mesh. It cannot settle lag.
- Diag in source (b304): `card524: client own-fall start <class> guid G: body 0 off sample 0 xy X uu, z Z uu (host
  minus client), applied N ms after sample 0 came`, up to 80 per map. That gives body 0 to body 0 per class and
  axis. If WarriorBug xy stays near 200 uu, the next step is the death pose: card 291 replays the host's death
  variant from frame 0 at the row's arrival, so the client's pelvis may be at a different point of the same
  animation.

## c263m (b306 1022ac44d36a3ed8, FED_C3M2, junior run, senior dig 2026-11-19)

- 47 `card524: client own-fall start` rows. Per class (median): WarriorBug 34 guids, xy 69.7 uu, z -0.9 uu (|z|
  20.4 uu); HopperBug 13 guids, xy 325.5 uu, z -795.6 uu (host lower), worst xy 1853.4 uu, z -1741.4 uu.
- WarriorBug is at the 70 uu bound already. The fault is HopperBug height: the host's corpse is far below the
  client's copy at sample 0.
- Host `card291` for HopperBug: ragdoll 7767 ms (0x24a), 1521 ms (0x2c8), 24316 ms (0x29a) after the death start,
  most "rendered in 0.2 s 0" on the host. A dead HopperBug spends seconds between its death start and its ragdoll
  start, and in that time the host's body ends up far lower than the client's copy. Not yet traced: whether the
  host capsule falls in that time while the client copy stays at its last replicated place (death stops its
  movement updates), or the host mesh (not rendered, budgeted tick) is stale.
- Next trace (static): the HopperBug death path (flying movement mode at death, what moves the capsule until the
  ragdoll), and whether the client copy's replicated movement goes on after its death start.
- Diag in source (after b307, backup build/pose_c132.cpp.pre524h): `card524: client hand-over <class> guid G: body 0
  off sample N xy X uu, z Z uu (host minus client), M ms after the own-fall start`, up to 80 per map, to split the
  card 523 hand-over distance by class.

## HopperBug 0x24a, both logs (c263m, senior 2026-11-19)

- Host: death at 23015974 ms with the actor at (-21316, -3498, 2898). At the ragdoll start 7767 ms later
  (`SetAllBodiesSimulatePhysics(1)` row) the actor is at (-21316, -3498, 2019): the dead HopperBug's capsule dropped
  879 uu to the floor (floor z 1930) during the death. Sample 0 root z 2026.
- Client: the Death row came at 23015995 ms, and at the own-fall start (23023788 ms) the copy's mesh was at
  (-21306, -3497, 2798), that is the actor still at about z 2898. The copy never followed the host's capsule down
  in those 7.8 s, so the whole 878.6 uu z offset is that drop.
- So the fault is not lag of a live bug: the client copy of a dead bug stops moving at its death row while the host's
  dead capsule still falls. WarriorBugs die on the ground, so theirs barely shows.
- Next trace (static, before any fix): what stops the client copy's movement after the death row (the host stops
  sending the dead actor's replicated movement, or the client's own death path turns off its movement component or
  ignores ReplicatedMovement), and whether the mod already has a dead-actor location path to reuse. The fix then
  moves the client copy with the host's dead capsule until the ragdoll start; the prediction is HopperBug z median
  under 70 uu at the own-fall start.
- Diag in source (after b307): the `card524: client own-fall start` row adds `actor z A, replicated z R` (the copy's
  ReplicatedMovement.Location). Prediction for the next C3M2 run: on the HopperBug rows with a big z offset, R is
  near the host's floor (the client did not follow a location it had) or near A (the host stopped sending). That
  picks the side of the fix.

## c247b309a (b309 a86a3943a7f0f8fe, FED_C5M2, junior run, senior dig 2026-11-19): cause found

- Per-guid hand-over rows by class: WarriorBug 64 rows, xy median 13.5 uu, z median -1.6 uu (card 523's 30 uu bound
  holds for it). HopperBug 9 rows xy median 133.8 uu (worst 948.7), HopperRanged 1 row 543.3 uu.
- The diag prediction ("R near the host's floor, or near A") is answered: on every own-fall start row the client's
  actor z equals its replicated z (HopperBug 0x1d8: actor z 1018, replicated z 1018), and the host's sample 0 root
  for 0x1d8 is at z 1024. The client capsule follows the host. Replication is not the fault.
- The client's mesh is the fault: it is left behind its own actor. Client mesh z at the ragdoll start minus the
  client actor z: WarriorBug -200 to -178 (its normal mesh offset), HopperBug -97, 33, 438, 795, 1266, ..., 2665
  (12 rows), HopperRanged 978 to 2253 (4 rows). The body 0 z offset at the own-fall start is that excess almost one to
  one (0x1d8: mesh 1266 above the actor, body 0 offset -1366). On the host the same hopper's mesh is 106 uu under its
  actor (mesh z 918, root z 1024).
- So the client copy of a dying HopperBug draws at its last flying height while its capsule drops with the host's.
  The user sees it die in mid-air, and the corpse then falls from there.
- Likely mechanism (to confirm statically): the client's character network smoothing. Each replicated location
  moves the capsule and adds the jump to the mesh translation offset (SmoothCorrection), and the offset decays only
  in the movement component's tick (SmoothClientPosition). The dead bug's movement no longer ticks on the client, so
  the offset never decays and the mesh stays where the bug was flying.
- Fix design: on the client, at the death row of a Character copy, clear its smoothing offset and put the mesh back
  at the character's base translation and rotation offsets, then keep it there until the ragdoll start (or stop
  adding offsets: smoothing mode Disabled on the dead copy). Static work first: find SmoothCorrection and
  SmoothClientPosition_UpdateVisuals in the exe, the ClientPredictionData MeshTranslationOffset, ACharacter's
  BaseTranslationOffset, and whether the client's CMC tick is off after the death row.
- Predictions (next lane 2 run with hopper kills): client mesh minus actor z at the ragdoll start under 0 for every
  HopperBug and HopperRanged row (as the host's); HopperBug own-fall start z offset median under 70 uu; hand-over xy
  median under 30 uu for every class.

## Fix in source (senior, 2026-11-19, after c247b309a; not yet in a build)

- `pose_c132.cpp` `c524_mesh_reset`, called from `c291_death_edge` (every client death edge, from `c38_death_edge`):
  reads the CMC's ClientPredictionData (CMC+0x7B0) MeshTranslationOffset (+0x80), zeroes it, puts the mesh back at
  the character's BaseTranslationOffset (Character+0x400) with `USceneComponent::K2_SetRelativeLocation` (teleport,
  no sweep, rotation kept), and sets NetworkSmoothingMode (CMC+0x233) to 0 (Disabled). SmoothCorrection's Disabled
  path (the `cmpb $0x0, 0xa3(%rdi)` test) moves the capsule only, so later corrections on the corpse add no offset.
- New symbol `USceneComponent_K2_SetRelativeLocation` (RVA 0x03AE8FD0) in `tools/symbols.txt`, bound in the
  hooks_ubw table as `g_set_relative_location`. New offsets in `ue/actor.h`.
- Rows: per guid (first 60 per map) `card524: client death edge <class> guid 0x..: smoothing mode M, offset (x, y,
  z); mesh D uu from its base offset; mesh minus actor z B before, A after`; per map `card524: client death edges:
  N meshes put back on their actor, K with a smoothing offset over 30 uu, largest L uu`.
- pose_c132.cpp, enemies.cpp, hooks_ubw.cpp compile clean. Backups `build/*.pre524`.

Predictions (next lane 2 run with client hopper kills):

1. Death edge rows: `after` about -100 to -200 uu for every class (the host's mesh sits 106 uu under its actor).
   The mode reads 1 or 2. If the offset at the edge is already large, smoothing was failing while alive too.
2. Client `card132: client ragdoll start` mesh z minus the actor z under 0 for every hopper (was up to +2665).
3. card523: start offset and hand-over means fall well under the c247b309a values; HopperBug own-fall z offset
   median under 70 uu; hand-over xy median under 30 uu for every class.
4. Unchanged: card140 0 over 2 uu, `Pose:` digest 0 mismatches.

If prediction 2 fails with the edge row showing `after` normal, something other than the smoothing offset moves the
mesh after the edge; trace that before any second change.

## Run 1 (c247b311a, b311 729695d7a1d46794, 648 s, junior run, senior read 2026-11-19)

- Map row: `card524: client death edges: 270 meshes put back on their actor, 70 with a smoothing offset over 30 uu,
  largest 119.3 uu`. Per-guid hopper rows read `mesh minus actor z -99 before, -100 after`, inside the predicted
  -100 to -200 uu.
- Hopper own-fall start offsets (host minus client z) now -12.0 to +52.3 uu; b309 had -544 and -778, up to 2665 uu.
- card523 row: 149 own falls (body 0 off sample 0 mean 48.2 uu, worst 445.5; b309 mean 407.0, worst 3328.9), 136
  handed over (mean 28.9 uu, worst 204.2), 0 teleported. The card 523 r3 prediction (hand-over mean under 30 uu)
  now holds as well.
- Digest 31 rounds, 0 mismatches; FATAL/ERROR 0.
- Predictions hold. Done in a lane 2 run; the user's playtest is the last check.
