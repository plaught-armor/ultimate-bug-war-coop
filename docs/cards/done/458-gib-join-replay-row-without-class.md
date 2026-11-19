# Card 458: gib join replay sends a part with no data class

Opened 2026-11-16 19:40 (senior, from run `c423b159b`, build `885a2976f01932ac`).

## Facts

- Client, 0.5 s after the rejoin: `WARN card137: client gib spawn of host part 469 failed: class not found (-)`,
  then `card148: client gib R row for host part 469 unmapped: no row`. Same pair in `c423b140a` (part 1269).
- Host: `card323: host ... gib join replay ...: 108 at rest, 1 in flight, 16 frame(s), 0 not sent`. So 1 of 109 rows.
- `-` is what `gib_class_path` (gib_c137.cpp:484) writes when the part's data class (`+0x310`) has no path.
- `c137_gib_join_replay` (gib_c137.cpp:1369) sends every row whose part is alive and whose root component is not
  hidden; it does not look at the data class.

## Reading (not proven)

The part went back to the game's pool (data class cleared) while its host row stayed and its root component stayed
unhidden. If so the host shows nothing there either and the row is stale; if the host does show the part, the
rejoiner misses one visible part.

## Next step (one build, with the next batch)

In `c137_gib_join_replay`: when the path is `-`, do not send the S and pose rows; count them and log one row with
the part's class, actor hidden flag, root hidden bit and mesh pointer. Add the count to the `card323 ... gib join
replay` row. The row decides: stale pool part (free the host row at the pool return) or a visible part with no
class path (send the class another way).

## Done when

A rejoin run shows 0 `WARN card137 ... class not found` on the client and the host row names why each skipped part
had no class; if a skipped part is visible on the host, a follow-up fix makes it visible on the rejoiner.

## Closed

Closed 2026-11-17 (senior): c423b173a: host skipped 2 parts (516, 310), both data class null and actor hidden 1, so stale pool parts not visible on the host; 0 class-not-found rows on the client in c423b172a, b173a, b174a. No follow-up needed.
