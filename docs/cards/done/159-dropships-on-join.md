# Card 159: A late joiner sees the host's dropships

Status: CLOSED, outcome in docs/CARDLOG.md. Was: queued 2026-11-08, from docs/playtest-2026-11-08.md finding 8. Budget: three runs.
Closed 2026-11-09: remainder proven in card 165 run c165c (NavDirection, spline id with a space).

Read: docs/playtest-2026-11-08.md finding 8, card 146 (join snapshot), card 144 (client local sim).

## Why

After the late join on FED_C2M1 the client saw no dropships. At load the client's own game ran SetState on every
BP_PlayerTransportDropship_C and BP_ProceduralDropshipTroops_C (exe+0x51D2615, exe+0x37DD566), and destroyed one
by local sim (exe+0x37DC6BD). Only 2 Dropship rows arrived after the join. The join sends no dropship state.

## Do

1. The join snapshot carries each dropship's state, mesh visibility and transform.
2. The client's own dropship logic and destroy stay off for host-owned dropships (with card 144).

## Done when

- A late-join run: every dropship the host has is present, visible and within 100 uu on the client at +5 s (a moving dropship: within the tolerance in the
  2026-11-09 decision below).
- The usual Report items (WARN diff, run length, rc, ERROR/FATAL, CRASH, dumps, DLL hash). It ends with "Report
  complete."

## From card 143 run c143-r3 (senior, 2026-11-08)

Card 140 late-join gap, same snapshot: STAISpawner_5 and STSpawnPoint_7/8 are attached live to the moving
BP_ProceduralDropshipTroops_C on the host, but a joining or rejoining client's fresh map never gets the attach (also
in c140g and c140m). The join snapshot carries each attach (parent, socket, relative transform) for actors attached
at run time. Added proof: 0 card 140 attach-group mismatches after a late join.
- Also in this snapshot (from card 146, playtest finding 26): the end state of every destroyed or killed placed prop
  at join (alive, collision, visibility), e.g. BP_WarriorBug_Corpses_Destructible_01_C keeping collision on the
  client after the replay. Added proof: 0 corpse-prop collision mismatches after a late join.

## Trace and predictions (junior, 2026-11-09, before run c159a)

Static trace from the playtest logs. Host: client/UBWCoop-menu-20261108-190720.log. Client:
logs/win64-old/UBWCoop-menu-20261108-191343.log. The late join was at host t=8982111.

1. The join replay was empty. The host logged "card132: host dropship replay to a joining PC: 0 of 0 journalled
   states sent".
   - All 22 troop dropships ran BeginPlay's SetState(2) inside LoadMap (host t=8955975, 44 changes).
   - c50_arm_map then called c132_dropship_arm_map after "LoadMap returned" (t=8956034) and wiped those 22 rows.
   - No state change happened between the load and the join, so the journal had nothing to replay.
2. BP_PlayerTransportDropship_C (3 placed, replicated) is never sent at all, live or at join. Only the troop class is
   relayed.
3. The card 132 digest covers only placed actors that do not replicate. "No dropship mismatch" in finding 8 therefore
   says nothing about the dropships, because both dropship classes replicate.
4. Do item 2 needs no change: the client's own game logic matches the host's.
   - At load both sides run SetState 2 -> 2 on every dropship, with the same callers exe+0x51D2615 and
     exe+0x37DD566.
   - The client's transport destroy (exe+0x37DC6BD) is the game's own departure. The host destroyed the same
     dropship at host t=9011983 with the same caller. The client's destroy came at the host-clock equivalent of
     t=9012097, which is 114 ms later.
   - So Do item 2 is closed with no change.

Not known statically: which of these differs on the client at join: state, actor bHidden, the mesh components'
bVisible or bHiddenInGame, or the transform. Blueprint code sets those, and the playtest logs carry none of them.
The dropship call log hit its 24-line cap at load.

Change (DLL after acee57eb67673896):
- At the first Pong, the host replay now builds a snapshot from the live actors instead of the journal: every placed
  BP_ProceduralDropshipTroops_C and BP_PlayerTransportDropship_C (and subclasses).
