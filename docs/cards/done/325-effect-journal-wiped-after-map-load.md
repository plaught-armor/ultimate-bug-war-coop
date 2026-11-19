# 325 The effect journal is wiped after the map load, so a joiner misses the map's placed effects

Senior audit 2026-11-14 (class 9, line by line). Cause settled statically and in played logs. Not yet seen by the
user as a named report; the client would lack ambient placed effects (fires, smoke, steam) the host has.

## Cause (settled)

1. During `LoadMap` the map's placed Niagara components activate in BeginPlay. The host captures each
   (`detour_c136_activate`), and `journal_apply` journals every placed Activate (`fx_c136_net.cpp`).
2. `c136_report("map arm")` runs after `LoadMap` returned (`c50_arm_map` -> `c98_arm_map` -> `c136_arm_map`,
   `sprint.cpp:239`, `world_c98.cpp:2852`). It calls `c136_net_arm_map`, which sets `g_host.journal.n = 0` and
   clears the asset table, the name table and the peers. `c136_report` also clears the live point 2 set
   (`g_c136.live.n = 0`), so a later Deactivate of a system spawned during the load names no spawn.
3. The client blocks the same Activates during its own load (`c136_component_skip`: an owned component under a
   blocked decision is skipped; "the host sends the op, for a placed owner too").
4. The join replay sends what is left in the journal: nothing from the load.

`c136_report("map end")` (LoadMap enter, `hooks_ubw.cpp:254`) already resets the same state, so the reset at the
arm is the only wrong one. The objective journal had this exact bug and was fixed by moving its reset to map end
(`sprint.cpp`, comment above `c49_map_end`).

## Evidence

- Run 3 host (`logs/b18r3-c310-host.log`): `host fx net map totals (map arm): 419 rows queued, 0 rows sent ...
  journal 33 (peak 223)`, then `host fx join replay ...: 1 journal rows of 1`. Client: `client fx block map totals
  (map arm): 223 outermost calls blocked, 0 kept, 223 effect calls skipped`.
- Played session (`client/playtest-menu-20261113-182510.log`, host rows): `(map arm): 2906 rows queued ... journal
  199 (peak 1476)`, join replay 4.4 s later: `23 journal rows of 23`. Earlier session
  (`client/playtest-menu-20261108-190720.log`): `journal 271 (peak 539)`, then `27 journal rows of 29`.
- The header comment of `c136_report` expects "usually zeros" between map end and arm. The logs show hundreds.

## Fix

At "map arm" the host only reports: no reset of the journal, asset table, name table, peers, pending row or the
live point 2 set. The reset stays at "map end" and "quit". The counts may reset at both. The client keeps its arm
reset only if its arm always precedes the join replay; confirm from the row order (run 3: client arm 295170929,
host replay 295178320) and from the code path of a connected travel, and say which.

## Second finding, diagnostic only (same build)

Run 3 client totals: `911 rows, 1 replayed, 562 played, 154 ended, misses: 0 asset, 6 key, 153 target`. A target
miss is a cue played on the PC instead of its actor, or a placed Activate whose component was not found and a new
system spawned instead. No row names them. Add, capped at 64 per map:
`card325: client t=<ms> target miss <point> <asset or tag> key <guid n|placed name|world> comp <name> result <text>`.
No fix before the rows are read.

## Sibling sweep (report before coding)

Every reset reached from `c50_arm_map` / `c98_arm_map` and the list in `sprint.cpp` around line 225: for each,
whether the host builds any of that state during `LoadMap` (BeginPlay, construction scripts, level streaming) and
whether a join replay, a digest or a later op reads it. Name each one: reset at arm safe, or same bug. First
suspects: `c132_ext_arm_map` (persistent actor state), `c132_decal_arm_map`, `c121_arm_map`, `c161_arm_map`,
`c132_dropship_arm_map`, `g_c140_placed`.

## Predictions (written before the run)

- Host: `map arm` row prints the journal size and the join replay sends that many rows minus ended ones (run 3
  route: about 33, not 1).
- Client: `replayed` equals the host's replay rows; target misses in the replay 0 (placed components exist on the
  client by name).
- New digest row or one-off count on both sides after the replay: active placed Niagara components, host = client.

## Done when

The predictions hold on C1M1, the sibling list is reported with each entry settled, and the target-miss rows are
read and carded or closed.

## Target-miss rows read (2026-11-16, b100 `661ce5eed52c7165` and b101 `7a7505c44a494c79`)

b100 added owner class, key actor and world point to the `card325: client ... target miss` line. Run c325b100a
(fast chain, passed, 22 rows `mismatches 0`) grouped the misses; b101 fixed the largest class and run c325b101a
(same chain with `UBWCOOP_C136_LOG=1`, passed, 22 rows `mismatches 0`) confirmed it.

| Class | Rows (b101 run) | Verdict |
|---|---|---|
| `NS_FX_Viking_GroundLandingSmoke` on the troop and player dropships, join replay | 60 in b100, 0 in b101 | Fixed in b101. A replicated placed owner has no NetGUID before the first client is sent it, so the journal row had no key and the replay spawned a new system at a stale world point. `host_key` now falls back to the placed key (`c41_guid` 0 and `c50_placed`). b101 client: 49 + 11 rows `played`. |
| `ExecuteGameplayCue ...AI` on `BP_AI_WarriorBug_C` and AI troopers | 29 | Closed. All fall in the 5 s after a map load (t 471931 to 471935 s) while the client's actor channels are not open yet; the cue plays at its own location parameter. |
| `NiagaraActivate` of a pooled component owned by `STWorldSettings` (`NS_FX_FlakbugProjectile_AllInOne` 43, `NS_FX_Frontend_PlasmaSpawnSmoke` 10, `NS_FX_PlasmaBug_MuzzleFlash` 2) | 55 | The row is the only delivery (no client copy of its own) and the client draws a new system at the world point. Counted as a miss although it plays. No spawn row precedes it on the host, so any user parameter set on the component is not sent. Card 409. |
| `NS_FX_AcidSpill` on `BP_HazardousZone_Acid_C` (host-spawned, key world) | 4 | Closed. The zone actor is not on the client; the system is drawn at the world point. |
| `NS_FX_PlasmaBug_Trail` and a no-asset `ParticleFX` Activate on `BP_Projectile_Plasmabug_C` | 2 + 2 | The projectile is not an `ADProjectile`, is not replicated and has no client copy: the client gets a trail that stays at the muzzle and no plasma ball. Card 408. |
| `<PickupFX>`, Landscape and Actor cue rows | 4 | Closed (no asset or no target by design; cue plays at its location). |

Done: predictions held in the earlier accepted runs, the sibling list was settled there, and every target-miss
class is now fixed, closed or carded (408, 409).
