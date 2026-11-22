# Card 570: client has no reserve ammo and no grenades in C1M1 (first seen after a checkpoint reload)

Found in run c551b380a (b380, C1M1). After-reload shots: the client's Morita shows 60/0 (no reserve) and 0 grenades;
the host shows 29/240 and 1 grenade.

## Corrected cause (2026-11-21, logs and shots)

The reload is not the cause. The client already had no reserve and no grenades before the reload: shot
`c373-566-host-holes-client` shows 60/0 with x0 grenades, and `c373-566-both-holes-client` shows 0/0 with x0. At the
same point the host shows 44/180 with 1 grenade.

The host-own pawn took every ammo and grenade pickup on the Morita table. The host's `goto` to
BP_WeaponPickup_MoritaMk1_C_0 stuck at (6572, 11441), next to the table, and on the way it walked over:
- BP_GrenadeAmmoPickup_Frag_C_0 at t=146623773 (`card245` taker host-own; the client logs `card254: client skipped
  Pickup_Implementation`, own pawn 0)
- BP_GrenadeAmmoPickup_Incendiary_C_0 at t=146623823 (same pattern)
- Morita ammo: `card071: host AddAmmunitions ... AmmunitionType.Morita 0 to 120`, then `120 to 240`, at t=146625272
  and t=146625389

These are walk-over (overlap) pickups that the base game places for one player. The first player to touch them gets
everything, and the pickup is then disabled for both (the card 098/095 rows reach the client). The client's own map is
then `Laser 300 @Morita 60 @UnderslungShotgun 8`, with no Morita reserve entry.

The checkpoint restore path (card 143) worked as designed:
- host Reload at t=146681320, client Restore sent at t=146684683
- host applied it at t=146684727 ("weapons ... matches the list")
- client AmmoSet Laser 0 to 300 at t=146685132

It restored the copy's state, and that state already had no Morita reserve and no grenades.

Side finding: after the restore the client's magazine reads 60 (a fresh Morita), not its saved count (0 before the
reload). The Reload/Restore text carries the reserve map but not the card 274 magazine count.

## Decision (user, 2026-11-21): per player

Each living player gets an ammo or grenade pickup's grant once, like card 495's weapon hold. The pickup goes away only
after every living player has taken it.

## Fix, b385 `da01109e78caca54` (= b384 + card 570, build-b5 copy kept)

Host:
- New detours on `ASTAmmoPickup::Pickup_Implementation` (exe+0x51D33F0, every ammo pickup) and
  `ASTItemPickup::Pickup_Implementation` (exe+0x51D3EF0, only `BP_GrenadeAmmoPickup_*`; the satchel stays first come).
  They cover placed pickups only; a pickup a bug drops stays first come.
- The credited character is the game's own `GetOverlappingCharacter` answer, unless that player already took the
  pickup. In that case it is a living non-taker that overlaps the pickup (both players stand on it).
- `c495_join` (split from `c495_refuse`) refuses a repeat take: no credit and no base call, and a `card495: ... refused
  Pickup_Implementation` row. Otherwise the player joins the taker set.
- The new `GetOverlappingCharacter` hook returns that character for the pickup during the call, so the subclass
  credits it.
- The base call goes through card 254 into `c495_hold`, which holds it until the takers cover every living player
  (the card 495 release row). The c495_recheck step releases it after a non-taker goes down or leaves.
- If the subclass returned before its base call (no credit, e.g. full ammo), the join is undone.
- `kC495Max` goes from 4 to 32. With a full table a take logs a WARN and stays first come.
- Each take logs: `card570: host t=N take of <pickup> credited <pawn> (<side>)[ instead of the first overlapper], base
  call 0|1`.

Client:
- When the own pawn overlaps a host pickup, the local subclass run is kept for the first take only. Card 073 binds the
  grenade input from it, and card 384 pairs the local ammo credit with the host's Ammo frame.
- A repeat take is skipped whole, with the row `card570: client ... skipped repeat take`. The taken set is cleared at
  map end.

Magazine (side finding):
- At Reload receipt the client keeps its own `c274_magazine_text`. After the rejoin, on the new pawn (after the
  AmmoSet seed), it sets that count exactly on the matching equipped weapon. Rows: `card570: client ... Reload keeps
  own magazine` and `kept magazine set on`.
- The set gives up after 60 s and logs a WARN. Only the equipped weapon's magazine is carried.

## Predictions (C1M1 range run)

The host takes the frag, incendiary and Morita ammo pickups first. Then the client takes each one.
- Host, at its own take: `card570 ... credited <host pawn> (host own) ... base call 1`, then `card495: ... held
  Pickup_Implementation ... taken by 1 of 2`. The pickup stays visible on both screens.
- Host, at the client's take: `credited <server copy> (server copy)`, then `card495: ... released ... taken by 2 of 2`.
  The pickup goes away on both screens.
- The HUD shows grenades x1 (frag) on both players, and a Morita reserve above 0 on both.
- When the host steps on its own held pickup again: a `refused Pickup_Implementation` row and no change in the count.
- After a checkpoint reload, the client's magazine shows its count from before the reload, not 60.

## Known gaps

