# Card 461: FED_C3M2 full ride: released client is flung 5.4 km and takes lethal fall damage

## Evidence (run `c236b163a`, b163, FED_C3M2, FAST=0)

- Host `521353105 card399: host ... release BP_PlayerCharacterMI_C (slot 1): detached 1 ... warp beside the host`,
  `card046 ... kill floor warp 1 ... now (-51200, -21064, 2186), moved 1; Warp frame sent`.
- Host `521353114 srv: correction ... server (-51240.1, -21133.3, 2228.1)`: the server copy is already moving.
- Host `521353115 HumanMovementLand_MS at (-50208, -21390, 1940) ... 95 uu from the remote pawn`: 10 ms after the
  warp the server copy landed 1040 uu away.
- Host `521353115 card114 ... damage 205.000 ... DamageType.Fall`, `hit slot 1 server-copy: damage 205.00 (armour
  100.00, health 105.00)`, `card132: god ... damage 105.0 blocked, Health -5.0 -> 100.0 put back`. Without the test
  god mode the client is down at the landing. Client `card157 ... Armour 100.0 -> 0.0`.
- Both sides `WARN card046 ... warp +0.5 s: BP_PlayerCharacterMI_C at (-46609, -24005, 2037), ... at (-51200, -21064,
  2037): gap 5453 uu`. The first pawn is the warped client pawn; the host did not move.
- The legs run at -18 and -36 degrees, the host yaw is -30: the pawn left along the dropship's heading.
- The user's b86 play on this map: "the client did not land beside the host". Card 399 blamed the dropped warp
  (fixed in b87) and proved it with FAST=1 only. FED_C3M1 and FED_C2M1 full rides show gaps of 80 to 173 uu and no
  fall damage, so only this map's ride shows it.
- `c418b158b` / `c418b157a` gap 62092 uu is a different thing: a scenario `warp` command moved the host inside the
  0.5 s window.

## Cause (static, not yet shown by a row)

`c399_release` zeroes `Velocity`, detaches and warps, but leaves the server copy's movement base. While attached the
character movement still ticks in Walking and its floor check takes the dropship as base. After the warp the pawn is
in the air, the mode goes Walking -> Falling, and `UCharacterMovementComponent::ApplyImpartedMovementBaseVelocity`
adds the base's velocity (the dropship component keeps its flight velocity from the sequence). One sub-stepped tick
at that speed lands the pawn 1040 uu away with fall damage, and the rest of the velocity carries it on. FAST=1 has
no flight, so the base velocity is 0.

## Fix

In `c399_release`, before `c46_warp`, and in the client ride-off edge for the own pawn:

1. Diagnostic row first: `card399: <side> release base <component> of <owner>, base velocity (x, y, z)` from
   `APawn::GetMovementBase` (`kPawnVtGetMovementBase`) and `MovementBaseUtility::GetMovementBaseVelocity`
   (or the component's `ComponentVelocity`).
2. `ACharacter::SetBase(nullptr, NAME_None, true)` (`?SetBase@ACharacter@@UEAAXPEAVUPrimitiveComponent@@VFName@@_N@Z`),
   then zero `Velocity` as now.
3. Diagnostic for one build: after each release, the released pawn's location, velocity, movement mode and base
   every host frame for 600 ms, capped at 40 rows per release. If the cause above is wrong these rows name the
   mover.

Sibling: `c46_move_to` (every warp) zeroes velocity but keeps the base; a warp off a moving base (mech, dropship
deck, lift) has the same fault. Clear the base there, so `c399_release` gets it through `c46_warp`; the seat case
with no host pawn on foot (`left at the seat`) clears it in `c399_release` itself.

## Predictions (FED_C3M2, FAST=0, the `c236` scenario)

1. The release base row names a dropship component with a base velocity over 1000 uu/s.
2. 0 `DamageType.Fall` rows and 0 `god ... blocked` rows within 1 s of the release.
3. `card046 ... warp +0.5 s` gap at most 200 uu on both sides, no `WARN card046`.
4. Client Armour unchanged across the landing (no `card157` Armour drop within 1 s of `ride off`).
5. The client landed shot shows the host beside it.

## Result of b165 (`c236b165a`, DLL `1a92573ebf4a81b5`, 2026-11-16 20:53, senior)

The movement-base cause is wrong. All checked predictions failed.

- Host `522464758 card399 ... release base InstancedStaticMeshComponent of Actor, base velocity (0, 0, 0)`: the base
  is ground geometry, not the dropship. The SetBase clear ran (`card046 ... base ... cleared`) and changed nothing.
- Same faults as b163: Fall 205, god blocked 105, Armour 100 -> 0, `warp +0.5 s` gap 5382 uu on both sides.
- The 40 frame rows: +0 ms at the warp spot (-51200, -21064, 2186), base none; from +24 ms on at rest at
  (-46424, -23545, 2037), velocity 0, walking, base ground. A relocation inside one host frame, not a carry.
- Three positions in 24 ms: the stale ServerMove (timestamp 36.8833, client still at the seat) leaves the server copy
  at (-51240, -21133, 2228); the landing sound is at (-50201, -21392, 1940), 95 uu from the pawn; then the rest spot.
  b163 landed at the same spot and rested 500 uu from this run's rest spot.
- The release frame is the first frame after a 976 ms hitch in which the game also sets up the host's own pawn
  (`Health -1.0 -> 100.0`, weapon slot null -> Morita) and the client gets a fresh host pawn copy: the game's own
  landing / player start flow runs in that frame. Health reached -5 before the god put-back, so a death or fall
  respawn path is a second candidate for the last jump.

Nothing in either log names the mover. Next step (b166, diagnostic only): the SetBase clear is removed; while the
600 ms window is open, every move of the watched pawn's root over 60 uu logs from, to, teleport flag and a 10 frame
stack; the Fall damage row carries its stack. Prediction: three callers show (ServerMove path, mover to the landing
spot, mover to the rest spot). The fix is chosen from those stacks.


## Result of b166 (`c236b166a`, DLL `d88eca03b8b88da0`, recorded 2026-11-16 21:07)

Diagnostic build only; the b165 SetBase clear was removed. Run 114 s, rc 0, no VOID, 0 EXCEPTION.

Stacks resolved against the pdb publics. Every move of the released pawn is the engine's own
`ServerMove_PerformMovement` > `MoveAutonomous` > `PerformMovement`:

1. +8 ms: `PhysWalking+0x708` > `AdjustFloorHeight` > `SafeMoveUpdatedComponent` > `ResolvePenetrationImpl`:
   an 80 uu depenetration. The release warp puts the pawn at host z+100, inside the host pawn's capsule.
2. A later `PerformMovement` starts in `PhysFalling` (mode already 3): sweeps of 967 uu and 186 uu.
3. Fall damage 205 through the GAS damage path (`USTFallDamageAbility`, velocity curve), pawn velocity
   (49182, -12236, 0).
4. About 20 `PhysWalking` > `MoveAlongFloor` sweeps; `warp +0.5 s` gap 6305 uu on both sides.

The client only copies this: `ClientAdjustPosition_Implementation`, then the replay in
`ClientUpdatePositionAfterServerUpdate`.

Reading: the velocity has the direction of seat -> warp spot ((0.969, -0.246) against (0.970, -0.241))
and the size of that 990 uu over one 0.0196 s tick. The mod zeroes the velocity at release and does not
set it anywhere. The velocity appears at the Walking -> Falling switch. Candidate cause:
`UCharacterMovementComponent::OnMovementModeChanged` adds `GetImpartedMovementBaseVelocity()` when the
mode becomes Falling, and at that moment the base is the host's pawn (capsules overlap after the warp),
which the game moved seat -> start spot in that same frame. Not proven.

Next (b167, third and last run of this batch): confirm rows at the first mode 3 move (base component,
its owner, owner velocity, the `bImpartBaseVelocity` bits, host pawn location and velocity), and the fix:
clear `bImpartBaseVelocityX/Y/Z` on the released pawn's movement component before the warp, restore when
it next walks on a floor or at the end of the 600 ms window; same for the client's own pawn.


## Result of b167 (`c236b167a`, DLL `a183768a97f78666`, recorded 2026-11-16 21:18)

Fix in: `bImpartBaseVelocityX/Y/Z` cleared on the released pawn's movement component before the warp, restored
when it next walks on a floor (host +26 ms, client +10 ms; restored bits equal the saved bits 1 1 1).

- Fling gone. First Falling move: velocity (225, -482, 400) on both sides (b166: (49182, -12236, 0)). No
  `DamageType.Fall`, no `god ... blocked`, Health 100.0, no client Armour drop. Rest spot (-51076, -21484, 2039)
  on both sides.
- Prediction (a) failed by its letter: the confirm row names the ground ISM as the base before the switch, with
  owner velocity 0, not the host's pawn. Which base the engine read at the switch is still unproven; the effect of
  clearing the bits proves the velocity came through the imparted base velocity path.
- Prediction (c) failed: `WARN card046 ... warp +0.5 s ... gap 437 uu (limit 200)` on both sides (b166: 6305 uu).
  The warp still lands the pawn on top of the host's capsule (host z+100); it is pushed out 80 uu, falls and
  slides to a spot 437 uu away.

Open: the release warp target. Next build: the ride release lands the client beside the host on the ground
(not on its capsule), so no depenetration and no fall. Card stays open for (c) only.


## Closed 2026-11-16 22:29: run `c236b168a` (b168 `0c097c2488d82959`)

Predictions met on the full FED_C3M2 ride: 0 depenetration rows, release gap about 150 uu (limit 200), 0 `WARN
card046`, 0 fall damage, 0 digest mismatches, 0 EXCEPTION. The side offset travels in 3 new Warp arguments; WarpTo
carries the absolute spot. Open and not this card: the landed proof shot has the host 150 uu to the side and out of
frame; cards 274 / 276 are still unproven on this route (no bugs at the PowerPlant spot).

## Note 2026-11-17 08:50 (run c283b177a, FED_C3M1, not reopened)

First `WARN card461: host ... (slot 1) moved 62 uu since the last frame, (-70878, 17254, 475) -> (-70878, 17254, 413),
with no move row: not through MoveComponentImpl`, 140 ms after the client's ride off. Vertical only, velocity 0
before and after, mode Falling -> Walking on the Landscape: the landing snap of the release, not a fling. Known noise
while the jump is vertical and under 100 uu; a row with a horizontal part or a larger jump reopens this card.
