# 499: Client completes BrainBug_O1 early (native parent check runs before the host's nested AddSubObjective)

Status: open (written 2026-11-19, senior). Batch into b258.

## Finding

c245b257a (b257 `6b5faf2a31aafe19`, FED_C4M2): digest rounds 14-19 report
`mismatch STLevelObjective O:BrainBug_O1 complete host 0 client 1`. The client completes BrainBug_O1 at 729847790
(live) and again in both rejoin replays (729859831, 729875691). The host completes it at 729960088. At map end SO4/SO6
read host 1 client 0 with "no row sent" (watch item; check again after the fix).

## Trace (senior and junior, c245b257a against c245b229a)

- Host: the SO2 OnCompleted handler calls `AddSubObjective(BrainBug_O1, BrainBug_SO3)` (729847767) inside the outer
  `CompleteSubObjective(BrainBug_O1, BrainBug_SO2)`. When the native parent check runs, SO3 exists and is open, so
  O1 stays open.
- Card 497 (b253) sends the outer complete first, then the nested rows: journal 119 Complete SO2, journal 120 Add SO3.
  In b229 the add went first (120, then Complete 121), so this is a b253 regression; b254-b256 runs never reached
  BrainBug.
- Client: applies Complete SO2 first. The count is 4/4, `USTLevelObjective::OnSubObjectiveComplete` (0x5165560, caller
  exe+0x51655C9) finds every sub complete and completes O1 natively (card097 depth 2). The Add SO3 row arrives 1 ms
  later, to an already complete parent.
- Research_O1 (card 497) needs the opposite order: there the handler adds SO2/Marker2 after O1 completes, and the
  client must not cascade into them. No single row order serves both, so reordering again is not the fix.
- Park_O1 and Stadium_O1 also complete via 0x51655C9 on the client, but the host completes them in the same call, so
  they match. The fix must keep those.

## Fix (class: mirror the host's completion set, not its order)

- Host (mod/src/ubw/objectives.cpp, card 491/497 queue): during the outermost mirrored call, record each objective the
  host's native code completes (the same points that log the card097 native rows). Send that set with the outer row
  (a count plus objective names or ids; bounded, reuse kC491Nested 64 as the cap, WARN on overflow and send without
  the filter).
- Client: while replaying that outer row and its nested rows, a native complete (c100_skip_local path, or the
  OnSubObjectiveComplete parent path) of an objective not in the host's set is skipped. One row per skip:
  `card499: client t=<ms> blocked native complete <objective> (not in host set of <n>, outer <call>)`.
- Applies the same on live rows and on join replay rows.
- Research_O1 gets the same treatment without needing its order: SO2/Marker2 are not in the host's set.

## Proof (written before the run)

- Next c245 (FED_C4M2) run: one card499 blocked row for BrainBug_O1 at the SO2 completion (live and each rejoin
  replay); 0 `card132: mismatch ... BrainBug_O1` rows; client BrainBug_O1 completes only after the host row near the
  host's own completion.
- card491 BrainBug_SO2 score: client gain equals the host gain (+10000, not +25000). Run with UBWCOOP_C99_LOG=1 so the
  card 491 per-pawn rows print.
- Park_O1 and Stadium_O1 still complete on both sides with 0 card499 rows for them.
- c497 or the next C5M2 soak: still 0 `mismatch ... Research_` rows.

Reopen when: any objective digest mismatch with client 1 and host 0.
