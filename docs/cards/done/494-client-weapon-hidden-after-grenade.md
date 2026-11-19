# 494: Client weapon mesh missing after own grenade throw

Status: closed 2026-11-18 16:18 (senior). Fixed in b237 `c92af11d2ae5bfac`.

## Finding

Playtest 2026-11-18: after some client grenade throws, the client stood with an empty glove hand while the HUD still
showed MORITA MK1 60/240 (b231a cmd 22, b234a cmd 41). Chain: `USTGrenadeThrowGameplayAbility::StartThrowingAnimation`
calls `ASTWeapon::HideWeapon` (0x05240410), which moves the mesh to CollisionCylinder and hides the actor. The throw
task's OnAnimationEnded runs `StartEquippingWeaponAnimation` (0x051703E0), which calls `ASTWeapon::ShowWeapon`
(0x0524DE00) and reattaches the mesh to FirstPersonMesh/RightHandSocket. On the client that end sometimes never fires.
The animator has silent paths that drop a bound end. `StartAnimationSequence` with no anim instance clears it.
`SetActiveAnimation` with interrupt 0 replaces the montage, so `CallAnimationEnded` no longer matches. Which of these
runs is not logged. **Root cause not determined.**

## Fix (b237, `mod/src/ubw/weapon_c494.cpp`, client)

A class guard on the only HideWeapon caller that pairs with a later ShowWeapon (the grenade throw). OnUnequip and
SpawnWeaponInstance hide for good. The check starts 2 s after the client's own grenade OnDoAction. If the pawn is
alive, the equipped ASTWeapon is hidden and off FirstPersonMesh, and no arms montage plays, the guard calls
`ShowWeapon`, the same call OnEquip makes. While an arms montage plays, it re-checks every 500 ms, up to 8 times. When
those are spent, it logs a WARN and leaves the weapon alone. New symbols: `ASTWeapon_ShowWeapon` 0x0524DE00 and
`ASTWeapon_StaticClass` 0x050B1860. Diagnostic rows (`card494: client ...`) come at each scenario shot and after each
own throw.

## Closed (2026-11-18 16:18)

Evidence, run c244b237a (FED_C4M1):
- rc 0 and EXCEPTION/FATAL 0/0 on both sides.
- The fix fired twice, at t=678143457 and t=678162829. Each time the 2 s row read `hidden 1 ... on CollisionCylinder/None`
  with montage none. The after row read `hidden 0 ... on FirstPersonMesh/RightHandSocket`, and every later shot row
  stayed `hidden 0` on the arms.
- The other throws were already shown at 2 s, so the guard did nothing.

Senior review (presume defect):
- Map change or death between throw and check: the guard reads the pawn live and returns Done.
- Re-throw: `c494_after_throw` resets the due time and the retries.
- Weapon swap inside the window: the equip montage plays, so the guard re-checks; once the new weapon is on the arms it
  returns Done.
- The fix path does not use the 120-row log cap.
- The retry countdown ends with the WARN on the 8th re-check.

Not done:
- The root cause: which animator path drops OnAnimationEnded.
- The fix row is a WARN by design, so it shows in the WARN diff while the cause stands.
- The `c244-hud-start` shot reads `hidden 1` on CollisionCylinder before any throw. That is the spawn state, shown by
  the first block; it is not this bug.
- The host's own throw and the remote (third-person) weapon view are not checked.

Reopen when: a client or host shot or playtest shows an empty hand after a throw, or the `arms montage still playing
after 8 re-checks` WARN appears.
