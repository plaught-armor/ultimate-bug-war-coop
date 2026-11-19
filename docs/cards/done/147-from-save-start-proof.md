# Card 147: A from-save host start reaches a late client, and the digest runs after a rejoin

Status: queued 2026-11-08, split from card 137 run 4 (hb72) by the user. Budget: three runs.

Read: card 137 step 3 (journal reconcile after LoadFromSlot, its rules per journal), card 137 run 2 to 4 results,
docs/STATE.md "Card scope and investigation rules".

## Why

Card 137 built the two-way journal reconcile at LoadFromSlot (0x0520AE10). hb72 reconciled the possession save
("c98 K dropped 3 added 0, H fixed 0; c56 dropped 1 added 0; c95 doors fixed 0 added 0"), but the checkpoint zone
never saved, so the planned save-then-change-then-restore proof did not run. hb49 and hb68 zone saves came only when
the client's server copy was in the zone too (hypothesis: the zone needs every player, or skips re-saving the same
checkpoint). After the rejoin no digest round ran (hb72 lines 328/331), and `warp client` failed with "no server copy
of the client's pawn" (FindTeleportSpot found no room), as in hb68 line 80.

The hb72 missing digest and warp fail after the rejoin are likely the card 142 reliable overflow (the host closed the
client at 240878349); confirm against 142's fix before treating them as separate. Also from 137: nuke shot 2 never
fired in hb72 (drive line 129, one host FireProjectile for the run; cause untraced, grant at 240579709).

## Do

1. Static: BP_CheckPointSaveZone_C's save condition (all players, once per checkpoint). Why no digest round runs after a
   rejoin. Why the rejoined pawn has no server copy for the warp.
2. Fix the digest and warp causes if they are mod code; change the scenario to meet the save condition.
3. One run: save with world changes in it, more changes, in-place restore with no client, late client join (event
   gated on the reconcile line), then a rejoin. Predictions: the client has the save's state, reconcile counts match
   the post-save changes, and the first digest round after the join and after the rejoin is at 0 mismatches.

## Done when

- The traces, the fixes, the run lines. The usual Report items. It ends with "Report complete."

## Senior prep (2026-11-12, from c206-r1; no run)

Already shown by c206-r1 (DLL `862ff8865d50909a`, `tools/scenarios/c142-r4.txt`), so step 1 shrinks:
- Save with changes, in-place load, reconcile: "checkpoint save by BP_PlayerCharacterMI_C" at 117213687 and
  117248929, load at 117256000, "journal reconcile after LoadFromSlot: c98 K dropped 1 added 0".
- Digest after the rejoin: client rounds 3 to 5, 1496 against 1496, 0 mismatches.
- `warp client` after the rejoin: scenario line 105 done, client within 18 uu.
So "no digest round after a rejoin" and "no server copy for the warp" were the card 142 overflow, as the card
guessed. Left for this card: (a) a late first join (not a rejoin) after a from-save host start, with the client
holding the save's state; (b) the zone's own save condition (the saves above came from `host travel save`, not
from BP_CheckPointSaveZone_C); (c) nuke shot 2 (hb72 drive line 129).

Item (c) closed statically (2026-11-12, logs/hb72-host.log): a scenario fault, not mod code. Shot 1 (240578364)
emptied the launcher and the game dropped it; the regrant at 240579709 reads "regrant, held before
BP_Weapon_MoritaMk1_C, equipped BP_Weapon_MoritaMk1_C", so the Morita stayed in hand. The shot-2 press (after
host mark 91552) produced 31 "FireProjectile(BP_WeaponFireAbility_MoritaMk1_C" lines and none for the launcher.
A scenario that wants a second nuke must select the launcher after the regrant and wait on its equip line.
Left for this card: (a) and (b) only.

## Junior trace (2026-11-12, static and existing logs; no run)

### (b) BP_CheckPointSaveZone_C's save condition

Verdict: the game's own rule, not a mod bug. The hb49 and hb68 saves were not zone saves.

Blueprint side (bytecode, BP_CheckPointSaveZone.uasset), verified:
- ReceiveBeginPlay binds "Bound Save" to STLevelObjectiveSubsystem.OnLevelObjectiveComplete. Any objective's
  completion counts; the Objective parameter is not read.
- ReceiveActorBeginOverlap and EndOverlap call the parent, then `USTCombatUtilityLibrary::IsPlayer(OtherActor)` (the
  ASC tag Tag_Player_IsPlayer). A player's begin overlap sets `PlayerInside = true`, and any player's end overlap sets
  it false. It is one bool, so the last event wins. It is not a count of players.
