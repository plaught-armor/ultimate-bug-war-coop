# Card 206: A decal on a static actor that has a guid (the Landscape) goes by the placed path, not '@guid'

Status: queued 2026-11-12, split from card 142 (c142-r4). Budget: three runs. Static first. Batch with card 204
(same resolver, same file) in one build and one run.

Read: `docs/cards/142-decal-join-replay-pacing.md` "Trace 3 (after c142-r4)", `mod/src/ubw/decal_c132.cpp`
(`host_target_text` :303, `host_world_row` :323-334, target dedup :360, `client_target`), card 204.

## Why

c142-r4: after a rejoin the client had 4 of the host's 11 decals. The 7 lost rows (attach 0, no world fallback)
sit on two LandscapeHeightfieldCollisionComponent tiles of the Landscape, guid 0xc5. `host_target_text` writes
'@guid' for any owner with a guid, and `host_world_row` returns before its world geometry list for any owner with
a guid. On the client `c41_resolve_guid` passes only NPCs, so a Landscape target never resolves. The same path
explains the nulls in c156-r1/r2 and the '@bf' rows in c142-185-r1.

Latent, same code: `g_dh.targets[].component` is a raw game pointer kept across frames and across the in-place
checkpoint load (cleared only at LoadMap enter). The mod rules forbid that.

## Do

1. Trace, no run: how an attach-0 row on a Landscape tile resolves by the placed path on the client (owner by
   name and location, component by name; are the tile component names equal on both sides), or whether the host
   must send it as a world-space row. Card 204 uses the same resolver: trace both.
2. Host: '@guid' only for NPC owners; a static owner with a guid takes the placed or geometry path. One log line
   per new target: index, component, owner class and name, guid.
3. Host target table: no game pointer kept across a load; clear it at the in-place load too, or key it by the
   wire identity.
4. One run on `tools/scenarios/c142-r4.txt`.

## Done when

- The run shows host live = client live in every decal round after the rejoin, held invariant
  held = late + dropped + released + failed + pending with failed 0, no "not spawned" WARN, and the new host target
  lines name the Landscape tiles. Card 142's late apply is proven here if a row is held on an NPC guid; if none
  is, say so. The Report ends with "Report complete."

## Trace (junior, 2026-11-12)

Static only: no code, no run. Each line is marked verified (with file:line or a log line) or open. Line numbers are
in mod/src/ubw/decal_c132.cpp after the card 142 held_failed change, unless another file is named.

### Item 1: can an attach-0 Landscape row resolve by the placed path?

**Which check blocks the attach-0 rows**

- Verified: host_world_row returns false at its first check, `attach != kAttachWorld` (:330). The guid return
  (:337) is never reached for an attach-0 row.
  - So an attach-0 row on any owner always becomes a target row, and the geometry list never applies to it.
  - The guid return matters only for attach-1 rows. In r4 those went out as '@c5', and the client fell back to the
    world position (client map totals at 115694269: "23 world-position fallbacks ... held 0").
  - This corrects Trace 3, which named the guid return as the block for the restore rows.

**The owner (c95_find_nearest)**

- Verified: the Landscape is a level-loaded actor. Host fx 3967-3977 lines say "owner Landscape (Landscape) ... key
  placed".
- c95_wire_target (door_c95.cpp:479) would send "<c50_name(Landscape)> x y z".
- c95_find_nearest (door_c95.cpp:501) matches placed actors by full c50_name (the FName with its number suffix,
  hooks_ubw.cpp:1213) nearest that location.
- Open: whether the Landscape's name is the same on both sides. Inference: yes, because it is serialized in the level
  package.
- The line that proves it: the host's new target line (item 2), plus the client "attached to placed" line.

**The component (c95_component)**

- Verified: the T row's component text is `object_name(component)` (host_target_text, :309). object_name reads only
  the FName's name entry and drops its number (mod/src/ue/reflect.cpp:117-146). c50_name keeps the number.
  - Every tile of one Landscape therefore goes out as the same text, "LandscapeHeightfieldCollisionComponent".
  - Targets 0 and 1 in r4 are two different tiles with that same text: host_target dedups by pointer (:364).
