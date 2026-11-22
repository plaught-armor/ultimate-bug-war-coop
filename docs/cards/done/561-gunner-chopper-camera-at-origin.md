# Card 561: Door Gunner (gunner chopper) breaks the host's and the client's camera

Playtest 2026-11-20 on b366 (`e62e76f63a05d88f`), FED_C3M1 (resort). User report (client): "gunner chopper cause
camera issue also" (after card 560's guided missile camera bug). The user then left the session (client goes to the
menu at 103436778).

## Client log (`UBWCoop-menu-20261120-184517.log`, lines 105736-106000)

| Client t (ms) | Row |
|---|---|
| 103422746-103422762 | SetPawn / AcknowledgePossession / ClientRestart / OnRep_Pawn of `BP_MountableTurret_Gunner_C` |
| 103422787 | `card125: client rider BP_PlayerCharacterMI_C arms hidden on its turret` |
| 103422789, 103422810 | audio rows: listener 2236 and 2813 uu from sounds at the fight (about (-59000, -18000)): camera still in place |
| 103422902 | `card125: client turret BP_MountableTurret_Gunner_C at (-53996, -22679) ran its possessed graph` |
| 103423426 | `card416: client ring slot 1 on BP_PlayerCharacterMI_C ... blip slot 1 added (markers 2 -> 3)`: a second player tag while only one remote player exists |
| 103423822 onward | listener distances fit a camera at the world origin: 53032 uu from (-20550, -48880) (origin distance 53024), 62012 from (-59251, -18161) (61972), 53028 from (-48698, -20875) (52983) |
| 103427375 onward | "-1 uu from the nearest remote pawn": no remote pawn found |

There are no card125 aim rows (`row applied`) for the gunner turret, only for `BP_MountableTurret_C`.

## User follow-up: the host's view broke too

"when i the client got into the chopper it gave both the host and i camera issues, they got a black screen which was
probably us looking at the texture/model of the choppper". The client's seat entry changed the HOST's camera as well.

Strongest lead (static, to confirm): the seat's possess graph (`ExecuteUbergraph_BP_MountableTurret_Gunner` entry
3439) sets the camera through `GetPlayerController(0)` or a view target call, a single-player assumption. On the host,
index 0 is the host's own controller, so the client's possess moves the host's view into the chopper model (black
screen). On the client, card 125 replays the same graph ("as the host's Possess did"), which may move the client's
view the same wrong way. Trace: list the graph's camera calls (SetViewTarget, SetViewTargetWithBlend, camera component
activate) and which controller each one uses; check the host log at the client's seat entry for a view target change on
the host's controller.

Fix direction for this part: the camera change applies only to the controller that possessed the seat, by class for
every seat and vehicle graph that picks a controller by index.

## Static trace (2026-11-20, `build/kis561/`, kis.py listings of `BP_MountableTurret_Gunner` and `GA_TacSupp_DoorGunner`)

The "gunner chopper" is the Door Gunner tactical support. There is no chopper actor: the seat
(`BP_MountableTurret_Gunner_C`) is itself the flying gunship, moved every tick along a spline actor.

Ability (`GA_TacSupp_DoorGunner`, runs on the host):
- 204-335: spawns the path (`BP_Spline_DoorGunnerPath`) at the approach point.
- 723-1235: deferred spawn of the seat at the caller, sets its exposed variables `Spline`, `Target` (the caller's
  trooper), `AimLoc`, `PlayerLoc`, then `FinishSpawningActor`.
- 30: after a latent step, `imp36(Target)` mounts the caller into the seat (the possess).

Seat graph (`ExecuteUbergraph_BP_MountableTurret_Gunner`):
- BeginPlay (3641 -> 3463 -> 3336): `GetPlayerCameraManager(self, 0).SetManualCameraFade(1.0, black, true)`: screen
  fully black on player index 0.
- ReceivePossessed (3439 -> 84): `GetPlayerCameraManager(self, 0).StartCameraFade(1.0, 0.0, 5.0, black, true,
  false)`: fade in from black on player index 0, then `Move to Orbit` (FlightStage 0).
- ReceiveTick (2316): FlightStage 0 moves the seat along `Spline.SplineEntry`, 1 orbits on `Spline.SplineCircle`, 2
  flies away and destroys the actor. Each step is `K2_SetActorLocationAndRotation(Spline...GetLocationAtDistanceAlongSpline(...))`.
- Add/RemoveSignificanceLocation: `GetPlayerController(self, 0)`.
- ReceiveUnpossessed (263): `Fly Away`, then the caller's trooper is teleported back next to `PlayerLoc`.

### Cause on the host (fits "they got a black screen")

