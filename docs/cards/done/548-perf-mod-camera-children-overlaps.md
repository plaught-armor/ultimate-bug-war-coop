# Card 548: performance mod diag, player primitive UpdateOverlapsImpl by component

Family: card 527 (performance mod). Diag only; it changes no result.

## Why

c263L368 card 546: the player Camera components cost 257 to 268 ms plus 115 to 210 ms of UpdateOverlaps self time
per 30 s host window (up to about 1.6% of GT). Card 547 ruled out UpdatePhysicsVolume. USceneComponent's
UpdateOverlapsImpl calls each attached child's UpdateOverlapsImpl directly (vtable +0x568), so card 534 and card 546
fold all child primitives (first-person meshes, weapons, what they carry) into the camera's self time.

## What (b363, ddf2675fe0c3a75f)

- Detours on UPrimitiveComponent::UpdateOverlapsImpl (0x3aadfa0), USkinnedMeshComponent::UpdateOverlapsImpl
  (0x3afef70) and USkeletalMeshComponent::UpdateOverlapsImpl (0x419aef0), with the signature
  `bool(comp, const TArrayView<const FOverlapInfo>*, bool notify, const TArrayView<const FOverlapInfo>*)`.
- The skinned override shares its address with USceneComponent::UpdateOverlapsImpl (identical code folding), so the
  camera's own impl also gets a row.
- Each game-thread call on a player-owned component gets a row keyed by component, with inclusive time and self time
  (minus nested hooked calls on other components). A Super call on the same component is not a new level.
- Every 30 s it logs:
  - `card548: <side> t=... primitive UpdateOverlapsImpl total: ...`
  - the top 16 rows: `card548: <side> t=... player primitive overlaps top N: <component> (<class>): calls, self, inclusive, mean`
  - `WARN card548` on drops.
- Gate: UBWCOOP_C132_FX=1. Source: `mod/src/ubw/perf_overlap_c548.cpp`.

## Predictions (the b363 run)

- The Camera rows' self time is under 10% of their inclusive time. The work is in the children.
- At most 3 child components hold at least 80% of the camera inclusive time, and they are primitives with overlap
  events on: a first-person mesh, a weapon mesh, or an attached collision shape.
- No WARN card548, mismatches 0, CRASH 0.

## Next

For each top child, read why it generates overlap events and who binds its overlap delegates. A child whose overlap
events nobody reads can have GenerateOverlapEvents turned off, an exact lever. Rank by GT ms.

## Result c263L369 (b363, ddf2675fe0c3a75f)

Host window t=90541975 (mission, both players on foot): primitive UpdateOverlapsImpl totals 1877.2 ms (6.25% of GT).
The player rows are almost all the two ally nav colliders of each of the two player characters:

| Component (class) | Calls | Self ms | Self us mean |
|---|---|---|---|
| AllyNavCollider (STPlayerDeterAllyCollider), pawn A | 1086 | 325.2 | 299.5 |
| AllyNavCollider (STPlayerDeterAllyCollider), pawn B | 1568 | 177.4 | 113.1 |
| Ally Nav Collider Left (BoxComponent), pawn A | 1086 | 173.8 | 160.0 |
| Ally Nav Collider Left (BoxComponent), pawn B | 1568 | 78.2 | 49.8 |

That is 754.6 ms per 30 s window (2.5% of host GT). The next player row (CollisionCylinder) has 4.3 ms of self time.
Prediction 2 hit: the cost is attached collision shapes, not meshes. The dropship phase is cheap (DropshipMesh 83 ms
per window at most). Lever: card 549.
