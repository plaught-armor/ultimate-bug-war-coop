# 305 Tango outpost on FED_C3M1 has no bugs

Playtest 2026-11-13 (user plays as client, FED_C3M1): "tango outpost bugged, there are no bugs".

## Trace (client log `UBWCoop-menu-20261113-184603.log`)

The client entered FED_C3M1 three times; the host's mission state came from a save on the second and third.

| Client t (ms) | Event |
|---|---|
| 257961950 to 258393119 | Session 1, fresh map. 257978451 `BEACHDEFENCETANGOINTRO02`. 257985656 `Beach_SO2` complete, `Beach_SO5` added (flags 0xE). 258288794, 258299121, 258309286 `Beach_SO1_A`, `_B`, `_C` complete, `Beach_SO1` 3/3, `Beach_SO3` added. 258370689 `Beach_SO3` complete, `Beach_SO4` added. |
| 258388391 | Both players dead; host loads its last save. Snapshot: `Beach_SO2` and `Beach_SO1` (3/3) complete, `Beach_SO3` and `Beach_SO5` open. |
| 258395629 to 258535231 | Session 2 (host from save; `IntroStart` still says `bFromSave 0`). 258397902 `BEACHDEFENCESIERRAINTRO01` plays at load. 258460130 `Beach_SO3` complete, 258503521 `Beach_SO4` and `Beach_Marker1` complete. |
| 258535231 to 258535238 | Client connection closed (`reason 14 Unknown`), client falls to a standalone map (card 290 class). User rejoins by hand (J, Enter) at 258539363. |
| 258542490 on | Session 3. Snapshot 258544525 to 258544665: same adds as session 2, then `Beach_SO3`, `Beach_SO4`, `Beach_Marker1` complete. 258544656 `BEACHDEFENCESIERRAINTRO01`, 258552807 `BEACHDEFENCETANGOINTRO01`. 258552846 host `Objective` rows (sent host t=84683693) set `Beach_Marker1`, `Beach_SO2`, `Beach_SO5` visible (flags 0x3). 258693792 and 258707432 `Monorail_SO1`, `_SO2` complete. |

Facts that bear on the report:

1. `Beach_SO5` is added in every session and never completed in this log. It becomes visible together with the
   Tango intro conversation at 258552807, so it is the best candidate for the Tango outpost objective.
2. The Tango intro and the visibility rows come from the host 10 s after the client's rejoin, on a host whose
   save already holds `Beach_SO1` 3/3, `Beach_SO3` and `Beach_SO4` complete. The host's script started a step
   whose earlier stages were restored from the save.
3. Enemy rows exist after the rejoin (card037 82 rows in 258570000 to 258600000, 74 in 258690000 to 258720000),
   so spawning in general works; the gap is at this one outpost. No spawner row of any kind names a Tango wave:
   after 258546000 the only spawner rows are two `BP_TentSpawner_HumanGameplay_C` timelines (258678155,
   258680939).
4. Snapshot replay bug seen beside it: `AddSubObjective(Beach_SO1, Beach_SO1_A/B/C)` and
   `(Resort_SO3, Resort_SO3_A/B/C)` leave the count unchanged (`objectives 14 -> 14`), then `WARN card053: client
   lacks the host's objective ...` for all six, and digest rounds 115 to 118 show 7 `STLevelObjective` presence
   mismatches. On a fresh map the parent is added with `AddCountSubObjective_SubObjectives` (flags 0x4); the
   snapshot adds it with `AddCountSubObjective`, which has no child slots.

## Later in the same session (second report: "i killed flack bug didn't update mission")

| Client t (ms) | Row |
|---|---|
| 259048293 | host `CalloutObjective(Resort_SO3)`; `Resort_SO2` complete |
| 259102716 | client's own hold done on `SatchelSiteCAC_BP_SatchelPlacementSite_C_CAT_0` (building `BP_SatchelBuilding_Flakbug_C_2`) |
| 259105813 | `replayed CompleteSubObjective(Resort_SO3, Resort_SO3_B)`: no `card097 native CompleteObjective` row follows, no `IncrementObjectiveCount Resort_SO3` (every other replayed completion in this log has its native row) |
| 259105826 | world `S` row: the flak bug explodes on the client |
| 259151140 | `Beach_SO5`, `Beach_Marker2` and `Beach_O1` complete |

