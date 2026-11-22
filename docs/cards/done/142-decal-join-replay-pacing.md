# Card 142: The card 132 decal join replay saturates the rejoin link

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: queued 2026-11-08, from card 136 hb65 review item 6. Budget: three runs.

Read: card 136 "hb65 review" item 6, card 132 (decal join replay), docs/STATE.md "Card scope and investigation rules".

Depends on: card 140. The join replay's pacing uses card 140's chan send queue (a token bucket per client
connection at 40% of its live CurrentNetSpeed, a FIFO drained each host tick, in `chan_send_client`). This card adds
no second pacing path; it sizes the join replay against that queue (its wait, and the reliable buffer's 511
outstanding bunches) and makes the rows compact.

## Why

hb65 rejoin (t=229169637): 309 of 599 frames not net ready (51.6%). Engine initial replication was 167048 B; the mod
added Decal 107616 B (card 132 decal join replay, about 74% of the mod's bytes), Fx 17946, Pose 13784. The next window
carried Digest 48400 B. The decal replay competes with the engine's initial replication on a 100000 B/s link.

Card 137 run 1 (hb68): the whole join replay is 250+ KB in its first window (Decal 84 KB, GrenadeSpawn 56 KB, Fx
50 KB, and more), near the link limit even without other traffic. The card covers the whole join replay's peak, with
the decal rows the largest part. Card 137 run 2 logs the peak.

Card 137 run 4 (hb72): the host closed the rejoined client with "reason 65 RPCReliableBufferOverflow" at 240878349,
about 850 ms after the rejoin journal replay burst (card049 41 frames, card056 3 names, card095 256 calls, card098 241
frames, pose 21). A rejoin can drop the player: this card covers the whole join replay against the reliable buffer
(511 outstanding bunches), not only its byte peak.

## Do

1. Static: what the decal replay sends (rows, bytes per row), and whether it can be sent compactly (binary rows as card
   136 does) or paced behind the engine's initial replication (send only while the connection is net ready).
2. Fix in the decal replay. Write a bound for the rejoin window's not-net-ready share before the run.
3. One run, batched with the next card that rejoins: the rejoin window stays under the bound, and every decal row still
   reaches the client (host count equals client applied count).

## Done when

- The byte split before and after, the fix, and the run lines.
- The usual Report items (WARN diff, run length, rc, ERROR/FATAL, CRASH, dumps, DLL hash). It ends with "Report
  complete."

## From card 156 run c156-r2 (senior, 2026-11-08)

The client logs a WARN for a null decal spawn after a rejoin (also in c156-r1). Trace it with this card's join
replay: it may be a decal row replayed before its world or component exists.

## Trace (junior, 2026-11-12)

Static trace only; no code, build or run. Each line is marked **verified** (file:line or log line) or **open**.

### 0. The card's premises are stale

- **verified**: hb65, hb68 and hb72 ran before card 140 (pacing) and card 150 (packing): their logs have 0 lines
  matching `card140|card150`. The Why numbers (107616 B Decal, 250+ KB, 51.6% not net ready) are pre-pacing,
  pre-packing text sends.
- **verified**: after cards 140 and 150 the decal replay is 10-13 KB of a 65-280 KB rejoin window and not the largest
  op (Fx, Pose, WorldOp are larger; section 2). Not net ready in the rejoin windows: 2.8% (c177-r1c), 5.3% (c186-r1),
  11.7% (c186-r2).

### 1. What the decal join replay sends

- **verified**: `c132_decal_replay` (mod/src/ubw/decal_c132.cpp:1031-1063, called from weapons.cpp:654 in the
  first-Pong replay) runs `host_scan`, then `host_flush(world)` (pending live rows to all ponged PCs first), then
  writes `J <n>;`, the M rows, the T rows and every live D row with its age through `out_row` into frames of at most
  kChanTextMax 1400 chars (mod/src/ue/chan.h:68), each sent with `c95_send_one(pc)`.
- **verified**: the D row is text, 22 fields, from `row_text` (decal_c132.cpp:411-428), about 112 text bytes per
  row (c177-r1c host: 149 live, 2 M, 7 T, 17266 text bytes in 13 frames).
- **verified**: the Decal op is packed on the wire (chan_ops.h:59 enum, :70 `kChanArgsPacked`; codec
  mod/src/ue/chan_pack.h). The packed blob is 15-bit units, each >= 0x80, so 16/15 bytes per packed byte plus a
  4-byte FString length (chan.cpp:26-35). Measured: c177-r1c window "Decal 10674 B / 13 rows = 821" per frame, so
  about 72 wire bytes per D row. Logged replay "bytes" are text bytes (`o.used`), not wire bytes.
- **verified (arithmetic)**: a full table (kDecalMax 512) is about 512 x 112 = 57 KB text in about 43 frames, about
  512 x 72 = 36.9 KB on the wire. M and T rows are a few hundred bytes (2 M + 7 T in the c177-r1c frame head).
- **verified**: all of it goes through `chan_send_client`'s queue: `c95_send_one` (door_c95.cpp:149) ->
  `c95_send_text` (door_c95.cpp:150) -> `chan_send_client` (chan.cpp:360). The only wire send is `send_now`
  (`g_client_message`, chan.cpp:300); chan.cpp:650 is the passthrough for the game's own ClientMessage. Inside
  `chan_send_client` a frame skips the FIFO only when the FIFO is empty and the gate says Go (chan.cpp:379), or on
  the counted exceptions `no_slot` (more than kChanPaceSlots connections, chan.cpp:375), `no_reading`
  (chan.cpp:257) and `full_sends` (FIFO full: kPaceEntries 8192, kPaceChars 1048576). No mod op bypasses the queue.
- **verified**: the measured replays: c177-r1c 149 live / 13 frames; c186-r1 164 / 19 frames on the wire
  (12176 B); c186-r2 171 / 15 frames (19575 text B, 13278 wire B); c152-r1 245 / 21 frames (28288 text B);
  c152-r3 221 / 19 frames; c177-r2 39; c164-r2 0.

### 2. The rejoin window's first window by op (newest logs with a replay)

- **verified**, c186-r2 (171 decals), window after the rejoin: not net ready 62 of 528 frames (11.7%);
  CurrentNetSpeed 250000; replicated 279948 B; reliable peak 256 (= kPaceRelHold); pace: drained 104662 B, queue
  peak 287 frames 104742 B, longest wait 559 ms, holds bits 94 reliable 39, peak QueuedBits+SendBuffer 492746 bits,
  full sends 0, no slot 0, no reading 0. By op (wire B / frames): Fx 41052/213, Pose 36252/92, WorldOp 31708/232,
  Decal 13278/26, Dropship 6156, GibPart 5900, GrenadeSpawn 5698/64, Hit 5480, GrenadeExplode 4332, Anim 3240,
  CollisionOp 1638. card150 pack: text 98501 -> packed 56505.
- **verified**, c186-r1 (164 decals): not net ready 30 of 564 (5.3%), reliable peak 198, longest wait 311 ms, queue
  peak 101266 B; WorldOp 31180, Pose 24822, Fx 22934, Decal 12176/19.
- **verified**, c177-r1c (149 decals): not net ready 17 of 599 (2.8%), replicated 79549 B, reliable peak 127,
  queue peak 280 frames 79792 B, longest wait 262 ms, holds bits 30 reliable 0; WorldOp 30812, Fx 12960,
  Decal 10674, Dropship 6154, Pose 2006, CollisionOp 1858. card150 pack: text 18636 -> packed 10784.
- **verified**: card049, card056, card095 and card098 replays go out as WorldOp / CollisionOp / Dropship frames
  through the same `c95_send_one` path; GrenadeSpawn, Fx and Pose likewise. **No op bypasses the queue.**
- **verified**: the 40% token bucket is gone. Card 140 runs 3 and 4 replaced it (comment chan.cpp:184-201): a frame
  goes when QueuedBits + SendBuffer bits + frame bits <= 0, or alone when nothing went this tick; and never while
  the PC channel holds `kPaceRelHold` 256 reliable bunches (chan.cpp:204, gate chan.cpp:263), half of the 511 limit.
  So the "40% of CurrentNetSpeed" budget no longer exists to measure against; the queue drains at what the engine
  leaves free.
- **verified (arithmetic)**: peak wait ~ queue peak bytes / (CurrentNetSpeed - replicated B/s): c186-r2
  104742 / (250000 - 55989) = 0.54 s against 559 ms measured.
- **Other ops over budget (line only, no fix here)**: Fx 41 KB, Pose 36 KB and WorldOp 32 KB are each about 3x the
  decal replay in c186-r2. If the rejoin window must shrink, those are the ops to card, not Decal.

### 3. hb72 "reason 65 RPCReliableBufferOverflow"

- **verified**: hb72 (DLL 321e8298ed49abf8, CurrentNetSpeed 100000) first-Pong burst at 240877493-240877518:
  card049 41 frames, card056 3, card095 256 calls, card098 241, card137 ext 207 rows, pose 21 frames (9355 B),
  decal 499 live (58008 text bytes, 43 frames), dropship 22 states, fx 165 asset + 465 name + 38 journal rows. All
  sent straight out in one tick (no card 140 queue then): more than 600 reliable ClientMessage bunches on the PC
  channel. The window before the close shows "map peak 355 (BP_PlayerController_C)", and the close is
  hb72-host.log:216084 "connection ... closed: reason 65 RPCReliableBufferOverflow". The decal replay was 43 of
  those 600+ bunches; card095 + card098 (~500) filled most of the buffer.
- **verified**: today's code cannot do it: `pace_gate` returns `PaceGate::Reliable` at
  `live.reliable_out >= kPaceRelHold` (chan.cpp:263), and every mod frame waits in the FIFO behind it. c186-r2 shows
  it working: reliable peak exactly 256, "holds: reliable 39".
- **open (residual)**: the bypass exceptions `no_slot`, `no_reading` and `full_sends` still send at once. All were 0
  in c177-r1c, c186-r1 and c186-r2.

### 4. The client's null decal spawn WARN after a rejoin (c156-r1, c156-r2)

- **verified**: c156-r1 client: "client decal journal replay starts (J 16)" (client-log:7777), then 10 WARNs
  "card132: client decal N spawn returned null (target T)" for decals 43-52 on targets 0, 1, 2. c156-r2 client:
  J 16 at client-log:7012, then 9 nulls: decals 28, 29, 31, 33, 35, 36, 37 on targets 0 and 1, and 32, 34 on
  target 4. Decal 30 on placed StaticMeshActor_1614 (target 2) spawned. The next round: "host live 10, client
  live 1". Before the drop, round 2 matched 10/10.
- **verified**: material and manager were not null; otherwise the WARN would be "not spawned: no material/no
  manager" (decal_c132.cpp:719-724).
- **verified**: the WARN at decal_c132.cpp:733 covers two causes: (a) `client_target` returned null and the row's
  attach is neither 1 nor kAttachAt, so `client_spawn` does `++g_dc.no_target` and no spawn (decal_c132.cpp:680-692);
  (b) the engine's SpawnDecalAttached returned null on a found component. The text "spawn returned null" names only
  (b). No post-rejoin "decal map totals" line exists in either run (both quit before LoadMap), so the
  no_target / nulls split is **open**. Attach-1 rows with no target take the world-position fallback
  (decal_c132.cpp:687), so the null rows were attach 0, 2 or 3.
- **inference, open**: targets 0 and 1 are `@guid` NPC targets. `client_target` resolves them with
  `c41_resolve_guid(pc, guid)` on every call (decal_c132.cpp:546-548) and logs nothing on failure; the host logs
  only placed targets. At the first-Pong replay the gibbed bugs' actor channels are likely not yet open (initial
  replication still running; c186-r2 shows 62 not-net-ready frames), so the guid does not resolve and the row is
  dropped for good. **This is an ordering matter**: the row arrives before its NPC. No log line shows the resolve
  result.
