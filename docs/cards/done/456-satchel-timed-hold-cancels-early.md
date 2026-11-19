# 456: Satchel timed hold cancels early (cause not logged)

Opened 2026-11-16 from runs c418b157a and c418b157b (b157 `1e6244b6739bbcd0`, C3M1, `tools/scenarios/c418-satchel-c3m1.txt`).

## Seen
- c418b157b, host's own hold: press 514277761 (`MS_WPN_SatchelCharge_Prime`), `MS_WPN_SatchelCharge_Cancel` at 514278863, 1.1 s into the 2.5 s hold, no `card418 ... fuse` row, objective Resort_SO3_A never completed (scenario line 79).
- c418b157a, client hold on Flakbug_C_2: press 514095977, release 514099494, no `card133: client ... own hold completed` row, host logged `timed, card 054 hold not started; the client's finished hold (HoldDone) is the event` (scenario line 96).
- Passing holds for comparison: c418b157a host (press 514088119, Cancel sound + fuse row 514090620), c418b121a client (press 486590776, completed 486593279).

## Ruled out
- Repeated `HandleInputTag_Pressed InputTag.Use` + `IA_ExitVehicle` 1.0 s after the press: present in passing holds too.
- Camera inside the building mesh (`aim: hit ... 0 uu`) for the host: same row in all three runs. Still open for the client (116 uu at (1422, -7900) passed, 0 uu at (1482.7, -8206.2) failed).
- Player damage before the host cancel: none (first host armour damage 514285430).

## Leads (correlation only)
- Host fail: `InternalTryActivateAbility(handle 149) = 1, ability GA_ShuntAbilityCharacter_C` at 514278296 and 514278513, inside the hold, each on the ms of a TurretTrooper damage row on a ChariotBug near the host. No shunt row inside the passing host hold. The row does not name the owner pawn.
- Client fail: client lost 74 to the host's blast 2.3 s before its press; two `ClientAdjustPosition` rows inside the hold (514098259 8.5 uu, 514098359 2.5 uu).
- A regression in b122 to b157 is not excluded: no run in between drove a client timed hold.

## Step 1: diagnostic (one build, one run)
1. Hook `IADInteractableInterface::OnTimedInteractionInterrupted(UADInteractorComponent*)`, `OnTimedInteractionStarted` and `OnTimedInteractionReleased` (public symbols exist; offsets through the generator) next to `detour_c133_completed` in `mod/src/ubw/hold.cpp`. Each logs one row on both sides: `card456: <side> t= hold <started|interrupted|released> on <site> by <pawn>, pawn at (x, y, z), speed, caller exe+0x...`. Log only, call the original unchanged.
2. `input_diag.cpp:1294` `jump: ... InternalTryActivateAbility` row: add the ASC owner name.
3. Scenario: client warp to the b121 spot (1422, -7900); `host clear 3000 kill` before each plant.

Predictions: with no bug near, both holds complete and no `interrupted` row appears. If a hold is interrupted, the caller offset names the game check that cancelled it (movement, range, damage, shunt). A client hold with no `started` row means the client never started it: then the fault is before the hold (press routing), a mod path.

## Done when
The cancel cause is named from a `card456` row. If the cause is game behaviour that solo play has too (a shove or movement cancels a hold), close as not a mod bug and fix the scenario. If the client's hold is cancelled by a host correction or never starts, fix in the mod and prove with one run.

## Static trace, 18:40 (read-only disassembly)
- `UADInteractorComponent::TickComponent` (exe+0x4A20230) calls `TraceForInteractable` (exe+0x4A20780) every frame. With the hold flag set (component +0xFC) and a current interactable (+0x100) that differs from the traced one, it calls the current interactable's interface slot 0x68 (the hold-interrupted event), then replaces +0x100 with the traced one.
- So the game cancels a timed hold on the first frame the interactor trace returns anything other than the held site: the aim leaves the site, the pawn is moved, or another body crosses the trace.
- Fits both leads: the host was next to shunting ChariotBugs under turret fire; the client stood inside the building mesh (aim hit 0 uu) and got two position corrections inside the hold.
- `OnTimedInteractionInterrupted` (exe+0x4A20070) is a real 47-byte function; `OnTimedInteractionReleased` (exe+0x4A200B0) is 14 bytes and `OnTimedInteractionStarted` starts at exe+0x4A200C0.
- Diagnostic addition: in the interrupted row, log what `TraceForInteractable` returns at that moment (null or the other interactable's name). That row decides between "game rule, scenario placement" and a mod fault (a host correction moving the client's pawn during its hold would be a mod-side cause worth fixing: card 138/054 hold handling).
- Mod override of the trace ruled out: `detour_trace_interactable` (`mod/src/ubw/interact.cpp:81`, cards 081/130) replaces the trace result only when the own pawn is down (logs `card081 ... own interact trace hidden`) or a downed teammate is in revive range (logs `card130 ... revive prompt shown`). Neither row exists in c418b157b host or c418b157a client. The junior's diagnostic can log the trace result from inside this existing detour: no new hook needed for it.

