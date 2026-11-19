# Card 481: C4M2 MoveableRoad differs between host and client in the digest

Opened 2026-11-17 18:21 from runs c245b194b, c245b194c and c245b194d (b194 `a119f90574be9a2d`, FED_C4M2). This is a sync gap (full-world-sync rule), not a crash.

## Seen
- `BP_DestroyableActor_C4M2_MoveableRoad_C` mismatches: 2 to 3 rounds in 9 in c245b194d, 15 in c245b194b, 13 in c245b194c.
- Card 423 (closed) added destroyable actor sync and lists C4M2_MoveableRoad (2) among its classes. Its static note says MoveableRoad has 8 exports and 0 script functions: component template overrides only.

## Do
1. From the digest rows, find which field differs (location, rotation, destroyed state, visibility, a part's transform), when it starts, and whether it heals.
2. Trace what moves the road on the host (level script, a timeline, physics) and why the card 423 path does not carry it. Fix it at the class level for every destroyable actor with the same mover.

## Done when
A C4M2 run past the moment the road moves shows MoveableRoad with 0 mismatches in every digest round.

## Step 1 (junior, from c245b194b/c/d logs, no new run, 2026-11-17)
- Field: location only. Host StaticMeshActor_32 (18500,-18000,3950), client (18600,-17950,-1250); host StaticMeshActor_14 (-12950,-36300,3700), client (-12950,-36300,-850).
- Start: digest round 15 (599414157) matched, round 16 (599419223) is the first mismatch. Never heals: every later round to the map end, in all of b, c and d.
- Mover: the level script FED_C4M2_Scripting at the Brain Bug arrival (BP_PlayerPresence_C_176 OnTriggerEntered at 599416516, the BrainBug_SO1 completion branch) calls `K2_SetActorLocation` on both roads (umap read with tools/iostore/iox.py).
- Why card 423 missed it: MoveableRoad is a card 423 ext actor (row 10, `BP_DestroyableActor_Base_C`). The ext sweep in ext_c132.cpp sent ext actors to `ext_host_actor` only and skipped `c140_placed_visit`, so the card 140 'A' row (placed-actor move poll, client `c140_client_placed`) was never sent for any ext actor. Only the hazard (row 1) and the large elevator (row 8) own their motion through ext.

## Fix b196 `d42815991b07ad70`
ext_c132.cpp: `ext_owns_motion(row)` (rows 1 and 8). Every other ext actor also goes through `c140_placed_visit`, in the candidate pass and the sweep (visit kinds kVisitPlaced / kVisitExt; an ext actor is never added as a second placed candidate). Class-wide: every destroyable and other ext row now gets its moves sent. Proof: the C4M2 run shows an 'A' row for both roads after BrainBug_SO1, and MoveableRoad 0 mismatches.

## Closed (2026-11-17 18:43)

Cause: ext actors (DestroyableActor rows) skipped c140_placed_visit, so the host never sent the 'A' move row for a placed actor that moved. Fixed in b196 (d42815991b07ad70): non-owning ext actors get the placed visit. Evidence, c245b196a host: "card140: host placed class BP_DestroyableActor_C4M2_MoveableRoad_C: first move row StaticMeshActor_14|-12926,-36280,-829 ... at (-12926, -36280, 3718)"; 0 MoveableRoad / StaticMeshActor_14 / StaticMeshActor_32 mismatches over all 22 rounds.

Not done: StaticMeshActor_32 never moved in this run, so its move row is unproven.

Reopen when: any BP_DestroyableActor_* digest mismatch, or StaticMeshActor_32 mismatches after it moves.
