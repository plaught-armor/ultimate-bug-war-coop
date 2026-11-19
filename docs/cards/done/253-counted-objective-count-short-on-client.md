# 253 Counted objective ends one short on the client

Found by card 241 run 3 (`logs/c241-c2m1-r3-*`). Cosmetic sync gap (objective count text), not a play blocker.

## Evidence

- Final card066 dumps differ on one line: host `LOS_O02_SO05 ... complete 1, count 2 of 2`,
  client `count 1 of 2`.
- Host order at 171407867-171407870: native `CompleteObjective LOS_O02_SO05/LOS_O02_SO05a` (leaf, caller
  exe+0x5233805), then the parent `LOS_O02/LOS_O02_SO05 (count 2/2)` nested at depth 1 (caller exe+0x5150CF3).
- Client at 171407889: the parent arrives first, as a native call at depth 0 with the leaf caller
  (exe+0x5233805) and `count 1/2`; the last child `LOS_O02_SO05a` is then completed inside it at depth 1
  (caller exe+0x5150CBC, the complete-children path), which does not raise the parent's count. The mod's
  replay rows arrive 2-3 ms later and change nothing (`objectives 21 -> 21`).
- Same shape in run 2: client `LOS_O03/LOS_O03_SO02 (count 2/3)` at 170600280, host `(count 3/3)` at 170600215.
- First children count correctly on the client (SO05b at 171396862).

So the short count comes from the order in which the client receives the completes (parent before its last
child), not from a missing row.

## Step 1 (static, no run)

Trace in `mod/src/ubw/objectives.cpp` which path delivers the depth 0 native complete on the client (caller
exe+0x5233805): the game's own replication or a mod replay entered through the library call. State where
`kObjectiveCountCurrent` is written on the host and whether any row sent to the client carries it (the sent
rows log `count 0`).

## Step 2 (fix by class)

Every counted objective (`g_c49_count_class()`), not only LOS_O02_SO05: after a Complete row for a counted
parent, the client's `CountCurrent`/`CountTarget` equal the host's. Preferred shape: the host's Complete
row carries both values and the client writes them after the replay. No per-objective special case.

Proof: c241 scenario, O02 and O03 blocks; final card066 dumps equal on every `count N of M` line; the
client's objective list shows the same count text as the host's.

Files: `mod/src/ubw/objectives.cpp` (others only if step 1 names them). Budget: 1 run.

Run 5 (`logs/c241-c2m1-r5-*`): three counted parents end one short on the client: `LOS_O02_SO05` 1 of 2,
`LOS_O03_SO02` 2 of 3, `LOS_O03_SO03` 2 of 3 (host 2 of 2, 3 of 3, 3 of 3). Same class.

## Step 1 answers (senior, static plus logs of run 3 and c241 run 10, 2026-11-13)

- Still present with card 100 in force: c241-c2m1-r10 final dumps show the same three parents one short on the
  client (`LOS_O02_SO05` 1 of 2, `LOS_O03_SO02` 2 of 3, `LOS_O03_SO03` 2 of 3).
- Cause: the host sends nested calls first. On the host the script completes the last child
  (`CompleteSubObjective(LOS_O02_SO05, LOS_O02_SO05a)`, depth 0); the game raises the parent's count to 2 of 2
  and, nested at depth 1, the script completes the parent (`CompleteSubObjective(LOS_O02, LOS_O02_SO05)`).
  `c49_after` emits when each call returns, so the parent's row is journal 66 and the child's row is journal 70.
- The client replays the parent's row first, with its count still 1 of 2. The game's complete-children path
  (caller exe+0x5150CBC) then completes the last child without raising the parent's count. The child's row
  arrives later and changes nothing.
- No row carries the count: Complete rows are sent with `count 0`. `kObjectiveCountCurrent` (+0x9C) is written
  only by the game on the host and by `c49_replay_set_count` (snapshot rows) on the client.
- Changing the send order to outer first was rejected: it changes which side creates objectives added inside a
  completion handler and breaks the card 258 prediction (`the id exists already` rows).

## Fix spec (step 2, `mod/src/ubw/objectives.cpp` only)

- Host, `c49_mirror` path for `Complete`, `CompleteAny` and `CompleteSub`: when the completed objective
  (`name[1]` for `CompleteSub`, else `name[0]`) is a `g_c49_count_class()` object, the frame's `count` field
  carries its `kObjectiveCountCurrent`. The field is already on the wire and is 0 for these kinds today.
- Client, `c49_receive`, before `kC49Replay[k]` for the same three kinds and not for snapshot frames: when the
  objective is a count object and the row's count is higher than its own, write the row's count into
  `kObjectiveCountCurrent`. The game's own completion events then read the host's count. Never lower it here.
- Log, capped per map: `card253: client count <id> a to b before <kind>`.
- Prediction (FED_C2M1 run, O02 and O03 blocks, batched with other C2M1 cards): three `card253` rows
  (`LOS_O02_SO05` 1 to 2, `LOS_O03_SO02` 2 to 3, `LOS_O03_SO03` 2 to 3), the final card066 dumps equal on every
  `count N of M` line, and the client's "after the completion" screenshot shows the full count if the row is
  still on screen. Budget: 0 extra runs.

## Closed (2026-11-16 20:00, senior)

The fix spec is in `objectives.cpp`. Run `c423b159b` (b159, FED_C2M1) shows the three predicted client rows:
`card253: client count LOS_O02_SO05 1 to 2`, `LOS_O03_SO02 2 to 3`, `LOS_O03_SO03 2 to 3`, each `before
CompleteSubObjective`, and the same three again in the join replay after the rejoin. The card066 dumps show
`LOS_O02_SO05 ... count 2 of 2` on both sides; digest 12 rounds, 0 mismatches.
