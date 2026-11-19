# Card 407: per-map name index for by-name actor finds

Status: accepted 2026-11-16. Class: card 330 (per-frame cost). Build `fa232da123925f82` (b99 = b98 + this card).

## Problem

`c95_find_nearest` (every `WorldOp` and `CollisionOp` row that names a placed actor) walked every level's actor
array per find. After card 405 the walk was still 511 of the client's 1025 mod CPU samples (run `c405b96a`).

## Change

- `hooks_ubw.cpp`: `g_c407`, an open-addressing table (131072 slots) from name hash to (actor, level, slot), built
  lazily from the card 402 slot copy on the first find after a reset, kept current by `c402_slot_new`, dropped by
  `c402_slots_reset`. `c407_named` returns up to 32 hits, each checked with `c402_valid`, `c50_name_is` and
  `c50_placed`; it returns -1 (caller walks) when the copy is not usable, the table is full or the name is too long.
- `door_c95.cpp`: `c95_find_nearest` asks the index first and falls back to the old walk (`c95_find_walk`) on no
  hit. The first 32 index answers per process are compared with the walk; a difference logs `WARN card407` and the
  walk's answer is used.

## Proof (run `c407b99a`, fast chain, passed, 22 rows `mismatches 0` on both sides)

| | card 405 (`c405b96a`) | card 407 |
|---|---|---|
| Client mod CPU samples | 1025 of 114843 (0.89 %) | 620 of 130821 (0.47 %) |
| `c50_name_match` + `c50_name_is` + `fname_entry_checked` on the client | 511 | 13 (`c407_named` 5) |
| Client `WorldOp` receive per 30 s | 67 to 94 ms | out of the top three ops (third is 8.5 to 87.8 ms; 10.1 ms in the one window with `CollisionOp` third) |

- Index build: 2.7 to 15.0 ms once per mission map on the client (25087 to 28679 placed actors), 20 ms on the host,
  at map load. Every find on a mission map was answered by the index (390, 425, 498, 871, 542, 579 of the same
  number); the 2 to 6 finds on the 26-slot transition world fell to the walk.
- `WARN card407`: 0 lines (32 compared answers equal to the walk).

## Seen in this run, next

Client `Objective` receive 58 to 88 ms per 30 s for 15 to 41 frames (3 to 6 ms per frame with a row, max 12.6 ms).
