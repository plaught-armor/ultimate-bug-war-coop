# Card 568: UpdateSignificanceManager at 30 Hz on both sides (perf mod family, card 567 target 1)

## Cause

`ASTGameplayGameMode::UpdateSignificanceManager` (exe+0x51408C0) runs every frame on both sides. On the host the game
mode tick tail-jumps to it (through `detour_c132_update_significance`); on the client the mod's
`client_significance_step` calls it with the local PC. Card 567: the client step is the largest mod step, 16.5 ms/s
average, 54 ms/s in combat windows (about 1.1 ms a frame), worst call 24.1 ms. The host cost is inside the game mode
tick and not in card330.

## Fix (b376 `eb49926909111446` = b375 + card 568, build-b5 copy kept, not run yet)

`c568_due` (enemies.cpp) gates both callers on a QueryPerformanceCounter deadline, 33 ms apart. Skipped frames
return without calling the game function (the host also skips adding remote views; they are added again on the next
run). The manager keeps its last significance values between runs, so NPC tick intervals and LODs change at most one
period late. A new world always runs on its first frame. Off switch: `UBWCOOP_C568_OFF=1` (every frame, A/B).
Log: once per world per side, `card568: <side> significance throttle 33 ms: new world, last world N run(s), M skipped`.

## Predictions (any b376 run with combat)

- Both logs: `card568:` rows at each map load; runs about 30 per second of play, skips the rest (at 49 fps, about 40
  percent runs).
- Client card330 rows: `client_significance_step` ms per 30 s window falls to about 60 percent of the b371 numbers in
  combat (about 1000 ms instead of 1629 at the peak window), and drops out of most windows' top 5.
- Host: combat-window frame counts equal or higher than the b371 runs on the same map (1034 to 1280 per 30 s).
- No visible change: NPCs near the client still animate and move at full rate (card 132 significance views still
  added on every run); no LOD pops in the shots.
- 0 new WARN rows.

### Run c558b376a (junior, 2026-11-21, b376 `eb49926909111446`)
Scenario `tools/scenarios/c375-c4m1-mech.txt` (FED_C4M1, the card 558 mech run). Its combat window is about 35 s: six
code-spawned warriors 800 to 1200 uu ahead of the client's mech, with LMB and RMB bursts and shots `568-npcs-1` to
`568-npcs-5`. The predictions above apply. The report gives the card568 rows from both logs, the client's
`client_significance_step` ms per 30 s window, the host frame counts in that window, and whether the NPC shots show
choppy motion or LOD pops.

## Result (senior 2026-11-21, from the card330 rows of later runs): closed, throttle kept

The throttle has been in every build since b376, with card568 rows at each map load on both sides.

| Run | Map, window | Peak client_significance_step ms per 30 s |
|---|---|---|
| b371 (before) | C2M1 combat | 1629 |
| c558b376a (b376) | C4M1 mech fight | 209.3 |
| c593b438a (b438) | C3M1, 16 warriors | 265.0 |
| c593b438b (b438) | C3M1, 16 warriors | 238.0 |
| c556b440a (b440) | C2M1 rack | 137.8 |

- The peak is 84 % below b371, which beats the 40 % prediction. The maps differ, so this is not an exact A/B, but every
  combat window since b376 stays under 270 ms.
- No LOD pop or choppy NPC motion has been reported in the shots or the user's play since b376.
