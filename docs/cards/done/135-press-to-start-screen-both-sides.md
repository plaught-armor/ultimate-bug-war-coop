# Card 135: Players control cinematics and the "press any key to start" screen; tests skip by the button path

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: queued, remainder only. Already landed in play build 7044ed8c10577a0c: nothing is auto-skipped in play, tests skip through UBWCOOP_TEST_SKIP=1 (movieskip.cpp). Open: the "Open" and "Do" items about the press-to-start screen on both sides.

Read: card 067 (`WB_Loading_Screen_Final`, `ui continue`, `ShowAsyncLoadingScreen` / `HideAsyncLoadingScreen`),
card 130 (`mod/src/ubw/cine_skip.cpp`), card 096 and card 120 (pause with a client connected). Memory rules
skip-cinematics and full-world-sync apply.

## Why

User (2026-11-08): the client should have the same splash screen as the host to enter the game when a map is loaded,
"just obviously after the host has seen theirs first". And: "right now on some of our tests the host sits on the
`press enter to start` screens and the host should have that."

User (2026-11-08), on cinematics: "when players are playing, nothing should be skipped automatically. the player
should be in full control and everyone should be experiencing the same thing as the other, and experiencing it how
they want. during testing, use 1." Option 1 is an automatic skip that runs exactly what each cinematic's own skip
prompt (the button) runs. This replaces card 130's always-on skip; the skip-cinematics memory rule is updated.

So this card covers two things:
- the level-intro screen ("OPERATION ... / BASIC TRAINING ... PRESS ANY KEY TO CONTINUE") that the player dismisses
  to enter the map;
- every cinematic: startup movies, the mission brief, `WB_Cutscenes_C` cutscenes and level sequences.

## Verified (senior, static)

- `cine_skip.cpp` `cine_skip_step`: after it skips a `WB_Cutscenes_C` on a `WB_Stack_C`, it also closes the next
  `WB_Loading_Screen_Final_C` top on that stack (`step == 1 && g_cine.skipped`, the card 067 close). So on maps with
  a brief movie, the mod closes the press-to-start screen by itself, on both sides. Where no cutscene was skipped
  first, the screen stays. That fits the user seeing the host sit on it in some tests.
- Test scenarios close it with `host ui continue` (for example `tools/scenarios/c100-measure.txt` line 5), and
  `UBWCOOP_AUTODROPSHIP=1` releases the pause (card 032).

## Open

- Whether the client gets a press-to-start screen at all today. The host's comes from the brief's `WB_Stack_C`
  after `TransitionToLevel`. `HideAsyncLoadingScreen` is called only from `ASTGameplayGameMode::BeginPlay`, and the
  client has no GameMode. Log which widgets the client's stack shows after its map load, and check
  `client-*.log` from a recent run for `card130: client cinematic loading screen ... closed`.
- What the host's screen pauses. The host requests a pause; card 096 skips `SetGamePaused` while a client is
  connected, and card 120 lifts the pause at a client's first Pong. State what the host's world does while its
  screen is up, before and after the client joins.

## Do

0. **Cinematics: the play build skips nothing, tests skip by the button path.**
   - Make card 130's skip (`cine_skip.cpp`: the `WB_Cutscenes_C` skip, the level-sequence `GoToEndAndStop`, and the
     loading-screen close) and the startup movie skip (`movieskip.cpp`; `UBWCOOP_SKIP_MOVIES` defaults to on even
     when unset, `flags.cpp:91`) test-only. Put them behind one flag that only `run_pair.sh` sets. Unset means off,
     and the play package never sets it.
   - Per cinematic type, find what the game's own skip prompt does when the player presses it (the key handler and
     the function it calls), and show that the test skip makes the same call with the same arguments. Today:
     `WB_Cutscenes_C` gets `On Video Ended` with Skipped=true (card 067 says this is what `OnKeyDown`'s Enter path
     calls). Level sequences get `GoToEndAndStop`, and that is not yet checked against the game's skip. If the game
     skips them another way (a skip widget, `SetPlaybackPosition`, a Blueprint skip event), use that. Record it as
     a table: type, the game's skip handler address, the call the test skip makes.
   - In play, both players get every cinematic the host gets, at the same point in the map. Each player can skip
     their own with the game's button, and that skip does not skip the other player's.
     - Open: whether the client plays the host's level sequences and brief at all today (replicated playback, or its
       own level script), and what the world does when one player skipped and the other is still watching. The
       host's level script goes on at the host's own `OnFinished`; state what the client sees then. Find this
       statically and in one test, then report it to the senior before building any hold or catch-up.
1. **Keep the screen.** In the play build, `cine_skip.cpp` must not close the `WB_Loading_Screen_Final_C` that
   follows a skipped brief. Only the cinematic itself is skipped. Test runs keep closing it with `ui continue` in the
   scenario, or through a test-only env flag, never by default.
2. **The host sees theirs first.** The host's press-to-start screen behaves as in vanilla: it shows after the map
   loads, and the host dismisses it with a key.
3. **Then the client.** The client gets the same screen, with the same level title and text, on its own machine.
   - It shows once the host has dismissed theirs (the host sends one small event), or on joining if the host already
     has. A late joiner gets it on its join too.
   - The client dismisses its own with a key. That only closes the client's screen and gives it input. It must not
     pause or hold the host's world, and the host must not wait for it.
   - If the client's game does not build the screen itself (see Open), build it the way the game does: the same
     widget class on the client's stack, through the game's own push, not a copy of its art.
4. **One run.** FAST=1, FED_C1M1 and one map without a brief movie. Test runs use the test skip from step 0. Also
   take one play-mode check (the flag unset): the brief movie plays on both sides, and each side skips it with one
   real key press. Take host and client screenshots: the host on
   its screen while the client has none yet, then the client on its screen after the host continued, then both in
   game. Use real key presses at human rates (human-test-driver).

## Done when

- The screenshots show the order above on both maps.
- The skip table from step 0 is filled in, and the play-mode check shows no automatic skip on either side.
- Logs show the host's continue, the event to the client, and the client's continue, with times.
- The Report has the WARN diff against the previous accepted run, the run length, rc, ERROR/FATAL, CRASH lines,
  dumps and the DLL hash (`sha256sum | cut -c1-16`).
- It ends with "Report complete."

## Senior prep (2026-11-12, static, tree at card 196)

