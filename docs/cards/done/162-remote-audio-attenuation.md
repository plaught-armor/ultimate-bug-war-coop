# Card 162: The other player's sounds play at their pawn

Status: REPORTED, partly proven. Fix built (DLL 8dfc02cb3577e884). Runs c162-r2 (harness loss) and c162-r3 used; run budget spent. See "## Report".

Read: docs/playtest-2026-11-08.md finding 2, card 136 (effects manager), card 155 (audio line).

## Why

Each player heard the other's sounds as if they stood together, on different parts of the course.

## Do

From card 155 line 7: which sounds play 2D or at the listener. Fix in the effects manager or the relay that plays
them, so every remote sound plays at its source with its attenuation.

## Done when

- A run with the players 5000 uu apart: each remote sound line shows 3D, at the other pawn, with attenuation.
- The usual Report items (WARN diff, run length, rc, ERROR/FATAL, CRASH, dumps, DLL hash). It ends with "Report
  complete."

## From card 141 (senior, 2026-11-08)

The client plays the host's fire sounds (16 of 16 MoritaMK1Fire_MS rows "played"), but card 155's `audio` line is
missing. client_play_sound passes client_key_actor(r) to pl_sound, which is null for world-keyed rows (guid 0), since
those rows carry no shooter pawn. Before this card's trace, make the line work for those rows: put the shooter on
the row, or log the row's location against the listener. Without it this card has no data.

## Static trace (junior, 2026-11-09)

Updated 2026-11-11 from the run 1 logs.

- Client. client_play_sound (fx_c136_net.cpp) plays every relayed sound row with the row's attenuation override
  (client_setting(r.atten)), and with null when the row has none. In UE a sound that has no override and no
  USoundBase::AttenuationSettings of its own plays non-spatialized: 2D, at full volume everywhere. The run 1 rows
  show this for the host player's own sounds, for example logs/c162-r1-client.log line 2353:
  "audio row HumanMovementMantleOut_MS from a world row at (13399, 13407, 246): 20580 uu from the listener, 42 uu
  from the nearest remote pawn; attenuation override null, sound's own null". Rows of this kind were seen for
  HumanMovementMantleOut_MS, HumanMovementMantleIn_MS, MS_FLY_Grenade_Incendiary_PullPin,
  MS_FLY_Grenade_Incendiary_Throw, GrenadeThrow_MS and MS_TACT_Tactical_Flare_Throw.
