# Card 075: Mechs in co-op: one mech per pad, and the client can pilot it

Status: sent 2026-11-05 09:52 to junior ultimate-bug-war-coop-bb, after card 066 was accepted. Drafted 2026-11-05 (senior). FED_C1M1 has no player mech, so this matters from FED_C2M1 on.

Read `docs/STATE.md` ("Duplicate mechs", "Client cannot enter a mech"), the card 032 Report (all of it, including the note on the mission-brief pause), and the card 054 Report and relay table (`kC54Relays`, hooks.cpp ~21395). Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

Card 032 found two faults:

1. **Duplicate mechs.** Mech pads (`BP_MechDropPlatform_C`) and pods are not replicated (`bReplicates 0`). Each machine runs the mission's pod drop by itself. The host's pod mech (`BP_BaseMech_C`, `bReplicates 1`) replicates to the client. The client also spawns its own mech under the same pad (roles local 3, remote 1 on the client), and the host never sees it. So the client sees two mechs per pad, and one of them exists only on the client.
2. **The client cannot enter a mech.** The client's `OnInteraction` (0x050F7840) runs `EnterMech` (0x050E65F0) locally and sends nothing to the server. `ASTPlayerMechBase` has no `Server*` RPC. The game reverts the local enter within about 4 s. The host's own enter possesses the mech through its controller (`ServerAcknowledgePossession`), and that possession replicates.

## Design

The host owns every mech. The client keeps only the host's replicated copies, and asks the host to run its enter and leave.

1. **Duplicates (client).** Find where the client spawns its own pod mech: the pod's spawn of `BP_BaseMech_C` (card 032 saw it after `BP_TacSupp_Pod_C.InboundMovementTimeline__UpdateFunc` on `BP_TacSupp_Pod_MechDrop_C`). Prefer stopping the spawn over destroying the mech afterwards:
   - First choice: on a client session, block the spawn of an `ASTPlayerMechBase` whose owner chain leads to a non-replicated pod. Find the narrowest call that does this, for example a `BeginDeferredActorSpawnFromClass` / `FinishSpawningActor` pair from the pod, and skip it only for that class on the client.
   - If no narrow point exists, destroy the client-authority mech (local role 3 on a client session, class `ASTPlayerMechBase`) on its first sighting, before anyone can enter it. Keep the pod and pad as they are, because their visuals are harmless.
   - Senior lead (static, unproven): `ASTPlayerMechBase::BeginPlay` is at 0x050DDFA0, `ActorFullyLoaded` at 0x050DA390 and `OnBoardingComplete` at 0x050F6950. One fallback point is a `BeginPlay` detour: on a client session, a mech with local role 3 skips the original and is destroyed. The host's replicated mech has local role 1 on the client, so it is not affected. Skipping the original also skips the Blueprint `ReceiveBeginPlay`, so the mech's own setup never runs. Check that the pod's sequence does not wait on it, for example a timeline that holds a reference to the mech.
   - Check that the host's replicated mech still arrives under the pad on the client.
   - Log one line per blocked or destroyed mech, with its location and the pad nearest to it.
