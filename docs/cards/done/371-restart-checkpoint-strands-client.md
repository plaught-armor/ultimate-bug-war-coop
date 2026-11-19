# Card 371: RESTART CHECKPOINT strands the client when the checkpoint was saved before it joined

Status: defect found by the proof run c371b56 (b56 08265a76bc24632c, lane 2). Fix in source (see the last section), not built.

## Run c371b56

Scenario `tools/scenarios/c371-restart-checkpoint.txt` (predictions in its header). FED_C1M1, FAST=1, GOD=0.
100 s, host rc 0, client rc 0, FATAL/ERROR 0 both sides, no crash dumps. Drive rc 1: line 52 (host Reload row) timed
out after 30 s.

Passed: both players down by code (client first, then host); host `(host-own, slot 0) -> Dead (all down: nobody
Up)`; host death screen with 5 widgets; client death screen with 3 buttons disabled; `host ui click
BndEvt__WB_PlayerDeathScreen_LoadLastSave` ran the load through the real click path (card 121 row "save engine load
runs (Load, context WB_PlayerDeathScreen_C, load pawns 1, destroy actors 1 ...)"); client card290 travel row,
card294 hold, card294 "second CompleteDeath skipped (death screen up)".

Failed: the client never gets back into the game.

```
host   395968630 card098: host destroy BP_PlayerCharacterMI_C ... at (-1769, 1820, 296) ...   (host-own)
host   395968640 card098: host destroy BP_PlayerCharacterMI_C ... at (6654, 11459, 97) ...    (server-copy)
host   395968646 PossessedBy(... RemoteRole 1 ...)                                             (host pawn only)
host   395970954 WARN card143: host t=395970954 no Reload to BP_PlayerController_C: no player pawn (null)
host   395971001 card045: host t=395971001 respawn in place (new pawn BP_PlayerCharacterMI_C, no map load): per-map reset
host   395971001 card043: host t=395971001 restart: slot 1 Out -> Up (pawn replaced, saved walk 700)
client 395971039 possession OnRep_Pawn pawn null (tick 891)
client 395986921 WARN card132: client round 1: actors 1494 host, 1496 client, mismatches 3 (classes STAISpawner:1 Player:2)
```

No later possession of the client's PC on either side up to the run end (60 s after the load). Screenshots at line
52: the host stands Up at the checkpoint (Health 100, Armour 100); the client still shows the death screen.

## Cause

The load destroys every player pawn (`destroy actors 1`) and spawns pawns only for the player rows in the save. The
only save on the map was FED_C1M1's first checkpoint, taken at host t=395957780, 1.8 s before the client's pawn was
possessed (t=395959599). That save has no row for the client, so no copy pawn is spawned. c143_after_load then
finds the copy PC without a pawn, c143_send_reload logs the WARN and returns, so the client gets no Reload, never
rejoins, and keeps a null pawn under the held death screen. The host's card043 restart row marks slot 1 Up for a
pawn that does not exist.

c143b55d did not hit this because its load used a quicksave taken after the client joined (t=394911502).

The same gap opens for any checkpoint saved while a client is not yet in the world (late join, every map's first
checkpoint on a fast host), and for every load route through detour_c137_load_slot (LoadLastSave, LoadLatestSave,
quickload).

## Fix direction (for the senior)

A server-copy PC with no pawn after LoadFromSlot needs a pawn and the rejoin. Two ways:
- the host spawns the copy pawn itself (the game mode's restart-player path) at the checkpoint before
  c143_after_load, then the normal Reload follows; or
- c143_send_reload sends a Reload with no saved row, so the client rejoins and gets the login pawn, with the
  Restore skipped.
A save that lacks a connected client's row could also be retaken once the client's pawn exists (card 146 anchor
save), which closes the common case at the source.

## Fix chosen (senior, 2026-11-15, in source, not built)

The second way: `c143_send_reload` (`mod/src/ubw/restore_c143.cpp`) sends the Reload with the text `none` when the
copy PC has no player pawn after the load (row `card371: host ... has no player pawn after the load ...: Reload with
no saved state`). The client's `c143_receive_reload` starts the card 045 rejoin as for any Reload and leaves
`g_c143.held` off, so no Restore is sent: the client comes back on a login pawn (card 046 spawn spot, starting
inventory, full Health). It reuses the proven rejoin and adds no spawn path on the host. Retaking the save once the
client's pawn exists is not done: it changes the game's save timing; open it as its own card only if the playtest
shows the starting inventory is a problem.

Proof wanted (one run of `c371-restart-checkpoint` on the next build): host card371 row; client `Reload received
(4 chars): none; rejoining`, reconnect under 60 s, 0 `Restore sent`; host 0 `Restore received`; the client's new
pawn possessed and Up near the checkpoint; a digest round after the rejoin with 0 mismatches; shots on both sides
show both players in the world; 0 card344/card355 rows; FATAL/ERROR 0; no new WARN rows against c370b56.

## Reach on the play path (senior, from logs)

On the lobby route the client's copy pawn exists before the first checkpoint: `c346-persistent-host.log` copy
possessed at 336434492, first checkpoint save at 336463025 (28.5 s later); `c350-c345-host.log` 337978970 against
338005310 (26.3 s). The save then has the client's row and the old path works. The gap in `c371b56` comes from the
test join route (FAST=1, client pawn 1.8 s after the save). On the play path it needs a client that joins or
rejoins after the latest save (late join, a reconnect by the card 045 follow) and a team wipe before the next
checkpoint. Lower reach than first read, same result when hit.

## Run c371b57 (b57 78025485639b5da3, lane 2)

68 s, host rc 0, client rc 0, drive rc 0 ("scenario passed"), FATAL/ERROR 0 both sides, no crash dumps.

```
host   396318777 card371: host t=396318777 BP_PlayerController_C has no player pawn after the load (null): Reload with no saved state
host   396318777 card143: host t=396318777 Reload to BP_PlayerController_C (copy null): sent now, ahead of 206 queued frames: none
host   396318828 card045: host t=396318828 respawn in place (new pawn BP_PlayerCharacterMI_C, no map load): per-map reset
client 396318798 card143: client t=396318798 Reload received (4 chars): none; rejoining "127.0.0.1:7778"
client 396321526 card045: client t=396321526 reconnected (1): try 1, 2728 ms after the drop
host   396322249 card017: host ownership slot 1 pawn ... BP_PlayerCharacterMI_C ... Controller ... BP_PlayerController_C
client 396322305 card294: client t=396322305 restart done in 4958 ms: death screens built 1, respawns in the old world 0 (held 0, new map)
```

Proof list: host card371 row yes; client Reload "none" and reconnect 2.7 s after the drop yes; 0 card143 `Restore
sent` / `Restore received` rows (the only matches are card132 ledger "Restore sent: sent 0" rows); both pawns
possessed; host standing 0 uu from (-1769, 1820, 296), client standing 150 uu from it; shots: the host's shows the
client's pawn beside it, the client's shows the world with Health 100 and Armour 100; 0 card344/card355 rows.

Missed: the digest round after the rejoin has 1 mismatch, `STAISpawner STAISpawner_5 location (one side attached?)
host (69000,63250,-4250) client (0,0,800)`. It is also in c371b56 after the load, and 0 in c370b56 and c143b55d,
so it belongs to the death-screen LoadLastSave path, not the fix. New WARN rows against c370b56 otherwise: host
card130 "grenade explosion (host's own) ... component gone (destroyed before the check)" (the load destroys live
grenades; also in c371b56).
