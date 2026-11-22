# Card 385: client clock offset taken from a sample that waited out the map load

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: fix written (senior), built in b70 (`6109d0898f23ed7d`), not proven: no stalled round in `c373b70` or `playb70`. Found in run `c373b69b` (b69), 2026-11-15.

## Defect (seen)

`c132_ledger_clock_offset` (card 313's only caller) returned the PC's last closed round, else the first round's samples
so far. A new PC appears at every map start, and its first samples sat in the queue during the load. Client log
`c373b69b`: `card132: client clock BP_PlayerController_C offset 2788 ms (min recv - sent 2788 ms, rtt 0 ms)`; every
other round in the run is -9 to -83 ms and the playlog clock rows give 5 to 6 ms. One row later:
`WARN card313 ... BP_TentSpawner_HumanGameplay_NoTent_C_3|Timeline ... age -2006 ms (host sampled t=413394513, clock
offset 2788 ms) outside 0 to 2000, position applied as sent`. The true age was about 780 ms, so that timeline ran about
0.8 s behind the host. The wrong offset holds until the next round closes (30 s), which is the window of the join
replay's 'L' rows.

## Fix (`mod/src/ubw/ledger_c132.cpp`, `sprint.cpp`, `hooks_internal.h`)

Client: the offset is the session's lowest (received - sent) minus half the session's lowest measured round trip, kept
on the ledger and not per PC. Both clocks are tick counts, so the difference is fixed for a session and a sample can
only be late. `c132_ledger_session_end()` drops it where `g_client_session` goes false (three sites in `sprint.cpp`).
The per-PC `offset` / `offset_set` fields had no reader left and are removed; the `card132: ... clock` round row stays.

## Predictions

Any two-map client run: 0 `WARN card313 ... outside 0 to 2000`; `card313` age rows after a map start between 0 and the
round trip plus the replay delay. Not covered: the first join of a session, where every sample so far may have waited out the load (the row then
still WARNs and applies the position as sent, as before); clock drift between machines over hours (tens of ms, against a 2000 ms
window).

## Second variant (2026-11-15, `c371b69`, `c371b70`)

The stalled sample can be the rtt instead of the delta: `client clock ... offset -993 ms (min recv - sent 1 ms, rtt 1988 ms)` after a restart from checkpoint. Before b70 that offset was used for up to 30 s; the card 313 age came out about 980 ms too large, inside 0 to 2000, so no WARN and the relayed timeline sat about 1 s ahead. b70 takes the session's lowest rtt, which covers it. Not proven: neither run relayed a timeline after the restart.

## Senior check 2026-11-16 (b161 to b168 logs)

Stalled rounds of the second variant did occur: 7 client rows with `rtt` 1890 to 2196 ms and per-round offset -944 to
-1094 ms (`c423b162a` 2, `c447b161a`, `c447b163a`, `c448b161a`, `c448b163a`, `c448b166a`, `c448b167a`, `c448b168a`;
for example `c448b168a` at 527704059: `offset -992 ms (min recv - sent 4 ms, rtt 1993 ms)`, rounds before and after
-4 and -9 ms). 0 `WARN card313` in all 30 client logs of b160 to b168 (8 to 22 `LoadMap` each). No first-variant round
(delta in the thousands) occurred.

This is not proof for the second variant: a -992 ms offset gives an age about 990 ms too large, still inside 0 to
2000, so the old code did not WARN either. The offset the client actually uses (`c132_ledger_clock_offset`, session
minimum) is logged only inside the WARN rows. The `rtt 0 ms` row right after a reload (`c315b168a` 526629256, offset
8 ms) is the per-PC print of a new PC with no round trip yet; the reader does not use it.

Owed, small (next build batch): the `card132: client clock` round row also prints the session offset
(`session offset %d ms (low delta %d, low rtt %u)`). Done when: in a run with a stalled round the session offset on
that row stays within 20 ms of the rounds around it. Then close.

## b170 row result: second variant confirmed, fix for b171 (2026-11-16)

The session row shows the bug (junior, b170): `c448b170a` after the scripted reconnect, first round trip 1973 ms (load
hitch), `session offset -982 ms` for 3.1 s (530290742 to 530293824), the join replay's window; `c423b170a` 530833765
`session offset 12 ms (low delta 12, low rtt 0)` until the next round (-12). Cause: `c132_ledger_session_end` clears
the minima at a rejoin, and the first round trip after it can be a stall.

Fix, scratchpad `c385b_patch.py` (1 edit, `c132_ledger_clock_offset`): a session round trip over 400 ms counts as not
measured (0), so the error is the one-way latency at most. The round row's `session offset` prints the value the
function returns (junior, same build).

Done when: `c448` on b171, the round rows after the reconnect read a session offset within 20 ms of the session's
other rounds; 0 `WARN card313`.

## Closed

Closed 2026-11-17 (senior), build b173 `dedb14d000d6ec36`: c448b173a: reconnect sample rtt 2292 gives session offset 0, other rounds -8 to -19, 0 WARN card313.
