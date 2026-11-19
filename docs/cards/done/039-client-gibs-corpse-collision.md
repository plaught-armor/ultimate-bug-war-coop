# Card 039: Gibs and corpse collision on the client

Status: sent 2026-11-03.

Read `docs/STATE.md` first ("Mod code rules", "Runtime facts", the milestone-3 known issue), then card 038's report and senior review. Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

Card 038 made enemies rear and collapse on the client. Two gaps remain:

- When the host gibs a bug (overkill), the client shows the normal death instead of the gib burst (card 038 shots 00396 host against 00395..00403 client).
- After death, the host releases the corpse's collision through the corpse ticket. Nobody has measured the client copy; a client player may walk into or shoot invisible-wall corpses.

## Static leads (senior, from the PDB publics and disassembly)

- **Overkill entry:** `ASTNonPlayableCharacterBase::StartOverkillState()` 0x051A8A20 (protected virtual, so no direct callers; reached through the vtable). It:
  - sets the NPC state byte at +0xAE8 to 5, calls two virtuals (+0xB30, +0xA38) and `ReceiveBaseStateChanged`;
  - cancels the bark, calls `ReleaseCorpseTicket` 0x051A38B0 when +0xA28/+0xA29 allow;
  - reads gore data at +0xA90, takes `GetGoreLocation` 0x050CD9F0 and the mesh rotation, and calls `USTCombatUtilityLibrary::SpawnGoreResponse(UObject*, const FSTGoreResponseData&, FVector, …)` 0x0524F880;
  - then touches the state machine (`USMInstance::IsActive`, `USTAggregatedSMInstance::SetTickingEnabled`).
- **SpawnGoreResponse** 0x0524F880 is cosmetic on a static read: `PlaySoundAtLocationFName`, `GetGoreMultiplier` from user settings, a secrets check, then `LoadSynchronous` of a Niagara system. Confirm it spawns no replicated actor (look for a `SpawnActor` call further in, or log the actors that appear).
- `TryPlayOverkillAnimation()` 0x051B12E0 picks between the sequence keys `Overkill`, `SpawnBody`, `Stagger`, `Death` and `Taunt` (wide strings near 0x148148350..0x148148398) and clears +0x9A0 on one path.
- Other natives: `UADHealthComponent::IsOverkilled` 0x037E75A0, `GetOverkillThreshold` 0x04A3C790, `OverkillDamageEffectReceived` 0x04A3D850, `ASTNonPlayableCharacterBase::IsOverkilledComplete` 0x051985A0, `USTGoreComponent::OnOverkill(FVector)` 0x0519D4D0, `USTCheatManager::OverkillAllNPCs(float)` 0x052113F0. `OverkillThreshold` already replicates (card 021).
- Card 038 found that the WarriorBug has no `USTBugAnimPhysComponent`, and the host never calls the gore component's `OnDeath`. Expect `USTGoreComponent::OnOverkill` to be absent on the bug too; check, do not assume.

## Do

1. **Host trace.** Detour `StartOverkillState`, `SpawnGoreResponse`, `TryPlayOverkillAnimation` and `ReleaseCorpseTicket` (log-only on the host). For one gibbed bug and one normal kill, log the order with ms offsets against card 038's `HandleDeathEvent` line, plus Health and `OverkillThreshold` at the death edge. Answer: what decides overkill (Health ≤ −threshold, or a flag), and which call draws the gibs.
2. **Client gib.** On the client's death edge (`c38_death_edge`), when the replicated values say overkill, call only the visual part: `SpawnGoreResponse` with the actor's gore data and `GetGoreLocation`, then `TryPlayOverkillAnimation` if the host calls it. Do **not** call `StartOverkillState` on the client (it moves the state machine and the corpse ticket). If the client can't tell overkill from the replicated values, report what is missing and stop there for this step.
3. **Corpse collision.** Measure on both sides, 1 s and 5 s after death: the capsule's collision enabled state and profile name, and the mesh's. If the client's copy still blocks while the host's does not, turn it off on the client to match (component call only, no game logic). Prove it with a client shot or walk through the spot.
4. **Log.** One line per enemy per event, capped per map. Flag-gate new behavior the way card 038 did.
5. **Runs.** Card 026c pair command. Use the cheat `OverkillAllNPCs` only if a normal fight does not gib within one run; say which. Take client shots 1 s after host shots (shared Screenshots folder).

