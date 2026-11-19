# Card 068: Why the client minimap shows no icons (log only)

Status: sent 2026-11-05 02:38 to junior ultimate-bug-war-coop-bb, after card 065 was accepted. Walk with the card 063/064 runner (goto, `ui continue`).

Read `docs/STATE.md` ("The client minimap shows no icons" and the card 058 origin fix in `sync_client_minimap_origin`). Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

In `client-c63f-picked-up.png` the client's minimap shows the compass and no icons. Card 058 fixed the null `OriginActor` that crashed the client, and the c63f client log shows the fix ran (`card058: client minimap ... -> BP_PlayerCharacterMI_C`). Something after that is still missing. Find which link it is before writing a fix.

## Senior research (2026-11-04, static)

The chain, from the objective to the icon:

1. `ASTLevelObjectiveMarker::ObjectiveUpdated` 0x051614C0 runs on a marker visibility edge (`USTLevelObjective::IsMarkerVisible` 0x0515F500; the marker caches the state at +0x2D8). It finds player controller 0's `USTMinimapComponent` and calls `ShowOnMinimap(marker)`. This is the only native caller.
2. `USTMinimapComponent::ShowOnMinimap` 0x0521CEC0 appends a 0x38-byte entry to `ActiveMarkers` (+0xF8; count at +0x100) only when the marker is inside `MinimapRange` (+0xDC) or `ShouldRemainAtEdgeOfMinimap` is set.
3. `USTMinimapWidget::NativeOnInitialized` 0x0520F170 gets its owning player (vtable +0x180), casts it, reads PC+0x7B0 (the `USTMinimapComponent`), binds two delegates on it (two `AddDynamic`-shaped calls at +0x276 and +0x336), then calls `AddNewMarker` for each marker already there (+0x3C5).
4. `USTMinimapWidget::AddNewMarker(int)` 0x051F2F70 makes the icon. Its other caller is `execAddNewMarker` 0x0507D8A0, the Blueprint thunk, which is probably the bound delegate's handler. `RemoveMarker(int)` 0x052130F0 and `UpdateMarkerLocation(FSTMinimapMarkerData)` 0x0521FD30 are the other widget entry points. `TickComponent` is 0x0521E460 and `USTMinimapWidget::NativeTick` is 0x0520FFE0.

Any link can break on the client:

- (a) There is no visibility edge, because the replayed objectives (card 049) do not reach `ObjectiveUpdated`.
- (b) `ShowOnMinimap` runs but does not append (a range or origin problem).
- (c) It appends, but the widget never hears about it: the widget initialized before the component existed, or it bound a different component.
- (d) `AddNewMarker` runs, but the icon is placed off the map.

## Do

Behind `UBWCOOP_C68_LOG=1` only, on both machines:

1. Detour `ObjectiveUpdated`, `ShowOnMinimap`, `AddNewMarker`, `RemoveMarker` and `NativeOnInitialized`. Log the role, `this`, the marker's name and class (for `ShowOnMinimap`), the `ActiveMarkers` count before and after, and the index (for `AddNewMarker`).
   - For `NativeOnInitialized`, log the widget, its owning player and the PC+0x7B0 component, both before and after the original.
   - Every `UpdateMarkerLocation` call would flood the log, so count those calls instead and log the count with the 1 s line.
2. Each second, log the local PC's minimap component (PC+0x7B0), its `OriginActor` (+0x108), the `ActiveMarkers` count and `MinimapRange`.
3. Run with the card 063/064 runner. Both players walk the start of the assault course until the host's minimap shows icons. Take a `shot` on each side at that moment.
4. Report the first link at which the client's sequence differs from the host's, with the log lines. Name (a), (b), (c) or (d), or a new cause.

## Done when

- The Report lists the host's and the client's sequences side by side, and names the broken link.
- Both shots are in the Report.
- `logsum` gives ERROR/FATAL 0, both games end with rc 0, and there is no new dump.
- With `UBWCOOP_C68_LOG` unset, the detours are not installed.
- fnlen reports no function over 60 lines.

## Report

**Broken link: (a), with a narrower cause.** On the client the replayed objectives never create their marker actors. `ASTLevelObjectiveMarker::CreateASTLevelObjectiveMarker` does not run, so `ObjectiveUpdated` never fires and `ShowOnMinimap` never appends. Links (b), (c) and (d) are fine on the client:

- When a marker is created, `ShowOnMinimap` appends.
- `AddNewMarker` follows in the same frame.
- The icons it adds are drawn on the map.

### Runs

