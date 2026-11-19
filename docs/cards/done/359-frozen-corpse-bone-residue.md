# 359: Frozen corpse bone 0 stays 1.9 to 3.3 uu off body 0 (WARN card191 residue)

Split from card 358. Low severity: about 2 to 3 cm, not visible. Queue after the playtest.

Rows: run `b42` guid 0x322 1.9 uu three times; run `b43` 3.3 uu (0x322), 2.8 (0x3b6), 2.8 (0x340), 1.9 (0x318), all
with "blend physics was 1", simulating 1, unchanged through the 10 freeze waits. `kC191BoneTolerance` is 1.0 uu.
Run `b43r2` had 0 rows, so it does not happen every run.

Likely reading (card 358 trace, not proven): the last apply moves bodies that are asleep (`awake 0` in the end-tick
rows) by the stream drift, and asleep bodies are not copied into the bones by the end-of-physics blend.

To do: prove or refute that from the end-tick rows; if proven, wake the bodies at the final apply so one end tick
copies them, and keep the tolerance. Gate: 0 `WARN card191` over 2 runs of `b43-c357-c2m1`.

## 2026-11-15, run b53 (DLL `4e4b316a3a920505`)

- Four guids froze with residue in `logs/b53-client.log` (0x24c 3.7 uu, 0x37e 1.0, 0x392 1.7, 0x3a6 2.8), against
  one in b52. The three new ones are corpses hit again by the test fire: every host resnap after a hit refreezes the
  copy and leaves the same residue. Same cause, more refreezes; no new path.
- New row: `WARN card132: recv 4361 ... Pose ... text R 37e result skipped: no row`. The host sent one more pose
  frame for guid 0x37e (sent t=390987682) after the corpse was gibbed (client `TermArticulated()` t=390987530,
  Health -15700). The client drops it safely. Fix with this card: the host ends a corpse's pose stream at its gib.

## 2026-11-15, run c125b54c (b54 `cd14c3d92a441cf0`, FED_C3M1 turret proof)

Same class on a turret kill: client `WARN card152` bone check guid 0xd4, 10 samples "(live)", pelvis 85.4 to 86.2 uu
from body 0 while body 0 is 1.1 to 2.3 uu from the last target set; `WARN card191` frozen bone off body 0xd4 82.7 uu
and 0xe8 127.2 uu. The body follows the host, the bones do not. Turret fire keeps hitting the corpse, as the test
fire did on b53. No new cause; fix stays with this card.

## 2026-11-16 11:13 senior trace: severity raised, visible up to 278 uu

Not a 2-3 uu residue only. Client logs hold corpses frozen with bone 0 far off body 0: 278.7 uu (c418b116c guid
0x12c), 127.2 and 82.7 uu (c125b54c 0xe8, 0xd4), 67.7 (c414b108a 0x5a), 48.0 and 24.2 (c400b89a 0x8c, 0xaa). It is
drawn wrong, not a read artifact: after the freeze of 0x12c the next final found the bodies 279 uu from the host
pose, so the kinematic bodies had followed the off bones.

Shape of a bad copy: bone 0 equals body 0 at the stream start, then bone 0 in component space jumps once (0xd4:
(-75.5, 109.9, 139.5) to (0.5, 117.7, 178.8)) and stays constant for the whole stream while the bodies follow the
host; simulating 1, ShouldBlendPhysicsBones 1, end tick registered 1, blend physics 1. A good copy keeps bone 0 on
body 0 every sample. Ten freeze waits do not help.

Ruled out by tabulation over 45 to 60 client logs (no separation of bad from good): VisibilityBasedAnimTickOption
(1 or 3), required bone count (35, 38, 40, 41), body re-creation by InitArticulated at death, the game's own
SetAllBodiesSimulatePhysics(1) during the stream.

