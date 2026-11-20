# Card 034: Keep the remote sprite's feet on the ground

Status: done 2026-11-03.

Read `docs/STATE.md` first: the "Mod code rules" section, the "Runtime facts" line about `tools/run_pair.sh`, and rows 028, 033 and 026. Then read the "Body transform" bullet in card 028's report and the last section of card 026's report. Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why

The user watched card 026c and saw both remote sprites sunk to about the knees: the client's sprite on the host, and the host's sprite on the client. `logs/card026c-host-4-Attack01-front.png` shows it. The NPC Morita troopers stand correctly, so the game already knows how to place a sprite. Our copy does not follow its rule.

## Static leads (senior, from disassembly; confirm at run time)

- `USTSpriteRenderComponent::FitMeshToCollider(float)` (RVA 0x051FB640, public `?FitMeshToCollider@USTSpriteRenderComponent@@QEAAXM@Z`) sets the renderer's relative location to `(0, 0, 50 * RelativeScale3D.Z - 0.5 * arg)`. It reads `RelativeScale3D.Z` at +0x180 and uses the constants 100.0 and 0.5. The quad is 100 units tall, so this puts the quad's bottom edge at `-arg / 2`. In other words, `arg` is the collider's full height, and the feet land on the capsule bottom.
  - Check against card 028's numbers: Morita scale Z 2.286 and capsule half-height 100 give 114.3 - 100 = 14.3, which is exactly the logged NPC relative z.
