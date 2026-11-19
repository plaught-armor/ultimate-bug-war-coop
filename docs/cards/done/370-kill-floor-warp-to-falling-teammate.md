# Card 370: kill floor warp picks a teammate who is falling too

Status: open, low, after the playtest. Found 2026-11-15 in run `c143b55d` (b55 `29ad572d08da2d85`).

## What happens

`c46_host_below` (`objectives.cpp`) warps a player below the kill height to `c46_up_target`: the first other pawn
that is Up. Up is the only test. When that teammate is in the air below the map as well, the warped pawn lands on a
falling pawn, falls again, and the two warp to each other in turn. Each warp costs 25 health (`c46_hurt`).

Log, `logs/c143b55d-host.log` (both pawns under the world because of a scenario warp point at z -200):
- warp 1 at 394925459 host-own at z -22041 to slot 1: now z -11833.
- warp 2 at 394928642 server-copy at z -22094 to slot 0: now z -19024, Health 30.0 -> 5.0.
- warp 3 at 394929375, warp 4 at 394930676; server copy Downed; all down at 394931376.
- `WARN card046 ... warp +0.5 s ... gap 1918 uu / 614 / 1059 (limit 200)`.

## Why low

It needs both players below the map at once. The end state (both dead, death screen) is what the game does to a
lone player who falls. One player falling with the other on the ground is unaffected.

## Fix

In `c46_up_target`, skip a pawn that is not on the ground (character movement mode Falling, or its z below the
kill height plus a margin). With no grounded Up teammate, send the pawn to the card 146 rescue anchor, as
`c46_all_down_return` does for a copy, instead of the all-down path. One manager for the class: the same target
rule serves the host pawn and the copies.

## Proof (one run)

1. Both pawns dropped below the kill height within one second: each gets one warp to the rescue anchor, 0
   `WARN card046 ... gap` rows, nobody Downed by warp damage alone.
2. One pawn dropped, the other standing: one warp to the teammate, as now.

## Also from this run (no card)

- The card 143 restore with a client on b55 is proven: Reload received, reconnected 2501 ms after the drop, Restore
  sent and applied (Health 100.0 -> 30.0, Morita and Laser 300 restored), digest rounds 2 and 3 at 0 mismatches,
  host stayed on FED_C1M1, 0 card344 / card355 / card346 travel rows.
- Row order inside the load is reconcile, Reload, then "save engine load done" (the first two run inside
  LoadFromSlot). The scenario's waits pass in that order.
- `tools/scenarios/c143-restore.txt` still warps both pawns to (5600, 14400, -200), which is under the world (same
  fall in `c143-r3`, 2026-11-08). Use a grounded point before the next use.

## Findings before the fix (read, not run)

(a) What `c46_all_down_return` needs that is not true while players are Up:
- It is reached only for a server copy with `g_c43.all_down` set (listen server, 2+ players, slot >= 0). While
  anyone is Up, `all_down` is false, so the path never runs; an Up pawn needs its own entry point.
- It always sends WarpTo to `r.p[slot].pc`. That is wrong for the host's own pawn (no remote controller); the
  host pawn needs the move without a frame.
- It applies no `c46_hurt` and adds no follow check, so it logs no gap rows. The Up path keeps the hurt (25% of
  MaxHealth), as for a teammate warp.
- `c46_rescue_spot` falls back to "beside the host's pawn" when the anchor spot fails, in any state. With the host
  itself below the map that fallback is the falling host; the anchor try comes first, so on FED_C1M1 with an
  anchor set it is not reached.

(b) A jump or a short drop next to a ledge: the character movement component reads `MOVE_Falling` (3, offset
0x231, the same field `enemies.cpp` already reads) for the whole jump arc and after a step off a ledge until it
lands. A teammate mid-jump is skipped for that moment. The pawn below the map then goes to another grounded Up
teammate if there is one, else to the rescue anchor (with the hurt). The anchor is a checkpoint spot, so this is a
safe place, only farther than the teammate. `c46_spawn_spot` with no grounded Up target also uses the rescue
spot, and `c46_note_checkpoint` falls back to the host's pawn as the saver.

## Change (b56, `08265a76bc24632c`)

- `ue/actor.h`: `kMoveFalling = 3` (the local copy in `enemies.cpp` removed).
- `objectives.cpp`: `c46_grounded(pawn)` = movement component present, mode not Falling, z at or above the kill
  height. `c46_up_target` requires it, so all its callers (kill floor warp, `c46_spawn_spot`,
  `c46_note_checkpoint`) skip a falling or below-map teammate.
- `c46_return_to_spot(pawn, ..., why)`: the move to the rescue spot, shared by `c46_all_down_return` ("while all
  down", row text unchanged) and the new Up path ("with no grounded teammate Up"). It sends WarpTo only for a
  server copy and says "not needed" for the host pawn.
- `c46_host_below`: with no grounded Up teammate and the pawn Up, if the anchor is set on this world: cooldown
  mark, return to the spot, hurt; no all-down path. With no checkpoint save yet on this map: one
  `WARN card046 ... no checkpoint save yet on this map` row per map (`C46State::no_anchor_warned`), then the old
  path.
- Behaviour change: an Up pawn whose teammates are all Downed now goes to the rescue anchor instead of the team
  wipe. This is the literal reading of "does not go to the all-down path while it is still Up". `c46_hurt` and the
  2 s cooldown are unchanged.
- `tools/scenarios/c143-restore.txt`: warp point moved to the range floor (6554, 11459, 197) / (6654, 11459,
  197), with a comment; not run.
- Proof scenario: `tools/scenarios/c370-kill-floor.txt`.

## Run c370b56 (b56 `08265a76bc24632c`, FED_C1M1, FAST=1, GOD=0): passed, all predictions met

Scenario passed in 65 s, both rc 0, FATAL/ERROR 0 on both sides, no crash dumps. Anchor (-1769, 1820, 296) from the
first checkpoint save.

1. Both under the world 0.1 s apart:
   - host-own: `kill floor with no grounded teammate Up ... back to the last checkpoint at (-1769, 1820, 396), moved
     1; WarpTo frame not needed`.
   - server copy 367 ms later: `... back to beside the last checkpoint at (-1639, 1897, 396), moved 1; WarpTo frame
     sent`; client `card146: client ... WarpTo 1 (-1639, 1897, 396) ... moved 1` 19 ms after.
   - One return each, 0 `kill floor warp ... (slot` rows, 0 `WARN card046 ... gap` rows, 0 Downed rows. Both
     `wait stand` at the anchor passed. Health 75 on both HUDs (one hurt each).
   - Shots `c370-both-host` (the client stands beside the host on the platform) and `c370-both-client`.
2. Only the client under the world, the host standing: `kill floor warp 3: ... (server-copy) ... to
   BP_PlayerCharacterMI_C (slot 0) +100 ... Warp frame sent`; client `Warp 2 to guid 0x8e ... moved 1` 40 ms later;
   follow check gap 80 uu (limit 200) on both sides, Health 50. Shot `c370-one-client` shows the host beside the
   client, the hurt overlay on screen.
3. WARN diff against c143b55d: no new row on either side. Gone: the three `gap` rows and six `still below the kill
   height` rows (host), two `gap` rows (client), and `card146: host checkpoint save with no player pawn` (that one
   came from the c143 save during the fall, a step this scenario does not take).
