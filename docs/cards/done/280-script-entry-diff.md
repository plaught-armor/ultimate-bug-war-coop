# 280 Level-script entries on host and client, measured (diagnostic only)

From `docs/SYNC_AUDIT.md` entry 5 (senior, 2026-11-13). The level script runs on both machines from each
machine's own triggers; client-side results are cut sink by sink (cards 006, 100, 254, 258). Before deciding on
"the client runs an entry only when the host ran it", measure how far apart the two scripts run today.

## Do (one diagnostic, no behaviour change)

- `mod/src/ubw/hooks_ubw.cpp` `detour_process_event` (hook exists): when the object's class name ends in
  `_Scripting_C` and no outer call on such an object is in progress (depth 0), log before the original:
  `card280: <host|client> t=<ms> script entry <function name> on <object>`; cap 400 lines per map, the rest
  counted and reported at map end as `card280: <side> script entries N, logged M`.
- The class test must cost one pointer compare per call after the first resolve per map (cache the class
  pointer at map load; `ProcessEvent` is hot). No string work on the hot path for other objects.
- Do not touch any sink cut. No new op, nothing sent.

## Predictions (rides the next run of each map, 0 extra runs)

- Host and client rows for one map differ: client-only entries exist (the card 254 chain is one), and shared
  entries differ in time by more than 100 ms where a trigger depends on which pawn overlaps.
- If the rows are equal in name and order on every map, the change in the audit entry is not needed and the
  entry is closed as "method right".

## Then

Senior diffs the rows per map and writes the verdict into the audit entry; a change gets its own card.

## Closed (2026-11-17 13:05): merged into card 149

Same diagnostic family (what the client's own game logic changes or runs). Its spec is carried in full in card 149; never built on its own.
