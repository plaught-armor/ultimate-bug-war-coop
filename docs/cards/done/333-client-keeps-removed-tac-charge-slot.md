# Card 333: client keeps a tactical support slot the host removed

**Status:** open, queued after the playtest (not a play blocker). Found by card 327's PW row, batch 3 run 1
(`e9aa849bb15eacf0`, `logs/c310-proof-c1m1-*.log`).

**Symptom:** after the client uses the one-shot bombing run (TacSupport_SO6), its own pawn still lists the slot. The
HUD shows a charge the host copy no longer has; a press does nothing on the host.

**Rows:**
- Host `card104: host t=308283361 TacCharge ...: 20000 1 10250 1 -1 0 -1 0` (pickup), then
  `card105: host server-copy activation handle 182 ability GA_TacSupp_BombingRun_Tutorial_C path host call`, then
  `card104: host t=308287695 TacCharge ...: 20000 1 -1 0 -1 0 -1 0` (slot 2 gone on the copy).
- Client `card104: client t=308287721 TacCharge 20000 1 -1 0 -1 0 -1 0: own slots 20000 1 10250 1 -1 0 -1 0 -> 20000 1 10250 1 -1 0 -1 0`.
- Client `WARN card327: client t=308339870 digest round 6 PW differs for two rounds: host f2fd, client f9bd`.

**Cause (static, settled):** `c104_receive_charge` (`mod/src/ubw/anim_c104.cpp`) skips a slot whose host value is
negative (`if (a == nullptr || v[2 * i] < 0) continue;`). `-1 0` is `c104_charge_text`'s form for "no ability in this
slot", so a removal on the host copy is never applied to the client's own component. The same skip hides the reverse
case (`a == nullptr` on the client, a value from the host): a slot the host has and the client lacks is not added.

**Fix:** mirror slot presence, not only charge and ready. Find the game function the host copy's removal runs
(the component's remove/unequip of a slot's ability; the add side is the card 310 D row path,
`EquipTacticalSupportAbility`) and call it on the client's own component when the host text has `-1` for a slot the
client holds. For the reverse case log one WARN naming the slot (the add needs the config class, which the card 310
rows carry; do not guess it from the charge frame).

**Sibling sweep:** every mirror that applies host values per slot and skips "absent" entries: `c50` weapon slots,
`c72` ammo map (text -1 when the map has free slots), grenade item rows. Each must apply a removal.

**Proof:** `c310-proof-c1m1` through TacSupport_SO6: client row `own slots 20000 1 10250 1 ... -> 20000 1 -1 0 ...`,
no `card327 ... PW differs` WARN afterwards, client `inventory` after SO7 lists no bombing run.

## Batch 3 run 11 note (`6ce017264c0f3c81`), 2026-11-14

