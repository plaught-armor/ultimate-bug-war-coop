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
