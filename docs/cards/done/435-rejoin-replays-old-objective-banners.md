# Card 435: a rejoining client replays every objective banner of the mission

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: fix written (`c435_join_popups_patch.py`, senior scratchpad), handed to the junior for b133. Proof owed.

## Seen

Run `c423b131a` (b131 `86739fe09499bfb8`, C2M1). The client reconnected 220 s into the map. Its shots after the
rejoin show the banners "Touchdown / Proceed out of the LZ" and then "Touchdown / Secure the LZ" while both objective
lists read MISSION ABORT; the host shows "Cut Me Some Flak", then nothing. Reported by the junior from the
`c423-gate-after` and `c269-chariot-corpse` shots.

## Cause (static, objectives.cpp)

`c49_join_replay` sends a late joiner the whole objective journal: every library call since the map load, each with
its original bools. Host row: `journal replay to BP_PlayerController_C (first Pong of the map): 137 of 137 frames`.

- `AddObjective(ctx, id, actors, bShowMarkers, bShowInList, bShouldPopupOnAdd, bShouldPopupOnCompletion)`: the popup
  bools are flag bits 2-3. `LOS_O01` went out with flags 0xB (popup on completion).
- `AddSubObjective(ctx, parent, id, bUseParentsData, actors, bShowMarkers, bShowInList, bShouldPopupOnAdd,
  bShouldPopupOnCompletion)`: bits 3-4. `LOS_O01_SO01` went out with 0x8E (0x80 is the mod's host-exists bit; popup
  on add).
- `CalloutObjective` frames (4 on C2M1 by then) are journalled too and replayed.

So the joiner's game queues one banner per popup bool and per callout of the mission so far. The checkpoint snapshot
path (`c49_snapshot_add`) already sends popups off; only the plain journal replay did not. A join at map start is not
affected (0 of 0 frames).

## Fix

Host only, in the join replay (the journal itself is unchanged, so live frames keep their popups):

- An Add frame whose objective is complete on the host now is sent with both popup bools cleared.
- An Add of an objective still open keeps its bools: the joiner sees the banner of the current objective.
- `CalloutObjective` frames are not sent: a one-time banner, not state.
- The replay row gains `card435: N Add(s) of complete objectives sent without popups, M callout frame(s) not sent`.

## Prediction (C2M1 `c423-gates-c2m1.txt`, the reconnect step)

- Host replay row at the rejoin reads N > 0 and M = the callouts sent before the rejoin (4 on b131).
- Client shot after the rejoin shows no "Touchdown" banner; the objective list is the same as the host's.
- Client `card066` objective rows after the rejoin are unchanged from b131 (same objectives, same complete flags).

## Not covered

Whether `CompleteObjective` shows a banner of its own for an objective added with both popup bools off. If the shot
still shows a completion banner, the next step is to send the joiner the snapshot form instead of the journal.

## 2026-11-16 19:17: leftover in run `c423b158a` (senior)

