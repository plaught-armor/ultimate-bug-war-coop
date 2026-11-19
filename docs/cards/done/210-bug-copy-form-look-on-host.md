# Card 210: The host sees a client's bug form (body, abilities) as the client has it

Status: queued, static first. Split from card 208 (2026-11-12). One goal: after a client switches form, the
host's screen shows that client's pawn in the new form.

## Known (card 208, runs c208-r2 and c208-r3, DLL a8188057ac1b264a)

- The host equips the server copy's form with `c208_copy_equip` (`EquipWeaponByClass` under `g_c50.applying`);
  the host logs `weapon slot 1 server-copy: STBugForm -> STBugForm (BP_BugFormDefinition_Hopper_C)` and the
  copy's Hopper Jump, Glide and DamageVolume abilities run.
- Host shot `host-c208-r3-c208-host-two.png` (prefix Screenshots folder): the copy is still a Warrior body. The
  same call on the client for the host's pawn did show Hopper wings (client shot in c208-r2).
- `ASTBugForm::OnEquip` writes the pawn's mesh and anim set at +0x750 / +0x758 (card 208 trace). The switch on
  (Corrected by the trace below: +0x750..+0x778 are movement curves and camera shakes; the body form is the anim
  blueprint's `CurrentForm`, set from the pawn's replicated `BugForms.*` tag.)
  the owner goes through `GA_BugForm_EquipAbility`, which the copy path skips: no Invulnerable window, no
  CommitAbility, no Tanker self GE (Trace 2 d).
- Client `WARN card073: grant BP_BugForm_SwoopAbility_Hopper_C: no replicated spec after 10000 ms` in c208-r3;
  the host log has no Swoop line for the copy, the other four Hopper grants bound.
- A host switch 512 ms after a warp, while falling, gave no activation (EquipAbility blocks Falling, Flying,
  Gliding on the owner). Not a mod fault as far as known; the scenario must switch on the ground.

## Do

