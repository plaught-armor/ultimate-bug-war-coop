# Card 171: Which 2026-11-07 playtest items still happen

Status: queued 2026-11-11. Budget: static first, then one run. Output is findings, not fixes.

Read: docs/cards/done/130-playtest-settings-lag-tacsupport-popups-audio.md items 1, 2, 4, 11, 13, 14 only.

## Why
Card 130 was closed partial with these items open. Items 3, 5, 6, 7 and 9 were covered later by cards 136, 140, 160
and 161. Nobody checked the rest against the current build.

## Do
For each item (1 settings menu slow with a client in; 2 TacSupport popup targets missing on the client; 4 training
prompts missing on the client; 11 no revive prompt; 13 host sees no objective markers on FED_C2M1; 14 AI troopers
and medics revive downed players): find a later card that fixed it (grep docs/CARDLOG.md), or show in one run that
it still happens, or that it does not. No code change.

## Done when
- A table: item, verdict (fixed by card N / still happens / gone), the log line or screenshot that shows it.
- One new card file drafted per item that still happens (spec only). Ends "Report complete."

## Senior prep (2026-11-12, static, from logs/c206-r1 and the source; no run)

| Item | Verdict so far | Evidence |
|---|---|---|
| 11 revive prompt | built inside card 130 (`interact.cpp`, `hooks_internal.h:1251`); host side shown working | c206-r1 host: "card130: host revive interactable STLimitedInteractionActor spawned (interface +0x2A8), prompt \"Revive\"", then "revive prompt shown for BP_PlayerCharacterMI_C" and "hidden" 3.3 s later |
| 14 AI pick-up | built inside card 130 (`death.cpp:854`); shown working | c206-r1 host: "card130: host t=117240083 AI pick-up: BP_AI_Trooper_SquadLeader_C ... goes to downed player BP_PlayerCharacterMI_C, 2273 uu" (a non-medic); `c142-r4.txt:75` relies on it |
| 2 TacSupport popup targets | likely fixed by the card 132 timeline rows and card 185 | c206-r1 client: 39 "world L BP_KillableBuilding_Base_TacSuppRangeTarget_C_N\|Timeline" rows; CARDLOG 185: 3 TacSuppRangeTarget timelines at 1.00 of 1.00, digest 0 mismatches |

Left for the one run:
- Item 11, client side: no "card130: client ... revive prompt" line exists in c206-r1 or c142-r4, because the client
  was the downed player in both. The run downs the host near a standing client.
- Item 2: a client screenshot of the raised TacSupport targets next to the host's (receiver-side visual proof).
- Item 1: the settings menu open and change times, solo against a client in.
- Item 4: training prompts on the client. No later card names them; expect "still happens".
- Item 13: host objective markers on FED_C2M1. The host log has card 049 and 070 lines, but nothing shows what the
  host's HUD draws; needs a host screenshot.
Items 1 and 13 need FED_C2M1 or a menu step, items 2, 4 and 11 FED_C1M1: check whether one route covers both before
writing the scenario.

Item 4, static (2026-11-12): the source has no relay for the game's training prompts. `WB_TutorialPrompt_C` appears
only as the mod's own overlay class (`lobby_ui.cpp:39`, `player_marker.cpp`), so the `WB_TutorialPrompt_C` counts in
the c206-r1 logs (host 13, client 19) are the mod's overlays, not game prompts. Expect "still happens"; the run
needs a line or screenshot of a game prompt on the host with none on the client, and the trace of what shows it
goes in the new card's spec.

## Run c171-r1: route and predictions (2026-11-12, written before the run)

Scenario: `tools/scenarios/c171-r1.txt`. Run:
`systemd-run --user --scope -q -p MemoryMax=12G env SCENARIO=tools/scenarios/c171-r1.txt DEPLOY=1 FAST=1 GOD=0
CLIENT_DELAY_MS=120000 HOST_EXTRA_ENV="UBWCOOP_AUTODROPSHIP=1 UBWCOOP_C132_FX=1" CLIENT_EXTRA_ENV="UBWCOOP_C132_FX=1"
tools/run_pair.sh c171-r1 900`. DLL 447d59fbfd989e83 (deployed hash checked).

### Route
One run cannot reach all five items. Items 2, 4 and 11 are FED_C1M1 content (the TacSupport range, the training
prompt triggers, and a downed host next to the client). Item 13 is FED_C2M1. `UBWCOOP_TRAVEL` picks one start map per
run, and the driver has no map-travel command, so a C1M1 run cannot reach C2M1. Item 13 is "not reached" on this run.
Existing evidence for it is thin: c147-r3 (FED_C2M1) spawned `BP_ObjectiveMarkerActor_C` three times on the host, but
with no client in, so the card 102 acting scope never ran and the card 130 line "host BeginPlay of ... runs outside the
acting scope" could not print. Item 13 needs its own FED_C2M1 pair run with the client in before the first marker
spawns, plus a host screenshot.

