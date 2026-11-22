# Card 447: a client that leaves the game in a mech or turret takes the seat with it

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: fixed in b150 `c05f5e851f6418a5` and b151; passes in run c447b150a (seat stays, mech reusable after rejoin). Closes on the junior's shot report.

## Defect

While a client pilots a mech or mans a turret, its PC's pawn is the mech or turret (the soldier sits hidden and
attached in the seat, no controller). On Logout the engine's `APlayerController::PawnLeavingGame` destroys the PC's
pawn; the game has no override (publics: only the engine symbol exists). So a client that drops or quits in a seat
destroys the mech or turret for the host and every other player, and its soldier stays in the world hidden, without a
controller. `detour_logout` also hands the mech, not the soldier, to `c43_logout`, so a card 043 entry of the soldier
stays, and the `g_c75_pilots` row keeps both raw pointers.

Not run: no scenario has left the game from a seat. The trace rests on host `card075 ... MechLeave before` rows (PC
pawn `BP_BaseMech_C`, soldier `controller null ... bHidden 1, attached to BP_BaseMech_C`).

## Fix

Host, `c447_logout(pc)` first in `detour_logout`: when the leaving PC's pawn is a mech or turret, release a held
fire relay on it and run the game's own leave (`RequestLeaveMech`, `LeaveTurret`; both finish in the call, run c89g
shows the PC on the soldier 2 ms later). The rest of the Logout then sees the soldier: the engine destroys it, the
seat stays empty and usable, and the leave detours clear the pilot row. Cards 440/445/446 already read the soldier.

## Done when

- Host `card447: host t=... leaves the game in mech BP_BaseMech_C: the game's leave run first, PC pawn now
  BP_PlayerCharacterMI_C (the seat stays)`, no `WARN card447`.
- After the leave the mech is still in the host's mech list (a later card 075 or card 029 row names it), and the
  rejoined client can enter it (`card075: host MechEnter ... OnInteraction`).
- Client screenshot after the rejoin shows the mech where it was left.
- 0 EXCEPTION, digest 0 mismatches.

## Not covered

- `RequestLeaveMech` refused (boarding animation running, no valid ejection point): `WARN card447`, the seat goes as
  before.
- The rejoined client starts on foot at the player start, not in the seat.

## Run c447b147b (b147 c317a84c5a6ff903): failed, second fix

The Logout hook is too late. `APlayerController::Destroyed` runs `PawnLeavingGame` (exe+0x4055780: `Destroy` of the
pawn at PC +0x2E8) before GameMode Logout: host log 509079064 `card098: host destroy BP_BaseMech_C`, 509079065 Logout,
no card447 row. The same order means the PC's pawn is null at every Logout, on foot too, so the pawn half of
`detour_logout` (card 024 release, card 043 row drop, card 448 Health keep) never ran.

Second fix (`c447b_patch.py`, 6 edits, check ok on the b148 tree; needs `python3 tools/pdb_rva.py gen`): hook
`PawnLeavingGame` (always on). Host, remote PC: the seat leave first, then on the PC's pawn the card 024 release,
`c448_logout`, `c43_logout`; then the original, which now destroys the soldier. `detour_logout` keeps only the
per-controller work.

Predictions: host `card447: host ... leaves the game in mech ...: the game's leave run first, PC pawn now
BP_PlayerCharacter... (the seat stays)`; then `card098: host destroy` of the soldier, none of `BP_BaseMech_C`; shot
`c447-mech-after-leave` shows the mech on its pad; the rejoined client and the host can both enter it. Still owed
from card 323: the join replay with a seated host (the host must board before the client reconnects).
