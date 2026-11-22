# Card 590: scenario goto sticks on geometry at the Door Gunner seat landing spot (FED_C3M1)

Found in c587b422a and c587b422b (b422 `2ad394a93d9e34bf`, FED_C3M1 FAST, lane 2). After the client's Door Gunner
seat leaves, the client trooper lands near (-20400, -31600, 498). `client goto actor BP_PlayerCharacterMI_C 150`
toward the host then sticks on geometry; re-path, jump and side step do not free it. The trooper itself is fine
(card490 MI row, W walk moves it). This is a test-driver movement fault, not a game or sync fault.

## Log rows

- c587b422a client: goto from (-20321,-32086,432) to (-20610,-31509,498), navmesh 6 points, 1597 uu. Moved 381 uu by
  t=172539531, then stuck at (-19928,-32093,445) at t=172541547; re-path and jump did not free it.
- c587b422b client: goto from (-20495,-31658,498) to (-20698,-31756,485) at t=173440605, navmesh 9 points. Moved 87,
  then 72 uu. t=173460025 "stuck 6 of 6 at -20460 -31518 498, point 2/9, trying side step". t=173462442
  "WARN cmd: 32 goto: gave up (timeout) at -20488 -31518 498, yaw 141, point 2/9, goal 95 uu past the navmesh end";
  end distance 317 uu, 6 stucks.
- Logs: logs/c587b422a-client.log, logs/c587b422b-client.log, logs/c587b422b.drive.

## Next step

Not chased (senior, 2026-11-21). When taken: read the goto steering near point 2 of the path (is the path point
inside or behind a prop the navmesh misses?), and check whether the stuck spot is the same prop both runs. Until then,
scenarios warp the client to open ground before a goto here.

## b429-b431 diag runs (2026-11-21)

- c590b430a (b430): the goto reached, but crawled (803 uu in 11.8 s at point 2/3, no stuck row).
- c590b431a (b431, progress row with point, W state and W presses): the goto from (-20449 -31956 467), a 10-point
  1584 uu path, walked 641 uu in 4 s, then stuck at (-20728 -31386 493) steering to point 10/10 (-20505 -31592 411,
  303 uu 2D) with W held and 0 W presses, moving 17 uu per 2 s. So the steering did not stall (W stayed down):
  something blocks the walk. Re-path and jump failed; the side step freed it to (-21231 -31692), where it stuck
  again; timeout at 22.3 s, 5 stucks. The give-up traces read clear, but they run along the view yaw at the final
  spot (after the side step), not at the first stuck spot, and they keep no actor. The end shot
  (client-c590b431a-c590-after-goto-client.png) shows a live Warrior Bug in front of the client: the only clear was
  `host clear 4000 kill` about 200 s earlier.
- b432 diag: each stuck row is followed by two "stuck blocker" rows (knee, waist) traced toward the steered point,
  naming the blocking actor class, the bearing and the view yaw. Proof run c590b432a
  (tools/scenarios/c590-c3m1-b432.txt) names the blocker: a bug class means a scenario gap (clear before the goto),
  a level mesh means geometry, both clear with the yaw off the bearing means steering.

## b432-b434 results (2026-11-21)

- c590b432a, c590b433a, c590b434a (C3M1): at every stuck, all seven blocker probes (ankle, knee, waist, waist left
  and right, waist reversed) read "clear past 150 uu, no actor" with the Pawn profile, which hits pawns and level
  geometry. The floor probe hits Landscape 95-99 uu under the pawn centre each time, so the capsule stands on the
  ground. The view yaw matches the bearing to the steered point (within the 360 wrap), so steering is not off.
- b434 stuck movement rows (client, cmd 34): 4 of 5 read `MovementMode 1, floor flags 0x0, velocity (0, 0, 0)` with
  full acceleration (-4096, -24, 0) or (-3563, -2020, 0). One (stuck 3, after the side step) read floor flags 0x3
  and velocity (-96, -7, 0). Walking with no valid floor and zero velocity under full input is what
  UCharacterMovementComponent gives when each move starts in penetration: the sweep is rejected, the velocity is
  rebuilt from a zero delta, and the floor is not found. A line trace that starts inside a shape never reports it,
  and the reversed waist probe only covers the bearing line, so the probes cannot see a shape around the capsule.
- Not a scenario gap from bugs: no live bug actor in any probe; the Warrior Bug death rows at t=182854219 (after
  the give-up) put the nearest bug 110 uu off the bearing line.
