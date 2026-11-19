# 296 The nuke launcher does not fire for the client

Playtest 2026-11-13 (user plays as client): "nuke launcher isn't firing on client but is on host".

## Trace (client log `UBWCoop-menu-20261113-184603.log`, FED_C2M1)

| Line | Client t (ms) | Row |
|---|---|---|
| 44881 | 257072283 | `weapon slot 0 client-own: BP_Weapon_PumpShotgun_C -> BP_Weapon_NukeLauncher_C` |
| 44884 | 257072858 | `ability slot 0 client-own: BP_WeaponReloadAbility_NukeLauncher_C` |
| 44923 | 257074704 | `AmmoMirror sent: ... AmmunitionType.Nuke 1 @AmmunitionType.Nuke 1` (one round loaded) |
| 44931, 44949 | 257075511, 257077688 | `ability slot 0 client-own: BP_Weapon_SpoolUpAbility_NukeLauncher_C` |
| 45099 | 257081451 | dropped and picked up again, launcher equipped, `@AmmunitionType.Nuke 1` |
| 45158, 45173 | 257083109, 257087319 | `BP_Weapon_SpoolUpAbility_NukeLauncher_C` |

Four fire presses, four spool-ups. No `BP_WeaponFireAbility_NukeLauncher_C`, no `SpoolDownAbility`, no projectile
launch row and no ammo change follows any of them. The same log has 54 `BP_WeaponFireAbility_PumpShotgun_C` and
44 `BP_WeaponFireAbility_MoritaMk1_C` activations, so the fire path as such works on the client. Card 029 saw
`BP_WeaponFireAbility_NukeLauncher_C = 1` on the host's own pawn.

The spool-up runs and never hands over to the fire ability on the client.

User, same playtest, second report: "it works if host fires nuke launcher but not client". The host's launcher
fires; only the client's own launcher fails.

## Step 1 (static, junior, no run)

1. From the `BP_Weapon_SpoolUpAbility_NukeLauncher_C` graph and its native parent: what ends the spool-up and
   what starts the fire ability (a timer or montage end, a gameplay event, a tag added to the ability system
   component, an input-still-held test). Name the function and the condition.
2. Which part of that condition fails on a client pawn (net mode 3, local role 2): an authority test, a net
   execution policy, a gameplay event sent only on the server, a tag or attribute the mod does not mirror, or a
   mod detour on the path (the `Fire` relay, the fire gates of cards 043, 157 and 243, `c243_ammo_fill`).
3. Whether the mod's `Fire` rows went out for these presses (the netbudget rows show `Fire 96` and `Fire 64`
   bytes sent in the two windows) and what the host did with them: after the playtest, the host log at host
   t=83206275 to 83218083 (client t minus 173869236).

Write the answers into this card before coding.

## Fix

The spool-up on the client's own pawn ends in the fire ability exactly as on the host: the condition found in
step 1 is met on the client by the same mechanism the mod uses for other weapons (the own pawn fires locally,
the host is told by the `Fire` row and launches the round that counts). One round leaves the launcher, one ammo
is spent, on both machines.

- Rows: `card296: client t=<ms> spool-up <class> ended: <reason>; fire ability <class> activated <0|1>`,
  `WARN card296: client spool-up <class> ended without a fire activation (<reason>)`.

## Sibling sweep (same class: a weapon whose fire needs a second ability or a held state)

| Weapon | Check |
|---|---|
| EPulse88 (spool-up, card 029) | client fires |
| EPulse44, Autocannon, FlameThrower (held fire) | client fires and stops |
| Morita underslung shotgun and grenade launcher variants | client fires (underslung fired 3 times this map) |
| Marksman (charge or scope, if any) | client fires |
| Mech and turret weapons (card 029 audit) | client fires |
| Every weapon class in the game's weapon table | static list of fire, spool, charge and pump abilities; mark each tested or not on the client |
| Nuke on the host seen by the client | projectile, explosion, damage and FX arrive (card 136 item) |

## Predictions (one run, after the playtest, batched)

