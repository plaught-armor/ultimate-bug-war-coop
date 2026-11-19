# 290 Client sees "lost connection to host" at level end; second level end after a rejoin

Playtest 2026-11-13 (user plays as client): "lost connection to host", twice. The two reports have different
causes. Part A and B are mod defects, traced from the client log. Part C needs the host log.

## Trace 1 (client log `UBWCoop-menu-20261113-183002.log`, FED_C1M1)

| Line | Client t (ms) | Row |
|---|---|---|
| 12132 | 255814542 | `card260: client EndLevel (own script) (t=255813775, caller exe+0x1424549)` |
| 12134 | 255814555 | `card260: client relay dropped, already ended` (host ended 13 ms later: correct) |
| 13087 | 255827787 | client Browse to `3D_Frontend`, 0 options; `reconnected (1): try 1, 2649 ms after the drop` back into C1M1 |
| 17224 | 255855751 | `card260: client EndLevel (own script)` again, about 25 s after the rejoin, on a level the host already ended |
| | 255867798 | client Browse to `3D_Frontend` with `FromLevelComplete`; `reconnected (2)` into C1M1 again |
| | 255888334 | user left by the pause menu QUIT; rejoined by the lobby panel at 255897738 |
| 24733 | 255906813 | `card260: client EndLevel (relay) (t=255906070)` |
| | 255913549 | `card052: network failure 5 (FailureReceived)`, "Host closed the connection."; 255913550 failure 3 (ConnectionLost), "Your connection to the host has been lost." |
| | 255916921 | auto reconnect try 1; connected to the host's `3D_Frontend` at 255918437 |

## Part A: the host's level-end travel is shown to the client as a lost connection

The host leaves the level (level complete, to the frontend or the next mission). The engine closes the client's
connection. The game's network failure handler shows "Host closed the connection." and "Your connection to the host
has been lost." The mod then reconnects (card 045), but the user has already seen a failure.

Fix: a host travel is not a failure. Before the host leaves a map by its own travel (level complete, restart,
load save, quit to frontend), it sends one row naming the travel. A client that got that row within the last
`kC290TravelMs` (10000 ms) treats failures 3 and 5 as the travel: no failure dialog, no failure sound, the card 045
follow runs as now. Without the row the failure shows as today.

- Step 1 (static): name the function that shows the failure dialog (the `HandleNetworkFailure` listener the card
  052 rows come from) and the host travel entry points (card 045 lists the client ones; the host's
  `ServerTravel`/`OpenLevel` callers after `EndLevel`). Write both into this card before coding.
- Rows: `card290: host t=<ms> travel row sent to <n> clients (<reason>)`,
  `card290: client t=<ms> failure <n> within <ms> ms of the host travel row: dialog skipped, follow runs`.

## Part B: the client runs EndLevel by its own script on a level the host already ended

Line 17224: after rejoining a host that sits on the level-end screen, the client's own script ended the level
again (second level-end screen, second `FromLevelComplete` Browse and reconnect). `detour_end_level`
(`objectives.cpp:1399`) lets a client own-script EndLevel through unless the relay ran first; `c260_map_end` resets
the state on every map load, so a rejoin forgets the level already ended.

Fix: only the host ends a level. The client's own-script EndLevel is skipped always (same rule as card 258 for
AddObjective); the relay is the only path. The host journals its EndLevel, so a client that joins after it gets
the row once it has loaded. Check line 12132 against this: there the client's own script ran 13 ms before the
relay, so skipping it costs at most the relay's transit.

- Rows: `card260: client EndLevel (own script) skipped: only the host ends a level (t=<ms>)`.

## Part C: connection timeout, cause not known (not codable yet)

Client log `UBWCoop-menu-20261113-184603.log`, FED_C2M1:

| Line | Client t (ms) | Row |
|---|---|---|
| 10674 | 256086297 | `WARN card132: client rows 292-308 pending and no ack from the host for 16947 ms` |
| 10683 | 256093685 | `card052: network failure 4 (ConnectionTimeout) in session`, "Elapsed: 20.01 ... Threshold: 20.00" |
| 11204 | 256118298 | reconnect try: `failure 4 (ConnectionTimeout) while the join was pending, 20026 ms after the join arm` |
| 13917 | 256136047 | host rows arrive again |

Nothing arrived from the host between about 256073685 and 256118298 (45 s), then it answered again. The client kept
sending (HitClaim rows to 256078524). Either the host's game stalled for 45 s or the path between the machines
dropped. Step 1 after the playtest: the host log between host t=82204449 and 82249062 (client t minus 173869236):
a gap in the host's rows means a host stall (find the last row before it and the caller); rows without a gap mean
a network drop. Card the stall if it is one.

## Sibling sweep (Part A and B)

| Host travel | Check |
|---|---|
| Level complete to the next mission | travel row sent, client follows, no dialog |
| Level complete to the frontend | same |
| Mission failed, restart mission | same |
| Load last save, load game | same |
| Host quits to the frontend by the pause menu | row sent; decide with the user whether the client follows or sees a message |
| Bonus or training levels | same entry points, or list the extra one |
| Host process exit or crash | no row: the failure dialog must still show |

