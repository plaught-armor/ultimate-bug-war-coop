# 527: Bug load performance, scoping (separate, co-op compatible mod)

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: Opened 2026-11-19 from the user's idea: look into how the bugs are loaded and ticked, possibly as a separate mod
that stays co-op compatible. Scoping only; no code until step 1 names where the time goes.

## Why

In c263L (150 live bugs on FED_C3M2) the host ran at 21 to 25 fps and the client at 24 to 34 fps. The co-op mod's
own share was 6 to 8 percent of frame time on the host and 9 to 16 percent on the client. GPU busy peaked at
57 percent, so the game thread is the limit. Most of that cost belongs to the game.

## Rules for a co-op compatible performance mod

- It must not change what the host decides: which bugs spawn, how many, when, their NetGUIDs, AI choices, or
  damage. All of those are synced, and the digest compares them.
- It may change what each side draws and how often purely visual work runs: animation tick rate and LOD,
  significance thresholds, FX and decal limits. These may differ per side, as long as card 140 (corpse positions)
  and the `Pose:` digest stay at 0 mismatches.
- Spawn path changes (pooling, preloading bug classes before a wave, async loads) are host side, must keep the
  spawn order and NetGUIDs, and must leave the client's copies created by replication as they are now.
- Packaging: a second DLL cannot also be `dwmapi.dll`. Either the co-op mod loads it (a plugin entry), or it uses
  another proxy name. It must run alone in single player as well.

## Static findings (senior, 2026-11-19, PDB publics and disassembly)

Spawn path:

- Bugs are not pooled. `ASTSpawnerBase::SpawnWithParameters` (0x0516F660) either queues the request
  (`QueueSpawnRequest`, drained by `TickQueuedSpawnRequests` 0x051745E0) or calls `PerformSpawn` (0x051671F0), which
  splits the count over spawn points and calls `PerformSpawnAtPoint` (0x05167930) per bug. That gets the class from
  `GetSpawnItemForTag`, takes the point's `GetSpawnTransform`, builds an `FActorSpawnParameters` and calls
  `UWorld::SpawnActor` (0x03DCA380) directly, then `ActorSpawned` on the spawner and the point, and plays the spawn
  gameplay cue. No `UADActorPoolerBase` call is on this path.
- The ADPooling plugin (`UADActorPoolerBase::Get` 0x04A2FF50, `PreAllocate` 0x04A307B0) is used for gore parts
  (`ASTGorePartBase::PoolingActivate/Deactivate`) and projectiles (`UADProjectilePooler::SpawnProjectile`), not NPCs.
- The class is resolved before the spawn (the spawner's tag to class map, `FullyLoaded`/`ActorFullyLoaded` gates),
  so a host spawn is not a sync class load. On the client, a replicated copy is spawned by the net driver; whether
  its first copy of a class loads the class at that moment is not yet known.
- The host spawns a wave about one bug per 100 ms (c263L `card125: host spawned` rows are 100 ms apart), so spawn
  cost is already spread. c263L frame windows sorted by spawns in the window show no clear spawn hitch (host
  windows with 0 spawns: mean 26.8 ms; with 30 or more: 28.1 ms). The windows are 10 s, too coarse to rule out
  single-frame hitches.
- Dead bugs are destroyed: `ConditionallyUpdateTicks` calls `AActor::Destroy`, and corpses are limited by corpse
  tickets (`TryTakeCorpseTicket`, `OnCorpseTicketExpired`, `GetCorpseDespawnTime`).

Tick path: the game already has a tiered system.

- `ASTNonPlayableCharacterBase::OnSignificanceChanged` (0x0519DE50) reads the `FSTNPCSignificanceLevel` for the
  bug's level, sets the actor and component tick intervals (`FTickFunction::UpdateTickIntervalAndCoolDown`, twice),
  sets aggregated static mesh tick intervals (`USTAggregatedSMInstance`), and can switch the skeletal animator to a
  static mesh (`USTSkeletalAnimatorComponent::SetShowingStaticMesh`). There is also a 2D form (`IsNPC2D`) and inlined
  ticks (`InitializeInlinedTicks`, `TickInlinedTicks`).
- `CalculateMeshBudgetSignificance` (0x0517EE70) feeds UE's animation budget allocator (`FAnimationBudgetAllocator`,
  `USkeletalMeshComponentBudgeted` are in the exe). Whether `a.Budget.Enabled` is on in this game is not yet known.
