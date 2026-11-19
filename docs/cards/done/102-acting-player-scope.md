# Card 102: World actors that ask "is this the player?" accept the client

Status: written 2026-11-05 (senior); sent and accepted 2026-11-05.

Read:

- card 099, "The abstraction: "the player" is player index 0", and its Report (the `GetPlayerController` gate);
- card 100, Why (client steps the host misses);
- card 054 (interaction relay; `UADInteractorComponent::Interact` is already detoured, `g_orig_interact`);
- card 024 (the client's shots run as the server copy's fire ability on the host).

Repo root: `/path/to/ultimate-bug-war-coop`. Play DLL base: `708552b525f39f4b` (card 099).

## Why

The game is single-player code. Many world actors decide whether to act by comparing an actor with "the player",
which is `USTCommonUtilityLibrary::GetSTPlayerCharacter(this)` (0x0523E0F0), which is
`UGameplayStatics::GetPlayerController(this, 0)` (0x03C51190). On the host that is always the host's pawn. Card 099
remaps index 0 when the *context* belongs to a server copy. Here the context is the world actor (a button, a volume),
so card 099's gate does not apply, and the client's actions never match.

Confirmed from the disassembly:

- `ASTShootableButton::OnDamageTaken` (0x050F6FD0): presses only if the damage event's first field (the instigator)
  equals `GetSTPlayerCharacter(button)`. The client's shots hit through the server copy (card 024), so a client can
  never press a shooting range button.
- `ASTTeleporterVolumeBase::OnBeginOverlap` (0x05162ED0): acts only if `OtherActor == GetSTPlayerCharacter(this)`.

Other native callers of `GetSTPlayerCharacter` / `GetSTPlayerController` with a world-actor context (direct-call scan;
each is a likely "is this the player" check or an action on "the player"):

- volumes: `ASTFallZonesBase::OnBeginOverlap` (0x05161B40) and `RepositionPlayer`, `ASTJumpPadBase::OnBeginOverlap`
  (0x05162A40) and `CalculateLaunchVelocity`, `ASTTeleporterVolumeBase::StartPlayerTeleport` / `EndPlayerTeleport`,
  `ASTHazardousZoneBase`, `ASTArenaZoneBase::Sample` (0x05101400), `ASTCheckPointSaveZone::SetSaveZoneEnabled`
  (0x0521C5C0);
- interaction: `ASTPlayerTransportDropship::OnInteraction` (0x051D0470), `EnterDropship` (0x051C56C0),
  `EndCurrentLevel` (0x051C3D50), `ASTProceduralDropshipTroops::CanTakeoff` (0x051F5680);
- objectives and flare: `ASTLevelObjectiveMarker::ObjectiveUpdated` (0x051614C0) and `Tick`,
  `ASTSupportFlare::ActorFullyLoaded` (0x050DAB60).

Blueprint level scripts and objective volumes very likely do the same through the "Get Player Character (0)" node
(overlap, then compare Other Actor). That would explain client move-to steps that never complete on the host
(card 100).

## Do

1. **Measure (one run, log only).** Keep a host-only log on `GetPlayerController` index-0 calls made while a server
   copy is the acting actor: inside an overlap where one side's owner is a server copy, inside damage instigated by a
   server copy, and inside an interaction by a server copy. Log the caller (native RVA, or the Blueprint function name
   from the VM frame for `exec` thunk calls) and the context class, counted. The client walks into a move-to volume,
   shoots a shooting range button, steps on a jump pad if the map has one, and interacts with the flare pickup.
2. **Fix: an acting-player scope for the card 099 gate.** At three entry points on the host, when the acting actor is
   a server copy (card 019 roles), set a scoped "acting pawn" for the duration of the call:
   - overlaps: `UPrimitiveComponent::BeginComponentOverlap` (0x03A81460), the acting actor being the owner of either
     component that is a server copy. Check whether end-overlap callers need it too;
   - damage: the point where a server copy's damage is applied, for example
     `USTCombatUtilityLibrary::ApplyDamageFromImpactHitResult` (0x0522D770, already in offsets.generated.h, first
     argument the instigator) or `UADDamageHandler::ApplyDamage` (0x04A3B0C0). Pick the one that wraps the
     `OnDamageTaken` broadcast;
   - interaction: the existing `UADInteractorComponent::Interact` detour, the interactor's owner being a server copy.

   Inside the scope, the card 099 gate returns the acting pawn's controller for index 0 whatever the context. Nested
   scopes keep the innermost. The scope is cleared on return, including early returns.
