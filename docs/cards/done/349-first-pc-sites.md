# 349 Host code that means "my own PC" asks for the world's first PC

`UWorld::GetFirstPlayerController` (and the mod's `g_world_first_pc`) is used at 52 sites. After a seamless travel
the first PC of the world is, until the client acks the swap, the client's old PC (b32 guard rows: the game's own
`ProcessVolumes` hit it 13 times). Only card110 was moved to `own_pc` in b33.

Fix (not written): each site that means the local player's PC uses `own_pc`; a site that truly wants any PC says so
in a comment. No behaviour change on a hard load. Report the count by kind (moved, kept with reason).

Prediction: `c346-persistent` and `c345-level-end` pass with no new WARN kinds; grep shows 0 unexplained sites.

## Result (b34 `a4a709a1b9ed4c46`, 2026-11-14): accepted

52 sites: 35 moved to `own_pc`, 1 more in autoinput (the InputKey PC, field renamed `world_own_pc`), 16 kept with a
comment ("any PC in the world will do (net mode only)" or "(the world context object only)"). The three runs of
card 348 passed. Two WARN kinds new in `c346-persistent`, neither from this card:

- Client `WARN cmd: 3 ui continue -> failed`: the scenario's client continue ran at 336483623, 525 ms before the
  client's GameStarted (336484148) asked for the pause. Scenario fixed: it waits for the client's
  `card135: client GameStarted ... on FED_C2M1` row, and the step is no longer a `try`.
- Client `WARN card114: cannot create ...UBWCoop-Difficulty.defaults.ini (err 32)`: host and client share one
  prefix in the test lane and wrote the file in the same millisecond (336423195). Not possible on two machines.