Fact 4 is the cause of the second report: `Resort_SO3_B` does not exist on the client after the join snapshot,
so the host's completion finds nothing, the count `Resort_SO3` stays 0/3 on the client's HUD and the kill shows
no mission update. The same will happen for `Resort_SO3_A` and `_C`. Part (b) of the fix is therefore a
confirmed playtest bug and comes first.

`Beach_SO5` did complete 598 s after the Tango intro, so the Tango stage can finish on the host; whether it
finished by a wave that came late, by another route, or with bugs the client did not see is for the host log.
The replay path must also warn when a replayed completion or count finds no objective:
`WARN card305: client replayed <op> <parent>/<child>: objective missing`.

## Cause (static; candidates, to confirm in step 1)

1. Save restore leaves the Tango wave unarmed on the host: the save restores objective state but the outpost's
   spawners or its start trigger were consumed before the save (or are not saved), so after "Load Last Save" the
   script announces Tango and nothing spawns. This would also fail in single player and is then a game defect the
   mod must still not make worse; step 1 item 3 decides.
2. The mod's restart or rejoin path changes what the host's script sees: the host's restart after both players
   died (card 294 path) or the client's join (a new pawn at the player start, card 288 class "which pawn does the
   trigger test") re-fires or skips a script step, so the Tango stage starts without its spawner activation, or
   the activation ran once in session 1 and its bugs were killed or removed by a mod path (corpse cleanup, the
   card 098 world journal marking spawners dead: `BP_DestructibleSpawner_Bugnest_HumanGameplay_C_16` is in the
   replayed journal as destroyed).
3. The bugs exist on the host and are not shown on the client (spawn rows not sent for this spawner class after a
   rejoin). The host log decides this.

## Step 1 (static, junior, no run; host log after the playtest)

1. In `FED_C3M1_Scripting_C`: which objective id is the Tango outpost (`Beach_SO5` expected), what starts its
   wave (trigger volume, conversation end, objective complete), which spawners or horde zones it activates, what
   completes it, and whether it is optional.
2. What the game saves for that stage: the script's stage variable, each spawner's activated and exhausted
   state, and what the script does on a load from a save taken after `Beach_SO1` 3/3 (the `bFromSave` branch).
   State why `IntroStart` carries `bFromSave 0` on a host that loaded a save.
3. Whether the same save loaded in single player spawns the Tango wave (from the script alone; no run).
4. Every mod path that can touch those spawners or the stage: card 294 restart, card 098 journal (spawner K
   rows), card 132 ext rows for `BP_AISpawner_C`, join-time replay, the trigger pawn test of card 288.
5. Snapshot add kind (fact 4): where the snapshot writer picks `AddCountSubObjective` for a parent that was added
   with `AddCountSubObjective_SubObjectives`.
6. From the host log: host t=84506000 to 84873000 (client t minus 173869236): the script rows for the Tango
   stage, spawner activations, enemy spawns near the outpost, and the host's restart at host t=84519155.

Write the answers into this card before coding.

## Fix

By the answer of step 1, as a class, no per-map patch:

- A wave or defence stage that the host's script starts after a save load, a restart or a join has its spawners
  armed exactly as on a fresh run; the mod neither consumes nor re-fires a stage. If the mod's restart path loads
  a save in a way the game's own "Load Last Save" does not (flag, order, missing `bFromSave`), it uses the game's
  path with the game's flag.
- A join never starts or repeats a script stage on the host: join-time pawn spawn does not count as a trigger
  overlap for stage starts (card 288 test).
- Snapshot rows carry the add kind the objective was created with, so sub-objectives of a counted objective
  exist on the client after a join or load.

- Rows: `card305: host t=<ms> stage <objective> started: <n> spawners armed, <n> already exhausted, from save
  <0|1>`; `WARN card305: host stage <objective> started with 0 live spawners`; `WARN card305: client snapshot add
  <child> under <parent> failed (parent kind <k>)`; per-map tally `card305: stages started <n>, with no spawner
  <n>, snapshot child adds failed <n>` (targets 0).

## Sibling sweep (same class: a stage started on restored or joined state)

| Item | Check |
|---|---|
| Every wave, defence and outpost objective on the 7 Trooper maps (card 301 table gains a column "after save load") | spawners armed after load, restart and join |
| Host restart after both dead (card 294) and "Load Last Save" from the menu | same script path and flags as single player |
| Client join mid-mission and rejoin after a drop (card 290) | no host stage re-fired; no stale intro conversation |
| Every counted objective with sub-objectives (`Beach_SO1`, `Resort_SO3`, others from card 301) | children present on the client after snapshot; digest 0 `STLevelObjective` mismatches |
| World journal K rows for spawner classes (`BP_DestructibleSpawner_*`, `BP_TentSpawner_*`, `BP_AISpawner_C`) | a journal replay never disables a spawner the host still uses |
| `IntroStart` `bFromSave` on a host that loaded a save | value matches the host's real load kind |

