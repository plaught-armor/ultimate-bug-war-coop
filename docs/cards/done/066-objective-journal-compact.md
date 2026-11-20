# Card 066: The objective journal never fills up

Status: sent 2026-11-05 09:11 to junior ultimate-bug-war-coop-bb, after card 047 was accepted. Ready since 2026-11-05 (senior, refreshed after card 070). Low priority; it matters only for a client that joins late in a long stretch with no checkpoint.

Read `docs/STATE.md`, then the card 049 and 053 Reports and the card 058 WARN note. Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why

Card 058 filled the host's 128-frame objective journal (`kC49JournalCap`, `mod/src/hooks.cpp`) during the two ranges before the first checkpoint. It logged `WARN card049: host journal full (128 frames): later frames are sent live but not recorded`. A client that is connected keeps up through the live frames. A client that joins after the journal is full gets the first 128 frames and nothing after them, so its objective list is stale until the next checkpoint. A larger cap only moves the cliff.

## Refresh after card 070 (senior, 2026-11-05)

- Card 070 put a `ChanOp::ObjectiveActors` names frame in front of each Add frame that has actors, and `c49_emit` records the pair only while there is room for both (`n + 1 < kC49JournalCap`). The journal now fills about twice as fast. Card 070's c70f late join replayed 40 of 40 frames after one checkpoint.
- The snapshot already emits a names frame from `USTLevelObjective+0x88` for each objective (`card070: host ... snapshot <id> actors <n>`). The compaction gets those frames for free, because it reuses `c49_snapshot_frames`. Check that the names frame and its objective frame stay adjacent across a compaction: a names frame left as the last journal entry before the rebuild would hit the client's `c70_take` id mismatch WARN.
- Recount the cap limit: with names frames, a FED_C1M1 snapshot is larger than the 33 to 50 frames given below. Measure it from a c70 host log (the `snapshot` lines plus the journal count), and pick the test cap from that. The cap must be at least the snapshot size plus 8.
- Proof route for the late join: `tools/scenarios/c70-late-join.txt` (STAGGER=190) already has the client join after the first checkpoint. For this card the client must join before any checkpoint, so make a variant that plays the assault course and the first range, then joins.

## Do

1. When `c49_record` finds the journal full, rebuild the journal in place from the host's objective map. Build it exactly as `c49_snapshot` does after a checkpoint: `SnapshotBegin`, the chapter frame, one frame per objective (`c49_snapshot_frames`), `SnapshotEnd`. **Record only: send nothing live.** Connected clients already have this state, and a live snapshot would make them rebuild their list for no reason.
   - Split the record and send halves of `c49_emit`, or add a record-only path, so the snapshot code serves both callers. Do not copy it.
   - Ordering (senior, static, 2026-11-04): `c49_mirror` runs "after each library call's original", so the host's map already includes the triggering call. The snapshot covers it, so do not record that frame as well (an Increment counted twice raises a late client's count by 2). Check this once at run time: log the count in the snapshot frame next to the call's count.
   - Re-entry: the snapshot frames go through `c49_emit` and so through `c49_record`. Start the compaction from `c49_emit`, not inside `c49_record`, and guard it, so that a full journal during the compaction cannot start a second one.
   - `c49_snapshot` takes the objective subsystem and `c49_emit` only has a world context. Get the subsystem from the world the way the existing code that finds it does (grep the `c49_objectives` callers); do not add a new RVA for it.
   - If the snapshot itself fills the journal (more objectives than the cap allows), log one WARN and keep the old behaviour.
   - Log one line per compaction: the frames before and after.
2. Proof, with the card 063 runner:
   - Run with a test cap of 64, behind `UBWCOOP_C66_CAP=64`. Leave the default at 128. Do not go lower: in past logs, a FED_C1M1 snapshot of 14 to 16 objectives is 33 to 50 frames, so a cap below about 56 only reaches the overflow WARN. Pass when the log shows at least one compaction before the client joins.
   - The host plays through the assault course and the first range alone. The client then joins.
   - Pass: the client's objective list matches the host's (dump both, as card 053 does), with every count equal.
   - Do one run with the client connected throughout, to show that a compaction does not change a connected client's list or counts.

## Done when

- Both proof runs pass. The Report shows the compaction lines and both list dumps.
- With `UBWCOOP_C66_CAP` unset, the cap is 128 and the card 058 route logs the same frames as before, plus the compaction line.
- `logsum` gives ERROR/FATAL 0, both games end with rc 0, and there is no new dump.
- fnlen reports no function over 60 lines.

## Report (junior, 2026-11-05)

