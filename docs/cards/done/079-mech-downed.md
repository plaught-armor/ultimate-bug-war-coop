# Card 079: Mech pawns and card 043: a mech is destroyed natively, not Downed; a Downed or Out pawn cannot enter a mech

Status: sent 2026-11-05 13:36 to junior ultimate-bug-war-coop-bb, after card 078 was accepted. Drafted 2026-11-05 (senior).

Read the card 076 Report ("Findings for the senior" 1 and 2), the card 043 Report and code (`c43_death` ~16442, `c43_enter_down`, `c43_health_pre` / `c43_health_post`, `c43_entries_step`), and the card 075 `EnterMech` and `MechEnter` code. Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why

Two bugs from the card 076 runs:

1. **A mech is Downed.** In c76f the client's mech server copy (`BP_BaseMech_C`, slot 1) went to Health 1.0 of 400 and card 043 put it in `Downed` with a 30 s bleedout. This happened in `HandleDeathEvent`, which was skipped. Card 043 sees the PC's possessed pawn, and while a player pilots a mech that pawn is the mech. The native mech destruction never runs, and the "downed" mech cannot be revived the way a soldier can.
2. **An Out pawn enters a mech and leaves Out.** c76e host log:
   - Line 2376: `BP_PlayerCharacterMI_C (host-own, slot 0) -> Out`.
   - Line 2388: `EnterMech` on that pawn.
   - Line 2398: `card043: host entry 229352 (Out) left: pawn gone`.
   
   The Out player escapes card 043 by entering a mech. A Downed player can likely do the same. Neither should be able to interact at all.

## Design

1. **Find the native path first, log only.** In a solo run (`SOLO=1`), card 043 is off because it needs 2 players. Let a mech be destroyed there, with `UBWCOOP_C43_TEST` unset, and log what happens:
   - the mech's `HandleDeathEvent` / `StartDeath`, or the mech's own destruction function;
   - whether the pilot is ejected as a soldier (which pawn the PC possesses afterwards, and its Health) or dies.
   
   A way to destroy the mech fast is fine, for example a test-only damage call behind a flag. The Report says which way was used.
2. **A mech is not a card 043 player.** In `c43_death`, a pawn that `is_a(g_mech_class())` returns false, so the native death runs. Check every other card 043 entry point that takes the possessed pawn:
   - `c43_skip_start_death`;
   - the health hooks;
   - the roster's up count (a pilot in a mech counts as up);
   - the all-down check.
   
   A mech must never enter the `C43Entry` table.
3. **After the native destruction**, the pilot follows the native result:
   - If the native result ejects the pilot, card 043 then applies to the soldier as usual.
   - If the native result kills the pilot outright, card 043 handles that death like any other player death (Downed, or Out when nobody is up). This holds only if the pilot's own `HandleDeathEvent` runs. Check that it does.
   - For a client pilot, the client must end up controlling the right pawn, through the card 075 leave path or the native possession. Show it in the log.
4. **No entering while Downed or Out.** On the host, refuse the mech enter for a pawn whose card 043 state is not `Up`. This covers both the host's own `EnterMech` and the client's `ChanOp::MechEnter` relay. Refuse it at the narrowest point that covers both (the `EnterMech` detour if both go through it). Log one line per refusal.
   - Also check the use press: if a Downed pawn can start any other interaction (turret, pickup), report it. Do not fix it in this card.
5. This runs in every co-op session. Only the step 1 log is behind a flag (`UBWCOOP_C79_LOG=1`).

## Do

1. Implement the above.
2. **Proof run 1 (co-op, client pilot):** the client enters a pod mech, and its mech is destroyed (by bugs or the test damage call).
   - The host log shows no `-> Downed` for `BP_BaseMech_C`.
   - The native destruction runs, and the pilot ends as the native path says.
   - Both sides take a shot.
3. **Proof run 2 (co-op, the host Out):** the host is downed and goes Out with `UBWCOOP_C43_TEST=1`, then presses E at a mech. The log shows the refusal, and the card 043 entry stays.
   - Do the same for the client Downed, sending `MechEnter`.
4. Run `c75-solo` and `c76-solo`: no change.

## Done when

- No `BP_BaseMech_C ... -> Downed` or `-> Out` line in any run, and the native mech destruction runs in co-op.
- A Downed or Out pawn cannot enter a mech, on either side, and its card 043 entry is kept.
- `logsum` gives ERROR/FATAL 0, both games end with rc 0, and there is no new dump.
- With `UBWCOOP_C79_LOG` unset, no card 079 log detour is installed.
- fnlen reports no function over 60 lines.

## Report

