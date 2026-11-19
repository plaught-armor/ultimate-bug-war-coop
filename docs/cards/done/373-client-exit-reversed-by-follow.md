# Card 373: client death-screen Exit is reversed by the card 045 follow

Found 2026-11-15 by run `c374a57` (b57 `78025485639b5da3`, scenario `c374-client-exit-rejoin`).

## Defect

A dead client presses Exit on the death screen. The game's `OpenMenuLevel` Browses
`/Game/Maps/3D_Frontend/3D_Frontend?FromGame` locally. `c45_client_browse` (`mod/src/ubw/restart.cpp`) treats every
local in-session Browse without `FromLevelQuit` as a session drop and arms the reconnect, so the player is pulled back
into the session about 5 s after choosing to leave.

Rows (`logs/c374a57-client.log`): line 3405 `option 0: "FromGame"`; 3406 `session dropped (Browse to local map
"/Game/Maps/3D_Frontend/3D_Frontend"): reconnect to "127.0.0.1:7778" once it loads`; 4010 `try 1 at +3016 ms`; 4458
`reconnected (1): try 1, 5710 ms after the drop`.

## Fix

`c45_client_browse`: a local in-session Browse with `FromGame` returns without arming the follow and logs
`card373: client t=<ms> left the session by the death screen Exit (FromGame): no reconnect`, the same treatment as the
pause menu QUIT (`FromLevelQuit`).

Sibling check: the only other `FromGame` on a client is the host-driven return to the frontend
(`card350: client ClientTravel ".../3D_Frontend?FromGame" (seamless 1)` in c373 runs). That is a seamless ClientTravel,
not a Browse, so it does not reach this branch. Drops by the host closing carry `closed` / `failed`, not `FromGame`.

Rejoin in play is the lobby join (`c52_join`, `lobby_ui.cpp`). The test driver has no client rejoin step; the scenario
needs one that goes through `c52_join`.

## Predictions for the rerun (b58)

1. Client: the card373 row once, 0 `session dropped` rows and 0 `try` rows after the Exit; client stays on the frontend
   for at least 15 s.
2. Host: not stranded (same rows as `c374a57`: respawn in place, slot 0 Dead -> Up), 0 card366/355/344 rows.
3. Rejoin through `c52_join`: card052 `join armed` row, client spawns beside the host (card307), join replay and
   card320 confirm, digest mismatches limited to card 218's spawner.
4. FATAL/ERROR 0 on both logs; no new WARN class.

## Result (b58 `30caa5dfea45b55b`, run `c374b58`)

All four predictions hit. Client: 1 card373 row, 0 `session dropped`, `try` or `reconnected` rows in the whole log,
on the frontend for 18 s, then `card374: client ... travel join: c52_join("127.0.0.1:7778")`, card052 `join armed`,
FED_C1M1 net mode 3. Host: respawn in place, slot 0 Dead -> Up, 0 card344/355/366/371 rows, card307 spawn beside,
join replay, card320 confirm. Digest 1 mismatch (card 218 spawner). FATAL/ERROR 0, WARN class diff against `c374a57`
empty. Diff reviewed by the junior (four attack attempts, none landed). Accepted 2026-11-15.
