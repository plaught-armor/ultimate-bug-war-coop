# Card 015: Get the host out of the FED_C1M1 dropship unattended; make the client collision guard dropship-aware; take input rows on foot

Read `docs/STATE.md` first: the "Mod code rules" section, the "Runtime facts" line about `tools/run_pair.sh`, and rows 013 to 015. Then read card 014's report and its senior review at the end. Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`. Run logs: `logs/card014{b,c,d}-{host,client}.log`. Symbols: `tools/symbols.txt`.

## Why

Card 014 runs show this sequence:

- The host (listen server, `FED_C1M1`) starts the mission inside `BP_PlayerTransportDropship_VerySlow_C`. `EnterDropship` runs on the host with LocalRole 3, and the host PC's Pawn is the dropship. The host snapshot shows bShowMouseCursor 1 and MouseCaptureMode 0.
- On the client, `EnterDropship` also runs for the replicated copy of that dropship (LocalRole 1). It disables collision on the client's **own** pawn: the caller exe+0x51C577E is inside `ASTPlayerTransportDropship::EnterDropship` 0x051C56C0. That is the writer card 013 was looking for.
- The card 014 guard (`own_pawn_collision_step`) re-enables collision a few ticks later. Even so, on the client: PerformMovement 0, StartNewPhysics 0, ReplicateMoveToServer 0, MovementMode 5. IA_Move and IA_Look are actuated but never Triggered; IA_Sprint is Triggered with binding Execute 0.
- `DescendToLandCompleted` never ran on either side:
  - 240 s with the host idle;
  - about 165 s after the host's own scripted W, mouse, Space, Shift+W, D and LMB, sent through `APlayerController::InputKey`.

So every input row so far was taken inside the dropship, where blocked move and look may be normal game behavior. The open question from cards 011 to 014 (does client input work on foot?) cannot be answered until the host's dropship lands and both players get out. That must happen without a human, because test runs are unattended.

Dropship members in the exe (from the publics): `BeginPlay`, `Tick`, `EnterDropship`, `DescendToLandCompleted`, `SetManualControl(bool)`, `HandleInputLookHorizontal/Vertical(float)`, `CanInteract`, `OnInteraction(UADInteractorComponent*)`, `SetIsInteractable(bool)`, `IsCarryingPlayer`, `EndCurrentLevel`, `ActorFullyLoaded(USTSaveComponent*)`.

The STATE known issue "the intro-movie click doesn't capture the mouse" may be the same gate: a widget or movie waiting for a click that `InputKey` never reaches.

## Do

1. **Find the gate, statically first.** Work out what makes the host's dropship descend and land, and what the player must do to get out. Read `ASTPlayerTransportDropship::Tick`, `BeginPlay`, `SetManualControl`, `OnInteraction` and `CanInteract`, the callers of `DescendToLandCompleted`, and whatever starts the descent (level sequence, conversation stage, widget callback, timer). Report each step with its RVA and the condition it waits on.
2. **Confirm at runtime with capped, one-shot logs.** Log the descent state on the host once per second for at most 60 lines per map: the dropship fields your step 1 identifies, plus whether a widget or movie has focus. Run one pair with `tools/run_pair.sh`, host idle, and say which condition never comes true.
3. **Unattended advance (test-only, behind an env flag).** Add `UBWCOOP_AUTODROPSHIP=1`, read in `config.cpp` like the other flags. On the host only, once per map, it does the smallest thing that satisfies the gate, using the game's own functions. For example: send the input the gate actually listens for through the path it listens on, or call the function the widget would call. Do not write fields directly unless no function exists, and say why if you do. Log each action once.
4. **Dropship-aware client guard.** Find out how the client's own pawn is meant to behave when the host's dropship runs `EnterDropship` and `DescendToLandCompleted` on the client. Use the `+0x749` player-inside flag and the early returns in `DescendToLandCompleted` (+0xC8 calls `SetActorEnableCollision(true)`; it returns earlier when `+0x749 == 0` or there is no ST PC). Then change `own_pawn_collision_step` so it does not undo a dropship disable while the pawn is legitimately inside one. Collision must still come back once the dropship has landed, by the game's own path where possible and by the guard otherwise. State which path ran in the log.
5. **Take input rows on foot.** Run a pair with `HOST_EXTRA_ENV=UBWCOOP_AUTODROPSHIP=1` and `CLIENT_EXTRA_ENV="UBWCOOP_AUTOINPUT=1 UBWCOOP_AUTOINPUT_DELAY_MS=<after landing>"`. The client script must start after the client pawn is on foot. Fill card 014's decision table from those rows: chain counts while W is held, corrections, and IA_Look / IA_Jump / IA_Sprint Triggered against binding Execute.

