# 508: A player who leaves while downed stays "Downed" in the other clients' life table

Opened 2026-11-19 by the senior, from c502c1b280a client2 round 12 (b280, FED_C1M1, three games).

## Finding

- The client was downed at 752939031: host Down seq 659, and client2 logged `card043 ... other pawn guid 0x270 (host slot 2) Up -> Downed` at 752939043.
- At 752939179 that client disconnected (scenario cmd 31). Host `c43_logout` (fire.cpp:1168) removed its entry and sent nothing.
- Client2 destroyed pawn 0x270 at 752939226, but its g_c43 entry stayed Downed. `c43_client_step` (death.cpp:1727) removes only Up entries after the StandUp window.
- Round 12 at 752969510: `WARN card132: mismatch Player P:2 presence host 0 client 1; last row seq 659 (Down)`. The entry would stay until the map ends.
- What a player sees: nothing directly, because the rejoined pawn has a new guid (0x37c). But each stale entry holds one of the kC43Max table rows. Once the table is full, a real teammate's Down is dropped (`WARN card209: client Down ... dropped: entry table full`), and that teammate would be shown standing while the host has them downed. It also leaves a digest mismatch for the rest of the map.

## Do

1. Host `c43_logout`: before `c43_remove(i)`, when the entry is in a down state, send every other remote PC one Up Down frame for that pawn. Take the guid from `c41_guid(pawn)`, because the roster may no longer hold the leaving PC. Clients then drop the entry after the StandUp window, as they do after a revive.
2. Do not send that frame to the leaving PC.
3. Log one host line per frame sent: "card508: host t=... leaver pawn id N (slot S) was <state>: Up sent to K".

## Done when

On the next bug-batch run with CLIENT2=1 where a downed client disconnects (c502-c1m1 cmd 31 already does this):
- The host logs the card508 line with K = 1.
- Client2 logs `card043 ... other pawn guid ... Downed -> Up`.
- No `mismatch Player P:` in any later digest round.

## Closed 2026-11-19

Proved in c502c1b281a (b281 5152ba17f51b94d0, FED_C1M1, CLIENT2=1):
- Host at 753947170: `card508 ... leaver pawn id 124280 (slot 2) was Downed: Up sent to 1`.
- Client2 at 753947180, 10 ms later: `card043 ... Downed -> Up` for that pawn.
- 0 `mismatch Player P:` lines in the digest rounds after it.
