# Card 589: card132 gate miss on a BP_PlayerPresence_C trigger state

Found in c582b414b (b414 `008b939213472e76`, c372-c3m1): the client's card132 round 4 read 1 mismatch,
`WARN card132: mismatch BP_PlayerPresence_C BP_PlayerPresence_C_8 triggers host 0001 client 0000; no row sent`
(client t=168602075). The other rounds of that run, and both b414 c582 runs on the host, read 0.

## Next step

Static: what sets the `triggers` bit on a BP_PlayerPresence_C on the host, and which op (if any) is meant to carry it
to the client ("no row sent" says none did). Not chased yet (senior, 2026-11-21).

## Merged into card 581 (senior, 2026-11-21)

Status: closed as a duplicate. This is the open PlayerPresence part of card 581. The c582b414b trace (the client's
copy of the seated host trooper enters `_8` every frame and consumes it; the host's trooper never enters it), the
next-build diagnostic (location and attach parent in the card245 overlap row) and the fix direction are in
docs/cards/581-digest-warns-during-host-seat-ride.md, section "c582b414b (b414)".
