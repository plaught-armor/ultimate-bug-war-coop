# Card 176: Server-copy weapon sounds through UAudioComponent::PlayInternal play 2D on the host

Status: queued 2026-11-11, from run c175-r2. Budget: three runs. One goal: the host does not hear, as its own 2D
sound, a 2D sound that a client's server copy plays through an audio component.

Read: card 160 (`c160_copy_effect`, the one skip for server-copy 2D effects), card 173 (its SpawnSound2D skip does not
cover this path), card 155 (`detour_pl_play_internal`).

## Why

c175-r2 host, three lines during the client's burst:
"playlog: host t=86827018 audio MSS_FOL_Low_Ammo_Click from BP_Weapon_MoritaMk1_C at (0, 0, 0) 17791 uu, 2D,
attenuation null". The weapon belongs to the client's server copy. The sound has no attenuation and plays 2D, so the
host hears the client's low-ammo click at full volume as if it were its own. Card 173 put the skip on
`UGameplayStatics::SpawnSound2D`; the click does not go there.

## Senior trace (verify each line before code; mark verified or open in a Trace section)

- Entry: `UAudioComponent::PlayInternal`, hooked ungated as `detour_pl_play_internal` (playlog.cpp:757). It already
  resolves the component's owner and tests `pl_remote(owner)` or `pl_remote(owner's owner)`, and reads the sound, the
  attenuation and bAllowSpatialization for the log line.
- The skip belongs to the card 160 manager: `c160_copy_effect(kC160Sound, ctx, owner, sound)` (player_c99.cpp:137,
  callers at 484, 499, 513). Open: what `c160_copy_effect` needs as ctx and owner to decide "server copy on the host",
  and whether it already tells a 2D sound from a 3D one or the caller must.
- Open: which sounds a server copy plays 2D through a component, from the asset list and Blueprint bytecode (weapon
  Blueprints and the player Blueprint), not only the click. The card covers the class; the Report lists covered and
  not covered.
- Open: the second argument of PlayInternal (`const PlayInternalRequestData&`). It arrives by reference, so a skipped
  call should own nothing; confirm from the disassembly that the caller keeps ownership and that no caller reads a
  result the callee would have set (bIsActive, a play ID) in a way that breaks when the call is skipped.
- Open: the client's own click still plays on the client (client-own weapon, local).

Stop and report the trace before code if any of these is false: the click reaches `detour_pl_play_internal`; the skip
can be decided from the owner chain alone; skipping PlayInternal leaks nothing.

## Do

1. Write the Trace section. Send it to the senior and wait for the go-ahead only if a stop condition is hit.
2. In `detour_pl_play_internal`, on the host, a server-copy-owned component about to play a sound 2D (no attenuation
   in effect, or not spatialized) asks `c160_copy_effect` and does not call the original on a skip. A 3D sound is
   untouched. One capped log line per skip, in the card 160 style. No second manager.
3. One run, `tools/scenarios/c175-flare-fire.txt` extended (or a copy `c176-click.txt`), carrying the open proofs
   below. Every state step waits on its log line; no timed-out try waits remain (line 53 of the old scenario waits
   for a line that can never come: fix it).

## Predictions (write the exact expected lines here before the run)

Changed after the senior's mute decision (Trace item 6). Run with tools/scenarios/c176-click.txt.

- Host, once: "playlog: host reflected VolumeMultiplier on <class>: at +0x448".
- Host: one mute line per server-copy click while the line cap allows, e.g. "card160: host t=... muted server copy
  2D component sound MSS_FOL_Low_Ammo_Click: context <component> (AudioComponent), copy BP_PlayerCharacterMI_C;
  component <component>, bAutoDestroy 1, earlier muted auto-destroy components live <n> of <m>". The scenario waits
  for it after the client's 4500 ms burst.
- Host: the playlog audio line for the click may still appear (it is deduplicated per 2 s), and when it does it ends
  ", volume 0.00": "playlog: host t=... audio MSS_FOL_Low_Ammo_Click from BP_Weapon_MoritaMk1_C ... 2D, attenuation
  null, volume 0.00".
- Host: the components do not pile up. Over the burst, <n> on the mute lines stays small (0 to 2, a click being a
  short sound), not rising with each click toward 16. At the next map line or session end, if one is logged:
  "card160: map: ... muted <k> 2D component sounds (<n> muted auto-destroy components still live)".
- Host: no mute line for a 3D play, and no mute line in the window of the host's own 6000 ms burst (its own clicks
  play). This is shown by absence only: the host's own component sounds write no line.
- Client: its own click plays. Shown by code only: c176_mute_copy_2d returns before any change on a client
  (g_client_session), and the client's PlayInternal detour is otherwise unchanged. No client line names a mute.
- Carried from card 175: at least one of the 8 short presses (60 to 120 ms, before the flare) gives "card175: host
  t=... release of BP_PlayerCharacterMI_C held: the copy fired <a> of the client's <b> rounds", then "card175: host
  t=... held release of BP_PlayerCharacterMI_C done: ..." or "... ended short by the bound: ...". If none of the 8
  leaves the copy short, that is reported, and no run is added for it.
- Carried from card 174: not shown. No known route gives a second charge in the same run (the charge after the
  strike rose only from kills, "10000 0" in c175-r2 host line 3257). The scenario has one throw, with the client's
  "card169: client ... spawned BP_FlareVisuals_C" and the host's held visual.
- WARN classes: only "WARN attach: DisableThreadLibraryCalls failed, err=126" on each side. (c175-r2 had only this one.)
- drive.py: no timed-out try waits (the scenario has no try waits).

### Run 3 predictions (after the review fixes: own cap, lazy note)

Written before run c176-r3, DLL 42c0cfeed1204098. The scenario now ends with `host travel next`, so the host's
c99_arm_map prints the full count on the new map.
- Host: at most 8 "card160: host t=... muted server copy 2D component sound MSS_FOL_Low_Ammo_Click" lines, then once
  "diag: card176 mutes ... further events not logged" if the burst muted more than 8.
- Host: the WeaponSwap_MS skip lines are still written ("card160: host t=... skipped server copy 2D sound
  WeaponSwap_MS"), because the skips have their own cap of 60 again.
- Host, after the travel: "card160: map: host skipped server copy effects: ... muted <k> 2D component sounds" with <k>
  equal to all muted plays: 8 written lines plus the uncapped rest, so <k> > 8 when the cap line is present.

## Done when

- Trace, change, run lines quoted from the logs, the covered and not-covered sound list, the skipped-original
  justification, dead time with no timed-out waits. The Report ends with "Report complete."

## Files

- May change: `mod/src/ubw/playlog.cpp`, `mod/src/ubw/player_c99.cpp`, `mod/src/ubw/hooks_internal.h`, the scenario
  file, this card.
- May not change: anything else. `fx_c136.cpp`, `fire.cpp`, `spawn_c169.cpp` and `claim_c157.cpp` stay as accepted.
  `tools/symbols.txt` only if the trace shows a symbol is needed, and then say so in the Report.

## Trace

1. Verified. The click is created in Blueprint bytecode and never passes the hooked `UGameplayStatics::SpawnSound2D`.
   ExecuteUbergraph_BP_WeaponFireAbility offset 307 calls SpawnSound2D(self, LowAmmoTick, 1.0, 1.0, 0.0, None,
   false, true) through the kismet thunk. `UGameplayStatics::execSpawnSound2D` (exe 0x3C89D80) has the body of
   SpawnSound2D and CreateSound2D inlined: it calls GetWorldFromContextObject, GetTypedOuter(AActor),
   FCreateComponentParams, FAudioDevice::CreateComponent, SetVolumeMultiplier, SetPitchMultiplier, then a virtual
   call at +0x2f8 (Play). It never calls 0x3C6FD60 (the hooked SpawnSound2D). That is why the card 173 skip never
   fires for any Blueprint SpawnSound2D. The card 173 skip covers only native C++ callers.
2. Verified. The click reaches `detour_pl_play_internal`. The c175-r2 line "audio MSS_FOL_Low_Ammo_Click from
   BP_Weapon_MoritaMk1_C ... 2D, attenuation null" is written only by playlog.cpp:765 (the other pl_sound callers
   are the client relay rows and the grenade PlaySoundAtLocation path). `UAudioComponent::Play` (0x3A41F30+0x74)
   calls PlayInternal (0x3A42030).
3. Verified. The caller owns the request data. `UAudioComponent::Play` builds PlayInternalRequestData on its stack,
   calls PlayInternal, then destroys the struct's arrays itself (DestructItems and Free on the FQuartzGameThreadSubscriber
   arrays right after the call). Play reads nothing back. The other callers are FadeIn, PlayQueuedQuantizedInternal and
   three in UAudioCurveSourceComponent (Play, FadeIn, TickComponent). All pass a request by reference.
4. Verified, from the log. The owner chain decides the skip. The component's owner is the weapon (GetTypedOuter(AActor)
   of the ability). `pl_remote(read_ptr(owner, kActorOwner))` already matched in c175-r2, so the weapon's Owner is
   the server copy pawn. `c160_copy_effect(kC160Sound, comp, owner, sound)` resolves c99_copy_of(comp): a component
   is not an actor, so the walk goes to its outer (the weapon), then the weapon's Instigator or Owner (the copy). It
   returns false on the client by its own test. Open until the run: that the walk ends on the copy in under kC99Hops.
5. Verified. The 2D test is the engine's own: `UAudioComponent::GetAttenuationSettingsToApply` (0x3A2CFC0) returns
   null when bAllowSpatialization is off or no attenuation applies. PlayInternal calls it itself (+0x2f4). The click
   has bAllowSpatialization 0 (CreateSound2D) and attenuation null.
6. **Stop condition hit: a plain skip leaks.** CreateComponent makes the component with the weapon as its outer and
   registers it, so the weapon's owned-components set holds it. The click is created with bAutoDestroy true. An
   auto-destroy component is destroyed only when its sound finishes (PlaybackCompleted calls DestroyComponent). A
   skipped PlayInternal never starts the sound, so the component is never destroyed: one inactive UAudioComponent
   stays on the server copy's weapon per skipped click until the weapon is destroyed. Over a mission that is one per
   low-ammo shot.
   - Proposed resolution, in this card's files: on a skip, if the component's bAutoDestroy is set, call
     `UActorComponent::DestroyComponent(false)` (0x3A27C90). That is the same step the engine takes when an
     auto-destroy sound ends or fails to start. A component without bAutoDestroy belongs to the Blueprint that keeps
     it, and a skipped play leaves it inactive, as the engine's own early-outs in PlayInternal do (no world audio).
   - Needs two symbols in tools/symbols.txt: UActorComponent_DestroyComponent and
     UAudioComponent_GetAttenuationSettingsToApply. The Blueprint still calls SetFloatParameter on the returned
     component after the skip; that is a call on a destroyed but live UObject, which UE allows.
   - Not chosen: `PlaybackCompleted(bool)`. It decrements ActiveCount, which is 0 for a component that never
     started.
   - **Senior decision (supersedes the proposal and Do item 2):** no skip and no DestroyComponent. On the host, a
     server copy's 2D play is muted (VolumeMultiplier 0) before the original, and the original always runs. A 3D
     play is untouched. UActorComponent_DestroyComponent is not added.
7. Open. Other 2D component sounds a server copy plays (weapon and player Blueprints). Every one of them reaches
   PlayInternal through the same thunk, so the mute covers the class; the list goes in the Report from the run's
   mute lines.
8. Verified. PlayInternal reads the plain field. It loads VolumeMultiplier from the component at +0x448
   (0x3A424A3) while it builds the FActiveSound and passes it to FActiveSound::SetVolume (0x3A424AE). No cached copy
   is read. `UAudioComponent::SetVolumeMultiplier` (0x3A53110) writes the same +0x448 and only pushes it to the audio
   device when the component is already active (+0x96 bit 0x10), which a component about to start is not. So the
   field write through the reflected offset (pl_offset "VolumeMultiplier", expected +0x448) is enough, and
   UAudioComponent_SetVolumeMultiplier is not added.
9. Verified. A zero-volume sound still ends and still auto-destroys. Every way an active sound ends calls the
   static UAudioComponent::PlaybackCompleted (0x3A43DC0), which calls the member PlaybackCompleted(bool)
   (0x3A43BD0). The member tests bAutoDestroy (+0x428 bit 0) at 0x3A43D4F and calls DestroyComponent(false) through
   vtable+0x490. The static is reached from:
   - FAudioDevice::AddNewActiveSoundInternal +0x299 (0x392C1A9): the inaudible reject, after LocationIsAudible and
     AddSoundToStop. Also +0x40b and +0x736, the other failed starts.
   - ProcessingPendingActiveSoundStops +0x51c (0x39496CC): a sound stopped, including one stopped as inaudible.
   - RemoveActiveSound +0x19 (0x394D4A9): a sound that played to its end.
   So the muted click either plays silently to its end or is stopped as inaudible, and both paths destroy a
   bAutoDestroy component.
10. Verified, with one open edge. A reused component (bAutoDestroy 0) that is muted belongs to the server copy's
   weapon or pawn. Its outer and owner are fixed at creation, and the mute is applied again at every PlayInternal
   while the owner chain resolves to the copy. If the Blueprint sets the volume again before each Play, that is
   fine. Open: a weapon handed from a server copy to the host's own player would keep a zero volume until its
   Blueprint sets it again. No log so far shows a weapon changing hands. This is not a stop.
11. Code. `c176_mute_copy_2d` (player_c99.cpp) does the work: host only; the 2D test is
   GetAttenuationSettingsToApply returning null; then c160_copy_effect(kC160Mute, comp, owner, sound, note), which
   writes one capped line in the card 160 style: "card160: host t=... muted server copy 2D component sound <sound>:
   ... copy <copy>; component <comp>, bAutoDestroy <0|1>, earlier muted auto-destroy components live <n> of <m>".
   The last 16 muted bAutoDestroy components are held by weak pointer, and a live count that stays bounded over a
   burst shows they do not pile up. The playlog audio line now ends ", volume <v>" for component plays. The map line
   adds "muted <n> 2D component sounds (<n> muted auto-destroy components still live)". Symbol added:
   UAudioComponent_GetAttenuationSettingsToApply (tools/symbols.txt; mod/src/offsets.generated.h regenerated by
   tools/pdb_rva.py gen).

## Report

### Files changed

- mod/src/ubw/player_c99.cpp:
  - New C160 kind kC160Mute ("2D component sound") and a verb table, so the card 160 line says "muted" for it and
    "skipped" for the others as before. c160_copy_effect takes an optional note that is appended to its line.
  - c176_mute_copy_2d. On the host, for a 2D play (GetAttenuationSettingsToApply returns null) owned by a server
    copy, it writes VolumeMultiplier = 0 through the reflected offset and logs one capped card 160 line with the
    component's bAutoDestroy. It keeps a ring of the last 16 muted bAutoDestroy components by weak pointer and
    reports how many are still live.
  - The GetAttenuationSettingsToApply address is resolved once with code_address. If it does not resolve, one WARN
    is logged and nothing is muted.
  - The card160 map line also counts mutes and the live components.
- mod/src/ubw/playlog.cpp:
  - detour_pl_play_internal resolves Sound and VolumeMultiplier for every play and calls c176_mute_copy_2d before
    the original. The original always runs.
  - The playlog audio line ends ", volume <v>" for component plays.
  - PlState.volume_at is new.
- mod/src/ubw/hooks_internal.h: the c176_mute_copy_2d declaration, and a defaulted volume parameter on pl_sound. The
  other pl_sound callers are unchanged.
- tools/symbols.txt: UAudioComponent_GetAttenuationSettingsToApply (0x3A2CFC0), needed by Trace item 5.
  mod/src/offsets.generated.h was regenerated from it by `tools/pdb_rva.py gen`. No other symbol was added:
  SetVolumeMultiplier is not needed (Trace item 8), and DestroyComponent is not added.
- tools/scenarios/c176-click.txt (new, from c175-flare-fire.txt): 8 short presses before the flare, the flare
  throw, the client's 4500 ms burst, and the host's own 6000 ms burst. There are no try waits.
- This card: Trace items 6 to 11, Predictions, Report.

### Summary

The host now mutes the server copy's 2D component sounds instead of skipping them. The component's life stays the
game's. Every click of the client's burst was muted, and none of the muted auto-destroy components were still live
at the next click.

- PlayInternal reads the plain field: VolumeMultiplier at +0x448, loaded at 0x3A424A3 and passed to
  FActiveSound::SetVolume at 0x3A424AE. The run confirms the offset: "playlog: menu reflected VolumeMultiplier on
  AudioComponent: at +0x448".
- Justification: a skipped PlayInternal leaves a bAutoDestroy component on the weapon forever, because only
  PlaybackCompleted destroys it (Trace item 6). A muted play still starts, then ends (to its end, or stopped as
  inaudible), and the member PlaybackCompleted destroys it (0x3A43D4F, Trace item 9). The run confirms it: 57 mute
  lines, each with "earlier muted auto-destroy components live 0 of <n>", up to "0 of 16".

Trace verdicts:
- Items 1, 2, 3, 5, 8 and 9: verified statically.
- Item 4: now verified by the run. Each mute line names "copy BP_PlayerCharacterMI_C".
- Item 6: resolved by the senior's mute decision.
- Item 10: verified, with one open edge (a weapon handed from a copy to the host's player). That edge is not seen
  in any log.
- Item 7: the run shows one class only, MSS_FOL_Low_Ammo_Click.

### Verification

Build: 0 warnings. DLL 61f6ce4bdd62f5f5 ("deployed: 61f6ce4bdd62f5f5", "lane 1: dll 61f6ce4bdd62f5f5, build
61f6ce4bdd62f5f5").

Run 1, c176-r1:
- "run c176-r1: ended by scenario after 70 s; host rc 0, client rc 0". drive rc 1.
- "FAIL line 67: host wait log card104: host t=\d+ server copy DoAction, key Flare_Throw since-mark timeout=8 ->
  timeout after 8 s".
- Cause: the 8 short presses spent the card104 line cap (kC104Lines, c104_line in anim_c104.cpp), so neither the
  DoAction line nor the ActivateFlare line was written. The throw itself happened: host line 3161 reads "card169:
  host t=88136649 held BP_FlareVisuals_C of the server copy destroyed during first person montage AM_FlareThrow at
  1.749 s", and line 3208 reads "TacCharge ... 0 0".
- The mute already worked in run 1: host line 2807 reads "card160: host t=88134153 muted server copy 2D component
  sound MSS_FOL_Low_Ammo_Click: ... bAutoDestroy 1, earlier muted auto-destroy components live 0 of 0".
- Fix: a scenario change only. The throw and the flare are now waited on through the card 169 montage line and
  "card098: host spawn BP_SupportFlare_C".

Run 2, c176-r2:
- "run c176-r2: ended by scenario after 76 s; host rc 0, client rc 0".
- "scenario c176-click.txt: drive rc 0", "scenario passed".
- "dead time: sleeps 0.0 s, timed-out try waits 0.0 s in 0 (none)".

Predictions against run 2:
- Offset: "playlog: menu reflected VolumeMultiplier on AudioComponent: at +0x448". Held (the side shows "menu"
  because the first play was in the front end).
- Mute lines, one per server-copy click: 57 lines, each with "bAutoDestroy 1". The first is "card160: host
  t=88288875 muted server copy 2D component sound MSS_FOL_Low_Ammo_Click: context AudioComponent (AudioComponent),
  copy BP_PlayerCharacterMI_C; component AudioComponent, bAutoDestroy 1, earlier muted auto-destroy components live
  0 of 0". The last is "... t=88297879 ... live 0 of 16". The 60-line cap was not reached (2 WeaponSwap skips + 57
  mutes, no "card160 skips" cap note). Held.
- The playlog line shows volume 0: "playlog: host t=88288875 audio MSS_FOL_Low_Ammo_Click from
  BP_Weapon_MoritaMk1_C at (0, 0, 0) 17791 uu, 2D, attenuation null, volume 0.00". There are 4 such lines, one per
  2 s window. Held.
- No pile-up: the live count was 0 on all 57 lines, so every earlier muted component was already destroyed by the
  next click. Held. Two limits on this proof: it depends on the card 31 weak-pointer helpers, and it cannot tell a
  silent play to the end from an inaudible stop. The map line is printed only at map load, and both map lines in
  this run came before the burst ("muted 0 2D component sounds (0 muted auto-destroy components still live)").
- Host's own burst: no mute line after the host's mark (host log line 3672) through the end of the 6000 ms burst.
  Held, by absence only. Not shown: whether the host's own magazine was low enough to click, and no line is
  written for a host-own play.
- Client's own click: not shown by a line. Shown by code only: c176_mute_copy_2d returns at g_client_session, and
  the client log has no mute line.
- Card 175: not shown. None of the 8 short presses (60 to 120 ms) left the copy short. All 8 ended with a normal
  "card024: host relay release (Fire 0)", and the host log has 0 card175 lines. No run was added for it, as the
  card says.
- Card 174 second flare: not shown. There is no known route to a second charge (after the strike the charge rose
  only to "10000 0"). The one throw: client "card174: ... not sent ... (1 skipped)" and "card169: client ...
  spawned BP_FlareVisuals_C"; on the host, the card 169 montage line and "card098: host spawn BP_SupportFlare_C".
- WARN classes:
  - Client: only "WARN attach: DisableThreadLibraryCalls failed, err=126". Held.
  - Host: that line and one new one: "WARN card157: host t=88299520 HitClaim shot 58 from pc BP_PlayerController_C
    (miss) found no round in 1500 ms: dropped (1 so far)". The client sent shot 58 at 88298009 ("card157: client
    t=88298009 HitClaim shot 58 miss Landscape ... sent (58 sent, 0 on NPCs)"). The host released the relay at
    88297981 with a plain "card024: host relay release (Fire 0)", which is before shot 58 and has no card175 hold
    line. See Out of scope.
- Counts: host 3989 lines, FATAL/ERROR 0, WARN 2. Client 4298 lines, FATAL/ERROR 0, WARN 1. "new crash dumps:
  none".

### Sound classes covered and not covered

Covered: every 2D play of a UAudioComponent that goes through UAudioComponent::PlayInternal on the host and whose
component (or the component's owner) resolves to a server copy:
- Blueprint SpawnSound2D and CreateSound2D, through the inlined kismet thunk;
- Play on a reused component;
- FadeIn and quantized plays, which reach PlayInternal too.

Seen in the run: MSS_FOL_Low_Ammo_Click only.

Not covered:
- 3D plays: untouched, by design.
- 2D sounds that do not create a component: PlaySound2D, and ADAudioManager::PlaySound2D. These stay card 160 skips
  ("skipped server copy 2D sound WeaponSwap_MS" twice in this run).
- A component whose outer and owner both fail to resolve to the copy within kC99Hops. None was seen.
- A sound already playing when its owner became a copy. The mute applies only at PlayInternal.

### Out of scope

- Card 173's SpawnSound2D skip (detour_c99_spawn_sound_2d on 0x3C6FD60) covers only native C++ callers. A Blueprint
  SpawnSound2D goes through execSpawnSound2D (0x3C89D80), which has SpawnSound2D and CreateSound2D inlined and never
  calls the hooked function (Trace item 1).
- The card104 line cap (kC104Lines) is spent by ordinary fire animation lines early in a run. After that, the
  DoAction and ActivateFlare proof lines that later scenarios wait on are not written (c176-r1).
- Card 175 class: the host released the client's relay (88297981) before the client's last round (shot 58 at
  88298009), and the release took the plain path, not the card 175 hold. The copy fired no round for shot 58, and
  card 157 dropped the claim: "WARN card157: host t=88299520 HitClaim shot 58 ... found no round in 1500 ms".

Report complete.

## Run 3 addendum (c176-r3, DLL 42c0cfeed1204098)

### Files changed

- mod/src/ubw/player_c99.cpp:
  - The mutes have their own cap: `kC160Cap` = {60 skips, 8 mutes}, with the cap line "card176 mutes".
  - The note is built only for a line that will be written (`C160NoteFn`, `c176_note`).
  - `c176_mute_copy_2d` runs the 2D test before `c160_copy_effect`, and reads bAutoDestroy only after the mute.
- tools/scenarios/c176-click.txt: the travel tail, and the line 80 change below.

### Shown

- Exactly 8 mute lines, all from the short presses (89089680 to 89090498), then "diag: card176 mutes ... further
  events not logged" at 89090583.
- The WeaponSwap_MS skip lines are still written (2 lines, "skipped server copy 2D sound WeaponSwap_MS").
- WARN classes, each side: baseline only ("WARN attach: DisableThreadLibraryCalls failed, err=126").
- "run c176-r3: ended by scenario after 75 s; host rc 0, client rc 0". FATAL/ERROR 0. No dumps.

### Not shown: the full count

The scenario failed at line 80, so the travel and the map line never ran: "FAIL line 80: host wait log card160:
host t=\d+ muted server copy 2D component sound MSS_FOL_Low_Ammo_Click since-mark timeout=5 -> timeout after 5 s".

The fault is the scenario, not the code. The 8 short presses play the click too, and spend the 8-line cap before the
burst. The cap behaved as written. My prediction missed that the clicks start at the short presses.

Line 80 now waits for the cap line instead. Run 4 would show the count. It is not run: run 3 was this card's third
run, and the senior decides.

Report complete.

## Senior review (2026-11-11)

Accepted. Decision at the stop: mute (VolumeMultiplier 0) and run the original, not skip, so the game destroys the
component itself. Verified by the senior: diff against the pre-176 snapshot is inside the allowed files plus one
symbol (GetAttenuationSettingsToApply); DLL 42c0cfeed1204098 built and deployed; c176-r2 57 mute lines, all "live 0
of N", 4 "volume 0.00" playlog lines; c176-r3 8 mute lines then the "card176 mutes" cap line, WeaponSwap_MS skips
still present, baseline WARN only on both sides, rc 0/0. Not shown: the map-line mute count (the c176-r3 scenario
failed its line 80 wait; the scenario is fixed and unrun) — carried to card 180's run, same scenario. Card 175 hold
and card 174 second throw stay unproven (no short press left the copy short; no known second charge); watch in the
soak. Out of scope, noted: card 173's SpawnSound2D skip covers native callers only, and Blueprint callers are now
covered by this mute; the card 104 line cap starvation went to card 149.
