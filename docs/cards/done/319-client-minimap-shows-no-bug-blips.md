# 319 Client minimap shows no bug blips

Playtest 2026-11-13, play build 4 `5e2ce00acad8efa3` (user, client): "As the client I also never saw the bug blips
on my screen". Host saw them. Related: card 293 (phantom blips on the host).

Senior trace 2026-11-14, exe disassembly. How a bug blip gets on the minimap is settled. Which gate fails on the
client in a played session is not: step 1 names it with one row. No fix is coded before that row is read.

## How a bug blip is made (settled)

Bug blips do not come from `ShowOnMinimap` calls. `USTMinimapComponent::TickComponent` (0x0521E460) scans for them:

1. Exit if OriginActor (+0x108) is null. Skip the scan if the float at +0xD8 is not above 0.
2. `GetSTHUD` (0x0523DF50); skip the scan if the HUD is null or not an `ASTHUDGameplay`.
3. `ASTHUDGameplay::GatherMinimapMarkers` (0x051FC5D0). It returns the cached list at HUD+0x3D0 while the byte at
   HUD+0x3F0 is set. Otherwise:
   - `GetSTPlayerController(HUD)`, pawn = PC+0x2E8. No pawn: no gather.
   - radius = `FindLargestMinimapScanningRange(PC)` (0x051C6670), centre = the pawn's root location.
   - A sphere overlap for actors (0x143D40360) with the object types in the array at HUD+0x3E0, ignoring the pawn.
   - Each hit that implements the minimap marker interface and passes `ShouldShowOnMinimap` (for an NPC: vfunc
     +0x9E0 false and the icon texture at NPC+0x838 set) goes in the list.
   - HUD+0x3F0 = 1, and a timer of HUD+0x400 seconds calls `SetMinimapMarkersReadyForUpdate` to clear it.
4. Back in the tick, each listed actor in range (+0xDC) gets a marker in `ActiveMarkers`; the widget draws it.

So a client bug blip needs, on the client: an `ASTHUDGameplay`, PC+0x2E8 set to the pawn the player is steering, a
range above 0, the timer firing, and NPC copies whose collision answers the HUD's overlap object types.

## What is known about the client

- Test client, 2026-11-07 (`logs/card131-shots/client-c131k2-c131k-nest-*.png`): red bug blips drawn. The chain
  worked on a test client on that build.
- Playtest client logs (`logs/playtest-menu-20261113-183002.log`, `...184603.log`): `card058 ... OriginActor` and
  `card120 ... minimap origin set` on every map. Gate 1's origin held.
- No later test shot settles it: scenarios clear bugs by code before the shots.

## Step 1 (diagnostic, both sides, one row per 5 s while a local pawn exists; rides the run 3 build)

`card319: <side> t=<ms> hud <class> gameplay <0|1>, pc pawn <same|other|null> (<uu> from the local pawn), range
<largest> scan <+0xD8> draw <+0xDC>, object types [<n>: ...], cached <0|1> every <s> s, gathered <n>, live npc in
range <n>: gathered <n>, in ActiveMarkers <n>`

Then, for up to 3 live NPCs in range that are not gathered, once per NPC:
`card319: <side> missed <class> guid <id> at <uu>: capsule collision <enabled> object type <n> responses <...>,
mesh collision <enabled> object type <n>, vfunc 9E0 <0|1>, icon <0|1>`

Map-end tally per side: `card319: <side> samples <n>, live npc in range <sum>, gathered <sum>, in ActiveMarkers
<sum>, samples with pc pawn not same <n>, samples with hud not gameplay <n>`.

"Live npc in range" comes from the mod's own NPC registry (health above 0, within the largest range of the local
pawn), not from the game's list.

## Predictions (written before the run)

- Host: every sample `gameplay 1`, `pc pawn same`, gathered = live npc in range = in ActiveMarkers (a difference of
  1 for one sample at a spawn or a death is accepted, the timer lags).
- Client in the test: the same, as on 2026-11-07. If so, the test does not reproduce the playtest, and the scenario
  must take the playtest's path before a second look: join from the menu, no FAST route, the client dies and
  respawns once, then stands near live bugs. The pawn check (`pc pawn other`) is the first suspect on that path:
  the scan centres on PC+0x2E8, the drawing on OriginActor, and only OriginActor is kept current by card 058.
