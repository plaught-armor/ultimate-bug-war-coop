# Card 138: Client BeginPlay runs before its pawn exists (join and reconnect crash)

Status: accepted 2026-11-08 (hb60, test DLL 8904021a22268866). Was: sent 2026-11-08, before the card 132 rerun. It blocks the card 132 closing run, and it can crash the play build
on any join or card 045 rejoin. Budget: three runs (STATE.md "Card scope and investigation rules").

Read: card 132 "Closing run hb59: results"; card 045 (rejoin); card 005 (deferred GameState path).

## Verified (junior, hb59)

- The client crashed at exe+0x51EB9BA, `USTWeaponUtilityLibrary::GetWeaponInventory`+0xA, reading 0x0. The stack runs
  `AGameStateBase::OnRep_ReplicatedHasBegunPlay`, NotifyBeginPlay, `AActor::BeginPlay`, then BP bytecode. The only mod
  frame is the ProcessEvent pass-through. Dump UECC-...F0FA5E82..._0000, DLL 42bec463a010f337.
- The caller is `BP_PickupTracker` (FED_C2M1 nuke pickup). Its ReceiveBeginPlay path "Enable Delegates" calls
  `GetWeaponInventory(GetSTPlayerCharacter(self))` and binds OnWeaponEquipped and OnWeaponUnequipped. No null check.
- The cause is the order in which the client's actors arrive. In hb58 the pawn came first, then the GameState. In hb59 the GameState came at
  t=215115131 with no pawn yet. In single-player the pawn always exists before BeginPlay, so the game's Blueprints
  assume it.

## Open

- Every Blueprint or native BeginPlay on the maps we play that reads the local player's pawn (`GetSTPlayerCharacter`,
  `GetPlayerCharacter`, `GetPlayerPawn`, `GetPlayerController(0)->Pawn`), and what each does on the client. That
  includes whether it binds delegates once and so stays dead for the whole map when the pawn was null.
- Whether the play build has hit this. Grep the playtest client logs.

## Do

1. **Trace (static, before code).** List the BeginPlay readers above with kis.py and the PDB. For each, give the class,
   the map, what it binds or reads, and whether the client needs it (HUD, prompts, visuals) or it is host-only logic.
   Send the list to the senior.
2. **Class fix: match the game's assumption.** On the client, hold the `OnRep_ReplicatedHasBegunPlay` begin-play dispatch
   until the local pawn is possessed, or until a timeout that logs a WARN. Then let it run once. Find the address and
   say how it is held, either by deferring the call or by re-calling it.
   - A null guard on `GetWeaponInventory` alone is not the fix. It leaves every tracker that binds once dead on the
     client for the whole map, and it covers one function only.
   - Keep a guard only as a crash backstop, logged as a WARN, if step 1 shows readers that the hold cannot cover.
3. **Prediction and one run.** Write the predicted log lines first. Run FED_C2M1 FAST=1 with a scripted client
   reconnect timed so the GameState arrives first; if the order cannot be forced, show it with an injected delay on the
   pawn. Log the hold start, the pawn arrival, the release, and the tracker's delegate bind on the client.

## Done when

- The client survives a reconnect with the GameState first, and the tracker binds on the client after the pawn arrives.
- The Report has the step 1 table, the hold address and mechanism, the WARN diff against hb58, the run length, rc,
  ERROR/FATAL, CRASH lines, dumps and the DLL hash (`sha256sum | cut -c1-16`). It ends with "Report complete."

## Step 1 trace (static)

Method: kis bytecode listings (FScriptName fixed to 12 bytes in memory), walked from `ReceiveBeginPlay` through the
synchronous control flow up to the first latent action (`$S/kis2/bpreach.py`). 58 packages have ReceiveBeginPlay plus a
player getter; 22 read the pawn or the controller on the synchronous path. Placement: the class paths in every
`FED_C1M1*.umap` and `FED_C2M1*.umap` (484 and 696 classes), resolved to packages. Native: the PDB-resolved callers of
`USTCommonUtilityLibrary::GetSTPlayerCharacter` (exe 0x523E0F0, 67 callers), `GetSTPlayerController` (0x523E190, 31),
`UGameplayStatics::GetPlayerCharacter` (0x3C51120, 5) and `GetPlayerPawn` (0x3C51620, 1), filtered to BeginPlay and
init functions.

Only actors in the world when the dispatch runs get their BeginPlay from `NotifyBeginPlay`: placed actors and
replicated actors that have already arrived. Actors spawned later (HUD, markers) run BeginPlay at spawn, so the hold
does not change them.

