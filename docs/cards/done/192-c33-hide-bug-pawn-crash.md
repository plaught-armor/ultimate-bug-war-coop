# Card 192: Travel to a bug-side map crashes both games in the card 033 hide

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: queued 2026-11-12, found in c177-r1c. Budget: three runs. Static first; runs with card 177's next run.

## Why

c177-r1c, `travel next` from FED_C2M1 to a bug-side map: both games crashed 120 ms after the travel (host
102523246, client 102523309). Fault exe+0x4A2BD22 reading 0x4000000EB; stack ubwcoop+0x477F2 = `c33_hide`
(`mod/src/ubw/remote_body.cpp:692`): `g_get_equipped_weapon(read_ptr(pawn, kMiWeaponInventory))`. The remote pawn
is BP_PlayerCharacterBug_C, which is not the Mobile Infantry pawn class, so the offset reads garbage and the
non-null garbage is passed to the game.

## Do

1. Trace, no run: the class that owns `kMiWeaponInventory` and `kPawnFirstPersonMesh`, and how the mod can tell a
   pawn is of that class (a class pointer compare or IsA walk already in the tree; never a read of the field
   itself). Then the sibling sweep: every read of an MI-pawn-only offset (`kMi*`, `kPawn*` that the bug pawn class
   lacks) on a player pawn, in every file, listed in the card with file:line and a verdict (guarded, or needs the
   guard).
2. One guard function, used at every listed site. A bug pawn takes no MI-only path; each skipped feature logs once
   per map (capped) which pawn class it skipped.
3. Proof: the travel to the bug-side map at the end of the card 177 scenario, both games alive 30 s after LoadMap
   returned, both pawns possessed, no crash dump.

## Done when

- The sweep table, the guard, the run lines. Out of goal: making remote-body features work for the bug pawn
  (list what is skipped under "Problems for the senior"; it becomes its own card). Ends with "Report complete."

## Trace (junior, 2026-11-12)

Class: ASTPlayerCharacterBug and ASTPlayerCharacterMI both derive from ASTPlayerCharacter (card 030).
`kPawnFirstPersonMesh` (0x848) is an ASTPlayerCharacter field, valid on the bug pawn. `kMiWeaponInventory` (0xA40)
and `kMiAmmoInventory` (0xA48) are ASTPlayerCharacterMI fields; on BP_PlayerCharacterBug_C those offsets hold
other data, which `c33_hide` passed to the game (the crash). The tree already tells the class with
`is_a(pawn, g_player_mi_class())`; the guard uses that, never a read of the field.

Guard: `c192_mi_pawn(pawn, feature)` in `mod/src/ubw/remote_body.cpp` (declared in `hooks_internal.h`): null gives
false, an MI pawn true; any other pawn false and, the first time per map for that feature, the line
`card192: <side> skips <feature> on <pawn> (<class>): not an ASTPlayerCharacterMI, no MI inventory (once per map)`;
repeats are counted (8 features, the rest as unlisted). At map arm, `c192_arm_map` logs
`card192: <side> map totals: <feature> skipped N times` for each feature seen, then resets.

## Sweep: every MI-only read on a player pawn

