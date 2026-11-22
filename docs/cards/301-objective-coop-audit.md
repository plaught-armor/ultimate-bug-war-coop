# 301 Audit every objective for co-op: no step may assume one player
Status 2026-11-22: open, parked until after the user's playtest. Every map has a full pass, some checks are open.

User rule 2026-11-13 (during the playtest, after card 300): "need to research all objectives and make sure they
have coop in mind".

The level scripts were written for one player. Cards 288 (door waits for all players), 299 (hole open state) and
300 (tunnel closes on a player) are single finds of one class. This card finds the rest statically, before a
player does.

## Scope

Every objective and sub-objective of every Trooper (FED) map: C1M1, C2M1, C3M1, C3M2, C4M1, C4M2, C5M1, C5M2 (bug
campaign later, per the Trooper-first rule). Source: each map's level script (`FED_*_Scripting_C`), the
objective actors behind each `ActorReference`, and the trigger volumes, doors, sequences and spawners they call.
Static only: no run, no build.

## Per objective, answer these (one table row each, in `docs/reference/objective-coop-audit.md`, one section per map)

| Question | Single-player assumption to look for |
|---|---|
| Start and complete condition: which pawn does it test | `GetPlayerPawn(0)`, `GetPlayerCharacter(0)`, first overlap only, the host's controller |
| Does it close, block, collapse or unload anything behind the player | closes with a player on the wrong side (card 300) |
| Does it open something | open state reaches every player as one state, picture and collision (card 299) |
| Does it move the player: teleport, cinematic, dropship, lift, forced possession | moves one pawn, leaves the other |
| Does it need every player in one place: level end, extraction, door entry | waits for all (card 288) or carries all |
| Counted or timed parts: kills, holds, defend timers, pickups | counted once, on the host, for any player's action (card 253, card 289) |
| Per-player state it grants or removes: weapon, ammo, mech, ability, input lock, HUD marker | given to every player |
| Fail and restart: death of the player, mission fail, checkpoint | all players dead, not player 0 dead (card 294); checkpoint placement for every player |
| Spawns aimed at the player, AI targets, kill volumes, out-of-bounds | aimed at or armed around player 0 only |
| Downed, respawning or late-joining player during the step | step still completes; player placed at the live step |

Each row ends with one verdict: `ok` (with the reason), `covered by card N`, or `gap`.

## Output

- `docs/reference/objective-coop-audit.md`: index at the top (map, objectives total, gaps), one table per map.
- Gaps grouped by class, not by objective: one fix card per class with the mechanism that owns it (the shared
  all-players test of cards 288 and 300, world rows of card 098, objective rows). No per-objective patches.
- Each gap class gets a row in the STATE queue; the gameplay test checklist in STATE gains "client stays behind
  at each closing step" and "client acts first at each counted step".

## Order

After step 1 of cards 299 and 300 (they name two of the actor classes). Start with FED_C2M1, the map in play.
Report with the 286-300 report if done by then, else as its own report.

## Done when

Every objective of the seven maps has a row and a verdict; every `gap` has a card; counts per map stated as N of
M audited.

## Senior note 2026-11-14

Class F is carded as 310 (cause settled: `bReplicates 0` on the three components in b11 soak run 1; playtest logs read). Class G waits for the class A all-players test.

## Scope check 2026-11-21 (junior)

Senior: add C3M1; the scope is every FED map. The game's IoStore path list (build/utoc_paths.txt) holds eight
persistent FED maps under Maps/FED/: FED_C1M1, FED_C2M1, FED_C3M1, FED_C3M2, FED_C4M1, FED_C4M2, FED_C5M1,
FED_C5M2. The other FED umaps are sublevels of those (_Blockout, _PFX, _Collision, _StaticLightsAdd, _Excavator,
_Scripting). C3M1 was the only one missing from the list above; it is added. Maps/Testing (16 paths),
Maps/3D_Frontend and Maps/Sequences are not FED campaign maps, and Maps/BUG is the bug campaign (out of scope).
Extracted level scripts so far: build/kis/c4m1.txt and build/kis/c5m2.txt; the other six still need extracting.


