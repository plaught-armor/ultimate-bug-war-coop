# 528: Client re-sees the old map's corpses as arrivals after a map change

Found 2026-11-19 in c263L313 (b313, FED_C3M2 load run). Client WARN rose from 34 (c263L312) to 82; 62 of them are
`WARN card137: client corpse arrival BP_AI_WarriorBug_C: arrival list full (32), the death visual played`, all at
28470535 to 28470543 ms, after the map end rows (28469736) and after `FSeamlessTravelHandler::Tick returned after a
map change` (28470510). The host has none. Not caused by card 526 or 527.

## Trace (static, senior)

- 28469736: LoadMap runs the map end and arm steps. `c37_arm_map` (`sprint.cpp:1108`) resets `g_c37`, so the seen
  table (`g_c37.seen`) is empty. The client's card 038 map totals at that moment say `dead tracked 94`.
- 28470535: `c37_step` scans again. 32 + 62 = 94 pawns come back, each with Health 400 -> 0 notified and
  `card131: client corpse first seen dead` at C3M2 coordinates. With an empty seen table every pawn is `first`, and a
  first sighting with Health <= 0 is a corpse arrival (`c37_pawn`, `enemies.cpp:642`). The arrival list holds 32; the
  other 62 play the death visual.
- So the scan reads the left map's pawns after the arm. Not yet known: whether `g_get_game_world` still returns the
  left world for those frames, or `c23_collect` reaches the old persistent level through the new world (seamless
  travel). `g_c37_pawns` (`enemies.cpp:666`) is not reset at the arm either.
- The count scales with the corpses alive at map end (c263L312 ended with fewer), which is why it grew.

## Fix (to decide after one read, no run of its own)

