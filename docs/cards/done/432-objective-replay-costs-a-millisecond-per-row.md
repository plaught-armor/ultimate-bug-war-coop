# 432 — Client objective replay costs about 1 ms per row; the host repeats unchanged visibility rows

Opened 2026-11-16 by the senior from the efficiency audit (card 329 method: log numbers first).

## Measured (c423b127b, FED_C2M1, client `card330 ... recv (outside mod total)` rows)

| 30 s window | Objective frames | ms | max ms |
|---|---|---|---|
| t=490761553 | 78 | 81.8 | 10.95 |
| t=490851570 | 24 | 27.5 | 6.98 |
| t=490911583 | 155 | 112.0 | 6.23 |

About 1 ms per replayed row, and the level script sends them in bursts: 9-10 `SetObjectiveVisibility` rows in one
host tick, often the same burst again 28-116 ms later, so the client takes a 10 ms hitch per burst. Host side of the
same run: 95 `SetObjectiveVisibility` sends, 27 equal to the last row sent for that id (20 of them with no other
objective op between). c399b122a (C3M1): 12 sends, 0 repeats. The reconnect replay sent 323 Objective rows
(18,206 B) because the journal holds every visibility call.

Other client recv costs seen in the same rows, not carded yet: GrenadeSpawn about 185 us per row (86 ms for 477
rows), GibPart spikes of 8.78 and 11.15 ms, Fx max 9.55 ms.

## Cause

`detour_c49_visibility` mirrors every call. The two bools are marker and list visibility (`kObjectiveMarker` 0x68,
`kObjectiveInList` 0x6A; the snapshot path builds the same bits from those two fields), so a call whose target
already holds both values changes nothing a client needs. Where the client's 1 ms goes (the game's own function or
the handler's checks and log lines) is not known yet.

## Fix (patch `c432_objective_patch.py`, objectives.cpp only)

1. Host: `c432_visibility_same` reads the two fields before and after the original call. Same before and after: no
   row, no journal entry; counted, first 8 logged as `card432: host SetObjectiveVisibility(...) changed nothing`.
   The objective must exist, so a call for an id the host does not hold is still sent with its exists bit clear.
2. Client: `c432_note_replay` times the game's call inside `c49_receive`. A call of 2 ms or more gets a
   `card432: client replay ...` row (32 at most); every 64 rows one `card432: client objective replays` totals row.

## Done when

- C2M1 run: host `changed nothing` count near 27 of 95, Objective frames on the client lower by the same count,
  objectives digest 0 mismatches, client objective list and markers equal to the host's at the end (existing join
  and digest checks), reconnect replay row count lower.
- The totals row splits the recv time: if the game's call holds most of it, the remaining cost is the game's and the
  card closes with the dedup; if the handler holds it, a second step trims the handler (c53/c305/c253 lines).

## Risk looked at

A client whose objective differs from the host's before an unchanged host call no longer gets healed by the repeat.
The reconnect snapshot still writes both bits from the host's fields, and every change on the host is still sent.

## 13:43: step 2, the handler's by-name find (patch written)

Run `c399b134a` (b134), client: 97 Objective frames cost 226.5 ms in the `card330 ... recv` rows, but the game's
own calls only 33.8 ms (`card432` total, 64 rows). The rest is `c70_take`: each Add with an ObjectiveActors names
frame resolves its names with `c64_find_actor`, which walks every level actor (25000 to 28000 on these maps) and
reads two names per actor. 46 such Adds, one name each; the `card070: client t=` rows of one burst are 4 to 5 ms
apart, so a find is about 3.8 ms (46 x 3.8 = 175 ms of the 193 ms gap). Card 407's name index was never used here.

Fix (`c432_step2_patch.py` in the scratchpad, `mod/src/ubw/objectives.cpp` only): `c432_find_named` asks
`c407_named`, takes the lowest (level, slot) hit (the walk's pick), and walks only with no index answer. The first
16 answers per process are compared with the walk (`WARN card432: name index gave ...`, walk kept). Used by
`c70_take` and by `c65_receive_activate` (same call form; 0 Activate frames in this run).

Not covered: a runtime-spawned actor with the same object name ahead of the placed one in level order would be the
walk's pick but not the index's; the host sends level-placed actors only, and the 16 checks would show it.

Done when (b135 chain run, client log): 0 `WARN card432: name index` rows; `card407 ... finds` per mission map
up by the Add count, all answered; Objective recv per frame in the `card330` rows under 1 ms (2.3 ms before);
`card070 ... names sent 1, actors found 1` rows unchanged in count.

## Closed 2026-11-16 (senior, C2M1 whole-map run `c423b163a`)

- Step 1: host `card432: host map totals: SetObjectiveVisibility not sent 29 (changed nothing), sent 66` (predicted
  near 27 of 95). Objectives digest 0 mismatches in all 5 rounds.
- Step 2: 0 `WARN card432: name index` rows. Client `card330` recv windows: `Objective 88 frames 77.4 ms max 7.55`
  (0.88 ms per frame) and `Objective 124 frames 22.7 ms max 4.15` (0.18 ms per frame); before: 2.3 ms per frame.
  The game's own calls: 192 rows, 40.4 ms, max 1.58 ms.
- Left, not carded: one 7.55 ms frame in the first window (the game's call max is 1.58 ms, so about 6 ms is the
  handler on one Add frame). It goes to the card 330 per-frame list if a later run repeats it.
