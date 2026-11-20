# Card 069: Why the client has no reserve ammo and cannot reload

Status: sent 2026-11-05 03:06 to junior ultimate-bug-war-coop-bb, after card 068 was accepted.

Read `docs/STATE.md` ("Ammo is not synced", the weapon and inventory layout bullet) and the card 065 follow-ups. Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why

In the card 065 runs, the client's HUD shows a full magazine and 0 in reserve (60/0, then 48/0), and reload does nothing. The host shows a reserve. The magazine lives on the weapon (an attribute); the reserve lives in the pawn's `USTAmmunitionInventory` (`ASTPlayerCharacterMI`+0xA48, `AmmoMap` at +0x118). So the client's weapon is set up, but its ammunition inventory is probably empty.

## Senior research (2026-11-05, static)

- `ASTPlayerCharacterMI::ResetInventory` 0x051DA430 is the only native code that fills a soldier's reserve: it loops the loadout, calls `GetWeaponDefinitionFromTag` and then `USTAmmunitionInventory::AddAmmo` 0x05229E20 (+0x2E8 into the function). It has no direct callers. It is a virtual override of the Blueprint-callable `ASTPlayerCharacter::ResetInventory` (the base is an empty stub; thunk `execResetInventory` 0x05086E20), so a Blueprint calls it, probably from game mode or checkpoint logic that runs only on the host.
- The other native `AddAmmo` callers are `ASTPlayerMechBase::ResetInventory`, `ASTMountableTurret::BeginPlay`, and the cheat manager's `GiveAllAmmo` 0x05206CD0 and `GiveMaxAmmo` 0x05206E90. `SetAmmoCount` 0x0524CAF0 is called from `USTWeaponUtilityLibrary::AddAmmunitions` 0x05229F50 (pickups, probably), `RequestAmmo` 0x0524BDE0 and `ASTWeapon::SyncEquippedInventoryAmmoCountToAttribute` 0x05252710.
- Reload: `USTAttackActionReload::DoAction` 0x02147D10 and `PerformReload` 0x0519F5E0. `RequestAmmo` takes rounds out of the reserve, so with an empty reserve a reload has nothing to load.
- Readers for the log: `GetAmmoCount(tag, bool)` 0x052393F0, `GetMaxAmmo(tag)` 0x0523B8D0, `HasAmmo` 0x05240320, `IsInfiniteAmmoEnabled` 0x05243060. `ASTPlayerCharacterMI::GetAmmunitionInventory` is 0x051C6E10 and the weapon's type tag is `AADWeapon`+0x2C8.

Hypothesis: the host's Blueprint calls `ResetInventory` on the host's pawn only; the client's pawn never gets the call, and card 050 builds the client's weapons through `AddWeapon`, which does not add reserve ammo.

## Do

Behind `UBWCOOP_C69_LOG=1` only, on both machines:

1. Detour `ASTPlayerCharacterMI::ResetInventory`, `AddAmmo`, `SetAmmoCount`, `RequestAmmo`, `PerformReload` and `DoAction` (reload). Log the role, the pawn or inventory, the tag, the amount, the return value and the caller's return address (as an RVA). Log the first 20 calls of each, then count.
2. Each second, log the local pawn's equipped weapon, its tag, and for that tag `GetAmmoCount` and `GetMaxAmmo`, plus the number of entries in `AmmoMap`.
3. Run with the card 063/064 runner: both players spawn, the client fires part of a magazine and presses reload. Take a `shot` on each side after the reload.
4. Report the host's and the client's sequences side by side and name the missing link: no `ResetInventory` on the client, `ResetInventory` runs but `AddAmmo` fails (for example a missing `MaxAmmoMap` entry), or something else.
5. Fix only if the cause is the first case and the fix is small: after the card 050 loadout is applied on the client, call `ResetInventory` on the client's own pawn once, or call `AddAmmo` with the host's counts. Choose by what `ResetInventory` does to the existing weapons (read it first: if it removes or re-adds weapons, use `AddAmmo` instead). The fix runs in every client session, not behind the log flag. Otherwise stop at the Report and the fix goes in a follow-up card.

