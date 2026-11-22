# 439 — Client nuke: SpoolDown starts before the Fire animation ends, fire ability stays active

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: open, cause half known (2026-11-16 13:57). The first text of this card blamed a trigger release inside
the Fire animation. That was wrong: `c423b129a`, `c423b131a` and `c423b133a` held the trigger 6500 ms and two of
them show the fault. The hold length is not the trigger.

## Fault

Client-own nuke launcher, first press. In the bad case the client's SpoolDown ability activates one frame
before the 1.00 s Fire animation ends by itself. The WindDown start cuts the animation (`AnimationEnded, key
NukeLauncher_Fire ... 0 ms after its last start` is logged after the WindDown start), the client's fire ability
never ends, `WeaponSystem.EquippedWeapon.Fire` stays on the weapon, no reload runs, and the next press runs
Fire, WindDown in one frame and makes no round (`c423b134a`: `client rounds 0` on press 2).

## What the logs show (4 runs, times from the Fire activation of each side)

| Run | Client Fire to SpoolDown | Host copy Fire to SpoolDown | Client Fire minus host Fire | Client log order in the SpoolDown frame | Press 2 |
|---|---|---|---|---|---|
| c423b129a | 999 ms | 1001 ms | +8 ms | WindDown start, then AnimationEnded Fire (0 ms) | broken |
| c423b133a | 999 ms | 1000 ms | +8 ms | WindDown start, then AnimationEnded Fire (0 ms) | broken |
| c423b134a | 999 ms | 1016 ms | +23 ms | WindDown start, then AnimationEnded Fire (0 ms) | broken, 0 rounds |
| c423b131a | 1016 ms | 1017 ms | -2 ms | AnimationEnded Fire (1016 ms), then WindDown start | good |

999 ms is 60 frames at the 60 fps cap, 1016 ms is 61. In all three bad runs the client's SpoolDown frame is the
first client frame after the host copy's SpoolDown (6 to 7 ms later). In the good run the client reaches its own
animation end 3 ms before the host copy's SpoolDown.

## Two causes still open

1. Host carrier: the host copy's SpoolDown reaches the client's own weapon and activates SpoolDown there before
   the client's animation has ended. Fits all 4 runs with one rule. Against it: the weapon actor does not
   replicate (input_diag.cpp, card 021 note) and no `ClientActivateAbilitySucceed` row or other receive row is
   logged in that frame.
2. Client frame boundary: SpoolDown is started by a 1.0 s timer of the game's own, and after 60 frames the timer
   has elapsed while the animation's summed time is just under 1.0. Fits too (the host copy also shows both 1000
   and 1016 ms), but then single player at 60 fps would break the same way, which nobody has reported.

## Steps

1. Diagnostic, rides the next build: `c439_diag_patch.py` (scratchpad) logs `card439: client t=... nuke
   SpoolDown handle N = R, callers ...` with a 14-frame caller chain for the client's nuke SpoolDown activations
   (6 rows). Resolve the frames against the exe symbols. A chain through the net driver receive path proves
   cause 1; a chain through the timer manager, a latent action or an ability task proves cause 2.
