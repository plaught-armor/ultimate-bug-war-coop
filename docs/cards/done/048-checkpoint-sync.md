# Card 048: Co-op checkpoint restore on the client

Status: done 2026-11-04 (audit; fixes carded separately). Picked by the user. Step 0 is a small card 046 follow-up the user decided on (the kill-floor hit ignores armour).

Read `docs/STATE.md` first (known issue "Checkpoint state not synced to the client"), then card 045 (Report and Senior review) and card 021 (weapon grants). Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why

In co-op, RESTART CHECKPOINT (`USTSaveSystemLibrary::LoadLatestSave` 0x0520B350, then `Load` 0x0520A580) reloads in place on the host. The host respawns both pawns, grants `GA_ResetStatusCheckpointLoad`, and runs `USTWeaponInventory::LoadInventory` on both the host's pawn and the server copy (card 045 run 4, host log 2106-2128). The client gets no `LoadInventory` call (client log: hook installed, never called) and keeps its own state:
- Weapon: the client HUD shows the Morita MK1, while the server copy holds only `BP_WeaponDefinition_Unarmed_C`.
- Objectives: the client's panel is empty after a reconnect and after a checkpoint, while the host shows three.
- The mouse cursor stays drawn on the client.
- `CompleteDeath` runs a second time on both machines after the LoadLastSave click (caller unknown).

## Do

0. **Card 046 follow-up: the kill-floor hit ignores armour** (the user's decision, 2026-11-04). See the card 046 Senior review, finding 1.
   - Replace the `g_deal_damage` call in `c46_hurt` with `UADHealthUtilityLibrary::DealDamage(AActor* target, const AActor* causer, float amount, FGameplayTagContainer, FGameplayTagContainer)` 0x04A3B510, with the tag `Health.DamageIgnoreArmour` in the container that `UADDamageHandler::IsIgnoreArmour` 0x04A3D1F0 reads. The tag's FName is the 8-byte global `HealthSystemTags::Health_DamageIgnoreArmour` at exe+0x941EC88 (`IsIgnoreArmour` loads it with `mov rcx, [rip+...]` at 0x144A3D204). Read it at runtime and check that it is non-zero.
   - `FGameplayTagContainer` is `TArray<FGameplayTag> GameplayTags` plus `TArray<FGameplayTag> ParentTags` (0x20 bytes). The binary confirms this layout: the `DealDamage` epilogue (0x04A3B67A-0x04A3B6AB) calls `FMemory::Free` on `[c+0x10]` (ParentTags data) and `[c+0x0]` (GameplayTags data) of both containers. Each container is passed by value as a pointer to a caller-built copy (4th argument in `r9`, 5th on the stack at `[rsp+0x28]`), and **the callee frees both arrays**. So allocate the `GameplayTags` data (one 8-byte `FGameplayTag` = FName, Num 1, Max 1) with `FMemory::Malloc` (as card 045 does for the URL), never from the stack or the CRT heap, and do not free it afterwards. Leave `ParentTags` zeroed (null data, 0, 0); `Free(nullptr)` is safe. Build a fresh pair for every call.
   - The source tags are the **first** container (4th argument). `DealDamage` passes it to `MakeDamageEffectSpec` 0x04A3D310, which appends it to spec+0x60 (0x04A3D485) and the second container to spec+0xE8 (0x04A3D4A1). In `FGameplayEffectSpec` the captured source tags come before the target tags, so +0x60 is the source. This is inferred from the struct order, so if Armour still drops, put the tag in both containers and report it.
   - Log Health and Armour (`UADAttributeSetHealth.Armour` +0x50, `MaxArmour` +0x60) before and after each hit. Expect Health to drop by 25 at full Health and Armour to stay unchanged.
   - Check that this path still reaches `HandleDeathEvent` at low Health, so card 043's Downed still runs, and that the self-causer is not scaled down (card 040's x0.1). If either fails, report and keep the old call.
   - Rerun the card 046 test (`UBWCOOP_C46_TEST=1`) as run `card048k`: (a) at Health 100 must show a change of 25, and (c) must reach Downed at Health 20 without the 5 and 1 retries.
