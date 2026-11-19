# 294 When both players die the client restarts several times before the map start

Playtest 2026-11-13 (user plays as client): "when both players die, as client kept restarting until map start
screen showed up".

## Trace (client log `UBWCoop-menu-20261113-184603.log`, FED_C2M1)

| Client t (ms) | Row | What the user saw |
|---|---|---|
| 256804152 | `card043: own pawn Downed -> Out`, `host pawn Up -> Dead` | both dead |
| 256807163 | `CompleteDeath` (3001 ms after StartDeath), death screen constructed, 3 buttons disabled | death screen 1 |
| 256811644 | `CompleteDeath` again, callers `0x1424549 < 0x154F88D` (script call), second death screen constructed | death screen 2 |
| 256812014 | `ClientRestart` with a null pawn | |
| 256813612 | `OnRep_Pawn` new `BP_PlayerCharacterMI_C` | |
| 256814374 | `card045: respawn in place (new pawn, no map load)`, `2 of 2 death screens removed`, Health 96.8 at (192803, 837, 7584) | alive in the old map (restart 1) |
| 256816418 | `card045: try 1 at +839 ms: SetClientTravel`, Browse | connection dropped, load screen |
| 256821161 | `reconnected (1): try 1, 5582 ms after the drop`, world FED_C2M1 | map start screen (restart 2) |

The host chose a restart on its death screen. The client then got, in order: a second death screen, a respawn in
the old world about 2 s before the host left it, a dropped connection, and a reconnect into the reloaded map.
One host action must be one transition on the client.

## Step 1 (static plus host log, junior, no run)

1. Which host button ran (RestartMission, LoadLastSave or LoadGame) and its travel entry point; the host row near
   host t=82938000 to 82947000 (client t minus 173869236).
2. What respawns the client's pawn in the old world before the travel: the game's restart flow (`RestartPlayer`
   for every controller) or a mod path (card 043/045 respawn). Name the caller of the client's `ClientRestart`
   at 256812014 on the host side.
3. What calls the second `CompleteDeath` at 256811644 (script caller 0x1424549): the host's death relay, or the
   client's own script after the host's choice.
4. Whether the host landed at the map start or at its last checkpoint, and whether the client's world state
   after the reconnect matches it (the user saw the map start screen).

Write the answers into this card before coding.

## Fix

- Uses the card 290 part A travel row: the host sends it when it commits to a restart or a load from the death
  screen, before anything else changes.
- Client, from that row until the new map has loaded: keeps the one death screen it has (no second one is
  built), does not take a pawn in the old world (the respawn in place is held, not shown), shows the game's
  loading screen, and follows the host by the card 045 path with no failure dialog.
- A second `CompleteDeath` for a pawn whose death screen is up is skipped.
- A host respawn in place that is not followed by a travel (revive, checkpoint respawn without a map load) stays
  as it is: the hold ends when no travel row is pending.
- Rows: `card294: client t=<ms> host restart (<reason>): death screen kept, respawn in place held`,
  `card294: client t=<ms> second CompleteDeath skipped (death screen up)`,
  `card294: client t=<ms> restart done in <ms> ms: death screens built <n>, respawns in the old world <n>`.

## Sibling sweep (same class: one host action shown to the client as several transitions)

| Host action | Check |
|---|---|
| RestartMission from the death screen | one transition |
| LoadLastSave from the death screen | one transition; client state equals the host's checkpoint |
| LoadGame from the death screen or pause menu | same |
| Restart or load from the pause menu while both live | same, no death screen |
| Level complete (card 290 part A) | same row, same follow |
| Host dead, client alive, host picks restart | decide with the user: the client is pulled along or asked |
| Client dead alone (host alive) | no travel row: revive and bleedout unchanged |

## Predictions (one run, after the playtest, batched with 290)

- Both players die, host clicks RestartMission: client log has one death screen constructed, one `card294`
  restart row with `death screens built 1, respawns in the old world 0`, zero `card052` failure rows, one
  LoadMap; client screenshots between the click and the map start show the death screen or the loading screen
  only.
- Same with LoadLastSave: client pawn within 200 uu of the host's checkpoint position; digest 0 mismatches.

## Step 1 answers (static plus host log `client/UBWCoop-menu-20261113-182510.log`; host t = client t - 173869236)

1. The host clicked **LoadLastSave**. Host 82942345 (client 256811581): `card121: host save engine load runs
   (Load, context WB_PlayerDeathScreen_C, load pawns 1, destroy actors 1, caller exe+0x520B3EF)`. It is an in-place
   save load with no Browse and no LoadMap, so card 290 part A (sent only at `c45_browse_enter`) sent no travel row.
