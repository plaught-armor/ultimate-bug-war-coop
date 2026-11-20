# Card 067: Close the host's mission brief in scripted runs

Status: sent 2026-11-04 23:15 to junior ultimate-bug-war-coop-bb, after card 064 was accepted.

Read `docs/STATE.md`, the card 063 Report ("Open gap") and the card 015 and 032 notes on `UBWCOOP_AUTODROPSHIP`. Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why

With `UBWCOOP_AUTODROPSHIP=1`, the host's world is unpaused, but the brief and "press any key" screen stays drawn. Every host shot shows the brief movie, not the game (`host-c63f-client-holds-rifle.png`). Card 063 tried injected `InputKey` Enter and SpaceBar and posted `WM_KEYDOWN`; none closed it.

## Senior research (2026-11-04, static, name maps only)

- `WB_Loading_Screen_Final` (`/Game/Blueprints/UI_Aloft/Gameplay_HUD/`) holds `PRESSANYKEYTOCONTINUE`, `ContinuePrompt`, `PressToContinue`, `LoadingFinished` and `OnFinishedLoadingLevel`, and overrides `OnKeyUp` and `OnMouseButtonDown`, which lead to `Continue`. These are Slate focus events. They reach only the widget that has keyboard focus, never through `APlayerController::InputKey`. That is why card 063's injected keys did nothing. The posted `WM_KEYDOWN` probably failed because the window was not focused (unchecked).
- `WB_Stack` (`/Game/Blueprints/UI_Aloft/WidgetComponents/`) is the widget stack that holds the `RequestGamePause` card 015 releases. It has `Push Widget`, `Push Widget from class`, `Pop`, `GetCurrentWidget`, `Is Empty`, `Widget Stack` and the `StackEmpty` delegate. Each pushed child is a `WB_StackElement`.
- `WB_MissionBriefVideo` plays the brief movie (`MediaPlayer`, `Video Ended`, `Video Finished`, `OnEndReached`). It is probably a child pushed onto the stack, or a child of the loading screen.
- What the close does in the game's own flow is unknown: which of `Continue`, `Pop` or `StackEmpty` runs, and what listens to `StackEmpty`. STATE notes that "the mission state set by its close is unchecked".

- Added 2026-11-04 23:26 (senior, static, after the card was sent):
  - `USTGameInstance::ShowAsyncLoadingScreen` 0x05103F80 (called from `TransitionToLevel` 0x05109340, `TransitionToLatestLevel` 0x051092A0 and `USTSaveSystemLibrary::Load`) creates a separate world (`CreateNewWorldContext`, `UWorld::CreateWorld`, kept at GameInstance +0x2F8). It constructs a `UUserWidget` there with `StaticConstructObject_Internal`, sets its player context to the first local player, and gives its `TakeWidget` to the engine movie player (`GetMoviePlayer`, three vtable calls). The loading screen widget is therefore outside the game world and the game viewport, and is drawn by the movie player. When listing widgets, log each one's outer world and compare it with GameInstance +0x2F8.
  - `USTGameInstance::HideAsyncLoadingScreen` 0x050EE750 has one caller, `ASTGameplayGameMode::BeginPlay` (0x05117A30, call at 0x05117A8E). It calls `FlushAsyncLoading(-1)`, then movie player vtable +0x50 `StopMovie` and +0x58 `WaitForMovieToFinish(false)` (slots read from the `FDefaultGameMoviePlayer` vtable), then tears down the world at +0x2F8.
  - The movie player has its own input path: `FDefaultGameMoviePlayer::OnLoadingScreenKeyDown` and `OnLoadingScreenMouseButtonDown`. These Slate handlers do not use `APlayerController::InputKey`.
  - The `WB_Loading_Screen_Final` name map has no `Continue` function. It has `On Back`, an event that `WB_StackElement` declares. `WB_StackElement` has `On Back`, the `OnBackPressed` delegate, `Remove Current Menu`, `Pop`, `Set Stack` and `Stack`. `OnKeyUp` probably checks `LoadingFinished` and the `ContinuePrompt` visibility, then runs `On Back` or the parent's `Remove Current Menu`. Step 1's function list decides which.
  - Open: whether the screen still drawn in host shots is the movie player's widget (Hide did not finish, or `WaitForMovieToFinish` returned early) or a second copy pushed on the HUD `WB_Stack`. Step 1 answers this from the widgets' outers. If it is the movie player's widget, `ui continue` may need to finish the movie player: log `IsMovieCurrentlyPlaying` (+0x68) and `IsLoadingFinished` (+0x60) each second.

## Do

