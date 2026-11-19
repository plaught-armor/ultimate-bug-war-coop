# Objective co-op audit (card 301)

Static audit of every objective and sub-objective on the seven Trooper maps against the ten
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
| FED_C1M1 | 5 plus Traitor branch and level end | 12 rows, classes A, F, G (player-0 sites) | first pass 2026-11-13: player-0 sites mapped; objective-to-site mapping partial (see notes) |
| FED_C3M2 | 4 (HydroDam, Silo, PowerPlant, Monorail) plus level end | 14 rows; 2 closes-behind (H), classes A, E | first pass 2026-11-18: presences, doors, evac traced; switch and pickup steps not per-row |
| FED_C4M1 | 6 rows | 3 closes-behind (H), 1 one-player level end (E) | first pass 2026-11-18 (door, player-0, move and EndLevel sweep) |
| FED_C4M2 | 6 (Tunnel, Plaza, Bridge, Sewers, BrainBug, plus Add Objective 2-4) plus level end | 9 rows; classes C, F (mild), G, H | first pass 2026-11-18: player-0 sites, door closes, level end traced |
| FED_C5M1 | Opening, Excavators, MunitionsBase, ResearchStation plus level end | 5 rows; classes D (check), E, H | first pass 2026-11-18: player-0 sites, door closes, level end traced |
| FED_C5M2 | 6 rows | classes E, G, H (C check) | first pass 2026-11-18 (door, player-0, move and EndLevel sweep) |

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
| Mid-level transports C_0, C_1 | OnPlayerEntryRequested -> Dropship_Silo_Boarded / Dropship_Powerplant_Boarded @26028-26115 | 4, 5 | covered by card 399 if the entry runs EnterDropship on the host; else gap E. Decode stops at @26203 (kis ERR op 0x11), handlers past it unread |
| Pickups | SecretPickup C_2 @16560; Autocannon @28753, EPulse88 @26166, Railgun @28758 | 7 | covered by card 254 / 289 |

Not traced per row: switch handlers C_0 @6386, WallSwitch C_1 @23472, PanelSwitch C_1 @27793; HydroDam SO1-SO3/SO6 and
Silo SO3-SO5 (Lock1-3) adds. Fix candidates: class H rows go into kC300Closures (C_52) and a switch-side twin for the
Switch C_3 lockdown; class A rows into the card 288 all-players test.

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

## FED_C5M1

Source: `kis.py` dump of FED_C5M1_Scripting (17 presence handlers). Only the sites a sweep for door closes,
player-0 reads, actor moves and EndLevel finds are rowed; the presence-by-presence pass is not done.

| Objective / step | How it starts / completes | Questions hit | Verdict |
|---|---|---|---|
| Switch C_5 doors | SwitchPressedDelegate C_5 @29072 -> @28419: Close Door on Gigantic C_17, C_15, C_16, then @23143 | 2 | gap H (one press shuts three gigantic doors; same shape as C3M2 Switch C_3) |
| TriggerBox_1 kill volume | ActorBeginOverlap @18894: an OtherActor with a Faction.Human trooper tag is hidden, moved to ActorReference_C_12 (K2_SetActorLocation) and InstaKillTarget | 4, 9 | class D if a player pawn carries one of those tags (the level script runs on both sides, so each side moves the pawn it sees); else ok (NPC troopers only). Check the tag set on a player pawn |
| TheLastNPCs | @4374 IsFriendFaction(arena NPC, GetSTPlayerCharacter) | - | ok (both players share the faction) |
| ResearchStation_O1 | @19590 CompleteObjective from @29082 | - | ok |
| Level end | Transport dropship C_0 PlayerSkippedFlight @29092 -> @26091 EndLevel; FlightPath_End -> @26145 -> same | 5, 10 | gap E unless card 399 has every pawn aboard; one player's skip ends the level for both (card 260 relays EndLevel) |

## FED_C4M1

Source: `kis.py` dump of FED_C4M1_Scripting (21 presence handlers, 0 decode errors). Only the sites a sweep for
door closes, player-0 reads, actor moves and EndLevel finds are rowed. The sweep found no player-0 read and no
SetActorLocation.

