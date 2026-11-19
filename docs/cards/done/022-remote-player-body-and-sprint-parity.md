# Card 022: Each player sees the other's body; sprint speed matches between players

Read `docs/STATE.md` first: the "Mod code rules" section, the "Runtime facts" line about `tools/run_pair.sh`, rows 018 to 022, the reports of cards 020 and 021, and the known issue about the shadow blob (it has the static lead). Then read card 019's report and its senior review. Card 020 found the client's copy of the host pawn has its movement set at 0 (MaxWalkSpeed 0); include it in the part A audit. Repo root: `/path/to/ultimate-bug-war-coop`. Run logs: `logs/card019f-{host,client}.log`. Symbols: `tools/symbols.txt`.

## Why

Client movement, look, jump and sprint now work. Neither player can see the other's body, only a shadow blob on the ground. That is the most visible co-op gap left.

Static lead (senior, from the disassembly; not yet checked at runtime):

- `ASTPlayerCharacter::ChangeCameraType` (0x051C1C30) is called from `PostInitializeComponents` (tail jump) and from `ActorFullyLoaded`, on every instance of the pawn, local or remote.
- With `CameraType` (+0x9E0) 0, first person, it does the following:
  - deactivates the third-person camera (+0x828) and activates `CameraComponent` (+0x818);
  - shows `FirstPersonMesh` (+0x848, `USkeletalMeshComponent`) with `SetVisibility(true, true)`;
  - hides `ACharacter::Mesh` (+0x328) with `SetVisibility(false, true)`;
  - sets bit 0x2 of the dword at +0x2B0.
- Camera type 1 does the reverse.
- The blob is `BlobShadowDecalComponent`.
- So on each machine the other player's pawn shows only its first-person arms (probably owner-only, so invisible to others) and the blob.

Sprint parity: the host's own pawn sprints at multiplier 1.1 (peak about 962). The client reaches about 1270, and its server copy flips between 1.1 and 1.5 during one sprint (card 019). The host HUD shows "UNARMED" while the client's shows MORITA MK1. The multiplier may depend on the equipped weapon; that is unproven.

## Do

### Part A: the remote body (required)

1. **Audit (log only, once per pawn per map, on both sides).** For every player pawn, log:
   - `IsLocallyControlled`, LocalRole and RemoteRole;
   - `CameraType`;
   - for `Mesh` (+0x328) and `FirstPersonMesh` (+0x848): the skeletal mesh asset name, the anim instance class, the visible flag, `bHiddenInGame`, `bOwnerNoSee` and `bOnlyOwnerSee`;
   - whether `Mesh` holds a full-body asset. If it is empty or a stub, say so, stop part A, and report what the game would need.
2. **Fix.** On each machine, once the remote pawn (not locally controlled) has run `ChangeCameraType`:
   - show `Mesh` the way the game's own camera type 1 branch does;
   - hide `FirstPersonMesh` for that pawn;
   - remove the blob decal only if the body casts its own shadow.

   Leave the local pawn as it is. Use the game's functions (`USceneComponent::SetVisibility`, or `ChangeCameraType` itself only if it touches nothing else, such as camera activation or the +0x2B0 bit, that would break the remote pawn). Say which you used and why. It must survive respawn and map change (key it per pawn, reset per map).
3. **Animation.** Check that the shown body animates: it is not in T-pose, and it walks when the other player moves. Use a screenshot of each window plus the logged anim instance. If it does not animate, report why (for example, the anim BP reads first-person-only data); do not fix it in this card.
4. **Verify.** One pair run with both windows visible. Take a screenshot from each instance while both pawns are on foot and in view of each other. Each player must see the other's body. Report the screenshot paths.

### Part B: sprint multiplier parity

5. Trace what `HandleSprintChange` (0x051272C0) uses to pick its multiplier: its caller's argument, an attribute, a tag, or the equipped weapon. Log the input on host-own and on the server copy.
6. If the cause is the host's missing weapon ("UNARMED"), report that and stop; it becomes its own card. Otherwise, make the server copy use the same rule as the host's own pawn.

