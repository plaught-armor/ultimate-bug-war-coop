# Card 600: player 0 is not remapped for the caller at exe+0x3C7F202 (mortar zone, mech drop pod)

## Finding

This came out of the card 582 closure list (2026-11-22). In all 18 C3M1 host logs with the C99 log on, these reads keep the host's answer:

- `player 0 not remapped: context BP_MortarZone_C, caller exe+0x3C7F202`;
- BP_TacSupp_Pod_MechDrop_C, with caller exe+0x3C7F202 (and exe+0x5251EEF), owner and instigator none.

A client standing alone in a mortar zone may never be targeted. A client's mech drop pod may resolve against the host. Both reads share one caller, so the gap is one class: that call site falls outside every card 099 rule (the place, damage, seat and instigator rules).

## Trace first (static, before code)

1. Resolve exe+0x3C7F202. Name the function it sits in, and say whether it is the generic kismet thunk for GetPlayerCharacter or GetPlayerPawn, so that every Blueprint reaching player 0 through it lands here.
2. For BP_MortarZone_C, read the kismet dump. Say which player 0 read picks the mortar target, and what the step 2 place rule would need to see to remap it.
3. For BP_TacSupp_Pod_MechDrop_C, say whether the pod is a client tac support call or a scripted level drop. Find which player it gives the mech to, or aims at.
4. Pick one rule for both: nearest player, or the player inside the zone or calling the pod. It goes in the card 582 condition table, so a new row is data.

## Proof

Fold this into the next C3M1 run:

- A client alone in a mortar zone gets a `card099 ... remapped` row for BP_MortarZone_C, and mortar fire lands near the client.
- A client mech drop resolves to the client: a remap row, and the mech goes to the client.
- 0 mismatches.

## Trace (junior, 2026-11-22, static plus b453 logs)

1. **Caller.** exe+0x3C7F202 is inside `UGameplayStatics::execGetPlayerCharacter`, the Blueprint thunk (card 099 done table). Every Blueprint `GetPlayerCharacter(self, 0)` lands on this caller, so it cannot pick a rule by itself. The context class has to pick it.
2. **Mortar zone.** BP_MortarZone_C reads player 0 in three places:
   - OnTriggerEnter @242: `GetPlayerCharacter(self,0)`, then `TriggerArea.IsOverlappingActor`, which sets `PlayerInside`.
   - GetMortarLocation @48 and @117: `GetPlayerCharacter(self,0)`, IsValid, K2_GetActorLocation. The mortar spawn location is that location plus random scatter plus MortarSpawnHeight.

   BP_MortarZone_C is already in `kC582Classes`, so `c582_place_pc` answers with the player nearest the zone. The C3M1 logs carry remap tallies of 416, 428 and 32 rows for this context. The `not remapped` row is `c582_kept_row` firing when the host was the nearest player. That is the correct answer, not a gap. **No code.**

   Side note, not chased: OnTriggerExit sets `PlayerInside = false` with no player check. If two players are inside and one leaves, mortars may stop while the other is still inside. The run proof below shows whether this matters.
3. **Mech drop pod.** This is a scripted level drop, not a client tac support call. The BP_MechDropPlatform `Activated` event spawns the pod deferred with GoalLocation set to the pad, GuaranteedActorsToSpawn set to [Mech To Spawn] and Owning Pad set to self. The mech goes to the pad, never to a player. The pod's SpawnDesiredFX reads player 0 twice:
   - @880 (0x3C7F202) feeds `ApplyDamageAtLocation(self, <player 0>, template 400 dmg r300 bIsSelfDamaging, loc+30z)`. Here player 0 is only the instigator of the landing blast, which is credit and friendly-fire scaling. This is the same shape as card 582 G9 (dropped).
   - exe+0x5251EEF is `StartCameraShakeAtLocation(self, shake, loc, 500, 3000)`, the host's own local camera shake. It must stay local.

   On the client the pod is the host's relayed copy (card105 blocks the client's Blueprint spawn, card104 maps the copy). The impact sound reaches the client as a relayed world row (b453: `audio row MS_TACT_TacticalSupport_DropPod_Impact from a world row`). b453 shows no client camera shake row for the pod landing. That is a possible all-effects gap for card 136/551 owners, carded separately if wanted.
4. **Rule.** One rule covers both reads: the existing card 582 place row (the player nearest the context actor). The mortar zone is already in the table. The pod gets no row: its player 0 read is the blast instigator only, and remapping it would only move the friendly-fire credit. Kept rows for BP_TacSupp_Pod_MechDrop_C are expected, the same as the instigator verdicts for G9 and card 310. **No code change.** The "client mech drop resolves to the client" proof line does not apply, because the mech goes to the pad.

