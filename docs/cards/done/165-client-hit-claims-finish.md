# Card 165: Client hit claims apply once on the host (card 157 remainder)

Status: CLOSED, outcome in docs/CARDLOG.md. Was: queued 2026-11-09, from card 157 run 4 (c157d), closed partial. Budget: three runs. Before the next play
build. Gameplay state.

Read: card 157 (all of it, mainly "Run 4 result (c157d) — closed partial"), mod/src/ubw/claim_c157.cpp,
mod/src/ubw/proj_c132.cpp.

## Why

Card 157 built the client hit claims (ChanOp::HitClaim, claim_c157.cpp). In c157d the host dropped all 24 claims
("result no handler") because HitClaim was in chan_client_table(), not chan_host_table(); that is fixed in DLL
acee57eb67673896 and not run. A second gap: the host logged 15 "OnDamageCaused ... 40.00" for the client's shots
and 0 overlap-park lines and 0 copy_check WARNs. The likely reading is that shooter_of(proj, ..., kSideServerCopy)
returns null on the host for a client's server-copy round, so c157_launch never notes the round, copy_check returns
early, and the copy's damage stands uncounted.

Play-build safety: with the handler registered and the copy round still dealing damage, an applied claim means a
double hit. Until this card passes, claim apply on the host is off by default in any build that may become a play
build.

## Static finding (senior, 2026-11-09) — wrong, see the junior's correction

Correction: `p.copying` is set only by c132_proj_copying, on the client around c74_receive's spawn. The host's
server-copy launches are not copies, so proj_c132.cpp:522 does call c157_launch for them. The paragraph below is
kept as written and is not the cause.


mod/src/ubw/proj_c132.cpp:522 calls `c157_launch` only `if (!p.copying)`. On the host, a client's round is the
server copy's launch, and card 074's spawn path marks it as a copy (`p.copying` true). So the host never notes a
client round in the round table, whatever shooter_of would return. sprint_side (hooks_ubw.cpp:324) returns
kSideServerCopy for an Authority actor with an AutonomousProxy remote role, so for the server copy's controller the
side test itself is right; the call site is the gap. copy_check (claim_c157.cpp) then reads only the projectile's
Instigator at hit (shooter nullptr), which may be null on pooled rounds; that matches the 0 copy_check WARNs.

## Do

1. Static first, with a written prediction (start from the static finding above): for a client's server-copy round on the host, what the fire task's
   avatar, its Owner and the projectile's Instigator are at launch and at hit (pooled rounds), and what sprint_side
   returns for the server copy's controller. Check whether c132_proj_launch calls c157_launch for the server copy's
   launches at all (own or copy classification on the host). Fix shooter_of or the call site.
2. copy_check must not rely on a field that is null on pooled rounds; use the round table or the launch-time
   shooter.
3. The run must first show a host launch count above 0 and overlap or OnHit park lines, then the claims applied,
   then the totals.

## Done when

- A run: client bursts at bugs on open ground plus the one range-target shot. Host "applied to BP_AI_WarriorBug_C"
  for the claimed hits, the copy damage count 0 (and the counter shown to be live, by a launch count > 0 and park
  lines), no bug damaged by both a claim and the copy round of the same shot, and the range-target shot resolved as
  the client saw it.
- Hit marker and hit-react on the client for a claimed hit (card 157 item 3), or written as open.
- The usual Report items (WARN diff, run length, rc, ERROR/FATAL, CRASH, dumps, DLL hash). It ends with "Report
  complete."

## Static check of the call site (junior, 2026-11-09, during card 159's run)

- `p.copying` is set only by `c132_proj_copying`, called only at mod/src/ubw/grenades.cpp:760-763, the client's
  card 074 GrenadeSpawn copy (its log line reads "card074: client t=..."). The host never sets it. So on the host a
  client's server-copy launch has `p.copying` false, and proj_c132.cpp:522 does call `c157_launch` for it.
