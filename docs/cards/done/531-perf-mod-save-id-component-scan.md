# Card 531: perf mod, cache the save component behind USTSaveComponent::GetSaveId

Family: card 527 (separate complementary performance mod, not the co-op mod).

## Finding (c263L344 host, b339, perf at digest round 4, 20 s, 33,545 samples)

- AActor::FindComponentByClass is the largest single game function outside Chaos: 4.3% of the GameThread samples
  and 11.0% of the Background Work samples. ACharacter::FindComponentByClass adds 0.8% more on the workers.
- USTSaveComponent::GetSaveId (exe+0x51FE7F0, 0xA0 bytes) is a FindComponentByClass call. It calls the actor's
  vtable +0x720 (FindComponentByClass) with USTSaveComponent::StaticClass(), then copies the FGuid at component
  +0x164. FindComponentByClass walks the actor's OwnedComponents and runs IsA on each one.
- The targeting code keys its per-target table, TMap<FGuid, FADTargetData>, by that save GUID. So every target
  lookup runs one component walk: UADTargetComponent FindTargetData, FindOrAddActor, GatherTargetsInRange,
  Set/ClearPrimaryTarget and SetTargetPriority, plus USTAttackComponent UpdateTargetAwareness and PerformPreAttack
  (static E8 callers of GetSaveId).
- The same samples show the targeting work next to it: DoesTargetMeetConditions 1.8%, the FADTargetData TSet find
  1.9%, GetSaveId itself 1.1% (Background Work), GatherTargetsInRange 0.7% and UpdateTargetData 0.4% (GameThread).

## Lever

- Detour GetSaveId and look up the actor's save component in a cache keyed by the actor's object index and serial
  number, instead of walking the components. Read the GUID fresh from component +0x164 on every call, so a load
  that rewrites the id is still seen. A cache hit must check that the component is still alive (same index and
  serial in GUObjectArray) and still has the actor as its owner. On any failed check, call the original function
  and store its result.
- The function runs on the task workers too, so the cache must be safe across threads. Use a fixed direct-mapped
  table whose entries are written with one 16-byte atomic store, and never take a lock.
- The results are the same as the game's own, so host decisions do not change. That makes it safe for co-op as
  well.

## Proof

- Before: the c263L344 numbers above. After: one perf run of the same route on the build with the detour. Its
  FindComponentByClass share plus its GetSaveId share must drop, and the GameThread frame ms must not rise.
- Also log the cache hit, miss and fallback counts once per digest round. Expect hits above 99%, and the
  component that is returned must match the original function's result on a sampled 1 in 1024 calls.

## Open

- Callers of the remaining FindComponentByClass share (UADFactionComponent::GetTargetFaction and others call it
  through the vtable, so a static E8 scan misses them). Get them from the next profile run, recorded with `-b`
  (see card 527, perf_frame.py --callers).

## Implementation (b342)

2026-11-20 senior. c263L346 (perf -b, fixed perf_frame.py) confirmed the premise: of the FindComponentByClass entries
the branch stacks show on the host, GetSaveId is about 88% (directly, and through ACharacter::FindComponentByClass,
whose callers are about 92% GetSaveId). The client is similar.

`mod/src/ubw/perf_save_c531.cpp` detours USTSaveComponent::GetSaveId on both sides. Each thread keeps a 512-slot
direct-mapped cache, keyed by actor address, holding the actor and its save component as weak pointers. A hit needs
both weak pointers to resolve to the same objects and the component's owner to still be the actor; the GUID is then
read fresh from component +0x164. A miss runs the original lookup (vtable +0x720 with USTSaveComponent::StaticClass,
new symbol row) and stores it. Every 1024th hit per thread re-runs the lookup and counts mismatches. The game thread
logs `card531:` totals (hits, misses, no component, checks, mismatches) every 60 s. Skipped: the original's
incremental-GC barrier on the class pointer (a native UClass is never collected). `UBWCOOP_C531_OFF=1` leaves the
detour out for an A/B run.

Build: build-b5/dwmapi-b342-ba9cd2c36cb4550a.dll (b341 + card 532 weak pointer fix + this card).

Proof asks: `card531:` rows on both sides with mismatches 0 and a hit rate; the perf share of FindComponentByClass on
the GameThread and Background Work threads against c263L346 (4.3% and 11%); no crash.

## c263L348 (b342) and the b345 change

- No mismatches: host checks 50767, client checks 1412.
- Client hit rate 99.61%. Client FindComponentByClass went from 4.1% of the GameThread and 11.4% of Background Work
  to below the listed rows (callers 2486 to 55 samples).
- Host hit rate fell from 81% to 63% as the wave grew. Host FindComponentByClass fell only about 15% (GameThread
  3.1% to 2.4%, Background Work 10.0% to 8.5%).
- Cause: the direct-mapped table. With about 300 live targets over 512 slots, about 45% of actors share a slot with
  another live one, and the two evict each other.
- b345 (build-b5/dwmapi-b345-cde43c04a766b216.dll): 1024 sets of 2 ways, 48 KB per thread. Prediction: host hit
  rate above 95% at the peak, mismatches 0, and host FindComponentByClass below 1% of the GameThread.

## c263L350 (b345): pass

Host hit rate 98.03% over the run, 97.42% in the peak window, 97.51% cumulative at the end (b343: 67.71%). Client
99.82%. Mismatches 0 on both sides. The host FindComponentByClass share is scored in the L351 perf run.

## c263L351 (b345, perf -b, host)

AActor::FindComponentByClass is 1.4% of the GameThread (L348: 2.4%). The prediction was below 1%: FAIL. Background
Work is unchanged at 8.5%. The GetSaveId callers are mostly gone; the remaining walks come from other callers
(card 536). The detour itself costs 0.6% of the GameThread and 2.0% of Background Work.
