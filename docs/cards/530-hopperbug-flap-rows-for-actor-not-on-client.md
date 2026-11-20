# 530: HopperBug flap sounds keyed to an actor the client does not have

Found 2026-11-20 by the senior in c263L334 (b334 `a4d4d521efcbba22`), card 518 step k.

## Finding

- The client log has 13 `card518: client key miss point 2 asset .../MS_FLY_HopperBug_Flying_Flaps ... key guid guid N
  comp 934 actor missing` rows over 4 guids (336, 272, 706, 316).

## Static trace (2026-11-20, logs only, no run)

- HopperBugs replicate natively: host `card098: host spawn BP_AI_HopperBug_C ... replicates 1 ... net cull 15000 uu`.
  They are not a mod spawn path, so the client gets one only while it is net relevant (inside 15000 uu).
- Guid 316 misses at 64589621, 64590663 and 64591688 (about 1 s apart). The host audit shows one HopperBug flapping on
  the same cadence about 50 ms earlier, at (-47329, -22023), (-46468, -21908) and (-45415, -21857). The fight at
  that time is at about (-25800, -11100) (host bullet impacts at 64589801). The bug is about 21,000 uu away, past the
  15000 uu cull, so the client has no actor channel for it. That is correct relevancy, not a lost spawn.
- Assumption not yet checked: the client pawn was near the fight. The client log does not print its own position at
  these times.

## Conclusion

- Not a full world sync break: the client sees and hears the bug once it comes within 15000 uu, like any replicated
  actor. Before step k these rows played a flap at a far world point, which was inaudible at that range anyway.
- The rows are still wasted wire bytes: the host sends attached fx rows for owners the client cannot resolve. In L334
  that is at least 13 rows (16-row log cap, 28 key misses counted in total), small next to about 22,000 rows.

## Next (low priority, card 518 family)

- Optional byte cut: the host drops guid-keyed attached rows whose owner is beyond its net cull distance from every
  client pawn (or has no open actor channel on the client connection). Measure first: count such rows in a run.
- Optional check: log the client pawn position on its first key miss per guid, to close the assumption above.
- c263L336 (b337) adds AI Morita rows to the same miss shape: muzzle flash guid 982 (point 4) and reload guid 426
  (point 2), "actor missing, not played". The pawn-position check above covers them too; treat them as this card's
  class unless that check puts them inside 15000 uu. Two more misses (guid 3680, 3848) arrived 1 to 5 ms after the
  client's next map arm: old-map rows after the arm, not relevancy.

## Done when

- Closed as not a desync once one run shows each HopperBug actor-missing guid is beyond 15000 uu from the client pawn
  at the miss time, or the host stops sending those rows.
