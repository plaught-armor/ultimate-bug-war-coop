# 236 — Client cannot resolve the host's dropship spline on FED_C3M2

Soak c234-c3m2 (DLL `20cf859dd191a8fe`): 11 client lines, card159 `dropship spline
BP_CustomFlightPathActor_C_6:FlightPathSpline not resolved`. A player may see the client's dropship fly its
default path while the host's flies the custom one. Not checked visually.

Static first: how card 159 names the spline (actor name plus component), whether `BP_CustomFlightPathActor_C_6`
exists on the client under another name (run-time spawn) or is not loaded (streaming level). Then one run on
`tools/scenarios/c234-c3m2.txt`. Done when the spline resolves and the dropship places match in a digest round.

## Step 1 answers (source plus logs c234/c235, senior, 2026-11-13)

- Cause: `ds_client_spline` (`dropship.cpp:403`) looks the owner up with `c50_find_placed(world, {AActor}, 1, name)`.
  `c50_find_placed` fills a `C23List` through `c23_collect`, which keeps the first `kC23Max` = 256 actors of the
  class and only counts the rest (`hooks_ubw.cpp:979`). With `AActor` as the class that is 256 of about 27800 actors
  (client digest round 1), so `BP_CustomFlightPathActor_C_6` is never compared. `found -` is the null owner.
- The actor is not missing on the client: the host has 7 placed `BP_CustomFlightPathActor_C` (map actors, no
  run-time spawn, `card132` inventory `placed=7 rep=0`), and placed actors load on both sides.
- Why FAST runs are clean: with the flight skipped, host and copy both stay on `DefaultSpline`, the names match and
  the function returns at its first line.
- Effect in the ride: the copy flies its own `DefaultSpline`, 10000 to 33000 uu off the host's dropship for the
  whole flight (12 capped rows per run; the real count is higher).
- The other three callers pass narrow classes (doors, pickups) and are far below 256 per map; no change there.
- Second, unproven link: `find_component_of(owner, class of the copy's DefaultSpline)`. If `DefaultSpline` is a
  subclass of `USplineComponent` and `FlightPathSpline` is not, the lookup fails after the owner is found. The
  WARN split below decides it in the same run.

## Fix spec (by class; `dropship.cpp`, `interact.cpp`, `hooks_internal.h`, `tools/symbols.txt` if a class is bound)

- `interact.cpp`: new `c50_find_placed_any(world, name)`: walks `world` levels and actors directly (bounds
  `kMaxLevels`, `kMaxLevelActors`, as `c23_collect` does), returns the first placed actor whose `c50_name` equals
  `name`. No `C23List`, no class filter. `ds_client_spline` uses it; `c50_find_placed` and its callers stay.
- Cost bound: the search runs only while the two spline names differ (once resolved the names match and the
  function returns early). State this in the comment.
- Component: look up by `USplineComponent` (bind its static class if not bound) instead of the class of the copy's
  own spline, then keep the name check. A base class finds any subclass.
- WARN split: `not resolved: owner not found` and `not resolved: owner <name> has no spline <component> (found
  <id>)`.
- Do not touch the card 159 lead and tolerance logic or the host's send path.
- Prediction (next FED_C3M2 run with the ride, FAST=0): 0 `not resolved` rows; one `card159` live row with
  `differs`, then rows with `spline BP_CustomFlightPathActor_C_6:FlightPathSpline/` the same on both sides and
  `uu off` inside the tolerance for the flight. Budget: 1 ride run, batched with another C3M2 card.

## Status check (2026-11-16 20:00, senior)

The fix spec is built (in source as of b161). Its proof run on FED_C3M2 is still owed; it goes with the next C3M2 run.

## Closed 2026-11-16 20:31 (run `c236b163a`, b163 `67d43ad88c956cf3`, FED_C3M2 with the ride)

Client totals row: `join check 14 of 14 match ...; 63 live frames ..., 0 splines not resolved`. 0 `not resolved` WARN rows on
either side. Live rows for `BP_PlayerTransportDropship_C_4` stay inside the tolerance for the flight (1 uu off of 166
at the start, 32 uu off of 716 later).

## Amendment (2026-11-16, senior)

Run `c236b163a` did not exercise the resolve path: the client's own spline already matched, 0 spline writes, no row
names `BP_CustomFlightPathActor_C_6`. The close rests on `0 splines not resolved` and the 14 of 14 join check. Watch:
any `not resolved` spline WARN in a run or playtest log reopens this card.