Item 1 works on any map, so the route is FED_C1M1. The body after the menu block is the c142-r4 route (lines 23-45),
which raised the range targets in c206-r1. There the targets' first timeline call (op 1, at -532 going to -32) came
35 ms after the client equipped the Morita, together with `SetObjectiveVisibility(TacSupport_SO3)` and
`CalloutObjective(TacSupport_SO3)`.

The client joins late (CLIENT_DELAY_MS=120000), so the first menu block runs solo. `UBWCOOP_C132_FX=1` on both sides
gives the card 132 "frame window 10000 ms" line: frame count, mean and max frame time per 10 s.

### Predictions
- **Item 1, settings menu.**
  - What the run measures, solo and then with the client in:
    - the open delay: the "cmd: N key Escape 150 -> done" stamp to the card067 "WB_Stack_C RequestGamePause" dump and
      the card096 line;
    - the OPTIONS click cost: the "card121: ui click" stamp to the "cmd: N ui click ... -> done" stamp (ProcessEvent
      is synchronous);
    - the frame time: the frame window before the menu against the two windows with the options menu up.
  - Prediction: the open delay is under 500 ms in both cases, and the OPTIONS ProcessEvent takes under 200 ms in both.
    With the client in, the mean frame time while the menu is up stays within 25% of the window before it, and no
    frame is over 250 ms.
  - If that holds, the verdict is "gone (not reproduced by injected input)", with the caveat below.
  - "Still happens" means one of these: a client-in open delay over 1000 ms, an OPTIONS cost over 1000 ms, or a menu
    window max over 250 ms with the solo case clean.
  - Caveat: the user's 1 to 3 s may sit in Slate input handling, which injected InputKey and ProcessEvent skip.
  - Not reachable: the time to change an option, because the options widget's function names are unknown and Slate
    navigation cannot be driven.
- **Item 2, TacSupport targets.**
  - Host: the card098 line "world L BP_KillableBuilding_Base_TacSuppRangeTarget_C_N|Timeline ... sent to 1 of 1".
  - Client: "applied timeline call" for the same row.
  - After the next digest round, the screenshots c171-i2-host-targets(-b) and c171-i2-client-targets(-b) show the
    same raised targets on both screens, with 0 digest mismatches.
  - Verdict "fixed by card 130/132/185 (timeline relay)". If the client screenshot shows the targets still down, the
    verdict is "still happens".
- **Item 4, training prompts.**
  - The host's walk to the Morita fires `BP_TutorialPromptTrigger_C_9` at (5186, 2519, 499) (the card132 "host ext
    ... X row sent" line, as in c206-r1 and c142-r4). The host shot c171-i4-host-trigger shows a game training
    prompt.
  - The client then walks the same path. Its trigger copy has already taken the host's X row (0x07000001 to
    0x07000000), and no relay exists for the prompt. The shot c171-i4-client-trigger shows no prompt.
  - Verdict "still happens", with a new card. Static lead for that card: the host's ext row turns the client's copy of
    the trigger off before the client can reach it, and the prompt itself is drawn inside the HUD, not through
    AddToViewport (no playlog "widget add" line came with the c206-r1 trigger).
- **Item 11, client side.**
  - `host down host` with the client standing 150 uu away. Host: "card043: host t=N BP_PlayerCharacterMI_C (host-own,
    slot 0) -> Downed".
  - Client, within 10 s: "card130: client t=N revive prompt shown for BP_PlayerCharacterMI_C", and the shot
    c171-i11-client-prompt shows the game's interact prompt "Revive".
  - After the client's 6 s hold, or the card 130 AI pick-up, "revive prompt hidden".
  - Verdict "fixed by card 130 (both sides)". With no client line, the verdict is "still happens (client side)".
- **Item 13**: not reached (above).
- **Item 14**: fixed by card 130 (Senior prep; not re-proved).

## Run c171-r1: verdicts (2026-11-12)

Run: `logs/c171-r1-host.log`, `logs/c171-r1-client.log`, `logs/c171-r1.drive`. Both rc 0, no crash dumps, FATAL/ERROR 0.
Screenshots: `prefix/drive_c/users/steamuser/AppData/Local/StarshipTroopers_UBW/Saved/Screenshots/Windows/<role>-c171-r1-c171-*.png`
(below as `<role>-…-<name>`).

