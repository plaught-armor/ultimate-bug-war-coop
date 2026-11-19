# Card 114: The difficulty values live in an editable file

Status: written 2026-11-06 (senior); sent and accepted 2026-11-06.

Read:

- `mod/src/ue/reflect.h` (the property walker, `StaticFindObject` / `StaticLoadObject`);
- `mod/src/lobby.h` (card 052 settings file `%LOCALAPPDATA%\StarshipTroopers_UBW\Saved\UBWCoop.ini`, its
  missing-file and bad-file rules);
- `mod/src/ubw/bug_scale.cpp` (`FactorDifficultyIntoWaveBudget` 0x0511E8A0 hook, card 111).

Repo root: `/path/to/ultimate-bug-war-coop`. DLL base: `71a9079ca720c324` (card 113).

## Why

User (2026-11-06): extract the difficulty values so they are easily modified. Game files are read-only, so the mod
writes the values out to a text file and applies the file at runtime.

Senior static decode (game files, unverified field names):

- `/Game/Data/Difficulty/DT_DifficultyDataTable` (`FSTDifficultyDataTableRow`, rows Easy, Medium, Hard, Extreme):
  - SpawnTable: BP_SpawnTable_Default in every row.
  - bShouldSpawnExtraEnemies: false on Easy and Medium, true on Hard and Extreme.
  - Three floats: Easy (2.0, 0.5, 1.0), Medium and Hard (1, 1, 1), Extreme (0.5, 2.0, 1.0). These are likely
    AmmoReceivedMultiplier, FedscoreMultiplier and HealingPeriodMultiplier; the order is not pinned.
  - Two accuracy curves, the same in every row.
  - DamageModifiers: DamageModPreset_Easy, _Default, _Hard or _Extreme.
- `/Game/Data/Difficulty/DamageModifierPresets/DamageModPreset_*`: 37 named entries, for example "Arachnid AI to
  Human Player" (0.25 / 1 / 2 / 4.5), "All Player Damage Taken" (0.5 / 1 / 1 / 1.25) and "All Player Damage Dealt"
  (1.5 / 1 / 0.75 / 0.5).
- Wave budget factor: `FactorDifficultyIntoWaveBudget` reads `USTDifficulty`+0x38 (difficulty 0-3).
  - It returns budget × the wave data's float at +0x2C/+0x30/+0x34/+0x38.
  - When `IsPlayerABug` is true it uses +0x3C..+0x48 instead.
  - Native defaults are 0.8 / 1.0 / 1.1 / 1.25 for both sets.
  - The BP_PWD_Warriors_* assets override some of the slots (one 0.9 and three 1.0; which slots is not decoded).

## Do

1. **Dump (both sides, once per launch, after the assets load).** Write
   `%LOCALAPPDATA%\StarshipTroopers_UBW\Saved\UBWCoop-Difficulty.ini`, but only if the file does not exist. It holds
   the game's current values, by reflection, with the real property names:
   - one section per DT row: every field, with soft references written as their path;
   - one section per DamageModPreset: one line per entry, keyed by the entry's description text. Include the
     multiplier and whatever else the entry struct holds that is a number. Use the reflected names, and leave the
     tags as read-only comments;
   - one section per pre-made wave data class (every `USTPreMadeEncounterWaveData` subclass loaded or loadable):
     the 8 difficulty factor floats (name the fields by reflection) and the base budget;
   - the ticket limits per type and difficulty, if the game keeps them per difficulty (check
     `RefreshTicketLimits_DifficultyChanged` 0x050FE340). Otherwise give one line saying they are not per
     difficulty.

   A header comment says what each section does and that deleting the file regenerates the defaults. Keep a
   read-only copy of the defaults next to it: `UBWCoop-Difficulty.defaults.ini`, rewritten every launch.
2. **Apply (host authority).** On launch, parse the file. A bad line is logged and skipped, and the rest still
   applies. Unknown keys are logged. Write the values into the loaded objects: the DT row memory, the preset entries
   and the wave data CDOs. Do this before the first map uses them, and again whenever a new one of those objects
   loads.
   - Full world sync: every player must play with the host's values. Measure which side reads each value (for
     example, ammo received and damage taken on the client's own pawn). If the client reads any value, the host
     sends its parsed values over the mod channel at join, and the client applies them in place of its own file.
     The host's file wins.
   - The card 111 player factor stays on top of the wave factor, unchanged.
