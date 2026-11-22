# Card 559: client's supply drop weapon taken back by the host

Playtest 2026-11-20 on b366 (`e62e76f63a05d88f`), FED_C2M1. User report (client): "host absorbed my autocannon when
they walked past me after i got it out of my supply drop".

## Client log (`UBWCoop-menu-20261120-184517.log`)

| Line | Client t (ms) | Row |
|---|---|---|
| 56211 | 101718122 | `card050: Use press: interactor target BP_WeaponPickup_Autocannon_C_2147269944, placed 0, relayed class 1: UseName not sent (not a placed relayed class or copy)` |
| 56213-56214 | 101718122 | client gives itself the autocannon "outside an apply ... the next host list decides" |
| 56219-56220 | 101718122 | `DisablePickup ... bReplicates 0`, the client destroys its local pickup |
| 56247 | 101719040 | `card104: copy of host 258165: BP_WeaponPickup_Autocannon_C at (173716, -21622, 6189)` |
| 56252, 56297 | 101719043, 101722153 | the host's weapon list removes the autocannon from the client (`apply #33 ... removed 1, matches the host`) |
| 56422-56435 | 101735490 to 101737476 | same again: client gives, host pickup copy 271483 appears, client loses it |

## Cause (from the log, host side to confirm)

The supply drop's pickup on the client is a client-local actor (`placed 0`, `bReplicates 0`), not a card 104 copy
of the host's. Its take is therefore not relayed (`UseName not sent`). The host never learns of the take: the host's
own pickup stays live, so the host takes it on walking past, and the host's weapon list for the client has no
autocannon, so the next card 050 apply removes the client's. The card104 copies right after are the host's swap
drops.

## User rule (2026-11-20)