1. Trace, static: what changes the body on a form switch (who reads +0x750 / +0x758, whether the mesh swap runs
   from OnEquip, from the equip ability's animation end, or from an anim notify), and why it ran on the client's
   copy of the host pawn but not on the host's server copy. Include card 028's body handling for the copy. Name
   why the copy's Swoop spec is missing (grant condition on the host). Senior reviews before code.
2. One fix for the body, one for Swoop if it is a mod fault. State what the skipped equip ability leaves out on
   the copy and whether the Invulnerable window matters with a client's own prediction.
3. One run, c208-r3's scenario with the host on the ground for its own switch and the copy in frame. Proof is the
   host screenshot of the client's pawn as a Hopper, plus zero `card073` WARN.

## Done when

Host screenshot shows the client's Hopper, client screenshot shows the host's Hopper, both switch back, no new
WARN class against c208-r3 minus the classes carded as 211 and 212.

## Trace (junior, 2026-11-12)

Static only: no code, build or run. Sources: the exe disassembly (`ASTBugForm::OnEquip` RVA 0x05244E20), kis
dumps of `ABP_PlayerBug`, `GA_BugForm_EquipAbility` and the Hopper abilities, asset name tables, the c208-r3 logs
and shots, and `mod/src/ubw/grenades.cpp` / `death.cpp` (card 073) and `remote_body.cpp` (card 028).

### 1. What swaps the body

- Verified: `ASTBugForm::OnEquip` (0x05244E20) makes no mesh, anim-class or visibility call. It writes six pawn
  fields +0x750..+0x778 from the form definition (+0xC0..+0xE8), with `USTCharacterData` (+0x50..+0x78) as the
  fallback; +0x760 and +0x770 are `UCameraShakeBase` classes. The definitions' name tables hold only movement
  curves (`BreakingFrictionFactorCurve`, `GroundFrictionCurve`), camera shakes (`CS_Shake_Walk_Warrior`,
  `CS_Shake_Sprint_Warrior`), abilities, effects and a HUD icon, so +0x750 / +0x758 are movement curves, not a
  mesh or anim set. The "Known" line above that calls them mesh and anim set is wrong. Open: the exact field to
  curve mapping (no xref sweep done).
- Verified: `GA_BugForm_EquipAbility` has no mesh call (EquipWeaponByInstance @600, K2_CommitAbility @211, HUD
  progress bar @428, OnAnimationEnded, then Delay @736 and K2_EndAbility @186). `BP_PlayerCharacterBug` has no
  SetSkeletalMesh or SetAnimInstanceClass.
- Verified: the body is one skeletal mesh (`SKM_AssassinBug_v6-4`, anim class `ABP_PlayerBug`) for every form.
  The form look is the anim graph's choice: `ABP_PlayerBug.GenPlayerForm` sets `CurrentForm` from the pawn's
  `HasMatchingGameplayTag`, checking in this order: `BugForms.Warrior` gives 3 (@0..88), `BugForms.Tanker` gives
  2 (@113..201), `BugForms.Hopper` gives 1 (@226..314), and no tag gives 0 (@339). It runs on every
  BlueprintUpdateAnimation (ubergraph @2929, GenPlayerForm @3053). The only gates are `IsValid(PlayerBug)` @2958
  and `IsValid(PlayerBug.Mesh)` @3019; there is no locally-controlled or role test. `CurrentForm` then feeds the
  form blend list (@1616 `__ByteProperty_14 = CurrentForm`) and the per-form state machines (Hopper_FlightLoco,
  Hopper_GroundedIdle, Hopper_GroundedLoco, Warrior_*, Tanker_*).
- Verified: the form tag comes from the definition's passive effect.
  `BP_BugForm_PassiveModifiersEffect_Hopper` carries `BugForms.Hopper` (with `PlayerAbilities.Gliding.CanGlide`),
  and `..._Warrior` carries `BugForms.Warrior`. So a form switch changes the body when the pawn's ASC loses the
  old form's tag and gains the new one. The FormIn/FormOut montages (`DA_PlayerCharacterBug_Anims`:
  `HopperFormIn`, `WarriorFormOut` and so on) are the transition only; they are played by the equip ability's
  native parent, which the copy path skips.

### 2. Why the client saw the host's Hopper and the host still sees a Warrior copy

- Verified: on the client, the host's pawn reads replicated ASC tags. The host's own switch runs on the authority,
  so the passive GE's `BugForms.Hopper` replicates and the client's `ABP_PlayerBug` picks form 1. The client's
  relayed `EquipWeaponByClass` is not what changed that body; the ABP reads tags only.
- Verified: on the host, the copy's tags did change. The card114 instigator at host 122855644 (after the copy's
  equip at 122851951 and before its switch back at 122884828) has `BugForms.Hopper`,
  `PlayerAbilities.Gliding.CanGlide` and `MovementMode.CustomPlayerBug.Gliding`, and no `BugForms.Warrior`.
  This pawn is the copy: host-own never switched (it has no card208 equip line after 3800) and carries
  `BugForms.Can.Hopper`. So GenPlayerForm on the host should already give the copy form 1.
- The host-two shot was taken at host 122884571, inside that Hopper window. It shows a grounded body with the
  wings folded. Open: whether this is the ABP's Hopper grounded state (wings folded) and not the Warrior state.
  The client's own view in the same shot pair has its wings spread (flight or glide pose on its machine). The
  copy on the host may be in Walking while the client glides; the movement mode drives Hopper_FlightLoco against
  Hopper_GroundedIdle. Not settled statically: the shot alone cannot tell a grounded Hopper from a Warrior.
- Card 028 on bug pawns, verified from the logs: card 028 attaches a FemaleMoritaTrooper sprite renderer and
  animator to the remote bug pawn on both sides (host 4196 `attached to BP_PlayerCharacterBug_C ... role 3`,
  client 3601 `role 1`) and drives its keys (host 122852398 `key Idle -> Attack01`). `c33_hide` writes no
  visibility for bug pawns. The shots show no Morita sprite on the bug, so it is not the body seen, but it is a
  Human-side body on a bug pawn. Open: whether that sprite renders anywhere.

