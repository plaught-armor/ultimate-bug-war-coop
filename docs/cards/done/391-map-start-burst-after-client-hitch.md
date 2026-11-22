# Card 391: map-start `QueuedBits` burst follows the client's load hitch

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: open, traced from logs only (playtest window, no runs). Corrects card 390, which called this burst "the
engine's own first actor replication". That was not supported.

## Evidence (`logs/c388b76-*`, `logs/c388b77-*`)

- `ReplicateActor` wrote about 2.9 kB in the 5 s window of the C5M2 burst (`props` rows); `ClientMessage` was
  191,643 of 194,529 bytes. Actor property replication is not the burst.
- First `stall frame` per map, `QueuedBits` (reliable bunches out): b76 573995 (218), 314113 (175), 287070 (186),
  114959 (148), 133383 (178), 501311 (256); b77 411484 (161), 64960 (119), 97208 (126), 59435 (115), 455620 (203).
  An earlier read of 520,000 to 650,000 for every map was the `CurrentNetSpeed` column, not `QueuedBits`.
  Card 390 halved the mod's bytes and the middle maps' bursts fell 3 to 5 times; the first and last map fell less.
- Each first stall comes 8 to 33 ms after a long client frame ends (`card303: client ... frame of N ms`):
  b76 client 1194 ms ends 424974722, host stall 424974731; 1453 ms ends 425001844, stall 425001874; 1001 ms ends
  425030415, stall 425030435; 732 ms ends 425106742, stall 425106763. b77: 2543 ms ends 427234166, stall
  427234199; 943 ms ends 427254713, stall 427254731; 871 ms ends 427284944, stall 427284952.
- The frame before each first stall was net ready and no mod frame left in between (the pacer held 129 frames at
  the 256 reliable-bunch hold), so 60 to 80 kB entered the connection in one host frame, outside both netbudget
  hooks (`ReplicateActor`, `ProcessRemoteFunctionForChannelPrivate`).

## Hypothesis (not proven)

The client does not read its socket during a load hitch of 0.7 to 2.5 s. The host keeps sending at up to
250 kB/s (the pacer gates on `QueuedBits` and on 256 unacked reliable bunches, not on time since the client last
acknowledged). The client's socket receive buffer (engine default 0x20000 = 131 kB, to be confirmed in this build)
overflows and drops packets. When the client's frame ends it NAKs the lost packets and the host resends every
reliable bunch they held in one frame: the burst. Resent bytes pass neither hook, which matches the gap.

## Predictions for the diagnostic run (b78, lane 2, after the playtest)

1. The host connection's lost-packet counter (`OutPacketsLost` / `OutTotalPacketsLost`, offsets to be found) rises
   in the frame of each first stall and nowhere else at a map start.
2. Burst bits are about the bytes the host sent during the client hitch minus the client's receive buffer size.
3. No rise in the counter: hypothesis dead; next suspect is channel open and NetGUID export bunches at level
   visibility (log `QueuedBits` before and after `UNetDriver::TickFlush` phases).

## Fix candidates, only after prediction 1 holds

- Pacer: hold mod frames while the connection has had no packet from the client for about 100 ms (the engine's
  `LastReceiveTime`), so nothing piles into a socket nobody reads. One rule in `chan.cpp`, covers internet play.
- Larger client socket receive buffer (`UIpNetDriver` receive buffer setting), if the build exposes it.

## Result (accepted 2026-11-15)
Proof, run `c391b78` (slot-diff diagnostic): burst frame t=428211872, QueuedBits -51426 to 406622, OutBytes +61,381 in
63 packets, OutTotalPacketsLost 0 to 70. The client did not read its socket during a load hitch; the host kept sending,
the packets were lost and the engine resent them in one frame.

Fix (b79 `0780e449884acb02`, kept in b80 `2e0c99e96d390d63`): the card 140 pacer holds its queue while the connection
has received nothing for 100 ms (`kPaceSilentMs`, `PaceGate::Silent`; LastTickTime +0x1F8 minus LastReceiveTime +0x1D8).

Runs: `c391b79` 0 `stall frame` lines (b77 36, b76 68), 489 silent holds, `full sends 0`; `c392b80` 0 stall frames,
scenario passed, 0 ledger WARN rows. Longest pacer wait 1502 ms (`c391b79`) and 4784 ms (`c392b80`); the 4784 ms wait
sits inside client frames of 4496 and 4436 ms at the first map load, when the client reads nothing anyway.

Open: the slot-diff diagnostic `c391_sample` (`net_budget.cpp`) is still compiled in; remove in the next build.