## Predictions (one run, after the playtest, batched with 284-289)

- Host completes a level: host `card290` travel row; client `card290` dialog-skipped row; no failure dialog in the
  client screenshot; client connected to the host's next map by the card 045 follow.
- Client joins a host on the level-end screen: one `EndLevel (relay)` from the journal, zero `own script` EndLevel
  rows that run, zero `FromLevelComplete` Browse rows on the client.
- Host process killed: client shows the failure dialog as today.

## Step 1 answers (static, 2026-11-13)

- **Failure dialog.** `UEngine::HandleNetworkFailure` (0x043E04F0, card 052's hook) calls virtual slot +0x568 at
  0x1443E0664. That slot is `UGameEngine::HandleNetworkFailure_NotifyGameInstance` (0x03C195B0).
  - It runs `FindFunction("HandleNetworkError")` on the game instance (`GameEngine +0x1248`), then `ProcessEvent`
    with `{FailureType, bIsServer}`. The game's blueprint game instance implements `HandleNetworkError`, and that
    event shows the dialog.
  - The disconnect follows separately, at 0x1443E0694-0x1443E06D3 (the online session's `HandleDisconnect`, slot
    +0x2C0).
  - `UGameInstance::HandleNetworkError` (0x03C19570) is the same `ProcessEvent` call. The base
    `UEngine::HandleNetworkFailure_NotifyGameInstance` (0x043E07A0) calls it.
  - The card 052 panel shows "Connection lost: <name>" from `c52_failure` (`C52Status::Lost`).
- **Host travel entry points.** Every host travel reaches the host's `UEngine::Browse` while the old world and its
  connections are still up, and card 045's `c45_browse_enter` already sees it.
  - That covers `USTGameInstance::OpenLevel`, `RestartCurrentLevel`, `TransitionToLevel`, `TransitionToNextLevel`
    and `TransitionToNewGame`, `ReturnToMainMenu`, and save load through `OpenLevel`. All of them go through
    `UGameplayStatics::OpenLevel` → `SetClientTravel` → `TickWorldTravel` → `Browse`.
  - The URL options name the reason: `FromLevelComplete`, `FromLevelQuit`, else `travel`.
  - Seamless `ServerTravel` is not used by the campaign.
  - A host process exit or crash has no Browse, so no row is sent.

## As coded (uncompiled, 2026-11-13)

### Part A

- New symbol `UGameEngine_HandleNetworkFailure_NotifyGameInstance` (0x03C195B0; the first 18 bytes have no
  rip-relative operand), hooked by `detour_c290_notify` (lobby_ui.cpp).
- Host: `c290_host_travel` (world_c98.cpp) is called from `c45_browse_enter`'s host branch before
  `c45_host_browse`. It sends one `'V'` world row, not journalled, with the name set to the reason. It logs
  `card290: host t=<ms> travel row sent to <n> clients (<reason>)`.
- Client: the `'V'` row stores its arrival time (kept across LoadMap). `c290_travel_failure(type)` is true for
  failure 3 or 5 within `kC290TravelMs` (10000) of that row.
  - When it is true, `detour_c290_notify` skips the dialog event and logs
    `card290: client t=<ms> failure <n> within 10000 ms of the host travel row: dialog skipped, follow runs`.
  - `c52_failure` sets the panel status to None instead of Lost.
  - `UEngine::HandleNetworkFailure` still runs `HandleDisconnect`, and `c45_pending_failed` and the card 045
    follow are unchanged.
- Open points:
  - The failure sound is assumed to sit in the blueprint dialog event (not checked).
  - The row rides the connection the host's LoadMap closes. It relies on `UNetConnection::Close` flushing the
    send buffer, which already holds the bunch.

### Part B

- `detour_end_level` (objectives.cpp): a client EndLevel that is not the relay is skipped always, with the row
  `card260: client EndLevel (own script) skipped: only the host ends a level (t=<ms>, caller exe+0x..)`.
- The host sets `g_c260.host_ended`. `c260_join_replay(pc)` runs in `c50_join_replay` after `c98_replay` (first
  Pong). It sends `ChanOp::EndLevel` to the late joiner and logs
  `card290: host t=<ms> EndLevel replayed to late joiner <pc>: sent`.

### Part C

Not coded. It waits for the host log.

### Sibling sweep (static)

| Host travel | Answer |
|---|---|
| Level complete to the next mission or frontend | Browse with `FromLevelComplete`: row `level-complete` |
| Mission failed, restart | `RestartCurrentLevel` → Browse: row `travel` |
| Load last save, load game | `OpenLevel` → Browse: row `travel` |
| Host quits to the frontend by the pause menu | Browse with `FromLevelQuit`: row `quit`. The client skips the dialog and follows to the host's frontend (card 154 listen). The user decides whether that is wanted or should show a message. |
| Bonus or training levels | same `OpenLevel` path, no extra entry point found |
| Host process exit or crash | no Browse, no row: the dialog shows |

## Host log read (senior, 2026-11-13, `client/UBWCoop-menu-20261113-182510.log`, host t = client t - 173869236)

Part C is settled as far as the logs go: **the host did not stall.**

- Host rows are continuous through host t=82195000 to 82245000 (166 to 328 rows per 5 s, no gap over 3 s; the
  only gaps over 3 s in the whole log are at the menu and in the first map before the join).
- Last mod rows received from the client: `Clock` and `Ping` at 82197070 and 82197112, one clock row at 82220129.
- `card128: host t=82244516 connection ... timed out`: "Elapsed: 20.00 ... Threshold: 20.00", reason 4
  `ConnectionTimeout`, remote address the client's. So the host's last packet from the client arrived near
  82224500, 20 s after the client stopped receiving (client-side 82204449).
- `netbudget ... QueuedBits -33332 to -33332` from the window ending 82215126 on: the host queued nothing more to
  that connection for the last 30 s.
- Logout dropped a pace queue of 7159 frames.

Reading: the path host to client broke first, client to host about 20 s later; neither game froze. Open for the
junior (static): why the host stopped queuing bits 10 s after the break while its pace queue grew to 7159 frames
(a send gate of the mod that waits for an ack or a Pong would turn a short path loss into a permanent one). If
such a gate exists, that is the defect and it is fixed here; if not, part C is a network event and only the
client-side handling of parts A and B applies.

Third loss (FED_C3M1, client t=258535238, `reason 14 Unknown`, card 305 trace): the host shows no timeout and no
gap, only `card049: host BP_PlayerController_C left` and `Logout ... 0 frames` at 84666147, 0.1 s after the
client's close. The close came from the client side. The client rows before 258535238 name the caller; trace
them with part A.

Fourth: `card128` timeout at host t=85559248 is the client's crash of card 306 (client t=259408308 is host
t=85539072, plus the 20 s threshold).