- **verified, not ordering**: target 4 is host "decal target 4 InstancedStaticMeshComponent of Actor_3 0.0 0.0 0.0:
  owner class Actor, placed 1, mobility 0" (c156-r2 host:8742). `host_world_row` converts only kAttachWorld rows
  (decal_c132.cpp:312-340), so non-world rows on this ISM stay attached; the client's `c95_find_nearest` cannot
  find "Actor_3" at 0,0,0 ("WARN ... target 4 InstancedStaticMeshComponent of Actor_3 0.0 0.0 0.0 not found") and
  `d.tried` stops retries. A target-resolution class, separate from this card: **new card candidate** (convert rows
  on unfindable placed ISM targets to world rows on the host, like kAttachWorld).

### 5. Fix proposal

- **Pacing**: nothing to add. The replay already goes through the one queue (section 1), with the reliable hold
  (chan.cpp:263). A second pacing path is not needed and not proposed.
- **Order of live D and E rows during a queued replay**: **verified** safe. `host_flush` runs before the replay is
  built (decal_c132.cpp:1031-1063); later live rows to that PC go through the same per-PC FIFO, oldest first
  (`chan_send_client` sends at once only when `q->count == 0`, chan.cpp:379; `chan_pace_step` drains in order).
  c95_send_all sends only to ponged PCs (door_c95.cpp:159). A live D for an id already in the replay counts as a dup
  (`client_copy(id) >= 0`); an E after the replay's D removes it. No change needed.
- **Binary D row** (the card's Do 1), if still wanted: in decal_c132.cpp, `row_text` becomes a fixed
  binary writer and `client_parse_d` a reader, and the op's kChanArgs entry in chan_ops.h:70 set to `kChanArgsBlob`.
  Layout: id u32 4, mat u8 1, target i8 1, loc 3 x f32 12, rot 3 x i16 (0.01 deg) 6, size 3 x u16 (0.1) 6,
  attach u8 1, life u16 (0.1 s) 2, sort i16 2, screen f32 4, fade start / dur 2 x u16 (0.01 s) 4, destroy u8 1,
  fade-in start / dur 2 x u16 4, age u32 ms 4 = **52 B**, about 55.5 B on the wire (blob 16/15). Full table: about
  28.4 KB against about 36.9 KB today, a 23% saving on an op that is 5% of the c186-r2 window. Variant: a field-set
  table (like M and T) for the per-material constants (size, life, sort, screen, fade, destroy) brings a D row to
  about 36 B (38.4 wire, about 19.7 KB per 512). **open**: whether those fields are constant per material.
- **In-goal fix that the logs actually ask for (the null rows)**: a client pending list in decal_c132.cpp for D rows
  whose `@` target does not resolve yet. `client_spawn` returns a "no target yet" result for an `@` target with a
  null resolve; `client_apply_d` puts the row in a fixed `g_dc.pending[kDecalMax]` table (id, row, arrival ms);
  the client tick retries each pending row (bounded by the table size), spawns it with age plus the wait, and drops
  it after a 10 s cap (counted, one WARN line with the count). An E row for a pending id removes it. A pending
  count goes into the round line, so "host live == client live" can include pending rows. Also split the line at
  decal_c132.cpp:733 so a no-target null says "no target" (counter `no_target`) and an engine null keeps its
  text. **open**: whether a late spawn changes the client's own-FIFO order check (`client_fifo_check`) in a way
  the round check sees.
- **Bounds** (written before the run): the rejoin window's not-net-ready share <= 15% of its frames (worst measured
  11.7%, c186-r2); the queue's longest wait <= 600 ms (559 measured, 0.54 s by the formula in section 2); reliable
  peak <= 256; full sends, no slot, no reading 0.

