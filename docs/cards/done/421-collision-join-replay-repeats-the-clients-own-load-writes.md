# Card 421: the collision join replay repeats the client's own load writes (95 kB per run, 99% no-ops)

Status: open (carded 2026-11-16 by senior, efficiency audit, card 329 class). Accepted 2026-11-16 as design 2 (b119 `6a3adf36b52871d8`, run c421b119a). The premise in the title was false; see runs 1 and 2 below. Same class as card 420;
build them together.

## Evidence (run c408b115a, b115 `d552217f60765a2b`)

- Host `netbudget` join windows: CollisionOp 135, 93, 179, 109, 150 and 375 rows (12065, 8338, 15963, 9392, 13112,
  35199 B, about 89 B per row): 1041 rows, 94069 B of the run's 94909 B of CollisionOp, 17% of all 541869 B sent.
  Outside the join windows CollisionOp is 1 to 4 rows per window.
- The rows are the host's journal of `SetCollisionEnabled` / `SetActorEnableCollision` writes, most made while the
  map loads (`rows by first caller: exe+0x4C618A1:74 exe+0x51D25D2:30 exe+0x37C2C74:28`: pickup, weapon and
  dropship BeginPlay). The client's own load makes the same writes (`card095: client t=... SetCollisionEnabled on
  BP_TacSuppPickup_DoorGunner_C_0 ... 1 -> 2`, same callers).
- The client logs a replayed row only when it changes its copy (`door_c95.cpp:988`): 11 lines in the whole run
  (9 `BP_SatchelBuilding_Flakbug_C`, 2 `BP_Dropship_LandingBlocker_C`). About 1030 of 1041 rows change nothing.
- With card 420's 219 ext rows these are 354 of the 356 frames paced in the map 1 join window
  (`queue peak 349 frames 57860 bytes, longest wait 1833 ms, 356 paced`); the burst holds back every row queued after it.

## Fix (lane 2, with card 420)

1. Both sides already see every collision write through the card 095 hooks. The client keeps the same journal the
   host keeps (name, place, component, last value), filled by its own writes; today only the host journals.
2. Host, at the join replay: instead of one text frame per row, packed frames of (32-bit hash of the row text
   without its value, value byte), base64, as many as fit in `kChanTextMax` (about 200 per frame).
3. Client: each pair is looked up in its own journal by hash. Same value: nothing. Different value or no such row:
   the hash goes into one `CollisionWant` frame back to the host (new client-to-host op, hashes only), and the host
   answers with today's full `CollisionOp` text row for each (it finds the row by the same hash; an unknown hash is
   counted and dropped: basic crash-safety check, friends-only trust model).
4. Host rows whose hashes collide in its own journal go as today's full rows.
5. Live rows after the join are unchanged.

### Design as written (2026-11-16, before any run; replaces steps 1 to 4 where they differ)

- No client journal. The client compares each pair with its copy's current value (`c95_current_value`), which is
  what the replayed row would have overwritten.
- Pair frame: `CollisionOp` text `~ hash value component hash value component ...` in decimal, filled to
  `kChanTextMax`. The hash is FNV-1a of the actor's `c50_name`; the component stays text (the client finds it by
  name, and the card 150 codec sends a repeated name as a dictionary reference).
- Client: one walk of all level actors per frame batch (placed, not replicated; table of up to 32768 name hashes,
  sorted, kept 1 s, about 25100 actors a map), cost in the totals row. Exactly one live actor with the hash and an
  equal value: done. Anything else (other value, no actor, two actors, table overflow) is asked for.
- Want frame: `CollisionOp` client to host, text `? hash hash ...` (host handler `c421_receive_want`). The host
  answers with every journal row of that name hash as today's whole row, so the apply, the place match, the card
  133 name fallback and the `applied, a -> b` log line are the old path unchanged.
- Whole from the start: rows whose name hash and component another journal row shares, and components with a space.
- A wrong "equal" needs a 32-bit name hash collision between two actors that also share the component name and
  value; nothing is written in that case.
- Totals: `card421: host collision replay to ...` per join, `card421: <side> map totals` per map (WARN on a bad
  frame, an unknown hash, or a full table).
- Added prediction: client own-table walk under 15 ms, at most 2 walks per map.

## Predictions

- Join window CollisionOp: from 93 to 375 rows down to 1 to 2 packed frames plus the wanted rows (about 2 to 9 per
  map), under 1500 B per map; run total CollisionOp under 9000 B (from 94909 B).
- With card 420 the map 1 join burst `queue peak` falls from 349 frames to under 40 and `longest wait` to under
  400 ms.
- The 11 changing rows still apply (same `applied, 3 -> 0` lines for the flak buildings and landing blockers);
  client `missed` 0; digest 0 mismatches; no new WARN class against c408b115a.

## Proof

Same c399 fast chain run as card 420: the netbudget rows, the pace row, the 11 changing applies, and the digest.

## Runs 1 and 2 (2026-11-16, c420b117a on b117 `c9b17057498236df`, c421b118a on b118 `fe8401c4b10d44b6`): premise false

- The client's copies really differ from the journal: 5 of 132 pairs equal on map 1 (20 of 91, 0 of 160, 18 of 109,
  36 of 143 on the next maps). Run 2's cause counters: every asked-for pair had exactly one live copy whose value
  differed (`asked for: 0 with no actor, 127 with a differing copy`), for example `BP_SatchelBuilding_Flakbug_C`
  `Corpse` and `BP_Dropship_LandingBlocker_C` `Box`, host value 0.
- The card's "121 of 132 rows change nothing" came from counting `card095: client ... applied, a -> b` lines, and
  those lines are rate limited (`c95_line`). The rows are needed. Compare-then-ask made the run cost more:
  CollisionOp 106043 B against 94909 B.
- No name repeated on the client in either run (`0 equal over repeated names`, every sample `1 live copies`).

## Design 2 (b119 `6a3adf36b52871d8`, written before run 3)

- The pair is the row: the client writes a pair's value straight to its one live actor of that name hash
  (`c95_client_apply`, the same write and change line as a whole row). It asks (`? hash ...`) only with no copy or
  more than one.
- The host sends a row as a pair only when its own world holds that name hash on exactly one placed, unreplicated
  actor (`c421_own_walk` on the host at replay, at most once a second); otherwise the whole row with its place, as
  before. The host's loaded levels cover the client's, so a name single on the host is single on the client.

Predictions for run 3 (c399 fast chain): per mission map at least 95% of pairs `applied here`, the rest asked for
with `no actor`; `one copy refused` 0; host want frames at most 1 per map; map 1 join window CollisionOp under 4500 B
(from 15519 B on b117, 12065 B on b115); run total CollisionOp under 35000 B (from 94909 B); host own walk under
15 ms; client `missed` 0; digest 0 mismatches; no new WARN class against c408b115a.

## Result (run 3, c421b119a, b119)

- Run CollisionOp 94909 B / 1051 rows (c408b115a) to 23326 B / 83 rows (75% less). Map 1 join window 12065 B to
  4613 B (prediction under 4500 B, missed by 113 B: 7 rows went whole).
- Host: 125, 91, 160, 104, 140 rows as pairs in 4, 2, 4, 3, 4 frames; 7, 2, 16, 5, 3 whole rows.
- Client: every pair `applied here` (125, 91, 160, 104), 0 asked for, 0 refused, 0 repeated names, 0 bad frames;
  own table walk 2.5 to 6.3 ms, one per map. Host want frames 0. Client `missed` 0.
- FATAL 0, digest 0 mismatches, no `WARN card095`/`card421`/`card420`. The WARN classes seen (`card152` bone check
  and pose stream totals, `card137` gib part) are combat rows present in earlier runs (c414b110a).
- Not measured: the host's own table walk has no row (the host totals line prints only with want frames). The same
  walk on the client is 2.5 to 6.3 ms; it runs once per join.
- The run ended early: the scenario failed at line 332 on FED_C5M1 (card 408 plasma block), so FED_C5M2 was not
  joined. Cause logged: the plasma building's yaw at the warp was -57.7 (run c420b117a: -144.7), the `front` warp
  point had no ground and both pawns fell (`WARN card389 ... Falling for 6016 ms at z -19047`). Scenario defect, not
  this card.

