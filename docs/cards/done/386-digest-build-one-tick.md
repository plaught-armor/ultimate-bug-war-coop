# Card 386: the digest round builds the whole world digest in one tick (13 to 20 ms on FED_C2M1)

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: open, measured, not fixed. Queue: after the playtest (card 329/330 class; not a play blocker). Found
2026-11-15 from `card330` rows.

## Cost (seen)

`c132_host_start` calls `c132_build` once per round (`kC132RoundMs` 30 s): every actor of every level gets a name, a
class name, a class table lookup, its state and ext reads, a pool copy, then one `qsort` of all entries. The round is
log only (nothing is applied from a digest).

- Host, FED_C2M1 (about 30000 actors): `card330: host map FED_C2M1 step c132_digest_step` longest call 20.34 ms
  (`c350-c344`), 15.04 ms (`c355b70`), 12.76 ms (`c276b70`). The window rows of `playb70` on FED_C2M1 show one such
  tick per 30 s window: `max tick 19.44`, `20.49`, `23.22 ms`, with no other step over 2.3 ms.
- Host, FED_C1M1: longest call 3.50 ms (`playb70`), 3.15 ms (`c373b70`).
- Client: the same `c132_build` runs in `c132_try_snap`, usually from the Digest frame's receive path
  (`digest_c132.cpp:2167`), which is outside `ubw_post_tick`, so `card330` does not time it (client
  `c132_digest_step` longest call 0.02 ms in `playb70`). Its cost is unmeasured and should equal the host's.

Effect: one frame longer than 16.7 ms on each side every 30 s on the large maps.

## Suspected driver (static, not measured)

`c132_name_index` is a linear `strcmp` scan of the class table (142 to 154 classes on FED_C2M1) per actor, after
`class_name` formats the name: about 30000 actors times up to 154 compares. The level table lookup is hoisted already.

## Fix plan

1. Measure first: time `c132_build` with one QPC pair and print it in the host round row and the client snapshot row
   (covers the untimed client path).
2. Class index by class pointer: a small direct-mapped table (class pointer, index) filled per build, so
   `class_name` and the scan run once per class and not once per actor. `c132_server_only` reads the same name and
   moves behind the same cache (its result per class).
3. If the build still exceeds 5 ms on FED_C2M1: split the walk over ticks is not possible without losing the
   snapshot (the client compares at ledger seq S), so lengthen the round in play mode (60 or 120 s) and keep 30 s for
   test runs.

## Proof

One FED_C2M1 run: host round rows and client snapshot rows carry the build time; before and after numbers quoted;
`card132: client round` rows still `mismatches 0` apart from the known card 218 STAISpawner row.

## Step 1 result (2026-11-15, build b73 `0cf31a2d208aba11`, diagnostic only)

`c132_build` now prints `card386: <side> ... digest build X ms, N actors walked, ...` each build (one
QueryPerformanceCounter pair, always on) and, with `UBWCOOP_C386=1`, the share per phase (rdtsc). The timers stay in
the source; b72 does not have them.

- `c344b73` (FED_C2M1, 2 rounds): host 23.92 and 23.16 ms (30384 actors walked, 29965 entries, 116 classes); client
  23.08 and 22.80 ms (30348 actors, 29963 and 29965 entries). The client path costs the same as the host's.
- `c276b73` (FED_C1M1): host 2.78 ms (2304 actors, 1496 entries), client 1.73 ms (1641 actors).
- Phase shares on FED_C2M1 (host, round 2): name 29.1, key 18.8, class 12.6, sort 11.7, state 9.4, attach 9.3,
  ext 9.1, tail 0.0 percent. No phase dominates; about 770 ns per actor spread over all of them.
- The suspected driver is refuted: `class` (class name, server-only scan, class table scan) is 11 to 13 percent, about
  2.8 ms. Fix plan step 2 alone would leave about 20 ms.
- The other calls over 1 ms of `c132_digest_step` on FED_C2M1 (10.47 ms in `c276b73`, 12.54 ms in `c355b72`, with
  no round on that map) are `c132_inventory(world, "map load")`, once per map load; no action.

Revised plan: no per-phase fix reaches 5 ms. Either (a) the play build runs the round every 120 s (one 23 ms tick
per 2 minutes per side) and test runs keep 30 s, or (b) the entries are kept between rounds and only the state, ext
and location fields are re-read (the name, key, class and sort phases, about 70 percent, run once per actor). (a) is
a constant plus a play/test switch; (b) needs an actor spawn/destroy feed into the digest and is the larger change.
Decide after the playtest.

## Step 2 (2026-11-15 20:10, option (a), coded, not run)

`c386_round_ms()` in `digest_c132.cpp`: 30 s in test runs (`config::test_skip()`, as before), 120 s in a play build
(`kC386PlayRoundMs`). Effect in play: one 23 ms tick per side per 2 minutes on FED_C2M1 instead of 4, and a quarter
of the digest frames on the wire. Built as `3de22d10fb2861a3` on top of b85; not installed. The source change rides
in the next lane 2 build (card 399).

Proof owed: (1) the next test run still shows `card132` timer rounds 30 s apart with `mismatches 0`; (2) the next
play log shows them 120 s apart. Option (b) stays open if 23 ms per 2 minutes is still felt.


## Closed (2026-11-17 13:09)

Proof 1 met: run `c423b182a` (C2M1, b182) has host timer rounds at 575564951, 575594984, 575624970, 575655005
(30 s apart), every round `mismatches 0`; the same holds in gate `c448b186a` (13 rounds, 0 mismatches). Proof 2
(120 s apart in the play build) can only be read from a play log.

Not done: option (b), spreading the build over several ticks. It was to be taken only if the 13 to 20 ms tick
every 2 minutes is felt in play.

Reopen when: a play log shows timer digest rounds closer than 120 s apart, or the user reports a regular hitch
that lines up with the `card132 ... digest round N (timer)` rows.
