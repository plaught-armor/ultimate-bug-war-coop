# 330 Per-frame cost of the mod

User, 2026-11-14: "More efficiency audits need to be done on working systems". Senior static audit; second card of
the class after 329 (network bytes). After the playtest blockers; step 1 is a diagnostic that rides a batch build.

## Finding 1: the play build measures nothing (settled)

- `ubw_post_tick` (`mod/src/ubw/hooks_ubw.cpp` 150 to 250) calls 58 `*_step` functions every engine tick.
- The only timing is card 252: `C252Timer` over the whole tick and over `c132_digest_step`, plus Chan, FxApply,
  Audit, Pose and DigestRecv spans. It runs and prints (`mod ms: chan ... other ...` in the `card132: frame window`
  row, `grenades.cpp` `c132_util_step`) only while `g_c132_fx_log` is set (`UBWCOOP_C132_FX`).
- That flag is off in the play build and in the current test runs: 0 `mod ms:` rows in the playtest host log
  (`client/playtest-menu-20261113-182510.log`), the playtest client log and both `logs/c319-played-c1m1-*.log`.
- So no log says what the mod costs per frame in a played session, or which step costs it. "other" in card 252
  lumps 56 steps together even when the flag is on.

## Finding 2: world scans (static; cost not measured)

`c23_collect` (`hooks_ubw.cpp:996`) walks every actor of every level with an `is_a` per actor. 31 call sites. Each
caller keeps its own list; none shares a scan made in the same tick. Per tick or near it:

- `c23_step` (`enemies.cpp:442`): one enemy scan per `kC23ScanMs` while armed.
- `c37_scan` (`enemies.cpp:651`): every `ASTCharacter`, client, while armed or auditing.
- `c41_resolve` by cell (`enemies.cpp:3413`): a full NPC scan for each received Anim frame that names a cell. The
  playtest sent 2.5% of channel bytes as Anim; the frame count per tick is not in the log.
- Timed (1 Hz or slower, by their own `next_ms`): `c75_count_step`, `c76_step`, `c125` turret poll, dropship live
  snapshot, `anim_probe`, `move_probe`, `c39_step` when a probe is due.

Not settled: actors per map and the time of one scan. Step 1 gives both.

## Finding 3: log volume (measured, playtest host log)

- 115763 lines, 19.5 MB in 4892829 ms (81.5 min): 23.7 lines/s, 0.41 lines per frame (280173 frames).
- By prefix: `card132` 28172 lines (4.2 MB), `card074` 23123 (3.4 MB), `netbudget` 12529 (2.1 MB), `playlog` 12343
  (1.5 MB), `card130` 6317, `card098` 6168, `card137` 4058. Most frequent single row: `card130: host fx grenade
  explosion`, 5573 lines.
- Each `log::line` is a `snprintf`, a `vsnprintf` and a file write on the game thread (`mod/src/ue/log.cpp`). Its
  time is already counted (`log::take_line_ticks`) but only printed under `UBWCOOP_C132_FX`.
- `card074` in a play build (settled 2026-11-14): 23062 of the 23123 rows are `FX SpawnSystemAtLocation`, all from
  one caller (`exe+0x51DAFA3`), 7222 `NS_FX_Weapons_MoritaHit_Ground_AI` and 7212 `NS_FX_Gore_BloodHit_Green01` on
  top. `detour_c74_niagara` (`grenades.cpp:1287`) logs on `g_c74_fx` alone, and `g_c74_fx` is true inside every
  relayed round's OnHit (`grenades.cpp:1669`). The sibling `detour_c74_st_niagara` (:1067) tests
  `g_c74_fx && g_c74_log`. Fix: the same test at :1287 and at any other `c74_log_fx` caller missing it. Rides the
  batch 2 build; proof: 0 `card074: ... FX` rows in a run without `UBWCOOP_C74_LOG`.

