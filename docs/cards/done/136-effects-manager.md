# Card 136: One effects manager, host to client

Status: accepted 2026-11-08 (hb67, test DLL 95f91961e777c34d). Run 6 passed every prediction except doubled gore
part splats on the client (16 measured; class: 1391 host splat rows plus 408 client-own). The user moved that class to
card 137 step 6: the host becomes the only source of gore part splats, together with the gib position sync.

Budget: 3 runs used through hb64; the user approved run 4 (2026-11-08) with scenario and journal-replay fixes only
and no effects code change. The user then also approved the wire name-field escape fix (hb64 gap split, 84 bad Flakbug
sound rows) into run 4, and the compact binary effects wire plus a per-op byte counter (Fault 3 trace: text rows
saturated the link and starved replication). Run 4 (hb65): no nuke fired (test grant). The user approved run 5
(2026-11-08) after the hb65 static traces and fixes; if it fails in the effects code, re-scope with the user. Run 5 (hb66): 1 of 4 nukes (regrant during the game's weapon swap), and projectile impacts
drawn twice on the client (card 132 GrenadeExplode copy plus card 136 cue rows). Re-scoped with the user (2026-11-08):
fix and run 6. The manager becomes the only source of impact effects (the card 132 projectile copy stops drawing them),
the send filter decides by instigator, not owner, and the client drops engine cue copies the manager covers. The
untraced client target misses are traced statically first. If run 6 fails in the effects code, back to the user.

Read: memory rules full-world-sync, all-effects-sent, investigate-before-carding; docs/STATE.md lines 25-33 (what the
game replicates); card 074 (GrenadeExplode), card 132 Report hb54-hb58 and its Traces section.

## Why

User (2026-11-08): "there needs to be some overall manager of these effects." Today each effect type has its own
relay (GrenadeExplode and round capture, the 'K' row death broadcast, Gib, the decal relay, the client
STSpawnNiagraSystemAtLocation fallback). Gaps were found one per run through hb54-hb58. One capture layer on the host
and one player on the client replace them for everything a player sees or hears.

Scope: visuals and sound only. Gameplay state (health, deaths, destroyed actors, pickups, objectives) stays in its own
relays. The host runs every effect itself, from the game; this card is only host to client.

## Known gaps it must close (hb55-hb58, host vs client)

- Impact cues (card 132 Trace 1, which corrects the earlier premise here): they run on the hit or damaged actor's
  ASC, which replicates, so they do arrive by NetMulticast, but only about 72% (bullet flesh) and 78% (bug footsteps)
  in hb61. NetMulticast is unreliable and relevancy bound. The manager carries them; step 0 decides whether the
  capture is at the send side (0x0495A340 and `ExecuteGameplayCue_NonReplicated` 0x049C5E80) with the client's
  multicast copy blocked, and measures the loss first (Trace 1 item 3).
- BarrelExplosion 12/3, Barrel_Damaged_Fire 4/0, PlasmaBugExplosion_Temporary 56/24 with 0 paired, ArcherExplosion
  2/0, MineJumper_Death 1/0 (hb58).
- Level-script Niagara toggles: dropship flares Flare_Fire 178/0 (FED_C2M1_Scripting instance components, NiagaraActor_*,
  BP_Flare_C_*); dropship Landing 6/0, Doors_Open 5/0, Troops_Deploy 11/0 and the door pose (hb55).
- Sky ambient plasma 69/64, positions differ (card 132 step 3b; random seed).
- Open, check from the traces: nuke fire hazard zone (missing on both sides; does vanilla spawn one), gib physics,
  RocketMuzzle on the remote NukeLauncher, WarriorBug death bark (owner null), and whether the client's view of its
  own shots comes from its local game or the host's copy.

## Senior decisions on the card 132 Traces (2026-11-08)

- Sky plasma (Trace 3) is covered by the full-world-sync rule: it is a visible effect. The host's blueprint timer
  drives it. The client's own timer is blocked, and the client plays the host's events.
- NPC death effects (Trace 3): no client OnDeath broadcast. That handler also deals damage and spawns an actor, and
  disarming those parts blueprint by blueprint is the per-type work this card replaces. The host runs the handler as
  today, the capture layer records the effects it spawns, and the client plays them. The client runs no death handler,
  so there is nothing of its own to block there. Step 0 names the actor the archer's handler spawns, and says whether
  it is an effect (carried here) or gameplay (card 137).
- Death barks: stop looking for the calling blueprint. Step 0 checks whether `ASTNonPlayableCharacterBase::PlaySound`
  (0x051A0B20) reaches a captured sound call. If it does not, it is a capture point itself.
- Barrels: Damaged_Fire and the DamagedLoop fuse are looping state, journalled per barrel (step 4). The fuse-only
  Destroy prediction (Trace 3) goes into step 7.
- The step 7 barrel test kills a barrel the way a player's shot does (the player as causer, InChainReaction false). The
  test kill with the barrel as its own causer explodes it twice on the host, which a player never sees.
- Trace 4 and 6 code (DLL 6027b0c0693dcf35) and the Trace 1 multicast count are proved in step 7, each with a
  written prediction.

## Do

0. **Traces (card 132 Traces a, b, d).** One table of every effect source on the maps we play: the host-side call
   (address), the owner and its bReplicates, how it reaches the client today, and the verdict. From it, the list of
   capture points below is final, and the client block list (step 3) is decided. The senior reviews before any code.
1. **Host capture layer.** Hook the choke points every visible effect passes, expected (confirm in step 0):
   - `USTCommonUtilityLibrary::STSpawnNiagraSystemAtLocation` (0x524C580), the ST system and sound helpers,
     `UNiagaraFunctionLibrary::SpawnSystemAtLocation` and `SpawnSystemAttached`;
   - `UGameplayStatics` PlaySoundAtLocation, SpawnSoundAtLocation, SpawnSoundAttached;
   - the decal spawns;
   - native `UNiagaraComponent` Activate, Deactivate, DeactivateImmediate (and SetActive if it does not route through
     them) on placed and level-owned components;
   - gameplay cue execution on owners that do not replicate (`UAbilitySystemComponent::ExecuteGameplayCue` 0x495A340
     and the other send-side entries), or the cue's own spawns, whichever step 0 shows gives no double.
   Each becomes one event: asset, transform, attach target and socket, parameters that change the look, the random
   seed, and the host time. One small binary frame (netcode-trust-model).
2. **Actor keys, once.** Replicated actors by NetGUID, placed actors by placed name (child actors `parent/component`,
   card 133), mod-spawned copies by their mod id. An event whose attach target the client does not have falls back to
   the world transform and is logged.
3. **Client player and block list.** The client plays each event through the same game function, with the host's
   seed. It blocks its own copies of world effects that the host sends, so nothing doubles, and keeps only effects of
   its own pawn and first-person view. The block list comes from step 0, by owner, not by asset.
4. **Late join and reconnect.** Looping or persistent effects (fires, flares, placed component on/off) are journalled as
   last state per key and replayed as state. One-shots are never replayed.
5. **Retire the per-type relays** once the manager carries their effects: the FX part of GrenadeExplode and round
   capture, Gib FX, the decal relay, the client STSpawn fallback. Gameplay parts stay. List what was removed.
6. **One log per side** (`card136: host fx <id> <asset> ...`, `card136: client fx <id> ...`) and `tools/fx_pair`
   reads it: every host event must pair with one client play, by id.
7. **One run** that checks written predictions from step 0: FED_C2M1 FAST=1, nuke (ground and on a bug), barrels, frag,
   plasma bugs, an archer, a mine jumper, the dropship landing, and one scripted client reconnect. Peak events per
   second and frame time on both sides.

## Done when

- fx_pair shows every host event paired on the client, 0 unpaired and 0 extra, or each remainder listed with its cause.
- Client screenshots next to the host's at the same time: nuke fireball, barrel explosion, dropship flares, sky.
- After the reconnect, flares and fires are back and no explosion replays.
- The Report has the trace table, the capture points with addresses, the removed relays, the peak rate, the WARN
  diff against the previous accepted run, the run length, rc, ERROR/FATAL, CRASH lines, dumps and the DLL hash
  (`sha256sum | cut -c1-16`).
- It ends with "Report complete."

## Step 0: effect source table (2026-11-08, from the hb61 logs, UBWCOOP_C132_FX=1; senior review applied)

Counts are host / client lines over the same session (`fxsrc.py`; the host's STSpawn duplicate lines skipped). Owner
bReplicates comes from the hb61 host's card098 collision lines (`placed P replicates R`, read on the host). The column
"client plays it locally" answers, for an effect caused by the receiving client's own pawn: does that client already
play it from its own run (card 024 local fire)? "yes" rows are not sent to that client; "no" rows are.

| Group | Host-side call (address) | Owner, bReplicates | Reaches the client today | Client plays it locally (own pawn's effect) | Example counts | Verdict |
|---|---|---|---|---|---|---|
| A. Impact cues on an ASC | `USTImpactGameplayCueNotify::ExecuteImpact` (0x511E800) to CreateImpactPFX / PlayImpactSound, via `ExecuteGameplayCueFromHit` and `UAbilitySystemComponent::ExecuteGameplayCue` (0x495A340) | hit actor's ASC: NPCs replicate (`STNonPlayableCharacterBase` replicates 1) | lossy cue NetMulticast plus the client's own local runs | no for hits on a bug's ASC (non-authority on the client, Trace 1): sent | MoritaRound.AI cue 1276 / 0; BulletImpact_Flesh 1320 / 956 | capture at host ExecuteGameplayCue (0x495A340); client plays through ExecuteGameplayCue_NonReplicated (0x49C5E80); the multicast copy's effect calls are blocked by owner |
| K. World-geometry hits (no ASC) | `UAbilitySystemComponent::ExecuteGameplayCue_NonReplicated` (0x049C5E80) to ExecuteImpact (0x511E800) to STSpawnNiagraSystemAtLocation (0x524C69B) to SpawnSystemAtLocation; also blueprint calls to STSpawn (exec 0x50C2B0D) | owner null in the audit (geometry) | only through the card 132 relay that step 5 retires (255 relayed MoritaHit_Ground in hb61) | yes for the client's own shots (client logs 226 non-AI MoritaHit_Ground of its own runs) | MoritaHit_Ground_AI 358 host; MoritaHit_Ground 250 / 226 | point 2 already catches them (frames 0x524C69B < 0x511BE8B < 0x511E800), but as several inner events (FX, sound). Capture 0x049C5E80 as the outer point instead, so one hit is one event and the client replays the cue whole through the same function |
| B. Gore responses | `USTCombatUtilityLibrary::SpawnGoreResponse` from `USTGoreComponent::OnDamage` / `OnOverkill`, NPC `StartDyingState` / `StartOverkillState`, corpse `OnOverkilled` | NPC replicates 1; corpses (`STDamageableActorCorpseBase`, `BP_WarriorBug_Corpses_Destructible_01/02_C`) replicate 0, placed and spawned | card 132 Gib and decal relays plus the client's own runs | not checked: step 1 logs the instigator per event, step 7 counts it | BloodHit_Green 908 / 1162 | capture at SpawnGoreResponse |
| B2. Gore floor and hit splats | `ASTGorePartNonSticky` FloorStatusChanged (0x511FFA3) / UpdateFloorDetection (0x513F4D2) / OnComponentHit (0x512EE67), `ASTGorePartSticky::OnComponentHit` (0x512F20B), ending in the decal and Niagara points | gore part, spawned on each side | each side's own physics | no | MI_FX_Gore_AlienDecal via SpawnDecalAttached (decal relay) | captured (senior 5): the host's splats are sent; the client blocks splats from its own ASTGorePart. Gib physics positions are world state: card 137 step 6 |
| C. Anim notifies on NPCs and world owners | `UAnimNotify_PlayNiagaraEffect::SpawnEffect` (0x54B37E4), `UAnimNotify_PlaySound::Notify` (0x38DD535), `USTPlayCustomSoundAnimNotify::PlaySound` (0x50FBA73), `UADAnimNotify_OneShotPFX` via `UADPFXManagerComponent::SingleUsePFX` (0x51DDB81), `USTAnimNotify_NPCFootStepsEffect` (0x50F610F) | NPC mesh, NPC replicates 1 | each side plays them from its own animation, at its own rate | yes for its own pawn and weapon notifies (muzzle, footsteps, landing `ASTPlayerCharacter::Landed` 0x51CD8F1) | AI Morita fire 1483 / 2988 (host about 175 ms apart, client about 100 ms); Bugs_WalkGround 1455 / 1139; Flakbug muzzle 324 / 258; player MoritaMk1 muzzle 92 / 61 | capture at the Niagara and sound points; client blocks notifies on NPC and world owners, keeps its own pawn and first-person view |
| D. Blueprint calls to the ST helpers (barrels, sky plasma, bug spawner, archer explosion, pain bark, doors) | `STSpawnNiagraSystemAtLocation` (0x524C580), `PlaySoundAtLocationFName` (0x5248971), `PlaySoundAtLocationFNameWithAIAwareness` (0x5248C7F), `SpawnSoundAttachedToComponentWithAIAwareness` (0x5250D9C), `SpawnSoundAttachedToPlayerWithAIAwareness` (0x5250FF5) | barrels `BP_KillableProp_ExplosiveBarrel_C` placed, replicates 0; spawners `BP_AISpawner_C`, `BP_DestructibleSpawner_Bugnest/BugHive_C`, `BP_SatchelBuilding_Plasmabug_C` placed, replicate 0; plasma sound owner STWorldSettings | partly host-only, partly both sides' own timers and handlers | no | WarriorBug_Pain 339 / 0; BarrelExplosion 6 / 3; Barrel_Damaged_Fire 3 / 0; PlasmaBugExplosion 26 / 23; BugsSpawnerSmallSpawning 4 / 0; ArcherExplosion 1 / 0; Doors_Open 5 / 0 | captured where they end (points 1 and 2); the helper itself is the outer call. Client blocks its own runs by owner |
| E. NPC native `ASTNonPlayableCharacterBase::PlaySound(FName)` (0x51A0B20), death barks | 0x51A0B20 to PlaySoundAtLocationFNameWithAIAwareness to PlaySoundAtLocationWithAccessibility (0x5249024) to `UADAudioManager::PlaySoundAtLocation` (0x4A1E0F0) to `UGameplayStatics::PlaySoundAtLocation` (0x03C62EB0) | NPC, replicates 1 | host only | no | WarriorBug_Death 38 / 0 | already reaches a captured call: no new capture point (senior decision c) |
| F. Trooper voice lines | `UADBarkSubsystem::PlaySound` (0x4C5BAF1) | subsystem, not an actor | random per side, never pair | yes for its own pawn's lines | MIBARK about 30 / about 20 | capture (it ends in SpawnSoundAttached); client blocks its own on NPC owners |
| G. Sounds from running Niagara systems | `UNiagaraDataInterfaceAudioPlayer::PerInstanceTickPostSimulate` (0x476CF9E) | the system's component | follows the system's state on each side | follows the system | Ambient_Morita_Fire 387 / 412; Dropship_Flare_Fire 52 / 0 | not a capture point: they follow the system. Capture `UNiagaraComponent` Activate / Deactivate / DeactivateImmediate for components no point 2 call created; journalled (step 4). Covers the flares |
| H. Dropship landing and doors | `ASTProceduralDropship::DescendToLandCompleted` (0x51C3141), `ASTPlayerTransportDropship` (0x51C2F88), `TickMoveDescent` (0x51E5A8D) | `BP_PlayerTransportDropship_C` and `BP_ProceduralDropshipTroops_C` placed, replicate 1; `BP_Dropship_LandingBlocker_C` placed, replicates 0 | host only | no | Landing 6 / 0; Troops_Deploy 20 / 0 | captured by the SpawnSoundAttached and PlaySoundAtLocation points |
| I. Remote pawn weapon effects | weapon fire on the other player's pawn (RocketMuzzle) | player pawn, replicates | not played | yes when the pawn is the receiving client's own (its local fire); the other player's: no | NukeLauncher RocketMuzzle 2 / 0 | capture effects owned by a pawn; send to every client except the pawn's own |
| J. Decals | `USTDecalManager::SpawnDecalAtLocation` (0x05138C40), `SpawnDecalAttached` (0x05138D60), `USTCombatUtilityLibrary::SpawnGoreDecals` (0x0524F220) | decal manager, not an actor | card 132 decal relay only | not checked: step 1 logs the instigator | no audit line yet | capture at the two USTDecalManager calls |
| L. Cascade emitters | `UGameplayStatics::InternalSpawnEmitterAtLocation` (0x03C5D7E0), `SpawnEmitterAttached` (0x03C6F450), `SpawnEmitterAttachedScale` (0x03C6F0F0) | caller's | audited in hb61 (c132 pass hooks installed on both sides): 0 calls | not checked | 0 / 0 | capture, for maps or weapons hb61 did not reach; step 7 counts them |

Archer death handler (senior decision b): `BP_AI_ArcherBug` spawns `/Game/Blueprints/LevelMechanics/BP_HazardousZone_Acid`
(package import 1) with BeginDeferredActorSpawnFromClass, then SetLifeSpan(5.0). It is a damage zone (replicates 0,
spawned at run time): gameplay, card 137 step 6. Its visible effects pass the capture points below on the host.

### Capture points (final)

1. `UGameplayStatics::PlaySoundAtLocation` 0x03C62EB0, `SpawnSoundAtLocation` 0x03C6FDD0, `SpawnSoundAttached` 0x03C70060.
2. `UNiagaraFunctionLibrary::SpawnSystemAtLocation` 0x04840630 and `SpawnSystemAttached` 0x04840B10,
   `SpawnSystemAttachedScale` 0x04840E00, `SpawnSystemAttachedWithParams` 0x04840EC0.
3. `UGameplayStatics` InternalSpawnEmitterAtLocation 0x03C5D7E0, SpawnEmitterAttached 0x03C6F450,
   SpawnEmitterAttachedScale 0x03C6F0F0 (Cascade).
4. `UAbilitySystemComponent::ExecuteGameplayCue` 0x0495A340, its effect-context overload 0x0495A3D0 (found in step 1;
   it reaches the params overload through a vtable, so it is a separate entry), and
   `ExecuteGameplayCue_NonReplicated` 0x049C5E80 for world-geometry hits. The client's copies of a cue that arrive by
   NetMulticast (`NetMulticast_InvokeGameplayCueExecuted_Implementation` 0x04965DF0, `_FromSpec` 0x04965D90,
   `_WithParams` 0x04965F70) are not blocked whole: their effect calls are blocked by owner, like every other.
5. `USTDecalManager::SpawnDecalAtLocation` 0x05138C40 and `SpawnDecalAttached` 0x05138D60.
6. `UNiagaraComponent` Activate 0x046F9EC0 (a stub: `xor r8d, r8d` then a jump to ActivateInternal), Deactivate
   0x04705220 (a stub: `xor edx, edx` then a jump to DeactivateInternal) and DeactivateImmediate 0x04705230. Events
   only for components that no point 2 call created (placed, level-owned, blueprint-constructed); a Deactivate of a
   component point 2 created is a stop event "of <id>". A direct call to ActivateInternal or DeactivateInternal
   bypasses these stubs; step 7 compares point 6 starts with the looping assets seen.

### One event per effect (nesting)

The points nest, and hb61 shows it: every STSpawn line has a SpawnSystemAtLocation line under it, and every
SpawnSystemAttached line a SpawnSystemAttachedScale line. A cue at point 4 runs a notify that calls points 1 and 2, and
point 2 activates a new component (point 6). A per-thread depth counter is held for the duration of each captured call.
Only the outermost call (depth 0 on entry) becomes an event. An inner capture is skipped, and the skip is logged with its
parent's event id. Point 6 checks that the component was not created by a point 2 call (the component pointer is
remembered for the duration of the outer call).

### Send filter (per receiving client)

Each event records its instigator pawn (the owner's instigator, or the owner if it is a pawn). An event whose
instigator is the receiving client's own pawn and whose row says "yes" above is not sent to that client. Every other
event is sent, including cues on a bug's ASC caused by that client's shots.

### Actor keys (step 2), by the bReplicates read above

- Replicates 1 (NPCs, player pawns, `BP_PlayerTransportDropship_C`, `BP_ProceduralDropshipTroops_C`): NetGUID.
- Replicates 0 and placed (barrels, `BP_AISpawner_C`, `BP_DestructibleSpawner_*`, `BP_SatchelBuilding_*`, placed
  corpses, `BP_Dropship_LandingBlocker_C`): placed name, child actors `parent/component` (card 133).
- Replicates 0 and spawned at run time (the acid zone, spawned corpses, gore parts): no key the client can resolve. The
  event carries the world transform; a looping effect on such an owner is journalled under the host's event id and
  ended by the host's own stop event (point 6 Deactivate, or the owner's EndPlay).
- The host picks the key from its own flag and puts it in the event. The client looks up whatever key it gets; on a
  miss it logs the miss and falls back to the world transform.

### Client block list (by owner)

- Blocked: effects whose owner (or attach parent's owner) is an NPC, a corpse, a placed or level actor, WorldSettings,
  a dropship, the bark subsystem on an NPC, the other player's pawn, or a gore part the client spawned itself. Blocks
  are at the effect calls only, never a whole notify or cue.
- Kept: effects of its own pawn, its weapons and its first-person view (the "yes" rows); UI.

### Step 7 predictions

- Every host event of groups A to L pairs with one client play by id: 0 unpaired, 0 extra.
- 0 events with a captured parent: each inner skip is logged with its parent's id.
- Client AI Morita fire count equals the host's (from 2988 / 1483 to one per host event).
- WarriorBug_Pain, WarriorBug_Death, Barrel_Damaged_Fire, Dropship_Flare_Fire, Landing, Doors_Open, RocketMuzzle,
  ArcherExplosion and BugsSpawnerSmallSpawning: client count equals host count (0 today).
- The client's own MoritaHit_Ground plays once (its own run), with no host copy sent to it.
- Row K, NPC rounds: the client's own world hits by NPC rounds (instigator an NPC) are all blocked, and the client plays
  the host's copy instead. Client blocked row K NPC-instigator hits equals host row K NPC-instigator events, each
  paired by id; the client's MoritaHit_Ground_AI plays equal the host's (358 in hb61).
- The client's "relayed" PlaySoundAtLocation lines (860 today) go to 0 once step 5 retires the relays.
- After the reconnect: flares and barrel fires are back from the journal; no one-shot replays.

Capture (step 1), from the host log of the same run (UBWCOOP_C136_LOG=1). A failure here uses run 2 of the 3:
- The client logs 0 `card136: host fx` lines, and the host logs no `WARN card136` line (depth, live set full).
- 0 events with a captured parent: every host skip line names a parent id on an earlier host event line.
- Row K: each MoritaHit_Ground_AI and MoritaHit_Ground hit is one `ExecuteGameplayCue_NonReplicated` event; the
  SpawnSystemAtLocation STSpawn makes under it (frame 0x524C69B) shows only as skip lines with that event's id. Every
  NPC Morita event (MoritaHit_Ground_AI, Ambient_Morita_Fire on an NPC) has an NPC-class instigator; a player hit's
  instigator is a player pawn. Host counts in the range of hb61: MoritaHit_Ground_AI 358, MoritaHit_Ground 250.
- Row A: impact cues on a bug's ASC are `ExecuteGameplayCue` events, owner an NPC class, key guid; BloodHit_Green in
  the range of hb61's 908.
- Row E: WarriorBug_Death (hb61 38) and WarriorBug_Pain are `PlaySoundAtLocation` events, owner an NPC, key world
  (at-location events are keyed to the world transform).
- Row H: Landing (hb61 6) and Troops_Deploy (hb61 20) are `SpawnSoundAttached` events whose attach owner is a dropship
  class, key guid.
- Row G: Dropship_Flare_Fire's system starts as a `NiagaraActivate` event (no point 2 call created it), owner a
  dropship or placed class, key guid or placed.
- Row L: 0 Cascade events.
- The map totals line (`card136: host fx map totals`) logs the live peak, under kC136Live (4096), with 0 not remembered.
  Step 7 sets kC136Live from that peak.
- User parameters: the `card136: host fx setvar` lines list every (asset, variable) set after the spawn, with its call
  count. Each asset marked "on a captured spawn" is listed in the step 7 report with the variable's effect (a beam end,
  a colour, a size, or not visible). Any that changes what is seen is captured as part of its event (a step 7 fix).
  SetNiagaraVariable* blueprint calls that do not route through UNiagaraComponent::SetVariable* are not counted; the
  report says whether any asset in the list moved without a row.

### Step 1: capture, built (2026-11-08, DLL 15c71339a33d40b1, not run; proved in step 7)

- Code: `mod/src/ubw/fx_c136.cpp` (enter / leave, owner, instigator, key, log lines); the capture calls sit in the
  existing detours (grenades.cpp points 1, 2, 4; proj_c132.cpp the pass detours, points 1 to 3; enemies.cpp decals).
  Every capture detour is now always installed: SpawnSystemAtLocation and SpawnSoundAtLocation moved from
  `c74_install` to kHooksUbw, and `c132_fx_install` runs unconditionally (its card 132 lines stay behind
  UBWCOOP_C132_FX=1 and skip their name lookups when off).
- Host only in step 1: the client returns at entry (step 3 adds its side). Logged on the host:
  `card136: host fx <id> t=<ms> <point> <asset or tag> at (...) rot (...) scale (...) attach <comp>/<socket> owner ...
  instigator ... key guid|placed|world look a b c[ of <id>]`, a skip line per inner capture with its parent id, and a
  seed line for each point 2 component (RandomSeedOffset, 0x52C). Caps: 200000 event lines, 20000 skip lines.
- Decals carry their size in the scale field and their life in look[0]; sounds carry volume, pitch, start time.
- Logging: event, skip and seed lines only with UBWCOOP_C136_LOG=1 (test runs set it until step 7 passes; off in the
  play build). One `card136: host fx map totals` line per map is always logged (events per point, skips, live peak,
  not remembered, found dead). With the flag, UNiagaraComponent::SetVariable* (12 entries, 0x0472E830 to 0x0472F300)
  are counted per (asset, variable) and logged per map; the play build does not install them.
- Live set (replaces the ring): each point 2 component is held with a FWeakObjectPtr until its Deactivate or
  DeactivateImmediate, or until the weak pointer no longer resolves (destroyed, collected, or a reused address). A live
  row is never evicted: a full set (4096) refuses the new row with a WARN naming the live count and peak.
- Seed (senior): the client keeps its own roll for a system without a fixed seed; positions, transforms and timing
  match, particle noise inside one effect need not. A system with a fixed seed uses the host's RandomSeedOffset.
- Instigator offset 0x1A0: `AActor::GetInstigator` (0x02E2AF10, PDB `?GetInstigator@AActor@@QEBAPEAVAPawn@@XZ`) is
  `48 8B 81 A0 01 00 00 C3` (mov rax, [rcx+0x1A0]; ret).

### Steps 2 to 6: design (2026-11-08, written before the code; proved in step 7)

Wire: a new text opcode `ChanOp::Fx`, host to client, rows ended by ';' (code `mod/src/ubw/fx_c136_net.cpp`).
- `A <i> <kind> <text>`: asset table row, kind `a` (an asset path, `c74_asset_path`) or `t` (a cue tag). The table is
  per map on the host; each receiving PC has its own count of rows sent, reset at its join replay, so a PC always has
  every row before the first event that names it (the channel is reliable and ordered).
- `E <id> <point> <asset> <of> <seed> <flags> <physmat> <atten> <conc> <component> <socket> <at> <rot> <scale> <world>
  <look a b c> <key>`: one event. `<asset>`, `<physmat>`, `<atten>` and `<conc>` (a sound's attenuation and concurrency
  overrides) are table indices (-1 none), `<of>` the spawn a point 6 call restarts
  or stops (0 none), `<flags>` bit 0 auto-destroy, bit 1 auto-activate, bits 2 to 4 EAttachLocation. `<world>` is the
  world position (for an attached call: the attach component's world location plus the offset), used when the key
  misses. Key last: `w`, `g <guid hex>` (replicated owner, `c41_guid`), or `p <name> <x> <y> <z>` (placed owner,
  `c95_wire_target`).
- `R ...`: the same fields, a journal replay row (not paired by fx_pair). `X <id>`: the host's spawn `<id>` ended
  without a captured Deactivate (component destroyed, owner EndPlay); the client stops its copy.
- Flush once per tick (`c136_step`, next to `c132_hit_step`), per ponged server-copy PC, frames up to kChanTextMax.

Send filter, decided per receiving PC P with pawn W: not sent when the owner is W or P, the owner's owner
(`AActor::Owner` 0x158) is W or P, or the attach component's owner is W; and not sent when the point is
ExecuteGameplayCue_NonReplicated and the cue's instigator is W (row K). Everything else is sent.

Client block (step 3): decided once at the outermost captured call on the client's thread and inherited by the inner
calls. Blocked when the client is not playing a host event and the effect is not its own: the same rule as the send
filter with the local pawn and PC. Ownerless effects follow the ownerless rule (senior review fixes, below). Only the effect calls (points 1, 2, 3, 5)
are skipped when blocked; they return null, a normal result for a null template. Cues and point 6 calls always run, so
an anim notify or a cue is never blocked whole. Gore decals keep the card 132 relay's suppression (below).

Client play: every row goes through the same game function with the host's values: sounds and Niagara through the
originals (WithParams as AtLocation or Attached, logged), decals through USTDecalManager, cues through
`ExecuteGameplayCue_NonReplicated` with an FGameplayCueParameters built by its own constructor (Location +0x70, Normal
+0x88, PhysicalMaterial weak +0xB8; the host reads the context overload's values through
`FGameplayCueParameters(const FGameplayEffectContextHandle&)`) on the key actor (a miss: the local PC, logged).
Cascade rows are counted as unsupported (0 predicted). Point 6 rows act on the client's copy of spawn `<of>`, else on
the key actor's component of that name; a miss on an Activate spawns the asset at `<world>` (logged). A key miss on an
attached call plays at `<world>` (logged). Each client play is logged `card136: client fx <id> <point> <asset>
<result>` with UBWCOOP_C136_LOG=1; `tools/fx_pair.py` pairs host and client lines by id.

Journal (step 4): each live point 2 spawn keeps its sent row; point 6 components no point 2 call created keep their
last Activate row (4608 rows, by weak pointer). The journal is swept 64 rows per tick: a row whose component is gone,
or inactive after it was seen active, sends `X <id>` and is removed. At a PC's join replay (`c50_join_replay`, the
first Pong) the host sends the asset table, then as `R` rows every journal row alive and active at replay time. A
one-shot that has finished is no longer in the journal, so it is not replayed.

Step 5, relays (every client effect source and what happens to it; review 2 completed the list):
- Removed: the client's `c74_play_fx` and `c74_spawn_zone` calls in `c74_receive_explode` (the GrenadeExplode and
  round FX). The host still fills those frame fields; the copy's expiry and pool call stay.
- Removed (review 2): the gore burst relay, card 132 `B` rows (`c132_decal_burst`, `client_apply_b`) and the client
  Niagara of every gore response (`c39_host_burst`, `c39_spawn_niagara`, `c39_response_niagara`,
  `c39_burst_niagara`), including the hit blood branch of `detour_spawn_gore` (hb61: 1162 BloodHit_Green01 and 22
  BloodHit_Red02 from relay frame 0x0406A7B6, `APlayerController::execClientMessage`). The host's gore Niagara are
  sent as effect rows with the gore component's owner.
- Gib FX: the client's own Gore OnDeath / OnOverkill (`c39_client_burst`, `c39_client_gib`) and hit replay
  (hit_c132.cpp) stay for their chunks and wounds (gameplay, card 137); every effect they call is owned by the NPC,
  so the owner block catches it.
- K rows (card 098 `c98_client_kill`, the placed actor's OnDeath, such as a barrel's explosion): stay for the kill;
  the blueprint's effects are owned by the placed actor and blocked, point 6 included (below). The host sends them.
- Point 6 on the client (review 2): an Activate, Deactivate or DeactivateImmediate under a blocked decision is
  skipped when the component has an owner (the host sends that op, placed owners included). An ownerless one at the
  outermost call still runs and is counted as extra.
- Kept: the gore decal relay (`decal_c132.cpp` D, E rows). It carries FIFO eviction, pool order and fade, which the
  manager does not; the manager does not send gore decals (`c132_decal_gore`), and the client's own gore decals stay
  suppressed. Its spawns are counted as extra.
- Kept: the client STSpawn fallback (`detour_c132_util_niagara`). The client has no GameMode, so STSpawn returns null
  there; its own effects and the replayed cues need the fallback. It calls SpawnSystemAtLocation through its detour,
  so the owner block decides it like any other call.

Client "extra" (review 2): `card136: client fx extra: <n> assets, ...` then one `card136: client fx extra <count>
<asset>` line per asset in the client map totals. It counts effects the client played that came from neither the
manager, nor its own effects, nor were blocked: the gore decal relay's spawns (by material) and ownerless point 6
ops.

Step 7 predictions added by steps 2 to 6:
- fx_pair: every host `E` row sent pairs with one `card136: client fx <id>` line; unpaired rows are listed by cause
  (filtered for the receiving PC; key miss with fallback still pairs).
- Client `card136: client fx block` totals per map: blocked effect calls > 0, 0 blocked calls while playing.
- After the reconnect: the client logs `R` replays for the flares and barrel fires and 0 for one-shot assets.
- 0 `WARN card136` lines on either side except key misses, each with its fallback.

### Senior review fixes, steps 2 to 6 (2026-11-08, DLL e489f861ef32c6db, not run; proved in step 7)

1. Ownerless rule. An effect with no owner and no instigator is sent when it is located: it has a world location
   (every Niagara, emitter and decal call, and a sound called with a location) or an attach component. Each client
   blocks its own ownerless located copy and plays the host's. It is local only, never sent and kept on the client,
   when it is a sound with no location and no attach, or when its world context is a UUserWidget. Code: `local_only`
   in fx_c136_net.cpp, used by `c136_net_queue` (host) and `c136_client_own` (client); `C136Call::located` is set by
   the PlaySoundAtLocation and SpawnSoundAtLocation detours.
2. Finished loops. The sweep reads `UActorComponent::bIsActive` (found by reflection once, then `c29_flag`). A row
   that was seen active (a spawn with bAutoActivate, a placed Activate, or a later sweep) and is now inactive sends
   `X` and is removed. A spawn made inactive stays until it is activated or destroyed. The 5 s replay age gate is gone.
3. Reused asset addresses. Each host asset row holds a FWeakObjectPtr (index and serial) checked on lookup. A row whose
   weak pointer no longer resolves to the same object is marked dead; the new object gets a new row, so each PC gets a
   new `A` row before its first use.
4. Sound settings. PlaySoundAtLocation and SpawnSoundAtLocation (attenuation a8, concurrency a9) and
   SpawnSoundAttached (a11, a12) send their overrides as asset rows; the client finds them by path and passes them to
   the same original (a miss uses the sound's own, counted as "sound setting" in the client totals). The WithParams
   entries now send their switches: FFXSystemSpawnParameters 0x68 LocationType, 0x69 bAutoDestroy, 0x6A bAutoActivate
   (read from SpawnSystemAtLocationWithParams 0x04840900: 0x69 goes to CreateNiagaraSystem, 0x6A gates Activate;
   0x6B pooling, 0x6C bPreCullCheck, 0x6D bIsPlayerEffect are not sent).
5. First-person effects (`fp_view`). Classes, from the hb61 card 132 audit:
   - Camera: an effect attached to the pawn's CameraComponent (pawn+0x818) is not sent. None in hb61.
   - Remapped: an effect attached to the pawn's FirstPersonMesh (pawn+0x848) or to any component of a weapon actor whose
     owner is a player pawn. hb61: NS_FX_Morita_MuzzleFlash3 (92, SpawnSystemAttachedScale on BP_Weapon_MoritaMk1_C),
     NS_FX_Weapon_RocketMuzzle (2) and MS_WPN_NukeLauncher_Fire (2) on BP_Weapon_NukeLauncher_C, MS_WPN_AI_Morita_Mk1
     (64), NukeLauncher_Interupt, Arming_Seq and MoritaMK1Reload_Mech_MS (1 each) on the pawn's first-person parts.
     Sent attached to the pawn's root component, key the pawn's NetGUID, socket none, KeepWorldPosition, at and rot
     the socket's world transform (`USceneComponent::GetSocketLocation` 0x03AE2980 and `GetSocketRotation` 0x03AE2D00)
     composed with the relative transform (KeepRelativeOffset: socket_at + R(socket)·at, R(socket)·R(rot); the snap
     types drop it; scale not applied). No GUID: sent at world, counted.
   - Not sent: a point 6 call on a first-person component (no third-person twin to act on).
   - Not first person: NS_FX_Weapon_Morita_Casing and MoritaMK1Fire_MS are at-location. Review 2 gives them an owner
     (below), so the shooter plays its own and the host sends them to the other PCs.
   The host totals line counts each class.

Notes:
- The client's asset table is never reset: each map's rows overwrite the old ones by index before any event uses them.
- Placed rows send `X` too (the sweep). The client keeps no live copy for a placed row, so it ignores that `X`.
- A placed component's Deactivate carries its last Activate row's id as `of`, so the client stops the copy it spawned
  when its key missed.

### Senior review 2 fixes (2026-11-08, DLL 6c1347d4aba1dc64, not run; proved in step 7)

1. Anim notify owner. The casing (`UAnimNotify_PlayNiagaraEffect::SpawnEffect` 0x054B3730) and fire sound
   (`UAnimNotify_PlaySound::Notify` 0x038DD380) pass the UWorld as world context, so the context outer walk finds no
   actor. `UAnimInstance::TriggerSingleAnimNotify` 0x03865D00 is hooked (`detour_c136_anim_notify`) and keeps the
   notifying anim instance in a thread_local while it runs; `c136_owner` walks that instance's outer chain (anim
   instance, skeletal mesh component, actor) when the context walk finds none. The owner is the weapon or the pawn;
   `make_filter` already matches a weapon through its owner.
2. Step 5 finished: the list above (B rows and gore Niagara removed, point 6 block, extra counter).
3. First-person KeepRelativeOffset composed as a transform (`compose` in fx_c136_net.cpp: FRotationMatrix axes,
   rotate the offset and the relative axes by the socket rotation, back to a rotator as FMatrix::Rotator does).

Not changed: `p.world` for a non-first-person attached row (fx_c136_net.cpp, component translation plus the relative
location in world axes) is the journal's and the log's position only; the client plays attached rows relative to
their component.

#### Review 2 predictions (step 7 run, `tools/scenarios/c136-step7.txt`, FED_C2M1)

The step 7 predictions above stand. In addition:

- Casing and Fire_MS: for each Morita burst, the shooter's own PC plays them natively and gets 0 rows for them; the
  other PC gets one row per host event, played. A host burst gives host events with owner the host pawn or weapon
  (through the anim notify instance), sent to the client; a client burst gives events on the client's server copy,
  not sent to that client (send filter) and played by the host natively.
- Client extra counter (`card136: client fx extra` lines at map end): rows only for gore decal materials, each count
  equal to that map's applied D rows, and for ownerless point 6 calls. Every other asset 0. Any Niagara or sound asset
  in that list that is not ownerless point 6 is a leak and a step 7 fix.
- Reconnect: the host logs one `card136: host fx join replay` line for the rejoining PC; the replayed rows are only
  placed and looping ones (Dropship_Flare_Fire, barrel fires); 0 one-shot explosion rows (barrel, nuke) replayed. The
  `rejoin-flares` and `rejoin-barrels` shots show flares and fires as on the host.
- Nuke: on the ground, on a WarriorBug, an ArcherBug and a JumperBug, each fireball is one host event tree and one
  client play by id; the client's same-time shot shows the fireball. The ArcherBug's ArcherExplosion and the
  JumperBug's MineJumper death explosion reach the client as host rows, not client natives (both on the K or E row
  owner block).
