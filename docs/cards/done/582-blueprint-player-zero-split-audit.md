# Card 582: Blueprint player-0 audit (single-player assumptions in Blueprint callers)

User request 2026-11-21: "Go through everything that normally the single player would be handling and make sure the
split is properly handled". SYNC_AUDIT L1 (card 321) covered native callers only. This card sweeps every Blueprint
call to a player helper.

## Method (senior, static, 2026-11-21)

- Packages: 87 packages that call a player helper, resolved disassembly in `build/kis_res/*.txt`.
- Digest: `build/digest_sp.py` lists each call site, its function, and the first 3 uses of the result
  (`build/sp_digest.txt`, deduped `build/sp_short.txt`, context `build/sp_ctx.txt`).
- Call counts: GetSTPlayerCharacter 160, GetPlayerController 55, GetPlayerCameraManager 34,
  WidgetBlueprintLibrary::Create 26, GetPlayerCharacter 24, GetSTPlayerController 17, StartCameraShake 14,
  PlaySound2D 7, GetSTHUD 4, GetHUD 3, SpawnSound2D 1, GetPlayerPawn 1.
- Rules used for verdicts:
  - Host c99 remap: GetPlayerController(ctx, 0) on the host follows the ctx Instigator, Owner and outer chain, or the
    acting scope (cards 102, 103, 302).
  - c160: the host skips a server copy's 2D sounds, MPC writes and no-owner widgets.
  - Level-script per-player writes are mirrored (cards 267, 310, 463, 553).
  - Bug-side Blueprints are skipped (trooper-first).

## Verdicts

### Covered: player-owned context, host remap gives the right player

GA_Slide, GA_Sprint, GA_HumanMelee, GA_WeaponMeleeAbility, GA_HumanTaunt, GA_MechTaunt, GA_TauntAbility,
BP_WeaponReloadAbility, BP_WeaponFireAbility_AutoCannon, BP_WeaponFireAbility_PumpShotgun,
BP_Weapon_EquipAbility_MechRazorback, GA_TacSupp_* (GuidedMissile, WhitePhosphorus, Resupply, DoorGunner),
BP_GuidedMissile and BP_WhitePhosphorus_Projectile (Instigator), ABP_Arms_Skeleton (the AnimNotify_GrenadeExplode
self-damage resolves to the copy on the host), ABP_MoritaMk2, ABP_NukeLauncher, ABP_Veteran,
BP_MountableTurret_Gunner (card 561, b408), BP_BaseMech (host rows "player 0 remapped (context) ... context BP_BaseMech_C, copy BP_BaseMech_C" at callers exe+0x523A1AC, 0x5248694, 0x5251D56: the client mech remaps to itself).

Also covered by an earlier card: BP_Door_Base (453), BP_AmmoCrate, BP_ArmourCrate and BP_GrenadeCrate RefillItem
(interaction scope, 054 and 123), BP_ResupplyRack (104, 105), BP_ObjectiveMarkerActor widget (102, 130),
BP_RandomEmitterVolume (418), the GetWorldYaw minimap functions (416).

### Fine: local presentation that runs on each side for its own player

BP_AudioVolume, BP_AudioSpline, BP_FX_FakeFogSphere_02, BP_FrontendTrooper_Base, BP_HUD_Manager, BP_HUD_Manager_MI,
BP_HUD_Manager_3DFrontend, BP_PhotoModeSpectatorPawn, BP_PlayerController GetHUD, BP_CamFadeController,
BP_SatchelBuilding_Flakbug (vertex LoD by camera distance), BP_HordeZone HordeAudio (follows the local player on each
side; needs the zone to run on the client, see open item 4).

### Gaps: host world actors that read player 0 and have no player context

The ctx of each of these is a placed world actor with no Instigator or Owner, so on the host the remap returns the
host's pawn.

