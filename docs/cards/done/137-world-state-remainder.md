# Card 137: World state remainder from card 132 (non-effects)

Status: closed 2026-11-08 by a user split after run 4 (hb72, test DLL 321e8298ed49abf8); partial. Proved: barrel K
replay with no OnDeath and 0 barrel FX (hb68, hb72 rejoin), acid zone damages the client's server copy (hb68 P5),
nuke grant ammo and shot 1 pairing (hb72), frozen-corpse pose rows and unfreeze 877/877 (hb72), the LoadFromSlot
journal reconcile on the possession save (hb72), dead NPCs posed at the first join (hb72). Moved: corpses at rejoin
to 146; from-save proof, digest after rejoin and nuke shot 2 to 147; gib sync, GibPart budget and splat doubling to
148; 2a change log tool to 149; rejoin replay reliable overflow to 142.

Was: queued 2026-11-08, after card 136. Holds the card 132 steps that are not visible effects, so card 132 can close
on the fixes already built. The step text stays in card 132; this card points to it and does not restate it.

Budget: 3 runs used (hb68 gib overflow; hb70 and hb71 failed in the scenario before the save). The user approved run 4
(2026-11-08) with a fail-soft scenario: a failed wait logs and the run continues. If run 4 fails in mod code, re-scope
with the user.

Read: card 132 (steps 1, 2, 2a, 2c, 2d, 3, 4 and its Traces section), memory rules full-world-sync,
investigate-before-carding.

## Do

1. Trace first (card 132 Traces c): what a reconnect does (map reload or not), what `c98_replay` applies, and what
   OnDeath runs on a barrel and a bug.
2. **Replay sets end state.** On a late join or a reconnect with a map reload, the replay sets the destroyed or dead
   state directly, with no OnDeath broadcast: no impulses, debris, decals, cues or sounds. On a reconnect without a
   reload, rows already applied are skipped by seq.
3. Card 132 step 2c and the persistent actor class (2d): the client takes persistent actor state from the host,
   never from its own save.
4. Card 132 steps 1, 2 and 2a: the class inventory, the digest diff and the client change log, as written there, for
   whatever is not yet built (state which are).
5. Nuke shot 2 (hb58): card 132 Trace 2 shows the mod causes it. The test grant (`c21_grant_to`, through
   `GiveWeaponByClass`) skips the ammunition step that `ASTWeaponPickup::PickupWeapon` (0x051D2FD0) runs. Fix: after a
   successful grant, give the definition's starting ammunition the same way (the magazine for a magazine type,
   otherwise the reserve). Test variable only; the play build is not affected.
6. From card 136 step 0: gib physics positions, and the archer's `BP_HazardousZone_Acid` (a deferred spawn from its
   death handler, SetLifeSpan 5.0). The host's zone must damage the client's pawn, and its effects reach the client
   through card 136.
   Also from card 136 hb67 (user decision 2026-11-08): gore part splats (BloodGib Green/Red, Splats_MS, owner
   STGorePart*) play twice on the client, the host's rows at the host's gib positions plus the client's own gibs'
   splats (1391 host rows and 408 client-own in hb67). The host is the only source: the client blocks its own gore
   part effects, and the synced gib positions make the host's splats land where the client sees the parts. One run
   proves both: 0 client splats within 40 ms and 100 uu of another from one host event, and client gib parts within a
   written bound of the host's.
7. Card 132 step 4: the soak, ending at 0 digest mismatches.

## Done when

As card 132 "Done when", plus the replay proof: after a scripted reconnect, destroyed barrels stay destroyed with no
explosion, sound or debris on the client. Ends with "Report complete."

## Step 3: journals after a save load

Trace (point 1): before this step, nothing told a late client about kills the save holds. K rows come only from
detour_health_changed -> c98_host_kill (Health from above 0 to 0 or below, on a placed, unreplicated actor, on the
listen server). A load sets a building dead through SaveFullyLoaded with no HealthAttributeChanged and no OnDeath, so no
row is written. The frontend Continue route arms the journals at LoadMap's return, then runs LoadFromSlot, so those
kills were lost. The reconcile now adds them. The client's K replay already puts a dead building in place the way a load
does (c98_load_dead).

Rules, applied by `detour_c137_load_slot` (save_c121.cpp) to every actor in every level at LoadFromSlot's return, on
the host only. The rebuild is journal-only and sends nothing; a client that is in the session during a restore is card
143.