| Class | Map | BeginPlay reads | Null pawn on the client | Client needs it |
|---|---|---|---|---|
| BP_PickupTracker | C2M1 | `GetWeaponInventory(GetSTPlayerCharacter())`, binds OnWeaponEquipped and OnWeaponUnequipped | **crash**: native GetWeaponInventory dereferences null (hb59, exe+0x51EB9BA) | yes: pickup prompts and markers |
| FED_C2M1_Scripting (level BP) | C2M1 | `GetSTPlayerController().OnWantsToBoardMech` bind | the controller is normally present; if null, Accessed None and the mech-board bind stays dead | yes: mech boarding |
| BP_ResupplyRack_Level1 | C1M1 | Cast<BP_Player_Character_MI>(pawn), caches ResupplyLevel | the cast fails and CachedResupplyLevel keeps its default | yes: the rack's level visuals |
| BP_TentSpawner_HumanGameplay (+_NoTent) | C1M1, C2M1 | IsOverlappingActor(pawn), then PlayerEnter or PlayerExit | false, so PlayerExit(None) runs | host-only spawn logic |
| BP_DestructibleSpawner_Base (+Bugnest_HumanGameplay) | C2M1 | IsOverlappingActor(pawn), then PlayerEnter or PlayerExit | as for the tent spawner | host-only spawn logic |
| BP_Door_Base (+Sliding_Large, LR_Door_Toggle) | C1M1 | Cast<BP_Player_Character_Bug>(pawn).FormsInventory.OnWeaponEquipped bind | the cast fails, the same as for the MI trooper | no: bug-player path only |
| BP_PlayerCharacterMI (each pawn's own BeginPlay) | both | WaitGameplayTagAddToActor(GetSTPlayerCharacter()) | the task is IsValid-checked | yes: the local pawn's ability-tag wait |
| ASTPlayerCharacter::BeginPlay (native, 0x51BE240) | both | `this == GetSTPlayerCharacter()` then DisablePlayerOnGameStartWhileLevelIsLoading | the local init is skipped; ActorFullyLoaded (0x51BB710) also calls it | yes, local pawn only |
| ASTGameplayGameMode::BeginPlay (native) | both | GetSTPlayerCharacter | not on the client: no GameMode | host-only |

Not placed on C1M1 or C2M1 (they show up only through references; no placed class has them as a parent):
BP_AI_AATurret, BP_AI_CannonTurret, BP_AIKillableStructure, BP_Building_BugNest_BugGameplay_New,
BP_DestructibleSpawner_Tent_ArachnidGameplay, BP_KillableBuilding_Generator, BP_MortarZone, BP_MountableTurret_Gunner,
BP_TerrorZone, BP_PhotoModeSpectatorPawn, BP_HUD_3DFrontend.

Spawned at runtime, so outside the dispatch: BP_HUD_Manager (spawned by BP_PlayerController and the GameMode; it also
rebinds through OnPossessedPawnChanged), BP_ObjectiveMarkerActor (spawned by BP_TestLevelObjectiveManager).

No other native BeginPlay calls these getters directly. The init callers are USTPlayerCharacterAbilityWidget and the
minimap/proximity widgets' NativeOnInitialized, which are widgets created later.

Play build: no playtest client log has exe+0x51EB9BA or GetWeaponInventory. Two playtest crashes are different sites:
lever, exe+0x518E6E0 (read 0x690); flare, exe+0x50FD2B4 (read 0x10). The race needs the GameState to arrive before
the pawn on a join, which only hb59 hit.

The hold point: `AGameStateBase::OnRep_ReplicatedHasBegunPlay` (exe 0x3C624D0). Its body: if bReplicatedHasBegunPlay
(+0x2D0) is set and Role (+0x168) is not Authority, call WorldSettings vtable +0x7A0 (NotifyBeginPlay), then +0x7A8
(NotifyMatchStarted). Proposed mechanism: a client-side detour defers the call. When there is no local possessed pawn,
it keeps a FWeakObjectPtr to the GameState and returns. A per-tick check calls the original once, either when the pawn
is possessed or after 10 s with a WARN. NotifyBeginPlay checks bBegunPlay itself, so a second call is a no-op. Every
reader in the table then sees the pawn, so no guard backstop is needed for these maps.

## Step 2: the hold (code), deadlock check, mod order, predictions

Senior conditions (2026-11-08): release on the local PC plus a possessed MI pawn; no release on timeout (a WARN at
10 s and every 10 s after); static deadlock check; mod order list; the four log lines; predictions before the run.

### Code

- `mod/src/ubw/begin_c138.cpp`. The detour on `AGameStateBase_OnRep_ReplicatedHasBegunPlay` (symbols.txt, RVA
  0x03C624D0, matches step 1) calls straight through on Authority, or when +0x2D0 is clear. On a client it calls
  through when `GetPlayerController(world, 0)->Pawn` is a BP_PlayerCharacterMI, and logs "no hold". Otherwise it keeps
  a FWeakObjectPtr to the GameState and logs "hold start".
- `c138_step` runs first in `ubw_post_tick`, and also from the card 120 Pong gate. It logs "pawn possessed" once,
  then calls the original once and logs "release after N ms (K host Pings held)". With no pawn it WARNs every 10 s
  and keeps holding. If the GameState dies, the hold is dropped with a WARN. `c138_load_map` (LoadMap enter) drops a
  hold left over from the old world.
- Card 120 gate (`c120_client_ready`): `c138_hold_ping()` comes first. While the hold is on, no Pong goes out. The
  origin check therefore runs after the dispatch, so a minimap BeginPlay that cleared the origin would hold the Pong
  too.
- Test switch `UBWCOOP_C138_HOLD_MS` (client, 0-30000). It forces the first hold to last at least that long, pawn or
  not, and then turns itself off.
- `c138_note_begin_play`, called from the DispatchBeginPlay detour on the client, logs the local pawn that each
  BP_PickupTracker BeginPlay sees.
- Pawn class check: across all 82 client `possess: OnRep_Pawn` lines in logs/*client*.log, the first possessed pawn
  is always BP_PlayerCharacterMI_C. A client never possesses the dropship, so the MI-only release cannot wait forever on
  the C1M1 ride.

### Deadlock check (static)

What the host waits on before it spawns and possesses the client's pawn:

1. Engine path, PostLogin, then HandleStartingNewPlayer, then RestartPlayer. AGameMode::PlayerCanRestart needs
   IsMatchInProgress, which is host state. APlayerController::CanRestartPlayer needs PlayerState and
   HasClientLoadedCurrentWorld, and that one is ClientWorldPackageName, which is set at NMT_Join. Neither reads client
   BeginPlay or MatchStarted. The game overrides none of these: the PDB publics have only the AGameModeBase and
   AGameMode versions.
2. Mod: no hook on RestartPlayer, HandleStartingNewPlayer, PostLogin or SpawnDefaultPawn. The grep finds only one
   hit, a card 046 call-site constant in objectives.cpp. The card 045 rejoin (restart.cpp) is client-side
   SetClientTravel retries, ended by the client's LoadMap return. Nothing in it waits on BeginPlay.
3. Card 120 Pong and minimap origin: the origin is set by card 058 in `detour_pc_set_pawn` (SetPawn, which comes from
   OnRep_Pawn and ClientRestart). That is possession, not BeginPlay. The host's join replays wait on the client's
   Pong, so they come after the pawn, never before it. The Ping and Pong ride the PC's channel RPCs. They need
   neither a ticking PC nor a begun world, and `c26_ping_step` runs from the engine tick.
4. While held, the world is not begun, so actor tick functions are not registered. The mod's per-frame work is all
   driven by `UGameEngine::Tick` (ubw_pre_tick and ubw_post_tick), and so is c138_step.

Nothing on the possession path waits on the client's BeginPlay or MatchStarted, so the hold cannot deadlock.

### Mod order with the dispatch deferred

The hold turns the GameState-first order into the pawn-first order: the order hb58 and every client log before hb59
ran in.

| Hook | Runs off | With the hold |
|---|---|---|
| Card 049 journal, 098 world replay, 095 door and collision replay, 096 pause lift, 056 PickupGone, 132 pose, decal and dropship replays | the host's first Pong from that client | after the dispatch: the Pong is gated on the release |
| Live host sends (c95_send_all, digest_c132:611, weapons:592, grenades:600) | the per-PC `ponged` flag | after the dispatch, same gate |
| Card 095 collision writes by the level script at BeginPlay | the client's own level BP BeginPlay | they run at the release, and the host replay lands after them and wins. Before, a replay could land first and be undone |
| Troop dropship SetState (card 132, dropship.cpp) | host frames to ponged clients | after BeginPlay has set +0x654 |
| c98/c99/c103/c120/c132 arm_map | LoadMap (sprint arm) | unchanged |
| cine_skip | level-sequence Play and the WB_Stack_C pause, engine tick | an autoplay sequence starts at its BeginPlay, now at the release, and is skipped the same way |
| HUD (BP_HUD_Manager) | spawned at runtime by the PC and GameMode, rebinds through OnPossessedPawnChanged | its spawn from the PC's BeginPlay comes after the pawn, as in pawn-first runs |
| Card 058 minimap origin, card 017 possession, own_pawn_collision_step | SetPawn and the engine tick | unchanged |

### Predicted lines (run hb60, FED_C2M1 FAST=1, c138-hold-proof.txt, client UBWCOOP_C138_HOLD_MS=3000)

Join 1 (forced):
- `config: UBWCOOP_C138_HOLD_MS=3000`
- `card138: client t=A hold start: GameState ... BeginPlay dispatch held (UBWCOOP_C138_HOLD_MS: forced)`. The pawn
  is null or MI, whichever came first.
- `card138: client t=B pawn possessed: ... (BP_PlayerCharacterMI_C) ..., N ms into the hold`
- `card138: client t=C release after M ms: BeginPlay dispatch ran (K host Pings held)`, with M >= 3000 and K >= 0.
- `card138: client t=D BeginPlay of BP_PickupTracker...: local pawn <non-null> (BP_PlayerCharacterMI_C)`, with D >= C
  and no WARN on it.
- `card120: client t=E minimap origin ... set`, with E >= C. The host's `card049: ... journal replay` comes after E.

Rejoin (card 045, real order):
- Either `card138: client ... hold start` with pawn null, then "pawn possessed" and "release after", or
  `card138: client ... no hold` with the MI pawn. hb59 had the GameState first on this rejoin, so the first is more
  likely.
- The BP_PickupTracker BeginPlay line again, with a non-null pawn. No crash at exe+0x51EB9BA.
- No `WARN card138: ... hold still on`. Client rc 0, no new crash dumps.

## Step 3: run hb60 (run 1 of 3)

DLL 8904021a22268866. Command as in tools/scenarios/c138-hold-proof.txt, client `UBWCOOP_C138_HOLD_MS=3000`.
Map FED_C2M1 (host travel option; the C1M1 lines are the startup FURL check).

Result: `run hb60: ended by scenario after 104 s; host rc 0, client rc 0`. Host FATAL/ERROR 0, WARN 0; client
FATAL/ERROR 0, WARN 92. `new crash dumps: none`. No CRASH lines. Drive rc 1, see the scenario fault below.

Join 1 (forced hold), client log:

    216657076 card138: hold start: GameState 0000000044FFA390 begun play, PC 000000008CE051A0 pawn 0000000003E400A0 (BP_PlayerCharacterMI_C): BeginPlay dispatch held (UBWCOOP_C138_HOLD_MS: forced)
    216657094 card138: pawn possessed: 0000000003E400A0 (BP_PlayerCharacterMI_C) on PC 000000008CE051A0, 18 ms into the hold
    216660085 card138: BeginPlay of BP_PickupTracker_C: local pawn 0000000003E400A0 (BP_PlayerCharacterMI_C)
    216661461 card138: release after 3004 ms: BeginPlay dispatch ran (0 host Pings held)
    216661481 card120: minimap origin 0000000003E400A0 (BP_PlayerCharacterMI_C) set: Pong to the host from now on (held 0 ms)
    216661516 card049: replayed Chapter(None) ... AddObjective(LOS_O01) ... AddSubObjective(LOS_O01, LOS_O01_SO01)

The release line is written after the original returns: the dispatch ran at 216660080 (hold start + 3004) and
took 1381 ms; the tracker's BeginPlay ran inside it with the MI pawn. Order matches the prediction: hold start,
pawn possessed, tracker BeginPlay with a non-null pawn, release, card120 origin, card049 replay.

Rejoin (card 045, try 1, 7696 ms after the drop): `card138: no hold: GameState 00000000B79C2740 begun play with pawn
0000000052FED570 (BP_PlayerCharacterMI_C)`, tracker BeginPlay with that pawn at +4 ms, card120 origin at 216671835,
card049 replay at 216672037. The pawn arrived first in both joins, so hb59's GameState-first order did not recur
naturally; the forced hold stands in for it.

Not predicted:
- Drive line 19 FAIL: the rejoin card120 origin line (216671835) was written before line 18's pawn wait returned
  (+42.1 s), so `new` did not see it. Scenario fault; the line is in the log.
- Host rows that reach a client before its Pong: card132 `recv 1 ... Anim` at 216670190, 2 ms before the rejoin
  dispatch (no-hold path). The Anim relay (enemies.cpp:3187), Gib (enemies.cpp:3932), Fire (hooks_ubw.cpp:509),
  Down (death.cpp:573) and Warp (objectives.cpp:1700) send to every remote PC with no `ponged` check. So step 2's
  "every live send goes to ponged clients" is wrong for these five. While a hold is on they can reach actors that
  have not had BeginPlay yet. In hb60 this one was harmless: `no NPC matched not played`.

WARN diff against hb58 (client, prefix counts): hb58 had 346 card132 and 23 card130. hb60 has 92 card132, all
`util niagara`: 88 NS_FX_Bugs_WalkGround and 4 NS_FX_Humans_WalkGround. Both kinds are in hb58. No new kind and no
card138 WARN.
