# Card 455: The client never hears its own hurt sounds

Status: open 2026-11-16 (senior audit, user rule "all effects sent"). Budget: three runs, batched with card 207.

## Fault

The player's hurt sounds are 2D sounds the player Blueprint plays from the damage event
(`USTCommonUtilityLibrary::execPlaySound2DFName` -> `PlaySound2DFName` 0x145248710 -> `UADAudioManager::PlaySound2D`,
caller exe+0x52487C4). The damage event runs only on the host. For a client's pawn the context is the server copy,
so card 160 skips the sound on the host (correct: it is not the host's sound), and nothing sends it to the client.

Evidence, run c447b152a (client card 099 log at 291 of 300 rows, so not capped): host skipped 19
`MS_GORE_Human_Player_Armour_Damage_Light`, 4 `..._Armour_Damage`, 2 `..._Health_Damage_Heavy`, 1
`..._Health_Damage_Light`, 1 `..._Armour_Break`, 2 `MS_PLAYERBARK_Pain_Heavy`; the client played 0 of them. Run
c448b151b shows the other class: the host skipped 4 `WeaponSwap_MS` and the client played 4 itself (its own ability
runs locally), so a relay of every skipped sound would double those.

## Fix

- Host, `detour_c99_ad_sound_2d` (player_c99.cpp), when `c160_copy_effect` returns true: if the sound's name starts
  with a row of a small prefix table (`MS_GORE_Human_Player_`, `MS_PLAYERBARK_Pain`), send the sound's asset path,
  volume and pitch to the PlayerController that owns the server copy (`c95_send_one`, a new `ChanOp` with text
  arguments, or an existing text op of the same owner if one fits).
- Client: resolve the asset with `c74_find_asset` and play it with the original `UGameplayStatics::PlaySound2D`
  (`g_orig_c99_sound_2d`, context and owner the own pawn). Unresolved asset: one WARN row, no sound.
- Rows of the table are data backed by log evidence. A sound goes in only when a host skip has no client row of the
  same name.
- Log: host `card455: host relayed hurt sound <name> to <PC>` (capped), client `card455: client played <name>`.

## Proof (one run, c207 probe, client hurt by real bugs, card 099 log on)

- Each host `card160 ... skipped server copy 2D sound MS_GORE_Human_Player_*` row has a host `card455` row and a
  client `card455: client played` row of the same name within 200 ms.
- No `card455` row for `WeaponSwap_MS`. No host-side sound for the client's hits (the card 160 skip stays).
- 0 EXCEPTION, digest 0 mismatches.

## Open

- Resolved 18:12: `MS_ITEM_PickupAmmoHumanGenericMedium` is a third row (`MS_ITEM_PickupAmmo`). Run c448b151b: 3 host
  skips (context `ADPickupsCustomSoundComponent_C`, acting copy by overlap, card 302), 0 `PickupAmmo` rows in the
  whole client log. `MS_WEAP_PickupWeaponGeneric` is not a row: the client plays it itself (host 1, client 2).
  The relay therefore resolves the copy as `c160_copy_effect` does, `c302_acting_for` included.
- Patch: scratchpad `patch_c455.py` (9 edits, `--check` passes on the b155 tree). Adds `ChanOp::HurtSound`.
- Limit: the client plays through `UGameplayStatics::PlaySound2D`, not `UADAudioManager::PlaySound2D`; any mix or bus
  the manager adds is not applied. Hearing check at playtest.
- Bark subtitles and bark range belong to card 451.
- Checked 18:15, no double play expected for the ammo row: runs c423b124a to c423b146a show host skips of
  `MS_ITEM_PickupAmmo*` with 0 client plays every time (8 runs). `SFX_Pickup_Negative` is not a row: the client plays
  it itself at the same tick as the host's skip (c423b143a: host 504646089, client 504646074). The old run c257-c2m1,
  where the client played the ammo sound, was the host pawn's copy overlapping a pickup in the client's world; current
  builds no longer show it (c207b155a: host's own 3 pickups, 0 client rows).

## Step 2, 18:16: every item pickup sound, not only ammo

Same fault class on two more rows of `ADPickupsCustomSoundComponent_C`:

- `MS_ITEM_PickupSatchelItem`: c418b121a host skip at t=486580299 (client's own
  Interact, relayed Use), client log has no row for it (card 099 budget not spent
  at that tick: typewriter rows continue after). The client's own component only
  calls `ADAudioManager::PlaySound2D` with a null sound (4 rows).
- `MS_ITEM_Pickup_TacticalSupport`: b14-soak-c1m1 host skip, 0 client rows.

Not in the class: `MS_WEAP_PickupWeaponGeneric` (c448b151b: client plays it itself
at t=510228905, host skip at 510228928) and `SFX_Pickup_Negative` (48 client plays).

Fix: prefix `MS_ITEM_PickupAmmo` becomes `MS_ITEM_Pickup` (patch
`patch_c455b.py`, one string). Proof: a host `card455 ... relayed hurt sound
MS_ITEM_PickupSatchelItem ... sent` row with a client `played` row, asset 1; no
client `card099` play of the same sound at that tick (no double play).

## Run c207b156a (b156 `97e63881d4a00093`), 18:20: hurt rows proven

Host: 8 `card160 ... skipped server copy 2D sound` rows for `MS_GORE_Human_Player_*`
and `MS_PLAYERBARK_Pain`, each with a `card455: host ... relayed hurt sound ...
(sent)` row at the same tick. Client: 8 `card455: client ... played hurt sound ...
(parsed 1, own pawn 1, asset 1)` rows 10 to 26 ms later. 0 `WARN card455`, 0 rows
for `WeaponSwap_MS`. Compile fixes by the junior: `kC132Ops` row for `HurtSound`,
`sscanf_s`. Open: pickup rows (step 2, b157) have no run proof yet; the hearing
check stays a playtest item.

## Run c418b157a (b157 `1e6244b6739bbcd0`), 18:33: pickup row proven, closed

Client's own satchel pickup: host `card160 ... skipped server copy 2D sound MS_ITEM_PickupSatchelItem ... acting copy
BP_PlayerCharacterMI_C (interaction)` and `card455: host t=514085939 relayed ... (sent)` at the same tick; client
`card455: client t=514085959 played hurt sound .../MS_ITEM_PickupSatchelItem ... (parsed 1, own pawn 1, asset 1)`, one
play, 20 ms later. 11 hurt relays, 11 client plays in the same run. 0 `WARN card455`, 0 EXCEPTION. Closed. The hearing
check (mix, loudness) stays a playtest item.
