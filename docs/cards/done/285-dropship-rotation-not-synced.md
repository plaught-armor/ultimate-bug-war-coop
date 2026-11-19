# Card 285: client dropship keeps its flight tilt after the host's landing (rotation not in the dropship frame)

Source: playtest 2026-11-13 18:32, user as client, play build `5e2ce00acad8efa3`. Screenshot
`~/Pictures/Screenshots/Screenshot_20261113_183206.png`: the player transport dropship sits on the pad
nose down and rolled, with the host's name tag inside it. Log:
`game/StarshipTroopers_UBW/Binaries/Win64/UBWCoop-menu-20261113-183002.log`.

## Trace (static, from the log and `mod/src/ubw/dropship.cpp`)

- Line 2427: `BP_PlayerTransportDropship_VerySlow_C_1` live, client spline distance 74106, host 71636, host speed
  -4994 (the host skipped the ride; the distance fell from 88335 to 0 in about 2.6 s).
- Line 2578: host state 1 (landed), host distance 0, client distance 68882, 13490 uu off, "4 fields applied".
- Line 2594: state 1/1, 0 uu off, 0 fields applied. Every later check row reads 0 uu off.
- `dropship.cpp:527`: the live apply calls `g_set_actor_location(ds, &host.at, ...)`. The frame (`DsLook.at`)
  carries a location and no rotation, and the check compares distance only.

So the client's actor was moved to the pad with the rotation it had at spline distance 68882. The client's own
spline tick, which is the only thing that turns the actor, stopped when the state went to landed. The check rows
then report a match because rotation is not measured.

## Fix

1. The dropship frame carries the actor rotation (pitch, yaw, roll). The host samples it with the location.
2. The client applies location and rotation together when either is off: location over the existing tolerance,
   or any axis over 2 degrees while the host state is not in flight (in flight the client's spline tick owns the
   rotation; applying it there would fight the tick every frame).
3. The check and live rows print the rotation difference, and a difference over 2 degrees at rest is a WARN.
4. Sibling sweep, same card: every other host-to-client location apply that calls `g_set_actor_location` on a
   world actor without a rotation (grep the call sites; `pose_c132.cpp` is expected to carry rotation already).
   List each site in this card with "has rotation" or the fix.

Both sides need the new build: the frame layout changes.

## Predictions for the run

Client joins after the host has skipped the ride (the play path: the host presses the skip button, the client is
still loading). Dropship row after the landing: rotation difference under 2 degrees on all three axes, 0 uu off.
Receiver-side shot of the landed dropship from the client: level on the pad. Digest 0 mismatches.

## Open observation, same log

Line 2745: `WARN card159 ... BP_ProceduralDropshipTroops_C_3 check: state 0/1 ... 612 uu off (tolerance 100)`,
spline differs. Later live rows converge to 0 uu off. To read with the host log after the playtest.

## As coded (junior, uncompiled)

`mod/src/ubw/dropship.cpp`:

1. Frame. `DsLook.rot` is the root component's world rotation (`component_rotator`, pitch, yaw, roll). `ds_look`
   samples it with the location. The frame inserts `<pitch> <yaw> <roll>` (`%.1f`) after `<ended>` and before
   `<kind>`, because the spline stays last (its names hold spaces). The receive parses 18 fields (was 15). Frame
   buffers use the new `kDsFrameChars` (+32 chars).
2. Apply. `at_rest` = host state != 0 (`kDsInFlight`, IsPerformingActions). `rot_bad` = at rest and any axis off by
   more than `kDsRotTolDeg` (2), with each axis wrapped to -180..180 (`ds_rot_off`). When `off > tol || rot_bad`, the
   location is applied and then, at rest only, `g_set_world_rotation(root, host rot, kTeleportPhysics)` (the card
   104/140 pattern). In flight only the location is applied, as before.
3. Rows. The check row prints `rotation X deg off (at rest | in flight, not compared; pitch c/h yaw c/h roll c/h)`.
   At rest over 2 degrees it is a WARN and counts as a mismatch. The join and live rows print the rotation off
   before, `(set)` / `(not set)` / `(in flight, not set)`, and the difference re-read after the apply. The row is a
   WARN when the after value is over 2 degrees at rest, and such a live row is written past the live-line cap.
   `same` / `differs` now includes the rotation.

Both sides need the new build: a 15-field frame from an old build is refused as `bad Dropship frame`.

4. Sibling sweep: every `g_set_actor_location` call (grep, 18 sites).

| Site | What | Verdict |
|---|---|---|
| `dropship.cpp` live/join apply | client copy to the host's dropship place | fixed here |
| `world_c98.cpp:1104` `c104_client_rest` | client copy to the host's rest | has rotation (`g_set_world_rotation`) |
| `world_c98.cpp:1154` `c140_client_placed` | client placed actor to the host's | has rotation |
| `gib_c137.cpp:975` `gib_client_pose` | client gib part to the host's pose | has rotation |
| `world_c98.cpp:1059` `c104_sweep_to` | client projectile sweep to the host's last point (hit path) | no rotation by design: the copy is moved to meet what the host's hit; it explodes or is destroyed there |
| `enemies.cpp:2176` `c132_drift_clamp` | client pawn z set back to the replicated z | location-only clamp of z; rotation comes from character replication |
| `remote_body.cpp:951` `c28_place` | host test: own pawn orbits the client pawn | test, not host-to-client; faces by ControlRotation |
| `testrun.cpp:1278` `c75_warp` | test warp | test |
| `death.cpp:1804` `c43_place`, `:1896` `c44_reviver_out` | host test placement | test |
| `enemies.cpp:283` `c57_place_host`, `:3667` `c132_off_shot_warp`, `:4591` `c42_stage_place` | test placement | test |
| `objectives.cpp:2162` `c46_drop`, `:2980` `c46_move_to` | test placement | test |
| `hooks_ubw.cpp:775` `c25_move` | test placement, faces by ControlRotation | test |
| `weapons.cpp:2336` `c50_test_move`, `:2489` `c57_test_solo` | test placement | test |
| `objective_c284.cpp` `c284_go_start` | card 284 test warp, view set by `c284_aim` | test |

No other host-to-client world-actor apply is missing a rotation.

## Senior review of the delivery (items 1-4, uncompiled): accepted for the build

Attacked: `ds_rot_off` wrap at differences of -360, -180, 180, 360 (argument of `fmod` stays positive, result in
0..180); rotation set only at rest so the client's spline tick is not fought in flight; state is applied before the
rotation, so a client still flying is first put in the host's state; `rot_warn` reads the value after the set, so
the cap bypass cannot flood; 18-field parse with the spline still last; frame buffer growth (+32 for three `%.1f`
fields of at most 6 chars and spaces). No defect found. Sweep table accepted; the teleport-versus-blend question for
the same sites is card 287 item 4.

## Closed (2026-11-17 12:14)

b183 `d6a486b94456960e`, gate c448b183a (FED_C1M1): client `card159 ... dropship BP_PlayerTransportDropship_VerySlow_C_1 check: state 1/1 ... 0 uu off ... rotation 0.0 deg off (at rest; pitch 0.0/0.0 yaw 0.0/0.0 roll -0.0/-0.0)`; client shot `client-c448b183a-c285-c-landed-ship` shows the transport level on its pad (junior viewed). Reopen on a card159 check row over 2 degrees or a tilted landed dropship on the client.
