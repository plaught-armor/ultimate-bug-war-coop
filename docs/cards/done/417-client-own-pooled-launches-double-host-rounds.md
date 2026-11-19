# 417 — Client launches its own mortar, AI and turret rounds on top of the host's copies

Found by the card 408 class sweep (placed, non-replicated Blueprints that use random functions). Static trace plus
existing logs; no run made.

## Evidence

Card 132 relays every host pooled launch made through `USTCombatUtilityLibrary::SpawnPooledProjectile`
(return `exe+0x52502AC`) as a round copy. Its comment (`grenades.cpp:872`) says the client never runs that path. The
client does:

| Run (client log) | Client own launches through `exe+0x52502AC` | Host copies received |
|---|---|---|
| `c416b112a` | MortarZone 16, AIMoritaRound 5, CannonTurretRound 2 | MortarZone 9, AIMoritaRound 38 |
| `c414b111a` | AIMoritaRound 15, MortarZone 13, CannonTurretRound 2 | MortarZone 12 |
| `c234-c3m2-r2` | AIMoritaRound 41, MortarZone 34, AATurretHomingRound 2 | (copies present) |

So the client sees the host's rounds plus its own, at other places and times. No other class launches on the client
through this return address in those logs.

`BP_MortarZone_C` (disassembly: scratchpad `kis-BP_MortarZone.txt`): `ReceiveBeginPlay` starts a loop of
`Delay(RandomFloatInRange(DelayBetweenMortars))` then `Drop Mortar`; when `Active`, `GetMortarLocation` picks a random
point in `TriggerArea` (up to 16 tries) at least `SafeDistance` from `GetPlayerCharacter(0)`, spawns the pooled round
and binds `Mortar Impact` (400 damage, radius 1000, cue `GameplayCue.Impacts.MortarZoneMortar`). It runs on both
sides with separate random numbers, and on each side "the player" is that side's own player only.

## Fix (class: client-own launches through the Blueprint pooled-spawn path)

1. Client, `detour_c74_spawn`: a launch that is not a relay copy (`!copying`) and whose return address is
   `exe+0x52502AC` is queued (weak reference, fixed ring of 32, WARN on overflow) and released to the pool on the next
   frame step, the card 286 way (`SetLifeSpan(0)`, `ReleaseToPool`, `c132_proj_unown`, `c157_forget`). Next frame, not
   at once: the Blueprint caller binds `BP_OnProjectileImpact` and sets velocity on the returned round after the call,
   and `ReleaseToPool` must run after that to clear the binding. The round is hidden at once so the one frame does
   not draw.
2. Log `card417: client t=<ms> own <init data> launch released (context <class>), <n> so far`, capped 16 lines, and one
   count per init data class in the card 132 round line (own stays 0 after the release).
3. Host, mortar only: `GetMortarLocation` keeps clear of the host's player alone. After the host's launch with context
   `BP_MortarZone_C`, if the launch XY is within the zone's `SafeDistance` of any remote player's pawn, the host
   releases the round and does not relay it (`card417: host mortar at (x, y) released: <d> uu from remote player`).
   The Blueprint drops another after its next delay.
4. `grenades.cpp:872` comment corrected.

## Not changed

`BP_RandomEmitterVolume_C`: ambient sound at a random point placed relative to the local listener, per player by
design. `BP_TacSupp_OrbitalStrike_ScriptedLauncher_C`: `TriggerMissiles` spawns `BP_TacSupp_OrbitalStrikeProjectile`
at a random offset through a Blueprint actor spawn; the 104/105 relay covers it if that class is in
`c105_relayed_class` (check on the next map that has it; card 123 lists it as covered).

## Predictions

- Client log: every `card132: client ... launch ... own ...` row with caller `exe+0x52502AC` is followed within one
  frame by a `card417` release row (while under the cap); the client's round lines show own 0 for MortarZone,
  AIMoritaRound and the turret rounds.
- Client `MortarZoneMortar` cue rows equal the host's for the same span (no client-only impacts).
- Host: 0 mortar launches relayed within `SafeDistance` of the client's pawn.
- No `Accessed None` flood, no crash; 0 mismatches; WARN classes unchanged.
- Receiver-side screenshot of a mortar zone on the client with one impact where the host shows one.

## Done when

One run on a map with a mortar zone and AI troopers (the fast chain already has both) shows the predictions.
