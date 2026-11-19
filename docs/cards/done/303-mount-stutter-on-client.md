# 303 Mounting a turret or a mech stutters for about a second on the client

Playtest 2026-11-13 (user plays as client, FED_C2M1): "mounting turret, or mech stutters for a second on client".

## Trace (client log `UBWCoop-menu-20261113-184603.log`)

| Line | Client t (ms) | Row |
|---|---|---|
| 61994-61997 | 257271578 | interact press; `MechEnter before` (mech roles 1/3, controller null); `MechEnter ... sent` |
| to 62104 | 257271679 | last row before the gap (`card018: client spec bind ... AD_MechPlayerAbilityDefinition_C`, the minimap origin already on the mech at 257271678) |
| 62105 | 257272416 | first row after a 737 ms gap with no row of any kind: `card098: client hide BP_PlayerCharacterMI_C` |
| 62142-62170 | 257272486 to 257272493 | attribute re-inits (cards 018, 019, 020), `IMC_Mech_*` contexts, weapon equip, `MechEnter after` (roles 2/3) |
| 63608 | 257319245 | mech exit: `card098: client show BP_PlayerCharacterMI_C` is the first row after an 804 ms gap |

Press to finished possession: 915 ms. The client's ping is 56 to 83 ms (rtt row at 257640657), so the round trip
explains at most a tenth of it. In the 737 ms no Pose, audio or netbudget row is logged, although Pose rows from
the host arrive every few ms before and after. The exit shows the same shape (804 ms).

This log has no turret mount (`TurretMount` ledger 0 sent in the map so far); the turret part of the report
comes from the user, same symptom.

## Cause (senior trace 2026-11-13, both logs; located, blocker not yet named)

Settled from the logs:

| Fact | Evidence |
|---|---|
| The host is not the delay | Host receives `MechEnter` 106 ms after the client's press and possesses in 7 to 9 ms (host t=83402448 to 83402457; second enter 83459742 to 83459744). Turret: `TurretMount` handled at 84234933, in the frame it arrives. The host's own enter takes 28 ms (83476200 to 83476228). |
| The client stops processing its controller channel for 0.74 to 0.97 s | Six of six transitions: mech enter 737 and 760 ms, mech exit 804 and about 850 ms, turret mount 971 ms, turret leave 950 ms. Each gap starts right after the `card058` minimap origin change (the replicated pawn change has arrived) and holds no log row of any kind. |
| Host frames pile up and are applied in one burst | Second enter: frames 9450 to 9571, sent by the host from 83459802 to about 83460560, are all applied between 257329741 and 257329786; lateness falls from 703 ms to 87 ms across the burst. Turret mount: the turret row sent at 84234976 is applied at 258105090, 880 ms late. |
| Not a first-use asset load | The second enter (10 s after the first exit) and both exits show the same gap. |
| Not the mod's possession handlers | The cards 017 to 020 rows run after the gap and span 3 to 77 ms. No mod receive gate applies mid-map (`c139_client_hold` is host side and only before the first Pong; `c120`/`c138` are per map load). |

Not settled: what blocks. Two candidates remain and the logs cannot tell them apart, because the client has no
per-frame row (log gaps of 200 to 800 ms are common in quiet play, so silence alone proves nothing):

1. A game-thread stall inside the client's possession change (the game's own client path or an engine call in
   it: a synchronous load, a flush of async loading, a garbage collection). The `at tick` counters of the
   `pawn-collision` rows fit this weakly: 116, 106 and 105 ticks per second across the three intervals, which
   becomes 118, 114 and 111 when 0.8 s is taken out of each.
2. The game thread runs, but the controller's actor channel queues its bunches for that time (the engine does
   this while a bunch waits for an object it cannot resolve yet); the mod's frames ride that channel, so they
   wait with it.

The possession rows (`possess: ClientRestart ...`) that would show the order are capped at 16 per session and
were used up at 256814333, before the first mech.

## Step 1 (diagnostic in the next build; no fix is coded before its rows are read)

1. Frame watchdog on the client and the host: a second thread notes the game thread's tick stamp; when a frame
   exceeds 200 ms it samples the game thread's stack once (module-relative addresses, 16 frames) and the game
   thread logs `card303: <side> t=<ms> frame of <ms> ms, stack exe+0x.. < ..` when it returns. Always on, one
   row per long frame, capped per map with a tally.
2. The possession rows (`OnRep_Pawn`, `ClientRestart`, `AcknowledgePossession`, `SetPawn`) lose the session
   cap for mech and turret transitions: one row each per transition, with the tick number.
