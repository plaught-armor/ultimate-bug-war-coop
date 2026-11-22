# Card 196: Remote-body features work on the bug-side player pawn

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: queued 2026-11-12, from card 192. Budget: three runs. Static first. After card 195.

## Why

Card 192 stopped the crash by skipping four MI-only features on `BP_PlayerCharacterBug_C`: "c33 weapon hide"
(`remote_body.cpp`), "c24 weapon inventory" (`hooks_ubw.cpp`), "c50 Use relay inventory" (`interact.cpp`), "c29 test
equip" (`vehicles.cpp`). On a bug map the other player's bug therefore shows no weapon or attack state from these
paths. Card 195's sweep table adds the other MI-only sites.

## Do

1. Trace what the bug pawn has in place of each skipped MI member (weapon mesh, inventory, attack ability) and what
   already replicates. One row per feature: needed on the bug pawn or not, and why.
2. Implement the needed ones in the owner of each feature; keep `c192_mi_pawn` for the rest.

## Done when

- A bug-map run shows each needed feature on the receiving side (log proof of the visible state), and the per-map
  "skips" lines list only features marked not needed. The Report ends with "Report complete."

## Evidence from c195-r1 (2026-11-12)

- New client WARN class on the bug map, three lines: "WARN card073: client ... GA_CharacterJump_C /
  GA_BugTaunt_C / BP_BugForm_VampirismAbility_Warrior_C: no SourceItem property, SourceItem not set"
  (`grenades.cpp:135`, `c73_point_item`). These are not item abilities. Trace why card 073 treats them as item grants
  on the bug pawn (and whether `c73_item_inventory(pawn)` reads an MI field there), then make the class test right;
  the WARN stays for a real item ability.
- Card 195's sweep table lists the 52 MI-only sites, and `death.cpp:1449` (a client bug-pawn death skips the
  `ASTPlayerCharacterBug::StartDeath` override).
