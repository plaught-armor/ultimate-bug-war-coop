# Card 038: Enemies die on the client

Status: sent 2026-11-03.

Read `docs/STATE.md` first: the "Mod code rules" section, the known issue "Enemies do not die on the client", and rows 023 and 037. Then read card 037's report and senior review. Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

Card 037 made enemy health replicate, but death runs on the authority only. On the client a bug the host kills stays standing with Health ≤ 0 (`logs/card037-shots/`). Card 023's "death pose shows on the client" was a host screenshot. The client needs the death visual, and only the visual.

## Static leads (senior, from disassembly; check them)

- `ASTNonPlayableCharacterBase::HandleDeathEvent(const FADDamageEvent&)` 0x05192BC0, game-side death. Callees, in order: `USTSkeletalAnimatorComponent::SetShowingStaticMesh`, bark cancel and `PlayBark`, `FGameplayTagCountContainer::UpdateTagMap_Internal` (most likely `Tag_Corpse`), `TryTakeCorpseTicket`, `IsNPC2D`, and a multicast broadcast (the health component's OnDeath delegate).
- Delegate listeners with natives: `USTBugAnimPhysComponent::OnDeath(const FADDamageEvent&)` 0x051C3460 (bound with `AddDynamic`), and `USTGoreComponent::OnDeath(const FVector&)` 0x0519C650.
- `ASTEnemyBase::StartDying` 0x051A82C0 is **game logic, not visuals**: `StartStateMachine_Dying`, `USTMetaGame::EnemyKilledByGameplayTag` (kill score), `ReleaseAllTickets`, `ASTAIAwarenessSystem::DeregisterAI`, and `ReceiveStartDying` (blueprint). Never call it on the client.
- `ASTNonPlayableCharacterBase::TryPlayDeathAnimation` 0x051B11E0 picks a death key and calls `TryPlayAnimationSequence(FName, bool)` 0x051B0F30. It branches on `IsNPC2D`, so it serves the sprite NPCs (AI troopers). It may do nothing for the bug.
- `FWarriorBugAnimInput` has a `bIsDead` field. The bug's anim input may read it, not health.

## Do

1. **Find what the host's bug death runs (host, log only).** Hook `USTBugAnimPhysComponent::OnDeath` and `USTGoreComponent::OnDeath`, plus `TryPlayDeathAnimation`, and log each call with the actor and the time relative to `HandleDeathEvent`. Log `bIsDead` in the bug's anim input on both sides before and after the death, if you can find its offset.
2. **Pick the smallest client call.** On the client, when card 037's notify sees a non-local NPC's Health cross from > 0 to ≤ 0 (once per actor), call the visual entry point that step 1 shows the host using. Preferred order: `USTBugAnimPhysComponent::OnDeath` with a zeroed `FADDamageEvent` (state the size you used and why), then `TryPlayDeathAnimation` for sprite NPCs (AI troopers). Gore is optional; say whether it plays.
   - Never call `HandleDeathEvent`, `StartDying`, `ReceiveStartDying`, or anything that touches score, tickets, drops, AI registration or `Tag_Corpse` on the client.
   - The edge fires once per actor. Track the actors that have died in a fixed-size set (no heap), and clear it on travel.
3. **Troopers.** Card 037 showed the host's InstaKill clear as 20 `50.0 -> 0.0` notifies on the client. Report whether those troopers now fall on the client, and whether any of them already fell before this card (a sprite death may already replicate through the animation key).
4. **Runs.** Use card 037's pair command. Turn `UBWCOOP_NO_ENEMY_HEALTH=1` off. Give host and client shots separate wall-clock slots (client 1 s later), and add a client kill shot at +2 s and +10 s after the client sees the death edge. Copy the cited shots into `logs/card038-shots/`.

## Answer, with log lines or screenshots

- Which natives run on the host when the bug dies, in what order?
- Which call did the client use, and does the client's bug collapse (shot at +2 s and +10 s)?
- Does any game logic run on the client? Check the score, ticket and AI-awareness callees: zero calls expected.
- Do the client's troopers fall after the host clear?

## Done when

- Client screenshots show the killed bug collapsed, and the host's view matches.
- No `StartDying`, `HandleDeathEvent` or `EnemyKilledByGameplayTag` call on the client (log the counts).
- `install()` is back within 60 lines, counting from the signature line (card 037 review). Move the `g_cXX = config::read_...()` block into a helper.
- Clean build (`/W4 /WX`, `--clean-first`), 84 exports, KERNEL32-only imports. No new crash dumps. Every function is at most 60 lines, counted from the signature line.

## Risks

- `USTBugAnimPhysComponent::OnDeath` may read fields of the damage event (hit direction, bone, impulse). A zeroed event may crash or give a flat fall. If it reads the event, carry the host's event fields instead, or report what it reads and stop.
- The collapsed bug keeps its client collision, which may block the client's movement or shots. Report it; do not change collision in this card.

## Senior addendum (2026-11-03, after sending)

`USTBugAnimPhysComponent::OnDeath` (0x051C3460) is six instructions. It never reads the damage event (`rdx` is unused). It reads a 32-bit state at component +0x430, and when that state is outside 0x10-0x1F it writes 0x10, which is most likely "dying". The fall is then played by the component's own tick from that state. Consequences:
- A zeroed or dummy `FADDamageEvent` reference is safe for this call. Pass a pointer to a zeroed stack buffer anyway, in case a hook or a later override reads it.
- Log +0x430 on the client's bug before and after the call, and on the host's bug around `HandleDeathEvent`, so we learn the state values.
- If the client bug still stands after the state becomes 0x10, check whether the component ticks on the client (its tick flag, or a log in its tick) before trying anything else.
- Later static read: `USTBugAnimPhysComponent::Die()` shares the same code (0x051C3460, a no-argument public; prefer it over `OnDeath`). `TickComponent` (0x051E2BA0) drives the rest: state 0x10 sets the byte at +0x428 (likely the anim input's dead flag) and moves to 0x11; 0x11 waits for a flag in the anim instance (+0x360, bytes from +0x4A9), then moves to 0x12 and calls four virtuals on the mesh at +0x198 (most likely physics and ragdoll setup). So the whole fall runs from the component's tick once the state is 0x10.

## Report

Runs: `logs/card038{a,b,c,d,e}-run.txt` (summaries appended). Final run is **card038e** (build `78fcab9518b89f09`): done after 286 s, rc 0/0, FATAL/ERROR 0, WARN 0 both sides, no new crash dumps. Shots: `logs/card038-shots/`.

**Host natives on death, in order** (log only; card038a/e host lines, same ms unless noted):
1. `HandleDeathEvent`
2. `ASTEnemyBase::StartDying`
3. `TryPlayDeathAnimation` (+0 or +1 ms after `HandleDeathEvent`)
4. `EnemyKilledByGameplayTag` (on `BP_MetaGame_C`)
5. `ReleaseAllTickets`
6. `DeregisterAI`
7. `TryTakeCorpseTicket`

The bug shows the same order: `[510863884] card038: host TryPlayDeathAnimation BP_AI_WarriorBug_C ..., +1 ms after HandleDeathEvent`.
- Host `USTBugAnimPhysComponent::OnDeath` was never called (0 lines in all 5 runs). Host Gore `OnDeath` was never called either (0 lines). The NPC-base `StartDying` detour did not fire.

**Client call.**
- On card 037's Health edge from > 0 to ≤ 0, once per actor: `is_a` NPC check, mark dead (fixed set of 128, cleared on map arm). Then:
  - if a BugAnimPhys component exists, call its `OnDeath` with a zeroed, aligned 0x100 event (per the addendum it only sets +0x430 to 0x10);
  - otherwise call `TryPlayDeathAnimation`, the call the host makes.
- `BP_AI_WarriorBug_C` has **no** USTBugAnimPhysComponent (`anim 0000000000000000 state 0xffffffff`), so the client used `TryPlayDeathAnimation` for the bug and for all troopers.
- The BugAnimPhys branch is unexercised. Its +0x430 tick check is not applicable. The `bIsDead` offset was not found.
- Client edge: `[510864037] card038: client death edge BP_AI_WarriorBug_C ... Health 260.0 -> 0.0 at (-1652, 1624, 384): TryPlayDeathAnimation; ... (gore component present, not called: the host never calls Gore OnDeath)`. That is 154 ms after the host `HandleDeathEvent`.

**Does the client's bug collapse? Yes.** Card038e client shots, about 3500 units from the bug:

| Shot | When | What it shows |
|---|---|---|
| `ScreenShot00395` | −2 s | standing |
| `ScreenShot00398` | kill shot 1, +2001 ms | rearing: death anim started |
| `ScreenShot00401` | +8 s | collapsed on the ground |
| `ScreenShot00403` | kill shot 2, +10005 ms | collapsed on the ground |

- Crop: `card038e-client-bug-crop-395-398-401-403.png`. Card038d (an earlier build) shows the same: 352 standing, 356 (+5 s) and 359 (+15 s) collapsed.
- Actor location is unchanged on the client at +2 s and +10 s: (-1652, 1624, 384).
- Host view: `ScreenShot00396` (+1 s) shows the bug gibbing (green gore). `00402` (+10 s) shows no bug in view.
- **Mismatch:** the host gibs, the client shows an intact collapsed body. Gore on the host comes from something other than Gore `OnDeath` (never called there), so the client Gore call (tried in card038a/d) was removed.

**Game logic on the client: zero.**
`[510874042] card038: client after the last kill shot: game-logic calls HandleDeathEvent 0, NPC StartDying 0, ASTEnemyBase::StartDying 0, EnemyKilledByGameplayTag 0, ReleaseAllTickets 0, DeregisterAI 0, TryTakeCorpseTicket 0; mod visual calls BugAnimPhys OnDeath 0, TryPlayDeathAnimation 21; dead tracked 21`

**Troopers.**
- After the host clear, the client got 20 trooper edges (`50.0 -> 0.0`) and called `TryPlayDeathAnimation` on each (21 total = 20 troopers + 1 bug).
- The troopers are about 8000 units from the client camera, so no shot shows them: **falling is not visually verified**.
- Whether any fell before this card: unknown, no evidence either way.

**Collision:** not changed and not measured. The collapsed bug stays at its spawn location on the client.

**Code and build.**
- `install()` is 56 lines; the `g_cXX = config::read_...()` block moved to `read_card_flags()`.
- Every function is at most 60 lines (scan of `hooks.cpp`). Touched functions: `c38_death_edge` 29, `c37_pawn` 22, `c37_step` 21.
- Clean `/W4 /WX --clean-first`, no warnings, 84 exports, KERNEL32.dll-only imports.
- Hooks added: 8 `kHooks` (3 host log-only death paths + 5 game-logic count detours) and 2 `kCalls` StaticClass entries.

**Runs that did not reach the goal:**

| Run | Setup | Result |
|---|---|---|
| card038a | no client fire | bug not killed before the cap |
| card038b | client `UBWCOOP_AUTOINPUT=1` | the script ended and the client exited before the bug spawned; host WARN "card023: host kill due but no enemy is listed -- no kill" |
| card038c | `UBWCOOP_AUTOINPUT_DELAY_MS=75000` | presses never relayed; fallback kill after the client left |
| card038d | — | collapse shown, but the card038 kill shots did not fire (gated on a BugAnimPhys component); fixed in card038e |

## Senior review (2026-11-03)

Accepted.

**Verified.**
- `logs/card038e-run.txt`: rc 0/0, WARN 0, FATAL/ERROR 0, no new dumps.
- Shot mapping, checked by wall-clock offset from the host's kill shot 1 (00396 at 16:57:09): 00398 and 00403 are the client's kill shots at +2 s and +10 s, and 00396 and 00402 are the host's. The four-frame crop shows standing, rearing, then collapsed twice.
- The client's game-logic counts line reads 0 for every logic path, with 21 `TryPlayDeathAnimation` calls.
- A function-size scan of `mod/src/*.cpp` from the signature line finds nothing over 60 lines. `install()` is fixed.

**Code read.**
- `c38_death_edge` runs only from `c37_pawn` on `last > 0 && now <= 0`, after the notify. It is gated by the NPC `is_a` check, so the other player's pawn never gets it.
- The dead set is fixed at 128 entries. It is keyed by pointer plus name, so a reused address with a new actor does not match, and it is cleared on map arm. A full set is logged once as WARN.
- One gap, accepted: if the same scan both adopts the set and crosses zero, the edge is skipped (`!adopted`). Adoption happens on the first scans after spawn, so a pawn would have to die within about 250 ms of becoming visible.

**My static lead was wrong.** `BP_AI_WarriorBug_C` has no `USTBugAnimPhysComponent`, and the host never calls its `OnDeath`. The host's bug death goes through `TryPlayDeathAnimation`, like the troopers'. The junior followed the host's order rather than the addendum, which was the right call. The BugAnimPhys branch stays in the code but has never run; treat it as untested.

**Open.**
- **Gibs:** the host gibs the bug, while the client shows an intact collapsed body. Gore does not go through `USTGoreComponent::OnDeath`. Likely candidates: the health set's overkill threshold (`OverkillThreshold` is replicated since card 021) in the damage path, or a blueprint event from `HandleDeathEvent`'s broadcast. This is cosmetic but visible; it needs its own card.
- **Troopers:** 20 client edges and calls are logged, but no shot shows them falling (8000 units away).
- **Collision:** the client's corpse collision is unchanged and was not measured. The host removes it (corpse ticket); the client copy may still block movement or shots.
- `bIsDead` offset: not needed for now.
