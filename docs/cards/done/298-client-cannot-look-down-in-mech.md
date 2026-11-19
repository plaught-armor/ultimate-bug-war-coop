# 298 The client cannot look down in the mech

Playtest 2026-11-13 (user plays as client, pilots the M7 Razorback on FED_C2M1): "can't look down in mech as
client but host can".

## Trace (client log `UBWCoop-menu-20261113-184603.log`)

| Line | Client t (ms) | Row |
|---|---|---|
| 62136-62141 | 257272486 | after `OnRep_SpawnedAttributes` on `BP_BaseMech_C`: `CameraPitchMax`, `CameraPitchMin`, `CameraYawMax`, `CameraYawMin`, `CameraFieldOfView`, `CameraLookInputMultiplier` all base 0.000 current 0.000 |
| 62146-62152 | 257272487 | `card018: client camera re-init AddAttributeSetFromData(PlayerCameraData_C)`: `CameraPitchMax` 89.900, `CameraPitchMin` -89.900, `CameraYawMax` 359.000, `CameraYawMin` 0.000, FOV 90.000, multiplier 1.000 |
| 62142 | 257272486 | movement re-init uses `PlayerMovementData_Mech_C` (WalkSpeed 750) |
| 27 `shot c ... w=BP_Weapon_MechRazorback_C` rows | 257278033 on | shot pitch between -19.25 and 3.59 in all 27; none lower |
| 62170 | 257272493 | `MechEnter after: ... weak PC null` (card 297) |

The camera attribute set after the mod's re-init allows -89.9, so the attribute limits are not what stops the
view. The lowest pitch the client reached in 27 mech shots is -19.25.

## Cause

Not settled. Candidates, to be decided in step 1:

1. The mech's pitch limit is not the attribute pair. The game applies it in its enter path (the
   PlayerCameraManager's `ViewPitchMin` and `ViewPitchMax`, or a clamp in the mech's look input handler), and
   the client keeps a value from another state or the host's value for the host's own camera.
2. The camera data the re-init reads from the pawn definition is the trooper's `PlayerCameraData_C`; the host's
   mech may get a different set or a gameplay effect that changes the pitch limits, which the client never gets
   (the attributes do not replicate: `CPF_Net 0`).
3. Same root as card 297: the mech's controller reference is null on the client, and the mech's own look code
   (cockpit and weapon pitch) reads the pilot through it.

## Step 1 (static, junior, no run; host log after the playtest)

1. Name the function that turns look input into mech view pitch and every clamp on the way (attribute pair,
   camera manager limits, mech blueprint clamp, weapon elevation limit). Give the limit values on the host's own
   mech.
2. Say which of them the client's enter (cards 075 and 255) does not set, or sets from the wrong source.
3. From the host log: the host's mech camera audit rows and its mech shot pitch range, against the client's
   -19.25 to 3.59.

Write the answers into this card before coding.

## Fix

The client's mech view has the same pitch range as the host's own, set by the same game call the host's enter
runs. On exit the trooper's limits come back by the game's exit call. No hand-written limit values.

- Rows: `card298: <role> t=<ms> mech enter view limits: pitch <min> to <max>, yaw <min> to <max>, source <name>`,
  `card298: <role> t=<ms> mech exit view limits: pitch <min> to <max>`,
  `WARN card298: client mech pitch limit <min> differs from the host row <min>`.

## Sibling sweep (same class: a view or input limit set by an enter path the client skips)

| Item | Check |
|---|---|
| Mech yaw range and turn rate | same as the host |
| Mech FOV, zoom on secondary ability | same |
| Turret pitch and yaw limits (card 029) | same |
| Trooper limits after mech exit, after turret exit | restored |
| Downed view (card 043), dropship ride, bug form (card 208) | pitch range equals the host's in the same state |
| After respawn in place or reconnect while piloting (card 045) | limits set again |

## Predictions (one run, after the playtest, batched with 297)

- Client enters the mech and looks fully down then fully up: `card298` enter row equals the host's row for its
  own mech; a mech shot at full down has pitch at the limit (within 1 degree); zero `WARN card298`; client
  screenshot at full down shows the ground at the mech's feet.
- Exit: `card298` exit row shows the trooper's -89.9 to 89.9.

## Senior trace (both logs, 2026-11-13): two candidates refuted, cause not settled

- Candidate 2 refuted. The host's mech gets its camera set from the same `PlayerCameraData_C`
  (host 83223765: `AddAttributeSetFromData(PlayerCameraData_C)`, camera sets 0 to 1), so the client's values
  after the re-init (pitch -89.9 to 89.9, yaw 0 to 359, FOV 90, multiplier 1) equal the host's.
