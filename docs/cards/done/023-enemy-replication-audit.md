# Card 023: Enemy replication audit (milestone 3 start)

Status: sent 2026-11-03.

Read `docs/STATE.md` first: the "Mod code rules" section, the "Runtime facts" line about `tools/run_pair.sh`, rows 006 and 020 to 022, and the known issue "Enemies are not replicated (milestone 3)". That issue carries every static lead this card relies on. Repo root: `/path/to/ultimate-bug-war-coop`. Symbols: `tools/symbols.txt`.

## Why

Milestone 3 is host-driven, replicated bugs. The static reading says `APawn`'s constructor already sets `bReplicates` and nothing in the enemy's native constructors clears it, so server-spawned bugs may already reach the client. Nobody has checked this at runtime. If they do arrive, milestone 3 starts from health, death and attack visuals, not from spawning. This card answers that question with logs and screenshots and changes no behaviour.

## Static leads for step 1 (senior, 2026-11-03)

- The pair run travels to `/Game/Maps/FED/FED_C1M1?listen`. Its `FED_C1M1_Scripting` sublevel is mostly friendly troopers, target-range buildings and a mech. The only placed bug sources are 2 `BP_DestructibleSpawner_Bugnest_HumanGameplay`, plus encounter managers (`BP_EM_1FactionSetWaves`, `BP_EM_1FactionLoop`, `BP_EM_Dropship`, `BP_EncounterManager_TacTargets`).
- No bug class name (`BP_AI_WarriorBug` and the others) appears in any FED Scripting map header (C1M1, C2M1, C3M1, C3M2). Bugs therefore come from spawner and encounter data, not from placed actors, so the map scan cannot tell whether they spawn near the landing. Check that at run time: log a count of `ASTEnemyBase` actors on the host every 5 s from map start.
- Fallback if none appear within about 60 s of landing: an env-gated host-only debug spawn (for example `UBWCOOP_SPAWN_BUG=1`):
  - load the class with the existing `g_static_load_object` (`StaticLoadObject`) using `/Game/Blueprints/Enemies/Arachnids/WarriorBug/BP_AI_WarriorBug.BP_AI_WarriorBug_C`, passing `UClass::StaticClass()` as the class argument (a `UBlueprintGeneratedClass` is a `UClass`);
  - spawn with `UAIBlueprintHelperLibrary::SpawnAIFromClass` (RVA 0x04B4D830; `(WorldContext, TSubclassOf<APawn>, UBehaviorTree* = null, FVector, FRotator, bool bNoCollisionFail, AActor* Owner)`). It spawns the pawn and its default controller in one call. Place it about 1500 units in front of the host pawn, on the server only (the authority check comes first).
  - Other options: `UWorld::SpawnActor` (0x03DCB440, location/rotation overload) followed by `APawn::SpawnDefaultController` (0x03F727A0).
  - Card 032 uses the `USTCheatManager::CheatSpawn` route (`StaticLoadClass` 0x0157BFE0, then `UWorld::SpawnActor`). If both cards are built, share one spawn helper. A plain `SpawnActor` gives an AI controller only if the class's `AutoPossessAI` allows spawned pawns, so check for the controller and call `SpawnDefaultController` when it is missing. `SpawnAIFromClass` avoids that question.
  - Spawn one bug, once per map. Log the returned pointer, class and location.
- Report which path produced bugs: natural encounter, Bugnest, or debug spawn.

## Do

1. **Get bugs on the host.** Find the cheapest way to have live enemies near both players in the existing pair run (the AUTODROPSHIP flow, or the first combat encounter after it). Say which you used. If no enemies spawn on the host within the run, stop and report what gates the spawn.
2. **Audit, log only, both sides.** Every 5 seconds, and once when each enemy actor first appears or is destroyed, log for every `ASTEnemyBase` actor in the world (cap at 64 lines per tick):
   - class name, actor name, pointer;
   - LocalRole and RemoteRole;
   - bit 3 of byte +0x5B (`bReplicates`) and byte +0x5A;
   - location, rounded to whole units;
   - `CurrentHealth` (+0xAC0) and `MaxHealth` (+0xAC4), plus the health set's `Health` CurrentValue (health component +0x688, set at component +0x130, then +0x30+0xC);
   - whether a controller is attached, and its class.
   Also log the per-tick count per side.