1. **Find the widgets (log only, `UBWCOOP_CMD=1`).** Once the host's mission map is loaded:
   - List each live `WB_Loading_Screen_Final_C`, `WB_MissionBriefVideo_C`, `WB_Stack_C` and `WB_StackElement_C` with its outer and visibility. For the stack, also log `Widget Stack` count and `GetCurrentWidget`'s class.
   - List the UFUNCTIONs on `WB_Loading_Screen_Final_C` and `WB_Stack_C`, with name, flags and parameter count, through reflection (`Children` and `UFunction`), so the real name and signature of `Continue` and `Pop` are known before any call.
   - Do one manual run, focused, with a person pressing a key, as the reference. Log the stack count and widget visibility before and after the key, and every `RequestGamePause` and `RequestGameUnpause` (the card 015 log path already has them). This shows what a real close changes.
2. **Command `ui continue`.** On the game thread, call the same function the real key path calls (`Continue` on the loading screen, or `Pop` on the stack, as step 1 shows) through `ProcessEvent`, with no new RVA. It ends `done` when the step 1 reference state is reached (stack empty or the brief widget hidden), or `failed (<reason>)` within 5 s. It must also work when no window has focus.
3. **Autodropship.** If `ui continue` gives the same end state as the manual key, have `UBWCOOP_AUTODROPSHIP` use it in place of the bare `RequestGameUnpause`. That holds only if the pause releases by itself through the real close; check this in the log. Otherwise keep the unpause and run `ui continue` after it. Report which.
4. **Proof.** Rerun `tools/scenarios/c57.txt` with `host ui continue` added after the brief. It passes twice, and `host-<tag>-client-holds-rifle.png` shows the game world with the client's trooper, not the brief.

## Not in this card

The client's own brief (the client has none today), cutscene skipping, and mid-mission "press to continue" screens. List any that step 1 sees, as findings.

## Done when

- c57 with `ui continue` passes twice in a row with no xdotool. Host shots show the game.
- The Report has step 1's widget and function lists and the manual-key reference.
- `logsum` gives ERROR/FATAL 0, both games end with rc 0, and there is no new dump.
- With `UBWCOOP_CMD` unset and `UBWCOOP_AUTODROPSHIP` unset, behaviour is unchanged.
- fnlen reports no function over 60 lines.

## Report

All steps are done. `ui continue` closes the brief through ProcessEvent with no new RVA. UBWCOOP_AUTODROPSHIP now
closes the brief the same way, so host shots show the game. c57 passed twice in a row with no xdotool (c67i, c67j).

### Step 1: what is on screen (log only, `UBWCOOP_CMD=1`)

The code is the card 067 block in `mod/src/hooks.cpp` (`c67_*`). It starts only from live roots: the WB_Stack_C
pause requester inside the pause/unpause detours, and the game instance's held requesters 5 s after a pause. Each
dump is tagged with the map name. It also logs each distinct (class, function) pair that passes through
ProcessEvent for the brief classes. The full lists are in `logs/c67e.host.full.log`.

- **The flow in FED_C1M1 (host):** a new WB_Stack_C (outer BP_GameInstance_C) calls RequestGamePause with flag 1.
  Its "Widget Stack" is `[WB_Loading_Screen_Final_C (Visibility 3), WB_Cutscenes_C (Visibility 4)]`.
  - WB_Cutscenes_C is the brief video. It holds WB_Glyph and the props BinkMedia, PCSkipInput and SkipActionName.
  - WB_MissionBriefVideo_C is never created.
  - With no key pressed, the video plays for about 106 s. Then WB_Cutscenes_C runs "Video Finished" and Destruct.
    The loading screen takes focus (OnAddedToFocusPath, OnFocusReceived), and "PRESS ANY KEY TO CONTINUE" stays up
    forever.
- **Functions** (UFunctions on each class itself):
  - **WB_Cutscenes_C:** CleanupCutsceneInput, Construct, Destruct, ExecuteUbergraph_WB_Cutscenes, two Finished_*,
    InitialiseCutsceneInput, IsKeyOfAction, OnAnalogValueChanged, "On Back", OnKeyDown (3 parms, 312 bytes),
    "On Media Opened", OnMouseButtonDown, OnMouseMove, "On Video Ended" (Skipped: Bool, 1 byte), Pause, Play,
    SequenceEvent, "Show Prompt", "Show Video", Tick, UpdateSubtitles, "Video Ended__DelegateSignature",
    "Video Finished".
  - **WB_Loading_Screen_Final_C:** "Apply Data", Construct, ExecuteUbergraph, "On Back", OnFinishedLoadingLevel,
    OnKeyUp, OnMouseButtonDown, PreConstruct, SetPlatformText, ShowRandomTooltip, Tick.
  - **WB_Stack_C:** Empty, GetCurrentWidget, "Is Empty", OnFocusReceived, Pop, "Push Widget",
    "Push Widget from class", "StackEmpty__DelegateSignature", "Stack Empty".
  - **WB_StackElement_C** (the parent of both screens): ExecuteUbergraph, "On Back",
    "OnBackPressed__DelegateSignature", "Open Menu", "Open Menu from Class", "Remove Current Menu", "Set Stack".
  - Calls from inside a BP to another BP function (Pop, Remove Current Menu) do not pass through ProcessEvent, so
    the ProcessEvent log does not show them.