- One Dropship frame per dropship: "<name> <state> <bHidden> <mesh> <x> <y> <z> J".
- `<mesh>` has one letter per mesh component, in the order DropshipMesh, "Troop Carrier Mesh":
  - v = visible
  - h = bHiddenInGame
  - i = bVisible 0
  - b = both h and i
  - "-" = no such component
- Each snapshot dropship is also made always relevant.
- The client logs its own values against the host's for each frame, then applies what differs:
  - state: SetState
  - actor hidden: SetActorHiddenInGame
  - mesh: USceneComponent::SetVisibility and SetHiddenInGame
  - position more than 100 uu off: a teleport
- 5 s later the host sends the same snapshot again with kind C (check only).
- The client compares against that check frame and logs "card159: client dropship join check: N of M match".
- The client's receive also matches transport dropships now.
- Live sends are unchanged (state only, troop class).

Predictions for run c159a (late join on FED_C1M1):
- P1. The host logs "card159: host dropship snapshot (join) to <pc>: 25 of 25 sent". FED_C1M1 has 22 troop
  dropships plus the transport dropships; the exact count is read from the host log.
- P2. The client's "at join" lines show at least one field differing on the dropships. This names the cause of
  finding 8.
- P3. After the check: "join check: M of M match", with 0 WARN check lines (state, hidden and mesh equal; position
  within 100 uu).
- P4. The usual Report items (WARN diff against c157d: only card 159 lines are new).

### Build and run plan (junior, before run c159a)

- DLL 83fe3b5ab9bd18e5. Code in mod/src/ubw/dropship.cpp: c132_dropship_replay now sends a live snapshot (J) of
  every placed dropship of both classes, and c159_dropship_step sends the C check round 5 s later. The journal path
  for the join is replaced; the live per-change relay is unchanged.
- Scenario tools/scenarios/c159-dropship-join.txt: first join (J + C), then a client disconnect and rejoin (J + C).
- P3 note: the client's "join check: N of M match" totals line is written at the next map load, so the first join's
  totals appear when the rejoin loads. For the rejoin round, P3 is read from its per-dropship "check:" lines (every
  one "match", no WARN).

### Run c159a result (DLL 83fe3b5ab9bd18e5): partial

- P1 holds: "host dropship snapshot (join) ... 4 of 4 sent" and "(check) ... 4 of 4 sent", at first join and rejoin.
- P2: the diff lines name the gaps. BP_ProceduralDropshipTroops_C_3: client state 2 / host 0, meshes ii / vv,
  447-572 uu off. BP_PlayerTransportDropship_VerySlow_C_1: state 2 / 1, DropshipMesh i / v. C_1 and C_2 same.
  The host never logged a SetState to 0 for C_3 (only 2 -> 2 at load), so state 0 is written outside the SetState
  detour; the live relay cannot see it, and only the snapshot carries it.
- P3 fails for C_3 only: after the J apply, state and meshes match at the check, but the copy is 103250 uu (first
  join) and 3720 uu (rejoin) off. C_3 flies its spline on the host in state 0; the client's copy got the state and
  the place, not the spline distance, so its own TickSplineTravel runs from its own distance.
- Run: 57 s, rc 0/0, no ERROR/FATAL, no dumps. WARN: only the card159 lines above plus the usual attach/config.

### Run c159b plan and predictions (DLL 724982a34ecedf7f)

- The frame adds the spline distance (float at +0x484). J applies it when more than 100 off; both line kinds log it.
- P5: C_3's J line shows a spline distance gap and applies it (5 fields). Its check line is within 100 uu with the
  host and "match", for the first join and the rejoin. If it still drifts with the distance equal, the client's
  copy is not ticking the spline (or ticks at another speed), and the next step is a periodic host position frame
  for dropships in a flying state.

### Run c159b result (DLL 724982a34ecedf7f): partial, P5 half holds

- J applies the distance on C_3 (5 fields) both times; C_1, C_2, VerySlow_C_1 match at both checks.
- First join: C_3 J distance 0/160559; check 118272/117976, 296 uu off. The copy now flies the host's spline at the
  host's speed (42287 of distance in ~5 s, about 8460 uu/s); the 296 uu is about 35 ms of travel, the frame's
  one-way time. A 100 uu tolerance at that speed needs the copy within 12 ms, which no one-shot frame can give
  without adding the one-way time (card 026 rtt / 2) times the speed.