## Step 1 (diagnostic, both sides, always on, play build included)

Time each step of `ubw_post_tick` with one `QueryPerformanceCounter` pair per step (58 pairs per tick; a table
indexed by step, no allocation), and count `c23_collect` calls, actors walked and ticks spent inside it.

Per 30 s window, one row: `card330: <side> t=<ms> window <ms>: <frames> frames, mod <ms> total (<pct> of frame
time), max tick <ms>; top 5 steps <name> <ms> max <ms>, ...; log <ms> <lines> lines; collect <calls> calls <actors>
actors <ms>`.

Map end, one row per step with non-zero time, sorted: `card330: <side> map <name> step <name>: <ms> total, <us>
mean, <ms> max, <ticks over 1 ms>`; and one `collect by caller` row (calls and ms per call site).

The card 252 `mod ms:` fields stay as they are; this row replaces "other".

## Predictions (written before the run)

- Mod total under 5% of frame time on both sides in a calm window; the fight windows are the unknown.
- `c23_collect` and `log` are among the top 5 on the host in a fight; `c37_scan` on the client.
- No single tick over 5 ms outside map load and join. A step with max over 5 ms in play is a finding by itself.

## Fix classes (after step 1, only for steps the rows name)

- One actor scan per tick per class, shared by every caller in that tick (one owner of the list), instead of a
  scan per caller. `c41_resolve` by cell reads the shared list.
- A step that polls for a change the mod already hooks gets driven by the hook.
- Log rows that repeat per event in play (`card130: host fx grenade explosion`, per-row `card132`, `card074`)
  become counters in a window row; the first occurrence and every WARN still print. Diagnostics a card still
  needs stay behind that card's flag.

## Sibling sweep

Hooks outside the tick that run per engine call (ProcessEvent-class detours, the Fx and sound detours, the net
send path): list them with their call counts from one soak, same row format, before any fix is coded.

## Done when

A C1M1 soak and the next playtest log carry the card 330 rows on both sides, mod total and the top steps are known
from the log, and each step over the limits above has either a fix with a before/after row or a note why it stays.

## 2026-11-14 12:30, c140 visit cut measured (DLL `2c3c6be39321ee19`, `logs/c330cut-soak-c2m1-host.log`)

Cut: `c140_placed_visit` looks at a root that is not Movable only in every 16th ext sweep (`kExtFullEvery`); the first
sweep of a map is full. C2M1, per ext tick, 8 rows:
- c140 visit 79.9 to 123.7 us (before: 293.7 to 367.9).
- ext total 146.9 to 219.1 us (before: 359.1 to 451.9).
- actors reaching `c140_placed_entry` per sweep 2599 to 3199 (before: about 30k of 30.6k).
- 0 `WARN card140: ... not Movable` on both sides.
C1M1 row: visit 116.5 us, total 204.6 us. Open: the `card140: host placed class` set over the C1M1 range part (next
c310-proof run). The class lookup (55 to 72 us) is now the next largest part.

## 2026-11-15, read of the b50r2 rows (DLL `f63840e619112273`, FED_C2M1)

- Finding 3 (log volume) costs little: window rows give log 1.5 to 3.6 ms per 30 s on the host (111 to 456 lines)
  and 2.4 to 5.8 ms on the client. The card 074 gate (`g_c74_fx && g_c74_log`) is in. No further log fix for cost.
- Finding 2 confirmed on the client and carded: card 367 (`c37_scan` walk every frame, 1164.3 ms in 2118 calls).
- Sibling sweep, `detour_process_event` (`death.cpp`), read statically: 2 stores and 8 flag tests around the call;
  the gates open in play (`g_c161_pe`, `g_c345b_pe`, `g_c158_pe`, `g_c44_pe`) lead to pointer compares against small
  tables, with capped log rows. No per-call scan, string compare only after an FName match (`c44_after_pe`). No
  finding. The Fx, sound and net send detours are still not counted per call.

