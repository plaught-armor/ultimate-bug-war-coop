# Card 553: C1M1 scripted weapon pickup gives the host nothing until the client takes it too

Playtest 2026-11-20 on b366 (`e62e76f63a05d88f`), FED_C1M1 training. User report: "when host picks up laser rifle,
and they don't receive it until i (client) pick it up. same with the morita in the training course."

## Cause (static)

Card 495 (`weapons.cpp`, `c495_hold`) holds the base `AADPickupBase::Pickup_Implementation` of the scripted
pickups (`BP_WeaponPickup_LaserMorita_C`, `BP_WeaponPickup_MoritaMk1_C` on `FED_C1M1_Scripting_C`) until every living
player has taken it. The design assumed each take credits its taker through `ASTWeaponPickup::PickupWeapon`. The
user report says it does not: the weapon a player gets comes from the level script's OnPickup chain
(RemovePlayersWeapons, then GiveLaser / GiveMorita on player 0, mirrored by card 267), and that chain runs only on
release. So the first taker waits empty-handed for the other player.

To confirm from the host log (`card495 held` / `released` rows, `card050` give rows): the host's own weapon list at
its take and at the release.

## Fix direction

The first take must arm its taker at once, as single player does. Only the objective step (door C_1 and
CaptureTheFlag_SO1, ShootingRange_SO1, TacSupport_SO3) waits for every player. Options to weigh after the trace:
give the taker the site's weapon on its own take (the same give the script uses, to that pawn only), or let the
script's give run on the first take and hold only the objective part. The user rule from card 495 still holds: the
mirror give does not count as a take, and the door opens only when both players have taken it.

## Prediction

Host takes first: the host's weapon list gains the laser on the take frame, the client's does not, the door stays
shut. Client takes: the client gains the laser, the door opens. Same with the client first, and with the two Morita
sites.

## Host log (`ultimate-bug-war-coop-public/logs/UBWCoop-menu-20261120-184651.log`)

Confirmed:
- `card495` held `BP_WeaponPickup_MoritaMk1_C_1` from 340703086 until the client took it at 340712616 (9.5 s). Only
  then did card 267 mirror the AddWeapon to host-own.
- The laser was held from 340999663 to 341012077 (12.4 s).

The host had no weapon from its own take during each hold.

## b369 (`4d7adc330945a079`, build-b5 copy kept), not run yet

Fix: the held branch of `c495_hold` now arms the taker with the site's weapon definition
(`c553_arm`: `AddWeapon` with the definition CDO, then `c50_host_nudge`). Proof row: `card553: ... armed its taker`.

### Run c551b370a predictions (was c552b369a, now on b370) (junior, 2026-11-21)
Same run as card 552 (C1M1 soak). Each scripted pickup (laser at CaptureTheFlag_SO1, Morita C_1) logs
`card495 ... held` on the first take and, in the same ms, `card553: host t=... held take of <pickup> armed its taker
<pawn> (...): AddWeapon(<def>, equip 1) = 1`; the taker's slot rows (card050 on the client when the client takes first)
gain the weapon before the second take. The `released` row still waits for 2 of 2 takers, and the step Complete comes
after the release.

## b420 (`5ca4dcf0b1251f1e`) and run c553b420a: fixed

Cause of the c551b370a WARN (senior trace): native `ASTWeaponPickup::PickupWeapon` had already added the Morita to
the taker unequipped (card021 `AddWeapon(..., equip 0) = 1`) in the same ms, before the held `Pickup_Implementation`.
So `c553_arm`'s `AddWeapon(equip 1)` returned 0 (already owned), and the taker stayed on `BP_Weapon_Unarmed_C` until
the release chain.

Fix: when the add returns 0, `c553_arm` finds the site's weapon in the taker's inventory by definition class and
equips it with `EquipWeaponByClass`, then nudges the weapon list. Row: `... already held, equipped (index N) = 1`.
The WARN stays only when the weapon is not held and the add failed.

Run c553b420a (`tools/scenarios/c553-c1m1.txt`, logs/c553b420a-*.log):
- host t=171475919 `card495 ... held ... taken by 1 of 2`.
- t=171475921 `card553: ... armed its taker BP_PlayerCharacterMI_C (host-own): already held, equipped (index 1) = 1`.
- t=171476116 card494 shot row: `equipped BP_Weapon_MoritaMk1_C`, 24.4 s before the client's take.
- t=171500563 `released ... taken by 2 of 2`. 0 WARN card553.

Closed 2026-11-21 by the senior on c553b420a.