### 6. One-run proposal, batched with card 185 (c56-rejoin.txt)

- **verified**: tools/scenarios/c56-rejoin.txt kills no NPCs, so no gore decals are live on the host before the
  rejoin; the replay would send 0 D rows and prove nothing for this card.
- **Add before the disconnect** (the c164-r2 Part 1 pattern, verified there: host decal round 1 live 41, round 2
  live 153): three rounds of `host spawn bug X Y Z yaw` x3, `sleep 2`, `try host kill actor BP_AI_WarriorBug_C`
  x3, each round waiting for "card132: host decal round" with live rising, aiming for 150-250 live, some on `@`
  NPC targets (gib bodies still present at the rejoin).
- **Wait on**: host "card132: host decal replay to a joining PC: N live decals" (decal_c132.cpp:1061); client
  "card132: client decal journal replay starts (J N)" (decal_c132.cpp:830); then the next client
  "card132: client decal round N: host live X, client live Y" with X == Y (plus pending, if the pending list lands);
  the netbudget window lines ("not net ready", "pace: ... longest wait", "chan bytes per row"); host and client
  "decal map totals" (decal_c132.cpp:1085, 1096) at quit, which needs a LoadMap or a totals line at quit.
- **Predictions**: 0 "spawn returned null" after J; not net ready <= 15%; longest wait <= 600 ms; reliable peak
  <= 256; full sends 0.
- Card 185 not started.

## Trace 2 (junior, 2026-11-12)
Answers to the senior's review of the first trace. Logs and code only, no run. It replaces the 10 s cap in section 6 above.

### 1. The null rows against the corpse copies' arrival
- c156-r1 client: J line at 269155300. The null WARNs for decals 43-52 (targets 0, 1, 2) are all at 269155300. The
  host's c156-r1 log names only target 4 as placed (StaticMeshActor_488, host line 7709). `host_target` logs only
  targets that are not '@' (decal_c132.cpp:365-369), so targets 0-3 are '@guid' targets. The bug corpse copies arrive
  later: "card137: client corpse arrival BP_AI_WarriorBug_C guid 0x2a2 held" at 269169007, 0x288 at 269169705, 0x29a at
  269169808. That is 13707-14508 ms after the replay row.
- c156-r2 client: J at 269807915. Nulls for decals 28, 29, 31, 33, 35 and 36 (targets 0, 1) at 269807915, and for 37 at
  269807947. The host's placed targets are 2 = StaticMeshActor_1614 (host 5817), 3 = StaticMeshActor_488 (host 6790)
  and 4 = Actor_3 ISM (host 8742, card 204), so 0 and 1 are '@'. The corpse copies arrive at 269812790 (0x2a0
  InitArticulated) and 269812792 (0x276, 0x26e and 0x2a0 held hidden). That is 4875-4877 ms after the replay row. At
  269815808 the client logs "not posed after 3016 ms (no host pose row yet)" for all three.
- Open: the T row's '@guid' text is logged on neither side (the client does not log T rows). The match of a target
  index to a guid is by elimination: these corpses are the only NPC copies that arrive after the replay. The fix adds
  the guid to the split WARN (section 5), which closes this in the run.
- Neither c156 run has a client "decal map totals" line after the rejoin.

### 2. Where the replay fires against the NPC actor channels
- `c26_host_pong` (chan_ops.cpp:44-51) calls `c50_first_pong`, then `c49_first_pong`, then `c50_join_replay(pc)`
  (chan_ops.cpp:49).
- `c50_join_replay` (weapons.cpp:649) calls `c95_replay`, `c98_replay`, `c132_ext_replay`, `c132_pose_replay`, then
  `c132_decal_replay` (weapons.cpp:654, decal_c132.cpp:1031), then the dropship, card 136 and card 096 replays.
