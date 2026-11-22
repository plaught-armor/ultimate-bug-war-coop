# 302 The host still hears some of the client's own sound effects
Status 2026-11-22: open only for the hearing check in a playtest.

Playtest 2026-11-13 (user plays as client; the host player reports): "host still hears some sound effects from
the client, fix that". Which sounds is an open question to the user.

The host runs the client's abilities, cues and notifies on that client's server copy pawn. A non-spatial (2D)
sound started there is the client's own feedback and must not play on the host. A spatial sound from the copy
(shots, steps, voice at the client's position) is correct and stays.

## What exists (static)

`mod/src/ubw/player_c99.cpp`, host side:

| Card | Entry hooked | Rule |
|---|---|---|
| 099 | `GetPlayerController(ctx, 0)` | a caller that belongs to a server copy gets that copy's controller |
| 160 | `PlaySound2D`, `ADAudioManager::PlaySound2D`, `PlaySoundAtLocationFName` | skipped when the context or owner resolves to a server copy |
| 173 | `SpawnSound2D` | same, returns null |
| 176 | audio component `Play` with no attenuation to apply | muted, component kept |

All four decide by `c99_copy_of(ctx)`: the context is the copy, or reaches it through the outer chain or the
Owner and Instigator chain. A 2D sound leaks when:

1. its context does not resolve to the copy (an ability object, a weapon or attachment whose Owner is unset, an
   anim notify whose context is a mesh component, a gameplay cue notify actor, a widget, a subsystem, a timer or
   latent callback that lost the Blueprint caller);
2. it starts through an entry with no hook (`SpawnSoundAttached` or `SpawnSoundAtLocation` with a non-spatial
   sound or `bAllowSpatialization` 0, `PlayDialogue2D`, `CreateSound2D`, a MetaSound source's own 2D output, an
   `ADAudioManager` event other than `PlaySound2D`, an audio component placed on the pawn Blueprint and started
   by `Activate` or auto-activate);
3. it is spatial but attached to the wrong pawn: `SpawnSoundAttachedToPlayer` (0x05250E70, 0x05250F40) and
   "Get Player ..." nodes whose caller the card 099 remap does not cover, so the sound sits on the host's own
   pawn at full volume.

The client log cannot show this: the sounds play on the host. The host log (after the playtest) has the evidence:
the `card160: map:` tally, every `card099: host ... PlaySound2D ... not a server copy` line, and the `card099:
map: player 0 remapped ... callers not counted` line.

## Step 1 (static, junior, no run; host log after the playtest)

1. List every sound entry point of the game and engine that can produce a non-spatial sound (the list in item 2
   above, checked against the exe's imports and the `USTCommonUtilityLibrary` and `ADAudioManager` functions).
   Mark each: hooked by which card, or not hooked.
2. List every Blueprint and native caller on the player pawn's side that plays a 2D or player-attached sound:
   weapon fire, reload, low ammo, dry fire, equip, hit marker, hurt, low health, heartbeat, downed, revive,
   pickup, ammo, FedScore, objective, mech and turret cockpit, jetpack and AirDash (card 216), grenade cook,
   scope, sprint breath, footsteps in first person. For each: entry point, context object, whether
   `c99_copy_of` resolves it. The unresolved ones are the leaks.
3. From the host log: all `card099` and `card160` sound lines whose context is not a server copy and whose time
   is within 200 ms of a client action row (Fire, reload, pickup, ability); the sound names are the leak list.
   Compare with the user's answer.

Write the answers into this card before coding.

## Fix

One rule for every sound entry on the host, in the one place that owns it (`player_c99.cpp`): a sound with no
spatialization whose acting pawn is a server copy does not play on the host; a player-attached sound attaches to
the acting copy, not to player 0. The acting pawn is found by the card 102 scope (the server copy for the extent
of a host entry point) when the context chain does not resolve, so abilities, notifies, cues and latent callbacks
are covered by scope, not by a list of sound names. Unhooked entries from step 1 item 1 get the same hook. No
per-sound table.

The reverse direction is checked in the same pass: the client must not hear the host's own 2D sounds (the client
gets host effects through Fx and audio rows; a row for a non-spatial host-own sound is not sent).

- Rows: `card302: host t=<ms> <skipped|muted|reattached> server copy sound <name> via <entry>: context <class>,
  resolved by <chain|scope>`, per-map tally line beside the card 160 one,
  `WARN card302: host played non-spatial sound <name> via <entry> inside server copy scope, context <class>
  unresolved`.

## Sibling sweep (same class: the client's own feedback reaching the host through "the player")

| Item | Check |
|---|---|
| Every 2D sound caller of step 1 item 2 | skipped or muted on the host, still heard by the client |
| Camera shake, rumble, screen effects, gore on screen (card 099 table) | none on the host for client actions |
| HUD: hit marker, damage direction, notifications, FedScore text, pickup prompts | none on the host for client actions |
| Material parameter collections, post-process (card 160) | unchanged on the host |
| Mech and turret cockpit sounds with the client as pilot (cards 296 to 298 enter path) | host hears them spatial, from outside |
| Downed, revive, death and respawn stingers | only for the player they belong to |
| Host-own 2D sounds toward the client (audio rows) | not sent |
| Spatial sounds from the copy (shots, steps, voice) | still heard by the host at the client's position |

## Predictions (one run, after the playtest, batched)

- Client fires each weapon to low ammo, reloads, picks up ammo and a weapon, takes damage, gets a hit marker and
  a FedScore line, uses AirDash: host log has `card302` or `card160` skipped or muted rows for every 2D sound of
  those actions, zero `WARN card302`; the host's audio capture for that window (null sink monitor) holds only
  spatial sounds; client log shows the same sounds played locally.
- Host does the same: client log has no audio row for a host-own 2D sound.
- Digest round after: 0 mismatches.

## Senior trace 2026-11-13 (host log `client/UBWCoop-menu-20261113-182510.log`; one leak class settled)

Settled, codeable: a 2D sound whose context is a world object, played while a server copy is the acting player,
is not skipped on the host.

- All 5 logged `MS_ITEM_PickupAmmoHumanGenericMedium` rows (host 81344488, 81344840, 81344841, 81344849,
  81361099) are the client's pickups: each has `card073: AddInventoryItem_Internal on server copy` in the same
  millisecond. The sound's context is `ADPickupsCustomSoundComponent_C` (the pickup's component), owner null, so
  `c99_copy_of(ctx)` finds no copy and the row reads `not a server copy`: the host hears the client's pickup.
