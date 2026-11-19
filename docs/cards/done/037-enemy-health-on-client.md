# Card 037: Enemy health on the client

Status: sent 2026-11-03.

Read `docs/STATE.md` first: the "Mod code rules" section, the two replication notes ("The game's own native code replicates nothing", "Replicating a game property"), the known issue "Enemy health does not replicate", and rows 021 and 023. Then read card 021's step 5 report and card 023's report and review. Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

Card 023 found that enemies spawn, move, animate and fall dead on the client, but the client's health value never changes: 400 while the host goes to −360. The client's enemy health bar, and any client-side check of enemy health, therefore sees full health.

## Senior hypothesis (static, check it first)

Card 021 already ORs `CPF_Net` into the `UADAttributeSetHealth` properties (Health, MaxHealth, Armour, MaxArmour, OverkillThreshold) on both machines at engine init (`c21_mark_health_net`). That change is class-wide, so it covers enemies too. For the client's own pawn, card 021 found **two** health sets:
- the one the component points at, `UADHealthComponent::HealthAttributeSet` (+0x130), which is local and never updates;
- the replicated one, in the ASC's `SpawnedAttributes`.

The fix in `c21_adopt_replicated_set` repoints the component, **for the client's own pawn only**. Card 023's audit read the set at component +0x130, so it most likely read the local copy on enemies as well. If so, the replicated value is already arriving and only the adoption is missing.

## Do

1. **Diagnose (log only).** On the client, for the spawned bug (`UBWCOOP_SPAWN_BUG=1`) and for a few AI troopers, log the health set at component +0x130 and every `UADAttributeSetHealth` in the ASC's `SpawnedAttributes`, with pointers and the Health current and base values. Reuse `c21_dump_health_sets`. Repeat after the host damages the bug. Report which set moves.
2. **Adopt (if step 1 confirms it).** Extend the adoption to every non-local pawn on the client that has a `UADHealthComponent`: enemies, AI troopers and the other player's pawn. Keep the same rule as card 021: repoint only when `SpawnedAttributes` holds exactly one health set and it is not the component's. Log one line per pawn, capped per map.
3. **Notify.** On a later Health change on the client, call the game's `UADHealthComponent::HealthAttributeChanged` (0x04A3CD20) with the same 0x48-byte buffer as card 021 (New at +0x38, Old at +0x3C).
   - **Guard:** never call it when Old ≤ 0 and New > 0. That is the NPC revive branch (`HandleHealthValueChangedEvent`, 0x05193030), which on the client would restart state machines, remove `Tag_Corpse` and re-enable collision. Log and skip such a change.
   - Skip the first value seen per pawn (no Old yet) as well.
4. **Death on the client.** Card 023 showed that the death pose already arrives on the client without any health change. With Health now changing, report whether anything plays twice: the death animation, the death sound, gibs, or the orange glow. Log only; do not add a client death visual in this card.
5. **Damage size (card 023 open point).** On the host, log each Health change on the bug together with the damage event, if it is cheap to reach (`UADHealthComponent::RaiseDamageEvent` 0x04A3E130, or `AttributeSetPostEffectApplied` 0x04A3B230). Report damage per projectile from the client's relayed MoritaMk1 shots against the host's own shots, so we can see whether the relayed shots hit harder.
6. **Runs.** Use the card 023 pair command with `UBWCOOP_ENEMYAUDIT=1` and `UBWCOOP_SPAWN_BUG=1` on the host, and `UBWCOOP_GRANT_WEAPON=MoritaMk1` on both sides. Take the client's shots 1 s after the host's (card 030 used `kC30ClientShotLagMs`), and keep the run_pair summary as `logs/card037<x>-run.txt` for every run. Keep `UBWCOOP_AUTODROPSHIP=1` on the host (STATE runtime facts: mission maps pause on the brief).

## Answer, with log lines

- Which health set changes on the client, and did adoption make the client's bug Health follow the host's (400 → … → ≤ 0)?
- Does the client's health component broadcast fire, and does anything visible change (a health bar, a hit flash)?
- Does anything play twice on death?
- What is the damage per shot, relayed against local?

## Done when

