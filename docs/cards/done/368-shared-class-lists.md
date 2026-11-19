# 368 — Periodic level walks share one cached list per class

Class: efficiency (cards 329, 330, 367). Source: run b52 (`logs/b52-{host,client}.log`, DLL `e3d10276a0c5ff9b`),
map-end rows of the FED_C2M1 stay (2118 client frames).

## Findings (play-relevant; a level walk is about 30.7k actors, about 0.5 ms)

| # | Side | Step | b52 cost | Cause |
|---|---|---|---|---|
| 1 | host | `c28_step` | 326.1 ms, 146.6 us mean, 96 calls over 1 ms | own raw level walk for player characters every 100 ms |
| 1 | client | `c28_step` | 249.6 ms, 118.4 us mean | same |
| 2 | host | `c125_step` | 258.2 ms; collect `mod+0x6cd2b` 350 calls 256.8 ms | `c125_host_rows` walks for turrets every 100 ms |
| 3 | client | `c208_step` | 114.5 ms, 54.3 us mean | `c43_client_other_pawns`: raw level walk every 200 ms |
| 4 | client | collect `mod+0x95706` | 200 calls, 111.7 ms | `c50_find_placed` from `c132_dropship_receive`: two walks per L frame, each second per dropship |

`mod+0x95706` was named a turret site in card 367; the disassembly (the function after `c50_receive_use_name`, static
list, call through a class-function pointer) shows it is `c50_find_placed` (interact.cpp). Its other callers are door
frames and a test command, both event driven.

## Change (source in, not built)

- `c368_actors(cache, world, cls, out)` in `hooks_ubw.cpp`: one `C368Cache` per class (`g_c368_players`,
  `g_c368_turrets`). The walk runs once per `kC368WalkMs` (250 ms) and on a world change; calls between read the kept
  actors through weak refs. Without the weak-ref functions every call walks, as before.
- Readers: `c28_step`, `c43_client_other_pawns` (so `c208_step`, `c110_others`, `c43_revivable_near`, the card 227
  presence count), `c43_client_host_pawn` (test drivers; now the first entry of the same list), `c125_host_rows`.
- `ds_find_placed` (dropship.cpp): the named placed dropship kept by weak ref, 8 entries; a miss or a dead entry walks.
- Not changed: `c37_scan` (card 367, own cache), `c271_spawned_turret`, `c123_nearest_turret`, door lookups (event
  driven and need a fresh list), the diag-only turret poll.

Effect on behaviour: a player pawn or turret is first seen up to 250 ms after it appears (was 100 ms for the body and
turret rows, 200 ms for the weapon track). A destroyed actor drops out in the same frame.

## Predictions for one run of `tools/scenarios/b50-c364-c365.txt` (FED_C2M1 map-end rows)

1. Host `c28_step` under 150 ms (b52 326.1); client under 120 ms (b52 249.6).
2. Host `c125_step` under 110 ms (b52 258.2).
3. Client `c208_step` under 30 ms (b52 114.5).
4. No `collect by caller` entry with about 200 calls from the dropship lookup on the client: `c50_find_placed`'s site
   under 20 calls.
5. Client `c43_step` total under 400 ms (b52 1437.1; the test tail used two walks a frame) and client mod under 6% in
   every window that is not machine-wide slow.
6. Unchanged: remote body shown on both sides, turret `row sent`/`row applied` counts for a used turret, dropship
   `missed` 0, WARN diff against b52 empty apart from guid-specific rows.

Left open after this card: `c132_ext_step` (host 418.9 ms, has its own split rows), `c75_count_step` (24.1 ms), the
host `c43_host_step` 87.19 ms single spike (not traced), 38 raw level walks not behind `c23_collect` minus the 3
removed here.

## Run b53 (2026-11-15, DLL `4e4b316a3a920505`, lane 2, scenario `b50-c364-c365.txt`, b52 run line)

