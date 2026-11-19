# Card 173: The remote player's walking footsteps, and the remote sounds still 2D

Status: queued 2026-11-11, remainder of card 162 (playtest 2026-11-08 finding 2). Budget: static first, then one run.

Read: docs/cards/done/162-remote-audio-attenuation.md "Report"; mod/src/ubw/fx_c136.cpp `c162_remote_atten`;
mod/src/ubw/playlog.cpp `pl_remote_source`.

## Why
Card 162 gave a remote player's unattenuated sounds a fallback attenuation at the three hooked play points
(PlaySoundAtLocation, SpawnSoundAtLocation, SpawnSoundAttached) and on relayed rows. Run c162-r3 showed it for jump,
mantle and weapon drop, at close range only. Two things were not shown:
- Walking footsteps produced no sound line on any hooked path on either side, so their play path is unknown (anim
  notify, Blueprint, a MetaSound on the mesh, or not played for a server copy at all).
- The host logged "audio MSS_FOL_Low_Ammo_Click from BP_Weapon_MoritaMk1_C at (0, 0, 0) 19591 uu, 2D, attenuation
  null" for the server copy's weapon.

## Do
1. Static: find how the player's footstep sound is played (PDB, kis.py on the character and anim Blueprints) and
   why no hooked point sees it for the remote player. Write the path and a verdict into this card before code.
2. Bring that path under the card 136 manager or the card 162 fallback. No second sound relay.
3. The low-ammo click: it is a player's own cue; on the other side it should not play 2D (card 160 class). Fix in
   the same place if the trace shows one cause; otherwise report it.
4. One run, players at least 5000 uu apart: the remote player walks and jumps.

## Done when
- Predictions written before the run. Each side logs the remote walk's footstep line with an attenuation, and a
  card162 fallback line for a jump at 5000 uu or more from the listener.
- The usual Report items. Ends "Report complete."

## Files
mod/src/ubw/fx_c136.cpp, fx_c136_net.cpp, playlog.cpp, hooks_internal.h, tools/symbols.txt if a hook is needed,
one scenario tools/scenarios/c173-footsteps.txt.

## Trace
Addresses are absolute at image base 0x140000000. "Verified" claims quote disassembly, kis.py output or log lines.
"Open" claims are not shown.

### The footstep call on the local player (verified)
- The call chain from the Tick to the cue:
  - ASTPlayerCharacterMI::Tick (0x1451E13D0) calls `ASTPlayerCharacter::Tick` at 0x1451e13e1.
  - ASTPlayerCharacter::Tick gets the movement component at 0x1451e0e2a (`callq *0x748(%rax)`; the identity of the
    vfunc is open).
  - At 0x1451e0e38 it calls `UPawnMovementComponent::GetLastInputVector` (0x143A93680). That function reads
    PawnOwner (+0x180), then the pawn's +0x308, LastControlInputVector (the mod's kPawnLastControlInput).
  - At 0x1451e0e74 it compares that value against 0.02 (`comisd`).
  - It calls StartOrUpdateFootstepEffects at 0x1451e0ea8 and 0x1451e0eba.
  - PlayFootstepEffects (call at 0x1451df241) has an on-ground vfunc check (open) and a velocity check against 500.0
    at 0x1451d6e1f.
- PlayFootStepCue (0x1451D6AA0):
  - It calls GetSurfaceHitResult at 0x1451d6be0, into rsp+0x40.
  - It picks the tag at CharacterData+0x2A8 (Run, when sprinting) or +0x2A0 (Walk), at 0x1451d6c0c and 0x1451d6c1a.
  - At 0x1451d6c21 it calls
    `?ExecuteGameplayCueFromHit@USTGASUtilityLibrary@@SAXPEAVAActor@@0AEBUFHitResult@@AEBUFGameplayTag@@@Z`
    (pawn, pawn, &hit, &tag).
