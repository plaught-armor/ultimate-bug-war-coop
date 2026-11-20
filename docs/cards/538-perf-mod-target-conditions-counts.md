# Card 538: perf mod, DoesTargetMeetConditions repeat counts (diag)

Family: card 527 (performance mod).

## Why

c263L355 (b350, host perf): USTAttackComponent::DoesTargetMeetConditions is 3.1% of Background Work and the
TSet<FGuid, FADTargetData> find it reaches is 1.8%. Both are cache-miss bound. The hot loads are the target's
health component flags (exe 0x145186873) and the 128 B FADTargetData chain walk (exe 0x145177c0f). A cut has to
make fewer calls. The caller in the branch stacks is IsAttackUsableAgainstTarget, which calls once per attack
config and target.

## What (b352)

`mod/src/ubw/perf_target_c538.cpp` detours the function on both sides and only counts. Per thread and per frame,
it records:
- calls, and calls per frame;
- calls whose (component, target) was already seen this frame, which a per-frame target memo could share;
- calls whose (component, conditions, target) was already seen, which a result memo could share;
- of those full repeats, how many returned a different result, which a memo would have got wrong.

Repeats on other threads are not counted, so the counts are lower bounds. The game thread logs a `card538:` row
every 60 s. `UBWCOOP_C538_OFF=1` leaves the detour out.

## Decision rule

- Same target above 50% of calls: card a per-frame memo of the target lookups (FADTargetData row and health
  component) inside the call.
- Same conditions above 30% with differ 0: card a per-frame result memo.
- Below both: drop the lever and look at who drives the call count.
