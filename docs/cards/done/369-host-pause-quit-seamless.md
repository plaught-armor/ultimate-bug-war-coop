# 369 The host's pause menu QUIT drops the client (hard travel left by card 346)

Found by the senior's static trace, 2026-11-15 (audit of paths not proven by a run since card 346). User rule
"persistent lobby" (2026-11-14): the session persists across matches, the client is never left disconnected.

## Trace (b54 source)

- `c346_host_seamless` (`restart.cpp`) refused a Browse with `FromLevelQuit` unless it came from the card 344 return
  (`c350_take_host_return`). The host's own pause menu QUIT (`WB_Pause_Menu` `ConfirmQuitToMainMenu`, OpenLevel to
  `/Game/Maps/3D_Frontend/3D_Frontend?FromLevelQuit?ChapterID=..?LevelID=..`) therefore ran the hard path:
  card 290 travel row "quit", `listen` added (`c45_host_browse`), the client is dropped and comes back by the
  card 045 follow (first try 3 s after its own menu loads, 60 s cap).
- On that path the client gets no ClientTravel, so the card 350 option store stays empty: its frontend HUD has
  neither `FromLevelQuit` nor `FromLevelComplete` and runs the first-start widgets (photosensitivity WARNING screen),
  once in its own menu and again after the reconnect in the host's hangar. Not seen in a run; inferred from card 350.
- The same URL already travels seamless from the card 344 return (card 350, runs `c350-c344`, `c344-no-confirm`).
  Card 096 lifts the pause while a client is connected, so the pause menu does not hold the travel.

## Fix (senior, for lane 2 build b55)

`restart.cpp` `c346_host_seamless`: the `FromLevelQuit` exclusion is gone; with a client connected the host's QUIT is a
seamless ServerTravel like every other host travel. `c350_take_host_return` and `C350State::host_return` had no other
use and are removed (`menu_c350.cpp`, `hooks_internal.h`). Hard paths left: no client connected, URL valid 0,
ServerTravel false, host process exit, the in-place load (card 294/143, not this card).

## Predictions (scenario `tools/scenarios/c369-host-quit.txt`, FED_C1M1, one run)

1. Host: `card346: host t=N seamless travel to "/Game/Maps/3D_Frontend/3D_Frontend?FromLevelQuit?ChapterID=N?LevelID=N"
   started by ServerTravel, Browse not run (2 players, ...)`; no `card290` row with "(quit)"; no `card154: host ...
   listen added` row for that travel.
2. Client: `card350: client t=N ClientTravel "...?FromLevelQuit..." (seamless 1)`, then `HasOption(.., FromLevelQuit)
   from the host's travel options: 1`; 0 NetworkFailure rows, 0 `session dropped`, 0 `reconnected`.
3. Host: 0 `card140: host Logout` between the join and the client's quit.
4. Shots: both in the hangar with the menu, no WARNING screen (`c369-host-hangar`, `c369-client-hangar`); the client
   shot is viewed.
5. The mission start from the hangar is a second seamless travel (`card346 ... FED_C1M1`), both reach own pawns,
   Clock rows arrive on the client.
6. FATAL/ERROR 0 on both; WARN by class against b54, new kinds named.

Budget: 3 runs. A refused ServerTravel (`WARN card346 ... refused`) or a client drop is a failed prediction: stop and
report, no widening.

## Run c369b55 (2026-11-15, DLL `29ad572d08da2d85`, lane 2, one run of the budget)

Build b55 from the current tree (restart.cpp, menu_c350.cpp, hooks_internal.h), `build/dwmapi-b55-29ad572d08da2d85.dll`;
EAT 0x54, KERNEL32.dll only. Scenario passed in 143 s, all steps ok, host rc 0, client rc 0, no crash dumps. Logs:
`logs/c369b55-host.log`, `logs/c369b55-client.log`, `logs/c369b55.drive`.

1. PASS. `card346: host t=393964154 seamless travel to "/Game/Maps/3D_Frontend/3D_Frontend?FromLevelQuit?ChapterID=0?LevelID=0"
   started by ServerTravel, Browse not run (2 players, bUseSeamlessTravel was 0)`. card290 "(quit)" rows: 0. card154
   "listen added" rows: 0.
2. PASS. Client: `card350: client t=393964169 ClientTravel "/Game/Maps/3D_Frontend/3D_Frontend?FromLevelQuit?ChapterID=0?LevelID=0"
   (seamless 1)`, then `card350: client t=393964767 HasOption("", FromLevelQuit) from the host's travel options: 1`.
   NetworkFailure: only the two hook-install rows, 0 failures. "session dropped" 0, "reconnected" 0, card290 rows 0.
3. PASS. Host `card140: host Logout` rows: 3. The first, at 393924786, is the solo 3D_Frontend PC (net mode 0) at the
   first travel, before the client joins (`card113: session players 1 -> 2` at 393933940). The other two, at 394045607
   and 394047818, follow the client's `quit` (394045558) and the host's quit.
4. PASS. Viewed `client-c369b55-c369-client-hangar.png` and `host-c369b55-c369-host-hangar.png` (prefix2 Screenshots).
   Both show the hangar with "Human Missions / x1 New Mission Available". No WARNING screen.
5. PASS. `host travel restart`: `card346: host t=393973327 seamless travel to "/Game/Maps/FED/FED_C1M1" started by
   ServerTravel`, client `card350: client t=393973340 ClientTravel "/Game/Maps/FED/FED_C1M1" (seamless 1)`. Both
   reached their own pawns; client Clock recv row at 394041302 (`card132: recv 414 ... Clock`). The Enter-loop
   fallback was not needed.
6. PASS. FATAL/ERROR 0 on both. Host WARN 4: attach err=126; config UBWCOOP_SKIP_FLIGHT needs AUTODROPSHIP; card346
   guard GetViewTarget on a null camera manager twice (3D_Frontend 1 hit, FED_C1M1 13 hits; the same class is in the
   b54 host log twice). Client WARN 2: attach err=126; card156 "back to Up (map load) +5 s" at 393969859 on the
   3D_Frontend: menu cursor shown, as expected in the hangar. This class is not in the b54 logs, but it is in
   `logs/c346-persistent-client.log` (the earlier seamless travel to the frontend). No new class from b55.
