# Card 191: A frozen final leaves the bones on the bodies when the game's ragdoll start was blocked

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: queued 2026-11-11, split from card 186 (one goal per card). Budget: three runs. Static first.

Read: `docs/cards/done/186-stream-final-rows-exact.md` (r2 outcome and senior review), card 146 (the block of the
game's ragdoll start on a host-posed guid), `mod/src/ubw/pose_c132.cpp` (`pose_try_apply`, `pose_freeze_step`,
`pose_bone_again`).

## Why

After card 186 every streamed final ends exact except one case, seen in three runs on a corpse that was re-woken
several times and on which card 146 blocked the game's own ragdoll start:

| Run | Guid | Final | Bone 0 from body 0 | Unchanged one tick later | card146 blocked |
|---|---|---|---|---|---|
| c152-r3 | 0x102 | frozen | 413.7 uu | yes | yes |
| c186-r1 | 0xf8 | frozen | 428.5 uu | yes | yes |
| c186-r2 | 0xf8 | frozen, snap 97 | 316.2 uu | yes | 1 |

In c186-r2 the same guid's earlier frozen finals read 1.3 uu (snap 87) and 0.2 uu (snap 94), and guid 0x10c (no
block) read 0.0 to 0.1 uu on both of its frozen finals. The diagnostic line says the copy simulated when the row
reached the apply and at the freeze step, so "not simulating" is ruled out. Body 0 is 0.0 uu from its target: the
bodies are right, the drawn bones are not. The player sees the corpse somewhere else than the host does.

What separates the off case in c186-r2 (junior, from the client log): it is the copy re-created after the rejoin.
Static mesh "host alive" at 99492750, arrival ragdoll start at 99494132 with the mesh at the bug's live death spot,
stream start at sample 5 at 99494784, card 146 block at 99494906, frozen final snap 97 at 99495779 (873 ms after
the block). The clean finals on that guid came 40 to 55 s after its block. Start the trace there: a fresh copy
whose game ragdoll start was blocked shortly before the frozen final.

## Do

1. Trace, no run: what decides whether the bones follow the bodies on this mesh after the freeze
   (`ShouldBlendPhysicsBones` reads 1 on the bad line too, so name the next condition: the per-body
   `bSimulatePhysics` / `PhysicsBlendWeight`, `bBlendPhysics`, the anim instance still ticking a death montage, or
   the end-physics tick not registered when the freeze runs). Compare the state card 146's block leaves behind with
   the state the game's own start would have set. Mark each line verified or open.
2. Fix at the layer that owns it (the card 146 block or the freeze step), for every frozen final, not this guid.
3. One run: the c186-r2 scenario shape (re-wake the same corpse at least three times with a card 146 block).

## Done when

- The trace names the condition; the run shows every frozen-final bone check at 1.0 uu or less, including a guid
  with "card146 game ragdoll starts blocked" above 0, and no "client bone check" WARN. The Report ends with
  "Report complete."

## Step 1 answers so far (senior, 2026-11-13: card 243 run 10 client log plus disassembly)

Verified:

- The block is not the separator. Run 10 has 4 frozen finals and all 4 fail the bone check: guid 0xd4 snap 2,
  188.2 uu (1 block); 0xfc snap 3, 1.1 uu and snap 6, 14.1 uu (0 blocks); 0xf2 snap 25, 4.7 uu (1 block). The
  title's premise is too narrow: the fault is in the frozen-final path for every guid.
- The bones are not stale, they are moved at the freeze. Guid 0xd4: 8 stream samples up to 225454021 read bone 0
  at 0.0 uu from body 0 (last at (-62684, 33882, 735), 166.2 uu from the mesh component). The frozen final at
  225458079 reads bone 0 at (-62831, 33975, 663), 323.0 uu from the mesh component, with body 0 exact (0.0 uu
  from target). The same position one tick later. So something wrote a new, constant bone pose after
  `pose_freeze_step`; the bodies never moved.
