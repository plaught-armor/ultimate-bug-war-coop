# 416 Teammate minimap blip and a coloured ring under the teammate

User request 2026-11-16: "Add blip, maybe a circle under the other player with a different color". The name plate
(card 110) stays; it costs 6 to 11 us per frame (run c414b111a) and is the only carrier of the down timer.

Both parts are local to each machine, never replicated, no wire rows. Each machine marks every other player pawn
(the `c110_others` list). Put both in `player_marker.cpp`, driven from `c110_step`, one state struct per slot.

## Part A: minimap blip

### Trace (exe disassembly, PDB names)

- `USTMinimapComponent::ShowOnMinimap(const AActor*)` (0x0521CEC0) appends an `FSTMinimapMarkerData` (0x38 bytes,
  array at component +0xF8, count +0x100): location 2D at +0x00 from `GetMinimapLocation` (0x05203250), angle at
  +0x10 from `GetMinimapAngle` (0x05203100), icon `UTexture2D*` at +0x18 from `Execute_GetIconTexture`, source
  actor at +0x20, marker widget at +0x28, remain-at-edge byte at +0x30 from `Execute_ShouldRemainAtEdgeOfMinimap`.
  It then broadcasts the delegate at +0xB8 with the new index; `USTMinimapWidget::AddNewMarker` (0x051F2F70) creates
  the marker user widget (class at widget +0x390), stores it in the entry at +0x28 and adds it to the canvas at
  widget +0x398. The broadcast is synchronous, so +0x28 is set when `ShowOnMinimap` returns (null when the minimap
  widget does not exist).
- `USTMinimapComponent::TickComponent` (0x0521E460), every tick, walks the array backwards and removes an entry
  when its source is null or garbage, when `Execute_ShouldShowOnMinimap(source)` is false, or when it is out of
  range (+0xDC) without the remain-at-edge byte. Surviving entries get location and angle refreshed. The tick's
  second loop (gathered actors) skips sources already in the array, so no double entry.
- The five `ISTMinimapMarkerInterface::Execute_*` wrappers are real static functions:
  `Execute_GetIconTexture` 0x0507D210, `Execute_GetWorldLocation` 0x0507D280, `Execute_GetWorldYaw` 0x0507D320,
  `Execute_ShouldRemainAtEdgeOfMinimap` 0x0507D390, `Execute_ShouldShowOnMinimap` 0x0507D400. Each looks up the
  function by name on the object, else the native interface, else returns the default (false, zero, null).
- Implementers: `ASTEnemyBase`, `ASTNonPlayableCharacterBase`, `ASTLevelObjectiveMarker`,
  `ASTAttackSystemEnemyBase`. The player character does not implement the interface. So a bare
  `ShowOnMinimap(player pawn)` adds an entry with no icon at world zero, and the next tick removes it.

### Change

1. Add the five wrappers to `tools/symbols.txt` (decorated names are in the PDB publics, for example
   `?Execute_ShouldShowOnMinimap@ISTMinimapMarkerInterface@@SA_NPEBVUObject@@@Z`), regenerate
   `offsets.generated.h` with `python3 -I tools/pdb_rva.py gen`, and detour all five. In each detour: when the
   object is a pawn in the teammate table (at most `kC43Max` pointer compares, plain array, no lookup), answer
   from the mod; otherwise call the original unchanged.
   - `ShouldShowOnMinimap`: true while the slot is in the state where the name plate shows or would show behind
     the camera (pawn alive or Downed, not hidden). False when Dead, hidden or gone, so the game's own tick
     removes the entry.
   - `GetWorldLocation`: the pawn's actor location. `GetWorldYaw`: the pawn's actor yaw.
   - `ShouldRemainAtEdgeOfMinimap`: true (the blip gives direction when the teammate is out of range).
   - `GetIconTexture`: `/Game/UI/Fed_HUD/GenericItemExports/T_Ally_Arrow.T_Ally_Arrow`, found loaded else loaded
     with `g_static_load_object` (the `difficulty.cpp:584` recipe), held in a weak reference and reloaded when dead.
