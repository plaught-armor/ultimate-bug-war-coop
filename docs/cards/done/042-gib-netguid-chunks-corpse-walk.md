# Card 042: Gib signal by NetGUID, gib chunks on the client, corpse walk test

Status: done 2026-11-03.

Read `docs/STATE.md` first ("Mod code rules", "Runtime facts"), then card 039 (the `ChanOp::Gib` signal, the client death burst, corpse collision) and card 041 (the `ChanOp::Anim` NetGUID route). Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

Card 039's `ChanOp::Gib` names the bug by a position cell. The cell wraps every 32768 uu, and the client takes the nearest NPC within ±1 cell, so two bugs dying close together can gib the wrong one. Card 041 showed that the NetGUID names an NPC exactly on both machines (GUID 0x19e matched on all 18 frames). This card moves Gib to the same route. It also takes the two items card 039 left open: the flying gib chunks, which the client does not show, and a walk test through a corpse on the client.

## Static leads (senior, PDB publics and disassembly)

- **The chunk gate.** In `SpawnGoreResponse`, the `GetGameMode` call at 0x14524FE3A must return an `ASTGameplayGameMode` (an IsA check against `ASTGameplayGameMode::GetPrivateStaticClass` 0x050603A0 follows). Otherwise it jumps to 0x1452500E4 and skips the parts. The GameMode pointer is captured into a deferred action (vtable 0x148150878) on `USTDeferredActionWorldSubsystem`.
- **The deferred action** copies each `FSTGoreResponsePart` (0xC8 bytes, copy constructor 0x052273A0). Its body is 0x052282E0. It looks up a `TSet` keyed by `TSubclassOf<USTGorePartDataBase>` at `[rsi+0x3A0]` (lookup `FindIndexByHash` 0x05222670). `rsi` is probably the captured GameMode; check that.
- **The chunks are gore-part actors**, not `ADProjectile`s: `ASTGorePartNonSticky_{Sprite,StaticMesh,SkeletalMesh}` and `ASTGorePartSticky_*`, built from `USTGorePartData_{NonSticky,Sticky}Physical_*` data, pooled by `USTGorePartPooler`. So the set at +0x3A0 is probably part-data class to pooler.
- The client has no GameMode, so the gate always fails there.

## Do

1. **Gib by NetGUID.** Change `ChanOp::Gib` to the card 041 layout: three arguments, GUID bits 0-13, bits 14-27, and a flag for the cell fallback. Reuse `c41_package_map`, `c41_guid` and the `c41_resolve` GUID route. Keep the cell route only as the fallback for GUID 0 or ≥ 2^28. Log the route that was used and count both.
2. **Chunks: find the path** (time box: one run plus the static work). Answer:
   - What `rsi` is in 0x052282E0, and what the set at +0x3A0 maps to.
   - Which gore-part class the WarriorBug's death and overkill responses use, and whether that actor has `bReplicates`.
   - What the pooler does to spawn and launch a part (its spawn call, the initial velocity, and the lifetime).
   - Whether the client can spawn the same part locally with no GameMode. For example: build a `USTGorePartPooler` of its own and call the same spawn function, or `SpawnActor` the part class and run its launch function. Name the functions you would call.
3. **Chunks: port them** if step 2 finds a local-only path that needs no GameMode, AI or score logic. Spawn the parts on the client from the Gib receive, with the same response data the host uses. If no such path fits the time box, stop after step 2 and report the plan.
4. **Corpse walk test.** In a run, kill an AI trooper (these corpses persist; bugs are destroyed after about 107 ms). Have the client walk its pawn through the corpse with autoinput. Log the client pawn's position each 100 ms while it crosses, and whether movement is blocked (`HitResult` or a speed drop). Do the same on the host.
5. **Run.** A 2-player run with at least two bugs overkilled close together (within 2 cells, one after the other within 1 s) and one trooper corpse. Report:
   - the GUID and cell counts, and whether every Gib matched the bug the host killed (compare positions);
   - the chunk answers, and screenshots of the client if chunks are ported;
   - the walk test result on both sides.

## Done when

- Gib resolves by NetGUID, with 0 wrong matches in the close-together test. The cell fallback stays in place and is counted.
- The chunk path is answered (steps 2 and 3), and the chunks are ported or the plan is reported.
- The walk test shows whether the client pawn passes through a corpse, with log lines.
- Client game-logic counts stay 0 (score, ticket, drop, AI).
- Clean build (`/W4 /WX`, `--clean-first`), 84 exports, KERNEL32-only imports. No new crash dumps. Every new function is at most 60 lines. Write `card042x-run.txt` with the rc.

