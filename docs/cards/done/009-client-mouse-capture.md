# Card 009: Client mouse is not captured (visible cursor, drag-to-look)

Read `docs/STATE.md` first: the "Mod code rules" section and the "Result after card 008" entry. Then read the report in card 008, which built the diagnostic logging this card reuses. Repo root: `/path/to/ultimate-bug-war-coop`.

## Why
Card 008 works: WASD moves the client. The mouse does not. On the client the cursor is visible, and the camera turns only while a mouse button is held. On the host, the mouse works normally.

The card 008 snapshot, taken on the client 10 s after `LoadMap`:
```
snapshot: pc=... BP_PlayerController_C, net client 1, local player ..., Pawn ... BP_PlayerCharacterMI_C
snapshot: IgnoreMoveInput 0, IgnoreLookInput 0, bShowMouseCursor 0
snapshot: viewport client ... MouseCaptureMode 3, MouseLockMode 1
```
- `EMouseCaptureMode` 3 is `CaptureDuringMouseDown`, which matches the drag-to-look.
- No `SetInputMode` call was logged on the client after the map load.

The usual sequence is that `SetInputMode(FInputModeGameOnly)` (or `UWidgetBlueprintLibrary::SetInputMode_GameOnly`) sets the viewport to `CapturePermanently_IncludingInitialMouseDown`, hides the cursor through the Slate user, and sets focus to the viewport. `CaptureDuringMouseDown` is what `FInputModeGameAndUI` sets, or the menu's leftover state. Both instances reach the map through `UBWCOOP_TRAVEL`, without the menu's normal start-game flow. The host still ends up in game mode, so something on the host's path sets it, and that path does not run on the client.

## Do
1. **Find the host's setter.**
   - Statically, find every path that puts the local PlayerController into a game-only input mode, or sets the viewport capture mode or cursor, after a map load or possession. Candidates:
     - the `ASTPlayerController` / `ASTHUD` / CommonUI activatable-widget input config, such as `UCommonActivatableWidget::GetDesiredInputConfig` and `UCommonUIActionRouterBase::SetActiveUIInputConfig`, which is how CommonUI games usually switch input modes;
     - `USTUIScreenMenu::NativeConstruct` / `NativeDestruct`;
     - GameMode / `PostLogin` / `HandleStartingNewPlayer` paths that call a client RPC;
     - loading-screen teardown.
   - For each one, say whether the client's local PC reaches it.
   - In the same build, extend the card 008 input-mode logging so it also logs on the host's **local** PC. Keep it capped and read-only. Then the senior's next run shows the host's sequence next to the client's. Log CommonUI input-config changes too, if that is the mechanism.
2. **Fix it, if step 1 proves the setter statically.** For the client's local PC only, run the same call the host gets, at the same point in the sequence. Examples: make the CommonUI router apply the game input config, or call the engine's `SetInputMode` with a game-only mode. Constructing an `FInputModeGameOnly` on the stack is fine; it is a small struct with a vtable, and you must show its layout and vtable RVA. Do not change the host or single player.
   - If the cause is not proven, ship only the step 1 logging, plus a one-shot fallback behind an env var `UBWCOOP_CLIENT_GAMEONLY=1`, default off. The fallback sets the game-only input mode on the client's local PC once its pawn is acknowledged.
3. **Do not regress card 008.** WASD must still work, and the existing logging must stay capped.
4. **Build and prove.** Use the command in STATE.md. Show:
   - a clean `/W4 /WX` build;
   - 84 exports;
   - imports only from `KERNEL32.dll`;
   - the RVAs, prologues, and offset and struct-layout evidence;
   - what each new log line will say.

   Deploy is the senior's job.

## Constraints
- Follow STATE.md's "Mod code rules": no heap use, every resource released, no stored game pointers beyond the current call, a skipped original must not leak, ≤60-line functions, bounded loops, capped logging, shared state touched only on the game thread.
- Do not launch the game, and do not touch `game/` or `logs/`. Under `prefix/`, only read the crash dumps.
- No subagents. No commits. If a delete or other action is denied, say so in the report; don't ask the senior to do it for you.

## Report
(append below)

