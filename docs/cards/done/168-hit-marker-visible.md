# Card 168: The relayed hit marker shows on the client

Status: CLOSED, outcome in docs/CARDLOG.md. Was: queued 2026-11-09, card 166 remainder. Budget: static first, then one run, batched with the card 145
MoritaSmall proof and the card 146 spot log.

Read: card 166 (all of it), mod/src/ubw (card 166 relay).

## Why

The client's own pawn now gets OnDamageCaused for each claimed hit (23 of 23), and WB_Hit_Marker is bound to it.
No marker shows in stills 34 and 172 ms after an event. Leads: the widget checks its player character against the
event's Causer or Instigator (the host's real event has Causer = ADProjectile, the relay sets the own pawn);
IsHitIndicatorEnabled at its default; or a marker animation too short for the stills.

## Do

1. Static first: read WB_Hit_Marker's bound handler (Blueprint bytecode or its native thunk) and write down every
   condition between the delegate call and the visible change (visibility, animation play, opacity). Write the
   prediction for the relayed event's arguments.
2. Make the relayed event match what the handler needs: the same Causer class and Instigator as the host's real
   event, with the client's own objects.
3. Proof by a log, not by stills: hook or read what the handler changes (the widget's visibility, or PlayAnimation
   on its marker animation) and log it per event. Control: the host, armed, fires one burst; its own marker change
   logs the same way.
4. Fix the client's "card166: host marker relay map totals" side label.

## Done when

- The client logs the marker's visible change for each relayed hit, the same as the host's control does for its
  own hits.
- The same run shows card 145's prediction (a respawn under a standing player logs E 1 then E 0 in the same ms, no
  card132 pickup mismatch) and card 146's spot log, if the scenario holds both without widening; otherwise say which
  one is left.
- The usual Report items (WARN diff, run length, rc, ERROR/FATAL, CRASH, dumps, DLL hash). It ends with "Report
  complete."

## Static trace (junior, 2026-11-09)

WB_Hit_Marker's bytecode (pakchunk0, extracted to the scratchpad, read with a Kismet listing):

