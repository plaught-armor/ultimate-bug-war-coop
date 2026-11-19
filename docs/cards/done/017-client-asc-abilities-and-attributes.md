# Card 017: The client pawn's ability system component has no abilities and default attributes: find where replication or client-side init breaks

Read `docs/STATE.md` first: the "Mod code rules" section, the "Runtime facts" line about `tools/run_pair.sh`, and rows 015 to 017. Then read card 016's report and its senior review. Repo root: `/path/to/ultimate-bug-war-coop`. Run logs: `logs/card016{a,b,c}-{host,client}.log`. Symbols: `tools/symbols.txt`.

## Why

Card 016 found that client look, jump and sprint are all blocked by the same component, the client pawn's `UADAbilitySystemComponent` (ASC):

- **Look:** IA_Look's action modifier `USTInputGASPlayerAttributeModifier` (exe+0x0512DF10) multiplies the look value by the attribute `CameraLookInputMultiplier`. The attribute is found on the client but is 0; on the host it is 1.
- **Jump and sprint:** `AbilityInputTagPressed` (exe+0x04A0EA00) looks the input tag up in the map at `ASC+0x1260`. On the client that map has Num 0, so no ability activates.

Both look like GAS state the client should have received and did not. In UE5, `ActivatableAbilities` and the attribute sets usually replicate only to the owning connection. So if the server-side copy of the client's pawn (or its ASC) is not owned by the client's PlayerController, the client gets neither. Card 008 changed client possession, so ownership is the first suspect. It is not the only one: the input-tag map and the attribute defaults may also be filled by a client-side init step (an `InitAbilityActorInfo` override, an ability-set grant, or an `OnRep` handler) that never ran on the client.

## Do

1. **Ownership, on the server.** For the server's copy of the client's pawn, log once per map:
   - the pawn's `Owner` and its Controller;
   - whether the Controller is the client's remote PlayerController and has a NetConnection;
   - the ASC's owner actor and avatar actor, and its replication mode.

   Log the same for the host's own pawn, for comparison.
2. **GAS state, both sides.** For the client pawn's ASC, log once per map, on the client and on the server's copy:
   - the `ActivatableAbilities` item count;
   - the spawned attribute set count and their class names;
   - the value of `CameraLookInputMultiplier`;
   - the input-tag map Num at `+0x1260`.

   Do the same for the host's own pawn ASC on the host; card 016 never measured the host map. Take the samples at three points: at possession, at landing (on foot), and 10 s after landing.
3. **Who fills the input-tag map.** Statically find the writers of `ASC+0x1260`: the function that adds a tag to handle entries, and its callers (for example `OnGiveAbility`, an ability-set grant, or an `OnRep_ActivateAbilities` path). Say which of them run on a client and what each waits on, with RVAs. Then hook the adder, log it with a cap, and show whether it runs on the server copy, on the client, or on neither.
4. **Who sets `CameraLookInputMultiplier`.** Find the attribute set class that owns it, its default value, and what sets it to 1 on the host: an init GameplayEffect, a data table, or a `PostInitialize`/`InitFromMetaDataTable` call. Say whether that value would reach the client by attribute replication or by a client-side step, and which step did not happen.
5. **Verdict.** Name the first broken link with evidence: ownership, ASC replication, ability or attribute replication, or a client-side init that never ran. Propose the smallest fix, and say which game function a normal client path would call. Apply it only if all of these hold:
   - it is one call to a game function that the real join path would make;
   - it runs on the side the game would run it on;
   - its effect is shown in a run: the input-tag map is filled, `CameraLookInputMultiplier` is 1, and ControlRotation changes under the scripted look.

   Otherwise report it and do not apply it.

## Done when

- The report has the ownership rows for both pawns and the GAS state rows for both sides at all three sample points.
- The report names the map writer and the attribute init path, with RVAs, and says where each one stops for the client.
- The report gives the verdict, and a fix that is either applied with its run evidence or only proposed.
- Clean build (`/W4 /WX`), 84 exports, KERNEL32-only imports. No new crash dumps in the runs.

## Runs

