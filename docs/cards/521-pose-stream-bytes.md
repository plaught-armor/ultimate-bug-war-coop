# 521: Corpse pose stream bytes (efficiency)

Opened 2026-11-19 by senior from the c263e (b294) FED_C3M2 run, per the smaller-packets rule ("always look for ways
to make packets smaller").

## Finding (c263e host log, one client, 431 s)

- Chan Pose is the second-largest op: 568,526 B on the wire in 1743 frames (326 B per frame, about 1.3 KB/s), after
  Fx (1,145,863 B). Summed from the `chan bytes per row this window` rows.
- `card152: host pose stream map totals` (host line 87666): 1030 stream samples, 508,066 B of text.
  `card132: host pose journal map totals` (line 87665): 401 snaps (336 resnaps, 237 unsettled), 187,621 B of text.
  The S, T, R and D rows are the small rest.
- A sample is `P <guid> <snap> <part> <parts> <bodies> <flags> <base64>` (`pose_send_blob`, `pose_c132.cpp`). The
  blob is a 12 B float root plus 3 u16 head fields, then 14 B per body: 3 int16 positions (0.25 uu, relative to the
  root) and 4 int16 quaternion components. Example (line 84895): 22 bodies, 453 B of text.
- Pose is a packed text op (card 150). Base64 carries 6 bits per char, so its packed wire size already comes close to
  a blob frame's (about 1.07 B per raw byte either way). Moving the same bytes to a blob frame alone saves little. The
  waste is in the content: every sample re-sends every body whole, and the quaternion takes 4 components.

## Cut (measure after each step)

1. **Smallest three.** Each body's quaternion as its three smallest components in int16, plus a 2-bit index of the
   dropped one (packed 4 bodies to a byte, or in the top bits of a component). That makes 12.25 B per body instead
   of 14, about 12 percent of the stream. It needs no state, so the join replay and the resnaps take it unchanged.
2. **Sample deltas.** A stream sample whose row's previous sample went to this PC carries each body as zigzag varint
   deltas from that sample (positions in 0.25 uu, rotation in int16 steps). Between 250 ms samples a settling corpse
   moves a few uu, so most fields take 1 B. The sample number already chains samples on the client (`pose_stream_part`
   `chain`). The client applies a delta only on a chained row, and the host sends the next sample whole after a
   join, a resnap or a miss. That gives an estimated 5 to 8 B per body, against 14.
3. Only if step 2's rows show it: send the binary body bytes in a blob frame of a new op instead of base64 text.

## Ordering and state

- Chan delivers one PC's frames in send order (card 519 static trace: one pace FIFO per PC). A delta sample
  therefore always arrives after the sample it refers to on that PC.
- The host needs each PC's last sent sample per row. The row already keeps `last[kPoseBodies]` for the move test.
  Per-PC delta state for 3 PCs is 3 copies of the sample blob, or delta from the last sample sent to every PC, with
  the join replay sending whole.

## Predictions (the same C3M2 PowerPlant scenario, one run)

1. Step 1: chan Pose bytes per stream sample down about 12 percent (log the per-sample bytes in the `card152` totals).
2. Step 2: chan Pose under 300,000 B for a comparable fight time (from 568,526). Report B/s with the run length.
3. Client: 0 bad payload refusals, the card 140 rest check unchanged, and the `Pose:` digest at 0 mismatches.

## Done when

Predictions 2 and 3 hold in one lane 2 run; numbers quoted here.

## In source (step 1, senior, 2026-11-19, not yet in a build)

- `pose_c132.cpp`: `pose_wire_pack` / `pose_wire_unpack` / `pose_wire_bytes`. Only the wire form changes; the
  internal blob stays 14 B per body, so every reader (`pose_want_from`, `pose_quat_from`, `pose_gone`, the apply,
  `pose_stream_move`) is unchanged. Wire body: 3 int16 positions, then the three smallest quaternion components times
  32767·√2 with the largest made positive; a gone body sends INT16_MIN as its first component. The 2-bit dropped
  index goes after the bodies, 4 bodies per byte. 22 bodies: 288 B against 326.
- `pose_send_blob` packs (and refuses with a `card521: host pose send refused` row on a bad body count or blob size);
  `pose_stream_part` and `pose_snap_part` unpack and count a size mismatch as a bad payload.
- Host and client must run the same build: an old peer reads the new frames as bad payloads.
- Host-side round trip (g++, 20,000 random blobs, 1 to 64 bodies): 0 mismatches, worst rotation error 0.0061 degrees,
  gone bodies kept, a truncated payload refused. Object compiles clean. Backup of the old file:
  `build/pose_c132.cpp.pre521`.

## Step 1 result (c263g, b298 f7be229361719772, FED_C3M2, 344 s, senior 2026-11-19)

- Prediction 1 holds. Chan Pose per sample (stream samples plus snaps): 334,814 B over 720 + 235 = 955 samples,
  350.6 B, against c263e's 568,526 B over 1030 + 401 = 1431, 397.3 B: down 11.8 percent. Stream text per sample
  (`card152` totals): 306,347 B / 720 = 425.5 B, against 508,066 / 1030 = 493.3 B (down 13.7 percent). Chan bytes
  per row fell only from 299.5 (c263f) to 290.9, because a row carries a varying number of samples; per sample is
  the measure.
- Prediction 3 holds: client `card132: client pose journal map totals` 235 received, 284 applied, 0 bad, 145
  checks, 0 mismatches, largest body error 0.0 uu; `card140` 103 freezes, 0 over 2 uu; "bad payload" rows 0;
  no `card521` refusal rows on either side.
- Next: step 2 (sample deltas), design in the step 2 notes below once written.

## Step 2 design (senior, 2026-11-19, from a static trace of `pose_c132.cpp`)

Trace facts:
- `pose_stream_one` sends each sample once to every PC (`pose_stream_emit`, pc null, a function-static blob that is
  never kept on the row). A skip or defer in `pose_stream_send` drops the sample for every PC, and `stream_seq`
  advances only on a send. So every PC that was present gets the same chain; no per-PC state is needed for them.
- The join replay (`pose_replay_stream`) re-reads the current bodies and labels them `stream_seq - 1`, so a joiner's
  base differs from the other PCs'. A delta chain must not start from that replay.
- Client `pose_stream_part` chains on `grav_off`, state Applied, matching body count and blob size; it does not check
  the sample number. A snap (`pose_snap_part`) overwrites `r.blob`.

Design:
1. The host row keeps the last broadcast sample (`stream_base`, `stream_base_n`) in its wire-rounded 14 B form
   (unpack of the packed wire), which is exactly what the client decodes. It is written only on a send.
2. A sample is whole when seq % 8 == 0 (self-heal), when the body count changed, or when the delta would not be
   shorter than the whole wire; else it carries a new flag bit (16, Delta; existing 1 Unsettled, 2 Frozen, 4
   Unfreeze, 8 Stream) and every int16 field (7 per body) as a zigzag varint (at most 3 B) of the change from
   `stream_base`. Snaps and resnaps do not touch either side's stream base, so they need no whole sample.
3. The join replay sends the stored `stream_base` whole under its own seq, so the joiner's base equals everyone's.
4. The client keeps its own stream base per row (separate from `r.blob`, which snaps overwrite) and its last seq. It
   applies a delta only when seq == last + 1 and the base body count matches; else it counts a `card521: client
   delta miss` and waits for the next whole sample (at most 8 samples, 2 s).
5. A gone body is all zero in the 14 B form on both sides, so it needs no special case in a delta.

Proof (same C3M2 scenario, one run): prediction 2 (chan Pose under 300,000 B for comparable fight time, B/s with the
run length), prediction 3 unchanged, and the client delta-miss count with its cause per row.

## In source (step 2, senior, 2026-11-19, not yet in a build)

- `pose_c132.cpp`: `pose_delta_pack` / `pose_delta_unpack`; `pose_send_blob` split into `pose_send_wire` (base64 and
  parts) plus the old check and pack. `pose_stream_emit` picks delta or whole and writes `stream_base`;
  `pose_replay_stream` sends `stream_base` whole as sample `stream_seq - 1` (no fresh read). Client
  `pose_stream_part` takes the flags: a delta applies only when `stream_base_n` matches the body count and seq is
  `stream_seq + 1`; else `card521: client delta miss` (capped row), ledger Skipped "delta miss". A delta for a guid
  with no row makes no row.
- New map rows (the card152 rows are unchanged): `card521: host pose stream wire totals: D delta samples (B), W whole
  samples (B), whole every 8` and `card521: client pose stream wire totals: A deltas applied, M delta misses`.
- Round trip (g++, 20,000 random sample pairs, 1 to 64 bodies, gone bodies, small and full-range changes): 0
  mismatches against the host's wire-rounded sample, every truncated or overlong input refused; 22 bodies with
  changes of up to 5 uu and 20 rotation steps: 172 B against 288 B whole. Object compiles clean. Backup:
  `build/pose_c132.cpp.pre521s2`.
- Proof run expectations: host delta samples about 7 of 8; client delta misses only on a joiner's first sample or a
  dropped frame, each with its row; prediction 2 and 3 as above.

## Step 2 result (c263j, b301 400c5ab73c8aebd9, FED_C3M2, 339 s, senior 2026-11-19)

- Prediction 3 holds: client 650 deltas applied, 0 misses; `card132` client 213 received, 263 applied, 0 bad;
  `card140` 98 freezes, 0 over 2 uu; 0 mismatch rounds.
- Prediction 2 fails. Chan Pose 444,192 B in 339 s (1310 B/s), against c263e's 568,526 B in 431 s (1319 B/s): the
  same rate. Per sample (904 stream + 213 snaps): 397.7 B.
- Cause, from the host `card521` row: 650 delta samples, 172,286 B (265 B each); 254 whole, 79,860 B (314 B each,
  about 24 bodies). A delta is only 15 percent smaller than a whole, about 11 B per body for 7 fields, so most fields
  need 2 varint bytes (a change of 64 steps or more). 254 wholes against 113 scheduled (904 / 8) means about 141
  samples went whole because the delta was not shorter.
- Guesses not yet measured: the rotation is the absolute quaternion in int16 steps, so a corpse that turns as a whole
  changes every body's 4 rotation fields by hundreds of steps; a quaternion sign flip moves all 4 by about twice their
  value. Positions are root relative, so a falling corpse's position fields should be small.

## Diag in source (senior, 2026-11-19, after b302, host only, no wire change)

- `pose_delta_stats`, called on every sample that may go as a delta. New map row `card521: host delta field sizes
  over N bodies: position 1/2/3 B a/b/c, rotation 1/2/3 B d/e/f; F quaternion sign flips; G samples whole because the
  delta was not shorter`. Backup `build/pose_c132.cpp.pre521d`. Object compiles clean.
- Next cut is chosen from that row: rotation fields over 2 B point to a rotation relative to the root body (or a
  sign fix on flips); position fields over 2 B point to a coarser delta step.

## Field sizes (c263l, b303 8ece51514ce72a9d, senior 2026-11-19)

- Row: `card521: host delta field sizes over 19964 bodies: position 1/2/3 B 53977/5915/0, rotation 1/2/3 B
  17433/57681/4742; 701 quaternion sign flips; 129 samples whole because the delta was not shorter`. Client 725
  deltas, 0 misses.
- Positions are already 90 percent 1 B. Rotations are 72 percent 2 B and 6 percent 3 B: the 4 int16 components
  change by 64 steps or more per sample. One step is 1/32767 of a component, about 0.0035 degrees, far finer than a
  corpse needs. Sign flips are 3.5 percent of bodies, a minor part.
- Step 3 design: quantize the stream rotation to a step of 8 (wire and base both, so the host's wire-rounded base
  still equals the client's) and send rotation deltas in units of 8. A change under 512 old steps then takes 1 B.
  Rotation error grows to about 0.03 degrees. Estimate about 7.5 B per body against about 11 B now. Sign flips:
  normalize the base and the new sample to the same hemisphere (dot >= 0) before the delta.

## In source (step 3, senior, 2026-11-19, after b304, not yet in a build)

- `pose_c132.cpp`: `pose_delta_flip` (dot of base and sample quaternion below 0) and `pose_delta_z` (one field's
  zigzag value). A delta's rotation fields go in units of `kPoseRotStep` (8), rounded, from the negated sample when
  flipped; a result outside +-32767 moves one unit back. `pose_delta_unpack` multiplies rotation fields by 8. The host
  decodes its own packed delta and keeps that as `stream_base` (wholes still keep the wire round trip), so host and
  client bases stay equal and the rounding does not build up. A delta that does not decode goes whole.
- `pose_delta_stats` uses the same helpers, so the field size row counts the bytes actually sent; its flip count now
  reads `N rotations negated (dot < 0)`.
- Round trip (g++, 5000 chains of 12 samples, 1 to 64 bodies, gone bodies, 4 percent sign flips per body, small and
  large motion): 0 mismatches (positions exact, rotation components within 8 steps of the sample or its negation),
  worst rotation error 0.028 degrees, every truncated delta refused. Small motion (5 uu, 0.02 per component):
  8.0 B per body. Object compiles clean. Backup `build/pose_c132.cpp.pre521s3`.
- Predictions (next lane 2 C3M2 run): rotation 1 B fields over 70 percent (from 22), delta bytes per sample down
  about 30 percent from c263j's 265 B, fewer "whole because not shorter" (129 in c263l); client 0 delta misses, 0 bad
  payloads; card140 0 over 2 uu; digest 0 mismatches.

## Step 3 run (c263m, b306 1022ac44d36a3ed8, FED_C3M2, senior dig 2026-11-19)

- Delta: 566 samples, 134027 B = 236.8 B/sample, against c263j 265.1 B: -10.7% (predicted -30%: fails).
- Rotation field sizes 1/2/3 B: 27689/27567/0, 50.1% 1 B (predicted over 70%: fails; it was 22%). Position
  36432/5010/0. 1766 rotations negated. Whole because not shorter: 18 (c263l 129). Client 566 deltas, 0 misses.
- Half the rotation fields still need 2 B at step 8, so the rotation deltas between samples 250 ms apart are often
  over 63 steps (504 raw units, about 1.2 deg on a smallest-three part). A coarser step costs precision; the next
  size cut needs the measured spread of those 2 B fields (per body, falling against at rest) before any change.

## Step 4 diagnostic (senior, 2026-11-20, in source after b316, object compiles)

`pose_delta_stats` also buckets each rotation field delta (in kPoseRotStep units: under 64, 128, 256, 512, 1024,
more), split by whether the body's position moved more than 1 unit in the same delta, and counts the 64-and-up
fields by component (x, y, z, w). New map-end row `card521: host rotation delta spread`. How to read it in the next
C3M2 run:
- 2 B fields mostly in the moving buckets: falling or thrown bodies drive the cost. A per-body "rotation unchanged"
  bit does not help; a shorter sample interval for settled bodies, or sending only moving bodies, does.
- 2 B fields at rest too: the base drifts or the step is too fine; try a step of 16 on settled bodies.
- One component (usually w) far above the others: drop it and rebuild it on the client from the other three.

## Step 4 result (c263L317, b317 68fc2b1d8b1981c6, host line 142838)

`card521: host rotation delta spread`: at rest 8088/745/420/227/71/17 (15 percent of fields 64 steps and up), moving
28088/10023/8734/6133/3030/1032 (51 percent); 64 and up by field x/y/z/w 7566/7697/7486/7683.
- Moving bodies carry 95 percent of the 2 B fields. Dropping one component does not help (all four even); a coarser
  step at rest saves little (1480 fields).
- Only lever left: a coarser step (16) for moving bodies, where the error is hardest to see. That turns the <128
  bucket (10023 fields) into 1 B: about 10 KB per C3M2 map, under 1 percent of the chan bytes. Parked: the Fx
  footsteps (card 518 step d, about 1.4 MB per map) come first. Reopen only if the pose op grows.
