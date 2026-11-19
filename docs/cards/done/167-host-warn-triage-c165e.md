# Card 167: Two host WARN classes from c165e

Status: CLOSED, outcome in docs/CARDLOG.md. Was: queued 2026-11-09, from card 165 run 3 (c165e). Budget: static first, then one run if a fix is needed. Batch
the run into another card's build.

## Why

Both WARNs are host-side, not from claims. Neither has a written cause.

1. `card132: host hit BP_AI_WarriorBug_C guid 0x268: impact (13726,15845,198) is 1239 uu from the NPC at
   (13714,14607,199); damage 8.0` (and one at 1434 uu). These are NPC trooper Morita rounds. The impact is near the
   bug's spawn point, while the bug was already 1239 uu away. Either the impact point is stale (pooled round,
   spawn-time hit result), or the damage lands on a bug the round never reached.
2. `OnHit of ADProjectile, whose GrenadeExplode already went` (grenades.cpp:1505), 4 lines, next to NPC trooper
   Attack01. The same WARN is in c157d-host.log.

## Do

Trace each one statically and write down its cause. For item 1, say whether the game itself (with no mod) deals
that damage. If it is the game's own behavior, it is not a defect; write that down and drop the WARN to a line. If
it is the mod's, fix it.

## Done when

The cause of each is written in this card. Any fix is proven in one batched run with 0 of these WARNs, or with the
line now marked game behavior. It ends with "Report complete."

## Static trace (junior, from c165e-host.log)

1. Far impacts. Bug guid 0x268 spawned at (13600, 15700, 400) at t=280427670, and in the same ms the game's
   ASTNonPlayableCharacterBase::OnSignificanceChanged (exe+0x519E0A5) swapped it to the static VAT mesh: "showing
   static 0 -> 1, mesh collision 1 -> 0". It stayed static until its death (t=280437855 "showing static 1 -> 0").
   Its hits while static fall into two kinds. Hits on the moving body have "bone None" (t=280429195 at 214 uu, and
   t=280430150 / 280430443 near the actor). The two far ones have "bone leg_back_L0_end_jnt" and sit about 190 uu
   from the spawn point, both at about (13730, 15843), 272 ms apart, while the actor ran 197 uu. The event's
   hit-result bone is None in both, so the bone name is the game's own choice for the event, not a body the round
   touched. No SetCollisionEnabled write on the bug between spawn and the hits (the card 095 collision diag logs
   them all), and the mod writes no NPC collision or KinematicBonesUpdateType on the host (pose_c132's SkipAllBones
   hold is client only). Lead: something the game leaves at the swap pose still answers the rounds' sweep. The
   component is not known yet. The far WARN now names the hit component (FHitResult +0xD8 weak pointer) and its
   ComponentToWorld translation.
2. "already went". The four repeats (t=280436464 to 280436807) come during NPC trooper spray attacks; none has an
   FX spawn in the same ms, and none of the four rounds passed ReleaseToPool or SpawnProjectile between its sent
   frame and the repeat (both drop the ring entry). So the game ran a second OnHit on a live round that had already
   hit. The repeat now runs with the effect capture on, and the WARN names what it hit and whether it spawned an
   effect.

## Predictions (batched into c166b, DLL efad1ded8d518cc7)

- If trooper rounds hit the static bug again: the far WARN names the hit component. If it is the bug's
  SkeletalMesh (CharacterMesh0) at the actor's location with the impact at the swap pose, that is the game's own
  hidden-mesh body (game behavior; the line drops to plain text). A component at the spawn point would be a stale
  transform.
- Repeat OnHit lines show "effects system no sound no zone no" and a hit on an NPC or a world mesh. That is the
  game's own repeat with no effect lost, so it drops to a plain line. Any "yes" is a lost effect and gets a fix.
- Neither may happen in a short run. If neither does, item 1 stays at the lead above and item 2 stays without an
  effect check.

Closed 2026-11-09 (senior): 0 of either WARN in c168-r1 to r3 with the new diags on both sides; causes stay as
written above.
