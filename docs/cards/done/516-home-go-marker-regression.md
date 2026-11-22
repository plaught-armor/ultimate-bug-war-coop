# Card 516: b287 home go stalls guide-marker steps (c514a Bridge_Marker, 311 s)

Harness regression from card 514, not a mod bug. Waits behind the bug and perf work, but it is a one-line fix to apply
before the next C4M1 run (never mid-run).

## Cause (senior, c514a logs)

- Bridge_Marker (bools 01100) and Bridge_SO1 are added together (host 11363212/11363213). Bridge_Marker has no
  CompleteSubObjective in the level script (`build/kis/c4m1.txt`: only Add at 3127 and visibility calls); it closes
  natively with its parent Bridge_O1 (host 11839970, after Bridge_SO4). A guide marker, lesson 13.
- c513a: tries 1-2 failed, try 3 stood at the try-3 presence (34795, -58255), near Bridge_SO1's place, and the run went
  on. It passed by chance.
- c514a: the card 514 home go sent the last try back to the try-1 stand (cmd 180, `home stand at
  BP_PlayerPresence_C_256`, 318 uu from the marker actor), and the encounter stay held 311 s (60 clears) there. Bridge_SO3
  came only at 11738296, after the step VOIDed (line 120) and the drive moved on to Bridge_SO1.

## Fix

1. `tools/drive.py` routine_presence: the home go runs only for kind `presence`, not `marker`. Pass the kind into the
   routine (ROUTINES maps both kinds to routine_presence; `drive.py:1360`).
2. Follow-up (separate, not in this fix): a marker step whose parent also has an open non-marker sibling, added in the
   same tick, is a guide marker; the drive should work the sibling first.

Fix 1 is applied (`tools/drive.py` `routine_presence(..., kind)`: the home go runs only for kind `presence`; ROUTINES
passes `"marker"`). Fix 2 is not done. Only the C4M1 proof run is left.

## c244b319a (b319, C4M1): fix 1 not enough, and a regression

- Bridge_Marker: 3 tries, no home row (pass), but no Complete and VOID; Bridge_SO1 started about 90 s after the Add
  (fail). Cause: Bridge_Marker is a zone guide. The level script hides it and shows Bridge_SO1 while the players stand
  in the zone (Bridge_Marker bools 01 with Bridge_SO1 bools 11, host 57733895) and flips back when they leave (each
  re-go's lift-out flips it for about 3 ms). Bridge_SO1 never completes on its own either: the zone's work is hidden
  Bridge_SO2 (presence, completed 57840983, never added) and then Bridge_SO3 (added 57840981); all of Bridge_O1 closes
  natively after SO4 (57900438).
- Stadium_Marker is the same kind of zone guide (hidden while Stadium_SO1..SO4 show, 57901854).
- Regression from fix 1 (lesson 19): Stadium_SO2 is kind marker but a flag step (lesson 18, card 514); it needs the home
  stand in BP_PlayerPresence_C_5, and fix 1 took that away. Its try 3 go gate also failed ("next switched-on presence is
  too far"), and the loop `continue`s on a gate failure, so no home and no stay: VOID at line 155, then 14 "none
  doable, current Stadium_SO2 kept" rows. It completed natively at 57993596, about 35 s after the VOID.

## Fix 3 (b321, senior; DLL part done, drive.py part waits for the c263L320 run to end)

1. DLL `objective_c284.cpp` `c284_handed_on`: a kind-marker step whose marker was shown at one of its goes, and is now
   hidden while an open sibling's marker is shown, ends its wait with `c284: objective wait X: guide marker handed on to
   Y`. It must hold for 30 wait ticks (Stadium_SO2 flips off and on within 3 ms at the zone edge). When exactly one
   sibling is shown, that sibling is recorded as a zone guide.
2. DLL `c284_zone_moved`: a zone guide's wait ends when, since the first wait tick on it, its parent gained a child or
   a child completed (`zone guide passed, parent moved on`). Bridge_SO1: SO3 added at 57840981, between waits 143 and
   148.
3. `tools/drive.py` routine_presence: the home go runs for kind marker again (guides now end through 1 and 2, so the
   home stand serves only steps like Stadium_SO2), and a failed go gate on the last try falls through to the home go and
   the stay instead of `continue`.

Kinds served and why each is safe:
- Guide markers (Bridge_Marker, Stadium_Marker, PowerPlant_Marker): end at the handoff instead of waiting out three
  tries.
- Stadium_SO2: its marker stays shown through its waits (final state 11 after each 3 ms flip), and when it was added
  hidden (bools 01110) it had no go yet, so rule 1 never fires. Rule 2 can only reach it if it is the sole shown sibling
  at another guide's handoff; then it ends only when a sibling is added or completes, which does not happen while the
  `Add Stadium_SO4` retry waits on its flag.
- Outpost_SO4 (C5M2): the `ended by` guide rule is unchanged; rule 1 needs its marker hidden with a sibling shown.
- marker_held in drive.py matches only `guide marker ended by `, so both new rows end the routine as done.

## Done when

The next C4M1 run shows Bridge_Marker at most 3 short tries with no home row, and Bridge_SO1 started within 60 s of
the Bridge_Marker Add. b321 adds: a `handed on` row for Bridge_Marker, Bridge_SO1 ended by `parent moved on` or its
own row, and Stadium_SO2 completed inside its routine (no VOID).

## b322 result and b323 fix (2026-11-20)

c244b322a: Bridge_Marker handed on (pass), Stadium_SO2 completed (pass). Bridge_SO1 VOIDed (line 120). The `parent
moved on` rule works: it fired 0.8 s after Bridge_SO3 was added. The cause was the presence pick. Bridge_SO2's trigger,
BP_PlayerPresence_C_12, was never entered in the first round.

Nearest presences to the marker were C_9 (try 1's own stand), C_256 (already switched off), C_255, then C_12. The card
514 rank counts every presence, on or off. So try 2 skipped C_9 and stood in C_255, and try 3 skipped C_9 and C_256 and
stood in C_255 again. The stay then sat at C_9 for 312 s. The rerun after the VOID found C_255 off, so try 2 picked
C_12, and Bridge_SO3 was added at once.

b323 (`d05e6ac4a87eaec9`) changes `c284_nth_presence`. Try n skips the nearest presence of all (try 1's) and the
presences that tries 2 to n-1 of the same step stood in. It then takes the nearest switched-on presence left, so every
try enters a new presence.

Prediction for the next C4M1 run: Bridge_SO1 try 3 logs `nth presence 2: skipped BP_PlayerPresence_C_9,
BP_PlayerPresence_C_255, picked BP_PlayerPresence_C_12`, Bridge_SO3 is added within 10 s, and the wait ends on `zone
guide passed`. No VOID.

## c244b443a result (b443, 2026-11-21): pass

- Bridge_Marker: 3 short tries (nth presence rows at 193835535 and 193852374, all names different), no home row.
- Bridge_SO1 try 3: `nth presence 2: skipped BP_PlayerPresence_C_9, BP_PlayerPresence_C_255, picked
  BP_PlayerPresence_C_12` (193919890). This is exactly the b323 prediction. Bridge_SO3 was added 4 ms later (193919894).
  There was no VOID. Bridge_SO1 closed with Bridge_O1 (194045834).
- Not part of this card: Bridge_SO3's tries 1 to 3 failed (try 3: the next presence was 32232 uu away). The home stand
  at C_12 completed it 16 s later (194034301).

## Closed (senior 2026-11-21)

Proved in c244b443a (b443 da2b501a10253111, C4M1): see the junior's result row in docs/STATE.md (2026-11-21).
