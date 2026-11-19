# 419 Plasma bug rounds burst at different points for each player

Found 2026-11-16 in run c408b115a (b115 `d552217f60765a2b`), traced statically the same day. Low priority: sky
decoration on FED_C5M1 only, no damage path.

## Evidence

- `BP_Projectile_Plasmabug` (`ExecuteUbergraph_BP_Projectile_Plasmabug`): `ReceiveBeginPlay` plays the audio component
  and starts `Delay(RandomFloatInRange(4.0, 5.5))`. Ubergraph entry 15 (the delay's resume point) spawns the burst
  effect and sound at the actor location and calls `K2_DestroyActor`. There is no hit event: the round always lives
  out its random time.
- `BP_SatchelBuilding_Plasmabug` `Fire Projectile` spawns the round at the `Muzzle` component transform composed with
  `MakeRotator(0, RandomFloatInRange(-2, 2), RandomFloatInRange(-2, 2))`.
- Both draws are made by each machine for its own round (card 408 keeps these buildings simulated per player, with
  turns driven by host rows). Run c408b115a: rounds spawn 10 uu and 0 uu apart on host and client, host lifetimes
  4.285 s and 5.716 s, client 4.701 s and 5.484 s, flight speed about 14000 to 17000 uu/s, so the burst points are
  about 22000 uu apart. The spread adds up to about 2400 uu at 70000 uu range.

## Fix (class: per-player simulated actors with random draws)

1. Host: on `BP_Projectile_Plasmabug_C` spawn, send one world row with the building id and the round's rotation; on
   its ubergraph entry 15, send one row with the burst location.
2. Client: match own rounds to host rows in spawn order per building; set the round's rotation and
   `ProjectileMovement` velocity direction from the row. Refuse the client's own ubergraph entry 15 (`c128_pe_run`
   refusal path) and call it when the host's burst row arrives, after moving the round to the host's location. A
   round with no burst row after 7 s runs its own entry 15, logged as a WARN.
3. Sibling draws in `BP_SatchelBuilding_Flakbug` (traced 2026-11-16): the firing duration and the time between
   firing are the `Timeout Fire Burst` and `Play Turn In Place` timers, both already driven by host rows (card 408),
   and `TurningRight` is in the turn row. Still drawn per machine: `AnimVariant` in `Set A New Animation`
   (`RandomIntegerInRange` over the clip's vertex-animation variants, written to the `FrameOffset` and `NumFrames`
   material parameters), and the flak effect's spread of -2 to 2 degrees. Carry `AnimVariant` in the card 408 row
   (spare bits 4 to 7) and write it on the client after its own call, then re-run the material parameter writes.

## Proof

One FED_C5M1 run: each client round logs its burst within 100 uu and 100 ms of the host's row, 0 rounds on the 7 s
fallback, both screenshots at a plasma building show the round on the same line.

## Reduced fix for b183 (senior, 2026-11-17 11:31)

The junior's size check put fix items 1 to 3 at about 240 lines (spawn row, per-building matching both ways, new
reflection offsets for the rotation and ProjectileMovement velocity, a ProcessEvent hook for `Set A New Animation`).
Too much for sky decoration. b183 ships the burst row only: the host sends one 'Q' row (event code 3) with the burst
location at its round's ubergraph entry 15; the client refuses its own entry 15, and on the host's row moves its
oldest live own round of the nearest building to that location and runs entry 15; 7 s fallback with a WARN.
Accepted differences, not fixed: the muzzle spread of -2 to 2 degrees while the round flies (up to about 2400 uu at
70000 uu, ended by the move at the burst), and the flak bug's `AnimVariant` clip choice.
Proof: one FED_C5M1 run, each client burst within 100 uu and 100 ms of the host's row, 0 rounds on the fallback,
host and client shots at a plasma building.

## Closed (2026-11-17 12:14)

b183 `d6a486b94456960e`, run c419b183c (FED_C5M1, scenario passed, 0 EXCEPTION, 4 digest rounds 0 mismatches): 6 host burst rows, 6 client rows, each client burst on the same building at the host's point, `burst diff 0.0 uu`, row age 17 to 19 ms, 0 `WARN card419` (no fallback burst). The client's round is moved 1197 to 1916 uu to the host's point at the burst (the accepted muzzle spread over a 4.3 to 4.5 s flight). Shots `host-/client-c419b183c-c419-*-plasma` show the same view (junior viewed). The map totals row prints only at the next map arm and was not reached. Reopen on a `WARN card419` row, or a burst seen at two places.