- Construct and SettingUpdated set bShowHitMarker = GetSTGameUserSettings()->IsHitIndicatorEnabled().
- OnCharacterCausedDamage_Event (bound to the player character's OnCharacterCausedDamage): popflowifnot(bShowHitMarker),
  then OnDamageCaused(DamageData).
- OnDamageCaused, in order. Any of these stops the marker:
  1. DamageData.ReceiverTags has `Actor.Hitmarker.NoMarker`.
  2. A DamageType of `DamageType.NoHitmarker`. `DamageType.LimitedHitmarker` passes through a gate that a
     TimeBetweenLimitedHitmarkers timer reopens.
  3. `EqualEqual_ObjectObject(DamageData.Instigator, DamageData.Causer)`: equal stops it.
- Then the marker plays. If DamageProcessResult.FinalDamage < 2.0, it is Play Hit(Hit_NoDamage, InvincibleHitSFX).
  Otherwise it reads ValueMap[`DamageModifier.CustomModifierAmount`]: 0 gives Hit_Regular, more than 1 gives
  Hit_Weakpoint, and anything else gives Hit_StrongPoint.
- Play Hit runs PlayAnimation(Animation, 0, 1, Forward, 1.0, false) and a 2D sound.
- OnCharacterKilledNPC_Event: bShowHitMarker, then Kill Caused. It stops on a tag of `Actor.Hitmarker.NoDeath` or
  `NoMarker`, and otherwise plays Play Hit(Hit_Kill, MS_GORE_2D_Kill_Hitmarker). There is no Causer check here.

Leads settled:
- Lead 1 is the cause. The relay set Instigator = Causer = the own pawn, so check 3 dropped every relayed damage
  marker.
- Lead 2 is not the cause. bShowHitMarker comes from the same user setting on both sides, and the host's own marker
  shows. Only the Causer's identity is checked, not its class, so a weapon Causer is enough. The host's real Causer is
  an ADProjectile.

Fix (marker_c166.cpp c166_receive): Causer = the own pawn's equipped weapon (`g_get_equipped_weapon(c24_inventory)`),
falling back to the PC with no weapon. It is never the pawn. The client log line now names the causer.

Proof hook: `UUserWidget::PlayAnimation` (0x3627590, detour_c168_play_animation). For a WB_Hit_Marker_C self, it logs
`card168: <side> t=... WB_Hit_Marker PlayAnimation <anim>`, capped at 40 lines per map. Map totals carry the count.

Side label: c166_arm_map labels its totals line by its counters (received or applied means client), not by side().
The client arms its map before g_client_session is set.

DLL 017270f302526af7 (on top of d2c8795e51251837, card 145's fix).

## Predictions (written before run c168-r1)

Scenario tools/scenarios/c168-markers.txt, FED_C1M1, FAST=1, GOD=0, host env UBWCOOP_C146_NO_START=1.

1. Control: the host's armed burst logs `card168: host t=... WB_Hit_Marker PlayAnimation Hit_Regular` (or another
   Hit_ animation) within 5 s.
2. Client: for each `card166: client ... OnDamageCaused on own pawn ... causer BP_Weapon_MoritaMk1_C_...`, a
   `card168: client ... PlayAnimation Hit_...` follows in the same ms. When the bug dies, it logs Hit_Kill. The client's
   PlayAnimation count equals its applied count, unless a LimitedHitmarker gate or a FinalDamage under 2 changes the
   animation. A count short of the applied count is a failure, and it gets written down.
3. The client's map totals line at the rejoin reads `card166: client marker relay map totals`, with the card168
   animation count.
4. Card 145: with the client standing on BP_AmmoPickup_MoritaSmall_C_0 at its respawn, the host logs
   `world E BP_AmmoPickup_MoritaSmall_C_0 ... 1` and then `... 0` in the same ms. Two digest rounds after it show
   mismatches 0.
5. Card 146 spot log: the host's corpse lies on the anchor, so the host logs `the checkpoint spot is ... uu from
   BP_PlayerCharacterMI_C...'s pawn (alive or dead): a side spot is taken`, then `spawns at beside the last checkpoint
   (x, y, z) instead`. The client's own pawn is within 300 uu of that point at +2 s and again at +5 s.
6. Card 167: either the WARNs come back with the new diag lines, or none of them appear. The report says which.

## Run c168-r1 (DLL 017270f302526af7)

Ended by the scenario after 193 s. Host rc 0, client rc 0, drive rc 1 (line 64). FATAL/ERROR 0, no CRASH, no new
dumps. WARN: host 2 (the attach and GOD config pair), client 11 (the config pair and the known card 132 STAISpawner_5 /
STSpawnPoint_7/_8 z rows on rounds 1 and 2, plus BP_Dropship_LandingBlocker_C_0 on round 2). Nothing new.

Card 168 (predictions 1 and 2): met.
- Control: the host's burst logged `card168: host t=285387206 WB_Hit_Marker PlayAnimation Hit_Regular_INST`, then
  Hit_Regular at 285387388 and Hit_Weakpoint at 285387491.
- Client: every relayed event names `causer BP_Weapon_MoritaMk1_C`. Before the kill there were 6 applied events, and 5
  played in the same ms: Hit_Regular at 285387866, 285387969, 285388174 and 285388277, and `Hit_Kill_INST` at
  285388363 with the OnDeathCaused.
- The one event with no animation (285388054) came 85 ms after the previous animation. The other gaps are 103 ms or
  longer, and the host's control gaps are 103 and 182 ms. Its tags and values match the events that played. The
  marker's ubergraph has a second Gate reopened by a TimeBetweenLimitedHitmarkers timer, so the cause is most likely
  that rate gate, which is the game's own behavior. This is inferred, not proven: the host's own hits are not logged
  one by one, so there is no host count to compare.
- The 10 events after the kill carry `HealthAndDamageSystem.Dead Actor.Hitmarker.NoMarker Corpse`, so no marker
  plays for them. That is check 1, the game's own behavior.

Prediction 3 (totals label): not reached. The run stopped before the rejoin map arm.

Card 145 (prediction 4): not tested. Scenario defect. The ammo pickup sits 30 uu from the Morita weapon pickup, so
the client took it at 285347402 while arming (line 19). It respawned at 285377413 with no one on it. Line 60's `try`
wait had no since-mark, so it matched the arm-step take. The client then stood on the enabled pickup after a warp and
fired, and no take followed, so line 64 timed out after 60 s. Side note, not chased: the warp is a non-sweep
teleport of the server copy (testrun.cpp c75_warp).

Card 146: not reached.

Card 167 (prediction 6): no `card167` or far-hit WARN in either log, and both sides fired armed bursts at a
WarriorBug.

## Run c168-r2 predictions (written before the run)

Scenario tools/scenarios/c145-146-respawn.txt, the same DLL and environment. Card 168's own part is done in r1, so
this run carries the card 145 and card 146 parts and the client totals label.

1. Card 145: the client's arm step logs `E BP_AmmoPickup_MoritaSmall_C_0 ... 0` since the mark. The client stands
   still and fires. Within 40 s the host logs `E ... 1` and then `E ... 0` in the same ms. The two digest rounds after
   that show no BP_AmmoPickup_MoritaSmall_C mismatch. The known spawner and LandingBlocker rows can still show.
2. Card 146 is the same as prediction 5 above, with the +2 s and +5 s near checks.
3. The client's map totals line at the rejoin reads `card166: client marker relay map totals`.

## Run c168-r2 (DLL 017270f302526af7)

116 s, host rc 0 and client rc 0, FATAL/ERROR 0, no crash dumps. Drive rc 1 at line 28. WARNs: host 2 (the attach
and GOD config pair); client 6 (the config pair, then round 1's known STAISpawner_5 and STSpawnPoint_7/_8 z rows).

Card 145 (prediction 1): not tested. Scenario defect again; the fix itself was not exercised.
- The arm-step take came since the mark as predicted: host `E ... 0` at 285713739 (AddAmmunitions on the client's
  copy, Morita 0 to 120). The client's mirror then raised the copy to 240 (285713892).
- The respawn sent `E ... 1` at 285743747. No nested take followed, so line 28's `E ... 0` wait timed out.
- Cause, from the exe: ASTAmmoPickup::CanBePickedUp_Implementation (0x1451C0F10) gets the overlapping character and
  checks each of the pickup's AmmunitionTypes (0x145232340 per entry). It returns false when no type can take more.
  The client's 1200 ms burst emptied part of the magazine only. Its reserve stayed 240 of 240, and its last mirror
  (285713862) read `AmmunitionType.Morita 240`. So the host's copy was full and the pickup correctly refused it. The
  game does the same for a full player.
- The same cause explains r1's side note (no take after the warp onto the enabled pickup): that burst also left the
  reserve full. The non-sweep warp is not the cause.
- Rounds 2 and 3 (285745803, 285775804): mismatches 0. Host and client agree that the pickup is enabled.

Card 146 and the totals label: not reached.

## Run c168-r3 predictions (written before the run)

Same DLL and environment. The scenario now reloads after the burst (`client key R`), so the Morita reserve drops
below 240. Two new waits come before the respawn: the client's `AmmoMirror sent` with Morita below 240, and the host's
`AmmoMirror from` with Morita going from 240 to that value.

1. Card 145: the respawn sends `E BP_AmmoPickup_MoritaSmall_C_0 ... 1`, then `E ... 0` in the same ms (the nested
   take, about 30 s after 285713739's equivalent). There is also a card071 AddAmmunitions line on the client's copy.
   The two digest rounds after that show no BP_AmmoPickup_MoritaSmall_C mismatch. The known spawner rows can still
   show.
2. Card 146: prediction 5 above, with the +2 s and +5 s near checks.
3. The client's map totals line at the rejoin reads `card166: client marker relay map totals`.

## Run c168-r3 (DLL 017270f302526af7)

`c145-146-respawn.txt`, 140 s, host rc 0, client rc 0, drive rc 0, FATAL/ERROR 0, no new crash dumps.
WARNs: host 2 (the attach and GOD pair). Client 12: that pair, rounds 1 and 2 of the known STAISpawner_5 /
STSpawnPoint_7/_8 (round 2 also BP_Dropship_LandingBlocker_C_0) locations (card 140's written cause: attached to
BP_ProceduralDropshipTroops_C_3, frozen past the net cull distance; card 144's class), and one new BP_AmmoPickup_MoritaSmall_C
row in round 2. Round 3: 0 mismatches.

1. Card 145: partly held. The reload brought the mirror to Morita 227 on both sides. At 286251126 the host sent
   `E ... 1`, the card071 AddAmmunitions on the server copy (227 to 240), DisablePickup, then `E ... 0`, all in the
   same ms. The client applied both. But round 2 (286253159, 2 s later) read `mesh visible host 1 client 0`, last row
   seq 303 (CollisionOp). Cause, from AADPickupBase::OnUpdatePickupStatus 0x04C61870: it calls IsPickupEnabled
   (vtable +0x760) once, at entry, into edi. It then calls the collider's SetCollisionEnabled (vtable +0x768; return
   address exe+0x4C618A1, the caller the card095/137 lines name), which runs the nested take synchronously
   (DisablePickup, then the nested OnUpdatePickupStatus hides the mesh). After it returns, the outer call runs
   SetVisibility(mesh +0x2F0, edi = 1) and the effect component +0x330's SetVisibility and activate with the same stale
   1, then tail-calls SetPhysicsEnabled(+0x400) at 0x04C62370. So on the host a taken pickup (bEnabled 0, collision 0)
   shows its mesh until the next respawn. This is the game's own order, and single player has it too. The client hid
   the mesh when it applied `E 0`. The respawn timer was not affected: the host sent `E ... 1` again at 286281136
   (+30 s).
   Fix (DLL 009d57589599d58d): `detour_c145_update_status` (weapons.cpp) reads bEnabled before the original. If the
   byte changed during the call, it logs `card145: <side> t=... pickup <name>: bEnabled 1 -> 0 inside
   OnUpdatePickupStatus (nested take); status run again for the visuals`, and runs the original once more. That
   second call reads 0 and hides the mesh and the effect. SetPhysicsEnabled compares the current state first, so the
   repeat is a no-op there. The journal is unchanged, so no extra row goes.
2. Card 146: held. The host logged `spawns at beside the last checkpoint (-1619, 1820, 396) instead`. The client was
   within 100 uu of it at both the +2 s and the +5 s checks.
3. Not tested. `c166_arm_map` prints the totals line only when a counter is nonzero. r3 has no marker events, and r1
   did not reach a map rearm after its markers. `c168-markers.txt` holds both (markers, then the 146 rejoin).

Card 167: none of its WARNs (`uu from the NPC`, `GrenadeExplode already went`) appear in c168-r1, r2 or r3, host or
client.

## Next-run predictions (DLL 009d57589599d58d)

- Card 145: at the respawn under the standing client, the host logs the `nested take; status run again` line in the
  same ms as `E ... 0`, with `mesh visible 1 -> 0` and the +0x330 component `1 -> 0`. The next two digest rounds show
  no BP_AmmoPickup_MoritaSmall_C row.
- Card 145, no sound or effect twice (senior's check). Static: the repeated call is the base
  AADPickupBase::OnUpdatePickupStatus 0x04C61870 only (an override's own code, sound included, is not repeated).
  It calls SetCollisionEnabled (0, already 0), SetVisibility on +0x2F0 and +0x330 (to 0), the +0x330 component's
  vtable +0x410 with 0 (the deactivate side), and SetPhysicsEnabled 0x04C62370, which returns when the state already
  matches. No spawn or play call is in it. Run check: no card136 host fx event and no extra sound or Niagara spawn
  line for BP_AmmoPickup_MoritaSmall_C_0 in that ms beyond what the take itself logs. The take's own lines appear
  once each (one card071 AddAmmunitions and one card050 DisablePickup).
- Totals label: after the markers and the rejoin, the client's line reads `card166: client marker relay map totals`
  with received > 0.

## Accepted (senior, 2026-11-09)

Accepted on c168-r1: the client's WB_Hit_Marker plays for relayed hits (Hit_Regular, Hit_Kill) like the host's
control, with the causer set to the equipped weapon. The one unplayed event (85 ms gap) and the corpse NoMarker
events are the game's own gates. Card 166 is closed with this card. Card 146's spot log held in c168-r3. Card 167
closes on 0 of its WARNs in r1-r3. Carried into card 163's run, with the predictions in "Next-run predictions": the
card 145 nested-take visuals fix (DLL 009d57589599d58d) and the client totals label.