## C3M1 findings 2026-11-21 (junior, static)

Source: build/kis/c3m1.txt, which is the full FED_C3M1_Scripting extract (0 decode errors). The rows are in
docs/reference/objective-coop-audit.md under "FED_C3M1": 20 rows, with the ubergraph offsets. The script has no
player-0 pawn, character or controller read. It has no SetActorLocation, no OtherActor use and no TriggerActor test.

By class:

- **H (4 sites, all switch presses).**
  - Switch C_4 @9746 closes Ramp Medium C_0 and C_1 (Beach_SO1_A).
  - Switch C_0 @21758 closes Gigantic C_1 (Beach_SO1_B).
  - Switch C_6 @9894 closes Gigantic C_2 (Beach_SO1_C).
  - The four doors reopen only at @13642, after Beach_SO4 completes, so a player shut out stays out until then.
  - PanelSwitch C_1 @17695 runs `Toggle barriers on` @12475, which raises five PylonSpline barriers around the
    Monorail finale.
  - This is the same shape as C3M2 Switch C_3 and C5M1/C5M2 switch doors, so it goes to the one class H switch
    fix: the press waits until every living player is past.
- **E (2 sites).**
  - Transport C_1 PlayerEntering @18514 completes Satcom_O1 and starts `End the level` after 13 s
    (@4548 -> @12337 EndLevel).
  - Transport C_1 PlayerSkippedFlight @19002 runs EndLevel at once.
  - Both are one-player level ends, the same as C5M1 and C5M2 (card 399/260).
- **A (2 presences, no close).**
  - C_8 @7447 completes Resort_SO1 and adds SO2. This is a marker step with low risk.
  - C_4 @13183 completes Monorail_SO1 and runs CleanUpEncounter on three encounters (SetWaves C_9/C_14, FactionLoop
    C_7). One pawn's entry ends fights the other player may still be in.
- **F shape, check (1 site).** `Splosion` @15254 shakes `GetPlayerCameraManager(self, 0)`. It is cosmetic: the
  shake reaches only player 0's camera on whichever side runs the event.
- **Checks (2).**
  - `inMonorailArea` is set and cleared by any pawn crossing presence C_19. It picks whether `Final Wave` starts at
    once or after 15 s (@11001). The script does not show whether the presence counts pawns.
  - The BeachBugs FX flags are toggled per side by presences C_9, C_17 and C_64. They are cosmetic, card 136
    class.
- **B, C, D, G: none in C3M1.**
- **Covered by existing cards.**
  - Area marker visibility (presences C_11/12/14/15/19) is covered by card 140, and the client's list follows the
    host's area.
  - EPulse pickup Beach_SO2 is covered by cards 254 and 495.
  - Satchel pickups and flak bugs are covered by cards 254 and 292.
  - Encounters and nests are covered by card 098.

Count: C3M1 has 4 objectives plus the level end. Every add and complete site found by the sweep is rowed. The
Resort_SO4 A-sub chain (@2360-@2815) is rowed as one step, not per sub.

## Senior review of the C3M1 pass (2026-11-21)

- Class H switch sites (Switch C_4, C_0, C_6): covered by the card 492 class fix (`c492_switch_press` sets the card
  288 hold for any switch press with OnPressSuccessful). No new code.
- PanelSwitch C_1 `Toggle barriers on` @12475 raises BP_PylonSpline_C_0/4/5/6/7 by `Toggle Barrier(true)`. The card
  288 hold covers `Close Door` only. Open check: does a raised barrier block a player pawn, and can a player be
  outside it at the press? If yes, it is class H and joins card 492's mechanism.
- Class E transport evac: carded as 595, with the C3M2, C5M1 and C5M2 transport sites.
- Splosion camera shake: already rowed in card 582 (C3M1 @15254-15350).
- Class A C_8 (marker) and C_4 (CleanUpEncounter, fewer bugs for the other player): ok.
- inMonorailArea: open check, same trace as the barrier (presence C_19 is the Monorail area).

