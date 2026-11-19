# Card 286: the client's own grenade throw shows two grenades

Playtest 2026-11-13 (user is the client): "the grenades throw double grenades on client".

## Trace (static, live client log `UBWCoop-menu-20261113-183002.log`)

- Line 7509, t=255346319: `playlog: client grenade spawn ADProjectile by null at (5204, 22630, 163) velocity
  (2635, -760, -1216)`. This row is written by `detour_c74_spawned` (`grenades.cpp:872`), so the client's own
  ability ran `OnGrenadeSpawned`: the client launched its own projectile.
- Line 7515, t=255346368 (49 ms later): `card074: client GrenadeSpawn 2 ... -> copy ADProjectile`, the host's relay
  copy of the same throw, at (5201.1, 22627.4, 161.9) with the same velocity.
- Line 7647: `round 10 launches: BP_ProjectileInitData_GrenadeFrag_C:2/2 BP_ProjectileInitData_GrenadeIncendiary_C:1/1`
  (own/copy). Every client throw is one own launch plus one copy. Same for the incendiary (lines 7565, 7571).

Card 074 step 1 found that the client's own throw never reaches `FireProjectile`, so `c74_relay` sends each throw to
every client, the thrower included (`grenades.cpp:541-548`). That finding no longer holds: since the client's ability
spawns its held grenade (cards 158/169), its graph runs on to the launch. The own projectile also gets the damage
lambdas `OnGrenadeSpawned` binds, so it can explode and deal damage on its own, apart from the host's explosion frame.

## Fix

1. Client: the own grenade launch is not kept. In `detour_c74_activate`, after the original, on a client session,
   when the task's projectile is the one `OnGrenadeSpawned` just marked (`g_c74_grenade`), release it to the pool
   (`g_orig_c74_proj[2]`, the card 157 pattern) and clear the mark. The host's copy (one grenade, the host's
   place, ended by the host's `GrenadeExplode`) is the only one shown. The ability must still end as it does now
   (montage, held grenade destroy, count from the host). Check that a released projectile cannot still fire its
   bound damage lambdas or explosion; if release is not enough, say so in the card before coding around it.
2. The client's own grenade launch is not counted as a shot or an own launch: no `playlog: shot c` row (line 7511
   shows one with the Morita as weapon) and no `own` count in the card 132 launch tally for it.
3. Row: `card286: client t=<ms> own <init data class> launch released (the host's copy is shown)`, capped.
4. Sibling sweep, in the card as a table: every client-own ability or input that can now launch or spawn locally
   what the host also relays to the thrower: frag, incendiary (and its `TrySpawnDamageZone` fire zone), flare
   (card 174 blocks `TryThrowFlare`: confirm), mech weapons and turrets (card 076 relay with `skip_pc`), any other
   `OnGrenadeSpawned` or pooled launch on the client's own pawn. For each: who launches, who is skipped, verdict.

## Predictions (one run, client throws one frag and one incendiary)

- Per throw: one `card286` release row, one `card074: client GrenadeSpawn` copy row, one `GrenadeExplode`.
- Launch tally: grenade classes `0/<n>` (own/copy) on the client.
- Receiver-side shot from the client mid-flight: one grenade.

## Release check (static, before coding)

- `AADProjectile::ReleaseToPool` (exe+0x4A361E0) empties the dynamic hit delegate +0x2B0 and clears the native one
  +0x2C0, then the three other pairs +0x2D8/+0x2E8, +0x300/+0x310 and +0x328/+0x338. It zeroes +0x388, calls
  `SetActorEnableCollision(false)`, then vcall +0x368 (hidden) and vcall +0x410 on the movement component +0x370,
  and returns the actor to its pool.
- `OnGrenadeSpawned` (exe+0x5163A70) binds its lambdas to +0x2C0 (`lea r14,[r15+0x2c0]`) and +0x338
  (`lea rcx,[r15+0x338]`). Both are cleared by the release, so the damage and fire-zone lambdas cannot fire.
- Not enough on its own: the release does not clear the actor's life span timer, and a grenade's explosion is its
  `LifeSpanExpired`. So the code sets the life span to 0 first. `AActor::SetLifeSpan` (exe+0x37F3920) stores the value,
  and with Role +0x168 == Authority (3) or torn off, a value of 0 or less takes the clear-timer branch. The client
  spawned this actor itself, so it has authority over it.

## As coded (junior, uncompiled)

