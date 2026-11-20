# Card 546: performance mod diag, which player components pay for UpdateOverlaps

Family: card 527 (performance mod). Diag only; it changes no result.

## Why

Since c263L356 the largest steady card 534 row is UpdateOverlaps by BP_PlayerCharacterMI_C: host 685 ms per 30 s
window (2.28% of the GameThread, about 13.9k calls at 49 us mean), client 2.29%. The row names only the owner. A
lever needs the components that pay, since the cost is likely a few components with overlap events on (weapon,
interaction or trigger shapes) rather than the capsule.

## What (b361, 566c061299776a2d)

- In the card 534 UpdateOverlaps detour (entry 12), a depth stack splits each call's time into inclusive and self
  (exclusive of nested UpdateOverlaps calls of attached children).
- Calls whose component owner is a pawn and not an NPC are rows keyed by component (128 rows, weak references).
- Every card 534 window logs the top 16 by self time:
  `card546: <side> t=... player overlaps top N: <component> (<class>): <calls> calls, self <ms> (<%>), inclusive <ms>, <us> self mean`,
  and `WARN card546` when rows were dropped.
- Gate: UBWCOOP_C132_FX=1, like card 534. Source: `mod/src/ubw/query_c534.cpp`.

## Predictions (the b361 run)

- The self times of the rows sum to within 10% of the card 534 BP_PlayerCharacterMI_C UpdateOverlaps row.
- At most 3 components hold at least 70% of that self time.
- No `WARN card546` rows.

## Next

Read what each top component overlaps and why (overlap events, GenerateOverlapEvents on child shapes). Card a lever,
for example turning off overlap events on a component whose overlaps nobody reads, ranked by GameThread ms.

## Result c263L367 (b361)

- The top two rows are both Camera (CameraComponent): 572 calls with self 260.9 ms (456 us mean), and 808 calls with
  self 93.6 ms (115.8 us mean). The CollisionCylinder self is tiny (3.2 us mean). The true player overlap cost is
  about 356 ms per window (1.19% of GT), and the cameras hold 99% of it. Prediction 2 hit.
- Prediction 1 missed. Card 534 counts every UpdateOverlaps call, nested ones included, so its per-owner row counts
  nested time twice. Capsule inclusive 241.5 + camera 260.9 + camera 93.6 = 596.7, which is the card534 row. Use the
  card 546 self sums, not card534 owner rows, for overlap cost.
- Prediction 3 missed: `WARN card546` dropped 9431 calls (also 17 and 64) with 128 slots and probe 8. b362 grows the
  table to 1024 slots with probe 16.

Why the camera costs this much (static, USceneComponent::UpdateOverlapsImpl at RVA 0x3afef70): a non-primitive
component does no overlaps of its own. Its only work is the virtual UpdatePhysicsVolume (vtable +0x5a8) when
bShouldUpdatePhysicsVolume is set. Card 547 measures that call.
