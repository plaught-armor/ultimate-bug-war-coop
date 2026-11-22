# Card 573: performance side mod, lowest CPU and GPU use and highest frame rate (umbrella)
Status 2026-11-22: open, parked until after the user's playtest. Shipped as ubwperf.dll, step C and further cuts are open.

User 2026-11-21: "I, as a side mod, want to bring down GPU and CPU utilization as low as we can and bring out as much
performance as possible."

This card is the program for the performance side mod (the card 527 family). It widens the scope from GameThread
headroom to every CPU thread and the GPU. The co-op mod's own cost stays in card 572.

## Goal and metric

The same workload costs less on every processor:
- At the 60 fps cap (the normal test setting), lower busy percent per thread and lower GPU busy.
- Uncapped, a higher frame rate.

Each step is measured in a matched window: same map, same phase (quiet window and combat peak), both sides, low
footprint pinning. Numbers per window:
- Per-thread CPU busy: GameThread, RenderThread, RHIThread, the Background Work and TaskGraph workers, and the Wine
  and Proton threads. Source: `perf record` samples by thread name through `tools/perf_frame.py`, plus
  `/proc/<pid>/task/*/stat` deltas for a cheap whole-run curve.
- GPU busy: the harness `logs/<tag>.gpu` samples (card1 `gpu_busy_percent`, every 10 s).
- Frame ms: card 330 window frames.

## Rules

- Card 527 rules hold. Host decisions are never changed (spawns, NetGUIDs, AI choices, damage), and the digest, card
  140 and `Pose:` stay at 0 mismatches.
- Lossless cuts first. A lossless cut leaves the image the same at the same settings: skipped work nobody sees, a
  per-frame result cached, a busy-wait made a sleep.
- Cuts that trade image quality (shadow, LOD, resolution or FX scalability) go only into a separate opt-in preset,
  which is opt-in (user decided 2026-11-21: lossless by default plus an opt-in preset). They are never a default.
- Each cut has its own off switch (`UBWCOOP_C<card>_OFF=1`) for A/B, and each runs alone in single player.

## Packaging (user decided 2026-11-21: own dll, the co-op mod loads it)

The perf code today is in the co-op dll (`perf_*.cpp`: cards 531, 536, 538, 539, 544, 548, plus card 568 in
enemies.cpp). It moves to its own dll, `ubwperf.dll`. The co-op `dwmapi.dll` loads it when it is
present. Without the co-op mod, a second proxy name loads it. That name must be an import of the exe other than
dwmapi; its import table has to be read first. Moving the existing perf files is its own card, after the
measurement step.

## Workstreams

A. **GameThread** (in progress). Chaos queries make up about 26% (card 534 names the callers), NPC component ticks
   31% (card 527 tier fields), significance at 30 Hz (card 568), component and save-id lookups (cards 531, 536, b390).

B. **Idle at the cap** (new, likely the cheapest win). When capped, the frame limiter waits out the rest of each
   frame. If UE or Wine spins in that wait (`FPlatformProcess::Sleep(0)` loops, `t.MaxFPS` spin), the CPU shows busy
   while it does no work. Check the quiet window profile for samples in the limiter wait, sleep and yield functions.
   If they are there, replace the spin with a timed wait (lossless; frame pacing checked from the card 330 frame
   counts).

C. **RenderThread, RHI and GPU** (new, nothing measured yet). First get the thread shares (perf by thread) and the
   GPU busy curve against bug count. Lossless candidates, each needing a measurement before a cut:
   - Draws for hidden things: corpses and gore off screen, inactive Niagara components that still render (the host
     scan counts 2170 components, 1937 inactive).
   - Shadow casting on small bug parts and gore.
   - Per-frame dynamic resource updates for actors that did not move.
   - The decal cap was sized in card 527 (500, no GPU slope measured).

D. **Worker threads**. Background Work had 11% in FindComponentByClass (fixed by card 536). Rank the rest from the
   same profile.

E. **Shader and load hitches**. PSO compile and stream-in stalls show up as single-frame spikes, not as averages.
   Count frames over 50 ms per window. Proton's vkd3d shader cache state is part of the measurement.

## Step 1: baseline (one run, no code change)

One C1M1 run on the current lane 2 build with `UBWCOOP_C571=1`. Take a 20 s perf record per side in a quiet window
and in the combat peak, plus `.gpu` and task stat deltas. Write one table per side: per-thread busy %, GPU busy %,
frames, and frames over 50 ms. Rank workstreams B to E by ms saved per frame.

## Predictions (written before step 1)

- GameThread is the busiest thread in combat on both sides (over 80%). The RenderThread is under 50%.
- GPU busy stays under 60% even at the combat peak (c263L: 57%), so GPU cuts lower utilization but do not raise
  frame rate under bug load.
- The quiet window at the 60 cap shows over 10% of GameThread samples in the limiter wait (spin), so workstream B
  saves real CPU with no visual change.
- Wine and Proton thread overhead (wineserver, vkd3d submit) is under 10% of process CPU.

## Step 1 result, run c569b390b (b390, C1M1 range, 60 fps cap, junior 2026-11-21)

Files: `logs/c573b390b/` (perf by thread, scored with `tools/perf_frame.py` into `*.frame.txt`; task stat deltas). The
quiet window is `quiet2` (09:48:08-28). The combat window (09:51:48-52:08) is the range nest and two grenade throws,
not a bug wave peak. CPU seconds in each 20 s window, from the task stat deltas:

| Thread | host quiet | host combat | client quiet | client combat |
|---|---|---|---|---|
| Background Work (workers) | 7.3 | 16.3 | 10.7 | 12.9 |
| GameThread | 6.2 | 7.2 | 4.9 | 4.9 |
| Foreground Work (workers) | 4.8 | 5.9 | 5.1 | 4.9 |
| vkd3d-swapchain | 4.5 | 4.5 | 4.5 | 4.6 |
| AudioMixerRender | 1.2 | 2.2 | 1.1 | 1.7 |
| vkd3d_queue | 1.5 | 1.6 | 1.6 | 1.6 |
| RenderThread 0 | 1.5 | 1.7 | 1.4 | 1.5 |
| Process total | 29.9 | 42.7 | 32.5 | 35.3 |

GPU busy (card1, shared by both games): 27 to 36% in these windows, 34% run average, peak 91%.

Predictions:
1. "GameThread busiest in combat, over 80%": failed. The GameThread is 31 to 36% busy on the host and 25% on the
   client. The worker pools use the most CPU. At the 60 cap with no bug wave, the GameThread is not the limit.
2. "RenderThread under 50%": held (about 8%).
3. "GPU under 60%": held (27 to 36%).
4. "Limiter wait spin over 10% of the GameThread": failed as written. The GameThread does not spin. Two spins were found
   elsewhere (rows A and B below).
5. "Wine and Proton overhead under 10%": failed. vkd3d-swapchain alone is 15% of the host's quiet CPU (row A).

## Ranked findings (CPU seconds per 20 s)

