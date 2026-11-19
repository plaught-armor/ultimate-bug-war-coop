# 277 FED_C2M1: the host's level script adds `LOS_O02_SO05` and its two children twice

Found in run B (`logs/c241-c2m1-b1-host.log`), 2026-11-13. Not a play blocker: the client ignores the second
set (`the id exists already`).

## Evidence

Host `AddCountSubObjective_SubObjectives(LOS_O02, LOS_O02_SO05)` at 233369551 (objectives now 7) and again at
233430164 (objectives now 21), each followed by `AddSubObjective` for `_SO05a` and `_SO05b`; same caller chain
`0x50818F2 < 0x154FAE8 < 0x1545188`. Between them the host completed `LOS_O02_SO05` (233404755). The second set
comes in one frame with `CompleteSubObjective(LOS_O02, LOS_O02_SO04)`.

## Step 1 (static, no run)

`kis.py` on `FED_C2M1_Scripting`: name the two script entries that reach the Add block, and what fires each
(which presence or pickup event). Decide: does single player also run it twice (game behaviour, close), or does
the second entry fire because a second player's pawn or copy trips an event that one player trips once (mod
fault, class of cards 227 and 254). Check run 8 and c257 host logs for the same double add.

## Step 2

Only if it is a mod fault: class fix at the event source, not at the objective call. Budget: 0 extra runs
(proof rides in the next FED_C2M1 run: one Add row per id on the host).

## Step 1 answer (`kis.py` on `FED_C2M1_Scripting`, 2026-11-13): game behaviour, closed

The Add block (ubergraph 10907, function `Setup_Objective02_Subobjective_05_PlasmaBugs`) has two callers by
design: offset 6377 in the frame of `BP_PlayerPresence_C_1`'s trigger event (entry 6323), and offset 10855 in
the `BP_AmmoPickup_NukesLarge_C_1` pickup event, just before `CompleteSubObjective(LOS_O02, LOS_O02_SO04)`.
Neither depends on the number of players: one player who enters presence `_1` and later takes the large nuke
runs it twice as well. Runs 8 and c257 show the same two host rows. The game's own duplicate guard and
`c49_skip_add` ignore the second set. No mod fault, no code change.
