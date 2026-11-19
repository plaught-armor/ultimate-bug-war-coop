# 233 — Turret rows for turrets spawned at run time find no turret on the client

Soak runs c127-c4m1 (4 lines) and c127-c4m1-r2 (2 lines), DLL `20cf859dd191a8fe`, FED_C4M1: `WARN card125: client
turret row BP_MountableTurret_C_2147472749 at (21470, -95170, -874): no turret matches, skipped`
(`mod/src/ubw/turret_sync.cpp:574`), each with a skipped WorldOp line. The turrets are spawned at run time where a
TurretTrooper dies; their names carry a run-time id that differs per machine, so the client's match by name and
place fails. A player would see: on the client such a turret may stay in its old state (mounted, aim, destroyed).

Static first: how the client gets its copy of a run-time turret (replicated actor or its own spawn), and which key
both sides share (net GUID, class plus place within a radius). Then one run on `tools/scenarios/c127-c4m1.txt`.

Done when: the run shows no `no turret matches` line and the row is applied on the client.

Senior trace 2026-11-12: `c125_client_turret` (`turret_sync.cpp:567`) finds the copy with
`c95_find_nearest(world, name, at)`, written for placed turrets whose name is the same on both machines. For a
run-time turret the name misses. Open question for the card: does the client hold a replicated copy of that
turret at all? If yes, match by turret class nearest to `at` within a small radius when the name misses; if no,
the gap is the spawn itself, not the row.

## Step 1 answer (senior, 2026-11-13): same class as card 271, already coded

- The client does hold a copy: `logs/c127-c4m1-r2-client.log` has `card098: client spawn BP_AI_TurretTrooper_C at
  (21470, -95170, -871) placed 0 replicates 1` and the turret weapon attach rows, at the place of the failing row.
- Card 271 (build 2, FED_C3M2 TurretTrooper posts) added exactly the open fallback: `c271_spawned_turret`
  (`turret_sync.cpp:572`), nearest non-placed turret within 200 uu when the name misses. It is map-independent.
- No code for this card. It closes with card 271 when the FED_C4M1 soak (card 244) shows `card271: client turret
  row ... matched spawned turret` for each run-time turret and 0 `no turret matches` rows.
