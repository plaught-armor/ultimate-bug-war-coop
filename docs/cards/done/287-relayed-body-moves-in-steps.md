# Card 287: a rolling flare (any relayed physics body) moves in steps on the client

Playtest 2026-11-13 (user is the client): "the flare prop when it's rolling on the ground isn't smooth".

## Trace (static, live client log `UBWCoop-menu-20261113-183002.log`)

Lines 5575-5615, host 126553 `BP_SupportFlare_C`: seven `card104: client ... rest at ...: our copy moved and put to
sleep` rows at t=255231439, 232089, 233188, 233904, 234404, 234854, 235388 (450 to 1100 ms apart), each
`was 50 uu` (or 16 to 51) `from the host's rest`.

- Host: while a relayed simulating body is awake, `c104_sample_moving` (`world_c98.cpp:1180`) sends a row when the
  body is more than `kC104SampleUu` 50 uu from its last row, at most every `kC104SampleMs` 200 ms. The row is the
  same 'M' rest row (`c104_send_rest`).
- Client: `c104_client_rest` (`world_c98.cpp:1090`) teleports the copy to the row's place and rotation and puts the
  body to sleep. So a rolling flare stands still, then jumps 50 uu and to a new rotation, about twice a second.
  Card 140 only aimed at the 100 uu position gate, not at how the motion looks.

## Fix

1. Host: a moving sample is told apart from a rest (a new kind or a flag on the 'M' row; the journal keeps only the
   last row as now). Moving samples go at most every 100 ms once the body is more than 10 uu from its last row.
2. Client: a moving sample does not teleport. Each copy keeps the last two samples (place, rotation, arrival time);
   every tick the copy is moved between them (place linear, rotation by quaternion, shortest way), over the time
   between the two rows, body asleep so its own physics does not fight it. A rest row ends the motion: the copy
   glides to the rest pose over the same time, then sleeps there (final place and rotation exactly the host's).
   A sample more than 300 uu from the copy still teleports. One table, one tick loop for all copies (one manager
   per class), bounded by `kC104Copies`.
3. Rows: one capped row per copy when its first moving sample is applied and one at its rest
   (`card287: client t=<ms> copy <name> host <id>: <n> samples over <ms> ms, largest step <uu> uu, at rest <uu> uu
   from the host's`). The largest step is the largest per-tick move the client made.
4. Sibling sweep, in the card as a table: every client apply that sets a moving thing's pose from periodic host
   rows by teleport: card 104 copies (flare, pickups, drop pods), gib parts (`gib_client_pose`), card 140 placed
   actors, dropship live rows (card 285), carried or thrown objective items. For each: row rate, teleport or
   blended, verdict. Classes that move visibly between rows get the same blend through the one table.

## Predictions (one run, client throws a flare down a slope)

- `card287` rest row: samples at about 100 ms spacing, largest step under 5 uu per tick at 60 fps for a roll of
  100 uu/s, 0 uu from the host's at rest.
- No `moved and put to sleep` row for a copy between its spawn and its rest.
- Receiver-side video or three shots 100 ms apart: the flare at three different places along the roll.

## As coded (junior, uncompiled)

Diff: scratchpad `c287.diff` (world_c98.cpp, hooks_ubw.cpp, hooks_internal.h).

Host (`world_c98.cpp`):
- `kC104SampleUu` 50 -> 10 uu, `kC104SampleMs` 200 -> 100 ms.
- `c104_send_rest(i, actor, root, at, moving)`: the 'M' row's name gains a fourth field, `pitch,yaw,roll,1` for a
  moving sample (from `c104_sample_moving`) and `pitch,yaw,roll,0` for a rest. It stays the one journal row per actor
  (`c98_journal_put` keeps the last), so a late joiner gets whichever was last. A moving sample passes `quiet` to
  `c98_host_world`: no `card098 world` line per sample (ten a second per rolling body); a failed send still warns.
- `c256_heal_rest` sends `moving = !rested`, so a heal of a body still rolling does not end the client's motion.
- A row with three fields (no flag) is read as a rest on the client.

Client (`world_c98.cpp`), one table `g_c287` (`C287Blends`, `kC104Copies` rows, swap-remove), one tick loop:
- `c287_take` (called from `c104_client_rest` before the old set-at-once path):
  - a moving sample creates the copy's entry (first time: a `first moving sample` row) or retargets it. `from` is the
    pose shown now, `to` the row's pose, blend time the gap since the previous row clamped to 16..500 ms (first sample:
    100 ms). The row is applied by the tick, not here.
  - a rest row with an entry: the last glide (`rest = true`).
  - more than `kC287TeleportUu` 300 uu from the copy: set at once (counted as a teleport; no step counted). A far rest
    row sets, sleeps, writes the rest row and drops the entry.
  - a rest row with no entry (a pickup on the rack, a late joiner's first rest): returns false, the old path sets it at
    once with its `card104 ... moved and put to sleep` row, as before.
- `c287_client_step` (hooks_ubw.cpp tick list, after `c104_settle_step`; returns at once on the host or with no
  entries): for each entry, `t = (now - start) / dur`; place linear, rotation by quaternion slerp on the shortest arc
  (nlerp when the two are within 0.9995); `SetActorLocation` + `SetWorldRotation` with TeleportPhysics, then
  `PutRigidBodyToSleep` on a primitive root, so the copy's own physics does not move it between rows. At the end of
  a moving blend the copy holds at the newest sample, asleep, with no further sets until the next row. At the end of
  a rest glide it is set exactly to the rest pose, sleeps, writes the rest row and leaves the table. A copy whose weak
  handle is gone leaves the table.
- Rotator <-> quaternion: `c287_quat` (FRotator::Quaternion) and `c287_rotator` (FQuat::Rotator, gimbal branches).
- `c104_client_destroy` drops the copy's entry; `c98_arm_map` clears the table with the copies.
- Card 256's first-rest line now fires on rest rows only (a moving sample is not a rest).

Rows:
- `card287: client t=<ms> copy <name> host <id>: first moving sample, <uu> uu from the copy[, set at once]`
- `card287: client t=<ms> copy <name> host <id>: <n> samples over <ms> ms, largest step <uu> uu, at rest <uu> uu from
  the host's (glided|set at once, <k> teleports)`. The rest distance is measured after the last set. Cap
  `kC287Lines` 48 per map (both rows share it).
- `WARN card287: client blend table full (<n>): copy <name> host <id> set at once` (cannot happen while the copies
  table bounds it the same; kept as the boundary check).

Prediction check (unchanged from the card): the rest row's largest step for a 100 uu/s roll at 60 fps is about
1.7 uu; no `moved and put to sleep` row between a moving copy's spawn and its rest (a moving sample never reaches the
old path unless the entry could not be made).