A. **vkd3d-swapchain spin, 4.5 s per 20 s (22% of a core), on both sides in every window.** The samples are a tight
   loop: d3d12core.dll +0x43772..0x437A5 calls ntdll +0x21BC0..0x21C5F and kernel32 (a timer read in a loop). This is
   the vkd3d-proton frame limiter that the harness turns on with `VKD3D_FRAME_RATE=60` (run_pair.sh:167). It
   sleeps, then spins up to the deadline. It costs the test machine, and is not the game's own cost. Cut: cap the
   tests with the game's own limiter (`t.MaxFPS 60`), or a limiter that waits on a high-resolution timer, and check
   the frame pacing from the card 330 frame counts. For players: the play build must not set `VKD3D_FRAME_RATE`. A
   perf mod limiter is a candidate if `t.MaxFPS` also spins (to be measured).
B. **Task worker idle spin, `LowLevelTasks::Private::FWaitingQueue::CommitWait`.** About 3000 to 3400 samples per
   window per side, roughly 3 CPU s per 20 s and 10% of process CPU, in quiet and combat alike. UE workers spin before
   they sleep. Next: a static read of CommitWait and its callers for the spin count and any console variable that
   sets it. A shorter spin is lossless.
C. **Detour crowd and nav tile rebuild in combat: dtCrowd plus dtSharedBoundary about 3000 samples, and
   dtBuildTileCacheContours about 1050 samples, per side.** The client's cost equals the host's. The client is running
   the crowd simulation for NPCs that the host moves. Carded as card 575 (co-op mod: client-side waste).
D. **The co-op mod itself.** `ubw::hooks` is 470 (quiet) and 710 (combat) samples on the host and about 150 on the
   client. On the host that is about 1.7% of process samples (card 572).
E. **Audio mixer.** 1.1 to 2.2 s per 20 s. Not a lever yet.

