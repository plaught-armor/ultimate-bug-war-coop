# 467 — Spawned turret row arrives before the client's copy after a rejoin

Found 2026-11-16 in `c460b170a` (C3M1 load run).

## Trace

- Before the load: `card271: client turret row BP_MountableTurret_C_2147466762 at (-3638, -12891, 820) matched spawned
  turret ... at 1 uu` (530455511) and `_2147465780 at (-1772, -10258, 820)` (530463642). Works.
- After the checkpoint load and rejoin: 530485321/530485322 `WARN card125: client turret row ... no turret matches,
  skipped` for both. The client's TurretTrooper copies arrived at 530485124/530485128; the spawned turret copies
  were not in the world yet when the join-replay rows ran (`c271_spawned_turret` found none within 200 uu).
- The row is one-shot: the turret's yaw, heat, in-use and ready state stay wrong on the client until the host's
  next change row for that turret.

## Fix (junior)

`turret_sync.cpp`: a 'T' row with no match is kept in a small pending table (8 rows, newest per name wins) and retried
each client tick for 10 s; applied on match with a plain `card467: client turret row ... applied N ms late` row; one
WARN on timeout or table full. Cleared at map end.

## Done when

`c460` C3M1 load run: 0 `WARN card125 ... no turret matches`; a `card467 ... applied N ms late` row for each spawned
turret after the rejoin (or no late row at all if the copies beat the rows); 0 mismatches.

## Closed

Closed 2026-11-17 (senior), build b173 `dedb14d000d6ec36`: c460b173b: turret respawned after the load, 0 WARN card467, 0 WARN card125.