## 2026-11-15, read of the b52 rows and the uncounted detours (DLL `e3d10276a0c5ff9b`, `logs/b52-host.log`)

Host steps left open by card 368, each read against the source. None needs a change.

- `c132_ext_step`: 140.6 to 190.9 us per tick (poll 25 to 33, class lookup 40 to 54, `c140` visit 75 to 103), about
  1000 actors per tick, 113 to 116 sweeps per 3600 ticks. Already tuned here (`kExtFullEvery`). Halving the budget
  would double the latency of a placed actor's move reaching the client (full world sync rule); left as is.
- `c43_host_step` 87.19 ms max: one tick in window t=390059043 to 390089051, during the scenario's all-down test. The
  path is `c43_all_down` / `c43_kill_host`, an event that runs the game's own death work, not a periodic cost. Steady
  cost about 29 us mean including the `c130_pickup_step` NPC collect.
- `auto_dropship_step` 680 to 686 ms single ticks: test only (`UBWCOOP_AUTODROPSHIP`, flight skip). Not a play cost.

Detours not counted per call, read statically (closes "Fx, sound and net send detours still not counted"):

- `detour_net_replicate_actor` (host, each actor channel that wrote bits): `net_add` scans up to 192 class rows by
  pointer; for an NPC, `net_note_npc` scans all `g_net_npc_count` rows (up to 256, no early exit) and copies health,
  location, yaw; `npc_props_add` is one hash probe. The busiest b52 window has 3579 `props` calls in 5 s (716 per
  second, top 14 classes), of which NPC classes at most 1109. At about 1 us per call that is under 1 ms per second
  (under 0.1% of frame time). Diagnostic for card 329; remove with it after the playtest rows are read, no change now.
- `detour_net_remote_function` (host, each RPC): two reads of the connection's pending bits, `net_add`, two small
  table updates. No scan beyond the 192 class rows.
- Decal, sound and animation detours (`detour_spawn_decal_at`, `_attached`, `detour_c99_sound_2d`,
  `detour_c104_start`, `detour_skel_anim_start`, `detour_c132_show_static`, `detour_frame_size_changed`,
  `detour_c41_try_play`, `detour_c163_broadcast`): per event, pointer and flag tests, log rows behind caps. No level
  walk, no uncapped log.
- `detour_spawn_gore` (`enemies.cpp`): formats a 256-byte row (`snprintf` with 9 floats and `class_name`) on every
  gore response, then `c39_line` drops it unless `g_c37.audit` is on. Wasted in play, a few us per
  bug hit. Small; fix with the next source batch: build the text only when `g_c37.audit` is set. Not worth its own
  build or run.
- `detour_update_colliders`, `detour_c41_frame`, `detour_c40_within` are installed only with their audit gates
  (`g_gate_c23`, `g_gate_c41`, `g_gate_c40`), off in play.

## 2026-11-15, read of the b53 rows (DLL `4e4b316a3a920505`, `logs/b53-{host,client}.log`, FED_C2M1 first stay 39.4 s)

- Card 368 took host `c28_step` 326.1 -> 108.9 ms, host `c125_step` 258.2 -> 81.0 ms, client `c28_step` 249.6 ->
  26.1 ms, client `c208_step` 114.5 -> 5.5 ms. Mod share per 5 s window: host 3.09 to 3.42%, client 2.55 to 3.90%.
- `c43_step` is mostly test cost. Host b52 461.7 ms = `c43_host_step` 152.1 + `c50_step` 38.5 + small steps + about
  265 ms in `g_c43_test` steps; client b52 1437.1 ms was mostly `c43_test_client_step`. The play build runs neither
  test branch. No finding.
- `client_significance_step` 397.7 ms is the game's own significance update for the client's controller, which the
  client needs. No finding.