2. **Enter (client to host).**
   - The client's `OnInteraction` on a mech sends one `ChanOp::MechEnter` frame and lets its local enter run as now. Check whether the local enter fights the replicated possession. If it does, skip the local run.
   - A replicated mech has no placed name, so card 054's `UseName` lookup by placed name does not fit. Identify the mech by the host's location of it: the client sends its copy's location. The host takes the nearest `ASTPlayerMechBase` within 300 uu, and logs the distance and a WARN when none is found.
   - The host runs `OnInteraction` (or `EnterMech`, whichever card 032's host-own enter logs show the game itself uses) with the sender's server copy pawn and controller, never another pawn. Guard it: no enter if the mech already has a pilot (weak PC +0xE30 set), if the copy is dead or downed, or if the frame came from a PC with no copy.
   - Pass when the client's camera and input move to the mech, and the host sees the mech walk with the client's input.
3. **Leave.** The same path with `ChanOp::MechLeave` and `LeaveMech` (0x050F2200), triggered by the client's own leave input. The client's soldier must reappear at the mech on both screens.
4. **Not in this card:** mech weapons, damage and death. Find out in the proof whether the mech's fire reaches the host (it may ride the existing card 024 path). Report it; do not fix it here. The remote sprite body (card 028) in a mech is also out of scope.

This runs in every co-op session. The card 032 audit detours stay behind `UBWCOOP_SPAWN_MECH`. Card 047's gate leaves them gated, so a fix here must not live in those detours.

## Do

1. Implement the above.
2. Proof runs on FED_C4M1 (`UBWCOOP_TRAVEL=/Game/Maps/FED/FED_C4M1?listen`). Card 032 used it because it has placed pads and pods. Card 067 closes the host's mission brief, which blocked card 032's earlier runs.
   - `c75a`: `UBWCOOP_SPAWN_MECH=mech` (the host spawns a mech 5 m ahead). The client walks to it and enters, walks 5 s, and leaves. Take shots on both sides at the enter, the walk and the leave.
   - `c75b`: a natural pod drop. Route both players to the mission point where card 032's pods dropped. Count `BP_BaseMech_C` actors on each side at 1 Hz: one per pad on both sides, with no client-local mech. The client enters a pod mech.
   - `c75c`: the host enters a mech while the client watches. Nothing changes from today, apart from the duplicate being gone.
3. Run solo (`SOLO=1`) on FED_C4M1: no `MechEnter` frames, and the host's own pod drop and enter work as before.

## Done when

- The shots show one mech per pad on the client, and the client piloting a mech on both screens.
- Each enter and leave is logged on both sides with the pawn, the controller and the mech's pilot before and after.
- `logsum` gives ERROR/FATAL 0, both games end with rc 0, and there is no new dump.
- fnlen reports no function over 60 lines.

## Report

Done (junior, 2026-11-05). All proof on FED_C4M1. Scenarios: `tools/scenarios/c75a-mech.txt`, `c75b-pods.txt`, `c75-solo.txt`. c75c is covered by c75a, where the host driver enters and leaves its own mech first, with the client watching.

### Root causes found

1. **Duplicate mechs.** The pod's Blueprint spawns `BP_BaseMech_C` through its SpawnActor node. The path is `execBeginDeferredActorSpawnFromClass` (RVA 0x03C78270, size 0x370), then `UWorld::SpawnActor` (0x03DCA380).
2. **Client enter.** `OnInteraction` and the exit input run only on the client, as card 032 found. The mech has no server RPC.
3. **Mech stood still under the client's input after the host-side enter.** The mech's aggregators already exist (its GEs ran before the pilot's re-init), so their base stayed 0. The next GE re-aggregation (InputTag.Move) set WalkSpeed 750 -> 0. Fixed in card 019's re-init: every base is now pushed through `SetNumericAttributeBase` (`card019_sync_aggregators`, hooks.cpp ~4646), which updates the aggregator too.
4. **Host could not see the mech the client drives.** The mech's visible body is the BP component `Mech` (StaticMeshComponent). `EnterMech` sets its `bHiddenInGame` = 1 on the machine that runs the enter (that machine's pilot gets the cockpit view), and `LeaveMech` shows it again. The host runs the enter for the client, so the host hid the body. The client's view of a host-piloted mech was already fine (`Mech` bHiddenInGame 0 on the client, c75a13 client shot). Card 028's sprite body and card 033's FP hide also touched remote mechs.

### What changed (mod/src/hooks.cpp, card 075 block ~26439 unless noted)

- **Spawn block (client):** `detour_c75_world_spawn` on `UWorld::SpawnActor`. On a client session, when the caller is inside the BP thunk's range and the class is an `ASTPlayerMechBase`, it returns null. It logs "card075: client blocked spawn" with the location and the nearest pad. This is not in the card 032 audit detours (they stay behind `UBWCOOP_SPAWN_MECH`).
- **Counts:** `c75_count_step` logs the `ASTPlayerMechBase` count, roles and nearest pad on each change, at 1 Hz.
- **Enter:** the client's `detour_c75_mech_on_interaction` sends `ChanOp::MechEnter` with its copy's location and skips its local run; the host's enter possession replicates to it. The host (`c75_receive_enter`) checks three guards: the PC has a server copy (`c75_sender_copy`), the copy is not dead or downed, and the mech has no pilot (weak PC +0xE30). It then takes the nearest mech within 300 uu (logging the distance, or a WARN when none is found) and runs `OnInteraction` with the sender's server copy. That is the path the host's own enter uses.
- **Leave:** the client's exit input (`detour_c75_exit_pressed`, `detour_c75_pc_exit`) sends `ChanOp::MechLeave` once per possession. The E leave is a 2500 ms `IA_ExitVehicle` hold that repeats every tick. The host (`c75_receive_leave`) runs `RequestLeaveMech` on the mech that PC drives.
- **Before/after lines:** `detour_c75_enter_mech` / `detour_c75_leave_mech` log the pawn, controller, weak PC and the mech's body hidden state, before and after. A pilot table (`kC75Pilots` = 4) names the soldier on the leave lines, because the mech's `ControllingPlayerCharacter` reads null on the host.
- **Remote mech body:** `c75_show_body` (~10406). `c28_visit` now skips mechs: no sprite body and no c33 hide. It also shows the `Mech` component again on a remote (not locally controlled) mech, without propagation, and logs "showed body". This touches the card 028 path, but "piloting a mech on both screens" needs it. The cockpit arms and weapons meshes (`FirstPersonMesh`, `Mesh`, under Camera) are left alone.
- **Test verb:** `warp actor|client <name> [dist]` (cmdfile.h/.cpp `Op::Warp`, default 300 uu; drive.py). A navmesh gap between the start and the pads blocks a `goto`. The host warps its own pawn or the client's server copy.
- **Diag:** the `c75_piloted_step` / `c75_server_cmc_line` diag lines are left in (capped). They can be removed in a later build if you want.

### Runs

| Run | Kind | Result |
|---|---|---|
| c75b6-9 | pods (earlier builds) | passed |
| c75b10 | pods | passed, rc 0/0, 0 ERROR, no dump; host shots showed no mech while the client piloted it -> root cause 4 |
| c75a11 | SPAWN_MECH | invalid: ran on C1M1 (`UBWCOOP_TRAVEL` missing) |
| c75a12 | SPAWN_MECH | crash: new dump `UECC-Windows-57088E5844EAF37B9B42A3931A663D0A_0000`, AV reading 0x10 (in dwmapi on the stack). Caused by my diag, which read a component of a null `Mesh`. The diag was removed; no later run made a dump |
| c75a13 | SPAWN_MECH | scenario FAIL at line 37 (MechLeave timeout), rc 0/0, no dump. The 5 s walk took the mech off a ledge (MovementMode 3, server MaxWalkSpeed 0), and a falling mech's leave fails. Gave the component evidence for root cause 4 |
| **c75b11** | pods, final build | **passed** (93 s), rc 0/0, host 1831 / client 2059 lines, FATAL/ERROR 0, WARN 0 host / 1 client, no new dump |
| **c75a14** | SPAWN_MECH, final build, 2 s walk | **passed** (102 s), rc 0/0, FATAL/ERROR 0, no new dump |
| **c75s1** | solo | **passed** (83 s), host rc 0, 1203 lines, FATAL/ERROR 0, WARN 0, no new dump |

c75b11 evidence:
- **Counts.** Host mechs 0 -> 2 (local role 3: 2). Client mechs 0 -> 1 after the warp, roles 1/3, local role 3 count 0. That is the host's replicated mech only, no client-local one. The far pad's mech (about 39,000 uu away) never entered the client's count; most likely net relevancy, not checked.
- **Client blocked spawn ×2**, nearest pad at 261 and 263 units.
- **Host lines:** "MechEnter from" at 0 units, EnterMech before/after (roles 3/1 -> 3/2), "showed body Mech of remote mech BP_BaseMech_C (controller BP_PlayerController_C)", "MechLeave from" at 1 unit, LeaveMech before/after.
- **Client lines:** MechEnter before/after (roles 1/3 -> 2/3), MechLeave before/after, and the soldier back on foot.
- **Shots** (`Saved/Screenshots/Windows/<role>-c75b11-*.png`):
  - The host's walk and drive shots show the client-driven mech moving. The drive shot also shows CrushProps sparks.
  - The client's walk shot shows the M7 RAZORBACK HUD.
  - The client's pad shot shows one mech on the near pad (no duplicate).

In c75a14 the host's own enter and leave (c75c) ran first, then the client's, with the host's "showed body" line. Its host walk shot shows only the host under bug attack (health 1), so c75b11 is the both-screens proof. c75a13's client shot of the host-piloted mech shows its body on the client.

c75s1 (solo): 0 MechEnter/MechLeave lines. Both pods dropped (mechs at 270 and 279 units from their pads). Host EnterMech before/after (body hidden 0 -> 1, the cockpit view), the piloted mech moved about 7000 uu, LeaveMech before/after (body 1 -> 0). Shots `host-c75s1-{pods,enter,walk,leave}.png`.

c75a scenario change: `client key W 5000` became `client key W 2000 timeout=15` (the ledge). c75b keeps the 5 s walk.

### Mech fire (not fixed, as specified)

No scenario fired a mech weapon, so whether a shot reaches the host is **untested**. Evidence so far:
- The client adds `IMC_Mech_KBM`/`IMC_Mech_Gamepad` on enter (c75b11 client log ~307-311: `IA_MechPrimaryFire <- LeftMouseButton`, `IA_MechSecondaryAbility <- RightMouseButton`).
- The client binds the 4 mech ability tags (`AD_MechPlayerAbilityDefinition_C`) and activates CrushProps locally (handle 143).
- The host log has 265 `InternalServerTryActivateAbility` lines during the drive, and the host's drive shot shows CrushProps sparks. So GAS ability activation reaches the server; mech fire may ride the same path.

A follow-up card should fire the mech weapon at a bug and check damage on the host.

### Other findings

- **Server mech CMC.** MaxWalkSpeed flips between 750 and 2250 while driven (sprint), and goes to 0 while falling (MovementMode 3).
- **`sprint_side()` mislabel (follow-up card).** While the client is in the mech, the host labels the client's soldier "host-own", and the client labels its own soldier "other". The labels are cosmetic: nothing in this card branches on them.
- **One client WARN per run, pre-existing (card 034):** "client refit of BP_PlayerCharacterMI_C skipped: offsets ok, half-height 0.0, FitMeshToCollider resolved".
- fnlen: 1416 functions, 0 over 60.

## Senior review (2026-11-05 11:50): accepted

- Checked c75b11, c75a14 and c75s1 myself. All three drives end with `scenario passed`. ERROR/FATAL 0 in every log. The only WARN is the pre-existing card 034 client refit line, once per co-op run. All three runs started after the DLL deploy at 11:38:45, and the deployed DLL matches `build/dwmapi.dll`. fnlen: 1416 functions, 0 over 60 lines.
- Dumps: the only new dump is the 11:31 one from c75a12, which the Report names and explains (the junior's own diag read a null `Mesh`; the diag is removed). No run on the final build made a dump.
- c75b11 client log: two `blocked Blueprint spawn of BP_BaseMech_C` lines, at 261 and 263 units from their pads, then `mechs 0 -> 1, local role 3 0 -> 0`. The enter and leave lines show roles 1/3 -> 2/3 -> 1/3 and the soldier back under the PC. Host log: `MechEnter from` at 0 units, `showed body`, and the piloted mech moving about 600 uu over 8 s under the client's input. This was the c75a6-a10 blocker; the aggregator push fixes it.
- Shots (montage in my scratchpad): the host's walk and drive shots show the client-driven mech body with the CrushProps sparks. The client's walk shot shows the M7 RAZORBACK HUD. The client's pad shot shows one mech.
- `card019_sync_aggregators` runs only in the client movement re-init, on the movement set, and pushes each attribute's own base, so values do not change; only the aggregator base is fixed. It runs for every client pawn re-init. c75b11 and c75a14 both include on-foot play after the leave, with no WARN.
- The spawn block sits on `UWorld::SpawnActor` with no gate, as it must, and only acts on a client for an `ASTPlayerMechBase` spawned from the BP thunk range. It is not in a card 032 audit detour.
- Accepted limits: mech fire is untested (follow-up card), and the `sprint_side()` mislabel is cosmetic (follow-up). The capped piloted/CMC diag lines can go with the next card that touches this block.