3. **Proof, one run (FED_C2M1, FAST=1, both players):**
   - the file is generated on a clean start, and a second launch does not overwrite it;
   - edit two values before the run: the Medium "Arachnid AI to Human Player" multiplier to 3.0, and the
     Warriors_Medium Medium-difficulty wave factor to 2.0;
   - the log shows both values applied on the host, and on the client if it reads them;
   - a Warriors_Medium wave logs `difficulty 32` (base 16), and a bug hit on a player does 3× the damage of the
     same hit in an earlier log;
   - one deliberately bad line is logged and skipped;
   - rc 0, ERROR/FATAL 0, no new dump.

Stop when the fix builds and that run shows it; the user will playtest.

## Done when

The Report has the file path and a short excerpt of each section, the reflected field names (which settles the DT
float order and the Warriors override slots), which side reads which value, the sync path if any, the run lines, the
run length, rc, ERROR/FATAL, dumps and the DLL hash. Copy the full generated defaults file to
`docs/difficulty-defaults.ini`. Ends with "Report complete."

## Report

**Result.** The config file works. At launch the mod writes `UBWCoop-Difficulty.ini` if it is missing and keeps it
if it is there. It also rewrites the read-only `UBWCoop-Difficulty.defaults.ini` every launch. Values come from the
host and go to each client once per map. The run (c114c) shows all of the following: the two edits applied,
`difficulty 32` (base 16), bug hits on a player at 3x, the bad line skipped, rc 0, and no errors or crash dumps.
DLL `17a60609748a4770`.

