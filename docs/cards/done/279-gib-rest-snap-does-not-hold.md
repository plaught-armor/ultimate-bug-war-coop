# 279 Gib rest snap does not hold on the client (about 5% of rest rows)

Found by senior in run c230-c3m1-d2 (FED_C3M1, build 3 `5e9069094b412c66`), 2026-11-13. Not covered by card 273
(sticky state machine): 101 of the 118 rows are `STGorePartNonSticky` (sticky state -1).

## Evidence (client log)

- Totals: `rest checked 2543, within 2 uu 2421, within 10 uu 2425, worst 509.5 uu`. 118 `WARN card137: client gib
  host part N ... lies D uu from the host's rest point` rows; median 117 uu, 90th percentile 271 uu. Run c242-c3m1
  (older build) had 61.
- The 8 `card201: client gib rest miss` rows name the pattern. In 6 of 8 the distance 1 s after the R row equals
  the distance before the R row's snap: 72.6 against 73.1, 29.9 against 30.3, 25.8 against 26.0, 26.1 against 26.3,
  93.6 against 86.6, 93.5 against 86.5. Body awake at the check 1 in one row, 0 in the others.
- So the part is where it was before `gib_client_pose` teleported it (`gib_c137.cpp:975`) and slept it. A wake by
  contact (the card 201 guess) would leave it at a random place, not the old one.
- Before the snap the part is off because the host sent no M row for the last second (`last M row 1013 to 1059 ms
  before the R row`: the host's part moved under `kGibStillUu` per sample) while the client's part kept its own
  velocity after the last M teleport.

## Not settled statically

Why the snap has no lasting effect on these parts and holds on 95%. The same call moves M rows correctly (`median
<= 5 uu` in flight). Two candidates, one diagnostic row decides:

- A: the teleport is refused or moves nothing (`SetActorLocation` returns false).
- B: the teleport lands and the next physics step writes the body's old transform back, then the body sleeps
  there.

## Step 1 (diagnostic, `mod/src/ubw/gib_c137.cpp` only, next build)

In the R branch of `gib_client_pose`, when the distance before the snap is over 10 uu (cap 20 rows per map): keep
the return value of `g_set_actor_location`, read the part's location back in the same call, and read it again on
the next `gib_client_tick`. Row:
`card279: client gib R snap host part <id> <name>: <d0> uu before the snap, set returned <0|1>, <d1> uu right
after, <d2> uu one tick later; simulating <0|1>, awake <0|1>, root attach parent <name|none>`.

Predictions: A shows `set returned 0` or d1 equal to d0. B shows d1 under 1 uu and d2 equal to d0. If d1 and d2
are both under 1 uu and the check 1 s later still misses, a later mover exists and the row names nothing: report,
no second guess.

## Step 2 (after the row)

- A: fix the call (the reason the row shows). B: sleep the body before the teleport, or stop the simulation, snap,
  and restart it asleep; the choice follows the row.
- By class, whatever the cause: the last second before the R row. The client's part should not travel on its own
  after the last M row; candidates are zeroing its velocity on each M row or a host M row on the R cadence. Decide
  with the step 1 numbers (d0 distribution).

## Proof

Rides the next FED_C3M1 run, 0 extra runs. Done when `rest checked N, within 10 uu N` (0 `lies` WARN rows) outside
parts the host flagged.

Not a play blocker: gore pieces 25 to 500 uu apart between players. Breaks the rule that every visible effect is the
same for every player.

## Build 4 run c230-c3m1-b4e, senior note 2026-11-13

The `card279` diagnostic row is not in build 4 (0 rows in the run, no `card279` text in `mod/src`). Totals of the
run: `rest checked 2601, within 10 uu 2575, worst 179.5 uu`, 26 `lies` WARN rows (run D: 118 of 2543). The
diagnostic goes into build 5 as specified above; the fix still waits on its row.

## Step 1 result and step 2 (senior, 2026-11-16 11:46)

Step 1 rows (b124 to b126, chain runs c423b124a/125a/126a, 36 `card279` rows): every row has `set returned 1`,
0.0 uu right after and 0.0 uu one tick later. So A and B are both out: the snap lands and holds for a tick. The
rows split by `simulating`: 29 rows with the root simulating, 0 of them missed the 1 s check; 7 rows with the root
not simulating, 5 of them missed (back at the place before the snap: 24.8 against 24.7, 18.0 against 17.9, 21.2
against 22.0); the other 2 misses in these runs had no `card279` row (under 10 uu before the snap).

A part's root stops simulating when its significance level says so: `ASTGorePartBase::OnSignificanceChanged`
(exe 0x1451328E0) calls the part's virtual at vtable +0x778, `SetPhysicsEnabled(bool)` (0x1451377C0):
`SetSimulatePhysics(b)` and `SetCollisionEnabled(b ? 3 : 0)` on the component at +0x2F0. Neither function stores a
transform, so the mover back is the engine, when the physics comes back on. Not traced further.

Step 2, patch `c279_physics_patch.py` (gib_c137.cpp only, for b128): on an R row, a part whose root is not
simulating gets `SetPhysicsEnabled(true)` through its own virtual before the snap (then the sleep, as before). The
`card201 ... rest miss` row gains `card279: physics off at the R row N (switched on for the snap, M this map),
simulating at the check S`.

Predictions (chain run, 0 extra runs): `card279` rows all print `simulating 1`; no rest miss row has `physics off
at the R row 1`; `lies` WARN rows fall to the parts under 10 uu before the snap or to 0. Refuted if a miss row
shows `physics off at the R row 1`: then the mover is not the physics switch and the method changes (hold the part
by a per-tick write until the check).

Open after this: the 2 not-simulating rows that passed the 1 s check may have moved back later, unseen (the check
is one read). If the prediction holds they are covered by the same fix.
