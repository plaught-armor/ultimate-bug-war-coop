# Card 145: Weapons dropped from crates show on the client

Status: CLOSED, outcome in docs/CARDLOG.md. Was: queued 2026-11-08, from card 137 step 2c (hb68 re-read). Budget: three runs.

Read: card 137 step 2c and the run 1 P2 correction, docs/STATE.md "Card scope and investigation rules".

## Why

The playtest showed weapons from opened crates missing on the client. hb68 showed placed weapon pickups equal on both
sides, so the gap is in pickups spawned at run time from crates (KillableCrate drops), not in placed ones.

## Do

1. Static: how a crate spawns its drop on the host (the class, whether it replicates, and how its pickup state and
   visibility are set), and what the client receives today. Write the cause and a prediction before any run.
2. Fix in the layer that owns the drop. Every crate drop the host spawns shows on the client at the host's place, can
   be taken by either player, and goes away on both sides when it is taken.
3. One run: open crates near both players; host and client drop lists are equal, and a take on either side removes the
   drop on both.

## Done when

- The cause, the fix, and the run lines. The usual Report items. It ends with "Report complete."

## Playtest 2026-11-08 addition (findings 17 and 20)

- Pickup respawn: BP_AmmoPickup_NukesSmall_C_5 came back on the host after PickupGone, with no row showing its mesh
  (mesh visible host 1 client 0). A dropped BP_GrenadeAmmoPickup_Incendiary_C stayed 200 uu up and disabled on the
  client while the host's fell and turned on.
- Killable props: BP_KillableCrate_02/04/05/06_C and BP_KillableProp_ExplosiveBarrel_C_10/11/12 revive on the host
  with no row (dead host 0 client 1), and the host then sends K again ("already dead, skipped").
- Added scope: one "back" row (alive, health, visibility, collision, transform) for every class that respawns or
  resets, the inverse of PickupGone and K, plus the landed transform and enable of spawned drop pickups. Sweep every
  AADPickupBase and killable prop class.
- Added proof: receiver side, a respawned pickup and a revived crate are visible and alive on the client, and the
  crate's next K applies.
- Also in the sweep: c132p1, c140g, c140j and c155c client WARNs show BP_WeaponPickup_MoritaMk1_C_0 and
  BP_AmmoPickup_MoritaSmall/Large_C_0 pickup or mesh "host 1 client 0" on the test route. This is an older gap with
  the same class.

## Static trace (2026-11-08, before any run)

Cause, from the code and the playtest client log (no host log was kept):

- D1. Duplicate placed names. BP_AmmoPickup_MoritaSmall_C_0 and BP_WeaponPickup_MoritaMk1_C_0 exist in the range
  sublevel and the main level. Card 056 PickupGone and its journal key on the name only, and the client resolves it
  with c50_find_placed (first name match), so a take of one turns off the other on the client.
- D2. The host's own re-enable sends nothing. BP_AmmoPickup_NukesSmall_C_5 got PickupGone, then the host's respawn
  timer turned it back on through SetPickupEnabled, which has no relay. Card 056 only has the "gone" direction.
- D3. Relayed drop pickups (card 104 copies, such as the rack's grenade pickup 258557): the client's own overlap ran
  the game's DisablePickup on the copy (Pickup_Implementation from ASTItemPickup::Pickup_Implementation from
  OnBeginOverlap). The card 140 block only covers placed pickups, so the copy turned itself off and card 132 refused
  its Destroy. Its enable state on the host is never sent either.
- D4. The killable props that "revived" (finding 20) followed an all-down LoadLastSave. Card 143 now makes the
  client rejoin after a restore and the c98_load_actor reconcile drops the K rows, so this case is covered. There is
  no runtime revive path in the game for these classes; no row is added for it.

Every enable change goes through AADPickupBase::OnUpdatePickupStatus (exe 0x144C61870): DisablePickup and
SetPickupEnabled write the enabled byte (+0x2a8) and call it through the vtable, and every override
(ASTCustomPickupBase, ASTWeaponPickup, ASTTacticalSupportPickup) calls the base.

Fix (one manager for the pickup enable class):

- F1. Host detour of OnUpdatePickupStatus. After the original, it reads +0x2a8. A placed pickup gets an 'E' world
  row (name, location, value = enabled), journalled per actor and sent only when it differs from the last row (no
  row means enabled). A relayed pickup gets an 'N' row (value = host object index, name = "0" or "1"), sent on change,
  also sent by c104_send_spawn and replayed with the M rows, and unjournalled by c104_relay_destroy.
- F2. Card 056's name-only PickupGone send, journal and replay retire. The load reconcile becomes an E-row
  reconcile. The client applies E through c95_find_nearest (name plus location, so duplicate names resolve) and N
  through the copy's host id, by calling the game's SetPickupEnabled, which then runs the client's own
  OnUpdatePickupStatus for mesh and collision.
- F3. The client's DisablePickup block also covers relayed copies.
- F4. The rack drop's landed place: read in the run (rest rows and the location digest).

Predictions for run 1:

- No "host 1 client 0" pickup or mesh WARN for MoritaMk1_C_0, MoritaSmall/Large_C_0 or a respawned pickup.
- A host take of a placed pickup logs one E 0 row; its respawn logs E 1; the client logs both applied.
- A rack or crate drop logs P, then N 1; a take logs N 0 or D, and the client copy matches.
- The card 132 digest: 0 mismatches.