- The shot pitch range is no evidence of a limit: the host's own 27 mech shots span -19.72 to 3.98, the client's
  27 span -19.25 to 3.59.
- Fact for step 1: on possession the client's camera set read 0 for all six attributes (257272486) and the mod
  wrote the values 1 ms later with `AddAttributeSetFromData`. Anything that copies the pitch pair once, at
  possession or in the game's enter path (camera manager `ViewPitchMin` / `ViewPitchMax`, a mech look clamp),
  copied 0 or never ran on the client. The trooper takes the same re-init and looks down, so the copy, if one
  exists, is on the mech's enter path, which the client does not run (`weak PC null`, card 297).
- Sibling fact: of the mech's six attribute sets the client re-inits three (movement, camera, weapon owner).
  `STAttributeSet_CommonCharacterAttributes` and `STPlayerMechAttributeSet` (fuel: `MaximumFuel`,
  `GetCurrentFuel`) get no re-init on the client's own mech. Step 1 says whether they replicate (CPF_Net) and
  what the client's mech reads for them.

Step 1 stays as written (disassembly of the look input path and of `EnterMech`, no run). Add to it: one row per
mount on both sides, `card298: <side> mech view limits: camera manager pitch <min> to <max>, attribute pitch
<min> to <max>, control pitch <deg>`, and once per second while piloted the lowest control pitch reached. No fix
code before the clamp is named.

## Step 1 answers (static, junior, 2026-11-13; partial: clamp located, client value not yet read)

- **Class chain.** `ASTPlayerMechBase` -> `ASTPlayerCharacterMI` -> `ASTPlayerCharacter` (super arguments of
  `GetPrivateStaticClass` at 0x05087B20 and of `ASTPlayerCharacterMI`'s). Every `ASTPlayerCharacter` cast on the
  camera path passes for the mech.
- **The clamp.** `APlayerCameraManager::ProcessViewRotation` calls `LimitViewPitch` (0x040511D0) with
  `ViewPitchMin` (CM+0x27AC) and `ViewPitchMax` (CM+0x27B0). The ctor sets -89.9 and 89.9. No mech-specific look
  or pitch function exists in the PDB publics (only `Add/RemoveMechInputContext`, `HandleInputExitVehicle`).
- **Writers of CM+0x27AC/0x27B0 (complete xref list):**
  1. `USTAttributeSet_PlayerCamera::UpdateCharacterCameraPitchMin` (0x051E9820) and `...Max` (0x051E9790): owner
     actor cast to `ASTPlayerCharacter`, then `Character+0x2D8` (Controller) cast to `ASTPlayerController`, then
     `PC+0x360` (camera manager); writes the value passed in. Any null or failed cast: no write, silently.
  2. Their only callers: `USTAttributeSet_PlayerCamera::InitFromData` (0x051CC2C0: base init, then each
     `Update*` with the attribute's current value) and `PostAttributeChange` (0x051D81E0).
  3. `ASTPlayerCharacter::ResetLookPitch` (0x051DA9C0): same Controller/CM gate, copies both limits from the
     camera manager class's default object. Called only from `ASTPlayerCharacterMI::ActorFullyLoaded` and
     `ASTPlayerCharacterBug::ActorFullyLoaded`.
- **What the client's mech enter skips.** Both writers need `Controller` (Character+0x2D8) valid at call time.
  The client's camera re-init (card018 `AddAttributeSetFromData(PlayerCameraData_C)` at 257272487) runs from
  `OnRep_SpawnedAttributes`, 1 ms after the set arrived and before the client sees the possession (card 297:
  `weak PC null` 6 ms later). If `Controller` is still null there, `InitFromData` writes nothing to the camera
  manager, and the attribute values never change again (`CPF_Net 0`, no `PostAttributeChange`), so the limits
  stay whatever the camera manager held before. The host's set is added after possession and writes them.
- **Not settled statically:** the value the client's camera manager holds when piloting (a down-limit near 0
  matches "cannot look down, can look up"). The mount row the senior asked for reads it directly:
  `card298: <side> mech view limits: camera manager pitch <CM+0x27AC> to <CM+0x27B0>, attribute pitch <min> to
  <max>, control pitch <deg>`, plus the `Controller` pointer at the re-init.
- **Fix shape (unchanged rule, game call only):** after the client's possession of the mech is seen
  (`Controller` valid), call `USTAttributeSet_PlayerCamera::InitFromData` path again, or the two `Update*`
  calls with the set's current values; on exit the trooper's own path (`ActorFullyLoaded` / its set) restores
  them. No hand-written limits.
- **Sibling sets.** `STAttributeSet_CommonCharacterAttributes` and `STPlayerMechAttributeSet`: not yet checked for
  `CPF_Net`; open.

## Senior verdict 2026-11-13: mechanism located, value not read, not released

- Accepted: the only writers of CM+0x27AC/0x27B0 need a valid Controller, and the client's camera re-init runs at
  `OnRep_SpawnedAttributes` before possession is seen. That predicts the limits stay the trooper's (-89.9 to 89.9
  from the ctor, or the trooper set's), which by itself does not give "cannot look down": the cause is not settled.
- The mount row decides: camera manager pitch limits, the mech set's attribute pitch, Controller at the re-init. If
  the limits are the trooper's and the client still cannot look down, the clamp is elsewhere (the mech's own aim or
  the cockpit control rig) and this fix shape is wrong.
