# Card 151: Smaller mod channel packets

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: queued 2026-11-08, from card 140 runs 3 and 4 (the user: "im sure we can also find ways to optimize our
packets"). Budget: three runs. Bandwidth, not gameplay. Follows card 150 (binary rows).

Read: card 140 Run 3 and Run 4 results, card 150 Result, card 136 (E rows), `chan.cpp`, the card 150 encoder.

## Why

In the c140d fight (TD 2.0), the mod offered 38 to 55 KB/s against a 100000 B/s link, while actor replication took 50
to 57 KB/s. Offered per 5 s at t=245836419: Fx 95 KB, Pose 37 KB, GrenadeSpawn 20 KB, GrenadeExplode 19 KB, Hit
17 KB, Anim 12 KB, GibPart 7 KB. Bytes per row after card 150: Fx 238, Pose 395, Decal 481, GibPart 323, Hit 100,
GrenadeExplode 98, GrenadeSpawn 90. Less mod traffic leaves room for actor movement and keeps the per-client upload a
friend's host needs low.

## Do

1. Static, per op, ranked by bytes in c140d. For each op, say what each field costs and what it can lose:
   - Wire overhead per frame: the RPC and bunch header bytes per ClientMessage call, and how card 150's binary rides in
     the FString (raw bytes, or an escaped or widened form). Count frames per tick in a fight.
   - Batching: several rows in one frame per tick and connection, so the per-RPC header is paid once.
   - Asset and name references (Fx templates, Decal materials, bone and socket names): a per-map string table, each
     name sent once and then a 16-bit index, replayed at join.
   - Quantization: positions as int32 centimetres, or int16 relative to the owning actor; rotations as packed 16-bit
     angles or a smallest-three quaternion; scales and normals 8 to 16 bit. Write the error bound for each, against the
     drift checks (kHitDrift 300 uu, the gib rest bound 2 uu).
   - Pose: send only the bones that differ from the reference pose, quantized, or a delta from the last acked pose.
   - Fx: merge identical effects with the same template in one tick into one row with a count and positions; leave out
     fields the client can derive from the attach actor.
   - Compression over a batched frame (LZ4 or zstd, if already linked), only if the numbers above leave a gain.
2. Build the changes in ranked order, in the card 150 encoder, one encoder and decoder for the class.
3. One fight run at TD 1.0 (the card 140 scenario), at the link cap in force then: offered bytes per op before and
   after, 0 decode errors, the drift and pose checks unchanged.

## Done when

- The ranked table, the changes, the run lines with bytes per op before and after. The usual Report items. It ends
  with "Report complete."

## Senior prep (2026-11-12, static, tree at card 222)

- Instruments exist: `netbudget: host ... chan bytes by op this window` (5 s windows, wire bytes per op) and the
  `pace rate` line. No new counters needed for the before and after table.
- Current baseline, summed over a run (bytes, then the worst 5 s window):
  - Fight, c201-r2 (19 windows): Fx 472056 (48520), Pose 237642 (43340), GibPart 202384 (39734), Digest 133798
    (42360), WorldOp 83088 (64412), Dropship 65726, GrenadeSpawn 50774, Hit 47866.
  - Map start and join, c206-r1: CollisionOp 75918 (41564), Fx 52162, Digest 47078, WorldOp 32066 (15470).
  So the rank is Fx, Pose, GibPart for fights; CollisionOp, Digest and WorldOp are bursts at join and at digest
  rounds, bounded by the pace queue, and are second priority.
- Fixed cost per frame, from `ue/chan.h` and `wire_bytes`: a blob frame is UCS-2 (2 bytes per unit), so prefix 6 +
  opcode 1 + length unit 1 + stamp 5 (`kChanStampLen`) + terminator 1 = 14 units = 28 bytes, plus the FString
  length 4, plus the RPC and bunch header of one reliable ClientMessage. Payload costs 16/15 of its packed bytes.
  For a 98-byte GrenadeExplode row that is about a third of the frame. One batched frame per tick and connection
  pays it once; `kChanBlobBytes` is 2623 per frame, so a batch needs a split rule.
- Rows are built as text and packed by tokens (`ue/chan_pack.h`): decimals already go as zigzag varints of their
  digits, names of 3 to 127 chars already go through a per-connection dictionary (4096 ids). So "string table" and
  "int32 centimetres" in Do 1 are partly in place; the static step must measure what is left per op (digits sent
  per number, tokens per row) before proposing quantization.
- Order constraint for batching: the ledger stamps each frame at its real send and the wire order is the ledger
  order (card 140). A batch must keep one stamp per row or the ledger must count rows inside a batch; decide this
  in the static step, before code.

## Step 1 (junior, static) — UNFINISHED

Stopped by senior order (2026-11-12) before the proposal was checked line by line; what is below is what the static
work reached. Items marked open were not traced. Aggregation script: $S/c151agg.py (scratchpad, not in the repo).

### Measured per op (netbudget "chan bytes per row" lines; "rows" there are frames, net_budget.cpp:127) — verified

c201-r2 fight, 19 windows of 5 s:

| op | bytes | frames | B/frame | peak window B (frames) at log line |
|---|---|---|---|---|
| Fx | 472056 | 3632 | 130 | 48520 (235) c201-r2-host.log:7520; peak frames 268 at :8909 |
| Pose | 237642 | 647 | 367 | 43340 (110) :6209 |
| GibPart | 202384 | 1204 | 168 | 39734 (134) :7520; peak frames 185 at :5545 |
| Digest | 133798 | 65 | 2058 | 42360 (18) :9632 |
| WorldOp | 83088 | 571 | 146 | 64412 (436) :3502 |
| Dropship | 65726 | 248 | 265 | 10522 (40) :3914 |
| GrenadeSpawn | 50774 | 596 | 85 | 10358 (121) :7971 |
| Hit | 47866 | 485 | 99 | 6544 (32) :7520 |
| GrenadeExplode | 47218 | 550 | 86 | 8896 (104) :8374 |
| Anim | 42876 | 1191 | 36 | 4896 (136) :8909 |
| Decal | 40408 | 272 | 149 | 16076 (65) :7520 |
| CollisionOp | 1500 | 9 | 167 | 948 (5) :3502 |

Sum 1425336 B in 9470 frames. Worst window :7520: 185770 chan B, 771 frames, rpc 202281 B in 788 calls.

c206-r1 join, 29 windows: CollisionOp 75918 B / 584 frames (peak 41564 in 324, c206-r1-host.log:9447), Fx 52162 / 202,
Digest 47078 / 35, WorldOp 32066 / 220 (peak 15470, :10458), Pose 19032 / 51, GibPart 5628 / 72, GrenadeExplode 282 / 1,
Hit 240 / 3.

### Fixed cost per frame

- Prefix 6 + op 1 + stamp 5 + terminator 1 (+ blob length 1) = 14 units; FString length 4 (chan.cpp:26-35). Verified
  from code.
- Every stamped frame is UCS-2 in practice: the stamp's ms units (chan.cpp:67-81, 15-bit values +1) are below 0x80
  only 128 times in 32768, and ms>>15 at a ~110M ms tick is ~3357. So text-op frames (Digest, WorldOp, Dropship, Anim,
  CollisionOp) pay 2 bytes per char though their text is ASCII. Verified from code; not cross-checked against the rpc
  bits per frame.
- RPC and bunch header: measured from rpc minus chan per call, 13.6-22.8 B (c201-r2), 13.1-22.7 (c206-r1). From code:
  open. So one frame costs about 28 + 4 + ~17 ≈ 49 B before payload; ~9470 frames × 49 ≈ 464 KB of the c201-r2 fight.

### Row formats, top three fight ops

- Fx (op 41, kChanArgsBlob, not the card 150 packer, chan_ops.h:60-78): binary, batched already, one frame per tick per
  PC (fx_c136_net.cpp:974-999, split at kChanBlobBytes in frame_add 920-937). E row (host_body 813-874): tag u8, id
  varint, point u8, asset u16, mask u16, then present fields (body_mask 790-810): loc 3×f32 12, rot 3×u16 6, scale 12
  (only when not one), comp/socket/guid varints, placed varint+12, owner_class, look 12, seed 4, world 12, of, flags u8,
  physmat/atten/conc varints. Map totals c201-r2-host.log:10222: 9587 E rows, 309801 B, 32.3 B per E row; 12431 rows in
  3648 frames. No log prints a row: open.
- Pose (op 38, packed text): "P <guid> <snap> <part> <parts> <bodies> <flags> <b64>" (pose_c132.cpp:554), 7 tokens plus
  one base64 token; the packer carries base64 as its bytes (chan_pack.h:14). Blob: root 3×f32 12 + per body 3×int16
  (kPoseUnit 0.25 uu) + 4×int16 quaternion = 14 B (pose_c132.cpp:60-66, 491-523). So Pose is already near binary; the
  body rows are ~97 % of its payload. No row in the logs: open.
- GibPart (op 42, packed text, gib_c137.cpp:15-19): S 16 tokens with the class path (dictionary), M/R 8 tokens
  (gib_pose_text 294: whole uu and degrees), G 2. c201-r2: 770 S, 3842 M, 767 R (gib totals c201-r2-host.log:10223,
  298782 text bytes). Wire payload ≈ (202384 − 1204×32) / 5379 rows ≈ 30.4 B per row. Estimated M row packed: inline
  kind 2 + id 3 + x,y,z 4+4+4 + angles ~7 = ~22 B, ×16/15 ≈ 23.5 B on the wire (estimate, not measured per kind).

### Proposal, ranked by predicted saving in c201-r2 (≈1.59 MB with rpc headers) — predictions, not checked

1. Cross-op batching, one frame per tick per PC: 9470 frames → ≥3632 (lower bound: the Fx frame count), ≈45 B saved
   per removed frame after a 4 B per sub-row head: ≤ ~263 KB (~16 %). Files: ue/chan.cpp send_now / pace_pop_send,
   ubw/ledger_c132.cpp c132_ledger_send / recv. Ledger rule pick: one stamp per batch frame, ledger counts rows inside
   it. Reason: c132l_check_gap (ledger_c132.cpp:334-342) needs one contiguous seq per frame, and per-row stamps cost
   10 B each (5 UCS-2 units), ~58 KB back. Catch: Digest B carries S = last seq sent (digest_c132.cpp:733-736,
   ledger 525); rows sharing a seq with B would be misplaced, so a B frame must start its own batch (open).
2. ANSI-safe stamp (7-bit units, ~10 units): text ops go 1 B per char. Saving ≈ (bytes − 4×frames)/2: Digest 66.8 K,
   WorldOp 40.4 K, Dropship 32.4 K, Anim 19.1 K (if its args are ASCII: open), CollisionOp 0.7 K ≈ 159 KB (~10 %);
   join c206-r1 CollisionOp ~37 KB. Files: ue/chan.cpp stamped and the stamp read. No precision change. Cheapest per
   byte; recommended first.
3. Fx flush every ~50 ms instead of every tick: 3632 → ≤~1900 frames, ≈85 KB (~5 %), ≤1 extra tick of latency.
   fx_c136_net.cpp host_flush. Overlaps with item 1.
4. Pose rotation as smallest-three 32-bit (3×10 bits + index): 14 → 10 B per body, ≈60 KB (~4 %). Error ~0.1° per body,
   ~0.1 uu at 60 uu, against kPoseTolerance 100 and kPoseFreezeTolerance 2 (pose_c132.cpp:69, 76). Join replay sends
   the stored blob, so encode and decode change together (pose_encode_to 491, client decode ~1657).
5. GibPart M/R as binary (int24 1 uu positions; M angles int8 1.4° under kGibTurnDeg 5; R keeps whole degrees):
   ≈33 KB (~2 %); with int16 deltas from the last row ≈45 KB. Rest bound 2/10 uu unchanged (whole uu now). S rows keep
   text and the dictionary. Join replay path for gib rows: open.
6. Digest H/P frames as binary blob (8 B per slot against 16 hex chars): ≈33 KB more after item 2. Digest is a check
   path; whether it runs in the play build: open.
7. Fx loc f32 → int24 at 1 uu: 3 B per E row, ≈31 KB (~2 %), error 0.5 uu (kHitDrift 300, hit_c132.cpp:77).
8. WorldOp and Dropship through the card 150 packer: ~40 % below the ANSI form, ≈29 KB after item 2.

### Not to do

- Compression: nothing linked, the DLL imports only kernel32 (winmsg.h:4-5); rows are short and already varint
  packed.
- Per-row stamps in a batch: 10 B per row eats a fifth of the batching gain.
- Fewer samples (Fx, gib, pose rates): changes what is synced; out of scope.
- f16 positions: no precision at map coordinates of ~230k uu.

### Open

The RPC/bunch header from code; Anim args ASCII; the gib join replay format; the Digest play-build use; per-kind GibPart
sizes; the tick count with any send (it bounds item 1).

Files read: ue/chan.h, ue/chan.cpp, ue/chan_pack.h, ubw/chan_ops.h, ubw/net_budget.cpp, ubw/fx_c136_net.cpp,
ubw/pose_c132.cpp, ubw/gib_c137.cpp, ubw/ledger_c132.cpp, ubw/digest_c132.cpp, ubw/hit_c132.cpp, winmsg.h; logs
c201-r2-host/client, c206-r1-host.

## Merged in 2026-11-17 16:20: card 431 (ledger stamp, same frame cost)

Senior decision on 431's open choice: (b). Row batching in step 1 pays the 22 B fixed frame cost (stamp 10 B of it) once per batch frame instead of once per row, which is the larger gain and the same encoder change; (a) the short stamp is dropped unless batching leaves stamp share above 5 % in the after run. The 431 measurements (stamp share 7.5 to 10.5 %, fixed share up to 23 % host bytes in c423b127a, peak windows 13 to 22 %) are the before numbers for that row. The proof adds 431's items: `WARN card132: seq gap` 0, clock offset rows within 2 ms of the run before, digest mismatches 0, one reconnect in the run. Full text: `done/431-ledger-stamp-is-ten-bytes-on-every-blob-frame.md`.

## Senior step 1 close-out (2026-11-17)

Open items from step 1, answered from code:

- Anim args: not text. c41_send_row (enemies.cpp:3929) sends a numeric args frame, `chan_to_client_args(pc, ChanOp::Anim, ChanArgs{3 x uint32})` (chan.cpp:550, `chan_frame`). Item 2's ANSI saving does not apply to Anim; its 19.1 K estimate is dropped. Anim gains only from item 1 batching.
- Gib join replay format: `c137_gib_join_replay` (gib_c137.cpp:1340-1420) packs S, M and R text rows into text frames up to kChanTextMax and sends each with `c95_send_one(pc, ChanOp::GibPart, text)`. It is already row-batched per frame and goes to one PC; item 5's binary M/R change must change this packer and the client parse together (the replay reuses `gib_pose_text`).
- Digest play-build use: digest rounds run in the play build, every kC386PlayRoundMs 120 s instead of kC132RoundMs 30 s (digest_c132.cpp:62-66); test runs keep 30 s through UBWCOOP_TEST_SKIP. Digest bytes in a play build are a quarter of the measured run, so item 6 is worth about 8 KB per measured span in play: drop item 6.
- Item 2 (narrow stamp) is done by card 390 (`narrow_stamp`, `kChanStampNarrowLen`, chan.cpp:106, 343).
- RPC/bunch header and the tick count stay unmeasured from code; the after run measures them as host bytes per frame against payload bytes (net_budget rows) instead.

### First build step (bounded)

1. Cross-op row batching on the host, per tick per remote PC: text and packed rows queued in the tick go out as one frame per PC, split at kChanBlobBytes.
2. One ledger stamp per batch frame; the ledger counts rows, not frames, so `c132l_check_gap` sees one contiguous seq per frame.
3. A Digest B frame (it carries S = last seq sent) starts its own batch, never shares a seq with rows.
4. Receiver splits a batch into rows and applies them in order through the existing per-op receivers.

Done when: one c448 gate run with host bytes per second below the b195 c448 run, `WARN card132: seq gap` 0, digest mismatches 0, clock offset rows within 2 ms of the run before, one reconnect in the run (card 431 items). Items 3-8 wait for the after numbers.

## Step 1 results (b197, b198; 2026-11-17)

Builds: b197 `dwmapi-b197-bba934ab82200699.dll` (batching, Pl PlayerState filter), b198
`dwmapi-b198-4ed937db0ddf506f.dll` (b197 + the host Pong sent now). Runs c448b197a and c448b198a, both scenario passed,
EXCEPTION/FATAL 0 on both logs of both runs.

| run | chan sent B/s (host) | non-Digest chan bytes | batches / rows | card132 rtt | session offset | seq gap | mismatches |
|---|---|---|---|---|---|---|---|
| c448b195a | 467 | 75891 | - | 30-31 ms | -6, -11 | 0 | 0 |
| c448b196a | 574 | 76050 | - | 30-31 ms | -6 .. -21 | 0 | Pl:wp 1 (6 rounds) |
| c448b197a | 481 | 64954 (Batch 3036) | 106 / 406 | 46-48 ms | -16 .. -19 | 0 | Pl:wp 1 (5 rounds) |
| c448b198a | 442 | 58721 (Batch 2912) | 104 / 383 | 28-30 ms | -7 .. -21 | 0 | Pl:wp 1 (5 rounds) |

- Bytes: batching takes about 15 % off the non-Digest chan bytes (Fx 31100 -> 23002 in b197). Digest bytes rose in
  b196 (7053 -> 23882, card 323 Pl entries), so b197's total was still above b195; b198 is below it (442 < 467 B/s).
- b197 clock: the host's Pong was batched and left at the tick's flush, about one 60 fps frame later; the client's
  card132 rtt rose 30 -> 46 ms and the session offset moved about -10 ms. b198 sends the Pong now
  (`chan_send_client_now`, chan_ops.cpp c26_host_ping): rtt 28-30 ms, offsets in b196's range.
- Latency: every other batched row also goes at the end-of-tick flush, up to one host frame later than unbatched.
- Stats: pace_gate runs per row inside a batch, so the alone and no-reading counts are rows, not frames.
- Reconnect: one rejoin in each run (card445 "kept tactical charge ... after the rejoin").
- The only mismatch is the standing Pl:<guid>:wp entry of the host pawn: host c9c4 = BP_Weapon_MoritaMk1_C, client
  1903 = BP_Weapon_Unarmed_C. c448b198a client shot c448-host-weapon-on-client (357 uu, facing) shows the host pawn
  holding the Morita, so it is a reader gap: the client's copy of the host pawn keeps only Unarmed in its weapon
  inventory (client card021 rows for "other": Unarmed only), and the digest reads the inventory's equipped weapon.

## Closed (2026-11-17 19:52)

Step 1 done in b197 (cross-op row batching, one stamp per batch frame) and b198 (Pong sent at once with chan_send_client_now; batching had added ~16 ms RTT). c448b198a, b198 4ed937db0ddf506f: chan 442 B/s (b195 467), non-Digest bytes 58721 (before ~76 K), seq gap 0 both sides, RTT 28-30 ms, session offsets -7..-21 (b196 range), 1 rejoin, FATAL/ERROR 0. Digest mismatch rounds 3-7, 1 key each: the standing PlayerPawn Pl:8e:wp reader gap (card 323), present before batching in b196.

Not done: items 3-8 (Fx flush rate, pose smallest-three, gib binary M/R, Fx loc int24, WorldOp/Dropship packer); item 6 dropped (Digest runs every 120 s in play). These belong to card 329 if its measurements still rank them.

Reopen when: chan bytes per second rise above 467 B/s on a c448 run, any `WARN card132: seq gap`, or RTT above the b196 range.
