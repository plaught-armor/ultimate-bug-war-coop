# Card 190: A client that joins while dropships fly sees them where the host has them

Status: queued 2026-11-11, from c186-r1 (junior finding 3). Trace first. Budget: three runs.

## Why
c186-r1 rejoin with dropships in flight, client log: card159 dropship check x3 ("BP_ProceduralDropshipTroops_C_0
check: ... 1953 uu off (tolerance 381 ...), spline distance 4764/6712"; C_2 1995 uu, C_4 2179 uu), "join check 21
of 24 match", and digest round 2 with 6 mismatches about 200 uu off on actors near the dropships (BP_AISpawner_C,
BP_Dropship_LandingBlocker_C, BP_AISpawnPoint_C x4). c152-r3 rejoined with none in flight and had none of these.

## Do
1. Trace (card 159's join path): what the join replay sends for a dropship in flight (spline distance, start time)
   and how the client places it; name why the client is about 2000 uu off. Card 164's moving-parent evidence may
   be the same actors: say so if it is.
2. Fix at the cause: the join sets the flight's state (replays set state).
3. Prediction: rejoin with at least two dropships in flight, 0 card159 check WARNs, digest 0 mismatches on the
   dropship-carried actors.

## Files
Named by the trace; the senior confirms before code.

## Step 1 answers (c186-r1 logs, `dropship.cpp`, disassembly of `TickSplineTravel` 0x51E75A0; senior, 2026-11-13)

- The three dropships were parked at the rejoin (`at join: state 2/2 ... same (0 fields applied)`, 98759596). They
  launched 1.2 to 2.2 s later. The error is not in the join frame; it is in every flight leg near a spline end.
- The client's copy runs `TickSplineTravel` itself between host frames. Its speed is not a stored value: each tick
  computes it from the spline distance +0x484, the settings object at +0x408 and three `StartAction` bytes:
  startup +0x481 (1 = ramp up over the first settings+0x60 uu, 0x51E7A19), nav +0x480, and target point +0x482
  (0 = slow down over the last settings+0x70 uu, 0x51E7B56; any other value skips the slow-down).
- `StartAction` runs on the host only. The J and L frames carry nav and the spline, not startup or target point
  (`ds_frame`, `dropship.cpp:347`). So the copy keeps whatever +0x481 and +0x482 it had.
- Measured: C_4 host 18939, 15507, 10057, 6460, 5463 at 98760832 to 98763670 (7350, 5450, 3600, 2650 uu/s,
  slowing to land). The copy, set to 6460 at 98763294, read 3283 at 98763670: 8450 uu/s, the cruise speed of the
  first join's rows (8500). The copy does not slow down, so it runs ahead by 2000 to 4000 uu between two L frames.
  The first join matched because those legs were at cruise.
- The digest rows 200 uu off (`BP_AISpawner_C`, `BP_Dropship_LandingBlocker_C`, `BP_AISpawnPoint_C`) are actors
  attached to these dropships (`dropship.cpp:126`), sampled between corrections. Same cause; card 164's
  moving-parent rows are the same actors when the parent is a troop dropship.
- Side finding: the spline-end branch (0x51E82CE) has no authority check. A copy reaching the end with +0x483 = 0
  runs the action of its own +0x482 (1 `Destroy`, 2 `SetState(2)`, 0 begin descent). The 33 `held at the spline
  end` rows of the rejoin are copies that got there before the host.

## Fix spec (step 2, `mod/src/ubw/dropship.cpp` only)

- `DsLook` gains `startup` (+0x481), `target` (+0x482) and `ended` (+0x483). `ds_frame` sends the three after
  `nav`; `c132_dropship_receive` parses them; the spline identity stays last.
- `ds_client_snapshot` (J and L): write +0x481 and +0x482 when they differ from the host's; write +0x483 = 1 when
  the host's is 1. Count them in `fields`. `same` includes the two speed bytes. The C round compares them.
- Log: the `at join`, `live` and `check` rows gain `startup c/h target c/h ended c/h` before `nav`.
- No change to the lead, the hold or the tolerance.

## Predictions (rejoin run with at least two troop dropships landing after the join)

- First L row of a launch: `target N/0` with N not 0 on the client (the stale byte), then `0/0` on every later row.
- Landing leg: every `live` row within tolerance from the second row on; 0 `WARN card159 ... check`; `join check N
  of N match`; digest 0 mismatches on `BP_AISpawner_C`, `BP_Dropship_LandingBlocker_C`, `BP_AISpawnPoint_C`.
- Watch (decides a follow-up, not this card): with target 0 the copy now runs the begin-descent call at its own
  spline end. If a `check` or digest row after touchdown is off, the copy's +0x483 is set to 1 at every J and L
  (the host's frames then own the end action) and that goes in a new card.
- Budget: 1 run, batched with the next rejoin proof (cards 194, 203).

## Closed (2026-11-16)

Predictions hold in `c423b163a` (two rejoins with dropships in flight): `join check 25 of 25 match` and `24 of 24
match`; the rows carry `target c/h ended c/h` (stale client byte `target 1/0` on 7 first rows, equal after); 0 `WARN
card159`; digest rounds 1 to 11 mismatches 0 (no `BP_AISpawner_C`, `BP_Dropship_LandingBlocker_C` or
`BP_AISpawnPoint_C` row). `c460b169a` and `c460b170a`: join check 7 of 7 and 6 of 6. Reopens on a `WARN card159 ...
check` row after a join that is not the known take-off race.
