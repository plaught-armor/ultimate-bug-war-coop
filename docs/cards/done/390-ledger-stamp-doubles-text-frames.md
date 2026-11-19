# Card 390: the ledger stamp doubled every text frame on the wire

Status: accepted on b77 `0f1e33f879593a62` (lane 2 only; play build 7 in lane 1 is still b76).

## Finding

The card 132 ledger stamp (`mod/src/ue/chan.cpp` `stamped`) appended four 15-bit chars to every frame. FString
serialization is ANSI only when every char is below 0x80, so the stamp made every text frame UCS-2: 2 bytes per char.
The pacer's `frame_bits` counted text frames at 1 byte per char, so it also under-counted them by about half.

Also traced: `Fx` rows are already binary blob frames filled to 2.6 kB. Correction (card 391): the `QueuedBits`
jump at a map start was first called the engine's own first actor replication; that was not supported
(`ReplicateActor` wrote about 2.9 kB in that window). It follows the client's load hitch; see
`docs/cards/391-map-start-burst-after-client-hitch.md`.

## Fix

A frame whose chars are all below 0x80 takes a narrow stamp: mark 0x1D, then seq and ms (30 bits each) as ten 6-bit
chars + 0x20. Wide frames (args, blob, packed) keep the old stamp. The receiver checks the wide mark first; a narrow
stamp's chars are never that mark. `frame_bits` counts the real stamped size. Host and client must run the same build.

## Proof: `c388b77` against `c388b76` (same chain, FED_C3M1 to C5M2)

| | b76 | b77 |
|---|---|---|
| `ClientMessage` bytes / calls | 924,004 / 4184 | 682,547 / 4069 |
| `WorldOp` B/row | 146 | 81 |
| `CollisionOp` B/row | 164 | 90 |
| `Dropship` B/row | 295 | 156 |
| `Fx` bytes / rows | 331,174 / 594 | 328,328 / 553 |
| reliable bunches outstanding, top 3 window peaks | 186, 218, 256 | 126, 161, 203 |
| `IsNetReady` false frames | 82 of 4753 | 36 of 3910 |

Scenario passed in 173.5 s, 0 FATAL/ERROR/CRASH, 0 ledger mismatch or seq gap WARN rows. One WARN row not in the b76
run: client `WARN card140: client hit BP_AI_WarriorBug_C ... 530 uu from the copy's ReplicatedMovement.Location`
(1 row; the Hit frame is packed and keeps the wide stamp, so its path did not change; watch for it).

## Leftover

`Fx` is now 48 percent of channel bytes. Row count at a map start (about 650 text rows) is unchanged; batching several
rows per frame would cut the reliable bunch peak further. Not measured under combat load.
