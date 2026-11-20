# Card 065: Shooting-range targets show only on the host

Status: sent 2026-11-05 00:08 to junior ultimate-bug-war-coop-bb, after card 067 was accepted.

Read `docs/STATE.md` first, then the card 058 "Findings for the user" and the card 063 and 064 Reports. Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why

Card 058 found that the FED_C1M1 shooting-range targets show on the host's screen only. The client stands at a range with nothing to shoot, and the range objective can only be finished by the host. The same card saw the host's health drop to 80 at the range with no cause found. The targets themselves shoot (see below), so that drop is probably a target hitting the host, not a bug.

## Senior research (2026-11-04, static)

- The targets are `BP_KillableBuilding_Base_TargetRangeTarget_C` (instances named `..._TargetRangeTarger_C`, sic) and its child `BP_KillableBuilding_Base_TacSuppRangeTarget_C`, under `/Game/Blueprints/EnvironmentElements/Buildings/KillableBuildings/SpriteTargets/`. A third child, `BP_KillableBuilding_Base_GrenadeRangeTarget`, exists. All derive from `BP_KillableBuilding_Base`.
- The `TargetRangeTarget` name map has a component `ADLevelTriggerReactions`, bound to `OnComponentActivated` (`BndEvt__..._ADLevelTriggerReactions_K2Node_ComponentBoundEvent_4_ActorComponentActivatedSignature`). It also has `MovementTimeline`, `ShouldMove`, `Sprite`, `DestroyedSprites`, `NoCollision` and `SetSprite`. The target is most likely hidden or lowered until a level trigger activates that component, and the activation plays the movement timeline. That makes it a third case of a level-script event that runs on the host only, after objectives (card 049) and interactions (card 054).
- It also has `FireLaser`, `Shoots?`, `ShootPoint` and `BP_TargetLaserProjectile`, with the sound `WPN_AI_LaserTrainingRifle_Fire`. Some targets shoot back. That explains the host's health dropping to 80.
- Natives, all `ADLevelStreaming`-plugin code:
  - `UADLevelTriggerReactionsComponent::Trigger` is at 0x04A41A60. It walks the reaction array at component +0xC0 (count at +0xC8). For each live entry, it reads a flag (bit 7 of +0xD4) on something it looks up for that entry, and calls the entry through vtable +0x260 when the flag is clear.
  - `UADLevelTriggerStatefulReactionsComponent::Trigger` is at 0x04A41BB0.
  - `UADLevelTriggerComponent::Trigger` is at 0x01162D20. It is an empty `ret` and is shared, so never detour it.
  - `UADLevelTriggerComponent::execTrigger` is at 0x03801F90. It is the Blueprint thunk for `Trigger`: it steps `Stack.Code`, then does a tail `jmp *0x4E0(vtable)`, so `Trigger` is vtable slot 0x4E0. The linker folded this thunk: six other symbols share the address, among them `AActor::execOnRep_ReplicatedMovement`, `USTWeaponAbility::execOnAnimationEnded` and `USTWeaponSwitchAbility::execEndWeaponSwitch`.
  - Neither `Trigger` has a native E8 caller. It is reached only from Blueprint, through that thunk. A return address inside `Trigger` points into the script VM, so it does not say who fired it.
  - To name the calling Blueprint, use the card 014 `g_bp_node` pattern (`detour_exec_set_actor_collision`). Detour the shared thunk; when `is_a(context, UADLevelTriggerComponent)` is false, call the original and do nothing else. Otherwise save `FFrame::Node` (+0x10) and `FFrame::Object` (+0x18), call the original and restore them. The `Trigger` detours then log the saved node and its outer. Install this only with `UBWCOOP_C65_LOG=1`, because the thunk is shared.
- Unknown, and found at run time by step 1: what fires `Trigger` on the host (an overlap volume, the objective manager or the level script), whether it ever fires on the client, and whether the component the blueprint binds is a `UADLevelTriggerReactionsComponent` or a plain `UActorComponent` that the reactions call `Activate` on.

## Do

1. **Find the edge (log only, behind `UBWCOOP_C65_LOG=1`).**
   - Detour `UADLevelTriggerReactionsComponent::Trigger` and `UADLevelTriggerStatefulReactionsComponent::Trigger`. Log the role, the component's name and class, the owner actor's name and class, the reaction count, and the return address.
   - Also log `UActorComponent::Activate` calls whose owner class name contains `RangeTarget`. If no resolved RVA is at hand, `OnComponentActivated` reaching the blueprint is enough, logged through the existing ProcessEvent path.
   - Run a scenario with the runner where host and client both walk to the first shooting range and wait there for 20 s. Report which calls happen on which machine, in order.