## Run proof (fold into the next C3M1 run, C99 log on both sides)

Predictions:

- With the host warped more than 3000 uu away and the client inside a BP_MortarZone_C trigger, the host log has a `card099: player 0 remapped` row with context BP_MortarZone_C and the client's copy and PC.
- The client log has mortar impact audio and FX rows within about 1500 uu of the client (the scatter radius plus the spawn height drop).
- With both players inside, the client leaves and mortars keep landing near the host. If they stop, the side note in step 2 is real and gets a card.
- 0 mismatches.

## Senior decision (2026-11-22)

The trace is accepted: the mortar target read is already covered, and the pod needs no remap row. The card stays open for the two side finds, because both are real co-op gaps:

1. **Mortar zone exit (code).** BP_MortarZone_C OnTriggerExit clears PlayerInside without checking which player left. With both players inside, one leaving stops the mortars on the other. Fix it by class, using the card 596 rule: on the host, the exit runs only when the leaving pawn is the last living player that still overlaps the zone. Before coding, check whether any other BP trigger in the kC582Classes or card 227 lists has the same unchecked exit. Cover those with the same rule.
2. **Pod landing camera shake (all effects sent).** The pod's StartCameraShakeAtLocation at 0x5251EEF runs on the host only. The client near the pad gets no shake. Find why the card 551 or shake relay path does not carry a world shake at a location. Fix it so that every StartCameraShakeAtLocation on the host reaches each client inside its outer radius. The client shakes only once: no double play when its own copy of the level script also fires.

Proof: one C3M1 run in the next build. It covers the mortar predictions above, plus both players inside the zone with one leaving (mortars keep falling on the one who stays), plus a pod landing with the client within the shake radius (a client shake row, once).

## b454 (junior, 2026-11-22): sweep, fix, shake trace

**Mortar zone exit: no new code.** BP_MortarZone_C is an ASTPlayerPresence subclass (card 244 logs it as a presence; its OnTriggerEnter and OnTriggerExit binds are the presence script delegates). `detour_c227_exited` with `c596_hold_exit` (card 596) already holds its exit on the host while another living player is inside. The proof owed is the row `card596: host ... exit held on BP_MortarZone_C_x`.

**Sweep (kC582Classes, card 227 triggers, other EndOverlap users).** Three classes bind `PlayerDetection.OnComponentEndOverlap` to a custom event that checks only `IsPlayer(OtherActor)`:

- BP_DestructibleSpawner_Base: PlayerExit @2929, then @1838 PauseReinforcementsInAllWaves when ActiveOnlyWhenPlayerNear.
- BP_TentSpawner_HumanGameplay: PlayerExit @1210, then @915 PauseReinforcementsInAllWaves.
- BP_EM_PrefabBase: Exit @1250, then @996/@930 `isPlayerOverlapping = false`.

Not affected: BP_KillableBuilding_ElectricPylon (no exit), BP_Door_Base (card 161), BP_Building_BugNest_BugGameplay_New (bug mode, skipped). FED_C1M1_Scripting's EndOverlap use is not checked.

**Fix (b454, `objectives.cpp` `c600_exit_pe`, called from `c128_pe_run` on the host).** For those three classes the Exit or PlayerExit event is refused while another living player still overlaps the same component (`c596_others_inside` now takes the component and uses `g_c321_prim_overlapping`). Row: `card600: host t=... PlayerExit on <actor> (<class>) by <pawn> held: 1 other player inside the <component>`, or `runs (last living player out)`. Build b454: dwmapi 0ed48e7e0c631437, ubwperf 753e7e42f8fe872d, kept in build-b5.

**Pod landing shake: no code yet, the run decides.** Static trace: `StartCameraShakeAtLocation` (0x05251EA0) calls `UGameplayStatics::PlayWorldCameraShake` (0x03C630E0), then `APlayerCameraManager::PlayWorldCameraShake` (0x04056330). That walks every PlayerController, remote ones included, and calls the virtual ClientStartCameraShake, a native RPC for a remote PC. The client runs it through `ClientStartCameraShake_Implementation` (caller exe+0x403CE82). Past client logs with the C99 log on show the path works: c588b416a-client has 199 world shakes with that caller. The b453 "no client shake" was void: the client ran without UBWCOOP_C99_LOG and stood 51748 uu from the pod. Two risks stay open, both measured by the run: the scale comes from the server's copy of the client camera (it may be stale), and the client's relayed pod copy might play a second shake.

## b454 run predictions (tools/scenarios/c600-c3m1-b454.txt, run c600b454a, C99 log on both sides)

