# Card 032: Mech spawn and enter audit (log only)

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: sent 2026-11-03.

Read `docs/STATE.md` first: the "Mod code rules" section, the "Runtime facts" line about `tools/run_pair.sh`, and row 029. Then read card 029's report and senior review (section 4, mechs), and `docs/design/coop-channel-lobby-downed.md` section 4. Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

Card 029 could not test mechs: FED_C1M1 places no player mech. Player mechs come from `BP_MechDropPlatform` pads placed in later maps (FED_C3M2 has three), and possibly from the MechSupport tactical support. The remote sprite body (card 028) and the channel (card 026) need to know what a mech does on each machine: whether it replicates, what happens to the pilot pawn, and whether a client can enter one at all.

## Static leads (senior, 2026-11-03)

Treat these as leads to confirm, not facts.

- **`USTCheatManager::CheatSpawn(FString path, float dist)`** (0x051F5DC0, private) loads a class by path with `StaticLoadClass` (0x0157BFE0). It then spawns the class through `UWorld::SpawnActor` (0x03DCB440), `dist * 100` units along the view of `this->Outer` (the PlayerController's `GetPlayerViewPoint`, vtable +0x708), with collision handling 2 (adjust if possible, always spawn). It uses `this` only for the outer (+0x20) and `GetWorld` (vtable +0x180).
- **`USTCheatManager::SpawnPickup(FString row)`** (0x0521D440) gets `USTDataTableSubsystem` from the game instance. It takes the pickups `UDataTable` from the subsystem's weak pointer at +0x58 and calls `FindRow<FSTPickupsTableRow>(FName(row))`. It resolves the row's soft class pointer at row +0x08 (the `FSoftObjectPath` is at row +0x10) and checks that the class is a child of `ASTCustomPickupBase`. Then it calls `CheatSpawn(GetPathName(class), dist)`.
- Neither function exists in the mod's call graph. A shipping PlayerController may have no `CheatManager`, so do not depend on one: do what the two functions do directly.
- Assets, read from the cooked packages with `tools/iostore/` (see `docs/reference/iostore-extract.md`):
  - Pickups table row **`TacMechSupport`** in `/Game/Blueprints/Pickups/DT_TacSuppPickups` (also in the composite `DT_Allpickups`). Its class is `/Game/Blueprints/Combat/MechAlly/BP_TacSuppPickup_MechSupport.BP_TacSuppPickup_MechSupport_C`. The pickup references `Config_TacSupp_MechSupport`, which references `GA_TacSupp_MechSupport` and `BP_SupportFlare`.
  - **`GA_TacSupp_MechSupport` references `BP_TacSupp_Pod_MechDrop` and `/Game/Blueprints/Enemies/Humans/M7Mech/BP_AI_M7Mech`.** Its node names (`GuaranteedActorsToSpawn`, `Random Actors Spawn Amount`, a flare cast) suggest that MechSupport drops an **AI** M7 mech ally in a pod at the flare, not a mech the player pilots. Confirm this at run time.
  - The pilotable mech is `/Game/Blueprints/Player/Mechs/BP_BaseMech.BP_BaseMech_C`. It is referenced by `BP_TacSupp_Pod_MechDrop` and by the level element `/Game/Blueprints/EnvironmentElements/MechPad/BP_MechDropPlatform`. The pad spawns the pod (`BeginDeferredActorSpawnFromClass`), has `CanBeActivated` / `ActivateOnStart` / `AllowRepeatedActivation`, checks `IsDead` on the player, and saves by actor GUID. `BP_AIMechCallIn` sits in the same folder.
  - **Placed pads exist** (name maps of the cooked `*_Scripting` sublevels, senior scan 2026-11-03). Card 029's "none placed" covered only the mech actor itself.
    - `BP_MechDropPlatform` (pilotable mech pad) is in FED_C3M1, FED_C3M2 (instances `_C_1`, `_C_6`, `_C_7`), FED_C4M1, FED_C4M2, FED_C5M1 and BUG_C1M1. FED_C2M1 has a `BoardMechsuit` objective.
    - `BP_AIMechCallIn` (AI mech ally) is in FED_C1M1 (several), FED_C5M2, BUG_C2M1, BUG_C3M2, BUG_C4M2 and BUG_C5M1. FED_C1M1, the usual test map, therefore has AI mech call-ins but no player mech pad.
- `ASTPlayerMechBase`: `OnInteraction` 0x050F7840 (interface subobject at +0xC08), `EnterMech` 0x050E65F0, `LeaveMech` 0x050F2200, weak PlayerController at +0xE30. No `Server*` RPC exists on it.
- `USTTacticalSupportComponent::EquipTacticalSupportAbility(TSubclassOf<USTTacticalSupportAbilityConfig>, bool)` 0x050E69E0 and `TryThrowFlare(int)` 0x0510A5B0.

## Do

1. **Scan and confirm.** On the host, after travel, log every actor whose class name contains `MechDropPlatform`, `AIMechCallIn`, `BaseMech` or `M7Mech`, with location and owner. Also log the `TacMechSupport` row of the pickups table (row name and soft class path), to confirm the static path above.
2. **Test-only spawn switch.** Add `UBWCOOP_SPAWN_MECH=<pickup|mech|pad>`, off by default. About 20 s after both pawns are on foot, the host:
   - with `pickup`, loads the MechSupport pickup's class from step 1 and spawns it 3 m in front of the host pawn, as `CheatSpawn` does;
   - with `mech`, spawns `BP_BaseMech_C` directly by the path above;
   - with `pad`, spawns `BP_MechDropPlatform_C` 5 m in front and activates it (its `Activate` event or `ActivateOnStart`), the closest path to the game's own player-mech drop.
   - Preferred if travel works: run the pair on `/Game/Maps/FED/FED_C3M2?listen` (path confirmed in `docs/reference/asset-paths.txt`) and use a placed pad as a player would. Report which route you used and why.
3. **Call-in.** With `pickup`, walk the host onto the pickup and use the tactical support input as a player would. Log `EquipTacticalSupportAbility` and `TryThrowFlare` (arguments and return), and the spawn of the pod and the mech.
4. **Log, on both sides, with timestamps:**
   - `EnterMech`, `LeaveMech` and `OnInteraction` on the mech: who calls them, the mech's weak PC (+0xE30), and whether the pilot pawn is hidden, attached or possessed away (the PC's pawn before and after, as card 029 logged for turrets);
   - whether the mech actor exists on the client, and whether its position on the client follows the host (sample once a second while the host drives it);
   - a client pressing interact on the mech: whether anything fires on the client, and whether anything reaches the host.
