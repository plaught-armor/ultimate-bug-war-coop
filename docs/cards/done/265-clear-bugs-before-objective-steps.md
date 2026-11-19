# 265 Test players kill the bugs around an objective before acting on it

User rule 2026-11-13, watching the test windows: "The bugs are also causing whatever objective they need to
hit hard to do so because they get in the way. Bugs need to be killed to do objectives properly."

Today the scenarios warp into a fight under god mode and press or aim through the bugs. Bugs body-block the
pawn, stand in the line of fire and push it off its stand. Built and proven together with card 264 (one
build, shared probes).

## Change

`mod/src/ubw/testrun.cpp` (test driver only): verb `<role> clear <radius> [timeout=S]`.

- Each round: among live hostile AI pawns within radius of the own pawn (reuse the mod's existing bug table,
  do not write a second enumeration), pick the nearest one the card 264 trace can see; turn to it with the
  card 124 smooth look; hold fire through the real input path (`InputKey` of the fire key) until its death is
  logged or 6 s pass; reload by the real key when the magazine is empty; then the next one.
- Ends when no live bug is within radius for 3 s, or at the timeout. One row:
  `cmd: N clear: killed K, left L within R uu after S s (ammo A)`.
- No direct damage calls, no kill by console: the deaths must come from the weapon so the host/client death,
  gib and hit sync is what the soak exercises. The client runs the same verb with its own pawn, which also
  gives the client-fired kills the soak has not proven yet.

`tools/drive.py`: route the verb; `clear ... max-left=<N>` voids when the row's `left` is over N (default 0).

Generators (`gen_c243.py` first, then 241, 242, 244-247): before every press, aim, throw and damage step
`both clear <radius>` (host and client in parallel), then `wait stand`, then the card 264 `look ... hit=`,
then the shot, then the action. Where the script spawns without end until the objective is done (nests, the
EM hold waves), the step states that in a comment, uses `max-left` with the count it tolerates and relies on
the 264 gate: a bug in the crosshair line names itself in the aim row and voids the action.

## Proof

Probe on FED_C3M2 at the Monorail nest area and the `BP_Switch_C_2` fight: clear rows on both sides with
`left 0`; death rows for the same guids on host and client; shots after the clear show no bug between the
pawn and the target; ammo use reported (state whether 240 rounds last, and where the scenario must pick up
ammo by a real pickup).

Files: `mod/src/ubw/testrun.cpp`, `tools/drive.py`, junior's generators. No sync code. Budget: shared with 264.

## Step 1 (junior, static, before code)

- Bug list: card 023's table, `c23_collect(world, pawn, g_enemy_class(), list)` (`enemies.cpp`, `C23List` in
  `hooks_internal.h:1986`), each entry with actor, location and distance to this side's pawn; AI troopers are
  ASTEnemyBase too and are skipped by `c23_trooper()`; live = `c23_set_health(actor) > 0`.
- Visible: the card 264 aim trace from `c24_view_point` to the bug's bounds centre (`g_actor_bounds`); visible when
  the hit actor is the bug (same grant question as card 264).
- Turn: card 124 smooth look, `g_c124.look_yaw/look_pitch` then `c124_turn()` each tick (mouse axis events).
- Fire: `autoinput::key_event(pc, L"LeftMouseButton", true/false)`, the path the `key` verb uses; reload
  `L"R"` the same way when the weapon reports an empty magazine (property read in code step, by reflection).
- Death: the bug's `c23_set_health` at or under 0, or the actor gone from the next `c23_collect`.
- Row: `cmd: N clear: killed K, left L within R uu after S s (ammo A)`; client runs the same verb on its own pawn.
- Senior condition 1 (2026-11-13): `c23_set_health` is read only, to know whether a bug is alive. Nothing in `clear`
  writes health or ammo on any actor; no top-up of the own pawn (god mode covers the host pawn, ammo comes from real
  pickups). Every kill is weapon fire through `key_event`.
- Parser: the verb needs `Op::Clear` in `mod/src/cmdfile.h` and one parse block in `mod/src/cmdfile.cpp`
  (`clear <radius>`); asked the senior for that grant.

## Senior check: "client kills without a host death" (probe c264-c265)

Not a sync gap. The 10 unmatched guids are all `BP_AI_ChariotBug_C`; all 10 warriors matched. The host rows
used for the match (`host ragdoll start`, `host stream guid`, `host pose unfreeze`) exist only for classes
with a ragdoll. Chariots die by gore: the host logs 10 `MS_BARK_ChariotBug_Death` and 10
`NS_FX_Gore_BloodDeath_Green01` rows, the client 10 death-bark world rows and 10 `client death gore` rows, at
the same 10 positions on both sides (for example (-42613, 48239, 807) and (-44276, 48893, 607)). The host's
`card132: host HandleDying` row is capped (4 chariot rows of 10 deaths), so it cannot serve as the match row
either. The death match in the 265 proof must use a row every class has on both sides, keyed by guid.

## Follow-up (senior note 2026-11-13, card 243 run 8): drop test without hysteresis. No build for this alone

Seen in run 8 (logs/c243-c3m2-r8-host.log):
- 15357: the clear targeted BP_AI_WarriorBug_C_2147476091 at 378 uu.
- 15606: it dropped the bug at 408 uu (`out of the 400 uu radius`).
- 18638: the clear ended at its 15 s cap with left 0.
- After that, the same bug meleed the host pawn 22 times in 15 s, and the next stand voided.

The drop test is a bare `distance > radius`, so a bug moving along the edge leaves the clear's watch.

Proposed when a DLL build is next due anyway: drop a target only beyond radius x 1.25, and never drop a target that
hit the pawn in the last 3 s.

## Closed (2026-11-17 13:05): the clear is the standing test tool

`clear <radius> kill` kills by code (user rule "clear bugs first"); 409 such steps across 46 scenario files. The
drop-test hysteresis follow-up no longer applies: a code kill has no target to drop. The Brain_Bug spare of
`c265_clear_kill` is in b186.

Reopen: an objective step that fails with a live bug inside the clear radius after a clear reported done.