2. Fix by cause. Cause 1: stop the carrier for the client's own weapon (the client drives its own abilities; the
   host copy's activations are duplicates). Cause 2: the mod ends the client's own fire ability when its Fire
   animation is cut by a WindDown start (same end the animation's own finish gives), and the same on the host
   copy.
3. Sibling sweep: every weapon with a spool-down ability (same Blueprint parent) gets the same check in the run.

## Done when

- The `card439` rows name the caller of the early SpoolDown and this card states the cause as fact.
- With the fix, a run with two nuke presses shows `client rounds 1` on both presses whatever the order of host
  and client Fire, and no `WeaponSystem.EquippedWeapon.Fire` tag left on the weapon after the WindDown.
- Receiver-side shot of the second nuke round on the host screen.

## Step 1 result (b136 `7191143d5e2d10f1`, run `c423b136a`, written 14:25)

All 4 `card439` rows carry one chain. Resolved against the exe symbols, innermost first:
`UAbilitySystemComponent::HandleGameplayEvent` < `UGameplayAbility::SendGameplayEvent` <
`UGameplayAbility::execSendGameplayEvent` < `UFunction::Invoke` < `UObject::CallFunction` <
`ProcessLocalScriptFunction` < `ProcessScriptFunction` < `UObject::execLocalFinalFunction` < (the last three again).
The SpoolDown is started by a Blueprint ability graph on the client calling `SendGameplayEvent`. No net driver frame
is in the 14 frames; the root of the script call is above them. Cause 1 (host carrier) has no support left, and it is
not ruled out until the root frame is read.

The run also withdraws the table above as a discriminator:

| Press | Hold | Fire start | SpoolDown | Fire to SpoolDown | Log order in the SpoolDown frame | Pawn ASC after |
|---|---|---|---|---|---|---|
| 1 | 3000 ms, release sent 498707241 | 498706239 | 498707254 | 1015 ms | WindDown start, then AnimationEnded Fire (0 ms) | `WeaponSystem.EquippedWeapon.Fire` still owned at 498708256 and 498708274 (after the WindDown end): bad |
| 3 | 9000 ms, key down | 499021326 | 499022361 | 1035 ms | WindDown start, then AnimationEnded Fire (0 ms) | no Fire tag at 499022364: good |

Same log order in both, so the order and the 999/1016 ms split do not decide the fault. What differs in this run: in
the bad press the release reached the client 1002 ms after the Fire start, 13 ms (one frame) before the SpoolDown; in
the good press the key was down. A 3000 ms hold ends exactly where the 2.00 s WindUp plus the 1.00 s Fire animation
end, so the scenario's hold length puts the release in the last frame of the Fire animation. Not proven as the cause:
the 6500 ms rows above have to be re-read for where their release fell.

Press 2 (498711744) is the damage: the fire ability activates in the press frame with no spool-up, SpoolDown and
SpoolUp follow 11 ms later, no round (`client rounds 0 ... a refused round still owed`), `LOS_O02_SO05a` stays open
and every objective step from scenario line 236 on fails. The tag that marks the bad state is on the pawn's ASC, not
the weapon's (weapon ASC owned tags: `WeaponSystem.Inventory.Equipped` only).

## Step 2 diagnostic (`c439_step2_patch.py`, scratchpad, `--check` ok against the b136 source, for b137)

- `card439: client t=... nuke SpoolDown deep callers (N frames, from frame 8) ...`: up to 56 frames, to the root of the
  script call (timer manager, ability task delegate, input release, or net receive).
- `card439: client t=... EndAbility <class> (handle N), cancelled N, callers ...`: every EndAbility of a nuke launcher
  ability on the client, 24 rows. The `UGameplayAbility::EndAbility` hook moves from the card 094 gate to the
  `UBWCOOP_DIAG` gate; card 094's own row keeps its `UBWCOOP_C94_LOG` condition.

Reading: the good press shows which call ends `BP_WeaponFireAbility_NukeLauncher_C` and from where; the bad press
shows that call missing or a different root for its SpoolDown. The fix is written from that difference. No scenario
change is needed: the 3000 ms hold (bad in this run) and the 9000 ms hold (good) are both in `c423`.

## Older runs re-read (2026-11-16, after step 1)

Fire to SpoolDown time, where the release fell against SpoolDown, and the outcome:

| Run | Fire to SpoolDown | Release | Outcome |
|---|---|---|---|
| b129a | 999 ms | held, +3.5 s | bad |
| b131a | 1016 ms | held | good |
| b133a | 999 ms | held | bad (refire without spool-up after the reload) |
| b134a | 999 ms | +2 ms | bad |
| b135b press 1 | 1000 ms | +302 ms | good |
| b135b press 2 | 1016 ms | held | good |
| b136 press 1 | 1015 ms | 13 ms before | bad |
| b136 press 3 | 1035 ms | held | good |

Neither the release time nor the Fire to SpoolDown time separates good from bad (the clock steps about 16 ms). The
step 1 remark that a release one frame before SpoolDown may be the cause is withdrawn. In b136 the pawn's ASC owned
`WeaponSystem.EquippedWeapon.Fire` at 498708256, before both ammo pickups (498710058, 498710140), so ammo added
during the reload is not the first cause of shot 2 either; the stuck fire ability comes first. The step 2 rows
(EndAbility with the cancelled flag, deep chain) decide.

## Step 2 result and the last-round loss (2026-11-16, run c423b137a, b137)

The b136 "no round" press did not reproduce: all three client presses show the `EndAbility
BP_WeaponFireAbility_NukeLauncher_C` row within 1 ms of the SpoolDown. Presses 1 and 2 are good on both sides
(`client rounds 1, copy rounds 1, refused 1, late-fired 1`).

Press 3 (the last nuke) is lost on the host. Client 500163347, one frame: Fire commits the last round, the
empty-ammo switch cancels the fire ability and starts SpoolDown and the weapon switch, then FireProjectile and the
Round. Host: the copy's fire ability activated at 500163321 and its Fire was refused and owed; the Equip came at
500163352 (switch ability and SpoolDown active); no FireProjectile; `press ended (release (weapon changed)): client
rounds 1, copy rounds 0, refused 1, late-fired 0, forced 0, unused 1` at 500163677. No `card437 ... during its
weapon switch` row, so the forced fire returned before its Fire call.

Not known from the log: why the owed Fire was not ready when the Round came, and which input of
`c437_force_fire` failed. The Round goes by the mod channel and the Equip by a game RPC, so their order on the
host is not fixed.

Next step (`c439d_patch.py`, scratchpad, log only, 3 edits in `round_c181.cpp`): a Round that finds a refused Fire
owed and still becomes a credit logs `card439: host ... Round ... with a refused Fire owed became a credit: owed
<reason>; weapon, key match, switch ability active, fire spec active, spool-down active`. Reading rule: `ability no
longer active` with switch ability active 0 means the Round came after the cancel and before the switch ability
(fix: force fire while SpoolDown is active too); fire spec active above 0 with the owed instance inactive means the
two spec lookups disagree (fix the lookup). The fix must also cover the sibling case, the last round of every
weapon that drops or switches when empty.

## Cause and fix (2026-11-16 15:06, run c423b138a, b138)

Correction: both the Equip and the Round go by the mod channel, in send order. The client's last round runs
`USTWeaponUtilityLibrary::CheckToAutoSwitch` inside Fire, at the ammo commit, so the Equip (msg 220, sent
501086430) left before the Round of the same Fire (msg 221, sent 501086431). The host applied both at 501086449,
Equip first: the copy's fire ability was cancelled (SpoolDown, weapon switch) and the owed Fire was no longer
active when the Round came: `client rounds 1, copy rounds 0, refused 1, late-fired 0, forced 0, unused 1`, no host
nuke. The later `card175 ... ended short by the bound: 7 of 8` row counts that lost round.

Fix (`c439f_patch.py`, scratchpad, 7 edits in `hooks_internal.h`, `weapons.cpp`, `round_c181.cpp`):
1. Client: an Equip sent inside the own Fire is held and sent right after that Fire's Round
   (`c439_fire_begin` / `c439_fire_end` around the Fire in `c181_own_fire`, the hold in `c57_send_equip`).
2. Host: `c57_receive_equip` calls `c439_before_equip`; a copy with an unspent reported round fires one credit on
   its current weapon's fire instance before the equip (the Round came before the copy's ability reached Fire).