- Engine conditions (image base 0x140000000):
  - `ShouldBlendPhysicsBones` 0x4014F40: bodies > 0, collision enabled 2 or 3, then bit 0x20 at mesh+0xA71
    (`bBlendPhysics`), else any body with weight. The hold sets that bit, and the row logs 1.
  - `UpdateEndPhysicsTickRegisteredState` 0x3AFD900: registered when (simulating and awake) or
    `ShouldBlendPhysicsBones`. Logged 1 on 0xd4, 0 on 0xfc snap 3: so on 0xfc the end tick was not registered
    one tick after the freeze although the hold was on. Nothing calls the update after `pose_hold` sets the bit.
  - The end-physics tick (`ExecuteTick` 0x41874F0) is the only caller of `BlendInPhysicsInternal` 0x3FEA4F0 on
    this path; the blend itself runs in `PerformBlendPhysicsBones` 0x400C630 under the physics read lock and
    returns early when the required-bones or bone-space arrays are empty.
  - `ShouldTickPose` 0x3AF85C0 returns false for `VisibilityBasedAnimTickOption` (+0x7BC) above 1 on a mesh not
    recently rendered, and for bits 0x08 and 0x10 at +0xA80.

Open, with the row that decides it (no extra run; batched into the next FED_C3M2 run):

- Which writer moved the bones. Candidate A: the anim evaluation wrote its own pose and no blend followed
  (end tick not registered, or the blend returned early). Candidate B: the blend ran but read body transforms
  the freeze call had not yet committed to the game thread.
- Diagnostic to add to the `frozen final` and `one tick later` bone lines (`pose_bone_line`, `pose_c132.cpp`):
  the mesh component's world location, bone 0 in component space, the byte at +0x7BC, the bits 0x08 and 0x10 at
  +0xA80, the required-bones count, and the same bone-0 component-space value captured at the stream start.
- Prediction A: bone 0 in component space equals its value from before the ragdoll (the anim pose) and the end
  tick reads 0 on at least one tick. Prediction B: bone 0 in component space equals body 0's position from the
  tick before the freeze, end tick 1 on both ticks.

Fix direction once decided (every frozen final, one place, `pose_freeze_step`): A: call
`UpdateEndPhysicsTickRegisteredState` after `pose_hold` and stop the anim writer for a held copy; B: freeze one
tick later than now, after a blend with the final bodies. No code before the row is read.

Note (run 10 pose rows): the corpse mesh is a `USkeletalMeshComponentBudgeted`. The animation budget allocator
decides its tick, so a throttled corpse may get no pose tick and no end-physics tick in the tick after the
freeze. Not traced further; the diagnostic row above decides it.

Correction (junior, 2026-11-13): `PerformBlendPhysicsBones` (0x400C630, called from 0x143FEAB56) takes rdx =
RequiredBones (+0xB60), r8 = component space (+0x5F0 + 16*[+0x634]), r9 = bone space (+0x928). Its early-out tests
the component-space and bone-space counts, not RequiredBones. The build 2 bone check row logs the required bones
count, VisibilityBasedAnimTickOption (+0x7BC) and the +0xA80 bits 0x08 and 0x10.

## Run 3 of `c338-c337` (b19, 2026-11-14): the diagnostic row, read

Guid 0x238 (the card 334 Warrior), 15 `WARN card152: client bone check` rows, all `simulating 0`, body error 0.0 uu
in every `card140` freeze check row. Since card 334 a settled final is held like a frozen one, so it has the fault too.

| Client t | Final | Bone 0 to body 0 | Bone 0 in component space |
|---|---|---|---|
| 324736503 | settled, snap 2 (first final, before any card 339 repeat) | 27.5 uu | (0.0, -0.0, 44.7) |
| 324736652 / 669 | frozen, snap 3, and one tick later | 27.5 uu | same |
| 324739680 to 324742228 | settled | 4.4 uu | (-0.0, -0.0, 44.7) |
| 324745317 | settled | 103.2 uu | (0.7, -0.1, 172.8) |
| 324746155 | settled | 440.1 uu | (57.5, -329.3, 235.2) |
| 324747627 to 324768412 | settled | 1.5 to 2.6 uu | (101.9, -473.6, 47.2) |

Bone 0 at the stream start: (0.0, -0.0, 74.4). End tick registered 1 and ShouldBlendPhysicsBones 1 on every row;
VisibilityBasedAnimTickOption 1 (3 on the 324739680 row); bits 0x08 and 0x10 clear.