- From the card 198 trace: the bug pawn's `FormsInventory` (+0xA30, `USTBugFormInventory`) is relayed by no card,
  and every hook on `ASTPlayerCharacterMI::LoadAbilitySystemComponent` (cards 048, 050) does not see the bug pawn
  (its override 0x051CDB60 calls `ASTCharacter`'s directly). Trace both here; split a card if forms need a relay.

## Senior prep (2026-11-12, static)

- The card 073 WARN: `c73_bind` (`grenades.cpp:153`) calls `c73_point_item` for every recorded grant, and
  `c73_point_item` (`:125`) warns whenever the ability instance has no `SourceItem` property. `g.item` is the
  grant's source (`:175`); for GA_CharacterJump_C, GA_BugTaunt_C and the vampirism ability it is not an item.
  `c73_item_inventory` reads the reflected `ItemInventory` property, so it is null-safe on the bug pawn, not an MI
  offset read. Candidate fix: an ability with no `SourceItem` property is not an item ability, so it binds its tags
  with no WARN; the WARN stays for "the grant's source is not our item". Confirm what `g.item` holds for the three
  before changing it.

## Trace (junior, 2026-11-12)

Static only (disassembly of the shipped exe, PDB publics, mod source, c195-r1 logs). RVAs are on the 0x140000000
image base.

### What the bug pawn has in place of the MI members

- `FormsInventory` (+0xA30) is a `UADWeaponInventory` subclass (`USTBugFormInventory`), and each bug form is an
  `AADWeapon` actor with its own weapon ASC. Proof: `ASTPlayerCharacterBug::StartDeath` (0x051DEE10) is byte-identical
  to `ASTPlayerCharacterMI::StartDeath` (0x051DEE70) except for the offset: base `ASTPlayerCharacter::StartDeath`
  (0x051DE410), then +0xA30 -> `UADWeaponInventory::GetEquippedWeapon` (0x04A2BD10) -> `AADWeapon::GetWeaponASC`
  (0x03650320) -> `UAbilitySystemComponent::CancelAllAbilities(nullptr)` (0x04951680). The MI copy reads +0xA40.
- Attacks: `ASTPlayerCharacterBug::HandleInputTag_Pressed` (0x051CB600) presses the tag on the pawn ASC (vfunc
  0x970) and then on `FormsInventory->GetEquippedWeapon()->GetWeaponASC()`, the same two-ASC pattern as the MI gun.
  Form abilities are `USTBugFormMeleeAbility` (PerformMelee, OnMeleeHit), `USTBugFormFlamethrowerAbility`
  (FireProjectile), `USTBugFormGlidingTask`.
- Form switch: `ASTPlayerCharacter::EquipWeapon` (0x051C57A0) jumps to vtable+0xAA8, the base
  `SendWeaponSwitchRequest` (0x051DB320), which the bug pawn does not override. So a form switch takes the same
  request path as an MI weapon switch.
- No ammo inventory: `GetWeaponInventory` and `GetAmmunitionInventory` exist in the PDB only on
  `ASTPlayerCharacterMI`. No grenade inventory either.
- Load: `ASTPlayerCharacterBug::LoadAbilitySystemComponent` (0x051CDB60) calls `USTBugFormInventory::LoadInventory`
  (0x05243C30) on +0xA30, equips one form if the equipped index is -1 (vfunc +0x20, then +0x528 with the index from
  +0x40), then calls `ASTCharacter::LoadAbilitySystemComponent` (0x050F28E0) and copies the load results' input
  binds.

### Table

| Site (file:line) | What it does on MI | On the bug pawn | Already replicated by the game | Needed on the bug pawn |
|---|---|---|---|---|
| `hooks_ubw.cpp:592` `c24_inventory` (card-192 "c24 weapon inventory") | +0xA40 weapon inventory for the relay press, `c24_weapon_by_id`, `c43_enter_down` cancel | +0xA30 `FormsInventory`, same base class | Form actors spawn on each machine; presses do not replicate (client-predicted ASC, card 024's reason) | **Yes.** One owner: MI -> +0xA40, bug -> +0xA30, both checked `is_a UADWeaponInventory`. Every caller (relay press on the copy's form ASC, weapon by id, `c43_enter_down` `hooks_ubw.cpp:1072`) then works for bugs. |
| `fire.cpp:112` `c26_own_fire_pawn`, `chan_ops.cpp:24` rider gate | Accepts only an MI own pawn for Fire frames | Bug presses rejected, so the client's bug attacks are never relayed and the host's server copy never attacks | No | **Yes.** Widen to player character (MI or bug). |
| `remote_body.cpp:746` `c33_hide` (card-192 "c33 weapon hide") | Hides the remote's own-copy weapon root | The form actor root through `c24_inventory` | Form actor visibility is local | **Yes, conditional.** Use `c24_inventory`; hide only if the root is visible. c195-r1 skipped it 296 times on the client (`c33 weapon hide skipped 296 times`), host log line 4106, client log line 3388. The run's c33 line shows whether a form root is visible at all. |
| `interact.cpp:191` `c50_interact` (card-192 "c50 Use relay inventory") | Inventory text in a log line (`c50_text`) | Same text from `c24_inventory` | n/a | **No.** Log text only; comes free once `c24_inventory` owns both. |
| `vehicles.cpp:402` `c29_equip` (card-192 "c29 test equip") | Test-driver weapon equip | n/a | n/a | **No.** Test driver only. |
| `weapons.cpp:1138` `c57_receive_equip`, `:1294` `c141_receive_switch`, `:1325`/`:1334` c141 step and pawn | Host applies the client's Equip/Switch frame on the server copy | Client sends the frame (`detour_c57_request` has no MI gate), host drops it: "WARN ... dropped (no server copy pawn)" | No | **Yes, as form switching.** See question 3; proposed as its own card. `c141_equipped` (~`:1268`) reads `kMiWeaponInventory` directly and needs the same owner. |
| `ammo.cpp:205/253/369/415`, `death.cpp:3014` `c72_client_step`, `hooks_ubw.cpp:1474` `c72_mirror_apply` | Ammo inventory sync | No ammo inventory (`GetAmmunitionInventory` MI only) | n/a | **No.** |
| `vehicles.cpp:296/579/1691`, `turret_sync.cpp:429/437`, `hooks_ubw.cpp:835` `c29_server_copy` | Vehicle, turret, mech seats | Bugs do not ride | n/a | **No.** |
| `grenades.cpp:648` `c76_relay`, `attributes.cpp:1163` `c76_mark_task` | Grenade throw relay | No grenades; flamethrower projectiles go through the ungated `c132_proj_task` | n/a | **No.** |
| `attributes.cpp:915` `detour_fire_projectile` | Shooter = avatar if MI, else the avatar's owner | A form avatar's owner is the bug pawn | n/a | **No change.** Already right for a form avatar. |
| `attributes.cpp:37` `player_asc` (card 018: `:165`/`:176`/`:203`/`:401`/`:433`/`:545`), `attributes.cpp:157/193/654` | Input-tag binds and spawned-attribute OnRep on the player ASC | Pawn-ASC binds on the bug came through card 073 (h179-h181, client log lines 3071-3076) | Specs replicate | **Probably no.** The run checks that every bug input tag binds (card017/073 lines) with no unbound press. |
| `chan_ops.cpp:28` soldier rounds (card 175) | Held-trigger round count | Bug attacks release at once, like a mech | n/a | **No.** |
| `chan_ops.cpp:35` `c26_host_use` | Use relay | Same `c43_receive_use` path, inventory via `c24_inventory` | n/a | **No** (comes free with `c24_inventory`). |
| `restore_c143.cpp:62/160` c143 Reload/Restore inventory, `:308` | Checkpoint restore of MI inventory | Forms come from the pawn's own `LoadInventory` (0x05243C30) | n/a | **No** for 196. Card 198 owns the bug restore if any. |
| `jump_c94.cpp:283/307` card 101 lerp window | MI mantle lerp | Bugs jump through `CanJumpInternal_Implementation`, no mantle | Movement replicates | **No** for now. |
| `death.cpp:1449` `c44_client_death` | Calls the MI StartDeath original under `death_pass` | Calls base `g_orig_start_death`, skipping the bug override | n/a | **Yes.** See question 4. |
| Diagnostics and log rows: `input_diag.cpp:1169/1534/1571/1572`, `fx_c136_net.cpp:181`, `fx_c136.cpp:334`, `jump_c94.cpp:74`, `sprint.cpp:915`, `hooks_ubw.cpp:1211` c48, `testrun.cpp:1687` c63, `death.cpp:1494` c44 host fire, `enemies.cpp:902` label, `playlog.cpp:230`, `weapons.cpp:318/321/324/531/663/865/888/1599` (c21/c50 MI weapon grant and parity) | Logs, MI weapon grants | n/a | n/a | **No.** c21/c50 weapon grant rows are MI weapon definitions; forms are granted by the pawn's own load. |

### Question 2: the card 073 WARN

- Only two code callers of `AddAbilityToActor` (0x04A0EE80) exist: `AddFeaturesToActor` (0x04A0F410, call site
  0x144A0F51E) and `ASTPlayerCharacterMI::ApplySecrets` (0x051BCE10). `AddFeaturesToActor` is called from
  `AADWeapon::AddOwnerEquippedFeatures` (0x144A2ADF5), `AADWeapon::AddOwnerInInventoryFeatures` (0x144A2AF5E),
  `USTItemInventory::AddInventoryItem_Internal` (0x14514ACFE) and `USTInventoryItem::OnEquip` (0x1451639D6).
- `AddOwnerEquippedFeatures` passes `this` (the weapon, at [rsp+0x28]) as the source; the target actor is
  weapon+0x2D8 -> +0xA0 (the owner pawn) and the abilities come from the definition (+0x2D0 -> +0x110) +0x80/+0x90.
- So `g.item` (`grenades.cpp:175`) for GA_CharacterJump_C, GA_BugTaunt_C and BP_BugForm_VampirismAbility_Warrior_C
  is the equipped bug-form `AADWeapon` (an owner-equipped or in-inventory feature), not a `USTInventoryItem`. The
  three classes have no `SourceItem` property; that property lives on `USTInventoryItemGameplayAbility`.
- The candidate rule is right: no `SourceItem` property means not an item ability, so `c73_point_item`
  (`grenades.cpp:125`) returns before the WARN at `:133-136` and the tags bind as now (c195-r1 bound all three: h179
  1 tag, h180 0 tags, h181 2 tags). The WARN stays for "the ability has `SourceItem` but the grant's source is not
  our item". `c73_item_inventory` (`grenades.cpp:69`) reads the reflected `ItemInventory` property: null-safe, no MI
  offset.
- c195-r1 ran with `UBWCOOP_C73_LOG` unset (client log line 42), so the source object name is not in the logs; the
  conclusion is static. On FED the MI pawn's Jump and Taunt bind through `InitFeaturesFromDefinition` (card017
  `BindInputTagToAbilitySpec`, caller exe+0x4A1096B, client log lines 1707-1711), not through card 073.

### Question 3: FormsInventory and the LoadAbilitySystemComponent override

- Card 198 already hooks the override: `detour_load_asc_bug` (`restore_c143.cpp:262`) runs only
  `c143_after_load_asc`. The MI hook `detour_load_asc` (`death.cpp:2147`) also runs the card 048 audit lines and
  `c50_after_load_asc`; neither is needed for forms (c48 is a log, c50 is the MI weapon grant).
