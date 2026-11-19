# 468 — After a checkpoint load and rejoin the client HUD differs from the host's in two cosmetic ways

Found 2026-11-16 in the `c460b170a` shots `host-/client-c460b170a-c460-after-load.png` (senior viewed both).

1. Objective list: same rows in the same order on both, but the host's two open rows ("Retrieve the weapon cache from
   Tango Outpost", "Lock down Sierra Outpost. 0/3") carry the bright marker and white text of a row just added; the
   client's are grey. Before the load (`c418-c-plant` shots) neither side has markers. Likely the "new objective"
   highlight the list plays when a row is added; the client's rows come from the join replay.
2. The client shows the mission location card top right ("Operation Razorwire", day, place, character name); the
   host does not. The rejoining client's HUD starts as on a fresh map start; the host's load does not replay the card.

Priority: low, cosmetic, no state difference (digest round 5: 0 mismatches). Trace first, static: which widget call
sets the row highlight and whether it is timed (then item 1 is only the later join and needs nothing); where the
location card is started and whether the game's own load skips it (then the client skips it on a rejoin into a map
past its start).

Done when: each item is either fixed with the same shot pair showing no difference, or closed with the traced reason.

## Closed (2026-11-17 13:01): both items traced to the widgets' own timing, no fix

Static trace from the cooked widgets (name maps of `WB_Objective_List`, `WB_Objective_List_Item_Fed`, `WB_Intro_Text`,
extracted read-only with `tools/iostore/iox.py`).

1. Row highlight. `WB_Objective_List` has a timed fade of its own: `TimeBeforeFadeOut`, `FadeOutTimer`,
   `FadeOutDuration`, `Begin Fade Out`, the `Fade` animation, gated by the user setting
   `USTGameUserSettings::GetObjectiveFadeEnabled`. `OnLevelObjectiveAdded`, `OnLevelObjectiveComplete`,
   `OnLevelObjectiveCalledOut` and `ObjectiveUpdated` show the list again and restart the timer; the row's
   `GlowBorder` follows `UpdateMostRecentlyUpdated`. A list built at widget construct from objectives that already
   exist (`FirstInit`, `NonAddedObjectivesUpdate`) gets no such event. So bright against grey is each screen's own
   fade timer: the host's rows were added by the load a moment before the shot, the client's list was built from the
   join replay's existing objectives. Rows, order and counts are the same (digest 0 mismatches), and the next
   objective event lights both lists.
2. Location card. `WB_Intro_Text` starts itself: `OnInitialized` binds `OnPossessedPawnChanged`, and the pawn change
   (`PlayerExitDropship` / `PlayerLeftDropship`, cast to `BP_Player_Transport_Dropship`) calls `StartWriting`, holds
   `HoldTime`, then `RemoveFromParent`. A rejoining client possesses a new pawn on a new HUD, so the card plays once
   and removes itself; the host's HUD in a checkpoint load showed none in the shot (host side not traced further).
   A joiner seeing the mission title once is not a state difference.

Reopen: an objective row that differs in text, order or count between screens, or a location card that stays on
the client.
