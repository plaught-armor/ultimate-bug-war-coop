# 307 A joining client spawns at the map's PlayerStart, not beside the host

Playtest 2026-11-13 (user plays as client, FED_C3M1): "when I spawned into the map I wasn't next to the host".

## Trace (client log `UBWCoop-menu-20261113-184603.log`, host log `client/UBWCoop-menu-20261113-182510.log`)

First `significance view` own-pawn rows after each of the three FED_C3M1 joins (host t = client t - 173869236):

| Join | Client pawn (client t) | Host pawn (host t) | Apart |
|---|---|---|---|
| 1, map entry | (-63773, -48690, 701) at 257972207 | (-70570, 16162, 409) at 84102879 | about 65200 uu |
| 2, after the host's restart from save | (-61946, -49703, 450) at 258405877 | (-74927, -1092, 1715) at 84543086 | about 50300 uu |
| 3, rejoin after the connection loss | (-64897, -48208, 469) at 258552724 | (-62051, -28026, 1428) at 84683228 | about 20400 uu |

All three client spawns are at the same place, the map's PlayerStart. The host log has no `card046` row for any
of them; its only three `card046: ... spawns beside` rows are on FED_C2M1 (82952017, 83319881, 83349617), where
the PlayerStart was encroached.

## Cause (static, settled)

`c46_spawn_spot` (`mod/src/ubw/objectives.cpp`, near line 3601) moves a player spawn beside an Up player only
when `FindPlayerStart` fell back to the world origin: `if (vec_dist(at, Vec3{}) > kC46OriginUu ...) return
transform;`. On a map whose PlayerStart is free the joining pawn keeps the PlayerStart, however far the host
has gone. Card 046 fixed the fall through the world, not the placement.

## Fix

One rule for every spawn of a non-host player pawn at `kC46SpawnPawnCall` (join, rejoin, respawn after a host
restart or load): if a player is Up, the pawn spawns beside that player at a spot `c46_free_spot` clears; the
origin test no longer gates it. The PlayerStart is kept only when the Up player is within 1500 uu of it (map
start, both at the start) or rides the dropship (the join then follows the dropship snapshot path of card 159,
unchanged). Nobody Up: the card 146 rescue spot, as today. The existing `card046` row drops the words "at the
origin" unless that was the case and gains `PlayerStart at (...) <uu> uu from <player>`.

- Rows: `card307: host t=<ms> player spawn for <pc>: PlayerStart (...) is <uu> uu from Up player <name>: spawns
  beside at (...)`, `card307: host ... kept PlayerStart: <within 1500 uu|dropship ride|nobody Up>`,
  `WARN card307: host player pawn <name> is <uu> uu from every Up player 5 s after its spawn`.

## Sibling sweep (same class: where a player appears after a load, join or restart)

| Item | Check |
|---|---|
| Join at map entry, host already away | beside the host |
| Rejoin after a connection loss | beside the host |
| Host restart mission, load last save, load game with the client present (cards 048, 294) | both players at the save's spot, client beside the host |
| Client respawn after death with the host Up (card 043 path) | beside the host or at the game's own revive place, stated in the card |
| Host inside a mech, a turret, the dropship, a tunnel or an interior | spot is reachable and not inside geometry (card 300 trap) |
| Host in combat | spawn spot not inside an enemy; no spawn protection added |
| Checkpoint rescue anchor with nobody Up (card 146) | unchanged |

## Predictions (one run, after the build)

- FED_C3M1, host warps 500 m from the PlayerStart, client joins: one `card307 ... spawns beside` row, client
  `significance view` own pawn within 500 uu of the host's within 10 s, zero `WARN card307`; screenshot from the
  client aimed at the host shows the host.
- Client rejoins after a forced close: same.
- Both at the PlayerStart at map start: `card307 ... kept PlayerStart: within 1500 uu`.
- Digest round after: 0 mismatches.

## Fix (junior, coded, uncompiled at writing)

`c46_spawn_spot` (`mod/src/ubw/objectives.cpp`) now also moves a spawn at a free PlayerStart when the new
`c307_beside_up` says so. Kept PlayerStart (row `card307: host t=<ms> player spawn at (...) kept PlayerStart: <why>`):
the host's slot (player controller 0) has no pawn, so the host's own pawn is the one spawning (map start, restart,
load); nobody Up; the Up player within 1500 uu of the PlayerStart; the Up player attached to a `*Dropship*` actor. Else
the existing beside-Up branch runs (`c46_free_spot`, the +x fallback) and writes `card307: host t=<ms> player spawn:
PlayerStart (...) is <uu> uu from Up player <name>: spawns beside at (...)`. The `card046` row says "at the origin"
only for the origin case and gains `PlayerStart at (...) <uu> uu from <player>`.

Not coded: `WARN card307 ... 5 s after its spawn` (needs a timed follow-up step; the existing card 046 warp follow-up
covers only warps). Open: client respawn after death goes through card 043 revive, not RestartPlayer, so it is not on
this path.

## Closed 2026-11-16 (senior, b168 logs)

The rule runs on every non-host spawn. `c448b168a` host: join `PlayerStart (-560, -1715, 128) is 3740 uu from Up
player ...: spawns beside at (-1619, 1820, 296)`, two rejoins after loads `20510 uu ... spawns beside at (12650,
14100, 29)`. `c460b168a` (FED_C3M1, the playtest map): `75869 uu ... spawns beside at (2165, -8899, 247)`. `b40b168a`:
`24018 uu ... spawns beside`. Kept cases logged with their reason (`the host's own pawn spawns`, `nobody Up`). 0
mismatches and 0 EXCEPTION in those runs. Not coded and dropped: the `5 s after its spawn` WARN (card 046's follow-up
covers the fall case). Client respawn after death stays on card 043 revive.