Every camera call uses player index 0. On the host that is always the host's own camera manager. When the client
calls the Door Gunner, the host's camera goes fully black at the seat's BeginPlay and fades back over 5 s at the
possess. The host's screen goes black even though the host is not in the seat. If the possess runs before BeginPlay,
or the fade-in is lost, the host stays black. The host log decides which.

### Cause on the client (fits the camera at the world origin)

`Spline` is an exposed spawn variable set only on the host. The client's copy of the seat has `Spline` null. Card 125
turns the client copy's actor tick on at the possess ("actor tick on, as the host's"). Its ReceiveTick then runs the
flight on a null spline, `GetLocationAtDistanceAlongSpline` returns (0, 0, 0), and the copy moves itself to the world
origin every frame. The client's view is the seat's camera, hence the listener at the origin from 103423822. The
client's black/fade calls use index 0 = the client's own camera manager, which is correct there.

### Fix direction (class)

1. Camera fades and significance calls in this seat (and every seat or vehicle graph found by a sweep for
   `GetPlayerCameraManager(self, 0)` / `GetPlayerController(self, 0)` in a possess or BeginPlay path) apply only to
   the controller that possesses the seat. On the host, a seat possessed by the client's controller makes no change to
   the host's camera. The client applies its own fade for its own seat.
2. The client copy never runs the seat's flight tick: the host moves the seat, and its transform reaches the client
   through replicated movement or a card 125 row. Whether the client copy needs its tick on for anything else (aim)
   is part of the trace; if it does, only the flight step is skipped.
3. Sibling sweep: every host-spawned actor whose BeginPlay or tick reads an exposed spawn variable that only the host
   sets (spline, target, aim point), on client copies.

## Candidates before the static trace (kept for the record)

1. The gunner seat rides the chopper (an attached, moving actor). On the client the seat's attach parent is missing
   or not relevant, so the seat and its camera fall back to the relative offset, which sits near the world origin.
2. The seat's camera view target is the chopper actor, and the client has no live copy of it.
3. Same class as card 560: the client's own trooper, unpossessed while seated, is taken for a remote pawn. The card416
   ring and blip at 103423426 point that way. Card 028 skips it only through the card 125 attach-parent check, which
   needs the trooper attached to the seat.

## Trace (after the playtest)

- Static: how `BP_MountableTurret_Gunner_C` attaches to the chopper, and what the chopper class is and whether it
  replicates (the log names no chopper class).
- The host log for the same time: where the host's seat and chopper are.
- Card416's remote test next to card028's `c28_remote` (`remote_body.cpp:689`): one owner-based test for both (card
  560 fix 1).

## Fix direction

The seat and its chopper stay in the same place on the client as on the host, by class for every seat on a moving
actor. The client's own trooper never gets a remote tag, ring, blip or body (card 560).

## Prediction

Client takes the gunner chopper seat: the host's view target does not change, and the client's listener stays within a few thousand uu of the chopper's host position the
whole ride, no card416 add row for the client's own trooper, and the view returns to first person on leaving the seat.

## Host log (`ultimate-bug-war-coop-public/logs/UBWCoop-menu-20261120-184651.log`, host t = client t + 241024698)

| Host t (ms) | Row |
|---|---|
| 344444912 | the client takes the Door Gunner pickup (relayed Use #53) |
| 344445490 | `TacSupport slot 1` from the client, run on the server copy |
| 344447435 | `GiveWeaponByClass(TurretMorita) on BP_MountableTurret_Gunner_C (host-own)`, ability `GA_TacSupp_DoorGunner_C` |
| 344447464 on | `WARN card490: host pawn class BP_MountableTurret_Gunner_C: Stamina at -1` every frame (card 562) |
| 344447571 on | card128 `TurretAim after the gap` rows: the client's aim reaches the host seat |
| 344458410 | `card043: host own view downed`: the host's trooper is downed 11 s after the seat spawned, alone in the fight |
| 344458414-344458604 | `card081: own interact trace hidden: Downed` and `restored: is Up, trace found BP_MountableTurret_Gunner_C` in the same frame, every frame (card 563). The host's own interact trace hits the seat: on the host the seat sits within interact range of the host's camera 11 s after it spawned |
| 344461525 | the client leaves to the menu: `card447: BP_PlayerController_C leaves the game in turret BP_MountableTurret_Gunner_C`; 344461532 seat destroyed |
| 344461540 | `card043: host-own -> Dead (all down: last teammate left (card 366))`: the downed host dies, mission lost |

There are no fade rows (no hook), so the black screen is not logged directly. What the host log adds:

- The seat on the host was still at the host's position 11 s after spawning (the host's interact trace hit it). The
  host camera inside or behind the seat's gunship model fits "they got a black screen which was probably us looking at
  the texture/model of the chopper". Whether the host's seat ever flew (ReceiveTick on the host, `Spline` set) is to
  trace: the fade cause above and a seat that never left the ground can both be true.
