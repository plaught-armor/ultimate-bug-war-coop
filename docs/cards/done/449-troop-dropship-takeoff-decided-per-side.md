# Card 449: troop dropship takeoff is decided per side, and ignores the client's trooper

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: fixed in b151 `7d8087dbc3cee8ab`; run c448b151b shows both client rows and 0 `WARN card159`. Host `takeoff held` row not reached yet. Closes on the junior's shot report.
on the b148 tree). Needs `python3 tools/pdb_rva.py gen` after applying (new symbol).

## Fault

`ASTProceduralDropshipTroops::TickBehaviours` (exe+0x521E0D0) runs on host and client. Landed with the doors shut it
asks `CanTakeoff` (exe+0x51F5680): false while `GetSTPlayerCharacter` (player 0) stands on the ship (bounds test plus
a line trace that hits the ship), else true once no trooper is left in the bay (+0x73C == 0). True runs
`StartAction(startup 1, nav, target 2, spline null)`.

1. The client's copy decides its own takeoff. Run c423b145a: the rejoined client's `BP_ProceduralDropshipTroops_C_10`
   took off about 6.4 s before the host's `card342 ... launch edge`; client `WARN card159 ... check: state 0/1 ...
   2205 uu off ... startup 1/0 target 2/0`.
2. On the host, player 0 is the host's trooper only: a client's trooper standing on the ship does not hold it
   (SYNC_AUDIT row `TickBehaviours -> CanTakeoff`).

## Fix

Hook `CanTakeoff` (always on).

- Host: the original, and when it says true, once more inside each server copy's card 102 acting scope
  (`GetSTPlayerCharacter` goes through the card 099 `GetPlayerController` gate). Any false holds the takeoff.
- Client: a copy that has a host row (`ds_row`) returns false; a copy with none keeps its own check.
- Client snapshot (`ds_client_snapshot`, kinds J and L): a landed copy whose host frame shows the takeoff (in flight,
  startup 1, target 2) first runs the game's `StartAction(1, host nav, 2, null)`, then the usual apply. The
  `StartAction` hook row loses its diag gate so the original exists in a play build.

## Predictions (one run, any FED map with a troop drop, client near the landed ship)

- Client: `card449: client ... landed copy leaves its takeoff to the host`, then `card449: client ... host took off,
  StartAction run on the landed copy (state 1 -> 0 ...)` per troop ship.
- 0 client `WARN card159 ... check: state 0/1` rows with `startup 1/0`.
- Host: `card449: host ... a client's trooper stands on it: takeoff held` only while the client stands on a landed
  troop ship; the ship leaves after the client steps off.
- Client screenshot after the host's launch edge: the ship climbing, not parked and not snapped away.
- card159 map-end join check totals all match, 0 EXCEPTION, digest 0 mismatches.

## Risk to check in review of the run

`StartAction` on the client copy with `nav` from the host frame: if the copy has no nav data the game falls to the
spline path; the row prints the state after the call, and a state other than 0 is a failure of this fix.