Done 2026-11-05 (junior). DLL `e8c631badf6b3ef6` (sha256 prefix), deployed by c79d. Finished runs c79a to c79i; logs in `logs/c79?-*`, shots in `Saved/Screenshots/Windows/<role>-c79?-*.png`.

### Step 1: the native mech destruction (solo, card 043 off)

Run c79c (`c79-solo.txt`, `SOLO=1`, `UBWCOOP_C79_LOG=1 UBWCOOP_C79_KILL=1`). The host enters a pod mech. 8 s later the `UBWCOOP_C79_KILL` test driver calls `USTCombatUtilityLibrary::DealDamage` on the piloted mech, once a second, for 10x its MaxHealth.

- **Damage:** the first two hits come from the nearest bug with the modifiers applied, and do nothing to the mech (c79b: five such hits, Health 400.0 stays 400.0). The driver now switches to the mech as causer with `ignore_modifiers` from hit 3, like the card 043 test's self hits. Hit 3 takes Health 360.0 to -3640.0.
- **Native chain** (c79c host lines 1190-1611):
  1. `ASTPlayerCharacter::HandleDeathEvent` on the mech (the mech has no own one). `c43_death` now returns false for a mech, so the original runs.
  2. `ASTPlayerMechBase::StartDeath` calls `StartSelfDestruction`, which sets the mech's Health to 1.0. The mech stays piloted and possessed.
  3. Hit 4 kills it again (Health 1.0 to -3999.0): a second `HandleDeathEvent`, `StartDeath` and `StartSelfDestruction`. The game handles this without trouble; a real fight would not hit the mech this way after the sequence starts.
  4. About 10 s after hit 3, `OnSelfDestructSequenceInitialized` ejects the pilot. In co-op this goes through `ASTPlayerMechBase::LeaveMech` (c79d host 1575-1580); the solo c79c log has no card 075 LeaveMech line because card 075 logs it only in a session.
  5. 3 s later `OnSelfDestructSequenceComplete` calls `DeathComplete`, and the mech explodes.
- **The pilot is ejected, not killed.** The PC holds the same soldier as before the enter (c79c: pawn 229351 before and after), with Health 100.0. The pilot's own `HandleDeathEvent` does not run. The explosion can hurt the ejected soldier, or kill it:
  - In c79d the client's soldier went from 100.0 to 50.0 at `DeathComplete`.
  - In c79i (solo) the soldier went from 96.9 to -15.0. That was an ordinary soldier death from explosion damage: its `HandleDeathEvent` ran (card 043 `gate off ... vanilla death`, since solo has 1 player), then the death screen.
  - In co-op, card 043 would take that soldier death as usual.

So Design 3 is the ejected case: card 043 applies to the soldier as usual, with no extra code. In c79d the ejected client soldier later took lethal damage from bugs, and card 043 handled it as a normal player (`-> Out (HandleDeathEvent skipped)`, all down because the host was already Out).

### What changed

