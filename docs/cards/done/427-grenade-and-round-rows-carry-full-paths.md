# 427: GrenadeSpawn and GrenadeExplode rows carry full asset paths

Efficiency, low priority (after the sync fixes).

## Finding
`logs/c399b122a-host.log`, window t=486924348 (turret and mech fire): GrenadeSpawn 7008 B / 36 rows = 194 B a row,
GrenadeExplode 3166 B / 34 rows = 93 B a row; about 2.0 KB/s while a mounted gun fires, more than Fx (1.4 KB/s) in
the same window.

Row text (`c74_transform_text`, `mod/src/ubw/grenades.cpp:773`): `seq <package>.<class> x y z qx qy qz qw vx vy vz`
with the quaternion at `%.6f`. The class path is about 90 of the 194 B and the quaternion about 40. GrenadeExplode
(`c74_relay_explode`, 1721) carries up to three paths (system, sound, zone class). The client resolves each path with
`swprintf` plus StaticFindObject on every row (`c74_find_asset`, 1750).

## Fix
Path table per client and map: the first row that uses a path sends `<index>=<path>`, later rows send the index; the
client keeps index to object pointers (weak or re-found on a miss) and skips the per-row find. Quaternion at `%.4f`.
A client that joins late gets the definitions again with the first row it sees (per-client sent bits).

## Proof
Same chain window: GrenadeSpawn at or under 110 B a row, GrenadeExplode at or under 50 B a row for rounds; 0 new WARN;
round copies fly and burst as before (one shot pair of a turret burst on the client).

## Re-measure 2026-11-16 12:15 (run c423b127a, b127, FED_C2M1)
Both ops are already `kChanArgsPacked` (`chan_ops.h`, card 150 codec: the path is one dictionary ref after its first
send on a connection). `netbudget ... chan bytes per row` in this run: GrenadeSpawn 73 to 76 B a row (windows of 11
to 117 rows), GrenadeExplode 76 B a row. The 194 B window of c399b122a was mounted-gun fire; not re-measured here.
So the path table of the Fix section is not needed for bytes. What is left of this card:
- GrenadeExplode at 76 B against the 50 B target: read one row's token list before changing anything.
- Client CPU: `c74_find_asset` runs `swprintf` plus StaticFindObject per row; no timing row exists. Add it to the
  card 330 step table before deciding on a pointer cache.
- Quaternion `%.6f` to `%.4f`: 4 B a row, only with another change to the same row.

## Closed (2026-11-17 13:09)

Byte goal met without the path table. The three small leftovers were moved to card 330 (section "From card 427"). No reopen condition of its own.
