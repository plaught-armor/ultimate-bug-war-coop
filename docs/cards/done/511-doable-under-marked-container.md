# 511: A marker-off step under a marked container is not doable

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: Opened 2026-11-19 by the senior, from the junior's c504b5 trace (DLL 4c265abd554c8232). Test-only code
(`objective_c284.cpp`). It goes into the next real-bug build batch with card 510.

## Finding

- FED_C2M1 c504b1 cmd 188: `container LOS_O03_SO02 count 2/3 marker 1; parent LOS_O03_SO02; open LOS_O03_SO02a kind
  other marker 0`.
- The three satchel pickup steps LOS_O03_SO02a-c are marker 0. Only their container LOS_O03_SO02 has its marker on.
- `c284_doable` (objective_c284.cpp:144) needs the step's own marker, or an open sibling with its marker on, so none of the three is doable.
- Result: `objective expect LOS_O03_SO02a` retries to the timeout and VOIDs, and `objective do` cannot reach the satchel-take routine.

## Do

In `c284_doable`, after the step's own marker check, add: when the step's container `up` is not the active parent, is
open, and has `kObjectiveMarker` on, the step is doable. The rule for steps directly under the active parent (b273,
a future step) stays as it is.

## Done when

On the batch build, one c504-board-c2m1 run:
- `objective expect LOS_O03_SO02a: doable (other), current`, and the same for b and c.
- Each `objective do LOS_O03_SO02x` gives `satchel-take ... complete on try N`.
- `objective wait LOS_O03_SO02` completes.
- Other expects in the run do not change. In particular, no future step under LOS_O03 becomes doable early. Compare the `expect` rows with c504b5.
- FATAL/EXCEPTION 0.

## Result (2026-11-19, b284 `62e2e778930a4dd6`, run c504b6)

Closed. `LOS_O03_SO02a: doable (other), current`, b the same; c was `already complete`, because its pickup was taken
by the a try (see the satchel-take gate note in objective-routines.md lesson 11). The other 14 expect rows are
identical to c504b5. The `objective now` doable lists differ only under LOS_O03, where all three SO02 subs are doable
at once. That is correct, because the subs are parallel: the game completed SO02c first. No step from another
container became doable early. LOS_O03_SO02 is the only marked container in the run. `objective wait LOS_O03_SO02`
completed, and the client replayed it. FATAL/EXCEPTION 0.