- Level walks left, by caller (calls, ms): host `c368_actors` 232, 170.4; `ds_host_track` 76, 53.0 (two classes a
  second); `c130_pickup_step` 73, 53.5 (every 500 ms); `c75_count_step` 37, 23.0 (every second). Client `c37_scan`
  338, 255.7 (every 100 ms, card 367); `c368_actors` 145, 112.2; `c50_find_placed` 120, 65.2 (the `kDsNamed` miss,
  fixed in source for b54). Together about 0.8% of host frame time and 1.1% of the client's.
- Possible later card (not written, low value): one level walk per period filling every class list at once would
  bring the host from about 11 walks a second to 4 and the client from about 16 to 10; about 0.5% of frame time
  each. Not worth a build before the playtest; revisit only if the playtest logs show walk hitches.
- `detour_spawn_gore`: the 256-byte row text is now built only with the audit (source, for b54).

## Run b54 turret (2026-11-15, DLL `cd14c3d92a441cf0`, FED_C3M1, `tools/scenarios/c125-proof.txt`, tag c125b54)

Proof of the card 125 turret rows on the 250 ms cached turret list (card 368 `g_c368_turrets`). Scenario rewritten
to the gameplay test rules first (pins, point stands with `wait stand`, `clear 3000 kill`, gated looks, log waits,
level end for the map end rows). Predictions, written before the run, both logs:

1. Every card125 row the sender logs is applied on the other side: host `row sent:` = client `row applied` (both
   forms), client `aim sent:` = host `aim applied:`, counted while under the 60-line cap; exact totals from the
   card132 ledger map end rows (TurretAim sent = recv applied; WorldOp carries the 'T' rows with the other kinds).
2. The first card125 row for each mounted turret is within 500 ms of its mount (client: `card123: host TurretMount
   from`; host: its `key E` command row).
3. Copy aim follows: after each turn the other side's 1 Hz state row for that turret shows the sender's last aim
   (+0x770 rel yaw within 1 degree of the last `aim sent` / `row sent` yaw).
4. Host `card330: host map FED_C3M1 step c125_step` row and the c368_actors collect site row quoted.
5. FATAL/ERROR 0; WARN lines listed by class.

### Results, run c125b54c (`logs/c125b54c-host.log`, `logs/c125b54c-client.log`, `logs/c125b54c.drive`)

Three runs of the budget. c125b54: VOID at line 16. The host waited for its own pawn before `ui continue`, but C3M1
starts in the dropship, so the client never confirmed; card344 WARN at 393218155 sent the host to the menu. Fix:
map, the card320 hold row, both continue, then the pawn waits. c125b54b: VOID at line 39, "look 3: no distance row".
The `focus=` aim scan takes the turret bounds (BP_MountableTurret_C_8, extent 1116 x 1100); 0 of 26 points hit, it
chose pitch 29.3. Fix: a plain `look actor` on both mounts, with the mount rows as the gate (as in c125q). This
departs from the focus= rule for turrets only. The first enemy-near wait also became a `try`. c125b54c: passed.

1. PASS. Line counts under the 60-line cap (cap hit at host 393700280, client 393700313): host `row sent` 40, client
   `row applied` 24 + `row applied (own turret, aim kept)` 16 = 40; client `aim sent` 20, host `aim applied` 20.
   Ledger at map end: client TurretAim sent 52, host TurretAim recv handled 52. Host WorldOp sent 506, client WorldOp
   recv applied 261 + handled 245 = 506.
2. PASS. B (client): host `card123: host TurretMount from` 393696062, first host `row sent` for B (in use 1) 393696132,
   70 ms; the client's copy applied it at 393696844 (own turret form). C (host): host `cmd: 7 key E -> done` 393698388,
   first `row sent` for C (in use 1) 393698385, same tick; the client's in-use row for C 393698413.
