# 503: FED_C5M2 Array_SO4 never completes (SetWaves C_1 encounter never ends)

Status: closed 2026-11-19 (c247b262a, b262 e138228037a8dede). Written 2026-11-19, senior.

## Finding

Array_O1 stays at 0 because Array_SO4 never completes. Seen in every c247 run: b240a, b242a, b242b, b245a, b246a
and b259a (FATAL 0, card132 mismatches 0). Host and client agree, so this is not a desync. It is an objective the
test never finishes, and players would hit it too if the cause is in the mod.

## Trace (2026-11-19, senior, c247b259a)

- The script (card 247 table, row A4; kis.py dump in docs/reference/objective-coop-audit.md FED_C5M2): the Array_SO3
  press adds Array_SO4 and starts EM loop C_7 and SetWaves C_1. SO4 completes only on the SetWaves C_1 encounter
  event (@5127), then Array_O1 (@5260). It is a "survive the waves" objective.
- Host log: `StartEncounter on BP_EM_1FactionSetWaves_C` at 733919374, the Array_SO3 press. After that come the
  card111 wave budget rows (ArrayBugmix base 6 scaled 10, Troopers_Large base 8 scaled 13). There is no
  ObjectiveComplete(Array_SO4) through the run's end at 734495569.
- The scenario's objective step classifies SO4 as `kind marker` ("reach the marker; completed by the level script"),
  because its marker is the same ActorReference as Array_Marker. It stands the players in presence C_5, which fired
  once for Array_SO1 (`once enter 1`) and stays disabled. That stand does nothing for SO4.
- `clear 4000 kill` ran 90 times across 470 s, with 0 left each time inside the radius. The scenario also moved the
  players to Outpost and back between waits.
- Contrast: Outpost_SO3 (SetWaves C_9 encounter) completed in the same run under the same clear and scaling.

## Hypotheses (rank by the first diag run)

A. Card 111 scaling: the raised wave budget (6 to 10, 8 to 13) asks for more spawns than this encounter's spawn
   points or ticket limit deliver, so a wave never reaches its count and the encounter never advances.
B. The encounter needs the players near it, or it spawns outside the 4000 uu clear radius; bugs left alive far off
   hold the wave open.
C. The code kill (`clear ... kill`) is not counted by this encounter's wave (its kill delegate), unlike C_9's.

## Diag (one row class, then one run)

On the host, per SetWaves encounter: the current wave index, spawned and alive counts against the wave budget, and
the encounter-end event. Log a row on each change, plus each alive enemy's distance from the host pawn when a
`clear` finds 0 left while the encounter is open.

## Fix

Depends on the hypothesis the diag confirms. If it is A, scale the spawn limit but not the completion count. B and
C are test-driver fixes: a `kind waves` step that clears map-wide or credits the encounter kill.

## Proof (written before the run)

C5M2 soak: Array_SO4 then Array_O1 complete on the host, the client mirrors both, 0 card132 mismatches, and the six
doors open on both sides.

Reopen when: any SetWaves objective stays open with no enemies left.

## Closed (2026-11-19, senior, c247b262a)

Hypothesis B holds: the wave stays open while one of its NPCs is outside the 4000 uu clear radius. It is not a mod
bug, and a player who goes after the last bug ends it.

Evidence (b262 e138228037a8dede, drive rc 0, 896 s, FATAL/EXCEPTION 0 on both logs, client mismatches 0 in 35 of 35
rounds):
- From +319 s to +669 s each `host clear 4000 kill` reported 0 killed, 0 left. At +672.9 s (drive line 2226) one
  clear reported "1 killed, 0 left, 110 scanned": a late NPC entered the radius.
- Host 737836963: native CompleteObjective Array_O1/Array_SO4 and ObjectiveComplete(Array_SO4), then Array_O1. The
  client replayed both at 737837038/039.
- The last card161 rows show DoorState 1 for all six BP_Door_Sliding_Large_C (C_1, C_4..C_8) on host and client.

Not done:
- The diag did not give per-wave rows for SetWaves C_1. The 600-row card503 cap was spent at host t=737282834 on
  repeated "open at clear" rows for an early friendly Morita trooper wave (9 NPCs, 35-43 km off). If this reopens,
  print a wave only when its counts change, and skip waves with no bug NPCs.
- The test driver still waits about 350 s for the stray NPC. A `kind waves` step that kills the open encounter's NPCs
  map-wide would save that time in every c247 run.

Reopen when: a SetWaves objective stays open with no enemies of that encounter alive anywhere on the map, or a
playtest player cannot end the Array wave.
