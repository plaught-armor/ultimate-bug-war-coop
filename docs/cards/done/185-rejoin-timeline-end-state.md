# Card 185 — A rejoining client gets the end state of host timelines

## Goal
After a rejoin, an actor the host moved by a timeline before the client left (or while it was away) stands where
the host has it. Replays set state (STATE.md): the end position, not a re-run of the timeline.

## Found (c183-r3, DLL 1c8125f82b56dde3, scenario c56-rejoin.txt)
Client, after the rejoin, digest round 2: 3 mismatches, class BP_KillableBuilding_Base_TacSuppRangeTarget_C (_0,
_1, _3), "location host (12650,14500,0) client (12650,14500,-250)". The host had sent
"world L ...TacSuppRangeTarget_C_0|Timeline ..." to the client 439 ms before the Logout (the Morita_C_1 /
TacSupport_SO2 objective raises the targets). New WARN class; the earlier accepted c56-rejoin run had no pickup.

## Trace first
1. How a 'L' (timeline) row is journaled (world_c98.cpp) and whether `c98_replay` sends it on the first Pong of a
   rejoin; if it is sent, what the client does with it when the replay is between the two 'R' rows (end state or
   skipped as a one-shot).
2. Whether the client's copy of the timeline was mid-play at the disconnect and its actors were reloaded at the
   start pose with the level.
3. Fix shape in the one mechanism that owns the class (the card 098 journal): the replayed row sets the timeline's
   end position. Cover every timeline-moved class on every map from the asset list, not only this target.

## Predictions
c56-rejoin.txt as it stands: 0 mismatches of this class after the rejoin; the client logs the timeline row applied
as end state; baseline WARN only; no dead time.

## Files
`mod/src/ubw/world_c98.cpp`, `mod/src/ubw/hooks_internal.h`, the card. The scenario only if a wait must change.

## Trace (junior, 2026-11-12)
Logs and code only, no run.

### 1. How an 'L' row is journaled and replayed
- Host:
  - `c98_host_timeline` (world_c98.cpp:548-575) journals a row with actor = the timeline component, kind 'L'
    (`kC98KindTimeline`, :211) and value = `C98TimelineOp` (hooks_internal.h:3545: Play 0, PlayFromStart 1, Reverse 2,
    ReverseFromEnd 3, Stop 4).
  - `c98_journal_put` (:~318-330) keeps one row per (actor, kind), and the last value wins. The skip at :563 drops a
    repeat of the same op, except PlayFromStart. So the journal holds the last op per timeline (the senior's reading
    is verified).
  - The hooks are `detour_c98_timeline_play` / `_from_start` / `_reverse` / `_reverse_from_end` / `_stop` (:164-191),
    installed at hooks_ubw.cpp:1822-1826.
- Replay: `c98_replay` (:1540-1567) sends "R - 0 0 0 1", then the rows in two passes, then "R - 0 0 0 0". It is called
  on the first Pong of a rejoin through `c50_join_replay` (weapons.cpp:649).
- Client: `c98_receive_world` (:1500-1537) passes 'L' to `c98_client_timeline` (:1519). That function (:1424-1451)
  finds the actor (`c95_find_nearest`) and the timeline (`prop_object_field`), and calls the op from its `kCall`
  table: play, from_start, reverse, reverse_from_end, stop. It does not check `g_c98_replaying`, so a replayed row runs
  the op again from the reloaded pose. It is not an end state and it is not skipped.
- c183-r3 evidence:
  - Host L rows (PlayFromStart) for targets _1 and _0 at 91844281 (host 3224, 3227). The client applies them live at
    91844290 (client 3381-3385).
  - The client reconnects at 91850287 (client 4356). The reloaded level has the target at (12665,14500,-482) (client
    4673), the start pose.
  - The replay starts at 91852612 (client 5040). The L rows are applied again, "applied timeline call 1", at 91852630
    (client 5074-5078).
  - The card140 'A' rows snap the targets to the end at 91852631-646 (client 5088-5092).
  - At 91853864 the digest gives "location host (12650,14500,0) client (12650,14500,-250)" for _0, _1 and _3 (client
    5329-5334). The re-run timeline moves the target away from the 'A' snap partway through its play.

### 2. Whether the copy was mid-play at the disconnect
Yes. The client applied the L row at 91844290. The drop was about 439 ms after the host's L row (91850287 - 5603 ms =
91844684). The host's card140 "first move row" came at 91845356 (host 3385), 1.07 s after the L row, so the timeline was
still playing. The reload put the actors back at the start pose (client 4673). Verified.

### 3. Fix shape
- Engine calls (from the PDB, `tools/pdb_rva.py lookup`):
  - `UTimelineComponent::SetPlaybackPosition(float NewPosition, bool bFireEvents, bool bFireUpdate)`
    `?SetPlaybackPosition@UTimelineComponent@@QEAAXM_N0@Z` = 0x042D9FB0
  - `UTimelineComponent::SetNewTime(float)` `?SetNewTime@UTimelineComponent@@QEAAXM@Z` = 0x042D9660. In UE source it
    calls SetPlaybackPosition(t, false) with the update fired.
  - `UTimelineComponent::GetTimelineLength` `?GetTimelineLength@UTimelineComponent@@QEBAMXZ` = 0x042CC240
  - `GetPlaybackPosition` = 0x042CBB20. `PlayFromStart` = 0x042D2610. `FTimeline::SetPlaybackPosition` = 0x042D96A0.