## Answer, with log lines

- What triggers overkill on the host, and in what order are the four calls made?
- Does `SpawnGoreResponse` spawn any replicated actor?
- Does the client show the gib burst on the same bug the host gibbed?
- What is the corpse collision on each side before and after the fix?

## Done when

- Shots: host gib and client gib of the same bug; a client walking or firing through a corpse spot that blocked before (or a log showing it never blocked).
- No game-logic calls on the client (state machine, corpse ticket, kill events counts all 0 in the client log).
- Clean build (`/W4 /WX`, `--clean-first`), 84 exports, KERNEL32-only imports. No new crash dumps. Every new function is at most 60 lines, `install()` included. Write `card039x-run.txt` with the rc.

## Risks

- `SpawnGoreResponse`'s gore data argument may be a struct read from +0xA90 at a different offset; read it the same way `StartOverkillState` does (show the disassembly lines you copied).
- The client death edge may come before `OverkillThreshold` arrives; if so, log both values and say whether a short delay is needed.
- Gibs and the death collapse may both play on the client. Report what the host shows for a gibbed bug (collapse or not) and match it.

## Senior addendum (2026-11-03, after sending): SpawnGoreResponse needs the GameMode

A further static read of `SpawnGoreResponse` (0x0524F880) shows that two of its parts sit behind `UGameplayStatics::GetGameMode`, which is null on a client (card 004/005):

- **Niagara burst:** at 0x14524FC85 it calls `GetGameMode`, checks the result is an `ASTGameplayGameMode`, reads `GameMode+0x388` (the `USTParticleEffectManager`), and only then calls `USTParticleEffectManager::STSpawnSystemAtLocation` 0x051DAF10. Any null jumps to 0x14524FDFF and skips the burst.
- **Gib parts:** the second `GetGameMode` at 0x14524FE3A gates the `FSTGoreResponsePart` loop that queues work on `USTDeferredActionWorldSubsystem` (copying `FSTGoreResponsePart`, and a helper that calls `UADProjectilePooler::SpawnProjectile`, so the chunks are probably pooled projectiles). A null jumps to 0x1452500E4, which still runs `SpawnGoreDecals` 0x0524F220 and `SpawnScreenAndWeaponGoreEffects` 0x052502D0.
- **Not gated:** the sound (`PlaySoundAtLocationFName`) at the start, then the decals and screen effects at the end.

So a plain client call to `SpawnGoreResponse` will most likely give sound and decals but **no burst and no chunks**. Expect that in step 2 and report what you see. Options, in order of preference:

1. **Burst:** call `UNiagaraFunctionLibrary::SpawnSystemAtLocation` 0x04840630 yourself with the same Niagara system (the soft pointer `SpawnGoreResponse` loads with `LoadSynchronous`), location and rotation. This is the stock engine path, so no GameMode is needed.
2. **Chunks:** find out what the helper after the gate spawns (projectile class, and whether a pooled projectile replicates). Check that it is local-only before calling `UADProjectilePooler::SpawnProjectile` on the client. If that would take more than step 2's time box, ship the burst only and report the chunks as open.
3. **Do not** widen card 005's `GetGameMode` hook to return a fake GameMode here. The client has no `ASTGameplayGameMode` object or particle manager, and a fake one would reach every other GetGameMode caller in the gore path.

In the host trace (step 1), also log whether `GameMode+0x388` is non-null on the host. That confirms this gate is the only difference.

## Senior addendum 2 (2026-11-03): the corpse box collider

A static read for step 3. Corpse collision is not only the capsule; the NPC has a separate **corpse box collider**, the scene component at `NPC+0xA70`.