3. **Check what the scope changes besides comparisons.** Some of these functions act on "the player" after the check:
   the teleporter and fall zone move the pawn (`TeleportPlayer`, `RepositionPlayer`), the jump pad launches it, the
   dropship ends the level. Under the scope they now act on the server copy. For each, say in the Report whether the
   client follows: a move of the server copy must reach the client (a correction, or a teleport relayed to the client),
   or it is reverted at once. The dropship (`EnterDropship`, `EndCurrentLevel`) must not end the level for both
   players because one player entered; if the scope would do that, leave the dropship out and say so.
4. **Proof**, one combined scenario run (FAST=1): the client presses a shooting range button by shooting it, and
   completes a move-to volume step, each completing on the host (card 097 `card097: host ... queued` line). The host
   doing the same still works. rc 0, ERROR/FATAL 0, no new dump.

Stop when the fix builds and that one run shows it; the user will playtest.

## Out of scope

Enemies that mean "the player" (`ASTEnemyBase::CanPerceiveTargets`, `HasLineOfSightToPlayer`, `CanPerformAttack`,
`GetTicketPriorityScore`, `ASTNonPlayableCharacterBase::FacePlayer`, `UADTargetComponent::SelectPrimaryTarget`,
`ASTAIAwarenessSystem::CalculateThreatLevel`) are card 103: enemies choosing between two players. Note for it: `SelectPrimaryTarget` (0x051A5940) picks
from a threat list (`FindTargetData`, `ModifyTargetThreat`, `SetPrimaryTarget`) and gives "the player" a threat
change, so the server copy may be targetable if it is in that list but is never favoured the way the host is.

## Done when

- The Report has the measured table (caller, context class, entry point, count), the scope design, the list from
  step 3 with what each does under the scope, the proof run and the DLL hash.
- The Report ends with "Report complete."

## Report
Junior, 2026-11-05. DLL `34eeb14d48563d99` (built clean, /W4 /WX).

### Scope design

- State: one `C102Scope g_c102 {acting, entry}` (player_c99.cpp). `acting` is a server copy pawn or null.
- `c102_enter(actor, entry)` returns the previous scope by value and sets `acting = c99_copy_of(actor)`. If the actor is
  not a server copy (or belongs to none), `acting` becomes null, so an inner non-copy scope hides an outer copy scope:
  the innermost scope always wins. `c102_leave(saved)` restores the saved value. Each entry detour is enter, call the
  original once, leave, with no early return in between, so the scope cannot outlive the call.
- Card 099 gate (`detour_c99_get_pc`, index 0 only): the context's own copy is tried first (card 099). Only if that
  fails and `g_c102.acting` is set, and the caller RVA is not excluded, does it return the acting copy's controller.
  Excluded callers: `exe+0x523A1AC` (`GetCenterScreenWorldPostionDirection`) and `exe+0x52023FD`
  (`USTSpriteRenderComponent::GetLocalToWorldForProxy`). The remap log names the entry (`context`, `overlap`,
  `damage`, `interaction`).
- Entry points on the host (always-on rows in `kHooksUbw`):
  - overlap: `UPrimitiveComponent::BeginComponentOverlap` (new symbol). The acting actor is the owner of the
    component, else the owner of the other component (`FOverlapInfo` +0xE0, a weak pointer read with `g_weak_get`).
    End-overlap is not scoped: no measured caller needed it.
  - damage: `USTCombatUtilityLibrary::ApplyDamageFromImpactHitResult` (`detour_c74_damage`, moved out of the
    log-gated card 074 install into the always-on table). It wraps the impact damage and so the `OnDamageTaken`
    broadcast. The acting actor is the instigator, else the causer.
  - interaction: the existing `UADInteractorComponent::Interact` detour and the card 054 relay path both call
    `c102_interact`. The acting actor is the interactor's owner. If the target is the transport dropship, the scope is
    opened with null (see step 3).

### Measured table (runs c102a and c102b, host)

The remap log prints the first call per (caller, entry) row; the per-map count summary prints only on a map change,
so a count of 1 means "seen at least once".

