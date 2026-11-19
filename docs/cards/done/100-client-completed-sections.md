# Card 100: Objective steps done by the client count, and only the host completes objectives

Status: written 2026-11-05 (senior); sent 2026-11-05; accepted partly 2026-11-05 (flare throw is card 104).

Read:

- `docs/STATE.md`, the sync rule;
- `docs/playtest-2026-11-05.md`, items 5 and 8 and the card queue (100);
- card 097 Report (native `CompleteObjective` / `IncrementObjectiveCount` hooks, host queue, `g_c49.depth`);
- card 102 (acting-player scope: overlaps, damage and interaction by the server copy count as "the player");
- cards 049 (objective mirror and replay), 058 (the TacSupport range completed with the host taking the items), 024
  (client fire relay), 050/057 (client ability and input relay).

Repo root: `/path/to/ultimate-bug-war-coop`. Play DLL base: `34eeb14d48563d99` (card 102).

## Why

User (2026-11-05): "What about sections completed by the client?" Every objective step must count when the client
does it, and the result must be the same for both players.

Today two things can go wrong:

1. **A client step does not complete on the host.** The host completes an objective only when its own copy of the
   world sees the step. The step can be a move-to volume, shooting targets, picking up and throwing the support flare,
   interacting, or destroying a bug hole. Some of these happen on the host through the server copy: the pawn moves
   there, and since card 024 its shots exist there. Others may not. In the playtest, the client's flare pickup left
   the range stuck (item 5), and card 058 finished that range only with the host taking the items.
2. **The client completes on its own.** The client's own level script (`FED_C1M1_Scripting_C`) runs and calls the
   objective functions locally. Nothing stops a client-local completion that the host never made. The client then
   shows a step done that the host has not done, and its script may run follow-ups (doors, spawns) on the client
   only.

## Do

1. **Measure (one run, log only).** The client, not the host, does each objective step of the FED_C1M1 tutorial and
   the first LOS bug hole: move-to volumes, shooting range targets, flare pickup, throw and bombing run, any
   interact steps, and destroying a bug hole. Use `objective complete` only to skip steps not under test. For each
   step, log:
   - whether the host completes it (the card 097 `card097: host ... queued; caller` line, with the caller);
   - whether the client completed it first on its own (a `card097: client` line outside a host replay; see step 3
     for why `g_c49.depth` cannot tell);
   - for a step the host misses, what the host lacks. For example: the overlap does not fire for the server copy,
     the interact or pickup input never reaches the server copy, or the item is granted on the wrong side.

   - the bombing run: the card075/098 spawn lines name the strike classes (card 098 could not reach the strike in a
     scripted run). Relay the strike as a spawn-of-listed-class row on the card 098 WorldOp path, so it shows on the
     client.
   - the bug hole destroyed by the client's satchel: the host's card 098 WorldOp must reach the client once. If the
     client's own copy also exploded locally, the relayed `ReceiveOnSatchelExploded` must not run a second time on it
     (skip when that building has already exploded on the client).

   The client's `warp` is undone by the server (card 098). Move the client by walking, or have the host warp the server
   copy.

   - card 102's damage scope was not exercised (no client damage in its runs): the shooting range row must show the
     client's shot pressing the button on the host under the `ApplyDamageFromImpactHitResult` scope. If the button's
     `OnDamageTaken` is not reached through that function, move the damage scope to the call that wraps it.
   - card 102's three items not shown: a client move-to volume step ending in `CompleteObjective`, and the host doing
     the same steps, both go in this table.

   Give one table row per step. Card 102 already makes overlaps, shots and interactions by the server copy match
   "the player" checks (`GetSTPlayerCharacter`); a step still missed after it has another cause, so name it.
