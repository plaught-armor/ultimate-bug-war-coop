# Card 200: The seven first-tick resets not yet traced against rows written inside LoadMap

Status: queued 2026-11-12, split from card 184 (same class as cards 199 and 184). Budget: three runs. Static first.

Read: `docs/cards/done/184-c104-tables-reset-after-loadmap.md` ("Static part" table and "Problems"), card 199.

## Why

The new world's BeginPlay runs inside `UEngine::LoadMap`. A table that is reset on the first tick after the return
loses every row written there, so the join replay and the digest miss it. Cards 199 and 184 moved five such resets
to `ubw_load_map_enter`. Seven are open: each has its own log lines inside LoadMap, and whether the state it resets
keeps a record the new world needs is not traced.

| Reset | State | Lines inside LoadMap (card 184 sweep) |
|---|---|---|
| c98_arm_map | pl_map, c137_change, c137_gib (client) | 9349 card137 |
| c98_arm_map | c132 round state, ledger, proj map | 4337 card132 Health (corpse destructibles 400 -> 0) |
| c50_arm_map | c95_arm_map `g_c95`, `g_c95c` | 7479 card095 collision |
| c50_arm_map | c132_dropship_arm_map `g_ds` | 787 card140 dropship |
| c50_arm_map | `g_c50` relay fields, `g_c62_instance_weak` | 2852 card050 DisablePickup |
| arm list | c39 | 293 card039 UpdateColliders |
| arm list | reset_card017_map `g_c17`, `g_c18` | 14346 card017 GiveAbility |

## Do

1. Trace, no run: per row, name the writer that runs inside LoadMap and say whether the reset wipes a record that a
   later reader (join replay, digest, relay match) needs. Verdict yes / no per row, each marked verified or open.
   For c95: whether the world is in ListenServer net mode at BeginPlay (the journal only records then).
2. Move every "yes" reset to `ubw_load_map_enter`, as cards 199 and 184 did. A "no" row gets one line of reason in
   the table and stays.
3. One run on a map where a moved row occurs inside LoadMap; the row is in the join replay and both digest rounds
   have 0 mismatches. If no row is "yes", no run: the table closes the card.

Also check the two side effects card 184 named: the moved resets run on a failed LoadMap, and a row the old world
writes during teardown (after enter, before the new GameMode spawn) lands in the new map's journal. State for each
moved table whether a teardown writer exists; guard it if one does.

## Done when

- The table has no open row; moved resets are proven by the run lines or the card states why no run is needed. The
  Report ends with "Report complete."

## Senior design note (2026-11-12, design review)

59 reset functions exist, called from two places: eight at `ubw_load_map_enter` (hooks_ubw.cpp:241 to 248) and the
rest on the first tick after LoadMap (`c43_arm_map`, `c45_arm_map`, `c50_arm_map`, `c98_arm_map` lists). Cards 182,
183, 184 and 199 each moved resets from the second place to the first after a crash or a lost row. The class rule
for this card: a table that names old-world objects or holds per-map rows is reset at `ubw_load_map_enter`; a reset
stays on the first tick only with a written reason (it needs the new world). Step 1's table covers all 59, not
seven; step 2 moves every one without such a reason, in one build.

## Step 1 answers for the seven open rows (senior, static plus logs, 2026-11-13)

