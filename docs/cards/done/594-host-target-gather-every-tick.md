# Card 594: host AI target gather runs every tick (performance mod family)

Umbrella: card 573 (workstream A, GameThread). Found 2026-11-21 by the senior, from the c573b398a host GameThread
profile and a static read of the exe.

## Evidence

- c573b398a host GameThread: 4734 samples in 20 s. The AI targeting cluster holds about 592 of them (12.5%):
  `TSet<FGuid, FADTargetData>` find 128, `HasMatchingGameplayTag` 89, `GatherTargetsInRange` 79,
  `FWeakObjectPtr::Get` 75, `UpdateTargetData` 69, `CityHash64` 62, `GetTargetFaction` 49,
  `GetFactionRelationshipTo` 17, `DoesTargetMeetConditions` 24. `FMallocBinned2::Realloc` (47) is partly the
  per-call candidate array below.
- Closed lead in the same profile: the GameThread samples in ntdll.so, ntdll.dll, libc and kernelbase (about 20%)
  are the frame cap wait (`Sleep`, `RtlQueryPerformanceCounter`), SRW lock and critical section paths, and about 150
  samples (3%) of wineserver round trips. None of it is a lossless cut. Workstream B already closed the cap wait.

## Static read (RVAs, exe image base 0x140000000)

`UADTargetComponent::TickComponent` 0x051AC040:
1. `FindPrimaryTargetData` 0x05189290 (a TSet find). Timer `+0x1AC` += dt, clamped to the interval at `+0xB8`.
2. With a primary: `UpdateTargetData(primary)`. If the primary actor is valid and its data flag `+0x29` is set, the
   call returns while the timer is below the interval, or when vfunc `+0x4F0` returns true.
3. Otherwise (no primary, an invalid primary, flag `+0x29` clear, or the interval passed): the timer resets,
   `GatherTargetsInRange` 0x0518A240 runs, then if the set is not empty `UpdateAllTargetData` 0x051B1CA0, primary
   selection 0x051A5940 and `FindPrimaryTargetData` again.

So a component with no locked primary gathers on every tick, not on the interval.

`GatherTargetsInRange` (loop at 0x0518A340):
- `UADGASSubsystem::GetAllAbilitySystemActors` fills a fresh TArray of every GAS actor, then frees it, per call.
- For each actor: pending-kill check, not the owner, squared distance from root component `+0x200` against
  `+0x1A8`. In range: `USTSaveComponent::GetSaveId` (the card 531 detour), `CityHash64` of the FGuid, a TSet find in
  `+0xF0`, and `FindOrAddActor` when absent.
- There is no faction test. Bugs in range of other bugs are added as target entries.

`UpdateTargetData` 0x051B4950, per entry per call: weak get, `CityHash64` + `FindIndexByHash` again,
`IsEnemyFaction`, `IsDead`, `UpdateTargetLOS_Async`, `GetTargetDefaultThreat`.

The cost scales with (components that gather each tick) x (GAS actors), plus (entries) x (UpdateTargetData).

## Step 1: count (diag in ubwperf.dll, host only, no behavior change)

Hooks on `TickComponent` 0x051AC040, `GatherTargetsInRange` 0x0518A240 and `UpdateTargetData` 0x051B4950. One row
every 600 frames: `card594: host per frame: ticks T, gathers G (of which no primary P), GAS actors A, in range R,
adds N, UpdateTargetData U, entries not enemy faction F`. F needs `IsEnemyFaction` (or the faction component) on
each entry. Read it only in the diag.

## Predictions (one host combat window, C1M1 range or a bug wave)

1. G is close to T: most bug target components have no locked primary and gather every frame.
2. Most entries (F/U above 50%) are other bugs, which are never valid targets.
3. If both hold, step 2 is one of these, each checked for loss against all consumers of the target set
   (`GetAllTargets`, threat, primary selection, the blackboard tasks):
   - a. Gather skips candidates that are not enemy faction before the hash. This is lossless only if no consumer
     reads friendly entries.
   - b. A component with no primary gathers on its interval `+0xB8` rather than every tick. This is lossy (target
     acquisition up to one interval later). It goes in only as an opt-in, or with the user's OK.
