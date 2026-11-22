# Card 381: a client's own level transition leaves it on the loading screen

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: fix in b67 `cc1d4251720f389d`; static, not proven by a run (hangar computer widgets are outside `ui click`; no test hook added). Watch in the playtest. Found by static audit 2026-11-15 (card 376 sibling).

## Defect (predicted, not yet seen in a run)

A client that followed the host into the hangar has the game's full frontend menu. Every menu route to a level
(NEW GAME, mission select, CONTINUE, restart) ends in `USTGameInstance::TransitionToLevel` (exe+0x5109340; reached by
tail calls from `TransitionToNewGame`, `TransitionToNextLevel`, `RestartCurrentLevel`) or
`TransitionToLatestLevel` (exe+0x51092A0). Both call `ShowAsyncLoadingScreen` (exe+0x5103F80) and then `OpenLevel`.
Card 154 refuses that Browse on a client in the host's session, and nothing ends the loading screen: the same state
card 376 saw for the pause menu's RESTART MISSION (runs `c376b61`, `c376b62`: "LOADING..." stayed, no input). The
third `ShowAsyncLoadingScreen` caller, `USTSaveSystemLibrary::Load`, is already skipped on a client (card 121).

## Fix (`mod/src/ubw/restart.cpp`)

`detour_c381_transition` and `detour_c381_transition_latest`: with `g_client_session` set the call is not run, one
`WARN card381: client ... own <function> not run` row, and the by-value FString argument is freed (the callee owns
it). Host and solo calls pass through. `g_transition_to_level` is now the hook's original.
Card 376's pause-menu filter stays (not widened or removed here).

## Predictions

1. Client in the hangar (host hosting, client joined) starts a mission from its own menu: one `WARN card381` row, no
   `WARN card154: client own Browse` row, no loading screen in the client shot, client still connected (0 reconnects).
2. Unknown, to observe: what the client's menu shows after the dropped call (Blueprint may have hidden the menu).
   If the client cannot use its menu afterwards, report it; do not chase.
3. Host then picks a mission: seamless travel carries the client as before (card346 rows).
4. Sibling: `c376` phase C (client pause-menu RESTART MISSION) still passes.

## Notes after b67

- Also covers the client's death-screen RESTART MISSION and any other Blueprint route to `RestartCurrentLevel`
  (tail call into `TransitionToLevel`), not only the hangar.
- Runs `c379b67`, `c377b67`: 0 `card381` rows (no client transition in those routes). `c376` on b67 is the sibling
  check (prediction 4).
- Run `c376b67`: prediction 4 holds (0 `WARN card381` in phase C; host RESTART MISSION passes through the detour and travels).
- Attack tried (static): a client that lost its session and then starts a solo game. `g_client_session` clears at the next `LoadMap` with no pending net game (`client_diag_load_map_enter`), which the drop to the menu always runs, so the solo start is not skipped. A lost session with no map load (the stale-flag watch item) was already stuck on the card 154 refusal before this card; not made worse.

## Closed (2026-11-17 11:47)

Closed under the backlog rule (2026-11-17). Fix in every build since b67; the sibling run c376b67 passes (host RESTART MISSION travels through the detour, 0 `WARN card381`); the hangar menu has no test hook. Reopen on a `WARN card381` row, or a client left on the loading screen after it used a menu route to a level.
