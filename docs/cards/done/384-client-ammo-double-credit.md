# 384 Client gains a pickup's ammunition twice (local credit plus the host's Ammo frame)

Senior, 2026-11-15. Found reading run `c276b67` (b67 `cc1d4251720f389d`).

## Evidence

- `c276b67`: the client walked over `BP_AmmoPickup_MoritaSmall_C_0` (120 rounds), Morita reserve 0 before
  (client AmmoMirror at 406906655 carried Laser 300 only).
  - client 406926886 `card254: client skipped Pickup_Implementation on BP_AmmoPickup_MoritaSmall_C_0`
  - host 406926893 `card071: ... AddAmmunitions on server copy ... Morita asked 120, 0 to 120 (max 240); Ammo sent`
  - client 406926901 `card071: client ... Ammo AmmunitionType.Morita +120: 120 to 240`
  - host 406927042 `card072: ... AmmoMirror ... Morita 120 to 240`
  One pickup of 120 gave 240 on both sides.
- Card 266's own evidence (`c127-c2m1`, weapon drop of 60) has the same shape: the copy went 120 to 180 while the
  client's apply row read 180 to 240.

## Cause

`ASTAmmoPickup::Pickup_Implementation` 0x51D33F0 calls `AddAmmunitions` at 0x51D35C7 before the base
`Pickup_Implementation`, where card 254's client cut sits. `detour_c71_add_ammunitions` passed through in a client
session, so the client's own pawn was credited locally, and the host's frame for the same take was added on top.
SYNC_AUDIT entry 4 knew the leak and deferred it to the `TryPickup` entry cut (after the playtest).

## Change (ammo.cpp, for b68)

Both credits of one take are paired per ammunition tag within `kC384PairMs` (2000 ms): `c384_pair` keeps each
side's unpaired gain; the later credit is reduced by the earlier one. Order 1 (local first, the normal case):
`c71_apply_pair` adds only `amount - local`. Order 2 (frame first): `c384_local_add` sets the count back by the
paired amount after the original. A local credit with no frame, or a frame with no local credit, is left whole,
so a client-only source and a host-only source (crate Blueprint on the copy) both still credit. The local run is
kept because it drives the client's own pickup notification.

Attacked: count at maximum on either side (gain 0, nothing paired or noted); unequal gains (the smaller is paired,
the rest stays); two takes of one tag inside the window (one row each, paired in order); eight tags pending (ninth
is not noted: one double credit, bounded); the changed client row still matches the c243 scenario regexes.

Becomes dead code when the `TryPickup` entry cut lands; remove it then.

## Proof: run `c384b68` (b68 `efdda02128e72fdc`, scenario passed, 64.2 s)

- client 407457492 `card384: ... local AddAmmunitions AmmunitionType.Morita 0 to 120: 0 already given by the host's
  frame, count now 120`
- host 407457509 `card071: ... AddAmmunitions on server copy ... Morita asked 120, 0 to 120 (max 240); Ammo sent`
- client 407457524 `card071: ... Ammo AmmunitionType.Morita +120 (card 384: 120 already credited locally): 120 to
  120, AddAmmo returned 0`
- client 407457662 `AmmoMirror sent: ... AmmunitionType.Morita 120`; host: 0 `AmmoMirror` raise rows (copy already
  at 120).
- Predictions 1, 2, 4 hold (FATAL/ERROR 0; WARN: attach on both, client `ui continue` failed under `try`).
  Prediction 3 not reached: the weapon pickup credited no ammunition in this run. Frame-first order not reached.

Accepted 2026-11-15.
