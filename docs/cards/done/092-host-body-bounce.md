# Card 092: Why the host's body bounces on the client's screen (log only)

Status: sent 2026-11-05 19:00 (senior). Log only. No fix in this card.

Read:

- card 028 (the remote sprite body);
- card 034 (feet on the ground, and the refit on frame size change, `g_fit_mesh`);
- `ubw/remote_body.cpp`.

Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

The user saw it in the card 091 session on play DLL `1ddc46f17e72752c`. Looking at the client's screen, the host's body bounces up and down. The user thinks it might be the client's camera. Two causes fit:

- **The client's camera moves:** view bob, step smoothing, or a server correction of the client's own pawn. In that case the whole scene should move, not only the host.
- **The host's sprite moves on its own:** the card 034 refit on each frame size change moves its Z, or the simulated-proxy smoothing of the host pawn jitters.

## Do

1. **Add `UBWCOOP_C92_LOG=1`**, client only. Every frame, for 10 s after the host's pawn is first seen, log one line:
   - the client's camera location (Z) from its `PlayerCameraManager`, and the client pawn's actor Z;
   - the host pawn's actor Z, its sprite renderer's world Z and `RelativeLocation.Z`, and the current animation key and frame;
   - a flag when a card 034 refit ran this frame.

   Cap it at 1200 lines.
2. **Three runs**, `FAST=1`, both pairs on lane 1 or lane 2. In every run the host stands still in front of the client at about 400 units.
   - **a)** The client stands still.
   - **b)** The client walks sideways (strafes).
   - **c)** The client turns in place.

   Take a short burst of client shots in each run: 5 shots, 0.2 s apart.
3. **Report** the peak-to-peak Z of each logged value in each run, and what moves together:
   - if the camera Z swings and the host's sprite Z is steady, it is the client camera;
   - if the sprite Z or `RelativeLocation.Z` swings while the actor Z is steady, it is the refit;
   - if the host actor Z swings, it is proxy smoothing or replication.

## Done when

- The Report names the cause with numbers from the three runs, and proposes a fix in one paragraph. Do not apply it.
- The Report ends with "Report complete."

## Report

### Change (log only)

- `ubw/flags.cpp` / `flags.h`: `UBWCOOP_C92_LOG=1` (`config::read_c92_log`), with the usual `config:` line. Client only.
- `ubw/remote_body.cpp`: `c92_frame`, called at the top of `c28_step` before its 100 ms throttle, so it runs every engine frame. Once the first remote pawn with a card 028 body is known, it logs one `card092: t=... dt ...` line per frame. The line holds:
  - the camera Z, pitch and yaw from `PlayerCameraManager.CameraCachePrivate` (reflected, at +0x1530; POV at +0x10 and +0x28);
  - the own pawn's X, Y and Z, and its `CharacterMovement.MovementMode` (reflected: `CharacterMovement` at +0x330, `MovementMode` at +0x231);
  - the host pawn's actor Z, its sprite renderer's world Z and `RelativeLocation.Z`, and the card 028 key and animator frame (`ActiveFrameIndex`, at +0x118);
  - `refit N` (card 034 refits of that renderer since the last line) and `sizechg N` (frame size changes).

  The window lasts 10 s, capped at 1200 lines, and closes with `window closed: N lines over M ms`. The mod takes the 5-shot burst itself (from +4 s, 200 ms apart), because drive steps block and could not overlap a key hold.
- `ubw/hooks_ubw.cpp`: `c64_find_actor` takes a `skip` actor. `c64_look` and `c75_warp` (`ubw/testrun.cpp`) pass their own pawn, so `look actor BP_PlayerCharacterMI_C` and `warp client BP_PlayerCharacterMI_C` find the other player, not the caller.
- `tools/scenarios/c92-{still,strafe,turn}.txt`. The host's copy of the client is warped 400 uu short of the host, and both sides look at each other. Then the scenario runs the action, waits for `window closed`, and quits.
- Build: no warnings; fnlen 0 over 60 (`remote_body.cpp` 46 functions). The DLL is `a67a353783e96c22` (c92c, c92d and c92e). c92a and c92b ran on `d3576c124d477b0c`, the same build without the own X/Y and MovementMode fields.

### Deviations

- **The window opens on distance, not on the first sighting.** The card said "10 s after the host's pawn is first seen". The host's pawn is seen long before the warp, so the window opens on the first frame the host is within 600 uu (2D) of the camera, which is the frame after the warp. The open line records both positions; the distance was 332 uu in every run.
- **I added MovementMode and own X/Y** after c92a and c92b, because c92a showed the own pawn's Z moving. c92e repeats the still run with them.

### Runs (lane 2, FAST=1; all scenario passed, rc 0, ERROR/FATAL 0, WARN 0 on both sides, no new crash dumps)

| run | client action | lines | client shots (`prefix2/.../Screenshots/Windows/`) |
|---|---|---|---|
| c92a | still (old line) | 1180 | ScreenShot02589-02593 |
| c92b | strafe (old line) | 1183 | ScreenShot02594-02598 |
| c92e | still | 1066 | ScreenShot02609-02613 |
| c92d | strafe, D/A 0.7-1.4 s holds | 1157 | ScreenShot02599-02603 |
| c92c | turn, MouseX ±1 | 1183 | ScreenShot02604-02608 |

Frames run at about 8.4 ms (about 120 fps), with a few hitches up to 82 ms while shots are taken.

### Peak-to-peak Z, after the first 120 lines (the 1 s landing from the warp, which leaves the client 100 uu up)

