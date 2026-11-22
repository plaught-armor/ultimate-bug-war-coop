# Card 567: performance targets from the card 330 rows (logs, 2026-11-21)

User 2026-11-21: "check if logs can show us where we need to tighten performance more as well".

Source: every `card330:` window row (30 s: frames, mod ms, max tick, top 5 steps, log, collect, recv by op) in the 16
newest host and client logs (b366 to b371: soak3m1b366c, soak3m1b367a, soak5m1b367b, c551b370a, c559b371b,
c560b371a), about 55 min per side. Summed with a scratch script (top-5 rows only, so steps outside a window's top 5
are not counted). Per-second figures are averaged over all windows; peaks are single combat windows.

## Whole-frame picture

- Combat windows drop to 1034 to 1280 frames per 30 s on the host (34 to 43 fps) and 1407 to 1537 on the client
  (47 to 51 fps); quiet windows hold 1800 (the 60 fps cap). The mod is 2.0 to 2.7 percent of host frame time and up
  to 8.3 percent on the client in those windows, so most of the drop is the game, not the mod (card 527 family).
- Mod average: host 14.4 ms/s, client 30.5 ms/s (client recv adds 5.3 ms/s).

## Targets, ranked by GameThread ms

1. `client_significance_step` (client, `enemies.cpp:4957`), 16.5 ms/s average, peak 1628.8 ms per 30 s (54 ms/s,
   about 1.1 ms a frame at 49 fps), worst call 24.1 ms. It is the game's own `ASTGameplayGameMode::UpdateSignificanceManager`
   (exe+0x51408C0), run for the local PC because the client has no game mode. The host runs the same function every
   frame unconditionally: `ASTGameplayGameMode::Tick` (+0x2E) tail-jumps to it, and its cost is not in card330 (it is
   inside the game mode's tick). Cut (perf mod, base-game mechanic per the bug-load-perf-mod rule): run it at a fixed
   rate (30 Hz first) on both sides, through the existing `detour_c132_update_significance` on the host and the
   step on the client. Measure: client step ms/s and host frame time in the same combat window, before and after;
   check LOD and AI tick buckets show no visible change (shots).
2. `c132_pose_step` (client), peak 660 ms per 30 s (22 ms/s); host 0.48 ms/s average, peak 75 ms per 30 s.
3. `c132_ext_step` (host), 3.1 ms/s average, steady 110 to 150 ms per 30 s in combat (every window's top 1).
   Split rows (`card330: host ext split`, soak3m1b367a and c560b371b): 55 to 103 us per tick, of which the c140
   visit is 32 to 69 us (sweep every 27 ticks, about 3000 to 3700 c140 calls per sweep, nearly all past the
   placed/replicated early-out, 1400 to 2400 reach `c140_placed_entry`), the poll 14 to 25 us (220 baselines,
   one row sent per minute), the class lookup 3 to 7 us. Cut candidates: make the early-out reject before the call
   (the counts show it rejects almost nothing), or sweep less often when the actor count has not changed (card 401
   already narrowed the candidate list).
4. `c37_step` (client), 2.9 ms/s average, peak 205 ms per 30 s, worst call 14.1 ms.
5. `c140_pace_step` (host), 1.2 ms/s average, peak 110 ms per 30 s, worst call 31.3 ms (soak5m1b367b window
   t=97821193).
6. `c132_decal_step` (both), 1.2 ms/s average, peak 87 ms per 30 s. Card 566 (b372) relays every decal instead of
   gore only, so this and the Fx/decal bytes will rise: run 4 (c551b37xa) must report the step ms and the host decal
   bytes against these numbers.
7. `c151_flush_step` (host), peak 109 ms per 30 s, worst call 8.1 ms.
8. Client recv: Fx 2.1 ms/s (peak 206.6 ms per 30 s, worst row 40 ms); GrenadeSpawn 0.94 ms/s, peak 688 rows in 163.5 ms
   (0.24 ms per row, the costliest row type per row); GibPart worst row 34.3 ms; Objective worst row 28.9 ms; WorldOp
   worst row 18 ms.

## Single-tick spikes (over 10 ms)

- Load-time, not gameplay: `c43_step`/`c43_host_step` 99.5 ms (soak3m1b367a host window t=97657024) is the death
  screen RestartMission run inside the step (card044 rows at 97644961); `cine_skip_step`/`c135_step` 84 ms is the
  game's GameStarted (known, 2026-11-16 audit); `c132_digest_step` 14 to 23 ms once per map (card 517 cuts FED_C3M2).
- `c114_step` 11 to 78 ms (soak3m1b366c client 77.4 ms). Traced 2026-11-21: once per process, not per map. Every
  spike is on `3D_Frontend` in the first engine tick, which is `c114_start` (the card 114 difficulty dump: 4 rows, 4
  presets, 113 waves, 105 KB defaults file written; its own row says 72 ms). On gameplay maps the step's max is 0.4 to
  0.5 ms (FED_C3M1, FED_C5M1). It is a main-menu hitch, so it is not a gameplay target. Dropped.
- Gameplay spikes to trace: `c140_pace_step` 31.3 ms, client `GibPart` row 34.3 ms, `Fx` row 40 ms,
  `Objective` row 28.9 ms, `WorldOp` row 18 ms, `c37_step` 14.1 ms.

## Next step

One card per target, each with a written prediction before any run. Order: 1 (card 568, b376), then the client recv rows (GrenadeSpawn per row cost, GibPart and Fx worst rows). Each
fix rides a bug-card build batch; the proof is the same card330 rows from that run.

## Target 2 trace: client pose step (2026-11-21)

Static map (qwen read of pose_c132.cpp, checked by the senior on the stream path): every frame the client runs
pose_freeze_tick, arrival_tick, sprite_wait_tick, rest_check_tick, pose_wait_tick and pose_stream_tick; the 250 ms
pose_client_step retries pending rows. pose_stream_tick is the only O(rows x bodies) part: each live streamed corpse
gets one physics teleport per body per frame plus velocity zeroing and gravity off. That hold is deliberate (the bodies
simulate, so a skipped frame lets joints drift), so it is not cut blind. Other candidates: arrival_tick and
pose_stream_tick both retry a pending row with a full pose_try_apply every frame (twice per frame with
pose_client_step); rest_check_tick scans all 256 rest rows each frame while one read-back is due.

b377 `bc12138adfaeb5ba` = b376 + a client split row, every 30 s: `card567: client pose step split over N ticks (ms
total): freeze, arrival, sprite wait, rest check, ragdoll wait, stream, 250 ms step; mean pose rows, mean arrivals`.
Prediction: in a combat window with many corpses, stream plus arrival hold most of the step's ms (pose step 660 ms
per 30 s at the b367 peak). The cut is picked from these rows, not before.

### Target 2 result, run c558b377b (b377, FED_C4M1 mech)

Split rows per 30 s: stream 14.1, 37.6 and 50.7 ms at 1.1, 6.1 and 10.3 mean pose rows (85 to 95 percent of the step);
freeze 2.2 to 4.5, ragdoll wait 1.5 to 1.7, arrival 0 to 0.2, the rest 0.3 or less. The prediction held for stream
only: arrival retries cost nothing. Stream grows about 5 ms per 30 s per live row (about 0.17 ms/s per corpse, 3 us a
frame), at 10 rows 1.7 ms/s. The per-body hold is deliberate, so no blind cut: next is a stream-internal split
(teleport vs velocity/gravity writes) only if a heavy-corpse soak shows stream over 5 ms/s. Card 568 in the same run:
client_significance_step 72 to 115 ms per 30 s (b371 peak 1629), max call 1.62 ms.

Same run, client: 299 `WARN card132: client decal N not spawned: no material` plus 289 `release ... found no decal (no
copy)`, all from three hole materials that the client never loaded (MI_FX_Decal_Needle, MI_FX_AutocannonBulletGround_Decal,
MI_FX_BulletGround_Decal). This is the card 566 cause, and the b378 load fallback covers it.

## Target 8 trace: client GrenadeSpawn row (b379 `2b94706ae9a7bb61` = b378 + split row, build-b5 copy kept)

`c74_receive` (grenades.cpp) per row: chan_text and a 12-field sscanf, `c50_resolve(path)` (StaticFindObject, else
StaticLoadObject), the pooler spawn `g_c74_spawn_fn`, then velocity, tables and the log row. New client row every 30 s
with rows: `card567: client GrenadeSpawn split over N rows (ms total): parse, resolve, spawn, rest`. Prediction: in a
mech fire window, spawn holds most of the time; if resolve is over 20 percent, cache the last path's class (mech rounds
repeat one path). Rides the next mech (C4M1) run.