- Client with the launcher and 2 rounds presses fire twice: two `card296` rows with `fire ability ... activated
  1`, zero `WARN card296`; host log shows two nuke projectiles from the client's pawn; client `AmmunitionType.Nuke`
  2 to 0; receiver-side screenshot on the host shows the explosion.
- EPulse88 on the client: same rows, fire activated.
- Digest round after: 0 mismatches.

## Step 1 answers (junior, static plus both logs, 2026-11-13; cause not settled, not coded)

1. **What ends the spool and starts fire.** `GA_Weapon_SpoolUpAbility` (parent of `BP_Weapon_SpoolUpAbility_NukeLauncher`
   and `_EPulse88`) runs a `USTAbilityTask_PlayAnimation` (`Activate` 0x050D9580) on the avatar's
   `UADAnimatorBaseComponent`: animator vtable +0x4E8 `StartAnimationSequence` (+0x4E0 for the fixed-time form), then
   AddDynamic `DoAction` (animator +0xC0) and `AnimationEnded` (animator +0xD0). The task's `OnDoAction` /
   `OnAnimationEnded` lead to `K2_EndAbility` plus `SendGameplayEvent(WeaponSystem.EquippedWeapon.Fire)`, which
   triggers `BP_WeaponFireAbility_NukeLauncher` (blocked while `EquippedWeapon.SpoolUp` / `SpoolDown` or
   `WeaponSystem.Blocked` is on the pawn). Releasing the input (`OnInputReleased`) cancels the spool with no event.
   If the animator component is missing, the task ends at once through `OnAnimationEnded` (an immediate fire).
   Neither nuke Blueprint adds an aim or tag gate (decompiled: Spool Activate plays `Arming_Seq`, OnEnd plays
   `Interupt`; Fire OnEnd only sets the audio's Mouse Released).
2. **Which condition fails on the client.** Not settled. Evidence:
   - The event path works on the client: client-own EPulse88 SpoolUp 257989074 then FireAbility 257989571 (+497 ms),
     and 257993161 then 257994602. Same parent ability, same event.
   - Client-own nuke SpoolUp at 257075511, 257077688, 257083109, 257087319, 257197855, 257200048: never a FireAbility,
     never a SpoolDown. The press gaps are 2.2, 5.4 and 4.2 s; the host's own nuke windup is 2.0 to 3.1 s
     (83272169 to 83275305, 83302745 to 83304749).
   - The client's animator delivers for other abilities (the reload completed, magazine 0 to 1 at 257074704).
   - The client log has no press/release row for the client-own fire input and no task callback row for the
     client-own side (`c104_note_task` logs only host sides), so "released before the windup ended" and "the nuke
     windup's DoAction / AnimationEnded never reaches the client-own task" cannot be told apart from this log.
   - No sprint: `GA_Sprint` was not activated during the presses (the `Walk -> Run` key at 257076128 is the
     animation speed key).
3. **Host rows at host t 83206275 to 83218083.** Each client press reached the host copy: `Arming_Seq` + SpoolUp at
   83206353, 83213951, 83218159; `Interupt` about 100 ms later (83206459, 83214053, 83218259) and FireAbility 4 ms
   after that (83206463, 83214058, 83218262). That 100 ms is card 276's re-press (`c276_repress`, `kC276RepressMs`
   100): it cancelled the copy's spool and started FireAbility directly. Press 1 fired on the host
   (`MS_WPN_NukeLauncher_Fire`, `shot s seq=0` at 83206475, `Fire_Latch` 83206663) and spent the client's round
   there, while the client's own pawn fired nothing. Same again at 83328709 / 83328814 / 83328819. The play build had
   no card 181; in the current tree card 181 refuses a copy Fire with no client Round, so the copy now waits.

