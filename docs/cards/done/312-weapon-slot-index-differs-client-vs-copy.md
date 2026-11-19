# 312 Weapon slot index differs between a client's inventory and its server copy

From the b14 C1M1 soak (`logs/b14-soak-c1m1-*`, DLL `17ac07260788d9b3`). Senior trace 2026-11-14, logs, code and
disassembly of `SendWeaponSwitchRequest` (exe+0x51DB390). Cause settled; code may start with step 1.

## Symptom

Host, once: `WARN card141: host t=279259221 copy weapon BP_Weapon_MoritaMk1_C underslung 0 (want 1) 600 ms after
the Switch`. The client's weapon is in the underslung state, its server copy is not: the client's underslung
shots are relayed to a copy that fires the rifle.

## Cause

Card 141 and card 057 relay weapon switches by slot index, and the index is not the same on both machines.

- Client 279258615: `IA_SwitchWeapon_Slot2`, `ConstructWeaponSwitchRequest(1, 0, BP_Weapon_MoritaMk1_C)`, caller
  0x51DB464. That call site passes the equipped index (`esi`, read through `inventory+0xE8` vtable +0x20), so the
  client's Morita is equipped in slot 1 and key Two is the underslung toggle there.
- Host 279258619: the replayed `SendWeaponSwitchRequest(1, 0)` on the copy builds
  `ConstructWeaponSwitchRequest(1, 0, BP_PlayerCharacterMI_C)`, caller 0x51DB491: the slot-switch branch. The
  copy's equipped index is not 1, so the request reads as "switch to slot 1", which is empty. No
  `USTWeaponUnderslungAbility::ActivateAbility` row follows (the passing case `logs/c141b-host.log` 265824746 has
  it in the same millisecond, both sides index 0).
- How the slots diverged: the copy got the Morita through the game's own pickup (`card054 ... relayed Use #2`,
  list `Unarmed` before, `Morita` after). The client picked it up locally into the next slot (`Unarmed|Morita`),
  then card 050 apply #2 removed `Unarmed` (`removed 1, given 0, matches the host`). `c50_same` and the Weapons
  text compare the packed weapon list (`c50_list`), not slots, so the row says "matches" while the client's Morita
  sits in slot 1 and the copy's in slot 0.

## Fix

Slot layout is part of the weapon state card 050 owns; a relay must not carry a machine-local index.

1. Step 1, report before coding: read the inventory's slot array (the object at `inventory+0xE8`, equipped index
   getter at vtable +0x20) and state how `RemoveWeapon`, `GiveWeaponByClass(-1)` and the pickup's replace choose
   slots. Add each weapon's slot index and the equipped slot to the card 050 text on both sides (host list and
   client `before` / `after`), so a slot difference is a logged fact.
2. Card 050 apply: the client ends with each weapon in the slot the copy has it in. `c50_same` compares slots too;
   a difference is `WARN differs from the host`.
3. Card 141 host replay, weapon branch (packed bit 2): call `SendWeaponSwitchRequest` with the copy's own equipped
   index, not the client's. The frame already says "toggle underslung on the equipped weapon"; the index adds
   nothing the copy does not have.
4. Card 057 `ChanOp::Equip` (`weapons.cpp:1580`, index as the argument): send the weapon's definition class (or
   its position in the card 050 list) and resolve the slot on the copy. With item 2 in place the indexes agree;
   this keeps a transient difference (a pickup in flight) from equipping the wrong weapon.

## Sibling sweep (report before coding)

Every chan op or journal row that carries a weapon, item or ability slot index: list it, say which machine's
index it is and whether the receiver resolves it against its own layout. Known: `Switch` (141), `Equip` (057),
`BugForm` (208, 210), tactical support slot use (100), grenade slot (070 / 073).

## Rows

- Both sides, card 050 text: `<equipped slot>|<slot>=<definition path>|...`.
- Host: `card141: host ... Switch (..., underslung, want <w>) ...: SendWeaponSwitchRequest(<copy index>, 0) on the
  server copy; client index <a>`.

