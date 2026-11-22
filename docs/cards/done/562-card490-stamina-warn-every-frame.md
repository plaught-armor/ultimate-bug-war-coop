# Card 562: card490 logs a stamina WARN every frame for every non-trooper pawn

Playtest 2026-11-20 host log (`ultimate-bug-war-coop-public/logs/UBWCoop-menu-20261120-184651.log`): 11629
`WARN card490: host pawn class <class>: Stamina at -1 (float), MaxStamina at -1` rows, one per frame while a player
drives a pawn without stamina:

| Class | Rows |
|---|---|
| BP_BaseMech_C | 6238 |
| BP_MountableTurret_C | 2777 |
| BP_MountableTurret_Gunner_C | 2115 |
| BP_GuidedMissile_C | 499 |

Each is paired with the trooper's normal `card490 ... BP_PlayerCharacterMI_C: Stamina at 3304` row, also every frame.

## Fix

Card 490's stamina lookup runs only for pawns with a stamina attribute, and its rows are logged once per class (a
class with no stamina is a valid state, not a WARN). The per-frame lookup is cached per class.

## Prediction

Same playtest route: at most one card490 row per pawn class per session, no WARN for the four classes above.

## b369 (`4d7adc330945a079`, build-b5 copy kept), not run yet

Fix: per-class stamina table in `sprint.cpp`, so the card 490 check no longer warns every frame.

### Run c559b371a predictions (junior, 2026-11-21)
Scenario `tools/scenarios/c371-c2m1.txt` (FED_C2M1, b371 `baeebee09d3e7f59`).
- The host drives a mech for about 5 s: at most one card490 row for `BP_BaseMech_C` in the whole host log, and no
  `WARN card490` row. At most one card490 row per pawn class on each side.
- Run 3 (C3M1) checks the same for `BP_MountableTurret_Gunner_C` and `BP_GuidedMissile_C`.

### Run c560b371b predictions (c560b371a crashed at 60 s, host still in the dropship; same predictions) (junior, 2026-11-21, b371, C3M1)

0 `WARN card490` rows per frame for `BP_GuidedMissile_C` and `BP_MountableTurret_Gunner_C` on both sides: at most
one row per class per side.

## Result (senior, 2026-11-21): passed, closed

0 `WARN card490` rows in c560b371b (both sides; guided missile, Door Gunner), c558b379a host and c558b376a client
(mech), c559b371b and c397b398a (was 11629 in the playtest). The remaining `card490 ... pc N <pawn>` rows log only
on a change (movement mode, MaxWalkSpeed from sprint, possession) under the `kC490Rows` cap: 35 mech rows over a
mech drive with sprint toggles, 1 to 2 rows per missile or seat. They are not per frame.
