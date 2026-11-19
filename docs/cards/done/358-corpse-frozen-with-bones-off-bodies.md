# 358: Client corpse frozen with bone 0 off body 0 (WARN card191)

Found in run `b42` (b42 `8965a327d495d442`, FED_C2M1). `WARN card191` is in no older client log.

## What the log shows

- guid 0x322: `frozen with bone 0 1.9 uu off body 0 after 10 wait tick(s), simulating 1` three times (342844243,
  342849327, 342860363). The corpse is frozen three times, so it was unfrozen in between (host grenade explosions at
  342844323 and after move settled corpses). 1.9 uu is small; the question is the tolerance and why 10 end ticks do
  not bring the bone onto the body.
- guid 0x424: `328.8 uu off` at 342877457. This bug died across the checkpoint reload: alive at 342874756 (health
  400), dead at 342874905 (health -1257.0), `corpse arrival held (mesh already hidden): no host pose row yet`,
  `held the game's ragdoll start (no stream row yet)` at 342876705, resolved after 174 ms. A corpse drawn 3.3 m from
  its bodies is a visible mismatch with the host.

## Trace first (static, then the b42 rows)

1. 0x424: order of ragdoll start, first host pose row, apply and freeze after the reload. Which step leaves the bones
   where the animation left them (the end-of-physics blend copies a body only while it simulates; see the comment at
   the freeze wait in `pose_c132.cpp`).
2. 0x322: what `kC191BoneTolerance` is, and whether 1.9 uu is a real offset or a steady blend residue.
3. Fix by class: every freeze path leaves bone 0 on body 0, or the freeze does not happen.

## Trace result (junior, b42 client rows plus pose_c132.cpp, before b43)

guid 0x424, settled cause in one place:

1. 342876705: the game's ragdoll path (caller exe+0x37C53CB, `BP_AI_AttackSystem` "ragdoll left") calls
   `SetSimulatePhysics(true)`; card 152 `pose_wait_gate` holds it ("held the game's ragdoll start on guid 0x424 (no
   stream row yet)").
2. The same Blueprint sequence then calls `SetAllBodiesSimulatePhysics(1)` directly. That call is not held: `card140:
   client pose own SetAllBodiesSimulatePhysics(1) on BP_AI_WarriorBug_C guid 0x0 (row none)`, one per held guid (5).
   The bodies simulate, but `bBlendPhysics` stays 0: only UE's `SetSimulatePhysics` sets `bBlendPhysics = bSimulate`.
3. 342876879, snap 2 apply: the copy is already simulating, so `pose_start_ragdoll` returns `started = false` and
   the stock `SetSimulatePhysics` never runs. The end-of-physics blend does not copy the bodies into the bones.
4. The freeze waits its 10 ticks and logs `WARN card191 ... 328.8 uu off`; the card 140 freeze check reads "blend
   physics was 0".
5. Snap 3 (342881263) found the copy not simulating after the freeze, went through `SetSimulatePhysics`, "blend
   physics was 1", bone 0 0.0 uu off after 1 end tick, no WARN.

guid 0x322, not settled: `kC191BoneTolerance` is 1.0 uu, `kC191FreezeWaits` 10. Its freezes at snap 4 and snap 5 have
"blend physics was 1" and simulating 1, and bone 0 stays 1.9 uu off body 0 through all 10 waits, unchanged after.
While streaming its bone 0 was 0.0 uu from body 0 and body 0 was 0.6 to 1.9 uu from the last target set. The apply
moves the bodies by that drift (up to 1.9 uu) and the end ticks show `awake 0`: a likely reading is that bodies posed
asleep do not refresh the bones, so the bones keep the last streamed pose. 1.9 uu (about 2 cm) is not visible; no fix
in b43, the rows are reported as they are.

## Fix (b43)

`pose_start_ragdoll` (pose_c132.cpp) treats a copy that simulates with `bBlendPhysics` off as not started and calls
the card 039/132 start (`detour_c132_set_simulate`, the stock `SetSimulatePhysics`), the same path snap 3 took.

## Prediction

Written by the junior after the trace, before any run. A copy whose game ragdoll start was held and then simulated by
the game's own `SetAllBodiesSimulatePhysics(1)` shows `(ragdoll started for it)` on its first pose apply, its freeze
check reads "blend physics was 1", and it has no `WARN card191` with a large offset. The 0x322 kind of 1.9 uu residue
may still show. Gate: 0 `WARN card191` on the same scenario, corpse shot on the
client after a checkpoint reload matches the host's.