2. **Fix the steps the host misses.** Each client action that an objective depends on must run on the host for the
   server copy, through the existing relays where one fits (024 fire, 050/057 abilities and input). For interact and
   pickup, the server copy does it on the host, and the result reaches the client by the card 098 world mechanism and
   the card 097 objective path. The flare (item 5): the client picks it up, and the client can then throw it with 4,
   and the bombing run happens on the host and shows for both.
   Flare chain, from the symbols and card 054 (check each link in the measure run):
   - The client presses E on `BP_TacSuppPickup_BombingRun_Tutorial_C`. Card 054 relays the interact. On the host,
     `ASTTacticalSupportPickup::OnInteraction` (0x051D05A0) takes the pawn from interactor +0xA0, keeps it in a weak
     pointer and calls `AADPickupBase::Pickup`. The grant is Blueprint and lands in
     `USTTacticalSupportComponent::EquipTacticalSupportAbility` (0x050E69E0, already detoured for logging by card 032
     in vehicles.cpp). So the host gives the flare to the server copy's component. Card 054 also found that the client's
     own trace can find the target, so the client may run `OnInteraction` on its own copy too (a double grant).
   - The client presses 4. Locally that is `USTTacticalSupportComponent::HandleTacticalSupportInput` (0x050EDD50),
     then `TryThrowFlare` (0x0510A5B0, detoured in hooks_ubw.cpp), then `USTTacticalSupportSlot::DeployFlare`
     (0x050E3220) and `USTUseTacticalSupportAbility`. Nothing relays this input to the server copy today. Find out
     whether the client's local component has the flare at all (`GetFlareFromSlot` 0x050EA030,
     `IsAbilityReadyForUse` 0x050F01B0), and whether 4 reaches `HandleTacticalSupportInput` on the client.
   - The fix direction: the server copy owns the flare. The client's 4 is relayed to
     `HandleTacticalSupportInput(slot)` on the server copy, and the host's throw and strike reach the client through
     the card 098 mechanism. The client's own copy must not keep a second, usable flare. The client's HUD still needs
     to show that it holds the flare (charge from `GetAbilityCharge` 0x050E8CB0); say in the Report how that is
     shown.

3. **Only the host completes objectives.** On the client, a native `CompleteObjective` or `IncrementObjectiveCount`
   that does not come from a host replay must not change objective state. Skip it, and log it once per objective.

   Do not test `g_c49.depth` for this. On the client, `c49_receive` (objectives.cpp ~1099) replays through the
   `g_orig_c49_*` trampolines directly (`kC49Replay[k](pc, c)`, ~1118), so `g_c49.depth` stays 0 during a replay. It is
   above 0 only when the client's own level script calls a detoured library function. The c97-tac2 client log shows
   this: replayed completions log `depth 0, library depth 0`. Add a client replay counter instead:
   - raise it around the `kC49Replay[k](pc, c)` call in `c49_receive`;
   - the set-count replay that calls `g_orig_c49_increment` (~637) runs inside that call, so it is covered;
   - check that no other client path completes objectives (card 053 snapshot rebuild).

   Gate at the card 097 native hooks, since every path goes through them: on the client, with the counter at 0, skip
   the original. Native cascades inside a replay (parent completes from its last sub) run with the counter above 0, so
   they still apply.

   Evidence that this is needed: the playtest client log (`client/playtest-menu-20261105-200846.log`) has the
   client's own script calling `CompleteSubObjective(Assault_O1, Assault_SO1)` with ctx `FED_C1M1_Scripting_C`, caller
   exe+0x5082232, plus its own `AddObjective` / `AddSubObjective` and about 230 `SetObjectiveVisibility` calls. The
   audit cap cut the rest. Leave Add and visibility alone in this card (card 049 `c49_skip_add` already handles
   duplicate Adds); only completions and counts are gated.

   Check that the client's script still gets its `OnLevelObjectiveComplete` events from the replayed completions,
   so its follow-ups run in the host's order. Check also that nothing in the client's UI or script breaks when its
   local call is skipped (return values, a script waiting for its own call).
4. **Proof**, one combined scenario run (FAST=1): the client does a move-to step, a shooting range step, the flare
   pickup with throw and bombing run, and destroys a bug hole (use `objective complete` to reach the second mission).
   Each completes on the host and reaches the client through the mirror. The client log shows its local completions
   skipped. rc 0, ERROR/FATAL 0, no new dump.

Small item from card 102: `c99_count` (card 099 per-caller tally) is not thread-safe. Skip the tally when not on the
game thread (`IsInGameThread`), so a render-thread caller of `GetPlayerController` cannot corrupt it.

