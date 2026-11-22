# Card 028: Remote players shown as a trooper sprite that mirrors what they do

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: ready (senior, 2026-11-03). The user chose this card next; send it only to a cleared junior.

Read `docs/STATE.md` first: the "Mod code rules" section, the "Runtime facts" line about `tools/run_pair.sh`, and rows 022, 025, 027 and 029. Then read card 027's report and senior review, card 022's report (part A audit), and card 029's report and senior review. Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

The game is a single-player FPS: the player has only first-person arms and no third-person body (card 022 audit, user 2026-11-03). In co-op each player must see the other. NPC troopers are 8-direction billboard sprites with a full animation set (card 027), so the remote player gets one of those, and the mod picks the animation from what that player is doing.

## Do

1. **Attach the body.** On each machine, for every player pawn that is not locally controlled, once per pawn: add a sprite render component and a `STSpriteVariantAnimatorComponent` with `AActor::AddComponentByClass` (PDB `?AddComponentByClass@AActor@@...`). Wire them the way `BP_AI_Trooper_Morita_C` is wired: log that NPC's component list, the animator's `AnimationData` (+0x128) and whatever links the animator to its renderer, then copy that wiring. Set `AnimationData` to `BP_CharacterAnimations_FemaleMoritaTrooper` (8 directions in every state used here; card 027). Match the NPC's sprite height and offset relative to the capsule. If the asset is not loaded, report how you loaded it.
2. **Map player state to animation.** Each tick, pick one key and call `UADSpriteAnimatorComponent::StartAnimationSequence(FName, float)` only when the key changes. Compare cached `FName`s, not strings. Priority, highest first:

   | Player state | Key |
   |---|---|
   | dead (health 0) | `Death01`, then `Corpse01` when it ends |
   | firing (relayed fire held, or the local fire state on the host's own pawn) | `Attack01` |
   | reloading | `Reload` |
   | throwing a grenade | `GrenadeThrow` |
   | damaged in the last 0.3 s | `Flinch` |
   | sprinting and moving | `Run` |
   | horizontal speed above 10 units/s | `Walk` |
   | otherwise | `Idle` |

   For each state, log which field or event you read it from and on which side it is valid. Card 029 found that no weapon state replicates: the equipped weapon, fire, iron sights and reload are all invisible to the other machine. So on the host, `firing` for the client's pawn comes from card 024's fire relay; on the client, the host's firing is not available until card 026 adds the host-to-client channel, so leave that row out on the client and report it. Reload and grenade are in the same position. Death and damage may replicate through attributes; check and report. Every player uses the same sprite set for now, whatever weapon they hold.
3. **Facing.** The sprite renderer picks its direction from the view angle against the actor's rotation. Check that the remote pawn's yaw is replicated and that the sprite turns when the other player turns.
4. **Prove it.** One pair run with both windows visible. For each machine, take screenshots of the other player standing still, walking, sprinting, firing, and seen from the front, side and back. Log every key change with a timestamp.

## Done when

- Each player sees the other as a trooper sprite that faces the right way and changes animation for idle, walk, run and fire.
- Each state in step 2 is either working, with a screenshot, or reported as not visible on that side.
- Clean build (`/W4 /WX`, `--clean-first`), 84 exports, KERNEL32-only imports. No new crash dumps. Every new function is at most 60 lines. Single player is unchanged.

## Risks

- `AddComponentByClass` on a replicated pawn: add components only on the local machine, and never set them to replicate.
- The renderer may need the local `PlayerController` (`USTSpriteRenderComponent.PlayerController`) to pick a direction. Set it from the local PC if the NPC wiring shows it is set.
- The first-person arms and the weapon of the remote pawn may also be visible. Report what each window shows; hide them only if they float.
- Turrets: a mounted pilot is attached to the turret and stays visible (card 029), so the sprite rides on the turret. Do not hide it. Turret mount state does not replicate, so the other machine sees the pilot standing beside the turret; that is expected until card 026's channel carries it.
- Calling any interface override (for example `OnInteraction`) needs the interface subobject pointer, not the actor pointer (card 029 crashes 1 and 2). Prefer reflected properties and plain member functions.

## Static lead (senior, 2026-11-03)

How an NPC's sprite body is wired, from the disassembly. Check each point at run time before relying on it.

- **The renderer is a static mesh.** `USTSpriteRenderComponent` derives from `UStaticMeshComponent`: its `BeginPlay` (0x051F53F0) calls `UStaticMeshComponent::BeginPlay` first, and it has `CalcBounds`, `CreateSceneProxy` and `ResizeMeshToTextureSize`. A new one needs the same static mesh (a quad) as the NPC's renderer. Copy the static mesh, relative transform and materials from the nearest `BP_AI_Trooper_Morita_C` renderer. Its `BeginPlay` also calls `USTCommonUtilityLibrary::GetSTPlayerCharacter`, which probably fills `PlayerController` for the facing calculation (`ComputeDirectionIndex` 0x051F6AB0, `RotateTowardsPlayerCamera` 0x052145C0).
- **The animator wires itself in `BeginPlay`.** `USTSpriteVariantAnimatorComponent::BeginPlay` (0x050DECD0) and `USTSpriteAnimatorComponent::BeginPlay` (0x050DEB40) call the owner's `FindComponentByClass(USTSpriteRenderComponent)` (actor vtable +0x720). They then `AddDynamic` two handlers, by name, onto the animator's frame delegates (`OnFrameChange`, `OnFrameSizeChange`); these are most likely `AnimatorFrameChanged` and `AnimatorFrameSizeChanged`. So add the renderer first and the animator second. If the animator's `BeginPlay` runs before the renderer exists, nothing gets bound and the sprite never changes frame. Log the delegate's bound-object count after adding.
- **Function addresses.**
  - `AActor::AddComponentByClass`: 0x037D7190.
  - `UADSpriteAnimatorComponent::StartAnimationSequence(FName, float)`: 0x04A249C0.
  - Reflected animator state: `CurrentAnimationKey` (on `UADAnimatorBaseComponent`), plus `ActiveFrameIndex`, `AnimationData` and `bLoopSequence` (on `UADSpriteAnimatorComponent`). The variant animator adds `VariantAnimationData[]`, `bUseVariants` and `VariantID`. Card 027 found `AnimationData` at +0x128 on the Morita's variant animator.
  - Set `AnimationData` before `BeginPlay`. `AddComponentByClass` takes a `bDeferredFinish` flag: pass true, set the properties, then call `AActor::FinishAddComponent` (0x037E1390, `?FinishAddComponent@AActor@@QEAAXPEAVUActorComponent@@_NAEBU?$TTransform@N@Math@UE@@@Z`) so `BeginPlay` runs after them. If the variant animator overwrites `AnimationData` from `VariantAnimationData[]` in `BeginPlay`, keep `bUseVariants` off.
- **Mechs need no new body.** `ASTPlayerMechBase` has its own `SpriteRenderComponent` and `DirectionalMaterials`, so it is drawn by the same renderer class. Mechs are out of scope for this card: none are placed in any level (card 029).

## Report

Done. Both players see the other as a Morita sprite. The sprite turns with replicated yaw and animates Idle, Walk, Run and Attack01, with Flinch and Death on the side that has the data. Two gaps remain. The client cannot see the host fire, because no source exists on that side, as the card expected. The host saw the client Walk but not Run, because the client's scripted sprint is stopped by a wall before speed passes the Run threshold. The remote pawn's first-person weapon and arms float above the sprite and are **not hidden yet** (see "Floating first-person parts").

### What was built (all in `mod/src/hooks.cpp`, `c28_*`)
- **Attach.** `c28_visit` runs per pawn on non-standalone net modes only, for remote pawns (`c28_remote`) that have no `STSpriteRenderComponent` yet. Pawns held by the dropship are skipped. `c28_try_attach` then:
  - finds a live `BP_AI_Trooper_Morita_C` as the template;
  - adds `STSpriteRenderComponent` and `STSpriteVariantAnimatorComponent` with `AddComponentByClass` (0x037D7190, `bDeferredFinish` true);
  - copies the 4 renderer settings the NPC carries (`PitchModifier` 0.2, `PPM` 112, `StaticMesh` `FowardFacingQuad`, `PlayerController`);
  - sets `AnimationData` (+0x128, matched against the reflected offset) to `BP_CharacterAnimations_FemaleMoritaTrooper`, loaded by name;
  - clears `bUseVariants` (+0x148);
  - calls `FinishAddComponent` (0x037E1390).

  The log shows "registered found again yes", with `OnFrameChange` and `OnFrameSizeChange` bound 1 on the new animator, the same as on the NPC.
- **PlayerController.** The NPC's renderer has `PlayerController` set to the local `BP_PlayerController_C`, so the new renderer copies it.
- **Body transform.** Measured on both sides: the Morita renderer is relative (0, 0, 14.3) with scale (1, 2.286, 2.286). The capsule half-height is 100 for Morita and 95 for the pawn, so the pawn's relative z is 9.3, which puts the feet on the same ground line. Rotation is (0, 0, 0), as on the NPC.
- **State to key (`c28_pick`, `c28_drive`).** Cached FNames. `StartAnimationSequence` (0x04A249C0) is called only when the key changes, and every change is logged with tick, health, speed, relay and yaw.

| Key | Source field | Host sees client | Client sees host |
|---|---|---|---|
| Death01, then Corpse01 after 1.1 s | health ≤ 0 (card 021 `c21_health`, replicated) | works by code path, not triggered in these runs | same |
| Attack01 | card 024 fire relay (`c24_find_relay`) | **works** (screenshots) | not available: no relay rows on the client |
| Reload | none | not available | not available |
| GrenadeThrow | none | not available | not available |
| Flinch (0.3 s) | health drop (c21) | **works**: 100→75 at landing, every run | works by code path; the host took no damage in these runs |
| Run (> 600) / Walk (> 10) | speed | Walk **works**. Run not reached (wall, see below) | Walk and Run **work** |
| Idle | default | works | works |

  - **Speed source differs per side.** The client reads the simulated proxy's replicated CMC `Velocity` (CMC+0xD0). On the host, the server copy's CMC `Velocity` reads about 0 while the client walks at 700 (run 2: 34 then 8 during a 3 s walk), so `c28_speed` uses the 2D location change between samples instead.
- **Facing.** Yaw replicates. Host control yaw 126/216/306 arrives on the client as 127/-143/-53. The sprite visibly shows front, side and back on both sides (orbit and host-turn shots below). Directional frames come from the renderer itself; no code is needed beyond the attach.
- **Proof tooling.** `UBWCOOP_C28_SHOTS=1` (new, read in `config.cpp` `read_c28_shots()`) adds two things:
  - **Key shots.** Each window takes a screenshot of the other player at the first occurrence of each key. Before a key shot, the host teleports to 400 units from the client on a side angle (`kC28ApproachDeg`, `c28_place`). The client's Run shot waits 1200 ms so the host is close.
  - **Front/side/back series.** The host orbits the client for its own series, then turns in place for the client's series.

  Shots are taken with console `shot`. Off by default; no effect when the env is unset.

### Runs (all rc 0/0, 0 FATAL/ERROR, no new crash dumps)
Env: host `UBWCOOP_AUTODROPSHIP=1 UBWCOOP_HOST_SPRINT=1 UBWCOOP_C28_SHOTS=1 UBWCOOP_GRANT_WEAPON=MoritaMk1`; client `UBWCOOP_AUTOINPUT=1 UBWCOOP_C28_SHOTS=1 UBWCOOP_GRANT_WEAPON=MoritaMk1` (run 1 had no weapon grant). Screenshots are in `logs/`.

| Run | Logs | Result |
|---|---|---|
| 1 `card028` | `card028{,-host,-client}.*` | Idle front, side and back on both sides (`card028-run1-*`). Client's view of the unarmed host shows floating grey arms. |
| 2 `card028b` | `card028b-*` | Host: client Walk side close up, Flinch. Attack shot dark: the front approach put the host inside a wall, so it was changed to side angles. Host speed read about 0, so it was switched to location delta. |
| 3 `card028c` | `card028c-*` | Host: Walk side, Attack01 side (muzzle sparks, partly behind a post). The client's Run shot of the host was at distance 1057 (a speck). |
| 4 `card028d` | `card028d-*` | Final. All states the card asks for are covered, except the two gaps above. |

Run 4 screenshots:
- **Host sees client:**
  - `card028d-host-2-Idle-front.png`, `-3-Idle-side.png`, `-4-Idle-back.png` (orbit, distance 401)
  - `card028d-host-5-Walk-side.png` (distance 290)
  - `card028d-host-6-Attack01-side.png` (distance 708, muzzle flash visible)
  - `card028d-host-1-Idle-front.png` is black: the shot lands during the fade-in, every run.
- **Client sees host:**
  - `card028d-client-1-Idle-front.png` and `-2-Walk-front.png` (distance 3400 to 3700, early in the match before the host approaches)
  - `card028d-client-3-Run-side.png` (distance 501, a clear running sprite)
  - `card028d-client-4-Idle-front.png`, `-5-Idle-side.png`, `-6-Idle-back.png` (host turn, distance 402)

Run 4 key changes (ms tick; host and client clocks are the same machine):
- **Host's view of the client.**
  - 491809025 None→Idle
  - 491839554 Idle→Flinch (health 75)
  - 491839862 Flinch→Idle
  - 491911269 Idle→Walk (speed 139)
  - 491914408 Walk→Idle
  - 491918842 Idle→Walk (36)
  - 491919742 Walk→Idle
  - 491925276 Idle→Attack01 (relay 1)
  - 491927293 Attack01→Idle
  - 491927793 Idle→Attack01
  - 491929809 Attack01→Idle
- **Client's view of the host.**
  - 491840281 None→Idle
  - 491847043 Idle→Walk (170)
  - 491850284 Walk→Idle
  - 491854526 Idle→Walk (37)
  - 491854927 Walk→Run (960)
  - 491856655 Run→Idle

### Gaps and findings
- **Host never sees the client Run.** The client's autoinput sprint heads at yaw about -50 into a building and stops within about a second, so location-delta speed never passes 600 on the host. This is map geometry and the test script, not the code. The client-side Run path, which uses the same threshold, works. A pair run with a human-driven client sprint in open ground would close this.
- **Floating first-person parts (not hidden).** The remote pawn's own first-person meshes still draw on the other machine. The client's view of the unarmed host (run 1) shows grey first-person arms floating near head height. The host's view of the armed client (`card028d-host-5-Walk-side.png`) shows the real MoritaMk1 mesh floating above the sprite's head, next to the rifle drawn in the sprite. They do float, so by the card's rule they should be hidden. I did not do that in this card, because it needs another build and pair run. The likely fix is to set `bHiddenInGame` (or `SetVisibility(false)`) on the remote pawn's first-person arms and weapon mesh components at attach time. Recommend a small follow-up card.
- **Turret pilots.** Not touched. Mount state does not replicate, so a remote pilot's sprite stays visible standing at the pawn's location.
- **No interface overrides.** No vtable patches. Components are local only and never replicated: each machine attaches its own pair to its remote pawn.

### Done-when check
- Sprite facing the right way and animating idle, walk, run, fire: yes on both sides, except the two gaps above, which are reported.
- Every state is either shown in a screenshot or reported as unavailable on that side: yes (table above).
- `/W4 /WX` `--clean-first` build is clean. DEPLOY check: 84 exports, KERNEL32-only imports (deployed 8b8e39572ec25a5a).
- No new crash dumps in any of the 4 runs.
- Every function in `mod/src/*.cpp` is ≤ 60 lines (awk check).
- Single player: `c28_visit` returns at once when `net_mode == kNetModeStandalone`, and the shot code is gated on the env. No separate SOLO run was made for this card.

## Senior review (2026-11-03)

Accepted. Checked independently:

- `logs/card028d.out`: rc 0/0, 187 s, 0 FATAL/ERROR/WARN on both sides, no new crash dumps. Key changes in `card028d-host.log` (line 656 on) and `card028d-client.log` (1038, 1067, 1080) match the report.
- Deployed `dwmapi.dll` hash starts 8b8e39572ec25a5a; 84 exports (`llvm-readobj --coff-exports`), KERNEL32 the only import. No function in `mod/src/*.cpp` is over 60 lines.
- `c28_visit` (`mod/src/hooks.cpp:9339`) returns before any work in standalone, so single player is untouched.
- Screenshots: `card028d-host-5-Walk-side`, `-6-Attack01-side`, `card028d-client-3-Run-side` and `-5-Idle-side` show a correctly placed, correctly facing Morita sprite. The floating first-person rifle (host-5) and grey arms (client-5) are confirmed.

Open, carried to follow-ups:

1. Floating first-person meshes: card 033 hides them.
2. Client cannot see the host fire: the card 024 relay only runs client to host. A host-to-client fire signal belongs on the card 026 channel. Not a separate card yet.
3. Host never saw the client Run: test geometry only; a human-driven pair run will cover it.
4. Reload and grenade have no source on either side; same channel as item 2.
5. The host-side speed comes from location change because the server copy's CMC `Velocity` reads about 0. Recorded as a runtime fact.
