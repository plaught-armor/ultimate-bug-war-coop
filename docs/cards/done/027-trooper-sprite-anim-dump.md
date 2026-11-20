# Card 027: Dump the trooper sprite animation assets (male vs female stand-up)

Status: sent 2026-11-03.

Read `docs/STATE.md` first: the "Mod code rules" section, the "Runtime facts" line about `tools/run_pair.sh`, and row 025. Then read card 025's step 6 senior review. Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why

The pak index lists stand-up frames for `MoritaTrooper` only in the front direction (`MI_MoritaTrooper_Standup_F_01` to `_06`). `FemaleMoritaTrooper` has the same 6 frames in all 8 directions (`MI_FemaleMoritaTrooper_StandUp_<dir>_01` to `_06`). The user doubts that the two really differ, and the card 025 revive showed no stand-up at all. Which frames each trooper plays is decided by the blueprint data assets `BP_CharacterAnimations_MoritaTrooper` and `BP_CharacterAnimations_FemaleMoritaTrooper`. Their packages are Oodle-compressed and cannot be read on disk. Blueprint properties are reflected, so the running game can print them.

## Do

1. **Single player only.** Use `SOLO=1` and `UBWCOOP_AUTODROPSHIP` into C1M1, as in card 025. Add a test-only switch `UBWCOOP_DUMP_SPRITE_ANIMS=1`, off by default.
2. **Find the assets.** Once the mission has started, find `BP_CharacterAnimations_MoritaTrooper` and `BP_CharacterAnimations_FemaleMoritaTrooper`: either the loaded objects or their generated classes' CDOs (`UClass::ClassDefaultObject` +0x110). Also log which one the nearest `BP_AI_Trooper_Morita_C` actually references. Find the NPC property or component that points to it, and log the path you followed. If no female trooper is loaded in C1M1, say so and dump only the male.
3. **Walk the properties recursively.** Use the existing `kStructChildProperties` / `kFieldNext` / `kFieldName` / `kPropertyOffset` walker. Recurse into struct, array, map and set properties, and print object properties as the referenced object's name. The FProperty subclass offsets you need (inner property, struct pointer, map key and value) are not in the code yet. Find them statically and check each one at runtime against a known value before trusting it. One line per leaf, with the path: for example `StandUp.Directions[3].Frames[2] = MI_FemaleMoritaTrooper_StandUp_L_03`. Cap the output at about 2000 lines per asset, and write it to `logs/card027-<asset>.txt` rather than the main log.
4. **Answer, with excerpts from the dump:**
   - For each asset, which frames does the stand-up state (`StandUp`/`Standup`, or whatever the key is called) list, and in which directions?
   - Does the male asset fill its side and back directions with the front frames, with another state, with the female's frames, or leave them empty?
   - Do any other states differ between the two assets? Diff the two dumps by state and count.
   - Is there a frame rate or duration per state? If so, give the stand-up's length in seconds. This explains why the 0.2 s revive shot missed it.

## Done when

