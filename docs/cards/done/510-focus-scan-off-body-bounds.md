# 510: The focus scan misses actors whose bounds sit off the body

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: Opened 2026-11-19 by the senior, from card 504 runs c504b2, c504b3 and c504b4 (DLL 4c265abd554c8232). Test-only code
(`testrun.cpp` b14 scan). Goes into the next real-bug build batch, not a build of its own.

## Finding

- `b14_start` (testrun.cpp:1249) builds its 26 points from `g_actor_bounds(actor, false, ...)`: the bounds centre,
  then a 5x5 grid on the face toward the eye (`b14_points`, :1167).
- BP_BaseMech_C (FED_C2M1 RallyPoint_Marker board step):
  - The whole-actor bounds centre sits about 1850 uu SW of the body. The extent is 1369 1772 350.
  - So every point is beyond the 250 uu interactor trace (`kInteractorRange` +0xF8).
  - Only a stand whose line to the centre crosses the body focuses.
- Board results:
  - c504b2: try 2 passed by luck of side.
  - c504b3: VOID on 0 of 3 tries.
  - c504b4: try 1 `look 26: aim hit Landscape_1 ... 9748 uu, pitch 3.0, focus none, not focus=BP_BaseMech_C`; try 2 passed by luck.
- Lessons 6 and 7 in `docs/reference/objective-routines.md`.

## Do

1. In `b14_start`, after `b14_trace_points` returns 0 hits, trace two more points without turning:
   - the actor origin (K2_GetActorLocation);
   - the root component's bounds centre (its own Bounds, not the whole-actor bounds).
   A hit on either takes the existing centroid/settle path. Log one row that names which fallback hit, e.g.
   `cmd: N aim scan: fallback origin hit` or `... root bounds hit`.
2. Make no change when a bounds point hits: the 26-point path stays as it is for every class it already focuses.
3. Mesh component bounds only if the root component is not the mesh and steps 1 and 2 still miss on the mech. Read
   the mech's root class first and say which one in the review.

## Done when

On the next real-bug batch build, one c504-board-c2m1 run (FAST=1, LANE=2):
- `objective do: board RallyPoint_Marker ... complete on try 1; client replayed`.
- The board try 1 aim scan names a fallback hit, and focus=BP_BaseMech_C.
- Satchel, pickup, marker and presence-actorless steps in the same run stay try 1, as in c504b4. Pickup _3 on the
  kept c423 lines is a known fail, not this card's.
- FATAL/EXCEPTION 0.
Then the Board row in objective-routines.md becomes Proven, and card 504 has every routine kind proven.

## Result (2026-11-19, b284 `62e2e778930a4dd6`, run c504b6)

Closed. Board try 1, client cmd 25: `aim scan: fallback origin hit, root CapsuleComponent`, `hits 1 of 27 ... by
fallback`, `aim: hit BP_BaseMech_C ... 54 uu`, focus BP_BaseMech_C. The objective do completed on try 1, and the client
replayed it. The root is CapsuleComponent and the origin hit, so step 3 (mesh bounds) is not needed. Other all-miss
looks logged `fallback none hit, root StaticMeshComponent` and still focused by the centre or sweep as before. Looks
with a bounds hit logged no fallback row.