### Summary
- **The host-side setter is not proven statically.** No native path that runs on the host and skips the client explains capture mode 3. Native code has no AST*/UST* caller of `SetInputMode` (card 008). The CommonUI router runs per local player and has no net-mode test in the paths I read. What remains is Blueprint content, such as a server-only possession event or a HUD widget's input config, and that is not in the exe.
- Shipped as the card says for the unproven case:
  1. Step-1 logging, extended to the host's local PC, plus a new CommonUI `ApplyUIInputConfig` log hook.
  2. A host/standalone snapshot after every successful `LoadMap`.
  3. A one-shot fallback behind `UBWCOOP_CLIENT_GAMEONLY=1`, off by default. It runs `FInputModeGameOnly` through `APlayerController::SetInputMode` on the client's local PC once that PC acknowledges its pawn.
- Card 008 behaviour is unchanged: the `SetPawn` receiver sync and every existing cap stay as they were.
- Build is clean under `/W4 /WX`. The DLL has 84 exports and imports only from `KERNEL32.dll`. Nothing was launched or deployed, nothing was committed, and no subagents were used. `game/` was read only for the exe's bytes, and `logs/` was not touched.

### Static findings (step 1)
All RVAs are from the Shipping exe (ImageBase 0x140000000). Listings were made with llvm-objdump, annotated from the PDB publics.

| Candidate | What it does | Reached by the client's local PC? |
|---|---|---|
| `APlayerController::SetInputMode` 0x0405F210 (vtbl slot 0xD50) | `GetWorld()->GetGameViewport()`, `Cast<ULocalPlayer>(Player)` at +0x348, then `mode->vtbl[+0x8]` (ApplyInputMode) with `LocalPlayer+0x1F8` (FReply) and the viewport client, then `vtbl[+0x10]` to bit 1 of `PC+0x6E8`. No net-mode test. | Yes, if anyone calls it. No native caller exists, only BP thunks (card 008). |
| `FInputModeGameOnly::ApplyInputMode` 0x04033690 | SetUserFocus/LockMouseToWidget on the viewport widget, then `SetMouseLockMode(1)` (+0x137), `[vpc+0x3B9]=0` (+0x13F), and `SetMouseCaptureMode(bConsumeCaptureMouseDown ? 1 : 2)` (+0x14A..+0x154). The host is therefore expected at capture 1/2, lock 1. | Yes (engine). |
| CommonUI `UCommonUIActionRouterBase::ApplyUIInputConfig` 0x04C0B000 (vtbl `??_7UCommonUIActionRouterBase@@6B@` +0x318) | Caches the config in router+0x31 (IsSet at +0x37) and skips the call when the config is equal and `!bForceRefresh`. Sets ignore move/look through PC vtbl +0x840/+0x858, then capture/lock/hide on `LocalPlayer(+0x20)->ViewportClient(+0x78)`. Capture 0/3/4 forces `SetShowMouseCursor(true)`. | Yes. The router is a `ULocalPlayerSubsystem` and has no net-mode test. |
| `UCommonUIActionRouterBase::SetActiveRoot` 0x04C415D0 | When the active root is cleared, it re-applies a hardcoded config only if `router+0x114 != 0` or the global `bResetUIInputConfigOnActivatableTreeDeactivation` (0x0906C4E1) is set. That config is `dword 0x00020000, word 0x0100` (+0x232/+0x23A), i.e. ignoreMove 0, ignoreLook 0, mode 2 (All), **capture 0**, lock 0, hide 1, with force = `!IsSet` (+0x21F..+0x22E). | Yes, but this gives capture 0, not the client's 3. |
| Other virtual callers of +0x318 | `FActivatableTreeRoot::ApplyLeafmostNodeConfig`+0x13D and `RefreshActionDomainLeafNodeConfig`+0x13E apply the leafmost active widget's `GetDesiredInputConfig` (0x04C1B6A0). | Yes. Which widget is active is content-defined. |
| `USTUIScreenMenu::NativeConstruct` | `SetInputMode_UIOnlyEx` + `or [pc+0x54C],1` (card 008). | Yes, for menus. |
| `ASTPlayerController::OnPossess` | Server-only (AController::Possess needs authority, card 008). No native SetInputMode there. A BP `ReceivePossess`/`OnPossess` override would also be server-only, and it is the most likely host-only setter, but it is not visible in the exe. | No. |

The client's snapshot values are capture 3 (CaptureDuringMouseDown), lock 1 and `bShowMouseCursor 0`. They match none of the native appliers on a fresh PC:
- CommonUI capture 3 would also have set `bShowMouseCursor` to 1.
- FInputModeGameOnly gives capture 1 or 2.
- The SetActiveRoot reset gives capture 0.
- The viewport client survives travel, so capture 3 is most likely the viewport's value from before the map load, or its ini default (`DefaultViewportMouseCaptureMode`), and nothing re-applies a game mode on the client afterwards.

