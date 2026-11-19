# Card 021: Both players are armed, and the client's shots exist on the server and aim from the client's view

Read `docs/STATE.md` first: the "Mod code rules" section, the "Runtime facts" line about `tools/run_pair.sh`, and rows 018 to 022. Then read card 020's report and its senior review. Repo root: `/path/to/ultimate-bug-war-coop`. Run logs: `logs/card020c-{host,client}.log`. Symbols: `tools/symbols.txt`.

## Why

Card 020 could not test firing: in the AUTODROPSHIP flow both players hold only `BP_Weapon_Unarmed_C`, and the client's own weapon inventory is empty (equipped null, weapons 0) while the server copy holds the unarmed weapon. Shooting is the core of the game, so this is the next gap. Card 020 also found two things that will decide whether the client's shots land:

- `GetTargetLocation_Implementation` deprojects through PlayerController 0. On the server that is the host's controller, so the server may aim the client's shots from the host's camera.
- The server copy's `CameraComponent` stays at pitch 0. The interactor traces from it, and weapon traces may too.

## Do

1. **Find how the loadout is granted.** Find the code path that gives a player their mission weapons (the mission brief close, a game mode or player state hook, or a loadout data asset). Name it from disassembly or a hook. The AUTODROPSHIP path calls `RequestGameUnpause`, which likely skips the brief close that grants the loadout; check whether a host Enter press at the brief is the real close. Then make the loadout reach **both** pawns on the server, by running that path for the client's pawn too if it only runs for player 0.
2. **The client's weapon inventory.** After step 1, check the client's own pawn: weapon inventory (+0xA40) count, equipped weapon, ammo. If it stays empty while the server copy is armed, find why (the inventory array, the weapon actor or its owner does not replicate) and fix it, or list it with the evidence.
3. **Fire from both pawns.** Same script on both (card 020's shared parity script): fire for 2 s at the ground in front, then 2 s at a point 30 px up. On the host, for each shot of each pawn, log:
   - that the shot ran on the server (the weapon fire function, `FireProjectile`, or the damage-apply path);
   - its origin and direction;
   - which view it came from: the pawn's own control rotation, the camera component, or PlayerController 0. Settle the `GetTargetLocation_Implementation` question here.
   If the client's shots come from the host's view, fix it so they use the client pawn's own controller, or list it as a proposed card with the disassembly evidence.
4. **Server copy camera pitch.** Drive the server copy's `CameraComponent` rotation from the controller's control rotation (or `RemoteViewPitch`) before the interactor and any camera-based trace reads it. Show before and after rows: the camera's world pitch on the server copy follows the client's look.
5. **Client health.** `ADAttributeSetHealth` has no `CPF_Net`. Damage the client's pawn on the server once (the host shooting it works, since `HealthAndDamageSystem.FriendlyFire.Enabled` is set; fall damage also works). Log Health on the server copy and on the client's own pawn. If the client's value stays at 100, find whether the game sends health to the client another way (a replicated property, a client RPC, `OnHealthValuesUpdated`). Fix it if the fix follows an existing pattern; otherwise list it as a proposed card.
6. **`InputPressed`.** Once weapons exist, check ADS (RightMouseButton) and hold-to-fire on the server copy: does `InputPressed` clear on release, and does anything that waits on release (`WaitInputRelease`) end? Fix only if it breaks one of these.

## Done when

- Both players hold a real weapon in the final run, on the server and on the client's own pawn (class, ammo).
- For each pawn, the server logs at least one shot with its origin and direction, and the client's shot origin is at the client's pawn, aimed along the client's view (not the host's).
- The server copy's camera pitch follows the client's look.
- Client health after damage is shown, and either follows the server or is a proposed card with the cause.
- Movement, look, jump and sprint still work on the client.
- Clean build (`/W4 /WX`), 84 exports (or say why the count changed), KERNEL32-only imports. No new crash dumps.

## Runs