- c98 K: a row whose actor is present, killable and alive goes. A dead actor (IsDead, or the Blueprint's Destroyed
  bool) that is placed, unreplicated and named, with no row, gets one. An actor no longer in a level keeps its row.
- c98 H: the row's value is rewritten to the actor's current bHidden. No rows are added, because hides on the loading
  thread happen on both sides alike.
- c98 S: kept. A building destroyed by a satchel after the save does not come back on the host either.
- c98 X: no change needed, because c132_ext_replay sends each tracked actor's current value after the rows.
- c98 P/M (card 104 copies): no change needed, because c98_unjournal drops a copy's rows when its actor is destroyed.
- c98 T: no change needed, because turret_sync journals each state change and the last one wins.
- c98 L: kept. No actor field gives a timeline's position. Open item.
- c38: no change needed. It is the client's own dead set, pruned against live health on each scan, and NPCs replicate.
- c56: the name of a pickup that is enabled now (byte 0x2A8) is dropped. A disabled placed pickup with no name is
  added.
- c49 objectives: no change needed, because c49_snapshot rebuilds the journal after SetObjectiveMapPostSave.
- c95 doors: rebuilt from DoorState (Open or Opening is 'O'; Closed or Closing is 'C'). A row whose open state differs
  is rewritten, and an open door with no row gets one. The active state has no field read and is kept.
- c95 collision: no change needed. Every write, load writes included, goes through the c95_relay_collision
  chokepoint, and the last value wins.
- c132 pose: no change needed, because each step drops the row of a corpse whose actor is gone or whose mesh is
  hidden.
- c132 decal: no change needed, because c132_decal_replay rescans the decal manager's active pool at the replay.
- c136 loops: no change needed, because journal_sweep drops rows that are dead or inactive, and c136_join_replay
  skips any row that is not alive and active.

## Run 2 predictions (written before the run; DLL 33068c68fc98d24d, for run 3 DLL 321e8298ed49abf8; scenario tools/scenarios/c137-run2.txt)

