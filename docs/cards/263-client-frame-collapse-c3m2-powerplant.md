# 263 Client frame collapse in the FED_C3M2 PowerPlant fight, and new WARN classes

Found by card 243 run 3 (`logs/c243-c3m2-*`; logs are overwritten by the next run of that tag, so the rows
are quoted here). Likely a play blocker for the client player on this map: decide after step 1.

## Evidence: frame collapse (client only)

After the warp to the `BP_Switch_C_2` area (PowerPlant), client `card132` frame windows:

| client t | frames per 10 s | max frame ms | fallback live | window peak | untracked |
|---|---|---|---|---|---|
| 210917480 | 910 | 28.9 | 33 | 33 | 0 |
| 210927594 | 487 | 527.8 | 102 | 127 | 0 |
| 210937633 | 168 | 329.5 | 58 | 131 | 0 |
| 210948115 | 126 | 860.4 | 87 | 200 | 0 |
| 210958170 | 46 | 2377.1 | 170 | 512 | 26 |
| 210968459 | 14 | 2518.7 | 185 | 512 | 138 |
| 210978789 | 10 | 3161.8 | 267 | 512 | 500 |
| 210988847 | 20 | 2916.2 | 189 | 512 | 816 |
| 210999992 | 58 | 2936.1 | 168 | 512 | 855 |

The host holds 367 to 691 frames in the same windows (fallback 0, it is client-only state). The collapse
starts (487, then 168 frames) before the table reaches its cap, so the cap is a second symptom, not the
cause of the first drop.

Known cost in the mod: `c132_util_track` (`mod/src/ubw/grenades.cpp:1089`) runs `c132_util_sweep` over the
whole table on every spawn, and each entry costs a weak-pointer read plus `g_niagara_is_complete`. At 512
entries and hundreds of spawns per frame this is quadratic. It only runs with `UBWCOOP_C132_FX=1`, which the
soak sets and the play build does not, so it cannot be the whole cause in real play; card 252 shows the same
client-only shape on FED_C2M1 at 15 to 25 fps.

## Step 1 (static, then at most one run)

1. Count from the run 3 client log what was spawned per window between 210917480 and 210999992: fallback
   Niagara spawns, gib parts, decals, bone checks, hit rows. Name the class that grows with the frame time.
2. State which of those paths run without `UBWCOOP_C132_FX` and `UBWCOOP_C136_LOG` (the play build's path).
3. Make the diagnostic table cheap regardless: sweep once per frame, not once per spawn.
4. Only then one run of the PowerPlant block with the diagnostics off on the client, to separate the cost of
   the diagnostics from the cost of the sync path. Prediction written before the run.

Same class as card 252; work them together, 252 first.

## New WARN classes in run 3 (client unless noted)

| Count | Row | Reading |
|---|---|---|
| 15 | `client gib host part N STGorePart(Non)Sticky_StaticMesh asleep above` | new |
| 1 | `client gib host part N ... lies N uu from` | new |
| 14 | `client hit <Hopper/Warrior/Chariot bug> guid ...: host location (...)` | new |
| 13 | `client bone check guid ... (live / settled final / frozen final): bone N (pelvis ...)` | pose class, see 262 |
| 1 | `client t=N door BP_Door_Sliding_Medium_C_N client-only DoorState` | new; a door state the host never sent |
| 1 | `recv N ... WorldOp NaN ... result skipped: no place` | new; a NaN reached the wire, find the sender |
| 1 | `client turret row BP_MountableTurret_C_N` | card 233 |
| host | `host hit BP_AI_WarriorBug_C ... impact ... uu from` | new |

Each new class: trace the emitting check, decide real divergence or a check that is too strict under load
(most rows fall inside the collapse window above), fix by class. The door row and the NaN row are checked
first: both are world state.

Files: `mod/src/ubw/grenades.cpp` for item 3; others named by step 1. After the playtest unless step 1 shows
the play build is hit.

## Step 1 answers (existing logs, senior, 2026-11-13)

- Run 9 (`logs/c243-c3m2-r9-client.log`, 54 minutes, whole route, bugs cleared by fire before each objective, 60 fps
  cap so 600 frames per window is the ceiling): 323 client frame windows; 306 at 500 frames or more, 16 between
  300 and 499, 1 below 300 (the join window). The collapse of run 3 did not recur.
- The diagnostic table never passed 151 entries in run 9 (run 3: 127 to 512 during the collapse). The slowest run 9
  windows are the ones with the largest table: peak 151 gives 419 frames, 119 gives 376, 107 gives 365; windows
  with a peak under 50 average 589 frames. Frame time follows the table size in both runs.
- This does not separate the cost of the table from the cost of the effects it counts. Item 3 removes the known
  quadratic part, which is diagnostics only (`UBWCOOP_C132_FX=1`); the play build does not run it.
- Reading: not a play blocker on the evidence so far. Real-play frame cost stays with card 252.

## Fix spec (item 3 only)

- `mod/src/ubw/grenades.cpp`: `c132_util_track` no longer calls `c132_util_sweep` on every spawn. Sweep once per
  engine tick in `c132_util_step`, and in `c132_util_track` only when the table is full (one sweep, then count
  `untracked` if still full). Peaks are then per-tick values; say so in the frame window row text.
- No other file. No run of its own: the next FED_C3M2 proof run shows the frame windows.
- Items 1, 2 and 4 and the WARN table stay open; the door row is card 270's class, the turret row card 271's.

Prediction: in the next FED_C3M2 run no client window below 300 frames outside load and join.

## Status check (2026-11-16 20:00, senior)

The fix spec is built (in source as of b161). Its proof run on FED_C3M2 is still owed; it goes with the next C3M2 run.

## Run `c236b163a` (2026-11-16 20:31, b163): prediction holds, at a low table peak only

Client frame windows after the load: 601, 600, 600, 598, 593, 593 frames per 10 s (the 241 and 479 windows are the
load and the join). The util table peaked at 27 per tick, against the peaks over 50 of the runs this card came
from, so the heavy case is not shown. Stays open for a FED_C3M2 soak with a full fight (card 243's run).

## b208 check (junior, 2026-11-18)

Skipped in b208: no plain static cause beyond the sweep fix already built. Its proof still needs a FED_C3M2 full
fight; C4M1 run c244b207b had 0 of 374 client windows under 500 frames (see card 252).
