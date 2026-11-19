# 495: CTF door opens on the first player's laser pickup

Status: closed 2026-11-18 18:05 (was open (written 2026-11-18 17:02, senior). User report: "CTF door opens before both players have laser gun".

## Finding (c448b240a, b240 `e7a83a34d41871c6`)

| t (ms) | Event |
|---|---|
| 681540470 | The host's own pawn picks up BP_WeaponPickup_LaserMorita_C_0, the only one placed (n=1). |
| 681540477, 681540485 | GiveWeaponByClass LaserMorita runs on the host's own pawn. |
| 681540486 | GiveWeaponByClass runs on the server copy (the mirror). |
| 681540488 | CompleteSubObjective(CaptureTheFlag_O1, CaptureTheFlag_SO1) runs, and the door opens at once. |

The client never walked to the pickup.

The sub-objective completes on the first player's pickup. Class: an objective pickup completes on the first player.
This is card 301 class H/E, single-player assumptions.

## Trace first (before any fix)

1. Find the BP path from the pickup to CompleteSubObjective(CaptureTheFlag_SO1) and to the door open.
2. Check the client's own view: does the server-copy GiveWeaponByClass leave the client holding a LaserMorita? If it
   does, the item part is already covered, and only the timing (the client not at the door) is the bug.
3. List every other objective pickup of the same shape (pickup -> CompleteSubObjective) on the FED maps. The fix is
   for the whole class.

## Fix direction

Hold the completion until every living player holds the item, or until each one has picked it up. The fix must use
the existing hold or gate mechanism (card 288, kC300Closures, the card 227 gate); no new manager.

Reopen when: the playtest still shows the door open before both players hold the laser.

## Trace and fix (junior, b241 `02ee9cd945d0a641`, reviewed by the senior 2026-11-18 17:10)

The client does get the laser, from the card463/267 mirror. The bug is latency.

c448b240a timeline (ms after 681540000):

| ms | Event |
|---|---|
| 488 | The sub-objective completes. |
| 518 | The door row reaches the client. |
| 690 | The client's own laser is applied: the door was open 172 ms before it. |
| 790 | The client applies the peer view of the host's laser. |

The cause is the 250 ms card 050 poll.

**Fix:** `c50_host_nudge()`:
- On the host it zeroes the server-copy next_ms and the peer next_ms. It is called from give_by_class, add_weapon,
  c132_remove_weapon and c254_pickup_implementation.
- On the client, c50_peer_receive zeroes the peer next_ms.

**Class:** every pickup -> Complete*Objective case reaches the client through the same poll, so the nudge covers all
of them: MoritaMk1 ShootingRange_SO1 and TacSupport_SO2, LaserMorita CTF_SO1, and the satchel pickups on C4M2, C5M2 and
C2M1.

**Proof (written before the run):** in c448b241a the client's own Weapons apply lands within 100 ms of the client
receiving the door row, and the peer laser view lands within 100 ms too.
- The user said "have laser gun", so the run also logs the client's equipped weapon (in hand), not only its inventory.
- If the laser is in inventory but not in hand when the door opens, the card stays open.

## Direction change (user rule, 2026-11-18 17:20)

User, verbatim: "they also both, host and client, need to pickup the laser rifle then the door should open to go
into the CTF zone".

The b241 latency nudge stays in: it serves every give. It does not close this card.

**New fix:**
1. The pickup stays takeable for each player who has not taken it. Today its PlayerCollider and InteractionCollider
   go 1->0 on the first take, and the host sends that to the client.
2. The mirror give does not count as a pickup.
3. CompleteSubObjective(CaptureTheFlag_SO1), and the C_1 door open that follows it, wait on the existing card 288/227
   gate until every living player has picked up.

**Class:** MoritaMk1 ShootingRange_SO1 and TacSupport_SO2 (C1M1), and the satchel pickups on C4M2, C5M2 and C2M1.
The trace decides which of these need the per-player rule. A satchel is a carried charge, not a weapon. Write down
the reason for each one.

**Trace first:**
- the BP path from the pickup to the collider off and to CompleteSubObjective;
- where the mirror gives the weapon;
- how a client press on a pickup reaches the host (the c50_interact relay).

**Proof (written before the run):** a c448 step where the host and the client each goto, look and press E at
LaserMorita_C_0.
- After the host's take only: no SO1 complete and no C_1 door row, and the client still sees the pickup.
- After the client's take: SO1 completes and the door opens, with both players holding the laser in hand.

## Design (junior trace, senior approved 2026-11-18 17:26, with conditions)

**Path:**
- `ASTWeaponPickup::PickupWeapon(pickup, character)` credits the taker.
- Then the base `AADPickupBase::Pickup_Implementation` runs DisablePickup (both colliders 1->0, sent to the client)
  and OnPickup.Broadcast.
- The broadcast runs the FED_C1M1 ubergraph at 6572, in this order: RemovePlayersWeapons, GiveLaser, Open Door C_1,
  CompleteSubObjective SO1 at 5408.
- The client gets the laser through the card 267 mirror (GiveLaser uses GetSTPlayerCharacter(0)).
- A client E is relayed to the host through c50_interact and c102_interact, so PickupWeapon runs on the host with the
  client's server pawn.

**Fix (host only, weapons.cpp):**
- g_c495 holds the takers for each per-player pickup.
- detour_c50_pickup_weapon refuses a repeat taker, adds a new taker and runs the original. The hook becomes
  always-on.
- detour_c254 holds the base until the takers cover every living player (the card 227 count). The last taker runs
  it, and so does a recheck in C330_STEP. The table clears at map end.

**Per-player sites:**
- LaserMorita_C_0 (CTF SO1 and door C_1).
- MoritaMk1_C_0 (ShootingRange_SO1).
- MoritaMk1_C_1 (TacSupport_SO3).
- Satchels are excluded: one charge per site, and the objective counts pickups, not players.

**Senior conditions:**
1. Key a taker by its player (roster slot or PlayerState), not by its pawn pointer. A respawned pawn must not take
   again.
2. "Cover" means every living roster player is in the set (containment), not taker count >= living count. A taker who
   left must not stand in for a living non-taker.
3. A base run from the recheck passes a living taker pawn as its character, never a stale or null one.
4. Solo host (one living player): the base runs on the first take, the same as unmodded.
5. A player who joins mid-session while a base is held is a non-taker, so the base stays held until they take.
6. Log a row for every refuse, hold and release.

## Closed (2026-11-18 18:05)

Proven in c448b245d on b245 `c99dbdd0be94ef0b`: drive rc 0, 186 s, 0 EXCEPTION/FATAL, 11 digest rounds with 0
mismatches.

- **MoritaMk1:**
  - The client takes it, and the base is held 1 of 2 (t=685297049).
  - The host takes it, and the base is released 2 of 2 (t=685300311).
  - ShootingRange_SO2 is added after the release.
- **LaserMorita:**
  - The host takes it, and the base is held 1 of 2 (t=685372062).
  - The client takes it, and the base is released 2 of 2 (t=685384826).
  - CompleteSubObjective(CaptureTheFlag_SO1) and door C_1 DoorState 0->2 come after the release. The void-unless
    order checks passed.
  - Both sides hold BP_Weapon_LaserMorita_C in hand (card494 rows on host and client).
- The b241 latency nudge (c50_host_nudge) stays in for every give.

**Not done:**
- The refuse path never fired. The game makes no PickupWeapon call for a player who already holds the weapon, so the
  refuse is a backstop only.
- A refused take skips only PickupWeapon. AddAmmunitionTypeToInventory is not blocked; no run hit it.
- MoritaMk1_C_1 (TacSupport_SO3) is under the same rule but not run.
- The mid-join and down cases are not run.

Reopen when: a playtest shows a door or objective advancing before every living player took their own weapon, or a
second take giving extra ammo.
