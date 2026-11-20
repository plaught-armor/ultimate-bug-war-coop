# Card 123: Close the proof gaps from cards 120-122

Status: written 2026-11-07 (senior); sent, item 4 redone at review, accepted 2026-11-07.

Read:

- card 120 Report and Senior review (`chan_ops.cpp` `c120_client_ready`, `c120_arm_map`; the hold branch was not
  exercised in c120d);
- card 121 Report, sweep section (save and load entries, level-script client effects, cards 006 and 100);
- card 122 Report (c122f/c122g co-op measure runs before the scale, c122q after it);
- scenarios `tools/scenarios/c70-late-join.txt`, `c113-travel.txt`.

Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`. DLL base: `4d058f899b394c59` (card 122).

## Why

User (2026-11-07): run more tests based on what we have learned. The last three cards each left a branch or a claim
that no run showed, and the three playtest crashes (cards 118, 119, 121) were one class: the client ran host-side game
logic (level script, save) on state it does not own. Each item below proves one new thing. No broad sweeps, no repeat
of a run that an accepted card already shows (user rule: targeted tests).

## Do

1. **Card 122 baseline, from logs only (no new run).** c122f/c122g were co-op runs before the scale. From their logs,
   give the same per-window counts as the card 122 proof table (attacking peak, attacks started by how many bugs,
   targets p0 / p1 / other). Say how much of the c122q rise the scale explains against 2 players without it. If
   c122f/c122g do not hold those counts, say so and stop on this item; do not add a run for it.
2. **Card 120 hold branch, one run.** Make the client's first host Ping arrive before its minimap has an origin
   (for example a late join timed into the host's map load, from `c70-late-join.txt`). The log must show the client
   holding its Pong, then answering once PC+0x7B0 -> +0x108 is set, and the client moving afterwards.
   - If the timing cannot be hit naturally in two tries, say so and stop on this item. Do not add a test-only delay.
3. **Card 120 gate reset across travel, same run as 2 if it fits, otherwise one run.** Both players in, host travels
   to the next map (`c113-travel.txt` path). The client log shows `c120_arm_map` re-arming the gate and the gate
   checked again on the new map, and the client moving there.
4. **Crash class sweep, static first.** List every actor class on the campaign maps the user plays (FED_C1M1, FED_C1M2,
   FED_C2M1 at least) that the client can use or trigger (levers, buttons, consoles, doors, pickups, triggers) and
   that runs level-script or other host-side logic on the client. For each one, name the gate that already covers it
   (card 006, 100, 121 or other) or mark it open.
   - Run only the open ones, client triggers each, one combined run if they fit on one route.
   - Any crash or open case: fix it with one mechanism for the class (user rules: no workarounds; one mechanism per
     class of state) and show it in that run.

Every run: rc 0, ERROR/FATAL 0, no CRASH line, no new dump on either side. Tests muted, windowed 640x360.

Stop when the items are done; the user will playtest.

## Done when

The Report has, per item: the counts or log lines that show it, or the reason it stopped. The sweep table (class, map,
what it runs on the client, covering gate or open, run result). Any fix and where it sits. Each run's length, rc,
ERROR/FATAL, CRASH lines and dumps. If any code changed: the DLL hash (`sha256sum | cut -c1-16`) and the play DLL
saved as `build/dwmapi-play-<hash>.dll`. Ends with "Report complete."

## Report

No code changed. Every run used DLL `4d058f899b394c59` (card 122). There is no new play DLL.

### 1. Card 122 baseline (logs only)

c122f and c122g hold the same per-window counts as the card 122 table: 5 s windows that have bugs in the fight, both players in, on the WarriorBug fight on FED_C2M1. Both runs predate the scale.

| Run | Attacking peak per window | Attacks started / by how many bugs, per window | Mean peak | Mean starters | Mean starts | Targets on players |
|---|---|---|---|---|---|---|
| c122f (`f86979eeab9eb69f`, no scale, 163 s) | 4, 2, 4, 4, 4, 7, 8, 7, 5, 3, 6 | 4/4, 1/1, 10/4, 11/5, 8/7, 18/11, 19/10, 21/11, 11/7, 9/6, 14/9 | 4.9 | 6.8 | 11.5 | about 52 % |
| c122g (`d3f3fb86192f9cf3`, no scale, 99 s) | 6, 4, 5, 6, 10, 10, 10, 2, 3 | 10/5, 14/5, 12/4, 15/9, 27/13, 30/12, 15/7, 8/2, 10/4 | 6.2 | 6.8 | 15.7 | about 72 % |
| c122q (scaled, 10 -> 16 %) | 4, 4, 4, 4, 4, 5, 10, 9, 6 | 9/5, 13/5, 7/5, 8/5, 7/5, 18/13, 24/12, 28/13, 5/2 | 5.6 | 7.2 | 13.2 | about 68 % |

The scale does not explain the rise in c122q over solo. Both unscaled co-op runs fall in the same range as c122q: peak 2-10 against 4-10, and 1-13 starters against 2-13, with c122g higher than c122q on peak and starts. The rise over solo (c122s: peak 3, 3 starters) comes from the second player. A second player puts a second target in the fight, and the bugs near each player attack that player. A 10 -> 16 % preference change sits inside the run-to-run noise. The card 122 lever is therefore correct in direction but too weak to measure. Making co-op hit clearly harder needs a bigger lever, which is the senior's call.

### 2. Card 120 hold branch: hit on try 2 (c123b)

- Try 1 (c123a, FED_C1M1 start, FAST=1) did not hit it. The gate opened with `held 0 ms`, as in c122p.
- Try 2 (c123b) used the start path that held in c122q: `UBWCOOP_TRAVEL=/Game/Maps/FED/FED_C2M1?listen`, FAST=1, both players in. The timing is natural; there is no test delay.
  - Client hold: `128023772 card120: client t=128023772 Pong held: minimap 0000000048419760 has no origin yet`
  - Client answer once the origin (PC+0x7B0 -> +0x108) is set: `128028598 card120: client t=128028598 minimap origin 000000006A278E80 (BP_PlayerCharacterMI_C) set: Pong to the host from now on (held 4826 ms)`
  - The host gets that Pong 26 ms later: `128028624 card026: host pong 2 from pc BP_PlayerController_C: rtt 45 ms`, then `card049: host t=128028624 journal replay to BP_PlayerController_C (first Pong of the map): 4 of 4 frames`.
  - The client moves afterwards. Goto lines from 128031954 on read `moved 1069`, then `1404`, `1403`, `1414`, `1403`, `1403`, `1388`, `1393`, `1135` uu per 2 s. In total the client moved from 231894 42900 to 224134 35199.
- The c122q log on the same DLL shows the same branch again (held 5008 ms).

### 3. Card 120 gate reset across travel (c123a)

c123a: FED_C1M1, both players in. The host uses `travel next` to go to FED_C2M1, and card 045 reconnects the client.

- First map: `127686199 card120: client ... minimap origin 000000007A0A63F0 (BP_PlayerCharacterMI_C) set ... (held 0 ms)`. The client then walks: goto `moved 1341` to `1408` per 2 s, ending with `goto: end (reached): dist 74, path 15838 uu, 22.3 s`.
- Travel: `127743430 card045: client ... local map loaded 467 ms after the drop`, then `127751162 card045: client t=127751162 reconnected (1): try 1, 8199 ms after the drop`.
- New map: `127752149 card120: client t=127752149 minimap origin 00000000122463F0 (BP_PlayerCharacterMI_C) set: Pong to the host from now on (held 0 ms)`.
  - `c120_arm_map` logs nothing itself. This second line is the proof that it re-armed the gate: the gate prints once per map, only on a gate that was cleared, and it checked the new map's own minimap (a new origin pointer).
- The client moves on FED_C2M1. Goto from 127819995 moved 848, 1802, 1407, 1404, 1406, 627, 511, 1189 uu per 2 s, about 8000 uu in total, from 231952 43172 to 226481 37423. It then stuck on terrain (`goto: end (stuck)`, the client's one WARN). That is a route limit of the scenario, not a gate fault.

### 4. Crash-class sweep (static)

FED_C1M2 does not exist in the game: FED_C1M1 travels to FED_C2M1. Class lists come from the extracted `FED_C1M1` and `FED_C2M1` umaps (main, `_Scripting`, `_Art`, `_Blockout` name maps). The mod gates by sink function, not by class, so each row names the host-side sink the class reaches on the client and the gate on that sink.

| Class (maps) | What it runs on the client | Covering gate | Run result |
|---|---|---|---|
| `BP_PanelSwitch_WithoutScreen`, `BP_Switch_C` (C1M1, C2M1) | Interact; level-script `SwitchPressedDelegate` -> CompleteObjective, save | 054 Interact relay; 100 client CompleteObjective skip; 121 PerformSave skip | covered (c121i) |
| `BP_Switch_AmmoCrate`, `BP_Switch_ArmourCrate`, `BP_AmmoCrate`, `BP_ArmourCrate` | Interact, inventory grant | 054 relay; 071/072 ammo from the host | covered |
| `BP_ZiplinePoint` | Interact | 054 relay | covered |
| `BP_PlayerTransportDropship(_VerySlow)` | board, descend, mission-end `TransitionToLevel` | 054 relay; the client's local Browse is a session drop, so card 045 reconnects it to the host | covered (c123a travel) |
| `BP_AIMechCallIn`, `BP_MechDropPlatform` | Blueprint mech pod spawn | 075 `c75_blocks_spawn` (client Blueprint mech spawn blocked) | covered |
| `BP_SatchelItemPickup(_CTF)`, `BP_SatchelPlacementSite`, `BP_SatchelBuilding_CTF/_Flakbug` | pickup, place, `ReceiveOnSatchelExploded`, objective | 054 relay; 098 WorldOp death; 100 objective skip | covered |
| Ammo, armour, health, grenade, weapon and TacSupp pickups, `BP_SecretPickup`, `BP_DevLogPickup` | overlap pickup | 054 relay; 056 PickupGone; 105/107 client runtime pickup spawns blocked | covered |
| `BP_PlayerPresence` | presence -> objective add, encounter start | 100; 006 (StartEncounter, PerformSpawn) | covered (c121i walks C_2, C_6, C_9) |
| `BP_CheckPointSaveZone`, `BP_CheckPointLoadPoint` | save, load | 121 PerformSave and Load skipped on the client, delegate completed | covered (c121i) |
| `BP_DamageVolume`, `BP_FallZones`, `BPHazardousZone` | damage, kill height | host-owned health (022/023); 046 `IsBelowKillThreshold` | covered |
| `BP_HordeZone`, `BP_ArenaZone`, `BP_EncounterManager`, `BP_EM_*`, `BP_AISpawner`, `BP_ProceduralDropshipTroops`, `BP_TestLevelObjectiveManager` | encounter start, spawns, objectives | 006; 100 | covered |
| `BP_TutorialPromptTrigger`, `BP_TriggerableAudioEmitter`, `BP_ZoneSettingOverrideVolume_Box` | UI prompt, audio, local settings | none needed (local presentation only) | covered |
| `BP_Dropship_LandingBlocker`, KillableCrates 01-06, `BP_KillableProp_ExplosiveBarrel`, `BP_WarriorBug_Corpses_Destructible` | collision, hide | 095 CollisionOp | covered |
| `BP_KillableBuilding_*` range targets | health damage -> range count | client fire is relayed; 065 counts it on the host | covered |
| `BP_DestructibleSpawner_Bugnest_HumanGameplay`, `BP_TentSpawner_HumanGameplay(_NoTent)`, `BP_DestroyableActor_BugBlocker/_HiveEntrance` | spawns, death event, pre-save | 006 spawns; 098 WorldOp death; 121 pre-save only inside a gated save | covered |
| `BP_Flare`, `BP_VikingDropship`, bombing-run strike | Blueprint spawns | 104/105 client Blueprint spawn of relayed classes blocked | covered |

No class is open, so there was no item 4 run and no fix. The sweep's own runs (c123a over both maps, c123b) logged no CRASH line.

### Runs

| Run | Scenario | Length | rc (host / client) | ERROR/FATAL | CRASH lines | New dumps |
|---|---|---|---|---|---|---|
| c123a | `c123-travel.txt`, FED_C1M1 -> FED_C2M1, FAST=1 | 224 s | 0 / 0 | 0 / 0 | none (only "crash trace installed") | none |
| c123b | `c123-hold.txt`, FED_C2M1 by `UBWCOOP_TRAVEL`, FAST=1 | 125 s | 0 / 0 | 0 / 0 | none | none |

Both runs were muted and windowed at 640x360. c123b's drive ended rc 1, but not from a game fault. The client's goto ran past its 20 s `try` timeout, so the client's command queue was still busy when the final `client shot` came, and that shot timed out. The game exited 0, and every proof line came before that point.

Report complete.

## Senior review (2026-11-07)

Items 1-3 accepted: the baseline settles that the card 122 scale is within noise (the lever decision goes to the
user), c123b shows the hold branch, and c123a shows the gate re-armed on a new map.

Item 4 is incomplete. The sweep covers FED_C1M1 and FED_C2M1 only, but the game ships more campaign maps (FED_C3M1,
FED_C4M1 and FED_C5M2 appear in our docs and scenarios; BUG_C1M1 too). The friend plays the campaign forward, so the
next crash of this class is on a map the sweep has not read. Fix:

1. List every campaign map in the paks (all `FED_*` and `BUG_*` umaps with their `_Scripting`/`_Art`/`_Blockout`
   sub-levels) and give the full list in the Report.
2. Extend the sweep table to every one of them, the same way (class, map, client sink, covering gate or open). A row
   already in the table only needs its new maps added.
3. Run only the open classes, one combined run if they fit; fix any open class with one mechanism for the class.

Same run rules as above. End with "Report complete." again.

## Report (item 4 redone, 2026-11-07)

DLL: `18783477b1111c15` (sha256 prefix), saved as `build/dwmapi-play-18783477b1111c15.dll`. Built with no warnings.

### 4a. Campaign maps in the paks

13 campaign maps, 78 packages with their sub-levels. Name maps were read from the extracted umaps.

| Map | Sub-levels |
|---|---|
| FED_C1M1 | `_Art`, `_Audio`, `_Blockout`, `_Lighting`, `_Scripting` |
| FED_C2M1 | `_Art`, `_Audio`, `_Lighting`, `_Scripting`, `_StaticLightsAdd` |
| FED_C3M1 | `_Art`, `_Audio`, `_Lighting`, `_PFX`, `_Scripting` |
| FED_C3M2 | `_Art`, `_Audio`, `_Blockout`, `_Lighting`, `_PFX`, `_Scripting` |
| FED_C4M1 | `_Art`, `_Audio`, `_Lighting`, `_PFX`, `_Scripting` |
| FED_C4M2 | `_Art`, `_Audio`, `_Blockout`, `_Collision`, `_Lighting`, `_Scripting` |
| FED_C5M1 | `_Art`, `_Audio`, `_Excavator`, `_Lighting`, `_PFX`, `_Scripting` |
| FED_C5M2 | `_Art`, `_Audio`, `_Lighting`, `_PFX`, `_Scripting` |
| BUG_C1M1 | `_Art`, `_Audio`, `_Lighting`, `_Scripting` |
| BUG_C2M1 | `_Art`, `_Audio`, `_FX`, `_Lighting`, `_Scripting` |
| BUG_C3M2 | `_Art`, `_Audio`, `_Lighting`, `_PFX`, `_Scripting` |
| BUG_C4M2 | `_Art`, `_Audio`, `_Lighting`, `_Scripting` |
| BUG_C5M1 | `_Art`, `_Audio`, `_Blockout`, `_Scripting` |

There is no FED_C1M2, BUG_C2M2 and so on: the campaign skips those slots. These maps are not campaign maps and were left out: `3D_Frontend`, `3D_Frontend_Art_Background`, `Entry`, `L_MI_C2M1_VFX`, `TrainingCamp_Showcase`.

Distinct `BP_*` classes per map (main plus sub-levels): F1M1 52, F2M1 80, F3M1 143, F3M2 116, F4M1 101, F4M2 103, F5M1 116, F5M2 136, B1M1 47, B2M1 50, B3M2 91, B4M2 72, B5M1 44.

### 4b. Sweep over every map

The item 4 table above still holds for its rows. Its classes also appear on the new maps, and the same gates apply, because each gate is on the sink function, not the class or map. The rows below are the classes that were not in that table.

| Class (maps) | What it runs on the client | Covering gate | Result |
|---|---|---|---|
| `BP_PanelSwitch_WithScreen` (F5M1, F5M2), `BP_WallSwitch` (F3M2, F5M1) | Interact, level-script objective, save | 054 relay; 100; 121 | covered |
| `BP_GrenadeCrate` (F3M2), `BP_HealthCrate` (B4M2) | Interact, inventory grant | 054 crate relay | covered |
| More pickup variants (all maps) | overlap pickup | 054; 056; 105/107 | covered |
| Gigantic sliding doors, destructible props, eggs, `KillableCrate_Base`, `ExplodingPlant`, `ExplosiveCar` | collision, hide | 095 CollisionOp | covered |
| `BP_DestroyableActor_*` bridges, floors, `PlasmaBugNest`, `PlasmabugShield` | death event | 098 WorldOp | covered |
| `BP_DestructibleSpawner_Tent_ArachnidGameplay`, `SatchelBuilding_Plasmabug/_Rubble`, `SatchelPlacementSite` (F5M2), `LevelObjectiveSatchelSIte` (F4M2) | spawns, death, objective | 006; 054; 098; 100 | covered |
| `HazardousZone` variants, `MortarZone`, `BombarmentHuman`, `FallingRock`, `DamageCauser` | damage | host-owned health 022/023 | covered |
| `TacSupp_OrbitalStrike_ScriptedLauncher` (F3M2) | Blueprint strike spawn | 104/105 | covered |
| `BP_Activatable_ShootableButton` (B4M2, F4M2) | damage, `OnDamageTaken` -> activate | host damage; ShootableButton instigator path 100/102 | covered |
| `BP_MechDropPlatform` (F2M1, F3M1, F3M2, F4M1, F4M2, F5M1, F5M2, B1M1, B3M2, B4M2), `BP_AIMechCallIn` | mech pod spawn, mech enter | 075 | covered |
| Dropship variants | board, descend, travel | 054; 045; 104 | covered |
| Presentation and test actors (audio, VFX, prompts, cinematics) | local presentation | none needed | covered |
| **`BP_MountableTurret_C`** (B3M2, F3M1, F3M2, F4M1, F4M2, F5M1, F5M2) | Use press -> `OnInteraction` -> local possess; `LeaveTurret` | none | **open, fixed in this card** |

No card has run the BUG campaign in co-op yet. Its classes all reach the same class-agnostic sinks, so this table covers them, but no run on a BUG map backs it.

### 4c. The open class: `BP_MountableTurret_C`

Before the fix, a client Use press on a turret ran the mount locally. The host never saw it, and the client did not possess the turret. The leave then crashed the client (the card 029 dump `3402DC26`).

The fix extends the card 075 mech mechanism to turrets, so there is one path for "a client takes a host-owned pawn seat":

- The client's `OnInteraction` on a turret (new hook row) is skipped locally and sent as `ChanOp::TurretMount`. The client's `LeaveTurret` (hook now ungated) is skipped and sent as `ChanOp::TurretLeave`. Both are in `mod/src/ubw/vehicles.cpp` and `chan_ops.*`.
- The host finds the turret nearest to the client's server copy, then runs the original `OnInteraction` or `LeaveTurret` with that server copy. The possession replicates to the client as usual.
- A client repeat guard (`kC123MountRepeatMs = 1000`) sends one TurretMount per press. The engine calls `OnInteraction` twice for one client press, and without the guard the host logs an "ignored" WARN for the second call. A leave clears the guard.
- A probe line logs each client Use press on a turret, with the `CanInteract` result, the in-use byte, +0x688, the pawn yaw and the turret yaw (`c123_client_press`, called from the card 050 client press in `attributes.cpp`).

`CanInteract` (0x051C1690) needs the pawn to face the same way as the turret, within a cone: the pawn has to stand behind the gun. For the test I added `warp actor|client <name> <dist> behind` (`cmdfile.cpp`, `testrun.cpp`, `tools/drive.py`). It warps to `dist` uu behind the actor, along its root forward axis. The scenario is `tools/scenarios/c123-turret.txt`: FED_C3M1 by `UBWCOOP_TRAVEL`, warp to `BP_MountableTurret_C`, press E, mount, press E, leave.

Proof, c123ac:

- Client: `card123: client ... Use press on BP_MountableTurret_C_8 ...: CanInteract 1 (in use 0, ..., pawn yaw -90.7, turret yaw -90.0)`, then `turret OnInteraction ... local run skipped`, then `AcknowledgePossession` of `BP_MountableTurret_C`. The client screenshot shows the turret view with the TURRET HEAT HUD.
- Host: `card123: host TurretMount from BP_PlayerController_C ...: PC pawn now BP_MountableTurret_C, turret in use 1`.
- Leave: client `TurretLeave ... sent`, then `AcknowledgePossession` back to `BP_PlayerCharacterMI_C`. Host `card123: host TurretLeave ...: PC pawn now BP_PlayerCharacterMI_C, turret in use 0`. No crash on leave.

### 4d. Runs

All runs were muted and windowed at 640x360. Every run had rc 0 / 0, ERROR/FATAL 0 / 0, no CRASH line (only "crash trace installed") and no new dump on either side.

| Run | Length | Result | DLL |
|---|---|---|---|
| c123t | 144 s | no target: the look aimed above the turret | `fa1c6b6fad3160f5` |
| c123u | 145 s | target found, `CanInteract` false (facing cone) | `fa1c6b6fad3160f5` |
| c123v | 147 s | `behind` warp bug | `739a85592b97fcb2` |
| c123w | 162 s | probe: `CanInteract` 0, pawn yaw 179 against turret yaw -90 | `4243357c2c3866bc` |
| c123x | 147 s | fail, same cause | `4243357c2c3866bc` |
| c123y | 142 s | fail, same cause | `4243357c2c3866bc` |
| c123z | 139 s | found the test-tool bug: a unit vector failed the `h > 1` test, so `behind` always went +x | `a89dcf8089453a3a` |
| c123aa | 114 s | pass, with one host WARN from the second `OnInteraction` (led to the repeat guard) | `8b6e3f7c77492be8` |
| c123ab | 158 s | look ran before the server position correction had landed (scenario race; two `wait near` steps added) | `18783477b1111c15` |
| **c123ac** | **118 s** | **pass**: drive rc 0; host WARN 128, all the existing card 095 "collision write ... name not sendable" lines; client WARN 0 | `18783477b1111c15` |

The failures in c123t-c123ab were scenario and test-tool faults, not game faults.

### Caveats for the playtest

- The turret root yaw is not synced. On FED_C3M1, `BP_MountableTurret_C_8` has yaw 0 on the host and -90 on the client. The client's `CanInteract` uses its own copy, so a client has to stand behind the gun as the client sees it. The test passed because the host mount does not re-check the facing.
- Aiming and firing from a client-possessed turret are out of scope, as with the card 075 mech weapons: they have no Server RPCs.

Report complete.