Host from a save (senior answer, 2026-11-08): the in-place restore before the join is accepted as the from-save
start. Both routes (the frontend Continue's OpenLevel and the in-place LoadLatestSave) end in LoadFromSlot on the host,
and the client is not in the session for either. The host plays to the first sub-objective, kills 2 barrels, takes
the placed Autocannon pickup, then completes LOS_O04, whose BP_CheckPointSaveZone_C "Bound Save" makes the checkpoint.
It kills 1 more barrel and 3 NPCs, then `travel checkpoint` restores the save in place. The client joins late
(travel at 300 s), so its world comes only from the join of the restored host state.

- Restore, after the join: the client has the save's state. The 2 pre-save barrels are dead, the post-save barrel is
  alive, and the Autocannon pickup is gone. The first digest round after the join has 0 mismatches. The ext fields the
  save changed reach the client in the join replay ("host ext join replay" sent = live), counted per class on both
  sides.
  Step 3 (senior, option A, two-way): the host rebuilds its journals from the actors' state when
  ASTSaveSystemEngine::LoadFromSlot (0x0520AE10) returns, which covers both routes. The reconcile line
  "card137: host t=... journal reconcile after LoadFromSlot" gives per-journal counts. Expected: c98 K dropped 1 (the
  post-save barrel), K added 0 (the 2 pre-save barrels already have rows), H fixed 0, c56 dropped 0 and added 0 (the
  pickup was taken before the save), c95 doors fixed 0 and added 0. The 3 post-save bugs count 0 in c98: replicated
  NPCs are not journalled. If the load destroys their corpses, the pose step drops their pose rows.
  After the client joins: the 2 saved barrels are dead on the client, the post-save barrel is alive on the client,
  the Autocannon is gone, and the first digest round has 0 mismatches.
- P1 barrels: after the reconnect, a digest round with 0 KillableProp mismatches; the client applies the barrels'
  K rows as end state (no explosion, no debris).
- P2/P7 placed pickups: the host's and the client's "pickup probe" marks strings are equal at the client's join
  (load + 10 s), at each "digest round" probe and at each 30 s summary. Pickup classes: 0 local-sim change rows outside
  the LoadMap window. (BP_DevLogPickup_C is the per-player collected-devlog state, listed separately.)
- P3 nuke: shot 1 and shot 2 each fire (card020 FireProjectile twice), and each shot's window (20 s apart) pairs
  NS_FX_Weapons_NuclearExplosion05 1/1 host to client.
- P4 gibs (6b): host totals at quit: overflow 0, "R unsent (live)" 0. Deferred and replaced are counted, and
  reported per 1 s budget window (from the M row times) to show whether the budget starves the flight bound. The rows
  are text with whole numbers (the 4 KB/s budget caps the bytes either way), so there is no int16 wrap and no
  re-origin count. Client rest check (1 s after R, distance to the host's R position, the part frozen by the R apply):
  every checked part within 10 uu. In flight: 0 samples over 150 uu. No RPCReliableBufferOverflow (reason 65) on
  either side.
- P6 pose: the client's "pose unfreeze ... applied" count equals the host's unfreeze snap count for live (non-replay)
  rows, with 0 "not simulating" lines; 0 Pose mismatches in the digest rounds after the reconnect.
- Card 142: the netbudget window peak at the late join and at the reconnect is logged, for card 142.
- Standard: the WARN diff against hb68, both logs, table peaks, rc, crash lines and the DLL hash.

### Run 2 attempt hb70 (DLL 33068c68fc98d24d): failed at the scenario, before the save

- The drive failed at line 34: `host wait log card056 ... PickupGone BP_WeaponPickup_Autocannon_C` timed out after 30 s.
  The goto reached the pickup (dist 59) and picked up the 2 shotgun ammo pickups, which are walk-over. The weapon
  pickup (class BP_WeaponPickup_PumpShotgun_C, named BP_WeaponPickup_Autocannon_C) needs Interact, and the scenario
  pressed nothing.
- Autoplay was blocked from the start: `autoplay: host blocked: objective LOS_O01_SO01 (other, ActorReference_C): this
  kind is not in phase 1`. The host stood still for the 150 s wait, so the client's fixed 300 s join (first Pong at
  host t=238021622) came before the save. That is the card 143 case, not this run's.
- The second barrel kill (BP_KillableProp_ExplosiveBarrel_C_1, -1900) chained to _3, so 3 barrels were dead before
  the save, not 2.
- rc 0/0, no crash dumps, 0 FATAL/ERROR. The reconcile never ran, because no restore happened.
- Scenario fix: the autoplay block is removed, and `host key E` follows the goto. Lane 2 saves were restored from the
  pre-run backup.

## Step 2 for NPC corpses: a bug already dead when its copy first reaches the client (hb70, the user's report)

The user watched hb70. At the join, the bugs the host had already killed stood in a pile and then died, instead of
lying where they fell. The 11 bugs were killed at host t=237868818 to 237909320, before any client was connected. The
client's join replay came at host t=238021627. Static trace from the hb70 logs (host UBWCoop-host-20261108-133726.log,
client UBWCoop-client-20261108-133728.log):

1. **Where the client first places the copy.** The copy is placed at the host's capsule, which stays where the bug
   died. The initial bunch carries the actor location, and a ragdoll moves only the mesh bodies, not the capsule. For
   example, client c131 "corpse first seen dead ... at (231562, 42901, 8772)" matches host "ragdoll start ... mesh at
   (231562, 42901, 8605)" (the capsule sits half its height above the mesh). All 11 copies arrived within 3 ms
   (client t=238021643 to 238021646), each at its own host death spot. The 11 deaths lie inside about 1400 by 1100 uu
   (230721..232069, 42076..43126), so the copies look like a pile. It is not the spawn point. What is wrong is the
   pose: the copy arrives with the living bug's animated mesh standing on its capsule.
2. **What makes it die visibly on the client.** Each copy runs this sequence, in the same tick as its Health OnRep:
   - card037 adopt/notify (Health 400.0 -> -294.0, HealthAttributeChanged)
   - card038 dead-set
   - card039 edge (capsule NoCollision)
   - card132 kept ragdoll collision 3
   - card131 "corpse first seen dead ... TryPlayDeathAnimation, base state 0 -> 4, HandleDying called, no death burst"

   c38_death_edge (enemies.cpp, corpse = true) calls TryPlayDeathAnimation for a corpse exactly as for a fresh death.
   The montage's anim notify then runs the Blueprint EnableRagdoll about 1 s later: client "card132: client ragdoll
   start ... from EnableRagdoll from Received_Notify" at 238022644 to 238022667, 11 lines. So the corpse is visible
   standing and then playing its whole death for about 1000 ms. The death burst and barks are already blocked for a
   corpse (card 131), but the montage and whatever its notifies trigger are not. The c41 Anim relay and the c136 replay
   play no part: 0 Anim rows at the join, and c136 has no NPC death effects for these actors.