Next: A goes first (test infrastructure, measured in the next run's footprint), then B static, then card 575.
Workstream C (GPU) runs now, in parallel. It has no threshold to clear before it starts.

User 2026-11-21: "We want it as low as it can go." GPU busy has no target level. Every lossless cut that lowers it
counts, at any load. The 60% figure above was only a prediction about frame rate, never a gate. Next for C: one run
that samples GPU busy every 1 s (`.gpu1s`) in a matched quiet window and a bug-wave window. It sends `console` A/B
toggles, one at a time, for each lossless candidate listed under workstream C. It then ranks the candidates by the
GPU busy points each one removes.

## b392 `14e7d599f2bd1ad2` (= b391 + scenario `console` verb, build-b5 copy kept)

The scenario could not set `t.MaxFPS`: the DLL had no console verb, and run_pair splits GAME_ARGS on spaces, so
`-ExecCmds=t.MaxFPS 60` is cut at the space. The lane 2 Engine.ini was not used because lane 1 may share it.

New test verb `console <word> [<word> ...]` (1 to 6 words of `[A-Za-z0-9_.=-]`, 63 chars at most, joined by single
spaces). It runs `APlayerController::ConsoleCommand` on the local PC through `ue::input::console_command`, logs
`card573: cmd console '<text>' sent`, and finishes done. Each side runs its own .cmd file, so each side sends its own.

Finding A test: run_pair `FPS_CAP=0`, then `console t.MaxFPS 60` on both sides at the start, then a quiet window
with task stat snapshots. Compare vkd3d-swapchain and process CPU seconds with step 1, and the card 330 frame counts
for pacing.

## Finding A test, run c569b392a (b392, FPS_CAP=0, `console t.MaxFPS 60` both sides, junior 2026-11-21)

`console` was sent at 154895.7 s on both sides, and the 20 s task stat window started about 3.7 s later. Files are in
`logs/c573b392a/`.

- **The vkd3d-swapchain spin is gone:** 4.47 s per 20 s dropped to 0.02 s on the host, and 4.49 s to 0.02 s on the
  client.
- **`t.MaxFPS` holds 60, shown by the card 330 window frames:**
  - Before the send, the game ran uncapped at about 120 fps (3589 frames per 30 s).
  - The window around the send had 2616 frames: 13.7 s at about 120 fps, then 16.3 s at 60 (predicted 2617).
  - Every window after it had 1788 to 1800 frames, the same as b390.
  - No new fps row is needed.
- **The other numbers are not a matched comparison:**
  - GameThread rose 6.21 to 8.47 s on the host and 4.86 to 7.42 s on the client.
  - Background Work rose 7.34 to 10.83 s on the host.
  - GPU busy read 57-61%, against 27%.
  - Process total rose 29.89 to 31.67 s on the host and fell 32.45 to 29.10 s on the client.
  - Why not matched: this window sat right after the map load and the cap change, not at b390's quiet2 point. Card1 is
    also shared with other sessions' GPU jobs (83% from other jobs just before this run).
- **Open:** UE's own limiter (`UEngine::UpdateTimeAndHandleMaxTickRate`) may finish its wait with a spin on the
  GameThread, which would move the spin rather than remove it. Next: a matched quiet window at the b390 quiet2 point
  with a GameThread `perf record`, scored for samples in the max tick rate wait, plus `.gpu1s` with GPU below 30% from
  other jobs at the start.

## Finding B static read, and b394

The CommitWait spin is fixed in the exe: SpinCycles 53 at the WorkerLoop call site, WaitCycles 587 to 1597 TSC ticks
per round from a per-worker table, a pause + rdtsc loop on this CPU (no waitpkg). That is about 13 us of spinning
on each idle entry, and no console variable sets it. The cut is card 576 (b394 `1e26bc27dc1be5e2`: SpinCycles
53 -> 8 by default, `UBWCOOP_C576_OFF=1` for A/B, `UBWCOOP_C576_SPIN=N`).

Run c569b392b: in the matched window, UE's own `t.MaxFPS` wait spins only 0.3% of the GameThread
(UpdateTimeAndHandleMaxTickRate). The open question from c569b392a is closed: `t.MaxFPS` sleeps. On the host, the
top GameThread cost is AI targeting. `.gpu1s` rose to 100 in the last 7 s with no other-job gate running, so it is
not counted. Next GPU runs keep a continuous other-job gate (`/proc/*/fdinfo` drm-engine-gfx of other processes)
through each window.

## Finding C r.* sizing result: c573rb401a (b401, junior 2026-11-21)

Both sides were at a quiet C_2 point with a 60 fps engine cap. Each phase uses its host console row + 2 s to + 14 s
(8-9 one-second samples), and the mean is card1 gpu_busy_percent. Other-job gfx was 0 in every phase, so no phase is
void.

| Phase | Busy % | Restore | Points vs restore |
|---|---|---|---|
| base | 24.4 | | |
| r.Shadow.MaxResolution 1024 | 25.5 | 25.8 (2048) | 0 |
| r.ShadowQuality 0 | 22.0 | 25.2 (5) | -3.2 (shadows gone, ground flatter in the shot) |
| r.PostProcessAAQuality 0 | 23.7 | 25.6 (4) | -1.9 |
| fx.MaxCPUParticlesPerEmitter 100 | 25.4 | 25.4 (1000) | 0 (the quiet scene has few particles: not sized) |

The values are integers, with about 1 point of noise. The restores drifted 0.8-1.4 points above base, and none fell
below it. At 640x360 the GPU is only about 25 % busy, so gains should scale with resolution.

Both real levers are lossy (visible), so neither goes in by default: the perf mod default stays lossless (card 578). They
become opt-in presets in ubwperf.dll: shadows off (about 3 points), then AA off (about 2). The particle cap needs a
combat window to size.

## Closed sub-cards and leftover levers (senior 2026-11-21)

Closed as done or retired and moved to done/: 527, 531, 535, 536, 537, 538, 539, 540, 543, 544, 545, 546, 547, 548, 549.
Leftover levers from them, none carded until it is the top row of a GameThread profile:

- 536: a column table per component class indexed by object index (the walk is 0.2 %, the detour about 2 %).
- 537: a negative entry for FindComponentByClass none walks (hit rate already 99.69 %).
- 543: a distance cull on footstep notifies (0.43 % GameThread); it needs a distance-bucket diag first.
- 545: the 1.7 ms max Chaos sweeps in USTGroundedMovementControl::Tick_DirectMovement (query side).
- 549: ally collider overlap events on only while firing or aiming (not exact as written).
- 517 (b): the digest build skips the 23,407 never-moving server-only actors by a per-level array index plus a
  pointer check, without reading the actor. At most about 1 ms per 30 s build (c263L441 class phase 1.06 to 2.15 ms).

## Next cut ranking and the b455 caller capture (2026-11-22)

Item B (the task worker CommitWait idle spin) is done: card 576, b394, SpinCycles 53 to 8. The next cut is ranked from the
c593b437ca bug-wave profile (logs/c593b437ca-gt/host-frame.txt, host GameThread 8389 samples, 16 warrior bugs, god):

| Rank | Cluster | GT samples | Share |
|---|---|---|---|
| 1 | Chaos kinematic and scene query: SetKinematicTarget_AssumesLocked 128, FChaosEngineInterface::IsStatic 93, ChaosInterface::GetUserData 88, three Chaos::TAABBTree rows 74-80 each | about 660 | about 8 % |
| 2 | Niagara: TickInstanceParameters_GameThread 102, FNiagaraSystemInstance 187 in all | 187 | 2.2 % |
| 3 | FWeakObjectPtr::Get 119 (callers spread) | 150 | 1.8 % |

The Chaos cluster ranks first, but its callers are not known: that capture has no branch stacks, and the shipping exe has
no frame pointers. A lossless cut needs the caller (for example kinematic bone updates on bug skeletal meshes each tick,
or overlap queries from a mod or game detour). So b455 takes one caller capture first and no code change:
tools/c573_capture.sh now takes `BRSTACK=1` (perf `-j any,u`; plain `-b` records empty stacks at perf_event_paranoid 2,
checked 2026-11-22), in the combined run c601b455a (tools/scenarios/c601-c3m1-b455.txt).

Predictions (c601b455a, read with `tools/perf_frame.py logs/c601b455a-gt/host-gt.txt --log <host log> --callers
SetKinematicTarget --callers IsStatic --callers GetUserData --callers TAABBTree`):

- The capture gate is met in one or two attempts. The host GameThread sample count is within 20 % of 8389, since the
  -j sampling cost is small at 499 Hz.
- The Chaos cluster is again 6-10 % of host GameThread samples.
- At least 60 % of the SetKinematicTarget samples have an entry branch seen, and one caller holds over half of them.
  If that caller is a per-tick body update that runs for bugs whose pose did not change, it is the cut candidate.
  If the callers spread with no owner above 30 %, the cluster is parked like the targeting cluster, and Niagara rank 2
  is traced next.

### b455 capture result, c601b455a (2026-11-22)

`perf_frame.py logs/c601b455a-gt/host-gt.txt --log logs/c601b455a-host.log --callers ...`, output kept in
logs/c601b455a-gt/host-callers.txt. The gate was met on attempt 1. Host GameThread had 7111 samples, against the
predicted 8389 ± 20 %, so it sits just inside the low edge.

- The Chaos cluster on GameThread is Chaos::TAABBTree 231 (3.2 %) plus FChaosEngineInterface 132 (1.9 %), about 5 %,
  which is under the predicted 6-10 %.
- SetKinematicTarget callers: 31.5 % of samples had an entry branch seen, against the predicted 60 % or more. Of those,
  20 of 23 come from one call site, exe+0x3fe3ba5. The IsStatic site exe+0x3fe31f4 and the GetUserData site
  exe+0x3fe5ea1 are in the same unnamed code. The publics there are misleading
  (FNetworkPhysicsSettingsNetworkPhysicsComponent::operator=).
- Static trace of that code:
  - The function at exe+0x3fe3140 (one of 0x3fe3140-0x3fe5270) holds the IsStatic and SetKinematicTarget sites.
  - Its only reference is a tail-jump thunk at exe+0x3feb6f0.
  - The only reference to that thunk is a `lea` at exe+0x4019eb5, inside
    `USkeletalMeshComponent::UpdateKinematicBonesToAnim`.

  So the cluster is the per-body lambda of UpdateKinematicBonesToAnim: each frame it moves each kinematic physics
  body of a skeletal mesh to its animated bone.
- In 189 of 7111 GameThread samples (2.7 %, a lower bound), the lambda appears in the last 16 branches.
- The TAABBTree GameThread call sites are NeedUpdateElementIn and UpdateAncestorBounds, the acceleration structure
  updates that those kinematic moves cause. The SimOverlapVisitor QueryImp sites are broad phase.

Next step: measure before any cut (tight-detours rule). Add a counting probe on UpdateKinematicBonesToAnim, in the
card 571 counts style, that logs per 10 s:

- calls per frame by owner class;
- bodies moved per call;
- how many calls hit an owner that is dead, a ragdoll, hidden or pooled, or whose collision is off.

That subset can be skipped without loss. Moving bugs still need their bodies, because traces and hits use them.
Skipping by distance, by screen or by frame rate is lossy and is not a candidate. Niagara
TickInstanceParameters_GameThread (rank 2) follows if the lossless subset is small.

## b456 counting probe (2026-11-22)

ubwperf `mod/src/perf/perf_kinematic_c573.cpp` detours `USkeletalMeshComponent::UpdateKinematicBonesToAnim`
(exe+0x4019C30). It is on by default; `UBWCOOP_C573_OFF=1` removes the hook. The co-op card 039 calls (`g_kinematic_bones`)
go through the detour and are counted too. It changes no result.

Every 10 s each side logs one total row, then the top 12 owner classes by inclusive time:

- **Total row.** `card573: <side> t=... UpdateKinematicBonesToAnim total: N game-thread calls, X ms (P% of W ms), K
  other-thread calls, J no owner; skippable ... S calls, about Y ms`.
- **Class rows.** `card573: <side> t=... kinematic bones top i: <class>: calls, ms, us mean, bodies mean and max; dead,
  ragdoll, hidden, collision off, SkipAllBones, skippable`.

How the probe counts each call:

- **Dead.** The owner is an NPC whose CurrentBaseState is Dying, Dead or Overkilled.
- **Ragdoll.** Any body of the mesh is simulating.
- **Hidden.** The actor has bHidden set, or the mesh has bVisible clear. Pool actors sit hidden, so a pooled owner counts
  here and its class row names it.
- **Collision off.** GetCollisionEnabled is NoCollision.
- **SkipAllBones.** The update type is 1 and no body simulates. This is the function's own early return.
- **Skippable.** The call is dead, hidden or collision off, and it is neither a ragdoll nor a SkipAllBones call. This is
  the lossless cut candidate.

### Predictions (b456 combined C3M2 run, host and client logs, written before the run)

1. Every 10 s window has a total row on both sides. Other-thread calls are 0, and there is no `WARN card573` row.
2. Bug NPC classes hold at least 60 % of host call time in any window with 8 or more live bugs. Player pawn classes
   have one row each.
3. Live, non-ragdoll bug calls are SkipAllBones in at least 90 % of cases (card 039: bug meshes set SkipAllBones). If
   so, the per-body lambda time in c601b455a comes from ragdoll calls (corpses) or from non-bug classes, and the class
   rows show which.
4. Host total time is 1-5 % of the window in bug fights (c601b455a: the lambda is in at least 2.7 % of GameThread
   samples).
5. Bug classes have 10-40 bodies per call on average.

Decision rule for b457:

- If skippable calls hold at least 20 % of host kinematic time in fight windows, b457 skips them in the detour, lossless,
  with an A/B switch.
- If ragdoll corpses hold most of the time instead, the next step is a static read of the ragdoll path, to see whether
  sleeping bodies can be skipped. This is not a cut yet.
- If neither holds, the cluster is parked and Niagara rank 2 is traced next.

### b456 results (c602b456b, FED_C3M2, host and client, 2026-11-22)

Windows parsed: 8 host, 7 client (every 10 s, both sides).

1. **Met.** Every window has a total row on both sides, other-thread calls are 0 in every window, and there are 0
   `WARN card573` rows.
2. **Partly met.** Bug classes hold 63 % of host time in the peak fight window (t=210562840: 317.4 ms, 3.17 %), and 14-47 %
   in the other fight windows. Outside fights the top classes are placed skeletal props: BP_MissleSilo_C (3594-3606
   calls, 3 bodies, 6.6-8.0 us), BP_Door_Sliding_Gigantic_C (4 bodies), BP_MissileSilo_Clasp_C and the other sliding
   doors. Together they cost about 90-100 ms per 10 s (0.9-1.0 %) whether anything moves or not. No player pawn class
   has a row on either side: the pawns make no calls that reach the top 12.
3. **Not met for warriors.** Jumper and Hopper bugs are SkipAllBones (JumperBug 6904 of 6938 calls; HopperBug 4184 of
   5140, the rest ragdoll or dead). BP_AI_WarriorBug_C is not: in the peak host window it has 6546 calls, of which 1055
   are ragdoll and 1003 SkipAllBones, so about 4490 live warrior calls update all 22 bodies (27.9 us mean). BP_AI_ArcherBug_C
   is the same (21 bodies, 34 us, 0 SkipAllBones). So the c601b455a lambda time comes mostly from live warrior and
   archer bodies, then from ragdolls, then from the placed props.
4. **Met.** The host total is 1.0-1.7 % outside the peak and 3.17 % at the peak fight window. The client peaks at 2.66 %.
5. **Met.** Bug classes average 21-29 bodies a call (archer 21, warrior 22, hopper 29); jumper has 1.

**Decision for b457: neither cut rule holds, so the cluster is parked.**

- Skippable calls (dead, not ragdoll, not SkipAllBones) are at most 10.5 ms of 317.4 ms in the peak host window (3.3 %)
  and 18.1 ms of 217.5 ms on the client (8.3 %). Both are under the 20 % bar.
- Ragdoll calls are 1055 of 6546 warrior calls at the peak. The probe has no per-flag time, but at the class mean they
  are about 29 ms of 317 ms. That is not most of the time.
- The live warrior and archer updates are not lossless to skip: their kinematic bodies follow the animation for hit
  traces against the physics asset.
- Next per the rule: Niagara rank 2 is traced next. One note is kept for later: the placed props (silo, clasp, doors)
  update every frame at a constant 0.9-1.0 %, moving or not. A static read of whether their bones change between frames
  would show if those calls are lossless to skip. That is a candidate, not a cut.

c602b456c (same map, second run): host peak windows 3.62 % (28762 calls, 361.9 ms) and 3.44 %; other-thread calls 0,
no `WARN card573`. Same decision.

## b457: Niagara rank 2 trace and counting probe (2026-11-22)

**Caller trace (exe call scan).** `FNiagaraSystemInstance::TickInstanceParameters_GameThread` (RVA 0x048D0FB0) has two
callers: `FNiagaraSystemInstance::Tick_GameThread` (call at 0x48D2A67) and `ReInitInternal` (0x48CA0CC, on a reset).
Tick_GameThread is called from `FNiagaraSystemSimulation::Tick_GameThread_Internal` (0x48D30E3, 0x48D33FE), once per
system instance per frame, and from `Spawn_GameThread` (0x48CFA7C) for new instances. So the call count is the live
system instance count per frame. The function reads the instance's attach component (TWeakObjectPtr at +0x48) and
copies its ComponentToWorld and the owner velocity into the instance parameters.

**Probe.** ubwperf `mod/src/perf/perf_niagara_c573.cpp` detours it, on the game thread, counting per 10 s window and per
Niagara system asset: calls, inclusive time, calls with no attach component, calls whose component or owner is hidden,
and calls whose owner is a dead or dying NPC. It changes no result; `UBWCOOP_C573_OFF=1` removes it with the bone hook.
Rows: `card573: <side> t=... Niagara TickInstanceParameters_GameThread total: ...` and `Niagara params top N: <asset>
(owner <class>): ...`.

**Predictions (b457 C4M1 run, written before the run):**

1. Every 10 s window has a Niagara total row on both sides; other-thread calls 0; no `WARN card573 ... not keyed`.
2. Calls per frame equal the live system count: tens to low hundreds, highest in the stadium (sparklers, fireworks,
   flares).
3. Total time is under 1 % of the window (rank 2 was about 1 % of GameThread samples in c601b455a).
4. Hidden-owner or hidden-component calls and dead-owner calls are the lossless cut candidates. A cut is proposed only
   if they hold at least 20 % of Niagara time in some window, and the sparkler rows show whether the stadium systems
   that end at SetWaves C_2 keep ticking after the stop.

## b458 results (c604b458a, FED_C4M1 to Stadium_O1, 2026-11-22)

1. **Met.** Host 42 and client 41 Niagara total rows (one per 10 s window); other-thread calls 0 on both sides; 0
   `WARN card573` rows.
2. **Met.** Peak 275.0 calls per frame on the host and 252.1 on the client, in the stadium. NS_FX_ENV_StadiumSparklers
   (owner NiagaraActor) is top 11 in one window: 5370 calls, 3.99 ms.
3. **Not met, but small.** Peak window 1.48 % of the host's 10 s (164169 calls, 147.7 ms, about 0.9 us a call) and
   1.65 % on the client. The median host window is 0.97 %.
4. **No cut.** Hidden and dead-owner calls together are at most 9.9 % of a window's calls, under the 20 % bar. In the
   peak host window: hidden 1296, dead owner 11356 of 164169 calls. The top assets are gore gib trails
   (NS_FX_Gore_Gibs_Trail), the tent spawner door (NS_FX_TentSpawnerDoor) and Morita tracer trails
   (NS_FX_Enemy_Morita_Trail). They are live, visible effects that a lossless cut may not skip. Rank 2 stays measured
   and not cut. The sparkler stop question is still open: the proof shots taken after Stadium_O1 were void, because the
   map travel had started.

## b460: next GameThread ranking, Chaos and Niagara parked (2026-11-22)

Source: c601b455a host GameThread, 7111 samples, read with `tools/perf_frame.py logs/c601b455a-gt/host-gt.txt
--log logs/c601b455a-host.log --mod-map build-b5/dwmapi-b455-0c2e33933afbf1a3.map --top 400`, summed by cluster over
the top 400 function rows. Wine and system libraries (ntdll.so, ntdll.dll, ucrtbase, libc, kernelbase: 1505 samples,
21 %) have no symbols and are not ranked. Chaos (716) and Niagara (370) are parked; the targeting cluster
(UADTargetComponent, 112) stays parked.

| Rank | Cluster | GT samples | Share | Top rows |
|---|---|---|---|---|
| 1 | Slate UI layout and paint | 518 | 7.3 % | SWidget::Prepass_ChildLoop 66, TPanelChildren<SWrapBox::FSlot>::GetChildRefAt 38, SWidget::Paint 36, TKeyInitializer<SWidget> 28, SImage::ComputeDesiredSize 25, SOverlay::OnArrangeChildren 21 |
| 2 | The mod's own code (ubw::hooks) | 180 (class rows) | 2.5 % | c303_watch 19, detour_process_event 14, c132_ext_step 10, c140_placed_visit 8, c493_after_pe 6 |
| 3 | Replication property compare | 170 | 2.4 % | FRepLayout::CompareProperties 37, FNetPropertyConditionManager::SetPropertyActiveOverride 28 |

FWeakObjectPtr::Get (137, 1.9 %) is just below rank 3; its callers spread (earlier ranking). No probe has run. The
Slate rows point at a wrap box that lays out each frame (SWrapBox children); the first probe would count Prepass calls
per widget class, to see whether a HUD widget re-lays out unchanged content every frame.

## b461: own hooks tightened, Slate census (2026-11-22)

Correction to the b460 ranking. The perf comm "GameThread" covers 8 tids (threads the game thread creates inherit its
comm). The real game thread is tid == pid: 6172 of 7111 samples in c601b455a. `c303_watch` is the card 303 watchdog
thread: a separate tid, started only in test runs (`config::test_skip()`), a 20 ms Sleep poll with three atomic loads.
It costs nothing on the game thread and nothing in a play build, so it is left as is.

Main-tid mod share: about 161 samples (2.6 %). Top: detour_process_event 14, c132_ext_step 10, c140_placed_visit 8,
c493_after_pe 6, c161_pe 5, C330Span dtor 4, detour_net_replicate_actor 4, c99_pick 4, c408_after_pe 4, c143_host_step 4.
c493_after_pe is on all map only under UBWCOOP_DIAG (`g_c493_pe = g_gate_diag`), so its 6 samples are a test cost.

Change (lossless): `c161_pe` (door_c161.cpp) first tests a 64-bit pointer mask of the tracked door actors
(`c161_pe_bit`), so a call on any other object returns without walking the door table (each C161Door is several cache
lines). The other ProcessEvent sub-hooks already return on one function-name or pointer compare.

Per-call baseline (card329 "ProcessEvent hook" rows, mod time without the original): b460 client FED_C1M1 0.204 and
0.213 us per call, 206-224 calls and 42-48 us per frame. The host FED_C1M1 row does not print in a gate run (the map
does not change before quit). The b461 run adds UBWCOOP_C571=1 for the card 572 split rows on both sides.

Slate census (`c573_slate_row`, frame_c330.cpp, under UBWCOOP_C132_FX=1, every 30 s window): user widgets by class
(all, shown, shown with a Blueprint Tick), and WrapBox slots by the class of the user widget that holds them. This is
counts only, with no cut. c601b455a points at a WrapBox: TPanelChildren<SWrapBox::FSlot>::GetChildRefAt 38 and
Prepass_ChildLoop 66 samples.

Predictions (c604b461a, C4M1):
1. Client per-call cost at or under b460 (0.21 us), with no rise in calls per frame.
2. The census names one holder class with most WrapBox slots, and a large share of the user widgets are hidden but
   still alive. A shown widget with a Blueprint Tick is a cut candidate for b462.
3. 0 mismatches; census scan under 5 ms per window.

### b461 results (c604b461a, C4M1, 508 s, rc 0 both sides, 0 mismatches)

1. Not a like-for-like compare yet. Client FED_C4M1: 0.328 us per call, 166.8 calls and 54.7 us per frame. The b460
   baseline is client FED_C1M1 (0.204-0.213 us), a different map. The c448b461a gate (client FED_C1M1) gives the same-map
   number. Host card572 split rows, per 30 s window: body avg 260-307 ns (before 199-236, after 61-73), 285k-369k calls.
2. Partly met. Host user widgets 700-792 per window, about 568-657 shown, 87-119 shown with a Blueprint Tick. Top
   classes (all and shown nearly equal, so few are hidden): WB_UI_Control_Input_Box_C 61, WB_DebugButton_C 53,
   WB_Boilerplate_InWorldComputer_LineItem_ToggleSwitch_C 30, ..._LineItem_Slider_C 27,
   WB_Boilerplate_FloatingButton_CenteredVersion_Wide_C 26, WB_Glyph_C 25, WB_Weapon_Switch_C 24,
   ..._InternalButton_C 23, WB_TypewriterText_C 20, WB_InputDeviceVisibilityPanel_C 18, WB_Objective_Marker_C 14,
   W_AloftSafeZone_C 13. UMG WrapBox count is 0, so the SWrapBox in c601b455a is native Slate (not a UMG WrapBox); the
   holder question stays open. The shown-with-Tick class list is cut off by the log line cap; the b462 census prints it
   as its own row. WB_DebugButton_C (53, shown) and the in-world computer line items look like the first cut candidates.
3. 0 mismatches. The scan takes 11.9-18 ms per window, over the 5 ms bar. It runs only under UBWCOOP_C132_FX=1, once
   per 30 s, so it is a test cost; b462 can split it across frames if it stays.

Same-map compare (c448b461a gate, client FED_C1M1, 220 s): 0.188 us per call, 220.3 calls and 41.4 us per frame,
against b460 0.204-0.213 us, 206-224 calls and 42-48 us. Per-call cost down 8-12 % at the same call rate. Prediction 1
met.

## b463 PE branch trace: detour_process_event only (2026-11-22, static)

Scope per the senior: c303_watch dropped (a test-only watchdog thread, not on the GameThread). c161_pe already has its
early-out.

**Call rate.** Card 571 counts in c604b461a (10 s rows):
- Host: UObject_ProcessEvent 230k to 369k per 10 s, which is 23k to 37k/s.
- Client: about 20k to 30k/s.
- Card 571 counts per hook, not per PE branch. The ranking below uses those rates times each branch's on-rate (how
  often its gate is open) and its per-call work.

