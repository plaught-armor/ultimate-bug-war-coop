# 355 A client that leaves during the title-screen hold sends the host to the main menu

User, 2026-11-14, asked whether a client disconnecting during the card 320 hold should also send the host to the
menu: "Host to main menu".

## Cause (code read)

`c320_logout` (`mod/src/ubw/intro_c135.cpp`) dropped the leaving client from the waiting set and `c320_try_release`
released the hold with "disconnect": the host started the mission alone. Only the 60 s bound (card 344) returned
the host.

## Fix (senior, unbuilt, rides b40)

- `c320_logout`: a client of this hold (IntroStart sent, confirmed or not) that leaves while the hold is on sets
  `left`; no release.
- `c320_step`: `left` runs the card 344 return (`c344_no_confirm(pc0, now, true)`, `c350_host_return_to_menu`) on
  the next engine tick, before or after the host's continue, not from inside the engine's Logout. With no client
  connected the travel is the hard Browse (`c346_host_seamless` needs 2 players and consumes the card 350 flag).
- A failed return releases the hold with a WARN, as in card 344.
- Not changed: the panel line is card 344's ("The other player did not finish loading: mission not started").
  A client that leaves after the hold is released: the host plays on.

Row: `WARN card355: host t=<ms> a waited-for client left during the hold (host continued <0|1>): host returns to
the main menu`.

## Prediction (scenario from `c344-no-confirm`: the client process quits during the hold instead of stalling; 1 run)

- Host: `card320: ... left during the hold`, one `WARN card355` row, `card350: host ... return to the menu`, a hard
  Browse to `3D_Frontend` with `FromLevelQuit`, 0 `hold off ... (disconnect)` rows, `hold off ... (map end)`, the
  hangar without the FTUE screens (shot).
- 0 new WARN kinds besides card355.

## Result (b40 `81b66827ad3499bb`, run `c355-b40`, 2026-11-14): accepted

- Host: `client BP_PlayerController_C left during the hold (waited for), clients 0` at 341464157, `WARN card355 ...
  (host continued 1): host returns to the main menu` 1 ms later, card350 OpenLevel with `FromLevelQuit?ChapterID=1?
  LevelID=0`, a hard Browse with 1 player, `hold off after 4218 ms (map end)`, world `3D_Frontend`. 0 `hold off ...
  (disconnect)` rows.
- Shot `c355-host-menu` viewed by the senior: the hangar mission menu, no FTUE screen, the panel line shown.
- Not exercised: a leave before the host's continue (`host continued 0`), a client that had confirmed.
- Senior edit after b40 (unbuilt): the panel line now reads "The other player left or did not finish loading:
  mission not started".

## b47 prediction: a leave before the host's continue (host continued 0)

Path: hangar, the host starts the next mission (seamless travel), the hold waits for the client; the client quits
right after `hold waits for client slot N (join), clients 1`, with no host continue. From `c320_logout` and `c320_step`
(left checked before host_done, then `c344_no_confirm(pc0, now, true)`), the host rows, in order:
- `card320: host t=N client BP_PlayerController_C left during the hold (waited for), clients 0`
- `WARN card355: host t=N a waited-for client left during the hold (host continued 0): host returns to the main menu`
- `card350: host t=N return to the menu: OpenLevel("/Game/Maps/3D_Frontend/3D_Frontend", absolute, "FromLevelQuit?ChapterID=N?LevelID=N"), as the pause menu QUIT`
- `card154: host t=N Browse to "/Game/Maps/3D_Frontend/3D_Frontend" with 1 players (FromLevelQuit 1): ...` (no client left: a hard Browse)
- `card320: host t=N hold off after N ms (map end)`
- `card052: t=N world 3D_Frontend (package /Game/Maps/3D_Frontend/3D_Frontend, net mode N): panel on, ...`
- 0 rows `hold off ... (disconnect)`, 0 rows `WARN card344: ... no return to the menu`. The host does not start the map alone.
Risk: UBWCOOP_AUTODROPSHIP=1 does an unpause 3 s after the PlayerController count row; if it lands before the quit,
the row reads `host continued 1` (the b40 case) and the 0 case stays unproven.

## b47 results (b47r2, faef8a0da0c13495): host continued 0 proven

Hangar, the host starts FED_C3M1, the client quits 0.9 s after the hold row, before autodropship acts. Host rows:
```
card320: host t=348463364 hold waits for client slot 1 (join), clients 1
card320: host t=348464256 client BP_PlayerController_C left during the hold (waited for), clients 0
WARN card355: host t=348464257 a waited-for client left during the hold (host continued 0): host returns to the main menu
card350: host t=348464257 return to the menu: OpenLevel("/Game/Maps/3D_Frontend/3D_Frontend", absolute, "FromLevelQuit?ChapterID=2?LevelID=0"), as the pause menu QUIT
card154: host t=348464272 Browse to "/Game/Maps/3D_Frontend/3D_Frontend" with 1 players (FromLevelQuit 1): listen added to the Browse URL (in place, engine allocator)
card320: host t=348464285 hold off after 3223 ms (map end)
card052: t=348465088 world 3D_Frontend (package /Game/Maps/3D_Frontend/3D_Frontend, net mode 2): panel on, status 6
```
0 `hold off ... (disconnect)` rows, 0 `WARN card344`. The host did not start the map alone. Shot
`host-b47r2-c355-host-menu-c0.png` (viewed): the hangar, "Human Missions", panel line "The other player left or did
not finish loading: mission not started" (the senior's unbuilt edit, now built in b47). No FTUE screen.
