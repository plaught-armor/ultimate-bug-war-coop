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

## Run `c263b` (2026-11-19, b288 `cc587891d7bf7846`, senior read): collapse reproduced, mod time ruled out

Scenario `tools/scenarios/c263-c3m2-plant.txt`, `UBWCOOP_C132_FX=1`, `UBWCOOP_C136_LOG` off. Host holds 589 to 601
frames per 10 s for the whole fight. The client falls steadily from client t=12532084 (12 natural fight rounds, then
6 lift rounds from about t=12620000):

| client t | frames per 10 s | mean ms | over 50 ms | fallback live | window peak | mod ms (sum of the split) |
|---|---|---|---|---|---|---|
| 12522065 | 592 | 16.9 | 1 | 37 | 50 | 821 |
| 12542086 | 437 | 22.9 | 2 | 47 | 48 | 708 |
| 12562135 | 275 | 36.5 | 4 | 26 | 51 | 386 |
| 12602189 | 222 | 45.1 | 69 | 41 | 49 | 484 |
| 12622259 | 179 | 56.2 | 117 | 81 | 81 | 552 |
| 12682424 | 161 | 62.4 | 127 | 41 | 60 | 423 |
| 12702488 | 166 | 60.5 | 119 | 28 | 44 | 317 |

- The card 252 step 1b rule decides: in every window under 400 frames, the mod's split is 3 to 5 percent of the
  window. The time is in the game or the GPU, not in mod code. The util table stays at 26 to 81 entries (no cap,
  untracked 0), so the b161 sweep fix holds and is not the cause.
- The drop starts before the lift block and before corpses grow (card132 pose rows: 5 corpses at round 4 and 5,
  19 at round 10). Client actors walked by the digest grow only from 28083 to 28507. Projectile copies per digest
  round stay flat (AIMoritaRound 426 to 554, then 432). So the growth is not actors; candidates are components
  (Niagara, audio, decals) or GPU work.
- Not ruled out: another session's GPU job. No GPU busy samples were logged during the run. The host held its cap,
  so a shared GPU would have to hit the client alone.
- The 30 s window threshold "client windows of at least 500 frames" fails: 30 s windows fall to 531 frames, and the
  10 s windows to 161.

Next: card 252 step 2 (the live object census), built into the next lane 2 batch. It is read in the next FED_C3M2
run, with a GPU busy sample every 10 s in the run log.

Scenario note (harness): in the 12 natural rounds the client's `look actor BP_AI_WarriorBug_C 50` failed with "no
such actor". The host's look found the nearest warrior 15606 uu away (host cmd 42: from -18716 -6070 1766 to -33846
-2438 577), likely past the client's net cull distance. So neither side fought at close range before the lift. The
next C3M2 fight run must place both players at the warriors (the lift-style `spawn bug` near the players, or a
warp to the nearest warrior), or it does not show the close fight.

## Closed (senior 2026-11-21)

The collapse no longer shows on current builds. The c263L363 to c263L370 runs use `tools/scenarios/c263-c3m2-load.txt`
on FED_C3M2, which spawns 150 bugs near both players, so the close fight is covered. The worst client frame window
(card132 "frame window" rows, about 19 windows per run) holds 275 to 403 frames per 10 s, against 161 in c263b on
b288. The proof gate from the 2026-11-19 10:12 STATE entry (no window under 100 frames per 10 s) holds in all 8 runs.
These runs were three instances on one machine with perf recording, so the real play numbers are higher. The card252
census rows (Niagara, audio and decal component counts) stay in the build for any later regression. Remaining
client frame cost belongs to the card 573 umbrella.