Build clean (rc 0, no warnings, no source fix). EAT 0x54, KERNEL32.dll only; pins updated in the same step. Run
line as b52 (GOD=0, UBWCOOP_C43_TEST=1 and UBWCOOP_C45_TEST=1 both roles, C73_LOG, AUTODROPSHIP on the host): the
scenario header needs GOD=0 and the predictions compare to b52. Ended by the scenario after 289 s at line 67
(RESTART CHECKPOINT, the AI pick-up), as b52 (290 s). Host 15194 lines, client 14689, FATAL/ERROR 0, no crash dumps.
First FED_C2M1 stay 39.4 s (b52 38.5 s).

1. **Met.** `c28_step` host 108.9 ms (b52 326.1), client 26.1 ms (b52 249.6).
2. **Met.** Host `c125_step` 81.0 ms (b52 258.2).
3. **Met.** Client `c208_step` 5.5 ms (b52 114.5).
4. **Not met.** Client collect by caller: `mod+0x95a06` (`c50_find_placed`, interact.cpp:126, its static list)
   120 calls 65.2 ms (b52 `mod+0x95706` 200 calls 111.7 ms). Cause in the log: FED_C2M1 sends 26 distinct dropship
   names (`BP_ProceduralDropshipTroops_C_0` to `_C_22`, `BP_PlayerTransportDropship_C_0/_2/_4`,
   `..._VerySlow_C_1`; the join check alone covers 25), `kDsNamed` is 8, and `ds_find_placed` takes a slot only
   when free or dead, so the first 8 names stay and every frame for any other name walks (two collects when the
   name is the second class). The live frames come for `_C_20` (20 rows), `_C_21` (18), `Transport_C_2` (18).
   Other client sites: `mod+0x13831e` (`c368_actors`) 145 calls 112.2 ms (about 4 a second, as designed),
   `mod+0x45501` (`c37_scan`) 338 calls 255.7 ms, `mod+0x7c348` 38 calls 22.3 ms, `mod+0x40cf1` 7, `mod+0x51a24` 5.
   Host: `mod+0x13831e` 232 calls 170.4 ms (b52 `mod+0x6cd2b` 350 calls 256.8 ms), `mod+0x147f8` 76, `mod+0x72960`
   73, `mod+0x7c348` 37.
