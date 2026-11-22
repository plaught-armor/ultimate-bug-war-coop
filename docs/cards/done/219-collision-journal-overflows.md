# Card 219: The collision journal fills and drops rows

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: queued 2026-11-12, from card 147 (three runs, cards 095, 128 and 147 all patched this journal). Static
design trace first, no code and no run until the senior accepts the trace. Budget after that: three runs.

Read: `docs/cards/done/147-from-save-start-proof.md` (caller trace, run 3 results), `c95_relay_collision` and
`c95_replay` in `mod/src/ubw/door_c95.cpp`, the digest's collision compare.

## Seen

c147-r3 (FED_C2M1, DLL `447d59fbfd989e83`): "256 collision rows (peak 256 of 256), 1087 collision rows dropped".
Rows by first caller: exe+0x37C2C74 (`execSetCollisionEnabled`, a Blueprint call) 184, exe+0x51491D9
(`ASTDestroyableProp::ActorFullyLoaded`, the save's value) 71. Rows by name: `BP_KillableCrate_*` about 120,
corpse props 65. The digest read 0 mismatches after the join and the rejoin although rows were dropped.

## Why it matters

A dropped row is a host collision value a joining client may never get. The journal records every write and
cannot tell a write the client makes on its own from one only the host makes, so it grows with play time and no
cap bounds it.

## Do (static, each line marked verified or open)

1. Name the Blueprint function behind exe+0x37C2C74 on `BP_KillableCrate_*` (bytecode), what starts it, and
   whether a client that joins later makes the same write on its own.
2. For each row class of c147-r3: does the joining client reach the host's value with no replay (derived from
   state another relay already sends), or only through the replay. The corpse props from a save are the known
   second kind.
3. Design verdict, one of:
   - keep the journal, with a class rule that admits only writes the client cannot derive (state the rule and
     the expected peak);
   - drop the write journal for collision: at a client's first Pong the host sends current values for the actors
     the client cannot derive (walk of placed non-replicated actors, bounded by a stated count);
   - repair from the digest: a collision mismatch in a digest round makes the host send its value for that
     entry. State what this hides and how long a wrong value can last.
4. The same question for the door rows of the journal: do they have the growth problem.

## Done when

- The trace answers 1 to 4 with a verdict, a step plan and written predictions (0 dropped, peak against the cap,
  digest 0 mismatches after a join on a from-save start). Ends with "Report complete."

## Step 1 answers (static plus `logs/c147-r3-*`, senior, 2026-11-13)

- Growth is bounded. A row is keyed by actor name, component and location (`c95_collision_row`,
  `door_c95.cpp:512`), so the journal cannot exceed the placed components whose collision changes; play time adds
  no rows. `dropped` counts refused writes, not distinct keys, so 1087 overstates the missing rows.
- The 184 rows by caller exe+0x37C2C74 (`execSetCollisionEnabled`) are live crates on a host that loaded a save.
  `BP_KillableBuilding_Base` (parent of `BP_KillableCrate_Base`, which has no collision write of its own) runs
  `ShowActor` from the `SaveFullyLoaded` bound event when `Destroyed` is false (ubergraph 2268, 2129, 2201);
  `ShowActor` calls `SetCollisionEnabled(3)` on the mesh in use. `ReceiveBeginPlay` (2100 to 879) does not call it.
- These rows are real state, not derivable from card 098 'K' rows: the client applies them as `0 -> 3`
  (`card095: client ... BP_KillableCrate_04_C_18 ... StaticMesh: applied, 0 -> 3`). A client has no save load, so
  its crates keep 0. The earlier idea (drop rows for dead killables) does not apply: the rows are for the living.
- The 71 rows by exe+0x51491D9 (`ASTDestroyableProp::ActorFullyLoaded`) are the same class: save-load state the
  client never computes.
- Doors (`g_c95`): 5 rows on the c242 route, keyed by name. No growth problem.
- Replay: `c95_replay` (`door_c95.cpp:458`) sends one reliable ClientMessage per row in one burst. 256 rows in one
  burst arrived whole (client ledger `CollisionOp recv: applied 256`). A larger burst is not proven safe against
  the engine's reliable buffer.

## Verdict and step 2 spec (build 4, with card 200)

Keep the journal (option 1). No row class is dropped. Two changes in `mod/src/ubw/door_c95.cpp` only:

1. `kC95Collisions` 256 to 2048 (row about 150 bytes, 300 kB; the linear key search stays, at most 2048 compares
   per write).
2. Paced replay: `c95_replay` sends the door rows and the first 128 collision rows, stores a cursor per PC, and
   the host tick sends 128 more per tick until the cursor reaches `g_c95c.n`. Rows added during the replay are
   sent live as now (the live fan-out already covers ponged clients), so the cursor stops at the count taken at
   replay start. Cursor table cleared in `c95_map_end` (card 200) and when the PC leaves. The replay log line is
   written when the cursor finishes.

Do not touch the significance and construction filters or the client apply.

Predictions (one from-save FED_C2M1 join, batched with another C2M1 card): host `map totals` and replay rows show
`0 collision rows dropped`, peak under 2048 (expected 300 to 1400), `N of N calls`; client ledger `CollisionOp
recv: applied N skipped 0`; digest 0 collision mismatches after the join; no connection loss during the replay.
If the peak is over 1024, card the from-save class for a per-class rule (client runs `ShowActor` itself).

## Closed (senior, 2026-11-16, b169 `d99dcd4bd6123245`)

Step 2 is in (cap 2048, paced replay). Proof on existing logs, no run spent:

- From-save join, run `c460b169a` (FED_C3M1 checkpoint load, client rejoin): host `card095 ... door and collision
  journal replay ... 1459 of 1459 calls, 1459 collision rows (peak 1459 of 2048), 0 collision rows dropped`; no
  connection loss; the digest after the rejoin reads 0 mismatches (b169 acceptance).
- Fresh joins: `c423b163a` (FED_C2M1) 189 of 189, `c236b169a` (FED_C3M2) 93 of 93, `c448b169b` (FED_C1M1) 39 of 39,
  all 0 dropped.
- The peak is over 1024, so the card's last rule applies: the from-save class goes to card 465 (journal at 71 % of
  its cap; 1033 of the 1459 rows are `execSetCollisionEnabled` writes of the save load).
