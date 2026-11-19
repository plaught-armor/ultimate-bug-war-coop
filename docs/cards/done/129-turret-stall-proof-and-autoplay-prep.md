# Card 129: Prove the card 128 TurretAim hold; static prep for card 126

Status: sent and accepted 2026-11-07.

Read: card 128 Report (TurretAim hold, `g_c128_host_ping_ms`, `tools/scenarios/c128-stall.txt`), card 125 Report
(`tools/scenarios/c125-proof.txt`), card 126 (autoplay, step 1). Memory rule card-run-discipline applies: confirm
before running, two failures of the same step means change the method or report.

## Why

Card 128 built the client's TurretAim hold (no reliable aim sends while the host's last Ping is more than 2.5 s old)
but no run exercised it. A host stall while the client sits in a turret is the case that would overflow the client to
host reliable buffer. Card 126 is next session's main card, and its static step needs no game runs, so it fits in the
time left.

## Do

1. **One run, FAST=1 (no dropship flight), FED_C3M1.** The client mounts the turret as in c125-proof.txt. The client
   moves its aim with `axis MouseX` for the whole stall. The host's game is stopped with `kill -STOP` for 8 s, then
   `kill -CONT`. Show:
   - the client's log line for the hold starting and ending (add one if there is none: first hit logs at once);
   - no close on either side, card 113 unchanged, and the host's turret aim following the client's newest aim after
     the resume (one TurretAim line);
   - rc 0, ERROR/FATAL 0, no CRASH line, no new dump.
2. **Label fix.** The card 076 relay log says "mech round" for turret rounds too. Name the vehicle class in the line.
3. **Card 126 step 1, static only (no runs).** The objective type table: where a player learns where to go (the
   active objective's world location, the waypoint actor or the HUD marker's target, with the field offsets) and,
   for each objective type FED_C1M1 uses, what it asks for and how the game marks it done. Write it into card 126
   under a "Prep (card 129)" heading.

## Done when

The Report has the run lines above, the run length, rc, ERROR/FATAL, CRASH lines, dumps and the DLL hash
(`sha256sum | cut -c1-16`) with the play DLL saved as `build/dwmapi-play-<hash>.dll`, and card 126 has the prep
table. Ends with "Report complete."

## Report (junior, 2026-11-07)

