# Card 599: the server copy PC's camera manager sits at the world origin

## Finding

Run c582b451a (b451, card 582 G3) shows that on the host, the PlayerCameraManager of a client's server copy PC reports location (0,0,0) and rotation zero. The copy's camera was at (-68529,-666,1373).

GA_HumanMelee aims both of its traces from GetPlayerCameraManager(0):

- TraceForTarget, the lunge;
- DealDamage.

Card 099 correctly resolves player 0 to the copy's own PC. But both traces start at the origin and point along +X, so the client's knife never hits anything. No log has ever shown a client melee hit or kill. This is a real gameplay bug: a client cannot knife.

## Fix (one class rule)

On the host, keep the camera cache POV of each live server copy PC's camera manager (location, rotation, FOV) at the copy's view. Use the data the host already has: the camera component location and the control rotation. Write it in the existing per-copy tick or update path.

Do not add a new per-frame detour unless no such path exists. If you need one, early-out on non-copy PCs first (tight detours).

## Sweep

Grep the Blueprint dumps for every caller that reads GetPlayerCameraManager(0) or its GetCameraLocation/GetCameraRotation for gameplay: aim, traces, spawn points, or lunges. Each one that can run for a copy is covered by the same fix. List them here.

## Proof

Fold this into the b452 C3M1 run:

- The copy's SphereTraceSingleForObjects rows (the card 582 diag) start at the copy camera, within 50 uu.
- A card582 door hit row has the copy's pawn as causer.
- One client knife kill on a bug.
- 0 mismatches.

## Fix target, corrected (b452)

The kismet dump of GA_HumanMelee (build/kis_res/GA_HumanMelee.uasset.txt) shows that both traces read
`GetPlayerCameraManager(self, 0)` and then `K2_GetActorLocation()` and `GetActorForwardVector()`. They use the camera
manager's actor transform, not its camera cache. The engine moves that actor only in UpdateCamera, which runs on
local controllers. For a server copy, ServerUpdateCamera_Implementation (RVA 0x0405DCB0, card 347) fills only
CameraCachePrivate, so the actor stays at (0,0,0) with zero rotation. Writing the cache alone would not move the
traces.

## Code (b452, dwmapi 61270761ad74d597)

- `grenade_c158.cpp` `c599_host_step` runs on the host from `c50_step` every frame. For each slot PC whose
  `sprint_side` is server-copy, it moves the PC's camera manager actor to the copy's view with
  AActor::SetActorLocationAndRotation (new symbol, RVA 0x037F25F0; no sweep, teleport physics).
- Source of the view: the camera cache POV (CameraCachePrivate, reflected once; POV.Location +0x10, POV.Rotation
  +0x28) when its Timestamp is above 0. Otherwise the pawn camera location and the control rotation.
- Early-outs: a slot with no copy PC; no cache and no pawn; the same view as the last move (no call).
- Rows: one `card599: host t=... copy ... camera manager ... set to its view from ...` per new camera manager; the
  map totals row `card599: host map totals: N copy camera manager moves (M from the camera cache, K fallback)`.

## Sweep

Blueprint callers that read the player 0 camera manager's actor transform for gameplay. All are covered by the same
fix, because they read the same actor:

- GA_HumanMelee: TraceForTarget and DealDamage (location and forward).
- GA_WeaponMeleeAbility: location and forward.
- GA_TauntAbility: location and forward.
- BP_GuidedMissile: forward vector.
- BP_SatchelBuilding_Flakbug: GetDistanceTo.
- GA_BugTaunt: bug only, out of scope (Trooper first).

Callers that use the camera manager only for fades or cosmetic camera work, not gameplay: BP_CamFadeController,
BP_MountableTurret_Gunner, BP_PlayerTransportDropship, BP_FrontendTrooper_Base, BP_FX_FakeFogSphere_02,
BP_BugGameplayCutscene, FED_C3M1_Scripting.

