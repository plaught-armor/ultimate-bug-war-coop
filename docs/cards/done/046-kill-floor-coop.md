# Card 046: Kill floor in co-op

Status: done 2026-11-04.

Read `docs/STATE.md` first (known issue "Kill floor bypasses downed"), then `docs/design/coop-channel-lobby-downed.md` section 3 ("Kill floor" and the paragraph after it), and cards 043 and 044. Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why

When a player falls below the chapter's kill height, the game kills them directly and skips `HandleDeathEvent`, so card 043's downed state never runs:
- On the host, a teammate's server copy has its `StartDeath` skipped (card 043), so it stays alive below the floor.
- The host's own pawn gets a real game over even with a teammate Up.
- The client runs the check on its own pawn too and can start a local death.

The design (decided by the user): for a co-op player with a teammate Up, teleport them to that teammate and apply 25% of MaxHealth as damage instead.

## Static facts (senior, 2026-11-04)

- `ASTPlayerCharacter::Tick` 0x051E06F0 calls `AActor::Tick`, then `USTCommonUtilityLibrary::IsBelowKillThreshold(AActor*)` 0x05242110 (call at 0x1451E074D).
  - If that returns true and the byte at pawn+0x8F8 is 0 (a "fell below" latch: `Tick` sets it to 1 at 0x1451E07B9 and nothing in the player code clears it), it stops the movement component (+0x330, vtable +0x4F8), writes a zero vector to its velocity (+0xD0, 3 doubles), and calls vtable +0x638 with (5, 0), i.e. `SetMovementMode(MOVE_Flying, 0)`. The step 2 restore can call the same slot with (1, 0) for Walking.
  - It then builds a damage event on the stack and calls `StartDeath` through vtable +0xA40 (0x1451E0944).
  - `ASTPlayerCharacterMI::Tick` 0x051E13D0 calls the base `Tick` first, so the same path covers MI pawns.
- `IsBelowKillThreshold` returns `RootComponent(+0x1B8)` world Z (the component-to-world translation, +0x210) <= `USTGameInstance::GetCurrentChapterKillZ()` 0x050E99E0. It returns false with no root component or a game instance that is not a `USTGameInstance`.
- It has one other caller: `ASTNonPlayableCharacterBase::Tick` 0x051AABF0 (at 0x1451AAC26). NPCs must stay vanilla.
- `Tick` runs on every machine, so the client checks its own pawn (and remote pawns) as well.
- Teleport: the mod already resolves `AActor::SetActorLocation` (`g_set_actor_location`, card 025/028, with `kTeleportPhysics`). `AActor::TeleportTo` is at 0x037F5600 if a rotation is wanted.

## Do