3. The mount row of the Fix section below, with the tick count between press and possession, so a stall
   (few ticks) and a queued channel (many ticks) read differently.

Prediction for the one run: each mount and leave shows either one `card303` frame of 700 to 950 ms whose stack
names the blocking call (candidate 1), or no long frame and 60 or more ticks between `OnRep_Pawn` and
`ClientRestart` (candidate 2). The fix is written from that row.

## Fix

By the answer of step 1:

- Client stall: the assets the mount needs are loaded before the press can happen (async, when the mech or
  turret becomes relevant to the client, the way the game preloads for its own player), and no audit, digest
  round or full-list apply runs inside the possession frame. Diagnostic passes move off that frame or behind
  their test flag.
- Host delay: the host possesses in the frame the row arrives; anything the game delays for presentation plays
  on the client without holding the possession.

The client keeps control of its trooper until the possession lands; no input gap. The same for dismount.

- Rows: `card303: client t=<ms> mount <class>: press to possession <ms> (host <ms>, wire <ms>, client <ms>),
  longest frame <ms>`, the same for dismount, `WARN card303: client mount <class> frame of <ms> ms` when a
  frame in the window exceeds 50 ms.

## Sibling sweep (same class: a possession or view change on the client)

| Item | Check |
|---|---|
| Mech enter and exit | no frame over 50 ms |
| Turret mount and leave (every turret class) | same |
| Dropship ride start and end, bug form (card 208) | same |
| Downed, revive, respawn (card 295 jitter after revive: same handlers of cards 017 to 020 run) | same; compare causes with card 295 |
| Weapon pickup and swap (held list apply) | same |
| First use against later uses on a map (asset already loaded) | both measured |
| Host doing the same actions | host frame times unchanged |

## Predictions (one run, after the playtest, batched with 297 and 298)

- Client mounts and leaves the mech twice and a turret twice: eight `card303` rows, press to possession under
  150 ms at 64 ms ping, longest frame under 50 ms, zero `WARN card303`.
- Digest round after: 0 mismatches.

## Build 7 (diagnostic coded, compiled; diff `b7-C-303.diff`, wiring in `b7-shared.diff`)

- `stall_c303.cpp` (new): the frame watchdog thread. Frames over 200 ms log `card303: <side> t=.. frame of N ms
  (tick T), stack exe+0x.. < ..` (RIP, then up to 15 exe addresses from a raw stack scan), 64 rows per map, map-end
  tally.
- `c303_press` at the client's MechEnter / MechLeave / TurretMount / TurretLeave send (`vehicles.cpp c75_send`);
  `c303_possession` from `log_possession` and `detour_pc_set_pawn` (uncapped, with the tick number).
- Mount row: `card303: client ... <what> <pawn class>: press to possession N ms, T ticks, longest frame N ms`; a
  `WARN card303` when the longest frame is over 50 ms or no possession comes within 5 s.
- Not split on the client: host time versus wire time. The host's card075/123 row gives the host side.

## Step 1 rows read (senior, 2026-11-17 08:23): candidate 1 holds; the raw stack scan cannot name the blocker

Client rows in `c447b173a` (same shape in `c447b159a`, `c447b164a`):

| Transition | Possession row | Long frame row |
|---|---|---|
| MechLeave | 561974550 (tick 2529), press to possession 48 ms | `frame of 1179 ms (tick 2529)` |
| TurretMount | SetPawn 561978727, `OnRep_Pawn` 561979811, both tick 2712 | `frame of 1135 ms (tick 2712)` |
| TurretLeave | 561983513 (tick 2934), press to possession 32 ms | `frame of 1222 ms (tick 2934)` |

- It is a game-thread stall of 1.1 to 1.2 s inside the tick of the possession change (candidate 1). On the turret
  mount it sits between the `SetPawn` inside `ClientRestart` and `OnRep_Pawn`. The mount row reads `longest frame
  17 ms` only because it closes at the possession, before the stalled frame ends.
- The host has no such frame on its own mounts (host long frames in that run: tick 2 and tick 619 only).
- Not a first-use load: every transition, in three builds.
- The stack row does not name the call: RIP reads `exe+0x0` (outside the exe: a system DLL, the driver layer or
  the mod's own DLL), and the 15 other addresses are a raw scan that picks up stale return addresses (resolved:
  Slate attribute and UMG `SynchronizeProperties` residue in one row, script VM residue in the other).
- Knock-on: the host pacer holds every send for that second (card 474), so each mount also freezes the world on
  the client for about 1 s.

