# 314 The host hears the client's weapon fire "like my own gun": the fire fallback attenuation fails once and stays off

Playtest 2026-11-13, user answer 2026-11-14: the other player's Laser Morita fire sounded "like my own gun", full
level at any distance. User rule: a player hears another player's sounds from that player's position, fading with
distance, never as their own. "Just do an entire sweep."

Senior trace 2026-11-14, logs and code. Cause settled for the host side. The client side in the playtest build is
card 304 (fixed in b7, proven in the b11 soak).

## Symptom (host log `client/UBWCoop-menu-20261113-182510.log`, play build 4)

Every fire sound of the client's server copy from 81350012 on is an audio component play with no attenuation set:

| Sound | Owner | Rows | Row shape |
|---|---|---|---|
| `MSS_WPN_Shotgun_Fire` | `BP_Weapon_PumpShotgun_C` | 89 | `3D, attenuation null, volume 1.00` |
| `MS_WPN_EPulse88_Fire`, `MS_WPN_EPulse88_Energy_Loop` | `BP_Weapon_EPulse88_C` | 42, 30 | same |
| `MS_WPN_Underslung_Shotgun_Fire` | `BP_Weapon_MoritaMk1_C` | 17 | same |
| `MSS_WPN_MountableTurret_Fire` | `BP_Weapon_Turret_C` | 7 | same |
| `MSS_WPN_LaserTrainingRifle_Fire` | `BP_Weapon_LaserMorita_C` | 5 | same, 761 to 9526 uu from the host |
| `MS_WPN_NukeLauncher_Fire_Latch` | `BP_Weapon_NukeLauncher_C` | 4 | same |

With no override the sound's own `ATT_WPN_Player_Fire` applies, and that asset has `bSpatialize 0` (card 173 rows):
the host hears the client's gun as its own. The same log's non-fire sounds of the copy (`MSS_WPN_Shotgun_Pump`,
reloads, equips) carry `ATT_FLY_Trooper_Footsteps`: their fallback loaded.

## Cause

`81350012 card162: host fallback attenuation /Game/Audio/Attenuation/ATT_WPN_MoritaMK1.ATT_WPN_MoritaMK1 failed
(class found, asset found, game instance missing); its sounds stay unattenuated`, in the same millisecond as the
first `MS_WPN_Underslung_Shotgun_Fire` row.

- `c162_fallback(context, sound)` (`fx_c136.cpp:844`) takes the game instance from `context` to hold the asset.
- `SpawnSoundAttached` has no world context: `c136_from_attached` (`proj_c132.cpp:304`) leaves `c.context` null, and
  `c162_call_atten` (`fx_c136.cpp:1013`) passes it on. `g_get_st_game_instance` is not called, `gi` is null.
- The failure sets `f.failed = true`. `g_c162` is initialised once (`fx_c136.cpp:796`) and never reset, so the fire
  fallback is off for the rest of the process.
- Whether it works is decided by the first remote "Fire" sound of the session: at-location (`MoritaMK1Fire_MS`, has a
  context) loads the asset for good; attached (underslung, laser, shotgun) kills it for good. That is why the soaks
  pass (Morita first) and the playtest failed.
- 18 host logs carry the row (11 for `ATT_FLY_Trooper_Footsteps`, 7 for `ATT_WPN_MoritaMK1`); no client log does: the
  client passes `g_client.pc` as the context (`fx_c136_net.cpp:1399`).

## Fix

1. `c162_fallback`: the game instance comes from something that always has a world (the attach component for an
   attached call, else the local player controller), not only the call's context. A missing game instance never
   latches; only a missing class or asset does. The failure row is a `WARN`.
2. Class rule at the one point every component sound passes, `detour_pl_play_internal` (`playlog.cpp:822`), both
   sides: when the owner is another machine's player (`c304_other_player`: pawn or weapon, by class and role, not
   the playlog list) and `GetAttenuationSettingsToApply` returns settings with `bSpatialize 0`, the component gets the
   card 162 fallback before the original runs. A play with no attenuation at all stays card 176 (muted: the
   player's own UI sounds). This covers components the three GameplayStatics detours never see (a weapon's own
   component, the `WithAIAwareness` spawners).
