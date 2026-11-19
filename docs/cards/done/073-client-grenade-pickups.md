# Card 073: Give the client the grenades its pickups grant on the host

Status: sent 2026-11-05 04:46 to junior ultimate-bug-war-coop-bb, after card 070 was accepted.

Read `docs/STATE.md` ("Ammo is not synced", "Ability net policy") and the card 071 Report, section "Grenades". Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

In card 069's shots the client shows grenades x0 where the host shows x1. Card 071 found that grenade pickups (`BP_GrenadeAmmoPickup_Frag_C`, `BP_GrenadeAmmoPickup_Incendiary_C`) do not go through `AddAmmunitions`:

- They run `ASTItemPickup::Pickup_Implementation` (exe+0x51D4139, called from `AADPickupBase::Pickup_Implementation` exe+0x4C61EE3).
- On the host, that path calls `UADGASBasicsUtilityLibrary::AddAbilityToActor(AActor*, const FADAbilityToInputTags&, UObject*)` (call site exe+0x4A0EFD5).
- When the client's soldier takes the pickup, that call gives its server copy `GA_FragGrenadeThrowingAbility` (card017 GiveAbility, handle 151).
- The client's own pawn gets nothing, so the client cannot throw.

## Senior research (static, 2026-11-05)

- `USTGrenadeThrowGameplayAbility` has `OnDoAction_Implementation`, `OnGrenadeSpawned`, `StartThrowingAnimation` and `StartEquippingWeaponAnimation`. Its publics name no grenade count, charge or stack, so the HUD's x0/x1 is not a field of the ability's native class.
- Not known yet, and this card must find out:
  - where the count lives: an attribute in an attribute set, a gameplay tag stack, the number of ability specs, or an `AmmoMap` entry that is not written through `AddAmmo` or `SetAmmoCount`;
  - what a second pickup does when the pawn already has the ability.

## Do

0. **Card 072 seed lost at respawn** (found in card 070 run c70b, client log t=217.70 s: `WARN card072: client: AmmoSet with no ammunition inventory`). Card 050 sees the new copy pawn and seeds it, but the AmmoSet frame reaches the client before its respawned pawn has an inventory, and `c72_receive_set` drops it. Fix: keep the last AmmoSet text on the client, and apply it from `c72_client_step` once the own pawn has an inventory. Drop it after 10 s with a WARN. Also check what the client's first AmmoMirror after a respawn sends: it must not lower the new copy before the seed is applied. Prove it with the card 059c death route (client env `UBWCOOP_C43_TEST=1 UBWCOOP_C45_TEST=1`). After the respawn, the client's 1 s line (`UBWCOOP_C69_LOG=1`) must read Laser 300 and the WARN must be gone.
1. **Diagnose**, behind `UBWCOOP_C73_LOG=1`, on both machines:
   - Detour `AddAbilityToActor` (log only). Log the actor, the ability class name, the input tags, the source object's class, the returned handle and the caller RVA.
   - Each second, log the local pawn's grenade count. Start from the HUD widget that draws x0/x1 and find what it reads. Log the ASC's ability specs whose class derives from `USTGrenadeThrowGameplayAbility`, with their level and input ID.
   - Run with the client taking a frag pickup, then the host taking one, and take a `shot` on each side after each pickup. Throw one grenade on each side and log what changes.
2. **Fix** by relaying the grant the way card 071 relays ammo:
   - **Host:** after `AddAbilityToActor` on a remote player's server copy, send that client one text frame on a new `ChanOp`. The frame holds the ability class path and the input tags, plus the count if step 1 shows the count is separate from the grant.
   - **Client:** apply the same grant to its own pawn. First check that a `GiveAbility` on the client's own ASC is allowed. If the own pawn's ASC is not authoritative on the client, the engine refuses with "GiveAbility called on ability ... on the client, not allowed!". In that case, find the path card 050 uses for its client weapons and use it, or stop and report.
   - **Count:** make sure a throw on the client spends the client's own count, and that the server copy's count does not block the next pickup. Card 072's mirror shows the pattern for this. Use it only if step 1 shows the count is kept apart from the ammo mirror.
   - The fix runs in every co-op session, not behind the flag.
3. **Prove** it with a new scenario: the client takes a frag pickup, the HUD reads x1, the client throws, and the grenade explodes on both screens. Then rerun the host-first order. The host's own grenades must behave exactly as before.

