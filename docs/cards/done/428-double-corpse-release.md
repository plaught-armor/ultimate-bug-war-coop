# Card 428: one corpse release row per corpse

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: Opened 2026-11-16 11:30 by the senior, from the WARN classes of run `c423b124a` (b124, FED_C2M1).

## Finding

The client logged 14 rows `WARN card132: recv N ... Pose 00000000 text R <guid> result skipped: no row`
(class not in `logs/b72-warn-classes.txt`).

Cause, from the source and the map totals: a dead NPC with a ragdoll holds two host rows, a pose row
(`pose_host_row`) and a rest row (card 269, `rest_host_step`). When the corpse goes, each table sends its
own `R <guid>`. The client handler (`c132_receive_pose`) drops the guid's pose, sprite and rest rows on any
`R`, so the second `R` finds nothing and is a WARN. The client's ledger does not print every applied row
(pose totals: 94 rows "removed by the host", 25 applied `R` rows printed), which is why the first `R` of a
pair is often not in the log. No visible effect: the corpse is already gone on both sides.

## Fix

Patch script `c428_patch.py` (senior scratchpad, `--check` passes against the b124 tree with the card 359
fix patch applied or not; it touches other lines). `mod/src/ubw/pose_c132.cpp` only: the pose row's `R`
send calls `corpse_host_released(guid)`, which removes the guid's host sprite and rest rows without a
second send. A pose row removed without an `R` (never sent) leaves the rest row to send its own, so one
`R` still goes.

Not covered, not seen in a log: a rest row that leaves the Dead state (revive, overkill) while its pose row
lives sends an `R` that also removes the client's live pose row; the pose row's later `R` would then WARN.
In `c423b124a` the one "left Dead" row had no NetGUID and sent nothing. If a play log shows
`card269: host rest row removed ... removal sent` followed by a `no row` WARN for the same guid, the
release row needs a table letter.

## Done when

On a FED_C2M1 run with the patch: 0 `text R ... no row` WARN rows on the client; the client's pose totals
"removed by the host" within a few rows of the host's pose "removed" total; no new WARN class.

## Second gap: a rest release while the pose row lives (2026-11-16, run c423b136a, b136; patch not built yet)

The case this card left open happened. Guids 0x4b8 and 0x4a4 (frozen warrior corpses) were gibbed at 499005799;
the host logged `card269: host rest row removed ... base state 5, removal sent` at 499005826 and the client logged
`WARN card132: ... text R 4b8 result skipped: no row` at 499006089 (sent 499006080), and the same for 4a4. The rest
row's bare `R` dropped the client's pose row 254 ms before the host's pose row went; the pose row's own `R` then
found nothing. 2 WARN rows in the run, no visible effect seen (the corpse was gibbed).

Fix (`c428b_patch.py`, scratchpad, 3 edits in `pose_c132.cpp`): `corpse_release_text` builds the sprite and rest
releases. While another host table still holds the guid (the pose row, or the other of sprite and rest) the release
is `R <guid> S` or `R <guid> T` and the client drops only that table's row (and does not clear the card 152 wait).
The last table to go sends the bare `R`, which drops whatever is left, as before. The pose row's `R` is unchanged.

Done when: a FED_C2M1 run with gibbed corpses shows 0 `result skipped: no row` WARN rows on the client, at least one
applied `text R <guid> T` row, and digest rounds after the gibs stay at 0 mismatches.

## Closed 2026-11-16 20:00 (senior)

Both parts are in the tree (`corpse_host_released`, `corpse_release_text` in `pose_c132.cpp`). Run `c423b159b`
(b159, FED_C2M1, gibbed corpses): 0 `text R ... no row` WARN rows on the client, 2 applied `text R <guid> S|T` rows,
digest 12 rounds at 0 mismatches. Done-when met.
