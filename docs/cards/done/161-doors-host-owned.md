# Card 161: Doors move as the host's do, and block as they show

Status: CLOSED, outcome in docs/CARDLOG.md. Was: queued 2026-11-08, from docs/playtest-2026-11-08.md finding 4. Budget: three runs.

Read: docs/playtest-2026-11-08.md finding 4, card 095 (doors), card 144.

## Why

On the CTF course the client walked through doors shown closed and was blocked by doors shown open. No DoorOp row
reached the client; its own game opened and closed BP_Door_Sliding_Large_C_0 by local sim (exe+0x37C2C74).

## Do

1. Static first: what drives BP_Door_Sliding_Large_C (proximity per side) and why DoorOp does not cover it.
2. The host owns door state and position for every placed door class; the client applies it and its own door logic
   stays off. Sweep the door classes.

## Done when

- A run: the host opens and closes a sliding door; the client's door state and mesh position match within 200 ms,
  and the client's pawn is blocked only while the host's door is closed.
- The usual Report items (WARN diff, run length, rc, ERROR/FATAL, CRASH, dumps, DLL hash). It ends with "Report
  complete."

## Static trace (junior, 2026-11-09)

- BP_Door_Sliding_Large_C (and Medium, Gigantic, Gigantic_Vertical, Ramp_Medium) derive from
  /Game/Blueprints/EnvironmentElements/Doors/Parents/BP_Door_Base.BP_Door_Base_C, not ASTDoor. Card 095's DoorOp
  hooks watch ASTDoor only, so these doors never journal or send a DoorOp.
- BP_Door_Base is placed and not replicated (bReplicates clear). Each side runs its own door: the
  NPCDetectionCollider overlap events (BndEvt _2 begin, _3 end) call "Open Door" / "Close Door", which drive
  DoorState (0 closed, 1 open, 2 opening, 3 closing) and the PawnBuffer collision. So each game opened the door for
  the pawns and NPCs it saw itself, and the client's collision followed its own state, not the host's.
- Fix (door_c161.cpp): both sides track every placed, non-replicated BP_Door_Base door and poll DoorState each tick.
  The host sends a DoorOp ('O' for 1/2, 'C' for 0/3) through card 095's journal on each change of open half. The
  client refuses its own overlap events and Open/Close/Toggle calls on tracked doors, and applies the host's DoorOp
  by calling the BP "Open Door" / "Close Door" itself. Test command: `door open|close <name>` falls through from
  card 095 to card 161 for these doors.

## Predictions, run a (DLL 57c942009c98bf22, tools/scenarios/c161-doors.txt)

1. Both logs list BP_Door_Sliding_Large_C_0 as tracked, with open/close/overlap functions found (no WARN).
2. Host `door open`: host DoorState goes to 2 then 1; one card095 "OpenDoor BP_Door_Sliding_Large_C_0: sent"; the
   client logs "DoorOp Open Door ...: applied" and the same DoorState steps within 200 ms of the host's
   (offset-corrected by the playlog clock lines). Close mirrors it (3 then 0).
3. PawnBuffer collision is enabled while closed and off while open, the same value on both sides at each step.
4. Every client PE line for the overlap events on tracked doors says "refused"; Open/Close say "ran (mod call)" only.
5. Client walk: blocked north of (-3674, 9600) while closed, arrives at (-3674, 10400) while open, blocked going
   south after the close.
6. Card 159 lead (batched): after `host travel next`, the client's post-travel check lines all match with lead
   0 ms during the 10 s load settle; the live lines after it lead by min(median rtt / 2, 250) ms.

Risks the PE lines will show: the client's "Open Door" being authority-gated, a client close waiting on a
SafeToClose check, or a host auto-close timer.

## Run a result (c161a, DLL 57c942009c98bf22)

- Scenario passed, 108 s, rc 0/0, no ERROR/FATAL, no dumps. WARNs: host 2, client 6 (the two attach/config ones each
  side, and the known card132 STAISpawner_5 / STSpawnPoint_7/_8 z mismatch seen since c140m; nothing new).
- Door state follows the host: open 0 -> 2 host 281530539, client 281530573; 2 -> 1 host 281534525, client
  281534561; close 1 -> 3 host 281536802, client 281536836; 3 -> 0 host 281540785, client 281540832 (raw 34-47 ms,
  clock offset 44 ms, rtt 102 ms). PawnBuffer 3 while closed and 0 while open on both sides at each step.
