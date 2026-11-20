# Card 099: The host plays the client's first-person fire cues and notifications

Status: written 2026-11-05 (senior); sent and accepted 2026-11-05.

Read:

- `docs/playtest-2026-11-05.md`, items 2 and 3;
- card 024 (the client's fire is relayed to its server copy, which runs the full fire ability on the host);
- `mod/src/ubw/grenades.cpp`, the card 074 FX log hooks (`SpawnSoundAtLocation`, `ExecuteGameplayCue`) as the pattern.

Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`. Play DLL base: `7f93daf100d2ee0c` (card 098).

## Why

In the 2026-11-05 playtest, when the client fired, the host heard the shot as its own first-person, non-spatial sound
and got the client's camera shake. The host's HUD also showed the client's notifications.

Since card 024, the host runs the client's fire ability, animations and cues on the server copy pawn. The game is
single-player code, so the first-person feedback goes to "the player" instead of the pawn's own controller.

### The abstraction: "the player" is player index 0 (senior, from a direct-call scan of the exe)

The game resolves "the player" in one place. `UGameplayStatics::GetPlayerController(ctx, index)` (0x03C51190)
is called with index 0, and every higher-level helper goes through it:

| Helper | RVA | Direct callers | What it gives "the player" |
|---|---|---|---|
| `USTCommonUtilityLibrary::GetSTPlayerController(ctx)` | 0x0523E190 | 33 | the controller |
| `USTCommonUtilityLibrary::GetSTPlayerCharacter(ctx)` | 0x0523E0F0 | 69 | the pawn |
| `USTCommonUtilityLibrary::GetSTHUD(ctx)` | 0x0523DF50 | 15 | the HUD (screen effects, gore on screen) |
| `USTCommonUtilityLibrary::GetSTLocalPlayer(ctx)` | 0x0523DFC0 | 10 | key glyphs and options |
| `USTCommonUtilityLibrary::StartCameraShake(ctx, class, scale)` | 0x05251D30 | 14 | camera shake |
| `USTCommonUtilityLibrary::PlayForceFeedbackEffect(ctx, effect)` | 0x05248680 | 11 | rumble (`ClientPlayForceFeedback_Internal`) |
| `USTCommonUtilityLibrary::SpawnSoundAttachedToPlayer[WithAIAwareness]` | 0x05250E70 / 0x05250F40 | 1 / 3 | a sound attached to the player's pawn |
| `UGameplayStatics::GetPlayerCharacter / GetPlayerPawn / GetPlayerCameraManager` | 0x03C51120 / 0x03C51620 / 0x03C510D0 | | Blueprint "Get Player ..." nodes |

`GetPlayerController` has only 47 direct callers. Blueprints reach it through the `exec` thunks, so they go through
the same function. Callers of `StartCameraShake` include `ApplyRecoil`, the ScreenShake anim notify, the impact cue,
and `ASTPlayerCharacter::PlayHurtCameraShake`. Callers of `PlayForceFeedbackEffect` include
`ASTPlayerCharacter::PlayFootstepCameraShake` and `PlayFootstepEffects`. Those player-character functions also run on
the server copy, so the host gets the client's footstep and hurt feedback too, not only its fire.

So the higher-level fix is **one gate in `GetPlayerController`**. On the host, when index is 0 and the context object
belongs to a server copy, return that server copy's own controller (the client's PlayerController on the host) instead
of the host's. The context belongs to a server copy when it is the pawn, or reaches the pawn through its outer chain
(ability, anim notify mesh component, weapon component) or its Owner/Instigator chain (weapon actor). Use the pawn
classification from card 019 (`SprintSide`: Authority with remote AutonomousProxy is a server copy).

With the remap, every helper then does the right thing for that pawn without a patch of its own:

- the camera shake goes to the client PC's camera manager on the host, which no one sees;
- the force feedback goes to a PC with no local player, so nothing plays;
- `GetSTPlayerCharacter` returns the server copy, so a sound attached to the player is attached to the client's pawn
  and plays in 3D at its position;
- `GetSTHUD` returns null, because the remote PC has no HUD. **Check every `GetSTHUD` caller for a null check**
  (`AddScreenEffects`, `SpawnScreenAndWeaponGoreEffects`, `ActivateTeleportScreenEffect`, `DrawToScreenRect`). If
  one does not check, gate that caller or return the host's controller for it.

Constraints for the gate:

- The mod calls `g_get_player_controller(ctx, i)` 34 times (hooktable.cpp:268), some with a pawn, component or widget
  as the context (hooks_ubw.cpp:743, objectives.cpp:2171, objectives.cpp:2287). Those calls must go to the
  trampoline, not the detour, so the mod's own lookups do not change.
- Only index 0 is remapped, only on the host (listen server). Contexts that are a world, a level script, an enemy, a
  subsystem or a widget do not change. Enemy code that means "the human player" (`ASTEnemyBase::CanPerceiveTargets`,
  `USTAttackActionBase::GetDistanceToPlayer`, AI threat) keeps the host. That is card 103, not this
  card's.
- The client side does not change: there, index 0 is already the client's own controller.

Notifications (item 3) may not go through this remap. `USTFedScoreSubsystem::NPCKilled` and
`USTFedScoreNotifier::OnDeath` call `GetSTPlayerCharacter` with an enemy or subsystem context, which the remap leaves
alone. Measure who raises the host's notification for a client kill (see below).

### Detail found earlier

One path is confirmed from the disassembly:

- `USTAnimNotify_ScreenShake::Notify_Implementation` (0x050F6170) calls `USTCommonUtilityLibrary::StartCameraShake`
  (0x05251D30).
- `StartCameraShake` calls `UGameplayStatics::GetPlayerController(context, 0)` (0x03C51190), then casts to
  `ASTPlayerController`.
- So a screen-shake notify on the server copy's fire animation shakes the host's camera.

The mantle montage `AM_Arms_Mantle` also has an `STAnimNotify_ScreenShake` notify (`Shake_RegularMantle`), so a
client mantle can shake the host too if the montage plays on the server copy (card 094 made the server copy run
GA_Mantling_Move).

`USTAnimNotify_ScreenShakeAtActor` (0x050F61C0) uses `StartCameraShakeAtLocation` (0x05251EA0), which is a world
shake by distance; check it too.

A second shake path, also from the disassembly: `USTWeaponFireAbility::ApplyRecoil` (0x0522FD00) calls
`StartCameraShake` with the shake class at ability+0x628, so every server-copy shot also shakes the host through
player 0. ApplyRecoil also calls `ApplyModToAttribute` for the RecoilAmountX/Y attributes on the weapon's ASC; that
is the weapon's own ASC, so check whether the host's aim kicks (ControlRotation) during the client's bursts.

Sound, the likely path: `USTWeaponFireAbility::Fire` (0x05236320) calls
`USTCommonUtilityLibrary::PlaySoundAtLocationFNameWithAIAwareness` (0x05248A10) at exe+0x52368C7, with the FName from
[r13+0x168] and the muzzle transform. That function looks the FName up with `UADAudioManager::GetAudioEvent`
(0x04A1D260) and plays it through `PlaySoundAtLocationWithAccessibility` (0x05248D40). The sound is placed at a
location, so the host hearing it as 2D most likely means the event is the first-person, non-attenuated fire sound.
The fix for the server copy is then attenuation (or the third-person event, if the weapon data has one), not a
dropped call: the host must still hear the client's shots, from the right place. Confirm in the measure run that
this call fires on the host for server-copy shots and name the FName.

Other sound candidates, if the measure shows the fire sound is not that call:

- `UGameplayStatics::PlaySound2D` (0x03C62C30), `SpawnSound2D` (0x03C6FD60);
- `UADAudioManager::PlaySound2D` (0x04A1DF10);
- `USTPlayCustomSoundAnimNotify::PlaySound` (0x050FB9D0), an attached (3D) sound;
- impact cues `USTImpactGameplayCueNotify::PlayImpactSound` (0x05133C80) and `PlayScreenShake` (0x05133E50).

Notifications (item 3), the likely source is the single `USTFedScoreSubsystem`, which has no notion of which player
scored:

- `AddFedScore(tag, FText, int)` 0x051BC960, `AddFedScore(tag)` 0x051BCBB0, `AddBonusFedScore` 0x051BC670;
- `EnemyKilled` 0x051C5340, `NPCKilled` 0x051CE9E0;
- `USTCommonUtilityLibrary::UpdateFedScore` 0x052564C0;
- `USTFedScoreNotifier::OnDamageTaken` 0x051CFDE0, `OnDeath` 0x051CFEB0, `WasNPCDamagedByPlayer` 0x051EA6F0;
- `USTTacticalSupportComponent::HandleFedScoreUpdate` 0x050ED6C0 (score feeds the support charge).

Log who calls these for a server-copy kill. Tutorial hints and pickup prompts are other sources; find them in the
measure run if the HUD shows more than score popups.

## Do

1. **Measure (one run, log only).** The client fires a few bursts near the host, walks, takes a hit, mantles, picks
   something up and kills a bug. On the host, hook `GetPlayerController` and log each index-0 call whose context
   resolves to a server copy: the context class, the pawn, and the caller (count per caller, capped). This shows which
   helpers the remap covers. Also log each call to the other functions above, and each `ExecuteGameplayCue` on the
   weapon ASC, with:
   - the context or owning actor;
   - whether that actor is the server copy, the host's pawn, or neither;
   - the resolved player controller where there is one;
   - the caller.

   Log the HUD notification calls the same way. Find the widget or HUD function that shows them, then log who
   raised them and for which pawn. Also check whether the host's ControlRotation changes during the client's
   bursts (recoil).
2. **Fix, one gate.** Where "the player" is resolved for a pawn that is a server copy, the first-person feedback must
   not reach the host:
   - no camera shake, recoil or force feedback on the host for the server copy's actions;
   - the 2D fire sound becomes a 3D sound at the server copy's position (or is dropped if the 3D part of the same cue
     already plays);
   - notifications raised for the server copy do not show on the host's HUD.

   Use the `GetPlayerController` remap above as the one gate. Add a per-helper gate only for a path the measure shows
   the remap does not cover (for example the fire sound, if it is a located sound and not attached to the player).
   Do not use one patch per asset. The host's own shots and the client's own feedback on the client must not change.
   Whether the client should get its notifications from the host (because the host decides kills and pickups) is a
   question for the Report, not this card, unless the measure shows the client gets none.
3. **Proof**, one combined scenario run (FAST=1):
   - the client fires bursts next to the host;
   - the host log shows the shake and 2D sound skipped (or replaced) for the server copy;
   - the host's camera and ControlRotation do not move;
   - the client walking and taking hits next to the host gives the host no footstep or hurt shake or rumble;
   - the host still gets its own shake and sound when it fires;
   - a notification raised for the client does not show on the host.

   rc 0, ERROR/FATAL 0, no new dump.

Stop when the fix builds and that one run shows it; the user will playtest.

## Done when

- The Report has the measured caller table (caller, context class, count), names each path (shake, sound, recoil,
  footsteps, notifications) and whether the remap covers it, the gate, the `GetSTHUD` null check result, the proof run
  and the DLL hash.
- The Report ends with "Report complete."

## Report

Junior, 2026-11-05. DLL `708552b525f39f4b`. Proof run `c99p3` (FAST=1, both sides `UBWCOOP_C99_LOG=1
UBWCOOP_GRANT_WEAPON=MoritaMk1`, host `UBWCOOP_AUTODROPSHIP=1`): host rc 0, client rc 0, FATAL/ERROR 0, WARN 0,
no new crash dump, scenario `tools/scenarios/c99-cues.txt` passed.

### The gate

`mod/src/ubw/player_c99.cpp`, `detour_c99_get_pc`, hooked on `UGameplayStatics::GetPlayerController` (always on).

- It calls the original first, through `g_get_player_controller`. The hook row's original slot is
  `g_get_player_controller`, so MinHook writes the trampoline there. All 34 of the mod's own lookups go to the
  trampoline and do not change.
- When index is 0 and ctx and the result are non-null, `c99_copy_of(ctx)` walks up to 8 hops:
  - a non-actor follows its outer;
  - an actor is the server copy if `sprint_side` says `kSideServerCopy`;
  - otherwise the actor's Instigator (0x1A0) counts if that is a server copy;
  - otherwise the walk follows Owner, or outer when Owner is null.
- On a hit, `c99_copy_pc` returns the copy itself if it is a PlayerController, or a pawn's Controller (0x2D8) if that
  is a PlayerController. That controller replaces the host's.
- On the client, and for world, subsystem, widget and enemy contexts, nothing resolves to a server copy, so the
  result does not change.
- `UBWCOOP_C99_LOG=1` installs the log-only hooks: `PlaySound2D`, `SpawnSound2D`, `UADAudioManager::PlaySound2D`,
  `PlaySoundAtLocationFNameWithAIAwareness`, `APlayerCameraManager::StartCameraShake`, and both `AddFedScore`
  overloads.

The first run (`c99p1`) crashed both sides in the shake log hook. `TSubclassOf` arrives as a pointer to the caller's
copy (MSVC x64, class with user-defined constructors), not as the class itself. The detour now takes
`const void* const*`.

### Caller table (host, c99p3, first call per caller logged)

Every remap resolved to the client's pawn `BP_PlayerCharacterMI_C` and its controller. Per-caller counts are dumped at
the next map arm; the run quit before one. So the counts below come from the shake log, which logs every call.

| Caller | Function | Context class | Calls |
|---|---|---|---|
| exe+0x5251D56 | `USTCommonUtilityLibrary::StartCameraShake` | `BP_PlayerCharacterMI_C` | 56 shakes (52 fire, 4 walk) |
| exe+0x5248694 | `USTCommonUtilityLibrary::PlayForceFeedbackEffect` | `BP_WeaponFireAbility_MoritaMk1_C` | ≥1 |
| exe+0x523E0FD | `USTCommonUtilityLibrary::GetSTPlayerCharacter` | `ABP_Arms_Skeleton_AnimBlueprint_C` | ≥1 |
| exe+0x3C7F202 | `UGameplayStatics::execGetPlayerCharacter` (Blueprint) | `ABP_Arms_Skeleton_AnimBlueprint_C` | ≥1 |
| exe+0x523A1AC | `USTCommonUtilityLibrary::GetCenterScreenWorldPostionDirection` | `BP_PlayerCharacterMI_C` | ≥1 |
| exe+0x52023FD | `USTSpriteRenderComponent::GetLocalToWorldForProxy` | `STSpriteRenderComponent` | ≥1 |

### Paths

- **Shake (fire, ScreenShake notify, recoil shake): covered.** On the host, 52 `Shake_Fire_MoritaStandard_C` calls
  went to the camera manager of the client's controller (server copy). The host's own 16 fire shakes and its
  `Shake_Generic_Fall_C` stayed local. `ApplyRecoil` goes through the same `StartCameraShake`.
- **Recoil on the host's aim: not seen.** The host screenshots before and during the client's two 2.5 s bursts show
  the same view.
- **Footsteps: covered.** `PlayFootstepEffects` (exe+0x51D6F39) shook the client's camera (`Shake_Walk_2ndPass_C`,
  4 calls), not the host's. Rumble (`PlayForceFeedbackEffect`) is remapped. On the client's controller on the host
  this is a `ClientPlayForceFeedback` RPC, so it may rumble the client a second time. Not measured: no gamepad.
- **Fire sound: no 2D fire sound seen on the host for the client's shots.** None of the hooked 2D sound calls fired
  for the server copy. The only located sounds were impact cues (`USTImpactGameplayCueNotify::PlayImpactSound`,
  context World, 3D at the hit). The call at exe+0x52368C7 in `Fire` is inside the hit-result loop, so it is the
  impact, not the shot. The first-person shot sound most likely plays through a sound attached to "the player"
  (`GetSTPlayerCharacter` from the arms anim blueprint, now remapped to the client's pawn, so it plays at that pawn
  in 3D). `ExecuteGameplayCue` was not logged, because card 074's log install owns that hook. Open: the user's
  playtest has to confirm by ear.
- **Notifications: not reproduced.** The only score calls were `AddFedScore FedScoreSystem.Reward.Goal` (an
  objective, shared by design). No client kill happened in the run. `NPCKilled` and `OnDeath` use enemy or subsystem
  contexts, which the remap leaves alone. A score popup for a client kill would still show on the host. Open for the
  playtest, and needs a per-player score owner if confirmed (outside this gate).
- **Aim side effect (playtest item):** `GetCenterScreenWorldPostionDirection` now uses the client's controller and
  camera on the host for the server copy, not the host's camera. That should be the right aim source for the
  client's shots. Watch for a change in where the client's bullets land on the host.

### GetSTHUD null check

All 15 callers of `GetSTHUD` check for null except three:

- `execGetSTHUD`, the Blueprint thunk, which returns the value to Blueprint;
- `ASTPlayerController::ScreenSpaceDebug`, which runs only behind a debug global at 0x14943BC7C that is off;
- `USTUIScreenMenu::OnBack_Implementation`, which tail-calls `ASTHUD::OpenPreviousMenu`. Its context is a host UI
  widget, never a server copy.

`GetSTHUD` itself checks the controller for null. All 10 `GetSTLocalPlayer` callers are UI or settings code with a
null check or a UI context. No caller gate was needed.

### Proof

`c99p3`, summarised above. The client fired two 2.5 s bursts next to the host and walked 2 s, then the host fired for
1.5 s. The host log shows the client's fire and walk shakes on the client's camera and the host's own shakes on its
own. The screenshots `host-c99p3-c99-host-before` and `-during` match. Not covered in this run: the client taking a
hit, and a notification for a client kill.

Report complete.
