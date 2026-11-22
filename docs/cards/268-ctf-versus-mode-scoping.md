# 268 CTF versus mode, one player per side: static scoping
Status 2026-11-22: scoping, parked until after the co-op playtest.

User, 2026-11-13: wants to consider Capture the Flag as a competitive mode between players, one player per
side (1v1, each side has a flag). The base game has no versus mode: CTF exists only as the scripted
`CaptureTheFlag_O1` chain in FED_C1M1 against AI laser troopers. Not a play blocker; runs after the Trooper
co-op playtest. This card is static only (no code, no run) and ends with a design note for the user.

## Pieces in the game files

- Flag item `BP_SatchelInventoryItem_CTFFlag`, flag site `BP_SatchelPlacementSite_Flag`, pickup
  `BP_SatchelItemPickup_CTF`, HUD icon `T_Flag_Small`.
- Side weapons `BP_Weapon_LaserMorita` (blue) and `BP_Weapon_LaserMoritaRed` with pickup
  `BP_WeaponPickup_LaserMoritaRed`, ammunition `BP_Ammunition_LaserRed`, blue and red trails.
- The CTF course in FED_C1M1; map `/Game/Maps/Testing/TrainingCamp_Showcase`.
- `UpdateEquippedGunGameWeapon` (0x525648D calls AddAmmunitions): leftover gun-game logic.

## Questions to answer

1. Player against player damage: what blocks it today (team attitude, damage filter, GAS tag, the mod's own
   hit relay) and the smallest change that lets side A hurt side B only in this mode.
2. Laser rifle: real damage or a training-only hit (what the laser troopers' hits do to the player in
   FED_C1M1; knock-down, respawn point).
3. Flag: what the flag item and site do natively (carry, place, the satchel hold), and what is missing for
   take, carry, drop on death, return and capture at the own base.
4. Map: can FED_C1M1 load with its level script chain off (or `TrainingCamp_Showcase` load at all), where two
   bases and spawns fit on the course, and what the doors and presences do without the script.
5. Death and respawn in a versus round with the current down/revive code (cards 133 class).
6. Score and round end: which existing widget can show two scores and a winner.
7. What `UpdateEquippedGunGameWeapon` belongs to and whether a mode switch already exists in
   `ASTGameplayGameMode`.

## Output

A design note in this card: rules proposal (captures to win, respawn delay, flag return), mechanism per
question, the list of new sync classes, and a card split with run budgets. The user decides before any code.

## Findings (static, 2026-11-13)

Sources: PDB publics, disassembly, asset name maps, logs of card 240 run 5. Property values inside assets
were not read (no zen property parser), so points marked "open" need a step 1 in their own card.

1. **Player against player damage already works.** `ASTPlayerCharacter::BeginPlay` (0x51BE240) ends with
   `SetCanDealFriendlyFire(true)` (0x5101EE0; ASC tag `HealthAndDamageSystem.FriendlyFire.Enabled`). Card 021
   records the host's shots damaging the client's pawn. The mod has no faction or friendly-fire code today.
   Co-op keeps this as is; versus needs nothing to turn damage on.
2. **Laser rifle damage is real.** Laser trooper hits on the player log 50.000 damage, preset
   `DamageModPreset_Default`, no scaling. Player Health is 100 with regeneration
   (`Abilities.HealthRegen.NoRecentDamage`), so two laser hits kill. Open: the player rifle's own damage value
   (`BP_WeaponAttributeSet_LaserMorita`); one log row in the first run settles it.
3. **Sides exist as factions.** `/Game/Data/Factions/DT_FactionRelationships` has `Faction.BlueTeam`,
   `Faction.RedTeam`, `Faction.Human.BlueTeam`. `UADFactionComponent` (pawn +0x6B0) has
   `SetFactionTag` (0x4A276C0) and `RestoreFactionTag` (0x4A27680). Setting a side per player gives hostile
   cursor and targeting for free (`IsHostileFaction` 0x50F0F90). Not needed for damage.
