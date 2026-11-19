# Card 201: A gib burst does not lose rows or show pieces seconds late on the client

Status: queued 2026-11-12, split from card 148 (c148-r2). Budget: three runs. Static first; the senior reviews the
trace before code.

Read: `docs/cards/done/148-gib-sync-client-apply.md` (Report, r2), card 140 (the GibPart budget and why it exists:
hb68 overflowed the reliable buffer and dropped the client), `docs/STATE.md` "Card scope and investigation rules".

## Why

c148-r2 host, nuke burst of 1791 parts in about 10 s: `S recorded 1791, sent 1479, not sent at a full queue 312
(peak waiting 1024 of 1024), longest S wait 13116 ms; R ... not sent at a full queue 76`. 312 host pieces never
appear on the client, 62 client pieces never get their rest row, and a piece can appear 13 s after the host's. In the
same run 1114 of 1479 parts ended "at a pool relaunch": most S rows describe a part the pool has already launched
again. Two new host WARN classes (spawn queue full, rest queue full).

## Do

1. Trace, no run. Answer each, verified or open:
   - Bytes per S, M, R and G row as sent, and the share of the class path in an S row.
   - How many of the r2 S rows were still waiting when their part was relaunched or gone (the row then describes a
     piece the host no longer shows). Those rows, and the G row that follows, need not be sent at all.
   - What bounds the budget: the reliable buffer limit card 140 measured, in bytes per window, and how far
     `kGibBudgetBytes` is below it.
   - The host pool size per class against the burst (why 75% of the parts are relaunched before they rest), and
     whether the client's pool relaunches the same actors in the same order.
2. Fix at the layer that owns it. Expected from the senior's read, to confirm or replace in the trace:
   - cancel a waiting S row when its part is relaunched or gone (no S, no G for it), and send S rows newest first or
     drop the ones older than a set age, so a shown piece is never seconds late;
   - send each class path once and an index in the S row; rows in a packed binary form if the text form is the cost;
   - queue sizes from the measured peak, with the peak logged against the cap.
   No silent loss: any row still not sent is counted by reason, and the client piece it leaves behind is ended
   (hidden) rather than left unrested.
3. One run with the r2 burst (the nuke) with the client near it.

## Done when

- The trace; the run shows S and R "not sent at a full queue" 0, or every unsent row counted as cancelled because
  its part was already taken back; longest S wait under 1000 ms; window peak within the budget; rests within 2 uu
  equal to checked; above the ground 0 on both sides; no queue-full WARN. The Report ends with "Report complete."

Files: `mod/src/ubw/gib_c137.cpp`, `mod/src/ubw/hooks_internal.h`, the card 140 budget code the trace names, one
scenario, this card.

## Trace (step 1, no build, no run)

Sources: `mod/src/ubw/gib_c137.cpp` (card 148 final, DLL fe4ed32fc8560d92), `mod/src/ue/chan_pack.{h,cpp}`,
`logs/c148-r2-{host,client}.log`, card 140 runs 3 and 6, and the class paths from the game's
pakchunk0-Windows.utoc index. Row bytes were measured with an offline harness that compiles the real
`chan_pack.cpp` (g++, the same `pack_text` the transport and `gib_cost` call).

### Q1. Bytes per row as sent, and the class path's share (verified)

The r2 burst used 9 gore classes (card042 lines): the Sticky Alien_Generic Gib01-04, GibGut01-02 and the
NonSticky Alien_Generic Chitin07-09. `c50_class_path` sends "<package>.<class>"; for these classes that path is:

- 126 chars for Gib01-04, e.g. `/Game/Data/Gore/EnemyData/3DGore_Alien_Generic/BP_GoreData_Sticky_Alien_Generic_Gib01.BP_GoreData_Sticky_Alien_Generic_Gib01_C`;
- 132 chars for GibGut01-02;
- 138 chars for Chitin07-09.

The dictionary (card 150) takes only strings of 3..127 chars (`kPackDictLen`). So the GibGut and Chitin paths (5
of 9 classes) are never dictionary refs: they go inline in every S row, on the wire too.

Packed bytes, measured with typical values (the numbers vary by a few bytes):

| Row | Text chars | Budget cost (inline, `gib_cost`) | Wire, first send | Wire, later sends |
|---|---|---|---|---|
| S, 126-char path | 216 | 177 | 178 (def) | 51 (ref) |
| S, 132-char path | 222 | 184 | 184 | 184 (inline) |
| S, 138-char path | 228 | 190 | 190 | 190 (inline) |
| S without the path (`-`) | 91 | 51 | 51 | 51 |
| M | 36 | 25 | 25 | 25 |
| R | 33 | 25 | 25 | 25 |
| G | 7 | 7 | 7 | 7 |

The class path is 126 to 139 of an S row's 177 to 190 inline bytes, about 72%. The 14 numbers cost 51 bytes.

The r2 logs agree:

- In the drain windows (t=108159434 to 108172564), each window spent 16200 to 16368 budget bytes while the S queue
  fell by 80 to 86 rows. That is about 195 to 200 budget bytes per S row.
- In the same 5 s windows, the wire GibPart bytes were 87092 and 87442 B (about 17.4 KB/s), about 1.06 times the
  budget.

