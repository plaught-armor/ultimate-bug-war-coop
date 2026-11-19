# 250: 3D frontend after level end: client widget holders keep collision off

Found by card 240 run 7 (c240-c1m1-r7). After the host's level-end Next, the client follows into the host's
3D_Frontend (net mode 3). The host turns actor collision on for its `Widget_Holder_3DFrontend_C` actors at
load (`_1` at (-6673, 6420, 425), `_4` at (7258, 5796, -881)); the client's copies stay off. Client log: 40
lines `card132 mismatch Widget_Holder_3DFrontend_C ... collision host N client N; no row sent`, 20 digest
rounds with `mismatches 2`. Not a play blocker: after the playtest.

Step 1, static: what the client shows in the host's frontend while the host reads the mission brief (brief,
menu, or an input-dead world), who enables the holders on the host (caller exe+0x3802287) and why that path
does not run on the client. Decide by the full-world-sync rule: the client's screen follows the host's
(brief shown, no input), and the digest either covers the holders with a sent row or leaves frontend-local
UI actors out by class with the reason logged.

Files: decided in step 1. Budget: 1 run (the tail of c240-c1m1).

## Step 1 answers (run 7 logs plus Blueprint bytecode; senior, 2026-11-13)

- Holder collision is not world state of its own. `Widget_Holder_3DFrontend` turns its actor collision on with
  `bWidgetEnabled`, that is when its 3D menu view is the current one (caller exe+0x3802287 is
  `AActor::execSetActorEnableCollision`, called from the holder's ubergraph). The mismatch says the two players
  are on different frontend screens: host on holder `_4`, client on holder `_1` (the one the host itself enables at
  game start, the main menu).
- Why they differ: `BP_HUD_3DFrontend` `Get Or Spawn Stack Manager` picks the start view from
  `GetGameMode(self).OptionsString` (`Get Setup from Options`: `FromLevelComplete`, `FromLevelQuit`,
  `FromShowCredits`, `ChapterID`, `LevelID`, `ToBonus`). The host's Browse carries `FromLevelComplete`,
  `ChapterID=0`, `LevelID=0`, `ToBonus=0` (host 165535849). A client has no game mode, so the string is empty and
  the start state is 0, the main menu. The client's own Browse before the reconnect has one option (`closed`), the
  reconnect has none; neither matters once it is a client.
- So the client stands in the main menu, input alive, while the host reads the level-complete screen. The path
  does run on the client; it runs with other inputs.

## Fix spec (after the playtest; one design point open)

- Class fix: the client's frontend start state equals the host's. Host sends its frontend option string once per
  frontend load (existing travel or lobby message, no new channel); the client answers the HUD's option read with
  it. Where to hook is the open point: `Get Setup from Options` is Blueprint, so either the native
  `UGameplayStatics::HasOption` and `GetIntOption` calls made from `BP_HUD_3DFrontend` get the host's string on a
  client, or the mod drives the view stack to the host's state after the HUD built it. Settle by reading
  `GenerateAndInitialiseViewStack` before coding; no run needed for that.
- Later navigation by the host (next mission, lobby) reaches the client through the lobby panel already; whether
  every 3D view change is mirrored is part of the pre-game menu work (card 268), not this card.
- Digest: no class exclusion. With equal start states the holders match; a remaining mismatch is a real one.
- Prediction (tail of a FED_C1M1 run): client row `card137: client change Widget_Holder_3DFrontend_C ... at
  (7258, 5796, -881) actor collision 0 -> 1`, no change row for the holder at (-6673, 6420, 425), 0
  `card132 mismatch Widget_Holder_3DFrontend_C` rows. Budget: 1 run.

## Closed (senior, 2026-11-16)

Card 350 (`menu_c350.cpp`) is this card's class fix: the client keeps the options of the host's seamless
`ClientTravel` URL to the frontend and answers the HUD's empty-string `HasOption` / `ParseOption` with them, so
the client's frontend start state equals the host's.

Prediction met in `c423b163a` (level end on FED_C2M1, both players travel to `3D_Frontend`), no run spent:

- client 520701933: `card350: client ... ClientTravel ".../3D_Frontend?FromLevelComplete?ChapterID=1?LevelID=0?
  ToBonus=0" (seamless 1)`; 520703145 `ParseOption("", ChapterID) from the host's travel options: "1"`.
- client 520703146: `card137: client change Widget_Holder_3DFrontend_C ... at (7258, 5796, -881) actor collision
  0 -> 1`; no change row for the holder at (-6673, 6420, 425) after the travel. Host: the same holder at
  520703025.
- 0 `card132 mismatch Widget_Holder_3DFrontend_C` rows; digest round 11 in the frontend: `actors 683 host, 683
  client, mismatches 0`, `slots differing 0`.

Later 3D view changes by the host stay with card 268 (pre-game menu), as written above.

Reopens on: a `card132 mismatch Widget_Holder_3DFrontend_C` row.

