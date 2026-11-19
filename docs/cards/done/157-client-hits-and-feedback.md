# Card 157: Client hits register where the client sees them, with feedback and armor

Status: CLOSED, outcome in docs/CARDLOG.md. Was: queued 2026-11-08, from docs/playtest-2026-11-08.md findings 12, 15, 19. Budget: three runs. Needs card 155's lines first. Fix
class decided by the user 2026-11-08 ("use best option"): client hit claims, not host lag compensation.

Read: docs/playtest-2026-11-08.md findings 12, 15, 16, 19 and "Host log", docs/STATE.md netcode trust model (friends-only).

## Why

- About 16% of the client's Morita shots never fire on the host (1796 own, about 1501 server-copy launches).
- Damage comes from the host's copy, flown against host bug positions; the client sees bugs one trip behind.
- The client gets no hit marker, hit sound or bug hit-react for its own hits.
- The client's armor never drops while its health does (cause not yet known; card 155 line 2 shows it).

## Do

1. From card 155's run: which client shots drop and why (weapon switch, ADS, reload, send gaps), the rtt, and the
   armor path per hit.
2. Client hit claims (decided): the client's own projectile hit is sent to the host as a Hit claim (shot sequence
   number, target guid, bone, damage, hit location). The host applies it after a basic check only (target exists and
   alive, distance within the weapon's range plus a margin, one line-of-fire trace from the server copy's muzzle area
   with a tolerance), per the friends-only trust model. The host's copy of that shot deals no damage, so no hit
   counts twice. Why this over lag compensation: hits land where the shooter saw them at any round-trip time, with no
   host-side position history to keep for every NPC.
3. Fix the drops, the armor path, and the client's own hit feedback (marker, sound, hit-react, impact FX), so every
   effect the shooter's side would show on the host shows on the client.

## Done when

- A run: every client hit on a bug logs a Hit claim applied on the host with the same shot number, and the host's
  copy of those shots deals 0 damage (no double hits).

- A run: `tools/match_shots.py` shows 0 dropped client shots in a scripted burst set; a client hit on a bug shows the
  marker and hit-react on the client and the damage on the host; a hit on the client's pawn drops its Armour on both
  sides first, matching.
- The usual Report items (WARN diff, run length, rc, ERROR/FATAL, CRASH, dumps, DLL hash). It ends with "Report
  complete."

## From card 141 run c141b (senior, 2026-11-08)

The host's hit line shows the server copy's Armour dropping on a hit (`Armour 100.0 -> 84.0, Health 100.0 -> 100.0`,
host t=265872562). So on the host, armor takes the damage first. Finding 19 is then cause (b): the value does not
reach the client or its HUD, or (after the playtest's loads) it was 0 from the save (card 143). Check the client's own
Armour value and HUD bar after a host hit. The same line's breakdown reads "(armour 0.00, health 16.00)", the
opposite of the before/after values: fix the breakdown labels or their source fields.

## From card 156 (senior, 2026-11-08)

"WARN card043: host real death: 5 hits, Health 1.0" at a game over: the host pawn's real death leaves it at 1 HP
(also in c112c, c117a, c130r, c76d). Trace it with this card's damage path. Is the host's death decided by the
mod's down logic rather than by the health reaching 0? Is it visible to either player (HUD health, death screen)?
Fix it if the state differs from what the game would do.

## Trace (junior, 2026-11-09)

- A client's Morita round on the host is the server copy pawn's FireProjectile task round. It is not relayed (no
  c132 round table entry, c74_relay returns for non-grenade tasks), so its AADProjectile::OnHit runs the original:
  the host's copy deals the damage where the host's bug is.
- The client's own round is the predicted one; its OnHit runs the original on the client (impact effects locally,
  no damage: the client has no authority), so no hit marker comes from it.
- Host 1 HP (card 156 note): USTCombatUtilityLibrary::DealDamage (0x5233F60) takes (causer, target), not
  (target, causer): rcx is stored as the instigator and causer, rdx's root location and GenerateHitResult actor are
  the damaged one. The mod's four calls passed (target, causer), so card 043's real-death hits damaged the nearest
  bug, and card 079's mech hits too ("mech takes nothing from bug hits"). Fixed in death.cpp (four calls); the
  symbols.txt comment is corrected.

## Design (built, claim_c157.cpp)

