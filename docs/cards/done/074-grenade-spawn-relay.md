# Card 074: Show the host's grenades on the client

Status: sent 2026-11-05 05:58 to junior ultimate-bug-war-coop-bb, after card 056 was accepted.

Read `docs/STATE.md` ("Grenades: fixed by card 073"), the card 073 Report ("Open") and Senior review, and the card 039 Report for the `ChanOp::Gib` relay shape. Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

Card 073 found that a grenade is thrown only on the host. The client's press activates the server copy, and the copy runs `FireProjectile`. The client's own local activation never reaches `FireProjectile`. The projectile is a pooled `ADProjectile` with RemoteRole 0, so it does not replicate. The client sees no grenade and no explosion, but it does take the burn damage. The host's own grenades probably have the same problem. They also come from a pooled `ADProjectile`, so the client does not see the host player's grenades either. Check this first.

## Design

The host stays the only side that deals damage. The client gets a cosmetic copy of each grenade.

1. **Check first, log only** (behind `UBWCOOP_C74_LOG=1`):
   - On the client, log whether the host player's grenade shows up at all (a pooled projectile spawn, or nothing).
   - Log why the client's local grenade activation stops before `FireProjectile`: which task or notify it waits for, and where it ends. The existing card 020 FireProjectile log (`kC20FireWhat`) already separates the four sides; use it.
2. **Relay:**
   - On the host, after `FireProjectile` for a grenade ability (`USTGrenadeThrowGameplayAbility` or a subclass), on the host's own pawn or a server copy, send every client one frame on a new `ChanOp`. The frame holds the projectile class, the spawn transform and the initial velocity. A binary frame is fine; see the netcode trust model in the memory and STATE.
   - Skip the frame for the client the grenade belongs to only if that client already shows its own grenade. Step 1 says whether it does.
