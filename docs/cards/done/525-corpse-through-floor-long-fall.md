# 525: Corpses that fall through the floor stream as long falls

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: Opened 2026-11-19 by senior from the c247b303a (b303 8ece51514ce72a9d, FED_C5M2) digest mismatch. Real bug in the
mod's card 189 check, plus an open question about the floor.

## Finding

- The only mismatch in 26 digest rounds: client round 13 (22735329 ms), `Pose:1`, AphidBug guid 0xfc0. Body 8 was
  8018 uu from the host's last sample against a tolerance of 7824 uu (100 plus the sample-to-sample span). Body 0 was
  at z -481,621 on the client and -489,697 on the host.
- Host history of 0xfc0: ragdoll start at 22703702 on the floor (mesh z 102, floor z 100, bodies 3297 uu from the
  mesh before the sync, 150 after). A second ragdoll start at 22731954 with the mesh at z -392,118, so the corpse had
  already gone through the floor. At 22732483: `card189: host pose guid 0xfc0 long fall, floor at z -1202782 (795207
  uu below)`.
- Card 189's trace from body 0 down to the kill z hits geometry about z -1.2M under the map and calls it a landing. The
  corpse then streams as a long fall at about 30,000 uu/s (about 7,700 uu per 250 ms sample). Each client check runs
  up to one frame behind the latest sample, about 500 uu at that speed, so the check can go over its tolerance.
- The same misclassification shows in this run for WarriorBug 0x9b6 (1,218,426 uu below; its second ragdoll start had
  the mesh at z 691 under a floor at z 1187), and in at least 15 earlier host logs (c244, c245, c247, c492, c502,
  c504). Over all host logs, real long falls reach 25,162 uu. The next depth is 795,207 uu, and every row from there
  on is above 795,000 uu.

## Fix (in source, senior, 2026-11-19, after b306)

- `pose_fell_check`: a floor more than `kC525MaxDropUu` (100,000 uu) under body 0 counts as no floor. The corpse goes
  through the existing card 189 "fell through the world" path (`c189_stop`: the simulation stops and is held). The
  row now prints the first hit z. Backup `build/pose_c132.cpp.pre525`. Object compiles clean, 0 warnings.

## Open (not chased; card when it is in view)

- Why the host corpse goes through the floor at all. Both cases had a second ragdoll start (`ragdoll left`) after the
  first, with the mesh already under the floor. 0xfc0's first start had its bodies 3297 uu from the mesh before the
  card132 sync. It may be the game's own behavior; the next step is a single-player check on the same map, or the
  host's body 0 z between the two starts.

## Predictions (next run with a fall-through)

1. Host: 0 `card189 ... long fall` rows deeper than 100,000 uu. Each such corpse gets a `fell through the world` row
   with the first hit z.
2. Client: 0 Pose mismatches from those guids; card140 0 over 2 uu.

## Done when

Prediction 1 and 2 hold in one lane 2 run that has a fall-through row.

## Run c247b309a (b309 a86a3943a7f0f8fe, FED_C5M2, junior run, senior dig 2026-11-19)

- Prediction 1 holds: one host `fell through the world` row (guid 0x8ce, body 0 z -332, start 748, first hit z
  -1218586) and 0 `card189 ... long fall` rows deeper than 100,000 uu. Prediction 2 holds: 24 client digest rounds, 0
  mismatches.
- The row printed `no floor down to z -3.4e38`: the C5M2 kill z is about -FLT_MAX (no kill plane), and the trace ran
  to it. Fix in source after b309: the trace stops at `kC525MaxDropUu` below body 0, since a floor past that counts as
  none. The row now prints that bound and the depth, and no longer prints the first hit. Backup
  `build/pose_c132.cpp.pre525b`. Object compiles clean, 0 warnings.
- Done when the next run with a fall-through prints a sane bound. The open floor question stays.

## Closed 2026-11-21 (junior, from c591b428a)

c591b428a (b428, FED_C4M1) host row 178663434: `pose guid 0x0 fell through the world: body 0 z -1698, start -651; no
floor down to z -101698 (100000 uu)`. The bound is sane (kC525MaxDropUu), and there is no long-fall row past it.
