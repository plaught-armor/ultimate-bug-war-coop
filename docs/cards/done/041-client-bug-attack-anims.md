# Card 041: Bug attack animations on the client

Status: sent 2026-11-03.

Read `docs/STATE.md` first ("Mod code rules", "Runtime facts"), then card 038 (client death animation, `TryPlayDeathAnimation`) and card 039 (the `ChanOp::Gib` signal and the client's NPC match). Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

While watching card 040, the user saw that bugs do not play their attack animations on the client. Enemy AI runs only on the host (`USTAttackComponent`, `USTAttackAction*`), so the host starts attack animations and nothing tells the client. Bugs still move and die on the client, so locomotion and death work; only attacks are missing. This card finds the attack animation calls on the host and replays them on the client as visuals only.

## Static leads (senior, PDB publics and disassembly)

- `ASTNonPlayableCharacterBase::TryPlayAnimationSequence(FName name, bool force)` 0x051B0F30 is the single entry point for NPC animation. It has 26 call sites in 0x05150000–0x051B2000, among them `USTAttackActionPlayAnimation::DoAction` 0x05152F90 and the other `USTAttackAction*` stages.
  - It uses an animation player object at **NPC+0xA38**. The player's vtbl+0x18 is probably "ready", vtbl+0x48 returns the current sequence name, and vtbl+0x8 plays a sequence.
  - When `force` is 0 and the current name equals `name`, it returns without restarting.
  - Otherwise it binds two UFunction callbacks by name on the NPC, `ProcessFrameAction` and `AnimationEnded` (built from the strings `&ASTNonPlayableCharacterBase::ProcessFrameAction` and `&ASTNonPlayableCharacterBase::AnimationEnded`), and calls the player's vtbl+0x8 with the name and both callbacks.
- `TryPlayAnimationAgumentedByTime(FName, float)` 0x051B0C90 is a second entry point. Log it too.
- **Risk:** `ProcessFrameAction` probably fires the attack's frame events (the hit frame, projectile spawn, and so on), and `AnimationEnded` feeds `USTAttackActionBase::AnimationEnded` 0x0514B850 and `USTAttackComponent::OnAnimationEnd`. On the client those must do nothing that changes game state: no damage, no projectiles, no AI stage changes. Find out what they do before relying on them.

## Do

1. **Log the host's calls**, log only, behind `UBWCOOP_C41_ANIM_TRACE=1`. Detour `TryPlayAnimationSequence` and `TryPlayAnimationAgumentedByTime` on both machines. Log the NPC (class and short pointer), the sequence name, `force`, the current name before the call (player vtbl+0x48), the return address (the call site, so attack and locomotion calls can be told apart), and the player object's class. Cap the lines.
2. **Log the client's calls** in the same run. Answer:
   - Which sequences does the client already play by itself (walk, run, idle, death)? Which play only on the host? Those are the attack set.
   - Does the client's own locomotion call `TryPlayAnimationSequence` every tick or on a change? A relayed attack animation must not be overwritten on the next frame.
3. **Check the callbacks on the client.** Read `ProcessFrameAction` and `AnimationEnded` in the disassembly, and say what each does without authority. If either can deal damage, spawn an actor, or advance the AI, the client replay must avoid it. For example, call the player's vtbl+0x8 directly with empty callbacks instead of `TryPlayAnimationSequence`. Show the lines you used.
4. **Relay the attack set.** On the host, from the `TryPlayAnimationSequence` detour, send each attack-set call (a call site found in step 1 that is not locomotion or death) to the client on a new `ChanOp::Anim`. Identify the NPC the same way card 039 does. If a cheaper exact id is easy to get (for example a replicated id or the actor's net GUID), say so and use it. Send the sequence as a short string or as an index into a fixed table built from step 1, not as an FName index: FName indices are not guaranteed to match between the two processes. On the client, play it as in step 3. Keep payloads binary and small.
5. **Run.** A 2-player run where bugs attack both players. Take a screenshot pair of a bug mid-attack on host and client. Report: the attack sequence names seen, relayed counts sent and received, unmatched NPCs, and whether the client showed the attack (screenshots).

## Done when

- The client shows bug attack animations that match the host's, seen in screenshots.
- The client replay changes nothing but visuals: no damage, projectiles or AI changes on the client. Show that with the log, for example the client's `ProcessFrameAction` and `AnimationEnded` calls and what they did.
- The trace in steps 1–2 is behind `UBWCOOP_C41_ANIM_TRACE`; the relay in step 4 is always on.
- Clean build (`/W4 /WX`, `--clean-first`), 84 exports, KERNEL32-only imports. No new crash dumps. Every new function is at most 60 lines. Write `card041x-run.txt` with the rc.

## Risks

- The player at NPC+0xA38 may be a vertex-animation or other non-skeletal system. Its vtable slots above are inferred from one function; check them with the class name and a second caller before calling vtbl+0x8 yourself.
- Locomotion on the client may overwrite the relayed attack at once. If so, report how locomotion decides (step 2) before adding any hold logic.
- Card 039's cell match wraps every 32768 uu and picks the nearest NPC within ±1 cell. With many bugs attacking close together, it can pick the wrong one. Report mismatches in the log rather than hiding them.

## Senior addendum (2026-11-03): what the two callbacks do

Read from the disassembly after the card was sent.

- `ASTNonPlayableCharacterBase::ProcessFrameAction` 0x051A2F00 only broadcasts the multicast delegate at **NPC+0x950** (`FADAnimationDelegate`, wrapper 0x035ABA30).
- `ASTNonPlayableCharacterBase::AnimationEnded` 0x0517CC10 only broadcasts the multicast delegate at **NPC+0x960** (`FAnimationSequenceComplete`, wrapper 0x04B07560) with `EAnimationSequenceEnd` 2.
- So neither does anything by itself. What matters is who is bound to those two delegates on the client. A `TMulticastScriptDelegate` invocation list is a `TArray` of `{FWeakObjectPtr object, FName function}`. In step 3, log each bound object's class and function name on the client and on the host for a bug that attacks, then say whether any client listener changes game state. `USTAttackComponent::OnAnimationEnd` 0x0519B070 and the `USTAttackAction*` objects are the ones to watch. On the client no attack action should exist, so a listener there may find no current attack and do nothing; prove it.
- **An exact NPC id for step 4: the NetGUID.** Every replicated actor has an `FNetworkGUID` that is the same on the host and on the client; this is how the engine names actors in its own packets. On the host, `UPackageMapClient::GetNetGUIDFromObject` 0x03EA5E20 (vtable call on the client connection's `PackageMap`) or `FNetGUIDCache::GetNetGUID` 0x03EA5CD0 gives it. On the client, `UPackageMapClient::GetObjectFromNetGUID` 0x03EA6C70 (or `FNetGUIDCache::GetObjectFromNetGUID` 0x03EA6590, second argument `false`) resolves it. The `PackageMap` and `GuidCache` offsets on `UNetConnection` and `UNetDriver` are not known yet; read them from a caller in the disassembly. If this costs more than about one extra step, use card 039's cell match for now and say so in the report. A NetGUID would also fix the cell wrap and nearest-bug errors in card 039's gib match.

## Report

Done. The client now plays the WarriorBug's `Attack` as visuals only. Final run `logs/card041x-run.txt` (rc=0, no new crash dumps, build `56ed66ffa2c365ce`).

**Step 1-2: trace and attack set.** `UBWCOOP_C41_ANIM_TRACE=1` traces both TryPlay functions, the two NPC callbacks, `USTAttackComponent::OnAnimationEnd` and `USTAttackActionPlayAnimation::OnAnimationEnd`, with 30 s totals. Run 041b and 041x (spawned WarriorBug):

| sequence | host calls (forced) | client calls (forced) |
|---|---|---|
| Spawn | 22 (22) | 0 |
| Idle | 100 (100) | 37 (37) |
| Walk | 37 (0) | 2 (0) |
| Run | 22 (0) | 15 (0), 6 same-as-current |
| Death | 56 (56) | 21 (21) |
| Attack | 18 (18) | 0 |

- The attack set is `{Attack}`. Only the bug plays it, always forced, always through `TryPlayAnimationSequence` (ret 0x51a947f). `TryPlayAnimationAgumentedByTime` was never called.
- The client's WarriorBug never calls TryPlay on its own, so no client locomotion overrides the relayed attack. Trooper locomotion (`Run`/`Walk`, not forced) is called again even when it is already the current sequence, so it is not change-only.
- Also host-only, but not attacks: `Spawn`, and about half of `Death`. Not relayed.

**Step 3: listeners.** NPC+0x950 (ProcessFrameAction) and +0x960 (AnimationEnded):

- Host: +0x950 has 0 listeners everywhere. +0x960 has one listener, `FSM_EnemyBase_C . SMGraphK2Node_TransitionResultNode_*`, or `FSM_Enemy_Dying_C` while dying: the enemy FSM, i.e. game logic.
- Client: 0 listeners on both delegates, for troopers and for the WarriorBug, logged at relay time.
- Host `USTAttackComponent::OnAnimationEnd` fired 16 times (reason 2). On the client it fired 0 times in every run, and `USTAttackActionPlayAnimation::OnAnimationEnd` fired 0 times on both sides.
- Client ProcessFrameAction fired once, on the bug, 180 s after the last relay (ret 0x1424549, the same delegate executor as AnimationEnded). It is not from the relay, and it reaches 0 listeners.

**Step 4: relay.**
- New `ChanOp::Anim`, three arguments: NetGUID bits 0-13, bits 14-27, and table index * 2 + by-cell flag. The `ChanArgs` frame type and the parser now carry up to 3 arguments.
- Host: `detour_c41_try_play` (always on) sends when `name` is in `kC41AttackSeq` and will play (forced, or not the current sequence).
- NPC id: NetGUID from `UPackageMapClient::GetNetGUIDFromObject` on the PC's `NetConnection+0x68` PackageMap. The client resolves it with `GetObjectFromNetGUID` on its own connection's map, then checks `is_a` NPC. The cell fallback (card 039) only runs for GUID 0 or ≥ 2^28. It was never used (by cell 0): the bug's GUID 0x19e matched on every frame.
- Client: `UADAnimatorBaseComponent::StartAnimationSequenceCallback` (player vtbl+0x8) with two zeroed (unbound) `FADAnimationDelegate`s and rate 1.0, gated on `HasAnimationSequenceForKey(name)` (vtbl+0x18).

**A correction to card 041's inferred vtable.** vtbl+0x18 is `HasAnimationSequenceForKey(FName)`, not a no-argument ready check. Calling it without the name worked by chance on troopers (`UADSpriteAnimatorComponent`, vtbl 0x7dd3c08). It failed on the bug (`USTSkeletalAnimatorComponent`, vtbl 0x80d92f0): run 041x1 had 18/18 frames "not ready". The trace's "ready"/same-as-current figures from runs a and b use the old call. Run x uses the fixed call. vtbl+0x48 (current key) is a folded getter that returns the FName through a hidden pointer.

**Step 5: results (041x).**
- Host sent 18 frames, all by GUID. Client received 18, played 18, unmatched 0, not ready 0. Every receive line says `played visual only`, `attack end 0`.
- Screenshots: host ScreenShot00798/00801 show the bug mid-attack. The client shot (00799) was taken while it played, but the client camera was about 3,500 uu away (the client player had been killed by the bug), so the bug is only a speck. The user watched the pair and judged it working. The client screenshots alone are not proof.

**Gates.**
- `cmake --build build --clean-first` is clean (/W4 /WX).
- run_pair DEPLOY checked 84 exports and KERNEL32-only imports.
- No new crash dumps in runs a, b, x1, c and x.
- Every function is ≤ 60 lines (awk scan of hooks.cpp).
- The only WARN is the existing card039 "probe table full".

**Open.**
- The client camera needs to face the bug for a clean screenshot pair (spawn the bug by the client, or aim the shot like card023 does).
- The Spawn and Death host-only plays are unrelayed.

## Senior review (2026-11-03)

Accepted. Checked against the logs and code:

- `logs/card041x-run.txt`: build `56ed66ffa2c365ce`, host rc 0, client rc 0, no new crash dumps. Host totals: sent 18 (by cell 0). Client totals: received 18, played 18, unmatched 0, not ready 0; callbacks frame 1, attack end 0.
- The only WARN (host line 2099) is the existing card 039 "probe table full (32)".
- Every trace path checks `g_c41_trace`, which is read from config in `install()`. The relay in `detour_c41_try_play` and `c41_receive_anim` is always on. The function-size scan finds nothing over 60 lines.
- The code is sound: `c41_resolve` checks `is_a` NPC on the resolved object, `c41_receive_anim` bounds-checks the index, and the client play passes unbound delegates and gates on `HasAnimationSequenceForKey(name)`. Visual-only is shown: the client has 0 listeners on +0x950 and +0x960, and the host's only +0x960 listener is the enemy FSM, which the client never binds.
- The NetGUID route works and is exact (GUID 0x19e on every frame). That makes the card 039 cell match obsolete; move `ChanOp::Gib` to the GUID route in a later card.
- My mistake in the card: vtbl+0x18 is `HasAnimationSequenceForKey(FName)`, not a no-argument "ready" check. The junior found it from run x1 (0/18 played) and fixed it.

Open:

- There is no clean client screenshot of an attack. The client camera was about 3,500 uu away after the bug killed the client player. The user watched the pair live and judged it working; a later run with the bug spawned near the client should take the pair.
- The host-only `Spawn` and half of the `Death` plays are not relayed. Spawn may be worth relaying if bugs appear mid-pose on the client.