4. Proof for step 2: the host GameThread targeting cluster falls (predicted below half of 592 samples for a), with
   card 132 mismatches 0 and bugs still attacking both players.

## Step 1 result: c590b434a (b434, FED_C3M1, junior 2026-11-21)

- 22 host rows of 600 frames each. Ticks 136176: primary updates 122880 (90%), gathers with no primary 13296.
  Prediction 1 failed: most ticks have a primary.
- Each gather scans about 1547 GAS actors (20.57M in total). About 179 pass the range test (2.38M), and adds total
  52599.
- UpdateTargetData ran 2.65M times, about 201 per frame on average and up to 1151 per frame in the peak row. 92.2%
  of the entries are not enemy (`+0x4A` = 1). Prediction 2 holds.
- Peak row: gathers 2296, scanned 3617359, in range 653261, UpdateTargetData 690623 (660115 of them not enemy).

## Static read 2 (2026-11-21): option a is not lossless

- `FADTargetData +0x4A` is written by UpdateTargetData as `IsEnemyFaction + 1`: 1 = not enemy, 2 = enemy.
- The readers of the set are found by key (GetTargetData, FindTargetData, the primary functions), plus two loops:
  primary selection 0x051A5940, and `GetAllTargets` 0x0518AFE0, which only `CanPerceiveTargets` calls
  (ASTEnemyBase 0x051804F6, ASTNonPlayableCharacterBase 0x051805F5).
- `CanPerceiveTargets` counts an entry when `+0x4A == 2`, or when the target is an NPC-class actor (the class is a
  lazy StaticClass at 0x050CC5E0; its PDB name is ICF-folded, so it is unconfirmed) with byte `+0xAE8 >= 1`. Entries
  that are not enemies therefore feed perception, so Gather must keep them. Option a is dropped.
- UpdateTargetData starts LOS traces (`UpdateTargetLOS_Async`) only for the primary entry, so entries that are not
  enemies cost no traces. Their cost per call is:
  - a weak get;
  - `CityHash64` + `FindIndexByHash` of the component's primary GUID `+0x190`, repeated on every call only to test
    whether this entry is the primary;
  - `IsEnemyFaction` (`GetTargetFaction` + `HasMatchingGameplayTag`, about 155 GameThread samples);
  - `IsDead` (health component lookup, card 536);
  - the distance and threat math.

## Step 2 candidates (lossless, ranked by the samples they touch)

1. Memo `IsEnemyFaction(self, target)` across frames, keyed by the component and target pointer and the target's
   GUID. Use the card 540 pattern: verify 1 call in N against the original and count any that differ, plus an
   environment switch for A/B. Faction is fixed for an actor's life in FED co-op, but differ must read 0 before the
   memo can be called lossless. It touches about 155 samples.
2. Hoist the primary lookup: one find of `+0x190` per UpdateAllTargetData pass, not one per entry. This needs a
   reimplementation of UpdateTargetData (0x3D0 bytes) or of the loop that calls it. It touches part of the 128 TSet and
   62 CityHash samples. It is harder, so it waits for 1's result.
3. GetAllAbilitySystemActors per gather: one shared list per frame instead of a fresh allocate, copy and free
   for each of the 2296 gathers in the peak row. The scan itself stays the same.

## Step 2.1 result: c594b435a (b435, junior 2026-11-21): IsEnemyFaction memo

- Memo served 33.6 % of calls, verified 2372, differ 0. The 30-frame expiry caps served.
- Bugs still attack: OnDamageCaused 30; client attack animations 28 against 20 before. card 132 mismatches 0.
- Not yet shown: the GameThread sample drop.

## Step 2.2 (senior 2026-11-21)