- The row goes in the next build as diagnostic only; `CPF_Net` of the two sibling sets is finished statically first.

## Junior answer for b8: CPF_Net of the two sibling sets (2026-11-13, static, exe reflection tables)

Read from the shipping exe's generated property parameter tables (each `FPropertyParamsBase` starts with the name
pointer, the RepNotify name pointer, then `EPropertyFlags`). The method is checked against a runtime row:
`STAttributeSet_PlayerCamera.CameraPitchMin` reads `0x0020080000000015` in the table and in the client's card018
audit row, and a `Health` table entry reads `0x0010000000000025` (CPF_Net set, the `UADAttributeSetHealth` row the
card020 audit reports as replicated).

| Set | Property | Flags | CPF_Net | RepNotify |
|---|---|---|---|---|
| `USTAttributeSet_CommonCharacterAttributes` | `DamageDealtModifier` | `0x0020080000000015` | 0 | none |
| | `FlinchTakenMultiplier` | `0x0020080000000015` | 0 | none |
| | `StaggerTimeMaximum` | `0x0020080000000015` | 0 | none |
| `USTPlayerMechAttributeSet` | `Fuel` | `0x0040000000010001` | 0 | none |
| | `MaximumFuel` | `0x0040000000010001` | 0 | none |

The other CommonCharacter attributes (DamageDealtMultiplier, Flinch Dealt and Taken modifiers, StaggerTime modifier
and multiplier) share the same generated shape; the three sampled rows are representative. Neither class has a
`GetLifetimeReplicatedProps` or any `OnRep_` in the PDB publics.

So neither set replicates. On the client's own mech both hold what the client's own ASC gave them at
`OnRep_SpawnedAttributes`: the camera set showed that is 0 for every attribute until the mod's re-init. For the
mech set that means `Fuel` and `MaximumFuel` 0 on the client unless a gameplay effect sets them there; for
CommonCharacter, damage and flinch modifiers 0 (multipliers 0 would matter only where the client computes damage or
flinch, which the host owns). Neither touches the view pitch; the card 298 mount row still decides the pitch cause.
Follow-up (not b8 code): the mount row also prints the mech set's `Fuel` and `MaximumFuel` on both sides; if the
client reads 0 and the fuel HUD or a fuel-gated ability differs from the host, the same card020 re-init applies to
`STPlayerMechAttributeSet`.

## Senior read of run `c447b152a` (2026-11-16 18:59, static log read, no new run)

- The `card298 ... camera re-init` row prints `CameraPitchMin 0.00/0.00 CameraPitchMax 0.00/0.00` on the client, but
  it is logged in the same tick (510543274) BEFORE the `card018: client camera re-init
  AddAttributeSetFromData(PlayerCameraData_C)` row. The zeros are the values before the re-init, not after it. Fix
  the row order when `c298_mount_row` is next touched (call it after the re-init), so the row proves the result.
