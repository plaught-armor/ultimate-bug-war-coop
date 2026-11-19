# Card 146: NPCs already dead appear in place on join and rejoin

Status: CLOSED, outcome in docs/CARDLOG.md. Was: queued 2026-11-08, split from card 137 run 4 (hb72) by the user. Budget: three runs.

Read: card 137 "Run 4 results (hb72)" and the corpse cause (hb70 trace), docs/STATE.md "Card scope and investigation
rules".

## Why

The user saw it in hb70: at the client's join, bugs already dead stood at their capsule spots and then died. The card
137 fix (owed guid-0 pose rows on the host; c132_pose_arrival hides the mesh, applies the host pose, shows it with no
death play, falls back after 3000 ms) passed at the first join in hb72 (4 arrived dead, 4 posed, 0 death plays) and
failed in two ways:

1. Rejoin: 20 arrived dead, 0 posed, 20 death plays. The fallback clock starts at arrival (240874420), but the host's
   pose replay waits for the client's first Pong (240877481, held for the minimap origin); the replay came 79 ms after
   the fallback fired.
2. Drift: 703 ms after the pose, a Blueprint ragdoll start (SetAllBodiesSimulatePhysics(1), caller exe+0x3B05087) ran
   on 3 of 4 posed client corpses; on guid 0x178 the card132 ragdoll-start "bodies synced" pulled the bodies 423 uu,
   and digest round 21 had it 586 uu from the host.

## Do

1. Static: what triggers that Blueprint ragdoll start on the client copy (the death path the arrival skipped, a timer,
   an OnRep), and why "bodies synced" moves bodies to the mesh. Write the cause and predictions before the build.
2. Fix: the fallback clock starts at the client's first Pong (or at the join replay), never at arrival. A posed corpse
   keeps the host's pose: the client's own ragdoll start is blocked or follows the host's state (frozen or not, card
   137 kPoseFrozen/kPoseUnfreeze).
3. One run with a join and a rejoin, both with corpses on the map. Also covers corpses that stream into relevancy
   during play.

## Done when

- At join and at rejoin: 0 death plays for NPCs dead before the join, 0 fallbacks, every corpse posed with visible 0
  frames, and every digest pose round after that under 100 uu.
- A client screenshot at each join facing the death spots.
- The usual Report items. It ends with "Report complete."

## Built in c140o (card 140 run 15, DLL 57976645d9592dee): the game's ragdoll start is blocked on a host-posed copy

Card 140 run 14 (c140n) logged 20 of the client's own ragdoll starts on copies that already held the host's pose.
All 20 came from caller exe+0x37C53CB (the BP_AI_AttackSystem "ragdoll left" path), each within about 120 ms of the
pose's own start. Guids 0x358 (c140m) and 0x342 (c140n), the two copies that later froze off the host's pose, are
both among them.

The fix is in `detour_c132_set_simulate` (enemies.cpp). On the client, a start from the game (nonzero exe caller) on
a copy whose pose row is Applied or Freezing is blocked before `c39_sync_mesh` or the engine call can move the
bodies. This uses the same pattern as card 136, which blocks client-own effects. The pose apply's own start has its
caller in the DLL, so it is not blocked.

Counting: each block logs `card146: client blocked the game's ragdoll start on host-posed guid ...` with the running
count for that guid. The map total is in `card140: client pose freeze map totals` as "game ragdoll starts blocked on
a host-posed copy N". A start on a copy whose row is still Pending, or a copy with no row, is not blocked.

## Note from the card 152 trace (2026-11-08): the other game ragdoll start

The game has a second start path in `BP_AI_AttackSystemEnemy_Base`: the ubergraph block at 0x23a8 (`IsAbleToRagdoll`,
`SetAllBodiesSimulatePhysics`, `WakeAllRigidBodies`, then `Wake Ragdoll`) enters the engine through
`execSetAllBodiesSimulatePhysics` at exe+0x3B05087. On the host it re-simulates hit corpses (card 152 has the trace).
That caller is also in the exe, so the block in `detour_c132_set_simulate` covers it on the client the same way as
exe+0x37C53CB. In c140q the client logged 26 blocks, all from exe+0x37C53CB and none from exe+0x3B05087. No change.

## Playtest 2026-11-08 addition (findings 5, 7, 10, 25, 26)

- Pose rows: CTF laser trooper corpses (0x2d2, 0x500, 0x55e), a WarriorBug corpse 0.5 s after the late join (0x68),
  and WarriorBug corpses in play (0x2f4, 0x2fc) got no host pose row within 3 s. Check on the host side whether rows
  were sent; cover the laser troopers' death path.
