# Card 226: Host pace queue backs up for 30 s when the host runs at low frame rate

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: queued 2026-11-12 (after the playtest), found by card 127 run c127-c1m1. Static trace first. Budget: three runs.

## What was seen

Run c127-c1m1 ran at about 8 fps on both sides (41 frame windows, about 81 frames per 10 s; c140n: 995) while
another heavy job loaded the machine. At that rate the host's per-connection pace FIFO (card 140, `ue/chan.cpp`
`chan_send_client`) peaked at 1545 frames, 965082 bytes, longest wait 32438 ms (`logs/c127-c1m1-host.log:138836`).
Three symptoms followed, all late and none lost:

- 20 client lines "rows N-N not acked by the host within 5000 ms": the LedgerAck (`ledger_c132.cpp:489`) waits in
  the same FIFO.
- 19 of 26 held client ragdoll starts fell back to the copy's own ragdoll after 2000 ms: card 152 skips stream
  samples while the queue holds over `kPoseStreamBacklog` = 8 frames (`pose_c132.cpp:105`, 788, 2474); host totals
  343 backlog skips.
- Digest stayed at 0 mismatches.

## Question

The drain is per frame, so the byte rate falls with the frame rate. A host on a weak machine or in a heavy scene
gets the same backlog. Decide from the code: is the drain budget per frame or per second, and should it scale with
frame time so the byte rate holds at low fps (within CurrentNetSpeed 250000 B/s)? Should acks and pose finals go
ahead of Fx frames in the queue?

## Done when

- At a forced low host frame rate (the game's own frame cap, about 10 fps) the longest pace wait stays under 2 s in
  a combat scene, or the trace shows why the link itself cannot carry it and what is dropped first.

## Static answers (source plus c127-c1m1 and c135-r2 logs, senior, 2026-11-13)

- The drain budget is per frame. `pace_gate` (`ue/chan.cpp:254`) lets a frame go only while `QueuedBits +
  SendBuffer bits + frame bits <= 0`, read live; `chan_pace_step` runs once per game tick.
- Measured: the headroom does not grow with the frame time. c127-c1m1 host windows 137954564 to 137964792:
  `most chan bytes in one frame` 1938, 2000, 2092; 24 to 25 `holds: bits` per 5 s (about one per frame); drained
  46740 to 69516 bytes per 5 s (about 10 kB/s of the 250000 B/s link). c135-r2 at 12 fps: 28604 bytes in the join
  window.
- Not settled by the logs: why the engine leaves only about 2 kB under zero at an 8 fps tick (its own clamp is 2
  ticks of CurrentNetSpeed). Candidates: the engine's actor replication fills to zero first
  (`GNumSaturatedConnections 1359` in c127), or the reading is taken after the flush. One diagnostic row decides.
- Second finding: one FIFO for all ops. Join replay bulk (card 223) and Fx rows sit ahead of hits, ledger acks and
  pose finals.

## Spec, step 1 (diagnostic, build 4; no behaviour change)

- `chan.cpp` `pace_drain`: on a `Bits` hold, at most 20 rows per map: `card226: host pace hold: pending bits P,
  frame bits F, queue N frames, D ms since the last step, sent this step B bytes`.
- Predicted: P between -16000 and 0 at D over 80 ms (the engine's replication took the tick's budget first). If P
  is under -100000, the gate itself is wrong and the fix is in `pace_gate`.
- Step 2 (after the playtest, from that row): a byte budget per second for the queue and two lanes (hits, acks,
  pose finals ahead of replay and Fx). Design waits for the row.

## Root cause (senior, 2026-11-15, static from logs)

The open question above is answered. The engine refills `QueuedBits` by `CurrentNetSpeed * 8 / 120` bits per frame
whatever the frame time (its bandwidth step is a fixed 1/120 s), and clamps at two refills. With 250000 B/s that is
16666 bits, 2083 bytes, per host frame:

- Gate rows show `QueuedBits -33332 to -33332` (2 x 16666) at 25 frames per 5 s (`c127-c1m1`) and at 300 frames per
  5 s (`playb70`, `c276b70`, `c355b70`). The c127 busiest windows carried 2085, 2053 and 2023 B per frame.
- So the link carries 2083 B x host fps: 250 kB/s only at 120 fps, 125 kB/s at 60 fps, 62.5 kB/s at 30 fps,
  16.7 kB/s at 8 fps. Step 1's diagnostic row is not needed.
- Playtest of 2026-11-13 (host log, 649 windows, mean about 86 fps): every gate row has the same floor; the peak
  window (484709 bytes, 276 frames, 52 not ready) used 1756 B per frame, 84% of the ceiling; 1175 not-ready frames
  in 78 windows.

## Fix (b71 `61f3016d372e1413`, `net_budget.cpp` `net_rate_frame`)

The value written to `CurrentNetSpeed` and `MaxClientRate` each tick is `250000 * frame time * 120`, frame time held
in 1/120 to 1/20 s, so one frame's refill is 250000 B/s of real time. Proof runs and result: `docs/STATE.md`
2026-11-15 15:29 entry and the one after it. The two-lane queue (step 2) stays open and waits for playtest numbers.

## b71 follow-up (2026-11-15): possession race at a seamless travel, fixed in b72 `653620cb80ca5f6a`

`playb71` failed at the FED_C2M1 start: the client's `ClientRestart` came before its pawn actor, no
`AcknowledgePossession` followed while the host world was paused by the card 320 hold, so the card 156 restore and
the card 135 GameStarted never ran (60 s hold, then the WARN). The race is in every build; b71's packet order
exposed it. b72 adds `c135_restart_step` (`intro_c135.cpp`): a held, unacknowledged pawn gets the engine's
`ClientRestart` after 1000 ms, at most 5 calls per op. Evidence and predictions: STATE 2026-11-15 16:03.

Guard proof (2026-11-15, `c346b74`, build b74 `ec9e23a2712063a1`, client `UBWCOOP_C135_DROPRESTART=1`): with the
engine's ClientRestart dropped on both gameplay map loads, `c135_restart_step` logged `ClientRestart call 1 of 5`
1000 ms after the pawn came, the pawn was acknowledged and GameStarted followed +1020 ms (FED_C1M1) and +1095 ms
(FED_C2M1, world paused) after the op. Scenario passed in 84.7 s.

## Closed

Closed 2026-11-17 (senior), build b174 `4efbc7838d907625`: step 2, c469b174a with the client at 20 fps: 18 of 22 windows queue peak 0, the 4 others are join/rejoin load windows that the 60 fps baseline has too; no two-lane queue needed.