- Both dumps exist (or only the male's, with the reason), and step 4 is answered with dump lines.
- The switch is off by default, and nothing changes when it is unset.
- Clean build (`/W4 /WX`), 84 exports, KERNEL32-only imports. No new crash dumps. Every new function is at most 60 lines.

## Risks

- A raw read through an unchecked FProperty subclass offset can crash. Verify each offset on a known property first, and stop recursing when a check fails.

## Static lead (senior, 2026-11-03, added after sending)

The exe's reflection tables (PDB `NewProp_*` symbols) give the layout, so a generic recursive walker is probably not needed. NPC sprite animation data is most likely `USTNPCAnimationData` (confirm that `BP_CharacterAnimations_MoritaTrooper_C` derives from it):

- `USTNPCAnimationData`: `Animations` (TMap, key type unchecked, value `FSTNPCAnimationSequence`), `DefaultAnimation`, `DeathAnimations[]`, `FlinchAnimations[]`, `IdleAnimations[]`, `MoveAnimations[]`, `TauntAnimations[]`, `OverkillData`, `PFXSystems` (map), `TextureSize`.
- `FSTNPCAnimationSequence`: `Frames[]` (`FSTNPCAnimationFrame`), `UniformFrameTime`, `bUseUniformTime`, `SequenceEndBehaviour`, `bDontProcessBasePFX`.
- `FSTNPCAnimationFrame`: `Directions[]` (one material per view direction), `FrameTime`, `bDoAction`, `PFXs[]`.
- Rendering: `USTSpriteRenderComponent` has `DirectionalFrameMaterials[]`, `FrameMaterial` and `DisplayMaterial`.

So the male-versus-female answer is `Directions.Num` (and the material names) for each frame of the stand-up sequence, and the duration is `Frames.Num` times `UniformFrameTime`, or the sum of `FrameTime`. Still read the offsets from the live `FProperty` chain (`Offset_Internal`); do not hard-code them from this list. Also log how the render component handles a frame whose `Directions` has fewer than 8 entries, if that is visible from the code path.

## Report

Run: `SOLO=1 DEPLOY=1 HOST_EXTRA_ENV="UBWCOOP_AUTODROPSHIP=1 UBWCOOP_DUMP_SPRITE_ANIMS=1" DONE_PATTERN='card027: done' tools/run_pair.sh card027 300`. It ended on `card027: done` after 209 s with host rc 0, 0 FATAL/ERROR lines and no new crash dumps. Logs are in `logs/card027-host.log` and `logs/card027-run.out`.

Output files:
- `logs/card027-BP_CharacterAnimations_ControllerTrooper.txt` (1507 lines): the male asset.
- `logs/card027-BP_CharacterAnimations_FemaleMoritaTrooper.txt` (1523 lines): the female asset.

Both are under the 2000-line cap, with 0 layout refusals and 0 write failures.

### Which asset the male uses (correction to the card)

`BP_CharacterAnimations_MoritaTrooper` does not exist at runtime. `StaticFindFirstObject` returned null for both the plain name and the `_C` name. The nearest `BP_AI_Trooper_Morita_C` (3321 units away) reaches its asset through this path:

`.AnimatorComponent` (+0xA30, PropertyClass `ADAnimatorBaseComponent`) → `STSpriteVariantAnimatorComponent` → `.AnimationData` (+0x128, PropertyClass `ADSpriteAnimationData`) → **`BP_CharacterAnimations_ControllerTrooper`**

The asset lives at `/Game/Blueprints/Enemies/Humans/MoritaTrooper/`, and its materials are named `MI_MoritaTrooper_*`. Both PropertyClass checks passed.

Survey of the C1M1 world (55 NPCs, 4 distinct assets):

| Asset | NPCs |
|---|---|
| NonCombatTrooper | 32 |
| ControllerTrooper | 18, all Morita |
| SquadLeader | 4 |
| MedicTrooper | 1 |

No NPC uses `BP_CharacterAnimations_FemaleMoritaTrooper`, but the asset is loaded. It lives at `/Game/AnimationData/NPCs/`.

### The data class (correction to the static lead)

Both assets are plain **`ADSpriteAnimationData`** (super `DataAsset`), not `USTNPCAnimationData`. The live layout, read from the FProperty chain, is:
- **Asset fields:** `ArrayRemappings` (map), `Animations` (map with name keys and `ADSpriteAnimationSequence` values; 32 bytes; pair 0x30, value at +0x8), `TextureSize` (256×256), `TextureOffset`, and `DefaultAnimationKey = Idle`.
- **Sequence fields:** `bUseUniformFrameTime`, `UniformFrameTime`, `bLoop` and `Frames[]`. There is no `SequenceEndBehaviour`.
- **Frame fields:** `TimeToDisplay`, `AnimNotifies[]` and `Materials[]`, with one material per direction.

Offset checks on the female asset all passed:
- **Map:** 23 live entries of 23 slots.
- **Struct:** size equals the aligned PropertiesSize.
- **Array:** the inner property of `Frames` is owned by it.
- **Bool:** `bUseUniformFrameTime` has field size 1, offset 0 and mask 0xFF.

No set was met.

### Stand-up frames per direction

| | Male (ControllerTrooper) | Female |
|---|---|---|
| `Standup` frames | 6 | 6 |
| Materials per frame | **1** | **8** |
| Directions | `MI_MoritaTrooper_Standup_F_01..06` (front only) | `MI_FemaleMoritaTrooper_StandUp_{F,FL,L,BL,B,BR,R,FR}_01..06` |
| Frame times (s) | 0.2, 0.3, 0.3, 0.1, 0.2, 0.1 | the same |
| Uniform time / loop | off / no | off / no |
| Length | **1.2 s** | **1.2 s** |

The female's direction index runs 0=F, 1=FL, 2=L, 3=BL, 4=B, 5=BR, 6=R, 7=FR. Every other directional state uses this same order.

### How the male fills its side and back views

It does not have side or back images; it has a front-only sequence. Static RE of `USTSpriteRenderComponent::AnimatorFrameChanged` (RVA 0x51F34D0) shows the handling:
- **Exactly 8 materials:** the frame is directional. The component copies the 8 materials into its directional materials (+0x678), sets a directional flag (+0x670 = 1) and calls `ComputeDirectionIndex` to pick one per view.
- **Any other count:** the frame is non-directional. The component takes `Materials[0]` as `FrameMaterial` (+0x688), clears the flag (+0x670 = 0) and calls `UpdateMeshRenderMaterial(Materials[0], true)`. **The same front image shows at every view angle.** No warning is logged on this path.

`SetDirectionalFrameMaterials` (RVA 0x521B790) behaves differently. For any count other than 8 it logs "An Incorrect Number of Directional Frames has been supplied ! Check that all frames in the sequences have directional frame materials set" and returns without changing anything. The animator path does not go through it.

So a male Morita that is getting up shows its front stand-up frames whichever side the camera is on. `OverKill` is also single-material (1 material, `*_Overkill_*`) in both assets. Every other state has 8 materials per frame.

### Diff of the other states

Frame rate is variable in almost every state: `bUseUniformFrameTime` is false and each frame carries its own `TimeToDisplay`. "f" below means frames.

| State | Male: frames, length | Female: frames, length | Note |
|---|---|---|---|
| Idle | 3 f, 1.5 s (0.5 s each) | the same, but uniform 0.5 | loops |
| Walk | 6 f, 0.94 s | the same | loops |
| Run | 6 f, 0.70 s | the same | loops |
| Attack01 | 3 f, 0.12 s (uniform 0.04) | 3 f, 0.15 s (0.05 each) | loops |
| Flinch | 3 f, 0.85 s | 3 f, 1.30 s | |
| Death01 | 4 f, 1.06 s | the same | |
| Death02 | 4 f, 0.31 s | 4 f, 0.80 s | |
| Corpse01 | 2 f, 0 s; frame 1 has 2 null directions | 1 f | |
| Corpse02 | 1 f | 1 f | |
| GrenadeThrow | 7 f, 1.90 s | 6 f, 1.40 s | |
| Reload | 10 f, 1.55 s | the same | |
| Taunt01 | 5 f, 1.00 s | 5 f, 1.75 s | |
| Thumbsup | 5 f, 1.30 s | 5 f, 1.60 s | |
| Patdown01 | 14 f, 1.76 s | 13 f, 1.97 s | |
| Patdown02 | 12 f, 1.75 s | 12 f, 1.95 s | |
| Pointing01 | 7 f, 1.33 s | the same | loops |
| Pointing02 | 7 f, 1.29 s | the same | |
| Shouting | 8 f, 2.21 s | the same | |
| Salute | 5 f, 1.80 s | 5 f, all times 0 | female's `Salute01` is the 1.80 s copy |
| VictoryPose | 10 f, 1.71 s | — | male only |
| Taunt | — | 5 f, all times 0 | female only, unused stub |
| OverKill | 4 f, uniform 0.05, 0.20 s, 1 direction | the same | |

`ArrayRemappings` differ in one place. The male's Taunt list has 10 keys (`Salute`, plus `VictoryPose`); the female's has 9 (`Salute01`, no `VictoryPose`). The Death and Attack remappings are the same.

### Code

- **New switch:** `UBWCOOP_DUMP_SPRITE_ANIMS`, read in `config.cpp`. It is off by default; when unset, `c27_arm_map` leaves the dump disarmed and nothing else runs.
- **Code:** the card 027 block in `mod/src/hooks.cpp`, functions `c27_*`.
- **New symbols in `tools/symbols.txt`:**
  - `StaticFindFirstObject`
  - `UADSpriteAnimationData_StaticClass`
  - `USTNPCAnimationData_StaticClass`

  Only the first two are needed for these assets.
- **Generic walker:** kept, as the card asked. Every container passes a runtime layout check before it is walked:
  - **Struct:** the class is `ScriptStruct`, and the aligned `PropertiesSize` equals `ElementSize`.
  - **Array / map / set:** the inner property's Owner is the container.
  - **Bool:** FieldSize equals ElementSize, ByteOffset is less than ElementSize, and FieldMask is not 0.

  When a check fails, the walker refuses that container and does not recurse into it. A verify gate on the asset's own `Animations` map runs before any file is written.
- **Build:** clean with `/W4 /WX`. 84 exports, KERNEL32 as the only import, and no function over 60 lines.

## Senior review (2026-11-03)

Accepted. Verified against the dumps, `logs/card027-host.log`, the disassembly and the build:

- **Asset path.** The log shows `BP_AI_Trooper_Morita_C` `.AnimatorComponent` (+0xA30), a `STSpriteVariantAnimatorComponent`, pointing at `BP_CharacterAnimations_ControllerTrooper` (18 NPCs). There are 55 NPCs and 4 distinct `ADSpriteAnimationData` assets (NonCombatTrooper ×32, SquadLeader ×4, ControllerTrooper ×18). My static lead was wrong: the assets are `ADSpriteAnimationData`, not `USTNPCAnimationData`.
- **Standup.** Both assets have 6 frames, no loop, and frame times summing to 1.2 s. The male (ControllerTrooper) has 1 material per frame, `MI_MoritaTrooper_Standup_F_01..06`. The female has 8, ordered F, FL, L, BL, B, BR, R, FR. My recount of all states: only `Standup` (male) and `OverKill` (both) have a frame-0 `Materials.Num` other than 8.
- **Fewer than 8 directions.** `AnimatorFrameChanged` 0x051F34D0 compares the count with 8 at 0x051F34DA and 0x051F34EB. Otherwise it stores `Materials[0]` in +0x688 and clears +0x670 (0x051F3626 and 0x051F3630), so the male stand-up shows the front image at every view angle.
- **Durations recounted.** Flinch: male 0.85 s, female 1.3 s. GrenadeThrow: male 1.9 s, female 1.4 s. Death01: 1.06 s in both.
- **Build.** `build/dwmapi.dll` (09:07) imports only KERNEL32 and has 0x54 = 84 exports. The switch is read in `config.cpp:189` and is off by default. The run log reports no new crash dumps.

Carried forward:
- **Card 025 revive gap.** Stand-up lasts 1.2 s, so the 0.2 s and 1.0 s shots should have caught it if it had played. That card 025 saw a snap to idle means the revive path never started `Standup`. Driving it would be `StartAnimationSequence("Standup")` on the animator.
- **Downed visual.** A remote player body built from the female asset gets 8-direction stand-up frames. The male asset shows the front frame from every side. Either is usable, and the female is the better choice for revive.
