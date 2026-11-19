
## Hit drift rows during play (2026-11-16, run c423b137a, b137; patch not built yet)

Two client `WARN card140 ... kHitDrift` rows during play (guid 0x7ba 460 uu, guid 0xde0 566 uu), both hits on a
corpse a pose row held. Cause: the measure differs by side. The host sends body 0 only when it simulates
(`c132_ragdoll_root`), else its actor location; the client also reads body 0 of a copy a pose row holds
(`c132_posed_root`). A frozen corpse lying away from its actor gives a false row. No visible effect (the drift is a
log measure). Fix (`c140h_patch.py`, scratchpad, 1 edit in `hit_c132.cpp`): the host uses the same two tests.
Done when: a FED_C2M1 run with hits on frozen corpses shows no `kHitDrift` WARN row outside the 3 s after a level end.

## Remaining `kHitDrift` row is a real client-side early ragdoll (senior, 2026-11-16, run `c423b163a`, b163)

The `c140h` host-side fix is in (`hit_c132.cpp:346`). The run still has one client row, 49 s after the map load, so
the done-when is not met: `WARN card140: client hit BP_AI_WarriorBug_C guid 0x148: host location (227732, 39818,
8612) is 707 uu from the copy's ragdoll root (227187, 39368, 8577)` at 520446612.

Trace (client log, guid 0x148):

- 520446179 `card152: client held the game's ragdoll start ... (no stream row yet), caller exe+0x37C53CB`, and in the
  same ms `card140: client pose own SetAllBodiesSimulatePhysics(1) on BP_AI_WarriorBug_C guid 0x0 (row none)`.
- 520446694 `card152: client stream start ... it was already simulating` (wait resolved after 515 ms). The host's
  sample 0 root is (227981, 39960, 8523), 990 uu from the client's body 0 at the WARN.

Cause: the blueprint starts a ragdoll with `SetSimulatePhysics(1)` and then a direct `SetAllBodiesSimulatePhysics(1)`
(ubergraph 1313 then 1396). Card 152 holds the first until the host's stream row; `detour_c132_all_bodies`
(`enemies.cpp:2966`) passes the second unless the copy has a rest row (card 207). The bodies then simulate from where
they were last updated (`c39_sync_mesh` never ran, the held start is what calls it), so the copy ragdolls on its own,
away from the actor, until the stream row poses it. 21 of 23 held starts in this run were "already simulating" at
their stream start (24 of 27 in `c423b137a`, 19 of 22 in `c423b162a`).

A player sees: on the client a dying bug's body can jump away from where it died for up to half a second (2 s at
most, the card 152 fallback), then jump to the host's corpse.

Fix (`c140w_patch.py`, scratchpad, 3 edits, `--check` passes on the b169 tree): new `c152_pose_game_start` in
`pose_c132.cpp`; `detour_c132_all_bodies` blocks the game's `SetAllBodiesSimulatePhysics(1)` on a client copy that is
in the card 152 wait table with its fallback not run. The stream start (`pose_stream_start`) or the fallback
(`pose_wait_tick`) starts the copy through the card 039/132 start as before.

Predictions (next FED_C2M1 run, no run of its own):

- Each `card152: client held the game's ragdoll start` row is followed by `card140: client blocked the game's
  SetAllBodiesSimulatePhysics(1) on waiting guid`.
- `card152: client stream start` rows read `ragdoll started for it` for those guids; "it was already simulating" only
  on copies never held.
- 0 `WARN card140: client hit ... kHitDrift` outside the 3 s after a level end; 0 `copy would not simulate`; stream
  finals unchanged (largest body error 0.0 uu); digest 0 mismatches.

## Closed (2026-11-17 13:02): the `c140w` predictions are met in the b180 to b182 runs

Read from the client logs of six FED_C2M1 runs (`c423b180a`, `b180b`, `b180c`, `b181a`, `b181b`, `b182a`):

- 0 `WARN card140: client hit` rows and 0 `copy would not simulate` rows in all six.
- `card152: client stream start` rows read `ragdoll started for it` (29 to 111 per run); "it was already
  simulating" 0 in `b181b` and `b182a` (was 21 of 23 before the fix).
- The block ran: 19 and 17 `blocked the game's SetAllBodiesSimulatePhysics(1)` rows in `b181b` and `b182a`.
- Stream finals: every row `largest body error 0.0 uu` (167 and 136 rows); digest 0 mismatches (198 rounds, card
  251 closure).

Reopen (already a playtest-log check under card 474): any `WARN card140: client hit` row. The wait time of the held
start is card 291.