- Verified: c95_component (door_c95.cpp:535) looks the name up with StaticFindObject under the actor. A name with no
  number suffix finds only the subobject whose FName number is 0.
  - So by the placed path as it is, the client would resolve every Landscape row to one tile, or to none.
  - This is the case even when the owner resolves.
- Open: whether the tile names (with number) are the same on host and client. Inference: yes. The collision
  components are serialized subobjects of the level's Landscape, so both sides load the same names from the same
  package.
- The line that proves it: the host T line with the full c50_name, and the client "attached to placed" line naming
  the same tile.

**The relative transform of an attach-0 row**

- Verified: attach 0 is KeepRelativeOffset. r.loc and r.rot are relative to the attach component.
  - The restore rows carry tile-local values, for example host fx 3904 in c156-r2 at (2.1, 17.6, -0.7), and in r4
    (124.7, 15.5, -0.5).
  - The client calls the same spawn with the same attach (client_spawn :723), so the copy lands right if the
    client's tile has the host's transform.
- Open: the tile transform on the client. Inference: equal, because the tile is static, placed, and comes from the
  same package.
- Open: decal scale. As far as I recall, UE's SpawnDecalAttached sets absolute scale on the decal, so the tile's
  scale moves the location but does not size the decal. This is not verified in this binary.

**Conclusion for item 1**

- The placed path can work for Landscape tiles only if the T row carries the full component name (c50_name).
- It still depends on two names that are equal only by inference.
- The simpler path is a world-space row, which needs no lookup. The design comment at :312-321 already says that
  geometry which never dies should travel this way.
  - For an attach-0 row the host needs the world transform. It can read that from the decal it just spawned: the
    `decal` argument of c132_decal_spawned is the attached UDecalComponent. Its ComponentToWorld holds the rotation
    quat at kComponentRotation (mod/src/ue/actor.cpp:58) and the translation (hit_c132.cpp:51, +0x200).
  - So r.loc and r.rot become the decal's world location and rotation, and r.attach becomes kAttachAt.
  - Open: a full quat-to-rotator helper. Only yaw exists (component_yaw, hooks_ubw.cpp:351).
  - Open: the decal's world scale (+0x220) for one attach-0 row. It must be (1, 1, 1); otherwise the size must be
    scaled too.

### Item 2: change points

**host_world_row (:329)**

- Verified: today the order is attach (:330), owner null (:334), guid (:337), not placed (:344), movable or health
  component (:347), geometry list (:353).
- Change:
  - Keep the null-owner check first.
  - Return false (a target row) for NPC owners only (`is_a(owner, g_npc_class())`), not for any owner that has a
    guid.
  - Then apply the geometry list (placed, not movable, no health component, class in kWorldGeometryClasses) for every
    attach mode.
  - The remaining rules (not placed, movable, health component) keep applying to attach 1 only, as now.

**host_target_text (:306)**

- Change:
  - '@guid' only when the owner is an NPC.
  - A placed owner takes c95_wire_target.
  - A spawned non-NPC owner that has a guid keeps '@guid' as now. On the client that gives the world fallback for
    attach 1 and "no target" for attach 0.
- Change: the T row's component text becomes c50_name(component), with the number. StaticMeshComponent0 and other
  names with no suffix are unchanged.

**Owners with a guid, other than NPCs and the Landscape, that reach this code**

- Verified: 0xbf, a static guid of a placed level actor under the start's gore (card 142, "c142-185-r1"). Its rows
  were held forever in r1, then took the world fallback after the package-map check.
- Open: its class. The host does not log '@' targets (:382).
- Open: the gore trace samples attach-1 rows on "Box of BP_KillableBuilding_Base_TacSuppRangeTarget_C" (c142-r4
  host, 9 rows), but those are bullet decals and are not relayed. Whether a gore decal on a killable building goes
  out as '@guid' (building with a guid) or as a placed target is not in the logs.
- After the change, every placed owner (StaticMeshActor, killable building, crate) takes the placed path whether or
  not it has a guid. That path resolves without any package-map state and already works for StaticMeshActor_488
  (r4 host: "decal target 2 StaticMeshComponent0 of StaticMeshActor_488 ...").