- The first taker hears no pickup sound while the base is held (OnPickup is held with it).
- A host checkpoint reload in place keeps the table entries of the old takes.
- Armour, health and tactical support pickups created from a crate or a bug drop are not placed pickups, so they stay
  first come.

## b386 `311656ba9f01b678` (= b385 + armour, health and tactical support)

The user answered on 2026-11-21: "Yes, all per player". `ASTArmourPickup` (exe+0x51D37B0), `ASTHealthPickup`
(exe+0x51D3B50) and `ASTTacticalSupportPickup` (exe+0x51D4220) `Pickup_Implementation` go through the same `c570_take`.
Each one calls `GetOverlappingCharacter` and then the base call, the same as the ammo pickup.

A player at full health or armour gets no credit. The game returns before the base call, so the join is undone and
that player can take the pickup later.

C1M1 has BP_TacSuppPickup_BombingRun_Tutorial_C. With the hold, its OnPickup script runs only after both players take
it, the same as card 495's weapon sites.

## b389 `89e1d458b2136a3e` (= b388 + two reload fixes, build-b5 copy kept)

Run c570b386a: the pickup rows passed (held 1 of 2, released 2 of 2), and the HUD showed Morita 60/240 and grenades x2
on both players. Two checkpoint reload gaps:

- **Magazine:** the client's pawn was already gone when the Reload came ("no own pawn" 6 ms before it), so the keep
  logged `(none)`. Now `c72_client_step` notes the own magazine text whenever the magazine sum changes
  (`c570_magazine_note`, per shot or reload, not per frame). A frame with no magazine weapon keeps the last note. The
  keep falls back to the note when the pawn is null.
- **Grenades:** the load's GrenadeItem frames reached the client while it had no pawn. They were deferred and then
  dropped at the map change, and the Restore carried only weapons and ammo. The Reload text now has a 4th field,
  `;<definition path> <count> ...`. It lists every item definition card 446 noted for that controller, each with the
  copy's live `c73_count` (0 included). `c143_after_load` now empties the card 446 table after the Reload texts. The
  client echoes the text. `c143_apply` cuts ammo at the next ';' and sets each count on the new copy with
  `c446_set_count`, which relays a GrenadeItem frame. A text that is too long drops the items first (WARN). Kept
  (card 440) and older texts have no items part.

Predictions (C1M1 checkpoint reload after a range take):
- Client: `Reload keeps own magazine: @<tag> <n>`, not `(none)`. Then `kept magazine set on`, and the HUD shows that
  count rather than 60.
- Host: the Reload text ends `;<...>Frag... 1 <...>Incendiary... 1`. At the Restore, `card446: ... kept item count
  ... 1 on <new copy>: 0 to 1` (or 1 to 1 when the starting items already hold it). The client gets `card073:
  GrenadeItem ... our count 0 to 1`, and its HUD shows the same grenades as before the reload.

### Run c570b389a (junior, b389 `89e1d458b2136a3e`, `UBWCOOP_C571=1`): both reload fixes pass

Drive rc 2 came from scenario lines 63 and 67, not the build. All 3 Morita gotos stopped short of their targets
("stuck on the leg past the navmesh path's end"), so the client never took MoritaSmall_C_1.

- Magazine prediction held. Client t=152996472 `Reload keeps own magazine: @AmmunitionType.Morita 51
  @AmmunitionType.UnderslungShotgun 8`, then t=153000086 `kept magazine set on BP_PlayerCharacterMI_C`, and card274
  Morita 60 to 51. The HUD shows 51/120 before and after the reload.
- Grenade prediction held. Host t=153000059 `card446: kept item count BP_GrenadeItem_Frag_C 1 ...: 0 to 1`, and the
  same for Incendiary. Client t=153000082 `card073: GrenadeItem BP_GrenadeItem_Frag_C 1: our count 0 to 1`. The HUD
  shows x1 x1 before and after. The client's 2 pending counts were dropped at the map change (t=152997560). The
  Restore re-sent them.
- Pickup rows: 4 host-own credits. Server-copy credits for Frag_C_0, Incendiary_C_0 and MoritaSmall_C_0. C_1 was
  released by the re-check during the reload (taken by 1 of 1 living players).
- New find: after the reload, the weapon selector panel shows MORITA MK1 "60/120" while the HUD shows 51. The
  panel likely reads a slot value that the magazine set does not update. Carded as card 574.

Card 570 is closed. Shot proof: 570-mag-before-client and 570-mag-after-client.

## Regression in run c397b398a (2026-11-21) and fix in b399

After the checkpoint restart, the client HUD showed 60/60. Cause: the kept magazine (Morita 3) was set at
t=159263939 on a pawn the card 294 restart held in the old world ("respawn in place held"). LoadMap followed at
t=159265039, so the travel destroyed that pawn, and the new map's pawn kept the default 60. Run c570b389a passed only
because its set came after "restart done".

Fix (b399 `3aecb88abac8f6f7`): `c570_magazine_step` waits while `c294_restart_pending()` is true. That window
starts at the host's restart row and ends at the new map's first pawn. Proof: in a checkpoint restart run, the
"kept magazine set" row comes after "card294 ... restart done", and the HUD and selector show the kept count.
This also covers card 574's 60/60 in the same run.
