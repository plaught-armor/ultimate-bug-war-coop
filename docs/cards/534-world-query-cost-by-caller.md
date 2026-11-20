# Card 534: world collision query cost by caller (diag, card 527 family)

Status: done (c263L349, c263L350). Build b344 (build-b5/dwmapi-b344-dec3ac46fb77d562.dll = b342 + this card); b343 had the first cut, superseded because its report could name a class unloaded during the window. Diag stays in, gated by UBWCOOP_C132_FX=1.

## Why

The c263L344 host profile puts Chaos at about 26% of the GameThread: TAABBTree sweeps 7.8%, raycasts 3.0%, overlaps
1.8%, tree inserts 3.1%. That slice is the largest one without a named owner. The c263L346 branch stacks cannot name
the callers: QueryImp and FindBestSibling show 99% and 98% "no entry seen", because 16 branches do not reach back out
of Chaos. No lever is chosen until the callers are known.

## What

`mod/src/ubw/query_c534.cpp` detours 13 query entry points: the UWorld Sweep{Single,Multi,Test}ByChannel,
Sweep{Single,Multi}ByObjectType, LineTrace{Single,Multi,Test}ByChannel, OverlapAnyTestByChannel,
OverlapMultiByObjectType, ComponentSweepMulti and ComponentOverlapMulti, plus USceneComponent::UpdateOverlaps.
- Each game-thread call is timed (QPC, inclusive).
- The component calls are keyed by the component's owner class. Character movement sweeps its capsule through
  ComponentSweepMulti, so the bug classes show up there. The other calls are keyed by return address (exe RVA).
- Calls from other threads are only counted.
- Every 30 s the game thread logs `card534: <side> t=... query total <entry>` and `query top <n>` rows (the top 24
  by time), then clears the table.
- It is installed only with UBWCOOP_C132_FX=1, so play runs do not have it.
- Not hooked: UWorld::OverlapMultiByChannel (card 046 detours it) and UPrimitiveComponent::MoveComponentImpl (card
  461 detours it).

## Predictions (written before the run)

1. ComponentSweepMulti by the bug classes (BP_*Bug_C) is the largest row on the host, from character movement.
   Together the bug classes hold most of the 7.8% sweep share.
2. UpdateOverlaps by the bug classes is the second-largest component row. Moving capsules with overlap events
   enabled pay for it every move.
3. The line traces come mostly from a few exe RVAs (AI line of sight in UADTargetComponent, the weapons) and add up
   to about 3% of the window.
4. On the client, both component rows are smaller than on the host: client NPCs are moved by replication, which
   does not sweep (card 532 also stops the client target ticks).

## Proof

One c263L peak run with UBWCOOP_C132_FX=1. Score the predictions from the peak window rows on both sides. Each
prediction that fails is written here with its numbers. The lever card follows from the top rows.

## Reading the rows

Times are inclusive. UpdateOverlaps calls ComponentOverlapMulti, so the "query total" rows overlap and must not be
summed. Each entry's total is read on its own.

## Self-review (b344)

Attacked:
- the argument forwarding for entries with fewer than 9 arguments. The extra values are read from the caller's
  frame and passed on unused; every hooked entry returns bool and takes only pointers, references and enums.
- the top-N selection: fill, then replace the smallest and re-sort.
- key collisions: the entry index sits in the top byte, so a null component still gets a nonzero key.
- nested calls on the game thread: rows are written only after the original returns.
- class lifetime: b343 printed the class name from a raw pointer up to 30 s after keying it, and a streamed
  Blueprint class can be collected in that time. b344 keeps a weak reference per row and prints "(class unloaded)"
  when it no longer resolves.

## Results

c263L349 (b343) and c263L350 (b345, same card 534 code as b344), peak 30 s windows:
- P1 pass, partly. On the host, ComponentSweepMulti by BP_AI_WarriorBug_C is the top row: L349 2.49%, L350 2.35%
  (39144 calls, 705.0 ms). The sweep total is 3.37% (L350), not 7.8%. The rest of the Chaos sweep share is spread
  over CapsuleTraceSingleForObjects (exe+0x3d22dcf, 1.62%) and FloorSweepTest (exe+0x3a2bd3f, 0.79%).
- P2 fail. The second host row is UpdateOverlaps by BP_PlayerCharacterMI_C: 12067 calls, 634.1 ms (2.11%), about
  53 us per call. Warrior UpdateOverlaps is smaller.
- P3 fail. The line traces add up to 0.94% on the host (282.1 ms), not about 3%.
- P4 fail. The client pays more than the host. FloorSweepTest SweepSingleByChannel is 271904 calls and 3102.0 ms
  (10.33% of the client GameThread, 18 times the host's calls). ComponentSweepMulti by Warrior is 4.80%, and
  UpdateOverlaps by Warrior is 2.34% at 1.8 us per call (host 0.1 us).
  Cause: client NPC proxies run SimulateMovement every frame, which runs MoveSmooth (the capsule sweep) and FindFloor
  (the floor sweep). In NavWalking the capsule ignores WorldStatic and WorldDynamic, so every floor sweep finds
  nothing.

Lever cards: 535 (skip the client NavWalking floor sweep). Next: the client MoveSmooth sweep, the host Player
UpdateOverlaps (53 us per call), and the host CapsuleTraceSingleForObjects caller.