5. **Met.** Client `c43_step` 167.2 ms, 77.3 us mean, 21 calls over 1 ms (b52 1437.1 ms, 681.7 us, 165). Client
   mod per window 2.55 to 3.90% (b52 5.57 to 8.51%), apart from the load window 390879436 (6.16%, one 1436 ms
   `cine_skip_step` tick; b52's 9.29%). Host 3.09 to 3.42%, with 5.14% and 6.07% in the two `auto_dropship_step`
   spike windows (690 ms max ticks).
6. **Met where testable.** Remote body: `card028 ... attached to BP_PlayerCharacterMI_C` 3 on each side (b52 3
   and 3). Turret: 0 card125 rows in b53 and in b52 (this scenario uses no turret), so the turret row counts are not
   tested here. Dropship: FED_C2M1 received 154, `no copy` 0 (b52 157, 0); C1M1 received 6, 0. WARN diff against
   b52 (numbers normalized): host empty. Client: card191 frozen 1 to 4 guids (0x24c, 0x392, 0x3a6, 0x37e, three in
   the second stay) with their card152 bone check rows; `WARN cmd ui continue failed` gone; new 1 `WARN card132 recv
   ... Pose ... text R 37e result skipped: no row`, a Pose frame for guid 0x37e 4.4 s after that guid's settled
   final. Death edges: card038 90, card146 5, `WARN card037` 0 (b52 93, 6, 0; kills 11 against 10).

## Senior review of run b53 (2026-11-15)

Rows re-read from `logs/b53-{host,client}.log`; the numbers above match. Predictions 1, 2, 3 and 5 hold.

- **Prediction 4, fix in source, not built:** `kDsNamed` 8 -> 32 in `dropship.cpp` (FED_C2M1 has 26 names). A map
  with more than 32 names walks as before, so the table size is a cost bound, not a correctness bound. Build b54
  carries it; one run must show the client `c50_find_placed` site under 20 calls on FED_C2M1.
- **Client WARN diff is not from this card.** `c368_*` is used only in `remote_body.cpp`, `turret_sync.cpp` and
  `hooks_ubw.cpp`; the card191/card152 rows come from `pose_c132.cpp`, which it does not touch. The three extra
  guids are corpses the test fire hit again in the second stay (guid 0x37e: host resnaps 2 to 5 after
  `SetAllBodiesSimulatePhysics(1)` at Health -112 down to -400), each refreeze leaving bone 0 1.0 to 2.8 uu off
  body 0, the residue card 359 already holds. The `skipped: no row` Pose frame is for 0x37e 152 ms after the
  client's `TermArticulated()` at its gib (t=390987530, Health -15700): the host sent one more pose for a corpse
  that was gibbed. One 445-byte frame, dropped safely; noted in card 359.
- **Not tested:** turret rows (no turret in this scenario). The turret list now comes from the 250 ms cache, so a
  turret placed mid-map is first seen up to 250 ms late. The playtest's first turret is the check; `card125` rows
  sent and applied must match.

## Run b54 (2026-11-15, DLL `cd14c3d92a441cf0`, lane 2, scenario `b50-c364-c365.txt`, b53 run line)

Build clean (rc 0, no warnings), with the senior's `kDsNamed` 32 and the card 330 gore-row edit in
`detour_spawn_gore`. EAT 0x54, KERNEL32.dll only; pins updated in the same step. Ended by the scenario after 293 s
at line 67 (RESTART CHECKPOINT), as b53. Host 14344 lines, client 13832, FATAL/ERROR 0, no crash dumps.

- **`c50_find_placed` under 20 calls: not met by count; the repeat walks are gone.** Client FED_C2M1
  `mod+0x957a6` 28 calls 3.1 ms (b53 120 calls 65.2 ms). 26 distinct dropship names (4 of them transport class,
  a second collect each) give at most 30 cold first lookups with 32 slots; 28 fits one walk per name, no
  repeats. The prediction did not count the first lookups at the join check.
- **Dropship totals: met.** FED_C2M1 received 157, no copy 0, bad 0 (b53 154, 0, 0); C1M1 received 4, 0, 0.
- **Gore: met.** Host `card042 ... Gib ... sent to 1 PCs` 1, client `card042 client Gib` 1, `card039` Gib signal
  1 and 1 (b53 3 each side): one per overkill in both runs (client overkill gore rows b54 1, b53 3). Host gore
  part rows 83 (b53 96), client death gore rows 13 (b53 17), with kills 10 against 11. No card039
  SpawnGoreResponse rows apart from the hook line, as before.
- **WARN diff against b53 by class:** host empty. Client: card191 frozen 4 to 2 guids (0x39a, 0x3a4) with their
  card152 bone check rows (one `live` sample row new), the card152 pose stream map totals row and the card132
  Pose `skipped: no row` row gone, `WARN cmd ui continue failed` back (b52 had it). All card 359 / per-run kinds.
- Steps hold the b53 level: host `c28_step` 104.7 ms, `c125_step` 76.0 ms; client `c28_step` 25.4 ms,
  `c208_step` 2.0 ms, `c43_step` 138.8 ms.

## Accepted 2026-11-15 on b54 `cd14c3d92a441cf0`

Prediction 4 was written wrong: it did not count one first lookup per dropship name (26 names, 4 of the second
class). Run b54 shows 28 calls 3.1 ms at the `c50_find_placed` site on the client (b53 120 calls 65.2 ms), which is
first lookups only, no repeats. The intent (no repeated walks for placed dropships) holds. Left open: turret rows
with the cached list, first checked at the playtest.
