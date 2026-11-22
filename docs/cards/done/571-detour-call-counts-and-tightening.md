# Card 571: count calls per detour, then tighten the hottest detours

User 2026-11-21: "Detours need to be tightened in general for speed up, always."

## Background

The hook table is not searched at run time. MinHook patches each target's first bytes with a jump straight to its
detour, so a row's place in the table costs nothing. The per-call cost is in the detour body: its checks, table
lookups and logging before and after the original. There are 521 rows; 152 are gated off, so about 360 are live in
normal play.

b387 `9f3b974f0dba89b5` (= b386 + this): `kMaxHooks` and the resolve/install target array are removed. install_hooks
works out each target again (base + RVA), so the row count has no cap.

## b388 `0b3b3a3268266a92` (= b387 + counter, build-b5 copy kept)

With `UBWCOOP_C571=1`, every installed row hooks a 32-byte stub instead of its detour:
`mov rax, &count; inc qword [rax]; mov rax, detour; jmp rax`. The detour sees the same stack and arguments. When the
variable is off, no stub is made and the counter costs nothing.

Each card 330 window row (every 30 s) is followed by:

`card571: <side> t=N hook calls in W ms: T total (R/s) over N rows; top 20: <name> <count>, ...`

## Plan

1. One C1M1 run (any planned run, with `UBWCOOP_C571=1` on both sides) gives the top 20 per side.
2. For each hot detour (calls per second times its body cost): the early-out goes first. That means a cheap check,
   such as a null owner, a wrong class or the map not being live, before any table scan, string work or log
   formatting. Cut repeated lookups, move per-call work out to per-frame or per-map steps, and gate diagnostic-only
   rows off by default.
3. Measure the change with the same card571 rows plus card330 mod ms in a matching window.

## Prediction

The top rows are tick, overlap, damage, animation notify and the fire functions: thousands of calls per second
each. Fewer than 20 rows hold over 90 percent of all calls.

## Result, run c570b388a (b388, C1M1, `UBWCOOP_C571=1`, diag gates on: 460 rows counted)

Host calls per 30 s window grew from 2.07M (69k/s) to 18.41M (613k/s) over 11 windows. The client went from 0.39M to
3.43M (114k/s). The growth follows the bug load on the map, not mod work: card 330 mod time in the last host window was
199 ms (0.66% of frame time).

Last host window, top rows: `USTSaveComponent_GetSaveId` 7.01M, `UFunction_Invoke` 3.42M,
`USTAttackComponent_DoesTargetMeetConditions` 2.47M, `AActor_FindComponentByClass` 2.19M,
`ProcessLocalScriptFunction` 1.44M, `UObject_ProcessEvent` 0.71M, `UADTargetComponent_GetPrimaryTarget` 0.27M,
`UGameplayStatics_GetPlayerController` 0.22M. The top 6 hold 94% of all calls (prediction 2 held). Prediction 1 was
wrong: tick, overlap and damage are not near the top. The top rows are perf-mod caches and the BP VM dispatch.

| Row | Detour | Body today | Action |
|---|---|---|---|
| GetSaveId | card 531 cache | 2-way set lookup, 97.82% hit | already tight; the call volume is the game's (card 527 family) |
| DoesTargetMeetConditions | card 538/540 memo | 1634 calls per frame, memo served 5.38M | already tight; game volume |
| FindComponentByClass | card 536 cache | null early-out, 2-way set | already tight |
| UFunction_Invoke | `detour_invoke` (card 019 sprint diag) | `g_gate_diag` row: not installed in a play build | none for play; diag runs pay it |
| ProcessLocalScriptFunction | card 507 | one bool when not armed | tight |
| ProcessEvent | `detour_process_event` | two `__rdtsc` plus the card 128 run | candidate: the card 330 timing could become sampled |
| GetPlayerController | card 99 remap | host index 0: copy walk, then card 102/103 | candidate: about 7k/s; needs a body cost before cutting |

Next:
- The rising game call rate (GetSaveId 0.92M to 7.01M per window) goes to the card 527 perf mod family. Its caller and
  the cause of the growth are not known yet.
- Mod-side tightening candidates are card 99 `detour_c99_get_pc` and the per-call rdtsc pair in
  `detour_process_event`. Both need measured per-call ms before a change.

## b390 `54792733a3725023` (= b389 + card 531 through card 536, build-b5 copy kept)

User 2026-11-21: "See what we can do about improving cache hits, but as a micro optimization."

The card 531 GetSaveId detour kept its own per-thread actor -> component table, checked with an exe
`FWeakObjectPtr::Get` on every hit (44.8M hits in c570b388a), and never stored "no save component" (954288 of 998246
misses walked again). Card 536 already caches the same FindComponentByClass answer, checked by card 537 generations
(no exe call per hit), with "none" results stored (99.73% hit).

The card 531 detour now calls `detour_c536_find(actor, USTSaveComponent class)` directly; the class is read once. Its
own table, counters and the `card531:` totals row (`c531_tick`) are removed (48 KB TLS per thread freed). With card 536
off (`UBWCOOP_C536_OFF=1`) the original GetSaveId runs. The ACharacter override of FindComponentByClass is skipped: it
only checks the mesh, movement and capsule members, never the save component.

Predictions (next C1M1 run with `UBWCOOP_C571=1`):
- No `card531:` rows. `card536:` hits rise by about the old card 531 hit count; its hit rate stays at or above 99.7%
  and mismatches stay 0.
- `card571` FindComponentByClass count is unchanged (the direct call bypasses the hook stub); GetSaveId count is the
  game's and stays similar.
- Card 330 mod ms in the matching late host window is below b388's 199 ms.

## b390 result, read from c594b436c (b436, FED_C3M1 seat window, `UBWCOOP_C571=1`; senior 2026-11-21)

No C1M1 rerun exists, so the nearest run with card571 rows is used.

- No `card531:` rows: pass. The GetSaveId detour now goes through the card 536 cache.
- card536: hits 10126392, none hits 111533, misses 358408 (96.62 % hit), checks 9988, mismatches 0. The 99.7 % hit
  prediction missed. 229760 of the 358408 misses (64 %) are none walks: classes the actor does not have. The card 537
  negative-entry lever covers them (listed in card 573).
- card571 top rows per 10 s: GetSaveId 120637, DoesTargetMeetConditions 85557, FindComponentByClass 75437,
  IsEnemyFaction 51852, UpdateTargetData 51836 (the last two are the card 594 diag, removed in b438). This is 41109
  calls per second in total, against 613k/s in the late c570b388a C1M1 window, so the windows are not comparable.
- card330 mod time is 466.8 ms per 30 s (1.56 % of frame time).

## Decision (senior 2026-11-21): closed

The two remaining mod-side candidates are too small for a card. The card 128 timing in `detour_process_event` is one
rdtsc pair per call, about 0.7M calls per window, so about 1 ms per 30 s. `detour_c99_get_pc` is about 7k calls per
second. The top rows are game call volume behind caches that are already tight. Any new hot detour follows the
standing tight-detours rule: early-out first, ranked by card571 counts.
