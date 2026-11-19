# 343 A round the client fires after its release has no press on the host copy

Found in run 16 of `c335-range-load` (b27 `0ebdac007a685d9c`), 2026-11-14. Sync fault (user rule "all effects
sent"): the host never sees the client's last round of a burst.

- Client 7418: `card026: client t=329664360 client sent Fire 3840 (AbilityInputTagReleased)`: a release carrying
  15 rounds.
- The client's shot seq 108 (its 16th round) is at client t=329664374, 14 ms after the release went out.
- Host 7302: `press ended (release (Fire 0)): client rounds 15, copy rounds 15, refused 16, late-fired 15, unused
  0, a refused round still owed`.
- Host 7309: `WARN card157: ... HitClaim shot 108 ... (miss) found no round in 1500 ms: dropped`.

## Cause (static, `round_c181.cpp`, `fire.cpp`)

The client's fire ability fires one more round after `AbilityInputTagReleased`. The release's count (card 175) is
taken at the release, so it says 15 and the host releases at once. The 16th Round then arrives with no relay held:
`c181_receive_round` makes a fresh row with one credit that nothing spends, and the round's HitClaim waits 1500 ms
and is dropped. For a hit on an NPC, card 281 serves the damage from the claim; the tracer and sound are lost
either way.

## Fix (b29, host)

`c343_straggler_press`: a Round with no primary relay held, within `kC343StragglerMs` (500 ms) of that pawn's
primary release, presses the copy again, credits the round, and releases with a count of 1 (the card 175 hold
releases once the copy fired it, or at its 300 ms bound). Row:
`card343: host t=<ms> Round from <pawn> <n> ms after its release: one-round press`.

Not covered: a second round after the release, and a weapon change inside the window.

## Prediction

A run with Morita bursts (`c335-range-load.txt`): any `card343` row is followed by a `card181` press end with
`client rounds 1, copy rounds 1`, and there is 0 `WARN card157 ... found no round`. The race does not hit every
run (b25: none); a run with no `card343` row proves nothing.