- Bound Save: `!IsLoadInProgress && !IsSaveInProgress && !bIsSaveZoneEnabled && PlayerInside` arms a 5.0 s
  one-shot timer (AllowSave). AllowSave checks the same three flags again, then calls `SetSaveZoneEnabled(true)`.
- SaveComponent PreSave and SaveFullyLoaded: `bIsSaveZoneEnabled && PlayerInside` calls `SetSaveZoneEnabled(false)`
  and clears the timer.
- The FED_C2M1 level script also calls SetSaveZoneEnabled(true) directly on zones _6 (after LOS_O04,
  CombatIsland02_Dropship_Complete), 23, 10, 12, 14, 13, 18, 20, 0, 19 and 21.

Native side (disassembly), verified:
- ASTCheckPointSaveZone fields: +0x2c8 bIsSaveZoneEnabled, +0x2c9 bIsCheckPointSaved, +0x2b8 LoadPoints,
  +0x2a8 BoxComponent.
- OnBoxComponentOverlap (exe+0x052106B0): `!bIsCheckPointSaved && bIsSaveZoneEnabled && OtherActor IsA
  ASTPlayerCharacter` calls TrySave(OtherActor). Any ASTPlayerCharacter counts, so the server copy
  (BP_PlayerCharacterMI_C) counts as well.
