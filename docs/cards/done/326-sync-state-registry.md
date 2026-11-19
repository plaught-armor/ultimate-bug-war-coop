# 326 One registry owns every sync state table's reset, replay and reconcile

User decision 2026-11-14: "do the registry and per-player row now, before the playtest." Audit source:
`docs/SYNC_AUDIT.md` section 10 (cards 199, 200, 219, 223 and 325 are all "a journal was reset, capped or replayed
at the wrong moment").

Senior trace 2026-11-14, mod source. The cause class is settled; the change is a move of call sites, with no change
to any mechanism's rows. Batch 2 (after the run that carries card 325's fix).

## What is wrong today (settled)

Each state mechanism is wired by hand into five moments, in five different places:

| Moment | Where the calls live now |
|---|---|
| map end (old world's rows dropped) | `ubw_load_map_enter` (`hooks_ubw.cpp`), about 25 calls |
| map arm (new world, after the load) | `c50_arm_map` (`sprint.cpp:1185`) and the chains under it (`c98_arm_map` to `c136_arm_map`, ...) |
| join replay (client's first Pong) | `c50_join_replay` (`weapons.cpp:1008`), `c49_first_pong` (`objectives.cpp:437`) |
| in-place load reconcile | `detour_c137_load_slot` (`save_c121.cpp`), then `c143_after_load` |
| quit | scattered `c*_report("quit")` calls |

Nothing checks that a mechanism appears in all five, or that it clears state only at map end. Card 325 is the
latest case: `c136_arm_map` reset the effect journal at map arm, after the load had already filled it. The card 323
table (junior, `c323-opcodes.md`) lists the state ops with no join replay at all (Down, seats, grenade in flight, gib
rest rows, hold progress): each was built without a replay and nothing flagged it.

## Design (one table, in a new `mod/src/ubw/sync_registry.cpp`)

One constant table, one row per state mechanism, in a fixed order:

    struct SyncMech {
        const char* name;            // "door", "world", "objective", "effects", "player", ...
        void (*map_end)();           // the only place state tables are cleared (also run at quit)
        void (*map_arm)();           // report, re-resolve classes, clear counters; never clears a state table
        void (*join_replay)(void*);  // host: current state to one player controller
        void (*after_load)(void*);   // host: rebuild from the world after an in-place load; nullptr = "clients rejoin"
        int32_t (*live)();           // rows held now, for the checks below
    };

- `sync_map_end()`, `sync_map_arm()`, `sync_join_replay(pc)`, `sync_after_load(world)`, `sync_quit()` walk the
  table. The five places above call these and nothing else for the registered mechanisms.
- Order: the join replay keeps today's order exactly (`c95`, `c98`, `c260`, ext, pose, decal, dropship, effects,
  pause lift, bug form, intro, script rows); map end keeps today's order. A reorder is a separate change.
- A slot left `nullptr` needs a one-line reason in the table ("event only", "clients rejoin, see SYNC_AUDIT 10").
- Scope: the mechanisms in the card 323 table whose class is "state", plus the journals of cards 095, 098/104, 049,
  132 (ext, pose, decal, dropship), 136, 208, 135, 260, 310, 137 (gib), 074 (live grenades), 133 (holds) and the
  player row of card 327. Diagnostic-only arm functions (`c22_arm_map` ... `c41_arm_map`, tallies) stay where they
  are.

## Checks the registry adds (runtime, both sides, always on)

- `sync_map_arm` reads `live()` before and after each mechanism's `map_arm`. A drop is the card 325 class:
  `WARN sync: <side> t=<ms> <name> lost <n> rows at map arm`.
- Join replay, one row per join: `sync: host t=<ms> join replay to <pc>: door <n>, world <n>, ... (<total> rows)`,
  `<n>` = `live()` at the replay. A mechanism with `live() > 0` and no `join_replay` prints `WARN sync: <name> holds
  <n> rows and has no join replay`.
- Map end, one row: `sync: <side> t=<ms> map end: <name> <live> ...` before the clears.

## Step 1 (junior, static, report before coding)

The table itself, as a markdown table: one row per mechanism with today's function for each of the five moments and
`file:line`, an empty cell where none exists, and for every arm function what it clears (the card 325 sibling sweep
is the start of this table; do not do it twice). Flag every state table cleared outside map end.

## Predictions (one run, the batch 2 run; C1M1 from the menu, one checkpoint reload, one late join if card 320 allows)

- 0 `WARN sync:` rows on both sides, except rows naming a gap that card 323 or 327 already lists as open.
- Each join's `sync: host ... join replay` row names every mechanism; the per-mechanism replay rows that exist
  today (`card095`, `card098`, `card136 ... journal rows`, ...) carry the same counts as that row.
- Digest round 1 after each join: 0 mismatches. WARN class diff against the batch 1 run: no new class.

## Done when

The five call sites hold only the `sync_*` calls for registered mechanisms, the predictions hold, and
`docs/SYNC_AUDIT.md` section 10 points at the table as the one place a new state mechanism is added.

## Step 1 result (junior, 2026-11-14, static)

Call sites. All paths are under `mod/src/ubw/`.
- Map end is `ubw_load_map_enter` (`hooks_ubw.cpp:249`).
- Map arm is `client_diag_load_map_return` (`sprint.cpp:203`). It calls `c43_arm_map` at `:238`, then `c49_arm_map` and `c50_arm_map` at `:239`. `c50_arm_map` (`sprint.cpp:1185`) calls `c161`, the dropship arm, `c98_arm_map` (`world_c98.cpp:2818`), `c99`, `c120`, `c114`, `c310` and `c133`. `c98_arm_map` then calls change, gib, `c121`, `c132`, hit, `c166`, decal, pose, ext and `c136`.
- Join replay is `c50_join_replay` (`weapons.cpp:1008`) plus `c49_first_pong` (`objectives.cpp:437`).
- In-place load is `detour_c137_load_slot` (`save_c121.cpp:157`), then `c143_after_load` (`restore_c143.cpp:263`), which sends Reload so each client rejoins.
- Quit is only the test quit command (`testrun.cpp:2595-2597`). The play build's own quit calls nothing.

| Mechanism | Map end | Map arm, and what it clears | Join replay | In-place load | Quit |
|---|---|---|---|---|---|
| doors and collision (c95) | `c95_map_end` `door_c95.cpp:558` (`hooks_ubw.cpp:274`) | none | `c95_replay` `door_c95.cpp:514` | `c95_load_actor` `:372`, `c95_load_collisions` `:806` | none |
| door state (c161) | none | `c161_arm_map` `door_c161.cpp:588`: **the door table `g_c161` and `g_c288` (state cleared at arm)**. It is rebuilt by the 5 s tick scan, and `d.sent` is the state at tracking. | none: open whether a DoorState change before the join reaches the c95 journal | none | none |
| world (c98w: K/H/E rows, turret rows, ext rows, c104 relayed and pending, c289, c221, c287 sent) | `c98_world_map_end` `world_c98.cpp:2789` (`hooks_ubw.cpp:270`) | `c98_arm_map` `:2818`: `g_c98` (diag), **`g_c140_placed`, `g_c104_copies`, `g_c287`, `g_c313_replay` (state cleared at arm)**. Safe today: these are filled from the tick and from received rows only, never at BeginPlay. | `c98_replay` `:2742` | `c98_load_actor` `:606`, `c145_load_actor` `:1391` | none |
| ext actors (c132_ext) | none | `c132_ext_arm_map` `ext_c132.cpp:501`: **memset of `g_ext`, the baseline table (state cleared at arm)**. Its rows live in c98w. | `c132_ext_replay` `ext_c132.cpp:484` | none (the tick poll) | none |
| objectives (c49) | `c49_map_end` `sprint.cpp:1167` (`hooks_ubw.cpp:269`), which also calls `c70_map_end` `objectives.cpp:1126` | `c49_arm_map` `sprint.cpp:1158`: timer and depth only | `c49_first_pong` `objectives.cpp:437` | clients rejoin | none |
| difficulty (c114) | none | `c114_arm_map` `difficulty.cpp:1061`: the world pointer only; the values are read live | `c114_send` inside `c49_first_pong` | clients rejoin | none |
| dropship (c132/c159) | `c132_dropship_map_end` `dropship.cpp:833` (`hooks_ubw.cpp:275`) | `c132_dropship_arm_map` `:847`: `load_ms` only | `c132_dropship_replay` `:669` | clients rejoin | `c159_dropship_report("quit")` (report only) |
| effects (c136) | `c136_report("map end")` `fx_c136.cpp:736` (`hooks_ubw.cpp:256`) | `c136_arm_map` `fx_c136.cpp:775`: after card 325, host counts only; the client still clears its whole state | `c136_join_replay` `fx_c136_net.cpp:1962` | clients rejoin | `c136_report("quit")` |
| corpse poses (c132_pose) | only `c152_pose_wait_clear_all` (`hooks_ubw.cpp:258`) | `c132_pose_arm_map` `pose_c132.cpp:3496`: **the `g_pose` journal, `g_arrive`, `g_sprite`, `g_rest` (state cleared at arm)**. These are runtime rows, so safe today. | `c132_pose_replay` `:3326` | clients rejoin | none |
| gore decals (c132_decal) | none | `c132_decal_arm_map` `decal_c132.cpp:1354`: **the `g_dh` host journal and `g_dc` (state cleared at arm)**. Runtime rows. | `c132_decal_replay` `:1275` | clients rejoin | none |
| gib parts (c137) | none | `c137_gib_arm_map` `gib_c137.cpp:1189`: **`g_gh`, the spawn queue and `g_gc` (state cleared at arm)** | **none (card 323 gap)** | clients rejoin | `c137_gib_dump("quit")` |
| change bracket (c137) | none | `c137_change_arm_map` `change_c137.cpp:128`: memset of `g_change` (diag; keeps the host frame) | none | none | none |
| bug form (c208) | `c208_map_end` `weapons.cpp:2166`, from `pl_map_end` `playlog.cpp:784` (`hooks_ubw.cpp:268`) | none | `c208_join_replay` `weapons.cpp:1972` | clients rejoin | none |
| intro (c135) | `c135_map_end` `intro_c135.cpp:166` (`hooks_ubw.cpp:271`) | none | `c135_join_replay` `:271` | clients rejoin | none |
| level end (c260) | `c260_map_end` `objectives.cpp:1574` (`hooks_ubw.cpp:277`) | none | `c260_join_replay` `:1579` | clients rejoin | none |
| script writes (c310) | none (`c310_collision_map_end` `testrun.cpp:930` is a diag cap) | `c310_arm_map` `script_c310.cpp:757`: **`g_c310 = {}`, which clears the journal (tags, chargeable, default class) and the held rows. This is the card 325 class: a level BeginPlay write journalled during LoadMap is wiped at the arm.** | `c310_join_replay` `:598` | clients rejoin | none |
| live grenades (c74) | memset in `c132_rounds_map_end` `grenades.cpp:1526` (`hooks_ubw.cpp:260`) | none | **none (card 323 gap)** | clients rejoin | none |
| holds (c133) | none | `c133_map` `hold.cpp:359`: **`g_c133` (waits and applied; state cleared at arm)**. Runtime. | **none (card 323 gap)** | clients rejoin | none |
| life state (c43) | none | `c43_arm_map` `hooks_ubw.cpp:1309`: **`g_c43` entries and holds (state cleared at arm)**; also `c45_arm_map` `:1333` (c44 one-shots). All are Up at load, so safe today. | **none (card 327)** | `c143_after_load_asc` `restore_c143.cpp:277` (host health fix) | none |
| weapons, ammo, charge (c50/c72/c104) | none | `c50_arm_map` `sprint.cpp:1185`: `g_c50.copies = 0` and the scratch fields | `c50_first_pong` `weapons.cpp:994` forces the poll | `c143_apply_weapons` `weapons.cpp:2789` through Restore | none |
| mech pilots (c75) | `c75_map_end` `vehicles.cpp:1670` (`hooks_ubw.cpp:267`) | none | **none (card 327)** | clients rejoin | none |
| turret seat (c125) | none (`g_c125s` resets when the world pointer changes, `turret_sync.cpp:350`) | none | through the c98w turret row, `c98_replay` | clients rejoin | none |
| pause (c96) | none | none | `c96_lift_on_join` `pause_c96.cpp:66` | none | none |
| Pong gate (c120) | none | `c120_arm_map` `chan_ops.cpp:122`: `g_c120` (control, per map by design) | none | none | none |

**State tables cleared outside map end (flagged):**
- c310: wrong today, the card 325 class. A level BeginPlay write is lost.
- c161, c98 arm part, ext, pose, decal, gib, holds, c43: safe today, because no BeginPlay row enters them. They still clear state at the arm and should move to map end under the registry.
- Client side of c136: it clears at the arm, which is fine because the client gets the replay after its arm.

Card 323 rows with no join replay: gib rest rows, grenades in flight, holds, life state, mech seat. Activate is not a separate journal: range target rows ride c95/c98, and that is still to confirm.

## Closed (2026-11-17 13:19)
- Evidence (static): `kSyncMechs` in `mod/src/ubw/sync_registry.cpp` is called from all five sites: map end
  (`hooks_ubw.cpp:290` `sync_map_end`), map arm (`sync_map_arm`), join replay (`chan_ops.cpp:52`
  `sync_join_replay`), in-place load (`save_c121.cpp:172` `sync_after_load`), quit (`testrun.cpp:2914` `sync_quit`).
  `docs/SYNC_AUDIT.md` section 10 names the table as the one place a mechanism is added.
- Evidence (runs, b186 `6bdc1fb92f415071`): `c448b186a` host logs `sync: ... map end`, `map arm` and 18
  `join replay to` rows naming every mechanism; 0 `WARN sync:` rows on both sides of `c448b186a` and `c225b186a`;
  digest 0 mismatches.
- Not done: the per-mechanism replay rows were not compared count by count with the `join replay` row; the gaps
  card 323 lists (gib parts, live grenades, holds join replay; turret seat) stay with card 323.
- Reopen when: a `WARN sync:` row, a join whose digest round 1 has mismatches, or a new state mechanism added
  outside `kSyncMechs`.