### 3. Why the copy's Swoop has no spec

- Verified: the WARN is on the client, not the host. `c73_client_step` (death.cpp ~2990) drops a wait after
  `kC73WaitMs`. The host has no grant lines in c208-r3 (card073 grant logging was off there).
- Verified: the handles say the host did grant Swoop. The client bound Jump h197, Glide h198, DamageVolume h200 and
  Taunt h201 (client 4143..4152), in the definition's grant order Jump, Glide, Swoop, DamageVolume, Taunt.
  h199 is the gap, most likely Swoop's spec.
- Verified (code): `c73_bind` (grenades.cpp ~150) returns false when `c73_point_item` returns false, and
  `c73_point_item` returns false whenever the spec has no instance yet (`c73_spec_instance` null), before it ever
  tests for `SourceItem`. An ability that is not instanced per actor (instanced per execution, or non-instanced)
  has no instance until it activates, so its wait never binds, even though the spec has replicated.
- Open: Swoop's `InstancingPolicy`. The CDO is unversioned (68 bytes) and the schema order was not decoded. The
  other four bind, which fits Swoop being the only one not instanced per actor.

### 4. What the skipped GA_BugForm_EquipAbility leaves out on the copy

- Damage: there is no `HealthAndDamageSystem.Invulnerable` window on the copy during AnimationTime +
  PostAnimationDelayTime (the host can hurt the copy mid-switch while the owner's own pawn is invulnerable), and
  no Tanker self GE. Card 208 Trace 2 d covers both.
- Seen: no FormIn/FormOut montage on the copy (the switch pops), no CommitAbility, and no HUD bar (the HUD bar
  only matters on the owner).

### Proposed fixes (one per finding)

1. Body and 4: after `c208_copy_equip` on the host, run the form's real equip ability on the server copy instead
   of only `EquipWeaponByClass`. For example, activate the definition's EquipAbility class on the copy's ASC
   (`TryActivateAbilityByClass`) and let it do the `EquipWeaponByInstance`. That gives the Invulnerable window,
   the Tanker self GE and the FormIn montage on the copy. Pawn: the server copy, host side only.
   Hold it until the run shows the copy's `CurrentForm`: if it is already 1 at the shot, the look gap is the pose
   or the montage, and this fix is the one. If it is 3, the Warrior tag lingers and the fix is in the unequip path.
2. Swoop: in `c73_point_item`, test for `SourceItem` on the spec's ability class (`read_ptr(spec, kSpecAbility)`)
   before needing an instance. When that class has no `SourceItem`, return true at once, so a bug form feature
   binds on the replicated spec without an instance. Pawn: the client's own pawn, client side.
3. Card 028: exclude bug pawns from `c28_visit` (no Morita sprite on a `kBugFormsInventory` pawn) once confirmed
   that the sprite serves no purpose there. A side finding, not this card's goal.

### Predictions for one run (c208-r3 scenario, host on the ground for its switch, copy in frame)

- A host diag line at each shot gives the copy's tags and its `ABP_PlayerBug.CurrentForm`: 1 while Hopper, 3
  after the switch back, with no `BugForms.Warrior` while Hopper.
- With fix 1: a host `HealthAndDamageSystem.Invulnerable` tag on the copy for about AnimationTime +
  PostAnimationDelayTime after each copy switch, and the host shot showing the copy with Hopper wings.
- With fix 2: client `card073: ... grant BP_BugForm_SwoopAbility_Hopper_C bound to replicated spec h199`, with
  zero `WARN card073`.

### Unknown

- Swoop's InstancingPolicy (fix 2 depends on its spec having no instance at grant).
- Whether the host-two shot body is the Hopper grounded pose or the Warrior state (needs the CurrentForm diag).
- The exact +0x750 / +0x758 curve mapping.
- Whether card 028's Morita sprite on bug pawns renders.

## Senior decision on fix 1 (2026-11-12)

No hand activation of the equip ability. On an Equip frame for a bug server copy the host lends the copy the
loose tag the owner already holds (`BugForms.Can.Hopper` for index 1, `BugForms.Can.Tanker` for index 2), calls
`g_equip_weapon(pawn, index)` as the MI path does, and removes the tag again if it added it. The whole vanilla path
then runs on the copy. `c208_copy_equip` is removed.

## Check A (junior, static): verified, the lent tag works and can be removed straight after the call

- Add and remove one loose tag: the mod already holds what `UAbilitySystemComponent::UpdateTagMap_Internal`
  (RVA 0x4977630) does per tag. First `FGameplayTagCountContainer::UpdateTagMap_Internal(asc + 0xF18, &tag,
  delta)` (RVA 0x4A07F40, `g_c131_update_tag_map`, `TagMapFn`), which returns true when the tag's existence
  changed. When it does, the ASC's tag-changed vfunc +0x960 `(asc, &tag, count)` runs, with count 1 on add and 0 on
  remove (`TagChangedFn`, `kTagCountContainer`, `kTagChangedVfunc` in hooks_internal.h; the same pattern as
  `c131_remove_dying_tag`). No new symbol.
- The tag value: an `FGameplayTag` is one FName (8 bytes). `fname_cached(cache, L"BugForms.Can.Hopper")` makes it
  through `FName_CtorFindName` with no heap; `RequestGameplayTag` is not needed. "Already held" is read from the
  owned tags (`ExplicitTags`, ASC +0x1020: data, Num +8, Max +0xC).
- The test runs inside the `g_equip_weapon` call, synchronously:
  1. `EquipWeapon` (0x51C57A0) calls vfunc +0xAA8, `SendWeaponSwitchRequest` (0x51DB320).
  2. That calls `SendGameplayEventToActor` (0x49710F0).
  3. That calls ASC vfunc +0x740, `HandleGameplayEvent` (0x4961CD0).
  4. That calls `InternalTryActivateAbility` (0x4963860).
  5. At 0x144963a70 that calls ability vfunc +0x2d8, `ShouldAbilityRespondToEvent`. The
     `USTWeaponSwitchAbility` override is at 0x5137FB0, and at 0x145138093 it calls `CanSwapToWeapon`
     (0x50b7cb0).
- A .text scan finds exactly one caller of `CanSwapToWeapon` (0x5138093).
- After activation no step rereads `CanSwapToWeapon`:
  - `USTWeaponSwitchAbility::ActivateAbility` (0x5115070) is latent (`WaitDelay` when CancelGracePeriod +0x3b8 >
    0, then `BeginWeaponSwitch` 0x5118D70).
  - `SwitchOutPreviousWeapon` (0x513A5A0) and `SwitchInNewWeapon` (0x513A2A0) send events and
    `WaitGameplayTagRemove`.
  - In the GA_BugForm_SwitchAbility BP, the second set of Can tests (offset 588+) is the loop back after the HUD
    flash, not a later recheck.
  - The equip abilities hold no Can tag strings.
  So the tag is removed straight after the call.
- `ASTCharacter::GetOwnedGameplayTags` (0x50EAF40) forwards to the ASC's tag interface, so the BP's
  `HasMatchingGameplayTag` on the pawn reads the ASC's owned tags, which hold the lent tag.

## Check B (junior, static): Swoop is instanced, so fix 2 is not applied

- `BP_BugForm_SwoopAbility_Hopper`'s super is `ADGameplayAbility`. The CDO does not serialize `InstancingPolicy`,
  so the native ctor default holds.
- `UGameplayAbility`'s ctor (0x4925750) at 0x144925c38 does `mov word ptr [rbx+0xcf], 0x102`, which sets
  `InstancingPolicy` (+0xcf, the byte `GetInstancingPolicy` 0x492DB50 reads) to 2, InstancedPerExecution.
- So the spec has no instance until Swoop activates. `c73_spec_instance` stays null and `c73_point_item` returns
  false until then: that is the client's `WARN card073 ... Swoop ... no replicated spec`.
- The order said that if Swoop is instanced, fix 2 is not applied. Swoop is instanced, so fix 2 is not applied
  and this item is open for the senior. In fix 2's form, the `SourceItem` test on the spec's ability class would
  still be valid for an InstancedPerExecution ability (it has a CDO, and no instance until it runs), but that is
  the senior's call.

## Code (junior, DLL a6a918954db931f2)

- `weapons.cpp`:
  - `c210_copy_equip` replaces `c208_copy_equip` (lend the tag, `g_equip_weapon`, remove the tag). It writes one
    line per Equip: `card210: host t=... Equip %u from %s: tag %s %s, EquipWeapon on the server copy, equipped
    %d -> %d`. The tag state is "added and removed", "already held" or "none needed", and the index after the call
    is read at once (the switch is latent, so it may still be the old one).
  - `c210_track`: a capped WARN when the copy's equipped index has not reached the wanted index
    `kC210EquipLateMs` = 1500 ms after the call. This is a deviation from "the next c208_track step": c208-r2 took
    484 ms from key Two to equipped on the host's own pawn (CancelGracePeriod and the montage), so a 200 ms step
    would warn on every switch.
  - `c210_form_look`, both sides, per tracked bug pawn on an equipped change and again 1 s later:
    `card210: <side> form look of <pawn> (<side text>): equipped %d, CurrentForm %d, tags <BugForms.*>,
    Invulnerable %d (<on change|+1 s>, CurrentForm at +0x%X)`.
    - `CurrentForm` is found by name in the anim class's reflected properties (`ABP_PlayerBug`'s byte variable;
      size checked to be 1, offset logged), on Mesh (+0x328), else FirstPersonMesh (+0x848).
    - `Invulnerable` is `HealthAndDamageSystem.Invulnerable` in the ASC's owned tags (-1: no ASC). The
      prediction needs it, and the BugForms.* tag list does not show it.
  - Caps: card210 lines 60 per run and WARNs 8 per run, through `note_event_cap`. The new row fields are in the
    existing C208Row table, so there is no new table.
