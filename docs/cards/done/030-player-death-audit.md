# Card 030: Player death audit (log only), groundwork for downed and revive

Status: sent 2026-11-03.

Read `docs/STATE.md` first: the "Mod code rules" section, the "Runtime facts" line about `tools/run_pair.sh`, milestone 3a ("Player death flow"), and rows 021, 024 and 025. Then read `docs/design/coop-channel-lobby-downed.md` section 3. Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`. This card only logs; the game's own death runs unchanged.

## Why

The downed state will detour `ASTPlayerCharacter::HandleDeathEvent` on the host. Before that is safe, four things must be known from a real death: whether the event fires once or on every hit at 0, what reacts to Health reaching 0 before it, what `OnDeath`'s blueprint does, and what the other machine sees.

## Static leads (senior, 2026-11-03)

Treat these as leads to confirm, not facts. Function names in the disassembly can be wrong where the linker folded identical functions.

- **Who calls what.** `ASTPlayerCharacter::StartDeath` (0x051DE410) has no direct callers except the `ASTPlayerCharacterMI` (0x051DEE70) and `ASTPlayerCharacterBug` overrides. Each of those calls the base first, then cancels the equipped weapon's ASC abilities. `HandleDeathEvent` and `CompleteDeath` have no direct calls at all. All three are reached only through the vtable or a delegate, so detour the vtable slots or the function bodies, not call sites.
- **What `StartDeath` does, in order.** It has no authority or net-mode check. It casts the Controller to `ASTPlayerController`, then:
  - makes a virtual call at +0x340;
  - calls `USTDebugSaveTracker::SetAutoSaveEnabled` (0x0521B160) and `USTSaveSystemLibrary::CancelAsyncSaveProcess` (0x051F5D80) on the game instance;
  - makes a virtual call at +0x8E8;
  - picks a random death animation from `USTCharacterData` and makes a virtual call at +0x4E8;
  - calls `UADAnimatorBaseComponent::BlockNewAnimations`;
  - calls `SetActorLocation` and `SetCapsuleSize`, which shrinks the capsule;
  - calls `SetRelativeLocationAndRotation` (probably the camera);
  - calls `UAbilitySystemComponent::CancelAllAbilities`;
  - makes a virtual call at +0x4F8 and calls `TryPlayAudioAtLocation`.
  It never spawns an actor and never restarts the level.
- **Save risk.** If a client's server copy runs `StartDeath` on the host, the host's autosave would be switched off and its pending save cancelled. Log every `SetAutoSaveEnabled` call with its argument and the pawn that caused it.
- **Restart and dead pawn are blueprint-driven.** `USTGameInstance::RestartCurrentLevel` (0x050FFC30) has one caller, its blueprint thunk `execRestartCurrentLevel`. `DeadPlayerPawn` is a class property at +0x8A8. In player-character code (0x051B0000 to 0x051F0000), the only native access to it is one write, which looks like the constructor. So expect the `ProcessEvent` log in step 2 to show a blueprint spawning `DeadPlayerPawn` and calling `RestartCurrentLevel`. Name the blueprint function that does each.

- **What `OnDeath` leads to (package name maps, 2026-11-03).** Expect a game-over menu, not a direct reload.
  - `BP_HUD_Manager` has an `OnDeath` handler and a `DeathHUDWidget` class variable. `BP_HUD_Manager_MI` references `WB_PlayerDeathScreen` (the bug side uses `WB_PlayerDeathScreen_Bug`). The HUD manager belongs to the local PC, so the menu appears only on the machine whose own pawn broadcast `OnDeath`.
  - `WB_PlayerDeathScreen` has four buttons: `RestartMission`, `LoadLastSave`, `LoadGame` and `Exit`. It calls `GetRandomLastDamagingActorQuote`, `IsLatestSaveValid` and `LoadLatestSave` on the save engine, and `GetSTGameInstance` (most likely for `RestartCurrentLevel`). The game waits for the player to pick one.
  - `BP_PlayerCharacterMI` is the only package besides the class itself that references `BP_DeadPlayerPawn`, so its `OnDeath` handler most likely spawns it. `BP_DeadPlayerPawn` holds only a camera.
  - Co-op risk: if the client's own pawn broadcasts `OnDeath`, the client gets this menu, and `LoadLastSave` or `RestartMission` would load a level on the client alone. Log whether the menu appears on the client in the `client` run.
  - Zen packages store native imports as hashes, so a call with no return value (such as `RestartCurrentLevel`) does not appear in the name map. Confirm the button targets in the `ProcessEvent` log.

- **Interact (static, 2026-11-03).** `ADIC_Default` has no `InputTag.Interact`; the likely tag for `IA_Interact` (triggers Pressed, Hold, Released) is `InputTag.Use`. The work is done by `UADInteractorComponent::Interact` / `ReleaseInteraction` / `TraceForInteractable`, all native, and no interaction function is a Server RPC. So unless `InputTag.Use` activates a GAS ability (which would reach the server through `InternalServerTryActivateAbility`, as card 024 found for fire), the client's interact runs on the client only. Step 4 settles which. If it is client-only, revive needs its own channel opcode.

- **Tick has no role gate on death (disassembly of `ASTPlayerCharacter::Tick` 0x051E06F0, 2026-11-03).**
  - **Kill floor.** After the check at 0x145242110 and the +0x8F8 flag, Tick calls `StartDeath` (vtable +0xA40) with no role check.
  - **Countdown.** At 0x1451E0F90, Tick does `+0x9DC -= dt`, clamped at 0, and then calls `CompleteDeath` (vtable +0xA48). This also has no role gate.
  - **Consequence.** Whichever machine runs `StartDeath` also runs the countdown and `OnDeath`, and therefore gets the death menu. That includes a client whose own pawn falls below the kill floor.
  - **Open question.** Whether `TimeUntilDeathCompletion` (+0x9DC) replicates is unknown. The PDB public for its `NewProp` params resolves to an unrelated struct, so the flag could not be read statically. The publics list no OnRep for death.

## Do

1. **Test-only kill switch.** Add `UBWCOOP_KILL_PLAYER=<host|client>`, off by default. About 20 s after both pawns are on foot, the host damages the chosen pawn through the game's own pipeline: `USTCombatUtilityLibrary::DealDamage(AActor* target, AActor* causer, float amount, bool)` (0x05233F60, static). It builds an `FSTDamageTemplate` and calls `ApplyDamageToActor`. Use the target as the causer. Deal 25% of MaxHealth every 0.5 s until Health is 0, then deal two more hits at 0. Do not write the attribute directly. The `bool` is "ignore damage modifiers": when it is true (checked at 0x0523408B), the function adds `UADHealthUtilityLibrary::GetIgnoreDamageModifiersTag` to the damage tags. Pass `false` so armour and difficulty apply as in real play, and log the Health lost per hit.
2. **Log, on both sides, with timestamps:**
   - Every call to `HandleDeathEvent` (0x051CA3B0), with the pawn, its role, its Health and +0x9DC (`TimeUntilDeathCompletion`) on entry. Does it fire once, or on every hit at 0?
   - Each Health change on that pawn, old to new, and the caller, as card 019's attribute logger does.
   - `StartDeath` (vtable +0xA40) and `CompleteDeath` (vtable +0xA48, 0x051C27A0) entries, and the `OnDeath` broadcast (+0x898): how many functions are bound to it, and their names.
   - Every UFunction the host and the dying pawn's PC run through `ProcessEvent` from Health 0 until 10 s after `CompleteDeath`, capped at 300 lines. This answers what the `OnDeath` blueprint does (reload, game-over screen, or travel) and whether anything acts before `HandleDeathEvent`.
   - Whether the client's own pawn and the server copy both enter `StartDeath` when the client is killed.
   - Once per map, from the reflected `FProperty` (`PropertyFlags`, `RepNotifyFunc`), whether `TimeUntilDeathCompletion` and every other `ASTPlayerCharacter` property whose name contains `Death` or `Dead` has `CPF_Net` (0x20) set.
3. **Screens.** Take a `shot` on both machines at 0.5 s, 3.5 s and 10 s after Health reaches 0.
4. **Interact input.** Separately from the kill, press the interact key on both machines next to anything interactive (a door, a pickup or a terminal). Log the input tag that the existing `AbilityInputTagPressed` logger records, and whether the host's server copy receives it for the client.
5. **Runs.** Run once with `host` and once with `client`. Card 024's fire relay and weapon grants are not needed.

## Answer, with log lines

- Is `HandleDeathEvent` edge-triggered or called on every hit at 0?
- What runs between Health 0 and `HandleDeathEvent`? Is there a HUD death screen, or an `OnHealthValuesUpdated` handler?
- What does `OnDeath` lead to: a reload, a game-over screen or travel? What happens to the client when the host dies, and to the host when the client dies?
- Which input tag is interact, and does the server copy receive it?

## Done when

- Both runs are logged, and all four answers are given with log lines and screenshots.
- The switch is off by default and nothing changes when it is unset. Clean build (`/W4 /WX`, `--clean-first`), 84 exports, KERNEL32-only imports. No new crash dumps except any the death flow itself causes, which must be reported. Every new function is at most 60 lines.

## Risks

- The checkpoint reload may break the pair session. That is a finding, not a failure: log what each side does and stop the run.
- Self-damage may be filtered: the combat library has `DamageCauserIsPlayer` and faction checks, so a causer equal to the target can be ignored. If no damage lands, use the nearest enemy as the causer, log which one, and say whether that changed the result.
- `DealDamage` may respect god mode or damage immunity tags while a mission's intro plays. If no damage lands, log the tags on the target's ASC and try again later in the mission.

## Senior addendum (2026-11-03, after card 023)

- **The mod's own client health notify.** Card 021 step 5 makes the client call the game's `UADHealthComponent::HealthAttributeChanged` (0x04A3CD20) on its own pawn whenever the replicated Health changes. When the client is killed, that call runs on the client at Health 0. Tag every client-side `HealthAttributeChanged` call in the log as `mod notify`. For each `HandleDeathEvent`, `StartDeath` or `OnDeath` on the client, say whether it came right after a mod notify (same tick) or from the game itself. A client death caused by our notify is a mod effect, not game behavior.
- **Screenshots collide.** Both instances write to one Screenshots folder, so simultaneous host and client shots overwrite each other (card 023 kept 7 of 10). Offset the client's shots by 1 s from the host's (0.5/1.5, 3.5/4.5, 10/11 s), or give each side its own file name, and say which you did.

## Report

Runs: `card030a` (`UBWCOOP_KILL_PLAYER=host`), `card030b` (`client`). Logs: `logs/card030{a,b}-{host,client}.log`. Shots: `logs/card030-shots/ScreenShot00191-196.png`. No new crash dumps. Clean build, 84 exports, KERNEL32-only imports.

### Deviations from the card

- **The driver did not kill the host.** In card030a, DealDamage self-hits took nothing. The host died to natural bug fire before the driver landed a hit (`Health 100 -> 92 ... 16 -> -17`, caller `exe+0x229F17D [game]`). The death sequence is the game's own, so the answers hold.
- **DealDamage is not linear.** A self hit (target as causer, `ignore_modifiers` false) does 0 damage for about the first 22 s on foot, then ramps. At 400 per hit the losses were 0, 4, 16, 40, 40. `ignore_modifiers` true made no clear difference. An AI trooper as causer did nothing either (card030a). `c30_nearest_enemy` now filters by class name `Bug`, and `kC30HitFraction` is 4.0 (400 points) with up to 40 hits. The ASC tag dump for a no-damage hit was not implemented.
- **`AActor::ProcessEvent` was not hooked.** The "PE" lines come from the existing UObject ProcessEvent logger, filtered to the death window and capped at 300.
- **The FProperty +0x48 word is a pointer, not `RepNotifyFunc`.** Reading it as an FName crashed card030a's first attempt. It is logged raw, and `CPF_RepNotify` comes from the flags.

### Answers

**1. `HandleDeathEvent` is edge-triggered.** It runs once per death on the authority pawn. Further hits at or below 0 change Health but do not re-enter it.

```
card030b-host.log:873  host HandleDeathEvent enter BP_PlayerCharacterMI_C (server-copy, role 3/2): Health 0.0, TimeUntilDeathCompletion 0.00
card030b-host.log:878  host HandleDeathEvent return ...: TimeUntilDeathCompletion 3.00, caller exe+0x503C806
card030b-host.log:970  host hit 6: DealDamage(...): Health 0.0 -> -40.0 ..., TimeUntilDeathCompletion 2.50
card030b-host.log:1005 host hit 7: DealDamage(...): Health -40.0 -> -80.0 ..., TimeUntilDeathCompletion 1.98
```

Counts: card030a host has 1 HandleDeathEvent, 1 StartDeath and 1 CompleteDeath; card030b host has 1, 1 and 1. Health kept falling to -137 (030a) and -80 (030b) with no second entry.

**2. Between Health 0 and `HandleDeathEvent` only the health component's damage path runs, in the same tick.** `HealthAttributeChanged` fires the BP bound event, then `HandleDamageEvent`, a camera shake, force feedback and a second bound event, then `HandleDeathEvent`, all at +0 ms. There is no HUD death screen and no `OnHealthValuesUpdated` handler before it.

```
card030b-host.log:865 host death window open (HealthAttributeChanged) on BP_PlayerCharacterMI_C (server-copy)
                  866 host Health 40.0 -> 0.0 ..., caller exe+0x229F17D [game]
                  867 PE +0 ms BP_PlayerCharacterMI_C.BndEvt__BP_PlayerCharacterMI_HealthComponent_K2Node_ComponentBo
                  868 PE +0 ms BP_PlayerCharacterMI_C.HandleDamageEvent
                  869 PE +0 ms Shake_Generic_DamagedMedium_C.ReceivePlayShake
                  870 PE +0 ms BP_PlayerController_C.ClientPlayForceFeedback_Internal
                  872 PE +0 ms BP_PlayerCharacterMI_C.HandleDeathEvent
