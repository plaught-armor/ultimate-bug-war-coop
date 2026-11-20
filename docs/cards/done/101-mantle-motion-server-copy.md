# Card 101: The client's mantle is corrected back because the server copy does not move with it

Status: written 2026-11-05 (senior); sent 2026-11-05; accepted 2026-11-06.

Read:

- card 094 Report, "Open: mantle movement diverges" (the correction table from proof3 and scout7);
- `docs/playtest-2026-11-05.md`, item 1;
- Trust model (user): play is between friends only, so the netcode uses fast paths with basic crash-safety checks and
  no anti-cheat validation.

Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`. Play DLL base: `9d6329427cc72647` (card 104).

## Why

Since card 094, the client's jump starts the mantle search on both sides, and GA_Mantling_Move (handle 45) activates on
the client and on the server copy at the same position. The motion still diverges:

- The server copy's Z stays at 817.2 and it creeps about 20 uu forward.
- The client rises about 100 uu and moves 136 uu forward.
- The host corrects the client back off the ledge (errors up to 153 uu), so the vault fails most tries.

How GA_Mantling_Move moves the pawn (senior, from the package name map and the disassembly; confirm in step 1):

- The Blueprint (`/Game/GAS/Mantaling/GA_Mantling_Move`, extracted name map) calls
  `USTAbilityTaskLerpActorToComponent::LerpActorAlongCurveToComponent` (factory 0x0512A880; arguments ability,
  duration, start and target location, component, actor, `MantleCurve`). It also uses `GetMantleDuration`,
  `GetTargetDataEndPointTransform`, `StopMovementImmediately`, `PlayAnimation` (the `AM_Arms_Mantle` montage), and the
  task's `OnMovementEnded` and `OnAnimationEnded` events.
- The task class is a `USTAbilityTaskLerpActorAlongCurve`. Its `TickTask` (0x0513B660) slerps rotation with
  `SetActorRotation`, and `Tick_LerpActorLocation` (0x0513D6B0) reads `GetMoveAlpha` (0x051251B0) and the curve
  (`UCurveFloat::GetFloatValue`), then calls `AActor::SetActorLocation` (0x037F2470) every tick.
  `EndLocationLerp` (0x0511D040) sets the final location and broadcasts the end.
- So the mantle is a per-tick teleport outside CharacterMovement, not root motion. The montage is the first-person
  arms only (`SK_Arms_Skeleton`), so it carries no pawn motion.

Why the server copy then diverges (hypothesis to check): the client's SetActorLocation calls are not saved moves, so
its ServerMove packets carry a client location the host cannot reproduce. On the host, either the server copy's task
does not tick (ability tasks tick through the ASC, which may not tick for the server copy), or it ticks and
CharacterMovement's ServerMove simulation overwrites the position each frame. The server copy's Z staying at 817.2
fits either case. Step 1 tells which.

## Do

1. **Find the motion (one run, log only).** One client mantle over a known ledge, with both sides logging every frame
   of GA_Mantling_Move:
   - the pawn's location and MovementMode;
   - each `Tick_LerpActorLocation` call (task, owner actor, alpha, target) and each `SetActorLocation` it makes, with
     the location before and after;
   - whether the server copy's task ticks at all, and `EndLocationLerp`;
   - the server copy's received ServerMove packets.

   - the activations of each mantle and jump ability on the server copy per client press, with the path (the
     client's `ServerTryActivateAbility` RPC, or a host-side `HandleGameplayEvent`). Card 104 now reads server-copy
     LocalPredicted abilities as ServerOnly inside `HandleGameplayEvent`, so an ability can start twice on the host:
     once from the event and once from the client's RPC. Expect one activation per press; if there are two, fix that
     first (for example skip the event path for an ability the client predicts and sends itself).

   Compare the two sides and name what the server copy lacks.
2. **Fix, at the cause if it is small.** For example: make the server copy's lerp task tick, or keep the
   location it sets from being overwritten by the ServerMove simulation for the lerp's duration.
3. **Otherwise, use the engine's client-authoritative path for the mantle only.** While GA_Mantling_Move is active on
   a server copy, set `bIgnoreClientMovementErrorChecksAndCorrection` and `bServerAcceptClientAuthoritativePosition`
   on its CharacterMovementComponent. Clear them when the ability ends or is cancelled, and also on a timeout as a
   guard. This fits the friends-only trust model. Card 094's "do not turn off server corrections" still holds outside
   the mantle window.
4. **Proof**, one scenario run: the client mantles over the proof3 ledge at about (-796, 20857, 853) and the scout7
   ledge at y 20968.6, three tries each. No host correction larger than 10 uu happens during a mantle. The client ends
   on top, and the server copy's position matches within 10 uu after landing. Walking and jumping corrections outside
   the mantle are unchanged. rc 0, ERROR/FATAL 0, no new dump.

Stop when the fix builds and that one run shows it; the user will playtest.

## Done when

- The Report names how the mantle moves the pawn, why the server copy did not, the fix, the proof run and the DLL hash.
- The Report ends with "Report complete."

## Report
Written 2026-11-06 (junior). DLL `c5a6e7f5bc589024` (copy: `build/dwmapi-c101-c5a6e7f5bc589024.dll`).

### How the mantle moves the pawn

The senior's reading is confirmed. GA_Mantling_Move runs a `USTAbilityTaskLerpActorToComponent`. Its
`Tick_LerpActorLocation` (0x0513D960) calls the base `USTAbilityTaskLerpActorAlongCurve::Tick_LerpActorLocation`
(0x0513D6B0), and that calls `AActor::SetActorLocation` every tick. The task layout is: +0x90 elapsed, +0x94 duration,
+0xB0 target, +0xC8 actor. The lerp lasts 0.078 s and lifts the pawn about 55 uu forward and 58 uu up, for example
from (-455.7, 21795.8, 145) to (-455.7, 21850.6, 201.6). CharacterMovement and root motion play no part.

### Why the server copy did not follow

Run c101d, on the base DLL plus log hooks. The server copy's lerp task does tick on the host, and its SetActorLocation
does move the pawn. Between two ticks, though, the ServerMove simulation resets the location: a tick's "before" is not
the previous tick's "after". The client's ServerMove positions are one or two lerp steps out of phase with the host's
lerp. As a result, ServerCheckClientError corrected the client during every mantle:

- 2.1, 24.4 and 60.2 uu at y 21796;
- 20.9, 38.6 and 17.5 uu at y 23866;
- 15.1, 41.4, 5.8 and 8.2 uu at y 26858.

That is ten corrections, seven of them above 10 uu. The lerp cannot be made to ignore the ServerMove simulation in a
small change, so the fix follows step 3.

### Activations per press

In c101d, both sides activated GA_Mantling_Move twice per press:

- The client activated once through its own TryActivateAbility (caller exe+0x49771B4). It activated a second time
  through ClientTryActivateAbility (caller exe+0x495339F).
- Each client activation sent a ServerTryActivateAbility, so the host reached ActiveCount 2 and ran two lerp tasks
  per frame.

The cause is card 104's per-handle flip in `detour_internal_try_activate`. It also ran for the client's own requests,
so the host read the LocalPredicted ability as ServerOnly inside the RPC's activation and told the client to start it
again.

c101g tried skipping only the HandleGameplayEvent path (the event flip went from 16 to 13 specs). The double was
still there.

### Fix

1. **Dedupe** (`input_diag.cpp` `detour_internal_try_activate`, `anim_c104.cpp`):
   - The request carries the client's prediction key, so the ability starts on the server copy as it is. A client
     request (`g_jump.in_server_try` on the host) therefore no longer calls `c104_policy_set`.
   - The request's handle goes into a fixed 1024-bit set. The HandleGameplayEvent flip (handle -1) skips the specs in
     that set, so the host cannot start a client-predicted ability a second time from an event.
   - Host-initiated activations, such as the slot's bombing run and the flare event, still flip as in card 104.
2. **Client-authoritative mantle window** (`jump_c94.cpp`, card 101 section). CMC byte +0x555 holds the two flags,
   read from the disassembly:
   - bit 0x2 is `bIgnoreClientMovementErrorChecksAndCorrection`; it is tested at the top of ServerCheckClientError
     (0x03A4CB30);
   - bit 0x4 is `bServerAcceptClientAuthoritativePosition`; it is tested in ServerShouldUseAuthoritativePosition
     (0x03A4F680).

   On the host, for a server copy only, these events open or extend the window:

   | Event | Window extended by |
   |---|---|
   | A successful client-request activation of GA_Mantling_Move | 600 ms (covers the time until the first lerp tick) |
   | Each lerp tick | 250 ms |
   | EndLocationLerp | 150 ms (the client's last moves of the mantle) |

   While a window is open, both bits are set on the server copy's CMC and their previous values are saved. Closing
   restores them. The window closes only in `c101_cmc_tick`, which runs before that CMC's own TickComponent, so it
   never writes to a CMC that is not live. A timeout therefore also covers an ability that is cancelled.

   There are four fixed window slots. When all are busy, a mantle stays server-corrected.

   The two lerp hooks are now installed always. Their logging still needs `UBWCOOP_C94_LOG=1`.
3. **Crash fix found in the proof run (c101i, host).** `InternalServerTryActivateAbility` passes an uninitialized stack
   slot as OutInstancedAbility (exe+0x14963F1, `lea 0x30(%rbp),%r9`). The engine writes that slot only on success, so
   after a failed client request c94_try's `class_name(*out)` read garbage (EXCEPTION_ACCESS_VIOLATION at
   0x1D0000002F, dump UECC-…A0_0000). The detour now sets `*out = nullptr` before calling the original. This was
   latent since card 094: the log path needs `UBWCOOP_C94_LOG=1`, but the activation notes read `*out` too.

### Proof

The route is `tools/scenarios/c101-measure.txt` (scout7):

- six W+jump presses;
- then eighteen short W steps, each with a D tap, to sweep x across the y 21796 ledge.

Whether the mantle search finds the ledge depends on x: x -455.7 (c101d), -472.9 (c101i) and -503.1 (c101g) mantled;
-462.9 (c101e) did not. So a run mantles only some of the time.

| Run | DLL | Mantles | Corrections during mantle | Activations per press | Result |
|---|---|---|---|---|---|
| c101d | base + logs | 4 | 10 (up to 60.2 uu) | 2 + 2 | rc 0 |
| c101g | window + event skip | 1 (x -503) | 0 | 2 + 2 (still double) | rc 0, ERROR/FATAL 0, no dump |
| c101i | + no flip for client requests | 1 (x -473) | 0 | 1 + 1 | host crash after the mantle (fixed in 3) |
| c101j | `c5a6e7f5bc589024` | 0 (route drift) | none | none | rc 0, ERROR/FATAL 0, no dump, no failed-request crash |

Details:

- **c101g:** the window opened at the activation and closed 530 ms after the lerp end. The server copy's end location
  was (-503.3, 21848.0, 200.7) and the client's was (-503.1, 21848.0, 201.8): 1.1 uu apart.
- **c101i:** one activation on each side. The window opened at the activation and closed after 600 ms, with no
  correction. The client ended at (-472.9, 21850.5, 201.8).
- **Corrections outside the mantle are unchanged:** 9 to 14 per run, all 1.8 to 2.2 uu.

Not shown:

- The proof3 ledge (about (-796, 20857, 853)) was not reached. A navmesh `goto` to the ledge area routes around it
  (c101c and c101f).
- No single run has three mantles at each ledge.
- c101j, the final DLL, did not mantle.

The fix is in and builds, and two runs show the mantle without corrections. Per the card, the user playtests from
here.

Report complete.