So the wire costs what the budget charges. The card 150 dictionary saves almost nothing for this burst, which
corrects the premise that the path already goes once and then as an index.

### Q2. S rows still waiting when their part was relaunched or gone (lower bound verified, exact count open)

- No per-row log exists, so the exact count is open.
- **The code.** `gib_host_sample` returns early while `spawn_unsent`, so a part makes no rest row (still, gone or
  bound) before its S row is sent. A relaunch, in `gib_host_record` with `r->live`, queues a G row for the old id
  whether or not that id's S row was sent. `gib_spawn_pop` sends the S row even when `r.id != q.id[at]` (the part
  was already launched again).
- **The burst came in one step.** At the first window line after the burst (t=108159434, window
  t=108158333), 0 of 16384 bytes were spent while S 1024 and R 832 were waiting. The burst was recorded before
  any of its S rows went out, with log stamps spread from 108159287 to 108159433 (likely one long frame; open). 312
  more S rows met the full queue in that step: the burst-2 peak was 177, so every one of the 312 drops was in
  burst 1. Burst 1 therefore recorded at least 1336 parts.
- **The 832 R rows.** In that state the rest queue can hold only relaunch G rows for burst parts whose S row was
  still waiting, plus rows of parts launched before the burst (P).
  - P is at most the S rows sent in the 10 s before the burst (the `kGibMaxMs` bound).
  - Wire GibPart in that time was 3358 B in the window ending t=108156242 plus at most about 6 KB after it, at 51
    or more bytes per S row. So P is at most about 150.
  - So at least 832 - 150 = about 680 S rows were stale when they were sent, and likely about 800 or more.
- **After that.** The rest queue grew from 832 to 1024 while no R or G row could go (S rows go first). The upper
  bound is the 1114 relaunches.
- **What they cost.** At least 680 stale S rows times about 195 budget bytes is at least 133 KB, which is at least
  8 of the 14 windows the burst-1 drain took.
- **What the client showed.** Each stale S row spawned a client piece for a host piece that the host had already
  taken back. Its G row arrived only after the whole S queue had drained, seconds later (the 13116 ms longest
  wait). Meanwhile the client's own pool reuse freed 991 of those rows ("freed by a pool reuse 991", 991 unmapped
  with no row).

### Q3. What bounds the budget (verified: no reliable-buffer byte limit was measured)

Card 140 measured no byte limit for the reliable buffer. What it measured:

- The run 3 `RPCReliableBufferOverflow` came from a FIFO flush that put 511 reliable bunches out at once. The limit
  is counted in bunches, not bytes.
- Since then the pace queue holds at 256 reliable bunches out (read live, NumOutRec). It keeps a frame's bits within
  one 30 Hz tick of CurrentNetSpeed (250000 / 30, about 8333 B), and a larger frame goes alone when the connection
  is idle.
- Run 6 (c140f) capped CurrentNetSpeed and MaxClientRate at 250000 B/s.

`kGibBudgetBytes` = 16384 is a share choice ("4096 was sized for the text link, 16384 for the packed one"), not a
measured ceiling. It is 6.6% of the 250000 B/s link.

In the r2 burst window (5 s ending t=108161244):

- all replication was 46772 B/s;
- every mod frame (ClientMessage) was 154079 B / 5 s = 30.8 KB/s;
- IsNetReady was false on 8 of 286 frames;
- GibPart wire peaked at 17.5 KB/s.

About 200 KB/s of the link stayed free, so the link would allow a higher budget. It is not needed with the fix
below (the post-fix burst fits one window), so this card keeps 16384.

### Q4. Pool sizes, and the client's relaunch order (client verified; host size open)

- **Host.** The parts come from the GameMode's poolers (card042 slots 0 to 5, one per kind). The burst used three
  kinds: Sticky StaticMesh (Gib, GibGut), NonSticky StaticMesh (Chitin) and NonSticky Sprite. The game config count
  is 100 per pooler ("config 100" on the client's build line).
  - The host's live pool size is not logged: open.
  - About 1336 parts over 2 or 3 kinds of about 100 actors, in one step, launch each actor about 4 to 6 times
    before anything can rest. (1336 - 300) / 1336 is 78%, consistent with r2's 75% "at a pool relaunch".
  - Many relaunches happen in the same frame, so the stale S rows describe pieces the host never showed.
- **Client.** `c42_spawn_exact` spawns from the client's own pooler (owner PC), built with `kC42PoolMax` = 24 actors
  per kind ("24 actors (config 100)").
  - It takes only the rows that were sent (1479 of 1791), over 14 s instead of one frame.
  - So the client pool relaunches different actors in a different order, and its 24 per kind is a quarter of the
    host's 100.
  - Even with only fresh rows, the client recycles a piece that the host still shows: up to 256 host pieces live
    against 72 client actors over 3 kinds. That is a remaining gap, listed under "Open" below.

## Proposed fix (words only; no code until the senior answers)

All of it in `gib_c137.cpp`; no change to the codec or to card 140's pace code.

1. **No S row for a launch the host replaced or took back.**
   - Each HostGib remembers its waiting S slot.
   - When the pool relaunches a part whose S row is still waiting, the newer launch's S row overwrites that slot in
     place (new id, text and born time; same FIFO place). The replaced id gets no S row and no G row. Count:
     "replaced at a relaunch".
   - When a part with a waiting S row is gone (hidden root), its slot is cancelled: id 0, skipped at no cost, the row
     not live, no G row. Count: "cancelled gone". This means `gib_host_sample` checks gone before its
     `spawn_unsent` early return.
   - Result: waiting S rows can never exceed the live table rows (`kGibRows` 256), so a burst cannot fill the
     1024-slot queue. A relaunch of a part whose S row was already sent keeps today's G row.
2. **The class path within the dictionary.**
   - The S row carries the class's package path only (e.g. `/Game/Data/Gore/EnemyData/3DGore_Alien_Generic/BP_GoreData_NonSticky_Alien_Generic_Chitin07`,
     at most 72 chars for the r2 classes, inside `kPackDictLen`), so it is one dictionary def per connection and
     then a ref.
   - The client rebuilds "<package>.<last segment>_C" before `c74_find_asset`. This holds for all 9 r2 classes,
     whose class name is the package's last segment plus "_C".
   - The host sends the full path, as today, when the class name does not match that form. Counted; expected 0.