- ExecuteGameplayCueFromHit:
  - 0x14511dcb4 `leaq 0xb8(%r8),%r15` takes hit.HitObjectHandle.
  - It calls FetchActor at 0x14511e26b.
  - At 0x14511e27a it calls `UGameplayCueFunctionLibrary::ExecuteGameplayCueOnActor(hitActor, tag, params)`.
  - **The cue's target is the ground actor, not the pawn.**
- ExecuteGameplayCueOnActor:
  - 0x1449c5e2e `cmpb $0x3,0x168(%rdi)` is the Authority check on an actor that has an ASC.
  - 0x1449c5e4f `jmp` goes to the replicated `UAbilitySystemComponent::ExecuteGameplayCue` (0x14495A340).
  - An ASC without authority returns.
  - With no ASC, 0x1449c5e5d calls `ExecuteGameplayCue_NonReplicated`, which plays only on the local machine.
  - The ground has no ASC, so footsteps are a local, non-replicated cue on each machine.
- The cue notify assets (kis.py name tables):
  - GC_Impacts_HumanFootstepsWalk has tag GameplayCue.Impacts.Footsteps.HumanWalk and parent GC_Impacts_Base. Its
    sounds are HumanMovementSurfaceDirt, HumanMovementSurfaceMetal and HumanMovementSurfaceStone, and
    MS_FLY_PlayerMovement_Run_Grass, _Mud, _Sand and _Water.
  - GC_Impacts_HumanFootstepsRun has tag GameplayCue.Impacts.Footsteps.HumanRun and the Sprint_ variants.
- The sound call: USTImpactGameplayCueNotify PlayImpactSound calls PlaySoundAtLocationFNameWithAIAwareness with
  attenuation 0, concurrency 0 and owner 0. The calls then go GetAudioEvent(FName), then
  PlaySoundAtLocationWithAccessibility, then UADAudioManager::PlaySoundAtLocation, then the hooked
  `UGameplayStatics::PlaySoundAtLocation`.

### The same call for the remote player's pawn
- **Host, server copy: likely, open at runtime.** The card 019 input mirror (sprint.cpp:50-76, host only, Authority
  with an AutonomousProxy remote) writes LastControlInputVector, and the host log has "card019: input mirror"
  12 times in c162-r3. So the Tick gate at 0x1451e0e74 can pass. No log line shows that the cue ran.
- **Client, simulated proxy: likely not, open at runtime.** Nothing adds movement input to a simulated proxy
  (hooks_internal.h:974-982, the card 019 comment). Its +0x308 stays zero and the gate at 0x1451e0e74 fails. That
  is what stops it.
- **The relay: open.** c162-r3 host quit totals: "ExecuteGameplayCue_NonReplicated 112". The tags of those events
  were not logged (g_c136_log was off), so it is not shown that the host's footstep cues become card 136 rows.
  Client: "166 rows, 1 replayed, 130 played". A row replayed through the cue notify logs no sound line.

### Attenuation passed (partly verified)
- The call passes null (attenuation argument 0 in PlayImpactSound).
- HumanMovementSurfaceDirt_MS, HumanMovementSurfaceDirtSprint_MS and every MS_FLY_PlayerMovement_* asset import
  ATT_FLY_Player, CON_FLY_Player and SCL_PlayerHuman_Movement. HumanMovementJump_MS imports only
  SCL_PlayerHuman_Movement.
- Open:
  - that ATT_FLY_Player is the sound's AttenuationSettings field, and not only an import;
  - ATT_FLY_Player's bSpatialize and FalloffDistance;
  - the FName-to-asset map inside GetAudioEvent;
  - whether the accessibility path builds its own attenuation.

### Why c162-r3 logged no footstep
- The owner is null, so pl_sound logs only through the nearest-pawn rule (playlog.cpp pl_remote_source, 300 uu).
- The sound has its own attenuation, so the card 162 fallback returns before it logs, at fx_c136.cpp:871:
  `if (atten != nullptr || sound == nullptr || !pl_remote_source(owner, at, kC162NearUu, d) || c162_own_atten(sound))`.
