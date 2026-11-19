# Card 111: More bugs, and more attacking at once, with more players

Status: written 2026-11-06 (senior); sent and accepted 2026-11-06.

Read:

- card 103 Report (attack tickets: `GetTicketPriorityScore`, the ticket manager);
- card 043 roster (`c43_roster`, player count on the host).

Repo root: `/path/to/ultimate-bug-war-coop`. DLL base: `a9ef553e1cddadbf` (card 110).

## Why

User decision (2026-11-06): difficulty rises with the number of players, as more bugs and more bugs attacking at
once. Bug health, damage and the spawn mix stay as they are.

The game already scales both by difficulty (senior, from the PDB publics):

- **Wave size:** `USTEncounterWaveBase::FactorDifficultyIntoWaveBudget(const TObjectPtr<USTPreMadeEncounterWaveData>&,
  int)` 0x0511E8A0 returns the wave budget (int) after the difficulty factor.
- **Attackers at once:** `ASTTicketManager::GetTicketLimit(ESTTicketType)` 0x050EBF30;
  `RefreshTicketLimits` 0x050FE290 and `RefreshTicketLimits_DifficultyChanged(EDifficultyLevel)` 0x050FE340;
  `CreateTicketContainer(ESTTicketType, int, float)` 0x050E2750 and `TryRefreshTicketContainer(ESTTicketType, int,
  float)` 0x0510A320 (the int is very likely the limit).

## Do

1. **Measure (static, then one log run on FED_C2M1 with both players).** Log each `FactorDifficultyIntoWaveBudget`
   result (wave, base, result) and each ticket container's type and limit as created or refreshed. Name which
   encounters use the budget (pre-made waves, dropship waves, spawners outside encounters) and which spawns it does
   not cover.
2. **Scale on the host only** (the client spawns no enemies). Factor per player count: 1 player 1.0, 2 players 1.6,
   then +0.5 per extra player, one table (`kC111Scale[kC43Max]`). Apply it:
   - to the wave budget: the detour returns `round(result * factor)`;
   - to each ticket limit at container creation and refresh (`ceil(limit * factor)`, at least the original + 1 for
     2+ players).
   The count is the players present when the wave starts or the limits refresh. When a player joins or leaves
   mid-map, call `RefreshTicketLimits` once so the limits follow; waves already spawned are not changed.
3. **Proof, one short FED_C2M1 run** with both players: the log shows budget and limit lines with base and scaled
   values, and more bugs attacking at once than a one-player baseline from the same log (count of tickets held at
   peak). rc 0, ERROR/FATAL 0, no new dump. A one-player run is not needed: factor 1.0 leaves the values unchanged,
   so show that path by code.

Stop when the fix builds and that run shows it; the user will playtest.

## Done when

The Report has the budget and ticket call sites, what each covers, the scale table, the run lines (base and scaled),
the peak tickets, the run length, rc, ERROR/FATAL, dumps and the DLL hash. Ends with "Report complete."

## Report

Junior, 2026-11-06. DLL 700ec68235953110 (`build/dwmapi-c111-700ec68235953110.dll`). Code in `mod/src/ubw/bug_scale.cpp`
(new); five symbols in `tools/symbols.txt`; hook rows and `c111_step` call in `hooks_ubw.cpp`; declarations in
`hooks_internal.h` (also the missing `c31_weak_get` declaration). Scenario `tools/scenarios/c111-proof.txt`.

### Call sites and what they cover

- **Wave budget:** `USTEncounterWaveBase::FactorDifficultyIntoWaveBudget` (0x0511E8A0) returns
  `(int)(budget * difficulty float)`. It has two callers:
  - `ASTEncounterManagerBase::InitialiseWave` (call at 0x05129407, logged as caller exe+0x512940C). The budget is the
    pre-made wave data's +0x28, and the procedural enemy list (data +0x60) is generated against the result.
  - `USTEncounterWaveBase::SetupReinforcements` (call at 0x05137BCB, logged as exe+0x5137BD0).
  - Not covered: explicit enemy lists in the wave data (+0x70), bosses, and spawners outside encounters. Their counts
    stay as the game sets them.
