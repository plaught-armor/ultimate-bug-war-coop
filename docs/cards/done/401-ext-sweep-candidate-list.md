# Card 401: the host's ext sweep walks every level actor twice a second (about 200 us per frame)

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: open, measured, design written, not coded. Class: card 330 (per-frame cost). Not a play blocker.

## Cost (seen, run `c399b87f`, b87, host)

`c132_ext_step` is the host's most expensive mod step on every mission map, about a quarter of the mod's 0.7 to
0.9 ms per frame:

| Map | mean per frame | actors per sweep | pass the placed/replicated early-out | reach `c140_placed_entry` |
|---|---|---|---|---|
| FED_C3M1 | 197.7 us | 26329 | 25662 | 3223 |
| FED_C3M2 | 264.7 us | 29086 | 28358 | 3637 |
| FED_C4M1 | 206.6 us | 30318 | 29716 | 4197 |
| FED_C4M2 | 233.7 us | 29027 | 28416 | 3780 |
| FED_C5M1 | 192.1 us | 28441 | 27734 | 2829 |

Split per tick (`card330: host ext split` rows): `ext_host_poll` 32 to 44 us (231 to 254 ext rows, every tick), class
lookup 49 to 74 us, `c140_placed_visit` 97 to 146 us. A sweep takes 25 to 28 ticks (`kExtBudget` 1024), 415 to 527 ms.

About 88% of the actors a sweep looks at are rejected every time for a reason that does not change between full
sweeps: not an ext class, and a root that is not Movable (card 330 already limits those to every 16th sweep, but the
sweep still reads each one's class row, placed flag, replication flag, root and mobility first).

## Design

Between full sweeps (`kExtFullEvery` 16, about 8 s) the sweep visits a candidate list instead of the level arrays.

1. A full sweep runs as today and records, per level, the actor count it saw, and for each actor that reached
   `c140_placed_entry` a candidate `{level index, actor index, actor pointer}` (cap 8192, a full list falls back to
   the level walk and logs one row).
2. The other 15 sweeps visit the candidates (skipping one whose slot no longer holds its pointer) and every slot past
   the recorded count of each level, plus whole levels added since (new actors are appended to `ULevel::Actors`, a
   destroyed actor's slot is nulled, not reused: check this in the exe before coding, it is the design's one
   assumption). Ext actors need no sweep between full ones: `ext_host_poll` reads their rows every tick; a new ext
   actor is found in the appended range.
3. `kExtBudget` stays, so a short sweep finishes in about 4 ticks; the sweep then waits out the rest of its 450 ms
   period, so the placed-actor rest test (`kC140PlacedStillUu` between two visits) keeps today's timing.

Expected: class lookup near 0, visit about 3500 of 28000 calls; `c132_ext_step` from about 200 us to 60 to 80 us per
frame (the 32 to 44 us poll stays). Optional step 2: poll the ext rows at 10 Hz in slices (about 4 us per frame, a
laser or hazard change up to 100 ms later); decide after step 1's numbers.

## Predictions (for the run that proves it)

1. `card330: host map <MAP> step c132_ext_step` mean under 100 us on all five maps above.
2. `card330: host ext split` rows: `c140 calls` per sweep under 5000 on 15 of 16 sweeps, the same
   `reach c140_placed_entry` counts as above within 10%.
3. The same `card098 ... world` placed rows ('M' and placed rest rows) and `card132` ext rows as a b88 run of the
   same scenario; 0 mismatches; no new WARN class.
4. Sweep period still 400 to 550 ms (`ext sweep N: ... ms` rows).

Files: `mod/src/ubw/ext_c132.cpp` (`ext_sweep_tick`), `mod/src/ubw/world_c98.cpp` (`c140_placed_visit` returns whether
the actor is a candidate).

## Results

- b91 `e83e833cf39573f8`, run c401b91a: host `c132_ext_step` mean 98.6 to 112.1 us (was 192 to 265). Split per tick:
  poll 28 to 39 us (231 ext rows), class lookup 6 to 7 us, c140 visit 61 to 70 us. 1472 candidates per quick sweep,
  about 4300 c140 calls per sweep, quick sweeps 450 to 471 ms, 0 stale, 0 `WARN card401`, 0 mismatches.
- b92 `edf08dd41151ba60`, run c401b92a: list rows spread over 24 ticks (`kExtQuickTicks`). No gain in the step's
  maximum (FED_C4M2 1.53 ms, FED_C5M1 1.61 ms); means 105.8 to 118.3 us. The calls over 1 ms are not the list.
- Card 402's diagnostic in that run showed a new actor in a slot below the level's last count (10 rows). A quick
  sweep reads only the list and the slots past the recorded count, so such an actor waited for the next whole sweep
  (up to 7.2 s). b91 and b92 are not acceptable builds for that reason.
- b93 `2662d2d696538671`: at each sweep start the sweep takes the refilled slots from card 402's ring
  (`c402_refills`) into the list; a lost ring row makes that sweep a whole one. Sweep row ends
  `(N stale, M from refilled slots)`. Run c401b93a pending.

### b93 runs (2026-11-15)

Run c401b93c (`cfdd06ae5462361c`), FED_C3M1 to FED_C5M1: host `c132_ext_step` mean 102.0 / 97.8 / 108.8 / 103.8 us (max 0.90 to 1.35 ms), against 118 us before the card; 38 quick and 18 whole sweeps; 0 `WARN card401`; 0 sweep rows with a non-zero "from refilled slots" count, so the refill feed from card 402 has not carried a row yet. Quick sweep period 450 to 495 ms against a predicted 450 to 480. Not accepted: the run failed at the FED_C5M1 level end (STATE entry of 2026-11-15 21:38), one clean chain run is still owed. The crash of run c401b93b and its fix are in card 402.

### Accepted 2026-11-15 21:43

Run c401b93d (`cfdd06ae5462361c`), full chain FED_C3M1 to FED_C5M2 in 181 s: passed, 0 `WARN card401`, 0 `WARN card402`, 22 compare rows per side with 0 mismatches, every travel within 15 ms of the second Next. The FED_C5M1 score screen stall of run c did not recur; its cause is not established (STATE entry 2026-11-15 21:43).