3. **Charge the wire, not the inline upper bound.**
   - `gib_cost` probes `pack_text` with a gib-owned PackSet, and `gib_append` commits to it.
   - The set is reset at map start and whenever the ponged PC count changes (a join or rejoin starts that
     connection's set empty). The charge stays an upper bound of every connection's wire.
   - S row cost: about 52 B (ref), or about 125 B the first time a class goes in the map.
4. **Late guard, no loss.** An S row popped more than 1000 ms after its launch is still sent. Its part's current pose
   (M, or R when still) goes in the same frame, so the client piece starts where the host's piece is now, not
   seconds back in its flight. Counted "sent late"; expected 0.
5. **Logs.** The card 140 dump line gains the cancel counts and keeps the peak against the cap. The window line is
   unchanged.

Budget after the fix: burst 1 leaves at most 256 fresh S rows (r2 table overflow 0). That is 256 x 52 plus 9 defs x
about 75, about 14 KB, within one 16384 window. The longest S wait becomes one tick after the burst frame, unless a
second burst lands in that same window. Then it is two windows at worst, under 2000 ms; 1000 ms is the prediction.

### Predicted log lines (one run, the c148 nuke route, client near)

Host dump (format: today's line plus the cancel counts):

    card140: host gib budget (quit): window peak <= 16384 of 16384 packed bytes, S recorded A, sent B, replaced at a relaunch C, cancelled gone D, sent late 0, waiting 0, lost 0, not sent at a full queue 0 (peak waiting <= 256 of 1024), longest S wait < 1000 ms; R queued unsent 0, not sent at a full queue 0 (peak waiting < 1024 of 1024)

The host dump numbers:

- A = B + C + D.
- C is about 60% to 75% of A for a nuke (r2: at least 680 of 1336 in burst 1).
- D is small (only parts taken back with the S row waiting under 1000 ms).
- The new line `card201: host gib class paths (quit): N sent as a package path, 0 sent in full`.

Host rest rows:

    card148: host gib rest rows (quit): X still, Y at the 10000 ms bound, Z gone, V at a pool relaunch; 0 with no ground below

with X + Y + Z + V = B. V now counts only relaunches after the S row was sent, so it falls far below r2's 1114.

Host totals: "samples deferred by the budget" well below r2's 841, since the S queue no longer holds the budget for
14 s.

Client:

    card148: client gib rows (quit): ... unmapped U (no row U1, no root 0), freed by a pool reuse F

The client numbers:

- 0 spawn failures (the rebuilt class paths resolve), 0 "class not found".
- F falls well below 991, but stays above 0 while the client pool is 24 per kind (Open, below).
- Rests within 2 uu equal to checked; 0 above the ground on both sides.

No `card140: host gib spawn queue full` or `rest queue full` WARN.

### Open

- The host pool's live size per kind. It is not logged; the config count is 100.
- The client pool (`kC42PoolMax` 24 per kind) is a quarter of the host's. After this fix the client can still
  recycle a piece the host still shows, a visible difference under the full-world-sync rule. Raising the client
  gib pool to the host's config count is a one-constant change in `enemies.cpp`, outside this card's file list.
  For the senior: in this card, or its own.
- The exact stale-row count for r2 (680 to 1114; no per-row log).

## Senior answer (implemented)

Items 1 to 5 approved as written. Replace in place for a relaunch while the S row waits, cancel for a part gone
while it waits, both counted. Package-path S rows with the client rebuild; a rebuilt path that does not resolve is
rejected (counted, first lines logged). Wire-cost charging through a gib-owned PackSet only if it provably stays in
step with every connection. Late guard as proposed, no budget change. Client gib pool `kC42PoolMax` raised to the
host's per-kind config count (100) in this card.

## Implementation notes

### How the charge stays an upper bound of every connection's wire

- The gib mirror (`GibHost::pack`) must hold only ids that every ponged connection's pace-queue PackSet holds (a
  subset). The connection set only grows between its resets (chan.cpp resets it when its pace slot is reallocated:
  a new connection or a rejoin); other ops' strings only add marks.
- The mirror is emptied at map start (`GibHost{}` in `c137_gib_arm_map`), whenever `c95_ponged_count` changes
  (checked at the start of every `gib_host_tick`), and after a fanout with `sent < to` (some ponged connection missed
  the frame's defs).
- A client rejoin is a drop (the count falls) and later a first Pong on the new map load (the count rises). Both are
  seen at tick starts, seconds apart (the rejoin loads the map). `c95_send_all` sends only to ponged PCs, so no gib
  frame reaches a rejoined PC before its count change emptied the mirror. Its new connection set starts empty, as
  the mirror does.
- Each frame is probed at flush from the mirror as it was at the frame's start (`frame_pack`), in one pass, the same
  way `wire_form` packs the frame against the connection set. With the connection set a superset, the real packing
  is no larger, and it is raw only if the probe is. A raw probe takes the frame's marks back (the wire carried no
  defs). A probe larger than the rows' charges is charged too (the true-up; the budget may dip below 0 and
  `spent_peak` shows it).
