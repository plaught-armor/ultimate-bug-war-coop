# 345 The client's own level-end continue leaves the host's session

User, 2026-11-14: "The lobby also needs to be persistent because the client disconnects from host after a match".

## Cause (logged)

Playtest client log `logs/playtest-menu-20261113-183002.log`, t=255867798: the client pressed Next on its own
`WB_TrooperLevelEnd`. The game opened `/Game/Maps/3D_Frontend/3D_Frontend` with `FromLevelComplete` on the client.
`c154_client_refuses` (`mod/src/ubw/restart.cpp`) lets every Browse to the menu map through, so the client left the
session (`card045: client ... session dropped`), landed in its own menu, and the card 045 follow then joined the host
again while the host was still on its level-end screen: `reconnected (2)` into FED_C1M1, a full load of the finished
mission.

The harness never showed it: in `c310-proof-c1m1` the host presses Next first, so the client's drop is the host's
travel.

## Fix (uncompiled, senior)

- `c345_client_stays` (`restart.cpp`): a client in the host's session whose own local Browse goes to the menu map
  with `FromLevelComplete` does not run it. The Browse detour returns Success (0), not Failure: `UEngine::
  TickWorldTravel` acts only on Failure (travel failure broadcast, which disconnects a client).
- `browse_enter` (`mod/src/ue/net.h`, `net.cpp`) now returns an int: `kBrowseRun` (-1) runs the original, any other
  value is returned as the Browse result. Card 154's refusal still returns Failure (1).
- The client leaves the map with the host: the host's Next travels, the client gets the card 290 travel row, drops,
  and the card 045 follow takes it into the host's hangar.
- Test command `travel end` (host): `USTGameInstance::EndLevel` through the mod's detour (`cmdfile.cpp`,
  `testrun.cpp` `c345_end_level`).

Row: `card345: client t=<ms> own level-end Browse to "<map>" not run: the client stays in the host's session until
the host travels`.

Not settled by the trace: what the client's screen shows after its Next (the widget may close, or a loading screen
may stay up). The shot `c345-client-held` decides. A blank or stuck screen is a follow-up fix in this card (a "waiting
for the host" line on the co-op panel), not a reason to let the Browse run.

## Not in this card

Every host travel is still a hard drop plus follow (card 154, option A). The follow gives up 60 s after the client's
menu loads (`kC45CapMs`); a client left there must join by hand. Both stay as they are until a log shows them fail.

## Prediction (scenario `tools/scenarios/c345-level-end.txt`, run name `c345-level-end`, 300 s)

- Client: one `card345: client ... not run` row after its Next; 0 `card045: client ... session dropped` rows between
  that row and the host's `FromLevelComplete` Browse; a `Clock` row received after it.
- Host: 0 `card140: host Logout` rows between its `card260: host EndLevel, sent to 1` and its own Browse.
- After the host's Next: client `card290 ... failure ... within 10000 ms`, `reconnected`, world `3D_Frontend` net
  mode 3. Client reconnect count for the run: 1.
- 0 new WARN kinds on either side.

## Result (b30 `9e23007ab565017e`, run `c345-level-end`, 2026-11-14): accepted for the session

- Client: 0 `session dropped` rows before the host's Browse; `Clock` row received after the card345 rows; reconnect
  count 1 (4694 ms).
- Junior's fix to my diff: the not-run path left `FWorldContext::TravelURL` set and leaked the FURL copy, so
  `TickWorldTravel` would call Browse again every tick. `browse_not_run_cleanup` (`mod/src/ue/net.cpp`) empties
  TravelURL and destroys the FURL.
- Findings: 3 card345 rows, because each test cutscene skip calls Browse again; new `WARN card130: client
  WB_Cutscenes_C ... still the top widget after 3 calls`; shot `c345-client-held` is black (the ended level-end
  cutscene widget stays on top).

## 345b (assigned, b31)

The client's own Next is not run while it is in the host's session: the level-end panel stays, with a "Waiting for
the host" line. The host's Next is relayed so both play the level-end cutscene; the client's cutscene-end Browse
stays not run. Prediction: 0 card345 rows before the host's Next, 1 per cutscene end call after it, 0 of that
`WARN card130`, the held shot shows the panel with the waiting line. With card 346 the client then leaves the map by
the host's seamless travel, not by a drop and follow.

## 345b result (b33 `100476d0085b4f43`, `c345-level-end` passed, 2026-11-14): accepted

Host `card345b: host level-end Next: relayed to 1`; the client ran its own Next handler 11 ms later; both played the
cutscene. Shot `c345-client-held` viewed: the level-end panel with "Waiting for the host to continue" in the top
left corner (small at 640x360). The client leaves the map by the host's seamless travel (card 346). Finding from
this run: card 348.
