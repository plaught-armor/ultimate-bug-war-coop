# Card 549: performance mod, overlap events off on the player ally nav colliders

Family: card 527 (performance mod). Exact lever: no host decision reads these overlaps.

## Why

Card 548 (c263L369 host): the two ally nav colliders under each player Camera, AllyNavCollider
(USTPlayerDeterAllyCollider) and "Ally Nav Collider Left" (BoxComponent), cost 754.6 ms of UpdateOverlapsImpl per
30 s window, 2.5% of host GameThread. Every camera move runs an overlap query for each of them against every nearby
body (bugs, allies).

Who reads their overlaps:
- USTPlayerDeterAllyCollider has no native functions beyond its class registration (pdb publics). It is a
  nav-relevant box (INavRelevantInterface, area class USTPlayerDeterAllyNavArea) that makes allies path around the
  player's front. Nav relevance uses the box bounds, not overlaps.
- "Ally Nav Collider Left" is a Blueprint-added box with the same name pattern, read as a second nav box. Its
  Blueprint overlap bindings are not checked statically (the asset is in the IoStore). This is the open exactness
  risk.

## What (b364, 6f338eb7027d1265)

- Once a second, for each player character (the card 368 cached list), walk the Camera AttachChildren two levels and
  clear bGenerateOverlapEvents on components named `AllyNavCollider` or `Ally Nav Collider Left`.
- UPrimitiveComponent::UpdateOverlapsImpl then skips the query and ends the overlaps the box held. It still visits the
  box's own children.
- Logs `card549: <side> overlap events off on <comp> (<class>) of <pawn>, N cleared` once per component (32 lines
  at most per process).
- Off switch: UBWCOOP_C549_OFF=1. Source: `mod/src/ubw/perf_overlap_c549.cpp`.

## Predictions (the b364 run, card 548 diag still on)

- 4 `card549` lines per mission on each side (2 colliders times 2 players). Respawns add more.
- Host card548 rows for both colliders drop to under 1 us self mean. Their summed self time drops from about 755 ms to
  under 20 ms per mission window.
- The host card548 primitive UpdateOverlapsImpl total drops by at least 600 ms per window at a similar bug load.
- No WARN card549, mismatches 0, CRASH 0. Ally squad movement looks the same in the drive.

## Result c263L370 (b364): closed, lever kept

Gate: CRASH 0, mismatches 0, no WARN card549 or card548, no new WARN kinds. b364 was promoted to lane 1.
- Each side logged 4 `card549` lines in the one mission of the run (2 colliders times 2 players). Prediction 1 hit.
- The collider rows show only before the clears (5 calls or fewer, 0.0 ms), then they are gone. Prediction 2 hit.
- Host card548 primitive UpdateOverlapsImpl totals per window, L370 vs L369: 93.7/288.2/302.0/669.0/1031.9/1062.6 ms
  vs 104.5/299.4/344.3/711.8/1877.2/1995.0 ms. The peak windows drop by 845-932 ms, about 3% of host GT. That is more
  than the colliders' own 755 ms, probably because the overlap-end and overlap-begin work on the other side is gone
  too. Prediction 3 hit.
- Ally behaviour proxy (the drive has no squad lines): the card545 STSquadLeaderComponent SphereOverlapComponents
  calls per window are 15/38/60/60/60/49 vs 14/37/60/60/60/60 (the last window is cut short by the run end). The card098
  ally tally rows are 337 vs 338. Nothing was checked visually; the open risk is still a Blueprint overlap binding on
  "Ally Nav Collider Left", to watch at the next playtest.

## Open risk check (b365, pre-playtest)

The open risk is checked statically at run time instead of parked on the playtest. Once per process, on the first
player character seen, `c549_list_fns` walks the class chain (UStruct::Children, UField::Next, SuperStruct). It logs
every UFunction whose name has BndEvt, Overlap or Ally in it, then a count line:
`card549: <side> player class chain of <pawn>: N classes, M functions walked, K named BndEvt/Overlap/Ally`.
A Blueprint OnComponentBeginOverlap/EndOverlap binding on a component compiles to a
`BndEvt__<Class>_<Component>_K2Node_ComponentBoundEvent_*` function on the owning class, so it would show here. It runs
whether or not the lever is on. kStructChildren and kUFieldNext moved from testrun.cpp to ue/reflect.h.

Predictions (the first b365 soak, either side):
- One count line per side, with at least 8 classes, no "(cap hit)".
- No `BndEvt__` row names AllyNavCollider or Ally Nav Collider Left. If one does, the lever must skip that component
  (or the bound event must be checked for what it does) before the playtest build ships.

## Result soak1m1b365a (b365): prediction 2 missed, lever retired

Both sides logged the same chain: `BP_PlayerCharacterMI_C: 8 classes, 307 functions walked, 10 named`. Two of the
rows are bound begin-overlap events on the colliders themselves:
`BndEvt__BP_PlayerCharacterMI_AllyNavCollider_K2Node_ComponentBoundEvent_2_ComponentBeginOverlapSignature` and
`BndEvt__BP_PlayerCharacterMI_Ally Nav Collider Left_K2Node_ComponentBoundEvent_5_ComponentBeginOverlapSignature`.

What they do (tools/kismet listing of BP_PlayerCharacterMI.uasset, build/kis/pcmi.txt):
- Ubergraph 10622 (AllyNavCollider) and 11001 (Left) run when a fire/aim input action is held or HasRecentlyAttacked
  is true. If the other actor is a BP_AI_Attack_System_Human, it is stored as ActorHitRight/ActorHitLeft.
- Ubergraph 897/2049 then check that the ally is a friendly faction and call `STMoveToLocationWithMoveParameters` on it.
  The target is the ally location plus the player forward vector times -250 and the right vector times +500 or -500.
  The ally steps out of the player's line of fire.
- A second path, from the input action event at ubergraph 345 (gated by MoveOutTimer), calls
  `AllyNavCollider.GetOverlappingActors(BP_AI_Attack_System_Human)`. That list is kept only while
  bGenerateOverlapEvents is set.

So clearing bGenerateOverlapEvents removes a vanilla behaviour: allies no longer move out of the line of fire. The
lever is not exact. It is retired: the source is in `mod/retired/perf_overlap_c549.cpp`, and the CMake entry, the
UBWCOOP_C549_OFF switch, the declarations and the c42_step call are removed. b364 and b365 carry the lever and must not
be the play build. The first build without it is b366.

A follow-up exact lever would have to keep both paths. One option is overlap events on only while the fire/aim input
or HasRecentlyAttacked is true. It is not exact as written, because turning overlaps on raises begin-overlap for
allies already inside the box. It is not carded for the playtest.