## Part C open item answered (junior, static, 2026-11-13)

The host stopped queuing because of card 140's pace gate (`mod/src/ue/chan.cpp` `pace_gate`), reliable hold: a frame
waits while the PC channel has `kPaceRelHold` (256) or more reliable bunches out. Host netbudget rows: peak reliable
out 69 in the window ending 82205118, then 256 in every window from 82210121 to the Logout, with holds `reliable`
150, 266, 290, 245, 314, 299 per window and `bits 0`; sent bytes 0 from 82215126 on. QueuedBits sat at the engine's
clamp (-33332) because nothing was handed to it.

Reliable bunches out drop only when the client acks them. The client stopped receiving at host time near 82204449,
so it acked nothing after that, and the count climbed to the hold and stayed. The gate is a symptom, not the cause:
it held the mod's own frames only, it does not touch the engine's actor channels or keep-alives, and without it the
same burst would have passed 511 and closed the connection with reason 65 (`RPCReliableBufferOverflow`) instead of
the timeout. When acks resume the FIFO drains in order. No mod gate here waits on a Pong or turns a short path loss
into a permanent one, so part C stays a network event; only the client-side handling of parts A and B applies.

Side note: `longest wait 0 ms` in those rows is wrong while nothing is sent (`max_wait_ms` is set only at a real
send). A queue that never drains reports no wait. Not changed in this batch.

### Fix (build b7): pace window longest wait

`ue::chan::chan_pace_window` (`mod/src/ue/chan.cpp`) now also takes, for every live pace queue with frames, the age
of its head (oldest) frame at the last pace step (`g_pace_now_ms - e[head].enq_ms`). It folds that into
`max_wait_ms` before it returns and clears the window. A held queue now reports its true wait in the
`netbudget: host ... longest wait <ms>` row. The sent-frame wait in `pace_pop_send` is unchanged.

Prediction: in a reliable hold like the 82210121 window, `longest wait` grows by about the 5 s window length per
row while `bits 0` / `sent 0` hold. It is never 0 while the queue has frames.

## User decision 2026-11-14

Host quits to the frontend by the pause menu: the client follows to the frontend, no dialog. That is what the `quit`
row already does; no code change. Proof owed in a soak: one host pause-menu quit with the client in the map, client
row for the `quit` travel, no failure dialog, client ends on the frontend.

## Closed (2026-11-17 08:50)

Close-out proof, b177, run c294b177a: host QUIT from the pause menu with the client in the map. Since card 369 the
quit is a seamless ServerTravel, so there is no card290 `quit` travel row; the rows are host `card346: ... seamless
travel to "/Game/Maps/3D_Frontend/3D_Frontend?FromLevelQuit..." started by ServerTravel (2 players)`, client
`card350: client ... ClientTravel ... (seamless 1)` and `card052: ... world 3D_Frontend ... net mode 3`. 0 `card052`
failure rows after the mark. Session held: host `card113: new world, session players 2 held for 45000 ms (present 2)`
on 3D_Frontend, client still receives `card132: recv` rows in the hangar. Shots: client in the hangar with no mod
panel (by design, `lobby_ui.cpp` shows the panel on the host only, card 154), host in the hangar with the UBW CO-OP
panel. A "lost connection to host" dialog at a level end or host quit, in a playtest, reopens this card.
