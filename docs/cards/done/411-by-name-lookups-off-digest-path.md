# Card 411: by-name property lookups off the digest and ext poll paths

Status: accepted 2026-11-16. Class: card 330 (per-frame cost). Build b105 `3cdf9d2a017ba9d3`.

## Problem

`prop_find_prop` (a by-name walk of a class's property chain) was called about 78000 times per fast chain on the
host (run `c411b103a`, counted by the temporary `prop_find_count` in `mod/src/ue/reflect.cpp`). The callers were
per-row work in the card 132 ext poll, the door child lookup and the intro prompt step.

## Change

- `ext_c132.cpp`: the baseline row keeps the resolved field (`HostRow.f`); the poll reads by offset and no longer
  calls `c132_ext_field` per row.
- `door_c95.cpp`: `c95_child_cac` resolves the child property once per class.
- `intro_c135.cpp`: `c135_first` result cached.

## Proof (run `c411b105a`, fast chain, passed, 22 rows `mismatches 0` on both sides, no new WARN class)

| | b103 (`c411b103a`) | b105 |
|---|---|---|
| Host `prop_find_prop` calls per chain | 78000 | 34000 |
| Client `prop_find_prop` calls per chain | 16000 | 16000 (the rest are test driver `Widget Stack` lookups) |
| Ext poll per tick, five maps | 35.7, 34.1, 29.3, 29.3, 34.2 us | 19.0, 17.5, 20.3, 11.3, 18.9 us |

- Failed prediction: the `c132_digest_step` max (12 to 19 ms once per map) did not drop. Cause found afterwards: that
  call is `c132_inventory("map load")` on the first host tick of a map, behind the loading screen (card 412).
- Run `c411b104a` (b104 `b39472d24782b28b`) was void: the prompt never became ready on FED_C3M1 and the log holds no
  cause. The b105 diagnostic names the wait state (`vis 4, LoadingFinished 1, Landscape 0`); it has not recurred.
  Open, unexplained one-off.
- Temporary, to remove or gate before a play build: `prop_find_count` in `reflect.cpp`, the prompt diagnostic in
  `c135_prompt_step`.