- So neither hooked line could show a footstep. "Not played" was never shown.
- The c162 comment near fx_c136.cpp:768 says that HumanMovement*_MS have no attenuation. That is true for Jump_MS
  only.

### Low-ammo click (verified)
- BP_WeaponFireAbility (/Game/GAS/Weapons/BP_WeaponFireAbility), ubergraph from kis.py:
  - "264: CallFunc_IsValid_ReturnValue = …(LowAmmoTick)"
  - "307: CallFunc_SpawnSound2D_ReturnValue = …(self, LowAmmoTick, 1.0, 1.0, 0.0, None, false, true)"
  - "397: …GetRatioForLowAmmoClickSound(GetWeapon)"
  - "434: iface(CallFunc_SpawnSound2D_ReturnValue).SetFloatParameter(name:Ammo, …)"
- There is no locally-controlled or authority branch between 244 and 434.
- The child abilities (BP_WeaponFireAbility_MoritaMk1 and the others) set LowAmmoTick = MSS_FOL_Low_Ammo_Click.
- The ability runs on the host's server copy, so the host plays `UGameplayStatics::SpawnSound2D`. c162-r3 host line
  3720: "audio MSS_FOL_Low_Ammo_Click from BP_Weapon_MoritaMk1_C at (0, 0, 0) 19591 uu, 2D, attenuation null".
- Cause: this is the card 160 class, a server copy's 2D sound on the host.
  - player_c99.cpp `c160_copy_effect` is wired into detour_c99_sound_2d (line 484) and detour_c99_ad_sound_2d
    (line 508), but not into detour_c99_spawn_sound_2d.
  - hooks_ubw.cpp:1828 installs the SpawnSound2D row only with `&g_c99_log`.
  - This is not the footstep cause.
- Open: if the gate returns nullptr, line 434 calls SetFloatParameter on None. In Blueprint that should be an
  accessed-None warning, not a crash. This is not shown.

## Proposed fix
1. Click: in player_c99.cpp detour_c99_spawn_sound_2d, call `c160_copy_effect(kC160Sound, ctx, nullptr, sound)`
   and return nullptr on a skip. Drop `&g_c99_log` from hooks_ubw.cpp:1828 so the row is always installed. No new
   symbol: `UGameplayStatics_SpawnSound2D` is already symbols.txt:1157.
2. Footsteps: extend the card 162 fallback (fx_c136.cpp c162_remote_atten, line 871).
   - A remote sound whose own attenuation is not spatialized gets ATT_FLY_Trooper_Footsteps. c162_own_atten
     returns the asset; the check reads its bSpatialize.
   - Log the remote sound's line with its own attenuation name and bSpatialize, so footsteps are visible either
     way.
   - No relay change and no new symbol.
3. If the client logs no remote footstep at all, the card 136 relay of the host's NonReplicated footstep cue is the
   gap. That is a separate step; it is not in this fix.

Predictions for the single run (players 5000 uu or more apart, remote walks and jumps):
- Each side logs a remote HumanMovementSurface* or MS_FLY_PlayerMovement_* line naming ATT_FLY_Player and its
  bSpatialize, or the ATT_FLY_Trooper_Footsteps fallback.
- The host logs "card160: host … skipped server copy 2D sound MSS_FOL_Low_Ammo_Click".
- The host has no "audio MSS_FOL_Low_Ammo_Click … 2D" line.
- The client still plays its own click.

## Out of scope
- tools/symbols.txt:153 says "BP_PlayerCharacterMI_C is not an ASTPlayerCharacter". That is contradicted by
  ASTPlayerCharacterMI::Tick calling ASTPlayerCharacter::Tick at 0x1451e13e1.

## Code step

