# Card 574: weapon selector shows a fresh magazine after a checkpoint reload

Found in run c570b389a (b389). After the card 570 kept-magazine set, the client HUD shows Morita 51/120, but the
weapon selector panel (shot 570-mag-after-client, panel open) shows MORITA MK1 "60/120".

## Lead

The card 570 set writes the magazine count on the equipped weapon after the rejoin. The panel likely reads a
different source: a per-slot cached value or an inventory entry that is filled when the weapon is given (60, a full
magazine) and refreshed only on fire or reload. Trace statically which field or function the selector widget binds
for the "current/reserve" text, and whether the set goes through the path that refreshes it.

## Proof

A client-side shot with the panel open after a checkpoint reload shows the same magazine as the HUD. Carry it on a
planned C1M1 reload run; it needs no run of its own.

## Static trace (senior 2026-11-21)

Widgets: `WB_Weapon_Switch_Base` (panel) and `WB_Weapon_Switch` (one row). Listings in `build/kisw/*.txt`.

- Row `Set Item Ammo(WeaponAmmoCount, AmmoTypeCount, IgnoreWeaponAmmoCount)`: with Ignore true it writes
  "<cached AmmoCount>/<reserve>" and keeps the cache; with Ignore false it writes the new magazine and caches it.
- `ShowMenu` → `UpdateAllAmmoCounts` calls every row with Ignore true: it refreshes the reserve only.
- The cache is written only by `PopulateMenuItem` (on `OnWeaponEquipped`, via `SetMenuItem`) and by
  `UpdateCurrentItemAmmo`, which runs on `PlayerMIWeaponInventory.OnEquippedWeaponStatusUpdated`.
- That delegate is broadcast by `USTWeaponInventory::BroadcastStatusUpdate()` (exe 0x05231780:
  `GetEquippedWeapon`, then `Broadcast(this+0x148, weapon, this)`). Its only caller is the exec thunk, so the weapon
  blueprints call it after their own magazine changes.

Cause: the card 570 set (`SetEquippedAmmunition` 60 → 51 on the new pawn's Morita) fires no status broadcast. The
row keeps the 60 cached when the new pawn's weapon was equipped. The HUD counter reads the weapon live and shows 51.

Sibling: `c21_grant_ammo` (card 137 grant, `ammo fill`) sets the starting magazine after `GiveWeaponByClass`, whose
equip has already cached the row. Same gap.

## b396 `03a56bfe6c55ded5` (= b395 + this, build-b5 copy kept)

`c574_weapon_status_refresh(pawn, inventory)` (weapons.cpp) calls `BroadcastStatusUpdate` on the pawn's weapon
inventory after the card 570 kept-magazine set and at the end of `c21_grant_ammo`. Row:
`card574: <side> weapon status broadcast on <pawn> (selector magazine refresh)` (first 8), or a `WARN card574:` row.

Prediction: on the next C1M1 checkpoint reload run, the client row `card574: client weapon status broadcast on
BP_PlayerCharacterMI_C` follows `card570: client ... kept magazine set`, and shot `570-mag-after-client` with the panel
open shows MORITA MK1 51/120, the same as the HUD.

## Result c397b400a (b400, junior 2026-11-21): passed, closed

Client: card294 restart done at 160203978, then `card570 kept magazine set ... Morita 3` and the card574 weapon
status broadcast at 160204036. In the shots after the reload, the HUD and the selector both show 3 in the
magazine. The reserve reads 0 in the first shot and 60 about 5 s later, on both widgets. That is carded as 580.