## Risks

- An overkilled bug is destroyed on the host about 107 ms after the kill. If its NetGUID is already unmapped on the client when the Gib arrives, resolving it fails. Count that case and fall back to the cell.
- Gore parts may simulate physics and hit things. A client-only part must not deal damage or overlap triggers that run game logic. Check `ASTGorePartBase::OnComponentHit` before spawning parts.
- Pooled actors need a pool owner. If the client creates a pooler, it must also clean it up when the map changes.

## Senior addendum (2026-11-03): where the gore poolers come from

Read from the disassembly after the card was sent. This answers most of step 2's static questions.

- **`ASTGameplayGameMode::DeferredBeginPlay` 0x0511C6B0 builds six poolers.** For each kind (NonSticky and Sticky, each in SkeletalMesh, SpriteRenderer and StaticMesh), it:
  1. constructs the pooler with `StaticConstructObject_Internal`, with the GameMode as Outer (for example `USTActorPoolerGorePartNonSticky_SkeletalMesh::StaticClass` 0x05022450);
  2. stores it on the GameMode at +0x348 to +0x370, one slot per kind;
  3. calls `USTActorPoolerMultiplatformBase::Initialise(UClass* partActorClass, int count, AActor* owner)` 0x051CC560, with the part actor class (for example `ASTGorePartNonSticky_SkeletalMesh::StaticClass` 0x05065E50), a count read through the pooler's vtbl+0x2B0 (a console variable), and the GameMode as owner;
  4. adds `{part data class, pooler}` to the `TMap<TSubclassOf<USTGorePartDataBase>, USTGorePartPooler*>` at **GameMode+0x3A0** (`Emplace` 0x0510DC80). Keys are `USTGorePartData_{Sticky,NonSticky}Physical_{Sprite,StaticMesh,SkeletalMesh}`.
- **The deferred action body** 0x052282E0 reads the GameMode from its capture (`rsi = [r15]`), looks up the part's data class in +0x3A0, and gets a part from that pooler.
- **Spawn entry point:** `USTGorePartPooler::GetGorePartInstance(TSubclassOf<USTGorePartDataBase>, FVector, FRotator, float, AActor*)` 0x051C8400 (each pooler kind overrides it, 0x051C7EC0 to 0x051C8320). The part then runs `InitialisePart` 0x05127FD0 (the NonSticky and Sticky overrides call it), `ApplyInitialImpulse` 0x051172D0 and `ApplyInitialRotationAndAngularImpulse` 0x051173B0 (or `ImpartInitialRotationAndImpulse` 0x03004800).
- **A client plan to check in step 2:**
  1. On the client, build the poolers that the WarriorBug's parts need, the way `DeferredBeginPlay` does: same pooler class, same part actor class, same count. Use a client-side owner, for example the local PlayerController or the GameState; there is no GameMode.
  2. Keep the mod's own `{data class, pooler}` table, and rebuild it on each map load.
  3. On a Gib receive, for each `FSTGoreResponsePart` in the response, call `GetGorePartInstance` with the same arguments the deferred action passes. Log those arguments on the host first.
  4. Before spawning, check that the part actor classes have `bReplicates` 0 (log it), and read `ASTGorePartBase::OnComponentHit` (0x01162D20 is a folded stub, so it is probably empty).
- Check this against the host: log the six slots at GameMode+0x348 to +0x370 and the entries of the map at +0x3A0 once after load, along with the pooler and part class names.

## Report

Result: **done.** The Gib travels by NetGUID with 0 wrong matches. The client spawns gib chunks with its own gore-part poolers, and a walk through a trooper corpse is not blocked on either side.

### Runs

The pair command is card 026c with `UBWCOOP_C42_TEST=1` on both sides (plus `UBWCOOP_AUTODROPSHIP=1` on the host). The run log `logs/card042x-run.txt` has rc=0: "ended by done after 114 s; host rc 0, client rc 0", FATAL/ERROR 0, WARN 0, no new crash dumps.

There were two runs:
- **First run.** Gibs and chunks passed, but the host's corpse target (a `BP_AI_Trooper_Morita_C`) was gone 1.5 s after the kill: the Morita, SquadLeader and Medic troopers do not leave a body. The host walked over an empty spot. The client never saw that corpse either; it chose a NonCombat corpse 2048 uu away and ran out of walk time.
- **Fix.** `c42_stage_move` now falls back to the nearest dead trooper that still exists. It logs "first corpse is gone; new target ...".
- **Second run.** The logs are `logs/card042x-{host,client}.log`. Every number below comes from this run.