3. **Compare.** From the logs, report whether each host enemy has a client counterpart, whether the positions track within a few units, and whether the client's health values ever change when the host's do.
4. **Look.** One screenshot per window with at least one bug in view of the client. Report whether the client's bug animates (not frozen or in T-pose) and whether a bug the host kills falls, stays standing, or vanishes on the client.
5. **Relayed damage (from card 024 step 5).** With `UBWCOOP_GRANT_WEAPON=MoritaMk1` on both sides, have the client fire at a bug. Report whether the relayed shot changes the bug's health on the host, and whether the client's own local shot writes health on the client (double damage). Log only; do not suppress anything.
6. **Client AI.** Confirm that no AI controller exists for enemies on the client. If one does, report what spawned it. `APawn::SpawnDefaultController` returns early on a client (`NM_Client`), so the revive path in the known issue is not the cause.

## Done when

- The report answers, with log lines: do enemies replicate to the client, do they move, do they animate, and what the client shows when a bug dies.
- No behaviour change shipped. The audit is gated behind an env var (for example `UBWCOOP_ENEMYAUDIT=1`) and off by default.
- Clean build (`/W4 /WX`), 84 exports, KERNEL32-only imports. No new crash dumps.

## Runs

- Build and run with the card 026c pair command (card 034 used it unchanged), adding `UBWCOOP_ENEMYAUDIT=1` in both `HOST_EXTRA_ENV` and `CLIENT_EXTRA_ENV`. Never pass `-nosound`.

## Risks

- A blueprint default could turn replication off for the enemy classes. The audit's bit 3 at +0x5B on the host shows that directly.
- Iterating actors every 5 seconds is cheap at 64 lines, but do not walk the whole actor list per frame.

## Report

Two paired runs, both rc=0: `logs/card023a-run.txt` and `logs/card023b-run.txt`. Logs are in `logs/card023{a,b}-{host,client}.log`. Run b is the clean one for step 5.

### Implementation

- `UBWCOOP_ENEMYAUDIT=1` turns on the audit. It is off by default and logs only, on both sides. It writes:
  - a tick line every 5 s with a per-side count;
  - one row per `ASTEnemyBase` actor: class, name, pointer, LocalRole +0x168, RemoteRole +0x60, bReplicates (+0x5B bit 3), byte +0x5A, location, +0xAC0, +0xAC4, the health set's Health and the controller;
  - APPEAR, GONE and HEALTH events.
- `UBWCOOP_SPAWN_BUG=1` is a host-only test driver. 18 s after landing it does the following:
  - InstaKills every live AI trooper.
  - Calls `UAIBlueprintHelperLibrary::SpawnAIFromClass` with `BP_AI_WarriorBug_C`, loaded through StaticLoadObject. The bug spawns 1500 units ahead along the control yaw, with up to 3 tries.
  - Faces each side at the nearest bug that is not a trooper and takes screenshots: one every 10 s, with a cap.
  - InstaKills the bug 5 s after the client's second relayed press, or 180 s after the spawn as a fallback. Kill screenshots follow at +1, 2, 4 and 10 s.
- New symbols: `ASTEnemyBase_StaticClass`, `UClass_StaticClass`, `UAIBlueprintHelperLibrary_SpawnAIFromClass`.

### Step 1: how bugs got onto the host

- No natural bugs appeared in either run. Every `ASTEnemyBase` on the host was an AI trooper, `BP_AI_Trooper_{Morita,NonCombat,SquadLeader,Medic}_C`. These classes derive from `ASTEnemyBase`, so the audit rows count them too. There were 0, then 5, then 49 to 56 on the host.
- The only bug came from the env-gated debug spawn. `SpawnAIFromClass` succeeded on the first try and returned `BP_AI_WarriorBug_C` with MaxHealth 500, health-set Health 400 and controller `ADAIController`.
- Run a, without clearing the troopers: the AI troopers killed the bug before the client's first press, at Health 400 → −21. Run b clears the troopers first.