- Build: `python3 -I tools/gen_proxy_exports.py && python3 -I tools/pdb_rva.py gen && cmake --build build --clean-first`.
- Run: `DEPLOY=1 HOST_EXTRA_ENV=UBWCOOP_AUTODROPSHIP=1 CLIENT_EXTRA_ENV="UBWCOOP_AUTOINPUT=1 UBWCOOP_AUTOINPUT_DELAY_MS=40000" DONE_PATTERN='autoinput: script done' tools/run_pair.sh <tag> 240`. Never pass `-nosound`; it crashes the host. Audio goes to a null sink by default.
- Keep the card 016 SendInput steps. A fixed look must show up on the SendInput path when the client owns the foreground window.

## Risks

- Writing the attribute or filling the map by hand would hide the fault, not fix it. Do not do either.
- Granting abilities on the client alone would let the client and the server disagree about abilities. Any fix must keep the server as the authority for grants.

## Report

Runs:

- `card017a`: the host crashed (`EXCEPTION_ACCESS_VIOLATION` reading `0xA256` in `dwmapi+0x1BCFC`, from `InitFeaturesFromDefinition+0x1B8`). The bug was mine. The `AddAttributeSetFromData` detour took its `TSubclassOf<>` argument as a class pointer, but MSVC x64 passes it by hidden pointer to the caller's copy. Both detours (`AddAttributeSetFromData`, `InitFeaturesFromDefinition`) now take `void* const*` and dereference before calling the original. The run left the new dump `UECC-Windows-59B8443F48A629A4920E71BF814D37B6_0000`, which this mod bug caused, not the game.
- `card017b`: ended by done after 127 s. Host rc 0, client rc 0, FATAL/ERROR 0, no new crash dumps.
- `card017c`: the same run, plus the camera set pointer and name in every GAS row. It ended by done after 129 s. Host rc 0, client rc 0, FATAL/ERROR 0, no new crash dumps.
- Build: clean (`/W4 /WX`), 84 exports (37 named), KERNEL32-only imports.

All new code is log-only, under "Card 017" in `mod/src/hooks.cpp` and `tools/symbols.txt`. Nothing writes game state.

### 1. Server-side ownership (card017b, host)

| Pawn | Roles | Owner | Controller | Remote PC / NetConnection | ASC owner / avatar | ReplicationMode |
|---|---|---|---|---|---|---|
| slot 1, the client's pawn | LocalRole 3, RemoteRole 2 | `BP_PlayerController_C` | the same PC | PlayerController 1, NetConnection `IpConnection`, Player `IpConnection` | component owner, OwnerActor and AvatarActor are all the pawn | 2 (Mixed) |
| slot 0, the host's pawn | LocalRole 3, RemoteRole 1 | `BP_PlayerController_C` | the same PC | NetConnection null, Player `STLocalPlayer` | the pawn | 2 (Mixed) |

Ownership is correct. The client's pawn is owned by a remote PC with a live NetConnection, so owner-only replication can reach that client.

### 2. GAS state (card017b/c)

Offsets used:

- `UAbilitySystemComponent`: `ActivatableAbilities.Items` at +0x508 (Num +0x510); `SpawnedAttributes` at +0x10A8 (Num +0x10B0); the non-authoritative flag at +0x7C0.
- `UADAbilitySystemComponent`: the input-tag map at +0x1260 (Num +0x1268).

| Side, pawn | Point | owner-auth | ActivatableAbilities | Attribute sets | Camera set object | Look base/current | Map Num |
|---|---|---|---|---|---|---|---|
| host, its own pawn (slot 0) | after InitFeatures, possession, landing, landing+10 s | 1 | 16 | 5 | `0x68F5D780` at every point | 1.000 / 1.000 | 8 |
| host, the client's pawn (slot 1) | after InitFeatures, possession, landing, landing+10 s | 1 | 16 | 5 | `0x6A984540` at every point | 1.000 / 1.000 | 8 |
| client, its own pawn | after InitFeatures (LocalRole 1) | 0 | 0 | 5 | `0x66AF9370` | 1.000 / 1.000 | 0 |
| client, its own pawn | possession, landing, landing+10 s (LocalRole 2) | 0 | 16 (replicated) | 5 | **`0x66AF92E0`** | **0.000 / 0.000** | **0** |

The 5 sets are `STAttributeSet_Movement`, `STAttributeSet_PlayerCamera`, `STAttributeSet_CommonCharacterAttributes`, `ADAttributeSetHealth` and `STRangedWeaponOwnerAttributeSetBase`. The scope lock and the pending bind queue (+0x12B0) were 0 at every sample on both sides.

