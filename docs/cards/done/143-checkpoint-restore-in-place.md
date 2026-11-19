# Card 143: A host checkpoint restore in place reaches the client

Status: CLOSED, outcome in docs/CARDLOG.md. Was: queued 2026-11-08, from card 137 run 1 (hb68) answer 3b. Budget: three runs. The host half (journals rebuilt from the actors after any LoadFromSlot) is built in card 137; this card keeps the case where a client is in the session during the restore.

Read: card 137 "run 1" report (3b trace), card 048 ("RESTART CHECKPOINT reloads in place on the host"), docs/STATE.md
"Card scope and investigation rules".

## Why

USTSaveSystemLibrary::Load (0x0520A580) has a same-level branch at 0x14520AD7C: PostSavedLevelLoadSetup,
ClearDeferredLoadParams, StartStreamingPause, LoadFromSlot (0x0520AE10) in place, ReactivateActorsInAllActiveLevels, no
OpenLevel. WB_PlayerDeathScreen "LoadLastSave" reaches it (LoadLatestSave), as do Load_Quicksave, execLoad and
USTDebugSaveTracker::Load. On the host this revives or resets barrels, pickups, objectives and NPCs with no map load, so
the client keeps its old world. Full world sync covers it.

## Do

1. Static: what LoadFromSlot in place changes on the host (ClearLevel, per-component Load, FullyLoaded) and which
   mod journals go stale (c98 K/S/H/X/M, c95, c49, c132 pose and decal, c136 loops). Choose one: the host forces a
   client map reload (as a reconnect does), or the host resets its journals and resends the world. Write the choice and
   a prediction before any run.
2. Fix in the layer that owns the restore. The client ends at the host's restored state.
3. One run: host dies, LoadLastSave, then a digest round at 0 mismatches and the restored barrels and pickups the same
   on both sides.

## Done when

- The trace, the fix, and the run lines. The usual Report items. It ends with "Report complete."

## Playtest 2026-11-08 addition (findings 22 and 24, host log)

- The case happened in play. Both players were down at host t=10046001 ("all down: host death"); the host clicked
  Load Last Save at host t=10063823 and again at 10089026. "journal reconcile after LoadFromSlot: c98 K dropped 26
  added 2". The client got no world reset: 42 digest mismatches (props, crates, barrels, pickups dead host 0 client 1).
- The load restored the client's server copy (save id 5050A3FA-...) at Health 1.0 and Armour 0.0, because the save
  row was written while it was downed; it was Downed again 10 s later (host t=10074854), and the client cycled Downed
  and Up from then on. The host's own pawn got Health 27.3, Armour 65.0 from its save.
- Scope added:
  - After LoadFromSlot the host sends the client a world reset (the join snapshot of world state), or the client
    re-runs the join path.
  - A load gives the server copy the health it had at the checkpoint, not a downed value: either a downed copy's
    health is not saved, or a respawn from a death restores full health. Check what the game does for the host's own
    pawn and match it.
  - The client's input and UI after the load go through card 156's back-to-Up owner.
- Added proof: the host loads with the client in session from a downed state; at +5 s the client's world digest has 0
  mismatches and its server copy's Health and Armour match the checkpoint values, and the client is not downed again
  in the next 30 s.

## Trace, choice and prediction (written before any run)

Trace:
- `detour_c137_load_slot` (save_c121.cpp) runs LoadFromSlot in place, then rebuilds the c98, c56 and c95 journals from
  the actors. The host world is right after it; the client keeps its old world.
- A world resend in place cannot work: the save revives props and NPCs the client has already destroyed, and the
  client has no save record to revive them from. The card 048 fallback (RESTART MISSION) loses the checkpoint.
- The client rejoin path (card 045 follow: SetClientTravel to the host URL, browse, map load, reconnected) gives the
  client a fresh map load. The join replays (c50_join_replay at the first Pong, card 137 run 2) then bring it to the
  host's restored world, as for any late join.
- A rejoin creates a new server copy: AssignStartingInventories gives starting weapons and Health 100. The copy state
  the save restored is lost, so the host snapshots it before the client leaves.