The new logs below will show which writer the host has and the client lacks: `SetInputMode` or `ApplyUIInputConfig` with its caller RVA, and the host snapshot's capture value.

### Code changes
- `tools/symbols.txt` / `offsets.generated.h`: added `UCommonUIActionRouterBase_ApplyUIInputConfig` 0x04C0B000 (hook) and `FInputModeGameOnly_Ctor` 0x0402AC20 (called).
- `config.h/.cpp`: added `read_client_game_only()`, which reads `UBWCOOP_CLIENT_GAMEONLY` into a 4-wchar stack buffer through the existing `read_env`. Only the exact value `1` enables the fallback, and every outcome is logged. `install()` now takes it as a second argument (`dllmain.cpp`, `hooks.h`), and it is written to `g_client_game_only` before any hook is enabled.
- `hooks.cpp`:
  - `is_local_pc(pc)` = `GetLocalPlayer(pc) != nullptr`. `SetInputMode` and `SetShowMouseCursor` now log for any local PC (the host's own, standalone, or the client's) instead of only net clients, and each line carries the net mode. The host's remote-client PCs have a `UNetConnection` and are still skipped. The caps are unchanged (16, then one "further … not logged").
  - New log-only hook `detour_apply_ui_input_config`. It copies the router's previous active config (6 bytes + IsSet) and the viewport capture/lock into locals, calls the original with the same arguments, then logs. Capped at 16 through `g_ui_config_logs`. No pointer is kept.
  - The snapshot is now armed after every successful `LoadMap`, for the host/standalone too, not only on the client. That is one snapshot per map load, game thread only.
  - Fallback `client_force_game_only(pc)`:
    1. `g_game_only_pending` is armed in `LoadMap` return only when the env var is set and the load joined a server, and cleared on `LoadMap` enter. It is game thread only.
    2. In `detour_acknowledge_possession`, after the original, the fallback runs if the flag is armed, `pawn != nullptr`, the PC is a net-client `ASTPlayerController`, and the PC is local. It clears the flag first, so it runs once per map load.
    3. It constructs `FInputModeGameOnly` in a 16-byte `alignas(8)` stack buffer using the game's own ctor, then calls the `SetInputMode` trampoline. The trampoline is used so the call is logged once by the fallback's own line, not twice.
    4. The dtor is skipped because it only rewrites the vtable pointer (0x0402D2A0: `lea rax,[FInputModeDataBase vtbl]; mov [rcx],rax; ret`), so nothing leaks.
  - Host and standalone behaviour is unchanged. With the env var unset, the client is unchanged apart from the extra logs.

### Layout evidence
- `FInputModeGameOnly` (16 bytes): ctor 0x0402AC20 is `lea rax,[??_7FInputModeGameOnly@@6B@ = 0x07B26340]; mov byte [rcx+8],1; mov [rcx],rax; ret`, so the vtable is at +0 and `bConsumeCaptureMouseDown` at +8.
- Vtable 0x07B26340 slots, read from the exe: [0] 0x0402FDC0 (deleting dtor), [1] 0x04033690 (`ApplyInputMode`, which `SetInputMode` calls at `[rax+8]`), [2] 0x00C7BC80 (`[rax+0x10]`, the flush-on-focus bool), [3] 0x0402FAA0.
- `FUIInputConfig` (6 bytes) and the router's `TOptional` at +0x31/+0x37 come from the bytewise compare in `ApplyUIInputConfig`+0x36..+0x77 and the store at +0xD8..+0xE6. Router+0x20 → local player comes from +0xEB, and local player+0x78 → viewport client from +0xF6. The viewport capture/lock offsets 0x3BA/0x3BC are from card 008.
- `ApplyUIInputConfig` prologue: `40 55 41 56 41 57 48 8D AC 24 50 FF FF` (`push rbp` with a REX prefix, `push r14`, `push r15`, `lea rbp,[rsp-0xB0]`). MinHook relocates the first 6 bytes (the three 2-byte pushes), which contain no RIP-relative operand. `FInputModeGameOnly` ctor: `48 8D 05 …` (RIP-relative `lea`). It is only called, never patched.