## Predictions (one run, after the playtest, batched with 294)

- Host plays FED_C3M1 to after `Beach_SO1` 3/3, both players die, host loads the last save, both go to the Tango
  outpost: `card305` row with spawners armed over 0, enemy spawn rows near the outpost on both logs, the Tango
  objective completes; zero `WARN card305`.
- Client drops and rejoins during the stage: no second stage start row on the host, no intro conversation replay
  on the client, `Beach_SO1_A/B/C` and `Resort_SO3_A/B/C` present on the client.
- Digest round after each: 0 mismatches.

## Step 1 answers, item 5 (junior, static, 2026-11-13) and part (b) fix (uncompiled)

Item 5, the snapshot add kind: `c49_snapshot_add` (objectives.cpp) picked the Add kind from the parent (root or
sub) and the class (count or not) only, so a count that counts its sub-objectives was recreated with
`AddCountObjective` / `AddCountSubObjective`. The two creators differ in one byte:
`CreateNewCountLevelObjective_SubObjectives` 0x05151520 writes `+0xA0 = 1`, `CreateNewCountLevelObjective`
0x051513E0 writes `+0xA0 = 0`. `USTLevelObjectiveCount`'s `CanHaveSubobjectives` (vtable +0x2D0, 0x036DB5F0) is
`movzx eax, byte [rcx+0xA0]; ret`, and `AddSubObjective` 0x0522A890 returns early when the parent's
`CanHaveSubobjectives` is false. So on a joining client `Resort_SO3` and `Beach_SO1` existed as counts that refuse
children, every `AddSub(Resort_SO3, Resort_SO3_A/B/C)` frame did nothing, and the host's later
`CompleteSubObjective(Resort_SO3, Resort_SO3_B)` found no objective (the library returns early, no native row).
The live journal is not affected: the host's detours record the kind the script called.

Fix (S/c305.diff):
- `kObjectiveCountSubs = 0xA0` in hooks_internal.h.
- `c49_snapshot_add`: a count with `kObjectiveCountSubs` set is sent as `AddCountSubs` (root) or `AddCountSubSubs`
  (sub-objective). The marker and list bools sit in the creator's same two slots for both kinds, so the bit layout
  is unchanged (bits 0-1 root, 1-2 sub).
- `c49_snapshot_frames`: `SetCount` for a count of sub-objectives moves after every completion. A child's
  `CompleteSub` increments its parent, so a `SetCount` sent first would count twice. Same frame total.
- Client rows: `WARN card305: client replayed <op> <parent>/<child>: objective missing` before every replayed
  Complete, CompleteAny, CompleteSub, Increment, Visibility or Callout whose objective is absent (`SetCount` keeps its
  `WARN card049` row); `WARN card305: client <snapshot|replayed> add <child> under <parent> failed (parent kind
  <none|plain|count|count-subs>)` after a sub-objective Add that left no child.

Sweep (same class: an objective recreated from host state with a different creator than the script used):

| Path | Status |
|---|---|
| Snapshot Add for root and sub counts | fixed here |
| Snapshot SetCount order for counts of sub-objectives | fixed here |
| Card 066 compaction (builds the same snapshot) | covered: same `c49_snapshot_journal` |
| Live journal frames (detours record the called kind) | ok |
| Non-count objectives with children (`AddObjective` / `AddSubObjective` parents) | ok: base `CanHaveSubobjectives` 0x00C7BC80 returns 1 |
| Digest `STLevelObjective` rows | the 7 mismatches should clear; prediction below |

Prediction (batched run): after a client join past `Beach_SO1` 3/3 on FED_C3M1, the client log has
`Beach_SO1_A/B/C` and `Resort_SO3_A/B/C` adds with no `WARN card305`, the `Resort_SO3` count matches the host, a
flak bug kill moves the HUD, and the digest shows 0 `STLevelObjective` mismatches.

## Host log read (senior, 2026-11-13, `client/UBWCoop-menu-20261113-182510.log`)