- The host, blind, was downed and then died when the client left: the black screen cost the mission.

Prediction addition: the host's interact trace never finds `BP_MountableTurret_Gunner_C` after the seat spawns, and a
host-side position row for the seat shows it on the spline (entry, then orbit).

## b369 (`4d7adc330945a079`, build-b5 copy kept), not run yet

Fix, two parts (`turret_sync.cpp`):
- Camera fades: hooks on the exec thunks of `StartCameraFade` and `SetManualCameraFade` note the calling pawn from the
  Blueprint frame. The native fades skip while that pawn's controller is not this machine's own player controller
  (no controller, a remote PC, or AI). The host no longer goes black when the client takes a seat, and the client no
  longer goes black when the host takes one. Proof row: `card561: ... skipped StartCameraFade`.
- Client copy tick: `c125_mirror_tick` leaves the actor tick off for a seat whose `Spline` variable is null on the
  client copy (only the host's spawn sets it), so the copy's tick no longer flies the seat to the world origin. Proof
  row: `card561: client turret ... keeps its actor tick off`.
The host's own seat flight is still to be checked in the next run.

### Run c560b371b predictions (c560b371a crashed at 60 s, host still in the dropship; same predictions) (junior, 2026-11-21, b371 `baeebee09d3e7f59`, scenario `tools/scenarios/c372-c3m1.txt`)

Host seat first, then client seat.
- Host seat: the host's own fades run (no card561 skip row on the host for its own seat). The client logs
  `card561: client t=... skipped StartCameraFade|SetManualCameraFade` and `card561: client turret ... keeps its
  actor tick off`. Shots `c372-561h-{2,8,20}s-host` show the seat moving (position differs between shots, not a
  black frame after the 5 s fade). Client shots stay on its own first person view.
- Client seat: the host logs `card561: host t=... skipped StartCameraFade|SetManualCameraFade`, its shots
  `c372-561c-{2,8}s-host` are not black. The client logs the tick-off row, its shots show the chopper view, not the
  world origin.

## c560b371b result and b402 regression fix (senior, 2026-11-21)

- Run steps 63 and 96 failed (no `GiveWeaponByClass(TurretMorita)` on either seat), so neither seat was taken and
  the seat predictions are not proved. The fade skip and tick-off rows did not run for a seat.
- Regression found: the fade rule skipped every fade of the transport dropship on the host, because the dropship is
  a pawn with no controller. Every lane 2 run since b369 logs it: 478 `BP_PlayerTransportDropship_VerySlow_C` and 48
  `BP_PlayerTransportDropship_C` skip rows across the last 80 logs, filling the 24-row cap, so no seat row could
  show. Its landing fades straddle `DescendToLandCompleted` (rows at 142724327, detach at 142724366, more after), so
  the host lost the drop fade the single-player game plays. Lane 1 (b364) does not carry card 561.
- Fix (source, for b402): `c561_foreign_pawn` returns null for an `ASTPlayerTransportDropship` caller with no
  controller. The class is checked, not the rider attachment, because the rider is detached partway through the fade.
- Prediction: 0 `card561 ... skipped ... from BP_PlayerTransportDropship` rows on both sides; a seat's skip rows
  appear when a seat is taken.
- The seat proof still needs a scenario that gets the Door Gunner seat (steps 63 and 96: the junior's scenario fix).

## Seat proof scenario fix (senior, 2026-11-21)

Why c560b371b steps 63 and 96 failed:
- The host's seat did spawn: `card021: host GiveWeaponByClass(BP_WeaponDefinition_TurretMorita_C, slot -1, equip 1) = 1
  on BP_MountableTurret_Gunner_C (host-own)` at 142824965. The wait regex `GiveWeaponByClass\(TurretMorita\)` never
  matches that row.
- The client logged 0 card561 rows. The host had warped to the pickup about 43000 uu from the client, beyond the
  15000 uu net cull, so the client never got the seat: `WARN card467: client turret row BP_MountableTurret_Gunner_C_...
  no turret copy after 10000 ms, dropped` at 142835144.

`tools/scenarios/c372-c3m1.txt` now uses the real card021 regex at both seat steps (checked against the c560b371b host
log: 1 match). After each pickup the other player is warped within 300 uu of the caller (`host warp client
BP_PlayerCharacterMI_C 300` for the host seat, `host warp actor BP_PlayerCharacterMI_C 300` for the client seat).
The seat proof and the b402 dropship fade check can share the next run of this scenario on b402.

## c372b402a (b402): host seat fade regression, fix in b404 `df68db7777c2191f`

The host seat works: the GiveWeaponByClass row, 0 WARN card467, the gunner view shot and the pawn coming back all
show it. But the host log has `card561: host t=161318685 skipped SetManualCameraFade from BP_MountableTurret_Gunner_C
(controller null)`. That is the host's own seat: GA_TacSupp_DoorGunner on the host-own pawn spawns it at 161318684,
and its BeginPlay black fade runs 1 ms later, before the possess. b402 skipped every unpossessed pawn except the
transport dropship.

b404 class rule: a pawn with no controller belongs to its Instigator, else its Owner. The fade runs when that is this
machine's player controller, or a pawn that controller drives. A client's seat on the host names the client's
server copy, which has a remote controller, so it is still skipped. The skip row now prints the instigator and
owner, so a seat that sets neither shows up in the row.

Prediction for the next c372 run (b404): 0 card561 skip rows naming the host's own seat on the host, and the client
log has skip rows for the host's seat only (instigator = the host's trooper). If a host-own seat row still shows
`instigator null, owner null`, the seat sets neither, and the rule needs the spawning ability's avatar instead.

## c372b404a (b404) result: own-seat prediction failed on both sides

- The seat's Instigator is the seat itself and its Owner is null. The ability spawns it with no owner. So the b404
  rule never reached a player, and both own-seat BeginPlay black fades were still skipped:
  - host: 161953797, own seat
  - client: 161992975, own seat
- Correct skips:
  - client, host's seat: 161953809 and 161953995
  - host, client's seat: 161992964 and 161992981 (controller = the client's PC)
- The seats themselves worked. Card 563 passed (host card081 Downed hidden row at 162001713).

## b406 `7d805beca8a57e0c`: Target rule

`GA_TacSupp_DoorGunner` sets the seat's exposed `Target` variable to the caller's trooper before
`FinishSpawningActor` (ability listing 808). An unpossessed pawn now belongs to its `Target` when it has one. Otherwise
it belongs to its Instigator (when that is not itself), else its Owner.

A new capped row `unpossessed <seat> fade: Target X (property found, net N), player Y, local L` shows on both sides
whether `Target` replicates.

Predictions (c372 on b406):
- host: the own seat's row has Target = the host trooper and local 1, and there is no skip row for it.
- host, client's seat: Target = the client's server copy, local 0, skipped.
- client: if `net 1`, the own seat runs (local 1) and the host's seat is skipped.
  - If `net 0` with a null Target, the client still skips its own BeginPlay black fade. The next step is then a host
    to client seat-owner message, or the possess-time fade only.

## c372b407a (b407) result: host passes, client own seat still skipped

- Host: the own seat's row has Target = the host trooper, local 1, and no skip. The client's seat has local 0 and is
  skipped. Pass.
- Client: Target is null (property found, net 0: not replicated). The own seat's BeginPlay SetManualCameraFade was
  skipped at 162499130, 16 ms before the possession SetPawn at 162499146. The host's seat was correctly skipped
  (162459850, 162460118).

## b408 `e422678fc9a216f7`: the host forwards a client's own pawn fades (ChanOp::CamFade)

Cause: the seat's ReceivePossessed fade-in runs only on the host, because PossessedBy is authority-only. Before the
mod, the client's own seat never faded in on the client. Under b407 the client got neither the black nor the fade-in.
The host is the only side that knows whose seat it is (it holds `Target`, and the possessing controller).

Class rule: when the host skips a pawn's fade, and the pawn's player (its controller, else the `Target`, Instigator
or Owner rule) is a connected client's PC, the host sends the fade to that client. The message is ChanOp::CamFade, a
30-byte blob: kind, flags, three floats, and the color. The client runs the fade on its own camera manager. The
client's own skip rule is unchanged.

Rows:
- `card561: host t=... forwarded <StartCameraFade|SetManualCameraFade> (a, b, d s) from <pawn> to <pc>, N forwarded`
- `card561: client t=... applied host <kind> (...), N applied`
- `WARN card561: host CamFade ... not sent` and `WARN card561: client CamFade dropped`

Predictions (c372 on b408):
- The client's seat call gives 2 forwarded rows on the host: SetManualCameraFade (1.00) at its BeginPlay, then
  StartCameraFade (1.00, 0.00, 5.00 s) at its possess.
- The client log has 2 applied rows in the same order, each within 100 ms of the host row (compare uptime against the
  clock offset).
- The client's gunner view shot goes black, then clears over about 5 s.
- The host's own seat forwards nothing.
- 0 WARN card561 rows.

## Closed (senior, 2026-11-22)

The fix is proven by logs and runs, and the proof is in the sections above. Under the no-playtest-parking rule, a proven fix does not stay open waiting for a play check. It is on the playtest checklist in STATE.md. If the user sees it again in play, reopen this card with that report.