Stop when the fix builds and that one run shows it; the user will playtest.

## Done when

- The Report has the per-step table (step, host completes, client-local completion, cause of any miss), the fixes,
  the client-local gate, the proof run and the DLL hash.
- The Report ends with "Report complete."

## Report

Status: partly done. The client-local gate and the flare crash fix are in. The bombing run is not done yet: the host
gets the client's 4, but the server copy does not throw. The bug hole and satchel rows were not measured. Handed off
early, per the user's "hand off quickly" rule.

DLL `e9e29f08f4ebb6e8`. Its behaviour matches the c100g build (`efe1a6ef3b1b4a88`). The only differences: the charge
is logged after the call, and the c100h/c100i diagnostic fallback was removed. This exact hash has not been run.

### Per-step table (client does the step; runs c100a/c100c/c100g/c100h/c100i, FAST=1)

| Step | Host completes? (card097 host line, caller) | Client-local completion first? | Cause of a miss |
|---|---|---|---|
| Assault_SO1 move-to (`BP_PlayerPresence_C_2`, client walks) | yes: `native CompleteObjective Assault_O1/Assault_SO1`, caller exe+0x5233805, under the card 102 overlap scope (`BP_ObjectiveMarkerActor_C` remap) | yes in c100a/c100c: the client's script also completed it. It is now skipped: `card100: client ... skipped local CompleteObjective Assault_SO1 ... caller exe+0x5233805`. The host replay applied it 15 ms later | none |
| ShootingRange_SO0 (`BP_PlayerPresence_C_24`) | no, when the host warps the server copy onto the volume | no | a teleport (`SetActorLocation`) does not start the overlap. Untested with a walk: the client's goto there got stuck at 7204 uu (c100a) |
| ShootingRange_SO1 (Morita `_0`, client E) | yes: `ShootingRange_O1/ShootingRange_SO1`, caller exe+0x5233805, under the card 102 interaction scope (card 054 relayed Use #1) | no | none |
| ShootingRange_SO3 (range targets) | not rerun. Card 065 showed that client shots raise the count on the host | n/a | FED_C1M1 has no `BP_Activatable_ShootableButton_C` (`goto` reports "no such actor"). The range targets are `BP_KillableBuilding_Base_TargetRangeTarget_C` (40 placed). So no run has exercised the card 102 damage scope. Statically, `ApplyDamageFromImpactHitResult` (0x522D770) calls `ApplyDamageToActor` and `USTCombatUtilityLibrary::ApplyDamage`, and those broadcast `OnDamageTaken` synchronously. So the scope already wraps it, and it was not moved |
| TacSupport_SO0 (host does the step: host pawn warped to `BP_PlayerPresence_C_1`) | no | no | the same teleport-without-overlap as ShootingRange_SO0. This was the host's own pawn, so the warp is the cause, not the client |
| TacSupport_SO2 (Morita `_1`, client E) | yes: `TacSupport_01/TacSupport_SO2`, caller exe+0x5233805 (relayed Use #2) | no | none |
| TacSupport_SO3/SO4/SO41/SO5 | skipped with `objective complete` | n/a | n/a |
| Flare pickup (`BP_TacSuppPickup_BombingRun_Tutorial_C`) | the pickup is a collision pickup, not an interact: the client's E found no target (`interactor target null`). The server copy picks it up when warped onto it (dist 10): the server copy's charge for slot 0 is 20000 | the client's own copy also picks it up locally: its own charge for slot 0 is 20000 | a double grant, see the fixes |
| Flare throw (client 4) | **open.** The relayed `HandleTacticalSupportInput(0)` runs on the server copy with charge 20000 -> 20000. No flare is spawned, and TacSupport_SO6 does not complete | before this card the client's local ability ran and crashed the client (below). Now the client blocks it | the server copy's `USTUseTacticalSupportAbility` starts but never reaches `OnDoAction`, the only caller of `TryThrowFlare` apart from exec. Likely cause: on the server, an ability for a pawn it does not control locally waits for that client's target data (`STGameplayAbilityTargetData_TacticalSupportInput`). The client cannot send that data (no NetSerialize), so the host must supply it itself. Diagnostic run c100h/c100i: calling `TryThrowFlare(0)` straight on the host spawns `BP_SupportFlare_C` (not replicated). It lands, and 18 s later it is destroyed with no strike spawned and no step completed. So the strike is owned by the ability, not by the flare actor. That fallback was removed |
| Bombing-run strike classes | not reached | n/a | follows from the throw row |
| Bug hole (satchel) and the double `ReceiveOnSatchelExploded` | not measured | n/a | open |

### Fixes

1. **Client flare crash (new finding).** On the client, pressing 4 with the flare crashed with `LowLevelFatalError
   GameplayAbilityTargetTypes.cpp Line 240: FGameplayAbilityTargetDataHandle::NetSerialize called on data struct
   STGameplayAbilityTargetData_TacticalSupportInput without a native NetSerialize` (c100d and c100f dumps). The stack
   runs from the enhanced-input binding (exe+0x2329963) into `HandleTacticalSupportInput` (+0x6A), then into
   `SendGameplayEventToActor`, `HandleGameplayEvent` and `ServerTryActivateAbilityWithEventData`. So
   `AbilityInputTagPressed` is not on this path. Gating the `InputTag.UseTacticalSupport.Slot1` tag (c100f) did not
   prevent the crash.
   - The fix is a new detour on `USTTacticalSupportComponent::HandleTacticalSupportInput` (0x050EDD50,
     `vehicles.cpp`). On the client, for its own pawn, the original never runs. The slot goes to the host on a new
     channel op, `ChanOp::TacSupport` (1 argument).
   - On the host, `c100_receive_tac` runs the original on the server copy's `TacticalSupportComponent`, and logs the
     charge before and after.
   - c100g, c100h and c100i: the client sent slot 0, the host ran it, and there was no crash and no new dump.
2. **Second usable flare on the client.** The client's own component keeps its charge of 20000, but it cannot use it:
   every slot input goes to the host instead. **HUD:** the client's HUD reads its own component's charge
   (`GetAbilityCharge`), which shows 20000 after the pickup, so the slot shows as held. The `card100` client line logs
   that local charge on every press. The client's charge does not fall when the host spends the server copy's flare.
   That needs a charge mirror once the throw works.

### Client-local gate (step 3)

- In `objectives.cpp`, `g_c49.replay` is raised around `kC49Replay[k](pc, c)` in `c49_receive`.
- `detour_c97_complete` and `detour_c97_increment` call `c100_skip_local`. On the client with the counter at 0, the
  original is skipped, logged once per objective id with the caller.
- A completion that is already done still passes, so a replayed duplicate cannot break anything. The card 053 rebuild
  writes the map directly and is not gated.
- The client's script still receives `OnLevelObjectiveComplete` from the replays. Each replayed completion logs
  `subsystem ObjectiveComplete(<id>)` (caller exe+0x50787C5) on the client, and the follow-up visibility for the next
  steps arrived in the host's order. Nothing broke when the local call was skipped, across c100a to c100i.

### Small item

- `c99_count` now runs only when `IsInGameThread()` is true (`player_c99.cpp`). The function is new in the hook
  table and in `tools/symbols.txt`.

### Runs

- c100g (`efe1a6ef3b1b4a88`): drive rc 0, ERROR/FATAL 0, no new dump. Move-to, range pickup and TacSupport pickup all
  completed on the host and were replayed on the client. The flare input was relayed, with no throw.
- c100h and c100i (diagnostic `TryThrowFlare` fallback, since removed): rc 0, no dump. The flare spawned on the host
  only, with no strike.
- c100d and c100f: the client crash described in fix 1, before the fix.

### Open (next card, or the playtest)

1. The server copy's tactical support ability must throw. Either supply the target data on the host (find where
   `USTUseTacticalSupportAbility::StartAbility` waits), or end the wait the way the host's own pawn does. Then name
   the strike classes and relay them over the card 098 WorldOp path. `BP_SupportFlare_C` does not replicate, so the
   client needs it relayed too.
2. Mirror the slot charge to the client after a host throw.
3. Bug hole and satchel double-explode rows.
4. ShootingRange_SO0 and TacSupport_SO0 with a walk instead of a teleport.

Report complete.
