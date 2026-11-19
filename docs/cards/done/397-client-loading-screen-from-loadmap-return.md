# Card 397: client shows the loading screen from its LoadMap return

Follows card 396. User report: "the client just sits on a black screen" while the host loads.

## What is left after card 396

Card 396 removed the queue wait for `IntroStart` and the flicker. The client is still black from its LoadMap
return (card 337 cover on) until its own `GameStarted` call, which needs the host's screen up, the client's HUD,
its pawn and the card 156 restore. Run c395b82, FED_C3M2: LoadMap return 430163886, HUD 430166177, pawn and first
Pong 430167965. That is about 4 s of black that no op ordering can remove, because the game's own screen is pushed
on the HUD's stack.

## Static trace

- `WB_Loading_Screen_Final_C` shows "LOADING..." with the loading icon until its press prompt comes up
  (`c135_prompt_step` reads `WB_Loading_Icon_White` and `PressToContinue`). It reads its title and image in
  "Apply Data" from the game instance's chapter and level IDs (card 135 trace). It does not need the HUD to exist:
  the HUD only pushes it on a `WB_Stack_C`.
- The mod already creates widgets without the game's graphs: `g_create_widget_instance(pc, &cls, 0)`,
  `g_add_to_viewport`, `g_widget_set_visibility`, `g_widget_remove_from_parent` (`lobby_ui.cpp`,
  `player_marker.cpp`). The class path is in `testrun.cpp` (`kC67Classes[0]`).
- UMG draws while world rendering is off (the title screen does today under the card 337 cover).
- Every host travel with clients goes through one place: `c346_host_seamless` (`restart.cpp`), called from
  `c45_browse_enter`. The frontend has set the host's chapter and level IDs before that Browse.
- The client's cover starts in one place: `c337_load_map_return`; it ends in one place: `c337_uncover`.
- Not known statically: side effects of the widget's Construct outside a stack (pause request, input mode), and
  whether its Tick brings the press prompt up on its own. The run answers both (predictions 4 and 5).

## Change

1. Host, `c346_host_seamless`, before `ServerTravel`: read `GetChapterID` and `GetLevelID` and send a new op
   `TravelIds` ("<map basename> <chapter> <level>") to every client with `chan_send_client_now`. Log the values.
2. Client: keep the op in its own small state (not `g_c135`, which LoadMap enter clears).
3. Client, `c337_load_map_return`, when the cover goes on and the kept op names this map: set the IDs on the game
   instance (existing `c135_set_ids` path), create one `WB_Loading_Screen_Final_C`, add it to the viewport above
   the HUD stack, set it HitTestInvisible. No stack, no focus, no input. If no PC exists yet, retry per tick in
   `c337_step` while the cover is on.
4. Client, `c337_uncover` (every reason) and `c337_load_map_enter`: remove the copy. With card 396's 500 ms paint
   delay the game's own screen is already drawn under it.
5. A client that joins a map directly (no travel op before its LoadMap) gets no copy and behaves as in card 396.
   This is the rare path: with the persistent lobby (card 345) clients travel with the host.

One manager for the copy: `render_c337.cpp` owns create and remove. `intro_c135.cpp` is not changed except to
expose the ID setter.

## Predictions (written before any run)

1. Host log: one `card397: host ... TravelIds <map> <chapter> <level>` line per travel with a client, and the IDs
   equal the later `card135: host ... screen up` IDs on that map.
2. Client log: `card397: client ... loading screen copy up` within 100 ms of `card337: ... world rendering off at
   LoadMap return` on every map reached by host travel.
3. Client shot 200 ms after the cover goes on shows the loading screen with the right level name, not black.
4. No new pause or input-mode line (`card018`, `card096`) between the copy going up and the client's own
   `GameStarted`; the world-paused value in the `client GameStarted` line is unchanged from b83.
5. Client shots through the handover (own screen up, +250 ms, after the uncover) show no black frame, no map frame
   and no doubled text; the press prompt appears only on the game's own screen.
6. WARN classes: no new class against `logs/b72-warn-classes.txt`; soak scenario `c388-c3m1-to-c5m2` passes with 0
   mismatches.

Budget: 3 runs.

## Result (b84 `3419c5f330a48d9c`, run c397b84, 1 of 3 runs) — accepted

- Prediction 1 holds: 10 `TravelIds` rows (5 maps, 5 hangar travels), each to 1 of 1 remote PC; IDs equal the later
  screen-up IDs (client `chapter 2->2 level 1->1` and so on at GameStarted).
- Prediction 2 holds: copy up +1 ms after the cover on FED_C3M2, C4M1, C4M2, C5M1, C5M2.
- Prediction 3 holds: `client-c397b84-c397-c-copy.png` shows "OPERATION OBSIDIAN", the level image and "LOADING...".
- Prediction 4 holds: `card018` input-mode rows 16 and pause requests 14, same as b83; world paused 1 at GameStarted.
- Prediction 5: by log only (copy removed in the same tick as the uncover, 500 ms after the game's screen is up); no
  handover shot taken. The user's playtest is the visual check.
- Prediction 6 holds: scenario passed, host 18 and client 10 WARN rows, no new class.
- Not covered, as the card says: a direct join (first map of the test route, 3986 ms black).
- Prediction 5, later run c397b84r2 (junior, client keeps the screen): holds. Four client shots on FED_C3M2 (copy up,
  after GameStarted, after the removal, 1 s later) all show the loading screen with the art in the same place; no
  black, no map, no doubled text. Copy removed and uncover in the same tick. The handover frame itself is inside a
  1697 ms client frame (join replay burst) and cannot be shot.
