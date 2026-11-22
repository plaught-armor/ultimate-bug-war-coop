# 433 — Client Fx play time by capture point (diagnostic)

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: Opened 2026-11-16 by the senior from the efficiency audit.

Fx is the largest client recv cost and 47 % of mod bytes on FED_C2M1. c423b127b client `card330` rows: Fx 849
frames 99.5 ms, 1168 frames 193.7 ms (max 3.69 ms), 1123 frames 155.5 ms (max 4.52 ms), 766 frames 130.4 ms (max
6.65 ms) per 30 s window: 120-170 us per frame. The rows do not say which capture point (NiagaraActivate,
SpawnSystemAttached, SpawnSoundAttached, cue, decal, component) holds the time, so no fix can be chosen yet.

Patch `c433_fx_play_time_patch.py` (fx_c136_net.cpp only): `c433_note` adds each row's play time (the card 406
timer's start) to a per-point table, end rows' deactivate calls to a last slot; `c433_totals` prints one
`card433: client fx play time (<when>): <point> N rows X ms, U us mean, max Y ms` row per used point with the
client fx net map totals and clears the table.

Done when a C2M1 run that ends the map prints the rows. Then: the point with the most time is traced (key lookup,
asset resolve, or the game's own spawn) and gets its own fix card, or this closes as "the game's cost" with the
numbers. The diagnostic stays until then.

## 2026-11-16 13:53: rows read, closed as the game's cost (senior)

Rows summed over all maps of each run (client):

| Point | `c423b133a` (C2M1, 716 s) rows / ms / mean | `c399b134a` (chain) rows / ms / mean / max |
|---|---|---|
| ExecuteGameplayCue_NonReplicated | 7247 / 1031.0 / 142 us | 627 / 143.8 / 229 us / 15.44 ms |
| SpawnSystemAttached | 6339 / 646.7 / 102 us | 484 / 50.8 / 105 us |
| SpawnSoundAttached | 10334 / 603.3 / 58 us | 574 / 35.5 / 62 us |
| SpawnSystemAtLocation | 2418 / 326.3 / 135 us | 100 / 11.6 / 116 us |
| ExecuteGameplayCue | 4282 / 207.4 / 48 us | 344 / 16.7 / 49 us |
| NiagaraActivate | 1933 / 59.3 / 31 us | 853 / 150.5 / 176 us / 23.16 ms |
| all points | 3084.8 ms | 423.9 ms |

C2M1 total: 3.08 s of play calls in a 716 s run, 0.43 % of client wall time, 4.3 ms per second.

Trace of the top point: `client_play_cue` does a cached FName read (`client_tag`), one actor lookup
(`client_key_actor`: the package map's guid lookup, or the card 407 name index through `c95_find_nearest`), a
params constructor and destructor, and the game's own local cue call. Nothing in the mod's part walks or
allocates per row; the 48 us mean of `ExecuteGameplayCue` (same mod path, cheap cue) bounds it. The rest is the
cue's own work in the game (it spawns its Niagara systems and sounds). The same holds for the spawn points: one
lookup, then the game's spawn. The max values (3.8 to 23 ms) are single first-use calls (card 406: not the asset
resolve).

No fix card: the only lever is fewer rows, which is card 431 (parked, bytes) and the card 438 hold. The diagnostic
is removed by `c433_remove_patch.py` (scratchpad, `--check` ok on the b134 tree) in the next build. Closed.
