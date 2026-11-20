# Card 112: Waves set up before a player joins follow the new player count

Status: written 2026-11-06 (senior); sent and accepted 2026-11-06 (rescale path not run; card 113 takes the lobby count).

Read:

- card 111 Report (`mod/src/ubw/bug_scale.cpp`, the wave budget callers `InitialiseWave` and `SetupReinforcements`,
  the "players 1 -> 2" refresh, the open point about 3 of 23 waves);
- `docs/design/coop-channel-lobby-downed.md` item 4 (ready-up on the mission map, under the pause).

Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`. DLL base: `700ec68235953110` (card 111).

## Why

User decision (2026-11-06): the wave size comes from the lobby's player count, and a late join adjusts it on the
fly. The mission map sets up some waves while it loads. The clients join that map during the ready-up pause, so
those waves are set up before the clients are in. In c111c, 3 of 23 waves kept their one-player size for this
reason.

One mechanism covers both cases: when the player count changes, every wave that is set up but has not started
spawning gets its budget scaled again to the new count. A join during ready-up (the lobby) and a mid-map join go
through the same path.

## Do

1. **Measure (static, then the run below).**
   - Where `InitialiseWave` and `SetupReinforcements` store the budget that `FactorDifficultyIntoWaveBudget`
     returns: the wave object's field (offset) and the type.
   - Which wave field or call marks that the wave has started spawning, so started waves can be left alone.
   - Whether waves outlive the map (Package outer, card 111), so stale waves from an earlier map are never touched.
2. **Build (host only, in `bug_scale.cpp`).**
   - At setup, record each wave in a fixed table with its world, its budget after difficulty (before the mod's
     factor) and the count it was scaled for. Use a weak reference, not a raw pointer. Size the table for the
     largest map plus margin. A full table logs once and leaves extra waves at their setup size.
   - On every player count change (the card 111 join/leave point), for each recorded wave of the current world that
     has not started: set its budget to `round(base * factor(new count))` and log the old and new value. Then call
     `RefreshTicketLimits` as now.
   - A leave shrinks waves that have not started in the same way. Waves that have started are not changed.
   - On a world change, clear the table.
3. **Proof, one short FED_C2M1 run** with the normal test order (the client joins after the map loads):
   - the log shows the waves set up before the join rescaled (for example Medium 16 -> 26);
   - one of those waves later spawns its scaled count (the spawn count for that wave, or the budget it reads at
     spawn time);
   - rc 0, ERROR/FATAL 0, no new dump.
   A leave does not need a run; the code shows that path.

Stop when the fix builds and that run shows it; the user will playtest.

## Done when

The Report has the budget field and the started flag, the table size, the rescale lines from the run, the spawn
check, the run length, rc, ERROR/FATAL, dumps and the DLL hash. Ends with "Report complete."

## Report

DLL `build/dwmapi-c112-822338c0bb9e6704.dll` (build 822338c0bb9e6704). Runs c112a, c112b and c112c: rc 0, ERROR/FATAL
0 on both sides, no new crash dumps, no SystemFunction036 abort. The one host WARN in c112c is the card 043 real death
from the fight. Run lengths: c112a 79 s, c112b 117 s, c112c 177 s.

**Result: the rescale code is in, but no run had a wave to rescale.** On FED_C2M1 a wave that InitialiseWave sets up
starts in the same tick (the same millisecond), so at a join almost every wave has either started or not been set up
yet. The Medium 16 -> 26 example cannot happen for the three start waves: they are set up and started at map load,
before any client can join, and started waves are not changed (per the card).

### Measured

- **Budget field.** The wave does not store the budget. InitialiseWave (0x05129290) passes the difficulty result
  straight into GenerateProceduralSpawnList (0x05120120). That call empties the wave's spawn list (TArray of
  FSTProceduralEnemy at wave +0x2B8, Num at +0x2C0; 16-byte entries: class at +0, cost int at +8, weight float at
  +0xC) and fills it up to the budget from the data's procedural list (data +0x60). The spawn list is the state.
  `USTEncounterWaveBase::SetProceduralEnemies(int budget, const TArray&)` (RVA 0x051378B0) is the same call with a
  new budget, so the rescale calls it with `data +0x60` and the new budget.
- **Started flag.** Byte wave +0x3A9. ActivateWave (0x05115980) returns early if it is set, then writes WORD +0x3A8
  = 0x0100. ClearWave sets it back to 0. Byte +0x3A8 = complete/empty.
- **Lifetime.** Wave objects are outered to a Package, not the level, so each table entry keeps its world (taken
  from the ticket manager, as in card 111). The table clears on a world change, and a dead weak reference drops its
  entry.
- **Timing (c112a host log).** 20 InitialiseWave setups. 18 of them log `card112: activate` in the same ms as the
  setup. The other 2 are Medium waves set up next to an ExtraLarge/Large wave (lines 1874 and 1877): later waves of
  an encounter, set up but held. These are the only waves the rescale can catch, and only when a join happens while
  that encounter is running. Reinforcement waves (SetupReinforcements, caller exe+0x5137BD0) are scaled at setup by
  card 111 and are not in the table.

### Built (bug_scale.cpp)

- **Table.** `C112Table`, 64 entries (c111c saw 23 waves on C2M1). Each entry holds a weak reference to the wave and
  the data CDO, the world, the base budget after difficulty and the player count the list was made for. A full
  table logs once (`card112: wave table full`) and leaves the wave at its setup size.
- **Recording.** In `detour_c111_factor`, only when the return address is the InitialiseWave call (exe+0x512940C),
  host only.
- **Rescale.** `c112_rescale(world, players)` runs in `c111_step` on every count change, just before
  `RefreshTicketLimits`. For each entry of the current world: if the reference is dead, the world differs or +0x3A9
  is set, the entry drops. Otherwise it calls
  `SetProceduralEnemies(wave, lround(base * factor(new)), data + 0x60)` and logs `card112: rescale ...` with the
  budget before and after and the spawn list's entries and cost before and after. A leave takes the same path with
  a smaller count.
- **Spawn check.** ActivateWave is hooked log only: `card112: activate <wave>: spawn list N entries cost C (budget B,
  P players)`; the entry then leaves the table.

### Run lines

- **c112a (normal order, join after map load):**
  - before the join: `activate ...: spawn list 3 entries cost 15 (budget 16, 1 players)` (x2) and `2 entries cost 8
    (budget 8, 1 players)`;
  - at the join: `card111: players 1 -> 2, refreshing ticket limits`, then
    `card112: 0 waves rescaled for 2 players, 0 not started in the table`;
  - after the join: the encounter waves activate at the scaled size, for example `spawn list 5 entries cost 25
    (budget 26, 2 players)`, `7 entries cost 35 (budget 38, ...)` and `10 entries cost 50 (budget 51, ...)`.
- **c112b and c112c:** tried to get the host into the first encounter before the join (host warps first,
  `CLIENT_DELAY_MS` 40000 then 95000). The client still joined about 1-10 s after the host pawn appeared, and the
  encounter started about 3 s after the join both times, so the result is the same as c112a. CLIENT_DELAY_MS does
  not push the join past the host's map load; the join follows the host listening. Scenario
  `tools/scenarios/c112-proof.txt` is kept.

### Not proven

The rescale and its log line have not run: no wave was held at a join in any of the three runs. The way to reach
it is a join (or a leave) while an encounter is running with held waves. The runner cannot time that today, but
the user's playtest can (the friend joins or drops mid-fight).

### Open question

Should a **started** wave grow on a join? The pre-join start waves (16/16/8) are the ones a late joiner meets at
once. The spawn list of a started wave keeps spawning through spawners, so regenerating it could change what is
left to spawn. That needs a measurement of how ActivateWave and the spawner callback consume the +0x2B8 list.

Report complete.