### 1. Gib by NetGUID

`ChanOp::Gib` now takes three arguments: GUID bits 0-13, GUID bits 14-27, and the cell. The GUID comes from `c41_guid`. When the GUID is 0 or ≥ 2^28, both GUID arguments are 0 and the client uses the cell route; that is counted as `cell fallback`. The client also resolves the cell route for every Gib. It counts `routes differ` when the two routes disagree and `wrong` when the actor it picked is not the one the GUID names. A repeated GUID is skipped and counted.

| | sent by guid | sent by cell | recv by guid | recv by cell | guid unmapped | cell fallback | wrong | routes differ | repeats |
|---|---|---|---|---|---|---|---|---|---|
| host | 2 | 0 | | | | | | | |
| client | | | 2 | 0 | 0 | 0 | 0 | 0 | 0 |

Close-together test:
- The host spawned two WarriorBugs 900 uu ahead and overkilled them with `InstaKill(bug, true)` 373 ms apart.
- Bug 0 had guid 0x1a6 in cell 2832 at (5885, 4151). Bug 1 had guid 0x1b0 in cell 2960 at (6113, 4135), 228 uu away and inside the 2-cell window.
- The client resolved each Gib to the same `BP_AI_WarriorBug_C` by both routes.
- The GUID was still mapped when the Gib arrived (6-8 ms after the send), so the unmapped-GUID fallback path was not exercised in this run. It is counted and falls back to the cell route if it ever happens.

### 2. Chunk path (static work, then confirmed at runtime on the host)

- **`rsi` in 0x052282E0** is the `ASTGameplayGameMode` that the deferred action captured (`rsi = [r15]`).
- **The +0x3A0 map** is a `TMap<TSubclassOf<USTGorePartDataBase>, USTGorePartPooler*>` with element stride 0x18: key at +0, value at +8.
  - The key is the native data class: the SuperStruct (+0x40) of the response part's blueprint data class.
  - The host log has 6 entries, matching the six GameMode slots +0x348 to +0x370 one to one:
    `STGorePartData_{Sticky,NonSticky}Physical_{Sprite,StaticMesh,SkeletalMesh}` → `STActorPoolerGorePart{Sticky,NonSticky}_{SpriteRenderer,StaticMesh,SkeletalMesh}`.
- **Part classes:**
  - The six part classes are `STGorePart{Sticky,NonSticky}_{Sprite,StaticMesh,SkeletalMesh}`, and all of them have **bReplicates 0** (read from the CDO on the host and on the client).
  - Trooper death bursts use NonSticky Sprite parts (`BP_GoreParts_*Trooper_*`).
  - The WarriorBug overkill uses NonStickyStaticMesh and StickyStaticMesh parts; the client built exactly those two poolers on the first bug Gib.
- **Response layout:**
  - The parts TArray is at data+0x18, stride 0xC8.
  - Each part holds:
    - +0 the data class;
    - +8 the offset;
    - +0x20 min, +0x24 extra, +0x28 chance percent;
    - three random-lerp vectors A (+0x30), C (+0x60, rotation) and B (+0x90), each with its high value 0x18 further on;
    - a float range at +0xC0/+0xC4.
  - The count is min + extra. Parts past min are rolled against chance.
- **Spawn and launch:**
  - Spawn is pooler vtbl+0x2B8 (`GetGorePartInstance`) with `(pooler, &dataClass, &location, &ZeroRotator, float, nullptr)`. The float is drawn from the +0xC0/+0xC4 range; it was 1.00 for every part seen. It is probably the lifetime or the scale; that is not proven.
  - The launch is part vtbl+0x750 with `(part, &A, &B, &C)`: the initial impulse, a second vector, and the rotation/angular impulse.
  - B came out as (-100, -100, -100) on every NonSticky sprite part seen. It looks like a fixed value in the data, not a velocity.
  - The pool size comes from pooler vtbl+0x2B0 (config, int at +0x10) and is 100.
- **The client needs no GameMode.** It works like this:
  1. The client constructs the same pooler class with `StaticConstructObject_Internal`, with the local PlayerController as Outer.
  2. It calls `USTActorPoolerMultiplatformBase::Initialise(partClass, count, PlayerController)`.
  3. It calls the same spawn and launch virtuals.
