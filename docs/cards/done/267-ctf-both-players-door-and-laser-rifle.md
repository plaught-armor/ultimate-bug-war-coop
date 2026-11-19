# 267 CTF course (FED_C1M1): both players walk through the doors and both get a laser rifle

User, 2026-11-13: "For the CTF we need both player to be able to walk through the door and also grab a laser
rifle." Play blocker class: fix before the play build.

## What is proven and what is not

- Card 240 (27 of 27 on FED_C1M1) completed `CaptureTheFlag_SO1` by the host's pickup of
  `BP_WeaponPickup_LaserMorita_C_0` only. The client never took a laser rifle in that run.
- Card 240 put both players onto `BP_PlayerPresence_C_7` and `_8` by warp after the client's goto stuck on a
  wall (run 3). No run has the client walk through `BP_Door_Sliding_Large_C_0` and `_1`.
- Card 161 sends door states from the host; card 227 holds door and wave presences until every player is
  inside. Neither proves the client's pawn passing the doorway.
- Card 050 proved one client pickup of the laser rifle in isolation (host had not taken it).

## Step 1 (static, no run)

- `ASTWeaponPickup::PickupWeapon` (0x51D2FD0) for a weapon the player does not hold: is the pickup consumed
  (+0x521) or destroyed after the first player takes it? Is there one laser pickup on the course or several?
- What does `CaptureTheFlag_SO1` listen to (pickup event of that instance, or the player holding the class)?
- What the laser rifle is needed for afterwards (the LaserTroopersRed wave) and whether a player without it is
  stuck or only weaker.
- Door path: with cards 161 and 227 in, which side moves `PawnBuffer` collision of doors `_0` and `_1`, and
  whether the host's server copy of the client can pass at the moment the client sees the door open.

## Step 1 answers (static plus existing logs, 2026-11-13)

### The pickup is single-use in the base game

- There is one laser pickup on the course. Every c240 run logs `card132: host inventory (map load) class
  BP_WeaponPickup_LaserMorita_C n=1 placed=1`, at (-4207, 9224, 225).
- `ASTWeaponPickup::OnInteraction` 0x51D0630 calls `PickupWeapon` 0x51D2FD0 on the pickup (`this-0x490`). If
  either +0x520 (weapon given) or +0x521 (ammo only) is set afterwards, it calls `AADPickupBase::Pickup_Implementation`
  0x4C61DC0, which does three things:
  - FX through `SpawnSystemAtLocation`.
  - `DisablePickup` 0x4C61170 (call site 0x4C61EE3).
  - `FADPickupDelegate::Broadcast` 0x4C60E60, which is `OnPickup`.
- The first take by any player therefore disables the pickup for everyone. Evidence in c240-c1m1-r8 host:
  - `DisablePickup on BP_WeaponPickup_LaserMorita_C ... callers 0x0 < 0x4C61EE3 < 0x51D06B3` at t=166901371.
  - card095 PlayerCollider and InteractionCollider rows go 1 -> 0, sent to the client.
  - The card 145 enabled-state sync hides it on the client as well.
- Nothing is destroyed. A second player who walks up finds the pickup disabled.
- `PickupWeapon` for a weapon the player does not hold:
  1. `USTWeaponUtilityLibrary::AddWeapon` 0x522AA30.
  2. Ammo setup (`DoesWeaponAmmunitionTypeUseMagazine` 0x52347F0, `SetEquippedAmmunition` 0x524D1E0, or
     `AddAmmunitionTypeToInventory` 0x51BC280).
  3. Sets +0x520.
  4. Broadcasts the inventory delegate at inventory+0x148 (0x1451D33C1).
  5. Broadcasts the character delegate at character+0x7B0 with pickup+0x2C8 (0x1451D33D4).
- For a player who already holds the weapon, only the two ammo credits run, then +0x521 is set.

### What `CaptureTheFlag_SO1` listens to: the instance's `OnPickup`

- In the same call, the FED_C1M1 level script runs its `OnPickup` handler for `BP_WeaponPickup_LaserMorita_C_0`, the
  same binding pattern card 056 found for the health and tac pickups. In c240-c1m1-r8, all at t=166901371 to 166901378:
  - `CompleteSubObjective(CaptureTheFlag_O1, CaptureTheFlag_SO1)` and `AddSubObjective(..., CaptureTheFlag_SO2)`.
  - Door `_1` `DoorState 0 -> 2`.
  - The player's loadout swap.