3. PASS. B, the client's turret: client last `aim sent` yaw 1.9 at 393700226; host state 393700398 +0x770 rel
   (-5.0, 1.9), client 393700947 (-5.0, 1.9). After the second turn host 393703414 (-5.0, 0.0), client 393703981
   (-5.0, -0.0). On the bug: client 393705998 (-11.7, 0.0), host 393706434 (-15.0, 0.0), client 393706999 (-15.1,
   0.0). C, the host's turret: host 393700398 (-0.0, -0.8), client 393700946 (-0.0, -0.8); host 393703414 (0.0, 1.1),
   client 393703981 (0.0, 1.1). Each copy matches the sender within one 1 Hz sample.
4. Host `card330: host map FED_C3M1 step c125_step: 96.2 ms total, 42.9 us mean, 1.42 ms max, 5 calls over 1 ms`
   (393722707). Collect site mod+0x1380be (c368_actors): host 242 calls 148.2 ms, client 107 calls 68.8 ms
   (`collect by caller` rows). Other host sites: mod+0x72700 77 calls 42.7 ms, mod+0x14588 82 calls 32.6 ms. Other
   client sites: mod+0x45291 269 calls 163.1 ms.
5. PASS. FATAL/ERROR 0 on both. Host WARN 4: attach DisableThreadLibraryCalls err=126; autodropship null pause
   requester, no action; card072 AmmoMirror with no server copy inventory; card346 guard GetViewTarget on a null
   camera manager at the 3D_Frontend travel. Client WARN 19: attach 1; card132 CollisionOp recv late 5 (sent
   393690410, applied +32 to +42 ms); card152 bone check guid 0xd4 10 and pose stream totals 1; card191 frozen bone
   off body 2 (guid 0xd4 82.7 uu, 0xe8 127.2 uu). No crash dumps.

## 2026-11-15, read of the b54 rows (DLL `cd14c3d92a441cf0`, `logs/b54-host.log`, `logs/b54-client.log`)

Calm windows, 30 s each: host mod 926.4 to 933.7 ms (3.09 to 3.11% of frame time), max tick 23.49 to 27.42 ms; client
1093.3 to 1133.5 ms (3.64 to 3.78%), max tick 2.81 to 3.49 ms. Under the 5% prediction on both sides.

- `c293_step` (host 108.4 to 110.3 ms, client 99.3 to 100.5 ms per 30 s, about 0.35%): the card 293 minimap marker
  diagnostic. It samples every tick while a suspect marker is listed, one visibility trace per NPC marker
  (`c293_reasons`). The b54 suspects are live troopers flagged below-ground for 914 to 4689 ms. Card 293 is open and
  waits for the playtest log, so the step stays as it is tonight. When card 293 closes: remove the step, or keep
  only the 1 Hz sample.
- `c132_ext_step` (host 299.7 to 305.4 ms): unchanged from the b52 read (`kExtFullEvery`), left as is.
- `client_significance_step` 423.4 to 452.8 ms and `c37_step` 278.5 to 286.7 ms on the client: as read on b53.

## 2026-11-15, read of the b60 rows (`logs/c375b60-{host,client}.log`): step maxima that are not mod cost

