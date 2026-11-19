# Card 184 — Card 104 relay tables are reset after the new map's actor init

## Goal
A card 104 relay spawn recorded while LoadMap initialises the new world is not wiped by the map reset.

## Known (card 182 sweep, untested)
`g_c104_relayed` and `g_c104_pending` are reset in `c98_arm_map`, which runs from `c50_arm_map` on the first tick
after LoadMap. A spawn the host records during `UWorld::InitializeActorsForPlay` of the new map would be cleared
there and never sent. This is not a stale read; it is a lost row.

## Do
Static first: can a card 104 spawn be recorded inside LoadMap on the host (which hook, which classes)? If no, close
with the proof and no code. If yes, move the reset to LoadMap enter (the card 182 place) and show one row surviving.
Sweep the other tables `c50_arm_map` clears for the same order problem; one table in the Report.

## Files
The file that owns the card 104 tables, `mod/src/ubw/hooks_ubw.cpp` (`ubw_load_map_enter` only), the card.

## Senior trace (2026-11-11)
- **Verified:** `c104_relay_spawn` (world_c98.cpp:687) has two callers: the landed flare (anim_c104.cpp:232, play
  only) and `detour_c75_world_spawn` (vehicles.cpp:1414), which relays a spawn of a card 105 relayed class when it
  is a strike, a runtime pickup or a Blueprint spawn (`c105_bp_spawn(caller)`).
- **Verified:** the tables are reset at world_c98.cpp:1605-1607 (`g_c104_relayed`, `g_c104_pending`,
  `g_c104_copies`), on the first tick after LoadMap, with `g_c140_placed`.
- **Open, decides the card:** on the host, BeginPlay of placed actors is dispatched inside LoadMap. Does any
  Blueprint spawn a card 105 relayed class from BeginPlay or a construction script (kis.py over the relayed class
  list and their spawners)? If yes, its row is recorded in LoadMap and wiped on the first tick; the destroy match
  and the join replay then miss it. If no, close with the list as proof.
- `g_c140_placed` has the same order: a placed-move row journalled during LoadMap is wiped too. Same answer needed.

## Static part (junior, 2026-11-12)

### Method
- Code: the arm list in `client_diag_load_map_return` (sprint.cpp:225-240), plus what `c50_arm_map` and
  `c98_arm_map` clear. It runs right after LoadMap returns, on a successful load only.