- The in-place load respawns both pawns and LoadAbilitySystemComponent restores them. A row saved while downed comes
  back at Health 1.0 (the card 043 hold) and Armour 0.

Choice: the client re-runs the join.
- Host, after the reconcile: for each server-copy PC, snapshot "<health> <armour>;<weapons c50_text>;<ammo
  c72_map_text>" from the copy pawn and send it with a new host-to-client op, Reload.
- Client on Reload: store the snapshot and start the card 045 follow (Retrying, at once). After the reconnect, at the
  first host Ping, send the snapshot back with a new client-to-host op, Restore, once.
- Host on Restore: hold it per PC; apply it once the new copy exists and its AssignStartingInventories ran (5 s
  fallback with a WARN): Health, Armour, weapons (c50 want/remove/give/equip), ammo (c72_apply, then the AmmoSet seed
  again).
- Downed at save: in detour_load_asc on the host, a restored player Health in (0, 1.0] becomes
  kC43ReviveFraction x MaxHealth (30), the revive value. The host's own pawn gets the same rule.
- Position is not restored: card 046 places the rejoined copy beside the host.
- Test support: a `travel save` drive command calls USTSaveSystemLibrary::Save_Quicksave on the host.

Prediction (one run, c143-restore.txt, FED_C1M1 FAST=1 GOD=0):
1. Both take a Morita; the client is downed; `host travel save` gives "card121: host ... save engine save runs".
2. The host kills two range targets, then `host travel checkpoint`: "save engine load done", the card137 reconcile,
   then "card143: host t=... Reload to PC ... (Health 1.0 -> snapshot 30.0 ...)".
3. Client: "card143: client t=... Reload received", "card045: client t=... reconnected", "card143: client t=...
   Restore sent".
4. Host: "card143: host t=... Restore applied ... Health 100.0 -> 30.0 ... weapons ... MoritaMk1".
5. The next digest round: mismatches 0 except the known BP_AmmoPickup_MoritaSmall_C class (card 145); the two
   targets are alive on both sides.
6. No "card043: client t=... own pawn Up -> Downed" in the 30 s after the restore.

## Run 2 findings and run 3 prediction (written before run 3)

Run 1 (c143-r1) stopped at the first `kill actor`: the scenario named the instance stem
`BP_KillableBuilding_Base_TargetRangeTarger_C`, not the class `BP_KillableBuilding_Base_TargetRangeTarget_C`.
This was a scenario error, not a code fault.

Run 2 (c143-r2, DLL 5121416e8b5308ab, drive rc 0) held predictions 1-4 and 6, with three exceptions:
- The second `kill actor` hit the same target again. A building has no health set, so the card 130 live filter keeps
  the dead one. One target was killed and then revived by the load ("c98 K dropped 1"). It matches on both sides
  after the rejoin.
- The Restore was applied on the late path ("late, no Assign mark", 5015 ms) with no WARN. The rejoined copy's
  AssignStartingInventories ran before the Restore came, but it logged the copy as host-own, because the side reads
  wrong before possession. The server-copy gate in c143_host_assign therefore never marked the copy. Fixes: the gate
  is removed (a mark on the host's own pawn is harmless, since the step looks up only the held controller's pawn),
  and the late path now logs a WARN, as the design said.
- Prediction 5 failed. The digest after the restore had 6 mismatches:
  - BP_WeaponPickup_MoritaMk1_C_0, host 1 client 0: the rack pickup class carried to card 145.
  - STLevelObjective O:ShootingRange_SO1, complete host 1 client 0: the join snapshot sent
    CompleteObjective(<sub-objective>), which the library ignores for a sub-objective. The client replay was a no-op
    ("objectives 16 -> 16"). The same class is in c132l2 (LOS_O01_SO01) on a plain late join. Fix in
    c49_snapshot_frames: a completed sub-objective goes out as CompleteSub(parent, id), as c97_call already does.
  - STAISpawner_5 and STSpawnPoint_7/8 location: card 140's attach group (attached to the moving
    BP_ProceduralDropshipTroops_C) is applied live on both sides, but a joining client's fresh map never gets the
    attach. This late-join gap is outside this card and is reported to the senior.

