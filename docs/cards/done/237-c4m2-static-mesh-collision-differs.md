# 237 — FED_C4M2: StaticMeshActor_52 has collision off on the host and on on the client

Soak c234-c4m2 (DLL `20cf859dd191a8fe`): every digest round, from round 1 on,
`WARN card132: mismatch StaticMeshActor StaticMeshActor_52 collision host 0 client 1; no row sent`. Round 1 also
counts 26556 actors on the host and 26554 on the client. If the mesh stands where players walk, the client is
blocked by something the host passes through (rubber-band, shots stop). Where it stands is not logged, so the
weight of this fault is unknown. First mismatch of any soak; treated as a play blocker until placed.

Senior trace 2026-11-12:
- The digest bit is the actor flag (`digest_c132.cpp:287`, `kActorCollisionFlags & kActorEnableCollisionBit`).
- The host journal (`c95_relay_collision`, `door_c95.cpp:764`) drops a write when it is no change, the owner is
  not placed or is replicated, the significance update made it (totals: 0 kept local), or the owner is still in
  its constructor (totals: 0). The host log has no line that names `StaticMeshActor_52`, and the journal was not
  full (109 of 256, 0 dropped).
- So one of: (a) the host's flag is written with no `SetActorEnableCollision` call (a sequence or blueprint
  property write, or the saved value in the map is 0 and the client turns it on); (b) the write happens on the
  host before `net_mode` reads listen server (map load before Listen), so the first test returns early and
  nothing is journalled; (c) the client's copy is another actor with the same name in another streamed level
  (the 2-actor count gap).

Step 1, diagnostic only, rides in the card 235 build: the client's mismatch WARN adds the copy's level, location,
static mesh path, hidden bit and root component collision; the host logs the same for a key the client asks
detail on. One run on `tools/scenarios/c234-c4m2.txt` (stop after round 2). Step 2: fix by the logged cause; for
(b) the journal takes map-load writes by a sweep of placed actors' flags against the class default at the first
join replay, not by hook.

Done when: FED_C4M2 digest rounds show 0 mismatches and the report says what the mesh is and where.

## Step 1 result (senior read-out, run c237-c4m2, DLL `d561aa4a8628ec3f`)

The mismatching actor is not on a walk path and has no physical effect:

- Host and client both hold two actors named `StaticMeshActor_52`, one per streamed level, so they share one
  digest key (`c6267a6f`). The digest's duplicate-key count reads 0 for the map, which is a second fault: two
  actors with one key are compared as one.
- `FED_C4M2_Art`, at (-10568, 15510, 2623), mesh `SM_FederationEagleBanner_01_ZegemaBeach`: hidden 0, actor
  collision 1, root collision 3 on both sides. Equal.
- `FED_C4M2_Scripting`, at (17254, -42505, 4998), mesh `SM_LevelHardBoundarySphere_01` (the hidden sky boundary
  sphere): hidden 1 and root component collision 0 (NoCollision) on both sides; the actor's enable-collision bit
  is 0 on the host and 1 on the client. With the root component at NoCollision the bit changes nothing a player
  can touch or see.

