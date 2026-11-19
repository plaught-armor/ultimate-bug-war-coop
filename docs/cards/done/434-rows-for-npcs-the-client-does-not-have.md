# Card 434: rows for NPCs the client does not have (net cull)

Status: done 2026-11-16 21:39 (both cases proven by log, see the last section).

## Finding (run c399b128a, FED_C5M2, b128)

Card 325 predicted that target misses stop within 5 s of the host's first NPC spawn row. They did not: 62 `card325 target
miss` rows and 61 `card041 ... no NPC matched` rows ran to the quit (+27.5 s). Every missed guid (0x81a, 0x864, 0x88c and
seven more) is a runtime-spawned NPC fighting 10,400-13,700 uu from both players, who stood still at (-32372, 35752). The
client log holds no row at all for those guids except the misses, while the host sends `card041: host Anim ... sent to 1
PCs` for them. The digest passed with 0 mismatches, so this is not a late load. Reading: the NPC copies are outside the
engine's net cull distance and do not exist on the client. The cue's effect still plays on the client at the cue point
("played on the pc (target miss)"), so nothing visible is lost; the Anim rows are wasted bytes and each miss writes a
client log line.

## Step 1 (diagnostic)

`card098` world-change rows gain `net cull <d> uu` (AActor::NetCullDistanceSquared by reflection, -1 when not found).

Prediction: NPC classes on C5M2 print a cull distance under 10,400 uu. If the distance is above 13,700 uu the reading is
wrong and the cause is something else (trace the actor channel instead).

## Step 2 (only if step 1 holds)

- Host: no per-NPC Anim row to a PC whose view point is beyond that NPC's cull distance (bytes saved, measured by the
  card041 sent count on the same C5M2 hold).
- Client: a miss for a guid with no actor counts in the totals row instead of one log line each.
- Card 325's done-when changes to: no miss for a guid whose NPC is inside the cull distance of the client's view.

Done when: the C5M2 40 s hold shows 0 `no NPC matched` rows for guids inside the cull distance, and the host's Anim sent
count for out-of-range NPCs is 0.

## Step 1 result (b130 `13a4e600eb021c80`, run `c423b130a`, FED_C2M1, 2026-11-16): prediction failed on C2M1

Every `card098` row with a value reads `net cull 15000 uu`, all NPC classes (warrior, chariot, aphid, trooper,
AttackSystem bases, controllers). The misses at 10.5k to 13.7k uu are inside that distance. The exe has no
ReplicationGraph symbol and no game override of `IsNetRelevantFor` (PDB publics: only AActor, APawn,
APlayerController and engine classes). C5M2 values owed from `c399b130a`. Step 2 as written is void if C5M2 matches.

Client rows, same run: 81 cue target misses, 60 of them with the world key on an NPC owner (53 WarriorBug, 7 M7Mech),
that is the host found no NetGUID for the owner at the send; 20 with a guid the client cannot resolve.

## Step 2 (diagnostic, patch `c434_no_guid_diag_patch.py`, for b131)

`c434_owner_row` (world_c98.cpp), host, two callers: `host_key` (fx_c136_net.cpp) for an FX owner keyed by guid that
has none, and `c41_send_attack` (enemies.cpp) for an Anim row whose NPC is 10000 uu or more from every remote pawn.
Row `card434: host WHY: NAME (CLASS) guid 0x..., RemoteRole R (0 = not replicated), NetDormancy D, net cull now C uu,
at (...), from the remote pawns: slot S N uu`, 80 rows per
run. Readings:
- RemoteRole 0: the game runs these NPCs unreplicated (a host-only crowd); the fix is the host sending their rows
  with the world key only (as now) and no Anim rows at all for an NPC with no guid.
- NetDormancy 2 or more with RemoteRole set: dormant until woken; same host rule, keyed on the guid.
- `net cull now` under the pawn distance: a blueprint lowers the cull after the spawn; step 2 of the first plan holds
  with that value.
- None of the three and the pawn distance under the cull: the actor channel is late or starved; then the row count
  per owner over time (same name repeating for seconds) is the evidence and the trace goes to the channel.
In every case the host-side saving is the same shape: no per-NPC Anim row for an NPC whose guid is 0 at the send.


## 2026-11-16 12:51: C5M2 rows and step 2b (senior)

C5M2 (`c399b130a`): 4 WarriorBug guids (0x820, 0x838, 0x860, 0x89c), 9275 to 15104 uu from the host pawn and about
9980 uu from the client's listener, never reached the client in 12 s while the host sent cue and Anim rows by guid.
Not bandwidth: `netbudget` reads "not net ready 0 of 231 frames" in that window. Not significance: the host already
adds each remote pawn as a significance location (card 132).

Two engine conditions remain and the step 2 row did not read them, so the patch `c434_view_point_patch.py` (applies on
top of step 2) adds them:
- `APawn::IsNetRelevantFor` (exe+0x3F67330) returns false for an actor with bHidden (or flag 0x04 of +0x58) whose
  root component has collision 0, before it tests any distance. Row fields `actor flags 0x..` and `root collision N`.
- The cull distance is measured from the PC's view point (FNetViewer), not its pawn. Row field
  `(view point N uu)` per remote slot, from `APlayerController::GetPlayerViewPoint` on the host's copy of the PC.
The Anim caller's floor drops from 10000 to 5000 uu so the C5M2 bugs print.

Readings: flags with 0x80 and root collision 0 -> hidden NPCs are cut from replication by the engine; the fix is the
host not sending rows for them (the client cannot draw them) or, if the host draws them, finding who hides them.
View point over 15000 uu with the pawn under it -> the host's view point for the client is stale (dropship ride or
cutscene camera); fix at the view target. Neither -> read the actor channel for the connection next.

## 2026-11-16 13:03: step 2 rows on C2M1, b131 run `c423b131a` (senior)

80 host rows (the cap), all BP_AI_WarriorBug_C with guid 0, RemoteRole 1, NetDormancy 1, net cull 15000. 78 are
"FX owner has no NetGUID", 2 are "Anim sent for a far NPC" (13532 and 14466 uu). 70 are over 15000 uu from the
remote pawn: those NPCs never replicate, which is the engine's behaviour. 7 are at 5000 to 15000 uu and 1 at 1945 uu,
all inside one spawn wave (51 rows in one second at t=493854104): rows sent on the spawn frame, before the first
replication gives the NPC a NetGUID. The client plays FX misses at the world location (card 325), so the visible gap
is the Anim rows only. Fix step, after the step 2b view point fields: the host skips per-NPC Anim rows while the NPC
has no NetGUID, and counts them.


## 2026-11-16 13:32: step 2b rows on C5M2, b134 run `c399b134a`, and step 3 (senior)

C5M2, 18 s from the first spawn row to the quit: 11 guids (0x826, 0x856, 0x86a, 0x874, 0x87e, 0x888, 0x8a6, 0x8b0,
0x8c4, 0x8ce, 0x8d8; troopers and warrior bugs) got 72 client `card041 ... no NPC matched` rows and 0 played rows;
4 guids (0x860, 0x892, 0x89c, 0x8ba) got 48 played rows. Card 436 holds worked (0 guid-0 Anim rows, 49 `sent by guid`),
so every miss now has a NetGUID on the host and no actor on the client.

Both step 2b readings are ruled out by the rows: every missed NPC has actor flags 0x70 (no bHidden), RemoteRole 1,
NetDormancy 1, net cull 15000 uu, and sits 7938 to 14771 uu from the client's view point (pawn and view point within
4 uu). Not a clean distance split: 0x8a6 at 7939 uu and 0x8c4 at 7938 to 8191 uu missed while 0x89c at 6862 to 9441 uu
played. No ReplicationGraph and no game override of IsNetRelevantFor (STATE 2068).

Step 3 (diagnostic, host, same `c434_owner_row` row, two fields more):
- `channel N`: `UNetConnection::FindActorChannelRef` (public symbol, takes a `TWeakObjectPtr<AActor>`) on the remote
  PC's connection: 1 = an actor channel is open for the NPC, 0 = none.
- `relevant N`: `APawn::IsNetRelevantFor(RealViewer = the remote PC, ViewTarget = its view target, SrcLocation = its
  view point)` called through the NPC's vtable: the engine's own answer.

Readings: `relevant 0` -> read which test fails in exe+0x3F67330 for that NPC (owner, attachment, bOnlyRelevantToOwner).
`relevant 1, channel 0` for seconds -> the driver never gets to the actor: consider list (NetUpdateFrequency, dormancy
flush) or the per-connection prioritized cut; read `ServerReplicateActors_PrioritizeActors`.
`channel 1` with client misses -> the channel is open and the client has not spawned the actor: client side (spawn
bunch waiting on an unloaded asset or an unmapped guid in the bunch); print the client's pending-guid count next.

Efficiency number for step 2 of the fix: 72 of 120 Anim rows (60 %) in this window went to a client with no actor.

### Static read of APawn::IsNetRelevantFor, exe+0x3F67330 (senior, same day)

Order of tests in the shipped code: (1) always relevant (+0x58 bit 0x08), viewer is the controller (+0x2d8), owner
chain (+0x158) holds the view target or the viewer, self, instigator, based-on either way (vtable+0x610) -> true.
(2) bHidden (0x80) or bOnlyRelevantToOwner (0x04) with no root collision -> false. The missed NPCs have flags 0x70, so
root collision 0 does not matter for them. (3) **Movement base**: when GetMovementBase() (vtable+0x750) is set, its
owner (+0xa0) is set, the pawn has a movement component (vtable+0x748), and the base is a skeletal mesh component or
its owner is the pawn's owner, the answer is `BaseActor->IsNetRelevantFor(...)` (vtable+0x528): the NPC is exactly as
relevant as the actor it stands on. (4) Otherwise distance (`bUseDistanceBasedRelevancy`, CDO +0x345, then
IsWithinNetRelevancyDistance exe+0x382A2F0).

New leading reading: the missed NPCs stand on a skeletal mesh actor (dropship, bug hole, lift) that is itself not
relevant to the client (hidden with no collision, or not replicated). It fits the rows: no distance split, same
classes both played and missed.

Step 3 gains one field: `base <component name> of <actor name> (base flags 0x.., RemoteRole N)` from
GetMovementBase() on the NPC, `base none` when null. Reading: a base actor on every `relevant 0` row -> fix by class
at the base (make the base relevant, or clear the delegation), not per NPC.


## Step 3 result (b135 `18be6a2b9157e5d5`, senior, 2026-11-16 14:14)

Runs c399b135a and c423b135b. Every `card434: host Anim sent for a far NPC` row reads `channel 1`: 205 rows in
c399b135a (192 with base none, the rest on landscape or an instanced mesh, all `relevant 1`) and 80 rows in c423b135b
(43 `relevant 1`, 36 `relevant 0`), with 0 client misses in c423b135b.

On C5M2 in c399b135a (client world load at tick 498358957) 11 guids never resolved on the client from tick 498365042 to
the quit, up to 23.7 s: 75 `no NPC matched` rows, 0 played. Guids 0x84a, 0x872, 0x85e, 0x868, 0x854, 0x87c, 0x89a,
0x8a4, 0x8ae, 0x8b8, 0x810 (WarriorBug, Trooper_Morita, Trooper_Shotgun; 7040 to 14718 uu from the client; the host
says `channel 1 relevant 1`, base none except 0x810 on landscape). 48 other guids played in the same window.

Conclusion: the movement-base reading is ruled out (base none on 10 of 11). The host holds an open actor channel for
each of them and the client has no NPC object for the guid. `c41_resolve` cannot tell "no object" from "object of
another class". Limit of this step: `channel` read 1 on all 285 rows and never 0, so the diagnostic has not been seen
to discriminate.

## Step 4 (diagnostic, client; patch `c434_step4_patch.py`, for the build after b136)

Detours on `UPackageMapClient::SerializeNewActor` and `UActorChannel::CleanUp` (pdb publics), client session only, and
one row per Anim guid miss. Rows:

- `card434: client t=... spawn bunch gave <class> guid 0x.. at (...)` (NPC classes, 64 rows a map).
- `card434: client t=... a spawn bunch made no actor` (16 rows; not a WARN: an actor in an unloaded sublevel reads the same).
- `card434: client t=... channel of guid 0x.. closed N ms after its spawn, reason N, for destroy N` (64 rows).
- `card434: client t=... Anim miss guid 0x..: object <class | none>; <spawn table state>` (48 rows).
- `card434: client NPC spawn bunches (map totals): ...` at each map arm.

Readings of the miss row:

- `no spawn bunch seen for it`: the client never processed a spawn bunch (not received, or queued on a pending guid
  or an unloaded asset). Next read: `UActorChannel::ProcessQueuedBunches` and the channel's queued bunch count.
- `spawned ..., channel closed ...`: the client closed its channel while the host keeps one. The reason value names
  the path (3 relevancy, 1 dormancy, 0 destroyed).
- `spawned ..., channel open` with `object none`: the guid on the Anim row is not the guid the client has for the
  actor. Fault is in the guid the host sends.
- `object <non-NPC class>`: the guid maps to another object. Fault is in the lookup or the class check.

Done when a run on C5M2 prints miss rows and each of the unresolved guids falls in one reading.

## Step 4 result (2026-11-16, run c399b137a, b137): the client's spawn bunch makes no actor

11 C5M2 guids (0x7f8 0x828 0x832 0x83c 0x846 0x85a 0x86e 0x878 0x88c 0x896 0x8aa, all dynamic; classes BP_AI_WarriorBug_C,
BP_AI_Trooper_Morita_C and BP_AI_Trooper_Shotgun_C (corrected 2026-11-16 from run c423b138a),
spawned 4.5 to 7 s after the map load) each have a client row `card434: client ... a spawn bunch made no actor
(channel ..., result 0)` within 3 ms of the host's spawn time. 4 other C5M2 spawn bunches (troopers) gave an actor.
On the other maps of the run every spawn bunch gave an actor (15, 12 and 20). So the NPC never exists on the client:
`UPackageMapClient::SerializeNewActor` returns false with a null actor, and every later row by that guid misses.

Next step (b139, diagnostic): a detour of `UPackageMapClient::SerializeObject` (RVA 0x03EB8D30) that is live only
inside `detour_c434_new_actor` remembers each inner call (class asked, result, out guid, object or null) and the
`made no actor` row prints them, at most 16 bunches. Reading rules: the archetype call gives a null object, the
client did not load that blueprint class; the actor call is null with no later call, the archive errored or the
guid is already broken; all calls good and no actor, the spawn itself failed. The fix follows from which one it is
and must cover every NPC class on every map (a class not loaded on the client at spawn time is a class of bug, not
a C5M2 case).

## Relevancy case: cause and fix (2026-11-16 15:19, run c423b139a, b139 `0512644b5a6033e3`, FED_C2M1; senior)

Separate from the C5M2 "spawn bunch made no actor" case above, which this run did not reach.

All 91 client `Anim miss` rows read `object none; spawned ..., channel closed N ms ago, reason 3` (relevancy;
N from 33 ms to 128.8 s, median 3.0 s). The client's channel for the NPC closed about 5 s after the NPC went past
the 15000 uu net cull distance from the client's view point (128 close rows, 127 with reason 3), the client destroyed
its copy, and the host kept sending Anim rows by NetGUID. The host's `channel` field now discriminates: guid 0x3ce
read `channel 1 relevant 0` for 3 s, then `channel 0` 182 ms after the client's close. Map totals: 4134 Anim rows
sent, 132 unmatched on the client (3.2 %).

Fix (`c434f_patch.py`, scratchpad, 3 edits in `hooks_internal.h`, `world_c98.cpp`, `enemies.cpp`):
`c41_send_row` sends no by-guid Anim row to a remote PC whose connection has no actor channel for the NPC
(`c434_has_channel`, `UNetConnection::FindActorChannelRef`). By-cell rows (NPCs the game does not replicate) are
unchanged. Row: `card434: host Anim for <class> guid 0x.. not sent to N PCs with no actor channel for it`.

Done when, on a FED_C2M1 run with the players apart: the host prints the `not sent` row, client `Anim miss` rows
with `channel closed` more than 1 s ago are 0, and Anim `played` on the client does not drop for NPCs near it.
A miss within a second of the close is the row and the close bunch crossing and is not a fault.

Not fixed by this, open: an NPC over 15000 uu from the client does not exist on the client while the host sees
it (engine relevancy). Raising `NetCullDistanceSquared` for NPC classes would close it at a byte cost when the
players are apart; the Fx rows for such NPCs already play at the world point (card 325).

## C5M2 case: cause and fix (2026-11-16 15:26, run c399b139a, b139; static trace of the exe)

Finding: 11 spawn bunches on FED_C5M2 made no actor on the client (b137 also 11; earlier maps of the chain 0). In
each, the archetype guid (static: 0xcf BP_AI_WarriorBug_C, 0xcb BP_AI_Trooper_Shotgun_C, 0x1ef
BP_AI_Trooper_Morita_C) did not resolve on the client.

Cause (exe disassembly): `UNetDriver::CleanPackageMaps` (exe+0x3E610B0) runs on each seamless travel. It calls
`FNetGUIDCache::CleanReferences` (exe+0x3E9CE40), which marks every entry whose object is gone read-only and removes
it at the first clean 90 s later, static guids included. It resets each connection's export ack state
(`UPackageMapClient::ResetAckState`, its only caller) only when `net.ResetAckStatePostSeamlessTravel` is nonzero;
the engine default is 0. So a class the client unloaded on an earlier map loses its cache entry there, while the host
still holds that guid's path as acked and sends the bare guid. Cards 346/369 made every travel seamless, which is why
this shows on the third map of a chain and not after a hard travel (new connection, new ack state).

Fix (`c434h_patch.py`, senior scratchpad, 3 edits: `tools/symbols.txt`, `restart.cpp`): `c434_reset_ack_on` sets
the cvar's int to 1 in `c346_host_seamless` before `ServerTravel`. The address is read from the compare at
CleanPackageMaps +0x1A with a byte check (WARN and no write when the bytes differ). Cost: each guid's path is sent
once more per map per client.

Diagnostic (`c434g_patch.py`, 7 edits): after a no-actor bunch, the client's guid cache entry of each unresolved guid
and its outers (`card434: client ... guid cache 0x<guid> (level N): ...` or `no entry`).

Done when: a chain run to FED_C5M2 on a build with both shows the host row
`card434: host ... net.ResetAckStatePostSeamlessTravel 0 -> 1`, 0 `a spawn bunch made no actor` rows on the client
(11 before), and no WARN from the byte check. If bunches still fail, the `guid cache` rows say whether the entry is
absent (ack reset did not run: find the travel path that skips CleanPackageMaps) or present with a null object (the
class does not load on the client: load it by path).

## Closed (2026-11-16 21:39, senior, from logs already on disk)

- C5M2 case (done-when above): chain run `c399b141a` has the host row `net.ResetAckStatePostSeamlessTravel 0 -> 1`,
  0 `WARN card434`, 0 client `a spawn bunch made no actor` rows (11 in `c399b139a`); client Anim unmatched 0 on all
  three maps.
- Relevancy case: run `c423b163a` (FED_C2M1) has 9 host `card434: host Anim for ... not sent to 1 PCs with no actor
  channel` rows (64 rows skipped), 0 client `Anim miss` rows, client Anim totals received 601, played 600,
  unmatched 0.
- Left as written under "Not fixed by this": an NPC over 15000 uu from the client does not exist on the client
  (engine relevancy). No fault seen from it; a card only if a playtest shows one.