## Done when

- Part A: the screenshots show each player's body on the other's screen, and the audit lines and the fix line are logged once per remote pawn. Or part A is reported blocked, with the audit showing why.
- Part B: the multiplier's source is named, and the speeds match or the cause is carded.
- Movement, look, jump and sprint still work on the client in the final run.
- Clean build (`/W4 /WX`), 84 exports, KERNEL32-only imports. No new crash dumps.

## Runs

- Build: `python3 -I tools/gen_proxy_exports.py && python3 -I tools/pdb_rva.py gen && cmake --build build --clean-first`.
- Run: `DEPLOY=1 HOST_EXTRA_ENV=UBWCOOP_AUTODROPSHIP=1 CLIENT_EXTRA_ENV="UBWCOOP_AUTOINPUT=1 UBWCOOP_AUTOINPUT_DELAY_MS=40000" DONE_PATTERN='autoinput: script done' tools/run_pair.sh <tag> 240`. Never pass `-nosound`; it crashes the host. Audio goes to a null sink by default.
- The pawns spawn at different spots. If they are not in view of each other, report the positions, and turn the client toward the host with the existing look steps.

## Risks

- Showing a mesh with no anim instance or no asset can draw a T-pose or nothing. That is what the audit is for.
- `ChangeCameraType` also switches cameras and delegates. Calling it on a remote pawn may move a camera the local player uses, so prefer direct `SetVisibility` on the two meshes.
- The weapon meshes may be attached to `FirstPersonMesh`, so hiding it may hide the remote weapon. Report what is attached; a third-person weapon is a later card.

## Senior note (static, 2026-11-03): NPC troopers are 8-direction sprites

The pak index shows that NPC troopers are billboard sprites. Each animation frame and view direction is its own material instance (`MI_<Type>Trooper_<State>_<F|FL|L|BL|B|BR|R|FR>_<n>`), played by `USTSpriteAnimatorComponent` or `USTSpriteVariantAnimatorComponent`. The states are Idle, Walk, Run, Death, Corpse, StandUp, Flinch, Reload and more (`MoritaTrooper` has the fullest set). The player may have no third-person body at all, which would explain the blob.

In the part A audit, also log on a remote pawn:
- `Mesh`'s skeletal mesh asset (or null) and its anim instance class;
- every component whose class name contains `Sprite`.

If the player has no usable body, a likely fix is to attach a sprite animator with the `MoritaTrooper` frame set to remote pawns, driven by their speed and facing. Report what you find; do not build that in this card.

## Static lead (senior, 2026-11-03)

These facts change what part A is likely to find. Check each one at run time before relying on it.

- **No player body art in the pak.** The pak index has no player third-person skeletal mesh (no `SK_`/`ABP_` human player assets) and no player sprite frames. `SM_PlayerHuman` sits next to `SCL_PlayerHuman*`, so it is probably a Logic Driver state machine, not a mesh. `BP_Player_HumanAnimationData` exists, but its type is unknown. Expect `Mesh` (+0x328) to be empty or a stub. If it is, report that in step 1 and stop part A there, as step 1 already says.
- **`ChangeCameraType` (0x051C1C30).** Both camera branches call `USceneComponent::SetVisibility` on `FirstPersonMesh` (+0x848) and `Mesh` (+0x328). They toggle the cameras at +0x818 and +0x828, activating through vtable +0x2C8, and then write the `CameraType` byte at +0x9E0. The function never touches `ThirdPersonRenderer` (+0x830).
- **`ThirdPersonRenderer` (+0x830).** It is a reflected property of `ASTPlayerCharacter`. The native constructor (0x051B8140) only zeroes it at 0x051B927B and creates no subobject there. In step 1, log its value and class on a live pawn; a blueprint may create it. If it is a `USTSpriteRenderComponent`, it is the intended remote body.
- **Likely follow-up if `Mesh` is empty.** Give remote pawns an NPC trooper sprite body: a `USTSpriteRenderComponent` with a `STSpriteVariantAnimatorComponent` driving `BP_CharacterAnimations_FemaleMoritaTrooper` or `_ControllerTrooper` (`ADSpriteAnimationData`; see card 027 report and review). Card 027's dump gives the frame sets and timings for that work, and the same body serves the downed visual (design doc section 3).

