# Card 144: The client runs its own AI spawner and hazard state

Status: queued 2026-11-08, from card 137 run 1 (hb68) step 2a change log. Budget: three runs.

Read: card 137 "2a local-sim classes" (hb68), docs/STATE.md "Card scope and investigation rules".

## Why

hb68 client change log, cause "local sim": spawner and navlink enable toggles on BP_AISpawnPoint_C, BP_AISpawner_C,
ADNavLinkProxy, STAISpawner (callers exe+0x4C5FE0C 159, exe+0x37C2C74 35, exe+0x51BA9D1, 0x5144609, 0x5143A0F 18 each),
bHidden and collision on BP_AI_MetricGuide_C, STSpawnPoint, BP_AISpawnPoint_C (exe+0x5145FF3 family), and
BPHazardousZone_Shunt_C 33, and WarriorBug_Corpses_Destructible_01/02 "alive" set by local sim (exe+0x229F17D: the client destroys these placed corpse props itself). The client's own AI director and hazard logic change world state the host owns. Navlink
and collision state differ from the host's.

## Do

1. Static: name each caller RVA's function, and say why it runs on the client (director tick, overlap, timer). Mark each
   one as presentation only or as world state.
2. World state ones: stop the client-local logic in the layer that owns it (host authority), and take the host's
   value through the existing relays where the client needs it.
3. One run: the 2a change log shows 0 local-sim rows for these classes.

## Done when

- The caller table with verdicts, the fix, and the run lines. The usual Report items. It ends with "Report complete."

The barbed-wire damage item (finding 1) moved to card 179 on 2026-11-11: one goal per card.

## Senior trace (2026-11-11): the callers, by PDB
Resolved against the PDB publics (`logs/pdb_publics.txt.funcs.pkl`):

| Caller | Function | Verdict |
|---|---|---|
| exe+0x4C5FE0C (159) | `AADPickupBase::AADPickupBase` +0x2dc | constructor: the actor is being built (level streaming in), not simulated |
| exe+0x51BA9D1 (18) | `ASTWeaponPickup::ASTWeaponPickup` +0x1c1 | constructor |
| exe+0x5144609 (18) | `ASTDamageableActorCorpseBase::ASTDamageableActorCorpseBase` +0x1a9 | constructor |
| exe+0x5143A0F (18) | `ASTAISpawner::ASTAISpawner` +0x10f | constructor |
| exe+0x5145FF3 family | `ASTSpawnPoint::ASTSpawnPoint` +0x2b3 | constructor |
| exe+0x37C2C74 (35) | `UPrimitiveComponent::execSetCollisionEnabled` +0x74 | Blueprint script call: the only caller that is client logic. Open: which Blueprint and event (kis.py) |
| exe+0x229F17D | `TBaseUObjectMethodDelegateInstance<UWorldPartitionSubsystem, void(UWorldPartition*)>` +0xad | world partition streaming: the corpse props go with their cell, not by simulation |

Open, to settle before code: the class names in the change log do not match the constructor that the caller address
names (a spawner row with an `AADPickupBase` constructor address), so either the change log reads the caller one
frame off, or these constructors share a tail. Check how the 2a log takes its caller address.

**Scope after this trace:** the constructor and streaming rows are not world state the client decides; they are a
change-log cause error and belong to card 149 (cause "construct" or "streaming", not "local sim"). What stays here:
1. the Blueprint `SetCollisionEnabled` rows (35): name the Blueprint, then host authority or presentation only;
2. BPHazardousZone_Shunt_C (33): which function toggles it on the client;
3. whether the client's AI director ticks at all (ASTAISpawner tick on a client): one static answer, with the address.
If 1 to 3 all come out presentation only, this card closes with no code and no run.

## Junior trace (2026-11-12)

Static only: no build, no run. Changed files: this card only.

### How the 2a change log takes its caller (the open question)

Every `c137_client_change` call site passes `exe_rva(_ReturnAddress())` taken inside the detour. Those detours are
`detour_set_actor_collision` and `detour_set_collision_enabled` (input_diag.cpp:550 and :615), and
`detour_c98_hidden` / `detour_c98_destroy` (world_c98.cpp:158 and :144). So the logged caller is the return site in the
direct native caller of `SetActorEnableCollision` / `SetCollisionEnabled` / `SetActorHiddenInGame` / `DestroyActor`.
It is not one frame off, and the constructors do not share a tail.

The apparent mismatch comes from the "Why" line, which groups callers across classes. Per row, each caller is the
native constructor of that row's own class chain:

| Caller | Function (PDB) | Row classes (hb68 client) |
|---|---|---|
| exe+0x4C5FE0C | `AADPickupBase` ctor +0x2dc | BP_AmmoPickup_* (component collision 1 -> 2) |
| exe+0x51BA9D1 | `ASTWeaponPickup` ctor +0x1c1 | BP_WeaponPickup_* (0 -> 1) |
| exe+0x5144609 | `ASTDamageableActorCorpseBase` ctor +0x1a9 | BP_WarriorBug_Corpses_Destructible_01/02_C (1 -> 0) |
| exe+0x5143A0F | `ASTAISpawner` ctor +0x10f | BP_AISpawner_C, STAISpawner (1 -> 0) |
| exe+0x5145F25 / 0x5145FF3 / 0x5145FFD | `ASTSpawnPoint` ctor +0x1e5 / +0x2b3 / +0x2bd | BP_AISpawnPoint_C, STSpawnPoint |
| exe+0x5145A00 | `ASTSatchelBuildingBase` ctor +0x130 | BP_SatchelBuilding_Plasmabug_C (archetype) |
| exe+0x4B8D9E4 | `ANavLinkProxy` ctor +0x2e4 | ADNavLinkProxy (actor collision 1 -> 0) |

All of these rows are on thread T 1300 (the async loading thread; the game thread is T 584). Each logs at (0, 0, 0)
with the object name equal to the class name, `Default__...`, or `NODE_AddChildActorComponent-..._CAT`. They are
CDOs and archetypes being constructed, not placed instances. `c50_placed` lets them through as "placed". That is a
cause-label problem for card 149 (cause "construct"); it is not fixed here.

For a Blueprint call, the direct caller is the VM thunk (`UPrimitiveComponent::execSetCollisionEnabled` +0x74), so the
Blueprint and event are not in the row. Naming them is also card 149's.

### Trace table

| # | Item | Function | Address | How it runs on the client | Verdict | Status |
|---|---|---|---|---|---|---|
| 1a | BP_Dropship_LandingBlocker_C Box 1 -> 0 | `BP_ProceduralDropshipTroops.UpdateCollision(NewDropshipState)`. Loops over `[GetNavigationBlocker().Box, DropshipWall_L/Middle/R/Back, Door_L/R]` with collision chosen by state (0 -> 0, 1 -> 2, 2 -> 0). Also `SetupAndAlignNavigationBlocker` (Box -> 1 at 827). | thunk exe+0x37C2C74; BP offsets 1381, 827 | Driven by the dropship's `StateChange` event. The dropship replicates (card098 `replicates 1`), so both sides run it from the same state. | Same write on both sides, from replicated input. The host's write is also relayed by card 095 CollisionOp (host log: `card095: host ... SetCollisionEnabled on BP_Dropship_LandingBlocker_C_N ... 1 -> 0, caller exe+0x37C2C74`). Not client-decided. | verified |
| 1b | BP_SatchelBuilding_Flakbug_C / _Plasmabug_C component 3 -> 0 (and Plasmabug 1 -> 0) | Flakbug Ubergraph 7511, entered from `BndEvt__..._STSaveComponent_..._SaveFullyLoadedDelegate`. It calls `Setup Mesh Visibility()`, then branches on the saved `Destroyed`: dead path 7640-7788 (VertexMesh 0, SkeletalMesh 0, Corpse 3, Capsule 0) or alive path 7827-7999 (Corpse 0, Capsule 3). Plasmabug has no SetCollisionEnabled of its own (inherited). | thunk exe+0x37C2C74; BP offsets 7640-7999 | The save component's "fully loaded" event, which fires at map load on every side | Load-time setup from save data; the host runs it too (hb68 host: 10 Flakbug and 4 Plasmabug lines at exe+0x37C2C74, and card095 relays of SkeletalMeshComponent 1 -> 0). Not client-decided. Which component the 3 -> 0 row is (Corpse on the alive path fits) is inferred. | event verified; component inferred |
| 1c | BP_DestructibleSpawner_Bugnest_HumanGameplay_C component 0 -> 3 | `SetGrenadeHolesEnabled(!HealthComponent.IsDead())` at Ubergraph 2202, entered at 2131 from `BndEvt__..._SaveComponent_..._SaveFullyLoadedDelegate`. The function sets `STGrenadeDamageVolume..4.SetCollisionEnabled`. | thunk exe+0x37C2C74; BP offset 2131/2202 | The same save "fully loaded" event at load | Same as 1b: load-time setup, run by the host too (hb68 host: 8 Bugnest lines at exe+0x37C2C74). Not client-decided. Open: the BP's switch literal for true reads as 1 (QueryOnly), but the row logs 3, so this exact write may come from a parent class call. The class and event are verified. | event verified; value open |
| 2 | BPHazardousZone_Shunt_C (33), component 3 -> 0 | `ASTHazardousZoneBase::ASTHazardousZoneBase` +0x47a: the constructor sets HazardEndPoint collision. No Blueprint toggles it (the BPHazardousZone_Shunt kis output has no SetCollisionEnabled). | exe+0x514542A | Construction only: all rows are T 1300, at (0, 0, 0), named `NODE_AddChildActorComponent-0_BPHazardousZone_Shunt_C_CAT` (an archetype). The same caller logs on the `Default__` CDOs of every hazard zone class, on the host too. | Construction, not simulation. Not world state the client decides; cause label for card 149. | verified |
| 3 | Does the client's AI director tick? | `ASTSpawnerBase::Tick` (0x51725A0): `AActor::Tick`, then `TickQueuedSpawnRequests` (0x51745E0), then vtable +0x4A8 with false when [+0x434] == [+0x408] (queue drained; likely `SetActorTickEnabled`, not proven). There is no `ASTAISpawner::Tick` override and no role or net-mode test in Tick. | 0x51725A0, 0x51745E0 | It can tick on the client, but has nothing to do. The queue fills only through `QueueSpawnRequest` (0x516A850): xrefs are OnWorldBeginPlay +0x70, SpawnWithDelay +0x41, SpawnWithParameters +0x5b and TickQueuedSpawnRequests +0x412. The only spawn call is `PerformSpawn` (0x51671F0; xrefs SpawnWithParameters +0x13, TickQueuedSpawnRequests +0x3d9). Card 006 gates both entries with `is_net_client` (guards.cpp:313 and :327), and `StartEncounter` / `ActivateCurrentWave` (manager side) too. hb68 client: `StartEncounter`, 3x `PerformSpawn` and 5x `QueueSpawnRequest` "skipped on client", then "client spawn ... further skips not logged"; host 0. | The gate is net mode NM_Client (3), at function entry (MinHook), so it covers every xref. The client director runs no spawn and changes no world state. | verified (tick enabled-ness itself open, no effect) |