## Step 2 (diagnostic, patch written by the senior: scratchpad `c303_patch.py`, syntax-checked on a copy)

`stall_c303.cpp`: the watchdog copies the game thread's context and up to 256 KB of its stack while it is suspended,
resumes it, and runs a real unwind (`RtlLookupFunctionEntry` + `RtlVirtualUnwind`) on the copy; 3 samples per long
frame, at 200, 500 and 800 ms into the frame, 32 frames each. Rows:
`card303: <side> t=.. frame of N ms (tick T), K stack sample(s)` then
`card303: <side> t=.. sample K at +A ms, frame I on: <module>+0x.. < ..` (8 frames per row; `exe+`, `ubwcoop+`
or the module's export name). `crash_trace.cpp` exports its `describe` as `c118_describe_pc`.

Prediction for one c447 run: the three samples of each mount frame share their outer frames (one blocking call, not
three unrelated ones); the innermost `exe+` or `ubwcoop+` frame names the call. The fix is written from that row.
A sample that ends after 1 or 2 frames (unwind lost) is a diagnostic defect to fix before any other step.

## Step 2a: static narrowing (2026-11-17 08:26)

Row order in tick 2712 of `logs/c447b173a-client.log` (TurretMount), read against `mod/src/ubw/input_diag.cpp`
(each detour logs after its original returns):

| t (ms) | Row | Meaning |
|---|---|---|
| 561978726 | `possession SetPawn`, `card058`, `possession AcknowledgePossession` | first SetPawn + acknowledge, no delay |
| 561978727 | `possession ClientRestart` | the game's `ClientRestart` has already returned: not the blocker |
| 561978727 | second `possession SetPawn` + `card058` (same pawn, origin unchanged) | the `SetPawn(GetPawn())` inside `AController::OnRep_Pawn` |
| 561979811 | `possession OnRep_Pawn` | logged after `g_orig_on_rep_pawn` returns, 1084 ms later |

So the stall is inside the game's `OnRep_Pawn` chain (the `ASTPlayerController` override) after its inner
`SetPawn`, not in `ClientRestart` and not in the mod's `SetPawn` work (the last mod step there, the capped
`client-input` row, does no blocking call). The step 2 samples at +200/+500/+800 ms should all show
`OnRep_Pawn` in the stack; the frames above it name the call. If a mod detour sits between `OnRep_Pawn` and
the top frame, the mod causes it; if not, it is game code that the host never runs on this path (the host
possesses through `OnPossess`), and the fix is to see what `OnRep_Pawn` loads or waits for.

Disassembly of `AController::OnRep_Pawn` (exe+0x3B35000, the hooked function, read-only `objdump`): old pawn
`SetController(null)`, virtual `SetPawn` (vtable +0x7C0, the second `SetPawn` row, returns at once), then one
last call at exe+0x3B3511C: `TMulticastScriptDelegate::ProcessMulticastDelegate` = the
`OnPossessedPawnChanged` broadcast. Nothing else runs between the second `SetPawn` row and the `OnRep_Pawn`
row, so the 1.1 s is spent in a Blueprint or widget handler bound to `OnPossessedPawnChanged` on the client.
This fits the Slate/UMG/script-VM residue of the raw stack scan. Prediction for the step 2 samples: frames
`ProcessMulticastDelegate` < `ProcessEvent` < script VM < a UMG or asset-load call. The host broadcasts the
same delegate from `Possess` with no long frame, so the handler's cost depends on client-only state (a
widget built again, or a synchronous load of something the host already holds).

## Step 2 result (b176 `ac991b60d632da66`, run c447b176a, 2026-11-17)

Every mount/leave frame on the client (MechEnter 1102 and 1113 ms, MechLeave 1164, TurretMount 1136, TurretLeave
1154) has 3 samples of 32 frames, all inside one call chain (innermost first):

`setupapi` < `GetSizeForDevID` (exe+0x15FCF40) < monitor enumeration (exe+0x15FB530) <
`FDisplayMetrics::RebuildDisplayMetrics` < `FSlateApplicationBase::GetDisplayMetrics` <
`USTSafeZoneScale::ApplySafeZone` < `USTSafeZoneScale::NativeConstruct` < `UWidget::TakeWidget` ... <
`UUserWidget::execAddToViewport` < script VM < `ProcessEvent` < `ProcessMulticastDelegate`.

