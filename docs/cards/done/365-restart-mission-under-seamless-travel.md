# Card 365: RESTART MISSION after game over has never run under seamless travel

Found by audit pass 3 (senior, static, 2026-11-15). No code change expected; one targeted run.

## Gap

Card 045 proved RESTART MISSION (the host's Browse to the same map, 0 options) with the old hard path: `listen`
added, client dropped, client follow reconnects. Since card 346 (b31) `c346_host_seamless` takes every local host
Browse with 2 players except the pause-menu quit, so RESTART MISSION is now a seamless `ServerTravel` to the map
that is loaded. Every seamless travel in the logs went to another map (3D_Frontend 20, FED_C2M1 18, FED_C3M1 1);
none went to the current map. Game over is likely in a playtest.

RESTART CHECKPOINT is an in-world reload (card 045, no Browse) and is not touched by card 346.

## Run (lane 2, with card 364)

The card 045 test levers (`UBWCOOP_C45_TEST=1` with `UBWCOOP_C43_TEST=1`): all down, host presses RESTART MISSION.

Predictions:
- Host: one `card346: host ... seamless travel to "/Game/Maps/FED/FED_C1M1"` row, `started by ServerTravel`.
- Client: 0 `session dropped`, 0 `reconnected`, 0 network failure rows; worlds FED_C1M1, Untitled, FED_C1M1.
- Both pawns on foot after the restart, client death screens gone, card 320 hold released by the client's confirm.
- One down and revive after the restart works.

If the travel is refused or the client drops: stop, report rows; the fix candidate is the hard path for a URL whose
map equals the current map (card 045 behaviour), decided by the senior.

## Done when

The run passes, or the defect is carded with its rows.

## Result (b50 run 2, build f63840e619112273, logs/b50r2-*.log)

Passed. The restart ran on FED_C2M1, not FED_C1M1: in b50 run 1 the test armed solo on C1M1 (host on foot before
the client's PC joined), so run 2 first made a seamless travel to C2M1.

- Host 7733: `test restart 1: RESTART MISSON (RestartMission)`.
- Host 7748: `card346: ... seamless travel to "/Game/Maps/FED/FED_C2M1" started by ServerTravel, Browse not run (2 players ...)`.
- Client card052 worlds: 3080 Untitled, 3671 FED_C2M1 (the first travel), 8026 Untitled, 8665 FED_C2M1 (the restart).
- Client: 0 `session dropped`, 0 `reconnected`, 0 network failure events (only the two hook-install rows).
- Restart hold: host 9243 hold on, 9875 confirm from host, 9876 card364 hold stays on, 9937 hold waits for client slot 1
  (join), 10608 confirm from client slot 1, 10609 `hold off after 4613 ms (all confirmed)`.
- Host 10756: test stage 0 -> 1 (phase 1, both on foot, players 2). Shots b50-after-restart-host/client: both on foot,
  health 100/100, no death screen.
- 0 card344, 0 card355. Host FATAL 0. New WARN on the host (both also seen with the seamless travel to C2M1): card346
  `guard: GetViewTarget on a null camera manager`, and `autodropship: pause requester ... null`. Client WARN: 22 card140
  hit-drift rows from the phase 1 fight.
- The scenario failed later, at the `test restart 2: RESTART CHECKPOINT` wait (outside this card): the card 130 AI pick-up
  (BP_AI_Trooper_Morita_C, host 12210 and 12688) revived the downed copy before Out, so the card 043 test waited at
  WaitOut until the timeout.
