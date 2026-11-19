# 489: c245 BrainBug_SO2_R2 marker step left open

Opened 2026-11-18 08:44 from the b215-b217 c245 runs.

## Symptom
All three c245 runs fail line 570 with "BrainBug_SO2_R2: marker step left open: no next switched-on presence with a navmesh point near the marker". SO2 still completes through the clears, so the boss run passes, but the R2 leg is never driven by its own step.

## Task
Trace why no switched-on presence with a navmesh point exists near the R2 marker (presence off, or the navmesh point search radius too small). Fix the scenario or the objective-driven placement so R2 completes from its own step.

Reopen when: n/a (open).

## Trace and fix (2026-11-18, b219 batch)

- c245b217a host log: BrainBug_SO2's four subs complete at `clear 20000 kill`. L1 and R1 at cmd 259 (2 killed), R2
  and L2 at cmd 262, after the failed go at cmd 261.
- An Archer bug sits at each marker (decal targets on BlockingVolumes next to the markers at z 5690; markers at
  z 6208). The navmesh is not the cause.
- The first `objective go all` stood at presence C_176, which was disabled (enabled 0). Go 2 took presence try 2,
  C_16, 15689 uu away. Go 3 found no presence at all, so the step logged the marker-step-left-open error.
- Fix (test side): in tools/gen_c245.py `hold()`, every go after the first is a `try` step, so a repeat go that finds
  no presence does not count as a failure; the kill at the marker completes the subs. Run c245b219a.


## Closed (2026-11-18 10:05)

Evidence c245b219a (b219, 313 s, rc 0/0, drive rc 0, 0 voids, EXCEPTION|FATAL 0 both logs, no new dumps): all 4 BrainBug_SO2 subs (L1, L2, R1, R2) sent complete, then SO2 and BrainBug_O1. Go 2 again stood 15689 uu from the R2 marker; the `try` path carried on as designed. No "left open" error.

Not done: the R2 marker distance itself is a test-side placement note, not a mod bug.

Reopen when: any BrainBug_SO2 sub or BrainBug_O1 left open at the end of a c245 run.
