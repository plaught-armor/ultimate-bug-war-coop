# Card 376: a client's refused map Browse drops it from the session

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: fix written by the senior (`restart.cpp`, `c45_browse_enter`), syntax-checked; build b61 and run owed.

## Defect (run c376b60, phase C, b60 `a48aa4475e889bf7`)

Client pause menu, RESTART MISSION, confirm. Client rows:
- 400386181 `card045 ... Browse URL ... map "/Game/Maps/FED/FED_C1M1", 0 options`
- 400386181 `WARN card154: ... own Browse ... refused: in the host's session, the host chooses the map`
- 400386181 `card052: ... travel failure 11 (ClientTravelFailure) in session`
- 400386218 `card045 ... Browse URL ... 3D_Frontend`, option `closed`; `session dropped`
- 400395551 `card045: client ... reconnected (2): try 1, 9333 ms after the drop`

The card 154 refusal returned `kBrowseFailure`. The engine raises ClientTravelFailure for a failed Browse and its
handler browses to the menu with `?closed`, which card 154 passes (a menu map) and `c45_client_browse` takes as a
drop. The client blacks out for about 9 s and comes back as a fresh join with a new pawn.

## Fix

`c45_browse_enter`: a card 154 refusal inside the host's session returns `kBrowseSuccess` (as card 345's stay
does), so the engine raises no failure and makes no fallback Browse. While the client follows the host back (no
session) the failure return stays, as card 154 proved it.

Sibling by the same path: a client's mission click in the hangar (persistent lobby) is the same refused Browse.

## Predictions (rerun of `tools/scenarios/c376-pause-restart.txt` phase C and D on b61)

- Client: the `WARN card154 ... refused` row, then no `card052: ... travel failure`, no `closed` Browse, no
  `session dropped`, no `reconnected` row in the next 15 s. The client's pawn id is the same before and after.
- Client screen 3 s after the confirm (shot): the world with the HUD, or the pause menu still up. Not known which;
  the Blueprint expected a travel. A stuck menu, a paused client or a loading screen that stays is a second defect:
  report the shot, do not fix in the run.
- Host: no Logout, no join replay.
- Phase D (host RESTART MISSION, both alive): `card346` seamless travel, client stays, no reconnect.

## Side finding from `c376b60` phase C (not fixed here; the fix above removes the route)

After the drop the client rejoined as a fresh join (LoadMap return 400395551) but the host never sent the engine's
`ClientRetryClientRestart`: the client log has `ClientRestart(arg null)` at 400397110, `OnRep_Pawn` with the pawn at
400397257 and 400400083, and no `AcknowledgePossession` up to the log end at 400417319. The card 156 map-load restore
runs only in the AcknowledgePossession detour (`input_diag.cpp:132`), so `g_game_only_pending` stayed set, `c135_step`
never called GameStarted, and rendering came back by the fallback (`WARN card337 ... after 15002 ms`). In the two good
joins of the same run the retry arrived 1.5 s and 3.1 s after the pawn (400347144, 400367163).

Unknown: why the host sent no retry (it retries from `NotifyServerReceivedClientData`, so either no ServerMove reached
it or its pawn check failed). Client state rows match the good joins (LocalRole 2, locally controlled 1, pause requesters
0). An unacknowledged pawn also means the host rejects the client's moves, so this would be a frozen client, not only a
late screen. Seen once in all kept logs (`grep -l "WARN card337" logs/*-client.log` gives this run only).

Signature to look for in any later log: client `card135 ... IntroStart ... waiting` with no
`card156 ... back to Up (map load): restore` after the last `LoadMap returned`, then `WARN card337` or
`WARN card135 ... GameStarted not called after 60000 ms ... card 156 restore pending 1`. Card it if it shows on any
other route.

## Part 2: loading screen left up after the refused restart (run `c376b61`, b61 `bd0f098555615ca2`)

Part 1 held: client rows 400747682 RestartLevel click, 400747699 `WARN card154 ... refused: in the host's session`,
`UEngine::Browse not run by the mod: returned 0 (Success)`; no `card052 travel failure`, no drop, no reconnect, pawn
unchanged.

Defect: the pause menu's RestartLevel builds `WB_Loading_Screen_Final_C` (PreConstruct 400747684, outside the
`WB_Stack_C`) before its OpenLevel. No map load follows the refused Browse, so nothing removes it: shots
`c-after3-client`, `c-after15-client` and `fail-line88` show "LOADING..." over the live world.

First fix, b62 `6d664b81169d8df2`, missed (run `c376b62`): the widget was remembered and `RemoveFromParent` called
(`card376 ... loading screen removed`, 401334639), but the screen stayed up (shots `c-after3-client`,
`c-after15-client`), got mouse events 50 ms later, never got Destruct, and no input action fired (goto moved 0 uu in
20 s). The screen ticks on thread T 1452, not the game thread: it belongs to the game's loading screen, not to the
viewport, and the mod has no traced call that ends it. That code is removed.

Fix, b63 (`restart.cpp`, `death.cpp`, `dropship.cpp`, `hooks_internal.h`): the client's RESTART MISSION is not run at
all. While a client's `WB_Stack_C` holds the pause in the host's session (`c376_note_pause` from the pause and unpause
detours sets `g_c376_pe`), `detour_process_event` drops a `WB_Pause_Menu_C` call to the
`BndEvt__WB_Pause_Menu_RESTARTLEVEL_button...` event (no confirm popup opens) and to `RestartLevel` (the confirm's
action), each with a `card376: client t=... pause menu <function> not run` row. The loading screen is never started.
Part 1 (the refused Browse reports Success) stays as the guard behind it. Cost: one bool test per ProcessEvent with no
client menu open; with it open, one class pointer compare, and names only for `WB_Pause_Menu_C` calls.

Predictions for the b63 rerun, phase C: both `card376 ... not run` rows; no `Browse URL`, no
`seen WB_Loading_Screen_Final_C`, no drop within 5 s; shot `c-popup-client` shows the pause menu with no popup; after
RESUME the client walks (goto arrives). Phase D: host RESTART MISSION travels seamless (`card346`), the client follows
and stays in the session.

Open, not in this card: the client's RESTART MISSION button still shows and does nothing. Hiding or greying it is a
follow-up if the playtest asks for it.
