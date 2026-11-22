# 246: Full-objective soak, FED_C5M1
Status 2026-11-22: open, parked until after the user's playtest.

Same shape and rules as card 240 (read `docs/cards/done/240-*.md` or `docs/cards/240-*.md` first: no
`objective complete`, wait on the host Complete row and the client replay, both players in presence volumes,
objectives dumps at the end, faults become their own cards).

Goal (one): one scenario that plays FED_C5M1 from spawn to the mission end and completes every objective by
its own trigger.

Objective list M = 37, from the game's data table `DT_LevelObjectiveData_FED_C5M1` (keys
`FED_C5M1_<id>_NAME`; the runtime ids may differ in case):
EXCAVATORA_O1
EXCAVATORA_SO1
EXCAVATORA_SO2
EXCAVATORA_SO2B
EXCAVATORA_SO3
EXCAVATORA_SO4
EXCAVATORB_O1
EXCAVATORB_SO1
EXCAVATORB_SO2
EXCAVATORB_SO3
EXCAVATORB_SO4
EXCAVATORB_SO5
EXCAVATORB_SO6
EXCAVATORB_SO7
EXCAVATORB_SO8
MUNITIONSBASE_O1
MUNITIONSBASE_SO1
MUNITIONSBASE_SO2
MUNITIONSBASE_SO3
MUNITIONSBASE_SO4
MUNITIONSBASE_SO5
MUNITIONSBASE_SO6
MUNITIONSBASE_SO7
OPENING_O1
OPENING_SO1
OPENING_SO2
OPENING_SO3
OPENING_SO4
RESEARCHSTATION_O1
RESEARCHSTATION_SO1
RESEARCHSTATION_SO2
RESEARCHSTATION_SO3
RESEARCHSTATION_SO3B
RESEARCHSTATION_SO3C
RESEARCHSTATION_SO4
RESEARCHSTATION_SO5
RESEARCHSTATION_SO6
The last soak (c234-c5m1) sent 1 sub-objective completions: ResearchStation_SO1 

Step 1, static, no run, appended here as a table: each id in play order with parent, counter, real trigger
with level-script or class evidence (`FED_C5M1_Scripting`), the driver steps that fire it, the synced
follow-ups to wait on, and any id that is a marker only or on a branch the map never activates (state the
evidence; such an id leaves M only with the senior's agreement).

Step 2: `tools/scenarios/c246-c5m1.txt` (new file; older scenarios stay).

Step 3: run, budget 3 runs. Command:
`systemd-run --user --scope -q -p MemoryMax=12G env SCENARIO=tools/scenarios/c246-c5m1.txt DEPLOY=1 FAST=1 HOST_EXTRA_ENV="UBWCOOP_TRAVEL=/Game/Maps/FED/FED_C5M1?listen UBWCOOP_GOD=1 UBWCOOP_C132_FX=1 UBWCOOP_C136_LOG=1" CLIENT_EXTRA_ENV="UBWCOOP_C132_FX=1 UBWCOOP_C136_LOG=1" tools/run_pair.sh c246-c5m1 3600`

Files allowed: `tools/scenarios/c246-c5m1.txt`, this card. Driver verbs only after a report to the senior.
No other mod file. No build while a game runs.

Done when: final objectives dump shows `complete 1` for M of M on host and client, lists match, both sides
reach the next map (FED_C5M2), 0 crash, every digest round 0 mismatches, frame
windows at or over 500 outside load. Report: completed N of M, each fault with its lines, new WARN classes
against c234-c5m1.

## Senior prep (2026-11-13): what the junior needs before step 1

Not started. Starts after the 2026-11-13 playtest, in card order, one map at a time.

- Model for step 1: the "Step 1 result" section of `docs/cards/243-full-objective-soak-c3m2.md` (line 80 on):
  objective table (id, parent and counter, real trigger with ubergraph offsets, driver steps, wait rows), then
  the encounter manager chains, presence volumes, switches with their stand side, timed holds and the mission
  end. Same columns here, from `FED_C5M1_Scripting` and `DT_LevelObjectiveData_FED_C5M1`.
- Rules that card 243 added and this card inherits: "Rules carried from cards 241 and 242" in card 243, and
  "Gameplay test scenario rules" in `docs/STATE.md`. In short: no `objective complete`; waits on log rows, never
  sleeps; `clear` before every press, aim, throw and damage step (card 265); a `shot` after every warp and look
  at a target, each one viewed (card 264); menus close by `winkey` or `ui continue`, never `key` (card 222).
- Scenario from a generator: copy `tools/gen_c243.py` to `tools/gen_c246.py` (it stamps the DLL hash guard and
  the map check); under 2000 steps; `python3 tools/drive.py --parse <file>` must report 0 bad before any run.
  The old route `tools/scenarios/c234-c5m1.txt` gives the walk order and the warp points only; its objective steps
  are not by real trigger.
- Start of every FED map: the client stays on the operation card until `host ui continue` and the client
  continue step (see the head of `tools/gen_c243.py`); keep that block.
- Run policy (user 2026-11-13): discovery mode, at most 2 runs per map per day, `voidsoft` after the DLL and map
  checks so one launch shows every fault of the route; no rerun for a cleaner pass while mod bugs are open.
  Each fault becomes its own card with its log lines; nothing is fixed inside this card.
- Before a run: GPU `cat /sys/class/drm/card1/device/gpu_busy_percent` under 30, no other game running, the
  `MemoryMax=12G` scope, test sound to the null sink.
- Ids that are markers only or never added leave M only with the senior's agreement: report them with the
  evidence (no Add or Complete call in the ubergraph) before the first run.