- GameThread profile A/B in one scenario: `UBWCOOP_C594_MEMO_OFF=1` against on, host GT samples on
  `HasMatchingGameplayTag` + `GetTargetFaction` (155 in c573b398a). Prediction: on drops them by at least 30 %.
- Raise the expiry from 30 to 300 frames, keep verify 1 in 64 and the differ count. Faction is fixed for an actor's
  life. The memo needs a forget on DestroyActor (the card 572 step 7 pattern) so a reused address starts empty; add it if b435 has none. Prediction: served above 80 %,
  differ 0.
- Candidates 2 and 3 wait for these numbers.

## Step 2.2 in b436 (ubwperf `77c1879cadea3d20`), junior 2026-11-21

- Expiry 300 frames, verify 1 in 64, differ count kept.
- Forget (deviation): the memo table is thread_local in ubwperf, and DestroyActor's hook is in dwmapi. So the key
  replaces a DestroyActor forget: each row also stores the target's and the faction actor's object serial and index
  (GUObjectArray), and a lookup matches only when both still agree. A reused address has a new serial, so it misses
  and starts empty. Actors with no serial (below 0) are not memoized.
- Rows are 40 B (40 KB table).
- GT A/B: c594b436a (memo on) and c594b436b (`UBWCOOP_C594_MEMO_OFF=1`). Each run has a 20 s
  `tools/c573_capture.sh` window while the client sits in its Door Gunner seat, saved in logs/c594b436{a,b}-gt/.

### Step 2.2 result: c594b436a/b (b436, junior 2026-11-21): no measurable GameThread change

- a (memo on): calls 442368, served 179291 (40 %), verified 2846, differ 0. b (off): calls 704512.
- GT samples, HasMatchingGameplayTag + GetTargetFaction: 15 + 3 of 6691 (a) against 13 + 5 (+1) of 7168 (b). The
  cost is small in this seat window. c573b398a (a bug wave) had 89 + 49 of 4734.
- Deviation, accepted: the key holds the object serial and index of the target and faction actors instead of a
  DestroyActor forget (the table is thread_local in ubwperf).
- Senior decision: the A/B must repeat in a bug-wave window (the c573b398a shape) before the memo stays. If that window
  also shows under 30 % drop, remove the memo.
- Queued (runs held for the playtest): the bug-wave A/B. The c573b398a shape (FED_C1M1 range or a C3M1 bug wave with
  the capture during the wave, no seat). Memo on against `UBWCOOP_C594_MEMO_OFF=1`; keep it only if HasMatchingGameplayTag
  plus GetTargetFaction GameThread samples drop by at least 30 %.

### Step 2.2 rerun in a bug wave: c593b437ca/b (b437c, junior 2026-11-21): no drop, remove the memo

- Window: 20 s `tools/c573_capture.sh` while 16 code-spawned warrior bugs attack the host (god), no seat. Captures in
  `logs/c593b437c{a,b}-gt/`, symbolized to `host-frame.txt` with `tools/perf_frame.py --log`.
- Host GameThread samples, HasMatchingGameplayTag + GetTargetFaction: a (memo on) 11 + 3 = 14 of 8389 (0.167 %);
  b (`UBWCOOP_C594_MEMO_OFF=1`) 8 + 3 (+1 interface thunk) = 12 of 8623 (0.139 %). On is not lower than off.
- card594 row in a: calls 36864, served 16575 (45 %, under the 80 % prediction), verified 263, differ 0.
- Under the 30 % rule: the memo goes. The pair is about 0.15 % of the GameThread either way.

## Decision (senior 2026-11-21, after c593b437ca/cb)

- The bug-wave A/B shows no drop (14 of 8389 against 12 of 8623 samples). Under the 30 % rule the IsEnemyFaction
  memo comes out in the next build.
- Candidates 2 and 3 scale with GAS actor count (c573b398a had about 1547 GAS actors per gather). They stay parked
  until a full-load host profile shows the targeting cluster above 5 % of GameThread samples. With 16 warriors it is
  0.17 %.
