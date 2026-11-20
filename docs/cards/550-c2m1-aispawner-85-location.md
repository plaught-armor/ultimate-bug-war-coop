# Card 550: C2M1 BP_AISpawner_C_85 location mismatch (card 132 gate)

Found in soak2m1b366b (b366, e62e76f63a05d88f), client log `logs/soak2m1b366b-client.log`.

## What the log shows

- From card132 client round 5 (line 12673) to the run end, 27 of 31 rounds report
  `mismatches 1 (classes BP_AISpawner_C:1)`. Rounds 1-4 were clean.
- Detail row: `mismatch BP_AISpawner_C BP_AISpawner_C_85 location (one side attached?) host (-23700,-4250,1200)
  client (0,0,800); no row sent`.
- card237: key 9cb55c2e, level FED_C2M1_Scripting, at (207037, -2287, 10790), mesh none, hidden 0.
- The host location looks like a relative location under an attach parent, and the client one like an unplaced
  default. One side has the spawner attached and the other does not, or the host moved it at run time.

## Player effect

None seen: the spawner has no mesh, and bug spawning is host-only. The gate is still 0 mismatches, so b366 fails it on
this one row. b366 was released for the playtest with this noted in the release text.

## Same pattern on C4M1 (soak4m1b366a)

`STAISpawner_1` location, host (26200,4300,-1650), client (0,0,800), 17 of 21 rounds. It is the same row as c244b364a
(b364, 17 rounds), which STATE.md calls the known card 218 load-path row. In both soaks the mismatch starts in the
first digest round after a `card159: client dropship totals (map end)` row (C2M1 93643585 then 93650022; C4M1
95155431 then 95160710). Client (0,0,800) is the same client value card 218 saw for dropship spawners.
Neither run had a death-screen load, so by card 218's reopen rule this is card 218 fault 2 on a new route (map
travel), or a new fault. Spline watch: card159 rows show no unresolved splines.

## Next step (static first)

Find who moves or attaches BP_AISpawner_C_85 in FED_C2M1_Scripting (kismet listing of the level script, as in card
241's spawner table), then decide: send its location (card 132 row), or mark it server-only like card 164 if no client
reads it. Prediction for the fix run: C2M1 soak rounds all `mismatches 0`.