Covers by class every weapon that switches when empty (all go through `CheckToAutoSwitch`).

Done when, on a run with launcher presses down to the last round: every launcher `press ended` row has client
rounds = copy rounds and unused 0; one host nuke FireProjectile per client one; the client row `card439: client
... Equip of slot N was sent inside the own Fire: held, sent after its Round` on each last round; 0 host
`became a credit` rows; 0 `card175 ... ended short by the bound` rows; Morita carry count equal on both sides.

## Sibling sweep (2026-11-16 15:17, static)

`CheckToAutoSwitch` (exe 0x145233020) ends in `ASTPlayerCharacter::EquipWeapon(index)` (vtable +0xaa8,
`SendWeaponSwitchRequest(index, 0)`) on two paths:

1. Weapon empty, another slot has ammunition: `index` is that slot. The mod sends an Equip (`c57_send_equip`).
   Covered by `c439f_patch.py`.
2. Underslung active with no secondary round left and primary rounds in the magazine: `index` is the equipped slot,
   which is the underslung toggle. The mod sends a Switch (`c141_send_switch`), inside the same Fire, before its
   Round. Not covered by `c439f`. No log shows it yet (no run fired the last underslung round).

Fix for path 2 (`c439g_patch.py`, scratchpad, 5 edits in `weapons.cpp`, applies on top of `c439f`): a Switch sent
inside the own Fire is held and sent after that Fire's Round; the host's `c141_receive_switch` calls
`c439_before_equip` before it replays the request. If the underslung round does not go through
`USTWeaponFireAbility::Fire` on the client, the hold never arms and nothing changes.