Open cause, two candidates: (a) the end physics tick is registered but does not tick on this copy (tick disabled
or on an interval, for example by the game's significance code), so the blend never runs; (b) the blend runs and
something writes the animation pose after it.

Step 1 (diagnostic, patch `c359_diag_patch.py` in the senior scratchpad): the `WARN card191 ... frozen with bone 0`
row gains the end tick count since the final's apply and the end tick function's group, flag and interval bytes.
Prediction: count 0 means (a), and the fix is to run or enable the end tick for a streaming copy; count 10 or more
means (b), and the fix is `bNoSkeletonUpdate` from the stream start. Done when a chain or soak run shows no
`WARN card191 ... frozen with bone 0` row above 20 uu and a receiver screenshot of a corpse that had the row.

## 2026-11-16 11:24 senior: diagnostic read (b124 `931762d66b1e7d1d`, run `c423b124a`, FED_C2M1), fix spec

- 90 `WARN card191 ... frozen with bone 0` rows; every one reads `11 end tick(s) since the final's apply`, bytes
  `03 00 0e 04`, interval 0. Candidate (a), an end tick that does not run, is dead.
- Worst: guid 0x81c, 436.4 and 437.5 uu (snaps 2 and 3, 20 s apart). The offset is constant across the 20 s, so it
  is not the one-frame lag of a moving body.
- Reading: the client step measures bone 0 at a point of the frame where the bones hold the animation's pose
  (already known after the freeze, card 191: `0.0 at the first end tick, 451.7 at the second`). On a copy whose
  animation still refreshes, the step's wait can never see bone 0 on body 0; after 10 waits it stops the bodies and
  sets `bNoSkeletonUpdate` with the animation's pose in the bones. The offset is the distance the ragdoll travelled
  from where the copy's component stands. Copies that pass (0.0 uu) are the ones whose animation no longer refreshes.
- Fix (`c359_fix_patch.py`, `pose_c132.cpp` only): in `detour_c191_end_tick`, for a row the step has already found
  off (`Freezing`, `freeze_waits != 0`), measure bone 0 before and after the original; when it is within
  `kC191BoneTolerance` after the blend, call `pose_freeze_step` there (bodies stopped and `bNoSkeletonUpdate` set
  while the bones are the bodies). Otherwise the step's bounded wait stays and its WARN row prints both distances.
- Predictions, next FED_C2M1 run: rows `card191: client guid ... freeze waited N tick(s) for bone 0, now 0.x uu`
  in place of the WARN rows; 0 WARN rows above 1 uu. If WARN rows remain, their `before the blend / after` pair
  says whether the blend writes at all (after large: blend does not write this copy, next cause is the blend's own
  conditions; after small: not reachable, the freeze would have run).
- Done when: 0 `WARN card191 ... frozen with bone 0` rows in a FED_C2M1 run, `card140 ... freeze check` rows still
  0 bad, and one client screenshot of a far-thrown corpse lying where the host's lies.

## Step 2 result (b125 `4183cad44040c8fa`, run `c423b125a`, 2026-11-16) : prediction failed

The freeze right after the end tick's blend did not help. Client log: 91 `WARN card191 ... frozen with bone 0`
rows, and in all 91 the distance before the blend equals the distance after it; 10 `freeze waited` rows ended at
0.0 uu. Sizes: 63 rows at 3 uu or less, 16 at 3 to 10, 10 at 10 to 50, 2 above 50 (guid 0x73c, 204.5 and 204.1 uu).
The error is constant per guid across finals (0x134: 2.4 and 2.5 uu; 0x4dc: 18.8 and 18.7), the bodies are exact
(0.0 uu body error) and the mesh component sits 334 to 377 uu from the bodies. The earlier reading ("the step
measures the bones while they hold the animation pose") is refuted: the blend ran and left the same error.

## Step 3 (patch `c359_weight_patch.py`, for b127)

Hypothesis: the blend writes bone = lerp(animation pose, body, the body's `PhysicsBlendWeight`), and these copies
have a weight below 1, so a fixed share of the offset stays (0x134: 2.4 of 334 uu, weight near 0.99; 0x73c: 204 of
377 uu, weight near 0.46). `FBodyInstance::PhysicsBlendWeight` is at +0x134: `SetAllBodiesPhysicsBlendWeight`
(exe 0x4197D30) loops over `Bodies` (+0xB80) and does `movss [body+0x134], xmm6`.

Change: `c359_blend_one(mesh)` writes 1.0 into every body's weight on each waited tick of `pose_freeze_step` and
returns the lowest weight found; the first wait's value is printed in the WARN row and the `freeze waited` row.

Done when, on a FED_C2M1 run: `freeze waited` rows show a first weight below 1.000 and end within 1.0 uu, and the
WARN count falls from 91 to under 10. Refuted when the rows print 1.000 and the WARN count stays near 91; then the
method changes (no second try of a weight write).

## Step 3 result (b129 `674d1a093bbc6c6a`, run `c423b129a`, 2026-11-16): refuted

91 `WARN card191 ... frozen with bone 0` rows again, 58 guids, lowest blend weight printed 1.000 in every row,
before == after the blend in all 91. Sizes: min 1.0, median 2.6, max 174.3 uu, 23 over 5 uu. Only 9 `freeze waited`
rows ended within tolerance (all after 1 tick). The chain run `c399b128a` has 0 such rows: FED_C2M1 only so far.
No second weight write; the method changes.

## Step 4 (diagnostic, patch `c359_host_bone_diag_patch.py`, for b131)

Hypothesis (not proven): the offset belongs to the corpse, not to the client copy. The bodies are exact, the blend
ran with weight 1, the error is constant per guid across finals, and the mesh component sits 334 to 377 uu from the
bodies with a non-uniform component scale (guid 0x50: (0.972, 1.006, 1.042)). A blend that composes bone transforms
through a scaled component leaves a fixed offset, and the host would carry the same one. Scale alone does not
explain the largest rows (0x73c 204 uu, 0x50 162 uu at about 5 %), so the row also prints the component distance.

Change: `c359_host_bone` (pose_c132.cpp), host, at every settled or frozen final in `pose_snapshot`: row
`card359: host guid 0x... snap N settled|frozen final: bone 0 X uu off body 0; mesh component D uu from body 0,
component scale (...)`, 200 rows per map. No behaviour change.

Reading the run (FED_C2M1): join host rows to client WARN rows by guid.
- Host X within 1 uu of the client's WARN distance for the same guid: the copies match; the client check then
  compares against the host's value (sent in the final, 2 bytes) and the 10-tick wait is skipped when the offset
  equals it. WARN falls to the rows that truly differ.
- Host X near 0 where the client reads over 1 uu: a client-only error; next step reads the client's component
  transform against the host's (component location and scale per guid in both rows).
Either way the futile wait (offset unchanged between two waited ticks) gets an early out in the fix step.


## Outlier 0x3a8 and the floor (2026-11-16, run c423b136a, b136)

b136's scale write works: 82 `WARN card191` rows on 42 guids, median 1.9 uu, p90 7.0 uu. One guid stands out:
0x3a8 (`BP_AI_WarriorBug_C`, rest row Death_07) reads bone 0 1011.8 uu off body 0, and 1011.9 uu again on the second
apply after the rejoin 196 s later, so it is a fixed property of that corpse and not a timing miss.

Ruled out from the logs:

- Bodies: largest body error 0.0 uu at the apply, the freeze and one tick later.
- The blend not running: 11 end ticks, blend weight 1.000, the same distance before and after the blend.
- Where the component sits: over 47 joined guids the host-to-client mesh distance does not predict the offset
  (0x47c is 447 uu apart and reads 1.2 uu; 0x504 is 663 uu apart and has no WARN row).
- Mesh hidden in game on the host: 8 of 22 rest rows have it, 6 of them have no WARN row.
- A held game ragdoll start ("it was already simulating"): 16 guids, 11 with no WARN row.

What sets 0x3a8 apart: its stream delivered 1 sample in 3.8 s (only 0x6a6 shares that, at 1.4 uu), and it has no
`card152: client bone check` row, so `r.bone0` was never stored and the WARN row's distance used a bone name read
at that moment. The log does not say where bone 0 is, so whether the name read failed (the measure is wrong) or
the bones sit 10 m from the bodies (the corpse is drawn in the wrong place) is not known.

Next step (`c359d_patch.py`, scratchpad, 2 edits, log only): the WARN row also gives "bone 0 name read N at (x, y,
z), body 0 at (...), component at (...)". Reading it: name read 0, or bone 0 at the component or the world origin,
means the measure is wrong and the row is noise; bone 0 at a real distant point means the corpse is displaced and
needs a receiver screenshot and a fix. The 1 to 3 uu floor stays open until the outlier is read.


## Outlier read with the c359d row (2026-11-16 16:35, run `c423b144b`, b144)

130 client `WARN card191` rows: 102 under 10 uu, 18 at 10 to 20, one at 20.8, one at 388.1 (guid 0x7b8, WarriorBug).
The 388.1 row answers the open question: name read 1, bone 0 at (197251, 13, 7522), body 0 at (197016, 320, 7553),
component at (197173, 228, 7333). Bone 0 is at a real point away from the body, so the corpse is drawn displaced;
the measure is not wrong. The host's row for the same snap reads 0.0 uu.

It was not permanent here: frozen final snap 2 at 506671360 reads 388.1; the host unfroze the corpse 1.9 s later
(snap 3) and the finals of snaps 4 and 5 read 3.0 and 3.2. In b136, 0x3a8 had no later unfreeze and kept 1011.8 uu
through a rejoin. So: a frozen final can leave the drawn corpse metres from its bodies until the next stream.
Both outliers had the client's stream running while the game's own ragdoll start was blocked ("blocked the game's
ragdoll start on host-posed guid", 1.0 s after the unfreeze for 0x7b8); 0x3a8 had 1 sample in 3.8 s.

Not known: why 10 end ticks with blend weight 1 leave bone 0 where it was. Next step is a row, not a fix: at the
frozen final print the bone's component-space position and the component transform at the apply and at the last
waited tick (a moved component with unchanged component-space bones means the blend did not write; unchanged both
means the bone buffer read is stale). Receiver screenshot of such a corpse still owed. Stays after the playtest.

## Build b187 `920b53a5d0bb7c2e` (2026-11-17)
91 client frozen-final rows; the shots were inconclusive because the players stood on one line through the body. c423 now places both on the +x side, 300 uu apart in y.

## Build b189 `d4181cdc840b944a` (2026-11-17)
Run c423b189a, rows only (shots dropped). 77 frozen-final rows: bone 0 to body 0 at the last waited tick, min 1.0, median 2.7, max 478.2 uu. The residue this card is named for (1.9 to 3.3 uu) is below anything visible. 6 rows are over 100 uu (0xdec 478.2, 0xdf6 261.4, 0xe00 191.0, 0xe0a 314.9, 0xe14 272.9, 0xd3a 153.7). In each one the mesh component itself sits about that far from body 0 (337, 255, 179, 240, 272, 158 uu), the final distance equals the waited-tick distance, and the blend is 1.000. The offset is between the component and body 0 before the freeze, not made by it. Open question for these 6: does the client's corpse show where the host's corpse lies? Next: compare the client's bone 0 to the host's final bone 0 for the same guids.

## Build b190 `d451b08388a464db` (2026-11-17)
Host final row versus the new client freeze row, joined by guid and snap. c423b190c: 105 guids, 5 over 50 uu (0xd8a 283.4, 0xd80 254.7, 0xd9e 231.1, 0xd76 219.1, 0xd94 102.6); every other guid is under 27 uu. c423b190b: 54 guids, 2 over 50 uu (0x7bc 323.7, 0x7f8 145.3). For every far guid, the host-to-client bone 0 distance equals the client's own WARN card191 "bone 0 off body 0" value, and on the host bone 0 is 0.0 uu off body 0. So the client's body 0 is where the host's is; the client's bone 0 does not follow its own body 0 at the freeze (both unchanged from the apply to the last waited tick, blend 1.000). The client shows these corpses 100 to 320 uu from where they lie on the host. Fix direction: at the freeze, when bone 0 is over 50 uu from body 0, put bone 0 back on body 0 and log one row per fix. Prediction: 0 guids over 50 uu in the join.


## Closed (2026-11-17 16:16)

Fixed in b191 (be7204b0fef4a7b1): `c359_mesh_to_body0` in `pose_freeze_step` moves the mesh with `USceneComponent::AddWorldOffset` when bone 0 sits more than 50 uu from body 0 at the freeze.

Evidence (c423b191a): 2 "card359: ... mesh moved" rows, "body 0 moved 0.0" on both. c359join: 93 guids, 0 finals over 50 uu, max 28.9 (0x688). The prediction held. The c448 gate passed on the same build with no crash.

Not done: the residue under 50 uu (max 28.9) is left as is; it is below the visible threshold the card set.

Reopen when: any c359join guid is over 50 uu, or a "body 0 moved" row is nonzero.
