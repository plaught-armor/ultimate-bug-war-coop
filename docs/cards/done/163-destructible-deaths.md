# Card 163: Destroyed plasma bugs and destructibles end destroyed on the client

Status: CLOSED, outcome in docs/CARDLOG.md. Was: queued 2026-11-08, from docs/playtest-2026-11-08.md finding 18. Budget: three runs. Visible world state.

Read: docs/playtest-2026-11-08.md finding 18, card 098 (WorldOps).

## Why

The client's nuke killed BP_SatchelBuilding_Plasmabug_C on the host (objective LOS_O02_SO05 completed), but the
client got only a TurnInPlace timeline row, no death or destruction, and the bugs stayed.

## Do

The host's death or destruction of BP_SatchelBuilding_* and every destructible-spawner child is sent with its end
state (mesh broken or hidden, death animation, collision off). Sweep satchel buildings, bug nests and ADDestructible*.

## Done when

- A run: the host destroys one of each class; the client's actor is in the destroyed state (receiver-side check).
- The usual Report items (WARN diff, run length, rc, ERROR/FATAL, CRASH, dumps, DLL hash). It ends with "Report
  complete."

## Static trace (junior, 2026-11-09)

BP_SatchelBuilding_Base (pakchunk0, Kismet listing in the scratchpad) has two kill paths:

- ReceiveOnSatchelExploded(premature): Become Destroyed(premature). Card 098 relays it as `S <name> 0|1`, and the
  client runs ReceiveOnSatchelExploded on its copy.
- Its ADHealth damage event (BndEvt ... HealthCompDamageEventDelegate). The condition is:
  InstigatorTags has `DamageType.ObjectiveDmg`, and not Destroyed, and (the impact point is within
  ObjDmgAcceptableRange, or HitActor == self), and OriginalDamage > Health. Health here is a Blueprint double, not
  the ADHealth attribute. When it holds, the event runs SatchelPlacementSiteInstance->NeutralizeSite (if valid),
  then Become Destroyed(false), then Killed by Damage. Killed by Damage is empty in the base, and children may
  override it. Otherwise, Health -= OriginalDamage.

Become Destroyed runs SatchelSiteCAC->I80(false, false), the destroyed skeletal mesh, the montage, PFX and SFX,
gore, the BuildingDestroyed broadcast, and ApplyDamageAtLocation(DetonationDamageTemplate) on self. That last call
is a second damage broadcast, nested inside the first. It then sets Destroyed = true, runs HideActor if
DestroyOnExplode, and sets the destroyed static mesh.

The nuke in finding 18 took the damage path. That path never calls ReceiveOnSatchelExploded, so card 098 sent no
row. The other classes in the sweep:

- Bug nests (BP_DestructibleSpawner_*) die by their health attribute, and card 131's 'K' row covers them.
- No native ADDestructible* class exists in the exe.
- Corpses_Destructible are covered by K and H.

All BP_SatchelBuilding_* children (Flakbug, CTF, Plasmabug) inherit the base's damage path, so one fix covers the
whole class.

## Fix (DLL 29409f5630fc8715)

- Host, world_c98.cpp `detour_c163_broadcast`: a detour on FHealthCompDamageEventDelegate::Broadcast 0x04A3B490
  (4 callers: OnDeath +0xF8, +0x108, +0x118, a thunk), with its own orig pointer. Card 131's resolve of the raw
  address stays. The detour watches only the outermost broadcast, never one inside ReceiveOnSatchelExploded, and
  only when the receiver is a satchel building whose Destroyed is still false. If Destroyed is true after the
  original returns, the host logs
  `card163: host t=... <name> (<class>) destroyed by a damage event (Destroyed 0 -> 1): relayed as S 2` and sends
  card 098's `S` row with value 2, which is journaled for late joiners.
- Client, c98_client_satchel, value 2: it runs NeutralizeSite on SatchelPlacementSiteInstance, then
  ReceiveOnSatchelExploded(false), which is Become Destroyed(false), then Killed by Damage, all under the existing
  card 133 one-apply guard and the replay mute. It logs `card163: client t=... <name> damage kill applied: site
  NeutralizeSite run, Become Destroyed(false), Killed by Damage run; Destroyed now 1, hidden <0|1>`.
- Shared: c98_satchel_row, which builds the row for both kill paths, and c98_bp_destroyed, the Destroyed read
  that c98_dead_now now also uses. The dispatch accepts values 0 to 2.

## Predictions (written before run c163-r1)

Scenario tools/scenarios/c163-plasmabug.txt, FED_C2M1, FAST=1, GOD on, host UBWCOOP_GRANT_WEAPON=NukeLauncher.