`client-c423b158a-c435-rejoin-3s.png`: banner "Cut Me Some Flak / Use C4 satchels to Destroy Flak Bugs." 3 s after the
rejoin, for LOS_O03 (complete before the leave). Gone in the 10 s shot. Host row: `13 Add(s) of complete objectives
sent without popups, 2 callout frame(s) not sent`. Suspect: `SetObjectiveVisibility(LOS_O03_SO03) flags 0x83` in the
replay (the sub-objective's Add was hidden, flags 0x80); the filter passes every kind above `AddCountSubSubs`
unchanged. Second candidate: popup bits of the AddCountSub kinds. Fix ordered from the junior. Done when (replaces
"no Touchdown banner"): the 3 s shot after a rejoin shows no banner of a complete objective, the host row counts the
quiet visibility frames, the objectives panel is unchanged, digest 0 mismatches.

Static facts (19:19, senior): `SetObjectiveVisibility(id, marker, list)` has no popup bool; LOS_O03_SO03 was added with
`bools 00000`, so the banner is the game's own reaction to a hidden objective becoming listed. The fix goes in how a
replayed Visibility frame of a complete objective is applied on the client (quiet apply by field write, or replay it
after the Complete frame). Replay mix in `c423b158a`: 116 frames = 66 SetObjectiveVisibility, 21 AddSubObjective,
16 CompleteSubObjective, 5 AddObjective, 4 CompleteObjective, 3 AddCountSub, 1 Chapter. Efficiency candidate for card
432: only the last Visibility frame per objective carries state; the join replay could send one per objective.

## 2026-11-16 19:30: cause traced and fix (junior), build b159 `885a2976f01932ac`

Cause (client log `c423b158a`, t=516784422): the banner is not the replayed Visibility frame. The replayed
`CompleteSubObjective(LOS_O03, LOS_O03_SO02)` fires the client's own `subsystem ObjectiveComplete(LOS_O03_SO02)`; the
client's `FED_C2M1_Scripting_C` handler then calls `CalloutObjective(LOS_O03_SO03)` (caller exe+0x5081EFE), then
`SetObjectiveVisibility(LOS_O03_SO03) bools 11`, all nested inside the replay. Card 140 lets client-local calls through
while `g_c49.replay != 0`, so the callout ran. The host's own callout frame for LOS_O03_SO03 was already dropped by
this card ("2 callout frame(s) not sent: LOS_O02_SO05 LOS_O03_SO03"), so the banner came only from the client's
level script.

Fix (`mod/src/ubw/objectives.cpp`): every library-call frame of the join replay (kinds below `SetCount`; the mirror
kinds keep their flags, Chapter carries the level there) carries `kC435JoinBit` (1 << 8). `c49_receive` counts
`g_c435.join` around the replay of such a frame, and `detour_c49_callout` skips a client `CalloutObjective` made while
`g_c435.join != 0` (`c435_skip_callout`, row `card435: client ... skipped CalloutObjective(...) inside the join
replay`). Visibility frames are still applied as sent (state, digest-compared), so the senior's quiet field write was
not needed for this banner. The host replay row now also counts `N frame(s) marked as join replay`.

Done when (replaces "no Touchdown banner"): the 3 s shot after a rejoin shows no banner of a complete objective
(Mission Abort allowed); host row marked count >= 1; client row skipped CalloutObjective(LOS_O03_SO03); objectives
panel unchanged; digest 0 mismatches. If the banner persists, the visibility path is next.

Open sibling: a checkpoint snapshot replay may also fire client level-script callouts; not changed here.

### Proof run `c423b159b` (b159 `885a2976f01932ac`, 2026-11-16 19:31): passed

- Host replay row (t=517766809): `135 of 137 frames ... 13 Add(s) of complete objectives sent without popups, 115
  frame(s) marked as join replay ..., 2 callout frame(s) not sent: LOS_O02_SO05 LOS_O03_SO03`.
- Client (t=517766846): `card435: client ... skipped CalloutObjective(LOS_O03_SO03) inside the join replay ... (1 skips
  so far); caller exe+0x5081EFE`: the same caller as the b158 banner.
- `client-c423b159b-c435-rejoin-3s.png`: only the "Mission Abort / Enter the M7 Razorback." banner (the open
  objective); panel MISSION ABORT only. 10 s shot: no banner, same panel.
- Digest rounds 1-12, 0 mismatches; EXCEPTION 0 both sides. Run `c423b159a` before it ended by card 457 (host save
  thumbnail crash, not this diff's path).

## Closed 2026-11-16 19:32 (senior review of run `c423b159b`, build `885a2976f01932ac`)

Real cause of the leftover (junior trace): the client's own level script calls `CalloutObjective` from its
`OnObjectiveComplete` handler inside a replayed Complete frame. Fix: every library-call frame of the join replay
carries bit 8 (`kC435JoinBit`), and the client skips a `CalloutObjective` made while it replays such a frame.

Proof: host row `journal replay ... 135 of 137 frames ...; card435: 13 Add(s) of complete objectives sent without
popups, 115 frame(s) marked as join replay, 2 callout frame(s) not sent: LOS_O02_SO05 LOS_O03_SO03`; client row
`card435: client ... skipped CalloutObjective(LOS_O03_SO03) inside the join replay`. Shot
`client-c423b159b-c435-rejoin-3s.png` shows only the banner of the open objective ("Mission Abort / Enter the M7
Razorback.", the same text as the objectives panel); the 10 s shot shows no banner. Digest rounds 1 to 12: 0
mismatches. Whole scenario passed (474 ok steps, 0 FAIL, 0 crash).

