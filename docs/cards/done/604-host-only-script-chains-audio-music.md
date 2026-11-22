# Card 604: placed audio and music from host-only level script chains never reach the client

## Finding (card 301 FED_C4M1 full pass, 2026-11-22, static)

Some level script chains start only on the host, because the client runs no encounters (card 006). The sources are:

- `EncounterDelegate` and `EncounterIndexDelegate` binds on SetWaves managers;
- custom events called from those binds (`Add Bridge_SO4`, `Add Metro_SO5` via its 2 s delay, `Bombing Run`, `Destroy Bridge` 1-3);
- the pickup delegate when the host runs the pickup (card 254).

The visible effects of these chains reach the client through class relays:

- hides go through the hb46 hide row (`c98_host_hide`);
- Niagara Activate and Deactivate go through card 136 point 6;
- `LD: Destroy` goes through card 423 (ext_c132.cpp);
- deferred actor spawns go through card 140;
- kills and hides on replicated actors go through engine replication.

Two kinds of effect have no relay:

1. **Placed audio components.** `BP_TriggerableAudioEmitter` TriggerAudio, StopAudio, Power Off, PowerOn and Edit Volume end in `UAudioComponent::Play`/`Stop` or volume writes on a placed actor. Card 136 captures PlaySoundAtLocation and SpawnSound* (point 1), and card 418 captures SpawnSoundAtLocation plays only. A component Play on a placed emitter has no host row.
2. **2D music and submix.** `PlaySound2DFName` (music stings), `EndMusic`, `ClearMusicTriggers`, the `Stadium * Handle Music` functions, and `Set/ClearSubmixEffectChainOverride`.

## C4M1 sites (build/kis/c4m1.txt)

| Source (host only) | Chain | Missing on the client |
|---|---|---|
| SetWaves C_2 EncounterDelegate @8928 | @9500 Stadium Obj Complete Handle Music; @9696 Toggle sparklers(false): StopAudio on NiagaraActor_0, 17-29 | stadium music change; sparkler audio keeps playing (the Niagara stop is relayed, card 136) |
| SetWaves C_11 EncounterDelegate @17938 -> @17278 | Add Metro_SO5 after 2 s -> @16358 BP_TriggerableAudioEmitter_C_7 TriggerAudio(true) | metro emitter sound |
| SetWaves C_11 EncounterIndexDelegate @19934 | BP_HordeZone_C_1 Update Progress Bar(1.0 / 0.6 / 0.3) | horde zone bar, if it is a world widget (to check: replicated or HUD) |
| WeaponPickup EPulse88 C_0 @18778 | EndMusic or ClearMusicTriggers, @19013 BP_TriggerableAudioEmitter_C_0 StopAudio, PlaySound2D | music change, emitter stop (BP_Flare_C_0 Deactivate: Niagara, relayed) |

Sites started on both machines are not gaps:

- objective events, which the client replays natively (card 100/499);
- presences, which the client copy runs (card 288);
- switch presses (card 602);
- BeginPlay and Loaded.

The C4M1 instances of those are Half Time Show (from Stadium Fight 1 -> 2, after Add Stadium_SO2), Toggle sparklers(true), the Solo music, the PanelSwitch speaker PowerOn and Edit Volume (@4279-5724), and DestroyMetroFrontage at load.

## Fix options (decide statically before any run)

- **(a) Effect relay.** The host captures `UAudioComponent::Play` and `Stop` (PlayInternal is already hooked, `detour_pl_play_internal`) on placed, unreplicated owners outside the client-own path. The host sends the placed name plus the component, and the client plays or stops its own copy's component. Volume writes (`Edit Volume`) need a row too, or a read of the component's VolumeMultiplier at play time. This matches the card 136 model, which is one effect class with existing keys. Music (2D, per listener) needs a separate row: the music trigger name, played on every client.
- **(b) Source relay.** As in card 602, the host relays the encounter-delegate broadcast, and the client broadcasts its own copy's delegate, so the whole chain runs on both machines. This is risky: the client chain would also run the effects already relayed (Niagara, hide, `LD: Destroy`), so they would play twice, and it would make objective and encounter calls on the client. Not recommended.