- `ASTNonPlayableCharacterBase::DisableCorpseBoxCollider` 0x05184EC0 saves the component's relative transform into `NPC+0xEA0..0xEFF`. It then calls `SetAbsolute`, a component virtual at +0x2C8 (likely `SetCollisionEnabled` or `SetVisibility`; check), and `SetWorldLocation` to a constant at 0x14710E760, which parks the box out of the way. `BeginPlay` 0x0517DB70 calls it, so every live NPC starts with the box parked.
- `EnableCorpseBoxCollider` 0x05186BF0 undoes that: `SetAbsolute`, the same +0x2C8 virtual, then `SetRelativeTransform` from `NPC+0xEA0`.
- `HandleDying` 0x05192BE0 (protected, right after `HandleDeathEvent` 0x05192BC0) runs as follows:
  - it calls `SetShowingStaticMesh`, cancels or plays barks, and updates a gameplay tag count;
  - if `+0xA28` is set, it calls `TryTakeCorpseTicket` (GameMode-gated, so a no-op on the client);
  - then, if `!IsNPC2D()`, it tail-jumps to `EnableCorpseBoxCollider`.
- `UpdateColliders` 0x051B2410 (virtual) does the following: if `!IsNPC2D()` and vtable +0x9E0 is true (probably "is dead"), it calls `EnableCorpseBoxCollider`; otherwise it calls `DisableCorpseBoxCollider`.

So on the host, death **turns the corpse box on** (most likely so corpses can be shot or gibbed) rather than turning collision off. The client calls only `TryPlayDeathAnimation` (card 038), so its corpse box probably stays parked. Measure first, then decide:

1. Log on both sides, 1 s after death: the `+0xA70` component's world location, collision enabled state and profile; the capsule's collision enabled state and profile; and whether `HandleDying` and `UpdateColliders` ran (detour, log only). Is the bug 2D (`IsNPC2D`) on each side?
2. If the host enables the box and the client does not, call `EnableCorpseBoxCollider` on the client's death edge. It touches only one scene component; it is a component move, not game logic. Do not call `HandleDying` (barks, tag counts, corpse ticket).
3. If the capsule still blocks on the client after death but not on the host, match the host's capsule state too.
4. The step 3 walk or shoot test now has two sides: a client can walk past the corpse as on the host, and a client shot hits the corpse box where a host shot does. A shot at a corpse on the client is replicated through the host anyway, so report what each side sees.

## Report

Result: **done, with the gib chunks left open.** On the client, an overkilled bug now gets the host's gore burst and overkill Niagara on the same bug. On both sides the corpse never blocks Pawn. Client game-logic counts are all 0.

The final run is `logs/card039x-{host,client}.log` and `logs/card039x-run.txt` (rc=0 on both sides, WARN 1 on the host and 0 on the client, no new crash dumps). Earlier runs: `card039w-*` (run 2) and `card039y-*` (run 3).

### Overkill trigger and call order (host, run 2 `logs/card039w-host.log`, about 513837578 ms)

At +0/+1 ms, in this order:
1. `HandleHealthValueChangedEvent` (Old 240, New -2500).
2. `HandleDeathEvent`, then `HandleDying`.
3. `SpawnGoreResponse` (chunks 9, niagara 1, sounds 1). This is the StartDyingState death burst, response +0x1B0.
4. `TryPlayDeathAnimation`.
5. `OverkillDamageEffectReceived` (Health -2500, OverkillThreshold 2500, dead 1, overkilled 1, state 3). The mod's host Gib signal goes out here, before the original runs: "host Gib signal ... cell 15494 (x 121 y 6) sent to 1 PCs".
6. 18 calls of `SpawnGoreResponse` with chunks 1 and niagara 0 (DismemberAtBone).

At +7 ms: `TryPlayOverkillAnimation`, then `StartOverkillState` (GameMode non-null, spawn manager `GameMode+0x388` non-null), then `ReleaseCorpseTicket`, then `SpawnGoreResponse` (chunks 16, niagara 1, sounds 1; response +0x230).

The host NPC EndPlay comes at +107 ms (reason 0, state 5).

Correction to run 1: run 1 reported "StartOverkillState never called". That came from the 128-line trace cap. Run 2 shows the call at +7 ms.

### Does SpawnGoreResponse spawn a replicated actor?

No replicated gore actor reaches the client. In run 2, the client's only `SpawnGoreResponse` calls were the mod's own (chunks 9 and 16, at 513837589). The chunks sit behind the second `GetGameMode` gate (0x14524FE3A, see senior addendum 1) and go through `UADProjectilePooler` deferred actions. No chunk actor shows up on the client from the host.