- Each machine loads its own forms through `LoadInventory`, and each form's owner-equipped features grant per form on
  each machine, so the ability set itself is fine.
- What a second player sees wrong without a relay: a client's form switch goes out as an Equip/Switch frame
  (`detour_c57_request`, no MI gate) and the host drops it at `c57_receive_equip` (`weapons.cpp:1138`) /
  `c141_receive_switch` (`:1294`). The host's server copy stays in the old form, so its attacks (once card 196 relays
  them) run the old form's abilities, and the host sees the old form. The host's own form switch is not shown on the
  client's copy of the host bug either.
- Recommendation: its own card (form-switch relay: Equip/Switch applied on the bug copy through `c24_inventory`, and
  the remote form view both ways). Card 196 keeps attack, hide and death. If the senior prefers one card, the change is
  the c57/c141 MI gates plus `c141_equipped` reading through `c24_inventory`, since `g_equip_weapon` goes through the
  base vtable `SendWeaponSwitchRequest`.

### Question 4: `death.cpp:1449`

- `c44_client_death` calls `(mi ? g_orig_start_death_mi : g_orig_start_death)(own, damage_event)` under
  `g_c43.death_pass`. For a bug pawn it calls the base `ASTPlayerCharacter::StartDeath`, so the client misses the
  override's tail: `CancelAllAbilities` on the equipped form's weapon ASC. A held melee, flamethrower or glide keeps
  running on the dying client.
- Fix: add symbol `ASTPlayerCharacterBug_StartDeath` (`?StartDeath@ASTPlayerCharacterBug@@MEAAXAEBUFADDamageEvent@@@Z`,
  0x051DEE10) and call it directly for a bug own pawn. It is not hooked; its internal base call reaches the hooked base
  detour, which passes under `death_pass`, the same as the MI trampoline path. `g_cancel_all_abilities` is already
  declared (`death.cpp:517`).
- `c43_enter_down` (`hooks_ubw.cpp:1072`) has the same gap at down (weapon ASC cancel); it closes through
  `c24_inventory`.

### Proposal

Code (one build):

1. `hooks_ubw.cpp` `c24_inventory`: MI -> +0xA40, `BP_PlayerCharacterBug_C` (is_a `ASTPlayerCharacterBug`) -> +0xA30,
   both checked `is_a UADWeaponInventory`, else null. Remove the card-192 "c24 weapon inventory" skip.
2. `fire.cpp:112` `c26_own_fire_pawn` and `chan_ops.cpp:24`: accept MI or bug player character.
3. `remote_body.cpp:746` `c33_hide`: read the inventory through `c24_inventory`; remove the "c33 weapon hide" skip.
   `interact.cpp:191`: text through `c24_inventory`; remove the "c50 Use relay inventory" skip.
4. `grenades.cpp:125` `c73_point_item`: no `SourceItem` property -> return (bind, no WARN).
5. `death.cpp`: symbol `ASTPlayerCharacterBug_StartDeath`; `c44_client_death` calls it for a bug own pawn; one log
   line "card196: client t=N bug StartDeath (form ASC cancel)".
6. `c29_equip` keeps its card-192 skip (test only).

Scenario: `tools/scenarios/c195-r1.txt` fits (FED_C2M1, host `travel next` to BUG_C1M1, both on
`BP_PlayerCharacterBug_C`). A copy `c196-r1.txt` replaces its `sleep 30` with: spawn two enemies near the client, a
client attack press (`client key LeftMouseButton 800`) waiting on the host relay line, `host down client` waiting on
the card043 Downed line, then a wait (timeout 45) on the client death line through the 30 s bleedout (no AI
trooper is expected on the bug map to revive; if card 130 revives the client, the step is a `try` and P5 is
unproven, not failed), then the existing shots and quits. `kill actor` (`testrun.cpp:869`) targets the nearest
actor of a class, so it is not used for the client pawn. Run with `UBWCOOP_C73_LOG=1` on the client so the card 073
source name prints.

Written predictions:

- P1: no client "WARN card073" line for GA_CharacterJump_C, GA_BugTaunt_C, BP_BugForm_VampirismAbility_Warrior_C;
  the three still bind (card073 bind lines, same tag counts 1/0/2).
- P2: client attack press: client "client sent Fire" line, host "card024: host relay press ... weapon
  BP_BugForm_..." on the server copy's form ASC, then a host hit line on an enemy.
- P3: card-192 per-map skip lines list only "c29 test equip"; no "c24 weapon inventory", "c33 weapon hide" or "c50
  Use relay inventory" skips on either side.
- P4: a c33 hide line for the form root on both sides, or none if the form root is not visible on the own copy (the
  line count says which).
- P5: `host down client` -> card043 Downed line; after the bleedout, client "card196: client t=N bug StartDeath
  (form ASC cancel)" from `c44_client_death`, with no crash.
- P6: no new WARN class on either side compared with c195-r1 (WARN diff).

Separate card: the form-switch relay (question 3).

## Run c196-r1 (run 1 of 3)

Build: `dwmapi.dll` 0fb27eec81254f5e (items 1 to 5 and Task A). The predictions below were written after the build
and before the run (the senior asked for them before the build; the build had already been made when that came).

### Bug presses and the fire kinds

A bug press from the client carries only `kFirePrimary`. `c26_own_fire_pawn` allows `kFireAds` only for an MI
soldier (`c196_inventory_at(pawn) == kMiWeaponInventory` and not a mech) and `kFireMechAlt` only for a mech, so the
bug pawn's form ASC never sends either. If a host ever received `kFireAds` for a bug pawn, `c26_host_fire` would
pass it to `c160_host_ads_bits` and relay it (the mech test passes for a bug); the client never sends one.

Which input tag the bug's attack uses is not known statically (the ability assets are compressed). A capped line,
"card196: client t=N bug form press <tag> on <ASC owner> (<class>) of <pawn> (client-own)", logs each new
(form ASC, tag) pair pressed on the bug pawn. If the bug's left mouse button is not `InputTag.Fire`, P2 misses and
that line names the tag.

### Changes from the proposal's scenario

- No enemies are spawned. `spawn bug` needs map coordinates, none are known for BUG_C1M1 statically, a spawned
  `BP_AI_WarriorBug_C` may be on the player's side there, and the relay proof needs no target. P2 therefore has no
  hit line.
- P5 uses `host down host` after `host down client`, not a bleedout. `c43_client_down` sets `death_pending` only
  when the host pawn's entry goes to Dead (all down, `death.cpp:1362`), so a client bleedout alone ends at Out and
  never calls `c44_client_death`.

### Predictions

