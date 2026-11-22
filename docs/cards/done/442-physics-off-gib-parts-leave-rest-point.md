# Card 442: gib parts with physics off leave the host's rest point

**Closed 2026-11-16 18:55 (senior): step 2 (hold sleeps the body) passes on b145 `851ac6a55c0aed2b`, run `c423b145a` (FED_C2M1): client rest checks 279/376/231 all within 2 uu, worst 0.0 uu, 0 `WARN card137` (b144a: worst 16.3 uu, 3 WARN). Not seen in the run, so not covered: an awake part falling after the hold ends.**

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: Opened 2026-11-16 15:12 by the senior, from run `c423b138a` (b138, FED_C2M1; first written here as FED_C5M2, the logs show C2M1). Follows card 279 (accepted on b128
with 1 residual part).

## Finding

Client rest check: 583 checked, 572 within 10 uu, worst 102.9 uu. All 8 misses over 10 uu (host parts 207, 208,
209, 934, 935, 936, 941, 944; 12 to 103 uu) share one history:

- `card201 ... rest miss`: `physics off at the R row 1`, `simulating at the check 0`, `body awake at the check 0`,
  last M row 1050 to 1084 ms before the R row.
- `card279 ... R snap`: the snap landed (0.0 uu right after, 0.3 uu one tick later, simulating 1, awake 1, no
  attach parent).
- At the check 6 of 8 lie at their distance from before the snap (62.7 then 62.9; 88.5 then 88.5).
- The host logged the same parts as `rests with nothing within 40 uu below`: the host's own part froze in the air
  (physics off by the host's significance level), and the client's copy froze at another place.

So card 279 step 2's prediction is refuted as that card wrote it: the R row switches physics on, the snap lands,
and within 1 s the part is not simulating again and is back near its old place. The M row teleports did not hold
on these parts either (32 M rows, still 62.7 uu off at the R row).

Static trace, exe: `OnSignificanceChanged` (0x1451328E0) calls `SetPhysicsEnabled` only when the level's flag
differs from `bIsSignificancePhysicsEnabled` (part +0x31C); `SetPhysicsEnabled` (0x1451377C0) is
`SetSimulatePhysics` plus `SetCollisionEnabled` on the mesh at +0x2F0, no transform write. `PoolingActivate` and
`PoolingDeactivate` (0x145134060, 0x145134100) also call it. `ASTGorePartNonSticky::Tick` and
`UpdateFloorDetection` write no transform. The mover is not found statically.

## Fix, step 1 (hold and diagnostic)

Patch script `c279h_patch.py` (senior scratchpad, 7 edits in `mod/src/ubw/gib_c137.cpp`, `--check` passes on the
b138 tree with c439d applied). For a part whose physics was off at the R row, each client tick until the 1 s
check: if it lies over 2 uu from the rest point it is put back (location and rotation, teleport). The first time
per part logs `card279: client gib host part N ... first found D uu off its rest point T ms after the R row ...:
simulating S, awake A, significance physics flag F; put back` (12 rows per map). Map-end row
`card279: client gib holds (...)`. The check reads before that tick's hold.

## Done when

On a run with gib-heavy fights (C5M2 nuke blasts): 0 `WARN card137 ... lies N uu from the host's rest point` rows
on parts with `physics off at the R row 1`, and the holds row shows a small put-back count per part (a one-time
mover). Predictions to read from the first-found rows:

- T about the same on every row and `simulating 0`: one event switches physics off and moves the part; next step is
  to find its caller from T and block or follow it.
- Put-back count near the tick count (about 60 per part): a per-frame mover; the hold does not fix it and the
  misses stay; next step is a per-part trace of who writes the root transform.
- Misses stay with 0 parts found off the point before the check: the move happens in the check's own frame; widen
  the diagnostic.

## Result of step 1 (b144, run `c423b144a`, FED_C2M1, scenario `c423-gates-c2m1.txt`, 2026-11-16)

Client: `physics switched on at the R row 136, parts found off their rest point before the check 5, put back 235
times` (about 47 per part: the per-frame branch). All 5 first-found rows: 2.7 uu off, 72 to 75 ms after the R row,
`simulating 1, awake 1`. Totals `rest checked 387, within 2 uu 380, within 10 uu 384, worst 16.3 uu` (b138 worst
102.9); 3 `WARN card137` (parts 76, 536, 540, 16.1 to 16.3 uu).

Cause of the rest: the part is not moved by an unknown writer, it falls. The R row switches physics on, the sleep
does not last, and the hold's teleport keeps the body's velocity. After 1 s of fall the speed is 980 uu/s, one
60 fps tick of that is 16.3 uu: the measured miss.

## Fix, step 2

Patch script `c279s_patch.py` (senior scratchpad, 1 edit in `gib_c279_hold`): the hold sleeps the body after the
put-back, which zeroes the velocity. Predictions for the next gib-heavy run: worst rest miss on a physics-off part
under 2 uu, 0 `WARN card137` on those parts; the put-back count stays near the tick count if something wakes the
body each tick (then the next step is the waker), or drops to a few per part if the sleep now lasts.

Not covered: after the 1 s check the hold ends, and an awake part then falls to the ground on the client while the
host's part stays where its physics was switched off. The check cannot see that. If step 2 shows the body woken
each tick, the fix moves to switching the part's physics off again at the rest point instead of holding it.

## Result of step 2 (b145, run `c423b145a`, FED_C2M1, scenario `c423-gates-c2m1.txt`, 2026-11-16)

Same scenario as `c423b144a` (no C5M2 run was used for step 1 either). Client holds rows:

- objectives command: physics switched on at the R row 167, parts found off their rest point before the check 2,
  put back 2 times;
- map change: R row 262, found off 2, put back 2;
- map change: R row 41, found off 0, put back 0.

One put-back per part found off: the sleep lasts, nothing wakes the body each tick. card137 totals: rest checked
279/376/231, all within 2 uu, worst 0.0 uu in each window; 0 `WARN card137` (b144a: worst 16.3, 3 WARN). Step 2
passes on the C2M1 run. The "Not covered" case (an awake part falling after the hold ends) was not seen in this run.
