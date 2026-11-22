# Card 532: the client runs every NPC's targeting itself, at full cost

Co-op mod card (the cost exists only because the client is a network client). Found in the card 527 profile.

## Finding (c263L345, b339, both sides profiled at digest round 4, 20 s)

- The client GameThread spends about as much on NPC targeting as the host does:
  - UADTargetComponent 1.7% (GatherTargetsInRange 0.8%, UpdateTargetData 0.4%)
  - USTAttackComponent 1.0% (DoesTargetMeetConditions 0.4%)
  - the TMap<FGuid, FADTargetData> find 1.4%
  - GetSaveId 0.4%, plus most of AActor::FindComponentByClass at 4.4% (card 531)
  - an unknown part of the Chaos raycast share (3.4%), which UpdateTargetLOS_Async issues
- UADTargetComponent::TickComponent (exe+0x51AC040, 0x130 bytes) has no authority or net-role check. Every tick it
  runs UpdateTargetData for the current target, GatherTargetsInRange (0x518A240) on its gather interval
  (+0xB8, timer at +0x1AC), then 0x51B1CA0 and SelectPrimaryTarget (0x51A5940).
- UADTargetComponent has no GetLifetimeReplicatedProps and no OnRep functions. So nothing in its target table or its
  primary target reaches the client from the host. The client builds its own guess, and it can differ from the
  host's.
- The AI controllers and behavior trees exist only on the host, so the client's attack decisions are not made from
  this data. ChooseAvailableAttack and PerformAttack are host paths (card 122).

## Why it matters twice

1. Cost. The client pays roughly 8-10% of its GameThread for a result that nothing authoritative uses.
2. Sync (full world sync rule). Anything visible on the client that reads the local primary target shows the
   client's guess, not the host's. Possible readers are OnTargetChanged (a BlueprintAssignable multicast) bound
   in NPC blueprints, and anim blueprints that read the target for head or aim tracking.

## Steps

