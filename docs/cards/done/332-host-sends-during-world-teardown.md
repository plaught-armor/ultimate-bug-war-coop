# 332 Host mirrors level-script calls made during world teardown, and receives them itself

b19 run 1 (`c319-played-c1m1`, `d781c1e804ad64d6`), senior trace 2026-11-14. Not a play blocker: seen only at
quit, no handler ran. Rides batch 3 with the card 326 registry.

## What happened (`logs/c319-played-c1m1-host.log`)

- t=303974225 `cmd: 35 quit: RequestEngineExit`. From t=303974287 the level script (`ctx FED_C1M1_Scripting_C`,
  caller `exe+0x5083584`) calls `SetObjectiveVisibility` for Assault_SO1, SO2, SO0, TacSupport_SO0 and
  ShootingRange_SO0, twice (t=303974287 and 303974291): the world's teardown.
- The host mirrors each: `card049: host sent SetObjectiveVisibility(...) to 1 pc(s); journal 60` to `journal 65`.
- Each frame comes back to the host's own receive detour in the same ms: `WARN card132: seq gap 14-1509 from
  BP_PlayerController_C (got 1510)`, then 10 rows `Objective ... result no handler`. The client's connection is
  cleaned up 1 ms later (`card128: ... connection ... closed: reason 25 Cleanup`, t=303974288).

## Cause

- Settled: `c49_send` (`objectives.cpp:303`) sends to every PC that `c49_synced` and `c26_is_remote_pc` accept. The
  remote test is the roles only (`sprint_side`, `hooks_ubw.cpp:360`); nothing asks whether the world is being torn
  down or the connection is closing.
- Settled: `detour_client_message` (`chan.cpp:684`) passes any prefixed frame to `receive` whatever PC it arrives
  on. On the host `side_ok` is false, so no handler runs, but `on_recv` still moves that PC's receive sequence with
  a host-to-client number: the gap row.
- Not settled: why the engine ran `ClientMessage` locally on the server copy (expected: the connection was already
  closing, so the call space was local). The fix does not depend on it.

## Fix (class: host sends during teardown)

1. One teardown state, set where `c310_map_end` is called (`ubw_load_map_enter`, `hooks_ubw.cpp:267`) and at
   engine exit, cleared at the map arm. `client_send_allowed` (`chan.cpp:357`) refuses while it is set, ledger
   reason `world teardown`. Card 310's `g_c310_leaving` reads the same state instead of keeping its own.
2. `c49_record` does not journal a call made in that state (the journal went from 60 to 69 on teardown calls).
3. `detour_client_message` on a PC that is not this side's own (`sprint_side` not `kSideClientOwn`): drop the frame,
   count it, one WARN per map; the ledger's receive sequence is not touched.

## Sibling sweep (report before coding)

Every hook that can fire from EndPlay or object destruction and then sends or records: effect deactivate rows
(card 136), widget removes, ability and tag writes (card 310, done), gib and decal pool releases, objective actor
lists (card 070). For each: does it send in the teardown state today, and does the client apply it to the world it
is about to leave or to the next one. The map travel and the checkpoint Reload are the cases that matter in play;
quit only shows the pattern.

## Proof (no run of its own; any batch 3 run that quits or travels)

Prediction: after the `quit` row and after each `ubw_load_map_enter`, 0 `seq gap` rows, 0 `result no handler`
rows, and one row `card332: host teardown: <n> frame(s) not sent, by op ...`. The client's log carries no
`SetObjectiveVisibility` replay stamped after the host's teardown start.