## Done when

- The report names the descent gate with RVAs and the runtime evidence.
- With `UBWCOOP_AUTODROPSHIP=1`, an unattended pair run reaches `DescendToLandCompleted` on the host, and both pawns are on foot. Without the flag, behavior is unchanged.
- The client guard does not fire while the pawn is inside a landed-pending dropship, and collision is on once on foot.
- The client rows from step 5 are in the report, with the decision-table verdict.
- Clean build (`/W4 /WX`), 84 exports, KERNEL32-only imports. No new crash dumps in the runs.

## Runs

- Build: `python3 -I tools/gen_proxy_exports.py && python3 -I tools/pdb_rva.py gen && cmake --build build --clean-first`.
- Run: `DEPLOY=1 tools/run_pair.sh <tag> <cap_s>` with `HOST_EXTRA_ENV` / `CLIENT_EXTRA_ENV` / `DONE_PATTERN`. Never pass `-nosound`; it crashes the host. Audio goes to a null sink by default.
- `UBWCOOP_AUTOINPUT_DELAY_MS` sets the script start delay after the acknowledge (default 5000, max 600000).

## Risks

- `UBWCOOP_AUTODROPSHIP` must not change anything when unset.
- Skipping the intro may skip state the mission needs later (conversation stages, objectives). Prefer driving the same path a player would.
- If landing turns out to be a long fixed-time descent rather than a gate, say so with the timing and make step 3 a no-op.

## Report (2026-11-02)

### Result
- **Gate:** the world is paused, and the dropship never ticks. `WB_Stack_C` calls `RequestGamePause` with flag 1 for the mission-brief movie (about 2 min) and then the "BASIC TRAINING … PRESS ANY KEY TO CONTINUE" screen. Until that screen closes, `UWorld::IsPaused` is 1. Nothing ticks the PTD, and the spline distance stays at 88335.
- **Landing:** this is not a long fixed-time descent. Once the world is unpaused, the spline flight takes about 24 s, `ExecuteBeginDescentBehaviour` follows, the descent takes about 4 s, and then `DescendToLandCompleted` runs. That is about 28 s from unpause to on foot.
- **Unattended landing:** with `UBWCOOP_AUTODROPSHIP=1`, the host takes one action. It calls `USTGameInstance::RequestGameUnpause` with the requester that holds the pause (`WB_Stack_C`), as that widget's close would. `DescendToLandCompleted` then runs on the game's own path, and both pawns end up on foot with collision 1.
- **Client:** the client pawn was held because the client's *replica* dropship ran `EnterDropship` locally. That replica has LocalRole 1, nothing ever releases it on the client, and `DescendToLandCompleted` only runs on the authority with a PC. The detour now skips `EnterDropship` when LocalRole != 3. This removes card 014b's collision-0 writer at its source (caller exe+0x51C577E). The guard never had to fire.
- **Movement:** fixed. While W is held, the chain rows show `SafeMoveUpdatedComponent` > 0. Corrections occur only at input transitions, at about 7 uu.
- **Look:** still dead. `IA_Look` gets a nonzero value after modifiers, but its trigger state stays None (Triggered 0, binding Execute 0), so ControlRotation never changes. **Jump:** Triggered 0 and binding Execute 2, with no falling. **Sprint:** Triggered and Execute both fire, but MaxWalkSpeed stays at 700. Details below.

