# Card 031: Lobby overlay render test (no network)

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: sent 2026-11-03.

Read `docs/STATE.md` first: the "Mod code rules" section, and milestone 5 ("Overlay groundwork"). Then read `docs/design/coop-channel-lobby-downed.md` section 2. Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

The Host / Join-by-IP overlay (user decision 2026-11-03) is native UMG built with no assets. A bare `UUserWidget` cannot be created (the class is abstract, see step 2), so the open risk is whether a game widget class with a hand-replaced tree renders in this build. This card answers that before any lobby logic is written.

## Static leads (senior, 2026-11-03, from package name maps and PDB publics)

- **Widget class to use: `WB_TutorialPrompt`** (`/Game/Blueprints/UI_Aloft/Gameplay_HUD/WB_TutorialPrompt.WB_TutorialPrompt_C`, 3.4 KB package).
  - Its tree is a `CanvasPanel` holding a `TextBlock`, a `RichTextBlock` and an `Image`, with placeholder text ("FED DEV INFO", "blah dummy text…").
  - It has no `ExecuteUbergraph`, so it has no Blueprint graph: no `Construct` logic to fight the replaced tree.
  - It is a HUD widget, so it may not be loaded at the main menu. Load it with the existing `g_static_load_object` (`StaticLoadObject`) when `StaticFindObject` misses.
  - Cheapest first try: keep its tree, set the `TextBlock`'s text, and add only the `UEditableTextBox` to its `CanvasPanel`.
- Second choice: `WB_Menu` (`/Game/Blueprints/UI_Aloft/WidgetComponents/WB_Menu`, 3.3 KB). It has an empty tree and only an `OnKeyUp` back handler.
- Natives (RVAs from the PDB publics):
  - `FText::FromString(const wchar_t*)` 0x0122DF60. This is the game's own FText constructor that step 2 asks for. Pass a hidden return buffer for the FText.
  - `UPanelWidget::AddChild(UWidget*)` 0x03596690, which returns the `UPanelSlot*`. On a `UCanvasPanel` that slot is a `UCanvasPanelSlot`. `UCanvasPanel::AddChildToCanvas` is not exported (it is inlined), so use `AddChild`.
  - `UCanvasPanelSlot::SetPosition(FVector2D)` 0x035E21C0 and `SetAutoSize(bool)` 0x035DEBD0, to place the box.
- **Note for later cards, not this one.** `/Game/Blueprints/UI_Aloft/DevMenus/WB_DebugMenu` is a shipped developer menu. Its buttons include God mode, NoClip, Finish Level, Give All Keys, Give All Grenades, Encounter Manager Debug, AI debug display, difficulty setters and a grant-weapon check box. It casts to `STCheatManager`. It could drive test setups (cards 023 and 032), but do not open it in this card.

## Do

1. **Test-only switch.** Add `UBWCOOP_OVERLAY_TEST=1`, off by default. Run single player only, at the main menu.
2. **Build the widget.**
   - `UUserWidget` itself is abstract. Its `ClassParams` (0x077041D0) has ClassFlags 0x00A810A1, so `CLASS_Abstract` (0x1) is set; `UCanvasPanel` has 0x00A800A0. `CreateWidgetInstance` and `StaticConstructObject_Internal` refuse an abstract class, so a bare `UUserWidget` cannot be created. Confirm the flag at runtime (`UClass::ClassFlags`), then use a concrete game widget class instead: pick a simple `WBP_`/`WB_` class already loaded at the main menu, preferably one with few children.
   - Create it with `UUserWidget::CreateWidgetInstance(APlayerController&, TSubclassOf<UUserWidget>, FName)` (0x03612DE0) on the local PC. Then replace its `WidgetTree`'s root with your own `UCanvasPanel`, or clear its root's children and add to it. Log which class you used, and its tree before and after.
   - Children: a `UTextBlock` ("UBW co-op: H host, J join") and a `UEditableTextBox`. Construct them through the widget tree's outer, with `StaticConstructObject_Internal` (0x01579E50). Log how you found each class: `UCanvasPanel` and `UTextBlock` have `NoRegister` exports; find `UEditableTextBox` by name. All three are concrete (ClassFlags without 0x1).
   - Add them to the canvas, then call `UUserWidget::AddToViewport` (0x0360E990).
   - Set the text with `UTextBlock::SetText(FText)` (0x035E37A0). Build the `FText` with the game's own constructor, not by hand.
   - The game widget's own blueprint logic (its `Construct` event) still runs. If it fights the replaced tree, log it and try another class.
