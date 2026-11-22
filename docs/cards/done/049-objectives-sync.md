# Card 049: Co-op objectives on the client

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: sent 2026-11-04. Picked by the user.

Read `docs/STATE.md` first, then the card 048 Report and Senior review (objectives), and the card 026 channel comment in `mod/src/hooks.cpp` (~6218, "Card 026: mod channel"). Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

Card 048 found that objectives are host-only state.
- The level Blueprint calls `USTObjectiveUtilityLibrary::AddObjective` and its variants on the host only: 15 `AddObjectiveObject` calls per map on the host, 0 on the client, from the first map load on.
- `USTLevelObjectiveSubsystem` is per world and nothing in it replicates.
- So the client's OBJECTIVES panel is always empty, while the host shows three.

The fix: the host mirrors every objective change to the client over the mod channel, and the client replays it through the same library functions in its own world.

## Static facts (senior, 2026-11-04)

Each of these is a static `USTObjectiveUtilityLibrary` function whose first argument is `UObject* WorldContextObject`. The Blueprint reaches each one through its `exec` thunk, so a detour on the native function catches the level-script call.
- `AddObjective` 0x0522A6D0 `(ctx, FName id, const TArray<AActor*>&, bool, bool, bool, bool)`
- `AddSubObjective` 0x0522A890 `(ctx, FName parent, FName id, bool, const TArray<AActor*>&, bool, bool, bool, bool)`
- `AddCountObjective` 0x0522A170 `(ctx, FName, const TArray<AActor*>&, int32, bool, bool, bool, bool)`
- `AddCountObjective_SubObjectives` 0x0522A2A0, `AddCountSubObjective` 0x0522A3D0, `AddCountSubObjective_SubObjectives` 0x0522A550 (read the mangled names in `text_publics` for the argument order).
- `CompleteObjective` 0x05233710 `(ctx, FName)`, `CompleteAnyObjective` 0x052336B0 `(ctx, FName)`, `CompleteSubObjective` 0x05233780 `(ctx, FName, FName)`.
- `IncrementObjectiveCount` 0x05240FB0 `(ctx, FName)`, `SetObjectiveVisibility` 0x0524D770 `(ctx, FName, bool, bool)`, `CalloutObjective` 0x052322F0 `(ctx, FName)`.
- Subsystem queries: `GetAllObjectives` 0x05158F30, `GetObjectiveByID` 0x0515AD70, `DoesObjectiveExistForID` 0x05153100.
- Card 048 saw `AddObjective`+0x111 (0x522A7E1) and `AddSubObjective` (0x522A9F8) in the host's call chains.
- The bool parameter names are not in the mangled name. Read them from the reflected `UFunction` (its `FProperty` chain) once, and log them.
- FName indices are per process, so send the objective id as **text**, never as an FName index. `g_fname_ctor` (`kFNameAdd`) rebuilds the FName on the client.
- Channel frames today have a fixed argument count per opcode, with values 1..0x7FFF (`kChanArgs`, `chan_parse`). That cannot carry a name.

## Do

1. **Audit on the host (log only, one run).** Detour every function above. Log each call with:
   - the id(s) as text, the bools and the count;
   - the number of actors in the array;
   - the caller.

   Log the bool parameter names once. On the client, log whether `USTLevelObjectiveSubsystem` and an `ASTLevelObjectiveManager` exist in its world after the map load. Put a short table of the calls seen on FED_C1M1 in the Report, then go on; do not stop.
2. **A variable-length opcode.** Add `ChanOp::Objective`, the one opcode whose argument count is not fixed:
   - Mark it with -2 in `kChanArgs`. Its arguments are: kind (which library function), flags (the bools as bits), count, name length L1, name length L2 (0 when there is no second name), then L1 + L2 characters.
   - `chan_parse` checks Num against 5 + L1 + L2 for this opcode only, and keeps the existing rules: every value is sent +1 and stays below `kChanArgMax`.
   - Cap each name at 64 characters. Drop and log any name that is longer or not ASCII.
   - Keep the frame buffer static; do not allocate per send. Update the card 026 comment to describe the new opcode.
