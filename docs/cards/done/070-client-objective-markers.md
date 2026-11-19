# Card 070: Give the client's replayed objectives their marker actors

Status: sent 2026-11-05 04:02 to junior ultimate-bug-war-coop-bb, after card 072 was accepted.

Read `docs/STATE.md` ("The client minimap shows no objective icons"), then the card 068 Report and the card 049 code in `mod/src/hooks.cpp` (`c49_after`, `c49_emit`, the client replay, `c49_snapshot_frames`). Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

Card 068 found that the client's minimap shows no objective icons because the card 049 replay calls `AddObjective` / `AddSubObjective` with an empty actor array. The host's call gets the level actors (`actors 1` in the log), and the game makes one `ASTLevelObjectiveMarker` per actor through `CreateASTLevelObjectiveMarker` 0x05150E20. The client's replay makes none, so nothing reaches `ShowOnMinimap`. Everything after the marker already works on the client.

## Design

Let the game make the markers itself: pass the same actors to the replayed call.

- **Host:** when `c49_after` records a library call, read its `TArray<AActor*>` (`actors`, already passed into `C49Call`) and send each actor's name. Level-placed actors have the same name on both machines; card 065's `c64_find_actor(world, name, Vec3{})` already resolves them this way. Do not change the existing objective frame layout. Send the names in a new frame that follows the objective frame on the channel, holding the objective name and up to N actor names, where N is what fits in `kChanTextMax`. Log a WARN and send what fits if there are more.
- **Client:** keep the names that arrive for an objective, and when that objective's frame is replayed, resolve each with `c64_find_actor`, build a `TArray<AActor*>` of the actors found (a stack array with data, count and max, as the game's ABI expects), and pass it in place of the empty array. Log one line per replayed call: names sent, actors found. An actor that does not resolve is left out with a WARN that names it.
- **Journal:** the new frame goes into the card 049 journal next to its objective frame, so a late joiner gets it too. A checkpoint snapshot (`c49_snapshot_frames`) has no library call to read the actors from. Find where the host's objective keeps them (the marker, or a field on `USTLevelObjective`; read `CreateASTLevelObjectiveMarker` and `ObjectiveUpdated` 0x051614C0 for what they store). If you cannot find them in a few hours of static work, keep the names the host sent at Add time in a table keyed by objective name and resend them from there. State in the Report which one you used.
- Order: if the names frame can arrive after the objective frame is already replayed, the replay is too early. Send the names frame first and replay on the objective frame, so the client always has the names when it replays.
- This runs in every client session, not behind a log flag. Keep the existing card 068 log detours (behind `UBWCOOP_C68_LOG=1`) for the proof.

## Do

1. Implement the above. Count each name's bytes against `kChanTextMax` once, in one place.
2. Prove it with the card 068 scenario (`tools/scenarios/c68-minimap.txt`), with `UBWCOOP_C68_LOG=1`:
   - The client's `ObjectiveUpdated` / `ShowOnMinimap` counts from `CreateASTLevelObjectiveMarker` (caller exe+0x5150FB0) match the host's within 1, and the client's `first-marker` shot shows the same objective icons as the host's.
   - The client does not make two markers for the one objective that its own level script also adds (card 068 saw `AddSubObjective(Assault_SO0)` from `FED_C1M1_Scripting_C` 26 s later). Log the client's marker count for that objective. If it makes a second marker, report it with the log lines; do not fix it in this card unless the fix is one guard.
3. One late-join run: the host passes the first checkpoint, then the client joins. The client's minimap shows the current objective's icon.

## Done when

- Both runs pass and the shots are in the Report, host and client side by side.
- `logsum` gives ERROR/FATAL 0, both games end with rc 0, and there is no new dump.
- A host with no client logs the same card 049 lines as before, plus the names frame lines.
- fnlen reports no function over 60 lines.

## Report

### What was built (`mod/src/hooks.cpp`)

