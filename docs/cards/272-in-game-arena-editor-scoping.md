# 272 In-game arena editor for CTF versus: static scoping
Status 2026-11-22: scoping, parked until after the co-op playtest.

User, 2026-11-13: "We also should figure out a way to be able to map build." Answers: an **in-game editor**,
first for **CTF versus arenas**. Not a play blocker; after the co-op playtest and after versus cards A to E
(card 268). This card is static only and ends with a design note the user decides on.

## Shape

Two layers. Layer 1 is a layout file: a text file that lists pieces (class or mesh path, position, rotation,
scale) and the mode points (two bases, two flags, pickups). The host loads it on a base map, spawns the pieces,
and sends the same list to the client, so both worlds are equal (full world sync rule). Layer 2 is the editor:
a fly camera, place, move, rotate, delete under the crosshair, save to a layout file. Versus card A's menu
picks the arena file.

No cooked maps: the game is a custom UE5 branch without an editor project, and pak mounting is parked (036).

## Known from the binary (PDB publics)

- Fly camera: `ADebugCameraController`, `UCheatManager::EnableDebugCamera`, `UCheatManager::Fly`.
- Pieces: `AStaticMeshActor::StaticClass`, `UStaticMeshComponent::SetStaticMesh`, `USceneComponent::SetMobility`,
  `AActor::K2_DestroyActor`; the mod already has `UWorld::SpawnActor` and `StaticLoadObject`.
- Pointing: `UWorld::LineTraceSingleByChannel`; the mod's crosshair trace (card 264).
- Piece art in the game: `CampKeitel/ShootingArena/SM_ArenaPiece_*`, `SM_WallLaserTag_*`, `SM_CoverTower_01`,
  sandbag walls, `MilitaryBase` crates, walls, railings, watchtower, tents, flag poles.
- UI: the mod's own panels (`lobby_ui.cpp`, card 052) for a piece list and a help line.

## Questions to answer (static)

1. Base map: an empty flat area to build on (which existing map loads fastest with its script chain held and
   has open ground; `TrainingCamp_Showcase` is art only and may serve as a clean base).
2. Spawned static meshes: collision for pawns and bullets, navigation not needed (no AI in versus), lighting
   of movable meshes, and how the client gets them (host list replayed locally, not engine replication).
3. Fly camera: does the debug camera work in the shipping build, or is a mod-made free camera on the pawn
   (collision off, gravity off) the smaller path.
4. Piece catalogue: which meshes have collision built in; the list the editor offers.
5. File format, place on disk (next to the mod's config, outside `game/` content), sharing with the friend
   (the host sends the layout at join, the client needs no file).
6. Limits: piece count cap and message size for the join replay (netcode trust model: friends only, basic
   crash-safety checks on the file and on received rows).
7. Who edits: host alone, or both players together in the editor session.

## Output

Design note here: mechanism per question, new sync class (layout rows), card split with run budgets.

## Findings so far (2026-11-13)

- Question 3: the shipping exe keeps real bodies for `UCheatManager::Fly` (0x39AE550, 0x100 bytes), `Ghost`
  (0x39B2750, 0x100), `EnableDebugCamera` (0x39AD790, 0x1A0) and `ToggleDebugCamera` (0x39C54A0). The smallest
  path is the editor player's own pawn in fly or ghost movement (`SetMovementMode` 0x3A51A60 flying,
  `SetActorEnableCollision` 0x37F2280), which keeps the mod's crosshair trace and input path; the debug camera
  controller is the fallback. Open: whether a cheat manager object exists on the player controller in shipping
  (`APlayerController::AddCheats` is an empty stub; `USTCheatManager` exists as a class).

## SDK road (user 2026-11-13: "research if there's possibility of an SDK to map build")

Facts:
- No official mod kit, editor or SDK for this game was found (web search 2026-11-13; hits were for the
  unrelated Terran Command scenario editor).
- Engine: Unreal Engine 5.6 (exe version resource 5.6.0.0), studio branch `++Aloft+Release_MS18-CL-149768`.
  Containers: IoStore, TOC version 8, not encrypted, not signed, no `.sig` files.
- The shipped PDB names every native class and function (about 1,000,000 publics), and the reflection data in
  the exe gives every reflected property and its offset. That is enough to generate header stubs.

A self-made SDK is possible in principle. It has three parts, each with its own risk:
1. **Stub project.** A UE 5.6 project whose C++ modules declare the game's classes (ST*, AD* plugins) with the
   same names, parents and reflected properties, generated from the exe's reflection data. Game Blueprints
   (`BP_SatchelPlacementSite_Flag` and so on) are referenced by path as empty stand-ins with the right parent,
   so a new map can place them and the real cooked ones load in the game.
2. **Cook and pack.** Build the level in the stock 5.6 editor, cook for Windows, write an IoStore container
   (`.pak` + `.utoc` + `.ucas`). Risk: the studio branch may have changed serialization (custom versions,
   package format); a stock-cooked package may be refused or crash. Not testable statically.