- Client `cine_skip_step` max 1505.20 and 1861.22 ms (2974.8 and 3335.6 ms per 30 s window; the same shape in
  `c346b59`, 2173.35 ms). Each hitch is one test skip press: the four `card130: client cinematic skipped
  WB_Cutscenes_C` rows sit at +1465 to +1860 ms after the pause request, on the ticks with the gaps. The game's own
  skip handler runs inside the step's ProcessEvent call. `cine_call` runs only under `config::test_skip()`, so the
  play build never pays this inside the step (the player's own key press runs the same game code).
- Host `auto_dropship_step` max 681.14 ms: the FAST=1 dropship skip, test only.
- Calm windows on b60: host 368.5 ms per 30 s (1.23%), max tick 3.69 ms; client `c346b59` 349.6 ms (1.16%), max
  tick 2.72 ms. No new step in the top 5.

## 2026-11-15, read of the b70 rows (`logs/playb70-{host,client}.log`, play mode)

- Calm windows: host 1.46 to 2.18% of frame time, client 1.35 to 2.14%; the top 5 steps are the ones read on b53/b54.
- Client `cine_skip_step` max 1522.53 ms in play mode is not a skip press: it is card 135's `GameStarted` call
  (`card135: client GameStarted t=414206930`, logged at 414208452) building the title screen under the loading screen
  with world rendering off (card 337). Game code, once per map start, not visible.
- Host on FED_C2M1: one 19 to 23 ms tick per 30 s window, the digest build. Carded as 386.
- Host frames of 788 ms (FED_C1M1) and 771 ms (FED_C2M1) at the dropship landing (non-FAST ride): the long frame ends
  before the first landing row (`DescendToLandCompleted`), and the mod's tick in those windows is under 4 ms. The
  game's own landing frame (level script, encounter activation), not mod cost. The client has no such frame.

## From card 427 (merged here 2026-11-17 13:09)

Card 427's byte goal was met without the path table (see `done/` copy). Left over, all small, to be taken only
with another change to the same rows:
- Client CPU: `c74_find_asset` runs `swprintf` plus StaticFindObject per GrenadeSpawn / GrenadeExplode / round row;
  no timing row exists. Add it to this card's step table before deciding on a pointer cache.
- GrenadeExplode is 76 B a row against a 50 B target: read one row's token list before changing anything.
- Quaternion `%.6f` to `%.4f`: 4 B a row.

## Read of the b223 rows (senior, 2026-11-18 12:33; `logs/c245b223a-*`, `logs/c448b223a-*`, DLL `5dd69a9bb162d8ef`)

Mod total per 30 s window: c245 (FED_C4M2, the boss fight) host 1.24 to 2.35%, client 1.31 to 2.64%; c448 (FED_C1M1)
host 0.62 to 0.71%, client 1.43 to 2.39%. The 5% prediction holds in a fight on both sides.

Step maxima over the 5 ms limit in play (test-only steps left out: `cine_skip_step`, `auto_dropship_step`, and
`c114_step` at map start or on the front end):
- Host `c132_digest_step` 17.23 ms: the digest round, card 386 (closed; play interval 120 s).
- Host `c43_step` 15.33 ms (one window, c245), client `c43_step` 6.97 to 8.84 ms with `c50_step` the same value in the
  same window (c448): the c50 time is inside c43's, so this is one call path, most likely a Down or join replay.
- Host `c315_host_step` 10.45 ms (one window, c245).
- Client `c37_step` 6.82 and 9.41 ms (c245).
Host windows reach a max tick of 17 to 21 ms while each top 5 step's max is near 1 ms, so the spike sits in a step
outside the top 5 or in the log write. The map-end per-step rows print only on the front end in these runs (the
mission map's row is lost when the run ends in the map), so they cannot name it.

Next, rides a build: print the per-step max row for any step whose single call goes over 5 ms, at the time it
happens (`card330: <side> t=<ms> step <name> took <ms>`, capped per step), so the window that has the 21 ms tick
names its step. No fix until a row names one.

## Closed (2026-11-18 14:33)
Evidence: the per-step over 5 ms rows (kC330SlowMs 5.0, kC330SlowRows 8) named every spike. c244b228a: the mod is 1.29% of client frame time; the client peak (c114_step 22.51 ms) is a load frame 113 ms after LoadMap returned; the host peak (c43_step 13.87 ms) is c45_respawn_check inside the +90 s card049 DIAG dumps plus card132 inventory rows, logging cost under the test log switches only. The b224 client cine_skip_step spike does not appear on b228.
Not done: no fix; no spike is a play cost.
Reopen when: a `card330` slow row names a step outside a load frame and outside a DIAG dump, or a play log shows a client frame window under 500 frames with the mod above 5% of frame time.