4. **Flag is a satchel item.** `BP_SatchelItemPickup_CTF` (red flag pole; take), inventory item
   `BP_SatchelInventoryItem_CTFFlag` (tag `InventoryItem.Satchel.CTFFlag`), `BP_SatchelPlacementSite_Flag`
   (blue flag pole; plant, prompts INTERACTMSG_PLANTFLAG and _INSUFFICIENT), `BP_SatchelBuilding_CTF`.
   Native site calls: `ASTSatchelPlacementSite::OnInteraction`, `CanInteract`, `ResetSatchelSite`,
   `NeutralizeSite`. Take and plant are native and already synced as interactions (cards 050, 054, 098).
   Missing natively: a second flag pair with the colours swapped, drop on death, return, re-arm after a
   capture (`ResetSatchelSite` is the candidate).
5. **Map: the FED_C1M1 course.** `TrainingCamp_Showcase` holds only art (no flag, spawns or scripts) and is
   out. FED_C1M1 has the arena, one flag pickup, one site, `BlockingVolume_Bullets_CTF` 1 to 4,
   `BP_PlayerPresence_CTF` and `_BlueFlag`, and laser rifle pickups. The level script chain must be held
   back in versus (training objectives, AI laser troopers, dropship).
6. **No versus code in the game.** No deathmatch, score, team score or player respawn symbols exist.
   Player death is `ASTPlayerCharacter::StartDeath` / `HandleDeathEvent` / `CompleteDeath`; the mod already
   owns that path (card 043 downed and revive, `pawn_slots_respawn`, `c45_respawn_check`, restart).
   `ASTGameplayGameMode` has no mode switch.
7. **Gun game is a secret** (cheat modifier `Secret.GunGame`), not a mode. Unrelated; `Secret.AllOutWar`
   makes every character hostile to every other and is not needed either.
8. **Score display.** No score widget in the game. The mod already builds its own panels with text
   (`lobby_ui.cpp`, card 052) and can show two scores, a flag state line and the winner.

## Design proposal (for the user's decision)

Rules: host is blue, client is red, each starts at the own flag with a laser rifle. Take the enemy flag,
carry it to the own flag site, plant it: one capture. A killed carrier drops the flag where they fell; the
owner returns it by touching it, the enemy can pick it up again. A killed player respawns at the own base
after 5 s (no downed state, no revive in versus). First to 3 captures wins; the panel shows the winner and
the host can start a new round or return to the lobby. No bugs, no AI troopers, no training objectives.

Mechanism: one new mode flag chosen in the co-op lobby panel and sent with the travel (versus on FED_C1M1).
With the flag set, the host holds the level script chain, sets the side faction per player, moves both
pawns to their bases, and runs a small host-side match state (flag home, carried by slot, dropped at
position; captures per side; round over). Flag take and plant reuse the native satchel pickup and site;
the second pair is spawned by the mod from the same classes. Death in versus goes to the existing mod death
path with a versus branch (respawn in place of downed).

New sync classes: match state (scores, flag state, round over) host to client; versus respawn; the mode
flag at join. Everything else (fire, damage, pickups, interactions, doors) uses existing classes.

## Card split (after the co-op playtest)

| Card | Goal | Runs |
|---|---|---|
| A | Mode flag in lobby and travel; FED_C1M1 loads with the script chain held; both players at their bases with laser rifles; side factions set | 1 |
| B | Kill and respawn in versus (5 s, own base), both directions, laser rifle damage row logged | 1 |
| C | Second flag pair spawned; take, carry, plant counts a capture; site re-arms | 1 |
| D | Carrier death drops the flag; return and re-take | 1 |
| E | Score panel, win at 3, new round, back to lobby | 1 |

Each card starts with a static step for its open point (A: how the script chain starts and what else it
arms; C: property values of the flag site and whether spawned copies work without the level script).

## Decisions wanted from the user

1. Rules as proposed (first to 3, 5 s respawn, drop on death with touch return)?
2. Weapons: laser rifle only, or also the normal Morita and grenades?
3. Arena: only the fenced CTF course, or the whole training camp?

## User decisions (2026-11-13)

- Weapons: laser rifle only. Arena: the fenced CTF course only.
- Round rules are not fixed: a menu before entering the game sets them (captures to win, respawn delay; defaults
  3 and 5 s). Card A carries the menu: versus options in the co-op lobby panel (card 052 class), chosen by the
  host, sent to the client with the mode flag and shown on both screens before travel.
- Open: which further options the menu holds (asked below the card split when card A is written).