- `ASTGameplayGameMode::Tick` ends in a tail call to `UpdateSignificanceManager` (0x051408C0) every frame. It uses the
  local ST player controller's view plus `GetSignificanceTransforms`. The co-op mod already adds each remote pawn as
  a significance location on the host for that call (`detour_c132_update_significance`), so bugs near the client
  keep a full tick on the host, and runs the same update on the client each frame (`client_significance_step`, the
  game's own cost, 2846 ms per map in c263L).

What this means for a performance mod:

- There is no spawn hitch to fix yet, and NPC pooling would reuse actors, which changes NetGUID and actor lifetime
  for every co-op guid map (pose rows, digest, card 140). Pooling is ruled out unless step 1 shows real spawn
  hitches; if so, the safer lever is preloading on the client.
- The likely levers are the significance tiers: their distance thresholds and tick intervals in the
  `USTSignificanceSettingsAsset`, and the animation budget. On the client they are free to change (its copies are
  visual). On the host only visual work may change (mesh and animation ticks of live bugs); the actor and movement
  tick intervals drive AI and movement and stay as the game sets them. A host bug's death animation timing sets its
  ragdoll start, which card 152 streams, so the host's animation rate must not change for a dying bug.

## Step 1 (measure, one load run, behind the DIAG flag)

New rows, both sides, per 10 s window and per map:

1. `card527: <side> spawns`: a detour on `UWorld::SpawnActor` (0x03DCA380) times each call whose class is an
   `ASTNonPlayableCharacterBase`: per class count, total ms, max ms, and the first spawn of the class on its own.
   On the client this covers the net driver's copies only if they go through `SpawnActor`; the row count against the
   host's `card125` count checks that.
2. `card527: <side> npc tick`: detours on `ASTNonPlayableCharacterBase::Tick`, and on the character movement and
   skeletal mesh `TickComponent` filtered to an NPC owner: per window, total ms and calls for each.
3. `card527: <side> npc census`: live NPC count per significance level, per 2D form and per static mesh form, read
   every 10 s (the significance level and form offsets need RE first: `OnSignificanceChanged` reads them).
4. The `a.Budget.Enabled` value at map start.

Census offsets (static RE, senior 2026-11-19, from `OnSignificanceChanged` 0x0519DE50 and
`GetCurrentSignificanceLevelSettings<FSTNPCSignificanceLevel>` 0x05178B80):

- The `ISTSignificanceObjectInterface` subobject sits at NPC+0x760 (the interface thunk does `lea rcx, [rbx-0x760]`
  to reach the actor). Inside it: +0x8 a bool (false: always level 0), +0x10 the level array data, +0x18 its count,
  +0x20 the significance as a float. The level index is `(int)(significance - 1.0)`, clamped to the last entry;
  each array entry is 16 bytes with the settings pointer at +0x8. `HasSignificance` (0x0515D990) gates all of it.
- So per NPC: significance float at NPC+0x780, level array at NPC+0x770 (count NPC+0x778). Settings: +0x20 the tick
  interval (float, given to `SetAggregatedTickInterval` on the three aggregated mesh instances at NPC+0xA10, +0xA18,
  +0xA20), +0x29 a bool read just after the 2D test (likely the static mesh form; to confirm by reflection of
  `FSTNPCSignificanceLevel`, `StaticStruct` 0x05097470).
- 2D form (`IsNPC2D` 0x05197F80): the component at NPC+0xA30 is a `UADSpriteAnimatorComponent` (IsA through
  `StaticClass` 0x04A21C00).
- The census row: per level index, count of NPCs, how many are 2D, how many have the +0x29 flag; plus the level
  table of the first NPC of each class once per map (count and each entry's tick interval).

Predictions (c263 FED_C3M2 load run, 150 bugs): spawn max per call under 5 ms on the host, and the client's first
copy of each class over 20 ms if it loads the class then; NPC tick plus movement plus mesh is the largest
game-thread share on the host (over 30 percent of frame time at the peak), less on the client (no AI).

A host-only baseline (the same scenario with no client) is a second run, only if step 1 shows the host's share is
not explained by NPC ticks.

Card 526 (client Niagara growth) goes first: it is a co-op bug, and it inflates every client measurement.

## Step 1 census in source (senior, 2026-11-19, not yet in a build)

- `frame_c330.cpp` `c527_census_row`, after the card 526 row (UBWCOOP_C132_FX=1 only, every census window): every
  live `ASTNonPlayableCharacterBase` (subclasses, CDOs left out, up to 1024 read) counted per level index, with the
  2D form (component at +0xA30 is `/Script/ADAnimation.ADSpriteAnimatorComponent`) and the settings +0x29 flag per
  level. Row: `card527: <side> npc census N (R read): level K: n (2D d, flag29 f), ...; sprite class found; scan X ms`.
- Once per class per process: `card527: <side> npc levels <class>: C levels; 0: tick T s, flag29 F; ...`.
- Backup `build/frame_c330.cpp.pre527`. Object compiles clean, 0 warnings.
- Item 1 in source: `detour_c75_world_spawn` (`vehicles.cpp`, the card 75 SpawnActor detour) times the original
  with QPC when the FX flag is set and hands it to `c527_spawn`, which counts only `ASTNonPlayableCharacterBase`
  results per class. Per map, at `c330_map_end`: `card527: <side> map M spawns <class>: n, T ms total, A ms mean,
  X ms max, first F ms`. Backups `build/vehicles.cpp.pre527`, `build/hooks_internal.h.pre527`.
- In build b313 (99a5d59922248f6b, with the card 526 fix), sent to the junior for a c263 C3M2 FX run. Item 2 (tick
  rows) is not in source yet.
- Predictions (next lane 2 C3M2 run with the FX flag): host and client level tables match per class; the client's
  level counts track the host's within a few NPCs per level (card 132 significance locations); scan under 5 ms.

## c263L313 results (b313, senior dig 2026-11-19)

- Level tables: match per class on both sides. Bugs have 7 levels (tick 0.034, 0.034, 0.1, 0.25, 0.25, 1, 30 s);
  Hopper 6 (0.1 to 30 s); Morita troopers 5. The +0x29 flag is set on levels 0 to 4 for bugs and never for troopers.
- Level counts do not track. At the peak the host had 247 NPCs (level 1: 21, level 3: 40, level 4: 102) and the client
  210 (level 3: 51, level 4: 124). Near the end the host had 153 at level 1 and the client 154 at level 4. The client
  ranks its copies lower (its own view only). That prediction fails, and it is not a co-op bug: the client's copies
  are visual, so a lower tier is a free saving there.
- Census scan: 4.4 to 7.3 ms. The under-5 ms prediction fails. It is a diagnostic, every 30 s, FX flag only.
- Spawns per map. Host: WarriorBug 154 spawns, 372 ms total, 2.4 ms mean, 5.8 ms max; all classes about 583 ms. Client
  copies: WarriorBug 151, 241 ms, 1.6 ms mean, 3.0 ms max; first copy of each class 0.9 to 1.8 ms. So there is no
  class load on the client's first copy (the over-20 ms prediction is wrong), and the host max is 5.8 ms (the under-5
  prediction just fails). Spawning is about 0.6 s of host game thread per 4-minute map: not a lever. Pooling and preloading
  are dropped.
- Next: item 2 (tick rows) decides whether the levers are the tier intervals.

## Item 2 in source (senior, 2026-11-19, build b314 fc7647d1ddbc2d21)

- Three detours installed only with `UBWCOOP_C132_FX=1` (hook table gate `&g_c132_fx_log`):
  `ASTNonPlayableCharacterBase::Tick` (0x051AABF0), `USkeletalMeshComponent::TickComponent` (0x03AFAC10) and
  `UADSpriteAnimatorComponent::TickComponent` (0x04A24F40). The movement tick is timed in the existing
  `detour_cmc_tick`. Component ticks count only when the component's owner (+0xA0) is an NPC.
- `frame_c330.cpp`: a component tick that runs inside an NPC actor Tick (inlined ticks) is counted as "inside" and its
  time is left to the actor Tick, so the four kinds do not overlap.
- Rows: every census window `card527: <side> npc tick window W ms: actor T ms N calls (I inside), movement ..., mesh
  ..., sprite ...; all X ms (P% of frame time)`, and per map `card527: <side> map M npc tick: ...`.
- Limits: a skeletal mesh subclass that overrides TickComponent without calling the base is missed; animation work
  handed to worker threads is not in the mesh time (this measures the game thread). Two QPC reads per timed call
  (about 0.1 to 0.3 ms per frame at 5000 calls) inflate the totals slightly.
- Predictions (next C3M2 FX run): host all-kinds share over 30 percent of frame time at the 150-bug peak, mesh the
  largest kind; client share lower, with the movement kind much smaller (simulated proxies, no AI).

## c263L314 results (b314, 2026-11-19)

- Host peak window 30.81 percent of frame time: movement 5196 ms, actor 3678 ms, mesh 380 ms. Map totals: movement
  19434 ms, actor 14486 ms, mesh 1918 ms. The over-30 percent prediction holds; mesh largest fails.
- Client peak window 29.98 percent: movement 5269 ms, actor 3080 ms, mesh 651 ms; map movement 20319 ms. The client
  is not lower, and its movement kind is the largest, not much smaller: both predictions fail. The client's NPC copies
  run full CharacterMovementComponent ticks, not light simulated-proxy ones.
- Spawns: WarriorBug 154, 376.6 ms in all, max 5.29 ms per call (not a lever, as c263L313).
- Per call (map rows): host actor 35 us (412572 calls), movement 30 us (641196), mesh 9 us (206320); client actor
  29 us (421342), movement 38 us (531443), mesh 10 us (220863). Every sprite tick runs inside the actor Tick (103623
  host, 106718 client), so its time is in the actor kind. Mesh ticks at about half the actor rate (the game's own
  rate optimisation already applies); movement ticks about 1.5 times per actor Tick on the host, 1.3 on the client
  (the reason is not read yet: substeps or NPCs with a CMC whose actor Tick is not ASTNonPlayableCharacterBase's).
- Levers, by size: (1) the NPC movement tick on both sides (about 60 percent of the NPC tick time), first the client,
  whose copies are driven by host rows and may not need the full CMC tick or need it at a lower rate; (2) the actor
  Tick (about 40 percent on the host). Mesh and sprite are under 10 percent together.
- Next: a static read of what the client CMC tick does for a host-driven NPC copy (mode, velocity, floor checks) and
  of the engine's tick interval and significance settings for ASTNonPlayableCharacterBase, before any code.

## c263L315 results (b315, 2026-11-19) and the next row

- Peak: host 32.43 percent (movement 5294 ms, actor 4217, mesh 235), client 35.02 percent (movement 6316, actor
  3763, mesh 435). Movement is still the largest kind on both sides.
- The tree has no FTickFunction offsets past TickState (`pose_c132.cpp` `kCompTickStateByte`, actor tick at +0x28,
  component tick at +0x38) and no UE SDK dump. TickInterval is a reflected member of the FTickFunction struct, so the
  next diagnostic reads it by name. `c491_struct_text` (`score_c475.cpp`) already prints a struct's number members:
  run it on the PrimaryActorTick and PrimaryComponentTick struct properties of the first NPC of each class per map,
  and add LocalRole, MovementMode and the CMC's NetworkSmoothingMode (`kCmcSmoothingMode`). One row per class per map
  behind `UBWCOOP_C132_FX=1`.
- What the row decides: a client CMC tick interval of 0 with LocalRole 1 (simulated proxy) on every copy means the
  client runs the full simulated tick each frame, and the lever is a client tick interval or a tick-off for copies far
  from both players. A nonzero interval already set means the 1.3 ticks per actor Tick come from elsewhere (substeps),
  and the lever is the substep count.
- In source and built (senior, 2026-11-20, b328 `eace4341d16ff00d`, on top of the card 518 b327 build):
  `c527_tick_setup` in `frame_c330.cpp`, called once per class right after the `npc levels` row (so FX flag only).
  Row: `card527: <side> npc tick setup <class>: PrimaryActorTick={...} Role=R RemoteRole=R PrimaryComponentTick={...}
  MovementMode=M NetworkSmoothingMode=S; movement component <class>`. Struct members are the reflected number
  members (TickInterval among them); `c491_struct_text` moved out of its anonymous namespace for this.
- Predictions: client rows Role=1 (simulated proxy) and host Role=3; PrimaryComponentTick TickInterval 0 on the client
  copies (full movement tick every frame), which makes a client movement tick interval or tier the first lever.
- Result c263L328 (b328): Roles pass. Client Role=1 RemoteRole=3 on Trooper_Morita, HopperBug, ChariotBug, WarriorBug,
  JumperBug and ArcherBug; host Role=3 RemoteRole=1 on those plus TurretTrooper and AATurret. MovementMode Warrior 1,
  Hopper 5, others 2; NetworkSmoothingMode 2. Both tick structs printed `{}`, so TickInterval stayed unread: the
  printer read only the struct's own members, and FActorTickFunction and FActorComponentTickFunction keep TickInterval
  in their super FTickFunction. NPC tick share peaked at 35 percent of the frame on the client (about 185 NPCs) and
  33 percent on the host.
- b329 `ebe782aabdc93458`: `c491_struct_text` now walks super structs. The TickInterval prediction carries to the
  next run.
- Result c263L329 (b329): the prediction failed. The movement component TickInterval is already 0.0333 (30 Hz) on
  both sides for every class, and 0.0167 (60 Hz) for HopperBug. Actor TickInterval differs per instance (client
  Warrior 0.070, Jumper 0.112, Chariot 0.175, Archer 0.014, Trooper 1.016; host Warrior 1.230, Jumper 0.026), so the
  game already staggers or tiers actor ticks; the row shows only the first instance seen. Consequence: the movement
  tick is already throttled to 30 Hz, and the remaining lever is a distance or visibility tier on top of it (for
  example 10 Hz beyond some range from every player, with HopperBug's 60 Hz as the first candidate). The next step
  is one row per tick setup that buckets the live NPC component intervals by distance to the nearest player, to show
  whether the game already tiers by distance before a tier is designed.
- Low priority: this is the scoping card for a separate mod. It waits behind real bug cards and is carried by a
  build that has a bug batch.

## Decal cap sized from measured cost (user, 2026-11-20)

The user's rule is that the number of live decals must be justified by what each one costs. The cap is the game's
own decal pool cap (pool +0x34), 500 on both sides. Changing it is a base-game mechanic, so it belongs to this
performance mod. The co-op mod keeps its plain FIFO (b339), which holds at any cap. Its arrays (kDecalMax 512 in
`decal_c132.cpp`) must stay at or above the cap.

- Wire cost does not depend on the cap. Each gore decal is relayed once, about 117 B per decal with its E row
  (c263L337: 994 relayed, 116,726 B, 1,528 rows). A lower cap only sends E rows sooner, and there is
  at most one per decal at any cap.
- Still to measure: the frame cost per live decal on each side. Use one run with the existing test cap
  (`UBWCOOP_DECAL_CAP`): 100, 250 and 500 on the same fight, then compare GPU busy and the game thread frame time
  against the live decal count in each window. The cap is the largest count whose cost stays inside the frame budget.
  Below the point where the cost shows in the frame, the cap stays where it is.
- Possible byte cut (co-op mod, card 518 family): if the host and client pools hold the same decals in the same
  order, each client FIFO release would match the host's. The host's FIFO E rows (484 in L337) would then be
  redundant. This holds only when no local-only decals enter either pool, so measure first: count client FIFO
  releases that match the host E row that follows.
- Result (c263L339 cap 100, c263L340 cap 250, c263L341 cap 500; b339, FPS_CAP 60, 640x360, GPU busy sampled every
  1 s into logs/c263L33{9,40,41}.gpu1s): no decal cost is visible. In rounds 5-7, at about 100, 245 and 480 live
  decals, GPU busy reads 50-53, 48-53 and 48-61. The whole-run means are 48.2, 47.0 and 51.1. Round 1 at 0 live
  already spreads 39-61 between runs, so the noise is about 20 points and there is no slope above it. Frame means
  rise from 16.7 to 27-50 ms in rounds 4-7 at every cap while GPU busy falls: that is the game thread under the
  150-bug load (this card's main subject), not decals. Digest 0 and decal rounds 0 of 7 in all three runs.
- Decision: the cap stays at the game's 500. A lower cap buys nothing measurable at test resolution. A raise is not
  justified either: at 640x360 a decal's pixel cost is far below a play-resolution frame, so the headroom seen here
  does not prove room at 1440p. Revisit only if a play-resolution capture shows decal cost.
- Harness note: run_pair.sh already writes logs/<tag>.gpu every 10 s (card 252). A second sampler must write to
  another file (L339's .gpu was overwritten).

## Whole-frame profile (user: "get to work" on the main thread, 2026-11-20)

The NPC component ticks explain about a third of the 27-50 ms peak frames (L329: 31.34%). The other two thirds are
unknown, so no lever is chosen until a sampling profile names them.

- Tool: `tools/perf_frame.py`. Wine copies the exe into anonymous memory (.text file offset 0x600, RVA 0x1000), so
  perf cannot name exe samples itself. The tool subtracts the run's exe base (log row `hooks: exe base`) and maps
  each IP to the nearest preceding PDB public in `build/publics.txt`, then prints self samples per thread name by
  function and by owning class. Samples outside the exe are grouped by DSO.
- Run: one c263L peak run on the current lane 2 build, low footprint as usual. When the host log prints
  `digest round 4`, record each game process (not the launcher pid) for 20 s:
  `perf record -F 499 -p <pid> -o logs/<tag>-<side>.perf.data -- sleep 20`, then
  `perf script -i ... -F comm,tid,ip,dso > logs/<tag>-<side>.perf.txt`. Delete the .data files once the .txt is
  written.
- Predictions, written before the run:
  1. The game thread holds the largest share on both sides; the render thread is below it (GPU busy falls at the
     peak, so the frame is not GPU bound).
  2. On the host game thread, CharacterMovement plus AActor tick plus anim/skeletal mesh update together hold 30-45%,
     matching the c527 tick rows.
  3. At least one large slice outside those: candidates are Niagara component management (host scan counts 2,170
     components, 1,937 inactive), physics/overlap updates for the bugs, and the AI controller / behaviour tree tick.
  4. Unlikely but cheap to check: the 23,076 STNavModifierVolume actors the host digest leaves out. They should cost
     nothing per frame if they are static and not ticking.
- The tool is checked against these predictions; a prediction that fails is written here with the numbers.
- Static check on prediction 4: ASTNavModifierVolume has no Tick override in the PDB publics. Its only natives are
  Set/IsNavmeshAreaEnabled, and AVolume does not tick by default. So any cost from these actors would have to come
  from overlap tests on their brush components while bugs move (UPrimitiveComponent::UpdateOverlaps under
  CharacterMovement). The profile shows that as overlap/physics self time.
- The game's own NPC LOD tiers (static trace, 2026-11-20). ASTGameplayGameMode::Tick tail-calls
  UpdateSignificanceManager (exe+0x513AA6E to 0x051408C0). That function feeds the local PC's significance transforms
  (camera view plus up to 4 AddSignificanceLocation entries) to USignificanceManager::Update (vtable +0x2B0). Card 132
  already covers both players: the host detour adds each remote pawn as a location, and the client calls the
  function itself every frame. Each level of an NPC is an FSTNPCSignificanceLevel. Its super, FSTSignificanceLevel,
  holds DistanceThreshold, the cone check, bIsTickEnabled, TickInterval and TickIntervalRandomisationValue. The NPC
  level adds the Movement/Controller/StateMachine/TargetComponent tick intervals, MoveToTargetUpdateInterval and the
  bIsAllowedToMove/Attack/EvaluateAwareness, bIsTickAnimationPose and bUseSkeletalMesh switches. At the L341 host
  peak, 176 of 225 NPCs are at levels 0-1 (0.034 s). So the tiers already exist, but at a fight's range almost every
  bug is in the top tier. A tier lever means editing these values (perf mod, data only), not new code.
- Source (not built yet, waits for the profile run to finish): `c527_level_fields` logs every number member of each
  level once per class (`card527: <side> npc level fields <class> <k>: {...}`), so the next run gives the distance
  thresholds. Its TickInterval must equal the raw +0x20 value in the `npc levels` row.

### Profile result, c263L344 host (b339, 2026-11-20)

`logs/c263L344-host.perf.frame.txt`. Note: the junior's dump named `c263L344-client.perf.txt` is really the host
dump (33,545 samples). The client perf failed with "No such process" because the pid was wrong.
- Prediction 1 holds. The GameThread has 9,412 of about 9,980 possible samples (20 s at 499 Hz), so it is about 94%
  busy, and the frame is bound by the game thread.
- Prediction 2 is not scored: without callers, CMC cannot be split from the other Chaos query users.
- Prediction 3 holds, and the slice is physics. Chaos makes up about 26% of the GameThread: TAABBTree 16.9%
  (QueryImp sweep 7.8%, raycast 3.0%, overlap 1.8%, FindBestSibling insert 3.1%), plus TSQVisitor,
  FChaosEngineInterface (SetKinematicTarget 1.0%, UpdateActorInAccelerationStructure 0.4%), GJK and PrePreFilter.
  The insert and kinematic rows point at moving bodies being re-inserted into the acceleration tree every frame.
  Niagara is small (1.3%).
- Prediction 4 holds: no STNavModifierVolume rows.
- New finding: FindComponentByClass takes 4.3% of the GameThread and 11.0% of Background Work, through the save-id
  key of the targeting table. Carded as card 531.
- Next profile: record with `perf record -b` (AMD LBRv2, 16 branches; `-j any_call` returns empty stacks on this
  kernel) and dump `-F comm,tid,ip,dso,brstack`. Then run `perf_frame.py --callers QueryImp --callers FindBestSibling
  --callers FindComponentByClass` to name who issues the sweeps and the component scans.

### Mod dll share (2026-11-20, from c263L348)

- On the host GameThread, about 4.6% of samples fall in "[perf-<pid>.map]" and are not in the exe. Bucketed by
  64 KB, nearly all of them sit inside the mod dll (base 0x6FFFEFC20000).
- Our own code is therefore a top-10 GameThread cost.
- The mod had no symbols. From b345 on, the link writes a /MAP file; the DLL bytes do not change, only the PE
  timestamp differs. The map is kept as build-b5/dwmapi-bNNN-<hash>.map.
- perf_frame.py takes `--mod-base <proxy module> --mod-map <map>`.
- The c263L350 perf record on b345 names the mod rows. Card 534 (b344+) names the Chaos slice.