### Step 2: audit, both sides

- Host, every enemy (troopers and the bug): LocalRole 3, RemoteRole 1, bReplicates 1, +0x5A = 0x74. Replication is not turned off by any blueprint default.
- Client, every enemy: LocalRole 1, RemoteRole 3, bReplicates 1, +0x5A = 0x74.
- +0xAC0 (CurrentHealth) reads 0.0 on every enemy on both sides. Either the field is unused or the offset is wrong. The live value is the health set's Health.
- +0xAC4 (MaxHealth) reads 25 for troopers and 500 for the bug.
- Counts: the client held 31 to 40 enemies against 49 to 56 on the host. Distant actors are presumably culled by relevancy or streaming.

### Step 3: per-enemy comparison

- Counterparts: every enemy near the players had a client counterpart. The spawned bug had host 0x74150010 and client 0xA0F86670.
- Position tracks closely. The worst sample was host (−1035, 2044, 188) against client (−1034, 2043, 188). The death spot was (−831, 2119) on both sides, with z 181 on the host and 179 on the client.
- Health does not track. The client bug stays at Health 400 for the whole run, including after host death at −360. Client troopers stayed at 50 while host troopers took damage. Client-side health is never written by replication. The health set is not replicated, or the replicated value lands somewhere other than where the audit reads it.
- Death:
  - InstaKilled troopers are destroyed, with GONE on both sides within about 1 s.
  - The dead bug stays as an actor on both sides at the same spot, with no GONE event. The host controller detaches (null) at death.
  - A second InstaKill on the already-dead bug changed nothing.

### Step 4: screenshots

- `logs/card023b-bug-alive.png` (ScreenShot00182): the live bug at about 1260 units, legs mid-stride, so it animates. The side is ambiguous because host and client both requested a shot at about 1260, and both captures share one filename.
- `logs/card023b-client-bug-dead.png` (00183) and `logs/card023b-client-bug-dead-10s.png` (00187): client views. The dead bug is close, which matches client dist 436; the host was at 988.
  - The dead bug lies fallen: collapsed body, legs splayed, an orange death glow.
  - It does not stay standing and does not vanish, even though client health still reads 400.
  - The death pose arrives by replication without any client health change.
- Run a: 00171 is the bug mid-leap at close range and 00172 is the bug far away. All run a and run b shots are in `logs/card023{a,b}-shots/`.
- Caveat: both instances write to one Screenshots folder, so a simultaneous host and client request collides on one filename. Run b kept 7 files out of 10 requests.

### Step 5: client fire with MoritaMk1 (run b)

- Troopers were cleared at 497874149 and the bug spawned at 497874210. The client's relayed presses came at 497881087 and 497883596. The host was not firing and no troopers were alive.
- The host bug's Health went 400 → 360 (497881140) → 20 (497882140) → −360 (497883140), so the relayed shot does change host health.
- In the window, the host's `server-copy FireProjectile` count was 42 and the client's `client-own FireProjectile` count was 42. That is 1:1, with no duplicate server projectiles.
- No double damage: the client's local shots never wrote client health (it stayed 400).
- Open point: about 760 damage in about 2 s is high for a MoritaMk1 against a 400-HP bug. It is not explained. It could be per-pellet or per-hit multipliers, or headshot or weak-point scaling. Worth a later look.

### Step 6: AI controllers on the client

- No client enemy row has a non-null controller, in either run. Host rows show `ADAIController` while the enemy is alive.

### Uncrouch refit (55 → 95)

- Still not exercised. Run b crouched once ("autoinput: action crouch" at 497878584). The crouch refit to capsule half-height 55.0 (last refit 95.0) logged on the host at 497878614 and on the client at 497878695. No stand-up refit line to 95 appeared.

### Gates

- Build clean at `/W4 /WX`, 84 exports, KERNEL32.dll the only import.
- Crash dumps: 16 before and 16 after, so none new.
- The audit is off unless `UBWCOOP_ENEMYAUDIT=1`. The spawn is off unless `UBWCOOP_SPAWN_BUG=1`.
- Cosmetic: early client log lines are labelled "host tick" until the client session is known, because `side()` defaults to host.