5. **Screens.** Take a `shot` on both machines while the host is in the mech.

## Answer, with log lines

- Is MechSupport an AI M7 ally or a pilotable mech? Which maps place a `BP_MechDropPlatform`?
- Does the mech replicate, and does it move on the client?
- What happens to the host's pawn and PC when the host enters, and what does the client see of it?
- Can a client enter a mech, and if not, where does its interact stop?

## Done when

- All four answers are given with log lines and screenshots.
- The switch is off by default and nothing changes when it is unset. Clean build (`/W4 /WX`, `--clean-first`), 84 exports, KERNEL32-only imports. No new crash dumps. Every new function is at most 60 lines.

## Risks

- A mech spawned outside its drop sequence may not be fully initialised (`ActorFullyLoaded`, `OnBoardingComplete`). If `mech` fails, say how, and rely on `pickup`.
- The call-in may be refused inside a tactical support blocking volume (`ASTTacticalSupportBlockVolume`). If the call-in is refused, move into the open, then retry, and log `IsPlayerInBlockingVolume`.

## Senior addendum (2026-11-03, after card 023)

- **Reuse card 023's spawn code.** Card 023 already loads a class by path through `StaticLoadObject` and spawns it on the host (`UBWCOOP_SPAWN_BUG`, `SpawnAIFromClass`). For `mech` and `pad`, share that load-and-place helper rather than writing a second one, and use `UWorld::SpawnActor` (0x03DCB440) for non-AI actors. If the spawned mech has `AutoPossessAI` set and no controller, report it; do not call `SpawnDefaultController` on a pilotable mech.
- **Screenshots collide.** Both instances write to one Screenshots folder (card 023 kept 7 of 10). Take the client's shot 1 s after the host's, or give each side its own file name.
- **Enemy AI troopers.** In FED_C1M1, AI troopers count as `ASTEnemyBase` (card 023). If you scan by class, filter by class name as step 1 says, not by base class.

