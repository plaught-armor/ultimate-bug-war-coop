# Card 366: host stays down for good when its last teammate leaves

Found by the third audit pass (player state), static. Senior fix, one condition.

## Defect

`c43_host_step` (death.cpp) ran the all-down decision only with `r.players >= 2`. The host is Downed or Out and
the only client leaves (quit, crash, 20 s `ConnectionTimeout`): `c43_logout` removes the client's entry, the roster
drops to 1 player, the host's entry stays Downed then Out (walk speed 0), and nothing ever runs the host's real
death. No death screen, no revive possible; the pause menu is the only way out.

## Fix

The `r.players >= 2` term is gone from the all-down decision. With an entry present and nobody Up, the host's real
death runs as in the two-player case (`c43_all_down`, then `c43_kill_host`); the row reads
`all down: last teammate left (card 366)`. Solo play is unaffected: with one player the gate in `c43_death` is off,
so no entry exists and `g_c43.count` is 0.

## Predictions for the run

Host Downed (card 043 test lever, client Up), client process ends. Host log, in order: the `Logout` rows,
`card043 ... all down: last teammate left (card 366)`, `real-death hit 1`, `host real death: 1 hits, Health 0.0`,
then the game's own death screen. 0 new WARN.

## Done when

The rows above are in one host log and a screenshot shows the death screen.

## Result (b51 run 1): run B skipped

The card 043 test lever cannot put the host Downed with the client Up on a map with AI troopers. On FED_C2M1 the card
130 AI pick-up (BP_AI_Trooper_Morita_C) revives the downed copy before it goes Out (host log row
`card130: ... AI pick-up done ... picked up player BP_PlayerCharacterMI_C (slot 1) after 3018 ms near`), so the test
stays at WaitOut (stage 3) and never reaches KillHost (stage 6 -> 7). Same in b50 run 2. On FED_C1M1 the test arms solo
(the host is on foot before the client's PC joins). Reaching the host-down stage needs new test code (for example, a
lever that skips the copy's half, or one that turns off the AI pick-up), so per the order no lever was written.

## Result (b51 run B, build f46ae0eb6b1386d6, logs/b51b-*.log): passed

Card 109 `host down host` on FED_C1M1, then `client quit` at once (scenario tools/scenarios/b51b-c366.txt). Host log:
2941 host-own `-> Downed`; 2995 card130 AI pick-up starts toward the host (3457 uu, too far to land before the exit);
3010 card049 `BP_PlayerController_C left`, 3011 card140 `Logout`; 3014
`-> Dead (all down: last teammate left (card 366))` 2 ms after the Logout; 3035 `real-death hit 1 ... Health 1.0 -> -899.0`;
3056 card130 pick-up dropped: all down; 3058 `host real death: 1 hits, Health -899.0` (no WARN prefix: Health is at or
below 0; the prediction's "0.0" was the wrong number, so the scenario's wait failed on that text).
Shot host-b51b-fail-line25 (07:48:21, 13 s after the death): the game's death screen (RESTART CHECKPOINT, RESTART
MISSION, LOAD GAME, EXIT). New WARN: host 3013 `WARN card209: host Down Dead for pawn id ... (slot 0) not sent: guid 0x0
not sendable` (the Dead frame after the only client left). Client: none new.