Rows that would settle item 2 in the next run (not coded in this batch, per the senior's scope order): client-own
fire input press/release times for the spool, client-own `DoAction` / `AnimationEnded` counts from the card 104
task hooks for `kSideClientOwn`, and the spool's end reason (`card296` / `WARN card296` rows as specified above).
Also to consider when coding: `c276_repress` should not re-press while a `SpoolUp` spec is active on the copy
(`c276_spec_active(asc, "SpoolUp") > 0`), or the copy skips the windup.

## Senior trace (both logs, 2026-11-13): one part settled, one part bounded

Settled (host side, codeable now): card 276's re-press treats a spool-up weapon as "pressed but no round" and
re-presses 100 ms after the press. The release cancels the copy's spool (`Interupt`), the new press starts the
fire ability directly, so the copy fires with no windup: nuke 83206353 spool, 83206463 fire (110 ms; the host's
own windup is 2.0 to 3.1 s), EPulse88 84119961 spool, 84120073 fire (112 ms; the client's own is 497 ms). Fix:
`c276_repress` makes no try while a `SpoolUp` spec is active on the copy's ASC, and the gate time restarts when
the spool ends (same shape as the reload skip at `fire.cpp:324`). Sibling: every weapon whose fire is behind a
windup or charge ability (list them from the weapon Blueprints' ability sets, not from this log).

Refuted: "the host's cancel of the copy's spool ends the client's own spool". The EPulse88 copy is cancelled at
112 ms the same way and the client's own EPulse88 fires at 497 ms (257989074, 257989571).

Bounded, not settled (client side): why the client's own nuke spool never reaches fire. The client's Fire frames
are 32 bytes each (netbudget `Fire` per 5 s window: 96, 96, 64, 64 for the windows ending 257076903, 257081909,
257086910, 257091917). The window ending 257076903 holds the reload press and release and the press at 257075511
and nothing else, so press 1 was still held at 257076903 (1392 ms) and was released before press 2 at 257077688
(2177 ms). The host's own windups are 2.0 and 3.1 s. So press 1 can have been released before the windup ended,
which cancels with no event by the game's own rule; presses 2 to 4 are bounded only below 4.2, 3.8 and 4.6 s.
The log cannot separate "released early" from "the windup's DoAction never came". No client fix is coded.

Diagnostic rows for the next run (the three the junior lists above: own fire press and release times, client-own
`DoAction` / `AnimationEnded` counts, the spool's end reason). Prediction: the client holds fire 5 s with the
nuke launcher: either FireAbility follows the spool at the host's own windup time (then the playtest presses were
short and the card closes with the host fix), or the end-reason row names what ended the spool.

## Build 7 (host part coded, client diagnostic; compiled; diff `b7-F-296.diff`)

- Host (`fire.cpp c276_repress`): no re-press while a SpoolUp or SpoolDown spec of the weapon's ASC is active
  (`c276_spec_active`); the press time moves to now, so the gate restarts when the spool ends. Row:
  `card296: host t=.. re-press of X held during the spool (SpoolUp active N, SpoolDown active N, N ms after the press)`.
- Sibling sweep: only `BP_Weapon_SpoolUpAbility` / `SpoolDownAbility` (NukeLauncher, EPulse88; parents
  `GA_Weapon_SpoolUpAbility`, `GA_Weapon_SpoolDownAbility`) use this path. `BP_BugForm_ChargeAbility_Warrior` is a
  bug form, not on it.
- Client diagnostic, one row per own spool-up: `card296: client t=.. spool-up X ended: <reason> (<how>, spool N ms,
  press N ms and release N ms from its start, client-own DoAction +N AnimationEnded +N); fire ability X activated N`.
  Reason: DoAction, AnimationEnded, "fire input released inside the windup, no task callback", or "no task callback
  and no release". End: spec inactive, still active after 10 s, or weapon ASC gone. `WARN card296` when nothing
  fired.
- Not coded: a client fix. It waits for the diagnostic rows.

## Senior verdict, b8 run 2 (2026-11-13): scenario hold too short, no mod bug shown

Client row: `spool-up BP_Weapon_SpoolUpAbility_NukeLauncher_C ended: fire input released inside the windup ... spool
1702 ms, press -1249 ms and release 1700 ms`. The spool began 1249 ms after the press and the 3000 ms hold ended
inside the windup. No host `re-press ... held during the spool` row because no re-press came due. Last run: one hold
of 6000 ms or more, the step waits on the client `card296: client ... spool-up` row. Prediction: the row ends by
`DoAction` or `AnimationEnded` with `fire ability ... activated 1`, and the host copy fires once, after the windup.
If the row instead shows no release and no fire activation, the client side has a mod bug and gets its own trace.

## Junior key timing trace, b8 run 2 (2026-11-13, client log `logs/b8-combined-client.log`)

| Client t | Row | Reading |
|---|---|---|
| 270264856 | `card050: ... Weapons received (#4 ...)` with the NukeLauncher | the interact pickup gave the launcher |
| 270265057 | `client sent Fire 1 (press)`; `local activation handle 167: failed` (SpoolUp) and `168: failed` (Fire) | press 1 (the scenario's 300 ms tap) lands inside the swap-in; the game refuses both abilities |
| 270265224 | `host own AnimationEnded, key NukeLauncher_SwapIn ... 681 ms after its last start` | swap-in ends |
| 270265224 | `local activation handle 169: activated ... BP_WeaponReloadAbility_NukeLauncher_C`, `reload press and release sent` | the launcher came empty: the game's automatic reload starts at once |
| 270265359 | `client sent Fire 0 (AbilityInputTagReleased)` | press 1 released |
| 270265908 | `client sent Fire 1 (press)` | press 2 (the 3000 ms hold) lands inside the reload |
| 270267076 / 270267140 | `AmmoMirror ... Nuke 1`; `AnimationEnded, key NukeLauncher_Reload ... 2597 ms` | reload ends, one round loaded |
| 270267157 | `local activation handle 167: activated ... SpoolUpAbility` | the held input starts the spool 17 ms after the reload ends (1249 ms after press 2) |
| 270268856 / 270268859 | `cmd: 6 key LeftMouseButton 3000 -> done`; `card296: ... spool 1702 ms ... released inside the windup` | the hold ends 1700 ms into a 2.0 to 3.1 s windup |

Result: the scenario's timing, not a mod bug. From the pickup, a fire needs the swap-in (681 ms), the automatic
reload (2597 ms) and the windup (2.0 to 3.1 s): about 6.4 s at worst. Each step behaved by the game's own rules (no
activation during the swap-in, a held input starting the spool when the reload ends). The b10 run waits on the
reload's `AnimationEnded, key NukeLauncher_Reload` row, then holds 6500 ms (`tools/scenarios/b10-last.txt`).

## Senior verdict, b10 last run (2026-11-13): confirmed

Hold of 6500 ms after the reload's end. Client: `ended: AnimationEnded (spec inactive, spool 2019 ms ...); fire
ability BP_WeaponFireAbility_NukeLauncher_C activated 1`. Host: `re-press ... held during the spool`, one
`FireProjectile` 2052 ms after the spool start. The host part (build 7) holds; the client side needs no code. Row
defect: a hold still down prints `release -271397729 ms`; next build prints `held`. Final proof is the user's own
launcher use in the next playtest.

## Closed (2026-11-17 08:49)

Measured on b174 (run c423b174a, FED_C2M1, client fires the launcher by held input): 4 client rows
`spool-up BP_Weapon_SpoolUpAbility_NukeLauncher_C ended: AnimationEnded (spec inactive, spool 2005 to 2020 ms ...); fire
ability BP_WeaponFireAbility_NukeLauncher_C activated 1`; the two LOS_O02 targets died from the client's nukes (host
`card163: ... destroyed by a damage event (Destroyed 0 -> 1): relayed as S 2` for `_C_0` and `_C_7`, client
`world S ... applied ReceiveOnSatchelExploded`), LOS_O02_SO05a/b completed. The same rows held on b170, b172, b173.
The `WARN card296 ... fire input released inside the windup` rows in that run are 800 ms presses of the card 437
steps, shorter than the 2 s windup: the game's own rule, known noise. A playtest launcher shot that ends with
`fire ability none activated 0` after a hold longer than the windup reopens this card.