| Site | Read | Verdict |
|---|---|---|
| remote_body.cpp:692 `c33_hide` | kMiWeaponInventory | needed the guard (the crash), guarded "c33 weapon hide" |
| hooks_ubw.cpp:566 `c24_inventory` | kMiWeaponInventory | needed the guard, guarded "c24 weapon inventory" (turret path unchanged); covers callers fire.cpp:146/176/247/376, death.cpp:1737, vehicles.cpp:465, enemies.cpp:710/731/1175/1251, marker_c166.cpp:360 |
| interact.cpp:191 `c50_interact` | kMiWeaponInventory | needed the guard (caller death.cpp:2914 `c50_relay_use` has no class check), guarded "c50 Use relay inventory"; `c50_text` handles null |
| vehicles.cpp:402 `c29_equip` | kMiWeaponInventory | needed the guard (test stage, callers vehicles.cpp:491/494), guarded "c29 test equip" |
| attributes.cpp:655 | MI-only | guarded (mi flag) |
| sprint.cpp:916 | MI-only | guarded (mi flag) |
| ammo.cpp:208, 253, 369, 418/419 | kMiAmmoInventory | guarded (is_a) |
| restore_c143.cpp:59/63 | inventories | guarded (caller :84 `c143_send_reload` is_a) |
| restore_c143.cpp:122/125 | inventories | guarded (caller :234 ready = is_a) |
| playlog.cpp:254 | inventory | guarded (`g_pl.mi`, set by is_a at :229) |
| weapons.cpp:241 | inventory | guarded (caller :319 is_a) |
| weapons.cpp:663 | inventory | guarded (is_a) |
| weapons.cpp:827 | inventory | guarded (caller :879 is_a) |
| weapons.cpp:1269 `c141_equipped` | inventory | guarded (callers :1296, :1325 is_a) |
| weapons.cpp:1758 `c72_seed` | inventory | guarded (caller :681 in `c50_host_poll`, after the is_a at :663) |
| testrun.cpp:1687 | inventory | guarded (is_a) |
| vehicles.cpp:221 | inventory | guarded (caller :311 is_a, pawn class MI) |
| hooks_ubw.cpp:1046, 1185, 1449 | inventory | guarded (is_a) |
| death.cpp:3014 | inventory | guarded (is_a) |
| remote_body.cpp:691, fx_c136_net.cpp:191, spawn_c169.cpp:137 | kPawnFirstPersonMesh | no guard needed (base-class field) |
| fire.cpp:536 | kMiTargetIface | no guard needed (pointer arithmetic from the interface, no read) |

Four sites needed the guard (under the 15 limit). No skip changes host-visible behaviour: before, the bug pawn
read garbage or crashed; the Use relay still relays, only its inventory text is skipped.

Prediction (c177-r2 part 4): after `travel next`, host and client `UEngine::LoadMap returned true` (client twice:
the entry map, then the bug map, then `card045: client t=.. reconnected (N)`), both pawns possessed
(`host wait state pawn=host-own`, `client wait state pawn=client-own`) before and again 30 s later, no crash dump,
and on the bug map `card192: <side> skips c33 weapon hide on .. (BP_PlayerCharacterBug_C)` at most once per side.

### Problems for the senior

- Remote-body features skipped for the bug pawn (its own card): the card 033 weapon hide, card 024 weapon
  inventory lookups (fire, death, vehicle, enemy, marker callers get null and skip), the card 050 Use relay
  inventory text, and the card 029 test equip.

## Run c177-r2 results (junior, 2026-11-12, DLL 583a464f36f76e1f)

PASS. After `travel next`: host `UEngine::LoadMap returned true` (104157389), client `card045: client t=104160366
reconnected (2): try 1, 7872 ms after the drop`; both pawns possessed (`state pawn=host-own`, `state
pawn=client-own`) before and 30 s after; quit by the scenario, exit 0, no crash dump. Skips, once per side:
`card192: host skips c33 weapon hide on BP_PlayerCharacterBug_C (BP_PlayerCharacterBug_C): not an
ASTPlayerCharacterMI, no MI inventory (once per map)` and the same on the client. Only the c33 hide was reached.
The client's map totals at quit said `card192: host map totals: c33 weapon hide skipped 296 times` (the session flag
is clear at map end); after the run the side is latched at each skip (`g_c192.side`), build f1c6127cacc511b1, not
run.

## Senior review (2026-11-12)

Accepted on c177-r2: both games alive after the bug-side travel, one "skips c33 weapon hide on
BP_PlayerCharacterBug_C" line per side, no crash. The four guarded sites call `c192_mi_pawn` only. The totals side
label fix (`g_c192.side` latched) is in tree build `f1c6127cacc511b1` and has not been run. Follow-ups: the card 138
hold never releases on the bug pawn (card 195); the four skipped features on the bug pawn (card 196).
