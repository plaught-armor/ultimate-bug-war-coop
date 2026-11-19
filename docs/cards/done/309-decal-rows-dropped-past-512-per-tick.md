# 309 Host decal rows past 512 in one tick are dropped: the client misses gore decals

Playtest 2026-11-13, host log `client/UBWCoop-menu-20261113-182510.log` (senior item E, b7 review).

## Trace

- 60 `WARN card132: host decal rows past 512 this tick: a D row dropped, the client will differ`, all `D` rows,
  in two bursts: 30 at 82943073 and 30 at about 84518700 (30 is the per-map line cap `kDecalLines`, so the
  lines undercount).
- Map totals (the real count):
  - line 70641, FED_C2M1: `WARN card132: host decal map totals: 6517 gore decals relayed, 4521 FIFO releases,
    ... 1395 rows dropped ... cap 500`.
  - line 115580, the next map: `5018 gore decals relayed, 4063 FIFO releases, ... 345 rows dropped`.
- Both bursts come right after a save load (`imc remove IMC_DummyUI_KBM / SetInputMode GameOnly`; the second is
  followed by `card137: host load reconcile`). In the same 2 ms the host relays mass corpse kills
  (`BP_WarriorBug_Corpses_Destructible_01_C_* Health 400 -> 0: kill relayed`), and gore traces run
  `SpawnDecalAttached` 7000-7050 times, mostly on the Landscape (world rows). Each new decal past the pool cap
  (500) also FIFO-releases an old one, so one burst asks for about two rows per decal.

## Cause (static, `mod/src/ubw/decal_c132.cpp`)

- `kDecalEvents = 512` rows per tick (line 69). `host_event` (line 245) drops the row past it.
- The spawn path (line 1200) adds the decal to `g_dh.live` before `host_event('D')`. A dropped D leaves the
  decal live on the host and absent on the client. Its later E (FIFO release) row reaches a client with no
  copy, and every R round row reports a live count the client cannot match.
- Most of a burst's D rows are released again by the FIFO within the same burst: the client would have drawn
  and removed them in one frame.

## Fix

The host keeps the D row as owed instead of dropping it, and coalesces a spawn and release in the same burst:

1. `host_event('D')` past the cap: the decal's id goes on an owed list (existence-based; the live row
   already holds every field, which `host_flush` re-reads for D rows anyway).
2. A release (E/FIFO) of an owed id removes it from the owed list and sends no E row: the client never had it.
3. `host_flush` first sends owed D rows of still-live decals, oldest first, up to the per-tick budget; the
   rest stay owed for the next tick. An owed row's age is its real age (the client fade starts from it).
4. An R round row is held while the owed list is not empty, so the round compares only settled counts.

- Rows: `card309: host t=<ms> decal burst: <n> D rows owed, <m> released before sending (no row), <k> sent
  late over <ticks> ticks`, once per burst; map totals gain `owed <n>, coalesced <m>, sent late <k>` and
  `rows dropped` should stay 0.
- Proof: one C2M1 run: complete objectives by command to the O05 save, kill a corpse-heavy area
  (`clear 6000 kill`), `travel save` then `travel checkpoint` (the playtest burst trigger). Host map totals:
  0 rows dropped; client decal totals: 0 E rows for an unknown id and every R round matched.

## Sibling sweep (same class: per-tick row budgets that drop past a cap)

Check every `note_event_cap` caller and every "past %d this tick" WARN for drops that leave host state the
client never gets (card 132 pose rows, card 177 sprites, card 269 rest rows), and whether a load burst can
reach their budget.

## Senior verdict 2026-11-13: cause settled, simpler fix shape, sweep not done

- Cause settled (`WARN card132: host decal rows past 512 this tick`, `1395 rows dropped`, `host_event` drop with the
  decal already live).
- Fix shape, simpler than an owed list: "owed" is one bit on the live decal row (not yet sent). `host_flush` sends a
  D row for every live decal with the bit clear and sets it; D rows no longer pass through the 512-event array. A
  release of a decal whose bit is clear sends no E row. The pool cap is 500, so the unsent live decals of one tick
  always fit and nothing is carried to a later tick; E rows are at most the 500 decals sent before. The R round then
  needs no hold. If the pool cap is ever raised past the per-tick send budget, the bit carries the rest over.
- Proof as written is void (command completion to the O05 save breaks the C2M1 chain). Use any map with a save and
  corpses: `clear 6000 kill`, `travel save`, `travel checkpoint`; host totals `0 rows dropped`, client `0 E rows for
  an unknown id`, every R round matched.
- The sibling sweep is a list of things to check, not a result. It is done statically, with a row per budget (cap,
  what is lost past it, whether a load burst reaches it), before the card is coded.

## Junior sibling sweep for b8 (2026-11-13, static, playtest host log `client/UBWCoop-menu-20261113-182510.log`)

One row per per-tick or per-map budget on a host relay path. Measured values are the FED_C2M1 map totals (host
lines 70560-70700, the map with both load bursts at 82943073 and 84518700) unless named.

