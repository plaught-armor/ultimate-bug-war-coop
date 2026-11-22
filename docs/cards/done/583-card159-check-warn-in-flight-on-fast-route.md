# Card 583: card159 check WARN on in-flight dropships while live rounds agree (FAST C1M1)

Found in c551b384a (b384, FAST=1 FED_C1M1), client log; 1 of the last 6 c551 runs (b370 to b384), 0 in the other 5.

## Rows

- Join round t=149740777: `BP_ProceduralDropshipTroops_C_3` state 2/0, 23729 uu off; `BP_PlayerTransportDropship_VerySlow_C_1`
  state 2/0, 13407 uu off (the client had not started them yet).
- First live round t=149741258: both in flight and in sync (130 uu off, tolerance 1879; 1 uu off, tolerance 1146).
- Check round t=149746691: `WARN card159` on both: 4723 uu off (tolerance 560, speed -7413, rtt 24, spline distance
  84505/89336) and 2542 uu off (tolerance 370, speed -4361, spline distance 15712/18242), "in flight".
- Live rounds right after (t=149752557, 149753714): 64 and 49 uu off, 9 and 5 uu off.

## Question

The live rounds before and after the check agree within about 130 uu, and the check row sees 4723 uu (about 0.64 s
of travel at the reported speed). Either the check compares the host's sample against the client's position at a
different time (the check's tolerance, 560, is about a third of the live round's 1879 at the same speed), or the
client really drifts for one interval between live rounds. Not a FAST route class in STATE.md: the old in-flight
card159 class (c344 notes) had 0 live dropship frames; this one has live frames on both sides of the check.

## Next step

Static: read `c159_dropship_step` in `dropship.cpp` for how the check round's host sample time and tolerance are
built against the live round's. No run until the static trace makes a prediction.

## Static trace and log read (junior, 2026-11-21)

Code (`dropship.cpp`):

- `c159_dropship_step` (host) sends the C round from `ds_snapshot_send`: the live `ds_look` at send time and the
  tracker row's last sampled speed. No send time travels in the frame.
- `ds_client_snapshot` (client), kind C: compare only. `off` is the copy's location against the host's location
  with no lead and no allowance for the frame's age. The tolerance is `ds_tolerance(speed)` = 100 + |speed| x
  (rtt floor / 2, capped at 250 ms, + 50 ms slack) / 1000.
