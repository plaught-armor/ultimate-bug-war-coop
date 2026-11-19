# 341 Digest slot sum differs on an entry the compare accepts

Found in the b25 run of `c335-range-load.txt` (`logs/c335-range-client.log`), 2026-11-14. Efficiency (card 329
family), no visible fault.

Rounds 7 to 10, all after the checkpoint load: `whole sum differs, slot sums asked`, `1 slots differ, 1 asked`,
then `mismatches 0 ... slots differing 1 (entries asked 12 host, 12 client)`. Rounds 1 to 6: 0 slots differing.

## Cause (static, `digest_c132.cpp`)

`c132_entry_hash` hashes the exact location steps (`loc[0..2]`). `c132_compare_pair` accepts a pair whose steps
differ by `kC132LocTolerance` or less. One entry in that slot sits inside the tolerance but not equal after the
load, so every round pays the slot sums ask (3 frames) and the detail (2 frames) and finds nothing.

Not determined: which of the 12 entries (no row names an accepted unequal pair).

## Step 1 (diagnostic)

In `c132_compare_pair`, a capped row for a pair that is unequal but inside the tolerance:
`card341: client round <n> key <k> <class> <name> within tolerance: host (x,y,z) client (x,y,z)`.

## Fix (after step 1)

By what the entry is: if it is a range target whose timeline position rounds differently on the two sides, the
client's apply should end on the host's step (card 335 family). A hash over coarser location bins is not the fix:
a pair straddling a bin edge still differs.

## Prediction

Step 1 names a `TargetRangeTarget` or `TacSuppRangeTarget` entry on rounds after the load.

## Step 1 result (b27 `0ebdac007a685d9c`, run 16 of `c335-range-load`)

- Round 5 (before the load): `TargetRangeTarger_C_3` and `_C_4`, host z -150, client z -100. One round only.
- Rounds 7 to 10 (after the load): `TacSuppRangeTarget_C_24`, host y 23550, client y 23600, every round.

The prediction held for the class, and missed in one way: a pair can also differ for one round before the load
(a moving target sampled one step apart).

Not determined: the real distance between the two copies of `_C_24` (the rows carry steps of 50; a real gap near
0 at a step edge and a gap of 50 read the same). Either is inside the compare's 100 uu.

## Fix (b28, client)

`c341_adopt_set`: when the compare accepts an unequal pair, the client keeps the host's steps for that key and level
(`kC341Adopt` 32 rows). `c132_sum` hashes the adopted steps in place of the client's own while the own steps stay
within `kC132LocTolerance` of them; an entry that leaves the tolerance frees its row and hashes its own steps
again. The compare itself is unchanged.

## Prediction (`c335-range-load.txt`)

After the load: one `card341` row for `_C_24` on round 7, and rounds 8 to 10 end on the whole sum (0 slots
differing, no slot sums asked). 0 `WARN card341`.

## b28 result (`c18852793a3d72b2`, run 18 `c335-range-load`)

Prediction met. One row: `client round 7 key ce5486fa ... TacSuppRangeTarget_C_24 within tolerance: host
(11700,23550,-50) client (11700,23600,-50)`. Round 7: whole sum differs, 1 slot, 12 entries asked, 0 mismatches.
Rounds 8 to 10 and 1 to 6: 0 slots differing, 0 entries asked. 0 WARN card341. Junior's addition, accepted: the
adopt table is cleared in `c132_arm_map`, so rows of a left map never fill the 32 rows. Accepted 2026-11-14.
