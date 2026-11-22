# 606: BP_HordeZone Begin / End KillEmAll is not relayed (card 301 class gap, 7 maps)

Found by the card 301 FED_C5M1 full pass (senior, 2026-11-22). Verified from existing logs the same day (see
"Evidence"); one part is proven, the rest is narrowed below.

## Evidence (c502b277a, FED_C4M2 plaza horde, b277)

- Begin ran on both machines (client local timeline row t=750042921, then the host's card 098 'L' call 1 at
  750043023). End ran on the host only: host sent 'L' call 3 (ReverseFromEnd) at 750139736, the client applied it at
  750139841, and no client-originated FadeIn call 3 exists.
- Collision, proven gap: the host's card 098 tally for BP_HordeZone_C collision changes is 213, the client's 142.
  ToggleCollision(true) makes 2 changes per CollisionMesh (SetCollisionEnabled, SetCollisionProfileName) and
  ToggleCollision(false) makes 1 (SetCollisionEnabled(0)). 142 = 71 meshes x 2 on both sides; the host's extra
  71 = 71 x 1 is the End's collision off. The client's 71 wall meshes stay collidable and visible
  (SetHiddenInGame(true) is in the same loop). Card 137 logged them as "client play-time change ... by local sim".
- Fog fade, already covered: the FadeIN and LerpPostProcess timeline calls are relayed by card 098, and their
  Update functions drive the Fog and FogBlock custom primitive data, IsHordeModeOn, and the PostProcess blend weight.
  So the fog fades out on the client. Only the Fog component's final SetHiddenInGame(true) in the passive setup is
  missed. It is not visible once the fade is at 0.
- HordeAudio loop: very likely covered by card 604 (b460). HordeAudio is a component of a placed, unreplicated actor,
  loaded with the level, so its Stop is a card 604 'S' row. c502b277a predates b460, so no log shows it yet.
- Not covered by any relay found: HordeSystem (Niagara) Deactivate, ToggleHazardZones(false) (SetHazardActive(false)
  on the shunt zones), Active = false, the KillEmAllEnd PlaySound2D (context is the zone, not *_Scripting_C, so it
  is not a card 604 '2' row), DustBurstFX, and DisableFakeTick.

## Finding

BP_HordeZone (Content/Blueprints/HordeMode/BP_HordeZone, listing build/kis/hordezone.txt) is the arena wall of a
"kill them all" fight. It has no replicated state and no RepNotify: each machine runs its own copy, and what that copy
shows depends only on which `Begin KillEmAll` / `End KillEmAll` calls its own level script makes.

- `Begin KillEmAll` (@985): EnableFakeTick, HordeAudio.Play, Active = true, SpawnSound2D KillEmAllBeginSFX, then
  `Kill Em All Passive Setup`: HordeSystem (Niagara) Activate, ToggleCollision(true) (the CollisionMeshes get the
  blocking profile), ToggleHazardZones(true), Fog visible.
- `End KillEmAll` (@1266): DisableFakeTick, HordeAudio.Stop, Active = false, PlaySound2D KillEmAllEndSFX,
  BeginSFX stop, DustBurstFX, PostProcess toggle, then the passive setup with Active false: Niagara off,
  collision off and meshes hidden, hazard zones off, fog hidden.

When one of these calls comes from a chain whose source runs on the host only (an encounter delegate), the client's
copy never changes. Two sites are traced:

| Map | Call | Source | Client result |
|---|---|---|---|
| FED_C5M1 | HordeZone C_1 End @16762 | SetWaves C_0 EncounterDelegate -> @26200 -> @26150 -> @16475 | wall collision and meshes, horde Niagara and hazard zones stay on for the client (fog fades by card 098) |
| FED_C4M1 | HordeZone C_1 End @17278 | SetWaves C_11 EncounterDelegate @17938 | same |
| FED_C4M2 | HordeZone C_1 End @5484 | host-only (c502b277a: no client-originated end) | proven: client collision tally 142 vs host 213 |

The begin calls in C5M1 come from a switch press (Switch C_1 -> @28400 -> @8418 -> TriggerLoopingWave @26012), which
card 602 runs on both machines, so the begin plays on both. The other maps (C2M1 15291/21846, C3M1 3932/17877,
C3M2 1429/11208, C4M2 4994/5484, C5M2 3331/21630) are not traced; any begin or end there with a host-only source has
the same gap.

The client's leftover wall is the worst part: its 71 CollisionMeshes keep their collision (the profile the begin set,
plus channel 14 Block) and stay visible, while the host's copy has none. Whether that profile blocks the player pawn is
not yet proven; the proof run below settles it. The client's hazard zones also stay active.

Every call passes `ShowUI = false`, so the progress-bar widget (Toggle UI, AddToViewport) is never created on any
map, and the `Update Progress Bar` calls (C4M1 @17948-18040, C5M1 @26150) have no visible effect. No bar relay is
needed; card 604's open "horde zone bar" row closes with this.

## Fix (class)

One relay, host to client, for both events on every BP_HordeZone:

