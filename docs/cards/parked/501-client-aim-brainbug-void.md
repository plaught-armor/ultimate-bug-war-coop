# 501: Client aim at BP_AI_BrainBug_C voids (c245 line 724)

Status: open (written 2026-11-19, senior). Repeated on b258 (c245b258a) and b259 (c245b259a).

## Finding

c245 line 724, the client's look at BP_AI_BrainBug_C, made 4 attempts with no hit from the aim scan, so the step
voids. The host still completes BrainBug_O1 (by code kill), so the objective chain is not blocked.

## Do

Trace statically before any run: what the aim scan targets on the Brain Bug (root, mesh bounds, a socket), where the
client pawn stands after `host warp client BP_AI_BrainBug_C 900`, and whether the line of sight from there hits the
bug's collision or something in between. Fix the scenario or the aim target.

## Proof (written before the run)

The next c245 run passes line 724 on the first or second attempt, and the aim proof shot shows the Brain Bug.

Reopen when: line 724 voids again.

## Trace (2026-11-19, senior, c245b258a, b259a, b260a, b261a, b262a)

- Results of line 724: b260a and b261a pass on attempt 1, b262a on attempt 3, b258a and b259a void after 4.
  The blockers differ: StaticMeshActor_56, StaticMeshActor_61, Actor_7, and "no hit within 10000 uu".
- The warp (testrun.cpp c75_warp, then b16_landing) asks for 900 uu but lands 480-630 uu from the Brain Bug. b16
  projects each of the 8 candidates with a half box of dist/2 (450 uu), and keeps the one whose path to the actor's
  navmesh point is shortest, which pulls the landing in toward the bug.
- In the failing runs the pawn falls after the warp. b262a cmd 269: navmesh landing z 6737, the pawn is set at 6837,
  and it is at 6200 one step later. b259a: 5877 to 5220 and 5080; b258a: 6837 to 6290, 6015 and 5623. In the passing
  runs it stays (b260a: 5977, then 5977). The fall puts the pawn under the rim mesh (StaticMeshActor_56 at 106-180 uu,
  pitch 2 to -34), so the look hits the rim, not the bug.
- Attempt 3 in b262a passed because that warp started from a standing pawn at 6837 and the look ran before it fell.
- Cause: the warp's navmesh landing is on a ledge or a surface that does not hold the pawn, and nothing checks the
  line of sight to the target. The step is a test harness problem, not a game bug.

## Fix (test harness, testrun.cpp b16_landing)

1. Among the candidates with a path, prefer one with a clear visibility trace (kEccVisibility) from the landing eye
   point (mesh z + pawn eye height) to the actor's bounds centre. The shortest path breaks ties. Log
   "warp: landing has line of sight: yes|no" in the landing row.
2. Each candidate gets a down trace (kEccWorldStatic) from 100 uu over the navmesh point. A candidate with no static
   floor within 150 uu under it is skipped (a ledge or a dynamic surface).
3. No new scenario commands. Line 724 stays as is.

## Proof (written before the run)

The next c245 run passes line 724 on attempt 1 or 2; the landing row says "line of sight: yes"; the client pawn's z
after the look is within 50 uu of the landing z; the c245-boss-00 shot shows the Brain Bug.