- Rejoin: journal replay 85 of 85 frames at 84675369, world journal 167 of 167, `SetObjectiveVisibility` for
  `Beach_Marker1`, `Beach_SO2`, `Beach_SO5` (flags 0x3) at 84683693. No host WARN in the join.
- Host NPC count (`netbudget: host NPC sends`): 16 tracked at 84683457, **all 16 "alive far (over 5000 uu from
  every pawn)"**; 22 tracked with 11 far at 84723492; 18 with 15 far at 84763531; 21 with 12 far at 84803586;
  26 with 16 far at 84843631; rising to 57 at 84948734 and 74 at 85189045.
- So at the Tango intro the host had no bug within 50 m of either player either, and only 3 to 11 near ones for
  the next two minutes. Cause candidate 3 (bugs on the host, not shown to the client) is unlikely for the start
  of the stage; candidates 1 and 2 (the stage's wave did not start, or started late, on the host) stand.
- Not settled: which spawner the Tango stage arms and when it fired. Step 1 items 1 to 3 (script graph) answer
  it; then match against the tracked-count rise at 84888677 to 84948734.

## Part (a) closed 2026-11-13 (user)

The user reports the Tango outpost was not a mod fault: the stage's Centurions were hiding and had to be found.
The host log agrees (no bug within 5000 uu at the intro on the host either). Part (a) needs no code. Part (b),
the join snapshot's add kind and the missing client sub-objectives, stays.

## Junior trace for b8: the `WARN card305: client replayed ... objective missing` rows (2026-11-13, static, b7b-2 logs)

93 such rows in `logs/b7b-2-client.log`, all on FED_C2M1: 267197226 (the first live O02 frames), 267226829 and
the burst at 267266837-838 (`SetObjectiveVisibility` for the three markers and `LOS_O02_SO01` to `SO05`, repeated
11 to 13 times each).

Cause: the host's own objective list lacks the same objectives, so the client is in step with the host and the
WARN is false.

- Host 267197201: `AddSubObjective(LOS_O02, LOS_O02_SO02) ... objectives now 2` from `FED_C2M1_Scripting_C`; the
  host had only `LOS_O01` and `LOS_O01_SO01`. The host never logged `AddObjective(LOS_O02)` in the run, and never
  added `Plasma_Marker`, `FlakBug_Marker` or `CrashedDropship_Marker` (no Add row for any of them). The level script
  calls AddSub, CompleteSub, Callout and SetObjectiveVisibility on IDs the host's subsystem does not hold; the
  library returns early on the host exactly as on the client. These calls ran 3.6 s after the map's first Pong,
  before any scenario step on the map, so they are the C2M1 script's own start sequence, not a test warp ahead of
  the script. Whether the game intends them as dead calls is not traced; the host and client agree either way.
- The host records and sends every such call (card 049 records the called kind, not its effect), and the client
  replays it. `c305_missing_line` (`objectives.cpp:1166`) checks only the client's own list, so it cannot tell a
  real gap from a call that did nothing on the host either.
- The 267266837-838 burst is the host re-running the same script visibility calls (11 rounds of the same eight
  IDs), each sent live; there was no reconnect at that time.

Fix (proposed, not in b8 code): the host sets a bit in the frame's flags when the target objective existed on the
host when the call ran (`g_c49_exists` on the host subsystem, read before the original call); the client WARNs only
when the bit is set and its own list lacks the ID. A frame without the bit gives a plain capped row
(`card305: client replayed <kind> <id>: absent on the host too`). Same for `c305_add_line` with the parent.

Proof (next run with a script step ahead of its objective): zero `WARN card305` while the host's row shows the
objective absent; a real gap (client missing an ID the host holds) still WARNs.

## Senior review of the junior trace (2026-11-13)

Accepted: the 93 WARN rows are false (host list lacks the same IDs). Code in b9 as proposed (host "existed" bit in
the frame flags; client WARNs only with the bit set).

## Senior review of b9 (2026-11-13, static): accepted

`kC49HostExistsBit` (1<<7) is read before the detour's original, for the target or a sub-Add's parent; the client
WARNs only when the host held the objective, else writes `card305: client replayed ... absent on the host too`
(cap 24 per map). Flags up to 0x9F fit the wire (`kChanArgMax` 0x7FFF). Prediction on C2M1: zero `WARN card305` rows,
plain `absent on the host too` rows for the ids the b8 trace named.

## Senior verdict, b10 last run (2026-11-13): confirmed

C2M1: 0 `WARN card305` on both sides, 28 `absent on the host too` rows (cap 24 per map reached once).