No other mod send runs inside the client's Fire: the only senders reached from `ConstructWeaponSwitchRequest` are
these two.

Done when: a run that fires the last underslung grenade with primary rounds left shows the client row
`card439: client ... Switch (N, 0) was sent inside the own Fire: held, sent after its Round`, the host press row
with client rounds = copy rounds, and the host's underslung state 0 after it. Until then the patch rides along.

## Form 3 (2026-11-16 15:40, c423b140a): client fires with no windup, copy spools, nuke lost on the host

- Client press 502859615, FireProjectile 502859625, no SpoolUp. Seen on every press after a card 430 reload retry
  (b133, b134, b136, b139, b140). Precondition: the fire ability ended while the fire key was still down.
- Host copy spooled 2 s; the reload press and AmmoMirror arrived inside the windup; copy Fire had no ammo.
  `client rounds 1, copy rounds 0, unused 1`.
- Fix c439i: `c437_force_fire` also fires a Round at once when the copy's SpoolUp is active (fire ability idle).
- Done when: host `card437 ... during its spool-up (card 439 form 3) ...: fired at once`, that press
  `copy rounds 1 ... forced 1, unused 0`, nuke FireProjectile counts equal host and client.
- Open: single-player behaviour of the no-windup fire is unknown; card 430 retry may cause it.

## Client root cause and fix (2026-11-16 15:47, c423b140a, static + log)

Cause 1 (host carrier) is proven. The bad press's `cancelled 1` EndAbility (client 502855180) resolves against the
PDB to: `UActorChannel::ReceivedBunch` -> `FRepLayout::ReceiveProperties` -> `FMinimalReplicationTagCountMap::
NetSerialize` -> `UpdateOwnerTagMap` -> `FGameplayTagCountContainer::UpdateTagMap_Internal` ->
`UADWeaponGameplayAbility::OnInventoryOwnerTagCountChange` (cancels when a tag of the ability's list at +0x448 gets
a count above 0) -> `CancelAbility`. The host copy ran 25 ms ahead (Fire 502854173, client 502854198), started
SpoolDown at 502855173, and its SpoolDown tag reached the client's own pawn 1 ms before the client's own animation
end. `UpdateOwnerTagMap` also sets each count to the host's value, over the client's local counts.

Fix c439j (`c439j_patch.py`, scratchpad, 2 edits in `script_c310.cpp`): on the client, a replicated tag map change
(return address 0x4A07914) of `WeaponSystem.EquippedWeapon.Fire/SpoolUp/SpoolDown/Reload` or
`WeaponSystem.Recoiling` on the own pawn ASC is dropped. Built in b143 `bf7d7d82aab68fc8` (b141 + c439i + c132s + c439j); b142 `d4d652b0e3abe035` was never tested.