- The client's Pong is held only for the minimap origin (chan_ops.cpp:66-76). Nothing waits for the NPC actor
  channels or relevancy. The engine opens a corpse's channel when the corpse becomes relevant to the new connection,
  4.9 s and 14 s later in these runs. pose_c132.cpp:1584-1587 records the same case for pose rows ("the journal
  replay after a reconnect, for corpses not yet relevant to the new connection").

### 3. Existing mechanisms that hold a row for an unresolved guid
- Pose rows (pose_c132.cpp) are the only ones. `PoseState::Pending` (:123). The per-step loop (:1581-1598) calls
  `pose_try_apply`. When the reason is "guid unmapped" (:947, from `c41_resolve_guid`) the row waits with no deadline
  (:1586-1589). Any other reason gives up after `kPosePendingMs` (15000, :75). The host's R row removes the row.
  `arrival_apply` (~:1603-1613) retries at once when a corpse copy arrives.
- Hit rows (hit_c132.cpp:333), Marker rows (marker_c166.cpp:339) and HitClaim (claim_c157.cpp:379, host side) count an
  unmapped guid and drop the row. Fx (card 136) plays at world on a key miss (fx_c136_net.cpp:1369, 1421, 1468), with
  no hold. Card 095 rows find placed actors by name (`c95_find_nearest`), not by guid.
- The fix reuses the Pose pattern in the same shape:
  - A D row whose '@' target does not resolve is kept in a Pending state in the decal table, with its arrival time.
  - `c132_decal_step` (decal_c132.cpp:993) retries it each step.
  - An E row for its id removes it.
  - No new table: the pending rows live in the client's decal row store, keyed by decal id, like pose rows.
- The hold is bounded by the decal's own end, not by a fixed cap. c156-r1 waited 14 s, so a 10 s cap would have
  dropped real rows. A row is dropped when `age_s >= fade_start + fade_dur` (the expired check that already exists,
  section 4). A row with no fade waits until its E row, like a pose row. The count is bounded by `kDecalMax`. A drop
  is counted and gets one WARN line.

### 4. Age of a late-applied D row
- `client_apply_d` takes `age_s = d.age / 1000` (decal_c132.cpp:~715) and skips a row whose
  `fade_start + fade_dur > 0 && age_s >= end`.
- `client_spawn` sets `life = later(r.life, age_s) + kLifeMarginS` (:682).
- `client_fields` (:656-672) sets the fade start to `later(fade_start, age_s)`, the fade-in to
  `later(in_start, age_s)` and the life to `later(life, age_s)`. `later(v, a) = max(v - a, 0)` (:651).
- A late apply passes `age_s = (d.age + (now - arrived_ms)) / 1000` and runs the expired check again with that age.
  The fade then ends when the host's does.

### 5. client_fifo_check (the open point from section 6)
- `client_fifo_check` (decal_c132.cpp:608-625) drops a copy only when the pool is at its cap and `data[0]` is a host
  copy.
- `client_before` (:873-893) runs first. It releases the oldest own decal, or grows the cap by one, so host copies
  leave only on E rows (the comment at ~:869-872).
- So a late copy at the back of the client's pool FIFO does not change which host copies leave. The host's FIFO
  releases arrive as E rows by id. No change is needed.

### 6. The :733 WARN split (approved)
The single WARN becomes two:
- "no target", when the '@guid' is not mapped (`client_spawn`, :690-692). This is the pending case: one line when the
  row is held, with the guid.
- "engine null", when the target resolved but the spawn call returned null.

## Run c142-185-r1 predictions (junior, 2026-11-12, written before the run)
Scenario tools/scenarios/c142-185-r1.txt (c56-rejoin route, card 183 form, plus Part 1: three rounds of 3 WarriorBug
kills about 800 uu in front of the client's start, (-560, -1715, 104) from c183-r3's client goto line). One build with
card 185.
- **Bounds** (accepted): the rejoin window's not-net-ready share <= 15% of its frames; the queue's longest wait
  <= 600 ms; reliable peak <= 256; full sends, no slot, no reading 0.
- **Before the rejoin**: a client decal round with host live equal to client live (live > 0).
- **At the rejoin**: host "decal replay to a joining PC: N live decals" with N > 0. On the client, the D rows on the
  corpses' '@guid' targets log "held: no target T @guid (guid not mapped yet)", then "applied late: target T @guid
  mapped after M ms held" when the corpse copy arrives (about 5 to 14 s, c156-r1/r2). 0 "dropped while held"; 0
  "not spawned ... no target" or "engine null" WARNs.
- **After the rejoin**: the next client decal round has host live equal to client live and pending 0.
- **Client map totals at quit**: none is written (no LoadMap after the rejoin; c156 had none either). The round lines
  carry the held counts.
- **WARN classes** against c183-r3 (the last accepted c56-rejoin run): host only "attach: DisableThreadLibraryCalls";
  client only that one (the 4 TacSupp WARNs gone by card 185). A new class from the Part 1 kills is checked against
  c164-r2's Part 1 (the same kills) and named in the report.
- **Risk**: if the corpses are gone on the host before the rejoin, their decals are released (E rows) and the replay
  sends no '@' rows, so the hold path is not shown; the report says so if it happens.

## Report (runs c142-185-r1 and c142-185-r2, build 57504b1e4440dfe0)
Both runs used tools/scenarios/c142-185-r1.txt, unchanged between them; r2 is the run tag of the second run. This
is run 2 of 3 for this card. DLL hashes come from `sha256sum | cut -c1-16`.

### Files changed
- mod/src/ubw/decal_c132.cpp only.
- tools/scenarios/c142-185-r1.txt, the scenario, is new.

### Summary
- **The hold.** A D row whose '@guid' target is not in the client's package map yet is now kept as Pending in
  the decal row store (a DecalCopy with a null decal). It is not spawned at the world position, and the hold logs a
  plain capped line:
  `decal N held: no target T @guid (guid not mapped yet), age A ms`.
- **Retry and removal.** c132_decal_step retries the held rows, and each retry runs the expired check again. A row
  that applies late spawns with age_s = (d.age + now - arrived) / 1000. A held row:
  - is removed by its E row ("released while held");
  - is dropped at its fade end, with one WARN per drop;
  - is cleared by LoadMap.
- **Counts.** The round line and the map totals carry "held: pending P, applied late L, dropped D, released while
  held R".
- **The :733 WARN** is split into "held: no target" (target index and '@guid') and "engine null".
- No binary row was added, and the pacing is unchanged.
- **Hold criterion (fixed after r1).** client_guid_unmapped:
  - It looks the guid up with g_object_from_guid on the PC's connection package map (offset 0x68).
  - A row is held only when that lookup returns null.
  - A mapped guid that c41_resolve_guid does not return takes the old path, as before the card. For attach 1, that
    old path is the world-position fallback. c41_resolve_guid returns only NPC objects.
  - The 0x68 offset is duplicated as kC142ConnPackageMap, because kConnPackageMap lives in enemies.cpp, outside this
    card's files. **Senior:** move it to a shared header if you want one definition.

### Run r1 failure (DLL 88a461e02b1b828d)
- r1 ended after 76 s, drive rc 1, at line 25 (the wait for "client decal N spawned").
- Every D row was held on target @bf:
  ```
  card132: client t=114525239 decal 1 held: no target 0 @bf (guid not mapped yet), age 0 ms
  card132: client decal round 1: host live 15, client live 0; ...; held: pending 15, applied late 0, dropped 0, released while held 0
  ```
- Cause: the first criterion was "c41_resolve_guid returns null". 0xbf is the static NetGUID of a placed level actor:
  it is mapped, but it is not an NPC, so c41_resolve_guid never returns it and the hold never ended. Before the card,
  these rows took the world-position fallback.
- The fix is the package-map check above.

### Verification (c142-185-r2)
Run summary: ended by scenario after 157 s, host rc 0, client rc 0, drive rc 0, FATAL/ERROR 0 on both sides, no
crash dumps.

Live play (Part 1). There were 60 client "spawned" lines in the run. Live counts match before the disconnect:
```
client: card132: client decal 1 spawned at (-457, -724, 6) size (5, 471, 471) age 0 ms, 1 live
host:   card132: host decal round 1: live 35, pool active 35 of cap 500; E sent 0
client: card132: client decal round 1: host live 35, client live 35; pool active host 35, client 35; E applied 0 (released 0, no decal 0), D/E before the replay 0; held: pending 0, applied late 0, dropped 0, released while held 0
```
The rejoin replay and the rounds after it:
```
host:   card132: host decal replay to a joining PC: 35 live decals, 1 materials, 1 targets, 3808 bytes in 3 frames, 3 sent
client: card132: client decal round 3: host live 35, client live 35; pool active host 35, client 35; ...; held: pending 0, applied late 0, dropped 0, released while held 0
client: card132: client decal round 4: host live 35, client live 35; pool active host 35, client 35; ...; held: pending 0, applied late 0, dropped 0, released while held 0
```
The map totals at the disconnect (map end):
```
card132: client decal map totals: 39 rows, 35 spawned, ..., 34 world-position fallbacks; ...; 0 without target, ...; held 0 (applied late 0, dropped 0, released while held 0, 0 still held)
```
Bounds, from the host netbudget windows:

| Bound | Result | Accepted limit |
|---|---|---|
| not net ready, worst window | 4 of 296 frames (1.4%) | 15% |
| longest wait | 223 ms (rejoin window) | 600 ms |
| peak reliable out | 27 | 256 |
| full sends, no slot, no reading | 0 in all 24 windows | 0 |

**Against the predictions:**
- Bounds, live counts equal before and after the rejoin, and pending 0: met.
- The hold and late-apply path was not shown. The prediction was wrong: it assumed the D rows target the corpses'
  '@guid'. In this route the host decal store has 1 target, @bf, the placed ground actor. The 35 replayed rows all
  point at it, and it is mapped on the client before the replay, so nothing was held. The two "try" waits for "held:
  no target" and "applied late" timed out (10 s and 40 s), as expected given that.
- The map totals line was written at the disconnect's map end. The prediction expected none at quit; none was
  written at quit.
- The held, applied late and dropped counters and their log lines were seen only in r1, under the wrong criterion
  (15 held, 0 applied, 0 dropped). No run has shown a late apply or a drop under the fixed criterion.
- **Senior:** a hold needs a D row whose target guid is not mapped when the replay arrives, for example a decal on
  a replicated (dynamic) actor that the client gets after the decal rows. This route has no such decal. Choose
  whether r3 needs a different route, or whether r2 is enough.

### WARN diff against c183-r3
- Host: 1 WARN, "attach: DisableThreadLibraryCalls failed, err=126", the same as c183-r3.
- Client: 3 WARNs. The attach WARN is the same as c183-r3. Two are new, from round 1:
  ```
  WARN card132: mismatch STLevelObjective O:ShootingRange_SO1 presence host 0 client 1; no row sent
  WARN card132: client round 1: actors 1494 host, 1495 client, mismatches 1 (classes STLevelObjective:1) ...
  ```
  These are a timing race in the digest, not decal code:
  - The host took its snapshot at t=114717843.
  - The client's level script added ShootingRange_SO1 at t=114717847, and the host's at t=114717855.
  - Rounds 3 and 4 have 0 mismatches.

  The TacSupp WARNs from c183-r3 are gone (card 185).
- There are no decal WARNs: 0 "dropped while held", 0 "engine null", 0 "not spawned".

Report complete.

## Run c142-r3 predictions (junior, 2026-11-12, written before the run)
This is run 3 of 3, the last in the budget. The route is c156-up, with the scenario copied to
tools/scenarios/c142-r3.txt. The body is unchanged; the only additions are log-line waits after the rejoin. The
build is 8b5a5351911d4e58.

Before the build, kConnPackageMap moved to hooks_internal.h, used by both enemies.cpp and decal_c132.cpp. No other
code changed.

### Static check (c156-r2)
- **The J line** is at 269807915. The null WARNs for decals 28, 29, 31, 33, 35 and 36 (targets 0 and 1) are at
  269807915, and the one for decal 37 is at 269807947.
- **The '@' targets.** The host's placed targets are 2 (StaticMeshActor_1614), 3 (StaticMeshActor_488) and
  4 (Actor_3 ISM). Targets 0 and 1 are therefore '@guid' targets.
- **Arrival.** The only NPC copies to arrive after the replay are the bug corpses 0x2a0, 0x276 and 0x26e. They arrive
  at 269812790-269812792, 4.9 s after the J line.
- **Mapping.** A corpse's NetGUID enters the new connection's package map only when its actor channel opens, so these
  guids are not mapped at the J line. They are even, dynamic guids, unlike the static odd 0xbf of c142-185-r2.
  Under the fixed criterion (g_object_from_guid on the package map returns null), the rows on targets 0 and 1 are
  held.
- **Retry.** When the copies map, they resolve through c41_resolve_guid, because they are NPCs.
- **The '@guid' text is still logged on neither side.** The guid-to-target match is by elimination, as in Trace 2.
  The new held line prints the guid, which closes this in the run.

### Predictions
- **Hold.** After the J line, H > 0 client lines of the form "card132: client t=... decal N held: no target T @guid
  (guid not mapped yet)", with T in {0, 1} and @guid one of the corpse guids. H is about 7, from c156-r2.
- **Late apply.** Every held row is applied late: "decal N applied late: target T @guid mapped after M ms held",
  with M about 5 s (c156-r2) to 15 s (c156-r1). Late = H, dropped 0. Released while held is 0, unless the host sent
  an E row for that id before its copy mapped (c156-r2's host round 3 sent 27 E rows); if so, the count is named.
- **WARNs.**
  - 0 "not spawned: ... (target 0/1 ...)" WARNs.
  - Target 4 (Actor_3 ISM, card 204) still logs "decal target 4 ... not found" and its rows still WARN, as in c156-r2.
    That is card 204's class, not this card's.
- **Decal round.** The first client decal round after the applies has host live = client live, with "held: pending
  0".
- **Bounds** are as in r2:

  | Bound | Limit |
  |---|---|
  | not net ready | 15% or less |
  | longest wait | 600 ms or less |
  | reliable peak | 256 or less |
  | full sends, no slot, no reading | 0 |

- **WARN classes against c156-r2**, the last run on this route:
  - Host: attach, the GOD config line and the "kill actor ... no such actor" line.
  - Client: attach, GOD config, the 3 card152 "corpse arrival ... not posed" lines, the target 4 lines and the
    BP_AmmoPickup_MoritaSmall_C mismatches.
  - Nothing new.
  - The c156-r2 client "decal N spawn returned null (target 0/1)" lines are gone. Its decal round WARN is gone if it
    came from those nulls.
- **Risk.** A corpse copy may map before its mesh is ready. Then the retry spawns and logs an "engine null" or "no
  target" WARN instead of "applied late". The report says so if this happens.

## Run c142-r3 result (junior, 2026-11-12): stopped before the rejoin, no card 142 evidence
- **Build** 8b5a5351911d4e58 (`sha256sum build/dwmapi.dll | cut -c1-16`). The run ended by scenario after 130 s:
  host rc 0, client rc 0, FATAL/ERROR 0, no crash dumps.
- **Drive rc 1.** The run stopped at line 34:
  `client wait log "card095: client t=\d+ collision BP_WeaponPickup_MoritaMk1_C_0 .*InteractionCollider .*0 -> 1"
  timeout=20`. That was in Part 1, at the client's Morita pickup, so the run never reached the checkpoint and the
  rejoin.
- **Cause: the c156-up body is stale since card 145.** The host now sends a 'E' WorldOp row (card 145, the pickup's
  bEnabled) before the CollisionOp rows:
  ```
  host:   card132: sent 328 t=115259198 WorldOp 73bcfb7f E 1 to BP_PlayerController_C
  host:   card132: sent 330 t=115259198 CollisionOp 73bcfb7f InteractionCollider 1 to BP_PlayerController_C
  client: card132: recv 328 sent t=115259198 applied t=115259213 WorldOp 73bcfb7f E 1 result applied
  client: card132: recv 330 sent t=115259198 applied t=115259213 CollisionOp 73bcfb7f InteractionCollider 1 result applied
  ```
  - The client's 'E' apply runs the pickup's own status update, which already sets the colliders.
  - The CollisionOp then finds before == after, and door_c95.cpp:650 logs the "collision ... applied, b -> a" line only
    when the value changes. So the line never prints, even though the collision is set on the client.
  - c156-r2 predates card 145. It had no 'E' row, and the line printed ("applied, 1 -> 0" at 269755898).
- **WARN classes.** Host: attach and the GOD config line. Client: attach and the GOD config line. Both are in the
  c156-r2 baseline; nothing is new up to line 34.
- **Not done.** The other wait patterns of the body still have their format strings in the code, but this was checked
  statically only, and only up to line 34 at run time.
- **Proposed fix (senior to decide).** Change line 34 to
  `client wait log "card132: recv \d+ sent t=\d+ applied t=\d+ CollisionOp \S+ InteractionCollider 1 result applied"
  timeout=20`.
  This is a body change, and a rerun would be run 4, over the budget. I made neither.

## Run c142-r4: scenario audit

Every `wait log` pattern in the c142-r3 body was run with `grep -acE` against logs made after card 145 (newer than
logs/c145-r3-host.log, 67 runs). Steps before line 34 were checked against logs/c142-r3-{host,client}.log. Steps after
line 34 were checked against the newest log of a run that reached them, on FED_C1M1 where one exists.

Line numbers are c142-r3's. In tools/scenarios/c142-r4.txt the header has 4 more lines, so each body line is 4 lower
down (r3 line 34 is r4 line 38). The body is otherwise identical: `diff` shows only line 34 and the header's run tag.

| Line | Side | Pattern (short) | Log | Hits | Action |
|---|---|---|---|---|---|
| 22 | host | `playlog: host t=\d+ clock 1 ` | c142-r3 | 1 | keep |
| 23 | client | `playlog: client t=\d+ clock 1:` | c142-r3 | 1 | keep |
| 24 | client | `card156: ... back to Up \(map load\) \+5 s` | c142-r3 | 1 | keep |
| 30 | host | `card021: host GiveWeaponByClass(Morita ... host-own)` | c142-r3 | 2 | keep |
| 32 | host | `playlog: ... weapon slot 0 host-own: .* -> BP_Weapon_MoritaMk1` | c142-r3 | 1 | keep |
| 34 | client | `card095: client ... collision BP_WeaponPickup_MoritaMk1_C_0 ... 0 -> 1` | all 67 | 0 | replaced (below) |
| 38 | host | `card021: host GiveWeaponByClass(Morita ... server-copy)` | c142-185-r2 | 2 | keep |
| 40 | client | `playlog: ... weapon slot 0 client-own: ... Morita` | c142-185-r2 | 1 | keep |
| 41 | host | `playlog: ... weapon slot 1 server-copy: ... Morita` | c142-185-r2 | 1 | keep |
| 49 | host | `playlog: host t=\d+ hit slot 1 ` | c179-r2 (FED_C1M1) | 7 | keep |
| 50 | host | `playlog: host t=\d+ hit slot 0 ` (try) | c179-r2 (FED_C1M1) | 2 | keep |
| 61 | client | `card043: ... own pawn Up -> Downed` | c163-r3m (FED_C1M1) | 1 | keep |
| 64 | client | `card156: ... back to Up \(revive from Downed\):` | c157d (FED_C1M1) | 1 | keep |
| 65 | client | `card156: ... back to Up \(revive from Downed\) \+5 s` | c157d (FED_C1M1) | 1 | keep |
| 70 | host | `playlog: host t=\d+ shot s seq=` | c182-r1 (FED_C1M1) | 58 | keep |
| 77 | host | `card121: ... save engine save runs` | c142-185-r2 | 2 | keep |
| 81 | client | `card043: ... own pawn Up -> Downed` | c163-r3m (FED_C1M1) | 1 | keep |
| 83 | client | `card044: ... death screen WB_PlayerDeathScreen_C` | c163-r3m (FED_C1M1) | 1 | keep |
| 87 | host | `card121: ... save engine load done` | c199-r1, c198-r2 (bug map) | 1, 1 | keep, traced |
| 88 | client | `card045: client t=\d+ reconnected` | c142-185-r2 | 1 | keep |
| 89 | host | `card143: host t=\d+ Restore applied` | c199-r1, c198-r2 (bug map) | 1, 1 | keep, traced |
| 90 | client | `card156: ... back to Up \(map load\):` | c142-185-r2 | 2 | keep |
| 91 | client | `card156: ... back to Up \(map load\) \+5 s` | c142-185-r2 | 2 | keep |
| 93 | client | `card132: ... decal \d+ held: no target` (try) | c142-185-r1 (FED_C1M1) | 15 | keep (the hold under proof) |
| 94 | client | `card132: ... decal \d+ applied late` (try) | all 67 | 0 | keep, traced (the late apply under proof) |
| 96 | client | `card132: client decal round \d+: host live ` | c142-185-r2 | 3 | keep |
| 97 | client | `card132: client round \d+: actors ` | c142-185-r2 | 3 | keep |
| 107 | host | `playlog: host t=\d+ shot s seq=` | c182-r1 (FED_C1M1) | 58 | keep |

Traces for the rows with no hit, or no FED_C1M1 hit, after card 145:
- **Line 34.** The cause is in "Run c142-r3 result". Card 145's WorldOp 'E' row sets the pickup's colliders on the
  client before the CollisionOp row arrives, so door_c95.cpp:650 (which prints only when before != after) stays
  silent. The line that now marks the same state is the client's CollisionOp apply:
  `client wait log "card132: recv \d+ sent t=\d+ applied t=\d+ CollisionOp \S+ InteractionCollider 1 result applied" timeout=20`.
  It has 1 hit in logs/c142-r3-client.log (`recv 330 ... CollisionOp 73bcfb7f InteractionCollider 1 result
  applied`). The senior accepted this change.
- **Lines 87 and 89.** Since card 145 these printed only on the bug map (the c198/c199 checkpoint route). They also
  printed on FED_C1M1 in c156-r2, before card 145: `save engine load done (file io success 1)` at 269801413 and
  `Restore applied to BP_PlayerController_C (copy BP_PlayerCharacterMI_C, ...)` at 269807472.
  - save_c121.cpp:122 prints unconditionally after the original Load on the host (`!g_client_session`).
  - restore_c143.cpp:171 prints on every host Restore apply. The format can be prefixed "WARN ", and the pattern still
    matches.
  - Neither depends on the map or on pickups, so card 145 does not reach them. Kept.
- **Line 94.** decal_c132.cpp:851 prints when a held row's target maps (client_held_step). No run after card 145 held
  a row under the fixed criterion, so this line has never printed. It is the result under proof, and the step is
  `try`. Kept.

Result: one change, line 34. Saved as tools/scenarios/c142-r4.txt; c156-up.txt and c142-r3.txt are unchanged.

## Run c142-r4 result (build 8b5a5351911d4e58, the last run)

The run ended by scenario after 179 s: host rc 0, client rc 0, drive rc 0, FATAL/ERROR 0, no new crash dumps. Every
step passed. New line 38 (old line 34) matched `card132: recv 337 ... CollisionOp 73bcfb7f InteractionCollider 1
result applied`. The only TRY-FAILs were the 4th `kill actor` (3 bugs) and line 98 (no late apply within 30 s).

The hold path ran: rows were held. No row was applied late, and the held rows ended without being counted.

| Prediction | Result |
|---|---|
| About 7 held rows on targets 0 and 1 with corpse guids | 7 held (decals 29, 30, 32, 33, 34, 37, 39) on targets 0 and 1, but both targets are `@c5`, not a corpse guid. The rejoin's corpse copies are 0x274, 0x27c, 0x292, and the host pawn is 0x20 |
| Late applies = held, dropped 0 | Late 0. No "dropped while held" either. At 115759245 (held about 62.1 s), all 7 left the hold and went to client_place, which failed with "not spawned: no target" |
| Target 4 WARNs stay (card 204) | The replay carried 3 targets (0, 1 = `@c5`, 2 = StaticMeshActor_488); no target-4 WARN this run |
| Decal round: host live = client live, pending 0 | Round 5: host live 11, client live 4, held pending 7 |
| Digest round 0 mismatches | Round 5: 1496 host, 1496 client, mismatches 0 |
| WARN classes as in c156-r2 | Host: same 3 classes. Client: new card132 "not spawned: no target" x7 and card152 classes (below) |

Log lines (client):
```
[ 115697123 ms] card132: client decal journal replay starts (J 15), 0 copies live
[ 115697123 ms] card132: client t=115697123 decal 29 held: no target 0 @c5 (guid not mapped yet), age 6107 ms
  (decals 30, 32, 33, 34, 39 the same on target 1 @c5; decal 37 on target 0 @c5)
[ 115697123 ms] card132: client decal 31 attached to placed StaticMeshComponent0 StaticMeshActor_488 ... (35, 36, 38 the same)
[ 115758873 ms] card132: client decal round 5: host live 11, client live 4; ... held: pending 7, applied late 0, dropped 0, released while held 0
[ 115759221-231 ms] card098: client spawn BP_AI_WarriorBug_C at (12700, 13900, 156), BP_AI_Trooper_Medic_C at (16941, 13294, 139),
  BP_AI_WarriorBug_C at (13094, 14230, 106), BP_PlayerCharacterMI_C collision (copies made relevant by the warp to the bug field)
[ 115759245 ms] WARN card132: client decal 39 not spawned: no target (target 1 @c5)   (x7, all held ids)
```
Host: `[ 115696677 ms] card132: host decal replay to a joining PC: 11 live decals, 1 materials, 3 targets, 1388 bytes`.

What the code says happened:
- client_held_step (decal_c132.cpp:824) retried each held row every step.
- At 115759245, client_guid_unmapped became false, meaning 0xc5 now maps to an object in the package map. That was
  14-24 ms after the warp made copies relevant.
- The row was then removed from the hold (client_drop) and passed to client_place. There client_target, through
  c41_resolve_guid, needs that object to be an NPC (`is_a(object, g_npc_class())`), then a component of the row's
  name on it (c95_component). One of the two failed. With attach != 1 there is no world fallback, so the result was
  "no target".
- The run's logs cannot say which check failed, or what 0xc5 is. No line names guid 0xc5 on either side; host
  targets with an '@' text are not logged (decal_c132.cpp:379).

Findings (no code change, by the assignment):
1. **Accounting gap in the hold path (defect).** A held row that maps but then fails client_place is counted in neither
   `applied late` nor `dropped`. client_drop runs before client_place, and only `++g_dc.late` follows a success. The 7
   rows leave "pending" with only the generic null WARN, so "held = late + dropped + released + pending" does not
   hold after 115759245.
2. **The '@c5' target is not a rejoin corpse copy.**
   - Decals 29-39 are host decals created after the client dropped. Session 1 ended at 28 rows: "decal map totals:
     35 rows, 28 spawned" at 115694269.
   - They use targets 0 and 1, which the host registered at about 115671141 in session 1 (target 2's line is at
     115671347). host_target dedups by component pointer (decal_c132.cpp:360), and no new target was registered
     after the host's in-place checkpoint load.
   - So either the owner of 0xc5 survives the load, or the restore reused a freed component's address and matched
     the stale target text.
   - Both are hypotheses. A run would need the '@' targets logged (owner class and name) to tell them apart.
3. The c156-r2 static check counted the J-line nulls on targets 0 and 1 and took them as corpse copies. That was an
   inference from timing; the target guid text was not read. The prediction "corpse guids" was wrong on this route.

New WARN classes (client), for carding by the senior:
- `WARN card132: client decal N not spawned: no target (target N @guid)`: the 7 held rows, finding 1.
- `WARN card152: client bone check guid ... (frozen final / frozen final, one tick later / settled final)`: 5 lines;
  also seen in c164-r2 (2).
- `WARN card152: client pose stream map totals: ...`: 1 line, at the checkpoint map end.
- `WARN card152: client no stream row for guid ... (BP_AI_WarriorBug_C) 2012 ms after its game ragdoll start was
  held: its own ragdoll started`: 3 lines (0x292, 0x27c, 0x274), after the warp.

The host has no new WARN classes: attach, GOD config and cmd kill-actor, the same as c156-r2.

Hash: 8b5a5351911d4e58 (built and deployed, unchanged since c142-r3). Files changed: tools/scenarios/c142-r4.txt
(new), docs/cards/142-decal-join-replay-pacing.md.

Per the assignment there is no further run. The hold itself is proven: 7 rows held while unmapped, and retried when
mapped. The late apply is unproven. The held rows that failed to place go uncounted.

Report complete.

### Code after r4 (held_failed; build 6fb22f161e5f8c1b, no run)

Only mod/src/ubw/decal_c132.cpp changed:
- `client_guid_lookup` is new. It returns the object a row's '@guid' maps to; `client_guid_unmapped` now calls it.
- `client_held_step`: when a held row's guid maps, it calls `client_held_mapped`. That logs one capped line (cap
  kDecalLines per map): "card142: client decal N guid 0x.. now maps to <class> <name>".
- If client_place then fails, `client_held_failed` counts `g_dc.held_failed` and logs one WARN per row:
  "WARN card142: client decal N held then not placed: <reason> (target T <where>, guid 0x.. maps to <class> <name>),
  K failed".
  - The reason is "guid maps to a non-NPC" (c41_resolve_guid null), "place failed on the NPC" (no component or engine
    null), or "no '@guid' lookup".
- The round line now reads "held H: pending P, applied late L, dropped D, released while held R, failed F", so held
  = late + dropped + released + failed + pending can be read from one line.
- The map totals print failed too. `held_failed` adds into `bad`.
- One failed row also counts once in `nulls` (or `no_mat`), so it adds two to `bad`. This is the same double count
  that `no_target` + `nulls` already has for an attach-0 row with no target.

## Trace 3 (after c142-r4)

Static trace only: no code, no build, no run. Every line is marked verified (with file:line or a log line) or open.
Line numbers are in decal_c132.cpp unless another file is named. Timestamps are from the c142-r4 host and client logs.

### 1. What guid 0xc5 is and why the late place failed

**Host: what targets 0 and 1 point at**

- Verified: targets 0 and 1 were registered at 115671141 by the first decal of the map, fx 1988 SpawnDecalAttached.
  The gore trace says "on LandscapeHeightfieldCollisionComponent of Landscape ... attach 1" (enemies.cpp:1901 prints
  it).
- Verified: 0xc5 is the net guid of the Landscape actor. The steps:
  - host_target_text (:303-312) writes "@%llx" whenever `c41_guid(owner) != 0`, whatever the owner's class.
  - host_world_row (:327-354) returns false at :334 when the owner has a guid. That check runs before the
    kWorldGeometryClasses check (:323), which lists "Landscape" and "LandscapeStreamingProxy".
  - So a Landscape that has a guid never reaches the geometry list. Its component becomes an '@' target.
- Verified: targets 0 and 1 are two different LandscapeHeightfieldCollisionComponent tiles of that one Landscape.
  host_target (:356-380) dedups by component pointer, so two tiles of one owner make two targets.
- Open: which tiles they are. The host log line at :379 prints only targets whose text does not start with '@'.
- Verified: decals 29-39 are the game's own decal restore during the in-place checkpoint load.
  - Host: "save engine load runs (Load, ... destroy actors 1)" at 115687993, decals spawned at 115690570 (caller
    exe+0x512B39F), "load done" at 115690575.
  - Fx 3967-3977 map in order to decals 29-39.
  - The held set {29, 30, 32, 33, 34, 37, 39} is exactly the set on Landscape components.
  - The other four, {31, 35, 36, 38}, are on StaticMeshActors and placed ("decal 31 attached to placed ...
    StaticMeshActor_488").
- Verified: the restore rows are relative attaches. The gore trace at 115690570 says "SpawnDecalAttached 50: ... on
  LandscapeHeightfieldCollisionComponent of Landscape ... attach 0".
  - attach 0 has no world-position fallback on the client (client_spawn :713-737 falls back only for attach 1).
  - The live session-1 rows were attach 1. They fell back to world position: client map totals at 115694269 say "35
    rows, 28 spawned, ... 23 world-position fallbacks ... held 0".

**Host: does the target table survive the in-place load?**

- Verified: yes. g_dh is reset only by `g_dh = DecalHost{}` at :1226, inside c132_decal_arm_map (:1200).
  - c132_decal_arm_map is called only from c98_arm_map (world_c98.cpp:1730), which runs on LoadMap enter
    (world_c98.cpp:1678).
  - The host's LoadMap enters were at 115598768 and 115602792 only. The checkpoint load (LoadFromSlot, destroy
    actors 1) does not pass through it, so g_dh.targets keeps its entries across the load.
- Verified: g_dh.targets[i].component is a raw game component pointer. host_target compares it on every later
  decal, across frames and across a load that destroys actors.
  - This is a kept game pointer across frames, which the mod rules forbid.
  - If an NPC is destroyed by the load and a new object gets its component's address, the new decal is matched to
    the old target, with that target's stale '@guid' text.
- Verified for this run: no address was reused here. Placed level geometry is not destroyed by the load. The
  restore's landscape components are the same live objects as targets 0 and 1, and the guid 0xc5 is still right.
  The defect is latent, not the cause of r4's loss.

**Client at 115759245: which call returned null**

- Verified: client_guid_unmapped (:694-702) went false, so g_object_from_guid now returned an object for 0xc5.
  client_held_step (:823-857) then dropped the hold and called client_place.
- Verified: the null came from c41_resolve_guid, not c95_component. client_target (:556-577) calls
  `c41_resolve_guid(pc, guid)`.
  - c41_resolve (enemies.cpp:3246-3262) gets the object from g_object_from_guid on the PC's connection package map,
    then `return is_a(object, g_npc_class()) ? object : nullptr`.
  - The object is the Landscape, not an NPC, so it returns null. c95_component (door_c95.cpp:535) is never reached.
- Verified: c41_resolve uses no mod NPC table. It needs only the package map and the NPC class filter.
- Verified: the owner of 0xc5 is the Landscape. None of the four copies relevant at 115759221-231 (2 WarriorBug,
  Trooper_Medic, PlayerCharacterMI) owns it.
  - A static guid maps on the client only when a replicated reference to the object reaches it. One of those copies
    carried the reference.
  - Open: which copy, and through which property. The likely path is a replicated movement base on that landscape
    tile (inference, not verified).
- Open: the log line that would name it. In client_held_step, when a held guid first maps, log "decal N guid 0x.. now
  maps to <class> <name>" from g_object_from_guid's result.
- Verified: the client did not log the failure as a held outcome. client_place (:741-776) counted ++g_dc.nulls and
  logged the WARN "not spawned", but client_held_step counts only success (++late, "applied late").
  - That is why host live 11 against client live 4 shows no held_failed in the round line.
  - r1's failure (a non-NPC guid held forever) is the same class: an '@' target that is not an NPC.

### 2. Pace bounds

**Window 115690622: peak 387 frames, 44638 bytes, longest wait 2642 ms**

- Verified: the window ran 6545 ms, 1.5 s long. The host's load hitch took that time:
  - no log line for 865 ms before 115688881;
  - no log line for 1510 ms before 115690566.
- Verified: the queue mechanism (mod/src/ue/chan.cpp):
  - One bits hold in pace_gate (:254) made the FIFO non-empty.
  - From then on chan_send_client (:360) queued every frame behind it.
  - pace_drain (:342) runs only from chan_pace_step (:397), called once per tick from net_budget.cpp:775. No tick ran
    during the hitches, so nothing drained.
  - pace_pop_send (:303) measures the wait at pop, so 2642 ms is the time from about load start (115687993) to the
    first tick after it.
- Verified: that is why the first window drained only 361 bytes.
- Verified: bytes sent by op that window: GibPart 15224, Fx 4632, Objective 2704, Decal 1510, ObjectiveActors 1350.
- Open: the queue's own per-op contents. They are not logged; only the per-window by-op totals are.

**Window 115695629: peak 393 frames, 68874 bytes, 3047 ms, peak reliable out 140**

- Verified: the window drained 47815 bytes. Holds on bits 38; peak QueuedBits + SendBuffer 22217.
- Verified: ops by bytes were CollisionOp 41396 B (323 rows), Fx 22478 (10), WorldOp 6180 (33).
- Verified: the stall shows in "pace queue 14 frames, reliable bunches out 124" at 115691112, and again at 115691164.

**Destination, and whether a player sees it**

- Verified: there was one remote PC, 000000002BDA0BE0, the session-1 client PC.
- Verified: its Logout at 115691504 logged "pace queue ... dropped with 0 frames", so the queue fully drained before
  the drop.
- Verified: the client applied the queued frames at 115691403-115691444 (CollisionOp 275, WorldOp 33, Reload 1,
  Dropship 4). The maximum send-to-apply lag was about 772 ms.
- Verified: the client was then on the death screen. It dropped at about 115691429; its old world ended at
  115692617; it reconnected at 115694269 ("2840 ms after the drop").
- Verified: the join replay superseded those rows (decal replay at 115696677, J at 115697123).
- Verified conclusion: the 2642 ms and 3047 ms waits are queue time across the host's own load hitch, against a
  connection that was about to drop. A player does not see them as a delay of the rejoined state.
  - Window 115700631 (582 ms) is the first window after the rejoin, and it is under the 600 ms bound.
- Verified: c142-185-r2 has 0 "save engine load runs" lines and a longest wait of 223 ms (host line 4342). Its route
  has no in-place load, so no hitch.
- Verified: the bound to state is 600 ms for windows with no host load hitch, outside the load window.

### 3. Proposed fix (no code)

**(a) Accounting** — decal_c132.cpp, client_held_step (:823-857) and the round and map-total lines.

- Add held_failed. When a held row's guid maps but client_place fails, count it and log WARN "decal N held then not
  placed: <reason>", where the reason is "guid maps to non-NPC <class>", "no component <name>" or "spawn null".
- Print held_failed and the pending count in the client round line and the map totals.
- Invariant: held = late + dropped + released + failed + pending.
- Proof line: the client round line, whose five counts sum to held.

**(b) Mapped but unresolved, and the non-NPC guid** — root cause on the host.

- host_target_text (:303-312): write '@guid' only when `is_a(owner, g_npc_class())`, the same filter as the client
  resolver (enemies.cpp:3260). Every other owner takes the placed path (c95_wire_target).
- host_world_row (:327-354): apply the guid early return at :334 only to NPC owners. Landscape owners then reach the
  kWorldGeometryClasses check.
- Open: how a relative-attach (attach 0) Landscape row should travel. There are two options: the placed path, if
  c95_wire_target resolves a LandscapeHeightfieldCollisionComponent by path; or a world-space row converted on the
  host. That choice needs the placed-path resolver traced first (card 204 traces the same resolver for instanced
  meshes).
- Client, client_held_step:
  - Hold only while the guid is unmapped.
  - Once it maps: an NPC that has the component places the row.
  - A non-NPC, or an NPC without the component, is not held again. It counts held_failed and takes the world
    fallback where attach == 1.
  - The hold is bounded by the row's fade end, which already exists (held_dropped). Add a cap on held rows per
    connection, so an unbounded replay cannot grow the set.
- Proof lines: the host target line for the landscape tile shows owner class Landscape and no '@'. On the client, the
  same rows show "attached to placed" or a world-position fallback count, and no held row for guid 0xc5.

**(c) Host line for '@' targets** — host_target (:356-380).

- Log every new target, including '@' targets: "decal target N <component name> owner <class> <name> guid 0x..".
- Proof line: that line for targets 0 and 1 in the next run that has decals.

**(d) What questions 1 and 2 add**

- Host table (from Q1): stop keeping raw component pointers across frames in g_dh.targets.
  - Key each target by its wire identity: owner guid plus component name for NPCs, and the placed path for
    geometry. Resolve to a live pointer only inside the call.
  - Also clear g_dh.targets at the in-place load (the card121 "load done" point, next to the LoadFromSlot reconcile),
    not only at LoadMap enter.
  - Proof line: the host target count reset at "load done", and targets re-registered after it.
- Client (from Q1): the "now maps to" line from question 1.
- Pace (from Q2): no change to the gate. The load-hitch waits are not a player-visible defect.
  - Optional: log the queue's ops by bytes when a hold starts, so the next over-bound window names what it held.
  - Proof line: the netbudget windows after the rejoin stay at or under 600 ms.

### Proof route

- Card 142 gets no run of its own.
- Card 194 (a corpse on a rejoining client lies where the host has it) is the carrier. Its step 3 is "One rejoin run;
  round after the join: 0 corpses over 100 uu". Bug corpses carry blood decals on '@guid' NPC targets, so that route
  has a rejoin with decals on a dynamic actor (verified: docs/cards/194-rejoin-corpse-bodies-off.md:19).
- Card 203 (step 3: "the c186-r1 shape ... client rejoin or re-arrival") is the fallback carrier.
- Lines that the carrier run must show:
  - client: held, then "applied late" for an NPC '@' target;
  - client: the round line's sum holds;
  - host: '@' target lines with owner class;
  - for a checkpoint route: no held row for a Landscape guid.

Report complete.

## Senior review (2026-11-12)

Closed with the hold path in, build `6fb22f161e5f8c1b` (not run; `8b5a5351911d4e58` ran in c142-r4). Four runs
used, one over the budget: r1 wrong hold criterion, r2 no row held, r3 stale scenario wait (line 34, card 145),
r4 allowed after a whole-scenario audit.

- Pace bounds met, no pacing change: c142-185-r2 worst window 4 of 296 frames not net ready, longest wait 223 ms,
  reliable peak 27; c142-r4 582 ms in the first window after the rejoin. The 2642 ms and 3047 ms windows in r4
  are queue time across the host's in-place load hitch, to a PC that logged out with 0 frames dropped.
- Hold proven (r4: 7 rows held while the guid was unmapped, retried when it mapped). Late apply unproven: the 7
  rows sat on the Landscape (guid 0xc5), which `c41_resolve_guid` never returns. That loss (client 4 of 11 host
  decals) is card 206, with the kept host target pointer. The numbered component name is cards 206 and 204.
- Code after r4 (read, not run): `held_failed` with a WARN naming what the guid maps to, the "now maps to" line,
  the round line "held H: pending P, applied late L, dropped D, released while held R, failed F".
- `kConnPackageMap` moved to `hooks_internal.h`.

Unproven, carried: a late apply on an NPC guid (card 206's run, else card 194's rejoin run); a drop at the fade end.