Reading against the two predictions:
- B (the blend read bodies from the tick before) is ruled out for snap 2: the apply moved the bodies 0 uu ("0 uu
  from the client's before"), so a one-tick-old read would give 0 uu, not 27.5.
- A in part: at snaps 2, 3 and 5 bone 0 sits on the component's z axis (x and y 0.0), which a ragdoll pelvis does
  not; that is an animation pose. But the end tick reads registered on those rows, so "no end tick" is not the
  reason the blend did not follow.
- The mesh component itself moves between finals ((141642, 12214, 10715), (141704, 12189, 10717),
  (141631, 12200, 10654), (141250, 12296, 10544)), so part of the distance is the component, not the bone.

Open: what writes the animation pose on a held copy after the freeze call, and whether the blend runs on a copy
whose bodies the apply put to sleep (`pose_apply_bodies` ends in the bodies' sleep). Next static step: the awake
test on the path from `ExecuteTick` 0x41874F0 to `BlendInPhysicsInternal` 0x3FEA4F0. After the b21 run (card 339
removes the `1 -> 1` repeats) the snap 2 row is expected to remain.

## Static step done (2026-11-14, b19 exe): no awake test before the blend

`ExecuteTick` 0x41874F0, in order:
- returns when the component is null, pending kill, or bit 0 at +0x94 is clear;
- when `IsSimulatingPhysics` (vtable +0x4E0) and `RigidBodyIsAwake` (0x40A6010) are both true:
  `SyncComponentToRBPhysics` (0x40A9910). This is the only awake test, and it gates the component move only. It
  explains why the mesh component moves between finals only while the copy simulates;
- when `ShouldBlendPhysicsBones` (vtable +0xC80) is true: `BlendInPhysicsInternal` 0x3FEA4F0. No awake or
  simulating test.

So on a held copy (ShouldBlendPhysicsBones 1, end tick registered 1) the blend is called every tick, asleep or
not, and the "bodies asleep" idea is ruled out. Bone 0 still keeps a constant component-space value
((0.0, -0.0, 44.7) over snaps 2, 3, 5, 7) while the bodies sit elsewhere, so either the blend's result is replaced
after it, or the blend does not write bone 0 from a body that is not simulating.

Diagnostic for the next lane 2 build (log only, client, capped at 8 rows per map): detour `ExecuteTick` 0x41874F0;
for a held copy's mesh, on the first two ticks after `pose_freeze_step`, log bone 0 in component space and its
distance to body 0 just before and just after the original returns:
`card191: client t=<ms> guid 0x.. end tick <n> after the freeze: bone 0 before (x, y, z) <d> uu, after (x, y, z) <d> uu`.
- After at 1.0 uu or less and the bone check row still off: something writes the bones after the blend; the next
  step is the writer (the animation evaluation's finalize).
- After off: the blend skips the body. The fix is in what `pose_hold` sets (per-body blend weight or
  `bBlendPhysics` read at 0x4014F40 against what `PerformBlendPhysicsBones` 0x400C630 tests per body).

## b22 run of c338-c337 (`0c40725031a4aede`): the frozen path has it too, and far worse on a re-arrival

The diagnostic printed nothing: it armed only on a held settled final, and this run's corpse (guid 0x21a) froze
(`snap 2 FROZEN after 5965 ms`) without a settled final. Six card152 WARN rows on the frozen final instead:

| Client t | Apply | Bone 0 at | From body 0 | Notes |
|---|---|---|---|---|
| 326646881 | first copy, stream final | (141423, 12128, 10739) | 1.6 uu | end tick registered 0, blend physics was 0 |
| 326688975 | re-arrival, "(ragdoll started for it)" | (141631, 12200, 10835) | 238.8 uu | end tick registered 1, blend physics was 1 |
| 326691330 | after `card146` blocked the game's own ragdoll start, "held again" | (141183, 12221, 10905) | 306.0 uu | same |

Bodies are exact in every row (largest body error 0.0 uu, host root (141424, 12130, 10740)). On the re-arrival the
pelvis bone sits at the capsule (capsule (141631, 12200, 10837), mesh rel (0, 0, -174)): the skeleton draws at its
animation pose on the capsule while the bodies lie 239 uu away at the host's place. So a player who walks away
and back sees the corpse in another place than the host. This is the visible fault; the 27.5 uu case of run 3 is
the small form of it.

## Diagnostic, changed for b23

Arm on every final apply on the client, frozen or settled, first copy or arrival (not only `r.held`). For the
copy's mesh, on the first two `ExecuteTick` (0x41874F0) entries after `pose_freeze_step`:
`card191: client t=<ms> guid 0x.. end tick <n> after the freeze: registered bit <+0x94 bit 0>, simulating <0|1>,
awake <0|1>, ShouldBlend <0|1>; bone 0 before (x, y, z) <d> uu from body 0, after (x, y, z) <d> uu`.
If the bone check (one tick later) runs and no `ExecuteTick` entry was seen for the mesh since the freeze, one row
`card191: client t=<ms> guid 0x.. no end tick since the freeze`. Cap 12 rows per map.

Reading:
- No end tick: nothing copies bodies to bones. Fix candidate: call the blend once at the freeze (the engine entry
  `BlendInPhysicsInternal` 0x3FEA4F0 with the mesh's end tick function), or keep the end tick registered.
- End tick runs, after is still off: the blend skips the bodies (`PerformBlendPhysicsBones` 0x400C630 per-body
  test, not simulating with weight 0). Fix candidate: physics blend weight 1 on every body before the freeze.
- After is within 1.0 uu but the check is off: a later writer in the same frame (the animation refresh). Name it
  from the row of the third apply, where the bone moved between two checks.

## b23 result (`0725fd1fbbe12914`, run of `c338-c337-c1m1`, client log)

b22's 0 rows and the first b23's "no end tick" rows were false: the `ExecuteTick` detour sat behind
`g_gate_c62_diag`, which test runs do not set. Gate is now `g_gate_c191` (any test run or DIAG).

| Guid | Case | End tick 1, bone 0 off body 0 | End tick 2 |
|---|---|---|---|
| 0x20a | first copy | 0.0 uu | 451.7 uu |
| 0x20a | arrival, 0 end ticks between apply and freeze (395.3 uu after the pose) | 395.3 | 395.4 |
| 0x15e | first copy | 0.0 | 273.9 |
| 0x20a | arrival, 1 end tick between apply and freeze | 0.0 | 504.1 |

Every row: registered 1, simulating 0, awake 0, ShouldBlend 1, before equals after.

Cause, two parts:
- The end-of-physics blend copies a body into its bone only while the body simulates. An arrival frozen before any
  end tick ran on its posed bodies keeps the bones of its old pose (395.3 uu).
- After the freeze the animation refresh writes its own pose into the bones each tick (0.0 to 451.7 uu between end
  tick 1 and 2), and the blend never puts the bodies back.

Static note: `BlendInPhysicsInternal` hands the blend to a task when the parallel-blend cvar is on, so "after" read
at the `ExecuteTick` return may be before the task ran; the end tick 2 "before" values are the proof, not the
before-equals-after columns.

## Fix (b24, client, `pose_c132.cpp`)

- `pose_freeze_step` waits, simulating, until bone 0 is within 1.0 uu of body 0 (up to 10 ticks; WARN at the cap).
- After the hold it sets `bNoSkeletonUpdate` when it was off (`noskel_set`); `pose_release` clears it.

Predictions (run of `c338-c337-c1m1` on b24): every `end tick 2` row within 1.0 uu; 0 `WARN card191`; fewer than 9
card152 bone check WARN rows on the client; card140 freeze check rows unchanged.

## Result (run of `c338-c337-c1m1` on b24 `5dfc485c94735310`), accepted 2026-11-14

- `card191: client bNoSkeletonUpdate at +0xA80 mask 0x08`; card152 rows read the bit 1 while frozen, 0 while live.
- One corpse (guid 0x21c; b23 had two). All 3 `end tick 2` rows: before 0.0, after 0.0 uu (b23: 451.7, 395.4, 273.9,
  504.1). The arrival was 93.0 uu off after its apply and 0.0 at the freeze, after 1 end tick.
- 0 `WARN card191`. Client card152 bone check WARN rows: 0 (b23: 9). card140 freeze check: 3 rows, all 0.0.
- Not exercised: the wait path (no freeze needed a wait). b25 adds a row for a completed wait
  (`freeze waited N tick(s)`) and `pose_applied_set` zeroes `freeze_waits`, so a new apply gets the whole budget.
- The sample is one corpse; the next playtest is the wider one.
