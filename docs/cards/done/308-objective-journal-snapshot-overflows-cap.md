# 308 The objective journal's snapshot outgrows the 128-frame cap on FED_C2M1

Playtest 2026-11-13, host log `client/UBWCoop-menu-20261113-182510.log` (senior item E, b7 review).

## Trace

Card 066 compactions on FED_C2M1 grow with the objective count until the snapshot no longer fits:

| Host line | Host t | Row |
|---|---|---|
| 14088 | 81923004 | compacted at CompleteObjective: 127 before, 104 after (26 objectives) |
| 65215 | 83745592 | compacted at AddSubObjective: 127 before, 125 after (33 objectives) |
| 69346 | 83987850 | `WARN card066: host journal compaction failed (the snapshot overflowed: 129 frames, cap 128); the journal keeps its 127 old frames and records nothing more until the next checkpoint or map` |
| 69349 | 83987850 | `WARN card049: host journal full (128 frames): later frames are sent live but not recorded` |
| 69350 | 83987850 | `card049: host sent CompleteSubObjective(LOS_O05, LOS_O05_SO05) ... journal 128` |
| 69934 | 84073704 | `card049: host t=84073704 journal replay to BP_PlayerController_C (first Pong of the map): 128 of 128 frames` |

The replay at 84073704 is the frozen journal: it does not hold the LOS_O05_SO05 completion, so a client that
joins or reconnects after 83987850 sees SO05 still open (and would miss SO06 and evac, had they come before
the map ended). About 3.8 frames per objective (Add plus its names frame, Complete, Visibility, counts): 33
objectives give 125 frames, so any C2M1 state past O05 SO05 overflows.

## Cause (static)

- `kC49JournalCap = 128` (`hooks_internal.h:3667`), `config::read_c66_cap` caps at 128 (`flags.cpp:475`).
- `c49_snapshot_frames` (`objectives.cpp:510`) emits per objective an Add (with its card 070 names frame), a
  Complete or CompleteSub when complete, a SetCount for counts, and one Visibility frame for every objective,
  default or not.
- `c66_compact` (`objectives.cpp:596`) restores the old journal on overflow and sets `compact_off` for the map,
  so every later frame is live-only.

## Fix

1. Raise `kC49JournalCap` to 512 and the `read_c66_cap` ceiling with it. One `C49Frame` is
   `(kChanTextFrame = 1408) * 2 + 4` bytes, about 2.8 KB; the journal and `g_c66_saved` together grow from
   about 0.7 MB to 2.9 MB static. The replay cost per join grows with the frames actually held, not the cap.
2. Snapshot skips a Visibility frame equal to the state the Add frame already gives (the Add carries the
   marker and in-list bits): cuts about one frame per objective.
3. Keep the overflow WARN, and add the objective count to it.

- Rows: the existing `card066: host ... compacted ... (N objectives, cap 512)`; new
  `card308: host t=<ms> snapshot Visibility frames skipped <n> of <m> (equal to the Add)`.
- Proof: one C2M1 run with every objective completed by command through LOS_O05_SO06 (journal compactions
  logged, zero `WARN card066` / `WARN card049: host journal full`), then a client reconnect: its replay row
  holds the SO05 completion and the client's objective list matches the host's (`objectives` on both).

## Sibling sweep

Other fixed-cap host journals replayed at join, same failure shape (frozen at the cap, later joiners miss state):

- card 095 door rows (`door_c95.cpp:303/355/874`, "journal full: not replayed"); 12 such rows at 83988052 in
  the same log. Check its cap and whether it overflows on a long map.
- card 098 world journal (470 rows replayed at 84073771) and card 132 decal replay (473 live decals): check
  their caps against the C2M1 totals in the same log.

## Senior verdict 2026-11-13: cause settled, fix narrowed, sweep not done

- Cause settled from the log (`129 frames, cap 128` at 83987850) and `c49_snapshot_frames`. Release fix 1 and 3.
- Fix 2 (skip Visibility frames equal to the Add) is dropped: with the cap at 512 it is not needed, and it rests on
  the client's Add path applying the Add's bits exactly, which is not traced.
- Proof as written is void: a C2M1 step completed by command skips the level script's own add and ends the chain
  (probe `logs/b7b-2-probe-host.log`). Proof is the map-end peak row (`journal peak <n> of 512`, zero
  `WARN card066`, zero `WARN card049: host journal full`) in the next whole-map play of C2M1, and a reconnect there.
