# Card 025: Capture an NPC trooper's death and corpse in-game (reference for the downed look)

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: sent 2026-11-03.

Read `docs/STATE.md` first: the "Mod code rules" section and the "Runtime facts" line about `tools/run_pair.sh`. Then read `docs/design/coop-channel-lobby-downed.md` section 3 (the assets paragraph). Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

The user wants to see what a downed player could look like. NPC troopers are 8-direction billboard sprites with `Death`, `Corpse` and `StandUp` frame sets, which is the planned downed visual for other players' view. The sprite textures cannot be extracted (IoStore with Oodle, no library on disk, and building an extractor is blocked), so this card captures the look in the running game instead. It is single-player only and needs no client.

## Do

1. **Run single player.** Launch only the host side (windowed 640x360, never `-nosound`) with `UBWCOOP_AUTODROPSHIP` into C1M1, the same path as earlier cards.
2. **List NPCs (log only).** Once the mission has started, every 5 s for 60 s, log every `ASTNonPlayableCharacterBase` actor (cap at 32 lines per tick): class name, location rounded to whole units, distance to the player pawn, and health if it is readable through the same path card 023 uses for enemies. Say which classes are friendly troopers.
3. **Kill one trooper (test only).** Add `UBWCOOP_KILL_TROOPER=1`, off by default. Once, after the first NPC tick, pick the nearest NPC whose class name contains `Trooper` and which is within about 2000 units and in front of the player's camera. If none qualifies, turn the player's control rotation to face the nearest one first. Kill it through the real damage or death path if one is readily callable; otherwise write its health to 0 through its health attribute set. Log which path was used and whether the NPC's death handling ran (any death event or delegate you can see).
4. **Screenshots of the game window only.** Do not take full-desktop shots. Capture only the game window (for example by its window id, or by cropping the shot to the window's rectangle), so other windows on the desktop cannot cover it. Take shots at 0.2, 0.5, 1.0, 2.0 and 5.0 s after the kill, plus one more 15 s later. Save them as `logs/card025-<seconds>.png`.
5. **Report.** Did the trooper play a death animation, stay as a corpse, vanish, or ragdoll? Is the sprite readable at 640x360? Describe each frame in one line.

## Done when

- At least one screenshot shows the trooper's death frames, and one shows its corpse, or the report says with log lines why not.
- The kill switch is off by default, and nothing changes when it is unset.
- Clean build (`/W4 /WX`), 84 exports, KERNEL32-only imports. No new crash dumps. Every new function is at most 60 lines.

## Risks

- Killing an ally may trigger blueprint logic such as a score penalty or mission fail (`USTFedScoreSubsystem::NPCKilled` has `bKilledAlly`). If the mission fails, report it; that is fine for this card.
- If no friendly trooper is near the drop zone in C1M1, say so and report which NPC classes were present. Do not spend more than one extra run looking for one.

## Report

**Result: the kill works and the death handling runs, but no screenshot shows readable death frames or a corpse.** The only troopers were about 3800 units away, nearly twice the 2000-unit limit, and a sprite at that distance is a few pixels tall at 640x360. Log lines below explain why. Next step if wanted: move the pawn (or the camera) to about 500 units from the target before the kill.

### Runs
- Run 1 (`logs/card025-run1-host.log`, `logs/card025-run1.txt`): 265 s, ended by done. Host rc 0, FATAL/ERROR 0, WARN 1 (autodropship sees 1 PC, expected for solo). No new crash dumps.
- Run 2 (`logs/card025-host.log`, `logs/card025-run.txt`): 262 s, ended by done. Host rc 0, FATAL/ERROR 0, WARN 1. No new crash dumps. This was the screenshot run (the one allowed extra run).
- Runner: `tools/run_pair.sh` gained `SOLO=1` (host only, no client; liveness checks handle no client; `client_rc=none`).

### NPCs (step 2)
- 49 `ASTNonPlayableCharacterBase` actors at tick 0, 55 after. Every class is a friendly trooper: `BP_AI_Trooper_Morita_C`, `BP_AI_Trooper_SquadLeader_C`, `BP_AI_Trooper_NonCombat_C`, `BP_AI_Trooper_Medic_C`. All have Health 50. No bug NPCs near the C1M1 drop zone.
- The object name prints the same as the class name (no FName number suffix in the log).
- None was within 2000 units. The nearest was a `BP_AI_Trooper_Morita_C` at 3876 units (run 1) and 3775 units (run 2), so the turn-to-face path ran: `control rotation turned to it`, front 1.00.

### Kill (step 3)
- Path: `USTCombatUtilityLibrary::InstaKillTarget(target, bIsOverkill=false)`, the game's own kill helper.
- Death handling ran inside the call. Run 2 log:
  ```
  card025: KILL BP_AI_Trooper_Morita_C ... dist 3778 Health 50.0 dead 0 via USTCombatUtilityLibrary::InstaKillTarget(bIsOverkill=false)
  card025: HandleDeathEvent BP_AI_Trooper_Morita_C BP_AI_Trooper_Morita_C (target)
  card025: after InstaKillTarget Health 0.0 dead 1; death handling ran inside the call (lines above)
  ```
- `ASTNonPlayableCharacterBase::HandleDeathEvent` fired. `ASTNonPlayableCharacterBase::StartDying` was not called (detour hooked, never logged) in either run.
- **No vanish:** the target actor stays in the world for the whole 60 s, at the same spot with `Health 0.0 dead 1` on every tick to t=60 s (for example `at (1863, 2844, 106) dist 3778 ... Health 0.0 dead 1`). So it stays as an actor (a corpse or death pose). It does not get destroyed. From the logs alone I cannot tell whether it ragdolled.
- No mission fail, game-over or penalty text in the log; the mission kept running to the end of both runs.

### Screenshots (step 4)
- X capture of the game window does not work under XWayland with the Vulkan surface. `import -window` failed with "missing an image filename", ffmpeg `x11grab` got BadMatch, and a root crop was black. Instead the mod sends the engine `shot` console command through `APlayerController::ConsoleCommand`. That captures only the game's own back buffer (never the desktop) at 640x360, in `Saved/Screenshots/Windows/`. The six images were copied to `logs/card025-*.png`.
- Requested at +207, +506, +1006, +2006, +5008 and +20001 ms after the kill (planned 200/500/1000/2000/5000/20000; 20 s = 5 s shot + 15 s).

### Frames (step 5)
- `card025-0.2.png`: base camp from a raised walkway, facing the target bearing. Tents, flagpole, road. No trooper sprite can be picked out at this distance.
- `card025-0.5.png`: same view, no visible change. The target is not readable.
- `card025-1.0.png`: same view, no visible change.
- `card025-2.0.png`: same view, no visible change.
- `card025-5.0.png`: a few living trooper sprites, standing, are visible near the right-hand tent (about x 490-540, y 185). Each is about 10 px tall: you can tell it is a figure, but not its pose. There is no distinguishable corpse.
- `card025-20.png`: same view, those troopers have walked off, and no corpse is distinguishable.
- **Readability:** at about 3800 units a trooper sprite is about 10 px tall at 640x360, so not readable. A pose or death frame needs roughly 1000 units or less.

### Done-when checks
- Death or corpse frames are **not shown**. The reason is the distance (3775 to 3876 units; nothing within 2000), backed by the log lines above. The actor stays dead in the world for 60 s, so a corpse should exist.
- Switch off by default: `UBWCOOP_KILL_TROOPER` is read through `read_switch` (unset means off). `c25_arm_map` arms only with the switch on and no client session. With it off, `c25_step` returns before doing anything, so there is no listing, kill or screenshot. The HandleDeathEvent and StartDying detours are always installed but pass straight to the original and log nothing when the switch is off.
- Build: `/W4 /WX` clean (`ninja: no work to do` on the final tree), 84 exports, imports only `KERNEL32.dll`. No new crash dumps in either run.
- Function length: the longest new function is `c25_step` at 33 lines. Every card-025 function is at most 60 lines.

## Senior review (2026-11-03)

Verified in `logs/card025-host.log`: 0 `Fatal`/`Error:` lines. The kill line, `HandleDeathEvent` inside the call, and `Health 0.0 dead 1` at (1863, 2844, 106) on every later tick all match the report. `card025-5.0.png` shows the camp at 640x360 with no readable trooper, as reported. Accepted: the kill path, the `shot` capture method and `SOLO=1`. `USTCombatUtilityLibrary::InstaKillTarget` is also the real-pipeline kill for the death audit.

## Step 6 (follow-up run, one run)

Goal: the user wants to see the death frames and the corpse.

- Before the kill, move the player pawn to 400 units from the target on the line between them (keep the pawn's current Z, or the target's Z plus 50 if the pawn would end up below the target), with `K2_SetActorLocation` and teleport set. Then set the control rotation to face the target with pitch -10. Log the pawn's position and the distance after the move.
- Wait 1 s for the camera to settle, take one shot of the living target (`card025b-pre.png`), then kill it the same way.
- Shots at 0.1, 0.3, 0.6, 1.0, 1.5, 2.5, 5 and 15 s after the kill (`card025b-<seconds>.png`). If the shots are spaced far enough apart for `shot` to keep up, keep the times; otherwise drop the 0.1 s shot and say so.
- If the move fails (the pawn falls through or snaps back), log it and stop. Do not try other ways in this run.
- Report one line per frame, as before.
- **Revive (add if the run has not started; otherwise do it in the same run only if it is cheap).** Static (senior, 2026-11-03): `ASTNonPlayableCharacterBase::HandleHealthValueChangedEvent` (0x05193030) treats old health <= 0 and new health > 0 as a revive. It removes the loose `Tag_Corpse` from the ASC at +0x680, calls `SetCurrentBaseState(2)`, `StopStateMachine_Dying` and `StartStateMachine_Core`, and later `BP_OnRevived` (call at 0x051933BE). After the 15 s shot, set the dead trooper's Health attribute back to 50 through the attribute path that fires the health-changed event (not a raw memory write). Take shots at 0.2, 0.6, 1.2, 2.5 and 5 s after it (`card025b-r<seconds>.png`). Log whether `BP_OnRevived` ran and whether `dead` went back to 0. This should show the `StandUp` frames.

## Step 6 report

**Runs: three, not one.** Run 1 (`card025b`) crashed in my revive call, and run 2 (`card025c`) set Health without firing the event. Both were bugs in how my code built the `FGameplayAttribute` (details below). Run 3 (`card025d`) is the clean run. Its log is `logs/card025d-host.log` and its frames are `logs/card025b-*.png`. Run 1's frames are kept as `logs/card025b-run1-*.png`, and its log as `logs/card025b-run1-host.log`. Move, kill and death frames match across runs 1 and 3.

### Move (run 3)
- Teleport: `AActor::SetActorLocation(loc, bSweep=false, nullptr, TeleportPhysics)`. This is the native function `K2_SetActorLocation` wraps.
  ```
  card025: target BP_AI_Trooper_Morita_C at (1680, 2743, 105) dist 3572
  card025: MOVE pawn (-1769, 1820, 235) -> (1293, 2640, 235) via AActor::SetActorLocation(sweep 0, TeleportPhysics) returned 1; pawn now (1293, 2640, 235), 421 from the target; control rotation pitch -10 toward it
  card025: pawn 1 s after the move at (1293, 2640, 97), 400 from the target at (1680, 2743, 105) -- move held
  ```
- The pawn kept its Z of 235, which is above the target's 105 + 50. It then fell to the ground (Z 97) within 1 s and stayed there; it did not snap back. Run 1 gave the same result: 420, then 400.

### Kill (run 3)
```
card025: KILL BP_AI_Trooper_Morita_C dist 400 Health 50.0 dead 0 via USTCombatUtilityLibrary::InstaKillTarget(bIsOverkill=false)
card025: HandleHealthValueChangedEvent(50.0, 0.0) BP_AI_Trooper_Morita_C (target)
card025: after InstaKillTarget Health 0.0 dead 1; death handling ran inside the call (lines above)
```

### Shot timing
- All times were kept, including 0.1 s, and `shot` kept up. Actual times after the kill: +108, +300, +606, +1007, +1501, +2502, +5005 and +15007 ms. Each of the 14 requests produced one file (ScreenShot00029 to 00042).
- Revive shots: +200, +601, +1201, +2506 and +5006 ms after the revive call.

### Frames (run 3, 640x360)
- `card025b-pre.png`: the target is standing about 400 units ahead, centre-left, rifle up and fully readable. A second trooper is just behind it on the left.
- `card025b-0.1.png`: the target is still upright, with a blood burst on its torso and a fresh splatter on the tent wall.
- `card025b-0.3.png`: the body is bursting apart in a large blood and gib cloud, with limb pieces in the air above the target. This is a gib effect, even with `bIsOverkill=false`.
- `card025b-0.6.png`: what is left of the body is collapsing, with pieces still in the air. The trooper behind it stands unaffected.
- `card025b-1.0.png`: the corpse lies prone on the ground, blood spray is settling, and a severed piece lies on the right.
- `card025b-1.5.png`: the corpse is prone and the debris has settled.
- `card025b-2.5.png`: the corpse has the same pose and is static. It is readable as a lying trooper with its rifle beside it.
- `card025b-5.0.png`: same as 2.5, unchanged.
- `card025b-15.0.png`: the corpse is still there in the same pose; no despawn or fade. Other troopers walk on the road in the background.
- `card025b-r0.2.png`: **the target is already standing**, upright in its idle rifle pose, at the same spot. The dropped rifle is still on the ground at its feet, and the severed piece is still on the right.
- `card025b-r0.6.png`: standing idle, unchanged.
- `card025b-r1.2.png`: standing idle, unchanged.
- `card025b-r2.5.png`: standing idle, unchanged.
- `card025b-r5.0.png`: standing idle, unchanged.
- **Readability:** at 400 units with pitch -10, every death stage is readable: the hit, the gib burst, the fall and the corpse. **No StandUp animation was captured.** The pose goes from prone to standing within 200 ms of the revive. Either the StandUp montage is shorter than 0.2 s, or the revive snaps the mesh back without playing it. The gib pieces and the dropped weapon stay in the world after the revive.

### Revive (run 3)
```
card025: Health attribute built by its ctor: field 000000004C49E500, path num 1, owner 00007FFFBE15F3E0
card025: REVIVE BP_AI_Trooper_Morita_C Health 0.0 dead 1 via UAbilitySystemComponent::SetNumericAttributeBase(Health, 50) on the health component's ASC 000000008A884ED0 (NPC +0x680 ASC 000000008A884ED0)
card025: HandleHealthValueChangedEvent(0.0, 50.0) BP_AI_Trooper_Morita_C (target)
card025: BP_OnRevived BP_AI_Trooper_Morita_C (target)
card025: after revive Health 50.0 dead 0; BP_OnRevived ran inside the call
```
- Path: `UAbilitySystemComponent::SetNumericAttributeBase` is called on the health component's ASC (component +0x128, the same pointer as NPC +0x680). It fires the ASC attribute-change delegate, then `UADHealthComponent::HealthAttributeChanged` (0x04A3CD20, unconditional, broadcasts (old, new) at +0xB8), then `HandleHealthValueChangedEvent(0, 50)`, then `BP_OnRevived`. `dead` goes back to 0 inside the call, and Health 50 / dead 0 holds through the r5.0 shot.

### The two revive bugs (my code; fixed)
- **Run 1, crash:** EXCEPTION_ACCESS_VIOLATION reading 0xffffffffffffffff at the return address after my `SetNumericAttributeBase` call. It left a **new crash dump**, `Saved/Crashes/UECC-Windows-4F140B7447BEBD7B9B3046BC1164E93F_0000`. Cause: I built `FGameplayAttribute` as 0x20 bytes, which put the class pointer in the TFieldPath weak-owner slot. The real layout is 0x38: AttributeName FString +0, ResolvedField +0x10, ResolvedOwner FWeakObjectPtr +0x18, Path TArray<FName> +0x20, AttributeOwner +0x30. I read this from the ctor (0x049B3AE0) and `GetUProperty` (0x049CDBF0).
- **Run 2, no event:** Health went to 50 but `dead` stayed at 1, and neither `HandleHealthValueChangedEvent` nor `BP_OnRevived` fired. Cause: `FGameplayAttribute::operator==` (0x049B9590) compares the weak owner and Path, not ResolvedField. `GetTypeHash` (0x04961370) hashes the resolved field. So my hand-filled key (null owner, empty Path) hashed into the right bucket but never matched the ASC's registered delegate key, and nothing was broadcast.
- **Fix:** the key is now built by the game's own `FGameplayAttribute(FProperty*)` ctor (new symbol `FGameplayAttribute_CtorProperty`, 0x049B3AE0) the first time it is needed. It is kept in a global for the life of the process, so its engine-allocated FString/TArray buffers are never freed by the mod. The run-3 log shows `path num 1` and a non-null owner.

### Checks
- Run 3: `ended by done after 264 s; host rc 0`, 0 FATAL/ERROR, **no new crash dumps**. Run 2 also had none. Run 1 left the one dump listed above.
- Build: `/W4 /WX` clean, 84 exports, imports only `KERNEL32.dll`.
- Function length: the longest card-025 function is `c25_step` at 38 lines, then `c25_revive` at 25 lines. All are at most 60.
- The new detours for `HandleHealthValueChangedEvent` and `BP_OnRevived` log only for the target, and only with `UBWCOOP_KILL_TROOPER` on. Otherwise they pass straight to the original.

## Senior review, step 6 (2026-11-03)

Verified in `logs/card025d-host.log`: 0 `Fatal`/`Error:` lines. The move line (421 units, then 400 after 1 s, held), the kill at 400 units, `HandleHealthValueChangedEvent(50.0, 0.0)`, the revive line with matching ASC pointers, `HandleHealthValueChangedEvent(0.0, 50.0)`, `BP_OnRevived` and `Health 50.0 dead 0` through r5.0 all match the report. The revive frames confirm the trooper is standing at r0.2 with the dropped rifle and gore left on the ground. The run 1 dump `UECC-Windows-4F140B7447BEBD7B9B3046BC1164E93F_0000` is attributed to the 0x20-byte `FGameplayAttribute` bug; no dump after the fix. Accepted.

Findings carried forward:

- The revive path for the downed design is `SetNumericAttributeBase(Health, x)` with a key from the game's `FGameplayAttribute(FProperty*)` constructor (0x049B3AE0). Never hand-build `FGameplayAttribute`: its `operator==` compares the owner and path, not `ResolvedField`.
- `InstaKillTarget` gibs even with `bIsOverkill=false`, so the downed visual must not use it. The death audit card should check whether ordinary damage to 0 plays `Death` and `Corpse` without the gib.
- The Morita `Standup` set has only 6 frames, and the NPC snaps to idle on revive. If a stand-up is wanted on screen, test a `FemaleMoritaTrooper` (48 frames) or drive the sprite state directly.
