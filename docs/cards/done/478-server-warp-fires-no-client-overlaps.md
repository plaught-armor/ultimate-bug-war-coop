# 478 — A server-side move of a client's pawn fires no overlap events on the client's own pawn

Found 2026-11-17 12:49, run c448b185a (FED_C1M1, DLL `41c369126e01b57f`), by the junior while proving card 220.

The scenario's `host warp client` (line 68) placed the client's pawn inside `BP_TutorialPromptTrigger_C_9`. The
host's copy logged "entered ... other player, skipped" at 579960243. The client's own pawn logged no "entered" row
and the client shot `c220-client-in` shows no prompt. When the client walked through the same trigger, the client
logged "entered/exited by <own pawn>: own pawn, passed".

Why it may matter outside tests: client-local triggers (training prompts, card 220; any per-player presence) are
missed when the mod or the game moves a client's pawn from the server. Mod sites that place a pawn with
`g_set_actor_location(..., kTeleportPhysics)`: death.cpp:2091 and 2183, remote_body.cpp:989, enemies.cpp:284.

Trace owed (static, before any fix):
1. Which path moves the client's own pawn after `host warp client` (the engine's position correction, or the mod's
   remote_body.cpp:989 own-pawn placement), and whether that path updates overlaps.
2. Which play-time moves use the same path (respawn, revive placement, rejoin placement, collapse catch-up).

Done when: after a server-side placement of a client's pawn inside a client-local trigger, the client logs the
trigger's "entered" row within 1 s, in one pair run; or the trace shows no play-time move uses the path, and the
card closes as test-tool only.

## Closed (2026-11-17 12:50): test tool only

Trace (senior, static + logs c448b185a):
- The test `host warp client` (`c75_warp`, testrun.cpp:1780) calls SetActorLocation on the server copy only. The
  client's own pawn gets there by the engine's position correction. Log: host rows for trigger _9 at 579960243
  (entered) and 579973110 (exited), no client row for either edge; the walked pass at 579943686 has both sides.
- Every play-time move of a client's pawn runs on the client itself: `c46_warp_apply` (objectives.cpp:2682, Warp
  and WarpTo: kill floor return, catch-up, landing) calls `c46_move_to` on the client's own pawn, a local
  SetActorLocation, which updates overlaps there. The other `g_set_actor_location` sites (death.cpp:2091 and 2183,
  remote_body.cpp:989, enemies.cpp:284) move the host's pawn, a remote copy or a camera mover, not a client's own
  pawn from the server.
- So no play path moves a client's own pawn by server correction alone. Host-side triggers (objective presences)
  fire on the server copy and are not affected.

Rule for scenarios (also in STATE): a client-local trigger (training prompt) is tested with `client goto`, never
with `host warp client`.

Reopens on: a play-time log where a client's pawn is moved by the host with no `card046: client ... Warp` row and a
client-local trigger is missed.

## Correction (2026-11-17 12:53, junior's read of c448b185a, checked by senior)

The title overstates it. A server warp of the client's pawn does fire client overlaps in the same run: line 76
(`host warp client point 12600 14100 400`) gave client rows `_0` and `_7` "entered ... own pawn, passed" at
579973119, 9 ms after the host copy's rows. Only the line 68 warp (5186 2519 499) missed trigger `_9` on the client
(host row at 579960243, no client row; the client had left `_9` at 579952203). So the miss is specific to that warp
(candidates: client bounds of `_9`, the drop from z 499, or two `_9` actors in different sublevels), not a property
of every server warp. The closure stands: play-time moves of a client's own pawn run on the client, and the scenario
rule (`client goto` for client-local triggers) stays. Same reopen condition.
