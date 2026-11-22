# Card 208: A bug-form switch shows on the other machine

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: queued 2026-11-12, split from card 196's trace. Budget: three runs. Static first. After card 196.

## Why

Card 196's trace: the bug pawn's forms are AADWeapons in `FormsInventory` (+0xA30, a UADWeaponInventory), switched
by the base `SendWeaponSwitchRequest` (0x051DB320). A client's switch frame is dropped on the host at
`c57_receive_equip` / `c141_receive_switch` ("no server copy pawn"), and `c141_equipped` reads `kMiWeaponInventory`.
So the host's copy of the client's bug stays in the old form (wrong look, wrong attack abilities), and the host's
own form switch is not shown on the client.

## Do

1. Trace the c57 / c141 gates (`weapons.cpp:1138`, `:1294`, `:1325`, `:1334`) and `c141_equipped` against the bug
   pawn; read the equipped form through `c24_inventory` (card 196 makes it MI or bug).
2. Relay the switch both ways for the bug pawn in the owner of the weapon switch; no second path.
3. One bug-map run: each side switches form; the other side logs the copy's equipped form by name, and an attack
   after the switch uses the new form's ability.

## Done when

- Trace, fix, run lines; the digest round after the switches is at 0 mismatches. Ends with "Report complete."

## Carried from card 196 (senior, 2026-11-12)

- Same build: add `!g_c43.in_kill` to the god test in `c43_death`'s HandleDeathEvent detour (`death.cpp`, the
  "god host HandleDeathEvent ... skipped, Health back to max" branch), so `host down host` under the test god ends in
  "card043: host real death: N hits, Health 0.0" with no WARN. c196-r2 host line 4696 shows the miss.
- The run reuses c196-r2.txt's body (travel to BUG_C1M1, downs at the end) with the form switches added.
- `weapon_pawn` (card 196) is the form-to-pawn link; do not read a form's `AActor::Owner`.

## Trace (junior, 2026-11-12)

Static only: disassembly (dis.py), the extracted assets (iox.py: IMC_Bug_KBM, GA_BugForm_SwitchAbility,
GA_BugForm_EquipAbility, GA_WeaponSwitchAbility, BP_PlayerCharacterBug) and logs/c196-r1, c196-r2. No build, no run.

### 1. The game's form switch path

- Input: IMC_Bug_KBM maps `IA_SwitchWeapon_Slot1/2/3` (display names SwitchBugForm1/2/3, rebind keys
  REBIND_SWAPBUGFORM1/2/3) to the keys `One`, `Two`, `Three`. The bug context has no Next, Previous or Underslung
  action. The same IA assets as the MI context feed the base switch handler.
- `ASTPlayerCharacter::EquipWeapon` (0x051C57A0) is `SendWeaponSwitchRequest(index, 0)` through vtable +0xAA8.
  The bug pawn does not override `SendWeaponSwitchRequest`. The base one (0x051DB320) returns early if vfunc +0x9E0
  is true (a blocked test; not named). Otherwise it builds the request with `ConstructWeaponSwitchRequest(a, b, this)`
  (0x051C28A0, hooked by the mod as `detour_c57_request`). Then it calls
  `UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(this, WeaponSystem.SwitchWeapon, data)`.
- There is no dedicated server RPC. The switch is a gameplay event on the pawn's own ASC. It travels to the host
  only if the triggered ability is predicted, as `ServerTryActivateAbilityWithEventData`. For GA_WeaponSwitchAbility,
  card 057 found that this RPC carries target data without NetSerialize, which is fatal. Card 057 therefore forces
  the switch CDO to LocalOnly on the client and relays the index as a ChanOp::Equip frame instead.
- On a bug pawn the event triggers `GA_BugForm_SwitchAbility` (a Blueprint, 9.7 KB). It is not GA_WeaponSwitchAbility.
  Its graph reads NewIndex/OldIndex and `CanSwapToWeapon`, and it tests the owner's tags `BugForms.Can.Tanker`,
  `BugForms.Can.Hopper`, `BugForms.NoCharge` and `PlayerAbilities.InProgress.Taunting`. It applies
  `GE_BugForm_TransformingEffect` and updates the bug HUD (WB_Bug_Form_ChargeBar_Vertical, crosshair). Then it sends
  a gameplay event (WeaponSystem.EquipWeapon) to `GA_BugForm_EquipAbility`. That ability gets the inventory
  (`USTWeaponUtilityLibrary::GetWeaponInventory`, which is FormsInventory on a bug pawn) and calls
  `UADWeaponInventory::EquipWeaponByInstance`. It plays a Delay or transform animation (`PostAnimationDelayTime`,
  `FormIn`) and holds `HealthAndDamageSystem.Invulnerable` while it runs.
- Neither switch ability Blueprint sets `NetExecutionPolicy`: the name is in neither name map. Both inherit their
  native parent's default. The parent of GA_BugForm_SwitchAbility is not resolved, because zen imports are hashes.
  It has no OnBeginWeaponSwitch, so it is probably not USTWeaponSwitchAbility.
- What changes on the pawn: FormsInventory's equipped weapon changes to the new STBugForm (an AADWeapon).
  `ASTBugForm::OnEquip` (0x05244E20) runs `AADWeapon::OnEquip`, which grants the form's owner-equipped abilities.
  Card 073 logs these grants, for example `BP_BugForm_MeleeAbility_Warrior_C` and
  `BP_BugForm_VampirismAbility_Warrior_C`. OnEquip then reads the form's USTBugFormDefinition and writes pawn
  +0x750/+0x758 from the definition (+0xC0/+0xC8) or the character data (+0x50/+0x58). These look like mesh/anim
  assets. It also starts a camera shake (+0xD0). The rest of OnEquip was not read. Scale is not seen to change.
- What replicates natively: nothing for the forms. Card 132 reports
  `host inventory (map load) class STBugForm n=3 ... mechanism=none:3`. Each machine spawns its own STBugForm actors,
  so a switch on one machine never reaches the other.

### 2. The mod's relay as it is (MI)

