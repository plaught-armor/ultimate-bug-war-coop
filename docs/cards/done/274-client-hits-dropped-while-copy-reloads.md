# 274 Client hits dropped while the host's copy reloads (client reload is not relayed)

Found in the live log of card 243 run 10 (FED_C3M2, 2026-11-13): 91 rows `WARN card157: ... HitClaim shot N from
pc ... (NPC) found no round in 1500 ms: dropped` in 52 s of one fight. Run 9 has 26, card 241 run 10 has 2.
Each row is a client hit on a bug that did no damage on the host. Real mod bug, play-visible: a client's shots
stop hurting bugs for seconds at a time.

## Mechanism (logs of run 10 plus source, senior, 2026-11-13)

- A client hit claim needs a round fired by the client's server copy (`claim_c157.cpp` `resolve`). The copy has
  its own magazine and runs its own reload when that magazine is empty (card 072, finding 1).
- The client's reload is not sent to the host. `ChanOp::Reload` is card 143's rejoin restore, not a weapon
  reload. `AmmoMirror` (card 072) carries reserve counts only.
- The client reloads with rounds left. Host rows `card072: AmmoMirror ... Morita 170 to 114 (ret -56)`, `114 to
  85 (-29)`, `85 to 36 (-49)`: reloads of 56, 29 and 49 rounds, not 60. After each one the client has 60 in the
  magazine and the copy has what was left.
- The copy then empties first and reloads by itself while the client keeps firing. Example: client reload at
  225602984 (56 rounds), copy reload sound at 225604789, 16 dropped claims at 225606 to 225607, and
  `card175: ... the copy fired 0 of the client's 18 rounds` at 225606546.
- Second effect, same cause: the host sees and hears the client's reload at the wrong time (the copy's own),
  which breaks the rule that every visible effect is the same for every player.

## Fix spec (by class: the copy's magazine follows the client's)

- Client: the `AmmoMirror` text gains the magazine count of each weapon that uses one (same pair form, a kind
  mark for magazine), sent when it changes by a reload and on the existing cadence. Source: the client's own
  weapon instances (`USTWeaponUtilityLibrary::DoesWeaponAmmunitionTypeUseMagazine`, already bound as
  `g_ammo_uses_magazine`).
- Host: `c72_mirror_apply` sets the copy's magazine with `SetEquippedAmmunition` (`g_set_equipped_ammo`, as
  `c21_grant_ammo` does) when the client's count is higher than the copy's. It never lowers it inside a burst
  (the copy may lag the client by rounds in flight).
- Client reload start is relayed as a fourth fire kind, no new op. Settled statically (run 10 client log):
  the reload key calls `AbilityInputTagPressed InputTag.Reload` on the weapon's ASC (`card021` row, owner
  `BP_Weapon_MoritaMk1_C`, tag in the weapon's input map next to `InputTag.Fire`), the same ASC and detour the
  Fire relay already uses.
  - `hooks_internal.h:1456`: `kFireReload = 3` (`InputTag.Reload`, soldier weapon only), `kFireKinds = 4`; the
    two static_asserts at 1472 to 1475 must still hold.
  - `fire.cpp`: `c24_kind_tag` (61), `c24_kind_text` (70), `c26_own_fire_pawn` (128 to 151; same rule as ADS:
    not a mech, not a bug form, both sides so the host's reload reaches the clients too).
  - `chan_ops.cpp:23`: `mech_ok` accepts `kFireReload && !mech`. `c24_relay` then presses and releases the tag
    on the copy's weapon ASC; the copy's own reload ability moves reserve to magazine, plays the montage and sound.
  - The copy's own empty-magazine reload stays as the fallback.
- The client reads its magazine with `USTWeaponUtilityLibrary::GetAmmoCountOfEquippedAmmunitionType`
  (`tools/symbols.txt:321`, already bound) in `c72_client_step` (`death.cpp:3059`).
- Files: `mod/src/ubw/hooks_internal.h`, `fire.cpp`, `chan_ops.cpp` (relay); `ammo.cpp`, `death.cpp`,
  `hooks_ubw.cpp:1555` `c72_mirror_apply`, `weapons.cpp:803` (magazine). Do not touch `claim_c157.cpp` or the
  card 175 hold.
- Predicted relay rows: client `client sent Fire` with kind 3 at each reload key press; host `host received
  Fire` kind 3 and a `card024` relay line `press (reload)` on the copy's weapon.

## Proof (batched with the next FED_C3M2 run, no extra run)

Scenario already does it: the client's `clear` with reloads. Predicted rows: `card274: host copy magazine
<tag> a to b` after each client reload; the copy's reload sound within 300 ms of the client's; 0 `found no
round` rows while the copy holds rounds; `the copy fired 0 of` rows only when the client's own magazine was
empty. Budget: 0 extra runs.

## Build 3 (DLL `5e9069094b412c66`), senior review 2026-11-13

- Correction to the spec: `GetAmmoCountOfEquippedAmmunitionType` reads the owner's reserve. The magazine is read
  with `GetEquippedAmmunitionCount(weapon, behaviour)` 0x523AA30 and `GetEquippedAmmunitionType(out, weapon,
  behaviour)` 0x523AC60 (ABI checked in disassembly: rcx weapon, dl behaviour; rcx out, rdx weapon, r8b behaviour).
- Wire: `AmmoMirror` text gains ` @<ammunition tag> <count>` per behaviour 1 and 2 that uses a magazine; sent at
  once when the sum rises. Host: `c274_magazine_apply` on frame receipt only (not on the card 050 re-apply),
  server copy only, raise only.
- Attacked: stale re-apply (excluded by call site), weapon mismatch between client and copy (tag compare, no
  write), reserve pairs seeing `@` (skipped in `c72_apply`, loop bound raised by 2), text overflow (snprintf
  bound, pair dropped). No defect found. Open: a tag of `kChanNameMax` characters plus `@` would fail the host
  parse with a `bad pair` WARN; no shipped tag is that long.
- Proof still owed: the rows in "Proof" above, in FED_C3M2 run A.

## Run A (c243-c3m2-a1), senior verdict 2026-11-13

- Held: 9 `card274: host copy magazine` rows; relayed reload (`card024: host relay press (reload)`, 4 rows).
- Broken: 32 `found no round` rows, 28 `the copy fired 0 of` rows with rounds in the client's magazine.
- Cause is not the magazine: a lost fire press on the copy, and the client's automatic reload has no key press.
  Both go to card 276. Code of 274 accepted; the card closes with 276's proof.

## Run `c243-c3m2-b4a` (build 4, 2026-11-13): not closed

11 `card274: host copy magazine` rows; reload sends log as `(reload ability)`, not `kind 3`. Not proven that each
client reload gets a host magazine row (run void, card 243 note). Closes with card 276 in the next FED_C3M2 run.

## Closed

Closed 2026-11-17 (senior): code accepted 2026-11-13; card 276's proof run (86 client shots, 86 host copy shots, 0 dropped, 0 found-no-round rows) covers it. Watch stays in STATE playtest-log checks.
