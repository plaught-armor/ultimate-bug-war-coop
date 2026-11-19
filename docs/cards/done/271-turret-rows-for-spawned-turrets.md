# 271 Turret rows for mid-map spawned turrets find no client turret

Found by card 243 run 9 (`logs/c243-c3m2-r9-*`, FED_C3M2, DLL 75d9c5ce204aa049).

## Evidence

| side | t | row |
|---|---|---|
| host | 222797828 | `card098: host spawn BP_MountableTurret_C at (-55688,-2503,1929) placed 0 replicates 1` |
| client | 222797859 | spawn of the replicated copy |
| client | 222797886 | `WARN card125: client turret row BP_MountableTurret_C_2147400006 at (-55688, -2503, 1929): no turret matches, skipped` |
| client | 222797886 | `WARN card132: recv 82414 ... WorldOp bbc56c30 T 1946600 result skipped: no placed copy matches` |
| host | 222822122 | spawn of BP_MountableTurret_C at (-56433, 5220, 2205), placed 0, replicates 1 |
| client | 222822133 | spawn of the replicated copy |
| client | 222822242 | the same two WARNs for `BP_MountableTurret_C_2147398104` (recv 85143, WorldOp 087aba59) |

The two turrets appear where the BP_AI_TurretTrooper_C troopers stand (client spawned those at t=222733610
and 222733623). The client copy exists before the row arrives, so this is not an ordering race.

## Cause

`c125_client_turret` (`mod/src/ubw/turret_sync.cpp`, about line 565) looks the turret up with
`c95_find_nearest` (`mod/src/ubw/door_c95.cpp:615`). That lookup only matches placed actors (`c50_placed`) by
exact name. A turret the host spawns at run time and the engine replicates is not placed, and its client copy
has a different local name.

## Related fault (not copied into this card's fix)

The client turret-mount warp is faulty: `docs/cards/done/234-trooper-campaign-soak-rest.md` lines 52 to 60. The
warp to `BP_MountableTurret_C_0` put the server copy 90 uu behind the turret at (-32012, -5982, 2065), with no
floor; it fell to z 567. Card 243 line 161 ("its turret warp is faulty and is not copied") and card 235
line 48 record the same fault. It belongs to the test driver's warp, not to this row path.

## Step 1 (static, no run)

1. List what card 125 rows carry for a turret (mount state, yaw, ammo, occupant) and which of these the
   engine already replicates for a `replicates 1` turret.
2. Check whether `c41_guid` resolves the replicated copy on the client at row time.

## Fix outline (class, every run-time spawned turret)

Key turret rows for non-placed actors by NetGUID (`c41_guid`), with the placed-name lookup kept for placed
turrets. If step 1 shows engine replication already carries every field, the host does not send card 125
rows for engine-replicated turrets instead.

## Step 1 answers and fix as written (2026-11-13)

- Cause: the client's `T` row lookup `c95_find_nearest` (door_c95.cpp, about line 615) matches placed actors only,
  so a turret spawned at run time never matches.
- Fix (`turret_sync.cpp`): `c125_client_turret` falls back to `c271_spawned_turret`, the nearest non-placed turret
  within 200 uu of the row's position (turrets do not move). One log row per matched copy.

## Proof

No dedicated run. The next FED_C3M2 soak through the TurretTrooper area: 0 `no turret matches` WARNs, and a
client mount on a spawned turret shows the same yaw on both sides.

## Senior code review (2026-11-13)

- Defect (log only): `c271_spawned_turret` keeps one `logged_id`. With two spawned turrets whose rows alternate,
  the `card271: ... matched spawned turret` line is written on every row, with no cap. Fix in build 4: a per-map
  line cap (`note_event_cap`) in place of the single id.
- To measure in the run: `c23_collect` over the turret class runs on every `T` row of a spawned turret; report
  the row rate from the log. If it is per tick, cache the match by row name.
- Attacked and held: placed turret inside 200 uu of a spawned one (`c50_placed` excludes it), no match (falls to
  the existing WARN), null result into `is_a`.

## Closed (2026-11-17 11:11)

64 `card271: client turret row ... matched spawned turret` rows in 25 logs and 0 `WARN card271` rows. The rejoin and table cases are cards 467 and 471 (closed). Reopen on a `WARN card271` or `WARN card467 ... no turret copy` row. Evidence base: the 202 host and client logs of builds b150 to b180 written since 2026-11-16.
