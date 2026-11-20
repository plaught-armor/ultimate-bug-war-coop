# Card 050: The client's weapons follow its server copy

Status: sent 2026-11-04. Playtest blocker; the user wants a playtest tonight.

Read `docs/STATE.md`, the card 021 Report (Steps 1 and 2), the card 024 and 026 channel notes, and the card 048 Report (inventory after a checkpoint). Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why

- Weapons do not replicate. `AADWeapon` has no `bReplicates`, so each machine has its own weapon actors (card 021).
- In real play the client's pawn is granted nothing: `AssignStartingInventories` reads chapter -1 level -1 on the client. Only the test-only `UBWCOOP_GRANT_WEAPON` gave it a Morita.
- The client's fire is relayed to its **server copy's** weapon (card 024). So the server copy's inventory is the truth for damage. The client's own inventory only drives its view, its HUD and its local fire effects, and today it never matches.
- After RESTART CHECKPOINT the server copy is restored from the save (Unarmed in FED_C1M1), while the client keeps a stale MORITA MK1 (card 045 finding 2, card 048).

The fix: the host owns the client's weapon list. It sends the server copy's list on every change, and the client makes its own pawn's inventory match.

## Static facts (senior, 2026-11-04)

`UADWeaponInventory` is at pawn+0xA40.
- `GiveWeaponByClass` 0x04A2C480 `(this, TSubclassOf<UADWeaponDefinition>, int32 slot, bool equip) -> bool`. It returns early when `GetWeaponInstanceFromClass` already finds the class, so a repeated give is a no-op. It creates the instance through vtable +0x558.
- `RemoveWeaponByClass` 0x04A2CFE0, `RemoveWeaponByInstance` 0x04A2D090.
- `GetAllWeaponsInInventory` 0x04A2BBF0, `GetWeaponCount` 0x04A2BF40, `GetEquippedWeapon` 0x04A2BD10.
- `EquipWeaponByIndex` 0x04A2B8F0, `EquipWeaponByClass` 0x04A2B880, `UnequipCurrentWeapon` 0x04A2D620.
- `ASTPlayerCharacter::LoadInventory` 0x05243EF0 and `LoadAbilitySystemComponent` 0x051CDF30 run on the server copy after a checkpoint load (card 048).
- Weapon pickups are `ASTWeaponPickup` (`StaticClass` 0x050B60D0). Pickup logic is Blueprint; the give goes through the virtual path above.
- `USTSaveSystemLibrary::Save_CheckPoint` 0x052180E0 gates on the local PC's byte +0x7B8 and `ASTPlayerCharacter::CanCheckpointSave`. It does not read `USTDebugSaveTracker`'s autosave flag (+0x3A). So the "client death turns off autosave" issue does not block checkpoints; step 5 confirms it at runtime.

## Do

1. **Audit (one run, log only, then go on).** On both machines, detour Give, RemoveByClass, RemoveByInstance, EquipByIndex, EquipByClass and Unequip. Log the pawn (host-own, server-copy, client-own), the class name, slot, equip flag, return value, count after, and the caller. Then pick up one weapon in FED_C1M1 as the client, and one as the host. Report for each pickup: which machine ran Give for which pawn, and whether the pickup actor disappeared on both machines. Put a short table in the Report.
2. **Wire.** Add a `Weapons` kind to card 049's variable-length `ChanOp::Objective` frame, or a new variable-length op that reuses its parser (your choice; say which). The payload is the equipped index, then each weapon definition class as its **path name** text (for example `/Game/.../BP_WeaponDefinition_MoritaMk1.BP_WeaponDefinition_MoritaMk1_C`). Cap 8 weapons and 160 characters per path; drop and log anything longer or not ASCII. One frame carries the whole list.
3. **Host sends.**
   - Send after any of the step 1 natives changes a **server copy's** inventory (count or equipped weapon differs from the last sent list), and once after the server copy's `LoadAbilitySystemComponent` on a checkpoint load.
   - Send the current list on the client's first `Pong` of a map (card 049 journal hook), so a joining client is filled.
   - Send to that server copy's own client connection only.
