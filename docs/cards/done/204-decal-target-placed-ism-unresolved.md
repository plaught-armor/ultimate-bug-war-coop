# Card 204: A decal on a placed instanced mesh finds its target on the client

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: queued 2026-11-12, split from card 142 (its trace, item 4). Budget: three runs. Static first.

Read: `docs/cards/done/142-decal-join-replay-pacing.md` ("Trace", item 4), `mod/src/ubw/decal_c132.cpp`
(`host_target`, `client_target`, the T row).

## Why

The host sends a T row for a decal target that is an instanced static mesh component of a plain placed Actor
("Actor_3" at (0, 0, 0), attach not world). The client cannot resolve it, so every decal on that target is dropped
with a WARN. The player on the client sees no blood or scorch on those meshes; the host does.

## Do

1. Trace, no run: how the host names such a target (`c95_wire_target`), why the client lookup fails (name not stable,
   level not loaded, component name not unique), and which other target classes share the cause. Mark each line
   verified or open.
2. Fix in the target naming or lookup, for the class.
3. One run: decals on such a mesh, host count equals client applied count, 0 "no target".

## Done when

- The trace names the cause; the run shows the decals applied on the client. The Report ends with "Report complete."

## Trace (junior, 2026-11-12)

Static only: no code, no run. Each line is marked verified (with file:line or a log line) or open. Line numbers are
in mod/src/ubw/decal_c132.cpp unless another file is named. Read together with card 206's trace (same resolver).

**What the host sends**

- Verified: target 4 is "decal target 4 InstancedStaticMeshComponent of Actor_3 0.0 0.0 0.0: owner class Actor,
  placed 1, mobility 0" (c156-r2 host:8742). It was registered by fx 3908, a restore decal during the in-place load
  (attach 0).
- Verified: the live rows on that ISM are attach 1 and go out as world rows ("Actor" is in kWorldGeometryClasses;
  c156-r2 host gore trace: 7 attach-1 rows on "InstancedStaticMeshComponent of Actor").
  - Only the attach-0 restore rows reach the placed path, because host_world_row returns at `attach !=
    kAttachWorld`.
  - So the loss on the client is limited to the decals restored after a checkpoint load.
- Verified: the ISM's world transform is the identity.
  - The live row "at (12074.8, 13847.0, -17.0) rot (234.8, 90.0, 74.0) ... attach 1" (c156-r2 host:5877) and the
    restored row "at (12074.8, 13847.0, -17.0) rot (-54.8, -90.0, -106.0)" (fx 3908) are the same pose.
  - (-54.8, -90, -106) is (180 - 234.8, 90 + 180, 74 + 180), the same rotation.

**Why the client does not resolve it**

- Verified: client_target (:558) calls c95_find_nearest("Actor_3", (0, 0, 0)), then c95_component(actor,
  "InstancedStaticMeshComponent").
  - On a null it logs "WARN card132: client decal target 4 ... not found" (c156-r2 client:7018).
  - `d.tried` stops every retry.
  - The WARN does not say which of the two steps failed.
- Verified: no "card133: name fallback" line in the c156-r2 client log. Either the exact name "Actor_3" was found
  (the fallback is not called and the component lookup failed), or no placed actor whose class name is in "Actor_3"
  is within 200 uu of the origin.
- Verified: the component text drops its FName number (`object_name`, mod/src/ue/reflect.cpp:117-146; card 206
  trace). A merged-geometry Actor holds several ISMs, which differ only by number. The client's StaticFindObject on
  the bare name finds only the number-0 one.
  - The hb50 comment at :318-321 says the client resolved none of the Actor_N ISMs (targets 2, 5, 7). That fits a
    numbered-name cause, more than one Actor_3 that is missing on the client.
- Open: which step fails.
- The one line that names it: the client not-found WARN split into "actor <c50_name class | none>, component <name>
  found 0/1".
- The host side of the same line: the T line with c50_name(component).

**Other target classes with the same cause**

- Every placed owner whose component name carries a number suffix (cause verified, instances open):
  - Landscape tiles (card 206);
  - foliage ISMs (FoliageInstancedStaticMeshComponent, hb40);
  - any Blueprint actor with numbered components.
