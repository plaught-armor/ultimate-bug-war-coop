# Card 121: Client crash at the checkpoint save after the assault course lever

Status: written 2026-11-06 (senior); sent, fixed at review and accepted 2026-11-06.

Read:

- card 119 Report (`detour_c119_prepare_save` in `mod/src/ubw/world_c98.cpp`, the client never loads its save);
- card 048 Report (client checkpoint save and load, `USTSaveComponent`);
- card 100 Report (client skips local `CompleteObjective`).

Repo root: `/path/to/ultimate-bug-war-coop`. DLL base: `4b162843fc154d5f` (card 119). Do this after
card 120; build on its DLL if card 120 is accepted first.

## Why

User (playtest 3, 2026-11-06): "assault course lever caused client to fatal error."

Logs: `logs/playtest-client-20261106-lever.log` (crash lines 2674-2696) and `logs/playtest-host-20261106-lever.log`.

Senior analysis (symbols from `build/publics.txt`):

```
9803264 card050: client Use press: BP_PanelSwitch_WithoutScreen_C_0 ... UseName sent
9803264 card100: client skipped local CompleteObjective Assault_SO4 / Assault_SO0 / Assault_O1
9803265 card020: client client-own Interact ... BP_PanelSwitch_WithoutScreen_C
9803267 CRASH 1/4: EXCEPTION_ACCESS_VIOLATION at exe+0x518E6E0, reading 0x690, rcx=0
#0 ASTNonPlayableCharacterBase::GetNPCGuid+0x0         (this = null)
#1 USTSquadLeaderComponent::OnPreSave_Implementation+0x6d
#2-#4 exec thunk / UFunction::Invoke / UObject::ProcessEvent
#5 ubwcoop+0x40B10
#6 ISTSavableComponentInterface::Execute_OnPreSave+0x3b
#7 USTSaveComponent::PrepareForSave+0x245
#8 ASTSaveSystemEngine::Save_ProcessBatch+0x15a
#9 ASTSaveSystemEngine::Tick+0x96
```

- The client's own lever press ran the level script locally. The objectives it completed (card 100 skipped them)
  started a checkpoint save on the client.