3. Rows: `card314: <side> remote player sound <sound> from <owner>: applied <ATT|none> bSpatialize <n>, plays with
   <ATT|its own|muted>` (once per sound per map); `WARN card314: ... plays unspatialized` when neither the fallback
   nor the mute applied; map tally `card314: <side> remote player component sounds <n>, re-attenuated <n>, muted
   <n>, unspatialized <n>` (target 0).

## Sibling sweep (report before coding)

| Item | Check |
|---|---|
| Every "resolved once, failed forever" latch whose failure depends on a call argument | `c162_fallback` is the known one; list the others (`g_c110.failed`, `g_c132d.failed`, `g_c176.resolve_failed`) with what can make each fail |
| Every `g_get_st_game_instance(context)` caller that can get a null or worldless context | `decal_c132.cpp:666`, `death.cpp:678`, `hooks_ubw.cpp:1340`: what each does on null |
| Attached calls with `c.context` null in other card 136 uses | anything else that needs a world from `c.context` |
| Client side: a remote pawn's component plays | no client log has one (`pl_sound` uses the 3 s list); the new tally uses class and role and will show them |
| `MSS_FOL_Low_Ammo_Click` (148 rows, `2D, attenuation null`) and `MS_PLAYERBARK_Breathing` of the copy | muted by card 176 every time (tally `muted`) |
| Vehicle and turret weapons with the client as pilot (`BP_Weapon_Turret_C`, `BP_Weapon_AutoCannon_C`, mech) | same rule, owner chain reaches the pilot |

## Open, not part of this fix

A remote player's rifle plays the first-person asset with `ATT_WPN_MoritaMK1` (FalloffDistance 30000 uu), while the
game's own third-person variants are shorter (`MSS_WPN_AI_LaserTrainingRifle_Fire` uses `ATT_EMIT_Occluded_75m`). The
game itself plays `MS_WPN_AI_Morita_Mk1` for the copy's Morita. Whether remote fire should use the AI variant and
its attenuation is for the user to hear after this fix.

## Predictions (one run, C1M1, batched)

The client's first shot of the session is an attached fire sound (Laser Morita, the C1M1 start weapon), host 9000 uu
away; then one underslung shot and one shotgun shot if the route gives them.

- Host: no `fallback attenuation ... failed` row.
- Host: `card173: host ... remote sound MSS_WPN_LaserTrainingRifle_Fire ... own attenuation ATT_WPN_Player_Fire
  bSpatialize 0, plays with ATT_WPN_MoritaMK1`, and the `playlog: host ... audio MSS_WPN_LaserTrainingRifle_Fire`
  row reads `attenuation ATT_WPN_MoritaMK1`, not null.
- Both sides: map tally `unspatialized 0`, no `WARN card314`.
- Host fires the same weapons, client 9000 uu away: client `audio row` lines carry `ATT_WPN_MoritaMK1`; client tally
  `unspatialized 0`.

## Done when

The predictions hold in one run, the sibling list is in the report, and the user hears it in the next playtest.

## Proof in b223 `5dd69a9bb162d8ef` (senior, 2026-11-18 12:29)
The fix is `fx_c136.cpp` `c162_fallback`: a missing game instance no longer latches `failed`. c245b223a and c448b223a: 0 `fallback attenuation ... failed` rows and 0 `WARN card314` on both sides. Remote fire on both sides: `own attenuation ATT_WPN_Player_Fire bSpatialize 0, plays with ATT_WPN_MoritaMK1` (host 2 rows, client 3; for example host 664667667 MS_WPN_VeteranRifle_Fire, client 664670504 MoritaMK1Fire_MS). The predictions hold. Only the hearing check is left, and it waits on the playtest: remote fire sounds spatial at range, and the user decides whether to switch to the AI variant's attenuation.
