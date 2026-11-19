# 362: client InstaKill skip with no Down frame from the host

Card 361 makes the client skip every InstaKillTarget of a player-class pawn and wait for the host's Down frame.
If the host never runs the same kill (a blueprint `execInstaKillTarget` in an event only the client replays, or an
untraced caller), the pawn stays alive on both sides and no row says so.

## Change

Client: after each card361 skip of a player pawn, remember (pawn, caller, time). If no Down frame for that pawn
arrives within 3 s, log once `WARN card362: client skip of <pawn> by <caller> had no Down frame in 3 s`. A Down
frame for the pawn clears the entry. Table of 4 entries, cleared at map end. No behaviour change.

## Proof

Rides the next lane 2 build. The b46 out-of-bounds step must give 2 skips and 0 `WARN card362`. No extra run.

## b47 prediction (build faef8a0da0c13495, scenario tools/scenarios/b47-c362-c355-c350.txt)

Implementation (mod/src/ubw/insta_c361.cpp): `c361_client_player` calls `c362_note_skip` with the pawn id
(`c43_client_id`), the caller and the time. A Down frame (`c43_receive_down`, death.cpp, non-Up state) calls
`c362_down_frame`, which removes skips with that id and keeps the frame in a ring of 4, so a frame that came before
the skip still clears it (window 3 s). `c362_step` (cine_skip_step, client only) writes the WARN row for a skip older
than 3 s and drops it. `c361_map_end` clears both tables.

Predicted on the client, from the b46 out-of-bounds block (client turn, then host turn):
- 2 rows `card361: client t=N InstaKill of <pawn> (client-own|other) by KillOwningActor (out of bounds expiry): skipped`.
- Down frames for both pawns (client slot 1 Downed/Out, host slot 0 Dead), so 0 rows `WARN card362`.
- 0 rows `WARN card362: client skip table of 4 full`.

## b47 results (faef8a0da0c13495)

- Run 1 (b47) failed at the host-turn wait for the client's `(other)` skip; run 2 (b47r2) passed, drive rc 0, no
  crash, FATAL/ERROR 0.
- Client turn, both runs: the Down frame came 2 ms before the skip, so the ring path cleared it:
  `card043: client t=348390749 own pawn guid 0x18c (host slot 1) Up -> Downed ... (Down received 1)` then
  `card361: client t=348390751 InstaKill of BP_PlayerCharacterMI_C (client-own) by KillOwningActor (out of bounds expiry): skipped`.
- Host turn, both runs: no client skip. The client's own countdown on the host pawn starts about 50 ms after the
  host's (b47: host 348239687, client 348239736). The host's Down frames (Out slot 1, Dead slot 0) reach the client
  first, so the client's countdown never expires and runs no kill. The b46 skip was the other order of the same
  race. The scenario wait is now a `try`.
- 0 rows `WARN card362` in both runs. Only 1 skip per run, not 2; the no-frame WARN path was not triggered (no case
  in the run lacks a frame).

## Senior review (2026-11-14): accepted

b47 `faef8a0da0c13495` in game2, lane 1 still `d20f47aa1b0383a5`. Run b47r2: 1 client skip (client-own, 348390751;
its Down frame came 2 ms earlier and the ring cleared it), 0 `WARN card362`, FATAL 0. The second skip I predicted
did not happen: the client's countdown on the host pawn starts about 50 ms after the host's, so the host's Dead frame
usually arrives first and the client never reaches the kill. The no-frame WARN path is not proven by run.

Found in review, carded as 363: the client row `InstaKill of BP_AI_Trooper_Morita_C by exe+0x5192EFE: runs`.