### Files changed
- `mod/src/hooks.cpp`:
  - The auto-dropship section has a comment block on the gate and on why Slate Enter failed.
  - `AutoDropship {wait_end_ms, next_ms, actions}`, reset per map in `client_diag_load_map_return`.
  - `auto_dropship_requester`, `auto_dropship_unpause`, `auto_dropship_ready` and `auto_dropship_step`.
  - A capped `dropship-watch` 1 Hz log (`kDropshipWatchLines` 60 per map: 30 one second apart, then 10 s apart).
  - `log_dropship` has its own cap (`kDropshipEnterExitLogs` 8) and logs the possessed pawn and its `bActorEnableCollision`.
  - `detour_enter_dropship` skips the original on non-authority dropships.
  - `detour_descend_completed` reads the PC (+0x2D8) before calling the original.
  - The card015h Slate press code and the Slate globals are removed.
- `mod/src/config.cpp` / `config.h` / `hooks.h`: `UBWCOOP_AUTODROPSHIP`, which is true only for the exact value "1". When unset, the log says "dropship left to the game" and nothing is armed.
- `tools/symbols.txt`:
  - Added the card 015 block: hooks on PTD Tick and on PD StartAction, SetState, SkipFlight, ExecuteBeginDescentBehaviour, TickBehaviours and TickSplineTravel; calls to PTD StaticClass, `AActor_IsActorTickEnabled`, `UWorld_IsPaused` and `UWorld_GetNumPlayerControllers`; hooks on `USTGameInstance_RequestGamePause` and `USTGameInstance_RequestGameUnpause`. SkipFlight is hook-only: it is logged and never called.
  - Removed `FSlateApplication_Get`, `OnKeyDown` and `OnKeyUp`.

**Build:** `python3 -I tools/gen_proxy_exports.py && python3 -I tools/pdb_rva.py gen && cmake --build build --clean-first` returned rc 0 with 0 warnings. The gen step reports `84 exports (37 named)`.
- `llvm-readobj --coff-exports build/dwmapi.dll`: 84 ordinals.
- `--coff-imports`: only `KERNEL32.dll`.
- The sources are unchanged since the card015k deploy (f30d7a4a40b3a8a3). The clean rebuild hashes 870ed98747c7d7f3. The only difference is the link timestamp.

### RVAs (static)
| Item | RVA / offset | Notes |
|---|---|---|
| ASTPlayerTransportDropship::EnterDropship | 0x051C56C0 | sets +0x749=1, possesses the dropship, attaches, sets CMC+0x1A0=0, turns collision off |
| ASTPlayerTransportDropship::DescendToLandCompleted | 0x051C2D10 | detaches, possesses the pawn, turns collision on, sets +0x749=0. Returns early when +0x749==0 or there is no ST PC |
| PTD::Tick | 0x051E1650 | calls TickBehaviours 0x051E2B30, which runs TickSplineTravel 0x051E75A0, then ExecuteBeginDescentBehaviour 0x051C5D90, then TickMoveDescent 0x051E5660, then DescendToLandCompleted |
| PTD vtable slots | 0x8A8 / 0x8C8 / 0x8D0 / 0x8D8 | IsPerformingActions / TickBehaviours / DescendToLandCompleted / TickDescent |
| AActor::IsActorTickEnabled | 0x037EA390 | 1 throughout. Ticking is not the gate |
| UWorld::IsPaused | 0x03DEDBF0 | 1 until the unpause. **This is the gate** |
| USTGameInstance::UpdatePause | 0x0510CDE0 | calls SetGamePaused 0x03C6BBD0 while GI+0x428 != 0 |
| USTGameInstance::RequestGamePause_Implementation | 0x050FEDA0 | `this` = GI+0x1C0 (interface subobject). Requester arrays at iface+0x260 (all) and +0x270 (flagged) |
| USTGameInstance::RequestGameUnpause_Implementation | 0x050FEEC0 | the action used |
| execRequestGamePause / execRequestGameUnpause | 0x04560900 / 0x045609D0 | BP thunks. Caller of the pause is exe+0x45609BD, the WB_Stack_C graph |
| UWorld+0x228 | OwningGameInstance | from AActor::GetGameInstance 0x037E59F0+0xE |
| UWorld::GetNumPlayerControllers | `?GetNumPlayerControllers@UWorld@@QEBAHXZ` | used to wait for the client PC |
| USTUserWidget::IsValidAsAnyKey | 0x05209630 | called only from its BP thunk (execIsValidAsAnyKey+0x8B). The any-key screen is BP OnKeyDown and needs Slate keyboard focus |
| FSlateApplication::Get / OnKeyDown / OnKeyUp | 0x059ED3F0 / 0x017FBA40 / 0x017FBC80 | `this` = slate+0x188. Tried in card015h, then removed |

