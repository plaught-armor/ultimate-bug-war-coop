# Card 187: A client that joins while a corpse is still moving follows it

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: queued 2026-11-11, from card 152 (c152-r3 window 2). Budget: three runs, shared build and run with card 186.

Read: `docs/cards/done/152-ragdoll-fall-stream.md` (r3 prediction 7, problem 4), `mod/src/ubw/pose_c132.cpp`
(`pose_wait_gate`, `pose_wait_tick`, the journal replay of pose rows).

## Why
In c152-r3 window 2, two corpses (0x160, 0x226) were still moving on the host when the client rejoined. The client
held its copy's game ragdoll start for a stream row, got none for 2000 ms, and fell back to its own ragdoll with a
WARN. The copy then lies where the client's physics put it until the next snap.

## Do
1. Trace first, in this card: what the replay sends for a row that is streaming on the host at join (which flags,
   which sample), whether the host's stream send loop includes the new PC from its next step, and where the client
   decides to hold the start. Mark each line verified or open.
2. Fix at the cause the trace names. Preferred: the replay sends a streaming row's latest sample as a stream sample,
   and the new PC is in the stream from the next step, so the wait ends through the normal path. Only if the trace
   rules that out: no start hold for a copy whose row came only through the replay.
3. Replays set state: nothing one-shot is re-run for the joining client.

## Trace (Do 1, from c152-r3 and the code; no run)
- Verified, the replay: `c132_pose_replay` (pose_c132.cpp:1979) sends only rows in `PoseState::Sent`, each as its
  last snap with that snap's flags. A streaming row is `Moving`. It is skipped without a log and without being counted
  as owed. At 97072121 the replay line reads "12 rows (14 tracked) ... 0 owed": the 2 rows left out are 0x226 and
  0x160, the only rows streaming in that window (samples at 97070020 and 97070851, "to 0 PCs").
- Verified in code, the send loop: `pose_stream_one` sends through `pose_send_blob(..., nullptr, world)`, which is
  `c95_send_all`. That function sends to every server-copy PC whose C50 copy has `ponged`, so a joining PC gets
  every stream sample and snap from its first Pong on. Open in the logs: the r3 run cannot show it, because sample
  lines are logged only for samples 0 and 1, and both were sent before the Pong.
- Verified, where the client holds: `pose_wait_gate` (pose_c132.cpp:399) holds the game's ragdoll start for a copy
  with no row whose death edge ran (`c38_is_marked`), or for a streamed row not yet posed. `pose_wait_tick` lets the
  copy's own ragdoll start after kPoseStartWaitMs (2000 ms). r3 held 0x160 and 0x226 at 97073344 ("no stream row
  yet") and fell back at 97075350.
- Verified, why no row ever came: the scenario's `host travel next` ran 1.4 s after the client's pawn state. The host's
  LoadMap totals line is at 97073506, before the client's fallback, so nothing more could be sent for those rows.
- Verified, the r3 part-3 kills streamed nothing: those bugs were spawned while the client was away, so they had no
  NetGUID (guid 0 rows do not stream). A rejoin test needs bugs spawned while the client is connected.
- Verified, the client accepts a stream sample with no prior row: `pose_part` creates the row and
  `pose_stream_part` sets `streamed`, then `pose_try_apply` starts the stream.
- Timing (r3): the host spawned the new BP_PlayerController_C at 97069415; the client's pawn arrived at 97071810; the
  first Pong was at 97072049 to 97072120. A corpse killed at the host's PC spawn line must still be moving about
  2.7 s later. In r3, streams ran up to sample 24 (about 6 s) on the slope.

- Verified in code, the guid: a row's guid is read by `pose_guid_check` (`c41_guid` on the row's C31Weak actor), which
  `pose_host_row` calls on every host step and `c132_pose_replay` calls for each row it sends. A guid-0 row gets its
  guid on the first step after the corpse has a NetGUID (the joining connection replicates it), and `stream_due`
  then lets it stream. No re-resolve fix is needed; the replay also calls `pose_guid_check` for the moving rows it
  now sends.

