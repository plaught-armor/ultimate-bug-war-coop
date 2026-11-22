# Card 199: A bug-map level objective the host has exists on the client

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: queued 2026-11-12, from c195-r1 (first run with a ponged client on a bug map). Budget: three runs. Static first.

## Why

c195-r1 client, BUG_C1M1, digest round 1: "WARN card132: mismatch STLevelObjective key=88364238 presence host 1
client 0; no row sent" (actors 1122 host, 1121 client). The host has one `STLevelObjective` actor the client does
not. The user's rule: objective state is the same for every player. New WARN class.

## Do

1. Trace, no run: name the objective (host log, key 88364238), who spawns it on the host (placed, GameMode,
   objective subsystem at run time) and why the client has no copy (not replicated, spawned server-only, or spawned
   before the client's hold released). Check the FED maps' objectives for the same path (card 049 journal).
2. Fix in the objective relay that owns the class; a join replay sets the state.
3. One run: bug map, client in, digest round with 0 mismatches.

## Done when

- The trace names the cause; the run's digest rounds on the bug map show 0 mismatches and the client HUD line for
  the objective. The Report ends with "Report complete."

## Trace (step 1, static and c195-r1 logs; no run)

Each line is verified (V) or open (O).

1. V. The key names the objective `Awaken_O1`. The card 132 objective key is FNV-1a of `"O:<id>"` at level 0xFF;
   FNV-1a("O:Awaken_O1") = 0x88364238. The host "Q" query logs "0 found" for it because `c132_host_query`
   (digest_c132.cpp) searches level actors only, never objectives (see Problems).
2. V. The host creates it inside `UEngine::LoadMap`, before LoadMap returns. c195-r1 host log line 3501:
   "card049: host t=104929898 AddObjective(Awaken_O1) bools 1110, count 0, actors 0, depth 0, ctx
   BUG_C1M1_Scripting_C, objectives now 1; caller exe+0x5081B44". "UEngine::LoadMap returned true" is line 3503
   (t=104929902) and "diag: host/standalone map loaded" is line 3668 (t=104929903). No "card049: host sent" line
   follows, because no synced PC exists yet.
3. V. The level Blueprint makes the add (kis.py on BUG_C1M1_Scripting.umap, extracted with tools/iostore/iox.py).
   `ReceiveLevelStart` goes to ExecuteUbergraph(962), then "Add Cave Objective()" (ubergraph 2989, goto 2041). At
   2041 it calls AddObjective(self, Awaken_O1, Temp_object_Variable, true, true, true, false), which matches the
   logged bools 1110. Its other adds (Cliff_O1 at 1160, Crater_O1 at 1251, Command_O1) are on later events.
   `ReceiveBeginPlay` (3024, goto 977) only sets the bindings ("Setup Objective Complete Binding", "Setup Forms
   Binding"). `ReceiveLoaded` (4512) runs the tutorials, then IsObjectiveComplete(Awaken_O1) at 2863.
4. V. `ReceiveLevelStart` is fired by `ASTSaveableLevelScriptActor::OnLevelStart` 0x05130EC0, which calls
   `ReceiveLevelStart` 0x05093B20. OnLevelStart has no authority check: it clears +0x338, calls ReceiveLevelStart,
   calls vfunc +0x768, then tail-jumps to 0x14513DFE0. It is virtual, slot +0x658 (index 203) of the
   ASTSaveableLevelScriptActor vtable 0x080CEA28 (entry at rdata rva 0x080CF080).
5. O. Who calls OnLevelStart (and so whether it is server-only). A byte search for `call [reg+0x658]` finds 30
   sites, all in unrelated engine functions, since that slot offset is common across classes. The PDB has no other
   public named for it, apart from `execOnLevelStart` (the Blueprint thunk).
6. V. The card 049 journal loses the add. `c49_emit` records it into `g_c49_journal`. `c49_arm_map()` is then called
   from `client_diag_load_map_return` (sprint.cpp:238), which runs after "UEngine::LoadMap returned true".
   `c49_arm_map` resets the journal (`c49_journal_reset`, sprint.cpp:1161-1172). The client's first Pong replay
   (objectives.cpp:395-418) therefore sends "journal replay ... 0 of 0 frames" (host log line 4128).
7. V. The client never adds it itself. The c195-r1 client log has zero card049 lines, and `c49_audit` logs on both
   sides. So the client's level script did not run the add (item 5 is why: OnLevelStart does not run there, or runs
   with a different world). With no relay frame and no local add, the client has no `STLevelObjective` for
   Awaken_O1: 1121 actors on the client against 1122 on the host.
8. V. FED_C1M1, FED_C2M1, FED_C3M1: every objective add comes after LoadMap returned, so the journal keeps them and
   the replay carries them. Checked over 625 host logs (scratchpad preadd.py, counting adds between "LoadMap enter"
   and "LoadMap returned").
9. V. FED_C4M1 takes the same path as the bug map. Its 12 Park adds come before LoadMap returns: Park_O1,
   Park_Marker, Park_SO1 (+A, B, C) and Park_SO2 (+A to E). Example c89g-host.log: the adds are lines 711-733,
   "LoadMap returned" is line 734, and the LZ_O1 adds come after it. The replay at line 1028 is "4 of 4 frames",
   the LZ frames only. A FED_C4M1 client also lacks the Park objectives. Their keys: O:Park_O1 0x3F864B41,
   O:Park_SO1 0x38CE2CFE, O:Park_SO2 0x37CE2B6B, O:Park_Marker 0x95E87B03.

Cause (verified): the card 049 per-map journal reset runs at LoadMap return. It wipes the adds that a level script
makes inside LoadMap (ReceiveLevelStart), so the join replay has nothing to send. This is the card 184 class: a
per-map table reset after LoadMap wipes rows recorded inside LoadMap.

## Proposed fix (step 2, in words)

The card 049 objective relay owns it (objectives.cpp, and `c49_arm_map` in sprint.cpp).

- Move `c49_journal_reset` and the `g_c49m` mirror reset out of the LoadMap-return path to LoadMap enter
  (`ubw_load_map_enter`, the place card 182/184 uses). The rest of `c49_arm_map` stays at return.
- The adds a level script makes inside LoadMap are then journaled. The first-Pong join replay sets them on the
  client: the Chapter frame first, then Add Awaken_O1 with its flags (1110). The existing client-side
  DoesObjectiveExistForID guard keeps a later duplicate a no-op.
- Check `c49_mirror`'s `first_add_ms` and Chapter emission: the first add now lands before the map-return arm, so
  the Chapter frame must still precede it in the journal.

Rejected alternative: snapshot the objective map at the first Pong (`c49_snapshot_journal`). It needs a second
source of truth beside the journal. The reset move keeps one.

## Predictions (step 3 run, the bug map with the client in)

- P1. The host log on BUG_C1M1 shows "card049: host ... AddObjective(Awaken_O1)" before "LoadMap returned", and the
  client's first-Pong replay line reads "journal replay ... N of N frames" with N >= 2 (Chapter + Add Awaken_O1).
- P2. The client log shows the Awaken_O1 add applied from the relay. The client HUD shows the objective line.
- P3. Every card 132 digest round on the bug map shows mismatches 0 (no STLevelObjective key=88364238 WARN).
- P4. No duplicate Add on the client: at most one "client ignored ... id exists already" line, and no second
  STLevelObjective for the id.
- P5. FED_C1M1, FED_C2M1, FED_C3M1 journals are unchanged (same frame counts as before the fix).
- P6, if the run reaches FED_C4M1 (not required): the replay carries the 12 Park frames as well as the LZ frames.

## Problems for the senior

- The card 132 "Q" query (`c132_host_query`, digest_c132.cpp ~1648-1704) searches level actors only. An objective
  key always answers "0 found", so every objective mismatch needs a hand FNV match to name it. A lookup of the
  `g_c49m` ids by key would name it in the log.
- Class sweep (one manager per class): card 184 had the same shape for the card 104 tables. Every per-map reset
  that runs at LoadMap return or the first tick after it (the `c49_arm_map` family in sprint.cpp) can wipe rows
  recorded inside LoadMap. That needs one sweep, not a card per table.
- FED_C4M1 Park objectives (item 9) are a live gap for the full-world-sync rule today. The same fix covers them,
  but no run has shown it.
- Item 5 stays open. If OnLevelStart also fires on the client in some route (for example a standalone load), the
  client would add the objective itself and the replay add must stay a no-op (P4 covers this).

## Run c199-r1 predictions (junior, before the run)
Build e7d7860ed7f47b95. The fix moves the journal, compact flag, card 066 cap and `g_c49m` reset from `c49_arm_map`
(first tick after LoadMap) to `c49_map_end`, called from `ubw_load_map_enter`.
- C199-P1. On BUG_C1M1 the host logs "card049: host t=N journal replay to BP_PlayerController_C... (first Pong of
  the map): K of K frames" with K above 0. c195-r1 logged "0 of 0 frames".
