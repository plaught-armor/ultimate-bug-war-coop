# Card 572: cut the co-op mod's own per-call detour cost

User 2026-11-21: "Prep separate perf card for our perf."

This card covers the co-op mod's own cost only: the work our detour bodies do around the original call. Base-game
mechanic cuts (GetSaveId call volume, bug load growth) stay in the card 527 perf mod family. Card 571 gave the call
counts. This card gets a per-call cost for each hot row and cuts the ones that are worth it. Ranking is by GameThread
ms saved (GT headroom goal).

## Inputs (run c570b388a, b388, last host window, 30 s)

Card 330 mod time was 199 ms per 30 s (0.66% of frame time). Hot rows whose cost is our detour body:

| Row | Calls / 30 s | Our body per call today |
|---|---|---|
| `UObject_ProcessEvent` (`detour_process_event`, death.cpp:331) | 0.71M | 4 `__rdtsc`, a `C128PeScope`, 11 flag tests split over `c128_pe_run` and the caller, `c330_pe` add |
| `UGameplayStatics_GetPlayerController` (`detour_c99_get_pc`, player_c99.cpp:356) | 0.22M | host index 0: `_ReturnAddress` RVA, `c99_copy_of` walk (up to `kC99Hops`, an `object_listed` + `is_a` per hop), `c102_excluded`, `c103_enemy_pc` |
| `USTSaveComponent_GetSaveId` (card 531) | 7.01M | b390 already moved it onto the card 536 cache (card 571 doc, b390 section) |
| `AActor_FindComponentByClass` (card 536) | 2.19M | 2-way set probe, no exe call; already tight |
| `ProcessLocalScriptFunction` (card 507) | 1.44M | one bool when not armed; tight |
| `UFunction_Invoke` (card 019 diag) | 3.42M | `g_gate_diag` row, not installed in a play build; out of scope |

Also inside this scope: `detour_c102_overlap` (two `g_weak_get` calls plus a `c99_copy_of` per host overlap). Its count
is not in the top 20 and it needs a card 571 count before any change.

## Step 1: per-call cost (one run, no code change to the bodies)

Add a sampled cost row next to the card 571 counter, behind `UBWCOOP_C571=1`. For the rows above, time 1 call in 64
with `__rdtsc` around the detour body without the original (ProcessEvent already subtracts `orig`). Print with each
`card571:` row:

`card572: <side> t=N <row> sampled K calls: body avg X ns, max Y ns, est Z ms per window`

Proof for step 1: the sum of the est values for the ProcessEvent and GetPlayerController rows, compared with the card 330
mod ms in the same window.

## Step 2: cuts (only the rows that step 1 shows above 5 ms per window)

- **ProcessEvent:** fold every `g_*_pe` flag into one `g_pe_armed` bitmask that the setters keep. When it is 0, skip
  the scope and the flag chain and call the original. Card 330 timing then uses 2 `__rdtsc` (sampled 1 in 16, scaled),
  not 4 per call. The closed-window path becomes one load, one branch and the original.
- **GetPlayerController:** early-out before `_ReturnAddress` and `c99_copy_of` when no server copy exists, no acting
  scope is open and no enemy remap is live (one flag set at copy spawn and despawn). Cache the last ctx -> copy answer
  for the frame (key ctx and frame number), because callers repeat the same ctx within a frame.
- No change that alters an answer: card 99/102/103 rows and their counts must match b390 in a matched window.

## Predictions

- Step 1: ProcessEvent and GetPlayerController together make up over half of card 330 mod ms in the late host window.
  ProcessEvent body is under 100 ns per call. `c99_copy_of` is the larger cost per call.
