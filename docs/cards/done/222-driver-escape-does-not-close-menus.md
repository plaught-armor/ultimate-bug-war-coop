# Card 222: The test driver's Escape key does not close the host's pause or options menu

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: queued 2026-11-12, from c171-r1. Tool card, no mod code. Budget: one run.

## Seen

c171-r1 scenario lines 30 and 53: after `key Escape` no "SetGamePaused(0)" line; the host's pause menu stayed up
for the rest of the run, so every later host screenshot is the pause menu, and the world paused again when the
client left.

## Known (card 135 trace)

The driver's key goes through `InputKey` and never reaches Slate focus (card 067); the card 015
`FSlateApplication` injection failed; `movieskip.cpp` posts real window messages (Enter and a click) and the
startup movie player takes them. Card 135 run 3 needs real keys on `WB_Cutscenes_C` and
`WB_Loading_Screen_Final_C` (OnKeyDown, OnKeyUp), so the fix covers every focused widget, not Escape only.

## Do

1. Find how the game closes these menus (the widget's key handler, or its Back or Resume button) and why the
   driver's key does not reach it (focus, key-up against key-down, the widget stack).
2. Make the driver close them by the game's own path, at human input rates (human-test-driver rule), with a log
   line the scenario can wait on.

## Done when

- One run: a real key closes `WB_Loading_Screen_Final_C` on the host; open the pause menu, open options, close both; "SetGamePaused(0)" logged; the next host screenshot
  shows the game. Ends with "Report complete."

## Step 1 trace (junior, 2026-11-12, static only)

### The game's key path (window message to widget)

| Step | Function | RVA |
|---|---|---|
| 1 | `FWindowsPlatformApplicationMisc::PumpMessages` (game thread, PeekMessage/TranslateMessage/DispatchMessage) | 0x0160C430 |
| 2 | `FWindowsApplication::AppWndProc` | 0x015F21D0 |
| 3 | `FWindowsApplication::ProcessMessage`: WM_KEYDOWN/WM_KEYUP/WM_CHAR are deferred | 0x0160A050 |
| 4 | `FWindowsApplication::DeferMessage`, then `ProcessDeferredEvents` / `ProcessDeferredMessage` on the next tick | 0x015F6730, 0x01608650, 0x016088D0 |
| 5 | `FSlateApplication::OnKeyDown` / `OnKeyUp` (the message-handler base at slate+0x188, card 015) | 0x017FBA40 / 0x017FBC80 |
| 6 | `FSlateApplication::ProcessKeyDownEvent` / `ProcessKeyUpEvent` | 0x01802AC0 / 0x018031A0 |
| 7 | Inside 6: `InputPreProcessorsHelper::PreProcessInput` at +0x80. If any preprocessor returns true, the event ends here (jmp to +0x669) and OnKeyDown returns "handled" | 0x017FFB50 |
| 8 | Otherwise: the user focus path (`FWeakWidgetPath::ToWidgetPathRef` at +0x1FD), the tunnel (OnPreviewKeyDown), then the bubble (OnKeyDown) | |
| 9 | Focused UMG widget: `SObjectWidget::OnKeyDown` / `OnKeyUp`, then `UUserWidget::NativeOnKeyDown` / `NativeOnKeyUp`, then the BP override | 0x03622D80 / 0x03623050, 0x0361E6D0 / 0x0361E830 |
| 9' | If nothing on the path handles it and the viewport is on the path: `SViewport::OnKeyDown`, `FSceneViewport::OnKeyDown`, `UGameViewportClient::InputKey`, then the PC | 0x019ADBD0, 0x041E91B0, 0x03C5D070 |

ProcessKeyDownEvent itself has no window-active check (stock UE5 shape: preprocessors, drag-drop Escape, then
focus routing).

### Cause 1: why the driver's `key` does not reach Slate focus

`host key` calls `APlayerController::InputKey` directly (autoinput, `c63_start` Op::Key, testrun.cpp:1471). That
is step 9' entered at its end: it skips steps 1 to 9. The focused widget's OnKeyDown/OnKeyUp (step 9) never runs.
The pause menu opens because opening it is a PC input action. Closing it, and closing the options page, the loading
screen and the cutscene, are widget key handlers that only Slate focus routing reaches (card 067's c67e reference:
WB_Stack_C/WB_Cutscenes_C OnPreviewKeyDown, then WB_Cutscenes_C.OnKeyDown, then WB_Loading_Screen_Final_C.OnKeyUp).

### Cause 2: why card 015h's FSlateApplication injection and card 063's posted WM_KEYDOWN failed

The game registers its own Slate preprocessor, `ADInputDetectionProcessor`. Its key-down handler
(`HandleKeyDownEvent` 0x04C5ED40) is:

```
slate = FSlateApplication::CurrentApplication            ; global, RVA 0x09299C88
if (!slate->IsActive()             /* vtable slot 1 = FSlateApplication::IsActive 0x017F7DC0: movzx eax, [rcx+0x1E0] */
    && slate[+0x504] == 0)         /* the "handle device input when the application is not active" flag */
    return true;                   ; CONSUMED: step 7 ends the event, OnKeyDown returns "handled"
HandleAnyInputKeyEvent(event);     ; 0x04C5EC70 (input-type detection)
return false;
```

- slate+0x1E0 is `bAppIsActive`: `ProcessApplicationActivationEvent` 0x018019C0 writes it (`mov [rcx+0x1E0], dl`
  at 0x018019F4), and `IsActive` returns it.
- slate+0x504 is the same flag `FCommonInputPreprocessor::IsRelevantInput` 0x04BE4100 reads right after its
  IsActive call (`cmp byte ptr [rbx+0x504], al` at 0x04BE412E), which is the UE source's
  `IsActive() || GetHandleDeviceInputWhenApplicationNotActive()` test.
- The test windows run side by side and neither has OS focus, so bAppIsActive is 0 and every key down is eaten at
  step 7, before focus routing. That is card 015h exactly: "each press was handled (1/1)" (OnKeyDown returned the
  preprocessor's true) and nothing closed. Card 015h's note "never got keyboard focus" was a guess; card 067 later
  logged the loading screen taking focus (OnAddedToFocusPath, OnFocusReceived) in unfocused runs, so focus was
  there. Card 063's c63d `winkey` (posted WM_KEYDOWN) reached step 5 and died at the same check. card 067's
  "probably not focused" is now this check.
- The processor has no key-up or analog-key override that gates (HandleKeyUpEvent is not overridden;
  HandleMouseButtonDownEvent 0x04C5EDA0 always returns false). Key ups and clicks pass unfocused; key downs do not.
- xdotool keys (drive57, c67e) worked because the window had OS focus, so bAppIsActive was 1.

### What movieskip's posted messages reach

movieskip.cpp finds this process's own visible top-level window (`EnumWindows` + `GetWindowThreadProcessId` == own
pid) and posts WM_KEYDOWN/WM_KEYUP VK_RETURN (down and up back to back) and a left click. These go through steps 1
to 6. The key down dies at step 7 while unfocused; the key up and the click pass the preprocessor and reach the
focus path. movieskip's skip itself does not need them: it skips through `OnAnyDown` 0x036DB620 when byte +0xDD is
set.

### Why a posted message stays with its own window

`PostMessageW(hwnd, ...)` puts the message on the queue of the thread that owns `hwnd`. Each game process posts
only to its own window (the own-pid filter above), and only that process's game thread pumps it (step 1). No
foreground window, no OS focus, and no SendInput (which is global and needs foreground, card 018) is involved. Two
windows side by side never see each other's keys.

## Step 1 plan

### Command: `winkey <Key> [hold_ms]`

- Default hold 150 ms (the `key` default, `kDefaultHoldMs`). A human press, one key, no chord.
- On the game thread, at start:
  1. The own window from movieskip's `own_window` (moved to a shared helper so both use one copy).
  2. `slate = *(void**)(exe + 0x09299C88)`. If null: fail "no Slate application".
  3. If slate+0x1E0 == 0 and slate+0x504 == 0, set slate+0x504 = 1 for this press only, and remember to restore it.
     This is what a focused window has: the preprocessor lets the key through, and everything after step 7 is the
     game's own path.
  4. Post WM_KEYDOWN, VK from a small FKey-name-to-VK table (Escape, Enter, SpaceBar, BackSpace, Tab, A-Z, 0-9,
     arrows), lParam = 1 | (MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16).
- At hold end: post WM_KEYUP with bits 30 and 31 set.
- 100 ms after the key up (the deferred message runs on the next tick): restore slate+0x504 to its old value, then
  `done`.
- One new data RVA (`FSlateApplication::CurrentApplication`), two field offsets (0x1E0, 0x504). No new detour, no
  new function RVA.

### Log lines

```
card222: host t=N winkey Escape down: VK 0x1B, hold 150 ms, window 0x..., Slate active 0, not-active input 0 -> 1
card222: host t=N winkey Escape up after 151 ms; not-active input restored to 0 -- done
```

### Files

- `mod/src/cmdfile.cpp`, `cmdfile.h`: the `winkey` verb (Op::WinKey), parsed like `key` without the chord.
- `mod/src/ubw/testrun.cpp`: start/continue/finish in `c63_start` and the continue step.
- `mod/src/movieskip.cpp` and a small shared helper (`winmsg.cpp`/`.h`) for User32, `own_window` and the post.
- `tools/symbols.txt` (the data RVA), `tools/drive.py` (the `winkey` verb in COMMANDS).
- `tools/scenarios/c222-r1.txt`.

### Run c222-r1 (one run, solo host block; the client joins late and is not used)

- Env: no `UBWCOOP_AUTODROPSHIP`, no FAST (FAST's SKIP_FLIGHT needs AUTODROPSHIP), no `ui continue`, so the brief
  and the loading screen stay up as in c67e.
- Steps:
  1. Wait for "card067: dump N in FED_C1M1 (WB_Stack_C RequestGamePause)". Shot c222-brief.
  2. `host winkey Enter 165` (the c67e press). Wait for the RequestGameUnpause pause line and "card067: dump N in
     FED_C1M1 (WB_Stack_C RequestGameUnpause)". Shot c222-brief-closed.
  3. After the flight lands and the pawn is the host's own: `host winkey Escape 150`; wait "SetGamePaused(1)". Shot.
  4. `host ui click BndEvt__WB_Pause_Menu_OPTIONS_button` (as c171-r1). Shot.
  5. `host winkey Escape 150` (closes options), then `host winkey Escape 150` again if SetGamePaused(0) has not come
     (the c171-r1 `when` pattern). Wait "SetGamePaused(0)". Shot c222-closed.

### Prediction

- Step 2: one winkey Enter. The key down passes the preprocessor (flag 1 for the press), WB_Cutscenes_C.OnKeyDown
  skips the video, the loading screen takes focus, and the same press's key up runs
  WB_Loading_Screen_Final_C.OnKeyUp. WB_Stack_C then calls RequestGameUnpause with the stack empty: the pause
  line shows "requesters now 0" and the c67 unpause dump follows within 1 s. c222-brief-closed shows the flight, not
  the brief.
- Steps 3 to 5: SetGamePaused(1) on the first Escape, the options shot shows the options page, and SetGamePaused(0)
  comes within 2 Escapes. c222-closed shows the game.
- Each winkey logs both lines, and the restore line shows the flag back at 0.
- No new WARN class against c171-r1.
- If step 2 shows the key down logged but no cutscene close, the preprocessor is not the only gate, and the run
  stops there (no retry with another method inside this card).

### Open points

1. The flag write is a test-driver-only state change for 150 to 300 ms per press. During that window a gamepad on
   the machine would also reach this unfocused game. Tests use no gamepad. Is that acceptable, or should the press
   instead detour `ADInputDetectionProcessor::HandleKeyDownEvent` to pass only the driver's own key?
2. Should `key` move to this path later? It is the human path for gameplay keys too (step 9'), but every scenario
   uses `key`, so that is a separate card.
3. The client's own menus (when the client presses Escape) use the same path, so `client winkey` works the same
   way. It is not in this run.

## Build

- DLL dc5c09ae0ebc3835, /W4 /WX clean.
- `tools/pdb_rva.py:126` (senior option A): data symbols are accepted as `@@0` to `@@3` (private, protected and
  public static members, globals), not only `@@3`. `?CurrentApplication@FSlateApplication@@0V...` is a private
  static member. `offsets.generated.h` differs from before by 2 added lines: the new entry and its comment
  (`FSlateApplication_CurrentApplication`, 0x09299C88).
- `mod/src/winmsg.cpp`/`.h`: User32, `resolve` and `own_window` moved out of `movieskip.cpp`. `press()` stays in
  movieskip, untouched, so its messages and their order are the same. The two error lines are
  "ERROR %s: ... -- %s" with "movieskip"/"no skip": the same text. Checked twice:
  - by diffing the file before and after;
  - by comparing c222-r1's movieskip lines with c171-r1's (times and pointers masked): the same 7 lines in the same
    order.
- Differences from the plan:
  - The key table holds Escape, Enter, SpaceBar, Tab, BackSpace and the four arrows, with fixed scan codes (no
    MapVirtualKeyW, no A-Z or 0-9).
  - There are three log lines: down; "up after N ms"; then "not-active input restored to N -- done".
  - The default hold is `kDefaultHoldMs` (100 ms; the plan's 150 was wrong). The scenario always gives the hold.
- Second winkey: the queue is serial, so a second one waits behind the first. If the flag is still owned when one
  starts, it fails "a winkey still holds the not-active input flag" (a WARN cmd line).

## Run c222-r1: void before the first winkey

- The run ended at scenario line 10 after 79 s. No winkey ran: there is no card222 line and no `cmd:` line except the
  fail shot.
- Cause: run_pair sets UBWCOOP_TEST_SKIP=1 by default (SKIP_MOVIES=1, card 135). The card 135 path closed the
  brief's loading screen itself, 3.3 s after the brief pause:
  - "card067: ui continue: ProcessEvent(WB_Loading_Screen_Final_C ..., Remove Current Menu)";
  - "card130: host cinematic loading screen ... closed (try 1)";
  - "pause: ... RequestGameUnpause(requester ... WB_Stack_C ...) requesters now 0".
  The "5 s after the WB_Stack_C pause" dump never comes when the unpause is within 5 s.
- Line 9 also matched the 3D_Frontend title screen's WB_Stack_C pause (dump 1), not the FED_C1M1 brief (dump 2).
  The scenario's premise that the brief stays up is wrong under SKIP_MOVIES=1. The c67e run that the plan copied
  predates card 135.
- WARN classes: one, "WARN attach: DisableThreadLibraryCalls failed", which c171-r1 also has. No new class.

## Run c222-r2: wait audit

Scenario `tools/scenarios/c222-r2.txt`, run with SKIP_MOVIES=0 (UBWCOOP_TEST_SKIP=0), no AUTODROPSHIP, no FAST.

### Screens between process start and the pause-menu block (skip off)

With UBWCOOP_TEST_SKIP not "1", dllmain does not start movieskip and `cine_skip_step` returns at once, so nothing in
the mod skips or closes a screen.

| # | Screen | What closes it in this run | Evidence |
|---|---|---|---|
| 1 | Startup splash movies | Nothing needed: they play out. | Pre-movieskip runs (card010, card011, card014b, card014d, card015g, card016a, card017c, card019c): the first game world comes 20 to 35 s after process start with no input. |
| 2 | 3D_Frontend title screen (WB_SPCAMPOS_1_TitleScreenWidget_v2, WB_Stack_C pause) | Nothing needed: UBWCOOP_TRAVEL's SetClientTravel 1000 ms after the first game world; WB_Stack_C unpauses at LoadMap. | c222-r1-host.log:954 and 1080-1081. |
| 3 | FED_C1M1 brief: WB_Cutscenes_C video, then the WB_Loading_Screen_Final_C any-key screen (WB_Stack_C pause) | The winkey Enter (the test). The key down skips the video, the key up removes the loading screen (c67e). If the video has ended first, the key up on the loading screen closes it. | c120a-host.log:1041 to the client join: untouched, the brief held the pause for 66 s. |
| 4 | Dropship flight (AUTODROPSHIP unset) | Nothing needed: the flight lands on its own about 28 s after the brief closes. No FED level sequence has been logged in any run. | c130d1-host.log: brief unpause 162628064, landing PossessedBy 162656490. c120a: pause lifted 91617625, landing 91645682. |
| 5 | Landing: SetGamePaused(1) then (0) within 8 ms | Nothing needed. It comes before the landing possession, so before pawn=host-own. | playtest-host-20261106-pause.log:1267 and 1271, possession at 1281. |

### Each wait

| Line | Pattern | Proven by (state the step expects) | Earlier match ruled out |
|---|---|---|---|
| 11 | `card067: dump \d+ in FED_C1M1 \(5 s after the WB_Stack_C pause\)` | c120a-host.log:1307 and c67e-host.log (dump 3): the brief up 5 s after its pause. | The title screen's dumps read "in ?" (inside LoadMap) or "in 3D_Frontend" (a 5 s dump, if the unskipped movie keeps the title screen up 5 s). The map name is reset on each map change. |
| 17 | `cmd: \d+ winkey Enter 165 -> done` | The shared c63_result line: c171-r1-host.log:2276 ("cmd: 4 ui click ... -> done"). WinKey finishes through the same c63_finish. | since-mark, set after line 11 (line 15). |
| 18 | `pause: host/standalone RequestGameUnpause\(requester \S+ WB_Stack_C, .*requesters now 0` | c222-r1-host.log:1768 and c130d1-host.log:1318: the brief's unpause. | since-mark: the title screen's unpause is before the mark. ADLevelStreamingSubsystem unpauses name another requester. |
| 19 | `card067: dump \d+ in FED_C1M1 \(WB_Stack_C RequestGameUnpause\)` | c222-r1-host.log:1769. | since-mark. |
| 23 | state `pawn=host-own` | c120a.drive line 5 and c130d1.drive line 6: true only from the landing possession (c120a matched at +132.7 s, at the landing, although the map had loaded 95 s before). | Not host-own while the brief is up or in flight (c120a: 66 s of brief and the flight without a match). |
| 26 | `cmd: \d+ winkey Escape 150 -> done` | As line 16. | since-mark. |
| 27 | `card096: host t=\d+ SetGamePaused\(1\) = 1 \(world paused\), 0 client` | c171-r1.drive line 19: the pause menu opened solo. playtest-host-20261106-pause.log:1420: a human's pause menu after landing. | since-mark, set after pawn=host-own, so after the landing blip. |
| 32 | `cmd: \d+ ui click BndEvt__WB_Pause_Menu_OPTIONS_button -> done` | c171-r1-host.log:2276. | since-mark. |
| 37 | `cmd: \d+ winkey Escape 150 -> done` | As line 16. | since-mark. |
| 38, 40 | `card096: host t=\d+ SetGamePaused\(0\)` | playtest-host-20261106-pause.log:1643: a human closed the pause menu solo (opened at 1420). | since-mark, set after the pause opened. |
| 39 | `when host last ... "SetGamePaused\(1\)"` | c171-r1.drive line 29. | Reads the last SetGamePaused line only. |

- Changes from c222-r1:
  - Line 9 (it matched the title screen) is gone. The brief wait is keyed on FED_C1M1.
  - The card222 down-line wait is gone, because no earlier log can hold a card222 line. The down, up and restore
    lines are read from the log after the run.
- Prediction amended: SKIP_MOVIES=0 makes `read_switch` log "WARN config: UBWCOOP_TEST_SKIP not "1" ... off". This is
  a WARN class that c171-r1 does not have. It is expected from the run form and is not counted as a miss.

## Run c222-r2: result

DLL dc5c09ae0ebc3835 (unchanged from r1). Run ended by the scenario after 87 s at line 18 (no failsoft); host rc 0,
FATAL/ERROR 0, no crash dumps, no game left running.

| Line | Prediction | Result | Log line (logs/c222-r2-host.log) |
|---|---|---|---|
| 11 | brief up in FED_C1M1 | met | `[ 133121810 ms] card067: dump 3 in FED_C1M1 (5 s after the WB_Stack_C pause)` |
| 16-17 | winkey Enter posted, done | met | `[ 133122314 ms] card222: host t=133122314 winkey Enter down: VK 0x0D, hold 165 ms, window 00000000000100C0, Slate active 1, not-active input 0 -> 0`; `[ 133122572 ms] ... winkey Enter up after 258 ms`; `[ 133122842 ms] ... not-active input left at 0 -- done`; `cmd: 2 winkey Enter 165 -> done` |
| 18 | WB_Stack_C RequestGameUnpause, requesters now 0 | missed | none after the mark (timeout after 10 s) |
| 19 onward | brief closed, pause menu block | not reached | |

Reading:

- The key reached the game the way a real key does: WB_Stack_C.OnPreviewKeyDown, WB_Cutscenes_C.OnPreviewKeyDown,
  WB_Cutscenes_C.OnKeyDown, OnKeyChar, then WB_Cutscenes_C.OnKeyUp and WB_Stack_C.OnKeyUp (log 133122327 to
  133122588). Slate was active (1), so the not-active input flag was not needed and was not set. The card 222 gate
  (ADInputDetectionProcessor) did not eat this key.
- The cutscene did not close: no WB_Cutscenes_C.Destruct, no WB_Loading_Screen_Final_C focus, OnKeyChar or OnKeyUp.
  The fail shot shows the brief video still playing.
- The difference from c67e is timing. In c67e the WB_Loading_Screen_Final_C ubergraph ran (6923506) before the
  cutscene widget came up (6923946) and the key came 2.1 s after that; OnKeyDown then destroyed the cutscene. In r2
  the press came 0.5 s after the cutscene widget came up (dump 3 at 133121810), and the WB_Loading_Screen_Final_C and
  WB_Cutscenes_C ubergraphs ran only after it (133123866), followed by WB_Cutscenes_C.Finished_978BFF66... (133124621).
  The brief shot (c222-brief) shows the Federal Developer logo, the start of the video. The skip is armed by that
  later ubergraph step, so the press landed before the brief accepts a skip.
- Line 11's state ("5 s after the WB_Stack_C pause") is therefore not the "brief skippable" state. The proven ready
  line is `card067: ProcessEvent WB_Cutscenes_C.Finished_\S+` (or the WB_Loading_Screen_Final_C ubergraph line)
  after the brief dump; c67e's press came after its ubergraph line.
- Stop rule: the key down is logged and the cutscene did not close, but the log shows the key reached
  WB_Cutscenes_C.OnKeyDown, so the cause is the press time, not a second input gate. No second run in this card.

WARN classes not in c171-r1: `WARN config: UBWCOOP_TEST_SKIP not "1"` (expected from SKIP_MOVIES=0, per the
amendment). `WARN attach: DisableThreadLibraryCalls failed, err=126` is in c171-r1 as well.

Screenshots: host-c222-r2-c222-brief.png, host-c222-r2-fail-line18.png.

## Run c222-r3: wait audit

Senior order after r2: one final run on the c171-r1 route, a pair run with FAST=1, the test skip on (SKIP_MOVIES at
its default), FED_C1M1, the c171-r1 environment. No brief step (the brief key and its ready wait move to card 135
run 3). Scenario: tools/scenarios/c222-r3.txt. Run command:
`systemd-run --user --scope -q -p MemoryMax=12G env SCENARIO=tools/scenarios/c222-r3.txt DEPLOY=1 FAST=1 GOD=0
CLIENT_DELAY_MS=120000 HOST_EXTRA_ENV="UBWCOOP_AUTODROPSHIP=1 UBWCOOP_C132_FX=1" CLIENT_EXTRA_ENV="UBWCOOP_C132_FX=1"
tools/run_pair.sh c222-r3 900`

The point of the run: in c171-r1's client-in block the host's `key Escape` opened nothing (both try waits failed,
lines 42-43, and no card067 ProcessEvent line after it); this run presses the same keys with `winkey`. With two
windows at most one is active, so at least one press should log `Slate active 0, not-active input 0 -> 1` and its
restore line. If both sides log `Slate active 1`, the inactive-window path stays unproven and the report says so.

Log facts the waits depend on:

- `pause:` lines are capped at 16 per map (dropship.cpp kPauseLogs). In FED_C1M1 the ADLevelStreamingSubsystem
  unpause lines use them up before play (c171-r1-host: no `pause:` line for the menu at 2175), so no wait uses them.
  r2's line 18 waited on one; r2's fault was still the press (no WB_Cutscenes_C.Destruct), but r3 does not repeat it.
- card096 writes a line only when the outcome changes. With a client in, the host's last outcome is already
  "skipped" (c171-r1-host:2970, at the first Pong), so the menu's skipped call writes nothing. No wait uses card096.
- The card067 dump line comes from every WB_Stack_C RequestGamePause/RequestGameUnpause (dropship.cpp
  c67_note_pause), at most 12 per map; this route reaches about 6 on the host.
- `cmd: N ui click ... -> done` is written even when no menu is open (c171-r1-host: cmd 11 done at 130946478 with
  no card121 line), so the OPTIONS waits use the card121 line.

| Line | Wait | Proving log line, same state | Earlier match ruled out |
|---|---|---|---|
| 10 | `host wait state pawn=host-own` | c171-r1.drive line 11 (same env) | state, not a log line |
| 11 | `host ui continue` | c171-r1.drive line 12 | command |
| 13-16 | client join waits | c171-r1.drive lines 33-36, copied | c171-r1 ran them in this order |
| 20 | `cmd: \d+ winkey Escape 150 -> done` since-mark | c222-r2 `cmd: 2 winkey Enter 165 -> done` (the same c63_result path, key name differs) | since-mark |
| 21 | `card067: dump \d+ in FED_C1M1 \(WB_Stack_C RequestGamePause\)` since-mark | host, 1 client in: c115a-host:2421 `card067: dump 5 in FED_C2M1 (WB_Stack_C RequestGamePause)` after `SetGamePaused(1) skipped ... 1 client(s)` (death screen push, the same WB_Stack_C path); the pause menu in it: c171-r1-host:2176 dump 4 in FED_C1M1, Stack[0] = WB_Pause_Menu_C | since-mark; the "5 s after" dump has another suffix |
| 26 | `card121: ui click: ProcessEvent\(WB_Pause_Menu_C \S+, BndEvt__WB_Pause_Menu_OPTIONS_button` since-mark | c171-r1-host, 130810996: `card121: ui click: ProcessEvent(WB_Pause_Menu_C 000000007EAF7720, BndEvt__WB_Pause_Menu_OPTIONS_button_K2Node_ComponentBoundEvent), stack element 0 of 1` (solo; the click code does not read the client count) | since-mark |
| 31 | winkey Escape done | as line 20 | since-mark |
| 32 | try `card067: ProcessEvent WB_Stack_C\.Destruct ` since-mark | **predicted, not proven**: OPTIONS creates a second WB_Stack_C (c171-r1-host 130810997: OnInitialized, PreConstruct, Construct on 000000007AD178A0); closing options should destroy it, and card067 logs each new (object, function) pair. No log holds an options close (c171-r1's Escape never closed it). `try`, so a wrong prediction does not end the run | since-mark |
| 34 | try `card067: dump \d+ in FED_C1M1 \(WB_Stack_C RequestGameUnpause\)` since-mark | as line 36 | since-mark |
| 35 | `when host last "card067: dump \d+ in \S+ \(WB_Stack_C Request" "RequestGamePause"` | c171-r1 line 29 shape; dumps as above | the last Request dump is the menu's open (line 21) unless line 31's press closed the menu |
| 36 | `card067: dump \d+ in FED_C1M1 \(WB_Stack_C RequestGameUnpause\)` since-mark | host, 1 client in: c115a-host:2684 `card067: dump 7 in FED_C2M1 (WB_Stack_C RequestGameUnpause)`; FED_C1M1: c171-r1-host:5267 dump 6. **Predicted for the pause menu's close** (no log holds a menu closed by a key) | since-mark |
| 40 | `host wait goto` | c171-r1.drive line 61 (`& host goto actor BP_WeaponPickup_MoritaMk1_C 120`, done) | the drive's goto state |
| 45 | client winkey done | as line 20 | since-mark |
| 46 | `card067: dump \d+ in \S+ \(WB_Stack_C RequestGamePause\)` since-mark | client, in FED_C1M1 with the host: c121r-client:2260 `card067: dump 2 in FED_C1M1 (WB_Stack_C RequestGamePause)`, Stack[0] = WB_Pause_Menu_C, after `cmd: 12 key Escape 150 -> done` | since-mark |
| 50 | client card121 OPTIONS line | line format on a client: c121r-client:2382 (`card121: ui click: ProcessEvent(WB_Pause_Menu_C 000000006F52B700, BndEvt__WB_Pause_Menu_QUIT_button_...`); OPTIONS itself as line 26 | since-mark |
| 55 | try client Destruct line | **predicted**, as line 32 | since-mark |
| 57-59 | client unpause dump (try, when, wait) | client line format: c154-r2-client:1146 `card067: dump 1 in ? (WB_Stack_C RequestGameUnpause)`; `\S+` for the map, since the client's map name is reset at map change. **Predicted for the menu's close** | since-mark |

Screens with the test skip on: startup movies (movieskip), the title screen and the brief (closed by the skip, as
c171-r1), the flight (FAST), the landing screen (`host ui continue`, c171-r1 line 12). The client's side is c171-r1's.

Predictions:

1. Each winkey down line names the side and `Slate active` 0 or 1; at least one press on the inactive window logs
   `not-active input 0 -> 1` and later `not-active input restored to 0 -- done`.
2. Lines 21, 26, 36 met on the host, and 46, 50, 59 on the client.
3. The host walk arrives (input back after the menu).
4. Screenshots: host pause, options, options closed, closed (the game), arrived; client pause, options,
   options closed, closed.
5. Stop rule: if a close does not happen after a logged key down, the run ends there and the finding is the logged
   path (which widgets got OnKeyDown and OnKeyUp).

## Run c222-r3: result

DLL dc5c09ae0ebc3835 (`sha256sum | cut -c1-16`, run_pair "lane 1" line after DEPLOY). Ended by scenario after 192 s,
host rc 0, client rc 0, drive rc 0, no crash dumps, FATAL/ERROR 0 on both sides. Every drive step ok; the two `try`
unpause waits after the first Escape (lines 34, 57) timed out as planned (that Escape closed options, not the menu),
and the `when` steps (35, 58) pressed the second Escape.

| Prediction | Result | Log line |
|---|---|---|
| 1. inactive-window press | met, host side | host, all three presses: `card222: host t=133719428 winkey Escape down: VK 0x1B, hold 150 ms, window 0000000000020096, Slate active 0, not-active input 0 -> 1` ... `winkey Escape: not-active input restored to 0 -- done` (also 133722220/133722715, 133727292/133727812). Client, all three: `Slate active 1, not-active input 0 -> 0`, `left at 0` |
| 2. host menu open, line 21 | met | `[ 133720428 ms] card067: dump 4 in FED_C1M1 (WB_Stack_C RequestGamePause)` (1 client in) |
| 2. host OPTIONS, line 26 | met | `card121: ui click: ProcessEvent(WB_Pause_Menu_C 00000000840BB270, BndEvt__WB_Pause_Menu_OPTIONS_button_K2Node_ComponentBoundEvent)` |
| host options close, line 32 (predicted) | met | OnKeyDown on 0000000071568350 and 000000006FF62A40 at 133722229, then `card067: ProcessEvent WB_Stack_C.Destruct (0000000071568350)` at 133722481 |
| 2. host menu close, line 36 (predicted) | met | `[ 133727568 ms] card067: dump 6 in FED_C1M1 (WB_Stack_C RequestGameUnpause)` |
| 3. host walk, line 40 | met | `host wait goto -> done` at +178.7 s |
| 2. client menu open, line 46 | met | `[ 133751951 ms] card067: dump 2 in FED_C1M1 (WB_Stack_C RequestGamePause)` |
| 2. client OPTIONS, line 50 | met | `card121: ui click: ProcessEvent(WB_Pause_Menu_C 00000000518B5730, BndEvt__WB_Pause_Menu_OPTIONS_button_...)` |
| client options close, line 55 (predicted) | met | `card067: ProcessEvent WB_Stack_C.Destruct (0000000074549520)` at 133753935 |
| 2. client menu close, line 59 (predicted) | met | `[ 133758934 ms] card067: dump 4 in FED_C1M1 (WB_Stack_C RequestGameUnpause)` |
| 4. screenshots | met | below |

Reading: with the host window inactive (Slate active 0), the press that did nothing in c171-r1's client-in block now
opens the menu, closes options and closes the menu, and the host walks after it. The client's window was the active
one, so its presses ran the active path. The stop rule did not fire.

WARN classes: `attach: DisableThreadLibraryCalls failed, err=126` and `config: UBWCOOP_GOD not "1"`, both in c171-r1. None new.

Screenshots (prefix/drive_c/users/steamuser/AppData/Local/StarshipTroopers_UBW/Saved/Screenshots/Windows/):
host-c222-r3-c222-host-pause, -host-options, -host-options-closed (the pause menu back), -host-closed (the game, HUD,
no menu), -host-arrived; client-c222-r3-c222-client-pause, -client-options, -client-options-closed, -client-closed
(the game, HUD, objectives).

## Senior review (2026-11-12): accepted

Diff against the pre-card snapshot holds only the card's files: `cmdfile.cpp/.h` (`Op::WinKey`), `ubw/testrun.cpp`
(`c222_*`), `winmsg.cpp/.h` (the user32 helper moved out of `movieskip.cpp`, no behaviour change there), one CMake
line, the `FSlateApplication_CurrentApplication` symbol, one line in `tools/pdb_rva.py`, `tools/drive.py`, three
scenarios. No heap, no pointer kept past the press except the Slate object and window read at the key down (both live
for the process). DLL `dc5c09ae0ebc3835` in `build/` and `game/`.

Run c222-r3 checked by me: scenario passed, no failed line; three presses per side, each with down, up and done
lines; host "Slate active 0, not-active input 0 -> 1" and "restored to 0" three times; client active path, flag left
at 0; digest rounds 4 and 5 0 mismatches; WARN classes unchanged (two known). Screenshots viewed: host pause menu
up, host and client back in the game with no menu.

Not proven here, carried: `winkey` on the brief and the press-to-start screen (card 135 run 3); the inactive-window
path on a client. Over 60 lines, carded after the play build: `parse_tokens` 157, `c63_start` 146.
