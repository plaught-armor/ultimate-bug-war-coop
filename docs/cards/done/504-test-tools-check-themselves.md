# 504: Test tools check themselves; a tool fault never costs a run

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: Opened 2026-11-19 by the senior, on the user's call: "The fact we're still running into run issues is bad design."
User choice: do it now, in the same build as the next real-bug batch (cards 263/252 and 502), not as a build of its
own.

## Why

- Card 501 alone: b263 to b267 went to the scenario's boss warp, and four runs voided at the same line
  (c502-c4m2 line 761). Cards 243, 244 and 245 had the same history before (warp, aim and placement faults).
- The root faults are in the design, not in any single actor:
  1. `warp` (c75_warp / b16_landing, testrun.cpp) computes a landing from navmesh and bounds math, then trusts it.
     Nothing checks in the game that the pawn ends up standing, on a floor, with sight to the target. Every new actor
     shape (a boss body bigger than the ring, a ledge, a platform with no navmesh) breaks it, and the break shows one
     full run later.
  2. Scenarios are whole-map scripts of hundreds of lines. A tool step deep in one decides the verdict, and the only
     way to test a tool fix is a full run (build, launch, load, drive to the line: 8 min to 1 h per try).
  3. A failed look gate (hit=, focus=, within=) VOIDs the run even under `try` (drive.py:24-31, :451). The gate's
     purpose is right: actions after a failed aim must not count as proofs. But it turns a tool fault into a run
     verdict.

## Do

1. **Warp verifies its own landing, in one command.** After SetActorLocation, the command watches the pawn on later
   ticks (bounded, about 1 s at most):
   - MovementMode walking (not falling);
   - z within 50 uu of the landing;
   - when the warp has an actor target, the eye Visibility trace hits that actor (the same test b16_landing uses for
     sight).
   On a fault, the command tries the next ranked candidate (bounded: every candidate once), then returns one result
   row: `warp: landed at (...) after N candidate(s): standing, sight yes` or `warp: no candidate held (reasons per
   candidate)`. The landing ring starts outside the actor's bounds (radius = max(dist, bounds reach + 100)), so a big
   actor's body is never inside it. This applies only to a pawn target (is_a APawn, as in b270; the Brain Bug is a Character): every
   other target (BP_PlayerPresence_C, volumes, markers, pickups) keeps the given dist, because the pawn must land inside it
   (c502b269a: a 2488 uu ring outside presence C_54 left Plaza_O1 open). Scenarios drop `retry N host warp ...` wrappers once this is in.
   Server-copy warps (client pawn) check on the host's copy; the client's own row follows on the client side as now.
2. **One short scenario per bug proof.** A bug's proof run starts at its objective (the game's own travel and
   checkpoint, read live per [[objective-driven-tests]]), does only that bug's steps, and is under 5 min. Map soaks
   (c243 to c247) stay discovery runs and never gate a bug build.
3. **Tool faults never void.** A failed look gate under `try` skips the gated actions and records them as "not
   proved" in the drive summary (step, gate, detail). The run verdict comes only from proof lines (non-try waits and
   asserts). The summary lists "not proved: N" so a skipped proof is never mistaken for a pass.

## Done when

- One run on the new build: the c502-c4m2 boss round (line 761) logs one warp result row with "standing, sight yes",
  or "no candidate held" with reasons and the run still ends PASS/FAIL on its proof lines (not VOID).
- drive.py: a failed `try ... hit=` gate shows as TRY-FAIL plus "not proved" rows, and no VOID.
- No new build is spent on warp or aim tuning after this card.

## Not in scope

- Bug-mode tools. Coordinate tables (user rule: objective steps read the live objective).

## Closed 2026-11-19 (senior, junior report)

b277 to b279 put warp self-checks and fallbacks in place. c502b280a passed: line 757 logged "none of 15 ranked ... sees the actor ...; old landing", and the run ended on its proof lines with 71 not-proved try rows and no VOID.