- **New opcode `ChanOp::ObjectiveActors`** (text frame, `kChanArgsText`). The frame text is `<objective id> <actor name> ...`. The host builds it in `c70_names_frame` and sends it just before the objective frame it belongs to. The objective frame layout is unchanged.
- **Byte count in one place:** `c70_append` (hooks.cpp:20135) is the only check of name bytes against `kChanTextMax`. If the frame is full, the host logs `WARN card070: host <id>: names frame full at <name> (<n> of <total> actors sent)` and sends what fits. More than `kC70MaxActors` (32) actors also gives a WARN and sends the first 32. The host always logs `card070: host t=... [snapshot ]<id> actors <n>: <frame text>`.
- **Journal:** `c49_emit` records the names frame in the card 049 journal only while there is room for its objective frame too (`n + 1 < kC49JournalCap`), so a late joiner never replays a names frame without its objective frame.
- **Client:** `c70_receive` keeps the pending names, and logs a WARN if an earlier frame was never used. On the next Add frame, `c70_take` checks that the id matches; a mismatched frame gives a WARN and is dropped. It then resolves each name with `c64_find_actor(world, name, Vec3{})`, gives a WARN for each name that does not resolve, and logs `card070: client t=... <call>(<id>): names sent <n>, actors found <m>`. `c49_receive` passes a real `TArray` (stack array of `kC70MaxActors`, as `C49Actors {data, num, max}`) in place of the old empty array. No flag: this runs in every client session. The card 068 detours stay behind `UBWCOOP_C68_LOG=1`; the c68 ObjectiveUpdated log now also prints the objective id.
- **Checkpoint snapshot:** this uses **the objective's own field `USTLevelObjective+0x88`** (`kObjectiveActors`, hooks.cpp:19184), not a name table. Static evidence: the three `USTLevelObjective` init variants (0x051513E0, 0x05151520, 0x05151680) assign the actors `TArray` to `objective+0x88`, then call `CreateASTLevelObjectiveMarker` (0x05150E20) once per element. Runtime check in c70b: the 11 `snapshot` lines show the same actor names as the live Add lines for the same ids.

### Runs

| Run | What | Result |
|---|---|---|
| c70a | `c68-minimap.txt`, `UBWCOOP_C68_LOG=1`, DEPLOY=1 | Pass. rc 0/0, ERROR/FATAL 0, WARN 0/0, no dump. |
| c70b | card 059c death route (checkpoint snapshot) | Snapshot proved, see below. The run ended by cap: my client env lacked `UBWCOOP_C43_TEST=1 UBWCOOP_C45_TEST=1`, so the client never logged the done line. rc 0/0, ERROR/FATAL 0, no dump. |
| c70c | late join, first try | Both games crashed at about 100 s with "GPU Crash dump Triggered". The kernel log shows an amdgpu gfx ring timeout (illegal opcode in command stream, vkd3d queue) and a full GPU reset at 04:23:10. This is the environment, not the mod. It left two dumps: `UECC-Windows-BC2DC67845D4899439B6148B56BCB338_0000` and `UECC-Windows-E76491B14238F7D0ADA0FDA3789B4F24_0000`. |
| c70d | late join, retry | "failed to create drawable / X connection broken" right after the reset. Not a mod fault. |
| c70e | late join, STAGGER default | The client joined at +114 s, before the host's checkpoint. AUTODROPSHIP pauses the host's world for up to 120 s (`kAutoDropshipWaitMs`) waiting for 2 PlayerControllers, so the host only passed the checkpoint after that wait. The scenario failed at line 13, as it should. |
| c70f | `tools/scenarios/c70-late-join.txt` (new), STAGGER=190, cap 520 | Pass. rc 0/0, ERROR/FATAL 0, no dump. The one host WARN is the known solo `autodropship: 1 PlayerController(s)` line. |
| c70solo | `tools/scenarios/c71-solo.txt`, `SOLO=1` | Pass. rc 0, ERROR/FATAL 0, no dump. The one WARN is the known solo autodropship line. |

**c70a counts (caller exe+0x5150FB0):** host and client each logged 12 `ObjectiveUpdated`, one per objective id, and 13 `ShowOnMinimap` each. The client logged 12 `card070: client` lines, each `names sent 1, actors found 1`, and no unresolved-name WARN.