| value | c92e still | c92d strafe | c92c turn |
|---|---|---|---|
| camera Z | 5.80 | 168.56 | 3.95 |
| own pawn Z | 5.80 | 168.56 | 3.95 |
| camera Z - own pawn Z | 0.00 | 0.00 | 0.00 |
| **host actor Z** | **0.00** | **0.00** | **0.00** |
| **host sprite world Z** | **0.00** | **0.00** | **0.00** |
| **host sprite RelativeLocation.Z** | **0.00** | **0.00** | **0.00** |
| card 034 refits / size changes | 0 / 0 | 0 / 0 | 0 / 0 |
| own MovementMode | 3 on all 1066 lines | 3 on all 1157 | 3 on all 1183 |

c92a and c92b, on the older line, give the same picture: host actor, sprite and relative Z p2p 0.00, and refits 0. c92a's camera Z swings 1.8 uu after landing.

The host's key is `Idle` throughout, and its frame cycles 0-2, so the animation runs. The host's actor Z is 296.15 on every frame, the sprite world Z is 315.45, and the relative Z is 19.30.

### Cause: the client camera, because the client's own pawn never leaves MOVE_Falling

By the card's rule, this is the client camera. The camera Z swings while the host's actor, sprite and relative Z do not move at all (p2p 0.00 in every run), and no card 034 refit runs. The camera is locked to the own pawn (camera minus own Z p2p 0.00), so the whole view moves, and the host's body is the nearest object in it.

The own pawn's Z is a sawtooth. In c92e (still), one cycle is 296.10, 295.95, 295.69, 295.33, 294.87, 294.31, then a snap back up to 296.10. The steps grow (0.15, 0.26, 0.36, 0.46, 0.56): gravity. Snap counts (an upward step over 0.5 uu):

| run | snaps | mean gap between snaps | mean rise | largest rise |
|---|---|---|---|---|
| c92e still | 168 | 53 ms | 1.96 uu | 5.62 uu |
| c92c turn | 176 | 51 ms | 1.58 uu | 3.90 uu |
| c92d strafe | 150 | 60 ms | 4.39 uu | 22.11 uu |

`CharacterMovement.MovementMode` on the own pawn is 3 (`MOVE_Falling`) on every logged frame in all three runs, including 10 s standing still on flat ground at the host's own height (296.1 against 296.15). So locally the client's own capsule never finds a floor:
- each frame it falls under gravity;
- about every 50 ms (the server correction rate) the server's position for it, which stands on the floor, snaps it back up.

That fits the user's guess that it is the players' collision. On the client, the own pawn's movement sweep does not land on the ground, so it stays in Falling. The server does not have this problem, which is why its corrections put the pawn back at floor height.

The rise is larger while moving (up to 22 uu while strafing): the client predicts a full fall plus a sideways move before each correction, so the jump is large enough to read as a "bounce".

The strafe run's large Z range (168 uu, own Z down to 127.6) is a different thing. At X -1100 to -1380 and Y 1780 to 1900 the client walks off the landing pad onto the lower ground, and climbs back while strafing the other way. The sawtooth goes on throughout. The shots (02609-02613) show the host standing on the pad's ramp, with its feet on the ground and steady against the pad markings.

Not measured:
- whether the host's own pawn on the host is also stuck in Falling (this log is client only);
- whether the client's pawn is in Falling before the warp. The window opens only after the warp, but c92d walks 950 uu on the ground and stays in mode 3 throughout, and the user saw the bounce in normal play with no warp.

### Fix proposal (not applied)

Find why the client's autonomous pawn never lands, and fix it at that point rather than smoothing the camera.

1. **First, one log card.** Log the own pawn's `CharacterMovement.CurrentFloor` (`bBlockingHit`, `bWalkableFloor`, `FloorDist`), its capsule's collision enabled state and object type, and its response to `WorldStatic`, on client and host, standing still on the pad. Also log MovementMode once before and after the dropship or warp drop, and on the host's own pawn.
2. **Then fix.** If the client's capsule (or the pad and ground) is not blocking on the client, restore that response on the client's own pawn. If it is a stale Falling mode left over from the dropship exit or the zipline (the client is put on the ground by server teleport, so it never runs `ProcessLanded`), call `SetMovementMode(MOVE_Walking)` once when the client's PC takes possession of its on-foot trooper.

   Either way the pawn would stand in Walking, the server corrections would stop, and the camera would stop bobbing. That proves it: a rerun of c92-still should show mode 1, own Z p2p near 0, and no snaps. Camera smoothing (`bEnableCameraLag`-style, or smoothing the client correction) would only hide the symptom and add input lag.

Report complete.

## Senior review

Senior, 2026-11-05 19:12. **Accepted.**

- Checked: `logs/c92e-client.log` has 1066 `card092:` lines, all `mode 3`. Host Z 296.15, sprite Z 315.45 and rel Z 19.30 are fixed, and the own Z runs the 296.10 to 294.31 sawtooth with snaps.
- Correction: the user said "It might be the client camera", not collision. The camera is confirmed; the cause behind it (the own pawn in Falling) is new.
- The `c64_find_actor` skip parameter changes `look actor` and `warp client` for every scenario that names the player class. This is accepted, because before it those steps could match the caller's own pawn.
- Card 014 already logs MovementMode, the CurrentFloor flags, the capsule collision and the last blocking hit (`input_diag.cpp` chain lines). Card 093 reuses them rather than adding a second floor log.
- Next: card 093 (cause and fix in one card).
