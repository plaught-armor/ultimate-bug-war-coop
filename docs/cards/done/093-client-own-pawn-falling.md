# Card 093: Client's own pawn stuck in MOVE_Falling: find why, then fix

Status: accepted 2026-11-05 19:21 (senior). Cause and fix in one card.

Read:

- the card 092 Report and Review;
- card 014 (the own-pawn collision watch, `fix_own_pawn`, and the `chain:` lines in `ubw/input_diag.cpp`);
- card 013's PhysWalking table (`FindFloor`, `StartFalling`, `CheckFall`).

Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why

Card 092 found that the client's own pawn is in `MOVE_Falling` (3) on every frame, even while standing still on flat ground. It falls under gravity each frame, and every 50-60 ms the server's correction snaps it back up. The client's camera follows the pawn, so the whole view bobs; the user sees this as the host's body bouncing.

## Do

1. **Cause, using what exists.** Card 014's `chain:` lines already report, at the last PerformMovement entry:
   - MovementMode;
   - the CurrentFloor flags;
   - the actor collision and capsule CollisionEnabled;
   - the last blocking hit.

   Turn them on for one c92-still run (FAST=1) on both sides. The host side watches its copy of the client as the control.
   - Add only what is missing. Log `CurrentFloor.FloorDist` and the capsule's response to `ECC_WorldStatic`, once per chain row.
   - Log MovementMode on the own pawn at possession, and log every mode change for the first 10 s after it. That shows whether the pawn ever reached Walking.
2. **Fix at the cause.** Some possible causes and fixes:
   - The capsule does not block WorldStatic on the client: restore the response.
   - Falling is stale, left over from the drop or teleport, while the floor is found: call `SetMovementMode(MOVE_Walking)` once on the own trooper after possession, and only when the floor is walkable.
   - Something else: say what it is and fix that.

   Do not smooth the camera or the corrections.
3. **Proof**, two runs only:
   - c92-still with `UBWCOOP_C92_LOG=1`: the own mode is 1 after landing, own Z p2p under 0.5 uu, and no snaps.
   - c92-strafe: no snaps over 2 uu on flat ground. Walking off the pad edge may still fall once, and that is fine.

   Both runs: rc 0, ERROR/FATAL 0, no new dump.

## Done when

- The Report names the cause with the logged values, the fix, and the two proof runs.
- The Report ends with "Report complete."

## Report

Junior, 2026-11-05. **Case: regression.** I followed the step 0 amendment, and the existing logs answered it, so I did not rerun the old DLL.

### Step 0: regression, found from the logs

c75a8 and c75a9 ran the same scenario (`c75a-mech.txt`) with the same config lines and walked the same on-foot path. The rot rows, before the mech enter:

| run | deployed DLL | rot[0..4] own pawn (BP_PlayerCharacterMI_C) | Z at (-759, -1098) |
|---|---|---|---|
| c75a8 (10:35) | f555545810eb3bdd | MovementMode 1, CurrentFloor flags 0x3 | 103.5 |
| c75a9 (10:40) | 022a9d6376d69c08 | MovementMode 3, CurrentFloor flags 0x0 | 103.2, then 102.4 |

The only new on-foot behaviour in the c75a9 client log is card 075's aggregator push (`card019_sync_aggregators`, `mod/src/ubw/attributes.cpp`). It runs in card 019's client movement re-init and calls `SetNumericAttributeBase` for all 14 movement attributes of the trooper. The log shows each base pushed unchanged, including:

`PostAttributeChange(CapsuleCollision_Radius) 0.000 -> 0.000 on BP_PlayerCharacterMI_C`
`PostAttributeChange(CapsuleCollision_HalfHeight) 0.000 -> 0.000 on BP_PlayerCharacterMI_C`

### Cause

The push calls `PostAttributeChange` for the capsule attributes with base 0. On the client, that sets the own trooper's capsule size from the attribute. The pawn then never finds a floor (flags 0x0), so it stays in `MOVE_Falling`, and the server's corrections snap it back up about every 50 ms. That is card 092's sawtooth.

