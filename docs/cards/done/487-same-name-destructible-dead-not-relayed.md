# Card 487: placed destructibles dead on host, alive on client (dead host 1 client 0)

Found in c244b213a (C4M1 soak, b213 57aacb585ebdd2f1). Carded only, not chased (backlog rule).

## Evidence
- 22 digest rounds with 8 mismatches each, all "dead host 1 client 0", persisting to run end:
  BP_Bench_Planks_Destructible_01_C_0..5 and BP_Bin_Small_Destructible_01_C_0/1, card237 rows say level FED_C4M1_Art
  (e.g. bin _1 at (11239, -59439, 165), bin _0 at (11464, -54692, 181), bench _1 at (11980, -67174, ...)).
- Host relayed only one of them: t=642409950 "BP_Bench_Planks_Destructible_01_C_0 Health 75.0 -> -874.0: kill relayed".
  Other host kill rows in the window are for _18/_19/_20 (other copies). No "kill relayed" row for bench _1.._5 or bins.
- Around t=642523970 host bin explosion FX at (42904,-59015,204); only H (hide) WorldOp rows went out (seq 49861-49875,
  sent t=642523976, applied on client t=642523991). No K row, so the client's dead flag stayed 0.
- The same actor names exist in more than one streamed level (FED_C4M1_Art and FED_C4M1_Scripting).

## Suspects (trace before any run)
1. c98_host_kill (world_c98.cpp ~790): `c98_journal_row(actor, kC98KindKill) == nullptr` gate. If the journal key is the
   c50_name (no level), a K row journalled for one level's `_N` blocks the other level's `_N`: no relay, no log row.
2. These deaths may not pass the Health hook at all (destroyed by explosion / OnDeath path without a Health write), so
   only the hide reaches the client. Check the dead-flag source the digest reads against the K apply path.
3. Client K apply resolving by name to the wrong level's copy (bench _0 K landed on the Art copy at 37216 in summary
   notes) would explain a single-row mismatch too.

## Done when
Static trace names the gate; fix keys the journal/apply by level + name (class-wide for every C98 kind); c244 rerun shows
0 "dead host 1 client 0" rows for destructibles.

## b214 static trace (suspect 1 and 3 disproved, suspect 2 is the cause)
- The c98 journal is keyed by the host actor pointer (`c98_journal_row` compares `row.actor == actor` and kind), not by
  name, so one level's `_N` never blocks another's. The client K apply (`c98_client_kill`) resolves with
  `c95_find_nearest(world, name, at)`: name plus nearest location, so it reaches the right level's copy. No level+name
  rekey needed.
- Cause: the bridge C4 blast (NS_FX_C4Explosion at 43952,-58255, plus the carpet bombing, t~642523969) killed the
  benches and bins through their Blueprint death with no HealthAttributeChanged call: 0 "kill relayed" and 0 Health
  rows in that window, only H rows. The digest dead bit is `IsDead(health) || c98_bp_destroyed(actor)`, so the host
  read dead 1 and the client, which got only the hide, read 0.
- Fix (world_c98.cpp): `c98_relay_kill` is the one K send (c98_host_kill uses it). A relayed hide calls
  `c487_hidden`: an actor already `c98_dead_now` gets its K row at once ("at its hide"); otherwise it is queued
  (64 rows) and rechecked 250 ms later by `c487_host_step` (object_listed plus fname match before the read), which
  relays K if dead then ("checked after its hide"). Rows log "card487: host ... dead with no Health change (...): kill
  relayed"; the map end logs relayed/alive/gone/not checked totals.

## b214 result (c244b214a, 1947 s)
- All 8 actors got a host K: Bench _1, _5 and Bin _0 by card098 (Health change); Bench _0, _2, _3, _4 and Bin _1 by
  the new card487 hide check ("dead with no Health change (at its hide): kill relayed", t=644788213, one blast).
  Rubble _2 also took the card487 path. Client applied a world K for each.
- Digest: 68 rounds, 0 with mismatches (b213: 22 rounds with 8 dead mismatches).
- The map-end totals row did not print: the run ends by quit, with no next map arm. Not a defect of the fix.
- c244b214b (2364 s): every bench/bin death relayed (card098 Health rows plus 2 card487 hide rows); 82 digest rounds,
  0 with mismatches. 0 EXCEPTION/FATAL.

## Closed (2026-11-18 07:46)
The lead was disproved: the c98 journal is keyed by actor pointer, and the client K apply uses c95_find_nearest(name, at). The real cause was Blueprint deaths with no Health change (C4 blast, carpet bomb), which sent only the hide row. Fixed in b214 (56b7f47d0bbc4783): c98_relay_kill is now the single K send. c487_hidden runs on each relayed hide and relays K at once when the actor is dead, otherwise rechecks after 250 ms (64 rows). c244b214a: all 8 actors got host K rows (Bench _0, _2, _3, _4 and Bin _1 through card487), and the client applied a world K for each. Digest: 68 rounds with 0 mismatches (b213: 22 rounds with 8 mismatches). c244b214b: 82 rounds, 0 mismatches.
Not done: none.
Reopen when: a digest mismatch on a destructible that is dead on the host and alive on the client, or a card487 recheck queue overflow row.