1. Readers (runtime, client, one short run): count the OnTargetChanged broadcasts on the client, and the bound
   functions per NPC class at BeginPlay (the delegate's invocation list). Also count the client GetPrimaryTarget
   calls by caller class, through the existing ProcessEvent hook for BP calls. Prediction: the anim blueprints
   are the only per-frame readers.
2. Fix:
   - No visible reader: on the client, turn off the component's tick at BeginPlay (SetComponentTickEnabled false).
     Keep Activate's first gather, or skip it as well.
   - Visible readers: also send the host's primary target per NPC when it changes. The host's SetPrimaryTarget
     detour already exists for card 103. On the client, write it with SetPrimaryTarget, so the readers see the
     host's choice. One small row per change, reusing the NPC id.
3. Proof: perf on the client on the same route. The targeting rows go to about 0, and FindComponentByClass falls.
   The step 1 counters show that each visible reader matches the host's target (sampled row compare).

## Step 1 result (senior, static, 2026-11-20)

Direct native callers of the readers (E8 scan of .text, scratchpad callers532.py):

- `GetPrimaryTarget`: USTFleeComponent (CanFindValidFleeLocation, InFleeRange), USTAttackComponent
  (FindBestTargetForAttack, UpdateTargetAwareness), ASTAttackSystemEnemyBase::IsRepositionRequiredForCurrentTarget,
  ASTNonPlayableCharacterBase::GetAttackTarget, TickUpdateRevertToUnawareTimer (NPC and ASTEnemyBase),
  **FSTBugProceduralAnimationData::UpdateFromNPC**, GetDebugDisplayString, and the BP thunk.
- `FindTargetData`: attack component LOS and condition checks, HasLineOfSightToActor, MoveToPreferredRange.
- `GetAllTargets`: CanPerceiveTargets. `GetTargetThreat`: HandleDamageEvent, HasActorOnTargetList, MoveThreatTo.

The visible client reader is the procedural animation. UpdateFromNPC (exe+0x510B9D0) calls GetPrimaryTarget when the
anim data flag +0x54 is set and, when the target data byte +0x48 equals 2, sets anim data +0x3 and builds a vector
from the NPC to the target: the bug's body and head turn toward its primary target. On the client that target is the
client's own local choice, so a host bug chasing player A can face player B on the client screen. This is a full
world sync gap as well as a cost.

Decision for step 2: the client's target tick stops (cost), and the host's primary target per NPC is sent on each
change (card 103 SetPrimaryTarget hook, host side) and applied on the client through SetPrimaryTarget, so the
anim reader sees the host's target. Payload: NPC net id + target net id (or none) + the +0x48 state byte, one row per
change. The AI readers (flee, attack, reposition) run only where the AI runs (host); confirm the client never calls
them before the tick stops, by a run with a count hook on GetPrimaryTarget callers on the client.

## Step 2: implementation (b341)

Built as build-b5/dwmapi-b341-4e81a71006ada29b.dll (b340 + this card), no warnings. Code: mod/src/ubw/target_c532.cpp.
The design changed from the decision above in one way: the client does not call SetPrimaryTarget. It overrides
GetPrimaryTarget instead, so no client target data is written and nothing else on the client reacts to a set.

- Host: after each SelectPrimaryTarget (detour_c103_select), FindPrimaryTargetData gives Target (+0x0) and
  HasLineOfSight (+0x48). A change from the last sent pair for that component queues one row in ChanOp::NpcTarget
  (blob): varint NPC NetGUID, varint (target NetGUID << 2 | HasLineOfSight), target 0 = none. The queue is flushed to
  every ponged client at most every 50 ms from c42_step, or earlier when the 2623-byte blob is full. When the ponged
  count grows (a join), the table empties so every NPC is sent again at its next selection. Rows with no NetGUID or
  with HasLineOfSight above 3 are skipped and counted, then retried at the next selection.
- Client: UADTargetComponent::TickComponent turns its own tick off (SetComponentTickEnabled false, vfn 0x410) and
  returns. GetPrimaryTarget for an NPC with a host row writes Target, HasLineOfSight and TargetLastKnownLocation (the
  target's location now) and returns true, or returns false when the host's target is none or gone. An NPC with no
  row yet keeps the original. Rows whose NPC or target does not resolve wait in a 64-row ring for up to 2 s.
- Both sides reset at map arm (c99_arm_map) after a final totals row.
- Log rows: `card532: host ... rows N in F frames, B bytes (x B/row), skipped, table full, join resets` and
  `card532: client ... rows applied, pending, dropped, bad frames, target ticks stopped, GetPrimaryTarget overlays,
  table full`, every 60 s while rows move, and at map arm. A bad blob logs `WARN card532:`.

## Proof asks (b341)

1. Client GameThread cost: the card 527 profile at the same round as c263L345. UADTargetComponent
   (GatherTargetsInRange, SelectPrimaryTarget, UpdateTargetData) should be near 0% on the client.
2. Sync: client `overlays` grows; `dropped` and `bad frames` stay 0 or near 0; `table full` 0.
3. Bytes: host B/row (expected about 6 to 8) and total bytes per minute at a peak wave.
4. Visual: with both players apart, a bug chasing the host faces the host on the client screen (one screenshot pair).
5. No new WARN rows, no crash; the card 533 proof rides in the same run.

## b341 crash and fix (b342)

c263L347 on b341: the host crashed 41 s in, at the first warrior spawn (AV in AActor::GetWorld on 0x137DF00038749).
FADTargetData::Target (+0x0) is a TWeakObjectPtr<AActor> ({index, serial}), not a raw pointer; c532_host_note read it
raw and passed the packed value to c41_guid. The client overlay had the matching bug: it wrote a raw pointer where the
caller expects a weak pointer. b342 reads it as a weak pointer on the host and writes the stored {index, serial} on
the client. The b341 proof asks stand for b342.

## c263L348 (b342) result

- Client UADTargetComponent left the GameThread top lists; L346 had it at 1.7%. Host unchanged, at 1.7%.
- Rows: 14061 applied, 5 dropped, 0 bad frames.
- Wire: 3.7 B/row, against a prediction of 6 to 8. Peak about 514 B/s.
- 0 crashes, 0 AV.
- The facing shot fits client bugs facing the host, but it does not prove it for a single bug. The single-bug shot
  is still open.

## Closed (senior 2026-11-21)

The overlay has shipped in every build since b342, including the b366 playtest on FED_C2M1 and the runs up to b440.
No card or user report since then has shown a client bug facing the wrong player, and card 132 mismatches stay 0. The
open single-bug shot is waived on that record. A wrong-facing report later gets a new card with the card532 rows.
