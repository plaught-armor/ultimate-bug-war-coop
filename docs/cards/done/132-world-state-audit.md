# Card 132: Whole-map world state audit (host versus client)

Status: accepted 2026-11-08 on closing run hb61 (test DLL 94e38ac3dc93b321) and the static Traces section. Effects go to card 136, the rest of world state to card 137, NPC movement send gaps (Trace 5) to card 140. Was: sent 2026-11-07.

Re-scoped 2026-11-08 (user agreed): card 132 closes on the fixes already built (through 5ac3ce3e0a2bf91b, plus the
g_c75 pointer fix), after one batched run that checks them. Every visible-effect item (2b explosions, cues, level
Niagara toggles, dropship one-shots, 3b seeds and sky) moves to card 136, the effects manager. The non-effect steps
(1, 2, 2a, 2c, 2d, 3, 4, the replay end state, nuke shot 2) move to card 137. The Traces section below still gets
written; cards 136 and 137 use it.

Read: memory rule full-world-sync (all world, enemy and objective state is the same for every player; one mechanism
per class), docs/STATE.md "Card proof and review rules", cards 049 (objective journal), 056 (PickupGone), 095 (doors
and collision), 098 (world journal rows S, K, T, timeline), 104, 105 and 107 (runtime copies), and 131.

## Why

The user's playtest (play DLL `0a98be3c89324a0d`) keeps finding world state that differs on the client: bug nests
not destroyed, bug corpses standing, and weapon pickups missing. Each one was carded as its own symptom and proved
from host-side log lines. The user's order: look at everything the host sends the client about the whole map,
because it must send all objective and item state. This card builds the check that finds every such gap at once,
and the list of what carries each class.

## Rule for every fix in this card (user, 2026-11-07)

The client runs the same game as the host, minus being the host. For world state, that means:

- The host's state is the only source. The client never changes world state that the host owns on its own: a death,
  an explosion, a destroyed or used actor, a pickup taken, or an objective step. Each of these reaches the client only
  through the host's journal or relay, and is applied once.
- What the client does locally (its own hold completing, its own damage, its own trace) is cosmetic until the host's
  row arrives. If no row arrives, the client's world does not change.
- The other direction is just as required: every client action that changes host-owned state is sent to the host as
  the finished event (who, what target, what result), and the host applies it. The host does not re-simulate the
  action through the server copy (its own trace, aim or hold timer) and hope for the same outcome, because the copy's
  view differs from the client's. Friends-only trust model: the host accepts the client's event after basic sanity
  checks (target exists, within range). Step 1 lists, per class, the client actions and whether each reaches the host
  as an event.
