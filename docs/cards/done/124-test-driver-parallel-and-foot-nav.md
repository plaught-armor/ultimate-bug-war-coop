# Card 124: Test driver: both players act in parallel, better pawn control and on-foot navigation

Status: written 2026-11-07 (senior); sent, recoveries proof added at review, accepted 2026-11-07.

Read:

- card 063 Report (`tools/drive.py`, command and state files, `mod/src/cmdfile.cpp`);
- card 064 Report (`mod/src/ubw/testrun.cpp` goto and look: navmesh path, W through InputKey, yaw steering, the
  stuck back-off; `c64_adopt_main_nav`);
- card 118 Report (`both <command>`);
- card 123 Report, Runs section (c123a client `goto: end (stuck)` on FED_C2M1 terrain; c123b drive rc 1, the final
  `client shot` timed out behind a long client goto).

Repo root: `/path/to/ultimate-bug-war-coop`. DLL base: `4d058f899b394c59` (card 122), or card 123's.

## Why

User (2026-11-07): improve parallel play in the test runs, control over the player, and navigating the map on foot.
What the runs show today:

- **Serial play.** drive.py runs one step at a time. Apart from `both <same command>`, the host and the client never
  do different things at once. Inside each game, a command waits behind a running goto (c123b: the client's last shot
  timed out behind a goto), so a role cannot look, shoot or take a screenshot while it walks.
- **Client navigation is a straight line.** Per the card 064 header, a net client has no navigation system by default
  (`bAllowClientSideNavigation=False`), so the client's goto walks straight at the goal, gets stuck on terrain and
  backs off. The client is the side we most need to walk (c123a ended `goto: end (stuck)`). Route attempts in cards
  119 and 123 lost runs to this.
- **Control.** The stuck handling is back off with S, then walk 45 degrees off. There is no jump, no sprint, no
  crouch and no re-path from the host's mesh, so small ledges, rocks and props end a walk.

## Do

1. **Measure (static, then the run in step 5).**
   - Why the client has no path: is the RecastNavMesh in the client's loaded level (the nav data actor list that
     `c64_log_nav` prints on the client)? Does the client's `UNavigationSystemV1` exist, and what does it take for it
     to build or adopt the placed nav data (`bAllowClientSideNavigation`, set through a user config such as
     `Saved/Config/Windows/Engine.ini`, never a game file)?
   - What ended the walks in the existing run logs: count `goto: end (reached|stuck|timeout)` per role and map over
     the logs that hold them, and list the stuck spots (map, position, what was in the way, if a shot shows it).
2. **Client paths, one mechanism.** Give the client's goto a real navmesh path. Preferred: the client's own
   navigation system on the placed nav data (config or the `c64_adopt_main_nav` route), if step 1 shows the mesh is
   on the client. Only if it is not: the client asks the host for the path points over the test command channel. The
   straight line stays as the last fallback, and it logs a WARN when it is used.