## Predictions (inside the next C1M1 run; client picks up the Morita, then key Two at the shooting range)

- Card 050 rows after the client's Morita pickup show the same slot for the Morita on host and client.
- `card141: host ... copy weapon BP_Weapon_MoritaMk1_C underslung 1 (want 1) 600 ms after the Switch`, preceded by
  a host `USTWeaponUnderslungAbility::ActivateAbility` row on the server copy. Zero `WARN card141`.

## Done when

Both predictions hold in one run and the sibling list is in the report.

## Step 1 result (junior, 2026-11-14, static: shipping exe and PDB)

Conclusion: a weapon's slot is its position in `UADInventory.Items`, which keeps holes; the slot a weapon lands in
depends on which slots were free when it arrived, so two machines with different pickup histories hold the same list
in different slots. Card 050 now carries and compares slots, and every relay that carried a client slot resolves it
on the copy (141 by the copy's equipped index, 057 and 210 by class).

Slot facts:

- Slot = position in `Items` (+0xD8), 16-byte `TScriptInterface` entries; an empty slot is a null entry.
  `GetWeaponBySlot(u8)` jumps to `GetWeaponInstanceFromIndex(slot)`.
- Equipped index: `EquippedItemIndex` at inventory+0x120, read by `GetEquippedItemIndex` (0x015FE270) through the
  interface at +0xE8, vtable +0x20 (the call `SendWeaponSwitchRequest` makes).
- `UADInventory::RemoveItem` (0x04A28C80) nulls the entry in place; nothing shifts (b14: client `Unarmed|Morita`,
  Unarmed removed, Morita stays in slot 1).
- `AddItem_Internal` (0x04A287D0) with slot -1: the item's existing index, else `FindFirstFreeIndex` (0x04A28AF0,
  first null entry, else append). A slot >= Num grows the array with null entries.
- `USTWeaponInventory::GetNextAvailableSlot(tag, preferred)` (0x0523C340): `preferred` when its category list holds
  the tag, else the first slot whose list holds it and is free (vtable +0x568), else -1.

Condition 1 (the 2b move keeps the weapon actor and its state):

- `RemoveWeaponByInstance` (0x04A2D090): `FindItemIndex`, then `UADWeaponInventory::RemoveItem` (0x04A2CD50). That
  calls `UnequipCurrentItem` (0x04A2D590, interface vtable +0x18) when the item is the equipped one
  (`ASTWeapon::OnUnequip` 0x05245890: `HideWeapon`, `AADWeapon::OnUnequip`), nulls the entry, and calls
  `AADWeapon::RemoveOwnerInInventoryFeatures` (0x04A2CEF0, vtable +0x758: `RemoveFeaturesFromActor` on the owner).
  No destroy.
- Underslung state: `IsWeaponInUnderslungState` (0x05243BD0) reads the tag container at the weapon ASC +0x130
  (`GetWeaponASC` 0x03650320). `ApplyTag` (0x05230810) sets loose tag counts on the weapon's ASC and the owner's
  (`GetWeaponInventoryOwnerASC` 0x04A2C3F0); remove and give touch neither, so the state survives a move unless the
  ability is mid-animation.
- No cheap read of a switch animation in flight exists on the client: the whole card 050 apply waits 600 ms
  (`kC50SwitchHoldMs`) after the client's own Equip or Switch request, and each move row logs underslung before and
  after.

Sibling sweep (every op or row that carries a slot or index):

| Op or row | Whose index | Resolved on the receiver's layout | Result |
|---|---|---|---|
| Switch (141) | Client slot | Yes: the copy's own equipped index | Fixed (item 3) |
| Equip (057) | Client slot | Yes: text frame with the class path, copy slot by class | Fixed (item 4) |
| Bug copy Equip (210) | Client forms slot | Yes: same frame as 057 | Fixed (item 4) |
| BugForm (208/209), host to client | Host position in the packed list | No; both lists come from the bug pawn's class defaults | Check only: no row shows a difference |
| TacSupport (100) | Input slot 0-3 of the tactical support component | Same class on both (card 310) | OK |
| GrenadeItem (073) | None: class path plus quantity | By class | OK |
| Card 267/278 GiveWeaponByClass rows | The level script's constant slot | The replay skips an occupied slot | OK |

