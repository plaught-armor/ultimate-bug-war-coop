# 257 FED_C2M1 `LOS_O05_SO04` stays open when the client pilots the mech

Found by card 255 run 2 (`logs/c255-c2m1-r2-*`). **Play blocker** once card 255 is in the build: the client
boards, SO01 to SO03 complete, SO04 ("Fend off the Arachnid Forces to establish an LZ") is added and never
completes, so SO05, SO06, `LOS_O05` and the level end never come.

## Evidence

- r5 (`logs/c241-c2m1-r5-host.log`, host pilots): SO04 added 177676981; self-destruct countdown 177706521;
  `LeaveMech` 177716529 to 177717759; mech destroyed 177720434; 177720655 SO05 added and SO04 complete in
  one tick (`ctx FED_C2M1_Scripting_C`, chain `0x5081E20 < 0x154FAE8 < 0x1545188`), 221 ms after the destroy.
- r2 (client pilots): SO04 added 180822443; countdown 180856797; `LeaveMech` 180866800 to 180866802 with
  `guard: ASTPlayerMechBase::RemoveMechInputContext ... skipped: controller not local`; mech destroyed
  180869808; no SO04 Complete in the next 43 minutes. Bugs spawn in both runs (WarriorBug lines).
- Card 241 table row 20 names the trigger as `Setup_Objective08_DefendArea` with `FinalWaveCompleteCount` /
  `WaveCompleteCount`. The script's name table also holds `MechBoarded`, `OnChildActorDestroyed`,
  `BP_EM_1FactionSetWaves_MechSuit` to `MechSuit4`.

## Step 1 (static, no run)

From the kis bytecode listing of `FED_C2M1_Scripting_C` (method of card 138), write into this card:
what completes SO04 (event, delegate or poll, with the function name), every read of a player in that chain
(`GetSTPlayerController`, `GetSTPlayerCharacter`, the pawn, the mech, `MechBoarded`), and which step does
not happen when the pilot is the client's server copy. Check against the two logs above (what the 221 ms
after the mech destroy is). List the same trigger shape on the other Trooper maps with a mech.

## Step 2 (fix)

The script gets from a client pilot what it gets from a host pilot, through the game's own event (as in
card 255); the mod does not complete the objective row. If the missing step is a mod guard (the
`RemoveMechInputContext` skip or card 075 code), fix the guard's side effect instead.

## Proof (1 run, `c257-c2m1`)

Scenario `tools/scenarios/c241-c2m1.txt` as it is (client boards). Predictions: (1) SO04 Complete row on the
host and replay on the client after the mech's end; (2) SO05, SO06, `LOS_O05` complete on both sides;
(3) `WB_TrooperLevelEnd`, both reach FED_C3M1; (4) no crash, digest only carded classes; (5) `LOS_O02_SO04`
completes (state why it did not in 255 r2 before the run and fix the step). Report N of 23.

Files: decided in step 1; `tools/scenarios/c241-c2m1.txt`; "## Report" in this card.

## Report

### Step 1 trace (static: kis listing of `FED_C2M1_Scripting_C`, native `ASTPlayerPresence::OnBeginOverlap`)

What completes SO04: the volume `BP_PlayerPresence_C_748` at (137967, -41112, 9726), event
`BndEvt__FED_C2M1_Scripting_BP_PlayerPresence_C_748_K2Node_ActorBoundEvent_7_VolumeTriggerDelegate`.
Chain: stub 22537 `goto 22345`; 22345 `EthanFound? = true`, PrintString `ETHANFOUND`,
`SetTriggerEventsEnabled(true)` on presences 78, 79, 81; `goto 17279` `Setup_Objective08_DefendArea()`
(ubergraph 15205: 15225 `AddSubObjective(LOS_O05, LOS_O05_SO05)`); `goto 15`: `CompleteSubObjective(LOS_O05,
LOS_O05_SO03)`, 52 `CompleteSubObjective(LOS_O05, LOS_O05_SO04)`, `Conversation09_EthanDeath_PartFour`,
`ClearMusicTriggers`. r5 matches: 177720655 SO05 add, SO03 and SO04 complete in one tick, 177720659 ext rows
748 `0x07000001 -> 0x07000000` (one-shot clear) and 78, 79, 81 `0x07000000 -> 0x07000001`.

