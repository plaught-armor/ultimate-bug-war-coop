# Card 083: Split S4b: input and widget to `ue/input.*` and `ue/widget.*`

Status: sent 2026-11-05 15:24 to junior ultimate-bug-war-coop-bb, after card 082 was accepted. Drafted 2026-11-05 (senior).

This is the second half of S4 in `docs/split-map.md` ("Move order"). Card 082 moved `ue/actor` and `ue/gas`. Use its names and follow its rules: a typedef goes with its pointer, `extern` pointers stay resolved by today's `kCalls`, each offset is checked against an engine type, and a function whose log text names a card stays.

Read the card 082 Report and Senior review, `docs/split-map.md` (the "Proposed files" rows `ue/input.cpp` and `ue/widget.cpp`, and every "Section map" row that cuts names to them), and `mod/src/ue/actor.h`. Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

Input and widget are the last `ue/` leaves before S5 (hook table) and S6 (net). After this card, every engine slot except the net ones lives in `ue/`.

## Map corrections (senior)

- **`g_client_game_only` stays.** It is the `UBWCOOP_CLIENT_GAMEONLY` card switch, written by `read_card_flags`. It moves with `read_card_flags` in S5.
- **`kFrameNode` goes to `ue/reflect.h`.** It is an `FFrame` offset, so it belongs with the reflection code.
- **`kHitFlags` moved in card 082** with the trace code (`FHitResult`).
- **`kViewportLockMode`** goes to `ue/input` with `kViewportCaptureMode`. The map leaves it out.
- **`g_request_exit`** uses `autoinput::RequestExitFn`. Keep that typedef where it is, and include `autoinput.h` from `ue/input.cpp` only if the build needs it. If `ue/` would then include a `ubw` header, leave `g_request_exit` in `hooks.cpp` and say so in the Report.
- **The card 031 widget typedefs** (`FTextFromStringFn`, `FTextToStringFn`, `WidgetVisibilityFn`, `WidgetFn`, `CreateWidgetInstanceFn`, `AddToViewportFn`, `PanelAddChildFn`, `UiOnlyExFn`) go to `ue/widget.h` with their pointers, not to `ue/actor` as the c031 row says.
- **The input typedefs** (`GetLocalPlayerFn`, `FKeyDestructorFn`, `AddMovementInputFn`, `ConsoleCommandFn`, `RequestScreenshotFn`, `GetInterfaceAddressFn`, `WorldGetFn` and the rest) go to `ue/input.h` with their pointers.
- **`GetSubsystemFn`** is in `ue/actor.h` since card 082. `g_get_enhanced_input` uses it from there.

## Scope

**`ue/input.{h,cpp}`**, namespace `ue::input`:

- The pointers, each with its typedef: `g_get_local_player`, `g_get_enhanced_input`, `g_get_interface_address`, `g_world_game_viewport`, `g_input_mode_game_only_ctor`, `g_fkey_ctor`, `g_key_args_ctor`, `g_fkey_destructor`, `g_add_movement_input`, `g_pc_console_command`, `g_request_screenshot` and `g_request_exit` (see above).
- The offsets: `kSubsystemInputInterface`, `kControllerControlRotation`, `kControllerIgnoreMove`, `kControllerIgnoreLook`, `kContextMappings`, `kMappingBytes`, `kMappingKeyName`, `kMaxMappingsScanned`, `kInstanceSourceAction`, `kTrackedModifiers`, `kViewportCaptureMode` and `kViewportLockMode`.
- `clear_pending_starting_mappings`, `cursor_bit` and `vec_len`.
- **The `request_shot` rename (from "Renames"):**
  - Add `ue::input::console_command(void* pc, const wchar_t* cmd)`. It builds the `FStringRaw` from the length of `cmd`, calls `g_pc_console_command`, and frees `out.data`, the same as the two bodies today.
  - Add `request_shot(void* pc, bool show_ui)`. It calls `console_command` with `L"shot showui"` when `show_ui` is true, and with `L"shot"` when it is false.
  - Replace every `c25_request_shot(pc)` with `request_shot(pc, false)`, and every `c31_request_shot(pc)` with `request_shot(pc, true)`. Delete both old functions and the forward declaration at ~6384.
  - Leave the other `g_pc_console_command` callers alone, for example `c56_disconnect`. They move with their card files.

