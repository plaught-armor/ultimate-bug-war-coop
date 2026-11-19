# Card 110: Show which troopers are players

Status: written 2026-11-06 (senior); sent and accepted 2026-11-06.

Read:

- card 028 Report (the remote player's sprite body);
- card 070 Report (`ASTLevelObjectiveMarker`, `CreateASTLevelObjectiveMarker` 0x05150E20, `ShowOnMinimap`,
  world markers such as RANGE and COURSE).

Repo root: `/path/to/ultimate-bug-war-coop`. DLL base: `c11271d7bda28b18` (card 109). The marker shows through walls, as the objective world markers do (senior default; the user may change it after the playtest).

## Why

User (2026-11-06): "We also need to have a better indicator of who is a player or not." The other player's sprite
body looks like any AI trooper, so in a fight neither player can tell the friend from the squad.

## Do

1. **Measure (static first, then one log run).** On each machine, the other player's pawn needs a marker that only
   that machine shows (local, not replicated, not an objective). Candidates, in order:
   - the game's own world marker: an `ASTLevelObjectiveMarker` (or the widget it drives) attached to the other
     player's pawn, with the player's name as its label, shown on screen and on the minimap. It must not add an
     objective, change the objective list or count, or reach card 049's journal;
   - otherwise a `UTextRenderComponent` (or the game's existing billboard text, if any) attached above the sprite
     body's head, with the player's name, always facing the camera.
   Name what the marker class needs (label field, icon, owner actor, how it follows a moving actor) and pick one.
2. **Build.** Each machine adds the marker for every other player pawn when it is classified (card 019 / 028), and
   removes it when the pawn dies, leaves or the map changes. Label: the player's name (the PlayerState name), and
   "DOWN" plus the seconds left while that player is Downed (card 043 state; the client has it from `ChanOp::Down`).
   One fixed slot per remote player (`kC43Max`).
3. **Proof, one short FED_C1M1 run**, FAST=1: both players stand near AI troopers. A side-by-side shot from each
   machine shows the marker over the other player only, not over AI troopers. The marker follows when the other
   player walks (one `goto`). If the run can down the client cheaply (`UBWCOOP_C79_DOWN=2`), the host's shot shows
   "DOWN". rc 0, ERROR/FATAL 0, no new dump.

Stop when the fix builds and that run shows it; the user will playtest.

## Done when

The Report has the chosen marker and why, the add and remove points, the shots (paths), the run length, rc,
ERROR/FATAL, dumps and the DLL hash. Ends with "Report complete."

## Report

**Marker chosen: a screen-space UMG overlay, not the game's world marker.** `CreateASTLevelObjectiveMarker`
(0x05150E20) spawns `ObjectiveMarkerClass` and stores a `USTLevelObjective` at marker+0x2B0; `ObjectiveUpdated`
(0x051614C0) reads that objective's vtable +0x2B0 and flags +0x68/+0x69 for visibility, and the label comes from the
objective's data. Without an objective the marker has nothing to show, and adding one would touch the objective list
and the card 049 journal, which the card forbids. The text render fallback is depth-tested, so it would not show
through walls without a custom material. The overlay uses the card 031 recipe: one `WB_TutorialPrompt_C` instance
(z 90), its root canvas cleared, one `UTextBlock` per remote slot (`kC43Max` fixed slots). Each frame the slot is
placed at `APlayerController::ProjectWorldLocationToScreen` of the pawn location + 110 uu, divided by
`UWidgetLayoutLibrary::GetViewportScale`; the slot Alignment is set to (0.5, 1.0) by reflection (`LayoutData` at
+0x68) so the text sits centred above the head. Screen space draws over walls by nature. Code:
`mod/src/ubw/player_marker.cpp` (`c110_step`, called from `ubw_post_tick` after `c31_step`); two new symbols in
`tools/symbols.txt`.

**Who gets a marker.** Host: roster slots 1.. whose pawn is a player character. Client: the host's pawn
(`c43_client_host_pawn`, cached as a weak ref and rescanned every 1 s). AI troopers are never candidates. Label: the
PlayerState name (`PlayerNamePrivate` +0x340), plus `DOWN <n>s` while Downed (host: `c43_find(pawn index)`;
client: the card 043 `ChanOp::Down` mirror, `kC43ClientHost`), `DOWN` while Out. Local only: nothing replicated,
nothing in the objective list or journal.

**Add and remove points.** Add: the overlay and slots are built the first frame another player's pawn exists in the
world (`c110_build`); a slot is shown when its pawn resolves. Remove: a slot hides when its pawn is gone (left, slot
empty, weak ref stale), when the player is Dead, or when the head projects behind the camera. On a world change
(`world_id` differs) the old widget is `RemoveFromParent`ed and state reset, so a new map rebuilds from scratch.

**Run c110b** (FED_C1M1, FAST=1, `tools/scenarios/c110-marker.txt`; DEPLOY=1): ended by scenario after 58 s; host and
client rc 0; FATAL/ERROR 0, WARN 0 on both; no new crash dumps; drive rc 0, all 27 lines ok. DLL a9ef553e1cddadbf
(saved as `build/dwmapi-c110-a9ef553e1cddadbf.dll`). The first try (c110a) lost the host at startup: wine aborted with
"unimplemented function advapi32.dll.SystemFunction036" right after the card 052 settings read (LAN address step,
unchanged code), before any card 110 code ran; the relaunch was clean.

Log: both sides "card110: ... overlay WB_TutorialPrompt_C built (3 slots, z 90)", Alignment write "was (0.00, 0.00),
now (0.50, 1.00)", "slot 1 label 'Player' (pawn BP_PlayerCharacterMI_C)"; after `host down client` the host label
steps "Player\nDOWN 30s", 29s, 28s... (test runs give both players the default name "Player").

Shots:
- `logs/c110b-near-side.png` (host left, client right): both near the NonCombat trooper line. Each shows "Player"
  over the other player only; the AI troopers (right shot) have no marker.
- `logs/c110b-host-walk.png`: after the client's `goto`, the host's marker sits over the client at the gate.
- `logs/c110b-down-side.png`: host shot shows "Player / DOWN 28s" over the downed client behind the gate wall
  (pawn not visible, label through the wall); the client shot is the client's own Downed view.

Note: at 640x360 the text is small (default font at viewport scale 0.22); at play resolutions it scales up.

Report complete.