## Report

Done, with one open item: the MechSupport pickup was never collected by the scripted walk, so its call-in was not observed. Switch `UBWCOOP_SPAWN_MECH=pickup|mech|pad` (config.h/.cpp), off by default. All c32 code sits in `hooks.cpp` (`// --- Card 032` block): 1 Hz class-name scan (`kC32Patterns` plus `ASTPlayerMechBase`), `EnterMech`/`LeaveMech`/`OnInteraction` detours logging caller, weak PC (+0xE30), the PC's pawn and the pilot's bHidden/attach/controller before and after, `TryThrowFlare`/`EquipTacticalSupportAbility` hooks, a ProcessEvent filter for mech, pod, pad and possession UFunctions, and the host driver (spawn 20 s after both players are on foot). Spawning reuses card 023's `StaticLoadObject` path and calls `UWorld::SpawnActor` (`FActorSpawnParameters` ctor, collision handling 2). `SpawnDefaultController` is never called.

Runs (all FED_C4M1 via `UBWCOOP_TRAVEL=/Game/Maps/FED/FED_C4M1?listen`, because C4M1 has placed pads, pods and a placed MechSupport pickup; no crashes in any run, crash dumps still 16):

| tag | route | result |
|---|---|---|
| `card032-pad` | `pad` | nearest placed pad 37850 units away; `Activate` ran via ProcessEvent, nothing dropped in 90 s |
| `card032-mech` | `mech` | host enter, drive (11700 units), leave; client sees the mech move |
| `card032-mech2` | `mech` | host enter and leave again; this time the drive did not move the mech; client interact traced |
| `card032-pickup` | `pickup` | pickup spawned, walk ended 300 units short, 3 throws of the equipped Resupply, no mech; level pods then dropped by themselves on both sides |

### 1. MechSupport: AI ally or pilotable? Which maps place a pad?

- The level's mech pods drop a **pilotable `BP_BaseMech_C`** (an `ASTPlayerMechBase`, `AutoPossessAI 0`, no controller), one per `BP_MechDropPlatform_C`:
  `card032: host found BP_BaseMech_C ... idx 237328 at (3191, -61508, 1519) ... bReplicates 1, AutoPossessAI 0, controller null (-), pilotable 1` right under pad (3150, -61481) after `BP_TacSupp_Pod_C.InboundMovementTimeline__UpdateFunc on BP_TacSupp_Pod_MechDrop_C`.
- The pickup row: `pickups row 54 TacMechSupport <-- MechSupport: soft class /Game/Blueprints/Combat/MechAlly/BP_TacSuppPickup_MechSupport.BP_TacSuppPickup_MechSupport_C`. Its folder is `MechAlly`, which hints at the AI `BP_AI_M7Mech` ally. This is not confirmed: the scripted walk stopped 300 units from the pickup, the equipped config stayed `Config_TacSupp_Resupply_C` and three `TryThrowFlare` calls (`IsPlayerInBlockingVolume 0`) brought nothing. Collecting the pickup by hand, then calling it in, would settle it.
- Pads (`BP_MechDropPlatform`, from the asset scan): FED_C2M1, C3M1, C3M2, C4M1, C4M2, C5M1, C5M2; BUG_C1M1, C3M2, C4M2. `BP_AIMechCallIn`: FED_C1M1, FED_C5M2, BUG_C2M1, C3M2, C4M2, C5M1.
- Pads and pods are **not replicated** (`bReplicates 0`, roles 3/0 on both machines). In `card032-pickup` both sides' pods began dropping at the same mission point (host 505947641 ms, client 505947896 ms, followed by a checkpoint `PreSaveGame`), and **each machine spawned its own mechs**: the client's copies are `roles local 3 remote 1` on the client, so they are client-authority actors the host never sees. A player-activated pad is likely the same: local on each side.
- Forcing a pad (`pad` route) did not work: `FindFunction("Activated")` null, `Activate` (exists) ran, `CanBeActivated 1`, `ActivateOnStart 1`, no pod moved: `WARN no pilotable mech 90004 ms after the pad activation`. The drop is driven by mission progression, not by `Activate` alone.