3. **Client spawn, cosmetic only.** Spawn the projectile through the game's own pooler (`UADProjectilePooler::SpawnProjectile`, as the card 039 gore path does) or the client's own `FireProjectile` path, whichever step 1 shows is simpler. The client copy must deal no damage on the client:
   - The host's burn and blast already reach the client's pawn through replicated health.
   - A second hit from the cosmetic copy would double the client's damage.
   - Find the damage step (the explosion's gameplay effect or the damage volume, `USTGrenadeDamageVolumeComponent`) and block it for relayed projectiles only.
   - Prove the double damage is absent: compare the client's Health drop for one incendiary with card 073 run c73i (100 to 96.5).
4. **Respawn** (card 073 limit): the client dies holding a grenade and respawns. Log the client's count before and after, against the host's copy. If they differ, send the absolute `GrenadeItem` counts again at the respawn, in the same place card 073 step 0 holds the AmmoSet seed.
5. The fix runs in every co-op session. Only the step 1 log is behind the flag.

## Do

1. Implement the above.
2. Prove it with a new scenario:
   - The client throws a frag and an incendiary.
   - The host throws one.
   - Take shots on both sides at the throw and at the explosion. Each grenade must show on both screens.
3. Run the client-death respawn route with one grenade held.
4. Run solo (`SOLO=1`). No frames are sent.

## Done when

- The shots show each grenade and its explosion on both screens.
- The client's damage from one incendiary matches c73i, with no double damage.
- The respawn result is in the Report: the count is kept, or fixed.
- `logsum` gives ERROR/FATAL 0, both games end with rc 0, and there is no new dump.
- With `UBWCOOP_C74_LOG` unset, the step 1 log detours are not installed.
- fnlen reports no function over 60 lines.

## Report

Done. The host relays each grenade spawn and explosion to every client. The client plays cosmetic copies only, so it takes no extra damage. All checks pass on the final build (c74u, log off).

**What runs**

- **Always, in any co-op session:**
  - Host relay detours: FireProjectile/OnGrenadeSpawned (the spawn frame), LifeSpanExpired (the explosion frame) and TrySpawnDamageZone.
  - TrySpawnDamageZone catches the incendiary's fire zone, BP_HazardousZone_IncendiaryGrenade_C.
  - STSpawnSystemAtLocation and PlaySoundAtLocation catch the first system and sound while an explosion is in progress.
- **Only with UBWCOOP_C74_LOG=1:** the log detours.
- **Explosion frame:** carries the system, sound and zone class paths plus the zone's position, yaw and life span.
- **Client side:**
  - Spawns the projectile copy through the pooler, with no damage. OnGrenadeSpawned binds damage, and the copy never gets that call.
  - Plays the FX and sound at the host's point.
  - Spawns the zone copy with collision off (g_orig_set_actor_collision false) and SetLifeSpan. The host's zone GetLifeSpan returns 0, so the copy uses the 10 s fallback, kC74ZoneLifeFallback.
- **Symbols:** AActor_SetLifeSpan is a new call in tools/symbols.txt, which also has its card 074 comments updated.

**Runs**

| Run | What | Result |
|---|---|---|
| c74n | c74-grenades, log on | All throws (client frag, client incendiary, host frag) and explosions on both screens; incendiary fire zone copy on the client. rc 0/0, WARN 0, no dumps |
| c74o | c73-client-grenade, log off | Client Health 100 to 96.5 from one incendiary, the same as c73i. No double damage. rc 0/0, WARN 0 |
| c74p | respawn, first scenario | Discarded: its second "own pawn on foot" match was the restart after game over, not a respawn (game over resets grenades to 0 on both sides) |
| c74q | c74-respawn | Respawn result below. rc 0/0, WARN 0, no dumps |
| c74r | c74-grenades, log off (build before the null-asset fix) | rc 0/0, WARN 0, all frames and effects |
| c74t | c74-solo, SOLO=1 | GrenadeSpawn "sent to 0 of 0 clients", no GrenadeExplode frame. Host explosions and fire show. WARN 1 is the usual "autodropship: 1 PlayerController(s)" line. rc 0 (c74s failed only because the solo scenario was run without AUTODROPSHIP) |
| c74u | c74-grenades, log off, final build (DEPLOY=1) | Config line "UBWCOOP_C74_LOG unset -- card 074 grenade spawn log off". 3 GrenadeExplode frames, with FX and the incendiary damage zone on the client. logsum: ERROR/FATAL 0, WARN 0 on both; rc 0/0; no new crash dumps. Shots: ~/.cache/claude-tmp/claude-1000/-mnt-based-backup-Repos-ultimate-bug-war-coop/50a11dc8-a0b2-4c6f-af73-8cd1b2a1b3e9/scratchpad/c74u.png |

**Respawn (c74q).** Setup: UBWCOOP_C43_TEST=1 with the new UBWCOOP_C43_START_MS=60000. The client picks up the frag/incendiary set, then the card 043 test downs it, bleeds it out and revives it from Out with the same pawn.

- **Before the down:** the client and the host's server copy both log items 2 [1,1].
- **Down and revive:** Downed at t=34572031, Out at 34601547, Out -> Up at 34607552.
- **After the revive:** the 1 s count line does not change on either side, so the counts stay 1/1 and the HUD still shows x1 x1.
- **Result:** the counts match, so the absolute-count resend at respawn is not needed and was not added.

**Other changes**

- **UBWCOOP_C43_START_MS (config.cpp):** sets when the card 043 test starts. Default 20000, valid range 20000–600000. It gives the scenario time to pick up grenades first.
- **Card 073 1 s line:** on the host, it now also logs pc 1's pawn ("host server copy 1s ...").
- **Null asset paths:** c74_asset_path returns "-" for a null asset without logging. A non-null asset whose path does not fit logs a WARN.
- **fnlen:** 1381 functions, 0 over 60.

**Gaps**

- The client often does not see the host's grenade in flight (host-flight shot): the spawn arrives late or the copy is hard to see at that range. The explosions and fire do show.
- The zone copy lasts the 10 s fallback, not the host's real zone life.
- A grenade that ends without LifeSpanExpired (for example, removed with the level) leaves a stale g_c74_live entry until the map reset.
- The first system and sound spawned during LifeSpanExpired count as the explosion's FX. An unrelated FX spawned in that same call would be relayed instead.

## Senior review (2026-11-05 07:55): accepted

- Checked c74n, c74o, c74q, c74t and c74u myself. All five drives end with `scenario passed`. logsum gives ERROR/FATAL 0 and WARN 0 on both games, except c74t, whose one WARN is the known solo `autodropship` line. The newest crash dump is still the 05:22 one from c73g, so these runs left no new dump. fnlen: 1381 functions, 0 over 60 lines.
- c74u started after the DLL deploy at 07:41:41. Its client log shows the `UBWCOOP_C74_LOG unset` config line and 3 GrenadeSpawn and 3 GrenadeExplode frames. Each explosion spawns its system and plays its sound. The incendiary's zone copy spawns with collision off and a 10 s life. The incendiary FX gap from c74l is fixed: `NS_FX_Incendiary -> spawned`.
- The log detours are installed only through `c74_install`, which runs only when `g_c74_log` is set (hooks.cpp:26777). The relay detours log only behind `g_c74_log`.
- c74u.png shows the frag smoke, the incendiary fire and the host frag on both screens.
- The damage check is accepted: in c74o the client's Health drops from 100 to 96.5, the same as c73i.
- The respawn result is accepted with a limit: c74q covers a down, Out and revive with the same pawn, not a new pawn after death. A new pawn is the card 073 step 0 path, and the GrenadeItem relay sends absolute counts, so it should resync.
- Follow-up, no fix needed now: in c74o the incendiary copy ended 682 uu from the host's point. The client's copy simulates its own flight, so a wall or a slope can make it land far from the host's grenade. The FX and the zone play at the host's position, so only the projectile copy itself is off. If this shows in play, snap the copy to the host's point on GrenadeExplode.