2. **Relay, if step 1 shows the host-only edge.**
   - The host sends the triggering component's owner name and component name on the existing channel, as one new `ChanOp`.
   - The client finds the actor by name in its own world (the same lookup cards 054 and 056 use) and calls the same function on its own component.
   - Suppress the client's own local firing of that edge for range targets only if step 1 shows it can fire there and would then double up.
   - The first time a target activates late on a client that has joined, it needs a journal entry, as card 049 did for objectives. Add it only if a late join at the range would otherwise miss the targets. Report the choice either way.
3. **Damage stays host-side.** A target that the client shoots must lose health on the host, through the existing hit path (card 024). Check that the range objective counts the client's kills. A target laser that hits the client's pawn is handled by the existing damage replication. Report what the client's HUD health does.
4. **Proof scenario, `tools/scenarios/c65-range.txt`.** Both players walk to the first range. The client shoots at least three targets. Then the host and client take a shot each. It passes when the targets show on both screens, the client's kills count toward the objective, and the range completes.

## Not in this card

Other level-trigger-driven actors that may show the same fault (doors, lifts, scripted drops). List any that step 1 sees fire, as findings. Grenade range and tactical support range only if the first range proof works and they use the same component.

## Done when

- The proof scenario passes twice in a row, with no teleport and no `xdotool`.
- The client's shot shows the targets up. The host's shot shows the range done.
- `logsum` gives ERROR/FATAL 0, both games end with rc 0, and there is no new dump.
- With `UBWCOOP_C65_LOG` unset, the step 1 detours are not installed.
- fnlen reports no function over 60 lines.

## Report

### Result

The proof passes twice in a row: runs c65p3 and c65p4, with `UBWCOOP_C65_LOG` unset (host env `UBWCOOP_AUTODROPSHIP=1` only). Both games end rc 0. logsum gives ERROR/FATAL 0 and WARN 0 on both sides, and there are no new crash dumps. The scenario uses no teleport and no xdotool.

| Run | Host relays | Client activations | Kills (SO3) | Client CompleteObjective(ShootingRange_O1) | Client shunt refusals |
|---|---|---|---|---|---|
| c65p3 | 10 | 10 | 10 | replayed | 4 |
| c65p4 | 10 | 10 | 10 | replayed | 2 |

- The client kills C_1, C_2 and C_11 (kills 1 to 3, while only the client fires). The host kills the other six bobbing targets, then kills the bug nest with a frag grenade.
- Screenshots are in the prefix's `Saved/Screenshots/Windows/`:
  - `client-c65p4-wave1.png`: a target up in the client's pit, objective at 0/10.
  - `client-c65p4-end.png` and `host-c65p4-end.png`: the Shooting Range objective is struck through and "+10,000 Objective Complete" shows on both screens.
- The DLL in the game and `build/dwmapi.dll` both hash to `bbedb8e5e18f0373`.
- fnlen:
  - `hooks.cpp`: 1254 functions, 0 over 60 lines.
  - `cmdfile.cpp`: 17 functions, 0 over 60 lines.
  - `config.cpp`: 37 functions, 0 over 60 lines.
  - The longest function in `tools/drive.py` is 29 lines.
- With `UBWCOOP_C65_LOG` unset, `c65_install` is not called. The line `hooks: card 065 level-trigger log on (4 detours)` is absent from both games' logs in c65p3 and c65p4, and present in the step 1 run c65a.

### Step 1: which calls fire where

- The `Trigger` calls fire on both machines on their own: `BP_TutorialPromptTrigger_C` OnEnter and OnExit, and `BP_PlayerPresence_C` OnEnter. They need no relay.
- The target pop fires on the host only:
  1. `Activate(1)` on the target's `ADLevelTriggerReactions` component, from `LR_ActivateGeneric_C` through `execActivate`.
  2. Then `Activate(0)` on `MovementTimeline`.

  The client's own Blueprint never fires it (run c65f). This is the edge that card 065 relays.
- At load, both machines log `AbilitySystemComponent` `Activate(1)` once for each TargetRange target and each `TacSuppRangeTarget_C`.

**Findings: other level-trigger actors seen to fire**
- `BP_TutorialPromptTrigger_C` and `BP_PlayerPresence_C`. Both are local on each machine.
- `TacSuppRangeTarget_C`: \_3, \_6, \_7, \_9, \_21, \_23, \_24, \_26, \_27, \_28, \_31 and \_32. These are the tactical support range. The relay covers them if they use the same component, but they are not proven here.
- `BP_KillableBuilding_Base_GrenadeRangeTarget_C_1`: the bug nest. It is the range's 10th target, it activates when the host reaches about 4580 24500, and it is relayed and proven.

### Relay

- **Host.** `detour_c65_activate` sits on `UActorComponent::Activate` and is always installed. On the host, after the original runs, a component of the level-trigger class whose owner is a range target sends one new `ChanOp::Activate` frame to each client. The frame carries `"<owner> <component> <reset>"`.
- **Client.** `c65_receive_activate` finds the owner by name with `c64_find_actor`, then finds the component with `StaticFindObject` (the level-trigger class, outer = owner). It then calls the same `Activate` on it.
  - Normal line: `card065: client ... activated`.
  - A miss logs `WARN ... no such actor` or `WARN ... no such component`.