- Kind J and L: the same `off` test, but the distance is applied as host distance + speed x lead (lead = rtt floor / 2,
  0 for `kDsLoadSettleMs` after the client's map load).

Log (c551b384a, C_3 rows; spline distance client/host):

| t | kind | off | tolerance | distance |
|---|---|---|---|---|
| 149745517 | L | 0 | 1898 | 93927/94717 |
| 149746691 | C | 4723 | 560 | 84505/89336 |
| 149746700 | L | 1473 | 561 | 84434/85936 |
| 149747871 | L | 223 | 562 | 77075/77297 |

- Through this window both games run 440-950 ms frames, on the same ticks on both sides (card303 rows, for example
  host 746229/746688 and client 746229/746689).
- The host's distance in the C frame (89336) is 3400 uu behind the host's distance in the L frame 9 ms later
  (85936). 3400 uu at 7413 uu/s is 459 ms, the host frame of 149746688. The C frame was built one host frame before
  the L frame and was compared in the same client frame: it was one 459 ms frame old at the compare.
- The other 1323 to 1473 uu: the copy runs ahead of the host. The L at 149745517 had tolerance 1898, so the rtt floor
  was about 386 ms (lag 193 ms). That L led the distance by 193 ms x 7413 = 1430 uu. The 9 ms later L row shows the
  same 1473 uu. The rtt floor was inflated by the long frames: a Pong waits for the next frame.
- 4723 is about 3400 (frame age) + 1430 (the old lead). The C round's tolerance (560) uses the new floor, rtt 24.
- The VerySlow transport matches the same way: a 2542 uu check, then 746 uu on the L 8 ms later.

## Verdict

A false WARN from the check's design, not a drift of the copy. The live rounds keep correcting the copy. Two parts:

1. The C compare has no allowance for a frame that is one long host frame old.
2. During frames of 450 ms or more, the rtt floor reads 300-400 ms. The L lead then places the copy up to 250 ms
   ahead, and the copy stays there until the floor drops (here about 1.4 s of drawn lead, then 223 uu).

Part 2 is player-visible only as a ship up to 250 ms ahead during a loading hitch, which is under the card 159
tolerance at the time.

## Proposed change (not made)

The C compare uses the led target and `dist_tol`, as the J and L kinds already do, with the slack raised by the
host's last frame time. Alternatively, the C frame carries the host's frame ms and is compared against host
distance + speed x (frame ms + lag). Prediction for either: on the next FAST C1M1 run with long load frames, C rows
stay within tolerance whenever the L row right after them is within tolerance (here 1473 against 561 would still
fail on part 2). Part 2 needs the rtt floor sampled outside frames over 250 ms (card 340 owns the floor). No run
until the senior picks one.

## Part 1 fix (b411 `0b694ba883d43fae`, `dropship.cpp`)

- The host keeps its last frame time (`c159_dropship_step`, now minus the previous step) and puts it in every frame's
  text before the kind (field 17 of 19; the client parser needs all 19).
- The C round's check now compares the led target's distance with `dist_tol` plus the host frame's travel
  (`|speed| * host frame ms / 1000`), not the location with `ds_tolerance`. The field match (state, hidden, mesh,
  nav, startup, target, spline) and the rotation check are unchanged.
- The check row adds "led distance N off (tolerance T, host frame F ms)".
- Part 2 (the rtt floor in long frames) stays open.

## Part 2 fix (senior, 2026-11-21, rides b414; `playlog.cpp`, `fire.cpp`, `stall_c303.cpp`)

- Cause: during a long frame, a Pong sits in the socket until the next net read. The round trip then measures the
  frame, not the link, and the card 340 floor (lowest of the last 5) read 300-400 ms after a run of 450+ ms frames.
- `c303_tick` keeps the last full frame (post tick to post tick) in `g_c303_last_frame`, read by
  `c303_last_frame_ms()`. `c26_pong` passes it to `pl_rtt(ms, frame_ms)`.
- `pl_rtt` keeps a sample read after a frame over `kPlRttHitchMs` (250 ms) out of the floor ring and counts it in
  `rtt_hitch`. The sample still goes into the last, min, avg and max stats. The floor keeps its pre-hitch value. Before
  any clean sample, the floor stays 0 (no lead), the same as before the first Pong.
- The playlog rtt row adds ", N after a long frame, floor F".
- Limit: a hitch on the host side (the host's Pong waits in its own long frame) is not visible to the client. The
  lowest-of-5 floor still absorbs a single such sample.
- Prediction for the next FAST C1M1 run with long load frames: the rtt rows show "after a long frame" greater than 0
  during the loads, the floor stays under 100 ms, and no L row leads the copy by 250 ms. The C/L check WARN pair from
  c582b411a (1473 against 561) does not appear.
- Syntax checks for the three files returned rc 0.

## Proof run c584b432a (b432 `06c5597f6e2354bb`, FED_C1M1 FAST, 2026-11-21)

- card159: "check 4 of 4 match", 73 live frames; the two in-flight check rows read 0 uu off (tolerance 647, speed
  -8358) and 79 uu off (tolerance 415, speed -4805), both at rtt 31. No card159 WARN on either side.
- rtt rows: "rtt 10 pings: min 30 avg 32 max 43 ms, 0 after a long frame, floor 30" and "12 pings ... floor 31". The
  floor stays under 100 ms.
- Not exercised: the hitch counter. The client's only long frame (card303, 1411 ms at t=180868155) ended before the
  client started its Pongs (card120 "Pong to the host from now on" at t=180868258), so no ping sample fell after a
  long frame. Parts 1 and 2 hold on this run (no WARN pair, low floor); the hitch skip itself has no sample yet.
- Logs: logs/c584b432a-host.log, logs/c584b432a-client.log.

## c448b442a check (b442, 2026-11-21)

- rtt rows: host 10-11 pings, floor 21-25 ms, max 431-455 ms, 0 after a long frame; client 10-11 pings, floor 29-30
  ms, max 58-103 ms, 0 after a long frame. No card159 WARN pair on either side. Parts 1 and 2 hold again.
- The hitch counter is still unexercised: no ping sample fell after a long frame on this run either. The host's
  431-455 ms max pings are not counted as hitch samples, so a long ping without a long frame on the host is a lead
  only if it repeats (not chased).
- Logs: logs/c448b442a-host.log, logs/c448b442a-client.log.

## Close (2026-11-22)

Closed: fixed. Cause: the C round compared a host sample that was one long host frame old with no allowance for its age (part 1, b411). The L lead also used an rtt floor inflated by long frames (part 2, b414). Proof: 62 client logs from b414 through b454 that carry card159 rows have 0 `WARN card159` rows. Those include c584b432a, c448b442a, c448b453a and c448b454a, where the check reports "4 of 4 match" and the rtt floor is 27-44 ms. The hitch skip in `pl_rtt` has still not seen a sample ("0 after a long frame" in every run), so it is unexercised code, not a proven path. A future WARN card159 pair during a load hitch reopens this card.