- In `c98_client_timeline`, only while `g_c98_replaying` is set:
  - Play / PlayFromStart: `SetPlaybackPosition(GetTimelineLength(), false, true)`. The update event fires once at the
    end value, and no finished event fires.
  - Reverse / ReverseFromEnd: `SetPlaybackPosition(0, false, true)`.
  - Stop: no call. The reloaded pose stays.
  - Live rows keep the current calls.
- The symbols are added to tools/symbols.txt (the gen step) in the code step.
- The timeline-moved classes, from every host log's card098 "world L" rows (not from the asset list):
  - `BP_SatchelBuilding_Flakbug_C|TurnInPlace` (ops 1, 4)
  - `BP_KillableBuilding_Base_TacSuppRangeTarget_C|Timeline` (1, 4)
  - `BP_KillableBuilding_Base_TargetRangeTarger_C|MovementTimeline` (4)
  - `BP_KillableBuilding_Base_GrenadeRangeTarget_C|MovementTimeline` (4)
  - `BP_DestructibleSpawner_Tent_ArachnidGameplay_C|MoveDoor` (0, 2)
  - `BP_DestructibleSpawner_Bugnest_HumanGameplay_C|TurnInPlace` (4)
  The fix is class-wide in the one function, so every class takes it.
- Open:
  - A row replayed while the host's timeline is still playing jumps the client to the end early, by the host's
    remaining play time (at most the timeline length, about 1 s here). The row carries no host start time.
  - A Stop row after a partial play leaves the client at the reload pose, not at the host's stopped position.
  - The asset-list sweep for classes that never fired in a log is not done.