- No NPC came near, so no overlap event fired on either side (prediction 4 not exercised).
- The walk proof was void: the navmesh goto took a path round the door (via -3417, 10034) and arrived each time.
- Card 159 lead: the post-travel check round matched for all 22 dropships (worst 439 of 729 uu, the flying
  BP_PlayerTransportDropship_C_2); live lines show lead 0 ms during the 10 s settle, then 25 ms (rtt 49).

## Predictions, run b (same DLL; straight walks)

The walker is warped to (-3674, 9500, 150), faces north and holds W 3 s.
1. Closed: the host (reference) and then the client stop at the door; "wait near -3674 10400" TRY-FAILs for both.
2. Open (after both logs show DoorState 2 -> 1): the client arrives north of the door.
3. Closed again: the client, walking south from (-3674, 10200), TRY-FAILs short of y 9500.
If the host passes through the closed door too, the door does not block pawns and that is game behavior.

## Run b result (c161b, DLL 57c942009c98bf22)

- Scenario passed, 117 s, rc 0/0, no ERROR/FATAL, no dumps; WARNs only the attach/config pair and the known card132
  spawner rows (client 11: the same rows on two rounds plus BP_Dropship_LandingBlocker_C_0 on the second map).
- Door state and PawnBuffer again matched step for step.
- The walk proof failed as a method: the host's warp to (-3674, 9500, 150) wedged it in geometry (it did not move on
  W), and the client's warp was snapped back to (-3601, 9643). From there the client held W north and ended at
  (-3533, 11768), past the door line (y 9849) while DoorState was 0 on both sides.
- Run a showed the same with the navmesh: the client crossed door _0 southward at x -3865 with DoorState 0.

## Static, after run b

BP_Door_Sliding_Base's construction script has Blocker1, Blocker2 (Block Nav / Unblock Nav), PawnBuffer
(BoxComponent), DoorSkeletalMesh (SKM_Gate_Medium_01, the open montage), CharacterDetectionCollider and
NPCDetectionCollider. Only NPCDetectionCollider has bound overlap events (casts to STNon_Playable_Character_Base and
BP_Player_Character_Bug), so troopers never open these doors; the level or the host's NPCs do. Card 095 found
BlockingVolume_1 at (-3635, 9855, 225) right at door _0, CollisionEnabled 0 on FAST: the entrance's real gate.
Open question for run c: does door _0 block a pawn at all in DoorState 0? The host walks the same line first as
the reference.

## Predictions, run c (DLL bae53a46c6c6e6e9: adds Blocker1/Blocker2/DoorSkeletalMesh CollisionEnabled to the lines)

1. If the host's reference walk crosses the closed door, door _0 does not block pawns in DoorState 0 (game
   behavior) and the client crossing it is correct.
2. Otherwise the client stops as the host does, closed and closed-again, and passes while open.
3. Blocker1/Blocker2/DoorSkeletalMesh CollisionEnabled match host and client at every DoorState line.

## Run c result (c161c, DLL bae53a46c6c6e6e9)

- 148 s, rc 0/0, no ERROR/FATAL, no dumps. Drive rc 1: FAIL at scenario line 61 (below). Client WARNs 6: the
  attach/config pair and the known card132 STAISpawner_5 / STSpawnPoint_7 / _8 z rows; nothing new.
- Prediction 1 held. The host's reference walk with DoorState 0 went from (-3307, 9532) north to (-3487, 11199): it
  crossed the closed door. The "wait near" TRY-FAIL (812 uu) is an overshoot, not a stop. The client did the same
  closed (to (-3532, 11729)) and open (to (-3548, 11627)). Door _0 does not block a pawn in any state, on both
  sides alike: game behavior. Its pawn gate is card 095's BlockingVolume_1.
- Prediction 3 held: Blocker1, Blocker2 and DoorSkeletalMesh are CollisionEnabled 3 at every DoorState line on both
  sides. Only PawnBuffer toggles (3 closed, 0 open), and it matched at every step.