**Branch table.** Unsampled path, play build, in call order. "Gate on" means when the flag is true.

| Branch | Gate on | Per-call work when on | Rank |
|---|---|---|---|
| detour self: `++g_c572_pe_tick`, `C128PeScope` (2 TLS loads, 4 TLS stores) | always | counter RMW on a global that several threads write, plus about 13 gate loads | **1** |
| `c600_exit_pe` (objectives.cpp:4460) | host, always (no flag) | FName read of `function`, 2 cached compares | **2** |
| `c491_pe` (score_c475.cpp:919) | client, always (no flag) | FName read, 2 cached compares; strcmp class only on a match | **2** |
| `c408_pe` / `c408_after_pe` (satchel_c408.cpp:447 / 481) | a flak or plasma building on the map | 1 ptr compare, 1 FName read and compare, 2 ptr compares; after: 2 ptr compares (host) | 3 |
| `c158_after_pe` (grenade_c158.cpp:138) | client, once a grenade ability is bound (the whole map) | 2 loops over up to 4 entries: up to 8 ptr compares | 4 |
| `c44_after_pe` (death.cpp:2045) | CompleteDeath until the death screen builds | FName read and compare | 5 |
| `c345b_pe` (objectives.cpp:2097) | level end only | 1 ptr compare | 6 |
| `c376_pe` (restart.cpp:373) | client, pause menu open | class ptr compare against a 1-slot skip cache; on a miss `class_name` strcmp | 7 (pause only) |
| `c30_note_event`, `c32_note_pe`, `c74_note_pe`, `c582_building_pe`, `c67_note_pe`, `c493_after_pe` | test or diag flags only (c30 armed, `UBWCOOP` spawn-mech mode, C74/C99 logs, CMD, DIAG) | GetCurrentThreadId plus a table walk (c30, c32) | off in play |