## Amendment (senior, 2026-11-03, after sending): part A is audit only

User decision: players have no third-person body (a typical single-player FPS), so the remote body will be an NPC trooper sprite driven by what the player is doing. That is card 028. In this card:

- Do part A step 1 (audit, log only), including `ThirdPersonRenderer` +0x830 and its class.
- Skip part A steps 2 to 4. Do not try to show `Mesh`.
- Do part B in full.

## Report

Runs `card022` (client sprint only) and `card022b` (host sprint script on too, `UBWCOOP_HOST_SPRINT=1`). Logs: `logs/card022{,b}-{host,client}.log`. Both ended by done (126 s), host and client rc 0, FATAL/ERROR 0, no new crash dumps. Clean `/W4 /WX` build; the run script's deploy check passed (84 exports, KERNEL32 only). Client movement, look, jump and sprint ran in both runs (autoinput steps 0 to 13, client sprint to MaxWalkSpeed 1302).

Code (all in `mod/src/hooks.cpp`, log only, no behavior change):
- `c22_step` / `c22_audit`: once per pawn per map, on both sides, 5 s after the pawn is first seen on foot. Pawns are keyed by object index (at most 8) and reset in `c22_arm_map` from `client_diag_load_map_return`. The audit reads properties by reflected name and checks each offset against the static one.
- Part B: `detour_invoke` also records every native call made from a frame whose object's class FName is the GA_Sprint class (16-entry ring, FNames and return bytes only, no pointers kept). `detour_sprint_change` calls `c22_sprint_dump`, which logs the ring, the ability's GA_Sprint_C and USTSprintAbility properties (maps printed pair by pair) and the avatar's equipped weapon. Capped at 6 dumps per side per map.

### Part A: audit (step 1), stopped there as amended

| Pawn | Mesh +0x328 | FirstPersonMesh +0x848 | ThirdPersonRenderer +0x830 | CameraType +0x9E0 |
|---|---|---|---|---|
| host-own (L3/R1, locally controlled) | null | SKM_Arms, ABP_Arms_Skeleton_AnimBlueprint_C | null | 0 |
| server copy on host (L3/R2) | CharacterMesh0, **no asset** (SkinnedAsset null, AnimClass null, no anim instance), bVisible true, AttachParent null | same as above | null | 0 |
| client-own (L2/R3, locally controlled) | null | same as above | null | 0 |
| host pawn on client (L1/R3, controller null) | CharacterMesh0, no asset, no anim instance | same as above | null | 0 |

- All reflected offsets match the static ones: Mesh 0x328, FirstPersonMesh 0x848, ThirdPersonRenderer 0x830 (PropertyClass SceneComponent) and CameraType 0x9E0.
- **Mesh is a stub.** Remote copies have an empty `SkeletalMeshComponent` with no asset. Locally controlled pawns have no Mesh at all; the property is null. No player full body exists to show.
- **ThirdPersonRenderer is null on every pawn, on both sides.** No blueprint fills it.
- **No component class contains "Sprite"** (checked over all owned object properties and two levels of AttachChildren).
- FirstPersonMesh: bVisible true, bHiddenInGame false, bOwnerNoSee false, bOnlyOwnerSee false; attached to Camera. So the remote arms are not hidden by owner flags.
- The pawn's other components: CharacterAnimationComponent = `PlayerAnimationComponent` (`ADSkeletalAnimatorComponent`), BlobShadow (`STCharacterBlobShadow`), ThirdPersonSpringArm and ThirdPersonCamera, HitBox, AllyNavCollider, inventories, ASC, health and others. The full list is in the logs.
- The client's copy of the host pawn now has MaxWalkSpeed 700, not the 0 card 020 saw.