Player reads in the chain: none. The delegate's actor parameter is unused; no `GetSTPlayerController`,
`GetSTPlayerCharacter`, pawn, mech or `MechBoarded` read. 748 is enabled from map load (state 0x07000001,
no enable row in either run; the 21500 enable in presence 76's event is a no-op re-enable).
Native `OnBeginOverlap` (0x050F6880) checks: trigger events enabled (+0x30a), authority, `+0x2a8` state byte,
no load in progress, other component alive with `+0x97 & 0x20`; then `OnTriggerEntered` (0x050FA160) broadcasts
the delegate and clears +0x30a when one-shot (+0x308). No player, controller or pilot check: the filter is the
collision profile (pawn capsule overlap).

Step that does not happen for a client pilot: no pawn capsule enters 748. It is not a client-pilot code path.
- r5 (host pilots): host mech warped to the SO04 marker `ActorReference_C_9` (137388, -42129), 1170 uu from
  748's center. Host trooper ejects, lands 177718285 at (137478, -41899, 9305), no fire. 177720433 the mech
  explodes, `GA_ShuntAbilityCharacter_C` on both pawns (knockback); 221 ms later 748 fires. The host trooper is
  next seen at (137801, -41436) (cmd 303), so the shunt pushed it into the volume.
- r2 (client pilots): host trooper stood at the marker (137376, -42132, 9367) from 180835026 to the end, no fire,
  so the marker is outside 748. Client trooper ejects at the mech (137134, -43183), lands 180867829 at
  (136866, -43330, 9461), 2300 uu from the center, and the shunt (180869807, server copy) pushes it away from
  the explosion, not toward 748. No 748 ext row in r2.
- The `RemoveMechInputContext ... skipped: controller not local` guard is not in the path:
  `pawn-collision: PossessedBy ... bActorEnableCollision 1 -> 1` at 180866802, the trooper lands and walks.

Same shape on the other Trooper maps (kis listings of FED_C3M1 to FED_C5M2 Scripting): every map completes
sub-objectives from `BP_PlayerPresence` BndEvts (29, 15, 21, 18, 17, 31 presence events), all without a
player check. No other map has C2M1's scripted mech run with self-destruct at an objective volume; they have
`BP_MechDropPlatform` call-ins (C5M2 also `BP_AIMechCallIn`). C4M1 `Stadium_SO3` takes
`BP_MechDropPlatform_C_1` as its marker and sets `mechReady`; the level script never completes `Stadium_SO3`.

LOS_O02_SO04 (prediction 5): r5 has the host row, `card049: host t=177209988 CompleteSubObjective(LOS_O02,
LOS_O02_SO04)`, from `BP_AmmoPickup_NukesLarge_C_1` (`AddAmmunitions ... Nuke 0 to 2`); the senior is right.
My 255 report missed it because I counted only `card097 ... subsystem ObjectiveComplete` rows. r2: the host never
takes NukesLarge. At cmd 83 the host fired one nuke (2 to 1), then at 180244247 re-took the respawned
`NukesSmall_C_0` 107 uu away (1 to 2, AT MAX), so it was at max when the goto (cmd 87) reached NukesLarge, and the
goto stalled 73 uu short. `warp actor ... 0` is out of range (WARN cmd 81, 86: dist 10-5000).

### Step 2 run `c257-c2m1` (1 of 1)

DLL `3d3f70b0bb6ba6de` (`dwmapi.dll`, `sha256sum | cut -c1-16`), as built; no mod change, no build. The run
ended by the scenario after 3021 s, host rc 0, client rc 0, drive rc 2 (25 failed lines, all after-effects
listed below or the pre-existing satchel `{g1}` lines 1228 to 1277). No crash dumps, FATAL/ERROR 0 on both sides.
The client piloted: host 185301505 `card075: host MechEnter from BP_PlayerController_C`, 185301506 `card255 ...
OnWantsToBoardMech mirrored`.

Files changed: the generator `gen_c241.py` (junior scratchpad), `tools/scenarios/c241-c2m1.txt` (regenerated,
1992 steps), this report.
- `so04_take`: two nukes into the sky, then `warp actor <pickup> 10` (was 0, out of the 10 to 5000 range).
- New block after the SO04 sweep: wait for `card098: host destroy BP_BaseMech_C`, then
  `host goto 137967 -41112 9726 80` (a walk), then the client goto to the same point. Up to 60 s on the SO04
  row. Then a `void` step ends the run if SO04 had not completed before the `c257-so04-check` shot.

Predictions:
1. Pass. 185413520 `card098: host destroy BP_BaseMech_C ... at (137420, -43159, 9664)`. 185414186 `cmd: 292
   goto: from 137388 -42129 9287 to 137967 -41112 9726, navmesh of 9 points, 2199 uu`. The host started at the
   SO04 marker, 0.67 s after the explosion: the knockback did not carry it in. 185416190 `goto: at 137764
   -41799 9338, dist 716`. 185416305 `card097: host ... ObjectiveComplete(LOS_O05_SO04)` and `card049: host sent
   CompleteSubObjective(LOS_O05, LOS_O05_SO04)`; 748 goes 0x07000001 -> 0x07000000 in the same tick. The
   client replays at 185416320. The host pawn was inside the volume when it fired; the client goto was skipped
   because SO04 had already completed. The goto ended `(reached)` at 185417356, 0 stucks.
2. Fail, partly. SO05 completes at 186435444 (client replay 186435472), but 1019 s after it was added at
   185416305: the scenario's zone hold (6 rounds of about 40 s plus the 400 s wait) ran out at about 1470 s run
   time. The SO06 card070 wait (180 s) also timed out at 1679.8 s, about 75 s before SO06 was added (186435443,
   marker `ActorReference_C_13`). So the SO06 sweep found no card070 line and skipped every candidate. SO06 and
   `LOS_O05` never complete. This is scenario timing: the SO05 hold is far longer than any wait in the
   scenario. It is not a sync fault, since both sides agree on every row.
3. Fail, as a result of 2: `WB_TrooperLevelEnd` never opens and FED_C3M1 is not reached.
4. Pass. No crash. The digest has 100 client rounds; 2 are not 0: round 17 `mismatches 1 (classes Pose:1)` and
   round 18 `mismatches 3 (classes Pose:3)` (card 251). No new WARN classes compared with c241 r5 on the host.
   On the client, `card137 corpse arrival` (1 line) and `card140 pose freeze check` (2 lines) are absent from
   r5 but present in c255 r2: known classes.
5. Pass. 184896833 `card071 ... AmmunitionType.Nuke asked 1, 0 to 1 (max 2)` at the NukesSmall_C_0 warp (both
   shots had left ammo at 0). 184896834 `card049: host sent CompleteSubObjective(LOS_O02, LOS_O02_SO04)`, with
   the client replay at 184896868. NukesLarge_C_1 was disabled in the same tick (the 10 uu warp overlapped
   both pickups).

Completed: 21 of 23 on both sides, the same set on each. Open: `LOS_O05_SO06` and `LOS_O05`.

Frame windows (last 60): host 653 to 1083 frames per 10 s, mean 9.2 to 15.3 ms, 0 to 1 over 50 ms.
Client 169 to 655 frames, mean 15.3 to 59.6 ms, 5 to 95 over 50 ms.

On foot: yes. The host walked the navmesh from the SO04 marker toward the volume centre, and the volume fired
2.1 s into the walk. The SO04 marker `ActorReference_C_9` (137388, -42129) is about 500 uu (2D) from the last
logged point before the fire (137764, -41799), and about 560 uu from the fire point (115 ms further at
700 uu/s). That point is about 650 to 716 uu from the volume centre. A player who follows the marker and walks
a few steps toward the LZ fires it. A player who stands on the marker does not (r2: 43 minutes).
The navmesh path ends 236 uu (2D) short of the volume centre.

### Step 2 result (senior read-out, run c257-c2m1)

Client pilots (card255 mirror line at 185301506). Mech destroyed 185413520; host goto to (137967, -41112) starts
185414186; SO05 added, SO03 and SO04 complete at 185416305 with the host at (137764, -41799), 716 uu from the
volume center, on foot. Client replay present. No crash. Not a mod fault; closed. SO06 and the level end stay
with card 241 (run 7 row).