3. **Focus and input.**
   - F8 toggles the overlay. While it is shown, call `UWidgetBlueprintLibrary::SetInputMode_UIOnlyEx` (0x03645EA0) and `UWidget::SetKeyboardFocus` (0x0362E160) on the text box.
   - On close, restore the game's own input mode. Do not force GameOnly; card 018 found the game sets its own modes.
   - Poll the text box's `Text` property and Enter each tick. On Enter, log the contents.
   - Poll H and J too, and log them. They do nothing else in this card.
4. **Keep the widget alive.** The mod owns no UObject, so a widget only on the viewport may still be collected on travel. Log whether the widget survives one round trip from the main menu to a mission and back. If it is gone, rebuild it when F8 is pressed.
5. **Prove it.** Take `shot` screenshots: the overlay shown at the main menu, text typed into the box, and the overlay hidden. Log the Enter line with the typed text, for example `127.0.0.1:7777`.

## Done when

- The screenshots show the overlay rendering and text being typed, or the card reports exactly where the hand-built tree fails (class refused, no tree, no Slate widget, nothing drawn), with log lines.
- With the overlay closed, the game's menus still take mouse and keyboard input.
- The switch is off by default. Clean build (`/W4 /WX`, `--clean-first`), 84 exports, KERNEL32-only imports. No new crash dumps. Every new function is at most 60 lines.

## Risks

- If the replaced tree draws nothing, fall back to keeping the game widget's own tree and changing only its texts. Report which class you used.
- Widget construction outside the game thread crashes. Build everything from a game-thread hook (an existing tick hook).

## Partial work (before clear)

None. Card 031 was not started: no code changed after card 030 (no `UBWCOOP_OVERLAY_TEST` code in `mod/`), and no game instances were running.

## Report

Done. Two scripted solo runs (`UBWCOOP_TRAVEL=` empty, `UBWCOOP_OVERLAY_TEST=script`): `card031`
(80 s) and `card031b` (81 s). Both ended by the done line with host rc 0, 0 FATAL/ERROR lines and
no new crash dumps. The build is clean with /W4 /WX and --clean-first, has 84 exports and imports
KERNEL32 only. Every card031 function is at most 60 lines.

