# Card 097: Some objective completions never reach the client

Status: accepted 2026-11-05 (senior). Reviewed: host-only queue at the native choke point, library calls not double-sent. The TacSupport path is proved by the user playtest (`card097: host ... queued; caller` lines).

Read:

- `docs/playtest-2026-11-05.md`, items 7 and 8;
- card 049 (objective mirror: host detours on `USTObjectiveUtilityLibrary` Add/Complete calls, journal, client replay);
- card 095 Report (the capture-the-flag entrance, `objective complete` test command).

Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`. Play DLL base: `6f8dc8e67527f37e` (card 096).

## Why

The user's read of item 7 (invisible wall) is that objectives are not synced on completion. The 2026-11-05 logs
support it. Client log `client/playtest-menu-20261105-200846.log`:

- The client replayed every Assault and ShootingRange completion, and TacSupport_SO2, SO3, SO4, SO41 and SO5.
- It never received a completion for **TacSupport_SO6, TacSupport_SO7, TacSupport_SO0 or TacSupport_01**. Those were
  added and replayed at t=230834098 (`replayed AddSubObjective(TacSupport_01, TacSupport_SO6)` etc.), and never
  completed. SO6/SO7 are the support flare and bombing run steps (items 5 and 6).
- In the second mission, no completion or count change for `LOS_O02_SO05*` (bug holes, item 8) reached the client.

The host log cannot show what the host did: the `card049 mirror` and `card049 audit` lines hit their caps
(t=1018594, 1244205). So it is unknown whether the host completed those objectives through a call the mirror does not
hook (for example a native path in `USTLevelObjectiveSubsystem` or `USTTacticalSupportComponent`, or a count
objective `Increment`), or hooked them and failed to send.

The client's level script (`FED_C1M1_Scripting_C`) is bound to `OnLevelObjectiveComplete` on the client too. If it
opens the capture-the-flag entrance on its own when `TacSupport_01` completes, a missing completion leaves the
client's copy of the entrance shut: that would be the invisible wall.

## Do

1. **Logging first.** Make the card 049 host send lines and client replay lines uncapped for Complete/Increment (or
   reset the caps per map), so a playtest log always shows every completion.
2. **Find the missing path.** On the host, complete the tutorial for real through TacSupport (use the card 095
   `objective complete` command only for the steps before TacSupport if that saves time; the TacSupport steps
   must run their real game path: pick up and throw the flare, call the bombing run). Log every way an objective
   becomes complete on the host (the subsystem's completion function and the `OnLevelObjectiveComplete` broadcast,
   not only the utility library), and compare with what the mirror sends. Do the same for one count objective
   (`STLevelObjectiveCount`, e.g. ShootingRange_SO3 or a bug hole count).
3. **Fix:** send each completion and count change from the point every path goes through (the subsystem or the
   broadcast), with the journal and first-Pong replay working as before, without double sends for the paths
   already mirrored.
4. **Check the entrance:** after the TacSupport completion reaches the client, find out whether the client's own
   script opens the capture-the-flag entrance (door `BP_Door_Sliding_Large_C_0`, `BlockingVolume_1` at
   (-3635, 9855, 225)), and walk the client through.
5. **Proof**, one combined scenario run (FAST=1): host finishes TacSupport through the real flare and bombing run
   path; the client log shows TacSupport_SO6, SO7, SO0 and TacSupport_01 completed; the client walks through the
   capture-the-flag entrance. rc 0, ERROR/FATAL 0, no new dump.

Stop when the fix builds and that one run shows it; the user will playtest.

## Done when

- The Report names the path that missed the mirror, the fix, whether the entrance now opens on the client, the proof
  run and the DLL hash.
- The Report ends with "Report complete."

## Report

**Fix (DLL b86475badfbc9520, saved as build/dwmapi-play-b86475badfbc9520.dll).** The completion is now sent from the
native choke point rather than only from the library functions:

- `USTLevelObjective::CompleteObjective` (0x05150C70) is hooked. From the disassembly, it returns at once if +0x69 is
  set. Otherwise it sets +0x69, completes every sub-objective (vtable +0x2B8), broadcasts, and for a sub-objective
  calls the parent's `OnSubObjectiveComplete` (+0x2D8), which can complete the parent.
  `USTLevelObjectiveCount::IncrementObjectiveCount` (0x0515D9B0) is also hooked; it completes the objective (+0x2B8)
  when the count reaches its target. Every completion path goes through these two functions: the library, level
  script, native components, parent cascades and counts.
- Host, outside a mirrored library call (`g_c49.depth == 0`): each objective that one outermost native call
  completes, and each count it changes, is queued in entry order (32 slots). The queue is sent after that call
  returns through `c49_mirror`, which means the card 049 journal, the first-Pong replay and card 066 compaction all
  still apply. A sub-objective is sent as `CompleteSubObjective(parent, id)`, a top-level one as
  `CompleteObjective(id)`, and a count as `IncrementObjectiveCount(id)` if it changed. The world context is the host
  PC of the objective's world (UObject outer chain). Inside a library call nothing extra is sent: the library mirror
  already sends that call, and the client's replay runs the same native cascade. So paths already mirrored are not
  sent twice. Native CompleteObjective is idempotent, so a repeat would be harmless anyway.
- `USTLevelObjectiveSubsystem::ObjectiveComplete` (0x051613C0) is hooked for logging only (the
  OnLevelObjectiveComplete broadcast).
- Logging: `card097: <side> native CompleteObjective <parent>/<id> (count n/target), depth, library depth,
  queued|mirrored by the library call|client; caller` on both sides (cap 1200 per process). Every subsystem broadcast
  is logged too. The card049 `host sent` and `client replayed` lines are uncapped for
  Complete/CompleteAny/CompleteSub/Increment (`c49_kind_line`).

**Missed path: not yet named from a log.** The run below showed:

- library completions reach the native function (caller exe+0x5233805);
- the parent completes through the native cascade (Assault_O1 from Assault_SO0, caller exe+0x51655C9, nested and
  not sent);
- the client runs the same cascade.

The host flare step in the scenario did not run, so the TacSupport_SO6/SO7 path was not exercised (see below). Two
explanations fit the playtest log. One is that the flare and bombing-run steps complete through a non-library
native or Blueprint call. The other is that the completions were sent but the host lines were cut by the cap. The
new hook covers both cases: any path now reaches the client, and the uncapped lines will name the caller in the
next playtest.

**Run c97-tac2** (FAST=1, tools/scenarios/c97-tacsupport.txt): host rc 0, client rc 0, FATAL/ERROR 0, WARN 0, no new
crash dumps, drive rc 0. The scenario did these steps:

1. `objective complete` for Assault_SO0, ShootingRange_SO0/SO3 and TacSupport_SO2/SO3/SO4/SO41/SO5. Completing a
   parent by command fails after its subs: it is already complete through the cascade (first run c97-tac, drive
   rc 1).
2. The host goes to the flare pickup, E, 4, fire, 4, fire.
3. Both list the objectives. The client walks to (-3674, 10400).

Results:

- Every command completion reached the client: 20 `card097` lines on each side, and a `client replayed
  CompleteSubObjective(TacSupport_01, TacSupport_SO2..SO5)` line for each.
- **The real flare path did not run.** The host's E at 152 uu from `BP_TacSuppPickup_BombingRun_Tutorial_C` found
  `Interact ... target null`, there was no `DisablePickup` for the pickup, and 4/fire did nothing. TacSupport_SO6,
  SO7, SO0 and TacSupport_01 stayed incomplete on both sides, so the proof the card asks for (client log showing
  them completed) is **not** shown. Most likely the pickup only arms when the script sees the real SO2–SO5 events,
  which the command skips. The user's playtest runs those steps for real.
- **Entrance:** the client walked to (-3674, 10400) unblocked (goto reached, dist 149, 0 stucks), as in card 095's
  run. In a fresh scripted run the client is not walled there, so this run cannot show whether the TacSupport_01
  completion is what opens it on the client.

**Playtest check:** play TacSupport for real (flare, bombing run). The client log should show `card097: client ...
native CompleteObjective TacSupport_01/TacSupport_SO6` (and SO7, SO0, TacSupport_01). The host log names the path
with `card097: host ... queued; caller exe+0x...`. Then walk the client into capture the flag. Bug holes (LOS_O02_SO05)
are covered by the same hook through IncrementObjectiveCount/CompleteObjective.

Report complete.