What the game would need for a remote body: a body component the pawn does not have, such as an NPC-style `USTSpriteRenderComponent` and sprite animator attached to remote pawns (card 028). `ThirdPersonRenderer` (a SceneComponent slot, always null) is a natural place to store it.

### Part B: sprint multiplier

Source: `HandleSprintChange`'s value is the GA_Sprint_C blueprint's argument. The blueprint takes it from `USTSprintAbility.SprintDirectionMultiplierMap` (+0x3C8, enum to float), keyed by the movement direction:

| key | multiplier |
|---|---|
| 0 | 1.0 |
| 1 (forward) | 1.5 |
| 2 | 1.1 |
| 3 | 1.1 |

The direction comes from the blueprint function `IsMovingForward`: `GetActorForwardVector` against `PawnMovementComponent.GetLastInputVector`. When `GetLastInputVector` is zero, the ability ends (`K2_EndAbility`); this is the card 019 cause. With input along the forward vector, the value is 1.5.

During a sprint, `ExecuteUbergraph_GA_Sprint` also calls `GameplayTagAssetInterface.HasAnyMatchingGameplayTags`:
- When it returns true, the blueprint re-sends 1.1 even while the pawn moves forward.
- When it returns false, the blueprint calls `CurveFloat.GetFloatValue` on `SprintStaminaSpeed_Curve` and `UpdateActiveGameplayEffectSetByCallerMagnitude`, then 1.5.

The tag container argument was not logged, so which tag causes the 1.1 is not known.

Not the weapon. The host-own pawn is `BP_Weapon_Unarmed_C` in both runs. In `card022b` it sprinted at 1.5 (dropped to 1.1 once on the tag check, then back to 1.5), so "UNARMED" does not set the multiplier. The client-own pawn has a null equipped weapon on its side and also gets 1.5.

Speeds in `card022b` match:

| Pawn | MaxWalkSpeed peak |
|---|---|
| host-own | 1312 |
| server copy | 1302 |
| client-own | 1302 |

All three got 1.5 for forward sprint. Same rule on all three, so no fix was made (step 6).

The card 019 baseline (host 1.1 for a whole sprint, `card019b`) fits the tag branch or a non-forward input, not a different rule. The server copy's 1.1/1.5 flips in card 019 fit the direction key flipping with the fed input vector. In this run, the server copy's single HandleSprintChange was 1.5 and did not flip.

Open (not carded here): which gameplay tag drives the 1.1 branch. Logging the `HasAnyMatchingGameplayTags` container (the frame's parameters) would name it.

## Senior review (2026-11-03)

Accepted. Verified against `logs/card022{,b}-{host,client}.log` and the build:

- **Part A.** On the host, the server copy's `Mesh` (+0x328) is `CharacterMesh0` with no skeletal asset, no anim class and no anim instance. On the client, the locally controlled pawn's `Mesh` is null. `FirstPersonMesh` (+0x848) is `SKM_Arms` with `ABP_Arms_Skeleton_AnimBlueprint_C`. `ThirdPersonRenderer` (+0x830) is null on every pawn on both sides. All reflected offsets match the static ones. The player has no body to show, so card 028 must build one.
- **Part B.** `SprintDirectionMultiplierMap` (+0x3C8) is {0: 1.0, 1: 1.5, 2: 1.1, 3: 1.1} and keyed by sprint direction. Weapon is not the cause. In run card022b all three pawns reached 1.5, so there is no parity bug to fix. Earlier 1.1 readings were sideways or backward sprint, or the unlogged tag branch.
- **Build.** `build/dwmapi.dll` (09:26) imports only KERNEL32 and has 0x54 = 84 exports. No FATAL or `Error:` lines in the four logs.

Carried forward: the `HasAnyMatchingGameplayTags` branch in `GA_Sprint_C` that forces 1.1 is unidentified. That is low priority because parity held.