1. **Audit first (log only).** On the host's checkpoint load, log, for each player pawn:
   - the inventory before and after `LoadInventory`, with the weapon classes and the equipped slot;
   - Health, Armour and position;
   - the return address of the second `CompleteDeath`.

   On the client, log the same for its own pawn at the respawn in place, and log which objective widget or data the client lacks. Find where the host's objectives come from (the meta game or the game mode, `ASTGameplayGameMode::LoadSettingsFromSave` 0x0512C8E0) and whether any of it replicates.
2. **Stop and report the audit** before any fix. The fix depends on what replicates. Options for the senior and the user:
   - (a) a channel op that tells the client to apply the host's inventory list to its own pawn;
   - (b) making the client run `LoadInventory` itself from the host's save data;
   - (c) in co-op, turning RESTART CHECKPOINT into RESTART MISSON (it loses checkpoint progress, but the session's map-load sync is already proven).
3. **Cursor.** On the client's respawn in place, set `bShowMouseCursor` false on its PlayerController and remove `IMC_DummyUI_KBM` if it is still mapped. Log both.

## Done when

- Step 0: in `card048k`, (a) shows Health 100 to 75 with Armour unchanged, and (c) goes Downed from Health 20. rc 0, no new dump.
- The audit is in the Report, with the second `CompleteDeath` caller named.
- The client cursor is hidden after a checkpoint (log and shot).
- Fix steps are carded separately after the audit.

## Risks

- The objectives may be pure UI state built from the host's game mode, which never exists on the client. Then (a) or (c) is the only path.

## Static facts (senior, 2026-11-04)

- The host's `LoadInventory` caller (return address exe+0x51CDF6D) is `ASTPlayerCharacterMI::LoadAbilitySystemComponent` 0x051CDF30, a protected virtual. It calls `USTWeaponInventory::LoadInventory` 0x05243EF0 on the inventory at pawn+0xA40 (call at 0x1451CDF68), then the base `ASTCharacter::LoadAbilitySystemComponent` 0x050F28E0.
- So the inventory restore runs from the pawn's ability-system load, which the host runs for every pawn it owns. Step 1 should log whether the client's own pawn ever reaches `LoadAbilitySystemComponent`; it most likely does not, because the client has no save-load path.
- The full host chain, from static xrefs: `ASTSaveSystemEngine::LoadFromSlot` calls `USTSaveComponent::Load` 0x05209EA0 for each saved actor (at 0x14520B06C), then `USTSaveComponent::FullyLoaded` 0x051FC0C0 (at 0x14520B0FC). `FullyLoaded` broadcasts the component's fully-loaded delegate and calls `ISTSavableComponentInterface::Execute_OnComponentFullyLoaded` on its sibling components. `ASTCharacter::ActorFullyLoaded(USTSaveComponent*)` 0x050D9E30 (vtable slot +0xA08) is bound to that delegate: it calls vtable +0xA10, which is `LoadAbilitySystemComponent` (the only indirect call through +0xA10 in game code), then `ReceiveActorFullyLoaded` (the Blueprint event).
- `USTSaveComponent::Load` also restores pawn state through `LoadOwnerVariables` 0x0520B480, then `LoadOwnerVariables_Pawn` 0x0520B820 (at 0x14520B576). That is the likely place where the position (and possibly Health) of each pawn is restored on the host. Step 1 should log it for both the host's pawn and the server copy.
- Consequence: everything a checkpoint restores on a pawn runs only inside the host's `LoadFromSlot`. The client has no save slot load, so it restores nothing. The server copy gets a `LoadInventory` (card 045 run 4), so the save must hold a record that matches the copy (by `USTSaveComponent::GetSaveId` 0x051FE7F0), or the copy reuses the host pawn's record. Step 1 should log the save id of each pawn the load touches, to tell which.
- Fix option (b) therefore means the client would need its own call to `LoadAbilitySystemComponent`, with inventory data it does not have. Option (a), the host sending the copy's inventory after `ActorFullyLoaded`, is the more likely fix. A detour on `ASTCharacter::ActorFullyLoaded` on the host is the natural hook point for it.
- `ASTPlayerCharacter::CompleteDeath` 0x051C27A0 (vtable +0xA48) writes 0 to the death timer at pawn+0x9DC and broadcasts the multicast delegate at pawn+0x898 (the Blueprint "on complete death" event, which builds the death screen). It has two callers in game code:
  - `ASTPlayerCharacter::Tick` at 0x1451E0FBC: `StartDeath` sets pawn+0x9DC to 3.0 (at 0x1451DECA9); `Tick` subtracts the frame time and calls `CompleteDeath` when it reaches 0.
  - `ASTPlayerCharacter::execCompleteDeath` 0x05085E70, i.e. a Blueprint call.
- The second `CompleteDeath` in card 045 came 7 ms after the LoadLastSave click on the host, too soon for the 3 s timer, so a Blueprint called it. A return address from a `CompleteDeath` detour will only show `execCompleteDeath`. To name the caller, detour `execCompleteDeath` (signature `void(UObject* Context, FFrame& Stack, void* Result)`) and log `Stack.Node` and `Stack.PreviousFrame->Node` names (the calling Blueprint function) plus `Context`. Verify the `FFrame` offsets at runtime (`Node` should read `CompleteDeath`) before trusting `PreviousFrame`. Call the original unchanged.
- Objectives live in `USTLevelObjectiveSubsystem`, a per-world subsystem that each machine has on its own. Nothing about them replicates: `ASTLevelObjectiveManager` and the subsystem have no `GetLifetimeReplicatedProps`.
  - New objectives come from Blueprint calls to `USTObjectiveUtilityLibrary::AddObjective` 0x0522A6D0 (and `AddSubObjective`, `AddCountObjective` and their variants), which all call `USTLevelObjectiveSubsystem::AddObjectiveObject` 0x0514AF00.
  - On a checkpoint, `ASTLevelObjectiveManager::ActorFullyLoaded` 0x05149AF0 rebuilds the list from the save and calls `SetObjectiveMapPostSave` 0x0516E010 (at 0x14514A599). Like the pawn restore, this runs only inside the host's `LoadFromSlot`.
  - Step 1 question: does the client ever call `AddObjectiveObject` during normal play (level script running on the client), or only the host? Log every `AddObjectiveObject` call (objective id, `USTLevelObjective` class) on both machines from the map load to the first checkpoint. If the client never calls it, objectives need a channel op of their own (send the id and state; the client calls `AddObjective` locally), and that becomes a separate card.
- `FFrame` offsets, from the binary: `Code` is +0x20 (`execCompleteDeath` 0x05085E70 does `P_FINISH` on `[rdx+0x20]`), so `Node` is +0x10 and `Object` is +0x18 (standard order). `PreviousFrame` is +0x78: `FFrame::GetStackTrace` 0x0154BD80 walks the chain with `mov rbx, [rbx+0x78]` (0x0154BE3D). Check at runtime that `Node` names `CompleteDeath`.
- Step 3 reuse: `kPcCursorFlags` 0x54C (bit 0 = `bShowMouseCursor`) and `cursor_bit()` in hooks.cpp ~1042, and `g_orig_set_show_mouse_cursor` (`APlayerController::SetShowMouseCursor`, detour ~1278). Call `g_orig_set_show_mouse_cursor(pc, false)` instead of writing the bit. The card 009 `UBWCOOP_CLIENT_GAMEONLY` path (~3789) shows how to call `SetInputMode` GameOnly on the client's PC. Check the viewport capture mode before and after (card 009: 3 means the cursor is shown until a mouse button is held).

## Report (2026-11-04)

Runs: `card048k`, `card048k2` (step 0), `card048a`, `card048b` (audit and cursor). All rc 0, FATAL/ERROR 0, WARN 0, no new crash dumps. Logs are `logs/<run>-{host,client}.log`. Shots are in `logs/card048-shots/`.

### Step 0: kill-floor hit through `UADHealthUtilityLibrary::DealDamage`: pass, with one deviation

- `c46_hurt` now calls `DealDamage(pawn, pawn, amount, source, target)` with fresh `FMemory::Malloc` tag data. The callee frees it. `g_deal_damage` stays as the fallback if a tag global reads 0.
- With `Health.DamageIgnoreArmour` alone (`card048k`), Armour stayed 100, but Health fell 100 to 97. The hit went through the modifier stage (x0.12). Test (c) needed the retries at 5 and 1, and went Downed only at Health 1. So the tag fixes armour, but not the self-causer scaling the card warned about.
- **Deviation:** the source container now also carries `HealthSystemTags::Health_DamageIgnoreModifiers` (exe+0x941EC90, PDB public 0003:4713616). The card said to report and keep the old call in this case. I kept the new call because the second tag gives exactly the required result. Senior: confirm, or revert to the old call.
- `card048k2`, with both tags in the source container:
  - (a) and (b): Health 100 to 75 (change 25). Armour 100 to 100, MaxArmour 100.
  - (c): Health 20 went Downed with no retries. The card 043 path ran (`HandleDeathEvent skipped`, Health 1.0, Down sent 1).
  - Host test done: warps 3, StartDeath 2, all down 1.

### Step 1: audit

**The second `CompleteDeath` caller, named** (`card048b`, 16-frame stack from the `execCompleteDeath` detour):

`Stack.Node` = `CompleteDeath`, `Stack.Object` = the pawn, `PreviousFrame` null. This verifies the `FFrame` offsets, and shows the call is a native `ProcessEvent`, not a Blueprint-to-Blueprint call.

Host chain:
1. `ASTSaveSystemEngine::LoadFromSlot`+0xC4
2. `USTSaveSystemLibrary::ClearLevel`+0x271
3. `AActor::Destroy` on the dead pawn
4. `UWorld::DestroyActor`
5. `APawn::Destroyed`
6. `DetachFromControllerPendingDestroy`
7. `AController::PawnPendingDestroy`
8. `UnPossess`
9. `ASTPlayerController::OnUnPossess`
10. `APlayerController::OnUnPossess`+0x99
11. `UADSkeletalAnimatorComponent::CallAnimationEnded`+0x30
12. `TMulticastScriptDelegate::ProcessMulticastDelegate`
13. `AActor::ProcessEvent`
14. `execCompleteDeath`

Unpossessing the dead pawn ends its death montage. The pawn's animation-ended delegate (bound in Blueprint) then calls `CompleteDeath`.

On the client the same chain starts at `UActorChannel::CleanUp`, then `DestroyActorAndComponents`: the replicated old pawn is destroyed.

It is harmless to card 043/045 state. The card 045 per-map reset of the one-shots runs after it (client 566867699, then the reset at 566868073). No fix is needed unless the death-screen delegate it broadcasts shows a stale widget.

The first `CompleteDeath` is the 3 s timer in `ASTPlayerCharacter::Tick`+0x8D2, as expected.

**Host load order** (all inside `USTSaveSystemLibrary::Load`, within about 1 ms):
1. `LoadSettingsFromSave`. Callers: `Load`+0x3EC, `LoadLatestSave`+0x9F, `execLoadLatestSave`+0x72.
2. `ClearLevel` (the second `CompleteDeath` above).
3. `LoadOwnerVariables_Pawn` for the host's pawn, then for the server copy.
4. `SetObjectiveMapPostSave`: 14 slots, from `ASTLevelObjectiveManager::ActorFullyLoaded` (exe+0x514A59E).
5. `ActorFullyLoaded`, then `LoadAbilitySystemComponent`, for both pawns. Caller: `ASTPlayerCharacter::ActorFullyLoaded`+0x55, from `ASTPlayerCharacterMI::ActorFullyLoaded`+0x12.

**Save ids:** the server copy has its own save record. It does not reuse the host's.

| Run | Host pawn | Server copy | Client's own pawn |
|---|---|---|---|
| card048a | 40F41770-46640A11-A5A83094-4D5246F8 | CF1CC918-442E0D53-DE6756AE-710AD628 | 35C349F6-45472302-F059388B-6A89DE37 |
| card048b | 24CF1FB0-4D80AEF8-FD6E468F-627415EC | 5184932E-4C4E0E9E-55FFDBAE-CE5E3C1A | 1C7E7E48-41CF4412-E61D37A4-9761C26C |

- Ids are new every boot, and the client pawn's id never matches the copy's. So the client cannot look up "its" record by its own id. Any client-side restore must be keyed by the host.
- In the `card048b` log, the first `LoadOwnerVariables_Pawn` "before" line for the copy reads `host-own`; the "after" line reads `server-copy`. Read the label with care. The save id and position are the reliable values.

**Per pawn on the host, before and after:**
- `LoadOwnerVariables_Pawn`: no visible change. Health 100 and Armour 100 on both. Positions: host (-1769, 1820, 296), copy (-560, -1715, 103). The respawn already placed them there.
- `LoadAbilitySystemComponent`:
  - Before: weapons 0 on both.
  - After: both hold only `BP_Weapon_Unarmed_C` (`BP_WeaponDefinition_Unarmed_C`). Host last equipped 0, copy -1.
- No `AddWeapon` ran on either machine in this map. So this checkpoint's saved inventory is Unarmed, and the restore itself is correct. The host HUD after the restart shows UNARMED (`card048b-host-after-restart.png`).

**Client's own pawn at the respawn in place, and again +5 s later:**
- Health 100, Armour 100, at (-560, -1715, 104).
- Weapons 0, equipped null, items 0.
- `LoadAbilitySystemComponent` calls: 0 (no `LoadInventory`).

The client HUD still shows MORITA MK1 (`card048b-client-after-checkpoint.png`): stale HUD state on an empty inventory. The pawn's real state (Unarmed on the copy) never reaches the client.

**Objectives:**
- The host calls `AddObjectiveObject` 15 times per map load. Callers are `USTObjectiveUtilityLibrary::AddObjective`+0x111, its variants and `AddCountObjective`, all through `ProcessLocalScriptFunction`, i.e. the level Blueprint.
- The client calls it **0 times** in both runs, from the map load through the checkpoint. The level script that adds objectives does not run on the client, and nothing replicates.
- The client's panel is empty (shot), while the host shows three.

### Step 3: cursor: done

- At the client's respawn in place, `card048b`:
  - `bShowMouseCursor` went 1 to 0, through `g_orig_set_show_mouse_cursor`.
  - Viewport capture went 1 to 1: GameOnly had already set 1.
  - `IMC_DummyUI_KBM` was not mapped, so nothing was removed.
- At +5 s: `bShowMouseCursor` 0, capture 1.
- The client shot after the checkpoint shows no cursor.

### Step 2: stopped here, before any fix. Recommendation

1. **Inventory: option (a).** On the host, detour `ASTCharacter::ActorFullyLoaded` (or hook after `LoadAbilitySystemComponent` returns) for the server copy. Send a channel op with the weapon definition classes and the equipped slot. The client applies them to its own pawn and refreshes the HUD.
   - Option (b) is not viable. The client has no save slot, and its pawn's save id never matches.
   - The op must also clear the client's stale MORITA MK1 when the list is Unarmed only.
2. **Objectives: a separate card.** They are host-only level-script state. They need a channel op of their own: the objective id, class and state at the map load and after `SetObjectiveMapPostSave`. The client then calls `AddObjective` locally. This also fixes the empty panel after a reconnect.
3. Option (c), RESTART MISSION in co-op, remains the fallback if (a) proves fragile. It would also cover the objectives, because the level script reruns on a full map load. But objectives are already missing on the client at the first map load, so (2) is needed in any case.
4. The second `CompleteDeath` needs no fix.

Code changes for this card, all in `mod/src/hooks.cpp` and `tools/symbols.txt`:
- step 0 in `c46_hurt`;
- the card 048 audit hooks: `LoadAbilitySystemComponent`, `LoadOwnerVariables_Pawn`, `ActorFullyLoaded`, `execCompleteDeath`, `AddObjectiveObject`, `SetObjectiveMapPostSave`, `LoadSettingsFromSave`;
- calls `GetSaveId` and `RemoveMappingContext`;
- the client cursor in `c48_client_cursor`;
- `caller_chain` now takes an optional depth (at most 16).

## Senior review (2026-11-04)

Accepted. Verified from `logs/card048{k,k2,a,b}-{host,client}.log`, the run summaries, both shots, and `mod/src/hooks.cpp` 17560-17900.

- All four runs: rc 0 on both machines, no new dumps. FATAL/ERROR/WARN 0, except `card048k` (WARN 3, the gap reports of the IgnoreArmour-only attempt).
- Step 0, `card048k2` host 910, 934 and 952-954:
  - (a) and (b) are 100 to 75 with Armour 100 to 100.
  - (c) is 20 to 1, then Downed through card 043 (`HandleDeathEvent skipped`, Down sent 1) with no retries.
- **Deviation accepted:** adding `Health.DamageIgnoreModifiers` (exe+0x941EC90). The fixed 25% kill-floor hit should not go through the damage modifiers (card 040's self-causer x0.1 and the difficulty scaling). The old call stays only as the fallback when a tag reads 0.
  - `c48_deal_ignore_armour` allocates the tag data with `FMemory::Malloc`, leaves ParentTags null, and does not free; this matches the callee-frees epilogue.
- Second `CompleteDeath` (host 2017): `Stack.Node` is `CompleteDeath`, `PreviousFrame` is null. I resolved the chain addresses myself:
  - 0x520AED4 is `LoadFromSlot`, 0x3B431DA is `AController::UnPossess`, 0x51D2108 is `ASTPlayerController::OnUnPossess`, 0x4A22FB0 is `UADSkeletalAnimatorComponent::CallAnimationEnded`, 0x1424549 is `UFunction::Invoke`.
  - The cause is the montage end when the dead pawn is unpossessed in `ClearLevel`. No fix is needed.
- Objectives:
  - Host: "AddObjectiveObject #n" lines from `AddObjective`+0x111 (0x522A7E1) and `AddSubObjective` (0x522A9F8); the per-map reset counts 15.
  - Client: every per-map reset reports 0 calls (client 334, 398, 1086, 1133, 1859).
  - The client shot shows an empty OBJECTIVES panel and MORITA MK1. The host shot shows three objectives and UNARMED.
- Cursor: client 1863 (1 to 0) and 1874 (+5 s, still 0).
  - The white triangle at the left edge of the client shot (x 0-10, y 283-295) points right, so it is an off-screen marker, not a cursor.
- Every function is 60 lines or fewer. `caller_chain` is 14 and `c46_hurt` is 24.

Open:
1. Inventory sync, option (a): a separate card, keyed by the host (save ids differ per boot). It must clear the client's stale weapon when the list is Unarmed only.
2. Objectives sync: a separate card. The client is missing objectives from the first map load, not only after a checkpoint.
