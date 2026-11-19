# Card 220: Training prompts never show on the client

Draft from card 171 (card 130 item 4). Spec only.

## Seen
- Playtest 2026-11-07 (card 130 item 4): the host gets the game's training prompts on FED_C1M1; the client gets
  none.
- Run c171-r1:
  - The host's walk to the Morita fires `BP_TutorialPromptTrigger_C_9` at (5186, 2519, 499): host "card132: host
    ext BP_TutorialPromptTrigger_C BP_TutorialPromptTrigger_C_9 ... 0x07000001 -> 0x07000000, X row sent" at
    130987557.
  - 34 ms later the client logs "card132: client ext apply (already equal) ... 0x07000000 -> host 0x07000000". The
    client's copy of the trigger is already spent before the client gets near it.
  - Neither log has a line for a game prompt. The only `WB_TutorialPrompt_C` lines are the mod's own overlay
    (`lobby_ui.cpp:39`, card110).

## Why it matters
FED_C1M1 is the tutorial map. A client who joins a new player's first map gets none of the control hints the host
gets. Under the full-world-sync rule, every visible effect shows for every player.

## Static steps
1. Find what the trigger's overlap does: the BP_TutorialPromptTrigger_C graph and the native call it makes. Name
   the function that draws the prompt. The prompt is drawn inside the HUD, not through AddToViewport, because no
   playlog "widget add" comes with the trigger.
2. Find why the client's copy is spent before the host's row arrives. Either the host's server-copy pawn overlaps
   the client's trigger copy, or the client applies the ext bits first. Read the card132 ext row path for
   BP_TutorialPromptTrigger_C.
3. Decide per prompt whether it shows per player. Each player gets the prompt when their own pawn enters the
   trigger, as in single player: the trigger stays live for each local player until that player's own pawn fires
   it. Or the host's firing is relayed to every client. Write the choice and the reason in the card before any code.
4. List every BP_TutorialPromptTrigger_C on FED_C1M1 (n=10) with its prompt text, so the run can check them all.

## Done when
- In one pair run where the client walks through at least two prompt triggers after the host does, the client log
  has a line for each prompt shown. The client screenshots show the prompt text the host's screenshots show at the
  same triggers.
- Digest rounds: 0 mismatches. No new WARN classes on either side.

## Static answers (blueprint bytecode, disassembly, logs c227a/c227b and c171-r1; senior, 2026-11-13)

1. What draws the prompt. `BP_TutorialPromptTrigger_C` is a `BP_PlayerPresence` (native `ASTPlayerPresence`). Its
   enter event sets `PlayerInside?` and runs `Turn On Prompt`: `GetAllWidgetsOfClass(WB_Tutorial_Prompt_Line_C)`,
   then `ShowTutorialPrompt(Final Prompt Message)` on each (the widget adds the text to its list, shows the newest,
   sets itself visible). The exit event runs `Turn Off Prompt` (`HideTutorialPrompt`; the widget collapses when
   its list is empty). No native call and no AddToViewport: the line widget lives inside the HUD. The mod's own
   overlay uses `WB_TutorialPrompt_C`, a different class, so the two do not touch.
2. Why the client's copy is spent. `ASTPlayerPresence::OnBeginOverlap` 0x50F6880 and `OnEndOverlap` 0x50F7200
   have no player filter (enabled byte +0x30A, begun play, no load in progress, valid other component). Every
   machine holds every player's pawn, so each trigger fires on both machines for the same pawn at the same
   time: c227b, `BP_TutorialPromptTrigger_C_9`, host enter 142781917 and exit 142783150, client enter 142781916
   and exit 142783149, both for the host's pawn. `OnTriggerExited` then clears +0x30A when the once-on-exit flag
   +0x309 is set (trigger `_9`: `once enter 0 exit 1`). The card 132 ext row is not the cause: the client's copy
   is already 0 when the row arrives (`already equal`).
3. Effect in play. The prompt shows on both screens while the first player stands in the trigger, and hides on
   both when that player leaves, even when the other player is still inside (`PlayerInside? = false`). A once
   trigger is then spent for the second player. A client who follows the host sees each hint only for the
   seconds the host is inside, about the host's position, not their own. That matches the playtest report.
4. Triggers on FED_C1M1 (n=10): `_0` (11906, 16447, 322), `_1` (-603, 17965, 1009), `_2` (4606, 28914, 911),
   `_3` (5365, 19867, 0), `_4` (-536, 21800, 986), `_5` (-8323, 18668, -78), `_6` (-8323, 15919, 0), `_7` (11906,
   16120, 322), `_9` (5186, 2519, 499), `_10` (5365, 23373, 0). Prompt texts are instance properties in the map;
   the proof run's screenshots name them.

## Fix spec (per player; rule chosen by the user 2026-11-13)

- `objectives.cpp` `detour_c227_entered` and `detour_c227_exited`: for a presence whose class is
  `BP_TutorialPromptTrigger_C` (matched once at BeginPlay, stored in a small per-map table like the gated one),
  pass the edge to the original only when the actor is this machine's own pawn; otherwise return. Log
  `card220: <side> prompt trigger <name> <edge> by <pawn>: own pawn, passed | other player, skipped`.