## Sibling sweep

| Class | Rows while moving | Client apply (before) | Verdict |
|---|---|---|---|
| Card 104 copies: flare, pickups, drop pods, any relayed simulating body | 'M' moving sample, now 10 uu / 100 ms | teleport + sleep per row | Fixed: blended through `g_c287`. |
| Card 104 non-simulating roots (rack pickups, danger zone) | none while moving; a rest row when still 30 ticks | teleport + sleep | Unchanged: no motion to blend; a rest row with no entry is set at once as before. |
| Gib parts (`gib_client_pose`, gib_c137.cpp) | 'M' every 200 ms past 8 uu / 5 deg | teleport, no sleep: the client part's own physics flies it between rows | Not stepped: its own simulation moves it between rows, and the 'M' row is a correction whose size card 148's flight buckets already log (`flight_worst`). Not changed here. If the playtest shows gib jumps, the same table takes them with a gib key. |
| Card 140 placed actors ('A', `c140_client_placed`) | none: a row only when the host's mover stands still | teleport + sleep at the stand | Not periodic: one move per stand. Out of this card (no samples to blend). |
| Dropship live rows (card 285 / card 159) | 'L' check every 1 s (`kDsLiveMs`) | the client's own spline tick flies it; rows correct only at rest (rotation, card 285) | Smooth: the local spline owns flight. |
| Ext state (card 132 step 2d, `kExtApplyMs` 250) | state values, not poses | enforce state | Not a pose: no blend. |
| Carried or thrown objective items | carried: attached to a replicated pawn; thrown: a relayed card 104 body when it simulates | engine attach / card 104 path | Carried follows the pawn's engine-smoothed motion; a thrown one that is a card 104 copy is in the first row. |

## User detail after the playtest (2026-11-13)

"After they are deployed their model is supposed to roll but it lags." Confirms this card's symptom: the flare
is single (no double, unlike card 286), and its roll after landing moves in steps on the client. Receiver-side
proof for the fix: the flare's roll on the client is smooth (pose samples interpolated between rows), compared
frame by frame with the host's.

## Build b7 change (senior review of b6)

The host keeps a per-map tally in `c104_send_rest` (world_c98.cpp, `g_c287_sent`): each moving sample counts as one
row sent, and the counter keeps the most rows in one 1 s window (a window starts at the first send after the
previous window has ended). `c98_world_map_end` logs the tally and clears it:
`card287: host map: <n> moving rows sent, peak <p> in one second`. The row is skipped when no moving row was sent.

## Closed (2026-11-17 11:12)

Covered by cards 470 and 473 (closed 2026-11-17, b173): in b40b173a the client's flare copy glides between the host's samples (0 hops after the first sample, largest step 82.3 uu), pickups glide, and the shot pair `client-b40b173a-c470-flare-flight.png` / `-flare-rest.png` was viewed. The host tally row `card287: host map: <n> moving rows sent, peak <p> in one second` is in 15 logs. Reopen on a user report of a relayed body moving in steps.
