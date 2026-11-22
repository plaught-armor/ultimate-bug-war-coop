# 608: The card 604 skip summary reads a freed sound asset at a map change and can crash
Status 2026-11-22: crash fix shipped in b465. The net budget name copy waits for the next promoted build.

Open 2026-11-22 (junior, found by card 573 b464 run 2). Fix written for b465, see "b465" at the end.

## Bug

`c604_skip` keeps the raw sound asset pointer (`C604Skip::asset`). `c604_skip_summary` reads that object's name when
it prints (`mod/src/ubw/audio_c604.cpp:223`). At a map change the print runs from `c99_arm_map`, after LoadMap has
returned, when the old map's assets can be gone.

## Evidence

- `logs/c448b464b-client.log`: the client crashed on `client disconnect`, 109 s into the run. Access violation reading
  0x499CE5A8 at ubwcoop+0x18BE11. Stack by the b464 map: fname_entry_checked, object_name, c604_skip_summary,
  c604_totals, c99_map_totals, c99_arm_map, client_diag_load_map_return, detour_load_map.
- The faulting instruction is `mov edx, dword ptr [rcx]`, the first read of the object's FName in the card 238 guard.
  So the stored object's memory was unreadable. That guard checks the name index and cannot guard this read. That the
  object was freed is an inference from this and the next row.
- All 28 map-time asset rows in the six c448 client logs that have them (b460a, b461a, b462a, b463b, b464a, b464b)
  print "None". The 60 s rows of b464a and b464b print real names (15 of 15, both sides). One of the six logs crashed.
- Dump: `prefix2/drive_c/users/steamuser/AppData/Local/StarshipTroopers_UBW/Saved/Crashes/` folder
  `UECC-Windows-DF09A9BE4A5E7622DB813BA531B2055B_0000` (not opened).

## Reach

- The row is in every kept build from b460 on, so also b463, the play build. b464 does not change this code.
- It does not need diagnostics: both lane 1 play logs of 2026-11-22 05:03 say UBWCOOP_DIAG unset and have 33 skip
  rows each. Neither reached a map-time row or a crash.
- The "quit" totals and the 60 s print read the same pointer. No bad name was seen on the 60 s path.
- Not shown: whether UBWCOOP_C571=1 changes the odds.

## Fix candidate

Copy the asset's name into the row at insert, as `owner` already is, and print only the copy. The pointer stays as a
compare-only key. This covers the map, quit and 60 s paths.

The sweep is under "b465" at the end. It replaced a first text-only pass that found three sites.

## Done

- The fix and the full sweep are in one build.
- A c448 run with the disconnect prints real asset names in the map-time rows (0 "None") and does not crash.

## Senior verdict (2026-11-22): fix approved as b465, not built

Checked by the senior: the source path, the crash tail, 28 of 28 map-time "None" rows, and 33 rows in each lane 1 play
log with UBWCOOP_DIAG unset. The symbolized stack was not re-checked.

- b465 is b464 plus this fix and any sweep find of the same kind. No step C and no card 607 code.
- Fix: a name field in `C604Skip`, filled by object_name when the row is inserted. The summary never dereferences
  `asset`. A reused address can merge two assets in one period of the tally: accepted, state it at the field.
- Sweep: every pointer that is stored in mod state and read through later by a summary, totals, map-arm, map-return or
  quit path, helpers and direct field reads included (object_name, class_name, c50_name, c74_asset_path, read_ptr).
  List each site here with the reason it is safe, or fix it.
- One run: c448 with UBWCOOP_C571=1 and the disconnect, the setting that crashed. Prediction: scenario passed, 0 not
  proved, mismatches 0, no CRASH row or new dump, WARN classes as c448b464a, at least one map-time asset row after the
  disconnect and 0 of all map-time asset rows named "None". No crash alone is no proof: 5 of the 6 old runs did not
  crash. The names are the proof.
- On a pass, promote that exact pair to lane 1 and build/ with no further question, with no game running, and check the
  hashes of both copies. On a fail, no rerun: send the logs and the failed line to the senior.

## b465

### Sweep method and limits

- Pass 1 followed pointer reads out of the report functions: 95 functions, 208 sites. It missed rows reached through
  table references (C409Row, C518Blocked), so it does not stand alone.
- Pass 2 listed every data-pointer field or global used in a function reachable from the map enter, map return,
  seamless, quit and report roots, helpers included: 540 functions, 74 stored pointers, each judged by hand below.
  After the fix it lists 71, and the ones left in fixed rows are only compared or null-tested.
- Not followed: pointers stored as integers, and memcpy copies. Both passes are static.
- `object_listed` and `object_present` do not guard a freed object: `object_item` reads the object's own index first.
  A weak check (index and serial) does guard.

### Fixed: the name is copied at insert, the pointer is never read through

| Row | Printed by | Path |
|---|---|---|
| C604Skip.asset | c604_skip_summary | 60 s, map, quit |
| C136SetVarRow.asset | c136_setvar_dump | c136_report (map end, map arm, quit) |
| C409Row.asset (cards 409 and 418 tables) | c409_table_line | c136_report |
| C418Null.cls | c409_report | c136_report |
| C518Blocked.asset, .owner_class | c518_blocked_lines | c136_report |
| C438BySystem.owner_class | c438_report_systems | c136_report; its "alive for the session" comment was false |
| C527Spawn.cls | c527_spawn_row | map end |
| C310Journal.default_class | c310_arm_map | map arm; both writes copy the name |
| ClassRow.cls (ubwperf) | class_report | card 536/537 report; object_listed was its guard |

