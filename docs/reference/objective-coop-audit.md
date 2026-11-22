# Objective co-op audit (card 301)

Static audit of every objective and sub-objective on the eight Trooper maps against the ten
questions of card 301. Source: the level scripting Blueprint decompile of each map
(ubergraph offsets cited as `@N`). Each row ends with a verdict: `ok` (reason),
`covered by card N`, or `gap` (class letter, see "Gap classes").

Questions: 1 start/complete pawn test, 2 closes behind, 3 opens something, 4 moves a player,
5 needs all players in one place, 6 counted/timed parts, 7 per-player grants, 8 fail/restart,
9 spawns or kill volumes aimed at player 0, 10 downed/respawn/late join.

## Index

| Map | Objectives (sub-objectives) | Gaps | Status |
|---|---|---|---|
| FED_C2M1 | 5 (23) plus level end | 6 rows, classes A-E | done 2026-11-13 |
| FED_C1M1 | 5 plus Traitor branch and level end | F covered; G (bomb zone) and B/D (Traitor by a friendly kill) accepted by the user; Traitor by a host self-kill is card 607; ExecuteMusicTrigger check | full pass 2026-11-22, callers corrected the same day; open |
| FED_C3M2 | 4 (HydroDam, Silo, PowerPlant, Monorail) plus level end | 14 rows; classes A (ok by precedent), E and H covered; 1 new effects gap (card 601), 1 check (card 602) | full pass 2026-11-22: every Add and Complete site, switches, presences, transports and visuals traced on the full re-extract |
| FED_C4M1 | 16 rows | H and E covered (card 492 kC300Closures C_17, C_255, C_70, Stadium_SO1 close), C covered (card 423), panel switches covered (card 602); new class gap: host-only chains drive placed audio, 2D music and the horde-zone bar (card 604) | full pass 2026-11-22 (every host-only source traced) |
| FED_C4M2 | 6 (Tunnel, Plaza, Bridge, Sewers, BrainBug, plus Add Objective 2-4) plus level end | 9 + 20 rows; classes C, F (mild), G, H, card 604 audio | full pass 2026-11-22: every chain traced; new card 604 shapes (plain PlaySoundAtLocationFName, ExecuteMusicTrigger unhooked) |
| FED_C5M1 | Opening, Excavators, MunitionsBase, ResearchStation plus level end | 14 rows; classes E, H covered; new gap card 606 | full pass 2026-11-22 |
| FED_C5M2 | 9 rows | E, G, H covered; Research collapse check | full pass 2026-11-22, callers corrected the same day; open |
| FED_C3M1 | 4 (Beach, Resort, Monorail, Satcom) plus level end | 20 rows; 4 switch closes (H), 2 one-player level ends (E), 2 class A, 3 checks | first pass 2026-11-21 (door, player-0, move, presence and EndLevel sweep) |

## Gap classes

| Class | What | Proposed fix card | C2M1 rows |
|---|---|---|---|
| A | A player presence the c227 gate does not cover (no LTOnEnter door or spawn reaction) completes a step or fires a close for any one pawn, while the other player is elsewhere | Extend the card 288 all-players test to every presence that drives an objective step or a close; the host decides, the client copy is suppressed and gets a card 098 row | O05 SO05 close (card 300), level end C_53 |
| B | The script binds an objective delegate on `GetSTPlayerController(self)` (player 0). On the host that is the host's controller, so a client action never fires it | Relay the client's event to the host's player-0 delegate (or bind for every controller) | O05 SO01 MechBoarded |
| C | `BP_DestroyableActor_*` state (`LD: Destroy`, mesh swap, HideActor) has no row class and no digest entry | Card 299/300 class fix: host relays `LD: Destroy` as a card 098 world row; digest ext row IsDestroyed | O05 blockers, HiveEntrance, nests |
| D | A script handler moves or hides the overlapping actor (`K2_SetActorLocation` on OtherActor). Each machine's copy moves the pawn it sees, so the host moves the client's pawn copy without the client's own movement agreeing | Host-only handler, result sent as a placement row to the owning client (card 300 placement rule) | Evac TriggerBox_3/_0 |
| E | Level end fires 32.5 s after one presence entry, with no check that every player is aboard | Part of class A: evac completes only when all living players are in C_53; card 260 relays EndLevel | Level end |
| F | The script writes per-player state on `GetSTPlayerCharacter(self)` (player 0) only: gameplay tags, faction, squad leader, inventory removal, tactical support class, movement/input, teleport. On the host that is the host's pawn, so the client's pawn never gets it | One class fix: host hook on these script calls (imp577 result used as a write target) repeats the write for every player pawn, sent to the owner as a per-player row; teleports go through the card 300 placement rule | none (C1M1 rows) |
| G | The script tests a player position or identity against player 0 only (`IsPointInBox(player0 location, ...)`, `OtherActor == player0`). A client in the zone never counts | Extend the card 288 all-players test: the test passes for any living player pawn (or all, per step) | none (C1M1 rows) |
| H | A script step (presence entry or a switch press) runs `Close Door` on doors behind the players, with no all-players test; the other player can be shut out | Add the presence to kC300Closures (card 300); a switch twin waits until every living player is past the doors | - (C3M2: C_52, Switch C_3) |

## FED_C2M1