3. **Mount.** The game must load an extra container. Parked card 036 holds the probe plan (search folders,
   mount test, override test) and has not run. The mod can also call `FPakPlatformFile::Mount` (0x23B4310)
   itself. Rule kept: only new `zz_ubwcoop*` files under `game/`, never a changed game file.

Cost: Unreal Editor 5.6 install (tens of GB, Linux build exists) and a new tool (reflection dump to stubs).
What it buys over the in-game editor: terrain, baked lighting, navigation, new meshes and textures, whole new
levels. What it costs: weeks of unproven groundwork before the first map loads, and both players need the
extra container files (the in-game editor's layout rows travel through the mod's channel at join).

Recommendation: keep the in-game editor as the first road. Decide the SDK road after one cheap proof, in this
order: card 036 mount probe (1 run), then one stock-5.6 cooked empty map loaded by path (1 run). If either
fails, the SDK road is closed without further cost.

User decision 2026-11-13: in-game editor first; the two SDK proofs run later, after the in-game editor.

User, 2026-11-13: "In game editor like Forge from Halo is fine." Target shape taken from that (the senior's
reading, to confirm in the design note): edit inside a live session on the arena itself; switch between
player and a flying edit view with one key; a piece menu by category (walls, cover, crates, spawns, flags,
weapon pickups); grab, move, rotate, delete the piece under the crosshair; both players may edit together and
see each other's changes at once (answers question 7); save and load named layouts; play the layout in versus
without leaving the session.

User, 2026-11-13: "Yes" to both players editing together with changes seen at once. Question 7 is closed.

## Findings, second pass (static, 2026-11-13)

- Question 1 (base map): the first base is FED_C1M1 with its script chain held, the same state versus card A
  builds; the editor then needs no map work of its own. `Maps/Testing/TrainingCamp_Showcase` is a top-level map
  with its own built lighting data (`_BuiltData`), so it is the candidate for a clean second base. Whether it
  loads by travel and has a floor with collision is one run in the editor's first card (deciding row: the mod's
  map-load line for that path and the pawn's Z after 5 s).
- Question 3: closed. The editor moves the player's own pawn with `SetMovementMode` flying and collision off; no
  cheat manager object is needed, so whether shipping creates one does not matter.
- Question 4 (catalogue): 26 static meshes in `Art/Environments/Manmade/CampKeitel` (15 `ShootingArena/SM_ArenaPiece_*`,
  2 `SM_WallLaserTag_350_*`, `SM_CoverTower_01`, 8 `SandbagWalls/SM_Sandbag*`). Each package has a `BodySetup`
  export with the collision profile `BlockAll`. The shapes inside are stored without property names, so the
  package names do not show simple against complex collision; the same meshes already block pawns on the
  FED_C1M1 course. The course adds `BlockingVolume_Bullets_CTF` 1 to 4, so bullets may pass some pieces.
  Prediction for the first editor run: a spawned `SM_ArenaPiece_01` stops the pawn's walk and a laser shot
  (one sweep row and one hit row).
- Question 5 (file): one text file per layout under the mod's own folder next to its config, outside `game/`
  content: header line (format version, base map, name), then one row per piece: catalogue index, position,
  rotation, scale. Mode points (bases, flags, pickups) are rows with their own kinds. The host sends the rows at
  join and on every edit; the client stores nothing.
- Question 6 (limits): a row is about 60 chars, so a text frame (`kChanTextMax` 1400) carries about 20 rows.
  Cap 512 pieces per layout: 26 frames at join. Checks on load and on receive: catalogue index in range, finite
  position inside the base map's bounds, scale within 0.1 to 10, row count at most the cap; a bad row is
  dropped with one WARN, never applied.

Still open for the design note: question 2 (lighting of movable meshes and bullet collision of spawned pieces;
the first editor run decides both with the rows above).

## Question 2, settled as far as static work goes (2026-11-13)

- Lighting: the Trooper maps ship baked lighting (`FED_C1M1_BuiltData.uasset` with `.ubulk`, and
  `FED_C1M1_Lighting_BuiltData`; 125 built-data files in the containers). A piece spawned at run time is a
  movable mesh: it is lit like every other dynamic actor in the map (doors, pickups, corpses) and casts only
  dynamic shadows; it gets no lightmap. Nothing to build. Prediction for the first editor run: one client and
  one host shot of a spawned `SM_ArenaPiece_01` beside a placed one, same brightness class, no black mesh.
- Bullet collision: every catalogue mesh has a `BodySetup` with profile `BlockAll`, which blocks every trace
  channel, the weapon trace included. `BlockingVolume_Bullets_CTF` 1 to 4 exist for gaps between pieces, not
  because the pieces let bullets through. Prediction as above (one sweep row, one hit row on the spawned piece).
- Both points are now run predictions only; no open static question remains for the design note.