- Step 2: card 330 mod ms in the matching late host window drops at least 30% from the b390 number. Card 99 remap
  counts (`c99_count` rows) and card 102 entries are the same within the run-to-run noise. Card 571 counts are
  unchanged (the call volume is the game's).

## Proof and budget

One C1M1 run per step on the current lane 2 build, `UBWCOOP_C571=1` both sides. Use the matched late host window and
both logs. A 3-run budget for the card. Card 536 mismatches must stay 0, and the WARN diff must show nothing new.

## Step 1 build: b398 `595c1ebc6db273b2` (= b397 + step 1 rows), senior 2026-11-21

`UBWCOOP_C571=1` also turns on one row per card 330 window, right after the `card571:` row:

`card572: <side> t=N ProcessEvent <calls> calls: body avg X ns, max Y ns, total Z ms; GetPlayerController <K> body
calls, sampled <S>: body avg X ns, max Y ns, est Z ms`

- ProcessEvent: every call is already timed for card 330 (`c330_pe`, the original excluded), so the total is exact.
  It includes the timing's own two `__rdtsc`.
- GetPlayerController: only host index 0 calls with a non-null ctx and pc reach the body. One body in 64 is timed, and
  est = sampled mean x body calls. The client returns before the body and logs 0 body calls.
- Nanoseconds come from each window's own TSC and QPC deltas. Counters are plain (render-thread callers can race),
  which is good enough for a cost estimate.
- With `UBWCOOP_C571` off, ProcessEvent and GetPlayerController pay one extra flag load each.

Proof: in the same window, ProcessEvent total + GetPlayerController est against card 330 `mod` ms (prediction: over
half). Run it in the c397 combat run (it sets UBWCOOP_C571=1) with b398 in place of b397. b398 contains b397.

## Step 2 static trace, senior 2026-11-21 (code comes after the step 1 numbers)

Per call, `detour_process_event` on a closed window does all of the following:

- 4 `__rdtsc`: 2 for card 330, 2 around the original.
- `C128PeScope`: thread_local saves and writes of `g_c128_pe_object` and `g_c128_pe_function`, then restores.
- 13 separate gate loads. The gates are bools spread over 10 files' .bss: `g_c30_window`, `g_c32_pe`, `g_c74_pe`,
  `g_c161_pe`, `g_c376_pe`, `g_c345b_pe`, `g_c408_pe`, `g_c44_pe`, `g_c67_pe`, `g_c158_pe`, `g_c493_pe`, plus
  `g_client_session` for c491.
- On the client, an out-of-line `c491_pe` call every time: a name read, two `fname_cached`, and on a name match a
  `class_name` strcmp.

Who needs the scope: `c136_attack_local` (only while `g_c408_pe`) and the card 128 reliable-overflow close row (a
failure diagnosis). The scope stays on every call. It is thread-local and cheap, and the card 128 row must keep
working.

Cut, if step 1 puts ProcessEvent above 5 ms per window:

1. Move the 12 gate bools into one `alignas(16) PeGates g_pe` (named fields, plain stores, so each writer stays a
   one-line assignment). The detour tests them with two 8-byte loads. 20 writer sites, all listed by grep.
2. Client c491: test the function's FName against the two cached names inline before the call. Only a name match
   pays for the call and the class strcmp.
3. Closed path: the scope, one gate test, the c491 name test, and the original. Card 330 timing is sampled 1 in 16
   and scaled, so only the sampled call pays its 4 `__rdtsc`.

Expected: about 13 loads and 2-4 `__rdtsc` fewer per call, at 0.71M calls per 30 s. Proof: the step 1 row before
and after, plus card 330 mod ms, in a matched host window.

## Step 1 result (run c397b398a, 2026-11-21)

The prediction failed in part. ProcessEvent plus GetPlayerController make up 29-42% of card 330 mod ms, not over
half (for example mod 215.4 ms, ProcessEvent 36.7 ms, GetPlayerController 39.1 ms). Both are above 5 ms per window,
so step 2 applies to both. GetPlayerController (host only) averages 482 ns per sampled body over about 130k calls,
so it costs as much as ProcessEvent.

## Step 2 build: b400 `9d4f79c052aea9a7` (= b399 + ProcessEvent sampling), senior 2026-11-21

- ProcessEvent: only 1 call in 16 is timed (`kC572PeEvery`). Its cost is weighted by 16 in the card 330 and
  card 572 totals, so call counts stay exact and ms totals become estimates. The other 15 calls pay no `__rdtsc`.
  The gate chain is unchanged. The PeGates struct (cut 1) was left out: 12 byte loads cost little next to 4
  `__rdtsc`.
- GetPlayerController: not cut yet. A per-frame ctx-to-copy cache is unsafe without a garbage collection hook. A GC
  inside the engine tick can free a ctx and reuse its address in the same frame, which is the card 476 freed-walk
  case. A safe cut needs a GC hook to clear the cache, or a cheaper walk. That is the next step.

Proof: in a matched late host window, the card 572 ProcessEvent total and card 330 mod ms fall compared with
c397b398a, while the call counts match the game's volume. Card 99/102 rows are unchanged, because GetPlayerController
is untouched.

### b401 `386cb6c8fda71fd8` (= b400 + GetPlayerController part timings)

The sampled body (1 in 64) now times its three parts. The card 572 row ends with `(copy walk X ns, enemy remap Y ns,
tally Z ns)`:
- copy walk: `c99_copy_of`.
- enemy remap: the acting scope and `c103_enemy_pc`, with its own walk and `c43_roster`.
- tally: `c99_count`, which takes `class_name` and a strncmp scan on every remapped answer.

The largest part gets the cut. Candidates:
- Copy walk: an is-actor test from the class cast flags in place of `FObjectPtr::IsA`.
- Enemy remap: skip it when no enemy pick is live.
- Tally: key the rows on the class pointer, so `class_name` runs only for a new row.

## Step 2 result c397b400a (b400, junior 2026-11-21): no drop

Matched late host windows, c397b398a vs c397b400a: #8 mod 182.0 vs 183.2 ms, PE calls 261223 vs 261248, PE total
36.2 vs 39.4 ms, GPC est 40.1 vs 41.3 ms; #9 mod 190.8 vs 200.3 ms, PE 33.9 vs 35.5 ms. The weighted PE total is the
same, so the timer was never the cost: the time is in the ProcessEvent chain bodies. The sampling stays (it is
lossless and removes 15 of 16 rdtsc pairs), but it is not a cut. The next PE step needs per-link counts for the
before and after chains. The GPC split comes from the b401 rows in c573rb401a.

## b401 GPC split result (c573rb401a, junior 2026-11-21)

Host card572 rows, all 9 windows: copy walk 182-218 ns, enemy remap 220-271 ns, tally 33-59 ns per sampled body.
GPC est 67.2 ms (early), 38.3 ms (mid), 33.6 ms (late). The enemy remap is the largest part in every window, and the
copy walk is close behind it.

## Step 3 diag + step 4 cut: b403 `de3fa1c5e726e5f4` (= b402 + the items below)

Step 3 (diag, `UBWCOOP_C571=1` only): the sampled ProcessEvent body is split at the original's start and end. The
card572 row adds `ProcessEvent sampled N: before X ns, after Y ns, slow S (over 4000 ticks) avg Z ns`. Before is the
C128PeScope, the notes and the refusals; after is `c128_pe_after`. A refused call counts all of its body as before.
The slow count separates a steady per-call cost from rare long bodies (a log row, a preemption).

Step 4 (cut, on by default, `UBWCOOP_C572_MEMO_OFF=1` for A/B):
- GetPlayerController same-frame memo (`c99_memo_pick`, c540 pattern). The table is game thread only, 256 direct
  mapped rows, keyed on ctx and the frame (`c99_frame_tick` in c42_step). It is used only with no acting scope and a
  caller that is not excluded, so a stored pick depends on ctx alone. A hit skips the copy walk and the enemy remap.
  The tally still runs per call. One repeat in 64 runs the body anyway and counts `differ` when the answer changed.
  The row adds `GetPlayerController memo on: served S, verified V, differ D (since start)`.
- `c103_player_slot` reads each slot's Downed and Out state once (it was read twice per slot, through c108_any_up and
  again in each search). This is lossless: the same eligibility rule.