### Item 3: the target table at the in-place load

- Verified: the game's decal restore runs inside the load call. Host: "save engine load runs" at 115687993, restore
  decals 29-39 spawned at 115690570, "load done" at 115690575.
  - Both lines are in detour_c121_load (save_c121.cpp:113-126), around g_orig_c121_load.
  - So a clear at "load done" would drop the targets that the restore rows have just registered, while those rows
    are live on both sides.
  - If the table is cleared, the point is detour_c121_load, host branch, just before g_orig_c121_load
    (save_c121.cpp:121).
- Proposed instead: key host targets by their wire identity, not by pointer.
  - host_target computes the text (component c50_name plus where), then dedups by string compare.
  - The host DecalTarget keeps no `component` pointer, so nothing survives a frame and nothing needs clearing at any
    load.
  - The index stays stable for the same identity across the load, which matches the client's table.
  - Cost: one host_target_text per relayed gore decal, bounded by kDecalTargets on the new entries.

## Code (junior, 2026-11-12; build 862ff8865d50909a)

Files: `mod/src/ubw/decal_c132.cpp`, `mod/src/ubw/hooks_ubw.cpp` (new `component_rotator` after `component_yaw`),
`mod/src/ubw/hooks_internal.h` (its declaration). /W4 /WX clean.

1. `host_world_row`: an NPC owner (`host_npc`: `is_a(owner, g_npc_class())`) returns false and keeps '@guid'. Every
   attach mode except SpawnDecalAtLocation (255) goes through the owner checks, so an owner in kWorldGeometryClasses
   gives a world row for attach 0 too. A non-NPC owner's guid is no longer checked.
2. `host_world_pose`: a world row takes its location from the decal's ComponentToWorld translation and its rotation
   from `component_rotator` (FQuat::Rotator, gimbal-lock branches included). One line per row, capped at 8 per map:
   "card206: host decal N world row from attach A: owner <class> <name>, component <numbered name>, spawn arg (x, y,
   z) rot (p, y, r), world (x, y, z) rot (p, y, r), world scale (sx, sy, sz)". For an attach-1 row the spawn
   arguments are already world space, so the two poses must match; that checks the rotator against the game's own.
3. `host_target_text`: '@guid' for NPCs only (an NPC with no guid gets no target); the component text is `c50_name`
   (numbered). Every other owner takes `c95_wire_target`.
4. `host_target`: the table is keyed by component text plus where; `g_dh.targets[].component` is never set on the
   host. One line per new target, '@' targets included (kDecalTargets bounds them).
5. `client_target`: the not-found WARN reads "... not found: actor <name> <class> | none -, component found 0".
   Found 1 never reaches this WARN (the component is the only thing it reports missing).
- Also: the host's "without a target" total no longer counts world rows (they have target -1 by design).

## Run c206-r1 predictions (tools/scenarios/c142-r4.txt, run 1 of 3)

Host:
- 1 to 8 "card206: host decal N world row from attach A: owner Landscape Landscape..., component
  LandscapeHeightfieldCollisionComponent_N" lines (capped at 8; c142-r4 sent 7 attach-0 restore rows and the live
  attach-1 Landscape rows). Every one has world scale (1.000, 1.000, 1.000). Every attach-1 line has world equal to
  the spawn arguments within 0.1 uu and 0.05 degrees.
- No target line with owner class Landscape and no '@c5' target. Every '@' target line names an NPC class
  (BP_AI_WarriorBug_C or another bug). The StaticMeshActor_488 target prints as before (StaticMeshComponent0).
- Host WARN classes as c142-r4: attach, config, cmd (the extra kill). No new class.

Client:
- No "held: no target ... @c5" line. The replay after the rejoin carries the restore decals as world rows: every
  "client decal round" after it shows host live = client live (c142-r4: 11 against 4).
- The map totals before the rejoin show far fewer world-position fallbacks than c142-r4's 23 (those were attach-1
  '@c5' rows; they are now world rows). Any remainder is a non-geometry target and is named by a target WARN.
- No "not spawned" WARN, no "target ... not found" WARN, no "held then not placed" WARN. The held line reads
  "held N: pending 0, ..., failed 0" with held = late + dropped + released + failed + pending.
