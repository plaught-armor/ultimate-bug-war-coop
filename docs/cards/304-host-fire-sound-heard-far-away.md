# 304 The client hears the host's pulse rifle as if it were close, wherever the host is

Playtest 2026-11-13 (user plays as client, now on FED_C3M1): "can hear pulse rifle when host is firing also".

## Trace (client log `UBWCoop-menu-20261113-184603.log`)

14 `audio row EPulse44Fire_MS` rows from the host's fire:

| Lines | Source in the row | Distance to listener | Nearest remote pawn | Attenuation used |
|---|---|---|---|---|
| 89289, 89610, 91734, 92187 | `BP_PlayerCharacterMI_C (attached)` | 15124, 12168, 264, 148 uu | 58, 56, 61, 15 uu | override `ATT_WPN_MoritaMK1` |
| 89426 | `BP_PlayerCharacterMI_C (attached)` | 14936 uu | -1 (none) | override null, sound's own `ATT_WPN_Player_Fire` |
| 89396, 98406, 98630, 101229, 101257, 101387, 101684, 101708, 101774 | `a world row (attached)` | 15244 uu, then 49593 to 52259 uu | -1 (none) | override null, sound's own `ATT_WPN_Player_Fire` |

Own pawn at (-61946, -49703, 450) at 258495899 while the rows come from (-75916, 599, 1223): about 520 m apart.

When the row finds the host's remote pawn on the client, the sound is attached to it and gets the third-person
attenuation. When it finds none (10 of 14), the sound plays at a world position with the sound's own
`ATT_WPN_Player_Fire`, the first-person asset, so the client hears the host's rifle at near full level from 500 m.

The whole log has 573 rows of this shape (`from a world row (attached)`, nearest remote pawn -1) across other
sounds: `MS_BARK_WarriorBug_Pain` 96, `MS_WPN_AI_Morita_Mk1` 93, `MS_EMIT_CannonTurret_Fire` 34,
`MS_WPN_AI_Mech_Razorback_Autocannon_Fire` 26, `MSS_WPN_MountableTurret_Fire` 12 and more: an attached sound
whose owner the client could not resolve.

## Cause (settled, senior trace 2026-11-13)

The receiver decides "this is a remote player's sound" from a diagnostic list that forgets the player.

- `client_play_sound` (`fx_c136_net.cpp:1389`) takes its attenuation from `c162_remote_atten`
  (`fx_c136.cpp:908`). That function returns the row's own override unchanged (null here) unless
  `pl_remote_source` (`playlog.cpp:662`) says the sound is a remote player's.
- `pl_remote_source` reads `g_pl.remote`, the playlog's list of remote pawns. An entry is added only when card
  028 sees that pawn and is dropped after `kPlRemoteMs` = 3000 ms without a sighting (`playlog.cpp:51`, `:236`).
  With the host far away the list is empty: the row's owner test fails (`pl_remote(owner)` walks the same list)
  and the nearest-pawn test fails (it also needs a listed pawn within `kC162NearUu` = 300 uu).
- The sound then plays with its own `ATT_WPN_Player_Fire`, the first-person asset. With the list filled, the
  same sound gets `ATT_WPN_MoritaMK1` (FalloffDistance 30000 uu, read from the log), which is what the game uses
  for a trooper's rifle heard from outside.

The log shows both branches for one sound: line 89289 (list filled, 58 uu from the remote pawn) gets
`ATT_WPN_MoritaMK1`; line 89426 has the owner resolved as `BP_PlayerCharacterMI_C` but `-1 uu from the nearest
remote pawn` (list empty) and gets no override. So the pawn does resolve; the list is the fault, not relevancy.
The same list decides on the host for the client's sounds.

The 573 other rows with `-1` (bug barks, AI Morita, turrets) are not this fault: those sounds carry their own
third-person attenuation, and `-1` only says the list was empty.

## Fix

The attenuation is decided by what the sound is, on the sender, not by whether the receiver finds a pawn: a
sound from the host's own pawn or its weapon travels with the third-person attenuation the receiver must use, so
the world-position fallback gets the same falloff as the attached branch. The host's pawn stays resolvable for
the client's audio rows at any distance (player pawns always relevant, or the row carries position and
attenuation and needs no pawn). Same in the other direction for the client's sounds on the host.