### Verdict

Items 1 to 3 are all not world state the client decides.
- The Blueprint rows (1a-1c) are the same load-time or replicated-state-driven scripts the host runs, with the same
  inputs. Card 095 relays the host's own writes.
- The Shunt rows (2) are construction of archetypes/CDOs.
- The AI director (3) ticks with an empty queue behind card 006's gates.

Per the scope, the card closes with no code and no run. The remaining wrong "local sim" labels (constructor/CDO rows on
T 1300, VM-thunk callers) belong to card 149.

Two opens remain, and neither changes a verdict:
- 1c: the exact write behind the logged 0 -> 3 value.
- 3: whether the spawner's tick is enabled on the client at all.

### Commands behind the verified lines

- PDB names: `python3 <62996b75 scratchpad>/rvaname.py $PWD/tools 514542A 5145A00 4B8D9E4 5143A0F 5145FF3 5145F25 5145FFD 37C2C74`
  and a scan of `logs/pdb_publics.txt.funcs.pkl` for `ASTSpawnerBase@@` / `ASTEncounterManagerBase@@` /
  `ASTAISpawner@@` (Tick 0x51725A0, TickQueuedSpawnRequests 0x51745E0, no ASTAISpawner::Tick).
- Tick body: `llvm-objdump -d --start-address=0x1451725A0 --stop-address=0x1451725E8 <Shipping exe>`.
- Queue and spawn xrefs: `python3 <62996b75 scratchpad>/xref.py <Shipping exe> 516A850 51671F0 51745E0`, then rvaname.py on
  the hits.
- Blueprints: `python3 -I <62996b75 scratchpad>/kis2/kis.py` on BP_ProceduralDropshipTroops (bpds/),
  BP_SatchelBuilding_Flakbug/_Base/_Plasmabug, BP_DestructibleSpawner_Bugnest_HumanGameplay and BPHazardousZone_Shunt.
  The outputs are in the 6d801d14 scratchpad, `kis-*.txt`.
- Log rows:
  - `grep "caller exe+0x37C2C74" logs/hb68-{host,client}.log` (per-class counts: host Bugnest 8, LandingBlocker 476,
    Flakbug 10, Plasmabug 4).
  - `grep "card137: client change.*caller exe+0x<rva>" logs/hb68-client.log` (thread T 1300, class per caller).
  - `grep "skipped on client" logs/hb68-client.log` (client gate lines; host count 0).
- Caller capture: `grep -n _ReturnAddress mod/src/ubw/input_diag.cpp mod/src/ubw/world_c98.cpp`, and
  mod/src/ubw/change_c137.cpp.

Report complete.

## Senior review (2026-11-12)

Closed with no code and no run, as the scope allowed. Checked by me: only this card changed; `detour_perform_spawn`
and `detour_queue_spawn` (guards.cpp) return early on a net client; hb68 shows 9 "skipped on client" lines on the
client and 0 on the host; the exe+0x514542A rows are `Default__` archetypes at (0, 0, 0) with placed 0. Open lines
kept as written (1b component, 1c parent-class write, whether the spawner tick is enabled on the client): none can
change the verdict. The archetype rows passing `c50_placed` go to card 149; card 179 no longer has a client-local
collision change to explain.
