# 408 Flak and plasma bug buildings run their attack loop separately on host and client

Rewritten 2026-11-16 after the static trace. The first title ("the plasma bug's projectile is never on the client")
was wrong: it came from a run too short for the client's own shot.

## Evidence (run c416b112a, b112 `096b066185f2c0da`, FED_C4M1)

- Host: `card098: host spawn BP_Projectile_Plasmabug_C` at (-42783, -15411, 4130) t=479597115 and
  (-15026, 9062, 5163) t=479597615, Blueprint spawn thunk caller, `placed 0 replicates 0`.
- Client: `card098: client spawn BP_Projectile_Plasmabug_C` at (-39787, -19058, 4130) t=479597509 and
  (-15764, 4390, 5163) t=479598709. Same two buildings, other muzzle places and times: the client's building faces
  another way and fires at another moment.
- Client logs its own `card098: client timeline ... TurnInPlace` rows beside the host's relayed `L` rows, for
  `BP_SatchelBuilding_Flakbug_C` and `BP_SatchelBuilding_Plasmabug_C`: the turn timeline is driven twice.
- The host's shot effect rows reach the client as `card325 ... target miss` (ParticleFX "no component",
  `NS_FX_PlasmaBug_Trail` "spawned at world (component miss)", muzzle flash on a pooled component): orphan host
  effects on top of the client's own shot.

## Cause (bytecode, `BP_SatchelBuilding_Flakbug`; `_Plasmabug` is a child that only overrides `Fire Projectile`)

The building is placed and not replicated, so each side runs the whole state machine:

1. `Fire Burst Trigger Timer`: State 0, `K2_SetTimer('Play Turn In Place', RandomFloatInRange(MinTimeBetweenFiring,
   MaxTimeBetweenFiring))` into `BehaviourTimer`.
2. `Play Turn In Place` (timer): State 1, `SelectRightOrLeftTurn` (`RandomBool` while `CurRightTurns` is inside
   plus or minus `MaxdegreesfromOrigin / 45`, else forced back), turn montage, `Begin Turning Rootmotion`
   (`FromRot = GetActorRotation`, `TurnInPlace` timeline; its update sets the actor yaw `FromRot + alpha` signed by
   `TurningRight`). With no turn allowed it goes straight to step 3.
3. Turn montage notify: `Play Pre-Firing Windup` (State 2), then `K2_SetTimer('Timeout Fire Burst',
   RandomFloatInRange(MinFiringDuration, MaxFiringDuration))` into `FiringTimer`, then `Play Burst Firing`
   (State 3, `ContinueFiringLoop = true`, `FiringAnim` montage; its notify calls `Fire Projectile(InSocketName)`).
4. `FiringAnim` completes: reloop while `ContinueFiringLoop`, else wind down (State 4), Idle, back to step 1.
   `Timeout Fire Burst` only sets `ContinueFiringLoop = false`.

The inputs that differ per machine are the two random timer lengths and the `RandomBool`. Everything else follows
from them. `LoDded` / `LoDTimer` / `Toggle Show SM` (distance to the local camera) is per viewer and stays local.

`BP_Projectile_Plasmabug` is a plain Actor with no hit or damage logic: BeginPlay waits a random delay, spawns the
explosion effect and sound at its place, and destroys itself. Nothing on arrival needs a host-only guard.

## Fix (by class: `BP_SatchelBuilding_Flakbug_C` and every child)

Host drives the two timer events; the client's copy runs the rest of the chain itself, from the host's facing.

1. Client: refuse the timer-fired `ProcessEvent` of `Play Turn In Place` and `Timeout Fire Burst` on actors of the
   class (a `g_c408_pe` filter in `detour_process_event`, shaped like `c161_pe`: armed only while such an actor is
   tracked on the map, pointer compares against the two recorded functions). The client's own timers then do
   nothing.