- Card 142's late apply is seen only if an NPC '@' row is held after the rejoin; on this route it most likely is
  not (c142-r4 held only Landscape rows). If none is held I say so.
- Client WARN classes as c142-r4 less the card132 "not spawned": attach, config, card152. No new class.

## Run c206-r1 result (junior, 2026-11-12; DLL 862ff8865d50909a)

Run: ended by the scenario after 181 s, host rc 0, client rc 0, no crash dumps, "scenario passed". Logs:
logs/c206-r1-host.log, logs/c206-r1-client.log, logs/c206-r1.drive.

Host:
- 8 "card206: host decal N world row from attach 1" lines (decals 1-8, then the cap note "card206 world row ...
  further events not logged"): 7 on Landscape Landscape_0 (LandscapeHeightfieldCollisionComponent_152 and _153),
  1 on Actor Actor_3 InstancedStaticMeshComponent_0. Every one reads world scale (1.000, 1.000, 1.000). Met.
- Location: world equals the spawn arguments to 0.1 uu on all 8 rows. Met.
- Rotation: the numbers differ, the orientation does not. The spawn arguments carry pitch beyond 90 (decal 1: 261.16,
  107.51, 123.00); `component_rotator` returns the canonical form (-81.16, -72.49, -57.00). (p, y, r) and
  (180 - p, y + 180, r + 180) are the same rotation, and all 8 rows satisfy that identity exactly. The prediction's
  "equal within 0.05 degrees" was worded too literally; the rotator is confirmed against the game's own rotation.
- Targets: one line only, "card132: host decal target 0 StaticMeshComponent0 of StaticMeshActor_488 13201.9 14668.4
  -113.5: owner class StaticMeshActor, placed 1, mobility 0". No Landscape target and no '@c5'. No '@' target at
  all in this route (no gore decal on an NPC). Met.
- Rejoin replay: "17 live decals, 1 materials, 1 targets, 2066 bytes in 2 frames, 2 sent".
- WARN classes: attach, config, cmd (the extra kill), as c142-r4. No new class.
- The host decal map totals line (with the world-space count) did not print: the host quits without a map arm,
  as in c142-r4.

Client:
- Map totals before the rejoin: "22 rows, 17 spawned, ... 0 world-position fallbacks; ... 0 without target ...
  held 0 (applied late 0, dropped 0, released while held 0, failed 0, 0 still held)". c142-r4 had 23 fallbacks. Met.
- Replay "J 19" after the rejoin, then rounds 3, 4 and 5: host live 17, client live 17 each. Met (c142-r4: 11
  against 4).
- No "held: no target" line, no "not spawned", no "target ... not found", no "held then not placed" WARN. Held line
  every round: "held 0: pending 0, applied late 0, dropped 0, released while held 0, failed 0". Met.
- Card 142's late apply: no row was held in this run, so it is not exercised here.
- WARN classes: attach, config, card152 (3 "no stream row" lines). c142-r4's card132 "not spawned" class is gone.
  No new class.

Table peaks: host targets 1 of 64, materials 1 of 16, live 17 of 512; client live 17, held 0.

## Senior review (2026-11-12)

Accepted on c206-r1, DLL `862ff8865d50909a`, one run of three. Diff read: `host_npc`, `host_world_row` for every
attach mode, `host_world_pose` (decal ComponentToWorld, `component_rotator` = FQuat::Rotator), host target table
keyed by wire text with no pointer, split not-found WARN. Checked myself: host live 17 = client live 17 in rounds
3 to 5 after the rejoin (replay "17 live decals, 1 targets"), held line all 0, client WARN classes = c142-r4 minus
"not spawned"; world scale 1,1,1 on 8 rows; spawn rot and world rot are the same rotation ((p, y, r) =
(180-p, y+180, r+180)).

Unproven, carried: the 8 "world row" lines were all attach 1, so the pose of an attach-0 restore row (host gore
trace 27 to 29 at the in-place load) has no line; it is covered only by the live counts. Follow-up in the next
build: the line cap counts attach-0 rows on their own.