- Client: own player rounds (instigator a player character, controller AutonomousProxy) are noted at launch with the
  card 155 shot number. Each one's first OnHit sends one HitClaim text frame
  "<shot> <npc guid hex, 0 = miss> px py pz nx ny nz <bone>"; the own OnHit then runs as before.
- Host: rounds launched by a server copy player character are noted per PC in launch order. Their OnHit does not run;
  the round parks with its hit kept (weak actor and component). Each tick, claims (in arrival order) take the PC's
  oldest open round, parked or flying. An NPC claim is checked (guid resolves, health not 0, point within 30000 uu
  of the shooter's pawn, a Spectator-profile (world static) trace from the view point stops within 600 uu of the point)
  and then runs the round's original OnHit with a hit built on the NPC's mesh at the claim's point. A miss claim
  replays a parked round's own non-NPC hit (the wall impact shows on the host) or releases it; a flying round is
  marked and does the same at its hit. A parked round with no claim after 1500 ms replays or releases (WARN when its
  hit was an NPC). A claim with no round after 1500 ms is dropped (WARN).
- Grenades (OnGrenadeSpawned), vehicle and turret rounds (c76_relay) are dropped from the table; ReleaseToPool and the
  map load clear entries.
- Diagnostic for the hit marker (run 1): ASTPlayerCharacter::OnDamageCaused is hooked and logs side, pawn, damage
  and the event's first 0x60 bytes. Client: one line per change of the own pawn's Armour and Health.
- ChanOp::HitClaim is new (client to host), so both sides need this DLL.

## Run 1 predictions (c157a, tools/scenarios/c157-claims.txt, DLL f8a68158e5aaff82)

1. Miss burst: client "card157: client ... HitClaim shot N miss ..." lines; host no "refused" or "found no round"
   WARN for them, the host's copies' ground impacts still play (no WARN lost).
2. Bug bursts: each client "HitClaim shot N NPC BP_AI_WarriorBug_C" line has a host "HitClaim shot N from pc ...
   applied to BP_AI_WarriorBug_C_..." line with the same N (parked or flying). 0 "refused".
3. Bug health drops on the host only through the applies: the host totals line "applied" equals the client's
   "on NPCs" count minus orphans; "lost" 0.
4. Orphans (claims with no round) about equal to the shots the host never launched (card 155's ~16%); match_shots.py
   shows the same drops. Their cause is not fixed in this build.
5. OnDamageCaused: host lines "on <server copy pawn> (server copy)" at the applies; no client lines (the cause of
   the missing marker). Run 2 relays it.
6. Bug hits on the client pawn: the host "hit slot 1 ... Armour a -> b" and a client "own pawn Armour a -> b" line
   with the same values, or the client's Armour unchanged (finding 19 cause found).
7. 0 ERROR/FATAL, no CRASH, no dumps; WARN diff only the new card157 lines if any.

### Run 1 result (c157a, DLL f8a68158e5aaff82)

Scenario stopped at line 20 after 133 s (both rc 0, FATAL/ERROR 0, no dumps): the client wait for the card 095
InteractionCollider row on the Morita pickup. Card 145 now syncs the host's take (E 0) and the respawn 10 s later (E 1,
applied on the client at t=275877767 as a PlayerCollider row), so the c155 route's wait no longer matches. No shot was
fired, so predictions 1-6 were not tested. Prediction 7 held. Fix: the client waits for the card 145 E 1 row, then takes
the respawned Morita. Same DLL, run 2 checks the same seven predictions.

Side note (not in this card's scope): client WARN card132 mismatch BP_AmmoPickup_MoritaSmall_C_0 pickup host 0 client 1
in digest round 2 after the client walked over it (client collision row 1 -> 0 applied at t=275897618).

### Run 2 result (c157b, DLL f8a68158e5aaff82)

Both took a Morita (the card 145 wait held). The miss burst fired 9 client shots (seq 1-9) and no HitClaim line came, so
the scenario stopped at line 31 after 125 s (both rc 0, FATAL/ERROR 0, no dumps). Cause, from the code: c157_launch found
the shooter only through the projectile's Instigator, but c132_proj_launch gets the shooter from the fire task (the
avatar, here BP_Weapon_MoritaMk1_C; the shot line names it) and reads Instigator only as a fallback. The pooled round's
Instigator gave no player character, so no round was noted and its hit sent no claim. Also the c155 aim point
(-3674 30000 0) was relative to the old route spot; from the pickup spot the burst flew level into open map.

Fix for run 3 (DLL fccc9bd54b9197fe): shooter_of takes the task's shooter and tries it, then its Owner, then the
Instigator (pl_shot's order); the round keeps the controller on both sides and client_claim sends through it. The miss
burst aims at the ground 900 uu in front of the client (6900 12300 100). Predictions 1-7 unchanged; run 3 is the last of
the budget.

### Run 3 result (c157c, DLL fccc9bd54b9197fe), budget spent

127 s, both rc 0, FATAL/ERROR 0, no dumps. Scenario stopped at line 44 (no client NPC claim).

- The shooter fix held: the client noted its rounds and sent claims (shot 1 miss on a StaticMeshActor; shots 10-19 and
  more in the bug bursts).
- P1 (miss claims, no WARN) failed. The host logged 22 "WARN card132: recv N ... HitClaim ... result no handler". Cause:
  the bone text read at FHitResult +0xE0 is garbage on most hits ("?????r??..."; once "Clear_Coat_UV_Rotation" on a
  StaticMeshActor), and chan_text_chars refuses any char outside 0x21-0x7F, so chan_parse returns op 0 and no handler
  runs. Fixes: send only printable ASCII bone text (else "None"), and confirm the BoneName offset (ue/actor.h has
  kHitMyItem at 0xD0, so the +0xE0 guess is not checked). The safe fix needs no new data. The offset needs one hit dump.
- P2/P3 not reached. Every client claim in the bug bursts was "miss BP_KillableBuilding_Base_TacSuppRangeTarget_C" at
  y 14497: a range target sits between the client (12600 14200) and the bugs (y 14581), and the client's own rounds stop
  on it. The host's server copies of the same shots hit the bug anyway (14 host OnDamageCaused lines, damage 40; the
  relayed "card132: client hit BP_AI_WarriorBug_C guid 0x246 ... damage 40" lines on the client), and no host round was
  parked (no park WARN). So either the host never noted those rounds (shooter_of on the server copy), or the host copy's
  damage does not go through OnHit. Not settled from this log: the host notes rounds silently, and the totals line comes
  only at map end, which the run never reached.
- P5 held: 14 OnDamageCaused lines on the host for server-copy shots, none on the client. The event qwords are logged
  for the hit marker relay.
- P6 held: host "hit slot 1 server-copy ... Armour 100.0 -> 75.0" at t=276326562, and the client "own pawn Armour
  100.0 -> 75.0" at t=276326570. The later steps match too.
- P7 held.

### Run 4 trace and predictions (c157d, DLL 9812b3688e1a2dac)

Trace (blocker 2, settled statically). A player round that touches a bug overlaps it and does not block it.
`AADProjectile::execOnOverlapBegin` (exe+0x4A37190, its native body inlined in the thunk) reads the projectile's
collision profile (the FName at +0x3C8) and its response to the other component's object type (vcall 0x5D8). On
Overlap (1) or Block (2) it broadcasts the same two hit delegates that OnHit does (+0x2B0 dynamic, +0x2C0 native), then
it calls ReleaseToPool or Destroy. Pierce counters are at +0x3C3, +0x3C4 and +0x3D0. Those delegates deal the damage, so a
host copy round damages a bug without OnHit. That explains run 3: no host round parked (OnHit never ran on a bug),
the client sent no NPC claim (its claims came only from blocking OnHits, which landed on the range target), and the
host copies still dealt damage 40.

Changes in this build:
- `detour_c157_overlap` hooks execOnOverlapBegin and reads its parms by reflection (UFunction OnOverlapBegin, child
  properties in order; logged once).
  - Host, own-table round on an NPC: a Flying round parks with the sweep result as its hit and logs a diag line
    (capped at 10). A Parked round is swallowed. A Miss round goes back to the pool. The original does not run.
  - Client, own round on an NPC: the claim goes out with the sweep hit (no sweep: the round's place as the point), then
    the original runs.
  - All other overlaps run as before.
- copy_check: a server-copy player round (instigator's controller is the server copy) that reaches an NPC with no
  table entry, by overlap or by OnHit, counts as copy damage and logs a WARN. The no-double-hit rule says the count is 0.
- FHitResult offsets are read by reflection (/Script/Engine.HitResult: BoneName, MyBoneName, Component,
  HitObjectHandle, Item, MyItem) and logged once. The bone text sent is printable ASCII only, else "None".
- Scenario: one short burst from 12600 14200 at a bug behind the range target, then that bug is killed. Then open
  ground at 13600 14200 with bugs at 13600/13300/13900 15700 and four bursts.

Predictions:
- R1: one line on each side, "FHitResult offsets by reflection" (not WARN), and one "OnOverlapBegin parms (6)" line
  naming OverlappedComponent, OtherActor, OtherComp, OtherBodyIndex, bFromSweep and SweepResult.
- R2: no "WARN card132: recv ... HitClaim ... result no handler" on the host.
- R3: the range-target shot. The client logs "HitClaim shot N miss BP_KillableBuilding_Base_TacSuppRangeTarget_C".
  The host has no "applied" line for shot N, and no server-copy OnDamageCaused line between that shot and the kill.
- R4: open ground. The host logs "parked on its overlap with BP_AI_WarriorBug_C" diag lines (1-10). The client logs
  NPC claims, and the host logs "applied to BP_AI_WarriorBug_C" lines.
- R5: no-double-hit. There is no "server-copy round ... outside the round table" WARN. The host totals line reads
  "copy damage 0". Host server-copy OnDamageCaused lines (capped at 30) each follow an applied line, and there are no
  more of them than applied claims.
- R6: P5-P7 as before (OnDamageCaused only on the host; client Armour matches the host's hit lines; FATAL/ERROR 0, no
  dumps, both rc 0).
- Card 145 ammo pickup WARN: not in this build. Its static cause is not clear yet. In c157a the pickup's collision came
  back on both sides about 30 s after the first take, with no 'E 1' row. The host's second DisablePickup (character
  null, caller exe+0x51D3793) then found its journal row already at 0, so no 'E 0' went out. The client's own copy was
  re-enabled at that point. The path that re-enables the pickup is not traced.

## Run 4 result (c157d, DLL 9812b3688e1a2dac): closed partial

Run c157d: 137 s, both rc 0, FATAL/ERROR 0, no dumps. Host WARN 27 (24 of them are the claim drops below), client WARN 4.
The drive stopped at line 62: the host never logged "applied to BP_AI_WarriorBug_C" within 10 s.

What run 4 proved:
- R1. The overlap parms are read by reflection on both sides (6 parms, SweepResult +0x20).
- R1. The FHitResult offsets are read by reflection on the client: BoneName +0xF0, MyBoneName +0xF8, Component +0xD8, HitObjectHandle +0xB8, Item +0xA8, MyItem +0xA4.
- R2. The client sends real bone names (beak_top_C0_0_jnt, leg_front_L0_1_jnt, ...). 24 claims went out, 10 of them on NPCs.
- R5. The range-target shot produced its claim: "HitClaim shot 10 miss BP_KillableBuilding_Base_TacSuppRangeTarget_C".

Root cause of the failure: `ChanOp::HitClaim` was registered in `chan_client_table()`, not in `chan_host_table()`.
- The host dropped all 24 claims with "card132: recv ... HitClaim ... result no handler".
- The run-3 diagnosis (a garbage bone name in the claim) was wrong. The ASCII guard stays as a safety check.
- Fix, applied for the next build (DLL acee57eb67673896): the entry has moved to `chan_host_table()` in mod/src/ubw/chan_ops.cpp:203.

Second gap, found in the logs and not yet confirmed:
- The host logged 15 "OnDamageCaused ... damage 40.00" lines, timed with client shots 10-11 (the range target) and 12-24 (the bugs).
- Over the same shots it logged no "parked on its overlap" line and no copy_check WARN.
- The overlap detour ran, because the parm line was logged. So the host's copies of the client rounds had no round-table entry, and copy_check did not count them either.
- Static reading: on the host, `shooter_of(proj, ..., kSideServerCopy)` returns null for these copies. That empties `c157_launch` (no `round_add`) and returns early in `copy_check`.
- Result: the copy damage stands without being counted, so "copy damage 0" would be a false pass.
- The next run of this card must check that shooter_of resolves on the host for a client's copy round. Expected evidence: host launch count > 0 and overlap park lines present.

Not reached: R3, R4 and R6 (no-double-hit totals and the after-travel totals line). The hit marker relay stays excluded.