- `death.cpp`: card 211 (see that card).

## Run c210-r1 predictions (written before the run)

Scenario `tools/scenarios/c210-r1.txt`. Pawn per line in brackets.

1. Client Two:
   - host `card210: host t=... Equip 1 from ...: tag BugForms.Can.Hopper added and removed, EquipWeapon on the
     server copy, equipped 0 -> 0` (server copy; "-> 0" because the switch is latent).
   - host `playlog: ... ability slot 1 server-copy: GA_BugForm_SwitchAbility_C`, then
     `... BP_BugForm_EquipAbility_Hopper_C` (server copy).
   - host form look "on change" of the server copy: `equipped 1`, `Invulnerable 1`.
   - host form look "+1 s": `equipped 1, CurrentForm 1, tags` with BugForms.Hopper and without BugForms.Warrior.
     The "on change" line may still show CurrentForm 3, because the anim graph reads the tags on its next update.
   - client form look "+1 s" of its own pawn (client-own): `equipped 1, CurrentForm 1`.
   - no `WARN card210`.
2. Host shot c210-client-two (taken with the client on the ground): the client's pawn has Hopper wings.
3. Host Two: client form look "+1 s" of the host pawn (other): `equipped 1, CurrentForm 1`. Host form look of
   host-own: the same.