### Target 8 result, run c558b379a (b379, FED_C4M1 mech)

Split rows: spawn holds 93 to 96 percent of the row (0.21 to 0.24 ms per row); parse plus resolve are under 0.02 ms
per row. So the class cache is not needed. The only lever is the spawn itself: the pooler call `g_c74_spawn_fn` builds
a full projectile per mech round. Next (its own card, perf mod family): measure whether the client pool returns a
reused actor or spawns new (a pool hit/miss count on the same row), then size the pool from the peak rows per second
(688 rows per 30 s). No blind cut.

### Target 8 pool count, in b424 (senior 2026-11-21)

The split row gains `pool hit H, miss M`. A hit is a pooler return that is an actor this client already got from the
pooler (a ring of the last 256 returns, checked by pointer and then by weak ref, so a freed and reallocated address is
not a hit). A miss is a new actor, or one that left the ring.
Prediction (C4M1 mech run): after the first fire burst most rows are hits (the pool warms), so the 0.21-0.24 ms per row
is the pooler's re-init of a reused round, not an allocation. If misses stay high in later windows, the pool is too
small for 688 rows per 30 s and the next step sizes it.

Result, C4M1 mech c567b424a (b424 `784f6a4e07de92ac`, 153 s, host rc 0, client rc 0): three client split rows.
500 rows: spawn 109.96 ms, pool hit 469, miss 31. 203 rows: spawn 40.16 ms, hit 203, miss 0. 157 rows: spawn 30.29 ms,
hit 157, miss 0. The pool warms in the first burst and every later row is a hit (prediction met), and spawn still
costs 0.19-0.22 ms per row on hits. So the pool does not need sizing: the per-row cost is the pooler's re-init of a
reused round (or the work around it), and that is where the next step for this target looks.