- The playtest found three splits of this kind in one session: nests, corpses and the satchel (card 133: the client's
  flak bug blew up, the host's did not). Step 1 lists, per class, the client-side paths that change state the host
  owns. Step 3 routes each one through the host.

## Do

1. **Class inventory (host, logged at each map load and at the end of each map).**
   - Walk every actor in the world.
   - For each class, log one row with:
     - the count;
     - placed or runtime;
     - bReplicates;
     - whether it has a health component, is an `AADPickupBase`, is an objective actor (in any
       `USTLevelObjective::ObjectiveActors`), has a timeline, or has a collision that changes;
     - the mod mechanism that carries its state changes to clients (049, 056, 095, 098 S/K/T/timeline, 104/105/107,
       engine replication), or **none**.
   - Every class whose state changes in play and has no mechanism is a gap. List the gaps in the Report.
   - Also cover the objective system itself: the active objective, the state of each sub-objective, markers, and
     objective counters (kills, nests left, items carried).
2. **World state digest and diff (both sides).**
   - Every 30 s, and at the end of a map, the host builds a digest of every actor that does not replicate (placed
     and runtime copies), keyed by name plus location rounded to 100 uu.
   - Per actor, the digest holds what a player can see or use: alive or dead (`IsDead`), hidden, collision enabled,
     pickup enabled, for runtime copies the location, and for objectives their completion state.
   - The host sends the digest to clients over the mod channel, chunked within `kChanTextMax`. This is a
     friends-only binary payload; keep it cheap.
   - The client builds the same digest of its own world and compares the two.
   - The client logs one `WARN card132: mismatch <class> <name> <field> host <v> client <v>` per actor and field,
     with the first 64 shown in full and the rest counted per class. It also logs a total line each round:
     `card132: client round N: actors H host, C client, mismatches M (classes ...)`.
   - Actors missing on either side count as mismatches ("host only" or "client only").
2a. **Client change log (client to host, user 2026-11-07: "we just need to know what the client is doing to things
    on the map, so it can be synced between host and vice versa").**
   - Both sides hook the same small set of engine and game chokepoints where a map actor's state changes, not each
     action. At least: a health component reaching 0 (OnDeath), `AADPickupBase::DisablePickup` and pickup taken,
     `AActor::Destroyed`/`EndPlay` on placed actors, `SetActorHiddenInGame`, collision enable changes, satchel
     building explode, interactable `OnInteraction`, objective and sub-objective state changes, and runtime actor
     spawns (card 107).
   - On the client, every hit on a non-replicated map actor logs `card132: client change <class> <name> <field>
     <old> -> <new> by <cause>` and is sent to the host as an event (step 1 rule: the host applies it once and
     relays it back to every client).
   - On the host, the same hooks feed the existing host-to-client journal (card 098). One table of chokepoints serves
     both directions.
   - In the run, every client change line must have a host apply line, and the step 2 digest must still end at 0
     mismatches. A client change that the host refuses, or that no hook covers (found by the digest), is a gap.
2b. **Known gap: the host player's projectile weapons (user, 2026-11-07: "friend couldn't see me nuke the bugs
    either for the objective").**
   - Verified (senior, static): `c76_mark_task` (`attributes.cpp`) puts a fire task on the card 076 relay only when the
     shooter is a mech or a turret. Pooled `AADProjectile` rounds do not replicate, so a player-fired projectile
     weapon (the NukeLauncher, picked up by the host at host log `logs/playtest-menu-20261107-172301.log` line 5668)
     spawns nothing on the client.
   - Verified: the client gets only the host's Fire press and release edges (`c26_client_fire` sets
     `g_c24.host_fire`, which only drives the remote sprite's Attack key). The host's own pawn copy on the client is
     never given the weapon the host picked up. `ChanOp::Weapons` and `ChanOp::Equip` cover only the client's own
     pawn, and the client log has no give or equip on the host's pawn.
   - Open: whether the bugs the nuke killed died on the client (AI bugs replicate, so their death should arrive),
     and which objective step the nuke was for.
   - Fix (one mechanism): every player-fired pooled projectile goes through the card 076 copy relay, not only mech
     and turret fire, with the same skip for the shooter's own client. The explosion and impact effects follow the
     card 074 grenade explode path. Step 4 needs a client screenshot of the host's nuke in flight and its explosion.
2c. **Known gap: placed weapon pickups (the weapons in crates) do not show on the client (user, 2026-11-07: "the
    weapons also don't show in the chests/crates/containers.. only the ammo and armor/health crates").**
   - The crates are static meshes (`SM_WeaponCrate_01`) with a placed `ASTWeaponPickup` inside. Ammo pickups are plain
     `AADPickupBase`. Weapon pickups are `ASTCustomPickupBase`, which is a persistent (save-system) actor:
     `PersistentActorActivate`/`Deactivate`, `ActorFullyLoaded(USTSaveComponent*)`, `PreSaveGame`, `IsPickupEnabled`.
   - Verified: on both sides the placed weapon pickups exist and their colliders are enabled (the card 098 collision
     lines match). The host enables `InteractionCollider` from `ASTCustomPickupBase::OnSignificanceChanged`
     (exe+0x51D1C61). The client enables it only from `PersistentActorActivate` (exe+0x51D25D2) and never from
     `OnSignificanceChanged`. `OnSignificanceChanged` (exe+0x51D1C40) sets the tick interval and can `AActor::Destroy`
     a pickup by significance level, after checks on `IsSpawnedAtRuntime` (exe+0x4C61740) and `HasSignificance`
     (exe+0x515D990).
   - Open, lead to check first: the client's own save state. `ActorFullyLoaded` and `IsPickupEnabled` may hide or
     disable a weapon pickup that the friend's own single-player save marks as already taken. Log, on the client,
     per placed weapon pickup at map load and +10 s: hidden, `IsPickupEnabled`, the mesh visibility, and whether
     `ActorFullyLoaded` ran with which save state. Compare with the same pickups on the host.
   - Fix: the host's state decides (rule above). Either the client ignores its own save for persistent actors in a
     co-op session, or the host sends its persistent actor state (the card 098 journal). One mechanism for every
     `ASTCustomPickupBase` class and any other persistent actor that the class inventory finds.
2d. **Candidate classes with no mod mechanism (senior, static, 2026-11-07).**
   - Source: `build/publics.txt`. These native classes have state-changing functions, and no file in `mod/src` names them. The step 1 inventory and the step 2 digest decide which ones are real gaps on the maps we play. Hook a function only after checking its body; three are folded one-line setters (below).

   | Class | State change (exe RVA) | Notes |
   |---|---|---|
   | `ASTLaser` (seen: `STLaser` tally) | `SetLaserActive` 0x5102A50 | laser barrier on or off; damages characters |
   | `ASTHazardousZoneBase` | `SetHazardActive` 0x516D340, `BeginMoveToOpenPosition` 0x514CCF0, `BeginMoveToClosedPosition` 0x514CC80 | persistent actor (save state) |
   | `ASTJumpPadBase` (seen: `STJumpPadBase` tally) | `SetActive` 0x516CB60, `PersistentActorActivate` 0x5168180 | persistent actor |
   | `ASTKeyPickup` (seen: `STKeyPickup` tally) | `Pickup_Implementation` 0x51D4150 | a key one player takes must be gone for both; check card 056 covers it |
   | `ASTShootableButton` | `OnDamageTaken` 0x50F6FD0 | a client's shot must reach the host as an event (2a) |
   | `ASTLargeElevator` | `NextFloor` 0x50F5670, `PrevFloor` 0x50FD4E0, `SetMoveDirection` 0x5102CE0 | floor index and position |
   | `ASTTeleporterVolumeBase` | `SetIsActive` 0x19D7360 is a folded setter (`mov [rcx+0x2F0], dl`): do not detour it, read +0x2F0 in the digest; `StartPlayerTeleport` 0x5170AC0 | |
   | `ASTSpawnerBase` | `SetSpawnerEnabled` 0x1975B10 is a folded setter (`mov [rcx+0x3FC], dl`): read +0x3FC in the digest | spawners are host-only logic; the digest field shows whether the client's copy differs, it does not mean the client must spawn |
   | `ASTActivationVolume` | `Activate` 0x5022080, `Deactivate` 0x5152940 | |
   | `ASTEnemyPresence`, `ASTPlayerPresence` | `SetTriggerEventsEnabled` 0x5103770 and 0x5103780 | objective triggers (card 126 arrival shapes) |
   | `ASTAnimatedProp` | `TryDoAction` 0x5174FF0, `StartNewAnimationSequence` 0x5170870 | sprite props; host animation state |
   | `ASTDestroyableProp`, `ASTDestroyablePropExplosive` | `StartPropDestroy_Implementation` 0x51714C0, `OnDeath` 0x5163210, `Explode` 0x5156190 | check that the card 098 `'K'` row covers them, with an explosion on the client |
   | `ASTDamageableActorCorpseBase` | `OnDeath` 0x1162D20 is an empty folded `ret`; use the health component path | corpse props (card 131 class) |
   | `ASTEnemyProjectileBileBlob`, `ASTEnemyProjectileSpawnEnemy` | `AttachToImpactedComponent`, `SpawnCharacterFromNPC` | bug projectiles: same question as 2b, in the other direction (does the client see them?) |
   | `ASTPlayerProjectilePenetrating` | | player projectile class for 2b |

   - **Bug projectiles: check, not assumed (user, 2026-11-07: "bug projectiles just need to be checked anyways").**
     - Known: the pooled `BP_Projectile_Plasmabug_C` (`replicates 0`) is spawned on both sides (c131k2, caller exe+0x3C78588). The card 098 spawn lines are capped at 2 per class, so they say nothing about how many shots each side fires.
     - Open: whether the client fires each bug shot the host fires, at the same time and aim, and whether it also fires shots of its own that the host never fired.
     - Log, on both sides, every bug projectile launch: the activation point the card 076 relay uses for pooled `AADProjectile`. Log the class, the shooter, the location, the direction and the time. Count per class per side for each digest round. The ledger's clock offset lines up the times.
     - Cover every bug projectile class on the run's maps, at least `BP_Projectile_Plasmabug_C`, `ASTEnemyProjectileBileBlob` and `ASTEnemyProjectileSpawnEnemy`. A projectile that spawns a bug must not spawn one on the client: the bug comes from the host by replication.
     - Proof: matched counts per class, or the gap with its cause. Client screenshots of one bug shot in flight and its impact, next to the host's at the same time.
     - Fix, if needed: the card 076 relay (host to client), the same mechanism as 2b.
   - **The persistent actor class is one gap, not many.** `ASTLaser`, `ASTHazardousZoneBase`, `ASTJumpPadBase`, `ASTSpawnerBase`, `ASTEnemyPresence`, `ASTPlayerPresence` and `ASTCustomPickupBase` (2c) all take their state from `PersistentActorActivate`/`Deactivate`, `ActorFullyLoaded` and `PreSaveGame`. If the client loads that state from the friend's own save (the 2c lead), every one of these differs on the client. Fix them with one mechanism, the 2c fix: in a co-op session the client takes persistent actor state from the host, never from its own save. Card 121 skipped the client's checkpoint save and load. Check whether `ActorFullyLoaded` on the client still reads save data through another path, and log which.
3. **Fix by class, not by symptom.** For each gap class from steps 1 and 2:
   - extend the mechanism that already owns that kind of state (the card 098 journal for placed-actor state, card
     104 copies for runtime actors);
   - do not add a new relay per symptom.
   - Card 131 (the client kill no-op and the corpses) stays its own fix. This card's diff is how card 131 is
     proved.
3b. **Sky ambient effects match (user, 2026-11-08; low priority, last fix before the soak).**
   - The sky's ambient plasma is chosen at random on each side (seen in hb54 through the thunk exe+0x50C2840), so
     the two skies differ. The user wants them the same.
   - Static first: find what the ambient spawner draws its randomness from.
     - A seeded `FRandomStream`: the host sends the seed at map load. A late joiner also needs the stream's current
       state, sent through the card 098 journal.
     - The global `FMath` random generator: relay each ambient spawn (asset, location, rotation, time) from host to
       client, and suppress the client's own ambient picks.
   - Pick the cheaper of the two, and state why. Network cost must stay negligible (a few small frames a minute
     at most).
   - Proof: a client sky screenshot next to the host's at the same time, both showing the same ambient effect.
   - **Every seeded random source, not only the sky (user, 2026-11-08: "if there are potentially seeded instances,
     all seeds need to match from host to client").** This is a class-wide audit:
     - List every random source that runs on both sides and changes something visible. Candidates:
       - `FRandomStream` members on actors and components, and `KismetMathLibrary` stream calls in Blueprints;
       - Niagara systems with a fixed `RandomSeed` or deterministic random, and Niagara user parameters set from a
         stream;
       - procedural placement (foliage, debris, decals, gore and gib scatter, spawner FX);
       - any per-map or per-actor seed set at BeginPlay.
     - Host-only logic randomness (spawn picks, AI choices, loot rolls) does not need a matching seed. Its results
       already come from the host; list it as host-only.
     - For each shared source, the client uses the host's seed. Send it at map load for map-lifetime streams, or in
       the event frame for per-event seeds (an explosion's or a gib's own seed). Late joiners get it through the card
       098 journal.
     - A matching seed is not enough when the two sides draw a different number of times. A stream that is drawn
       from per tick, or per local event, will drift apart. Where that can happen, seed per event, or relay the
       results.
     - Report: a table of source, owner class, address, shared or host-only, and how it is synced.
3a. **Test god mode (user, 2026-11-07: "you need to give client and host godmode so you can properly test").**
   - Add `UBWCOOP_GOD=1`, read on each side for its own pawn. With it on, neither player can take damage or go down.
     Default off; the user's play build never sets it.
   - Find the damage gate statically, and state the address. Candidates: the health component's damage path (the
     exe+0x5233F60 family from card 131), or holding the Health attribute at max. `AActor::bCanBeDamaged` may not
     stop gameplay-effect damage, so prove the choice in the log (`card132: god <pawn> damage <n> blocked`).
   - `run_pair.sh` sets it for every run except the down and revive tests (cards 043 and 044) and any run that needs a
     death.
4. **One soak run, after the hold is lifted.**
   - The whole of FED_C2M1, FAST=1, played through by the card 126 autoplay as far as it goes. It must include
     trooper deaths with weapon drops, nest kills, pickups taken by both players, and objective steps.
   - The client joins late (after the first objective), so the journal replay is covered too.
   - The end-of-map round must show 0 mismatches. Every mismatch class in earlier rounds must be either fixed or
     listed with its cause.

## Done when

- The Report has the class inventory table, with the mechanism or the gap for each class, and the digest's fields.
- It has the mismatch count per class for each round, ending at 0, and client screenshots for one item from each
  fixed class.
- It has the table peaks, the WARN diff against c130d5, the run length, rc, ERROR/FATAL, CRASH lines, dumps and the
  DLL hash (`sha256sum | cut -c1-16`), with the play DLL saved as `build/dwmapi-play-<hash>.dll`.
- It ends with "Report complete."

## Report

### Side work: client gore, bug sync and corpses (runs hb1 to hb11)

All runs: lane 2, FED_C2M1, FAST=1, card 041 trace on both sides. hb1 to hb5 use `anim-probe.txt`; hb6 to hb11 use
`c132-gore-proof.txt`. The DLLs are test DLLs; 4cb036fa33c090bc stays the play build.

| Run | DLL | Length | rc host/client | ERROR/FATAL | CRASH lines | Dumps | WARN host/client |
|---|---|---|---|---|---|---|---|
| hb1 | 00ea4de282616ec0 | 120 s | 0/0 | 0 | 0 | none | 0/9 |
| hb2 | 1c4038ea04da1258 | 123 s | 0/0 | 0 | 0 | none | 0/14 |
| hb3 | 9ae840b4e677c8d3 | 123 s | 0/0 | 0 | 0 | none | 0/13 |
| hb4 | de869afbc7cae384 | 124 s | 0/0 | 0 | 0 | none | 0/21 |
| hb5 | 167f1fc8dcae8423 | 124 s | 0/0 | 0 | 0 | none | 0/12 |
| hb6 | 72db0bf5e4b383fc | 323 s | 0/0 | 0 | 0 | none | 0/27 |
| hb7 | 72db0bf5e4b383fc | 297 s | 0/0 | 0 | 0 | none | 0/29 |
| hb8 | 0b786c940611ec84 | 300 s | 0/0 | 0 | 0 | none | 0/27 |
| hb9 | 8dd0d13872f4c968 | 50 s | 0/0 | 0 | 0 | none | 0/0 |
| hb10 | 8dd0d13872f4c968 | 117 s | 0/0 | 0 | 0 | none | 0/9 |
| hb11 | e791321b536c6768 | 318 s | 0/0 | 0 | 0 | none | 0/27 |

WARN diff, by class (client; the host logged none):

- `card098: mismatch LevelSequenceActor key=bef36fd1 presence ...` and its digest round line: every run, 9 lines per
  120 s. This is the open LevelSequenceActor_1 host-only item, not new.
- `card132: client hit BP_AI_WarriorBug_C guid ...: host impact (...) is N uu from the client's NPC at (...)`: hb2 to hb5, 3 to 12 lines per
  run. These are client hits on a corpse whose ragdoll sits elsewhere on the host (see Corpses below). None in hb6.
- No other class.

Far gore (fixed in hb5): a client copy of a bug that was never rendered kept its physics bodies at its spawn point,
so `GetGoreLocation` (the mesh centre of mass, exe 0x14418F070) put the death gore there. The client now updates the
kinematic bodies to the current pose (`UpdateKinematicBonesToAnim`, RVA 0x04019C30, TeleportPhysics, no deferral)
before it reads the gore location, and leaves a simulating (ragdoll) mesh alone, because that call with
TeleportPhysics also teleports simulating bodies (lambda exe 0x143FE3140). hb5: 21 of 21 client deaths have their
gore within about 150 uu of the bug. hb6 logged 12 client deaths: 10 "bodies synced", 1 "ragdoll left", 1 trooper
"no pose", each with the gore location moved 0 uu (all those bugs were rendered).
hb7 (tools/scenarios/c132-gore-proof.txt) logged 30 client deaths: 25 "bodies synced" (23 moved 0 uu, 2 moved 1 uu)
and 5 troopers "no pose". The far-death wait (a death whose gore location moved 1000 uu or more) timed out: every bug
that died near the client had been rendered, so the stale-bodies case did not recur. One extra WARN class in hb7 is
the scenario's own `cmd: warp client` failure, which is a test-driver step and not a game fault. The hb7 hit-chunk
screenshot pair is not a valid proof: troopers block the client view of the bug.

Correction to hb6 and the hb5 fix: the hb6 guid 0xa0 death showed the fix was a no-op for a bug that was never
rendered. Its gore was 9900 uu from the actor, at 0xa0's spawn point, and the line said "bodies synced, gore
location moved 0". The cause is static: bug meshes set `KinematicBonesUpdateType` (mesh +0xA6C) to SkipAllBones, and
`UpdateKinematicBonesToAnim` then returns at once unless a body simulates (exe 0x144019C66 to 0x144019C84). The sync
now sets the type to SkipSimulatingBones for that one call and puts it back (`c39_sync_mesh`, enemies.cpp). The hb7
"moved 0" deaths were rendered bugs, as hb7 said; they were not evidence that the fix worked.
hb8 (DLL 0b786c940611ec84): 11 client deaths, all rendered, none needed the lift; the far-death wait failed on a
scenario regex that did not allow the new "(SkipAllBones lifted)" text (fixed).
hb9 (DLL 8dd0d13872f4c968): the far death came at +46 s. The client logged "death gore for BP_AI_WarriorBug_C at
(225030, 37634, 8599); actor at (225031, 37640, 8595); ... bodies synced (SkipAllBones lifted), gore location moved
5360 uu". So the gore moved 5360 uu from the stale spawn point onto the bug. The far-death screenshot (client-hb9-far-death.png,
the client warped 300 uu from the gore point) shows the green gore at the death point. hb9: rc 0/0, ERROR 0, WARN 0
on both sides, no dumps, 0 of 20 guids stale, 0 corpses apart.

Bug movement: in hb6, 5 of 36 guids were more than 500 uu apart while alive (worst 526 to 668 uu). Each is a fast
mover, and its client copy never stood still while the host copy moved. This matches the guid 0xaa case from hb5:
the host and client samples are taken at different times, and the speed changed. With that offset corrected, the
error was 15 to 253 uu, and it did not grow.

Corpses (hb6, `tools/movesync.py`, mesh rows): 6 of 11 corpses are more than 500 uu apart mesh to mesh. On the host,
the ragdoll is thrown 700 to 1400 uu in the first 2 s after death, then rests by 4 to 7 s. For guid 0x8c, the mesh
rose 330 uu at +1 s, so it is launched, not sliding on a slope. On the client, the corpse mesh moves less than
150 uu. One client corpse (0xa0) sinks at a steady 480 uu/s through the floor; that is open.

### Closing run hb59 (run 1 of the closing budget): predictions

DLL 42bec463a010f337 = 5ac3ce3e0a2bf91b plus `c75_map_end` (vehicles.cpp: `g_c75_pilots` and `g_c75_client` emptied in
`ubw_load_map_enter`, both sides). Scenario `c132-decal-proof.txt`, the hb58 command plus `UBWCOOP_C69_LOG=1` on the
host. These predictions were written while hb59 was starting, before any of its output was read (the run was launched
before the 2026-11-08 trace-before-run rule arrived).

1. Table clears (`c132_rounds_map_end`, `c132_rounds_forget`, `c75_map_end`): the scripted reconnect and the map load
   finish with rc 0/0, 0 CRASH lines, no new dumps, 0 ERROR/FATAL on both sides.
2. Nuke shot 1 relay: `fx_pair.py` shows `NS_FX_Weapons_NuclearExplosion05` paired at least 1 (hb58: 2/1, paired 1).
3. Gate rule: the "hooks: skipped" line names no hook that a relay or journal depends on.
4. Dead time: drive.py's "dead time:" line is quoted; every timed-out wait is listed for removal.
5. Nuke shot 2 is out of scope (card 137). The `try` wait for a loaded launcher is predicted to time out (the
   re-granted launcher's reserve is 0 on the host; trace in Traces below) and is removed after this run.

### Closing run hb59: results

`logs/hb59.runpair.out`: "run hb59: ended by exited after 187 s; host rc 0, client rc 3"; "host: 32205 lines,
FATAL/ERROR 0, WARN 7"; "client: 16679 lines, FATAL/ERROR 0, WARN 116"; "new crash dumps:
UECC-Windows-F0FA5E824C5E1359992C04A8D8992290_0000"; "drive rc killed". WARN against hb58: host 13 to 7, client 369
to 116.

1. Fails. The client crashed at the scripted reconnect, 5 ms after the new GameState arrived:
   `CRASH 1/4: EXCEPTION_ACCESS_VIOLATION (0xC0000005) at exe+0x51EB9BA ... reading 0x0` (client t=215115136).
   Stack, symbolized: `USTWeaponUtilityLibrary::GetWeaponInventory+0xA` < `execGetWeaponInventory+0x72` < `execLetObj`
   < `execLocalFinalFunction` < `execLocalVirtualFunction` < ... < `AActor::ProcessEvent` < `AActor::BeginPlay+0x241`
   < `AActor::DispatchBeginPlay` < `AWorldSettings::NotifyBeginPlay` < `AGameStateBase::OnRep_ReplicatedHasBegunPlay`
   < `FRepLayout::CallRepNotifies`. No mod frame other than the ProcessEvent pass-through (ubwcoop+0x4CEAE).
   - Caller (verified, bytecode): `BP_PickupTracker` (the FED_C2M1 nuke pickup tracker). Its ubergraph runs
     `GetWeaponInventory(GetSTPlayerCharacter(self))` and binds `OnWeaponEquipped` / `OnWeaponUnequipped` ("Enable
     Delegates", reached from ReceiveBeginPlay). `GetSTPlayerCharacter` returns the local player's pawn;
     `GetWeaponInventory` reads the actor without a null check.
   - Cause (verified, log order): the client's BeginPlay pass runs when the GameState replicates. In hb58 the pawn came
     first (pawn spawn t=214212395, GameState t=214212413); in hb59 the GameState came first (t=215115131) and no pawn
     had spawned since the reconnect, so the tracker passed null. The single-player game always has a pawn at
     BeginPlay. This is a join-order race, not one of the card 132 table clears; it can hit any client join or
     reconnect on a map with a `BP_PickupTracker`, including the play build.
   - The table clears themselves are not graded: the reconnect never reached the map's play state.
2. Not graded. The drive was killed at line 168 (reconnect pose wait); the nuke steps start at line 229. The host and
   client logs have 0 `Nuclear` lines.
3. Holds. The host's "hooks: skipped" lines list 125 hooks, none a relay or journal hook (input, dropship, movement,
   jump, weapon-grant and diagnostic hooks); 0 names contain relay, journal or replay.
4. Not graded. The drive was killed before it printed its "dead time:" line. The waits that timed out: line 92 and
   line 93 (decal placed waits, 2 s each), line 168 (reconnect pose apply, 60 s; the client had crashed).
5. Not reached (see 2).

Closing is blocked by a crash outside the card's goal. Per the scope rules, it goes to the senior as a new item before
any fix.

### Closing run hb61 (run 2 of the closing budget): predictions

Written before the run. DLL 94e38ac3dc93b321 = hb59's 42bec463a010f337 plus card 138 (client BeginPlay hold,
accepted on hb60) and card 139 (one ponged gate on every host-to-client frame, batched here). Scenario
`c132-decal-proof.txt`: nuke shot 2 and the note 3 block are removed (card 137), so `UBWCOOP_C69_LOG` is dropped
from the hb59 command.

1. Table clears and the reconnect: rc 0/0, 0 CRASH lines, no new dumps, 0 ERROR/FATAL on both sides. The client's
   rejoin writes `card138: ... no hold` or `hold start` and then `release after`, and a BP_PickupTracker BeginPlay line
   with a non-null pawn, with no `WARN card138`.
2. Nuke shot 1 relay: `fx_pair.py` pairs `NS_FX_Weapons_NuclearExplosion05` at least once.
3. Gate rule: the "hooks: skipped" line names no hook a relay or journal depends on.
4. Dead time: drive.py's "dead time:" line is quoted. No `try` wait on a launcher reload is left, so no wait in the
   nuke part times out.
5. Card 139 (see that card): on join and on rejoin, every client `card132: recv` line before the `card120: ... minimap
   origin` line is a Ping, Pong or LedgerAck. The host's ledger writes `not sent: not ponged yet (card 139)` only
   before that client's first Pong on the map. The rejoin's join replays (card049, card098, card132 pose, decal and
   dropship) still apply on the client, and the `client pose apply ... bodies` wait passes.

### Closing run hb61: results

`logs/hb61.runpair.out`: "run hb61: ended by scenario after 192 s; host rc 0, client rc 0"; "host: 40343 lines,
FATAL/ERROR 0, WARN 29"; "client: 38836 lines, FATAL/ERROR 0, WARN 500"; "new crash dumps: none"; "drive rc 0". 0 CRASH
lines on either side. DLL 94e38ac3dc93b321.

1. Holds. Reconnect: "card045: client t=217229853 reconnected (1): try 1, 8931 ms after the drop", then "card138:
   client t=217230861 no hold: GameState 00000000C0CB0760 begun play with pawn 0000000057C6C740
   (BP_PlayerCharacterMI_C)" and "BeginPlay of BP_PickupTracker_C: local pawn 0000000057C6C740". Join 1 was also "no
   hold" (t=217156367). 0 `WARN card138` lines. Digest rounds 1 to 5 all "mismatches 0" (round 5: "actors 30327 host,
   30327 client").
2. Holds. fx_pair: `NS_FX_Weapons_NuclearExplosion05` host 4, client 2, paired 2. The 2 host gaps are the
   SpawnSystemAtLocation lines under the STSpawn lines of the same explosions (frames `0x524C69B < 0x511BE8B`), as in
   hb58.
3. Holds. 125 skipped hooks, the same list as hb59; none is a relay, journal, replay or channel hook.
4. Holds. "dead time: sleeps 51.3 s, timed-out try waits 29.2 s in 3 (line 223 20 s, line 262 8 s, line 173 1 s)".
   None is in the nuke part (lines 243 to 248 all passed). hb58: 56.3 s sleeps, 41.5 s in 7 waits.
5. Holds (card 139). The client's recv rows before its origin line: join 1, only a Pong; the rejoin, a Ping and a
   Pong. The first other row is Difficulty, after the origin line both times (t=217160217 after 217160146;
   t=217235715 after 217235695). The host held 2 frames, both Anim, both before the first Pong's journal replay
   (217160087 and 217160108; "card049: host t=217160171 journal replay ... (first Pong of the map)"). The rejoin
   replays applied: card049 "replayed ..." lines from t=217235716, and the line 168 wait "client pose apply ...
   bodies" passed.

WARN diff against hb58 (the same scenario; hb59 crashed): host 13 to 29: "fx grenade explosion (host's own)" 10 to 23
and "OnHit of ADProjectile ... GrenadeExplode already" 2 to 4 (more grenades and the frag shot this run); new kind
"host hit BP_AI_WarriorBug_C ... impact (0, 0, 0)" 2 (a hit with no hit point; trace 5). Client 369 to 500: util
niagara WalkGround 303 to 446 (trace 4), fx relayed explosion 23 to 32, hit position 40 to 22; the hb58 mismatch
WARNs (2 nuke pickup, 1 round) are gone.

Card 132 closes on this run, with the Traces section below still to finish.

### Traces (static, 2026-11-08)

Senior's list from the hb58 review, items 4 and 6 first (the others follow in this section).

#### Trace 4: client "util niagara WalkGround none" WARNs

- Source: `detour_c132_util_niagara` (grenades.cpp), the client's `STSpawnNiagraSystemAtLocation` fallback. It calls
  `UNiagaraFunctionLibrary::SpawnSystemAtLocation` itself (`g_spawn_niagara`) with the caller's arguments and writes
  `WARN ... none` when that returns null.
- Caller: exe+0x511BE8B, the footstep path (`NS_FX_Bugs_WalkGround`, `NS_FX_Humans_WalkGround`), with the caller's
  `bPreCullCheck` passed through.
- `SpawnSystemAtLocation` returns null, by design, when the pre-cull check culls the system: the effect type's distance
  or view cull, or its instance budget, all against this player's view. hb60: 118 WalkGround lines, 92 of them
  "none", all early in the map and away from the client's own position; the "spawned" ones are close to it
  (for example (228582, 41020, 8409) spawned, (224016, 38534, 8453) none).
- Verdict: log noise. A culled footstep is the engine's normal per-player answer; the host culls its own far
  footsteps the same way and logs nothing. Not a client defect.
- Change: the line also prints `pre_cull`; a null with `pre_cull` 1 is a plain counted line ("culled"), not a WARN.
  A null with `pre_cull` 0 stays a WARN (that one would be a real failure).

#### Trace 6: pointer sweep, the 13 unchecked singles

| Global | Pointers held | Verdict |
|---|---|---|
| g_c130r (interact.cpp) | world, actor, iface | SAFE: used only when world matches and `object_index(actor) == id` |
| g_c54_track (interact.cpp) | pawn | SAFE: pawn_id checked and a time limit before any read |
| g_c40 (enemies.cpp) | params, hit, host_pawn | SAFE: params and hit live only inside the traced call; host_pawn is compared, never read |
| g_c126 (autoplay.cpp) | world | SAFE: key only |
| g_c114 (difficulty.cpp) | world, settings, preset | world: STALE-RISK (below); settings and preset are compared only |
| g_c122 (attack_pace.cpp) | classes[] | STALE-COMPARE: log dedupe only; a class at a reused address skips one log line |
| g_fx (fx_watch.cpp) | row[].comp | SAFE: `object_index(comp) == id` before every read |
| g_c24_aim (fire.cpp) | pawn | SAFE: set only inside detour_zero_adjust |
| g_c125s (turret_sync.cpp) | world, rider_weak | SAFE: world key, FWeakObjectPtr |
| g_c120 (chan_ops.cpp) | ready_world, held_world | SAFE: compared only, and emptied at each map load (c120_arm_map) |
| g_c132_bp (enemies.cpp) | node, event, caller | SAFE: set and restored around one script frame |
| g_significance_view (enemies.cpp) | world | SAFE: compared only (a reused address only resets the line budget) |
| g_c62_instance (hooks_ubw.cpp) | ability instance | FAULT-RISK, test only: emptied at map load, but an instanced ability can be collected mid-map, and input_diag.cpp:1614 reads two bytes from it. Only with `g_c50_test` (never in the play build). Fix: hold it as an FWeakObjectPtr |

- g_c114.world: `c114_refresh(g_c114.world)` (the client's 'E' Difficulty frame) passes it to `c114_settings`, which reads
  it. It is set each tick when the world changes (difficulty.cpp:1042), not emptied at LoadMap. An 'E' frame that
  arrived between LoadMap and the new world's first tick would pass the freed world. Card 139 holds every frame
  but Ping, Pong and LedgerAck until the Pong on the new map, so that window is closed now; the fix still empties it in
  c50_arm_map.
- The object_index checks read the index from memory that may already be freed. UObject memory stays mapped in the
  binned allocator, so the read does not fault; a reused slot has a new index, so the check fails safe. That is the
  pattern the whole mod uses, not a single's defect.
- g_c75_pilots and g_c75_client: fixed in 42bec463a010f337 (`c75_map_end`), proved by hb61's reconnect.

#### Trace 5: the client "hit ... host impact is N uu from the client's NPC" WARNs

How an enemy's position reaches the client. The host's ReplicateActor for the NPC's actor channel gathers
`ReplicatedMovement` from the root component's location (NetUpdateFrequency 100.0 for every NPC in the netbudget
rows, so at most 10 ms between gathers while the connection is net ready). The client's
`ACharacter::OnRep_ReplicatedMovement` (exe+0x39b8ec0) applies it to the simulated proxy, which smooths toward it.
On this LAN pair, the expected lag is one host frame plus one client frame plus the smoothing, about 100 to 200 ms.
WarriorBug 0x4b4 moved about 740 uu/s on the host (its impacts went from x 230628 at t=217236313 to x 229926 at
t=217237262), so a client copy that follows replication should lag about 75 to 150 uu.

The WARN's metric is not that lag. It compares the host's impact point with the client's NPC, so it also counts the
distance from the impact to the actor origin. On a WarriorBug that distance can be several hundred uu (legs, a hit
on the far side), and the host does not log it. The host-side check (`hit_c132.cpp:159`, the same kHitFar = 1000 uu
against the host's own actor location) tells the two apart.

What hb61 shows: there are 22 client far lines. 2 belong to guid 0x184 and carry a host impact of (0, 0, 0), covered
below. The other 20 belong to 0x4b4 and 0x484, at t=217237745 to 217239205:
- The host logged no far WARN for any of these 20 hits. Its far cap is 20 per map and only 2 were used. All 20 rows
  were queued and relayed, so the host's check ran on each one. So on the host, every impact was under 1000 uu from
  the host's actor.
- On the client, each impact is 1011 to 1286 uu from the client copy's `ReplicatedMovement.Location`, not only from
  the drawn copy. That location is (230850, 39579, 8740) for 0x4b4 and (229394, 39355, ...) for 0x484.
- The client movesync rows for 0x4b4 hold the same values at t=217236113, 217237114, 217238125 and 217239130:
  location, replicated location and field 15 = 0. Meanwhile the host's impacts on it moved about 1000 uu in -x.
- At t=217240143 the copy jumps to (229676, 39647). The corpse pose snaps confirm the gap at the death: 0x4b4 was
  346 uu from the client's copy and 0x484 was 789 uu.

Verdict: a real desync, not a mis-tuned WARN. For about 3 s the client received no new `ReplicatedMovement` for
0x4b4 (and for 0x484), while the host's bug kept moving. The client applied what it had. Its replicated location is
stale, not ignored, so the hb43 root-motion OnRep gate (enemies.cpp:3233) is not the cause, and that gate was ruled
out before.

The missing evidence is on the host: was 0x4b4's ReplicateActor skipped for those 3 s, or did it run and write no
movement? `net_budget.cpp` already measures the per-NPC send gap. It prints only the top 6 gaps, though, and in hb61
those are always the 6 far-away bugs with 0 sends and gaps over 40 s (0x17a, 0x2da, 0x21e, 0x1e2, 0x25a, 0x228).
72 of 96 alive NPCs had a gap over 500 ms in the window ending t=217235680, and the connection was not net ready on 8
of 500 frames. So the diagnostic hides exactly the NPCs that matter.

Proposed changes, for the next card's run and not for card 132:
1. `net_budget.cpp`: rank the gap lines only over NPCs that had at least one send in the window, or that are within
   a fixed range of a player pawn. The 0-send far bugs become one count line. This names a 3 s gap on a nearby bug.
2. `hit_c132.cpp`: the host adds its actor location at the hit to `HitRow` (12 bytes per row). The client compares
   that location with its copy's `ReplicatedMovement.Location`. That is the replication lag itself, with no body
   extent in it. Threshold kHitDrift = 300 uu, about 2x the expected 150 uu at 740 uu/s and 200 ms. The
   impact-to-NPC WARN goes.
3. A host impact of (0, 0, 0) with hit bone None means the event has no hit point. hb61's two lines, 0x184 at
   damage 5.0, look like a DoT or radial tick. Today such an event gives 195824 uu "far" lines on both sides, which
   is a mis-tune. Both sides skip the far check for such a row and count it as "no hit point" in the end-of-map
   line. The gore apply at (0, 0, 0) on the client is checked at the same time. If `hit_apply` places blood at the
   world origin for these rows, it skips them as well.

#### Trace 2: nuke shot 2, why the re-granted launcher had no round

hb58, host-own:
- t=214246923: shot 1 (`FireProjectile`, launch #1274).
- t=214247262: `RemoveWeaponFromOwnerInventory BP_Weapon_NukeLauncher_C (0, pickup 1) = 1`, inventory count 2 -> 1.
  It is called from a Blueprint thunk (callers 0x524B90E < 0x0 < 0x50B8E00), about 340 ms after the shot. This is the
  launcher's own drop when it is empty. It is vanilla, and it spawns `BP_WeaponPickup_NukeLauncher_C` through
  `SpawnWeaponPickup` (exe+0x52515C0).
- t=214247381: card021 re-grants the launcher with `GiveWeaponByClass` (equipped).
- Shot 2: `FireProjectile` never ran, and the drive's wait timed out.

The static path:
- `USTWeaponInventory::GiveWeaponByClass` (0x05240150) checks the definition class and the slot, then calls the
  `UADWeaponInventory` base grant. It touches no ammunition.
- A weapon's round count is an attribute on the weapon's ASC. `GetEquippedAmmunitionCount` (0x0523AA30) reads either
  the magazine attribute or the inventory attribute. Which one depends on a tag on the weapon ASC.
  - For an ammunition entry that has no magazine (the weapon's ammunition array, stride 0x14, bool at +8 == 0),
    `ASTWeapon::SyncAttributeToEquippedInventoryAmmoCount` (0x052525D0) keeps the attribute equal to the
    inventory's `AmmoMap` count. `SyncEquippedInventoryAmmoCountToAttribute` (0x05252710) keeps the reverse.
  - `HasAmmunitionAvailable` (0x05240340) is true only when the equipped count is above 0, or when the inventory
    holds any allowed type.
- Vanilla gives a weapon only through `ASTWeaponPickup::PickupWeapon` (0x051D2FD0):
  1. It grants the weapon.
  2. For the pickup's primary ammunition (pickup+0x4D0 tag, +0x4E0 count) and its secondary ammunition (+0x4E4 tag,
     +0x4EC count), it either sets the magazine (`DoesWeaponAmmunitionTypeUseMagazine` true: 0x0524D1E0) or adds
     the count to the reserve (`AddAmmunitionTypeToInventory`).
  - `TryToCopyWeaponDefinitionStartingValues` (0x051E9030) fills those fields from the definition's starting
    ammunition when the pickup has none: the definition's arrays at +0xC8 and +0xD8, with the count at element +0xC.

Verdict: the gap comes from the mod, not from vanilla. After shot 1 the launcher was empty, with no magazine and no
reserve, so the vanilla Blueprint dropped it. The card021 test grant then added a new launcher through
`GiveWeaponByClass`, which skips the pickup's ammunition step, so the launcher had no round. Vanilla has no grant
without ammunition: every launcher arrives with its pickup's count or the definition's starting count.

Not shown by the log: where shot 1's round came from. No host-own `AddAmmunitions` or `SetAmmoCount` line was
logged. Host-own touched the NukesLarge pickup and two NukesSmall pickups at t=214131653, 0.9 s before its first
grant, and the round could have come from either of those, or from the weapon attribute's default. Either way, shot 2
has no source once the reserve is 0.

The play build is not affected. `UBWCOOP_GRANT_WEAPON` is a test variable only, and players get launchers from
pickups. Proposed change, for the next card that fires shot 2, not for card 132: after a successful grant, c21_grant_to
gives the definition's starting ammunition the same way `PickupWeapon` does. That means the magazine for a magazine
type, and `AddAmmunition` on the reserve for the other types. A cheaper alternative is that the drive fires only one
shot per grant.


#### Trace 1: impact cues, who executes them and what reaches the client

**Correction.** The probe hooks `UAbilitySystemComponent::ExecuteGameplayCue` (0x0495A340). That function runs only on the side that starts a cue. A replicated cue reaches the client through `NetMulticast_InvokeGameplayCueExecuted_WithParams_Implementation` (0x04965F70), which calls `InvokeGameplayCueEvent` (0x049643C0) and never passes through `ExecuteGameplayCue`. So the client's 0 calls in hb61 (and the hb56 note at grenades.cpp:905, "the client ran 0 Impacts cues") do not show that no cue arrived. The client does run the cue notify: hb61-client.log has 5028 effect spawns under `USTImpactGameplayCueNotify::ExecuteImpact` (0x0511E7A0).

**Static path.** The round, footstep and landing tags go through `USTGASUtilityLibrary::ExecuteGameplayCueFromHit` (0x0511DC50; return address 0x0511E27F in every audit line). It maps the tag by surface, then calls `UGameplayCueFunctionLibrary::ExecuteGameplayCueOnActor` (0x049C5DE0) on the hit actor (`FHitResult` handle +0xB8). `ExecuteGameplayCueOnActor` does this:

- It finds the hit actor's ASC: the interface first, then `GetComponentByClass<UAbilitySystemComponent>`.
- No ASC (world geometry): it calls `UGameplayCueManager::ExecuteGameplayCue_NonReplicated` (0x049C5E80). That is a local cue on whichever side ran the hit. The probe never logs it, and nothing is sent.
- ASC, and the actor's LocalRole (+0x168) is 3: it calls `UAbilitySystemComponent::ExecuteGameplayCue`, then the cue manager, then the ASC's NetMulticast.
- ASC, and the actor is not authority (every bug and every remote pawn on a client): nothing happens.

BugMelee and Grenade use a different caller. `USTCombatUtilityLibrary::ApplyDamageToActor` (0x0522DAE0, return 0x0522E80F) calls `ExecuteGameplayCue` on the damaged actor's ASC directly.

So every logged host cue ran on the ASC of the actor that was hit or damaged: a bug, a trooper or a player pawn. It never ran on the weapon or the round, so `AADWeapon` (bReplicates 0) owns none of these. The owners all replicate. Enemies and troopers have bReplicates 1 (card 023), player pawns replicate, and the client's copies have ASC ReplicationMode 2 (card 037). The earlier "rounds and weapons never arrive" prediction does not apply to these tags.

**Cue table.** Host counts are `ExecuteGameplayCue` calls from hb61-host.log. The client column counts the effects the client's `ExecuteImpact` spawned, with the host's count of the same effect in brackets. Host Niagara lines are counted once: the host logs each spawn twice, under `STSpawnSystemAtLocation` and under `SpawnSystemAtLocation`.

| Tag | Host cues | Cue owner | bReplicates | Client: arrived (log) | Predicted |
|---|---|---|---|---|---|
| Impacts.MoritaRound.AI, MoritaRound, NeedleLarge, NukeLauncherRound | 1276, 60, 10, 1 (hb58) | hit bug, trooper or pawn ASC | 1 | `MS_WPN_BulletImpact_Flesh` 956 (host 1320), all under `ExecuteImpact` | arrives by NetMulticast, which is unreliable and relevancy bound: about 72% here |
| Impacts.Footsteps.BugWalk, AIHumanRun | 102, 23 | walking bug or trooper ASC | 1 | `NS_FX_Bugs_WalkGround` 1139 (host 1455), all under `ExecuteImpact` | arrives by NetMulticast (78%). The client's own notify starts nothing, because the bug is not authority there, so there is no double |
| Impacts.Landing.Human, .Heavy | 11, 2 | player pawn ASC (`ASTPlayerCharacter::Landed`) | 1 | not split out | arrives by NetMulticast |
| Impacts.BugMelee, Grenade, Grenade.Frag | 70, 4, 1 | damaged actor ASC | 1 | not split out | arrives by NetMulticast. A grenade's explosion is the card 074 relay, not this cue |
| world-geometry hits (no ASC) | not logged | none (NonReplicated) | n/a | `NS_FX_Weapons_MoritaHit_Ground_AI`: 115 under `ExecuteImpact` plus 255 from the relay (`APlayerController::execClientMessage`, 0x0406A7B6), against host 358 | a local cue on each side's own round, plus the relay. Pairing (100 uu, 1 s) finds only 5 of the 115 local cues beside a relayed one, so doubles are rare |

**What this changes.**

1. Replicated cues do reach the client. The gap is loss, not absence: 28% of bullet-flesh sounds and 22% of bug footsteps never played on the client in hb61. The likely cause is that NetMulticast is unreliable and relevancy bound, but nothing has measured that.
2. Gore blood (`NS_FX_Gore_BloodHit_Green01`, client 1162, host 908) comes only from the relay on the client (0x0406A7B6), never from a cue, so it does not double.
3. Proof, folded into card 136 step 7 (no new run): hook `NetMulticast_InvokeGameplayCueExecuted_WithParams_Implementation` (0x04965F70) on the client and count per tag. Prediction: every tag in the table appears there, and each count equals that tag's `ExecuteImpact` count. If a tag's multicast count is close to the host's while its `ExecuteImpact` count is lower, the loss is inside the notify. If both are lower, the loss is in the network.

#### Trace 3: host-only effects in hb61 (barrels, sky plasma, archer death, death barks)

Static, from the hb61 logs and the blueprint listings (kis.py). Counts are "audit: SpawnSystemAtLocation" lines only; the host logs each Niagara spawn twice.

**Barrels.** The barrel classes are `BP_KillableBuilding_Base` → `BP_KillableProp_DamageDealer_Base` → `BP_KillableProp_ExplosiveBarrel` (the last only calls its parent's BeginPlay).

- Building OnDamaged (bound event _1): if not Destroyed, plays "Damaged SFX" (`MS_EMIT_Barrel_Damaged_Fire`), spawns gore, sets Damaged once below DamageTriggerHP%.
- Building OnDeath (bound event _0): if not Destroyed, calls Destroy.
- Building Destroy: sets Destroyed, plays the destruction VFX (`NS_FX_BarrelExplosion`) and SFX (`MS_EMIT_Barrel_Damaged_Explosion`), hides the actor or runs Ambient FX Setup, fires BuildingDestroyed. It has no Destroyed gate, so a second Destroy plays the FX again.
- DamageDealer damage handler: if FinalDamage > 0, no timer and not Destroyed, it sets DamageInstigator, sets InChainReaction = (causer is a DamageDealer), starts the DamagedLoop SFX and VFX (the burning fuse) and arms Timed Explode after FuseTime.
- DamageDealer Destroy override: calls the parent Destroy, clears the timer, then either re-arms Timed Explode with ChainReactionFuseTime (InChainReaction) or calls `ApplyDamageAtLocation(DamageInstigator or self, self, DestroyedDamageTemplate, mesh location + DmgZOffset)`.
- Timed Explode: if not Destroyed or InChainReaction, clears InChainReaction and calls Destroy.

What hb61 shows:

1. The test kill (scenario command 21, `InstaKillTarget`) passes the barrel as its own causer (`InstaKillTarget` at 0x05241580 calls the damage function with target, target), so InChainReaction is true. At the kill (217193267) the host plays Damaged_Fire and the first explosion; Destroy re-arms the fuse instead of dealing damage. About 150 ms later (217193421) Timed Explode runs Destroy again: a second explosion, then the blast, which kills barrels _1 and _3. Those are chain kills, so each also explodes twice.
2. The double explosion is a test artifact. A player's shot makes the weapon the causer, InChainReaction is false, and the barrel explodes once with the blast at death.
3. The client gets the world K relay, which sets Health to 0 and broadcasts OnDeath: Destroy runs once, the explosion VFX and SFX play (3 of each, at 217193285, 217193448 and 217193455). The client logged 0 barrel blast cues.
4. Missing on the client: Damaged_Fire (OnDamaged never runs there), the DamagedLoop burning loop (a barrel damaged but not killed burns on the host only), and the test's second explosion.
5. `ApplyDamageAtLocation` (0x1522D5C0) has no authority check itself. Whether the client's copy of the blast deals damage is decided deeper (not traced); the client logged no damage from it.

Prediction for card 136 step 7 (no run here): a barrel damaged but not killed shows the burning loop on the host only. When its fuse ends, Timed Explode's Destroy runs on the host without a Health change, so whether a K row is sent depends on the self-blast bringing Health to 0. The step-7 run counts, per barrel, host Destroy calls against client K applies; a host Destroy with no K row is the gap.

**Sky plasma (`PlasmaBugExplosion_Temporary`).** Host 26, client 23. All are high in the sky (Z 57000 to 72000), in two streams (y about −25000 and y about +48000), about every 11 s, spawned from a blueprint timer (ProcessEvent, then Invoke). Each side runs its own timer, so times and places never pair: by position all 26 host spawns are host-only. The count gap is the client's rejoin gap (no spawn between 217219196 and 217245254). Candidate sources in the log are `BP_Frontend_PlasmaProjectile_C`, `BP_Projectile_Plasmabug_C` and `BP_SatchelBuilding_Plasmabug_C` (66 lines); the source is not confirmed. Classification: a local ambient effect, not synced. Whether the full-world-sync rule covers it is the senior's decision.

**Archer death.** Scenario command 86 (`InstaKillTarget`, 217307729). The host plays `MS_BARK_ArcherBug_Death`, the `MS_EMIT_MineJumper_Death_Explosion` sound and `NS_FX_ArcherExplosion` at (226487, 39523, 8609). These come from `BP_AI_ArcherBug`'s HealthComponent death event, which also applies the on-death damage, line-traces down and spawns a deferred actor (probably the decal, lifespan 5 s). The client shows only the gib relay (gore, `MS_GORE_Overkill`, needle impacts): no bark, no explosion, no death sound and no spawned actor.

**Death barks.** `MS_BARK_WarriorBug_Death` host 38, client 0; `MS_BARK_ArcherBug_Death` host 1, client 0. On the host the bark goes through `ASTNonPlayableCharacterBase::PlaySound(FName, bool)` (0x051A0B20). Its two callers are `execPlaySound` (0x050CFD00, a blueprint call) and `ASTNonPlayableCharacterBase::Tick` (0x051AABF0). The hb61 frames show the blueprint path. The calling blueprint is not found: it is not `BP_AI_WarriorBug` and not visible in `FSM_Enemy_Dying`. The bark name exists only in `BP_AudioManager`, so the lookup is by FName. Also checked, with no PlaySound or bark name found: `FSM_EnemyBase`, `FSM_Enemy_Aware`, `FSM_Enemy_Dying` and the `AM_WarriorBug_Death_*` montages (their notifies are AN_TempRagdoll and DoAction). Ghidra's caller list for 0x051A0B20 misses 5 call references, so a native caller is still possible.

**The class gap.** On the client no NPC OnDeath blueprint handler runs. The client's death is `c38_death_edge` (enemies.cpp): gib, BugAnim OnDeath or TryPlayDeathAnimation, then `c131_client_dying` (HandleDying). So every effect a bug's blueprint makes at death (death barks, the archer explosion, its sound and decal actor) is host-only. Barrels differ: the K relay broadcasts OnDeath, so their death effects play; their OnDamaged effects do not. A class fix belongs in card 136 (effects manager): either broadcast the HealthComponent OnDeath for NPCs on the client, with the damage parts disarmed, or relay the death effects themselves.

#### Trace c: reconnect, the journal replay and OnDeath (for card 137)

1. **A reconnect reloads the map.** The drop is a local Browse to a landing map (`c45_client_browse`, restart.cpp). Once that map loads, the client calls `SetClientTravel(UBWCOOP_TRAVEL, TRAVEL_Absolute)` every kC45RetryMs, and the reconnect is a full LoadMap of the host's map (`c45_load_map_return`). Every client actor is new. `c98_arm_map` clears the client relay state (copies, journal, ledgers) at each map change.
2. **What the host replays.** At the client's first Pong of the map, `c50_join_replay` (weapons.cpp) sends: c56 pickups gone, c95 doors opened, the c98 world journal, c132 corpse poses, c132 live gore decals, c132 dropship states, and the c96 pause lift. `c98_replay` sends the journal between two R rows (rest rows M last). Between the brackets `g_c98_replaying` is true, so a K row applies Health 0 and the OnDeath broadcast with effects muted (`c98_fx_quiet`): the late joiner sees dead barrels and fallen buildings without the earlier explosions. Row kinds: S satchel, P spawn copy, D destroy copy, K kill, L timeline, X ext state, M rest, H hidden, T turret.
3. **What OnDeath runs.** A barrel: Building OnDeath → Destroy (explosion FX, hide or ambient FX, BuildingDestroyed), then the DamageDealer override (blast or chain re-arm). On the client this runs from the K relay, muted during a replay. A bug: on the host its blueprint HealthComponent death handler (archer: sound, explosion, on-death damage, decal actor; plus death barks from an unfound blueprint). On the client, only `c38_death_edge`.

#### Trace 4 and 6 build note

The Trace 4 and Trace 6 code is built in DLL 6027b0c0693dcf35 with no run. Its predictions go into card 136 step 7. The play build stays 4cb036fa33c090bc.
