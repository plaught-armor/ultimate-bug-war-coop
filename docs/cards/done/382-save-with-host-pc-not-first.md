# Card 382: a save taken before the client's PC swap pairs pawns with the wrong PC on load

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: fix is in the build (string present in b144 `b09caa0e7c1f80da`). Prediction 1 holds 2026-11-16 16:33: runs `c423b144a` and `c423b144b`, 10 saves each, 0 `card382` rows, digest 0 mismatches. Prediction 2 (forced save inside the window) not run. Found by static audit 2026-11-15 (card 375 sibling).

## Defect (predicted from the card 375 trace; not seen in a run)

Saved player pawn rows hold the index of the pawn's PC in the world's PC list (card 375). After a seamless travel
the host's PC is swapped first (appended) and the client's kept PC is swapped only on its ServerNotifyLoadedWorld, so
for that window the list is [client, host]. A checkpoint saved in the window stores the host's row with index 1.
Loaded later (RESTART CHECKPOINT, or CONTINUE on another day) with the list as [host, client] or [host], the host's
saved pawn goes to the client's PC or to nobody. Run `c378b65` shows how close it is: seamless done 404201774, client
PC swap 404205208, first checkpoint save 404206068 (0.9 s margin on one machine; a slower client loses it).

## Fix (`mod/src/ubw/save_c121.cpp`, `detour_c121_perform_save`)

Host: `c375_host_pc_first(world)` before every save (the card 375 fix C helper, proven in `c377b65`); a
`card382:` row when the host was not already at slot 0.

## Predictions

1. Normal runs: 0 `card382` rows (host already first), no behaviour change, digest unchanged.
2. A save forced inside the window (`travel save` right after the host's `seamless map change done`, before the
   client's card307 spawn row), then `travel checkpoint`: one `card382` row; after the load the host PC holds the
   pawn at the host's saved position and the client gets a pawn (card 371 or a saved row), 0 `card380` rows for the
   host's pawn.

## Closed (2026-11-17 13:09)

The defect was predicted from the card 375 trace and has never been seen in a run. The fix is one call of
`c375_host_pc_first(world)` before every save; that helper is proven in `c377b65`, and the call site has run in
every build since b144 (first check: `c423b144a` and `c423b144b`, 10 saves each, 0 `card382` rows, digest 0
mismatches). No host log holds a `card382` row, so the host has been first at every save.

Not done, stated plainly: prediction 2 (a save forced inside the swap window, then a checkpoint load) was never
run, so the reorder branch itself has not executed at a save.

Reopen when: a host log has a `card382` row and the next checkpoint load in that log shows a `card380` row for the
host's pawn, or after a RESTART CHECKPOINT or CONTINUE the host has no pawn or the client's pawn.