- New fault (line 61): after the host's "door close", the host went 1 -> 3 (t 282087950 Recheck Blockers) -> 0 at
  282091885. The client applied the DoorOp Close (282087930), went 1 -> 3 and ran Recheck Blockers (282087985),
  then logged nothing more: DoorState stayed 3 for the whole 15 s wait, until quit. The client pawn stood at about
  (-3604, 10134), in the door. In runs a and b the client stood clear and 3 -> 0 came within 34-47 ms. So the
  client's own Recheck Blockers holds the close while its local pawn is in the door; the host's copy of the same
  pawn did not hold it there. Host closed, client stuck closing: a visible mismatch.
- The card 159 travel was not reached in run c; run a is the lead proof (22 of 22 in tolerance after travel).

## Conclusion (run budget used)

Done: door state follows the host on both sides (runs a-c), PawnBuffer matches, client calls are refused, and
the doors' non-blocking of pawns is the same on both sides (game behavior). Card 159's lead (median rtt, 250 ms cap,
skipped during load and the settle) is proven by run a.

Open: the close hold. Proposed fix, static only, for the next batched build: the host's DoorState change to 0
(closing done) is sent as a settle op, and the client, on receipt with its local door in 3, sets DoorState 0 and
PawnBuffer to the host's value (the same write path c161 uses for the open half), so the client's Recheck Blockers
no longer decides the end of a host-owned close. Proof: run c's close step with the client in the door, the client's
3 -> 0 within the usual 50 ms of the host's.

## Review after c161a-c161c (senior, 2026-11-09)

Closed partial. Accepted: the host owns the door ops, the client's own ops and detection BndEvt are refused,
PawnBuffer matches, and the closed door letting pawns through is the game's own behavior on both sides (written
above).

Open, batched into card 166's build: the client's Recheck Blockers holds DoorState 3 while its local pawn is in the
door. Fix it as a class, not only for the 3 -> 0 settle: the host sends every DoorState change it makes, whatever
wrote it, and the client writes the host's DoorState and PawnBuffer through the c161 write path. The client never
settles a door on its own. Proof in that run: c161c's close step with the client standing in the door, the
client's 3 -> 0 within about 50 ms of the host's, and no other client-only DoorState change in the whole run.

## Class fix, after the review (junior, built in DLL ac23c37c83cc2bf9; run batched into card 166's build)

- Host (door_c161.cpp c161_poll): every DoorState change, whatever wrote it, goes out as card 095's DoorOp
  "<name> <digit> <PawnBuffer>" through c95_host_door_state (journalled as the row's open state, so the first-Pong
  replay gives a joining client the last state; its PawnBuffer comes from card 095's CollisionOp replay).
- Client (c161_receive): a host state that crosses the open/closed half first calls "Open Door" / "Close Door" (the
  door's own montage and sounds), then c161_write sets the host's DoorState and PawnBuffer. "Recheck Blockers" (the
  close timer, the hold in c161c) is refused on the client like the other door triggers. Any DoorState change the
  client's own Blueprint makes is set back to the host's on the same tick: "WARN card161: client ... client-only
  DoorState a -> b, set back to the host's c", counted in the map totals.
- Predictions for the run: c161c's close step with the client standing in the door gives the client's 3 -> 0
  within about 50 ms of the host's; no "client-only DoorState" line in the whole run; map totals show 0 set back.
- Risk named before the run: the client's montage starts one way later than the host's and runs as long, so its
  OnCompleted (2 -> 1) may land before the host's 1 does. That shows as a set-back line; if it does, the montage
  callbacks' state write is the next one to refuse (their sounds checked first).

## Result of the class fix (c166a, DLL a4b6ea6a5a7d1e8e)

- The client stood in door _0 at (-3674, 9849) for the host's close. Host 3 -> 0 at t=283304368, client 3 -> 0 at
  t=283304398 (30 ms). Open 2 -> 1: 19 ms. No "client-only DoorState" line in the run.
- The close downed the client: 9999 damage from Actor.Type.Building (the game's door crush) on the host's server
  copy at t=283301692, then card043 Downed with Down sent. Game behavior, authoritative on the host and synced.

Accepted (senior, 2026-11-09): the class fix is proven in c166a (DLL a4b6ea6a5a7d1e8e). Close 3 -> 0 30 ms after the
host with the client in the door, open 19 ms after, and no client-only DoorState line. The door crush that downed
the client (card114, 9999, Actor.Type.Building) was applied on the host and synced, which is the game's own door
kill. Card 161 is closed.