**Open:** the chunks are pooled projectiles, GameMode-gated, and were not ported. DismemberAtBone (18 single-chunk responses) is approximated by Gore OnOverkill plus Niagara.

### Does the client show the gib on the same bug?

Yes. Run 4, `logs/card039x-client.log`:

```
[ 514971989 ms][T  368] card039: host Gib signal BP_AI_WarriorBug_C BP_AI_WarriorBug_C at (-1654, 1624, 372) cell 15494 (x 121 y 6) sent to 1 PCs
[ 514971998 ms][T  940] card039: client Gib signal cell 15494 (x 121 y 6): BP_AI_WarriorBug_C BP_AI_WarriorBug_C at (-1654, 1625, 376); Health 280.0 OverkillThreshold 2500.0 dead 0 overkilled 0 state 2: Gore OnOverkill and Niagara; death burst (Gore OnDeath and Niagara); Niagara spawned 1
[ 514972301 ms][T  940] card039: client gib shot requested (+300 ms after the Gib signal)
[ 514972117 ms][T  940] card039: client NPC EndPlay BP_AI_WarriorBug_C BP_AI_WarriorBug_C reason 0; Health -2500.0 OverkillThreshold 2500.0 dead 1 overkilled 1 state 2
```

- The signal arrives 9 ms after the host sends it and matches the same bug: the host has it at (-1654, 1624), the client at (-1654, 1625).
- At that moment the client still has Health 280 and dead 0. Health arrives with the destroy, so the client death edge never runs for an overkilled bug, and the signal runs the death burst itself.
- "Niagara spawned 1" counts the overkill response (+0x230) only, because the counter is sampled after the burst. That matches the host's niagara 1 for the response.

Shots in `logs/card039-shots/`:

| File | What it shows |
|---|---|
| `run4-client-prekill-00563.png` | The bug before the kill, client view at 3522 uu. |
| `run4-client-gib300ms-00564.png` | Client, Gib signal +300 ms: a green gore cloud where the bug stood. |
| `run4-client-crop-before-after.png` | The same two shots, cropped 4x. |
| `run4-host-prekill-00562.png` | The bug before the kill, host view at 262 uu. |
| `run4-host-kill1s-00565.png` | Host, +1 s: a full gib with chunks. |
| `ScreenShot00480/481/482.png` | Run 2 host gib at +1, +2 and +4 s. |

The client shows the burst and cloud but no flying chunks (open, as above). The kill shots at +1.85 s and later show the bug gone; it is destroyed at +107 ms on both sides.

### Corpse collision, both sides, before and after

| Side | Box (`NPC+0xA70`) | Capsule | Blocks Pawn? |
|---|---|---|---|
| Host, +116 and +1005 ms | NoCollision 0 | `NPC_CapsuleCollider_IsDying` collision 3, responses 22000000 (Pawn ignored) | no |
| Host, +5 s (run 1) | Corpse 3, 22000000 | NoCollision | no |
| Client before the fix (run 1) | Corpse 3 | `Pawn` 3, responses 22220002 | **yes** |
| Client after the fix (runs 2 to 4) | Corpse 3, 22000000 | collision 0 (Custom), "capsule set to NoCollision" at the death edge | no |

The client mesh is CharacterMesh collision 1 (22020202) on both sides. Warrior bugs are not 2D (`IsNPC2D 0`, "box none") and are destroyed at about +107 ms, so the corpse question applies to the troopers that persist. The proof is a log, not a walk test: the probes at +16 and +1001 ms show no Pawn-blocking channel on either side.

### Risks from the card

- **Gore data offset.** It is read the way StartDyingState reads it:
  - StartDyingState: `mov rsi,[rdi+0xa90]` at 1451a86a7, `mov rdx,[rsi+0xd0]` at 1451a87ee, `add rdx,0x1b0` at 1451a8816.
  - Gore OnDeath: `mov rdx,[rdi+0xd0]` at 14519c797, `add rdx,0x1b0` at 14519c7ae, call 14524f880 (SpawnGoreResponse) at 14519c80c, with `rsi` (arg 2) as the location.
  - The overkill response is +0x230.