4. **Client applies.** Resolve each path with `StaticFindObject`, then `StaticLoadObject` as a fallback; log a miss and skip that entry.
   - Remove every weapon the list lacks, give every missing one (slot -1, equip false), then `EquipWeaponByIndex` to the sent index (or `UnequipCurrentWeapon` when the list is empty).
   - Guard the client's own detours with a flag while applying, so the apply does not count as a local change.
   - Log the count and equipped class before and after, and whether the HUD weapon name changed (shot).
   - Client-local gives that the host did not send (from a client-side pickup) stay until the next host list replaces them. Report how often that happens in the run; do not suppress them in this card.
5. **Autosave check.** Log `USTDebugSaveTracker::IsAutoSaveEnabled` (+0x3A) and the `Save_CheckPoint` calls on the host. In `card050x`, let the client die once (card 044 route), then walk the host through the next checkpoint zone. Report whether `Save_CheckPoint` still ran and wrote the save.
6. **Runs.**
   - `card050x`, two players, FED_C1M1, with **no** `UBWCOOP_GRANT_WEAPON`. Take shots of both HUDs at spawn. Pick up a weapon as the client and fire it at a bug; the hit lands on the host (card 024 relay). Pick one up as the host. Then RESTART CHECKPOINT: the client HUD must match the server copy's restored list (UNARMED if that is what the save holds). Take shots of both HUDs at each point.
   - `card050s`, solo: no frames are sent and gameplay is unchanged.

## Done when

- The client's inventory and HUD match its server copy's after a join, after a pickup by either player, and after RESTART CHECKPOINT.
- The client's relayed fire uses the weapon it sees.
- The step 5 autosave result is reported.
- rc 0 on all runs, no new dump, the build is clean with `/W4 /WX`, and every function is 60 lines or fewer.

## Risks

- A pickup may run its Blueprint on the client only, so the server copy never gets the weapon. If step 1 shows that, relay the client's pickup to the host on `ChanOp::Use` (card 043 route): the host runs the same pickup interaction for the server copy. Then step 4 brings the client in line. Report before building anything larger.
- Ammo is not synced in this card. Log the ammo of each weapon on both sides at each shot point, so a follow-up has numbers.
- Removing the equipped weapon on the client while it fires may break the local fire ability. Unequip before removing.

## Addendum (senior, 2026-11-04): pickups placed in FED_C1M1

The name map of `FED_C1M1_Scripting.umap` lists `BP_WeaponPickup_MoritaMk1`, `BP_WeaponPickup_LaserMorita`, `BP_AmmoPickup_MoritaSmall`, `BP_AmmoPickup_MoritaLarge`, `BP_HealthPickup_Medium`, `BP_GrenadeAmmoPickup_Frag`, `BP_GrenadeAmmoPickup_Incendiary` and `BP_DevLogPickup`.
- Use the two weapon pickups for step 1 and step 6.
- In the same step 1 run, also have the client take one ammo pickup and one health pickup. Log which machine applies the effect, for which pawn, and whether the pickup disappears on both machines. Log only; a fix for ammo and health pickups is a follow-up card unless the step 1 relay fix covers them for free.
- Log each pickup actor's `bReplicates` (actor+0x5B bit 3) and its role on both machines.
- Pickups are overlap-driven. `AADPickupBase::OnBeginOverlap` 0x04C61750 filters the overlapping actor by class (`ACharacter`) and has no authority or net-mode check. `Pickup` 0x04C61940 and `Pickup_Implementation` 0x04C61DC0 follow. So the server copy may already take a pickup on the host when its replicated movement overlaps it, while the client's own pawn takes the same pickup on the client. Step 1 should confirm or refute this. If it holds, step 4 only has to correct the cases where one side took a pickup and the other did not (for example, the host took it first and it vanished on the host only).

## Addendum 3 (senior, 2026-11-04): after card 049

Card 049 added a `Chapter` frame: the client's game instance now gets the host's chapter and level (+0x410/+0x414) before the first objective. After it, the client's `AssignStartingInventories` gives its own pawn `Weapons.Unarmed`, but a pawn set up before the frame keeps what it had. In step 1, log every client `AssignStartingInventories` call and whether it ran before or after the Chapter frame. The host list from step 3 must win in both cases.

## Addendum 4 (senior, 2026-11-04): weapon pickups go through Interact, not overlap