## Run c418b158a (b158 `d8f61a2f0a8eb107`, log-only diagnostic build, lane 2 only), 18:44
- Hooks placed on `OnTimedInteractionStarted` (exe+0x4A200C0), `Interrupted` (exe+0x4A20070), `Released` (exe+0x4A200B0); the interrupted row carries the last trace result from the card 081 detour; the `jump: ... InternalTryActivateAbility` row carries the ASC owner.
- Host hold with `host clear 3000 kill` before it: completed. `card456: host ... hold started` 514826178, fuse row 514828680, `hold released ... completed 1` 514829396, no `interrupted` row inside the hold. Prediction held for the host.
- Shunt rows are bug-owned (`owner BP_AI_AphidBug_C`): the b157b shunt rows were the ChariotBugs' own abilities, not the host pawn's. The bugs next to the host remain the lead for that cancel (body in the trace line), not a shunt of the player.
- Interrupted rows after the blast on both sides come from TickComponent (caller exe+0x4A202EA) with the game's trace null: the site is gone. Harmless.
- Client hold did not run: scenario line 96 VOID, the look from the fixed point (1422, -7900) got no focus. Cause: `BP_SatchelBuilding_Flakbug_C_2` turns (3 `card408 ... turn` rows), so the site moves and its bounds differ per run (b121a centre 1597 -8012, b157a 1577 -8249, b158a 1703 -8331). A fixed coordinate is wrong for this site; the earlier `warp client <site> 100` put the camera inside the mesh.
- Open question from the rows: interactor +0xFC reads 1 on the host 2.5 s after release and on the client at a site it never held.
- Next: run c418b158b, client warp relative to the site (250 uu, front). Run 2 of 3.
- New lead for the client fail in c418b157a: the building turned during the hold and moved the site out of the trace. To check in that log.

## Cause found from the old logs, 18:46: the flak bug building turned during both failed holds
- c418b157b host fail: press 514277761; `card408: host ... turn BP_SatchelBuilding_Flakbug_C_0 yaw -130.00 DoATurn 1 dir R` at 514278365 (0.6 s into the hold); `MS_WPN_SatchelCharge_Cancel` at 514278863 (0.5 s after the turn began).
- c418b157a client fail: press 514095977; `card408: host ... turn BP_SatchelBuilding_Flakbug_C_2 yaw -60.00 DoATurn 1 dir R` at 514097938 (1.96 s into the 2.5 s hold); no completion.
- c418b157a host pass: no `turn BP_SatchelBuilding_Flakbug_C_0` row at all before or in the hold.
- With the static trace this is the whole chain: the building turns, the site child actor rotates with it, the interactor's line trace no longer hits the site first, TickComponent interrupts the hold. Game rule, the same in solo play: a satchel cannot be planted through a turn of the flak bug. Turns come about 14 to 20 s apart in these logs, the hold takes 2.5 s.
- Not a mod bug, not a b122 to b157 regression. The shunt rows and position corrections were coincidence.
- Scenario fix: each plant step waits for a `card408 ... turn <its building>` row, then for the turn to end, then looks and presses; so the hold sits in the quiet gap. Co-op check left for run c418b158b: the client's own trace follows the turn the host sent (card 408), so the client must see the same gap; an `interrupted` row on the client with no host turn row within 1 s before it would be a sync fault.
- Interactor +0xFC answered (disassembly exe+0x4A1FF00 to 0x4A20B00): it is only read (TickComponent, two compares), never written by Interact, ReleaseInteraction, TickComponent or the trace. It is a component setting (interrupt on focus change), not a "holding" state, so a constant 1 is expected and nothing is stuck. The `card456` row's `holding 1` label is wrong: rename to `flag +0xFC` when the rows are next touched, or drop the rows when the card closes (they are diagnostic).
- Turn timing from c418b158a (`Flakbug_C_2`): `turn ... DoATurn 1` 514833517, `timeout ... DoATurn 0` 514838849 (turn takes 3.7 to 5.3 s), next turn 514847610 (quiet gap about 8.7 s). The turn-end row for the scenario wait is `card408: host t=\d+ timeout <building> ... DoATurn 0`; a 2.5 s hold started right after it fits the gap.

## Run c418b158b (b158 `d8f61a2f0a8eb107`), 18:48: passed, closed as not a mod bug
- Scenario now waits per plant for the building's `card408 ... turn` row and then its `timeout ... DoATurn 0` row, re-places from the site (client `host warp client <site> 250 front`), looks, gates on focus, clears, presses.
- Host: `hold started` 515249294 (1.7 s after the C_0 timeout row), fuse 515251795, `hold released ... completed 1` 515252511.
- Client: focus CAT_0 at 175 uu, `hold started` 515277954, `card133: client ... own hold completed ... HoldDone sent` 515280437 (2.48 s), host fuse 515280443, released completed 1 at 515281470. Client shot `c456-client-focus` shows the "HOLD E PLANT SATCHEL CHARGE" prompt (viewed by the junior).
- Interrupted rows: none inside a hold. One pair before the host press, 498 ms after a C_0 turn row, on both sides (the turn reaches the client's trace too: sync holds). Two pairs after site destruction.
- 0 EXCEPTION both sides, digest 0 mismatches, scenario passed in 85 s.
- Conclusion: a flak bug turn cancels a satchel hold by game rule. Nothing to fix in the mod. The `card456` rows stay as diagnostics (capped); the `holding 1` label means "interactor +0xFC", a constant setting.
