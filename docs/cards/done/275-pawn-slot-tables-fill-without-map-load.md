# 275 Per-pawn slot tables fill up within one map (remote body no longer refitted)

Found in the live client log of card 243 run 10 (FED_C3M2, 2026-11-13): 4 rows `WARN card034: client slot table
full: BP_PlayerCharacterMI_C will not be refitted` from t=227060162 on. Real mod bug, play-visible in a long
session: after the table fills, a new remote player pawn gets no card 034 body refit, and the sibling tables give
it no sprint setup (card 022) and no hide state (card 033).

## Mechanism (source plus run 10, senior, 2026-11-13)

- `c28_slot` (`remote_body.cpp:528`), `c33_slot` (`remote_body.cpp:701`) and `c22_slot` (`sprint.cpp:839`) key a
  slot by the pawn's UObject index and never free one. Capacity `kC28MaxPawns = 8` (`hooks_internal.h:1868`).
- The tables are emptied only by a map load and by `pawn_slots_respawn` (`sprint.cpp:1077`, card 130 item 15:
  respawn in place). Run 10 has 0 `pawn slots dropped` rows on the client.
- Run 10 client attached a remote body to 13 different pawns on one map (`card028: client attach` rows with a
  template at 225422865, 225571691, 225768638, 225921854, then one per new pawn up to 227584540). The ninth new
  pawn found the table full. Card 130 fixed one cause of pawn replacement; any other cause (rejoin, death and
  respawn, relevancy re-creation) still fills the tables.

## Fix spec (by class: all three tables, one rule)

- When a new pawn index finds its table full, reuse the first slot whose pawn is gone (the stored index no longer
  resolves to a live object of the pawn class; use the weak-handle check the mod already has, as `c31_weak_set`
  rows do). Reset the reused slot to its initial values. Only when all 8 slots hold live pawns does the WARN stay.
- One shared helper for the three tables; no change to `pawn_slots_respawn`.
- Log, capped: `card275: <side> <table> slot of gone pawn index N reused for <pawn>`.
- Files: `mod/src/ubw/remote_body.cpp`, `sprint.cpp`, `hooks_internal.h`.

## Proof (batched with the next FED_C3M2 run, 0 extra runs)

Predicted: `card275: ... reused` rows on the client from the ninth new remote pawn on, 0 `slot table full` rows,
and a `card034` refit row for every `card028: client attach` with a template.

## Accepted 2026-11-16 (senior, from the stored log; no new run)

Run `c243-c3m2-a1` (FED_C3M2, 2026-11-13 12:24, the run after r10) client log: 16 `card275: client ... slot of gone
pawn index N reused` rows (card022 8, card028 4, card033 4), 0 `slot table full` rows, and 12 `card034: client refit
BP_PlayerCharacterMI_C` rows spread over the whole run, the last ones after the first reuse. The attach rows of that
build no longer print "template", so the refit count stands in for the per-attach prediction. No later log holds a
`slot table full` row (newest one is r10).
