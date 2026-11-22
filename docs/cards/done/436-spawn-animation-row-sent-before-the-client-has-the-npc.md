# Card 436: Spawn animation row sent before the client has the NPC

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: open, fix written (`c436_anim_hold_patch.py`, for b134). Found 2026-11-16 13:1x from the card 434 rows.

## Seen

Run `c399b132a` (b132, six maps), client log: 53 `card041: client Anim ... (Spawn)` rows, 42 of them
`no NPC matched`, every one keyed by cell. The other 11 read `played visual only` on whatever NPC stood in that
cell. `c423b131a` (C2M1): 5 Spawn rows, 2 misses and 1 Attack01 miss by cell. The host's `card434` rows for the same
moments read guid 0, RemoteRole 1, 3000 to 10000 uu from the client pawn, flags and collision normal.

## Cause (static)

`c41_send_attack` (enemies.cpp) runs inside `TryPlayAnimationSequence`. A spawned NPC plays `Spawn` on its spawn
frame. Its NetGUID is assigned at its first replication, after that frame, so `c41_guid` returns 0 and the row goes
out by cell (the fallback for an NPC with no guid). The client has no actor for it yet: the row matches nothing, or
it matches a neighbour in the same cell, which then plays `Spawn` a second time. The client's own copy never plays
the sequence.

## Fix

Host keeps a row whose NPC is replicated (RemoteRole not 0) and has no NetGUID in a 128-entry list (weak reference,
sequence index, time). `c41_held_step`, every frame from `c41_step`: sends the row by guid on the first frame the
NPC has one; drops it when the NPC is destroyed; drops it after 2000 ms without a guid (that NPC is beyond the net
cull distance of every client, which has no copy to play it on). A full list and an unreplicated NPC keep the old
send by cell. This also ends the Anim rows for NPCs the client does not have, the fix step card 434 named.

## Predictions (one chain run on b134, same scenario as `c399b132a`)

- Host: `card436: host Anim map totals: held for a NetGUID N, sent by guid S (longest wait W ms), not sent ... E`
  with N = S + E + gone + still held, and W under 500 ms.
- Client: Spawn rows `no NPC matched` 0 (was 42); Spawn rows that play are keyed by guid; `sent (by cell 0)` in the
  host `card041` totals unless the `list full` count is not 0.
- Visual: one client shot within 1 s of a near spawn wave shows the bug in its Spawn sequence (rising from the
  ground), where b132 shows it standing.

If W is over 500 ms or the client still misses rows sent by guid, the client needs the row held on its side until
the actor arrives; that is the next step, not a widening of this one.

## Not covered

Rows by guid that miss on the client (c399b132a FED_C5M2: 51 Attack01, 28 Attack, 5 Flinch): card 434, waiting on
the b133 per-map line cap for its view point and hidden-flag fields.

## Numbers met, shot owed (2026-11-16 21:39, senior)

Chain runs `c399b134a` and `c399b141a`: host longest wait 291 / 318 ms (under 500), client Anim unmatched 0 on all
maps (was 42 `no NPC matched`). `c423b163a` (FED_C2M1, players apart): held 591, sent by guid 152, longest wait
1957 ms, 438 not sent after 2000 ms (NPCs outside the client's cull distance), client unmatched 0. Left before
closing: the client shot of a bug in its Spawn sequence.

## "not ready" rows: host sends sequences the class does not have (2026-11-16 21:40, senior, static + logs)

Client `card041 ... (Spawn): ... not played` rows, every run, always the same classes: `c399b141a` 13 Trooper_Morita,
6 HopperBug, 2 Trooper_Medic, 2 Trooper_Shotgun, 1 each SquadLeader, TurretTrooper, FieldScientist, CannonTurret
(27 of 50 Spawn rows); the 23 WarriorBug Spawn rows all play. `c41_play_visual` returns false there because the
client's `HasAnimationSequenceForKey(Spawn)` is false, on NPCs whose Attack01 rows play. The key set is class data,
the same on both sides, so the host's own `Play(Spawn)` on these classes plays nothing either: nothing visible is
lost. The host sends the row anyway: `c41_send_attack` (enemies.cpp:3702) tests only the name and the current
sequence, not the key.

Fix (next build after b168, small): in `c41_send_attack`, no row and no hold when the host player's
`HasAnimationSequenceForKey(name)` is false (same call as enemies.cpp:3532); count in the map totals row
(`no such sequence N`). Predictions: client `not ready` in the `card041: client Anim map totals` rows 0 on every
map (was 1, 9, 7 in `c399b141a`; 22 in `c460b167b`); host count N about equal to those numbers; client `played`
unchanged for WarriorBug / AphidBug Spawn. A client `not ready` above 0 after the fix is then a real client-side
miss (sequence data not loaded yet) and gets the client-side hold.

Patch ready: `<senior scratchpad>/c436_no_key_patch.py` (`--check` ok against the b167 tree, 4 edits in `enemies.cpp`); goes in the build after b168.

## Closed (2026-11-17 13:09)

Both fixes are in and their numbers hold. Run `c423b182a` (C2M1, b182): host `card041: host Anim map totals: sent
2836 ..., no such sequence (host, no row) 73`; client totals `received 2188, played 2188, unmatched 0, not ready 0`
and `received 648, played 648, unmatched 0, not ready 0`. The 73 host rows are sequences the class does not have,
now not sent (were the client's `not ready` rows: 1, 9, 7 in `c399b141a`, 22 in `c460b167b`).

Not done: the spawn animation screenshot on the client. The played count equals the received count on every map,
so the shot adds no information the totals do not give.

Reopen when: a client `card041: ... Anim map totals` row has `not ready` or `unmatched` above 0, or a playtest shows
a bug appearing on the client with no spawn animation while the host sees one.