2. The game's save load respawned the pawns ("load pawns 1": the load destroys actors and spawns new pawns for
   both players): host pawn at the checkpoint (189030, 4189, 7363), the client's copy at (192803, 837, 7584) with
   Health 96.8. Host card045 "respawn in place ... per-map reset" and card043 restart (both slots Up) at 82943438.
   The client's `ClientRestart` (1 call in the netbudget window at host 82943655) is the possession of that loaded
   pawn, not a mod RestartPlayer. Then host card143 `Reload to BP_PlayerController_C ... sent` at 82943175: by design
   (`c143_receive_reload`) the client starts the card 045 follow (SetClientTravel) at once, so it reconnects. That
   reconnect is restart 2.
3. The second `CompleteDeath` is a script call during the save load (callers `0x0 < 0x1424549 < 0x154F88D`). The
   host has the same second CompleteDeath at 82942383 with the same callers, and builds a second death screen too.
   It is the client's own script reacting to the replicated load, not the mod's death relay.
4. The host landed at its last checkpoint in the same world (no map travel). The client rejoined into that world
   by the Reload (host connection closed 82947290; rejoin spawn 82952017 by card046 "at the origin (no PlayerStart
   free...)", beside the Up player at (183912, 6788, 7662); card143 Restore applied 82954097). The "map start screen"
   the user saw is the client's fresh map load for the rejoin, not a host map start. The rejoin spawn by the origin
   fallback is card 307's subject.

## Coded (uncompiled)

- `save_c121.cpp` `detour_c121_load` (host branch): a load with load pawns notes `c294_host_load("death-screen
  load" | "in-place load")`; `c294_host_step(engine)` (called from `hooks_ubw.cpp` beside `c293_step`) sends the card
  290 travel row on the next engine tick (the load has no engine at hand; the Reload follows about 800 ms later).