- The client's bug Health tracks the host's within one audit tick, and the log shows it.
- No revive-branch call on the client (the guard's skip lines, if any, are logged).
- The client's own pawn health (card 021) still works: rerun card 021's test hit and check `client-own Health 100.0 -> 75.0, HealthAttributeChanged called`.
- Clean build (`/W4 /WX`, `--clean-first`), 84 exports, KERNEL32-only imports. No new crash dumps. Every new function is at most 60 lines.

## Risks

- An enemy's ASC may hold no health set in `SpawnedAttributes` on the client, if NPC attribute sets are not replicated subobjects. If so, report the ASC's replication mode and stop; the next step would be a mod-carried health message.
- A client-side `HealthAttributeChanged` may start blueprint logic (hit reactions, barks) that only the server should run. Log any new effects you see, and say whether they are harmless visuals.

## Report

Runs: `card037a` (diagnosis, client `UBWCOOP_NO_ENEMY_HEALTH=1`), `card037b` (adoption, found a sentinel bug), `card037c` (final). Summaries in `logs/card037{a,b,c}-run.txt`. All three: done after 118-119 s, rc 0/0, WARN 0 and FATAL/ERROR 0 on both sides, no new crash dumps.

**Which set moves (step 1, card037a).** On the client every non-local pawn (13 troopers, the bug, the host's player pawn) has ASC ReplicationMode 2, SpawnedAttributes 2 (5 on player pawns), and exactly one `UADAttributeSetHealth` in it, which is not the component's. After the host killed the bug, the component set stayed at 400 while the spawned set went to 0:
`card037: client sets tick BP_AI_WarriorBug_C ...: component ... set 0000000066EE7800 Health 400.0/400.0 ...; ASC ... ReplicationMode 2 SpawnedAttributes 2, health sets 1: 0000000069B83A00 0.0`.
So the hypothesis holds: the replicated value arrived; only the adoption was missing.

**Adoption and tracking (steps 2-3, card037c).** `c21_adopt_set` now holds card 021's rule and serves both the client-own pawn (card 021, unchanged log line) and every other pawn (`c37_adopt`, one capped line per pawn). `c37_step` scans every 250 ms on the client, skips the client-own pawn, records the first value seen and the adoption scan without notifying, and calls `HealthAttributeChanged` (0x48 buffer, New +0x38, Old +0x3C) on later changes. Client against host for the bug:
- host: 400 → 360 → 320 → 260 → 220 → 160 → 100 → 60 → 20 → −20 → −60 → −120 → −160 → −200 → −204 → −244 → −284
- client: `Health 400.0 -> 360.0`, `360.0 -> 260.0`, `260.0 -> 160.0`, `160.0 -> 20.0`, `20.0 -> -20.0`, `-20.0 -> -120.0`, `-120.0 -> -200.0`, `-200.0 -> -244.0`, `-244.0 -> -284.0`, each `HealthAttributeChanged called`
Each notify lands 40-110 ms after the host change, inside one scan. Host steps within one scan arrive as one notify. The final value −284 matches.
- Card037b bug, fixed: the "not seen" sentinel was −1.0, and overkill takes Health below that (to −196). A dead bug was then a "first sighting" every scan, so notifies stopped at `60.0 -> -64.0` and a dump line ran every 250 ms. Now `kC37Unseen = -3.0e38f`. Card037c shows one "first" dump for the bug and notifies down to −284.
- The broadcast reaches the game: on the client the NPC `HandleHealthValueChangedEvent` hook fires from the mod notify (`card037: client NPC HandleHealthValueChangedEvent BP_AI_WarriorBug_C ...(Old 400.0, New 280.0) [mod notify]` in card037b). I did not check the screenshots for a health bar or hit flash: no `.png` from the run turned up in a search. Someone needs to look at the screens.
- Troopers: card 023's host clear (`host cleared 55 live AI troopers (InstaKillTarget)`) shows on the client as 20 notifies of `50.0 -> 0.0` on tracked troopers.

**Revive guard.** No skip line and no call: Health never went from ≤ 0 back above 0 in any run. No `WARN` on the client.

**Card 021 check.** `card021: client client-own Health 100.0 -> 75.0, HealthAttributeChanged called` appears in card037b and card037c.

**Death (step 4).** Host: `HandleDeathEvent` once per run (at 20 → −20 in card037c). Client: `HandleDeathEvent`, `StartDying` and `BP_OnRevived` 0 calls in every run, including after the mod notify crossed 0. Nothing that we log plays twice. Visual and sound doubling are not checked; the screens need a look.

**Damage per shot (step 5).** `AttributeSetPostEffectApplied` (0x04A3B230) is hooked on the host, audit-only, for non-trooper enemies. Every hit, host and relayed, is `effect ADGEDamage, attribute Damage op 0 magnitude 40.00`, with the Damage meta 40 at entry.
- Host burst hits (4 over b and c): −40 each.
- Relayed client hits: mostly −40, but also −60 (5 in c, 2 in b) and −4 (1 in c, 4 in b).

The magnitude is the same 40 on every hit, so the set scales after the meta: 1.5× and 0.1×. That points to a hit-location multiplier (weak spot, or armour) rather than a duplicated effect. Relayed shots do **not** apply twice (one PostEffect per hit). The host sample has no 60s or 4s, but 4 hits are too few to call it relay-specific. The host burst is short (`kC37BurstHoldMs` 300) so the client can still reach the bug.

**Blueprint side effects.** None seen in logs beyond the NPC `HandleHealthValueChangedEvent` hook firing on the client. No WARN or ERROR, and no crash.

**Code.**
- `mod/src/hooks.cpp`: card 037 section (`c37_*`, `detour_post_effect`); `c21_adopt_set` split out of `c21_adopt_replicated_set`.
- `c23_collect` takes a class (bug or `ASTCharacter`).
- Client screenshots are aligned to a wall-clock grid plus `kC23ClientShotLagMs` 1000 (card037c: client shots 892 ms and 888 ms after the host's).
- `config::read_no_enemy_health` (`UBWCOOP_NO_ENEMY_HEALTH=1` turns adoption and notify off).
- `tools/symbols.txt`: `ASTCharacter_StaticClass`, `UADHealthComponent_AttributeSetPostEffectApplied`.
- Build: `--clean-first`, /W4 /WX clean, 84 exports, KERNEL32.dll the only import, no function over 60 lines.

**Open.**
- Visual check of the client screens: health bar, hit flash, double death effects.
- The source of the 1.5× and 0.1× damage scaling (`PostGameplayEffectExecute` on the health set) needs a longer host burst or a headshot test.

## Senior review (2026-11-03)

Accepted, with one correction to card 023 and one code-rule note.

**Verified.** `logs/card037{a,b,c}-run.txt`: rc 0/0, WARN 0, no FATAL or ERROR, no new dumps. In card037c the client bug notifies follow the host from 400 down to −284, each within one 250 ms scan. Card 021's `client-own Health 100.0 -> 75.0, HealthAttributeChanged called` line is present. The revive guard is in `c37_notify` and never fired. The `kC37Unseen` fix is correct: overkill takes Health well below −1. The deployed DLL matches the build.

**Screenshots.** Both instances write to `prefix/drive_c/users/steamuser/AppData/Local/StarshipTroopers_UBW/Saved/Screenshots/Windows/` (it is inside the Wine prefix, not under `game/`). I mapped files to requests by wall-clock offset from each run's first shot. Copies are in `logs/card037-shots/`:
- `c-client-bug-hit-300ms-after-death.png` (ScreenShot00244): client shot 1, 300 ms after the host's `HandleDeathEvent`. The bug is upright, with a hit flash on its body from the client's own fire.
- `c-client-bug-standing-10s-after-death.png` (ScreenShot00247): client shot 2, 10.3 s after the host death. The bug is still upright, in the same pose.
- `a-client-bug-standing-9s-after-death.png` (ScreenShot00229): card037a, where the client's notify was off. The client bug is upright 9 s after the host death.
- `a-host-dead-bug-1476.png` (ScreenShot00228): the host's view at the same time. The bug lies collapsed, with green gore on the ground.

**Correction to card 023: the death pose does not reach the client.** In both runs, with and without the mod notify, the killed bug stays standing on the client. Card 023's "the client sees the bug collapsed" came from ScreenShot00183. The host and client shots shared file names in that run, so 00183 was most likely the host's view. The mod notify is therefore not to blame. Health ≤ 0 on the client starts no death (0 calls to `HandleDeathEvent` and `StartDying`), which matches the static reading that death runs on the authority only. The answer to step 4 is "nothing plays at all on the client", not "nothing plays twice". A client death visual is the next step for milestone 3. `TryPlayDeathAnimation` (0x051B11E0) is no longer at risk of double play.

**No enemy health bar.** No shot shows an enemy health bar, on either side. The game appears not to draw one for bugs, so the only visible effect of the health fix is that client-side code now reads the right value.

**Code-rule note.** `install()` (hooks.cpp line 13142) runs 62 lines from the signature to the closing brace; the body alone is 59. This card added one line to it (`g_c37_off`). The rule counts the whole function. Fix it in the next card that touches `install()`: move the `g_cXX = config::read_...()` block into a `read_card_flags()` helper. The function-size check in the next report must count from the signature line.

**Open.**
- Client death visual for enemies (new card).
- The source of the 1.5× and 0.1× damage scaling (headshot or armour).
- Card 023's tooling note stands: give each side its own screenshot file name. The 1 s lag works, but host kill shots still collide with host shots (00246 holds both host shot 2 and kill shot 2).