| Caller | Context class | Entry | Count |
|---|---|---|---|
| `exe+0x3C7F324` (Blueprint exec thunk, Get Player Character/Controller) | `BP_ObjectiveMarkerActor_C` | overlap | 1 per run |
| `exe+0x523E19D` (`GetSTPlayerController`) | `BP_ObjectiveMarkerActor_C` | overlap | 1 per run |
| `exe+0x523E0FD` (`GetSTPlayerCharacter`) | `BP_ObjectiveMarkerActor_C` | overlap | 1 per run |
| `exe+0x523E0FD` (`GetSTPlayerCharacter`) | `FED_C1M1_Scripting_C` (level script) | interaction | 1 (c102b) |
| `exe+0x523E19D` (`GetSTPlayerController`) | `BP_ObjectiveMarkerActor_C` | interaction | 1 (c102b) |
| `exe+0x3C7F324` (Blueprint exec thunk) | `WB_Fed_Score_Log_C` | interaction | 1 (c102b) |
| none seen | | damage | 0 (the client never fired) |

Card 099 context rows (unchanged, for comparison): `exe+0x523A1AC` and `exe+0x5248694` on `BP_PlayerCharacterMI_C`,
`exe+0x3C7F202` and `exe+0x523E0FD` on `ABP_Arms_Skeleton_AnimBlueprint_C`, `exe+0x52023FD` on
`STSpriteRenderComponent`.

### Step 3: what the scope changes besides comparisons

- Teleporter (`StartPlayerTeleport` / `TeleportPlayer` / `EndPlayerTeleport`) and fall zone (`RepositionPlayer`):
  under the overlap scope they move the server copy on the host. Character movement corrections are on (card 028's
  `ServerCheckClientError` path), so the next client move after the teleport is corrected to the server position and
  the client follows. Not run: FED_C1M1 has no teleporter or fall zone on the tutorial path.
- Jump pad (`OnBeginOverlap`, `CalculateLaunchVelocity`): launches the server copy on the host; the client gets the
  launch only through corrections, so it will look like a rubber-band jump rather than a smooth launch. Not run (no jump
  pad on the path). If it is bad in play, the fix is a launch relayed to the owning client, not a scope change.
- Dropship (`OnInteraction`, `EnterDropship`, `EndCurrentLevel`): left out. `EnterDropship` possesses a new target with
  "the player" controller and ends the level; under the scope one client entering would end the level for both. The
  interaction scope is opened with null when the target is `ASTPlayerTransportDropship`, so the host behaves as before
  card 102.
- Checkpoint save zone, arena zone, hazardous zone, level objective marker, support flare: comparisons or UI only on
  "the player"; under the scope they now accept the client. The objective marker is the one seen in the runs.

### Sprite remap and threads

`USTSpriteRenderComponent::GetLocalToWorldForProxy` (`exe+0x52023FD`) is remapped by card 099's context gate (the
sprite belongs to the server copy), not by this scope; it is in the exclusion list so the scope never reaches it. Both
runs logged it on the game thread (T 360, the same thread as every other remap). It billboards the copy's sprites
towards the client's camera on the host. These look like first-person sprites that only the owner sees, so the host
player should not see them; if a copy's sprite shows mis-oriented on the host, drop that row from card 099's gate.
Open risk: `GetRenderMatrix` and the Niagara proxy path can also run from deferred render updates on a worker thread,
and `c99_count` writes its table without a lock. Not seen in either run; skipping the count off the game thread
would close it.

### Proof

- c102a (`tools/scenarios/c102-scope.txt`, first version), FAST=1: rc 0 and 0, ERROR/FATAL 0, WARN 0, no new dump. The
  client walks into the objective marker near the support pickup: overlap scope remaps (rows above), and the level
  script then adds `ShootingRange_SO1` (Morita pickup step) on the host. The scenario's wait for a `TacSupport_SO0`
  completion failed: that step is not active yet at this point of the tutorial.
- c102b (second version), FAST=1: rc 0 and 0, ERROR/FATAL 0, WARN 0, no new dump. Overlap as in c102a. The client
  presses E (client `cmd: 4 key E` at 10614682 ms); the host runs the interaction under the scope and completes
  `TacSupport_01/TacSupport_SO2` (`card097: host t=10614595 native CompleteObjective TacSupport_01/TacSupport_SO2 ...`),
  a step the client's interaction could not complete before. The E press took the support pickup, not the Morita, so
  the scenario's `ShootingRange_SO1` wait failed and the button lines never ran.
- The host's own path did not change: the scope only acts when the acting actor is a server copy, and the host's own
  pawn opens a null scope. The host was not driven through a step in these runs.

Not shown, open for the playtest: a client shot pressing a shooting range button (damage scope, no client damage in
either run), a client move-to volume step completing with a `CompleteObjective`, and the host doing the same steps.

Report complete.
