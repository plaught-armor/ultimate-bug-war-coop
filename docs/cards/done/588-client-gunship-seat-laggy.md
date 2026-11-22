# Card 588: client view in the gunship looks laggy

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: open (user report 2026-11-21: "The client looks laggy in the gunship"). Diagnostic rides the next build.

## Static trace (senior, 2026-11-21)

- The gunship is the Door Gunner seat `BP_MountableTurret_Gunner_C` (FED_C3M1). There is no separate chopper actor.
  The host's ReceiveTick flies the seat along `Spline.SplineEntry` and then `SplineCircle` with
  `K2_SetActorLocationAndRotation`, at about 1200 uu/s (c582b414a). Only the host's spawn sets `Spline`, and card 561
  keeps the client copy's actor tick off, so the client copy never runs the flight.
- The seat is an ACharacter. On the client that rides it, the seat is that client's possessed pawn, LocalRole 2
  (AutonomousProxy). `ReplicatedMovement` is COND_SimulatedOrPhysics, and `AActor::OnRep_ReplicatedMovement`
  (RVA 0x0382B5D0) applies it only when LocalRole is 1 (SimulatedProxy) and the root has no attach parent. So the
  replicated movement path does not move the client's own seat.
- No mod code writes the seat transform (turret_sync.cpp has no SetActorLocation).
- The client copy does move: the card271 row at 168105744 put the host and client copies 74 uu apart. The most
  likely mover is the host CMC's `ClientAdjustPosition`. The client's ServerMove reports a position that the host
  flight left behind, so the host sends a correction, which arrives one round trip late and snaps the seat. The view
  then steps forward in jumps and trails the host by about rtt × 1200 uu/s, which is what "laggy" looks like.
- The earlier "0 ClientAdjustPosition rows during the ride" is not evidence against this: `note_client_adjust` logs
  through a capped `note_event` table, and the whole log had only its 16 capped rows.

## Diagnostic (seat_c588.cpp, client only)

- While the client's own pawn is a turret, `c588_step` (ubw_post_tick) measures every frame's location step.
- A row every 1 s (at most 120 per map) gives: frames, frames that moved, mean and largest step, `OnRep_ReplicatedMovement`
  calls on the seat (a new hook, one pointer compare per call), `ClientAdjustPosition` calls on the seat's CMC with
  their mean and largest correction distance, and how far `ReplicatedMovement.Location` sits from the actor.
- A map-end tally is written as `card588: client seat motion`.

## Predictions (written before the run)

1. The rows show `ClientAdjustPosition` at a few per second, with a mean correction of about rtt × 1200 uu/s
   (40 to 60 uu at a 40 ms rtt). The number of moved frames is far below the frame count, and the largest step is
   much larger than the mean step. Together, those are the steps the player sees.
2. `OnRep` is 0, or nonzero with no effect, on the own seat (role 2).
3. If prediction 1 fails (no corrections, but the seat moves every frame), another path moves the seat. Find it
   before any fix.

## Fix direction (after the data)

- If corrections are confirmed: the client copy flies its own flight, the same way as the dropship (card 159). The
  client gets the spline and the MoveTime/FlightStage state, and host rows correct it with a lead of speed × rtt/2.
  The CMC corrections for the seat are dropped because the host flight owns the position. This is a class fix for
  every host-flown seat that a client rides.

## Related gap, same class

- c582b414a client, t=168066685: `WARN card467: client turret row BP_MountableTurret_Gunner_C_2147465062 ... no turret
  copy after 10000 ms, dropped`. That was the host's seat while the host rode it. Check whether the client had a copy
  at all (net relevancy of a seat flying far from the client) when the next run gives the card588 rows.

## Result c588b415a (b415 `8b4b4e097f486dce`) and fix (senior, 2026-11-21)

- 17 client rows, prediction 1 confirmed. Each row: 60 frames, 15 to 17 moved, 15 to 17 `ClientAdjustPosition`, and
  the moved count equals the correction count. Correction mean 67 to 86 uu (max 93 to 190, the mount row 7724), 0
  OnRep (prediction 2). Only the corrections move the seat: the view holds for 3 to 4 frames and then jumps about
  75 uu, at about 16 Hz. `ReplicatedMovement` sits 3500 to 6800 uu from the actor, a stale value (not replicated to
  the autonomous proxy), which plays no part.
- Fix (seat_c588.cpp, client, own turret only): each correction is a sample of the host's location. Two samples give
  the velocity (averaged with the last one; a jump over 1000 uu or a speed over 5000 uu/s is a teleport and resets it).
  Every frame (ubw_post_tick) the seat is placed at sample + velocity × (time since the sample + rtt floor / 2, capped
  at 250 ms). The correction itself is led the same way before the CMC applies it, so the correction frame does not
  step back. No correction for 250 ms: the seat holds. A base-relative correction is passed through.
- Kept as a lead on the correction stream instead of the dropship-style local flight: no spline, no BP tick on the
  client, and no Begin Orbit / Fly Away side effects. It applies to any own turret pawn the host moves.
- Prediction for the next run: in the card588 rows, frames led and moved each about equal to frames (59 to 61), step
  mean about 1200 / 60 = 20 uu, step max under 60 uu outside the mount row, and the correction distance (server vs
  the led seat) mean under 40 uu. The map-end row shows the teleport sample count (1 or 2 per seat: mount, and the
  fly-away exit).

## Result c588b416a (b416 `d0fba6bb3daff98e`): fixed, user to confirm in play (senior, 2026-11-21)

- 16 client rows. 13 steady rows: every frame moved and led (60/60), step mean 18.6 to 22.0 uu (speed 1114 to
  1329 uu/s, lag 22 ms), step max 21 to 29 uu, correction distance mean 10.7 to 14.2 uu. Prediction met.
- Mount row: 8978 uu (the teleport into the seat), as predicted.
- Three single-step spikes: 85 uu at 169542912, 131 uu at 169548961 and 121 uu at 169551978. The 169548961 row also
  has 5 frames not led (a correction gap over 250 ms, then a hold), and the speed changes from about 1120 to about
  1300 uu/s right after it, so this is the entry-to-circle switch. Linear extrapolation overshoots on that curve.
  These are one-frame steps of about 0.1 s of flight each, far below the earlier 16 Hz stepping. No further work
  unless the user still sees a jolt at the stage switch.

## Closed (senior, 2026-11-22)

The fix is proven by logs and runs, and the proof is in the sections above. Under the no-playtest-parking rule, a proven fix does not stay open waiting for a play check. It is on the playtest checklist in STATE.md. If the user sees it again in play, reopen this card with that report.