One PW WARN after the FED_C1M1 to FED_C2M1 travel: `WARN card327: client t=313790126 digest round 18 PW differs for
two rounds: host 8f9b, client 9ed1; client weapons 0|0=...MoritaMk1...; ammo AmmunitionType.Laser 300
AmmunitionType.Morita 240 AmmunitionType.UnderslungShotgun 8; charge 0 0 -1 0 -1 0 -1 0`. Host row 313737761
`inventory 8f9b` (text not logged). Weapons match the host (card 050 apply #8 and #9 `matches the host`), so the
difference is in ammo or charge text. Candidate, not proven: the client keeps the `AmmunitionType.Laser 300` entry of
the C1M1 training rifle across the travel and the host's new copy does not. The host row must log its weapons, ammo
and charge text beside the hash (as the client WARN does) before this is traced further.

## 2026-11-14 12:20, senior: static trace and fix (not built yet)

Cause: `c104_receive_charge` (`mod/src/ubw/anim_c104.cpp`) skipped every slot where the host value was below 0, so a
host removal ("-1 0") never reached the client's own component. The host log has no `OnAbilityUsed` row at the
bombing-run removal, so the removal does not pass through the hooked use path.

Fix: the game's own removal, `USTTacticalSupportSlot::DestroyTacticalSupportAbility`
(`?DestroyTacticalSupportAbility@USTTacticalSupportSlot@@QEAAXXZ`, RVA 0x050E3910), is now in `tools/symbols.txt` and
the pointer table as `g_c104_destroy_slot`. `c333_slot_presence` calls it on the client's slot when the host's row says
the slot is empty and the client's holds a live ability, and logs `card333: client t=... slot N: the host's slot is
empty, DestroyTacticalSupportAbility on ours`. The reverse case (the host holds one, the client does not) cannot be
created from the charge row; it logs `WARN card333: client: slot N ability on the host only` (cap 8).

Predictions for the run: (1) after the host's removal row the client logs the `card333:` line once per removed slot;
(2) the next `card104: client ... own slots` row shows "-1 0" for that slot, equal to the host text; (3) no
`WARN card333` after both players hold their loadout. Unknown until run: whether the client HUD drops the icon (the
function's broadcast is not traced); a screenshot after the removal decides it.

Still open from the card: sibling sweep (c50 weapon slots, c72 ammo map, grenade rows) and the run-11 note (PW hash
differs after travel; the host row should log weapons, ammo and charge text beside the hash).

## 2026-11-14 12:30, senior: rework after the junior's review

Two defects in the first version: (1) any host "-1" removed the client's slot, so a host frame from before the copy's
loadout would strip the client for good; (2) the host-only WARN fired before the client had a loadout. Now the destroy
needs a host live-to-empty transition for that slot (`g_c333_host`, per component, reset on a new pawn), and the WARN
is silent until the client holds a live slot. Log line: `card333: client t=... slot N: the host's slot went empty,
DestroyTacticalSupportAbility on ours`. Extra prediction: no card333 line at a map arrival or after a reconnect.

## 2026-11-14, junior: sibling sweep (static, b43 logs), batch b44

| Mirror | Host removal path | Frame to the client | Client applies a removal? | Gap |
|---|---|---|---|---|
| c50 weapon slots | A weapon leaves the copy's weapon inventory; `c50_host_poll` (weapons.cpp ~1120) rebuilds `c50_text` every poll and sends `Weapons` whenever the text changed or a new copy pawn forces it | yes, the whole list | yes: `c50_apply` runs `c50_remove_extras` once every class in the host list resolves (`want.all`) | none. One edge: a host class that does not resolve on the client keeps all extras that frame (logged), retried on the next frame |
| c72 ammo map | No game removal exists (`USTAmmunitionInventory` has no Remove or Empty). A tag disappears only when the host's new copy (respawn, seamless travel) starts without it; `c72_seed` sends `AmmoSet` with the new copy's present pairs only | yes, the present pairs | no: `c72_take_set` / `c72_apply` set listed pairs and never clear an absent tag | **yes**, below |
| grenade item rows (c73) | `RemoveItemByDefinition` on the copy: `detour_c73_remove` then `c73_relay` sends the absolute count, 0 included | yes, per change | yes: `c73_apply` lowers with `detour_c73_remove(..., want == 0)` | none at runtime. At a new copy `c73_send_all` (card 356) sends present items only, but the client's new pawn starts with no items (card 354 skips its own ResetInventory adds), so nothing stale is left. b43r2: host replay `1 of 1 items` Frag 2 at C2M1 arrival |

### c72 gap, b43r2 rows (run-11 note explained)

- C1M1: the training loadout gives `AmmunitionType.Laser 300` on both sides (host copy row 343902051, client
  `AmmoSet "AmmunitionType.Laser 300"` 343902259).
- FED_C2M1, host: the new copy holds `Morita 240 UnderslungShotgun 8`, no Laser (card327 row 343921816); AmmoSet
  sent with those two.
- FED_C2M1, client: the new own pawn already holds a Laser entry before the host's AmmoSet: the AmmoSet at
  343923425 sets Morita and UnderslungShotgun `0 to` their counts, yet the next AmmoMirror (343923509) lists
  `Laser 300` first (insertion order). No mod row writes Laser in between, so the game wrote it on the client.
- The host's `c72_mirror_apply` then adds Laser 300 to the copy (`AmmoMirror ... Laser 0 to 300`, host row
  343923542; copy row 343923576 now has three tags). The stale client tag spreads to the host: both sides end equal
  (no PW WARN in b43r2), but the host's copy holds ammo the host's game never gave it, and card 143's Restore carries
  it into the next copy (client AmmoSet 343979817 re-adds Laser to the reloaded pawn that had none).
- Run 11's PW WARN is the window before the mirror lands.

Fix options checked and rejected:
- Client sets an absent tag to 0: the entry stays (`Laser 0`), and `c327_inventory_hash` hashes that pair while the
  host has none, so PW differs for good. The hash is not edited.
- Manual TMap removal on the client: the sparse array gets a free slot, `c72_map_text` then returns -1 and the hash
  loses the ammo part; the hash chain must be relinked by hand. Not safe.

Next: find the client-local writer. b44 runs with `UBWCOOP_C69_LOG=1` on both sides (AddAmmo / SetAmmoCount /
ResetInventory rows with caller).

Predictions (b44):
1. Client, FED_C2M1, after its own pawn row and before the AmmoSet row: one `card069: client AddAmmo` or
   `SetAmmoCount` row with tag `AmmunitionType.Laser` on `BP_PlayerCharacterMI_C`, naming a caller. If there is none,
   the entry does not come through either function (the inventory object is carried or written directly), and the
   fix is a different class; I report the rows instead of guessing.
2. Host, FED_C2M1: no Laser row for the client's copy; whether the host's own pawn gets one tells if the game carries
   training ammo into C2M1 at all.
3. Fix shape if (1) names a caller: the card 353/354 pattern, skip that caller's write on the client's own pawn in a
   client session (the host's AmmoSet is the writer), and the host's mirror apply refuses a tag the copy's map
   lacks. Built only after the row; not in b44.

### b44 run 1 (b43 DLL 8f7d7fc43a76c451, C69 log on): prediction 1 held

- Client FED_C2M1 344701491: `ResetInventory #1` (caller exe+0x51BE49A), inside it `AddAmmo AmmunitionType.Laser 300`
  caller exe+0x51DA71D (ResetInventory +0x2ed, the loadout ammo loop), then the host's AmmoSet at 344703164 adds
  Morita and UnderslungShotgun beside the stale Laser.
- Host FED_C2M1 344700030 and 344700055 (own pawn and the client's copy): the same caller adds
  `Morita 300 (ret 240)` and `UnderslungShotgun 16 (ret 8)`, no Laser. So the game does not carry C1M1 ammo into C2M1;
  the client's reset reads its own stale loadout (the last map's) after the seamless travel.
- The call site discards the result (`call AddAmmo; mov 0x54(%rsp),%eax`).

### Fix (b44 build d875ca838402bfc5)

Card 354's one-writer rule, for ammo. `USTAmmunitionInventory::AddAmmo` moves from the C69-only install to kHooksUbw
(always on); `detour_c69_add` keeps its card 069 rows behind `g_c69_log`. Client session, own pawn
(`c354_own_local`), caller exe+0x51DA71D: the add is not run, row
`card333: client t=... own loadout AddAmmo <tag> <amount> (caller exe+0x51DA71D): local add not run, count N; the
host's AmmoSet writes it` (cap 20). Every other AddAmmo caller (card 071 pickup credit, Blueprint grants) runs as
before. No host change: with no stale tag on the client, the mirror has nothing extra to add.

Predictions (b44 run 2):
1. Client: one `card333: ... own loadout AddAmmo AmmunitionType.Laser 300` row at each C1M1 reset and one at the
   C2M1 reset; no `card069: client AddAmmo ... caller exe+0x51DA71D` row.
2. Client C1M1: AmmoSet `Laser 0 to 300` (host's writer). Client C2M1: the first AmmoMirror lists Morita and
   UnderslungShotgun only; no Laser on the host copy's card327 row afterwards; no host `AmmoMirror ... Laser 0 to 300`.
3. 0 `WARN card327 ... PW differs` on the client after C2M1 arrival.

### Run b44r2 result (build d875ca838402bfc5, drive rc 0, scenario passed, no crash)

1. Prediction 1, partly as written: one `card333: client t=345076204 own loadout AddAmmo AmmunitionType.Laser 300
   (caller exe+0x51DA71D): local add not run, count 0` row at the FED_C2M1 reset of the client's own pawn (its
   AssignStartingInventories ran with chapter 0 level 0, the stale loadout). On FED_C1M1 the client's own pawn had no
   AddAmmo from that caller at all, so no row there. The only unskipped `card069 ... caller exe+0x51DA71D` rows
   (345079063, Morita 300 and UnderslungShotgun 16) are on inventory 2A27D180 of the host's pawn copy on the client
   (`AssignStartingInventories on BP_PlayerCharacterMI_C (other)`, chapter 1 level 0), the same loadout the host
   gives its own pawn: not the client's own pawn, values equal to the host's, no gap.
2. Held: client AmmoSet Morita 0 to 240 and UnderslungShotgun from the host frames; the client mirror carries
   `Morita 240 UnderslungShotgun 8` only; no host AmmoMirror Laser row; the host copy's card327 row has ammo
   Morita 240 and UnderslungShotgun 8; 0 `WARN card327` on either side. The client HUD (`oob-client-in`) shows
   Morita Mk1 60 / 240.
3. Held: 0 PW WARN. WARN classes against b43r2: host adds one `card132` hit 1160 uu from the NPC (right after the
   host's warp into the out-of-bounds volume); client has one `card191` frozen guid 0x322 and lost b43r2's card132,
   card140, card152 and card177 rows. No new class from the fix.

## Closed 2026-11-16 (senior)

The three predictions held in the b44 batch (section above), and the b168 runs `c448b168a` and `b40b168a` have 0
`WARN card327` on host and client. A playtest log with a `WARN card327` PW row after a travel reopens the card.