Result: pass. Both proof runs pass at `UBWCOOP_C66_CAP=64`. The run with the cap unset sends the same live frames and does not compact (see the limit below). ERROR/FATAL 0 and rc 0/0 in all three counted runs, no new dump, fnlen 0 over 60.

### Code (`mod/src/hooks.cpp`, `config.cpp`, `cmdfile.cpp`, `tools/drive.py`)

- `c49_snapshot_journal(world, v)` is the one builder: journal reset, then SnapshotBegin, the chapter frame, `c49_snapshot_frames`, SnapshotEnd. `c49_snapshot` (checkpoint) and the compaction both call it, so nothing is copied.
- `c66_compact(world_context, c)` runs first in `c49_emit`. It acts only on the host, only for library calls (`kind < SetCount`), and only when the call's frames would not fit (`n + 2 > cap`, a names frame plus its Objective frame).
  - The subsystem comes from `c49_subsystem(world_context)`, the existing lookup. No new RVA.
  - It saves the old journal, sets `record_only` (`c49_emit` then records and sends nothing), builds the snapshot, and logs `card066: host t=... journal compacted at <kind>: <before> frames before, <after> after (<objectives> objectives, cap <cap>)`.
  - Re-entry: `building` is set for the whole snapshot build, so frames emitted during the build cannot start a second compaction.
  - It returns true ("covered"). `c49_emit` then sends the triggering call live but does not record it, nor its names frame.
- Ordering check at run time: `c66_log_trigger` logs the triggering objective's state as it is in the snapshot, next to the call's count.
- Names adjacency: a names frame is recorded only while `n + 1 < cap`, so the journal never ends on one. The compaction checks room for both frames before it rebuilds. No `c70_take` mismatch WARN on either client.
- Overflow: if the snapshot drops frames or leaves less than 2 free, `c66_restore` puts back the old journal, logs one `WARN card066: host journal compaction failed (...)`, and sets `compact_off` until the next checkpoint or map (the old behaviour from then on). This was not reached at cap 64; the snapshot is 51 to 54 frames.
- `g_c49_cap` comes from `config::read_c66_cap()`, reading `UBWCOOP_C66_CAP` in the range 32 to 128. The default is `kC49JournalCap` (128).
- Test aid: the runner command `<role> objectives` (`cmdfile::Op::Objectives`, then `c66_dump`) logs the panel items (`c49_item_lines`) and one line per objective: `card066: <side> objective <id>: parent <p>, complete <c>, count <cur> of <target>`.

### Measured snapshot size

With card 070 names frames, a FED_C1M1 snapshot of 16 objectives is 51 frames at the first range's targets and 54 at its end (more objectives carry actors by then). That is above the card's 33 to 50 estimate. Cap 64 still fits it: 54 + 8 = 62, which is at most 64.

### Scenarios

Both scenarios are generated from `tools/scenarios/c65-range.txt` by the scratchpad script `gen66.py`.

- `tools/scenarios/c66-late-join.txt`: the host alone plays the assault course and the first range. Its waits use the `card049: host t=... <Kind>` mirror lines, because "host sent" lines appear only when a client is connected. It then waits for the compaction line, dumps, and lets the client join (`UBWCOOP_TRAVEL_DELAY_MS=360000`). It then waits for the replay and dumps both sides.
- `tools/scenarios/c66-connected.txt`: c65-range with host and client `objectives` dumps after wave 1, after kill 3, and at the end.

"Before any checkpoint": the only Save_CheckPoint before the join is the flag-1 one at map start. It comes before any objective exists and does not reset the journal. The only reset is `c49_snapshot` (SetObjectiveMapPostSave), and it did not run before the join.

### Runs

| Run | Setup | Result |
|---|---|---|
| c66a | late join, cap 64 | Environmental failure. An amdgpu ring gfx timeout ("Illegal opcode in command stream", GameThread vkd3d_queue) at 09:19:55 reset the GPU. Both games died (host rc 3, client rc 1) and left 2 dumps (UECC-Windows-621D2DF5..., UECC-Windows-78A82DF5...). Not the mod. |
| c66a2 | late join, cap 64 | Environmental failure. Xwayland restarted at 09:23:46 ("failed to create drawable"), so there was no log after 91 s. |
| **c66a3** | late join, cap 64 | **Pass.** 403 s, rc 0/0, ERROR/FATAL 0, host WARN 1 (the autodropship line), no new dump. |
| **c66b** | client connected throughout, cap 64 | **Pass.** 188 s, rc 0/0, ERROR/FATAL 0, WARN 0/0, no new dump. |
| **c66c** | client connected throughout, cap unset (128) | **Pass.** 184 s, rc 0/0, ERROR/FATAL 0, WARN 0/0, no new dump. |

### c66a3: compaction before the join, then a late join