**`ue/widget.{h,cpp}`**, namespace `ue::widget`:

- The pointers, each with its typedef: `g_ftext_from_string`, `g_ftext_to_string`, `g_text_block_class`, `g_editable_text_box_class`, `g_widget_set_visibility`, `g_widget_set_keyboard_focus`, `g_widget_remove_from_parent`, `g_create_widget_instance`, `g_add_to_viewport`, `g_panel_add_child` and `g_ui_only_ex`.
- Check that `g_text_block_class` and `g_editable_text_box_class` are engine UMG classes (`UTextBlock`, `UEditableTextBox`), not UBW ones. A UBW class stays.

**From card 082** (its Report, "Kept back"). Check each against an engine type, as card 082 did:

- `kHitBlocking`, `kHitStartPenetrating`, `kHitSize`, `kHitTime`, `kHitItem` and `kHitMyItem` (`FHitResult`) go to `ue/actor`.
- `kMoveWalking` and `kMoveFlying` (`EMovementMode` values), `kVtSetMovementMode` (the `UCharacterMovementComponent` vtable slot) and `kBodyCollisionEnabled` (`FBodyInstance`) go to `ue/actor`.
- `kSpecFlags` (`FGameplayAbilitySpec`) goes to `ue/gas`. Keep its "inferred" comment.

If a listed name is not where the row says, or a moved function calls a name that stays in `hooks.cpp`, say so in the Report and leave that name where it is.

## Rules

1. `ue/core.h` includes both new headers. `hooks.cpp` gets one `using namespace` line each for `ue::input` and `ue::widget`, next to the others. No `using namespace` goes in a header.
2. Do not reorder `kCalls` or `kHooks`.
3. No behaviour change except the `request_shot` merge. The console strings, the `FStringRaw` lengths (5 and 12, counting the terminator) and the free must stay exactly as they are today.

## Proof

- **Build:** `systemd-run --user --scope -q -p MemoryMax=12G ninja -C build -j4`, with no warnings.
- **fnlen:** run it on `hooks.cpp`, `ue/input.cpp` and `ue/widget.cpp`. No function may be over 60 lines.
- **Routes:**
  - the base routes `host-morita`, `client-zipline`, `c47-gates` and `c75-solo` (`DEPLOY=1` on the first run);
  - plus `c68-minimap` (widgets and FText) and one route that takes a `showui` shot. Name it in the Report, and show that its shot still has the HUD while a plain `shot` does not.
- **Log check:** run `c75-solo` and `c68-minimap` on the card 082 DLL before you change anything. Diff each against the new DLL's run, using the card 077 normaliser and sorted unique line texts. State a reason for each difference.
- `logsum` gives ERROR/FATAL 0, rc 0, and there is no new dump.

## Done when

- `ue/input.*` and `ue/widget.*` exist, and `ue/core.h` includes both.
- `grep -n 'c25_request_shot\|c31_request_shot' mod/src` finds nothing.
- `grep -n 'c[0-9][0-9]\|card' mod/src/ue/input.* mod/src/ue/widget.*` finds nothing.
- Every route passes, the log check is explained, the shot check is shown, and the Report gives the `hooks.cpp` line counts before and after.
- The Report ends with "Report complete."

## Report

### Files and moves

- New `mod/src/ue/input.h` / `input.cpp` (namespace `ue::input`, includes `ue/actor.h` and `ue/reflect.h`):
  - Typedefs: `GetLocalPlayerFn`, `FKeyDestructorFn`, `GetInterfaceAddressFn`, `WorldGetFn`, `InputModeCtorFn`, `AddMovementInputFn`, `ConsoleCommandFn`, `RequestScreenshotFn`.
  - Pointers (defined as nullptr in the .cpp, original comments kept): `g_get_local_player`, `g_get_enhanced_input` (typed `ue::actor::GetSubsystemFn`, which stays in actor.h), `g_fkey_destructor`, `g_get_interface_address`, `g_world_game_viewport`, `g_input_mode_game_only_ctor`, `g_add_movement_input`, `g_pc_console_command`, `g_request_screenshot`.
  - Offsets: `kControllerControlRotation`, `kControllerIgnoreMove`, `kControllerIgnoreLook`, `kSubsystemInputInterface`, `kContextMappings`, `kMappingBytes`, `kMappingKeyName`, `kMaxMappingsScanned`, `kInstanceSourceAction`, `kViewportCaptureMode`, `kViewportLockMode`.
  - Functions: `cursor_bit(pc)`, new `console_command(pc, cmd)` and new `request_shot(pc, show_ui)`.