- **Late threshold.** OverkillThreshold (2500) is already on the client when the signal arrives. Health is not; it arrives with the destroy. No delay is needed, because the host signal decides the gib, not client state.
- **Gib plus collapse.** The host plays TryPlayDeathAnimation, then TryPlayOverkillAnimation 7 ms later, and destroys at +107 ms, so no collapse is visible. The client plays no death animation for an overkilled bug: its death edge never runs and the bug is gone at EndPlay. It matches the host.

### Done-when checks

| Check | Result |
|---|---|
| Client game-logic counts | All 0: "HandleDeathEvent 0, NPC StartDying 0, ... StartOverkillState 0, ReleaseCorpseTicket 0, HandleDying 0, OverkillDamageEffectReceived 0". Mod visual calls only: TryPlayDeathAnimation 20. |
| Build | `--clean-first` clean under `/W4 /WX`, 84 exports, KERNEL32.dll the only import. |
| Crash dumps | none new |
| Function length | longest 46 lines, all ≤60 |
| Rules | No StartOverkillState or HandleDying on the client. Card 005's GetGameMode hook is unchanged. Niagara goes through `SpawnSystemAtLocation`. |

### Changes

- `tools/symbols.txt`: added `ASTNonPlayableCharacterBase_EndPlay`.
- `mod/src/hooks.cpp`:
  - `ChanOp::Gib`, a one-argument cell payload (x<<7|y, 256 uu cells);
  - host send, once per NPC, inside `detour_overkill_effect`; client receive matches within ±1 cell to the nearest NPC not yet gibbed;
  - client burst and gib (Gore OnDeath and OnOverkill plus Niagara);
  - capsule set to NoCollision at the death edge; corpse box;
  - a log-only NPC EndPlay hook;
  - a client gib shot at +300 ms; card 038 kill shots now start from the Gib signal.

Small known gap: the client gibbed set holds 16 NPCs. Past that, a gib still plays but is not remembered, so a repeat signal could gib a neighbour. The host set also holds 16, and past that a repeat OverkillDamageEffectReceived on one NPC would send again. Both reset each map.

## Senior review (2026-11-03)

**Accepted.** Checked against the source, not only the report:

- `logs/card039x-run.txt`: rc 0 on both sides, host WARN 1 (probe table full) and client WARN 0, no new crash dumps.
- `hooks.cpp` mtime 17:59 is before the 18:08 run. The `build/dwmapi.dll` at 18:09 is the `--clean-first` rebuild of that same source; its hash differs from the deployed one only because of the link timestamp.
- The function-size check reports nothing over 60 lines.
- Host Gib send at 514971989 and the client match at 514971998 on the same bug: (-1654, 1624) on the host, (-1654, 1625) on the client.
- The client's live-trooper `UpdateColliders` lines ("box collision 3") also appear on the host (37 lines), so that is stock behaviour, not a side effect of the fix.
- `run4-client-crop-before-after.png` shows a green gore cloud replacing the bug.

The junior was right to replace "the client death edge decides the gib" with a host signal: Health arrives with the destroy, so the edge never runs for an overkilled bug. My card assumed the edge would see it.

Open points, none blocking:

- **Chunks**: no flying chunks on the client (pooled projectiles behind the GameMode gate). Next step if wanted: the gib projectile class and whether `UADProjectilePooler` works without GameMode.
- **Cell wrap**: the Gib payload is 7 bits per axis at 256 uu, so it wraps every 32768 uu. Two bugs 32768 uu apart on one axis, with the real one a cell off from client lag, could gib the far one. This is unlikely on current maps. Widen the payload if a big map shows it.
- **Capsule mismatch off the Pawn channel**: the client sets the dying capsule to NoCollision, while the host keeps `NPC_CapsuleCollider_IsDying` (collision 3, responses 22000000). They match for Pawn; other channels (camera, visibility traces) differ. Client traces are cosmetic, so this is accepted for now.
- **No walk test**: the corpse proof is a log only. A client walk through a trooper corpse is still worth one look in a later run.
- **16-entry gibbed sets**: on overflow, a repeat signal could gib a neighbour. Both sets reset each map.
