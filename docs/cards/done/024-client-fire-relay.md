# Card 024: Relay the client's fire to its server copy's weapon, aimed from the client's view

Status: sent 2026-11-03.

Read `docs/STATE.md` first: the "Mod code rules" section, the "Runtime facts" line about `tools/run_pair.sh`, and rows 020 and 021. Then read card 021: the step 3 addenda (the `GetTargetLocation` and `ApplyDynamicZeroThresholdAdjustment` leads, with their fix sketches), your report, and the senior review. Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`. Run logs: `logs/card021l-{host,client}.log`. Symbols: `tools/symbols.txt`.

## Why

Card 021 found that the client fires only on its own machine. Weapons do not replicate, so the client's weapon ASC has no network path, and the server copy's weapon never fires. Nothing the client shoots can hurt anything the server owns. Milestone 2 needs the client's shots to exist on the server, start at the client's pawn, and aim along the client's view.

## Do

1. **Find a press and release signal on the server (log only first).** Hook these on the host for server-copy pawn-ASC traffic, and log handle, ability class and time:
   - `UAbilitySystemComponent::ServerSetInputPressed_Implementation` (0x04971B10);
   - `UAbilitySystemComponent::ServerSetInputReleased_Implementation` (0x04971B50);
   - `ServerEndAbility_Implementation` (0x04971770) and `ServerCancelAbility_Implementation` (0x04971410);
   - `InternalServerTryActivateAbility`, which is already in symbols.

   Count the handle 39 (`GA_Weapon_RecoilAbility`) requests per 2 s burst. The dedupe log showed only the first one. InputPressed on handle 39 stays 1 after the activation ends (`card021l` c20[50] and c20[51]), so it cannot mark the release. Report which of these give a clean press at burst start and a clean release at burst end.
2. **Relay.** Implement option 1 from your report. On the host, for a server copy only (LocalRole 3, RemoteRole 2):
   - **Press:** at the press signal, call `AbilityInputTagPressed(InputTag.Fire)` on that pawn's server-side weapon ASC (the equipped `AADWeapon` +0x2C0).
   - **Release:** call `AbilityInputTagReleased` (0x04A0EBE0) at the release signal.
   - **No release signal:** if step 1 finds none, release when no handle 39 request has arrived for 1.5 times the fire interval (about 900 ms). Say so in the report.
   - **Bad state:** never press while the weapon ASC is missing or the pawn is dead, and log and skip.
   - **Leftover hold:** on pawn destroy or weapon change, release any held relay.
3. **Aim.** Fix both host-view problems from the card 021 addenda in this card, following the sketches there:
   - **`GetTargetLocation`:** detour `ASTPlayerCharacterMI::GetTargetLocation_Implementation` (0x051C9D70) so that a server copy uses its own view. Use its camera, which card 021 step 4 now pitches from ControlRotation.
   - **Deprojection:** handle the deprojection inside `ApplyDynamicZeroThresholdAdjustment` (0x0522EDD0), at the branch at 0x0522F2AC. First log whether that branch runs for relayed shots.
   - **Do not** make `GetPlayerController(world, 0)` return the client's controller.
4. **Prove it.** In the run, have the client fire at the ground and then up (as in `card021l`). Then:
   - **Host log:** server-copy FireProjectile lines whose origin sits within about 100 units of the server copy's pawn, with pitch following the client's ControlRotation, and none at the host's camera.
   - **Count:** report the relayed shot count against the client's local count for the same bursts.
   - **Screenshots:** one per window during a burst. Report whether the host sees the client's shots (tracers or impacts), and whether the client sees any duplicate shots.
5. **Damage (stretch, log only).** If bugs are near both players by the end of the run, report whether a relayed shot changes a bug's health on the host. Do not add any client-side damage handling.

## Done when

- Relayed shots exist on the host for the server copy and start at the server copy's pawn. They aim from the client's view and stop within one fire interval of the client's release, all shown by log lines.
- The host's own fire and aim are unchanged: compare host-own FireProjectile origin and pitch rows with `card021l`.
- Move, look, jump, sprint, health sync and the camera pitch fix still work.
- Clean build (`/W4 /WX`, `--clean-first`), 84 exports, KERNEL32-only imports. No new crash dumps. Every new function is at most 60 lines.

## Runs

Build and run as in card 021, with `UBWCOOP_GRANT_WEAPON=MoritaMk1` on both sides. Never pass `-nosound`. Fix the `card020` FireProjectile side label on the client (it says `host-own` for the client's local weapon), since you are in that code.

## Risks

- **Double damage.** The client still fires locally. Those shots must not apply damage that disagrees with the server. If a client-local hit writes health on the client, report it. Do not suppress it yet.
- **Feedback loop.** The relay must not run for the host's own pawn, and the pawn-ASC request hooks must not catch the relay's own presses.
- **Stuck fire.** Losing a release would leave the server copy firing forever. The timeout fallback guards this even when a release RPC exists.

## Report

### Step 1 (log only): no clean press or release signal exists

Run `card024a` (126 s, both rc 0, no FATAL/ERROR/WARN, no new crash dumps). Hooks added, log only, host side filtered to server-copy owners: `ServerSetInputPressed_Implementation`, `ServerSetInputReleased_Implementation`, `ServerEndAbility_Implementation`, `ServerCancelAbility_Implementation`, plus the existing `InternalServerTryActivateAbility` detour. Client side logs local activations. Clean build, 84 exports.

Client bursts (autoinput, LeftMouseButton held 2000 ms each):

| Burst | Press | Release | Client local FireProjectile | Host handle 39 traffic |
|---|---|---|---|---|
| 1 `fire-ground` | 476488628 | 476490626 | 20 shots, 476488629 to 476490527 | 1 `InternalServerTryActivateAbility` at 476488635 (+7 ms) |
| 2 `fire-up` | 476491126 | 476493130 | 20 shots, 476491135 to 476493031 | none |

Findings:

- `ServerSetInputPressed`, `ServerSetInputReleased`, `ServerEndAbility` and `ServerCancelAbility` never fire for handle 39 in the whole run. The only server-copy hits are jump (31) and sprint (30) `ServerEndAbility`, plus slide (43), recoil (39) and melee (42) try-activates.
- Handle 39 gives a clean press for the first burst only (7 ms after LMB down). It gives no release. The server spec goes `ActiveCount 1 -> 0` about 470 ms after activation (476489104), still in the middle of the burst, while `InputPressed` stays 1 for the rest of the run. That end is server-local and is not tied to the release.
- The second burst sends no request at all. The client activates handle 39 locally only once (476488630). Its local recoil instance apparently stays active, so it never asks again. `card021l` shows the same pattern: one handle 39 request at the first burst, nothing for the second.
- The client's weapon fire interval is 100 ms (20 shots in 2 s), not 600 ms. The card's 900 ms timeout (1.5x) assumed 600 ms. With 100 ms the timeout would be 150 ms, and the existing traffic cannot keep anything alive that long.
- On the client, `AbilityInputTagPressed(InputTag.Fire)` runs every frame while LMB is held, on both the weapon ASC (handles 2, 22) and the pawn ASC (tag not in its map). That gives a clean client-side press edge and release edge, but nothing replicates it.

Conclusion: no existing pawn-ASC RPC carries the client's fire state well enough to drive the relay. A relay built on handle 39 would fire only the first burst, for one timeout, and miss every burst after that.

### Proposed fallback (needs a senior decision before step 2)

The client mod sends its own edges over an existing replicated RPC. On the client, detect the `InputTag.Fire` press edge (the first `AbilityInputTagPressed` after none in the previous frame) and the release edge (no pressed call this frame, or a hook on `AbilityInputTagReleased` 0x04A0EBE0). On each edge, call the pawn ASC's `ServerSetInputPressed(handle 39)` or `ServerSetInputReleased(handle 39)` RPC stub (not the `_Implementation`). On the host, the step 1 detours on those `_Implementation`s, for server-copy owners only, become the relay's press and release, with the timeout fallback kept as the stuck-fire guard.

The cost is a new client-originated RPC per edge. The server's own `_Implementation` sets the spec's `InputPressed` and fires the recoil ability's input events, which should be harmless while it is inactive, but this is unverified. An alternative is to repurpose a different handle or RPC; I have not found a better existing channel.

Steps 2 to 5 are not started. The step 1 hooks stay in place (log only, capped).

Side note: the client's `card024: client local activation` cap (120 lines) was used up by sprint retries (`handle 30 = 0, ability null`, 117 lines) before the bursts. If the instrumentation stays, it should skip failed activations with a null ability.

## Senior decision on the step 1 result (2026-11-03)

Verified in `card024a-host.log`: all four hooks are installed. `ServerEndAbility` fires for handles 30 and 31, so the hooks are live. Nothing arrives for handle 39 except one `InternalServerTryActivateAbility`. The finding stands, and so does the 100 ms fire interval.

Do not use `ServerSetInputPressed` or `ServerSetInputReleased` on handle 39. On the host, `_Implementation` runs `AbilitySpecInputPressed` or `AbilitySpecInputReleased` on the live recoil spec, and that spec's InputPressed is already stuck at 1 (card 020). Its input events and `WaitInputPress` / `WaitInputRelease` tasks are unverified, so this would change recoil behaviour as a side effect.

Use the mod channel from `docs/design/coop-channel-lobby-downed.md` section 1 instead. This card builds its first use.

- **Client send.** On the client's own pawn, detect the `InputTag.Fire` press and release edges in the existing `AbilityInputTagPressed` / `AbilityInputTagReleased` hooks. Send `\x1Fubw1:fire 1` on press and `\x1Fubw1:fire 0` on release. While held, resend `fire 1` every 250 ms as a heartbeat.
  - Sending: the local PC's `ServerChangeName` through `UObject::FindFunctionChecked` plus `ProcessEvent`. The generated `ClientMessage` stub at 0x0403B6FA to 0x0403B70D shows the pattern: vtable +0x260, with the parms struct holding one `FString`.
  - The `FString` must be engine-allocated or a correctly laid-out `TArray<wchar_t>` that the engine will not free. Explain your choice in the report.
- **Host receive.** Detour `APlayerController::ServerChangeName_Implementation` (0x0405C9B0).
  - If the string starts with the prefix, handle it and do not call the original. Otherwise call the original unchanged.
  - Cap the length at 64 characters.
  - Accept only `fire 0` and `fire 1`. Log rejects, capped at 10.
  - The pawn is the receiving PC's pawn, and the relay runs only if that pawn is a server copy (LocalRole 3, RemoteRole 2). Never take a player id from the payload.
- **Relay.**
  - `fire 1` from not-held: `AbilityInputTagPressed(InputTag.Fire)` on that pawn's server-side weapon ASC.
  - `fire 0`: `AbilityInputTagReleased`.
  - Guard: release if no `fire 1` heartbeat has arrived for 750 ms.
  - Never call `AbilityInputTagPressed` again on a heartbeat. Check whether the weapon ASC needs per-frame presses for full auto, as the client's local path does (`AbilityInputTagPressed` every frame while held). If it does, press on the host every frame while held, and say so.
- **Prove** that the prefix messages never reach `AGameModeBase::ChangeName`: the host and client player names are unchanged at the end of the run.
- Steps 3 to 5 are unchanged.

## Report, steps 2 to 5 (2026-11-03)

Runs: `card024b` (relay only) and `card024c` (relay plus aim). Both were pair runs with `UBWCOOP_GRANT_WEAPON=MoritaMk1` on both sides. Each ended by done after about 130 s, with rc 0/0, 0 FATAL/ERROR/WARN, both maps loaded and no new crash dumps. The logs are `logs/card024{b,c}-{host,client}.log`.

### Step 2: client fire over the mod channel

**Client send.**
- `c24_client_tag` runs in the `AbilityInputTagPressed` hook. It acts only on `InputTag.Fire` on a weapon ASC whose weapon Owner is the client's own MI pawn (kSideClientOwn).
- It sends `"\x1Fubw1:fire 1"` on the press edge and repeats it every 250 ms while held.
- `fire 0` is sent from the new `AbilityInputTagReleased` hook. A tick fallback also sends `fire 0` if no press has been seen for 200 ms.
- Sending uses `FindFunctionChecked(pc, FName "ServerChangeName")`, then `ProcessEvent` through vtable +0x260, with a parms struct holding one FString.

**FString ownership.**
- The FString points at a static, mod-owned `wchar_t[65]`, with Num = Max = length + 1.
- ProcessEvent does not take ownership. The engine's own `ClientMessage` stub (0x0403B500) frees its FString copy only after ProcessEvent returns, so the parms memory stays the caller's.
- On the client, a server RPC serialises the string during the call and keeps no reference. The engine never frees or reallocates our buffer.
- No engine allocation was needed, and no heap is used.

**Host receive.**
- The `ServerChangeName_Implementation` (0x0405C9B0) detour passes any string without the prefix to the original, unchanged.
- A prefixed string is handled and never reaches the original:
  - Length is capped at 64 characters, and the string must be terminated.
  - Only `fire 1` and `fire 0` are accepted. Rejects are logged, capped at 10; none occurred.
  - The pawn is the receiving PC's own pawn, and must be an MI server copy (LocalRole 3, RemoteRole 2).
  - No id is taken from the payload.

**Relay.**
- `fire 1` from the not-held state calls the original `AbilityInputTagPressed(InputTag.Fire)` on that pawn's equipped weapon ASC (`weapon+0x2C0`). It records `{pawn id, weapon id, last beat}`.
- A heartbeat only refreshes last beat.
- `fire 0` calls `AbilityInputTagReleased` on the weapon found by id through `GetWeaponBySlot`.
- The guard releases after 750 ms with no heartbeat, or when the equipped weapon changes. A relay whose pawn is gone is dropped and logged.
- Relays are cleared on map change.

**Per-frame question: not needed.** One press is enough for full auto on the host. `card024b` relayed 20 and 21 server-copy shots for the two 2 s bursts, against the client's local 21 and 21, at the 100 ms fire interval. `card024c` had 42 relayed shots against 40 local. The host does not re-press on heartbeats.

**Release.** In `card024c`, burst 1's last relayed shot was at 477702817 and the release at 477702825. No shot followed before the next press at 477703317. Burst 2's last shot was at 477705315, with the release at 477705323. Both stop well within one fire interval.

**ChangeName proof.**
- The PlayerState names were logged every 20 s on both sides.
- Host pc 0 shows `257` and host pc 1 shows `258` from login to the end of the run, through both bursts. The client pc 0 shows `258` throughout.
- The only other value is the pre-login default `Player`, logged once per side before the first name was assigned.
- No name ever changed, so the 22 channel messages per run did not reach `AGameModeBase::ChangeName`.

**Not exercised.**
- The 750 ms guard did not fire, because every release arrived.
- The weapon-change and pawn-gone paths did not run.

### Step 3: aim from the client's view

**The fix.**
- `ASTPlayerCharacterMI::GetTargetLocation_Implementation` (0x051C9D70) is detoured. Its `this` is the pawn + 0xA30.
- For a server copy only, it rebuilds the original's trace from that pawn's controller instead of player 0:
  - The view comes from `APlayerController::GetPlayerViewPoint`. On the server this is the camera cache that the client's `ServerUpdateCamera` fills.
  - end = view + forward × 10000.
  - The trace is `UWorld::LineTraceSingleByProfile` with profile `PlayerProjectile`, ignoring the pawn, using params from the engine's `FCollisionQueryParams` ctor.
  - The hit location is returned if bBlockingHit is set, else end.
- The params' two ignore-array heap pointers (+0x48, +0x68) are freed with `FMemory::Free`, exactly as the original frees them.
- Every other pawn calls the original.

**Deprojection branch (0x0522F2AC).** It never ran in either run: the muzzle translation before and after `ApplyDynamicZeroThresholdAdjustment` is identical for every server-copy shot. It is handled anyway:
- `detour_zero_adjust` sets a game-thread context pawn (server copies only) around the original.
- A `DeprojectScreenPositionToWorld` detour returns that pawn's view point while the context is set, and logs when it does: 0 lines in `card024c`.
- `GetPlayerController(world, 0)` is unchanged.

### Step 4: proof

**Origin.** In `card024c`, server-copy FireProjectile origins are (-859.4..-859.9, 4328.8..4329.5, 127.5..134.2). The client's local origins are (-859.3, 4327.9, 91.6), about 36 units lower in z and under 2 units apart in the plane. None are at the host camera.

**Aim, before and after.**

| | relayed shot yaw | client local yaw |
|---|---|---|
| Before (`card024b`) | -43.6 to -45.1 (host camera direction) | about 93.5 |
| After (`card024c`) | 93.3 to 94.8 | 93.1 to 94.6 |

**Pitch follows the client.** Sampled pairs, relayed against local:

| Burst | relayed pitch | client local pitch |
|---|---|---|
| 1 (view about 4° down) | -5.0 to -6.2 | -3.6 to -4.2 |
| 2 (view level) | -0.1 to -1.1 | +0.1 to -1.4 |

The trace target in `GetTargetLocation` moved from z 6.3 (a ground hit at about 1400 units) to z 108 (no hit) when the view levelled. The roughly 1.5° offset in burst 1 is muzzle-to-camera parallax toward a ground point.

**Shot count.** 42 relayed against 40 local in `card024c`; 41 against 42 in `card024b`.

**Host's own fire unchanged.**
- `host-own FireProjectile` and `ApplyDynamicZeroThresholdAdjustment`: 41/41 in `card024c`, the same as `card021l`.
- Host-own aim is unchanged: the detours call the original for non-server-copy pawns.

**Regression checks against `card021l`, host/client line counts.**

| Check | `card024c` | `card021l` |
|---|---|---|
| Server-copy camera fix | 3/0 | 3/0 |
| Health sets | 9/9 | 9/9 |
| GA_HumanJump | 10/7 | 7/6 |
| GA_Sprint | 110/62 | 106/60 |
| Client `rot[` rows | 499 | 499 |

**Screenshots: not usable.**
- A watcher took a full-desktop `spectacle` shot 0.8 s into each burst: `logs/card024c-burst1.png` and `logs/card024c-burst2.png`.
- The desktop was idle-dimmed, and another application's large window covered the client game window. Burst 2 shows no game content.
- They prove nothing. The log evidence above stands on its own. Retaking them needs an attended run with the two game windows unobstructed.

**Label fix.** `detour_fire_projectile` now takes the side from the weapon's Owner pawn when the avatar is a weapon. The client's local shots log as `client-own`, where they were `host-own` before.

### Step 5 (stretch): not reached

No bug was in range of the drop-zone burst; the relayed shots hit the ground at about 1400 units. There are no bug health readings, so the relayed-damage and client-local health-write (double damage) questions are still open. They need a run with a bug in front of the client.

### Build

- `cmake --build build --clean-first` is clean under `/W4 /WX`.
- 84 exports, KERNEL32-only imports.
- Longest function in `hooks.cpp` is 38 lines.
- The deployed DLL (`981b4b6b…`) was built from the same source. The hash differs from the clean-first rebuild (`68509b53…`); there were no source changes in between.

New symbols in `tools/symbols.txt`:
- ServerChangeName_Implementation
- AbilityInputTagReleased
- FindFunctionChecked
- FName(const TCHAR*, EFindName)
- GetWeaponBySlot
- GetTargetLocation_Implementation
- DeprojectScreenPositionToWorld
- GetPlayerViewPoint
- FCollisionQueryParams ctor
- LineTraceSingleByProfile
- FMemory::Free

### Open

- Screenshots, with an attended run.
- Step 5: relayed damage and client-local health writes, which need a bug in range.
- Guard, weapon-change and pawn-gone paths: not yet exercised. A `UBWCOOP_AUTOINPUT` step that drops the client mid-burst would test the guard.
- A pawn destroyed mid-hold has its relay dropped without a release call, since its ASC is gone too.

## Senior review (2026-11-03)

Accepted: steps 2 to 4. Step 5 and the guard paths are carried forward.

Verified in `logs/card024c-{host,client}.log`:
- No `Fatal` or `Error:` lines on either side.
- The host logs two relay press/release pairs (477700820 to 477702825 and 477703317 to 477705323). Server-copy FireProjectile lines inside those windows: 21 and 21, matching the report's 42. The first one aims at P -5.27, Y 93.65, from the server copy's origin, not the host camera.
- Player names: the host sees its own PC as `257` and the client's PC as `258`, and the client sees itself as `258`, for the whole run. Channel messages never reached `ChangeName`.
- The screenshots are unusable, as reported: an unrelated window covers both game windows.

Code read in `mod/src/hooks.cpp` (the mod channel and relay section): it matches the decision. The parms `FString` points at a static buffer the mod owns, the host acts only on the receiving PC's server-copy pawn, and the relay's own press bypasses the logging hook.

Carried forward:
- **Wire format (user decision, 2026-11-03).** The co-op is friends only, and the priority is fast, small payloads. Design doc section 1 now says: binary opcode frames, send on change only, no heartbeats, and only crash-safety checks. The `fire 1|0` text verbs, the 250 ms heartbeat, the 750 ms guard and the reject logging move to that format in the channel card. Until then they stay as they are.
- **Double damage (step 5)** moves to card 023, which already puts bugs near both players.
- **Screenshots** need an attended run; fold them into the next run the user watches.