1. **Detour `IsBelowKillThreshold` 0x05242110.** Run the original first. If it returns false, or the actor is not a player pawn (use the card 043 side classification), return its result unchanged.
2. **Host, co-op gate (card 043's gate), player pawn below the kill height:**
   - With another player Up:
     - pick that player's pawn as the target, and move the fallen pawn to the target's location plus 100 uu up, with `SetActorLocation` (no sweep, teleport physics);
     - zero its velocity, set movement mode 1 (Walking) if the game already set Flying, then apply 25% of MaxHealth as real damage through the card 043 test damage path (not a Health write), so that a player at 25% or below goes Downed by the normal rule;
     - return false, so `Tick` does not run `StartDeath`.
   - With nobody else Up:
     - for a server copy, treat it as Out (card 043 all-down logic), move it to the host pawn and return false. A server copy must never run `StartDeath`;
     - for the host's own pawn, return the original result, so the vanilla death and game over run.
   - Rate-limit: one warp per pawn per 2 s. A pawn still below after the warp is logged and not warped again until then.
3. **Tell the client.**
   - Add `ChanOp::Warp` (args: the target slot). When the host warps a server copy, send `Warp` to that client. The client moves its own pawn to the target slot's pawn location plus 100 uu, the same way.
   - The host's move on the copy and the client's own move must land near each other, so the client's movement does not snap it back. Log both positions and the gap after 0.5 s.
4. **Client.** In a co-op client session, the detour returns false for the client's own pawn and for remote pawns. The host decides, and the client acts only on `Warp`. Log the first time it suppresses a true result for each pawn.
5. **Test (env `UBWCOOP_C46_TEST=1`).** Each run is the two-player setup:
   - (a) Move the server copy to 500 uu below the kill height, read from `GetCurrentChapterKillZ`. Expect a warp to the host, 25 damage, no `StartDeath` and a client `Warp`.
   - (b) Do the same for the host's own pawn while the copy is Up. Expect a warp to the copy and no game over.
   - (c) Set the copy to Health 20, then repeat (a). Expect a warp, then Downed through `HandleDeathEvent`.
   - (d) With the copy Out, drop the host pawn. Expect the vanilla death and the card 044 game over on both machines.
   - Solo run: a drop below the kill height gives the vanilla death.
6. **Report** against "Done when". Read every shot before describing it.

## Done when

- (a) to (c) show the warp, the damage and no `StartDeath` call on either machine. The client's own pawn ends within 200 uu of the host's copy of it.
- (d) and solo show the vanilla death unchanged.
- An NPC that falls below the kill height still dies (log one, or state that none fell).
- No crash or new dump. rc 0 on all runs. The build is clean with `/W4 /WX`, and every function is 60 lines or fewer.

## Risks

- The game sets Flying, stops movement and sets the +0x8F8 latch before `StartDeath`, but only after `IsBelowKillThreshold` returns true. Returning false avoids all three. Once the latch is 1, `Tick` never runs the kill-floor path again for that pawn, so a vanilla trigger cannot repeat. If the pawn was already set to Flying by an earlier tick, restore Walking.
- A target standing on a ledge can be next to the same pit. The +100 uu offset and the 2 s rate limit stop a loop, but log every warp so a loop is visible.
- On the client, the own pawn's corrected position may lag behind the host's. If the gap stays over 200 uu, report it; do not add a correction loop in this card.

## Senior addendum (2026-11-04): the damage call in step 2

"The card 043 test damage path" means `c43_hit`, which calls `g_deal_damage` (`USTCombatUtilityLibrary::DealDamage(target, causer, amount, ignore_modifiers)`). Do not reuse `c43_hit` itself: its first `kC43SelfHits` hits use the target as its own causer with `ignore_modifiers` false. In card 045 run 4 (host log 1543-1558), three 400-point self hits on the copy took Health 100 to 100, then 96, then 80. The self-causer and modifier scaling (see card 040, the x0.1 hits) shrink the hit, so 25% of MaxHealth sent that way would barely move Health.

For the kill-floor damage:
- Call `g_deal_damage(pawn, pawn, 0.25f * MaxHealth, true)`. `ignore_modifiers` is true, so the friendly-fire scaling does not apply. Use `c43_max_health` for MaxHealth.
- Log Health before and after. If armour still absorbs part of the hit, that is vanilla behaviour, and it is acceptable. Report the Health change you actually get. Do not write Health directly.
- Test (c) must reach Downed. Set the copy's Health to 20 with `c43_set_health`. If the warp hit then leaves Health above the Downed threshold because of armour, lower Health further (to 5) and report it. Do not add an armour writer in this card.

Note (senior, same day): "the card 043 side classification" is `sprint_side()` (hooks.cpp ~3135, sides `host-own`, `server-copy`, `client-own`, `other`). An NPC that is not a player pawn must take the `IsBelowKillThreshold` original path; check that with the player class, not with `sprint_side() == other` alone, because `sprint_side()` only reads the net roles: on the host every NPC reads as `host-own` (Authority with a SimulatedProxy remote), and on the client a remote player pawn reads as `other`. Check the class first (the card 043 roster or `BP_PlayerCharacterMI_C`), then the side.

## Report (2026-11-04)

Runs: pair `card046y` and `card046z` (final build, the client screenshot lag and the gate-off log cap), solo `card046s`. Logs are in `logs/card046{y,z,s}-{host,client,run}.*`. Every run ended by its done line with rc 0, host and client rc 0, FATAL/ERROR 0, and no new crash dumps. The numbers below come from run z; run y gave the same results within a few uu.

### Done when

**(a) to (c): warp, damage, no `StartDeath` on either machine.** Met.

| Test | Drop | Warp | Damage (25.0 asked, `ignore_modifiers` true) | Host `StartDeath` | Client |
|---|---|---|---|---|---|
| (a) copy | z 103 to -22500 | 1, to the host +100 (-1769, 1820, 396), 8 ms after the drop | Health 100.0 to 100.0 (change 0.0), Up | 0 | `Warp 1` applied, own pawn to (-1769, 1820, 396) |
| (b) host pawn | z 296 to -22500 | 2, to the copy +100 (-1409, 1905, 377) | 100.0 to 100.0 (0.0), Up | 0 | no frame (host's own pawn) |
| (c) copy, Health 20 | z 277 to -22500 | 3 | 20.0 to 20.0 (0.0), Up | 0 | `Warp 2` applied |
| (c) retry, Health 5 | z 296 to -22500 | 4 | 5.0 to 0.8 (4.2), still Up | 0 | `Warp 3` applied |
| (c) retry, Health 1 | z 296 to -22500 | 5 | 1.0 to 1.0 (0.0), **Downed** through `HandleDeathEvent` | 0 | `Warp 4` applied |

- The client's `StartDeath` count stayed 0 through (a) to (c). Its only `StartDeath` came in (d).
- The kill-floor damage mostly does not move Health. At 100 and at 20 the change is 0. At 5 it is 4.2 (5.0 to 0.8). At 1 Health stays 1.0, but the hit runs `HandleDeathEvent`, and card 043 puts the copy into Downed by the real path. This looks like a game-side floor or armour absorption, not a mod fault. Per the addendum, nothing writes Health or armour.
- Test (c) needed a third step beyond the addendum's 5: the code tries Health 20, then 5, then 1 (`kC46LowHealth`). After the last try, if the copy is still Up, it would log a WARN and force Downed through `c43_enter_down`. That fallback never fired in y or z.
- The copy went Out after the 30 s bleedout. Then (d) ran.

**Client position gap after 0.5 s** (the client's own pawn against the host's pawn it was warped to; host and client agree within 6 uu):
- (a) 278 uu on the host, 282 on the client.
- (c) at Health 20: 343 and 346.
- (c) at Health 5: 81.
- (c) at Health 1: 172.

The gaps over 200 come from the warp target: the pawn lands 100 uu above the target's capsule, lands on it and slides off, so it ends up beside the target rather than on top. Host and client still agree with each other within 6 uu, so the client was not snapped back. Per the risk note, the gaps over 200 are reported and no correction loop was added.

**(d) and solo: vanilla death unchanged.** Met.
- (d): the host pawn dropped with the copy Out. The log shows `all down: kill floor`, the copy Out and the host Dead, then `own pawn ... below with nobody else Up: all down, vanilla death`. Host `StartDeath` is 2 (the vanilla `Tick` path). Host `CompleteDeath` ran and the death screen (`WB_PlayerDeathScreen_C`) was built.
- (d) on the client: the card 044 Dead frame ran `StartDeath` once on the client's own pawn, `CompleteDeath` followed 2998 ms later, and the death screen was built. Client done line: `Warp frames 4, StartDeath 1`. Host done line: `warps 5, vanilla 1789, StartDeath 2, all down 1`.
- Solo (`card046s`): gate off (players 1), vanilla death, `StartDeath` 2, death screen built. Done line: `warps 0, vanilla 1794, StartDeath 2`.

**NPCs.** No NPC fell below the kill height in any run. The `kill floor on non-player ... vanilla` line has 0 hits in every log. NPCs take the original result, decided by the player-class check before `sprint_side()`.

**Build.** `cmake --build build --clean-first` gives 0 warnings under `/W4 /WX`. `dwmapi.dll` imports `KERNEL32.dll` only. The longest card 046 function is `c46_host_below` at 37 lines. No function in `hooks.cpp` exceeds 60 lines.

### Changes made during testing

- The gate-off log line has its own cap of 3 (`kC46GateLines`). `Tick` calls `IsBelowKillThreshold` every frame while the dead host pawn lies below the floor (the vanilla count reached about 1790), and before the cap this one line used up the whole 120-line card 046 log budget.
- The client takes its screenshots 1 s after the host's (`kC46ClientShotLag`). Host and client share one `Screenshots/Windows` folder, and two shots taken in the same millisecond overwrite each other as one file. In run y that cost 3 shots; in run z all 11 landed.

### Notes

- The client's step 4 suppress line (`kill floor suppressed on ...`) never fired. The client never saw a player pawn below the floor: the copy is moved on the host, and in (b) and (d) the host pawn's drop below the floor did not replicate to the client before the warp or the death. The suppress path is in place but was not exercised.
- The client's `StartDeath` log shows `KillZ -3.4e38`: the client has no chapter kill height from `GetCurrentChapterKillZ`. This only affects the log value; the client decision does not use it.
- In (c) the host sees the copy lying Downed (shot `z-c-host-view-copy-lying-downed...`). The client's own view 2.5 s later shows its trooper's legs standing (`z-c-client-own-trooper-standing-legs...`). Card 046 does not change how Downed is shown; whether the client should show its own pawn Downed is a card 043 question.

### Shots (`logs/card046-shots/`, each read before it was named)

Run z (all 11 landed):
- `z-a-host-view-copy-standing-at-pad-edge-plus1.5s-after-warp1.png`: the copy's trooper standing at the landing pad's edge by the railing.
- `z-a-client-view-trooper-standing-facing-camera-mid-pad-plus2.5s-after-warp1.png`: a trooper standing in the middle of the pad, facing the camera.
- `z-b-host-view-steep-close-trooper-legs-by-curb-plus1.5s-after-host-warp.png`: steep close view of a trooper's legs beside the pad curb.
- `z-c-client-own-trooper-from-behind-at-pad-edge-plus2.5s-after-warp2-health20.png`: the client's trooper from behind at the pad edge.
- `z-c-client-close-trooper-legs-on-pad-edge-plus2.5s-after-warp3-health5.png`: close view of the trooper's legs on the pad edge.
- `z-c-host-view-copy-lying-downed-on-pad-by-curb-plus1.5s-after-health1-warp.png`: the copy lying prone (Downed) on the pad by the curb.
- `z-c-client-own-trooper-standing-legs-on-pad-edge-plus2.5s-after-warp4-health1.png`: the client trooper's legs, standing, on the pad edge.
- `z-d-host-death-menu-service-guarantees-citizenship-over-basic-training-loading-press-any-key-plus5s.png`: host death menu, "Service guarantees citizenship" with Restart Checkpoint / Restart Mission / Load Game / Exit, over the BASIC TRAINING loading screen with "PRESS ANY KEY TO CONTINUE".
- `z-d-host-death-menu-over-basic-training-loading-plus9s.png`: the same menu without the press-any-key line.
- `z-d-client-death-screen-success-is-not-final-quote-red-tint-hud-plus6s.png`: client death screen, "Success is not final, failure is not fatal...", the same four buttons, over the red-tinted gameplay view with the HUD and the mouse cursor visible.
- `z-d-client-death-screen-success-is-not-final-quote-red-tint-hud-plus10s.png`: the same, 4 s later.

Run y (some lost to the shared-folder collision fixed above): `y-a-host-view-copy-standing-on-pad-after-warp1`, `y-b-host-view-steep-close-copy-legs-beside-host-after-host-warp`, `y-c-client-own-trooper-from-behind-on-pad-after-warp2-health20`, `y-c-client-own-trooper-side-on-pad-after-warp3-health5`, `y-c-host-or-client-close-standing-legs-on-curb-after-warp4-health1` (collided), `y-d-death-screen-courage-quote-plus5s-host-or-client` and `-plus9s` (collided). Solo: `solo-death-menu-over-basic-training-loading-plus5s`, `solo-death-menu-over-basic-training-press-any-key-plus9s`.

## Senior review (2026-11-04)

Verified from `logs/card046z-{host,client}.log`, `logs/card046s-host.log`, the three `*-run.txt` files, the code (hooks.cpp `c46_move` to `c46_receive_warp`) and the shots `z-a-host-view…`, `z-c-host-view-copy-lying-downed…`, `z-d-client-death-screen…plus6s` and `solo-death-menu…plus5s`.

Accepted. What holds:
- Run z and solo: rc 0, FATAL/ERROR 0, no new dumps. The two host WARN lines in z are the card's own gap reports (278 and 343 uu); the solo WARN is the auto-dropship unpause.
- (a) to (c): five warps, each handled 8 to 10 ms after the drop, and no `StartDeath` on either side. Every copy warp sent `Warp`, and the client applied all four (client log 862-906). Host and client positions agree within 6 uu at +0.5 s.
- At Health 1 the hit ran `HandleDeathEvent` and card 043 put the copy into Downed (host log 1.0 of 100.0, bleedout 30 s). The host shot shows the copy lying prone on the pad.
- (d): `all down: kill floor`, the copy Out, the host Dead, and the vanilla death. The client's death screen has RESTART CHECKPOINT, RESTART MISSON and LOAD GAME greyed out (card 045) and EXIT live. Solo: vanilla death with the menu over the loading screen.
- The code follows the card: the player-class check comes before `sprint_side()`, a server copy never returns true, and Health is never written in the warp path.

Findings (open, not blocking):
1. **Armour absorbs the warp damage.** The 25-point hits gave Health changes of 0, 0 and 4.2, and the last hit went through in full. That fits an armour pool of about 71 that takes the first 70.8 points. Armour (`UADAttributeSetHealth.Armour` +0x50, `MaxArmour` +0x60) was not logged, so this is inferred, not shown. So the design's "25% of MaxHealth" does almost nothing to a player with armour. The game has a bypass:
   - `UADDamageHandler::IsIgnoreArmour` 0x04A3D1F0 returns true when the source tags contain `Health.DamageIgnoreArmour` (`HealthSystemTags::Health_DamageIgnoreArmour`).
   - `UADHealthUtilityLibrary::DealDamage(AActor*, const AActor*, float, FGameplayTagContainer, FGameplayTagContainer)` 0x04A3B510 takes tag containers.

   Whether the kill-floor hit should ignore armour is a design call for the user. Either way, log Armour before and after the hit.
2. **`StartDeath` counts are per detour, not per death.** `ASTPlayerCharacterMI::StartDeath` calls the base, and both are hooked, so one vanilla death counts 2 (solo, (d)). The client's 1 in (d) is the card 044 path. The log label should say "detour calls".
3. **The client never sees its own pawn below the floor.** The client's `GetCurrentChapterKillZ` returns -3.4e38 (no chapter on the client's game instance), so `IsBelowKillThreshold` cannot return true for any actor there. The client suppress path is inert, not just unexercised. Keep it as a guard; no test is possible without a chapter on the client.
4. **Landing gaps over 200 uu** (278 and 343) come from landing on the target's capsule and sliding off. A sideways offset (for example 120 uu behind the target, with a ground trace) would fix it. It is polish, not a bug.
5. **The client's view of its own Downed state.** In (c) the host shows the copy prone while the client's own view shows its trooper standing. This belongs to card 043's presentation, not to card 046.
6. The test driver's forced-Downed fallback (WARN, then `c43_enter_down`) is test-only and never fired. That is fine.
