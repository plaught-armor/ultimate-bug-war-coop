# 297 Mech animations do not play for the client

Playtest 2026-11-13 (user plays as client, pilots the M7 Razorback on FED_C2M1): "mech animations are not
working". Which animations is an open question to the user (own cockpit view, or a mech seen from outside).

## Trace (client log `UBWCoop-menu-20261113-184603.log`)

| Line | Client t (ms) | Row |
|---|---|---|
| 60272-60322 | 257246294 | card 022 audit of `BP_BaseMech_C`: `FirstPersonMesh` = `SKM_RazrobackM7_CockpitArms`, AnimClass `ABP_Player_RazorbackMech_Cockpit_C`; `1PArmsSKM`; `CharacterAnimationComponent` = `PlayerAnimationComponent (ADSkeletalAnimatorComponent)`; body `Mech` is a StaticMeshComponent; `.Mesh +0x328` reads null |
| 61997 | 257271578 | `MechEnter ... sent` |
| 62170 | 257272493 | `MechEnter after: mech BP_BaseMech_C (roles 2/3) controller BP_PlayerController_C, weak PC null (its pawn null)` |
| 62360-62363 | 257276550 | `BP_BaseMech_CrushProps_C`, `CrushProps_SmallStuff_C` activate |
| 62401-62557 | 257278021 to 257286356 | 4 `BP_WeaponFireAbility_MechRazorback_C` activations, 4 `FireProjectile`, `GA_Weapon_RecoilAbility_C` |
| 62169-62969 | 257272493 to 257302628 | `client piloted mech` once per second, position moves 164527 to 169150 |

The mech drives and fires on the client. The log has no row for any mech animation: no montage, no animator row,
no `ADSkeletalAnimatorComponent` state. The mod has no mech animation code (`vehicles.cpp`: input contexts,
enter, exit, weapons, movement only).

## Cause (static; candidate, to confirm in step 1)

After the client's enter, the mech's own controller reference (`kMechController`, the weak pointer the game sets
in its enter path) reads null on the client although `GetController` is the client's PlayerController. The mod's
client enter sets possession but does not run the part of the game's enter that fills that reference and starts
the cockpit's animator. An animation blueprint or `ADSkeletalAnimatorComponent` that reads the pilot through that
reference then gets no pilot, no input and no speed, and stays in its idle or entry pose.

## Step 1 (static, junior, no run)

1. What drives each mech animation, by function and input: cockpit arms (`ABP_Player_RazorbackMech_Cockpit_C`,
   `1PArmsSKM`), walk bob and step, enter and exit, fire recoil, saw and stomp abilities, the third-person body
   seen by the other player. For each: the variables it reads (controller reference, velocity, movement
   attributes, gameplay tags, `ADSkeletalAnimatorComponent` state) and who writes them.
2. What the game's own enter (`OnInteraction` on the host's own mech) does that the client path of card 075 and
   card 255 skips: the controller weak pointer, animator start, `STMeshUpdateComponent`, any `OnRep` or
   `ClientRestart` step. Name each skipped call.