- New `mod/src/ue/widget.h` / `widget.cpp` (namespace `ue::widget`): the 8 widget typedefs and 11 widget pointers. `g_text_block_class` and `g_editable_text_box_class` are engine UMG classes: they resolve through `offsets::UTextBlock_StaticClass` and `offsets::UEditableTextBox_StaticClass`.
- Card 082 leftovers:
  - `ue/actor.h`: `kVtSetMovementMode`, `kMoveWalking`, `kMoveFlying`, `kHitBlocking`, `kHitStartPenetrating`, `kHitSize`, `kHitTime`, `kHitItem`, `kHitMyItem`, `kBodyCollisionEnabled`. The component section header now reads "UActorComponent, USceneComponent, UPrimitiveComponent, FTransform".
  - `ue/gas.h`: `kSpecFlags` (keeps its "inferred" comment).
- `ue/reflect.h`: `kFrameNode`.
- `ue/core.h` includes `ue/input.h` and `ue/widget.h`. `hooks.cpp` gets `using namespace ue::input;` and `using namespace ue::widget;`; no header has a `using namespace`. `mod/CMakeLists.txt` lists both new .cpp files.
- Merge: 16 `c25_request_shot(pc)` calls became `request_shot(pc, false)` and 4 `c31_request_shot(pc)` calls became `request_shot(pc, true)`. Both old functions, their comments and the forward declaration `void c25_request_shot(void* pc); // card 025` are gone. `console_command` sets the FString length to `wcslen(cmd) + 1`, so `"shot"` is 5 and `"shot showui"` is 12, as before. It frees the out string through `ue::reflect::g_memory_free` the same way as before. The command strings are the same. Only the storage changed: string literals now, where the old code used static arrays.
- `kCalls` / `kHooks` were not reordered.

### Names not where the card row says, or kept in hooks.cpp

- `vec_len` went to `ue/actor` (declared in actor.h, body before `vec_dist` in actor.cpp), not `ue/input`. It is Vec3 maths and belongs next to `vec_dist`.
- These stay in hooks.cpp:
  - `g_fkey_ctor`, `g_key_args_ctor`, `g_request_exit`: their types are `ubw::autoinput` typedefs, so moving them would make `ue/` include a ubw header.
  - `clear_pending_starting_mappings`: it writes `g_starting_mappings_pending`, which is hooks.cpp state.
  - `kTrackedModifiers`: it sizes the card-013 log table (`ModifierEval mod[kTrackedModifiers]`). It is not an engine offset.
  - `g_client_game_only`: the map says it stays.
- Same-struct names the card did not list, left in hooks.cpp: `kMappingTriggersNum`, `kMappingModifiersNum`, `kMappingAction`, `kIfaceAddMappingContext`, `kIfaceRequestRebuild`, `kMaxMappingLines` (a log cap). These can move in a later pass if wanted.
- Typedefs that hooks.cpp now reaches through `using namespace` for pointers that stay there: `InputModeCtorFn` (`g_input_mode_game_and_ui_ctor`, `g_input_mode_ui_only_ctor`), `WidgetFn` (`g_panel_clear_children`), `WorldGetFn` (`g_world_first_pc`).
- The UInputMappingContext layout comment stays in hooks.cpp. A shortened copy, covering only the Key field, sits above the moved offsets in input.h.

### Build, size, greps

- `ninja -C build`: clean, no warnings. DLL hash f4147be095052b24 (082 DLL was e15b3c5e2320ab69).
- `hooks.cpp`: 27979 lines before, 27895 after.
- fnlen:

  | File | Functions | Over 60 lines |
  |---|---|---|
  | hooks.cpp | 1425 (was 1429) | 0 |
  | ue/input.cpp | 3 | 0 |
  | ue/widget.cpp | 0 | 0 |
  | ue/actor.cpp | 7 | 0 |

- `grep -n 'c25_request_shot\|c31_request_shot' mod/src` finds nothing (rc 1).
- `grep -n 'c[0-9][0-9]\|card' mod/src/ue/input.* mod/src/ue/widget.*` finds nothing (rc 1). The case-insensitive form also finds nothing.