- Peak events per second: computed from the host `card136: host fx <id> t=<ms>` stamps (most events in any 1000 ms
  window), expected highest in the nuke and soak parts, under the send budget (no failed sends in the net map totals).
- Frame time: `card132: <side> t=<ms> frame window` lines on both sides; mean and max in the range of hb61 on each
  side, no window with more over-50 ms frames than hb61's worst.
- Same-time shot pairs: morita-host-fire, morita-client-fire, flares, sky, barrel-explode, barrel-after, nuke-ground,
  nuke-warrior, nuke-archer, nuke-jumper, rejoin-flares, rejoin-barrels. Each pair shows the same effects.
- Relays, as the step 5 list: GrenadeExplode FX, gore 'B' rows and the c39 gore Niagara relay removed; Gib FX,
  K rows and the STSpawn fallback stay for gameplay and their effects are caught by the owner block (0 extra rows
  for their assets); the gore decal relay stays and counts as extra by design. The client's "relayed"
  PlaySoundAtLocation lines are 0.


#### Step 7 run 1 (hb62, DLL 6c1347d4aba1dc64): host crash at map load, fixed

- Crash: access violation reading 0x12 at ubwcoop+0x8117C in `detour_c132_niagara_attached_scale`, under a WarriorBug
  spawn's SpawnSystemAttached (NS_Compute_DamageAndRadius). tools/symbols.txt had the two Niagara SpawnSystemAttached
  overloads' names swapped: the mangled name with `@78@3` (back reference 3, a second TVector) is the Scale overload
  (0x04840B10), so the detour on 0x04840E00 read EAttachLocation (2) as a Scale pointer. Names swapped back; the Scale
  detour's bAutoActivate is a10 (a9 is ENCPoolMethod); the client's Scale play slots follow the Scale order.