Run 1: `SCENARIO=tools/scenarios/c145-pickups.txt DEPLOY=1 FAST=1 GOD=0 HOST_EXTRA_ENV="UBWCOOP_AUTODROPSHIP=1" tools/run_pair.sh c145-r1 900`, DLL 61d9048a01924c2c.

Run 1 result (c145-r1, 154 s, rc 0 both, no crash, WARN 2 + 2 = baseline): card 145 lines all as predicted. Host weapon take
`E BP_WeaponPickup_MoritaMk1_C_0 6445 11469 101 0` applied on the client at the same spot; its respawn `E ... 1` 10 s later
applied. Join replay of the three pre-join `E BP_DevLogPickup_C_n ... 0` rows: "already". Crate kills: three P copies, then
`N ... 1` each ("already"); the client walk-over sent `N 0` for the two EPulse copies (applied, the copies at the drop spot).
Rack: four P copies with `N 1` ("already"). No "not placed and not relayed" line, no card145 WARN, card137 probe EEE on
both. Digest rounds 1-3: mismatches 0 (round 2: 1 slot differed, the MoritaSmall respawn in flight, entries matched).
The scenario failed at line 67: its regex wanted `card132: client t=N digest round K: ` but the result line is
`card132: client round K: actors ...`. A scenario bug, not a mod fault; the travel and the card 026 rtt did not run.
Run 2: same DLL, line 67 regex fixed.

Run 2 result (c145-r2, 155 s, drive rc 0, rc 0 both, no crash, WARN 2 + 2): card 145 rows as in run 1 (weapon take E 0/1
applied, rack N 1). Both crates died and the kill was relayed (`K` rows), but the game rolled no loot this time, so there are
no P rows; run 1 has the drop proof. Digest rounds: mismatches 0. The travel ran (host Browse to FED_C2M1), but line 69 had no
mark, so it matched the first map's minimap line, and the client quit while its Browse was still pending: no rtt after the
travel. Run 3: same DLL, client mark before the travel, minimap wait since-mark, then a pong wait since a second mark.

Run 3 result (c145-r3, 169 s, drive rc 0, rc 0 both, no crash): card 145 rows as before (weapon take E 0/1, rack N 1
"already"); no card145 WARN, no "not placed and not relayed" line. Crates died and relayed with no loot rolled again (P rows
only in run 1). Travel to FED_C2M1: the client's first pong after the load read 1525 ms (taken during the load), and the first
pong after the minimap origin read 36 ms, then 60 ms (card 026 batch item: Ping-to-Pong across a travel is fine).
Client WARN 7 against 2: a single digest round 2 with 4 location mismatches (STAISpawner_5, STSpawnPoint_7/_8,
BP_Dropship_LandingBlocker_C_0), each about (250, 200) uu off, all next to the departing AUTODROPSHIP dropship near
(115000, 86500). The host log shows the blocker moving with the dropship (card095 lines at changing locations). Rounds 3 and 4:
mismatches 0. This is the digest sampling an actor in flight, not card 145 code; it is passed to the senior as a lead, outside
this card's scope.

Outcome: fixes F1-F4 in. Placed-pickup state rides 'E' rows keyed by name and location (no duplicate-name collision), the
respawn re-enable is sent (`E ... 1` applied), and card 104 copies carry 'N' rows; the client's own overlap on a copy is blocked
and the host's take sends `N 0`. Card 056 removed. Run budget used: 3 of 3.

## Open follow-up (senior, 2026-11-09, from c157a)

Client WARN card132 mismatch BP_AmmoPickup_MoritaSmall_C_0 "pickup host 0 client 1" in digest round 2, after the
client walked over it. The host's disable after the client's take did not reach the client, or the client's own
re-enable overrode it. Trace statically. Batch the fix into the next build if the cause is clear, with no run of
its own.

### Follow-up trace and fix (junior, 2026-11-09)

Cause, from the c157a logs and the exe:
- Host 275867591: the client's take. Collision 1 -> 0 and `E ... 0` were sent, and the client applied both.
- About 30 s later the host's respawn ran `SetPickupEnabled(1)` (0x4C62470: write +0x2A8, then call virtual +0x768,
  OnUpdatePickupStatus). The original's SetActorEnableCollision ran the overlap synchronously. The client's character
  was still standing there, so TryPickup -> DisablePickup (0x4C61170: write 0, then +0x768) ran nested inside the
  same call.
- The host log shows only that nested call: collision 1 -> 0 at 275897601, caller exe+0x4C618A1. The outer call's
  collision relay saw 0 before and 0 after, so it logged nothing. c145_host_status ran only after the original and
  saw 0, which matched the journal's 0, so no `E` row went.
- The client's own respawn had re-enabled its copy (card140 blocked its own DisablePickup at 275897586). Only the
  collision row arrived, so the client stayed enabled 1 while the host was at 0: the round 2 mismatch. The second
  respawn at 275927602 had no player on the pickup and logged the normal 0 -> 1 and `E 1`.

Fix (weapons.cpp detour_c145_update_status): the host also calls c145_host_status at entry. Both callers write the
byte before the virtual call, so the outer call sends `E 1` and the nested call `E 0`. The check after the original
stays, and the journal dedups it. DLL d2c8795e51251837. No run of its own, per the senior. Prediction for the next
build's run: a respawn under a standing player logs `E ... 1` then `E ... 0` within the same ms, with no card132
pickup mismatch.
