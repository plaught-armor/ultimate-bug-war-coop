# 328 NPC copy z clamp repeats on the same spot

Senior static audit 2026-11-14 (`enemies.cpp`, class 7 second pass). Not a user report yet; it likely shows as a bug
or trooper copy that bobs about 200 uu on the client.

## What the logs say (played client, play build 4 `5e2ce00acad8efa3`)

- `logs/playtest-menu-20261113-184603.log`, map totals row: `drifted copies set back to the host's z 508 times`,
  `local Falling held at the host's mode 5711 times`.
- The logged rows repeat on one spot: 14 rows for one `BP_AI_WarriorBug_C` with the host's last location
  (-17947, -45812, 937), each `velocity (0, 0, 0)`, 205 to 216 uu above the host's z, about 0.5 s apart. Other
  spots repeat 3 to 6 times. Movement base is `none` or `LandscapeHeightfieldCollisionComponent`.
- `logs/playtest-menu-20261113-183002.log`: the same for a `BP_AI_Trooper_Morita_C` at (-8389, 23416, 101).

## What is settled

`c132_drift_clamp` (`enemies.cpp:2264`, after the client's `SimulateMovement`) sets a Walking or NavWalking copy's z
to `ReplicatedMovement.Location.z` when the two differ by more than 200 uu. It compares z only and does not look at
what the copy stands on. The clamp was written for a copy lifted by a client-side ragdoll (hb25). Here the copy
stands still on the landscape, is set down 205 uu, and is back up within 0.5 s: the clamp and the client's own floor
disagree, and the clamp loses every time. The clamp does not hold the copy, it moves it twice per second.

## What is not settled

Why the client's resting z is 205 uu above the host's last replicated z for a standing NPC. Candidates: the host NPC
is in NavWalking on a nav mesh below the landscape surface; the host NPC is emerging or burrowed; the replicated
location is stale (the host stopped sending while the NPC still moved); the copy's xy differs on a slope.

## Step 1 (diagnostic, one build, rides the next run)

Client row, replacing the current drift row (same cap): add the copy's xy distance from the replicated location, the
copy's floor hit z under it (one visibility trace down from the copy), the replicated movement mode byte, and the
age in ms of the last replicated movement update for that copy. Host row for the same guid at the same second (asked
by the client through the existing digest query path or logged by the host for every NPC whose copy clamps, by
guid): the NPC's z, movement mode, movement base and floor hit z.

Predictions: the client's floor z equals the copy's z before the clamp (the copy rests on real ground); the host's z
is below the host's own floor z (host NavWalking under the surface) or the update age is over 1 s (stale location).

## Fix (after step 1; by the cause)

- Host below its own floor: the clamp is wrong for that NPC; clamp only when the copy's movement base is not world
  geometry (the hb25 case: a ragdoll or another character under it).
- Stale location: the copy's z follows the mod's own position rows for that guid, not ReplicatedMovement.
- Either way the clamp must not fire again on a copy it set back within the last second: a repeat is logged as its
  own WARN row with a count per map.

## Sibling sweep

Every client write of a copy's location or mode against the game's own simulation: `detour_set_movement_mode`
(Falling held at the host's mode, 5711 times in the same map), card 104 copies, card 317 projectile copies, pose
rows. Report which of them can fight the client's simulation every tick.

## Done when

Map totals: no copy set back more than once per 10 s at one spot, and a client shot pair 1 s apart shows the copy at
the same height.

## Step 1 rows read (senior, 2026-11-16; `c423b163a` C2M1, `c460b168a` C3M1)

13 client clamp rows on C2M1 (guids 152, 61e, a78, e42), 1 on C3M1 (13a). Every pair says the same:

- Host: Walking on the Landscape, standing, z 169 to 171 uu above its own floor trace (Warrior; Trooper 108). The
  host is on its floor, so "host below its own floor" is ruled out.
- Client: copy 201 to 209 uu above the host's z, xy 0 to 19 uu off, velocity 0, replicated mode Walking. Location age
  600 to 2800 ms and the rows repeat every 600 ms while the host NPC stands still, so "stale location" is ruled out
  too: the location is current, the copy leaves it.
- Client `floor z 0` on every row. `trace_run` returns the hit location only for a blocking hit, so the client's
  trace down from the copy's centre reported a blocking hit with location z 0. Reading (not proven): the trace starts
  inside a Visibility-blocking body that the host does not have at that spot, and the copy stands on or is pushed out
  of that body. Candidates: a client-kept corpse or its corpse box (cards 191, 273), a second copy at the same spot,
  a client-only gib. The copies clamped were all 12000 uu or more from the client's player.

Both predictions of step 1 failed; the fix list above does not apply as written. The clamp still moves the copy twice
per second and still loses.

Step 2 (diagnostic, small, next build batch), client drift row: (a) the floor trace returns the hit's actor and
component names and the start-penetrating flag (`trace_channel` actor_out and comp_out, FHitResult bStartPenetrating),
printed as `floor <z> on <comp> of <actor>[, start inside]`; (b) the copy's capsule half height and the CMC
`CurrentFloor` hit actor and component; (c) the same trace repeated from 50 uu above the copy's top. Prediction: the
named body is a dead NPC copy or its corpse box, and the host has no blocking body there. Fix then goes to that
body's collision on the client (same response to Pawn as the host's), not to the clamp.

## Step 2 rows read (senior, 2026-11-17 11:46; logs of b150 to b182, 536 client rows)

The step 2 prediction failed: no client-only blocking body.

- Floor hit under the copy: Landscape 362 rows, StaticMeshActor 58, instanced meshes 58, mech drop platform 24; a
  pawn or corpse body in only 22 rows. `start inside` 0 rows. The trace from 50 uu above the copy's top hits the same
  floor, so nothing lies between the copy and the ground.
- Numbers of one row (c423b182a guid b26): host z 5500, floor 5328, so the host stands 172 uu above the floor with a
  175 uu capsule half height: on the ground. The client copy is 210 uu higher (382 uu above the floor), velocity 0,
  mode Walking, `CurrentFloor` the Landscape, xy 6 to 9 uu off. It is back at +210 500 ms after each set-back, 4 rows
  in a row.

So the copy is lifted by about 205 to 210 uu by the client's own movement step while the floor under it is the same
landscape the host stands on. Not known: which step lifts it. Candidates: `AdjustFloorHeight` on a `CurrentFloor`
with a large negative `FloorDist`; root motion from a montage applied with its z on the simulated copy; a penetration
push from the copy's own mesh or attached body.

## Step 3 (diagnostic, one build, about 40 lines, no new run: rides any C2M1 run)

At a set-back, arm a 30-frame trace for that guid (one guid at a time, cap 4 per map). Per frame, one row: the copy's
z, `CurrentFloor` `bBlockingHit`, `bWalkableFloor`, `bLineTrace`, `FloorDist`, `LineDist`, the mesh's active montage
name (or none) and whether it has root motion, and the movement mode. Predictions, one of which names the fix:
 - z jumps +200 in one frame with `FloorDist` under -100: floor adjust on a bad floor result; fix is to refresh the
   floor (the game's `FindFloor`) after the set-back instead of writing z alone.
 - z climbs over several frames with a root-motion montage active: fix is to drop the z of root motion on Walking
   copies, as the host's Walking mode does.
 - neither: print the hit of a capsule sweep at the copy's place (the pushing body).


## Encroaching row, first logs (c245b194d, b194, FED_C4M2, 2026-11-17)

Two traces armed:
- guid 1a4 `BP_AI_ArcherBug_C`, set-back to z 2809: `encroaching 1, adjustment (29.6, 32.8, 0.3)`; z stays 2809.3
  over the trace frames, mode 2, CurrentFloor blocking 0.
- guid 5e0 `BP_AI_WarriorBug_C`, drifted 207 uu in z, set-back to z 2865: `encroaching 1, adjustment (-95.3, 92.1,
  199.8)`. Frame 1 z 2988.5, frame 2 onward 3023.8, mode 2, FloorDist 1.6, no montage (root motion translation -1).
So the lift matches the third candidate: the set-back spot encroaches a body, and the penetration push lifts the copy
about 160 to 200 uu within 2 frames. No floor-adjust or root-motion cause. Fix direction (senior to decide): do not
write z alone at the set-back; use the encroachment adjustment's xy, or skip the set-back while the spot encroaches.

## Fix b196 `d42815991b07ad70` (2026-11-17)
`c132_drift_clamp` (enemies.cpp) now probes `g_c46_encroaching` at the set-back spot before writing it. If the spot encroaches, the set-back is not written: the row says "skipped (encroaching)" and `g_c38.drift_skipped` counts it (card038 summary: "not set back (encroaching) N times"). Otherwise the row says "set back" as before. Siblings: `c132_drift_clamp`, called from `detour_simulate_movement`, is the only set-back writer, and it covers every simulated character copy (NPCs and troopers), so there are no other writers to change. Prediction: no climb rows with an adjustment over 50 uu z.

## Closed (2026-11-17 18:43)

Fixed in b196 (d42815991b07ad70): the set-back is skipped when the spot encroaches (UWorld::EncroachingBlockingGeometry), sibling set-back writers in enemies.cpp too. Evidence, c245b196a client: guid 222 BP_AI_WarriorBug_C drifted 279-298 uu z, "skipped (encroaching)" 16 times (adjustment ~(104,-22,3)), 0 set backs, 0 climb rows; the drift lines stop after 16 frames when the host update caught up. Run 0 EXCEPTION/FATAL.

Not done: the client shot pair 1 s apart; the map-end card038 summary was not printed (run ended by scenario quit).

Reopen when: a map total shows a copy set back more than once per 10 s at one spot, or a client shot shows a copy floating or sunk while the host copy stands.
