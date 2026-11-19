# 264 Crosshair trace as the aim gate for test looks

User report 2026-11-13, watching the test windows: "The Host is looking at the sky in most of these tests."
Confirmed from probe 5 (`UBWCoop-host-20261113-064734.log`) and its shots. Blocks card 243 run 5 and every
later soak card: an aim step that passes while the view shows sky proves nothing.

## Cause (three faults in one class: the look target is a computed point nobody checked against the screen)

1. Presence steps look at the trigger volume's centre, which is above the pawn: cmd 74 pitch 58.4, cmd 78
   pitch 85.1 (`BP_PlayerPresence_C_52`), shot `host-c243-p5-p5h20.png` is ceiling. A presence volume is not
   an aim target.
2. Press steps add a guessed dz to the actor origin: cmd 90 `BP_Switch_C` at z 2214 from eye 2104, pitch 32.7;
   cmd 93 pitch 51.6; run 4 cmd 44 pitch 76.9 after the fall. The origin is not the mesh the player sees.
3. The `within=` gate (card 243) checks distance only. A wall between eye and target passes: cmd 37, nest at
   2521 uu, shot `host-c243-p5-p5h10.png` is a concrete wall.

## Change

`mod/src/ubw/testrun.cpp` (test driver only, never in a play path):

- After every `look` settles (snap and smooth), trace one line from the view point along the control
  rotation, 10000 uu, visibility channel, ignoring the own pawn. Use the engine's own trace through the
  existing reflection call path (`KismetSystemLibrary::LineTraceSingle` or the world's line trace by RVA; step 1
  names which is reachable). Reuse the `FHitResult` layout in `claim_c157.cpp` (`g_hit`), do not copy offsets.
- Log one row per look: `cmd: N aim: hit <actor name> (<class>) at X Y Z, D uu, pitch P` or
  `cmd: N aim: no hit within 10000 uu, pitch P` (sky).
- For E-use targets also log what the game itself offers: the interactable the pawn's interaction component
  focuses that tick (card 248 found the tick; name it from `kC248TickInteractable`), as
  `cmd: N aim: focus <actor name>` or `focus none`.

`tools/drive.py`: `look ... hit=<name prefix>` voids (also under `try`) unless the aim row's hit actor, or for
E targets the focus actor, starts with the prefix. `within=` stays.

Scenario generators (`gen_c243.py` first, then 241, 242, 244-247):

- Press, aim, throw and damage steps: `look actor <name> hit=<name>`; dz is found by the probe as the value
  where the trace hits the actor, not guessed.
- Presence steps: no look at the volume. `look <yaw> 0` along the walk direction, shot marked "presence".

## Proof

One probe on FED_C3M2 (no E, no objective change): every press and aim stand of card 243 logs an aim row
whose hit (or focus) is its target; the sky case and the wall case (cmd 37 stand) are shown to void. Shots
viewed against the rows: where the row names the target, the crosshair is on it.

Files: `mod/src/ubw/testrun.cpp`, `tools/drive.py`, `tools/symbols.txt` only if a new RVA is needed,
junior's generators. Do not touch any sync code. Budget: 1 build, 2 probes. Then card 243 run 5.

## Step 1 (junior, static, before code)

- Trace entry: the one engine trace the mod already calls, `trace_profile()` (`hooks_ubw.cpp:667`), which runs
  `g_line_trace_by_profile` (UWorld::LineTraceSingleByProfile by RVA) with `g_query_params_ctor` ignoring the own
  pawn. `testrun.cpp:554` (`c64_probe`) is a caller of it, not a second trace. It returns only the hit location:
  the FHitResult is a stack buffer inside `trace_profile`, and its actor field never leaves the function.
- `hit_layout()` / `g_hit` in `claim_c157.cpp:122-171` sit in that file's anonymous namespace; testrun.cpp cannot
  call them. `kHitSize`, `kHitFlags`, `kHitLocation` are local to `hooks_ubw.cpp`.
- Therefore the aim row's hit actor needs, outside this card's file list: `trace_profile` gains an optional
  out parameter (default null, every caller unchanged) that returns the hit actor read from the FHitResult's
  `HitObjectHandle` weak pointer at `hit_layout().actor` (claim_c157.cpp moves `hit_layout()` out of the anonymous
  namespace, one declaration in `hooks_internal.h`). Asked the senior for this grant; no code before the answer.