| # | Blueprint | What reads player 0 | Effect in co-op | Fix shape |
|---|---|---|---|---|
| G1 | BP_MortarZone | BeginPlay `PlayerInside = TriggerArea.IsOverlappingActor(GetPlayerCharacter(0))`; GetMortarLocation aims at player 0's location | Mortars aim only at the host. A client alone in the zone is never targeted, and the host leaving can clear PlayerInside while the client is still inside | Target pick: a player inside TriggerArea (nearest to the zone, else any). PlayerInside = any player overlapping |
| G2 | BP_EM_Dropship, BP_EM_1FactionSetWaves | ShowMarker: `TargetComponent.ModifyTargetThreat(GetSTPlayerController.K2_GetPawn(), 2000)` | Marked enemies get +2000 threat on the host only, so they chase the host | Fan out: the same call for every player pawn |
| G3 | BP_KillableBuilding_Base | Damage event: if `Causer == GetPlayerCharacter(0)`, SpawnSoundAttachedToPlayerWithAIAwareness (MS_EMIT_Human_Building_Generic_Damag...) | A client's hits on a building give the client no hit sound (all-effects-sent gap). The plain Damaged SFX at the FX location still plays | Acting scope: compare with the causer's player; forward the attached sound to that client (ChanOp sound path) |
| G4 | BP_PickupTracker | Binds OnWeaponEquipped and OnUnequip Nuke on player 0's inventory; DeleteFurthestTempPickup by distance to player 0 | Nuke pickup respawn and cleanup follow only the host's nuke equip | Bind on every player's inventory (fan out at bind time and on client join); distance = the nearest player |
| G5 | BP_KillableBuilding_ElectricPylon | CheckLoS runs only when `Target == GetSTPlayerCharacter` | A client target never passes the LoS check (pylon attack or fx host-only) | Compare Target with any player pawn |
| G6 | BP_SupportFlare GuidedMissileCheck | Box trace from player 0's location | A client's flare checks from the host's position | The flare's Instigator owns it: verify whether the c99 remap reaches GetSTPlayerCharacter here; if not, acting scope |
| G7 | BP_MountableTurret | Reads player 0's STWeaponInventory (overheat GE, overheat audio param); `GetPlayerPawn(0) == self` | A client on a turret may get the host's overheat values or audio (cards 243 and 263 touch it) | Use the seated pawn (the turret's controller) |
| G8 | BP_TacSupp_Pod SpawnDesiredFX | `ApplyDamageAtLocation(self, GetPlayerCharacter(0), ...)`, self-damaging template | A client's pod credits the host as instigator (kill credit, friendly-fire owner) unless the pod's Owner chain reaches the client copy | Verify the pod's Owner/Instigator at spawn; if null, set it from the caller |
| G9 | BP_AIGrenade_Frag_VivaGranata | `ApplyDamageAtLocation(GetSTPlayerCharacter, ...)`, 1000 damage, radius 1500 | The host is the instigator of an AI grenade. Harmless for the split unless credit or friendly-fire rules read it | Low; verdict after G8 |
| G10 | BP_EM_PrefabBase, BP_DestructibleSpawner_Base, BP_TentSpawner_HumanGameplay | BeginPlay or re-enable: `PlayerDetection.IsOverlappingActor(GetSTPlayerCharacter)`, then PlayerEnter or PlayerExit | Later overlaps use IsPlayer(OtherActor) and work for any player. Only the initial check reads player 0: a client already inside at enable is missed, and PlayerExit can fire while the client is inside | Initial check: any player overlapping |

### Candidates on the client: copies of the host's pawn read the client's player

ABP_Arms_Skeleton on a remote copy: BeginPlay binds the local PC's OnPossessedPawnChanged, Set Procedural Data reads
the local IsDead, and AnimNotify_GrenadeExplode would call ApplyDamageAtLocation on the client's own pawn if the
throw montage plays on the copy. The meshes are hidden (card 033) and remote pawns are sprites (card 028), so the
visible effect is likely nil. Open: does the throw montage play on the client copy's arms, and does the client's
ApplyDamageAtLocation reach a real health change (host-owned health, cards 022 and 023, says no)?

### Level scripts (FED_C1M1 to FED_C5M2 _Scripting)

Per-player writes, position tests and teleports are already classed: cards 267 and 310 (writes, mirrored), the card
301 class G and D sweep in `docs/reference/objective-coop-audit.md` (C4M2 @32745-33228 teleport is a debug console
event; C5M1 IsFriendFaction is fine; C4M2 C_176 enable gap is card 301's), and C5M2 ApplyDamageAtLocation uses
player 0 as instigator only (card 310). Not classed before this card: the level scripts' own presentation calls.
The level script runs on both machines from each machine's own triggers (card 280), so each call reaches the client
only when the client's copy of that entry runs.

| Map | Site | Call |
|---|---|---|
| C1M1 | @3958, @6300 | PlaySound2D |
| C1M1 | @11930 | PlaySound2DFName MS_EMIT_C4M1_Stadium_Siren |
| C2M1 | @21151, @22637 | StartCameraShake (USTCommonUtilityLibrary, own camera) |
| C3M1 | @15254-15350 | intro flight `Splosion`: StartLegacyCameraShake on GetPlayerCameraManager(0), then PlaySound2DFName MS_EMIT_MortarField_Explosion |
| C4M2 | @6579, @11831, @21611, @21709 | PlaySound2D |
| C4M2 | @36378 | StartCameraShake |
| C5M1 | @27511 | StartCameraShake |
| C5M2 | @17723, @18012, @19993, @20085 | StartCameraShake (scale 2.0, 1.0, 2.0, 1.5) |

Verdict owed per row: the client runs it within 100 ms of the host (fine), never (gap: the host relays it, as card
322 did for conversations), or twice (gap: cut the client's own). The card 551 shake rows cover only the landing
notify caller so far; no run since b378 reached these entries on C2M1, C4M2, C5M1 or C5M2.

## Fix plan (one class, not per row)

One condition table, keyed by (Blueprint class, function): the rule for "player 0" inside that function on the
host. Rules: `any-overlap` (G1, G10: true if any player overlaps the given component), `nearest` (G1 target,
G4 distance), `fan-out` (G2, G4 bind: run the call once per player pawn), `causer-player` (G3, G5: compare with any
player pawn and act for that player). The table rides the card 302 acting scope, so a new row is data. G6, G7, G8 and
G9 are verified first; they may already be covered by the Instigator or seat rules.

## Proof owed

- Static: a row per gap with its rule, and the open items below answered, before code.
- One run (FED map with a mortar zone and an EM marker wave): the client alone in BP_MortarZone gets mortar fire
  aimed at the client (row: mortar target = client copy); a marked wave's enemies get threat rows for both pawns;
  a client's hit on a killable building plays the attached hit sound on the client (forward row). Differ 0, no new
  WARN class.

## Maps (host logs, last 150 runs)

- BP_MortarZone: FED_C3M1, FED_C3M2.
- BP_PickupTracker: FED_C2M1, FED_C5M1.
- BP_EM_Dropship and BP_EM_1FactionSetWaves: every FED map.
- BP_HordeZone: FED_C2M1 to FED_C5M2.
- BP_KillableBuilding_ElectricPylon: not seen in the last 150 host logs.

## Step 1 (runtime diag, rides the next build)

The pod, flare and AI grenade are spawned natively (no Blueprint spawn site in the 87 packages), so their Owner and
Instigator are not visible statically. The c99 "remapped" rows are capped per caller and list only calls that
remapped. Add one host row per (context class, caller) for a player-0 read that did not remap while a client is
connected: `card582: player 0 not remapped: context <class>, caller exe+0x..., owner <class>, instigator <class>`,
capped at 64 distinct keys per map, under UBWCOOP_C99_LOG. One run on FED_C3M1 with the client throwing a pod and a
flare and hitting a killable building gives the G6, G7, G8 and G9 verdicts and confirms G1 to G5 from live rows.

Same build, level-script presentation: under UBWCOOP_C99_LOG, both sides log one row per StartCameraShake
(USTCommonUtilityLibrary and APlayerCameraManager, which the legacy shake reaches), PlaySound2D and PlaySound2DFName
whose context class ends in `_Scripting_C`: `card582: <side> t=<ms> level script <call> <asset> scale <x>`, and a
map-end tally per (call, asset). The verdict per row of the level-script table comes from the host and client
tallies and times; the rows ride the next soak or objective run of each map, 0 extra runs.

## Step 2 (code, senior, 2026-11-21, rides b410)

`player_c99.cpp`: a new host rule in `c99_pick`, after the copy walk, the acting scope and the enemy remap all give
nothing. When the context's class (or a super) is in `kC582Classes` (BP_MortarZone_C,
BP_KillableBuilding_ElectricPylon_C, BP_EM_PrefabBase_C, BP_DestructibleSpawner_Base_C,
BP_TentSpawner_HumanGameplay_C), player 0 is the player nearest the actor; slot 0 (the host) leaves the original
result. Entry `kC102Place` ("place") is new in `C102Entry`. Class verdicts are cached per class pointer (64 per map,
reset in `c99_arm_map`). Covers G1, G5 and G10. G2 and G4 are not in the table on purpose: one nearest player would
take the host's marker threat or nuke bind, so they need a fan-out (still open). G3 is still open.

Predictions (b410, any map with a G10 spawner or C3M1 mortar zone, UBWCOOP_C99_LOG=1):
- Host rows `card099: player 0 remapped (place): caller exe+0x..., context BP_<class>_C ...` appear only while the
  client is nearer the actor than the host. No such row in a host-alone stretch.
- C3M1: with the client alone in a mortar zone, mortars land near the client (shell impact rows near the client's
  location); the host leaving does not stop fire while the client is inside.
- Card 572 differ stays 0 (the rule depends on ctx and player positions only, and the memo is per frame).
- No new WARN class.

## Step 3 (code, senior, 2026-11-21, rides b410): G2 threat fan-out

New hooks (`tools/symbols.txt`, `offsets.generated.h` regenerated): `UADTargetComponent::execModifyTargetThreat`
(0x05020300, notes the calling Blueprint object from FFrame::Object) and `UADTargetComponent::ModifyTargetThreat`
(0x0519A190, calls the original, then on the host repeats a BP_EM_Dropship_C or BP_EM_1FactionSetWaves_C call made for
roster slot 0's pawn for every other player pawn, same delta). Code: `player_c99.cpp` (`detour_c582_threat`).

- Each (enemy component, client pawn) pair is mirrored once per map. On a host pawn change the Blueprint adds +2000
  to the new pawn and -2000 to the old one, which is no longer slot 0, so a second mirror would stack +2000 on the
  client. A client's new pawn is a new pair.
- A pair is noted only when the enemy already has a target entry for that pawn (`FindTargetData`), because the native
  adds only to an existing entry. Otherwise the next marker call tries again.
- Tally row at map end on the host: `card582: map: EM marker threat calls N for the host pawn, mirrored M times to
  client pawns, R repeat pairs skipped`. When the 256-pair table is full: `WARN card582: map: threat pair table full`.

Predictions (b410, any FED map with an EM marker wave, client connected): N > 0, M > 0 and M <= N, no WARN card582.
Card 572 differ stays 0.
Known limit: an enemy that perceives a client pawn only after the last marker call never gets that pawn's +2000.

## Step 4 (code, senior, 2026-11-21, rides b411): G4 pickup tracker fan-out

BP_PickupTracker binds its `Weapion Is Equipped` and `Unequip Nuke` handlers to the weapon inventory of
GetSTPlayerCharacter(self), which is player 0 only (`Enable Delegates`; `Disable Delegates` removes them). Pickups run
on the host for every taker (card 254), so a client's nuke equip or unequip on its server copy never reached the
tracker, and the nuke pickup was never respawned after the client used it. That is a possible softlock of the C2M1
and C5M1 nuke objectives.

Code (`player_c99.cpp`, `c582_tracker_*`; hook points: the BeginPlay detour, card 507's `detour_c507_local_script` in
`door_c161.cpp`, and `death.cpp` c50_step after c310_host_step):

- On the host, BeginPlay notes each BP_PickupTracker_C in a weak table (16 per map) together with its two UFunctions.
- When a tracker's own Enable Delegates body has run, the call is repeated by ProcessEvent for every roster pawn
  inside a card 102 acting scope (slot 0 with no scope, other slots with `kC102Place`). Each slot's bound pawn is kept
  as a weak pointer. A Disable Delegates body is repeated the same way for every pawn still bound.
- Every 0.5 s, an enabled tracker is bound again for a slot whose pawn changed (a respawn or a join).
- Binding is AddUnique and the handlers check the tracker's Active flag, so a repeat or a stale bind is harmless.
- Rows: `card582: host t=... tracker <name> bound for slot S pawn P (Enable Delegates|pawn changed)`, and `tracker
  <name> delegates disabled for every pawn`. Map tally: `card582: map: T pickup trackers, B pawn binds, U pawn unbinds`.
  WARN rows: table full, and a tracker with no Enable/Disable Delegates function.

Predictions (b411, FED_C2M1 or FED_C5M1 with a client connected): at least one bind row for slot 1, no card582 WARN.
After the client equips and then fires or drops the nuke, the tracker's respawn timer runs and the pickup comes back.
Card 572 differ stays 0. Known limit: DeleteFurthestTempPickup still measures distance from the host's pawn.

## Open items

0. G3 needs one log read before code. Inside a client shot's damage, `detour_c74_damage` holds a card 102 damage scope
   for the copy, so `Causer == GetPlayerCharacter(0)` may already pass and the attached sound would then play on the
   host at the copy with no relay to the client. Check the c582b410a host log (C99 log on) for `card099:   caller ...
   context BP_KillableBuilding*` rows. If they are present, the fix is a card 455 relay row for the building damage
   sound. If they are absent, the fix is the causer-player compare.
1. G6, G7, G8, G9 verdicts from the step 1 rows.
2. Level-script verdicts (camera shake, 2D sound, control rotation, actor location).
3. Does BP_HordeZone run on the client (placed, non-replicated)?

### Run c582b413b predictions (b413 `093a98bc473b3a85` = b412 + card 586 (b) guard + card 585, c372-c3m1)

Scenario fixes from c582b412a's logged causes: line 75 `host warp client BP_PlayerCharacterMI_C 300` is now `try`
(at the tower Door Gunner pickup every landing failed the sightline, "no candidate held", and the group failure cut
line 81 and 94 waits to 1 s); the host's PossessedBy(trooper) wait after its seat is strict, so `host down host` runs
on the trooper.

- The quit table shows a BP_MortarZone_C row with entry (place) or (overlap).
- The G3 door shot (bounds look, no focus=) gives a BP_KillableBuilding*/BP_Door_* context row, or not; reported
  either way.
- No card130 full WARN. No crash. No `WARN card043 ... not a trooper` row (the host is in its trooper at the down).
- Card 585: zero client `WARN card451 ... bark row dropped` rows with speaker 0; the host bark row prints `culled`.
- card132 0 mismatches.

### Run c582b413b result (b413 `093a98bc473b3a85`; c582b413a was void: launched without the header's TRAVEL/GOD/DIAG env)

- Quit table: "player 0 remapped 70401 times by 51 callers"; `caller exe+0x3C7F202, context BP_MortarZone_C (place),
  416 calls`. Met.
- G3: `caller exe+0x3C7F202, context BP_Door_Sliding_Gigantic_C (damage), 13 calls` (the door shot); no
  BP_KillableBuilding row. Also `exe+0x523E0FD, GA_TacSupp_DoorGunner_C (context), 1 call`.
- No crash; no `WARN card043 ... not a trooper` row (the strict PossessedBy wait held the host in its trooper); the
  line 78 client warp passed. 0 FAIL lines.
- No card130 full WARN (2 "component gone" rows on the host's own grenade). Card 585: 0 client card451 WARN rows;
  host bark rows print `culled 0`.
- card132: client 5 rounds, 0 mismatches.

## G3 verdict (senior, 2026-11-21, from c582b413b)

- BP_Door_Sliding_Gigantic_C is a BP_KillableBuilding_Base subclass (host row `BndEvt__BP_KillableBuilding_Base_
  HealthComponent...`). Its damage event remapped player 0 to the copy 13 times (`caller exe+0x3C7F202 ... (damage)`),
  so `Causer == GetPlayerCharacter(0)` already passes for a client shot. The causer-player compare needs no fix.
- The asset table has one sound of that prefix: `MS_EMIT_Human_Building_Generic_Damaged`, used both by the plain
  PlaySoundAtLocationFName at the FX location and by the attached-to-player call (by FName).
- The client log has 4 `audio row MS_EMIT_Human_Building_Generic_Damaged from a world row` rows, all at the building
  (9-29k uu from the listener). There is no row at or attached to the client's own pawn, so the attached copy does not
  reach the client: an all-effects-sent gap.
- Next (static first): find what `USTCommonUtilityLibrary::SpawnSoundAttachedToPlayerWithAIAwareness` calls. If it
  ends in `SpawnSoundAttachedToComponentWithAIAwareness` (hooked in guards.cpp, card 052), the fix is in that detour:
  when the attach component's owner is a server copy, skip the host play and send the asset to the copy's PC with the
  card 455 row (c455_host_relay with a table row for `MS_EMIT_Human_Building_`). Client plays it attached to its own
  pawn. If it ends elsewhere, hook that path the same way. Prediction: one client card455 row per client building hit
  that passes the sound's own cooldown.

## Step 1 coded (senior, 2026-11-21, rides b414)

Step 1 was never built: c582b413b has no `player 0 not remapped` or `level script` rows. It is now coded in
player_c99.cpp, and player_c99.cpp passed the syntax check (rc 0).

- Host, UBWCOOP_C99_LOG only: `c99_answer` calls `c582_kept_row` when the pick keeps the host's answer. It writes one
  row per (context class, caller) while a client is connected: `card582: player 0 not remapped: context <class>,
  caller exe+0x..., owner <class>, instigator <class>`. The table holds 64 keys per map. When it is full, the map
  totals print a WARN.
- Both sides, UBWCOOP_C99_LOG only: PlaySound2D, SpawnSound2D and ADAudioManager::PlaySound2D (the PlaySound2DFName
  path) write `card582: <side> t=<ms> level script <call> <asset> volume <v> (<class>)` when the context class ends in
  `_Scripting_C`. Rows are capped at 40 per map. A per-(call, asset) tally prints with the card099 totals at map arm
  and at quit. Camera shakes are not covered: the camera manager detour has no world context. Shakes stay an open
  item.
- Predictions for the next C99-log run with a client throw of a pod and a flare (FED_C3M1): rows with context
  BP_SupportFlare_C (G6), BP_TacSupp_Pod_C (G8) and BP_AIGrenade_Frag_VivaGranata_C (G9) appear only if those reads
  keep the host's answer. Absence after a confirmed client throw means the remap already covers the gap. G7 is
  already covered: c582b413b remapped BP_MountableTurret_Gunner_C (context) 2040 times.

## Open item 3 verdict (senior, 2026-11-21): BP_HordeZone runs on the client

`logs/c582b413b-client.log` has 8 card132 template-child rows whose owner is `BP_HordeZone_C`. The zone is a placed
level actor, so the client loads its own instance with the map. A placed actor's BeginPlay and Tick run on each
machine, so HordeAudio's player-0 reads follow each machine's own player, as in single player. The read at bytecode
2492 (spline-closest location, then DrawDebugSphere) is inside the `Debug` branch only. No fix is needed. If a playtest reports the horde
ambience missing on the client, reopen this item.

## Level-script camera shakes (senior, 2026-11-21, rides b414)

- The `APlayerCameraManager::StartCameraShake` detour has no caller context, so a level script's shake could not be
  told apart there. The always-installed `USTCommonUtilityLibrary::StartCameraShake` detour (card 551,
  `detour_c551_start_shake`) does take a context. It now calls `c582_level_row("StartCameraShake", context, shake,
  scale)`, which logs only under C99_LOG and only for `*_Scripting_C` contexts.
- The level row text now reads "volume/scale" (sounds carry the volume, shakes carry the scale).
- A level script that shakes through `GetPlayerController(0)` and the PC is already covered by the card 99 remap and
  its card582 not-remapped rows.
- Prediction for b414 with C99_LOG: each level-script shake prints a "level script StartCameraShake" row on the host.
  If the client has no matching shake (card 551 rows, or a camera manager row), that shake is a split gap, carded by
  class (a level-script shake relay like card 455's sound relay).
- Syntax check for player_c99.cpp returned rc 0.

## G3 fix (junior, 2026-11-21, rides b414)

Native path (static): `SpawnSoundAttachedToPlayerWithAIAwareness` (exe+0x5250F40) gets the player pawn through
exe+0x523E0F0, which calls `UGameplayStatics::GetPlayerController(ctx, 0)` (call return exe+0x523E0FD, the card 099
remap applies) and reads PC+0x2E8. It then passes pawn+0x1B8 (RootComponent) to
`SpawnSoundAttachedToComponentWithAIAwareness` (exe+0x5250A80, the guards.cpp card 052 detour) at exe+0x5250FF0. The
first FName argument is the sound table row name (looked up at exe+0x5250BE2), the second the attach point; the detour
parameters were renamed to match.

Fix: `c455_attached_relay` (player_c99.cpp), called from the detour. On the host, a component whose owner is a server
copy and whose sound starts with `MS_EMIT_Human_Building_` is not played; "@<sound> <attach> x y z stop auto vol pitch
start" goes to the copy's PC as a card 455 row. The client parses it and plays the sound attached to its own pawn's
RootComponent (`card455: client ... played attached sound`). UBWCOOP_C99_LOG adds a host row per attached call
(owner, server copy or not, listed or not, caller), 24 per map.

Prediction (c372-c3m1 with C99_LOG): host rows show MS_EMIT_Human_Building_Generic_Damaged on the client copy (server
copy, listed) for client building hits, one client "played attached sound" row each, no WARN. If the host rows say
"not a server copy" (host pawn), the card 099 remap did not apply at exe+0x523E0FD and the relay never fires: follow
up there.

## G3 proof status (senior, 2026-11-21, from c582b414a/b logs)

Not proved yet. Both b414 runs have no host `card455 ... attached sound MS_EMIT_Human_Building_*` row on a server
copy, and no client `played attached sound` row. Every host attached-sound row is "not a server copy" (drop pod
falling sounds, the Gigantic gate malfunction). The one client Building_Generic_Damaged row comes from a world row
29,222 uu away, not from a client hit. The scenario never made the client hit a BP_KillableBuilding. The next c372-c3m1
run needs one client `look` at a killable building plus a fire step, with C99_LOG on, to show the relay row pair.
Done in the scenario (senior, 2026-11-21): `tools/scenarios/c372-c3m1.txt` now has a client BP_AI_CannonTurret_C hit
after the door shot (try steps, `card455: client .*played attached sound` wait, shot c582-g3-turret-client). In
c582b414b the turret's Building_Generic_Damaged reached the client only as a world row.

## G3 status (senior, 2026-11-21)

The turret target failed in c595b447b-d: the host logged no card455 turret hit sound, and one turret fire sound
missed its client actor (new card 597). G3 needs another target with a host card455 attached building sound.

## G3 target (junior, 2026-11-21, static and existing logs)

- BP_KillableBuilding_Base damage event (build/kis_res/BP_KillableBuilding_Base.uasset.txt, bytecode 1065-1266): if
  not Destroyed, PlaySoundAtLocationFNameWithAIAwareness(Damaged SFX) at the FX location, then
  `EqualEqual_ObjectObject(DamageEvent.Causer, GetPlayerCharacter(self, 0))`; only a true compare reaches
  SpawnSoundAttachedToPlayerWithAIAwareness(MS_EMIT_Human_Building_Generic_Damaged).
- For a gun hit the causer is the weapon, not the pawn: card166 rows read `causer BP_Weapon_MoritaMk1_C`
  (c595b447d-client.log 198679291). Across every log in logs/ the causer classes are weapons, projectiles
  (ADProjectile), hazards and AI pawns; `causer BP_PlayerCharacterMI_C` never appears. So the compare is false for
  gun and projectile hits on the host's own pawn too, and the attached sound never plays in single player for them.
  That is why no run has a host card455 `attached sound MS_EMIT_Human_Building_*` row, for the door or the turret.
  It is not a co-op gap for those hits.
- The only player damage that might pass is one whose causer is the pawn itself: melee (GA_HumanMelee_C, key V,
  card 439 scenario c439-underslung-melee-c1m1.txt). Not checked: whether the melee causer is the pawn or the knife
  actor (card169 spawn rows).
- Proposed target, one C99_LOG run on FED_C3M1: host V melee on BP_Door_Sliding_Gigantic_C first (control), then
  client V melee on the same door.
  - Prediction A: host row `card455: host ... attached sound MS_EMIT_Human_Building_Generic_Damaged on
    BP_PlayerCharacterMI_C (not a server copy ...)` for the host melee, then the server-copy row and one client
    `played attached sound` row for the client melee. G3 proved.
  - Prediction B: no host attached row for either melee. The melee causer is not the pawn either; the attached call
    is unreachable for players in single player, and G3 closes as no gap (the c455_attached_relay stays, harmless).

## G3 melee causer (junior, 2026-11-21, static)

- GA_HumanMelee_C DealDamage (build/kis_res/GA_HumanMelee.uasset.txt, offset 1126):
  `STCombatUtilityLibrary::ApplyDamageAtLocation(GetPlayerCharacter(self, 0), GetPlayerCharacter(self, 0), template,
  location)`. The causer is the player pawn, not the knife (KnifeVisuals is a visual actor attached to the first
  person mesh and destroyed after the swing; it is not passed to the damage call).
- On the host, that read is already remapped for a client's melee: c439b168 host
  `card099: player 0 remapped (context): caller exe+0x3C7F116, context GA_HumanMelee_C (GA_HumanMelee_C), copy
  BP_PlayerCharacterMI_C`. So a client melee's causer is the client copy, and the building's own
  `GetPlayerCharacter(0)` (remapped in the damage scope, c582b413b `caller exe+0x3C7F202 ... (damage)`) should
  match it.
- So melee is the one player hit that reaches SpawnSoundAttachedToPlayerWithAIAwareness. G3 needs the melee run.

## G3 melee steps (for the next FED_C3M1 run, not the gate; C99_LOG on the host)

Melee reach is short (64 uu sphere trace from the camera), so the warp distance needs a check on the first try: if
the host wait fails, look at the shot and move closer.

    # --- card 582 G3: melee on the Gigantic door. Host first (control), then the client.
    host clear 4000 kill
    host mark
    host warp actor BP_Door_Sliding_Gigantic_C 200
    host look actor BP_Door_Sliding_Gigantic_C 0 bounds
    host key V
    try host wait log "card455: host t=\d+ attached sound MS_EMIT_Human_Building_Generic_Damaged on BP_PlayerCharacterMI_C \(not a server copy" since-mark timeout=5
    host shot c582-g3-melee-host
    sleep 2
    host mark
    client mark
    host warp client BP_Door_Sliding_Gigantic_C 200
    client look actor BP_Door_Sliding_Gigantic_C 0 bounds
    client key V
    try host wait log "card455: host t=\d+ attached sound MS_EMIT_Human_Building_Generic_Damaged on BP_PlayerCharacterMI_C \(server copy" since-mark timeout=5
    try client wait log "card455: client t=\d+ played attached sound .*MS_EMIT_Human_Building_Generic_Damaged" since-mark timeout=6
    client shot c582-g3-melee-client

Predictions: the host melee gives one host attached row "not a server copy" (the control: the call is reachable). The
client melee gives one host "server copy" row with a relay, and one client "played attached sound" row, no WARN. If
the control row is missing, the melee missed the door (check the shot) or the door's Destroyed flag is set; G3 stays
open, no code change.

Prediction C (senior, 2026-11-21): the host control row appears, but no card455 attached row appears for the client
melee on either side. The door's own `GetPlayerCharacter(0)` read then returned the host pawn, so the compare was
client copy vs host pawn. G3 is a real gap. Next: read the host card099 caller rows at the client melee for a
BP_KillableBuilding_Base / BP_Door_* context. A `(damage)` remap row there (as in c582b413b, `caller exe+0x3C7F202,
context BP_Door_Sliding_Gigantic_C (damage)`) with no attached row points past the compare; no such row means the
remap list needs that context.

Run c582b449a (b449, dll d7867a1f250714d9, 2026-11-21): scenario passed, WARN host 5 / client 4, no crash. G3 not
proved: both melees missed. The 200 uu warp ring from the door pivot left the look hit 873-886 uu from the eye, and
the knife reaches 64 uu (lesson 86), so neither prediction A/B nor C was tested. Rerun c582b449b with
tools/scenarios/c582-c3m1-b449b.txt: point warps 100 uu short of run a's host look hit (-68556 -767 1333).

Run c582b449b (2026-11-21): scenario passed, WARN 5/4, no crash. G3 not proved again: the point warps were right,
but `look actor ... bounds` turned each player toward the bounds centre (-68582 -44 1453), behind them; 0 of 26
samples hit the door. No card455 attached row on either side, the control included. Rerun c582b449c
(tools/scenarios/c582-c3m1-b449c.txt) looks at the door face points (-68556 -767 1333) and (-68496 -767 1333).

Run c582b449c (2026-11-21): scenario passed, WARN 5/5, no crash. The host melee hit the door (card161 OnDamageTaken;
card304 player sound MS_EMIT_Human_Building_Generic_Damaged on the host pawn; client card173 remote sound). No card455
attached row: the 24-row UBWCOOP_C99_LOG attached-diag cap (kC455AttachedDiagLines, player_c99.cpp) was spent by the
1 Hz MS_EMIT_Gate_Large_MilitaryBase_Malfunction rows (not listed, not a server copy) before the melee. The client at
-68483 -660 hit StaticMeshActor_3116, not the door. Rerun c582b449d: the host steps aside, the client melees from the
host's spot; checks are card161 and the card455 relay row. Code follow-up (not built): count only listed or
server-copy rows against the attached-diag cap.

Run c582b449d (2026-11-21): scenario passed. Host control proved: host melee at -68543 -668 1266 looking at
-68556 -767 1333 gave card161 OnDamageTaken on BP_Door_Sliding_Gigantic_C_1 and card304 player sound
MS_EMIT_Human_Building_Generic_Damaged on the host pawn; the client heard it (card173). Client melee from the same
point and look (client aim hit at -68550 -750 1319, 102 uu, same as the host): the server copy activated GA_HumanMelee
(card105/card019/card104 DoAction Melee), the knife swung (MS_WPN_Knife_Attack, card162), but no
MS_EMIT_Human_Building_* row of any kind followed on the host (no card304, no card455 attached or relay row) and the
client played none. Not decided: whether the copy's melee damaged the door at all. card161 PE rows log the first call
per door and event only (c161_seen), so a second OnDamageTaken is silent. Neither prediction A/B nor C is confirmed:
C expects the sound on the host pawn (a card304 row) and none came. G3 needs a diag before another run: a per-hit row
for the door's damage (instigator, amount) at the client melee, or card099 caller rows for the copy's melee trace.

### b450 run c582b450a: predictions (written before the run)

Build b450 (dwmapi 2d1616a348c96582, ubwperf 4ffd32378416a4b5), diag only, no c448 gate. Two diags:
(a) `card582: <side> t= building hit <door> (<class>): damage, Health old -> new; instigator, causer` from the
UADDamageHandler::Process detour, and `card582: <side> t= building <door> (<class>) PE OnDamageTaken`, both on every
BP_KillableBuilding_Base subclass under UBWCOOP_C99_LOG, 6 rows per building per second. (b) card455 attached-diag
rows: only listed or server-copy rows count against kC455AttachedDiagLines; the host's own unlisted ones have their
own cap of 8. Scenario `tools/scenarios/c582-c3m1-b450.txt` (b449d with the pins and the two door checks changed).

1. Host melee: a host `building hit BP_Door_Sliding_Gigantic_C_1` row (or the PE row) with instigator the host pawn
   (host-own) and causer the pawn or its knife, then the card304 Building_Generic_Damaged player-sound row.
2. Client melee: does the host log a `building hit BP_Door_Sliding_Gigantic_C_1` row with instigator or causer = the
   client's server copy? If yes, is there a card455 attached-sound row (listed, server copy) and a relayed row?
3. If the copy's melee makes no building row at all, the gap is in melee damage on the server copy, not in sound
   sync; the next step is the copy's melee trace, not card 455.
4. The client log has no `building hit` rows of its own (damage is host-side).

### b450 run c582b450a: result (scenario passed, 2026-11-21)

- Host melee (control): `card582: host building hit BP_Door_Sliding_Gigantic_C: damage 0.00, Health 400.0 -> 400.0;
  instigator BP_PlayerCharacterMI_C (host-own), causer BP_PlayerCharacterMI_C`, then PE OnDamageTaken, card455
  attached sound MS_EMIT_Human_Building_Generic_Damaged on the host pawn, Shake_HumanMelee_Hit_C. Prediction 1 held.
  The door takes 0 damage per knife hit, but OnDamageTaken and the damaged sound still fire.
- Client melee: void. The server copy ran DoAction Melee (card104, t=201563109) with only Shake_HumanMelee_C (no Hit
  shake) and no card582 row, but the client `look point` ran 25 ms after `host warp client` and aimed from the
  client's old spot (yaw 277.3 instead of about 262). The miss is the scenario's aim, so predictions 2 and 3 are
  untested. Lesson 88.
- WARN counts: host 7, client 4.
- Scenario fixed: `client wait near -68543 -668 1266 80` before the client look; card582 regexes match the name
  without `_1`. Rerun c582b450b on the same DLL (2d1616a348c96582), same predictions.

### b450 run c582b450b: result (scenario passed, 2026-11-21)

Same DLL 2d1616a348c96582, scenario fixed per lesson 88. Logs: `UBWCoop-host-20261121-231537.log`,
`UBWCoop-client-20261121-231539.log` (lane 2 Win64). WARN: host 7, client 6.

- Host melee (control) again hit: card582 building hit row (damage 0.00, Health 400.0 -> 400.0, causer the host
  pawn), PE OnDamageTaken, Shake_HumanMelee_Hit_C.
- Client melee, now with a matching aim: client look from (-68532 -668 1392) yaw -103.9 pitch -29.9, 117 uu (host:
  same point, yaw -103.9 pitch -33.7, 122 uu). Server copy camera row at 202016007: ControlRotation P 330.10
  Y 256.09, camera after P -29.90 Y -103.91. The copy's knife sound plays at (-68530, -668, 1292), the host's spot.
  The copy activates GA_HumanMelee (handle 123, client request, 202016399), spawns BP_Knife_C, and runs DoAction
  Melee at 202016516, then only Shake_HumanMelee_C. No Hit shake, no card582 building row, no OnDamageTaken, no
  Building_Generic_Damaged sound anywhere.
- Prediction 2 failed, prediction 3 holds: the server copy's melee does no damage at the same spot and aim where
  the host's does. The gap is in melee hit detection on the server copy, not in sound sync. The relay of the
  damaged sound is untested because the event never happens.
- Next step (proposed): trace the copy's melee hit test. Find the DoAction Melee path's trace or overlap call
  (GA_HumanMelee / BP_Knife_C), log its start, end and hit for host and copy. Suspect: a trace from a mesh socket
  whose pose is stale on the unrendered server copy, or a trace from the copy's view point.

### G3 melee, b451 diag and predictions (written before run c582b451a)

**Log grep (before the build).** No log in `logs/` has a server copy `Shake_HumanMelee_Hit_C`, a card161 or card019
kill, or a damage row caused by a client knife. Every c582 run has the copy swing once and hit 0 times, and
c169-r2-host.log has the same result. The client's melee has never been seen hitting anything, which makes this a
real gameplay bug: a client cannot knife bugs. It outranks the sound question.

**Static path.** From `build/kis_res/GA_HumanMelee.uasset.txt`: OnDoAction runs DealDamage. DealDamage and
TraceForTarget both call `GameplayStatics::GetPlayerCameraManager(self, 0)` and take its K2_GetActorLocation and
GetActorForwardVector. They then run
`SphereTraceSingleForObjects(self, PCM loc, PCM loc + fwd * DamageOriginDistanceFromCam, 64, ...)`. Damage is
`ApplyDamageAtLocation(GetPlayerCharacter(self, 0), ...)` at the hit. Did_Damage false plays the swing shake only. The
knife actor (BP_Knife_C) is visual, attached to FirstPersonMesh. No socket or anim pose is read on the damage path.
`execGetPlayerCameraManager` (exe+0x3C7F060) calls the detoured GetPlayerController at exe+0x3C7F111, so its return
address is exe+0x3C7F116. The card 099 gate therefore sees the ability's call. However, b450 has no card099 or
card582 row with a GA_HumanMelee context for either player's swing.

**Diag (b451).** A detour on UKismetSystemLibrary::SphereTraceSingleForObjects (exe+0x3D41440, the exec thunk calls
it at +0x472), behind g_c99_log, 24 rows per map, logs only when the context class starts with GA_HumanMelee. Row:
`card582: host t=... melee trace <ability>: start, end, radius, hit, actor; outer, copy; player 0 answer <pc>,
camera manager at (...); host camera, copy camera`.

**Predictions (one of these holds).**

- (a) Stale socket or pose (VisibilityBasedAnimTickOption). Ruled out statically: the trace starts at the camera
  manager, not at a socket. If the rows show a start at the copy camera with a correct forward vector and still no
  hit, this returns as the leading case.
- (b) View-point trace from the copy's own camera manager. The copy row's start is near the copy camera
  (-68530 -668 ~1390) and the end is about DamageOriginDistanceFromCam along the client's aim. If hit is 0 there
  while the host's identical trace hits, the cause is in the copy PCM's rotation or in the object types.
- (c) New, most likely: the player 0 answer for the copy's ability is the host's controller. The copy row then
  names `player 0 answer BP_PlayerController_C` with the same pointer as the host's own swing row, and the start
  equals the host camera (the host stands at -68300 -800 1266 during the client swing). The trace runs from the
  host and misses the door. The fix would be a card 099 context rule: a UGameplayAbility resolves to its avatar
  actor (the ability's outer chain, or ActorInfo).

Proof needed: one host row and one copy row in c582b451a.

### b451 run c582b451a: melee trace result

DLL c4b1edf9083c61a5. Host log `UBWCoop-host-20261121-233040.log` (lane 2 Win64).

| Swing | Trace | Start | End | Radius | Player 0 answer | Camera manager at |
|---|---|---|---|---|---|---|
| host | TraceForTarget | (-68533, -666, 1375) | (-68720, -1570, 991) | 96 | host PC 82A50BE0 | (-68533, -666, 1375) |
| host | DealDamage | (-68538, -689, 1376) | (-68586, -920, 1278) | 64 | host PC 82A50BE0 | (-68538, -689, 1376); door hit row follows |
| copy | TraceForTarget | (0, 0, 0) | (1000, 0, 0) | 96 | copy PC ACEA7480 | (0, 0, 0) |
| copy | DealDamage | (0, 0, 0) | (256, 0, 0) | 64 | copy PC ACEA7480 | (0, 0, 0); no hit row |

The copy camera was at (-68529, -666, 1373) and (-68534, -686, 1375).

None of predictions (a), (b) and (c) holds. The card 099 gate does remap the ability's player 0: the copy's own
controller answers, so (c) is wrong. The cause is a fourth case, (d). On the host, the server copy's
PlayerController has a PlayerCameraManager whose actor transform was never updated. It sits at the world origin
with zero rotation, so its forward is +X. Every `GetPlayerCameraManager(0)` location or rotation read made for a
server copy returns the origin. The copy's knife traces from (0, 0, 0) and can never hit. The same applies to
TraceForTarget (melee lunge), and to any other Blueprint that aims from the camera manager for a copy.

Proposed fix (one class rule, next card): keep each server copy PC's camera manager transform at the copy's view
on the host. Location is the copy camera component; rotation is the copy's control rotation (the same pair the
card 582 camera row logs). Update it each host tick for live copies, or at least before the ability runs. Proof:
the copy row's start equals the copy camera, a door hit row with causer the copy, and Shake_HumanMelee_Hit_C on the
copy.

### G3 next (senior, 2026-11-21, after b452)

Card 599 fixed the copy melee hit (c599b452a: the copy hits the door, and a client knife kill). Two sound gaps remain, and both are now plain G3 work:

(a) **Building damaged sound for a copy hit.** BP_KillableBuilding_Base plays MS_EMIT_Human_Building_Generic_Damaged only when DamageEvent.Causer == GetPlayerCharacter(0). In the door's context, player 0 is the host pawn, because the card 099 remap covers only the GA_HumanMelee context. A copy causer therefore never matches. This is the old Prediction C gap, which could not show until the copy could hit. Before writing code, confirm it statically from the card099 caller rows at t=203806841 (no remap row for a BP_KillableBuilding context). Fix: add a card 099 remap for BP_KillableBuilding_Base that resolves player 0 to the PC whose pawn is the damage causer, scoped to that damage event. Proof: a card304 or card455 row on the host with the copy pawn as owner, and the client hears it (card173).

(b) **MS_GORE_Knife_Attack_Impact missing on the client** for the copy's knife, while the host plays it (card173). Find which path plays it on the host, then why no row goes to the client: a filter on the owning PC, or a play-locally path. Fix it by class, covering every sound that is skipped for the owner's own PC in the same way.

Both go into one build with one C3M1 run.

### G3 trace and fix (b453, 2026-11-22)

Static trace, both gaps:

- (a) The card099 kept-row table was already full (64 keys) before the client melee at t=203806841, so a missing row there proves nothing on its own. The cause is in the code path. GA_HumanMelee DealDamage calls `USTCombatUtilityLibrary::ApplyDamageAtLocation(GetPlayerCharacter, GetPlayerCharacter, template, loc)` (GA_HumanMelee bytecode 1126), and that function had no card 102 scope. Only ApplyDamageFromImpactHitResult had one (`detour_c74_damage`). So the door's damage handler `Causer == GetPlayerCharacter(self, 0)` ran with no acting copy, and player 0 resolved to the host pawn.
- (b) The host plays MS_GORE_Knife_Attack_Impact through the GC_Impacts_PlayerMelee cue on the copy pawn. Host rows at 203821075: card173 remote sound, and card551 skipped shake and force feedback. The card 136 row carries the copy pawn as instigator, so `filter_hits` skips it for the copy's own PC. The filter assumes that PC made the effect itself. The client never runs the impact (its log has only the knife spawn and destroy), because the hit happens only on the host.

Fix, by class:

- New detour on ApplyDamageAtLocation (exe+0x522D5C0, `detour_c582_damage_at` in player_c99.cpp). It opens `c102_enter(instigator ?: causer, kC102Damage)`, the same scope gun damage has. Every damaged actor's player-0 check inside it then resolves to the acting copy's PC.
- When the acting actor is a server copy and instigator == causer (a self-caused hit: the knife), `c582_copy_melee()` is true for the duration of the call. `c136_net_queue` then clears the row's send filter, so no PC is skipped. This applies to every cue, sound and Niagara row the hit makes.
- Rows from projectile users of ApplyDamageAtLocation (the projectile is the causer) are not touched.

Predictions for `c582b453a` (b453 dwmapi `b51bd70464acbc03`, ubwperf `76cc4e94730b4490`, `tools/scenarios/c582-c3m1-b453.txt`):

1. Client knife on the door: the host logs `card455: host ... relayed attached sound MS_EMIT_Human_Building_Generic_Damaged to BP_PlayerController_C (sent)`, and the client logs `card455: client ... played attached sound ... Generic_Damaged`.
2. The host logs `card582: host ... knife row MS_GORE_Knife_Attack_Impact (...) of BP_PlayerCharacterMI_C... sent to its own PC too`, or the cue row (null asset name with a cue tag). The client log names MS_GORE_Knife_Attack_Impact at least once after the door knife. The knife kill block adds bug hits.
3. The host's own knife (the control) has no card582 knife row, because the host pawn is not a copy.
4. Gun damage is unchanged: no card582 knife rows outside the knife steps.
5. 0 FATAL/ERROR/CRASH, mismatches 0, no new WARN class against c599b452a.

If prediction 2 has no knife row, the cue runs outside ApplyDamageAtLocation (deferred cue flush), and (b) needs the cue's own path instead.

Result `c582b453a` (b453 `b51bd70464acbc03`, 152 s, rc 0/0, drive rc 0, FATAL/ERROR 0, mismatches 0, no dumps). WARN classes: host attach, card046, card132 (bug hit offset), card537, config. Client: attach, card046, card537. All of them are known classes from c599b452a.

1. **Held: (a) is fixed.** On the client knife on the door, the host logs `card455: host t=204941656 relayed attached sound MS_EMIT_Human_Building_Generic_Damaged to BP_PlayerController_C (sent)`. The client logs `card455: client t=204941683 played attached sound @MS_EMIT_Human_Building_Generic_Damaged`, 27 ms later.
2. **Held, with one correction: (b) is fixed.** The door knife made no GC_Impacts_PlayerMelee cue on either side, on the host too. The b452 knife impact row was from a bug hit, not the door. On the three bug hits (204949889, 204951505, 204956058), the host shows these rows:
   - `knife row null (BP_AI_WarriorBug_C) of BP_PlayerCharacterMI_C sent to its own PC too`, which is the cue row.
   - card173 `remote sound MS_GORE_Knife_Attack_Impact`.

   The client now receives the cue at 204949934, 204951533 and 204956084 (`card551: client ... skipped impact cue screen shake of Default__GC_Impacts_PlayerMelee_C: relayed cue of a remote player`). Those are 45, 28 and 26 ms after the host. The client plays the knife impact sound inside the relayed cue, so it is not a separate named row. b452 had none of these client rows.
3. **Held.** The host's own knife (the control) has no knife row.
4. **Held.** Knife rows appear only in the knife steps. The other rows in scope were already sent before this change: bug pain bark and green blood hit (instigator the bug), and the door damaged sound (no instigator). The new filter does not change them, and the client has no local copy of them, so nothing doubles.
5. **Held.**

Two TRY-FAILs, both known: the scenario's own knife-row regex expected the copy as instigator on the door row, and the WarriorBug "hit 1" trace wait. The bug took the hits (cue rows) and died.

### Double-play check and closure list (junior, 2026-11-22, logs only)

Double play (logs/c582b453a-client.log, 3 knife hits at t=204949934, 204951533, 204956084). Every asset plays once
per hit:
- GC_Impacts_PlayerMelee (which carries MS_GORE_Knife_Attack_Impact): 1 relayed cue per hit. Its screen shake and
  force feedback are skipped (card551 "relayed cue of a remote player"). The client has no local cue and no local Hit
  shake row.
- MS_GORE_2D_Generic_Hitmarker: one play per hit. Each hit logs two card099 rows, but the second (UGameplayStatics
  PlaySound2D, caller exe+0x4A1DFB8) is the inner call of the first: exe+0x4A1DFB8 is 0xA8 into
  UADAudioManager::PlaySound2D (0x04A1DF10). Hit 3 also plays MS_GORE_2D_Kill_Hitmarker once (the bug died).
No asset doubles, so the filter clear stays as it is.

Closure list:
- Proof owed, static: done (verdict table, fix plan, the G3 verdict from c582b413b).
- Proof owed, run, mortar aimed at a lone client: still open. No run has a client alone in BP_MortarZone. All 18
  C99-log C3M1 runs show `player 0 not remapped: context BP_MortarZone_C, caller exe+0x3C7F202` (the host's answer
  kept), so it is not even shown that the step 2 place rule covers this read.
- Proof owed, run, marker threat for both pawns: done (c244b443a: 47 EM marker threat calls for the host pawn,
  mirrored 17 times to client pawns).
- Proof owed, run, building hit sound on the client: done (c582b453a, card455 attached sound relayed and played).
- Open item 0 (G3): done (verdict c582b413b; fix b414 relay plus the b453 damage scope; proof c582b453a).
- Open item 1, G6 (flare): still open. No C99-log run had a confirmed client flare throw, so the missing
  BP_SupportFlare rows prove nothing.
- Open item 1, G7 (turret): done (c582b413b, 2040 BP_MountableTurret_Gunner_C remaps).
- Open item 1, G8 (pod): still open. Every C99-log C3M1 run keeps the host's answer for BP_TacSupp_Pod_MechDrop_C
  (callers exe+0x3C7F202 and exe+0x5251EEF, owner none, instigator none). No run tells a client-called pod apart
  from a scripted mech drop.
- Open item 1, G9 (AI frag grenade): still open, low priority. No rows yet.
- Open item 2 (level scripts): still open. Only C3M1 @15254 shows up (the host's Splosion MS_EMIT_MortarField_Explosion,
  ADAudioManager plus the inner PlaySound2D, 9 runs). The client side was never logged: every run had C99_LOG on the
  host only, and the level row is C99_LOG-only on both sides. No C99-log run reached the C1M1, C2M1, C4M2, C5M1 or
  C5M2 rows.
- Open item 3 (BP_HordeZone): done (verdict from c582b413b: runs on the client, no fix).
- Not numbered, still open:
  - G4: the fan-out is coded (b411), but every run shows 0 pickup trackers. Trackers live on C2M1 and C5M1 only.
  - G1, G5 and G10: the place rule is coded (b410) with no proof rows. No pylon has shown up since card 152.
  - The ABP_Arms_Skeleton copy candidate: unanswered.

## Closed (senior, 2026-11-22)

The class fixes are in and proven: G3 (b414, b453; c582b453a), G7 (c582b413b), the marker threat fan-out (c244b443a), and the client knife (card 599). Disposition of the open list:

- (a) Mortar and (c) mech drop pod: both read player 0 through the unremapped caller exe+0x3C7F202. Moved to card 600.
- (b) G6 flare, (f) G4, G1, G5 and G10 proof rows: the code is in, but no run reached them. They ride the C2M1 and C5M1 soaks (trackers) and the next run with a client flare throw. The rows are named in STATE.md. This is not a card.
- (e) Level scripts: future C99-log runs set UBWCOOP_C99_LOG on the client too. Each map's rows get read when its soak runs. This is not a card.
- (d) G9: no row ever fired. Dropped.
- (g) ABP_Arms_Skeleton: the first-person arms animation is local presentation (the "Fine" class). Dropped.
