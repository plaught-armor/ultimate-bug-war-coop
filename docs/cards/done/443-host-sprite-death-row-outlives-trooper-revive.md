# Card 443: the host's sprite death row outlives a trooper revive

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: Opened 2026-11-16 15:31 by the senior, from a new client WARN class in run `c423b139a` (b139, FED_C2M1).

## Finding

`WARN card177: client sprite death BP_AI_Trooper_Morita_C guid 0xdec: no host row 3002 ms after it arrived` and the
read-back WARN after it (client showed `Death02`, the fallback name `Death`). The trooper died at 502533569 (row
`Death01` received), was revived, died again, and its copy arrived dead on the client at the mission end with no row.

Cause: `sprite_host_step` (`pose_c132.cpp`) tests a revive with `ASTNonPlayableCharacterBase::CurrentHealth`
(+0xAC0), which stays 0 on a revived Morita trooper. The client's revive edge (`c37_pawn`) reads the health attribute
set and drops its row (`c318_sprite_revive`). Host totals of the run: 61 rows added, 52 removed, **0 revived**, 9
`at a second death start` updates; the client logged 17 revives. So the host row lives through the revive, and at the
second death:

- same variant name as the first death: `sprite_host_update` sends nothing; the client has no row, so a copy that
  arrives dead falls back to the local `Death` (possibly another variant than the host shows);
- another variant name: the update is sent with the first death's age (`host age 11865 ms` for a death that had just
  started), so an arriving copy is set to the rest state instead of playing.

A copy that is live on the client at the second death plays its own death, so this shows only on copies that arrive
dead (relevancy, rejoin, late join). A late joiner also gets rows for troopers that are alive.

## Fix

`c177r_patch.py` (senior scratchpad, 1 edit, `--check` clean on the b140 tree): `sprite_host_step` reads
`c23_set_health(npc)` (the attribute set, as the client does). A revived trooper's row is dropped with its `R`, and
the next death adds a new row with its own start time.

## Done when

On a FED_C2M1 run with troopers revived: host totals show `revived` above 0 and about the client's revive count;
host `card177: host sprite row removed ... revived` rows; `at a second death start` updates only for a real second
sequence on a corpse (OverKill); 0 `WARN card177 ... no host row` for a trooper that died twice; 0
`result skipped: no row` WARN rows for sprite guids.
