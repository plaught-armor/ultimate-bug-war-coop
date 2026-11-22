# 252: Client frame time in heavy fights (FED_C2M1 plasma area and wave hold)

Found by card 241 run 2 (c241-c2m1-r2, DLL `83cf0b9cdcfe0cb0`). Client frame windows under 500 frames per
10 s: 34; host: 3. Client 169851854-169881943 (LOS_O02 plasma area): 148-183 frames, mean 55-68 ms, max
355-417 ms, 51-72 frames over 50 ms. Client 170352158-170612877 (LOS_O04 wave hold, LOS_O03 start): 218-420
frames, mean 24-46 ms, max up to 252 ms. Host in the same seconds: 513-600 frames, mean 17-19 ms. The same
lines print `util fallback live` 17-75 and `window peak` up to 143 on the client, 0 on the host. Both games
share one GPU in the test, so part may be the pair; the host does not drop, and the gate so far checked host
windows only. c238-c2m1 (no fight reached): 5 low client windows.

Step 1, no fix: attribute the client time. From the r2 client log per low window: counts of replayed fx,
sounds, pose snaps, gib and corpse rows, digest work, and what `util fallback` counts (card 132 code);
compare with a high window. If the logs cannot split it, add a per-window client time split behind
`UBWCOOP_C132_FX=1` (ms spent in each relay class per window), one run of the c241 O02/O04 blocks.

Step 2: fix the top cost by class (batching, caps per frame with carry-over, no per-event lookups), same
visible result on the client.

Gate from now on: frame windows are read on both sides, not only the host.

Files: decided in step 1. Budget: 2 runs.

## Step 1 answers (senior, logs of c241 runs 2 and 10, 2026-11-13)

- Still present: run 10 has 12 of 105 client windows under 500 frames (host 1), with the 60 fps test cap, so
  600 is full. Worst: 220 frames at 193710252.
- Counts per low window (run 10): 4100 to 13100 log lines, 1200 to 3500 replayed fx rows, 0 to 1365 own fx rows,
  2400 to 6700 fx audit rows, 0 to 140 pose rows, fallback peak 60 to 337. A full window nearby: under 1000 lines.
  Run 2, 169851854 to 169881943 (148 to 183 frames): 10297 audit rows, 4746 replayed fx, 1750 own fx in 30 s.
- Low windows without any own fx (194540915 to 194590995, 338 to 407 frames) exist, so the client's own firing
  is not needed for the drop; replayed fx volume is.
- The logs cannot split the time. Ruled out by count only: log volume as such (the host writes 22947 lines in
  the 30 s where the client writes 18634 and holds 513 to 600 frames; both use one unbuffered `WriteFile` per
  line, `mod/src/ue/log.cpp:135`). Not ruled out: the audit's stack capture per row (`caller_chain` depth 8,
  `proj_c132.cpp:173`, test flag only), the replay path per fx, the shared GPU.
- The audit and the fx log are test flags (`UBWCOOP_C132_FX`, `UBWCOOP_C136_LOG`). If the split below puts the
  time there, the play build is not affected and the fix is to the test flags, not to the relay.

## Step 1b spec (diagnostic only, next build, read in the next FED_C2M1 run; no own run)

- Per frame window, on both sides, behind `UBWCOOP_C132_FX=1`: milliseconds spent (QueryPerformanceCounter
  around the call) in each class, added to the `frame window` row as `; mod ms: chan <a> fx <b> audit <c> log
  <d> pose <e> digest <f> other <g>`: channel receive and dispatch in total, fx replay (card 136 apply), audit
  rows (stack capture plus format), `log::line` in total, pose apply and stream tick, digest rounds, and the
  rest of the mod's per-tick step.