4. Both switch back (client One, then host One): every "+1 s" line shows `equipped 0, CurrentForm 3`. The client
   One host line reads `tag - none needed`.
5. Swoop: expected to miss. Check B shows Swoop is InstancedPerExecution and fix 2 is not applied, so client
   `WARN card073: grant BP_BugForm_SwoopAbility_Hopper_C: no replicated spec` stays (client-own). This is a known
   miss, not a new finding.
6. Real death (card 211): host `card211: host t=... real-death hit 1: landed the self-caused hit (the causer's hit
   left Health unchanged)` when a bug NPC is the nearest enemy, or `the causer's hit` or `the self-caused hit (no
   causer found)`. Then `card043: host real death: N hits, Health <= 0` with no WARN.
7. 0 mismatches. WARN classes per side equal c208-r3's minus card043. card073 (Swoop) stays, see 5. card132 impact
   and card148 gib lines are known and carded.

## Run c210-r1 scenario audit

- Every state-dependent step waits on its log line: the Equip line, both ability activations, the two form look
  lines per switch per side, the card208 send and receive lines, the weapon slot lines, the Jump activations, the
  Downed, StartDeath, card211 and card043 lines.
- Host position: there is no warp. The host's `look actor BP_PlayerCharacterBug_C` skips its own pawn
  (`c64_find_actor(..., pawn)`). Its result line gives the client pawn's place (`cmd: N look: ... at X Y Z from`),
  and `host goto {g1} {g2} {g3} 400` walks there and waits for the walk's end. `goto actor` does not skip the own
  pawn, which is why it is not used. So the host is on the ground and unwarped for the client's switch and for
  its own.