- The handler swaps the loadout of player 0, not the player who took the pickup:
  - `RemoveWeaponFromOwnerInventory BP_Weapon_MoritaMk1_C`, then `RemoveWeaponFromOwnerInventory BP_Weapon_LaserMorita_C`.
  - Then `GiveWeaponByClass(BP_WeaponDefinition_LaserMorita_C) ... on BP_PlayerCharacterMI_C (host-own), weapons 1;
    callers 0x522AC1D < 0x50B88A5`. 0x50B88A5 is in `USTWeaponUtilityLibrary::execAddWeaponByTag`; the two removes
    come from `execDropWeapon` (frames `... < 0x50B8E00 < 0x1557274`), so the script drops both weapons and adds the
    laser by tag.
  - The course is laser-only for the host after this.
- Client-first takes (c63c t=908280, c67g t=7341613) show the fault directly:
  - `PickupWeapon` gives the laser to the client's server copy: `GiveWeaponByClass(...) on ... (server-copy), weapons
    2; callers ... < 0x51D3276 < 0x51D0695`.
  - The script then gives it again to the host's own pawn (`host-own, weapons 1`).
  - The client keeps its Morita plus the laser. The host is stripped to the laser although the host never took it.
- So whoever takes the pickup, only the host gets the course loadout. In the host-first order of card 240, the client
  gets nothing.

### The LaserTroopersRed wave

- The waves run after the take:
  - `BP_PWD_LaserTrooperBlue_C` started at the presence `_7` entry.
  - `BP_PWD_LaserTroopersRed_C` started at t=166903533, with `CaptureTheFlag_SO2`, `SO3` and `SO4` after it.
- Both waves are scaled for 2 players (base 12, scaled 19).
- No log shows a client kill of a laser trooper, so whether the Morita damages them is not established.
- A client without the laser keeps its Morita (host-first order), so it is not locked out of the course by inventory.
  The proof run must show client hits on `BP_AI_Trooper_Laser_*`.

### Doors `_0` and `_1`: the host moves `PawnBuffer`, and the client follows

- Card 161 tracks both doors on the host. `DoorState` and `PawnBuffer` change on the host first (card095
  `SetCollisionEnabled ... caller exe+0x37C2C74`) and are sent to the client. The client sets back its own
  door-logic changes (card 227 `WARN card161: client-only DoorState ... set back`).
- Door `_0` in c240-c1m1-r8:
  - Opens by the objective at t=166892049 (0 -> 2). `PawnBuffer 3 -> 0` at t=166894684.
  - Closes at t=166899066 (`PawnBuffer 0 -> 3`, DoorState 1 -> 3), when the `_7` presence fires.
  - Card 227 holds that presence, so the door now closes only after both players are inside.
- Door `_1` opens in the `OnPickup` frame (t=166901378, 0 -> 2), so it opens once for both players.
- The client sees each door open about one send after the host. On the host, the client's server copy therefore
  passes a doorway that is already open, so no block is expected on opening.
- On closing, the host closes first. A client still in the doorway is then inside the host's `PawnBuffer`.
- Not established statically:
  - Whether the host's copy movement sweeps against `PawnBuffer`.
  - The route card 227 saw the client take past the closed door `_0`.
- The proof run decides both, with real walks and no warps across the doorway. No door code change is proposed
  until it shows a fault.

### Proposed step 2 files (no code until the senior answers)

Root cause: the level script grants the course weapon to player 0 only, and the pickup is single-use. There are two
shapes:

- A (recommended, class fix). On the host, when a level script changes the host's own pawn's weapons through
  `USTWeaponUtilityLibrary` Blueprint calls (`execAddWeaponByTag` 0x50B88A5, `execDropWeapon` 0x50B8E00, and their
  siblings on the same library), apply the same change to every living client's server copy. The card 050 `Weapons` list then carries it to each
  client.
  - Files: `mod/src/ubw/weapons.cpp`, next to the card 021 `GiveWeaponByClass` and card 132
    `RemoveWeaponFromOwnerInventory` hooks.
  - The caller check uses the Blueprint return address, so player-taken pickups (caller 0x51D3276) are not doubled.
  - This covers every Trooper map whose script hands out weapons, with no per-map list.
- B (only if the user wants each player to grab it physically). Keep a level-gated weapon pickup enabled until every
  living player holds the class:
  - Hook `Pickup_Implementation` 0x4C61DC0 for `ASTWeaponPickup`.
  - Skip `DisablePickup` until the last player has taken it.
  - Broadcast `OnPickup` only on the first take, so the objective, door `_1` and the waves fire once.
  - Under B, the player-0 loadout swap still needs A for the second player.
  - Files: `mod/src/ubw/weapons.cpp` (card 050/140 pickup code), `hooks_ubw.cpp` (hook table entry).
- Doors: no change proposed. If the proof shows the host copy blocked while the client sees the door open, the fix goes
  in the card 161 door code (`mod/src/ubw/` door file with the card161 lines).

## Step 2 (fix by class)

Every mission weapon pickup that a level script gates on (all Trooper maps, list from the asset list): each
player can take the weapon. Preferred shape: the pickup stays takeable until every living player holds the
class (per-player consumed state on the host), the objective completes on the first take as in the base game.
No per-map special case. Door fault, if step 1 finds one: fix in the card 161 door class.

## Step 2 as written (shape A, 2026-11-13)

- `weapons.cpp`:
  - The exec detours of `USTWeaponUtilityLibrary` (`execAddWeapon`, `execAddWeaponByTag`, `execGiveWeaponByClass`, and
    new `execDropCurrentWeapon`, `execDropWeapon`, `execDropWeaponByTag`) set `g_c267_call` while the frame object is
    a host-own `LevelScriptActor`.
  - The native detours (`AddWeapon`, `GiveWeaponByClass`) then repeat the change on every server copy of a client
    pawn (`c267_mirror_add`, `c267_mirror_give`). The card 050 `Weapons` list carries it to the client.
- `vehicles.cpp`: `detour_c132_remove_weapon` calls `c267_mirror_drop`, which removes the same weapon class from
  each copy.
- `hooks_internal.h`: the `C267Call` enum and the declarations.
- `hooks_ubw.cpp`: the four weapon hook rows are no longer gated on `g_gate_diag`; there are 3 new rows for the drop
  execs.
- `tools/symbols.txt` and `offsets.generated.h` now have the 3 drop exec addresses.
- Player-taken pickups are not doubled, because `PickupWeapon` does not run inside a level-script frame.
  `execGiveWeaponByInstance` is not mirrored: one weapon instance cannot be given twice.

## Proof (one run, scenario under "Gameplay test scenario rules" in docs/STATE.md)

Both players walk (real movement, no warp across the doorway) through door `_0` into presence `_7`; host takes
the laser rifle, then the client takes it (crosshair gate `focus=`, shots viewed); both `inventory` rows list
`BP_Weapon_LaserMorita_C`; both walk through door `_1` into presence `_8`; wave starts once at 2 of 2; client
screen shots at each doorway and with the rifle equipped.

Files: decided in step 1. Budget: 1 run.

## Senior code review (shape A, 2026-11-13, no run yet)

Attacked: the side test on a level script actor (authority, so host-own on the host only; a client's script never
mirrors), recursion (mirrors call the originals; `AddWeapon`'s inner give carries kind Add and is not mirrored
twice), nested exec calls (kind saved and restored), drop-then-add order when the copy already holds the laser
(dropped first, then added), the card 050 note (client-only, not needed on copies). No defect found.
Open, for the proof run: a client pawn that is dead or not yet spawned at the script frame gets no mirror and
respawns with its old loadout. If run C shows it, the fix is in the card 143 restore (copy the host's script
loadout on respawn), a new card.

## Run C (c240-c1m1-c1, build 3), senior verdict 2026-11-13

- Held: 9 host `card267: host level script ... mirrored to copy` rows. Door `_0`: presence `_7` `held: 1 of 2`
  at 234505722, `fired after hold: 2 of 2 players inside` at 234507091. `inventory` rows at 234513706: host-own,
  server copy and client-own all hold `BP_Weapon_LaserMorita_C`. Shots viewed by the senior: host at the weapon
  case with the training rifle, client past door `_0` with the training rifle (60/300), client past door `_1` on
  the flag course with the objective "Collect the Red Flag".
- Side rows: 2 `WARN card161` (card 270, see there); 2 `Widget_Holder_3DFrontend_C` mismatches (card 250).
- Open, not seen in this run: a client pawn dead or not spawned at the script frame gets no mirror. Card 278.
- Accepted.