| Objective / step | How it starts / completes | Questions hit | Verdict |
|---|---|---|---|
| O01 add | Setup_Objective01_Intro @5710 at level start | - | ok (no pawn test) |
| O01 SO01 | Completes when encounters StartEncounter_01 and _02 (SetWaves C_77, C_5 @5819/@5995) finish; then ForceMoveNPCSToArena | 6 (two encounters) | ok (encounter end is host AI state, sent by card 098 rows) |
| O01 SO02 | Added @21048 (Setup_Objective01_Subobjective_02, presence C_54) | 1 | covered by card 227 if C_54 has a spawn reaction; else class A (check host log) |
| O01 complete | @6011 | - | ok |
| O02 SO01 / SO02 | Presences C_4 / C_5 move the marker (ref C_15) | 1 | ok (marker only, no close or spawn) |
| O02 SO04 | NukesLarge pickup C_1, pickup delegate @10855 | 7 | covered by card 254 (pickup runs on the host for both takers); per-taker weapon credit is card 289 |
| O02 SO05 | Count of 2 nests: Bugnest C_7 / C_0 BuildingDestroyed @11106 / @11331, PlasmaBugCount == 2 | 6 | covered by card 098 K rows for the kill; nest mesh state is class C (card 299) |
| O02 complete | OnLevelObjectiveComplete bind | - | ok |
| O03 SO02 | Count of 3 satchel pickups C_2 / C_3 / C_5 (@7721 / @7759 / @7797) | 6, 7 | covered by card 254; client act-first case on the checklist |
| O03 SO03 | Count of 3 flak bugs (SatchelBuilding_Flakbug C_0 / C_2 / C_4 BuildingDestroyed) | 6 | covered by card 292 (satchel site); join-time count add is card 305 (b) |
| O04 SO01 | Presence C_3 completes @9915 | 1 | ok (marker step; no close) |
| O04 SO02 | Ref C_4; completes @2319 after SetWaves C_70 / C_71 encounter delegates; StaticMeshActor_97 / _95 `LD: Destroy` | 3, 6 | class C for the two `LD: Destroy` actors |
| O05 SO01 | Board mech: MechBoarded bound on `GetSTPlayerController(self)` @18160, completes @2982-3033 | 1, 9 | gap B (client boarding never fires the host's player-0 delegate) |
| O05 SO01 add | BugBlocker_C_3 / _5 / _7 `LD: Destroy` (HideActor) at the add @13016-13088 | 3 | gap C (card 299 candidate b) |
| O05 SO02 | Refs C_23 / C_25 / C_27, completed by presences C_25 / C_27 / C_75 (@14805 "Enter the Ethan saving tunnels") | 1, 5 | class A for presences outside the c227 gate |
| O05 SO03 | Ref C_21, completed via presence C_77, then conversation | 1 | class A (no close; marker only, low risk) |
| O05 SO04 | Ref C_9 | 1 | ok (marker) |
| O05 SO05 | DefendArea ref C_14 @15225; HiveEntrance `LD: Destroy` plus HordeZone_C_3 Begin KillEmAll wall when any pawn is in C_78 / C_79 / C_81 after EthanFound | 2, 5, 9 | gap A + C (card 300) |
| O05 SO05 end | Presence C_53 @21392 → @15277 Conversation_010_Outro, HordeZone_C_3 End KillEmAll, CompleteObjective(LOS_O05) @15327 | 1, 5 | gap A (one pawn completes the defend and starts evac) |
| O05 SO06 | Evacuate, ref C_13, added @15534; dropships C_17 / C_18 OnLanded @19176 / @19467 force NPCs to arenas and enable evac overlaps @19246 | 6 | ok (timed by host dropship actions) |
| Evac overlaps | TriggerBox_3 @18757-18844 and TriggerBox_0 @20087-20174: Trooper-tagged OtherActor is hidden and `K2_SetActorLocation` to ActorReference_C_1 | 4 | gap D |
| Level end | @15351 → @8168 "PLAYERS IN DROPSHIP", EvacTakeOff1/2 after 1 s, `End Level` after 32.5 s @8254 → STGameInstance:EndLevel @23094; EndLevelEvacuationDropships after 10 s @15743 | 5, 10 | gap E (no all-aboard check); EndLevel reaches the client by card 260 |
| Debug keys | M / N / B / O / K input debug events | - | ok (not gameplay) |

No `GetPlayerPawn` / `GetPlayerCharacter` calls in the C2M1 script; the only player-0 binding is MechBoarded (class B).
Fail/restart (question 8) and downed/late join (question 10) have no script-side branches on C2M1;
restart is the card 294 travel path.

## FED_C3M2

Source: `kis.py` dump of FED_C3M2_Scripting (ubergraph offsets `@N`). Every presence below is a level-script
delegate (`BndEvt ... VolumeTriggerDelegate`), so none is under the c227 gate (no LR_Door / LR_SpawnAI reaction) and
none is in kC300Closures. New class H: a script step closes doors behind the players (question 2) with no
all-players test.

| Objective / step | How it starts / completes | Questions hit | Verdict |
|---|---|---|---|
| Silo intro, Silo_SO1 | Presence C_2 @2222: dialogue, Setup Objective 2_02, @2148 CompleteSubObjective(Silo_O1, Silo_SO1), EM Loop C_20 trigger | 1, 9 | gap A (one pawn starts the encounter and completes the step) |
| Silo lockdown | Switch C_3 handler @5595: opens silo hatches C_1/2/5, jams C_0/3/4, four SetWaves EMs, then @6057-6129 Close Door on Gigantic C_2, Medium C_1, Medium C_2; opens Gigantic Vertical C_0 and Gigantic C_3; Setup Objective 2_03 | 2, 3, 9 | gap H (a switch press by one player shuts three doors; the other player may be outside). Reopened later @7698-7842 |
| Dialogue presences C_4, C_8 | @4556, @14122 | - | ok (dialogue only) |
| Dropship encounters C_10, C_9 | C_10 @16550: EM_Dropship C_9/C_11 (and @3500 C_3/C_6); C_9 @16974 SetWaves C_7 | 1, 9 | gap A |
| Door breaches C_12, C_14 | C_12 @17027: EM C_51 plus InstaKillTarget(Door_Sliding_Gigantic_C_12); C_14 @17032: EM C_53 plus InstaKill Door_Gigantic_C_9 | 1, 3, 9 | gap A (opens the way; no trap) |
| Markers C_15, C_16 | C_15 enter @17037 / exit @17080 marker show/hide; C_16 enter @17869 / exit @17976 markers, StopEncounter C_31/C_34 | 1 | ok for the step (marker only); C_16 stops encounters on one pawn's exit: gap A (mild) |
| Monorail_SO2 | C_51 enter @20784: markers, Objective_Monorail_SO2(), conversation. Parts A-D: Bugnest BuildingDestroyed delegates C_0/1/5/9 (count 4) | 1, 6 | gap A for the start; nest count ok (host nest state, card 241) |
| PowerPlant start | C_53 @21768: if PowerPlant_RearComplete, EM C_6, trigger disabled, PowerPlant_InitialEncounterDelay | 1, 9 | gap A |
| PowerPlant_SO1 / SO2, doors close | C_52 @24446 -> @15107: SetSwitchExternallyEnabled(Switch C_2), Setup Objective 3_02 (@10811 -> @9225): AddSubObjective PowerPlant_SO2 (switch C_2), then @9296-9368 Close Door on Gigantic C_8, C_7, C_10; then @2373 CompleteSubObjective PowerPlant_SO1 | 1, 2 | gap H (one pawn in C_52 shuts three gigantic doors; add C_52 to kC300Closures, or the card 288 all-players test) |
| PowerPlant_SO3 DoorSwitch1/2 | Switch C_6 / C_8 count @9405-9552, completes @15145 / @15183 | 6, 7 | ok (switch interaction runs on the host; check a client press reaches it, card 254 class) |
| HydroDam_SO5 defend | @10431 AddSubObjective, missiles, 90 s EvactuationArrivalDelayedFunction, three Loop EMs | 6 | ok (host timers) |
| Evac, level end | Transport dropship C_2 OnPlayerEntryRequested -> EDropship_Interacted @12862: SetIsInteractable(false), flight StartAction, EvacuatDropship_Interacted @13048 -> CompleteObjective(HydroDam_O1), Dialogue_Outro, NukeExplosion 13.5 s, EndLevel 21.5 s (@13680 STGameInstance:EndLevel) | 5, 10 | gap E (one player's entry request flies the ship and ends the level; card 399 boards every pawn only if the host's EnterDropship runs; card 260 relays EndLevel) |
| Mid-level transports C_0, C_1, C_3 | PlayerEntering binds: C_0 @26665 (SetIsInteractable(false), flight @12772), C_1 @26598 (flight @14380), C_3 @26732 (flight @22712, StartAction @22754). OnPlayerEntryRequested binds -> Dropship_*_Boarded -> @12215 Setup objective 4_02 -> @3092 CompleteSubObjective HydroDam_SO1; dam landing @121/@341 completes HydroDam_SO2 | 4, 5 | covered: card 595 gate (all players ask) plus card 399 (every pawn seated); client local chain: card 601 |
| Evac skip | PlayerSkippedFlight C_2 @25324 calls EndLevel(); C_4 skip @25899 prints only | 5 | covered: card 595 (no possession before all ask), card 260 (only the host ends) |
| Switches | Switch C_0 @6386 -> @1805 MissleSilo Prepare For Launch x5, EM C_45; WallSwitch C_1 @23472 Setup Objective 4 Monorail, Monorail_SO3, EMs C_27 / C_22; PanelSwitch C_1 @27793 -> @16932 audio and missile fire @15897; STSwitch OnPressSuccessful binds @24456-24607: Switch C_2 Plant_Generator_Reset @14743 (GeneratorsOn, PowerPlant_SO2 @14848), C_6 / C_8 Plant_RearDoor1/2 (DoorSwitch1/2 @15145 / @15183, close nothing) | 7, 9 | ok (host runs the relayed press); Switch C_3 lockdown closes: card 492 switch hold (door_c161.cpp c492_switch_press) |
| Silo locks | Lock1-3 @5481-5557 via Silo_*_ClaspDestroyed (MissleSilo "Hatched Opened" delegates) | 6 | ok (host actor state) |
| Encounter-driven steps | PowerPlant / Silo EncountersEnded -> @12026 / @10696 (HydroDam_SO3), @12239 (SO4); EvactuationArrivalDelayedFunction @26780 -> SO5 @12612; EM C_40 @6949 PowerPlant_SO4; EM C_30 / C_50 -> @10830 CompleteObjective PowerPlant_O1 @11258; OnLevelObjectiveComplete -> @5261 Monorail_SO2; Dialogue00_Silo_Site_MissleFire -> @4420 CompleteObjective Silo_O1 @4531; SilosStage4 delay @4073 -> @9083 Silo_SO5; Objective_Monorail_SO2 -> @21195 Monorail_SO1 | 6 | ok (host encounters and timers, card 098) |
| SetTriggerEventsEnabled | Disables C_16 @2696, C_15 @7940, C_51 @11585; enables C_12 @10272, C_14 @10377 | 1 | ok; enables covered by class G hook `detour_c301_exec_enable` (players already inside) |
| Pickups | SecretPickup C_2 @16560; Autocannon @28753, EPulse88 @26166, Railgun @28758 | 7 | covered by card 254 / 289 / 495 |
| Outro visuals | EDropship_Interacted @12862 -> Dialogue_Outro, NukeExplosion @26203 (CreateLevelSequencePlayer @26243, Play @26495), NukeExplosion_Shake @26528 (StartCameraShake), flying music | 10 | new effects gap: card 595 skips the client's original, so its chain never runs; fixed in b455 by card 601 (host DropshipBoard row, client runs its own original) |
| Generator glow | GeneratorsOnCheck @26829-27788: 12 SetScalarParameterValueOnMaterials (StaticMeshActor_12/16/8/9/2) at BeginPlay on both, then on the Switch C_2 press | 10 | check: card 602 (does the client's switch copy broadcast OnPressSuccessful) |
| MonorailVisibility | @16128 GetAllActorsWithTag, SetActorHiddenInGame(false) @16361, from BeginPlay @7263 | - | ok (both machines run it) |

Full pass 2026-11-22 on the full re-extract (build/kis/c3m2.txt, 2029 lines, kis.py decodes UE5 EX_BitFieldConst
0x11, ubergraph to @28797). Class closures since the first pass: H rows are covered (C_52 in kC300Closures,
objectives.cpp; Switch C_3 by the card 492 switch hold); E by card 595 (all players ask, three rides and the evac) and
card 260; presence exits by card 596 (exit runs only when the last living player leaves; C_16's ResumeEncounter);
class A presence starts (C_2, C_9, C_10, C_12, C_14, C_51, C_53, C_16 enter) are ok by the C3M1 precedent (marker
steps and encounter starts by one pawn; encounters card 098, markers card 140). No class B, D, F or G sites: no
player-0 hash, no SetActorLocation, no OtherActor or TriggerActor test past the bound parameters. Debug keys N @1234,
L @23759 are not gameplay.

## FED_C4M2

Source: `kis.py` dump of FED_C4M2_Scripting. The c245 soak (b194) completed 32 objectives with both players, so
these rows are about the second player's position, not about steps that never finish.

| Objective / step | How it starts / completes | Questions hit | Verdict |
|---|---|---|---|
| Plaza doors close | Presence C_73 @10933 -> @9746: Close Door on Medium C_1, C_2, C_3, C_4, Toggle Plaza Flares(false), CleanUpEncounter C_35/C_37. Reopened @22980-23064 | 1, 2 | gap H (one pawn in C_73 shuts four doors; C_73 is script-bound, not in kC300Closures) |
| Plaza_SO2b, Gigantic C_2 | @5066 AddSubObjective Plaza_SO2b, Complete Plaza_SO2a, five AIKillableStructures damageable, @5339 Close Door Gigantic C_2, UpdateDamageable, two EMs. Opened @4958 after the arena cleanup | 2, 9 | gap H if a player can be outside Gigantic C_2 at Plaza_SO2a completion (trigger chain into @5066 not traced) |
| Plaza_SO2a | @9577 AddSubObjective with Switch C_0 | 7 | ok (switch on the host; card 254 class for a client press) |
| BrainBug start | @20514 SetTriggerEventsEnabled(C_176, true), then @20547 GetPlayerCharacter(self, 0) and IsOverlappingActor(C_176 TriggerArea): only then Brainbug Start Immediately; else the C_176 enter starts it | 1 | gap G (player 0 only: a client already in C_176 does not start the fight; it starts when the host's pawn or a new entry arrives) |
| Player control cut | @11892 fade, @11929 Toggle Player Control(false): @33006 caches player 0 location and control rotation in CachedTPLoc. @18354 Toggle Player Control(true): @32759-32943 SetControlRotation and K2_SetActorLocation on player 0 | 4 | gap F (mild): only player 0 is restored; the client is not moved either way, so it stays wherever the cut left it |
| Bridge collapse | @25923/@26001 and @29502/@29580 K2_SetActorLocation on StaticMeshActor_14 and _32; @29659-30991 LD: Destroy on about 35 BridgePart actors, cars and StaticMeshActors | 3 | class C: covered by the card 300 LD: Destroy row if each part has a row; the StaticMeshActor moves are world state for the digest (card 481 MoveableRoad is open on this map) |
| Eggs, nests | EggDestroyed delegates on about 80 egg actors; nest C_5 Activate @1593; PlasmaBugNest LD: Destroy @1803, @18108, @18182 | 6 | ok (host destruction, relayed by the card 098 / 241 rows) |
| Satchel sites C_1-C_4, satchel pickups C_3/5/7/9 | SatchelPlacementSiteEvent and ADPickupDelegate handlers | 6, 7 | covered by card 254 (pickups on the host); satchel placement by a client: check a client plant reaches the host site |
| Level end | CompleteObjective BrainBug_O1 @1743, CallFunctionWithDelay(21.0, EndLevelDelayed) -> @1710 EndLevel | 10 | ok (host decision after the boss dies; card 260 relays EndLevel) |

Full pass 2026-11-22 (build/kis/c4m2.txt, ubergraph lines 1050-2487): every objective chain traced to its source,
with the host-only sources (encounter delegates, pickups, the boss death) checked for effects the client does not
make itself.

| Objective / step | How it starts / completes | Verdict |
|---|---|---|
| Tunnel_O1 | `Setup 1st Objective` @31068 adds O1 and SO1; Presence C_6 chain completes it @1569 | ok (presence runs on both; objective state from the host) |
| Plaza_SO1 | Add Objective 2 @31289; Presence C_24 @9263 completes SO1 @9628, adds SO2a, stops encounters C_19/C_20 | ok |
| Plaza_SO2a -> SO2b | SetWaves C_39 EncounterDelegate @4530 (host only): Complete SO2a @5117, Add SO2b @5066, AIKillableStructures, Close Door Gigantic C_2 @5339, Bridge_Explosion1-4 delays @6015-6129, STSpawnNiagraSystemAtLocation @5630-6223 | door via DoorOp; Niagara spawns covered (card 136); Gigantic C_2 close is the existing gap H row |
| Plaza_SO2b -> SO3 | SetWaves C_35 EncounterDelegate @3900 (host only): Complete SO2b @4188, Add SO3 @4137, trigger EM C_19/C_37, Open Door Medium C_1-C_4 @3973-4081 | ok (host state; doors via DoorOp) |
| Plaza_SO3 | LightFadeOut @21542 -> ShortFadeUp: PlaySound2D @21611, @21709, BugTrooperDelay1-4, Complete SO3 @21784 | gap, card 604 if the LightFadeOut source is host only (source not resolved; PlaySound2D is hooked by card 604) |
| Plaza_SO4 | Egg Flat_Destructible_01_C_0 adds SO4 @12796; egg GlowingEmissive_01_C_0 EggDestroyed @11816: PlaySound2D @11831, Toggle Player Control(false) @11929, FadeUp after 3 s, Door3 -> 4 -> 2 -> 1 delays, Complete SO4 @12407 | egg death from the host (card 098/241); PlaySound2D gap, card 604; player control is the existing gap F row |
| Plaza_SO5, Plaza_O1 | DelayLights (from Presence C_17 @6442) adds SO5 @23227; SetWaves C_40 EncounterDelegate @27085 (host only) completes Plaza_O1 @27090 | ok |
| Presence C_17 / C_14 / C_73 audio | C_17 @6474 ClearMusicTriggers; C_14 @6579 PlaySound2D; C_73 @10933 ExecuteMusicTrigger(0, 60.0, true) | presences run on both machines: each side makes its own sound. ExecuteMusicTrigger is not hooked by card 604 (no relay needed while its source runs on both) |
| Bridge_SO1 -> SO3 | Add Objective 3 @31382; Presence C_23 @9042 completes SO1, adds SO2; Setup_Objective_Bridge_Sub03_Satchels completes SO2 @19572 and adds SO3 (count 4); SO3a-d added and completed @18473-18899 from the satchel site events @20431-20446 | ok (satchel pickups on the host, card 254; the placement row above stays open) |
| Satchel pickup C_9 | ADPickupDelegate @19796 (host only): PlaySoundAtLocationFName MS_EMIT_BrainBug_Teleport_Indicator @20028, Niagara spawn @20332 | Niagara covered (card 136); the sound is a gap, card 604: plain PlaySoundAtLocationFName is not hooked (only the WithAIAwareness form, card 099) |
| Bridge_SO4, Bridge_O1 | Bridge_SO3_Complete (OnLevelObjectiveComplete bind @19419) adds SO4 @19128; BridgeExplode (Dialogue_BrainBug_Fight_Stage02_Start + 3 s) completes SO4 @3592 and O1 @3629, then markers, Presence C_21 off, BridgePostExplosionSave, ExplosiveCar C_20/C_21 Destroy, EM C_19 / SetWaves C_37 trigger | ok; Destroy covered (card 423) |
| Sewers_O1 | Add Objective 4 @31475; Presence C_19 @8741 completes SO0 @8968, adds SO1-3; egg-destroyed chains complete SO1-3 @15/@441/@1233 (EggVision Toggle Off, CheckPointSaveZone); EggsOutro completes O1 @7014; Presence C_22 @8380 adds markers | ok (egg deaths from the host) |
| BrainBug_O1, SO1 | SetUpBrainBug @2920 (after Dialogue_BrainBug_Intro) -> BrainBugObjective adds O1 @20671; Presence C_176 @15401 completes SO1 @15457, TriggerSpecificMusic MSS_MUS_Boss_BrainBug @15606, adds SO2 (L1, L2, R1, R2), triggers EM Loop C_6/4/2/1 | ok: C_176 runs on both (player-0 start is the fixed gap G row, b212) |
| BrainBug_SO2 | SetWaves C_6/C_12/C_18 EncounterDelegates @16994/17036/17078 (host only) complete R2/R1/L1; OnLevelObjectiveComplete_Event @17399 completes L2 and SO2, adds SO3 | ok (objective state from the host) |
| BrainBug_SO3 -> SO7 | SetWaves C_1 EncounterDelegate @17661 (host only) completes SO3 @18071 and SO5 @18145, PlasmaBugNest LD: Destroy; BrainBugDropshipDelay @26554 (OnActiveDropshipLanded + 5 s) adds SO4 and SO6; SO5 added and SO4 completed @16717-16768; End Centurion Phase @13866 completes SO6, adds SO7 | ok; LD: Destroy covered (card 423) |
| Brain Bug teleport sounds | PlaySoundAtLocationFName MS_EMIT_BrainBug_Teleport_In @14520, _Out @15324 | gap, card 604 if the teleport events are host only (boss AI): same unhooked call as the satchel row |
| BrainBug death | BrainBugDead: ClearMusicTriggers @2434 | gap, card 604 (boss death is host only; ClearMusicTriggers is hooked, so the b457 C row covers it) |
| Plaza single doors | SetWaves C_42-C_45 delegates @11087-11525 open and close single Medium doors | ok (host; DoorOp) |
| Hides, Niagara | SetActorHiddenInGame on StaticMeshActor_52/180-188/70-77/132/72/1/12 @21399-25881; Niagara Activate / Deactivate on NiagaraActor_6/10/25/37-40, BP_Flare Deactivate | covered (hb46; card 136) |
| Switch C_0 | @26080 -> Loop Plaza Siege Threats | ok (switch class, card 602) |

Class closures (full pass 2026-11-22): classes C (card 423), H (rows above; Medium C_1-C_4 by card 492), G (C_176,
b212) and the effect classes covered by card 136 and hb46 have no new sites. Class F stays the mild player-control
row. The card 604 class has three new shapes here: PlaySound2D and ClearMusicTriggers from host-only chains (hooked
in b457), and plain `PlaySoundAtLocationFName` from the satchel pickup and boss teleports, which b457 does not hook.
`ExecuteMusicTrigger` (C_73, and three sites on C1M1) is also not hooked; its C4M2 source runs on both machines.

## FED_C5M1

Source: `kis.py` dump of FED_C5M1_Scripting (build/kis/c5m1.txt; 17 presence handlers). First pass 2026-11-18 (door,
player-0, move and EndLevel sweep); full pass 2026-11-22 (senior) traced every effect call in the ubergraph to its
source and checked each against the class fix that covers it. BP_HordeZone was read from its own listing
(build/kis/hordezone.txt).

| Objective / step | How it starts / completes | Verdict |
|---|---|---|
| Switch C_5 doors | SwitchPressedDelegate C_5 @29072 -> @28419: Close Door on Gigantic C_17, C_15, C_16, then @23143 | H, covered (card 492 fix 2: a switch press sets the card 288 trigger; door_c161.cpp c492_switch_press) |
| TriggerBox_1 kill volume | ActorBeginOverlap @18894: an OtherActor with one of the NPC trooper tags (TrooperGrunt, MedicTrooper, RadioTrooper, TrooperShotgun, TrooperSquadLeader, MechSuit, TrooperMedic) is hidden, moved to ActorReference_C_12 and InstaKillTarget | ok: a player pawn carries Faction.Human.Player (card 020), not in the list; a player-piloted mech would die on the host's kill as in solo |
| TheLastNPCs | @4374 IsFriendFaction(arena NPC, GetSTPlayerCharacter) | ok (both players share the faction) |
| Excavator A | Switch C_1 @29062 -> @28400 -> @8418: objectives, conversation, Excavator turn, encounters, `TriggerLoopingWave` after 2 s -> HordeZone C_1 Begin KillEmAll(false) @26012 | ok: card 602 runs a host switch press on the client copy, so the chain runs on both |
| Excavator A end | SetWaves C_0 EncounterDelegate (host only) -> @26200 -> @26150 Update Progress Bar(50) -> @16475: CompleteObjective ExcavatorA_O1, markers, HordeZone C_1 End KillEmAll @16762 | gap, card 606 (the client's wall, fog, horde Niagara and HordeAudio stay on); the progress bar has no widget (ShowUI false) |
| Excavator B sawing | Switch C_2 @29057 -> @9059 and Switch C_3 @29067 -> @9969: Excavator turn, DisplayPanel `LD: Change Screen`, `StartExcavatorBTimer` -> TickBProgressBars -> UpdateAllScreensProgressBar_B, FinishSawing after ExcavatorBSawingTimeMax | ok (switch chains run on both machines, card 602; the timer and screens are local on each) |
| NukeLauncher pickup | ADPickupDelegate @11107 (host runs pickups, card 254) -> Plasma Bug 1 setup, objectives, switch enables | ok (host objective and switch state) |
| Satchel and generator buildings | Become Destroyed Flakbug C_0-C_3 @6200-6311, BuildingDestroyed Generator C_0/C_1, Plasmabug C_2/C_3 | covered (world_c98 building state, card 408 satchels) |
| Effects in host-only chains | LD: Destroy (PlasmaBugNest, PlasmabugShield), SkeletalMeshActor hides @29991-30261, Niagara Activate(false) @2292/@2425, BP_Flare Activate @2985, TriggerAudio C_1/C_5/C_6, TriggerSpecificMusic, ClearMusicTriggers, conversations | covered: card 423, hb46, card 136, card 604 (placed audio and music), card 322 |
| Doors and mechs | Open Door rows (@2666-@20417), MechDropPlatform C_0/C_1/C_3/C_4 Activate, Door Gigantic C_6 InstaKill @20660 | ok (DoorOp; mechs are replicated, card 075) |
| AA turret spawns | @14182-15338 BeginDeferredActorSpawnFromClass at ActorReference transforms (Setup AA Turret) | ok (deferred spawn on the host replicates, card 140) |
| ResearchStation_O1 | @19590 CompleteObjective from @29082 | ok |
| Level end | Transport dropship C_0 PlayerSkippedFlight @29092 and DropshipEventDelegate @26086 -> @26091 EndLevel; FlightPath_End -> @26145 -> same | E, covered (card 595: the release waits for every living player; card 260: only the host ends; card 601: the client runs its own outro) |
| Debug keys | H, I, J, L, Up and others | not gameplay |

Class closures (full pass 2026-11-22): H covered by card 492, E by card 595, the switch chains by card 602, effects by
cards 136, 322, 423, 604 and hb46. No class B, D, F or G sites. One new class gap: BP_HordeZone Begin / End
KillEmAll has no replicated state, so a call from a host-only source changes the host's arena wall only. It is called
on seven maps; card 606 holds the sites and the relay fix.

## FED_C4M1

Source: `kis.py` dump of FED_C4M1_Scripting (build/kis/c4m1.txt; 21 presence handlers, 0 decode errors).
First pass 2026-11-18 (door, player-0, move and EndLevel sweep); full pass 2026-11-22 traced every chain whose
source runs on the host only: encounter delegates, custom events called from them, pickups and switches.

| Objective / step | How it starts / completes | Verdict |
|---|---|---|
| Stadium_SO1 -> Stadium_SO2 | Objective event `Add Stadium_SO2` @6586 (Stadium_SO1 counts kills A-D) adds SO2, starts Stadium Fight 1 after 20 s, closes Medium C_2, Medium C_1, Gigantic C_1 | H, covered (card 492 fix 6, objectives.cpp Stadium_SO1 close row) |
| Stadium panel switches | PanelSwitch C_0-C_3 SwitchPressedDelegate @15008 -> CompleteSubObjective Stadium_SO1_A-D, speaker Edit Volume / PowerOn, Stadium Switch Press Handle Music | covered (card 602: the client copy runs its own OnInteraction) |
| Stadium fights, Half Time Show | Stadium Fight 1 -> 2 (@15136) -> Half Time Show (@15255, @20079): panels 70/100, halftimeReady, music, sparklers and graphics on, Add Stadium_SO4 after 5 s; @15115 InstaKillTarget Door_Sliding_Gigantic_C_3 | ok (objective event chain, runs on both machines; the door state comes from the host) |
| Stadium end | SetWaves C_2 EncounterDelegate @8928 (host only): CleanUp, IncrementObjectiveCount, CompleteSubObjective Stadium_O1, presences C_31/C_5 off, MechDropPlatform C_1 Activate, Handle Music, Add Metro Objective; sparklers and graphics off @9674-9711 | gap, card 604 (music and sparkler StopAudio on NiagaraActor_0/17-29); the Niagara stop (card 136) and the graphics hide (hb46) are covered |
| Metro_SO1 -> Metro_SO2 | `BP_PlayerPresence_C_17` enter @14243 -> @13363 closes Large C_3, Large C_2, Medium C_3, Medium C_4 | H, covered (kC300Closures C_17) |
| Metro frontage | SetWaves C_11 EncounterDelegate @17938 -> @17278 HordeZone C_1 End, Add Metro_SO5 after 2 s -> @16250: objectives, TriggerAudio C_7, DestroyMetroFrontage (StaticMeshActor and DecalActor hides), Floor C_1 LD: Destroy | hides covered (hb46), LD: Destroy covered (card 423), TriggerAudio C_7 covered (card 604); HordeZone C_1 End gap, card 606 |
| Horde zone bar | SetWaves C_11 EncounterIndexDelegate @19934: BP_HordeZone_C_1 Update Progress Bar 0.3/0.6/1.0, host only | no effect: every Begin KillEmAll passes ShowUI false, so the bar widget is never created (card 606) |
| Metro / Bridge encounters | SetWaves C_3/C_4/C_5 (@14119-14382), C_8 @17669, C_14 @17865, C_7 @16042 -> @10544: objectives, CleanUp, Trigger EM, Open Door Large C_0, FallZones C_8 off | ok (host state; doors via DoorOp) |
| Bridge step | `BP_PlayerPresence_C_255` enter @19570 closes Large C_0 | H, covered (kC300Closures C_255) |
| Bridge collapse | SetWaves C_13 @19611 -> Add Bridge_SO4 -> Bombing Run @11083 (deferred spawn, card 140) -> Destroy Bridge 1-3 (BridgePart3 C_1, BridgePart4, Bridge C_0 LD: Destroy) -> Toggle Bridge Nav @19221 (BoxOverlapActors, InstaKillTarget, SetActorHiddenInGame on the found pawns) | ok: LD: Destroy covered (card 423); kills and hides land on replicated pawns; ReceiveLoaded @18635 runs Toggle Bridge Nav(false) on both |
| Park | Bugnest C_4-C_7, C_11 (@7170-7296, @17860): CompleteSubObjective Park_SO2_A-E | ok (host objective state) |
| Satchel / LZ | Satchel Flakbug C_0-C_2 (@17397-17479): End Encounter Manager; Rubble C_2 @24379 -> @19735 Add LZ_SO2, Spawn AISpawner C_5 | ok (host state) |
| EPulse88 pickup | WeaponPickup delegate @18778 (host runs pickups, card 254): BP_Flare_C_0 Deactivate, EndMusic / ClearMusicTriggers, StopAudio TriggerableAudioEmitter C_0, PlaySound2D | gap, card 604 (music and emitter stop; the flare Niagara stop is covered by card 136) |
| Fireworks | Delay 18 s from @114 in the ubergraph start block | ok (both machines) |
| Level end | `BP_PlayerPresence_C_70` enter @16245 -> @13797 EndLevel | E, covered (kC300Closures C_70: EndLevel waits for every living player) |
| Debug keys | Delete, H, Left, M, N, Right, Up | not gameplay |

Class closures (full pass 2026-11-22): classes H and E are covered by card 492, class C by card 423, and the panel
switches by card 602. There are no class B, D, F or G sites. One new class gap: chains with a host-only source
(encounter delegates, pickups) drive placed audio components (BP_TriggerableAudioEmitter, NiagaraActor audio),
2D music and the horde-zone progress bar, and none of these is relayed. Card 604 holds the sites and the fix
options; the proposed class fix is an effect relay on UAudioComponent Play/Stop plus a music row.

## FED_C5M2

Full pass 2026-11-22 (senior). Source: `kis.py` dump of a full extract of FED_C5M2_Scripting (7145313 bytes, 31
presence handlers, 0 decode errors), `build/kis/c5m2.txt`. Every ubergraph effect call (hide, move, Niagara, audio,
music, `LD: Destroy`, `Close Door`, damage, player-0 read, EndLevel, HordeZone) was mapped to the entry event that
reaches it (90 sites). The first mapping used the nearest preceding entry, which is wrong for shared `goto` blocks.
The rows below were corrected by reachability from the events. Client coverage was checked only for the rowed
sites; the other sites have no verdict.

| Objective / step | How it starts / completes | Questions hit | Verdict |
|---|---|---|---|
| Switch doors | `BP_PanelSwitch_WithoutScreen_C_1` SwitchPressedDelegate @14353: Close Door on Large C_4, C_1, C_5, C_6, C_7, C_8, then FallZones C_6/C_10/C_9 on | 2 | H, covered (card 492 row FED_C5M2, done) |
| Cave in | Presence `BP_PlayerPresence_C_408` enter @30571 sets `caveInReady`; event `Cave in` @31328 re-arms every 30 s (@32214): Niagara burst and a 20 damage, 1500 radius blast at `GetSTPlayerCharacter(self)` | 4, 9 | G, covered (card 493, done) |
| Pillar blasts (Finale_SO4) | SatchelPlacementSite C_4/C_5/C_6 SatchelExplosionEvent @25863/25882/25901 -> `DestroyPillarA/B/C` @25920: ApplyDamageAtLocation (500 damage, 1500 radius) at the placement site with `GetSTPlayerCharacter` as instigator, `MS_EMIT_C5M2_Assassin_Cave_Explosion_01` at the site | - | ok (the blast point is the site, not a pawn; player 0 is only the instigator credit) |
| Satchel pickups | `Set satchels` @17096 (root ReceiveLoaded only) moves BP_SatchelItemPickup_C_1/2/3 (K2_SetActorLocation); `Revert Satchels` @34821 moves them back and is called only from ReceiveLoaded | 4 | ok (placed actors, carried by the card 140 'A' row; c247b311a: the client's use target for C_2 was found 0 uu from the host's position) |
| Checkpoint restore | ReceiveLoaded @24219 replays saved state: Toggle Solar Light, Toggle c4 Flares, Set/Revert Satchels, Array screens, Set Nav Blocker, Hide Hoppers, `Research collapse(ResearchHoleCollapsed)` | - | ok (load path) |
| Research collapse | `Research collapse` @376: roots SetWaves C_1 and C_5, PanelSwitch_WithScreen C_3, ReceiveLoaded, debug Delete | - | check: StaticMeshActor_25's hide reaches the client (c247 `world H` row). No client row was found for its collision or the two Niagara actors; Set Nav Blocker and the InstaKill list are not traced |
| Outpost bug waves | `Outpost Bug Waves` @3331 (roots SetWaves C_9, presence C_402) runs HordeZone C_1 `Begin KillEmAll`; `EndWhiskeyHordeZone` @21630 (root SetWaves C_5) runs `End KillEmAll` | 3 | ok (card 606, done: the host relays begin and end, the client refuses its own calls) |
| Level end, evac | Transport C_2 PlayerEntering @13949: CompleteObjective Finale_O1, flight StartAction, then `End The Level` after 27 s (@9519 EndLevel) | 5, 10 | E, covered (card 595, done: the release waits for every living player) |
| Level end, skip | Transport C_2 PlayerSkippedFlight @28639 -> `End The Level` | 5, 10 | E, covered (cards 595 and 260) |

No proven new gap. Open for card 301 until the Research collapse check has a verdict.

## FED_C3M1

Source: `kis.py` dump of FED_C3M1_Scripting (1916612 bytes, 135 events, 29 presence handlers, 0 decode errors), at
build/kis/c3m1.txt. Objectives: Beach_O1 (Marker1, Marker2, SO1 A-C, SO2-SO5), Resort_O1 (Marker, SO1-SO4),
Monorail_O1 (Marker, SO1-SO3), Satcom_O1 (Marker, SO1-SO3). The sweep found no GetPlayerPawn/Character/Controller
read, no SetActorLocation, no OtherActor use and no TriggerActor test in any presence handler. The only player-0
read is the camera shake row below. Rows cover door closes, the level end, presences that complete or add a step,
and the player-0 site.

| Objective / step | How it starts / completes | Questions hit | Verdict |
|---|---|---|---|
| Level start | Dropship C_3 DropshipEventDelegate @5405: AddObjective Beach_O1, Resort_O1, Monorail_O1 and their first subs, `Landed` = true, SetWaves C_12 | - | ok (host dropship action) |
| Area markers | Presence pairs C_11 (@8196/@8490), C_12 (@7693/@8013), C_14 (@8729/@8912), C_15 (@9210/@9429), C_19 (@11531/@11751): enter shows one area's subs and hides the others, leave reverses it | 1 | covered by card 140 (the client's own calls are skipped and the host's are replayed). Cosmetic: the client's list follows where the host stands |
| Beach_SO1 A | Switch C_4 SwitchPressedDelegate @9746: Close Door Ramp Medium C_0 and C_1, then CompleteSubObjective Beach_SO1_A | 2 | gap H (one press shuts two doors, no all-players test) |
| Beach_SO1 B | Switch C_0 @21758: Close Door Gigantic C_1, then @9856 Beach_SO1_B | 2 | gap H |
| Beach_SO1 C | Switch C_6 @9894: Close Door Gigantic C_2, then Beach_SO1_C | 2 | gap H. All four doors reopen only at @13642 (Beach_Marker1, reached from the Beach_SO4 complete @15985), so a player shut out stays out until Beach_SO4 ends |
| Beach_SO2 | WeaponPickup EPulse88 C_1 ADPickupDelegate @21699: if Beach_SO2 is not complete, @18585 Add Beach_SO5, complete Beach_SO2, SetWaves C_3 on, FactionLoop C_10 off; else @4402 `Mech Drop` after 1 s | 7 | covered by card 254 (pickup on the host) and card 495 (each player picks up their own) |
| Beach bug FX | Presences C_9 (e26/e53), C_17 (e27/e54), C_64 (e42/e55) set and clear BeachBugs1-3FXOff, which gates Niagara FX | - | check (cosmetic: each side's own pawn toggles its own copy, so the FX can differ between screens; card 136 class) |
| Resort_SO1 | Presence C_8 enter @7447: Add Resort_SO2, complete Resort_SO1 | 1 | class A (marker step, no close; low risk) |
| Resort_SO2 / SO3 | SatchelItemPickup C_1/C_2/C_3 @16879/@16734/@16884: when the count matches, complete Resort_SO2 and call out Resort_SO3; presence C_7 @7264 spawns from AISpawner C_76 when `flakbugsReady` | 6, 7, 9 | covered by card 254 (pickups) and card 292 (satchel sites) |
| Resort_SO4 / O1 | Resort_SO4_A..., then @2726 `Add to objective count`, complete Resort_Marker and Resort_O1, presence C_15 events off | 6 | ok (host objective state) |
| Monorail_SO1 | Presence C_4 enter @13183 -> @5773: Add Monorail_SO2, complete Monorail_SO1. CleanUpEncounter on SetWaves C_9/C_14 and FactionLoop C_7, progress bars at 100 (@13188-13414, the same handler's later path) | 1, 5 | class A (one pawn's entry cleans up three encounters while the other player may still be fighting them) |
| Monorail flag | Presence C_19 sets `inMonorailArea` on enter and clears it on leave; event `Final Wave` @11001 starts waves at once if it is set, else after 15 s | 5 | check (one player leaving clears the flag while the other is still inside; whether STPlayerPresence counts pawns is not known from the script) |
| Monorail_SO2 / SO3 | PanelSwitch C_1 @17695 -> @11156: `Finale Bug Fight`, Add Monorail_SO3, complete Monorail_SO2, `Toggle barriers on` @12475 (Toggle Barrier on PylonSpline C_0/C_4/C_5/C_6/C_7) | 2 | gap H (one press raises the five pylon barriers around the finale; same shape as a switch door close). Barrier collision reaches the client through the card 095 collision rows |
| Satcom_SO1 | Switch C_3 @13600: TriggerAudio, @3504 Add Satcom_SO2, complete Satcom_SO1 | - | ok (switch on the host) |
| Satcom_SO2 | Presence C_14 adds it @9125 with ref C_11 and enables override volume C_6; Radar C_0 TurnFinished -> `Satcom Rotation Complete` @17625 | - | ok (host radar action) |
| Dropship explosion shake | Presence C_16 @14887 (if `dropshipsReady`): SetWaves C_5, then `FlakFire` after 8.4 s and `Splosion` after 8.5 s; `Splosion` @15254 StartLegacyCameraShake on `GetPlayerCameraManager(self, 0)` | 9 | check (class F shape, cosmetic: the shake goes to player 0's camera on whichever side runs the event, so the client may never feel it) |
| Level end, evac | Transport C_1 PlayerEntering @18514: ReenableBugHordesForEnding, flying music, @4448 complete Satcom_SO3 and Satcom_O1, StartAction(1,1,2), `End the level` after 13 s (@4548 -> @18745 -> @12337 EndLevel) | 5, 10 | gap E unless card 399 has every pawn aboard (one player's entry starts the outro) |
| Level end, skip | Transport C_1 PlayerSkippedFlight @19002 -> @12337 EndLevel | 5, 10 | E, covered (card 595: the release waits for every living player; card 260 relays EndLevel) |
| Opening skip | Transport C_3 PlayerSkippedFlight @20969: EndCurrentConversation only | - | ok |
| Encounters and nests | SetWaves delegates, AIKillableStructure C_1/2/3/5 and Flakbug C_0/1/2 BuildingDestroyed | 6 | ok (host AI state, card 098 rows; flak bugs covered by card 292) |

## FED_C1M1

First pass from `FED_C1M1_Scripting.umap.txt` (ExecuteUbergraph). Objectives: Assault_O1, ShootingRange_O1
(SO0-SO3), TacSupport_01 (SO0, SO2, SO4, SO41, SO5), CaptureTheFlag_O1 (SO1-SO3), the Traitor branch
(`Traitor? = true` @22139) and level end. Every `GetSTPlayerCharacter(self)` (imp577) site is listed; where the
owning objective is not yet tied by reading the handler, the row says so.

| Objective / step | How it starts / completes | Entry event and scope | Write replicates? | Questions hit | Verdict |
|---|---|---|---|---|---|
| CTF SO2 / squad lead | @712 completes CaptureTheFlag_SO2, then `SquadLeaderComponent` set true on player 0 @749-770; set false @17115-17136 (reached when CTF SO2 is not complete @22189) | Set true: BndEvt BP_PlayerPresence_C_8 VolumeTriggerDelegate (stub to @14542), fired from OnTriggerEntered inside the entering pawn's overlap scope, so player 0 is whoever entered. Set false: Dropship_VerySlow_C_1 DropshipEventDelegate, ReceiveLoaded, debug key B; no scope (host) | No: the mod has no relay; the component's own flags are unread (runtime read needed) | 7 | gap F, both directions: a client entry makes only the client's server copy squad leader and the host misses it; the reset always hits only the host |
| CTF SO3 / level end | @1066 Handle CTF(4), @1088 completes CaptureTheFlag_SO3, CTFDropship, `CallFunctionWithDelay(15.0, 'EndLevel')` @1227 | Not traced per site (no per-player write) | - | 5, 10 | gap E-like (no all-players check; EndLevel relayed by card 260) |
| TacSupport SO5 end | @1997 completes TacSupport_SO5, RaiseFinalTargets; @2145-2216 calls imp572 = `/Script/Aloft.STCombatUtilityLibrary:RaiseAIAwarenessAudioEvent` (resolved by the script import hash 0cfc04ddc08e7bcc) with player 0 as the instigator, its location and radius 999999, then disables presences C_5 / C_4 / C_0 | PublicEnemyTrigger region, bound by BindNPCKill (from ReceiveGameReady) to the host pawn's `OnCharacterKilledNPC`: runs only on a host kill, in the host's own damage scope (`grenades.cpp:1673`) or none | Not a state write: an AI awareness (noise) event; AI runs on the host only | 1, 9 | minor: every AI hears a noise at the host's position with the host as instigator, so the final targets' attackers head for the host; no client state to sync. Not class F |
| Tutorial bomb-zone damage (was "shooting range presence") | @19129-19303: after KillAllTacSupportTargets, BP_PlayerPresence_C_15 bounds and `IsPointInBox(player0 location)`; inside, 150 explosive damage, radius 64, at player 0's location. Not the shooting range step | TriggerVolume_4 ActorBeginOverlap (stub to @20237) cast to BP_Tac_Supp_Bombing_Run_Projectile_Tutorial, while TacSupport_01 is not complete, then KillAllTacSupportTargets (@27802, @26035, @18445). The overlap is the projectile's; `c99_copy_of` follows its Instigator, so a client-called bombing run tests and damages the client's copy and a host-called one the host | Damage goes through the game's damage path on the host (health relayed) | 1, 5 | gap G: only the caller of the bombing run is tested and damaged; the other player standing in the zone is not |
| Traitor faction | @11791-11867 and @22204-22280 and @24551-24572 set a FactionComponent tag on player 0 (traitor / back to Federation) | @11791: PublicEnemyTrigger (host-kill delegate, above). @22204: ReceiveLoaded, no scope. @24551: TraitorFallzone, a 60 s `CallFunctionWithDelay` from PublicEnemyTrigger, no scope. All host | No relay in the mod; `UADFactionComponent` flags unread (runtime read needed) | 7 | accepted 2026-11-22 for a friendly kill, card 607 for a self-kill; was gap F (only the host turns traitor; AI targeting differs per player) |
| CTF SO1 flag carry tags | @22700-23223: CaptureTheFlag_SO1 complete and player 0 has no tag: Remove/AddLooseGameplayTags on player 0 | ReceiveLevelStart (to @22553), no scope (host) | No: loose gameplay tags do not replicate | 7 | gap F (flag state tags only on the host pawn) |
| Whipping (Traitor) | @23548-23663: player 0 `CharacterMovement.Deactivate`, then `GetSTPlayerController.EnableInput`; @28404-28474 Disable Input Whipping and Deactivate again | @23548: ReceiveLoaded, no scope (host). @28404: BP_FallZones_C_3 FallZoneDelegate (overlap scope of the falling pawn), and a `CallFunctionWithDelay` target (no scope, host) | Movement and input are local to each machine; nothing reaches the client | 4, 7 | accepted 2026-11-22 for a friendly kill, card 607 for a self-kill; was gap F (only the host is held, except a client fall-zone entry, which holds only the client's server copy) |
| Traitor teleport | @24304-24425 `K2_SetActorLocationAndRotation` on player 0 | TraitorFallzone (60 s delay), no scope (host) | The host pawn's move replicates; the client's pawn is not moved | 4 | accepted 2026-11-22 for a friendly kill, card 607 for a self-kill; was gap F (+ card 300 placement rule; client not moved) |
| Weapon strip / grant | @24948-25212 AddLooseGameplayTags then `ItemInventory.RemoveItemByDefinition` x2 (imp189, imp190, 15) on player 0; functions GiveLaser, GiveMorita, GiveUnarmedWeapon, RemovePlayersWeapons, EnableLeftWeapons cast player 0 to BP_Player_Character_MI | EnableLeftWeapons (to @24830), from ReceiveLoaded (no scope, host) and from WeaponPickup_LaserMorita_C_0 ADPickupDelegate (the picker's overlap scope) | No: loose tags do not replicate; the inventories are per machine (`engine-facts.md`: items are plain UObjects, the mod builds each loadout by card 021) | 7 | gap F (client keeps / never gets the weapons; a client pickup strips only the client's copy) |
| TacSupport class | @25374-25545 RemoveLooseGameplayTags, `TacticalSupportComponent.DefaultTacticalSupportClass` set on player 0, name shown on screen; @26652-26754 GetTacticalSupportComponent of player 0 enabled (SetTacSupportChargeable) | @25374: same chains as the weapon strip (ReceiveLoaded; LaserMorita pickup scope). @26652 (SetTacSupportChargeable, @26796) from: SkipTacs1and2 (PlayerPresence_C_1, WeaponPickup_MoritaMk1_C_1, TriggerVolume_0: overlap scopes); Setup Tactical Support Objective from AddObjectives (dropship event or debug B: no scope, host); PlayerPresence_C_1 directly (overlap); Tac SO4 from TacSuppRangeTarget_C_0/1/3 BuildingDestroyed (the destroyer's damage scope) | No relay. The state that matters is on the host: a client's tac support input runs on its server copy's component (card 100, `vehicles.cpp:2127`), so the server copy needs the class and the charge flag | 7 | gap F, mixed: scope-triggered calls set the toucher's server copy only; ReceiveLoaded and AddObjectives set only the host |
| TacSupport SO41 resupply | @27609-27750: overlap counts only when `OtherActor == player0`, then Handle Tactical Support(5), completes TacSupport_SO41, TryOpenTheCrate | TouchedPlayer, bound @3020-3043 to the health pickup actors' OnActorBeginOverlap (Bind Health Pickup, @26806): inside the overlapping pawn's scope, so player 0 is that pawn and a client overlap passes | - (InspectedResupply? is level-global) | 1 | not a gap (overlap scope) |
| Public enemy kills | @29096-29140 `OnCharacterKilledNPC` bound on player 0 (PublicEnemyTrigger) | BindNPCKill from ReceiveGameReady, no scope: bound on the host pawn only | - | 6, 9 | gap B-like (client kills never fire it); same fix as class B |
| Moved actors | ActorReference_C_0 @3439, StaticMeshActor_5 @27807 / @28090 (bomb run crate lid) `K2_SetActorLocation*` | Not per player | Card 098 relay | 3 | ok if card 098 relays the move; check in a C1M1 run |
| Section enables | @26068-26121 set ShootingRange_SO0 / Assault_SO0 / TacSupport_SO0 visible | Not per player | Objective frames | - | ok (objective frames, card 049) |

Notes: the tutorial map's per-player grants (class F) are the main C1M1 risk: the client plays the whole
tutorial without the tags, weapons and tac support the script gives the host. Open: resolve imp572 @2216, and tie
each class F site to its triggering event (the elided region @22343-25700 is one handler chain under the
Traitor / CTF branches).

### Senior review of the C1M1 rows (2026-11-13): classes F and G not settled per site

The rows ignore card 102 (`player_c99.cpp:319-331`): on the host, inside a server copy's overlap or interaction
scope, `GetPlayerController(context, 0)` (so `GetSTPlayerCharacter` and `GetSTPlayerController`) returns the acting
copy's controller, unless the call site is in `kC102Excluded`. So "player 0" depends on what triggered the handler:

| Trigger of the handler | "Player 0" on the host | Result |
|---|---|---|
| A client copy's overlap or interaction (card 102 scope) | the client's copy | The write or test applies to the client only. A per-player write (class F) then misses the host: the reverse gap |
| The host pawn's overlap or interaction | the host | Client missed (class F as written) |
| Timer, delay, delegate, tick, objective event (no scope) | the host | Client missed (class F / G as written) |

Consequences for the rows above, each to be confirmed by reading the handler's entry event:

- TacSupport SO41 @27609 (`OtherActor == player0` in an overlap handler): inside the scope player 0 is the
  overlapping copy, so a client overlap passes. Probably not a gap; verdict withdrawn until the entry is read.
- Shooting range @19185 (`IsPointInBox(player0 location)`): a gap only if the test runs outside an overlap scope
  (tick or timer). Entry event not named in the row.
- Every class F row: the verdict "client never gets it" holds only for a no-scope or host-scope trigger. For a
  scope trigger the write lands on whoever touched the trigger, and the other player (host included) misses it.

Required before any class F or G card: one more column per row, "entry event and scope" (the event node that
reaches the offset, and whether a card 102 scope is open there), and for each write whether the state replicates
(loose gameplay tags do not; inventory and faction: read the property flags). The class fix then has one rule:
a per-player script write is applied to every player pawn regardless of who triggered it; a per-player test passes
for any (or all) living players regardless of scope. Proof source for C1M1: the playtest client log covers the map
(the user played it as the client); read it for the grants (weapons, tac support class, tags) before coding.

### Junior: entry event, scope and replication per C1M1 row (2026-11-13, static)

The C1M1 table now has the two columns the review asked for. Entry events come from a reachability pass over
`FED_C1M1_Scripting.umap.txt` (ubergraph jumps, flow stack, local calls, `CallFunctionWithDelay` targets and
delegate binds). Card 102 scopes open only in `detour_c102_overlap`, `c102_interact` and the damage detour
(`grenades.cpp:1673`); everything else (timers, Receive* events, NPC-kill delegates, dropship events) runs with
player 0 as the host.

- Corrected: @19129-19303 is not the shooting range step. It is the tutorial bomb-zone damage on player 0, reached
  from the bombing-run projectile's overlap of TriggerVolume_4.
- Withdrawn: SO41 resupply runs in the overlapping pawn's scope and is not a gap.
- Mixed rows (squad lead, whipping, weapon strip, TacSupport class): some entries run in a scope and some do not.
  So the same write lands on the client's server copy only, or on the host only, depending on the trigger. The
  class F rule from the review ("apply to every player pawn regardless of who triggered it") covers both.
- Replication: loose gameplay tags do not replicate, and inventories are per machine (`engine-facts.md`). The
  flags of `UADFactionComponent`, `USTSquadLeaderComponent` and `USTTacticalSupportComponent` are not readable
  statically (unversioned packages). The mod relays none of them. A runtime `CPF_Net` row, like the card 018
  walker in `attributes.cpp`, settles them. For tac support the host side is what counts: a client's slot input
  runs on its server copy's component (card 100).
- imp572 @2216 resolved: `STCombatUtilityLibrary:RaiseAIAwarenessAudioEvent` (module `/Script/Aloft`), a
  noise at player 0 with radius 999999. Hash method: CityHash64 of the lower-case UTF-16 path with `.` and `:` as
  `/`, top two bits cleared, matched over every `exec` thunk in the PDB publics. Not a per-player write.
- Still open: the playtest client log read for the grants before any class F card.

### Senior review of the junior's C1M1 columns (2026-11-13): accepted, class F and G cards still blocked on one fact

Checked in source: the only scope openers are `detour_c102_overlap` (`player_c99.cpp:369`), `c102_interact` (`:409`)
and the damage scope (`grenades.cpp:1673`); `c99_copy_of` follows Instigator, then Owner or Outer (`:123-142`), so a
projectile's overlap acts for its thrower. The per-row scopes follow from that. Accepted verdicts: SO41 @27609 is not
a gap; @19129-19303 is the tutorial bomb-zone damage on player 0 (class G); the mixed-scope rows (whipping, weapon
strip, tac support class and chargeable) confirm the class rule: a level-script write to "the player" is applied to
every player pawn, whatever the trigger.

Blocking fact before any class F card: whether `UADFactionComponent`, `USTSquadLeaderComponent` and
`USTTacticalSupportComponent` properties replicate (CPF_Net). Not readable statically; b11 logs it at runtime (one row
per class, card 018 walker). If they replicate, the fix is host-side only (write on every server copy); if not, the
write also needs a relay to the owning client. Also open: imp572 callee @2216.

### Senior verdict (2026-11-14): blocking fact answered, class F carded as 310

- Replication: b11 soak run 1, host and client: `ADFactionComponent`, `STSquadLeaderComponent`,
  `STTacticalSupportComponent` each `bReplicates 0`, no game CPF_Net property. Every class F write needs a host
  apply to every player pawn, and a client row where the client reads the value.
- Playtest logs read (host side `client/playtest-menu-20261113-182510.log`, client side
  `logs/playtest-menu-20261113-183002.log` for C1M1): weapons already reach the copy (card 267, 11 rows) and the
  client (card 050); tactical support pickups and charges agree on both sides. Not covered: the same writes when
  player 0 is a copy, loose tags, `DefaultTacticalSupportClass` (a property write @25461), chargeable, faction,
  squad leader, and the client's tactical support after a host checkpoint load (caller exe+0x50F83A1).
- Card: `docs/cards/done/310-level-script-per-player-writes.md` (one manager, the card 267 rule in both directions).
  Class G stays uncarded until the class A all-players test (cards 288, 300) is extended.


### FED_C1M1 full pass (2026-11-22, senior)

Source: `kis.py` dump `build/kis/c1m1.txt` (`AmTraitor` does not decode). 79 ubergraph effect sites. Entries are by
reachability from the events; the first mapping (nearest preceding entry) was wrong and is replaced. Verdicts per site:

| Site(s) | Entry | Verdict |
|---|---|---|
| Squad leader @749/@770, faction @11867/@22280/@24572, loose tags @23079/@23223/@25052/@25395, weapon strip @25112/@25212, `DefaultTacticalSupportClass` @25461, chargeable @26673 | @749 presence C_8; @11867/@24572 PublicEnemyTrigger; @22280 ReceiveLoaded; @25112 LaserMorita pickup, ReceiveLoaded; @25461 ChangeTacSupportToTutFlavour (its caller is not resolved) | F, covered (card 310, done) |
| Door C_0 close @5445, door C_1 open @6649 | LaserMorita_C_0 pickup broadcast | H, covered (card 267; card 495, each player's own pickup) |
| Door C_1 close @6712 | presence C_8 | H, covered (card 227, waits for all players) |
| ActorReference_C_0 @3439 | Bind Health Pickup: ReceiveGameReady, TriggerVolume_0 overlap | not verified on the client |
| StaticMeshActor_5 (crate lid) @27807/@28090 | @27807 Handle Bomb Run Crate: health pickup events, ReceiveGameReady, ReceiveLoaded | card 140 class; not verified in a client log |
| TacSupport SO41 `OtherActor == player0` @27630 | TouchedPlayer (health pickup overlap scope) | ok (card 102 scope) |
| `RaiseAIAwarenessAudioEvent` @2216 at player 0 | PublicEnemyTrigger | ok (AI noise on the host; no client state) |
| EndLevel after 15 s @1227 | Trigger Asteroid Objective: SatchelBuilding_CTF_C_0 BuildingDestroyed | E, EndLevel relayed (card 260) |
| ClearMusicTriggers, PlaySound2D, PlaySound2DFName | several | ok (card 604 rows) |
| `ExecuteMusicTrigger` @1024, @6360, @6895 | presence C_8; presence C_17; TriggerVolume_3 BeginOverlap | check: card 604 does not hook it. Presences run on both machines; whether the TriggerVolume_3 handler runs on the client is not proven. The handler has no authority gate (cast to the player pawn @17545, then Assault_O1 complete or AmTraitor @10725), and c448b464a has no row from it. Its EndOverlap calls ClearMusicTriggers @7106, which card 604 hooks: one host and client crossing of the volume settles it by the paired-call count |
| Tutorial bomb zone: 150 damage at player 0 @19129-@19885, InstaKillTarget @19034 | KillAllTacSupportTargets: TriggerVolume_4 BeginOverlap with the bombing run projectile | G, accepted by the user 2026-11-22, no card, including that it may always damage the host. Who is damaged is unverified: the tutorial projectile @20898 is spawned with no Owner, so it may always be the host and not the caller |
| Traitor branch: @12055 AddObjective Traitor_O1, siren, faction, teleport @24425 | PublicEnemyTrigger, bound to the host pawn's `OnCharacterKilledNPC` @29140 (BindNPCKill, ReceiveGameReady) | B and D. Friendly kill accepted by the user 2026-11-22; host self-kill with Downed is card 607. See below |

Traitor evidence:

- A host kill of a friendly starts it (c310-proof-c1m1: squad leader). The objective is mirrored to the client, card 310
  applies Faction.EnemyNumberOne to the other player too, and only the host is teleported after 60 s.
- A self-inflicted lethal hit on the host also starts it, even when the host is only Downed and can be revived (c70b,
  c89c; old builds, not rerun on a current one).
- A bug-caused host down did not start it (c78f, c84g, c89f), nor did a `down host` command with the client up
  (c570b388a, c551b380a). The vanilla death after an all-down by that command did (c146-r2, c168-r3).
- It blocks the tutorial after a revive (static): ShootingRange completion needs NOT AmTraitor, and CTF setup and
  with it EndLevel need ShootingRange complete. Card 607 has the offsets.
- Not traced: a host kill of the client (card 607 sibling check).

User verdict 2026-11-22, on corrected facts: the bomb zone and the friendly-kill Traitor case are accepted. The
self-kill plus revive case was to be carded only if it blocks the tutorial, so it is card 607.

### Junior b211 class G and class D sweep (2026-11-18, static)

Source: the `kis-<map>-scripting.txt` level script dumps made on 2026-11-16 in session scratchpad
`316f03d0-ef4a-41af-8d1d-90993a7b967c` (C3M1, C3M2, C4M2, C5M1). No dump existed then for C4M1 or C5M2 (C4M1 was dumped 2026-11-18; the C5M2 extract is truncated). Search: the
named calls and pin names `GetPlayerCharacter`, `GetSTPlayerCharacter`, `GetPlayerController`, `IsPointInBox`,
`EqualEqual_ObjectObject`, `IsOverlappingActor`, `K2_SetActorLocation`, loose tag calls, and the import hashes of
GetPlayerCharacter (`6525afa15cb492ad`), GetPlayerController (`56ff3c31e90a1619`) and IsOverlappingActor
(`6ee02c00e7115a16`). This sweep covers classes G and D only. It does not give the full per-objective table of
those maps.

| Map | Site | What | Verdict |
|---|---|---|---|
| C3M1, C3M2 | none | no player-0 test, no player move | ok for G and D |
| C4M2 | @20514-20656 | After `Dialogue_BrainBug_Intro`: `BP_PlayerPresence_C_176.SetTriggerEventsEnabled(true)`, then `TriggerArea.IsOverlappingActor(GetPlayerCharacter(0))` starts `Brainbug Start Immediately` at once (the flow ends there; AddObjective BrainBug_O1 @20671 is a separate entry) | gap G: if only the client is inside C_176 when the triggers are enabled, the at-once start is skipped. The start then waits for a presence entered event. Whether enabling fires that event for a pawn already inside is not known statically, so the stall is open: the client may have to step out and back in |
| C4M2 | @32745-33228 | `K2Node_CustomEvent_DoTeleport`: player 0 to `CachedTPLoc`; a second branch caches player 0's location and control rotation | ok: debug custom event (console), not on an objective path |
| C4M2 | @14231, @25923-29580 | `K2_SetActorLocation` on BrainBug and StaticMeshActor_14/_32 | not D (no player). The world move is covered by actor replication or the card 098 rows; not checked here |
| C5M1 | @4374 | `IsFriendFaction(GetSTPlayerCharacter)` filters arena NPCs into TheLastNPCs | ok: every player is FED, so player 0's faction answers for all |
| C5M1 | @18894-19444 | An overlapping actor with a Trooper NPC faction tag is hidden, moved to ActorReference_C_12 and insta-killed | ok: NPC only (TrooperGrunt, MedicTrooper, RadioTrooper and similar tags) |

C4M2 C_176 fix: this is not plain, so nothing was coded. It needs either a host hook on
`STPlayerPresence::SetTriggerEventsEnabled(true)` that re-checks every living player pawn against TriggerArea, or a
runtime check that enabling fires the entered event for pawns already inside (then it is not a gap). Prediction for
a run: the client stands in C_176 and the host stands outside at the end of `Dialogue_BrainBug_Intro`. If the
BrainBug fight does not start until the client steps out and back in, it is a gap.

### Junior b212: class G, the SetTriggerEventsEnabled gap and the C1M1 bomb site (2026-11-18, static)

**Does enabling a presence fire "entered" for pawns already inside? No.** `ASTPlayerPresence::SetTriggerEventsEnabled`
(RVA 0x05103780) is `mov byte [rcx+0x30A], dl; ret`: a bare store to the enabled byte. Its only caller is the exec
thunk `ASTPlayerPresence::execSetTriggerEventsEnabled` (0x0508B980). Nothing re-tests the overlaps, and the engine fired
OnBeginOverlap for a pawn already inside before the enable, when gate G1 (byte +0x30A) dropped it. So gap G is real.

FED_C4M2 shows the single-player pattern that covers it for player 0 only:

    20514: BP_PlayerPresence_C_176.STPlayerPresence:SetTriggerEventsEnabled(true)
    20547: CallFunc_GetPlayerCharacter_ReturnValue = GetPlayerCharacter(self, 0)
    20573: CallFunc_IsOverlappingActor_ReturnValue = BP_PlayerPresence_C_176.TriggerArea.IsOverlappingActor(player 0)
    20646: pop_flow_if_not (CallFunc_IsOverlappingActor_ReturnValue)
    20656: Brainbug Start  Immediately()

A client standing inside C_176 when BrainBug_O1 is added (5 s after the third island) never counts.

**Fix (b212, class wide).** `detour_c301_exec_enable` (objectives.cpp) hooks the exec thunk. On the host with a
client, after the byte goes 0 -> 1 on a BP presence (not a per-player tutorial prompt), every other living player
body overlapping the presence goes through `detour_c245_overlap`. That means the game's own OnBeginOverlap gates, then
OnTriggerEntered and the card 227 gate. The pawn's root component is passed as the other component; OnBeginOverlap
reads only the actor and that component. Player 0 (GetPlayerController(world, 0), the same pawn the script reads,
card 102 scope included) is left to the script's own test, so it never enters twice. A once-enter presence clears the
byte on its first entry, and the loop stops there. Row: `card301: host t=... presence <name> enabled with N other
player(s) inside: M sent through OnBeginOverlap, enabled byte now B`. C4M2's C_176 bound event (15401) is guarded by
`BrainbugFightStarted`, so even a second start would change nothing there.

**C1M1 tutorial bomb site (class G row @19129-19303): exact lines.** It is reached from the bombing-run projectile's
overlap of TriggerVolume_4 (20237 cast to BP_Tac_Supp_Bombing_Run_Projectile_Tutorial, 20312 TacSupport_01 not
complete, 20361-20472 the tac support subsystem, then KillAllTacSupportTargets 18445-19124, whose loop ends at 19129):

    19129: BP_PlayerPresence_C_15.GetActorBounds(true, Origin, BoxExtent, false)
    19185: CallFunc_GetSTPlayerCharacter_ReturnValue_14 = GetSTPlayerCharacter(self)
    19206: CallFunc_K2_GetActorLocation_ReturnValue_4 = <player 0>.K2_GetActorLocation()
    19256: CallFunc_IsPointInBox_ReturnValue = IsPointInBox(<player 0 location>, Origin, BoxExtent)
    19303: pop_flow_if_not (CallFunc_IsPointInBox_ReturnValue)
    19313: CallFunc_GetSTPlayerCharacter_ReturnValue_14 = GetSTPlayerCharacter(self)
    19334: CallFunc_K2_GetActorLocation_ReturnValue_4 = <player 0>.K2_GetActorLocation()
    19384-19777: STDamageTemplate: DefaultDamageTrace, DamageType.Explosive, 150.0, radius 64.0, self damaging
    19885: ApplyDamageAtLocation(None, None, template, <player 0 location>)
    19933: pop_flow

It does not reduce to the enable shape. It uses no presence events, only the presence's bounds as a box. It is not the
C4M2 player-0 overlap shape either: the result is damage at player 0's location, not a step or a trigger. A fix would
need its own detour on ApplyDamageAtLocation from the level script frame, run once per other player inside the box.
For a tutorial "you stood in the bombing zone" punishment of 150 that is low value, and it is not fixed in b212. It
stays carded as gap G (C1M1), the only player-0 damage site found so far.

**Run c245b212a (b212 `a8562ff63c709122`, C4M2, client alone inside C_176 when enabled): fixed.** The client was
warped into C_176 at +263 s while it was still disabled: `card245 ... C_176 begin overlap by
BP_PlayerCharacterMI_C_2147478630 ... gate G1 enabled byte 0`. At the enable (5 s after Sewers_O1), the hook sent it:
`card301: host t=638885753 presence BP_PlayerPresence_C_176 enabled with 1 other player(s) inside: 1 sent through
OnBeginOverlap, enabled byte now 0`, with `OnTriggerEntered ... original runs` 4 ms before. BrainBug_SO1 was complete
on both sides at once (client replay at 638885768). The scenario's next step `objective expect BrainBug_SO1` then
voided because SO1 was already complete. That step assumes the host walks in, so it is a scenario assumption, not a
mod bug. FATAL/ERROR 0 on both sides, no crash dumps. Two side findings:
- PlayerPawn presence mismatches (3 keys, rounds 13-25): the client was 42 000 uu from the host, and the client's
  copy of the host pawn was net culled (`card098: client destroy BP_PlayerCharacterMI_C ... net cull 15000 uu` at
  638882263). The digest counts a pawn that one side cannot have at that distance, so these are false positives.
- BrainBug_SO4 took 165 s (b204: 17 s) with the same boss loop and more hits (259 host-counted player hits, b204
  162). The scenario's budget then ended at SO5, so BrainBug_O1 stayed open. The cause is not found yet.