- `c99_answer` runs `c102_excluded` and the game thread check once per call (each was run twice). `c103_enemy_pc`
  leaves the game thread check to its only caller.

Predictions for the next `UBWCOOP_C571=1` run on b403 against the b401 windows:
- differ is 0. Any non-zero differ voids the memo: turn it off and look at the caller.
- GPC body avg drops from about 480-540 ns to under 300 ns, and GPC est falls by at least a third in matched
  windows. If served is under about 30 % of the body calls, the memo cannot pay for itself; then turn it off and
  report the repeat rate.
- The PE before/after split names the larger chain, and the next PE cut goes there.

## Step 4 result c397b403a (b403, junior 2026-11-21): memo right, small drop

- The memo was never wrong: differ 0 in all 9 windows (served 373306, verified 5925). Served is 30-40 % of calls.
- GPC est fell only 17 % early, 9 % mid and 1 % late (58.8 / 34.3 / 38.1 ms against 70.8 / 37.9 / 38.5 ms on b400).
  The body avg is 433-501 ns.
- The "body calls" count (`c572_c99_call`) counts every host index 0 call, memo-served or not. The sampled avg also
  covers both, so est = calls x avg is right, and the memo's whole gain shows in the avg.
- The parts moved: enemy remap 159-200 ns (was 220-271), but copy walk 210-256 ns (was 182-218). Served samples put
  nothing into the copy part, so the walk on memo misses got dearer. A likely cause is a cache miss on the memo row.
  The step 5 split below measures it.

