# 517: digest build skips STNavModifierVolume (perf)

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: Opened 2026-11-19 by senior from the c243b271b perf baseline. Follows card 386 (closed; option (b) left open).

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

## C386 split read and fix (senior 2026-11-21)

The `UBWCOOP_C386=1` split is in c263L365, c263L366 and c263L370 (FED_C3M2, b363 to b370): host builds 5.28 to 8.06
ms over 28,736 to 29,219 actors, with class 36.4 to 43.3 percent, state 22.9 to 29.8 percent, key 10.9 to 14.4
percent, sort 5.6 to 7.3 percent, and name 5.6 to 7.4 percent. So the walk itself is not the cost. The class name
text read per actor is the largest phase. Option (b) of card 386 is not needed yet.

Fix, in source (`mod/src/ubw/digest_c132.cpp`, `c517_slot`): a per-build open-addressed table (1024 slots) keyed by
the actor's UClass pointer. It holds the server-only row, the d.cls index and the class name, so the class name is
read once per class per build, not once per actor. The table is reset at the start of every `c132_build`, so a class
freed between builds is never looked up. A full table falls back to the old per-actor path. `d.cls.dropped` counts
the same as before (one per actor of a class left out of a full table). A compile check in `build-c517` is clean
with 0 warnings. Not in a lane 2 build yet.

Predictions for the next FED_C3M2 run with `UBWCOOP_C386=1`:

1. `card386` class share under 10 percent (from 36 to 43), and host build time 3 to 5 ms (from 5.3 to 8.1).
2. Same entries (about 4,713 to 4,760) and classes (145 to 148) per round as c263L370.
3. `card164` server-only counts unchanged (`STNavModifierVolume:23076`).
4. Client rounds `mismatches 0`.

Done when 1 to 4 hold in one run.

## c263L441 results (b441, FED_C3M2, `UBWCOOP_C386=1`, 2026-11-21)

Logs: `logs/c263L441-host.log`, `logs/c263L441-client.log`, `logs/c263L441.drive`, `logs/c263L441.runner`. Both
sides rc 0, FATAL/ERROR 0, no crash dumps. The drive ended VOID on six fire-clear lines (211, 221, 226, 231, 236,
247: "bug ... within 400 at the cap"); none of them touch the digest.

| Prediction | Result | Holds |
|---|---|---|
| 1. class share under 10 percent, host build 3 to 5 ms | host class 20.8 to 29.6 percent, build 4.68 to 8.06 ms over 7 rounds; client class 24.0 to 30.6 percent, 6.36 to 7.73 ms | no |
| 2. entries 4,713 to 4,760, classes 145 to 148 | entries 4,588 to 4,629, classes 143 to 146 | close, not same |
| 3. `STNavModifierVolume:23076` | 23,076, the same 10 server-only classes, 23,407 left out | yes |
| 4. client mismatches 0 | `mismatches 0` (map end and session end) | yes |

Reading. The class phase in milliseconds fell from about 2.0 to 3.3 ms (c263L365 to c263L370) to 1.06 to 2.15 ms, so
the class-name text read per actor is gone. The total did not fall, because the state phase rose to 24.7 to 35.4
percent and the run-to-run spread is as large as the saving. What remains in the class phase is the first touch of
each actor: `read_ptr(actor, kObjectClass)` on 29,000 cold actors, 23,407 of them server-only. That is one cache miss
per actor, which a class cache cannot remove. Prediction 1 was wrong because it assumed the name text was the whole
class cost.

Next lever (card 386 option (b)): skip the server-only actors without dereferencing them. Card 164 shows they never
move (`moved since the last round 0`). A per-level table of the actor array index of each server-only actor, checked
by pointer equality on the array slot (no read of the actor), would skip 23,407 cold reads per round. Not carded yet;
the senior decides.

## Closed (senior 2026-11-21)

The c517_slot cache is kept: lossless, and it takes the class phase from about 2.0 to 3.3 ms down to 1.06 to 2.15 ms
per build (c263L441). Server-only counts and actors left out are the same, and mismatches are 0. The entry and class
counts differ by run content: the c263-c3m2-load fight differs run to run, and the cache keys on the exact class
pointer, so it cannot change which class an actor gets. The total build stays at 4.7 to 8.1 ms, because the run spread
is as large as the saving. Prediction 1 was wrong: the remaining class cost is the first cold read of each actor, not
the name text.

Option (b) (skipping server-only actors by array index, with no read of the actor) is not carded. It saves at most
about 1 ms per 30 s build, which is about 0.04 ms/s of GameThread time. It is listed as a leftover lever on card 573.