### Follow-ups suggested

1. Client health. Find the replicated health field on the enemy health set, or a RepNotify path. Neither +0xAC0 nor the client health set changes, so the client UI and any client-side death logic see full health.
2. The damage magnitude in step 5.
3. Give each side its own screenshot filename.

## Senior review (2026-11-03)

Accepted. Checked the following:
- `card023{a,b}-run.txt` rc=0.
- Host and client logs for run b: APPEAR rows with roles 3/1 and 1/3, bReplicates 1 and +0x5A 0x74 on both sides.
- The host health trail for the bug: 400 → 360 (497881140), → 20 (497882140), → −360 (497883140).
- Client side: no `client HEALTH` lines and no client row with a non-null controller.
- Screenshots 00182 (live bug mid-stride) and 00183 (the client sees the bug collapsed with the orange death glow, and the host sprite in view).
- `dwmapi.dll` (13:18, before run b at 13:21) has 84 exports and imports only KERNEL32.dll. No function body in `mod/src` is over 60 lines.

What this changes:
- **Spawning and movement already replicate.** Milestone 3 starts from health, not from spawning.
- **The death pose replicates as well.** My static lead in STATE predicted that a dead bug would stay standing on the client; that was wrong. Something replicated (the death state, a tag, or a multicast) drives the death animation on the client. Find what it is before building a client-side death visual, so we don't play it twice.
- **`CurrentHealth` (+0xAC0) is 0 on the host too.** My guess that the blueprint writes it is wrong for these classes. Its health bar, if any, does not read it. The live value is only the health set's `Health`.
- The damage size (about 760 in 2 s) stays open. The 42:42 projectile count rules out duplicate server projectiles.

Follow-ups:
- A future card finds the client-side health route: is the health set's `Health` `CPF_Net`, and is there a RepNotify?
- Each side gets its own screenshot filename, in the tooling. Simultaneous shots collided (7 of 10 kept).
- The uncrouch refit is still unexercised; card 035 step 3 covers it.
- Cosmetic: early client lines are labelled "host".

## Static notes moved from STATE (senior, 2026-11-03)

- Replication bits: the `APawn` constructor (0x03F49A00) sets `bReplicates` (`or byte [this+0x5B], 8`) plus bits 0x50 at +0x5A. Neither the `ASTEnemyBase` constructor (0x05057090) nor the `ASTCharacter` one (0x050D5B60) clears them. Class chain: `ASTEnemyBase` → `ASTNonPlayableCharacterBase` → `ASTCharacter` → `ACharacter` → `APawn`. `ASTWarriorBug` is not the pawn; it is an `AActor` holding follow and leg-smoothing fields.
- Server-only death: `ASTNonPlayableCharacterBase::HandleDeathEvent` (0x05192BC0) and `StartDeadState` (0x051A8260) start the Logic Driver machine `StartStateMachine_Dying` (`USTAggregatedSMInstance`), plus barks, the kill score (`USTMetaGame::EnemyKilledByGameplayTag`), ticket release and AI awareness deregistration. `GetCorpseDespawnTime` sets when the server destroys the corpse.
- Visual death: `TryPlayDeathAnimation` (0x051B11E0) is `TryPlayAnimationSequence(FName("Death"), true)` (0x051B0F30), through the `AnimatorComponent` script interface (object +0xA30, interface +0xA38). It has no authority check and touches no score, ticket or AI state. Bug animation runs on `USTSkeletalAnimatorComponent`.
- NPC health fields: `CurrentHealth` +0xAC0 and `MaxHealth` +0xAC4 (blueprint-visible, not `CPF_Net`), and the `OnHealthValuesUpdated` delegate at +0x970 (players: +0x888). Native code writes only `MaxHealth`, from `USTNPCData` in `ApplyData` (0x0517D0F0).
- A candidate fix for health: replicate `Health` (`CPF_Net`, as for players in card 021 step 5) and broadcast on the client, so the client health bar refreshes.