- Rejoin: C_3 J distance 0/22552; check -19612/2372, 2313 uu off. The host reached the spline end in those 5 s and
  ran its spline-end action (distance restarts); the client's copy ran past the end into negative distance. The
  spline-end action is host-only (authority), so a single join frame cannot follow a dropship through it.
- Run: 57 s, rc 0/0, no ERROR/FATAL, no dumps, WARN only the card159 C_3 lines plus the usual attach/config.
- Open: a moving dropship needs host frames while it flies (state 0), not only at join: a periodic "L" frame (state,
  distance, place, about 1/s) for dropships in a flying state, applied like J, with the distance led by rtt/2 times
  the speed, and the check tolerance set from that speed. One run left in the budget.

## Decision after runs a and b (senior, 2026-11-09)

In scope for this card, using its third run. A late join that leaves a flying dropship off its path fails the same
Done-when, and full world sync covers the flight, not only the join.

1. Static first: find what writes the dropship state field to 0 outside the SetState detour (the C_3 case). Relay
   that write live if it has one writer. Write down what it is either way.
2. While a placed dropship is in a moving state, the host sends an L frame about once per second, applied like J,
   with the distance led by (rtt/2) times the speed.
3. The client copy does not run its own spline end past the end. At the spline end it holds until the next host
   frame, because the end action runs only on the authority.
4. The +5 s check tolerance is 100 uu + speed x (rtt/2 + 50 ms) for a moving dropship, and 100 uu for a parked one.
   The Done-when uses this tolerance.

## Static step 1: the writers of the state field (junior)

Full-exe sweep for stores to +0x69B (llvm-objdump, every `mov* ..., 0x69b(...)`):

| Site | Value | Function |
|---|---|---|
| exe+0x51B98B0 | movw 0x102 (state 2, next byte 1) | constructor |
| exe+0x51C2E8C | 1 | DescendToLandCompleted (0x51C2D10) |
| exe+0x51C3027 | 1 | not named in the PDB |
| exe+0x51C5994 | 0 | exe+0x51C5970, called by StartAction |
| exe+0x51DC3DA | the argument | SetState (card 132 relays it) |
| exe+0x51DDF31 | 0 | StartAction |
| exe+0x1695C62, exe+0x6A6EA02 | | other classes (same offset, not a dropship) |

C_3's 0 comes from StartAction (directly or through 0x51C5970). The field has several writers outside SetState, so
no single write is relayed. The host tracker sends an L frame on the frame any tracked dropship's state differs
from its last sample, whichever writer made it.

## Run c159c plan and predictions (card 159 run 3)

Build: DLL e9e40f752b2f0508 (includes the card 165 launch diagnostic). Same scenario, plus a "live:" wait after the
first check and a batched card 165 client burst after clock 1.

- Frame now "<name> <state> <bHidden> <mesh> <x> <y> <z> <dist> <speed> <kind>", kinds J, C and L. Speed is the
  host tracker's spline-distance rate (per s, signed), 0 for a parked dropship, a state change, a new spline or a
  jump over 30000/s (the spline-end restart).
- Host tracker: every placed dropship sampled each 1000 ms; an L to every PC when it moved more than 10 uu or its
  state changed; tracked rows also send an L on the frame their state byte changes.
- Client J and L: the distance led to host + speed x rtt/2 (card 026 last round trip), applied when more than 100
  off; the place applied when more than the tolerance off. L lines capped at 24 per map.
- Client hold: each tick, a tracked copy with distance below 0 or past GetSplineLength is clamped there (logged once
  per hold until the next host frame).
- Tolerance (C check and place apply): 100 uu parked, 100 + |speed| x (rtt/2 + 50 ms) moving.

Predictions:
- P6: the client logs "dropship C_3 live:" lines while C_3 flies, speed about 8460 in magnitude, distance led by
  speed x rtt/2.