- Rejoin spawn (finding 25, host log): the PlayerStart at (232629, 43682, 9092) was blocked by a
  BP_PlayerTransportDropship_C mesh; "WARN card046: host player spawn at the origin ... no player Up to spawn beside:
  left at the origin". The host was dead, so the kill floor was gated off ("all down 1") and the pawn fell to z
  -101885 (and -225312 on the second rejoin). Fix: fallback order last checkpoint (BP_CheckPointSaveZone_C), then the
  host pawn in any state, then the PlayerStart with FindTeleportSpot's adjustment. Never leave a pawn at the origin.
  Below KillZ, put the pawn back at that point.
- Join end state (finding 26): after the replay 11 BP_WarriorBug_Corpses_Destructible_01_C kept collision on the
  client (host 0, client 1). The replay brings the destruction WorldOp but not the collision state after it. A join
  needs each prop's end state (alive, collision, visibility), with card 159's snapshot.
- Added proof: a rejoin while the host is dead and the PlayerStart is blocked; the pawn is on the ground within a few
  hundred uu of the chosen point at +2 s; 0 corpse-prop collision mismatches after the replay.

## r1 (2026-11-08): causes traced, fixes, predictions (written before the build run)

Causes, from the playtest logs (host t = client t - 249903283):

- Rejoin corpse WARNs (hb72): the client's arrival clock started when the corpse arrived, but the host's join replay
  (pose rows among it) only starts at the client's first Pong, which card 120 holds until the minimap origin is set.
  The 3 s wait ran out while the Pong was still held.
- In-play WarriorBug WARNs (0x2f4, 0x2fc) and the Morita troopers (0xf8, 0x530): the pawn was adopted by the c37
  scan only when its death arrived, so it took the "corpse first seen dead" branch and waited for a pose row. The host
  sends the first snap when the corpse settles (kPoseStillMs) or at kPoseForceMs 8000; 0x2f4's came 5.8 s after
  the client's arrival. The 3 s wait was too short, and these were live deaths in view, not join corpses.
- Laser troopers (0x2d2, 0x500, 0x55e, 0x4b8): no host pose row at all; whether they ragdoll on the host is not
  known yet (card 130: they are downed and picked up). The new live-death line shows which path they take.
- Rejoin spawn: PlayerStart encroached by BP_PlayerTransportDropship_C, FindTeleportSpot 0, the host dead (no Up
  player), so c46_spawn_spot left the pawn at the origin; with all down the kill floor gate was off and the server
  copy was held (false) and fell forever.

Fixes:

- pose_c132.cpp arrival_tick: the wait counts from the later of the arrival and the first Pong of this map load
  (c120_pong_open_ms), kArrivalWaitMs = kPoseForceMs + 1000; while the Pong is held the corpse stays hidden, up to
  kArrivalHoldMaxMs 30 s. The WARN names both times.
- enemies.cpp c37 scan: each scanned pawn keeps when it was first seen (C37Seen.since). An adopted pawn seen alive
  for kC37SeenLiveMs 1 s or more whose health then reaches 0 is a live death: c37_notify and c38_death_edge with
  corpse=false (the death visual plays), "card146: client ... adopted dead after N ms in view ... live death".
