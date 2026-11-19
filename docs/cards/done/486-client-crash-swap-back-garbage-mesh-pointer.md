# Card 486: client crash, c269_swap_back reads a non-pointer static mesh field (b212, c244b212a)

## Evidence
- c244b212a (b212 `a8562ff63c709122`, FED_C4M1), client crashed at ~1000 s, game thread 600. Dump
  `UECC-Windows-D188DC1B4CFD4908B7111EA11986FCE6_0000`. Host FATAL 0.
- EXCEPTION_ACCESS_VIOLATION reading 0x31F0000000D at ubwcoop+0x152C73.
- Stack (symbolized by relinking the same objects with `/map`; .text sha identical, 982d9a9b24a04a57):
  `object_present+0x13` (reflect.cpp:278, inlined object_item) < `c269_swap_back+0x2a3` (enemies.cpp, via
  c479_static_dead) < `c132_pose_arrival+0x1be` < `c38_death_edge+0x25f` < `c37_pawn+0x2ba` < `c37_scan+0x22d` <
  `ubw_post_tick+0xc15`.
- The read is `object + kObjectIndex (0xC)`, so `mesh = read_ptr(animator, 0x128) = 0x31F00000001`: not a pointer
  (odd, two packed ints 0x31F and 1). Card 479's guard (object_present / object_listed / class name) assumes the field
  holds a real address, freed or live; here the first guard step itself dereferences garbage.
- Last client rows before the crash: TankerBug fx near (-17669, -81778); the last card479 row
  `WARN card479: client corpse arrival BP_AI_TankerBug_C guid 0x808: static mesh component 00000000853DA600 not live
  (freed)` at 640054794 shows the same field already dangling on corpse copies in this map.

## Cause (static)
- animator+0x128 on this copy holds non-pointer data. Either the animator object at npc+0xA30 is itself stale
  (memory reused; the `SkeletalAnimator` class-name check read a class pointer that still resolved), or the field is
  not yet written on a fresh animator. Not known which.

## Proposed fix (class, in the responsible layer)
1. `object_item` (reflect.cpp) rejects any pointer that is not 8-byte aligned or not in user space
   (< 0x10000 or > 0x7FFFFFFFFFFF) before reading +0xC. Every guard built on object_present/object_listed then holds
   for garbage, not only for freed objects.
2. `c269_swap_back` checks the animator with `object_listed` before the class-name read, so a stale animator is
   skipped with the card479 WARN row (reason `animator not live`).
- Prediction: a C4M1 soak past the same TankerBug fights: 0 client EXCEPTION; any bad copy shows as a card479 WARN
  row with reason `unreachable or garbage` or `animator not live`.

## b213 (57aacb585ebdd2f1)

- Static check (senior b213 item 1): c269_swap_back does not cache the animator. It reads `npc+0xA30` (kNpcAnimator)
  fresh on each call, and the npc comes from c37_scan's weak refs each frame (c31_weak_get), so the actor is live.
  The stale piece is the animator component pointer on a live actor: its memory was freed and reused (0x31F00000001
  reads as two int32s, 1 and 0x31F, an FName or TArray num/max).
- Fix 1 (cause): c269_swap_back reads the animator only when `object_listed(animator)` holds (listed, live UObject),
  and logs `WARN card486: client corpse arrival ... animator %p not live (freed, unreachable or garbage), swap back
  skipped` (capped with the card479 rows) when it refuses. The animprobe loop in enemies.cpp takes the same test.
- Fix 2 (guard): object_item rejects any pointer that is not 8-byte aligned or outside user space
  (below 0x10000 or above 0x7FFFFFFFFFFF) before reading `object+0xC`.
- Prediction: c244b213a and c245b213a finish with 0 EXCEPTION; any refused animator shows as a WARN card486 row.

## b214 (class test, senior b214 item 1)
- Review finding: object_listed(animator) passed when the freed slot held another live UObject of the wrong class.
- Fix: c269_swap_back now requires `is_a(animator, find_class(L"/Script/Aloft.STSkeletalAnimatorComponent"))` before any
  field read (was a strstr on the class name). Refusal logs `WARN card486: client corpse arrival ... not a
  STSkeletalAnimatorComponent ... swap back skipped`, with " (class not found)" when the class lookup fails.
- Static: npc+0xA30 is ASTNonPlayableCharacterBase::AnimatorComponent (TScriptInterface, PropertyClass
  ADAnimatorBaseComponent). The only caller is c132_pose_arrival from c38_death_edge, gated by is_a(g_npc_class()),
  so every pawn c37_scan hands to c269 is an NPC that has the field.
- c448b214a: 0 EXCEPTION/FATAL, 0 mismatches. c244b214a (1947 s): 0 EXCEPTION/FATAL, no dumps, 0 card486 rows (no
  refusal, no "class not found").
- c244b214b (2364 s): 0 EXCEPTION/FATAL, no dumps, 0 card486 rows.

## Closed (2026-11-18 07:46)
Fixed in b214 (56b7f47d0bbc4783). c269_swap_back now requires is_a(animator, STSkeletalAnimatorComponent) before any field read. A refusal logs "WARN card486 ... swap back skipped". Static: npc+0xA30 is ASTNonPlayableCharacterBase::AnimatorComponent. The only path in is c132_pose_arrival from c38_death_edge, which is gated by is_a(g_npc_class()). Runs c244b214a and c244b214b: 0 card486 rows, 0 crashes, 0 dumps.
Not done: none.
Reopen when: a client crash in c269_swap_back or c132_pose_arrival, or any card486 refusal row in a run.