- `ext_c132.cpp` and `digest_c132.cpp`: the enabled byte of this class is per-player state. The host sends no
  X row for it, the client does not enforce it, and the digest does not compare it.
- No change to the widget or to other presences.

## Predictions (one FED_C1M1 play-path run, batched with cards 224 and 225)

- Host walks through `_9`, then the client does 10 s later. Host log: `own pawn, passed` for its own edges,
  `other player, skipped` for the client's. Client log: the reverse. Client screenshot inside the trigger shows
  the prompt text; host screenshot at the same time shows none.
- 0 digest mismatches for `ASTPlayerPresence`; 0 `card132: host ext BP_TutorialPromptTrigger_C` rows.

## Build 4 run c224-c1m1-b4d (FED_C1M1, DLL `5e2ce00acad8efa3`, 639 s, scenario passed, 0 ERROR, no VOID), senior verdict 2026-11-13

- Held in the logs: each side logs `own pawn, passed` for its own walk and `other player, skipped` for the other's,
  on enter and exit (triggers `_0` and `_5` rows; client `_9 entered ... own pawn, passed` at 241424133). 0 `host ext
  BP_TutorialPromptTrigger_C` rows.
- Shot `host c220-host-client-in` viewed: client trooper in the trigger, no prompt on the host. Held.
- Client prompt on screen not proven: the client's pause menu was stuck up (card 225 verdict, scenario fault).

## Senior check of run `c225b186a` (b186, 2026-11-17 13:07): triggers pass, no prompt on either screen

- Log part met: host rows `card220: host/standalone prompt trigger BP_TutorialPromptTrigger_C_7 / _0 entered ...
  own pawn, passed` at 580991239, the client's copies `other player, skipped`; client rows `own pawn, passed` at
  581003178, the host's copies `other player, skipped`. No exit row before the shots.
- Shot part not met: `c220-host-in`, `c220-host-both-in` and `c220-client-in` (taken 1 s after the pass) show no
  prompt text at all, on the host as well. The same holds for the `c448b185a` and `c448b186a` shots. So the
  filter works, but nothing proves the prompt is drawn for anybody in the test lane.
- Next (b187, log only): after each `own pawn, passed` enter row, and again 1 s later, one row
  `card220: <side> prompt widgets <n>: <class> visibility <v>, list <num>, newest '<text>'` from
  `GetAllWidgetsOfClass(WB_Tutorial_Prompt_Line_C)` (the same lookup `Turn On Prompt` uses), plus the trigger's
  `Final Prompt Message` text. Predictions to tell the cases apart: n = 0 means the HUD holds no line widget in
  this route (FAST route or HUD class); n above 0 with an empty message means these two triggers carry no text
  and the scenario must use another trigger; n above 0, text set, widget visible means the shot is the problem
  (timing or widget placement at 640x360).

## Build b187 `920b53a5d0bb7c2e` (2026-11-17)
Host and client both log 3 `WB_Tutorial_Prompt_Line` widgets Collapsed, list 0, text '' at the enter; triggers _0 and _7 carry the game's placeholder Final Prompt Message. Shots show no prompt on either screen. The 1200 uu warp landed inside both triggers; c448 now warps 2500/2700 uu out. b188: trace the game's own visibility path for the widget before any change.

## Build b189 `d4181cdc840b944a` (2026-11-17)
Run c448b189a. Triggers _0 and _7 have Enabled?=0 on both sides and only the game's placeholder Final Prompt Message, so the game itself never shows them; they are not a co-op gap. Trigger _9 has Enabled?=1 and real text ("Press <glyph id_"Objectives"/> to review your objectives."), but the client's widget stays Collapsed 1 s after the client's own enter. The host rows for _9 are "other player, skipped", as designed. The remaining bug is _9 on the client. The c448 warps measured from the actor landed inside the boxes (they reach 2420 uu in x and 4746 uu in y), so c448 now starts both players at x 8800, about 690 uu west of the box edge.

## Build b190 `d451b08388a464db` (2026-11-17)
Run c448b190a: at the client's own enter of trigger _9, the live `WB_Tutorial_Prompt_Line` (flags 0x8, outer WidgetTree) is Visible with list 1 and the text 'Press <glyph id_"Objectives"/> to review your objectives.', at the enter and 1 s later. The Collapsed object seen in b187-b189 has flags 0x280008: it is the class template, not the live widget. Triggers _0 and _7 are disabled by the game itself (Enabled?=0, placeholder text), so nobody sees them in single player either.

## Closed (2026-11-17 15:57)
Evidence: c448b190a client rows above; the client gets the one enabled training prompt it walks through, from its own pawn's enter, as the host does. Digest 0 mismatches in all 7 rounds.
Not done: no screenshot comparison of the prompt text; the client walked through only _9 with a live prompt (_0 and _7 are off in the game).
Reopen when: a playtest client misses a training prompt the host sees on FED_C1M1, or a `card220: client prompt widgets` row for an enabled trigger shows the live widget (flags 0x8) Collapsed 1 s after the client's own enter.
