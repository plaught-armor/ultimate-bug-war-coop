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

## Cause

The host value is vanilla server state. The client value is the map default.

- Vanilla offset: every first attach row shows relative (0,0,800) on both sides (b11, c504b4-b6, and the first rows of
  both b366 soaks).
- Host only, after the per-map reset (card045 "respawn in place ... per-map reset") and the dropship's next
  StartAction: soak2m1b366b line 12926 shows relative (-23725,-4227,1201), and soak4m1b366a line 15481 shows
  `STAISpawner_1` at world (5383,-80043,-1313) under a dropship at (-19447,-70663,315), relative (26193,4292,-1628).
  The host's spawn logic moves spawners (card098 `MoveToArea caller exe+0x5143A0F` for STAISpawner and
  BP_AISpawner_C), and the spawner rides the dropship again from where it was put.
- The client never runs spawner logic: card 006 (guards.cpp) skips ASTSpawnerBase::PerformSpawn and QueueSpawnRequest
  on the client. Spawners are placed, not replicated, and have no mesh. So the client copy stays at (0,0,800) and
  nothing on the client reads it.

## Fix (b368)

`digest_c132.cpp`: `BP_AISpawner_C` and `STAISpawner` join the card 164 server-only list (`kC132ServerOnly`), next
to the spawn points. Both sides leave them out of the digest and count them in the card164 line.

The b367 client relative-copy (host `|R` rows from `c140_attached_visit`) was wrong: it moved the client copy to
match server state the client never uses. It is reverted, and b367 runs are not proof for this card.

## Prediction for the proof run (C2M1 and C4M1 soaks on b368)

- Every card132 client round reports no `BP_AISpawner_C` or `STAISpawner` mismatch.
- The card164 lines on both sides list `BP_AISpawner_C:<n>` and `STAISpawner:<n>` with n above 0.

## Closed 2026-11-21 (junior, from c559b371b)

c559b371b (b371, FED_C2M1) client: 26 card132 rounds, 0 BP_AISpawner_C or STAISpawner mismatch rows. The card164 line
lists `BP_AISpawner_C:120 STAISpawner:33`. Both predictions hold.