(a) is the class fix. Size the music rows first: count how many `PlaySound2D*` and music calls the eight FED scripts make from host-only sources.

## Proof (when fixed)

A C4M1 run through the stadium fight end (SetWaves C_2 encounter end). The client log has the audio rows for the StopAudio of NiagaraActor_17-29 and for the music change. A client sparkler shot shows them off.

## Sizing (static, 2026-11-22)

There are 63 music call sites over the eight FED scripts: C1M1 9, C2M1 5, C3M1 5, C3M2 6, C4M1 17, C4M2 7, C5M1 4 and
C5M2 7. Most fire once per map, so the rate is well under 1 row per minute. No log has counts: c244b443a predates the
card 582 tally. Rows are text of about 20-60 B, so a map costs a few hundred bytes at most. A binary encoding would
save tens of bytes per map and is not worth the loss of readable rows. The b457 run measures the real rate.

## Design as built (b457, audio_c604.cpp)

Option (a), one class:

- Host capture: PlayInternal and Stop on a placed, unreplicated owner outside every card 136 capture point, plus
  TriggerSpecificMusic, EndMusic, ClearMusicTriggers, PlaySound2DFName and PlaySound2D called with a level script
  world context.
- One ChanOp::LevelAudio text row per capture, to every ready client: `P owner comp vol% start`, `S owner comp`,
  `T 0/1 name`, `E 0/1 fade`, `C 0/1`, `F vol% pitch% start name`, `2 vol% pitch% path`.
- Client pairing: a 32-entry ring of recent calls (own and host) keyed by kind and object, 5 s window. A call whose
  twin from the other side is in the ring consumes it and does not run. Stops are never paired.
- A send cap of 16 rows/s bounds a looping retrigger. Submix set and clear are counted and logged only.
- Per-map tally at map end: rows, bytes, B/row, paired calls skipped, host rows missed, dropped, submix counts.

## C4M2 sites (card 301 full pass, 2026-11-22)

- PlaySound2D @11831 (egg chain), @21611 and @21709 (LightFadeOut / ShortFadeUp); ClearMusicTriggers @2434
  (BrainBugDead). Hooked in b457.
- Plain `PlaySoundAtLocationFName`: MS_EMIT_BrainBug_Teleport_Indicator @20028 (satchel pickup C_9, host only),
  Teleport_In @14520 and Teleport_Out @15324. Not hooked in b457 (only the WithAIAwareness form is, card 099). Next
  class step: add it to the same row set as a located sound row.
- `ExecuteMusicTrigger`: C4M2 C_73 @10933 (presence, both machines, no relay needed) and C1M1 @1024, @6360, @6895
  (sources to be checked). Not hooked in b457.

## b457 run predictions (c604b457a, C4M1 stadium end)

1. The host logs `card604: host ... sent to 1 of 1 clients` rows for the SetWaves C_2 stadium-end chain: `S` rows for
   the NiagaraActor sparkler components (NiagaraActor_0, _17-_29) and music rows (`T`, `E` or `C`) from Handle Music.
2. The client logs `card604: client t=... host row ...` for each of them, with no `WARN card604: client ... no placed`
   and no `not played` rows.
3. At map end both tallies print. The client's host rows missed count is 0. Calls the client also makes itself show
   up as paired calls skipped, not as a second play.
4. Submix set and clear counts are equal on both sides.
5. Rows are 20-60 B (B/row in the tally), and well under 1 row per minute outside the stadium end.
6. A client shot of the sparklers after the stadium end shows them off, matching the host shot.

## c604b457a and c604b458a (2026-11-22) and the b459 fix

