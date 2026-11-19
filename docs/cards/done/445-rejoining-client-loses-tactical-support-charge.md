# Card 445: a rejoining client loses its tactical support charge

Status: fix written 2026-11-16 16:32 (`c445_patch.py`, senior scratchpad; `--check` ok on the b144 tree), not built.
Found in run `c423b144b` (b144 `b09caa0e7c1f80da`, C2M1); the same rows are in b143a and b144a. Sibling of card 440.

## Defect

Host `card327` player row of the client's copy: charge `20000 1 -1 0 -1 0 -1 0` before the leave (506780794), and
`0 0 -1 0 -1 0 -1 0` on the new copy after the rejoin (506802649). Card 440 keeps weapons and ammo of a gone
controller and applies them as a held Restore; the charge of the tactical support slots was not in it, so a player
who drops and reconnects loses a ready support call.

## Fix (3 files: `restore_c143.cpp`, `weapons.cpp`, `hooks_internal.h`)

`c440_keep` also takes the copy's charge text (the card 104 text last sent). When a rejoined controller adopts a kept
entry held as a Restore, the charge is owed to it (`c445_owe`). `c445_step` (each `c143_host_step`) applies it once
the controller's Restore hold is gone and every carried slot has a live ability on the new MI pawn, through
`c106_set_charge` (the game's UpdateAbilityCharge and SetIsAbilityReadyForUse); it drops it with a WARN after 40 s.
The card 104 poll then sends the copy's new charge to the client as TacCharge, as on any change.

## Done when (one C2M1 `c423` run, the rejoin steps)

- Host `card445: host t=... kept tactical charge "20000 1 -1 0 -1 0 -1 0" for controller N: set on the new pawn`.
- The host `card327` row after the rejoin carries the same charge text as the row before the leave.
- Client `card104` TacCharge row after the rejoin with that text (or the client HUD shot shows the call ready).
- 0 `WARN card445`.

Not covered: the client's own Restore after a checkpoint reload carries no charge (not checked whether the game's
save restores it); a second client gone at the same time gets the newest entry (card 440's stated limit).

## Accepted 2026-11-16 16:44 (run `c423b145a`, b145 `851ac6a55c0aed2b`)

Host `card445: host t=507603346 kept tactical charge "20000 1 -1 0 -1 0 -1 0" for controller 259774: set on the new
pawn, 9 ms after the rejoin`. Host card 327 charge: `20000 1 ...` before the leave (507570013), `0 0 ...` at 507603588
(the starting inventory's own row), `20000 1 ...` at 507603840. Client `card104 ... TacCharge 20000 1 ...: own slots
0 0 ... -> 20000 1 ...` at 507603867. 0 `WARN card445`, 0 EXCEPTION, scenario passed. The client sees the charge at 0
for 22 ms first.
