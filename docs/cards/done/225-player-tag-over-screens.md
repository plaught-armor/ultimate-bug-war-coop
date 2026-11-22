# Card 225: The other player's name tag draws over full-screen widgets and cutscenes

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: queued 2026-11-12, from card 135 (c135-r2 client screen shot, c135-r3 client brief video shot).

The card 110 player marker (`mod/src/ubw/player_marker.cpp`, `kC110ZOrder` 90, lines 29 and 168) hides only when
the pawn is gone, dead or behind the camera. It shows over the press-to-start screen, the brief video and,
by the same reasoning, over any WB_Stack_C menu.

## Do

One rule for the class: the tag is hidden while the local player's screen stack is non-empty or a cutscene or
movie widget is up (name the existing checks to reuse: `c67_ui_top`, `c135_screen_up`, the cine skip's cutscene
state). State the Z order of WB_Stack_C and WB_Cutscenes_C from code or one logged line; lowering the tag's Z
order below them is the alternative, decide which from that.

## Done when

- Shots on the client with the press-to-start screen, the pause menu and a cutscene up show no tag; the tag is back
  in game. Proof rides in the card 224 run.

## Step 1 answers and fix spec (senior, static, 2026-11-13)

- One existing signal covers the class. The press-to-start screen, the brief video and the in-mission movie
  cutscenes are all widgets on a `WB_Stack_C`, and the stack calls `RequestGamePause` when it gets one.
  `cine_note_pause` (`cine_skip.cpp:157`, always hooked, both sides, play build too) keeps a weak reference to
  that stack in `g_cine.stack_weak`; `c67_ui_top(stack, &num)` gives the number of widgets on it.
- Rule: the tag is hidden while that stack resolves and holds one widget or more. No Z-order change (the game's
  Z orders are not logged anywhere; the rule does not need them).
- Code: `cine_skip.cpp` exports `bool cine_stack_open()` (weak get plus `c67_ui_top`, no config test, declared in
  `hooks_internal.h`). `player_marker.cpp`: in the per-slot step, before the projection, `c110_hide(s, "screen
  up")` when it is true. Nothing else changes; the tag returns through the existing show path.
- Not covered by this signal: level-sequence cinematics (no stack widget). Junior checks the `seq_step` state in
  `cine_skip.cpp` for a "sequence playing" flag that also holds in the play build; if there is one, the same
  hide uses it; if not, report and leave it out.
- Prediction (rides in the card 224 run): client rows `card110: client slot 1 hidden (screen up)` at the
  press-to-start screen and at the pause menu, a show row after each closes; shots without a tag.

## Build 4 run c224-c1m1-b4d (FED_C1M1, DLL `5e2ce00acad8efa3`, 639 s, scenario passed, 0 ERROR, no VOID), senior verdict 2026-11-13

- Shot `client c225-client-paused` viewed: pause menu over the dimmed game, host trooper beside, no name tag. Held
  for the pause menu.
- No log proof: the `card110` rows hit the diagnostic cap at 241300838 (`further events not logged`), before the
  pause step, so no `hidden (screen up)` row exists. Tag back in game: shot `client c240-client-end` shows the
  "Player" tag over the host trooper.
- Press-to-start screen and cutscene cases not exercised (see card 224 verdict; the client never had the screen).
- Scenario fault: the second `client key Escape` did not close the menu (card 222: the driver's `key` does not
  reach Slate focus; it fired `IA_Pause` again). The menu stayed up on a black background until the rejoin, so
  shots `c225-client-unpaused`, `c220-client-host-in` and `c220-client-in` show the menu. Use the card 222 close
  path in the scenario.

## Closed (2026-11-17 13:07)

Run `c225b186a` (FED_C1M1, b186 `6bdc1fb92f415071`, scenario passed, 0 EXCEPTION or FATAL in both logs). Senior
viewed the shots: `c225-client-press-start` (the press-to-start movie) and `c225-client-brief-video` (the brief
video) show no name tag; `c220-client-in` shows the tag over the host's trooper in game. The pause menu case was
proven in run `c224-c1m1-b4d`.

Limit of this proof, stated plainly: in this run the client's overlay is built at tick 580979550, after both
screens closed, so no `hidden (screen up)` row was written for them; the shots prove the screens are clean, the
hide rule itself was exercised only by the pause menu run. The rule is one signal for the whole class (a widget
on the `WB_Stack_C`).

Reopen when: a playtest shot or report shows a name tag drawn over a press-to-start screen, a movie cutscene, the
pause menu or any other full-screen widget. Level-sequence cinematics are not covered by the stack signal; a tag
seen over one reopens this card with that case.