**Finding: the gates sit on about 13 cache lines.** From the b462 .map, every PE loads gate bools in separate 64-byte
lines:
- 0x3ce0c8: `g_c572_pe_tick`, which is written.
- 0x3ccd59: `g_c30_window`, sharing a line with `g_orig_process_event`.
- 0x3b9840: `g_c32_pe`.
- 0x2c9a666 to 0x2c9a678: `g_c67_pe`, `g_c74_pe` and `g_c44_pe` (one line).
- 0x1fd6c60: `g_c99_log`.
- 0xf5b4e0: `g_c161_pe`.
- 0x3dd2b8: `g_c376_pe`.
- 0x6a1150: `g_c345b_pe`.
- 0xf63e80: `g_c408_pe`.
- 0x3411c2: `g_client_session`.
- 0xf69300: `g_c158_pe`.
- 0x338218: `g_c493_pe`.

ProcessEvent runs on several threads. The non-atomic `++g_c572_pe_tick` bounces its line between cores. This matches
b461's main-thread samples: detour_process_event self has 14 samples, more than any sub-hook (c161_pe 5,
c408_after_pe 4).

**Proposed early-outs.** All are lossless: the same calls run, in the same order, with the same results.
1. **One gate word.** Move every PE gate into one `uint32_t g_pe_gates` bitmask, with one bit per sub-hook and a
   host/client side bit. Setters become `pe_gate_set(bit, on)`. Each call then does one load; with `g_pe_gates` equal
   to the side bit alone, it skips straight to the always-on pair. The 13 lines become 1.