2. In `c110_step`, per slot, when the answer to `ShouldShowOnMinimap` is true and the local PC's minimap component
   (`kPcMinimap`) has no entry whose source (+0x20) is this pawn: call the original `ShowOnMinimap(minimap, pawn)`.
   Check the array at most once per second per slot (same cadence as `kC110ScanMs`), not per frame. Skip while the
   minimap's OriginActor is null (`chan_ops.cpp:68` note).
3. After the call, when the new entry's widget (+0x28) is not null, tint it with
   `UUserWidget::SetColorAndOpacity(FLinearColor)` (0x0362D840) using the teammate colour (below), so the blip
   differs from squad troopers.
4. Log, once per slot per map: `card416: <side> blip slot <n> added (icon <name>, widget <ptr or none>, markers
   a -> b)`, and a WARN when the icon cannot be loaded or the entry is missing after the call.

## Part B: ring on the ground under the teammate

A static mesh ring attached to the teammate pawn, unlit, in the teammate colour.

1. Component: `UStaticMeshComponent` added with `g_add_component_by_class` (the `remote_body.cpp:455` recipe),
   attached to the pawn root. No collision, no shadow, no decal receive, no overlap events.
2. Mesh: `/ControlRig/Controls/ControlRig_Circle_3mm.ControlRig_Circle_3mm` (cooked, listed in
   `docs/reference/asset-paths.txt`). Its size is not known: read the mesh bounds at run time and set the scale so
   the ring's outer radius is 60 cm. Fallback when the mesh does not load: `/Engine/BasicShapes/Cylinder.Cylinder`
   scaled to a 120 cm wide, 1 cm high disc. Log which one was used.
3. Material: a dynamic instance of `/Engine/EngineMaterials/GizmoMaterial.GizmoMaterial`
   (`UPrimitiveComponent::CreateDynamicMaterialInstance` 0x03A89B70) with vector parameter `GizmoColor`
   (`UMaterialInstanceDynamic::SetVectorParameterValue` 0x03E2CCC0). The parameter name comes from engine source,
   not from this game's cooked asset; the screenshot is the check.
4. Placement: relative Z = minus the capsule half height plus 2 cm, so it sits on the ground under the feet;
   absolute rotation off, so it tilts with the pawn only as far as the capsule does (the capsule stays upright).
5. Visibility follows the same per-slot state as the blip: hidden when the pawn is hidden, Dead or gone. Set it only
   on a state change, never per frame.
6. Teammate colour for both parts: one constant, linear (0.0, 0.85, 1.0, 1.0) (cyan; squad and objective icons are
   white, gold and red).

## Cost bound

No per-frame work beyond what `c110_step` already does: the blip check is once per second per slot, the ring is
moved by the engine's attachment. The five detours add one pointer compare per slot per call. Report the
`card330` row for `c110_step` on both sides against b111 (host 5.7 to 7.8 us mean, client 6.0 to 11.3 us).

## Predictions for the run

- Each side logs `card416: ... blip slot 1 added` once per mission map, with a non-null widget, and the marker
  count goes up by 1 and stays up (a sample row 5 s later shows the entry still listed).
- Each side logs the ring component and the mesh used, once per mission map.
- No new WARN class, 22 `mismatches 0` each side, FATAL and ERROR 0.

## Proof

- One fast chain run (`c399-fast-chain`, lane 2, new build with the 17 pins updated), plus one screenshot on each
  side taken with the teammate in view (teleport next to the teammate, look at it): ring visible in cyan under the
  teammate, blip visible in cyan on the minimap. This also settles the name plate screenshot owed from card 414.
- Budget: 3 runs. Gameplay test scenario rules in `docs/STATE.md` apply.

## Not in scope

No change to the name plate. No objective marker. Nothing sent on the wire.