2. Host: after each of the two events returns on such an actor, send one world row by placed name (same naming as
   the `B` and `L` timeline rows): which event, and for the turn event the actor yaw before the call, `DoATurn`,
   `TurningRight` and the new `CurRightTurns` (read from the properties after the call).
3. Client, on the row: `Timeout Fire Burst`: call it. `Play Turn In Place`: set the actor yaw to the host's, then
   force the host's choice without touching the random call: write `CurRightTurns` to the bound that forces the
   host's direction (at or above `+N` forces left, at or below `-N` forces right, `N = MaxdegreesfromOrigin / 45`),
   call the event with the filter let through, then write the host's `CurRightTurns`. When the host did no turn
   (`MaxdegreesfromOrigin == 0`) the call takes the same branch on its own.
4. Stop the double drive: the host's `L` and `B` rows for these buildings' `TurnInPlace` timeline are no longer
   sent (the client's own chain plays it from the synced `FromRot`). Exempt by owner class in `c98_host_timeline`.
5. Stop the orphan effects: host effect and sound rows whose owner is one of these buildings or a
   `BP_Projectile_Plasmabug_C` / flak projectile they spawn are not sent; the client's own shot makes them. Use the
   existing local-owner test (`trail_local` in `fx_c136_net.cpp` is the model) and name the class list in one place.
6. Death stays as it is (satchel `S` / kill rows); `Become Destroyed` clears both timers.

Also in this build, card 409's diagnostic changes (see that card).

## Residual, to measure and report, not to fix in this card

Each side's shot has its own spread (`RandomFloatInRange(-2, 2)` on pitch and yaw) and its own explosion delay, so
the cosmetic ball ends at a slightly different point per player. If the run shows it visibly different, the next
step is the card 104 `P` copy with rotation and the client's own spawn refused (`c105_client_blocks`).

## Predictions (one run, FED_C4M1 part of the fast chain)

- Client: 0 `card098: client timeline ... TurnInPlace` rows that are not preceded by a card 408 turn row for the
  same building within 100 ms; 0 `client spawn BP_Projectile_Plasmabug_C` before the first card 408 row.
- Each host `Play Turn In Place` row has one client row with the same building, direction and `CurRightTurns`;
  after the turn the client's actor yaw equals the host's within 1 degree (log both at the timeline finish).
- Client plasma spawn places within 150 uu of the host's for the same shot (same muzzle, same facing) and within
  latency plus 100 ms in time.
- 0 `card325 ... target miss` rows for `NS_FX_PlasmaBug_*` and the flak effects.
- Receiver-side screenshot: one host and one client shot of the same building at the same moment, same facing.
- Gate as always: 0 mismatches, WARN classes unchanged.

## Done when

All predictions hold in one run and the class list (children of `BP_SatchelBuilding_Flakbug`) is written here.

## Correction 2026-11-16 (run c408b113a, b113 `5f87a477aaf3ab3b`: not armed on both sides)

Two names above are not class members as written, so the property lookup failed and nothing armed:

- The member is named `MaxdegreesfromOrigin (Rounds Up To Nearest 45)` (int), not `MaxdegreesfromOrigin`.
- `DoATurn` is the out parameter of `SelectRightOrLeftTurn`, held in the ubergraph frame local
  `CallFunc_SelectRightOrLeftTurn_DoATurn`; no class property lookup reaches it. The host derives it: `DoATurn = true`
  is only reached through the branch that moves `CurRightTurns` by one, and `DoATurn = false` leaves it untouched, so
  `turn = CurRightTurns after the event != CurRightTurns before it`.

## Class list (run c408b115a, b115 `d552217f60765a2b`)

Both sides log the same list from `card408: <side> class list`:

- `BP_SatchelBuilding_Flakbug_C` (the base). FED_C3M1 and FED_C4M1 have 3 each (`_C_0`..`_C_2`); FED_C3M2 and FED_C4M2 have none (card 408 not armed there).
- `BP_SatchelBuilding_Plasmabug_C`, a child of `BP_SatchelBuilding_Flakbug_C`. FED_C5M1 has 7 buildings in total:
  Flakbug `_C_0`..`_C_3` and `_C_9`, and Plasmabug `_C_2` at (-41416, -17342, 1262) and `_C_3` at (-15322, 6715, 2295).
  The projectile class `BP_Projectile_Plasmabug_C` is loaded on this map only.

## Senior review of b115 (2026-11-16 09:52): not accepted, four findings

- F1 (junior, next build): `c408_client_row` applies the host yaw and host `CurRightTurns` on turn rows only. Timeout
  rows carry both and apply neither: FED_C3M1 `Flakbug_C_0` host -175.00 own -158.12, FED_C5M1 diffs 1.13, 1.13 and
  0.25, and the 0.25 carried into the next turn row. Rows are not journalled, so a client that arms after earlier
  turns is wrong until its first row. Fix: apply both on every row. Proof: every timeout row logs diff 0.00.
- F2: plasma rounds bursting about 22000 uu apart is a separate cause (random lifetime and spread drawn per machine):
  card 419.
- F3: the plasma screenshots of run c408b115a are invalid (host camera inside rock, client warp did not stick). The
  scenario steps are fixed by the senior; the next run must give valid receiver-side screenshots.
- F4 (junior, from logs): `c136_attack_local` "by owner" and "by ProcessEvent" also match the building's explosion and
  death effects. Show that both sides play them when a flak or plasma building is destroyed, or narrow the test to
  attack effects.

## F1 result (b116 `eaab0a957f07fec3`, run `c409b116a`, 2026-11-16)

`c408_client_row` now applies the host yaw and `CurRightTurns` on every row (turn and timeout). Every client row reads
`after the host yaw set 0.00`, and the map totals on both sides read `(after the host yaw set, any row: 0.00)`. Host
rows 3, 1 and 16 per map; the client applied 3, 1 and 16, missed 0, direction differs 0. The diffs up to 2.26 logged
before the set are the client's own timeline before the row arrives, which the set then replaces.

## F4 (b116)

Static: run `c408b115a` destroyed no building, so it had no death effect to compare. The test also matched
`NS_FX_Weapons_MoritaHit_*` by owner (the building's own Morita hits), so `c136_attack_local` is narrowed to the attack
effects. Run `c418b116b` destroys one building per side to compare the destroy effects on both logs.

## F4 result (run `c418b121a`, b121 `821e36ce8e035086`, FED_C3M1)

One satchel planted per side (`Flakbug_C_0` by the host, `Flakbug_C_2` by the client). Both receiver-side screenshots
(`client-c418b121a-c418-h-plant`, `host-c418b121a-c418-c-plant`) show the destroyed building with its destroy effects.

## The 1.09 timeout diff and F5 (b121, runs `c418b116c`, `c418b121a`, `c399b122a`)

- The stall theory is refuted: `c418b116c` had no stall (rows applied within about 5 ms) and still logged timeout diffs
  of 1.12 to 1.45; `c399b122a` logs one of 2.88 (`Flakbug_C_1`).
- Cause: each side's own turn size varies per turn with the frame rate. Host turns measured +29.24, +28.12, -28.13,
  -29.26, -29.50, +28.12, -29.24 degrees; the client's vary too. The drift builds within one turn on the client's own
  timeline, and the F1 set removes it at every timeout: every row reads `after the host yaw set 0.00` and the next turn
  row reads diff 0.00 (22 and 13 rows in `c399b122a`). Not a defect.
- F5 (b121): a client turn row moves the client's `TurnInPlace` timeline on by the row's age
  (`c132_ledger_frame_age_ms`, at most 2000 ms) through `c313_advance_timeline`. Rows log `row age 15-24 ms, timeline
  at 0.005-0.036` in `c399b122a`. It only shortens the visible lag of a late row; keep or revert is the senior's call.