- `WARN card136: host fx queue full this tick, rows dropped (512)`: the map load and a wave start each activated about
  400 placed systems in one tick. kQueue is 4608 (the journal's bound).
- The client logs `card136: host fx` lines on its menu map before the join (standalone, by design).
- Run 2: hb63, DLL 970cf322d6594570.

#### Step 7 run 2 (hb63, DLL 970cf322d6594570): client crash at the first WarriorBug, fixed

- The client died 53 s in (line 33). AV reading 0x618 at exe+0x472EA6E. exe+0x4BBA707 calls SpawnSystemAttached
  (0x04840E00, NS_Compute_DamageAndRadius on a new BP_AI_WarriorBug_C), then passes the result to exe+0x472EA40 with
  no null check, and that function reads `+0x618` of it. On the client the blocked attached spawn returned null
  (C136_PASS: blocked, original skipped). hb62 never reached a client bug, so this is the first run to hit it.
- Fix (class: every attached spawn whose caller gets the component): C136_PASS_INACTIVE. On the client a blocked
  attached spawn runs the original with its bAutoActivate slot cleared. The caller gets a real component that never
  activates. It dies with its parent, and a later game Activate of it is blocked as point 6 (owned component). The
  host's row plays the visible effect. Applied to SpawnSystemAttached (a8), SpawnSystemAttached with Scale (a10),
  SpawnSystemAttachedWithParams (byte 0x6A of the params, restored after), SpawnEmitterAttached (a9) and
  SpawnEmitterAttached with Scale (a10). The at-location spawns keep the null return: the engine returns null there
  itself for a pre-cull, so their callers check it.
- hb63 ran with the symbols fix (6c2611e7054fc1ff) and the queue fix; it did not have the emitter flag fix below.

#### Sibling sweep: every hooked overload against its signature (tools/symbols.txt, MSVC x64)

By-value TVector, TRotator and the cue context handle are passed as a pointer to a copy. Slots 5 and later are on the
stack, so a float there reads as the low 32 bits of the slot.

| Family | Address | Signature (after `this`, if any) | Detour | Slots read | Verdict |
|---|---|---|---|---|---|
| SpawnSystemAtLocation | 0x04840630 | ctx, System, Loc, Rot, Scale, bAutoDestroy, bAutoActivate, ENCPoolMethod, bPreCull | detour_c74_niagara | all 9, typed | match |
| STSpawnNiagraSystemAtLocation | 0x0524C580 | same as SpawnSystemAtLocation | detour_c132_util_niagara | all 9, typed | match |
| STSpawnSystemAtLocation (USTParticleEffectManager) | this + same 9 | same | detour_c74_st_niagara | this + 9, typed | match |
| SpawnSystemAtLocationWithParams | 0x04840900 | const FFXSystemSpawnParameters& | detour_c132_niagara_params | a1 fields 0x00 to 0x6A | match |
| SpawnSystemAttachedWithParams | 0x04840EC0 | const FFXSystemSpawnParameters& | detour_c132_niagara_attached_params | a1 fields | match |
| SpawnSystemAttached | 0x04840E00 | System, Comp, Name, Loc, Rot, EAttachLocation, bAutoDestroy, bAutoActivate, ENCPoolMethod, bPreCull | detour_c132_niagara_attached | a1 to a5, flags a6 a7 a8 | match (hb62 fix) |
| SpawnSystemAttached with Scale | 0x04840B10 | System, Comp, Name, Loc, Rot, Scale, EAttachLocation, bAutoDestroy, ENCPoolMethod, bAutoActivate, bPreCull | detour_c132_niagara_attached_scale | a1 to a6, flags a7 a8 a10 | match (hb62 fix) |
| client play, attached with Scale | | as above | fx_c136_net.cpp NiagaraAttachedScale slots | Pool 0 in a9, activate a10, precull a11 | match (hb62 fix) |
| PlaySoundAtLocation | 0x03C62EB0 | ctx, Sound, Loc, Rot, Volume, Pitch, Start, Atten*, Conc*, const AActor* owner, Params* | detour_c74_play_sound | all 11, typed | match |
| SpawnSoundAtLocation | 0x03C6FDD0 | ctx, Sound, Loc, Rot, Volume, Pitch, Start, Atten*, Conc*, bAutoDestroy | detour_c74_spawn_sound | all 10, typed | match |
| SpawnSoundAttached | 0x03C70060 | Sound, Comp, Name, Loc, Rot, EAttachLocation, bStopWhenAttachedToDestroyed, Volume, Pitch, Start, Atten*, Conc*, bAutoDestroy | detour_c132_sound_attached | a6 location, a7 stop, a8 to a10 floats, a11 a12, a13 destroy | match |
| InternalSpawnEmitterAtLocation | 0x03C5D7E0 | World, Template, Loc, Rot, Scale, bAutoDestroy, EPSCPoolMethod, bAutoActivate | detour_c132_emitter | a1 to a5 | match |
| SpawnEmitterAttached | 0x03C6F450 | Template, Comp, Name, Loc, Rot, EAttachLocation, bAutoDestroy, EPSCPoolMethod, bAutoActivate | detour_c132_emitter_attached | a1 to a5; flags not read | **fixed**: flags a6 a7 a9 (the row said KeepRelative, destroy and activate always) |
| SpawnEmitterAttached with Scale | 0x03C6F0F0 | Template, Comp, Name, Loc, Rot, Scale, EAttachLocation, bAutoDestroy, EPSCPoolMethod, bAutoActivate | detour_c132_emitter_attached_scale | a1 to a6; flags not read | **fixed**: flags a7 a8 a10 |
| ExecuteGameplayCue | 0x0495A340 | this, FGameplayTag (8 bytes, by value), const FGameplayCueParameters& | detour_c74_cue | asc, tag, params | match |
| ExecuteGameplayCue (context) | 0x0495A3D0 | this, FGameplayTag, FGameplayEffectContextHandle (by value: pointer to copy) | detour_c136_cue_context | asc, tag, context* | match |
| ExecuteGameplayCue_NonReplicated | 0x049C5E80 | static: AActor*, FGameplayTag, const FGameplayCueParameters& | detour_c136_cue_local | target, tag, params | match |
| SpawnDecalAttached | 0x05138D60 | this, Material, Size, Comp, Name, Loc, Rot, EAttachLocation, LifeSpan | detour_spawn_decal_attached | all 9, typed | match |
| SpawnDecalAtLocation | 0x05138C40 | this, ctx, Material, Size, Loc, Rot, LifeSpan | detour_spawn_decal_at | all 7, typed | match |
| UNiagaraComponent::Activate | 0x046F9EC0 | this, bool bReset | detour_c136_activate | component, reset | match |
| UNiagaraComponent::Deactivate | 0x04705220 | this | detour_c136_deactivate | component | match |
| UNiagaraComponent::DeactivateImmediate | 0x04705230 | this | detour_c136_deactivate_now | component | match |
| UAnimInstance::TriggerSingleAnimNotify | 0x03865D00 | this, FAnimNotifyEventReference& | detour_c136_anim_notify | anim instance, ref | match |

The client does not play EmitterAttached rows yet (no Cascade play path in fx_c136_net.cpp), so the emitter flag fix
changes only the host's row and log, not a client spawn.

#### Standalone gate

- Capture runs only on a listen server. The host side reads the map's ENetMode once, from the first outermost
  capture call that has an owner actor (net_mode(owner)). c136_arm_map resets it. In standalone (single player, the
  menu, a client's menu map) every call skips the event at once, and the map totals line counts it
  ("net mode N, M standalone calls skipped"). UWorld::Listen is not hooked in this build ("hooks: skipped:
  UWorld_Listen"), so the mode is read from an actor.
- The "at least one remote connection" part is not applied. A listen server with no client must still journal: the
  join replay and the reconnect replay (hb63 part 5) need the placed and looping rows that started while no client
  was connected.

#### hb63 review item: gore decals

- The host never sends a gore decal as a point 5 row. c136_net_queue drops it and counts it ("N gore decals left to
  card 132", fx_c136_net.cpp `counts.gore`). So a splat reaches the client only as its D row, and the client's extra
  count for gore materials is the D rows applied (decal_c132.cpp client_spawn calls c136_client_extra). That is one
  decal per splat, by design. Row B2's claim was wrong. Run 3 checks it with numbers: the host's "gore decals left
  to card 132" against the host's D sent, and the client's gore extra against the D applied.
- Run 3: hb64, DLL f3dce55a76e6fad5 (attached spawn fix, emitter flags, standalone gate).

#### Step 7 run 3 (hb64, DLL f3dce55a76e6fad5): no crash; effects numbers met, three scenario faults

Both games exited 0 after 247 s, with no crash dumps, 0 FATAL or ERROR lines, and 7 host and 29 client WARN lines.
None of the WARN lines is card 136. The card 132 WARNs are the known hit-distance and spawner-location lines.

Effects manager, against the predictions:

- Owner block and ids: there were 22259 host events and 20560 client rows, with 0 client rows that had no host id.
  The client blocked 539 outermost calls and kept 0.
- Gore decals: before the drop, the host made 400 gore SpawnDecalAttached events (386 AlienDecal and 14 Human_Decal).
  None was sent as a point 5 row. The client applied 400 D rows ("412 rows, 400 spawned") and its extra counter was
  386 MI_FX_Gore_AlienDecal plus 14 MI_FX_Gore_Human_Decal = 400. So there is one decal per splat. The client's extra
  list holds 2 assets, both gore decals. No Niagara or sound asset leaked.
- Morita: Casing had 119 host events, 119 rows and 119 played. MoritaMK1Fire_MS had 120, 120 and 120. All of these were
  host-weapon events, and none was played on the shooter's own PC. The client burst was not proven (see the faults
  below).
- Nuke: there were 4 NukeLauncherRound cues on the host. Each became one client row, played, and the client spawned
  NuclearExplosion05 at the host's position 5 to 45 ms after the host did. All 4 came from 2 shots in the Morita step
  and 2 in the nuke-ground step.
- Reconnect: the drop happened, and the client reconnected on try 1, 8442 ms after the drop. The host logged "join
  replay: 131 asset rows, 58 journal rows of 58". The 58 replayed rows were 49 Gore_Gibs_Trail, 4
  NS_Compute_DamageAndRadius, 3 bug windup muzzles, 2 WA_Flares and 1 Viking thrusters. 0 one-shot explosions were
  replayed (no barrel, nuke or blood hit). Open question for the senior: the 49 gib trails replay "spawned at world
  (component miss)". These are trails on gibs that may already have settled or despawned.
- Peak: 1590 host events per second, at t=225875542 (the second ground nuke). No card 136 send failure was logged.
  The host's last-map net totals were not printed, because the host quit before the map ended.
- Frame windows (23 per side): host mean max 27.3 ms, max 3795.8 ms (the map-load window at t=225829853), worst 4
  frames over 50 ms. Client mean max 17.4 ms, max 3399.6 ms, worst 3 frames over 50 ms. Every figure is below hb61's
  (host 33.0, 4254.3 and 5; client 27.0, 3878.2 and 4).
- Standalone gate: the menu map logged "net mode 0, 24 standalone calls skipped" on both sides, with 0 events. The
  later client menu loads logged 37 and 30 skips. FED_C2M1 ran in net mode 2.

Scenario faults. These are not effects-manager faults:

1. The client was still seated in the dropship during steps 1 and 2. Its pawn was at (232629, 43682), and "warp
   client" at t=225838858 moved only the server copy. So the client did not fire (its Morita stayed at 60 rounds), and
   the morita-client-fire and flares shots aimed from the seat. A wait for the client to leave the dropship is needed
   before step 1.
2. Nukes 2 to 4 never fired, which is why the line 71, 80 and 89 waits were TRY-FAIL. The empty launcher drops after
   each shot and the card 021 grant re-adds and equips it. The regrants at t=225842316 and t=225843322 put the nuke back
   in the host's hands during the Morita burst, so 2 rounds were spent there. The 6th grant, at t=225875420, hit
   kC21GrantMax = 6 ("grant of NukeLauncher stopped: grant cap reached"). So the WarriorBug, ArcherBug and JumperBug
   nukes had no launcher.
3. The "warp client" at t=225870818 did not move the client either. The client looked from (196820, 3238), the barrel
   point, at t=225874157. So the nuke-ground client shot faced away from the fireball, although the client log shows
   it spawned. Cause traced from the hb64 logs (below, "Fault 3 trace"); it is not a scenario fault.
4. The line 34 flare wait was a TRY-FAIL because the first Dropship_Flare_Fire came at t=225898831, about 38 s after
   the wait. The dropship was not there yet. The line 104 flare wait after the rejoin passed.

#### Fault 3 trace (static, from the hb64 logs): the host link was saturated

- How `warp client point` works (`c75_warp` in testrun.cpp): it calls SetActorLocation on the host's server copy
  only. The client's pawn follows only through CharacterMovement corrections (ServerCheckClientError, then
  ClientMoveResponsePacked). Those are sent only in reply to the client's ServerMoves, and only when the
  connection is net ready.
- Warp 14 (t=225864898) worked. Sixteen `srv: correction` lines followed it from t=225864910, with error 48433 uu,
  and the client then looked from (196820, 3238). That 5 s window was not net ready in 24 of 597 frames.
- The barrel kill at t=225867255 shunted the server copy about 570 uu (y 3240 to 2667). The client refused its own
  `GA_ShuntAbilityCharacter_C` (card 065), so the client did not move. Corrections started at t=225867541, and the
  log cap ("ServerCheckClientError correction ... further events not logged") was reached at t=225867626.
- From the window that ends at t=225869250 until the scripted disconnect at t=225943202, the host connection was not
  net ready in 90 to 100% of frames: 363/524, then 564/564, 530/530, 595/595 and so on, for 15 windows in a row.
  In those windows the netbudget lines show only `ClientMessage` (the mod channel): 47 to 790 KB per 5 s, about
  6.1 MB in 75 s against a 100000 B/s cap. There were 0 actor property sends, 0 NPC sends and 0
  `ClientMoveResponsePacked` calls. Before that, every window had 12 to 47 move responses.
- Warp 20 (t=225870818) fell inside that window, so no correction reached the client. The client stayed at
  (196819, 3238) until the drop, and it next showed up at (229924, 38271) after the rejoin, when the link was idle.
- Share of the load: the host made about 24000 effects events in those 15 windows. An E row from `host_body` is 27
  space-separated fields, about 130 to 170 characters, so the effects rows are about 3.1 to 4.1 MB, which is 50 to
  70% of the channel bytes. There is no per-op byte counter, so this is an estimate. Earlier runs without card 136
  rows (hb53 to hb61) had at most 2 saturated windows each; hb64 had 16 of 42.
- Cause: the effects wire volume during combat saturates the host connection. While it is saturated, all actor
  replication starves, not only the warp: NPC positions and movement corrections too. This is an effects-code
  defect (row size and rate), not a scenario fault. A retry would not fix it.

#### hb64 gap split (static, from the hb64 logs; `tools/fx_gap.py`)

The 1699 is a line count: 22259 host events minus 20560 client rows. The client rows hold 20505 distinct ids, and 55 ids
have two rows (an E row before the drop and the R row of the join replay). So 1754 host ids never reached the client
as a row, and 1754 - 55 = 1699. Each missing id was put in the first part that fits, by its host event line:

| Part | Ids | How it is known |
|---|---|---|
| Before the client's first Pong (host alone) | 882 | ids 1 to 882, all before `card026: host pong 1` at t=225836674; the host map totals at t=225826628 say 882 events, 0 rows sent |
| Disconnect: client Browse to the menu (t=225943202) to the host's first Pong of the rejoin (t=225957373) | 204 | event times in that window; the host closed the connection at t=225943295 to 225943303 |
| After the host quit closed the connection (t=226056406, `cmd: 51 quit` at t=226056313) | 59 | ids 22201 to 22259, all NiagaraDeactivate of NS_Compute_DamageAndRadius on WarriorBugs at teardown; the last client row is id 22200 |
| Gore decals, relayed by card 132 D rows (`counts.gore`, not point 5 rows) | 500 | 400 before the drop (as the run 3 report) and 100 after the rejoin |
| Filtered as the client's own: cue on the client pawn's ASC (owner is the pawn) | 18 | 13 MoritaRound.AI, 4 BugMelee, 1 Barrel, all owner BP_PlayerCharacterMI_C; 23 like events with the host pawn as owner were received. Same-ms pairs (3819 received / 3820 missing, 3952/3953, 4087/4088) are one melee on both pawns |
| Filtered as the client's own: NonReplicated cue with the client pawn as instigator | 7 | Impacts.Landing.Human on Landscape; 108 of the 115 such events (host pawn) were received |
| local_only | 0 | |
| First person drop | 0 | |
| Send failures | 0 | no missing id is left over; the host printed no net totals (it quit before the map ended) |
| Client parse failure: comp name with a space | 84 | see below |
| Unexplained | 0 | |

The player-pawn instance is not in the event line, so the two "filtered" parts are by elimination: once an event with
the pawn as owner or instigator is queued, no other host drop applies (not off thread, not gore, not local_only, not
first person, no queue-full WARN), and the received events of the same kind are the host pawn's.

**Defect found (effects code, wire format).** 90 host events attach to a component named `Box Component`
(SpawnSoundAttached MS_ATK_FlakBug_Fire_ChargeUp and EMIT_FlakBug_Fire_MovementGuttural on
BP_SatchelBuilding_Flakbug_C). The host writes the component name into the space-separated row (`host_body`, `%s`).
The client's `client_parse` reads it with `%63s`, which stops at the space, so the row fails to parse and is counted
as bad. 84 of the 90 were in send windows: 42 before the drop, which is exactly the client's map total of "42 bad", and
42 after the rejoin. The other 6 fell in the windows above. So on the client the Flakbug charge-up and guttural fire
sounds never play. The same parse risk applies to the socket (`%63s`) and to a placed key `parent/component` name
(`%64s`) if a name holds a space; hb64 has no such socket or placed name. The fix is in the effects wire, so it is not
in run 4 unless the senior and the user approve it.

#### Run 4 wire format: compact binary rows (2026-11-08, written before the code; user approved option (a))

The fault 3 trace puts the hb64 link saturation on the effects wire: text E rows of 27 fields, 130 to 170 chars each.
Run 4 replaces the text rows with binary rows and adds a per-op byte counter. The name-field fix (the `Box Component`
parse failures) is folded in: strings appear only in asset rows and name rows, length-prefixed, so no field is parsed
by whitespace or ';' any more.

**Frame (ue/chan, new kind `kChanArgsBlob` = -4).** It reuses the channel's one packing rule: every unit after the
opcode is a 15-bit value + 1, so no unit is 0 and every unit is below the surrogate range. There is no second escape.
The frame is the prefix, the opcode, one length unit (the byte count, 1 to `kChanBlobBytes`), then the bytes packed
little-endian into 15-bit units (`ceil(bytes * 8 / 15)` units, the last one padded with zero bits), then the
terminator. The card 132 stamp goes before the terminator as on every frame. `kChanBlobUnits` = 1400 data units
(the text frame's size, so the stamped buffer is unchanged), which gives `kChanBlobBytes` = 1399 * 15 / 8 = 2623
bytes. The receiver checks the length unit against the unit count, then unpacks. A payload of 15-bit units is not
pure ANSI, so FString serialization sends it as UCS-2: 2 bytes per unit, which is 16 wire bits per 15 payload bits.
The frame overhead is 14 units (prefix 6, opcode 1, length 1, stamp 5, terminator 1), 28 bytes per frame, shared by
all the rows in it.

**Rows.** A frame holds whole rows, back to back. Every row starts with a one-byte tag. A row never spans two frames.
Integers are unsigned LEB128 varints (7 bits per byte) unless a width is given.

| Tag | Row | Fields after the tag |
|---|---|---|
| 1 | A, asset | varint index, kind byte (0 asset path, 1 cue tag), u8 length, the path bytes |
| 2 | N, name | varint index, u8 length, the name bytes |
| 3 | E, event | see below |
| 4 | R, replayed event | the same fields as E (a journal row's body under tag 4) |
| 5 | X, end | varint id |
| 6 | D, deactivate | point byte (NiagaraDeactivate or DeactivateImmediate), varint id, varint `of` (the row it stops) |

A and N rows are sent once per peer per map, ahead of the first row that uses them, like the text A rows were. Both
tables start over on each map (host) and are overwritten by index (client). A string is 1 to 255 bytes. A name longer
than 255 bytes is not truncated: the event that needs it is not sent and counts as `bad (too long)` on the host. The
host reads names with their full length (`object_name_into` for objects, the FName entry header for sockets), so a
long name is seen and not silently cut at 63 chars as `fname_text` would cut it.

A Deactivate whose `of` is known (the journal row of the spawn or placed Activate it stops) goes as a D row. One with
`of` 0 (no journal row) goes as a full E row, because the client then needs the key and component to find the target.

**E row.** It is: tag, varint id, point byte, u16 asset index (0xFFFF none), u16 presence mask, then only the present
fields, in mask-bit order:

| Bit | Field | Present when | Encoding |
|---|---|---|---|
| 0 | location | not (0, 0, 0) | float32 x3 |
| 1 | rotation | not (0, 0, 0) | u16 x3. Rotator degrees: `round(deg * 65536 / 360) & 0xFFFF`, decoded to (-180, 180]. Cue points (Cue, CueContext, CueNonReplicated) carry the cue Normal here: int16 x3 of `round(v * 32767)` |
| 2 | scale | not (1, 1, 1) | float32 x3 |
| 3 | attach component | an attach or acted-on component | varint name index |
| 4 | socket | not None | varint name index |
| 5 | key: replicated owner | a NetGUID | varint NetGUID |
| 6 | key: placed owner | a placed target | varint name index ("name" or "parent/component"), then float32 x3 (the anchor's location) |
| 7 | owner class | an owner | varint name index |
| 8 | look | not (0, 0, 0) | float32 x3 (sound volume, pitch, start time; decal fade) |
| 9 | seed | not 0 (a fixed-seed system) | int32 |
| 10 | world location | an attached point (the key-miss fallback) | float32 x3 |
| 11 | of | not 0 | varint id |
| 12 | flags | not 0 | u8 |
| 13 | physical material | a cue's PhysicalMaterial | varint asset index |
| 14 | attenuation | an override | varint asset index |
| 15 | concurrency | an override | varint asset index |

Neither key bit set means the world key ("w"). Bits 5 and 6 are never both set. The host's event line keeps every field
in text, so the pairing by id with the client line is unchanged. The client line gains the owner class.

Expected size: a world Niagara spawn with location, rotation and flags is 1 + 3 + 1 + 2 + 2 + 12 + 6 + 1 = 28 bytes,
32 with a seed; a SpawnSoundAttached with attach, socket, guid key, owner class, look, world and flags is about 48. The
target average is about 30 bytes per E row. hb64's text rows averaged 130 to 170 chars (1 byte each, ANSI), so the
effects wire drops about 4 to 5 times.

**Byte counter (both sides, netbudget lines).** ue/chan counts the serialized FString size of every frame sent and
received, by opcode: 4 bytes (the length) plus Num times 1 (ANSI) or 2 (any unit at or above 0x80). Each netbudget
window then logs a line per side, `netbudget: host|client t=... chan bytes by op this window: sent ... received ...`,
with the Fx op's bytes per second first. The client gets its own window line, because the netbudget step ran on the
host only. The netbudget window cap per map goes from 60 (5 minutes) to 240, so the nuke fights late in the run are
inside it.

**Reliable buffer (static check, done).** ClientMessage is a reliable RPC. The FOutBunch constructor (exe+0x3B5FC80)
checks `Channel->NumOutRec` at UChannel+0x4C against `0x1FF + bClose`: at 511 (512 for a close bunch) it marks the
bunch overflowed. ProcessRemoteFunctionForChannelPrivate (exe+0x3E79060) then, for a reliable function
(FUNC_NetReliable 0x80, exe+0x3E79246), logs "Outgoing reliable buffer overflow" and closes the connection with
reason 0x41 (ClientReliableBufferOverflow, exe+0x3E79326). So RELIABLE_BUFFER is 512 in this build, not the 256 card
128 assumed, and the limit is 511 outstanding reliable bunches on one channel. ClientMessage goes on the player
controller's actor channel. Run 4 logs, in each host netbudget window, the peak NumOutRec read after each RPC on the
channel the RPC used, with the channel's actor class, and the map peak in the totals.

**Host filtered line.** The host event line (`c136_log_event`) gains `pawn <instance>` when the owner or instigator is
a player pawn, so the events filtered as a client's own are counted directly in the gap split, not by elimination.

#### Run 4 scenario fixes and logging (coded, DLL 94bcba771804bce1)

- Client in the dropship (fault 1): the client's pawn starts held at its seat (MovementMode 5, collision off, no
  ServerMoves). hb64's first correction came 12 s after the host's SkipFlight, and it snapped the client to the warped
  server copy. `tools/scenarios/c136-step7.txt` now waits `client wait near 229512 38956 9100 600 timeout=120` after the
  first warp, before step 1.
- Grant (fault 2): `kC21GrantMax` is 12. `c21_grant_to` equips the granted weapon only when the pawn's hands are empty
  (`GetEquippedWeapon` null before the grant), never on a server copy. The grant line logs `held before`. Each nuke
  step presses Two before it fires.
- Flares (fault 4): the line 34 wait is for the first flare of the arriving wave dropship, up to 120 s (the rejoin
  wait is up to 60 s).
- Host filtered line: `c136_log_event` appends `pawn <instance>` when the owner, the owner's owner (a weapon) or the
  instigator is a BP_PlayerCharacterMI. `tools/fx_gap.py HOST CLIENT [--p1 --d0 --p2 --q]` counts the unmatched
  events per pawn instance (only the client's pawn on the host is a filter; any other pawn is a gap).
- netbudget: the window cap is 240 per map. Each host window adds `reliable bunches outstanding: window peak N (class),
  map peak N (class), limit 511` and `netbudget: host t=... chan bytes by op this window: sent Fx B (B/s), <10 largest
  others>; received ...`. The client logs the same bytes line every 5 s. The host logs `map totals: reliable bunches
  outstanding peak` at the next map's arm.
- `tools/fx_pair.py --c136` reads the client line's `owner <class>` field.

#### Run 4 predictions (written before the run)

All the earlier run 4 items stay:

1. Client own burst: the client fires the Morita, and its own Casing and fire sound are not sent back to it (filtered
   rows on the host name the client pawn).
2. Nukes 2 to 4 fire on a WarriorBug, an ArcherBug and a JumperBug. Each death effect pairs by id between host and
   client.
3. Flares: the first Dropship_Flare_Fire comes inside the wait, which now waits for the dropship's arrival.
4. Gap split with `tools/fx_gap.py`: 0 unexplained.
5. Same-time screenshots on both sides at the nuke and flare steps.
6. Client `bad` rows 0, with `bad (too long)` 0 on the host.
7. The Flakbug ChargeUp (MS_ATK_FlakBug_Fire_ChargeUp) and Guttural (EMIT_FlakBug_Fire_MovementGuttural) sounds on
   BP_SatchelBuilding_Flakbug_C's `Box Component` play on the client, paired by id.

New with the binary format:

8. Fx bytes per second: the peak window and the mean over the windows with the client connected, from the byte
   counter. hb64's estimate was about 41000 to 55000 B/s of text in its saturated windows; at 4 to 5 times less, the
   expected peak is below 15000 B/s and the mean below 6000 B/s. The average E row is about 30 bytes (total Fx bytes sent minus A, N and frame overhead, over
   the E rows sent).
9. During the nuke fights (the WarriorBug, ArcherBug and JumperBug steps), no netbudget window is above 50% not net
   ready.
10. `warp client` moves the client (the fault 3 retest): the client's own position line after the warp is within 200
    units of the warp point.
11. The outstanding reliable bunch peak stays below 511 in every window (expected below 64).

Card 140 note: hb61 had NPC send gaps with no card 136 rows, so card 140 keeps its own cause. Run 4's byte counter
gives card 140 a baseline of the channel's bytes per op beside the NPC send gaps.

#### Run 4 results (hb65, DLL 94bcba771804bce1, 2026-11-08)

The scenario passed in 301 s, with host and client rc 0 and no crash dumps. Logs: `logs/hb65-*`; id pairing in
`logs/hb65.fxpair.txt`. Windows for `fx_gap.py`: `--p1 229001978 --d0 229155242 --p2 229167097 --q 229271683`.

| # | Prediction | Result |
|---|---|---|
| 1 | Client burst not sent back | Pass, but not provable per pawn. 21 Morita Fire_MS, Casing and MuzzleFlash3 events from the client's burst are unmatched and carry the pawn field. The field logs `BP_PlayerCharacterMI_C`, the object name, which in this build is the class name for both pawns (host events with the field: 567, all one value). The field must log the pawn's address or net id instead. |
| 2 | Nukes 2 to 4 on Warrior, Archer and Jumper | **Fail: no nuke fired.** All four `FireProjectile(BP_WeaponFireAbility_NukeLauncher_C` waits timed out; the host fired the Morita 120 times. Cause: the run 4 grant rule equips only when the hands are empty, and it applied to the first grant as well, so the launcher went in at index 2 with the Morita still held (`held before BP_Weapon_MoritaMk1_C, equipped BP_Weapon_MoritaMk1_C`). `host key Two` selected no weapon (in the other scenarios, Two is the Morita slot). Not an effects-code failure. |
| 3 | Flare inside the wait | Pass: the first Dropship_Flare_Fire came at +90.6 s, inside the 120 s wait. 110 host, 106 client played; 4 unseen. |
| 4 | Gap split, 0 unexplained | Pass: line gap 1865, sum 1868. Before the first Pong 882, disconnect 217, after quit 64, gore decals 585, pawn field 120, unexplained 0. There are 3 ids with 2 rows (2 WA_Flares NiagaraActivate and 1 DamageAndRadius), all made before the drop. |
| 5 | Same-time shots | Taken at the Morita, flare, sky and barrel steps; the nuke steps had nothing to show. In both flare shots a WarriorBug fills the view, so neither shows a flare. |
| 6 | Bad rows 0 | Pass: 0 client `bad` rows, 0 `bad (too long)`. |
| 7 | Flakbug ChargeUp and Guttural | Pass: 55 host / 52 played each; 3 unseen. |
| 8 | Fx B/s | Pass: host Fx sent peak 13435 B/s, mean 3733 B/s over 53 windows (expected below 15000 and 6000). The average E row is not available: map 2's fx net totals line prints only at the next map's arm, and the run quit first. |
| 9 | Nuke fights below 50% not net ready | Not tested (no nukes). The run's maximum was 51.6% at t=229169637, the rejoin window. |
| 10 | `warp client` moves the client | Pass: the client's landing sound came at (229512, 38956, 8616), 0 uu horizontal from the warp point (z is the ground). The wait-near passed at 387 uu in 3D. |
| 11 | Reliable bunch peak below 511 | Pass: peak 166 (above the expected 64, below the limit). |

Host WARN 7 to 42: card 132's `host fx grenade explosion (host's own) ... not shown` went from 5 to 39, and 2 new `OnHit of
ADProjectile, whose GrenadeExplode already went: no second frame`. Corrected below (hb65 review item 1): the "not shown"
count is a stale card 130 check, not a manager defect.

Open for run 5 (needs the user): the grant rule (equip on the first grant; on a re-grant, equip only when the hands are
empty), and the pawn field logging an address or net id.

#### hb65 review (static, from the hb65 logs; senior items 1 to 6)

**Item 1: grenade explosion "not shown", 5 to 39.** The check is card 130's `fx_watch` (`fx_watch.cpp`). It watches
the host's own Niagara component, the one `detour_c74_st_niagara` (`grenades.cpp`) hands it, at +100 ms and +1000 ms.
It is host-local: the client's manager play does not reach it. Every one of the 39 (37 late, 2 early, of 814 checks;
hb64: 5 of 917) reads `complete 1, requested state 3, active 0, visible 0`, which is a burst that ended and whose pooled
component was hidden. By system: `NS_FX_PumpShotgun_Hit_Concrete` 31 of 69 late checks (the Morita underslung rounds
this run fired), `Gore_BloodHit_Green01` 2 of 386, `Archer_Needle_Impact` 4 of 17. The rise is the new shotgun-hit
mix, not a regression. Pairing by id: all 1081 host GrenadeExplode ops matched a card 136 event (a spawn-point event,
the parent cue within 5 ms, or the parent through its `host fx skip ... in <parent id>` line). 1079 played on the
client. The 2 others were inner skips: 10061 (`ExecuteGameplayCue_NonReplicated` MoritaRound.AI) played on the
client, and 19938 (`ExecuteGameplayCue` NeedleLarge, owner the client's pawn) was filtered as that client's own cue.
No manager defect. Fix: the late check passes on `complete 1` (`fx_watch.cpp`). The early check is unchanged.

**Item 2: the 21 unmatched client-burst Morita events.** Here, "unmatched" means a host event with no client row.
Host events from `BP_Weapon_MoritaMk1_C`: 258. Of these, 195 have a client row and 63 do not. The 63 are 21
`MoritaMK1Fire_MS`, 21 `MuzzleFlash3` and 21 `Casing`, all at t 229010818 to 229012868, inside the client's burst.

| Case | Count |
|---|---|
| Sent to the shooter | 0 |
| Made on the host (server copy), no client row, filtered as the shooter's own | 63 |
| Played on the client with no host event | 4 `Underslung_Shotgun_Fire`, 4 `Shotgun_Pump`, 4 `MoritaShotgun_Casing1`, about 33 `PumpShotgun_Hit_Concrete` |

The manager behaved correctly. Separate finding, a full-world-sync gap outside the manager: the client's burst fired
`BP_WeaponFireAbility_MoritaMk1Underslung_C` on the client (36 FireProjectile lines), while its server copy on the host
fired `BP_WeaponFireAbility_MoritaMk1_C` 21 times. The fire mode does not follow the client to its server copy, so
the host and third parties see the main-fire effects while the shooter sees the shotgun. The host's own burst also
fired the underslung mode. Card 136 does not fix this; it needs its own card.

**Item 6: the rejoin window, 51.6% not net ready (t=229169637, 309 of 599 frames).** Engine actor replication in that
window was 167048 bytes over 19 classes; the reliable bunch peak was 166 (`BP_PlayerController_C`). Mod channel
bytes in the window: Decal 107616 (card 132's decal join replay, about 74% of the mod's part), Fx 17946 (the card 136
join replay, 3589 B/s), Pose 13784, Objective 1868, CollisionOp 1794, WorldOp 1784. In the next window, Digest sent
48400. The window is the engine's initial replication plus the join replays, and card 132's decal replay dominates
the mod's part. The card 136 Fx replay is small. Not a card 136 issue.

**Item 3, pawn field.** It now reads `pawn <side>:<address> guid <NetGUID>` (`fx_c136.cpp`, `c41_guid`; the GUID is
0 while the pawn has none). `fx_gap.py` groups by the `<side>:<address>` token.

**Item 4, grant.** The first grant per pawn side equips. A regrant equips when the hands are empty, or when the
dropped launcher was the weapon held at the last grant step (1 s apart, `weapons.cpp`). The second condition covers
the game putting the Morita in the hands as the empty launcher drops: under a strict "hands empty" rule, nukes 2 to 4
would then not be held. A regrant while the pawn holds the Morita it chose (the hb64 double nuke) still does not
equip. A server copy never equips (hb53).

**Item 5, totals.** `c136_report(when)` prints the host map totals, the client block totals and the net totals line
(with the average E row), then resets. It runs at map end (`ubw_load_map_enter`, before LoadMap), at the quit command
(`testrun.cpp`, before RequestEngineExit), and at the map arm (what came between the map end and the new map's arm,
usually zeros). Each line carries `(map end)`, `(quit)` or `(map arm)`.

**Scenario.** No weapon key selects the launcher: the grant puts it at inventory index 2, and in hb65 `key Two` gave
`ConstructWeaponSwitchRequest(1, 0, <pawn>)`, which selected no weapon. The nuke steps now run first, while the host
still holds the launcher from its first grant, and they no longer press Two. The Morita step follows them; its
`key One` takes the Morita from the regranted launcher. For the flare shot, the three wave WarriorBugs nearest the host
are killed first, and the view aims 800 uu above the flare point.

#### Run 5 predictions (hb66, DLL d315ddd41c815c03, written before the run)

1. Nukes: 4 `card020: host host-own FireProjectile(BP_WeaponFireAbility_NukeLauncher_C` lines, one per nuke step. The
   host-own grant lines are 1 `first grant` (equipped `BP_Weapon_NukeLauncher_C`), then a regrant after each shot
   with `(the dropped one was held)`, equipped `BP_Weapon_NukeLauncher_C`. Grants stay below the cap of 12. No nuke
   FireProjectile happens during the Morita bursts.
2. P9: no window above 50% not net ready from the first nuke mark to `nuke-jumper-after`. The rejoin window may
   still exceed 50%, for the item 6 cause.
3. Grenade explosions: every host GrenadeExplode pairs by id with a client card 136 row, played or filtered as the
   client's own, with 0 unexplained. The late card 130 `not shown` count is 0. The early count stays small (2 in hb65).
4. The average E row prints in `host fx net map totals (map end)` or `(quit)` for FED_C2M1, between 10 and 40 bytes
   per row.
5. Flare shots: `flares` and `rejoin-flares` show the flares, with no bug in front on either side.
6. The P1 split counts per pawn, by `<side>:<address>`. The client pawn's events on the host are all filtered. Any
   other pawn's count is a gap.

#### Run 5 results (hb66, DLL d315ddd41c815c03)

The run ended by the scenario after 242 s. Host rc 0, client rc 0, no crash dumps. Logs: `logs/hb66-host.log` and
`logs/hb66-client.log`.

| # | Prediction | Result |
|---|---|---|
| 1 | 4 nukes | **Fail: 1 of 4 fired** (230051674). The regrant came 4 ms after the game's own switch to the Morita had started its swap-in: `MoritaMK1_SwapIn` at 230051982, then `regrant (the dropped one was held) ... equipped BP_Weapon_NukeLauncher_C` at 230051986. GA_WeaponSwitchAbility never ended. The host pawn still owned `WeaponSystem.Blocked` and `WeaponSystem.SwitchWeapon` at 230066253, 14 s later. After that the host never fired again: there is only 1 `host host-own FireProjectile` line in the whole run, so the Morita burst at line 71 fired nothing too. `both key One` did not switch either: the host HUD still shows NUKE LAUNCHER in the flare shots. This is grant code (card 021), not effects code. Fix, built but not run: DLL 022a0f90113de3d3. A regrant now waits until the weapon has been missing for half a grant step, which means the next 1 s step. The swap-in ends 200 ms after the drop. |
| 2 | P9 below 50% during the nuke fights | Pass only for the nuke 1 window: the maximum is 2.8% from 230050156 to 230120000. Nukes 2 to 4 did not fire, so their fights were not tested. The rejoin windows are 56.8% (230152146) and 74.5% (230157147). That is card 142. |
| 3 | Explosion pairing, 0 unexplained; late not shown 0 | Pairing by id: 28079 host events and 23409 client rows. Every client row has a host id (0 orphans). The gap split: before the first Pong 2183, disconnect 1755, gore decals 610, pawn field 118 (all server-copy). 6 are unexplained, all made 1 to 10 ms before the client's disconnect (230137677 to 230137686), so they were in flight at the drop. Late `not shown` 0. Early `not shown` 5, all host grenade explosions at +100 to +108 ms, complete 1 (they ended inside 100 ms): 230111003, 230111379, 230111530, 230111578, 230111822. **New defect, see 19938 below: projectile impacts play twice on the client.** |
| 4 | Average E row 10 to 40 bytes | Pass: `host fx net map totals (quit)` reports 20706 event rows, 641202 bytes, average 31.0. |
| 5 | Flare shots | `flares`: both sides show the flare and its trail clearly. `rejoin-flares`: the host shows the trail, with a Warrior partly in front. The client's view is filled by bug gore, so no flare shows. |
| 6 | P1 per pawn | Pass: 111 events for `server-copy:0000000086D9C740` and 7 for `server-copy:00000000692AC740` (the pawn after the rejoin). No host-own pawn events are unmatched. |

**19938 (hb65, senior question).** The host line is
`card136: host fx 19938 t=229115285 ExecuteGameplayCue GameplayCue.Impacts.NeedleLarge ... owner BP_PlayerCharacterMI_C (BP_PlayerCharacterMI_C) instigator BP_AI_ArcherBug_C (BP_AI_ArcherBug_C) ... pawn BP_PlayerCharacterMI_C`.
Its instigator is the archer. In the code, the send filter checks the owner as well as the instigator: `filter_hits` is
true when the owner, the owner's owner or the attach owner is the PC's pawn. The instigator counts only for
`ExecuteGameplayCue_NonReplicated`. The cue ran on the client pawn's ASC, so the owner rule dropped it, on the premise
that the client's ASC plays that replicated cue itself.

The client log does not prove that premise. No client line shows a NeedleLarge cue of its own near 229115285. Cues
are not in the card 132 fx audit, and the block totals count kept calls only as a total. The impact that this cue
makes on the host (`NS_FX_Archer_Needle_Impact` at (229463, 38939, 8771) plus `MS_EMIT_HazardZone_Acid_Splash`) does
reach the client, but by another path: card 132's projectile copy,
`card132: client t=229115297 GrenadeExplode #822 2147484470 229471.2 38952.1 8770.4 .../NS_FX_Archer_Needle_Impact ...`,
which spawns it at (229463, 38940, 8771). The cue's decal (`MI_FX_Decal_Needle`, an inner skip of 19938) has no client
evidence.

Both NeedleLarge cues that were sent, 19941 and 19944 (owner: the host pawn), play the impact twice on the client.
`client fx 19941 ... played` spawns at 229115318 at (229340, 38908, 8672), and `GrenadeExplode #823` spawns the same
asset at the same place at 229115309. The same holds for 19944 and #824 (229115335 and 229115327). Over the whole run,
client impacts at the same place within 40 ms: 9 of 26 in hb65 and 8 of 62 in hb66.

So card 132's projectile explode copy and card 136's cue rows both draw a projectile's impact. The owner filter only
hides that overlap for cues on the client's own ASC. This is an effects-code defect, so card 136 stops here for a
re-scope.

#### Run 6 static work (2026-11-08, from the hb66 logs and the code; written before the build)

**Correction to run 5 (19938 and the doubles).** The "plays twice" claim above is wrong. The card 132 fx audit line is
written before the card 136 block decides, so an audit line is not a spawn. The client spawn at 229115309 is the
engine's own copy of the replicated cue: `USTImpactGameplayCueNotify::ExecuteImpact` (0x0511E7A0) runs
`CreateImpactPFX`, which spawns through `STSpawnNiagraSystemAtLocation` with the cue notify's `GetWorld` as context
(`UGameplayCueNotify_Static::GetWorld` 0x049CDCC0). That spawn has no owner, and the client blocks ownerless spawns, so
it was not drawn. The 40 ms "same place" pairs in hb65 and hb66 are one drawn spawn plus one blocked audit line. The
real defect is a loss, not a double: an impact cue that an NPC makes on the client pawn (19938) is dropped by the host's
owner filter, and its engine copy is blocked on the client, so it plays on neither path.

**Item 1: one source for impact effects.** The card 132 client explode path (`c74_receive_explode`, grenades.cpp) spawns
no effect today. In hb66 the client audit for the copies shows only their ends: 585 `ReleaseToPool` (rounds) and 35
`LifeSpanExpired` (grenades), and no spawns. What the path does on the client, all kept: it takes the copy out of the
relay table, moves nothing, logs the distance between the copy and the host's explode point, and ends the copy with
`ReleaseToPool` (pooled round) or `LifeSpanExpired` (grenade). The copy deals no damage (card 132). Effects the client
path touches today: the copy's trail `NiagaraActivate` at spawn and its `Deactivate` at the end. Both are captured and
blocked (ownerless copy, owner not the client), and the host sends its own trail rows for the host projectile; see item
4 for why those rows miss. The impact itself (Niagara, sound, decal) comes only from the card 136 rows. Nothing is lost
and nothing is doubled on this path; fix C below makes the trail local to each side's projectile.

**Item 2: send filter by instigator.** `filter_hits` (fx_c136_net.cpp) changes to: when the event has an instigator,
the event is filtered for a client only when the instigator is that client's pawn or PC. The owner rule (owner, owner's
owner, attach owner) applies only when there is no instigator. The `cue_local` special case goes away, because the rule
now covers every capture kind. The client's own predicted effects (instigator its pawn) stay filtered on the host and
kept on the client (`c136_client_own` uses the same function). Effects on the client pawn made by someone else (19938:
owner the client pawn, instigator the archer) are sent.

**Item 3: engine copies.** A replicated cue reaches the client by
`UAbilitySystemComponent::NetMulticast_InvokeGameplayCueExecuted_WithParams_Implementation` (0x04965F70; the
`_Executed` 0x04965DF0 and `_FromSpec` 0x04965D90 variants likewise), the active-cue fast array and the minimal
replication tags (`OnRep`/`PostReplicatedAdd`), and these all call `InvokeGameplayCueEvent` (0x049643C0). None of them
pass `ExecuteGameplayCue` (0x0495A340, a capture point), so card 132 Trace 1's 72 to 78 % arrival is this path. Every
cue that runs on a side, replicated or local, ends in `UGameplayCueManager::HandleGameplayCue` (0x049CED20,
`this, AActor* target, FGameplayTag, EGameplayCueEvent::Type, const FGameplayCueParameters&,
EGameplayCueExecutionOptions`), already in tools/symbols.txt.

- Hook: a detour on `UGameplayCueManager::HandleGameplayCue`. On the client it opens a card 136 scope (new point
  `CueHandle`, never sent) with the target as owner and the cue parameters as instigator source, then always calls the
  original. Inner capture points inherit the decision, so the scope blocks or keeps every effect the cue draws.
- Test that tells the two apart: depth first, then the instigator. A cue that runs inside a capture point
  (`ExecuteGameplayCue` 0x0495A340, `ExecuteGameplayCue_NonReplicated`, `ExecuteGameplayCueContext`) reaches
  `HandleGameplayCue` at depth 1 or more and inherits that point's decision, which is unchanged. A cue that reaches it
  at depth 0 did not pass a capture point on this side: a replicated arrival, or a predicted cue flushed later from the
  pending list. The instigator rule of item 2 decides it: instigator the client's pawn or PC, kept (the client's own
  prediction); anything else, blocked, because the host sends it. The instigator is
  `FGameplayCueParameters::GetInstigator` of the cue's parameters (`g_c136_cue_instigator`), with the target as the
  owner. Counted per map as `engine cue copies dropped` and `kept`, each with its tag on a UBWCOOP_C136_LOG line.
- Host: the same detour sets the cue's instigator for the scope, so the events the cue draws carry the cue's
  instigator even when the inner spawn has a world context and no owner (the impact cue above). The host never blocks.
- Other engine paths that draw a captured effect: a replicated `bIsActive` on a Niagara component (`OnRep_IsActive`
  calls `Activate`/`Deactivate`, both capture points), and anim notifies that run on both sides (montage replication;
  already scoped by `detour_c136_anim_notify`). Both go through the capture points, where the same instigator rule
  decides, so neither needs its own hook. No replicated Niagara component spawn path was found that bypasses the
  capture points.

**Item 4: target, key and asset misses (hb66, before the drop / after the rejoin).**

| Miss | Before | After | Cause | Action |
|---|---|---|---|---|
| target: projectile trail | 603 | 350 | `NiagaraActivate`/`Deactivate` on an `AADProjectile` (world key). The client has its own relay copy; the host row spawns a static trail at the world point. | Manager defect. Fix C: projectile trail component ops are local on both sides. |
| target: gore part trail | 799 | 31 | Same, on `ASTGorePart*`; each side makes its own parts. | Manager defect. Fix C. |
| target: spawn row's copy gone | 214 | 210 | `NS_Compute_DamageAndRadius` on a WarriorBug (guid key, `of` set): the client copy completed or was pooled while the host re-activates its component. | Manager defect. Fix D: spawn attached to the key actor's root at the host's point. |
| target: pooled component on `STWorldSettings` | 202 | 253 | `FlakbugProjectile_AllInOne` Activate with no spawn row; the pooled component's runtime name differs per machine. Spawned at the right place at world. | Listed, no fix. |
| target: key actor not on the client | 4 | 21 | Guid not found (not relevant to the client or destroyed). Played at world or on the PC. | Listed. |
| target: `BP_Projectile_Plasmabug_C`, world key | 16 | 18 | Not an `AADProjectile`, not relayed. | Listed. |
| target: `BP_HazardousZone_Acid_C`, world key | 16 | 6 | Not relayed. | Listed. |
| target: failed world spawn after a miss | 71 | 23 | Counted with the miss; the world spawn returned none. | Listed. |
| key: guid not found | 54 | 126 | Morita muzzle flash, `MS_WPN_AI_Morita`, `WarriorBug_Pain`, DamageAndRadius attached spawns. Played at world. | Listed. |
| key: pooled `STWorldSettings` sound | 15 | 18 | `PlasmaBug_Projectile_Explode` `SpawnSoundAttached`. | Listed. |
| asset: null decal material | 45 | 11 | `SpawnDecalAttached` with no material, owner Landscape. The engine draws nothing. | Manager defect. Fix F: not sent. |
| asset: other | 2 | 0 | Two other rows. | Listed. |

About 4000 client `no component` stop rows (2814 `Bugs_WalkGround` `DeactivateImmediate` on `STWorldSettings`, and
others) are stops whose start the host never sent: there is no journal row. Listed, not fixed (fix E was dropped
after the trace): in hb66, 92 such stops on keyed `BP_ProceduralDropshipTroops_C` actors played on the client, so a
stop with no journal row is not always dead. The `STWorldSettings` stops with no `of` are harmless no-ops on the client,
and the projectile and gore part stops become trail-local under fix C.

**Item 5: card 130 early check.** The 5 early not-shown were complete 1 at +100 ms, the same staleness as the late
check. The early check now passes on complete 1 too; counts unchanged.

**Fixes in the run 6 build (with the card 021 grant fix of DLL 022a0f90113de3d3):**

- A. Instigator-first send filter (item 2).
- B. `HandleGameplayCue` detour, client drop of engine copies and host instigator scope (item 3). It acts on
  `EGameplayCueEvent::Executed` (2) only. Persistent `OnActive`, `WhileActive` and `Removed` cues pass through and keep
  the capture-point rules.
- C. Projectile and gore part trail component ops local on both sides (new symbols `AADProjectile::StaticClass`,
  `ASTGorePartBase::StaticClass`), counted on the host as `local trail`.
- D. Dead copy with a live key actor: spawn attached at the host's point.
- E. Dropped, listed only (see the item 4 note on stops with no journal row).
- F. An event with a null asset and no tag is not sent; counted.
- G. Early fx check passes on complete 1.
- H. Client log line for kept depth-1 calls (point, asset, place, owner; capped), so a double can be measured.

Not fixed, reported: the client runs its own AI rounds (528 `AIMoritaRound` launches from
`STAttackActionRangedSprayAttack`, caller exe+0x52502AC). That is a visible double in card 132's area.

Build: DLL 95f91961e777c34d (fixes A to D and F to H, with the grant fix of DLL 022a0f90113de3d3). Scenario change:
after the rejoin kills the client moves 800 uu toward the flares for `rejoin-flares` (hb66: the two Warriors died at
the players' feet, at (229225, 39007) and (229407, 39345)).

#### Run 6 predictions (hb67, DLL 95f91961e777c34d, written before the run)

1. 4 nukes fired. P9 at most 50 % in every nuke fight. The rejoin windows are exempt (card 142).
2. 0 client impacts at the same place within 40 ms that come from one host event. Measured from the client
   `client fx own` lines and the `card136: client fx` rows: a played row and a kept own call with the same asset within
   40 ms and 100 uu count as a double unless the own call has instigator the client pawn.
3. Every host impact on the client pawn made by an NPC (owner the client pawn, instigator not a player) is played on
   the client and paired by id.
4. Engine cue copies dropped: the client `engine cue copies N dropped` count equals the number of manager-covered
   Executed cue rows (`ExecuteGameplayCue*` host events) that arrived on the client, within the rows in flight.
5. 0 unexplained in `tools/fx_gap.py`, apart from rows in flight at the drop.
6. Target misses: projectile and gore trails near 0 (they are local now); the `NS_Compute_DamageAndRadius` misses
   become `spawned on the key actor (component miss)`; the pooled `STWorldSettings` (about 200), guid missing (about
   20), plasmabug (about 17), acid zone and failed world spawn misses stay at hb66 levels with the same causes. Asset
   misses from null decals 0 (fix F); key misses as in hb66.
7. `rejoin-flares`: both sides show the flares; no gore fills the client's view.


#### Run 6 results (hb67, DLL 95f91961e777c34d)

No crash. Scenario passed (drive rc 0, 216 s). Host WARN 4, client WARN 15; none from card 136. Windows for the gap
split are the card 136 join replays, not the first Pong: the host sends no one-shot rows before
`card136: host fx join replay` (232220208, rejoin 232300195), and the journal replays the loops. The drop is at
232288384 and the quit at 232402962.

| # | Prediction | Result |
|---|---|---|
| 1 | 4 nukes, P9 at most 50 % in the nuke fights | Pass. 4 nukes at 232227883, 232237957, 232247885, 232257876. P9 maximum 6.5 % in the nuke fights, 8.2 % outside the rejoin. The rejoin windows (exempt, card 142) are 34.9 % and 62.5 %. A fifth `NukeLauncher` `FireProjectile` at 232265265 is the `morita-host-fire` press, which came before the weapon switch to slot One had finished. |
| 2 | 0 client doubles from one host event within 40 ms | **Fail, in the effects code.** 16 doubles within 40 ms and 100 uu, all gore part splats (`NS_FX_Gore_BloodGib_Green`/`_Red` and `Splats_MS`, owner `STGorePart*`). The class is wider than 16: each side simulates its own gore parts (card 132 D), and the client plays both. It plays 1391 host gore part splat rows at the host's gib positions and 408 of its own splats (`client fx own`). Fix, not built: a gore part owner is local for every capture point, not only trails. That extends fix C: the host does not send these, and the client keeps its own. The other kept client calls are its own predicted fire (36 `Impacts.Shotgun`, 7 landings) and the trails. |
| 3 | NPC impacts on the client pawn played, paired by id | Pass. 3 of 3: `NeedleLarge` 4657 and 5322, `MoritaRound.AI` 14370. All are `played`. |
| 4 | Engine cue copies dropped equal the covered cues that arrived | Partly. 141 dropped, 0 kept. All 141 match an arrived manager cue row by tag within 1.5 s, and 0 are unmatched, so no uncovered cue was dropped. The count is not 251, the number of replicated `ExecuteGameplayCue` rows that arrived: the engine delivered its copy for 141 of 251 (56 %). The prediction was worded too strongly. The engine copy does not arrive for every cue (card 132 Trace 1: 72 to 78 %). |
| 5 | 0 unexplained apart from rows in flight at the drop | Pass. 1 unexplained: id 11159 at 232288377, 7 ms before the drop. |
| 6 | Target misses as traced | Pass, mostly. Before the drop / after the rejoin: projectile and gore trails 0 / 0 (hb66 603 / 350 and 799 / 31). Dead spawn copy 14 / 6 (214 / 210); 17 rows now `spawned on the key actor (component miss)`. Pooled `STWorldSettings` 138 / 245, of which 352 are `NS_FX_FlakbugProjectile_AllInOne` (hb66 202 / 253). Guid missing 4 / 25. Plasmabug 12 / 18. Acid zone 4 / 0. Key misses: guid not found 57 / 107, pooled sound 11 / 18. Asset misses 1 / 0; 67 null decals were not sent (fix F). |
| 7 | `rejoin-flares` with no gore in the client's view | Pass. The client shows the flare and its trail with no gore. The host shows both flares. |

Stopped here as the senior asked, because prediction 2 failed in the effects code.

## Senior review of step 0 (2026-11-08)

The table is accepted with these changes, which go into the table before step 1 code:

1. **One event per effect (nesting).** The capture points nest. A cue at 0x495A340 runs its notify, which calls
   SpawnSystemAtLocation and PlaySoundAtLocation, and SpawnSystemAtLocation activates a new UNiagaraComponent. Only the
   outermost captured call becomes an event. A depth counter is set for the duration of a captured call, and inner
   captures inside it are skipped. Point 6 (Activate and Deactivate) applies only to components that no point 2 call
   created: placed, level-owned and blueprint-constructed ones. The step 7 prediction adds: 0 events with a captured
   parent (each logged inner skip names its parent).
2. **World-geometry hits.** The host runs `ExecuteGameplayCue_NonReplicated` (0x049C5E80) for hits with no ASC.
   Today these reach the client only through the relay that step 5 retires (255 relayed MoritaHit_Ground in hb61).
   Add 0x049C5E80 as a host capture point, or show which point already catches them.
3. **The receiving client's own actions.** The host runs a server copy of each client's weapon and pawn (card 024), so
   it also captures effects the client already plays locally: world-geometry hits of its own shots, its muzzle, its
   own pawn's sounds. Add one column to the table: for an effect caused by the client's own pawn, does the client
   play it locally (yes or no)? The send filter follows from that column. Effects the client already plays are not
   sent to that client. Effects that only the host can make, such as a cue on a bug's ASC (non-authority on the
   client, Trace 1), are sent. The filter goes by instigator, and it is decided per receiving client.
4. **Owner bReplicates for corpses, barrels, spawners, dropships and the acid zone.** The key in step 2 depends on it.
   Check it before step 1 (static from the class defaults, or one log line in the step 7 run, with the key scheme
   written for both outcomes).
5. **Gore floor and hit splats.** These are captured, not left local. They are visible and they persist (full world
   sync). The host's splats are sent, and the client blocks splats from its own ASTGorePart. Gib physics positions
   themselves are world state and go to card 137.
6. **Acid zone (card 137).** Card 137 checks that the host's zone damages the client's pawn on the host and that the
   client sees its effects through this card.
7. Cascade addresses: put them in the table now, not in step 1.

Senior, on the updated table (2026-11-08): accepted, with three notes for step 1.
- The client blocks at the effect calls (the capture points), by owner and instigator. It never blocks an anim notify
  or a cue as a whole: an NPC notify can also spawn a projectile or deal damage.
- Row K on the client also covers world hits by NPC rounds that the client spawns itself. Their instigator is an NPC,
  so they are blocked, and the host's copy plays instead. The step 7 count shows it.
- The key is chosen by the host from its own flag, and that choice goes in the event. The client looks up whichever key
  it gets, and logs a miss with the world-transform fallback. "The client's flag decides" is removed.

## Defect in accepted code (found in card 140 run 7, c140g)

The client fx asset table kept a raw UObject* per asset row for the whole map. An asset that the client loaded itself
has no root, so GC could free it, and the next row then passed freed memory to SpawnSystemAtLocation. That crashed the
client at fx 10528, NS_FX_Gore_BloodDeath_Green01. The fix is in build 01840e14be0db1d3:
- each looked-up asset is registered with the game instance at its first lookup and unregistered for the whole table at
  map unload;
- a weak check stays as a guard, counted as "freed while held", which must be 0;
- the extras table is keyed by name instead of by pointer.

The play build 4cb036fa33c090bc predates card 136 and is not affected.