| Budget (file) | Cap | What is lost past it | Measured on C2M1 | Load burst reaches it? |
|---|---|---|---|---|
| Decal rows per tick, `kDecalEvents` (`decal_c132.cpp:69`) | 512 rows per tick | The D row: the decal is live on the host and absent on the client; its E row and the R rounds then differ | 1395 rows dropped | Yes, both bursts. This card's fix |
| Card 098 world journal, `kC98WorldRows` (`world_c98.cpp:210`) | 512 rows per map | The replay copy of a row (the live send still goes); a later joiner misses a permanent row | 29 dropped, all `BombingRunProjectile` | Not a burst cap: a map-long fill by permanent rows. Card 308 fix (1024, peak and per-kind counts) |
| Card 049 objective journal, `kC49JournalCap` (`hooks_internal.h:3667`) | 128 frames per map | Later frames are live-only; a reconnect replays a frozen journal | Overflowed at 83987850 | Not a burst cap. Card 308 fix (512) |
| FX rows per tick, `kQueue` (`fx_c136_net.cpp:69`) | 4608 rows per tick | The fx row (the client never plays it) | `0 queue full`, journal peak 398 of 4608 | No: a tick's rows are bounded by the journal, also 4608 |
| FX journal, `kJournal` (`fx_c136_net.cpp:65`) | 4608 rows | The replay copy of a live spawn | `0 journal full`, peak 398 | No |
| Hit relay per tick, `kHitMax` (`hit_c132.cpp:69`) | 96 hits per tick | The hit row (no gore or impact on the client) | 7374 queued, 0 dropped | No: the load's mass corpse kills are Health writes (`kill relayed`), not hits; 0 dropped across both bursts |
| Pose journal rows, `kPoseRows` (`pose_c132.cpp:58`) | 128 corpses tracked per side | The corpse pose (the client's corpse rests where its own ragdoll put it) | 487 tracked over the map, 0 dropped | No: 0 dropped across both bursts (rows leave as corpses are removed) |
| Pose stream per step, `kPoseStreamPerStep` (`pose_c132.cpp:102`) | 24 samples per step | Nothing: the rest wait a step | 0 cap hits, peak 7 | No |
| Pose stream backlog, `kPoseStreamBacklog` (`pose_c132.cpp:106`) | Skips a sample while a PC's pace queue holds more than 8 frames | A mid-fall sample only; the final snap is always sent | 290 backlog skips (next map, line 115581 area) | Yes, by design: a load burst fills the pace queue, so samples are skipped and the finals carry the end state. No state is lost |
| Card 177 sprite death rows (`remote_body.cpp`) | 256-row table, 32-row wait list | The death sprite row (`table full`, `wait list full` rejects) | table peak 22 of 256, wait peak 0 of 32, all rejects 0; 70 owed (sent when named) | No |
| Card 269 rest rows, `kRestRows` (`pose_c132.cpp:2599`) | 256 rows per side | The rest row (corpse rest pose), with a first-time WARN | peak 23 of 256, no WARN | No |
| Card 095 door journal, `kC95Doors` (`door_c95.cpp:26`) | 64 doors per map | The door's replay copy | 5 doors, 0 dropped | No |
| Card 095 collision rows | 2048 rows per map | The collision row's replay copy | peak 233 of 2048 (FED_C1M1), 0 dropped | No |
| Card 132 dropship rows, `kDsRows` (`dropship.cpp:105`) | 64 per map | The dropship state's replay copy | 0 journal full | No |
| Card 104 relayed copies, `kC104Copies` (`world_c98.cpp:250`) | 256 alive per side | The relayed actor copy | peak 55 of 256 | No |
| Pace FIFO (`ue/chan.cpp`, 8192 frames, 1 MB chars per PC) | Never drops while the PC lives: a full FIFO sends its oldest frames early | Only the frames of a PC that is gone (no receiver) | not reached | A burst fills it and is drained early (card 294 priority sends skip it) |

Result: the decal per-tick array is the only per-tick budget a load burst reaches with state lost. The two
per-map journals that lost replay copies (world rows, objective frames) are fixed in card 308. No other budget
needs code in b8.

## Senior review of b8 (2026-11-13, static)

Code accepted. Sibling sweep accepted (decal array is the only per-tick budget a load burst reaches with state lost).
Residual: M, T, E and R rows still go through the 512-row tick array; a dropped E leaves a released decal on the
client, a dropped M or T leaves a D naming a table row the client lacks. The b8 run's `0 rows dropped` decides: any
drop means E moves to the same per-row bit and M/T are sent before the D flush without the array.

## Senior verdict, b8 run 2 (2026-11-13): unproven

Host decal totals never printed (they come only from `c50_arm_map`; an in-place load does not reach it, and the run
quit without a map end). No burst occurred. Client totals: 0 round mismatches, 0 releases with no decal. b10 prints
the host totals at the respawn-in-place reset. Prediction: the host row shows `0 rows dropped`; a burst past 512 M, T,
E or R rows in one tick is still the residual, decided by the whole-map soak.

## Senior verdict, b10 last run (2026-11-13)

Host row at the respawn in place: `284 gore decals relayed ... 0 rows dropped ... most D rows in one tick 137`. No
burst past 512 M, T, E or R rows occurred; the residual is decided by the whole-map soak's totals rows.

## Senior note 2026-11-14 (b12 run 2)

Run 2: the respawn totals row prints on every host in-place load (two loads, 0 rows dropped). The burst itself is still unproven: no corpses on C1M1 at the loads. Proof waits for the C2M1 soak.

## Closed 2026-11-16 (senior, C2M1 whole-map run `c423b163a`)

The proof the card waited for: C2M1 host totals `532 gore decals relayed, 34 FIFO releases, 0 other releases, 10
rounds, 0 rows dropped ... most D rows in one tick 15`. FED_C3M1 `c460b168a`: `178 gore decals relayed ... 0 rows
dropped`, most D rows in one tick 10. No tick came near 512, so the owed-list path stays unexercised; a playtest log
with `rows dropped` above 0 reopens the card.