## Done when

- The Report lists both sequences side by side and names the broken link.
- If fixed: the client's shot shows a reserve above 0 and the magazine refilled after the reload, and the 1 s log shows the count changing.
- `logsum` gives ERROR/FATAL 0, both games end with rc 0, and there is no new dump.
- With `UBWCOOP_C69_LOG` unset, the log detours are not installed.
- fnlen reports no function over 60 lines.

## Report

**Outcome: no fix. The cause is not "no ResetInventory on the client", so the card's small-fix case does not apply.** ResetInventory runs on the client's own pawn. It gives that pawn no ammunition, and it gives no pawn on either side any Morita ammunition. The Morita reserve in this mission comes only from the ammo pickups (`BP_AmmoPickup_MoritaSmall_C`, via `USTWeaponUtilityLibrary::AddAmmunitions`). Their overlap runs on the host and credits the inventory of whichever pawn on the host overlapped them. When the client's soldier walks over a pickup, the credit goes to the client's server copy on the host. `USTAmmunitionInventory` does not replicate, so the client's own inventory stays empty: AmmoMap 0 entries, a reserve of 0, and the magazine is never refilled.

### Runs

All three runs used `tools/scenarios/c69-reload.txt`, and in every run both games ended with rc 0, logsum gave ERROR/FATAL 0, and there was no new crash dump.

| Run | Who walks to the Morita first | Logging |
|---|---|---|
| c69a | host | `UBWCOOP_C69_LOG=1` on both |
| c69b | client | `UBWCOOP_C69_LOG=1` on both |
| c69off | client | unset |

In c69off both logs say `config: UBWCOOP_C69_LOG unset -- card 069 ammo log off`, the "card 069 ammo log on" line is absent, and there are 0 `card069` lines. fnlen reports 0 functions over 60 lines (hooks.cpp 1281, config.cpp 39).

### Sequences side by side (c69a; c69b where it differs)

| Step | Host (own pawn; inventory 6D5AD000) | Client (own pawn) |
|---|---|---|
| BeginPlay | ResetInventory #1 adds `AmmunitionType.Laser` 300 (AddAmmo, caller ResetInventory+0x2ED) | ResetInventory #1 on the own pawn: nothing between before and after, so no AddAmmo or SetAmmoCount |
| Other pawn spawns | ResetInventory #2 on the server copy adds Laser 300 | ResetInventory #2 on the host's proxy adds Laser 300 |
| Walk to the Morita | two Morita small pickups: SetAmmoCount 120, then 240 (caller AddAmmunitions 0x522A10F) | nothing |
| Pick up the Morita | no ammo (card 050 PickupWeapon/GiveWeaponByClass adds none) | no ammo |
| 1 s line, Morita equipped | AmmoMap 2 entries, equipped type count 240 (c69a ran before the stride fix, so the second entry printed as None=0) | AmmoMap 0 [ ], equipped type count 0 |
| Fire 1.5 s | magazine 60 to 44 | magazine 60 to 44 (the shot `after-fire` shows 44/0) |
| Reload (R) | `USTWeaponReloadAbility::RequestNewAmmunitionFromInventory` does SetAmmoCount 180, then 224 (callers 0x524BF76 and 0x524BFB6) | nothing logged; no SetAmmoCount or RequestAmmo |
| The shot `after-reload` | 60/224 | 44/0 |

c69b, with the client walking first, shows the same mechanism from the other side:
- The client's server copy (pawn 88F17220, LocalRole 3, inventory 66A62680) got both Morita pickups: SetAmmoCount 120, then 240.
- The host's own pawn 6CCAAAE0 kept AmmoMap 1 (Laser only), and its `after-reload` shot reads 44/0. The host's reload fails the same way once someone else has taken the pickups.
- The client's own pawn 665FAAE0 had AmmoMap 0 all run, and its shots read 44/0 after the fire and after the reload.

