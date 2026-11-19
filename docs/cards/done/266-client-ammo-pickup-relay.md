# 266 Client gains no ammunition from weapon pickups (singular AddAmmunition not relayed)

Senior order 2026-11-13. Goal: every host-side ammunition credit on a client's server copy reaches that
client, whatever library entry made it.

## Step 1: static trace (verdict: no gap, no code change)

The card 243 claim ("the singular `AddAmmunition` is not relayed") was wrong.

- Singular `AddAmmunition` 0x5229E60 builds 1-element tag and amount TArrays (alloc 0x1164E70) and calls the
  plural `AddAmmunitions` 0x5229F50 at 0x5229F10. The plural is hooked (`detour_c71_add_ammunitions`,
  ammo.cpp), so it is the common point for both entries. Hooking the singular too would send twice.
  The card 137 test grant (singular) also passes the plural hook (host only; the client returns early).
- Plural callers: execAddAmmunitions 0x50B8674 (Blueprint: crates), ASTAmmoPickup::Pickup_Implementation
  0x51D35C7, the singular 0x5229F10, UpdateEquippedGunGameWeapon 0x525648D.
- Singular callers: execAddAmmunition 0x50B853D; AddAmmunitionTypeToInventory 0x51BC320, called from
  PickupWeapon 0x51D310A / 0x51D3125 / 0x51D3300 / 0x51D3369 and WeaponPickup Pickup_Implementation
  0x51D43E0 / 0x51D4400.
- Paths that bypass the plural, via `AddAmmo` 0x5229E20 (GetAmmoCount + jmp SetAmmoCount 0x524CAF0):
  ASTPlayerMechBase::ResetInventory 0x50FFB5C, ASTMountableTurret::BeginPlay 0x51BDE8F,
  ASTPlayerCharacterMI::ResetInventory 0x51DA718 (map start; covered by the card 072 AmmoSet), cheat
  GiveAllAmmo 0x5206DC9 and GiveMaxAmmo 0x5206F6F. None of them is a play-time credit to a player pawn.
- Other SetAmmoCount callers: plural +0x1BA, RequestAmmo, RequestNewAmmunitionFromInventory (reload),
  SyncEquippedInventoryAmmoCountToAttribute: spends and syncs, not credits.
- Card 072 mirror order: the client applies the card 071 `Ammo` row, then its next `AmmoMirror` carries the
  new count back to the copy; the row-to-ammo clear is unchanged.

## Live evidence (logs/c127-c2m1, client walked over a MoritaMk1 death drop)

```
host   card050 DisablePickup BP_WeaponPickup_MoritaMk1_C                      t=139732413
host   card071: host t=139732413 AddAmmunitions on server copy ... Morita asked 60, 120 to 180 (max 240);
       Ammo sent: AmmunitionType.Morita 60
client card071: client t=139732559 Ammo AmmunitionType.Morita +60: 180 to 240
client card072 AmmoMirror sent: Morita 240 Underslung 8                       t=139732571
host   card072 AmmoMirror from ... Morita 180 to 240                           t=139732581
```

Underslung was at its 8/8 maximum, so it was not credited. Earlier soaks showed few card071 copy rows only
because their scenarios never walked over drops.

## Change

None. DLL unchanged: 75d9c5ce204aa049.

## Proof (run c243-c3m2-r5, first Monorail_O1 defend round)

`host mark`, `client mark`, `both inventory`; both roles walk over the nearest BP_WeaponPickup_MoritaMk1_C
drop; waits on the host card071 copy row, the client apply row, the client's next AmmoMirror and the host
own-pawn row; `both inventory`. Expected: Morita +60 (or to cap) on the client, equal on the host copy and
still equal after the mirror; the host's own pawn credited the same way.

## Closed 2026-11-16 (senior): no gap, no code change

Run `c243-c3m2-r5` holds the pair for a non-crate credit: host `card071: host t=219387938 AddAmmunitions on server
copy ... AmmunitionType.Nuke asked 1, 0 to 1 (max 2); Ammo sent` twice, client `card071: client t=219387938 Ammo
AmmunitionType.Nuke +1: 0 to 1` and `1 to 2`. With the c127-c2m1 weapon-drop rows above, both entries (singular and
plural) are shown to pass the one hooked point. The double credit of a drop spawned on top of the client is card 239.