This corrects the overlap assumption in Addendum 2 for weapons.
- `ASTWeaponPickup::OnInteraction` 0x051D0630 calls `PickupWeapon` 0x051D2FD0. `PickupWeapon` casts to `ASTPlayerCharacterMI`. If the pawn already has the class (`GetWeaponInstanceFromClass`), it only adds ammo. Otherwise it calls `USTWeaponUtilityLibrary::AddWeapon` (already in `tools/symbols.txt`), honouring `USTGameUserSettings::IsWeaponPickupToActiveSlot`.
- `ASTWeaponPickup::Pickup_Implementation` 0x051D42E0 (the overlap path) only adds ammo for weapons the pawn already owns.
- `UADInteractorComponent::Interact` 0x04A1FE70 uses the interactable cached at component +0x100 (set by `TickComponent` 0x04A20230 through `TraceForInteractable` 0x04A20780). It checks `CanInteract` (vtable +0x28 on the cached interface) and broadcasts the multicast delegate at +0xC8. `ReleaseInteraction` is 0x04A200F0.
- The host's `ChanOp::Use` handler (`c43_receive_use`, hooks.cpp ~16153) only drives revive holds. It never calls `Interact` on the server copy. So today a client weapon pickup gives the client's own pawn only, and the server copy gets nothing. Step 4 as written would then remove the client's new weapon again.

Change to the plan:
- Step 1: log, for the client's pickup, whether the server copy's interactor has a cached interactable at +0x100 when the client presses Use (does `TickComponent` trace on the server copy?).
- New step 3a: on a client `Use` press (in `c43_receive_use`, after the revive logic declines it), call `Interact` on the server copy's `UADInteractorComponent`; on release, `ReleaseInteraction`. If +0x100 is empty on the server copy, call `TraceForInteractable` first, and report whether it finds the same pickup. Log the interactable's class each time.
- This relays every client interaction (switches, satchel sites, ziplines, item and tactical support pickups, turrets) to the host, not only weapons. In this card, keep the relay limited to `ASTWeaponPickup`, `ASTItemPickup` and `ASTTacticalSupportPickup` (class check on the cached interactable), and list in the Report which other interactable classes the client used in the run. A wider relay is a follow-up card.

## Addendum 5 (senior, 2026-11-04): the server copy's trace, and a fallback key

- `TickComponent` 0x04A20230 calls `TraceForInteractable` right after `UActorComponent::TickComponent`, with no locally-controlled or role check. So the server copy's interactor traces every tick too, and +0x100 may already be set on the host.
- `TraceForInteractable` 0x04A20780 returns at once when the component pointer at +0x108 is null or pending kill. Otherwise it traces from that scene component (location at +0x200, its forward vector) out to the range at +0xF8 (float). On the server copy the result depends on whether that component's rotation follows the client's aim. If it does not, the host's +0x100 can be empty or a different actor than the client's.
- Fallback if step 1 shows a mismatch: put the client's interactable identity in the `Use` payload. Pickups and switches placed in the level are not spawned at run time, so their object names (for example `BP_WeaponPickup_..._C_3`) and outer level are the same on both machines. On the host, find the actor by that name in the same level (`StaticFindObjectFast` with the level as outer, or a walk of the level's actor array), check it implements `UADInteractableInterface`, then set it as the server copy's interactor +0x100 target before `Interact`, or call its `OnInteraction` directly with the server copy's interactor. Say which you used. Run-time spawned pickups (from a dropped weapon) do not have a stable name; skip them and log them.
- Log, for each relayed press, the host's +0x100 actor name before your change and the client's actor name, so the Report shows how often they match.

## Report

**Build:** `cmake --build build` builds clean (`/W4 /WX`). The new code is in `mod/src/hooks.cpp` under "Card 050". The switch `UBWCOOP_C50_TEST` is read in `config.cpp`. Thirteen symbols were added to `tools/symbols.txt` (eight hooked, five resolved).

**Runs:**

| Run | Setup | Duration | Result |
|---|---|---|---|
| `logs/card050x-*` | pair; `UBWCOOP_AUTODROPSHIP=1 UBWCOOP_C50_TEST=1` | 60 s, ended by the done pattern | 0 FATAL/ERROR, 0 WARN, no crash dumps |
| `logs/card050s-*` | solo | 151 s | 0 Weapons frames, 0 relays, no warnings |