### 1a. Does card 136 carry the host's footstep cue? Yes, so 1b is built
- The cue's Instigator is the pawn. ExecuteGameplayCueFromHit stores arg1 (rdi, the pawn) into the cue parameters,
  which sit at rbp-0x20 (`14511e270: leaq -0x20(%rbp), %r8` before ExecuteGameplayCueOnActor):
  - `14511dd62: movq %rdi, -0x68(%rbp)`
  - `14511dd79: leaq 0x80(%rbp), %rcx`
  - `14511dd80: callq ??4FWeakObjectPtr`
  - So Instigator = params+0xA0, between Normal (kCueNormal 0x88, 0x18 bytes) and the physical material
    (kCuePhysMat 0xB8, with EffectCauser and SourceObject between).
- The capture: fx_c136.cpp detour_c136_cue_local does `C136Call c{C136Point::CueNonReplicated}; c.tag = tag;
  c.owner = target; c.cue_params = parameters; const uint32_t id = c136_fx_enter(c);`.
  - PlayFootStepCue reaches it with no capture point open, so on the host it is an outermost event.
  - c136_instigator returns `g_c136_cue_instigator(c.cue_params)`, the pawn.
- The host queue (fx_c136_net.cpp c136_net_queue): a footstep cue passes every filter.
  - `local_only`: `if (owner != nullptr || instigator != nullptr) return false;`. The owner is the ground actor.
  - `trail_local`: `c.point == C136Point::NiagaraActivate || is_stop(c.point)`. It is neither.
  - `no_asset`: `c.asset == nullptr && c.tag == 0 && ...`. The tag is set.
  - `fp_view`: no attach and no component, so None.
  - The row then carries `p.at = read_vec3(c.cue_params, kCueLocation); p.rot = read_vec3(c.cue_params, kCueNormal);`
    and the physical material.
- The send filter is filter_hits, "by instigator for every capture kind when the event has one".
  - The host's own pawn's footstep goes to the client.
  - The server copy's footstep is not sent to its own client (`f.instigator == pawn`).
- The client plays the row with client_play_cue, then `g_orig_c136_cue_local(target, tag, params)`. That runs the
  cue notify, then PlaySoundAtLocation (grenades.cpp:1172), unblocked while playing, so c162_call_atten runs.
- No double: the client's simulated proxy fails the 0.02 input gate (0x1451e0e74), so it plays no footstep of its
  own.
  - If it ever did, c136_fx_enter blocks the client's outermost call unless `c136_client_own`. The instigator is the
    host's pawn and not the client's, so filter_hits is false, and the block covers the sound inside it.
  - The proxy is not touched (1c is not built).

### Changes
- player_c99.cpp detour_c99_spawn_sound_2d: `c160_copy_effect(kC160Sound, ctx, nullptr, sound)`. It returns nullptr
  on a skip.
- hooks_ubw.cpp:1828: the SpawnSound2D row loses `&g_c99_log`, so it is always installed.
- fx_c136.cpp c162_remote_atten:
  - A remote sound with an attenuation of its own goes to c173_own_atten.
  - That function reads bSpatialize from the asset's FSoundAttenuationSettings (c173_spatialize) and gives the card
    162 fallback to an own attenuation with bSpatialize 0.
  - It logs one capped line per sound per second (pl_once, cap 80).
- playlog.cpp pl_listener_uu, declared in hooks_internal.h: the distance from the own pawn.

### Predictions (c173-r1, written before the run)
1. Client, host walk at about 7000 uu: "card173: client t=… remote sound HumanMovementSurface*|MS_FLY_PlayerMovement_*
   … ≥5000 uu from the listener …: own attenuation ATT_FLY_Player bSpatialize <0|1>, plays with …". bSpatialize 1
   plays with "its own"; 0 plays with ATT_FLY_Trooper_Footsteps.
2. Host, client walk at about 7000 uu: the same line from the host. This is the server copy's footsteps through the
   card 019 mirror.
3. A remote jump at 5000 uu or more, each side: "card162: <side> t=… remote sound HumanMovementJump_MS … plays with
   ATT_FLY_Trooper_Footsteps".
