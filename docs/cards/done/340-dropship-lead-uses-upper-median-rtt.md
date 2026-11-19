# 340 Dropship lead uses the upper median of the round trips

Found in the b24 run of `c338-c337-c1m1` (`logs/c338-c337-client.log`), 2026-11-14.

`WARN card159: client t=327992264 dropship BP_PlayerTransportDropship_C_2 check: ... 1254 uu off (tolerance 1050,
speed 3166, rtt 1601) ...: differs`, 46 ms after a live correction from 3213 uu off. It converged in 2 s (1751, 145,
142, 0).

## Cause (static, `playlog.cpp` `pl_rtt_median`)

The ring held 4 samples at that time: pongs 1 to 4 were 1601, 27, 30 and 2900 ms (pongs 1 and 4 fell in load
hitches). The median returns `v[n / 2]`, the upper middle for an even count: 1601. `ds_lag_ms` (dropship.cpp) caps
it at `kDsLeadCapMs`, and the live correction leads the copy by speed x lag, so the copy was placed ahead of the
host by the capped lag and the next check found it off.

## Fix

`pl_rtt_median` returns the lower middle, `v[(n - 1) / 2]`: 30 ms for that ring. Two hitch samples in a ring of 5
still cannot move it. A ring of 1 or 2 with a hitch sample still reads the hitch; the cap bounds that case.

## Prediction

Same scenario: no `card159 ... differs` WARN with an `rtt` over 100 in its row; the check after each live correction
of the transport dropship is within tolerance.

## Sibling sweep

`pl_rtt_median` callers: `ds_lag_ms` and the two card159 rows only (grep `pl_rtt_median`). The ledger clock offset
uses its own last sample (`c132_ledger_rtt`), a separate question, not changed here.

## b26 result (`d05529011e95739a`, run 14 of `c338-c337`) and second fix

The lower middle was not enough. At the transport's first live row (t=329094208) the ring held 1643, 30, 1106 (join
load hitch, a clean pong, travel load hitch): the middle of 3 is 1106, tolerance 3551. The check 83 ms later still
matched (1 uu), 0 WARN card159, and the estimate recovered at pong 4 (rtt 30, tolerance 643).

Second fix (b27): `pl_rtt_floor` replaces `pl_rtt_median` and returns the lowest of the ring. A hitch only adds to a
round trip, so the lowest is the path's own time; any ring with one clean sample reads clean. A ring holding only
hitch samples (the first pong after a join) still reads the hitch; `kDsLeadCapMs` bounds that.

Prediction (b27): every transport dropship `live` and `check` row after pong 2 reads `rtt` under 100.


## b27 result (`0ebdac007a685d9c`, run 15 of `c338-c337`)

Pongs 1 to 5 read 1432, 30, 2972, 398, 66. Every `BP_PlayerTransportDropship_C_2` `live` and `check` row after
pong 2 reads rtt 30; live tolerances 614 to 689 (and 855 on the first, speed 11615). The prediction held. Accepted
2026-11-14.

Known limit (junior's review): when the path really slows, the floor keeps the old low value for up to 5 pongs, so
the tolerance is too tight and the lead too short for that long. Stricter, not looser.

The one `WARN card159` in the run is not an rtt fault: card 342.