- **Manual-key reference (c67e):** one focused Return (keydown, then keyup 165 ms later), sent by xdotool from the
  scratchpad `keyref.sh`.
  - On keydown: WB_Stack_C and WB_Cutscenes_C OnPreviewKeyDown run, then WB_Cutscenes_C.OnKeyDown and OnKeyUp,
    then WB_Stack_C.OnKeyUp. WB_Cutscenes_C runs Destruct, and the loading screen takes focus. Then
    WB_Loading_Screen_Final_C.OnKeyChar and WB_Stack_C.OnKeyChar run.
  - On keyup: WB_Loading_Screen_Final_C.OnKeyUp, then its Destruct. WB_Stack_C then calls RequestGameUnpause with
    the stack empty.
  - About 7 s later, WB_Stack_C runs Destruct.
  - **End state:** "Widget Stack" is empty, the WB_Stack_C requester is released (0 requesters), the world is
    unpaused, and both screens are destructed. The brief-after shot shows the game.
- **The senior's questions:**
  - GI+0x2F8 (the loading-screen world) is null all through the brief.
  - Every widget's outer is BP_GameInstance_C. The screen in the host shots is therefore the copy on the HUD stack,
    not the movie player's widget.
  - IsMovieCurrentlyPlaying (+0x68) and IsLoadingFinished (+0x60) were **not** logged. I have no pointer to the
    movie-player instance yet, and these values were not needed once the stack copy was identified.
- **A crash in my first step-1 build (c67a):** it wrote dump `UECC-Windows-B06BF1864A6FDE2ECA39A686A4B19876_0000`
  at 23:21. The cause was widget pointers that were stored before a map change and read after the change freed them.
  The fix is the live-root rewrite described above. There have been no new dumps since then (c67b to c67l).

### Step 2: `ui continue`

- **Where:**
  - `cmdfile` has `Op::Ui`, which parses only `ui continue`.
  - `tools/drive.py` accepts the `ui` verb.
  - The code is `c67_ui_*` in `hooks.cpp`.
- **The stack:** the last WB_Stack_C that requested a pause on this map. It is stored in an FWeakObjectPtr from the
  pause detour and cleared on map change.
- **Each tick:** the command reads the top of "Widget Stack". It calls that widget's function through
  `g_find_function` and `g_orig_process_event`, which adds no new RVA:
  - WB_Cutscenes_C: `On Video Ended(Skipped=true)`, which is the skip.
  - WB_Loading_Screen_Final_C: `Remove Current Menu`, the WB_StackElement close that any key leads to.

  It makes one call per change of top widget, and repeats a call on the same widget only after 1 s.
- **Done:** "Widget Stack" is empty and the stack holds no pause. A remembered stack that has been freed also counts
  as done, logged as "nothing to close".
- **Failed:**
  - within 5 s, giving the top widget, the stack count and whether the pause is held;
  - when the top widget's class has no step;
  - when no WB_Stack_C has paused on this map.
- **c67f** (no autodropship, no xdotool, no focus): `ui continue` was sent while the brief video played.
  - The two calls ran 9 ms apart. WB_Cutscenes_C ran Destruct, and the loading screen took focus.
    WB_Loading_Screen_Final_C ran Destruct, and WB_Stack_C called RequestGameUnpause.
  - The command logged "empty and holds no pause after 2 call(s); world paused 0" and ended `done` 90 ms after it
    was sent.
  - This is the same sequence and end state as the c67e reference.
  - `host-c67f-brief-after.png` shows the dropship flight.

### Step 3: autodropship, which option

`ui continue` gives the reference end state. The bare RequestGameUnpause unpauses the world but leaves the brief on
screen. So `auto_dropship_unpause` now runs the same steps first, through `c67_ui_close`, on the requester it already
reads fresh from the game instance's array. The bare unpause is kept only as the fallback, when the stack still holds
the pause afterwards. That covers a top widget with no step, such as a mid-mission screen on a map that starts on
foot, which keeps the old path unchanged.

This works without `UBWCOOP_CMD`. In c67l (AUTODROPSHIP=1, CMD unset), action 1 logged "closed the stack through
ProcessEvent", and the world unpaused after 1 action. In the c57 runs, autodropship closes the brief before the
scenario reaches it, so `host ui continue` ends `done` with 0 calls.

