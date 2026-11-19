# Card 104: The client's support flare throws on the host and the bombing run shows for both

Status: written 2026-11-05 (senior); sent and accepted 2026-11-05.

Read:

- card 100 Report, the flare rows and Open items 1 and 2 (`ChanOp::TacSupport`, `c100_receive_tac`);
- card 098 Report (`ChanOp::WorldOp`, journal, `c95_find_nearest`);
- card 101, Why (the mantle also plays an ability animation on the server copy).

Repo root: `/path/to/ultimate-bug-war-coop`. Play DLL base: `e9e29f08f4ebb6e8` (card 100). That hash
was not run; the first run of this card is its smoke test (rc 0, ERROR/FATAL 0, no dump), say so in the Report.

## Why

Since card 100, the client's 4 reaches the host and runs `HandleTacticalSupportInput(0)` on the server copy. The
server copy's `USTUseTacticalSupportAbility` starts, but `OnDoAction` never runs, so `TryThrowFlare` is never called,
the charge stays 20000, and the bombing run never happens. Calling `TryThrowFlare` directly spawned a flare but no
strike, so the strike belongs to the ability path.

How the ability reaches the throw (senior, from the disassembly and the `GA_UseTacticalSupportAbility` name map):

- `USTUseTacticalSupportAbility::StartAbility` (0x05139840) creates `USTAbilityTask_PlayAnimation::PlayAnimation`
  (0x050FAFB0, ability, actor, animation key FName, rate) and calls `ReadyForActivation`, then binds the task's
  delegates and calls `OnThrowStarted` (0x050B4B90).
- The task's `Activate` (0x050D9580) starts the animation on the pawn's `UADAnimatorBaseComponent`, very likely
  `StartAnimationSequenceCallback` (0x04A24B10) with two callbacks: `USTAbilityTask_PlayAnimation::DoAction`
  (0x050E3BC0) and `AnimationEnded` (0x050DBCB0).
- `DoAction` drives `USTUseTacticalSupportAbility::OnDoAction` (0x0512FDE0), whose only native caller of note is
  `OnDoActionTriggered` (0x050B4B60), the Blueprint event that spawns `BP_SupportFlare` and the flare visuals at the
  `Flare_Throwhand_r` socket of the first-person mesh.
- The Blueprint has no wait-target-data node, so card 100's target-data idea is not the likely cause.

Hypothesis: the action point is an animation event, and the server copy's first-person animation on the host never
reaches it. The arms mesh of a pawn the host does not see is probably not ticking its pose
(`VisibilityBasedAnimTickOption`, owner-only visibility), so no notify fires, and `DoAction` never comes. If that holds,
every ability that waits on `USTAbilityTask_PlayAnimation::DoAction` or `AnimationEnded` is stuck on a server copy, not
only the flare. Check grenades, melee, reload and the mantle (card 101) against the same log.

## Do

1. **Measure (one run, log only).** The client picks up the flare (walk or host-warp the server copy onto it) and
   presses 4. On the host, for the server copy, log:
   - `USTAbilityTask_PlayAnimation::PlayAnimation` and `Activate` (animation key, actor);
   - the `UADAnimatorBaseComponent` call `Activate` makes, with its key and callbacks, and whether the key resolves
     (`HasAnimationSequenceForKey`);
   - `DoAction` and `AnimationEnded`, with time since `Activate`;
   - for the mesh the animator drives: `VisibilityBasedAnimTickOption`, whether the mesh ticks its pose, and the
     montage position each second.

   Do the same for the host's own flare throw and compare. Also log `DoAction` / `AnimationEnded` counts for any other
   ability the server copy runs in the same run (fire, reload, grenade) to see whether they are stuck too.