- Camera manager limits read -89.90 / 89.90 on the client in the mech (the trooper's values), at possession and at
  re-init. The host prints no `card298` row (the row is client-only), so the host's mech limits are not on record.
- Open: `STPlayerMechAttributeSet` `Fuel` and `MaximumFuel` read 0.00/0.00 on the client at both points, and no
  re-init exists for that set. Mech abilities are granted by the host (`client GiveAbility(GA_BaseMech_LateralCancel_C)
  = handle -1`, the client's own grant is refused), so a fuel check that runs on the host is not affected. What can
  differ: the client's fuel HUD and any client-side prediction of a fuel-gated ability (`GA_BaseMech_LateralCancel_C`
  has a `ContinuousFuelEffect`).
- Next step (one build, one run, with the next lane 2 batch): print the row on the host too (own mech), move the
  client row after the re-init, and in `c447-c4m1.txt` have the client press the mech dash/sprint key and wait for
  the host's ability row. Prediction if there is a gap: host row shows Fuel above 0, client row 0, and the client's
  dash either does not start or the client shot shows an empty fuel bar. If the client dash runs and the bar is
  filled, the 0/0 is harmless and the card's fuel point closes.
- Playtest check until then: in the mech, does the client's fuel bar show fuel, and does the client's dash work.

## Build b159 `885a2976f01932ac` (2026-11-16, log-only, `build-b5/dwmapi-b159-885a2976f01932ac.dll`)

- `attributes.cpp` `card018_camera_fix`: the `camera re-init` mount row now prints after `log_camera_audit("client
  after camera re-init")`, so it shows the values after the re-init. The early returns (camera set not replicated,
  init data not found) print no row.
- `vehicles.cpp` `c75_receive_enter`: the host prints `c298_mount_row("host enter", mech)` after the MechEnter
  `after` line, for the same mech's `Fuel` and `MaximumFuel`. Host-safe: `g_get_player_controller(pawn, 0)` on the
  host returns the host's own PC, so the camera manager fields in that row are the host's view, not the pilot's.
- `tools/scenarios/c447-c4m1.txt`: after the client's second mech enter, `client key LeftShift 2500 W 200` (sprint
  input tag runs `GA_BaseMech_LateralCancel_C`), shot `c298-mech-after-dash`, try-wait on the host's
  `jump: host ... InternalTryActivateAbility(...) = N, ability GA_BaseMech_LateralCancel_C` row. Pins `885a2976f01932ac`.
- Predictions: (a) host row Fuel and MaximumFuel above 0; (b) client re-init row pitch attributes non-zero; (c) client
  Fuel 0/0: dash does not start (card the `STPlayerMechAttributeSet` re-init) or it runs with a filled bar (point closes).

## Senior read of run `c447b159a` (2026-11-16 19:22, build `885a2976f01932ac`)

Run passed: 84 ok steps, 0 FAIL, 0 EXCEPTION, digest 28509 actors 0 mismatches, WARN classes known noise only.

- Prediction (a) met: host row `card298: host ... host enter` shows mech set Fuel 100.00/100.00, MaximumFuel
  100.00/100.00 (second enter Fuel 83.00/83.00: the host used fuel while it drove).
- Prediction (b) met: client row `camera re-init` (now written after the re-init) shows CameraPitchMin -89.90/-89.90,
  CameraPitchMax 89.90/89.90 on both mounts; the `possession` row 1.1 s earlier shows 0.00. The look-down fix works by
  the rows. The user judges the feel in the playtest.
- Prediction (c) not decided: client mech set stays Fuel 0.00/0.00, MaximumFuel 0.00/0.00 after both mounts. The
  client's LeftShift reached `HandleInputTag_Pressed InputTag.Walk` and `AbilityInputTagPressed InputTag.Walk` on the
  mech's ASC (tag map Num 4, this tag not in the map). The host logged no `HandleSprintChange`, no ability activation
  and no `server-copy GA_Sprint` count for the client in that window (517237090 to 517239200). The step's wait pattern
  (`InternalTryActivateAbility ... GA_BaseMech_LateralCancel_C`) has no host baseline: the host never pressed LeftShift
  in the mech in this scenario, and "not in map" also prints for the host's own Move, Use and ExitVehicle tags, so that
  text does not separate host from client.

Next step (one run): in `c447-c4m1.txt`, while the host sits in the mech, `host mark`, `host key LeftShift 2500 W 200`,
then record which host rows appear (`HandleSprintChange`, `InternalTryActivateAbility`, fuel change in a
`c298_mount_row`). That row set is the baseline; the client step then waits for the same rows on the host. If the host
baseline shows a dash and the client step shows none, card the client mech dash as a new bug (no relay of the sprint
input for a mech pawn) and trace the on-foot sprint relay for the pawn class check before a fix.

Correction (same review, 19:23): the host baseline is in `c447b159a` already. The host's `key W 3000` gave
`HandleSprintChange(3.000) on BP_BaseMech_C: GE GE_Mech_LateralCancel_C` about 2 s into the walk with no LeftShift
(1.000 at enter), and fuel fell 100 to 83. The mech's fast move starts from walking, so the client step
(`LeftShift 2500 W 200`, 200 ms of walk) could not show anything. New step: `client key W 3000`, then a host wait for
`HandleSprintChange(3.000) on BP_BaseMech_C`. Prediction: the row appears (card 075 saw MaxWalkSpeed 750/2250 flips
while the client drove); then only the client's fuel bar (client set Fuel 0/0) stays open, as a playtest check.