- **Ticket limits:** `ASTTicketManager` keeps one container per ESTTicketType (1 Melee, 2 WeakProjectile,
  3 DeadlyProjectile, 4 AreaAttack). The limit is at container +0x90; `RequestTicket` (0x050FF6D0) grants a ticket while
  used (+0x8C) + cost <= limit.
  - `CreateTicketContainer` (0x050E2750) is called only from BeginPlay, four times.
  - `TryRefreshTicketContainer` (0x0510A320) is called from `RefreshTicketLimits` (0x050FE290), four times.
  - Both detours scale their limit argument. `RefreshTicketLimits` is also what the mod calls when the player count
    changes.
  - `RequestTicket` is hooked for logging only: it logs peak holders and points per type.

### Scale table

`kC111Scale = {1.0, 1.6, 2.1, 2.6}` for 1 to 4 players.
- Budget: `lround(result * f)`.
- Limits: `ceil(limit * f)`, at least limit + 1 with 2+ players; a limit of 0 stays 0.
- One player: f 1.0. The budget returns the game's result as is (`players <= 1` path), and `c111_scale_limit` returns
  the limit unchanged. Both paths are visible in the run: the create lines and the waves set up before the client joined
  show `1 players`, with scaled equal to base.
- The player count is the PlayerControllers in the world, found through the object's outer chain. Wave objects are
  outered to a Package, not the level, so the wave detour takes the ticket manager's world. In c111a/b the waves read
  1 player because of this; fixed in c111c.
- Player join or leave: `c111_step` sees the count change and calls `RefreshTicketLimits` once. Waves already set up
  are not changed.

### Run c111c (two players, FED_C2M1, FAST=1)

Ended by the scenario after 79 s. Host rc 0, client rc 0, drive rc 0. FATAL/ERROR 0 on both sides; no new crash dumps.
No SystemFunction036 abort in any of the three launches (c111a, c111b, c111c).

```
card111: ticket limit create Melee: base 100, scaled 100 (1 players, factor 1.00)     (x4 types, BeginPlay, before join)
card111: players 1 -> 2, refreshing ticket limits
card111: ticket limit refresh Melee: base 100, scaled 160 (2 players, factor 1.60)    (x4 types)
wave budget (data BP_PWD_Warriors_Medium):     base 16, scaled 26 (2 players, InitialiseWave)   x6
wave budget (data BP_PWD_Warriors_Large):      base 24, scaled 38 (2 players, InitialiseWave)   x3
wave budget (data BP_PWD_Warriors_ExtraLarge): base 32, scaled 51 (2 players, InitialiseWave)
wave budget (data BP_PWD_Warriors_Small):      base 8,  scaled 13 (2 players, InitialiseWave)   x3
wave budget (data BP_PWD_Troopers_Medium):     base 16, scaled 26 (2 players, InitialiseWave)   x3
wave budget (data BP_PWD_Warriors_Medium):     base 1,  scaled 2  (2 players, SetupReinforcements) x2
wave budget (data BP_PWD_Warriors_Medium):     base 16, scaled 16 (1 players, before join)      x2 (+1 Small 8 -> 8)
card111: ticket peak Melee: 3 enemies hold 4 points, limit 160 (base 100)
```

Peak tickets: Melee 3 holders / 4 points, the same as in c111a, where the limit was still 100. **The ticket limit is
not what bounds attackers on this map at this difficulty.** The game's limit is 100 points per type and the fight never
came near it, so raising it to 160 does not change how many bugs attack at once by itself. More attackers at once in
co-op come from the larger waves: 26 instead of 16 budget for a medium group. The limit scaling only matters where a
difficulty or a big fight brings the base limit into play. I did not chase a bigger fight; the user playtest will show
the feel.

### Open points for the senior

- Waves set up before the client joins stay at one-player size: 3 of 23 here, at map start. In real play the client
  joins after the host's map load, so the first encounters may set up unscaled. A fix would take the count from the
  lobby's expected players instead of the PlayerControllers present. Not done; that is a design choice.
- Difficulty-float ("difficulty" column) equals base here, so the test difficulty factor is 1.0.

Report complete.