- **`c43_death`:** a mech returns false at once, after one log line `card079: ... HandleDeathEvent on mech ... the game's own destruction runs` (always on, capped at 96 lines). This line is not behind the flag; it is how the proof shows the native path.
- **`c43_skip_start_death`:** a mech returns false (the old ServerCopy and ClientOwn skips would have blocked the mech's `StartDeath` on both sides).
- **`c43_enter_down`:** a guard: a mech is never put in the `C43Entry` table. It logs `WARN card079: host ... is a mech: not a card 043 player, not downed` and returns. The other callers are `c46_copy_out` (kill floor) and `c46_after_low` (test).
- **Other card 043 entry points**, checked and left as they are:
  - `c43_health_pre` / `c43_health_post` only look up the table, and a mech is never in it.
  - `c43_up_count` counts each roster pawn that is not in a down state, so a pilot in a mech counts as up. The all-down check uses the same count.
- **Mech enter refusal, `detour_c75_enter_mech`:** on the host, `c79_refuse_enter` refuses `EnterMech` for a pawn that `c43_is_down` (Downed, Out or Dead). One line per refusal: `card079: host ... EnterMech refused: <pawn> (<side>) is <state>; mech <mech> stays empty, card 043 entry kept (<n> entries)`. The host's own E reaches this detour (c76e line 2388). The client's `MechEnter` relay was already refused one step earlier by card 075's `c75_sender_copy` ("its copy is downed"). The `EnterMech` check is a second line of defence behind it.
- **Step 1 log, behind `UBWCOOP_C79_LOG=1`:** five log-only detours (`ASTPlayerMechBase::StartDeath`, `DeathComplete`, `StartSelfDestruction`, `OnSelfDestructSequenceInitialized`, `OnSelfDestructSequenceComplete`; new symbols in `tools/symbols.txt`), plus a per-slot "PC pawn changed" line. They are installed only when the flag is set.
- **Test drivers (host only), both new flags:**
  - `UBWCOOP_C79_KILL=1`: the test kill described above.
  - `UBWCOOP_C79_DOWN=host|client`: the first time that player's soldier is within 600 uu of a mech, `c43_enter_down(..., Downed)`. For `host` the bleedout is cut to 0.5 s, so the pawn goes Out.
- `fnlen`: `hooks.cpp` 1454 functions, `flags.cpp` 41 functions, 0 over 60 lines. Ninja builds with no warnings.

### Deviation: proof run 2 uses `UBWCOOP_C79_DOWN`, not `UBWCOOP_C43_TEST`

The card 043 test is a fixed stage machine that places both pawns with `c43_place` and has no point where one player could walk to a mech and press E. So I added the narrow `UBWCOOP_C79_DOWN` driver, which uses the real `c43_enter_down` and bleedout. Say if you want it folded into the card 043 test instead.

### Proof runs

All runs use `HOST_ENV` `UBWCOOP_AUTODROPSHIP=1 UBWCOOP_TRAVEL=/Game/Maps/FED/FED_C4M1?listen` plus the flags shown. The co-op runs also set `UBWCOOP_C79_LOG=1` on the client. "WARN" counts are host/client.

| Run | DLL | Route | Flags | Result |
|---|---|---|---|---|
| c79a | `7126d2aa` | `c79-solo.txt`, SOLO | `C79_LOG=1 C79_KILL=1` | **Host crash**, rc 3, new dump `UECC-Windows-1320E1484F0D6BC6BA16FD9789FD4E6B_0000` (13:37:32). Cause: new card 079 code, fixed (see below). |
| c79b | `bd238026` | `c79-solo.txt`, SOLO | `C79_LOG=1 C79_KILL=1` | rc 0, no dump, WARN 1: `test kill over`. Five bug hits with modifiers left Health at 400.0. This led to the own-cause hits. |
| c79c | `09307947` | `c79-solo.txt`, SOLO | `C79_LOG=1 C79_KILL=1` | rc 0, WARN 0, no dump. Step 1 native chain (above). |
| c79d | `e8c631ba` (final) | `c79-mech-kill.txt` (proof 1) | `C79_LOG=1 C79_KILL=1` | rc 0/0, WARN 0/1, no dump. The client WARN is the existing card034 refit line. The client's mech is destroyed natively: host `HandleDeathEvent` lines 1505 and 1517, `OnSelfDestructSequenceInitialized` 1574, `LeaveMech` 1575-1580. The host PC slot 1 pawn goes from the mech to soldier 231610 (server copy, Health 100). On the client: `AcknowledgePossession` of `BP_PlayerCharacterMI_C` at 1781, slot 0 back to soldier 69158 (client-own, Health 100), and MechLeave after at 1814. Shot `client-c79d-after` shows the client on foot with Morita MK1. |
| c79e | final | `c79-down-host.txt` (proof 2a) | `C79_LOG=1 C79_DOWN=host` | rc 0/0, WARN 0/0, no dump. The host soldier goes Downed 316 uu from the mech, then Out. Two E presses give two `EnterMech refused: BP_PlayerCharacterMI_C (host-own) is Out; mech BP_BaseMech_C stays empty, card 043 entry kept (1 entries)` lines and no "entry left" line. The shot shows the soldier on foot next to the empty mech. |
| c79f | final | `c79-down-client.txt` (proof 2b) | `C79_LOG=1 C79_DOWN=client` | rc 0/0, no dump. Host WARN 1 is the expected `card075: host MechEnter from BP_PlayerController_C ignored: its copy is downed`. The client stays on its soldier (shot). Later the host pawn died to bugs: "all down", which is normal card 043 behaviour. |
| c79g | final | `c75-solo.txt`, SOLO (regression) | none | rc 0, WARN 0, no dump. No `hooks: card 079` line. The three `config: UBWCOOP_C79_* unset` lines are present. The unique-text diff against c78e shows only those three config lines, the shot names, and two gore-part lines (random gibs). |
| c79h | final | `c76-solo.txt`, SOLO (regression) | `C76_LOG=1` | rc 0, WARN 0, no dump. No `hooks: card 079` line. The unique-text diff against c76s2 shows only the three config lines plus combat variance (which bugs died and how, gore parts, melee DoAction lines). No card075 or card076 line text differs. |
| c79i | final | `c79-solo.txt`, SOLO (c79a's exact route and flags, on the final DLL) | `C79_LOG=1 C79_KILL=1` | rc 0, WARN 0, ERROR/FATAL 0, **no new dump**. Passes travel and the dropship, and the whole step 1 chain repeats (`HandleDeathEvent` on mech twice, lines 1192 and 1200). Here the explosion killed the ejected solo pilot (96.9 to -15.0 at `OnSelfDestructSequenceComplete`). Card 043's gate is off in solo (`gate off ... vanilla death`, line 1230), so the vanilla death screen followed (line 1249). |

### The c79a crash

- **Which build and flags:** DLL `7126d2aa`, the first card 079 build. Flags `UBWCOOP_C79_LOG=1 UBWCOOP_C79_KILL=1`, route `c79-solo.txt`. Neither driver had acted yet: the log ends at `movieskip: done`, before travel finished, with no EnterMech.
- **The dump:** `EXCEPTION_ACCESS_VIOLATION` reading 0x131, thread 360 (the game thread). The walked stack is ntdll only, but the exception context gives RIP `0x6FFFF27CB47F` = `dwmapi.dll+0x14B47F`, which is the mod DLL. r13 = `0x0FA21C00`.
- **What crashed:** the mod has no PDB, so I disassembled the deployed `7126d2aa` dwmapi.dll. That RIP is in `c79_watch_pawns` (the `C79_LOG` per-slot "PC pawn changed" logger), inlined `c79_health`. It read `+0x688` (HealthComponent) on the PC's pawn r13 = `0x0FA21C00`, which was the dropship pawn during auto-dropship travel. The dropship has no HealthComponent: `[+0x688]` = 1, and the following field read hit 0x131.
- **So:** this was new card 079 code (log-only, behind `UBWCOOP_C79_LOG`), not a pre-existing crash.
- **The fix:** `c79_health` returns -1 unless the pawn `is_a` player character (soldier or mech). This is in every build from `bd238026` on.
- **Proof:** c79b and c79c passed the same travel with the same flags and no dump.

### Design 4: a Downed pawn can still start other interactions (report only, not fixed)

- **Host's own pawn:** `detour_interact` passes every E press to the native `UADInteractorComponent::Interact`. It calls `c43_interact_edge` first, but only for the revive hold. In c79e the Out host's E reached the native Interact with target `BP_BaseMech_C` (card020 line), and only the new `EnterMech` check stopped it. A mountable turret (`ASTMountableTurret::OnInteraction`), a pickup or a switch would run for a Downed or Out host.
- **Client:** `c50_relay_use` on the host relays a client's Use to its server copy (pickups, switches in the card 054 table) without a `c43_is_down` check. It skips only a press that started a revive hold. The client's own local Interact also runs natively.
- The HUD still shows "E ENTER M7" to a Downed or Out player (shots `host-c79e-again`, `client-c79f-refused`), because the native prompt does not know about card 043.

A single `c43_is_down` gate in `detour_interact` (host-own) and in `c50_relay_use` (server copy) would cover both. I did not add it.

### Edge to know

`c43_kill_host` (card 043 test) deals its damage to `r.p[0].pawn`. If the host pilots a mech, that is the mech, so the test would now start the mech's self-destruction instead of downing the host. Only the test is affected.

Report complete.

## Senior review (2026-11-05 14:06): accepted

- **First pass (13:57), sent back.** The Report still had the `RUNS_TABLE` placeholder and did not mention the c79a dump. Both are now fixed.
- **Runs.** I checked these myself:
  - Every drive from c79b to c79i ends with `scenario passed`, and logsum gives ERROR/FATAL 0.
  - In c79g and c79h, nothing from card 079 is logged, so its detours stay uninstalled when the flag is unset.
  - The newest dump is the c79a one (13:37:32). c79i, run on the final DLL with c79a's route and flags, wrote no new dump.
  - The deployed DLL matches `build/dwmapi.dll` (e8c631badf6b3ef6).
- **c79a crash accepted.** Cause: the log-only `c79_health` read the HealthComponent of the dropship pawn. The fix (`is_a` player character) is at `hooks.cpp` ~26962. The card 043 death path never caused the crash.
- **Behaviour.**
  - No mech goes Downed or Out in any c79 log.
  - The host's Out refusal shows twice in c79e, with the entry kept.
  - The client's Downed `MechEnter` is refused by card 075's `c75_sender_copy` (c79f).
  - The native chain runs in co-op (c79d): `HandleDeathEvent`, `StartSelfDestruction`, then the ejection.
- **`UBWCOOP_C79_DOWN` deviation accepted.** Leave it as its own driver.
- **fnlen.** `hooks.cpp` 1454 functions, 0 over 60.
- **Follow-ups (card 081):**
  - Design 4 finding: a Downed or Out pawn can still interact, both as the host's own pawn through `detour_interact` and through the client relay `c50_relay_use`. The "E ENTER" prompt also still shows.
  - The `c43_kill_host` edge: the test damages the mech when the host pilots one.