3. **Live mirror (host to client).**
   - Every host detour calls the original first, then sends one `Objective` frame to each remote client PC (`chan_to_client`).
   - The client's receiver builds the FNames and calls the **same native library function**, with its own PC as `WorldContextObject` and an empty actor array.
   - Log each replay with the subsystem's objective count before and after.
   - Guard against echo: the client's own detours must not send anything; check `g_client_session`.
4. **Journal for a late join or a reconnect.**
   - The host keeps an ordered journal of the frames sent since the last map load (cap 128; log and stop recording when it is full). Reset it on the per-map reset (card 045).
   - When a client's first `Pong` of a map arrives on the host (card 026 ping, every 5 s), the host replays the whole journal to that client once.
   - Live frames sent before that first `Pong` may also be in the journal. So the client must ignore an Add for an id it already has (`DoesObjectiveExistForID`) and log it.
5. **Checkpoint.** After the host's `SetObjectiveMapPostSave`, send the snapshot:
   - Clear the journal and rebuild it from `GetAllObjectives`: id, parent id (if the object holds one), completed, count, visible. Log the `USTLevelObjective` field offsets you use, and how you found them.
   - Send the snapshot as Add frames, then Complete, Count and Visibility frames.
   - The client keeps objectives it already has, and only applies completion, count and visibility to them.
   - Log every objective the client has that the host's snapshot lacks; do not remove it in this card.
6. **Runs.**
   - `card049x` (two players): the client panel must show the same objectives as the host after the map load. Then trigger one objective completion; the shooting range or tactical support training in FED_C1M1 should be quickest. Then RESTART CHECKPOINT. Take shots of both panels at each of the three points.
   - `card049r`: the client disconnects and rejoins (card 045 tooling); the panel is filled again from the journal.
   - Solo `card049s`: no frames are sent, and the host log is unchanged apart from the audit lines.

## Done when

- The client's OBJECTIVES panel matches the host's after the map load, after a completion, and after a checkpoint (shots of both panels at each point).
- After a rejoin the client panel is filled from the journal.
- The client's replay has no game-side effect beyond its panel: no mission-complete, no level transition, no extra save. Check for `CheckAllObjectivesComplete` and save calls on the client and report what you saw.
- rc 0 on all runs, no new dump, the build is clean with `/W4 /WX`, and every function is 60 lines or fewer.

## Risks

- The library functions may need `ASTLevelObjectiveManager` or a data table that exists only on the server. Step 1 checks the client's world. If a replayed Add creates nothing on the client, stop and report, with the subsystem state and the return path.
- Completing an objective on the client could trigger level logic there (e.g. `ObjectivesCompletedGate`, or a mission end). Log it. If it fires anything beyond the UI, report; do not suppress it blindly.
- Actor arrays (markers and waypoints) are not sent in this card. The panel text works without them; the markers are a follow-up.
- A long journal replay may be one burst of reliable RPCs. 128 frames of fewer than 140 characters is about 36 KB. Log the replay time.

## Addendum (senior, 2026-11-04): replay path checked statically

- `AddObjective` 0x0522A6D0 has no authority or net-mode check. It gets the subsystem from the context (`USTLevelObjectiveSubsystem::GetSubsystem`), returns early if `DoesObjectiveExistForID` is true (so a duplicate Add is already a no-op in the game), calls `USTLevelObjective::CreateNewLevelObjective` 0x05151670 (a `NewObject` in the transient package), then calls `AddObjectiveObject`. A second path calls `GetSTHUD` and `ASTHUDGameplay::HUDAddScreenEffect`.
- `CreateNewLevelObjective` uses the actor array for the markers only: it calls `ASTLevelObjectiveMarker::CreateASTLevelObjectiveMarker` 0x05150E20 per actor, after finding a `USTSaveComponent` on it. So an empty array means no markers, and no failure is expected.
- `AddObjectiveObject` 0x0514AF00 only fills the map and binds delegates, then broadcasts. No server-only manager is needed for an Add. Step 1's world check is still worth logging, for Complete and Count.
- For the later markers card: the client could resolve each actor by its path name. Level-placed actors keep the same path on both machines.
- `CompleteObjective` 0x05233710 looks the objective up by id. If its virtual `IsComplete` (+0x2B0) is false, it tail-calls `USTLevelObjective::CompleteObjective` 0x05150C70 (+0x2B8). That function broadcasts the objective's multicast delegates and calls `ObjectiveUpdated` 0x05161640. It has no net-mode check. On the client, side effects can only come from whatever is bound to those delegates there: the HUD, or level Blueprint code if the level script binds them on the client too. Log the bound delegate count before the first client Complete.