### 3. Input-tag map writer

- Writer: `UADAbilitySystemComponent::BindInputTagToAbilitySpec`, at 0x04A0F870. Its callers:
  - `UADGASBasicsUtilityLibrary::InitFeaturesFromDefinition` +0x426 (0x04A10966). This is the only caller on the join path.
  - `AddAbilityToActor` +0x18A.
  - `UADAbilitySystemComponent::TickComponent` +0x6E, which flushes the pending queue at +0x12B0.
  - the exec thunk.
- `InitFeaturesFromDefinition` (0x04A10540) is called from `ASTCharacter::PostInitializeComponents` +0x1A7, on every machine and for every pawn. In order, it calls:
  1. `AddAttributeSetFromData` per set (call at 0x04A106F3);
  2. the init GameplayEffects;
  3. `UAbilitySystemComponent::GiveAbility` (call at 0x04A10913);
  4. `BindInputTagToAbilitySpec(tag, returned handle)`.
- Host: GiveAbility returns real handles (1-16, 30-45, all from caller `exe+0x4A10918`). Bind (caller `exe+0x4A1096B`) fills the map from 0 to 8 tags: Walk, Jump (x2), Taunt, SwitchTech, Skill.Ability, HumanMelee, Crouch, Objectives.
- Client: the same code runs at PostInit with the pawn at LocalRole 1. GiveAbility gates on `IsOwnerActorAuthoritative` (0x04964840, flag +0x7C0) and returns handle -1. The hook shows `GiveAbility(Default__GA_Sprint_C) = handle -1, owner-authoritative 0, ActivatableAbilities 0, caller exe+0x4A10918`. Bind then runs 18 times with handle -1 (`BindInputTagToAbilitySpec(InputTag.Walk, handle -1): input tag map Num 0 -> 0`), and every tag is dropped.
- Next, the 16 specs replicate to the owner through `ActivatableAbilities` (`OnRep_ActivateAbilities` 0x0496BFB0, `OnGiveAbility` 0x04969EB0). No client path calls Bind for them afterwards. The map stays at Num 0 for the rest of the map.

### 4. CameraLookInputMultiplier init path

- Owner: `USTAttributeSet_PlayerCamera` (StaticClass 0x05035BE0). `CameraLookInputMultiplier` is an `FGameplayAttributeData` at +0x40 (Base +0x48, Current +0x4C).
  - Its PropertyFlags are 0x0020080000000015: no `CPF_Net` and no RepNotify, so the attribute does not replicate.
  - The constructor (InternalConstructor 0x05036E80) leaves it at 0. The default is 0.
- What sets it to 1 is the InitData asset, not a GameplayEffect:
  - `AddAttributeSetFromData(PlayerCameraData_C)` (0x04A0F060) calls `GetOrCreateAttributeSubobject` and then `USTAttributeSet_PlayerCamera::InitFromData` (0x051CC2C0).
  - `InitFromData` calls `UADAttributeSetBase::InitFromData` (0x04A10A60), a reflection name-match copy from the data asset with no authority gate.
  - It runs on both sides at PostInit. The client row reads `AddAttributeSetFromData(PlayerCameraData_C): camera sets 1, CameraLookInputMultiplier base 1.000 current 1.000`.
  - The camera set's `PostAttributeChange` (0x051D81E0) fired only for `CameraFieldOfView 90 -> 90` (caller `exe+0x49E0DB1`), never for the multiplier.
- Where the client loses it: between PostInit and possession, the camera set object on the client ASC changes from `0x66AF9370` to `0x66AF92E0`. The ASC and the pawn stay the same.
  - `SpawnedAttributes` is replicated (`OnRep_SpawnedAttributes` 0x0496CB90). It replaces the client's locally created and initialised set with the server's replicated subobject.
  - The replicated subobject is built by the constructor with the multiplier at 0. The attribute has no `CPF_Net`, so the server's 1.0 never arrives.
  - Nothing re-runs `InitFromData` on the new object.

### 5. Verdict and fix

Ownership and ASC replication are fine. The first broken links are two client-side init steps that run too early. Both run in `InitFeaturesFromDefinition` at PostInit, before the replicated GAS state arrives:

1. **Map:** Bind runs on the client before any spec exists there (GiveAbility returns -1 off-authority). When the specs replicate later, nothing binds them.
2. **Look:** the attribute init runs on a local set that replication then replaces with a fresh, non-replicated-attribute set whose value is 0.

Proposed fix (not applied):

- **Look:** on the client, once the replicated set is in place, call the game's own init again for the camera data. The call is `UADAbilitySystemComponent::AddAttributeSetFromData(asc, PlayerCameraData_C)` (0x04A0F060), triggered after `OnRep_SpawnedAttributes` (or at possession). `GetOrCreateAttributeSubobject` would return the replicated set, and `InitFromData` would copy 1.0 into it. That is one game call, client side, the same call the client already makes at PostInit. The other four sets probably reset the same way (their non-replicated attributes return to constructor defaults), so the faithful version re-runs every `AddAttributeSetFromData` entry of the definition.
- **Map:** on the client, after `ActivatableAbilities` replicates (`OnRep_ActivateAbilities` 0x0496BFB0, or per spec in `OnGiveAbility` 0x04969EB0), call `BindInputTagToAbilitySpec` for each definition entry, with the replicated spec's handle in place of -1. The handle can be found by ability class on the ASC. This is local input routing only: it grants nothing, and the server stays the authority for grants. It is one call per bound tag (18), not one call, and needs the definition's tag list.

Why it was not applied: the apply rule needs the map filled, the multiplier at 1, and ControlRotation changing, all from one game call. No single call does both halves. The look half alone would not satisfy the card's condition, so both halves stay proposals.

ControlRotation in card017c: Y stayed at 90.00 through all 28 rot rows. In this run, `EnhancedPlayerInput::InputKey` saw 0 MouseX/MouseY events (MouseX nonzero 0), so this run does not test the scripted look path itself. W (IA_Move) did reach the pawn.

Next card suggestion:

1. Apply the look half (re-run `AddAttributeSetFromData` for the definition's sets after `OnRep_SpawnedAttributes`) and check the multiplier reaches 1 on the replaced set.
2. Apply the map half (bind replicated handles after `OnRep_ActivateAbilities`).
3. Then rerun the card 016 SendInput look with the client in the foreground.

## Senior review (2026-11-02)

**Verdict: accepted.** Both breaks are shown in the logs, and the decision not to apply a fix follows the card's rule. Card 018 applies both halves.

### Checked
- `card017c-client.log`: every `GiveAbility(...)` at PostInit returns `handle -1, owner-authoritative 0`, and every `BindInputTagToAbilitySpec(..., handle -1)` leaves the map at `Num 0 -> 0`. At possession, landing and landing+10 s, the same ASC has `ActivatableAbilities 16`, so the specs do replicate and nothing binds them.
- `AddAttributeSetFromData(PlayerCameraData_C)` sets `CameraLookInputMultiplier` to 1.000 at PostInit; later the action modifier reads it as 0 (`in (1.4000, 0, 0) -> out (0.0000, 0, 0)`). This agrees with the set swap the report describes.
- card017b and card017c: FATAL/ERROR 0 and WARN 0 on both sides. The card017a host crash was the mod's own detour (a `TSubclassOf` passed by hidden pointer, read as a class pointer). Both detours now take `void* const*`.

### Corrections
- **card017c did not test the SendInput path.** The client logged `capture mode 0 lock mode 0` at the mouse step (card016a/b had 1/1), so the 237 to 239 sent events per step never became MouseX/MouseY in the game. The six `in (1.4000 ...)` modifier lines come from the scripted `InputKey` MouseX 20.0 step, so that path did run, which contradicts "InputKey saw 0 MouseX/MouseY". The rot rows count `EnhancedPlayerInput::InputKey` events, and the scripted step evidently does not show up in that counter. Card 018 must get capture mode 1 before it can judge SendInput look.
- **"Re-run every `AddAttributeSetFromData` entry" is not safe as written.** `InitFromData` copies every matching property from the data asset. On a set that also holds replicated attributes (health, for example), it would overwrite the server's replicated values on the client. If the value then does not change on the server, no new replication arrives to correct it. Re-run a set's init only if none of its attributes has `CPF_Net`, or copy only its non-replicated attributes.
