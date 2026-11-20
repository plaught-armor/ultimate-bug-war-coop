# 522: Tracking fire command for fire tests

Opened 2026-11-19 by senior from the junior's c263h report. The user: "i don't understand how we cant auto target onto
arachnids at this point instead of whatever we're doing". Today a fire round is one snap `look actor ... live`, then
2.5 s of LeftMouseButton held at that fixed aim, so a moving bug walks out of it and the players fire at nothing.

## Command

`<role> fire actor <class> <ms> live [bounds]` (test runner, `testrun.cpp`, next to `c64_look`).

- Every game tick for `<ms>` (capped at 30,000): pick the nearest live actor of the class (`c64_find_actor` with
  `live`, from the view point, as `c64_look` does), aim at its bounds centre (`g_actor_bounds`; without `bounds`,
  its location), and set the control rotation with `c64_set_yaw`. Do not use `c423_near_sample`; that is for nests.
- Fire goes through the same real input path the drive's LeftMouseButton hold uses (press at start, release at end
  and on every early exit), so the weapon, ammo and the host/client fire sync run exactly as in play.
- Retarget when the target dies or is gone. No target: keep the button released and the aim still, and retarget each
  tick; end early with a reason row after 3 s with no target.
- One `cmd: <seq> fire:` row at start (target, distance), one per retarget (old, new, cause dead/gone), and an end row:
  ms run, targets, kills (targets that died while targeted), ticks with no target, and the hit count if the game's own
  hit row (card 310/316 rows) can be counted cheaply. Otherwise say no hit count in the end row.
- The drive step waits on the end row, not a sleep (scenario-waits-on-logs).

## Proof (one run, on the next scenario run that already has a fight round)

1. Each fire round is one line per role; the end row shows kills > 0 in a round with live bugs in range.
2. The host and client views (aim proof shots) show the crosshair on a bug during the round.
3. No change to the gate: 0 mismatches.

## Owner

Junior writes it in `testrun.cpp` (and the `cmdfile` parse) while the senior writes card 521 step 2 in
`pose_c132.cpp`; both go in one build. Senior reviews the diff before the build.

## Closed (2026-11-19)

No code: card 265 `clear <radius> [seconds]` already is tracking fire (nearest live bug through the c264 visibility trace, card 124 smooth look to the bounds centre, LMB held only while the crosshair trace names the target, R when dry, quiet end 3 s after no live bug). The junior turned the 18 look/fire rounds of `tools/scenarios/c263-c3m2-plant.txt` into `ammo fill` plus `both clear 2500 10 max-left=99`.
