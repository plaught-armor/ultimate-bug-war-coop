# Card 529 (sync bug, fix in source): a FedScore change made in the digest tick goes out after B

c263L314 (b314): client digest rounds 5 and 7 each show `WARN card132: mismatch Score Score: ext host c544 client
82dc; no row sent` (round 7: `e054` / `d884`). Both times the client applied the host's next "T" 1 ms after its
snapshot: round 5 snapshot 29274041, `host FedScore 50500, own 33500` at 29274042; round 7 snapshot 29333816,
`host FedScore 188500` at 29333817. The client made no own adds in the run (every T was "raised at once").

Cause: card 323 polls the host total once per tick in `c475_step` (`c323_host_step`), after the digest's B send in
the same tick. The host snapshot already holds the new total, B goes out first, and the T is queued behind it. This is
the old "no row sent" Score:1 row seen since c244b219a (card 323 note at line 77); the b222 settle fix covered the
client's own adds, not this order. "no row sent" only means score frames have no ledger row.

Fix (source, next lane 2 build): `c323_host_send(world)` (score_c475.cpp, was `c323_host_step(engine)`) is now also
called at the start of `c132_send_begin` (digest_c132.cpp), before each PC's `chan_flush_client`, so the T is queued
ahead of B on the same ordered pace queue (chan.h: frames are sent in order; B only starts a new batch).

Done when: a soak with score changes during digest rounds (c263 or c244) shows 0 Score mismatches, and a T row sent
in the same tick as a B appears in the host log before that round's B row.

## Closed 2026-11-19 (b315, c263L315)

Digest rounds 1 to 8 all show 0 mismatches. Every FedScore row comes before its round's B: round 7 FedScore
t=29948491, B t=29948603; round 8 FedScore t=29951671, B t=29951812.
