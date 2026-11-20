# Card 535: perf mod, client NavWalking NPC proxies skip the FindFloor sweep

Family: card 527 (performance mod family). Status: in source, build b346
(build-b5/dwmapi-b346-380d70adf2e77ca6.dll + .map = b345 + this card). Not yet run.

## Why

Card 534 (c263L349, c263L350): the top client row is FloorSweepTest SweepSingleByChannel (exe+0x3a2bd3f), with
271904 calls and 3102 ms in the 30 s peak window. That is 10.3% of the client GameThread and 18 times the host's
calls.

## Static trace

- Client NPCs are simulated proxies. Each frame UCharacterMovementComponent::SimulateMovement runs MoveSmooth and
  then FindFloor (exe RVA 0x3A2B2A0, virtual). FindFloor calls ComputeFloorDist, then FloorSweepTest, then
  SweepSingleByChannel.
- In NavWalking (SetNavWalkingPhysics), the capsule ignores WorldStatic and WorldDynamic, so the sweep finds no
  floor. SimulateMovement then takes the no-floor branch (falling velocity, SetMovementMode(Falling)). Card 132
  holds the host's mode, and card 132's drift clamp keeps z at the host's value.
- FindFloor has an early out (exe 0x143a2b6a7 to 0x143a2b710), taken when HasValidData or the collision test
  fails. It writes FFindFloorResult::Clear and returns:
  - flags &= ~7;
  - FloorDist and LineDist = 0;
  - zero 0x10..0x110;
  - +0xb4 = -1, +0x14 = 1.0f;
  - +0xc8 = 16 zero bytes;
  - +0xd8 = {0 (8 bytes), -1, 0}.
  This is the same result a sweep that hits nothing produces.
- Not replicated: the cached-floor path's bForceNextFloorCheck update (+0x2e9 bit 0x10). It only runs with
  bCanUseCached and a moving base. Leaving the bit unchanged can only force a later full check.

## What

`mod/src/ubw/floor_c535.cpp` detours FindFloor. On the client, for a CMC whose MovementMode is NavWalking and whose
owner has LocalRole == SimulatedProxy, it writes the early out's stores and returns without sweeping. Every 256th
such call runs the original instead (into the real result) and counts results with bBlockingHit or bWalkableFloor
set. A `card535:` row prints totals every 60 s. The host always runs the original, so host decisions do not change.
Off switch for A/B: `UBWCOOP_C535_OFF=1`.

## Predictions (written before the run)

1. The client FloorSweepTest row (card 534, exe+0x3a2bd3f) falls from about 10% to below 0.2% of the client
   GameThread. The remaining calls are the verify runs plus non-NavWalking callers.
2. `card535:` client checks show found blocking and walkable floors at or near 0 (below 1% of checks). Any larger
   share means some NPC proxies stand on floors that the skip hides.
3. No new WARN kinds. Card 132 drift clamp rows are unchanged in count (within run noise). No AV.
4. The host has no `card535:` rows.

## Proof

One c263L peak run on b346 with UBWCOOP_C132_FX=1 (card 534 rows). Score P1 to P4 from the peak windows on both
sides. If P2 fails, turn the card off (UBWCOOP_C535_OFF=1) and trace the floors that were found.

## Self-review (b346)

Attacked:
- the calling convention: a virtual const member with (FVector const&, FFindFloorResult&, bool, FHitResult const*).
  The detour takes the same five arguments in rcx, rdx, r8, r9b and one stack slot, and forwards all of them.
- players: the local player is an autonomous proxy and remote players walk (MovementMode Walking), so neither
  matches. Only NavWalking simulated proxies are skipped.
- the host: g_client_session is false, so the original runs on every call.
- byte layout: the stores follow the early out's disassembly in order. The memset runs first and the later stores
  overwrite inside it, as the original does.
- threads: proxies move on the game thread. The verify step counter is game-thread state, and the totals are
  relaxed atomics.
- floors that are actually found (another pawn's capsule under an NPC, on the Pawn channel): the verify counts
  measure them (P2).

## c263L352 (b346, v1): FAIL, premise refuted

- P1 FAIL: the client FloorSweepTest peak row went to 46171 calls, 634.8 ms, 2.11% (L350: 271904, 3102 ms, 10.33%).
  The remaining sweeps come from Walking-mode proxies, which v1 did not skip.
- P2 FAIL: 1628 verify checks found blocking 411 and walkable 410 (50.4%). NavWalking proxies do find floors. v1
  therefore hid real walkable floors: SimulateMovement took the no-floor branch (fall velocity, SetMovementMode(Falling))
  instead of AdjustFloorHeight and SetBase. That is a client visual change, so v1 must not ship.
- The card 132 drift was unchanged: one stuck Warrior per run (L352: 16 rows, guid 1f2; L350: 14 rows).

## v2 (b348, build-b5/dwmapi-b348-7eeaca8d6b14eb60.dll + .map = b347 + v2)

Per proxy. A real sweep that hits nothing arms 3 skips for that movement component. A skip writes the same no-hit
result as before. A proxy whose last real sweep hit anything always sweeps. Walking and NavWalking proxies both
qualify. A new floor under a proxy without one is therefore seen at most 3 calls late (50 ms at 60 fps). A verify hit
disarms the proxy. The `card535:` row now reads: real sweeps (no hit N), skips, checks, found blocking, walkable.
The table is 1024 direct-mapped slots, game thread only. A collision or a reused address costs at most one sweep, or
gives at most 3 skips.

Predictions for v2:
1. Skips are about 3 times the real no-hit sweeps. Client FloorSweepTest peak calls fall by at least 30% against
   L350's 271904. The reduction is set by the no-hit share of the real sweeps, which the row reports.
2. Checks that find a walkable floor stay below 5%. A floor appears under a proxy within 3 frames of a miss only
   when the proxy lands or steps onto something.
3. The card 132 drift rows on the client show no new actor or pattern against L350 and L352.

## c263L353 (b348, v2): pass

- P1: client FloorSweepTest peak 148954 calls, 1875.9 ms, 6.25% (L350: 271904, 3102 ms, 10.33%): calls -45%, pass.
  Skips are 2.13 times the no-hit sweeps, not 3 (real sweeps 262482, no hit 109240, skips 232218). Some armed skips
  are never used: SimulateMovement skips FindFloor when the step-down already computed a floor or Velocity.Z > 0,
  and the proxy's next call then finds no armed slot left. That part of the prediction was wrong; it costs no correctness.
- P2: 910 checks found blocking 8 and walkable 6 (0.66%): pass.
- P3: card 132 drift 16 rows (Archer bb2, Warrior aa6). Archer, Warrior and Chariot drift rows appear in L340 to L349
  too, before this card: no new pattern, pass.
- Host FloorSweepTest 15372 calls, 0.90%: not touched.