3. **When the c132 pose join replay snaps it.** It never does. The host line is "card132: host pose replay to a joining
   PC: 0 rows (0 tracked)", and the client's "round 11 pose: 0 corpses checked". c132_pose_track drops a ragdoll start
   whose c41_guid is 0. A NetGUID is assigned the first time the actor is replicated to a connection, so every corpse
   that died before the first client connected has no guid, and no row (counted only in g_pose.no_guid). Every
   earlier late-join run shows the same: "0 rows (0 tracked)" in the host logs of 112921, 120536 and 130303. Even
   with a row, the apply waits for the copy's own EnableRagdoll (the collision gate in pose_try_apply, hb38), so the
   death would still play before the snap.
4. **A corpse that streams into relevancy later gets the same.** Opening an actor channel for a dead
   NPC goes through the same first Health OnRep and c38_death_edge(corpse) path, and so does a rejoin. If the corpse
   was never replicated to anyone (guid 0), the host has no row for it either.

**Fix, in the layers that own it:**
- Host, pose_c132.cpp (c132_pose_track / pose_host_row / c132_pose_replay): track a ragdoll start with guid 0 by its
  actor and snapshot it as usual. A row with guid 0 is not sent; it is marked owed. Each host step, and at the join
  replay, the guid is read again, and an owed row is sent to every ponged PC as soon as its guid exists. Host lookups
  by actor replace the guid lookup in c132_pose_sim_change.