- Client: `detour_c57_request` (weapons.cpp:1204). It has no MI gate. On `g_client_session`, it calls
  `c57_set_policy(LocalOnly)`. On `kSideClientOwn && !g_c50.applying`, it sends either `actor == pawn && a >= 0` →
  `c57_send_equip` (ChanOp::Equip, chan_to_host) or `c141_send_switch` (ChanOp::Switch). Bug slot keys give
  `a` = slot index and `actor == pawn`, so a bug switch takes the c57 Equip path. The c141 path (Next/Previous,
  Underslung) has no bug binding.
- `c57_set_policy` (weapons.cpp:1110) acts only on `kC57SwitchCdo` (GA_WeaponSwitchAbility_C, :1105).
  GA_BugForm_SwitchAbility_C keeps its native default policy on the client. **Risk:** if that default is
  LocalPredicted, a client bug switch sends `ServerTryActivateAbilityWithEventData` with the same switch target data.
  That is the card 057x fatal NetSerialize path. This is unverified, because no run has had a bug switch.
- Host: `c57_receive_equip` (weapons.cpp:1136) tests `is_a(pawn, g_player_mi_class())` at :1138, so a bug server
  copy fails. It logs `WARN card057: host t=<t> Equip <i> from <pc>: dropped (no server copy pawn)`. If the test
  passes, the host runs `c57_set_policy(ServerOnly)` and `g_equip_weapon(pawn, index)`, which is the base EquipWeapon
  → vtable → base SendWeaponSwitchRequest. That works on a bug pawn, but the event triggers GA_BugForm_SwitchAbility,
  whose policy `c57_set_policy` does not set.
- MI-only tests in c141: `c141_equipped` reads `kMiWeaponInventory` (≈:1268), `c141_receive_switch` (:1294) and
  `c141_step` (:1325) test the MI class, and `c141_pawn_of` (:1334) tests MI and falls back to weapon_pawn. None of
  these are on the bug path, because the bug has no c141 input.
- Host → client: no relay exists. ChanOp::Equip is client → host only. A host-own switch, and a client switch
  applied on its server copy, are never sent to the other clients. For MI, the weapons replicate natively. For bug
  forms they do not (mechanism=none).
- The log is also MI-only: `pl_weapons` (playlog.cpp:253-265) reads `kMiWeaponInventory` and only for `g_pl.mi[s]`.
  So `playlog: ... weapon slot` never prints for a bug pawn.

### 3. What c196-r1 and c196-r2 show

- No `card057`, `card141` or `card062` lines, and no `no server copy pawn` line. No switch was pressed in either run.
- Host: `card132: host inventory (map load) class STBugForm n=3 ... mechanism=none:3`. Spawns of STBugForm (r2 host
  3439/3440, then 3925/3926), each logged as `STBugForm` or `BP_BugForm_C`. Client: spawns at 3121 and 3133.
- Equipped form per copy: only the Warrior form's abilities are granted or activated, on both sides. Examples:
  `card073: client t=... grant BP_BugForm_VampirismAbility_Warrior_C bound to replicated spec h180`,
  `playlog: client t=... ability slot 0 client-own: BP_BugForm_MeleeAbility_Warrior_C`, and
  `playlog: host t=... ability slot 1 server-copy: BP_BugForm_MeleeAbility_Warrior_C`.
- The logs cannot show which three forms each inventory holds, or which form index is equipped. All form actors
  print as `STBugForm` with no _N suffix and no definition name. They also cannot show whether
  `BugForms.Can.Tanker` or `BugForms.Can.Hopper` is on the start pawn.

### 4. The input and the second form

- Key: `Two` (IA_SwitchWeapon_Slot2, SwitchBugForm2), and `Three` for form 3. `One` returns to form 1. The test
  driver already presses `Two` (c142-r4 uses `client key Two`).
- Second form on BUG_C1M1: **not proven.** Each inventory holds three STBugForm actors (n=3), and three definitions
  exist (BP_BugFormDefinition_Warrior, Tanker, Hopper). But GA_BugForm_SwitchAbility tests `BugForms.Can.Tanker` /
  `BugForms.Can.Hopper` on the owner before a swap. Where those tags are granted (campaign progress, mission, or
  character data) is not found statically. If BUG_C1M1's start pawn lacks them, Two and Three do nothing and the
  card needs another map or an unlock. That is the senior's decision.

### 5. The visible result on the other machine

