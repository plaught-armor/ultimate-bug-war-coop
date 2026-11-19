# 229 — Effect name table overflows on FED_C3M1

Found by soak runs c127-c3m1 and c127-c3m1-r2 (DLL `2928654b3a96090d`): `WARN card136: host fx name table full
(1024)`; the map-arm totals read `1024 names (1646 table full)`. FED_C1M1 and FED_C2M1 stay under the cap.

Cause: `kNames = 1024` and `kNameSlots = 2048` in `mod/src/ubw/fx_c136_net.cpp` (line 70). `host_name` returns -1
past the cap, so every effect row whose component, socket, owner class or placed key name comes after the 1024th
name is sent without it and the client cannot key it. FED_C3M1 needs about 2670 names.

## Do

1. Static: confirm the name index is not limited by its wire encoding or by any index type (host `slots` is
   `int16_t` holding index + 1; client checks `i >= kNames`; `r.socket`), and that the join replay of the whole name
   table has no smaller bound (frame size, rows per frame).
2. Raise `kNames` to 4096 and `kNameSlots` to 8192. Log the peak against the cap in the existing map totals line if
   it does not already show it (`%d names` is the count; add ` of %d`).
3. One run of `tools/scenarios/c127-c3m1.txt` (with the pending radius fix on line 234).

## Done when

- The run shows no `name table full` line, `0 table full` in the map totals, and the names peak under 4096.
- Client `misses: ... key` in the fx totals is not higher than in c127-c3m1-r2.
- Digest shows no class other than the known Pose:1 corpse (card 231); no new WARN class.

## Step 1 (junior)

I read the name index path in `mod/src/ubw/fx_c136_net.cpp` before any edit. Nothing limits the index below 4096.

- Wire encoding: every name index is a LEB128 varint. The N row writer is `put_varint(row, i)` in `frame_assets`.
  The event row writers are `put_varint(o, p.socket)` and `put_varint(o, p.placed)`, plus comp and owner_class
  through the same path. The reader is `get_varint` (up to 10 bytes, `uint64_t`). An index of 4095 takes 2 bytes.
  No field is fixed at 8 or 10 bits.
- Host types: `HostNames::slots` is `int16_t[kNameSlots]` and holds index + 1, so it holds up to 32767, which is
  above 4097. `n` and the row indices are `int32_t`. The hash probe is bounded by `kNameSlots`
  (`for k < kNameSlots`), and the full test is `t.n == kNames || t.slots[slot] != 0`. The map arm resets with
  `memset(g_host.names.slots, ...)`, which takes its size from the array.
- Client bounds: `client_name_row` checks `i >= kNames`, the event reader's `name` lambda checks `i >= kNames`,
  `client_socket` checks `r.socket >= kNames`, and `client_name` checks `i < kNames`. All of them follow the
  constant, and `ClientName names[kNames]` is sized by it. The `kNames` at line 670 is a local array name
  (`{"bIsActive", nullptr}`) and is not the same constant.
- Join replay: `frame_assets` sends the rows from `peer.names_sent` to `t.n` in one `host_flush`. `frame_add`
  sends the frame and starts a new one when the next row would pass `kChanBlobBytes` (2623 bytes, chan.h:71). There
  is no rows-per-frame count and no bound tied to 1024.
- Risk, not a limit: on FED_C3M1 about 2670 name rows (about 30 to 70 bytes each) go out in one tick at the first
  flush after the client's Pong, which is about 60 frames. The netbudget pace queue absorbs that burst. Run 1 already
  sent 1024 names plus 2906 map-arm rows that way, with a pace queue peak of 310 frames.
- Memory: the arrays are static globals (`g_host`, `g_client`). Raising them to 4096 rows of about 264 bytes adds
  about 0.8 MB on each side, and nothing goes on the stack.

## Run (junior)

- **Change and build:** `kNames` 4096 and `kNameSlots` 8192, and the host totals line now reads "<n> names of <cap>".
  The build had no warnings. DLL `20cf859dd191a8fe`.
- **Run and drive:** run c229-c3m1 with the c127-c3m1 header command, deployed with FAST. The drive returned rc 0 and
  the scenario passed. There were no FAIL lines. There were two TRY-FAIL lines, 154 and 155 (the Beach_SO1
  CompleteSubObjective tries), the same as c127-c3m1-r2. Lines 233, 234 and 239 (the turret behind warp, radius 250)
  passed.
- **Name table:**
  - No `name table full` lines (r2 had 8).
  - The host map totals at the FED_C3M1 arm read `1516 names of 4096 (0 table full)` (r2 read
    `1024 names (1646 table full)`). The next map reads `140 names of 4096`.
- **Client fx totals at map end, against r2:**
  - misses 1 asset (r2: 1)
  - misses 346 key (r2: 394)
  - misses 250 target (r2: 305)
  - live peak 641 (r2: 729)
- **Digest:** 9 rounds, all with 0 mismatches. The r2 Pose:1 corpse (card 231) did not show this run.
- **ERROR:** 0 ERROR and 0 FATAL lines. The read-out hits are card067 "Unhandled" parameter names only. No new crash
  dumps.
- **Host WARN:**
  - card133 wire name 44 (r2: 44)
  - card157 HitClaim 5 (r2: 39)
  - attach 1
  - The card136 name table full class is gone.
- **Client WARN:**
  - bone check 7
  - pose stream 1
  - attach 1
  - card148 `client gib host part ... asleep above the ground` 24. This class is absent from r2 but not new to the
    project: c127-c3m1 run 1 had it 5 times, and so did c127-c1m1, c127-c2m1 and c227. Gib counts vary from run to
    run.
- **Host frame windows:**
  - All 10 s windows were 538 to 584 frames, except two.
  - One was 110 frames, during the map load.
  - The other was 480 frames: the first window after the client's first Pong, which holds the join replay and the
    one-flush name burst (see Step 1).

## Senior read-out (2026-11-12, accepted)

Diff is the two constants and the totals field. c229-c3m1 read by the senior: figures as the junior reports. The
old `1646 table full` counted refused look-ups, not names; the map needs 1516. Regression c229-c1m1: passed, 18
rounds 0 mismatches, 0 ERROR; three digest WARN lines of the round-overlap class (card 232, also on the DLL before).
Play build `20cf859dd191a8fe`.

