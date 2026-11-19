# Card 437: client nuke fired with no host round when fire is held through the reload

Status: open, found 2026-11-16 in run `c423b133a` (b133 `180eb421f35f5a09`, FED_C2M1). It voided the run from
line 222. Before b131 the client's auto reload did not run (card 430), so this path was not reachable.

## Seen (tick ms)

One `client key LeftMouseButton 6500` hold on the nuke launcher, magazine 1, reserve 1:

- Shot 1: client 494738600 (`shot c seq=1`), host copy 494738607 (`shot s seq=0`, 1 round noted).
- Reload: client try fails under `WeaponSystem.Blocked` 494740581, card 430 retry succeeds 494740600; host copy
  reloads 494740591. Client reload `DoAction` 494742448, `AnimationEnded` 494742498.
- Shot 2, client only: `BP_WeaponFireAbility_NukeLauncher_C` activates 494742515 with no SpoolUp ability before it,
  `FireProjectile` 494742532 (`shot c seq=2 ... rl=1932`). The launcher is then empty with no reserve, so the client
  starts `GA_WeaponSwitchAbility_C` in the same frame (494742532) and drops the launcher at 494742832.
- Host: the switch reaches the copy first (`ConstructWeaponSwitchRequest` and `GA_WeaponSwitchAbility_C` at
  494742540), then the Round and HitClaim (`HitClaim shot 2 ...: 0 rounds noted`). `c276_repress` tries at
  494742545, 494742646, 494742761 (`fire ability active 0, credit waiting 1`); none starts an ability while the
  switch runs. Copy unequips 494742660, drops 494742859.
  `card181: ... press ended (release (weapon changed)): client rounds 2, copy rounds 1, refused 1, late-fired 1,
  unused 1`; `WARN card157: ... HitClaim shot 2 ... found no round in 1500 ms: dropped`.

Result for the players: the client sees its second nuke fly and explode, the host world gets no projectile, no
damage and no objective progress, and the client's last nuke round is spent (`AmmoMirror ... Nuke 0 @Nuke 0`).

## Cause

Card 181 lets the copy fire only inside its own fire ability (`c181_copy_fire` at the copy's `DoAction`, or the
owed instance while its spec is active). The client's round and its weapon switch leave in the same frame, on
different channels (mod op on the PlayerController channel, the ability RPC on the pawn's), and the switch is
handled first. After that the copy's fire ability cannot activate, and a re-press on a spool weapon would need a 2 s
windup in any case. The same order problem holds for any weapon whose last round triggers a switch or drop in the
frame of the shot; the nuke launcher is the one found (heavy weapons drop when empty).

Not known: whether the fire with no spool-up after a held reload is the game's own behaviour in single player. It
does not change the fix (the host must fire every round the client fired), but it decides whether a second card is
owed for the missing windup on the client.

## Fix (proposed, not written)

Host, `c181_round_credit`: when a Round arrives and nothing is owed, and the copy's weapon is the weapon the round
names but its fire ability cannot run (a `GA_WeaponSwitchAbility_C` or unequip ability active on the copy, read with
`c181_spec_active`), fire the round at once on the fire ability's primary instance (`c73_spec_instance`,
InstancedPerActor only) with `g_orig_weapon_fire`, counted as `forced`. Guard: the weapon must still be attached to
the copy (the drop comes 319 ms after the switch in this run). Risk to check in the first run: `Fire` on an instance
that is not active (commit, cooldown, PostFire paths); if it crashes or makes no projectile, the other route is to
hold the copy's switch activation until the credit is spent (bounded 500 ms).

Sibling sweep owed with the fix: every `press ended` row with `unused` above 0 in the six-map chain logs, by weapon.

## Predictions

- Scenario step: one 6500 ms hold on the nuke launcher with reserve 1 (the old c423 line 222 hold, kept as its own
  step after the objective steps so it cannot void them).
- Host: `shot s` rows 2 for the hold; `card181: ... press ended ...: client rounds 2, copy rounds 2, ... forced 1,
  unused 0`; 0 `WARN card157 ... found no round`.
- Host world: the second round's `nuke fx probe: ExecuteGameplayCue GameplayCue.Impacts.NukeLauncherRound` row.
- Single player check in the same build (host alone, same hold): whether shot 2 has a SpoolUp row before it.

## Part B: fire held across a weapon change (same run, 13:35 sweep of `press ended` rows)

`card181: host t=494747323 ... press ended (next press): client rounds 1, copy rounds 0, refused 0, late-fired 0,
unused 1`. Trace:

- The hold was still down when the launcher dropped and the Morita came up (client equip about 494742835, host copy
  `BP_Weapon_EquipAbility_MoritaMK1_C` 494742860).
- Client, key still held: `BP_WeaponFireAbility_MoritaMk1_C` 494743015, `shot c seq=3 w=BP_Weapon_MoritaMk1_C`
  494743034. Key released 494743064.
- Host: `c24_host_guard` ended the relay at 494742863 (`release (weapon changed)`, `pressed weapon 245490 not
  found`). The client sends a press only on a key edge, so the copy's Morita never got one. The Round became a
  credit with no relay; `c343_straggler_press` printed nothing (no release time is kept on this path).
  `HitClaim shot 3 ... 0 rounds noted`, then `WARN card157 ... shot 3 ... dropped`.

Result for the players: after any weapon change with fire held (heavy weapon emptied and dropped, weapon wheel,
pickup swap) the client's new weapon fires for as long as the key stays down and the host copy fires nothing: no
tracers or sound for the host, no damage. One round here because the hold ended 30 ms later; a held Morita would be
a whole magazine.