- P1: no client line "WARN card073: client t=N <name>: no SourceItem property, SourceItem not set" for
  GA_CharacterJump_C, GA_BugTaunt_C or BP_BugForm_VampirismAbility_Warrior_C. The three still bind: "card073: client
  t=N grant GA_CharacterJump_C bound to replicated spec hN (1 tags)", the same for
  BP_BugForm_VampirismAbility_Warrior_C (0 tags) and GA_BugTaunt_C (2 tags).
- P2: after `client key LeftMouseButton 800`, client "card026: client t=N client sent Fire 1 (press)" (argument
  `c24_fire_arg(kFirePrimary, true)`), and host "card024: host relay press (Fire 1) t=N: pawn
  BP_PlayerCharacterBug_C..., weapon BP_BugForm_..." (the server copy's equipped form).
- P3: the card192 lines "card192: <side> skips <feature> on ..." name only "c29 test equip" on either side, and only
  if the c29 path runs; no "c24 weapon inventory", "c33 weapon hide" or "c50 Use relay inventory" line. No "WARN
  card196: ... not a UADWeaponInventory" line.
- P4: on each side, one or more "card196: <side> form root of BP_PlayerCharacterBug_C...: form <form> (<class>),
  root <root>; before bVisible V bHiddenInGame H, after bVisible V bHiddenInGame H" lines for the other player's
  copy. The "after" values are whatever card 033 sets for the MI weapon root today (hidden on the copy). Zero
  lines means the copy's FormsInventory has no equipped form, and P4 misses with that cause.
- P5: client "card043: client t=N own pawn Up -> Downed" after `host down client`; after `host down host`, client
  "card044: client t=N StartDeath (bug override) call BP_PlayerCharacterBug_C..." and "card196: client t=N bug
  StartDeath (form ASC cancel)", and no crash (both logs end in their quit lines).
- P6: the digest round after the attack on the bug map: client "card132: client round N: actors A host, B client,
  mismatches M" with no mismatch class that c195-r1 round 1 did not have (c195-r1: 1 STLevelObjective presence).
  WARN classes per side against c195-r1: the three card073 SourceItem WARNs go (P1); no new class.

### Scenario audit (`tools/scenarios/c196-r1.txt` against this build's log texts)

| Line | Step | Log text it waits for | Source |
|---|---|---|---|
| 14-15 | wait state, both | driver state (pawn=host-own / client-own) | `testrun.cpp` state |
| 17 | client card138 (MI) | "card138: client t=N no hold: GameState ... begun play with pawn ... (BP_PlayerCharacterMI_C)" or "release after" | c195-r1 line 15, unchanged |
| 18 | client card120 (MI) | "card120: client t=N minimap origin ... (BP_PlayerCharacterMI_C) set" | c195-r1, unchanged |
| 24 | host LoadMap | "UEngine::LoadMap returned true" | c195-r1, unchanged |
| 25 | client reconnect | "reconnected (" | c195-r1, unchanged |
| 26 | client LoadMap count=2 | "UEngine::LoadMap returned true" | c195-r1, unchanged |
| 27-28 | wait state, both | driver state | c195-r1, unchanged |
| 29-30 | client card138 / card120 (bug) | as 17-18 with BP_PlayerCharacterBug_C | c195-r1, unchanged |
| 31 | host card098 | "card098: host t=N world journal replay to BP_PlayerController_C (first Pong of the map)" | c195-r1, unchanged |
| 32 | try host card049 | "card049: host t=N journal replay to BP_PlayerController_C (first Pong of the map)" | c195-r1, unchanged |
| 39 | host relay | "card024: host relay press (Fire 1) t=N: pawn " | `fire.cpp:199`, `c24_kind_text` "press (Fire 1)" |
| 40 | try client edge | "card026: client t=N client sent Fire " | `fire.cpp:86`, `hooks_ubw.cpp:564` |
| 41 | try client press | "card196: client t=N bug form press " | `fire.cpp:119` |
| 45 | host digest | "card132: host t=N digest round N (timer) sent: " | `digest_c132.cpp:783`, every 30 s (`kC132RoundMs`) |
| 46 | client digest | "card132: client round N: actors " (also matches a "WARN " prefix) | `digest_c132.cpp:1303` |
| 52 | client Downed | "card043: client t=N own pawn Up -> Downed" | `death.cpp:1353` |
| 54 | client bug StartDeath | "card196: client t=N bug StartDeath (form ASC cancel)" | `death.cpp:1459` |
| 55 | try death screen | "card044: client t=N death screen " | c142-r4 line 87 text |

The only sleep is 3 s before the quits. `down` (`testrun.cpp:933`) is host only and puts the slot into Downed
directly, so the god mode (`UBWCOOP_GOD=1`, both sides by default) does not block it.

### Results, c196-r1

73 s, host rc 0, client rc 0, no crash dumps, drive rc 2 (one failed line, 39: the host relay wait).

- P1 met. No client card073 SourceItem WARN. The three bind: GA_CharacterJump_C h179 (1 tags),
  BP_BugForm_VampirismAbility_Warrior_C h180 (0 tags), GA_BugTaunt_C h181 (2 tags).
- P2 missed. The bug's left mouse button is `InputTag.Fire`, pressed on the form ASC: "card021: client
  AbilityInputTagPressed InputTag.Fire on asc 00000000582AB140, owner STBugForm (pawn null, other)", and the form
  activates locally ("card024: client local activation handle 27 = 1, ability BP_BugForm_MeleeAbility_Warrior_C,
  owner STBugForm"). The form's `AActor::Owner` (+0x158) is null, so `c26_own_fire_pawn` and `c196_note_bug_press`
  read no pawn and return: no card026 edge, no card196 press line, no host relay.
- P3 met. No card192 line on either side; no "WARN card196" line.
- P4 missed, cause not logged. No "card196: ... form root" line on either side. `c196_form_root_line` starts with
  `last` = -1 and skips an id equal to `last`, so a null root (id -1) is never logged: the copy's inventory, its
  equipped form or that form's root was null on every call, and the line does not say which.
- P5 met. Client "card044: client t=118583910 StartDeath (bug override) call BP_PlayerCharacterBug_C (client-own,
  role 2/3)", "card196: client t=118583910 bug StartDeath (form ASC cancel)", CompleteDeath 3012 ms later, death
  screen WB_PlayerDeathScreen_Bug_C, both games quit cleanly.
- P6 met on the digest: "card132: client round 1: actors 1076 host, 1076 client, mismatches 0". WARN classes: client
  only "WARN attach: DisableThreadLibraryCalls failed" (as c195-r1); host adds "WARN card043: host real death: 5
  hits, Health 1.0" (`death.cpp:837`). That one comes from the scenario, not this card: `host down host` under
  `UBWCOOP_GOD=1` puts each of the host pawn's death hits back.

## Trace 2 (after c196-r1)

Static trace, no build and no run. Addresses are exe RVAs (image base 0x140000000) unless written as a full VA.