The first fix written here (make the client's supply drop pickups host copies) was rejected. The user's rule: no walk-over
pickup. A player gets a weapon only by a Use press, as in single player. The host took the autocannon by walking
past, so the bug is the walk-over take on the host, not the relay.

## Trace (after the playtest, host log needed)

1. What took the host's autocannon pickup when the host walked past: the pickup's overlap path
   (`AADPickupBase` overlap or auto-pickup for a weapon the pawn already owns or an empty slot), on the host's own
   pawn or on the host's copy of the client's pawn. Name the caller.
2. Whether vanilla single player ever picks a weapon up on overlap. If it does not, the mod path that does it is the
   fault; if it does, the overlap take is gated to a Use press for weapon pickups.
3. Sibling sweep: every pickup kind a pawn can take by overlap (weapons, support items, ammo, health), on both
   sides, listed with its take path.

## Fix direction

Weapon pickups are taken only by the taker's own Use press, on both sides, by class. Walking past takes nothing.

## Prediction

Host walks across a supply drop autocannon without pressing Use: no take row on the host, the pickup stays, the
client keeps any autocannon it took.

## Host log (`ultimate-bug-war-coop-public/logs/UBWCoop-menu-20261120-184651.log`, host t = client t + 241024698)

| Host t (ms) | Client t | Row |
|---|---|---|
| to 342743444 | | the host fires its OWN autocannon (`shot h ... w=BP_Weapon_AutoCannon_C`, seq to 2241) |
| 342742828 | 101718130 | the client's Use arrives. `card050 relay ... copy target BP_WeaponPickup_MoritaMk1_C_2147292799, no client name (not a placed pickup)`: the host runs the client copy's Interact on a nearby Morita drop, not on the autocannon. `WARN card057 ... client slot 2 ... AutoCannon ... dropped (the copy has no weapon of that class)` |
| 342743447-342743677 | 101718749-979 | the host switches to slot 0; the game unequips and removes the host's own autocannon with `pickup 1` (vanilla heavy weapon drop on switch). This drop is the client's card104 copy 258165 at 101719040 |
| 342746782 on | 101722084 on | the host's weapon list for the client has no autocannon; the client's card050 apply removes it (client rows 56252, 56297) |
| 342760113 | 101735415 | `card043: host own interact press (Interact)`: the host pressed Use. 342760114 `GiveWeaponByClass(AutoCannon ...) on BP_PlayerCharacterMI_C (host-own)`, 342760116 `DisablePickup on BP_WeaponPickup_Autocannon_C, placed 0` |
| 342761758-342762111 | 101737060-413 | the host enters a mech (`GiveWeaponByClass(MechRazorback) on BP_BaseMech_C`); the switch drops the autocannon again (`pickup 1`): the client's copy 271483 |

### What the host log shows

1. There is no walk-over take. The host took an autocannon once, by its own Use press at 342760113, 1.6 s before it
   entered a mech (most likely the press meant for the mech; the interact target was the pickup).
2. The client's FIRST loss (101719043 / 101722153) has no host take at all. The client took its supply drop pickup
   locally, the relayed Use hit a Morita drop on the host instead, and the host's weapon list then removed the
   client's autocannon. That is the relay gap from the first cause above.
3. The second time, both took within about 75 ms: the host by Use at client-time 101735415, the client locally at
   101735490. On the host the client's pickup was never taken, so the host could take it.

So the cause on the client's side is the unrelayed take of a client-local pickup (`placed 0`, `bReplicates 0`), and
the host's side is a real Use press, not an overlap. This conflicts with the user's walk-over reading; the fix
direction needs the user's decision (see the report of 2026-11-20).

## User answer (2026-11-20): the weapon vanished from the hand, twice

"the autocannon just disappeared out of my hand. it later happened with a hawkeye." This matches the log: a loss with
no host take, not a walk-over.

The second case, client log line 85095: at 102483342 the client pressed Use on `BP_WeaponPickup_MoritaMk1Scoped_C_2147199329`
(`placed 0`, `UseName not sent`; `card132: sent 50 ... Use 00000000 - -`, an empty Use). The client gave itself the scoped
Morita "outside an apply (#13) ... the next host list decides" and dropped its own Morita. At 102521932 (line 86492), card
050 apply #48 matched the host's list, which had no scoped Morita for the client, and removed it.

Cause (class): a client take of a client-local pickup (`placed 0`, not a card 104 copy) never reaches the host. The
client arms itself locally, and the host's next weapon list takes the weapon away. Each case is a pickup the client
spawned itself (supply drop contents). Card 105 already blocks the client's own swap drops for the same reason ("the
host's copy is relayed").

Sibling sweep: every pickup the client can see that is client-local, and why it exists on the client (supply drop
spawn, level script, ability).

## Fix chosen by the user (2026-11-20): relay the take

When the client presses Use on a client-local weapon pickup (`placed 0`, not a card 104 copy), it sends the pickup's
class and position in the Use row. The host finds its own live pickup of that class nearest that point, within a small
radius, and runs the take for the client's pawn: the same `PickupWeapon` path a relayed placed pickup takes. Its weapon
list then holds the weapon, and its pickup goes away. The client-local spawn stays as it is now. If the host has no
match, the host logs one WARN with the class and point, and the client's next list apply removes the weapon as now.

Prediction: the client takes a supply drop autocannon and a scoped Morita. Each Use row carries the class and point,
one host take row follows for the client's pawn, no card050 apply removes the weapon over the next 60 s, and the
host's pickup is gone.

## b369 (`4d7adc330945a079`, build-b5 copy kept), not run yet

Fix: the client's Use on a client-local pickup now sends a `$<class> x y z` use name (`c50_send_use_name`,
`attributes.cpp`), and the host takes the nearest live actor of that class within 500 uu (`c559_nearest_of_class`,
`interact.cpp`). The take happens only from the client's own Use press (no-walk-over rule). Proof row: `sent by class
and point (card 559)` on the client, with a host take row and no weapon removed from the client afterwards.

### Run c559b371a predictions (junior, 2026-11-21)
Scenario `tools/scenarios/c371-c2m1.txt` (FED_C2M1, b371 `baeebee09d3e7f59`).
- The client throws its resupply flare and presses Use on a client-local weapon pickup from the drop. The client's
  Use row carries `placed 0` and `sent by class and point (card 559)`.
- One host `card050: host t=... relay for ...: match` or `mismatch` row follows, then a host take
  (GiveWeaponByClass of that class on the client's copy). No `refused, no live actor of the client's class` WARN.
- Over the next 60 s no client card050 apply removes the weapon. `client inventory` still lists it, and the host's
  pickup is gone.

## Static trace (2026-11-21): the class-and-point fallback is not reachable in play

Every client supply drop pickup is a card104 host copy that carries a host id. The client's Use goes by host id
("UseName sent by host id") and the host relay matches it (c559b371b), so the card 559 class-and-point fallback is
never reached. The proof route is: use a weapon the client does not own, walk over it and hold for 60 s (nothing is
taken), then press Use (it is taken). Do not force the fallback.

## Run c587b422b (b422 `2ad394a93d9e34bf`, FED_C3M1): proved, closed 2026-11-21

A placed BP_WeaponPickup_EPulse44_C (the client owns only a Morita Mk1; test supply racks hold only the Morita).
- Walk-over: the client was warped 30 uu from the pickup and held 60 s. `client inventory` at t=173463092 and
  173523209: weapons 1 (Morita Mk1) both times. No host GiveWeaponByClass(BP_WeaponDefinition_EPulse44_C) row in the
  hold. Nothing taken.
- Use: the client's Use press at t=173524526 ("placed 1, relayed class 1: UseName sent"); the host relay matched at
  0 uu (t=173524540) and gave BP_WeaponDefinition_EPulse44_C to the server copy. `client inventory` 60 s later
  (t=173584975): weapons 2, EPulse44 equipped. No card050 apply removed it.
- The card 559 class-and-point fallback is not reachable in play (static trace above) and was not forced.