### New / changed log lines
- `config: UBWCOOP_CLIENT_GAMEONLY unset -- client input mode left to the game`, `config: UBWCOOP_CLIENT_GAMEONLY=1 -- client local PC gets FInputModeGameOnly once per map load`, or `WARN config: UBWCOOP_CLIENT_GAMEONLY not "1" (status=N, err=N) -- off`.
- `input-mode: SetInputMode(pc=…, net mode N, GameOnly|GameAndUI|UIOnly|other, vtable exe+0x…) caller exe+0x…, bShowMouseCursor N`. Net mode: 0 standalone, 2 listen server, 3 client.
- `input-mode: SetShowMouseCursor(pc=…, net mode N, 0|1) bShowMouseCursor A -> B, caller exe+0x…`
- `input-mode: ApplyUIInputConfig(router=…, local player …, force 0|1) caller exe+0x…`, followed by `input-mode:   config ignoreMove … ignoreLook … mode … capture … lock … hideCursor …; previous (set S) a b c d e f; viewport capture X -> Y, lock X -> Y`. The value 255 means there is no viewport client. Capped at 16 events, then `diag: ApplyUIInputConfig ... further events not logged`.
- `diag: client map loaded; …` or `diag: host/standalone map loaded; input snapshot in 10000 ms`, followed by the card 008 `snapshot:` lines, now for the host as well.
- With the env var set: `client-input: UBWCOOP_CLIENT_GAMEONLY: SetInputMode(pc=…, GameOnly); viewport … capture A -> B, lock N, bShowMouseCursor N`. The expected value is B = 1, lock 1.

### Suggested test (senior)
1. Run once with the env var unset on both instances, and compare host vs client:
   - the `ApplyUIInputConfig` / `SetInputMode` lines after the gameplay `LoadMap`;
   - the host snapshot's capture value.
   
   A host-only `SetInputMode` or `ApplyUIInputConfig` with its caller RVA identifies the setter, and a follow-up card can mirror it exactly.
2. Then run with `UBWCOOP_CLIENT_GAMEONLY=1` on the client only, and check the cursor is hidden and the mouse turns the camera without a button held.

### Risks / open
- If a CommonUI widget becomes active later on the client, for example a menu that is then closed, the router can re-apply its own config. Whether the client returns to capture 3 after that depends on that widget's config. The `ApplyUIInputConfig` log will show it. The fallback fires only once per map load by design.
- `SetShowMouseCursor` now also logs for the host's local PC. If the host's BP calls it often, its 16 log lines may be spent before the gameplay map. The card 008 counters are process-wide, as before.
- Net mode in the log is the raw `ENetMode` byte.

## Senior review (2026-11-02)
Accepted as diagnostics. The fallback works mechanically, but the user-visible problem is **not fixed**.

Checks:
- Disassembly of the `ApplyUIInputConfig` prologue, the `FInputModeGameOnly` ctor (vtable 0x07B26340, +8 = 1) and the dtor matches the report.
- The rebuild is clean: 84 exports, imports only from `KERNEL32.dll`. It is deployed.

Run (`logs/card009-*`), with `UBWCOOP_CLIENT_GAMEONLY=1` on the client only:
- **The host's setter is Blueprint.** About 4 s after the mission `LoadMap`, the host's local PC (net mode 2) gets `SetInputMode GameOnly` from exe+0x364BFF0, which is inside `UWidgetBlueprintLibrary::execSetInputMode_GameOnly` (0x0364BF20). The host snapshot shows capture 1, lock 1. The client never receives this call.
- The menu calls come from `execSetInputMode_GameAndUIEx` (+0x364BEE6) and `execSetInputMode_UIOnlyEx` (+0x364C1EA). The client's front-end snapshot shows capture 3, so capture 3 is carried over from the menu.
- `ApplyUIInputConfig` was never called on either instance, so CommonUI is not the mechanism.
- The fallback fired after `AcknowledgePossession` and changed the client's capture from 3 to 1 (lock 1, `bShowMouseCursor 0`). The 10 s snapshot still shows capture 1.
- Both instances exited 0, with no new crash dump.

**User report: the client still cannot use the mouse, and WASD on the client lags.** So capture mode was not the only cause. Card 010 follows up.

`ASTPlayerController::HandleInputLook` (0x051CA640) has no early return: it stores the look value at +0x788/+0x790, then always calls PC vtbl +0xD30/+0xD28 (AddYaw/AddPitchInput). If the mouse is dead, either IA_Look never fires on the client, or the rotation is applied and then overwritten or ignored.
