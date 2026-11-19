# 342 A dropship's launch waits up to one sample for its first live frame

Found in run 15 of `c338-c337` (b27 `0ebdac007a685d9c`, `logs/c338-c337-client.log`), 2026-11-14.

- 6289 `WARN card159: ... BP_PlayerTransportDropship_C_2 check: ... 2313 uu off (tolerance 100, speed 0, rtt 30)
  ... spline distance 1095/3400`.
- 6372, 679 ms later, the first `live` row: `spline distance 6534/11778 (client/host, led to 11778 at speed
  11615)`. The client's copy was 5244 uu behind and was moved there in one frame.
- The join row (5861, t=329373546) already had host state 0 (in flight), distance 0, speed 0.

## Cause (static, `dropship.cpp`)

The ship is in flight state before it starts along its spline, so the start is not a state change and
`ds_host_track` sends no L for it. The tracker samples every `kDsLiveMs` (1000 ms), so the first L after the start
comes up to 1 s late. Until then the client's copy flies on its own timing. A check frame inside that window carries
the row's speed 0 (`r->speed`), so its tolerance is 100 uu.

## Fix (b28, host)

`ds_host_launch_edge`, each frame: a tracked row in flight with sampled speed 0 whose spline distance moved more
than `kDsMovedUu` from the sample is sampled and sent as an L that frame, and the next full sample is pulled in to
`kDsEdgeMs` (100 ms) later for the real speed. Cost: one float read per such row per frame. Row:
`card342: host t=<ms> dropship <name> launch edge: spline distance <d>, speed <s>`.

No change to the check: with the edge frame an in-flight speed 0 means a ship that stands still.

## Prediction (`c338-c337-c1m1.txt`)

- One `card342` row for `BP_PlayerTransportDropship_C_2` with spline distance under 1000.
- The client's first `live` row for it after that shows a host distance under 1000, and no later `live` row shows
  client and host distances more than 1000 apart.
- 0 `WARN card159`.

## b28 result (`c18852793a3d72b2`, run 17 `c338-c337`)

0 WARN card159, the check at 330256372 matched (286 uu off, tolerance 733). The two distance predictions missed:
the edge row read `spline distance 3400, speed 15315` because the host's first moving frame was 905 ms long
(`card303: host t=330254270 frame of 905 ms`, begun at the autodropship unpause), so the ship went 0 to 3400 in one
step and the edge fired on that first frame. The client's first two live rows were 2980 and 1717 uu off (`lead 0
ms`, inside `kDsLoadSettleMs`), the third 205, then under 300. Accepted 2026-11-14 with that limit: the edge is sent
on the first moving frame, and a host hitch at the launch still costs the client about 1 s to close.

Open, b29: the row names the class without the instance suffix (use the full instance name). A jump that zeroes
the speed (`kDsJumpSpeed`) re-fires the edge on the next frame: one extra L per hitch or loop restart, 13 rows in
run 17 and 5 in run 18, under `kDsEdgeRows` 64. Left as is.