- **Damage:** `ASTGorePartBase::OnComponentHit` is the folded stub at 0x01162D20 (senior read), so client parts deal no damage. No game-logic counter rose on the client.

### 3. Chunk port

`detour_spawn_gore` now copies the location and rotation before calling the original, because the original adds the data+0 offset to `*location` in place. When the client's own call fails the GameMode gate, `c42_client_chunks` replays the parts from the same response data:
- The kind comes from walking the data class's super chain (at most 8 levels).
- The pooler is held weakly per kind. It is built on first use, at most 3 builds per kind per map, with the count clamped to 1..24 (the host's is 100).
- The budget is 32 parts per response.

`c42_arm_map` resets the table on every map load. The poolers have the PlayerController as Outer and go away with the world.

Client totals: responses 24, parts 110, attempts 96, spawned 96, null 0, no kind 0, no pool 0, pool builds 3 (NonStickySprite, NonStickyStaticMesh, StickyStaticMesh). The 24 responses are the trooper death bursts plus the two bug gibs.

Screenshots in `logs/card042-shots/` were taken on the client, facing the gibbed bug:
- `client-gibA-250ms-00812.png` and `client-gibA-900ms-00814.png` are bug 0 at +250 and +900 ms.
- `client-gibB-250ms-00813.png` and `client-gibB-900ms-00815.png` are bug 1.
- Shell pieces and green giblets are in the air around the client pawn.

### 4. Corpse walk

The test holds W for 2.5 s toward a dead `BP_AI_Trooper_NonCombat_C` and logs a row every 100 ms. A row counts as blocked when the speed is below half the run's maximum after 300 ms.

| side | start → corpse | rows | blocked | max speed | slowest within 150 uu | result |
|---|---|---|---|---|---|---|
| host | 400 uu | 23 | 0 | 710 | 694 | passed over the corpse (2 uu from it at +640 ms), ended 1321 uu beyond |
| client | 418 uu | 23 | 0 | 709 | 695 | passed over the corpse (8 uu from it at +675 ms), ended 1341 uu beyond |

The host teleported the client pawn to its own start, 120 uu to the side, and the move held. Neither pawn slowed near the corpse.

### Checks

- Client game-logic counts are 0: no `client game-logic` lines and no score, ticket, drop or AI calls.
- The build is clean with `/W4 /WX` and `--clean-first`. `run_pair.sh` verified 84 exports and KERNEL32-only imports at deploy. Every new function is 60 lines or fewer.
- The new flag is `UBWCOOP_C42_TEST`, which runs the scripted test (host timeline from foot + 20 s; client walk on a ping). The chunk port and the GUID Gib are always on.

### Open

- The unmapped-GUID fallback is untested live (count 0).
- The float argument's meaning (lifetime or scale) is not proven.
- The client pools 24 parts per kind against the host's 100; a large battle may recycle chunks sooner on the client.

## Senior review (2026-11-03)

Accepted. Checked:

- `logs/card042x-run.txt`: build `42ed66bbc9516cab`, done after 114 s, host rc 0, client rc 0, FATAL/ERROR 0 and WARN 0 on both sides, no new crash dumps.
- Totals lines: the host sent 2 by GUID and 0 by cell. The client received 2 by GUID, with 0 wrong, 0 routes differ, 0 GUID unmapped and 0 cell fallback. Chunks: 24 responses, 110 parts, 96 attempts, 96 spawned, 0 null, 3 pool builds. The client log has no game-logic lines.
- Two bugs 228 uu apart, overkilled 373 ms apart, each resolved to its own GUID. That is the case the cell match could get wrong.
- `client-gibA-250ms-00812.png` shows the gore cloud with flying chunks on the client.
- The function-size scan finds nothing over 60 lines.

Open:

- 110 parts against 96 attempts: 14 parts were not tried. The report does not say why (probably a spawn chance or count in the part data). Explain it with a log line in a later card.
- The meaning of the spawn float (1.00 for every part seen).
- The client pool is 24 per kind against the host's 100.
- The poolers are held weakly. If GC collects one mid-map, it is rebuilt, up to 3 builds per kind; after that, chunks stop for that map. Rooting them (`AddToRoot` with removal on map change) would be safer if chunks go missing in long maps.
- The `card038` "host game-logic" lines show that Morita, SquadLeader and Medic corpses do not persist, so the walk test used a NonCombat corpse.
