# 431 - The ledger stamp is 10 B on every blob frame, 11 B on every text frame

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: open, parked until after the playtest (efficiency audit 2026-11-16; no build yet).

## Measured
Every mod frame carries the card 132 stamp (`chan.cpp stamped`): seq 30 bits and sender ms 30 bits. A blob or packed
frame is UCS-2, so the fixed cost is FString length 4 + 2 x (prefix 1 + op 1 + length 1 + stamp 5 + NUL 1) = 22 B, of
which the stamp is 10 B. A text frame takes the 11 char narrow stamp (card 390).
- `c423b127b` host: 2,393,134 B in 21,584 frames: stamp share 9.0 %.
- `c423b127a` host: 3,837,174 B in 40,134 frames: fixed 22 B share 23 %, stamp 10.5 %.
- `c399b122a` host: 353,026 B in 2,651 frames: stamp share 7.5 %.
Peak 5 s windows are 13 to 22 % fixed cost (GrenadeSpawn, GrenadeExplode, Anim, GibPart rows are one row a frame).

## Readers of the stamp
`ledger_c132.cpp`: `c132l_check_gap` (seq must follow the peer's last; 1 restarts the peer), `c132l_clock_sample`
(min of receiver now minus sender ms, the clock offset cards 223 and 313 use), the `card132: recv` row text and the
LedgerAck seq. Nothing needs 30 bits of either value on each frame: the channel is a reliable ordered RPC, so seq
rises by one a frame, and ms is monotonic per sender.

## Fix (one of, decide before coding)
(a) Short stamp, 2 units (4 B) on blob frames and 1 + 5 chars on text frames: seq low 15 bits and ms low 15 bits; the
    receiver extends both from the peer's last full values. A full stamp goes on a peer's first frame and whenever
    the sender's last frame to that peer is older than 16 s (the 15 bit ms wraps at 32.7 s). Saves 6 B a blob frame,
    5 B a text frame: about 5 to 6 % of mod channel bytes.
(b) Row batching for the one-row-a-frame ops (card 151 step 1, one stamp per batch frame) removes the whole 22 B for
    every row after the first in a tick; larger gain in fights, larger change.

## Proof when done
Same scenario, before and after: `netbudget ... chan bytes per row` totals per op fall by 6 B a frame for (a);
`WARN card132: seq gap` 0, `card132` clock offset rows within 2 ms of the run before, digest `mismatches 0`, one
reconnect in the run (the peer restart path).

## From card 441 (merged here 2026-11-17 13:09)

Card 441 (AI round spawn and impact rows as text) measured that a hand-made binary row would save about a third
of those two ops, about 4 % of host bytes, and that the larger cost is the 22 B fixed part, which is this card.
Cheap variant, only when those rows are touched for another reason: quaternion at `%.3f` (about 8 B a row) and the
round number without the round bit as a negative decimal (about 3 B a row on both ops). Full numbers are in
`done/441-...md`.

## Closed (2026-11-17 16:20)

Merged into card 151 with option (b), row batching; see the merge note there. Not done: no code. Reopen when: card 151 ships batching and its after run still shows the stamp above 5 % of mod channel bytes.