- MaxWalkSpeed 700, MaxAcceleration 4096, MaxStepHeight 25 (card019, card124 rows): no speed zeroing.

Next step (not built): one capsule overlap query at the stuck spot (the pawn's own capsule, Pawn profile) that names
each overlapping component and its owner class, to name the shape the pawn starts inside. Candidates to confirm or
rule out: the seat turret actor the client just left (the goto starts right after the seat return), a client-only
collision left by the seat exit, or level geometry the navmesh walks through. One build and one C3M1 run.

## b444 overlap diag (2026-11-21)

- At each goto stuck (after the stuck blocker rows), `c590_stuck_overlaps` (objectives.cpp) runs the engine's
  UWorld::EncroachingBlockingGeometry for the live pawn at the stuck spot: the pawn's own capsule and collision
  profile, the pawn itself ignored. The card 046 OverlapMultiByChannel detour lists what the query returns:
  `card590: <side> cmd N overlaps at the stuck spot, channel C: n (blocking hit b)`, then up to 8
  `card590: ... overlap i: owner <class> <name>, d uu; response r (block 0/1); <component>` rows, then
  `card590: ... stuck spot (...): EncroachingBlockingGeometry e, adjustment (...), probe k of 12`.
  The cap is 8 rows per stuck and 12 probes per run.
- Prediction (c590b444a, tools/scenarios/c590-c3m1-b444.txt): if each move starts in penetration,
  EncroachingBlockingGeometry reads 1 with a non-zero adjustment, and a block 1 overlap row names the shape: the
  Door Gunner seat or turret, a client-only collision from the seat exit, or level geometry. If
  EncroachingBlockingGeometry reads 0 at every stuck, penetration is ruled out for this card.
- Caveat: the engine shrinks the capsule by a small epsilon for this test, so a contact that is only touching may
  read 0.

## c590b444a result (2026-11-21, b444 dwmapi e7f47ff60a371a65)

- Client cmd 16 (the goto after the seat return) stuck 5 times at (-20265, -31375, 506), point 2/2, W down.
- Every stuck spot probe read EncroachingBlockingGeometry 1 with a non-zero adjustment, for example (-27.0, 26.8,
  -5.0) and (22.8, -117.7, -97.6).
- The only overlap with block 1 at each probe is a live `BP_AI_WarriorBug_C` CapsuleComponent (profile Custom,
  collision 3), 102-220 uu from the pawn. Its position moves between probes. The other overlaps are trigger and
  overlap-only volumes (BP_PlayerPresence_C, BP_EM_1FactionLoop_C, the spawner boxes, the incendiary pickup
  spheres) with block 0.
- The client's card319 row at the seat return lists six hostile Warrior Bugs within 1544 uu, the nearest at 325 uu.
  Card173 attack sounds from a bug 299 uu away follow.
- card132 shows 11 rounds, all with mismatches 0. These are real synced bugs, not a client-only copy.
- Cause: the bug capsule overlaps the trooper capsule, so each move starts in penetration. That gives the
  MovementMode 1 rows with floor 0x0 and velocity 0. The line probes started inside the bug capsule, which is why
  they read clear.
- This is a scenario gap, not a mod bug: the only `host clear 4000 kill` ran about 200 s earlier, before the
  rack and seat blocks.
- Fix (scenario): `host clear 4000 kill` right before the post-seat goto in tools/scenarios/c590-c3m1-b444.txt
  (parse: 97 steps, 0 bad). Lesson 82 records the rule for every goto after a long block.
- Cap note: the blocking row was overlap 7 of 10. Two rows past the cap of 8 were not shown. A later diag should
  log block 1 overlaps first.
- Same as c590b434a and unrelated to this card: the rack look VOIDs (lines 43, 44) and the card581 wait (line 91).
- Logs: logs/c590b444a-client.log (card590 rows from t=195358286), logs/c590b444a.drive.

## Closed (senior, 2026-11-21)

Closed as a scenario gap. c590b444a named the cause: a live Warrior Bug capsule overlapping the trooper capsule after a
long block with no clear. The fix is scenario only (`host clear 4000 kill` before the post-seat goto, lesson 82) and
rides the next C3M1 run with no proof run of its own. The b444 overlap diag stays in as is; ordering block 1 overlaps
first is a nice-to-have, not carded.
