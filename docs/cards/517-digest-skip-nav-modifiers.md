# 517: digest build skips STNavModifierVolume (perf)

Opened 2026-11-19 by senior from the c243b271b perf baseline. Follows card 386 (closed; option (b) left open).

## Finding

- The longest mod tick on FED_C3M2 is the digest build: `card386: host/standalone ... digest build 18.07 ms, 28101
  actors walked, 27789 entries` and 16.61 ms (c243b271b host lines 3323, 3488). The client pays the same: 16.40 and
  14.23 ms (client lines 3216, 3334). At 60 fps each build drops one frame on each side, every 120 s in a play build
  (`kC386PlayRoundMs`) and every 30 s in test runs.
- The class inventory at map load (host line 2636 on) shows 23076 of the 28094 actors are `STNavModifierVolume`:
  placed 23076, rep 0, changes `-`, mechanism none. Nav modifiers feed the server's nav mesh only; the client never
  reads them, the same reason card 164 left out `RecastNavMesh*`.
- Every actor paid the name phase (29 percent of the build, card 386 step 1) before the server-only check ran.

## Fix (source, in the next lane 2 build)

`mod/src/ubw/digest_c132.cpp`:
1. `kC132ServerOnly` gains `"STNavModifierVolume"`.
2. `c132_add_actor` runs the server-only check before `c132_actor_name`, so a left-out actor costs one class name
   read and one short string compare loop. Side effect: a replicated actor of a server-only class is now counted in
   the card 164 server-only row and not in the unnamed count (both are log-only counts).

## Predictions (one run that reaches a FED_C3M2 digest round, any scenario that already does)

1. `card386` host and client build on FED_C3M2 under 6 ms (from 14 to 18 ms); actors walked unchanged at about 28100;
   entries about 4700 (27789 minus 23076).
2. `card164: host round N` row lists `STNavModifierVolume:23076` on FED_C3M2, same count on the client row.
3. `card132` client rounds `mismatches 0`, as before.
4. `card330` FED_C3M2 map total for `c132_digest_step` max under 6 ms (from 18.35 ms).

If 1 misses (build still over 6 ms), read the `UBWCOOP_C386=1` split: the next candidate is card 386 option (b).

## Done when

Predictions 1 to 3 hold in one lane 2 run; numbers quoted here.

## Result c263f (b296, FED_C3M2, senior 2026-11-19)

- Prediction 1 misses on time, holds on entries: host builds 12 rows, 5.37 to 10.38 ms, median 7.20 ms; client 12
  rows, 6.13 to 10.80 ms, median 7.07 ms (from 14 to 18 ms, so about half). Actors walked 28,736 to 29,003; entries
  4,713 to 4,746 (predicted about 4,700).
- Prediction 2 holds: `STNavModifierVolume:23076` on 12 host and 12 client `card164` rows.
- Prediction 3 holds: client rounds `mismatches 0` (16 rows).
- Prediction 4 misses: `card330` host map FED_C3M2 `c132_digest_step` 14.78 ms max, 19 calls over 1 ms (from 18.35).
- Next: the remaining ~7 ms is the walk of ~28,900 actors itself (entries fell 83 percent, time 50 percent). One
  FED_C3M2 run with `UBWCOOP_C386=1` gives the split; if the walk phase dominates, option (b) of card 386 (entries
  kept between rounds, spawn/destroy feed) is the fix. The run can be any C3M2 scenario already planned; it carries
  the env var only, no build.
