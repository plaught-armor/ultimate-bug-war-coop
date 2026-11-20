# Card 543: performance mod, plan for cache misses and lookups after cards 536 to 540

Family: card 527 (performance mod). This is a plan; each step gets its own card when it starts.

## Where the time is (c263L362, b356, host perf frame)

- The GameThread is the critical path.
  - Its wait functions are near zero (FTickTaskManager 0.2%, ProcessTasksUntilQuit under 0.1%).
  - Background Work spends 15.2% of its samples in FWaitingQueue::CommitWait, which is idle. So BG has headroom,
    and BG savings do not shorten the frame.
- GameThread lookup costs:
  - Component lookup is solved: detour_c536_find 0.3%, ACharacter::FindComponentByClass 0.1%.
  - Card 540 targeting: DoesTargetMeetConditions 0.5% plus detour 0.1%.
  - FWeakObjectPtr::Get 1.3% on the GameThread and 1.5% in BG. Each Get resolves an object index through
    GUObjectArray chunks and item serials, so it costs cache misses. The callers are not known.
  - Our detour_c531_save_id: 0.6%.
- The largest GameThread cost is scene queries: Chaos::TAABBTree 14.8%, TSQVisitor 2.4%, TAABBTreeLeafArray 1.2%,
  FChaosEngineInterface 1.7%. That is about 20%.

## Plan, in order of GameThread gain

1. **Scene query owners (card 534 rows, host).** Rank the host's sweeps, overlaps and traces by caller class, as the
   client was ranked for card 539. Card 542 removes the client's bug-against-bug sweeps; the host side is
   unmeasured. Each class gets its own question: can it query less often, or with a smaller shape or channel set?
2. **FWeakObjectPtr::Get callers.** Add a sampled two-frame caller table, the card 536 "none caller" pattern, on
   FWeakObjectPtr::Get. Then:
   - for callers in our mod, keep the resolved pointer for the frame, or store an object index and serial and
     check it directly;
   - for game callers, card each hot one separately.
3. **detour_c531_save_id (0.6%, ours).** Trace why it runs that often, and cut it to once per actor per frame.
4. **DoesTargetMeetConditions health gather: parked.** It is 71.7% of host component lookups (ADHealthComponent),
   but its cost is mostly in BG, which has idle time. Reopen when BG idle falls under 5% at peak, or when the
   GameThread share of DoesTargetMeetConditions plus its detour passes 1.5%.

## Rules for every step

- One row per measured cost, with its share and sample count from a named run; predictions are written before the
  build.
- Never change a host decision. Every cache is verified by calling the original 1 time in N and counting a
  mismatch, as cards 536 and 540 do.
- Table sizes: keep each per-thread table at 64 KB or less unless a run shows that a larger one pays. L360 to L362
  showed a 128 KB table costs more than it saves.

## Step 1 result (static, L362 host callers symbolized)

| Caller (exe RVA) | Function | L362 host |
|---|---|---|
| 0x3d22dcf | UKismetSystemLibrary::CapsuleTraceSingleForObjects (Blueprint) | 17417 calls, 696.4 ms, 40 us mean |
| 0x3a2bd3f | UCharacterMovementComponent::FloorSweepTest | 11131 calls, 201.6 ms |
| 0x3d406d0 | UKismetSystemLibrary::SphereOverlapComponents (Blueprint) | 93 calls, 140.2 ms, 1507 us mean |
| 0x50f60dc | USTAnimNotify_NPCFootStepsEffect::Notify_Implementation | LineTraceSingleByChannel, 127.8 ms |

The top host query is a Blueprint capsule trace. The exe frame cannot name the Blueprint; the next diag names the
calling Blueprint function (the VM's current FFrame node) and the owner class for those two Kismet calls. Footstep
traces are effect-only. A host decision does not depend on them, so they are a perf mod candidate (distance cull)
once the owner and distance counts exist.

## Footstep notify trace (static, 2026-11-20)

USTAnimNotify_NPCFootStepsEffect::Notify_Implementation (exe 0x1450f5c50), 127.8 ms per 30 s in L362 host callers:
- It first checks that the owner is an ASTNonPlayableCharacterBase with NPC data, then calls
  ASTNonPlayableCharacterBase::IsPlayMovementFX (exe 0x1451985b0). That reads a component through vfunc +0x748,
  calls its bool vfunc +0xe0, then finds an entry in the map at this + 0x760 and tests the entry's byte at +0x2d.
  This looks like an existing significance or LOD gate, but that is not proven.
- After the gate: one UWorld::LineTraceSingleByChannel from the foot socket along the up vector (ignoring the owner),
  then USTGASUtilityLibrary::ExecuteGameplayCueFromHit. Each side runs its own anim notifies, so the effect is local
  and a cull on one side changes nothing on the other.
- A distance cull would overlap the existing gate. Before carding a lever, a diag must count notifies that pass the
  gate, bucketed by distance to the local view. At 0.43% GameThread this ranks below the card 545 Kismet callers.