```

The sequence on the authority:

1. `HandleDeathEvent` calls `StartDeath` (MI, then base).
2. `StartDeath` calls `USTDebugSaveTracker::SetAutoSaveEnabled(0)` from `exe+0x51DE49D`, in the same millisecond.
3. `HandleDeathEvent` returns with `TimeUntilDeathCompletion` 3.00.
4. Tick counts the timer down, and `CompleteDeath` runs about 3 s later and broadcasts `OnDeath`.

**3. `OnDeath` leads to a game-over screen, `WB_PlayerDeathScreen`. There is no reload or travel within 10 s.**

- **Host dies (card030a).** `OnDeath` has 2 bound. `BP_HUD_Manager_MI_C."Show Death UI On Death"` builds and focuses `WB_PlayerDeathScreen_C`. No `RestartCurrentLevel`, travel or `DeadPlayerPawn` spawn appears in the 10 s window; those must wait for a button on the death screen.
  ```
  card030a-host.log:1107 host CompleteDeath enter BP_PlayerCharacterMI_C (host-own, role 3/1): Health -137.0
                    1108 host OnDeath on BP_PlayerCharacterMI_C: 2 bound
                    1109 host OnDeath[0]: BP_HUD_Manager_MI_C (BP_HUD_Manager_MI_C).Show Death UI On Death
                    1110 host OnDeath[1]: ABP_Arms_Skeleton_AnimBlueprint_C (ABP_Arms_Skeleton_AnimBlueprint_C).Player Dead
  PE +3001 ms BP_HUD_Manager_MI_C.Show Death UI On Death; WB_PlayerDeathScreen_C.OnInitialized / Construct / OnFocusReceived
  ```
  The client logs nothing of the host's death: no card030 death lines, only pings. Death is not replicated (see the property flags below), so the client keeps seeing a live host.
- **Client dies (card030b), host side.** The server copy runs the full sequence: HandleDeathEvent, StartDeath, `SetAutoSaveEnabled(0)` and CompleteDeath after 3002 ms. Its `OnDeath` has only 1 bound (`ABP_Arms."Player Dead"`), because the server copy has no HUD manager, so the host shows no death UI. The card 028 sprite went `Idle -> Death01`.
  **Save risk confirmed:** a client's death switches the host's autosave off (`card030b-host.log:876 SetAutoSaveEnabled(0) ... caller exe+0x51DE49D; last dead pawn BP_PlayerCharacterMI_C`). Server corrections show the server pawn's Z dropping (capsule shrink) while the client stays at Z 103-136.
- **Client dies (card030b), client side.** Every Health change on the client is the mod's notify (`caller exe+0x0 [mod notify]`, about 20 ms after the host). HUD widgets ran `OnHealthChanged`. **No HandleDeathEvent, StartDeath or CompleteDeath ran on the client** (counts 0, 0, 0), so the mod notify does not cause a client death. The window closed with `CompleteDeath not seen` after 40079 ms (104 PE lines). `ClientAdjustPosition` snapped the client down to the server Z of 48.5. Client shot 194: normal first-person camera, no death UI. Host shot 193: no client body visible.
  ```
  card030b-client.log:795  client Health 40.0 -> 0.0 on BP_PlayerCharacterMI_C (client-own), caller exe+0x0 [mod notify]
  card030b-client.log:1409 client death window closed 40079 ms after Health 0 (CompleteDeath not seen)
  ```
- **Replication of death state.** None of the death properties is replicated:
  ```
  STPlayerCharacter.OnDeath                  offset 0x898 flags 0x0020080010080200 CPF_Net 0 CPF_RepNotify 0
  STPlayerCharacter.DeadPlayerPawn           offset 0x8A8 flags 0x002C081000010215 CPF_Net 0 CPF_RepNotify 0
  STPlayerCharacter.TimeUntilDeathCompletion offset 0x9DC flags 0x0048001040000200 CPF_Net 0 CPF_RepNotify 0
  ```
  A client death therefore needs a channel message telling the client to run its own death (or downed) sequence.

**4. Interact is `InputTag.Use`, and it stays local.** On each machine, E reaches `HandleInputTag_Pressed InputTag.Use`, `AbilityInputTagPressed InputTag.Use` and `UADInteractorComponent::Interact`. On the client, the ASC's tag map has no handle for `InputTag.Use` (`this tag's handles -1`), so no ability activates, nothing goes to the server, and the host's server copy receives nothing. Revive needs its own channel opcode.