### 2. Does the mech replicate and move on the client?

Yes, a host-spawned mech does. `SpawnActor` gives `BP_BaseMech_C ... roles local 3 remote 1, bReplicates 1, AutoPossessAI 0, controller null`. In `card032-mech` the client's copy (roles 1/3) follows the host about 0.5 s behind:
host `505453058 ms ... at (26968, -95756, -712)`, client `505453615 ms ... at (28224, -95824, -743)`; host `505456067 ms (32230, -95829)`, client `505456617 ms (32332, -95762)`. On the client the mech shows `owner null, controller null`, so the client never learns who drives it. In `card032-mech2` the drive input moved nothing (no host `moved` rows at all), so the client logged `mech seen, never moved`. That run tells us nothing about replication.

The level-spawned pod mechs (question 1) are not the host's replicated actors on the client; the client spawns its own.

### 3. Host pawn and PC when the host enters

`OnInteraction` -> `EnterMech` (both with caller exe+0x0, called by the driver):
- before: `weak PC null, mech controller null; pilot BP_PlayerCharacterMI_C (host-own) bHidden 0 attached to null`
- after: `weak PC BP_PlayerController_C (its pawn BP_BaseMech_C), mech controller BP_PlayerController_C; pilot BP_PlayerCharacterMI_C bHidden 1 attached to BP_BaseMech_C, controller null`
- PE in between: `BP_BaseMech_C.ReceivePossessed`, `PlayerController.ServerAcknowledgePossession`, `BP_PlayerController_C.ReceivePossess`, HUD `OnPossessedPawnChanged`, `WB_MechEject` built. The PC possesses the mech. The character is hidden, attached and unpossessed.
- Leave (`RequestLeaveMech`, `LeaveMech` caller exe+0x50FF278): weak PC and mech controller go to null, `PC pawn BP_PlayerCharacterMI_C` again.
- The client sees the mech move (question 2). In run 1, about 14 s after leaving, the host paused (`BP_BaseMech_C.Paused`, HUD `SetInputMode UIOnly`, a "press to continue" popup). The popup did not show up in mech2.

Screenshots (shared folder, distinct numbers, no overwrite): run 1 host `ScreenShot00211` (cockpit view of the mech, bugs ahead) and client `ScreenShot00210` (third person; client took it 0.9 s *before* the host). mech2 `00213` host and `00214` client. `kC32ClientMovedShotMs` is now 6000 so the client shot lands about 1 s after the host's (not re-run).

### 4. Can a client enter? Where does its interact stop?

No. The client's `OnInteraction` runs `EnterMech` **locally only** (`card032-mech2`, 530 units from the host's mech, roles 1/3):
- after: `weak PC BP_PlayerController_C (its pawn BP_PlayerCharacterMI_C), mech controller null; pilot ... bHidden 1 attached to BP_BaseMech_C`. The client hides and attaches its own character, but nothing possesses: the mech controller stays null and the PC's pawn stays the character.
- 4 s later: `client state after interact: ... pilot null`. Replication from the host overwrites the local pilot state.
- On the host: no `OnInteraction`, no `EnterMech`, no Server RPC PE line after the client's interact. **The interact stops on the client, inside `EnterMech`; no server RPC is sent.** A co-op mech enter needs a mod relay (client asks, host runs `EnterMech` for the client's server pawn and PC).
- Same in `card032-pickup` on a client-local pod mech (roles 3/1 on the client): local hide and attach, no possession, pilot reverted by 4 s.

### Tooling

`tools/run_pair.sh` has a new `HOST_ENTER_PATTERN` env: each new host-log match presses Enter in the host window (x < 330), for the mid-mission "press to continue" popup that pauses the host while the client sits in the mission. It did not fire in mech2 or pickup (no popup; in pickup it also printed `WARN no host window found` once when it matched early, so the window lookup needs another look if it is used again).

### Build

`cmake --build build --clean-first`: clean under `/W4 /WX`, 84 exports, imports `KERNEL32.dll` only. Every function is 60 lines or fewer (awk check over hooks.cpp and config.cpp). No new crash dumps in any of the four runs.

## Senior review (2026-11-03)

**Accepted, with one open item:** the MechSupport call-in was not observed. Verified by the senior:

- **Errors.** All four runs (pad, mech, mech2, pickup) have 0 FATAL/ERROR lines on both sides.
  - Process gap: no `card032-*-run.txt` (rc and summary) was kept, so the exit codes are not on file. Keep the run_pair summary in a file for every run.
- **Log lines.** The report's lines match the logs:
  - `card032-mech-host.log`: the level pod mechs (`pilotable 1, bReplicates 1, AutoPossessAI 0`) at lines 1029–1030, and the host-spawned mech at line 1134;
  - `card032-mech2-client.log`: the client's `EnterMech` before and after at lines 880–881, and the reset to `pilot null` 4 s later at line 886;
  - `card032-mech2-host.log`: the host has only its own `OnInteraction`/`EnterMech`, at 505637275 ms. Nothing appears after the client's interact at 505678368 ms.
- **DLL.** The build after the runs differs from the deployed DLL in exactly one instruction (`cmp 0xfa0` → `cmp 0x1770`): the `kC32ClientMovedShotMs` change to 6000, which has not been run.
- No function over 60 lines.

**Conclusions for the design:**

1. **Duplicate mechs in co-op.** Pads and pods are not replicated (`bReplicates 0`). Each machine runs the mission's pod drop on its own, so the client gets its own client-authority mechs. Those mechs replicate nothing, and the host never sees them. The host's pod mechs also replicate to the client (`bReplicates 1`), so the client sees two mechs per pad. This is now a known issue. The likely fix is to stop the client's own pod drop, or destroy client-authority mechs, and let the host's replicate. The host spawn path works: the client's copy follows about 0.5 s behind.
2. **Mech enter needs a relay.** A client's interact runs `EnterMech` locally and sends nothing to the server. The relay has the client ask and the host run `EnterMech` (or `OnInteraction`) for the client's server pawn and controller. The host's own enter possesses the mech through the PC (`ServerAcknowledgePossession`), so a host-run enter for the client's PC should replicate the possession.
3. **No driver on the client.** On the client the mech shows `owner null, controller null`. A remote driver's mech has no driver sprite, but the mech itself replicates, so this matters little.

**Open:**

- What MechSupport calls in: AI M7 ally or pilotable. Settle it by collecting the pickup by hand.
- The host's "press to continue" pause after leaving the mech in run 1, and the `HOST_ENTER_PATTERN` window lookup that missed once.

## Report correction (2026-11-03, after the senior review)

The user found the cause of the odd results: **in every earlier run the host sat on the mission-brief pause** ("OPERATION TOURNIQUET ... PRESS ANY KEY TO CONTINUE", `WB_Stack_C` `RequestGamePause` flag 1 about 2.5 s after LoadMap). Meanwhile the client was already playing in the mission. Injected keys do not release that pause (card 015h). The user dismissed it by hand in run 1 (505438871 ms) and in pickup (505947635 ms). In mech2 nobody dismissed it.

Corrections to the report above:

- **Pod drop = mission start, not mission progression.** In `card032-pickup` the pods dropped 6 ms (host) and 0.25 s (client) after the user's key released the brief. In the new runs they drop about 2.6 s after the auto-unpause, on both sides. The `pad` route's "drop is driven by mission progression" is withdrawn. On C4M1 the placed pads drop at mission start anyway, so the `pad` route cannot test a forced activation on this map.
- **mech2's drive did not move the mech because the host was paused.** mech2's client enter also ran against a paused host. The run 1 "press to continue after leaving the mech" was the same kind of `WB_Stack_C` pause. It was not seen again in the new runs, even though the unpause stays armed for later pauses.
- **The `card032-pickup` result was invalid** (paused host). The re-run is below.
- `HOST_ENTER_PATTERN` is **removed** from `tools/run_pair.sh`. Its regex could not match the brief line, and key injection does not release the pause anyway.

**Fix: `UBWCOOP_AUTODROPSHIP=1` now also covers maps that start on foot.** `auto_dropship_step` needs only a pawn now, not a dropship. When the pawn is on foot, the unpause action calls `RequestGameUnpause(WB_Stack_C requester)`, as before, with no dropship work, and stays armed for later pauses. Log: `autodropship: action 1/8: USTGameInstance::RequestGameUnpause(... WB_Stack_C) ... dropship 0000000000000000 state -1 behaviour -1, requesters now 0, world paused after: 0`, then `world unpaused after 1 action(s); on-foot pawn ... -- staying armed for later pauses`. It still waits for 2 PlayerControllers and then 3 s. Pair runs on C4M1 should set it in `HOST_EXTRA_ENV`.