- `c160_copy_effect` (`player_c99.cpp:158`) tests only the context and owner chain. The acting-copy scope of card
  102 (`g_c102.acting`, set by `detour_c102_overlap`, the interaction scope at `:358` and the damage scope in
  `grenades.cpp:1673`) is read only by the player remap at `player_c99.cpp:279`, never by the 2D sound, widget
  or material-parameter skips.
- Fix: `c160_copy_effect` falls back to `g_c102.acting` (same `c102_excluded` caller list) when the context and
  owner resolve to no copy. Row gains `acting copy <name> (<entry>)`. The matching client side stays: the client
  plays its own pickup sound itself (check in the run that it does; if the client never plays it, the host sends
  it as a row to that client only).
- Not proven statically: that the pickup sound is played inside the overlap call (same millisecond as
  `DisablePickup`, callers `0x4C61EE3 < 0x51D4139`), not deferred. The new row answers it: a pickup sound row
  with `acting copy null` during a copy's pickup means the scope must also wrap the pickup's collect call.

Checked and not a leak in this log:

| Sound | Count | Finding |
|---|---|---|
| `MS_GORE_2D_*_Hitmarker` (context `WB_Hit_Marker_C`) | 55 | 39 within 400 ms of host fire only, 16 of both, 0 of client fire only: the host's own hits |
| `MSS_FOL_*Low_Ammo_Click`, `MS_PLAYERBARK_Breathing` (copy's components) | 1738 muted in the map ending 85657373 | muted at volume 0.00 |
| Copy damage, pain, `WeaponSwap_MS`, `MS_UI_FED_FedScore_Supply_Upgrade` | 201 skipped in that map | skipped |
| `HumanMovementJump_MS`, autocannon sparks from the copy | attenuated | 3D at the copy's position, correct |

Open (needs the user's answer, which sounds): `MS_UI_FedScore_Typewriter_Click` (113, context
`WB_TypewriterText_C`) and the 162 null-sound rows from `World` cannot be tied to a player from the log.

Sibling sweep additions: every effect skip that uses `c160_copy_effect` (2D sounds, widgets, material
parameters) gets the acting-copy fallback at once; interactions (`:358`: ammo crates, terminals, mounts) and
damage dealt by the copy (kill and hit confirm sounds) are the same class.

Prediction (one run, batched with 304): client picks up 5 ammo and grenade pickups 300 m from the host: 5 rows
`skipped server copy 2D sound MS_ITEM_Pickup... acting copy`, zero `not a server copy` pickup sound rows, the
client's log has 5 own pickup sound rows.

## Build 7 (coded, compiled; diff `b7-G-302.diff`)

Static check of the pickup collect scope (exe):
- `AADPickupBase::OnBeginOverlap` (0x4c61750) calls `TryPickup` (0x4c628f0) at 0x4c61844; TryPickup tail-jumps
  through vtable +0x758 (Pickup). This runs inside the BeginComponentOverlap broadcast, so inside
  `detour_c102_overlap`'s scope. No extra wrap is needed.
- `ASTItemPickup::OnInteraction` (0x51d03f0) calls `Pickup` (0x4c61940) at 0x51d0597 / 0x51d0618, inside
  `c102_interact`'s scope.
- The reported stack `0x4C61EE3 < 0x51D4139` is `AADPickupBase::Pickup_Implementation` called as the super of
  `ASTItemPickup::Pickup_Implementation`.
- Open: `DeferredBeginPlay` (0x4c6111a) and `execTryPickup` (0x4c63935, Blueprint) call TryPickup with no scope. The
  row below shows them as `acting copy null`.

Code:
- `C102Scope` gains `site`: the world actor the act is on. Overlap: the other side's owner (the pickup).
  Interaction: `interact_target`. Damage: none.
- `c160_copy_effect` (2D sounds, material parameter writes) falls back to the scope's acting copy only when the
  effect's context or owner resolves to the site actor and the caller is not `c102_excluded`. Widget creation
  (`detour_c160_exec_create`) uses the same test on the caller.
- Why narrower than "any effect inside the scope": a level script that completes an objective on the copy's overlap,
  or a stinger or marker for every player, runs inside the same scope with the LevelScriptActor as context. Those
  stay on the host (full world sync).
- Rows: the card160 skip row appends `acting copy <name> (<entry>)`; the card099 PlaySound2D row appends the same,
  `acting copy null (none)` outside a scope.
- The run checks the client side too: the client plays its own pickup sound.

## Senior verdict, b8 run 2 (2026-11-13)

Attribution works: `acting copy BP_PlayerCharacterMI_C (overlap)` for `MS_ITEM_PickupAmmoHumanGenericMedium`. The
host stood about 1500 uu away (the warp exceeded the 5000 dist cap and failed), so the far case is unproven.
Prediction for the last run (host 30000 uu away, wait on the host log): the same `acting copy ... (overlap)` row and
no pickup sound played on the host.

## Senior verdict, b10 last run (2026-11-13): confirmed, host about 97000 uu away

`card160: host ... skipped server copy 2D sound` for `MS_ITEM_PickupAmmoHumanGenericMedium` x2 and
`SFX_Pickup_Negative` x3, each `acting copy BP_PlayerCharacterMI_C (overlap)`. Open: the user's list of which other
client sounds the host heard.

## Parked on hearing (2026-11-18 13:15)

The user cannot test audio for now. This card waits for the next playtest with sound, where the user names which client sounds the host still hears. Under the hearing-only rule it is the one kind of card that waits on a playtest.
