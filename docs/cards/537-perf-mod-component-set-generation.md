# Card 537: perf mod, validate the FindComponentByClass cache by component-set generation

Family: card 527 (performance mod). Follows card 536. User order 2026-11-20 ("then do option 3 for now").

## Why

Card 536 cut the host AActor::FindComponentByClass walk from 8.5% to 0.1% of Background Work, but its own detour
costs 2.2% (L353) to 2.5% (L354, b349). Removing the actor weak pointer in b349 did not help. L354 instruction
samples inside detour_c536_find (b349 map, 390 samples): about 55% on the load of the cached component's owner
(component +0xA0), about 25% on the cache entry load. The cost is cache misses on the component memory, not the
FWeakObjectPtr::Get call. "None" results were also not cached: 507466 walks that found nothing on the host in L354.

## What (b350)

- The walk's result depends only on the actor's owned-component set. The set changes only through
  AActor::AddOwnedComponent, RemoveOwnedComponent and ResetOwnedComponents (static caller scan in the card 536 notes).
  Those three are detoured. Each bumps a generation counter for the actor's object index (65536 slots, index & mask)
  once before and once after the original.
- A cache entry holds {actor, class, result, object index, generation read before the walk}, 32 bytes. A hit needs
  actor, class, index and generation to match. There is no component dereference. "None" results are cached too.
- The same object index at the same address after a free: AActor::PostInitProperties always calls
  ResetOwnedComponents, which bumps. A change racing a walk on another thread leaves the stored entry one
  generation old, so it never hits.
- Kept: the verify every 1024 hits per thread (none hits included) and the mismatch count, and the none caller
  sampling. Code: mod/src/ubw/perf_component_c536.cpp. `UBWCOOP_C536_OFF=1` turns off all four detours.

Build: build-b5/dwmapi-b350-75c3fa9b4913612a.dll (+ .map) = b349 + this card.

## Predictions (one perf -b run, same route as L354)

- card536 rows: mismatches 0 on both sides. The host hit rate (hits plus none hits) is above 99.5%, and the none
  walks are below 10% of L354's 507466.
- detour_c536_find on the host is below 1.2% of Background Work (L354: 2.5%). The three new detours together are
  below 0.1%.
- The host AActor::FindComponentByClass self share does not rise above L354 (0.1% of BG, 0.2% of the GameThread).
- No crash. No new WARN kinds.

## Self-review

- Attacked: a change racing a walk on another thread, an address and index reused after a free, a component renamed
  to another actor (PostRename goes through Remove and Add), a collision of two indexes in one generation slot (it
  only costs misses), the hook install order (the three set-change hooks install before the cache hook).
- Open gap: an exe path that edits OwnedComponents without the three functions, or garbage collection clearing a set
  entry. The verify count measures it.

## c263L355 (b350)

- Mismatches 0 on both sides; CRASH/AV 0; no new WARN kinds. This is a correctness pass, and b350 is now lane 1.
- Host hit rate 96.18% (target above 99.5%: FAIL). Misses rose from 2.16M to 4.72M, about 30 per set change
  (154115 changes). None walks 278910 (target below 50k: FAIL, though L354 had 507466).
- Host Background Work: detour 1.3% (target 1.2%: FAIL), the walk 0.5% (L354 0.1%). The two together are 1.8%,
  against 2.6% in L354. The three set-change detours have 0 samples.
- Next (v2): log which actor classes bump the most, and whether the bumps land on hot actors or on colliding
  index slots. Then either key the generation by the exact index or narrow what a change invalidates.

## v2 (b351)

The change is in mod/src/ubw/perf_component_c536.cpp (header comment "Card 537 v2").
- A change can only alter the result for a class that the changed component IsA. AddOwnedComponent and
  RemoveOwnedComponent therefore bump one slot per class in the component's SuperStruct chain, keyed by
  (actor index, class), with 131072 slots.
- ResetOwnedComponents, and a change with a null component, bump a per-index "all" generation.
- A hit checks both its (index, class) slot and the "all" slot.
- New `card537:` rows name the top 5 component classes by set changes. A class is named only while
  object_listed, because blueprint classes unload with their map.

Build: build-b5/dwmapi-b351-a9bbca4c713d724f.dll (+ .map).

Predictions for the same route as L355:
- mismatches 0 on both sides;
- host hit rate above 99% (L355: 96.18%);
- none walks below 100k (L355: 278910);
- detour plus walk below 1.4% of host Background Work (L355: 1.8%);
- the class rows show which components churn.

## c263L356 (b351, v2) results and the b352 change

- Mismatches 0 on both sides, CRASH/AV 0. The host hit rate is 96.03% (L355: 96.18%) and none walks are 276021: both
  flat, so the per-class generations did not remove the misses. The detour plus the AActor walk is 1.6% of host
  Background Work (L355: 1.8%). The detour_c537 functions have no samples. b351 is promoted to lane 1.
- Top set-change classes on the host: AudioComponent 26973, BrushComponent 23303, NiagaraComponent 13272,
  StaticMeshComponent 4957, SceneComponent 2831. None of these is a class the hot callers look up.
- New premise: the misses are capacity misses, not invalidations. 1024 sets x 2 ways per thread is 2048 entries,
  against about 300 live actors times every class looked up on each worker.
- b352 (build-b5/dwmapi-b352-61c380eacb947494.dll): 4096 sets (320 KB per thread). The set index now uses hash
  bits >> 40 (the old >> 54 gave only 10 bits). Each miss is also counted as stale when its (actor, class) is still
  in the set, which means the index or generation check failed. The log row shows `stale N`.
- Predictions: host hit rate above 99%, stale below 10% of misses, mismatches 0. If stale is most of the misses,
  the premise is wrong and the generations are what to look at.

## Result (c263L358, b352, 4096 sets)

- Host: 98.52% hit (L356 96.03%). Misses fell from 3895773 to 1420086 (-64%), so the capacity misses were real.
- Stale misses: 4039, 0.28% of misses. None walks are still 260348, flat; they are not capacity misses.
- BG detour + AActor + ACharacter FindComponentByClass: 2.5%, against 2.6% in L356. Mismatches 0. b352 promoted.
- The 99% target was missed. The remainder is mostly none walks (classes the actor does not have), which a
  negative entry would cover. That is the next lever if the cost justifies it.
- c263L359 (b353, same code): host 99.69% hit, misses 323451, stale 5275. The 99% target is met; L358's 98.52%
  was a shorter run, with more of it spent in warm-up.
