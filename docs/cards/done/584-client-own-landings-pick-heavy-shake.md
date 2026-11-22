# Card 584: the client's own landings play the heavy landing shake

Found by the senior in c551b384a (b384, card 551 jumps): on its own landings the client ran
`Shake_Enemy_HeavyLanding_C` 9 of 10 times (the first landing ran `Shake_Generic_Fall_C`, t=149762978), while the host's
own landings on the same jumps ran `Shake_Generic_Fall_C` 11 of 11. All rows have caller exe+0x5133F6A and impact
target `BP_LandingPlatform_01_MilitaryBase_C`. The host's skip rows for the client copy alternate between
`GC_Impacts_Human_Landing_C` and `GC_Impacts_Human_Landing_Heavy_C`.

## Static trace (junior, 2026-11-21)

- exe+0x5133F6A is inside `USTImpactGameplayCueNotify::PlayScreenShake` (exe+0x5133E50). It reads the shake class
  from the cue notify itself (this+0x1E8 / +0x1F0, a soft class resolved there) and calls
  `USTCommonUtilityLibrary::StartCameraShake` (exe+0x5251D30). So the shake class is fixed per cue class:
  `Shake_Enemy_HeavyLanding_C` means the heavy landing cue ran, not a different shake pick inside one cue.
- The heavy / normal choice is upstream, at the cue tag. Lead: `USTFallDamageAbility::OnLandedCallback` (exe+0x5130DD0)
  stores the avatar's velocity Z (vtbl+0x360, double at +0x10) as a float at ability+0x4D0 on landing; the function
  at exe+0x5130F40 (movement mode 3 = Falling, a check on byte +0x231 being 1 or 2) reads +0x4D0 and calls
  exe+0x143B6B4F0 with it. Not yet read: where the landing cue tag (Human_Landing vs Human_Landing_Heavy) is chosen
  from that value, and the threshold.

## Question

Does the client's own pawn land with a larger fall speed than the host's on the same jump (a client velocity
overwritten or extrapolated by the mod's movement sync, or a longer fall from a correction), or does the client read
a different value (ability on the client not the predicting instance)?

## Next step

Static: follow +0x4D0 into the cue tag choice (exe+0x5130F40 onward, and the Blueprint that sends the landing cue),
get the threshold. Then, only with a written prediction, one run that logs the landing velocity Z per landing on
both sides for the same jumps.

## Static trace, part 2 (junior, 2026-11-21): the heavy pick is by air time, not fall speed

The fall damage lead above is the wrong path. `USTFallDamageAbility::OnMovementModeChange` (exe+0x5130F40) feeds
+0x4D0 into a `UCurveFloat` (+0x3A8) for fall damage only (exe+0x1451170B0). It does not pick the landing cue.

The cue pick is in `ASTPlayerCharacter::Landed` (exe+0x51CD6F0):

- cfg = character+0x7E0, then +0x110.
- If cfg+0x94 (`HeavyLandingTimeThreshold`) >= character+0x9C8 (air time), the landing is normal. It uses
  `JumpLandingVelocityMultiplier` (cfg+0x80) and the cue at cfg+0x290 (exe+0x14511DC50).
- Otherwise the landing is heavy. It uses `JumpHeavyLandingVelocityMultiplier` (cfg+0x84) and
  `HeavyLandingGameplayCue` (cfg+0x298), plus the heavy landing damage path (`HeavyLandingDamageTemplate` cfg+0x98,
  `bApplyHeavyLandingDamageWhenLandingOnCharacter` cfg+0x198, exe+0x14522D5C0).
- At the end, Landed sets character+0x9C8 = 0.

Air time is accumulated in `ASTPlayerCharacter::Tick` (exe+0x51E06F0, at exe+0x1451E0F0E):

- It gets the movement component (vtbl+0x748), then calls vtbl+0xD8 on the interface at +0x120 (read as
  IsFalling).
- If that is true, +0x9C8 += DeltaSeconds (xmm10, the Tick argument).
- Otherwise, +0x9C8 = 0.

So the client's own pawn lands heavy when its Tick saw Falling for longer than `HeavyLandingTimeThreshold` before
Landed ran. The same jump on the host's pawn stays under it.

## Leads

- (a) Long client frames. One frame of 450 ms or more while Falling adds its whole DeltaSeconds. Card 551 jumps run
  at about 60 fps, so this alone is unlikely 9 of 10 times.
- (b) More likely: something on the client keeps its own CMC in Falling, or re-enters Falling after the
  landing, without a Landed in between. Candidates are a server correction (ClientAdjustPosition sets Falling and
  replays) or the mod's floor and base code (card 535 is client copies only, so not that). Landed then sees air
  time summed over more than one fall.
- (c) Landed runs before the Tick reset on the host and after it on the client. This does not change the sum: the
  reset happens only when not falling.

## Prediction and run (needs the senior's go)

One diag row per own-pawn Landed on both sides: t, air time +0x9C8, cfg+0x94, movement mode before, and the count of
Falling to Walking changes since the last Landed. Prediction: the client's own landings read air time above the
threshold, with more than one Falling stretch or a correction between jumps; the host's read under it on the same
jumps. Rides the next card 551-style jump run (the c551 block on b410+), 0 extra runs.

## Diagnostic (b411 `0b694ba883d43fae`, `player_c99.cpp`)

`ASTPlayerCharacter::Landed` is hooked (ungated, log only). On this machine's own trooper it logs one row per landing
(48 per run): "card584: <side> t=... own Landed <pawn>: air time A s, heavy threshold H s (heavy|normal), mode M,
falling enters E leaves L since the last Landed (N ms ago)". The enters and leaves are counted in
`detour_set_movement_mode` for the own trooper's CMC only. A clean jump reads 1 enter and 1 leave, because the previous
landing's leave runs after its Landed. The proof rides the next card 551-style jump run.

## Proof run c584b432a (b432 `06c5597f6e2354bb`, FED_C1M1 FAST, 2026-11-21): not reproduced

- 10 alternate jumps per side (c551 layout, lesson "alternate jumps"). Each side logged 11 `card584: ... own Landed`
  rows: the spawn landing (air time 0.247 s host, 0.233 s client, falling enters 1 leaves 0) and 10 jump landings
  at air time 1.083 s on both sides, all "heavy threshold 1.500 s (normal)", each with 1 falling enter and 1 leave.
- Shakes: 11 `StartCameraShake Shake_Generic_Fall_C` on each side (own landings only), 0 `Shake_Enemy_HeavyLanding_C`.
- The client's air time matches the host's on every jump, so no correction stretched the client's Falling stretch.
  The c551b384a heavy picks (b384) do not reproduce on b432. Close unless a playtest shows the heavy shake again.
- Logs: logs/c584b432a-host.log, logs/c584b432a-client.log, logs/c584b432a.drive.

## Closed (senior, 2026-11-22)

The fix is proven by logs and runs, and the proof is in the sections above. Under the no-playtest-parking rule, a proven fix does not stay open waiting for a play check. It is on the playtest checklist in STATE.md. If the user sees it again in play, reopen this card with that report.
