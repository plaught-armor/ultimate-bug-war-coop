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

## Pawn position diag (2026-11-21, next lane 2 build after b442)

- The client `card518: client key miss` row now ends with `client pawn found|none at (x, y, z)` (fx_c136_net.cpp,
  client_key_miss; g_client.pawn is refreshed each c136_step). Same 16-row cap.
- Check on the next run that carries it (any C4M1 or C3M2 run with HopperBugs): for each actor-missing guid, the
  distance from that pawn position to the host audit point of the flapping bug at the same time. Over 15000 uu on
  every guid closes the card as correct relevancy.

## c244b443a result (b443 da2b501a10253111, 2026-11-21): closed, correct net cull

32 client `card518: client key miss ... actor missing` rows, each ending `client pawn found at (x, y, z)`. Every
HopperBug row is keyed `world guid 0 comp 832`, and the host card434 rows (18) say the flap owner has no NetGUID and
the net cull is 15000 uu. Each miss was paired with the host card132 `fx audit: SpawnSoundAttached
MS_FLY_HopperBug_Flying_Flaps` rows from the 300 ms before it. Single-candidate windows: every distance to the client
pawn is 15920 to 24496 uu. Multi-candidate windows: each has at least one host flap over 15000 uu (the nearer ones,
4302 to 10555 uu, are other bugs the client has). So the misses are bugs outside the client's relevancy, not a desync.
The pairing is by time, since the client row has no fx id. The Archer muzzle flash and Aphid launch misses (one guid
keyed, guid 2774) were not checked; they are not HopperBug rows.

## Closed (senior 2026-11-21)

Proved in c244b443a (b443 da2b501a10253111, C4M1): see the junior's result row in docs/STATE.md (2026-11-21).