## Report

Status: done, with two open differences after a checkpoint (see "After RESTART CHECKPOINT").

### Step 1: audit (FED_C1M1, host)

Reflected bool names, read once from each `UFunction`'s `FProperty` chain:
- `AddObjective(WorldContextObject, InObjectiveID, ActorsToMark, bInShouldShowMarkers, bShouldShowInObjectivesList, bShouldPopupOnAdd, bShouldPopupOnCompletion)`.
- `AddSubObjective(…, InOwningObjectiveID, InSubObjectiveID, bUseParentsData, ActorsToMark, bShouldShowMarkers, bShouldShowInObjectivesList, bShouldPopupOnAdd, bShouldPopupOnCompletion)`.
- `AddCountObjective(…, ActorsToMark, TargetCount, 4 bools)`; `AddCountSubObjective(…, bUseParentsData, ActorsToMark, TargetCount, 4 bools)`.
- `SetObjectiveVisibility(…, ObjectiveID, bShowMarkers, bShowInObjectiveList)`.

Calls per map load (all from `FED_C1M1_Scripting_C`, depth 0):

| Function | Calls | Ids | Bools | Actors | Caller |
|---|---|---|---|---|---|
| AddObjective | 3 at load, +1 at the first player death | Assault_O1, ShootingRange_O1, TacSupport_01; Traitor_O1 (bools 1100) | 1101 | 0 | exe+0x5081B44 |
| AddSubObjective | 10 | Assault_SO0, ShootingRange_SO0, TacSupport_SO0, SO2..SO7, SO41 | 01100 | 1 | exe+0x5081E20 |
| AddCountSubObjective | 1 | ShootingRange_O1 / ShootingRange_SO3, TargetCount 10 | 00000 | 1 | exe+0x50815D2 |
| SetObjectiveVisibility | 17 | the SO0s (11), TacSupport_SO2..SO7/SO41 (00), the three O1s at the death (00) | | | exe+0x5083584 |
| CompleteSubObjective | 0 from the level; 1 from the test | TacSupport_01 / TacSupport_SO0 | | | |

No AddObjective-count variants other than AddCountSubObjective, no Increment, no Callout, no CompleteObjective in the first minutes of FED_C1M1.

World check on both sides after a map load: one `STLevelObjectiveSubsystem` and one `ASTLevelObjectiveManager`. The client's subsystem was empty before card 049; with the mirror it holds the same 14 or 15 objectives as the host.

IsSubobjective finding: the library's `CompleteObjective` 0x05233710 returns early for a sub-objective. Its virtual check at +0x2B0 is `IsSubobjective` 0x0515FCD0, not `IsComplete` as the addendum assumed. A sub-objective is completed through `CompleteSubObjective(parent, id)`, so the test does that.

### Steps 2 to 5: what was built

- `ChanOp::Objective` (-2 in `kChanArgs`): kind, flags, count, L1, L2, then L1 + L2 name characters. Every value is +1 and below `kChanArgMax`. Names are capped at 64 ASCII characters; a longer or non-ASCII name is logged and not sent. The frame buffer is static.
- Live mirror: every host detour runs the original first, then sends one frame to each synced remote PC. The client replays through the same native function with its own PC as context and an empty actor array. The client never sends (`g_client_session`).
- Journal: cap 128, reset at each real map load. It is replayed to a PC on its first Pong of the map. The client skips an Add for an id it already has (`DoesObjectiveExistForID`) and logs it.
- Checkpoint: after `SetObjectiveMapPostSave` the host rebuilds the journal from the objective map and sends SnapshotBegin, Chapter, the Adds (with a host-complete bit), Complete, SetCount and Visibility frames, then SnapshotEnd. The client keeps the objectives it has, applies completion, count and visibility, and logs every objective the host lacks.
- `USTLevelObjective` offsets: id +0x58 (`GetObjectiveID` 0x030E5E80), marker +0x68 (`UpdateVisibility` 0x05176C50), complete +0x69 (`IsComplete` 0x019E5C40), in list +0x6A (`GetShowInObjectiveList` 0x02A30BD0), parent +0x70 (`GetParentObjective` 0x050E9A30), count target +0x98 and current +0x9C (`GetTargetCount` 0x050E9AF0, `GetCurrentCount` 0x050E9AD0). The subsystem's TMap is at +0x60 with stride 0x18 (`GetAllObjectives` 0x05158F30). Each offset was read from the disassembly of these getters.