- Client: the hook is c38_death_edge(corpse = true), the card 131 corpse path in enemies.cpp. For a corpse that is not
  gibbed, it no longer calls TryPlayDeathAnimation or BugAnimPhys OnDeath. HandleDying still runs, for the Dying
  state, AI and tag. Instead it calls c132_pose_arrival (pose_c132.cpp). If the host row is already held (the replay
  is sent at the first Pong, before the copies arrive), the arrival applies it straight away in that same tick:
  pose_try_apply starts the ragdoll at collision 3 (already set by card 132's kept-ragdoll rule), poses every body,
  and freezes it if the host's is frozen. If the row is not held, the mesh is hidden (SetHiddenInGame) until it comes.
  At each engine tick the arrival is retried; on success the mesh is shown and the pose applied in the same tick
  (shown before the ragdoll start, never hidden at it: hb14). A copy with no row after kArrivalWaitMs (3000 ms) is
  shown and given the old death visuals, with a WARN, and counted as a death play.

Run 3 predictions for this step:
- At the join, 0 client death plays for NPCs that were dead before the join. "card137: client corpse arrival" totals:
  death plays 0.
- For each such corpse, visible frames before the apply: 0, logged per corpse as "hidden N ticks, visible 0". The
  root error after the apply is under 100 uu ("card132: client pose apply ... after").
- Host: "pose replay to a joining PC" shows rows equal to the pre-join corpse count still alive as actors, plus the
  owed sends made after the join.
- One client screenshot (`client shot`) about 3 s after the client's first Pong, showing the corpses lying where they
  fell.

**Built (DLL 321e8298ed49abf8):**
- pose_c132.cpp, host:
  - pose_find_actor is now the host lookup, used by c132_pose_track and c132_pose_sim_change. pose_find(0) returns
    null.
  - A ragdoll start with no NetGUID is tracked (and still counted in "without guid").
  - pose_snapshot marks a guid-0 snap owed and sends nothing.
  - pose_guid_check runs each host step and at the replay. It reads the guid again. Once the guid exists, an owed
    snap goes to every ponged PC (the joining PC is marked ponged in c50_first_pong before c50_join_replay), logged as
    "card137: host pose owed snap sent guid ...".
  - The replay line adds "card137 N owed rows". A new map-end line gives "card137: host pose map totals: N rows got a
    NetGUID after their death, N owed snaps sent".
  - The R (remove) frame is not sent for a row that was never sent.
- pose_c132.cpp, client:
  - c132_pose_arrival holds up to 32 copies. The mesh is hidden with g_orig_c132_set_hidden, unless the game already
    hid it.
  - arrival_tick runs every engine tick, before the 250 ms step gate. pose_try_apply shows the mesh (after its
    collision gate, before the ragdoll start) and logs, uncapped, "card137: client corpse arrival <name> guid ...
    posed N ms after it arrived: hidden N ticks, visible 0 frames before its pose, no death play; X uu from the
    host's root after, largest body error Y uu".
  - A row already applied (a copy that left relevancy and came back, same guid) is applied again.
  - After 3000 ms the corpse is shown and c38_corpse_death_play runs, with the WARN "card137: client corpse arrival
    ... not posed after".
  - Map totals: "card137: client corpse arrival map totals: N arrived dead, N posed, N death plays, N gone, N not
    held".
- enemies.cpp, client: c38_death_edge (corpse, not gibbed) calls c132_pose_arrival instead of the death visual. The
  card131 line's visual field reads "card137 pose arrival, no death play". c38_corpse_death_play is the old visual,
  now used only on that WARN path.

**Scenario (tools/scenarios/c137-run2.txt):**
- After the reconcile, with no client connected, the host kills 4 WarriorBugs at about (196820, 3840). These are the
  pre-join corpses; they are owed rows.
- After the client's first Pong (the minimap origin line), the client is warped to (196820, 2840, 8300), looks at
  the corpses and takes `client shot c137-join-corpses`.
- CLIENT_DELAY_MS=540000, and the client's pawn wait timeout is now 900.
- `host look point 197606 -2002 7520` comes before `host key E` (hb71: `look actor` by that name was a class search).

**Measures for these predictions:**
- Death plays at the join: in the client log, card131 lines that name TryPlayDeathAnimation or BugAnimPhys OnDeath
  for the 4 corpses: 0. Corpse arrival WARNs: 0.
- Visible frames: 4 "posed ... visible 0" lines. A corpse that is never hidden and posed in its arrival tick shows
  "hidden 0 ticks".
- Error after the apply: the per-corpse "uu from the host's root after" value is under 100, and its largest body
  error is under 100.
- Host replay: the rows the host still tracks plus the owed rows equal the 4 kills. Each owed row has an owed-snap
  line at or after the join.

## Run 3 pre-run checks (DLL 321e8298ed49abf8, tag hb71, the last run in the budget)

**The checkpoint save.** LOS_O04 completion makes no save. In hb49, hb51 and hb52, CompleteSubObjective(LOS_O04,
LOS_O04_SO01) is followed by no "save engine save runs" line. Every FED_C2M1 save has caller exe+0x52186DF and follows
a player warp into the BP_CheckPointSaveZone_C at about (229312, 38956), with no objective done: hb49 1.4 s after
`warp actor point 229312 38956 9100`, hb68 2.1 s after the same warp, hb51 6 s after a client warp to (228451, 2790),
hb52 15 s after a warp to (229043, 38525). The "Bound Save" delegates on OnLevelObjectiveComplete (card 049) are not
what saves here. The scenario now warps the host into that zone (line 51) and waits 60 s for a new save (line 52).
Run 2 prediction text "completes LOS_O04, whose ... Bound Save makes the checkpoint" is replaced by this.

**Barrel chain (prediction change, accepted).** The second pre-save barrel kill (BP_KillableProp_ExplosiveBarrel_C_1)
chains to _3 (hb70), so 3 barrels are dead before the save, not 2. The restore predictions count the barrels from the
host's world K lines between the save and the load: every barrel dead before the save is dead on the client after
the join; every barrel that died after the save (the line 49 kill, and any chain from it) is alive on both sides, and
the reconcile's c98 K dropped count equals that number.

**Client join order.** CLIENT_DELAY_MS=540000. hb70's host reached its first save at +143 s; the slowest host path to
the reconcile line, with every wait at its timeout, is under 540 s. The analysis checks the order: the client's first
Pong must come after "card137: host t=... journal reconcile after LoadFromSlot". A client Pong before the reconcile
line voids the run.

**`key E` at dist 59.** No proof of the range value (UADInteractorComponent::TraceForInteractable, float at +0xF8,
not read). A take at a larger distance is proven: c100h, the client's server copy 156 uu from MoritaMk1, host
"card056: host t=12185389 PickupGone BP_WeaponPickup_MoritaMk1_C_0 ... sent to 1 of 1 clients". 59 uu is inside
that. The line that proves the take is weapons.cpp:1515, "card056: host t=%llu PickupGone %s (%s): sent to %d of %d
clients", where the first %s is the object name (BP_WeaponPickup_Autocannon_C, as the wait expects) and the second
the class (BP_WeaponPickup_PumpShotgun_C). `host look point 197606 -2002 7520` faces it first (hb71 below).

**Every wait line, traced to its printer (current line numbers):**

| Line | Wait | Printer | Fields |
|---|---|---|---|
| 21, 97, 313 | wait state pawn=host-own / client-own | testrun.cpp:1683 | the state file's pawn field |
| 23, 52, 58 | "card121: host t=\d+ save engine save runs" | save_c121.cpp:92 | host t=%llu, then the literal |
| 45 | "card056: host t=\d+ PickupGone BP_WeaponPickup_Autocannon_C" | weapons.cpp:1515 | object name first |
| 74 | "card121: host t=\d+ save engine load done" | save_c121.cpp:118 | host t=%llu |
| 75 | "card137: host t=\d+ journal reconcile after LoadFromSlot" | save_c121.cpp:169 | host t=%llu |
| 98 | "card120: client t=\d+ minimap origin" | chan_ops.cpp:88 | client t=%llu |
| 113 | client wait near 229612 38956 9100 600 | drive.py:271 | the state file's pawn position |
| 120, 129 | "card020: host host-own FireProjectile\(BP_WeaponFireAbility_NukeLauncher_C" | attributes.cpp:927 | matched in hb68 |
| 302 | "card098: client t=\d+ world K ... IsDead now 1" | world_c98.cpp:868 | matched in hb68 |
| 312 | "card045: client t=\d+ reconnected" | restart.cpp:130 | client t=%llu |
| 328, 331 | "card132: client t=\d+ digest round \d+ snapshot" | digest_c132.cpp:1209 | client t=%llu, round %u |

The scenario has no expect lines.


### Run 3 attempt hb71 (DLL 321e8298ed49abf8): failed at the scenario, before the save

- The drive failed at line 37 (now 41): the PickupGone wait timed out after 30 s. The goto arrived ("goto: end
  (reached): dist 56, path 541 uu, 2.0 s"). `host look actor BP_WeaponPickup_Autocannon_C` then faced the real
  Autocannon class at (147762, -38961, 9798), 62074 uu away. c64_find_actor (hooks_ubw.cpp) treats a class name as a
  class search, even when an actor's object name is the same text, and the pickup named BP_WeaponPickup_Autocannon_C
  is class BP_WeaponPickup_PumpShotgun_C. The E press logged "UADInteractorComponent::Interact(owner
  BP_PlayerCharacterMI_C): target null". My pre-run note ("matches the class, takes the nearest") named the rule and
  missed that this name is also a real class.
- rc 0/0, no crash dumps, 0 FATAL/ERROR. The WARNs are the 2 attach lines and the autodropship unpause line, as in
  every run. The save zone step, the restore and the corpse fix did not run.
- Scenario fix: the goto stops 150 uu short (197606, -2152), and `host look point 197606 -2002 7520` faces the pickup.
  Lane 2 saves restored from the pre-run backup (identical to the pre-hb70 backup).

### Run 4 setup (hb72, the user's approval: fail-soft)

- tools/drive.py: a `failsoft` line turns fail-soft on. From then on, a failed step logs "scenario: wait FAILED line
  N (<text>), continuing" ("step FAILED" for a non-wait step) and the scenario goes on. The run still ends when a
  role's game process is gone (game_pids, the card 128 lookup), and on a void step. The drive's exit code is 2 when
  any line failed, so a fail-soft run never reads as "scenario passed".
- New step `void unless <roleA> log "<rxA>" before <roleB> log "<rxB>"`: it ends the run as VOID when rxB's first
  [ ms] stamp is earlier than rxA's, or when rxB matched and rxA never did. Both games run on one machine, so the
  GetTickCount64 stamps share one clock. Checked offline on hb70's logs: with the reconcile regex it returns VOID
  (hb70 had no reconcile); with a host line from before the Pong it passes.
- Scenario: `failsoft` is at line 20. The void step is at line 101, right after the client's minimap origin wait (the
  line that starts its Pongs), and checks it against the host's reconcile line.
