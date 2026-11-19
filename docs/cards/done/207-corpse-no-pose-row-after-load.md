# Card 207: A corpse the host holds after an in-place load has a pose row

Status: queued 2026-11-12, from c142-r4. Budget: three runs. Static first. Related: cards 194, 203 (rejoin
corpses), card 152 (owner of the hold).

## Why

c142-r4 client, after the rejoin and a warp to (12500, 14100, 400): three lines "WARN card152: client no stream
row for guid 0x292 / 0x27c / 0x274 (BP_AI_WarriorBug_C) 2012 ms after its game ragdoll start was held: its own
ragdoll started". The three warriors were killed on the host at +84 s (scenario lines 55 to 57), before the host's
in-place checkpoint load. At the rejoin the host logged "pose replay to a joining PC: 0 rows (0 tracked)", and
round 5 "0 sent corpses checked". The client's copies arrived dead ("card137: client corpse arrival ... held: no
host pose row yet"), waited 2 s, then ragdolled on their own, so each corpse lies where the client's physics
put it, not where the host has it.

## Do

1. Trace from the two logs and the code, no run: what the host holds for these three after the in-place load
   (the same actors, or corpses respawned by the save), why the pose table has 0 tracked (cleared at the load, or
   never filled because the death ran before a NetGUID or inside the load), and which host path owns a row for a
   corpse that exists without a death edge in this session.
2. Fix in the pose owner for the class: every dead NPC the host holds with a ragdoll has a tracked row that the
   join replay and a late relevance arrival both get.
3. One run on `tools/scenarios/c142-r4.txt`: no "no stream row" WARN, round after the warp checks 3 corpses,
   0 over 100 uu.

## Done when

- Trace, fix, run lines. The Report ends with "Report complete."

## Step 1 answers (c142-r4 logs, `pose_c132.cpp`, `enemies.cpp`; senior, 2026-11-13)

- The three are new actors the save spawned dead, not the ones killed at +84 s. The old rows left with their
  actors (`host pose remove guid 0x250 (actor gone, 3 snaps)`, 115690623), so `0 tracked` is correct for the table.
- Host, inside `LoadLatestSave` (115689048): each warrior gets `Health 400.0 -> 0.0`, `mesh hidden 1`, `static mesh
  ... IsDead 1 ... death edge 0 -> host dead`. At 115690701 to 115690735 the game shows the mesh again (`mesh hidden
  0 ... simulating 0`, caller exe+0x5103063). No `SetAllBodiesSimulatePhysics` row follows: the host's corpse is a
  visible mesh that never ragdolls.
- A pose row is created in one place only: `c132_pose_track`, called from the ragdoll start detour
  (`enemies.cpp:2695`). A corpse with no ragdoll start has no row, so the join replay and the card 137 owed list
  have nothing for it.
- Client (115759244 on): the copy's death edge runs, its game ragdoll start is held 2 s for a stream row, then
  released (`WARN card152 ... its own ragdoll started`). The client's corpse falls limp; the host's does not.
- The sample step already handles such a row: `pose_host_row` (`pose_c132.cpp:718`) counts a mesh that is not
  simulating as settled and snapshots it on the next step; a row with guid 0 is owed and sent when the corpse
  has a NetGUID (card 137).

## Fix spec (step 2)

- `enemies.cpp` `detour_c132_set_hidden`: on the host, after the call, when an NPC mesh goes hidden 1 to 0, the
  owner is dead, the mesh is not simulating and `pose_find_actor` has no row, call `c132_pose_track(mesh)`.
  Expose the row test from `pose_c132.cpp` as one small function; no other change to the pose owner.
- Log (capped under `kPoseLines`): `card207: host tracked a corpse shown without a ragdoll: <class> guid 0x...,
  bodies N, update type U`.
- By class: any dead NPC the game shows without a ragdoll start, whatever spawned it (save load, streaming).

## Predictions (one run of `tools/scenarios/c142-r4.txt`)

- Host: 3 `card207` rows after the in-place load, update type 0, bodies 22; the rejoin's `pose replay` reports 3
  rows (or 3 owed rows that go out when the guids exist).
- Client: 0 `WARN card152 ... no stream row`; each of the three copies gets `pose applied ... frozen`; the digest
  round after the warp checks 3 corpses, 0 over 100 uu.
- If update type is 2 (SkipAllBones) the host's bodies do not follow the shown mesh and the sent pose is not what
  the host shows: stop, report the row, and the pose is then taken from the bone transforms in a follow-up.
- Budget: 1 run, batched with cards 194 and 190 (same rejoin class).

## Senior ruling, build 4 review (2026-11-13): one owner per corpse

Card 269 (build 4) gives a 3D corpse that reaches Dead with no ragdoll a rest row. This card's
`c207_track_shown_corpse` would give the same corpse a pose row. Ruling: the rest row owns every corpse with no
ragdoll start; the pose row owns ragdolls only. The track here skips a corpse when `rest_find(0, npc)` has a row,
and `c269_state_host` already skips one with a pose row. Card 197's Chariot evidence predates card 269: re-read
197 against a build 4b log first; what card 269 covers is struck from it. Both go to build 5, coded together.

