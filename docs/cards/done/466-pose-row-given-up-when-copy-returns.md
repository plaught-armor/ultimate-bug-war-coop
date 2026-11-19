# 466 — Corpse back in relevancy loses its pose row on arrival

Found 2026-11-16 in `c460b170a` (C3M1 load run): client digest round 2 `mismatches 2 (classes Pose:2)`, `2 gave up`.

## Trace (guid 0xb4, Aphid; 0x8c is the same)

- 530422456 client ragdoll + stream start, snap 1 applied.
- 530427523 client channel closed, reason 3 (relevancy). Copy gone.
- 530428280 host `pose resnap guid 0xb4 snap 2 settled`. Client stores the row; its guid is unmapped, so it waits
  (`pose_client_step`: unmapped rows have no deadline).
- 530444719 the copy comes back (`IsDead 0 health 200.0`, its death edge not replayed yet).
- 530444730 `WARN card132: client pose guid 0xb4 snap 2 not applied after 16424 ms: ... its death has not run`.
  The 15 s `kPosePendingMs` deadline counts from `r.got` (row arrival), so the unmapped wait used it all up and the
  row was dropped on the first tick the copy existed.
- 530444771 `card137 ... held hidden: no host pose row yet`; 530447280 `WARN card152 ... its own ragdoll started`;
  530453778 `WARN card137 ... not posed after 9006 ms`. Corpse rests in a client-only pose: Pose mismatch.

Not a b170 regression: the code path is unchanged; `c460b169a` had no corpse return more than 15 s after its final row.

## Fix

Scratchpad `c466_patch.py` (1 edit, `pose_c132.cpp` `pose_client_step`): while a row's guid is unmapped, `r.got = now`,
so the deadline starts when the copy arrives.

## Done when

`c460` C3M1 load run on the fixed build: 0 `not applied after` WARN with `its death has not run`, 0 `WARN card137 ...
not posed`, digest rounds 0 Pose mismatches, `gave up` 0. If no corpse returns after 15 s in that run, the proof is
owed to the next run that has a `reason 3` close followed by a re-arrival over 15 s later.

## Closed

Closed 2026-11-17 (senior), build b173 `dedb14d000d6ec36`: c460b172b, c460b173b: 0 `its death has not run`, 0 `WARN card137 ... not posed`.