- Host. The server copy of the client's pawn plays the same player MetaSounds through PlaySoundAtLocation with a
  null attenuation (earlier host logs show "audio HumanMovementJump_MS from BP_PlayerCharacterMI_C ... attenuation
  null"), so the host also hears the client's foley 2D. The server copy's rifle plays the AI twin
  MS_WPN_AI_Morita_Mk1 with ATT_WPN_MoritaMK1, which is already 3D.
- Cause: the player's own sounds carry no attenuation, because the game only ever plays them at the local listener.
  Played for the other player, they stay 2D.
- Fix (one helper in the effects manager, used on both sides): c162_remote_atten (fx_c136.cpp). A sound gets a
  fallback attenuation when all three hold: it has no override; it has no AttenuationSettings of its own (and, for a
  USoundCue, no bOverrideAttenuation); and it is a remote player's sound. A sound is a remote player's when its owner
  (or the owner's Owner) is a remote pawn. With no player owner, it is a remote player's when the nearest player pawn
  to it is a remote one within 300 uu (pl_remote_source in playlog.cpp); client world rows carry no owner.
  The fallback is the AI twin's attenuation:
  - A "Fire" sound gets /Game/Audio/Attenuation/ATT_WPN_MoritaMK1.
  - Every other sound gets /Game/Audio/Attenuation/ATT_FLY_Trooper_Footsteps.
  Each fallback is found or loaded once, registered with the game instance, and reached through a weak pointer.
  The helper is applied at these points:
  - client_play_sound, for relayed rows.
  - The PlaySoundAtLocation and SpawnSoundAtLocation detours (grenades.cpp), after c136_fx_enter, only when the call
    is not blocked.
  - The SpawnSoundAttached detour (proj_c132.cpp), with the C136_PASS body written out, so the attenuation slot is
    set only on an unblocked call.
  The host's row keeps the call's own attenuation; the client chooses the fallback itself.
- The pl_sound and pl_sound_row lines now print the attenuation the sound plays with (for a fallback, the "override"
  field names the fallback).

- Client side: every host sound reaches the client as a card 136 row (PlaySoundAtLocation, SpawnSoundAtLocation,
  SpawnSoundAttached; anim notify sounds come through these). client_play_sound plays a PlaySound row with
  PlaySoundAtLocation at the row's location. It passes the row's attenuation override, or null, and null means the
  sound's own AttenuationSettings. A sound that has no attenuation of its own and no override plays unattenuated,
  heard at full volume everywhere. That is one candidate for finding 2 on the client.
- Host side: the client's server copy runs its Blueprints on the host, including 2D sounds (PlaySound2D, the
  ADAudioManager 2D path). Those played on the host's own listener, as if the client stood next to the host. Card 160
  now skips them on the host ("card160: host ... skipped server copy 2D sound ..."; c160-r1 skipped WeaponSwap_MS
  twice). The 3D sounds of the server copy play at the copy, where they belong.
- Diag (the card 141 ask): client_play_sound now also calls pl_sound_row for every relayed row, world-keyed rows
  included. Line: "playlog: client t=... audio row <sound> from <owner|a world row>[ (attached)] at (x, y, z): D uu
  from the listener, R uu from the nearest remote pawn; attenuation override <A>, sound's own <B>".

Built: DLL 4c06669758eb18a0 (diag only); run 1 uses a758aa0b57c6361f (diag plus the card 160 and 169 items).

## Predictions, run 1 (tools/scenarios/c162-run1.txt)

The card 162 steps are last in c162-run1.txt, after the batched card 169 and card 160 steps (their predictions are
in those cards). c162-audio.txt is the same card 162 block on its own.

Players about 7300 uu apart (client at the Morita pickup 6445 11469, host at 13500 13500).

1. Host fires the Morita 1.2 s at the sky and walks about 700 uu. The client logs "audio row" lines for
   MoritaMK1Fire_MS and the footsteps, each about 7300 uu from the listener and under 200 uu from the nearest remote
   pawn. Any row with override None and sound's own None is an unattenuated sound: those are the fix list.
2. Client fires 1.2 s. The host logs "playlog: host t=... audio ... from BP_PlayerCharacterMI_C" lines (the server
   copy), 3D, about 7300 uu from the host. Any 2D one shows up as a card160 skip line instead.
3. Nothing else changes: rc 0/0, WARN diff only the known lines.

## Predictions, run 2 (tools/scenarios/c162-audio.txt alone, DLL 8dfc02cb3577e884)

The players are about 7300 uu apart: the client is at the Morita pickup (6445, 11469), the host at (13500, 13500).

1. Each side logs one "card162: <side> attenuation <ATT> loaded: bAttenuate 1 bSpatialize 1 FalloffDistance <uu>"
   line per fallback it uses, with FalloffDistance greater than 0. If bSpatialize or bAttenuate is 0, the fallback
   choice is wrong. That is a stop-and-report result, not grounds for another run.
2. Host fires: the client logs "card162: client t=... remote sound MoritaMK1Fire_MS ... plays with
   ATT_WPN_MoritaMK1", and the "audio row MoritaMK1Fire_MS" line shows about 7300 uu from the listener, under 300 uu
   from the remote pawn, and override ATT_WPN_MoritaMK1.
3. Host walks about 700 uu: the client logs "remote sound HumanMovement*_MS ... plays with
   ATT_FLY_Trooper_Footsteps" (or the foot sounds the walk plays), with audio rows about 7300 uu from the listener.
4. Client fires and walks: the host logs "card162: host t=... remote sound <sound> from BP_PlayerCharacterMI_C...
   plays with ATT_FLY_Trooper_Footsteps" for the server copy's foley, if the walk plays any.
   The server copy's rifle stays MS_WPN_AI_Morita_Mk1 with ATT_WPN_MoritaMK1, unchanged.
5. No card162 line names the local player's own sounds (an owner test returns false for the own pawn).
   rc 0 and 0. No new WARN class: the card162 lines carry no WARN prefix.

## Report

### Files changed

- mod/src/ubw/fx_c136.cpp: c162_remote_atten and c162_call_atten. They apply a fallback attenuation to a remote player's sound when the sound has none: ATT_WPN_MoritaMK1 for "Fire" sounds, ATT_FLY_Trooper_Footsteps for others. Each fallback is loaded once, registered with the game instance and held by weak pointer. Lines are capped at 60.
- mod/src/ubw/playlog.cpp: pl_remote_source. A sound counts as remote when its owner, or the owner's Owner, is the remote pawn. With no player owner, it counts as remote when the nearest remote pawn is within 300 uu and closer than the own pawn.
- mod/src/ubw/hooks_internal.h: declarations of the three new functions.
- mod/src/ubw/grenades.cpp: the PlaySoundAtLocation and SpawnSoundAtLocation detours pass c162_call_atten on unblocked calls.
- mod/src/ubw/proj_c132.cpp: the SpawnSoundAttached detour now has the C136_PASS body written out, so it sets the attenuation slot only on unblocked calls.
- mod/src/ubw/fx_c136_net.cpp: client_play_sound plays relayed rows with c162_remote_atten. The audio row lines print the effective attenuation.
- tools/scenarios/c162-audio.txt: run 2 header, card162 try waits, and the client goto.
- docs/cards/162-remote-audio-attenuation.md: trace, predictions and this report.

### Summary

**Cause.** The player's own foley carries no attenuation at all, neither an override nor USoundBase::AttenuationSettings (at +0x118). When the sound is played for the other player, UE plays it 2D at full volume. Run 1 line: "audio row HumanMovementMantleOut_MS from a world row at (13399, 13407, 246): 20580 uu from the listener, 42 uu from the nearest remote pawn; attenuation override null, sound's own null".

**Fix.** At every sound play point, on both sides, a remote player's sound with no attenuation gets the AI twin's attenuation. The fix is in the card 136 effects manager. There is no new relay kind.

**Prediction 2 was wrong.** The host's fire sound already carries its own attenuation and is 3D, so it needs no fallback. Line: "audio row MoritaMK1Fire_MS from a world row at (13491, 13525, 337): 7242 uu from the listener, 70 uu from the nearest remote pawn; attenuation override null, sound's own ATT_WPN_Player_Fire".

**Not done or not shown:**
- Walking footsteps were not relayed or played through these paths during either player's walk. Neither log has a footstep sound line in the walk windows. Lines 36 and 45 are TRY-FAIL.
- The client fallback lines (jump, mantle, weapon drop) happened with the players close together: 80 to 192 uu from the listener. So the fallback is not shown at 5000 uu or more.
- The footstep path, if footsteps play at all, is untraced.

**Run 2 lost to the harness, not the code.** The host's steamrt4 update ("INFO: Updating steamrt4 to 4.0.20261110.268882... INFO: Downloading steamrt4 (4.0.20261110.268882), please wait...") delayed the host attach about 85 s. That tripped the 90 s liveness check: "run c162-r2: ended by no-log after 106 s; host rc 247, client rc 0". FATAL/ERROR 0, no dumps.

### Verification

Build: 0 warnings. DLL 8dfc02cb3577e884 ("lane 1: dll 8dfc02cb3577e884, build 8dfc02cb3577e884").

Run c162-r3: "run c162-r3: ended by scenario after 112 s; host rc 0, client rc 0".

The fallback assets load as 3D on both sides:
- "card162: host attenuation ATT_FLY_Trooper_Footsteps loaded: bAttenuate 1 bSpatialize 1 FalloffDistance 7500"
- "card162: client attenuation ATT_FLY_Trooper_Footsteps loaded: bAttenuate 1 bSpatialize 1 FalloffDistance 7500"

Client, remote foley now attenuated:
- "card162: client t=82660266 remote sound HumanMovementJump_MS from no owner at (6546, 11381, 97), 27 uu from the remote pawn: no attenuation, plays with ATT_FLY_Trooper_Footsteps"
- "audio row HumanMovementJump_MS from a world row at (6546, 11381, 97): 80 uu from the listener, 27 uu from the nearest remote pawn; attenuation override ATT_FLY_Trooper_Footsteps, sound's own null"
- Also HumanMovementMantleIn_MS (41 uu from the remote pawn) and MS_FLY_Weapon_Drop_Generic (0 uu).

Client, remote fire at distance is 3D by its own attenuation: "MoritaMK1Fire_MS ... 7242 uu from the listener, 70 uu from the nearest remote pawn; attenuation override null, sound's own ATT_WPN_Player_Fire".

Host, server-copy foley now attenuated (owner match; it prints -1 uu because no distance is computed on an owner match):
- "card162: host t=82633764 remote sound MS_FLY_Weapon_Drop_Generic from BP_Weapon_MoritaMk1_C at (6554, 11461, 97), -1 uu from the remote pawn: no attenuation, plays with ATT_FLY_Trooper_Footsteps"

Host, server-copy fire at distance is 3D: "audio MS_WPN_AI_Morita_Mk1 from BP_PlayerCharacterMI_C at (6554, 11461, 97) 7466 uu, 3D, attenuation ATT_WPN_MoritaMK1".

No card162 line names a local player's own sound (prediction 5 holds).

drive.py:
- "scenario c162-audio.txt: drive rc 0", "scenario passed".
- "dead time: sleeps 13.0 s, timed-out try waits 15.2 s in 3 (line 45 5 s, line 36 5 s, line 34 5 s)".
- Lines 34, 36 and 45 are TRY-FAIL, for the reasons given above.

Counts:
- host: 4278 lines, FATAL/ERROR 0, WARN 2.
- client: 3927 lines, FATAL/ERROR 0, WARN 2.
- CRASH 0. "new crash dumps: none".

WARN classes (both sides, known baseline only):
- "WARN attach: DisableThreadLibraryCalls failed, err=126"
- "WARN config: UBWCOOP_GOD not "1""

There is no new WARN class.

### Done when, status

- 3D with attenuation at the other pawn:
  - Proven for remote fire on both sides at about 7300 uu, by the sounds' own attenuation.
  - Proven for remote foley on both sides, by the fallback (bSpatialize 1, FalloffDistance 7500).
  - The foley lines were logged at close range, not at 5000 uu.
- Walking footsteps: not shown, because no footstep sound line appeared in either walk.

### Out of scope

- Host: "audio MSS_FOL_Low_Ammo_Click from BP_Weapon_MoritaMk1_C at (0, 0, 0) 19591 uu, 2D, attenuation null". The server copy's weapon plays a 2D low-ammo click on the host (card 160 class).
- Walking footsteps produce no sound line on any hooked path, on either side. Their play path is untraced.

Report complete.