### Why the client panel was empty, and the fix (new Chapter frame)

The first runs mirrored everything, and the client's objective map and its panel widgets matched the host's on all 211 reflected properties. The client panel still showed only the OBJECTIVES header.

The cause: the item widget's texts come from the chapter's DataTable. `USTLevelObjectiveSubsystem::GetLevelObjectiveDataTable` 0x0515A4C0 reads the chapter at game instance +0x410 and the level at +0x414. A net client's game instance keeps -1/-1 (the card 021 log line "chapter -1 level -1"). So `GetDataTableRowByID` 0x05159540 returns empty text, and the item Blueprint collapses ItemHeading, ObjectiveDescription and BulletPoint.

The fix is a new mirror-only kind, `Chapter` (count = chapter, flags = level). The host sends it before the first Add of each map, and right after SnapshotBegin, so it is first in every journal. The client writes both values into its game instance and logs before and after ("chapter -1 level -1, host 0 0: set").

Wider effects of the client now having a chapter (seen in the logs, not suppressed):
- `AssignStartingInventories` on the client now sees chapter 0. Its later calls give the client's own pawn `Weapons.Unarmed`, like the host, instead of the empty loadout ("weapons 0:") it got before. In card049r2 the client still showed its Morita Mk1, because its pawn was set up before the frame arrived.
- Anything else that reads game instance +0x410/+0x414 on the client now sees real values. That includes the card 046 chapter kill height, which was -3.4e38 on the client, so the card 046 suppress path can now act there. Not exercised in these runs.

### Runs

| Run | What | rc host/client | Dumps |
|---|---|---|---|
| card049x9 | two players: load, completion of TacSupport_SO0, RESTART MISSON, RESTART CHECKPOINT | 0 / 0, 245 s | none |
| card049r | late join attempt (120 s delay): the client joined before the host's Adds, so its journal had 0 frames | 0 / 0 | none |
| card049r2 | late join (180 s delay), 45 s after the host's Adds | 0 / 0, 226 s | none |
| card049s | solo; ended at the first world check, before any Add | 0, 128 s | none |
| card049s2 | solo, to the last load-time call (28 library calls audited) | 0, 189 s | none |

Solo (card049s2): no "host sent" line and no journal replay, so no frame was sent. Apart from the card 049 audit lines, the only new line is "card049: host chapter frame: chapter 0 level 0": the frame is recorded in the journal and sent to 0 PCs. WARN 1 is the existing autodropship unpause line.

Earlier diagnosis run card049x7 crashed both sides (rc 3, dumps `UECC-Windows-C6072274465964CB07A6BBB70CA9F433_0000` and `UECC-Windows-C725F8D649F7AE08A35571A406CA5593_0000`). The cause was my panel diagnosis code: it read a `DescriptionPanel` CanvasPanel slot at item-widget offsets. A class-name guard fixed it, and x8 and every later run had rc 0 and no dump. The panel diagnosis (`c49_panel_watch`, a widget read every 250 ms) now runs only with `UBWCOOP_C49_TEST` set.

Shots: `logs/card049-shots/x9-montage.png` (rows: load, load after RESTART MISSON, completion, checkpoint; host left, client right) and `r2-montage.png`. The host's load and completion shots land in the intro movie or the mission brief, so for those points the per-item widget dump of both panels (visibility of ItemHeading, ObjectiveDescription and BulletPoint per item, 1.5 s after each panel change) is the comparison.

### Do the panels match?

