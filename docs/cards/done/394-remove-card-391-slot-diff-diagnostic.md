# Card 394: remove the card 391 slot-diff diagnostic

Accepted 2026-11-15. Build b81 `4ed3c7548739ccce` (lane 2).

The card 391 diagnostic (`C391Snap`, `g_c391`, `c391_diff_log`, `c391_sample`) is gone from `net_budget.cpp`. Kept:
the connection offsets, the `silent_ms` fill for the pacer, `packets lost` in the stall line and `silent client` in
the pace line.

Run `c394b81` (`c388-c3m1-to-c5m2`, 210 s): drive rc 0, 0 `card391:` lines, 0 stall frames, silent client holds on
12 of 20 pace lines, `full sends 0` on 20 of 20, 10 `card392` lines, no level-end `try 2`, 0 FATAL, no dumps. WARN
host 23, client 9; no class new against earlier runs.
