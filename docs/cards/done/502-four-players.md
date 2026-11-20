# 502: Four players (host and three clients)

Status: open (written 2026-11-19, senior). User: "I also wanted 4 player". Batch into the next build after b260.

## Goal

A host and three clients play a Trooper map together, under the same sync rules as two players: world, enemies,
objectives and every visible effect are the same on all four machines, joins and rejoins included. Bug mode is out
of scope (user, 2026-11-19).

## Known state (static, 2026-11-19)

- Card 209 ran three instances (c209b257a): Down, Up and Warp rows named the right pawn and slot for both clients.
  No run has had a fourth player.
- Per-player tables are sized 8 or by kC43Max (kC17Slots): kC30MaxPlayers, kC128Conns, kC132Peers, kC22MaxPawns,
  kC28MaxPawns, kC29Pawns, kC491Pawns, kC227PlayersMax. Bug scaling (card 111) already has a factor for 4 players
  (`kC111Scale = {1.0, 1.6, 2.1, 2.6}`).
- lobby.h kMaxIps = 3 is this machine's LAN address list, not a player limit.

## Sites to fix or check (trace each before the build)

1. mod/src/ubw/dropship.cpp:1178 `kAutoDropshipPlayers = 2`: the host starts the flight once 2 PlayerControllers
   exist. With 3 clients, the flight can start before clients 2 and 3 are in. Trace whether the per-frame boarding
   (dropship.cpp:2123) seats a late pawn during the flight. If it does not, the gate waits for every client the
   lobby knows about (the joined count), with the existing timeout.
   Traced 2026-11-19: c399_step walks every roster slot each kC399StepMs and boards any server copy not yet
   boarded while the host rides, so a late pawn boards during the flight. Keep the gate; the run checks it.
2. mod/src/ubw/objective_c284.cpp:76-79 `pawns[3]`, `placed[3]`, `slot[3]`: objective placement handles three
   pawns only. Size it by kC43Max and add the client3 role.
3. mod/src/ubw/hooks_ubw.cpp:1904 `c44_other_pawn`: on the host it returns the first server copy. List its callers;
   any non-test caller must loop over every copy (as `c43_client_other_pawns` does for card 209).
   Traced 2026-11-19: the callers are enemy_c103.cpp:160 (a log line), death.cpp:2046 (card 044 test aim) and
   objectives.cpp:3232 (card 046 test). No gameplay caller: nothing to fix.
4. Engine join limit: confirm that the host accepts a fourth connection (AGameSession MaxPlayers, or the game's
   override, read at runtime and logged once at listen). If the value is below 4, raise it on the host.
5. One class sweep for "the client" singular state on the host: a static slot that holds one remote PC, pawn or
   connection where every remote one should be held. Card 209 covered player rows only.
   First pass 2026-11-19 (file-scope `void*` globals, single remote fields): no host-side single-remote slot found.
   The hits are call-scoped (g_c361_scoped, g_c74_grenade), own-pawn (g_c170_pawn) or diagnostics (C92State).
   The four-instance run is the real check.

## Finding 1 trace (senior, static, 2026-11-19)

Gap: c323_pawn_culled (digest_c132.cpp:875) leaves a player pawn out of the one shared digest when any remote PC has
no channel for it. A client that still has the channel keeps its own Pl:<guid>:hp/ar/wp entries (on a client
c323_pawn_culled is always false), so those show as own-only, and they also change that client's slot sums, so the
card 329 whole-sum check (c132_sum_compare:2104) fails and every split round goes to the query path.

Fix sites:
1. Host: c323_add_player_pawn (digest_c132.cpp:894) records the culled guid in a round list on C132Host (at most
   kC17Slots, reset in c132_host_start before c132_build).
2. Host: c132_send_begin (digest_c132.cpp:1264) sends a new frame "P<round> <guid> ..." to each PC right after its
   B frame, in the same batch, only when the list is not empty (kChanTextMax 1400, so 3 guids fit easily). Do not
   change the B format: its last field is the free-text why.
3. Client: the frame handler (digest_c132.cpp:2453 chain) parses P only while c.begun and the round matches, and
   stores the guids (B clears them).
4. Client: c132_sum_compare (digest_c132.cpp:2098), before c132_find_dups and c132_sum, removes the own entries
   whose key is Pl:<guid>:hp/ar/wp for a stored guid and counts them (log one line per round with the count).
   This covers both the sums and c132_compare, which runs on the same cl.own.

Not a marker inside the digest entries: the host sum would carry a key no client has.

Proof: in a split round, the client round line shows mismatches 0 and the whole sum equal (no "slot sums asked"),
plus one card323 culled-guid row on the client naming the removed entries.

## Finding 2 trace (senior, static, 2026-11-19)

Cause: the scenario roles reach the host's pawns through c29_server_copy_nth (hooks_ubw.cpp:1229), the nth server copy
in GetPlayerController order. A rejoin gives client 1 a new PC at the end of that list, so "client" then means client2
(testrun.cpp:1868, 1898, 1927, 3028, 3109 all use c.copy). Join order alone does not fix it: the rejoined client is
also the newest join. The host needs to know which instance owns each PC.