### Runtime evidence per run (all `tools/run_pair.sh`, rc 0/0, FATAL 0 unless noted)
| Run | Build / settings | Finding |
|---|---|---|
| card015a–f | watch logs, no action | `dropship-watch` showed state 0, behaviour 0, distance 88335, Tick 0 / TickBehaviours 0, tick enabled 1, world paused 1. The pause requester was WB_Stack_C (flag 1), first for the brief movie and then for the press-any-key screen |
| card015g | Slate OnKeyDown with the wrong `this` | Host crash, dump 5DFCEA9D: AV reading 0x0 at OnKeyDown+0x40. The code passed the FSlateApplication base and not slate+0x188. Fixed in 015h, and the code is now removed. That build was never kept |
| card015h | Slate Enter at slate+0x188, 60 presses 4 s apart | Each press was handled (1/1), but the world stayed paused, ending with "WARN autodropship: world still paused after 60 presses -- disarmed". The movie and the any-key widget never got keyboard focus, so synthesized Slate keys do not reach the BP OnKeyDown. No dumps |
| card015i | one direct RequestGameUnpause | One action unpaused the world. Flight took about 24 s, descent about 4 s, and the host was on foot. The client joined after landing, but its replica PTD (LocalRole 1) ran EnterDropship locally, so the client pawn was held with collision 0 indefinitely. The server-side client pawn was on foot, with corrections of about 3488 uu |
| card015j | wait for 2 PCs before the unpause | The host waited about 33 s for the client PC, unpaused, and DescendToLandCompleted ran on the game path about 28 s later with collision 1. The client was still held by the replica, so the run was stopped early |
| **card015k** | final build, deploy f30d7a4a40b3a8a3. `HOST_EXTRA_ENV=UBWCOOP_AUTODROPSHIP=1`, `CLIENT_EXTRA_ENV="UBWCOOP_AUTOINPUT=1 UBWCOOP_AUTOINPUT_DELAY_MS=40000"` | Ended by the done pattern after 119 s. Host log 484 lines, client log 608. FATAL/ERROR 0, WARN 0. No new crash dumps |