- Build: `python3 -I tools/gen_proxy_exports.py && python3 -I tools/pdb_rva.py gen && cmake --build build --clean-first`.
- Run: `DEPLOY=1 HOST_EXTRA_ENV="UBWCOOP_AUTODROPSHIP=1 UBWCOOP_HOST_SPRINT=1" CLIENT_EXTRA_ENV="UBWCOOP_AUTOINPUT=1 UBWCOOP_AUTOINPUT_DELAY_MS=40000" DONE_PATTERN='autoinput: script done' tools/run_pair.sh <tag> 240`. If the loadout needs a different start flow, change the env vars and say so in the report. Never pass `-nosound`; it crashes the host. Cutscenes advance on Enter.

## Risks

- Granting the loadout twice to player 0 may duplicate weapons. Grant only to pawns that lack it, and log the inventory count before and after.
- Firing may hit the other player or alter the mission. Aim away from the other pawn except in step 5.
- The step 4 fix runs every frame on the server for remote pawns. Restrict it to LocalRole 3 / RemoteRole 2 pawns, as card 019's input mirror does, and make sure it does not touch the host's own camera.
- Hooks taking UE by-value structs receive a hidden pointer. Check each new detour signature against the disassembly.
- Do not start the body visibility work; that is card 022.

## Senior static lead for step 5 (added 2026-11-03, after the card was sent)

From the PDB and the exe, not yet tested at runtime:

- **The game replicates no health natively.** `UADHealthComponent` and `UADAttributeSetHealth` have no `OnRep_` function, no client RPC and no `GetLifetimeReplicatedProps`. No game class (`UST*`, `UAD*`, `AST*`, `AAD*`) defines `GetLifetimeReplicatedProps` or any `OnRep_`; all 54 `GetLifetimeReplicatedProps` in the PDB are engine or GAS classes. Expect the client's Health to stay at 100 after damage.
- **Cheap fix candidate: set `CPF_Net` (0x20) on the set's attributes.** `UE::Net::bAutoRegisterReplicatedProperties` (`Net.AutoRegisterReplicatedProperties`, data RVA 0x08FB8258) is 1 in this build. With it on, `FRepLayout::InitFromClass` (0x040FC2A0) registers a `CPF_Net` property that the class's `GetLifetimeReplicatedProps` did not register, so no hook on that function is needed.
- **Timing.** `UClass::SetUpRuntimeReplicationData` (0x01431400) builds the class's replicated property list once and caches it. Its callers are `FRepLayout::InitFromClass`, `FClassNetCacheMgr::GetClassNetCache`, `FNetPropertyConditionManager::FindOrCreatePropertyTracker` and the Iris descriptor builder, so it runs on first network use of the class. The flags must be set on **both** host and client before any connection replicates any `ADAttributeSetHealth`, including in the lobby. Set them once at mod start, from `UADAttributeSetHealth::StaticClass()`, not when the mission pawn spawns.
- **Which attributes:** Health, MaxHealth, Armour and MaxArmour only. Leave the meta attributes (Damage, HealingHealth, HealingArmour) alone; they are server-side scratch values.
- **No RepNotify.** The set has no `UFunction`, so the client's value changes silently: the client's `OnHealthAttributeChanged` delegates will not fire. Check whether the HUD polls `GetCurrentHealth` or binds the delegate. If it binds, the client must notice the change and broadcast it (for example through `UAbilitySystemComponent::SetBaseAttributeValueFromReplication`).
- **Card 020's re-init.** With `CPF_Net` set, card 020's client re-init skips this set by design, because it only runs on a set with no `CPF_Net` attribute. Log that it skipped.

This counts as a cheap fix in the sense of step 5 if it works. If any of the above is wrong at runtime, say which point failed.

## Senior static lead for step 3 (added 2026-11-03, after the card was sent)

From disassembly, not yet tested at runtime. **On the server, the client's shots will aim at the host's crosshair.**

