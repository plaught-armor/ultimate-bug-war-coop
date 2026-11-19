# 259 Test kill helper skips dead actors

Found by card 241 run 9 (`logs/c241-c2m1-r9-*`). Test tooling only; no play-path code changes. Carries card
241's last run.

## Evidence

- `kill actor <class>` (`c98_kill`, `mod/src/ubw/testrun.cpp:870`) takes the nearest actor of the class from
  `c64_find_actor(..., live = true)` (`mod/src/ubw/hooks_ubw.cpp:1430`), which skips an actor only when
  `c23_set_health == 0.0f`.
- Run 9: guid 0x9ce (`BP_AI_WarriorBug_C` at (195981, 998, 7660)) died to the first kill (death ragdoll at
  192070285, the same caller exe+0x37C53CB as all 64 Warrior death ragdolls of the run). The next 69 kill
  commands up to 192300000 hit a Warrior at the host's feet again; the game re-ran
  `SetAllBodiesSimulatePhysics(1)` on 0x9ce 1037 times until the log end. So the corpse's health set did not
  read 0 and the helper kept choosing it. The live bugs of EM C_70 were never killed, C_71 and the boss EM
  C_67 never started, `LOS_O04_SO02` stayed open.
- Verdict: test-helper fault. Real players shoot live bugs. The run below confirms it.

## Change

1. `c64_find_actor` with `live`: skip an actor of the class when its health component's `kHealthIsDead` is
   set or its health set reads <= 0 (`c25_dead` gives both). An actor without a health component stays a
   candidate (buildings, bug holes).
2. `c98_kill`: one log line per kill with the target's guid (when it has one), health and dead flag before
   and after `InstaKillTarget`.

Files: `mod/src/ubw/hooks_ubw.cpp`, `mod/src/ubw/testrun.cpp` (a header only for a declaration). Nothing
else. No scenario verb changes.

## Proof (1 build, 1 run; this is also card 241's run 10)

Scenario `tools/scenarios/c241-c2m1.txt` as in run 9. Written predictions:

- No kill line shows `dead 1` or health <= 0 before the call; no guid is killed twice.
- EM C_70 ends, C_71 and C_67 trigger, `LOS_O04_SO02` and `LOS_O04` complete.
- Card 241's tail: `LOS_O05_SO05`, `SO06`, `LOS_O05`, `WB_TrooperLevelEnd` on both sides, both in FED_C3M1.

If a Warrior with `dead 0` and health > 0 survives `InstaKillTarget`, stop and report: that would be a game
or mod fault on the play path and gets its own card.

The test DLL differs from the play build `3d3f70b0bb6ba6de` only in these two test functions; the play build
is not repackaged for this card.

## Result

**Change.**
- `hooks_ubw.cpp` `c64_find_actor`: with `live`, an actor of the class is skipped when `c25_dead` returns 1 or returns 0 with health <= 0. -1 (no health component) stays a candidate. The function grew by 2 lines and stays under 60. Its header comment was updated.
- `testrun.cpp` `c98_kill`: the kill now logs `kill: InstaKillTarget(<class> <name>) guid 0x.. health H dead D` before the call, and `kill: after InstaKillTarget health H dead D` after it (+6 lines).
- No header or scenario change: `c25_dead` and `c41_guid` were already declared in `hooks_internal.h`, and no scenario line matches the old kill text.

**Build.** One build, clean. The DLL is `61cf21ee94ecdd3b`.

**Run.** One run, `c241-c2m1-r10` (cap 4800 s; the scenario ended it after 1098 s). Both sides exited with rc 0, logged no FATAL/ERROR, and left no crash dumps.

**Predictions.**

1. **Confirmed.** All 108 kill lines read `dead 0` with health > 0 before the call (81 Warrior, 12 Aphid, 11 Chariot, 4 Centurion). All 108 after-lines read `health 0.0 dead 1`. No guid was killed twice, and no kill had guid 0.
2. **Confirmed.** The O04 chain ran:
   - SO02 add at 193792514; C_70 Warriors_Medium activate at 193814696
   - C_71 Warriors_Large + Warriors_ExtraLarge activate at 193839376
   - C_67 Centurion_Boss activate at 193888716
   - Centurion kill (cmd 173, guid 0xa38) at 193895196
   - `LOS_O04_SO02` and `LOS_O04` complete at 193895197
3. **Partly confirmed.**
   - Confirmed on the host: `LOS_O05_SO05` at 194490504; `LOS_O05_SO06` and `LOS_O05` at 194528438; `WB_TrooperLevelEnd` at 194561958.
   - Confirmed on both sides: FED_C3M1 reached (host 194659693, client 194664486).
   - **Not confirmed: the client never logged `WB_TrooperLevelEnd`** (scenario line 2010 timed out). The r8 and c257 client logs have no `WB_TrooperLevelEnd` line either.

**Card 241 count.** 23 of 23 objectives, with the same set on the client. `LOS_O02_SO04` shows as card049 `CompleteSubObjective` on both sides; the other 22 show as card097 rows.