## Step 5: b405 `37f5016558767eca` (= b404 + walk cut + diag), in b406 `7d805beca8a57e0c`

- Cut (lossless by the UE rule that an actor's Outer is its ULevel): `c99_copy_walk` stops at an actor with no Owner.
  The old walk then climbed ULevel, UWorld and the package: 3 hops of `object_listed` plus `IsA`, always ending in
  nullptr. The cut applies to every `c99_copy_of` caller.
- Check: every sampled body (1 in 64, `UBWCOOP_C571=1`) also runs the full walk, untimed, and counts any answer that
  differs. The row gains `full walk checked N, differ M`.
- Diag: sampled bodies are split into memo-served and other, each with its own average.

Predictions (c397 on b406, `UBWCOOP_C571=1`):
- full walk differ is 0.
- copy walk under 130 ns.
- GPC est at least 25 % below b403 in each matched window.
- The served avg is under 80 ns. If it is not, the memo lookup itself costs more than it saves: run the
  `UBWCOOP_C572_MEMO_OFF=1` A/B next.

## Step 6: roster cut, in b424 (senior 2026-11-21)

- Cut: `c43_roster` makes kC43Max `GetPlayerController` calls plus pawn reads, and `c99_pick` ran it up to twice per
  body (`c103_enemy_pc` for an enemy context, then `c582_place_pc` for a placed class). The roster is now built once
  per frame (`c572_roster`, player_c99.cpp, keyed on the `c99_frame_tick` frame) and both callers read the same copy.
  Game thread only (both callers already are). `UBWCOOP_C572_MEMO_OFF=1` turns this cache off together with the memo.
- Verify: one cache hit in 64 also builds the roster fresh and compares pc and pawn per slot. A difference counts as
  differ and logs `WARN card572: roster cache differs ...` with both rosters (first 8 since start).
- The card572 row gains `roster cache built N, served M, verified V, differ D (since start)`.

Predictions (C3M1 c587b423a and the C4M1 mech run, `UBWCOOP_C571=1`):
- roster differ is 0, and there is no `WARN card572: roster cache differs` row.
- The GetPlayerController body avg falls below the c397b407a range (437-545 ns) in matched windows, and the enemy remap
  part falls below its 189-240 ns range: the roster was the bulk of the non-served body.

### Step 6 result, C3M1 c587b423a (b424 784f6a4e07de92ac)

- Scenario passed in 364 s, host rc 0, client rc 0, no crash dumps.
- Roster differ is 0 in every window, and there is no `WARN card572: roster cache differs` row (prediction met).
  At the end: roster built 17985, served 213741, verified 3392. Memo differ 0, full walk differ 0.
- Body avg per window: 1148, 665, 469, 1049 (one 716960 ns max spike), 468, 478, 585, 603, 590 and 545 ns. Enemy
  remap: 503, 302, 205, 782 (spike), 194, 211, 249, 255, 249 and 230 ns. Copy walk 190-260 ns (417 in the first window).
- The body ns did not fall below the c397b407a range: steady windows sit at 468-603 ns against 437-545 ns, so the
  prediction is not met. This scenario is not matched to c397b407a, so this is not a clean before/after either. The
  cache is hit on about 92% of calls, so the roster build was not the bulk of the body cost; what remains is the
  per-body work after the roster.

### Step 6 result, C4M1 mech c567b424a (b424)

- Roster differ 0 and no roster WARN (built 6320, served 88781, verified 1409 at the end).
- GetPlayerController body avg 575, 557, 558 ns; enemy remap 234, 236, 232 ns; copy walk 211 ns. These do not fall
  below the c397b407a ranges either, so the body ns prediction is not met on both maps.
- New: the step 4 memo verify counted `differ 1` before the first window (served 46712, verified 741) and stayed at 1.
  It is the first nonzero memo differ in any log in logs/. The memo has no detail row, so the ctx and both answers are
  not known. One possible cause: c572_roster_forget drops the roster on a host destroy mid-frame, but the memo rows of
  that frame stay, so a repeat after the destroy can differ from the stored pick. The same window existed before b424
  (the roster was rebuilt per call), so it is not proven to be new with the roster cut. Proposed next: a WARN row for a
  memo differ (ctx class, caller rva, stored and fresh own and copy), and on a roster forget hit also bump g_c99_frame
  so the memo rows drop with it.

## Step 7: roster cache revert, memo differ WARN, memo forget, in b426 `8bf1b2d3f80f12ae` (senior 2026-11-21)

- The step 6 roster cache is gone. It cut 0 ns and added a staleness path that could feed the memo. `c582_place_pc`
  and `c103_enemy_pc` build `c43_roster` per call again, as before b424. The card572 row drops its "roster cache" part.
- Memo differ WARN (`c99_memo_warn`, 8 rows since start): "WARN card572: memo differs in frame F: context <class> <ptr>,
  caller exe+0x..., stored own/copy/entry, fresh own/copy/entry".
- Memo forget (`c572_memo_forget`, from the host DestroyActor path in world_c98.cpp). It drops the memo row keyed by
  the destroyed actor, so a later actor at that address starts with no stored pick. When the actor is a pawn or player
  controller in the live roster, it does `++g_c99_frame`, which drops every row. The card572 row counts these as
  "forget bumps".
- Prediction (C4M1 mech, c572-c4m1-b426.txt, run c572b426a): memo differ stays 0 after the revert and the bump. If it
  is nonzero, the WARN names the case.
- Next 572 cut, not yet done: first split the GetPlayerController body timing by sub-step (the c43 roster pawn reads,
  object_listed, IsA, the rest) on one run. Then cut only the measured top part.

### Step 7 result, C4M1 mech c572b426a + c572b426b (b426)

- c572b426a: the host crashed at 126 s in the engine net tick, at exe+0x521B7B4. The same stack showed up in
  c558b377a on b377, so it is older than this batch and now has card 591. Up to the crash: memo served 69296, verified
  1099, differ 0, forget bumps 2.
- c572b426b: 160 s, ended by the scenario, no crash. Memo served 100833, verified 1600, differ 0, forget bumps 2. No
  "WARN card572: memo differs" row. card132 mismatches 0. The only WARN classes are card537 (class table full) and the
  attach row.
- The prediction held: memo differ stays 0 after the revert and the forget bump.

## Step 7 closed

2026-11-21: the senior accepted the c572b426b result (memo differ 0, mismatches 0). b426 went to lane 1.

## Step 8: memo differ in b435 (c594b435a), senior trace 2026-11-21

Two host rows (the first nonzero differ since b426):
- frame 3753: `BP_AI_AphidBug_C`, caller exe+0x517EFAC (`GetPlayerViewPoint` after index 0, card 103 table), stored
  own 0 entry 0, fresh own = the client controller, entry 4 (`kC102Enemy`).
- frame 8944: `BP_MountableTurret_Gunner_C`, caller exe+0x3C7F116, stored own 0, fresh own = the client controller,
  copy = the turret itself, entry 0 (the copy walk found the turret as a server copy).

Cause (static): the memo row is keyed by ctx and frame, but the pick reads state that can change inside one frame.
`c103_enemy_pc` is a pure function of ctx and live state (the rva is used only for logging), so the key is right; the
inputs are not frozen:
- the enemy's primary target (`detour_c103_set`, called from the target component tick between two callers in the
  same frame): the aphid had no client primary at the first call and had one at the second;