| Objective / step | How it starts / completes | Questions hit | Verdict |
|---|---|---|---|
| Stadium_SO1 -> Stadium_SO2 | Objective event `Add Stadium_SO2` @6586 (ID Stadium_SO1, a count objective of 4 kills A-D) adds SO2, starts 'Stadium Fight 1' after 20 s, enables FallZones C_6/C_7, closes Medium C_2, Medium C_1, Gigantic C_1 (@6976-7048) | 2 | gap H (objective event, not a presence: the doors close when the last kill lands, wherever the second player stands; the script tracks `inStadium` from a presence, @5911/@6119) |
| Metro_SO1 -> Metro_SO2 | `BP_PlayerPresence_C_17` enter @14243 -> @13740, if `metroReady` (set @13050 when Metro_SO1 is added) -> @13363: C_17 events off, closes Large C_3, Large C_2, Medium C_3, Medium C_4 (@13447-13555) | 2 | gap H (presence: kC300Closures row) |
| Bridge step | `BP_PlayerPresence_C_255` enter @19570: closes Large C_0, then @19154 enables FallZones C_1/C_5 | 2 | gap H (presence: kC300Closures row) |
| Bridge collapse | @19221 `BridgeDead`: BoxOverlapActors around BP_DestroyableActor_C4M1Bridge_C_0 | 4 | not traced further (card 481, closed, covered the C4M2 MoveableRoad digest, not this bridge) |
| Level end | `BP_PlayerPresence_C_70` enter @16245 -> @13797 EndLevel at once | 5, 10 | gap E (one player's entry ends the level for both; card 260 relays EndLevel). Fix: add C_70 to kC300Closures so EndLevel waits for every living player |
| Metro / Park / LZ sub-objectives | CompleteSubObjective rows from encounter and spawner events | - | ok (host objective state, relayed by the objective sync) |

## FED_C5M2

Source: `kis.py` dump of a full extract of FED_C5M2_Scripting (7145313 bytes, 31 presence handlers, 0 decode
errors; the earlier 3000000-byte extract was truncated). Only the sites a sweep for door closes, player-0 reads,
actor moves and EndLevel finds are rowed.

| Objective / step | How it starts / completes | Questions hit | Verdict |
|---|---|---|---|
| Switch doors | `BP_PanelSwitch_WithoutScreen_C_1` SwitchPressedDelegate @14353: Close Door on Large C_4, C_1, C_5, C_6, C_7, C_8, then FallZones C_6/C_10/C_9 on | 2 | gap H (one press shuts six doors; same shape as C5M1 Switch C_5) |
| Cave in | Presence `BP_PlayerPresence_C_408` enter @30571 (Nuke Town, Add Finale_SO7) sets `caveInReady`; event `Cave in` @31328 re-arms every 30 s (@32214): Niagara burst and a 20 damage, 1500 radius, self-damaging blast at `GetSTPlayerCharacter(self)` location | 4, 9 | gap G (the hazard follows player 0 only: on the host the blast centres on the host pawn, so the client is hit only near the host and the host sees no burst at the client). Card 493 |
| FallZone C_5 blast | FallZoneDelegate C_5 @25858: ApplyDamageAtLocation at BP_SatchelPlacementSite_C_4 with `GetSTPlayerCharacter` as instigator | - | ok (instigator credit only; the blast point is fixed) |
| Satchel pickups | `Set satchels` @23388 and `Revert Satchels` @34821 move BP_SatchelItemPickup_C_1/2/3 to ActorReference_C_5/6/7 (K2_SetActorLocation) | 4 | check (class C shape: the client must see the pickups where the host has them; the script runs on both sides, so both move them if both run the event) |
| Level end, evac | Transport C_2 PlayerEntering @13949: CompleteObjective Finale_O1, flight StartAction, then @9208 `End The Level` after 27 s (@9607 -> @9519 EndLevel) | 5, 10 | gap E unless card 399 has every pawn aboard (one player's entry starts the outro) |
| Level end, skip | Transport C_2 PlayerSkippedFlight @28639 -> `End The Level` | 5, 10 | gap E (one player's skip ends the level for both; card 260 relays EndLevel) |

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
| Traitor faction | @11791-11867 and @22204-22280 and @24551-24572 set a FactionComponent tag on player 0 (traitor / back to Federation) | @11791: PublicEnemyTrigger (host-kill delegate, above). @22204: ReceiveLoaded, no scope. @24551: TraitorFallzone, a 60 s `CallFunctionWithDelay` from PublicEnemyTrigger, no scope. All host | No relay in the mod; `UADFactionComponent` flags unread (runtime read needed) | 7 | gap F (only the host turns traitor; AI targeting differs per player) |
| CTF SO1 flag carry tags | @22700-23223: CaptureTheFlag_SO1 complete and player 0 has no tag: Remove/AddLooseGameplayTags on player 0 | ReceiveLevelStart (to @22553), no scope (host) | No: loose gameplay tags do not replicate | 7 | gap F (flag state tags only on the host pawn) |
| Whipping (Traitor) | @23548-23663: player 0 `CharacterMovement.Deactivate`, then `GetSTPlayerController.EnableInput`; @28404-28474 Disable Input Whipping and Deactivate again | @23548: ReceiveLoaded, no scope (host). @28404: BP_FallZones_C_3 FallZoneDelegate (overlap scope of the falling pawn), and a `CallFunctionWithDelay` target (no scope, host) | Movement and input are local to each machine; nothing reaches the client | 4, 7 | gap F (only the host is held, except a client fall-zone entry, which holds only the client's server copy) |
| Traitor teleport | @24304-24425 `K2_SetActorLocationAndRotation` on player 0 | TraitorFallzone (60 s delay), no scope (host) | The host pawn's move replicates; the client's pawn is not moved | 4 | gap F (+ card 300 placement rule; client not moved) |
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
- Card: `docs/cards/310-level-script-per-player-writes.md` (one manager, the card 267 rule in both directions).
  Class G stays uncarded until the class A all-players test (cards 288, 300) is extended.


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