- `ResizeAndFit(FVector2D size, float arg)` (0x05214500) sets the scale to `(1, size.X / PPM, size.Y / PPM)`, reading PPM as a float at +0x650. It then tail-calls `FitMeshToCollider(arg)`.
- `AnimatorFrameSizeChanged(const FVector2D&)` (0x051F3660) only sets the scale. It never moves the quad.
- Why we sink:
  1. Card 028 places the renderer once, at `loc.z - npc_hh + pawn_hh` = 9.3. The sign is wrong: the same rule gives 14.3 + 100 - 95 = 19.3 for the idle frame.
  2. The scale then changes with every frame size (the animator's `OnFrameSizeChange`), while z stays fixed. The quad grows around its centre, so a taller frame pushes the feet down further.
- How the NPC keeps its feet down is not known yet. Its `OnFrameSizeChange` (animator +0x100, a `FMulticastScriptDelegate`) probably reaches a Blueprint handler that calls `ResizeAndFit` or `FitMeshToCollider` with the capsule height. Our animator's binding is to the renderer's native `AnimatorFrameSizeChanged`.

## Do

1. **Measure first.** Log once per map:
   - for both the NPC template's animator and our remote animator, each `OnFrameSizeChange` and `OnFrameChange` binding: the bound object's name and class, and the function FName. Each invocation-list entry is an `FScriptDelegate` holding a weak object pointer and an FName.
   - the NPC renderer's `RelativeLocation.Z` and `RelativeScale3D.Z` on two different animation keys, if one NPC changes key during the run (Idle and Walk, for example). This shows whether the NPC is refitted when its frame size changes.
2. **Fix.** After every frame-size change on one of *our* renderers, call `FitMeshToCollider(2 * pawn capsule half-height)` on it, so the bottom edge is the capsule bottom.
   - The suggested place is a detour on `AnimatorFrameSizeChanged`: call the original first, then refit only when `this` is a renderer that `c28` attached. Look it up in the c28 slot table; never touch NPC renderers.
   - If step 1 shows the NPC uses a different function or argument, copy what the NPC does instead, and say why.
   - Replace the fixed z in `c28_body_transform` with the same rule, so the first frame is right too. Keep the logged numbers.
3. **Log.** One line per remote pawn the first time each key is refitted: key, scale Z, new relative z, and capsule half-height. Cap the lines per map.
4. **Runs.** Use the card 026c pair command unchanged (`UBWCOOP_C28_SHOTS=1` on both sides).

## Answer, with log lines

- What is the NPC's `OnFrameSizeChange` bound to, and does the NPC renderer's z change between keys?
- What relative z does our renderer get for Idle, Walk and Attack01 on each side?
- Are the feet on the ground in the side and front shots on both machines?

## Done when

- In the card 028 key shots on both sides (Idle, Walk and Attack01 at least), the sprite's feet are at ground level, matching how a nearby NPC trooper stands, where one is in view. Show me one shot per side next to the matching 026c shot.
- Only our c28 renderers are refitted. NPC sprites are untouched, and standalone is untouched.
- Clean build (`/W4 /WX`, `--clean-first`), 84 exports, KERNEL32-only imports. No new crash dumps. Every new function is at most 60 lines.

## Risks

- `+0x180` and `+0x650` are offsets taken from disassembly. If the reflected `RelativeScale3D` or `PPM` offsets differ, log both and stop.
- `FitMeshToCollider` also sets x and y to the constant at 0x148FA1340, which is probably zero. If the logs show it moves x or y, report it.
- The pawn's capsule half-height may change, for example while crouching. Read it at each refit, not once.

## Senior addendum (2026-11-03, after sending)

- **Callers, from a scan for direct calls in `.text`.**
  - `FitMeshToCollider` is called only by its exec thunk, so only from Blueprints, and by `ResizeAndFit`.
  - `ResizeAndFit` is called only by `ASTAnimationPreviewCharacter::TryPlayAnimationSequence`.
  - `ResizeMeshToTextureSize` is called by its exec thunk and by the gore-part sprites.
  - The Morita Blueprint's name map shows none of these, but a void call is invisible there (hashed imports), so it proves nothing.
- **The sign error accounts for only 10 units** (9.3 against 19.3). The sprite is about 228 units tall, so a 10-unit drop is about 4% of its height, not the knee-deep sink the user saw. Something else is also low.
- **Add to step 1:** log world z values once per key on both sides for one remote pawn and one nearby NPC Morita:
  - the capsule bottom (actor z minus the scaled half-height);
  - the renderer's world z;
  - the quad's bottom edge (renderer world z minus 50 times the world scale Z);
  - the ground z from a downward line trace, or the pawn's floor result if that is easier.

  Fix whichever gap the numbers show. Do not tune a constant until the cause is known.

## Report

**Summary.** The knee-deep sink had two causes:

- **Sign error, 10 units.** The attach z used the card 028 rule. That is fixed: the attach z is now 50 × scale Z − half-height = 19.3.
- **Crouch, 40 units.** This was the main cause. The autoinput script crouches (`autoinput: action crouch (t=13503 ms)`) before its fire steps, so every Attack01 shot was taken crouched. Crouching takes the capsule half-height from 95 to 55 and lowers the root by 40. Our renderer's relative z stayed fixed, so the quad dropped 40 below the capsule bottom.

Card 034 now calls `FitMeshToCollider(2 × half-height)` in two places:

- after every frame-size change on a c28 renderer, in a detour on `AnimatorFrameSizeChanged`;
- on every c28 step where the remote pawn's half-height differs from the last refit's.

With both in place, the quad bottom equals the capsule bottom in every key on both sides. Runs card034a (measure only, refit off) and card034b (fix) used the 026c pair command unchanged.

### Offsets (risk check)

My first static `RelativeLocation` constant was wrong. I had taken +0x158 from FitMeshToCollider. The reflection check in card034a caught it, and the refit stayed off for that run:

```
WARN card034: host reflected RelativeLocation +0x140 (static +0x158), RelativeScale3D +0x170 (static +0x170): MISMATCH, refit off
```

On a second look at the disassembly, FitMeshToCollider loads +0x158/+0x168 as **RelativeRotation**: it passes the rotation through, and the rotator normalisation follows. RelativeLocation is +0x140. I fixed the constant and added a reflected check for RelativeRotation. All three offsets match in card034b, and PPM matches as well:

```
card034: host reflected RelativeLocation +0x140 (static +0x140), RelativeRotation +0x158 (static +0x158), RelativeScale3D +0x170 (static +0x170): match, refit on
card022:     card028 Morita renderer STSpriteRenderComponent.PPM +0x650 (FloatProperty) = 112
```

The "relative z 0.0" values in the card034a world z lines are RelativeRotation.Pitch, which the wrong constant read. They are not data; card034b has the real values.

### What the NPC's OnFrameSizeChange is bound to, and whether its z changes

The NPC's OnFrameSizeChange is bound to the Morita's own sprite renderer, `AnimatorFrameSizeChanged`. Our animator has the same binding to our renderer.

```
card034: host Morita animator AnimatorComponent OnFrameSizeChange: 1 bound
card034:   [0] SpriteRenderComponent (STSpriteRenderComponent, outer BP_AI_Trooper_Morita_C) function AnimatorFrameSizeChanged (weak index 118239)
card034: host remote animator STSpriteVariantAnimatorComponent OnFrameSizeChange: 1 bound
card034:   [0] STSpriteRenderComponent (STSpriteRenderComponent, outer BP_PlayerCharacterMI_C) function AnimatorFrameSizeChanged (weak index 117844)
```

OnFrameChange is bound to `AnimatorFrameChanged` on both. That function only swaps the frame material and writes no location. The Morita's z does not change between keys:

```
card034: host Morita BP_AI_Trooper_Morita_C size change 256.0 x 256.0: relative z 14.3 -> 14.3, scale Z 2.286 -> 2.286
```

Its relative z is 14.3 in every world z line on both sides. That equals 50 × 2.286 − 100, the same rule we use. Every frame is 256 × 256, so the size change fires once per renderer (at the first key) and the scale stays 2.286.

### World z (addendum)

Before the fix (card034a, Attack01, the same on both sides):

```
card034: host world z remote BP_PlayerCharacterMI_C key Attack01: capsule bottom 8.5, renderer 82.7, quad bottom -31.5, ground 6.3; quad bottom - capsule bottom -40.0, capsule bottom - ground 2.2; ...
```

Idle, Walk and Run already had a quad bottom − capsule bottom gap of 0.0 in card034a, because of the attach-time sign fix. The Morita's gap is 0.0 as well: it is −0.0 in every line.

After the fix (card034b):

```
card034: host refit BP_PlayerCharacterMI_C key Idle (half-height change, frame 0.0 x 0.0): scale Z 2.286, relative z 19.3 -> 59.3, x 0.0 -> 0.0, y 0.0 -> 0.0, capsule half-height 55.0 (last refit 95.0)
card034: host world z remote BP_PlayerCharacterMI_C key Attack01: capsule bottom 8.5, renderer 122.7, quad bottom 8.5, ground 6.3; quad bottom - capsule bottom -0.0, capsule bottom - ground 2.1; relative z 59.3, ... capsule half-height 55.0
card034: client world z remote BP_PlayerCharacterMI_C key Attack01: capsule bottom 8.5, renderer 122.7, quad bottom 8.5, ground 6.3; quad bottom - capsule bottom -0.0, capsule bottom - ground 2.2; relative z 59.3, ... capsule half-height 55.0
card034: host world z nearest Morita BP_AI_Trooper_Morita_C key None: capsule bottom 4.9, renderer 119.4, quad bottom 4.9, ground -0.8; quad bottom - capsule bottom -0.0, capsule bottom - ground 5.6; relative z 14.3, ...
```

The capsule bottom sits 2 to 7 units above the trace ground on our pawn, and 2 to 14 on the Morita (the trace uses the PlayerProjectile profile). So the quad bottom is within a few units of the ground for both.

The "key Idle" label on the half-height refit line is the key that was playing when the crouch landed. The refit fires on the step where the half-height changes, not on a key change.

### Relative z per key

| side | Idle | Walk | Run | Attack01 (crouched) |
|---|---|---|---|---|
| host (client's pawn) | 19.3 | 19.3 | 19.3 | 59.3 |
| client (host's pawn) | 19.3 | 19.3 | 19.3 | 59.3 |

Source lines: the card034b `world z remote` lines above, plus the size-change refit:

```
card034: host refit BP_PlayerCharacterMI_C key Idle (size change 256.0 x 256.0): scale Z 2.286, relative z 19.3 -> 19.3, x 0.0 -> 0.0, y 0.0 -> 0.0, capsule half-height 95.0 (last refit 0.0)
card034: client refit BP_PlayerCharacterMI_C key Idle (size change 256.0 x 256.0): scale Z 2.286, relative z 19.3 -> 19.3, x 0.0 -> 0.0, y 0.0 -> 0.0, capsule half-height 95.0 (last refit 0.0)
```

**x and y do not move.** They stay at 0.0 → 0.0 in every refit line, so the constant at 0x148FA1340 is zero for x and y.

### Shots

The shots were copied from `Saved/Screenshots/Windows` (ScreenShot00153–165) and matched to the SHOT lines by time gaps. There are 13 files for 14 SHOT lines, because client SHOT 5 and SHOT 6 fired in the same millisecond, the same quirk as in 026c.

- **Host, Attack01 front.** `logs/card034b-host-4-Attack01-front.png` against `logs/card026c-host-4-Attack01-front.png`. In 026c the sprite is cut off at the knees. In 034b the full body stands with its feet on the ground.
- **Client, Attack01 front.** `logs/card034b-client-7-Attack01-front.png` against `logs/card026c-client-7-Attack01-front.png`. The feet stand on the bridge deck.
- **Side views.** `logs/card034b-host-2-Walk-side.png` and `logs/card034b-host-6-Idle-side.png` both show the feet on the ground, with the shadow under them.
- **No client side view.** The client has no side key shot this run. Its SHOT 3 back view (`card034b-client-3-Idle-back.png`) does not contain the host pawn, the same view-tooling limit as before. The client's front shots are `client-1`, `client-2`, `client-4`, `client-5` and `client-7`.

### Scope and checks

- **Only c28 renderers are refitted.** The detour refits only renderers whose object index is in the c28 slot table, with the owner pawn's index matching as well. The half-height check runs only from `c28_visit`, which handles remote pawns only and returns early in standalone.
  - The Morita renderer runs the original only and is logged: relative z 14.3 in every line.
  - Standalone was not run this card. There the slot table is empty, so the detour only calls the original.
- **Build.** Clean with `--clean-first` under `/W4 /WX`: 84 exports, imports `KERNEL32.dll` only.
  - card034a: rc 0/0, FATAL/ERROR 0, WARN 2 (the mismatch and the refit-skipped line, both expected).
  - card034b: rc 0/0, FATAL/ERROR 0, WARN 0.
  - No new crash dumps.
- **Function lengths.** The longest new or changed functions are `c28_attach` (35 lines), `c28_find_morita` (32) and `c34_refit` (32). All are under 60.

### Code

All code changes are in `mod/src/hooks.cpp`, card 034 block:

- `c34_check_offsets` and `c34_log_bindings`;
- the world z helpers: `c34_world_z`, `c34_log_z_line` and `c34_log_z`;
- `c34_slot_of_renderer`, `c34_refit` and `c34_track_half_height`;
- `detour_frame_size_changed`.

Supporting changes:

- `c28_body_transform` uses the fit rule.
- `c28_find_morita` takes an optional actor and returns the Morita nearest to it.
- The `C28Slot` and `C28State` fields were added.
- The hook and call-table entries were added.
- Two entries were added to `tools/symbols.txt`.

## Senior review (2026-11-03)

Accepted.

Verified:
- Logs: `card034b` host and client have 0 FATAL, 0 ERROR and 0 WARN. The offset check reads "match, refit on" on both sides (RelativeLocation +0x140, RelativeRotation +0x158, RelativeScale3D +0x170).
  - The size-change refit gives relative z 19.3 at half-height 95.
  - The half-height refit gives 19.3 → 59.3 at half-height 55, once per side.
  - Attack01 world z: quad bottom 8.5, equal to the capsule bottom 8.5, on both sides.
  - The Morita renderer stays at relative z 14.3.
- The crouch cause is confirmed: `autoinput: action crouch` is present in both `card026c` and `card034b`, before the fire steps.
- My first-run notes agree: in `card034a`, only Attack01 had a gap (−40), and `AnimatorFrameChanged` (0x051F34D0) only swaps the material.
- Screens: `card026c-host-4-Attack01-front` shows the sprite cut off at the knees. `card034b-host-4-Attack01-front` shows the full body with its feet on the ground. The client front shots show the feet on the bridge deck in both runs.
- Build: the deployed DLL matches `build/` (xxh64 850542d48355a227), with 84 exports and KERNEL32 only. The awk check finds no function over 60 lines in `mod/src/*.cpp`.

Follow-ups (not blocking):
- The uncrouch refit (55 → 95) was not exercised: autoinput never stands up again. The same half-height check covers it. Watch for a "half-height change … last refit 55.0" line in a later run.
- No `card034b-run.txt` with the rc. The logs end cleanly, so the run is accepted. Keep writing the run file in future cards.
- The client still gets no side view of the host's pawn (view tooling).