**Files.** `%LOCALAPPDATA%\StarshipTroopers_UBW\Saved\UBWCoop-Difficulty.ini` (under wine,
`C:\users\steamuser\AppData\Local\StarshipTroopers_UBW\Saved\`), plus `UBWCoop-Difficulty.defaults.ini` next to it.
The defaults file is set read-only and rewritten each launch. The dump has 4 rows, 4 presets and 113 waves: 121
sections, 1177 editable values, 105515 bytes, written in 10-11 ms. It is copied in full to
`docs/difficulty-defaults.ini` (CRLF, as written).

**How lines work.** `key = value` lines are editable numbers, or true/false for a switch. Lines that start with
`; key = value` are read-only info: text, tags, asset references, enums and maps. When a line is bad, the mod logs
`WARN card114: file line N skipped (...)` and skips it.

Excerpts, one per section kind:

```
; UBWCoop-Difficulty.defaults.ini: the game's difficulty values, for the co-op mod (card 114).
; READ ONLY: the game's defaults, rewritten every launch. Edit UBWCoop-Difficulty.ini instead.
; In co-op the host's file counts: the host sends its values to each player who joins.
...
; Ticket limits (how many bugs attack at once) are not per difficulty: they come from a ticket set,
; not from this data, so they are not in this file.

[Difficulty.Medium]
; SpawnTable = /Game/Data/Enemies/TranslationTables/BP_SpawnTable_Default.BP_SpawnTable_Default_C
bShouldSpawnExtraEnemies = false
AmmoReceivedMultiplier = 1
HealingPeriodMultiplier = 1
FedscoreMultiplier = 1
; RangedAttackTargetHitPercentageCurve = /Game/Data/Curves/EnemyRangedAccuracy/RangedAttackOntargetChance.RangedAttackOnTargetChance
; RangedAttackMissDeviationCurve = /Game/Data/Curves/EnemyRangedAccuracy/RangedAttackDeviationDistribution.RangedAttackDeviationDistribution
; DamageModifiers = /Game/Data/Difficulty/DamageModifierPresets/DamageModPreset_Default.DamageModPreset_Default_C

[DamageModPreset_Default]
; Arachnid AI to Human Player.InstigatorTagsMatchMethod = 2
; Arachnid AI to Human Player.DamageInstigatorTags = Faction.Alien.Aphid, Faction.Alien.AphidShotgun, ... (cut with " ...")
; Arachnid AI to Human Player.ReceiverTagsMatchMethod = 2
; Arachnid AI to Human Player.DamageReceiverTags = Faction.Human.Player
Arachnid AI to Human Player = 1

[Wave.BP_PWD_Warriors_Medium]
WaveBudget = 16
WaveMultipliers_Human.WaveBudgetMultiplier_Easy = 0.9
WaveMultipliers_Human.WaveBudgetMultiplier_Medium = 1
WaveMultipliers_Human.WaveBudgetMultiplier_Hard = 1.1
WaveMultipliers_Human.WaveBudgetMultiplier_Extreme = 1.25
WaveMultipliers_Bug.WaveBudgetMultiplier_Easy = 1
WaveMultipliers_Bug.WaveBudgetMultiplier_Medium = 1
WaveMultipliers_Bug.WaveBudgetMultiplier_Hard = 1
WaveMultipliers_Bug.WaveBudgetMultiplier_Extreme = 1
```

**Field names (from reflection).**
- **DT row** (`FSTDifficultyDataTableRow`), in property order:
  - `SpawnTable`
  - `bShouldSpawnExtraEnemies`
  - `AmmoReceivedMultiplier`
  - `HealingPeriodMultiplier`
  - `FedscoreMultiplier`
  - `RangedAttackTargetHitPercentageCurve`
  - `RangedAttackMissDeviationCurve`
  - `DamageModifiers`

  This fixes the order of the three floats: Ammo, Healing, Fedscore. Easy is 2 / 0.5 / 1 and Extreme is
  0.5 / 2 / 1.
- **Presets.** `UADDamageModifierPreset.TagModifierSets` is an array of `FADTagDamageModifierSet`. Each element has
  `Description` (the key), `InstigatorTagsMatchMethod`, `DamageInstigatorTags`, `ReceiverTagsMatchMethod`,
  `DamageReceiverTags` and `Multiplier`. `Multiplier` is the editable value line under the description key. When
  two elements share a description, the second gets a key ending in " #2".
- **Waves** (`USTPreMadeEncounterWaveData` CDOs): `WaveBudget`, `WaveMultipliers_Human` and `WaveMultipliers_Bug`.
  Each multiplier set holds `WaveBudgetMultiplier_Easy`, `_Medium`, `_Hard` and `_Extreme`.
- **Warriors override slots.**
  - Most waves use Human 1 / 1 / 1 / 0.9 and Bug 0.8 / 1 / 1.1 / 1.25.
  - The `BP_PWD_Warriors_*` assets instead use Human 0.9 / 1 / 1.1 / 1.25 and Bug 1 / 1 / 1 / 1.
  - So on the human side, Easy (0.9) and Medium (1.0) differ from the usual set, and the Hard and Extreme values
    sit one slot earlier. On the bug side all four slots are 1.0.

**Which side reads what, and how values reach it.**
- **Host or solo.** The host applies its own file at startup.
  - The DT row values are written into the table rows, then `USTDifficulty` LoadSettings runs so the settings
    reload.
  - Preset values are written into the active preset CDO (subsystem+0x30). This is checked again inside the
    damage hook `detour_c114_damage`, which is chained after card 040's `GetModifiedDamageByTags` trace.
  - Wave values are written into the wave CDOs before card 111's wave budget code reads them; card 111's player
    factor is applied on top.
  - Reads measured on the host: damage (all 18 logged lines), the wave factor (3 calls on the menu map), and
    FedscoreMultiplier (`reads FedscoreMultiplier = 1.000`).
- **Client.** Damage reads and wave sizes are worked out on the host, so the client's damage counter stays at 0.
  - The client reads the wave factor (15 calls in the session map in c114b). It gets the host's values over
    `ChanOp::Difficulty`, which uses text frames: `B n`, then `I section|key=val~...`, then `E n`.
  - The host sends once per map per PC, from the `c49_first_pong` gate.
  - The client writes the values and logs `host values in force, N applied (host sent N)`.
  - The host's values replace whatever the client's own file set.
- **Ammo and healing getters.** These were not called in either run, so neither side read them. They are hooked
  and logged for the playtest.

**Proof run c114c.**
- **Setup:** FED_C2M1, FAST=1, both players, `tools/scenarios/c114-proof.txt`. The scenario warps the host next
  to a `BP_AI_WarriorBug_C` and waits for a bug hit on a player.
- **Edits in the user file:**
  - line 161: `Arachnid AI to Human Player = 3.0` in [DamageModPreset_Default];
  - line 1676: `WaveMultipliers_Human.WaveBudgetMultiplier_Medium = 2.0` in [Wave.BP_PWD_Warriors_Medium];
  - line 1677: the bad line `WaveBudget = sixteen`.
- **Result:** run length 53 s. Host rc 0 and client rc 0. FATAL/ERROR 0 on both sides, with 1 WARN each (the bad
  line). No new crash dumps. Drive rc 0.

```
host:   card114: C:\users\steamuser\AppData\Local\StarshipTroopers_UBW\Saved\UBWCoop-Difficulty.ini exists, kept; defaults ...UBWCoop-Difficulty.defaults.ini rewritten (105515 bytes)
host:   WARN card114: file line 1677 skipped (not a number): sixteen
host:   card114: file sets [DamageModPreset_Default] Arachnid AI to Human Player = 3 (game 1)
host:   card114: file sets [Wave.BP_PWD_Warriors_Medium] WaveMultipliers_Human.WaveBudgetMultiplier_Medium = 2 (game 1)
host:   card114: host or solo: [DamageModPreset_Default] Arachnid AI to Human Player: 1 -> 3 (Default__DamageModPreset_Default_C)
host:   card114: host or solo: [Wave.BP_PWD_Warriors_Medium] WaveMultipliers_Human.WaveBudgetMultiplier_Medium: 1 -> 2 (Default__BP_PWD_Warriors_Medium_C)
host:   card111: wave budget STEncounterWaveBase (outer Package, data Default__BP_PWD_Warriors_Medium_C): base 16, difficulty 32, scaled 32 (1 players, ...)
host:   card114: host sent 2 values to BP_PlayerController_C in 3 frames            (once, the only send)
host:   card111: wave budget ... Default__BP_PWD_Warriors_Medium_C): base 16, difficulty 32, scaled 51 (2 players, ...)
host:   card114: host or solo damage 25.000 -> 75.000 (preset Default__DamageModPreset_Default_C; instigator Faction.Alien.Warrior, ...; receiver Faction.Human.Player ...)
client: card114: client: [Wave.BP_PWD_Warriors_Medium] WaveMultipliers_Human.WaveBudgetMultiplier_Medium: 1 -> 2 (Default__BP_PWD_Warriors_Medium_C)
client: card114: client: host values in force, 2 applied (host sent 2)
```

**Damage lines** (host, the logged hits):

| Hit | Damage (before -> after) | Count |
|---|---|---|
| Warrior on a player | 25 -> 75 | 8 |
| Warrior on a player | 10 -> 30 | 1 |
| Warrior on a Warrior | 25 -> 0 and 10 -> 0 | 3 |
| Player on a bug | 800 -> 800 | 5 |

Bug hits on a player are 3x. Bug-on-bug and player-on-bug hits are not changed.

The card asks for "3x the damage of an earlier log", but no earlier log has a bug-on-player `GetModifiedDamageByTags`
line. Card 040's logs only cover player-on-bug and bug-on-bug hits. So the 3x is shown by:
- each line's game input (25), next to the output (75);
- the game's default for this row, which is 1 (`Arachnid AI to Human Player = 1` in `docs/difficulty-defaults.ini`).

I tried a solo baseline run (c114d) with the line set back to 1, but no bug hit the player within its 60 s wait.
Its scenario was deleted.

**Changed files.**
- New: `mod/src/ubw/difficulty.cpp`, `mod/src/ubw/difficulty_waves.inc`, `tools/scenarios/c114-proof.txt`,
  `docs/difficulty-defaults.ini`.
- Edited:
  - `mod/src/ubw/hooks_internal.h`;
  - `mod/src/ubw/hooks_ubw.cpp`: card 040's ByTags row is now card 114's chained row, plus the getter, call and
    step rows;
  - `mod/src/ubw/chan_ops.h` and `mod/src/ubw/chan_ops.cpp`: `ChanOp::Difficulty`;
  - `mod/src/ubw/objectives.cpp`: `c114_send` in `c49_first_pong`;
  - `mod/src/ubw/bug_scale.cpp`: `c114_wave` at the top of `detour_c111_factor`;
  - `mod/CMakeLists.txt`;
  - `tools/symbols.txt`.
- The user file under `prefix/` still holds the three test edits. Delete it to get a fresh copy from the game's
  defaults.

Report complete.