The fire path, `USTWeaponFireAbility::Fire` (0x05236320), takes no target data from the client. It computes the aim itself, on whichever machine runs it:

1. Through the owner interface (`[ActorInfo+0xA0]`, interface slot +0x10) it calls `GetMuzzleTransform`.
2. Then `USTWeaponUtilityLibrary::ApplyDynamicZeroThresholdAdjustment` (0x0522EDD0) turns the muzzle toward the target, using two sources:
   - **interface slot +0x18 = `GetTargetLocation`.** For a player, `AADWeapon::GetTargetLocation_Implementation` (0x04A2BEC0) forwards to the owner through `Execute_GetTargetLocation`. That reaches `ASTPlayerCharacterMI::GetTargetLocation_Implementation` (0x051C9D70), which:
     - takes `UGameplayStatics::GetPlayerCameraManager(this, 0)`;
     - calls `USTCommonUtilityLibrary::GetCenterScreenWorldPostionDirection` (0x0523A190), which deprojects the viewport center through `GetPlayerController(world, 0)`;
     - line-traces 10000 units from there, along the direction of the component at camera manager +0x1B8 (`ForwardVector` if null).
   - **a second deprojection.** On one branch (0x0522F2AC), `ApplyDynamicZeroThresholdAdjustment` itself does `GetPlayerController(world, 0)`, then `GetViewportSize` and `DeprojectScreenPositionToWorld`, and builds the muzzle rotation from the result.
3. Then `UADAbilityTask_FireProjectile::FireProjectile` (called at 0x05236707).

On the server, player 0 is the host, so all three sources are the host's view: the camera manager, the center deprojection and the zero-threshold deprojection. `GetHUDRaycastStartAndDirection` (`ASTCharacter`, 0x050EA160) is a `jmp` to the same center helper, so anything that uses it has the same problem.

Fix direction, for you to verify:

