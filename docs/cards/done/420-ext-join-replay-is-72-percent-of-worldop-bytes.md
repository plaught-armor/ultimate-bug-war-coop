# Card 420: the ext join replay is 72% of WorldOp bytes and 63% of the join burst frames

Status: accepted 2026-11-16 (senior wrote and ran it; b117 `c9b17057498236df`, run c420b117a; still in b119 `6a3adf36b52871d8`).

## Evidence (run c408b115a, b115 `d552217f60765a2b`, host log)

- `c132_ext_replay` (`mod/src/ubw/ext_c132.cpp:685`) sends one WorldOp text frame per tracked ext actor to every
  joining PC, changed or not: `card137: host ext join replay`: 219, 231, 171, 203, 225, 251 rows on the six mission
  maps, 1300 rows in the run at about 80 B each (`netbudget ... WorldOp 25625 B / 319 rows = 80`), about 104 kB of
  the run's 144430 B of WorldOp and 19% of all 541869 B the host sent on the channel.
- The same window's other WorldOp rows: 68 journal rows plus 2 `R` rows (`c98_replay`), 35 live rows.
- The join burst is paced: `pace: ... queue peak 349 frames 57860 bytes, longest wait 1833 ms`. 219 of those 349
  frames are ext rows, so every later row of the burst (collision, Fx journal, objectives) waits behind them.
- The rows change almost nothing on the client: of 384 logged `card132: client ext apply` lines, 383 are
  `apply (already equal)`; the one difference is `BP_PlayerPresence_C_64 0x07000001 -> host 0x07000000`.
- Each received row also makes a `ClientRow` the client's 250 ms retry sweep walks (219 to 251 rows per map).

## Why the rows cannot simply be dropped

Card 137 3a: the host's baseline is its own first read, and a fresh client load can hold other per-instance values,
so "send only what changed on the host" misses a client-side difference. The client must still compare every actor.

## Fix (one build, lane 2)

1. Host: `c132_ext_replay` sends packed frames instead of one frame per actor: a new WorldOp kind (`x`) whose text
   is base64 of repeated (32-bit hash of the `c50_name` string, 32-bit `ext_value`), as many pairs as fit in
   `kChanTextMax` (about 130 pairs per frame; 251 actors fit in 2 frames, about 11 chars per actor).
   Two tracked actors with the same hash (checked in the host's own table at replay) go as the old `X` rows.
2. Client: on an `x` frame, hash the names of its own ext actors once per frame batch (the same class set the host's
   baseline poll walks), look each pair up, and compare `ext_value`. Equal: nothing, no `ClientRow`. Different: make
   the `ClientRow` from the client's own actor name and location and apply as `c132_client_ext` does today.
   A pair with no local actor: kept as a pending (hash, value) entry, retried on the existing 250 ms sweep, WARN with
   the hash after the existing try limit.
3. Totals: the client map row gains "packed pairs received, equal, applied, pending"; the host line keeps
   `card137: host ext join replay` with the frame count added.

### Design as written (2026-11-16, before any run; replaces the details of steps 1 to 3 where they differ)

- Frame text is `x n hash value hash value ...` in decimal, 55 pairs per frame at most; the card 150 codec packs each
  number as a varint, about 11 bytes per pair. No base64: the row name field holds 192 chars only, and decimal needs
  no new codec. 219 actors are 4 frames, about 2.4 kB.
- The hash is FNV-1a of the `c50_name` text only. Place is left out because some of these actors move.
- The client walks every level actor once per frame batch (table kept 1 s, weak pointers) with the host's filter
  (ext class, placed, not replicated). A pair that resolves always gets its `ClientRow`, equal or not, so the 250 ms
  enforce pass covers the same actors as today. Equal pairs write nothing and log nothing.
- A hash two client actors hold, or a class row that differs from the value's row: counted, `WARN card420`, not applied.
- `c98_receive_world` hands an `x` frame to `c420_client_ext_pack` before the row parse; the ledger gets one result
  per frame, so the per-actor ledger keys of the old join rows are gone (risk: the digest's "row newer than sample"
  skip no longer covers an actor the join frame changed; expected effect is one compare round at most).
- Totals: `card420: client ext pairs` row per map (frames, pairs, equal, refused, pending, own table size, walk cost).

## Predictions (written before any run)

- `netbudget` join window: WorldOp rows drop from about 319 to about 107 (70 replay + 35 live + 2 packed), WorldOp
  bytes in that window from 25625 to under 10500 (decimal pairs: 2 more frames and about 3 bytes more per pair
  than the base64 estimate; rows about 109).
- `pace: queue peak` in the join window drops from 349 frames to about 132, and `longest wait` falls with it.
- Client: `BP_PlayerPresence_C_64` (or that map's equivalent) is still applied; packed pairs received equals the
  host's live rows; pending 0 at map end; digest 0 mismatches; no new WARN class against c408b115a.

## Proof

One c399 fast chain run (batch with the next lane 2 build's other cards): the three predictions above read from the
host and client logs. No screenshot: no visible change is intended.

## Result (run c420b117a, b117; same numbers in c421b119a on b119)

- Run WorldOp 144430 B / 1782 rows to 59452 B / 488 rows (59% less). Map 1 join window WorldOp 25625 B / 319 rows to
  11574 B / 96 rows; the prediction was under 10500 B (missed by 1074 B: 10 actors of that map share a name hash on
  the host and went as name rows).
- Map 1 join `queue peak` 349 frames 57860 B to 116 frames 35318 B, `longest wait` 1833 ms to 113 ms (b119 with card
  421: 59 frames 25870 B, 82 ms).
- Client `card420: client ext pairs map`: 209, 231, 203, 221 pairs, all equal, 0 refused, 0 pending; own table walk
  0.2 to 1.4 ms, one per map. `BP_PlayerPresence_C_64` and `_8` still applied (`0x07000001 -> host 0x07000000`).
- FATAL 0, digest 0 mismatches, no new WARN class against c408b115a.

