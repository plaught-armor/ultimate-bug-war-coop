# 597: The client misses a cannon turret's fire sound: its actor is missing (C3M1)

Found in c595b447b-d (card 582 G3 attempt, b447).

## Finding

- The client log has card518 rows "MS_EMIT_CannonTurret_Fire key guid guid 754 comp 1595 actor missing, not
  played (key miss, no world point)", for example c595b447d-client.log 198682142. The client pawn was at
  (-75642, -9887, 1260).
- Other BP_AI_CannonTurret_C fire rows on the same client resolve their owner and play. So one turret (guid 754) has
  no client actor while the host fires it.
- The host log has no card455 row for a turret hit sound (Building_Generic_Damaged). The card 582 G3 premise, that a
  turret hit plays an attached building sound, did not hold on the host either. G3 needs a different target.
- Note: the card166 rows with Faction.Human.CannonTurret are the client's own Morita damaging the turret (the
  OnDamageCaused causer is the client's weapon), not the turret hitting the client.

## Trace first

1. Where guid 754 is on the host when it fires, and whether the client ever received its spawn bunch (card434 rows),
   a destroy, or a net cull (15000 uu).
2. If the actor is culled or not yet replicated, the sound row has no world point. Should the row carry the
   location, as the "played at world" key-miss path does for other sounds?

## Trace result (static and c595b447d logs, no run)

- Guid 754 is the turret the host spawned at (-60230, 16360, 1460), host log 198629473 (card098 host spawn). The
  client never got a spawn bunch for it: the client card098 spawn rows list only three turrets, (-75090, -19890),
  (-76540, -9950) and (-47300, 9650). The client card325 cue rows "key guid 754" sit at (-60302, 16161) and
  (-60269, 16169), next to that turret.
- At the misses (198682142, 198682375) the client pawn was at (-75642, -9887): 30437 uu from the turret. That is
  past the 15000 uu net cull, so the actor is correctly not relevant to the client, and past the sound's
  ATT_EMIT_Occluded_200m attenuation (20000 uu), so the sound would be silent at that distance even if it played.
- Nothing is lost for the player. All 16 capped key-miss rows in that log are "actor missing" (bark, Razorback,
  turret fire, MountableTurret sounds); only the two turret rows were checked against the cull distance.
- Carrying a world point (question 2) would not help: a sound past the cull is past its attenuation in every case
  checked here, so it plays as silence.
- Packet note (smaller-packets rule): the host sends every sound row to the client with no distance check
  (fx_c136_net.cpp has no cull or attenuation test on the send path). Rows whose owner is not relevant to the client
  cost bytes and end as key misses. A host-side skip for attached sounds whose owner is past the net cull from every
  remote pawn would cut them. Size it from the card518 byte rows before carding; not done here.

## Outcome

No bug: the client behaves correctly for an actor past the net cull. Close, or turn the packet note into a
smaller-packets card if the byte count is worth it.

## Proof

None needed: no code change. The trace above is from logs already on disk.

## Closed (senior, 2026-11-21)

Not a bug. The turret was 30437 uu from the client, which is past both the 15000 uu net cull and the sound's
20000 uu attenuation, so nothing audible is lost. The packet note is not carded here. The junior sizes it from
existing logs first (smaller-packets rule), and it gets a card only if the bytes are worth a cut.
