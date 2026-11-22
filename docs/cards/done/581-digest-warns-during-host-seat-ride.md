# Card 581: digest WARNs while the host rides the Door Gunner seat (c372b402a)

Status: closed 2026-11-21, b422 c587b422b (presence reject gate holds; reject rate accepted).

Found in c372b402a (b402, FED_C3M1), client log, during the host's seat ride (host seat from 161318685, pawn back at
161353734):
- `WARN card327: client t=161351844 digest round 4 PW differs for two rounds: host e06d, client ad31`. The client
  weapons are MoritaMk1, with ammo Morita 240, UnderslungShotgun 8 and charge 14500 0 -1 0 -1 0 -1 0. The client never
  sat in this run (the scenario's client seat step was void).
- `WARN card132: mismatch BP_PlayerPresence_C BP_PlayerPresence_C_9 triggers host 0001 client 0000` at 161352877,
  round 4: 1 mismatch of 5600 actors.

The gate rule is 0 mismatches, so both count. Both landed inside the host's seat window. Likely the same class: the
host's trooper is unequipped and parked while it drives the seat, and the two sides disagree on its state (the
presence volume overlap, and a weapons, ammo or charge field in the PW digest).

Next (static first): read the PW digest fields (card 327) and the host log at round 4 to find the field that
differs. Then check where the host trooper is on each side during the ride (a presence trigger at the host side
only). Fix by class, so every seat and vehicle ride is covered, not only the Door Gunner.

Logs: logs/c372b402a-host.log, logs/c372b402a-client.log.

## c372b404a (b404) and static trace: PW cause found, fix in b407 `557b7fc860c35a8b`

- PW WARN again: 161988024, same hashes (host e06d, client ad31). Only the charge part differs: the host copy has
  12000 and the client has 14500. The PlayerPresence WARN did not appear this run.
- Sequence on the client at the host's seat (Door Gunner use):
  - 161953824: TacCharge 12000 applied from the host. The host sent it at 161953799, in the same ms as FedScore
    9500 -> 12000 (+2500). So the 12000 already holds the charge from that +2500.
  - 161953911: the card 323 settle broadcast OnFedScoreGained 2500 (a host-only add). `c475_broadcast` calls every
    binding except the popup, and that includes `USTTacticalSupportComponent::HandleFedScoreUpdate`. So the client
    charged the same 2500 a second time: 14500.
  - The host copy's charge text did not change after that, so no new TacCharge came to correct it.
- Fix (class: every client re-broadcast of a host-only add, both the card 323 settle and the card 475 join replay):
  `c475_broadcast` skips the `HandleFedScoreUpdate` binding, as it skips the popup. The host's TacCharge frame owns
  the client's charge.
  - Adds that both machines make (mirrored kills, objective completes) still charge on each side through the
    game's own broadcast, and the host's absolute TacCharge matches them.
  - The join row and the settle row now print `charge bindings skipped N`.

Predictions (c372 on b407):
- 0 card327 PW WARNs.
- The settle row after the host's seat has `charge bindings skipped` at 1 or more.
- The client's charge text equals the host copy's row at the next digest.
- The PlayerPresence trigger WARN stays open: if it shows again, trace it.

## c372b407a (b407) result: PW part passes

- 0 card327 PW WARNs. The join and settle rows show 1 to 2 charge bindings skipped, and the settle totals equal the
  host's.
- No BP_PlayerPresence_C WARN this run. That part stays open until a run of the same seat ride shows it again or
  shows 0 across the next c372 run.

## c582b414b (b414): the PlayerPresence WARN is back, trace (senior, 2026-11-21)

- `WARN card132: mismatch BP_PlayerPresence_C BP_PlayerPresence_C_8 triggers host 0001 client 0000` at client
  168602075, round 4. `_8` is a once-enter presence at (-13641, -7069, 860).
- The host mounted the Door Gunner at 168577302. At 168577363 the host's trooper entered `_1033` on the host, and the
  client's copy of it, `BP_PlayerCharacterMI_C_2147469714`, entered `_1033` at 168577377. The two sides agree there.
- At 168577550 the client copy entered `_8` (gate passed, OnTriggerEntered ran, and the once-enter disable cleared
  the byte). After that, begin-overlap rows by the same copy came about every 16 ms. The host has no `_8` overlap at
  all, so the host's trooper never entered it.
- At 168577682 card 132 enforced the host's enabled state, and at 168577695 the copy entered again and consumed it
  again. The client held 0 until the digest.