- Step 0's flag and step 1 are in the tree: `config::test_skip()` (`UBWCOOP_TEST_SKIP`, set only by
  `tools/run_pair.sh` line 131 through `SKIP_MOVIES`, default 1) gates `cine_skip_step` (`cine_skip.cpp:168`
  returns at once when off) and the level-sequence queue (`cine_skip.cpp:141`). So the play build closes no
  loading screen and skips no cinematic. Not done from step 0: the skip table (the level-sequence
  `GoToEndAndStop` checked against the game's own skip handler) and the open item on what the client plays.
- Left for this card: both Open items (static first), the skip table, steps 2 to 4.
- Evidence so far on the first Open item: in c196-r2 the client log has only `card067: class ...
  WB_Loading_Screen_Final_C at ...` lines (the class is loaded) and no `Widget Stack[0] = WB_Loading_Screen_Final_C`
  line; the host log has that stack line. That run joined after the host continued, so it does not settle whether
  the client's game builds the screen on its own map load. The trace must name the function that pushes the
  screen on the host (caller of the `WB_Stack_C` push after `TransitionToLevel`) and whether that caller runs on
  a client.
- Order of work: trace and the answer to both Open items go to the senior before any code. The run budget is
  three, and the play-mode check (flag unset, `SKIP_MOVIES=0`) is one of them.

## Trace (junior, 2026-11-12, static: PDB, disassembly, package name maps, source, logs c171-r1 and c147-r3)

### 1. Who pushes the press-to-start screen, and does it run on a client

- Verified: `ASTGameplayGameMode::BeginPlay` (0x05117A30) calls `USTGameInstance::HideAsyncLoadingScreen` (+0x5E),
  then `USTCommonUtilityLibrary::GetSTHUD` (the first local player's HUD, +0x6E), checks it is an `ASTHUDGameplay`
  (+0x7B to +0x99), reads `FURL::HasOption(L"FromSave")` on the world context URL (+0xC0), and calls
  `ASTHUDGameplay::GameStarted(bool bFromSave)` (0x0506D9E0, call at 0x05117AFB). `GameStarted` is a Blueprint event
  thunk (`FindFunctionChecked` + `ProcessEvent`). It has exactly one native caller (xref scan of `.text`).
- Verified: the event lands in `BP_HUD_Manager_C` (parent of `BP_HUD_Manager_MI_C`). Its name map holds
  `GameStarted`, `K2Node_Event_bFromSave`, `CallFunc_ShouldShowLevelLoading_ReturnValue`,
  `K2Node_DynamicCast_AsWB_Loading_Screen_Final`, `CallFunc_LoadCutsceneForCurrentLevel_ReturnValue`,
  `CallFunc_Push_Widget_Added_widget`, `Add to pause stack`, `RequestGamePause`, `IntroCutsceneEnded`,
  `OnFinishedLoadingLevel`, `Set Input Mode UI`. In c171-r1 the host's `Set Input Mode UI` is called "in
  BP_HUD_Manager_C on BP_HUD_Manager_MI_C" at 130795627, the same ms as the map's `WB_Stack_C RequestGamePause` with
  `Widget Stack[0] = WB_Loading_Screen_Final_C`.
- Verified: `ASTHUDGameplay::ShouldShowLevelLoading` always returns true (its exec thunk calls a shared
  `mov al, 1; ret` at 0x00C7BC80).
- Open (no Kismet bytecode decoder in the tree): the exact order of nodes inside the `GameStarted` graph, and what
  else it does once (intro text, `Set Input Mode UI`, the pause stack). The host log shows the outcome: the
  loading screen is pushed first, the brief `WB_Cutscenes_C` on top of it (card 067).
- Verified: the caller does not run on a client. `ASTGameplayGameMode` exists only on the server, so the client's
  `GameStarted` never fires. The client does spawn its own `BP_HUD_Manager_MI_C` (c171-r1 client line 1598,
  130914381, "client spawn BP_HUD_Manager_MI_C ... replicates 0"), but after that the client log has no
  `WB_Stack_C RequestGamePause`, no `Widget Stack[0] = WB_Loading_Screen_Final_C` line and no `card130: client
  cinematic` line. The only client stack dumps are the frontend ones (`WB_FTUE_PhotoSensitivity_C`,
  `WB_FTUE_LegalText_C`, `WB_Single_Confirm_Popup_Taller_C`).
- Verified: after its own map load the client's stack shows no mission widget. The client gets no press-to-start
  screen and no brief today, whatever the join time.

### 2. What the host's world does while its screen is up

- Verified, before a client joins: the push requests a pause through `WB_Stack_C`, `USTGameInstance::UpdatePause`
  calls `SetGamePaused(1)` (caller exe+0x510CE24), and with 0 clients card 096 passes it through: "SetGamePaused(1)
  = 1 (world paused), 0 client(s) connected" (c171-r1 host 130795627). The host's world is frozen until the screen
  closes (130798929, "SetGamePaused(0) = 1 (unpaused)").
- Verified, a client joins while it is up: card 120 lifts the pause at the client's first Pong ("pause lifted:
  client connected while paused (first Pong)"). After that every `SetGamePaused(1)` that `UpdatePause` sends is
  skipped ("the world keeps running, 1 client(s) connected"). The requester arrays are untouched, so the host's
  screen stays up and its close runs the normal unpause. c171-r1 shows this with the pause menu (130916002 and
  130916003). The press-to-start screen holds the same requester path, so it acts the same; no run has shown it with
  this screen yet.
- So the host's world runs (bugs, dropship, timers) while the host still sits on its screen, as soon as one client
  is connected. With the client gone the next `UpdatePause` frame pauses it again (c171-r1 131129782).

### 3. Skip table

| Type | The game's own skip | The test skip (`UBWCOOP_TEST_SKIP=1`) | Same call? |
|---|---|---|---|
| Startup movie | `FDefaultGameMoviePlayer::OnAnyDown` (exe+0x036DB620), only when the skippable byte (+0xDD) is set; reached from a real key or click | `movieskip.cpp` posts `WM_KEYDOWN`/`WM_KEYUP` Enter and a left click to the game window every 500 ms while `IsStartupMoviePlaying` | Verified: yes, it is real window input into the same handler |
| Mission brief / in-mission movie (`WB_Cutscenes_C`) | `WB_Cutscenes_C.OnKeyDown` (Slate focus), which calls `On Video Ended(Skipped=true)` and then Destruct (card 067 c67e trace) | `cine_skip.cpp` `cine_call` step 0: `ProcessEvent(top, "On Video Ended", Skipped=true)` | Verified for the function and argument. Open: anything `OnKeyDown` does before that call (bytecode not decoded) |
| Press-to-start screen (`WB_Loading_Screen_Final_C`) after a skipped brief | `OnKeyUp` (Slate focus), then Destruct; `WB_Stack_C` then calls `RequestGameUnpause` (card 067) | step 1: `ProcessEvent(top, "Remove Current Menu")` (the `WB_StackElement` function) | Open: card 067 found no `Continue` function; whether `OnKeyUp` reaches `On Back` or `Remove Current Menu` is not decoded. The outcome (stack empty, unpause) matches in c171-r1 |
| Level sequence | Verified: none. No `Skip` symbol on any sequence or cutscene class in the PDB (only `ASTProceduralDropship::SkipFlight`), and no skip name in the 2733 package name maps under Blueprints, Maps, Data, Cinematics and Sequences. `GoToEndAndStop` appears only in `BP_CamFadeController`, `BP_FallingRock`, `BP_PlayerTransportDropship` and `BP_SolarPanel` (gameplay) | `cine_skip.cpp` `seq_skip`: `GoToEndAndStop` the tick after `Play`, for sequences under `/Sequences/`, `/Movies/`, `/Cinematics/` | No. The player cannot skip a level sequence in vanilla; the test skip has no game equivalent. In play nothing is skipped (gated by `test_skip()`), which matches vanilla |

### 4. Does the client play the host's level sequences and brief today

- Brief: verified no (item 1). The client gets neither brief nor screen.
- Level sequences: open. No pair-run client log has a `card130: client level sequence Play` line; FED_C1M1 with
  FAST=1 plays none on either side (c171-r1), and the logs that have Play lines are frontend-only playtest logs.
  `LevelSequenceActor` placed actors exist on both sides on FED_C2M1 (c147-r3 digest round 12, "replicates here but
  not on the host"), so the client has its own copy of the level's sequence actors. Whether the client's level script
  plays them (it has no GameMode and no server-side triggers) or the host's playback replicates
  (`bReplicatePlayback`) is not settled statically.
- What the client sees when the host's level script goes on at the host's own `OnFinished`: the objectives, doors
  and spawns that follow reach the client through the existing sync (card 098/132 journals) while the client is in
  game. Open until the sequence question above is answered by a log line on FED_C2M1 (`FED_C2M1_Intro`).

### 5. Step plan for card steps 2 to 4

- Step 2, host: nothing to build. With `test_skip()` off the host's screen is vanilla (item 1 and step 1). Tests
  close it with `host ui continue`. Note: `UBWCOOP_AUTODROPSHIP=1` (card 015/032/067 `c67_ui_close`) also closes
  every `WB_Stack_C` pause 3 s after it starts, and `cine_skip_step` closes the screen after a skipped brief (step 1
  in the table). Both must be off for the c135 runs, or the host's screen is never seen.
- Step 3, the client, through the game's own push: the client calls the same event the host's GameMode calls,
  `ASTHUDGameplay::GameStarted(bFromSave)` on its own local `BP_HUD_Manager` (via `ProcessEvent`, the host's
  `bFromSave` value carried in the event). That runs the same `BP_HUD_Manager` graph: the same
  `WB_Loading_Screen_Final_C` with the same level data, and the client's own brief on maps that have one.
  - Host side: when the host's map stack held a `WB_Loading_Screen_Final_C` and `WB_Stack_C` calls
    `RequestGameUnpause` with the stack empty (the hook card 067 already has), log "card135: host continued" and
    send one small op to every connected client (map name, bFromSave). Remember "continued on this map".
  - Late joiner: at `c50_join_replay` (`weapons.cpp:649`, next to `c96_lift_on_join`), if the host already continued
    on this map, send the op to the joining client.
  - Client side: on the op (or on its map load, when the op arrived first), wait until its local HUD is a live
    `BP_HUD_Manager_C` with a possessed pawn, then call `GameStarted` once per map, with a log line. The client's
    `RequestGamePause` reaches only its own game instance; card 096 shows a client's `SetGamePaused` cannot pause the
    server ("unpause refused" at c171-r1 client 130912138), so the host does not wait.
  - The client dismisses its own screen with a key (or `client ui continue` in tests); `card067 ui continue` must run
    on the client too (check `testrun.cpp:1323` has no host-only guard).
- Files: `mod/src/ubw/cine_skip.cpp` (or a new `intro_c135.cpp`), `mod/src/ubw/chan_ops.h`/`chan_ops.cpp` (one new
  op), `mod/src/ubw/hooks_internal.h` (declarations), `mod/src/ubw/weapons.cpp` (`c50_join_replay`: one call),
  `mod/src/ubw/testrun.cpp` (the step-1 close kept off for c135 runs, if a flag is chosen),
  `tools/scenarios/c135-r1.txt` to `c135-r3.txt`.
- Step 4, three runs:
  - r1, FED_C1M1, FAST=1, test skip on, AUTODROPSHIP off, client joins while the host's screen is up.
  - r2, a map without a brief movie (open: which map; the candidate must have `LoadCutsceneForCurrentLevel`
    return none), client joins late, after the host continued (the late-joiner path).
  - r3, play check, `SKIP_MOVIES=0`, FED_C1M1: briefs play on both sides and each side skips by one real key.

### 6. Written predictions

- r1: host log "SetGamePaused(1) = 1 (world paused), 0 client(s) connected" with `Widget Stack[0] =
  WB_Loading_Screen_Final_C`, then "card130: host cinematic skipped WB_Cutscenes_C" and no "cinematic loading screen
  ... closed". After the client's first Pong: "pause lifted ... (first Pong)", then "skipped: the world keeps
  running". Host shot: host on its screen, client shot: client in game, no screen. `host ui continue`: "card135: host
  continued" and the op sent to 1 of 1. Client: "card135: client GameStarted" then its own `WB_Stack_C
  RequestGamePause` with `Widget Stack[0] = WB_Loading_Screen_Final_C`, its own "card130: client cinematic skipped
  WB_Cutscenes_C", and no host `SetGamePaused` line at that time. Client shot: the same level title and text as the
  host's. `client ui continue`: client stack empty, `RequestGameUnpause`; both shots in game.
- r2: no `WB_Cutscenes_C` on either side; the host screen comes up and stays; the client joins after "card135: host
  continued"; the op is sent at the join replay; the client gets the screen on join.
- r3: no `card130` skip line on either side; the brief plays on both; one key on each side skips its own copy only
  (the other side's `WB_Cutscenes_C` stays the top widget until its own key).

### 7. Card 222 and the play check

- The r1 and r2 screenshots need no menu close: the screen closes through `ui continue` (`ProcessEvent`), which
  works today. Card 222 does not block them.
- The r3 play check does depend on a key reaching Slate focus. The driver's `key` goes through `InputKey`, which
  never reaches `WB_Cutscenes_C.OnKeyDown` or `WB_Loading_Screen_Final_C.OnKeyUp` (card 067; `hooks_internal.h`
  card 015 note: `FSlateApplication::OnKeyDown`/`OnKeyUp` injection did not release the pause). This is the same gap
  as card 222's Escape. So card 222's key path must land before r3, or r3 uses posted window messages the way
  `movieskip.cpp` does (unproven for widgets: card 067 noted the window was likely not focused).

## Senior decisions on the trace (2026-11-12)

Trace accepted. Card 222 (driver keys reach widgets) runs first; this card's build follows it.

- (a) The client gets the brief and the screen, through `GameStarted` on its own HUD (the user's words: both
  players get every cinematic, each skips their own). The client enters later than the host; the host never waits.
- The client's world must keep running under its brief and screen: both machines run the full simulation, so a
  paused client world falls out of step. Before code, verify what the client's `SetGamePaused` from this graph
  does and skip it the way card 096 does on the host; input mode and the widget stay.
- (b) With test skip on, both sides close the brief and the screen as today (existing scenarios keep working, the
  client included). One test-only flag keeps the screen up for the c135 runs. Unset in play: nothing closes.
- (c) Run 3 (play check) uses real keys through card 222's path.
- (d) The map without a brief: one header check, written in the card before run 2.
- (e) No Kismet decoder now; the open Blueprint points are checked by the runs' log lines.
- (f) From card 222 run c222-r2: run 3's brief key waits for the brief's ready line (`card067: ProcessEvent WB_Cutscenes_C.Finished_\S+`, or the `WB_Loading_Screen_Final_C.ExecuteUbergraph_WB_Loading_Screen_Final` line, after the brief dump) before it presses, since a press 0.5 s after the brief came up reached WB_Cutscenes_C.OnKeyDown and did not skip it.

## Build (junior, 2026-11-12)

### Static answers (before any code)

- The client world keeps running under the client's GameStarted graph. A client's `SetGamePaused(1)` is refused
  today: "card096: client ... SetGamePaused(1) = 0 (pause refused), 0 client(s) connected; context
  BP_GameInstance_C, caller exe+0x510CE24" (c222-r3-client.log:2752). `pause_c96.cpp` needs no change. Proof line in
  r1: the client's "card135: client GameStarted ..." line logs `world paused 0` (`UWorld::IsPaused` on the client's
  world right after the call), and a later client card096 line reads "(pause refused)".
- Decision (d), the run-2 map: `FED_C2M1`. Header evidence: the `BP_CutsceneManager` package
  (/Game/Movies/BP_CutsceneManager) name map lists intro movies only for `FED_C1M1` and `BUG_C1M1`; outros for
  FED_C1M1, C2M1, C3M2, C4M2, C5M2 and BUG_C5M1. `USTCutsceneManager::LoadCutsceneForCurrentLevel(WorldContext,
  bIntro)` therefore has no intro for FED_C2M1: no `WB_Cutscenes_C` brief. Route: `UBWCOOP_TRAVEL=/Game/Maps/FED/FED_C2M1?listen`
  (the c147 route). Its level sequence `FED_C2M1_Intro` is test-skipped by `seq_skip` (card 130), not by this card.
- `ui continue` has no host-only guard: `testrun.cpp` `Op::Ui` calls `c67_ui_start`, then `c67_ui_tick`, on either
  side. The stack it closes is the one `c67_ui_remember` took at `c67_note_pause`, which runs whenever `g_c63_on`
  (the test driver, on both sides).
- A client's pawn while its screen is up (report only, nothing built): the HUD graph's `Set Input Mode UI` puts the
  client's own PC in UIOnly, so the client's pawn takes no gameplay input. Its server copy stands in the host's
  world, which runs (card 120 lift, card 096 skip) as soon as one client is connected, so bugs can reach and damage
  that standing copy. Tests run GOD. Nothing protects it in play.

### What was built

- `mod/src/ubw/intro_c135.cpp` (new, in `mod/CMakeLists.txt`): one state struct `g_c135`, one reset `c135_map_end`
  called from `ubw_load_map_enter` (hooks_ubw.cpp).
  - Host: `c135_note_pause` (from `cine_note_pause`) remembers the `WB_Stack_C` whose element 0 at its pause is
    `WB_Loading_Screen_Final_C`. `c135_note_unpause` (from `detour_request_unpause`, dropship.cpp, one call) fires
    when that remembered stack unpauses with its stack empty: logs "card135: host continued ... IntroStart sent to N
    of M client(s)", remembers it for this map, sends `ChanOp::IntroStart` text "<map> <bFromSave>" to every remote
    PC. `bFromSave` is read from the GameMode's `OptionsString` ("FromSave", case-insensitive: the option
    `ASTGameplayGameMode::BeginPlay` reads). `c135_join_replay` (from `c50_join_replay`, weapons.cpp, one call)
    sends it to a joiner once the host has continued.
  - Client: `c135_receive` (chan_ops.cpp client table) stores the PC as an FWeakObjectPtr and the op; `c135_step`
    (from `cine_skip_step`, before its test-skip return, so the play build runs it) waits per tick, at most 60000 ms,
    until the PC's HUD is a `BP_HUD_Manager*`, the PC has a pawn and the PC's world name equals the op's map, then
    calls `GameStarted(bFromSave)` on that HUD through ProcessEvent once: "card135: client GameStarted ...". A
    second op on the same map: "card135: client ... IntroStart (...) refused: GameStarted already called/pending".
- `UBWCOOP_C135_KEEP_SCREEN=1` (flags.cpp `read_c135_keep_screen`, read in `read_card_flags`): `cine_skip_step` does
  not close the press-to-start screen after a skipped brief. **The flag has no effect with UBWCOOP_AUTODROPSHIP=1**:
  the autodropship close in dropship.cpp stays; the read logs one WARN when both are set.
- `ChanOp::IntroStart` (chan_ops.h, `kChanArgsText`). The build's static assert also needs one row in
  `ledger_c132.cpp` `kC132Ops` (`{"IntroStart", Text, false}`): a file outside the card's list, forced by the new op.

### Teardown writer

- The press-to-start screen's `WB_Stack_C` is outered to `BP_GameInstance_C` (c222-r3-host.log:1774): it outlives
  every map, so an unpause of that stack can come after `ubw_load_map_enter` on the old world's teardown. Guard:
  `c135_note_unpause` acts only for the stack remembered by `c135_note_pause` on this map with `screen_seen`, and the
  reset at LoadMap enter clears both, so such an unpause is ignored. A pause of the same stack on the new map is
  remembered again only when its element 0 is the loading screen.
- The stack is empty when its `RequestGameUnpause` comes: c222-r3-host.log dump 3 (133594007) lists no `Widget
  Stack[...]` row, and the card130 close line follows the unpause in the same ms. The frontend stack unpauses with
  3 elements (dump 1, 133588687), so the empty check is needed.

### Callers of the new send and apply (checked before run 1)

- `c135_send`: `c135_note_unpause` (all remote PCs) and `c135_join_replay` only.
- `c135_note_unpause`: `detour_request_unpause` only. `c135_note_pause`: `cine_note_pause` only (WB_Stack_C
  requesters, after the game's pause call).
- `c135_join_replay`: `c50_join_replay` only (host, a server copy's first Pong).
- `c135_receive`: the client table only (`chan_pc_s<c135_receive>`); the host table has no IntroStart row.
- `c135_call` (the GameStarted apply): `c135_step` only; `c135_step`: `cine_skip_step` only.
- A frame sent to a client before its first Pong is held (card 139) and not counted as sent; the join replay at
  that client's first Pong sends it then.

Build: clean (/W4 /WX), DLL hash b947a2ba5e8df7de. Longest new functions: `c135_step` 30 lines, `c135_receive` 29.

### Run c135-r1: scenario audit (before the run)

`tools/scenarios/c135-r1.txt`. FED_C1M1 (the run_pair default route), FAST=1, test skip on (run_pair default
SKIP_MOVIES=1), `UBWCOOP_C135_KEEP_SCREEN=1` on both sides, no AUTODROPSHIP, GOD default on, CLIENT_DELAY_MS default.
- Step 1 waits for the host's in-map stack pause dump (`in FED_C1M1`: the frontend dumps say `in ?`), then marks, so
  the brief skip and the 5 s dump are this pause's, not the frontend's. The brief skip is a `try`: it is a
  prediction, read from the log; the 5 s dump (screen still held) is required.
- Step 2 waits for the card 096 first-Pong lift (the client is in while the host's screen is up; the screen stays
  until step 3, so any join time is inside it) and the client's first clock line, then a shot per side.
- Step 3: marks both sides before `host ui continue`; every later wait is since-mark. Required: the host's
  "card135: host continued ... IntroStart sent to 1 of 1", the client's IntroStart line, its GameStarted line (60 s,
  the c135_step cap), its own in-map stack pause dump and its 5 s dump. The client brief skip is a `try`
  (prediction). Shots: host in game, client on its screen.
- Step 4: `client ui continue`, its done line and its stack's unpause dump; end shots; quit.
- Expected WARN: "UBWCOOP_SKIP_FLIGHT=1 needs UBWCOOP_AUTODROPSHIP=1 -- off" (host). No sleeps except the 2 s
  between quits (as every pair scenario).

### Run c135-r1: result

Ended by the scenario at line 13 after 334 s (drive rc 1; host rc 0, client rc 0). Scenario defect, not a code
result: the in-map stack pause dump logs `in ?` (host-log:1377, "card067: dump 2 in ? (WB_Stack_C RequestGamePause)"
at 134892047 ms); only the 5 s dump names the map (host-log:1789, "dump 3 in FED_C1M1"). The audit assumed the map
name on the pause dump; c222-r1 uses `in \S+`. The card 135 code paths after step 1 were never reached: no `ui
continue` was sent, so no "card135:" line exists on either side (correct for the code: the frontend stack unpause,
host-log:1086, requesters 0, logged none).
Seen on the way: the host's brief skipped at +3405 ms (host-log:1646), the screen still held 5 s after the pause
(host-log:1787, world paused 1) and kept to the quit (no WB_Stack_C unpause after host-log:1377), the first-Pong lift
at 134901720 (host-log:2014). The client has no in-map stack pause at all (only its frontend's), the card's bug.

### Run c135-r1b: scenario audit (before the run)

`tools/scenarios/c135-r1b.txt` is c135-r1.txt with the three stack dump waits changed to `in \S+` (lines 14, 31,
40) and the shot names. Run c135-r1 is void (a scenario fault); a second void run counts against the budget.
Every `wait log` line and the log line it matches (`L` = logs/; source lines for the card135 lines, which no run
has produced yet):

| Scenario line | Matches |
|---|---|
| 14 host `card067: dump \d+ in \S+ \(WB_Stack_C RequestGamePause\)` (300 s: frontend and map load) | L/c135-r1-host.log:650 "card067: dump 1 in ? (WB_Stack_C RequestGamePause)" (the frontend's; see the note) and :1377 "dump 2 in ? (...)" (the map's) |
| 16 try host `card130: host cinematic skipped WB_Cutscenes_C` since-mark | L/c135-r1-host.log:1646 |
| 17 host `card067: 5 s after the WB_Stack_C pause: \d+ pause requester` since-mark | L/c135-r1-host.log:1787 "... 1 pause requester(s) held, world paused 1" |
| 19 host `card096: host t=\d+ pause lifted: client connected while paused \(first Pong\)` (300 s: the join) | L/c135-r1-host.log:2014 |
| 20 client `playlog: client t=\d+ clock 1:` | L/c135-r1-client.log:2022 |
| 27 host `cmd: \d+ ui continue -> done` since-mark | L/c222-r3-host.log:2020 "cmd: 1 ui continue -> done"; format mod/src/ubw/testrun.cpp:1432 |
| 28 host `card135: host continued t=\d+ on FED_C1M1 \(bFromSave 0\): IntroStart sent to 1 of 1 client` | mod/src/ubw/intro_c135.cpp:158 "card135: host continued t=%llu on %s (bFromSave %d): IntroStart sent to %u of %u client(s)" |
| 29 client `card135: client t=\d+ IntroStart \(FED_C1M1, bFromSave 0\) in` | mod/src/ubw/intro_c135.cpp:200 "card135: client t=%llu IntroStart (%s, bFromSave %d) in: waiting for the HUD and pawn" |
| 30 client `card135: client GameStarted t=\d+ on FED_C1M1 ` (60 s: the c135_step cap) | mod/src/ubw/intro_c135.cpp:229 "%scard135: client GameStarted t=%llu on %s (bFromSave %d, ..." (the `%s` prefix is empty or "WARN "; the regex is unanchored) |
| 31 client `card067: dump \d+ in \S+ \(WB_Stack_C RequestGamePause\)` since-mark | L/c135-r1-client.log:650 "card067: dump 1 in ? (WB_Stack_C RequestGamePause)" (the client's frontend one: same format) |
| 32 try client `card130: client cinematic skipped WB_Cutscenes_C` since-mark | no run has a client one; mod/src/ubw/cine_skip.cpp:120 "card130: %s cinematic skipped %s %p ..." with side() = "client" on a client session (mod/src/ubw/hooks_ubw.cpp:323) |
| 33 client `card067: 5 s after the WB_Stack_C pause: \d+ pause requester` since-mark | L/c135-r1-client.log:980 |
| 39 client `cmd: \d+ ui continue -> done` since-mark | as line 27 (the same driver code on both sides, testrun.cpp:1432) |
| 40 client `card067: dump \d+ in \S+ \(WB_Stack_C RequestGameUnpause\)` since-mark | L/c135-r1-client.log:1196 "card067: dump 1 in ? (WB_Stack_C RequestGameUnpause)" |

Timeouts: 300 s only on line 14 (the frontend and map load) and line 19 (the join); every other wait is 5-60 s.
Note on line 14: `in \S+` also matches the host's frontend stack pause (c135-r1 host-log:650), which comes first.
Step 1 then marks at the frontend pause. Lines 16 and 17 still find the map's lines in c135-r1's order (the frontend
unpaused after 4.5 s, host-log:1086, before its 5 s dump; the map's skip and 5 s dump come 7-12 s after the mark,
inside their 30 s). If the frontend pause ever lasts 5 s, line 17 matches the frontend's 5 s dump, and step 1 proves
less, but the run goes on: step 2 waits for the join either way. Steps 3 and 4 are unaffected (their marks come
after the join).

### Run c135-r1b: result

Scenario passed, 49 s (host rc 0, client rc 0, drive rc 0), FATAL/ERROR 0 both sides, no new crash dumps, DLL
b947a2ba5e8df7de. Every card 135 prediction held:
- host-log:2310 135362896 "card135: host continued t=135362896 on FED_C1M1 (bFromSave 0): IntroStart sent to 1 of 1
  client(s)", the same ms as the host's `ui continue` Remove Current Menu (host-log:2307) and its stack unpause dump
  (host-log:2311); the host stack empty, world paused 0 (host-log:2327).
- client-log:2039 135362913 "IntroStart (FED_C1M1, bFromSave 0) in" (+17 ms after the host's send).
- client-log:2143 "card135: client GameStarted t=135362919 on FED_C1M1 (bFromSave 0, HUD BP_HUD_Manager_MI_C, +6 ms
  after the op); world paused 0". The call is at t=135362919; the line is stamped 135364466, after ProcessEvent
  returned (the call itself ran about 1.5 s; inside it the client's stack pushed and paused: client-log:2084 "dump 2
  in FED_C1M1 (WB_Stack_C RequestGamePause)" at 135362920).
- The client brief skipped (client-log:2146, +1546 ms after its pause request), its screen held 5 s after (client-log:
  2261, 1 requester, world paused 0: the client world keeps running, the card's prediction), `client ui continue`
  emptied its stack (client-log:2320 unpause dump, 2336 "empty and holds no pause").
- No refusal line (one op, one call). Digests: card132 rounds mismatches 0, playlog mismatches 0.
Screenshots (Saved/Screenshots/Windows of prefix/, `<role>-c135-r1b-c135-r1b-<name>.png`): host-screen shows the
press-to-start screen ("BASIC TRAINING", the map's image, a tip, "PRESS ANY KEY TO CONTINUE"); client-before shows
the client in the map with the HUD and no screen; client-screen shows the client's own press-to-start screen; host-
in-game shows the host in the dropship flight; client-end shows the client in the map with the HUD.
Differences seen on the client's screen (not fixed; for the senior): the title reads "Level Name" (the widget's
placeholder) where the host's reads "BASIC TRAINING"; the background image is a different (night, rocky) one; the tip
differs (may be random per screen); the mouse cursor shows. Client WARN new against c222-r3: "WARN card156: client
... back to Up (map load) +5 s: ... bShowMouseCursor 1, viewport capture 0, HUD UIMode 1 ..." at 135365876, while
the client's screen was up (it went up at 135362920). Step 1's line 17 matched the frontend's 5 s dump (host-log:956,
"dump 2 in 3D_Frontend"), the case the audit names; the map's 5 s dump is host-log:1862.

## Trace before r2 (static, no run)

1. **Level name and image (verified).** WB_Loading_Screen_Final_C's "Apply Data" graph (name map of
   WB_Loading_Screen_Final.uasset: CallFunc_GetSTGameInstance, CallFunc_GetChapterID_ReturnValue,
   CallFunc_GetLevelID_ReturnValue, CallFunc_GetUIDataForLevel_ReturnValue, K2Node_DynamicCast_AsDA_Level_Data_Master,
   TextBlock_LevelName, Level Image) reads the game instance's chapter and level IDs and passes them to
   USTMetaGame::GetUIDataForLevel(int, int) (PDB publics). Its defaults are the text "Level Name" and the image
   T_Loading_Screen_Klendathu, which is what the client showed in r1b. The host's IDs come from its own game
   instance; the client never sets its own (it joins without choosing a level), and nothing in mod/src touches
   ChapterID or LevelID. Open: the IDs' value on the host under the test route (UBWCOOP_TRAVEL skips the frontend's
   level select); r2 logs it. The tip comes from ShowRandomTooltip / CallFunc_Array_Random_OutItem over LoadingToolTips,
   so it is random per screen and is allowed to differ.
2. **Cursor (verified as a capture difference, not a state difference).** No host screenshot shows a cursor, even with
   the pause menu up (host-c222-r3-c222-host-pause.png), while client shots show one at the top left
   (client-c222-r3-c222-client-options.png, client-c135-r1b-c135-r1b-client-screen.png). The shots are taken
   in-game through FScreenshotRequest with the UI (testrun.cpp:1455). No code change. r2 shoots both sides with the
   screen up and after it closes, and the card 156 line logs bShowMouseCursor after the close.
3. **The client WARN "card156 ... back to Up (map load) +5 s ... bShowMouseCursor 1 ... HUD UIMode 1" (verified).**
   c156_client_up(pc, "map load", true, now) runs from detour_acknowledge_possession (input_diag.cpp:130) when
   g_game_only_pending is set (sprint.cpp:211). It restores game input and arms check_ms at +5 s. c156_client_check
   (death.cpp:2430) then reads the cursor and UI mode. In r1b the restore came at 135360517, GameStarted pushed the
   screen at 135362920, and the check at 135365876 read the screen's own UI mode 1 and cursor 1. The card 135 screen
   sets both on purpose, so it is the cause. A second hazard for the late join: if AcknowledgePossession's restore came
   after the client's GameStarted, Set Input Mode Game would land on the screen.

## Change for r2

- intro_c135.cpp:
  - Host, c135_note_pause: when the screen is seen, read GetChapterID and GetLevelID on the game instance of the
    stack's world (ProcessEvent, c135_gi_int) and log "card135: host t=... screen up: <GI> chapter C level L".
  - The IntroStart text becomes "<map> <bFromSave> <chapter> <level>"; c135_receive parses the four fields with
    sscanf_s and logs the IDs.
  - Client, c135_step: waits also for !g_game_only_pending (the card 156 restore first), then c135_set_ids calls
    SetChapterID and SetLevelID on its own game instance right before GameStarted, so the screen's Apply Data reads the
    host's IDs. The GameStarted line logs "chapter A->B level C->D", with a WARN suffix if a function is missing or the
    values read back differ.
  - Client, c135_note_pause also remembers its own screen's stack (after its GameStarted). The new c135_screen_up()
    reports whether that stack still holds a widget.
  - The FName caches move to g_c135_names, which c135_map_end does not reset.
- death.cpp (outside the card's list; it owns the card 156 check): c156_client_check returns while c135_screen_up(),
  keeping check_ms armed and logging one "check deferred" line, so the check runs once the screen closes.
- hooks_internal.h: the c135_screen_up declaration; the c135_note_pause comment now says both sides.
- cine_skip.cpp: the call-site comment only ("both sides").

DLL cd442736a94c40f0. Built clean under /W4 /WX. New and changed functions: c135_gi_int 19 lines, c135_gi 4,
c135_note_pause 22, c135_screen_up 8, c135_receive 37, c135_set_ids 20, c135_step 33, c156_client_check under 60.

## Run c135-r2: scenario audit

| Line | Wait | Source |
|---|---|---|
| 14 | host "screen up: ... (GetChapterID, GetLevelID)" | intro_c135.cpp:187 (new) |
| 16 | host "card067: 5 s after the WB_Stack_C pause: \d+ pause requester" since the screen-up mark (the frontend's own 5 s line comes earlier) | c135-r1b-host.log:956 |
| 21 | host "cmd: \d+ ui continue -> done" | c135-r1b-host.log:2328 |
| 22 | host "host continued ... on FED_C2M1 (bFromSave 0): IntroStart sent to 0 of 0 client" | intro_c135.cpp:233 |
| 25 | host "join replay: IntroStart (FED_C2M1 0) sent to" | intro_c135.cpp:239 |
| 26 | client "IntroStart (FED_C2M1, bFromSave 0, chapter C level L) in" | intro_c135.cpp:279 (new) |
| 28 | client "] card135: client GameStarted ... chapter A->B level C->D); world paused" (no WARN prefix, no WARN suffix) | intro_c135.cpp:342, 302 |
| 29 | client "card067: dump \d+ in \S+ (WB_Stack_C RequestGamePause)" since the mark | testrun.cpp:1198; c135-r1b-client.log:2084 |
| 30 | client "card067: 5 s after the WB_Stack_C pause" since the mark | c135-r1b-host.log:977 (same format) |
| 31 | try client "card156 ... +5 s check deferred: the card 135 screen is up" | death.cpp:2440 (new) |
| 38 | client "cmd: \d+ ui continue -> done" | c135-r1b-host.log:2328 (same format) |
| 39 | client "card067: dump \d+ in \S+ (WB_Stack_C RequestGameUnpause)" | c135-r1b-host.log:2311 |
| 41 | client "] card156: client ... back to Up (map load) +5 s: own pawn" over the whole log (no WARN prefix) | death.cpp:2456 |

Predictions: the host's screen-up line shows its IDs; the client's GameStarted line shows them as the after values with
no WARN suffix; the client screen shot shows the host's title and image (the tip may differ); the deferral line comes
(the screen is held more than 5 s); the card 156 line after the close has no WARN, bShowMouseCursor 0 and UIMode not 1;
the WARN diff against c222-r3 is host +SKIP_FLIGHT only.

## Run c135-r2: result

DLL cd442736a94c40f0. Ended by the scenario after 133 s, host rc 0, client rc 0, drive rc 0, scenario passed with no
timed-out try. FATAL/ERROR/CRASH 0 on both sides, no new dumps. Host 5330 lines, WARN 2; client 4311 lines, WARN 14.

| Prediction | Line (ms) | Held |
|---|---|---|
| Host reads its IDs at the screen | host 135945216 "screen up: BP_GameInstance_C chapter 1 level 0 (GetChapterID, GetLevelID)" | yes |
| Host continues before the join | host 135951000 "host continued ... IntroStart sent to 0 of 0 client(s)" | yes |
| The join replay sends the op | host 136038159 "join replay: IntroStart (FED_C2M1 0) sent to BP_PlayerController_C" | yes |
| The client gets the IDs | client 136041681 "IntroStart (FED_C2M1, bFromSave 0, chapter 1 level 0) in" | yes |
| The client sets them before GameStarted, no WARN | client 136041700 "GameStarted ... +19 ms after the op; BP_GameInstance_C chapter 1->1 level 0->0); world paused 0" | yes, but a no-op: the client's IDs were already 1/0 |
| Client screen up | client 136041702 "dump 2 in FED_C2M1 (WB_Stack_C RequestGamePause)" | yes |
| Card 156 check deferred | client 136043036 "+5 s check deferred: the card 135 screen is up" | yes |
| Clean check after the close | client 136047751 "back to Up (map load) +5 s: ... bShowMouseCursor 0, viewport capture 1, HUD UIMode 0, IMC_DummyUI_KBM not mapped, death screens live 0" (no WARN) | yes |
| Title and image | host-c135-r2-c135-r2-host-screen.png and client-c135-r2-c135-r2-client-screen.png: both "OPERATION PAYBACK" with the same image; the tips differ (random) | yes |
| Cursor | client screen shot: a cursor at the top left while the screen is up; client end shot: none (bShowMouseCursor 0 logged). Host shots never show one | as traced |
| WARN diff vs c222-r3 | host: +SKIP_FLIGHT (and -GOD, the c222-r3 env). Client: -GOD, +11 card140 hit drift (136041682-136042988), +2 card152 bone check (136043043, 136043048) | **no** |

Notes:
- The IDs were already equal on the client on this route, so r2 does not prove the set changes a screen. r1b's
  "Level Name" was on FED_C1M1; r3 (FED_C1M1) is where the before and after values can differ. Open: where the
  client's IDs come from without the frontend.
- The host's screen shot at its 5 s mark shows "LOADING...", the client's "PRESS ANY KEY TO CONTINUE".
- The extra client WARNs: all within 1.3 s of the client's world starting (the late join, 88 s into a live fight at
  the LZ; c147, c208 and c210 on FED_C2M1 joined early and have none). card140 compares the host's NPC location at a
  hit with the fresh copy's ReplicatedMovement.Location (hit_c132.cpp:307); card152 was also seen in c212-r1. Neither is
  on a card 135 path. The cause from code is open (likely the first replicated positions of copies spawned mid-fight).
- host-c135-r2-c135-r2-host-in-game.png is black with "Skip Flight" (the flight start after the continue).

## Change for r3

The r2 host shot at the 5 s dump caught "LOADING...": no log line marked the press prompt. r2's dumps show the
difference: at the host's 5 s dump `WB_Loading_Icon_White` had Visibility 4 (SelfHitTestInvisible, "LOADING..."
on screen, c135-r2-host.log:2289); at the client's 5 s dump it had Visibility 2 (Hidden, the shot shows "PRESS ANY
KEY", c135-r2-client.log:4178). `c135_prompt_step` (intro_c135.cpp, run from `c135_step` every tick on both sides)
reads the remembered screen (element 0 of the stack `c135_note_pause` kept) and, the first tick its loading icon is
Collapsed (1) or Hidden (2), logs once per map, reads only:
`card135: <side> t=N screen prompt ready: loading icon Visibility V, PressToContinue Visibility P, LoadingFinished L,
LandscapeLoadingFinished K (+M ms after the screen's pause)`. The two bools are logged, not used: which one gates the
prompt is not verified. `C135State` gained `screen_ms` and `prompt_logged`. DLL 8edaffb0745a50fe.

## Run c135-r3: scenario audit

Run: `SCENARIO=tools/scenarios/c135-r3.txt DEPLOY=1 SKIP_MOVIES=0 GOD=0 tools/run_pair.sh c135-r3 1500` (play
check: no test skip, no FAST, no AUTODROPSHIP, no keep-screen flag). Screens before step 1 (c222-r2 audit): the
startup splash movies play out, 3D_Frontend closes on its own (UBWCOOP_TRAVEL), then the FED_C1M1 stack: element 0
WB_Loading_Screen_Final_C, element 1 WB_Cutscenes_C (c222-r2-host.log:1407, 1821).

| Step | Wait | Matches |
|---|---|---|
| 1 | host `screen up: ... (GetChapterID, GetLevelID)` | intro_c135.cpp:191 (c135-r2-host.log:1754) |
| 1 | host `screen prompt ready: ` | intro_c135.cpp:350, `c135_prompt_step` (new) |
| 2 | host `pause lifted: client connected while paused \(first Pong\)` | pause_c96.cpp:59 (c135-r1b-host.log) |
| 2 | client `playlog: client t=\d+ clock 1:` | playlog.cpp:729 |
| 3 | try host `ProcessEvent WB_Cutscenes_C\.Finished_` | death.cpp:3025 (c222-r2-host.log:1909, after a skip); a try: the natural end is not seen in any log |
| 4 | host `winkey Enter 165 -> done`, `RequestGameUnpause(... WB_Stack_C, ... requesters now 0` | testrun.cpp cmd line, dropship.cpp:804 (c222-r2) |
| 4 | host `continued ... FED_C1M1 (bFromSave 0): IntroStart sent to 1 of 1` | intro_c135.cpp:233 |
| 4 | client `IntroStart (FED_C1M1, bFromSave 0, chapter .. level ..) in`, `GameStarted ... chapter A->B level C->D); world paused` | intro_c135.cpp:283, 391 (c135-r2-client.log:3191, 3326) |
| 4 | client `screen prompt ready: `; try client `Finished_` since the mark | intro_c135.cpp:350; death.cpp:3025 |
| 5 | client `winkey Enter 165 -> done`, `card067: dump N in .. (WB_Stack_C RequestGameUnpause)` | testrun.cpp:1198 (c135-r2-client.log:4226) |
| 6 | host state `pawn=host-own` | testrun.cpp:1833 (c222-r2: the flight lands about 28 s after the brief) |

Predictions (written before the run):
1. The client's GameStarted line reads `chapter A->B level C->D` with B, D equal to the host's screen-up IDs and A or
   C different from them; the client's prompt shot shows "BASIC TRAINING" with the host's image. Stop rule: if A, C
   already equal the host's IDs and the client title is "Level Name", stop after the run and report, no second fix.
2. Both prompt shots show the press prompt ("PRESS ANY KEY", no "LOADING..."), unless the brief video still plays
   (the step 3 try timed out; then the shot shows the video and the report says so).
3. WARN diff vs c222-r3: the host adds only `config: UBWCOOP_TEST_SKIP not "1"` (SKIP_MOVIES=0; no SKIP_FLIGHT WARN:
   FAST is off), the client the same config line and nothing else new.

## Run c135-r3: result

DLL 8edaffb0745a50fe. Ended by the scenario after 289 s at line 27 (drive rc 1, host rc 0, client rc 0). FATAL/ERROR
0 on both sides, no new dumps, map loaded 2 on each side.

Failure, line 27: the host's `pause: host/standalone RequestGameUnpause(... WB_Stack_C ..., requesters now 0` line is
never logged. The `pause:` log is capped at 16 lines per map (dropship.cpp:795 `kPauseLogs`). The host used all 20
pause lines, the 16 capped plus the cap note, before the Enter press, mostly on ADLevelStreamingSubsystem unpause
calls during the 180 s step 3 wait. The audit missed the cap. The unpause itself happened: host
`card067: dump 4 in FED_C1M1 (WB_Stack_C RequestGameUnpause)` and `card135: host continued t=136737194 ... IntroStart
sent to 1 of 1 client(s)` at 136737194 (log lines 4210-4211). Steps after line 27 did not run as steps, but the
client log carries their lines.

Predictions:

1. Client IDs: `card135: client t=136737405 IntroStart (FED_C1M1, bFromSave 0, chapter 0 level 0) in` and
   `card135: client GameStarted t=136737410 ... BP_GameInstance_C chapter 0->0 level 0->0); world paused 0`. The IDs
   were already equal before the set: the prediction is wrong. The host's IDs are 0/0 too (screen-up line) and its
   screen shows "BASIC TRAINING", so 0/0 is FED_C1M1's correct pair. The client title is not seen. The only client
   shot after GameStarted (fail-line27, 136747505) shows the mission brief video (WB_Cutscenes_C, first seen
   136742444) with the card 110 "Player" tag over it. Stop rule: the IDs are equal before the set and the title is
   unobserved. Stopped, no second fix.
2. Prompt shots: the host shot (136736144, 182 s after the host prompt-ready line) shows "BASIC TRAINING", the FED_C1M1
   camp image and the tip line, with no "LOADING..." and no "PRESS ANY KEY" visible. The client prompt shot was not
   taken (line 27 failed). The client prompt-ready line fired at 136739317 (+1906 ms after the pause), 3 s before
   WB_Cutscenes_C appeared, so on the client the line comes before the brief video, not after it.
3. WARN diff vs c222-r3: host +`config: UBWCOOP_TEST_SKIP not "1"` only, client the same line only. As predicted.

Other: the host logs 4 first-chance `CRASH n/4` access violations at 136538357 on thread 448 (ntdll.dll+0x3A41C
reading 0x0, stack exe and ntdll only, no dwmapi frame), 300 ms after the map's ext sweep, game handling continued.
The same stack appears in c118c-client.log; not in c222-r3 or c135-r2.

## Senior review (2026-11-12): accepted in part

Checked by me: diff against the pre-card snapshot (all changed files, `death.cpp` and `ledger_c132.cpp` outside the
list accepted with their reasons), no function over 60 lines added, DLL `8edaffb0745a50fe`, logs and shots of
c135-r1b, r2, r3.

Proven: the host's continue sends IntroStart; the client calls GameStarted once per map and gets its own screen
(r1b, +6 ms); a late joiner gets it from the join replay (r2, +19 ms, title and image equal to the host's on
FED_C2M1); the card 156 check waits for the screen and reads clean after it (r2); the play path with real keys on
the host (r3: host continue, client GameStarted +5 ms, client brief video starts). Digest 0 mismatches in all three.

Not proven, carded: the client's title on FED_C1M1 ("Level Name" in r1b; the IDs were already 0/0 in r3, so the ID
set is not the cause there) and the client's prompt and Enter after its brief in the play path (card 224); the card
110 name tag over the screen and the brief video (card 225). The r3 scenario stopped at a wait on a capped `pause:`
line (second scenario fault of the card; card 224 waits on the `card067: dump` line). Four first-chance access
violation lines on the host in r3 (thread 448, ntdll only, also in c118c-client): watched in the soak (card 127).
