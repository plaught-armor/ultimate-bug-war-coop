# Card 587: client fire while it steers the guided missile is dropped on the host

Status: closed 2026-11-21, not reproduced (c587b422b).

## Finding

The host logged two WARNs, one right after the other:

- `WARN card181: Round from BP_PlayerController_C ignored: pawn BP_GuidedMissile_C has no round row`
- `WARN card157: HitClaim shot 1 ... found no round in 1500 ms: dropped`

The client possessed BP_GuidedMissile_C (tactical support) and sent a Round and HitClaim keyed on its possessed pawn.
The host's round table holds only trooper pawns. This is the same class as card 586: a possessed non-trooper pawn
stands in for the player.

## To do

1. Static trace: what client input sends the Round while the missile is possessed? Either the client sends fire it
   should not send (the missile takes the input as its own boost or detonate), or the row must be keyed on the
   player's trooper.
2. Fix by class, together with card 586: one rule that maps a possessed seat or vehicle back to its player's trooper
   at each send site. Do not change c43_roster.
3. Prediction: zero card181 and card157 rows in a run that fires a guided missile from the client.

## Static trace (junior, 2026-11-21): does not trace cleanly yet

Timeline (c582b413b host): the client copy's Fire press at t=166362098, GA_TacSupp_GuidedMissile_C given and the
missile spawned at t=166362106-108, the press end at t=166362110 found "pawn 219859 is gone" (card024, fire.cpp:812),
then the Round (sent t=166362100) and the HitClaim (sent t=166362102) arrived at t=166362114. So the client sent
nothing wrong: both rows left before its possess, and met a PC already on the missile. To-do 1 is answered: key the
rows on the trooper.

Host sites that read the player from `PC->Pawn` (kControllerPawn) and break while a seat or missile is possessed:

- round_c181.cpp:393 `c181_receive_round` (the WARN) and the c181_round_credit gate `sprint_side(pawn) ==
  kSideServerCopy && c181_soldier(pawn)`.
- claim_c157.cpp:378 `claim_check` (shooter location for the range test), :453 `copy_weapon`, :556 `made_round`
  (world), :486 (log only).
- fire.cpp:812 card024 press end (the pawn id match through `PC->Pawn`): the same class, it released nothing.

Rule found: the possessed non-trooper names its trooper. `BP_GuidedMissile_C` has Instigator (kActorInstigator) =
the player's `BP_PlayerCharacterMI_C` (card561 row at t=166362108). The Door Gunner seat has its exposed `Target` =
the trooper (card561 rows, property found). So one helper `void* c587_player_trooper(const void* pc)`: the PC's pawn
when it is a trooper (not a mech), else the pawn's Instigator when that is a trooper, else the seat's `Target`.
No per-PC table and no c43_roster change.

Why it is not clean (needs runtime facts first):

1. `APlayerController::OnUnPossess` calls `SetAutonomousProxy(false)` on the old pawn (UE source), so the unpossessed
   trooper reads RemoteRole SimulatedProxy, and `sprint_side` calls it host own. Every side gate on it (c181 row,
   card160 muting, card314 and others) misreads it while the missile or seat is held. Mapping only the receive sites
   moves the WARN, it does not credit the round. This needs a role read on the unpossessed trooper to confirm.
2. The credit path then fires an owed weapon (`g_orig_weapon_fire`) on a pawn with no controller. Unproven whether
   the weapon fires or faults (ability ActorInfo PC null).

## Proposed next step

A log-only diag (rides any build with a client missile throw): at the card181 WARN and card024 "pawn gone" rows, log
the mapped trooper, its LocalRole/RemoteRole, Controller and equipped weapon. Prediction: trooper found through
Instigator, RemoteRole 2 (simulated), Controller null. Then the fix keys the side test on the PC (remote PC) instead
of the trooper's role, at these sites only.

## Run c587b421a (b421): no repro

Only throw 1 ran. After a guided missile throw, the slot loses its ability: `host charge client 1 20000 1` then fails
with "no TacticalSupportComponent slot ability or charge functions". Throws 2 and 3 never fired. Result: 0 WARN
card181, 0 card587 rows, no card157 or card024 pawn-gone rows. Repeated throws need a new pickup before each throw.

## Run c587b422b (b422 `2ad394a93d9e34bf`): not reproduced, closed 2026-11-21

Three client throws, each from a fresh guided missile pickup (lesson 71). Missile possess rows at t=172900544,
172911311 and 173006594; the trooper came back after each (card490 MI rows, e.g. 172917604 and 173012864). Host:
0 WARN card181, 0 WARN card157, 0 card024 pawn-gone rows, 0 card587 rows. Closed as not reproduced. The two
scenario wait failures in that run were the ClientRestart row hitting the diag cap, not a missing return (lesson 72).