3. The same for a mech piloted by the other player as seen from outside (host sees the client's mech, client sees
   the host's): which state the body's animation needs and whether any row carries it.

Write the answers into this card before coding.

## Fix

The client's enter runs the same animation setup as the game's own enter (the calls named in step 1 item 2, by
the game's functions, not by writing the result fields). State that a remote view needs travels in the existing
mech rows.

- Rows: `card297: <role> t=<ms> mech <guid> enter: controller ref <set|null>, animator <class> state <s>, anim
  instance <class>`, `card297: <role> t=<ms> mech <guid> animation <name> started (<trigger>)`,
  `WARN card297: <role> mech <guid> piloted, animator not running`.

## Sibling sweep (same class: an animation set up by a game path the client's possession skips)

| Item | Check |
|---|---|
| Client pilots, own cockpit view | arms, bob, recoil, ability animations play |
| Client pilots, seen by the host | body and weapon animations |
| Host pilots, seen by the client | same |
| Enter and exit animations, both sides | play once |
| Turrets (card 029, same enter mechanism) | gunner arms and barrel animations |
| Dropship ride, bug form (card 208) | own-view and remote animations after possession change |
| Respawn or reconnect while in a mech | animator state after card 045 |

## Predictions (one run, after the playtest, batched)

- Client enters the mech, walks 5 s, fires 3 shots, uses the saw, exits: `card297` enter row with `controller ref
  set` and animator running; one `animation ... started` row per action; zero `WARN card297`; client screenshots
  mid-walk and mid-fire differ in the cockpit arms pose; host receiver-side screenshot shows the client's mech
  moving its weapon.
- Digest round after: 0 mismatches.

## Also seen (not this card)

Client t=257318415 to 257318431: `card075: client PC exit input` logged every 8 ms. Check with the exit path
whether one press sends one exit.

## Senior trace (both logs, 2026-11-13): candidate stands, not settled

The host sets the mech's controller reference for both pilots (host's own enter 83476228 and the client's enter
83402457: `weak PC BP_PlayerController_C (its pawn BP_BaseMech_C)`); the client's mech reads `weak PC null`
after its enter (257272493). The reference at +0xE30 is a plain weak pointer written by `EnterMech`, which the
client never runs. That is a fact; that the animations read it is still a candidate. Step 1 stays as written
(static, no run) and must name the reader. The open question to the user (which animations) is asked with the
review report, not before.

## Step 1 answers (static, junior, 2026-11-13)

Sources: llvm-objdump of the shipping exe, callees named from the PDB publics; the cockpit anim blueprint and
`BP_BaseMech` name maps extracted with `tools/iostore/iox.py`. Image base 0x140000000.

### The game's own enter chain (host, or standalone)

`OnInteraction` (0x050F7840) calls `EnterMech(character)` (0x050E65F0). The chain is three steps, each started by an
animator sequence end, or called directly when that sequence key is empty:

1. `EnterMech(character)`:
   - clears the timer handle at +0xE08 (`FTimerManager::FindTimer` / `InternalClearTimer`);
   - sets the weak pointer +0xE18 to the character (`GetControllingCharacter` = `Get(+0xE18)`) and the weak pointer
     +0xE30 to the character's controller (character +0x2D8). If +0xE30 is invalid, it stops;
   - `UADAbilitySystemComponent::SetGameplayTagCount` twice on the character's ASC (+0x680, container +0xF18);
   - `USTAIUtilityLibrary::MoveThreatTo(character, mech)`: AI threat moves from the soldier to the mech;
   - `ASTPlayerController::StartBoardingTheMech(pc)`: the OnWantsToBoardMech broadcast (card 255 mirrors it on the
     host PC);
   - if +0xCD4 > 0: `StartCameraFadeToColour` on the PC;
   - if the sequence key +0xCC8 is set: the character's `GetAnimatorComponent()->StartAnimationSequence(+0xCC8,
     +0xCD0)` (vtable +0x4E8), with `OnBoardingComplete` bound to its end; otherwise it calls
     `OnBoardingComplete` directly.
2. `OnBoardingComplete` (0x050F6950), gated on +0xE30 valid:
   - copies the soldier's faction tag to the mech (`GetFactionTag` / `SetFactionTag`, cached at +0xE50);
   - `AController::Possess(pc, mech)`;
   - adds a gameplay tag on the mech's own ASC (`FGameplayTagCountContainer::UpdateTagMap_Internal`, +1);
   - `SetMeshVisibility(FirstPersonMesh +0x848, true)` and `SetMeshVisibility(+0xC20, false)`. Both are
     `USceneComponent::SetVisibility`: the cockpit arms are shown and the second mesh is hidden;
   - `AttachControllingPlayerCharacterToMech`: `USTWeaponUtilityLibrary::UnequipCurrentWeapon(soldier)`,
     `AttachToActor`, `SetActorLocation/Rotation`, `ASTPlayerCharacterMI::SetUnpossessedCollisionProfile`;
   - if +0xE30 is valid and +0xCE4 > 0: `StartCameraFadeFromColour` (+0xCE0, +0xCE4);
   - if the embark key +0xCD8 is set: the mech's animator `StartAnimationSequence(+0xCD8)` with `OnEmbarkingComplete`
     bound; otherwise `OnEmbarkingComplete` directly. `TryEmbarkingAnimationSequence` (0x051098B0) is the same tail
     as a UFUNCTION.
3. `OnEmbarkingComplete` (0x050F7110), gated on +0xE30 valid:
   - `AddMechInputContext`;
   - `ASTEnemyPresence` exit of the soldier (`DoesActorTriggerPrescence` / `TriggerExit`);
   - the fuel delegate broadcast and the self-destruct timer setup.

Every step after the first is gated on +0xE30. A machine that never ran `EnterMech` for the mech has +0xE30 null,
and the chain does nothing there even if it is called.

### What the client path skips (item 2)

The client's E on a host mech (`detour_c75_mech_on_interaction`, simulated proxy) sends MechEnter and returns. The
host runs the whole chain above for the client's soldier. Possession, attach and the ASC tags replicate (or are
host-side effects). These steps never run on the client:

| Skipped call (client side) | Effect on the client |
|---|---|
| weak +0xE18 / +0xE30 assigns | `weak PC null` (log row 257272493); every later step's gate fails |
| `SetMeshVisibility(FirstPersonMesh, true)` / `(+0xC20, false)` | cockpit arms keep their spawn visibility; the second mesh is not hidden |
| `StartAnimationSequence(+0xCC8)` on the soldier's animator | no board sequence |
| `StartAnimationSequence(+0xCD8)` on the mech's animator | no embark sequence (cockpit entry) |
| `StartCameraFadeToColour` / `FromColour` | no fade |
| `AddMechInputContext` (local) | card 075 adds it already (`guard_mech_input`) |

No `STMeshUpdateComponent`, `OnRep` or `ClientRestart` call is in the chain.

### What drives each animation (item 1)

- Cockpit arms (`ABP_Player_RazorbackMech_Cockpit_C` on `FirstPersonMesh`): the event graph reads only the pawn
  owner (`TryGetPawnOwner`):
  - `GetMovementComponent` velocity, dotted with the actor forward and right vectors (FSpeed, FMovement,
    RMovement), and `GetLastInputVector`;
  - `GetControlRotation` through `GetPlayerController`, as `NormalizedDeltaRotator` against ControlRotLastTick
    (RLook, UpLook: inertia sway);
  - `HasMatchingGameplayTag` through the gameplay-tag asset interface (the attack and button-press blend by bool);
  - RBtnDownTime and RBtnUpTime.
  
  The graph plays blend spaces `3DBS_Razorback_Locomotion` / `LSide` / `RSide`, the GunIdle sequence and additive,
  `Gun_Attack_01` and `ButtonPress_Left`, plus a control rig. It does **not** read +0xE30 or `GetControllingCharacter`.
  The candidate "the animation reads the weak PC" is refuted for the arms.
- Enter and exit: animator sequences keyed by FName (+0xCC8 board on the soldier's `ADSkeletalAnimatorComponent`,
  +0xCD8 embark on the mech's), not montages. They run only where `EnterMech` runs: on the host for both pilots.
- Fire recoil, saw and stomp: ability-driven (`GA_Weapon_RecoilAbility_C` activates on the client, log 62401-62557).
  For the arms they show only through the gameplay tag the ABP checks.
- Seen from outside (item 3): the mech's body `Mech` is a **StaticMeshComponent**. It has no skeletal animation on
  any machine; only its transform moves. No row needs to carry body animation state. The remote view needs only the
  body shown (card 075 `c75_show_body`) and the replicated movement.

### Remaining candidates for "animations not working" (client pilot)

1. `FirstPersonMesh` visibility is never set on the client (skipped `SetMeshVisibility`). If its spawn default is
   hidden, the client sees no arms at all. If the mesh is hidden, its anim instance may also not tick
   (VisibilityBasedAnimTickOption).
2. The tag the ABP's attack blend checks is set by the host's abilities. If it is a loose tag that is not
   replicated, the client's arms never blend to the attack pose, although the client's own `GA_Weapon_RecoilAbility`
   activates locally.
3. Locomotion and look inputs (velocity, last input vector, control rotation) are local on an autonomous proxy and
   should work. The client is role 2/3 (autonomous proxy), so they need no fix.

### Fix shape (replaces "Fix" above, pending the senior)

On the client, after possession of a host mech by its own PC (card 075's MechEnter acknowledged), run the
visibility half of `OnBoardingComplete` by the game's own function: `ASTPlayerCharacter::SetMeshVisibility`
(0x051DBB20) on +0x848 true and +0xC20 false. Optionally start the embark sequence through the mech animator's
`StartAnimationSequence` with the +0xCD8 key. Do not write +0xE30 (card 075 c75a: a client-side weak PC left input
dead). The enter row logs FirstPersonMesh visible, the anim instance class and whether it ticks. The remote body
needs no change.

Proof needs one run: client enter, then a shot of the cockpit before and after. Before that, the first run row
(`card297` enter row) must show FirstPersonMesh bVisible on the client after the enter; it settles candidate 1.

## Senior verdict 2026-11-13: step 1 accepted, cause still one of two, not released

- Refuted for the cockpit arms: "the animation reads the weak PC". Body is a StaticMeshComponent: no remote
  animation state to send.
- Open between candidate 1 (FirstPersonMesh never shown on the client) and candidate 2 (the attack blend's tag is
  loose and not replicated). They need different fixes, so no fix is coded yet.
- Settled by either: the user's answer (were the cockpit arms visible at all while piloting), or the enter row
  (FirstPersonMesh bVisible, anim instance class, tick, and the tag the attack blend checks with its count on the
  client). The row goes in the next build as diagnostic only.
- Also not run on the client: the board and embark sequences and the camera fades. If the user names enter or exit
  animations, that is a third item with its own fix (the animator's `StartAnimationSequence` by key).

## Senior verdict 2026-11-14: user answer settles candidate 1, fix released

User answer: while piloting as the client, the cockpit arms were not visible at all. That is candidate 1: the
client never runs `SetMeshVisibility(FirstPersonMesh +0x848, true)` / `(+0xC20, false)`.

Fix to code (fix shape above, with these limits):

1. Client, on its own PC's possession of a mech: `ASTPlayerCharacter::SetMeshVisibility` (0x051DBB20) on +0x848 true
   and +0xC20 false. +0xE30 is not written.
2. Client, on leaving the mech: the reverse, as the game's own exit does it. The junior names the exit chain's
   visibility calls from the exe first (the enter chain is traced above, the exit is not) and mirrors exactly those.
3. Sibling, same call: every other way a client comes to pilot or leave a mech without the game's chain (join or
   reconnect while piloting, respawn in a mech, card 045 restore, the mech destroyed under the pilot, host restart).
   Turrets (card 029): report whether the gunner's first-person mesh is shown by a chain the client also skips.
4. Rows: `card297: client t=<ms> mech <guid> enter: FirstPersonMesh visible <0|1> -> <0|1>, second mesh visible
   <0|1> -> <0|1>, anim instance <class>, attack tag <name> count <n>`; the same shape for the exit;
   `WARN card297: client pilots mech <guid>, FirstPersonMesh hidden` from a 1 s check while piloting.
5. Candidate 2 (attack blend tag) is not fixed blind: the enter row's tag count and one row per client mech shot
   (`attack tag count <n>` at the shot) decide it in the same run. Board and embark sequences and the camera fades
   stay out of this card.

Predictions (one run, client enters, walks 5 s, fires 3 shots, exits): enter row `FirstPersonMesh visible 0 -> 1`;
zero `WARN card297`; the client screenshot while piloting shows the cockpit arms, the one after the exit does not;
the per-shot rows show the tag count, and a count of 0 at every shot opens a follow-up card for candidate 2.

## Senior read of run c447b177a (2026-11-17 08:47, b177 e7197c28f8ab7bb8): candidate 1 proven

- Client rows: 2 enters `mech 256 enter (possessed): FirstPersonMesh visible 0 -> 1, second mesh visible 0 -> 0`, exit
  `(left): FirstPersonMesh visible 1 -> 0`, 0 `WARN card297`.
- Receiver-side picture: `client-c447b177a-c298-mech-look-up.png` (client piloting) shows the cockpit frame, both arm
  guns and the two control sticks.
- Owed: the per-shot rows for candidate 2. No run has one: no scenario fires the client's mech. Added to
  `tools/scenarios/c447-c4m1.txt` after the card 298 look steps: client holds fire 1.5 s, waits on the
  `card297 ... shot: attack tag ... count N` row, shot `c297-mech-fired`. Rides the next c447 run; the count decides
  close or a follow-up card.

## Senior read of run c447b177b (2026-11-17 09:08)

No `shot:` row: `c297_shot` sits behind `g_c76_log` (UBWCOOP_C76_LOG unset). The tag rows answer candidate 2
anyway: during the client's burst the mech gets `tag +WeaponSystem.EquippedWeapon.Fire` at every shot (t=565991166,
..482, ..649, ..966, ..) with `+WeaponSystem.Recoiling`, 10 client FireProjectile rows, 11 host launches. The attack
tag reaches the client's mech. Owed for close: the shot inside the burst (scenario fixed: 4 s burst in the
background, shot 1 s after the first tag row, host shot too). Next run c447b177c with UBWCOOP_C76_LOG=1 on the client.

## Closed

Closed 2026-11-17 on b177 `e7197c28f8ab7bb8`, run c447b177c (FED_C4M1, rc 0, scenario passed, 0 EXCEPTION/FATAL on
both sides). Client rows during a 4 s held burst in the mech: 23 `card297: client ... tag
+WeaponSystem.EquippedWeapon.Fire` rows and `card297: client t=.. mech 260 shot: attack tag
WeaponSystem.EquippedWeapon.Fire count 1` rows about every 160 ms. Receiver-side shot taken inside the burst
(`client-c447b177c-c297-mech-fired.png`): arm gun raised with muzzle flash, ammo counter 175 of 999 counting down.
Playtest watch stays: a client mech burst with no arm movement reopens.