### 1. How the game gets from a form (or its ASC) to the pawn

- `AADWeapon::InitialiseWeaponItem` (0x04A2C680) stores the definition at weapon+0x2D0 (VA 0x144a2c6f6) and the
  owning inventory, its third argument, at weapon+0x2D8 (VA 0x144a2c6fd). The inventory's
  `UActorComponent::OwnerPrivate` (+0xA0, `kComponentOwner`, `ue/actor.h:88`) is the pawn.
- The game itself walks weapon+0x2D8 then +0xA0 for both kinds: `ASTWeapon::InitialiseWeaponItem` (0x05241380,
  VA 0x1452413bb..c9: `mov rax,[rbx+0x2d8]; mov rcx,[rax+0xa0]; call IsPlayer`, stored at +0x388), and
  `ASTBugForm::OnEquip` (0x05244E20, `this` is the subobject at weapon+0x2B8, so `[rdi+0x20]` is weapon+0x2D8;
  VA 0x145244ea9 / 0x145244eb6 read +0x20 then +0xA0 and test the class `ASTPlayerCharacterBug`).
  So **weapon+0x2D8 (owning inventory) then inventory+0xA0** is the one member chain the game sets for both an MI
  gun and a bug form.
- `AActor::Owner` (+0x158, `kActorOwner`, `ue/actor.h:78`): `UADWeaponInventory::SpawnWeaponInstance`
  (0x04A2D320) spawns with a default `FActorSpawnParameters` (ctor at VA 0x144a2d3ed, Owner null; `SpawnActor` at
  VA 0x144a2d42d, return 0x4A2D432) and then calls slot +0x748 `InitialiseWeaponItem(weapon, &def, inventory)`
  (VA 0x144a2d497). Owner is set only by the MI override `USTWeaponInventory::SpawnWeaponInstance` (from about
  0x5251010): when the spawned actor is an `ASTWeapon` it calls `[rax+0x500]` = `AActor::SetOwner` (VA
  0x1437f4150) with `[rbp+0xa0]`, the inventory's owner pawn (VA 0x1452512fd), then `HideWeapon` (VA 0x145251306).
  `USTBugFormInventory` has no `SpawnWeaponInstance` override: every form spawn in r1 has the base caller
  exe+0x4A2D432 (card098 lines, both sides). `ASTBugForm` derives from `AADWeapon` directly
  (`Z_Construct_UClass_ASTBugForm_Statics` DependentSingletons[0] = `Z_Construct_UClass_AADWeapon`), not from
  `ASTWeapon`, so nothing calls SetOwner on a form and its Owner stays null.