### Step 4: proof

`tools/scenarios/c57.txt` now has `host ui continue` in place of the four host Enter/SpaceBar presses. The runs used
the scenario header's env and no xdotool.

| Run | Result |
|---|---|
| c67g | passed |
| c67h | failed at line 24 (`host key Two`, then waiting for ConstructWeaponSwitchRequest). See below. |
| c67i | passed in 105 s |
| c67j | passed in 107 s |

- **Why c67h failed:** the spawned bug fight killed the host. The log has "card043: host real death: 4 hits,
  Health -999.0" about 3 s after the bug's gore. WB_PlayerDeathScreen_C was then pushed onto the same WB_Stack_C,
  which paused the game, and it took the key. The fail shot shows "Killed by a Warrior Bug". This is a race in the
  scenario; the brief path was done by then.
- **c67i and c67j:** host rc 0 and client rc 0; logsum gives ERROR/FATAL 0; no new crash dumps.
  - The host's only WARN is the existing "card039: host probe table full (32)".
  - `host-c67j-client-holds-rifle.png` shows the game: the dropship over Camp Keitel, the Skip Flight prompt and
    the Drill Sergeant's dialogue.
- **Env unset (c67k):** UBWCOOP_CMD and UBWCOOP_AUTODROPSHIP unset, capped at 150 s. Host and client rc 0,
  ERROR/FATAL 0, no new dumps. Both logs have 0 `card067`, `autodropship: action` and `cmd: polling` lines.
- **fnlen:** 1241 functions, 0 over 60 lines.

### Findings

- **The client's own brief:** the client shows no mission brief in FED_C1M1. It logs no WB_Stack_C pause on that
  map (its only one is the front end's), and `client-c67j-picked-up.png` shows the game. Nothing is needed now.
  `ui continue` works on either role, because it uses that game's own WB_Stack_C.
- **Cutscene skipping:** `On Video Ended(Skipped=true)` skips any WB_Cutscenes_C on the stack. Other WB_Cutscenes_C
  videos in a mission can be closed the same way, but none were tested. Cutscenes that are level sequences (not on
  the stack) are not covered.
- **Mid-mission "press to continue" screens:** the only other stack widget seen is WB_PlayerDeathScreen_C.
  - Its functions: OnFocusReceived, the bound events RestartMission, LoadLastSave, LoadGame and Exit, "On Back",
    Destruct, Construct, OnLoad, UnbindOnLoad, ExecuteUbergraph.
  - `ui continue` fails on it by design ("no continue step"); a scenario should not close a death screen blindly.
  - Autodropship's fallback still bare-unpauses it on maps that start on foot, as before.
  - Other mid-mission screens are unlisted: none appeared in these runs. A new row in `kC67UiSteps` adds one once
    its class is known.
- **Scenario race (c67h):** the c57 bug fight can kill the host before line 24. It passed in 3 of 4 runs here. If
  it recurs, a scenario fix is to wait for the death screen or give the host god mode.

## Senior review (2026-11-05 00:08): accepted

Checked by the senior, not taken from the Report:

- `logs/c67i.drive` and `c67j.drive` each end `scenario passed`, and the two runs are consecutive. Neither has an `xdotool` line. `tools/scenarios/c57.txt` has `host ui continue` and no host Enter or SpaceBar keys. The xdotool press in c67e was the manual-key reference that step 1 asks for, not part of the proof.
- `logsum`: ERROR/FATAL 0 on both games in c67i, c67j, c67k and c67l. The host's one WARN in c67i and c67j is the existing card 039 probe-table line. The only crash folder since 15:28 is c67a's at 23:21, from the first step-1 build, which the Report explains and the live-root rewrite fixed.
- `host-c67g-client-holds-rifle.png` (seen by the senior) shows the dropship over the camp with the Skip Flight prompt, not the brief.
- Gating: `c67_note_pause` runs from the pause detours only under `g_c63_on` (`UBWCOOP_CMD=1`). `auto_dropship_unpause` calls `c67_ui_close` first and falls back to `RequestGameUnpause` when the stack still holds the pause. With both env vars unset, c67k logs no card 067, autodropship or cmd lines.
- fnlen: 1241 functions, 0 over 60 lines.

Behaviour change to note: with `UBWCOOP_AUTODROPSHIP=1` (no `UBWCOOP_CMD`), the brief now closes through the game's own BP functions instead of staying drawn. This is what step 3 asked for.

Follow-ups:
- The c57 bug fight can kill the host before line 24 (c67h, 1 run in 4). If it recurs, the scenario should wait for the fight to end or give the host god mode; it is not a brief fault.
- Other `WB_Cutscenes_C` videos and mid-mission screens: add a `kC67UiSteps` row when one is seen.

Next: card 065.