## Re-rank on b376 to b440 (senior 2026-11-21): closed

Source: the card330 window rows of c558b376a (C4M1 mech fight), c593b438a (C3M1, 16 warriors) and c556b440b (C2M1
rack). These are the top-5 steps summed per window.

- The mod's total is now 1.10 to 1.50 % of host frame time and 1.26 to 1.81 % on the client in the busiest windows
  (2.0 to 2.7 % and up to 8.3 % in the b366 to b371 rows above).
- Target 1 is done (card 568): the client significance peak fell from 1629 to 265 ms per 30 s.
- Target 3, `c132_ext_step` (host), is still the host's top step: peak 104 ms per 30 s. The split rows are 53.6 to
  58.5 us per tick: poll 12.7 to 16.6, class lookup 4.2, c140 visit 34.3 to 35.2 (about 3800 calls per sweep, about
  1780 reach `c140_placed_entry`). That is about 3.4 ms/s, or 0.3 % of host frame time. Reaching fewer entries means
  polling still placed actors less often, which delays a moved actor's row (the 100 ms latency rule). Not carded.
- The remaining steps each peak under 125 ms per 30 s (c37_step 123, c43_step 78, c140_pace_step 73, c50_step 67,
  c132_pose_step 65, decal 49 to 59). None is worth a card on its own.
- GameThread headroom now lies in the game's own work (card 573 umbrella, card 527 family), not the mod. The
  per-row client recv costs (GrenadeSpawn 0.19 to 0.22 ms per pooled row, GibPart and Fx worst rows) get a card only
  if a combat-window profile puts them in the top rows again.