- Detour `ASTPlayerCharacterMI::GetTargetLocation_Implementation`. For a pawn that is not locally controlled on the server (LocalRole 3, RemoteRole 2, as in card 019's input mirror), compute the target from that pawn's own view instead:
  - start at the camera location (after the step 4 pitch fix), or at `GetActorEyesViewPoint`;
  - aim along the controller's control rotation;
  - trace 10000 units with the same trace profile.
  Leave locally controlled pawns on the original code.
- The second deprojection has no pawn argument. Either check whether that branch runs at all for the client's shots (log which branch is taken at 0x0522F2A6), or set a per-call context in a pre-hook on `Fire` and handle `DeprojectScreenPositionToWorld` under it.
- **Do not make `GetPlayerController(world, 0)` return the client's controller.** On the server that controller has no local player or viewport, so `DeprojectScreenPositionToWorld` would fail.

### Addendum to the step 3 lead: view source and trace details (static, 2026-11-03)

- **Where the client's view comes from on the server.** Use `APlayerController::GetPlayerViewPoint` (0x040483A0) on the client's server-side controller (`pawn->Controller`). It returns the camera manager's camera cache (PC+0x360 is `PlayerCameraManager`, and the cache is used when its timestamp at PCM+0x1530 is above 0). On the server, the engine RPC `ServerUpdateCamera_Implementation` (0x0405DCB0) fills that cache from the client's own camera. Engine RPCs work, so this should be the client's real camera location and rotation, quantized, with no dependence on the step 4 pitch fix. Log it next to the client's own view once to confirm. If the cache timestamp stays 0, fall back to `GetActorEyesViewPoint` plus control rotation.
- **What the original computes**, so a replacement matches it exactly:
  - start = deprojected screen center;
  - direction = forward of the camera manager's transform component;
  - end = start + direction × 10000;
  - `UWorld::LineTraceSingleByProfile` (0x03A36B30) with profile `PlayerProjectile`;
  - `FCollisionQueryParams` built by the constructor at 0x0399ECF0, with tag `TargetLocationQuery`, bTraceComplex true and the pawn ignored;
  - it returns the hit result's `Location` (FHitResult+0x10) when bBlockingHit is set (bit 0 of FHitResult+0xAD), else `end`.
  - The hit result is a 0x100-byte buffer: memset to 0, then the original sets a few fields (0x051C9DB2..0x051C9DFF: Time 1.0 at +0x04, -1 at +0xA4, and a 16-byte handle at +0xC8 with -1 at its +0x08). The query params own two TArrays, which the original frees afterwards (0x051C9F2C..0x051C9F49). Copy that sequence rather than inventing one.
- **The detour receives the interface pointer, not the pawn.** The function is reached through the `IADWeaponAbilityOwnerInterface` subobject: the original does `lea rsi, [rcx-0xA30]` to get the pawn. Apply the same adjustment.

### Addendum: when the second deprojection runs (static, 2026-11-03)

`USTWeaponUtilityLibrary::ApplyDynamicZeroThresholdAdjustment(const AADWeapon* weapon, FTransform& muzzle)` (0x0522EDD0) finds the owner through `weapon->OwningInventory` (+0x2D8), then the component's owner at +0xA0. It reads two attributes from the weapon ASC: `DynamicZeroCloseTargetDistanceThreshold` and `DynamicZeroObstacleTraceRadius`. Then:

- **Either attribute about 0** (absolute value at most 1e-8): it skips straight to the normal path at 0x0522F35B, which turns the muzzle toward `GetTargetLocation`.
- **Muzzle-to-target distance at or below the threshold**, or **a `BoxTraceSingleByProfile` from the muzzle toward the target blocks**: it takes the deprojection branch (0x0522F2AC). That branch gets `GetPlayerController(world, 0)` and, if it is an `ASTPlayerController`, deprojects the viewport center. It then **overwrites the muzzle translation** (`muzzle+0x20`) with the deprojected camera position, and the rotation with the deprojected direction.
- **Otherwise:** the normal path.

So the branch runs for close targets and whenever something sits between the muzzle and the target. Both are common at bug range. On the server, a client shot in either case would spawn **at the host's camera position**, not just aim along the host's view. That is worse than the `GetTargetLocation` problem; check for it in the step 3 logs (projectile spawn location against the client pawn's location).

Fix shape:

1. Detour `ApplyDynamicZeroThresholdAdjustment`. Read the owner pawn as the original does (`weapon+0x2D8`, then `+0xA0`, null-checked) and store it in a thread-local. Call the original, then clear the thread-local.
2. Detour `APlayerController::DeprojectScreenPositionToWorld` (0x0403FD50, signature `bool (const APlayerController*, float x, float y, FVector& worldLoc, FVector& worldDir)`). When the thread-local holds a pawn that is not locally controlled, return that pawn's controller `GetPlayerViewPoint` location and its rotation's forward vector, and return true. Otherwise call the original.

The host's own shots never see the thread-local set to a remote pawn, so their behavior is unchanged.

### Addendum to the step 5 lead: making a replicated health value reach the client's HUD (static, 2026-11-03)

With `CPF_Net` on `Health`, the client's attribute set gets the new value silently. GAS's `SetBaseAttributeValueFromReplication` is not in this binary (stripped or inlined), so nothing on the client fires. The game's own handler is small enough to call directly:

- `UADHealthComponent::HealthAttributeChanged(const FOnAttributeChangeData&)` (0x04A3CD20) calls `RefreshState` (0x04A3E480), then broadcasts the dynamic delegate `OnHealthAttributeChanged` (component+0xB8) with (NewValue, OldValue). It reads only `data+0x38` (NewValue, float) and `data+0x3C` (OldValue, float), nothing else.
- `RefreshState` reads `HealthAttributeSet` (component+0x130): `Health` current value at set+0x30+0xC, and `OverkillThreshold` at set+0xC0+0xC. Set layout: Health +0x30, MaxHealth +0x40, Armour +0x50, MaxArmour +0x60, Damage +0x70, DamageTakenModifier +0x80, DamageTakenMultiplier +0x90, HealingHealth +0xA0, HealingArmour +0xB0, OverkillThreshold +0xC0 (each a 0x10-byte `FGameplayAttributeData`: BaseValue +0x8, CurrentValue +0xC). Replicate `OverkillThreshold` too, or the client computes the overkill flag against 0. It sets `IsDead` (component+0x148) and `IsOverkilled` (+0x149), and the ASC (+0x128) loose tags `Health.Dead` and `Health.Overkilled`.
- The sibling handlers have the same shape: `ArmourAttributeChanged` 0x04A3B200, `MaxHealthAttributeChanged` 0x04A3D530, `MaxArmourAttributeChanged` 0x04A3D500, with delegates at +0xC8, +0xD8 and +0xE8.

Client-side fix shape: on the client, once per tick for each locally relevant pawn, compare the replicated current values with the last values seen. On a change, build a zeroed 0x48-byte buffer with NewValue at +0x38 and OldValue at +0x3C, and call the matching handler on the pawn's health component. The HUD binding on `OnHealthAttributeChanged` should then update. `OnDeath` (+0xF8) is broadcast only from the server's fatal-damage path; client-side death presentation is a separate step. Check against the HUD before building this: if the HUD polls `GetCurrentHealth` (0x04A3BCA0) instead of binding the delegate, replication alone already shows the value.

## Addendum to the step 3 lead: why the client's shot may never start (senior, static, 2026-11-03)

From `logs/card021g-*.log`: the client's `IA_PrimaryFire` reaches `Triggered` (55 to 117 per row), but the client's `InternalTryActivateAbility` hook only ever sees `GA_Sprint_C` (handle 30) and `GA_HumanJump_C` (handle 31), and the host logs no `server-copy FireProjectile`. The host's own shots show the fire ability's avatar is the weapon, so it lives on the weapon's own ASC (`AADWeapon` +0x2C0, `GetWeaponASC` 0x03650320), not on the pawn's.

Fire input reaches abilities through `UADAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag&)` (0x04A0EA00; called from blueprint through its exec thunk). It looks the tag up in a tag-to-spec-handle map on that ASC (hash data near +0x1288, count compared at +0x1294), and for each handle found with `FindAbilitySpecFromHandle` it calls `UAbilitySystemComponent::TryActivateAbility`, or the spec input-pressed virtual (vtable +0x8E8) when the ability is already active. `AbilityInputTagReleased` is at 0x04A0EBE0.

So on the client, one of these holds: the blueprint never calls `AbilityInputTagPressed` on the client's weapon ASC, or that ASC's map has no entry for the fire tag (no spec granted on the client's copy of the weapon). One hook on 0x04A0EA00 that logs the ASC's owner actor, the tag, the map count and the handles found separates the two. Even when it fires, the weapon actor does not replicate (the `AADWeapon` constructor never sets `bReplicates`), so a client-side activation cannot reach the server through the weapon ASC's own RPCs. The server copy is broken too: in card021g its `IA_PrimaryFire` reaches `Triggered` (57 to 120 per row), yet it never fires. So put the same hook on the host and check the server copy's weapon ASC first. The server copy has authority and already sees the input, so making it fire is the fix; a client-side activation is only prediction on top of that.

Where the map gets filled: `UADAbilitySystemComponent::BindInputTagToAbilitySpec(tag, handle)` (0x04A0F870), called from `UADGASBasicsUtilityLibrary::AddAbilityToActor` (0x04A0F00A call site), `InitFeaturesFromDefinition` (0x04A10966) and the `execAddFeaturesToActor` thunk. Binds can also be queued: `UADAbilitySystemComponent::TickComponent` (0x04A18C20) flushes a pending array (data +0x12B0, count +0x12B8, 12-byte entries of tag plus handle) into the map, but only when the ability-list scope lock count (+0x560) is 0. `UADAbilitySystemComponent::GetShouldTick` (0x04A104A0) returns true while that count is above 0, so a queued bind normally lands on the next tick; it can stay stuck only if the component's tick was never registered or something keeps it disabled. Log the pending count (+0x12B8) and whether the weapon ASC's tick is enabled, alongside the map count.

## Report

Final run: `card021l` (logs `logs/card021l-{host,client}.log`). It ended on `autoinput: script done` after 124 s, with host rc 0 and client rc 0, FATAL/ERROR 0 and WARN 0 on both machines, and no new crash dumps. The build is clean under `/W4 /WX` with `--clean-first`, and `DEPLOY=1` passed the 84-export / KERNEL32-only check (dll hash 878a339329c71961). The supporting runs are `card021g` (fire), `card021h` (camera), `card021i` (health-set dump) and `card021j` (health fix).

**Run flow change.** Both env strings add `UBWCOOP_GRANT_WEAPON=MoritaMk1`; everything else matches the card's command. The client's autoinput script gained the step 3 fire steps (LeftMouseButton held 2 s at the ground, MouseY up, held 2 s again) and an ADS press (RightMouseButton).

**Summary.** Steps 1, 2, 4 and 5 work. Step 3 is not fixed: the client fires only on its own machine, and the server never runs those shots. A proposed card follows below. Step 6 is partly verified.

### Step 1: loadout

- The loadout comes from `ASTPlayerCharacter::AssignStartingInventories`, which reads the GI chapter and level. On the host it logs chapter 0 level 0 and grants only `Weapons.Unarmed` (C1M1's level data). On the client it logs chapter -1 level -1 and grants nothing.
- C1M1 therefore starts unarmed, and the brief close is not the missing grant. Instead, `UBWCOOP_GRANT_WEAPON=<row>` gives each pawn that lacks the weapon the row's definition, through `GiveWeaponByClass(slot -1, equip true)`. The host grants to its own pawn and to server copies; the client grants to its own pawn.
- `GetWeaponDefinitionFromTag` returns the definition's default object, so the class comes from its `ClassPrivate`. The grant runs once a second, at most 6 times per map.
- `card021l` grant lines:
  - host-own: weapons 1 -> 2, equipped `BP_Weapon_MoritaMk1_C`.
  - server-copy: weapons 1 -> 2, equipped `BP_Weapon_MoritaMk1_C`.
  - client-own (client): weapons 0 -> 1, equipped `BP_Weapon_MoritaMk1_C`.
- Ammo was not logged. Both pawns fire full 2 s bursts.
- `card021d` crashed (two dumps). The grant reached a frontend pawn that has no MI weapon inventory. The grant now runs only for `BP_PlayerCharacterMI_C` pawns (`is_a(pawn, g_player_mi_class())`), and no run after the fix crashed.

### Step 2: the client's inventory

The client's own pawn holds its own `BP_Weapon_MoritaMk1_C` (+0xA40 count 1, equipped). `AADWeapon` never sets `bReplicates`, so the client's weapon is a separate, client-local actor and not a copy of the server's.

### Step 3: fire

- The fire ability lives on the weapon actor's own ASC. `InputTag.Fire` goes through `AbilityInputTagPressed` on both ASCs:
  - The pawn ASC's map (Num 8) has no Fire entry.
  - The weapon ASC's map (Num 5) maps Fire to handle 2.
  - Pending binds are 0 on both, on both machines.
- So the blueprint does press the weapon ASC, and its map has the fire spec. Both of the senior lead's alternatives are ruled out.
- **Client:** it activates handle 22 `BP_WeaponFireAbility_MoritaMk1_C` locally (owner `BP_Weapon_MoritaMk1_C`), about every 600 ms while held, and stops on release.
  - Shots start at the client's own pawn, about (-860, 4336, 92), and follow the client's view: ground phase P about -3.6, up phase P 0.4 / -1.1 / -0.3.
  - The client log's `card020` FireProjectile lines label these shots `host-own`. That is a label quirk: the side comes from the non-replicated weapon actor, which is Authority locally.
- **Host:**
  - Every server-side shot is host-own (41 in `card021l`, origin (723.5, 2810.3, ~102)). Ground phase P -4.2 / -3.97; up phase P 4.46.
  - The host logs no `AbilityInputTagPressed` for the server copy.
  - The client's ability requests to the server are only pawn-ASC abilities: GA_HumanJump (31), GA_Sprint (30), GA_Slide (43), GA_Weapon_RecoilAbility (39) and GA_HumanMelee (42), each "activated, spec present".
- **Correction to the senior's last addendum.** The `srv[N]` enhanced-input rows count the host's own local input (the card 012/013 control), not the server copy's. The server copy's `IA_PrimaryFire` never fires on the host, and the client does activate fire (handle 22).
- **The `GetTargetLocation_Implementation` question.** It could not be tested on the server, because no client shot runs there. The host's own shots use the host's view, as expected.
- **Proposed card: relay client fire to the server copy's weapon.** The client's weapon ASC has no network path. What reaches the server is the pawn-ASC `GA_Weapon_RecoilAbility` (handle 39) request, which the client sends per shot. Options:
  1. On the host, when a server copy's handle 39 activation arrives, call `AbilityInputTagPressed(InputTag.Fire)` on that pawn's server-side weapon ASC. Release it on the matching end or cancel.
  2. Add an explicit relay through an existing client-to-server RPC.

  Either way, the host-view problems in this card's step 3 addenda (`GetTargetLocation`, the deprojection in `ApplyDynamicZeroThresholdAdjustment`) must be fixed in the same card, or the relayed shots will aim from the host's camera. Count the handle 39 requests per burst first: the dedupe log shows only the first one.

### Step 4: server copy camera pitch (fixed)

- For server-copy pawns only (LocalRole 3 / RemoteRole 2), every tick sets the `CameraComponent` world rotation from the controller's control rotation, through `USceneComponent::SetWorldRotation` (0x03AF7C80, FRotator by hidden pointer).
- The cause: `UCameraComponent::GetCameraView` applies `bUsePawnControlRotation` only for a local controller.
- Rows from `card021j`; the host's own camera is not touched:
  - `ControlRotation P 4.20 Y 94.20; camera before P 0.00 Y 94.20, after P 4.20 Y 94.20`
  - `ControlRotation P 355.80 Y 94.20; camera before P 4.20 Y 94.20, after P -4.20 Y 94.20`
  - `ControlRotation P 0.00 Y 94.20; camera before P -4.20 Y 94.20, after P 0.00 Y 94.20`

### Step 5: client health (fixed)

- At engine init, both machines OR `CPF_Net` into the `UADAttributeSetHealth` properties Health, MaxHealth, Armour, MaxArmour and OverkillThreshold. The cached replication layout is built later, so the flags take effect.
  - The `card021: host CPF_Net set` lines on the client say "host" because they run before the session side is known.
- The damage is a test hit, not the damage pipeline. 30 s after the server copy first appears, the host writes Health base and current to minus 25 directly.
- Diagnosis (`card021i`): the client's pawn had two health sets.
  - `UADHealthComponent::HealthAttributeSet` (+0x130) pointed at a local set, which stayed at 100 and is not in the ASC's SpawnedAttributes.
  - The replicated set in SpawnedAttributes did go 100 -> 75.
- Fix: on the client-own pawn only, when SpawnedAttributes holds exactly one health set and it is not the component's, the component is pointed at it. On a later change, the client calls the game's own `UADHealthComponent::HealthAttributeChanged` (0x04A3CD20) with a 0x48-byte buffer (New +0x38, Old +0x3C), which runs `RefreshState` and the `OnHealthAttributeChanged` broadcast.
- `card021j` result:
  - host: `server-copy Health 100.0 -> 75.0`
  - client: `client-own health component ... now uses replicated set ...`, then `client-own Health 100.0 -> 75.0, HealthAttributeChanged called`
- The HUD itself was not checked on screen.
- Open: client-side death presentation (`OnDeath` is server-only), and the Armour and MaxHealth handlers.

### Step 6: InputPressed

- Hold-to-fire with release works on both machines: shots come about every 600 ms and stop at release.
- ADS (RightMouseButton) activates no ability on either machine. No hook covers the ADS path, so ADS is not verified.
- `WaitInputRelease` is not verified on the server copy, since the server copy never fires (step 3).

### Unchanged behavior

Move, look, jump and sprint on the client still work (`card021j`):

- The server copy's CMC rows show walking at 700 and falling (MovementMode 3, jump).
- `HandleSprintChange(1.500)` takes MaxWalkSpeed 700 -> 1050, with speed up to 1295.
- The look pitch follows (step 4 rows).
- These match `card021h`.

### Code

All in `mod/src/hooks.cpp`, plus `config.{h,cpp}` (`UBWCOOP_GRANT_WEAPON`), `autoinput.cpp` (fire and ADS steps) and `tools/symbols.txt`:

- `c21_grant_*`: loadout grant.
- `c21_sync_camera`: step 4.
- `c21_mark_health_net`, `c21_adopt_replicated_set`, `c21_watch_health`: step 5.
- `c21_test_hit`: test hit.
- `c21_log_tag_pressed`: logs input tag presses, skipping held axis tags (Move, MoveUp, Look, Walk).
- `c21_note_activation`: activation and server-request dedupe log.
- `c21_dump_health_sets`: diagnosis dump, capped at 10.

New symbols: `USceneComponent_SetWorldRotation`, `UADAttributeSetHealth_StaticClass`, `UADHealthComponent_HealthAttributeChanged`.

## Senior review (2026-11-03)

Accepted: steps 1, 2, 4 and 5 are done; step 3 moves to card 024; step 6 is partly done.

Checked against the logs, not the report:

- `card021l`: no `Fatal:` or `Error:` lines on either side. A plain `error` grep hits 84 host lines, but they are all `ServerCheckClientError` text in the srv/rot movement rows, not engine errors. No WARN lines.
- Grant lines match the report: server-copy and host-own weapons 1 -> 2, client-own 0 -> 1, all `BP_Weapon_MoritaMk1_C`.
- Fire: on both sides `AbilityInputTagPressed(InputTag.Fire)` reaches the weapon ASC (map Num 5, Fire -> handle 2, pending binds 0) and also the pawn ASC (Num 8, no Fire entry). FireProjectile counts are 42 per log.
- Health (`card021j`): host test hit 100 -> 75 on the server copy; the client's own pawn follows, 100 -> 75, with `HealthAttributeChanged called`.
- Camera (`card021j`): the three ControlRotation and camera rows match the report.
- Relay signal: the host logs the server copy's handle 39 (`GA_Weapon_RecoilAbility_C`) going to ActiveCount 1 with InputPressed 1, then ActiveCount 0 while InputPressed is still 1, 500 ms later.

The correction stands. The `srv[N]` rows are the host's own input, and my addendum misread them. The server copy never fires.

Notes carried forward:

- **Release signal.** InputPressed on handle 39 stays 1 after the activation ends, and card 020 saw it stay 1 after release. It cannot mark the release. Card 024 must find a real one.
- **Test hit only.** Health sync is proven for a direct attribute write. The damage pipeline is still unproven, and so is the HUD on screen.
- **Unverified.** ADS and `WaitInputRelease` remain unverified.
- **Log label.** The client's `card020` FireProjectile lines say `host-own` for the client's local weapon. Fix the label when that code is next touched.