## C3M1 open checks: PylonSpline barriers and inMonorailArea (static, 2026-11-21)

### Barriers (PanelSwitch C_1, `Toggle barriers on` @12475): not class H

- `Toggle Barrier(On)` (build/kis/BP_PylonSpline.txt, ubergraph @2101 -> @1544) calls `Toggle  Pylon(On)` on each
  valid entry of `Pylons` and sets the visibility of the `LightingQuads`. The quads are the beams between posts.
  SpawnHazardZones creates them with `SetCollisionEnabled(0)` (@7398), so they never collide.
- Each pylon is a BPHazardousZone_ElectricFence child actor (build/kis/BPHazardousZone_ElectricFence.txt).
  `Toggle  Pylon(On)` runs `SetHazardActive(On)` (@94), then `HazardousZone.SetCollisionEnabled(1)` (@362, QueryOnly)
  for on and `SetCollisionEnabled(0)` (@599) for off. A malfunction also switches it off (@1157).
- `HazardousZone` is the native `HazardousDetection` UBoxComponent made in the ASTHazardousZoneBase ctor (RVA
  0x05144AB0 region). The ctor binds OnBeginOverlap and OnEndOverlap and sets no collision profile. The box keeps
  the UShapeComponent ctor default, `OverlapAllDynamic` (the only profile string in that ctor). None of the three
  BPs (BPHazardousZone, BPHazardousZone_ElectricFence, BP_PylonSpline) names BodyInstance, CollisionProfileName or
  a response override.
- SpawnHazardZones sizes each box to (half span + 250, 200, 500).
- So a raised barrier is an overlap damage zone: an electric DOT through ApplyDOT and DamageActor
  (card 179: covered for clients; the cue rides card 136). It is not a wall.
- The pylon posts (SM_ElectricPylon_01) have mesh collision, but the barrier between them has none.
- A player outside the barriers at the press can always walk back in and takes the fence damage while crossing.
  The DOT amount is not read: the BPHazardousZoneData_ElectricFence values are not decoded.
- Not class H. Nothing for card 492's mechanism.
- If a playtest shows the crossing kills, it is a separate damage question, not a shut-out.

### inMonorailArea (presence C_19): new gap, last edge wins

- C_19's enter event @11531 sets `inMonorailArea = true`. Its exit event @11751 sets it false and shows the Beach
  and Resort markers again. `Final Wave` @11001 and `2nd wave` @11269 run their encounter managers only while the
  flag is set. Otherwise they retry by CallFunctionWithDelay (15 s at @11124; 20 s and 35 s at @11324 and @11354).
- Native: `ASTPlayerPresence::OnEndOverlap` (RVA 0x050F6200) has the same gates as OnBeginOverlap (G1-G6, card
  245). It then calls OnTriggerExited (vtable +0x760) for the leaving actor.
- `OnTriggerExited` (RVA 0x050FA1A0) broadcasts the exit script delegate (+0x2D8) and runs the LTOnExit reactions
  (+0x2F0, vcall +0x4E0). It clears +0x30A only for a once-exit presence. It does not check whether another player
  still overlaps.
- C_19 has no door or spawn reaction, so card 227 does not gate it. Both edges pass for every player pawn.
- Failure:
  1. Both players are inside the Monorail area and the waves run.
  2. One player steps out. Its exit sets `inMonorailArea = false` while the other player is still inside.
  3. `Final Wave` and `2nd wave` then retry every 15 or 20 s and never start until some pawn crosses in again.
     The player inside who stays put stalls the finale indefinitely.
  4. The markers also flip back to the Beach and Resort set for both players. That part is card 140 scope.
- Unmodded single player cannot hit this: leaving the area there means nobody is in it.
- Sibling with the same shape: FED_C4M1 Stadium presence C_5 (enter @5729 sets inStadium, exit @5937 clears it).
  Card 492's kC492ObjectiveTriggers row already uses that presence as its area. The other area-flag presences on
  the remaining maps are not swept yet.