- Client fails in the test: the row names the gate, and the `missed` rows say whether it is collision (the copy's
  capsule or mesh does not answer the HUD's object types), the icon, or vfunc 9E0.

## Run 3 result (b18, build `0c86ba1c6c2d79b0`, senior-verified from both logs)

- Host: 82 gameplay samples, all `gameplay 1`, `pc pawn same`; one sample unequal (t=295208702, in range 2,
  gathered 0), spawn lag.
- Client tally: `samples 77, live npc in range 181, gathered 179, in ActiveMarkers 179, pc pawn not same 0, hud
  not gameplay 0`. One unequal sample (t=295206603), two `missed BP_AI_Trooper_NonCombat_C` rows in the same ms.
- Both minimap shots show markers, but they are yellow (friendly troopers); no live bug was in range, the scenario
  clears them. The proof shot with a live bug is still owed.
- So the test client does not reproduce the playtest, as predicted. Next: the played path (join from the menu, no
  FAST route, the client dies and respawns once, then stands within 2500 uu of live bugs with no `clear`), same
  rows. `object types [0:]` prints empty on both sides: fix the row so it lists the HUD's object types.

## Played-path scenario review (senior, 2026-11-14, `tools/scenarios/c319-played-c1m1.txt`; change before the run)

- The user said "never saw": the fault may be there from the map start, before any death. Do the live-bug step twice, once right after `clock 1` and before the down, once after the respawn, each with its shot pair. The tally then separates "from the start" from "after the respawn".
- The respawn in the scenario is a checkpoint Reload, which reconnects the client: a fresh join and a fresh controller, the path least likely to leave a stale pawn. Keep it, but add the playtest's other pawn changes if they are cheap: a down and revive by the host with no reload, before the reload case.
- Keep the title screen on both sides (`UBWCOOP_C135_KEEP_SCREEN`), as in the play build; the client confirms after the host. The timer that clears the HUD's cached flag and the scan float at +0xD8 are both set near the map start, and the test build's skipped screen changes that order.
- Add predictions for the pre-death samples: client `range` and `scan` above 0 and `cached` toggling (two samples 5 s apart do not both read the same gathered list while a bug moves in). A client row with `scan 0.0` or `hud` not gameplay names the start-order cause without the death.
- `host spawn bug` makes bugs by code. The playtest's bugs came from the game's spawners (burrow, bug form rows of card 208, arena zones). Add one sample pair beside game-spawned bugs: `host objective go` to the first objective with a live wave and no `clear` until both shots are taken. No client test has had a live bug on the minimap since 2026-11-07, so a failure for every bug in any session is the first thing this run rules in or out.

## Batch 1 review (senior, 2026-11-14; change before the run)

- The sample waits match `live npc in range [1-9]`. The count includes friendly troopers (run 3: client 181 in range with every bug cleared), so each wait passes at once and step C proves nothing about game-spawned bugs. Add `hostile <n>: gathered <n>, in ActiveMarkers <n>` to the row and the tally (`c319_live`, by the mod's own enemy test) and wait on `hostile [1-9]`.
- `host down client` hits through `c43_hit`; under `GOD=1` `c132_god_block` puts the Health back (only `in_kill` is exempt), so B and D end in `gave up`. The test hit must land under the test god.
- `host ui continue` and `client ui continue` each need a wait on that side's `card135: ... screen prompt ready` row first.

## Proof shot

Receiver side: one client screenshot with a live bug within 3000 uu and no `clear`, minimap cropped
(`magick <png> -crop 110x100+0+260 +repage -filter point -resize 300% out.png`), viewed by the junior, spot-checked
by the senior; the host's shot at the same step beside it.

## Fix (after step 1; one of, by the failing gate)

- Pawn: wherever card 058 sets OriginActor, the same pawn is what the scan uses; fix where the client PC's pawn
  goes stale, not in the scan.
- Collision: the NPC copy gets the host's collision setup for the components the HUD's object types query. Class
  rule for every copy class, with card 293's `no-collision` reason checked against the same table.
- HUD, range or timer: the client gets what the host's server-only path sets, at the same moment.

## Sibling sweep (report before coding the fix)

Every other reader of the same inputs on the client: `USTBugProximityAreaWidget` (OnNewMarker 0x05210890,
NativeTick 0x0520F8F0: the screen-edge bug proximity icons, bound to the same component), any other sphere overlap
against NPC copies (aim assist, melee, interact prompts), and every HUD field the host fills in a server-only call.

## Done when

Client tally: gathered = live npc in range on every sample of a played-path run, the proof shot shows red blips on
the client, and the user sees bug blips as the client in the next playtest.

## b19 run 1 result and decision (senior, 2026-11-14, `d781c1e804ad64d6`, `logs/c319-played-c1m1-*.log`)

- Does not reproduce. Client tallies: `samples 116 ... hostile 5: gathered 5 ... pc pawn not same 0` before the
  reload and `samples 4 ... hostile 12: gathered 12` after it; the first samples after the respawn (t=303958885 on)
  read `pc pawn same`. Samples A (map start), B (down and revive) and D (checkpoint reload) held. Sample C is void:
  the objective step had no live spawns.
- So the played path, a death and a reload are not the cause. No more c319 runs.
- What the playtest had that the run did not (client log `logs/playtest-menu-20261113-184603.log`): 81.5 min,
  several map travels, mech and turret use. `card058: client minimap ... OriginActor` changed 76 times: 68 to
  `BP_PlayerCharacterMI_C`, 4 to `BP_BaseMech_C`, 4 to `BP_MountableTurret_C`. Play build 4 printed no `card319`
  rows, so that log cannot name the failing gate.
- Decision: `c319_step` is unconditional from b19 on (`hooks_ubw.cpp:204`), so play build 5 logs the gate row every
  5 s on both sides. The next playtest names the gate (`pc pawn other`, `hud` not gameplay, `scan 0.0`, or hostile
  in range not gathered); the fix follows from that row. Not a play blocker.
- Owed in the C2M1 soak (first map with a mech): one client sample pair seated in the mech and one after leaving it,
  waiting on `hostile [1-9]`, and sample C with a spawn step.

## Closed

Closed 2026-11-17 (senior): c447b173a on FED_C4M1: client gate rows seated in the mech hostile 8 gathered 8, in the turret 9 of 9. No failing gate in any run since b19; a client minimap with no blips in a playtest reopens it with the card319 gate row.

## Reopened (2026-11-18 14:39, senior)

The user, during c244b231a (FED_C1M1, both windows at a nest, screenshot): "host also has a bunch of enemies on their
radar and client doesnt". This meets the reopen condition. The card319 rows matched on both sides while they ran (both
gathered 2, ActiveMarkers 2, hostile 1 at t about 673031), but the 64-row cap ended them before the nest moment, so the
gap at screenshot time is not logged.

b232 diag (junior): one uncapped row at every `shot` on both sides, with OriginActor, +0xD8, +0xDC,
FindLargestMinimapScanningRange(PC), HUD+0x3F0, gathered and ActiveMarkers counts, and for each gathered NPC the vfunc
+0x9E0 result, the icon at +0x838, and the distance. Prediction: the client gathers the hostiles but drops them at
+0x9E0 or +0xDC, or its scan range is smaller (the range reads host-only state).

### Static gates (senior, 2026-11-18)

- `ASTPlayerController::FindLargestMinimapScanningRange` (0x051C6670) is the largest `USTMinimapComponent::GetMinimapRange`
  (0x05203680, a bare read of the component's +0xD8) over the minimap components on the PC itself (GetComponents on
  the controller, not the pawn). If anything raises +0xD8 at run time (an upgrade or a tactical item), and that write
  runs only on the host, the client's scan radius stays smaller. The shot row's +0xD8 and scan range answer this.
- `ASTNonPlayableCharacterBase::ShouldShowOnMinimap_Implementation` (0x051A6890), from the interface at NPC+0x7A0:
  false if the actor's vfunc +0x9E0 returns true; false if the icon at NPC+0x838 is null or its object flag bit 30 is
  set; otherwise true. The +0x9E0 slot is not named yet (no vtable symbols in the publics); `ASTCharacter::IsDead`
  (0x050F0D40, reads +0x688 then a tail call) is the likely candidate. If the client's NPC copies answer +0x9E0 true
  while alive, the shot row's vfunc 9E0 column shows it.

## b231-b237 data (senior, 2026-11-18 16:24)

- **Per 5 s sample totals (hostile in ActiveMarkers / hostile live in range), host vs client:**

  | Run | Host | Client |
  |---|---|---|
  | c244b231a | 152/170 | 141/154 |
  | c244b234a | 145/158 | 197/207 |
  | c244b236a | 44/49 | 34/43 |
  | c244b237a | 74/88 | 75/85 |
  | c247b237a | 0/0 | 145/147 |

  The client's ratio is the same as the host's in every run. Over a run, the scan does not lose bugs on the client.
- **One shot:** c244b237a `c244-04-go` (both players at the same nest, shots 88 ms apart) differs.
  - The host gathered 6 (5 WarriorBugs at 502 to 2175 uu). The client gathered 3 (2 WarriorBugs).
  - The cropped minimaps show the same: about 5 red blips on the host and 2 on the client. That is the user's
    report.
  - The client's gathered rows all pass the dead and icon gates, so the bugs it lacks were not in its sphere overlap
    at that moment.
  - The `missed` rows show the same collision on both sides (capsule 3, type 2; mesh 1, type 21), so collision is not
    the gate.
  - The host's own 5 s sample at 678172322 also had 1 of 3 marked, so per-instant gaps happen on the host too.
- **Open question:** were the bugs the client lacked alive on the client at that moment (spawn or kill relay timing
  after `host clear`), or alive but outside its overlap? The rows cannot say, because the gathered rows carry no guid.

**Next diag (b238):** each shot row lists every live hostile NPC within the scan range with its guid, its distance
and gathered 0/1, on both sides. Joining host and client by guid then shows which side lacks which bug and why.

## Closed again (2026-11-18 18:52, senior, c247b246a guid rows)

The b238 guid diag ran in c247b246a: 7 shots had hostiles on both sides. Joined by shot and guid, 14 bugs were live on
both sides, and all 14 were gathered and marked on both. There were 0 host-only bugs. 5 were client-only, all at 2272
to 2497 uu, the edge of the 2500 uu scan. Each side scans from its own pawn and the two pawns stand a little apart, so
those are range-edge differences, not a lost blip. The b237 shot where the client lacked 3 bugs did not happen again,
and the guid rows show the same live set on both sides.

Not done: the b237 one-shot gap is not explained. The likely cause is spawn or kill relay timing after `host clear`,
but that is not proven.

Reopen when: a shot pair has a bug live and inside 2000 uu of both pawns that is gathered on the host and not on the
client, or a playtest again reports no blips on the client.

## Reopened (2026-11-18 19:03, senior)

The close above tested the wrong display. The user: the bug display is the middle HUD, not the minimap. That HUD is
`USTBugProximityAreaWidget` (child icon `USTBugProximityIconWidget`; props `BugProximityIconWidgetClass`,
`ProximityOverlay`). The c247b246a minimap rows (14 of 14 bugs gathered on both sides) stay valid for the minimap
component, so the component's scan works on the client; the gap is between that component and the widget.

Static trace of `NativeOnInitialized` (RVA 0x0520ED70):
- `GetSTPlayerController` (0x523E190); null -> return, no binding.
- `ASTPlayerController::GetProximityComponent` (0x5085890: FindComponentByClass USTMinimapComponent), stored at
  widget+0x358. Null -> return, no binding. This runs once: if the client's widget initializes before the PC has
  the component, the widget is never bound and never retries.
- `__Internal_AddDynamic` (0x36BF770) twice: `OnNewMarker(int)` (0x5210890) on the delegate at comp+0xB8,
  `OnRemoveMarker(int)` (0x5210A90) on the delegate at comp+0x180.
- Then calls `OnNewMarker` directly (0x14520EF0B) for markers that already exist.

Open: does the client's widget hold a non-null comp at +0x358, and do the comp+0xB8 / +0x180 broadcasts fire on the
client (markers may come from `ShowOnMinimap(AActor)` callers that only run on the host).

## Diag (b248 or next build)

Both sides, card319 rows, at the existing c319 shots:
1. Detour `OnNewMarker` (0x5210890) and `OnRemoveMarker` (0x5210A90): count calls per side; one row per shot with
   the totals since the last shot.
2. Find live `USTBugProximityAreaWidget` objects: per widget, comp at +0x358 (null or not, and equal to
   `read_ptr(pc, kPcMinimap)` or not), invocation count of the delegates at comp+0xB8 and comp+0x180 (TArray num of
   the invocation list), widget visibility, and its live icon child count.
3. One row per shot: "card319: <side> t=<ms> widgets <n> comp <ptr|null> same_as_pc <y|n> binds new <n> rm <n>
   calls new <n> rm <n> icons <n>".

Prediction: host shows binds 1/1, calls > 0, icons > 0. If the client shows comp null or binds 0, the fix is a
client-side rebind (call the widget's NativeOnInitialized binding path again, or AddDynamic ourselves) once the PC
has the component. If the client binds but calls stay 0, the markers are fed from a host-only path; trace
`ShowOnMinimap` callers next.

Reopen-close rule: close only when the client's widget icon count matches the host's at the same shots.

## b249/b250 diag result and cause (2026-11-18, senior)

Junior rows (c448b250a, c492b250a): both sides have two `WB_Bug_Proximity_Area` widgets; the live one holds a
second `STMinimapComponent` on the PC (not the one at pc+0x7B0; host A72C2140 vs A72C2020, client 9D2ADE40 vs
9D2ADC00), bound to OnNewMarker once on both sides. Host: 184 new / 182 rm calls, up to 25 icons. Client: 0 calls,
0 icons at every one of 46 shots.

Static (exe): `USTMinimapComponent::TickComponent` (0x14521E460) returns at once when `OriginActor` (comp+0x108,
`kMinimapOrigin`) is null or pending kill (+0x30..+0x4A). Only after that gate does it gather from
`ASTHUDGameplay::GatherMinimapMarkers`, filter by `ShouldShowOnMinimap`, and broadcast
`FMinimapMarkerChangedDelegate` on comp+0xB8 (0x14521E90B-E91E), which is what calls the widget's OnNewMarker.
`ShowOnMinimap` has one caller, `ASTLevelObjectiveMarker::ObjectiveUpdated`; bugs do not use it.

Cause (predicted): card 058 sets OriginActor on the client only for the pc+0x7B0 component
(`sync_client_minimap_origin`, input_diag.cpp:63), copying what server-only OnPossess does. The proximity widget's
component is a second one that OnPossess also sets on the host and that nothing sets on the client, so its tick
stops at the origin gate.

Fix: in `sync_client_minimap_origin`, call `g_set_minimap_origin(comp, pawn)` for every `USTMinimapComponent`
whose owner is the PC (objects_of_class plus `kComponentOwner`), not only pc+0x7B0; one card058 row per component.
Add the comp's OriginActor to the card319 per-widget row. Proof: client widget row shows origin = client pawn,
calls new > 0, icons > 0 at shots where the host has icons for the same bugs. Close when the client icon count
tracks the host's at the same shots.

Addendum (senior, static): the only native call of `SetOriginActor` (0x14521C2B0) is `ASTPlayerController::OnPossess`
+0x255 (0x1451D1A15), on pc+0x7B0 only. The second component's origin on the host therefore comes from Blueprint
(`execSetOriginActor`), most likely BP_PlayerController_C's possess event, which is server only. Same gap as card
058, second component. The fix (set every PC-owned component) covers it whichever BP node sets it.

## Closed (2026-11-18 20:36, senior, b251 be4b80883eb2b57e / b252 3071a1fef0b49ebe)

Fix (b251): `sync_client_minimap_origin` sets OriginActor on every USTMinimapComponent whose Outer is the PC
(objects_of_class with subclasses; `USTMinimapComponent_StaticClass` 0x050A91D0), one card058 row each.
Evidence: c492b252a client OnNewMarker calls 174 (b250: 0), host 156; all 28 paired shots with host icons shown > 0
show client icons shown > 0 (client peak 18 icons, 13 shown; c244-04-go host 8/8, client 11/9). C5M2 c493b252a client
icons 8, shown 5.
Not done: widget+0x358 reads null on both sides while calls flow, so 0x358 is not the comp pointer on
WB_Bug_Proximity_Area (diag only, no effect on the fix).
Reopen when: the client's middle HUD shows no bugs while the host's does.
