# 239 — A killable crate's drops reach the client twice (own replayed death and the host's rows)

Soak c234-c5m1: the host's turret fire killed `BP_KillableCrate_03_C_6`; the client's replayed crate death spawned
its own drops, then the host's P, M, N and D rows for the host's drops were refused or skipped (card104/card145
WARNs, 13 lines). End state equal on both sides in that run, no visible fault. Risk: with a drop that stays, the
client could hold a second copy or one the host's pick-up row does not name. Static first: which source should
win (the host's rows; the client's replayed death should not spawn drops). After the playtest.

## Step 1 answers (logs c234-c5m1 plus source, senior, 2026-11-13)

- The title is wrong: the client's replayed crate death spawns no drops. Every drop on the client is the host's
  'P' row (`c104_client_spawn`, `world_c98.cpp:934`).
- Cause: the client's own pawn stood on the crate. The copy overlapped it inside `SpawnActor`
  (`AADPickupBase::OnBeginOverlap` 0x4C61750, then `ASTAmmoPickup::Pickup_Implementation` 0x51D33F0, base
  `Pickup_Implementation` 0x4C61DC0, `DisablePickup`, `Destroy`). The client guard `c50_client_pickup_overlapper`
  (`weapons.cpp:1080`) asks `c104_copy_host_id`, but the copy enters `g_c104_copies` only after the spawn returns
  (`world_c98.cpp:948`), so the guard says "not a host object" and the game's own take runs. No `card140` row.
- Result: spawn returns null (`-> no spawn`), and the host's M, N and D rows for that id find no copy (the 13
  WARN lines; 6 `no spawn` rows in the run).
- Play-visible part: the client's own take credits ammo before the host's `Ammo` row arrives. In the run both
  were capped at the maximum (`Shotgun +18: 18 to 18, AddAmmo returned 0`, mirror before: no shotgun ammo;
  `MoritaMK2 +150: 250 to 250`, mirror before: 100). Below the maximum the client is credited twice and its count
  leaves the host copy's until the next `AmmoMirror`.
- Build 3's card 254 skip uses the same test, so it does not cover this case.
- Normal case for comparison (c241-c2m1-r8, placed pickup): host row first, `120 to 240, returned 120`, mirror
  240: one credit.

## Fix spec (by class: a host copy is a host copy from the first instruction of its spawn; `weapons.cpp`)

- `c50_client_pickup_overlapper`: `hosts` is also true while `g_c107_copy_spawn` is set (the actor being spawned
  is the host's copy by construction). Before coding, name the guard that stops the subclass credit
  (`ASTAmmoPickup` `AddAmmunitions`) on the client in the normal case and make sure it reads the same test; if no
  guard exists and the normal case is single only by ordering, report that and stop.
- No change to `c104_client_spawn`, the 'M', 'N', 'D' handlers or the host.
- Sibling sweep: every other reader of `c104_copy_host_id` that can run inside the copy's spawn (`world_c98.cpp:134`,
  `digest_c132.cpp:309`): say for each whether the spawn window matters.
- Prediction (FED_C5M1 run, a player on a killable crate when it dies; batch with card 246): `card104: client ...
  copy of host N ... -> BP_AmmoPickup_...` (no `no spawn`), a `card140` block or `card254` skip row for the copy,
  the host's 'N' and 'D' rows applied, one `card071: client` row per drop with `returned` equal to the gain when
  below the maximum, 0 `unknown host id` rows.

## Coded 2026-11-15 (senior, for b68; not built, not proven)

- `c50_client_pickup_overlapper` (`weapons.cpp`): `hosts` is also true while `g_c107_copy_spawn` is set. One line.
- The "name the guard" step: there is no client guard on the subclass credit (`docs/SYNC_AUDIT.md` entry 4: the
  card 254 cut sits on the base, the `ASTAmmoPickup` credit runs before it). This fix closes only the spawn-window
  hole (copy destroyed by the client's own take, `no spawn`, the host's M, N, D rows refused). The double credit
  below the maximum stays with the entry cut at `TryPickup` and the three `OnInteraction` entries (after the
  playtest, SYNC_AUDIT entry 4).