- Fix direction, class fix, no per-map rows:
  - Host and client: for an ungated ASTPlayerPresence, the exit edge runs its original only when the leaving pawn
    is the last living player that overlaps it. The check is `g_c227_is_overlapping` over the other living player
    pawns, the same pawn set the card 227 gate counts.
  - The enter edge stays as it is: a second enter rewrites the same flag.
  - A once-exit presence (+0x309) needs the same rule, so its disable runs only on the last exit.
  - This belongs with the card 227 edge detour (objectives.cpp detour_c227_exited), not card 492.

## FED_C3M2 full pass (2026-11-22, senior order after b454)

The pass used the full re-extract `build/kis/c3m2.txt`, whose ubergraph runs to @28797. That re-extract depends on the kis.py UE5 EX_BitFieldConst decode, which fixed the old ERR at @26203. Every Add and Complete site is traced, along with the switches, presences, transports, SetTriggerEventsEnabled calls and the level-script visuals. The rows are in docs/reference/objective-coop-audit.md under FED_C3M2.

Coverage of the first-pass gaps:

- Class H is covered. C_52 is in kC300Closures, and the Switch C_3 lockdown is covered by the card 492 switch hold.
- Class E is covered. Card 595 holds the evac and all three mid-level rides until every player asks, and card 260 relays EndLevel.
- Presence exits are covered by card 596.
- Class A presence starts are ok, by the C3M1 precedent.
- There are no class B, D, F or G sites.

New gaps:

- **Card 601 (fixed in b455).** This is a class gap that card 595 introduced: the client never runs its own transport OnInteraction. It misses:
  - the outro conversation;
  - the flying music;
  - the C3M2 nuke LevelSequence and shake;
  - the local ride chains.

  The fix: the host sends ChanOp::DropshipBoard after its original runs, and the client then runs its own original. Its EnterDropship, EndLevel and objective calls stay skipped.
- **Card 602 (check).** In GeneratorsOnCheck, the C3M2 generator glow uses SetScalarParameterValueOnMaterials, and nothing relays it. It turns on at the Switch C_2 press, so the question is whether the client's switch copy broadcasts OnPressSuccessful. No other FED script calls it.

Every map has a full pass (updated 2026-11-22): C1M1 and C5M2 were the last. Their caller columns were wrong and were corrected the same day. Still open (see the reference doc): the C5M2 Research collapse check and the C1M1 ExecuteMusicTrigger check. User verdict 2026-11-22 on corrected facts: the C1M1 bomb zone and Traitor by a friendly kill are accepted, no card. Traitor by a host self-kill that only downs blocks the tutorial, so it is card 607.

## FED_C4M2 full pass (2026-11-22, senior order for b457)

Static pass on `build/kis/c4m2.txt` (ubergraph lines 1050-2487). Every Add and Complete site is traced to its
source, and each host-only source (encounter delegates, pickups, the boss death) is checked for effects the client
does not make itself. The rows are in docs/reference/objective-coop-audit.md under FED_C4M2 ("Full pass 2026-11-22").

- No new door, player-0, move or EndLevel sites beyond the first-pass rows. Classes C, G and H are covered (cards
  423, b212, 492); class F stays the mild player-control row.
- Card 604 class, new shapes on this map:
  - PlaySound2D from the egg chain @11831 and from LightFadeOut / ShortFadeUp @21611, @21709 (source not resolved),
    and ClearMusicTriggers in BrainBugDead @2434. b457 hooks both calls.
  - Plain `STCommonUtilityLibrary::PlaySoundAtLocationFName` (satchel pickup C_9 @20028, Brain Bug teleport in / out
    @14520, @15324). b457 does not hook it; only the WithAIAwareness form is hooked (card 099). Added to card 604.
  - `ExecuteMusicTrigger` (C_73 @10933; C1M1 @1024, @6360, @6895). Not hooked. The C4M2 source runs on both
    machines, so no relay is needed there; the C1M1 sources are to be checked under card 604.

This pass predates the C5M1, C5M2 and C1M1 full passes; the line under "Every map has a full pass" is current.
