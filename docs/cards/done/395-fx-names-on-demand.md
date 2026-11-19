# Card 395: FX name rows on demand (map-start bytes) + card 389 fall count

Efficiency audit finding, 2026-11-15, from `logs/c394b81-host.log`.

## Finding

`frame_assets` (`mod/src/ubw/fx_c136_net.cpp`) sends a peer every name row from `peer.names_sent` to
`g_host.names.n`, whether a row for that peer uses the name or not. The host's name table fills during the load
(its own FX events under the card 320 hold: 2906 rows queued, 0 sent on FED_C3M1), so the join replay ships the
whole table:

| map | name rows | journal rows | Fx bytes, first window |
|---|---|---|---|
| FED_C3M1 | 1538 | 118 | 70754 |
| FED_C3M2 | 892 | 84 | 38624 |
| FED_C4M1 | 806 | 124 | 38978 |
| FED_C4M2 | 483 | 205 | 26146 |
| FED_C5M1 | 589 | 75 | 28110 |
| FED_C5M2 | 1409 | 93 | 61112 |

A journal row names at most 4 strings (`comp`, `socket`, `owner_class`, `placed`), so FED_C3M1 needs at most 472
name rows, not 1538. About 41 B per name row: roughly 60 KB of the 107 KB the host sends in the first window of
FED_C3M1 (Fx 70754, WorldOp 23740, CollisionOp 12107) is names nobody uses yet.

## Change

1. Per peer, a bit per name row (`kNames` 4096 bits) in place of `names_sent`. A name row goes into a frame only
   ahead of the first row for that peer that uses it; the bit is set only when `frame_add` took the name row. If the
   name row or the row using it does not fit, neither is marked or counted as sent wrongly: the using row must never
   reach a peer before its names.
2. Queue rows and journal rows carry their name row indices (up to 4, -1 none) so `host_flush` and
   `c136_join_replay` can do this without parsing row bodies.
3. Every place that resets `names_sent` (peer slot reuse, map arm, join replay if it does) clears the bits. Check
   whether the client's name table expects indices in order; it must accept sparse indices.
4. The join replay log line prints name rows sent to this peer, not the table size. Asset rows stay as they are
   (27 rows, small).
5. Card 389 leftover, same build: `c389_step` (`objectives.cpp`) counts fall time under the card 320 load hold
   (world paused, z constant) and logs `WARN card389 ... no grounded Up player`. Start or restart the fall count
   while `c320_holding()` is true.

## Predictions (one run, `c388-c3m1-to-c5m2`, b82)

- Join replay name rows on every FED map at most 4 times its journal rows (FED_C3M1 at most 472; b81 1538).
- First-window Fx bytes on FED_C3M1 under 30000 (b81 70754), on FED_C5M2 under 25000 (b81 61112).
- Client: 0 `WARN card136` bad row or unknown name lines; client FX replay counts per map equal to b81 within the
  run-to-run spread; digest mismatches 0.
- 0 `WARN card389 ... no grounded Up player` rows that start under a load hold.
- drive rc 0, 0 stall frames, 0 FATAL, WARN classes not new against `c394b81`.

## Result (accepted 2026-11-15, b82 `d4d25f54b139c092`, run `c395b82`)

Drive rc 0, 0 FATAL, 0 stall frames, 0 `WARN card136`, digest mismatches 0, client FX replay counts as b81.
Join replay name rows and first-window Fx bytes per map (b81 in brackets): FED_C3M1 103 of 1538, 13420 B (70754);
C3M2 82, 10914 B (38624); C4M1 120, 17900 B (38978); C4M2 125, 16822 B (26146); C5M1 66, 8854 B (28110); C5M2 80,
11274 B (61112). Card 389 fall count restarts during the load hold: 0 `WARN card389`.

Review (presume defect), attacked: a bad or out-of-table name index (row dropped, counted, warned once); a row whose
names do not fit one frame; the frame-full path (the fuller frame goes first, names and row stay together); the name
mark (only when `chan_send_client` took the frame; an accepted frame is sent or its peer's bits are cleared before
reuse, junior trace of chan.cpp); a returning peer (bits cleared at the join replay). One path left: a refused frame
followed by an accepted one in the same tick would send a row whose name went in the refused frame; `client_hold`
does not change inside a tick, so it is not reachable.
