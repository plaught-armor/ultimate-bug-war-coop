# Card 158: The client's grenade shows in its hand and flies where it aimed

Status: CLOSED, outcome in docs/CARDLOG.md. Was: queued 2026-11-08, from docs/playtest-2026-11-08.md finding 13. Budget: three runs.

Read: docs/playtest-2026-11-08.md finding 13, card 073 (grenade grant), card 155 (throw aim line).

## Why

The client's GA_FragGrenadeThrowingAbility_C holds SourceItem (+0x3B8) with quantity 0 (t=259063781); no grenade
shows in the hand. The flying grenade is only the host's copy, thrown 1.15 s after the press from the server pawn's
aim.

## Do

1. Static first: where the held mesh comes from (SourceItem or the montage notify) and why the client's is empty;
   which aim the server throw uses.
2. Fix: the client's held grenade shows; the throw starts from the client's aim sent with the activation; the host's
   flying copy uses it. Sweep the other grenade types.

## Done when

- A run: the client's throw shows the held mesh (component visible during the montage), and the host's launch
  rotation is within 2 degrees of the client's aim at the press.
- The usual Report items (WARN diff, run length, rc, ERROR/FATAL, CRASH, dumps, DLL hash). It ends with "Report
  complete."

## Static trace (junior, 2026-11-09)

### The held grenade

The held mesh does not come from SourceItem or from a montage notify. It is an actor that GA_GrenadeThrowAbility_C
(the Blueprint parent of GA_FragGrenadeThrowingAbility_C and GA_IncendiaryGrenadeThrowingAbility_C) spawns. Those two
child classes have 2 exports each and override no function.

- OnAnimationStarted (ubergraph 0x422) runs in this order:
  1. casts the owner to BP_Player_Character_MI and sets PlayerSkeletalMesh = FirstPersonMesh;
  2. creates an AbilityTask SpawnActor with GrenadeVisualClassRef and binds its Success and DidNotSpawn delegates to
     Success_C2957362411E56145C3907B3345B407B and DidNotSpawn_C2957...;
  3. calls BeginSpawningActor, and calls FinishSpawningActor only when that returns true.
- UAbilityTask_SpawnActor::BeginSpawningActor spawns only when `IsNetAuthority()`. On the client it broadcasts
  DidNotSpawn and returns false.
- DidNotSpawn only stores a temp. Success stores the actor in GrenadeVisuals, sets its location to
  GetSocketLocation(SocketToBeAttachedWith) and its rotation to SpawnRotation, then calls
  AttachToComponent(PlayerSkeletalMesh, SocketToBeAttachedWith, 2, 2, 2, true).
- OnDoAction and K2_OnEndAbility destroy GrenadeVisuals when it is valid.

So the client's own throw never has a held grenade. The server copy spawns one, attached to the server copy's first
person mesh, which nobody sees. SourceItem's quantity 0 plays no part in this. Native OnDoAction_Implementation reads
SourceItem only to call RemoveItemByDefinition, and it never runs on the client (card 074).

OnAnimationStarted is a BlueprintNativeEvent of USTInventoryItemGameplayAbility (exec thunks `execOnAnimationStarted`
and `execOnAnimationStarted_Implementation`). The PlayAnimation task's delegate reaches it through ProcessEvent, with
the Blueprint's UFunction.

### The throw's aim

USTGrenadeThrowGameplayAbility::OnDoAction_Implementation (exe+0x5163290) builds the launch transform like this:

- Rotation: the owner pawn's Controller (+0x2D8). When that is an APlayerController, the rotation comes from vtable
  +0x828, `GetPlayerViewPoint(&loc, &rot)`. The fallback is the root component's rotation. The ability's LaunchAngle
  float (+0x3E8) is then added to the pitch.
- Translation: GetSocketLocation(SocketToBeAttachedWith +0x3D8) on the pawn's skeletal mesh default subobject
  (vtable +0x508).
- Both go to FireProjectile.

