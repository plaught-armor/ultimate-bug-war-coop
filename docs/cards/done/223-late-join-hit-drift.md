# Card 223: Hits applied to a late joiner land on copies up to 1804 uu from the host's bug

Status: queued 2026-11-12, from card 135 run c135-r2 (client joined FED_C2M1 88 s into the LZ fight).

## Seen

`logs/c135-r2-client.log`, 136041682 to 136042988 (the first 1.3 s after the client's world started): 11 lines
"WARN card140: client hit BP_AI_WarriorBug_C guid ...: host location ... is N uu from the copy's
ReplicatedMovement.Location, kHitDrift 300", 627 to 1804 uu, on three guids (0x4e, 0x8, one more). Early-join C2M1
runs (c147, c208, c210) have none. The class itself is older (c140b, c152-r1 to r3, c186-r2).

## Static first (no run until the senior accepts the trace)

`hit_drift` (`mod/src/ubw/hit_c132.cpp:307`) compares the host's NPC location read at the hit with the copy's
replicated location read at apply; its own comment says a pacing wait adds speed times wait. Two candidate causes,
decide from code and the c135-r2 logs:

1. The hit rows waited: sent during the join, held by the pace queue (card 140) or behind the join replay, applied
   seconds late. Check the row stamps against the apply times and the `netbudget` backlog lines around 136038159
   (join replay) to 136042988.
2. The copies were stale: the copy's `ReplicatedMovement` had not yet received its first update after the join
   (the copy stands at its spawn or initial replicated position while the host's bug has moved).

State which, with log lines. Then the fix direction: for 1, hits older than a bound are dropped or applied without
the position-dependent part; for 2, hits wait for the copy's first movement update. State what the player sees in
each case (blood and wound at the wrong place, or a hit reaction on a bug that is elsewhere).

## Done when

- A late join into a live fight logs no hit drift over `kHitDrift`, in the same scenario as c135-r2, digest 0
  mismatches. Budget: three runs.

## Static answers (c135-r2 logs plus source, senior, 2026-11-13)

- Cause 1, verified: the hit rows waited in the host's pace queue behind the join replay. Host hit on guid 0x4e at
  136038162 (`card132: host hit ... at (225790, 39581, 8617) damage 1.0`); the client applied the same row at
  136041682, 3520 ms later. Host pace rows: 136040782 `queue peak 473 frames 77492 bytes, longest wait 3003 ms`,
  136045791 `longest wait 3882 ms`. The join replay offered 78267 bytes in one burst (`ClientMessage 68594 bytes,
  431 calls`); the host ran 58 frames in that 5 s window.
- Cause 2 adds to it: `netbudget: host NPC guid 0x4e moved and unsent 1340 ms, max send gap 1730 ms` at 136040782
  (7 such rows at the join, 7 in the next window, none later). The copy ran ahead on its own extrapolation
  ((227347, 39339) against the host's (225556, 39528)).
- What the player saw: blood and hit reactions on a bug 6 to 18 m from where the copy stands, for the first
  5 s after a late join. No state is lost; health arrives by the engine's own replication.
- Same root as card 226 (the pace queue drains about 2 kB per frame). No fix of its own: the card closes with
  226's proof plus one late-join run with 0 `card140: client hit ... kHitDrift` rows. Not a play blocker.

## Re-read after card 226's fix (senior, 2026-11-16 10:56; all 2026-11-16 client logs)

51 `WARN card140: client hit ... kHitDrift` rows today, 307 to 1817 uu. 49 are applied in the first tick after a
logged long client frame (`card303: client ... frame of N ms`, 1.0 to 2.1 s; the game's own hitch at the level
end and map join, the host logs the same stack and length at the same moment): the rows were on time on the wire
and the client applied them late, while its copies stood still. Example `c414b109a`: host hit guid 0x1fe at
475390145, client frame of 1080 ms ends 475391206, applied 475391219. The other 2 are 307 and 315 uu, at the
threshold, in a normal frame (one send gap of a moving bug). The pace queue is not in it any more (`longest wait
0 ms` in those windows). No fix: a hit effect one hitch late at a level end is not a play fault. Left for a
batch with spare room: the WARN becomes a plain row when `c132_ledger_frame_age_ms` is over 300 ms, so the WARN
class only counts drift the channel caused. Card stays open for that one line and the late-join run.

## Closed (senior, 2026-11-16)

- The one line is in the code (`hit_c132.cpp` `hit_drift`: a row applied more than `kHitLateMs` after its frame is
  plain, `card223: frame age N ms (applied late, not counted)`).
- Late-join run: `c423b163a` (client rejoined FED_C2M1 at 520652264, mid fight). After the rejoin: 24 hits
  received, 24 applied, `0 drift over kHitDrift (largest 1 uu)`. Also 0 rows in `c448b170a` (2 rejoins),
  `c448b169b` (2) and `c460b169a` (1; 80 hits, largest 25 uu).
- The pace queue cause went with card 226 step 1 (`longest wait 0 ms`).
- The single remaining `WARN card140 ... kHitDrift` row in `c423b163a` (707 uu, frame age 38 ms, before the rejoin)
  is a different cause, the early client ragdoll; it is tracked and fixed in card 140.
- Reopens on: a `WARN card140: client hit ... kHitDrift` row inside 5 s after a join.