**Switch.** `UBWCOOP_OVERLAY_TEST` is off by default. `1` gives manual F8 mode; `script` also
drives the test itself with SendInput, and only while the game window is in the foreground. The
code is in the card 031 section of `mod/src/hooks.cpp`. Its only hooks into existing code are a
record-only call in `detour_set_input_mode` (the game's own input mode) and one in
`detour_exec_ui_only` (the object whose blueprint called UIOnlyEx), plus `c31_step` in
`detour_tick`. It runs only in net mode 0.

**Classes (ClassFlags at UClass+0xD4).**

| Class | ClassFlags | Abstract? | Note |
|---|---|---|---|
| UserWidget | 0x30E810A1 | yes | |
| CanvasPanel | 0x30E800A0 | no | |
| TextBlock | 0x30E800A0 | no | |
| EditableTextBox | 0x30E800A0 | no | |
| WidgetTree | 0x30C800A0 | no | |
| WB_TutorialPrompt_C | 0x00E41000 | no | Not in memory at the menu, so StaticFindObject returned null and StaticLoadObject found it. |

**Tree.**
- CreateWidgetInstance(pc) gives a widget whose outer is `BP_GameInstance_C`.
- Before: CanvasPanel root holding [Image, RichTextBlock, Image, TextBlock].
- The root is a canvas, so it was cleared with ClearChildren. A TextBlock and an EditableTextBox
  were then added, each built by StaticConstructObject_Internal with the WidgetTree as outer
  (RF_Transactional).
- After: CanvasPanel holding [TextBlock, EditableTextBox].
- The slot LayoutData.Offsets read 40/40/100/30 after SetPosition. I wrote the box's size into
  Right/Bottom.
- The widget is added with AddToViewport(z 100) and built collapsed. F8 shows it at
  SelfHitTestInvisible.

**Rendering.** It works with the native widgets, so the fallback was not needed.
- The plain `shot` command left the overlay out of the image, which made run 1 look empty.
  `shot showui` includes it; card031 now uses that.
- Run 2 shots, kept in `logs/card031-shots/`:

  | Shot | Content |
  |---|---|
  | 00203 | Overlay shown: label "UBW co-op: H host, J join" and an empty box at the top left |
  | 00204 | Box shows the typed `127.0.0.1:7777` (default style, low contrast: light grey on light grey) |
  | 00205 | Overlay hidden |
  | 00206 | Down arrow after close moved the menu highlight (keyboard reaches the menu) |
  | 00207 / 00208 | Menu after the round trip: rebuilt overlay shown, then hidden |

- The box draws at the default size, not 480x48; the size write to LayoutData did not take.

**Focus and input.**
- Show: SetInputMode_UIOnlyEx(pc, box, DoNotLock) and SetKeyboardFocus run one tick after it is
  shown. The SendInput characters (KEYEVENTF_UNICODE) reached the box.
- Enter logged `card031: Enter: text box "127.0.0.1:7777" (14 chars)` in both runs. The text is
  read with FText::ToString on the box's reflected `Text` property.
- Hide: restores the game's recorded mode, UIOnly (lock 1), through native UIOnlyEx with
  WB_SPCAMPOS_1_TitleScreenWidget_v2_C as focus. GameOnly is never forced.
- After hide the game's own menu took keyboard input: the title screen advanced, a GameAndUI call
  followed, and the Down arrow moved the highlight.
- Side effect: the F8 that hides the overlay also reaches the title screen and counts as its
  "press any key".
- H and J are only polled and logged (GetAsyncKeyState, own foreground window only).
- Mouse is not scripted; the user should check menu clicks by hand with the overlay closed. The
  shown canvas is SelfHitTestInvisible, so it should let clicks through.

**Survival.**
- The widget did not survive travel: at FED_C1M1, and again back at 3D_Frontend, the log shows
  `overlay widget gone`.
- On the next F8 it rebuilt cleanly: class lookup, tree and AddToViewport, the same as the first
  build.
- The mod keeps only FWeakObjectPtrs to the widget, the box and the focus object, and compares the
  world by address.

**Notes for the lobby card.**
- Give the box an explicit style or colours.
- Find a working size path: the slot size via `SetOffsets`/`SetSize` is not exported, so it needs
  either a reflection-correct LayoutData write or SizeBox wrapping.
- Swallow F8 so the title screen does not see it.

**Post-test change (14:58).** After card031b I changed only the comment above `c31_request_shot`, to
record the observed reason for `shot showui`, then rebuilt with --clean-first (84 exports, KERNEL32
only). No code changed, so I did not rerun. The deployed DLL (99fc1646...) is the build that was
tested; build/dwmapi.dll (576dfe21...) differs from it only by that rebuild.

## Senior review (2026-11-03)

**Accepted.** Verified by the senior:

- `card031-run.out` and `card031b-run.out`: each run ended by its done line (80 s and 81 s). Host rc 0, FATAL/ERROR 0, no new crash dumps. The only WARN is the intended `UBWCOOP_TRAVEL is empty`.
- In `card031b-host.log`: the class flags (lines 301–306), `Enter: text box "127.0.0.1:7777"` (347), `overlay widget gone` at FED_C1M1 (546) and again at 3D_Frontend (695), then a clean rebuild (712).
- Screenshot 00204 shows the label and the box at the top left. The typed text cannot be read in the shot (light grey on light grey), so the Enter log line is the proof that typing worked.
- Post-test change: the disassembly of `build/dwmapi.dll` and of the deployed DLL is identical, so the two differ only in build metadata. This agrees with the junior's comment-only statement.
- No function in `hooks.cpp` is over 60 lines.

**For the lobby card (milestone 5):**

- The base widget is `WB_TutorialPrompt_C`, its tree cleared and rebuilt. A widget made with `CreateWidgetInstance` has the game instance as its outer, yet it does not survive travel. The lobby must rebuild it on each world, keyed by world address, as this card does.
- Open items:
  - Size: the box ignores the LayoutData write. Wrap it in a `SizeBox`, or find the reflected `FAnchorData` layout.
  - Style: the box text is light grey on light grey and needs explicit colours.
  - F8: the key must be swallowed so it does not count as the title screen's "press any key".
  - Mouse: the overlay with mouse input is untested.
- `shot showui` is needed to capture UMG overlays; record this in STATE runtime facts.
- Menu tests need `UBWCOOP_TRAVEL=` set empty in `HOST_EXTRA_ENV`, because `run_pair.sh` sets `FED_C1M1?listen` on the host by default.