On the listen server, the client's PlayerController view point is the client's own camera. The client sends it with
ServerUpdateCamera; when the camera cache is empty, the fallback is the control rotation that ServerMove replicates.

The throw takes its aim at the montage's DoAction notify, not at the press. That is also true in single player.

Measured in run c141b (stationary aim):

| | Pitch | Yaw |
|---|---|---|
| Client at the press (t=265870348) | 1.23 | 90.28 |
| Host launch velocity (-14, 2982, 326), 1.10 s later | 6.24 | 90.27 |

The yaw matches to 0.01 degrees. The pitch difference is 5.01 degrees, which is LaunchAngle; a host's own throw gets
the same offset.

So the server throw already uses the client's aim, with no network change needed. The 1.1 s is the windup: the
DoAction notify comes about 1 s into the montage on both sides. Playtest finding 13's "does not fly where aimed"
matches three things: no held grenade, the copy appearing only after the windup plus one trip, and the designed 5
degree lob. Its log has no press-aim line to compare against (card 155 added that line later).

## Fix design

Client only. This is a ProcessEvent hook, gated by `g_c158_pe` and set once a grenade ability spec binds (c73_bind):

1. At bind, resolve the ability's `OnAnimationStarted` and `Success_C2957...` UFunctions by FindFunction. Keep up to
   4 pairs, so any override in another class still matches.
2. After ProcessEvent of a recorded OnAnimationStarted on a client-own avatar's ability, if GrenadeVisuals is null:
   - spawn GrenadeVisualClassRef at the avatar (AdjustIfPossibleButAlwaysSpawn);
   - ProcessEvent Success with {SpawnedActor}.

   The Blueprint's own Success path then sets GrenadeVisuals, places the actor and attaches it. Its own OnDoAction and
   EndAbility destroy it. There is no new lifetime code.
3. Log one line per spawn (capped): the class, GrenadeVisuals set (yes/no), the attach parent and the actor hidden
   flag. Log a second line when OnDoAction ends it (the visual was destroyed, and how long it was held).

Aim: no code change, per the trace above. The run measures it instead. The client's press aim line (playlog) is
compared with the host's launch rotation from the launch velocity, minus LaunchAngle (logged once on the host).

Sweep: frag and incendiary share the parent's functions, so one pair covers both. Other abilities whose Blueprint
spawns a visual through AbilityTask SpawnActor would have the same client gap. That sweep is listed in the run report
as an open item, not fixed here. The host's own held grenade is unchanged, and the host never sees a client's first
person mesh.

## Run c158-r1 predictions (DLL ee3d440fb38d03c8)

Scenario tools/scenarios/c158-held.txt, one FAST=1 pair run on FED_C1M1. The client stands still, takes the frag and
incendiary pickup set, and throws each at a fixed sky point. A client shot is taken about 0.5 s after each G.

1. Client: one `card158: client <ability>: OnAnimationStarted ..., Success ... (SpawnedActor at N)` line per grenade
   ability class, with no WARN and N >= 0.
2. Client, per throw: `card158: client ... held grenade ... spawned and set as GrenadeVisuals; attached to <component>
   (BP_PlayerCharacterMI_C...)`, root visible 1, actor hidden 0. No `WARN card158 ... not spawned` line.
3. Client, per throw: `card158: client ... OnDoAction: aim P p Y y; held grenade shown for t ms; GrenadeVisuals
   cleared`, with no WARN and t between 200 and 2000.
4. Host, per throw: `card158: host ... server copy throw ...: aim P p' Y y'`, with |p' - p| and |y' - y| at most 2
   degrees against that throw's client line (pitch is compared modulo 360).
5. The client shots at +0.5 s show the grenade in the first-person hand.
6. Both logs: no new WARN classes against the c163-r3m baseline, and no crash.

## Run c158-r1 results (71 s, rc 0/0, no crash dumps, FATAL/ERROR 0)

- 1 held: one bind line, GA_FragGrenadeThrowingAbility_C, SpawnedActor at 0, no WARN. The incendiary class shares the
  parent's functions (no second bind line), as traced.
