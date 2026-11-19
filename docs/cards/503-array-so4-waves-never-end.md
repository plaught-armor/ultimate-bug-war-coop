# 503: FED_C5M2 Array_SO4 never completes (SetWaves C_1 encounter never ends)

Status: open (written 2026-11-19, senior). Batch into the build after b260.

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