- Sibling sweep correction: the 29 `(journal full: not replayed)` rows are **card 098 world rows**
  (`world_c98.cpp:391`, `kC98WorldRows = 512`), not card 095 doors (card 095 map totals: `0 dropped (journal full)`).
  First at 83986119: `world P ...BP_TacSupp_BombingRunProjectile_C ... (journal full: not replayed)`. So the world
  journal filled on C2M1 while later replays hold 470, 175, 167 rows. Open, static, before any code: which row
  kinds held the 512 slots at 83986119, and whether ended live `P` rows leave the journal. That is the same class
  and is fixed in this card.

## Senior trace 2026-11-13: the card 098 world journal overflow (sibling, static)

Settled from the host playtest log and `world_c98.cpp`; this closes the open trace in the verdict above.

- What overflowed: 29 rows, all at 83986119 or later on one map: 9 `P`, 10 `M`, 10 `N`, every one a
  `BP_TacSupp_BombingRunProjectile_C` copy. Map totals row at 84080103: `389 rows, 29 dropped`.
- What filled the 512 slots: not live copies. `card104: host map: relayed actors peak 55 of 256 alive`, and a copy
  holds at most three rows (`P`, `M`, `N`; all three leave in `c104_relay_destroy`, `world_c98.cpp:962-964`), so live
  rows were at most 165. The other 347 or more were permanent placed-actor rows (`S K L B X H T A E`), which
  `c98_journal_put` never removes: one per placed actor and kind for the whole map. The replay 87 s later carried 470
  rows with the bombing run over.
- Harm in the playtest: none found. The live sends went (`sent to 1 of 1 clients`); only the replay copy was lost,
  and the lost rows were projectiles that ended before the next replay (84073771). The harm case is a permanent row
  arriving while the journal is full: a client that joins or reconnects later never gets that door, timeline or ext
  state. The log cannot say whether that happened, because only the first 100 world lines per map are written and
  there is no count per kind.
- Ended live rows do leave the journal; that part of the open trace is answered yes.

Fix to code with card 308 (same class: a fixed journal sized under a real map's peak):

1. `kC98WorldRows` 512 to 1024 (`C98World` is 232 bytes, so 238 kB; the replay loop is three linear passes).
2. A peak row count and a count per kind in the `host world relay map totals` row, so the next whole-map play gives
   the measured peak per map instead of an inference.
3. The existing `journal full` WARN stays; add the kind counts to its first occurrence per map.

Proof: the map totals row of the next whole-map play shows `0 dropped` and a peak under 1024 on every map.

## Senior verdict, b8 run 2 (2026-11-13)

C1M1: objective journal peak 40 of 512, world journal `3 rows (peak 3 of 1024; kinds E:3), 0 dropped`. Caps hold on
C1M1. Other maps are decided by the whole-map soak's peak rows.

## Senior check 2026-11-16

C2M1 whole map (`c423b163a`): world journal `341 rows (peak 372 of 1024; kinds B:7 E:10 K:229 L:4 M:21 N:22 P:22 S:5
X:21), 0 dropped`, objective journal peak 137 of 512, 0 `WARN card066`. C1M1 was 40 and 3 (b8). The b164 to b168 runs
on C3M1, C3M2 and C4M1 have 0 `WARN card066` but no `world relay map totals` row: the row prints at map end and those
scenarios end by `quit`. Owed (small, next build batch): print the world and objective journal totals rows at scenario
`quit` / session end too. Close when C3M1, C3M2 and C4M1 show a peak under the cap.

## Closed (2026-11-17 08:46)

The owed quit-time totals rows exist (`c98_world_totals("quit totals")` from `sync_registry.cpp:375`,
`c308_journal_totals("quit")`). Measured peaks, world journal of 1024 and objective journal of 512, all with 0 dropped,
0 not recorded and 0 `WARN card066`:

| Map | Run | World journal peak | Objective journal peak |
|---|---|---|---|
| FED_C1M1 | b40b173a | 338 | 149 |
| FED_C2M1 (whole map) | c423b163a | 372 | 137 |
| FED_C3M1 | c460b173b | 125 | 61 |
| FED_C3M2 | c236b170a | 73 | 29 |
| FED_C4M1 | c421b119a, c447b147a | 71 | 30 |

Limit of the proof: the C3M1, C3M2 and C4M1 runs are route scenarios, not whole-map soaks. The largest whole map uses
36 % and 29 % of the caps. The whole-map soaks (cards 243, 244) print the same rows; a `WARN card066` row or a
non-zero "dropped" / "not recorded" count in any run or playtest log reopens this card.
