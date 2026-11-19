# 315 A looping mover's phase is set once and lost on the next frame stall (card 313 follow-up)

From run 3 of the b17 C1M1 proof (DLL `2df06e3cf7129696`, host log `UBWCoop-host-20261114-035445.log`, client log
`UBWCoop-client-20261114-035447.log`). Senior trace 2026-11-14. The card 313 arithmetic is proven right; the cause of
the remaining gap is supported by the rows below but not proven. Step 1 is the row that proves or kills it.

## Result of card 313 in run 3

- Prediction 1 holds: `289498697 ... _C_1|MovementTimeline ... applied timeline state: position 4.24 + 343 ms = 4.58
  of 6.00, playing 1` (host sample 289498353, apply 344 ms later).
- Prediction 2 fails: 38 `WARN card132: mismatch` rows for `TargetRangeTarger_C_1` and `_C_2` in rounds 21 to 40.
  Card 237, 14 ms apart: client z -78, -82, -82, -86, host z -322, -326, -326, -330. Both descend at the same rate
  (4 uu per round, 17 ms of phase per round), 244 uu apart: the client runs about 1.1 s behind. In b16, with no
  ageing, it was 0.3 s behind.

## Supported cause

The client stalled right after the state row. Client log: no row from 289498919 to 289499979 (1060 ms), then 371 ms;
the placed row sent at 289499169 is applied at 289500371, 1202 ms late. The engine caps one frame's delta time
(`MaxUndilatedFrameTime`, default 0.4 s), so a timeline loses the rest of a long frame. Host and client stall at
different moments around a load, so a phase that is set once drifts by every stall after it. The host log has gaps
of 2685, 936 and 918 ms in the same window.

## Step 1 (diagnostic, in the next batched run)

Card 237 rows for an actor with a playing timeline carry `timeline <name> position <p> of <len>` on both sides.
Prediction: after the load the client position is 0.9 to 1.3 s behind the host's in every round, and equal (within
0.1 s) before the load. If the positions are equal while the z differs, the cause is not the phase and this card
stops there.

## Fix (after step 1 confirms)

The host repeats the state of every playing looping timeline it relays: position plus sample time, on the existing
3 s placed scan (card 140), only for timelines already in the card 185 list. The client ages it the card 313 way and
sets the position only when its own differs by more than 100 ms. Row once per timeline per map, tally `card315:
client looping timelines <n>, corrections <n>, largest <ms>`.

## Sibling sweep (report before coding)

The card 313 sibling list (136 FX, 161 doors, 159 dropship, 132 decal, pose rows): which of them keep running on
the receiver from a value applied once. Anything that does is the same class.

## Predictions

Zero `WARN card132: mismatch` for keys 910311a0 and 94031659 in every round after the load; corrections at least 1
after the load, 0 before it.


## Step 1 result: cause proven (run `c315b167a`, b167 `a183768a97f78666`, 2026-11-16 21:32)

Scenario `c315-range-load-c1m1.txt` (range targets activated, then a checkpoint load), passed 197 lines.

- Host replay 524975652: `_C_1|MovementTimeline: position 1.69 of 6.00, playing 1`. Client 524975673: `applied
  timeline state: position 1.69 + 36 ms = 1.72 of 6.00, playing 1` (card 313 arithmetic right again).
- Client log gap right after it: 524975680 -> 524977592, 1912 ms with no row (one stalled frame after the restart).
- Card 237 round 5: host 524980383 position 0.420 (1.69 + 4.73 s, wrapped: as expected); client 524980366 position
  4.850, where 1.72 + 4.69 s wrapped gives 0.41. The client lost 1.56 s: the 1912 ms frame minus the engine's
  frame cap. Rounds 7 and 8: host 0.136 / 0.122, client 4.567 / 4.550: the same 1.57 s, no further drift.
- 6 `WARN card132: mismatch` rows for `_C_1` and `_C_2` (keys 910311a0, 94031659) in rounds 5, 7, 8; rounds before
  the load: none for these keys.

The prediction holds: equal before the load, a constant offset after it that matches one stalled frame. Cards 311
and 313 did their part in this run (replayed Activate applied, aged position applied). Fix goes into b168.


## Closed 2026-11-16 22:29: run `c315b168a` (b168 `0c097c2488d82959`)

Before the load: 2 looping timelines, 0 corrections. After the checkpoint reload: one correction per target (own
3.646 -> host 4.894, about 1248 ms), then "already in phase"; 0 digest mismatches in rounds 1 to 8. Cost about 230 B
per 3 s scan. Leftover for b169 (no state fault): the first sample after the reload has no age (2 s send backlog
during the load) and logs `WARN card315 ... no age for the host sample` once per target; that row becomes a plain
row while the journal replay of a join is not older than the first aged sample. Sibling sweep: the same class is
card 136 FX (`fx_c136_net.cpp:8-34`); doors, dropship, decals and pose are not this class.
