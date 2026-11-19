# Card 404: client by-name finds built every actor's name with snprintf (2 to 5 ms per received row)

Status: accepted 2026-11-16. Class: card 330 (per-frame cost). Found by card 403.

## Cost (seen, run `c403b94b`, `c56d08282c73d7b3`, client `card330: ... window` recv part, per 30 s)

| Op | Frames | Total | Per frame |
|---|---|---|---|
| WorldOp | 245 to 314 | 517 to 1488 ms | 2 to 5 ms |
| CollisionOp | 151 to 352 | 747 to 1762 ms | 5 ms |
| Fx | 33 to 344 | 441 to 2937 ms | max 72 to 264 ms |

These rows are mostly the join replay at each map load (ext `X` rows, collision rows), so in play the cost is a 2 to
5 s stall at each load plus the Fx rows during a fight. Cause: `c95_find_nearest`, `c50_find_placed` and
`c50_find_placed_any` called `c50_name` (copy + `snprintf("_%u")`) and `strcmp` for each of about 16000 level
actors per row.

## Fix

`c50_name_is(object, name, name_len)` (`hooks_ubw.cpp`): same answer as `c50_name` + `strcmp`, without building the
text. It compares the entry length plus suffix length first, so nearly every actor is refused on the name entry
header. The three finders use it. `c50_name` writes the number suffix by hand (`c50_suffix`), which also speeds the
scans that need every name (`c95_match_rows`, the digest queries).

## Proof (run `c404b95a`, `6a8da3341521cae9`, same chain, passed, 22 summary rows `mismatches 0`, no WARN class the
run before did not have, 0 `no copy by that name`)

| Op | Before | After |
|---|---|---|
| WorldOp | 517 to 1488 ms | 103 to 167 ms |
| Fx | 441 to 2937 ms, max 264 | 114 to 696 ms, max 97 |
| CollisionOp | 747 to 1762 ms | out of the top 3 (under 167 ms) |
| recv total without EndLevel | 1410 to 4622 ms | 330 to 991 ms |

`EndLevel` (900 to 1900 ms, once per map) is the game's own `USTGameInstance::EndLevel` called by the relay, not mod
code.

perf sample (run `c404b95b`, same build, passed, 100 s of the client process): mod code 1.21 % of CPU samples
(6.59 % before). 729 of the 1043 mod samples are still the by-name walk (`read_i32`, `c50_name_is`,
`fname_entry_checked`, `c50_placed`, `c95_find_nearest`); everything else in the mod is 0.36 %.

## Left open

A find still walks every level actor (about 0.4 ms). A per-map name index would remove it; it is load-time cost
only (100 to 170 ms per map), so not carded.