C604Skip keeps the text (object_name). The others keep the 8-byte FName (object_fname) and print it with fname_text,
which gives the same text. A reused address merges two objects in one row of each tally. In ubwperf that table lasts
the process, and a class that is gone now prints its name in place of "(unloaded <pointer>)".

### Safe, not changed

| Site | Reason |
|---|---|
| C545Row.cls, C546Row.comp, C548Row.comp, HostAsset.object | weak checked before the read |
| C573Row, C573nRow, C593Pair.nav, C593Class | name already copied at insert |
| C593Watch.actor_cls, C593Seen.comp, C132ClassSlot.cls, C160MapDump.asc, C586Hold, C104Relayed.actor, C132 objective_actor, C67.visited, C67.listed, g_c220_class, C101Window.cmc | compare key; any read is of the caller's live argument |
| C565Entry.npc, C74Live.proj, DecalCopy.decal, c310_live | null test or count |
| C114State.world, C45State.pawn, C67.cls, ClientNet world/pc/pawn, C158State.held, g_c347_camera, g_c74_grenade, g_c322_bp_caller, g_c361_scoped, PlState.own_cmc, g_c220_class at map end | cleared without a read |
| C423Class.cls | cleared at map end; the map arm and quit do not read through it |
| C103Enemy.component | printed as a pointer value |
| C42Chunks.held, ClientAsset.held | held by a GameInstance reference, the GameInstance by weak |
| C337State.engine, g_c346_engine, exe base, log file, NamePool, g_perf, Api tables, autoinput steps, cached native classes and their static caches (c104 primitive, c132 mesh classes) | process lifetime |
| C330Caller.ret, C545Row.ret | code address, used as an integer |
| C27Walk, C27Out, C27Sparse, C29Layout, C44Kid, C114Walk, SparseView, collect lists | filled and read in the same call |
| Join replay, after-load, step and test paths from pass 1 | not on a summary, totals, arm, return or quit path |

Outside these paths: the gameplay reads of C310Journal.default_class are card 609.

### Build

- b465: dwmapi `03df06297887feeb`, ubwperf `a59345153c6e5358`, both with maps in build-b5. It is the b464 tree plus the seven files of this fix
  (no other source file is newer than the b464 DLL); no step C and no card 607 code. 0 warnings.

### Run c448b465a and promotion (2026-11-22)

- Passed the prediction: ended by scenario after 196 s, scenario passed, 0 not proved, 0 mismatch rows, 0 CRASH rows,
  no new dump, WARN classes as c448b464a (totals 4 and 6). The client printed 5 map-time card 604 asset rows, all after
  a disconnect (lines 6058-6059 and 10196-10198), 0 named "None". The host printed no such row.
- card518 still prints "None" on 1 host and 5 client rows. The same six rows, with the same owners and counts, are in
  c448b463b. They are not stale names, because the name is now copied at insert. By the print code they are calls with
  no asset and no tag (inference: the pointer is not logged). Two one-call Trooper rows that were "None" in c448b464a
  have no such row in b465; that run prints MIBARK cue names for those owners.
- Promoted to lane 1 and build/ with both maps and no game running. All four DLL hashes were checked after the copy
  (dwmapi 03df06297887feeb, ubwperf a59345153c6e5358). Rollback: the b463 pair in build-b5, byte for byte what was replaced.
- One run. It shows the names fixed on the crash path; it does not measure a crash rate.

### Senior check of b465 (2026-11-22)

Passed and promoted. Run c448b465a: scenario passed, 0 not proved, every mismatch row 0, no CRASH row or new dump, WARN classes
as c448b464a, 5 client map-time asset rows after the two disconnects and 0 named "None" (b460a-b464b: 28 of 28 "None").
The crash was intermittent (1 of those 6 runs), so the names are the proof and the single run without a crash is not.
Lane 1 and build/ checked at 09:19: dwmapi `03df06297887feeb`, ubwperf `a59345153c6e5358`, both build/ maps equal build-b5.
Rollback: the b463 pair in build-b5.

One site outside the sweep's paths is open for the next batch, found by reading and never seen to fail:
`net_budget.cpp` stores a class or function pointer (`net_add`, line 347) and reads its name up to 5 s later (line 711).
A map change is safe, because `net_budget_arm_map` empties the table at LoadMap return before the next report.
An unload and GC inside one window on the same map is not safe. Fix: copy the name at insert and keep the pointer as the compare key.
`c434_calls_row` is safe: it reads inside the bunch that filled the table.

## Residual fix in b466 (net_budget, batched with card 607)

`net_add` copies the FName at insert and keeps the pointer as the compare key only. `net_log_window` prints it with
`fname_text` and no longer reads through the pointer. b466: dwmapi c43256c1745ffb29, ubwperf ccb1fa91ca26e74c.

Prediction, written before run c607b466a: the host prints `netbudget: host   props|rpc <name>` rows every 5 s without
an extra env, as in c448b465a (266 rows). The names are the same kind (BP class and function names), none is empty,
"None" or garbled, also in the windows after the restart's map change. No crash and no new dump. The run does not
force an unload inside a window, so it shows the copy printing right and not the stale case.

## Result b466, run c607b466a (2026-11-22)

The host printed 562 net budget name rows, 53 of them after the restart's respawn row. All 25 distinct names read as
class or function names, none empty, None or garbled. No crash and no new dump. The stale case was not forced.
The batch failed on card 607 step 3, so this fix is in b466 only and is not in the play build (b465).
