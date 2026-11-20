# Card 544: performance mod, Chaos dynamic tree padding

Family: card 527 (performance mod; it must never change a host decision). Goal: GameThread headroom.

## Why

c263L364 host GameThread (b358, 10374 samples), Chaos scene query functions:

| Function | Share of GT | Kind |
|---|---|---|
| TAABBTree::QueryImp<2> (overlap) | 4.5% | query |
| TAABBTree::FindBestSibling | 4.3% | tree maintenance (reinsert) |
| TAABBTree::QueryImp<1> (sweep) | 2.7% | query |
| TAABBTree::QueryImp<0> (raycast) | 2.0% | query |
| TAABBTree::RotateNode | 0.9% | tree maintenance |
| TAABBTree::UpdateElement | 0.5% | tree maintenance |
| TAABBTree::UpdateAncestorBounds | 0.5% | tree maintenance |

Queries are 9.2%, which matches card 534's timed query share. Tree maintenance is another 6.2% that card 534 does
not see. A dynamic tree leaf holds the body bounds grown by `p.aabbtree.DynamicTreeBoundingBoxPadding` (engine
default 5 cm). Each move that leaves the padded box removes the leaf and reinserts it. A running bug leaves a 5 cm pad
almost every frame.

## What (b359, 575ecce67cf652a2)

- `perf_tree_c544.cpp`: once per process, on the first frame with a local PC, the PC console command sets the
  padding to 30 cm, on both host and client. The row `card544:` logs the cvar readback before and after.
- Query results do not change. The padding only widens broad-phase boxes, and every candidate still passes the
  exact narrow-phase test.
- Off switch: `UBWCOOP_C544_OFF=1`.

## Predictions (L365)

- The card544 row on both sides shows before 5 and after 30. If the readback is empty or unchanged, the command did
  not reach the console manager, and the run measures nothing.
- Host GT: FindBestSibling + RotateNode + UpdateElement + UpdateAncestorBounds fall from 6.2% to under 3%.
- Host GT: QueryImp<0..2> rises by less than the maintenance drop (L364 9.2%), and the Chaos total falls.
- Mismatches 0, CRASH 0, no new WARN kinds, drift at most 16.

If maintenance falls but queries rise by more, try 15 cm. If maintenance does not fall, the reinserts come from
something other than the padding (for example teleports, or bodies that are re-added each frame); trace UpdateElement's
callers.

## Result (c263L365, b359 575ecce67cf652a2)

Gate passed (230 s, mismatches 0 both sides, CRASH 0), so b359 is promoted to lane 1.

1. Hit. Both sides read back 5 before the set and 30 after.
2. Missed (under 3% was predicted). The tree maintenance functions went from 701 samples (6.8% of the host
   GameThread, all three UpdateElement symbols) to 416 (4.0%):

   | Function | L364 | L365 |
   |---|---|---|
   | FindBestSibling | 450 | 240 |
   | RotateNode | 93 | 35 |
   | UpdateAncestorBounds | 55 | 25 |
   | UpdateElement (3 symbols) | 103 | 116 |

   UpdateElement runs on every move, padded or not, so it does not drop. Reinsertion costs (FindBestSibling,
   RotateNode, UpdateAncestorBounds) fell by 50%.
3. Hit. QueryImp rose from 946 to 982 samples (+36), against a maintenance drop of 285.
4. Hit.

Net: about 250 samples, or 2.4% of the host GameThread.

The side label was wrong: the client row said "host", because the set runs at the main menu, before the session
flag exists. Since b361 the row prints no side.

## b361: padding 60 cm

Reinsertion still costs 300 samples (2.9%). b361 tries 60 cm to see whether reinsertion keeps falling faster than
queries rise.

Predictions:
- FindBestSibling + RotateNode + UpdateAncestorBounds fall below 200 samples;
- QueryImp stays within +80 samples of L365.

If QueryImp rises more than reinsertion falls, 30 cm stays.

## Result c263L367 (b361, 60 cm)

- Maintenance trio (FindBestSibling + RotateNode + UpdateAncestorBounds): 162 samples. Prediction hit.
- QueryImp: 1065 samples, +83 over L365's 982. Prediction missed by 3.
- The trio fell about 138 samples from 30 cm, and queries rose about as much. 60 cm buys nothing over 30 cm, and
  every query pays for the wider boxes. b362 goes back to 30 cm, which stays the shipped value.