## Done when

- Step 0: the respawned client gets its seed, with no `AmmoSet with no ammunition inventory` WARN.
- The Report names where the grenade count lives and what the HUD reads.
- If fixed: the client's HUD shows the grenade after the pickup, the throw works and is seen on the host, and the count drops on the client.
- `logsum` gives ERROR/FATAL 0, both games end with rc 0, and there is no new dump.
- With `UBWCOOP_C73_LOG` unset, the log detours are not installed.
- fnlen reports no function over 60 lines.

## Report

**Result: fixed, with one open item.** The client now gets grenades from pickups. Its HUD shows x1, and G throws the grenade. The throw spends the item, and the HUD drops to x0. The grenade explodes and does damage on the host. **The client does not see its own grenade or the explosion**, though it takes the burn damage. Grenade projectiles do not replicate. Fixing that is a separate spawn relay, proposed below.

### Step 0: respawn AmmoSet seed

- The client holds the respawn `AmmoSet` text and applies it from `c72_client_step` once its own pawn has an ammunition inventory. It drops the text after 10 s with a WARN.
- c73b: the seed applied at both respawns, with "held 0 ms" and "held 1109 ms". There was no `AmmoSet ... dropped` WARN. Run c73a, which was before this change, had that WARN.
- c73b's two client WARNs are card 044's own `no CompleteDeath ... called here` fallback, which is the expected path after a client death.

### Step 1: where the count lives and what the HUD reads

- **Count:** the pawn's reflected `ItemInventory` (`USTItemInventory`), in `Items` at +0xD8 (16-byte entries) with Num at +0xE0.
- **Item fields:** each `USTInventoryItem` has `Quantity` at +0x38, `ItemDefinition` at +0x40 and `OwningInventory` at +0x48.
- **HUD:** `WB_Inventory_Item` calls `FindItemByDefinition` and shows `Quantity` as `x{n}`.
- **What replicates to the client:** only the granted ability spec. None of these do:
  - the inventory items;
  - the counts;
  - the input tag binds;
  - the ability instance's `SourceItem`.
- **CanActivate:** `USTInventoryItemGameplayAbility::CanActivateAbility` needs `SourceItem` (a UPROPERTY at +0x3B8) to be a live item with `Quantity > 0`. `OnGiveAbility` sets it from the spec's `SourceObject`, which is null on the client. So before the fix, the client had neither a HUD count nor an ability it could activate.

### Step 2: the fix (every co-op session; only the log detours sit behind `UBWCOOP_C73_LOG`)

- **Count relay:**
  - The host hooks `AddInventoryItem_Internal` and `RemoveItemByDefinition` on a client's server copy.
  - It sends a `GrenadeItem <definition path> <quantity>` text holding the absolute count, so a lost or late message self-corrects.
  - The client adds the item to its own inventory, or sets its quantity, so the HUD reads the true count.
- **Grant:**
  - The client intercepts its own local grant of the grenade ability.
  - When the host's spec replicates, the client binds the input tags to that spec's handle, so the press reaches the server copy.
  - It also sets the instance's `SourceItem` to our own item, after checking that the item's `OwningInventory` is our pawn's inventory. Without that, CanActivate fails.
- **Stale handle unbind (`c73_drop_stale`):**
  - When the host removes a spent grenade's spec, the client still has the old handle bound.
  - Run c73g crashed the client on the second G: `EXCEPTION_ACCESS_VIOLATION` reading 0xC at `AbilityInputTagPressed+0x197` (exe+0x4A0EB97), because `FindAbilitySpecFromHandle` returned null and was not checked.
  - Dump: `prefix/.../Saved/Crashes/UECC-Windows-55C1248646CB029A7439C3A38F452D7D_0000`. It is from the pre-fix build.
  - Now, on each press and release, the client drops handles with no spec through `UADAbilitySystemComponent::RemoveInputTagBindingForAbility`, which was added to `tools/symbols.txt`.
  - Run c73h logged `InputTag.Skill.Ult: handle 151 has no spec (removed on the host), unbound` and did not crash.

### Step 3: proof

Scenario: `tools/scenarios/c73-client-grenade.txt`.

