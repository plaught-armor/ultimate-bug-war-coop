# 254 Client runs a pickup's level-script chain on its own

Found by card 241 run 4 (live logs `UBWCoop-host-20261112-194502.log`, `UBWCoop-client-20261112-194505.log`,
copied to `logs/c241-c2m1-r4-*` at run end). Not a play blocker: the state meets again when `LOS_O02`
completes. Until then the client shows "Load the Nuke Launcher" ticked while the host does not.

## Evidence

- Host 173297632: warp to 100 uu from `BP_AmmoPickup_NukesSmall_C_0`; the host pawn collects `_1` and `_5`
  (card071 Nuke 0 to 1, 1 to 2). No host `CompleteSubObjective(LOS_O02, LOS_O02_SO04)` in the whole run.
- Client 173297689: the host pawn's copy overlaps `BP_AmmoPickup_NukesSmall_C_0` and
  `BP_AmmoPickup_NukesLarge_C_1`. card140 blocks both `DisablePickup` calls, but the client's
  `FED_C2M1_Scripting_C` still runs the chain bound to that pickup, as native client calls:
  `AssignStartingInventories`, `EquipWeaponByClass`, `AddCountSubObjective_SubObjectives(LOS_O02, LOS_O02_SO05)`,
  `AddSubObjective(LOS_O02_SO05, LOS_O02_SO05a/b)`, `CompleteSubObjective(LOS_O02, LOS_O02_SO04)`
  (caller exe+0x5082232). No host row exists for any of them.
- Real play shows the same: SO04 complete only in the client log (259655568), never in the host log.

Two readings to settle in step 1: the host never touched the pickup the script is bound to (then the host
state is right and only the client ran ahead), or the host's chain is suppressed by the mod.

## Step 1 (static, no run)

Find which pickup instance the `FED_C2M1_Scripting_C` chain is bound to and by which event (overlap
delegate, pickup-collected delegate). Trace why it fires on the client although card140 blocks
`DisablePickup`, and whether card049 applies or drops the client's own objective calls made from level
script. List every other pickup-bound script chain on the Trooper maps (same event class).

## Step 2 (fix by class)

On the client, a pickup event caused by a remote pawn copy must not run level-script consequences; the
host's rows are the only source. One guard at the event source for the class, not one per objective call.

Proof: c241 scenario O02 block: no client-native `CompleteSubObjective`/`AddSubObjective` lines without a
host row before them; card066 dumps equal after the ammo pickup.

Files: decided in step 1 (`mod/src/ubw/objectives.cpp`, card140 pickup code). Budget: 1 run.

Run 5 settles the two readings: the host completes SO04 itself when its pawn takes nuke ammo below max
(host row at 177209988, with card071 Nuke 0 to 2). The client had completed it natively 108 s earlier
(177101872). So the host state is right and the fault is only the client running the chain ahead of the host.

## Step 1 answers (static, senior, 2026-11-13)

- Every pickup subclass (`ASTAmmoPickup`, `ASTHealthPickup`, `ASTArmourPickup`, `ASTItemPickup`, `ASTKeyPickup`,
  `ASTSecretPickup`, `ASTTacticalSupportPickup`, `ASTWeaponPickup`) ends its take in the base
  `AADPickupBase::Pickup_Implementation` (0x4C61DC0, 0x2F0 bytes). The base does three things in order: FX,
  `DisablePickup` (call at 0x4C61EE3), then `FADPickupDelegate::Broadcast` (call at 0x4C61EEE), which is `OnPickup`.
- Level scripts bind their chains to `OnPickup` of a placed instance (cards 056 and 267 found the same binding).
- Card 140 blocks only `DisablePickup` on the client (`c50_client_pickup_blocked`, `mod/src/ubw/weapons.cpp:986`).
  The broadcast two instructions later still runs, so the client's level script runs the whole chain from a remote
  pawn copy's overlap. This is the event source for the class; no per-map list is needed.

## Fix spec (step 2)

- New symbol `AADPickupBase_Pickup_Implementation` 0x04C61DC0 in `tools/symbols.txt`, one hook-table row.
- Client detour in `mod/src/ubw/weapons.cpp` next to `detour_c50_disable_pickup`: when the card 140 condition holds
  (the overlapping character is not the local player's own pawn), return without calling the original, so no FX, no
  `DisablePickup`, no `OnPickup`. Reuse `c50_client_pickup_blocked` for the test and its log line; do not add a
  second condition. Log `card254: client skipped Pickup_Implementation on <name> (<class>), overlapper <who>`.
- The host and the client's own pawn take the unchanged path. The host's FX and enabled state reach the client by
  cards 136 and 145 as today.
- Report: whether the `DisablePickup` block can still be reached after this (other callers); do not remove it.

Predicted rows (FED_C2M1 O02 block): `card254` skip rows for the nuke pickups; no client-native
`CompleteSubObjective` or `AddSubObjective` row without a host row before it; card066 dumps equal after the ammo
pickup. Budget: 1 run, batched with card 260 on FED_C2M1.

## Correction to the fix spec (senior, 2026-11-13)

`c50_client_pickup_blocked` has no own-pawn test: it holds for any overlapping character on a placed pickup or a
card 104 host copy. The skip uses that same condition, so the client's own take is skipped as well. The host is the
only source: the client's server copy overlaps the pickup on the host (card 267: c63c, c67g), and FX (card 136),
enabled state (card 145) and objective rows come from there. The row carries `own pawn 0|1` as log only. Subclass
credits that run before the base call still happen locally (junior reports which, from two subclasses).

## Run B (c241-c2m1-b1, build 3), senior verdict 2026-11-13

Held: 6 client `card254: client skipped Pickup_Implementation` rows on the nuke pickups (all `own pawn 0`); no
client-only objective completion. One client-first call remains: `CompleteSubObjective(LOS_O05, LOS_O05_SO04)`
ran on the client 6 ms before the host's row, from the same script chain, and card 100 skipped it. No state
difference. Accepted.
