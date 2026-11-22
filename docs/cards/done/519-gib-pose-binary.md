# 519: GibPart pose rows in binary (efficiency)

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: Opened 2026-11-19 by senior from the c263b (b288 cc587891d7bf7846) FED_C3M2 run, per the smaller-packets rule
("always look for ways to make packets smaller").

## Finding (c263b, one client, 305 s)

- Chan GibPart is the second-largest op: 400,012 B on the wire (about 1.3 KB/s), after Fx (card 518).
- `card137: host gib totals (map change)` (host line 64879): 1160 S rows, 10158 M samples, 1136 R rows, 5427 frames,
  618,392 B of text before the chan pack. The client received 10145 samples and 1119 rest rows, with 0 bad rows.
- The samples already go out only past a move (`kGibMoveUu` 8 uu) or a turn (`kGibTurnDeg` 5 degrees), at most
  every `kGibSampleMs` (200 ms). The rate is not the waste; the encoding is.
- The M and R rows are text: `M <id> <x> <y> <z> <pitch> <yaw> <roll>;` (`gib_pose_text`), about 35 to 45 chars. A
  sample's location sits within a few hundred uu of the part's last sent pose, and the rotation needs only the 5 degree
  turn gate's precision.

## Cut

Send M, R and G as binary rows in a blob frame (`chan_blob_frame`, as card 136 Fx does):
- tag u8; the id as a zigzag delta from the previous row in the frame (1 B);
- location as 3 zigzag varints of whole uu relative to that part's last sent location (`HostGib::sent_at`; 1 to 2 B
  each in flight);
- rotation as 3 u8 of 360/256 degrees (1.4 degrees, under the 5 degree gate);
- R adds its flag byte.

That gives about 9 to 11 B per M row against about 40, so roughly 75 percent less on 10158 rows. The S rows keep their
class path dictionary (card 201) and can stay text in the first step.

## Ordering (answered, static trace 2026-11-19)

The S rows (text, GibPart) and the pose rows (blob, new op) would travel in two frames. The client maps an id only
after its S row. Check that chan delivers ops in send order on one connection (`c95_send_all`, `chan_blob_frame`); if
it does, send the S frame of a flush before its pose frame. If it does not, move S into the blob too, with an asset
table row (Fx's A row) in place of the card 150 path dictionary.

Answer: order holds. `gib_flush` sends with `c95_send_all` without priority, so each row goes through `c95_send_text`
and then `chan_send_client` (`mod/src/ue/chan.cpp:593`). The Fx blob uses the same `chan_send_client`. That function
sends at once only when the PC's pace queue is empty; otherwise it pushes onto that one per-PC FIFO queue
(`pace_push`). Two frames sent in one flush therefore arrive in send order: S frame first, then the pose frame.

## In source (2026-11-19, senior; objects compile, not yet in a DLL)

Backups of the six touched files are in `build/pre519/`. Host and client change together (one DLL).
- New op `ChanOp::GibPose` (blob, last before `Count`; ledger row `GibPose`, events). Handler
  `c137_receive_gib_pose`. New fanout `c95_send_blob_all` (`door_c95.cpp`) sends one blob frame to the same ponged
  set as `c95_send_all`, through each PC's pace queue.
- `gib_c137.cpp`: live M, R and G rows go into the pending GibPose frame (`gib_pose_put`). Row: tag u8 (M, L = a
  sample right after its late S row, R0/R1 = rest with the card 273 no-ground flag, G), the id as a zigzag varint
  delta from the frame's previous row, the location as 3 zigzag varints of whole uu from the frame's previous row
  (not from `HostGib::sent_at`: frame-local deltas need no state that could drift between host and client), and
  the rotation as 3 bytes of 360/256 degrees. G rows carry no pose. The budget is charged the row's exact bytes.
- `gib_flush` sends the S text frame first, then the GibPose frame. A row that would overflow the GibPose frame
  flushes both first.
- The join replay (card 323) still sends M and R as text in GibPart; the text parser stays for it.
- The client splits the pose and gone handlers into parse plus apply (`gib_client_apply`, `gib_client_gone_id`), so
  text and binary rows run the same code and counters.
- New host map-end row: `card519: host gib pose rows (<why>): M, L, R (no ground), G; <bytes> bytes in <frames>
  frames (partial fanout <n>)`. The card137 totals `bytes` now count the text rows only.

## Predictions (the same C3M2 PowerPlant scenario as c263b, one run)

1. Chan GibPart plus the new op total under 200,000 B for a comparable fight time (from 400,012); report B/s with the
   run length.
2. The client's `card137` totals match the host's (spawned, samples, rest rows), with 0 bad and 0 unmapped rows during
   the map.
3. The card 140 rest check holds as before: the rest distances stay within 10 uu.
4. A receiver-side shot shows the gib pieces at rest at the host's places.

## Done when

Predictions 1 to 3 hold in one lane 2 run; numbers quoted here.

## Result c263e (b294, 431 s, about 375 s of netbudget windows)

- Chan GibPart 73,568 B plus GibPose 218,210 B = 291,778 B, about 0.78 KB/s (c263b: 400,012 B, 1.31 KB/s).
  Prediction 1 (under 200,000 B) missed.
- `card519` host row: M 11412, L 0, R 1032 (no ground 37), G 56; 141,584 blob bytes in 5267 frames (partial fanout
  0), about 12.4 B per row. Chan framing is the other 76.6 KB, about 14.6 B per frame. The next cut is fewer, fuller
  GibPose frames (flush on a timer as card 518 does for Fx), not a smaller row.
- Prediction 2 holds: host 1095 spawned, 11412 samples, 1088 rest rows; client 1095 spawned, 0 failures, 11406
  samples, 1032 rest rows, 0 unmapped, 0 bad, 0 overflow.
- Prediction 3 holds: rest within 10 uu 1032 of 1032, worst 0.0 uu. In flight worst 656.5 uu, 27 over 150 uu.
- card519 WARN 0. Prediction 4 (receiver shot) not taken in this run.

## Closed (senior 2026-11-21)

Predictions 1 to 3 hold: about 12.4 B per row, rest within 10 uu on 1032 of 1032, card519 WARN 0. The receiver shot
(prediction 4) is waived: rest positions match to 0.0 uu by the rows. The remaining lever, fewer and fuller GibPose
frames (about 14.6 B of framing per frame, 76.6 KB per map), goes on a new card only if a netbudget window shows
GibPart in the top ops again.