- P7: both check rounds report all 4 dropships "match" (C_3 within its moving tolerance), map totals "8 of 8 match".
- P8: no check line shows a negative distance or one past the spline length. Either a host L follows C_3 through
  its spline-end action, or the client logs "held at the spline end" first and the next L moves it on.
- P9: the host map totals print "card159: host dropship tracker: N L frames, M sends, 0 rows full" with N > 0.
- Card 165: the host logs at least one "card165: host t=.. launch" line for the client's burst, naming the shooter
  candidates and the controller found.
- No ERROR/FATAL, no dumps; WARN lines only the usual attach/config ones.

### Run c159c result (DLL e9e40f752b2f0508): partial; first join holds, rejoin fails on direction

- Run: 84 s, rc 0/0, FATAL/ERROR 0, no dumps. WARN: the usual attach/config, 9 card157 claim drops (the batched
  card 165 burst, see card 165), card132 round 1 STAISpawner/STSpawnPoint mismatches (seen before, not this card),
  and the one card159 rejoin check below.
- P6 holds: 31 L frames reached the client, C_3 speed -8200 to -8650 on the first leg, distance led by rtt/2.
- P7, first join holds: 4 of 4 match; C_3 at the check is 1 uu off (tolerance 670), distance 118472/118437. L
  frames keep it within 0-500 uu while it flies.
- P8 holds: near the end of the first leg the copy reached distance 0 first (led, host decelerating to 3400, 2059)
  and was held at 0 (5 holds, "distance -15 -> 0 (length 205095)"), then the next L moved it to the host value.
  No check line shows a distance below 0 or past the length.
- P7, rejoin fails: C_3 check 9139 uu off (tolerance 1120), distance 113885/123011, WARN differs. Logged cause:
  on the second leg the host's speed is positive (+8500); the copy's distance falls between L frames while the
  host's rises (L at t=..849451 applies 84398; 1 s later the copy reads 75836, the host 92375). The copy runs the
  spline the other way. The host's spline-end StartAction set a new NavDirection (+0x480) on the authority only;
  the frame carries the distance, not the direction, so each L puts the copy on the right point and it then flies
  backwards. On the first leg the copy's direction matched the host's, so it held.
- P9 not checked: the host map totals print at arm_map, and the host never left the map in this run.
- Fix (needs a decision; the run budget is used): the frame also carries NavDirection (+0x480) and the client
  applies it on J and L when it differs. Whether the end action also changes the spline (+0x400) is open: the
  StartAction hook is diag-gated and its lines were not logged in this run. A static read of the spline-end path in
  TickSplineTravel can settle it before any run.

## Review after run c159c (senior, 2026-11-09)

Closed partial: three runs used. The first join passes (4 of 4, C_3 1 uu off). The rejoin fails with a logged
cause: the frame carries the distance but not NavDirection (+0x480), which the spline-end StartAction sets on the
authority only, so the copy flies the second leg backwards.

Remainder, batched into the first run build of card 165 (its proof rides that run, not a new card):
1. Static first: read the spline-end branch of TickSplineTravel and write down whether it also changes the spline at
   +0x400. If it does, the frame also carries the spline's identity.
2. Add NavDirection to the frame; the client writes it on J and L when it differs.
3. Proof: a rejoin while C_3 flies its second leg, the check within the moving tolerance, and the host tracker
   totals (P9) printed at a map change or at quit.

## Remainder, static step (junior, 2026-11-09)

- TickSplineTravel (exe+0x51E75A0..0x51E8340). Its spline-end branch (exe+0x51E82B6..) runs once per leg: with
  +0x483 still 0 it calls Destroy (+0x482 = 1, exe+0x37DC670), SetState(2) (+0x482 = 2) or the descent
  exe+0x51C5D90 (+0x482 = 0), then sets +0x483 = 1. The branch writes neither +0x400 nor +0x480. Nothing in the
  function writes them, and neither does the descent callee.
- StartAction (exe+0x51DDD70) writes +0x481 startup, +0x480 nav and +0x482 target, and clears +0x483. It stores its
  spline argument at +0x400, or with a null argument the return of the virtual at vtable +0x720. Native callers in
  exe+0x5100000..0x5300000: exe+0x521C538, which passes a null spline and nav = (+0x719 != 0), and exe+0x521E3FE.
  The BP thunk execStartAction is a third way in.