- Rows: the existing `audio row` line gains `owner <resolved|world fallback>`; `WARN card304: client audio row
  <sound> owner unresolved, played with <attenuation> at <uu> uu`; per-map tally `card304: audio rows <n>,
  owner unresolved <n>, first-person attenuation used <n>` (target 0).

## Sibling sweep (same class: an attached sound row whose owner is not resolved on the receiver)

| Item | Check |
|---|---|
| Every player weapon fire, reload, equip, melee and grenade sound from the host | third-person falloff on the client at 1, 50, 150, 500 m |
| Mech and turret weapon sounds with a player as pilot | same |
| The 573 unresolved attached rows of this log, by class (bug barks, AI Morita, cannon turret, AI mech) | each class: owner resolvable or correct falloff by row |
| Client's sounds on the host (card 302 is the 2D class; this is the far class) | same rule |
| Player pawn relevancy at distance | remote body, markers and revive still work at 500 m |
| Fx rows (muzzle flash, tracers) with the same owner lookup | same fallback check |

## Predictions (one run, after the playtest, batched with 302)

- Host fires the EPulse44 at 1, 50, 150 and 500 m from the client: every `audio row` has the third-person
  attenuation; tally `first-person attenuation used 0`; zero `WARN card304`; the client's audio capture (null
  sink monitor) level falls with distance and is silent at 500 m.
- Client fires, host listens: same.
- Digest round after: 0 mismatches.

## Build 7 (coded, compiled; diffs `b7-B-304.diff`, playlog part in `b7-F-296.diff`)

- Sender (`fx_c136_net.cpp`): a sound whose owner is the sender's own player pawn or its weapon carries
  `kC136FlagPlayerSource`.
- Receiver: a row with the flag, or an owner that is another machine's player pawn by class and role (a server copy
  on the host, a simulated proxy on the client), counts as a remote player's sound without the playlog remote list.
  The remote-player attenuation then applies, not the sound's own first-person one.
- Rows: `WARN card304: client audio row X owner unresolved, played with <attenuation> at N uu`; the map-end tally
  `card304: audio rows N, owner unresolved N, first-person attenuation used N`; the playlog audio row names the owner
  as `resolved` or `world fallback`.

## Senior verdict on b7b-2 (2026-11-13)

- Fire sound: passes. First map tally `audio rows 23, owner unresolved 21, first-person attenuation used 0`; every
  `MoritaMK1Fire_MS` row played with `ATT_WPN_MoritaMK1` at 1280 uu.
- Not passing: second map tally `audio rows 8, owner unresolved 6, first-person attenuation used 5`. The five WARN
  rows are all at 267279651-652, right after the client's reconnect: `GrenadeExplosionGeneric_MS` (160 uu),
  `MS_GORE_Overkill_Trooper` (106 uu), `MS_BARK_WarriorBug_Death` x3 (435, 435, 654 uu), each "played with its own
  attenuation". Two things to settle statically before any code:
  1. Why the host set `kC136FlagPlayerSource` on a bug death bark and an explosion (`c304_player_pawn` walks an
     owner chain; which hop reached a player pawn). The flag is meant for the sender's own pawn or weapon.
  2. Whether these sounds carry attenuation in the asset. If they do, "its own attenuation" is correct for them and
     the WARN is wrong for sounds that are not first-person; if not, the fallback has a gap.
- `owner unresolved` 21 of 23: the client cannot resolve the host's pawn or weapon as the row's key actor. With the
  flag the attenuation is right, but the sound is placed at a point and does not follow the pawn. Trace
  `client_key_actor` for a host weapon owner (static) and say what key the host sends.

## Junior trace for b8 (2026-11-13, static, b7b-2 logs)

1. Why the host set `kC136FlagPlayerSource` on the 5 rows at 267279651-652. The flag is set at
   `fx_c136_net.cpp:1707-1709` when `c304_player_pawn(owner)` is the host's own pawn. `c304_player_pawn`
   (`fx_c136.cpp:922`) goes through `weapon_pawn` (`hooks_ubw.cpp:656`), which for any actor that is not a weapon
   returns its `Owner`. The host had thrown a frag: `card098: host destroy BP_GrenadeVisuals_Death_C` at 267279626,
   then three bugs died from 800 damage with the host's pawn as instigator. The grenade actor is owned by the host
   pawn, so every sound whose `c136_owner` is the grenade (explosion, and the sounds the blast causes through the
   context outer chain) is flagged as a player source. The flag is therefore set for any actor one Owner hop from
   the host pawn, not only for the pawn and its weapons. The exact owner of the bark and gore rows is not in the
   log (the row does not print the owner class); the grenade (context outer) is the likely one.
