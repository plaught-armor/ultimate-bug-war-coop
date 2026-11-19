# 273 Gib part rest rows recur after card 212: parts asleep in the air, one sticky part off the host's rest

Found by card 243 run 9 (`logs/c243-c3m2-r9-*`, FED_C3M2, DLL 75d9c5ce204aa049). Card 212 (done) fixed the
probe that reported "below 0 0 0". Card 263 lists these rows only as frame-time candidates. This card owns
them as a sync class.

## Evidence (client)

- 24 lines `WARN card148: client gib host part N ... asleep above the ground: nothing within 40 uu below X Y Z
  (all-component bounds, above N)`: 17 `STGorePartNonSticky`, 7 `STGorePartSticky`. First: part 87 at
  t=221174949.
- 1 line `WARN card137: client gib host part 3071 STGorePartSticky_StaticMesh lies 134.9 uu from the host's
  rest point` at t=223272301. card201 notes "S row applied 7472 ms before the check".

## Step 1 (static, no run)

1. For each of the 24 parts, find the host rest row (card104/card201) and its position. Is the host's part
   also above the ground (a sticky part stuck to a wall or bug), so the probe is wrong, or is the client
   part at a different place?
2. For part 3071: the S row was applied 7.5 s before the check. What moved the part afterwards on the
   client (physics woken by a later hit, or a re-attach)?
3. Say whether the probe should skip sticky parts that the host reports attached.

## Fix outline

Decided by step 1: either the probe is corrected (sticky parts attached on the host are not "above the
ground"), or the client keeps a part at the host's rest point once the rest row is applied (physics stays
asleep until a host row wakes it).

## Proof

No dedicated run. The next soak on FED_C3M2: 0 `asleep above the ground` WARNs that do not match a host
row, and 0 sticky parts more than 40 uu off the host's rest.

## Step 1 answers (run 9 logs plus disassembly, senior, 2026-11-13)

1. The "asleep above the ground" rows are not a sync fault. The host logs the same test on its own part
   (`card148: host gib part N ... rests with nothing within 40 uu below`, `gib_c137.cpp:611`): 24 rows on each
   side, 22 parts in both lists at the same point within 1 uu (part 87: host 221173900, client 221174949, both
   at -43272 51117 564). Part 1107 rests at z -19392 on both sides (fell out of the map in the base game).
   Two parts per side have no partner (client 460 and 1194, host 1173 and 1175): pieces in a pile at about
   (-16500, -12500, 2030), where the 40 uu probe is marginal. The client line is a WARN although the host is in
   the same state.
2. Part 3071 and part 1630 (the only two `card201: rest miss` rows, both `STGorePartSticky`) moved after the R
   row was applied, by the part's own state machine, not by physics woken from outside:
   - `ASTGorePartSticky::Tick` 0x513AE50 reads the state byte +0x340. State 2 (wall slide): timer +0x330
     against +0x334, each tick `TickWallSlide` 0x513D000 moves the actor down by speed times delta
     (`AddActorWorldOffset`), or sets state 1 when `GetWallHit` 0x5126380 finds the wall end. State 3 (ceiling):
     timer +0x338 against +0x33C, then `UnstickFromCeiling` 0x513EFD0 turns physics on and sets state 4.
   - The client runs this machine on its own copy of a host part, from its own `OnComponentHit` 0x512EEE0. The
     client's R row teleports and sleeps the body (`gib_client_pose`, `gib_c137.cpp:880`) but leaves the state
     byte, so a part in state 2 slides on (1630: 1.7 uu at the R row, 9.8 uu one second later) and a part in
     state 3 drops when its own timer ends (3071: 134.6 uu before the R row, 134.9 uu after it, asleep again).
   - Prediction, to be shown by one diagnostic field: the host queued these R rows while its own part was in
     state 2 or 3 (body asleep or not simulating, so it counts as rested), and the host's part moved again
     afterwards with no row sent.
3. Yes for the probe: the client line becomes a note when the host flagged the same part.

## Fix spec (step 2, by class)

- Host (`gib_c137.cpp`, rest detection): a sticky part is at rest only in state 1, or in state 4 with the body
  asleep. In state 2 or 3 it keeps sending M rows on the existing cadence and budget; no R row.
- Client: a host-driven sticky part does not run its own machine. On the S row and on every M and R row set
  +0x340 to 1 (the tick does nothing in state 1), so the host's rows are the only mover.
- R row gains one flag character: the host's `gib_no_ground` result. Client `gib_ground_check` logs a WARN only
  when the host flag is clear; with the flag set it counts the part and logs nothing.
- Diagnostic field: the state byte in the host's `card148: host gib part` line and in the client's `card201:
  rest miss` line.
- Files: `mod/src/ubw/gib_c137.cpp` only. Do not touch the pool code in `enemies.cpp` or the budget constants.

Predicted rows (next FED_C3M2 run, no extra run): 0 `card201: client gib rest miss` rows for sticky parts; 0
`WARN card148 ... asleep above the ground` rows except parts the host did not flag; host R rows for sticky
parts show state 1 or 4 only.

## 2026-11-16 11:50: state 0 parts wait the full 30 s (c423b125a)

- Host log c423b125a: 19 `WARN card273: host gib part N sticky state S not at rest after ~30000 ms` rows, 16 in
  state 0 and 3 in state 2. `gib_sticky_rested` counts only state 1, or state 4 with the body asleep, so a part that
  stays in state 0 sends its R row at `kGibStickyMaxMs` (30 s) though it stopped long before; the client's copy
  simulates on its own until then.
- State 0 in the exe: `ASTGorePartSticky::Tick` branch at 0x14513AFC7 only runs a timer (+0x36C against +0x374) that
  calls the sprite rotation update; nothing moves the part. The only immediate writes to +0x340 are 1 and 4; the state
  leaves 0 on a component hit. A part whose physics the significance manager switched off (card 279) gets no hit.
- Fix (`c273_state0_patch.py`, for b128): state 0 is at rest when the body is asleep or not simulating, the same
  test as state 4.
- Done when: a chain run has 0 `WARN card273` rows with `sticky state 0`, and the host gib totals row still has
  0 overflow. Refuted if state 0 rows remain: then log the body's awake and simulating bits on the WARN row.
- State 2 (1 row in c423b126a, part 442, 30159 ms; the earlier count of 3 was wrong): the tick's state 2 branch
  counts +0x330 up to +0x334 and then calls two component virtuals each tick with no state write. The forced-rest
  row carries both timers from b128 (`slide timer A of B s`); a row with A >= B means the state byte stays 2 after
  the slide and the body decides, as in state 0.
