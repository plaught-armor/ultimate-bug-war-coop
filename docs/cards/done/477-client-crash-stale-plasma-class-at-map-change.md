# 477 — Client crash at a map change: stale plasma round class in the card 419 BeginPlay path

Found 2026-11-17 12:32, run c419b184a (FED_C5M1, DLL `c145bca0829a932e`), client log line 5434:
`CRASH 1/4: EXCEPTION_ACCESS_VIOLATION (0xC0000005) at exe+0x14CBD96 ... reading 0xAAD04A88`, stack
`exe+0x14CBD96 <- ubwcoop+0x14BBB8 <- ubwcoop+0xE9801 <- ubwcoop+0x7BAFF <- exe+0x37EEBD2`, during the frontend load
after the host quit.

Cause (static): exe+0x14CBD80 is the class is-child-of test; it faults reading `0x38(%rdx)`, the class argument.
`c408_pe` (satchel_c408.cpp) sends every `ReceiveBeginPlay` to `c419_begin`, which runs
`is_a(round, g_c408.projectile)`. `g_c408` and `g_c408_pe` are cleared only in `c408_arm_map`
(sprint.cpp:1201), which runs after the new map is up. Between the old map's teardown (the class is unloaded) and
that call, any actor's BeginPlay in the new world tests against the freed class. New in b183 (card 419). The same
window exists for the `is_a(actor, g_c408.cls)` tests at satchel_c408.cpp:572 to 583.

Fix: clear the card 408 state at the start of a map load (the LoadMap enter path), printing the map totals there, so
no card 408 or 419 test runs against a class of the old map. Hold nothing of the old map across the load.

Sibling sweep: every per-map cached class pointer used in a ProcessEvent or BeginPlay hot path that is cleared only
when the next map arms.

Done when: the c419-c5m1 run ends with the client back at the frontend, 0 EXCEPTION on both sides, and the card 408
and card 419 map totals rows print at the map load.

## Closed (2026-11-17 12:49)

b185 `41c369126e01b57f`. `c408_map_end()` and `c161_map_end()` run at LoadMap enter (`ubw_load_map_enter`) and at
the test quit: they print the per-map totals and drop the cached map Blueprint classes before the new world's
BeginPlay calls. Run c419b185a (FED_C5M1): 0 EXCEPTION on both sides, the client's frontend LoadMap returned, the
client card408 and card419 map totals printed at that LoadMap enter (42 rows applied, 6 bursts applied, 0 missed),
7 digest rounds with 0 mismatches, 0 WARN card419. Gate c448b185a passed (10 rounds, 0 mismatches).

Sibling sweep (junior, reviewed): c161 door class and door table had the same window and are fixed; c67, c423, c402,
c417, c98, c99, c120, c114, c440 hold no freed class pointer across a map change.

Reopens on: an EXCEPTION at exe+0x14CBD96 at a map change.