1. `grenades.cpp` `c286_client_release(task)`, called in `detour_c74_activate` right after the original. On a client
   session, when the task's projectile is `g_c74_grenade` (marked by `OnGrenadeSpawned` inside that Activate), the
   function clears the mark, takes the shot and the own count back, calls `c157_forget`, then
   `g_set_life_span(proj, 0)` and `g_orig_c74_proj[2](proj)` (ReleaseToPool, the card 157 pattern). The ability is
   untouched: its montage, held grenade and the host's count run as before.
2. `playlog.cpp` `pl_shot_undo()`: the own pawn's burst opened in this tick (`seq != 0`) loses one launch, and closes
   unflushed when it was the only launch, so no `playlog: shot c` row is written. `ShotOwn` drops by one. The shot
   number stays used (the gap is harmless: its round was forgotten). `proj_c132.cpp` `c132_proj_unown(init data)`:
   the class's own tally drops by one; the launch number (`launches`) is unchanged, because it numbers the lines.
3. Row: `card286: client t=<ms> own <init data class> launch released (the host's copy is shown); shot row
   <taken back|not open>, own count <taken back|not found>, life span <cleared|not cleared (no SetLifeSpan)>, <n> so
   far`, WARN when any part failed, cap 16.

## Sibling sweep

| What the client's own pawn does | Who launches on the client | Host relay to the thrower | Verdict |
|---|---|---|---|
| Frag throw (`OnGrenadeSpawned`) | own launch, released here | GrenadeSpawn copy (c74_relay, every client) | one grenade: fixed here |
| Incendiary throw, its `TrySpawnDamageZone` fire | own launch, released here (zone lambda bound to it, cleared) | GrenadeSpawn copy; the zone comes in the host's GrenadeExplode frame | one grenade, one fire: fixed here |
| Any other `USTGrenadeThrowGameplayAbility` throw | same `OnGrenadeSpawned` mark | same c74_relay | covered by the same release (it keys on the mark, not the class) |
| Flare (`TryThrowFlare`) | none: card 174 returns before the original on the client (`vehicles.cpp:880`) | the host's server copy throws; card 104 copy | one flare: no change |
| Player weapon rounds | own launch (predicted, kept for card 157 hit claims) | card 076 relay with `skip_pc` = the shooter's PC (`c76_mark_task`, card 132 step 2b) | one round: no change |
| Mech weapons, mounted turrets | own launch (predicted) | card 076 relay, the pilot's or gunner's PC skipped | one round: no change |
| Satchel charge | own OnInteraction shows the charge; the local explosion on a building is blocked (`detour_c98_satchel`) | host 'S' world row | one explosion: no change |

## User detail after the playtest (2026-11-13)

"Grenades show up in hand fine, but when I throw a grenade I see two sprites. Flares aren't the same." The held
grenade is correct (the earlier "grenade doesn't show in client hand" report is withdrawn). The double is in
flight, as traced above: own launch plus the host's copy. The flare remark is read by the senior as "the flare
does not double" (card 174 blocks the client's own `TryThrowFlare`), which fits the trace; asked back to the
user to confirm.
Confirmed by the user: flares do not double (one sprite).

## Build b7 change (senior review of b6)

`detour_c74_activate` now calls `c286_client_release(task)` last, after `c74_relay(task)` and
`c132_early_hit_send(task)`. On the client, neither of the two read the projectile anyway:

- `c74_relay` sets `grenade` only on the host (`!g_client_session`). The mech branch is host-only too, so on the
  client it returns after reading the task's two pointers. The projectile is never dereferenced.
- `c132_early_hit_send` uses `g_c132_early`, which is set only in `detour_c132_on_hit` under `!g_client_session`.
  On the client it is always empty, so the function compares a pointer and returns.

The new order removes the dependency anyway. The only later reader is the `UBWCOOP_C74_LOG` diagnostic block, which
reads the velocity and class of the pooled actor. A pooled projectile is still a live object, so that read is safe.

## Closed (2026-11-17 11:43)

b182 `d3a6c76b270110c3`, run c423b182a: one client frag throw gives one `card286 ... launch released` row, one host `card074 ... GrenadeSpawn #1 ... sent to 1 of 1 clients`, one client `GrenadeSpawn 1 ... -> copy ADProjectile`, and the client's digest launch tally `BP_ProjectileInitData_GrenadeFrag_C:0/1` (own 0, copy 1): exactly one grenade actor on the client. The mid-flight shot shows no grenade (thrown behind the carcass in frame), so the count rests on the rows. 25 release rows in 21 logs since 2026-11-16, 0 `WARN card286`. Reopen on a launch tally with own above 0 for a grenade class, or a user report of two grenades.
