# 299 The bug hole blown open with the mech still looks closed on the client

Playtest 2026-11-13 (user plays as client, FED_C2M1): "the objective to blow open bug hole with mech i can still
see the hole closed but can walk through it".

The client has the open state for collision and the closed state for the picture.

## Trace (client log `UBWCoop-menu-20261113-184603.log`)

| Line / client t (ms) | Row |
|---|---|
| 257361886 | `native CompleteObjective LOS_O05/LOS_O05_SO02`, host row `Objective c5de7d3f kind 8` |
| 257382412, 257387232 | audio rows `MSS_MECH_FLY_Razorback_SelfDestruct_Countdown`, `_Eject`, `_Buildup` from `BP_BaseMech_C` at (171135, -19608, 6268) then (173951, -21760, 6270) |
| 257390166 | `card098: client destroy BP_BaseMech_C` at (173951, -21760, 6268); gib rows for a WarriorBug and a trooper next to it; `host pawn Up -> Downed` |
| 257395625 | `world K BP_DestructibleSpawner_Bugnest_HumanGameplay_C_5 177000 -23329 6121`: `Health base 0 and OnDeath broadcast, IsDead now 1` |
| 257451140 | client's own level script (`FED_C2M1_Scripting_C`, chain `0x5082232 < 0x154FAE8 < 0x1545188 < 0x1557ADB`) calls `CompleteSubObjective(LOS_O05, LOS_O05_SO03)`: skipped locally (card 100), then replayed from the host row at 257451204; `LOS_O05_SO04` added with `ActorReference_C_9` |

Between 257387000 and 257452000 the client log has no `card098`, `card137`, `card163` or `card145` row for any
actor at the mech's explosion point other than the mech, its weapon and the gibbed pawns: no hide, no destroy, no
collision change, no mesh swap for a hole cover. The digest rounds 77 to 79 (18 mismatches each, classes
STAISpawner, BP_AISpawner_C, BP_SatchelBuilding_Flakbug_C, BP_SatchelBuilding_Plasmabug_C, STLevelObjective) list
no hole actor either, so the hole's closed and open state is not in the digest.

## Cause (static; candidate, to confirm in step 1)

