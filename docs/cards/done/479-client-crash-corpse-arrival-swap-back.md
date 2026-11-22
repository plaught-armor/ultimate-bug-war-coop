# Card 479: Client crash in the card 269 swap back at a corpse arrival

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: Opened 2026-11-17 13:16 from run `c245a` (b186 `6bdc1fb92f415071`, C4M2). Crash, so it is fixed in the next build (b187).

## Seen
- Client row at 581553252: `CRASH 1/4: EXCEPTION_ACCESS_VIOLATION (0xC0000005) at exe+0x3AF2CB8`, reading
  0x1E30E0678, rax=1E30E0120, rcx=rbx=928AABA0. Host EXCEPTION 0. Digest rounds 1-14: 0 mismatches.
- Stack: exe+0x3AF2CB8 (`USceneComponent::SetHiddenInGame`+0x68, `call [vtable+0x558]`) < ubwcoop+0x58090
  (`c269_swap_back`, enemies.cpp:3303) < exe+0x5102F42 (`USTSkeletalAnimatorComponent::SetShowingStaticMesh_Internal`
  +0x92) < mod frames (`c132_pose_arrival`).
- Context: a batch of corpse arrivals in one client tick (guids 0xa4, 0x72, 0xcc logged their swap back rows with
  "showing static 0, 6 frames of the deferred swap left"), right after the client's warp to the Bridge satchels.
  The crashing arrival logged no row, because the row is written after the call.

## Trace (static)
- `SetShowingStaticMesh_Internal(false, true)` (exe+0x5102F0C onward) calls `SetHiddenInGame(animator+0x128, true)`
  with no null or validity check. animator+0x128 is the static mesh component. The vtable read from it was
  0x1E30E0120, not an image address, so the pointer was dangling (freed or reused memory), not null.
- The game's own deferred completion (`TickComponent` exe+0x5107F40, count at +0x15C reaching 0) calls
  `SetSkeletalMeshEnabled(this, true, true)` (exe+0x5103000) and `SetStaticMeshEnabled(this, false)`
  (exe+0x5103470). `SetStaticMeshEnabled` also reads +0x128 raw, so the game would hit the same pointer when its
  count ran out. The mod's call only reaches it earlier, at the arrival.
- Not known: why +0x128 dangles on that copy (which class, whether the actor was being destroyed, whether the
  static mesh component was destroyed apart from its owner).

## Fix (b187)
1. `c269_swap_back` runs the original only when animator+0x128 is a live UObject: a GUObjectArray item at its
   index, not unreachable or garbage, class name containing `StaticMeshComponent`, and the owning actor not being
   destroyed. Otherwise it skips, sets nothing, and logs a capped row
   `WARN card479: client corpse arrival <name> guid <g>: static mesh component <ptr> not live (<reason>), swap back
   skipped; showing static <n>, <f> frames left, actor being destroyed <0|1>`.
2. When skipped and the count at +0x15C is above 0, the count is set to 0 so the game's tick does not reach
   `SetStaticMeshEnabled` with the same pointer. Stated cost: that body's skeletal mesh may stay hidden; the WARN
   row counts it.
3. The swap back row is logged before the call, so a later crash names its actor.

## Predictions
- A C4M2 run past the Bridge satchel warp: 0 EXCEPTION on the client; either 0 `WARN card479` rows (the bad copy did
  not recur) or a `WARN card479` row naming the class and reason, which gives the cause for a second step.
- 2 runs with no row and no crash do not prove the fix; the card then closes on the guard alone with the playtest
  check below.

## Done when
- The C4M2 run passes the point with 0 client EXCEPTION, and any `WARN card479` row is read and its cause recorded
  here.

## Build b187 `920b53a5d0bb7c2e` (2026-11-17)
No crash for about 50 s after the client's satchel pickup, 0 card479 rows. The placement sites and BridgeExplode were not reached (Bridge_SO1 never became active, card 245).

## Closed (2026-11-17 17:35)

Runs with the guard in place (b187 onward), each past the client's warp to the Bridge satchels: client EXCEPTION 0 in every C4M2 run. c245b193a covered both players' satchel pickups and all four placements. c245b194a covered BridgeExplode and Bridge_O1, after which the client played on into Sewers, 688 s with rc 0/0. Over all of them: 0 `WARN card479` rows and 0 `card479` rows.

Not done: the guard never fired, so why animator+0x128 dangled is still unknown. Closed on the guard alone, as the card allows.

Reopen when: any `WARN card479` row appears (it names the class and reason), or a client crash at `SetHiddenInGame` / `SetStaticMeshEnabled` reading the static mesh component.