```
card030b-client.log:885 client UADInteractorComponent::Interact - on BP_PlayerCharacterMI_C (client-own), 6084 ms after Health 0
card030b-client.log:887 client ASTPlayerController::HandleInputTag_Pressed InputTag.Use ...
card030b-client.log:888 client AbilityInputTagPressed InputTag.Use on asc ..., tag map Num 8 free 0, this tag's handles -1
card030b-host.log:1039  host UADInteractorComponent::Interact - on BP_PlayerCharacterMI_C (host-own), 5055 ms after Health 0
```

The client was dead on the server at the time but alive locally, and its interact still ran locally (`target null`: there was no interactable in reach). The host log has no Interact or Use lines on the server copy.

### Senior addendum items

- **Mod notify vs game.** Every client-side `HealthAttributeChanged` line is tagged `[mod notify]` or `[game]`. Each HandleDeathEvent, StartDeath and CompleteDeath line records whether it ran inside a mod notify and how long after the last one. In card030b none of them ran on the client at all. On the host they are all `not inside mod notify`.
- **Screenshot collision.** The client's shots are offset by 1 s (`kC30ClientShotLagMs` = 1000). Host shots were at 503, 3537 and 10086 ms; client shots at 1592, 4548 and 11045 ms. All six files survived (ScreenShot00191-196: odd numbers are host shots, even numbers are client shots). No collisions.