card015k host sequence (from `logs/card015k-host.log`):
```
collision: host/standalone EnterDropship(...VerySlow_C, LocalRole 3) player-inside flag 0 -> 1; controller ... now possesses ...VerySlow_C
autodropship: world paused with dropship pawn ...; waiting for 2 PlayerControllers (now 1, at most 120000 ms)
autodropship: 2 PlayerController(s); unpause action in 3000 ms                      (+33.7 s)
autodropship: action 1/8: USTGameInstance::RequestGameUnpause(... WB_Stack_C) as the closing screen would; ... requesters now 0, world paused after: 0
autodropship: world unpaused after 1 action(s); dropship ... ticks from here -- done
dropship-watch[31] t=40s distance 71638 ... Tick 391 ... world paused 0
dropship-watch[33] t=60s distance 1421 ... descent Z 1305
collision: host/standalone DescendToLandCompleted(...VerySlow_C, LocalRole 3) player-inside flag 1 -> 0; controller ... now possesses ...BP_PlayerCharacterMI_C, bActorEnableCollision 1   (+28.1 s after unpause)
dropship-watch: t=70s PC pawn is now ...BP_PlayerCharacterMI_C; watch ends
```
The client log records `collision: client EnterDropship(...VerySlow_C, LocalRole 1) skipped: not the authority's dropship; the server keeps this player's pawn on foot`. The own pawn was new with collision 1. Every row shows `re-enables since last row 0 (map total 0)` and no FIX line, so the guard never ran.

**Collision paths.** The host's collision came back by the game path (`DescendToLandCompleted`). The client's pawn is never put in a dropship, so it has collision 1 from spawn and needs neither path. The guard's existing dropship-hold check (`holding_dropship`/`note_dropship_hold`) stays for any authority-consistent hold.

**Known side effect.** Only the pause is released. The host screen still draws the brief movie/any-key widget over the game, and the client shows the on-foot HUD. The mission state after the widget closes, such as conversation stages, was not checked. The flag is test-only.

**Flag unset.** Nothing is armed and `EnterDropship` behaves as before on the host. The only behavior change without the flag is the client-side skip on non-authority dropships, which is intended (task 4).

### Client rows, card015k (autoinput starts 40 s after acknowledge, landing was about 31 s)
| Rows | Step | Chain: SafeMoveUpdatedComponent | rot ClientAdjustPosition | srv corrections (\|e\|) |
|---|---|---|---|---|
| 0–3 | W hold 4 s (MouseX starts at row 3) | 121, 120, 119, 120 (false 0, blocking 0) | 0, 0, 0, 0 | 0, 0, 0, 0 |
| 4 | W released, MouseX 5.0 | 43 | 4 | 15 (7.2) |
| 5–8 | MouseY 3.0, SpaceBar | 0 (no move input) | 0 | 0 |
| 9–12 | LeftShift and W 3 s, then D | 61, 119, 118, 105 | 0, 0, 0, 4 | 0, 0, 0, 14 (6.8) |
| 13–15 | D, then release | 121, 121, 41 (blocking 2, 1, 0) | 0, 0, 4 | 0, 1 (3.6), 13 (6.7) |

Speed while W is held is 700 (= MaxWalkSpeed). The pawn moved from Y -1062 to 1039 over rows 0–3 and from 1122 to 2827 over rows 8–11. Corrections appear only where input starts or stops, and they are one-frame sized (≤ 7.2 uu).

| Action | Rows | Raw nonzero | After modifiers | Mapping triggers | Binding Execute | Effect |
|---|---|---|---|---|---|---|
| IA_Look | 3–8 | 120/row (max 5.0 / 5.83) | 120/row (max 0.35 / 0.41) | None 120, Ongoing 0, **Triggered 0** | **0** | ControlRotation stays at P 0 Y 90 on every row. HandleInputLook 0 |
| IA_Jump | 8 | 12 | 12 (1.0) | None 13, **Triggered 0** | 2 | no PhysFalling, SafeMove 0, Z stays 103.5 |
| IA_Sprint | 10–12 | 118 / 58 | 118 / 58 (1.0) | Triggered 118 / 58 | 118 / 59 | MaxWalkSpeed stays 700.0 |

