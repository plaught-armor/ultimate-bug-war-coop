# 313 Replayed timeline state is stale by the send delay: looping movers run behind the host after a load

From the b16 C1M1 proof run (DLL `4443e6922875cf3a`, host log `UBWCoop-host-20261114-024347.log`, client log
`UBWCoop-client-20261114-024349.log`). Senior trace 2026-11-14, logs and code. Cause settled from the rows below; the
size of the lag is the one number the run must confirm.

## Symptom

After the checkpoint load at 285434xxx the two cycling range targets (`TargetRangeTarger_C_1`, key 910311a0, and
`_C_2`, key 94031659) mismatch in every digest round 28 to 35: `WARN card132: mismatch ... location host
(6000,14850,-350) client (6000,14850,-150)` (27 rows for `_C_1`, gaps of 150 to 200 uu, tolerance 100 uu). Rounds 20
to 27, before the load, had 0 mismatches while the same targets cycled (host 'A' rows alternate z 114 and -336 every
3 s).

## Cause

Both copies move after the load; the client's copy runs behind the host's by a fixed phase.

- Card 237 rows, same round, 20 ms apart: client z -336, -331, -327, -335, host z -264, -272, -275, -268. Both sides
  are mid-cycle and the offset is steady (63 to 72 uu), so both loop at the same period and differ in phase only.
- Host 285434562: `replay timeline ..._C_1|MovementTimeline: position 3.67 of 6.00, playing 1` (the position is read
  when the replay is built). The row leaves at 285434781 (`sent 246`), and the client applies it at 285434790 as
  position 3.67 (`c185_client_state`, `SetPlaybackPosition(pos)` then Play). By then the host's timeline is at 3.90.
  The copy stays 228 ms behind for as long as the timeline loops.
- The card 140 placed rows ('A', every 3 s, all `result applied`) cannot correct it: they set the actor's place once
  and the client's own timeline moves it again on the next tick.
- Card 311's replayed `Activate(1)` is not the cause: it is applied first (285434757) and the state row sets the
  position after it.

Before the load the client's timeline was started by the live 'L' row (285046254), a few ms behind the host, inside
the tolerance.

## Fix

A replayed timeline state must be the host's position at the time the client applies it.

1. Host: read the position when the row is sent, not when the replay list is built (or carry the sample time in the
   row). Report which one the row layout allows before coding.
2. Client, `c185_client_state`, playing rows only: advance the position by the row's age, `(now - sent t - clock
   offset) * play rate`, forward for Play, backward for Reverse, wrapped or clamped the way the timeline itself does
   (looping flag, length). The clock offset is the card 155 estimate (`c132l_clock_sample`); the ledger stamp gives
   `sent t`. An age below 0 or above 2000 ms is a WARN and the position is applied as sent.
3. Row: `card098: client ... applied timeline state: position <sent> + <age> ms = <applied> of <len>, ...`.

## Sibling sweep (report before coding)

Every replayed or join-time row that carries a sampled value of something still moving on the host: timeline
positions (185), play rate rows (261), dropship and vehicle path positions, door and lift movers (095, 161), montage
positions, looping FX start times. For each: is the value aged on apply, and does the receiver keep running from it.

## Predictions (inside the next C1M1 run, after the checkpoint load)

- The new client row shows an age between 150 and 400 ms for `_C_1|MovementTimeline`.
- Zero `WARN card132: mismatch` rows for keys 910311a0 and 94031659 in every round after the load.
- Card 237 rows, if any other key mismatches in the same round, show the two targets within 30 uu of each other.

## Done when

The predictions hold in one run and the sibling list is in the report.

## Closed 2026-11-16 22:30

run `c315b168a` (b168 `0c097c2488d82959`): the replayed rows carry an age (`position 1.82 + 41 ms = 1.86 of 6.00`; the 150 to 400 ms range was the old run's send delay), 0 mismatches in rounds 1 to 8. What the age cannot see (the journal sample itself was about 1.2 s old after the load backlog) is corrected by card 315 within one scan (`corrected ... own 3.646 host 4.894`). Sibling list: card 315 closing section.