### Routes (new DLL)

| Tag | Route | Time | Host lines | Client lines | ERROR | Dumps | Result |
|---|---|---|---|---|---|---|---|
| c83c | host-morita | 115 s | 1383 | 1176 | 0 | none | scenario passed |
| c83d | client-zipline | 124 s | 1796 | 1728 | 0 | none | scenario passed |
| c83e | c47-gates | 198 s | 2415 | 2304 | 0 | none | scenario passed |
| c83f | c75-solo | 84 s | 1224 | solo | 0 | none | scenario passed |
| c83g | c68-minimap | 142 s | 1903 | 2098 | 0 | none | scenario passed |

The c83e client has WARN 1. The 080 run of the same route (c80e) also had client WARN 1.

Baselines c83a (c75-solo) and c83b (c68-minimap) ran on the 082 DLL before your lighter-proof message arrived. Both passed with 0 errors. Per your message, I skipped the c68 diff.

### Shot check (c47-gates, client, c83e)

The showui route is c47-gates. It calls `request_shot(pc, true)` for the client's "card050 client HUD after a weapon apply" shot. It calls `request_shot(pc, false)` for "own downed view (client)". I matched the console shots to the log lines by time: client t=63810672 is ScreenShot02477 (15:35:00), and client t=63862216 is ScreenShot02480 (15:35:51).

- `logs/c83e-shots/client-showui-hud-after-weapon-apply.png` (ScreenShot02477): the HUD is drawn. It shows the OBJECTIVES panel, minimap, health/armour bars, and the ammo/UNARMED panel.
- `logs/c83e-shots/client-plain-own-downed-view.png` (ScreenShot02480): the same pier view with no HUD at all.

### Log check (c75-solo, c82f on the 082 DLL vs c83f)

I normalised each host log with `c77norm.sh`, ran `sort -u`, then diffed. Result: 25 lines only in c82f, 16 lines only in c83f. Every difference is run noise:

- `card038` HandleDeathEvent call numbers and which bug classes die in what order.
- `card039` Gib signal position and cell.
- `card042` gore part list: c82f logged one extra 9-part list (Chitin and Sticky gib set) for a bug that did not gib in c83f.
- `card075` mech-to-pad distances (271/274 vs 276/283) and piloted mech heights.
- `cmd: N shot <tag>-...` run tags.
- `movieskip`: 3 presses / 5 polls vs 4 presses / 7 polls.

No input, widget, console or screenshot line changed. No line appears or disappears outside these groups. Line counts are 1225 and 1224.

As a cross-check I also diffed against c83a (082 DLL, same route): 31 / 49 lines. The same groups appear, plus two more:

- `card017` ability handle numbers are shifted by 2 (spec allocation order). This shift is absent against c82f, so it is per-run.
- c83a had no JumperBug overkill gib. c82f and c83f both did, so against c83a there are 16 extra gore-part lines and the "further events not logged" cap line.

Report complete.

## Senior review (2026-11-05 15:45): accepted

- **Runs.** I checked c83a-c83g myself:
  - Every drive ends with `scenario passed`, and logsum gives ERROR/FATAL 0 for every log.
  - The newest crash dump is still c79a's (13:37).
  - The deployed DLL matches `build/dwmapi.dll` (f4147be095052b24).
- **Code.**
  - `ue/input.*` and `ue/widget.*` exist, and both greps are empty.
  - fnlen: `hooks.cpp` 1425/0, `ue/input.cpp` 3/0. `hooks.cpp` is 27895 lines.
  - `console_command` builds the same FString (`Num` counts the terminator) and frees the out string the same way as before.
- **Shot check.** I looked at both images. The showui shot has the full HUD; the plain shot has none.
- **Log check accepted.** Every c82f/c83f difference is gameplay noise, and no input, widget or shot line changed.
- **Kept-back names.** All accepted:
  - `vec_len` belongs in `ue/actor`;
  - the autoinput-typed pointers stay in `hooks.cpp`;
  - `kTrackedModifiers` is a log-table size, not an engine offset.
  - The remaining mapping-struct offsets (`kMappingTriggersNum` and the others) can move with F2 (`ubw/input_diag`) or stay. They are not needed for the batch.