Fix (proposed): `c24_host_guard`, weapon-changed branch, primary fire of a soldier pawn: release the old weapon as
now, then carry the press to the new weapon (a new relay on it, same kind) once the pawn has an equipped weapon
again; while it has none, keep the relay for at most 1000 ms. The copy still fires only reported rounds (card 181
gate), so a carried press cannot fire rounds the client did not. The client's real release ends it as usual.

Prediction: scenario step with the Morita as second weapon: hold fire 6500 ms on the launcher's last round. Host:
after `release (weapon changed)` a `card024: host relay press (Fire 1)` row on `BP_Weapon_MoritaMk1_C` with a
`carried` note, `shot s` rows for the Morita equal to the client's `shot c` rows inside the hold (within 1), and
`press ended` with unused 0; 0 `WARN card157 ... found no round`.

## 2026-11-16 14:03: part A patch written (senior)

`<scratchpad>/c437a_patch.py` (`--check` ok on the b135 tree; `round_c181.cpp`, `hooks_internal.h`). Not built yet.

- The Round payload was 0; it now carries `c437_weapon_key` (FNV-1a of the client weapon's class name, never 0), read
  before the client's Fire runs. The host needs it: a Round that arrives after the copy finished its switch must not
  be fired from the next weapon.
- Host `c181_round_credit`, after the owed branch and before the credit: `c437_force_fire`. Conditions, all required:
  the copy's weapon class matches the key, `WeaponSwitchAbility` ActiveCount > 0 on the pawn ASC, the weapon ASC's
  first `FireAbility` spec has ActiveCount 0, its primary instance exists and is InstancedPerActor. Then
  `g_orig_weapon_fire(instance)`. A projectile made (`g_c44_fires[server copy]` rose) = `forced` and `copy rounds`
  +1; none made = `WARN card437 ... forced Fire made no projectile, kept as a credit` and the old credit path.
- Press-ended row gains `forced N` between `late-fired` and `unused` (the scenario regex `press ended \(` still matches).

Review (attacked): a Round with key 0 (weapon unreadable on the client) never forces; a Round for a weapon the copy no
longer holds never forces (key mismatch); a normal Round during a switch with the fire ability still active takes the
owed or credit path as before (ActiveCount must be 0); the forced Fire calls the original, not the detour, so the
round gate does not see it and counts are added by hand once. Not attacked by a run: whether Fire on an idle instance
commits ammo on the host copy (junior read: safe). Predictions unchanged (above).

## Part C: a re-spool while fire is held is not relayed (2026-11-16, run c423b136a, not built yet)

Run c423b136a had 0 `card437` rows and the 9000 ms hold ended `unused 1`. Trace: the client's SpoolUp ability
activated again at 499025261 with the button still down, and round 2 left at 499027260. The client sends Fire rows
only on a button edge, so the host heard nothing; its re-press came at 499027296, on the Round row, and the card 175
bound ended the press at 499028629.

Fix (`c437c_patch.py`, scratchpad, 3 edits, client only): `c437_respool_activation`, called from `pl_activation`.
When a `SpoolUpAbility` activates on the own soldier pawn while primary fire is held and the press is at least 500 ms
old (`kC437RespoolMs`), the client sends a release (with the rounds of the press so far) and a press straight to the
host, past the card 178 deferral, and restarts `g_c296.press_ms`. The host then runs its normal press path. No host
change.

Prediction for the 9000 ms hold step: client row `card437: client t=... BP_Weapon_SpoolUpAbility_NukeLauncher_C while
fire is held, about 5950 ms after the press: release (0x..) and press sent to the host`; host two `press ended` rows
for the hold, each `client rounds 1, copy rounds 1 ... unused 0`; no `ended short by the bound`.

## Parts A, B, C proven; part D: carried press counts the old weapon's rounds (2026-11-16 19:59, senior)

Run `c423b159b` (b159), 9000 ms hold: client `card437 ... SpoolUpAbility_NukeLauncher_C while fire is held, 5935 ms
after the press: release (0x100) and press sent` (part C); host `press ended (release (Fire 0)): client rounds 1, copy
rounds 1` at 517668724, then `press ended (release (weapon changed)): client rounds 1, copy rounds 1 ... forced 1`
(part A) and `card437: host ... press carried to the new weapon` 41 ms later (part B). 0 `WARN card157`, 0 `WARN
card437`.

Part D, found in the same rows: the client's release at 517671775 reports `9 rounds since the press` (1 nuke round
plus 8 rifle rounds: the client does not restart its count at the weapon change). The host split the press in two (1
round on the nuke press, 8 on the carried press), so `c175_release_or_hold` (`fire.cpp:569`) held the carried press
for a ninth round: `card175 ... ended short by the bound: the copy fired 8 of the client's 9 rounds (bound 300 ms)`.
Effect: the host copy holds fire up to 300 ms longer than the client did; with an automatic weapon it can fire one
round the client never fired.

Fix (host only): `c437_note` stores the ended press's `fired`; `c437_carry_press` puts it in a new `C24Relay` field
`carried` on the relay it creates; `c175_release_or_hold` takes `carried` off the client's count before the compare
(never below 0). Prediction for the same hold step: `card175 ... release of ... held: the copy fired N of the
client's 8 rounds` or no hold row, and no `ended short by the bound` row for the carried press.

Done when: one `c423` run shows the carried press ending with equal counts and no `ended short by the bound`.

## Closed 2026-11-16 22:30

parts A, B, C proven earlier (section above); part D proven in run `c423b163a` (b163 `67d43ad88c956cf3`): `held release ... done: the copy fired 7 of the client's 7 rounds`, 0 `ended short by the bound`, host `press carried to the new weapon, 1 round(s)`. Card 276 stays a playtest watch (any host `found no round` row reopens).