2. **Shared FName dispatch for c600, c491, c408 BeginPlay and c44.** Read `read_u64(function, kObjectName)` once.
   Compare it against the cached FName ids (Exit, PlayerExit, FedScoreGained, ReceiveBeginPlay, Construct) held in one
   line. Call a sub-hook only on its own match. This removes 3 repeated FName reads and the per-hook `fname_cached`
   statics spread over several lines.
3. **Thread-local sample counter.** Make `g_c572_pe_tick` `thread_local`, as `g_c128_pe_object` already is in
   section 0006. This ends the cross-core line bounce. The 1-in-16 sampling stays.
4. **c158: one 8-entry array.** Hold the started and do_action pointers in one flat array, scanned once (up to 8
   compares, early out on `n == 0`), instead of two loops.
5. **c376 (pause only, low):** compare the function name before `class_name` strcmp, or cache the WB_Pause_Menu_C
   class pointer once found. Optional.

**Prediction for a b463 with steps 1 to 4.** The detour body's per-call cost (c330_pe row) drops from 0.188 us on the
client, C1M1, by at least 25 %. b461 had 220 PE calls a frame, 41.4 us/frame; the prediction is under 31 us/frame.
Card 571 PE counts are unchanged within ±10 % (same calls). All card behavior rows are unchanged.

**Proof:** one run, the C1M1 c448 scenario with both logs. It compares c330 PE us/frame and the main-thread mod
samples against b461 and b462.

### b463 results (c448b463b, C1M1, 240 s, rc 0 both sides, scenario passed)

Build: dwmapi 24017bbe2e00b7ca, ubwperf 76ff5e38866e61bb (build-b5 copies with .map). Steps 1 to 4 only.
c448b463a is void: it was launched without LANE=2 and SCENARIO, so it ran the lane 1 play build (b462) at the menu
until the cap (lesson 97). The lane 1 dwmapi hash was unchanged afterwards; nothing else that a game run writes
in lane 1 was checked.

Card 329 ProcessEvent rows (mod time, the original excluded):

| Run | Side, map | us/frame | calls/frame | us/call |
|---|---|---|---|---|
| b461 | client FED_C1M1 | 40.4 / 41.4 | 209.2 / 220.3 | 0.193 / 0.188 |
| b462 | client FED_C1M1 | 41.0 / 43.2 | 211.4 / 230.8 | 0.194 / 0.187 |
| b463 | client FED_C1M1 | 42.6 / 47.3 | 214.1 / 231.9 | 0.199 / 0.204 |
| b462 | client 3D_Frontend (join) | 6.5 | 35.1 | 0.185 |
| b463 | client 3D_Frontend (join) | 4.3 | 35.0 | 0.123 |
| b462 | host 3D_Frontend | 8.6 | 19.9 | 0.434 |
| b463 | host 3D_Frontend | 5.9 | 20.2 | 0.291 |

- **Prediction 1 (under 31 us/frame, client C1M1): missed.** C1M1 is unchanged to slightly worse (0.199 to 0.204 us
  per call against 0.187 to 0.194). Two frontend rows dropped by 33 %; their cause is not known (see the correction below).
- **Prediction 2 (PE counts within ±10 %): met** on the card 329 call counts (214.1 / 231.9 against 211.4 / 230.8
  per frame, within 1.5 %). The card 571 rows were not in this run (UBWCOOP_C571 unset in both b462 and b463).
- **Prediction 3 (behavior rows unchanged): met as far as checked**, which is these rows and no others: scenario
  passed, 0 not proved. The WARN classes match b462
  exactly (host 5, client 7, same rows).

The host printed no FED_C1M1 card 329 row in b462 or b463 (the map does not end before quit), so host C1M1 cost was
not measured.

**Correction (2026-11-22).** The cause first written here (c161_pe and c158_after_pe live for the whole map and being
the remaining cost, with gate loads ruled out) was wrong and was never measured. c161_pe already has its early-out,
c158 bound about 60 s into the first window, and b463 made the gate loads more expensive. See the proposal below.

### b464 proposal (static, 2026-11-22; not built, not run; senior verdict below: step A approved)

Evidence: llvm-objdump of build-b5 b462 and b463, the source, and existing logs.