### Caveats

- card030a's 10 s post-CompleteDeath window may be partial: that run was stopped with TaskStop.
- The ProcessEvent cap (300) was not reached in either window: 131 lines on the host, 104 on the client.

## Senior review (2026-11-03)

**Accepted.** Verified by the senior:

- `card030b-run.txt`: ended by done after 154 s, host rc 0, client rc 0, FATAL/ERROR 0, no new crash dumps. card030a was stopped with TaskStop, so it has no rc line. Its logs still cover the host death through `CompleteDeath` and the `OnDeath` binds.
- The log lines quoted in the report match the logs:
  - property flags on both sides;
  - `HandleDeathEvent enter` (030a host-own at Health −17, 030b server-copy);
  - `SetAutoSaveEnabled(0)` from `exe+0x51DE49D` in both runs;
  - `OnDeath[0]` is the HUD manager on the host-own pawn and `ABP_Arms` on the server copy;
  - zero `HandleDeathEvent enter` in `card030b-client.log`.
- Screenshots 00193 (host) and 00194 (client) were viewed. Neither shows a death UI or a client body, as reported. All six shots kept.
- No function in `hooks.cpp` or `movieskip.cpp` is over 60 lines.
- The deployed DLL (14:26) is newer than the runs. It carries the movie-skip change, which was made at the user's request and is recorded in STATE.

**Conclusions for the design:**

1. Death is not replicated: `OnDeath`, `DeadPlayerPawn` and `TimeUntilDeathCompletion` have no `CPF_Net`. When the client dies, the server runs the full sequence and the client sees nothing. The client keeps playing locally while the server holds its pawn dead. A channel message is needed for client death (or for the downed state).
2. **A client death turns off the host's autosave** (`SetAutoSaveEnabled(0)` on the shared `STDebugSaveTracker`). The host's own death does the same, but there the death screen follows. Before any co-op play test longer than one life, the downed design must keep the server copy out of `StartDeath`, or restore autosave. Recorded as a known issue.
3. Interact (`InputTag.Use`) is local and has no ability handle, so revive needs its own channel opcode, as the design document assumed.
4. The mod's own client health notify does not trigger the client's death sequence, so card 021's notify is safe in this respect.

**Open points:**

- DealDamage self-hits do 0 for about 22 s and then ramp. The cause is unknown; it may be spawn protection or difficulty scaling. Not needed now. Future test kills should use the direct Health write from card 021, or InstaKill.
- What follows the death screen (restart, checkpoint reload) has not been seen, because it waits for a button press.
