# Card 209: Player rows name any player's pawn, so a third player sees them

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: queued, static first. Split from card 208 (2026-11-12).

## Why

The mod's player rows on a client address only two pawns: the client's own (a self flag) and "the host's pawn".
The second is found by `c43_client_host_pawn`, which returns the first player pawn of sprint side "other". With
three or four players that is the host's pawn or another client's copy, whichever comes first in the level's
actor list. Card 208's BugForm row skips and counts rows for another client's copy; card 043's Down and revive
rows have the same limit. The sync rule says every player sees the same state for every player.

## Known (junior trace in card 208, "Fixes after c208-r1")

- No existing op addresses a third player's pawn on a client.
- The existing way to name a replicated actor is its NetGUID: `c41_guid(actor)` on the host and
  `c41_resolve_guid(pc, guid)` on the client. Cards 042, 132 and 136 use it for NPCs and corpses, none for
  player pawns.

## Do

1. Static: list every channel op whose client side resolves a player pawn by the self flag or by
   `c43_client_host_pawn` (table: op, sender, how the pawn is found, what a third player sees today).
2. Propose one addressing form for all of them (the pawn's NetGUID in place of the self flag), with the case
   where the GUID does not resolve yet on the receiver (late join, pawn not yet replicated). Senior reviews the
   table and the proposal before code.
3. Proof needs a three-instance run. `tools/run_pair.sh` starts two; state what a third instance needs before
   any run is planned.

## Done when

The table is in the card, every listed op carries the new address, and a three-player run shows a form switch
and a Down of client A on client B's screen (log line and screenshot on B).

## Step 1 table (senior, static, 2026-11-13; tree at build 4)

`c43_client_host_pawn` (`hooks_ubw.cpp:1239`) returns the first player pawn of sprint side "other" in level
actor order. With three players it is the host's pawn or the other client's copy.

| # | Site | Op or use | How the pawn is found | Third player today |
|---|---|---|---|---|
| 1 | `weapons.cpp:1782` `c208_receive` | BugForm row | `kC208SelfFlag`, else host pawn | another client's form change is applied to whichever "other" pawn comes first, or skipped |
| 2 | `death.cpp:1363` (Down row receive) | Down, Out, revive state | entry id `kC43ClientSelf` or `kC43ClientHost` | a third player's Down has no entry: not shown |
| 3 | `death.cpp:1274` `c43_revivable_near` | revive prompt | host entry plus host pawn | cannot revive the other client |
| 4 | `objectives.cpp:2034` Warp receive | Warp to slot | slot 0 only (WARN for other slots) | cannot warp to the other client |
| 5 | `objectives.cpp:2083` `c46_resolve` | follow-up rows | self id, else host pawn | a row about the other client moves the host pawn |
| 6 | `objectives.cpp:2590` presence count | card 227 presence | roster plus one host pawn slot | the other client's copy is not counted |
| 7 | `player_marker.cpp:78` `c110_others` | name tag | fills `out[1]` only | one tag, on the wrong pawn at times |
| 8 | `weapons.cpp:1755` `c208_step` | BugForm tracking | own pawn and host pawn | the other client's copy is not tracked |
| 9 | `hooks_ubw.cpp:1065` side to entry | Down entry lookup | side "other" maps to the host entry | every other pawn reads the host's state |
| 10 | `hooks_ubw.cpp:1224` `c44_other_pawn`, `death.cpp:2001`, `objectives.cpp:2294` | test drivers only | host pawn | not player facing; leave |

Rows 1 to 9 are the work. Row 10 is test code.

## Addressing form (step 2 proposal; senior approves this, junior codes it)

- Every row that names a player pawn carries the pawn's NetGUID (`c41_guid(pawn)` on the host) in the place of
  the self flag or the fixed id. The client resolves with `c41_resolve_guid(pc, guid)`; "own" is
  `resolved == c23_own_pawn(pc)`. No self flag remains on the wire.
- Client state tables keyed by `kC43ClientSelf` / `kC43ClientHost` (`C43Entry::pawn_id`, `hooks_internal.h:2833`)
  are keyed by the NetGUID; `kC43Max` entries already fit four players. Check the entry array size before coding.
- Unresolved GUID (late join, pawn not replicated yet): hold the row in a fixed table of `kC43Max` rows per op
  (last value wins per GUID) and apply from the client tick when the GUID resolves; drop it at map change. Log
  one line per held and per applied row. This is the pattern the corpse rows use (cards 042, 132); reuse its
  helper if one fits, do not write a second queue type.
- Replace `c43_client_host_pawn` at rows 3, 6, 7, 8 by one function that fills all "other" player pawns
  (`void c43_client_other_pawns(const void* world, void* out[kC43Max])`, one level scan); keep
  `c43_client_host_pawn` only for row 10.

## Three-instance run (step 3 facts)

`tools/run_pair.sh` has two lanes (`LANE=1` port 7777 window y 40, `LANE=2` port 7778 y 420) and starts one host
and one client per lane. A third instance needs: a second client launch in the same lane (own log name
`<tag>-client2.log`, own window position, `UBWCOOP_TRAVEL` to the same port), its own Proton prefix copy or a
check that two clients can share one prefix, and a third role in `tools/drive.py` (roles today: `host`,
`client`). Memory: three games in the `MemoryMax=12G` scope; measure one pair's peak first. This is harness work
of its own; card it separately before any run and do not start it before the playtest.

Priority: after the playtest (two-player play is not affected).

## Senior review of the build 5 diff (2026-11-13, static): accepted, run proof owed

Attacked, no defect found:

- `player_marker.cpp:77` reads `found[i]` for every `i < kC43Max`: `c43_client_other_pawns`
  (`hooks_ubw.cpp:1246`) nulls all of `out[]` first, so no uninitialised slot reaches `c31_weak_set`.
- Scan bounds: `kMaxLevels`, `kMaxLevelActors`, `n < kC43Max`; null world, class and level arrays are handled.
- `c41_guid_args` refuses 0 and anything at or over 2^28, so both values stay under `kChanArgMax` and
  `c43_client_id` fits int32; a pawn with no GUID yet gives -1 and `c43_find_pawn` does not look it up.
- Held table (`death.cpp:1454`): `kC209Rows` = `kC43Max` x 2 ops, full table logs a WARN and drops; `c209_step`
  swap-removes while walking down, the apply functions do not add rows; `c209_arm_map` clears at map load
  (`hooks_ubw.cpp:1297`) and logs rows left unresolved.
- Two-player case: Down, Warp and BugForm now always carry a GUID; a GUID the host cannot send is logged and not
  sent (no silent fall back to "the other pawn").

Open, for the run: NetGUID equality for player pawns between host and each client is assumed from the engine
(server-assigned). The proof run must show, per client, a `card209 ... applied: resolved to <pawn>` or a direct
apply naming the right pawn for each of Down, Warp, BugForm, with a third instance. Needs the 3-instance harness
card first. One small log fault: a held Warp applied later prints the current `g_c46.warp_count`, not the held
row's number (`objectives.cpp:2050`); pass the held value through when the file is next touched.

## Three-instance harness (junior, 2026-11-13): senior review accepted

`tools/run_pair.sh CLIENT2=1` (default off; aborts with SOLO or MENU), role `client2` in `tools/drive.py` and
`tools/logsum.py`, scenario `tools/scenarios/c209-three.txt` (111 steps, FED_C2M1 then travel to the bug map for
BugForm). Checked: with CLIENT2 unset every path is as before (c208-r3 91 steps, c127-c4m1 356 steps parse the
same); `mod_log client` cannot pick a client2 log (the name pattern needs `client-`); shutdown is by
`wineserver -k` and the lane pattern, so the third game ends with the others.

Open before the run:

1. Test verbs cannot name the third player: `down host|client` (`cmdfile.cpp:334`, `testrun.cpp:1427`) covers
   roster slots 0 and 1, and `warp client` uses the first server copy (`c29_server_copy`, `hooks_ubw.cpp:919`).
   Add `down client2` (roster slot 2) and `warp client2` (second server copy in PC order). Coded after the build
   5 proof runs so those runs keep `faaa085d7714170b`.
2. Memory: pairs peaked 5.5 to 8.8 GB today (journal scope peaks), so three games need up to about 13 GB. The
   12G scope is a user rule (oomd, memory `oom-scope`); a 16G scope for this one run needs the user's word.
3. Old scenarios that match the pre-209 card043 client rows ("own pawn Up -> Downed", "host pawn") do not match
   build 5 rows (they now carry guid and host slot). Sweep `tools/scenarios/` for those patterns before any of
   them is reused on build 5.

Test verbs `down client2` and `warp client2` reviewed in source, accepted (`cmdfile.cpp:202`, `:336`, `testrun.cpp:1258`,
`:1431`, `c29_server_copy_nth` at `hooks_ubw.cpp:923`). Attacked: slot bound (`r.players > slot`), nth walk bounded by
`kC17Slots`, `c29_server_copy` callers unchanged (n 0). Unproven until the three-instance run: roster slot 2 and the
second server copy in PlayerController order are the same player; the run's `cmd: down: slot 2` and `server copy 2`
rows must name the same pawn.

## c209b253a void (2026-11-18 21:03)

Void at line 19 after 243 s: the scenario opening predated card 320 (C2M1 starts players in the dropship, pawn=other),
so no client confirmed and card 344 sent the host to the menu. 0 EXCEPTION/FATAL on all 3 logs. Scenario fixed to the
c469c opening shape (lints 125 steps, 0 bad). The user's one 16G run is used; a rerun needs a new approval.

## Closed and split (2026-11-19 06:41, senior, c209b257a, b257 `6b5faf2a31aafe19`)

c209b257a failed at line 110, a scenario stand wait: the downed host warped to z 9100 landed 369 uu off. Before that it
proved two of the three ops with three instances (FATAL 0, no dumps):

- Down: client (guid 0x6, slot 1) and client2 (guid 0x76, slot 2) each log the other's Down and Up by GUID with the
  matching host slot. client 730622063 "other pawn guid 0x76 (host slot 2) Up -> Downed"; client2 730613222 "other
  pawn guid 0x6 (host slot 1) Up -> Downed". Both also log the host's Down (guid 0xa2, slot 0).
- Warp: client 730631091 "Warp 2 to guid 0x76"; client2 730631392 "Warp 2 to guid 0x6". Gap 162 uu (limit 200) on
  both sides. Host cmd 5 "server copy 2" moved slot 2's pawn.

Not done: BugForm with a third player, which needs the travel to the bug map. That moves to card 500. The held-Warp
log fault (objectives.cpp:2050, current warp_count printed) is still open; fix it when the file is next touched.
Reopen when: a client logs a Down or Warp row naming the wrong pawn or slot with three or more players.