**Finding: b463 turned each gate test into a function call.** `std::atomic<uint32_t>::load` was not inlined at /O2
/Ob2. It is 0x18000d970 (`mov; test; je; ret`), and each site adds `lea; xor; call`. b462's detour had 0 such calls
(byte compares on globals). b463 has 3 per call in the detour, 7 in the early group and 6 in the after group (the
first of the 6 runs even when that group is closed).

| State | Load calls per ProcessEvent |
|---|---|
| both groups closed | 4 |
| early open (a door is tracked) | 11 |
| after open | 9 |
| both open (client C1M1 in c448b463b) | 16 |

This fits a C1M1 rise. It is not proved to be all of it, and it does not explain the frontend drop.

**Which gates were open in c448b463b, client C1M1.**

| Gate | Why open | In play |
|---|---|---|
| c161 | 2 doors tracked | yes |
| c158 | from t=223679380, about 60 s into the first window, until process exit (nothing clears it) | yes, after bind |
| c67 | UBWCOOP_CMD=1 and the classes resolved (logged) | no, CMD is unset |
| c493 | UBWCOOP_DIAG=1 on map arm (by code, no row logs it) | no, DIAG is unset |

So the after group was open through the test for test-only reasons, whether or not c158 was bound.

**Static per-call miss path of each hook** (instructions inside the hook, from the b463 disassembly).

| Hook | Path | Instructions |
|---|---|---|
| c161_pe | frame and stack cookie 9; one mask load and bit test 8; exit 13. No table walk on a bit miss | 30 |
| c158_after_pe | frame, 2 xmm saves, cookie 14; setup 7; 2 compares 13; exit 17 | 51 |
| c67_note_pe (test only) | frame, class read, 5 compares, exit | about 46 |
| c493_after_pe (test only) | frame, 6 xmm saves, name read, fname_cached call, exit | about 70 |

With 2 doors at most 2 of the 64 mask bits are set, so about 97 % of calls take the c161 miss path if objects hash
evenly (not measured).

**Step A (recommended, alone): make the gate read one instruction again.**
- `PeHot::gates` becomes `uint32_t`. `pe_gates()` is `__atomic_load_n(&g_pe.gates, __ATOMIC_RELAXED)`, and pe_gate_set
  uses `__atomic_fetch_or` and `__atomic_fetch_and`, relaxed. The five users are death.cpp:349, death.cpp:379 and
  hooks_internal.h:91, 95, 97.
- Behavior is unchanged: same word, same memory order, atomic writes, and every reload stays in place, so a gate
  set by one hook is still seen by the next test.
- Check before any run: the b464 disassembly of the detour, early and after groups has 0 calls for gate reads.

**Step B (optional, small): frameless miss path for the two hooks.** The early-out is a split, not a new test.
- c161_pe keeps only the mask test and tail-calls a noinline hit body. A miss already returns false with no side effect.
- c158_after_pe keeps only the 2n compares and tail-calls a noinline hit body with the index. A miss already has no
  side effect. Order, compares and hit code are unchanged.

**Predictions, client C1M1, same scenario and env. The cycle ranges are assumptions, not measurements.**
- A: each site loses 6 instructions and a call/ret pair, assumed 3 to 8 cycles. 16 sites = 48 to 128 cycles = 9 to
  28 ns at 5.5 to 4.5 GHz. From 0.199 / 0.204 that is **0.171 to 0.195 us/call**, or 2 to 6 us/frame less at 214 to
  232 calls.
- B on top of A: about 20 plus 32 instructions go, at IPC 3 to 1 = 17 to 52 cycles = 3 to 12 ns, giving **0.159 to
  0.192 us/call**. Before c158 binds only the c161 part applies.
- Two windows of one run differ by up to 0.007, so one run can show A but cannot confirm B.
- A is falsified if both windows stay at or above 0.196 with 0 load calls in the disassembly.

**Is a per-hook cost row needed?** Not for A: the disassembly and the card 329 row test it. For B a per-hook rdtsc row
would not work: the timer costs about as much as a hook miss (2 to 11 ns). B is not worth a run by itself.

**What these cuts do not explain.** A and B together are at most 40 of about 200 ns. In the existing b461 C4M1 split
(c604b461a client, C571=1, four 30 s rows) sampled bodies over 4000 ticks were 3 to 11 % of samples and 23 to 40 % of
the mean (slow count x slow average / samples / body average). C1M1 has no split row. To split the rest, run A once
with UBWCOOP_C571=1 after its like-for-like run (no code: before, after and tail per 30 s), and add a stage row only
if that points at the body and not the tail. No run here represents play: the scenario needs CMD, so c67 stays open.

### b464 senior verdict (2026-11-22)

Checked by the senior: 19 gate-load call sites in b463 (detour 6 over its two copies, early 7, after 6, so 16 per call
with both groups open), and a test compile with the mod's flags where `__atomic_load_n` is one `mov` and
`std::atomic::load` is a call. Which gates were open in c448b463b was not rechecked.

- **Step A approved alone. Step B declined:** its predicted 3 to 12 ns is too close to the 7 ns window spread.
- **Build in build-b5, not build/.** tools/playtest.sh copies build/dwmapi.dll over lane 1 at each launch and does not
  copy ubwperf.dll, so a candidate in build/ would reach the play build untested and unpaired. Both directories have
  the same flags. On promotion copy the kept pair to lane 1 and to build/, then compare all four hashes.
- **Before a run:** 0 gate-load calls in the detour, early and after groups, and every gate test still present.
- **Run 1 (like for like, C571 unset):** behavior as b463, and both client C1M1 windows at or below 0.195 us/call.
  One window only is inconclusive; both at or above 0.196 falsifies the prediction. No repeat run for the number.
- **Run 2 (UBWCOOP_C571=1, only after run 1 passes on behavior):** the before, after and tail split, host C1M1
  included, plus card 571 counts. C571 adds stubs and the timed path, so its us/call is never compared with run 1.
- **Promotion:** lane 1 takes b464 if behavior passes, whatever the number. Until then lane 1 stays b463.
- **Step C (not in b464, rank by the run 2 counts):** the same non-inlined load is in other hot detours, most older
  than b463: dwmapi detour_c534 (13 copies) and detour_c545_*; ubwperf detour_c536_find, c577_rebuild, c531_save_id,
  c538_conditions, c548_*, c573_bones, c573_niagara, c593_resolve and perf_frame.

### b464 build and pre-run check (2026-11-22)

Build in build-b5: dwmapi 248cc58e865316f7, ubwperf c65c01272dc9681a (kept copies with .map). The ubwperf instruction
stream is identical to b463 (2 bytes of the file differ). 85 exports as b463, KERNEL32 only.

Disassembly of b464: 0 gate-load calls (b463: detour 6, early 7, after 6). Each test is a `mov` of the word and a
test of its bit: detour 3 per copy, early bits 0x1 to 0x40 (7), after 0xf80 then 0x80 to 0x800 (6). All 43 writes of
the word in the DLL are `lock or` or `lock and`.

Run 1 predictions are in the verdict above. Run 2 predictions (C571=1, written before either run):
- the same behavior result as run 1;
- card 572 split rows print on host and client during FED_C1M1;
- on client C1M1 "before" is larger than "after" in every 30 s row (as in b461 C4M1);
- bodies over 4000 ticks are at least 15 % of the mean. Under that, the cost is in the body and a stage row is next.

