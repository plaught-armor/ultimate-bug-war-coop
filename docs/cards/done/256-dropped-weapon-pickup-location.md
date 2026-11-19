# 256 Dropped weapon pickup rests at a different place on the client

Found by card 241 run 4 (`logs/c241-c2m1-r4-*`). Visible sync gap (a dropped Morita lies elsewhere on the
client); not a play blocker.

## Evidence

- Digest: 99 of 120 rounds with mismatches from round 23 (173805203) on, classes
  `BP_WeaponPickup_MoritaMk1_C:1` (78 rounds), `:3` (18), `:2` (2).
- WorldOp-spawned dropped pickups, host against client location: #261840 (217550, -26750, 5050) against
  (217500, -26750, 5200); #240483 (216600, -4550, 7950) against (216650, -4600, 8100); #258983 host z 5100,
  client z 2750.
- Not seen in run 5 (same scenario, 83 rounds, only one `Pose:1`). The offsets are 50-150 uu with one large
  z gap, so the drop seems to settle by physics on each machine after the spawn row.

## Step 1 (static, no run)

Trace the dropped pickup path: which row spawns it on the client, whether the pickup simulates or falls
after spawn on either side, and whether the host sends its rest location. Check the other dropped pickup
classes (same base class) for the same path.

## Step 2 (fix by class)

The client's dropped pickup ends at the host's rest location for every dropped pickup class: the host sends
the rest transform once the drop settles, and the client does not simulate its copy.

Proof: c241 scenario with weapon drops on slopes; digest 0 `BP_WeaponPickup_*` mismatches.

Files: decided in step 1. Budget: 1 run.

## Step 1 answers (run 4 logs, `world_c98.cpp`, disassembly, 2026-11-13)

- Source of the drop: `ASTNonPlayableCharacterBase::SpawnDeathDrops_Internal` 0x51A7010 (card 107 caller chain
  0x51A71BD). It calls `UWorld::SpawnActor`, `IgnoreActorWhenMoving`, `FMath::VRandCone` on the up vector and a
  virtual on the root primitive (+0x6F0, the impulse). The drop is native physics with a random direction;
  `BP_WeaponPickup` has no script for it (`kis.py`: only the interaction prompt). Every NPC death drop class
  takes this path.
- Rows: 'P' at spawn (`c104_send_spawn`), 'M' while moving and at rest (`c104_settle_one`, sample every 50 uu
  and 200 ms, rest when asleep or 30 still ticks), 'N' for the enabled byte (`c145_send_copy_enable`). The
  client applies 'M' with `SetActorLocation` (teleport) and puts the root to sleep (`c104_client_rest`).
- So the design in step 2 already exists. Attacked in `c104_settle_one`: root not simulating at spawn (rest row
  at once, then `off_rest` clears `rested`), body read asleep on the first tick, sample gate, swap-remove of
  the three tables in `c104_relay_destroy`, pending path. Every path sends a row once the body is 50 uu from
  the last one. No defect found by reading.
- What run 4 shows for the four mismatched copies (#240483, #261840, #258983, #251559):
  - The client copy is exactly at the 'P' row location (x, y and z) 13 s to 3 min later, so no 'M' row with
    another location was ever applied to it. The host's lies 50 to 60 uu aside and 113 to 126 uu lower.
  - The last WorldOp row for each is 1.5 to 1.75 s after its spawn (1616, 1497, 1752, 1686 ms).
  - Three copies hang at the spawn point with root collision 0 (host 2). #258983 has root collision 2, fell
    through the ground from round 26 on (z -613, later -1008150), and `AActor::Destroy` on it was refused twice
    (`card132: client kept host copy`, caller exe+0x37DC6BD).
  - 157 other copies in the same run and all copies in runs 5, 8, 9 and 10 have no location mismatch. The
    fault is intermittent; no card since run 4 changed this path.
- Not settled by these logs: whether the host sent 'M' rows for the four. The host's row line and its send
  WARN share the 100-line cap (`kC98WorldLines`), the client's rest line has a 40-line cap (`kC104RestLines`)
  and the ledger `recv` lines stop after 600. All were used up 7 min before the drops.

## Step 1b spec (diagnostic and two class fixes, build 4, `world_c98.cpp` only)

- Host WARN on a failed send (`f.sent != f.to`) is logged always, outside the line cap. Same for the journal
  full note.
- Host: one row per relayed pickup at its first rest, outside the cap, bounded by `kC104Copies` per map:
  `card256: host pickup <id> rest at (x, y, z) after <ms> ms, <n> rows sent, simulating <0|1>, <asleep|still>`.
- Client: one row per copy when its first rest row is applied, same bound:
  `card256: client copy <id> rest applied, moved <uu> uu, root collision <n>, simulating <0|1>`.
- Class fix 1: the client's copy of a relayed actor whose root simulates gets `SetSimulatePhysics(false)` on
  the 'P' row. Only the host's rows move it. This ends both the hang at the spawn point with another collision
  state and the fall through ground the client has not loaded. Check first that `OnUpdatePickupStatus` does
  not turn simulation back on (0x51D22F0 calls 0x4C62370 for status 1 and 2).
- Class fix 2: the host sends the rest row again when the digest names a relayed actor with a location
  mismatch (`c132` detail path, at most once per actor per round). A lost or unapplied row then heals in one
  round, for every relayed class.
- Prediction (next FED_C2M1 discovery run, no extra run): every death drop has one host and one client
  `card256` row with `moved` under 5 uu after the last sample; 0 `BP_WeaponPickup_*` location mismatches;
  0 `client kept host copy` rows for pickups that the client did not pick up. If a mismatch shows again, the
  two rows say which side lost the row: host `0 rows sent` means the settle loop, client row missing means the
  send or the apply.

## Evidence 2026-11-15 (b68 `efdda02128e72fdc`, run c239b68r3, FED_C2M1, senior, from logs)

- 21 death drops (`BP_WeaponPickup_MoritaMk1_C`): 21 host `card256: host pickup N rest` rows (16 `asleep`, 5 `still`)
  and 21 client `card256: client copy N rest applied` rows, one pair per drop.
- Client `moved` at the rest row: 0 to 10 uu (13 of 21 at 5 uu or under, 8 at 6 to 10 uu). The prediction said under
  5 uu; the measured tail is 10 uu, far from the 50 to 150 uu of the original fault.
- 0 `BP_WeaponPickup_*` location mismatches (1 digest round, `mismatches 0`), 0 `client kept host copy` rows.
- Verdict: the rest row arrives and is applied for every drop in this run. Not closed on one short run (the fault was
  intermittent); it closes if the playtest logs show the same pairs and no pickup mismatch.

## Closed (2026-11-17 11:11)

The rest row moved every drop to within 10 uu of the host's and 202 logs hold 0 pickup location mismatches and 0 `WARN card256` rows. Reopen on a `BP_WeaponPickup_*` location mismatch in a digest. Evidence base: the 202 host and client logs of builds b150 to b180 written since 2026-11-16.