- The form's ASC: the `AADWeapon` ctor (0x04A29FE0) creates a `UADAbilitySystemComponent` default subobject, so the
  ASC's component owner (+0xA0) is the form (the r1 line "card021: client AbilityInputTagPressed InputTag.Fire on
  asc 00000000582AB140, owner STBugForm"). The ASC's OwnerActor (+0x3E0) and AvatarActor (+0x3E8) are the weapon
  for a gun (the card 024 comment in `detour_fire_projectile`: a weapon's fire ability has the weapon as avatar);
  for the form this is inferred from the same base class, not read. The inventory's own owner (+0xA0) is the pawn
  for both kinds (`USTBugFormInventory::LoadInventory` 0x05243C30 reads `[rcx+0xa0]` and tests the bug class).

### 2. Mod readers that take the pawn as weapon->Owner

Every `read_ptr(x, kActorOwner)` in `mod/src/ubw` where `x` is a weapon or a weapon ASC's owner:

| Site | Use | Bug form reaches it | r2 |
|---|---|---|---|
| `fire.cpp:107` `c196_note_bug_press` | bug press note | yes (pawn null today) | moves |
| `fire.cpp:140` `c26_own_fire_pawn` | own fire edge, the r1 P2 miss | yes | moves |
| `attributes.cpp:887` `detour_zero_adjust` | aim adjust | if a form fire ability calls it | moves |
| `attributes.cpp:916` `detour_fire_projectile` | shooter side | if a form fires a projectile | moves |
| `weapons.cpp:1334` `c141_pawn_of` | underslung pawn | MI only in practice | moves |
| `claim_c157.cpp:214`, `:577` `shooter_of` | claim candidate | MI only in practice | moves |
| `playlog.cpp:525`, `:561` `pl_shot` | shot owner | MI only in practice | moves |
| `playlog.cpp:617` `pl_sound`, `:644` `pl_remote_source`, `:804` | sound owner | if a form plays a sound | moves |
| `fx_c136.cpp:335` `owner_owner` | MI class filter | MI filter after | moves |
| `fx_c136_net.cpp:183` | first-person kind, MI gated | returns None for a bug either way | stays |
| `input_diag.cpp:1168` (c160), `:1533` (c21) | diagnostic lines | yes | move |
| `input_diag.cpp:1571` (c29) | MI filter | no | moves, MI filter kept |
| `input_diag.cpp:1631` `c43_refuse_tag` | downed pawn's press refusal | yes: a downed bug's form press is not refused today | moves |

Not weapon readers (pawn to controller, vehicles, enemies), left as they are: `enemy_c103.cpp:131`,
`fire.cpp:625` (`c76_mech`), `vehicles.cpp:719`, `player_c99.cpp:138`, `:627`, `attributes.cpp:106`,
`fx_c136_net.cpp:131`.

Proposed helper, in `hooks_ubw.cpp` beside `c24_inventory`, declared in `hooks_internal.h`:

```cpp
// The player pawn a weapon belongs to: the owning inventory's component owner (AADWeapon +0x2D8, set by
// InitialiseWeaponItem for MI guns and bug forms alike), else AActor::Owner (vehicles, turrets, other actors).
const void* weapon_pawn(const void* actor) {
    if (actor == nullptr) return nullptr;
    if (is_a(actor, g_player_character_class())) return actor;
    if (g_c50_weapon_class != nullptr && is_a(actor, g_c50_weapon_class)) {
        const void* const inventory = read_ptr(actor, kWeaponOwnerInventory);
        const void* const pawn = inventory != nullptr ? read_ptr(inventory, kComponentOwner) : nullptr;
        return is_a(pawn, g_player_character_class()) ? pawn : nullptr;
    }
    return read_ptr(actor, kActorOwner);
}
```

`kWeaponOwnerInventory = 0x2D8` moves from the local `kC132WeaponOwnerInventory` (`vehicles.cpp:1347`) to
`hooks_internal.h`; `g_c50_weapon_class` is the `AADWeapon` StaticClass (`hooks_ubw.cpp:2321`). Callers that start
from an ASC pass `read_ptr(asc, kComponentOwner)`. Every "moves" row above replaces `read_ptr(weapon, kActorOwner)`
with `weapon_pawn(weapon)`; an MI gun gives the same pawn as before (its SetOwner pawn is the inventory's owner).

### 3. The bug pawn's copies

- FormsInventory (+0xA30) is non-null on every copy: card022 lists "component property FormsInventory" for the
  client-own pawn and the remote ("other") pawn on the client, and for both pawns on the host.
- Forms per copy: `ASTPlayerCharacterBug::LoadAbilitySystemComponent` (0x051CDB60) calls `LoadInventory` on
  `[this+0xA30]` at 0x051CDBA0 with no role test before it, and it is the only direct caller. The host's server copy
  has forms (spawn lines 118556955 and 118556956). On the client the remote copy's grants carry "source STBugForm
  handle -1" (AddAbilityToActor #4 to #6), and no form spawn line follows the copy's spawn (118557778, LocalRole
  1), but the spawn lines are capped (`kC98FirstLines` = 2), so whether the remote copy equips a form is not
  shown by r1.
- Root: the `ASTWeapon` ctor (0x05227160) makes a SkeletalMeshComponent and calls SetRootComponent (VA
  0x145227279); the `AADWeapon` ctor makes no scene component and the form class is the native `STBugForm` (no
  blueprint subclass). Every card098 form line is at (0, 0, 0), which `actor_location` returns for a null root
  (`ue/actor.cpp:62`), and no hide, show or attach row names an STBugForm. **A form has no root on any copy, so the
  bug part of `c33_hide` has nothing to hide: "not needed".** The pawn's own visible body is a bug mesh on the pawn,
  which card 033 does not touch. The r2 line below confirms the null on each copy and names which step is null.

### 4. `c43_enter_down` (now `hooks_ubw.cpp:1093`)

- The gate is `hooks_ubw.cpp:1106`: `void* const weapon_asc = is_a(pawn, g_player_mi_class()) ? c24_weapon_asc(pawn,
  &weapon) : nullptr;`. A downed bug pawn gets only its pawn ASC cancelled; the equipped form's ASC keeps any active
  ability (BP_BugForm_MeleeAbility_Warrior_C) running through Downed.
- Fix: `void* const weapon_asc = c24_weapon_asc(pawn, &weapon);`. `c24_inventory` returns the MI inventory or
  FormsInventory and null for anything else; a mech returns before this line.

### 5. The host side of a bug press

- `chan_ops.cpp:10` `c26_host_fire`: `c44_host_fire` (`death.cpp:1504`) is MI gated but only logs; the mech test
  passes; the rider test uses `c196_inventory_at` (fine for a bug); `soldier` (`chan_ops.cpp:29`) is MI, so a bug's
  rounds are 0 and the release goes at once, which is right for melee.
- `c24_relay` (`fire.cpp:264`): `c24_weapon_asc` (`hooks_ubw.cpp:628`) then `c24_inventory` then
  `g_get_equipped_weapon` then `kWeaponAsc` gives the server copy's form ASC; `c21_health` reads the bug's Health
  (400 in r1); the tag is `c24_kind_tag(kFirePrimary)` = InputTag.Fire, the tag the bug's left button presses in
  r1; `g_orig_asc_tag_pressed` on the form ASC. Nothing on the fire path still tests MI in a way that blocks a bug.
- One MI test blocks a bug on the Use side: `chan_ops.cpp:36` `c26_host_use` passes the press to
  `c43_receive_use` only when `is_a(pawn, g_player_mi_class())`. A bug client's revive or Use press is dropped.
- r1 never reached the host because the client sent no Fire frame (`c26_own_fire_pawn` read the form's null Owner).

### Proposal for r2

1. Helper `weapon_pawn` as above; `kWeaponOwnerInventory` to `hooks_internal.h`; the "moves" sites in the table
   use it.
2. `c196_form_root_line`: `C33Slot.form` starts at `INT32_MIN` (`remote_body.cpp:710`), and the key is the root's
   id when there is a root, else -2 (inventory null), -3 (form null) or -4 (root null). `c33_hide` passes the
   inventory. The line, logged once per key change, within `kC196FormLines`:
   - root present: "card196: <side> form root of <pawn>: form <form> (<class>), root <root>; before bVisible V
     bHiddenInGame H, after bVisible V bHiddenInGame H"
   - otherwise: "card196: <side> form root of <pawn>: <inventory null|form null|root null>, inventory <name>, form
     <name> (<class>)"
   Since the form has no root (section 3), the bug branch of `c33_hide` changes nothing else; it stays as a
   logged no-op for this run and is removed if r2 shows "root null" on every copy.
3. `c43_enter_down`: the gate in section 4, with its fix.
4. `c26_host_use`: `is_a(pawn, g_player_character_class())` in place of the MI test, so a bug's Use reaches
   `c43_receive_use`. It is not proven in r2 (no Use target, below).

### Scenario c196-r2 (`tools/scenarios/c196-r2.txt`, to be written with the build)

- The body is c196-r1.txt unchanged except: the header names c196-r2 and the run line drops `UBWCOOP_GOD=1` from
  `HOST_EXTRA_ENV` and sets `GOD=0` (`run_pair.sh:131` defaults `UBWCOOP_GOD=1` on both sides). The r1 WARN
  "card043: host real death: 5 hits, Health 1.0" (`death.cpp:837`) is `c43_kill_host` running out of tries because
  `c132_god_block` (`death.cpp:394`) puts back each real-death hit on the god host. With god off the hits land.
  `UBWCOOP_GOD_HOST_ONLY` is not a fix: it keeps god on the host pawn.
- Risk with god off: BP_AI_Trooper_Morita_C are on BUG_C1M1 (card098 collision lines) and can hurt the bug pawns
  before the down step. A real pawn death before line 51 shows as a card043 line before the scripted down.
- Use press: BUG_C1M1 has no placed interactable in r1 (no InteractionCollider row after the bug-map LoadMap,
  host log from line 3510), so no Use target is known and **the c50 Use path on the bug map stays unproven**.

### Predictions, c196-r2

- P1: as r1: no client "WARN card073: client t=N <name>: no SourceItem property, SourceItem not set"; the three
  binds "card073: client t=N grant GA_CharacterJump_C bound to replicated spec hN (1 tags)",
  BP_BugForm_VampirismAbility_Warrior_C (0 tags), GA_BugTaunt_C (2 tags).
- P2: after `client key LeftMouseButton 800`: client "card196: client t=N bug form press InputTag.Fire on STBugForm...
  (STBugForm) of BP_PlayerCharacterBug_C... (client-own)", client "card026: client t=N client sent Fire 1 (press)",
  and host "card024: host relay press (Fire 1) t=N: pawn BP_PlayerCharacterBug_C..., weapon STBugForm..." (the
  server copy's equipped form).
- P3: no card192 line naming "c24 weapon inventory", "c33 weapon hide" or "c50 Use relay inventory"; no "WARN
  card196" line.
- P4: on the host, "card196: host form root of BP_PlayerCharacterBug_C... (the server copy): root null, inventory
  STBugFormInventory..., form STBugForm... (STBugForm)"; on the client, for the remote copy, the same "root null"
  text, or "form null" if the remote copy equips no form. No line with a root present on either side.
- P5: client "card043: client t=N own pawn Up -> Downed" after `host down client`; after `host down host`, client
  "card044: client t=N StartDeath (bug override) call BP_PlayerCharacterBug_C..." and "card196: client t=N bug
  StartDeath (form ASC cancel)"; host "card043: host real death: N hits, Health 0.0" with no WARN prefix; both
  logs end in their quit lines.
- P6: "card132: client round N: actors A host, B client, mismatches 0" after the attack. WARN classes: client only
  "WARN attach: DisableThreadLibraryCalls failed"; host none, and no "WARN card043: host real death" line.

## Run c196-r2 (run 2 of 3)

Build: `dwmapi.dll` 475eb41d933cecaf (the senior's five corrections to the Trace 2 proposal). The predictions and
the scenario audit below were written after the build and before the run.

### Changes

- `hooks_internal.h`: `kWeaponOwnerInventory = 0x2D8` (moved from the local `kC132WeaponOwnerInventory` in
  `vehicles.cpp`, which now uses it); `weapon_pawn` declared; `C33Slot.form` comment; `C43State.in_kill`.
- `hooks_ubw.cpp` `weapon_pawn`: an `AADWeapon` whose owning inventory's component owner is a player character gives
  that pawn; anything else (a turret, vehicle or NPC weapon, a weapon whose inventory owner is null, any non-weapon)
  gives `read_ptr(actor, kActorOwner)`, as before. Unlike the Trace 2 sketch, a player character passed in is not
  returned as itself: it also gets its `AActor::Owner`, so every non-weapon input is unchanged.
- Sites moved to `weapon_pawn(x)` with nothing else changed: `fire.cpp` `c196_note_bug_press`,
  `c26_own_fire_pawn`; `attributes.cpp` `detour_zero_adjust`, `detour_fire_projectile`; `weapons.cpp`
  `c141_pawn_of`; `claim_c157.cpp` the two `cand[1]`; `playlog.cpp` `pl_shot`, `pl_activation`, `pl_sound`,
  `pl_remote_source` and the sound line at 804; `fx_c136.cpp` `owner_owner`; `input_diag.cpp` the c160 and c21
  holder lines, the c29 filter and `c43_refuse_tag`.
- `hooks_ubw.cpp` `c43_enter_down`: the weapon ASC is fetched when `c196_inventory_at(pawn) != 0` (MI or bug), in
  place of the MI test, so a downed bug's form abilities are cancelled. The same predicate as the rider gate, so a
  turret's weapon inventory is not reached here (`c24_inventory` would return it).
- `chan_ops.cpp` `c26_host_use`: `c196_inventory_at(pawn) != 0` in place of the MI test. Audit of `c43_receive_use`
  and its callees for MI-only reads on the pawn, none found: `c43_use_edge` reads only the roster, `c43_is_down` and
  `actor_location`; `c43_hold_step` and `c43_revive` act on roster slots; `c50_relay_use` reads
  `kCharacterInteractor` (0x858, `ASTPlayerCharacter::InteractorComponent`, the base of both pawn classes),
  `c43_is_down`, `c43_find_hold`; `c50_pick_target` reads the interactor's target, `kPawnController` and the
  card 050 name; `c50_interact` reads the inventory through `c24_inventory` (MI or bug since r1) and the
  interactor. Not proven by r2 (no Use target on BUG_C1M1; the c50 path is a card 127 watch item).
- `death.cpp`: `c43_kill_host` sets `g_c43.in_kill` around its `g_deal_damage` call; `c132_god_block` returns false
  while it is set, beside `in_reset`. The god block runs inside that call (r1: each "card132: god ... blocked" line is
  logged in the same ms as, and before, its "real-death hit" line). No existing flag covered `c43_kill_host`.
- `remote_body.cpp`: `C33Slot.form` starts at `INT32_MIN`; `c196_form_root_line` keys on the root id, or -2/-3/-4
  for inventory/form/root null, and logs the null case by name; `c33_hide` on a bug pawn logs that line and returns
  before any `c33_keep_hidden` call (no hide, show or visibility write; card 192 skipped all of `c33_hide` for a bug
  pawn, so this is the pre-card visibility behaviour). MI pawns are unchanged.
- `tools/scenarios/c196-r2.txt`: the c196-r1 body (lines 13 to 58 of r1 = lines 10 to 55 of r2, `diff` clean);
  header and run line renamed. God stays on (`UBWCOOP_GOD=1`).

### Scenario audit (`tools/scenarios/c196-r2.txt` against this build's log texts)

The body is r1's; every wait text is unchanged in this build. r2 line numbers (r1 minus 3):

| Line | Step | Log text it waits for | Source |
|---|---|---|---|
| 11-12 | wait state, both | driver state | `testrun.cpp` |
| 14-15 | client card138 / card120 (MI) | as r1 lines 17-18 | unchanged |
| 21-23 | host LoadMap, client reconnect, client LoadMap count=2 | as r1 | unchanged |
| 26-31 | card138 / card120 (bug), card098, try card049, states | as r1 | unchanged |
| 36 | host relay | "card024: host relay press (Fire 1) t=N: pawn " | `fire.cpp:199` |
| 37 | try client edge | "card026: client t=N client sent Fire " | `fire.cpp:84` via `hooks_ubw.cpp:564` |
| 38 | try client press | "card196: client t=N bug form press " | `fire.cpp:119` |
| 42 | host digest | "card132: host t=N digest round N (timer) sent: " | `digest_c132.cpp:783` |
| 43 | client digest | "card132: client round N: actors " | `digest_c132.cpp:1303` |
| 49 | client Downed | "card043: client t=N own pawn Up -> Downed" | `death.cpp` (unchanged text) |
| 51 | client bug StartDeath | "card196: client t=N bug StartDeath (form ASC cancel)" | `death.cpp:1462` |
| 52 | try death screen | "card044: client t=N death screen " | `death.cpp:1568` |

### Predictions, c196-r2

- P1: no client "WARN card073: ... no SourceItem property"; the three binds as r1 (GA_CharacterJump_C 1 tags,
  BP_BugForm_VampirismAbility_Warrior_C 0 tags, GA_BugTaunt_C 2 tags).
- P2: client "card196: client t=N bug form press InputTag.Fire on STBugForm_N (STBugForm) of
  BP_PlayerCharacterBug_C_N (client-own)"; client "card026: client t=N client sent Fire 1 (press)" (the argument as
  `c26_edge_line` prints it); host "card024: host relay press (Fire 1) t=N: pawn BP_PlayerCharacterBug_C_N, weapon
  STBugForm_N".
- P3: no card192 line naming "c24 weapon inventory", "c33 weapon hide" or "c50 Use relay inventory"; no "WARN
  card196" line.
- P4: host "card196: host form root of BP_PlayerCharacterBug_C_N: root null, inventory <FormsInventory component>,
  form STBugForm_N (STBugForm)" for the server copy; client the same "root null" text for the remote copy, or "form
  null" if that copy equips no form. No root-present line, and no card033 hide line naming a bug pawn, on either side.
- P5: client "card043: client t=N own pawn Up -> Downed"; then client "card044: client t=N StartDeath (bug override)
  call BP_PlayerCharacterBug_C_N" and "card196: client t=N bug StartDeath (form ASC cancel)"; host "card043: host
  t=N real-death hit 1 on BP_PlayerCharacterBug_C_N (causer ...): Health 1.0 -> 0.0" (or below 0) with no card132
  god block line for it, then "card043: host real death: N hits, Health 0.0" with no WARN prefix. Both logs end in
  their quit lines.
- P6: "card132: client round N: actors A host, B client, mismatches 0". WARN classes: client only "WARN attach:
  DisableThreadLibraryCalls failed" (as r1); host none (r1's "WARN card043: host real death" gone).

### Results, c196-r2

Build 475eb41d933cecaf (`sha256sum | cut -c1-16`), /W4 /WX clean. Run 73 s; host rc 0, client rc 0, drive rc 0; no
crash dumps. Host log 4728 lines, client log 4035 lines. Object names print without a `_N` suffix in this build, so
the predicted `_N` names appear as the bare names below.

- P1 met. No client "no SourceItem property" WARN. Client 3122 "card073: client t=119675824 grant GA_CharacterJump_C
  bound to replicated spec h179 (1 tags)", 3124 "grant BP_BugForm_VampirismAbility_Warrior_C bound to replicated
  spec h180 (0 tags)", 3126 "grant GA_BugTaunt_C bound to replicated spec h181 (2 tags)".
- P2 met. Client 3558 "card196: client t=119675975 bug form press InputTag.Fire on STBugForm (STBugForm) of
  BP_PlayerCharacterBug_C (client-own)"; client 3560 "card026: client t=119675975 client sent Fire 1 (press)"; host
  4177 "card026: host t=119675983 host received Fire 1 (BP_PlayerController_C)"; host 4180 "card024: host relay press
  (Fire 1) t=119675983: pawn BP_PlayerCharacterBug_C, weapon STBugForm". The release pair is host 4235/4236.
- P3 met. No card192 line on either side, and no "WARN card196" line.
- P4 met. Host 4043 "card196: host form root of BP_PlayerCharacterBug_C: root null, inventory FormsInventory, form
  STBugForm (STBugForm)" (server copy). Client 3545 has the same "root null" text for the remote copy. One line per
  side (the capped key held), no root-present line, and no card033 hide line naming a bug pawn on either side. The
  line text has no side tag, so the copy's side is from the c33_hide call site (remote bodies only), not the line.
- P5 missed (host part). The client part is met: client 3841 "card043: client t=119702338 own pawn Up -> Downed, 30 s
  bleedout"; 3860 "card044: client t=119702477 StartDeath (bug override) call BP_PlayerCharacterBug_C (client-own,
  role 2/3)"; 3861 "card196: client t=119702477 bug StartDeath (form ASC cancel)". The host downs ran (4612
  server-copy Downed, 4628 host-own Downed, 4636 server-copy Out, 4641 host-own Dead). The host real death did not
  finish: host 4696 "WARN card043: host real death: 5 hits, Health 400.0".
  - Cause, from the logged lines: the in_kill flag worked for the Health put-back (no c132_god_block line around the
    hits), but the second god layer, c43_death's HandleDeathEvent detour (death.cpp:599-615), has no in_kill test.
    Each hit drives Health below 0 and then the detour skips the death event and restores Health to max. Hit 1:
    host 4645 "playlog: ... hit slot 0 host-own: damage 4000.00 ... Health 1.0 -> -3599.0", then 4648 "card132: god
    host HandleDeathEvent on BP_PlayerCharacterBug_C (host-own) skipped, Health back to max", then 4649 "card043: host
    t=119702443 real-death hit 1 ...: Health 1.0 -> 400.0". Hits 2-5 repeat the pattern (the same skip line at 4658,
    4665, 4688 and 4694).
  - Proposed fix, not applied (no second build): add `!g_c43.in_kill` to the god test in c43_death, next to the test
    already added in c132_god_block. No other flag covers c43_kill_host.
  - Both logs end in their quit lines: host 4700/4703 "cmd: 6 quit", client 4017/4022 "cmd: 3 quit".
- P6 met. Client 3834 "card132: client round 1: actors 1076 host, 1076 client, mismatches 0".

WARN classes against c196-r1:
- Client: only "WARN attach: DisableThreadLibraryCalls failed, err=126" (line 3), as in r1.
- Host: "WARN attach: DisableThreadLibraryCalls failed" (line 3), as in r1, and "WARN card043: host real death" (line
  4696), also as in r1 but now "Health 400.0" (r1: "Health 1.0"). No new class.

Unproven after r2:
- The Use path for a bug pawn (c26_host_use with the new inventory gate, then c43_receive_use and c50): no Use press
  in the scenario, so this is a watch item on card 127.
- The host real death of a bug pawn under the test god (P5 host part), until the c43_death fix above runs.
- Which side the client's form root line names (the line carries no side tag).

## Senior review (2026-11-12)

Accepted on c196-r2, DLL `475eb41d933cecaf`. Diff read against the pre-card snapshot: every moved site is
`weapon_pawn(x)` only, non-player weapons fall back to `AActor::Owner`, `c33_hide` writes nothing on a bug pawn,
no function over 60 lines added. Logs checked: P1 to P4 and P6 met, P5 client part met (bug StartDeath, CompleteDeath
3011 ms later, no crash); client WARN classes equal r1, host has no new class.

Open, moved on:
- Host "WARN card043: host real death: 5 hits, Health 400.0" under the test god: the HandleDeathEvent god layer in
  `c43_death` has no `in_kill` test. Test tool only (no god in the play build). The fix goes into card 208's build
  and its run proves it.
- Bug-pawn Use and revive path (`c26_host_use`, `c43_receive_use`, c50): no target on BUG_C1M1; watch item on card 127.
- Form switch relay: card 208.