4. The client fires the Morita:
   - The host logs "card160: host t=… skipped server copy 2D sound MSS_FOL_Low_Ammo_Click".
   - The host has no "playlog: host … audio MSS_FOL_Low_Ammo_Click … 2D" line.
   - The client's own click is not shown in the log: the client has no log line for its own 2D sounds, and
     c160_copy_effect returns false on the client.
5. No doubled footstep on the client. This holds statically (1a), and the client log has no card173 line whose
   sound comes from a blocked call.

## Report

### Files changed
- mod/src/ubw/player_c99.cpp: detour_c99_spawn_sound_2d calls `c160_copy_effect(kC160Sound, ctx, nullptr, sound)` and returns nullptr on a skip.
- mod/src/ubw/hooks_ubw.cpp:1828: the SpawnSound2D row loses `&g_c99_log`, so it is always installed.
- mod/src/ubw/fx_c136.cpp:
  - c162_remote_atten sends a remote sound that has its own attenuation to the new c173_own_atten.
  - c173_spatialize reads bSpatialize from the attenuation asset.
  - The card 162 fallback is given only when bSpatialize is 0.
  - One capped line is logged per sound per second (cap 80).
- mod/src/ubw/playlog.cpp: pl_listener_uu, the distance from the own pawn. It is declared in mod/src/ubw/hooks_internal.h.
- tools/scenarios/c173-footsteps.txt (new).
- docs/cards/173-remote-footsteps-2d-rest.md: the code step, the predictions and this report.

### What was done
- 1a: yes. Card 136 already carries the host's footstep cue to the client, so 1b was built (the quoted lines are under "1a" above):
  - The host queues ExecuteGameplayCue_NonReplicated as a row, filtered by instigator.
  - The client plays the row through client_play_cue, then g_orig_c136_cue_local, then the cue notify, then PlaySoundAtLocation.
- The proxy is untouched. There is no second relay.
- The footsteps were already played, but with their own ATT_FLY_Player, and that has bSpatialize 0. So they played 2D. The fix gives such sounds the card 162 fallback, ATT_FLY_Trooper_Footsteps (bSpatialize 1, FalloffDistance 7500).
- The run logged bSpatialize 0, so the fallback is the answer, as the approval required.

### Verification
- Build: "[64/64] Linking CXX shared library dwmapi.dll", 0 warnings. DLL 4420334cb7b8a3d1 ("deployed: 4420334cb7b8a3d1", "lane 1: dll 4420334cb7b8a3d1, build 4420334cb7b8a3d1, port 7777").
- "run c173-r1: ended by scenario after 72 s; host rc 0, client rc 0".
  - host: 3449 lines, FATAL/ERROR 0, WARN 1.
  - client: 3669 lines, FATAL/ERROR 0, WARN 1.
  - "new crash dumps: none".
- Prediction 1, the client hears the host's walk: shown. The value is bSpatialize 0, so the footsteps get the fallback.
  - "card173: client t=84817090 remote sound MS_FLY_PlayerMovement_Run_Stone from no owner at (13549, 13547, 175), 7300 uu from the listener, 98 uu from the remote pawn: own attenuation ATT_FLY_Player bSpatialize 0, plays with ATT_FLY_Trooper_Footsteps"
  - "card173: client t=84817705 remote sound HumanMovementSurfaceDirt_MS ... 7479 uu from the listener ... bSpatialize 0, plays with ATT_FLY_Trooper_Footsteps"
  - HumanMovementLand_MS appears at 7261 and 7526 uu.
- Prediction 2, the host hears the server copy's footsteps (change 2, the open host side): shown. There are 24 host card173 lines.
  - "card173: host t=84829809 remote sound HumanMovementSurfaceDirt_MS from no owner at (6558, 11505, 0), 7496 uu from the listener, 97 uu from the remote pawn: own attenuation ATT_FLY_Player bSpatialize 0, plays with ATT_FLY_Trooper_Footsteps"
  - Earlier lines are at 8710 and 12743 uu, during the client's walk to the pickup.