### Decision-table verdict (card 014)
- **"Fix works"** row: **matches**. Chain rows 0–3 and 9–12 show `SafeMoveUpdatedComponent` > 0. rot ClientAdjustPosition is 0 except at release rows (4 per transition), and srv corrections are 0 except at transitions. **Movement is fixed.**
- **IA_Look** matches none of the table's rows. The value gets through the modifiers (0.35 after scaling 5.0 by 0.07), but the trigger state stays None, so it is never Triggered and the binding never runs. The mapping has `triggers 0`, so any trigger comes from the action asset or from the implicit actuation test.
  - Unverified guess: the actuation threshold (default 0.5) is above the scaled value. A real mouse delta of 5 px scales to 0.35. A larger scripted MouseX (for example 20.0, which gives 1.4) would test this without any code change.
  - Next step for look: either rerun with a larger axis amount, or statically read IA_Look's action-level triggers and the trigger evaluation in `UEnhancedPlayerInput::ProcessActionMappingEvent`.
- **IA_Jump:** Triggered 0 with Execute 2. The two Executes are probably Started/Completed-type bindings, but that is not verified. No jump happened. This needs the same trigger read as IA_Look, because a 100 ms hold may not satisfy a Pressed or Tap trigger evaluated per frame.
- **IA_Sprint:** fires and its bindings run, but speed does not change. Either the BP sprint handler sets a different speed field, or it is gated by stamina or state. This was not investigated (out of scope).

### Crash dumps
The existing dumps are 10FF0037 (18:08), 39E8E08F (14:33), 4BAAF5BF (14:46), 5DBA4C39 (15:17), 5DFCEA9D (19:03, card015g, the wrong-`this` build explained above), D891BDFC (14:21) and EFDA5B83 (15:30). card015h through card015k (final build) added none.

## Senior review (2026-11-02)

**Verdict: accepted.** The gate, the unattended landing, the client `EnterDropship` skip and the movement fix are reproduced. Look, jump and sprint stay open and move to card 016.

### Independent run
`card015s`: clean rebuild, 84 exports, KERNEL32-only imports, deploy cf323a8de5800582. `HOST_EXTRA_ENV=UBWCOOP_AUTODROPSHIP=1`, `CLIENT_EXTRA_ENV="UBWCOOP_AUTOINPUT=1 UBWCOOP_AUTOINPUT_DELAY_MS=40000"`, cap 240 s.
- Ended by the done pattern after 115 s. rc 0/0, FATAL/ERROR 0, WARN 0, no new crash dumps.
- Host: one autodropship unpause action, then `DescendToLandCompleted` on the game path with collision 1.
- Client: `EnterDropship ... skipped: not the authority's dropship`, no FIX line. PhysWalking and `SafeMoveUpdatedComponent` about 120 per row while W is held, ClientAdjustPosition 0 on those rows, location advances.
- IA_Look: raw max 5.0, after modifiers 0.35/0.41, trigger state None on every evaluation, Triggered 0, binding Execute 0, `HandleInputLook` 0, ControlRotation fixed at Y 90. IA_Jump: Triggered 0, Execute 2. IA_Sprint: Triggered and Execute 62 to 120 per row. All match the report.

### Code
- `detour_enter_dropship` skips the original only when LocalRole != Authority, and the skip log is capped. The host path is unchanged.
- The autodropship state resets per map, is armed only on the host (`!g_client_session`), is bounded to 8 actions 4 s apart and a 120 s wait for 2 PCs, and calls the unpause only when the pause requester is `WB_Stack_C`. With the flag unset, nothing is armed.
- The descent watch and the pause hooks are log-only and capped.
- No function added in this card exceeds 60 lines (`value_len` is 3 lines, `resolve_all` 16). No new allocations or handles.
- The Slate key path from card015g/h is fully removed, and the wrong-`this` crash build was not kept.

### Not covered
- The brief/any-key widget stays drawn on the host after the unpause. Mission state that the widget's close would set (conversation stages, objectives) was not checked. Added to the STATE known issues; `UBWCOOP_AUTODROPSHIP` stays test-only.
- All look rows used scripted deltas of 5.0 (0.35 after the 0.07 scale). A real mouse on foot has not been tried.
