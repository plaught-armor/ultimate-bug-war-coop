# Card 374: a client that joins while the host is Dead shows the death screen over an Up pawn

Found 2026-11-15 by run `c375a59` (b59 `c0c1e733ab2bb380`, scenario `c375-rejoin-host-dead`). Low. Fix written by the senior 2026-11-15, waits for b60. Route: the client dies and leaves by Exit, the host dies alone, the client rejoins before the host
picks a death-screen button.

## What happens (rows in `logs/c375a59-{host,client}.log`)

- Host: `card307: ... kept PlayerStart: nobody Up`; its table holds only slot 0 Dead; no card043 row for the new
  slot 1 copy, which stays Up on the host.
- Client: the Down join replay gives `other pawn guid 0x8e (host slot 0) Up -> Dead`; `c43_receive_down` sets
  `death_pending` (card 044 all down) and the death screen goes up about 5.3 s later with 3 buttons disabled and
  Exit kept. The client's own pawn has no entry and stays Up behind the screen (shot: death screen over a Health 100
  HUD).
- The host stays on its death screen, no auto-restart. Host LoadLastSave brings both back (card 371 path, 0
  stranded). A second client Exit and rejoin repeat the same rows.

## Verdict

The player-facing outcome is right: the mission is failed and the client waits for the host's choice. The defect is
the state behind it: the host never puts the late copy Down or Dead while `all_down` is set, so the two sides
disagree about that pawn until the restart, and enemies can still target the copy.

## Fix sketch (not done)

Host, at a player join while `g_c43.all_down` is set: add a Dead entry for the new pawn and broadcast it, so the
client's own pawn follows the same card 044 path as a pawn that was present at the all-down. One owner
(`c43_host_step` or the join replay caller); no client change.

## Side finding, no card

`WARN card322: client map end: radio <name> own start skipped x1, no relay of it on this map` appears on this route
(FED_C1M1_SHOOTINTRO01_C / 02_C) because the host played the radio before the join. The class is older (FED_C1M1_
LANDED01/02 in earlier client logs). It is a late-join tally artifact, not a missed relay.

## Fix written (senior, 2026-11-15, for b60)

`death.cpp` `c374_late_copies`, called from `c43_host_step`: while `all_down` is set and the host's own pawn still
holds its Dead entry, a roster pawn in slots 1 and up with no entry gets `c43_enter_down(..., Out)` and the row
`card374: host t=<ms> <pawn> (slot <n>) joined after the all-down: Out`. The Dead-entry guard keeps a pawn that the
restart or load spawns before the per-map reset out of it. The client needs no change: the join replay
(`c43_join_replay`) carries the new entry.

Predictions for `c375-rejoin-host-dead` on b60: one card374 row per join; host `card043 ... -> Out (HandleDeathEvent
skipped)` for the copy; client `own pawn ... -> Out` and the death screen as before; the host stays Dead, no
auto-restart, no second all-down row; LoadLastSave brings both back Up with walk speed and Health restored (client
Health 100 after the rejoin); FATAL/ERROR 0; no new WARN class.

## Result (2026-11-15, b60 `a48aa4475e889bf7`, run `c375b60`)

Every prediction held. Host: `card374 ... (slot 1) joined after the all-down: Out` at t=399794325 and t=399807445, each followed by `card043 ... (server-copy, slot 1) -> Out`. Client: `own pawn ... Up -> Out` per join, death screen as before. No second all-down row, no auto-restart. LoadLastSave: `restart: slot 0 Dead -> Up`, `slot 1 Out -> Up`; client reconnected on try 1. FATAL/ERROR 0 on both sides. Accepted.