All runs used `tools/scenarios/c68-minimap.txt`.

| Run | C68 log | Result |
|---|---|---|
| c68d | on | Main evidence run. |
| c68f | on | Rerun after the `install()` split below. It reproduces c68d: host ObjectiveUpdated 22 / ShowOnMinimap 11 / AddNewMarker 18; client 1 / 1 / 7. |
| c68e | unset | Off check. |

Every run passed the drive (rc 0). Both games ended with rc 0, logsum gave FATAL/ERROR 0 and WARN 0, and no new dump appeared.

With the log unset (c68e), both logs say `config: UBWCOOP_C68_LOG unset -- card 068 minimap log off`. The `hooks: card 068 minimap log on (6 detours)` line is absent and there are 0 `card068:` lines, so the detours are not installed.

fnlen: `hooks.cpp` has 1266 functions with 0 over 60 lines; `config.cpp` has 38 with 0 over 60.

### Side by side (c68d)

Times are in ms, from the mod log clock. The two machines' clocks agree to within a few ms.

| Step | Host | Client |
|---|---|---|
| Widget init | `NativeOnInitialized` after: widget 6EA99D90, minimap 87CFD1E0, ActiveMarkers 0 (18210266) | `NativeOnInitialized` after: widget 40562B60 (`WB_Minimap_Compass_C`), minimap 7CCA91C0, ActiveMarkers 0 (18179632) |
| Objective added | 18210634 `AddObjective(Assault_O1) ... actors 0`, then 18210635 `AddSubObjective(Assault_O1, Assault_SO0) ... actors 1` | 18210642 `replayed AddObjective(Assault_O1) flags 0xB count 0`, then `replayed AddSubObjective(Assault_O1, Assault_SO0) flags 0x6 count 0` |
| Marker created and updated | 18210635 `ObjectiveUpdated marker 7EA486E0 BP_ObjectiveMarkerActor_C, objective STLevelObjective, visible cache 0 -> 1, caller exe+0x5150FB0`, followed by more in the same ms | **nothing** |
| ShowOnMinimap | 18210635 `ShowOnMinimap ... actor BP_ObjectiveMarkerActor_C, ActiveMarkers 0 -> 1, origin BP_PlayerCharacterMI_C, caller exe+0x51615D4`, then 1 -> 2, 2 -> 3 and so on, up to 9 -> 10 | **nothing** |
| AddNewMarker | index 0, 1, 2 and so on, `caller exe+0x507D913` | **nothing** |
| Totals | ObjectiveUpdated 22, ShowOnMinimap 11, AddNewMarker 19 | ObjectiveUpdated 1, ShowOnMinimap 1, AddNewMarker 7 |
| 1 s line, ActiveMarkers | 3 to 7 once the pawn spawns | 0 for 57 s, then 1 to 5 |

The two `ObjectiveUpdated` callers, identified with `llvm-symbolizer` against the PDB:

| Caller | Function | Host | Client |
|---|---|---|---|
| exe+0x5150FB0 | `ASTLevelObjectiveMarker::CreateASTLevelObjectiveMarker(USTLevelObjective*, AActor*)` | 12 | 1 |
| exe+0x5078855 | `ASTLevelObjectiveMarker::execObjectiveUpdated`, the Blueprint thunk for later state changes | 10 | 0 |

The other callers that appear in the log:

| Caller | Function |
|---|---|
| exe+0x507D913 | `USTMinimapWidget::execAddNewMarker` (Blueprint) |
| exe+0x507E2B3 | the `RemoveMarker` thunk |

### The client's only marker

The client's only `ObjectiveUpdated` and `ShowOnMinimap` came at 18236723, 26 s after the replay.

That marker came from the client's own level script, not from card 049. Just before it, the client log has `SetObjectiveVisibility(Assault_SO0) ... ctx FED_C1M1_Scripting_C`. In the same ms it has `card048: client ... AddObjectiveObject #15: STLevelObjective ... callers 0x522A9F8 < 0x0 < 0x5081E20`, which is the native `AddSubObjective` path with its actor. Once that local objective made a marker, the client went through every remaining link in one frame:

```
AddNewMarker index 0, caller exe+0x507D913
ShowOnMinimap ... ActiveMarkers 0 -> 1, origin BP_PlayerCharacterMI_C, caller exe+0x51615D4
ObjectiveUpdated marker 486D1870 ... visible cache 0 -> 1, caller exe+0x5150FB0
```

