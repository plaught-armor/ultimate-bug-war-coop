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

## Follow-up: host seat's last row (junior, 2026-11-21, b423 `0f868ebb00718022`)

Seen in c582b414a, c587b422a and c587b422b (client): `WARN card467: client turret row BP_MountableTurret_Gunner_C_...
at (-77701, 1601, 4205): no turret copy after 10000 ms, dropped`. Trace (c587b422b): the host sends the seat's last
row (in use 0) at t=173145747, at unpossess, and destroys the seat at 173145754 (card471 row). The client's copy is
already gone when the row lands, so the row was held 10 s for a copy that will never come. Nothing visible: the row's
only effect would be the unpossess graph on a copy that no longer exists.

Fix in b423 (turret_sync.cpp, world_c98.cpp): the client remembers each spawned copy (not placed) a row was applied
to, by the host's name (16 entries, oldest replaced). When that copy is destroyed here, a held row for the name is
dropped, and a later row for it is dropped at once with a plain row "card467: client turret row ...: its copy was
destroyed N ms before, row dropped". A copy that comes back under the same name (net relevancy) refreshes its entry.
The map totals row gains "N for destroyed copies"; it stays WARN only for timeouts and a full table.

Prediction (next seat run, c587-c3m1-b422c as c587b423a): one destroyed-copy row for the host seat, 0 Gunner seat
timeouts. If the row lands before the client's copy goes, it applies normally and no row prints; that also passes.

Result, C3M1 c587b423a (b424 `784f6a4e07de92ac`, which carries the b423 fix): one row "card467: client turret row
BP_MountableTurret_C_2147462432 at (-19596, -17328, 1022) applied 3638 ms late". Totals: 1 held, 1 applied late, 0 timed
out, 0 table full, 0 still held, 0 for destroyed copies. The Gunner seat timeout WARN from c587b422b is gone (0
timeouts). The destroyed-copy path did not fire in this run (the row arrived before the copy went), which the
prediction allowed, so that path is still unproven in a live run.

New find, C4M1 mech c567b424a (b424): the client logged "WARN card467: client turret row BP_MountableTurret_C_2147468440
at (9425, -55430, 266): no turret copy after 10000 ms, dropped". It is the only card467 row in that run, and no other
C4M1 log has this WARN. The run ended about 35 s later without a map totals row. Not chased; it needs a static trace
(spawned turret out of the client's net cull, or a copy that never came).
