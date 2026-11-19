# Card 364: the title-screen hold releases while a connected client is still loading

Found by audit pass 3 (senior, static, 2026-11-15). Fix written by the senior in `mod/src/ubw/intro_c135.cpp`.

## Defect

Card 320 rule: the title screen holds the world until every connected player has confirmed. The hold only waits for
PCs that were sent `IntroStart`, and card 139 (`c139_client_hold`) refuses every op to a PC that has not ponged on
this map. After a seamless travel (card 346, every mission start from the lobby) the client's PC exists on the host
while the client still loads. If the host continues before the client's first Pong, `c135_note_unpause` sends to
0 of 1, `n_waiting` is 0 and `c320_try_release` releases: the mission runs while the client loads, and the client
gets its title screen over a running world.

Evidence: `logs/c346-persistent-host.log` lines 6160 to 6940. Hold on at 336479977 with clients 0; the client's
join replay at 336482340; the host's continue at 336482451. The margin was 111 ms on one machine with the test's
auto-continue (screen read 2474 ms). A friend's slower load loses the race.

Sibling (card 355 rule): a client that leaves while still loading was not counted by `c320_logout` (not in `sent`),
so the host went on alone.

## Fix

- `c320_unsent()`: remote PCs in the hold's world not in `sent`.
- `c320_try_release`: no release while `c320_unsent() != 0`; one `card364: host ... hold stays on` row per map. The
  join replay at the client's first Pong sends the op, adds the PC to `waiting`, and its `IntroDone` releases.
- The card 344 bound is unchanged (60 s after the host's continue, then the host returns to the menu); its WARN row
  now also prints the not-loaded count.
- `c320_logout`: any remote PC that leaves while the hold is on sets `left` (card 355 return), row says
  `not loaded yet`.

## Risk checked

- The old PC of a seamless swap is destroyed without `Logout` (c346 run 3: 0 Logout rows across 2 travels), so the
  wider `c320_logout` does not fire on the swap. The run must confirm: 0 `card355` rows.
- At the client's confirm the old PC is gone (the swap precedes the first Pong), so `c320_unsent()` is 0.

## Predictions (one lane 2 run, with card 365)

Scenario: lobby, mission start by seamless travel, host continues at once (before the client's Pong). Lever if the
order cannot be forced by the driver: none added; the run uses the earliest host continue the screen takes.

- Host: `card364: host ... hold stays on: 1 connected client(s)` when the continue comes first; then
  `hold waits for client slot 1 (join)`, `confirm from client slot 1`, `hold off ... (all confirmed)`.
- If the Pong comes first (as in c346): no card364 row, rows as before. Not a failure; state which order ran.
- 0 `card344` and 0 `card355` rows, 0 new WARN.

## Done when

The run shows one of the two orders above with the hold released only after the client's confirm.

## b50 run 1 (f63840e619112273): first fix missed, second fix written

The first fix counted remote PCs without IntroStart. `logs/b50-host.log`: hold on at 388167227 with clients 0
(line 1659), host continue 388170353 (2071), `hold off after 3126 ms (all confirmed)` (2074), no card364 row; the
client's join replay came at 388174700 (2688) and its confirm at 388177165 was "not waited for" (2875). Cause: during
a seamless travel the client has no PC in the new world until its own load ends. The frontend PC was destroyed at
388170271 and the new `BP_PlayerController_C` spawned at 388172480, so at the continue the PC count was 0.

Second fix (senior, source only, for b51): the travel itself records how many clients it carries
(`c364_expect_clients(players - 1)` in `c346_host_seamless`; 0 at every host Browse that is not seamless),
`c320_send` counts it down, and `c320_unsent` is the larger of that count and the PC count. A carried client that
never arrives is ended by the card 344 bound (host to the main menu), as the user rule says.

Predictions are unchanged, with the row text now
`card364: host t=... hold stays on: 1 client(s) not sent IntroStart yet (no Pong on this map; 1 carried by the seamless travel)`.

## Result (b51 run 1, build f46ae0eb6b1386d6, logs/b51-*.log)

Seamless travel case (FED_C1M1 to FED_C2M1, the host continues at its prompt ready): passed, all rows in order.
Host 4226 hold on (FED_C2M1) clients 0; 4880 confirm from host; 4881
`card364: ... hold stays on: 1 client(s) not sent IntroStart yet (no Pong on this map; 1 carried by the seamless travel)`;
4950 hold waits for client slot 1 (join); 5606 confirm from client slot 1; 5607 `hold off after 4649 ms (all confirmed)`.
0 card344, 0 card355.

Cold start case (the harness start: the host Browses frontend to FED_C1M1 while the client is standalone on
3D_Frontend, net mode 0): still released with no wait, as in b50 run 1. Host 1659 hold on (FED_C1M1) clients 0, 2074
`hold off after 3144 ms (all confirmed)` at 388870518; the client's session join is at 388875453 (host 2610), and 2877
`confirm from client slot 1 (not waited for ...)`. That Browse is a hard travel with no client connected, so the expect
count is 0 and nothing on the host knows a client will come. Whether this case is in scope is the senior's call.

Build fix: `c364_expect_clients` was defined inside the anonymous namespace in intro_c135.cpp, so the header's
declaration had no definition (-Werror unused function). Moved after the namespace's end.
