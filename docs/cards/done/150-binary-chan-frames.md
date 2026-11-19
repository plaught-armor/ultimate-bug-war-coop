# Card 150: Mod channel frames go binary, not UTF-16 text

Status: CLOSED, outcome in docs/CARDLOG.md. Was: queued 2026-11-08, from card 140's static trace. Budget: three runs. Bandwidth, not gameplay.

Read: card 140 Trace (hb61 freeze window: ClientMessage 265541 B in 705 calls), card 136 (binary E rows, 31 B
average), `chan.cpp`.

## Why

Most mod channel frames still go as UTF-16 text in ClientMessage RPCs, about twice the bytes of a binary form. In
fights and join replays this fills the host's upload to the client, and the engine stops sending actor movement
(card 140). Card 140 paces the frames; this card makes each frame smaller.

## Do

1. Static: list each text op with its bytes per frame in hb72, ranked by total bytes.
2. Move the top ops to the binary form card 136 uses, one encoder and decoder for the class.
3. One run: bytes per event type before and after, and the same apply counts on the client.

## Done when

- The ranked list, the change, the run lines. The usual Report items. It ends with "Report complete."

## Batched into card 140 run 3 (senior, 2026-11-08)

The senior batched this card into card 140's run 3 build (c140c). This is card 150's work, not card 140 widened,
and the result is written here.

### Ranked text ops (c140b, heaviest fight window, bytes per row on the wire)

| Op | Bytes per row | Form in c140b |
|---|---|---|
| GibPart | 2067 | text (rows batched per frame) |
| Pose | 920 | text with base64 payload |
| GrenadeExplode | 520 | text |
| GrenadeSpawn | 456 | text |
| Hit | 261 | text |
| Decal | 250 | text |
| Fx | 238 | already a blob (card 136 E rows) |
| WorldOp | 159 | text |
| Anim | 36 | text |

Text frames cost 2 wire bytes per char (the stamp units make the FString wide). Blob frames cost about 1.07 wire
bytes per byte (15-bit units, 2 bytes each).

### Design

- One new arg kind, `kChanArgsPacked` (-5). Senders still build a text frame and handlers still read one. The
  transport packs the text into a blob frame at the real send (`send_now`) and unpacks it in `receive()` before the
  ledger and the handler. The ledger's `on_send` sees the text frame and `on_recv` the unpacked text, so ledger kinds
  and the card 132 gate are unchanged.
- Packed ops: GrenadeSpawn, GrenadeExplode, Hit, Pose, Decal, GibPart. Decal is added because it is the next text op
  by bytes and shares the class. WorldOp and Anim stay text (small, rare).
- One encoder and one decoder (`ue/chan_pack.{h,cpp}`). Byte 0 is the form: 0 raw ASCII (the fallback when tokens are
  not smaller), 1 tokens. Tokens split at ' ' and ';', each a tag byte (delimiter, kind, small arg) and a value:
  canonical decimals as zigzag varints with the fraction digit count, lower-case hex, base64 of 16+ chars as bytes,
  short strings inline, longer strings through a host dictionary (4096 ids, 3..127 chars). Each connection has a set
  of the ids it has been sent; a def is marked only when its frame really goes, in wire order, so a ref always follows
  its def. A def names its id, so the client overwrites by id and never resets. A full dictionary sends the string
  inline. Nothing is dropped. Only text that formats back to itself takes a numeric kind, so the round trip is exact.
- `frame_bits` (the pace gate's cost) measures the packed wire form plus the stamp.
- Offline check: a native ASan/UBSan harness round-tripped sample S, M, R and Hit rows plus 200000 random strings
  (exact), and decoded 200000 random byte strings without a crash. An empty text is never packed: `wire_form` sends
  it as text. Packed ops always carry args.
- Card 140 run 3 also charges the GibPart window budget (4096) in packed bytes without the dictionary, an upper bound
  of the wire bytes, instead of text chars. The budget stays 4096.

### Predictions (c140c, the card 140 run 3 scenario)

- Bytes per row on the wire at least 3x smaller than c140b for GrenadeSpawn, GrenadeExplode, Hit, Decal and GibPart.
  Two exceptions, said before the run: Pose about 2.5x (its payload is already base64 bytes, so only the 2-byte-per-char
  text cost and the 4/3 base64 cost go), and Fx unchanged (already a blob).
- `card150: host pack` lines: frames > 0, raw a small share; `card150: client pack` lines: bad frames 0, undefined refs
  0 for the whole run.
- The client apply counts per op match the host send counts (the ledger acks every row; card 132 gate 0 mismatches).

### Result (c140c, DLL eaa42f9557ef6e75)

Bytes per row on the wire, the whole run (c140b, then c140c):

| Op | c140b | c140c | Smaller by |
|---|---|---|---|
| GrenadeSpawn | 453 | 90 | 5.0x |
| GrenadeExplode | 535 | 98 | 5.5x |
| GibPart | 1006 | 323 | 3.1x |
| Hit | 271 | 100 | 2.7x (prediction 3x: fail) |
| Pose | 915 | 395 | 2.3x (prediction about 2.5x) |
| Decal | 644 | 481 | 1.3x (prediction 3x: fail) |
| Fx | 140 | 143 | unchanged (already a blob) |

- Pack lines: host raw 3 of 962 frames; the client unpacked every packed frame (bad frames 0, undefined refs 0, all
  windows).
- Hit and Decal miss 3x. I have not traced why. Decal's average includes the rejoin replay's long batched rows.
- The card 140 gate stall (card 140 run 3) hides the apply-count check: the connection closed at reason 65. Report
  complete.

### Open items (senior, 2026-11-08; they do not block)

- Hit packs 2.7x and Decal 1.3x, under the 3x prediction. Not traced.
- c140d (card 140 run 4): Fx (19 KB/s offered in the fight, already a blob) and Pose are now the largest mod loads.