## Senior read of run `c447b159b` (2026-11-16 19:34, build `885a2976f01932ac`)

Prediction (c) met: after `client key W 3000` in the client-driven mech the host logs `card019: host server-copy
HandleSprintChange(3.000) on BP_BaseMech_C` and MaxWalkSpeed 750.0 -> 2250.0 (t=517980395). The client's mech fast
move reaches the host. 84 ok steps, 0 FAIL, 0 crash. Left for the playtest: the client fuel bar (client attribute set
reads Fuel 0.00/0.00) and the look-down feel.

### Run `c447b159b` (2026-11-16, b159 `885a2976f01932ac`): client mech fast move works
- Scenario `tools/scenarios/c447-c4m1.txt` lines 109-112: `client key W 3000`, then the hard wait on the host's HandleSprintChange(3.000) row. Met.
- Host log: `card019: host server-copy HandleSprintChange(3.000) on BP_BaseMech_C: GE GE_Mech_LateralCancel_C, set-by-caller Abilities.Sprint.Multiplier = 3.000 ... MaxWalkSpeed 750.0 -> 2250.0` at 517980395 ms (after server-copy (1.000) at 517980262, 2250 -> 750). Host's own baseline in the same run: host-own (3.000) at 517967934, 750 -> 2250.
- Run: 103 s, rc 0/0, EXCEPTION 0/0, digest 3 rounds at 0, no new WARN classes, try-fails 0.
- Still open, playtest check only: the client's fuel bar (STPlayerMechAttributeSet Fuel 0/0 on the client set). Shots `client-c447b159b-c447-client-reentered` and `client-c447b159b-c298-mech-after-dash` show the same bar before and after the walk.

## Fuel trace and fix order (2026-11-16 19:40, senior)

The client's `STPlayerMechAttributeSet` reads Fuel 0.00/0.00 because nothing initialises the set on the client. Card
020 re-inits such sets from the pawn definition's init data, but `kC20ReinitSets` (attributes.cpp:357) lacks this
set. Fix (b160): add it to the list. Predictions for `c447b160a`: client row `card020: client
STPlayerMechAttributeSet re-init AddAttributeSetFromData(...)` on `BP_BaseMech_C`; the `card298 ... camera re-init`
row reads Fuel above 0. A `skipped` row on the mech names the next cause. Open after that: whether the client value
follows the host's (host read 83.00 at the second enter).

## Run `c447b160a` (b160) and b161 order (2026-11-16 19:45)

- Fuel fix works at the first mount: client `card020 ... STPlayerMechAttributeSet re-init ... base 0.000 -> 100.000`.
- Second mount got no re-init at all (no `card018` camera re-init, no card 019/020 rows): look limits, walk speed and
  fuel all stayed at 0 there. Same miss in `c447b150a`, so it is intermittent and older than b160.
- Cause class: `g_c18.fix_pending_asc` is one slot; a later player-ASC `OnRep_SpawnedAttributes` (for example the
  host's trooper) overwrites the mech's key before the client possesses it.
- b161: `card018_slot_step` runs the fix for the own pawn whenever its key differs from `fix_done_asc`; pending slot
  removed. Predictions in the STATE entry of 19:45 and the junior order.
- Still open after b161: client Fuel is the init value (100) while the host holds the real value (83 at the second
  enter). Card 459.

## Run `c447b161a` (b161 `1dbd69c0ee8dba3a`) (2026-11-16 19:58)

Passed, 84 ok, 0 FAIL. Both client mech mounts got `card020 ... STPlayerMechAttributeSet re-init` and `card018 ...
camera re-init` (518850194 and 518877468), 5 camera re-init rows for 5 possessions, host `HandleSprintChange(3.000)`
at 518881092. The second-mount miss is fixed. Left open: the user's feel check of looking down in the mech (playtest),
and the client fuel value (card 459).

## Closed (2026-11-17 11:05)

Closed on measured proofs, under the rule that only hearing waits on the playtest. Client `card298 ... camera re-init`
rows give pitch min -89.90 max 89.90 and Fuel above 0 at every mech possession (c447b161a, c447b173a, c447b177c).
Client shots `client-c447b177c-c298-mech-look-down.png` and `-look-up.png` show the cockpit view pitched down and up.
The client fuel value is card 459. A mech mount in a playtest log with no `camera re-init` row reopens this card.
