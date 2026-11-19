# Card 441: AI round spawn and impact rows are sent as text with full asset paths

Status: open, not coded. Found 2026-11-16 in the wire audit of run `c423b136a` (b136 `7191143d5e2d10f1`, FED_C2M1,
165 netbudget windows, about 825 s). Low priority: after the playtest.

## Numbers (host log, netbudget "chan bytes by op" summed over the run)

Host sent 3,893,379 B in total, about 4.7 kB/s average.

| Op | Bytes | Rows | B/row | Share |
|---|---|---|---|---|
| Fx | 1,765,464 | 18,689 | 94.5 | 45% |
| Pose | 639,944 | 1,793 | 356.9 | 16% |
| GibPart | 361,200 | 3,534 | 102.2 | 9% |
| GrenadeSpawn | 240,898 | 3,231 | 74.6 | 6% |
| GrenadeExplode | 225,182 | 3,075 | 73.2 | 6% |
| WorldOp | 145,013 | 1,825 | 79.5 | 4% |
| Dropship | 115,871 | 709 | 163.4 | 3% |
| Anim | 103,579 | 3,989 | 26.0 | 3% |

GrenadeSpawn plus GrenadeExplode are 466 kB, 12% of all host bytes, about 0.56 kB/s. Nearly all of them are AI
trooper rifle rounds (`BP_ProjectileInitData_AIMoritaRound_C`, launch numbers past 3000 in this run), about 4 rounds
per second.

## Cause

- `c132_relay` (grenades.cpp) builds each round's frame with `c74_transform_text`: decimal text holding the round
  number, the init data class's full package path and name, the position, a quaternion with 6 decimals and the
  velocity.
- `c74_relay_explode` builds each impact as text with up to three asset paths (system, sound, zone class), three
  positions, a yaw and a life span. A rifle round's impact has no zone, so 5 of its numbers are always 0.

## Fix direction

One binary row per round and one per impact, for rounds only (`kC132RoundBit`); grenades keep the text rows (they are
rare and carry a zone).

- Spawn: round number, a class index, position (3 floats), velocity (3 floats). The client builds the rotation from
  the velocity direction. About 30 B against 75 B.
- Impact: round number, position, a system index and a sound index. About 20 B against 73 B.
- The index is into a small host table of asset paths; the host sends a path once per client the first time it uses
  an index (and again in the join replay), so no per-process name index crosses the wire.

Expected saving: about 60% of the two ops, about 0.3 kB/s on this map.

## Proof when coded

Same scenario: GrenadeSpawn and GrenadeExplode B/row in the netbudget rows fall below 35 and 25; the client
`card132` round ledger shows the same applied counts as before; a screenshot of AI trooper tracers on the client.

## Checked and not carded: the peak window

The largest 5 s window of the run (158,402 B at t=499383191) is the join replay of the rejoin step (line 500 of
the scenario): objective 312 frames, door 182, world 309, decal 500, effects 499. It is paid once per join.
About 250 of the 312 journal frames are `SetObjectiveVisibility` toggles: the game flips the markers of 8
sub-objectives about 32 times each in 780 s. They cost 16 kB once per join and 3.1 ms on the host; card 066's
compaction bounds the journal at 512 frames. No change.


## Senior re-read (2026-11-16 22:50, static): the expected saving is far smaller than stated

Both ops are `kChanArgsPacked` (card 150 codec, `mod/src/ue/chan_pack.h`): on the wire each decimal token is one
tag byte plus a zigzag varint of its digits, and the class path is a 2 to 3 byte dictionary ref. So the 75 B spawn
row is already 22 B of fixed frame cost (card 431) plus about 53 B of numbers: round number 6 B (the round bit
forces a 5 byte varint), path ref 3 B, position about 13 B, quaternion at `%.6f` about 20 B, velocity about 13 B.
A hand-made binary row would be 22 + about 29 B = 51 B: a third off these two ops, about 4 % of host bytes, not
the 60 % written above (that number assumed text on the wire).

Cheap variant if these rows are touched again for another reason (do not build for this alone): quaternion at
`%.3f` (about 8 B a row) and the round number sent without the round bit as a negative decimal (about 3 B a row on
both ops). The larger cost is the 22 B fixed part, which is card 431.

## Closed (2026-11-17 13:09)

Not built: the measured gain is about 4 % of host bytes and the fixed 22 B part belongs to card 431. The cheap variant was moved to card 431 (section "From card 441"). This is a merge, not a fix.