- **No suppression needed.** The client never fires the edge itself, so nothing doubles up.
- **Late-join journal: not added.** In the supported flow the client is in the session from map load, and every wave popped while it was there: 10 out of 10 in both runs. A client that joins after a wave has popped would miss that wave. Add a journal entry, like card 049's, if late join at the range matters.

### Damage and the client's HUD

- **Damage.** Damage stays on the host through the existing hit path. Each kill is a host `IncrementObjectiveCount(ShootingRange_SO3)`, relayed by card 049, and the client's three kills count.
- **Health.** The client's HUD health reads 100. A shunt from a moving target briefly takes it to about 97.
- **Ammo.** The client's ammo shows 60/0. It has no reserve and cannot reload. The scenario fires only while a target is up so that the client stays inside its 60 rounds. This is a follow-up; it is not in this card.
- **SHOOT marker.** The client shows no "SHOOT" marker over targets.

### Client crash found and fixed

- **Symptom.** Before the fix, the client crashed with `LowLevelFatalError GameplayAbilityTargetTypes.cpp:240` when a moving range target shunted its pawn. That produced four dumps: FEAF2BA9… (c65o), 64E71B14… (c65p), ECA917D2… (c65r) and 9B619DF3… (c65s).
- **Cause.** The client activated `GA_ShuntAbilityCharacter_C` itself, with gameplay event data, and serializing the target data asserts. The Wine stacks were unusable; a try-activate log found the cause.
- **Fix.** `c65_refuse_client_try`, called first in `detour_internal_try_activate`, refuses that one ability on the client when it comes with event data. It logs up to 8 lines per map. The host still runs its own pawn's shunt. The client's pawn is therefore not pushed by range targets; the push is cosmetic.

### The bug nest (10th target)

- **Damage.** It takes grenade damage only, and bobs between a look z of about -453 and 210.
- **Throw.** The host throws its one frag from the platform at about 4800 24555 z 270. It presses G while the nest rises: one look puts the nest's point (root + 60) at -100 to -299, and a second look 0.3 s later puts it at -99 to 99. The host then aims at the nest's top, `look point 5341 24015 207`.
- **Timing.** The projectile leaves about 1.1 s after the key, and the explosion comes about 1.2 s after that, near the top of the bob.
- **What failed before.** Aiming at 5250 23980 200 (yaw -51.7) missed in c65p2.

### Runner additions (`tools/drive.py`, `cmdfile`)

- `look actor <name> [dz]`: aims at the actor's z plus dz.
- `look point x y z`: aims at a point.
- `wait log ... count=N`: waits for N matching lines.
- `unless <role> log "<regex>" count=N <step>`: skips the step once N lines match.
- `when <role> last "<regex>" "<test>" <step>`: runs the step only when the role's last matching log line also matches the test.
- `MAX_STEPS` raised from 1000 to 2000. The scenario has about 1670 steps, generated per target: 40 aim-and-fire cycles for each target, each cycle gated by `unless` and `when`.

### Follow-ups

- Client ammo: reserve 0 and no reload.
- The client's own shunt is refused. If it should push the client's pawn, the shunt needs a host-driven path.
- Tactical support range targets: not proven.
- Late-join journal for range activations, if needed.

## Senior review (2026-11-05 02:38): accepted

Checked by the senior, not taken from the Report:

- `logs/c65p3.drive` and `c65p4.drive` each end `scenario passed`, and the two runs are consecutive (02:29 to 02:35). Neither has a teleport or xdotool line. Neither game log has the "level-trigger log on" line, so the step 1 detours were not installed.
- `logsum` on both runs: ERROR/FATAL 0 and WARN 0 on both games. The newest crash folder is from 01:38 (c65s), before the shunt fix; there is none from the proof runs.
- `build/dwmapi.dll` and the game's copy have the same hash, `bbedb8e5e18f0373`. fnlen on `hooks.cpp`: 1254 functions, 0 over 60 lines.
- Shots seen by the senior: `client-c65p4-client-kills.png` shows a target up on the client's screen with the objective at 3/10. `host-c65p4-end.png` shows "Shooting Range" complete.
- Shunt refusal (`c65_refuse_client_try` in `detour_internal_try_activate`): it runs in every client session, not only in test runs. It matches only `GA_ShuntAbilityCharacter_C` activations that carry event data. It ran in both proof runs ("client refused its own GA_ShuntAbilityCharacter_C"), and the four client dumps stopped after it was added. This is accepted as a crash fix. The cost is that the client's pawn is not shunted.

Follow-ups, each a candidate card:
- The client's ammo shows a magazine with 0 in reserve (48/0 in the shot), and the client cannot reload.
- The client's pawn is not shunted by moving targets. The fix would run the shunt on the host for the client's pawn, or replay it without the target data.
- The tactical support range is not proven.

Next: card 068.