## Run c207b155a (b155 `09fa0ac06c00c9d1`), 2026-11-16

- Host half proven: 3 `card207: host corpse shown without a ragdoll or a death edge` rows 100 ms after
  `LoadLatestSave returned 1`; rejoin `card269: host rest replay to a joining PC: 3 rows sent of 3`; client 3
  `card269: client rest arrival ... played`.
- Client half failed: 2.2 s after the arrival the game's ragdoll start (caller exe+0x37C53CB) reaches each copy.
  No pose row exists, so `pose_wait_gate` holds it and `pose_wait_tick` releases it after 2000 ms: `WARN card152 ...
  its own ragdoll started` x3, `WARN card269: client rest read-back 2 ... differs` x3.
- Step 2 (b156): `c132_pose_game_ragdoll` blocks the start when the guid has a rest row and no pose row;
  `pose_wait_tick` starts nothing when a rest row came during the wait. Proof: client `card207: client blocked the
  game's ragdoll start on rest-row guid` rows, 0 of the three WARN classes above, shots match.

## Run c207b156a (b156 `97e63881d4a00093`), 18:20: step 2 works, two more causes

Step 2 held: 4 `card207: client blocked the game's ragdoll start on rest-row guid`
rows (2 live corpses, 2 on 0x29c after the rejoin), no `WARN card152` for a
rest-row guid. Still failing:

1. Host, after the load: 2 of 3 corpses have no current sequence (`WARN card269:
   host rest row ... not added: no death name (no death note, FName 0x0)`, health
   0.0 and -8.0), so they got no rest row. Client: guids 0x26a and 0x28a took the
   old path (2 `WARN card152 ... its own ragdoll started`, 2 `WARN card137 ... not
   posed after 9006 ms`).
2. Client guid 0x29c: the SetSimulatePhysics start was blocked, but the blueprint
   native `SetAllBodiesSimulatePhysics(1)` at exe+0x3B05087 set its bodies
   simulating from 513343049 to 513349047.

The `WARN card269: client rest read-back 2 ... differs` row is not a proof row: the
hash is over raw doubles, and WarriorBug rest rows differ in 6 of 6 runs with or
without a ragdoll (junior trace of 211 read-back rows).

Step 3 (b157, `patch_c207c.py`): the host gives a loaded corpse with no death name
a bare rest row (seq `None`); the client shows that copy as it is and starts
nothing; the client does not pass on the game's `SetAllBodiesSimulatePhysics(1)`
for a copy with a rest row and no pose row. Done when: after the rejoin 3 rest
arrivals (played or bare), 0 `WARN card152 ... no stream row`, 0 `WARN card137 ...
not posed after`, no body-simulate 0 -> 1 row on a rest-row guid, live ragdoll
deaths unchanged, and the after-rejoin shots show the corpses lying the same.

## Run c207b157a (b157 `1e6244b6739bbcd0`), 18:27: log proof passed, shots not conclusive

Host after LoadLatestSave: 3 `card207: host corpse shown` rows, all `rest row 1` (death name 1, 1, 0), 0 `WARN card269 ...
not added`, `rest replay to a joining PC: 3 rows sent of 3`. Client after the rejoin: 2 `card269: client rest arrival ...
played` (Death_07, Death_01), 1 `card207: client rest arrival ... bare`, then on each of the 3 guids one blocked ragdoll
start and one blocked `SetAllBodiesSimulatePhysics(1)` (exe+0x3B05087). 0 `WARN card152`, 0 `WARN card137`, 0 EXCEPTION,
round 6 digest 0 mismatches, scenario passed. No `death name 0` row before the load. Live deaths before the save streamed
as before.

Open: the after-rejoin shots are taken from different spots (host on the stairs, client behind the host), so they do not
show that the three corpses lie the same on both sides. Owed: one pair of shots from the same spot and facing. Run budget
3 of 3 is used; the shot goes with the next run that loads this checkpoint, or the playtest.

## Closed (2026-11-17 13:10)

Log proof passed in run `c207b157a` (b157): 3 host `card207: host corpse shown` rows with `rest row 1`, 3 of 3 rest rows
replayed to the joining PC, client 2 `played` and 1 `bare` rest arrivals, each guid's ragdoll start and
`SetAllBodiesSimulatePhysics(1)` blocked, 0 `WARN card152`, 0 `WARN card137`, digest 0 mismatches.

Not done: the pair of after-rejoin shots from the same spot and facing. The shots taken were from different spots
and prove nothing either way.

Reopen when: after an in-place load or a rejoin, a `Pose:` digest mismatch names a corpse guid, a
`WARN card152 ... no stream row` or `WARN card137 ... not posed after` row appears for a corpse the host held at
the load, or a playtest shows a corpse lying differently on the two screens after a checkpoint load.