- The receiver-side shot comes after both "+1 s" lines and before the client's Jump, so the client stands on the
  ground with the copy in front of the host. The host-two shot also comes before the host's Jump.
- The driver has no landing log line, so a walk replaces the warp (the order's second option).
- The real death has no NPC spawn. FED_C2M1 had `BP_AI_WarriorBug_C` near the host in c208-r3 (the causer of
  every r3 hit), which is the case the fix targets. If no bug NPC is near, the line reads "no causer found" and the
  211 bug-causer branch stays unproven.

## Run c210-r1 result (senior review)

The lent-tag equip and card 211 are met (senior, from the logs). Four items go into one more build and run c210-r2:
the client's Hopper Jump was never bound (card 073 wait ring), Swoop (fix 2 in its class form), card 213 (the card
028 sprite on bug pawns), and the scenario faults (the host's own Jump before its One; receiver-side shots hidden by
the viewer's own body).

## Code for run c210-r2 (junior, DLL 04264aa86c6b11e0)

- `hooks_internal.h`: `kC73Waits` 4 -> 16. The Hopper form grants five abilities at once, and since the vanilla
  switch on the copy all five arrive before their specs, so the fifth wait overwrote the Jump's slot in c210-r1.
- `grenades.cpp`, `c73_wait_slot`: a grant that waits takes a free slot. When all 16 are held, the oldest wait is
  dropped with `WARN card073: client t=... wait table full (16): grant <ability> dropped, input not bound`. The ring
  counter `g_c73_waits` is gone. Each new peak of held waits is logged as `card073: client grant waits peak N of
  16`. Deviation: the order asked for the peak at map end; the map reset (`c73_reset_map`) is in `sprint.cpp`, which
  is outside this build's files, so the peak is logged when it is reached (the last peak line of a map is its
  peak; the peak is per process, not per map).
- `grenades.cpp`, `c73_point_item` (fix 2): when the spec has no instance yet, the spec's ability (`kSpecAbility`,
  the class default object) is tested for a `SourceItem` property on its class. A class without one returns true
  and the grant binds now. A class with one keeps waiting as before. Swoop (InstancedPerExecution, no instance
  until it runs) now binds at once.
- `remote_body.cpp`: card 213 (see that card).
- `weapons.cpp` and `death.cpp`: unchanged.

## Run c210-r2 predictions (written before the run)

Every r1 prediction that was met, with the same lines:

1. Client Two: host `card210: host t=... Equip 1 from ...: tag BugForms.Can.Hopper added and removed`, the
   playlog activations of `GA_BugForm_SwitchAbility_C` and `BP_BugForm_EquipAbility_Hopper_C` on the server copy,
   form look of the copy "equipped -1 ... Transforming, Invulnerable 1", then "equipped 1, CurrentForm 1" on
   change and at +1 s (Invulnerable 0). The client's own form look reads "equipped 1, CurrentForm 1".
