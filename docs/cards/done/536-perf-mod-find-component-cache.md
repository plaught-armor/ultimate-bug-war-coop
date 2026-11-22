# Card 536: perf mod, per-thread cache behind AActor::FindComponentByClass

Family: card 527 (performance mod family). Status: in source, build b347
(build-b5/dwmapi-b347-cb60fc3474ceffbc.dll + .map = b346 + this card). Not yet run.

## Why

c263L351 (b345, perf -b, host, 20 s at digest round 4), after card 531:
- AActor::FindComponentByClass is still 8.5% of Background Work (1018 samples), 2.2% of Foreground Work and 1.4%
  of the GameThread. ACharacter::FindComponentByClass adds 1.4% of Background Work.
- Callers: 65% show no entry in the branch stack (the walk's own loop fills all 16 entries), 27% come through
  ACharacter's override (exe 0x1439ae084, after its mesh, movement and capsule checks) and 6.5% from
  USTAttackComponent::DoesTargetMeetConditions.
- Card 531's GetSaveId cache costs 0.6% of the GameThread and 2.0% of Background Work. Its callers are now a small
  part of the lookups.

## What

`mod/src/ubw/perf_component_c536.cpp` detours AActor::FindComponentByClass (exe RVA 0x37E0A00). The TSubclassOf
argument arrives by pointer (rdx; see the `lea rdx` at 0x1439ae07c).
- Cache: per thread, 1024 sets of 2 ways, keyed by (actor, class). Each entry holds weak pointers to the actor and
  the component.
- Hit: the actor and class match, both weak pointers resolve, and the component's owner is still the actor.
- A miss runs the original and stores a found component. "None" results are not stored.
- Every 1024th hit on a thread also runs the original and counts a mismatch.
- For every 64th "none" call on a thread, the two frames above the detour are recorded (RtlCaptureStackBackTrace).
  This names the callers that still pay a full walk.
- Rows: `card536:` totals every 60 s, and the top 10 "none" callers as exe+RVA or mod+RVA.
- Off switch for A/B: `UBWCOOP_C536_OFF=1`.

## Predictions (written before the run)

1. Mismatches 0 on both sides.
2. Hit rate above 90% of non-none calls on the host.
3. Host Background Work: AActor::FindComponentByClass falls from 8.5% to below 3%, and detour_c536_find costs
   below 2%. The sum stays below the L351 8.5% + 2.0% (card 531 detour).
4. "None" calls are a minority. Their callers are named for the next lever.

## Proof

A c263L run with perf -b on b347, host and client (perf_frame.py --log <run log> --mod-map
build-b5/dwmapi-b347-cb60fc3474ceffbc.map). Score P1 to P4 from the card536 rows and the perf listing. If P3 fails
because the hit path costs as much as the walk, turn the card off with the switch and cut the hit check.

## Self-review (b347)

Attacked:
- the class argument: by pointer, as card 531's vtable call and the ACharacter call site show.
- actor reuse at the same address: the actor weak pointer fails (serial differs), so the lookup misses.
- a destroyed component: the weak pointer excludes garbage, so the lookup misses and walks. The original can still
  return a garbage component, and then it is returned unchanged.
- a component added later with the same class that iterates first: the cache keeps the older one. The verify count
  measures this (P1).
- a Blueprint class unloaded and another loaded at the same address, with the same live actor: an IsA mismatch is
  possible. It is rare, and the verify count measures it.
- threads: the caches are thread_local. Totals are relaxed atomics. The caller table takes an SRW lock, only on
  sampled none calls.
- frame skip: c536_sample_none is noinline. Skipping 2 frames leaves the detour's caller (the hook enters the detour
  by a jump) and that caller's caller.

## c263L353 (b348): pass, except the detour cost

- P1: mismatches 0 (host checks 97318, client 1641).
- P2: host hit 98.71%, client 94.79%.
- P3: host AActor::FindComponentByClass 0.2% of Background Work (L351 8.5%), 0.1% of the GameThread (1.4%), 0.1% of
  Foreground Work (2.2%). detour_c536_find 2.2% of Background Work (prediction below 2%: FAIL), 0.3% of the
  GameThread. Net Background Work cut about 6.1 points.
- P4: none calls are 0.5% of host calls. Top none callers (b348 map): the mod's own c132_inventory (map load and map
  end pass, 41%), detour_c531_save_id for actors without a save component (21%), c132_actor_state's health lookup
  (host 4%, client 26%), then exe callers at 6% or less each. None of them is worth a card at today's cost.
- Host and client caller rows with equal counts (302): separate processes and tables, so a coincidence.

## b349 (build-b5/dwmapi-b349-5f9a858cba25c328.dll + .map = b348 + this change)

The hit path drops the actor weak pointer: one FWeakObjectPtr::Get per hit instead of two. A destroyed actor's
components become garbage with it, and the component weak pointer excludes garbage, so a live component owned by the
actor at this address belongs to that same actor. A destroyed actor still in memory now misses and runs the original,
which returns the same result as before. Entries shrink from 32 to 24 bytes. Prediction: detour_c536_find below 1.5%
of host Background Work, mismatches 0.

Not carded: a column table per component class indexed by object index, filled on component register and
unregister. It would remove the remaining hit cost, but the walk is now 0.2% and the detour about 2%, so it waits
until that is the top row.
