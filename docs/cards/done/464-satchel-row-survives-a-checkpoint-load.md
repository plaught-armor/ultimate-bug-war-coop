# Card 464: a satchel building's 'S' journal row survives a checkpoint load that revived the building

Senior trace 2026-11-16 22:29, run `c460b168a` (b168 `0c097c2488d82959`), FED_C3M1. Not a b168 regression.

## Evidence

- Host 526852219: `card098 ... world S BP_SatchelBuilding_Flakbug_C_2 ... sent`, client applied it live.
- Host 526857310 `travel checkpoint: LoadLatestSave`; the autosave (526847432) is older than the explosion, so the
  load revives `_C_2` on the host.
- Client rejoin replay 526869017: `world S BP_SatchelBuilding_Flakbug_C_2 ... applied ReceiveOnSatchelExploded ...
  journal replay`. Digest rounds 3 and 4: `BP_SatchelBuilding_Flakbug_C_2` dead host 0 / client 1.

## Cause

`c98_load_actor` (world_c98.cpp:837) rebuilds only the K and H rows from the actor after `LoadFromSlot`. An S row
(`kC98KindSatchel`) is never dropped, so the replay destroys a building the load brought back. Card 460 fixed the
digest side of the same pair; this is the journal side.

## Fix

In `c98_load_actor`: an actor with an S row whose Blueprint `Destroyed` bool (`c98_bp_destroyed`) is false loses the
row (`c98_unjournal(actor, kC98KindSatchel)`), counted in a new `C137LoadCounts::s_dropped` and printed by the load
totals row; per-row line `card137: host load reconcile c98 S <class> <name>: alive, row dropped` under the existing
cap. The other direction (destroyed by the save, no row) is already the K rule for a killable building; a satchel
building with no health component and `Destroyed` set gets an S row with value 0.

## Sibling sweep (report with the fix, static)

World journal kinds with no load rule: L, B, O (timelines), T (turret), A (placed moved), G (secret), F (closure),
C (range target), Q (card 408), P / D / M / N (card 104 copies), and the mechanisms with no `after_load` in
`kSyncMechs` (objective, ext, pose, decal, effects, bugform, script). For each: does `LoadFromSlot` change the state
the row describes, and does the row then differ from the world? Rows only for the ones that do, each with its rule.

## Prediction

`c460-load-c3m1.txt`: host `card137: host load reconcile c98 S ... BP_SatchelBuilding_Flakbug_C_2: alive, row
dropped`; no client `world S BP_SatchelBuilding_Flakbug_C_2` row in the rejoin replay; 0 digest mismatches after the
load.

## Done when

The prediction holds in one c460 run and the sweep is reported.


## Closed 2026-11-16 22:48 (b169 `d99dcd4bd6123245`)

Run `c460b169a`: host `card137: host load reconcile c98 S ... BP_SatchelBuilding_Flakbug_C_2: alive, row dropped`
(528887294), reconcile row S dropped 1. The client still got an S row for `_C_2` in the rejoin replay, and that row
is correct: the save was taken mid-fuse, the load restored the armed fuse, the building blew up again on the host
(`ObjectiveComplete(Resort_SO3_B)` 528887830) and the host journalled S again. Digest rounds 1 to 4: 0 mismatches.
The prediction "no S row after the load" holds only for a save outside the fuse.

Sibling sweep (junior, static): rows rebuilt from live state or dropped on destroy are safe (O, A, G, C, Q, P, M, N,
D, objective, ext, pose, decal, effects, bugform, script). Possibly stale after a load, no rule yet, no route to
prove it: L/B (timeline position, card 137 open item), T (turret moved by a load with no change event), F (closure
reopened by an older checkpoint). Kept as candidates in `docs/STATE.md`.
