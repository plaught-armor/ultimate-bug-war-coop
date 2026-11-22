# 509: The downed-leaver Up frame reads its guid through the leaving connection

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: Opened 2026-11-19 by the senior, from the junior's c448b282a finding (b282 1765d1a2dd105abb).

## Finding

- Host: `WARN card508: host leaver pawn id 124879 (slot 1) was Downed: guid 0x0 not sendable, no Up sent`.
- `c508_leaver_up` gets the guid from `c41_guid`, which reads it through `c41_package_map(world)`. While the leaver's connection is closing, that read returns 0.
- In this 2-player run nobody was left to tell, so nothing was lost. With 3 or more players, though, if the first connection leaves while Downed, the other clients could keep a stale Downed entry. That is the card 508 bug again. c502 proved only the second connection leaving.

## Do

1. Store the pawn's guid in C43Entry when the entry is made or goes Down, while every connection is live. `c508_leaver_up` sends the stored guid, so it never reads the guid at leave time.
2. Build the recipient list before the guid check. With 0 recipients, log a plain `no other remote PC` line, not a WARN.

## Done when

On the next CLIENT2=1 bug-batch run, the first client connection (slot 1) leaves while Downed:
- The host logs the card508 `Up sent to 1` line with a nonzero guid.
- Client2 logs `card043 ... Downed -> Up` for that pawn.
- 0 `mismatch Player P:` lines after it.
- A 2-player leave logs `no other remote PC`, not the WARN.

## Result (2026-11-19, b283 `4c265abd554c8232`, run c502c1b283b)

Closed. In the 3-player run the first client connection left while Downed. After its rejoin it held slot 2, not slot 1, but the stored guid makes the slot irrelevant.
- Host line 9292: `card508: ... leaver pawn id 125951 (slot 2) was Downed: guid 0x270, Up sent to 1 of 1`.
- client2 line 6173 logged `other pawn guid 0x270 (host slot 2) Downed -> Up` 13 ms later.
- Both clients logged 0 `mismatch Player P:` lines. Digest round 13 after the leave: 1515/1515 actors, 0 mismatches.
- The 2-player `no other remote PC` line was not reached in this run; it is accepted by static check. `c508_leaver_up` (fire.cpp:1181) returns on 0 recipients before the guid check. The next 2-player c448 run shows it in passing.
- WARN classes are the same as in c502c1b282a. About 163 s of idle came from the FAST=1 dropship try waits in the scenario; those were removed from c502-c1m1.txt the same day.