- Read which world the 28470535 scan used: one row behind `UBWCOOP_C132_FX=1` in `c37_scan` on the first fresh walk
  after each arm (world pointer, `g_c37_pawns.world`, pawn count, first pawn's outer level).
- Then skip in `c37_scan` every pawn not in the current world's levels, or every pawn whose object flags mark it as
  being destroyed, whichever the row shows. Reset `g_c37_pawns` in `c37_arm_map`.
- Predictions (next lane 2 run that changes map): 0 `arrival list full` rows and 0 `corpse first seen dead` rows in
  the first 2 s after a map arm; the card 038 `dead tracked` count of the new map starts at 0.

Diagnostic in source (not yet in a build): `c528_walk_row` in `enemies.cpp`, called on each fresh walk of
`c37_scan`. Behind `UBWCOOP_C132_FX=1`, up to 8 rows per process, when the seen table is empty and the walk found
pawns: `card528: <side> t=... fresh walk with an empty seen table: world W (last walk's L), N pawns; first <name>
level P in world Q`, with ` (not this world)` when Q is not W. Compiles clean.

## c263L314 (b314, 2026-11-19)

Two card528 rows, 0 `arrival list full` rows (c263L313 had 62). Row 1 (t=29134216, last walk's world 0) is the first
walk of the process, so it says nothing about a left map; row 2 (t=29343062) walked the same world as the last walk.
The run never changed map with corpses alive, so the bug was not exercised. Caveat on the row: a ULevel's outer is
its own package UWorld, so a pawn in a streaming sublevel always prints ` (not this world)`; only the world pointer
against the last walk's is a clean signal. ULevel::OwningWorld has no offset in the tree yet.

Next: the proof needs a run that changes map with corpses alive (c263 FED_C3M1 to FED_C3M2, as c263L313). Carried by
the next lane 2 build's c263 run; no run of its own.

## c263L315 (b315, 2026-11-19): bug reproduced, the corpses are in the new world

The map change with corpses alive is the scenario's end `host travel restart` (FED_C3M2 to FED_C3M2; c263L313 never
loaded C3M1). Client order:
- 29953648 `FSeamlessTravelHandler::Tick returned after a map change (loaded world=C9E0F5D0)`, map totals `dead
  tracked 86`.
- 29953666 card528: world C9E0F5D0 (last walk's 488D9A20, so the scan read the new world), 72 pawns, first
  BP_AI_WarriorBug_C in level 5ACE13C0, whose outer is C9E0F5D0.
- 29953667 to 29953672: 54 `corpse first seen dead` (card137 pose arrival, base state 4 -> 3, HandleDying called)
  and 21 `arrival list full`. Health values such as -44, -310 and -144 are overkill values, so these are the
  left map's corpses, not freshly placed pawns.
- Host: `travel restart` at 29952431, its own map change only at 29953809, 161 ms after the client's.

Reading: the client finished loading the new world before the host left the old one, and the host's old-world corpses
(still on open actor channels) were spawned into the client's new world. Not proven: the card434 spawn and close rows
were capped long before the map end. So resetting `g_c37_pawns` or a world check in `c37_scan` would not help: the
pawns really are in the current world.

Next (diagnostic first, no run of its own): uncap card434 spawn bunch and channel close rows for the 2 s after each
client map arm (one row each, with guid and Health), so the next c263 shows whether the 72 pawns arrived by spawn bunch
in that window and when their channels close (expected at the host's map change). Fix once that row confirms it: on
the client, a pawn first seen dead before the host's new map is live is neither a corpse arrival nor given
HandleDying; it waits for its channel close.

Diagnostic in source (2026-11-19, compiles clean, for the next lane 2 build): `c434_row_allowed` in `enemies.cpp`.
For 5 s after each client map arm (`c41_relay_arm_map`, same arm as `c37_arm_map`), card434 spawn and close rows
skip their per-map caps (own cap 192). The spawn row now carries the object name, which joins it to the card131
`corpse first seen dead <class> <name>` row. How to read the next c263 end restart:
- The 72 names have spawn rows after the arm: the host's old-world corpses were sent into the new world by spawn
  bunch. Their close rows (expected near the host's map change, reason 0 or 2) give the gate's end.
- No spawn rows for those names: the pawns were not newly spawned. They were carried into the new world, so the gate
  must key on something else (for example the seamless travel actor list). Limit: the arm clears the spawn ring, so a
  channel opened before the arm logs no close row.

## c263L316 (b316 5e4d5d4e5fec2ece, 2026-11-20): carried by seamless travel, fix in source

- End `host travel restart`: client LoadMap 56154209, then `card392: client seamless travel: 119 kept NPCs in the
  transition world` (56155475). The card434 map totals at the arm: 273 spawn bunches, 154 channels closed; no card434
  spawn row at all after the arm, so no spawn bunch brought the corpses.
- 56155503 card528: fresh walk in world CC5C0150 (Untitled), 41 pawns, first BP_AI_JumperBug_C in that world; then
  27 `corpse first seen dead` (WarriorBug, Health 0 to -729, base state 4 -> 3, HandleDying called). The run quit
  before the arrival. In c263L315 the same pawns were seen in the destination world, after card 392 carried them on.
- `arrival list full` 0 (21 on L315): fewer corpses at the end of this fight.

Reading: the pawns are card 392's kept NPCs (the left map's actors the client carried out by seamless travel; the
host destroyed them all), seen first in Untitled and again in the destination world.

Fix in source (2026-11-20, objects compile, for b317):
- `hooks_ubw.cpp`: card 392's kept table also stores each NPC's raw pointer and now outlives the arrival (an `arrived`
  flag stops the second still; the table is replaced at the next Untitled). New `c392_kept_npc(actor)`: raw match,
  confirmed by the weak ref.
- `enemies.cpp`: `c528_drop_kept` removes those NPCs from each fresh level walk in `c37_scan`, so none is a first
  sighting. Row (up to 8 per process, when the seen table is empty): `card528: <side> t=... walk skipped N NPCs
  carried by seamless travel (card 392), M pawns left, T skipped this process`.

Predictions (next lane 2 run with the end restart): a card528 skipped row after the end arm with N > 0; 0 `corpse
first seen dead` and 0 `arrival list full` rows from the arm to the host's map arm; corpse rows on the new map only for
pawns that die there.

Player impact: likely none (the rows fall in the travel, before the new map shows), but each re-seen corpse runs the
game's HealthAttributeChanged and a death visual on an actor that is being torn down, which is a crash risk.

Files: `mod/src/ubw/enemies.cpp`, `mod/src/ubw/sprint.cpp`. Budget: carried by the next lane 2 build, no own run.

## Result c263L317 (b317 68fc2b1d8b1981c6, 2026-11-20): fixed

Client line 146570: `card528: client ... walk skipped 79 NPCs carried by seamless travel (card 392), 0 pawns left, 79
skipped this process`. After it: 0 `corpse first seen dead` and 0 `arrival list full` rows. ERR 0, digest 0
mismatches. Predictions hold; card closed.
