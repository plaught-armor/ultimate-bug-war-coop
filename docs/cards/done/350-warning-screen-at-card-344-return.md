# 350 Photosensitivity WARNING screen on both sides at the card 344 return to the menu

Seen in the b33 `c344-no-confirm` shots: after the host's card 344 return (no client load confirm in 60 s), host and
client both show the game's start-up photosensitivity WARNING screen in `3D_Frontend`.

## Step 1 (one host-only run, no client)

In single player, leave a mission through the pause menu's quit to the main menu and take a shot in `3D_Frontend`.

- The WARNING screen shows there too: game behaviour, close the card with the shot named, no fix.
- It does not show: the card 344 return reaches the menu by another path than the game's own quit. Trace statically
  what the pause-menu quit sets before its travel (URL options, GameInstance fields) that `c344` does not, make the
  card 344 return use the same path, and prove it with `c344-no-confirm` (both shots without the WARNING screen).

Budget: 1 host-only run, then at most 2 `c344-no-confirm` runs.

## Result (b35 `eb8125c2c3dc3ec6`, b36 `312bc9f472523c84`, 2026-11-14): accepted

- Step 1 (`c350-solo-quit2`, host only): the game's own pause-menu quit shows no WARNING screen. So it was ours.
- Cause: `BP_HUD_3DFrontend` BeginPlay reads the GameMode's options; with neither `FromLevelQuit` nor
  `FromLevelComplete` it runs the first-start widgets, `WB_FTUE_PhotoSensitivity` first. Host: card 344 used
  `USTGameInstance::ReturnToMainMenu`, which opens the menu with no options. Client: no GameMode, so its options were
  always empty; it got the WARNING screen after every mission end as well.
- Fix (`mod/src/ubw/menu_c350.cpp`): the host's card 344 return calls `UGameplayStatics::OpenLevel` with the pause
  menu's URL and options, and card 346 takes that one travel seamless. The client stores the options of the host's
  ClientTravel to the frontend and answers the HUD's empty-string `HasOption` / `ParseOption` for the four setup keys;
  the store is cleared at the next ClientTravel and when the client session ends (b36).
- Proof: `c350-c344` (b35) passed, both menu shots show the hangar with "Human Missions". `c350-c345` (b36) passed,
  client rows `HasOption FromLevelComplete 1`, ChapterID "0", LevelID "0"; both hangar shots show the OPERATION PAYBACK
  panel, client shot viewed by the senior. 0 new WARN kinds.
- Not proven by a run: the session-end clear (code read only). Budget: step 1 took 2 runs (run 1 void, a co-op-only
  wait row in a solo scenario).

## b47 prediction: the session-end clear, then a fresh join

Path: C2M1 level end by EndLevel, Next, seamless travel to the hangar; then `client disconnect` (a local Browse),
card 045 rejoins. From `detour_c350_client_travel`, `c350_client_session_end` (sprint.cpp, LoadMap with pending null)
and the card 045 rows, on the client, in order:
- `card350: client t=N ClientTravel "/Game/Maps/3D_Frontend/3D_Frontend?FromLevelComplete?ChapterID=N?LevelID=N?ToBonus=0" (seamless 1): frontend options "FromLevelComplete?..."`
- `card045: client t=N session dropped (Browse to local map "...")`
- `card350: client t=N session ended: frontend options "FromLevelComplete?..." cleared`
- `card045: client t=N reconnected (1)`, then `card052: ... world 3D_Frontend (..., net mode 3)`
- After the session-end row: 0 rows `card350: client t=N HasOption(...) from the host's travel options` and 0
  `ParseOption(...) from the host's travel options` rows until a new ClientTravel stores options again.

## b47 results (b47r2, faef8a0da0c13495): the session-end clear proven

Client rows, in order (log line numbers in logs/b47r2-client.log):
```
8154  card350: client t=348440681 ClientTravel "/Game/Maps/3D_Frontend/3D_Frontend?FromLevelComplete?ChapterID=1?LevelID=0?ToBonus=0" (seamless 1): frontend options "FromLevelComplete?ChapterID=1?LevelID=0?ToBonus=0"
8804  card350: client t=348441841 HasOption("", FromLevelComplete) from the host's travel options: 1
9264  card045: client t=348445974 session dropped (Browse to local map "/Game/Maps/3D_Frontend/3D_Frontend"): reconnect to "127.0.0.1:7778" once it loads
9441  card350: client t=348445975 session ended: frontend options "FromLevelComplete?ChapterID=1?LevelID=0?ToBonus=0" cleared
10018 card045: client t=348450460 reconnected (1): try 1, 4486 ms after the drop
10026 card052: t=348450477 world 3D_Frontend (package /Game/Maps/3D_Frontend/3D_Frontend, net mode 3): panel off, status 0
10255 card350: client t=348456990 ClientTravel "/Game/Maps/FED/FED_C3M1" (seamless 1): frontend options ""
```
After line 9441: 0 rows `... from the host's travel options` (HasOption or ParseOption). The fresh join reads no stale
options. Shot `client-b47r2-c350-rejoin-hangar.png` (viewed): the rejoined client shows the hangar Mission Select
(Operation Razorwire), no FTUE or WARNING screen.