| Reset | Verdict | Proof |
|---|---|---|
| c95_arm_map `g_c95c` (collision journal), `g_c95` (doors) | yes, move | `c242-c3m1-host.log` first tick after the gameplay LoadMap (t=205281667): `card095: host map totals: 132 collision rows` then wiped. The world listens before BeginPlay (`net_mode == ListenServer` passes at `door_c95.cpp:771`; rows are journalled). A joining client's replay never gets these rows. |
| c132_dropship_arm_map `g_ds` (dropship journal) | yes, move | Same tick: `dropship map totals: 10 state changes ... 5 journalled`, wiped. c241-c2m1-r10 and c243-c3m2-r10 show the same. |
| c39 `g_c39` | move (pointer rule) | Totals and line caps only, no record a reader needs; but `C39Probe` and `gibbed` keep old-world actor pointers (`enemies.cpp:2212`), the class cards 182 and 183 moved. |
| pl_map, c137_change, c137_gib | no | Client change table is log only (50 rows of the new map's BeginPlay are counted to the old map: a totals skew, no sync record). Gib tables name parts spawned on the tick. |
| c132_arm_map, ledger, proj map | no | Round state, ledger key table and audit counters; the Health lines inside LoadMap are log lines, no row kept. `next_ms` must be stamped after the return. |
| `g_c50` relay fields, `g_c62_instance_weak` | no | IDs and a weak index of the old pawn; DisablePickup lines are logs; the pickup journal is card 098's, already moved. |
| reset_card017_map `g_c17`, `g_c18` | no | Slot FNames, timers and log caps; `start_ms` must be stamped after the return. |

Teardown writers (card 184 side effect), counted between `LoadMap enter` and the new GameMode spawn in
c242-c3m1, c241-c2m1-r10 and c243-c3m2-r10 host logs: 0 card095 rows and 0 dropship rows. No guard needed.

## Step 2 spec

- `door_c95.cpp`: split `c95_arm_map` into `c95_map_end` (totals lines plus the two table resets), called from
  `ubw_load_map_enter` (`hooks_ubw.cpp:233` list); drop the call in `c50_arm_map` (`sprint.cpp:1173`).
- `dropship.cpp`: `c132_dropship_map_end` (totals, `c159_dropship_report`, `g_ds = {}`, `g_ds_snap = {}`) at enter;
  `c132_dropship_arm_map` keeps only `g_ds_snap.load_ms = GetTickCount64()` (the settle window counts from the
  end of the load).
- `enemies.cpp`: `c39_arm_map` body to a `c39_map_end` at enter (with `c42_arm_map` only if it keeps no
  new-world need; say which in the report).
- Collision journal size: the map's rows now add to the BeginPlay rows (c242: 132 + 153 = 285 of 256). Raise
  `kC95Collisions` in the same build; this is card 219's cap, do both together.
- The other first-tick resets of the 59: no move in this card. Any that keeps a raw old-world pointer and is not
  in the table above is listed in the report, not moved.
- Prediction (any map run of the build, no extra run): the `card095: host map totals` and `dropship map totals`
  rows print at `LoadMap enter` for the old map; no such row with rows above 0 on the first tick after the
  return; the join replay's collision and dropship row counts include the BeginPlay rows (c242 route: at least
  132 collision rows, 5 dropship rows replayed to the joining client); 0 `journal full`; digest 0 mismatches.

## Closed (senior, 2026-11-16)

Step 2 is in the code: the tables are cleared by each mechanism's map end in `sync_registry.cpp` (`door_map_end`,
`dropship_map_end`) and `c39_map_end`, all called from `ubw_load_map_enter`. `c42_arm_map` stays on the first tick
with its written reason (`enemies.cpp`, above `c39_arm_map`).

The prediction is met by existing runs; no run was spent:

- `c460b169a` host: `sync: host ... map end: ... door 0 ... dropship 0 (0 rows)` at `LoadMap enter`; on the first
  tick after the return `sync: host ... map arm: ... door 127, world 56, ... dropship 5 (188 rows)`, so the
  BeginPlay rows written inside LoadMap are kept. First join replay: `door 127 ... dropship 5`, `127 of 127
  calls, 127 collision rows (peak 127 of 2048), 0 collision rows dropped`.
- `c448b170a` host (FED_C1M1): map arm `door 39, world 3, dropship 3`; three join replays each `door 39 ...
  dropship 3`; client rounds 4 and 5 `mismatches 0`, `slots differing 0`.
- Teardown writers: 0 (counted in step 1), no guard.

Reopens on: a `sync: ... map arm` row that shows 0 for door or dropship on a map whose first join replay then
has rows, or a `journal full` drop above 0.

Report complete.

