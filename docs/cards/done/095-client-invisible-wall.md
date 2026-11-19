# Card 095: Client blocked by an invisible wall at the capture-the-flag entrance

Status: accepted 2026-11-05 (senior), wall not reproduced; the user playtest decides. If the client is still blocked, the client log lines named in the Report pick the next step.

Read:

- `docs/playtest-2026-11-05.md`, item 7;
- card 056 (PickupGone relay of a placed, non-replicated actor's change by actor name);
- card 042 (Gib relay), for the relay shape.

Repo root: `/path/to/ultimate-bug-war-coop`. Play DLL base: `718ad12a93ca2896` (card 094).

## Why

In the 2026-11-05 playtest the client could not enter the capture-the-flag area of FED_C1M1. An invisible wall stopped
the client at the entrance; the host walked through. Neither log has a line for it.

Likely cause: a placed blocker (blocking volume, or an actor with blocking collision) that the level script turns off
or destroys when the objective before it is reached. The script runs on the host only. Placed actors here are not
replicated (`bReplicates 0`, like the pickups), so the client's own copy of the blocker stays on and the client's own
movement collides with it.

## Do

1. **Find the blocker.** On the host, at the capture-the-flag entrance, log what opens the way: the actor (name, class,
   location) and the call (`SetActorEnableCollision`, `SetCollisionEnabled`, `SetCollisionProfileName`,
   `SetActorHiddenInGame`, `K2_DestroyActor`, or a level-sequence/streaming change). One line per change, for actors
   within a few thousand uu of the entrance only. On the client, log the client's copy of that same actor and its
   collision state when the client is stopped there.
2. **Fix at the cause:** relay the change by actor name, host to client, and apply the same change to the client's copy
   (reuse the card 056 path if it fits). If the script call is a general one (any blocker the script opens), relay
   the call, not this one actor, so other script-opened blockers are covered too.
3. **Proof**, one combined scenario run (FAST=1): drive the host through the objective that opens the entrance, then
   walk the client through it. The client passes on both screens; the client log shows the relayed change applied to
   the named actor. rc 0, ERROR/FATAL 0, no new dump.

Stop when the fix builds and that one run shows the client passing; the user will playtest.

## Done when

- The Report names the blocker actor, the script call, the fix, the proof run and the DLL hash.
- The Report ends with "Report complete."

## Report

**Blocker actor: not pinned down in a run.** The capture-the-flag entrance is the door pair
BP_Door_Sliding_Large_C_0 at (-3674, 9849, 298) and BP_Door_Sliding_Large_C_1 at (-4630, 9855, 285). Right at door _0
sits BlockingVolume_1 at (-3635, 9855, 225). All three are placed and not replicated (bReplicates 0). The doors are pure
Blueprints (BP_Door_Sliding_Base), not ASTDoor. With FAST=1 the entrance is open on both sides: BlockingVolume_1 has
CollisionEnabled 0 on host and client, and both pawns walked north through door _0 in scout c95-scout2. The user notes
that the entrance opens only after every objective in the tutorial area is finished. A FAST run does not finish them,
so the wall did not reproduce, and the script call that opens it was not seen.

**Cause class.** The level script runs on the host only, and placed actors do not replicate. The scouts showed host-only
collision writes on placed actors that the client never gets. The first proof run found 39 of them, for example:
- killable crate meshes, 3 -> 0;
- BP_SatchelPlacementSite_Flag_C meshes, 3 -> 0;
- target-range Box/Cube, 1 -> 0;
- BP_Dropship_LandingBlocker_C_0 Box, 0 -> 2 (host only).
Any of these leaves the client's copy colliding where the host's does not, which is the invisible wall. The host's
server copy of the client moves by the host's collision, so the client's own sweep is the one that stops.

**Script call relayed.** The fix relays the collision writes themselves, so it does not depend on which actor the
tutorial completion opens:
- `AActor::SetActorEnableCollision`;
- `UPrimitiveComponent::SetCollisionEnabled`.

**Fix (mod/src/ubw/door_c95.cpp, input_diag.cpp, chan_ops):**
- Host (listen server): after the original, each real change (before != after) on a placed, non-replicated actor or
  one of its components is journalled. The journal has 256 rows per map and is keyed by host actor + component name.
  The change is sent to every ponged client as the new text opcode CollisionOp:
  "name x y z value component|-". The location is in the frame because names repeat across sublevels (two
  BlockingVolume_1).
- Host replay: a client's first Pong of the map replays the whole journal (card 056 pattern), so writes made before
  the client joined still arrive.
- Client: it picks its placed actor with that name nearest to (x, y, z). It finds the component by name with
  StaticFindObject(outer = actor) and checks the owner. It then calls the original SetActorEnableCollision or
  SetCollisionEnabled. Misses are WARN lines and are counted in the map totals.
- Also added:
  - an ASTDoor OpenDoor/CloseDoor/SetDoorActive relay by name (DoorOp opcode), for native doors;
  - a `door list|open|close` test command;
  - a capped card095 note on collision writes to placed blocker-like actors, on both sides.

**Proof run c95-proof** (FAST=1, tools/scenarios/c95-proof.txt): host rc 0, client rc 0, no new crash dumps. No
ERROR/FATAL lines; the only grep hits are the hook name ServerCheckClientError.
- On the client's first Pong, the host replayed 39 collision rows.
- The client log shows 197 "applied" lines that changed its copy, for example:
  - "collision SatchelSiteCAC_BP_SatchelPlacementSite_Flag_C_CAT_0 -8357 21000 380 0 Mesh
    (BP_SatchelPlacementSite_Flag_C): applied, 3 -> 0";
  - "collision BP_KillableCrate_01_C_0 -3947 29513 -1 0 StaticMesh (BP_KillableCrate_01_C): applied, 3 -> 0".
- 0 missed.
- The client walked to the door and through door _0 to (-3674, 10400): goto done.

**Triggering objective success (new test command).** `objective complete <id>|all` (host only, cmdfile Op::Complete,
`c95_complete` in testrun.cpp; drive.py knows it). It completes every incomplete objective through the card 049
detours, sub-objectives first (CompleteSubObjective(parent, id)), then the top-level ones, so the host mirror sends
each one to the client. One `all` pass after landing finishes the 14 tutorial objectives (Assault_O1/SO0,
ShootingRange_O1/SO0/SO3, TacSupport_01 and its 8 subs). The level script then adds CaptureTheFlag_O1 with
CaptureTheFlag_SO0 (presence actor BP_PlayerPresence_C_7), and the client shows both. A second pass would also
complete the capture-the-flag objective, so scenarios use one pass.

**Run c95-obj2** (FAST=1, tools/scenarios/c95-objectives.txt): host rc 0, client rc 0, FATAL/ERROR 0, WARN 0, no new
crash dumps, drive rc 0.
- One `objective complete all`, then CaptureTheFlag_O1/SO0 incomplete on the client.
- No host collision write on a placed actor follows the tutorial completion (the only nearby ones are the dropship
  landing blocker 0 -> 2 -> 0, relayed to the client). So in this run, the completion did not open the entrance with
  either collision call.
- Door list matches on both sides: the same 11 placed actors at the entrance, including BlockingVolume_1 at
  (-3635, 9855, 225) and doors _0 and _1.
- The client walked from (-3240, 9459) through door _0 to within 144 uu of (-3674, 10400): "goto: end (reached)",
  0 stucks.

**Not proven:** the invisible wall did not reproduce in a FAST run, with or without the objectives done, so the
blocker actor is still not named. The user will playtest: finish the tutorial with the client present (or run
`objective complete all` on the host), then walk the client through the capture-the-flag entrance. If the client is
blocked, the client log should show which "card095: client ... collision <name> ...: applied" lines arrived. If none
did, the opener is neither collision call; a Blueprint door timeline is the next suspect.

DLL hash: 96c41a875d2030cb

Report complete.