2. Whether those assets carry their own attenuation: yes, and it is spatialized. Client card173 rows:
   `GrenadeExplosionGeneric_MS` uses `ATT_Explosion_Small` (bSpatialize 1), `MS_GORE_Overkill_Trooper` uses
   `ATT_EMIT_Gore_Medium` (1), `MS_BARK_WarriorBug_Death` uses `ATT_BARK_EnemyMedium` (1). `c162_remote_atten`
   calls `c173_own_atten`, which returns null (play with its own) when the own attenuation is spatialized. So the
   client played them correctly attenuated, and `c304_row` counted them as "first-person attenuation used" and
   wrote the WARN only because `atten == nullptr`. The WARN and the tally are false for these rows.
3. Why `client_key_actor` does not resolve the owner (21 of 23 rows). The host writes the key from `c136_key`
   (`fx_c136.cpp:205`): an effect that is not on an actor (no attach, component, cue params or cue context: an
   at-location sound such as the explosion) gets the `world` key, so `guid` is 0 and `placed` is -1. An on-actor
   effect of a replicated owner gets `c41_guid(owner)` (the host package map's NetGUID); a placed one gets its wire
   name and location. `client_key_actor` returns null for a world key by design, and `c304_row` is passed
   `client_key_actor(r) != nullptr` as "owner resolved". So every at-location row counts as unresolved even though
   it never named an owner. The remaining guid-keyed rows fail when the owner (a projectile that exploded) is
   already gone on the client, or never got a NetGUID before it was destroyed (a guid of 0 falls to the world key).

Fix (proposed, not in b8 code):

- `c304_row`: count an own attenuation with bSpatialize 1 as correct; WARN only when the own attenuation is not
  spatialized and no fallback was used. Count "owner unresolved" only for rows whose key is `guid` or `placed`.
- Host flag: set `kC136FlagPlayerSource` only when the owner is the host pawn itself or a weapon (`kC50` weapon
  class) whose pawn is the host's, not for any actor one Owner hop away (a grenade, a placed turret).
- Row: add the owner class and the key kind to the host `card304` row, so the next run names the owner.

Proof (next run): the frag throw by the host with the client 30 m away gives zero `WARN card304` rows, and the
map tally shows `first-person attenuation used 0`.

## Senior review of the junior trace (2026-11-13)

Accepted: the five WARN rows are false, and the flag is too wide. Code in b9 as proposed (flag only for the host pawn
and its weapons; tally counts a spatialized own attenuation as correct and "unresolved" only for guid or placed keys;
row names owner class and key kind). Proof: host frag throw, client 30 m away, zero `WARN card304`.

## Senior review of b9 (2026-11-13, static): accepted

WARN only for a sound with no attenuation that is not spatialized by itself; `owner unresolved` counts guid and
placed keys only; the host row names owner and key kind (cap 16). Prediction: tally row with `owner unresolved 0`,
no `WARN card304`.

## Senior verdict, b10 last run (2026-11-13): rows clean, symptom not exercised

Client tally `audio rows 2, owner unresolved 0 ..., first-person attenuation used 0`; host rows x3; no WARN. Two
rows do not test the playtest symptom (host weapon fire heard far away). Open for the soak or the playtest.

## Measured proof scenario (senior, 2026-11-17 08:53, b177)

`tools/scenarios/c304-far-fire.txt` (FED_C3M1, run c304b177a, queued for the junior): the host fires its Morita beside
the client and then about 8600 uu away. Predictions: every far `playlog: client ... audio row EPulse44Fire_MS` row
reads about 8600 uu with a spatialized attenuation (`ATT_WPN_MoritaMK1`), never `override null, sound's own
ATT_WPN_Player_Fire`; 0 `WARN card304`; tally `first-person attenuation used 0`. What the user hears stays with the
playtest.

## Measured proof, c304b178a (b178 `f6596be3fcb2d778`), 2026-11-17

Far row: `audio row MoritaMK1Fire_MS ... 8626 uu from the listener ... attenuation override ATT_WPN_MoritaMK1`.
Tally: `card304: audio rows 32, owner unresolved 0, own attenuation spatialized 0, first-person attenuation used 0`.
Measured part done; only the hearing check waits on the playtest.