- After the map load: yes. The item dumps of both sides list Assault_O1, ShootingRange_O1, TacSupport_01 in the same order, with the same sub-items and widget states. The client shots show ASSAULT COURSE / SHOOTING RANGE / TACTICAL SUPPORT TRAINING with their descriptions.
- After the completion: yes. The host and client item dumps (13 lines) are identical. The client shot shows TACTICAL SUPPORT TRAINING with its sub-line gone, as on the host.
- After RESTART CHECKPOINT: same texts, two differences.
  - Order: the host's restore rebuilds its list as TacSupport, ShootingRange, Assault. The client keeps Assault, ShootingRange, TacSupport.
  - Traitor_O1: the level adds it at the player death, on both sides. The host's restore drops it; the client keeps it and shows TRAITOR! ("You are a traitor to the Federation and will be executed!"). The snapshot logs it: "client objective Traitor_O1 is not in the host's snapshot (kept)", as this card asked. Removing it, and matching the order, needs a client-side list rebuild or a remove path: a follow-up card.
  - The client also keeps a collapsed TacSupport_SO0 widget that the host's rebuilt list lacks. It is not visible.
- After a late join (card049r2): yes. The host replayed 29 of 29 journal frames in 0.152 ms on the client's first Pong. The client applied all of them within 5 ms (Chapter first). Both item dumps (14 lines) are identical, and the client shot shows the three objectives with text. The RESTART MISSON reconnect in x9 is also a rejoin, but there the journal was empty at the first Pong (map reset), and the live frames filled the panel.

### Side effects on the client

- `CheckAllObjectivesComplete`: hooked on both sides, 0 calls on either side in x9 and r2.
- Saves: one `ASTSaveSystemEngine::Save(flag 1)` on the client at the replayed completion (t=572763514), with the same caller chain as the host's save 19 ms earlier (exe+0x521F166 < 0x521C6C5). It comes from `OnLevelObjectiveComplete`, which has 5 bound delegates on the client, as on the host:
  - [0] and [1] `BP_CheckPointSaveZone_C.Bound Save`;
  - [2] `FED_C1M1_Scripting_C.On Level Objective Complete` (level script logic runs on the client);
  - [3] `WB_Objective_Popup_Manager_C.OnLevelObjectiveComplete_Event`;
  - [4] `WB_Objective_List_C.NonAddedObjectivesUpdate`.

  So a replayed completion makes one extra save on the client. This is reported, not suppressed. No mission complete and no level transition were seen.
- Per objective: `OnUpdated`, `OnCompleted` and `OnCallout` have 1 bound each.

### Build and code

- `cmake --build build`: clean with `/W4 /WX`.
- Every function is 60 lines or fewer, checked with a script over `mod/src/hooks.cpp`.
- The diagnosis functions (`c49_panel_watch`, `c49_panel_lines`, `c49_item_lines`, `c49_prop_line(s)`) stay in, gated to the test.

## Senior review (2026-11-04)

Accepted.

Checked:
- Runner summaries: x9, r, r2 rc 0/0; s, s2 solo rc 0; "new crash dumps: none" on all. The `UECC-*` folders under `Saved/Config/CrashReportClient` are config folders, not dumps.
- The x9 montage: the client panel shows ASSAULT COURSE / SHOOTING RANGE / TACTICAL SUPPORT TRAINING with descriptions at load, the completed TacSupport sub-line gone, and after the checkpoint the reversed order on the host and TRAITOR! on the client only, as reported.
- Logs: the host adds `Traitor_O1` from `FED_C1M1_Scripting_C` (level script) at the death, so the TRAITOR text on the client is a real level objective mirrored, not a mod artefact. The client's `chapter -1 level -1, host 0 0: set` line is there.
- No function over 60 lines in `mod/src/*.cpp`.

Follow-ups (not blocking the playtest):
- Card 053: after a checkpoint snapshot, the client rebuilds its objective list to the host's (remove `Traitor_O1` and stale entries, host order).
- The Chapter frame changes client behaviour wherever +0x410/+0x414 is read. Card 050 must log `AssignStartingInventories` on the client after the frame, and the card 046 KillZ on the client is now live: watch for client deaths on high ground in the playtest.
- The extra client `Save(flag 1)` on a replayed completion writes the client's own save only; leave it.
