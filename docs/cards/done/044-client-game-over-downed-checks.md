# Card 044: Client game over, and the downed checks card 043 left open

Status: done 2026-11-04.

Read `docs/STATE.md` first ("Mod code rules", "Runtime facts", known issue "Game over on the client"), then card 043 (the whole card, including its Report and Senior review). The code is the card 043 block in `mod/src/hooks.cpp` (`c43_*`). Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

Card 043 works on the host, but after all down only the host gets the game-over menu. The client stays in the world with its pawn Out and no UI. Card 043 also left three things checked only by the log, not by a screen or a runtime path:
- what the remote downed sprite looks like;
- the refused Fire/Walk tags on a downed pawn;
- a relayed Fire 1 from a downed client.

## Do

1. **Client death on all down.**
   - When the client gets the all-down Down frame (the host pawn goes to Dead), run the real `StartDeath` on the client's own pawn once. Today `c43_skip_start_death` skips it once a Down frame has arrived. Let one call through for this case, and keep skipping any other.
   - Call the original through the hook trampoline (`g_orig_start_death_mi` for `BP_PlayerCharacterMI_C`). Never call the detour, because it would skip.
   - The second argument is a damage event. Do not pass null: `StartDeath` 0x051DE410 reads it (`rbx` = `rdx`) at +0x20/+0x28, +0x30/+0x38, +0x160/+0x168 and +0x170/+0x178. These look like (pointer, count) pairs. Read the code at 0x1451DE73B–0x1451DE7C2 and confirm that a count of 0 skips the reads. Then pass a zeroed, aligned buffer of at least 0x200 bytes. If a count of 0 does not skip them, stop and report what it reads.
   - Expected result: `CompleteDeath` about 3 s later broadcasts `OnDeath` and shows `WB_PlayerDeathScreen` on the client. Log the `StartDeath` call, the `CompleteDeath` call (card 030's detour or log line) and any UI input context added (`IMC_DummyUI_KBM`).
   - `StartDeath` turns autosave off on the client. That is fine: the client's save is not the co-op save. Log `IsAutoSaveEnabled` anyway.
2. **Client death-screen buttons.**
   - Do not build a co-op restart. Do log the widget's buttons and their bound functions: the class and function names of the `OnClicked` delegates, the same way card 030 logged the `OnDeath` bindings.
   - If the test driver can do it safely, have the host pick the menu's restart (or retry) entry. Then log what the client does: travel, disconnect or nothing. If the host side cannot be driven without real input, write down which function it would call and leave it.
3. **Downed sprite on screen.**
   - In the test, after the copy goes Downed, turn the host's view toward the copy with `SetControlRotation`. Use a pitch down to the copy's feet and a distance of at least 300 uu, so a lying sprite is in frame.
   - Take shots at `Death01` +0.5 s, at `Corpse01`, and at `Standup` +0.3 s. Do the same on the client for the host's sprite.
   - Try `shot showui` (or `HighResShot 1 showui` if that is not accepted) for the game-over shots, so the menus are in the image.
4. **Refused tags.**
   - While the host's own pawn is Downed, have the test press `InputTag.Fire` and `InputTag.Walk` through the same path the real input uses (`AbilityInputTagPressed`), then release them. Expect one `refused` line each and no shot fired: no new projectile, no ammo change.
   - While the copy is Downed, have the client test send `ChanOp::Fire` 1, then 0. Expect the host to log the ignored Fire 1 and the Fire 0 release.
5. **Run and report.**
   - Two-player run `card044x`, then solo `card044s` to confirm single player is unchanged.
   - Report against "Done when". Read every shot you cite before you describe it, and say plainly when a shot does not show the thing.

## Done when

- The client logs `StartDeath`, then `CompleteDeath`, then a UI input context, after all down, and a UI shot shows its death screen.
- The client has no crash and no new dump, and its death runs once.
- Shots show the remote downed sprite lying down, and standing up after a revive, on both machines.
- The refused Fire/Walk and the ignored relayed Fire 1 each appear in the log, with no projectile spawned.
- The client's death-screen buttons and bindings are logged, and the client's reaction to a host restart is recorded or stated as not driven.
- Single player has the same death and game over as card 043's solo run.
- The build is clean with `/W4 /WX`, and every function is 60 lines or fewer.

## Risks

- `StartDeath` on a pawn without authority. It calls `CancelAllAbilities` and `DisableInput`, and changes the capsule, all locally. The client's pawn is a simulated or autonomous proxy, so a replicated change from the host can undo the local state. That is fine for a pawn that will not play on.
- If the damage-event reads turn out not to be guarded by the count, the zeroed buffer can still crash. Step 1 says to stop and report in that case.
- The menu's restart entry on the client acts only locally (a level reload on the client alone would desync). Do not press the client's buttons in the test.

## Senior addendum (2026-11-04): the damage-event reads are safe with a zeroed buffer

I read 0x1451DE736–0x1451DE7D5. Each of the four reads is a linear search of an array of 8-byte pointers: `begin = [rbx+off]`, `end = begin + count*8` (count at off+8), and the loop runs only while `begin != end`. A count of 0 gives `begin == end`, so it jumps past without a dereference. The whole block is also skipped when `rdx` equals `r13` (the compared value). So step 1's zeroed buffer is safe for these reads; you do not need to stop for them. Still check the rest of `StartDeath` for other `rbx` reads after 0x1451DE811 (I found none in the next 0x340 bytes).

For size: the kill floor (`Tick` at 0x1451E0944, vtable +0xA40) builds its damage event on the stack at `rbp-0x30`, and the frame's locals run to `rbp+0x1A8`. So about 0x1D8 bytes, and a 0x200-byte zeroed buffer covers it.

## Report

Runs: two-player `card044x3` (the accepted run, logs/card044x3-{host,client}.log, logs/card044x3-run.out) and solo `card044s` (logs/card044s-host.log). Shots are copied to logs/card044x3-shots/ and logs/card044s-shots/. Every shot cited below was read before it was described. Both machines write to one Screenshots folder, so each shot was mapped to its machine by the gaps between its file time and the log's `screenshot:` lines; all 14 gaps agree.

Earlier runs: `card044x` (first run) had two shot problems. The client's StandUp shot (00836) was blocked by a railing, because after the revive the copy stayed 120 uu from the host. Also, no shot showed the sprite standing. `card044x2` added a "standing +2 s" shot on both sides and moved the reviver back to the host-downed shot spot. But that spot was captured in a branch that never ran (`hits` is not 0 on entering KillHost), so it stayed at (0,0,0): the host logged a WARN and the client's revive shots were taken from 120 uu. `card044x3` captures the spot at the host's Down instead. That run is clean, so the rest of this Report uses it.

### Done when

1. **Client StartDeath, then CompleteDeath, then UI context, plus a death-screen shot: met.**
   - Client 558100960: `StartDeath (MI trampoline) call` on its own pawn (role 2/3), with Health 1.0, TimeUntilDeathCompletion 0.00 and IsAutoSaveEnabled 1. On return, the timer is 3.00 and autosave is 0.
   - 558103960: `CompleteDeath` 3000 ms later, OnDeath bound 2. In the same ms, `AddMappingContext IMC_DummyUI_KBM priority 2` and `client death screen: 5 widgets`.
   - Shot 00866 (client, showui, +2 s) shows the death screen. It has the quote "Our world is beginning, theirs is dying.", the buttons RESTART CHECKPOINT, RESTART MISSON, LOAD GAME and EXIT, the mouse cursor and the HUD at the bottom. Shot 00868 (+6 s) shows the same screen.
2. **No crash, no new dump, death runs once: met.** Both processes exit with rc 0, and `new crash dumps: none`. Client FATAL/ERROR 0 and WARN 0. StartDeath and CompleteDeath each appear once on the client. The 8 s fallback did not fire.
3. **Remote sprite lying, and standing after revive, on both machines: met. The Death01 +0.5 s shots do not show it lying.** All aim shots are from 350 uu, pitch -20.4, aimed at the feet.
   - Host, viewing the copy:
     - 00855 (Death01 +0.5 s): the copy is **standing** side-on with its rifle, not lying.
     - 00857 (Corpse01): the copy is lying flat.
     - 00858 (StandUp +0.3 s): the copy is on hands and knees, pushing up.
     - 00859 (standing +2 s): the copy is standing upright.
   - Client, viewing the host:
     - 00860 (Death01 +0.5 s): the host is **standing**, facing the camera, not lying.
     - 00862 (Corpse01): the host is lying on the ground behind the railing.
     - 00863 (StandUp +0.3 s): the host is crouched on hands and knees, rising.
     - 00864 (standing +2 s): the host is standing upright.
   - Why Death01 +0.5 s still shows a standing figure: Death01 runs about 1.1 s before Corpse01. On the client, the Death01 key is at 558090346 and Corpse01 at 558091451. The early Death01 frames still draw the figure upright. Corpse01 is the first lying frame.
   - The client's standing shot came 718 ms after its `Standup -> Flinch` key, because KillHost2 starts hitting the host 5 s after the revive. The Flinch frame is upright.
4. **Refused Fire/Walk and the ignored relayed Fire 1, with no projectile: met.**
   - Host 558091276: `card043: host refused InputTag.Fire on downed … (host-own)`, followed by the same line for InputTag.Walk.
   - At the press and at the release, the host logs ammo 0 and host-own FireProjectile calls 0. Caveat: the host holds only BP_Weapon_Unarmed_C, so "no ammo change" here is a weak check.
   - The client sends relayed Fire 1 (558049775) and Fire 0 (558050275), and both are sent.
   - The host logs `Fire 1 from downed BP_PlayerCharacterMI_C: ignored (not relayed); server-copy FireProjectile calls so far 0`. It then logs `Fire 0 …: release passed to the relay`, with the count still 0.
5. **Bindings logged and the client's restart reaction recorded: met.** Both sides dump `WB_PlayerDeathScreen_C`. Its widgets are WB_InputDeviceVisibilityPanel_C and four WB_Boilerplate_FloatingButton_CenteredVersion_Wide_C buttons:

   | Button | `<Button>.OnClicked` calls |
   |---|---|
   | RestartMission | `WB_PlayerDeathScreen_C.BndEvt__WB_PlayerDeathScreen_RestartMission_K2Node_ComponentBou…` |
   | LoadLastSave | `…LoadLastSave_K2Node_ComponentBound…` |
   | LoadGame | `…LoadGame_K2Node_ComponentBoundEven…` |
   | Exit | `…Exit_K2Node_ComponentBoundEvent_6_` |

   - The names are cut short by the ObjectName buffer.
   - On each button, the inner `Button.OnClicked` calls its own `OnPressed_Event`, and `OnHovered` / `OnUnhovered` call `OnHovered_Event` / `OnUnhovered_Event`.
   - Restart, driven on the host only: at 558110932 the host test calls ProcessEvent(death screen, the RestartMission BndEvt), the same function the click runs. It returns after 15 ms. The host then reloads the mission standalone (net mode 0, no listen).
   - **Client reaction: disconnect to the title screen.** At 558111000 (68 ms after the host's call) the client starts its own `UEngine::Browse` to the front end, net mode 0, and gets `Browse returned 0 (Success)`. It does not follow the host's restart. The client's buttons were never pressed.
6. **Solo same as card 043: met.**
   - card044s: 716 lines, FATAL/ERROR 0, WARN 1 (`autodropship`, as in card043s), no crash dump.
   - The solo death runs vanilla: CompleteDeath with OnDeath bound 2, IMC_DummyUI_KBM priority 2, and the death screen built. Card043s logged the same IMC_DummyUI_KBM priority 2, and both runs end with `host test done`.
   - The solo driver does not press restart.
   - The solo shots are plain shots, not showui, as in card 043. Shot 00870 (+5 s) shows only the base world with no menu, so the solo death screen is shown by the log only.
7. **Clean /W4 /WX build, functions of 60 lines or fewer: met.** The build gives rc 0 with no warnings. A script check finds no c43/c44/detour function over 60 lines.

### Other findings

- **The host's game-over shots show a stale "BASIC TRAINING" card.** Shot 00865 (host, showui, 3 ms after CompleteDeath) shows only the level-intro card: "BASIC TRAINING", "Call in tactical resupply drops…" and "PRESS ANY KEY TO CONTINUE". The death screen is not drawn yet. Shot 00867 (+5 s) shows the death screen (quote "Shot by a Morita Trooper", the same four buttons) drawn over that intro card. So the host still has its intro widget in the viewport and showui renders it. The client's death screen (00866) has no such card.
- On the host and in solo, the CompleteDeath line reads "-1 ms after the client's StartDeath". That value means "no client StartDeath on this machine" (the start time is 0). It is a wording quirk of the log line, not a timing.
- In card044x3, the host-downed spot is (-560, -1715, 103), and the reviver moves back there with `moved 1, 350 uu`.

### Changes (mod/src/hooks.cpp)

- The client runs the real StartDeath once, through `g_orig_start_death_mi`, on the all-down Down frame. It passes a zeroed 0x200-byte damage event, and `c43_skip_start_death` lets that one call through. The client logs StartDeath (call and return), CompleteDeath, the UI context and IsAutoSaveEnabled, with an 8 s fallback that did not fire.
- Death-screen dump: widgets, OnClicked/OnHovered/OnUnhovered bindings, and the RestartMission function. `c44_restart` calls that function on the host after the test.
- Test driver:
  - Aim shots at 350 uu pitched to the feet (Death01 +0.5 s, Corpse01, StandUp +0.3 s, standing +2 s) on both machines, and showui game-over shots.
  - Refused Fire/Walk presses on the downed host, and relayed Fire 1/0 from the downed client.
  - The reviver goes back to the host-downed spot after the revive.

## Senior review (2026-11-04)

Accepted.

Checked myself:
- `logs/card044x3-run.txt` and `logs/card044s-run.txt` are both rc=0.
- x3 host and client logs have 0 FATAL/ERROR and 0 WARN lines. The solo log has 1 WARN, the autodropship line. No new `.dmp` files.
- No function in `mod/src/*.cpp` is over 60 lines.
- Client log: `StartDeath` through the MI trampoline once (autosave 1 → 0, timer 3.00), `CompleteDeath` 3000 ms later (OnDeath bound 2), then `IMC_DummyUI_KBM` priority 2 in the same ms.
- Host log: refused `InputTag.Fire` and `InputTag.Walk` on the downed host pawn, and the downed client's relayed Fire 1 ignored with server-copy FireProjectile 0.
- Shots I read:
  - 00857 shows the copy lying flat (Corpse01).
  - 00863 shows the host pawn on hands and knees (Standup), seen from the client.
  - 00866 shows the client's death screen with the four buttons and the cursor.
  The report's descriptions match.

Notes:
- The upright Death01 +0.5 s frames are the game's own animation timing, not a mod fault.
- The ammo check is weak because the host held only `BP_Weapon_Unarmed_C`. The refused lines and FireProjectile 0 are enough for now.

Open (carried to STATE):
- Restart takes the host into a standalone reload (net mode 0), and the client drops to the title screen 68 ms later. Co-op needs the host's restart to keep the session (listen) and bring the client along.
- The client's own death-screen buttons act locally. RESTART CHECKPOINT or RESTART MISSON on the client would reload the client alone. They should be disabled or routed to the host.
- The host's death screen draws over a stale level-intro card ("BASIC TRAINING … PRESS ANY KEY TO CONTINUE"), shot 00865/00867. Check whether this happens without the mod.
- The kill floor still bypasses `HandleDeathEvent` (senior lead in card 044's chat; Tick at 0x1451E0944 calls `StartDeath` through vtable +0xA40).