### b464 run 1 result (2026-11-22, c448b464a, C571 unset)

Behavior passes: rc 0 on both, scenario passed, 0 not proved, 179 steps as c448b463b, no crash dump, mismatch rows 0,
and the same WARN classes (host 4, client 6; one card 537 periodic row fewer each in the shorter run, 196 against
240 s). One step result differs: optional line 55 (client bark) passed where the baseline timed out after 30 s. It
timed out in 6 of the 14 c448b40-46 drive logs.

| client FED_C1M1 window | b463 us/frame | b464 us/frame | b463 us/call | b464 us/call | rule |
|---|---|---|---|---|---|
| 1 | 42.6 | 33.9 | 0.199 | 0.162 | at or below 0.195: met |
| 2 | 47.3 | 33.5 | 0.204 | 0.150 | at or below 0.195: met |

The rule is met, but the size of the drop is not all shown to be the cut:
- both windows are below the predicted floor of 0.171, so that range was wrong or the runs differ in more than the cut;
- client ext split, code b464 did not change, also fell (median 7.4 to 4.9 us per tick, b462 5.7). The client ext pairs
  walk, digest build and Niagara timings moved 10 % or less, and the host ext split 13.0 to 12.7;
- FED_C1M1 loaded 9 s faster;
- ps-recomp test jobs wrote files during c448b463b (05:15:35 to 05:16:47 at least). Their CPU use was not logged. The
  same project's job held CPUs 7,15 before this run, and the launch waited for it to exit.

By the verdict there is no repeat for the number. The host printed no FED_C1M1 card 329 row, as in b462 and b463.

### b464 run 2 result (c448b464b, UBWCOOP_C571=1): FAILED, not promoted

The client crashed 109 s into the run, on `client disconnect`. The cause is card 608: the card 604 summary reads a
stale asset pointer. That code is not changed by b464 and is in b463. No rerun was made. Lane 1 and build/ stay b463
(24017bbe2e00b7ca / 76ff5e38866e61bb); game2 holds b464 (248cc58e865316f7 / c65c01272dc9681a).
Mismatches were 0 up to the crash.
Senior 2026-11-22: b464 alone is not promoted. Step A ships in b465 with the card 608 fix, on the one run in card 608.
Step C and the PE body stage row come after b465.

Card 572, the one row of each side in FED_C1M1:

| side | t | calls | body avg ns | max ns | sampled | before ns | after ns | slow | slow avg ns | tail |
|---|---|---|---|---|---|---|---|---|---|---|
| host | 236698598 | 340208 | 146 | 6140 | 21263 | 103 | 43 | 130 | 1690 | 7.1 % |
| client | 236701505 | 400256 | 150 | 5020 | 25014 | 106 | 44 | 122 | 1830 | 6.0 % |

Predictions: same behavior as run 1, false (crash); split rows on both sides, true; client before above after, true
(one row); tail at least 15 %, false. By the written rule the cost is in the body and a stage row is next.

Step C ranking, host calls per 10 s at t=236698598:

1. c531_save_id (GetSaveId) 3390878
2. c538_conditions (DoesTargetMeetConditions) 1373525
3. c536_find (FindComponentByClass) 1138331 (client 66834)
4. c593_resolve (GetNavDataForPropsConst) 23856
5. c573_niagara 3420, c573_bones 1198, c577_rebuild 185, c593_possess 0

c534 and c545 are in none of dwmapi's top 20 for that window, so each hook is below 3025 per 10 s (9073 per 30 s).
The c548 hooks are not among ubwperf's 15 counted rows, so they have no count from this run.

### Step C and PE stage row proposal (2026-11-22, static; no code, build or run)

Evidence: llvm-objdump of ubwperf b465 (a59345153c6e5358), the source, and the run 2 counts above.

**Out-of-line atomic loads on the hit path of the top three.**

| Hook | Variable | Loads per call | Host calls per 10 s | Loads per 10 s |
|---|---|---|---|---|
| c531_save_id | g_c531_class, then c536_find's two by its direct call | 3 | 3390878 | 10172634 |
| c536_find, external calls | g_c537_gen and g_c537_all | 2 | 1138331 | 2276662 |
| c538_conditions | g_c538_frame | 1 | 1373525 | 1373525 |
| total | | | | 13822821 |

The direct call from c531 is not in c536_find's count, so c536_find itself runs up to 4.5 M times per 10 s. The c531
row is an upper bound (null and garbage actors leave before the loads). A c536 miss adds 2 to 4 loads (not counted).

**Change (these three only).**
- The four variables become plain words read and written with `__atomic_*`, as in step A, with each order kept:
  relaxed for class and frame, acquire for the two generation loads, acq_rel for their increments.
- No other line changes. c593 and below total under 30000 calls per 10 s and are not worth a change.

**Expected gain.** With step A's assumed 3 to 8 cycles per site at 5.5 to 4.5 GHz: 7.5 to 25 ms per 10 s, which is
0.08 to 0.25 % of one core, summed over all threads that call these hooks. The GameThread share is not known. This is
an assumption from instruction counts, and it is too small for one run to show.

**Proof.** Before a run, 0 load calls on the three hit paths and the same order at every site. In the run, behavior as
c448b465a and 0 new card 536, 538 and 540 mismatch or differ counts. No timing claim.

**PE body stage row (card 572, UBWCOOP_C571 only).** "before" is 103/106 ns of the 146/150 ns body. It has four stages:

1. the thread-local scope (two TLS reads, two writes);
2. the early group (gate test, then up to 7 hooks);
3. the names check and the FName read;
4. the c408 gate and the role hook (c600 on the host, c491 on the client).

- A sampled call takes one extra rdtsc at one boundary, in rotation over four points: directly after t0 (the timer's
  own cost) and after stages 1, 2 and 3. Stage 4 is "before" minus the third boundary. At 21263 samples per 30 s that
  is about 5300 per point.
- That call's "before" holds one timer read. The row prints the timer cost and subtracts it, so one call never holds
  four reads. The untimed path (15 of 16 calls) and the C571-off path keep the same instructions; check both in the
  disassembly.
- Predictions: the stages sum to "before" within 10 ns. The early group is the largest on the client (c161's gate is
  open in C1M1). Rule: a stage at 40 % or more of "before" gets a static trace next. If none is, the cost is flat and
  ProcessEvent cutting stops.

**Batch.** Not in the card 607 build (senior 2026-11-22). Waits for a verdict, then one c448 run with UBWCOOP_C571=1.

### Senior verdict on step C and the stage row (2026-11-22)

- **Stage row: dropped, and ProcessEvent cutting stops here.** The run 2 rows give the whole body 49.7 ms per 30 s on the host
  and 59.9 on the client: 0.17 and 0.20 % of one core, about 0.03 ms per frame at 60 fps. "before" is about 0.12 and 0.14 %.
  A stage at the 40 % rule would be about 0.05 %, less than a build and run can show.
- **Step C: accepted as proposed, with no build or run of its own.** It goes into the next build that already needs ubwperf,
  after the card 607 batch. Proof is the disassembly (0 load calls on the three hit paths, orders kept) plus that build's
  behavior result and 0 new mismatch or differ counts. No timing claim.
- Later cuts are ranked by measured GameThread ms per frame, not by ns per call.
