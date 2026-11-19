# 311 Range target Activate is lost when the client reloads the world

From the b14 C1M1 soak (`logs/b14-soak-c1m1-*`, DLL `17ac07260788d9b3`). Senior trace 2026-11-14, logs plus code.
Cause settled; code may start.

## Symptom

After a host checkpoint load, `BP_KillableBuilding_Base_GrenadeRangeTarget_C_1` is active and moving on the host
(z -471 to -436 across digest rounds 40-51) and sits at its rest place on the client (z -553 every round). 26
`WARN card132: mismatch ... location` rows on the client, rounds 40 on, never converging.

## Cause

Card 065 relays a range target's `ADLevelTriggerReactions` `Activate(reset)` live only: `detour_c65_activate`
(`objectives.cpp:3139`) calls `c65_send_activate`, which sends `ChanOp::Activate` to the PCs connected at that
moment. Nothing journals it.

On a checkpoint load the client reloads the map (client log: `UEngine::LoadMap enter` at 280210910), so its
placed copy is back at its load state. The host's copy is restored active by the save. The world journal replay
(280214308 to 280214477) then carries the target's timeline row (`applied timeline state: position 5.28 of 6.00,
playing 1`), but no `Activate` row: the client has one `card065: client ... Activate(1)` row for this target in
the whole run, at 279661365, before the loads. The timeline plays on a copy whose Blueprint never ran the
activation, so the copy does not move.

Same path, same loss: a client that joins after the Activate, and the two `TargetRangeTarger` targets (host rows:
3 `Activate(1)` in the run, 1 grenade target, 2 range targets).

## Fix

`Activate` on a range target is lasting world state, so it goes through the world journal (card 098), not a bare
chan op.

1. Host: journal each relayed `Activate(reset)` by owner name, component name and location, the way the 'L'
   timeline rows are journalled. A later `Activate` or the target's destruction replaces or removes the row (card
   098 unjournal rule).
2. Replay order: the `Activate` row before the same actor's timeline state row, so the Blueprint's activation
   runs first and card 185's position and play state land on top.
3. Client: the replayed row calls the same `g_orig_c65_activate` as the live row, effects muted during the replay
   (`c98_fx_quiet`).
4. Host after its own checkpoint load: confirm from the log whether the restored target counts as activated
   without a new `Activate` call. If the save restores it silently, the journal row must survive the load
   (journal rows are per map, not cleared by an in-place load) or be rebuilt from the live component's active
   state at replay time. State which one in the report.

## Sibling sweep (report before coding)

List every `ChanOp` the host sends for a lasting world or objective state and mark it journalled or live only.
Each live-only one with lasting state is the same defect; fix in this card when the change is the same journal
row, else card it.

## Step 1 result: item 4 and sibling sweep (junior 2026-11-14, senior accepted)

- Host load is in place (`card121 ... save engine load runs`, 280196449 and 280207954; no `LoadMap enter` after
  map start). `g_c98w` is cleared only at `LoadMap enter`, so the journal row survives; the save restores the
  target active with no new `Activate` call (3 host rows in the run, none after a load).
- A save taken before the `Activate` would restore an inactive target under a row that says active. So the replay
  is gated on the component's live `bIsActive`: active replays, inactive logs `skipped (inactive on host)`.
  Senior condition: the journalled row logs `bIsActive` right after the host's `Activate`, so the gate's premise
  (this class's `Activate` sets it) is shown by the first row, not assumed.
- Sweep over every `ChanOp`: `Activate` is the only live-only op with lasting state. All others are journalled
  and replayed at the first Pong, re-sent per new copy pawn, client requests, or events. No other card.
- Plan: journal kind 'C' in `world_c98.cpp`, one row per owner, last call wins; replay in pass 0 before 'L';
  unjournal on the owner's destroy.

## Rows

- Host: `card311: host journalled Activate(<reset>) <owner> <component>; journal <n>`, and at replay
  `card311: host replay Activate(<reset>) <owner> <component> to <pc>`.
- Client: `card311: client replayed Activate(<reset>) <owner>.<component>: <activated | no such actor | no such
  component>`.

## Predictions (proof inside the next C1M1 run that does a checkpoint load after the grenade range)

- After each client world reload: one `card311: client replayed Activate(1)` row per active range target, before
  that target's `applied timeline state` row.
- Digest rounds after the load: 0 `card132: mismatch` rows for `GrenadeRangeTarget` and `TargetRangeTarger`.

## Done when

Both predictions hold in one run and the sibling list is in the report.

## Closed 2026-11-16 22:30

run `c315b168a` (b168 `0c097c2488d82959`): after the checkpoint reload the client logged `card311: client ... replayed Activate(1) ... activated` for both active range targets (526627218), one ms before their `applied timeline state` rows; 0 `card132: mismatch` rows. Sibling list: step 1 section above.
