# Card 205: The digest reports an objective the host added a few ms after its snapshot

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: queued 2026-11-12, from c142-185-r2 (new WARN class). Budget: one run, batched. Static first.

## Why

c142-185-r2 round 1, client: `WARN card132: mismatch STLevelObjective O:ShootingRange_SO1 presence host 0 client 1;
no row sent` and the round WARN. Host snapshot at t=114717843 (S=628). The client's own level script ran
`AddSubObjective(ShootingRange_O1, ShootingRange_SO1)` at 114717847; the host ran it at 114717855 and sent ledger
row 633 (ObjectiveActors) plus the AddSubObjective relay. The round line says "excluded 0 (a ledger row after
S=628)", so the exclusion did not match this key. Rounds 3 and 4 were clean. Not a world difference; a false WARN
that hides real ones.

## Do

1. Trace, no run: why the ledger exclusion missed it (the AddSubObjective relay is not a ledger row, or the
   objective key is not derived from row 633's text). Also state why the client's script adds the objective locally
   before the host's row arrives, and whether card 049's "ignored: the id exists already" is the intended path.
2. Fix in `digest_c132.cpp` at the exclusion: an objective key whose add or change was sent after S is excluded
   and counted, like any other ledger row after S.

## Done when

- The trace names the cause; a batched run shows no objective presence mismatch in a round whose snapshot falls
  within a relay, with the excluded count above 0 when it happens. The Report ends with "Report complete."

## Senior note (2026-11-13)

Same cause as card 258: the client's level script adds the objective before the host's row. Build 3 (card 258)
skips every client-local Add, so the client cannot hold an objective the host has not sent. No digest change is
needed if card 258's FED_C2M1 prediction holds (0 `STLevelObjective` mismatch rows, no `the id exists already` row
outside a snapshot). Close this card with card 258; reopen only if a presence mismatch of this class appears with
card 258 in.
