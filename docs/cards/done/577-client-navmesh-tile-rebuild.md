# Card 577: client rebuilds navmesh tiles at runtime (performance mod family)

Umbrella: card 573. Found by the card 575 .pdata remap, 2026-11-21.

## Evidence

In c573b390b client combat, about 2300 Background Work samples sit in `FRecastTileGenerator::GenerateNavigationDataLayer`
(0x04af4200) and the builders it calls: `dtBuildTileCacheContours` (0x4a49c40), `dtBuildTileCachePolyMesh`, and
`dtBuildTileCachePolyMeshDetail` (helper 0x04a514c0 alone has 1617). The client runs no AI and no authoritative
pathing. A rebuilt navmesh does nothing for it.

## Fix (b397 `f9db0cab8fd0c1e7`, perf_navgen_c577.cpp)

Hook the virtual `FRecastNavMeshGenerator::RebuildDirtyAreas(const TArray<FNavigationDirtyArea>&)` (0x04AFF750).
When `g_client_session` is set, return without calling the original. The host always runs it. Both sides count:
`card577: <side> t=N RebuildDirtyAreas #K skipped|ran`, logging the first 8 calls, then every 256th.
`UBWCOOP_C577_OFF=1` means not installed.

The outer `UNavigationSystemV1::RebuildDirtyAreas(float)` is never skipped. It clears the dirty array after the
generator call, so skipping the inner call drops the areas and nothing accumulates. Skipping the outer call would
grow the array without bound.

Risk: the test driver's client `goto` paths on the navmesh. The cooked navmesh stays loaded. Only runtime changes
(dynamic obstacles, destroyed props) stop reaching the client copy.

## Predictions (one combat run with the c573 sampler)

1. Client logs `card577: client ... skipped` rows. Host logs `ran` rows at a similar rate.
2. In a matched combat window, client `FRecastTileGenerator` and `dtBuildTileCache*` samples are near 0 (from about
   2300). Client Background Work drops by about that share.
3. Client scenario `goto` still arrives. Card 132 pose mismatches stay 0. No new WARN lines.

## Budget

3 runs. A/B against `UBWCOOP_C577_OFF=1` only if prediction 2 is unclear.

## Result in run c397b398a (2026-11-21): passed

- The client logged 87 skipped rows and the host 87 ran rows.
- Client `FRecastTileGenerator` and `dtBuildTileCache*` samples were 0. Client Background Work was 2777 against the
  host's 6163.
- The client's scenario goto arrived 5 of 5 times. No new WARN lines from card 577.

Card 577 is closed. It moves with the other perf files at the card 578 split.