## Predictions (b452 run c599b452a, scenario tools/scenarios/c599-c3m1-b452.txt)

1. A `card599: host ... set to its view from the camera cache` row appears after the client spawns.
2. The client door melee's card582 trace rows start within 50 uu of the logged copy camera, and the
   `camera manager PlayerCameraManager at` field equals that start, not (0, 0, 0).
3. A card582 `building hit BP_Door_Sliding_Gigantic_C...` row has instigator `BP_PlayerCharacterMI_C ...
   (server-copy)`, and the client plays MS_EMIT_Human_Building_Generic_Damaged (card455).
4. Knife kill: a client melee trace row has hit 1 on BP_AI_WarriorBug_C, followed by that bug's death (card104
   Death start or rest row) while the host is not firing.
5. The host melee rows are unchanged, with the host camera at the trace start.
6. 0 mismatches. No new WARN class.

## b452 run c599b452a: result (scenario passed, exit 0, 2026-11-21)

Host log `UBWCoop-host-20261121-234541.log`, client log `UBWCoop-client-20261121-234542.log` (lane 2 Win64).
Drive log `logs/c599b452a.drive`.

1. Prediction 1 held, from the fallback source: `card599: host t=203755031 copy BP_PlayerController_C camera
   manager PlayerCameraManager set to its view from the camera component and control rotation` at the copy's
   spawn, before its camera cache had a timestamp. The row logs once per camera manager, so later moves from the
   cache have no row. CameraCachePrivate was reflected at +0x1530. The map totals row did not print because the run
   ended before a map end.
2. Prediction 2 held. Every client melee trace starts at the copy camera, 0 uu off. Example: t=203806725, start
   (-68529, -666, 1373), `camera manager PlayerCameraManager at (-68529, -666, 1373)`, copy camera
   (-68529, -666, 1373). In b451 the same row showed start (0, 0, 0).
3. Prediction 3 partly held. `card582: host t=203806841 building hit BP_Door_Sliding_Gigantic_C ... instigator
   BP_PlayerCharacterMI_C (server-copy), causer BP_PlayerCharacterMI_C`: the copy's knife reaches the door. The
   door takes damage 0.00, the same as from the host's knife. There is no MS_EMIT_Human_Building_Generic_Damaged
   for the copy hit: no host card455 attached row and no client played row. The host's own hit plays it
   (card455 and card304 at t=203803733). This is an open sound gap, reported to the senior.
4. Prediction 4 held. Spawned WarriorBug guid 0x410 took three copy knife hits of 125 each: card166 `OnDamageCaused
   on server copy ... D 410 125.000` at t=203821075, 203822691 and 203827158. It also took eight Morita NPC hits of
   8 each. The last copy hit killed it: `InitArticulated() ... guid 0x410 ... IsDead 1 health -31.0` and a card269
   rest row `seq Death_06` at t=203827159. The client got the hit (card132 `OnDamage ran`), the hitmarker
   (MS_GORE_2D_Generic_Hitmarker) and the pain bark. The scenario's death wait matched an earlier, unrelated bug
   (guid 0x3f6, far away). The proof is the guid 0x410 rows above, not that wait.
5. Prediction 5 held. The host melee rows are unchanged: start equals host camera (-68533, -666, 1375).
6. Prediction 6 held. card132 rounds 1-6 show mismatches 0, and both playlog summaries show mismatches 0.
   WARN: host 7, client 7. All are known classes: attach, config, card046 warp, card537 not keyed, card137 change
   table, card114 defaults ini, and the card132 impact-far check (seen since hb51).

Open: the copy's knife on a building plays no Generic_Damaged sound on either side. The knife impact sound
MS_GORE_Knife_Attack_Impact plays on the host (card173 remote sound from the copy), but the client log has no row
for it.

## Closed (senior, 2026-11-21)

Proven in b452 (c599b452a), and c448b452a passed. Ships on lane 1 as b452 (dwmapi 61270761ad74d597).
