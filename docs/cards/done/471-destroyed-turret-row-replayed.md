# 471 — Destroyed turret's row is replayed; dead turrets fill the host turret table

Found 2026-11-16 by the senior in `c460b172b` (C3M1 checkpoint load): the one WARN card 467 left,
`WARN card467: client turret row BP_MountableTurret_C_2147468098 at (-1772, -10258, 820): no turret copy after 10000 ms`.

## Trace

- Host: turret spawned at (-1772, -10258, 820) 531643284, `row sent` 531643404, **destroyed 531662388** (the
  checkpoint load). The host log has no later spawn there in the run; the other spawned turret came back at 531686375.
- Client: its copy was destroyed 531663710; the join replay at 531673519 still carried the `_2147468098` row.
- Cause 1 (`world_c98.cpp`): `c98_host_turret` journals a 'T' row per turret; nothing removes it when the turret is
  destroyed (`c98_unjournal` runs for kinds P, M, E, A, K, S only). Every late joiner and every rejoin gets a row for
  a turret that is gone; before card 467 that was a `WARN card125 ... no turret matches`, now a 10 s retry and a WARN.
  The row also keeps a dangling actor pointer as its journal key.
- Cause 2 (`turret_sync.cpp` `c125_host_rows`): `g_c125s.row[kC125Turrets = 32]` is keyed by object index and never
  frees a slot. Each spawned AI turret that dies and respawns takes a new slot; once 32 are used a new turret gets
  `r == nullptr` and **no rows at all, with no log line**. C3M1 has 9 placed turrets plus spawned ones per load.

## Fix (`c471_patch.py`, senior scratchpad, `--check` ok: 6 edits)

Host DestroyActor detour calls `c125_host_forget(actor)`: frees the turret's slot and drops its 'T' journal row, one
plain `card471` row. `c125_host_rows` logs `WARN card471: host turret table full` once per map instead of silence.

Attacked: non-turret destroy (no slot, no work beyond a 32-entry scan); destroy before the first row (no slot yet,
no journal row either); object index reused by a new turret (slot freed first, new slot starts at `~0u`, row sent);
map change (table reset by world as before); swap-with-last while `c125_host_rows` iterates (destroy does not run
inside that loop: `c125_pack` and `c98_host_turret` destroy nothing).

## Done when

c460 C3M1 load run: host `card471 ... destroyed: row slot and journal row dropped` for each turret the load destroys;
0 `WARN card467`, 0 `WARN card125 ... no turret matches`, 0 `WARN card471`; the respawned turret gets
`card467`/`card271` matched rows; digest 0 mismatches.

## Sibling sweep (senior, static)

Every `table[n++]` append in `mod/src/ubw/*.cpp` with no WARN, full, dropped or peak text within 9 lines was read
(23 sites). The sync tables among them (`g_c24.relays`, `g_c100.id`, `c409_row`, `g_c43.e`) log or count a full
table; the rest are diagnostics. `g_c125s.row` was the only silent one.

## Closed

Closed 2026-11-17 (senior), build b173 `dedb14d000d6ec36`: c460b173b: `card471: host ... destroyed: row slot and journal row dropped, 9 slots in use`, turret respawned, 0 WARN card467.