**Assault_SO0:** the client made exactly **one** marker for it. The client's own level script only calls `SetObjectiveVisibility(Assault_SO0)` there, not an Add, so there is no second marker and no guard is needed.

**c70b (snapshot):** the host's checkpoint snapshot held 14 objectives and 44 journal frames, and logged 11 `card070: host ... snapshot <id> actors 1: ...` lines. The client took all 11 (found 1 each). Its Adds were then skipped by `c49_skip_add`, because a LoadLastSave restore does not load a map and the ids already exist, so the client kept its existing markers. ActiveMarkers was 2 on both host and client afterwards. `ObjectiveUpdated` per map load: host 11 / 11 / 10, client 11 / 11 / 0 (the client's third map was the restore, with no new markers to make).

**c70f (late join):** the host saved the checkpoint (`Save_CheckPoint ... flag 1`) at 24156193 ms and added Assault_SO0 at 24184490 ms. The client joined after that, and the journal replay sent 40 of 40 frames at 24235039 ms. The client logged 11 `card070: client` lines, all found 1. `ObjectiveUpdated` 11 / 11, `ShowOnMinimap` 12 / 12, ActiveMarkers 3 / 3. The client's world view shows the RANGE and COURSE markers.

**Solo host (c70solo against c71solo, same scenario, card 071 build):** the 62 card049 lines are the same after removing timestamps. The only difference is the subsystem pointer in the `world check` line. The 13 new `card070: host` lines appear, one per Add with actors.

**fnlen:** `functions 1300, over 60: 0`.

### Shots (host left, client right)

- `logs/c70a-first-marker.png`: the `first-marker` shots side by side. `logs/c70a-pickup.png`: the `pickup` shots.
- `logs/c70a-mm.png`: 4x minimap crops at first-marker, pickup and range. The objective icons are the same on both sides.
- `logs/c70f-view.png`: the late-join shots with 4x minimap crops. The host shows 3 objective icons and the late-joined client shows 3.
- The source shots are in `prefix/.../Saved/Screenshots/Windows/{host,client}-c70a-*.png` and `{host,client}-c70f-late-join.png`, plus `host-c70f-checkpoint-passed.png`.

### Found on the way

- **New in c70b:** `WARN card072: client: AmmoSet with no ammunition inventory` at the checkpoint respawn. The respawned client pawn has no ammunition inventory yet when the AmmoMirror frame arrives. Card 070 did not change this; it is card 072 code.
- c70b also had the two known card044 `CompleteDeath` fallback WARNs, also seen in 059c.
- The late-join scenario needs STAGGER=190 while AUTODROPSHIP waits up to 120 s for a second PlayerController. The scenario header says so.

## Senior review (2026-11-05 04:46): accepted

- Checked c70a, c70f and c70solo myself. All three drives end with `scenario passed`. logsum gives ERROR/FATAL 0, and the only WARN is the known solo `autodropship` line. c70b has the two known card044 `CompleteDeath` fallback WARNs and the new card072 one.
- All runs started after the DLL deploy at 04:08:30. fnlen: 1300 functions, 0 over 60 lines.
- c70a: the client logs 12 `card070: client` lines, all found 1, and c70f logs 11. The minimap crops in `logs/c70a-mm.png` and `logs/c70f-view.png` show the same objective icons on host and client. The late-joined client also shows the RANGE and COURSE world markers.
- The two new dumps (04:23) come from c70c. The kernel log shows the amdgpu reset at 04:23:10, so they are environmental, not a mod fault.
- Using `USTLevelObjective+0x88` for the snapshot is accepted. It is better than a name table, and c70b matches it against the live Add names.
- Follow-up: `WARN card072: client: AmmoSet with no ammunition inventory` at a checkpoint respawn. The seed arrives before the client's respawned pawn has its inventory, and it is dropped, so the client loses its Laser seed after a respawn. This goes into card 073 as step 0.