2. Host Two: the host's own form look "equipped 1, CurrentForm 1". The client's form look of the host pawn
   (other) reads "equipped 1, CurrentForm 1" on change and at +1 s.
3. Client One: `tag - none needed ... equipped 1 -> -1`, then CurrentForm 3 (Warrior) on both sides.
4. Card 211: `card211: host t=... real-death hit 1: landed the self-caused hit`, then
   `card043: host real death: 1 hits` without WARN.

New in r2:

5. After client Two the client logs `card073: client t=... grant BP_BugForm_JumpAbility_Hopper_C bound to
   replicated spec` and `... grant BP_BugForm_SwoopAbility_Hopper_C bound to replicated spec`, and a waits peak
   line of 5 of 16.
6. The client's Hopper Jump (the old line 85): host `playlog: host t=... ability slot N server-copy (client
   request)?: BP_BugForm_JumpAbility_Hopper_C`.
7. Host One switches the host back: host `card208: host t=... form 0 of ... (host-own) equipped: BugForm sent to
   1 client`, client `BugForm 0 for the host pawn ...: EquipWeaponByClass(...), equipped 1 -> 0`, form look
   "equipped 0, CurrentForm 3" for the host pawn on both sides (host-own on the host, other on the client).
8. Card 213: one `card213: <side> no card 028 renderer on bug pawn ...` line per side for the other player's
   pawn, with "renderer present 0"; no `card028: ... attached to BP_PlayerCharacterBug_C` line on either side.
9. Zero `WARN card073`, `WARN card034`, `WARN card043`, `WARN card210` and `WARN card211` on both sides; 0
   mismatches in every card132 client round; 0 failed scenario lines.
10. Shots: the host shot after client Two (`host-c210-r2-c210-client-two`) shows the client's pawn as a Hopper
    (wings), left of the frame centre, clear of the host's own body. The client shot after host Two
    (`client-c210-r2-c210-host-two`) shows the host's pawn as a Hopper in the same place. Both after One
    (`host-...-c210-client-one`, `client-...-c210-host-one`) show a Warrior. No trooper sprite on a bug in any
    shot.

## Run c210-r2 scenario audit

`tools/scenarios/c210-r2.txt` is c210-r1 with these changes only:

- The host's own Hopper Jump (r1 lines 99 to 101) is removed. From its Two to its One the host only takes look
  steps, so it stays on the ground (EquipAbility is blocked on Falling, Flying and Gliding).
- Host place: the host still reads the client pawn's place from its `look actor` line, then walks to
  `{g1+1200} {g2} {g3}` (radius 150), 1200 uu past the client along the tunnel. In c210-r1 both pawns stood on y
  -377 with the host on -x facing yaw 0, and the host shot showed the tunnel running on ahead of the client, so
  the walk goes along that tunnel. The walk is before the client's switch, as in r1; there is still no warp.
- Shot geometry. The third-person camera sits about 800 uu behind the viewer's own pawn (r1: host pawn at x
  -17332, camera at -18140), so a look straight at the other pawn hides it behind the viewer's own body. Each
  viewer therefore turns 45 deg off the line before its shot: the host to yaw 225 (the client is at yaw 180 from
  it), the client to yaw 45 (the host is at yaw 0 from it), pitch -10. With the pawns 1200 uu apart and the camera
  swinging 800 uu behind the pawn, the other pawn is about 27 deg left of the view axis, near x 160 of 640 and a
  little below the frame's middle. The viewer's own body covers about x 230 to 400 (r1 shots), so the target is
  clear of it, at about 0.4 of the own body's size.
  - `host shot c210-client-two`: client pawn (Hopper) left of centre.
  - `client shot c210-host-two`: host pawn (Hopper) left of centre.
  - `host shot c210-client-one`: client pawn (Warrior) left of centre.
  - `client shot c210-host-one`: host pawn (Warrior) left of centre.
  The sender-side shots (`client shot c210-client-two`, `host shot c210-host-two`, and the One shots of the
  switching side) are kept as they were.
- Each look step runs right before its shot pair and after all the log waits for that switch, so no state change
  waits behind it.
- Open risk, said before the run: the client's Hopper Jump now activates (it never did in r1), and there is no
  landing line. Between that Jump and the client's One are the host's Two waits (on change and +1 s form looks
  on both sides, about 2 s) plus the look and shot steps. If the client is still in the air at its One, the
  client's CanSwapToWeapon fails, no Equip is sent and the line after `client key One` fails, which would show as
  a missed prediction 3, not a code fault.
- Every other step and wait is as audited for c210-r1.

## Senior review (2026-11-12): accepted

DLL `04264aa86c6b11e0`, run c210-r2 (scenario passed, 0 failed lines), diff read against the pre-210 snapshot.
- The host lends the copy the owner's `BugForms.Can.*` tag for the one `EquipWeapon` call, so the vanilla switch
  runs on the server copy (switch and equip ability, Transforming, Invulnerable). `c208_copy_equip` is gone.
- Receiver side: form look `CurrentForm 1` on the host for the copy after client Two and on the client for the
  host pawn after host Two; `CurrentForm 3` on both sides after One.
- Client wait table 4 -> 16 (peak 5 logged); Swoop binds without an instance. All five Hopper grants bound; zero
  WARN card073, card034, card043. 0 mismatches. Function-length list unchanged.
- Unproven: the shots are small and dark (the host shot after client Two shows the client's pawn to the left,
  form not readable at 640x360); the look is for the user's playtest and card 127. Tanker switch not run.

## Run c210-r2 result fragment (found 2026-11-12 as a stray file docs/cards/210-*.md, written after the card was moved here; merged by the senior)


## Run c210-r2 result (junior)

Host rc 0, client rc 0, 91 s, drive "scenario passed" (0 failed lines, 0 timed-out try waits), DLL
04264aa86c6b11e0. Predictions 1 to 9 met (lines in the report to the senior). Prediction 10 missed: only the
host shot after client Two shows the other pawn.

- `host-c210-r2-c210-client-two`: the client pawn ("Player" name tag) at x 100 to 230, y 150 to 250, clear of the
  host's own body at x 230 to 400; a short body with raised wings and no long abdomen, a Hopper. No trooper sprite.
- `client-c210-r2-c210-host-two`: no host pawn in frame. The left third (x 0 to 235) is a near rock face; the
  centre is the client's own Hopper, tilted in the air after its Jump, inside green Jump rings.
- `host-c210-r2-c210-client-one`: no client pawn in frame. Centre: the host's own pawn (Hopper, after host Two).
  Left: an orange and black AI bug and a blue shield marker at about x 140, y 190.
- `client-c210-r2-c210-host-one`: no host pawn in frame. Centre: the client's own Warrior (long spiked abdomen)
  on a green ring. Left: the tunnel with AI bugs.

Positions known from the logs: client pawn at (-16982, -377, 2804) (client rot line at 125608680, and host cmd 4);
host goto ended 123 uu from (-15782, -377, 2804) (host cmd 5). No position line exists for either pawn at the
three missed shots, and the camera's place after a `look <yaw> <pitch>` in a narrow tunnel is not logged, so why
the targets left the frame is not proven (a rock face in front of a swung camera in the client shot, and a pawn
moved or hidden after the host's own switch in the host shot, are only readings of the pictures).

Also seen: the client's LMB as a Hopper (line 80, InputTag.Fire, Swoop) failed locally 48 times, `card021:
client local activation handle 194: failed, spec present, ability BP_BugForm_SwoopAbility_Hopper_C` and
`card024: ... handle 194 = 0, ability null`; the host logged no Swoop activation. Swoop was not in the
predictions; the spec is bound (fix 2) but the ability does not run.