- Host: after its own `Begin KillEmAll` or `End KillEmAll` runs (detour_process_event on those two UFunctions of
  BP_HordeZone_C), send a priority row: the zone's placed level key, one byte for begin or end, one byte for ShowUI.
- Client: skip its own level script's calls to those two events (one source of truth, so a switch-sourced begin does
  not play twice), and on a relayed row run its copy's event once with the detour bypassed.
- Join and in-place reload: a zone whose host copy has Active = true when the client finishes loading gets one begin
  row (the same rule the door and hide state use).
- Log one line per send and per receive: `card606: host|client t=<ms> <zone> begin|end showui=<0|1>`.

## Proof (one run, folded into the next build's run)

- Predictions written before the run: the card 098 BP_HordeZone_C collision tally is equal on host and client after
  the end (213 and 213 on C4M2); the client log has a card 604 'S' row for HordeAudio at the end.
- C5M1, C4M1 or C4M2: drive to the horde fight, finish it. The client log has the relayed `end` row within 100 ms of the
  host's, and no client-originated begin or end runs (skip line).
- After the end, the client walks through the former wall line with no position correction burst near the wall, and a client screenshot at the wall shows no fog.
- c448 gate: 0 mismatches.

## b462 (dwmapi b01ad3ed4e0b535b, ubwperf 370519ecb6d260ab)

Code: `mod/src/ubw/horde_c606.cpp`. The gate sits in `detour_c507_local_script` (both sides): the host sends
ChanOp::HordeZone `<level key> B|E <ShowUI>` to every ready client before its own event body runs; the client refuses
its own `Begin KillEmAll` / `End KillEmAll` and runs the host's row through ProcessEvent. Join replay: sync registry
"horde", one begin row per zone with Active = true. The class and both functions resolve from the first loaded zone
(5 s scan), and are dropped at map end.

Predictions for c606b462a (`tools/scenarios/c606-c4m2-b462.txt`, FED_C4M2 plaza horde), written before the run:

1. Host `card606: host ... BP_HordeZone_C_1 begin showui=0 sent to 1 of 1 clients` at the Plaza_SO2a switch and
   `end ... sent to 1 of 1` at Plaza_SO2b. The client runs each within 100 ms (`own copy's event ran`). The client
   refuses its own level script's begin once and has no own end.
2. The card 098 BP_HordeZone_C collision tally is equal on host and client after the end (213 and 213).
3. The client log has a card 604 'S' row for HordeAudio, with no card 604 miss for it.
4. A client walk through the former wall line, both ways (to the plaza entrance C_7, then to C_54), arrives both
   times. The host log has no `srv: correction` burst (3 or more within 1 s) in the walk window.
5. A client screenshot at the zone after the end shows no fog wall. 0 EXCEPTION; digest mismatches 0.

### b462 results (c606b462a, c606b462b; FED_C4M2)

c606b462a ran the relay correctly but missed two proofs through scenario faults. The host log names the zone
`BP_HordeZone_C` (object_name), not `BP_HordeZone_C_1`, so line 194 timed out. The scenario also had no
`client objectives` step, so the client never printed its card 098 tally. Both are fixed in
`tools/scenarios/c606-c4m2-b462.txt`. c606b462b passed (drive rc 0, host rc 0, client rc 0, no crash dumps).

1. Held. The relayed begin ran on the client 54 ms after the host sent it (b: host 221562356, client 221562409),
   and the client refused its own begin once. The end ran 42 ms after (host 221639187, client 221639229). The client
   made no own end call. In run a the gaps were 58 ms and 43 ms.
2. Held. The card 098 BP_HordeZone_C collision tally is n=213 on the host and n=213 on the client (card 137 client
   change row: 213, local sim, play). In c502b277a the client had 142.
3. Not as written. The host sends no card 604 'S' row for HordeAudio: card 604 relays the play
   (`P BP_HordeZone_C_1@7eb20b13 HordeAudio 100 0.0`, received by the client) but not the stop. The stop relay
   (detour_c604_stop, audio_c604.cpp:451) skipped it because c604_active(comp) was false when it stopped; the host logs
   hold 67 other 'S' rows sent. HordeAudio.Stop() is at
   bytecode 1280 of the End KillEmAll body (build/kis/hordezone.txt). That body now runs on the client from the host
   row, so the client stops its own HordeAudio, and the 'S' relay is not needed for this zone. KillEmAllEndSFX
   (PlaySound2D, 1327), HordeSystem.Deactivate and ToggleHazardZones(false) (Kill Em All Passive Setup, 220 and 271)
   run in the same body. No gap remains and no card is needed (senior, 2026-11-22).

Accepted by the senior 2026-11-22; b462 deployed to lane 1 (game/), hashes verified.
4. Held. Both client gotos through the former wall line arrived (9 s and 14 s). The host had 2 single corrections in
   the walk window, at 1.9 uu and 2.0 uu error, which is not a burst. Every other correction came from the start
   warps, before the begin.
5. Held. The shots `client-c606b462a-c606-zone-after-end.png`, `-walk-entrance.png` and `-walk-c54.png` (lane 2
   Saved/Screenshots/Windows) show no fog wall. 0 EXCEPTION; card 132 rounds had 0 mismatches.
