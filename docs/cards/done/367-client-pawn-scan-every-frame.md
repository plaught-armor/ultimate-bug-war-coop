# Card 367: client walks every actor of the map each frame (card 037 scan); card 330 step 2

Class: efficiency (per-frame cost). Source of numbers: `logs/b50r2-{host,client}.log` (DLL `f63840e619112273`),
`card330:` rows, map FED_C2M1.

## Findings

1. **Client `c37_scan` walks the levels every frame.** `c37_step` 1195.5 ms total, 564.4 us mean, 2118 calls; its
   `c23_collect` call site (`mod+0x4515b`) 2118 calls, 1164.3 ms. Window rows: collect 1835 to 1837 calls,
   56.3 M actors, 1035.3 to 1216.8 ms per 30 s; mod total 7.83 to 9.45% of frame time on the client against
   3.99 to 4.62% on the host. The walk visits every actor (about 30.7k) to find at most 256 characters.
2. **`c43_step` on the client: 367.5 us mean (778.4 ms), none of it through `c23_collect`.** It is an umbrella of
   about 20 sub-steps (`c46_step`, `c50_step` and its children, `c227_recheck`, ...). Host: 175.3 us mean, 96.23 ms
   max, 17 calls over 1 ms. The step table cannot say which child costs it.
3. **`c28_step` walks the levels itself at 10 Hz** (`remote_body.cpp`, `kC28StepMs` 100) to find player pawns:
   host 264.7 ms (120.0 us mean), client 208.3 ms (98.4 us mean). Not fixed here.
4. **Host `c125_host_rows` turret walk at 10 Hz**: call site `mod+0x6cc5b` 349 calls, 169.4 ms; `c125_step`
   170.6 ms. Not fixed here.
5. **38 level walks outside `c23_collect`** (`grep kLevelActors mod/src/ubw/*.cpp`) are not in the
   collect-by-caller row, so the card 330 collect totals understate the walks (finding 3 is one of them).

## Change (in source, for b52)

- Finding 1, `enemies.cpp` `c37_scan`: the level walk runs every `kC37CollectMs` (100 ms) and on a world change;
  the frames between read the same pawns through weak refs (`g_c37_pawns`, `c31_weak_get`). A Health change is
  still seen in its frame. Only a pawn's first sighting can be up to 100 ms late. Without the weak-ref functions
  every frame walks, as before.
- Finding 2, `death.cpp`: `C330_STEP` around the 8 children of `c43_step` and 13 children of `c50_step` (21 new
  step ids, 79 of 96). Measurement only.

## Predictions (written before the run; same scenario as b50r2)

- Client window rows: collect calls per 30 s from about 1836 to under 450; `c37_step` from 1066.5 to 1255.0 ms to
  under 300 ms; client mod total under 6% of frame time.
- Client death edges unchanged: `card038` death rows and `card146` rows present as in b50r2, 0 new WARN kinds,
  0 `WARN card037`.
- The map-end rows name the `c43_step` child that holds the 367.5 us.

## After the run

Findings 3 and 4 and the child named by step 2 get one shared fix if they are level walks: one cached list per
actor class (walk every period, weak refs between), owned by one place, instead of a walk per caller.

## Run b52 (2026-11-15, DLL `e3d10276a0c5ff9b`, lane 2, scenario `b50-c364-c365.txt`, b50r2 run line)

Ended by the scenario after 290 s at line 67 (RESTART CHECKPOINT, the AI pick-up), as b50r2 (288 s). Host 14713
lines, client 14275, FATAL/ERROR 0, no crash dumps.

- **Collect (finding 1): met.** Client FED_C2M1 windows: collect 314 to 448 calls per 30 s (b50r2 1797 to 1952),
  176.5 to 353.5 ms (b50r2 948.0 to 1302.8). Map end: `c37_step` 232.3 ms, 110.2 us mean (b50r2 1195.5 ms, 564.4 us);
  call site `mod+0x45171` 325 calls 203.9 ms (b50r2 `mod+0x4515b` 2118 calls 1164.3 ms).
- **`c37_step` under 300 ms per window: met in 7 of 8.** 158.7 to 256.7 ms; 381.9 ms in window 390270632, where every
  step on both sides about doubled (host 5.78%, its `c28_step` 323.8 ms): a machine-wide window.
- **Client mod under 6%: not met.** 5.57 to 8.51% (13.08% in the doubled window); b50r2 6.81 to 9.86%. `c43_step`
  rose: windows 964.0 to 1338.7 ms (b50r2 578.8 to 833.1); map end 1437.1 ms, 681.7 us mean, 165 calls over 1 ms
  (b50r2 778.4 ms, 367.5 us, 42).
- **The `c43_step` time (step 2): not in a wrapped child.** Client children at map end: `c50_step` 179.7 ms (its
  children `c208_step` 114.5, `c72_client_step` 28.1, `c50_client_step` 12.9, `c73_client_step` 1.9,
  `c310_client_step` 0.7, `c347_client_step` 0.5, `c345b_step` 0.2; `c75_count_step` 19.4), `c45_respawn_check` 4.6,
  `c43_client_step` 2.4, `c227_recheck` 0.9, `c133_client_step` 0.5, `c46_step` 0.3, `c44_client_tick` 0.1. Sum
  about 189 ms of 1437.1: about 1248 ms sits in the unwrapped tail, `c43_test_client_step` + `c43_shot_step`
  (UBWCOOP_C43_TEST only). `c43_test_client_step` calls `c43_client_host_pawn` (hooks_ubw.cpp:1412) every frame: a
  level walk of every actor outside `c23_collect` (finding 5's kind), until the first non-own player character.
  Its callers are test drivers only (c43, c44 `c44_other_pawn`, c46 `c46_test_client`), so a play build does not
  pay it. Why it doubled is not proven: the likely cause is that the c37 walk no longer warms the actor array the
  same frame. Host: `c43_step` 461.7 ms (b50r2 386.6); children `c43_host_step` 152.1 (max 87.19 ms, the spike),
  `c50_step` 38.5, `c45_respawn_check` 4.6, `c227_recheck` 0.5, `c46_step` 0.4: about 265 ms in
  `c43_test_host_step` + `c43_shot_step`.
- **Death edges: unchanged per death.** Client card038 94 rows, card146 9 (103; b50r2 113 + 10 = 123). Fewer kills
  this run (EnemyKilledByGameplayTag 13 against 15). Every StartDying has its death edge (WarriorBug 10 of 10,
  Morita 2 of 2, Medic 1 of 1; the Medic, spawned at 390160016, was adopted 37 ms later). `WARN card037` 0.
- **WARN diff.** Host: same 6 kinds and counts as b50r2. Client: card140 hit-drift rows 22 to 0; new 3 rows for one
  frozen copy, guid 0x24c, 1.6 uu bone offset (`WARN card191` frozen with bone 0 1.6 uu off body 0 after 10 wait
  ticks, `WARN card152` bone check sample 14, `WARN card152` pose stream map totals).