- StaticMeshComponent0 (StaticMeshActor) has no suffix, which is why those targets resolve (r4: "attached to placed
  ... StaticMeshActor_488").

## Proposal for cards 204 and 206 (one build, one run)

All in mod/src/ubw/decal_c132.cpp, except one small helper.

1. `host_world_row`: owner null gives a world row. An NPC owner gives a target row. Then:
   - a placed, static owner with no health component and a class in kWorldGeometryClasses gives a world row for
     every attach mode, attach 0 included;
   - the not-placed and movable rules stay for attach 1;
   - no guid return for non-NPC owners.
   - This covers the Landscape (206) and Actor_N ISMs (204) with no client lookup at all.
2. `c132_decal_spawned`: a world row takes r.loc and r.rot from the spawned decal's ComponentToWorld (translation and
   quat), not from the spawn arguments, so attach 0 converts exactly; attach 1 gives the same values.
   - New helper next to component_yaw (hooks_ubw.cpp:351): quat to rotator, pitch, yaw and roll, with
     FQuat::Rotator's formula.
   - One capped host line: "card132: host decal N world row from attach A on <comp> of <owner>: (x, y, z) rot (p, y,
     r), scale (sx, sy, sz)". The scale must read 1, 1, 1 (card 206 item 1, open).
3. `host_target_text`: '@guid' only for NPC owners; placed owners take c95_wire_target; the component text is
   c50_name(component).
4. `host_target`: dedup by wire identity (component text plus where), with no game pointer kept (card 206 item 3).
   - One line per new target, '@' included: "card132: host decal target N <comp> of <where>: owner class <c> name
     <n>, placed p, mobility m, guid 0x..".
5. `client_target`: the not-found WARN names the failing step: actor found, with class, or none; component found or
   not.

### Predictions for one run on tools/scenarios/c142-r4.txt

- Host:
  - The restore decals 29-39 on Landscape tiles print "world row from attach 0 on
    LandscapeHeightfieldCollisionComponent_N of Landscape ..." with scale 1, 1, 1.
  - The StaticMeshActor rows still print "decal target N StaticMeshComponent0 of StaticMeshActor_488 ...".
  - No target line names the Landscape, and no '@' target has owner class Landscape.
  - Any '@' target line names an NPC class (WarriorBug or another bug).
  - The live attach-1 Landscape rows count in the host's world-space total instead of going out as '@c5'.
- Client:
  - Every client decal round after the rejoin shows host live = client live.
  - The held line reads "failed 0", and held = late + dropped + released + failed + pending.
  - No "not spawned" WARN, no "target ... not found" WARN, no "card142 ... held then not placed" WARN.
  - The world-position fallback count drops by the Landscape attach-1 rows, which now arrive as world rows.
- Card 204 in this route: c142-r4 has no restore decal on an Actor_N ISM (r4 host: the restore set is Landscape and
  StaticMeshActor only). The 204 proof needs a route whose save is made after gore on the ISM field, as c156-r2 had
  (fx 3908, 3910). The c156-up scenario is the candidate.
- Card 142 late apply: the r4 route held only Landscape rows. If no NPC '@' row is held after the rejoin, I will say
  so; card 194's run remains the carrier.

Report complete.

## Code and route (junior, 2026-11-12; build 862ff8865d50909a)

The joint code is in (card 206, "Code"): an Actor_N instanced-mesh owner is in kWorldGeometryClasses, so its decals
go out as world rows for every attach mode, posed from the decal's ComponentToWorld; no client lookup remains.

Route: tools/scenarios/c142-r4.txt has no restore decal on an Actor_N mesh (c142-r4 host: the restore set is
Landscape and StaticMeshActor_488 only). Card 204 stays open as "code in, not proven"; no run is added for it. If
c206-r1 happens to log a "card206: host decal N world row ... owner Actor Actor_N" line, it is reported as a side
observation, not as the proof.

Side observation from c206-r1 (not the proof): host decal 2 went out as "world row from attach 1: owner Actor
Actor_3, component InstancedStaticMeshComponent_0", world scale 1, 1, 1, and the client matched the host's live
count in every round after the rejoin. It was a live attach-1 row, not a restore decal, so card 204's restore
case stays "code in, not proven".

## Senior review (2026-11-12)

Closed with card 206 (same code, DLL `862ff8865d50909a`, c206-r1). The route does hold a restore decal on an
Actor's InstancedStaticMeshComponent: host gore trace 28 at the in-place load, attach 0, at (12730, 13666, 116).
It went out as a world row, and the client held 17 of 17 after the rejoin with no "not found" WARN. Its pose has
no log line (card 206's carried item).