- The host's trooper collision is not turned off at the mount (no card098 hide or collision row for the trooper), so
  the two copies are at different places. The client copy also re-enters every frame, which means it moves in and
  out of the box each frame. Most likely, attachment replication to the seat and the trooper's own replicated
  movement place it at two different points.
- Diagnostic in the next build: the card245 overlap row now prints the body's location and its attach parent actor.
  The host's copy of the trooper has no row there, so compare the client row's location with the seat row
  (card271/125) and the host trooper's location.
- Fix direction (class, after the data): on the client, a presence overlap by another player's body copy does not run
  the trigger. The host's real body decides, and its X row carries the state. The client's own body still runs it.
  Check before the fix: what the client-side reactions of a presence entered by the host's body are for (local effects
  that must still play), because the host's X row only carries the trigger state.

## c588b416a (b416): the card245 location rows show the cause (senior, 2026-11-21)

- In this run the client rides the seat. Its own trooper `BP_PlayerCharacterMI_C_2147469356` is attached to
  `BP_MountableTurret_Gunner_C` (the attach parent column reads the seat on every row while riding).
- Every frame (about 16 ms apart) the client gets two kinds of begin overlap from that one trooper:
  1. `_1033` at the right world place along the flight, for example (-15193, -36567, 3350), then (-15210, -36559,
     3342), and so on.
  2. `_8`, `_15`, `_23` and `_3`, all at (-158, 0, z), where z falls -70, -78, -86, -94, -103, -122, -133, -154 frame
     by frame. That is the seat-relative attach offset read as a world place, with gravity added each frame. So on the
     client the trooper's capsule spends part of each frame at the map origin, falling, and then attachment puts it
     back on the seat.
- `gate passed` rows on `_8`, `_15` and `_23` mean the client ran the once-enter trigger locally from the origin
  place. That consumes it, and the reactions ("script broadcast, reactions") play on the client only. This is the b414
  `_8` mismatch, and the every-16-ms re-entry seen there.
- The host's copy of the same trooper (`_2147478600`) is attached too. It has only right-place rows (`_1033` and
  `_23` at about z 3420) and no origin rows. So the bug is client-side: an attached player character's movement still
  runs as if unattached (CMC in falling, moving the capsule in world space from the relative offset).
- This covers both cases: the client's own trooper when the client rides (this run), and the client copy of the host
  trooper when the host rides (b414).

### Next step (static, before any fix)

- Find what MountTurret (detour_mount_turret) does to the seated trooper's CharacterMovement on the host (movement mode
  None, or the component deactivated), and whether that state reaches the client. The host copy does not fall, so
  something on the host stops it.
- Find which client path moves the attached capsule: SimulatedTick / SimulateMovement on a SimulatedProxy, or the
  own CMC after OnUnPossess. The sample shows z accumulating from -70, so state is kept between frames.

### Fix direction (class)

- Client: while a player character's root is attached to a turret, its CMC does not move it. Apply the host's
  movement mode, or skip the simulation for an attached root. Prediction: no card245 rows at (-158, 0, z) and no
  client-only `gate passed` on `_8`, `_15` or `_23` during a ride, for either rider.

## Static trace result and fix spec (senior, 2026-11-21)

- `ASTMountableTurret::AttachCharacterToTurret` (RVA 0x051BD3A0) does, in order: `CharacterMovement` (+0x330)
  `StopMovementImmediately` (vfn 0x4F8), `SetArmsVisbility(false)`, three `SetGameplayTagCount(tag, 1)` on the ability
  component (+0x680), `SetActorLocation`, `AttachToActor(turret)`, a relative rotation, and `UnequipCurrentWeapon`. It
  never sets the movement mode to None and never deactivates the CMC.
- Host: the trooper stays still because `MountTurret` then possesses the turret, so the trooper has no controller and
  the authority CMC tick skips `PerformMovement` (`bRunPhysicsWithNoController` is false). Nothing replicates that
  state; the host's `ReplicatedMovementMode` stays Walking or Falling.