- Attack (static): a load-time or spawn-time disable has no overlapping character, so `GetOverlappingCharacter`
  returns null and the game's disable still runs (incendiary drop disabled at spawn, enabled on landing). A copy
  spawn of a non-pickup class reaches this test only through another pickup's detour, and every pickup on a
  client is the host's. Siblings `world_c98.cpp:134` and `digest_c132.cpp:309` do not run inside the spawn.
- Proof when a run has a player on a killable crate at its death (FED_C5M1): predictions above. No run planned
  before the playtest.

Correction 2026-11-15 (senior): the "normal case: one credit" line above is wrong. In `c241-c2m1-r8` and in
`c276b67` the client's apply row reads `120 to 240` while the host copy went `0 to 120`: the client had already
credited itself locally. No guard stops the subclass credit in any case; the double credit is general and is
card 384 (pairing of the two credits, b68). The card 239 guard only decides the base `Pickup_Implementation` cut
for a copy that overlaps inside its own spawn.

## Proof attempt 2026-11-15 (b68 `efdda02128e72fdc`, scenario `tools/scenarios/c239-crate-on-player.txt`): not reached

- Runs c239b68 and c239b68r2 were void at the start (scenario order on a direct FED_C2M1 start; fixed in the file,
  cause in its comments). Run c239b68r3 reached the kill: client's server copy 111 uu from `BP_KillableCrate_01_C_25`
  (host log 408404994), host kill 408408593, client `world K ... IsDead now 1` 408408613.
- The crate dropped nothing in that run: 0 `world P ... AmmoPickup` rows on the host (c131k2 had 2 at the crate's
  origin for the same crate). So no copy spawned on the client's pawn and the guard was not exercised. Drops are not
  certain per kill; why is not traced.
- Budget spent (3 runs). The guard stays coded and unproven; it is watched in the playtest logs: a `-> no spawn`
  row or a `WARN card104/card145` for a drop id means it failed, a `card254 ... own pawn 1` row right at a
  `card104: client ... copy of host` row means it worked.

### Trace of the "host dropped nothing" kill (2026-11-15, static, logs only)

Verdict: not a mod defect. A killable crate's drop list is rolled per kill in each process, and the roll can be empty.

- Run c239b68r3 (b68), `kill actor BP_KillableCrate_01_C_25`: the host logged `Health 40.0 -> 0.0: kill relayed` and
  sent WorldOp `K` (ledger row 829, host 408408593), then spawned nothing. The client applied the `K` (408408613) and
  its own OnDeath rolled `BP_GrenadeAmmoPickup_Frag_C` and `BP_AmmoPickup_MoritaLarge_C`; card 105 blocked both.
- Old run c131k2 (build `4cb036fa33c090bc`), same command: crate 01 gave 2 drops, crate 05 gave 3, and crate
  `BP_KillableCrate_06_C_25` gave none (host 170561066: `kill relayed`, `world K`, `-> done`, no `host spawn` row).
  So an empty roll on a command kill predates cards 361-363.
- Same r3 run, crate 06 killed by damage (`Health 15.0 -> -10.0`, host 408432131): 4 host spawns, each with P, M, N
  rows, and 4 `card104: client ... copy of host` rows.

The host's roll is the only one that counts; the client's roll is blocked, so both sides show the same drops. The
sentence above, "Drops are not certain per kill; why is not traced", is answered by this section.

Side observation, not carded: 3 of crate 06's 4 drops fell through the floor on the host and were destroyed at
z -7887 (host 408437698, caller exe+0x37DC6BD); the client destroyed its 3 copies 10 ms later (`host <id> destroyed:
our copy destroyed`). c131k2 shows the same for 2 of 7 drops. The mod does not write physics state on host pickups
(its sleep and move calls are on client copies only), so this reads as the game's own drop physics; both sides agree.
Playtest watch: `card098: host destroy BP_*Pickup*` rows with z near -7850.

## Closed (2026-11-17 11:47)

Closed under the backlog rule (2026-11-17). The spawn-window guard is in every build since b68; the run that found the double source ended with equal state on both sides and no visible fault; three proof runs did not reach the case. Every digest round since (b150 to b182) reads 0 pickup mismatches. Reopen on a playtest or run log with a `-> no spawn` row followed by a pickup digest mismatch, or a crate drop the client can see and the host cannot.