Done when: client `card439: ... host tag map change of <tag> ... dropped` rows; no `cancelled 1` fire EndAbility
through `0x4A2C9BD`; no `WeaponSystem.EquippedWeapon.Fire` in a `card430` row after the fire ended; no launcher
FireProjectile without a SpoolUp before it.

Open: whether the host pawn also keeps Fire (the dropped rows show the host's values); `EquippedWeapon.Melee` and the
weapon actor's own ASC are not covered (no evidence yet).

## b143 result (run `c423b143a`, 2026-11-16)

Prediction 1 passed (6 dropped rows on the own pawn). Prediction 3 passed (no `EquippedWeapon.Fire` on the pawn after
a fire ended; SpoolDown, Reload and the next SpoolUp ran). Prediction 2 failed once: the cancel at 504730311 still
comes through `0x4A07914`, by a tag outside the five names, 15 ms after the fire's own 1000 ms; state stayed clean.
c439k (diagnostic, b144) logs every host tag map change on the own pawn that is passed, so the next run names the
tag (expected `WeaponSystem.Blocked`, which cannot be dropped wholesale: it also carries host-only causes).

## b144 result (run `c423b144a`, 2026-11-16)

c439k names the remaining host-driven cancel: `host tag map change of WeaponSystem.Blocked by 1 ... passed` at
505672571, the same millisecond as the `cancelled 1` row through 0x4A07914, 16 ms before the fire's own end row.
Accepted, no code change: the client's own SpoolDown activation cancels its own fire in the same way at the natural
end (505595911 and 505603677, through 0x497769B < 0x4A2CB8C), the normal end row follows in the same tick, and
SpoolDown runs its full 1000 ms. `WeaponSystem.Blocked` has host-only sources and is not dropped. Prediction 2 is
replaced by: a cancel through 0x4A07914 comes only from `WeaponSystem.Blocked` +1 and within one host round trip of
the fire's own end. Passed tags over the whole run: Blocked only. Form 3 rows: 0 (c439i forced path not needed with
c439j). Open: underslung proof (c439g) and `EquippedWeapon.Melee` need a C1M1 scenario.

## Scenario for the two open items (2026-11-16 21:38, senior)

`tools/scenarios/c439-underslung-melee-c1m1.txt` (join and rifle pickup from `c141-underslung.txt`): underslung on,
8 presses down to the last underslung round, rifle burst, host melee (control), client melee twice with a rifle burst
after each. Predictions are in the scenario header. Not run yet; rides with the b168 runs.


## b168 runs `c439b168a..c` (2026-11-16 22:29)

c439g proven live: client `Switch (0, 0) was sent inside the own Fire: held`, host underslung 0 after 600 ms, press
row rounds 1/1, 0 "became a credit". Melee: `card169` rows present, 0 `EquippedWeapon.Melee` rows; the burst after
the melee was not reached (3-run budget used). The after-melee burst rides the b169 run of this scenario.


## Closed 2026-11-16 22:44 (run `c439b169a`, b169 `d99dcd4bd6123245`)

Scenario passed, 0 failed lines. Both open items proven: c439g again (client `Switch (0, 0) was sent inside the own
Fire: held, sent after its Round`, 529140286; host Equip row 15 ms later); client melee twice (`card169` spawn and
`held BP_Knife_C ... destroyed` rows), and the rifle burst after each melee reached the host (`playlog: host ...
shot s ... w=BP_Weapon_MoritaMk1_C`, 529148907 and 529153123). 0 `EquippedWeapon.Melee` rows in the client log, so
Melee does not join the dropped tag names. Dropped host tag changes in the run: `Fire` only (2). 0 EXCEPTION, no
new WARN class on either side, session summary `mismatches 0`.