### The broken link

The Morita ammo exists only in the server copy's `USTAmmunitionInventory` on the host, and nothing carries it to the client's own pawn. The client's reload reads its own empty inventory, so `RequestNewAmmunitionFromInventory` has nothing to give.

### What ResetInventory does (0x051DA430)

1. It calls a virtual at vtable+0xA30 on the pawn. This fills a list of weapon tags and a map from ammunition type to count, presumably the loadout.
2. If the gun-game secret is active (`USTSecretsUtilityLibrary::IsSecretActive`), it calls `UpdateEquippedGunGameWeapon` and returns.
3. Otherwise, for each weapon tag it calls `GetWeaponDefinitionFromTag` and gives that weapon through the weapon inventory: pawn+0xA40, vtable+0x500, with arguments (definition, -1, 0).
4. It then walks the ammo map and calls `AddAmmo` for each entry.

So ResetInventory re-adds weapons. By the card's own rule it is not the fix; AddAmmo would be. On the client's own pawn the loadout virtual returns an empty ammo map, so the loop does nothing. On every pawn the loadout holds only Laser 300.

### Other findings

- **The player's reload path.** The player reloads through `USTWeaponReloadAbility::RequestNewAmmunitionFromInventory`, which calls `SetAmmoCount` directly. `USTAttackActionReload::PerformReload`/`DoAction` and `USTAmmunitionInventory::RequestAmmo` never fired on either side; they are not the player's reload path.
- **Log corrections made during the card:**
  - AmmoMap is keyed by ammunition type (`AmmunitionType.Morita`), not by the weapon's `WeaponTypeTag` (`Weapons.MoritaMk1`). The 1 s line now calls GetAmmoCount/GetMaxAmmo with each AmmoMap key.
  - The AmmoMap element stride is 0x14: FName alignment is 4, so 0x18 was wrong.

### What a fix would need (for a later card; not done here)

The host would have to send the server copy's ammunition changes to the client. One way is a host-side detour on `SetAmmoCount` for server-copy inventories that sends a small binary message (ammunition tag and count). The client would then apply it to its own pawn's inventory, through AddAmmo or SetAmmoCount. The reverse direction matters too: the client's own fire and reload change its own magazine, and the host must not overwrite that.

A rough alternative is to give the client's own pawn the host's counts once after the card 050 loadout. This would not follow later pickups, so it is not a real fix.

## Senior review (2026-11-05 03:24): accepted, no fix (correctly)

- Checked c69a, c69b and c69off myself. All three drives end with `scenario passed`, and logsum gives ERROR/FATAL 0 and WARN 0 on both sides of every run. No new dump: the newest crash folder is still the 02:46 one from card 068.
- The off check holds: c69off has 0 `card069` lines on both sides. It ran on the deployed DLL (03:16:35, run start 03:16:41). c69a and c69b ran on earlier builds that differ only in the log code (the AmmoMap stride and the key fix), so their call sequences stand.
- fnlen: 1281 functions, 0 over 60 lines.
- The c69b host log confirms the mechanism: `SetAmmoCount #3 inventory 66A62680 ... AmmunitionType.Morita, amount 120 ... caller exe+0x522A10F` (`AddAmmunitions`, pickup) on the client's server copy. The client's 1 s line keeps `AmmoMap 0 [ ]` to the end.
- Shots checked: `host-c69a-after-reload` reads 60/224, and `client-c69a-after-reload` reads 44/0. The client also shows grenades x0 where the host shows x1, which is probably the same pickup mechanism.
- Not fixing was right: the small-fix case did not apply, and `ResetInventory` re-adds weapons. The fix is card 071: relay the host's `AddAmmunitions` credits on a server copy to that client.