1. The host's nuke logs one FireProjectile, then exactly one `card163: host ... (BP_SatchelBuilding_Plasmabug_C)
   destroyed by a damage event ... relayed as S 2` line, followed by its `card098: host ... world S ... sent to 1 of
   1` line. There is no second card163 line for the same building: the nested self-damage is not watched, and a
   later hit finds Destroyed already 1.
2. The client logs `card098: client ... world S ... applied ReceiveOnSatchelExploded`, then the card163 client
   line with `site NeutralizeSite run` (or `no site` if the instance is null on this building),
   `Killed by Damage run`, and `Destroyed now 1`. `hidden` is the same as the host's (DestroyOnExplode).
3. Receiver-side visual: client stills c163-edge and c163-5s show the destroyed mesh (or nothing), matching the
   host's stills.
4. The next two digest rounds after the kill show mismatches 0.
5. The host's objective list after the kill shows the Plasmabug sub-objective (LOS_O02_SO05a/b) progressed, and the
   client's list matches it.
6. No card163 WARN line (`site has no NeutralizeSite`, `no Killed by Damage`) appears, and the WARN diff against
   c168-r1 shows no new class.

Carried items, run on the same DLL with tools/scenarios/c168-markers.txt (FED_C1M1) as c163-r1m, right after
c163-r1: the predictions are in docs/cards/168-hit-marker-visible.md, "Next-run predictions". They cover card 145's
nested-take line (logged once, with no extra sound or FX in that ms) and the client totals label. The two runs go one
after the other, not on two lanes at once, because 13 GB is free and four game instances do not fit.

## Run c163-r1 (DLL 29409f5630fc8715)

The run took 87 s and ended by the scenario. Host rc 0 and client rc 0. FATAL/ERROR 0 on both sides. WARN 1 per
side: the usual `attach: DisableThreadLibraryCalls failed, err=126`, so the WARN diff shows nothing new. No new crash
dumps. Drive rc 0, with every step ok.

1. Held. FireProjectile(NukeLauncher) was at 287390399. One `card163: host t=287390468
   BP_DestructibleSpawner_Bugnest_HumanGameplay_C_0 (BP_SatchelBuilding_Plasmabug_C) destroyed by a damage event
   (Destroyed 0 -> 1): relayed as S 2` followed in the same ms, then `world S ... 2: sent to 1 of 1 clients`.
   There was 1 card163 line in the host log. The building's name is its parent nest's name (it is a child actor);
   card 095's nearest-by-class-and-location match finds the copy.
2. Held. The client logged at 287390497 (29 ms later): `world S ... 2 (BP_SatchelBuilding_Plasmabug_C): applied
   ReceiveOnSatchelExploded`, then `card163: client ... damage kill applied: site NeutralizeSite run, Become
   Destroyed(false), Killed by Damage run; Destroyed now 1, hidden 0`. Plasmabug does not set DestroyOnExplode,
   so the destroyed mesh stays.
3. Not shown by the stills. At +1 s and +5 s, both the client's and the host's views are filled by the nuke's fire.
   The receiver-side proof is the digest (item 4).
4. Held. Both digest rounds after the kill (rounds 1 and 2) logged mismatches 0, with 30300 actors on each side in
   round 2.
5. Not testable here. Both objective lists still show TOUCHDOWN / Secure the LZ (the run starts at the LZ), so the
   LOS_O02 Plasmabug objective is not active yet.
6. Held. There are no card163 WARN lines.

## Run c163-r1m (c168-markers.txt, DLL 29409f5630fc8715): host crash in the card 157 diagnostic

The host crashed at 287557244, 102 s in, on the host's control burst (scenario line 35). The crash was an
EXCEPTION_ACCESS_VIOLATION reading 0xA00 at ubwcoop+0xD6219, inside `object_name()` called from
`detour_c157_damage_caused`. A crash dump was written (UECC-Windows-DFB73F164D49EDEC206851BAEE9BC61B_0000), and the
drive returned rc 1.

This was not caused by card 163 or card 145. The hooked ASTPlayerCharacter::OnDamageCaused gets its
IADDamageCauserInterface subobject as `self` (pawn + 0x658). Card 166's `host_relay` subtracts that offset, but the
card 157 diagnostic read `self` as a UObject. It already logged garbage in c168-r1 (`OnDamageCaused on s (other)`,
`on None (other)`), and this time the FName read went to an unmapped address. Fix:

- `kCauserIface` moves to hooks_internal.h.
- The diagnostic logs `self - kCauserIface` and returns early on a null `self`.

Sweep: the only other interface-thunk hook (card 133's OnTimedInteractionCompleted) already resolves its object
through `iface_object`. New DLL 1cd475d8b5f57d58 (clean build, no warnings). The r1m predictions are carried to
c163-r2m unchanged, with one addition: every `card157: host ... OnDamageCaused on` line names a
BP_PlayerCharacterMI_C with side host own or server copy.

## Run c163-r2m (c168-markers.txt, DLL 1cd475d8b5f57d58)

193 s, host rc 0, client rc 0, drive rc 1 at line 64. FATAL/ERROR 0, no crash dumps. WARNs: host 2 (the attach and
GOD pair). Client 6: that pair and round 1's known STAISpawner_5 / STSpawnPoint_7/_8 rows (card 144's class).

- Card 157 fix: held. Past the old crash point with no crash. All 29 `card157: host ... OnDamageCaused on` lines name
  BP_PlayerCharacterMI_C: 4 host own and 25 server copy.
- Card 168 markers: host control Hit_Regular_INST at 287940778. The client's Hit_Regular_INST came at 287941359, and
  Hit_Kill_INST at 287942250.
- Card 145: not tested. This is the c168-r1 scenario defect again, my error in reusing c168-markers.txt unchanged.
  The client took BP_AmmoPickup_MoritaSmall_C_0 at the arm step (287901019). The pickup respawned with no one on it
  (287931025), during the marker steps. Line 60's wait had no since-mark, so it matched the arm-step take. Line 64
  then timed out.
- Card 146 and the totals label: not reached.

Scenario fix: the card 145 block from c145-146-respawn.txt (since-mark take, fire, reload, both mirror waits, then
the respawn under the still client) now comes straight after the client arms, in c168-markers.txt. The host arms
after it, while the ammo pickup is down again. The marker and card 146 blocks are unchanged.

## Run c163-r3m predictions (written before the run)

Same DLL and environment, the fixed c168-markers.txt.

1. Card 145: these come in the same ms: `E BP_AmmoPickup_MoritaSmall_C_0 ... 1`, then the `card145: host ... nested
   take; status run again` line, then `E ... 0`. The take's own lines appear once each: one card071 AddAmmunitions
   on the server copy and one card050 DisablePickup. Also in that ms, no card136 fx event and no sound or Niagara
   spawn line for the pickup. The two digest rounds after it show no BP_AmmoPickup_MoritaSmall_C row.
2. Card 168 markers: as in r2m.
3. Card 157: every `card157: host ... OnDamageCaused on` line names BP_PlayerCharacterMI_C (host own or server copy).
4. Card 146: the host logs `spawns at ... instead`. The client is within 300 uu of that spot at the +2 s and +5 s
   checks.
5. Totals label: after the rejoin, the client's line reads `card166: client marker relay map totals`, with
   received > 0.
6. FATAL/ERROR 0, no crash, drive rc 0, WARNs only the known pair plus the known spawner rows.

## Run c163-r3m (fixed c168-markers.txt, DLL 1cd475d8b5f57d58)

180 s, host rc 0, client rc 0, drive rc 0. FATAL/ERROR 0, no crash dumps.

- WARNs, host 2: the attach and GOD pair.
- WARNs, client 7:
  - the attach and GOD pair;
  - round 1's known STAISpawner_5 / STSpawnPoint_7/_8 rows (card 144's class);
  - one `card140: client pose guid 0x23c snap 13: frozen row simulating 906 ms` after the kill. This is the WarriorBug
    corpse, the card 152 ragdoll-start class that card 146's run already logged.

1. Card 145: held. At 288239073-288239074 the host logged these, in order:
   - `E BP_AmmoPickup_MoritaSmall_C_0 ... 1`;
   - one card071 AddAmmunitions on the server copy (227 to 240);
   - one card050 DisablePickup;
   - `E ... 0`, then CollisionOp PlayerCollider 0;
   - `card145: ... (nested take); status run again for the visuals: mesh visible 1 -> 0, NiagaraComponent visible
     1 -> 0`.

   No sound, Niagara spawn or card136 line came in that ms; the one Niagara match is the card145 line's own text.
   The client applied E 1 ("already"), E 0 and the CollisionOp at 288239085. Digest rounds 2, 3 and 4 had 0
   mismatches, so there is no BP_AmmoPickup_MoritaSmall_C row. The c168-r3 `mesh visible host 1 client 0` gap is
   gone.
2. Card 168 markers: held. The client's Hit_Kill_INST came at 288302114 (6 animations this map).
3. Card 157: held. All 27 `OnDamageCaused on` lines name BP_PlayerCharacterMI_C: 4 host own and 23 server copy.
4. Card 146: held. The host logged `spawns at the last checkpoint (13500, 13500, 372) instead`. The client was within
   100 uu of it at +2 s and at +5 s.
5. Totals label: held. `card166: client marker relay map totals: 0 sent, ... 24 received, 24 applied (23 damage,
   1 kills) ...; card168: 6 WB_Hit_Marker animations`.
6. Held.

## Accepted (senior, 2026-11-09)

Accepted on c163-r1 (Plasmabug building S 2, client destroyed state, 0 digest mismatches) and c163-r3m. Bug nests
rest on card 131's K row, and no native ADDestructible* class exists. The objective progress after the kill is not
testable from the LZ start and is left to a run that reaches LOS_O02. Card 145 closes on c163-r3m: one nested take, the
visuals fixed, no doubled sound or FX, 0 pickup mismatches. The c163-r1m host crash was the card 157 diagnostic's
interface read, fixed (kCauserIface 0x658). The new frozen-row WARN on guid 0x23c is card 152's.