- 2 held: frag BP_GrenadeVisuals_C and incendiary BP_IncendiaryGrenadeVisuals_C were spawned, set as GrenadeVisuals and
  attached to FirstPersonMesh (BP_PlayerCharacterMI_C), root visible 1, actor hidden 0.
- 3 partly: the frag showed for 1094 ms and the incendiary for 616 ms. Both lines were WARN because GrenadeVisuals was
  still set after OnDoAction. The Blueprint's DestroyActor does not null the variable; r2 checks with a shot after the
  throw.
- 4 held exactly: frag client P 1.14 Y 118.56, host P 1.14 Y 118.56. Incendiary client P 1.99 Y 116.14, host P 1.99
  Y 116.14. LaunchAngle is 5.00.
- 5: the frag-held shot shows the frag in the first-person hand. The inc-held shot (taken 0.3 s after the spawn line)
  shows an empty hand, most likely after the 616 ms throw. The frag is not lingering in it.
- 6 held: WARN only attach and GOD config, same as the baseline.

## Run c158-r2 predictions (DLL b88f10cdb272213a)

The run is the same, with the shot taken right at the spawn line and a second shot 0.4 s after OnDoAction.

- The held shots show the frag and the incendiary in the hand.
- The after shots show an empty hand (the Blueprint destroyed its held actor).
- The OnDoAction lines carry no WARN.
- Prediction 4 again holds within 2 degrees.

## Run c158-r2 results (71 s, rc 0/0, no crash dumps, FATAL/ERROR 0)

- The aim matched exactly again: frag P 1.14 Y 118.56, incendiary P 1.99 Y 116.14 on both sides.
- Both OnDoAction lines had no WARN. The frag showed for 1098 ms and the incendiary for 601 ms.
- The frag-after and inc-after shots show an empty hand: the Blueprint destroyed its held actor.
- The held shots, taken right at the spawn line, show an empty hand. The arm is not up yet at OnAnimationStarted;
  r1's frag shot at +0.3 s showed it.
- Client WARN: the known card 132 STAISpawner/STSpawnPoint rows only.

## Run c158-r3 predictions (DLL b88f10cdb272213a, scenario only)

The frag shot is at +0.3 s again, and the incendiary shots are at about +0.1, +0.2 and +0.3 s (the throw comes at
about 600 ms). At least one incendiary shot shows the incendiary in the hand. The logs predict the same as r2.

## Run c158-r3 results (71 s, rc 0/0, no crash dumps, FATAL/ERROR 0, WARN 2 per side: attach and GOD config only)

- The aim matched exactly for the third time (frag P 1.14 Y 118.56, incendiary P 1.99 Y 116.14).
- Holds: frag 1100 ms, incendiary 604 ms.
- The frag-held shot (+0.3 s) shows the frag in the first-person hand again.
- The incendiary shots at +0.1, +0.3 and +0.5 s show only the hand at the bottom edge. The logs read the same as for
  the frag: spawned, attached to FirstPersonMesh, root visible 1, actor hidden 0. The incendiary montage is about
  600 ms, and the hand never comes up into view in these frames. It is open whether the host's own incendiary throw
  looks the same; no host control shot was taken.
- Budget: 3 of 3 runs used.

Open items:
- A host control shot of the host's own incendiary throw, to show whether the low hand is the game's own montage.
- Sweep: other abilities that spawn visuals through AbilityTask SpawnActor (the same authority-only spawn) were not
  surveyed.

## Review (senior, 2026-11-09)

Closed partial. Accepted: the frag shows in the client's hand (c158-r1, r3), and the aim matches the host's launch
to the hundredth with no code change. Remainder in card 169:
1. The sweep. Every ability that spawns a visual through AbilityTask SpawnActor has the same authority-only spawn.
   The fix should be the class, not one ability.
2. The incendiary visual. Prove it with the host's own incendiary as a control in the same frames, or with a log of
   the held actor's on-screen state. Do not leave it to the user's play.