- the actor roles that `sprint_side` reads (`LocalRole`/`RemoteRole` at possession), plus the card 586 holder state:
  the turret became a server copy when the client mounted it, mid-frame.
In both rows the memo would have served the host's controller for the rest of that frame. The error is at most one
frame, but full world sync needs differ 0.

Fix (class: every writer of a pick input bumps `g_c99_frame`, which drops all rows):
1. `detour_c103_set` (host): read the component's primary GUID at +0x190 before and after the original; bump when it
   changed. Do not bump on every call: the set runs per selection pass.
2. Role flips: hook `AActor::SetAutonomousProxy` (APlayerController::OnPossess calls it on the new pawn; the junior
   confirms the RVA and the unpossess path statically) and bump when the RemoteRole byte changed. If unpossess does not
   go through it, also hook the unpossess path found in the trace.
3. Card 586 holder start and end (`c586_frame` add, `c586_end`): bump.
4. The card572 row gains `input bumps N` (since start), split by the three sources.

Prediction (one run with a client mount and bug waves, the c594 scenario): differ 0, no `memo differs` WARN, served
falls by less than 10 % against c594b435a (bumps are rare next to calls).

## Step 8 in b436 (dwmapi `ec6aa2c9d1910b6f`), junior 2026-11-21

- `c572_input_bump(source)` (player_c99.cpp) bumps `g_c99_frame` and counts the source in
  `g_c572_memo.input_bumps[3]` (primary, role, hold).