- **c604b457a: host crash at map load (36 s).** `detour_c604_stop` called the pdb address of
  `UAudioComponent::IsPlaying` (exe+0x3A35C20), a folded, this-adjusted body that reads `[rcx-0x1AA]`. b458 reads
  `UActorComponent::bIsActive` directly (byte +0x97 bit 6, the offset `execIsActive` reads at exe+0x3A60C85) and drops
  the binding. The `UAudioComponent::Play` address (exe+0x3A41F30) was disassembled too: a real prologue that reads
  StartTime from xmm1.
- **c604b458a (FED_C4M1 to Stadium_O1, 464 s, no crash, mismatches 0 in 28 rounds).** The host sent 93 rows
  (component play 61 rows at 46.1 B/row, component stop 31 rows at 45.0 B/row, EndMusic, ClearMusicTriggers). The
  client received all of them, but found **none** of the 94 component owners (`WARN card604: client host row ... no
  placed BP_KillableCrate_05_C with component LoopingSFX here`). Cause: the host wrote names with `object_name`, which
  drops the FName number, so every crate was sent as `BP_KillableCrate_05_C`. The client's finds
  (`c50_find_placed_any`, `c95_component`) match the numbered name. The host quit totals also show `1073 dropped over
  16/s`, with no kind recorded.
- **b459 fix.** The P and S rows carry `c50_name` for owner and component (with the `_N` suffix). A name over 64 chars
  is not sent and logs a WARN. The cap drops are now counted per kind in the totals rows (`..., N dropped over the
  cap`), and the first drop in each second logs its row (`WARN card604: host ... over 16 rows/s, dropped: "..."`).

## b459 run predictions (c604b459a, same scenario)

1. 0 client `no placed ... here` WARN rows for component rows, or each one names an owner that the client log shows
   was destroyed or never streamed in.
2. The client applies the play and stop rows (no WARN after `host row component play/stop`).
3. The per-kind drop counts name the kind behind the 1073 drops. If it is component play at load, the next card sizes
   a load-time gate. It is not chased in this build.
4. Host component rows stay under 50 B/row.
5. Mismatches 0. The only new WARN class is the card604 drop row.

## b459 results (c604b459a, FED_C4M1 to Stadium_O1, 476 s, drive rc 0, 2026-11-22)

No crash, no void. Mismatches 0 in 30 rounds. The client received 157 host rows and found 108 of them (it found 0 of
94 in c604b458a).

