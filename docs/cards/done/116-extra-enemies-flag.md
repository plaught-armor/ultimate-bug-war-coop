# Card 116: What Hard and Extreme's "extra enemies" spawn, and whether co-op scales them

Status: written 2026-11-06 (senior); sent and accepted 2026-11-06 (flag has no reader; nothing to scale).

Read:

- card 114 Report and `docs/difficulty-defaults.ini` (`bShouldSpawnExtraEnemies`: false on Easy and Medium, true on
  Hard and Extreme);
- card 111 Report (what the wave budget covers: pre-made procedural lists; not explicit lists at data +0x70,
  bosses or spawners outside encounters).

Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`. DLL base: `17a60609748a4770` (card 114; card 115 built no play change).

## Why

The difficulty row has a switch, `bShouldSpawnExtraEnemies`, that nobody has traced. If it adds bugs through a path
that card 111 does not scale, then on Hard and Extreme the co-op increase misses those extra bugs. Every difficulty
should get the same player factor on every bug source.

## Do

1. **Measure (static, then one log run).**
   - Find the reader of the field: its offset in `USTDifficulty` or the row (by reflection, card 114), then the
     code that reads it.
   - Say what it spawns (extra spawners, explicit lists, a second wave), and whether that goes through
     `FactorDifficultyIntoWaveBudget`.
   - One FED_C2M1 run on Hard with both players, with the spawns logged.
2. **Build, only if it bypasses card 111.** Scale that path on the host with the same factor and session count.
   If it already goes through the budget, there is no code to write: report that and stop.
3. **Proof** (only if step 2 builds): the extra spawns at the 2-player size on Hard. rc 0, ERROR/FATAL 0, no new
   dump.

Stop when the fix builds and that run shows it; the user will playtest.

## Done when

The Report has the reader, what the flag does, whether it was scaled already, any change, the run lines, the run
length, rc, ERROR/FATAL, dumps and the DLL hash. Ends with "Report complete."

## Report

**Result.** Nothing in the shipped game reads `bShouldSpawnExtraEnemies`. The flag is dead data: it is set to true in
the Hard and Extreme rows, but no native code and no Blueprint reads it. Hard and Extreme get no extra bugs from it,
so there is no unscaled path, and card 111 misses nothing here. No code was written and no DLL was built. The play
DLL stays `17a60609748a4770`.

**Where the field is.** `FSTDifficultyDataTableRow` +0x30, a bool. Its reflection setter,
`NewProp_bShouldSpawnExtraEnemies_SetBit` (RVA 0x014C2390), is `movb $0x1, 0x30(%rcx)`. `USTDifficulty` keeps no copy
of it, and there is no native getter for it.

**Native readers: none.**
- Every typed row lookup goes through `FSTDifficultyDataTableRow::StaticStruct` (RVA 0x05051EC0). In all of
  `.text`, the only call to it is in the `FindRow<FSTDifficultyDataTableRow>` instance at VA 0x14510e950
  (`callq` at 0x14510e977). The only other reference is `StaticStruct`'s own lea of `Z_Construct_UScriptStruct_...`.
- The only call to that `FindRow` instance in all of `.text` is `USTDifficulty::LoadSettings` (RVA 0x0512C490),
  `callq` at 0x14512c58e.
- LoadSettings keeps the row pointer only in a register and does not store it. From the row, it reads:
  - SpawnTable (+0x8/+0x10), into USTDifficulty +0x50;
  - the curves and DamageModifiers, into +0x58/+0x60/+0x68;
  - the three floats (+0x34/+0x38/+0x3c), into +0x40/+0x44/+0x48.

  It never touches +0x30.
- The exe has no string for a lookup by name: the name appears only once, as the UTF-8 reflection param name, with
  no wide-string copy.

**Blueprint readers: none.** I pulled the name maps (package headers) of 8385 packages from pakchunk0 (every
.uasset/.umap outside the Textures/Meshes/Materials/Audio/Animations/FX/Fonts/Movies folders):
- all of them at a 24 KB head;
- the 2946 that hit that limit again at 256 KB.

`ShouldSpawnExtra` appears in none of them. A Blueprint that read the field (Break Struct, Get Data Table Row) would
have both the property name and `STDifficultyDataTableRow` in its name map. The only package that names
`STDifficultyDataTableRow` is `DT_DifficultyDataTable` itself, which holds the values. The one limit: 524 packages
are longer than 256 KB and were not read past that point. They are large levels and assets, and a name map that
size would be unusual.

**What it spawns: nothing.** Nothing reads it, so it adds no spawners, no lists and no second wave. It looks like a
leftover design switch. The real Hard/Extreme differences are what card 114 already lists: the wave budget
multipliers on each wave (`WaveBudgetMultiplier_Hard/_Extreme`), the SpawnTable, the damage presets and the
ammo/heal/fedscore floats. The wave budget multipliers apply inside `FactorDifficultyIntoWaveBudget`, so card 111
scales them for the session count at every difficulty.

**Run: not done.** The game itself never reads the flag, so a Hard run with spawns logged cannot show what it adds.
The spawn counts would only show the Hard budget multipliers, which card 111 already covers. The runner and mod also
have no way to pick Hard; adding one would mean a new hook just for this run. If you still want the run, the cheap
form is a two-player C2M1 run with `bShouldSpawnExtraEnemies = true` set on the Medium row in
`UBWCoop-Difficulty.ini` (card 114 writes it into the row), compared with a baseline. Say so and I will run it.

Run lines: none. Run length: none. rc: n/a. ERROR/FATAL: n/a. Dumps: none new. DLL: `17a60609748a4770`
(unchanged; nothing built).

Report complete.
