# Card 405: by-name finds compare the FName pair; raw reads inline

Status: accepted 2026-11-16. Class: card 330 (per-frame cost). Follows card 404's "left open".

## Cost (run `c404b95b`, `6a8da3341521cae9`, perf sample of the client)

729 of the client's 1043 mod CPU samples were the by-name walk: `read_i32` 294 (an out-of-line call per field read,
the build has no LTO), `c50_name_is` 178, `fname_entry_checked` 136, `c50_placed` 64, `c95_find_nearest` 57.

## Fix

- `read_ptr`, `read_i32`, `read_f32`, `read_u64` are inline in `ue/reflect.h` (all 2367 call sites).
- `C50NameRef` / `c50_name_ref` / `c50_name_match` (`hooks_ubw.cpp`): a scan's name resolves to its FName
  (pool index, number) at the first text match and is kept per name in a 1024-slot table (`g_c405`). After that
  each actor costs two integer compares and no name pool read. A pool index names one entry for the life of the
  process and the pool holds each text once, so the pair is the same test as the text. A name no actor ever
  carried stays on card 404's text path. `c95_find_nearest`, `c50_find_placed` and `c50_find_placed_any` use it,
  and test the name before the placed flag.

## Proof (run `c405b96a`, `74cc9b58691982a8`, fast chain, passed in 203 s, 22 summary rows `mismatches 0`)

| Measure | Card 404 | Card 405 |
|---|---|---|
| Client WorldOp receive per 30 s | 103 to 167 ms, max row 2.0 to 3.3 ms | 67 to 94 ms, max row 0.5 to 1.8 ms |
| Client Fx receive per 30 s | 114 to 696 ms, max 20 to 97 ms | 60 to 235 ms, max 10 to 69 ms |
| Client mod share of CPU samples | 1.21 % | 0.89 % (1025 of 114843) |
| Host mod share of CPU samples | 0.71 % (card 403) | 0.51 % (554 of 108862) |

Client samples left in the walk: `c50_name_match` 294, `c50_name_is` 132, `fname_entry_checked` 85,
`c95_find_nearest` 14 (the first scan of each name, names with no actor, and the cache misses of touching each of
about 16000 actors). Host top: `c132_actor_ext` 72, `c140_placed_visit` 39, `prop_find_prop` 29.

## Left open (measured, not carded)

- A per-map slot index (name hash to level and slot) would remove the walk itself: about 0.45 % of client CPU,
  nearly all in the join replay at a map load.
- `prop_find_prop` + `strcmp` on the host: 47 samples (0.04 % of host CPU). A cache keyed by class pointer would
  hold property pointers across a class unload, so not done.