The other 6 `AddNewMarker` calls on the client (indices 1 to 4, from 18242359) are non-objective icons, such as pickups. They come through `execAddNewMarker` without `ShowOnMinimap` and are not objective markers. They show up in the shots.

### Cause and fix direction

The host's objectives get their marker actors from the actor array that is passed to `AddSubObjective`: `actors 1`, then `CreateASTLevelObjectiveMarker(objective, actor)`.

The card 049 journal sends only the name, flags and count. The client's replay calls `AddObjective` / `AddSubObjective` with no actors, so no marker is created and nothing reaches the minimap.

The fix belongs in card 049's journal or replay:

- One option is to send the objective actors as network GUIDs or level paths, and pass them to the replay.
- The other is to call `CreateASTLevelObjectiveMarker` for each replayed objective, using the client's copy of the placed actor.

`MinimapRange` (+0xDC) reads -1 on both sides throughout, so range is not a factor. The client's `OriginActor` is `BP_PlayerCharacterMI_C`, the same as the host's.

### UpdateMarkerLocation counts

The counts are per 1 s line. Most lines are 0 on both sides.

- Host: 10 once, just after the spawn, and otherwise 1 or 2 when markers change.
- Client: 1 on the lines where a marker was added.

### Shots (c68d)

The shots are in `Saved/Screenshots/Windows/` as `host-c68d-<name>.png` and `client-c68d-<name>.png`, for `start`, `first-marker`, `pickup` and `range`.

| Shot | Host | Client |
|---|---|---|
| `first-marker` (3 s after the host's `AddObjective(Assault_O1)`) | Yellow assault-course objective icons to the north-east on the minimap. | Empty minimap, apart from the player arrow. |
| `pickup` and `range` | Pickup and teammate icons. | The same pickup and teammate icons, on the map and in the right places. This is why (d) is ruled out. |

### Problems I caused, and their fixes

These happened before c68d.

- **c68a crashed both games** (dumps A170698848… and F073D476…).
  - In `c68_log_init` I called the widget's vtable +0x180 thinking it returned the owning player. It is `GetWorld`.
  - Fixed by using `g_get_player_controller(widget, 0)` instead.
- **c68b crashed the client during map load** (dump 4CA8D424…).
  - I read PC+0x7B0 on a controller that was not an `ASTPlayerController`.
  - Fixed with `is_a(pc, g_st_pc_class())` guards in both `c68_log_init` and `c68_step`.
  - No dumps since then.
- **c68c's wait regex `card068: host ShowOnMinimap` never matched,** because the host's side text is `host/standalone`.
  - The scenario now waits on `card049: host sent AddObjective\(Assault_O1\)`, which also works with the log off.

### Code

- `UBWCOOP_C68_LOG` is read by `config::read_c68_log()`.
- `hooks.cpp` has six detours, installed by `c68_install()`. Event lines are capped at 400 per map, and the 1 s line comes from `c68_step` in `detour_tick`.
- `UpdateMarkerLocation` is detoured only to count calls.
- `tools/symbols.txt` gained the six card 068 symbols. Their RVAs match the senior's research, and `UpdateMarkerLocation` is at 0x0521FD30.
- The c65 and c68 installs moved out of `install()` into `install_card_logs()`, which brought `install()` back under 60 lines.

## Senior review (2026-11-05 03:06): accepted

- Checked c68d, c68e and c68f myself. All three drives end with `scenario passed`. logsum gives ERROR/FATAL 0 and WARN 0 on both sides of all three runs.
- The only dumps (4CA8D424, A1706988, F073D476, 02:42 to 02:46) come before c68d and are the c68a/c68b detour bugs named in the Report. There are no dumps after them.
- The final DLL (hash 352c69e81374be56, deployed 03:01:25) predates c68f (start 03:01:31), so c68f is on the shipped code. c68e ran on the DLL before the `install_card_logs()` split. The gate itself did not change, and c68e shows `UBWCOOP_C68_LOG unset -- card 068 minimap log off` with 0 `card068:` lines.
- fnlen: 1266 functions, 0 over 60 lines.
- The client log confirms the cause. Replayed `AddSubObjective(Assault_O1, Assault_SO0) flags 0x6 count 0` passes no actors, and no `ObjectiveUpdated` follows it. The host's first-marker shot shows objective icons on the minimap; the client's minimap is empty.
- Broken link (a) accepted: the card 049 replay creates no `ASTLevelObjectiveMarker`. The fix is a follow-up card (070): pass the objective actors through the journal, or call `CreateASTLevelObjectiveMarker` on the client's copy of each placed actor.
