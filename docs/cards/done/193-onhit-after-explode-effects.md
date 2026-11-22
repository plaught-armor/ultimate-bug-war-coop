# Card 193: An OnHit with effects after GrenadeExplode reaches the client

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: queued 2026-11-12, from c177-r1c (card 188 made this line a WARN only when the hit carries effects; it
now fired with effects). Budget: three runs. Static first.

## Why

Host, c177-r1c: "WARN card132: host t=102457623 OnHit of ADProjectile, whose GrenadeExplode already went: no second
frame (1 so far); hit BP_AI_WarriorBug_C (CharacterMesh0) at (229690, 41420, 8535), effects system no sound yes
zone no" (twice, 42 ms apart). The host played a sound the client never got (user rule: every host effect reaches
the client). Site: `detour_c132_on_hit`, `mod/src/ubw/grenades.cpp`.

## Do

1. Trace: which projectile class this is (plain ADProjectile hitting a bug: a round, not a grenade?), why its
   GrenadeExplode went first, and why the frame is sent once per projectile. Verified or open per line.
2. Fix in the effects manager (one manager): a later hit that carries effects sends them; no one-shot is doubled.
3. One run that makes the case from the trace; the client logs the sound as played.

## Done when

- Trace, fix, run lines, 0 lines of this WARN. Ends with "Report complete."

## Step 1 answers (senior, source plus `logs/c243-c3m2-r10-*`, 2026-11-13)

- The premise no longer holds. Since card 136 step 5 a host effect reaches the client through the effects
  manager, not through the `GrenadeExplode` frame (`c74_receive_explode`, `grenades.cpp:1396`: the frame only
  ends the copy and names the effects for the log). A repeat `OnHit` needs no second frame.
- Proof in run 10: host WARN at 228030043 (`OnHit of ADProjectile ... hit BP_Door_Sliding_Gigantic_C ... sound
  yes`), host audit `PlaySoundAtLocation MS_EMIT_Destructable_Doors_Damaged` at the same ms; client `card136:
  client fx 281212 PlaySoundAtLocation ...MS_EMIT_Destructable_Doors_Damaged owner BP_Door_Sliding_Gigantic_C
  played` at 228030062. Second sample: host `MS_WPN_GrenadeImpact_Dirt` at 228469014, client 228469024.
- Tally of the WARN in three run 10 logs: 12 rows, all `ADProjectile` on a bug mesh or a door, 6 with a system.
- Why a hit comes after the explode frame (repeat in one flight, or a stale ring entry: `c132_sent_add` runs
  after the original `OnHit`, which may already have released the round) does not change what the client gets.

## Step 2 spec (build 4, log class only)

`detour_c132_on_hit` (`grenades.cpp:1548` to 1567): the line is a note in both cases, under
`kC132RepeatLines`; drop the `WARN ` prefix and reword to `... OnHit after its GrenadeExplode: effects go by the
effects manager`. No behaviour change, no run of its own. Prediction: 0 rows of the old WARN in the next run;
the card 136 host and client effect counts for the map stay equal (the existing card 136 totals row).

## Closed 2026-11-16 (senior, stored logs, no run)

Step 2 is in the code (`grenades.cpp`, the repeat-hit line is a note in both cases). Run `c423b145a` (b145) host log:
0 rows of `whose GrenadeExplode already went`. The effect itself reaches the client by the effects manager (step 1).