- `world_c98.cpp` travel row receive: calls `c294_client_travel(reason, t)`.
- `death.cpp`:
  - `c294_client_travel` starts the hold and logs `card294: client t=... host restart (<reason>): death screen
    kept, respawn in place held (<n> death screens tracked)`.
  - `detour_complete_death` -> `c294_skip_complete_death`: a CompleteDeath after one already ran (same pawn, or any
    pawn during the hold) while a death screen is up is skipped: `card294: client t=... second CompleteDeath
    skipped (death screen up): <pawn>, caller exe+0x...`.
  - `c45_respawn_check` -> `c294_hold_respawn`: during the hold (row within kC290TravelMs, or the card 045 follow
    running) a new pawn in the old world is held: no card 156 restore, the death screen stays, `g_c45.pawn` stays
    the old one. Row `card294: client t=... respawn in place held: ...`.
  - The hold ends at the new map's first pawn (`restart done in <ms> ms: death screens built <n>, respawns in the
    old world <n> (held <n>, new map)`) or, when no travel follows, the held respawn runs and `c294_shown` logs the
    done row with `no travel followed`. WARN when more than one death screen was built or a respawn was shown.
  - `c45_client_screen` counts death screens built during the hold.

Not coded: the client possesses the loaded pawn by replication (ClientRestart from the host); the mod cannot refuse
the possession, so "does not take a pawn" is met by keeping the death screen up over it until the Reload's load
screen. The card 143 Reload rejoin itself stays (it is how the client gets the host's checkpoint state); the row
makes the drop within kC290TravelMs a travel (no failure dialog). Sibling "host dead, client alive" waits on the user.

## Build b7 checks (senior review of b6, static)

1. **`g_c45.pawn` is null before the rejoined map's first pawn.**
   - On every successful LoadMap, `hooks_ubw.cpp:268` calls `client_diag_load_map_return`.
   - `client_diag_load_map_return` calls `c45_arm_map()` at `sprint.cpp:236`.
   - `c45_arm_map()` sets `g_c45.pawn = nullptr` at `hooks_ubw.cpp:1327`.
   - The rejoin is a full LoadMap: client log `UBWCoop-menu-20261113-184603.log` line 32296 has `new map: reset` at
     256821160, before the first PostInitializeComponents of the new map (256821414).
   - In `c45_respawn_check` (`death.cpp:3093`), the first pawn the new PlayerController possesses therefore reaches
     `c294_hold_respawn(pawn, nullptr, now)`.
   - That call takes the `old == nullptr` branch (`death.cpp:3034`) and ends the hold with `new map`.
   - Nothing between the LoadMap return and the possession writes `g_c45.pawn`. The other writers, `death.cpp:3108`
     and `:3123`, run only from `c45_respawn_check` itself.
   - So the hold does not end by timeout and does not log a false `no travel followed`.
2. **Pawn of the second CompleteDeath (client 256811644): the same pawn as the first (256807162).**
   - No player pawn was created on the client between the two calls. The next PostInitializeComponents for
     BP_PlayerCharacterMI_C is 0x509900A0 at 256812831 (line 30938), 1.2 s after the second call. It is the fifth
     one this map, so it is under the `kPawnWatchLogs` cap of 8 and none was hidden.
   - Both rows say `client-own`, and the only client-own pawn alive then was the first one.
   - So `c294_skip_complete_death` catches the second call by `pawn == g_c294.dead_pawn` (with `g_c44.complete_ms`
     set, since no map reset ran between the two calls). It does not depend on the hold having started.
   - The travel row does not need to move into the load detour: the host sends it on the next engine tick, and
     with the same pawn the order against the second CompleteDeath does not matter.

## Senior verdict on b7b-2 (2026-11-13): fix failed, cause settled, my review missed it

Checked in `logs/b7b-2-host.log`, `logs/b7b-2-client.log`, `ue/chan.cpp`, `save_c121.cpp`.

- Host: `LoadLatestSave` 267260812, load done 267263104, `card290: ... travel row sent to 1 clients (in-place
  load)` 267263217. Client: no card290 or card294 row for it; `respawn in place` in the old world at 267265016.
- Two faults, both in the b7 change I accepted:
  1. `c294_host_step` sends on "the next engine tick", but the load blocks the game thread, so the next tick is
     after the load (2.4 s late) and after the load's own burst of rows.
  2. `c290_host_travel` goes through `chan_send_client`, which returns true for "queued". The queue held 1868
     frames (311782 bytes, drain 3719 B/s, `QueuedBits` about 500000), so the row sat behind about 84 s of
     frames and the line said "sent".
- The client then dropped (about 267266300) and the queue was discarded.

Fix (both parts, one change):

1. The row leaves before the load starts: send it from `c294_host_load` itself (the world from the widget's
   outer chain or the cached engine pointer), not from a later tick.
2. A send that skips the pace FIFO for rows that must not wait behind a burst (`chan_send_client_now` or a
   priority flag): one small frame straight to `send_now`, counted in the pace stats. The travel row uses it on
   both paths (in-place load and `c45` travel).
3. The card290 line says `sent now` or `queued behind N frames`; the ledger gets its row in both cases.

Sibling sweep before coding (static, one table row per row kind): every host row whose late arrival changes
what the client does, with the worst queue depth at its send point: death and restart rows, EndLevel (card 260),
IntroStart, checkpoint and save rows, the card 300 closure row, the card 288 gate row. Each gets `priority`,
or `ordered, may wait` with the reason.

Open, to trace from the same logs before the next run:

- Why the client dropped at about 267266300 (failure type and reason on both sides). If every in-place load drops
  the client, the 84 s drain is moot and the follow must be the travel path; if it does not, an 84 s backlog of
  world state after a load is its own defect.
- Host death screen still up 90 s after the checkpoint load (`WARN cmd: 18 ui continue -> failed (top widget
  WB_PlayerDeathScreen_C ...)` at 267353358): the host side of the same restart.
- `WARN card198 ... Restore read-back ... Health 1.0 (applied 30.0)` at 267275179.

Proof: the b7b-2 confirmations again, with the client row `card294: ... host restart (in-place load): death
screen kept, respawn in place held` before the host's load-done row and `respawns in the old world 0`.

## Junior trace for b8 (2026-11-13, static, from `logs/b7b-2-host.log` and `logs/b7b-2-client.log`)

### (a) Why the client dropped at about 267266300

It is the card 143 rejoin, by design; it is not a timeout and not a host kick.

| t | Side | Row |
|---|---|---|
| 267263088 | host | `card143: ... Reload to BP_PlayerController_C ... sent` (end of LoadFromSlot, before the next tick) |
| 267263217 | host | `card290: ... travel row sent to 1 clients (in-place load)`: queued behind Reload and the load's burst |
| 267263524 | host | pace window: queue peak 1868 frames, 311782 bytes, longest wait 2722 ms |
| 267265013-016 | client | the engine's (unpaced) new pawn arrives: `card045 respawn in place` in the old world |
| 267266298 | client | `card143: client ... Reload received` (3.2 s after the send) arms the card 045 follow |
| 267267475 | client | `card045 try 1 ... SetClientTravel`; `card128 connection closed: reason 14` |
| 267271744 | client | reconnected |

- Every in-place load with a client drops it: `c143_after_load` sends Reload, `c143_receive_reload` arms the card 045
  follow, and SetClientTravel closes the connection. The queue is dropped by `chan_pace_step` when the PC leaves.
  So the 84 s drain is moot for the frames behind Reload; the follow already is the travel path.
- The V row never reached the client: it was behind Reload in the FIFO and was dropped at the close.
- The comment in `save_c121.cpp` ("before the card 143 Reload, about 800 ms later") is wrong: Reload goes out at the
  end of LoadFromSlot itself, so a row sent on the next tick is always behind it.
- Reload itself waited 3.2 s in the FIFO. During that time the engine replicated the new pawn and the client
  respawned in the old world. Reload is therefore `priority` in the table below. The V row must leave before
  `g_orig_c121_load` runs.

### (b) Host death screen still up at 267353358

This is not a failed host restart. The host restart worked; both players then died a second time.

- 267263221: `card045: host ... respawn in place`; `card043 restart: slot 1 Out -> Up`, `slot 0 Dead -> Up`.
- The checkpoint load brought back the save's bugs at the spawn. The scenario's first `clear` came after the wait
  steps, at 267356679 (`5 killed`). Warrior bugs hit the host from 267270701.
- 267274927: client copy Downed (it had Health 30 from the Restore, one hit). 267277243-244: `all down: host death`.
- 267280245: a second `CompleteDeath` and `death screen WB_PlayerDeathScreen_C constructed` on the host. This is the
  screen that `cmd: 18 ui continue` found at 267353358.
- Scenario fix (b8, part C): `clear <radius> kill` right after the load-done row and after the reconnect, before any
  wait step.

### (c) `WARN card198 ... Health 1.0 (applied 30.0)` at 267275179

This is a false alarm from the read-back, not a Restore failure.

- 267274179: Restore applied, Health 100.0 -> 30.0 (want 30.0). The 30 comes from the save: Reload carries the
  restored copy's state.
- 267274927: a warrior bug hit downed the copy, Health 1.0 (`card043 ... -> Downed`).
- 267275179: the read-back 1000 ms after the apply reads 1.0. `c143_read_back` compares against the applied value
  without checking whether damage or a life-state change came in between.
- Tool follow-up (not in b8): the read-back names a life-state change or damage since the apply instead of
  warning. It is noted here only; the b8 scenario clears bugs before the restore, so the row is clean there.

### (d) Sibling table: host rows whose late arrival changes what the client does

The queue depths are the b7b-2 pace windows covering each send point (`netbudget: host ... pace: ... queue peak`).
The rule: an op is always `priority` or never, so rows of one op keep their order among themselves.

| Row (op) | Send point | Worst queue depth seen at its send | Class | Reason |
|---|---|---|---|---|
| Travel `V` (WorldOp kind V, card 290) | `c290_host_travel`: in-place load, card 045 browse | 1868 frames / 2722 ms (267263524) | priority | It races the engine's unpaced pawn and travel replication. It is one row per travel, and nothing queued before it matters after the travel. |
| Reload (card 143) | `c143_after_load` | same window, arrived 3.2 s late | priority | It starts the rejoin; the queue behind it is dropped at the close anyway. Late, it lets the old world respawn the client (a). |
| Down (card 209 / 043): life state, all-down Out/Dead, restart Up | `death.cpp:715` | 1608 / 5805 ms (267272416); 1323 / 3207 ms (267277431, the all-down at 267277243) | priority | The engine replicates Health unpaced, so the client sees Health 1 without the downed state, and its death screen comes up seconds late. Down rows refer to pawn NetGUIDs only, never to queued chan state. |
| EndLevel (card 260) | `objectives.cpp:1468` | 0 (map end at 267473385) | ordered, may wait | The objective rows before it (the last Complete) must land first. The map change itself is carried by the V row, which is priority. |
| IntroStart (card 135), join replay | `intro_c135.cpp:112` | 1608 (join replay at 267274170) | ordered, may wait | It belongs to the join replay, after the objective journal; the client's GameStarted HUD must see the replayed objectives. |
| Checkpoint and save rows | none | - | - | A save sends no row to clients (`save_c121.cpp` has no send). The load sends Reload and V (above). |
| Closure `F` (card 300) | `c300_host_send` (WorldOp, journalled) | not in a deep window in b7b-2 | ordered, may wait | It shares the FIFO with the CollisionOp and DoorOp rows of the same passage; overtaking an earlier open would apply the close before it. |
| Card 288 gate: DoorOp close held / closing | `door_c95.cpp:300/352` | not in a deep window in b7b-2 | ordered, may wait | DoorOp rows for one door must apply in order. DoorOp is a high-volume op (906 rows in a join replay), so it cannot be priority as a whole. |

- Priority frames are small: V is one WorldOp text row, Reload is one text frame, and Down is five arguments. They
  go to `send_now` past the gate. A priority frame is still counted in the pace stats (`priority` sends) and gets
  its ledger row.

## Senior review of b8 (2026-11-13, static)

Code accepted: the V row leaves from `c294_host_load` before the load; V, Reload and Down go through
`chan_send_client_now`. Checked: queued frames are packed at send time (`wire_form` with commit in `send_now`), so a
priority frame cannot break the dictionary of frames still queued; an op is always priority or never. Accepted risk:
priority sends skip the `kPaceRelHold` guard (a handful of frames per event). Junior traces (a), (b), (c) and table
(d) accepted. Follow-up for b9: the card 198 read-back names damage or a life-state change since the apply instead
of a WARN. Proof still owed: the b8 run's two cases.

## Senior verdict, b8 run 2 (2026-11-13)

All-down case shown twice: host travel row `sent now, ahead of 0 queued frames (in-place load)` (270357896,
270464308), Reload `sent now, ahead of 863` / `697 queued frames`, client `restart done in 8665 ms` / `7898 ms` with
`death screens built 1, respawns in the old world 0`. Open: host down with the client alive (user decision pending);
the Down priority send has no log row. b10 adds `card294: host Down sent now, ahead of N queued frames` (capped).
Prediction for the last run: both restarts again show `respawns in the old world 0`, and at least one Down row with
`sent now`.

## Senior verdict, b10 last run (2026-11-13): all-down case confirmed

Host `card294: ... Down Downed/Out/Dead ... sent now, ahead of 0 queued frames` x4, Reload `ahead of 857 queued
frames`; client `second CompleteDeath skipped (death screen up)`, `restart done in 6528 ms: death screens built 1,
respawns in the old world 0`. Open only: host down with the client alive (user decision).

## Senior static note: host restarts while the client is alive (2026-11-13, not run)

A host in-place load reloads the host's world, so every client follows it by the travel row (`c294_host_step`,
`save_c121.cpp:211`; client `c294_client_travel`, `death.cpp:3021`); there is no path that leaves a client in the old
world. With the client alive the client tracks 0 death screens, the same state as a map travel, which b10 showed
clean (`restart done in 8059 ms: death screens built 0, respawns in the old world 0`). Prediction for the soak: same
row with `(in-place load)`. The user decision is therefore not "pull or ask" in the netcode, only whether the host's
restart button should be blocked or confirmed while another player is still up.

## Senior note 2026-11-14 (b12 run 2)

Proven in C1M1 soak run 2 (b12 3b02e3e35c099b5a): case A, host down and client alive, client `restart done in 4364 ms: death screens built 0, respawns in the old world 0`; case B, both down, `death screens built 1, respawns in the old world 0`, host `card043: host restart: 2 down entries stood up`. Closed for C1M1; other maps ride the soaks.

## User decision 2026-11-14

Host restarts (death screen or pause menu) while the other player is up: allowed, no prompt and no block; the client
is pulled along by the travel row. That is the coded behaviour (b11 soak C1M1 run 1 confirmed the host-down,
client-alive case). The sibling row "host dead, client alive, host picks restart" is closed with this answer. Proof
owed in a soak: a pause-menu restart while both live, one transition on the client.

## Closed (2026-11-17 08:50)

Close-out proof, b177 e7197c28f8ab7bb8, run c294b177a (FED_C1M1, `tools/scenarios/c294-290-pause.txt`): host RESTART
CHECKPOINT from the pause menu with both players up. Client: exactly one `card294: client t=565544728 host restart
(in-place load): death screen kept, respawn in place held (0 death screens tracked)` and one `restart done in 2976 ms:
death screens built 0, respawns in the old world 0 (held 0, new map)`; 0 `card052` failure or dialog rows; one
reconnect try (the first checkpoint predates the client pawn). Client shot `p1-client-after-restart`: in the map on
its own pawn, HUD up. RESTART MISSION is the card 346 seamless travel and sends no card294 rows by design. More than
one `card294 ... host restart` row per restart, or `death screens built` above 0, in a playtest log reopens this card.