- Profile for the aim trace: `Visibility` is a channel, not a profile; the trace uses profile `BlockAll`
  (already cached the same way by gib_c137 / enemies), which stops on world static, pawns and bugs alike, so a
  wall in front of a nest names the wall.
- View: `c24_view_point(pawn)` (PlayerCameraManager view point, the crosshair line), 10000 uu.
- E focus: the game's own focus field, read, no new hook: `interact_target(read_ptr(pawn, kCharacterInteractor))`
  (`hooks_ubw.cpp:448`, `kInteractorTarget` +0x100), which UADInteractorComponent::TickComponent writes from its
  own trace each tick (card 081 detour). Card 248's `kC248TickInteractable` hooks the interactable side
  (TickInteractable(dt, interactor)), not the focus choice; the +0x100 field is the value the "E" prompt shows.
- Log point: after `c64_look` sets the view (snap) and after `c124_look_step` reports the smooth turn done, one
  `cmd: N aim:` row and, when the pawn has an interactor, one `cmd: N aim: focus <name>|none` row.
- drive.py: `hit=<prefix>` on `look`; waits for the aim row of that cmd and voids (also under try) unless the hit
  actor or the focus actor name starts with the prefix.

## Note: guid 0x0 on host rows (p2, senior accepted 2026-11-13)

- The aim rows and the clear rows print the same `c41_guid`. On the host it reads GetNetGUID from the remote
  client connection's package map, so an actor has a guid only after it has replicated to the client.
- p2 seq 111: the bug (BP_AI_WarriorBug_C_2147463116) was 2 s old and the client was far away: guid 0x0. In clear
  cmd 126, with the client nearby, the same bug is 0x290.
- p2 seq 75: bug 2147473411 shows 0x0; the watch later lists it as 0x5b2.
- So 0x0 means "not yet replicated to the client", not a lookup fault. A cross-side match by guid (deaths, card 265)
  must skip host rows at 0x0 and use the later row of the same actor.

## Note: p3 clear fault 1 (cmd 36, client) and the p3b fix

- Target 0x140 was picked at 217518315. The 45 s cap came at 217523946, 5.6 s later, so the 6 s give-up
  (`kC265TargetMs`) was never reached. The end row at 217526446 includes the 2.5 s clear-end reload.
- Fire never happened because the fire gate needed both the crosshair trace naming the target and the view within
  3 degrees (`kC265FireDeg`) of the direction to the target's bounds centre. For a bug at or touching the eye point,
  that direction swings wildly from tick to tick (the centre is at 0-60 uu from the eye), so the turn never settled
  inside 3 degrees. The trace alone did name the bug (a trace that starts inside the bug's collision names it at
  0 uu), and `c265_sees` was true. No fire meant no dry reload row and no Health drop.
- p3b fix: the angle check is gone; fire is held only while `c264_trace` from the eye along the view names the
  target. The turn still runs every tick.
- Fault 2 (Hopper held at 5929 uu, radius 1500): a held target outside `radius` (or with no distance) is dropped
  at once with a `dropped ... out of the N uu radius` row and is not counted live.
- Fault 3: nearest first; a bug within 400 uu of the pawn ignores the skip list and outranks the held target at
  every tick (`... (within 400) outranks ...` row).
- Clear end: the quiet end needs max-left met and no live bug within 400 uu of the stand (the pawn location at the
  clear's start). At the cap with a bug inside 400 uu, the clear ends failed with a row naming the bug, its guid
  and its distance, so the driver VOIDs.

## Closed (2026-11-17 13:05): the gate is the standing test tool

In use in every scenario since build 5: 1 300+ `look actor` / `look point` steps across the 305 scenario files, each
followed by the gate and the aim-proof shot (STATE "Gameplay test scenario rules"); every drive log ends with
`gate skips: N action step(s) after a voided look gate` (0 in `c448b186a` and `c225b186a`). The clear notes at the
end of this card are superseded by `clear <radius> kill` (card 265).

Reopen: a scenario action that runs after a look the gate should have voided.