1. **Partly met.** 49 rows are still missed, in two shapes:
   - **36 S rows for runtime-named components.** Example: `S BP_SatchelBuilding_Flakbug_C_1
     AudioComponent_2147474350`. The flak buildings (`_0` 14 rows, `_1` 14 rows, `_2` 8 rows) spawn their audio
     components at run time (SpawnSoundAttached), so the component's name is per machine and no name can match it.
     Fix shape: for a component whose name number is in the runtime range, send the owner plus the sound asset name,
     and have the client stop its own component on that owner that plays the same sound.
   - **13 P rows `P NiagaraActor_N Audio`** (N 0 and 17 to 29, the stadium NiagaraActors). The client finds the owner
     (the drive's `look actor NiagaraActor_17` ran on the client), but has no component named `Audio` on it. Next
     check: is `Audio` a component the host adds at run time, or one the client's copy names differently?
2. **Met** for the rows found: no WARN after them.
3. **Met.** All 1073 drops are component play, in one second: `WARN card604: host t=214552410 component play
   BP_Bench_Planks_Destructible_01_C_3 over 16 rows/s, dropped: "P BP_Bench_Planks_Destructible_01_C_3 LoopingSFX 100
   0.0"`. The same millisecond the host runs `LoadSettingsFromSave` and `GA_ResetStatusCheckpointLoad_C`: a checkpoint
   reload replays every placed component's play. Next card: gate the relay during a checkpoint reload, since the client
   reloads too and plays its own. Not chased in this build.
4. **Partly met.** component play 48.4 B/row (61 rows); component stop 58.0 B/row (36 rows), over 50 B because of the
   runtime names in point 1. The asset-name shape in point 1 would also shorten those rows.
5. **Met.** New WARN classes from this build: the card604 drop row only. Host `WARN card132 ... host hit` (1 row) and
   client `WARN card152` (4 rows, guid 0x796) are older classes, also seen in soak3m1b367a and soak5m1b366a.

card 603 in the same run: 0 drift rows.

### Gate c448b459a (FED_C1M1, b459): passed, with a WARN flood from the same runtime-name shape

The gate passed: drive rc 0, mismatches 0 in 15 rounds, and the TRY-FAIL lines were 43, 55, 155, 185, 186, 193 and 212
(the allowed set). The client WARN count rose from 4 (c448b456a) to 261. 254 of the new rows are `WARN card604: client
host row "P BP_RandomEmitterVolume_C_N AudioComponent_<runtime number> 100 0.0": no placed ...`, 24 to 30 rows for each
of the 9 volumes (`_0` to `_8`). The host sent 80 rows in this run. Each random emitter volume spawns a fresh audio
component for each sound, so its name never matches on the client. Most likely the volume's own random logic also runs
on the client.

**Proposed rule (for the senior to decide; not built):** the host does not relay a component whose FName number is in
the runtime range (it is not a placed or archetype component). A host-only chain on a runtime component, such as the
flak building stops in c604b459a, is sent as owner plus sound asset instead, and the client acts on its own component
on that owner that plays the same sound. This removes the gate flood and the 36 S misses, and it shortens the rows.

## Senior decision for b460 (2026-11-22)

The card 604 relay covers only components that exist in both worlds: components loaded with the level or the actor's class, meaning the placed actor's own components and default subobjects. Test with the object flags (loaded from package, or default subobject), not with an FName number range. Runtime-created audio components are not relayed. Each one is skipped and counted by sound asset, with one summary row per 60 s and no per-row WARN.

Why: a runtime audio component is made by a sound spawn (SpawnSoundAttached, SpawnSoundAtLocation) or by a BP's own CreateComponent. The spawn path is already relayed (cards 136 and 455), so relaying its later Play as well risks a double play, and the name can never match anyway. Owner-plus-asset matching would guess at which client component is meant. Do not build it.

The proof that the skip is lossless is the same run. For every skipped asset, show either a card455/card136 row for that asset and owner, or that the owner runs the same chain on the client (BP_RandomEmitterVolume_C is placed: check whether its timer also runs on the client copy). Any asset with neither is a real gap. Name it here, and give it a spawn-relay fix, not a component-name fix.

The NiagaraActor "Audio" misses follow the same rule. A runtime NiagaraActor is not relayed by component name.

Also in b460:
- Checkpoint reload burst (1073 drops at t=214552410). Name what the host restarts at LoadSettingsFromSave/GA_ResetStatusCheckpointLoad, and whether the client runs its own reload. If the client reloads too, those rows are not relayed during the reload.
- The music and submix rows stay as built.

Proof: the gate's client WARN count is back to the b456 level (4 to 6), plus the per-asset lossless check above in one C4M1 run.

## b460 fix and predictions (c604b460a C4M1, then gate c448b460a)

Fix (b460, dwmapi 157b37d09efb2004), per the senior decision above:
- `c604_component_play` and `detour_c604_stop` relay only a component that is loaded with the level or the class:
  ObjectFlags (+0x8) has RF_WasLoaded (0x80000) or RF_DefaultSubObject (0x40000). A runtime component is not sent.
  Both sides count it by sound asset and first owner, and print one summary per asset every 60 s (`card604: <side>
  t=... runtime component not relayed (60 s): <asset> on <owner>: N plays, N stops`) plus a skip summary row. No
  per-row WARN.
- `c604_send` sends nothing while the host runs an in-place checkpoint load (card 121, `load pawns 1`, flag
  `g_c121_in_place_load`); the card290 travel row is sent first, so the client reloads the map and plays its own
  placed components. The totals row counts these as `not sent in an in-place load`.

Predictions:
1. Gate c448b460a client WARN count is 4 to 6 (c448b456a had 4). No `WARN card604: client host row ... no placed`
   row names a `BP_RandomEmitterVolume_C` owner or an `AudioComponent_<n>` component.
2. Both sides print `runtime component not relayed` rows. For each asset and owner, either the client count is near
   the host count (the owner runs its own chain on the client, as the random emitter volumes are expected to), or the
   host has a card455 or card136 row for that asset and owner. An asset with neither is a gap for the spawn relay.
3. The C4M1 run has 0 card604 drop rows. If a death-screen load happens, `not sent in an in-place load` is above 0
   and the client's placed destructibles still play LoopingSFX after its reload (client playlog rows).
4. Mismatches stay 0. No new WARN class.

## b460 results (c604b460a, C4M1, lane 2, dll 157b37d09efb2004)

Run: drive rc 0, host and client rc 0, no crash dump, 31 sync checks with 0 mismatches. Client WARN 21 (c604b459a 72,
c604b458a 116), host WARN 39. No new client WARN class against c604b459a.

1. Holds. Gate c448b460a (C1M1, lane 2): drive rc 0, host and client rc 0, 13 sync checks with 0 mismatches, client
   WARN 6 (c448b456a 6, c448b459a 261), host WARN 4, the only retry is TRY-FAIL line 43 (allowed). None of the client
   WARNs is a card604 row. b460 went to lane 1 on 2026-11-22 (dwmapi 157b37d09efb2004, ubwperf dec1f214e33499d4). In C4M1 no `no placed` row names a `BP_RandomEmitterVolume_C` owner or an
   `AudioComponent_<n>` component. One `no placed` row is left: `P NiagaraActor_0 Audio 100 0.0` at t=216405619, sent
   in a burst with NiagaraActor_17 to _29, which all found their placed copies. The quit totals count it as `1 host
   rows missed here`. These are the only 2 card604 client WARNs. It is carded as card 605.
2. Holds. The host skipped 7 calls on 3 assets, all on `BP_SatchelBuilding_Flakbug_C`:
   `MSS_EMIT_FlakBug_Ambient_Idle` (0 plays, 5 stops), `EMIT_FlakBug_Fire_MovementGuttural` (0 plays, 1 stop) and
   `MS_ATK_FlakBug_Fire_ChargeUp` (0 plays, 1 stop). All 0 plays: the host starts these sounds with an anim notify
   (`card408: host fx local SpawnSoundAttached ... by notify`), not through a component play. The client runs the same
   notify chain on its copy: client card173 `remote sound MSS_EMIT_FlakBug_Ambient_Idle from
   BP_SatchelBuilding_Flakbug_C` rows (9 rows for that asset, 2 for each of the other two). Every skipped asset is
   covered by the owner's own chain on the client. No gap; no spawn relay change. The client printed no skip rows of
   its own, because it relays nothing.
3. Holds. 0 drop rows on either side. The checkpoint reload burst is the death-screen load at t=216121664
   (`card121: host save engine load runs (Load, context WB_PlayerDeathScreen_C, load pawns 1, destroy actors 1)`). It
   restarts every saved actor's placed components on the host. The client reloads the whole map (client map loaded at
   t=216127459, after the card290 travel row) and its placed components restart there. The host sent none of the
   burst: `1089 not sent in an in-place load`.
4. Holds. 0 mismatches. No new client WARN class. The one new host class, `objective wait Park_SO1_C -> failed`, is a
   scenario wait retry and not a card604 row.

## Closed (senior, 2026-11-22)

b460 passed: c604b460a, and gate c448b460a with client WARN 6. The skipped runtime components are covered by the owner chain on the client. The reload burst is not sent during an in-place load. The one remaining NiagaraActor_0 row moved to card 605. b460 is on lane 1.