- Prediction 3, the remote jump: shown on both sides.
  - Client: "card162: client t=84818082 remote sound HumanMovementJump_MS from no owner at (13538, 14184, 136), 29 uu from the remote pawn: no attenuation, plays with ATT_FLY_Trooper_Footsteps". The card162 line prints no listener distance. The players were about 7000 uu apart by position: the client was at the pickup near (6500, 11500).
  - Host: "card162: host t=84831011 remote sound HumanMovementJump_MS from BP_PlayerCharacterMI_C at (6485, 12075, 97), -1 uu from the remote pawn: no attenuation, plays with ATT_FLY_Trooper_Footsteps".
  - Fallback load, both sides: "card162: host|client attenuation ATT_FLY_Trooper_Footsteps loaded: bAttenuate 1 bSpatialize 1 FalloffDistance 7500".
- Prediction 4, the low-ammo click: not shown. Drive line 30 was TRY-FAIL: "try host wait log card160: ... MSS_FOL_Low_Ammo_Click since-mark timeout=10 -> TRY-FAIL timeout after 10 s".
  - The logged cause is that the click never ran in this run.
  - The host log has no Low_Ammo line on any path: no card160 skip, and no "playlog ... audio MSS_FOL_Low_Ammo_Click" line.
  - The host log has no SpawnSound2D call from a server copy's fire ability. The card099 and card160 logging was live; it logged "card160: host t=84816350 skipped server copy 2D sound WeaponSwap_MS" in the same run.
  - The client fired 2 rounds (host: MoritaMK1Fire_MS at 84818397 and 84819493). In c162-r3 the click (host line 3720, t=82680879) came after a longer burst.
  - The skip path itself is proven by WeaponSwap_MS through the same c160_copy_effect call. The SpawnSound2D gate removal is live.
  - No second run was started.
- Prediction 5, no doubled footstep on the client: holds.
  - The client total is "engine cue copies 0 dropped, 0 kept (own)". No local cue copy ran beside the relayed rows.
  - "28 rows, 1 replayed, 20 played ... misses: 0 asset, 0 key, 1 target".
  - Every client card173 line is "from no owner" at the remote pawn's position (96 to 99 uu), which is the relayed-row shape.
- WARN classes compared with c170-r1:
  - "WARN attach: DisableThreadLibraryCalls failed, err=126" is the baseline, on both sides.
  - "WARN config: UBWCOOP_GOD not "1"" is in c170-r1 and absent here. That is a difference in the run setup (god mode), not in the code.
  - There is no new WARN class.
- drive: "dead time: sleeps 2.0 s, timed-out try waits 10.0 s in 1 (line 30 10 s)", "scenario passed".

### Out of scope
- Remote fire is also 2D on its own attenuation. The new path now gives it the fallback:
  - "card173: host t=84818397 remote sound MoritaMK1Fire_MS from BP_Weapon_MoritaMk1_C at (6544, 11487, 162), 7502 uu from the listener, -1 uu from the remote pawn: own attenuation ATT_WPN_Player_Fire bSpatialize 0, plays with ATT_WPN_MoritaMK1"
  - The fallback loads as "ATT_WPN_MoritaMK1 loaded: bAttenuate 1 bSpatialize 1 FalloffDistance 30000".
  - This contradicts card 162's report that the remote fire "is 3D by its own attenuation". That report saw the attenuation's name, not bSpatialize.
- The server copy's MS_FLY_Weapon_Drop_Generic on the host prints "-1 uu from the remote pawn" (an owner match). This is cosmetic.
- symbols.txt:153 comment: as noted above, not corrected.

Report complete.

## Senior review (2026-11-11)
CLOSED, accepted. Diff matched the allowed files; rebuild gave DLL `4420334cb7b8a3d1` (build and deployed). c173-r1 rc 0/0: host 24 and client 4 `card173:` lines, every one "own attenuation ... bSpatialize 0" with the fallback; no new WARN class. The low-ammo click skip is built but was not reached in the run; its proof rides card 175's run.