- SetSaveZoneEnabled (exe+0x0521C5C0): writes +0x2c8. When it is true, it calls TrySave only when
  `GetSTPlayerCharacter(self)` (local player 0, which is the host's own pawn) is inside the box. It does not read
  bIsCheckPointSaved.
- TrySave (exe+0x0521EE90): the LoadPoint nearest the pawn by path distance, then
  `USTSaveSystemLibrary::Save_CheckPoint(world, data, true)`, then bIsCheckPointSaved = 1. Each zone saves once.
  The return address after that call is exe+0x521F166.

So a zone saves when two things are true. First, the zone is enabled: by the level script, or by an objective
completing while PlayerInside is true, and then 5 s pass. Second, the host's pawn is in the box at that moment, or any
ASTPlayerCharacter begins to overlap it later. Each zone saves once.

Logs, verified:
- The hb49 save at 206743315 came 5.62 s after AddObjective(LOS_O01) at 206737695, before the first warp
  (206752323).
- The hb68 save at 235672216 came 5.6 s after AddObjective at 235666600.
- The hb72 save at 240133925 came right after `ui continue`.
- All three are the mission-start save. None of them depended on the server copy being in a zone.
- In hb72, lines 51 to 58 warped the host alone into the box at 229312 38956 9100, and no save followed within 60 s.
  That zone was not enabled, or it had already saved.

Open: every logged save has caller exe+0x52186DF (Save_CheckPoint's PerformSave), so the original caller of
Save_CheckPoint is not logged. Settling line: the Save_CheckPoint detour (weapons.cpp detour_c50_save_checkpoint, in
the hooks "skipped" list in hb49, hb68 and hb72) logging its return address. exe+0x521F166 means TrySave.

Report line (the game's single-player design, not a fix): one player leaving the box clears PlayerInside while
another player is still inside it, so a later objective completion does not arm the zone. SetSaveZoneEnabled checks
only the host's pawn.

### (a) Late first join after a from-save host start

Verdict: hb72 already shows it, so no run is needed to find the fault. The client does not hold all of the save's
state. One table, the c95 collision journal, is full and is not covered by the reconcile.

First join, verified:
- Host: the only first-Pong replays of the map are at 240552804 (the join) and 240877493 (the rejoin).
- Client: the map end at 240004796 has an empty ledger (all counts 0), so it is the frontend. The load at 240289369
  ("load pawns 1, destroy actors 1, caller exe+0x520B3EF") happened with no client connected.

Replay at the join (hb72-host.log 74831 to 75599), verified:
- card049 objective frames, 16 of 16.
- card056 PickupGone, 3 of 3.
- card095 doors and collision, 256 of 256.
- card098 world, 238 of 238.
- card137 ext, 207 of 207.
- card136 fx, 85 journal rows of 86 (1 run-time target skipped).

Digest after the join, verified (hb72-client.log):
- Round 21 at 240573323: 30299 host actors against 30297 client actors, mismatches 2 (one corpse, one Pose).
- Rounds 22 to 30: mismatches 1 every round, the same actor each time:
  `mismatch BP_WarriorBug_Corpses_Destructible_01_C BP_WarriorBug_Corpses_Destructible_01_C_37 collision host 0
  client 1; last row seq 518 (WorldOp) sent t=240552815 applied t=240554659`.
- Row 518 is the c98 H row ("world H ..._37 196687 2214 7364 1: hidden now 1"). The client's K rows for both _37
  actors read "already dead, skipped", so no OnDeath ran on the client. The hidden flag arrived; collision did not.
- The same class of mismatch (corpse collision host 0, client 1) is in playtest-menu-20261108-191343.log at
  260501395 (_58, _57, _93), on the Continue route.

Cause: the c95 collision journal is full. Verified by arithmetic:
- c95_replay's count is the door ops plus the collision rows. hb72 has no door rows (no card161 lines and no DoorOp
  rows), so 256 means g_c95c.n = kC95Collisions = 256 (door_c95.cpp:48).
- At the cap, c95_collision_row returns null (door_c95.cpp:408). The write is still sent live to the clients
  connected then (none, after the load), but it is kept out of the journal. It logs "(journal full: not replayed)"
  only when a client is connected.
- The rejoin replay at 240877500 is also exactly 256.
- In the logs, 54 replays read "256 of 256 calls", so the cap is reached often across runs. It is not specific to
  this card.
- The first map's totals were already 165 collision rows (240010060).

Open: why the load adds so many rows. Rows are keyed by actor pointer (c95_collision_row compares `actor`), and the
load runs with destroy actors 1. Hypothesis: a respawned or reloaded actor gets a new row, and its old row stays.
Settling line: the "card095: host map totals: N collision rows, M dropped (journal full)" line, which is only written
at the next map arm, so hb72 never logged it for this map. A replay line that reports the dropped count settles it
in one run.

Tables the card 137 reconcile (save_c121.cpp:150 to 176) covers: c98 K and H, c145 E (c56 in the hb72 DLL), and c95
doors (DoorState only). Tables it does not cover:
- the c95 collision journal (the cause above);
- card049 objective frames;
- card098 world rows other than K and H;
- card136 fx journal rows;
- card137 ext rows.
In hb72, only the c95 collision journal gave a lasting mismatch.

## Plan

Fix (mod code, door_c95.cpp, one manager):
1. Key c95 collision rows by (name, component), not by actor pointer, so that a rewrite of the same placed actor
   after a load reuses its row. The row already stores the name.
2. At LoadFromSlot, add a c95 collision step next to c95_load_actor. For every collision row, read the actor's
   current flag (the actor-level bit, or the component's CollisionEnabled) and fix the row's value. Count it into a
   new "c95 collision fixed %u" field of the card137 reconcile line.
3. Report the dropped count in the replay line: "... %d of %d calls, %u collision rows dropped (journal full)".
   Raise kC95Collisions only if the run still shows drops. That is a static array, with no heap.

Scenario, one run, FED_C1M1 with FAST=1, host only at the start:
1. Host start. Wait for the card098 kill lines of the map-start corpses.
2. `host travel save`. Wait for "card146: host t=... checkpoint save by".
3. World changes after the save: kill two enemies. Wait for their card098 K lines.
4. In-place restore (the travel checkpoint, as in hb72). Wait for "card137: host t=... journal reconcile after
   LoadFromSlot".
5. Start the client and let it join. Wait for "card095: host t=... door and collision journal replay".
6. Wait for the client's first "client round N:" line, then one more round.
7. Disconnect and rejoin the client. Wait for the second replay line, then the next "client round" line.

For (b), a scenario that needs a real zone save waits for an objective completion with the host's pawn in the box,
or uses a zone the level script enables (FED_C2M1 _6 after LOS_O04), then waits on the Save_CheckPoint caller line
showing exe+0x521F166.

## Predictions

- The reconcile line after the restore reads "...; c95 doors fixed 0 added 0; c95 collision fixed N ...", with N
  greater than 0 (the corpses the load re-killed at 240290232 in hb72).
- The join replay line reads "door and collision journal replay ... X of X calls, 0 collision rows dropped (journal
  full)", with X less than 256.
- The client applies the corpse rows: "card095: client t=... collision BP_WarriorBug_Corpses_Destructible_01_C_...
  ...: applied, 1 -> 0".
- The client's first digest round after the join reads "mismatches 0" (a transient Pose line may appear in that round
  only), as does the first round after the rejoin.
- No "mismatch BP_WarriorBug_Corpses_Destructible" line in the client log.

## Phase B results (2026-11-12, runs c147-r1 and c147-r2; junior)

### Code

- `mod/src/ubw/door_c95.cpp`: collision rows are keyed by actor name, component and location (a same-named actor
  within 500 uu is the row's actor); no actor pointer is stored. `c95_load_collisions` runs after LoadFromSlot: each
  row takes its actor's current value, and a row whose actor or component is gone is removed. The replay and map totals
  lines report the row peak against the cap, the dropped count, and (added after c147-r1) the six name prefixes with
  the most rows. `kC95Collisions` raised from 256 to 512 after c147-r1 (peak 256 of 256, over half the cap).
- `mod/src/ubw/save_c121.cpp`: the card137 reconcile line adds "c95 collision fixed N, removed M (rows A -> B)".
- `mod/src/ubw/weapons.cpp`: the Save_CheckPoint audit line adds the caller address.
- `mod/src/ubw/hooks_internal.h`: the C137LoadCounts fields and the `c95_load_collisions` declaration.
- `tools/scenarios/c147-r1.txt`: new. After c147-r1, three waits were corrected: the barrel kill waits on the "kill
  relayed" line (barrels write no world K line); the card146 zone save fires on its own before the travel-save step,
  so its wait no longer uses since-mark; the bug kill waits on the command echo.
- DLL: c147-r1 ran `29ed4b45f9d50ed1` (cap 256); c147-r2 ran `01dd498d41403c1f` (cap 512, name tally).

### Results

Both runs: host rc 0, client rc 0, FATAL/ERROR 0, no crash dumps. The client's first Pong (c147-r2 t=128778272) comes
after the reconcile line (t=128589900), so the run is valid.

| Prediction | c147-r1 (cap 256) | c147-r2 (cap 512) |
|---|---|---|
| Reconcile "c95 collision fixed N", N > 0 | missed: fixed 0, removed 0 (rows 256 -> 256) | missed: fixed 0, removed 0 (rows 376 -> 376) |
| Replay "X of X calls, 0 dropped", X < cap | missed: 256 of 256, peak 256 of 256, 1087 dropped | missed: 512 of 512, peak 512 of 512, 695 dropped |
| Client applies a corpse row, 1 -> 0 | missed: no client apply line | missed: no client apply line |
| First digest round after join and after rejoin at mismatches 0 | missed: rounds 12, 13, 14 at mismatches 5 (BP_AISpawner_C) | met: rounds 12, 13, 14 at mismatches 0 |
| No "mismatch BP_WarriorBug_Corpses_Destructible" line | met | met |

The hb72 corpse mismatch did not appear in either run. In c147-r1 the five mismatches were BP_AISpawner_C_0, _17, _20,
_21 and _22: location host (4250,750,-2550) against client (0,0,800). These spawners are attached to
BP_ProceduralDropshipTroops_C, and on the host their relative offset changed from (0, 0, 800) to (4272, 769, -2534)
(card140 attach line at t=128057683). The fault did not recur in c147-r2.

### The journal still fills: the rows are not the hb72 class

c147-r2 row counts:

- 165 rows during the FED_C2M1 load, cleared at the map reset after LoadMap.
- 376 rows by the restore, about 140 s into play.
- 512 rows plus 695 dropped writes before the first join.
- The dropped count did not change between the first join and the rejoin, so no new rows came after the join.

Top row prefixes at the join: SkeletalMeshActor 57, BP_KillableCrate_01_C 49,
BP_DestroyableProp_GoreResponse_MoritaTrooperCorpse01_C 45, BP_KillableCrate_04_C 45, BP_KillableCrate_05_C 42,
BP_KillableCrate_06_C 36. None of these was touched by the scenario. All of these writes happened with no client
connected, so card095 logged none of them and their caller is not known. The digest showed no mismatch for any of these
classes, so both machines appear to make the same writes themselves.

This looks like local view state, like the significance writes card 128 excludes, but it is not proven. The
"significance changes kept local" count was 0 in both runs. Raising the cap does not settle it, because the number of
rows grows with play time.

### Uncovered tables (report only)

- card049 frames: no card049 total or WARN line in either side's log; no mismatch.
- card098 rows other than K and H: the world journal replay sent 292 of 292 at both joins; host tally 0 dropped; no
  card098 mismatch.
- card136: host fx net totals show 0 dropped in every bucket (journal peak 407); the client reports misses of 75 key
  and 217 target; no digest mismatch.
- card137 ext: client ext totals show 208 received, 208 applied, 0 drift, 0 missed; no mismatch.

### Save caller

The Save_CheckPoint lines show caller exe+0x521F166 with flag 0, which is ASTCheckPointSaveZone::TrySave (card146
anchor BP_CheckPointSaveZone_C). A second caller, exe+0x513E1F9 with flag 1, comes about 0.8 s earlier and has not been
identified.

### Not proven

- Who writes the 1207 collision values with no client connected (caller and reason).
- Whether the hb72 corpse row is replayed now. The run did not reproduce the corpse fault, so the in-place restore path
  has not been tested on a corpse collision change.
- The AISpawner attach offset fault seen once in c147-r1.

## Caller trace (2026-11-12, static: PDB, disassembly, c147 logs; junior; no build, no run)

Method: nearest PDB public symbol for each return address, disassembly around the call, vtable slot dumps of the
ISTSignificanceObjectInterface subobject, and the c147 and older logs. "Verified" means the disassembly or a log line
shows it; "open" means it is inferred.

Caution on the senior's tally: card098 collision lines are capped (362 lines in c147-r2) and c98_note logs only the
first 2 writes per (class, kind). The tally therefore counts first writers per class, not the writers of the 512 rows.
No row was sent with a caller in either run: all 1207 journal writes (512 rows plus 695 dropped) happened before the
client joined, and c95_relay_collision logs only when a client is connected.

### 1. The eight callers

| Caller | Function | Chain to the tick | Kind |
|---|---|---|---|
| exe+0x4C5FE0C | AADPickupBase constructor (+0x2dc) | level load or stream-in, object construction | construction (both machines, same value) |
| exe+0x4C618A1 | AADPickupBase::OnUpdatePickupStatus (+0x31) | pickup status change (hooked by card 145); callers not traced | open: world state when a pickup is taken, view state if it is the pickup's significance status |
| exe+0x517A474 | ASTNonPlayableCharacterBase constructor (+0x734) | spawn or pool construction | construction |
| exe+0x51BA9D1 | ASTWeaponPickup constructor (+0x1c1) | level load, construction | construction |
| exe+0x5165410 | ASTDamageableActorBase::OnSignificanceChanged (+0x360), the `call [rax+0x768]` at 0x516540A | game mode tick, UpdateSignificanceManager (detour_c132_update_significance), USignificanceManager::Update, PostSignificanceUpdate (slot +0x28), ProcessSignificanceUpdate (slot +0x30), OnSignificanceChanged (slot +0x38) | view state; hooked (index 0), so these writes go to `local`, not to rows (verified by code; the C2M1 local count was never printed) |
| exe+0x51BA247 | ASTTacticalSupportPickup constructor (+0x177) | level load, construction | construction |
| exe+0x37C2C74 | UPrimitiveComponent::execSetCollisionEnabled, the Blueprint thunk | a Blueprint event; which event is open | open: depends on the Blueprint |
| exe+0x5226FFB | ASTProjectileBase constructor (+0x27b) | projectile spawn | construction |

Other callers met in the trace:

| Caller | Function | Kind |
|---|---|---|
| exe+0x380231B | Blueprint thunk; the nearest public is execSetActorHiddenInGame, but the `call [rax+0x368]` it returns from enters the SetActorEnableCollision detour directly, so it is the SetActorEnableCollision thunk under a folded name (verified: return address and slot) | Blueprint; see 2 |
| exe+0x51491C9 and exe+0x51491D9 | ASTDestroyableProp::ActorFullyLoaded(USTSaveComponent*) | save load; see 2 |
| exe+0x517150A | ASTDestroyableProp::StartPropDestroy_Implementation | damage (world state) |
| exe+0x5054B36 | ASTDestroyableProp::execOnDamageTaken | damage (world state) |
| exe+0x519FB05 | ASTEnemyBase::PersistentActorActivate (+0xd5) | persistent actor activation (12 lines just before the r2 save); open |
| exe+0x512940C | ASTEncounterManagerBase::InitialiseWave (+0x17c) | encounter script (world state) |
| exe+0x513E1F9 | ASTSaveableLevelScriptActor::TriggerSave (+0x219) | the flag-1 Save_CheckPoint caller (card 147 audit) |

Significance call order (verified from disassembly and vtables): PostSignificanceUpdate ends with `jmp [rdx+0x30]`,
which is ProcessSignificanceUpdate. ProcessSignificanceUpdate calls `[rax+0x38]` at 0x516A823 (the class's
OnSignificanceChanged, where the six hooks sit) and then, after that call has returned, calls
ISTSignificanceObjectInterface::Execute_BP_OnSignificanceChanged at 0x516A832 (the Blueprint event). No class
overrides slot +0x30: the vtables of ASTAISpawner, ASTAmmoPickup, ASTAttackSystemEnemyBase, ASTCustomPickupBase,
ASTDamageableActorBase, ASTDamageableActorCorpseBase, ASTEnemyBase, ASTGorePartBase, ASTProceduralDropship,
ASTProceduralDropshipTroops, ASTWeaponPickup, ASTNonPlayableCharacterBase, ASTShootableButton and ASTSpawnerBase all
hold 0x516A630 there. ASTAISpawner and ASTSpawnerBase have no OnSignificanceChanged override in slot +0x38, so none of
the six hooks runs for them at all.

### 2. The three top row classes

- **SkeletalMeshActor (57 rows).** Not an engine skeletal mesh actor: the row name is the level name of placed
  BP_DestroyableProp_GoreResponse_MoritaTrooperCorpse01_C actors (verified in hb48 and c132l2 client lines such as
  "collision SkeletalMeshActor_108 ... 1 <CapsuleCollider> (BP_DestroyableProp_GoreResponse_MoritaTrooperCorpse01_C):
  applied, 3 -> 1"). Same writer as the next row.
- **BP_DestroyableProp_GoreResponse_MoritaTrooperCorpse01_C (45 rows).** Writer exe+0x51491D9,
  ASTDestroyableProp::ActorFullyLoaded(USTSaveComponent*) (verified, c147-r2 card098 lines). Disassembly: after
  LoadAbilitySystemComponent restores the saved ability state, a bool test on the prop's component at +0x388 picks
  SetCollisionEnabled(0) (return 0x51491C9) or SetCollisionEnabled(1, QueryOnly) (return 0x51491D9). Depth is 0
  because this is the save component's load callback, a non-significance path. It is world state from the save: the
  client never loads a save and keeps the level default 3 (QueryAndPhysics), and the older client lines show the
  replay changing 3 to 1. These rows must stay in the journal.
- **BP_KillableCrate_* (49, 45, 42, 36 in the top six; 02 and 03 also present).** Two writers in the first-writer
  lines: exe+0x5165410 on the StaticMesh component (inside the hooked OnSignificanceChanged, so counted local, not a
  row) and exe+0x380231B on the actor flag (12 lines each, verified). The actor flag write comes from a Blueprint. The
  verified call order above runs Execute_BP_OnSignificanceChanged after the hooked OnSignificanceChanged has returned,
  so a write from that Blueprint event sees depth 0 and is journalled. That the crate Blueprint making the write is
  BP_OnSignificanceChanged is open (the Blueprint bytecode was not traced); it fits the evidence: the same crates, the
  same moment as their significance writes (t=128569984 to 128569985), and a host standing still with no client.
  Answer: a write after the hook returned, inside the same ProcessSignificanceUpdate call. View state.

### 3. Proposed class rule

Replace the six OnSignificanceChanged hooks with one hook on ISTSignificanceObjectInterface::ProcessSignificanceUpdate
(0x516A630) that raises g_c95_significance_depth around the original. It is the single slot +0x30 implementation for
every significance class, it contains both the C++ override and the Blueprint event, and it also covers direct
Blueprint calls through execProcessSignificanceUpdate (0x509A9D0), which a wrap around detour_c132_update_significance
and client_significance_step would miss. The same hook runs on the host and the client.

What it would wrongly keep local: any world-state collision write that an OnSignificanceChanged override or a
BP_OnSignificanceChanged event makes. None is known; such a write would be a game bug (view-dependent gameplay).
What it would still journal: collision written later by ticks that ProcessSignificanceUpdate enables or retimes
(SetTickFunctionEnable, UpdateTickIntervalAndCoolDown in its body); whether any tick writes collision is open.

Second class, not covered by this rule: construction writes (the AADPickupBase, ASTWeaponPickup,
ASTTacticalSupportPickup, ASTNonPlayableCharacterBase and ASTProjectileBase constructors). Both machines construct
the same placed actors with the same values, so these rows are redundant, not wrong. The map totals line before
C2M1 play showed 165 rows of this kind at load (BP_Dropship_LandingBlocker_C 20, BP_GrenadeAmmoPickup_Frag_C 16,
BP_TacSuppPickup_BombingRun_C 16, BP_DestructibleSpawner_Bugnest_HumanGameplay_C 14, BP_HealthPickup_Medium_C 13,
BP_ArmourPickup_Medium_C 9).

### 4. Cap (proposal; door_c95.cpp is not in this step's files)

World-state rows alone: the save-load corpse rows (57 + 45 seen in the top six, other corpse classes unknown) plus
damage and script rows (a few per kill). That is about 100 to 130, at or under half of 256. By the senior's criterion
kC95Collisions goes back to 256. But with only the rule in 3, the construction rows stay: about 165 at load plus the
corpse rows is about 270 to 300, over 256. So revert to 256 only together with a construction exclusion; with the
rule in 3 alone, keep 512. The name keying, the load rebuild, the peak and the tally lines stay either way.

### 5. Scenario sleeps (proposal; c147-r1.txt is not in this step's files)

Each command already logs "cmd: N <command> -> done" on the host, so a mark before the command and a wait on its done
line replaces most sleeps.

| Line | Sleep | Replacement |
|---|---|---|
| 16 | `sleep 3` after the first "save engine save runs" | none needed: the next step (warp) does not depend on the save finishing. Remove. |
| 19 | `sleep 2` after the warp | `host wait log "cmd: \d+ warp actor point 196820 3240 8133 -> done" since-mark` (mark before the warp) |
| 23 | `sleep 2` after the second barrel kill | mark before the kill, then the line 21 wait with since-mark |
| 30 | `sleep 3` after the save waits | none needed: both save lines are already waited on. Remove. |
| 35 | `sleep 3` after the spawns | mark before the spawns, then `host wait log "cmd: \d+ spawn bug 197020 3640 8200 90 -> done" since-mark` |
| 40 | `sleep 2` after the bug kills | none needed: line 39 waits on the kill done line. Remove. |
| 65 | `sleep 2` before host quit | `client quit` has no host-side line in r2's tail (none found for the client leaving); keep as a fixed delay, it is not a state-dependent step |

The line 32 barrel kill has no wait after it; add the kill relayed wait with a mark, as at line 21.

### Predictions for run 3 (if the rule in 3 is built, cap 512, same route)

- Replay line: row peak about 270 to 300 of 512 (165 construction plus the corpse rows plus a few kill rows), 0
  dropped. If kC95Collisions is 256 without a construction exclusion, the peak reaches 256 and rows drop: that is
  the reason for keeping 512 with the rule alone.
- The crate actor-flag writes go to local: no BP_KillableCrate_* row in the tally.
- Digest rounds after the join and the rejoin: 0 mismatches.
- To check the local count for C2M1, the replay line should print it (the map totals line prints it only at the next
  map's arm). This is a one-line log change in the same build.

### Corrections

- The Phase B report said "significance kept local 0". That value (r2 host line 2103) is the previous map's total,
  printed at the C2M1 arm; the C2M1 local count was never printed.

## Run 3 build (2026-11-12)

### Construction rule: the state bit, verified statically

FObjectInitializer::PostConstructInit (exe+0x1574430) ends with `mov edx,0x200; call UObjectBase::AtomicallyClearFlags`
at exe+0x1574909. It is reached from ~FObjectInitializer (exe+0x1568790), after the class constructor returns. So while a
constructor, or a component constructor inside it, runs, the owner's ObjectFlags (offset 0x8) has RF_NeedInitialization
(0x200) set. ASTDestroyableProp::ActorFullyLoaded (the write at exe+0x51491D9) runs on a live actor after its save
component loads, when 0x200 is long clear. The bit separates the two, so the corpse rows from the save stay.

Rule: in c95_relay_collision, after the placed and replicated checks and the significance check, a write whose owner has
0x200 set is counted as "construction" and is neither journalled nor relayed.

### Significance rule

One hook on ISTSignificanceObjectInterface::ProcessSignificanceUpdate (exe+0x516A630, new tools/symbols.txt entry) raises
g_c95_significance_depth around the original, on host and client. The six per-class OnSignificanceChanged hooks, their
install lines and their six symbols are removed; nothing else referenced them. The regenerated offsets.generated.h
differs from before only by the new entry and the six removed entries.

### Item 4: AADPickupBase::OnUpdatePickupStatus (exe+0x4C618A1), closed

OnUpdatePickupStatus calls IsPickupEnabled (vtable +0x760), then SetCollisionEnabled on the component at +0x300 (return
exe+0x4C618A1), then SetVisibility. Its callers are DisablePickup, SetPickupEnabled and every override's call of the base.
That is world state (the pickup taken or enabled), and the host already relays bEnabled as card 145 'E' rows, so the
write stays journalled. A pickup's BeginPlay may run it with a value different from the constructor's, so pickup rows may
still appear.

### Diagnosis added

Each journal row stores the exe RVA of the write that added it. The replay line and the map-totals line print the top 6
first-write callers by row count ("rows by first caller:"), and the map's "local" and "construction" counts (the replay
line on FED_C2M1 gives the C2M1 values; the map-totals line prints them before the reset). kC95Collisions is 256 again.

### Run 3 predictions (senior's, written before the run)

1. Row peak at join at most 130 of 256.
2. 0 dropped.
3. No BP_KillableCrate_* and no pickup-constructor class in the row tally. Pickup rows may appear only with
   OnUpdatePickupStatus (exe+0x4C618A1) as first caller; none with a constructor as first caller.
4. Corpse rows present, and the reconcile line showing the save's values.
5. Digest 0 mismatches after the join and after the rejoin.
6. No new WARN class on either side.

### Run 3 results (c147-r3, DLL 447d59fbfd989e83)

- The ProcessSignificanceUpdate hook is installed on both sides. The C2M1 replay line shows 834 local writes and 0 construction writes.
- Predictions 1-3 missed: peak 256 of 256, 1087 dropped, and BP_KillableCrate_01/04/05/06_C in the row tally.
  - Rows by first caller: exe+0x37C2C74:184, exe+0x51491D9:71, exe+0x5164C6B:1.
  - exe+0x37C2C74 is UPrimitiveComponent::execSetCollisionEnabled, a Blueprint call, and its owners do not carry RF_NeedInitialization (0 construction writes).
  - So the crate writes are not C++ constructor writes. The Blueprint function that makes them is not named in the log; the per-call card095 lines reached their cap before the C2M1 load.
- Prediction 4 met: MoritaTrooperCorpse01_C and SkeletalMeshActor rows are present (first caller ActorFullyLoaded exe+0x51491D9, 71 rows). Reconcile line: "c95 collision fixed 0, removed 0 (rows 256 -> 256)".
- Prediction 5 met: digest rounds 12, 13 and 14 show mismatches 0.
- Prediction 6 missed on the client: six WARN classes not in r2 (card137 corpse arrival for BP_AI_WarriorBug_C and its map totals, no stream row, ragdoll wait table totals, card132 rows not acked, card177 sprite death totals). The host has no new WARN class.
- Scenario: line 35 failed. The third barrel kill (cmd 6, InstaKillTarget) printed no card098 Health line.
- C1M1 map totals: 165 rows, 0 dropped, 0 local, 0 construction. Rows by first caller: exe+0x4C618A1 (OnUpdatePickupStatus):104, exe+0x37C2C74:37, exe+0x51D25D2 (ASTCustomPickupBase::PersistentActorActivate):21, exe+0x4C6118D (DisablePickup):3.

## Senior review (2026-11-12)

Accepted in part, budget of three runs used. DLL `447d59fbfd989e83`, run c147-r3.

- (b) second nuke and checkpoint: closed as the game's own rule (one save per zone), no mod fault.
- (a) kept: collision rows keyed by name, component and location (no actor pointer); rows rebuilt after
  LoadFromSlot; one hook on `ProcessSignificanceUpdate` in place of six per-class hooks (834 view writes kept local
  on FED_C2M1); constructor writes not journalled; peak, dropped, class and first-caller tallies; checkpoint caller.
- (a) not met: the journal still fills (256 of 256, 1087 dropped). 184 rows come from a Blueprint call
  (exe+0x37C2C74) on crates, not named. The digest read 0 mismatches after the join and the rejoin in r2 and r3
  with rows dropped, and the hb72 corpse mismatch did not reproduce. Card 219 takes the journal.
- Line 35 failure is a scenario fault: the kill landed 11.5 s after the command (the save was still running),
  past the 10 s timeout.
- Client WARNs (corpse arrival not posed, no stream row, rows not acked) are the corpse classes held behind
  card 215 and the card 140 ack class; not caused by this change.
