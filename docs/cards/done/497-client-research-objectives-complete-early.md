# 497: Client marks Research_SO2 and Research_Marker2 complete before the host

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: open (written 2026-11-18 18:26, senior). Batch into the next build.

## Finding

The card 132 digest shows `WARN card132: mismatch STLevelObjective O:Research_SO2 complete host 0 client 1` 17 times and
the same for `O:Research_Marker2` about 10 times. The client holds the objective complete while the host does not. It
predates b245: seen in c247b242a, c247b242b and c247b245a (FED_C5M2).

## Trace (2026-11-18 18:28, senior, c247b245a)

- Host 685719335-340: the level script calls the library `CompleteObjective(Research_O1)` (depth 0). Inside it, the
  native cascade completes Marker1 and SO1. The OnCompleted broadcast then runs the level script handler, which makes
  nested library calls: four SetObjectiveVisibility, `CompleteSubObjective(O1, Marker1)`, and
  `AddSubObjective(O1, Research_SO2)` / `(O1, Research_Marker2)`. The adds are sent at once (journal 125 and 127).
  The outer `CompleteObjective(Research_O1)` is sent only when it returns (journal 128). On the host, SO2 and Marker2
  are added to an already complete parent, so they stay incomplete.
- Client 685719363-371: it replays in journal order. AddSub SO2 (2569) and Marker2 (2571) come first, then
  `CompleteObjective(Research_O1)` (2575). The native cascade then completes every child of O1, including the new
  SO2 and Marker2 (card097 depth 1 rows, card491 score +20000). That is the digest mismatch, and a score gain the host
  never had.
- Cause: card 491 orders only complete-kind calls nested in a complete. "Adds and the other kinds keep their order",
  so a nested add goes out before its outer complete.

## Fix

- In mod/src/ubw/objectives.cpp, extend the card 491 queue (c491_enter / c491_after) to every mirrored kind. On the
  host, while `g_c491.completes > 0`, any library call (add, add-count, visibility, callout, increment, complete) takes
  a slot at entry and fills it when it finishes. Only complete kinds bump `completes`. The outermost complete sends its
  own row first, then the slots in entry order.
- Raise kC491Nested from 16 to 64: this chain alone has 8 nested rows. A full queue keeps the existing WARN and sends
  in finish order.
- Client replay order becomes: O1 complete (cascade over the children present on the host at that time), then the
  handler's rows. AddSub to a complete parent leaves the child incomplete, as on the host.
- One row per outermost flush that sends any non-complete nested row: `card497: host t=<ms> <outer> sent before <n>
  nested rows (<k> non-complete)`.

## Proof (written before the run)

- The next C5M2 soak shows Research_SO2 and Research_Marker2 complete 0 on both sides after Research_O1 completes,
  and 0 `card132: mismatch ... Research_` rows.
- The client card491 score after Research_O1 equals the host's (40000 there, not 60000).
- The card497 row fires at the Research_O1 completion.

## Trace first (done above)

- Find what sets the objective complete on the client: a client-local trigger (presence, interaction, kill count) that
  runs its original on the client instead of waiting for the host's objective sync.
- Check whether the host later completes it too (the mismatch clears) or never does (a real desync).

## Proof (write before the run)

To be written after the trace.

Reopen when: any objective digest mismatch with client 1 and host 0.

## Closed (2026-11-18 21:03, senior, b253 `168fd79b995e1d9c`, c497b253a)

Evidence: host "card497: CompleteObjective(Research_O1) sent before 7 nested rows (6 non-complete)" at 695517619;
0 "card132: mismatch ... Research_" rows on either side (checked); card491 scores equal at SO3, SO4, Marker1, SO1,
O1, ending 40000 = 40000; Research_SO2 and Research_Marker2 complete 0 on both sides.
Not done: a Complete nested inside an Increment still goes out before the Increment row (residue, no case seen).
Reopen when: any objective digest mismatch with client 1 and host 0.
