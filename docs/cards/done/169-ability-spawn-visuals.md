# Card 169: Ability SpawnActor visuals show on the owning client (card 158 class)

Status: CLOSED, outcome in docs/CARDLOG.md. Was: queued 2026-11-09, card 158 remainder. Budget: static first, then up to two runs, batched with card 160's
build if it fits. Visible effect (all effects sent).

Read: card 158 (all of it), mod/src/ubw/grenade_c158.cpp.

## Why

UAbilityTask_SpawnActor spawns only with net authority, so on a client any ability that spawns a held or local
visual this way fires DidNotSpawn and shows nothing. Card 158 fixed GA_GrenadeThrowAbility_C by hand. The other
abilities with the same task were not surveyed. The incendiary visual is logged as spawned and attached but not shown
in a still.

## Do

1. Static first: list every Blueprint (abilities and others) that uses AbilityTask SpawnActor, with the spawned
   class and what the Success handler does with it. Say for each whether the spawned actor is a local visual (spawn
   on the owning client) or gameplay state (host only, already synced).
2. If more than the grenade needs it, move card 158's fix down to the task: on a client-own ability, when the task
   would fire DidNotSpawn for a local-visual class, spawn locally and run the Blueprint's Success handler, as
   card 158 does. Keep grenade_c158.cpp's behavior, but drive it from the shared path.
3. Incendiary: a host control of its own incendiary in the same frames as the client's stills, or a log of the held
   actor's on-screen state (visible, attached socket, world position next to the camera).

## Done when

- The list is written in this card, and every local-visual case shows on the client in a run (log plus still or the
  host control).
- The incendiary matches the host's control.
- The usual Report items (WARN diff, run length, rc, ERROR/FATAL, CRASH, dumps, DLL hash). It ends with "Report
  complete."

## Static sweep (junior, 2026-11-09)

Scan: every package in pakchunk0's utoc (3869 non-asset packages by their 128 KB header, plus all 65 `GA_`
packages in full) for the K2 temporaries an AbilityTask SpawnActor node leaves in the name map
(`CallFunc_BeginSpawningActor_*`, `AbilityTask_SpawnActor`). Three packages use the task, all gameplay abilities:

| Blueprint | Event that spawns | Spawned class | Success handler | Ends | Kind |
|---|---|---|---|---|---|
| GA_GrenadeThrowAbility (frag, incendiary) | OnAnimationStarted | GrenadeVisualClassRef (per item) | sets GrenadeVisuals, attaches to FirstPersonMesh | OnDoAction and K2_OnEndAbility: DestroyActor | local held visual |
| GA_UseTacticalSupportAbility | OnThrowStarted (cast owner to BP_Player_Character_MI) | hardcoded class literal (import I10) | sets "Flare Visuals", zero location and rotation, AttachToComponent(FirstPersonMesh, hand_r), AddActorLocalTransform(90, 0, 0) | OnDoActionTriggered and K2_OnEndAbility: DestroyActor | local held visual |
| GA_HumanMelee | K2_ActivateAbility (cast owner to BP_Player_Character_MI) | hardcoded class literal (import I8), Instigator set before finish | sets KnifeVisuals, AttachToComponent(FirstPersonMesh, KnifeAttachSocket, snap), then PlayAnimation and the lunge | K2_OnEndAbility: DestroyActor | local held visual |

No spawned class is gameplay state: each lives only as the first person mesh's held prop, and the Blueprint destroys
it itself. Damage, the thrown projectile, the flare and the melee hit run in other nodes (OnDoAction_Implementation,
DealDamage), which the host already runs and syncs.

Melee note: its DidNotSpawn handler jumps into the same ubergraph code as Success (store KnifeVisuals, attach, play the
animation, lunge), with a null actor. So on the client the melee animation already played, but with no knife in hand.

## Design (junior)

One detour at the task: `UAbilityTask_SpawnActor::BeginSpawningActor` (exe+0x494E3B0), mod/src/ubw/spawn_c169.cpp.

- Client, and the task's own ability's avatar is the client's own pawn: spawn the class deferred, as the native
  authority branch does (identity transform, AlwaysSpawn, bDeferConstruction; read from the function's code at +0xDD
  and +0xEB), and return true. The Blueprint's own FinishSpawningActor then finishes the actor and broadcasts Success,
  the same path the host runs. DidNotSpawn does not fire, so melee runs its animation once, from Success.