So: each time a HUD widget holding a `USTSafeZoneScale` is constructed, the game rebuilds the display metrics,
and the per-monitor EDID read through setupapi takes about 1.1 s here (Proton). The possession broadcast adds
the mount/leave HUD on the client, which is why the user saw it at mounts. The mod's only frame is the
pass-through `detour_process_event`. It is not client-only: the host log of the same run has the same chain in
its own frames of 971, 973, 1008 and 1095 ms (my earlier "host unaffected" was only true at the client's mount
times), and the post-travel HUD frames (2030, 2273 ms on the client) are two of these calls in a row.
Each such frame also holds the host's paced sends (card 474) and delays every relayed row by a second.

## Step 3: fix (b177)

Cache `GetSizeForDevID` (`?GetSizeForDevID@@YA_NAEBVFString@@AEAH11@Z`,
`bool (const FString& TargetDevID, int32& Width, int32& Height)`). A device id names one monitor; its EDID
size does not change in a session, so the answer is cached for the session, success or failure.

- New detour, both roles, always on (not behind a diagnostic gate). Table of 8 entries
  `{wchar_t id[128]; int32 len; int32 w; int32 h; bool ok}` under one SRW lock; the original is called outside
  the lock. Key = the FString's characters (`data`, `num`), compared by length and `memcmp`.
- Pass straight through (no cache, one capped WARN row) when: `data` null or `num` under 1, id over 127
  characters, or the table is full.
- Hit: write `w`/`h` only when `ok`, return `ok`. Miss: call the original, time it, store, and log
  `card303: <side> display size for <id> read in N ms: WxH ok K, cached (entry I of 8)`.
- `kC303Frames` stays 32.

Prediction (run c447 on b177): one `display size ... read in` row per monitor per process, with N near 1000;
no client or host `frame of` row of 400 ms or more in the ticks of MechEnter, MechLeave, TurretMount,
TurretLeave; the post-travel HUD frames drop by about 1 s each time they held one of these calls. If a
mount frame stays long, its samples name the next call. Then gate c448: 0 mismatches.

Sibling sweep: the fix is at the one slow call, so every caller of `GetDisplayMetrics` gains (HUD build after
travel, revive, dropship, weapon HUD, menus). After the run, list every remaining `frame of` row of 400 ms or
more outside a map load with its top named frames.

Other long frames of c447b176a (host, not map loads), named from sample 1, both the game's own work and not
carded: 221 ms in `ASTSaveSystemEngine::Tick` > `Save_ProcessBatch` > `USTSaveComponent::Save` (checkpoint save
serialising), 354 ms in `ASTSpawnerBase::TickQueuedSpawnRequests` > `USTSpawnTable::LoadEntity` >
`FStreamableManager::RequestSyncLoad` (first spawn of an enemy class loads it synchronously).

## Step 3 result (b177 `e7197c28f8ab7bb8`, run c447b177a, 2026-11-17 08:38)

- The function takes four arguments (`Id, Width, Height, DPI`); the cache replays all three outputs.
- One read per process, at boot: host `display size for Default_Monitor read in 621 ms: 1920x1080 dpi 152 ok 1,
  cached (entry 1 of 8)`, client 591 ms. No pass-through WARN.
- Client mount/leave rows, longest frame in the window: MechEnter 17 ms and 9 ms, MechLeave 17 ms,
  TurretMount 18 ms, TurretLeave 18 ms (b176: 1102 to 1164 ms each).
- Remaining `frame of` rows are map loads only (client 2451, 961 (travel to `3D_Frontend`), 2836, 307 ms; host
  2869 ms) plus the host's 221 ms checkpoint save. The post-travel HUD frames of 2030 and 2273 ms are gone.
- No crash on either side.

## Closed

Closed 2026-11-17 on b177 `e7197c28f8ab7bb8`. Cause: the game rebuilds display metrics in every
`USTSafeZoneScale::NativeConstruct`, and `GetSizeForDevID` (setupapi EDID read) took about 1.1 s per call under
Proton, on both machines; the client's possession broadcast builds such a widget at every mount and leave.
Fix: session cache of `GetSizeForDevID` (`stall_c303.cpp`, 8 entries, both roles, always on). Proof: c447b177a
client mount/leave longest frame 9 to 18 ms (b176: 1102 to 1164 ms), host frames of 971 to 1095 ms gone,
post-travel HUD frames gone, only map-load frames remain; gate c448b177a 0 mismatches, 0 `WARN card313`, no
crash. Playtest-log check: a `WARN card303 ... display size not cached` row, or a mount that still hitches,
reopens. The frame sampler stays in (test builds only, `config::test_skip()`).
