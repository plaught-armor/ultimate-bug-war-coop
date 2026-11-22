# Card 540: performance mod, same-frame memo of DoesTargetMeetConditions

Family: card 527 (performance mod; it must never change a host decision). It follows card 538's counts.

## Why

c263L358 (b352) card538 rows on the host:
- peak window: 52860.8 calls per frame (64860160 calls in 1227 frames);
- 33.6% of calls repeat the same (component, conditions, target) in the same frame on the same thread;
- 0 of those 25.3 million repeats returned a different result. With the memo off, every repeat is compared.

L355 costed the function at 3.1% of Background Work, plus 1.8% for the FADTargetData TSet find it reaches. Both
are cache-miss bound: the target's data row is a 128 B TSet element chain, and the health component sits behind
FindComponentByClass and then a pointer. A full-result repeat skips all of those misses.

The client makes 563.9 calls per frame and gets no true results, so the lever is for the host.

The static trace (exe 0x1451866a0) shows what the original reads:
- the owner's targeting component (this + 0xa0, then + 0xa60);
- the FADTargetData row for the target (exe 0x145189f50);
- the target's health component: valid flag, dead check, health and max health ratio;
- one more component of the target, checked against the tag queries at conditions + 0x18/+0x38/+0x58.

It writes nothing outside its own locals. The conditions pointer is the caller's attack config + 0x10
(IsAttackUsableAgainstTarget, exe 0x145196ea0), so it names stable data, not a stack temporary.

## What (b354)

The card 538 detour (`perf_target_c538.cpp`) gains the memo, so one detour owns the call.
- The (component, conditions, target) table grows from 1024 to 4096 slots per thread (160 KB) to cut evictions.
- A same-frame repeat returns the stored result.
- One repeat in 256 still calls the original. If the result differs from the stored one, it is counted, and the
  stored entry is refreshed.
- The table is per thread and keyed by the frame, so nothing is shared and nothing outlives the frame.
- The card538 row adds `card540 memo on|off, served N, verified N`. `differ` then counts verified repeats only.
- `UBWCOOP_C540_OFF=1` restores counts only (the A/B switch).

## Predictions (b354 run)

- served is at least 30% of host calls;
- differ is 0, with verified around served / 255;
- host BG: DoesTargetMeetConditions + 0x145189f50 + detour_c538_conditions drops by at least 25% against L358;
- mismatches 0, CRASH 0.

If differ is above 0, the memo is wrong for some caller: turn it off and trace which state changes inside a frame.

## Next (a DOD gather, only if the counts support it)

Same target in frame is 43.8% of host calls, which is a lower bound. If served stays near 33%, the next step is a
per-frame packed table of per-target health state, gathered once per target. The call would then read it instead
of chasing the health component pointer. That needs a reimplementation of the health part of the original, so it
waits for this card's differ result.

## Result (c263L360, b354)

- Host peak window: served 33971865 of 79753216 calls (42.6%), verified 133215, differ 0. Mismatches 0, CRASH 0.
  The client gets no served calls (same conditions 0%).
- Host BG: DoesTargetMeetConditions fell from 2.7% to 2.0% (313 to 249 samples), but detour_c538_conditions rose from
  0.7% to 1.7% (75 to 211). The sum rose from 3.4% to 3.7%, so the BG prediction failed.
- GameThread: the sum fell from 0.8% to 0.5%.
- Reading: the 128 KB per-thread table (4096 rows of 32 B) plus card 538's pair table cost two cache lines per call,
  and those misses ate the saved work.

## b356 (dafb85fe1cf3e172)

- The table is halved to 2048 rows (64 KB).
- The pair table (card 538's same-target count) is skipped while the memo is on. That leaves one row touch per call,
  and same target then reads 0.
- Prediction: the detour plus the original costs less in BG than L358's 3.4%. If it does not, the memo stays on only
  for its GameThread gain, or turns off by default. A packed per-target gather (see Next) is the remaining route.

## Result (c263L362, b356)

- Host peak window: 64946 calls per frame. That is 23% more than L358 (52860), from the heavier peak in this run.
  Served 39.3%, differ 0. Mismatches 0, CRASH 0.
- Host BG samples (DoesTargetMeetConditions + detour):

  | Run | Samples | Share of BG |
  |---|---|---|
  | L358 | 313 + 75 = 388 | 3.4% |
  | L360 | 249 + 211 = 460 | 3.7% |
  | L362 | 310 + 154 = 464 | 3.75% |

  The detour's cost fell from 211 to 154 samples, so table size was part of it. Per call, the pair costs about 11%
  less than L358, because L362 made 23% more calls for 20% more samples. Total BG is flat.
- GameThread: 0.64%, against 0.86% in L358.

## Decision

- The memo stays on: it is correct, saves GameThread time and costs nothing net in BG.
- Card 536 lookup class rows (L362 host) show ADHealthComponent is 71.7% of sampled FindComponentByClass calls. The
  next items are ADAbilitySystemComponent (16.4%) and PawnMovementComponent (8.7%). On the client,
  ADAbilitySystemComponent is 90.2%.
- The memo cannot go further, since 60% of calls are first calls in a frame. Further gains must make the original
  cheaper per call: the per-target health gather (Next).