Hypothesis (c) holds for the name, (a) or (b) for the bit: something on the host clears the actor bit and no row
is sent. Not a play blocker. Step 2 after the playtest: find the host write (static trace of the Scripting
level's script for `SetActorEnableCollision`), relay or journal it, and make the digest key level-qualified so
same-named actors in two levels are compared apart and counted as duplicates.

## Step 1 result (junior, 2026-11-12, DLL d561aa4a8628ec3f)

Diagnostic in `mod/src/ubw/digest_c132.cpp:1059` onwards: c132_mismatch notes each non-presence key
(`c237_note_key`); at the end of c132_compare the client walks its level actors for those keys
(`c237_walk`, `c237_detail_line`), logs one line per actor and sends `X<round> <keys>` on DigestQuery; the host
walks its own copy and logs the same lines (`c237_host_detail`). New scenario tools/scenarios/c237-c4m2.txt ends after
digest round 2 plus the two detail waits.

Run c237-c4m2: both sides logged the detail in all 5 rounds. Key c6267a6f matches TWO actors named
StaticMeshActor_52 in different sublevels:

- host: `StaticMeshActor StaticMeshActor_52 level FED_C4M2_Scripting at (17254, -42505, 4998) mesh
  /Game/Art/Environments/Organic/General/Sky/SM_LevelHardBoundarySphere_01.SM_LevelHardBoundarySphere_01 hidden 1
  actor collision 0 root StaticMeshComponent0 (StaticMeshComponent) collision 0`
- client: the same actor with `actor collision 1`.
- Both sides: `StaticMeshActor_52 level FED_C4M2_Art at (-10568, 15510, 2623) mesh
  .../SM_FederationEagleBanner_01_ZegemaBeach hidden 0 actor collision 1 ... collision 3`, equal.

The mismatch (`WARN card132: mismatch StaticMeshActor StaticMeshActor_52 collision host 0 client 1`) is the hidden
level hard-boundary sphere in FED_C4M2_Scripting: the host has actor collision off, the client on; its root
component collision is 0 on both. Hidden and with no component collision, so nothing a player sees or touches.
The digest key is the name only, so the two same-named actors in two sublevels share a key.
No fix in this build.

## Step 2 static trace (senior, 2026-11-13; `kis.py` on `FED_C4M2_Scripting.umap`, logs c237-c4m2)

- The sphere is the Brain Bug arena shield, not only a sky boundary. Level script function `Toggle Brain Bug
  Boundary` (ubergraph 26913) is its only writer: with `BugArenaShieldEnabled` it calls `SetActorHiddenInGame(false)`,
  `SetActorEnableCollision(true)` and `StaticMeshComponent.SetCollisionProfileName(BlockAll)`; without it, hidden
  true, collision false, profile `NoCollision`.
- Call sites: `ReceiveLevelStart` (2506, flag false), the checkpoint load chain (21178), the fight start (15545,
  flag true; entry is `BP_PlayerPresence_C_176` `VolumeTriggerDelegate`, block 15401, which also completes
  `BrainBug_SO1` and spawns the Brain Bug), and the Brain Bug intro (20489, flag false).
- So the earlier "no physical effect" holds only before the fight. In the fight the host's sphere blocks. The
  client's sphere blocks only if the client's own script runs block 15401 (its own presence event, as in card 258)
  or the three writes are relayed. `SetCollisionProfileName` is not hooked; the actor flag is journalled
  (`c95_relay_collision`); the hidden flag goes through `detour_c98_hidden`.
- Which side misses the level-start write is not decided by the old logs, and cannot be: `c95_note_collision`
  logs blocker-like owners only (`door_c95.cpp:391`), and the client's `card137` change rows end at their cap
  inside that frame (3 of 4 `BlockingVolume` rows shown). The host journal has 5 rows from caller exe+0x3802287
  (4 `BlockingVolume` and one more), which fits a journalled sphere row.
- Weight: play-relevant for the FED_C4M2 boss fight (a player on one machine could leave or enter the arena
  through a shield the other machine has up). Moves up with card 245 (C4M2 soak).

## Step 2 spec (diagnostic first, one run, batched with card 245)

- `door_c95.cpp`: `c95_note_collision` also logs a placed owner that is not blocker-like when the write comes from
  a Blueprint node (`node != nullptr`), under its own cap of 40 lines. No behaviour change.
- The card 237 detail line gains the root component's collision profile name.
- Digest key: level-qualified (`<level>:<name>`) so the two `StaticMeshActor_52` are compared apart; duplicate
  count then reads 0 honestly.
- Scenario: c245 route plus one detail query after `BrainBug_SO1` completes on the host.
- Predictions: (1) level start: host row `SetActorEnableCollision on StaticMeshActor_52 ... 1 -> 0, blueprint
  FED_C4M2_Scripting_C.ExecuteUbergraph_FED_C4M2_Scripting`, and no such row on the client (the client's
  `ReceiveLevelStart` runs before its level script reference resolves, or not at all); (2) after `BrainBug_SO1`:
  host detail `hidden 0 actor collision 1 root collision 3`. If the client's detail differs in any of the three,
  the fix is by class: relay `SetCollisionProfileName` on placed actors beside the existing collision journal
  and replay the journal row for the actor flag at join. If the client's detail is equal, only the level-start
  flag differs and the fix is the join replay alone.

## Closed (2026-11-17 12:32)

b184 run c237b184a (FED_C4M2): 5 client digest rounds, 0 mismatches, actors 26557/26557; the round 1 mismatch `StaticMeshActor_52 collision host 0 client 1` of the c234-c4m2 soak no longer occurs. The mesh is the brain bug arena boundary (`SM_LevelHardBoundarySphere_01`, `StaticMeshActor_52` in `FED_C4M2_Scripting`, at (17254, -42505, 4998)). The level script's `Toggle Brain Bug Boundary` (run twice by the test-only `levelcall` command) writes only SetActorHiddenInGame; card 098 relays it (`card098: host hide StaticMeshActor ... placed 1 replicates 0`) and the host and client shot pairs agree. Since b183 the collision profile setter is in the same journal and relay as the other two collision writes, with run-time, construction-script and transient-name components left out (b184, 0 `WARN card095` on C5M1 and C1M1). Not proven by a run: a collision write on the boundary during the boss stage (no route reaches it; card 245). Reopen on a `StaticMeshActor_52` digest mismatch or a player blocked at the arena edge where the other is not.

Correction (2026-11-17 12:36): `Toggle Brain Bug Boundary` has two branches on `BugArenaShieldEnabled`. Off (ubergraph 21399):
hide, actor collision off, profile NoCollision. On (26927): show, actor collision on, profile BlockAll. Both
`levelcall` toggles in c237b184a ran with the flag false, so only the off branch was exercised, and it is the branch
that runs at map start (the original host 0 client 1 mismatch). The on branch runs when a player enters
`BP_PlayerPresence_C_176`; the card 245 route checks the digest after that step.

