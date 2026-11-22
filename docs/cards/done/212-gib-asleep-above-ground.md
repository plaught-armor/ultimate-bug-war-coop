# Card 212: A gib part does not sleep above the ground on the client

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: queued, static first. New client WARN class in c208-r3 (7 lines):
`WARN card148: client gib host part N STGorePartNonSticky_StaticMesh asleep above the ground: nothing within N uu
below ...`. No earlier log has it. The run was on BUG_C1M1 with NPC Warrior bugs hit near the players.

Do: read the card 148 check and the 7 lines (part, position, the host's rest position for the same part), state
whether the part hangs in the air on the client or the ground probe misses on this map (cave floor, trace
channel). Fix the cause or the check. Proof rides in a run with gibs on BUG_C1M1.

## Senior trace (2026-11-12, static, from c208-r3 logs)

The check is wrong, not the pieces. All 7 client lines and all 6 host lines ("rests with nothing within 40 uu
below") print the bottom point `0 0 0`. `gib_no_ground` (`gib_c137.cpp:546`) seeds `centre` with the actor
location and then lets `g_actor_bounds(part, true, ...)` overwrite it; for `STGorePartNonSticky_StaticMesh` at
rest the colliding-only bounds come back empty (origin 0, extent 0), so the trace runs from the world origin
and finds nothing. Six of the seven parts fail the same way on the host (parts 1, 2, 23, 30, 31, 32); part 25
fails on the client only.

Fix (one function): when the colliding-only bounds are empty (extent all zero), keep the actor location and take
the bounds of all components (`only colliding` false); when those are empty too, trace from the actor location.
Count the fallback and log its first lines (capped) with the class name, on both sides. Open, to answer from the
class before code: why this class has no colliding component at rest (collision switched off when it sleeps, or
never on), and whether part 25 differs on the host. Prediction: zero "asleep above the ground" lines whose
bottom is `0 0 0`; any line left names a real position.

## Junior trace (2026-11-12, static: game binary with PDB, c208-r3 logs)

The senior trace of `gib_no_ground` is confirmed. `g_actor_bounds(part, true, &centre, &extent, false)`
(AActor::GetActorBounds, only colliding) writes origin 0 and extent 0 when no component of the part has collision
enabled. It overwrites the seeded actor location, so the trace runs from the world origin down to z -40 and hits
nothing. All 13 lines print the bottom `0 0 0`. The probe is wrong for these parts; the lines do not say whether the
parts hang in the air.

**Why the class has no colliding component at rest.** Collision is switched off, not missing from the start.
`ASTGorePartNonSticky_StaticMesh` does not override `SetPhysicsEnabled`, so `ASTGorePartBase::SetPhysicsEnabled`
(exe RVA 0x051377C0, vtable slot +0x778) runs. Read from the binary:

- It calls the mesh component (actor +0x2F0) through vtable +0x6D8 with the flag (SetSimulatePhysics).
- It then makes a tail call through +0x768 with `edx = flag ? 3 : 0` (SetCollisionEnabled: QueryAndPhysics or
  NoCollision).

So `SetPhysicsEnabled(false)` turns physics and collision off together. A scan of all 462 `*GorePart*` functions
finds three callers of slot +0x778:

1. `ASTGorePartBase::PoolingActivate` passes 1.
2. `ASTGorePartBase::PoolingDeactivate` passes 0. That is a pooled-away part, which is hidden and not checked.
3. `ASTGorePartBase::OnSignificanceChanged` (0x051328E0) passes the byte at +0x18 of the settings entry for the
   current significance level, and only when it differs from the cached byte at +0x6C. Significance is the part's
   `ISTSignificanceObjectInterface` level.

Parts at a low significance level therefore lose physics and collision. That is the empty colliding bounds.

Other paths do not change collision:

- `InitialiseCollisions` (0x05127F60) only sets the profile name and one flag (vtable +0x770, +0x760).
- `ASTGorePartNonSticky::FloorStatusChanged` (0x0511FD00) plays the floor sound and Niagara, places the floor decal
  and binds the surface lifetime. It never touches collision.
- `OnComponentHit` and `UpdateFloorDetection` do not either.

A significance drop also stops the simulation, so a part that drops while still falling stays where it is. After
the fix, any line that is left gives that real position.

**Can part 25 differ on the host? Yes.** Significance is evaluated in each process for its own viewer: the part is
an actor in each process, and the level comes from the local significance object. This is UE's design for
significance; the game's own manager was not traced further.

- The host checks its part when the part's R row is queued (`gib_host_ground`).
- The client checks its copy when the R row arrives (`gib_ground_check`), after teleporting it and calling
  `g_prim_sleep`.

Each side reads its own part's collision state. Host part 25 was at a level with physics on, so its bounds were not
empty. On the client, part 25 was at a level with physics off, so its bounds were empty. The c208-r3 counts agree:
the host's "19 still ... 6 with no ground below" and the client's "rest checked 19, above the ground 7".

## Plan (Phase B, after the senior's answer)

Fix, in `mod/src/ubw/gib_c137.cpp`:

- `gib_no_ground`: when the colliding-only bounds come back with extent all zero, keep the actor location and take
  `g_actor_bounds(part, false, ...)` (all components). If those are empty too, the probe starts at the actor
  location with bottom = actor location.
- One new helper, `gib_bounds_fallback_line`: counts the fallback per side and logs its first `kGibBoundsLines`
  (20, in `hooks_internal.h`) through `note_event_cap`.
- Nothing else changes.

Proof is in run c212-r1 on BUG_C1M1. NPC bugs are killed near both players so the parts come to rest.

### Predictions, run c212-r1

1. Both sides log fallback lines, `card212: (host|client) gib part \S+ STGorePartNonSticky_StaticMesh: no colliding
   bounds, ground probe from (all-component bounds|the actor location) at -?\d+ -?\d+ -?\d+ \(fallback \d+\)`. At
   least one appears on the host, since c208-r3 had 6 such parts out of 19.
2. Zero card148 lines on either side whose bottom is `0 0 0`: `grep -c 'below 0 0 0'` = 0 on both logs.
3. The card148 summary lines are still there on both sides: client `rest checked N, above the ground M`, host
   `N with no ground below`. M and the host count are each no more than in c208-r3 (7 and 6) for a similar part
   count. Any above-the-ground line that is left names a non-zero position (a part frozen by significance in the
   air). That is a report line, not a fix.
4. No new WARN class on either side.

## Run c212-r1 (2026-11-12, DLL 6d7f5193580ad2cd, BUG_C1M1)

Host rc 0, client rc 0, scenario passed (all 186 lines ok, no try wait timed out). Three WarriorBugs spawned and
killed between the players gave 12 parts (10 rest rows by the quit, 8 by the objectives dump).

1. Met, with a different class. Four `card212` lines on each side, the same four parts, all "from all-component
   bounds" with real positions, e.g. host `card212: host gib part STGorePartSticky_StaticMesh
   STGorePartSticky_StaticMesh: no colliding bounds, ground probe from all-component bounds at -15159 -803 2638
   (fallback 1)` and the client's line at the same point. The class is `STGorePartSticky_StaticMesh` (WarriorBug
   gore), not `STGorePartNonSticky_StaticMesh` (door debris, not produced in this run). No "the actor location"
   fallback.
2. Met: zero lines with `below 0 0 0` on both logs.
3. Met: host `card148: host gib rest rows (objectives command): 8 still, ... 0 with no ground below`; client
   `rest checked 8, above the ground 0`. No "asleep above the ground" line is left, so no per-line details.
4. Met: no new WARN class (host: attach only; client: attach and card152, which is also in earlier runs' client
   logs).

Unproven: the door debris class (`STGorePartNonSticky_StaticMesh`) path through the fallback; the same function
serves it, but no such part rested in this run.

## Senior review (2026-11-12): accepted

Diff against the pre-212 snapshot: `gib_c137.cpp`, `hooks_internal.h`, `input_diag.cpp`, the scenario, nothing else.
DLL `6d7f5193580ad2cd` rebuilt hash matches the deployed one. No function over 60 lines in the two files. Run
c212-r1: 0 failed lines, 0 mismatches in both rounds, predictions 1 to 4 read from the logs by the senior. The
client's card152 bone check WARN (206 uu, frozen final) is card 191's class, held behind card 215. Door debris
through the fallback goes to card 127's watch list.