- On the copy, the other machine must show three things: (a) FormsInventory's equipped form is the new STBugForm;
  (b) ASTBugForm::OnEquip has run there, so pawn +0x750/+0x758 (the form's mesh/anim set) and the owner-equipped
  abilities match, and the attack after the switch uses the new form's ability; (c) the transform itself:
  GE_BugForm_TransformingEffect, the FormIn animation and the camera shake on the owner only.
- The remote body is card 028's sprite body (c28_drive on the variant animator). Whether c28_drive picks the form's
  sprite variant from +0x750/+0x758 or from the equipped form is not traced. If it reads neither, the body does not
  change even when the copy's form does. **Unknown.** It needs a receiver-side shot.

## Fix goals found

The trace shows these goals. They all sit in the weapon switch relay (weapons.cpp c57), but they are separable:

1. **G1, client → host:** c57_receive_equip accepts a bug server copy (the class test goes through
   `c196_inventory_at(pawn) != 0` instead of the MI class).
2. **G2, policy:** c57_set_policy sets both switch CDOs (GA_WeaponSwitchAbility_C and GA_BugForm_SwitchAbility_C).
   This keeps a client bug switch from taking the 057x fatal RPC. Without it, G1 may crash the client the first time
   it presses Two.
3. **G3, host → client:** a new direction. The host sends the copy's new form index (pawn id + index) to the client,
   and the client equips it on its copy of the host pawn, under `g_c50.applying` so it is not sent back. This covers
   the host's own switch, and a client switch applied on a server copy for other clients. The card's Do 2 ("both
   ways") includes it.
4. **G4, log:** pl_weapons reads `c24_inventory` instead of `kMiWeaponInventory`, so the equipped form prints per
   copy. A form's name is not unique, so the line should also carry the form's definition class.
5. **Carried:** c43_death's god test (death.cpp:604) gets `!g_c43.in_kill`.
6. **Open, not a fix:** the second form may be locked on BUG_C1M1 (§4), and the sprite body may not follow the form
   (§5).

Per the boundary ("more than one fix goal: list them and stop"), there are no predictions or scenario table yet.
G1+G2+G3 are one relay owner in both directions, which is the card's Do 2, plus G4 for the log. The proposal is
ready to write once the senior picks the scope and answers §4.

## Proposal (scope from the senior: G1+G2+G3+G4 plus the carried c43_death fix, one build)

Fix sites:

- **G1** `weapons.cpp` c57_receive_equip: the server-copy test is `c196_inventory_at(pawn) != 0 &&
  sprint_side(pawn) == kSideServerCopy`, so a bug server copy takes the Equip frame like an MI copy.
- **G2** `weapons.cpp` c208_set_policy, called first from c57_set_policy: it sets NetExecutionPolicy on
  `Default__GA_BugForm_SwitchAbility_C` and `Default__GA_BugForm_EquipAbility_C`, looked up on every call (a miss is
  silent, the bug classes load only on a bug map). Line: `card057: <side> GA_BugForm_SwitchAbility_C
  NetExecutionPolicy N -> M` (same for the Equip CDO). The Equip CDO has the same exposure: the switch ability sends
  WeaponSystem.EquipWeapon to GA_BugForm_EquipAbility, which runs on the same pawn with the same default policy, so
  leaving it alone would only move the 057x RPC from the switch ability to the equip ability. It is treated the same.
- **G3** `weapons.cpp` c208_* block, `death.cpp` c50_step calls c208_step, `chan_ops.*` + `ledger_c132.cpp` add
  ChanOp::BugForm (args: self flag, form index). The host tracks each bug pawn's equipped index in a 5-row table
  (pawn id + index, no pointer across frames), 200 ms step. On a change it sends BugForm to the owner of a server
  copy (self=1), or to every client for the host's own pawn (self=0); other clients are skipped and counted. The
  client resolves self to its own pawn and 0 to the host pawn, and equips the form through
  EquipWeaponByClass(inventory, definition) under g_c50.applying. A bad index or a missing bug pawn is dropped with a
  capped WARN. Join replay owner: `weapons.cpp` c50_join_replay ends with c208_join_replay(pc), one row per bug
  pawn. The table is cleared at map end (pl_map_end → c208_map_end), with peak against cap logged.
- **G4** `playlog.cpp` pl_weapons reads c24_inventory per player and prints `playlog: <side> t=N weapon slot S
  <sprint side>: <old> -> <new> (<definition class>)`.
- **Carried** `death.cpp` c43_death's god test ends `&& !g_c43.in_kill`.

New log lines:

- `card208: <side> bug forms of <pawn> (<sprint side>): tags <BugForms.* list or none>, forms <k=definition class,
  ...>, equipped <index>` (once per pawn per map, first tick with a non-empty form list)
- `card208: host t=N form I of <pawn> (<sprint side>) equipped|join replay: BugForm sent to C client(s), K skipped
  (another client's copy)`
- `card208: client t=N BugForm I for the own pawn|host pawn <pawn>: EquipWeaponByClass(<def>), equipped A -> B`, or
  `...: already equipped`
- `WARN card208: client t=N BugForm I for the <who> dropped: <reason> (F forms)` (capped 8)
- `card208: <side> map end: bug form rows R, peak P of 5, rows skipped K`

Known limits: the client's own switch comes back from the host as a self row (a brief double equip on quick presses
is possible). A client cannot address another client's copy; those rows are skipped and counted. pl_map_end also runs
from pl_map after LoadMap returns, so map end may print twice and the possess line may print once per call.

## Run c208-r1 predictions

Index meaning: One is form 0 (Warrior), Two is form 1. Predictions name these values; waits use `[1-9]` / `0`.

- **P1** each side prints the card208 possess line for both bug pawns (host: host-own, server-copy; client:
  client-own, other). Each side prints its `card057: ... GA_BugForm_SwitchAbility_C NetExecutionPolicy` line on its
  first switch.

Branch A, tags present (the line lists BugForms.Can.*):

- **P2** client Two: client `card057: client t=N switch to weapon 1: Equip sent`; host `card057: host t=N Equip 1
  from <pawn>: EquipWeapon on the server copy`; host `card208: ... form 1 of <pawn> (server-copy) equipped: BugForm
  sent to 1 client`; client `card208: ... BugForm 1 for the own pawn ...`; client weapon line to a non-Warrior
  definition.
- **P3** the attack after it: host relay press, then `playlog: host t=N ability slot S server-copy:
  BP_BugForm_<non-Warrior>..._C`.
- **P4** host Two: host `form 1 of <pawn> (host-own) equipped: BugForm sent to 1 client`; client `BugForm 1 for the
  host pawn ...: EquipWeaponByClass(<def>), equipped 0 -> 1`; the host attack logs a host-own non-Warrior ability.
- **P5** One on each: the same lines for form 0, and the client host-pawn line `equipped 1 -> 0`.

Branch B, tags absent (the line says `tags none`): the switch is refused by the game with no mod WARN and no crash;
either no Equip frame reaches the host, or a host line names the refusal. The run stops after this run.

- **P-last** host `card043: host real death: N hits, Health 0.0`; WARN classes per side are those of c196-r2 (host:
  attach 1, card043 1; client: attach 1), with no new class. The card043 WARN is expected to go (carried fix).

Shots: `c208-client-two`, `c208-host-two`, `c208-client-one`, `c208-host-one`, each on both sides. If the body does
not follow the form, it is reported, not fixed.

Unproven in this run: the join replay row (no rejoin in the route), and rows for another client's copy (2 players).

## Run c208-r1: scenario audit

| Scenario wait | Emitted by | Exact emitted text |
|---|---|---|
| `card208: host bug forms of \S+ \(host-own\): tags ` | c208_pawn_line | `card208: %s bug forms of %s (%s): tags %s, ...` |
| `card208: client bug forms of \S+ \(other\): tags ` | c208_pawn_line (client, c43_client_host_pawn) | same |
| `card057: client t=\d+ switch to weapon \d+: Equip sent` | c57_send_equip | `card057: client t=%llu switch to weapon %d: Equip sent` |
| `card057: host t=\d+ Equip \d+ from \S+: EquipWeapon on the server copy` | c57_receive_equip | `card057: host t=%llu Equip %d from %s: EquipWeapon on the server copy` |
| `card208: host t=\d+ form [1-9] of \S+ \(server-copy\) equipped: BugForm sent to 1 client` | c208_send | `card208: host t=%llu form %d of %s (%s) %s: BugForm sent to %u client(s), ...` |
| `card208: client t=\d+ BugForm [1-9] for the own pawn \S+: ` | c208_receive | `card208: client t=%llu BugForm %d for the %s %s: ...` |
| `playlog: client t=\d+ weapon slot 0 client-own: \S+ -> \S+ \((?!BP_BugFormDefinition_Warrior)` | pl_weapons | `playlog: %s t=%llu weapon slot %d %s: %s -> %s (%s)` |
| `card024: host relay press \(Fire 1\) t=\d+: pawn ` | card024 relay | `card024: host relay press (Fire 1) t=%llu: pawn %s, weapon %s` |
| `playlog: host t=\d+ ability slot \d+ (server-copy\|host-own): BP_BugForm_\w*Ability_(?!Warrior_C)\w+_C` | pl ability | `playlog: %s t=%llu ability slot %d %s: %s` |
| `card208: client t=\d+ BugForm \d for the host pawn \S+: EquipWeaponByClass\(\S+\), equipped A -> B` | c208_receive | `...: EquipWeaponByClass(%s), equipped %d -> %d` |
| `card043: host real death: \d+ hits, Health 0\.0` | c43 kill | `%scard043: host real death: %u hits, Health %.1f` |

All the other waits are the c196-r2 body unchanged. The only sleep is the final `sleep 3`.

## Run c208-r1

DLL `101523912892e155` (deployed and built hash match). Run ended by the scenario after 277 s; both games exited rc 0,
but the host log ends in a crash, not a quit line, and the client log ends standalone (`card024: host pc 0 ...`).
New crash dump `UECC-Windows-C1713B0F41AE5F0BDD050ABAD1355D10_0000`.

**Result: host crash at the client's join, in the new card 208 join replay, before any switch step.** Branch: neither
(no possess line, no switch ran). All predictions P1-P5 and P-last are missed (not reached). Shots: none (the drive
failed at line 11, `host wait state pawn=host-own timeout=240 -> timeout; last state pawn=other`).

Logged lines (host log):

- 2401 `card208: host t=120947779 form 0 of BP_PlayerTransportDropship_C (host-own) join replay: BugForm sent to 1
  client(s), 0 skipped (another client's copy)`
- 2402 `card208: host t=120947779 form 0 of BP_PlayerCharacterMI_C (server-copy) join replay: BugForm sent to 1
  client(s), ...`
- 2403 `CRASH 1/4: EXCEPTION_ACCESS_VIOLATION (0xC0000005) at ubwcoop+0xE55C0 thread 368`, 2404 `reading 0x2D8`,
  2405 `rcx=0`; stack `#1 ubwcoop+0x1D7E8 #2 ubwcoop+0x1B674 #3 ubwcoop+0x288BF ...`; later `Unhandled Exception:
  EXCEPTION_ACCESS_VIOLATION reading address 0x00000000000002d8`.

Cause (from the lines and the code): c208_join_replay builds `C50List{}` when a slot's pawn is not a bug pawn, and
`C50List{}` zero-initialises `equipped` to 0, not -1. The `list.equipped >= 0` test therefore passes for every slot:
the dropship (host's pawn on FED_C1M1 at join) and the client's MI copy were sent a form 0 row (lines 2401, 2402), and
the next slot, with no PC, called c208_send with a null pawn. c208_send reads the pawn's controller first
(`read_ptr(pawn, kPawnController)`, the 0x2D8 read from rcx=0). The client never logged a card208 line (the frames did
not arrive before the host died); its only new WARN is `card045: client follow gave up after 3 tries` (line 3796),
from the host crash.

c208_track is not exposed the same way: it takes a row only when `list.count > 0`, and a default list has count 0.

Fix (not built, per the stop rule): in c208_join_replay, skip when `inventory == nullptr` (send only when
`inventory != nullptr && list.equipped >= 0`). The same guard in c208_track for clarity, and c208_send returning early
on a null pawn as the boundary check.

WARN classes against c196-r2: host attach 1 (card043 not reached); client attach 1 plus card045 1 (from the crash).

Unproven: everything in this card's predictions (no switch ran), plus the join replay row and other clients' copies.

## Fixes after c208-r1 (weapons.cpp only)

1. c208_join_replay sends only when `inventory != nullptr && list.count > 0 && list.equipped >= 0`.
2. c208_send returns at once on a null pawn (or null world).
3. c208_track takes a row only when `inventory != nullptr && list.count > 0`.
4. c208_receive drops `index < 0` as well as `index >= list.count`, before `list.weapon[index]` is read, and drops
   a null form (null weapon entry or null definition) with the same capped WARN, reason "null form".

Same-class check over every c208_* function:

- Default C50List: after the fixes, every use of `list.equipped`, `list.count` or `list.weapon[]` sits behind
  `inventory != nullptr` (track, join replay, receive). c208_pawn_line and c208_forms run only from track, after it.
- Null slots: c208_step (host) and c208_join_replay give a null pawn for a slot with no PC or no pawn, and
  c208_inventory returns null for it. c208_send now returns on a null pawn. The client's c23_own_pawn and
  c43_client_host_pawn return null when nothing is there, and that null reaches only c208_inventory.
- Dropship and mech pawns: c196_inventory_at returns 0 for anything that is neither MI nor STPlayerCharacterBug, so
  c208_inventory is null for them. c208_tags, c208_forms and object_name are reached only with a non-null bug pawn and
  c50_list's non-null entries; object_name(nullptr) prints "null" anyway.

Question: how does a client address a third player's pawn? No existing op does.

- Card 043's Down and revive code on the client knows only the host's entry (kC43ClientHost). It finds the pawn with
  c43_client_host_pawn, which returns the first player pawn of sprint side "other". With three players, that would be
  either the host's pawn or the other client's copy, whichever comes first in the level's actor list.
- The existing way to name an arbitrary replicated actor is its NetGUID: c41_guid(actor) on the host and
  c41_resolve_guid(pc, guid) on the client (g_object_from_guid on the PC's package map). Card 042's Gib, card 132's
  '@guid' decal targets and card 136's FX rows use it for NPCs and corpses, but no op uses it for player pawns.
- A three-player BugForm (and Down) row would carry the pawn's NetGUID instead of the self flag.

## Run c208-r2

DLL f75f9ee39db9b7b5 (the c208-r1 build plus the four weapons.cpp fixes above). Scenario tools/scenarios/c208-r2.txt
(c208-r1 with the header and run line renamed). The run took 158 s. Host rc 0, client rc 0, no crash dump. Drive rc 2
(failed lines below). Both logs end in their quit lines: host "cmd: 13 quit: RequestEngineExit" (scenario line 103)
and client "cmd: 10 quit" (line 104). After those come the playlog "summary (session end)" lines.

No crash at the join. The c208-r1 fault is gone. Join replay (host t=121394698): form 0 was sent to 1 client for
host-own and for server-copy. The non-bug pawns sent no rows. On the client both rows printed "already equipped".

Predictions:
- P1 met (lines 47-50). BugForms.Can.* tags exist only on the owner's own pawn.
  - Host host-own: "BugForms.Can.Hopper,BugForms.Warrior", forms 0=Warrior, 1=Hopper, 2=Tanker, equipped 0.
  - Host server-copy: "BugForms.Warrior" only.
  - Client client-own: "BugForms.Warrior,BugForms.Can.Hopper".
  - The client's copy of the host pawn: "BugForms.Warrior" only.
  - No pawn has Can.Tanker.
- G2 met.
  - Client: "GA_BugForm_SwitchAbility_C NetExecutionPolicy 0 -> 1", the same for EquipAbility and the switch
    ability CDO.
  - Host: 0 -> 3 for all three.
- Client Two (P2) missed.
  - Line 55 ok: "switch to weapon 1: Equip sent".
  - Line 56 ok, host line 4781: "Equip 1 from BP_PlayerController_C: EquipWeapon on the server copy".
  - Host 4785: ConstructWeaponSwitchRequest(1, 0, BP_PlayerCharacterBug_C) on the server copy. It produced no
    switch ability activation, no ability slot line and no weapon slot line. The server copy stayed on form 0.
  - Lines 57 (the server-copy form 1 send) and 58 (the host's copy on the client) FAILED. Line 63 (a server-copy
    non-Warrior ability) FAILED. At host 4881 the server copy still fires BP_BugForm_MeleeAbility_Warrior_C.
  - The client predicted the switch locally:
    - it activated BP_BugForm_UnequipAbility_Warrior_C, GA_BugForm_SwitchAbility_C and
      BP_BugForm_EquipAbility_Hopper_C;
    - it granted the five Hopper abilities, which waited for replicated specs;
    - 10 s later (client t=121431458) it printed 4 WARN card073 "grant ...Hopper_C: no replicated spec after
      10000 ms, input not bound" lines (Taunt, Glide, Swoop, DamageVolume).
  - The client prints no weapon slot line for its own pawn until One.
  - Cause (from the lines, not yet proven by a run): the switch ability's tag check refuses on the server copy
    because the copy has no BugForms.Can.Hopper. On the client the request passed because client-own has the tag.
- Host Two (P3) met.
  - Line 71, host 4987: "form 1 of BP_PlayerCharacterBug_C (host-own) equipped: BugForm sent to 1 client(s)".
  - Line 72, client 4675: "BugForm 1 for the host pawn ...: EquipWeaponByClass(BP_BugFormDefinition_Hopper_C),
    equipped 0 -> 1".
  - EquipWeaponByClass works on the tagless copy.
- Line 75 FAILED: after the host's Hopper LeftMouseButton, no non-Warrior host-own ability activated.
  - Host 4992: the Fire press reaches STBugForm, whose input tag map has Num 0.
  - Hopper's only Fire-tagged ability is BP_BugForm_SwoopAbility_Hopper_C, with tags InputTag.Skill.Mobility and
    InputTag.Fire (client 4672).
  - So a grounded Hopper Fire does not activate an ability. The wait expected a Warrior-style melee. This is a
    scenario prediction error, not a relay fault (not yet checked in game).
- Client One (lines 83, 84) FAILED.
  - Host 5086-5087: Equip 0 reached the server copy, which was already on form 0.
  - The client re-equipped Warrior locally (client 4735: "weapon slot 0 client-own: null -> STBugForm
    (BP_BugFormDefinition_Warrior_C)").
- Host One (P5) met.
  - Line 90, host 5335: form 0 sent.
  - Line 91, client 4822: "EquipWeaponByClass(BP_BugFormDefinition_Warrior_C), equipped 1 -> 0".
  - Client map end: "bug form rows 2, peak 2 of 5, rows skipped 0".
- P-last (the card043 death fix) met.
  - Host 5362 host-own Downed, 5370 server-copy Out, 5375 Dead, 5382 "gate off ... vanilla death".
  - 5383: "real-death hit 1 ... Health 1.0 -> -3599.0".
  - 5388: "card043: host real death: 1 hits, Health -3599.0", with no WARN prefix.
  - The try wait at line 101 failed only because its regex expected "Health 0.0".

Tags branch: mixed. The owner has the Can.* tags and the copies do not.
- Host to client works, because EquipWeaponByClass skips the tag check.
- Client to host is refused by the game on the tagless server copy. The mod prints no WARN and nothing crashes.

Shots (prefix/drive_c/users/steamuser/AppData/Local/StarshipTroopers_UBW/Saved/Screenshots/Windows/):
- c208-client-two:
  - Client: its own pawn still has the Warrior body, with HUD slot 2 highlighted and no ability icons on the right.
    This is the local prediction without a server spec.
  - Host: its own Warrior, the HUD on slot 1.
- c208-host-two:
  - Host: its own pawn is the Hopper (wings), HUD slot 2, Hopper ability icons, green FX.
  - Client: Hopper wings fill the view ahead of the camera, with the same green FX. The wings are most likely the
    host's pawn, which stood next to the client.
- c208-client-one: the client is back on HUD slot 1 and the Hopper wings are still in view (the host has not yet
  pressed One).
- c208-host-one: the host is a Warrior again, HUD slot 1.

WARN classes against c196-r2 (c196-r2: host attach 1 + card043 1, client attach 1):
- host: attach 1. card043 is gone.
- client: attach 1, plus 4 new card073 lines (the Hopper grants after the refused client switch).

Unproven:
- the client switch reaching the server copy, and so the server-copy form row to the other machine;
- rows for another client's copy (this run had only 2 players);
- the Hopper attack line.

Proposed G1 fix (not built). In c57_receive_equip, when the requested slot is a bug form, the host applies it on
the server copy with EquipWeaponByClass(definition of list.weapon[index]) under g_c50.applying. This replaces
the game's switch ability, which refuses without BugForms.Can.*. The c208_track send then follows as it does on
the host's own switch. The client's local prediction gets its replicated specs because the server copy now holds
the form.

## Trace 2

Static only (kis.py bytecode dumps of the cooked Blueprints, CDO property decoding, the exe's PropPointers arrays).
No run.

**a. The Can.Hopper branch.** GA_BugForm_SwitchAbility is the native USTWeaponSwitchAbility. Its
ShouldAbilityRespondToEvent (RVA 0x05137FB0) calls the Blueprint CanSwapToWeapon (thunk 0x050B7CB0) before
the WeaponSystem.SwitchWeapon event can trigger it. CanSwapToWeapon in BP_PlayerCharacterBug:

```
    206: ..._ReturnValue_1 = MakeLiteralGameplayTagContainer({BugForms.Can.Hopper})
    404: HasAllMatchingGameplayTags_1 = GetSTPlayerCharacter(self).HasAllMatchingGameplayTags(<Can.Hopper>)
    460: BooleanAND_1 = (NewIndex == 1) AND HasAllMatchingGameplayTags_1
    366: BooleanAND   = (NewIndex == 2) AND HasAllMatchingGameplayTags(<Can.Tanker>)
    498: BooleanOR    = (NewIndex == 0) OR BooleanAND_1
    536: BooleanOR_1  = BooleanOR OR BooleanAND
    574: if not (BooleanOR_1) goto 1186
   1162: ReturnValue = BooleanOR_1   (true path)
   1186: GetSTHUD -> BP_HUD_Manager_Bug.SpecificHUD.WB_Bug_Crosshair_ProgressBar.BadFlashAnim()   (false path)
```

On a miss it returns false. The false path plays only the local HUD BadFlashAnim, and the HUD cast fails on the
host's copy. ShouldAbilityRespondToEvent then returns false. The switch ability is never activated, so there is no
ActivateAbility, no EquipWeapon event and nothing to end. This matches c208-r2 host 4785: ConstructWeaponSwitchRequest
ran, and no ability or weapon line followed.

**b. Who sets Can.Hopper.** BP_PlayerCharacterBug's ubergraph sets it as a loose ASC tag through
`GetSTPlayerCharacter(self).AbilitySystemComponent.AddLooseGameplayTags / RemoveLooseGameplayTags`. Loose tags are
not replicated. The tag is set in three places:

- ReceiveBeginPlay (4604) runs on every copy. It calls DefoGiveCanHopper (4614), which sets Can.Hopper with no
  condition. It then starts the looping Tick Charge Event timer.
- Tick Charge Event (3435) calls TryAssignCanTankerTag (3842) every tick.
- TryAssignCanHopperTag (7485) sets Can.Hopper when ChargeDelta / MaxCharge >= 0.33 and removes it otherwise.
  TryAssignCanTankerTag (7097) does the same for Can.Tanker at >= 1.0.

ChargeDelta and MaxCharge grow only from owner-local sources:

- Fedscore Gained Event (4089), bound to the local world subsystem's OnFedScoreGained;
- ReceiveActorFullyLoaded (6956), from save data;
- EngageFullCharge.

So each machine computes the tags from its own charge values. The owner's pawn holds the save and fedscore charge,
and the server copy does not. In c208-r2 (P1) only host-own and client-own carried Can.Hopper. The server copy and
the client's copy of the host pawn had BugForms.Warrior only. The exact removal point on the copy is not quoted
here. The likely candidate is the charge-bar path, which recomputes at a charge of 0.

**c. Activation tags of the abilities.** These were decoded from each ability CDO's unversioned properties. The
UGameplayAbility property order comes from the exe's PropPointers. The weapon ability class adds six
InventoryOwner* containers in front of it.

| Ability | Required | Blocked (own / InventoryOwner) |
|---|---|---|
| GA_BugForm_EquipAbility (Hopper and Tanker children add no tags) | none | owner: MovementMode.Native.Falling, Flying, CustomPlayerBug.Gliding |
| BP_BugForm_MeleeAbility_Tanker | none | BugForms.Ability.Flamethrower; owner: BugForms.Transforming |
| BP_BugForm_FlamethrowerAbility_Tanker | none | EquippedWeapon.Melee; owner: BugForms.Transforming |
| BP_BugForm_DamageVolumeAbility_Tanker (trigger Transforming.Finished) | none | none |
| BP_BugForm_JumpAbility_Hopper (no own containers; GA_CharacterJump is assumed to be the parent) | none | InProgress.Jumping, BugForms.Transforming |
| BP_BugForm_GlideAbility_Hopper (trigger OwnedTagAdded Falling) | none | none (it blocks Sprint) |
| BP_BugForm_SwoopAbility_Hopper | none | MovementMode.Native.Walking, NavWalking |
| BP_BugForm_DamageVolumeAbility_Hopper | MovementMode.CustomPlayerBug.Gliding, BugForms.Ability.HighSpeed | none |
| GA_BugTaunt (Hopper and Tanker children only set the anim) | none | Taunting, Equip/Unequip, Transforming, Melee, Jumping, Flying, Falling, Gliding, Flamethrower, Charge |

None of these abilities requires BugForms.Can.* or any other owner-only tag. The only required tags are on the
Hopper DamageVolume, and both are state tags made on the machine that runs the glide: HighSpeed comes from the Glide
GE, and Gliding comes from the movement mode. Whether they appear on the server copy during a client glide is not
checked here. So the equip-by-class fix leaves no ability dead for a missing owner tag, and Step 2 proceeds.

Side finding: a grounded Hopper's Fire is Swoop, which is blocked while Walking. So the r2 line 75 wait could never
pass, and the r2 line 63 wait (the client's Hopper attack) cannot pass either. Both waits move to the Hopper Jump.

**d. What GA_BugForm_EquipAbility does besides EquipWeaponByInstance.** The host path skips the ability. It does
the following:

- K2_CommitAbility, with no cost GE.
- A local HUD widget's Toggle ProgressBar(true, AnimationTime + PostAnimationDelayTime), and then on
  OnAnimationEnded, after Delay(PostAnimationDelayTime), Toggle ProgressBar(false) and K2_EndAbility. This is local
  HUD only.
- While it is active, ActivationOwned WeaponSystem.EquipWeapon, and InventoryOwnerActivationOwned
  HealthAndDamageSystem.Invulnerable on the owner. Skipping it means the server copy is not invulnerable during the
  transform.
- No montage, GameplayEffect or cue is played by the base ability. The transform animation comes from elsewhere,
  through the form weapon or anim BP that sends OnAnimationEnded.
- BP_BugForm_EquipAbility_Tanker also applies a GameplayEffect to self (an imp1 GE class) and prints the debug
  PrintString 'plarps'. The Hopper and Warrior children add nothing.

Visible items named here, with no relay built: the Tanker equip GE, the transform animation, and the owner's
Invulnerable window.

## Run c208-r3 predictions

Code: weapons.cpp only. In c57_receive_equip, a bug server copy goes to c208_copy_equip. That function bounds-checks
the index against c50_list (capped WARN and drop on a bad index or null form). It then calls
g_orig_c50_equip_by_class under g_c50.applying and logs one line. The MI path is unchanged. The changed functions
are 18 and 24 lines long. Scenario: tools/scenarios/c208-r3.txt. Its changes against r2 are listed in its header.
The client attack and host attack ability waits are now Hopper Jump waits.

Scenario audit (whole file): every state-dependent step waits on its log line.
- The possess lines (48-51) wait without a mark, but they match bug pawns only.
- `sleep 3` (line 112) only spaces the quit lines.
- The new warp and look complete through their cmd done lines before the shots.
- There is no client mark between client Two and host Two, so the card073 check covers that whole window.

- P2 (client Two):
  - host "card208: host t=... Equip 1 from BP_PlayerController_C: EquipWeaponByClass(BP_BugFormDefinition_Hopper_C)
    on the server copy, equipped 0 -> 1";
  - host "form 1 of ... (server-copy) equipped: BugForm sent to 1 client(s)";
  - client "BugForm 1 for the own pawn ...: already equipped" or its EquipWeaponByClass line;
  - host "playlog: host ... weapon slot 1 server-copy: ... (BP_BugFormDefinition_Hopper_C)";
  - the client's SpaceBar gives host "ability slot N server-copy (client request): BP_BugForm_JumpAbility_Hopper_C";
  - zero "WARN card073" lines on the client between the Two mark and the host Two mark, because the server grants
    the Hopper specs and they replicate;
  - the host shot c208-client-two shows the client's pawn as a Hopper.
- P3 (host Two): met as in r2 (lines 71/72). The host SpaceBar logs "host-own: BP_BugForm_JumpAbility_Hopper_C".
- P5 (client One):
  - host "Equip 0 ... EquipWeaponByClass(BP_BugFormDefinition_Warrior_C) on the server copy, equipped 1 -> 0";
  - host "form 0 of ... (server-copy) ... sent to 1 client";
  - client "BugForm 0 for the own pawn".
  Host One is met as in r2.
- Everything met in r2 stays met: P1, G2, the join replay, host Two and One, and the card043 real-death line (now
  without a WARN prefix, any Health).
- 0 mismatches.
- The WARN classes equal c196-r2's, minus the host card043 line.

## Run c208-r3

DLL a8188057ac1b264a. Scenario tools/scenarios/c208-r3.txt. Logs: logs/c208-r3-host.log, logs/c208-r3-client.log,
logs/c208-r3.drive. Run time 150 s; host rc 0, client rc 0, no crash dumps; drive rc 2 (failed lines 80, 81, 85,
100, 101). Both quit by command (host log 7847, client log 6762). 0 mismatches (client rounds x8, host summary).

Scenario deviation: r2 line 63 (client Fire after Two) had the same flaw as r2 line 75 (a grounded Hopper's Fire is
Swoop, blocked while Walking); r3 uses the client Jump there too. This was outside the ordered changes.

### P2, client Two: met

- host 4788: `Equip 1 from BP_PlayerController_C: EquipWeaponByClass(BP_BugFormDefinition_Hopper_C) on the server
  copy, equipped 0 -> 1`.
- host 4790: `weapon slot 1 server-copy: STBugForm -> STBugForm (BP_BugFormDefinition_Hopper_C)`.
- host 4792: `form 1 of BP_PlayerCharacterBug_C (server-copy) equipped: BugForm sent to 1 client(s)`.
- client 4153: `BugForm 1 for the own pawn: EquipWeaponByClass(BP_BugFormDefinition_Hopper_C), equipped -1 -> 1` (the
  local switch was mid-unequip); client 4155: `weapon slot 0 client-own: null -> STBugForm (Hopper)`.
- client 4143-4152: Jump h197, Glide h198, DamageVolume h200 and Taunt h201 bound to replicated specs.
- host 4810: `ability slot 1 server-copy (client request): BP_BugForm_JumpAbility_Hopper_C`; client 4183: client-own
  Jump. The server copy's Glide (h198) and DamageVolume (h200) activations follow.
- One client `WARN card073` remains (client 4567): `grant BP_BugForm_SwoopAbility_Hopper_C: no replicated spec after
  10000 ms` (r2 had 4).

### P3, host Two: missed (scenario fault)

- `host warp actor BP_PlayerCharacterBug_C 400` (host 4853) ran while the client copy was mid Hopper jump (copy at
  z 4144). It moved the host from z 2813.9 to z 3786.9, into the air.
- Host Two was pressed 512 ms later (host 4886) while the host was falling. ConstructWeaponSwitchRequest(1, 0)
  (host 4893) gave no EquipAbility activation. The likely cause is GA_BugForm_EquipAbility's
  InventoryOwnerActivationBlocked, which holds MovementMode.Native.Falling (Trace 2c); not proven at runtime.
- So the host stayed Warrior: SpaceBar gave GA_CharacterJump_C (host 6258), lines 80, 81 and 85 failed, and host One
  (lines 100, 101) had nothing to change. The host path is unchanged since r2, where host Two and One were met.

### P5, client One: met

- host 6497: `Equip 0 from BP_PlayerController_C: EquipWeaponByClass(BP_BugFormDefinition_Warrior_C) on the server
  copy, equipped 1 -> 0`; host 6500: weapon slot 1 server-copy Warrior; host 6513: form 0 sent to 1 client.
- client 5318: `BugForm 0 for the own pawn: EquipWeaponByClass(BP_BugFormDefinition_Warrior_C), equipped 1 -> 0`.

### Met as in r2

Join replay (host 4283/4284, client 3839/3841: already equipped), P1 possess lines, lines 37-44, the downs, card196
StartDeath, the card044 death screen.

### Real death: not reached

Try line 111 TRY-FAILed. host 7579: `WARN card043: host real death: 5 hits, Health 1.0`. The hits came from
BP_AI_WarriorBug_C and did 0 damage (Health 1.0 -> 1.0). r2 had a 4000 self-hit and `gate off ... vanilla death`
(r2 5382); r3 has no gate-off line. Cause unproven (possibly the host's position after the warp).

### Shots

- client c208-client-two: the client's own pawn is a Hopper (winged body from behind), HUD form slot 2 selected,
  Hopper ability icons (Swoop, Jump).
- host c208-client-two: dark sky, only the copy's "Player" name tag at the top edge. The host was mid-air and the copy
  above it; the copy's body is not in frame. No receiver-side visual of the Hopper copy.
- host c208-host-two (host t=122884571, before client One at 122884828, so the copy should be a Hopper): the copy is
  seen side-on with its "Player" tag; no wings are visible and the shape reads as Warrior-like. Not a clear Hopper.
  Receiver-side visual proof of the copy's Hopper form is therefore missing or contrary.
- host c208-client-one: the host's own Warrior from behind. client c208-client-one: a Warrior-shaped bug facing the
  camera at a door with green particles; which pawn it is cannot be told from the shot.

### WARN classes

| Run | Host | Client |
|---|---|---|
| c208-r3 | attach 1, card043 1, card132 3 (hit impact ~1050-1150 uu from the NPC) | attach 1, card073 1, card148 7 (gib part asleep above the ground) |
| c196-r2 | attach 1, card043 1 | attach 1 |

The prediction "c196-r2 minus the host card043" is missed: host card043 (real death) stays, and host card132,
client card073 and client card148 are new.

### Unproven

- Host Two's cause (Falling block on the EquipAbility) and the host path in r3; r2 is the last proof of host Two/One.
- The copy's Hopper look on the host (shots above): the server copy equips the Hopper definition, but no shot shows
  a Hopper mesh on the copy, and host c208-host-two looks Warrior-like.
- The Swoop card073 WARN (possibly the Swoop spec sits on the form ASC).
- The real-death 0-damage hits.
- The DamageVolume and Glide tags on the server copy.
- The host-side c208_copy_equip path skips GA_BugForm_EquipAbility, so the Invulnerable window, CommitAbility and the
  Tanker self GE are not run on the server copy.

## Senior review (2026-11-12): accepted in part, run budget spent, remainder split

Accepted (DLL `a8188057ac1b264a`, runs c208-r2 and c208-r3; diff against the pre-208 snapshot read, function
lengths unchanged, 0 mismatches in both runs, no crash after the r1 guard fixes):
- Host switch reaches the client: BugForm rows, client `EquipWeaponByClass`, and the client's shot in c208-r2
  shows the host pawn as a Hopper. Join replay sends rows for bug pawns only.
- Client switch reaches the host's server copy by `c208_copy_equip` (the copy lacks the owner-local
  `BugForms.Can.*` loose tags, Trace 2), the copy's Hopper Jump, Glide and DamageVolume run on the host, and the
  row goes back to the owner.
- Playlog weapon line with the definition class; bug switch and equip ability policy.

Not met, moved out:
- The host's screen still shows the client's copy as a Warrior after the copy equipped Hopper (host shot
  `host-c208-r3-c208-host-two.png`; the client's own view is a Hopper). Receiver-side proof of the client-to-host
  direction is missing. Card 210, with the Swoop `card073` WARN, the skipped Invulnerable window and Tanker GE,
  and the host switch while falling.
- Host real death under god: `WARN card043: host real death: 5 hits, Health 1.0` again in c208-r3 (causer
  BP_AI_WarriorBug_C, 0 damage); r2 passed with a 4000 self-hit. Card 211.
- New client WARN class in c208-r3, `card148: client gib host part ... asleep above the ground` x7. Card 212.
- Third and fourth players: card 209.