## Run c142-185-r1 predictions (junior, 2026-11-12, written before the run)
Scenario tools/scenarios/c142-185-r1.txt (c56-rejoin route, card 183 form, plus card 142's Part 1). One build with
card 142.
- The Morita_C_1 pickup raises the three TacSupp range targets (_0, _1, _3, op 1 = PlayFromStart), as in c183-r3.
- At the rejoin the host logs, for each of the three, "card098: host t=... replay timeline
  BP_KillableBuilding_Base_TacSuppRangeTarget_C_n|Timeline: position P of L, playing 0, reversing 0 (op 1)", with
  P = L (the raise ended before the Logout). It sends 0 timelines as gone.
- The client logs, for each, "card098: client t=... world L ... applied timeline state: position P of L, playing 0,
  reversing 0", with the same P and L as the host.
- The digest round after the rejoin has 0 mismatches: 0 TacSupp location mismatches (c183-r3 had 3, z -250 against 0).
- Live 'L' rows (before the disconnect) still log "applied timeline call 1", as in c183-r3.
- **WARN classes** against c183-r3: host only "attach: DisableThreadLibraryCalls"; client only that one (the 3
  "mismatch ... TacSupp" lines and the round WARN gone).

## Report (run c142-185-r2, build 57504b1e4440dfe0)
The run tag is c142-185-r2. The scenario file is tools/scenarios/c142-185-r1.txt and was not changed between the
runs. Run r1 (DLL 88a461e02b1b828d) stopped in card 142's Part 1 before it reached a rejoin, so it gave no card 185
result. Run r2 is run 2 of 3 for this card. The DLL hash comes from `sha256sum | cut -c1-16`.

### Files changed
- mod/src/ubw/world_c98.cpp:
  - the host replay sends the timeline state (c185_host_find, c185_replay_value, and a change in c98_replay);
  - the client applies it for replayed rows (c185_client_state, and a branch in c98_client_timeline).
- tools/symbols.txt: 5 entries under "# card 185": UTimelineComponent SetPlaybackPosition 0x042D9FB0,
  GetPlaybackPosition 0x042CBB20, GetTimelineLength 0x042CC240, IsPlaying 0x042D0840, IsReversing 0x042D0860.
- mod/src/offsets.generated.h: regenerated by `python3 -I tools/pdb_rva.py gen`. The diff is only those 5 entries.
- mod/src/ubw/hooks_internal.h: no change was needed.

### Summary
- **Value bits.** A replayed 'L' row keeps its row text format. The 32-bit value field (parsed with %u on the
  client) holds:

  | Bits | Meaning |
  |---|---|
  | 0-3 | the op (kC185OpMask) |
  | 0x10 | kC185State: the host's state follows |
  | 0x20 | playing |
  | 0x40 | reversing |
  | 0x80 | kC185EndByOp: the host's timeline is gone |
  | 8 and up | position in ms (24 bits, clamped) |

  Live rows still carry the op alone, so the client takes the old path for them.
- **Validity check (c185_host_find).** The host looks the timeline up again rather than reading the journal's
  pointer:
  - It splits the row name at '|'.
  - It finds the owner with c95_find_nearest, by name and location, over the world's live level actor lists.
  - It reads the timeline property with prop_object_field.
  - The result must equal the journal row's actor (compared only, never read) and have the class name
    "TimelineComponent".

  If any step fails, the row goes out as the end state of its op with kC185EndByOp, and the host counts it as gone.
- **Client.** For a replayed row with the state or end-by-op bit set, the client:
  - calls SetPlaybackPosition(pos, false, true) (none for Stop with end-by-op);
  - calls Play or Reverse only when the host's timeline was playing;
  - logs "applied timeline state" (capped).

  Live rows are unchanged.
- **Not done.** The asset-list sweep for timeline classes that never fired in a log is not done. The class list in
  the trace comes only from host log rows.

### Verification (c142-185-r2)
Host, at the rejoin replay:
```
card098: host t=114748504 replay timeline BP_KillableBuilding_Base_TacSuppRangeTarget_C_1|Timeline: position 1.00 of 1.00, playing 0, reversing 0 (op 1)
card098: host t=114748504 replay timeline BP_KillableBuilding_Base_TacSuppRangeTarget_C_0|Timeline: position 1.00 of 1.00, playing 0, reversing 0 (op 1)
card098: host t=114748505 replay timeline BP_KillableBuilding_Base_TacSuppRangeTarget_C_3|Timeline: position 1.00 of 1.00, playing 0, reversing 0 (op 1)
card098: host t=114748505 world journal replay to BP_PlayerController_C (first Pong of the map): 26 of 26; timelines with state 3, gone 0 (map totals)
```
Client (value 256017 = 0x3E811: position 1000 ms, the state bit, op 1):
```
card098: client t=114748565 world L BP_KillableBuilding_Base_TacSuppRangeTarget_C_1|Timeline 12065 15300 -532 256017 (BP_KillableBuilding_Base_TacSuppRangeTarget_C): applied timeline state: position 1.00 of 1.00, playing 0, reversing 0
card098: client t=114748565 world L BP_KillableBuilding_Base_TacSuppRangeTarget_C_0|Timeline 12665 14500 -482 256017 (...): applied timeline state: position 1.00 of 1.00, playing 0, reversing 0
card098: client t=114748566 world L BP_KillableBuilding_Base_TacSuppRangeTarget_C_3|Timeline 11046 14823 -482 256017 (...): applied timeline state: position 1.00 of 1.00, playing 0, reversing 0
```
Live rows before the disconnect are unchanged (value 1, the op alone):
```
host:   card098: host t=114739911 world L BP_KillableBuilding_Base_TacSuppRangeTarget_C_1|Timeline 12065 15300 -532 1: sent to 1 of 1 clients
client: card098: client t=114739915 world L BP_KillableBuilding_Base_TacSuppRangeTarget_C_1|Timeline 12065 15300 -532 1 (...): applied timeline call 1
```
Digest rounds after the rejoin show 0 mismatches. c183-r3 had 3 TacSupp location mismatches here.
```
card132: client round 3: actors 1496 host, 1496 client, mismatches 0 (classes -); slots differing 0 ...
card132: client round 4: actors 1496 host, 1496 client, mismatches 0 (classes -); slots differing 0 ...
```
All predictions held. Run summary: ended by scenario after 157 s, host rc 0, client rc 0, drive rc 0,
FATAL/ERROR 0 on both sides, no crash dumps.

### WARN diff against c183-r3
- Host: 1 WARN, "attach: DisableThreadLibraryCalls failed, err=126", the same as c183-r3.
- Client: 3 WARNs.
  - "attach: DisableThreadLibraryCalls failed" is the same as c183-r3.
  - The 3 TacSupp "mismatch ... location" lines and the c183-r3 round-2 WARN are gone.
  - Two WARNs are new, both in round 1, before the disconnect:
    ```
    WARN card132: mismatch STLevelObjective O:ShootingRange_SO1 presence host 0 client 1; no row sent
    WARN card132: client round 1: actors 1494 host, 1495 client, mismatches 1 (classes STLevelObjective:1) ...
    ```
    These are a timing race in the digest, not this card's code:
    - The host took its round-1 snapshot at t=114717843 (S=628).
    - The level script's AddSubObjective(ShootingRange_SO1) ran on the client at t=114717847 and on the host at
      t=114717855, 12 ms after the snapshot.
    - Rounds 3 and 4 have 0 mismatches.

    c183-r3 did not have this race because its route timing was different.

Report complete.

## Senior review (2026-11-12)

Accepted on c142-185-r2 (build `57504b1e4440dfe0`). Diff read: `world_c98.cpp`, `tools/symbols.txt`, generated
offsets; no function over 60 lines. Checked in the logs myself: host replay "26 of 26; timelines with state 3, gone
0"; three client "applied timeline state: position 1.00 of 1.00, playing 0, reversing 0" lines (value 256017);
digest rounds 3 and 4 after the rejoin 0 mismatches (c183-r3 had 3 TacSuppRangeTarget location mismatches there);
live rows still "applied timeline call 1".

Unproven, carried to card 127's watch list: a timeline that is playing or reversing at the replay (position mid
way, client continues), and the "host timeline gone: end by op" path. The asset-list sweep was not done.