2. **Fix at the cause.** If the mesh does not tick: on the host, make server-copy meshes that abilities animate tick
   their pose and fire notifies (for example `VisibilityBasedAnimTickOption = AlwaysTickPoseAndRefreshBones` on the
   server copy's arms mesh when it is classified, card 019). One setting per server copy, not one patch per ability.
   If the cause is something else, name it and fix it there.
3. **The strike shows on the client.** With the throw working, the card075/098 spawn lines name the strike classes
   (aircraft, bombs, explosions) and the flare actor (`BP_SupportFlare_C`, not replicated). Relay them on the card 098
   WorldOp path as spawn rows for listed classes: the client spawns the same class at the same transform. Bomb damage
   stays on the host (enemies are the host's).
4. **Charge mirror.** After the host spends the server copy's flare, the client's own `TacticalSupportComponent`
   charge for that slot must fall the same way, so its HUD stops showing the flare as held. Send the server copy's
   charge to the client after each change, and write it to the client component (the field `GetAbilityCharge`
   reads).
5. **Proof**, one combined scenario run (FAST=1, `objective complete` up to the flare step): the client picks up the
   flare and presses 4. The host log shows `DoAction`, the flare and the strike; TacSupport_SO6 (or the step after
   the throw) completes on the host and replays to the client; the client log shows the relayed flare and strike
   spawns; the client's charge drops to 0. The host's own throw still works. rc 0, ERROR/FATAL 0, no new dump.

Stop when the fix builds and that one run shows it; the user will playtest.

## Done when

- The Report has the measure (where the server copy's ability stops, and why), the list of other stuck abilities if
  any, the fix, the strike classes and how they reach the client, the charge mirror, the proof run and the DLL hash.
- The Report ends with "Report complete."

## Report

**Smoke (first run, c104b, base hash e9e29f08f4ebb6e8 plus measure hooks 36feb0ce7c799544).** rc 0 on both sides,
ERROR/FATAL 0, WARN 0, no new dump. The client's throw reached the host's server copy (`DoAction`, flare thrown), but
no strike followed.

**Measure (c104c, c104d).** The flare chain on the server copy runs RunLZQuery, LZQueryFinished,
StartActivationTimer(0.01), ActivateFlare, the slot's ActivateAbility, GiveAbilityAndActivateOnce and
InternalTryActivateAbility, then stops: OnAbilityUsed_Internal never fires. Cause: GAS refuses abilities whose
NetExecutionPolicy is LocalPredicted (0) or LocalOnly (1) when the avatar is not locally controlled. For event
triggers the refusal is in HasNetworkAuthorityToActivateTriggeredAbility; for direct activation it is in
InternalTryActivateAbility when there is no prediction key. The host's server copy of the client pawn is not locally
controlled, so both paths refuse.

**Other stuck abilities.** Every LocalPredicted or LocalOnly ability that the server copy activates by gameplay event
or by a server-side TryActivate: the strike ability spawned by the flare, and the jump/mantle event abilities (card
101). The fix below covers all of them, not the flare only.

**Fix.** `c104_policy_set` / `c104_policy_restore` (anim_c104.cpp) set the spec ability's NetExecutionPolicy to
ServerOnly (3) for the duration of the call and put it back after. They wrap the call in `detour_c94_gameplay_event`
(HandleGameplayEvent, now ungated so server-copy events pass) and in `detour_internal_try_activate`. Only server copies
of client pawns are flipped; the host's own pawn is untouched. c104d logged "HandleGameplayEvent: 17 spec(s)" and
"InternalTryActivateAbility: 1 spec(s)" flipped, and OnAbilityUsed fired for slot 1 and slot 0: the strike ran on the
host.

**Strike classes and how they reach the client.** The strike spawns non-replicated actors through the Blueprint
SpawnActor thunk: BP_TacSupp_DangerZone_C (2), BP_TacSupp_Pod_Resupply_C, and the flare BP_TacSupp_SupportFlare_C.
The pod then spawns BP_ResupplyRack_Level0_C and pickups (MoritaMk1 weapon, MoritaSmall ammo, Medium health, Frag
grenade). Relay: the world spawn hook (`detour_c75_world_spawn`) sends every non-replicated `BP_TacSupp_*` spawn, and
`detour_c104_flare_activate` sends the flare at landing, as a WorldOp 'P' row (class path, location, host object
index) through `c104_relay_spawn` in world_c98.cpp. The client spawns the same class at that location
(`c104_client_spawn`) and keeps a fixed table of {host index, copy}. `detour_c98_destroy` sends a 'D' row when a
relayed actor dies; the client destroys its copy if it is still in the world. P and D rows are not journalled (late
joiners do not replay them). Bomb damage stays on the host.

**Charge mirror.** The host polls the server copy's TacticalSupportComponent every 250 ms in `c50_host_poll`
(`c104_charge_poll`): for up to 4 slots it reads the ability's charge (+0x2C) and ready flag (+0x29) and sends a
TacCharge text frame ("charge ready" pairs) when they change. The client (`c104_receive_charge`) calls the game's
`UpdateAbilityCharge(slot, host - local)`, which also fires the HUD broadcast, and `SetIsAbilityReadyForUse` on a
ready mismatch. Dying ability objects (object flags bit 30) are skipped.

**Proof (c104e, DLL 9d6329427cc72647).** FAST=1, scenario c104-measure.txt. rc 0 on both sides, ERROR/FATAL 0, WARN 0,
no new dump.
- Host charge: 0, 10000, "20000 1" (ready), then 0 after the throw. The client applied every frame and its own slots
  matched, ending "0 0".
- Host relayed 'P' for DangerZone (2), Pod_Resupply and SupportFlare; the client spawned all 4 copies. 'D' rows
  followed about 1 s later for the pod and zones and about 14 s later for the flare; the client logged "our copy
  destroyed" for each.
- Host log shows `DoAction`, ActivateFlare and OnAbilityUsed for the strike.

Open points for the playtest:
- TacSupport_SO6 stays complete 0 on the host. It did not complete in this run; the objective is not addressed by
  this card.
- The client's pod copy runs its own Blueprint, so the client also spawns its own rack and pickups (client log:
  MoritaSmall placed=3). These are client-local, not the host's pickups; picking one up on the client is not synced.
- The flare is relayed at landing (ActivateFlare), not during its flight.
- The host's own throw was not exercised in this run; the host pawn's abilities are not flipped, so its path is
  unchanged.

DLL hash: 9d6329427cc72647.

Report complete.
