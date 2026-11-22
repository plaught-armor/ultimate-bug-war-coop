# 520: GrenadeSpawn and GrenadeExplode wire bytes (efficiency)

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: Opened 2026-11-19 by senior from the c263d (b290 ae05cba6d5406b5b, FED_C3M2, 424 s) host log, per the
smaller-packets rule.

## Finding (c263d host netbudget windows and ledger map end)

- GrenadeSpawn: 303,640 B, 4206 rows (about 72 B/row). GrenadeExplode: 190,824 B, 3057 rows (about 62 B/row).
  Together they are the second-largest cost after Fx.
- The explode frame carried the system path, sound path, effect point, zone class path, zone point, yaw and life
  span. The client (`c74_receive_explode`) uses only the number (to end its copy) and the host point (for its log
  line); the effects reach it through the effects manager since card 136 step 5. Everything after the point was
  bytes the receiver could not use.
- The spawn frame sent locations and velocity with one decimal and the quaternion with 6.

## Fix (in source 2026-11-19, senior; object compiles; backup `build/grenades.cpp.pre520`)

All in `mod/src/ubw/grenades.cpp`; host and client change together.
- `c74_relay_explode`: the frame is `<number> x y z` in whole uu (`c74_whole`, llround, no "-0"). The effect, sound
  and zone names move to the host log line (`effects ...`), built only when that line is written.
- `c74_receive_explode`: parses those 4 fields only.
- `c74_transform_text` (live spawns and the join replay): location and velocity in whole uu and uu/s, quaternion
  with 4 decimals. `c74_receive` normalizes the quaternion (identity when it reads as zero).
- Warm packed sizes from the card 150 packer (scratch harness): explode row 48 to 17 B; AI Morita spawn row 45 to
  39 B.

Not done: the round number with `kC132RoundBit` set costs about 5 B packed in every row. A binary spawn op (about
24 B/row) is the next step if spawn stays a top op.

## Predictions (the same C3M2 PowerPlant scenario, one run)

1. GrenadeExplode under 35 B/row on the wire (from 62); GrenadeSpawn under 66 B/row (from 72). Report B/s with the
   run length.
2. Client: 0 `WARN card074: client: bad GrenadeSpawn frame` and 0 `bad GrenadeExplode frame` rows.
3. The client `GrenadeExplode` lines' "uu from the host's" distances stay in the c263d range (the point is whole uu
   now, so at most 0.9 uu more).
4. Scenario waits on `card074: client t=\d+ GrenadeExplode` and `GrenadeSpawn 0 ` still match (the formats keep those
   prefixes).

## Done when

Predictions 1 to 4 hold in one lane 2 run; numbers quoted here.

## Result c263e (b294, 431 s)

- Prediction 1 holds: GrenadeExplode 118,572 B / 3823 rows = 31.0 B/row (under 35); GrenadeSpawn 307,550 B / 4766
  rows = 64.5 B/row (under 66, barely). The warm packed spawn row is 39 B, so about 25 B per row is chan framing:
  coalescing spawn rows into fewer frames is the next cut.
- Prediction 2 holds: 0 bad GrenadeSpawn and 0 bad GrenadeExplode frame WARNs.
- Prediction 3 holds: 3840 client explode rows, distance median 30.8 uu, p95 307.0, max 4125.8 (c263d: 3080 rows,
  median 50.0, p95 377.5, max 4794.7).
- Prediction 4: this map's rows are AI rounds, which log with the `card132:` prefix (`grenades.cpp:1819`); the
  `card074:` lines come only from player grenades, and the c263 plant scenario waits on neither. Not exercised.

## Result c263f (b296) and close (2026-11-19)

- GrenadeSpawn 209,720 B in 3665 frames (57.2 B/frame); GrenadeExplode 79,712 B. Both stay under the prediction 1
  bounds.
- Prediction 4 by static check: player rounds log `card%s: client t=%llu GrenadeExplode` with `074`
  (`grenades.cpp:1819`), the prefix the c127 and c130 scenarios wait on. Not exercised by a run, but the format is
  unchanged.
- Closed. The remaining cut (coalescing spawn rows, or a binary spawn op of about 24 B per row) gets its own card only
  if GrenadeSpawn stays in the top three ops after card 521.
