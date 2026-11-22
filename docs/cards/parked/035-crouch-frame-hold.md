# Card 035: Show a crouch pose on the remote sprite
Status 2026-11-22: parked, not in the queue. The status below is as first written.

Status: draft (senior, 2026-11-03). Send only after the user says go and the junior has been cleared.

Read `docs/STATE.md` first: the "Mod code rules" section, the "Runtime facts" line about `tools/run_pair.sh`, and rows 028, 033 and 034. Then read card 034's report and senior review. Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

When the other player crouches, their sprite still stands at full height (card 034 now keeps its feet on the ground, at half-height 55). No NPC set has a crouch animation; the junior's survey (`logs/crouch-survey/`) found only single low frames inside StandUp and Death. Our own set, `BP_CharacterAnimations_FemaleMoritaTrooper`, has a `StandUp` key whose frame 4 is a half-crouch with an upright torso. Holding that frame while crouched gives a readable crouch with no new art. This card also builds the "hold a chosen frame" path that custom crouch art would reuse (card 036).

## Static leads (senior)

- Animator natives (RVAs from the PDB publics; add to `tools/symbols.txt` as needed). The `UADAnimatorBaseComponent` versions of Start, Pause, Resume and FixedTime are pure-virtual stubs; call the `UADSpriteAnimatorComponent` overrides, or go through the vtable.
  - `StartAnimationSequence(FName, float PlayRate)`: sprite override 0x04A249C0. Card 028 already calls it through `g_start_sequence` (symbol `UADSpriteAnimatorComponent_StartAnimationSequence`).
  - `PauseAnimationSequence()`: sprite 0x04A242F0. It is a this-adjusting thunk (`add rcx, -0xB8`), so its `this` is the object 0xB8 bytes into the component, not the component itself. It calls vtable slot +0x410 with `false`, which is most likely `SetComponentTickEnabled`. A pause therefore stops the whole animator tick.
  - `ResumeAnimationSequence()`: sprite 0x04A24650, the same thunk. It makes the same call with `true`, but only when the pointer at thunk-this +0x68 (component +0x120) is non-null.
  - `StartAnimationSequenceFixedTime(FName, float Seconds)`: sprite 0x04A24C20. It looks up the sequence, then calls `StartAnimationSequence` (vtable +0x4E8) with PlayRate = duration / Seconds. It plays the whole key in that time; **it does not seek** to a frame.
  - `IsPlayingAnimation(FName)` sprite 0x04A24040 (it takes a key). `HasAnimationSequenceForKey(FName)` sprite 0x04A23EB0. `GetCurrentAnimationKey()` 0x01E1A040. `GetLastFrame(const FName&, FADAnimationFrame&)` 0x04A23770, which returns a key's last frame.
- Consequence: because a pause stops the tick, the variant animator probably stops choosing the frame by view direction while paused. Plan for it: when the remote pawn's direction bucket changes while held, restart `StandUp` with a high play rate, then pause again when frame 4 is reached.
- The frame shown arrives through the animator's `OnFrameChange`, which is bound to the renderer's `AnimatorFrameChanged(const FADAnimationFrame&)` (0x051F34D0; card 034). That function only swaps the frame material. A detour on it, like card 034's on `AnimatorFrameSizeChanged`, sees every frame change on our renderers.
- Card 034 already tracks the remote pawn's capsule half-height on every c28 step: 95 standing, 55 crouched. `ACharacter::bIsCrouched` is the other signal; use whichever card 034's code already reads, and log both once.

## Do

1. **Measure.** Check `HasAnimationSequenceForKey(StandUp)` on our animator and log the StandUp frame count. Confirm that frame 4 (one-based, as in the survey sheet) is the half-crouch: take one close shot with it held, on the host.
2. **Hold.** While the remote pawn is crouched, the c28 key picker selects `StandUp` held at frame 4, in place of Idle, Walk and Run.
   - Start `StandUp`, then pause when the frame reaches 4 (seen through the frame-change detour). Say how you reach frame 4 quickly (a high play rate) and how you call the thunked Pause (vtable, or the component pointer plus 0xB8).
   - Attack01 while crouched: keep the held crouch frame. A crouched player firing shows as crouched. Note this as a known gap (no crouch-fire art).
   - On uncrouch, return to the normal key choice. Log `IsPlayingAnimation` after the change, to show that the pause was released.
   - The variant animator picks the frame for the view direction. Check that the held frame still changes with the viewing angle (side and front shots).
3. **Autoinput.** If the autoinput script has no stand-up step after its crouch, add one (an "uncrouch" action a few seconds after the fire steps). This also exercises card 034's uncrouch refit (55 → 95): look for its "half-height change … last refit 55.0" line.
4. **Log.** One line per remote pawn per crouch change: crouched or standing, the key chosen, the held frame, the half-height and the refit z. Cap the lines per map.
5. **Runs.** Use the card 026c pair command, as in card 034. Both instances share one Screenshots folder, so take the client's shots 1 s after the host's, or give each side its own file name (card 023 lost 3 of 10 shots to collisions).

## Answer, with log lines

- Which call holds the frame, and does the held frame follow the view direction?
- Does the sprite return to Idle, Walk or Run after uncrouch, with no stuck pause?
- Does the uncrouch refit fire (relative z 59.3 → 19.3)?

## Done when

- Shots on both sides: the crouched remote sprite in the half-crouch pose with its feet on the ground (side and front), and the same pawn standing again after the uncrouch.
- NPC sprites are untouched, and standalone is untouched.
- Clean build (`/W4 /WX`, `--clean-first`), 84 exports, KERNEL32-only imports. No new crash dumps. Every new function is at most 60 lines. Write the `card035x-run.txt` file with the rc.

## Risks

- A paused animator may stop the direction updates. If the held frame does not turn with the view, use the restart-on-direction-change plan from the static leads, and report how often it fires.
- The half-crouch frame is shorter than the standing frame within the same 256 × 256 cell, so `FitMeshToCollider` still puts the cell's bottom on the capsule bottom. If the feet in that frame are not at the cell's bottom edge, report the gap in pixels; do not tune z by hand.
