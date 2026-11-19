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
