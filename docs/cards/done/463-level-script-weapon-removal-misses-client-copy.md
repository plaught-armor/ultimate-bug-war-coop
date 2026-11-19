# 463 Level script weapon removal leaves the host's copy of the client armed (C1M1 outro, after a checkpoint load)

Senior trace 2026-11-16 21:14, run `b40b166a` (b166 `d88eca03b8b88da0`); the same WARN is in run `b40` (2026-11-14), so it is
not new in b166.

## Evidence

- Client: `WARN card327: client t=523908651 digest round 13 PW differs for two rounds: host 0159, client 5108;
  client weapons 0|0=...BP_WeaponDefinition_Unarmed_C`.
- Host 523857607 (checkpoint load done, before the client is back): `FED_C1M1_Scripting_C` runs the outro step
  (`conversation list FED_C1M1_OUTRO02_C`, `AddObjective(Asteroid_O1)`) and `RemoveWeaponFromOwnerInventory
  BP_Weapon_LaserMorita_C` on one inventory. Earlier in the run the same script function is named in a card021 row:
  `execDropWeapon from BP function RemovePlayersWeapons (in FED_C1M1_Scripting_C host-own)`.
- Host 523857778 and 523858040: weapon lists for the client's copy: `Unarmed | MoritaMk1` (first Pong), then
  `LaserMorita` (list changed). The copy ends with the Laser Morita.
- Client 523866252 (8.6 s later): its own `FED_C1M1_Scripting_C` runs the same step: radio skipped (card 322),
  AddObjective skipped (card 258), but `RemoveWeaponFromOwnerInventory BP_Weapon_LaserMorita_C` runs on its own
  pawn and `gives BP_WeaponDefinition_Unarmed_C outside an apply (#6) ... the next host list decides`. No host
  list follows. The client stays unarmed; the host's copy holds the Laser Morita until the run ends 54 s later.

## Reading (not proven)

The level script takes weapons from the players it finds when it runs. On the host that is before the client's new
pawn exists (or only player 0); on the client it is its own pawn, later. Nothing sends the host's copy the removal,
and the client's local removal is not reported. Class: a level script that edits a player's weapons runs once per
machine on the local player (card 301 class, single-player assumption).

## To trace before a fix

1. Which pawns `RemovePlayersWeapons` touches on the host (whose inventory is `0000000085F1C200` at 523857607), and
   whether the client's copy existed then.
2. Whether the same step in a run with no load (CTF done, both present) leaves both sides equal: rows near the
   first `OUTRO02` in a full C1M1 run.
3. Sibling sweep: other `RemovePlayersWeapons` / `ResetInventory` callers in level scripts of the FED maps.

## Fix direction

The host owns the weapon list (card 050). The client does not run a level script's weapon removal on its own pawn
(same rule as cards 258, 322, 354: one writer); the host applies the script's removal to every player pawn,
including a copy that appears after the script ran, and the card 050 list carries it.

## Predictions

After a checkpoint load at the C1M1 outro: 0 `WARN card327`; the client's `inventory` and the host's row for the
copy name the same weapons; one client row for the skipped local removal.


## Junior trace (2026-11-16 21:24, static, b40b166a rows)

- Inventory `0000000085F1C200` at 523857607 is the host's own. The client's copy existed (controller 523854765,
  pawn 523855688); card 267 mirrored the removal to it.
- Gap A: the card 267 mirror removes only the classes the host's own pawn held. The copy also held MoritaMk1 from
  the card 278 respawn replay (523855701), untouched. Host list 523857778: `Unarmed | MoritaMk1`.
- Gap B (main cause): the client's pre-load card 143 Restore (LaserMorita, sent 523857695) is applied on the host
  at 523857778 (`card143: host weapons from Restore: removed 2, given 1`), after the script's removal. Host list
  523858040: LaserMorita. The client's own script removes it locally at 523866252; no host list follows.
- No-load flow: no run reaches the outro without the load; by reading, with no Restore and no respawn replay the
  mirror leaves both sides Unarmed. Not proven by rows.
- Sibling sweep: of the 8 FED level script packages only FED_C1M1_Scripting calls weapon edits
  (RemovePlayersWeapons, GiveUnarmedWeapon, GiveMorita, GiveLaser, DropWeapon, EnableLeftWeapons,
  RemoveItemByDefinition). Class is C1M1 only.
- b40 lines 1150 / 1151 TRY-FAIL are a scenario fault: the outro checkpoint loadout has 0 grenades, so no card 073
  row is due. Lines replaced by a wait on the host's `items 0` row.

## Fix decided (b168)

(a) A Restore must not undo a newer host script edit: the host skips the weapons part of a card 143 Restore for a
copy whose list a level script edited after its spawn (or re-applies the script edits after the Restore if the
mark has a hole on a plain respawn; junior checks and reports). (b) The card 267 mirror of RemovePlayersWeapons
empties the copy's whole weapon list. (c) The client skips its own level script weapon removal and gifts where the
host mirrors them, one skip row each.

Predictions (b40 on b168): 0 `WARN card327`; after the respawn the host list for the copy and the client
`inventory` are both Unarmed only; one host `card463` row; a client skip row for the outro removal; all rounds 0
mismatches.


## Closed 2026-11-16 22:29: run `b40b168a` (b168 `0c097c2488d82959`)

Predictions met: 0 `WARN card327`, both copies Unarmed after the respawn, host `card463 ... mirrored to ... (host-own)`
rows, `3 script loadout rows newer than its state re-applied`, the client skipped its 3 rows, 0 mismatches. As built:
the mark became a time-filtered replay (`c463_replay_newer`); a clear empties the list only when the source dropped
at least one weapon; DropWeapon, EnableLeftWeapons and RemoveItemByDefinition are not skipped on the client.