The hole cover is opened by a game path that the mod does not relay and the digest does not cover. The client's
own copy of the level script ran part of it (it reached `CompleteSubObjective` at 257451140 on its own), which
fits collision going off locally, while the visible part (mesh hide or swap, a destruction or geometry
collection, a timeline or a spawned effect started by the mech's explosion damage on the host) only ran on the
host.

## Step 1 (static, junior, no run; host log after the playtest)

1. Name the actor: class and instance of the hole cover for `LOS_O05_SO02` and `LOS_O05_SO03` in FED_C2M1 (the
   actor behind the objective's `ActorReference`, near (173951, -21760, 6270)), its components, and the function
   that opens it (damage event from the mech's self-destruct, overlap, level script call).
2. List what that function changes: collision, visibility, mesh, destruction, timeline, effect, nav. For each,
   say which mod row class carries it today (WorldOp K, S, L, CollisionOp, card 137 change, Fx) and which has no
   row.
3. Say why the client's collision opened without a row: the client's level script, a replicated property, or a
   mod row the log caps.
4. From the host log: the rows the host sent for that actor between host t=83513000 and 83582000 (client t minus
   173869236).

Write the answers into this card before coding.

## Fix

The hole cover's open state reaches the client as one whole state through the mechanism that owns that class of
actor (card 098 world rows): the client runs the game's own open function for it with the host's result, so
mesh, collision, effect and nav change together. The join and reconnect replay (journal) carries it too. The
actor's open flag enters the card 132 digest.

- Rows: `card299: <role> t=<ms> hole <actor> opened by <cause>: collision <before> -> <after>, visible <before>
  -> <after>, mesh <name>`, `WARN card299: client hole <actor> collision open, still visible`.

## Sibling sweep (same class: a level actor opened or destroyed by damage or by the level script, with a visible
part and a collision part)

| Item | Check |
|---|---|
| Every bug hole and tunnel cover on every FED map | static list by class; each in the digest |
| Rock walls, gates and barricades opened by explosives, satchels or script | same |
| Doors opened by the level script (card 288 CTF door) | same mechanism |
| Destroyable props (`BP_DestroyableProp_*`) | picture and collision change together on the client |
| Nests (`BP_DestructibleSpawner_Bugnest_*`): `Health base 0 and OnDeath broadcast` path | the closed mesh is replaced on the client |
| Join in progress and reconnect after the hole is open | open on arrival |
| Digest | 18 mismatches per round in this map (spawner locations, Flakbug and Plasmabug `dead host 0 client 1`, 8 STLevelObjective presence): each explained or fixed in the batch |

## Predictions (one run, after the playtest, batched with 297 and 298)

- Host blows the hole with the mech's self-destruct: client log has one `card299` row with collision and visible
  both changed, zero `WARN card299`; client screenshot aimed at the hole (teleport and look, per the gameplay test
  rules) shows it open; the client walks through.
- Client reconnects after: hole open on arrival, same row from the journal.
- Digest round after: 0 mismatches, the hole actor counted.

## Step 1 answers (junior, static, 2026-11-13)

1. **Actor.** Not proven from static data alone; two candidates, ranked.
   - **(a) `BP_DestructibleSpawner_Bugnest_HumanGameplay_C_5`** at (177000, -23329, 6121), 3.5k uu from the mech's
     self-destruct point, the only placed actor near the blast whose death reached the client (the `world K` row at
     257395625, 5 s after the blast). The nest is the "bug hole" the mech can blow open: no level-script reference
     (no `Bugnest_HumanGameplay_C_5` in `FED_C2M1_Scripting`), it dies by damage. Components: `StaticMesh`,
     `STMeshUpdate`, `HealthComponent` (UADHealthComponent), five `STGrenadeDamageVolume*`, `SM_Godray_01..05`,
     `PointLight`, `AbilitySystemComponent`, `SaveComponent`. Open function: the host's fatal damage ->
     UADHealthComponent OnDeath (+0xF8) -> Blueprint `Destroy` override (score reward, parent `Destroy` imp56,
     `SetGrenadeHolesEnabled(false)`, godrays hidden, light off). The picture change is not in that override: it is
     `USTMeshUpdateComponent`, which binds `UpdateMeshBasedOnDamageThreshold(float, float)` (0x0521FD80) at its
     BeginPlay (0x051F5140) to the owner's UADHealthComponent delegate at +0xB8 and swaps the mesh by an async soft
     load (`UpdateMeshComponent` 0x05220000, `OnAsyncLoadFinished` 0x052105C0).
   - **(b) `BP_DestroyableActor_BugBlocker_C_3/_5/_7`** (alive mesh `SM_BugSpawner_CaveGate_01`, no destroyed mesh)
     at (198121, -45838), (182466, -16298), (193273, -37590), each within 1.6k uu of the `LOS_O05_SO02` references
     `ActorReference_C_23/25/27`. They are not opened by damage: `LD: Damage` only spawns DamageVFX and DamagedSFX.
     The level script calls `LD: Destroy` on all three at the `LOS_O05` add (sequence branch at 13016-13088, when
     `objCompleteCount == 3`), which runs `Destruction FX` then `Setup Destroyed` -> `HideActor`:
     `StaticMesh.SetVisibility(false, false)` and `StaticMesh.SetCollisionEnabled(0)` in one function.
   - The tunnel objective `LOS_O05_SO03` (`ActorReference_C_21` at (178964, -34060, 5962)) has no destroyable actor
     within 1.5k uu (only presence `C_77`, a flare and a corpse prop), so SO03 has no cover of its own.
2. **What the open changes and which row carries it.**
   - Nest (a): health 0 and OnDeath reach the client by `world K` (card 130/131 `c98_client_kill`). Grenade volumes,
     godrays and light follow from the client's own `Destroy` override. The mesh swap rides on the +0xB8 delegate
     and an async load; no mod row confirms it ran and none checks the mesh afterwards. Nav: none. Collision: the
     `StaticMesh` collision is not changed by the Blueprint; it changes only with the swapped mesh.
   - BugBlocker (b): visibility and collision change together in `HideActor`; Destruction PFX, SFX and shake in
     `Destruction FX`; `IsDestroyed` and the `Destroyed` delegate. No mod row class carries any of it: hb46 `H` rows
     cover actor `bHidden`, not component visibility; the card 098 table has no `BP_DestroyableActor_*` row; the
     digest has no `kC132Ext` row for the class. The client only gets it by running its own level script copy.
3. **Why collision opened without a row.** For (b) a split state cannot come from `HideActor` (one function sets
   both); it needs the client's script copy to run `LD: Destroy` and a later path to show the mesh again
   (`Setup Alive` from `SaveFullyLoaded` with `IsDestroyed` false sets `AliveMesh` and `ShowActor`, which restores
   collision too, so that does not fit either). For (a) the split fits: the client's kill sets health and
   broadcasts OnDeath, but the mesh swap depends on the +0xB8 delegate firing with the old and new health on the
   client and on the async load finishing; if the swap does not run, the alive mesh stays while the Blueprint's
   death side (grenade volumes off) runs. Candidate (a) is the working answer; the `card299` rows below decide it
   in the next run.
4. **Host log (senior, 2026-11-13).** The actor is settled: candidate (a), the nest
   `BP_DestructibleSpawner_Bugnest_HumanGameplay_C_5`.
   - Host kills it at 83526417 (`Health 10.0 -> -90.0: kill relayed`), 5 s after the mech blast; the client applies
     the `world K` row at 257395625 (`Health base 0 and OnDeath broadcast, IsDead now 1`).
   - The client's own pawn is at (176489, -24201, 6224) at 257419849, about 1000 uu from the nest, 24 s after its
     death: this is where the user stood.
   - Candidate (b) is out for this report: the three BugBlockers are 10k uu or more from that spot, and neither log
     has a row for them (host inventory `mechanism=none:3`). They are still a real gap of class C (no row, no
     digest entry, not replayed to a joiner; the client rejoined at host t=83319878 and 83349613, after the
     `LOS_O05` add at 83221172 that destroys them) and stay in the sweep.
   - The host sent nothing but the kill row for the nest in 83513000 to 83582000: no hide, collision or mesh row.

## Cause (actor settled; one static step left before coding)

The client's kill of a placed nest (`c98_client_kill`, `world_c98.cpp:1966`) sets the Health base to 0 and
broadcasts OnDeath (+0xF8). The nest's picture changes through a different delegate: `USTMeshUpdateComponent`
binds `UpdateMeshBasedOnDamageThreshold(float, float)` to the health component's delegate at +0xB8 and swaps the
mesh by an async soft load. Nothing in the mod fires or checks that path, and no row or digest entry carries the
mesh. The join replay path (`c98_load_dead`, `SaveFullyLoaded`) has the same question.

Step 1b (static, junior, disassembly only, no run), then code:

1. Who broadcasts the +0xB8 delegate, and does `SetNumericAttributeBase` on a non-authority ASC reach it
   (`HealthAttributeChanged` -> `RefreshState`)? If not, that is the cause and the client kill must drive the mesh
   update itself (`ForceUpdateMesh` with the destroyed mesh, or the +0xB8 broadcast with old and new health).
2. Does `SaveFullyLoaded` with `Destroyed` set swap the mesh (`Setup Dead From Load`)? If not, the replay path
   needs the same call.
3. Which collision the alive and the destroyed mesh have, to explain "can walk through" (the user passes the
   alive picture): the collision either never blocked the pawn, or the host's corrections carry the pawn through.

## Step 1b answers (junior, static, 2026-11-13)

1. **The +0xB8 delegate fires on the client.** `UADHealthComponent::HealthAttributeChanged` (0x04A3CD20) calls
   `RefreshState` (0x04A3E480), then broadcasts +0xB8 with (OldValue, NewValue) from the `FOnAttributeChangeData`
   (+0x3C, +0x38). No role or authority test in either. `InitializeHealthComponent` (0x04A3CDD0) binds it through
   `UAbilitySystemComponent::GetGameplayAttributeValueChangeDelegate(Health)` (0x0495E100, vtable thunk slot
   0x4F0), also without a role test, and the GAS value-change delegate fires on any side when
   `SetNumericAttributeBase` changes the value. The client's `world K` row proves the chain ran: `IsDead now 1`
   is +0x148, which only `RefreshState` writes, and the mod's own detour (`detour_health_changed`, death.cpp:461)
   calls the original for placed actors. So the +0xB8 broadcast did run on the client with new health 0.
   - `USTMeshUpdateComponent::BeginPlay` (0x051F5140) binds `UpdateMeshBasedOnDamageThreshold` to +0xB8 only when
     its byte +0xB9 is set, its threshold array (+0xC0, count +0xC8, stride 0x40, `FSTAIDamageThresholdMesh`) is not
     empty and the owner's health component is found at that moment; it stores the health component at +0xD8 and
     sets the current index +0xE0 to -1. No role test.
   - `UpdateMeshBasedOnDamageThreshold` (0x0521FD80) walks the thresholds and picks entry i where
     `new <= pct_i * MaxHealth` and (i is last or `new > pct_(i+1) * MaxHealth`); with new health 0 that is the last
     entry. It calls `UpdateMeshComponent` (0x05220000) only when i differs from +0xE0. No role test.
   - `UpdateMeshComponent` first calls `CheckForLoadingObjects` (0x051F6070): when the entry's soft mesh is still
     loading, it stores the entry at +0x108 and returns; `OnAsyncLoadFinished` applies it later. Otherwise it
     resolves the mesh (`LoadSynchronous` as fallback) and sets it on the current mesh component (+0xD0).
   - So the cause is not a missing broadcast. The remaining candidates, in order: (a) the client copy's
     STMeshUpdate never bound (+0xB9, +0xC8 or the health lookup failed at its BeginPlay, for example a client
     BeginPlay before the health component was ready); (b) the swap was deferred at +0x108 and the async load
     callback never applied it; (c) +0xE0 already held the last index. A `card299` diagnostic row after the client
     kill tells them apart: mesh component +0xD0 and its static mesh name, +0xB9, +0xC8, +0xD8 non-null, +0xE0,
     +0x108 pending, compared with the same row on the host.
2. **The load path does not swap the mesh.** `SaveFullyLoaded` with `Destroyed` set (BP_KillableBuilding_Base @2129)
   runs `Ambient FX Setup`, `Setup Dead From Load` (@2258: `HideActor` only when `HideOnDestroy`) and
   `Setup Hitmarker Tag`; the nest's override (@409) adds godrays hidden and light off. Nothing calls STMeshUpdate.
   In `c98_load_dead` the mesh therefore depends on the same +0xB8 path, driven by `c43_set_component_health(0)`
   before it, with the same three candidates. The nest's BeginPlay (@535) calls an STMeshUpdate function with
   `StaticMesh` (likely `SetCurrentMeshComponent`, whose `OnMeshComponentChanged` re-runs the threshold update with
   the current health), so a copy made after the death would pick the dead mesh by itself.
3. **Collision.** Not answerable from the decompiles: the nest Blueprint never changes `StaticMesh` collision, and
   the mesh assets' collision is not in the extracts. Only the grenade volumes go off (`SetGrenadeHolesEnabled
   (false)`). "Walk through" fits either the alive mesh having no pawn-blocking collision at the hole, or the
   swapped mesh being set while its render state did not update. The diagnostic row adds the component's
   collision enabled and the static mesh name, which answers it in the next run.

Fix direction once the row decides: if (a), the client kill calls `ForceUpdateMesh` (0x051FBF40) with the last
threshold's mesh; if (b), the row names the pending load and the fix completes it; the digest gets the nest's
current mesh index either way.

## 2026-11-16 11:50: nest has the component in its Blueprint, lookup returns none

- c423b126a: `card299 ... no STMeshUpdate component (class found)` on 2 host and 4 client rows for
  `BP_DestructibleSpawner_Bugnest_HumanGameplay_C`. The Blueprint extract has an SCS node `STMeshUpdate_GEN_VARIABLE`
  and references `SM_BugSpawner_Small_Destroyed`, so the component exists at construction. `find_component_of` is the
  same vtable FindComponentByClass call 7 other callers use with success.
- Open causes: the node's class is not `/Script/Aloft.STMeshUpdateComponent` (another native or Blueprint class), or
  the component is gone by the time of the row (destroyed by the death graph).
- Next (`c299_owned_patch.py`, for b128): on a miss the row is followed by
  `card299: <role> nest <actor> owns N component(s): name:class ...` (at most 32). The class printed beside
  `STMeshUpdate` names the lookup class; no `STMeshUpdate` entry means the component is destroyed before the row and
  the row must move ahead of the death graph.

## Senior read of the owned-component rows (2026-11-17 08:49, runs c423b173a, c423b174a): the rows were on the wrong actors

- The two LOS_O02 "nests" the c423 scenario nukes are instances named `BP_DestructibleSpawner_Bugnest_HumanGameplay_C_0`
  and `_7`, but their class is `BP_SatchelBuilding_Plasmabug_C` (host rows `card163: ... _C_0
  (BP_SatchelBuilding_Plasmabug_C) destroyed by a damage event (Destroyed 0 -> 1): relayed as S 2`, `card095 ...
  collision ... Corpse 0 -> 3`). They die by the card 163 'S' path, own 15 components (SkeletalMeshComponent, Corpse,
  StaticMesh, VertexMesh, Muzzle, a `MS_EMIT_PlasmaBug_Idle` AudioComponent ...) and have no STMeshUpdate component.
  That is why every `card299` row so far read `no STMeshUpdate component (class found)`: the lookup is right, the
  actors are not the playtest's nest.
- The playtest's nest is `BP_DestructibleSpawner_Bugnest_HumanGameplay_C_5` at (177000, -23329, 6121), class
  `BP_DestructibleSpawner_Bugnest_HumanGameplay_C` (host playtest log `client/UBWCoop-menu-20261113-182510.log`,
  t=83526417: `card098: host ... BP_DestructibleSpawner_Bugnest_HumanGameplay_C
  BP_DestructibleSpawner_Bugnest_HumanGameplay_C_5 Health 10.0 -> -90.0: kill relayed` x4 in one tick, then
  `NS_FX_BugSpawnerDestruction`; the hits before it are `NS_FX_Weapons_MarksmanHit_Ground1`). It is of the
  killable-building family (the STMeshUpdate analysis above stands for it) and dies by the 'K' path. No run since the
  playtest has killed it: c423b174a has 0 rows naming `_C_5`.
- Also seen: the K row was relayed 4 times in the same tick with the same Health 10.0 -> -90.0 (4 damage events
  reading the same old value). The client applied one. Not a visible defect; noted for the K path's dedupe.
- Next (scenario only, no build): `tools/scenarios/c423-gates-c2m1.txt` now has a card 299 block after the
  `c423-gate-before` shot: both players at nest `_C_5`, shots before, `host kill actor ..._C_5`, waits on the host
  `kill relayed` row, the client `world K` row and the client `card299` row, shots 3 s after. Expected: `card299` rows
  on both sides with the same mesh name (the destroyed mesh), index and collision, and both after-shots showing the
  hole open. A client row with another mesh, a pending load or `mesh none`, or an after-shot with the hole closed,
  names the fix (candidates (a) and (b) above).

## Senior read of run c423b177a (2026-11-17 09:08)

Nest `_C_5` killed by command: host and client rows equal, live and after the rejoin replay: `mesh
SM_BugSpawner_Small_Destroyed bound 1 thresholds 2 health non-null index 1 pending null collision 3`. One host
`kill relayed` row (the 4 rows of the playtest came from real damage, not the kill verb). The shots did not show the
nest (client looked at cave wall, a bug filled the host frame). Scenario fixed: closer stands (900 / 700 uu), looks by
bounds, bugs cleared and looks repeated before the after-shots. Run c423b177b owes the four shots; rows already pass.

## c423b177b shots (senior spot-check, 2026-11-17)

`client-c423b177b-c299-nest5-after.png`: tunnel open, nest remnant low in the frame, score 160,000. Junior viewed all
four: nest whole before, open after, on host and client. The run ended in the card 476 host crash after this block, so
the card closes with the next c423 run that passes end to end (c423b178a).

## Closed

Closed 2026-11-17. Runs c423b177b (shots: nest whole before, tunnel open after, host and client) and c423b178b/c on
b178 `f6596be3fcb2d778` (end to end pass; client `world K ..._C_5` 1 live row and 1 journal replay row after the
rejoin; nest rows equal on both sides). K row now relayed once per death (`c98_host_kill` journal test, b178).