- Logs: every `UEngine::LoadMap enter` .. `returned` block in logs/*host*.log and logs/*client*.log, split at the
  new world's `BP_GameMode` spawn line. 751 host blocks have that line; 931 have none and are skipped. Lines after
  the split were written inside LoadMap for the new world. Lines before it belong to the old world's teardown.
- Scripts: tear2.py / tear3.py (journal kinds) and tear4.py (card prefixes), in the session scratchpad.

### Answer to the open line
**Yes.** The host records card 104 spawns inside LoadMap. In c123t..c123x, `BP_TacSupp_Pod_MechDrop_C` is relayed
by the card 104 path inside LoadMap: 51 `world P` rows in total. For example, c123x-host.log:
- line 903: `host spawn BP_GameMode_GameplayMI_C`
- line 1103: `card098: host t=129723601 world P /Game/Blueprints/Combat/BP_TacSupp_Pod_MechDrop.BP_TacSupp_Pod_MechDrop_C -77657 -23660 52412 218019: sent to 0 of 0 clients`
- line 1304: `UEngine::LoadMap returned true`

The first-tick reset then wiped the row from both `g_c98w` and `g_c104_relayed`.

The same window also holds rows the reset wiped:
- 11411 `world K` rows (placed killable buildings killed in their own BeginPlay, e.g. the
  `BP_WarriorBug_Corpses_Destructible` set);
- 141 `world E` rows;
- 7 `card049 AddObjective` frames (card 199).

`g_c140_placed` is **no**. Its only writer is `c140_placed_visit` (ext_c132.cpp:359, `ext_sweep_tick`), which runs
from `c132_ext_step` on the engine tick (hooks_ubw.cpp:213), and LoadMap does not tick. The sweep finds 0
`card140: host placed class` lines inside LoadMap.

Teardown (before the split) holds only 24 `card049 SetObjectiveVisibility` frames, all in one playtest log, and no
card 098 world row.

### Table (one row per reset)
Verdict: can the host (or the client, where noted) write this state inside LoadMap?

| Reset (where) | State | Verdict | Source |
|---|---|---|---|
| c49_arm_map (sprint.cpp) | `g_c49_journal` (n, compact_off), `g_c49_cap` | yes | 7 AddObjective frames inside LoadMap; c195-r1 bug map 0 of 0 replayed (card 199) |
| c49_arm_map | `g_c49m` (synced PCs, first_add_ms) | yes | first_add_ms is set by those adds; the reset after them re-emits nothing (card 199) |
| c49_arm_map | `g_c49.world_check_ms`, `g_c49.depth` | no | the world check names the new world; it must stay after the return |
| c98_arm_map (world_c98.cpp) | `g_c98w` world journal + totals line | yes | P 51, K 11411, E 141 rows inside LoadMap |
| c98_arm_map | `g_c104_relayed` + peak line | yes | the MechDrop P rows above (c104_relay_spawn, world_c98.cpp:687) |
| c98_arm_map | `g_c104_pending` + card140 line | yes | same writer (world_c98.cpp:707) |
| c98_arm_map | `g_c104_copies` + client map-end line | no | client: made from received rows (world_c98.cpp:886), on the tick |
| c98_arm_map | `g_c140_placed` + map line | no | tick only (above) |
| c98_arm_map | `g_c98` (C98State), `g_c98_replaying`, `g_c132_refused` | no | diagnostic dump and a replay flag; no record kept for the new world |
| c98_arm_map | pl_map, c137_change, c137_gib | open | client: 9349 card137 lines inside LoadMap, not traced |
| c98_arm_map | c121, net_budget, c132_hit, c166, c132_decal, c132_pose, c136, c101_forget | no | totals and counters only, or rows from hit, decal, pose or FX events relayed on the tick; c136 logs FX inside LoadMap but its arm only reports |
| c98_arm_map | c132_arm_map: `g_c132h`/`g_c132c` round state, c132_ledger_arm_map, c132_proj_map | open | 4337 card132 Health lines inside LoadMap (corpse destructibles 400 -> 0); whether the ledger keeps them is not traced |
| c98_arm_map | c132_ext_arm_map `g_ext` | no | host polls on the tick (ext_sweep_tick) |
| c50_arm_map (sprint.cpp) | c95_arm_map `g_c95`, `g_c95c` | open | 7479 card095 collision lines inside LoadMap; `c95_relay_collision` journals only in ListenServer net mode, and whether the world is listening at BeginPlay is not traced |
| c50_arm_map | c161 doors, c133, c99 / c103 / c160 / c176, c120 gate, c114 world pointer | no | no line of these cards inside LoadMap; pointer or counter resets |
| c50_arm_map | c132_dropship_arm_map `g_ds` | open | 787 card140 dropship lines inside LoadMap; the journal writer is not traced |
| c50_arm_map | `g_c50` fields, `g_c62_instance_weak` | open | 2852 card050 DisablePickup lines inside LoadMap; the relay state is not traced |
| arm list | c25, c27, c29, c22, c28, fx_watch_map, c23, c37, c38/c39/c40, c41, c30, c32 | no (c39 open) | no line of these cards inside LoadMap except c032 PE diag (615) and c039 UpdateColliders (293: open) |
| arm list | c45_arm_map (c44 one-shots), c43_arm_map (c43, c46, c48, c130, c156) | no | death and down state of the old pawns; c048 logs 36 AddObjectiveObject lines but its arm only resets a counter |
| arm list | reset_card017_map `g_c17`, `g_c18` | open | 14346 card017 GiveAbility lines inside LoadMap; whether C17State keeps a needed record is not traced |

Counts: **5 yes** (all moved), **13 no**, **7 open** (left in place; listed under Problems).

### What moved, and what stays
- **Card 199:** `c49_map_end` (sprint.cpp) now runs from `ubw_load_map_enter`. It resets the journal, compact_off,
  the card 066 cap and `g_c49m`, keeping `delegates_logged` and `completed` as before. `g_c45_phase` only changes
  on the tick (death.cpp:2766), so `completed` evaluates the same at enter. `c49_arm_map` keeps `world_check_ms`
  and `depth` at the return.
- **Card 184:** `c98_world_map_end` (world_c98.cpp) now runs from `ubw_load_map_enter`. It logs the world relay
  totals, the card 104 peak and the card 140 deferred-spawn line, then resets `g_c98w`, `g_c104_relayed` and
  `g_c104_pending`. `c98_arm_map` keeps everything else at the return: pl_map, the c137 arms, `g_c98`, the client
  copies line, `g_c104_copies`, `g_c140_placed` and the rest of its calls. None of the moved state needs the new
  world; the old world's actors in the tables were never read again after the enter.
- `g_c98w` moves with the card 104 tables. Without it, the P row would survive in `g_c104_relayed` but not in the
  journal that the join replay reads.

## Run c199-r1 predictions (card 184)
- C184-P1. The host logs `card098: host world relay map totals` once per map change, at LoadMap enter (between the
  `UEngine::LoadMap enter` line and the new `BP_GameMode` spawn), and no longer after `LoadMap returned`.
- C184-P2. A row recorded inside LoadMap survives. On the bug map, either a `card098: host t=N world <K|E|P> ...`
  line between the new GameMode spawn and `LoadMap returned` is followed by a first-Pong world replay that counts it
  (`world journal replay ... N rows` with N at least the rows logged inside LoadMap), or no row occurs inside LoadMap
  in this run. In that case the proof is the c123x line above, and this run shows only that the move breaks nothing:
  the replay is sent and the digest rounds have 0 mismatches.

## Problems (card 184)
- 7 open rows, all left at the return: c137 (client), the c132 ledger and round state, g_c95 / g_c95c, g_ds, the
  g_c50 relay fields, g_c39, and g_c17 / g_c18. Each has its own log lines inside LoadMap. Which of them keep a
  record the new world needs is not traced; that is one follow-up card for the class.
- The moved resets now also run on a failed LoadMap. Before, a failed load kept the old journal. After a failed
  load the old world is gone anyway; not seen in any log.
- Teardown: the sweep finds no world row before the new GameMode spawn. A row the old world wrote there would now
  go into the new map's journal. This is the same class as the card 199 Visibility frames.

## Report (run c199-r1, build e7d7860ed7f47b95; accepted by the senior)
- C184-P1 holds. `card098: host world relay map totals: 226 rows, 0 dropped, 0 sent, 0 applied, 0 missed` (host line
  3275) is logged from the enter hook, just before the `UEngine::LoadMap enter` line (3276), not after the return.
  The host logs it once: the checkpoint restart is LoadFromSlot in place and makes no host LoadMap. The card 104 peak
  and card 140 lines are absent because their tables were empty.
- C184-P2 holds, with a row surviving. On BUG_C1M1, 10 `world K` rows were logged between the new GameMode spawn
  (line 3338) and `LoadMap returned` (3618), for example `card098: host t=106830367 world K
  BP_WarriorBug_Corpses_Destructible_01_C_1 7410 -4715 -770 0`. The first-Pong replay then read `card098: host
  t=106837198 world journal replay to BP_PlayerController_C (first Pong of the map): 11 of 11`. On FED_C2M1, 100 rows
  inside LoadMap and a replay of `226 of 226`. Digest rounds: 0 mismatches.
- The 7 open rows stay at the return. The senior cards them as one class card.

## Senior review (2026-11-12)

Accepted on c199-r1 and c198-r2. The static table answers the open line (yes: card 104 P rows, K and E rows are
written inside LoadMap); `c98_world_map_end` runs from `ubw_load_map_enter`. Runs: world journal replay on the bug
map 11 of 11 and 24 of 24 rows, 0 digest mismatches. The 7 open resets are card 200.
