# Card 109: A downed player can still shoot

Status: written 2026-11-06 (senior); sent and accepted 2026-11-06.

Read:

- card 043 Report (Downed: refused tags, ignored relayed Fire 1, own view);
- card 044 Report (the refused Fire/Walk and ignored Fire 1 checks);
- `docs/design/coop-channel-lobby-downed.md` line 64 (the old design: block fire while downed).

Repo root: `/path/to/ultimate-bug-war-coop`. DLL base: `a4fdd1e0a051c984` (card 108).

## Why

User decision (2026-11-06): a downed player can shoot ("Rico was downed in the movie and was able to shoot"). This
reverses the card 043 design. Walking and sprinting stay blocked, and interactions stay refused (card 081).

Today four places block fire while Downed:

1. `input_diag.cpp` (around line 1544): `AbilityInputTagPressed` refuses `InputTag.Fire` and `InputTag.Walk` on a
   downed pawn's ASCs.
2. `chan_ops.cpp` `c26_host_fire`: a downed client's relayed Fire 1 is not passed to `c24_relay`.
3. `death.cpp` `c43_own_view`: `c43_release_own_fire` on entering Downed (keep: it releases a held trigger once;
   the player presses again to shoot).
4. Enter Downed calls `CancelAllAbilities(nullptr)` on the weapon ASC (keep: cancels the active shot once).

## Do

1. Remove the Fire refusal from 1 (keep Walk) and the downed gate from 2. Keep 3 and 4. Reload and aim go through
   the same weapon ASC; check they work while Downed, and say in the Report which input tags are still refused.
2. Update the design doc line 64 and the card 043 note in `docs/STATE.md` (if it states "no fire while downed") to
   the new rule.
3. **Proof, one short FED_C2M1 run** (c106-fight route, client goes Downed): while Downed, the client fires a burst
   and reloads. The host log shows server-copy FireProjectile calls rising while Downed, the client's ammo falls,
   and a bug takes damage from the downed client if one is in front. The host's own Downed fire, if the run can
   down the host cheaply (`UBWCOOP_C79_DOWN=1`), in the same run; otherwise skip it. rc 0, ERROR/FATAL 0, no new
   dump.

Stop when the fix builds and that run shows it; the user will playtest.

## Done when

The Report has the change, the tags still refused, the run row (shots while Downed on the host, ammo on the
client), the run length, rc, ERROR/FATAL, dumps and the DLL hash. Ends with "Report complete."

## Report

DLL `c11271d7bda28b18` (`build/dwmapi-c109-c11271d7bda28b18.dll`).

### Change

1. `mod/src/ubw/input_diag.cpp` `c43_refuse_tag`: only `InputTag.Walk` is refused on a downed player's pawn or
   weapon ASC. `InputTag.Fire` is no longer refused. It still runs on both sides, so the client no longer blocks its
   own fire either.
2. `mod/src/ubw/chan_ops.cpp` `c26_host_fire`: a downed client's relayed Fire 1 now goes to `c24_relay`, like Fire 0.
   The card 044 line (`death.cpp` `c44_host_fire`) now says "press passed to the relay".
3. Kept as they were:
   - `c43_release_own_fire` on entering Downed;
   - `CancelAllAbilities` on the pawn and weapon ASCs.
4. Docs:
   - `docs/design/coop-channel-lobby-downed.md`, "Enter Downed": only sprint is blocked; fire, reload and aim work.
   - `docs/STATE.md` item 3a: one sentence with the new rule. STATE had no "no fire while downed" line to replace.
5. A test-only runner command, so the proof doesn't depend on bugs winning a fight (c109a waited 90 s and the client
   never went down):
   - `host down host|client` (`cmdfile.h/.cpp` `Op::Down`, `testrun.cpp` `c109_down`, `tools/drive.py`) calls card
     043's `c43_enter_down` for roster slot 0 or 1. That is the same entry a lethal hit takes.
   - It is host only, and refused when there is one player or the pawn is already down.

Inputs still refused while Downed:

- `InputTag.Walk` (sprint), in `c43_refuse_tag`.
- Interactions, via card 081: the interact trace is hidden, presses are refused, and no relayed Use goes to a down
  copy.

Reload and aim:

- No mod code refuses them. Reload goes through the weapon's own ability, and the run shows it working while Downed.
- Aim (`RightMouseButton`) was not pressed in the run. Only Walk is in the refusal list.

Scenario `tools/scenarios/c109-shoot.txt`:

1. The client warps 1500 uu short of a WarriorBug.
2. `host down client`.
3. While Downed: a 2 s burst, `R`, then a second 2 s burst.

### Run row

| Run | Map | Length | Host rc | Client rc | ERROR/FATAL | WARN | Dumps | Drive |
|---|---|---|---|---|---|---|---|---|
| c109b | C2M1 | 62 s | 0 | 0 | 0 / 0 | 0 / 0 | none | rc 0, scenario passed |

- **Downed.**
  - 46644175: `cmd: 3 down: slot 1`, then `card043: host … (server-copy, slot 1) -> Downed`.
  - 46644186: on the client, `own pawn Up -> Downed, 30 s bleedout`.
- **Shots while Downed on the host: pass.** In `card044: host … from downed …`, the server-copy FireProjectile count
  went:
  - 46644440, Fire 1 press passed: 0;
  - 46646440, Fire 0: 20;
  - 46650952, Fire 1: 20;
  - 46652955, Fire 0: 41.

  That is 41 relayed shots, all while Downed. There were no `card043: … refused InputTag.Fire` lines on either side.
- **Client ammo and reload while Downed: pass.**
  - The reserve was 240 (`AmmunitionType.Morita=240/240/240`).
  - The `R` reload at 46649916 ran the game's own `SetAmmoCount` (caller exe+0x524BF76 / exe+0x524BFB6). That left
    the reserve at 220.
  - The second burst fired from the refilled magazine. The client's own `FireProjectile` lines continue to 46652948.
- **Bug damage from the downed client: not shown.** The run had no damage trace, and the nearest bugs were about 1500
  uu away. The card asked for this only if a bug was in front.
- **Host's own Downed fire: skipped.** `UBWCOOP_C79_DOWN=host` needs a mech nearby. With the client down, `down host`
  would end in all-down.

Report complete.