- The client decode state needs no reset: each def writes its dictionary slot.
- Residual: one PC drops and a different PC gets its first Pong in the same tick, so the count stays equal. The
  under-charge is then at most one def per class path (about 9 x 130 B), once. With two players this cannot happen.

### Tables sized from `kC42PoolMax`

None. It is used only in the pool-count clamp in `enemies.cpp` (`c42` pool init). The memory is game-side: each
kind's pool pre-allocates its gore part actors on first use, so 100 instead of 24 actors per kind, built lazily per
kind, the same count the host's own pools build from the config. The constant moved to `hooks_internal.h` so
`gib_c137.cpp` can log the client pool peak against it; `enemies.cpp` only lost its definition line.

## Run c201-r1 predictions

Route: `tools/scenarios/c201-r1.txt`, the c148-r2 nuke part only (two nukes on 4 WarriorBugs each, client 300 uu
from the host, both facing the field), then `objectives` dumps, then quit. c148-r2 also had three rounds of 3
WarriorBug kills first, so totals are smaller; the comparisons below are per mechanism, not per count.

Host (objectives and quit dumps):

    card140: host gib budget (quit): window peak P of 16384 packed bytes, S recorded A, sent B (late 0), replaced at a relaunch C, cancelled gone D, waiting 0, lost 0, not sent at a full queue 0 (peak waiting Q of 1024), longest S wait W ms; R queued unsent 0, not sent at a full queue 0 (peak waiting < 1024 of 1024)
    card201: host gib class paths (quit): N sent as a package path, 0 sent in full; wire charge: mirror resets X (PC count Y, send failed 0), frame rollbacks 0, path defs committed K, untracked 0, true-up bytes T
    card148: host gib rest rows (quit): s still, b at the 10000 ms bound, g gone, V at a pool relaunch; 0 with no ground below

- A = B + C + D (lost 0). C > 0 and C is the larger part of A for a nuke (about 60% to 75%). D small.
- Q <= 256 (the live table rows). W < 1000 (so late 0). P <= 16384 plus at most one frame's true-up.
- N = A (every class "<package>.<last segment>_C"). K <= 9 per mirror reset (one per class path).
- Y = 1 (the ponged count goes 0 to 1 once, at the first gib tick after the client's first Pong; the host quits
  first, so no drop is seen), X = Y = 1 (`c137_gib_arm_map` resets the struct, not counted). Send failed 0, frame
  rollbacks 0 (a gib frame is never raw: its numbers alone pack below its text).
- s + b + g + V = B. V falls far below c148-r2's 1114 (only relaunches after the S row was sent).
- `card137: host gib totals`: samples deferred by the budget below c148-r2's 841.

Client:

    card137: client gib totals (quit): B spawned, 0 spawn failures, ...; rest checked c, within 2 uu c, within 10 uu c, ...
    card148: client gib rows (quit): ... freed by a pool reuse F, empty frames 0, overflow 0 of 1536 rows; rest checked c, above the ground 0
    card201: client gib pools (quit): package paths rebuilt B, not resolved 0; rows in use at once per kind (...) k0 k1 k2 k3 k4 k5 of 100

- Spawned = host B; rebuilt = B; not resolved 0; 0 "class not found".
- F <= host V. Reason: the client pool now has the host's 100 per kind and gets the S rows in the host's launch
  order with no stale ones, so it recycles an actor only when the host's pool did (a relaunch after the S row went,
  which is V). c148-r2 had F 991 from 24 per kind and the stale S rows.
- Each kind peak <= 100 (a reuse frees the older row, so rows in use per kind cannot exceed the pool). The kind the
  nuke chunks use (non-sticky static mesh) reaches 100 when V > 0, since the host's pool of 100 wrapped; the other
  kinds stay below.

WARN classes:

- Host: c148-r2's minus the two queue-full classes, so only `WARN card132: host t...` (if the round timing hits), no
  `WARN card140: host gib spawn queue full` and no `WARN card140: host gib rest queue full`.
- Client: the same as c148-r2: `WARN card132: mismatch`, `WARN card152: client bone check`, `WARN card132: client
  round` (classes, not counts; c201-r1 drops the Part 1 kills, so some may not appear, which is not a new class).

