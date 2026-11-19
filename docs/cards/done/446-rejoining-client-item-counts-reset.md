# Card 446: a rejoining client's item counts go back to the starting loadout

Status: open, patch written 2026-11-16 16:41 (`c446_patch.py` in the senior scratchpad, applies on the b145 tree), for
b146. Sibling of cards 440 (weapons, ammo) and 445 (tactical charge).

## Defect

Run `c423b144b` (b144): the rejoined copy's card 356 loadout replay sends `BP_GrenadeItem_Frag_C 2` at 506802375,
the starting count, whatever the gone copy held. Nothing on the host keeps a copy's item counts across the leave: the
card 440 keep holds weapons, ammo and (card 445) charge only. A client that threw its grenades and drops gets them
back; one that picked items up loses them.

## Fix

- `grenades.cpp` `c73_relay` (the one place every item change on a server copy passes) calls
  `c446_note_item(pc, pawn, definition, count)`.
- `restore_c143.cpp`: table of up to 32 `{controller, pawn, definition, count}` entries beside the card 440 keeps,
  emptied at each map arm and load. At the card 440 adoption `c446_adopt` gives the gone controller's entries to the
  new controller and marks them owed. `c446_step` sets each owed count on the new pawn once the controller's Restore
  hold is gone (`c446_set_count`: raise by AddItemByDefinition, lower by the game's remove called direct plus
  `c73_relay`, never `detour_c73_remove`, which card 310 would apply to every player). The relay sends the client
  the absolute count. An entry whose live controller is on another marine (a respawn) is dropped.

## Done when

One run with a client grenade throw before the leave:

1. Host `card073: host ... on server copy ... GrenadeItem ... Frag_C 1` before the leave.
2. Host `card446: ... controller N takes 1 kept item count(s) of the gone controller M`.
3. Host `card446: ... kept item count BP_GrenadeItem_Frag_C 1 on ...: 2 to 1`, then the `card073` relay row with
   count 1, and the client `card073: client ... GrenadeItem ... our count 2 to 1`.
4. 0 `WARN card446`; digest 0 mismatches; 0 EXCEPTION.

## Not covered

- Health and armour: a rejoin gives a fresh pawn (run `c423b144b`: Armour 0.0 before the leave, 100.0 after). Not
  changed here.
- Item definitions the gone copy never changed are not in the table; they keep the starting count, which is what the
  gone copy had.
- Two clients gone at once: the adoption takes the newest orphan, as card 440.

## Accepted 2026-11-16 17:02

Run `c423b146a` (b146 `8852b98c1503ca7f`), scenario passed. Host: `card446 ... takes 1 kept item count(s)`, then
`kept item count BP_GrenadeItem_Frag_C 1 on BP_PlayerCharacterMI_C: 2 to 1`. Client after the rejoin: `our count 0 to 2`
(loadout), then `2 to 1`; HUD frag counter reads x1 before the leave and after the rejoin. 0 `WARN card446`, 0 EXCEPTION,
digest 0 mismatches on both sides.