- objectives.cpp: c46_rescue_spot, in order: the last checkpoint (each host Save_CheckPoint, caller exe+0x52186DF,
  notes the BP_CheckPointSaveZone_C nearest the saver within 5000 uu, else the saver's spot), then beside the host's
  pawn in any state, then the first PlayerStart moved by EncroachingBlockingGeometry's adjustment and cleared by
  FindTeleportSpot. c46_spawn_spot uses it when nobody is Up (never the origin while the map has a PlayerStart).
- objectives.cpp c46_all_down_return: a server copy below KillZ while all are down goes to c46_rescue_spot, and the
  client moves its own pawn there (new ChanOp::WarpTo text frame "x y z"), with the card 046 cooldown.
- Not in r1: the corpse-prop collision end state at join (finding 26) is built with card 159's snapshot, as the
  playtest addition says.

Predictions for run c146-r1 (tools/scenarios/c146-proof.txt, FED_C1M1, FAST=1, GOD=0):

1. Bugs spawned beside the client and killed after more than 1 s in view: no "corpse first seen dead" and no
   "WARN card137 ... not posed" for them; each has either a normal death edge or a "card146: ... live death" line.
2. Client rejoin with those corpses on the ground: each corpse arrival logs "card137: client corpse arrival ...
   posed", none "not posed"; the map totals line has 0 death plays for arrivals.
3. At least one "card146: host t=... checkpoint save by ..." line (the FED_C1M1 checkpoint zones).
4. All down (client, then host), then the server copy warped to z -23000 (KillZ -22000): "card146: host t=... kill
   floor while all down: ... back to the last checkpoint at (...)" with WarpTo frame sent, and "card146: client
   t=... WarpTo ..." with the own pawn moved within 200 uu of that point.
5. No new WARN classes against c156-r2 apart from those named above.
6. Not driven: the origin-spawn branch of c46_spawn_spot (FED_C1M1's PlayerStart is free at a rejoin). It calls the
   same c46_rescue_spot that prediction 4 runs.

## Report r1 (run c146-r1, DLL cbecf3fcdd840a50; final DLL cdc357c3aebb05fd)

Run: `logs/c146-r1-{host,client}.log`, drive log `logs/c146-r1.drive`. Host rc 0, client rc 0, no crash dumps.
Drive rc 2 comes from one failed wait, line 38 of my scenario. The wait for the corpse arrivals ran before the
step that moves the client back by the corpses. Its 40 s ran out at client t 270648.3 s, and the arrivals came at
270649370, right after that move (relevancy). This is a scenario order error, not a mod failure.

1. Live deaths in view: three spawned WarriorBugs, killed after 3 s in view. Each has a normal client death edge
   (`card038: client death edge BP_AI_WarriorBug_C ... Health 400.0 -> 0.0 ... TryPlayDeathAnimation`, 270591977,
   270592080, 270592200). They have no corpse wait and no WARN card137. The new live-death branch did not fire,
   because these pawns were adopted alive. Held.
2. Rejoin with corpses: all three arrivals were posed: `card137: client corpse arrival BP_AI_WarriorBug_C guid 0x1ec
   posed 0 ms after it arrived: hidden 0 ticks ... no death play; 0 uu from the host's root after`, and the same for
   0x1f6 and 0x1e2. There were 0 "not posed" lines. Held.
3. `card146: host t=270583424 checkpoint save by BP_PlayerCharacterMI_C at (-1769, 1820, 296): rescue anchor the
   saver's spot at (-1769, 1820, 296)`. FED_C1M1's start save has no BP_CheckPointSaveZone_C within 5000 uu, so
   the anchor is the saver's own spot. Held.
4. All down, then the copy was moved to z -23000. Host: `card146: host t=270652869 kill floor while all down:
   BP_PlayerCharacterMI_C (server-copy) at (12600, 14200, -23000), KillZ -22000, back to the last checkpoint at
   (-1769, 1820, 396), moved 1; WarpTo frame sent`. Client: `card146: client t=270652885 WarpTo 1 (-1769, 1820,
   396): own BP_PlayerCharacterMI_C (12600, 14200, 18) -> (-1769, 1820, 396), moved 1`. The return ran once, with
   no repeat. Held.
5. WARN class diff against c156-r2 found two new classes:
   - Host: `WARN card043: host real death: 5 hits, Health 1.0`. This is the known class the senior placed on card 157.
   - Client: `WARN card140: client pose guid 0x1e2 snap 2: frozen row simulating 1547 ms after its apply; worst body
     9 is 540 uu`. The c140o block caught one ragdoll start on 0x1e2 (exe+0x37C53CB) at 270650677, and the corpse
     simulated again anyway. This is the second game ragdoll start from the card 152 trace note. It belongs to card
     152, not this card's arrival clock.
6. Not driven: the origin-spawn branch of c46_spawn_spot. FED_C1M1's PlayerStart is free at a rejoin. That branch
   calls the same c46_rescue_spot as item 4.

After the run, self-review found that the anchor's world-pointer check could match a new map that reuses the old
UWorld address. The anchor (world and point) moved into C46State, so c46_arm_map clears it at each map load (DLL
cdc357c3aebb05fd). No logic changed apart from that; it has not been run.

Open: the corpse-prop collision end state at join (finding 26) is left for card 159's snapshot. The scenario took no
client screenshot.

Report complete.

## r2 (2026-11-08): the origin-spawn branch run for real, predictions (written before the run)

Senior's r1 review: the origin-spawn branch is the playtest bug and has to run. FED_C1M1's PlayerStart is free, so a
test-only switch stands in for the encroached start. `UBWCOOP_C146_NO_START=1` (objectives.cpp
`c146_test_no_start`): a player spawn on the host while 2+ PCs are present and nobody is Up is treated as
FindPlayerStart's WorldSettings fallback, so `c46_spawn_spot` runs its origin branch (c46_probe_starts, then
c46_rescue_spot). With the switch off, nothing changes. DLL 0f776365b8c75d05, which includes the C46State anchor move
(cdc357c3aebb05fd).

Run c146-r2 (tools/scenarios/c146-r2.txt), FED_C1M1, FAST=1, GOD=0, host env UBWCOOP_C146_NO_START=1. The checkpoint
save at the start sets the anchor. The client is downed, then the host is downed: all down, host death. The host
stays dead on its death screen, which waits for a button. The client disconnects and rejoins.

Predictions:

1. Host: "card146: host t=... checkpoint save by ... rescue anchor ..." at the start, as in r1: (-1769, 1820, 296).
2. Host, at the rejoin spawn: "card146: host test: player spawn at (...) with nobody Up taken as the origin
   fallback", then "card146: host player spawn at the origin with no player Up: spawns at the last checkpoint
   (FindTeleportSpot ...) (x, y, z) instead", within about 150 uu of the anchor. There is no "WARN card046 ... left
   at the origin".
3. Client: after "card045: client t=... reconnected" and its own pawn state, at +2 s its pawn is within 300 uu of
   (-1769, 1820, 296). It is still within 300 uu 3 s later, so it is on the ground and not falling. No "card046
   ... kill floor" line and no "card146 ... kill floor" line on either side.
4. Possible failure that the run decides: while all down, the rejoined pawn may come back in a Downed or Out state,
   or the host's all-down state may reject the new PC. Either one is logged by card 043 and gets written down, not
   worked around.

Finding 5 (CTF laser troopers with no pose row), answered from the playtest host log
client/UBWCoop-menu-20261108-190720.log:

- On the host, laser trooper deaths run HandleDeathEvent and HandleDying ("base state 2, movement mode 2, mesh hidden
  0", t=8662350 for Laser_Blue, 8662520 for Laser_Red), then card 130's AI pick-up goes to them. The host log has no
  pose line for guid 0x2d2, 0x500 or 0x55e, so their deaths never reach detour_c132_set_simulate: there is no
  ragdoll start and no pose row to send.
- The client's 0x2d2 line was "corpse first seen dead" (relevancy arrival), not an adopted live death. So the c37
  live-death fix does not cover it: the arrival waits kArrivalWaitMs for a row the host never sends, and then shows
  the corpse with a WARN.
- Open: a corpse arrival whose host death has no ragdoll (troopers, pick-up targets) should not wait for a pose row.
  The host needs to say so (for example, a "no ragdoll" pose state per guid) or the client needs to know the class
  path. This is not fixed in r2; it is written down here for the senior to place.

## Report r2

Run c146-r2 (tools/scenarios/c146-r2.txt), DLL 0f776365b8c75d05 (built on cdc357c3aebb05fd plus the r2 test flag),
host env UBWCOOP_C146_NO_START=1. Host rc 0, client rc 0, FATAL/ERROR 0, no new crash dumps. The drive returned rc 1
because of its last step only (prediction 3, second check). Runs used: 2 of 3.

Change in r2: objectives.cpp gains c146_test_no_start(). When UBWCOOP_C146_NO_START is set, at least 2 players are on
the roster, and nobody is Up, c46_spawn_spot treats any player spawn as FindPlayerStart's origin fallback. That runs
the rescue branch for real without a map where the start is blocked. The switch is off unless the env var is set.

Predictions:

1. Pass. Anchor set at the checkpoint save, (-1769, 1820, 296).
2. Pass. Host: "card146: host test: player spawn at (-560, -1715, 128) with nobody Up taken as the origin fallback",
   then "FindTeleportSpot at the last checkpoint (-1769, 1820, 296): 1 -> (-1769, 1820, 396)" and "spawns at the last
   checkpoint (-1769, 1820, 396) instead". That is 100 uu above the anchor. There was no "left at the origin" WARN.
3. Part pass. The client reconnected 5853 ms after the drop. Its pawn spawned at (-1769, 1820, 378) and landed (the
   Landing.Human cue at 271047711). At +2 s it was within 180 uu of the anchor. At +5 s it was 304 uu away: the state
   file reads (-1764, 1516, 296), so it moved about 300 uu in y with z level. It is on the ground, not falling, and
   neither side logged a kill-floor line. The drive sent no input. The cause of the slide is not found. One lead: the
   host's dead pawn lies at the same spot ((-1769, 1820, 241), from host significance-view lines before and after
   the rejoin), so the new pawn spawns on top of the host's corpse and may be pushed off it. The test died at the
   checkpoint because the scenario never moved away from it. On a real map the host's corpse is usually somewhere
   else.
4. Did not happen. Neither side logged a Downed or Out state for the rejoined pawn, and no PC was rejected.

WARN diff against r1:

- New on the client: "WARN card156: client t=271036713 back to Up (map load) +5 s: bShowMouseCursor 1, viewport
  capture 0, HUD UIMode 1, IMC_DummyUI_KBM mapped, death screens live 1". This is the client's death screen during
  the all-down (card 156's class), logged before the disconnect. It belongs to card 156, not to this card.
- Gone on the client: the card140 0x1e2 frozen-row WARN (card 152's).
- Gone on the host: "card043 real death: 5 hits". r2 downs players by command, not by hits.

Finding 5: answered above, under the r2 predictions. The c37 live-death fix does not cover the CTF laser troopers.
This stays open, for the senior to place.

Open for the senior: the 300 uu slide in prediction 3, which needs a decision on whether it matters. The rescue spot
works: the pawn spawned at the checkpoint, on the ground, with nothing left at the origin.

Report complete.
