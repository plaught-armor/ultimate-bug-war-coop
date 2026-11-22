# Card 402: every class lookup walks all 28000 level actors (0.7 ms a walk, 7 to 15 walks a second)

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: open, measured, design written, not coded. Class: card 330 (per-frame cost). Not a play blocker.

## Cost (seen, run `c399b87f`, b87, `card330: <side> map FED_C5M1 collect by caller`)

`c23_collect` reads every actor of every level and tests its class. One call costs 0.6 to 0.75 ms on a mission map.

| Side | Caller | Calls | Total |
|---|---|---|---|
| client | mod+0x47761 (card 037 scan, every 100 ms) | 243 | 168.8 ms |
| client | mod+0x13dcee (`c368_actors`, players and turrets, every 250 ms each) | 101 | 75.1 ms |
| client | five other sites | 60 | 21.0 ms |
| host | mod+0x13dcee (`c368_actors`) | 165 | 111.9 ms |
| host | mod+0x14e28 | 60 | 36.1 ms |
| host | mod+0x74dd0 | 52 | 32.2 ms |
| host | nine other sites | 43 | 26.6 ms |

Client: 264.9 ms in 1540 frames, 172 us per frame, the largest mod cost on the client (the walk is 94% of
`c37_step`'s 117 us mean and all 11 of its calls over 1 ms). Host: 206.8 ms in 1635 frames, 126 us per frame, spread
over `c28_step`, `c43_step`, `c125_step` and others, second only to card 401's sweep. Same shape on FED_C4M2
(client 213 ms).

## Design: one class index behind `c23_collect`

Callers stay as they are. `c23_collect` keeps, per (world, class) it has been asked for (table of 48), a list of weak
refs to that class's actors (cap 1024) and, per level, the actor count it has already classified.

1. A call scans only the slots past each level's recorded count (actors spawned since), adds the matches, then fills
   the caller's list from the weak refs, dropping dead ones. A world change, a change in the number of levels, a
   level whose count shrank, or a full list falls back to the whole walk and rebuilds the entry.
2. Assumption: a spawned actor is appended to its `ULevel::Actors` and a destroyed one leaves a null slot. The index
   checks it at run time: every 5 s per class the whole walk runs anyway and its result is compared with the index;
   a difference logs `WARN card402: <side> class index missed N actors of <class>` and the walk's result wins. With 0
   such rows over the soak maps the period can go to 30 s.
3. `c37_scan` and `c368_actors` keep their own weak lists and periods; their walks become tail scans.

Expected: client walks 172 us to about 10 us per frame (`c37_step` under 20 us), host 126 us to about 10 us.

## Predictions

1. `card330 ... collect by caller` on FED_C5M1: client total under 25 ms, host under 25 ms (was 264.9 and 206.8).
2. `card330: client map FED_C5M1 step c37_step` mean under 20 us, 0 calls over 1 ms outside the 5 s check.
3. 0 `WARN card402` rows; the same `card037` adopt and notify rows, `card038` death edges and `card023` counts as a
   b88 run of the same scenario; 0 mismatches; no new WARN class.

Files: `mod/src/ubw/hooks_ubw.cpp` (`c23_collect`), `mod/src/ubw/frame_c330.cpp` (row gains the tail and whole walk
counts). Build with card 401 (same assumption, same run).

## Results

- b91 `e83e833cf39573f8`, run c401b91a: `c23_collect` FED_C5M1 host 0.7 ms, client 1.9 ms in total; client `c37_step`
  mean 8.0 to 27.8 us (was 117 to 215). Prediction 3 failed: 9 to 10 `WARN card402`, the index one actor short.
- b92 `edf08dd41151ba60`, run c401b92a (miss diagnostic): all 10 rows name an actor in a slot 27 to 671 below the
  level's end, level 0, level count equal to the classified count. Host: BP_AI_WarriorBug_C,
  BP_AI_Trooper_Morita_C, BP_AI_TurretTrooper_C, BP_AI_AphidBug_C; client: BP_PlayerCharacterMI_C. The tail scan could
  not see them; until the 5 s check an enemy or player pawn was absent from every `c23_collect` caller.
- b93 `2662d2d696538671` (rework): no slot order assumed and no weak refs. A copy of every `ULevel::Actors` array is
  compared with the live one on each call; a slot holding a new actor is classified once for every class entry; a
  row counts only while its slot still holds its actor. The 5 s whole classification compare stays. New rows:
  `card402: ... took level L slot S of N, which held none|another actor` and
  `card402: ... slots since the last reset: A appended, R refilled, P replaced, C cleared`. The second row settles
  whether slots are refilled (the b92 rows fit that, and also a weak ref reading null for a live actor; the rework
  covers both). Expected cost: about 28000 pointer compares per call. Run c401b93a pending.

### b93 runs (2026-11-15)

- Run c401b93a: void. The host stalled after "galaxycfg: waiting for steam_api64.dll", before any world and any card 402 code.
- Run c401b93b (`2662d2d696538671`): client crash on the first world change, `EXCEPTION_ACCESS_VIOLATION at exe+0x14CBD96`, stack `is_a` from `c402_slot_new`. `c402_slots_reset` kept the entries' class pointers, and a blueprint class of the old world was freed by the travel's GC. Fix: the reset sets `g_c402_used = 0`; `c402_entry` rebuilds an entry on the next ask. Build `cfdd06ae5462361c`.
- Run c401b93c (`cfdd06ae5462361c`), FED_C3M1 to FED_C5M1: 0 `WARN card402` on both sides, 0 mismatches.
  - Slots below a level's last count do take new actors. Per mission map the host saw 1 to 2 refilled and 18 to 84 replaced slots, the client up to 221 replaced. The actors are the travel's own set (PlayerController, PlayerState, PlayerCameraManager, BP_PlayerCharacterMI_C, GameStateBase, AbstractNavData, BP_HUD_Manager_MI_C, DefaultPhysicsVolume, ParticleEventManager). The append-only premise of the first cut was false; the copy compare covers it.
  - The copy also resets inside a map about every 61 s (FED_C5M1: rows at 438606300 and 438667317, 27851 slots appended again each time, no world change). The level's Actors count drops below the copy's count, which `c402_slots_current` treats as a new level set. The period matches the engine's garbage collection interval, which compacts the Actors array. The reset is correct there (every index moves); it costs one whole rebuild per minute.
  - Prediction missed: `c23_collect` under 40 us per call on the host. Measured 76.0 / 126.2 / 128.2 / 127.2 us on C3M1 / C3M2 / C4M1 / C4M2 (b92a: 111.8 / 138.9 / 97.4 / 103.0; b91a: 46.6 / 79.1 / 100.6 / 79.0). Totals stay at 16 to 25 ms per map over about 200 calls. The 5 s whole check and the first classification after each reset are the likely cost; not measured apart.
- Not accepted: run c failed at the FED_C5M1 level end (see the STATE entry of 2026-11-15 21:38). One clean chain run is still owed.

### Accepted 2026-11-15 21:43

Run c401b93d (`cfdd06ae5462361c`), full chain FED_C3M1 to FED_C5M2 in 181 s: passed, 0 `WARN card401`, 0 `WARN card402`, 22 compare rows per side with 0 mismatches, every travel within 15 ms of the second Next. The FED_C5M1 score screen stall of run c did not recur; its cause is not established (STATE entry 2026-11-15 21:43).