**c73h** (deploy 21a8f0728b41b48e) and **c73i** (same build):
- Both had rc 0/0, ERROR/FATAL 0, WARN 0, no dumps and drive rc 0.
- The client's frag press (handle 151) activated the server copy, which ran `RemoveItemByDefinition` and `FireProjectile`. The relay sent 0, and the client count went from 1 to 0. The incendiary (h152) did the same.
- Client HUD: x1/x1 after the pickups (`client-c73h-after-pickup.png`), frag x0 after the throw (`client-c73h-after-frag.png`), and incendiary x0 after its throw.
- In the host shots (`host-c73i-incendiary-1..3.png`), the incendiary fire burns at the client's spot. The host took damage (HP 87, then 79), and the client burned (Health 100 to 96.5).
- **Open:** the client shots show no grenade and no explosion.
  - The client's local activation runs (`InternalTryActivateAbility(handle 152) = 1`), but it never reaches `FireProjectile` or `RemoveItem`. Only the server copy spawns the projectile.
  - The projectile is a pooled `ADProjectile` with RemoteRole 0, so it does not replicate.
  - Proposed follow-up: a host relay of the grenade spawn (class, transform, velocity) that the client spawns as a cosmetic projectile, in the same shape as card 039's gib relay.

**c73j**, host-first (`c73-host-diag.txt`, deploy c0f9bbf98c498277):
- rc 0/0, ERROR/FATAL 0, WARN 0, no dumps, drive rc 0.
- The host's own grenades are unchanged: 2 `RemoveItemByDefinition` calls and 3 `FireProjectile` lines, the same as c73e, which ran before the step 2 change.

**c73k**, the client scenario with `UBWCOOP_C73_LOG` unset:
- rc 0/0, ERROR/FATAL 0, WARN 0, no dumps, drive rc 0.
- `hooks: card 073 grenade log on` is absent from both logs, so the log detours are not installed.
- Only the fix's own lines appear: the relay, the grant bind, `SourceItem` and the stale unbind. The HUD count again went from 1 to 0 for both grenades.

**Checks:**
- logsum for c73a through c73k: ERROR/FATAL 0 in every run.
  - The c73a WARN is the pre-step-0 AmmoSet drop.
  - The c73b WARNs are card 044's fallback.
  - The c73d WARN is a scenario goto timeout, which was fixed in the scenario.
  - c73e through c73k have no WARNs.
- fnlen: `functions 1327, over 60: 0`.

### Limits

- Grenades held across a client death and respawn were not tested. The step 0 seed covers ammunition only, so the new pawn's grenade count comes from the next relayed `GrenadeItem` text (the next pickup or throw).
- The client's explosion visual is missing. See Open above.

## Senior review (2026-11-05 05:44): accepted

- I checked c73b, c73h, c73i, c73j and c73k myself.
  - Each of c73h to c73k ends with `scenario passed`, and logsum gives ERROR/FATAL 0 and WARN 0 on both sides.
  - c73b's two WARNs are card 044's known `CompleteDeath` fallback.
- Builds: c73j and c73k ran on the deployed DLL c0f9bbf98c498277 (05:33:22). c73h and c73i ran on 21a8f0728b41b48e, the build before it, so c73k is the one that proves the client path on the final build.
- Crash: the newest crash folder is the 05:22 one from c73g, which ran on the pre-fix build. There is no dump from later runs.
- fnlen: 1327 functions, 0 over 60 lines.
- Step 0: the c73b client logs `AmmoSet ... (held 0 ms)` and `(held 1109 ms)`, and there is no dropped WARN.
- c73k client:
  - `GrenadeItem ... Frag_C 1: our count 0 to 1`, then `0: our count 1 to 0`, and the same for the incendiary.
  - No `card 073 grenade log on` line on either side.
- c73h client: `handle 151 has no spec (removed on the host), unbound`.
- The `client-c73h-after-frag` HUD shows one grenade type at x1 and the other at x0.
- Accepted:
  - the absolute count for the item relay;
  - binding to the replicated spec, with `SourceItem` set to our own item;
  - the stale-handle unbind. It guards the game's own unchecked `FindAbilitySpecFromHandle` in `AbilityInputTagPressed`.
- Follow-ups:
  - The client sees no grenade or explosion, because the pooled `ADProjectile` does not replicate. This is card 074, a cosmetic spawn relay.
  - Grenades held across a client respawn were not tested. Card 074 covers it.
