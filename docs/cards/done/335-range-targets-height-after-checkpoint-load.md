# Card 335: range targets stand on the host and lie down on the client after a checkpoint load

**Status:** open, queued after the playtest (cosmetic: finished-objective props differ in height after a checkpoint
load; no objective, damage or path depends on them). Found by the digest gate, batch 3 run 9 (`6ce017264c0f3c81`,
`logs/c310-proof-c1m1-*.log`). Whether play build 4 has it is not determined; the path is card 098/140 code, not
batch 3 code.

**Rows (C_3, WorldOp 930314c6; the 9 TargetRangeTarget actors and the GrenadeRangeTarget behave the same):**
- 312407098 host save runs after ShootingRange_SO3 10/10 (all targets killed). 312407666 host `save engine load runs`
  (in place, `destroy actors 1`); the client does a `LoadMap` (312410757 to 312412334).
- 312408415 host `Health 100.0 -> -410.0: kill relayed` (twice): this is the load restoring the saved dead Health,
  not a new kill. The host target is dead after the load.
- 312409691 host sends `K 0` (1625, 1626) from the queued frames; 312414551 the journal replay to the reloaded client
  sends `L|MovementTimeline`, `A|6010,17850,-336`, `K 0` (339, 341, 343).
- 312414576 client `world K ... Health base 0, Destroyed set and SaveFullyLoaded run (no OnDeath), IsDead now 1`.
- Digest rounds 7 to 16, 10 mismatches each, location only: host (6000,17850,100), client (6000,17850,-300); C_1 host
  z 50, client -300; C_5 host 100, client -250. Host round row: `at (6010, 17850, 80) ... hidden 0`.

**Reading:** both sides hold the target dead; only the height differs. The host's in-place load leaves the dead target
at its raised height (80 to 100); the client's 'K' apply runs the SaveFullyLoaded path on a fresh map copy and ends
lowered (-300). Before the load both sides were lowered (digest clean). The junior's reading (stale kills replayed
onto restored targets) does not hold: the host Health row shows the host targets dead after the load.