- Primary: `detour_c103_set` copies the 16-byte key at `+0x190` before the original and compares it after. Static
  check: `GetPrimaryTarget` (exe+0x518F310) starts `add rcx, 0x190; mov edx, 0x10; call CityHash64`, so this is the
  primary GUID.
- Role (deviation from the spec): `APawn::PossessedBy` (0x03F69410) and `APawn::UnPossessed` (0x03F758A0) are
  hooked in place of `AActor::SetAutonomousProxy`. PossessedBy calls SetAutonomousProxy on the player controller
  branch and CopyRemoteRoleFrom on the AI branch. UnPossessed does not touch the role but clears Controller and
  Owner, which the context walk reads. On the host, for player pawn classes only, each hook snapshots RemoteRole
  (0x60), Owner (0x158) and Controller (0x2D8). It bumps when any of the three changed.
- Hold: `c586_frame` bumps when the hold set changed (a new hold or a different count), and `c586_end` bumps before
  it ends a hold.
- The card572 row ends with `input bumps N (primary P, role R, hold H) (since start)`.
- Run: c594b436a with `tools/scenarios/c594-c3m1-b436.txt`.

### Step 8 result: c594b436a/b/c (b436 `ec6aa2c9d1910b6f`, junior 2026-11-21)

- Memo differ 0, full walk differ 0, no `memo differs` WARN in all three runs (b435 had 2). card132 mismatches 0.
- Last c row: served 191947, verified 3046, input bumps 2059 (primary 1735, role 316, hold 8).
- Deviation, accepted: the role source hooks `APawn::PossessedBy` 0x03F69410 and `APawn::UnPossessed` 0x03F758A0 on
  host player pawn classes (snapshot RemoteRole, Owner, Controller; bump on change), not `SetAutonomousProxy`.
- Role 316 is well above the 6 seat and missile swaps. It costs only memo hits, so it is not a fault. A later look
  can log which pawns change.
- Prediction met. Step 8 closed.