- In that save, a `USTSquadLeaderComponent` (likely on the client's player pawn, or on an NPC squad) walks its
  squad and calls `GetNPCGuid` on a null member. On the client the squad list holds a null NPC.
- Card 119 skips only the components whose owner is a relayed copy. This owner is not a copy, so the save ran.
  This is the second crash from client-side save code that reads state the client never filled in. The next class
  would be a third.

## Do

1. **Measure (static, plus the run below).**
   - Name the owner of the crashing `USTSquadLeaderComponent` and why its squad has a null member on the client.
   - Confirm that nothing on the client reads the checkpoint save it writes: no `LoadLastSave`, restart or travel
     path on the client loads it (card 045 restarts and card 113 travel rejoin from the host). Also check whether
     the same save engine writes anything the client does need: profile, settings, unlocks, campaign progress.
   - Name `ubwcoop+0x40B10` (frame #5), so the report shows whether a mod hook is in the save path.
2. **Fix, one mechanism** (user rule: one mechanism per class of state, not per symptom).
   - Preferred: on a client in a co-op session (net mode client), the checkpoint save does not run at all. Skip it
     at the narrowest point that covers every component, for example `ASTSaveSystemEngine` starting a checkpoint
     save, or `USTSaveComponent::PrepareForSave` for every owner. Log it once per save. This replaces card 119's
     per-copy skip; remove that skip if the new one covers it.
   - Keep any save that step 1 shows the client needs (profile, settings, unlocks). If those go through the same
     engine, skip only the checkpoint kind and say how the two are told apart.
   - The host's save is not touched.
3. **Other cases, static sweep (user, 2026-11-06: "check for any other case this could happen").** Same class of
   fault: client-side game code that runs on state only the host fills in.
   - Every entry into the save engine on the client: checkpoint, autosave, death, quit to menu, level end and
     travel. Each one goes through the skip, or the report says why it is safe.
   - The load side: every `OnPostLoad` / `PostLoadGame` / `ISTSavableComponentInterface` load callback, and whether
     any of them runs on the client (LoadLastSave, restart, card 045 rejoin). If one can, it gets the same treatment.
   - List every class in `build/publics.txt` that implements `OnPreSave` / `PreSaveGame` / `OnPostLoad`, and mark
     which ones exist on the client. The skip must cover all of them. A per-class guard is not enough.
   - Client interaction that runs the level script locally (the lever ran `CompleteSubObjective` and save triggers
     on the client before the host's relay). Name the other client-local effects of that script beyond objectives
     (card 100) and saves: spawns, AI squad changes, checkpoints, timers. For each, say whether it can reach the same
     kind of null state, and fix any that can in this card.
4. **Proof, one run:** FED_C1M1, FAST=1, both players in. The **client** pulls the assault course lever
   (`BP_PanelSwitch_WithoutScreen_C_0`) after the assault objectives before it are done.
   - The client's log shows the save skipped and no CRASH line.
   - The host's checkpoint save still runs (its log line).
   - rc 0, ERROR/FATAL 0 on both sides, no new dump.

Stop when the fix builds and that run shows it; the user will playtest.

## Done when

The Report has the squad leader component's owner and the null member, what the client save engine writes and
whether anything loads it, frame #5, the step 3 sweep (save entries, load callbacks, class list, level-script effects, each with its result), the fix and where it sits, the run lines (client lever, save skipped, host
save), the run length, rc, ERROR/FATAL, CRASH lines, dumps and the DLL hash (sha256 prefix,
`sha256sum | cut -c1-16`). Ends with "Report complete."

## Report

### Measurements

- **Owner and null member.** Crash frame `USTSquadLeaderComponent::OnPreSave_Implementation+0x6d` calls `GetNPCGuid` on a null element of the array at `this+0x40/+0x48`. Its `this` is not the component: it is the component's `ISTSavableComponentInterface` subobject at component+0xB8. `OnComponentFullyLoaded_Implementation` confirms this, because it does `lea -0xb8(%rcx),%rbp` before it calls `SetInSquad`. The array is therefore component+0xF8/+0x100, the live squad `TArray<ASTNonPlayableCharacterBase*>`. `GetSquadMembers`, `AddToSquad`, `SetInSquad` and `CheckForSquadRemovals` read the same offsets. With rbp=0x66D2A078 in the playtest dump, the component is at 0x66D29FC0. Its owner is a player pawn: `ASTPlayerCharacter` keeps the component at +0x868 (`GetSquadLeaderComponent`). The playtest did not log which pawn it was.
  - **Why a null can sit there.** `CheckForSquadRemovals` (run from `TickComponent`) checks for null and for the GC pending-kill flag. `OnPreSave` checks neither. A squad member that GC nulls (the slot is a UPROPERTY) is therefore fatal only inside a save. On the client, a host-replicated NPC that joined a squad and is then destroyed leaves exactly such a slot. I did not reproduce the null state. In every run the diagnostic squad line logs `squad of BP_PlayerCharacterMI_C (client-own): ... 0 members, 0 null`, and the walk covers every `ASTPlayerCharacter`.
- **Client save writes and loads.** In co-op the client never loads its own checkpoint. Card 048 shows its pawn save id never matches, card 045 restarts from the host, and card 113 rejoins from the host after travel. Nothing reads the client's save, so the client needs nothing the save engine writes. All client saves seen come through `PerformSave` from `Save_CheckPoint` (caller exe+0x52186DF).
- **Frame #5 (`ubwcoop+0x40B10`)** is the return address inside `detour_process_event` (death.cpp:271), a plain passthrough to the original ProcessEvent. The mod is not the cause.

### Fix

`mod/src/ubw/save_c121.cpp` hooks two save engine calls.

- **`USTSaveSystemLibrary::PerformSave` (0x05211400).** On the client in a co-op session (`g_client_session`), the call is skipped and logged (`card121: client ... save engine save skipped`). The host passes through unchanged and logs `card121: host ... save engine save runs`.
- **`USTSaveSystemLibrary::Load` (0x0520A580).** On the client the call is skipped, sets `*file_io_success = false` and logs. The host passes through.

The hooks are wired in hooks_ubw.cpp (not diag-gated), with symbols in symbols.txt and offsets.generated.h. Card 119's bare-copy skip is removed from world_c98.cpp, and `c98_arm_map` now calls `c121_arm_map` to reset the per-map line cap. One mechanism covers every saveable class, because all pre-save callbacks run only inside a save.

### Sweep (step 3)

- **Save entries.** `Save_CheckPoint`, Manual, Quicksave, the deferred retries, ReturnToMainMenu, TriggerSave and CheckPointSaveZone all go through `PerformSave`, so all are gated on the client.
  - Not gated, and kept: `TrySaveStateToFile`/`SaveGameToSlot` (level completion progress) and `UGameUserSettings`. Neither runs a pre-save callback.
- **Load entries.** `execLoad`, `LoadLatestSave`, `Load_Quicksave` (from `ASTPlayerController::HandleInputQuickload_Pressed`, the quickload key) and `USTDebugSaveTracker::Load` all go through `Load`, so all are gated on the client. Callers see `file_io_success` false.
- **Pre-save callbacks.** `PrepareForSave` called from `Save_ProcessBatch` runs them, only inside a save, so none runs on the client now. Implementers:
  - `OnPreSave_Implementation`: `ISTSavableComponentInterface` (base); `USTSquadLeaderComponent` (player pawns, on the client: this crash); `USTGoreComponent` (pawns and enemies, on the client); `USTHitVolumeComponentBase` (enemies, on the client); `USTOutOfBoundTriggerComponent` (level volumes, on the client).
  - `PreSaveGame_Implementation`: `ASTProjectileBase` (on the client).
- **Post-load callbacks.** These run only from `Load`, which is now gated on the client:
  - `OnComponentFullyLoaded_Implementation`: SquadLeader, Gore, HitVolumeBase, HitVolumeGrenadeHole, OutOfBoundTrigger, DitherEffect;
  - `OnPostLoad`: the `ASTGorePart*` classes, `ASTMountableTurret` and `ASTPlayerController`.
- **Other squad array walkers.** `GetSquadMembers`, `CheckForSquadRemovals` and `AddToSquad` users either check for null or only append. Only `OnPreSave` dereferences without a check, and it is now host-only. A null on the host is not possible, because the host owns the NPC lifetimes.
- **Client-local level-script effects.**
  - Spawns and encounters: gated on the client by card 006 (guards.cpp "client encounter").
  - Objectives: card 100 skips client `CompleteObjective` (seen in the run as `skipped local CompleteObjective Assault_SO4`).
  - Checkpoints and saves: this card.
  - AI squad changes: client copies are replicated NPCs, and the only crash path was the save above.
  - Timers: none found that reach a crashable callee.

  No further fix was needed.

### Proof run

Run tag c121i, DLL **07d9a319e58cf37b** (`sha256sum | cut -c1-16`). Command: `SCENARIO=tools/scenarios/c121-lever-save.txt FAST=1 DEPLOY=1 HOST_EXTRA_ENV="UBWCOOP_AUTODROPSHIP=1" tools/run_pair.sh c121i 300`. FED_C1M1, FAST=1.

The client walks presence volumes C_2, C_6 and C_9 (Assault_SO2..SO4 added). The host then warps the client's server copy to the switch, and the client aims at the panel (origin z+100) and presses E.

- Client lever: `95556222 card050: client Use press: interactor target BP_PanelSwitch_WithoutScreen_C_0 (BP_PanelSwitch_WithoutScreen_C), placed 1, relayed class 1: UseName sent`
- Client objective: `95556222 card049: client CompleteObjective(Assault_O1) ... ctx FED_C1M1_Scripting_C`
- Client save skipped: `95556223 card121: client t=95556223 save engine save skipped (PerformSave, flag 1, caller exe+0x52186DF)` followed by `squad of BP_PlayerCharacterMI_C (client-own): component 0000000003BD6A00, 0 members, 0 null`
- Host objective: `95556230 card049: host CompleteObjective(Assault_O1)`, sent to 1 pc
- Host save: `95556231 card121: host t=95556231 save engine save runs (PerformSave, flag 1, caller exe+0x52186DF)`. An earlier host checkpoint save at 95544078 also ran.

Run results:

- Length: 94 s, ended by scenario; drive rc 0.
- Exit codes: host rc 0, client rc 0.
- ERROR/FATAL: 0 on both sides.
- CRASH lines: 0 on both sides.
- New crash dumps: none.
- The client's map loaded 4 / detach 1 is its return to the title screen after the host quit.

Earlier runs on this card (c121b to c121h) also showed host save runs, client save skipped and no crash. They failed only on scenario aim: the switch origin is at floor level, so `look actor` with dz 0 found no interactor target.

Report complete.

## Senior review (2026-11-06): one fix before acceptance

The measurement, the sweep and c121i check out (hash 07d9a319e58cf37b, client "save engine save skipped" at
95556223, host "save runs" at 95556231, no CRASH). Gating at PerformSave and Load is the right single mechanism, and
removing card 119's skip is fine.

Defect: `USTSaveSystemLibrary::PerformSave(UWorld*, FSTSaveGameMetadata&, TDelegate<void(bool, const
FSTSaveGameMetadata&, USTCampaignSlotsSaveGame*)>&, bool)` takes a completion delegate. The client skip returns
without ever executing it. Any caller that waits for that delegate now waits forever on the client. The report
lists ReturnToMainMenu among the callers, so a client that quits to the main menu from the pause menu may hang there.
Saving-in-progress state that the caller sets before PerformSave (a saving flag, the save icon, a deferred retry)
may also stay set, so later saves are never attempted.

Do:

1. For each PerformSave caller in your sweep list (Save_CheckPoint, Manual, Quicksave, the deferred retries,
   ReturnToMainMenu, TriggerSave, CheckPointSaveZone), name what its delegate does, and any state it sets before the
   call and clears in the delegate.
2. Fix in `detour_c121_perform_save`: on a skipped client save, complete it the way a failed save would. Execute the
   delegate if it is bound, with success false (or true, if a caller treats false as an error to show the player;
   say which and why). Use the TDelegate execute the game itself uses at the end of the real save, not a hand-built
   call. Log it in the skip line (`delegate executed` or `unbound`).
3. Proof, one short run: FED_C1M1, FAST=1, both in. The client opens its pause menu and quits to the main menu
   (the menu click, not a kill). The client reaches 3D_Frontend, and the log shows the skip line with its delegate
   result. rc 0, ERROR/FATAL 0, no CRASH, no new dump. If quit to the main menu was already fine in a run without
   the fix, say so, but the delegate still gets completed.

Append the result under this section and end with "Report complete." again.

### Result (junior, 2026-11-06)

**1. Callers and their delegates.** None of them sets state before PerformSave that its delegate clears.

| Caller | PerformSave call | Delegate (lambda vtable, Execute) | On false | On true |
|---|---|---|---|---|
| `USTSaveSystemLibrary::Save_CheckPoint` (0x052180E0) | exe+0x52186DA | 0x14814E3B8, 0x051FA890 -> 0x051F2690 | `testb dl; je ret`: nothing | locks the slot tracker (game instance +0x1C8, crit section +0x440), `USTCampaignSlotTracker::AddSaveToSlot` |
| `Save_Manual` (0x05219800) | exe+0x5219E38 | 0x14814E4F0, 0x051FA860 | nothing | `AddManualSaveToSlot` (0x051F2D60) |
| `Save_Quicksave` (0x0521A300) | exe+0x521A8DE | 0x14814E488, 0x051FA880 -> 0x051F1EB0 | nothing | `AddSaveToSlot` |
| PerformSave's own deferred retry (timer lambda, while level streaming is busy) | exe+0x51F2842 | a copy of the caller's delegate | goes through the detour again | same as the caller's |

The other callers reach PerformSave only through these three and use the same lambdas:
- `USTGameInstance::ReturnToMainMenu`+0x200 calls Save_CheckPoint.
- `ASTSaveableLevelScriptActor::TriggerSave`+0x214 calls Save_CheckPoint. This is the lever path.
- `ASTCheckPointSaveZone::TrySave`+0x2D1 calls Save_CheckPoint.
- `ASTPlayerController::HandleInputQuicksave_Pressed`+0x30 calls Save_Quicksave.
- The `execSave_*` thunks and `USTDebugSaveTracker::Save`+0xCA call these too.

The save engine's in-progress state is set inside `ASTSaveSystemEngine::Save`, past PerformSave, so the client never sets it. The game itself already drops the delegate in one case: PerformSave with no save engine or no world returns without executing it.

`ReturnToMainMenu` (0x050FFD10) never waits on the delegate:
- It calls Save_CheckPoint only past its own guards: GetSTPlayerController non-null, then game instance +0x200's interface (+0x28) vfunc +0x10 true, then controller +0x7B8 == 0, then GetSTPlayerCharacter non-null.
- It then always calls `UGameplayStatics::OpenLevel` (exe+0x50FFF6C), whether or not the guards passed. A guard that fails jumps straight to the OpenLevel at exe+0x50FFF36.
- So quit to the main menu could not hang on the delegate, even before this fix.

**Success value: false.** No caller shows the player an error on false. All three lambdas do nothing on false. True would record a slot entry for a save that never happened, with a null slots object.

**2. Fix.** `mod/src/ubw/save_c121.cpp`, new function `c121_complete_skipped_save`, called from the client branch of `detour_c121_perform_save`.
- It follows the sequence `FSaveTaskThread::Run` uses at the end of a real save (exe+0x521501A):
  - size at +8 != 0 and instance at +0 != null;
  - then the instance's IsSafeToExecute (vtable +0x30);
  - then its Execute (vtable +0x58) with (instance, false, metadata, null slots).
- The skip line now ends with the result: `delegate executed (success 0)`, `unbound`, or `not safe to execute`.

For the menu click, `ui click <prefix>` was added: `cmdfile.cpp`/`.h`, `testrun.cpp` `c121_ui_click`, `tools/drive.py` doc.
- It works like card 067's `ui continue`. Injected InputKey never reaches Slate, so it calls the BP function behind the button through ProcessEvent.
- The target is the topmost widget in the map's WB_Stack_C that has a parameterless function with that name prefix.
- Build: 0 warnings. DLL f47d4dba55177c72.

**3. Proof.**

c121q (quit only; `tools/scenarios/c121-quit-menu.txt`, since removed):
- Client: Escape, then `ui click BndEvt__WB_Pause_Menu_QUIT_button`. That ran `BndEvt__WB_Pause_Menu_QUIT_button_K2Node_ComponentBoundEvent_12`, and the confirm went on the stack ("stack element 0 of 2" on the next click).
- Then `ui click ConfirmQuitToMainMenu`. The client loaded 3D_Frontend at 96132888.
- It logged no card121 save line: on the client, ReturnToMainMenu's guards did not pass, so it went straight to OpenLevel and never called Save_CheckPoint. I did not find which guard failed.
- Quit to the main menu works and makes no client save.
- 134 s, host/client rc 0/0, FATAL/ERROR 0, CRASH 0, no new dumps. Drive rc 1 only because the scenario then waited for a skip line that never came.

The delegate therefore needs a save that does reach PerformSave on the client, so the proof run combines the lever save with the menu quit.

c121r (`SCENARIO=tools/scenarios/c121-save-quit.txt FAST=1 DEPLOY=1 HOST_EXTRA_ENV="UBWCOOP_AUTODROPSHIP=1" tools/run_pair.sh c121r 300`, FED_C1M1, both in):

```
client 96328825 card050: Use press: interactor target BP_PanelSwitch_WithoutScreen_C_0 ... UseName sent
client 96328826 card049: CompleteObjective(Assault_O1)
client 96328826 card121: client save engine save skipped (PerformSave, flag 1, caller exe+0x52186DF): delegate executed (success 0)
host   96328834 card121: host save engine save runs (PerformSave, flag 1, caller exe+0x52186DF)
client 96335247 card121: ui click: ProcessEvent(WB_Pause_Menu_C, BndEvt__WB_Pause_Menu_QUIT_button_K2Node_ComponentBoundEvent_12), stack element 0 of 1
client 96337450 card121: ui click: ProcessEvent(WB_Pause_Menu_C, ConfirmQuitToMainMenu), stack element 0 of 2
client 96337947 card052: world 3D_Frontend (net mode 0)
```

- 88 s, scenario passed (drive rc 0).
- Host/client rc 0/0. FATAL/ERROR 0, WARN 0/0. No CRASH line. No new crash dumps.
- The client loaded 3D_Frontend 0.5 s after the confirm.

Report complete.