## Report (run c201-r1, run 1 of 3)

### Files changed

- `mod/src/ubw/gib_c137.cpp`: replace in place and cancel for waiting S rows (`gib_host_retake`, `gib_spawn_queue`,
  `gib_spawn_cancel`; `gib_host_sample` checks gone before its `spawn_unsent` return; `gib_spawns_waiting` skips
  cancelled slots); package-path S rows (`gib_class_path`) and the client rebuild (`gib_client_path`, rejected and
  counted when the rebuilt path does not resolve); the wire charge through the gib-owned mirror (`gib_cost`,
  `gib_pack_commit`, `gib_frame_check`, `gib_pack_reset`, the ponged-count check in `gib_host_tick`, the fanout check
  in `gib_flush`); the late guard (`gib_spawn_try`); the client pool peak per kind (`gib_client_reuse`, `ClientGib::kind`);
  dump lines split into `gib_host_dump` and `gib_client_dump` (60-line rule) with the card 201 lines.
- `mod/src/ubw/hooks_internal.h`: `kC42PoolMax = 100` (moved from `enemies.cpp`), `c42_kind` declared.
- `mod/src/ubw/enemies.cpp`: the `kC42PoolMax = 24` definition line removed (its clamp now reads the header's 100).
  The totals line is not changed: the pool peak is logged from `gib_c137.cpp`.
- `tools/scenarios/c201-r1.txt`: new.
- This card.

Build: `cmake --build build -j4` in the 12G scope, /W4 /WX clean. DLL `71bbf9bdcedf679e` (sha256sum | cut -c1-16),
deployed to both lanes.

### Verification output

Drive tail:

    02:06:46 +  73.2s ok   line 57: host quit -> done
    02:06:46 +  73.3s ok   line 58: client quit -> done
    02:06:46 +  73.3s dead time: sleeps 34.0 s, timed-out try waits 0.0 s in 0 (none)
    02:06:46 +  73.3s scenario passed

Host (quit):

    card137: host gib totals (quit): 254 spawn rows, 2142 samples, 254 rest rows (43 at a pool reuse), 556 frames, 129397 bytes; samples deferred by the budget 450, replaced by a newer row 120, deferred unsent 0; R unsent (live) 0, overflow 0
    card148: host gib rest rows (quit): 159 still, 1 at the 10000 ms bound, 51 gone, 43 at a pool relaunch; 0 with no ground below
    card140: host gib budget (quit): window peak 16384 of 16384 packed bytes, S recorded 255, sent 254 (late 0), replaced at a relaunch 0, cancelled gone 1, waiting 0, lost 0, not sent at a full queue 0 (peak waiting 38 of 1024), longest S wait 10 ms; R queued unsent 0, not sent at a full queue 0 (peak waiting 48 of 1024)
    card201: host gib class paths (quit): 255 sent as a package path, 0 sent in full; wire charge: mirror resets 1 (PC count 1, send failed 0), frame rollbacks 0, path defs committed 37, untracked 0, true-up bytes 0

Client (quit):

    card137: client gib totals (quit): 254 spawned, 0 spawn failures, 2142 samples, 160 rest rows, 43 unmapped rows, bad rows 0, overflow 0; in flight worst 212.3 uu, median <= 20 uu, over 150 uu 1; rest checked 160, within 2 uu 159, within 10 uu 159, worst 59.9 uu, gone before the check 0
    card148: client gib rows (quit): 51 gone rows hidden, unmapped 43 (no row 43, no root 0), freed by a pool reuse 43, empty frames 0, overflow 0 of 1536 rows; rest checked 160, above the ground 0
    card201: client gib pools (quit): package paths rebuilt 254, not resolved 0; rows in use at once per kind (sticky sprite, mesh, skeletal; non-sticky sprite, mesh, skeletal) 0 20 0 51 100 0 of 100

WARN classes:

- Host: none (c148-r2: spawn queue full 120 lines, rest queue full 76 lines, `card132: host t` 1).
- Client: `WARN card137: client gib host part 245 STGorePartNonSticky_StaticMesh lies 59.9 uu from the host's rest
  point` (1). c148-r2 had `card132: mismatch` 8, `card152: client bone check` 4, `card132: client round` 2; none here.

### Predictions against the run

- Met: lost 0; no queue-full WARN on either side; peak waiting 38 (<= 256); longest S wait 10 ms (< 1000; c148-r2
  13116 ms); late 0; 255 of 255 sent as a package path, 0 in full; client rebuilt 254 = spawned 254, not resolved 0,
  spawn failures 0; send failed 0, rollbacks 0, untracked 0, true-up 0; mirror resets = PC count resets = 1;
  s + b + g + V = 159 + 1 + 51 + 43 = 254 = B; V 43 (c148-r2 1114); samples deferred 450 (c148-r2 841, all in the first
  window); client F 43 <= host V 43 (equal: every client recycle matched a host relaunch); kind peaks at most 100, the
  non-sticky static mesh kind at 100 (the host's pool wrapped too: V 43 > 0), the others below; above the ground 0.
- Missed: C (replaced at a relaunch) is 0, not "the larger part of A". The cheap S rows (refs after the first def)
  drained the queue within 10 ms, so no part was relaunched while its S row waited. The replace path ran 0 times in
  this run; the cancel path ran once (D 1). K 37 is above "<= 9 per reset": the nukes used more than 9 class paths
  (the 9 was the r2 count from its first burst, not a bound). A 255 is far below c148-r2's 1791: this route has no
  Part 1 kills, and nuke 2 made only 34 parts (it is smaller per shot, the bugs died partly before the shot).
- Missed: the client WARN classes are not c148-r2's. The card 132 and card 152 classes did not appear (they came
  from c148-r2's Part 1 kills, which this route does not have), and one new class appeared: the rest check of host
  part 245 (nuke 2) at 59.9 uu. See Problems.

### Problems

1. Client rest check, host part 245, 59.9 uu (1 of 160; the other 159 within 2 uu). Not diagnosed: no log line names
   what moved the piece after its R row snapped and slept it. Only R rows are logged per count, not per part. Candidate,
   not shown: a contact from another piece still flying, since the raised client pool (100 per kind) keeps more pieces
   on the ground; on the host a contact after the R row is not sent either, so the two sides may both have moved.
   Proving it needs a per-part line (the client's pose at the check vs. its pose at the R row and its awake state),
   which is outside this card's change. For the senior: in this card, run 2, or its own card.
2. The replace-in-place path is not exercised by this route (C 0). It needs a burst whose S rows wait longer than a
   pool relaunch (more than 100 parts of one kind in one window with the budget spent). Run 2 could fire the two
   nukes back to back (one window) to force it, if the senior wants it proven.
3. The first host window line after map start (`t=0: ... 16384 of 16384 bytes spent; waiting S 28`) reports a full
   window: `GibHost{}` starts with budget 0, so the first refill's line reads spent = 16384. This sets
   `spent_peak` 16384 in the dump. It is not caused by this card (c148-r2 has the same first line); the real
   windows of this run peaked at 16379. Not changed (outside scope).

Report complete.

## Senior answer to run c201-r1 (run 2 scope)

Not accepted on c201-r1: the run did not repeat the c148-r2 load. Run 2 (c201-r2) repeats the c148-r2 scenario shape
(Part 1: three rounds of 3 WarriorBug kills; Part 2: the two nukes) with the card 201 waits. Inside this card,
gib_c137.cpp only: a capped client line with the row's history for each rest miss (part 245 class), and the first
window after map start no longer reports a full window. No fix for the rest miss unless the new line names its cause.

## Run 2 changes (gib_c137.cpp)

- Rest miss line (client): `ClientGib` keeps its row's history: when the S row was applied, whether its first M row
  came right after its S row in one frame (the host's late S row carries its pose that way), the M rows applied, when
  the last M row and the R row were applied, and the piece's distance from the R point when the R row was applied
  (before the snap). A rest check over 2 uu counts a miss and logs (own cap, 8 lines):
  `card201: client gib rest miss host part <id> <name>: <d> uu from the host's rest point at the check; S row applied
  <ms> ms before the check, late pose <0|1>; M rows <n>, last M row <ms> ms before the R row; <d> uu from the R point
  when the R row was applied; body awake at the check <0|1|-1> (misses <n>)`.
  `last M row` is -1 with no M row; `awake` is -1 when the root is not a primitive. The card201 client pools line gains
  `rest checks over 2 uu <n>`. Late pose is read from row order: an M row directly after its own S row in one frame.
  A late S row whose S and M rows fall into two frames (the frame filled between them) reads 0; the host's `late`
  count is the cross-check.
- No static cause for part 245: a rest check runs 1 s after the R row teleports the piece and puts its root body to
  sleep. Both a contact from another piece and a pool relaunch could move it, but a relaunch frees the row before the
  check (`gib_client_reuse` frees rows in any state, so a relaunched piece is never checked). So that leaves a wake
  by contact, or a sleep that did not hold, and the line tells these apart (awake at the check, the distance before the
  snap). No fix in this run.
- Window peak: the first refill after map start ends no real window (`budget_ms` 0, `budget` 0), so its line reports
  0 bytes spent and the peak comes from real windows only.

Build: /W4 /WX clean. DLL `853951c74ca46986` (sha256sum | cut -c1-16).

## Why c148-r2 recorded 1791 S rows and c201-r1 255 (from the logs)

The count is the number of gib parts the game launched. `recorded` counts the outermost Impart calls; the mod's queue
never causes an Impart, so there is no feedback from the full queue. The difference is the bodies each nuke gibbed
(`card042: host Gib ... sent` lines within 2 s of each FireProjectile line):

| run | nuke 1 | nuke 2 |
|---|---|---|
| c148-r2 | 40 bodies (35 WarriorBug, 5 Morita), 20 of them dying in the blast | 5 (4 WarriorBug, 1 Morita) |
| c201-r1 | 10 bodies (4 WarriorBug, 6 Morita) | 1 WarriorBug |

c148-r2 fired nuke 1 86 s into the run (c201-r1: about 37 s). By then the map's own bug attack had been fighting the
Morita squad next to the players (16 HandleDying lines between the Part 1 kills: map bugs and Moritas), so the nuke hit
the Part 1 corpses, the map bugs and the Moritas. In c148-r2, nuke 1's burst recorded about 1500 S rows in one frame
(S ids 1088 to 1288 were not sent at the full queue 140 ms after the shot). WarriorBugs give more parts than Moritas.
So with Part 1 back, the expected count is c148-r2's. The map's AI is not scripted, so the bodies near the players at
nuke 1 vary.

## Run c201-r2 predictions

- S recorded about 1800 (range 1200 to 2000: the bodies nuke 1 meets vary with the map's fight), in nuke 1's burst
  mostly.
- Not sent at a full queue: 0 for S, 0 for R. No queue-full WARN.
- Replaced at a relaunch: hundreds (c148-r2 had 1114 pool relaunches of 1479 sends): the burst launches more parts in
  one frame than the game's pools hold, and every relaunch of a part whose S row waits is replaced in place. So
  recorded = sent + replaced + cancelled + waiting with lost 0, and sent well below recorded (about the parts the pools
  hold at the burst's end plus the parts launched later).
- Peak waiting at most 256 of 1024. Each waiting S row belongs to a live host table row (kGibRows 256), because a
  relaunch replaces the row's slot instead of adding one. A cancelled slot holds its place until it reaches the queue
  head, so the peak can exceed 256 by the cancelled slots still in the queue at that moment (c201-r1: 1 cancel).
- Longest S wait under 1000 ms, or every S row waiting over 1000 ms is counted late and goes with an M pose row. The
  burst's S rows cost their package-path refs after each class's first def; c201-r1 sent 254 in 10 ms, so a
  256-row queue fits in about one window.
- 0 sent in full (every gib class is "<pkg>.<seg>_C"); client rebuilt = client spawned, not resolved 0.
- Client pool peak per kind at most 100, spawn failures 0.
- Rests: within 2 uu = checked, or every miss has its `card201: client gib rest miss` line (misses at most 8 lines,
  the count in the pools line).
- Above the ground 0 on both sides.
- Window peak at most 16384, from real windows. The first window line reads `0 of 16384 bytes spent`.
- Host WARN classes: c148-r2's minus the two queue-full classes, so only `card132: host t` (1 in c148-r2) if it
  recurs.
- Client WARN classes: c148-r2's (`card132: mismatch`, `card132: client round`, `card152: client bone check`), plus
  the card 137 rest WARN only if a miss over 10 uu fires again.

## Report (run c201-r2)

### Files changed

- `mod/src/ubw/gib_c137.cpp`:
  - The client rest miss line (`card201: client gib rest miss`, own cap `kGibMissLines` 8) and the row history in `ClientGib`.
  - `rest checks over 2 uu` in the card201 client pools line.
  - The first window after map start reports 0 spent.
- `tools/scenarios/c201-r2.txt`: the c148-r2 shape (Part 1: three rounds of 3 WarriorBug kills; Part 2: the two nukes), with the card 201 log waits and the two objectives waits.

Build: /W4 /WX clean. DLL `853951c74ca46986`, deployed on both lanes (`deployed: 853951c74ca46986`). The scenario passed (123.8 s, exit 0).

### Summary

The c148-r2 load came back: 1778 S rows recorded (c148-r2 1791). The fix holds under it:
- 0 not sent at a full queue, for S and for R.
- Peak waiting 213 of 1024 (S) and 214 (R).
- Longest S wait 757 ms, 0 late.
- Lost 0. 0 sent in full.
- Client not resolved 0.

The ledger balances: 770 sent + 1006 replaced + 2 cancelled + 0 waiting = 1778.

Every rest check was within 2 uu (394 of 394), and 0 pieces were above the ground on either side. The part 245 class did not recur, so the new miss line never fired (0 misses) and names no cause. The first window line reads 0 spent; the 16384 peak comes from real windows (nuke 1 16382, nuke 2 16384).

### Verification output

Host (`logs/c201-r2-host.log`, objectives command):
```
card140: host gib budget (objectives command): window peak 16384 of 16384 packed bytes, S recorded 1778, sent 770 (late 0), replaced at a relaunch 1006, cancelled gone 2, waiting 0, lost 0, not sent at a full queue 0 (peak waiting 213 of 1024), longest S wait 757 ms; R queued unsent 0, not sent at a full queue 0 (peak waiting 214 of 1024)
card201: host gib class paths (objectives command): 1778 sent as a package path, 0 sent in full; wire charge: mirror resets 1 (PC count 1, send failed 0), frame rollbacks 0, path defs committed 41, untracked 0, true-up bytes 0
card137: host gib totals (objectives command): 770 spawn rows, 3841 samples, 767 rest rows (253 at a pool reuse), 1206 frames, 298746 bytes; samples deferred by the budget 710, replaced by a newer row 276, deferred unsent 0; R unsent (live) 3, overflow 0
card148: host gib rest rows (objectives command): 394 still, 0 at the 10000 ms bound, 120 gone, 253 at a pool relaunch; 0 with no ground below
card137: host gib window t=0: 0 samples sent, 0 deferred, 0 replaced, 0 of 16384 bytes spent; waiting S 1, R 0
card137: host gib window t=110172609: 4 samples sent, 123 deferred, 0 replaced, 16382 of 16384 bytes spent; waiting S 160, R 214
card137: host gib window t=110192825: 51 samples sent, 68 deferred, 0 replaced, 16384 of 16384 bytes spent; waiting S 0, R 36
```
Client (`logs/c201-r2-client.log`, objectives command):
```
card137: client gib totals (objectives command): 770 spawned, 0 spawn failures, 3842 samples, 394 rest rows, 253 unmapped rows, bad rows 0, overflow 0; in flight worst 566.1 uu, median <= 20 uu, over 150 uu 248; rest checked 394, within 2 uu 394, within 10 uu 394, worst 0.0 uu, gone before the check 0
card148: client gib rows (objectives command): 120 gone rows hidden, unmapped 253 (no row 253, no root 0), freed by a pool reuse 253, empty frames 0, overflow 0 of 1536 rows; rest checked 394, above the ground 0
card201: client gib pools (objectives command): package paths rebuilt 770, not resolved 0; rows in use at once per kind (sticky sprite, mesh, skeletal; non-sticky sprite, mesh, skeletal) 0 100 0 54 100 0 of 100; rest checks over 2 uu 0
```

### Predictions against the run

| prediction | result |
|---|---|
| S recorded about 1800 (1200 to 2000) | 1778 |
| not sent at a full queue 0, S and R | 0, 0 |
| replaced in the hundreds; recorded = sent + replaced + cancelled + waiting, lost 0 | 1006; 770 + 1006 + 2 + 0 = 1778; lost 0 |
| peak waiting at most 256 (plus cancelled slots) | 213 (S), 214 (R) |
| longest S wait under 1000 ms, or late rows with a pose | 757 ms, late 0 |
| 0 sent in full; client not resolved 0 | 0; 0 (770 rebuilt = 770 spawned) |
| client kind peak at most 100, spawn failures 0 | peaks 0 100 0 54 100 0; failures 0 |
| rests within 2 uu = checked, or a miss line each | 394 = 394; misses 0, no miss line |
| above the ground 0 both sides | client 0; host 0 with no ground below |
| window peak from real windows; first window 0 spent | first window `0 of 16384`; peak 16384 from nuke 2's window |
| host WARN classes = c148-r2 minus the queue-full classes | met: only `WARN attach: DisableThreadLibraryCalls failed` (also in c148-r2); `card132: host t` did not recur |
| client WARN classes = c148-r2's | met, see below |

Client WARN classes:
- `card132: client round` (2)
- `card132: mismatch` (12): BP_AISpawner_C 3, BP_AISpawnPoint_C 6, BP_Dropship_LandingBlocker_C 3, NiagaraActor 1
- `card152: client bone check` (2)
- `attach: DisableThreadLibraryCalls failed` (1)

No card 137 rest WARN.

The classes are c148-r2's. Inside the mismatch class there is one actor class c148-r2 did not show: `NiagaraActor_14` at about (223200, 57400, 15550), with a host/client offset of about 1050 uu, "no row sent", in round 2 only. The same actor shows the same mismatch in c152-r1 (`logs/c152-r1-client.log`), so it is not new to this card and no gib path touches it: it is a placed NiagaraActor high above the map, not a gib. Both runs had two mismatch rounds: c148-r2 rounds 3 and 4 had 4 mismatches each. c201-r2 round 3 had 4 (the same set), and round 2 had 9 (that set doubled plus NiagaraActor_14); the cause of the extra round-2 rows is not traced here (card132 scope).

The 8 `card148: client gib G row for host part N unmapped` lines are info lines, at their cap. The same 8 lines appear in c148-r2 and c201-r1.

### Part 245 (rest miss)

It did not recur: 394 of 394 rests were within 2 uu, worst 0.0 uu, so the history line never fired. There is no cause to name from this run. The line stays in place for run 3.

### Problems

- Part 245 is unreproduced, so the rest miss diagnosis is still open.
- `NiagaraActor_14` is a card132 mismatch outside this card's scope. It is pre-existing (c152-r1); not carded here.

Report complete.

## Senior review (2026-11-12)

Accepted on c201-r2 (DLL `853951c74ca46986`). Checked by me: the diff against the pre-card snapshot touches only
`gib_c137.cpp`, `hooks_internal.h`, `enemies.cpp` (the `kC42PoolMax` line) and the two scenarios; no function over 60
lines; host quit lines read S recorded 1778 = sent 770 + replaced 1006 + cancelled 2, lost 0, not sent at a full queue
0 for S and R, peak waiting 213 and 214 of 1024, longest S wait 757 ms, late 0, 0 class paths sent in full; client
770 rebuilt, 0 not resolved, 0 spawn failures, 394 of 394 rests within 2 uu, 0 above the ground. Host WARN classes:
the attach line only. Client WARN classes: those of c148-r2 (NiagaraActor_14 in the mismatch class is card 164's).

Not proven: the late path (0 rows waited over 1000 ms); the mirror reset on a failed send (0); the c201-r1 rest miss
(part 245, 59.9 uu, 1 of 160) did not recur and has no cause; its history line (`card201: client gib rest miss`)
stays in and is a card 127 watch item. Client pools reached 100 of 100 on two kinds, equal to the host's pool size.