3. **Pawn control in goto.** User (2026-11-07): "I want to make the automated tooling as human and perfect as
   possible." Human means a walk drives the pawn only through the inputs a player has, at a rate a player can do it:
   - Steering goes through the mouse axes (`MouseX`/`MouseY` axis events through the PC's input path, as the `axis`
     step does), at a capped turn rate with easing, not by setting the control rotation. Say the rate you chose and
     why. `look` keeps its current meaning for setup steps. Add `look ... smooth` for a human turn.
   - Movement is W/A/S/D, jump, sprint and crouch keys only. A walk never warps or writes the pawn's location or
     velocity. The proof run uses no warp.
   - Perfect means a walk that the navmesh says is reachable ends `reached`. A stuck walk gets every recovery below
     before it gives up, and when it does give up, the log names the spot and what blocked it.
   Recoveries:
   - On a stuck: re-query the path from the current spot first. Then try a jump (the game's jump key through InputKey),
     then the side step. Log which one freed the pawn.
   - An optional `sprint` flag on goto (hold the game's sprint key while walking), for long routes.
   - Measure the trooper's step height and jump reach (CharacterMovement `MaxStepHeight`, `JumpZVelocity`) and give
     them in the Report, so we know which obstacles a walk can clear.
4. **Parallel steps.**
   - In the game: goto runs in the background. Its command returns "started", other commands (look, key, shot,
     axis, warp) run while it walks, and a new goto replaces the running one. The end line stays as it is. A new
     `<role> wait goto [timeout=S]` step waits for the walk's end line, and fails on stuck or timeout.
   - In drive.py: a step prefixed with `&` starts without waiting for its result. `join [<role>]` waits for every
     started step of that role (or of both), and a failure there fails the scenario the same way as now. Keep every
     existing scenario working unchanged: a plain `goto` step still blocks until the walk ends.
   - Update the drive.py usage text.
5. **Proof, one run.** FED_C2M1 by `UBWCOOP_TRAVEL`, FAST=1, both players in, muted, windowed 640x360.
   - The host and the client each walk a different route to a different target at the same time, on foot, with no
     warp. The client's route crosses the terrain where c123a stuck. While walking, each role takes a screenshot
     (shot during goto), then both `wait goto` reach their targets.
   - The client log shows a navmesh path (not the straight line), and every stuck in the run shows which step freed
     it.
   - The log shows steering only through mouse axis events (no control rotation writes during a walk) and the peak
     turn rate used.
   - rc 0 for the drive and both games, ERROR/FATAL 0, no CRASH line, no new dump.

Stop when the driver changes build and that run shows them; then the next cards use them.

## Done when

The Report has the step 1 findings (client nav system and mesh, the end-reason counts, the stuck spots), the client
path mechanism and where it sits, the stuck order and the step height and jump numbers, the new scenario syntax, the
proof run lines (both walks started together, shots during the walks, client navmesh path, both ends reached), the
run length, rc, ERROR/FATAL, CRASH lines and dumps, and the DLL hash (`sha256sum | cut -c1-16`) with the play DLL
saved as `build/dwmapi-play-<hash>.dll`. Ends with "Report complete."

## Report

DLL: `9f2d3eec4477d9c4` (sha256 prefix), saved as `build/dwmapi-play-9f2d3eec4477d9c4.dll`. Built with no warnings. Base was `18783477b1111c15`.

### 1. Measure

- **Client nav system and mesh.** The client already has a NavigationSystemV1 and navmeshes. Card 064 put them there through config only (no game file): `c64_allow_client_nav` sets the CDO's `bAllowClientSideNavigation` and `bCreateOnClient`, and `c64_adopt_main_nav` takes `RecastNavMesh-Default` as the main nav data. The card's premise is stale. Over 237 logs, 221 of 221 client gotos and 209 of 210 host gotos walked a navmesh path. The single straight-line goto was on the host.
- **End reasons over 237 logs:**

| Role | reached | stuck | timeout |
|---|---|---|---|
| host | 208 | 0 | 2 |
| client | 207 | 2 | 10 |

- **Stuck spots, by type:**
  - **Type A: the goal is off the navmesh end.** The path ends short of the goal and the pawn pushes into geometry. Runs: c101c/c94-scout6, c101f, c64g, c64h, c65a host, c73d host, c121e. A recovery cannot free these. The start line now logs "the goal is N uu (2D) past the navmesh path's end", and the give-up line names the blocker.
  - **Type B: mid-path.** c100a. c75a6/a8/a9 at -2081 1765 451. c123a on FED_C2M1: first stuck at 231952 43172 8721 (point 3/10), final stuck at 226481 37423 8554 (point 2/25). It ended stuck after 6 stucks at 40.6 s, because the only recovery then was backing off.

### 2. Client paths: one mechanism

Both roles use the same path query in `c64_plan` (`mod/src/ubw/testrun.cpp`), on the navmesh above. The straight line is only the last fallback, used when there is no main nav data. It logs `WARN card124: ... no main nav data; ... straight line`. No WARN appeared in c124a.

### 3. Human pawn control

- **Steering uses only MouseX/MouseY axis events** (`c124_turn`, through `autoinput::axis_event`).
  - Rate: capped at **240 deg/s**, eased at 1440 deg/s² (the cap is reached in about 1/6 s), with 4 deg/s per degree of error. This is a calm human turn: flicks run 500+ deg/s, tracking 100-300.
  - The gain per unit is learned from what the game applies (start −0.14 / +0.14 deg/unit, from c92c).
  - A walk never writes the control rotation. Each goto end logs `control rotation writes N`, and c124a has 0 on both roles.
  - W is held only while the heading is within 45°. It is released above 75°, so the pawn does not orbit at corners.
- **`look ... smooth`** turns through the same axis events, finishing within 1° (3 s limit). Plain `look` is unchanged for setup steps.
- **Movement** uses W/S/A/D, SpaceBar (jump) and LeftShift (sprint) only. A walk never warps and never writes location or velocity. The new optional `sprint` flag holds LeftShift.
- **Stuck recoveries** run in order: re-query the path, then jump (SpaceBar with W held), then side step (S for 600 ms, then A or D for 800 ms, then W and re-query). After each one the log says `freed by <recovery> (moved X)` or `<recovery> did not free it`. After 6 stucks the give-up line is `WARN cmd: N goto: gave up (<why>) at x y z, ...`. It names the blocker from Pawn-profile traces at knee, waist and head over 150 uu, plus a floor drop 100 uu ahead: off navmesh, wall, low obstacle, partial, ledge, or nothing.
- **Pawn numbers** (logged as `card124:`, the same on both roles): MaxStepHeight 25.0, JumpZVelocity 800.0, GravityScale 1.50, jump apex 218 uu, MaxWalkSpeed 700.

### 4. Parallel steps

- **In the game:**
  - goto runs in the background and returns `cmd: N goto: started`. look, key, shot, axis and warp run during the walk.
  - A new goto ends the running one ("replaced").
  - The end line is unchanged, followed by the steering line.
- **`<role> wait goto [timeout=S]`** fails unless the role's last goto reached its goal.
- **drive.py:**
  - `& <role>|both <command>` posts a step and goes on at once.
  - `join [<role>]` waits for those steps and fails if any failed.
  - A plain goto still blocks, so existing scenarios are unchanged.
  - The usage text is updated.

### 5. Proof: c124a (`tools/scenarios/c124-parallel.txt`)

Setup: FED_C2M1 by `UBWCOOP_TRAVEL`, FAST=1, both players in, muted (PULSE_SINK), windowed 640x360.

- **Both walks started together**, at drive +114.3 s: `& client goto 226311 37189 8569 sprint -> started client cmd 1` and `& host goto 229112 39656 8694 -> started host cmd 3`. Both were on foot, with no warp.
- **Shots during the walks:** `host shot c124-walk` and `client shot c124-walk` were done at +118.4 s and +118.5 s.
- **Client navmesh path:** `cmd: 1 goto: from 232629 43682 8721 to 226311 37189 8569, navmesh of 2 points, 9154 uu, walk 700 uu/s, timeout 33 s, sprint`.
- **The client's route crosses the c123a stuck terrain.** It passed 231660 43018 (about 330 uu from the c123a first stuck), then 231056 42257, 229332 40449 and 227734 38787, to within 200 uu of the c123a second stuck.
- **Both reached their goals:**

| Role | End line | Steering line |
|---|---|---|
| Client | `end (reached): dist 135, path 9154 uu, 7.7 s, 0 stuck(s)` | `815 mouse axis event(s), peak turn rate 144 deg/s (cap 240), MouseX gain 0.093 deg/unit, control rotation writes 0` |
| Host | `end (reached): dist 145, path 4632 uu, 6.9 s, 0 stuck(s)` | `748 mouse axis event(s), peak turn rate 240 deg/s (cap 240), MouseX gain 0.140 deg/unit, control rotation writes 0` |

  Both `wait goto` steps were done, and `join` returned `host 3 (goto): done; client 1 (goto): done`.
- **Host smooth look:** `look: smooth turn done: 148 mouse axis event(s) so far, peak 240 deg/s`.
- **Stucks: 0 in this run,** so no "freed by" line exists here. The recoveries are not exercised by this run. The c123a stuck came from a different path (a 10-point path toward a far pickup). On this route the client got through the same ground without sticking.

| Run | Length | rc | ERROR/FATAL | WARN | CRASH lines | New dumps |
|---|---|---|---|---|---|---|
| c124a | 125 s (scenario), 131 s wall | drive 0, host 0, client 0 | 0 / 0 | 0 / 0 | none | none |

Caveat for the user: the stuck recoveries and the give-up blocker report are built but were not hit in this run.

Report complete.

## Senior review (2026-11-07)

Accepted as built: parallel steps, `& ... / join`, background goto with `wait goto`, mouse-axis steering at 240 deg/s
with 0 control rotation writes, `look ... smooth`, and the pawn numbers. The client navmesh finding is right; my
premise came from the card 064 header comment, which is now stale (fix that comment).

Not done: the user asked for walks that are "perfect", and the card asked that every stuck show what freed it. c124a
had 0 stucks, so none of the recoveries or the give-up report ran. Both stuck types are known and repeatable:

1. **Type B, mid-path:** rerun the c123a walk exactly: FED_C2M1 by `UBWCOOP_TRAVEL`, client, the same
   `goto actor BP_GrenadeAmmoPickup_Frag_C 80` from the same start (its 10-point path through 231952 43172). Also the
   c75a6/a8/a9 spot (-2081 1765 451) if it is on a map the same run can reach. The log must show each stuck and which
   recovery freed it, and the walk must end reached.
2. **Type A, goal past the navmesh end:** a human walks off the mesh to a goal they can see. When the path ends short
   of the goal, the walk goes on from the path's end straight toward the goal, with the same recoveries, and it ends
   reached if the pawn gets within the radius. It gives up only after the recoveries fail, with the blocker named.
   Show it on one of the type A spots from step 1 (c121e or c101f, whichever is on a map the run already loads).

One run for both if they fit (parallel steps: one role per case). Same rules: muted, windowed, no warp during a walk,
rc 0, ERROR/FATAL 0, no CRASH, no dump. Append a short Report addendum with the lines, the DLL hash and the play DLL.
Ends with "Report complete."

## Report addendum (senior review)

DLL: `bce0340ca6cc197a` (sha256 prefix), saved as `build/dwmapi-play-bce0340ca6cc197a.dll`. Built with no warnings.
Scenario: `tools/scenarios/c124-stuck.txt`. It starts on FED_C1M1 with one role per case in parallel, then goes to
FED_C2M1 with `host travel next` for the c123a walk. No warp happens during a walk. The client is warped to the
c100a start before its walk begins.

### Code changes

- **Downed ends the walk.** A downed pawn (card 043 own downed view, walk speed 0) now ends the goto as `downed`
  with a WARN line. No input can move it, so recoveries would only burn time.
- **Freed on reach.** When a recovery frees the pawn and it reaches the goal before the next 2 s check, the log now
  says `freed by <recovery> (reached the goal)`. Before this, that line was missing.
- **Stuck count capped at 6.** The end line used to say 7.
- **Close-in steering.** Within 500 uu of the steered point, W is released above 25° and pressed again below 10°.
  The far values stay 75° / 45°. In c124c the eased turn drew a 170-330 uu circle, so the pawn orbited a goal
  55-105 uu away until the timeout.
- **Reach height.** Reached means within the radius in 2D, and in z within 300 uu of either the goal or the floor
  at the navmesh path's end (`floor_z`). In c124d the c100a goal actor's root was 379 uu above the walkable floor:
  the pawn stood 8 uu away in 2D and still timed out.
- **Comments.** The card 064 header in `testrun.cpp` was rewritten (navmesh on both roles, background goto,
  recoveries). The card 006 comment in `hooks_internal.h` now says the client navigation system exists only for the
  test runner.

### c124e lines

Type A, host, c121e spot (FED_C1M1, `goto actor BP_PanelSwitch_WithoutScreen_C_0 90`):

```
cmd: 4 goto: from 17048 12495 216 to 17309 12643 116, navmesh of 7 points, 1160 uu, walk 700 uu/s, timeout 21 s
cmd: 4 goto: the goal is 189 uu (2D) past the navmesh path's end
cmd: 4 goto: stuck 1 of 6 at 17309 12786 213, point 7/7, trying re-path
cmd: 4 goto: re-path did not free it
cmd: 4 goto: stuck 2 of 6 at 17309 12786 213, point 3/3, trying jump
cmd: 4 goto: freed by jump (reached the goal)
cmd: 4 goto: end (reached): dist 86, path 289 uu, 10.3 s, 2 stuck(s)
cmd: 4 goto: steering: 1217 mouse axis event(s), peak turn rate 240 deg/s (cap 240), ..., control rotation writes 0
```

Type B and type A together, client, c100a walk (FED_C1M1, `goto actor BP_PlayerPresence_C_24 80`):

```
cmd: 1 goto: from -496 15149 97 to 5311 18971 476, navmesh of 17 points, 10343 uu, walk 700 uu/s, timeout 34 s
cmd: 1 goto: the goal is 155 uu (2D) past the navmesh path's end
cmd: 1 goto: stuck 1 of 6 at -306 13960 102, point 3/17, trying re-path
cmd: 1 goto: re-path did not free it
cmd: 1 goto: stuck 2 of 6 at -306 13960 102, point 2/16, trying jump
cmd: 1 goto: jump did not free it
cmd: 1 goto: stuck 3 of 6 at -306 13963 103, point 2/16, trying side step
cmd: 1 goto: freed by side step (moved 659)
cmd: 1 goto: stuck 4 of 6 at -819 14375 97, point 1/19, trying re-path
cmd: 1 goto: re-path did not free it
cmd: 1 goto: stuck 5 of 6 at -819 14375 97, point 1/19, trying jump
cmd: 1 goto: freed by jump (moved 983)
cmd: 1 goto: end (reached): dist 77, path 10728 uu, 33.6 s, 5 stuck(s)
cmd: 1 goto: steering: 3791 mouse axis event(s), peak turn rate 240 deg/s (cap 240), ..., control rotation writes 0
```

The exact c123a walk, client (FED_C2M1, `goto actor BP_GrenadeAmmoPickup_Frag_C 80`, the same 10-point path):

```
cmd: 3 goto: from 232625 43680 8721 to 181768 6011 7480, navmesh of 10 points, 65663 uu, walk 700 uu/s, timeout 113 s
cmd: 3 goto: at 231956 43030 8724, dist 62363, point 3/10, moved 933, 2.0 s
...
cmd: 3 goto: at 225589 36908 8573, dist 53618, point 3/10, moved 641, 16.0 s
WARN cmd: 3 goto: the pawn is downed at 225589 36908 8573 (card 043 downed view, walk speed 0)
cmd: 3 goto: end (downed): dist 53618, path 65663 uu, 17.1 s, 0 stuck(s)
```

### Findings

- **c123a was not a terrain stuck.** In c124b, WarriorBugs downed the client (Health 100 to 1, `card043: client own
  view downed ... walk speed 0`), and the old walk logged that as stuck. In c124e the walk passed the c123a spot
  (231952 43172) at 2 s without a stuck, and bugs downed the client at 17 s, 6400 uu further on. A walk on this map
  cannot end reached while bugs down the player on the way. That is for the card 126 fight logic, not for navigation.
- **c75a spot (-2081 1765 451)** is on FED_C4M1 with a spawned mech (`c75a-mech.txt`). This run cannot reach it, so
  it was not exercised.
- **Earlier attempts.** c124b found the downed client and the missing `freed by` line. c124c found the orbit near
  the goal and an unreachable goal z (69, under a floor at about 500). That case is avoided by the c100a start warp,
  which happens before the walk.

### Runs

| Run | DLL | Length | rc | ERROR/FATAL | WARN | CRASH lines | New dumps |
|---|---|---|---|---|---|---|---|
| c124d | 44c4e1790a2047d8 | 157 s wall | drive 0, host 0, client 0 | 0 / 0 | 0 / 4 (give-up, failed, downed, failed) | none | none |
| c124e | bce0340ca6cc197a | 175 s | drive 0, host 0, client 0 | 0 / 0 | 0 / 2 (downed, failed) | none | none |

c124d ran before the reach-height fix: the c100a walk timed out at a 2D distance of 8 uu. c124e is the proof run.

Report complete.