- Deciding rows: in a window under 400 frames, the sum against the window length. Over 30 percent in one class:
  that class is step 2. Under 10 percent in total: the time is in the game or the GPU; step 2 is then a count
  comparison of live fx components client against host (`fallback` peak against the host's own count).
- Files: `mod/src/ubw/grenades.cpp` (`c132_util_step`, the row), one scoped timer type in `hooks_internal.h`,
  call sites in `proj_c132.cpp`, `pose_c132.cpp`, `chan_ops.cpp`, `mod/src/ue/log.cpp`. No behaviour change.

## b208 check (junior, c244b207b client log, FED_C4M1, 2026-11-18)

No static cause to fix. The step 1b split is built (`mod ms:` in the frame window row, `grenades.cpp:1555`). In
c244b207b (63 min of C4M1 with C132_FX and C136_LOG on) 0 of 374 client windows were under 500 frames; the mod's
own time per 10 s window was `other` median 273 ms, max 457 ms (under 5 percent). The heavy case (C2M1 plasma
area, C3M2 PowerPlant) is not reached on C4M1. Stays open for a FED_C2M1 or FED_C3M2 run with the split.

## Step 2 spec (2026-11-19, senior, from `c263b`; diagnostic only, next lane 2 build)

`c263b` (FED_C3M2) gives the deciding row: client windows of 161 to 275 frames per 10 s, with the mod's split at 3 to
5 percent. The time is in the game or the GPU (card 263 table). Step 2 counts what the engine holds, on both sides.

- Behind `UBWCOOP_C132_FX=1`, once per 30 s `card330` window: one pass over GUObjectArray (the loop of
  `objects_of_class`, `mod/src/ue/reflect.cpp:310`). Count the live objects in total and, by exact class pointer,
  NiagaraComponent, ParticleSystemComponent, AudioComponent, DecalComponent, SkeletalMeshComponent,
  StaticMeshComponent and ProjectileMovementComponent. Resolve each class once per map with `find_class`.
- One row per side: `card252: <side> t=<ms> census: objects <n>; NiagaraComponent <a>, ... ; scan <x> ms`. The
  scan time is printed so its own cost can be seen; it must not run in the play build.
- Deciding rows: the class whose client count grows with the frame time while the host's stays flat is step 3. If
  no count grows and GPU busy is high on the client's frames, the cost is drawn on the GPU (the client renders FX
  or decals that the host culls): step 3 is then a render-side comparison.
- `tools/run_pair.sh`: sample `/sys/class/drm/card1/device/gpu_busy_percent` every 10 s into `logs/<tag>.gpu` with
  a wall clock stamp. This is a harness change; make it between runs.

## c263L (4-player load, 150 bugs, b309, senior dig 2026-11-19)

Frame windows after the spawn: host 208 to 256 frames per 10 s, client 239 to 513. The mod's share was 6 to 8
percent on the host and 9 to 16 percent on the client, so most of the time is the game. Map totals from the card 330
step rows, largest first:

- Host: `c293_step` 2220 ms (225 us per tick, 558 calls over 1 ms), `c132_ext_step` 556, `c151_flush_step` 412,
  `c140_pace_step` 363, `c21_parity_step` 303.
- Client: `client_significance_step` 2846 ms (279 us per tick), `c293_step` 1496, `c37_step` 856 (max 31 ms, 54
  calls over 1 ms), `c132_pose_step` 623.

Notes:

- `client_significance_step` is the game's own UpdateSignificanceManager, the work the host's game mode does every
  frame. It stays.
- Fix in source: `c293_step` (card 293's closed diagnostic) traced every minimap marker on every tick while any
  suspect was listed, which at 150 bugs was nearly always. Between the 1 Hz samples it now samples only the listed
  suspects. Backup `build/audit_npc.cpp.pre293`.
  - Prediction: host `c293_step` under 300 ms per map at the same load.
  - The dwell leave rows keep tick resolution.
- Open: the `c37_step` client spikes of up to 31 ms. Read the step before any change.
- c263L312 (b312, FED_C3M2): host `c293_step` 1030 ms per map (was 2220). The under-300 prediction fails. Between
  the 1 Hz samples, each listed marker still pays `c293_dwell_find` (a weak-pointer assign plus a 64-entry scan), and
  the 1 Hz sample traces every marker. Card 293 is closed, so the step now runs only with the audit flag
  (`g_c37.audit`, from `UBWCOOP_ENEMYAUDIT`) in `hooks_ubw.cpp`. Not in a build yet. Prediction: no `c293_step` row
  without the audit flag.
- Client `c37_step` re-measured in c247b311a: 31 to 138 ms per 30 s, max call 4.2 ms (STATE.md). No card.

## Closed (senior 2026-11-21)

Every item on this card is done or measured: the c293_step runs only with the audit flag, client_significance_step is
throttled (card 568: peak 1629 down to 265 ms per 30 s), and c37_step peaks at 123 ms per 30 s with no card. The mod is
now 1.26 to 1.81 % of client frame time in the busiest windows (card 567 re-rank). The client's heavy-fight frame drop
is the game's own work or the GPU, and it is tracked in card 263 (the C3M2 PowerPlant collapse) and the card 573
umbrella.
