# Card 414: efficiency audit batch (AmmoMirror row, name plate build, ext sweep, heat lookups, slot compare)

Accepted 2026-11-16 for the parts in b108 and b109. The b110 parts are marked.

## What changed

1. `c72_mirror_apply` (`hooks_ubw.cpp`): an AmmoMirror that arrives while the sender has no pawn logs a plain row,
   not a WARN. In b108. Run c414b108a: host row `card072: host: AmmoMirror from BP_PlayerController_C with no server
   copy inventory (pawn null)`.
2. `c110_step` (`player_marker.cpp`): the name plate overlay is built only once a plate could show (a teammate that
   is not hidden, no cinematic stack open). Before, it was built in every world, transition worlds included: 8 builds
   in run c414b108a and single calls of 9.06 and 11.45 ms in `client map Untitled step c110_step`. In b109. Run
   c414b109a: 6 builds, each after a mission map load; `Untitled` max 0.01 ms; mission maps 0.52 to 1.20 ms max;
   `slot 1 shown` rows 5 (5 in c414b108a).
3. `ext_sweep_tick` (`ext_c132.cpp`): roots that are not Movable are visited every 64th sweep (`kExtDeepEvery`),
   not every 16th. Calls reaching `c140_placed_entry` fell from about 3700 to 2400 per sweep; the step mean on
   mission maps fell from 66-87 us to 56-72 us. The prediction (under 50 us) failed: the cost is the walk over the
   level's actors, not the table lookups. In b109.
4. `c125_attr` (`turret_sync.cpp`) keeps the `CurrentHeat` and `MaximumHeat` offsets per class (was 20 to 50
   by-name lookups a second on the host); `c402_slots_step` (`hooks_ubw.cpp`) compares a level's slots with one
   `memcmp` before the slot loop. In b110. Run c414b110a: the heat names are gone from the `card411` list; client
   `c402_slots_step` 69 samples to 36 (the prediction, under 25, failed); 22 `mismatches 0` each side.
5. `gib_client_tick` (`gib_c137.cpp`) returns at once while no gib row waits for its rest check. In b111. Run
   c414b111a: client `c137_gib_tick` 15 samples to 0; gib change totals identical to c414b110a (65 rows, local sim
   1410).
6. By-name lookups kept per class: the `Widget Stack` array offset (`c67_stack_array` in `testrun.cpp`, shared by
   `c67_ui_top`, `c121_ui_find` and `c135_first`) and the dropship look (`ds_mesh`, `ds_mesh_letter` in
   `dropship.cpp`: two mesh field offsets per dropship class, the `bHiddenInGame` and `bVisible` bits per mesh
   component class, cleared at each map load). In b111. Run c414b111a: `prop_find_prop` calls per run fell from
   24000 to 8000 on the host and 16000 to 6000 on the client; `Widget Stack`, `DropshipMesh` and `Troop Carrier Mesh`
   are gone from the `card411` list; the client dropship check rows are the same as in c414b110a (`mesh ii/ii`,
   join check 12 of 13).

## Numbers (run c414b111a, b111 `eb5786d9f7844b74`, fast chain, DIAG on)

- Drive rc 0, FATAL/ERROR 0, no crash dump, 22 `mismatches 0` rows on each side, no new WARN class.
- Mod share of process CPU samples: host 636 of 125114 (0.51%), client 593 of 126167 (0.47%).
- Mod share of frame time (`card330` window rows, game calls made by the mod included): host 0.67%, client 0.87%.
  Nearly half of the client figure is `client_significance_step`, which is the game's own significance update.
- About 40 `prop_find_prop` calls a second remain on each side (`bHiddenInGame` from `c33_keep_hidden`,
  `CapsuleHalfHeight` from `c34_track_half_height`, the wave fields at each wave); 19 host samples in all. Left.

## Numbers (run c414b109a, b109 `08f358d6ef15bd85`, fast chain, DIAG on)

- Drive rc 0, FATAL/ERROR 0, no crash dump, 22 `mismatches 0` rows on each side.
- Mod share of process CPU samples: host 674 of 125146 (0.54%), client 600 of 126654 (0.47%).
- Host top symbols: `c140_placed_visit` 48, `c132_ext_step` 31, `prop_find_prop` 27, `strcmp` 22, `c303_watch` 21.
- Client top symbols: `c402_slots_step` 69, `strlen` 41, `c137_gib_tick` 24.
- WARN classes: none new on the host. Client: `card140: client hit ... kHitDrift 300` on three more guids (460 to
  1501 uu), the class of open card 223.

## Not proven

No screenshot in the fast chain holds the teammate in the client's view, so the name plate has log proof only
(`slot 1 shown` rows). The next run that takes a client shot of the host shows it.