## Code (b15)

- 2a: card 050 text `<equipped slot>|<slot>=<class path>|...` on both sides (host send row, client before/after).
- 2b: client apply moves each weapon the host has in another slot: `RemoveWeaponByInstance`, then
  `GiveWeaponByInstance(w, host slot, equip)` (original, under `g_c50.applying`); all misplaced weapons leave first.
  A failed move is a WARN and the weapon goes back to any free slot.
- 2c: `c50_same` on the client also compares each slot and the equipped slot. The host's card 143 restore compare
  stays list-only (the copy's slots are its own; its client follows them).
- 4: Equip is a text frame `<client slot>[ <class path>]`. The host equips the copy's slot of that class; a class
  the copy lacks is a WARN and no equip. Only an empty slot or Unarmed (no path) uses the client slot.

Rows:

- Client: `card050: client moves <weapon> slot <from> -> <to> (equip <0|1>): <ok|failed>; underslung <a> -> <b>;
  now in slot <n>`, and the apply row's `moved <n>` plus `slots before ..., after ...`.
- Client: `card057: client t=.. switch to weapon <slot> (<class path | empty slot | Unarmed, no path>): Equip sent`.
- Host: `card057: host t=.. Equip from <pc>: client slot <s>, class <path>, copy slot <c>, used <the copy slot of
  the class | the client slot>: <result>`.

## b15 run crash (fixed in b16)

- Row: client log `UBWCoop-client-20261114-022545.log` line 3996, `CRASH 1/4: EXCEPTION_ACCESS_VIOLATION
  (0xC0000005) at 0x0 thread 640`, `executing 0x0`, stack `#1 ubwcoop+0x20A20` (c50_move), right after the client's
  TacSupport_SO2 Morita pickup (host CompleteSubObjective(TacSupport_SO2) at t=283588860, crash at 283589063).
- Cause: the 2b move called `g_orig_give_by_instance`, the trampoline of the GiveWeaponByInstance hook row. That row
  is diag-gated (`UBWCOOP_DIAG`); the proof run had diag off, so the hook was not installed and the trampoline
  stayed null. A call through it jumps to 0x0.
- Fix (b16): a CallSpec row `g_give_by_instance` (hooks_ubw.cpp kCallsUbw) resolves the target in every run; c50_move
  calls it at both sites. Sibling: `c154_listen_here` (lobby_ui.cpp, key H) called the diag-gated `g_orig_listen`
  the same way; it now calls the CallSpec `ue::net::g_listen`. A sweep of all 449 hook rows (119 gated) found no
  other trampoline of a gated row used outside its own detour except `g_orig_c41_try_timed` in pose_c132.cpp
  rest_arrival, which is null-checked.
- Guard (b16): after install, `log_null_slots` (hooktable.cpp) logs one `WARN hooks: <n> null pointers after install
  (call rows and installed trampolines): <names>` row if any call slot or installed row's trampoline is null; no row
  when all are set.
- Play build 4 ran with diag off (every 20261113 game/ log: `config: UBWCOOP_DIAG unset`), so its H key would have hit
  the same null `g_orig_listen`; the playtest logs have no `card154: H:` row, so H was never pressed. c50_move is
  post-playtest code (b15), not in play build 4.

## Closed 2026-11-16 (senior, run `c439b168c`)

Both predictions hold in one run. Host `card050: ... Weapons to BP_PlayerController_C ... sent ... 0|0=...MoritaMk1`
at 528053793 and client `card050: client ... apply #2 ...: removed 1, given 1, moved 0, matches the host`. Host
`card141: ... copy weapon BP_Weapon_MoritaMk1_C underslung 1 (want 1) 600 ms after the Switch` at 528102010 and
`underslung 0 (want 0)` at 528116260; 0 `WARN card141`. The sibling list is in the b15/b16 sections above.