- Save retry (lines 55 to 58): while fewer than 2 save lines exist (line 19's is the first), the host leaves the zone,
  enters it again and waits another 60 s, then goes on either way.
- Analysis: each prediction that rests on a failed wait is reported "not tested (line N)", never as a pass.


### Run 4 results (hb72, DLL 321e8298ed49abf8)

The run ended by scenario after 1111 s: host rc 0, client rc 0, no crash dumps, FATAL/ERROR 0 on both sides. The drive
returned rc 2 with 4 failed lines: 52, 58, 328 and 331. The lane 2 saves were restored from savegames-prefix2-pre-hb71.

- Void check (line 101): host reconcile at 240291520 ms, client minimap origin at 240552664 ms. Order kept; the run is valid.
- Save zone (lines 52, 58): no save within 60 s after either zone entry. The only save in the run came when the player
  possessed the pawn (240133925, after ui continue). Re-reading hb49 and hb68 shows their first save also came at
  possession, not from the zone warp. Their later zone saves came only when the client's server copy was warped into the
  zone as well (hb49 cmd 58/59; hb68 cmd 4/5). Hypothesis, not traced: the zone needs every player inside, or it does
  not re-save the checkpoint that was just saved. BP_CheckPointSaveZone_C needs a static trace.
- Step 3 restore predictions: not tested (line 52/58). The travel loaded the possession save, which came before the
  barrel kills and the pickup. The reconcile matched that save: "c98 K dropped 3 added 0, H fixed 0; c56 dropped 1
  added 0; c95 doors fixed 0 added 0". The 3 barrels and the Autocannon pickup came back alive and enabled.
- Corpses at the first join, all predictions met:
  - client map totals: "4 arrived dead, 4 posed, 0 death plays";
  - 4 "posed ... visible 0 frames ... no death play; 0 uu from the host's root after, largest body error 0.0 uu" lines;
  - host replay: "0 rows (4 tracked) ... 4 owed rows". The 4 owed-snap lines were sent at 240562982 ms, after the
    join, to 1 PC each.
- Corpse drift after the pose: 703 ms after the pose, a blueprint ragdoll start (SetAllBodiesSimulatePhysics(1), caller
  exe+0x3B05087) ran on 3 of the 4 posed corpses. For guid 0x178 the card132 ragdoll start "bodies synced" moved the
  bodies 423 uu back to the mesh. Digest round 21 then put it 586 uu from the host (22 of 22 bodies over 100 uu).
- Corpses at the rejoin: failed. The client logged "20 arrived dead, 0 posed, 20 death plays" (WARN, 20 "not posed
  after 3000 ms" lines).
  - The corpses arrived at 240874420 ms. The client's 3000 ms fallback fired at 240877425 ms.
  - The client's first Pong went out at 240877481 ms, held until the minimap had an origin. The host's pose replay (21
    rows) followed at 240877504 ms, 79 ms after the fallback.
  - At the first join the Pong came about 10 s before the corpses arrived, so the race did not show there.
  - The fallback clock starts when the corpse arrives, but the host replay waits for the first Pong. That is a defect in
    the mod code.
- Screenshot c137-join-corpses: not tested. Line 106 ran at 14:25:02, about 6 s before the corpses arrived at the client
  (240562895 ms). It was also taken facing 196820 3940, away from the 194000 / 197000 death spots.
- Digest rounds after the reconnect (lines 328, 331): no round on either side after round 31 (240850702 ms, before the
  disconnect). `warp client` after the rejoin failed twice with "no server copy of the client's pawn", the same as
  hb68 line 80. Host WARN: "FindTeleportSpot found no room ... the rejoined pawn spawns N uu above".
- WARN kinds on the client that hb68 did not log: 20 corpse fallbacks, 19 card138 "hold still on ... pawn (null)",
  11 corpse collision mismatches (BP_WarriorBug_Corpses_Destructible_01_C_37, host 0 client 1), 11 pose body-distance
  rounds, about 30 gib spawn "no pool or no spawn", and 1 "rows pending and no ack". The host logged 5 WARNs: attach,
  autodropship, FindTeleportSpot, and 2 warp client.