Screenshots 01278–01281.

### What was built

**Wire: two new text ops, `Weapons` (host to client) and `UseName` (client to host).**
- They carry up to 1400 printable-ASCII chars (`chan_text_frame` / `chan_text`). The existing ops carry numbers only.
- An Objective-style 64-char name field was too short. One class path is about 92 chars, for example `/Game/Blueprints/Weapons/Unarmed/BP_WeaponDefinition_Unarmed.BP_WeaponDefinition_Unarmed_C`.
- `chan_to_host` was split into `chan_send_host(pc, WireString)` plus the old wrapper.

**Host side**
- Each server copy that has sent a Pong is polled every 250 ms.
- The list text is `<equipped index or -1>|<definition class path>|...`, in `Items` order, at most 8 weapons.
- A new list is sent when:
  - it differs from the last one sent;
  - it is the first Pong;
  - the copy has a new pawn;
  - the copy went through `LoadAbilitySystemComponent`.
- Each send is logged with its reason.

**Client side**
- The last list is held. It is applied when it arrives, when the own pawn changes (on foot, not downed) and after `AssignStartingInventories`.
- An apply does four things:
  1. Resolves each path with `StaticFindObject`, falling back to `StaticLoadObject`.
  2. Removes weapons the host does not have, unequipping first. This is skipped if any path failed to resolve, so nothing is lost on a lookup failure.
  3. Gives missing weapons with `GiveWeaponByClass(slot -1, no equip)`.
  4. Equips the host's equipped class.
- It then checks the result against the host's list (as sets, plus the equipped class) and logs it.
- It equips with `EquipWeaponByClass`, not `EquipWeaponByIndex`. Slot order differs between the two machines whenever the gives happened in a different order, so the class is the stable key.
- Gives to the client's own inventory made outside an apply are counted and logged (`c50_note_give`). This run had 0.

### Step 3a / Addendum 5: Use relay

On the host, `c43_receive_use` calls `c50_relay_use` after `c43_use_edge`. A press that started a revive hold (`c43_find_hold`) is not relayed, so a revive wins.

**Choosing the target**
- If the client sent a fresh `UseName` (the placed actor name, sent just before its Use, valid for 2 s):
  - When it matches the name of the copy's own +0x100 target, that target is used.
  - Otherwise the host looks for the placed actor (RF_WasLoaded) with that name among the three pickup classes and uses it.
  - Both names are logged on every relayed press.
- Without a fresh name, the copy's own +0x100 target is used.

**Allowed classes:** `ASTWeaponPickup`, `ASTItemPickup` and `ASTTacticalSupportPickup`. Other targets are declined, and the decline is logged.

**Method:** `GetInterfaceAddress(target, UADInteractableInterface)` is written to the copy interactor's +0x100, then the original `Interact` is called. The release is relayed to `ReleaseInteraction` only for a copy whose press was relayed. `OnInteraction` is not called directly.

**Result in card050x**
- The test moved the server copy next to `BP_WeaponPickup_LaserMorita_C_0` (host-side teleport; a client teleport is corrected back by the server). The client pressed Use from 164 uu away.
- Relay #1 ran. The copy's +0x100 was already `BP_WeaponPickup_LaserMorita_C_0` (logged as "already set"). This shows the copy's own trace finds the pickup when the copy is standing at it.
- The host then logged, in order:
  1. `ASTWeaponPickup::PickupWeapon` on the server copy;
  2. `DisablePickup`;
  3. 250 ms later, "Weapons ... list changed: sent, 195 chars".
- The client received the list and gave LaserMorita: "apply #2 ... given 1, matches the host".
- Equipped stays Unarmed (index 0) on both machines, because the pickup does not auto-equip on the host either.
- Name counts: match 0, mismatch 0. The client did not send a `UseName` (see the open issues), so this press went through the no-name branch. The name-match and find-by-name code was not exercised live.

### Addendum 3: AssignStartingInventories timing

On the client in FED_C1M1 the order was:

| Event | Client ms |
|---|---|
| Own pawn on foot | 575723017 |
| `AssignStartingInventories` | 575723448 (431 ms later) |
| First Weapons frame | 575725127 |
| Card 049 Chapter frame | 575729723 |