New runs (host unpaused, all FED_C4M1, `UBWCOOP_AUTODROPSHIP=1` on the host). The run_pair summaries are in `logs/card032-<tag>-run.txt`:

| tag | route | result |
|---|---|---|
| `card032-mech3` | `mech` | the brief is released 3 s after the 2nd PC; host enter, drive (`moved` rows), leave; client sees it move; client enter at 600 units |
| `card032-mech4` | `mech` | same as mech3, plus the view-height row |
| `card032-pickup2` | `pickup` | pickup spawned, walk ended **1 unit** from it, pickup still in the world, equipped config still Resupply; 2 throws, no mech |

Exit codes rc 0/0, 0 FATAL/ERROR, no new crash dumps.

- **Question 2 holds.** The host-spawned mech replicates (client roles 1/3) and follows. Shots: mech3 host `ScreenShot00218` (in the mech) and client `00219` (mech moving, 4.1 s after the host shot).
- **Question 4 holds with the host unpaused** (`card032-mech3`, replicated mech, roles 1/3 on the client, 600 units). On the client, `OnInteraction` and `EnterMech` hide the character and set the weak PC. `OnEmbarkingComplete` also runs locally and adds `IMC_Mech_KBM`/`IMC_Mech_Gamepad`. The PC's pawn stays `BP_PlayerCharacterMI_C`, the mech controller stays null, and 4 s later the pilot is `null`. The host log has no row of any kind between the client's interact (506423723 ms) and the end. The relay conclusion stands.
- **Duplicate pod mechs confirmed.** In pickup2 the client holds its own client-authority pod mechs (roles 3/1). The host's replicated pod mech (roles 1/3) appeared on the client only at 506770333 ms, 40 s after the host spawned it. The client was about 38000 units away, so the delay is probably net relevancy.
- **The MechSupport pickup is not collected by touching it.** Standing on it (1 unit) left it in the world and did not equip it: no new `EquipTacticalSupportAbility`. It probably needs the interact input. Still open: what it calls in.
- **Client camera "too low" (the user's report):** a new one-shot row gives the same camera height on both machines: `view: own pawn BP_PlayerCharacterMI_C at z -865.5, camera component z -784.5 (camera - pawn 81.0)` (client) and `... z -865.4, camera component z -784.4 (camera - pawn 81.0)` (host). The camera component has the same eye offset on the client and the host. If the client view really is lower than the host's on-foot view, the cause is in the final view (camera manager or crouch), not the component. A matching host on-foot shot would settle it.

## Senior review of the correction (2026-11-03)

**Accepted.** The correction replaces the earlier pad and pickup conclusions. Verified by the senior:

- **Runs.** `card032-{mech3,mech4,pickup2}-run.txt`: host rc 0 and client rc 0 in each run, FATAL/ERROR 0, no new crash dumps.
- **Unpause.** `card032-mech3-host.log` shows it working. `WB_Stack_C` calls `RequestGamePause` at line 190. The mod's `RequestGameUnpause` with the `WB_Stack_C` requester follows at line 305, about 5 s later, and the requester count drops to 0.
- **Client enter.** `card032-mech4-client.log` lines 1202 and 1236 show the client's local `EnterMech` with no possession. The host's only `EnterMech` is its own, at lines 1124–1145.
- **DLL.** The code in `build/dwmapi.dll` and in the deployed DLL is the same.
- No function over 60 lines.

**Changes to my review above:**

- The earlier results came from a paused host. mech2's failed drive and the pad route's "nothing dropped" are void.
- The pod drop comes at mission start (after the brief), on each machine.
- The duplicate-mech and mech-relay conclusions stand.

**Open:**

- What MechSupport calls in. Standing on the pickup does not collect it; it probably needs the interact input.
- The user reports the client camera looks low. The camera component is 81.0 above the pawn on both machines. The next check is a host on-foot shot beside a client one, plus the camera manager's view location.