- c157d host log agrees: the client's rounds show as playlog "shot s ... w=BP_Weapon_MoritaMk1_C" lines, which come
  from pl_shot inside c132_proj_launch (pl_slot found the client's server-copy pawn as the shooter or its Owner).
  pl_shot returned true, so no "launch #" line printed for them, and c157_launch ran right before that return check.
- So the gap is inside `c157_launch`: `shooter_of(proj, shooter, kSideServerCopy)` returned null, or round_add ran
  and the hit path missed the entry. No host "card157: ... totals" line exists in c157d (no map end before quit),
  so the launch count is not known. The first run of this card needs a host line at c157_launch that prints the
  three candidates, the pawn's controller and sprint_side(controller), capped.
- "OnDamageCaused on None (other)": the damage event's self names None, so that hook's self is not the round or
  the pawn; check what the 15 lines are before reading them as copy damage.

## Batched from card 159 (senior, 2026-11-09)

This card's first run build also carries card 159's remainder (NavDirection in the dropship frame; see card 159,
"Review after run c159c"). Its proof is a rejoin while C_3 flies its second leg. It does not count against this
card's three runs unless it forces a rebuild.

## Static trace: why the noted rounds found no claim (junior, 2026-11-09)

- c159c host: all 8 launch lines say "noted", yet all 9 miss claims "found no round in 1500 ms". resolve() matches by
  pc alone (oldest_open(c.pc)), so the round was gone or its pc differed.
- Cause found in the call order on the host, per player round:
  1. detour_fire_projectile (attributes.cpp:919) calls c76_mark_task. Since card 132 step 2b, a player shooter's
     task is marked too (`g_c76_mech.task = task`, vehicle = the pawn).
  2. The task's Activate spawns the round. detour_c74_spawn calls c132_proj_launch, then c157_launch, then
     round_add: the round is noted.
  3. After Activate returns, detour_c74_activate calls c74_relay(task). The task is g_c76_mech.task and not a grenade,
     so it goes to c76_relay, whose first act is `c157_forget(proj)` ("a vehicle's or turret's round takes this relay").
     That erases the round noted in step 2, every time, within the same call.
- Fix (grenades.cpp c76_relay): forget only when the marked shooter is not a player character
  (`!is_a(g_c76_mech.vehicle, g_player_mi_class())`). Mech and turret rounds are still forgotten; the card 076 copy relay
  is unchanged.
- The pointers: the claim's pc comes from the channel (chan_pc_s, chan_ops.cpp:203) and the round's from
  read_ptr(server-copy pawn, kPawnController). Both name BP_PlayerController_C; this build logs both pointers to show
  they are the same object.
- Diagnostics in this build (capped at kLaunchLines, all counted): the launch line prints the controller pointer;
  each host claim prints its pc pointer, the rounds noted, how many of them belong to this pc, and one other pc;
  every host c157_forget of a noted round prints who forgot it (the card 076 relay, the card 074 relay,
  OnGrenadeSpawned or ReleaseToPool), its state and its age.

## Run 1 plan and predictions (DLL 2b4e8fa114641922; batched with card 159's remainder)

Scenario tools/scenarios/c159-dropship-join.txt as in c159c: the client takes a Morita and fires one 800 ms burst at
(6900, 12300, 100), with no bug there, so these are miss claims.
- P1: no host "card165: ... forgotten ... by the card 076 relay" line for a player round.
- P2: each host "card165: host ... HitClaim shot N from pc X <ptr>" line shows at least 1 round of this pc, and the
  pc pointer equals the launch line's controller pointer.
- P3: no "WARN card157: host ... found no round" line. The claims resolve: a miss on a round still Flying marks it
  Miss (its later hit replays or goes back to the pool); on a Parked round it replays or releases.
- P4: ReleaseToPool forget lines may come only after the claim for that round (state 2, Miss), or for rounds no claim
  named. A ReleaseToPool forget with state 0 (Flying) before the claim would be the next gap.
- P5: no NPC claims in this burst; NPC hits (applied lines, copy damage 0) are the next run's goal unless a bug
  crosses the line of fire.

## Run 1 result (c165a, DLL 2b4e8fa114641922; confirmed again in c165c, DLL b910af8313d99960)

- P1 holds: no "card 076 relay" forget of a player round. No forget line at all was logged, because no noted round
  was forgotten before its claim.
- P2 holds: all 9 claims came from pc 000000005D8F9760. That is the same pointer the launch lines log as the
  controller ("controller BP_PlayerController_C 000000005D8F9760 side 1 (server copy 1): noted"). Each claim had 1
  or 2 rounds of this pc and no other pc.
- P3 holds: no "found no round" WARN. The client sent 9 claims ("HitClaim shot 1..9 miss ... sent"). The host
  ledger shows all 9 as "HitClaim ... result handled", and the host logged 9 "card165: host ... HitClaim" lines.
- P4: no ReleaseToPool forget was logged. Each round was resolved, and so removed, before its pool release, so
  c157_forget never found it.
- P5 holds: 0 claims on NPCs.
- c165a: 104 s, rc 0/0, no ERROR/FATAL, no dumps. c165c: 95 s, rc 0/0, no ERROR/FATAL, no dumps. The host WARNs are
  only the usual attach and config lines. The client WARNs in c165a were the card 159 frame regression (see card
  159). In c165c the client WARNs are only the card 132 STAISpawner_5 / STSpawnPoint_7/_8 location mismatch, which
  also shows in c159c, c143-r3, c140m and c145-r3, so it is not new.
- Card 165 passes. The play-build hold on e9e40f75 and later DLLs can lift from b910af8313d99960.

## Review after c165a-c165c (senior, 2026-11-09)

Not passed yet. The claim matching is fixed and proven (c76_relay forgot player rounds; 9 of 9 claims handled, same
pc pointer, 0 "found no round"). The Done-when's core is not proven: the run had 0 NPC claims, so there is no
"applied to BP_AI_WarriorBug_C" line, no live copy-damage count of 0, and no proof against a double hit. The
play-build hold stays on b910af8313d99960 and later DLLs until that run passes. Runs used: one (c165a); two left.

Next run: client bursts into warrior bugs on open ground plus the one range-target shot, on the current build if no
code changes. Card 159's remainder is accepted (c165c, P10-P12) and is not rerun.

## Run 2 plan and predictions (DLL e1e4d6c1a8a0450c, scenario tools/scenarios/c157-claims.txt)

- Senior review: the c165a/c165c claims were all misses, so the Done-when core (NPC applies, copy damage 0, no
  double hit) is unproven.
- Build changes, both logging only:
  - The host "applied to" line now prints the bug's Health before and after the claim's OnHit (c23_set_health).
  - The per-claim line cap kLines goes from 40 to 120, so every claim of the bug stage prints.
- Scenario: c157-claims.txt as in c157d. It runs the c155 route to the Morita, the miss burst, the range-target
  shot at a bug behind the target, then four bursts at three bugs on open ground. A travel then prints the totals.
- P1: there is a "card165: host ... launch ... noted" line for the client's rounds, and the host totals show rounds
  noted > 0. This shows the counter is live.
- P2: each client "HitClaim shot N NPC BP_AI_WarriorBug_C" has a host "HitClaim shot N ... applied to
  BP_AI_WarriorBug_C" line with the same N. Each one has a Health drop equal to one shot's damage. 0 refused, 0
  found no round.
- P3: the host has overlap park lines ("parked on its overlap with BP_AI_WarriorBug_C") for the copy rounds, and 0
  "server-copy round ... reached NPC ... outside the round table" WARNs. The host totals end with "copy damage 0".
- P4, no double hit: per bug, the host Health falls only at applied lines, one drop per claim. The client's card037
  "notify BP_AI_WarriorBug_C Health a -> b" lines follow the same steps.
- P5, range target: the client sends "HitClaim shot N miss BP_KillableBuilding_Base_TacSuppRangeTarget_C". The host
  has no applied line for that shot, and the bug there keeps full Health on both sides.
- P6, feedback: client card157 "OnDamageCaused" lines (hit marker path) and card041 "client hit ... OnDamage ran"
  lines (hit-react) for the claimed hits. If either is missing, it is written as open.

## Run 2 result (c165d, DLL e1e4d6c1a8a0450c): partial, the drive stopped at the range-target step

- Run: 128 s, rc 0/0, no ERROR/FATAL, no dumps. The drive stopped (rc 1) at line 43, where it waits for the
  range-target miss claim. Logged cause: the drive slept 2 s after "host spawn bug 12600 15700". In that time the bug
  crossed about 1450 uu to the client at 12600 14200, and the client's 100 ms burst hit the bug at (12555, 14245)
  instead of the target. That is a scenario fault, not a mod fault. The open-ground bursts after it never ran.
- P1 holds: "card165: host ... launch ... noted" lines for both shots.
- P2 holds for the two NPC claims that ran:
  - Shot 10: "applied to BP_AI_WarriorBug_C ... (parked, waited 6 ms; distance 105) Health 376.0 -> 336.0".
  - Shot 11: "applied ... Health 336.0 -> 296.0".
  - 0 refused, 0 "found no round".
- P3: 0 "outside the round table" WARNs. The totals line did not print, because the drive stopped before the travel.
- P4 holds: no double hit. The client's card037 notify lines show the same steps, 376 -> 336 -> 296. There is one
  40.0 drop per claim, and no second 40 drop for either shot. The later 8.0 drops (296 -> 288 and so on) come from
  the NPC trooper's Morita: the client "hit ... damage 8.0 bone None" lines, at the gore points the host logs with no
  claim.
- P5: not shown. The bug took the shot (see above).
- P6, hit-react holds: the client logs "card132: client hit BP_AI_WarriorBug_C ... damage 40.0 ... OnDamage ran" for
  shots 10 and 11. Hit marker is open: ASTPlayerCharacter_OnDamageCaused is hooked on the client but logged 0 calls.
  On the host it fires for the claim ("OnDamageCaused on None (other) damage 40.00"), so the marker event happens only
  on the host's server copy and is not relayed to the shooter.
- WARN diff:
  - Host: only the usual attach and config lines.
  - Client: the card 132 STAISpawner_5 / STSpawnPoint_7/_8 location mismatch, round 2 BP_Dropship_LandingBlocker_C_0
    location, and BP_AmmoPickup_MoritaSmall_C_0 "pickup host 0 client 1".
  - All of them also show in earlier runs (c140g/m, c143-r2/r3, c145-r3, c155d, c156-r1), so none is new. The ammo
    pickup line is the card 145 ammo WARN that is still open.
- Scenario fix, ready for run 3 (no rebuild): the client aims at the range target point (12665 14500) first, then
  the host spawns the bug and the client fires at once, with no sleep in between.

## Run 3 result (c165e, DLL e1e4d6c1a8a0450c)

Drive rc 0, scenario passed in 130.7 s, travel ran. Host 7057 lines, client 6912; FATAL/ERROR 0 both; no new dumps.

- P1 range target: client shots 10 and 11 are "miss BP_KillableBuilding_Base_TacSuppRangeTarget_C" at
  (12663, 14497, 51) and (12660, 14497, 12). The bug behind it spawned on the host at t=280422558 (health 400.0).
  Between the shots and the scenario's kill (client notify 400.0 -> 0.0 at t=280425134) neither side logs a health
  change for it. The host records both as misses. Passes.
- P2 open-ground NPC claims: the client sent 16 NPC claims (shots 12, 15-18, 20, 21, 25-33). The host has one
  applied line for each, with the same shot number, and each matches a client card037 notify:
  - 12: 360.0 -> 300.0 (beak, weak spot); 15: 300.0 -> 260.0; 16: 400.0 -> 360.0; 17: 360.0 -> 320.0;
    18: 320.0 -> 280.0; 20: 280.0 -> 240.0; 21: 240.0 -> 200.0.
  - 25-33 hit one bug, 28.0 -> -12.0 and then 40.0 steps down to -332.0. Shot 25 killed it and shots 26-33 hit
    the corpse. The corpse keeps taking damage the same way on both sides, which matches the explosion overkill
    values (-1463, -3963) that come from the other damage paths.
  - Each applied line shows one Health drop, so there is no double hit. The other client notifies are 8.0 / 9.0
    steps from the NPC troopers' Moritas.
- P3: 0 refused, 0 found no round, 0 "outside the round table" on both sides. (The 672 "refused" grep hits are
  card132 ledger map-end lines, "refused 0".)
- P4 totals:
  - host/standalone: 60 rounds noted, 16 applied, 0 refused, 41 misses, 59 parked, 0 lost, 0 orphan claims,
    0 table full, copy damage 0.
  - client: 60 rounds noted, 57 claims sent (16 on NPCs), 0 lost, copy damage 0.
  - 12 card165 host launch lines (capped). 57 = 16 NPC + 41 miss, the same count on both sides.
- P5: there are 40 client card132 hit-react lines. The hit marker is still open; it is card 166.
- Host shot 10 logged "0 rounds noted": the claim came in before the host's copy of the round was noted. It was a
  miss, so nothing needed to be applied.
- WARN diff against c165d:
  - Already seen before: STAISpawner_5, STSpawnPoint_7/_8, the MoritaSmall ammo pickup (card 145), and the attach /
    GOD config lines.
  - New 1 (host): two lines of "card132: host hit BP_AI_WarriorBug_C guid 0x268: impact (13726, 15845, 198) is
    1239 uu from the NPC at (13714, 14607, 199); damage 8.0" (and 1434 uu). Damage 8.0 means NPC trooper rounds,
    not claims. The impact point is near where the bug spawned (y 15700), while the bug is already at y 14607.
    Not carded here.
  - New 2 (host): 4 lines of "OnHit of ADProjectile, whose GrenadeExplode already went" (grenades.cpp:1505), at
    t=280436464-807 next to NPC trooper Attack01. The same WARN is in c157d-host.log, so this class was seen
    before; it is not from claims.
  - New 3 (client, card 159, after travel): the BP_ProceduralDropshipTroops_C_20 check is 8386 uu off (tolerance
    655), and BP_PlayerTransportDropship_C_2 is 9151 uu off. The live frame before it had rtt 2056 ms (load
    hitch), and its lead (speed -8348) left C_20's spline distance at 274992 against the host's 283420. The
    totals read "join check 23 of 25 match". This is new, but it happens only right after travel.
- Card 165 Done-when is met by c165d (shots 10 and 11) together with c165e.

## Accepted (senior, 2026-11-09)

Accepted on c165d (shots 10, 11) and c165e (range target, 16 NPC claims, totals: copy damage 0 on both sides, 0
orphan, 0 lost). The play-build hold lifts from DLL e1e4d6c1a8a0450c on. The hit marker is card 166. The new WARNs of
c165e are routed: the dropship post-travel lead to card 159's "Reopened after c165e", the two host WARN classes to
card 167.