1. Pod: the client stands about 1500 uu from the pod goal when it lands. The client log has exactly one `card099: client CameraManager::StartCameraShake Shake_Generic_ExplosionBrief_2ndPass_C scale >0 ... caller exe+0x403CE82` row within 500 ms after its `audio row MS_TACT_TacticalSupport_DropPod_Impact` row. 0 rows is a gap; 2 rows is a double play. Either gets the shake fix.
2. Mortar, client alone in BP_MortarZone_C_3 with the host about 13000 uu away: the host log has a `card099: player 0 remapped (place)` row with context BP_MortarZone_C. The C_3 launch rows in that window sit within about 1500 uu (x, y) of the client.
3. Both players in C_3, then the client leaves: `card596: host ... exit held on BP_MortarZone_C_3: ... 1 player inside`. The C_3 launch rows in the next 8 s sit within about 1500 uu of the host. The host then leaves: `card596 ... runs: last living player out`.
4. Tent spawner, both players at 200-250 uu, the host leaves: `card600: ... PlayerExit on ... (BP_TentSpawner_HumanGameplay_C) by ... held: 1 other player inside`. Then the client leaves: `runs (last living player out): 0 other players inside`. No row at all means the stand was outside the spawner's PlayerDetection (a stand finding, not a code fail).
5. 0 mismatches, no new WARN against the c582b453a baseline.

## b454 run c600b454a (2026-11-22): results

Run: `logs/c600b454a-{host,client}.log`, `.drive`. 176 s, both rc 0, no FATAL or ERROR, no crash dumps. WARN host 14, client 6. Drive rc 2: one hard fail at line 49 (the mortar held row), which also cut line 55.

1. **Pod landing shake: path proven, no class fix needed.** Host `StartCameraShakeAtLocation` goes through the engine's `PlayWorldCameraShake`, which calls the native ClientStartCameraShake RPC on every player controller. The client log has exactly one row for the landing: `card099: client CameraManager::StartCameraShake Shake_Generic_ExplosionBrief_2ndPass_C scale 0.00 ... caller exe+0x403CE82` at 206479764, 1 ms before the client's DropPod_Impact audio row (206479765). There is no double shake. The scale is 0 because the pod lands about 2.6 s after the hold ends, during the intro ride. The client's view target was BP_PlayerTransportDropship_C (card399 "ride on"), 23110 uu from the pod, even with the pawn warped to the pad. The engine scales the shake by the camera location, so 0 is correct for that camera. A scale above 0 cannot be staged at this pod.
2. **Card 600 spawner and prefab exits: proven.** BP_TentSpawner_HumanGameplay_C and BP_EM_1FactionLoop_C sit at BP_MortarZone_C_3, so the mortar block exercised them. At 206545094 the client left: `PlayerExit on ... (BP_TentSpawner_HumanGameplay_C)` and `Exit on ... (BP_EM_1FactionLoop_C)`, both `held: 1 other player inside the PlayerDetection`. At 206558335 the host left: both `runs (last living player out): 0 other players inside`. The tent block's own try waits saw nothing, because the exits had already run.
3. **Mortar zone exit: no gap. This corrects the b454 "no new code" claim above, whose reason was wrong.** No card596 row was logged for any BP_MortarZone_C, so `detour_c227_exited` did not hold the exit. But the exit has no effect to hold. In `BP_MortarZone` (build/kis_txt/BP_MortarZone.uasset.txt), OnTriggerExit_Event only runs `PlayerInside = false` (@400), and nothing reads `PlayerInside`. Drop Mortar (@424) gates only on `Active`, which only `Toggle Mortar Zone Active` sets. The level script calls that with false for C_1 and C_2 when the Beach substage completes (FED_C3M1_Scripting). GetMortarLocation reads player 0 only to keep drops out of SafeDistance, and card 582 remaps that read to the nearest player (the `card099 remapped (place)` row with context BP_MortarZone_C is in this run). The zones fire from load with no players needed. So one player leaving never stops the mortars on the other, and no code is needed. The sweep found the same unchecked-exit shape only in the three card 600 classes, which b454 covers.
4. **Remap row.** It is printed once a session (206478446, during the ride), so the try wait in the mortar-alone window could not see it. It is not a fail.

Next: gate c448b454a on b454 (dwmapi 0ed48e7e0c631437).

## Closed (senior, 2026-11-22)

The mortar zone exit is no gap (PlayerInside is never read). The spawner and prefab exits are held by the last-player rule (c600b454a). The pod shake reaches the client through the engine RPC. b454 passed gate c448b454a and goes to lane 1.