The server never runs this re-init, so its copy keeps a full capsule and stands. The mech gets the same push, which explains c75a9's mech rows going to mode 3, flags 0x0 from rot[10].

Not measured directly: the capsule size after the old push. The fixed build skips the push, so it cannot log that value. The proof that this is the cause is that skipping only the zero bases brings back Walking. Fixed build: `card019: ... 10 attributes ...; capsule radius 40.00 half height 95.00 -> radius 40.00 half height 95.00`.

### Fix

- `card019_sync_aggregators` skips any attribute whose base is 0. A new aggregator's base is already 0, so for those attributes the push never fixed anything; it only fired `PostAttributeChange`. Every nonzero base is still pushed, so card 075's root cause 3 (the mech's WalkSpeed 750 -> 0) keeps its fix.
- New log: `c93_capsule_size` (reflected `CapsuleRadius` / `CapsuleHalfHeight` of the root component). The card 019 "bases pushed" line now shows the capsule before and after the push.
- Not added: FloorDist, the WorldStatic response, and the per-frame mode-change log from step 1. Step 0 found the cause, so they were not needed.
- Build: no warnings. DLL `7f734db36ab30611`. Function lengths: `card019_movement_fix` 38, `card019_sync_aggregators` 30, `c93_capsule_size` 11.

### Proof (lane 2, FAST=1, `UBWCOOP_C92_LOG=1`; DLL 7f734db36ab30611)

Both runs: scenario passed, host and client rc 0, FATAL/ERROR 0, WARN 0 on both sides, no new crash dumps.

| run | scenario | card092 rows | mode after landing (from row 44) | own Z p2p | snaps |
|---|---|---|---|---|---|
| c93a | c92-still | 1182 | 1 on all 1138 rows | 0.00 uu (camera 0.25) | 0 over 0.5 uu |
| c93b | c92-strafe | 1184 | 1 on all 1140 rows, no Walking -> Falling change | 167.40 (pad to lower ground) | 0 on flat ground; see below |

The first 44 rows of each run are the 100 uu drop after the warp (mode 3), which is expected.

c93b detail:
- On the flat pad (842 frame pairs at Z 296.15 ± 0.6), the largest frame-to-frame |dz| is 0.39 uu.
- There are 0 down-then-up sawtooth triples.
- The 110 upward steps over 2 uu are the pawn walking up from the lower ground, about 2.3 uu per frame while X/Y move about 5 uu per frame (for example (-1122.0, 1886.9, 143.24) -> (-1126.4, 1885.1, 145.27)). They are not corrections.
- The pawn stays in Walking on the slope down, so it never falls off the edge.

Before the fix, c92e (still) had 168 snaps with mean rise 1.96 uu, and c92d (strafe) had 150 with mean rise 4.39 uu, in mode 3 throughout.

Note: the user asked that movement-only checks like these run as one combined scenario, not one run each. I will do that from now on.

Report complete.

## Senior review

Senior, 2026-11-05 19:21. **Accepted.**

- Checked: `logs/c93a-client.log` has 1138 `card092:` rows in mode 1 and 44 in mode 3 (the warp drop). The card 019 line reads capsule radius 40.00, half height 95.00 before and after the push. The fix in `attributes.cpp` skips a zero base before `SetNumericAttributeBase`. `build/dwmapi.dll` and lane 2 are both `7f734db36ab30611`.
- Skipping step 1's logs was right: step 0 found the cause.
- Play build: `1ddc46f17e72752c` (card 091) has this bug. The senior saved `build/dwmapi-play-7f734db36ab30611.dll` read-only and pointed `docs/PLAYTEST.md` at it. playtest.sh deploys `build/dwmapi.dll`, which is the same build.
- Lesson: card 092's probe should have started with the existing card 014 `chain:` rows from older logs. They would have shown the regression in one grep.