- Client: the trooper is a SimulatedProxy in both cases (the host's trooper always; the client's own trooper after the
  host's `OnUnPossess` drops the autonomous role). `SimulatedTick` runs `SimulateMovement` with no controller check and
  no attach check. Walking finds no floor at the seat, switches to Falling, adds gravity every frame, and moves the
  capsule, which then overlaps the presences at the wrong place before the attachment update puts it back. An
  autonomous proxy without a local controller would not move, so `SimulateMovement` is the only path that fits the
  falling z.
- Fix (class, client): in `detour_simulate_movement` (enemies.cpp), before the original, skip the call when the CMC's
  owner (`kCmcCharacterOwner`) has a root (`kActorRootComponent`) with an attach parent (`c140_attach_parent`). This
  covers any attached character, because an attachment always owns an attached root's transform. Test the attach
  parent first (one load), so the unattached NPC path costs one compare more. Count the skips per map and write one row
  per actor the first time it is skipped, capped at 16 rows. Add a map-end row: `card581: client attached characters
  not simulated: N calls, M actors`.
- Prediction for the C3M1 seat ride run: no card245 rows at (-158, 0, z) during the ride for either rider. No
  client-only `gate passed` on `_8`, `_15` or `_23`. The map-end row shows skips during each ride (about 60 per second
  of ride per attached trooper) and 0 outside rides. The trooper is back on the ground and moving after `LeaveTurret`.

## b421 (`b21991c7bfbe72b2`) and run c587b421a: partial

- The skip fires for both riders. Client rows: t=171763888 "BP_PlayerCharacterMI_C attached to CollisionCylinder:
  SimulateMovement skipped (first skip, actor 1)" (host seat ride) and t=171808148 (actor 2, client seat ride). The
  parent prints as the turret's root component (CollisionCylinder), not the turret actor name.
- The fall is gone: 0 client card245 rows at (-158, 0, z). c588b416a had 70.
- Still open: 28 client card245 "gate passed" rows on `_8`, `_15`, `_23` (and `_3`) from t=171764027 to 171764473,
  with the overlap at (-115, 0, 0), attached to BP_MountableTurret_Gunner_C. That is the first 0.6 s of the host seat
  ride. The overlaps still happen on the client, just at a fixed spot instead of a falling one.
- Not proved: the map-end "not simulated" row did not appear (the client quit before a map load). The walk after
  LeaveTurret is unproved because the scenario's ClientRestart wait ran with a 1 s timeout after a regex failure, and
  the following `client goto` returned "no path".

## b422 (`2ad394a93d9e34bf`) and run c587b422a: gate fix holds

- c587b421a showed the client turret copy matched and placed at t=171764025 (card271 row), about 140 ms after the
  first skip. The gate rows came while the seat copy was being placed, as the senior guessed.
- Fix: `detour_c245_overlap` (`objectives.cpp`) rejects, on the client, a begin overlap by a player body whose root
  has an attach parent (card 220 prompt triggers stay per machine). `c581_note_reject` (`enemies.cpp`) counts the
  rejects, logs the first 8, and adds them to the map-end row. The first-skip row now names the seat actor.
- c587b422a: first-skip rows at t=172342331 (host ride, actor 1) and t=172381455 (client ride, actor 2), both
  "attached to BP_MountableTurret_Gunner_C". 8 reject rows (the row cap) at t=172342487 to 172342499, all in the
  host-ride window. 0 client "gate passed" rows on `_8`, `_15`, `_23` or `_3` during either ride (passes only before
  the rides). The prediction holds.
- The client got its trooper back at t=172416471 (client-input SetPawn to BP_PlayerCharacterMI_C; no ClientRestart
  row on this path). The trooper then walked: goto moved it 381 uu in 2 s before sticking on geometry at the seat's
  landing spot. No card581 skip after the seat left. The map-end row was missed again because the strict
  ClientRestart wait failed the group. Rerun c587b422b fixes the waits.

## Run c587b422b (b422): all predictions met

- First-skip rows: t=173110728 (host ride, actor 1) and t=173285863 (client ride, actor 2).
- Map-end row: "card581: client attached characters not simulated: 4198 calls, 2 actors; presence overlaps rejected:
  6877". The two rides ran about 35 s each (host 173110728 to 173145762, client 173285863 to 173320934); 70 s at 60 fps
  is about 4200 calls, so there were no skips outside the rides.
- 0 client card245 "gate passed" rows on the turret presences during either ride. The one pass in the host-ride window
  (t=173145729, `_12`, "attached to none") is the trooper landing after the detach.
- Rejects: 6877 over both rides, about 100 per second of ride. The begin overlap fires again every frame while the
  attached body stays in the volume, because the reject skips the original handler. The cost is the early-out only.
- After the client seat the trooper was back (card490 MI row at t=173320934) and walked: goto moved it under steering,
  then stuck on geometry by the host (the scenario's ClientRestart and SetPawn waits hit the diag cap, lesson 72).
- Open: none for the gate. The card can close when the senior accepts the reject rate.

## Closed (senior, 2026-11-21)

Status: closed 2026-11-21 on b422 `2ad394a93d9e34bf` (c587b422b). The senior accepted the reject rate: about 100
early-outs per second of ride costs nothing.