Fix: each client instance already knows its role from UBWCOOP_LOG_TAG (ue/config.cpp:87 read_log_tag; run_pair.sh
sets client and client2). In test runs only, the client sends its tag to the host once per map, right after its first
Pong (chan_ops.cpp:142 c26_client_answer). The host stores it on that PC's C50Copy (c50_first_pong, weapons.cpp:1113)
and logs one row "card502: host PC <name> is <tag>". A new c29_server_copy_role(own, tag) replaces the copy index
lookup in testrun.cpp. With no tag stored it falls back to PC order and logs a WARN, so an old client still runs.

Proof: in the rejoin round of a CLIENT2=1 run, the role row names the rejoined PC "client", and the next `client`
step moves the rejoined client's pawn (its own log shows the step), not client2's.

## Finding 3 trace (senior, static, 2026-11-19)

Cause: c491_resupply_apply (score_c475.cpp:773) resolves the pawn with c41_resolve_player. When the pawn is not
resolved yet (a join replay row for a pawn whose channel has not opened), set is false: the level is lost and a WARN row
is logged. Nothing applies it later, so the level watch (score_c475.cpp:882) then reports "not by a U row" when the
replicated or BP value moves.

Fix, reusing the card 209 table (death.cpp:1748-1810):
1. Add C209Op::Resupply (hooks_internal.h:3411) and "Resupply" to kC209OpText. kC209Rows grows with Count.
2. In c491_resupply_apply, when guid != 0, pawn == nullptr and level >= 0: c209_hold(C209Op::Resupply, guid, level).
   The award still pays at once, because the score lives on the subsystem and not on the pawn. The row then counts as
   ok (held), not WARN.
3. In c209_step, the Resupply op calls a new c491_level_set(pawn, level) taken from the set branch (the offset lookup,
   memcpy and c491_watch_set). No own-step effects: a held row is a replay, not a step the player saw.
   c209_arm_map already counts rows left unresolved at map end.

Proof: on a CLIENT2=1 join, a "card209: ... Resupply <level> ... held" row and then "applied: resolved to", 0
"not set" rows and 0 "not by a U row".

## Test scope (user, 2026-11-19)

"We can test 3 players instead of 4 because realistically 4 would work if 3 does." No CLIENT3 harness and no
four-game run. Item 2 (c284 arrays of 3) stays as it is: it is test-only and three roles fit. Item 4 is still
needed, since three games cannot show a join limit of 3: read MaxPlayers at runtime and log it.

## Proof (written before the run)

Three-instance runs (CLIENT2=1) only:
- the host logs MaxPlayers >= 4 once at listen (raised on the host if it was lower);
- the host logs 2 remote PCs, and each client logs 2 other player pawns (card 209 rows) with the right slot;
- the dropship flight boards all three pawns (a late pawn is logged boarded during the flight);
- 0 card132 mismatches on both clients through a C4M2 soak;
- one client rejoin with three players: the rejoined client's digest is clean, and its other-pawn rows name both
  other players.

Reopen when: any desync, wrong-pawn row or missing effect that appears only with four players.

## Proof status (senior, 2026-11-19 10:09, from c502b267a, three instances, b267)

- Met: the host logs "MaxPlayers 16 -> 16 (want >= 4)" at every listen. The engine cap is already 16, so item 4 needs
  no write.
- Met: card132 rounds on the client 23 and on client2 22, mismatches 0 in all; the client rejoined twice (card045
  "reconnected (1)" and "(2)"); rc 0 on all three; 0 FATAL/ERROR.
- Not met: the dropship boarding of three pawns. The run used FAST=1, and the flight exists on FED_C1M1 only. It
  rides the next C1M1 run that carries a bug batch, not a run of its own.
- Not met: "the rejoined client's other-pawn rows name both other players". card209 rows log only on events (Down,
  Resupply), so after the rejoin there were none to read. The c323-seat-down-c4m1 run (host downed after a rejoin)
  covers the host slot; client2's slot needs no separate run (user: three players are enough).
- Side note: after its disconnect, the client logs a "card502: host" row on its own FrontEnd world. This is the
  label of a non-client world, not a second host. Harmless.

## Closed 2026-11-19 (senior, from c502c1b280a, b280, FED_C1M1, FAST=0, three games)

- Met: the dropship boards all three pawns. card399 host board rows: slot 1 at 752650718 and slot 2 at 752656361 into BP_PlayerTransportDropship_VerySlow_C (attached 1). Both were released at 752669536 (detached 1).
- Met: MaxPlayers 16 at listen; the host role rows name "client" and "client2"; the client's rejoin was tracked.
- The run's one Player mismatch (client2 round 12, P:2) is a real bug of its own: a client that leaves while downed stays Downed on the other clients. Card 508.
- The client's early laser door open in the same run is card 507.
