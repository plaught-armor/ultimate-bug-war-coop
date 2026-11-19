# Card 188: OnHit of a projectile whose GrenadeExplode already went

Status: queued 2026-11-11, from c152-r3 (new host WARN class, x2; not caused by card 152's change).

## Why
Host log: "WARN card132 OnHit of ADProjectile, whose GrenadeExplode already went" twice in c152-r3. No new WARN
class passes review.

## Do
1. Static first: the WARN's site, the order of GrenadeExplode and OnHit for a grenade that explodes on contact, and
   whether the second event sends anything to the client (a doubled effect) or is only logged.
2. If nothing is sent twice, the line becomes a counted note with the reason in a comment. If something is, fix the
   order at the relay. No run of its own: check the line in the next run that throws grenades.

## Files
The file holding the WARN site, this card.

## Senior trace (2026-11-11)

- Verified: the site is `detour_c132_on_hit`, `mod/src/ubw/grenades.cpp:1515` (card 167's repeat line). It runs when
  the host's OnHit fires on a projectile already in the sent set (`c132_sent_has`); the original is not called.
- Verified (logs/c152-r3-host.log lines 4727, 4750): both lines are one resting projectile touching the Landscape
  at the same point (228864, 38913, 8595), 84 ms apart, and both captures read "effects system no sound no zone
  no". Nothing visible ran on the host that the client lacks.
- Verdict: not a sync gap. The line is a WARN only when the repeat captured an effect (`c132_fx_any(got)`);
  otherwise it is a capped note, still counted in `g_c132_sent.repeats`.
- Change: that condition at the one site. It rides the card 186/187 build; proof is the next run's WARN diff.

## Change and run c186-r1 prediction
- `grenades.cpp` repeat site: `g_c132_sent.repeats` is still counted on every repeat. The line keeps its "WARN "
  prefix only when `c132_fx_any(got)`; otherwise the same text is a note under `kC132RepeatLines` (24 a map,
  `g_c132_sent.repeat_lines`, cleared with the table at map end).
- Prediction: 0 "WARN card132: host ... OnHit of ..., whose GrenadeExplode already went" lines, unless a repeat
  captured an effect (then the line says which). Any repeats show as the same line without "WARN".

## Run c186-r1 outcome and c186-r2 prediction
- r1: the repeat site did not run (no OnHit repeat line, WARN or note); the scenario throws no grenades.
- r2: same scenario parts, so the line is again absent or a note. No change in r2.

## Senior review (2026-11-11)

Accepted without its own run (static card). The repeat-hit line in `detour_c132_on_hit` is a WARN only when the
hit carries effects, otherwise a note capped at `kC132RepeatLines` (24). The class is absent in c186-r1 and
c186-r2; neither run had a repeat hit, so the note path itself is unexercised and is watched in the soak (card 127).