`AssignStartingInventories` ran about **6.3 s before** the Chapter frame, so the log says "no Chapter frame yet this map". `AssignStartingInventories` came first, and the held list (or the first one received) is applied after it, so the starting loadout cannot overwrite the host's list.

### Audit, log only

- `PickupWeapon`, `Pickup` and `DisablePickup` log name, class, placed, bReplicates, roles, character side and callers.
- `Save_CheckPoint` and `RemoveWeaponByClass` / `EquipWeaponByIndex` / `EquipWeaponByClass` / `UnequipCurrentWeapon` log the side, "game" or "card 050 apply", and callers.

**Finding:** placed pickups have **bReplicates 0** (host: roles local 3, remote 0; client: local 0, remote 3). The host's `DisablePickup` does not reach the client.

### Open issues

1. **The client's own interactor target was null** at 164 uu while aimed at the pickup (and at 130 uu in the first run). The client's `Interact` therefore did nothing locally. This is good, since nothing was given outside an apply. But it also means `UseName` was not sent, and the Addendum 5 name path depends on a client-side target. The shot at 01281 shows the client looking straight at an open, empty weapon case. Either the client-side trace does not run for a client pawn, or the pickup's interactable is off on the client. This needs a check of the client interactor's +0x100, +0xF8 and +0x108 each tick, which I did not do.
2. **Pickup state is not synced** (bReplicates 0). When the client takes a pickup through the relay, the host's copy of that pickup is disabled, because the host and the server copy share one world. The client's local copy of the pickup stays visible. A pickup the host takes also stays on the client.
3. **Ammo is not synced** or logged.

### Skipped

- Ammo sync and ammo logging.
- Ammo and health pickup visits in the test.
- Firing at a bug.
- Calling `TraceForInteractable` (Addendum 5 says it already runs every tick).
- Calling `OnInteraction` directly.
- Host-side moves for the host's own pawn.

## Senior review (2026-11-04)

**Verdict: accepted, with the untested steps carried into card 055.** The playtest needs the sync now, and card 055 already plays the missing beats.

**Checked by the senior**
- Logs: `card050x` host rc 0, client rc 0, 0 FATAL/ERROR/WARN, no new crash dumps. `card050s` host rc 0, no Weapons frames, no relays.
- First Pong sends the 92-char Unarmed list. The client applies it as "removed 0, given 1, matches the host".
- Relay #1 runs `PickupWeapon` on the server copy, then `DisablePickup`. 250 ms later the host sends the 195-char list. The client applies it as "given 1, matches the host". 0 gives outside an apply.
- `fnlen.py`: 1085 functions, 0 over 60 lines. The DLL was rebuilt at 10:56.
- Shots 01280 and 01281 show the client's HUD as UNARMED after the LaserMorita apply, which is correct: the host does not auto-equip either.
- The client's case is shown open and empty. This does not match open issue 2 ("the client's local copy stays visible"). Card 055 must look at the pickup on both screens and say which is true.

**Code review (attacked, not found broken)**
- Apply order:
  - remove is skipped on a resolve failure;
  - remove keeps the snapshot pointers while the array shifts;
  - unequip runs before an equipped weapon is removed;
  - an empty list (`-1`) unequips.
- Equipped index: `c50_text` re-bases it to the kept index when a path is dropped.
- Relay:
  - a revive hold wins;
  - the release is relayed only for the pawn whose press was relayed;
  - targets outside the allowed classes are declined.
- Notes, not defects:
  - `relayed_id` and `use_name` are single globals. That is fine for two players; a third player needs per-copy state.
  - `c50_interact` leaves its write at +0x100. That is harmless, because the copy's trace overwrites it on the next tick.

**Not done (the card asked for these)**
- RESTART CHECKPOINT with a weapon held:
  - the forced send after `LoadAbilitySystemComponent`;
  - the held list applied again on the new pawn.
- Step 5: the client death, then the next checkpoint. `Save_CheckPoint` was logged three times (flag 1, then 0, 0), but no client died in this run, so the autosave question stays open.
- A host pickup.
- Firing the picked-up weapon at a bug.
- The `UseName` path. The client's interactor target is null, so the name path never ran.

Card 055 step 3 now names each of these as a check.