| Item | Verdict | Evidence |
|---|---|---|
| 1 settings menu with a client in | gone | The menu costs the same with or without a client. Solo: Escape at 130810053, "card096: host t=130810779 SetGamePaused(1) = 1 (world paused)" (726 ms); OPTIONS ProcessEvent 130810996 to 130811771 (775 ms); window "frame window 10007 ms: 502 frames, mean 19.9 ms, max 787.2 ms". Client in: Escape 130930240, window "mean 18.5 ms, max 796.7 ms, 1 over 50 ms"; OPTIONS ProcessEvent 130945615 to 130946478 (863 ms), window "mean 18.7 ms, max 885.7 ms". Client in is 100 ms (13%) worse at most, under the 1000 ms "still happens" line. The client's frames stay clean while the host's menu is open (client windows max 17 to 90 ms; the 1767 ms one is its map load). The 0.8 s hitch on OPTIONS also happens solo, so it is not a co-op cost. Shots host-…-i1-solo-options, host-…-i1-client-options |
| 2 TacSupport popup targets | fixed by card 132 (the card 098 timeline rows, end state by card 185) | Host "card098: host t=131075805 world L BP_KillableBuilding_Base_TacSuppRangeTarget_C_1\|Timeline 12065 15300 -532 1: sent to 1 of 1 clients", the same for _0 and _3. Client "card098: client t=131075819 ... TacSuppRangeTarget_C_1\|Timeline ... applied timeline call" (14 ms later). Shot client-…-i2-client-targets shows the raised targets. Next digest round 10: 0 mismatches. The host shot shows only the pause menu (below) |
| 4 training prompts on the client | still happens (static; the run's visual evidence is void) | Host "card132: host ext BP_TutorialPromptTrigger_C BP_TutorialPromptTrigger_C_9 at (5186, 2519, 499) 0x07000001 -> 0x07000000, X row sent" (130987557). Client "card132: client ext apply (already equal) ... BP_TutorialPromptTrigger_C_9 ... 0x07000000 -> host 0x07000000" (130987591): the client's copy of the trigger is already spent 34 ms after the host's pawn walks through it. The client never came within 800 uu of the trigger (drive line 70: last 8988 uu). Neither log has a game prompt line; the only `WB_TutorialPrompt_C` lines are the mod's overlay (card110). Source: no relay for the game's prompts (Senior prep). The host shot host-…-i4-host-trigger shows the pause menu, not the prompt. Draft card 220 |
| 11 revive prompt, client side | fixed by card 130 | Host "card043: host t=131100537 BP_PlayerCharacterMI_C (host-own, slot 0) -> Downed". Client "card130: client t=131100556 revive prompt shown for BP_PlayerCharacterMI_C" (19 ms later). Shot client-…-i11-client-prompt shows "REVIVE" over the downed host. The client's E hold: host "use press by BP_PlayerCharacterMI_C: reviving entry 116180 (Downed, 150 uu, needs 3000 ms)", "-> Up (revived from Downed after a 3000 ms hold)" at 131104051; client "revive prompt hidden" at 131104079 |
| 13 host objective markers, FED_C2M1 | not reached | The route is FED_C1M1. `UBWCOOP_TRAVEL` picks one map per run and the driver has no travel command (Route, above) |
| 14 AI pick-up | fixed by card 130 | Not re-proved by a dedicated step. It fired anyway: host "card130: host t=131100695 AI pick-up: BP_AI_Trooper_SquadLeader_C ... goes to downed player BP_PlayerCharacterMI_C, 253 uu", then "AI pick-up of entry 116180 dropped: the target is up or gone" after the client's revive |

### Other findings
- **The driver cannot close the pause menu.** `key Escape` closes neither the options page nor the pause menu
  (cmds 6, 7, 13, 14). The host's pause menu stayed up from 130930240 to the end of the run. As a result:
  - every host shot after line 41 shows the pause menu (the same 34841-byte image);
  - after the client quit, "SetGamePaused(1) = 1 (world paused), 0 client(s)" came back at 131129782.
  The world kept running with the client in (card 096), so the host's walks, warps and the downing still ran.
  Host-side screenshots on a route need a menu-close step that works (driver work, not card 130).
- **Scenario order, lines 85 and 86.** The range targets rise when the host enters the range, not when the client
  equips. In c206-r1 the rows came with the host's warp at 117237910, and here with the host warp at 131075805.
  Both waits ran before the warp, so they failed while the rows were still to come.
- **One digest round mismatched: round 8 at 131039666.** Client "mismatch BP_AmmoPickup_MoritaSmall_C
  BP_AmmoPickup_MoritaSmall_C_0 pickup host 0 client 1" and "mesh visible host 0 client 1".
  - Last row: seq 419 (CollisionOp), sent at 131024211.
  - The host sent two world E rows for _0 in the same ms, "...6424 11495 99 1" and then "... 0", with DisablePickup between them.
  - Round 9 was clean, and so were all rounds from 4 to 11 apart from round 8.
  - It is a one-round transient. No card: it cleared without action and is outside card 130's items.

## Senior review (2026-11-12)

Accepted. Run c171-r1, DLL `447d59fbfd989e83`, no code change. Items 1 gone, 2 fixed (card 132, 185), 11 and 14
fixed (card 130), 4 still happens (card 220), 13 not reached (watch item in card 127, FED_C2M1 host screenshot).
From the run: ammo pickup state mismatch for one digest round (card 221); the driver's Escape does not close the
host's menus (card 222).