**Not determined (trace before any fix):** why the host target is raised after the in-place load (saved transform,
timeline reset by the load, or the host's own SaveFullyLoaded not moving it); whether a single-player checkpoint load
shows the same raised dead targets (then the host is right and the client apply is wrong); whether the host sends an
'A' row for the new height after the load (the last host 'A' row for C_3 is 312362248, before the kill).

**Fix (class, after the trace):** after a host checkpoint load, every journalled placed actor's height is re-sent
('A' row from the host's real position after the load, applied after 'K'), so the client ends where the host is,
whichever height the game picks. Sibling sweep: all KillableBuilding classes with a MovementTimeline
(TacSuppRangeTarget, GrenadeRangeTarget), and every journal kind whose client apply runs a load-path event.

**Proof:** digest rounds after `travel checkpoint` show 0 mismatches on the range classes.

## 2026-11-14 12:19, senior: static read of run 11 (`logs/c310-proof-c1m1-*.log`, same bug, C_3)

- The in-place load destroys only actors with `placed 0` (host rows after 313442561). The range targets persist, so
  their card 140 placed entries (anchor, sent, prev) persist too. No table reset is involved.
- C_3: Activate at 313394100, kill at 313397618, 3.52 s into the 6.00 s MovementTimeline. Host round rows after the
  load: actor `at (6010, 17850, -6)`, `timeline MovementTimeline position 3.533 of 6.00, playing 0`. The actor root is
  where the kill stopped it, before and after the load; the last host 'A' row (313397671) was sent from there, which
  is why no 'A' row follows the load.
- The digest mismatch is not the actor root: `location host (6000,17850,0) client (6000,17850,-200)`, and the client
  value changes between rounds (-200 in rounds 9 to 12, -150 in rounds 13 and 14) while the client root reads
  `at (6010, 17850, -77)`, timeline position 1.x. So the client copy is still moving after the load and the host's is
  not. The first reading in this card (host raised, client lowered, both at rest) is wrong on the second half.
- The load re-runs the kill on the host (`Health 100.0 -> -260.0: kill relayed`, twice, 313443335) and sends `K 0`
  twice (313444795).

Open before any fix: which component the digest location reads for this class, and what moves it on the client after
the 'K' apply (the client's SaveFullyLoaded path, or the second `K 0`, or the replayed `L|MovementTimeline` rows
playing the timeline from 0 on a fresh map copy while the host's sits at 3.533, not playing). The class fix in this
card ('A' row re-sent after the load) does not cover a timeline that plays on the client only; the journal's 'L' row
for a timeline the host holds stopped needs its position and playing flag, not only the play call.

## Trace (2026-11-14, `logs/c310-proof-c1m1-{host,client}.log`, run 11)

Actor `BP_KillableBuilding_Base_TargetRangeTarger_C_3` (the game's spelling). After the host's checkpoint load the
client replays, in this order:

1. `card311: client t=313450175 replayed Activate(1) ...ADLevelTriggerReactions: activated`. This starts
   `MovementTimeline` on the fresh map copy from 0.
2. The card 185 'L' state row for the host's timeline: position 3.533 of 6.00, playing 0.
3. 'K' at 313450374 (`SaveFullyLoaded run (no OnDeath)`).

Client round 9 row: `timeline MovementTimeline position 1.151 of 6.00, playing 1`, root z -77. Host: position
3.533, playing 0, z -6.

Cause: `c185_client_state` (`world_c98.cpp`) applied a host state with playing 0 as `SetPlaybackPosition` only. A
copy that is already playing keeps playing from the set position.

Not confirmed from logs: the order of rows 1 and 2 (the `kC185Lines` cap of 24 is spent on both sides, so the
state row for this timeline is not printed), and whether `SaveFullyLoaded` plays the timeline itself. If the fix
row below shows `playing 1` again, one of those two is the cause and the next step is to raise the cap for this
run and read the order.

## Fix (in the source, rides lane 2 build b21)

`c185_client_state`: when the host's state is not playing (stopped, or the by-op end), call the original
`UTimelineComponent::Stop` on the copy before the position set. One hunk, client only.

Attacked: by-op `Stop` (stop, no position set: the same as before plus a stop on a stopped copy); by-op
`Play` end (stop, then position at the length: the copy rests at the end as the host's does); a playing host state
(unchanged path).

## Prediction (short run: C1M1, range, `travel checkpoint`, one digest round)

- Client round row for `TargetRangeTarger_C_3`: `MovementTimeline position 3.533 of 6.00, playing 0` or the host's
  value in that run, equal on both sides.
- 0 location mismatches on the range target class after the checkpoint load (run 11: z -77 against -6).

## Proof runs so far (2026-11-14)

Two runs of `c335-range-load.txt` were void before `travel checkpoint`, neither from this card:

- b21: the card 336 landing check refused points inside a disabled fall zone. Fixed in b22 (`+0x310` gate).
- b22: line 167 (`client look actor BP_PanelSwitch_WithoutScreen_C_0 0 bounds focus=...`) gave focus none. The aim
  scan's one traced hit was the bounds centre (17309 12686 215); the interactor focuses the switch at eye height
  (17309 12746 295 in the good run `c310-proof-c1m1`). Fixed for b23 in `testrun.cpp` `b14_step`: when the traced
  choice misses, the view steps through all 26 points and stops at the first focus (`by focus sweep`).

The card's prediction is unchanged and still unproven.

- b23: line 260, the goto ended mid-jump and the look had 0 of 26 hits. Fixed in b24 (landing wait).
- b24: line 191, the goto ended with the pawn sliding at about 420 uu/s. Fixed in b25 (standing wait).

## Result (run of `c335-range-load.txt` on b25 `c967354ff1db0488`), accepted 2026-11-14

- Scenario passed, 342 s. Host load done at 328687454; client rejoined.
- Client `world L ...TargetRangeTarger_C_1|MovementTimeline ...: applied timeline state: position 3.05 + 0 ms = 3.05
  of 6.00, playing 0`, the host's replay row value (`position 3.05 of 6.00, playing 0`).
- Digest rounds 7 to 10 (all after the load): 1505 actors on both sides, mismatches 0 (run 11: 10 per round). 0
  card237 rows in either log (they print only on a mismatch).
- Leftover, not this card: rounds 7 to 10 read `slots differing 1 (entries asked 12 host, 12 client)` with 0
  mismatches; rounds 1 to 6 read 0 slots differing. Carded as 341.

