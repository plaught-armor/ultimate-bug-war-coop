# Card 393: test cinematic skip repeats on a level-end cutscene during travel

Status: accepted. Fix in b80 `2e0c99e96d390d63` (lane 2). Test path only (`config::test_skip()`). Run `c392b80`: 0 `try 2`/`try 3` lines on a level-end cutscene, 5 travels to 3D_Frontend for 5 level ends, scenario passed (drive rc 0).

## Evidence
Run `c391b79` failed at scenario line 152. Host log: `card130: host cinematic skipped WB_Cutscenes_C 00000000C1DFA4C0`
try 1 at 428529686 started the travel to 3D_Frontend (428529696); try 2 on the same widget at 428530689 fell inside the
transition world (`Untitled` loaded 428530541, 3D_Frontend 428530774) and started a second travel at 428530775. The
doubled world rows met the scenario's `count=3` waits early. In `c388b77` try 2 fell 82 ms after 3D_Frontend loaded
and did nothing. It is a timing race, not an effect of card 391.

## Fix
`cine_call` (`mod/src/ubw/cine_skip.cpp`): after a successful skip of a top widget with `IsLevelEndCutscene` set, no
repeat call on that widget (`g_cine.travel`).

## Predictions (run c392b80)
- No `card130 ... (try 2` or `try 3` line with `IsLevelEndCutscene 1`.
- Exactly one `card346: host ... seamless travel to` 3D_Frontend per level end; scenario passes.

## Open
A player who presses skip twice about 1 s apart could reach the same double travel in the play build; in `c391b79`
the second travel completed without harm. Not fixed here.
