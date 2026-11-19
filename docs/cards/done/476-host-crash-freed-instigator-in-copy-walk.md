# 476 Host crash when a departed client's grenade explodes (freed instigator in the copy walk)

Play blocker. Found 2026-11-17 in run c423b177b (b177 `e7197c28f8ab7bb8`, FED_C2M1), host log row
`CRASH 1/4: EXCEPTION_ACCESS_VIOLATION (0xC0000005) at exe+0x14CBD9E`, reading 0xFFFFFFFFFFFFFFFF.

## Trace

- Stack: `FObjectPtr::IsA` (exe+0x14CBD9E) < mod `is_a` (ubwcoop+0x148C48) < `c99_copy_of` inlined in `c102_enter`
  (ubwcoop+0x12EEBB) < `detour_c74_damage` (grenades.cpp:2044, ubwcoop+0xC5B41) < `AADProjectile::OnHit` <
  `detour_c132_on_hit`.
- Sequence on the host: t=567290910 the client throws a frag (card 446 step); t=567291309 the client disconnects, its
  PC, pawn, weapons and PlayerState are destroyed (card113 players 2 -> 1); t=567296344, at the fuse, the frag's
  damage call hands the mod an instigator that is the destroyed pawn. By then the memory was reused: the class pointer
  read from it was `0x3FE5310F40000000` (a double), and `IsA` faulted on it.
- `c99_copy_of` (player_c99.cpp:126) walks raw pointers (the entry object, Instigator, Owner, Outer) with no liveness
  check. Runs c423b170a..177a went through the same step without a crash: the fault needs the pawn's memory to be
  freed and reused before the fuse.
- Playtest reach: any client leave or connection loss with a live grenade, rocket or other delayed-damage projectile
  of that client crashes the host with some probability.

## Fix

1. One helper, `object_listed(const void* o)`: true only when the object array entry at the object's InternalIndex
   (int32 at +0xC) points back at `o` and is not flagged unreachable or garbage. The index read from a stale pointer
   must be range-checked against the array's count before the lookup. Reuse the lookup `c31_weak_get` resolves
   through; no second copy of the array walk.
2. `c99_copy_of`: check the entry object and every hop (`o`, `instigator`, `owner`, outer) with the helper before
   `is_a` / `sprint_side`; a hop that is not listed ends the walk with nullptr and one capped row
   `WARN card476: host t=.. copy walk met a freed object at hop N, caller exe+0x..`.
3. Sibling sweep, same build: every mod detour that takes an instigator, causer or owner pointer from a projectile or
   damage path and reads through it (`c76_damage`, `detour_c132_on_hit`, card 140 hit rows, card 074 explode relay
   `skip_pc`), and every table that keeps a raw pawn or PC pointer of a client across frames without a weak ref.
   List each site in the report with "checked, weak" or "fixed".

## Proof

Run c423 through the card 440 leave with the frag in the air. Prediction: no EXCEPTION row; either the
`WARN card476` row at the fuse (pawn freed) or a normal damage row (pawn not yet freed); rejoin block and map end
pass; digest mismatches 0. If the driver can force a garbage collection on the host, do it between the disconnect
and the fuse so the freed case is the one proved.

## Closed

Closed 2026-11-17 on b178 `f6596be3fcb2d778`. The first b178 (`28ee0cc62db2ff6b`, mod walk guarded only) still
crashed in c423b178a: the game's own `ApplyDamageFromImpactHitResult` (exe+0x522DC72) reads the instigator's vtable
unchecked. Added `c476_live_instigator` (grenades.cpp): a freed instigator is replaced by the listed causer before
the game call. Runs c423b178b and c423b178c pass end to end with 0 EXCEPTION, 5 `WARN card476 ... its instigator is
freed, the causer stands in` rows (AI Morita rounds whose shooter died: the crash class existed apart from the client
leave); gate c448b178a passes. Sibling sweep in the junior report: `c160_actor_of` and the NetPcRel channel read
fixed, the rest compare-only or weak.
Open refinement (next build): replace only when the object array entry no longer points at the instigator (freed),
not when it is merely flagged garbage (destroyed, memory still valid), so damage attribution of a destroyed but
unfreed shooter stays the game's own.
Playtest watch: `WARN card476` rows are expected noise; an EXCEPTION at exe+0x14CBD9E or exe+0x50D22A1 reopens.
