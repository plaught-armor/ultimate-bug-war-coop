# 484: gib parts not at rest after 30 s (card273)

Status: Open.

## Seen

c244 run c (b205, FED_C4M1): 21 card273 "not at rest after 30 s" rows, new on this map (none in c127-c4m1-r2).

## Do

From the rows, name the part classes and where they are (stuck in geometry, sliding on a slope, or still moving on the host too). If the host's part is at rest and the client's is not, fix by class in the gib rest path (card 137 R rows).

## Done when

A C4M1 run with 0 card273 not-at-rest rows, or every such row matched by the host's own part still moving.

## b206 findings (c244b205c logs)

All 21 rows are on the host, 0 on the client. Every one is ASTGorePartSticky state 2 (wall slide) or 3 (ceiling) with
that state's own timer at 0.00: the sticky tick that counts the timer and moves the part never ran, so the part cannot
move, and its R pose ("rested where it is") is correct. Named by card148: p195/p196 STGorePartSticky_StaticMesh at
(-25859, -87840, -521) and (-25861, -87842, -541), state 3; p284 at (-8895, -76277, -676), state 3; the other 18 are
past the card148 cap. Gib overflow 0; 894 rest rows sent.
Coverage noise. b206 (gib_c137.cpp gib_sticky_rested): a state 2 or 3 part with its timer at 0 counts as at rest, so
the WARN keeps only running state machines.

## Closed (2026-11-17 22:49)

All 21 b205c rows were host parts in state 2 or 3 with that state's timer at 0 (at rest); gib_sticky_rested now treats that as rested (b206 a287f0f8996280df). c244b206a: 21 -> 2 rows, parts 428 and 471 in state 2 with slide timers 1.61/4.85 and 2.27/4.74 s, really still sliding on the host at 30 s, so the rows are correct.

Not done: nothing; the 2 remaining rows are true.

Reopen when: a not-at-rest row for a part whose host state timer is at 0, or a client part still moving after the host part rests.
