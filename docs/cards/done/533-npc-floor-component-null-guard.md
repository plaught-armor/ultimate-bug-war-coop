# Card 533: guard the NPC anim update against a destroyed floor component

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: fix in b340 (also in b341), proof pending. Card 392 family.

## Bug

c263L342 client crashed in world at the run end: EXCEPTION_ACCESS_VIOLATION reading 0x0 at exe+0x510B401
(dump UECC-Windows-6DB9A42141B3202F1939278F27C6F150_0000, logs/c263L342-client.log line 88984).

## Cause (static trace)

- `ASTNonPlayableCharacterBase::Tick`+0x4A4 calls `FSTBugProceduralAnimationData::UpdateFromNPC` (RVA 0x510ADB0).
- r15 = NPC vfn +0x748 (`GetMovementComponent`), the CMC.
- At +0x621 the function tests CMC+0x4F5 bit 0 (`CurrentFloor.HitResult.bBlockingHit`; CurrentFloor 0x438, HitResult
  +0x10, flag byte +0xAD). When set, it calls `FWeakObjectPtr::Get` on CMC+0x520 (`HitResult.Component`) and calls
  vfn +0x5D8 (`GetCollisionObjectType`) on the result with no null check. The AV is at +0x651.
- The floor component belongs to an actor the client destroyed (a bug standing on another bug that was net-culled or
  killed by `clear`). The weak ref dies; the call goes through null.
- The function's second +0x520 read (exe+0x510B658) only compares pointers. It is safe.
- ubwcoop+0x5581 in the stack is the card 527 timing wrapper. It is diag-gated and not the cause.

## Fix

Always-on detour of `UpdateFromNPC` (hooks_ubw.cpp, `detour_c533_update_from_npc`), both sides. If bBlockingHit is set
and the weak component no longer resolves, clear bBlockingHit, then call the original. The floor is stale anyway; the
CMC rewrites CurrentFloor at its next floor find. Log row `card533:` at counts 1, 2, 4, 8, ...

## Proof

- Any run that kills bugs near other bugs (`clear <radius> kill`) on the client: `card533:` rows appear, no crash.
- Zero `card533:` rows is not a failure by itself; the crash is rare. The check is no exe+0x510B401 AV across the
  next soaks.

## Closed 2026-11-21 (junior, backlog rule)

The guard fired with no crash in soak2m1b366b, soak3m1b366a and soak4m1b366a (client `card533:` rows). No
exe+0x510B401 AV in any log since b340.