## Change
- `pose_stream_emit` is the shared encode-and-send of a row's bodies now as one stream frame, to one PC or to all
  ponged PCs. `pose_stream_one` uses it for the stream; the replay uses it for the joining PC.
- `c132_pose_replay`: a `Moving` row that is streaming (guid set, at least one sample sent) sends its bodies now to
  the joining PC only, labelled with its last sample's number. The row's seq, stream time and counters do not
  change; the stream's next sample goes to every ponged PC, this one included. New host line "card152: host replay
  stream guid 0x... sample N to the joining PC", and the replay line counts them ("card152 N moving rows sent their
  current stream sample").
- Scenario part 3 (`tools/scenarios/c186-r1.txt`): the host waits for the joining PC's spawn line, warps just above
  the slope, spawns three WarriorBugs at z 9600 and kills them in the air at once. The drop stays under kPoseFellUu
  (1000 uu from the first sample), so the fell check does not put them to sleep. Corpses on this slope moved 1.7 to
  2.3 s after settling starts in r3; the fall adds about 1.3 s, so they should still move at the first Pong (about
  2.6 s after the PC spawn).

## Run c186-r1 predictions (one run, shared with cards 186 and 188)
- At least 2 "card152: host replay stream guid" lines, all at the first Pong's replay, and the replay line counts
  the same number of moving rows.
- On the client, those guids each log "card152: client stream start ... (live)" and "card152: client stream
  final"; 0 "no stream row for guid" WARN lines and 0 wait fallbacks in the ragdoll wait map line for these copies.
- Digest rounds after: 0 pose mismatches.
- If the corpses settle before the Pong (the replay line counts 0 moving rows), the run does not prove the change;
  that is reported as a scenario timing miss, not a pass.

## Run c186-r1 outcome (reported to the senior)
- 1 replay line (0x2d8), and its client stream start and final followed. The other 2 corpses landed before the Pong
  (landings 98757659 to 98758441, Pong 98758645) and the game's re-ragdoll sent unfreeze snaps, so they were `Sent`
  rows at the replay, not `Moving`.

## Change for run c186-r2
- Scenario `tools/scenarios/c186-r2.txt` part 3: after the PC spawn line, the host also waits for
  "card128: host ... timeout now 20.0 s (state 3", the new connection's timeout set in state 3. In r1 that line came
  955 ms after the PC spawn line. The r1 warp already followed the pawn spawn at +791 ms (the host thread logged
  nothing from 98755468 to 98756244), so the kills move about 164 ms later. No later host join line exists before the
  Pong: the next is the Pong itself. The spawn height stays at z 9600 (under kPoseFellUu). No sleeps.
- No code change for card 187.

## Run c186-r2 prediction
- At least 1 "card152: host replay stream guid" line at the first Pong, the replay line counting the same number,
  each followed by the client's stream start (live) and final, and 0 "no stream row for guid" WARNs for them. Two or
  more only if the 164 ms shift keeps a second corpse moving at the Pong; r1's earliest landing was 986 ms before
  it, so count=1 is the likely result.
- Digest rounds after: 0 pose mismatches.

## Files
`mod/src/ubw/pose_c132.cpp`, `tools/scenarios/c186-r1.txt` and `c186-r2.txt` (shared with 186 and 188), this card.

## Senior review (2026-11-11)

Accepted on c186-r2, DLL `50615a859586a9ad`. `pose_replay_stream` sent the current sample of both Moving rows to
the joining PC (guids 0x248, 0x252); the client started both streams from it, 0x252 reached its settled final,
0 fallbacks, 0 "no stream row" WARNs. Not shown: a digest round after the join (the travel came first), and the
scenario's own kills, which the game re-ragdolled in the air so they were Sent rows at the replay and took the
snap path (same as 0x314 in r1, end state right there). Scenario lines 34 and 77 timed out because AI troopers
killed or shot the target bugs; a later run that reuses this scenario removes the troopers first.
