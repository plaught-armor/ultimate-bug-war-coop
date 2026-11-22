# Card 440: a rejoining client loses its picked-up weapons, and the pickups it took stay gone

**Closed 2026-11-16 18:55 (senior): fix is in the build (b140 or earlier: `c423b140a` line 506 passed) (host keeps the gone controller's inventory and gives it to the new pawn as a Restore). Proof: run `c423b144b` (b144 `b09caa0e7c1f80da`, FED_C2M1), inventory equal before the leave and after the rejoin, weapons and ammo; `c423b146a` host row `card440: ... takes the kept inventory of the gone controller ... weapons 2`. The status line below is older than the build. Siblings: cards 445 (charge) and 446 (item counts), both done. Limit: health and armour come back full.**

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: coded as scratchpad patch `c440_patch.py` (senior, 2026-11-16 14:19), not built, proof owed. Found 2026-11-16 in run `c423b135b` (b135 `18be6a2b9157e5d5`). After the playtest unless a
play log shows it.

## What the logs show

- 497616044 client takes the placed `BP_WeaponPickup_NukeLauncher_C_0`: `GiveWeaponByClass(BP_WeaponDefinition_NukeLauncher_C)`
  on client-own (client log) and on the server copy (host log 497616060), weapons 2.
- 497799433 the client rejoins FED_C2M1. Both sides run `AssignStartingInventories ... weapons 1: Weapons.MoritaMk1` on
  the new pawn; host `card327 ... player row` for the new copy pc 260941 lists the Morita only.
- 497803455 the join replay applies world row E for the pickup (taken), and the +10 s probe shows it disabled
  (`bEnabled 0 ... mesh visible 0 ... collider collision 0`). Correct world state, but the launcher is now nowhere.
- The scenario's second approach to the pickup then found nothing to aim at (`aim scan ... hits 0 of 26`) and voided
  at line 540. The junior moved that scenario block before the rejoin; the scenario is not the bug.

## Cause

The new pawn of a rejoining player gets the map's starting inventory on both sides. Nothing carries the weapons,
magazine and reserve ammo the player held when it left. Card 327's player row only hashes the current inventory for
the digest; it restores nothing. Placed pickups are single use (`DisablePickup` on the first take) and their taken
state is kept (card 056), so a weapon taken before a disconnect is lost for the rest of the map. C2M1 has 4 placed nuke
pickups (`host tally BP_WeaponPickup_NukeLauncher_C ... n=4`), so one loss does not block the nest objectives; a map
with one required pickup would be blocked.

## Fix (design, to confirm by a static trace before coding)

1. Host keeps one snapshot for the remote player per map: the weapon definition paths, per-weapon equipped ammo and
   reserve ammo pairs that `c327_inventory_hash` already builds as text (`weapons.cpp`), refreshed when the card 327
   row changes, kept when the copy PC goes away, cleared at map arm (`sprint.cpp` next to `g_c50.c327_rows = 0`).
2. On the first card 327 row of a new copy PC in the same map with a snapshot present: every definition in the
   snapshot that the new soldier lacks is given by `GiveWeaponByClass(def class, slot -1, equip 0)` on the server
   copy, then ammo set to the snapshot values (`g_set_equipped_ammo`, `g_add_ammunition`; subtract what the starting
   inventory already gave).
3. The client must get the same weapons: the pickup path gives on each side by its own local call, so a host-only give
   leaves the client without the weapon. Step 1 of the work is to find which existing op makes the client give a
   weapon by definition (card 021 grant path, card 143 checkpoint restore `restore_c143.cpp`); reuse it, no new receiver
   if one applies.
4. Not restored: mech seat, turret seat (the player rejoins on foot at the join point).

## Done when

One run: client takes the nuke launcher, fires one round, disconnects, rejoins; host logs
`card440: host t=... restored N weapon(s) to <copy>` and the next digest round shows `PW` equal on both sides
(0 mismatches) with weapons 2 in both `inventory` rows and the reserve ammo equal to the value at the disconnect.

## Static trace and final design (senior, 2026-11-16 14:19)

The trace replaced points 1 to 3 of the design above. Card 143 already does the whole apply on the host:
`c143_host_step` applies a held Restore text to a new copy after its `AssignStartingInventories`
(`c143_apply_inventory`: extras removed, missing given, ammo map set, card 072 `AmmoSet` seeded to the client), and
card 050 sends the copy's weapon list to its client. Point 3 was wrong: no client-side give is needed.

What the patch adds (host only, 7 edits in `restore_c143.cpp`, `weapons.cpp`, `sprint.cpp`, `hooks_internal.h`):

- `c440_keep(pc, weapons, ammo)` from `c327_row` on each row change: keeps the copy's weapon list and ammo map texts
  per controller (4 entries). No new per-frame work: the texts are the ones the row already built.
- A controller with no entry, when another entry's controller is gone from the world: it takes the newest such entry
  as a held Restore `-1.0;<weapons>;<ammo>`. A negative Health means "no Health": `c143_apply` then leaves Health
  alone and skips the card 198 read-back. Armour is not carried.
- Kept entries are cleared at map arm and at a checkpoint load in place (Reload and Restore carry that state). A
  Restore from the client already held for the controller wins.

Known limits, by design:

- No player identity is read. With two clients gone at once the first to rejoin takes the newest entry; a new
  player's first join takes the entry of a player who left.
- A rejoin while the old controller is still in the world (a client crash, before the host times the connection
  out) restores nothing; no `card440` row is logged then.
- Script loadout rows (card 278) given while the player was away are not replayed on a restored pawn (as card 143).
- Health and Armour are the new pawn's (a rejoin heals). Mech and turret seats are not restored.

## Done when (replaces the list above)

One run, client takes the nuke launcher, fires one round, disconnects, rejoins:

- host `card440: host t=... takes the kept inventory of the gone controller N ..., held as a Restore: weapons <two
  definitions>`;
- host `card143: host t=... Restore applied ... after its AssignStartingInventories ... weapons restored` with no
  `WARN` prefix, and `card143: host weapons from Restore: removed 0, given 1, matches the list`;
- the next host `card327 ... player row` for the new copy lists 2 weapons and the next digest round has `PW` equal
  (0 mismatches);
- client screenshot with the launcher in the weapon wheel or held.