Play DLL **4b0362a16c4318ad** (`sha256sum | cut -c1-16`), saved as `build/dwmapi-play-4b0362a16c4318ad.dll`, deployed,
packaged as `dist/UBW-Coop-20261107-1239.zip` (the zip's `UBW-Coop/build/dwmapi.dll` hashes the same). Base
2f443e99fa6ed52a. Three runs: c129a and c129b failed, c129c passed. The causes are below.

### Defect found: the card 128 TurretAim hold flapped in normal play (fixed)

The host pings every `kC26PingMs` = 5000 ms, not every second. The card 128 hold (2500 ms with no host Ping) therefore
started between every two Pings. c129a's client log, after the stall: `TurretAim held: last host Ping 25xx ms ago`
then `hold ended after ~2.5 s: host Ping back`, every 5 s from 147348046 to the end (9 hold lines). Client turret
aim was dropped half the time. The peer caught this from the same log.

Fix (`turret_sync.cpp`): `kC128AimHoldMs = kC26PingMs + kC26PingMs / 2` (7500 ms). `kC26PingMs` moved from `fire.cpp`
to `hooks_internal.h`, so the hold follows the ping interval if it changes. A `static_assert` checks that the aims sent
before a hold (7500 / 50 = 150 bunches) fit the 256-bunch reliable buffer. On a host stall, the client stops at most
7.5 s after the last Ping. With the card 128 significance flood gone, nothing else came near the buffer in c129c (no
close).

### Step 1: turret aim through an 8 s host stall (c129c, FAST=1, FED_C3M1)

Scenario `tools/scenarios/c129-turret-stall.txt`:
- the c125-proof mount;
- then `& client axis MouseX 0.3 12000` (the aim moves through the whole stall), `sleep 0.5`, `host stall 8`
  (SIGSTOP, then SIGCONT), `join client`;
- then the log waits, and `sleep 30` (six Ping intervals on the turret with no stall).

Lines:

```
client [147721958] card128: client t=147721958 TurretAim held: last host Ping 7527 ms ago
host   [147724679] card128: host t=147724679 TurretAim after a 8078 ms gap from BP_PlayerController_C: yaw 0.6 pitch -5.0 applied
client [147724730] card128: client t=147724730 TurretAim hold ended after 2772 ms: host Ping back
client [147724730] card128: client t=147724730 first TurretAim after the hold: yaw 12.5 pitch -5.0
host   [147724817] card128: host t=147724817 TurretAim after the gap #78: yaw 12.5 pitch -5.0 applied
host   [147726603] card128: host t=147726603 TurretAim after the gap #97: yaw 14.4 pitch -5.0 applied
```

- The hold starts and ends inside the stall. It is the only hold pair in the client log: no flap in the 30 s on the
  turret after it, and none before.
- After the resume, the host applies the 77 aims the client sent before its hold (yaw 0.6 to 12.4, all within
  138 ms). It then applies the client's newest aim, yaw 12.5, at #78, and follows the live aim to 14.4.
- No close on either side: no `UNetConnection_Close` or close-reason line, and the run ran to the scenario's end.
- card 113 is unchanged after the joins: `session players 1 -> 2 (present 2, join)` and no later card 113 line.
- Run length 136 s. host rc 0, client rc 0, drive rc 0. FATAL/ERROR 0 on both sides. No CRASH line, no new crash dumps.
- Host WARN 0. Client WARN 1: `WARN card114: cannot create ...\Saved\UBWCoop-Difficulty.defaults.ini (err 32)`. That
  is a sharing violation, with both instances writing the same file in one prefix at startup. c119g had it too, so it
  is not from this card. It is not fixed here; it is a candidate for a card.

New log lines (all log their first hit at once):
- client: `TurretAim held` / `hold ended after N ms` / `first TurretAim after the hold`;
- host: `TurretAim after a N ms gap` (more than 2000 ms with no applied aim), then `after the gap #n` for each aim in
  the next 2000 ms, at most 256.

Failed runs, with the logged cause:
- **c129a** (DLL 47c8862f49c6de48, 2.5 s hold): the hold, the hold end, the first aim after it and the host's gap line
  all passed (host yaw 6.8 = the client's newest). The step for the host's `after the gap #n` line failed. The axis
  (4.5 s) had ended, so the client sent nothing more. The flapping above shows in this run.
- **c129b** (DLL 4640fce7ee76a91b, 7.5 s hold): `first TurretAim after the hold` timed out. The hold started 7.5 s
  after the last Ping (147553049), which was after the 4.5 s axis had ended, so there was no newer aim to send. The
  client's last aim (yaw 4.3) had already reached the host (host `aim applied: yaw 4.3`). The behavior was correct and
  the scenario was wrong; the card asks for the aim to move through the whole stall. Fixed in the scenario (12 s
  axis). The host gap log was widened from 12 lines to a 2000 ms window, so the newest aim shows after the queued
  ones.

### Step 2: card 076 label

`C76Mech` gained `vehicle` (set with the task in `c76_mark_task`). The relay line now reads `card076: host t=... %s
round #N ...` with the vehicle's class (`BP_MountableTurret_C`, the mech's class). The cap is named "card076 vehicle
round relay". This is build-checked only: no card 076 line comes up in these runs, which fire no rounds.

### Step 3: card 126 prep

Written in `docs/cards/126-autoplay.md` under "Prep (card 129)":
- the objective field offsets, read from the exe's reflected property tables;
- the go-to rule: the location of `USTLevelObjective::ObjectiveActors` (0x88) element 0, with the
  `ASTLevelObjectiveMarker` showing it;
- the active-objective rule: a sub-objective that is not complete and has bShowMarkers set;
- the FED_C1M1 objective table from the host logs, with what each asks and how the game marks it done;
- the kind-by-actor-class dispatch table.

Open points, left for card 126's first run:
- whether the marker sits at the actor's root location;
- the trigger for the TacSupport call-in steps;
- the actors for CaptureTheFlag_SO1 and SO3;
- the dropship boarding step.

Report complete.
