# 282 Client "local sim" change table split into load time and play time (diagnostic, build 5)

Source: `docs/SYNC_AUDIT.md` entry 6. `change_c137.cpp` counts every change the client's own game logic makes to
a host-owned map actor (cause "local sim"), but load-time setup fills the table (FED_C3M1: `BP_AISpawnPoint_C`
324, `BP_AISpawner_C` 87), so the play-time rows, which are the places the client still acts as a second owner,
are never read. Diagnostic only; changes no game state.

## Spec

- `ChangeRow` gains a phase (load, play). Phase is play from the first Pong applied on this map (the same moment
  the digest starts), load before it. Reset with the table on map change.
- A play-phase "local sim" change logs as `WARN card137: client play-time change <class> <name> <field> <old> ->
  <new> by local sim caller exe+0x...`, first `kLinesPerRow` per (class, field) as today. Load-phase rows keep
  the plain row. "host row" and "own input" causes are unchanged.
- Totals row gains the two local-sim counts: `local sim load N, play M`.
- Audit entry 9 rides in the same table: a client effect spawn outside a host row (the card 136 spawn
  chokepoint) counts as field `fx spawn`, play phase only, no per-row line (count in the totals dump).
- `kChangeRows` 128 stays; the phase is part of the row key, so check the FED_C3M1 row count against 128 in the
  first run and raise the constant if the overflow counter is not 0.
- Files: `mod/src/ubw/change_c137.cpp`; one call in `fx_c136_net.cpp` for the spawn count. Nothing else.

## Proof and use

Rides in build 5's first proof run of any map; budget 0 extra runs. Prediction (FED_C3M1): play-phase rows for
`BPHazardousZone_Shunt_C`, the destructible corpse and beach table classes, and pickups; no play-phase row for
the spawner classes. Senior reads the WARN diff and cards each class as "client sim to cut" or "local by
design"; a class judged local by design is listed in the audit entry so the WARN can be lowered to a note.

## Closed (2026-11-17 13:05): merged into card 149

Same diagnostic family (what the client's own game logic changes or runs). Its spec is carried in full in card 149; never built on its own.