- C199-P2. The client logs "card049: client t=N replayed AddObjective(Awaken_O1...) ... objectives a -> b" with b
  greater than a.
- C199-P3. Every card 132 digest round on the bug map, before and after the checkpoint restart, reads mismatches 0.
  c195-r1 had "mismatches 1 (classes STLevelObjective:1)".
- C199-P4. FED_C2M1 (the start map) replays the same frame count shape as in c195-r1 (no growth from old-world frames:
  the reset runs before the new world spawns).
- C199-P5. After the checkpoint restart (an in-place LoadMap), the host replays the journal again with K above 0 to the
  rejoined client, and the client adds no duplicate objective.

## Report (run c199-r1, build e7d7860ed7f47b95; accepted by the senior)
- C199-P1 holds. `card049: host t=106837198 journal replay to BP_PlayerController_C (first Pong of the map): 2 of 2
  frames in 0.016 ms` (c195-r1: 0 of 0).
- C199-P2 holds. `card049: client t=106837221 replayed AddObjective(Awaken_O1) flags 0x7 count 0: objectives 0 -> 1`.
- C199-P3 holds. `card132: client round 1: actors 1122 host, 1122 client, mismatches 0 (classes -)`; round 2 (after
  the restart) also has mismatches 0. The STLevelObjective WARN is gone.
- C199-P4 holds. FED_C2M1 replay is `0 of 0 frames`, as in c195-r1.
- C199-P5 holds. After the checkpoint, `journal replay ... 5 of 5 frames`; the client replays `AddObjective(Awaken_O1)
  ... objectives 0 -> 1` once, with no duplicate.
- c198-r2 (build 669cdec6305c5510) repeats P1 and P3.

## Senior review (2026-11-12)

Accepted on c199-r1 (DLL `e7d7860ed7f47b95`) and c198-r2 (`669cdec6305c5510`). Checked by me: diff limited to the
allowed files; `c49_map_end` runs from `ubw_load_map_enter`; host journal replay on BUG_C1M1 2 of 2 frames (was 0 of
0); the client adds Awaken_O1 after the rejoin; digest rounds 1 and 2 at 0 mismatches; the STLevelObjective WARN is
gone. Not run: FED_C4M1 Park objectives (soak watch item in card 127).