Run 3 prediction (c143-restore.txt; the second kill now names `..._TargetRangeTarger_C_2`):
1. Two different targets are killed; the reconcile reports "c98 K dropped 2".
2. Host: "card143: host ... AssignStartingInventories on BP_PlayerCharacterMI_C (host-own at the call): marked",
   then "Restore applied ... after its AssignStartingInventories", with no WARN.
3. The digest after the restore has no STLevelObjective mismatch. The remaining mismatches are only the
   card 145 rack pickup and the card 140 attach group (STAISpawner_5, STSpawnPoint_7/8).
4. No new down of the client in the 30 s after the restore.

## Report

DLL 74200d1c7baccb88 (sha256 cut 16). Three runs of tools/scenarios/c143-restore.txt (budget used: 3 of 3).

Code changes:
- mod/src/ubw/restore_c143.cpp, c143_host_assign: the server-copy side gate is removed. A rejoining copy reads
  host-own at AssignStartingInventories (before its possession), so the gate never marked it. The log now says the
  side at the call. The apply line is prefixed WARN on the late path (no Assign mark) or when the weapons do not
  match, as the design said.
- mod/src/ubw/objectives.cpp, c49_snapshot_frames: a completed objective with a parent is sent in the join snapshot
  as CompleteSub(parent, id), not CompleteObjective(id). The library ignores CompleteObjective for a sub-objective,
  so a joining or rejoining client kept it incomplete (c143-r2 O:ShootingRange_SO1; the same class as c132l2
  LOS_O01_SO01 on a plain late join). This mirrors c97_call.
- The scenario's second kill now names the instance `BP_KillableBuilding_Base_TargetRangeTarger_C_2`. A class kill
  hits the same dead building again, because buildings have no health set for the live filter.

Run 3 (c143-r3, drive rc 0, no crash dumps). The predictions held as follows:
1. Held. "journal reconcile after LoadFromSlot ... c98 K dropped 2": both targets were killed after the save and
   revived by the load.
2. Held. The host log shows "AssignStartingInventories on BP_PlayerCharacterMI_C (host-own at the call): marked
   (0 held Restore)", then "Restore applied ... (copy BP_PlayerCharacterMI_C, after its AssignStartingInventories,
   5 ms after it came): Health 100.0 -> 30.0 (want 30.0), Armour 100.0 -> 100.0 (want 100.0)", with weapons
   "removed 1, given 1, matches the list". The host has no card143 WARN (its only WARNs are the attach
   DisableThreadLibraryCalls line and the GOD-off config line). The saved downed state was restored at Health 30
   by the downed rule ("loaded at the downed Health 1.0: set to 30.0").
3. Held for the objective: rounds 3 and 4 after the restore have no STLevelObjective mismatch. Each round has
   6 mismatches. Three are expected: BP_WeaponPickup_MoritaMk1_C_0 pickup and mesh (card 145 rack), and
   STAISpawner_5 and STSpawnPoint_7 location (card 140 attach group). STSpawnPoint_8 matched this time.
   The prediction left out the other 2: BP_AmmoPickup_MoritaSmall_C_0 pickup and mesh visible, host 1 client 0.
   This is the known card 145 ammo-pickup class. It is in rounds 1 and 2 before the save as well, so the restore
   did not cause it. Its last row (seq 65, CollisionOp) was sent at the Restore apply's millisecond and applied
   on the client, yet the client still reads 0. That detail is for card 145.
4. Held. The client's only "own pawn Up -> Downed" is the scripted down before the save. No down follows the
   restore in the 30 s window.

Open, outside this card:
- Card 140 late-join gap. STAISpawner_5 and STSpawnPoint_7/8 are attached to the moving
  BP_ProceduralDropshipTroops_C. The attach is applied live on both sides, but a joining or rejoining client's
  fresh map never gets it, so the client sees the placed location. This is in c140g and c140m as well.
- Card 145: the rack weapon pickup and the MoritaSmall ammo pickup (host 1 client 0) after a join.

Report complete.