- The by-value target data handle is destroyed by the callee (x64 ABI). It is released by calling the original with
  the task's Ability cleared for that call: with no Ability the original skips both the spawn and the DidNotSpawn
  broadcast (+0x26 and +0x157 checks), destroys the handle, and returns false.
- A failed local spawn falls back to the original call, which is the old behavior.
- Everything else (host, server copies, other pawns' abilities) calls the original unchanged.
- The proof is one generic check: each locally spawned actor is held in a table until `AActor::K2_DestroyActor` (the
  Blueprints' own end of a held visual). There the client logs `AActor::WasRecentlyRendered(0.2)`, the distance from
  the camera, and the attach parent. This is the incendiary's on-screen state (item 3) and the same check for every
  class.
- grenade_c158.cpp no longer spawns or calls Success. It logs GrenadeVisuals and its attach state after
  OnAnimationStarted (set by the shared path), and the aim at OnDoAction, as before.

Built: DLL 095e6ccaf9f3919a.

## Predictions, run 1 (batched with card 160: tools/scenarios/c160-c169.txt)

Added for the incendiary question: the destroy-time line now also gives the root's attach socket and its visible
flag, plus the actor's hidden flag. The host's own held visuals take the same table, so the host's own throw logs
the same line as the control, in the same run.

1. Host control: the host takes both grenade pickups first and throws the frag, then the incendiary. Each gives
   "card169: host t=... held <X> destroyed after N ms: rendered ... 1, D cm from the camera, attached to
   FirstPersonMesh socket S, root visible 1, actor hidden 0".
2. Client: after the pickups respawn it takes both and throws. Each gives "card169: client t=... spawned ..." and
   then the same destroy line. For the incendiary the socket, visible flag and distance match the host's own within
   a few cm. If the host's own incendiary also gives a distance close to the camera with rendered 1, the client's low
   hand is how the game animates it, and there is no defect.
3. Melee (V) on the client: "card169: client ... spawned" for the knife class, then the destroy line at
   K2_OnEndAbility, attached to the KnifeAttachSocket. The host's own V press is the control.
4. Flare: only if the run gets that far (it is the last block, with failsoft). The client's flare visual follows
   the same two lines, attached at hand_r.
5. Map totals at quit: spawned equals rendered on both sides; 0 not spawned.

## Run 1: c160-r1 (DLL 42b93b6221fa7e9c, shared with card 160)

The host control ran. Drive rc 1 at line 53: the client's grenade pickup lines came while it walked to the Morita,
before the scenario's later client mark, so the "since-mark" waits could not match. That is a scenario fault, fixed
for run 2 (whole-log waits). The client's throws, the melee and the flare were not reached.

- Host's own frag: "held BP_GrenadeVisuals_C destroyed after 1097 ms: rendered in the last 0.2 s 1, 41 cm from the
  camera, attached to FirstPersonMesh socket FragGrenadeSocket, root visible 1, actor hidden 0".
- Host's own incendiary: "held BP_IncendiaryGrenadeVisuals_C destroyed after 600 ms: rendered in the last 0.2 s 0,
  113 cm from the camera, attached to FirstPersonMesh socket RightHandSocket, root visible 1, actor hidden 0"
  (logged WARN by the not-rendered rule).
- So the host's own incendiary, with no mod code in its path, is not rendered in its last 0.2 s and sits 113 cm from
  the camera on RightHandSocket. That matches the client's low hand in card 158's shots. Run 2 gives the client's line
  in the same frames for the side-by-side.

## Run 2: c169-r2 (DLL 42b93b6221fa7e9c)

Ended by scenario after 106 s. Host rc 0, client rc 0. FATAL/ERROR 0, no new crash dumps. Drive rc 2: one failsoft
wait failed, line 84 (the client's melee "spawned" line). The line is in the log at line 3925, after the mark at
3921. It was flushed after the 5 s timeout ran out, together with the destroy line that the next wait then matched at
once. That is log flush latency, not a missing spawn. WARN diff: host 3 (attach, config, and the host's own
incendiary card169 line), client 6 (attach, config, the known card 132 STAISpawner/STSpawnPoint rows). Unchanged
from run 1.

| Held visual | Side | Shown for | Rendered last 0.2 s | From camera | Socket |
|---|---|---|---|---|---|
| BP_GrenadeVisuals_C (frag) | host own | 1096 ms | 1 | 41 cm | FragGrenadeSocket |
| BP_GrenadeVisuals_C (frag) | client own | 1092 ms | 1 | 42 cm | FragGrenadeSocket |
| BP_IncendiaryGrenadeVisuals_C | host own | 614 ms | 0 | 113 cm | RightHandSocket |
| BP_IncendiaryGrenadeVisuals_C | client own | 615 ms | 1 | 48 cm | RightHandSocket |
| BP_Knife_C (melee) | host own | 476 ms | 1 | 100 cm | LeftHandSocket |
| BP_Knife_C (melee) | client own | 481 ms | 1 | 50 cm | LeftHandSocket |

All six: root visible 1, actor hidden 0, attached to FirstPersonMesh. The client's three each logged "spawned ... for
its own ability" and the Blueprint's own Success attach. The host's server copy throws matched the client's aims (frag
aim P 0.70 Y 118.55 on both sides; incendiary P 1.55/1.54 Y 116.12/116.13).

- Incendiary: the client's held incendiary is rendered at the destroy frame, 48 cm from the camera, on the same socket
  as the host's own. The screenshot client-c169-r2-inc-held.png (0.2 s into the hold) shows the canister in the
  lower left hand. The host's own incendiary, with no mod code in its path, was not rendered in its last 0.2 s and
  was 113 cm out, in both runs. That is the game's own throw pose at release; the client is not behind it.
- Knife: the client now holds the knife during melee (before this card, DidNotSpawn ran the animation with no knife).
- Flare: not run (dropped from the scenario to keep the run short). It goes through the same task-level path.
- The class is covered at the task: grenade_c158 runs through the shared spawn, with no grenade-specific spawn left.

Card 169 is done.

## Review (senior, 2026-11-09)

Accepted on c160-r1 and c169-r2: one detour at UAbilityTask_SpawnActor::BeginSpawningActor covers the grenade, the
flare and the HumanMelee knife; the frag, incendiary and knife show on the client. Two checks ride card 162's run:
1. The tactical-support flare in a run (the Done-when names every local-visual case).
2. The incendiary at destroy: host 113 cm and unrendered, client 48 cm and rendered. Log the first-person montage
   position at destroy on both sides. If the client's montage lags the host's, say why; if they match, write the
   difference down as socket or pose and close it.

## Card 162 run items (junior, 2026-11-09)

Flare, static trace: on the client, the client's own GA_UseTacticalSupportAbility never runs. Card 100 sends every
slot input to the host, and HandleTacticalSupportInput is not run for the client's own pawn (vehicles.cpp:1922). The
ability runs only on the host's server copy. That ability's OnThrowStarted spawns the FlareVisuals there, attached to
the server copy's FirstPersonMesh, which nobody sees. So the shared task-level spawn never sees a client-own flare
ability. The prediction is that the client holds no flare in its hand: a gap in the Done-when ("every local-visual
case"), and the detour cannot fix it on its own. The run tracks the server copy's held visuals on the host
(spawn_c169.cpp now tracks host-own and server-copy spawns) to show it.

Montage at destroy: each destroy now also logs "held <class> of the <own pawn | server copy> destroyed during first
person montage <montage> at <s> s". The montage is the avatar's FirstPersonMesh anim instance's
GetCurrentActiveMontage, read with Montage_GetPosition.

### Predictions, card 162 run 1 (tools/scenarios/c162-run1.txt)

1. Host's own incendiary: destroyed during the throw montage at position P_h. The host's server copy of the client's
   incendiary is destroyed during the same montage at about the same position. The client's own is at P_c.
   - If P_c is about P_h, the montage is not behind. The 113 cm against 48 cm is then pose or camera: the host's
     camera tracks its own pawn's view, while the client's is its own camera. Write it down and close.
   - If P_c is clearly earlier, the client destroys early. Its destroy comes from the local Blueprint's OnDoAction
     notify, while the host's comes from the montage notify on the server copy.
2. Host's own flare: "held <FlareVisuals class> of the own pawn destroyed", rendered 0 or 1, with a montage.
3. Client's flare: host "card100 ... HandleTacticalSupportInput run, charge 20000 -> ...", then the host's "held ...
   of the server copy destroyed". The client logs no "card169: client ... spawned" line: the gap above.