```
card066: host t=41930272 journal compacted at SetObjectiveVisibility: 63 frames before, 54 after (16 objectives, cap 64)
card066: host trigger SetObjectiveVisibility(TacSupport_SO0) count 0; in the snapshot: found, complete 0, count -1 of -1
card049: host t=42042223 journal replay to BP_PlayerController_C (first Pong of the map): 56 of 56 frames in 0.196 ms
card053: client objective map matches the snapshot (set and order): no rebuild
```

The client joined 112 s after the compaction. Two more frames were recorded in that time (54 + 2 = 56). Final dumps: both sides report `ObjectiveListItems 16, box slots 3, objectives 16`.

The 16 objective lines (id, parent, complete, count) are identical on host and client, including `ShootingRange_SO3 ... complete 1, count 10 of 10`. The panel item lines are identical except for one field: `DescriptionPanel` render opacity is 1.00 on the host and 0.00 on the client, for the ShootingRange items. Visibility, slots, and every other widget field match. That opacity is a fade-animation state, not a list or count. The same `DescriptionPanel 4/0.00` appears in older runs on both sides (card049r, card049x9, card053x, c70b, c73a), so it predates this card.

### c66b: compaction with the client connected

```
card066: host t=42315398 journal compacted at IncrementObjectiveCount: 63 frames before, 51 after (16 objectives, cap 64)
card066: host trigger IncrementObjectiveCount(ShootingRange_SO3) count 0; in the snapshot: found, complete 0, count 6 of 10
card049: host sent IncrementObjectiveCount(ShootingRange_SO3) flags 0x0 count 0 to 1 pc(s); journal 51
```

- The trigger is the 6th `IncrementObjectiveCount(ShootingRange_SO3)` the host sent, and the snapshot already holds count 6. The snapshot therefore covers the trigger. The journal stays at 51 after the trigger is sent, so the trigger was not recorded twice.
- Nothing else was sent during the compaction. The only `host sent` line at t=42315398 is the trigger itself. Around it the client logs only the normal replay: `replayed IncrementObjectiveCount(ShootingRange_SO3) ...: objectives 16 -> 16`. There was no snapshot, no rebuild, and no card053 line.
- Dumps came before the compaction (wave 1, kill 3) and after it (end). Each dump is identical host to client: all 16 objective lines and all 18 panel item lines, opacity included. The end dump shows `ShootingRange_SO3 complete 1, count 10 of 10` on both sides.

### c66c: cap unset

- The cap is 128. The host sent the same 60 objective frames as c66b, in the same order, with the same kinds, ids, flags, and counts (`diff` of the `host sent ...` sequences is empty). Only the journal numbers differ.
- The journal ended at 73 frames with no compaction and no full WARN. The final dumps match host to client, and they match c66b's final dump.
- Limit, stated plainly: the scripted route is the assault course plus the first range. It does not reach 128 frames, so no compaction line appears at the default cap. Card 058's 128 came from a manual rehearsal through two ranges, and no runner scenario exists for the second range. The compaction itself is the same code at any cap; only `g_c49_cap` changes. A cap-128 compaction line needs the second range scripted, or a playtest.

### fnlen

`hooks.cpp` 1391 functions, `cmdfile.cpp` 17, `config.cpp` 44: 0 over 60.

## Senior review (2026-11-05 09:50): accepted

- Checked c66a3, c66b and c66c myself. All three drives end with `scenario passed`. logsum gives ERROR/FATAL 0. The only WARN is the known solo `autodropship` line, logged before the late join. There is no card066 or card070 WARN.
- The build at 09:11:50 contains the hooks.cpp from 09:11:41, and it was deployed at 09:13:35, before all three runs. fnlen: 1391 functions, 0 over 60 lines.
- The two 09:20 dumps come from c66a. The kernel log shows the amdgpu `ring gfx_0.0.0 timeout` and ring reset at 09:19:55, so they are environmental, the same as c70c.
- Code read (`c49_emit`, `c66_compact`, `c66_restore`):
  - `c49_mirror` runs after the library call's original, so the snapshot already holds the triggering call. Not recording that call is correct, and c66b's count 6 of 10 confirms it at run time.
  - On a failed snapshot, `c66_compact` returns false. The trigger then takes the old path: a full journal drops the frame with the existing WARN.
  - `record_only` keeps connected clients from receiving the rebuilt journal.
  - `building` blocks re-entry while the snapshot is built.
  - The names-frame rule (`n + 1 < cap`) still holds after a compaction.
- The cap-128 limit is accepted. The compaction is the same code at any cap, and c66c shows the default cap sends the same 60 live frames as c66b.
- The `DescriptionPanel` opacity difference predates this card and is a fade state, not a list mismatch. No follow-up.
