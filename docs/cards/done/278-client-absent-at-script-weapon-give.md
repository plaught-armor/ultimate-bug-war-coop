# 278 A client that is dead or not spawned at a script weapon give keeps its old loadout

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: Open item of card 267 (senior review, junior's run C report). Not seen in a run. Play-visible when it happens:
one player enters the CTF course without the laser rifle.

## Mechanism (source, `c267_copy_inventories`)

The host mirrors a level script give or drop to each client's server copy at the script frame. A client with
no live pawn at that frame is skipped with no log row, and the card 143 respawn restore gives back the loadout
from before the script frame.

## Fix spec

- `c267_copy_inventories`: log `card278: host script loadout change skipped for <pc>: no live pawn` and store the
  script's give and drop list per map (bounded table, cleared at map load).
- Card 143 restore on the host: after the restore, apply the stored list to the new copy, in order, through the
  same mirror calls as card 267. Log `card278: host applied N script loadout rows to the respawned copy`.
- Files: decided by the junior's step 1 read of the card 143 restore; report them before coding.

## Proof

Rides in the FED_C1M1 play-path run of card 224: `kill client` before the laser give, respawn after it; predicted
rows: one `skipped` row, one `applied` row, client `inventory` row with `BP_Weapon_LaserMorita_C`.

## Step 1 result (junior) and senior decision, 2026-11-13

- Correction: there is no card 143 respawn restore. `restore_c143.cpp` serves only the rejoin after a checkpoint
  load. A respawned server copy gets the level's starting inventory (`AssignStartingInventories`), so every copy
  that respawns after a script give loses the scripted weapon, not only one absent at the script frame.
- Decision: replay the stored give and drop rows on every new player character of a server copy (respawn or
  first spawn after the give). Files: `weapons.cpp`, `hooks_internal.h`, one call line in `death.cpp` (`c50_step`
  host branch). `restore_c143.cpp` untouched.
- Conditions given to the junior: occupied-slot behaviour of the give settled in disassembly before coding; a
  replayed drop leaves no world pickup; trigger is a new character object, never a possess change; a copy that
  gets a card 143 Restore is not replayed (logged); table of 16 rows with a WARN when full.

## Build 4 run c224-c1m1-b4d (FED_C1M1, DLL `5e2ce00acad8efa3`, 639 s, scenario passed, 0 ERROR, no VOID), senior verdict 2026-11-13

Held: host `card278: host replay of GiveWeaponByClass(BP_WeaponDefinition_Unarmed_C) ... skipped: the copy holds
it` and `host applied 3 script loadout rows to the respawned copy ... (8 rows kept)`. Shot `client
c278-client-rejoined` viewed: the rejoined client holds the TRAINING RIFLE (BLUE), 60/300, CaptureTheFlag
objective shown.
