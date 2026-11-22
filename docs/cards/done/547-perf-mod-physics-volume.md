# Card 547: performance mod diag, UpdatePhysicsVolume cost by component

Family: card 527 (performance mod). Diag only; it changes no result.

## Why

c263L367 card 546: the two player Camera components hold 99% of the player UpdateOverlaps self time, about 354 ms per
30 s host window (1.19% of GT). For a non-primitive component, that self time is USceneComponent::UpdatePhysicsVolume.

Static trace of USceneComponent::UpdatePhysicsVolume (RVA 0x3aff140):
- It returns at once unless bShouldUpdatePhysicsVolume (+0x1a1 bit 2) is set.
- It takes the world (component +0xa8, else GetWorld) and the default physics volume (world +0x150).
- It reads the non-default volume count (world +0x290, array at +0x288) and compares it with a global threshold
  (0x14901aac0, value 20).
  - Up to 20 volumes, it loops them: FWeakObjectPtr::Get, then a bounds and priority check per volume.
  - Above 20, it runs one world overlap query: ComponentOverlapMultiByChannel (0x143aff4dc) for a primitive
    component, or OverlapMultiByChannel (0x143aff54a) otherwise. It then filters the hits for APhysicsVolume and
    keeps the highest priority.
- When the volume changes, it broadcasts PhysicsVolumeChanged and sets the weak pointer at +0x1a5.

Before any lever, the run must show which path runs, how many volumes the map has, and which components pay.

## What (b362, 36adef794cea1455)

- Detours on USceneComponent::UpdatePhysicsVolume and UPrimitiveComponent::UpdatePhysicsVolume (0x3aaf100), with the
  signature `void(USceneComponent*, bool bTriggerNotifiers)`. Only the outermost game-thread call is timed, since
  the primitive override calls the scene one as Super.
- Rows are keyed by (entry, component class, owner class). Every 30 s it logs:
  - `card547: <side> t=... physics volume total: N calls, ms (%); world volumes max V (query path above 20: Q calls)`
  - the top 12 rows: `card547: <side> t=... physics volume top N: <entry>::UpdatePhysicsVolume on <class> owned by <owner class>: ...`
  - `WARN card547` when rows were dropped.
- Gate: UBWCOOP_C132_FX=1. Source: `mod/src/ubw/perf_volume_c547.cpp`.

## Predictions (the b362 run)

- The host CameraComponent rows owned by BP_PlayerCharacterMI_C sum to within 15% of the card 546 camera self sum
  in the same window.
- World volumes max is above 20, so the query path runs (Q is most of N).
- No WARN card547, mismatches 0, CRASH 0.

## Next

If the query path dominates, find who reads the camera's physics volume. If nothing reads it (no PhysicsVolumeChanged
binding, no GetPhysicsVolume on the camera), a lever can skip the camera's update. That lever is cosmetic only and
changes no host decision. Otherwise, look for an exact cache: the result for an unmoved point and an unchanged volume
set is the same.

## Result c263L368 (b362): closed, physics volumes are not the cost

- World volumes max is 0 in every window, so the query path never runs. The host total is 2.5 to 4.3 ms per window
  over 35k to 85k calls, all bug, trooper and player capsules.
- No CameraComponent row appears: the cameras never call UpdatePhysicsVolume. Predictions 1 and 2 missed.
  Prediction 3 hit (no WARN card547, mismatches 0, CRASH 0).
- In the same windows, card 546 still shows the cameras at 257 to 268 ms (448 to 463 us mean) and 115 to 210 ms.
- Static re-read of USceneComponent::UpdateOverlapsImpl: the loop over attached children (0x143aff0a5) calls each
  child's UpdateOverlapsImpl through vtable +0x568, not through the public UpdateOverlaps that card 534 hooks. The
  camera's self time is therefore its attached primitives. Card 548 measures those. b363 removes the card 547 hooks.
