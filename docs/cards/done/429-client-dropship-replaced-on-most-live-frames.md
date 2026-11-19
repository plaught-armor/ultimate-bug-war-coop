# 429: client dropship copy is re-placed along its spline on most live frames

Status: fix written (`c429_dropship_dist_patch.py`), proof owed. Found 2026-11-16 11:53 by the senior, efficiency
audit of run c423b126a.

## Finding

- Client totals, c423b126a: `473 live frames (372 fields applied)`. The host sends one L frame per second for each
  moving placed dropship; the client's copy flies its spline by its own tick between frames.
- The logged live rows (22 in steady flight at about 8500 uu/s, runs c423b125a and c423b126a) show the copy 120 to
  570 uu off the led target, 14 to 65 ms of travel, on 16 of 22 frames. One 60 fps frame at that speed is 141 uu, so
  this is the timing noise of one frame (host sample time, arrival frame, lead step 0 to 23 ms), not drift.
- `ds_client_snapshot` (dropship.cpp) writes the spline distance whenever the offset is over the fixed
  `kDsTolerance` 100 uu, so the copy jumps 1 to 4 frames of travel along its path about once a second: a visible
  stutter on a ship in flight, and no gain in accuracy.

## Fix

The distance write and the row's "same" text use `dist_tol = kDsTolerance + |speed| * kDsSlackMs / 1000` (50 ms of
travel on top of 100 uu; 525 uu at 8500 uu/s; 100 uu parked, so landing and parked placement are unchanged). The check
frame (C) already uses a speed-scaled tolerance.

## Done when

One chain run (`c399-fast-chain`, travel to FED_C3M1): the client totals row has fields applied under a quarter of
the live frames (b126: 372 of 473), the dropship check rounds still read `N of N match`, and no new
`WARN card159` row. Refuted if fields applied stays over half: then log which field each live frame wrote.

## Not changed

L frame bytes (165 B a row, 473 frames in b126 = 78 kB): the frames carry real changes in speed, so sending fewer was
rejected.