- So the next leg's StartAction (not TickSplineTravel) sets the new NavDirection, and it can set a new spline.
  Static reading cannot say which spline the virtual picks, so the frame carries the spline's identity.

## Remainder, build (DLL 2b4e8fa114641922, card 165 run 1)

- The frame gains `<nav> <spline>` before the kind. nav is +0x480. spline is "<owner>:<component>", the placed names
  of the +0x400 component and its actor ("-" none, "?" unnamed).
- Client, on J and L: a different spline resolves through c50_find_placed(actor class, owner name) and
  FindComponentByClass with the class of the copy's current spline. It is written only when the found component's
  identity matches. Otherwise a WARN "spline ... not resolved", counted. A different nav is written. Both are written
  before the distance, so the spline-end hold clamps to the right length.
- The C check's "match" also needs the same nav and spline. The log lines print nav and spline client/host.
- Totals now come from c159_dropship_report(when), at the map end and at the quit command (testrun.cpp), on both
  sides.

Predictions for the rejoin:
- P10: if C_3 is on its second leg at the rejoin, the J line shows "nav a/b" with the host's value written (fields
  applied >= 1). The check line says "match": within the moving tolerance, with nav and spline equal.
- P11: the spline is either the same text on both sides (no write) or written. No "spline ... not resolved" WARN.
- P12: at quit, the host prints "card159: host dropship tracker (quit): N L frames ..." with N > 0, and the client
  prints "card159: client dropship totals (quit): ...", which shows the NavDirection writes.

## Remainder, runs (c165a, c165b, c165c)

- c165a (DLL 2b4e8fa114641922): regression. The client refused every troop dropship frame as "bad frame" (62
  times), and only VerySlow_C_1 parsed. So there were 0 live frames on the client.
- c165b (DLL 7351b9b3370b6181, diagnostic only: logs the raw text of the first 4 refused frames). Logged cause: the
  troop dropship spline component is named "Default Spline", with a space. For example:
  "BP_ProceduralDropshipTroops_C_3 0 0 vv ... 0 BP_ProceduralDropshipTroops_C_3:Default Spline J". `%129s`
  stopped at the space, so the kind field read "S".
- Fix (DLL b910af8313d99960): the kind goes before the spline. The spline identity is the last field and is read to
  the end of the text (`%129[^\n]`). The resolve path splits on ':' and compares the whole string, so spaces are
  fine there.
- c165c (DLL b910af8313d99960): 95 s, rc 0/0, no ERROR/FATAL, no dumps, 0 bad frames.
  - P10 holds: at the rejoin, C_3 was on its second leg (speed +11953). The J line shows "nav 0/1" and applied 5
    fields. The check shows "nav 1/1", 777 uu off within the tolerance of 1162, "match".
  - First join: C_3 at the check was 1 uu off (tolerance 727). Both rounds: 4 of 4 match.
  - P11 holds: 0 "spline not resolved". All four splines are equal by identity, so there were 0 spline writes.
  - P12 holds: the quit totals print on both sides. Client: "join check 4 of 4 match ... 8 live frames ... 1
    NavDirection and 0 spline writes, 0 splines not resolved". Host: "dropship tracker (quit): 64 L frames, 47
    sends, 0 rows full". The map-end totals showed 39 live frames and 5 spline-end holds.
- Card 159 remainder passes.

## Reopened after c165e (senior, 2026-11-09) — closed: proven in c161a, 22 of 22 post-travel checks within tolerance

After a travel, the client's checks of BP_ProceduralDropshipTroops_C_20 (8386 uu off, tolerance 655) and
BP_PlayerTransportDropship_C_2 (9151 uu off) fail. The live L frame before them used rtt 2056 ms (a load hitch) for
the lead, so the client put C_20 at distance 274992 against the host's 283420. Fix: the lead does not use one raw
rtt sample. Use a smoothed or median rtt and cap the lead (for example at 250 ms of travel), and skip the lead while
the client is loading. Batched into card 161's build. Proof in that run: a travel with flying dropships, all checks
within tolerance after it.
