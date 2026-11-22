# Card 513: cut idle time in objective runs (measured on c504k1)

## Finding

`tools/idle_report.py c504k1`: the host command span was 1268 s. Only 159 s were inside commands; 1109 s were waits
between them. Removable parts with a log row behind each:

1. **Satchel plant clear never ran.** `tools/drive.py:1173` sends `clear 1500 kill max-left=99` raw through `send()`.
   Gate words are stripped only on scenario lines, so the DLL skipped all 5 (`WARN cmd: 64 "clear 1500 kill
   max-left=99" skipped: clear takes <radius> ...`). Bugs stay alive during the 3.2 s hold; c504s3 Park_SO1_B try 1
   was interrupted at progress 1.88.
2. **150 s tail.** When Metro_O1 is not complete, the two `try ... wait state map=FED_C4M2` lines (90 s + 60 s) still
   run to their timeouts (drive "dead time: timed-out try waits 150.1 s").
3. **Go settle fixed at 1 s.** 158 `objective go` commands, median 0.995 s each, 158 s. The settle
   (`testrun.cpp` ObjectiveGo, `c284_go_settle`) waits the whole c.ms even when every pawn stopped at once.
4. **VOID loops** (927 s): cards 512 and the drive VOID cap, measured in c504k2.

## Do

1. `drive.py:1173`: `clear 1500 kill`. Then check every `send()`/`post()` string in drive.py for gate words; a gate
   on a raw send goes through `send_gated` or is dropped.
2. Level end counts as done (senior, after c504k2): Metro_SO2's presence opened WB_TrooperLevelEnd with no
   Complete row (lesson 16). drive.py `done_rx` takes the host card018 WB_TrooperLevelEnd row, and `objective do`
   reports the step done without the client replay wait. gen_c244 END_RX takes the same row, so the loop ends. The
   tail is the gen_c243 END path: Next until 3D_Frontend, client reconnect, ui continue/Enter until Browse
   FED_C4M2, both in FED_C4M2. Every tail line is `when host last "<level end row>" "."`, so a run with no level end
   skips it (no 150 s wait).
3. DLL: the go settle ends early once every placed pawn is in walking mode and has moved under 5 uu over the last
   300 ms (at least 300 ms after the warp). The 1 s cap and all settle rows and fail checks stay. Trace first: the
   tick that polls the settle, and whether a client pawn's position correction can land after 300 ms (c504b4 drift
   461 uu). If it can, keep the full settle for client pawns.
   Trace (2026-11-19): `c63_continue` polls ObjectiveGo every engine tick, so `c284_go_still` runs per tick from
   the first tick after the warp. The host reads every placed pawn, the client's too, on the server. The server
   moves its copy of a client pawn only by simulating that client's ServerMoves. A ServerMove sent before the client
   saw the warp would show as movement and restart the 300 ms window. An idle client sends at least every 1/12 s, and
   the pair is local, so those land well inside 300 ms. The c504b4 461 uu drift was a fall: mode not walking, which
   also restarts the window. c504k2 drift rows: 290 host and 290 client, all mode 1, 103 and 105 of them over 5 uu
   (max 534 and 596). The early end waits for those to finish. Verdict: early end for client pawns too.
4. Every run report includes `tools/idle_report.py <tag>` output next to the k1 numbers.
5. Stay follows encounter progress (senior, after c504k2, 599 s of 1043 s idle was stay clears). Static source:
   `tools/kismet/kis.py` listing `build/kis/c4m1.txt` (recovered 2026-11-19; offsets match card 244). Stadium_SO2 has no
   timer and no encounter gate: ubergraph 8598 `if not (inStadium) goto 8891`, 8891 is the 10 s
   `CallFunctionWithDelay 'Add Stadium_SO4'` retry; inStadium is set true at 5911 by BP_PlayerPresence_C_5's enter
   event (5729) and false at 6119 by its leave event (5937). The k2 retries were players outside C_5 (card 514).
   Its CompleteSub (8740) runs inside the Add Stadium_SO4 path (8598). In c504k2 the script retried
   `CallFunctionWithDelay 'Add Stadium_SO4'` every 10 s from 8231025 until the Add at 8326126, and SO2 closed 7 ms
   later. Stadium_SO4 (count 2) is a counter step (8976) on encounter EM_2 (8928): each close is an encounter end.
   drive.py `stay` on the last try: a period whose clear killed more than 0, or that logged a card111 wave or
   card245 StartEncounter row, keeps the stay; 3 periods in a row with neither end it (no done row, VOID); hard cap
   300 s. Checked against the logs. The c504k2 Stadium_SO2 window (8152973-8326133) never had 3 quiet periods in
   a row (at most 2, at 8313364/8318605), so the stay would have held to the close at 173 s. c504b4 C2M1
   LOS_O04_SO02's 59 s hold killed in every period but 2. Tries 1 and 2 keep one period.

## Done when

- Next C4M1 run: 0 host `skipped:` WARN rows for clear; the satchel plant rows show `clear: killed`.
- No tail wait when the level did not end; on level end the run reaches FED_C4M2 on both sides.
- Shots carry the do number (`c504-<step>-d<N>-try<M>`), no overwrites.
- Go total under 80 s for about 158 goes, with no new drift or inside failures against c504k2.

## c244b443a result (b443, 2026-11-21)

- Pass: 0 host `WARN ... skipped:` rows for clear (the 15 `skipped:` rows are card096 pause, card336 presence centre in
  BP_FallZones_C and hook lists). 29 `clear kill:` rows killed more than 0.
- Pass: the level ended and both sides reached FED_C4M2 (host card045 Browse at 194290391).
- Pass: shots carry the do and try numbers (`c244b443a-c4-Metro_SO2-d1-try2` form), no duplicates.
- Pass: idle_report: 37 goes in 12 s total (median 0.33 s); dead time 21.6 s in 3 timed-out try waits.

## Closed (senior 2026-11-21)

Proved in c244b443a (b443 da2b501a10253111, C4M1): see the junior's result row in docs/STATE.md (2026-11-21).
