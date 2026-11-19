# Card 450: card 327 player row can read a freed soldier through a stale mech pilot slot

Status: in b151 `7d8087dbc3cee8ab`. Regression check owed on a FED_C4M1 run (card327 seat rows).
Found by static trace after run c447b147b (the stale "1 mech pilot row" at the rejoin).

## Fault

`g_c75_pilots` rows `{mech, soldier}` are written at every EnterMech and cleared only at map end
(`c75_map_end`), never at a leave or when the mech or soldier is destroyed. `c327_soldier` (weapons.cpp) asks
`c75_soldier_of(pawn)` for every PC pawn, every row step. A pawn allocated at a destroyed mech's address (the mech
died, or went with a leaving client before the card 447 fix) matches the stale row, and `c327_inventory_hash` then
reads the destroyed soldier: a host crash of the hb57 class. No log shows it yet; the window is an address reuse.

## Fix

`c327_soldier` consults the slots only when the pawn is a mech. A live possessed mech always has a fresh row
(EnterMech rewrites the mech's slot), so the soldier in it is live.

## Proves done

No new row. Regression check in any run with a mech: `card327: host ... seat mech BP_BaseMech_C` rows still appear
while a client pilots, and `seat none` after the leave; 0 EXCEPTION.

## Not covered

`c75_join_replay` (card 323) counts stale rows too ("1 mech pilot row(s)" after the mech was destroyed). The count
is a placeholder until card 323 settles a seat op; clearing a slot at LeaveMech belongs there, with a check that the
card 079 destruction rows, which read the slot after the leave, take a null pilot.
