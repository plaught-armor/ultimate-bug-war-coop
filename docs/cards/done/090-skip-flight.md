# Card 090: `UBWCOOP_SKIP_FLIGHT`: skip the FED_C1M1 transport dropship flight in test runs

Status: sent 2026-11-05 18:00 (senior). This is a test switch only.

Read:

- the card 015 autodropship block (`// --- UBWCOOP_AUTODROPSHIP (card 015) ---`, in `ubw/dropship.cpp`);
- the dropship detours `detour_skip_flight` and `detour_set_state`, and `log_dropship_call`;
- `CallSpec` in `ue/hooktable.h`; game call rows go in `kCallsUbw` (`ubw/hooks_ubw.cpp`) unless the target is a `ue/` slot.

Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

Every FED_C1M1 route spends about 40 s riding `BP_PlayerTransportDropship_VerySlow_C` from the spline start to the landing zone before `pawn=host-own`. Most routes do not test the ride; they only need both troopers on the ground. The game has `ASTProceduralDropship::SkipFlight(bool to_landing)` (RVA `0x051DDB90`, `offsets::ASTProceduralDropship_SkipFlight`). Today it is only logged by the diag detour.

## Do

1. **Flag.** `UBWCOOP_SKIP_FLIGHT=1`, read in `ubw/flags.cpp` next to `UBWCOOP_AUTODROPSHIP`, with the same `config:` line style. It needs `UBWCOOP_AUTODROPSHIP=1`. If it is set without that flag, log a `WARN config:` line and leave it off.
2. **Call.** When autodropship logs `world unpaused after N action(s); dropship ... it ticks from here, done`, call `SkipFlight(dropship, true)` once on the host's (authority) dropship.
   - Call it through a function pointer that is resolved even when the diag gate is off: add a `kCalls` row, or use the detour's orig when the hook is installed. Do not change the diag detour's gate.
   - Log one line: `autodropship: SkipFlight(%p, to_landing 1): state %d -> %d`. Use the state byte that `log_dropship_call` already reads.
3. **If the skip lands the dropship but the pawns do not get off**, or the client's view breaks, stop. Report what each side's `dropship-watch` and `collision:` lines show. Do not add workarounds this card.
4. **Runner.** `tools/run_pair.sh` gets `FAST=1`, which adds `UBWCOOP_SKIP_FLIGHT=1` to the host env. Document it in the header with the other switches.

## Proof

- Build with no warnings, and fnlen 0 over 60.
- **Routes, each with `FAST=1`:**
  - `host-morita` (`DEPLOY=1`);
  - `client-zipline`;
  - `c56-rejoin`.

  Each must pass, with ERROR/FATAL 0 and no new crash dump.
- **The same routes without `FAST=1`** must still pass (`host-morita`, `client-zipline`). The flag is off by default.
- **Timing table:** for each FAST route, give the time from the `world unpaused` line to `pawn=host-own` and to `pawn=client-own`, against the newest non-FAST run of the same route.
- **Client check:** the client's log shows its own pawn on the ground (`pawn=client-own`) and no `WARN`/`ERROR` lines that the non-FAST run lacks. Take one client screenshot after landing (`client shot`) and name the file.

## Done when

- `FAST=1` routes pass and save at least 25 s each, or the Report shows why the skip does not work.
- The Report ends with "Report complete."

## Report

### Change

- `ubw/flags.cpp` / `flags.h`: `config::read_skip_flight(bool auto_dropship)` reads `UBWCOOP_SKIP_FLIGHT`. It logs `config: UBWCOOP_SKIP_FLIGHT unset -- transport dropship flies its spline`, or `config: UBWCOOP_SKIP_FLIGHT=1 -- host calls SkipFlight(dropship, true) once after the autodropship unpause`. A value other than "1" logs `WARN config: UBWCOOP_SKIP_FLIGHT not "1" ...`, and "1" without `UBWCOOP_AUTODROPSHIP=1` logs `WARN config: UBWCOOP_SKIP_FLIGHT=1 needs UBWCOOP_AUTODROPSHIP=1 -- off`. Both WARN cases leave it off. The flag is read first in `read_card_flags`, where `g_auto_dropship` is already set by `ubw::init`. The switch is documented in the `flags.h` env list.
- `ubw/hooks_ubw.cpp`: `g_skip_flight`, and a new first row of `kCallsUbw`: `{&offsets::ASTProceduralDropship_SkipFlight, &g_call_skip_flight}`. It resolves with the diag gate off; the diag detour row and its gate are unchanged. With `UBWCOOP_DIAG=1` the call goes through the installed detour, which logs its own `dropship:` line (not run this card).
- `ubw/dropship.cpp`: `auto_dropship_skip_flight(dropship)` is called from the `world unpaused ... it ticks from here, done` branch of `auto_dropship_step`, only when the pawn is the transport dropship and the flag is on. That branch runs once per map, on the host or standalone only (`g_auto_dropship_armed` needs `!g_client_session`), on the game thread. It logs `autodropship: SkipFlight(%p, to_landing 1): state %u -> %u` from `kDropshipState`, the byte `log_dropship_call` reads (`%u` like that function, not `%d`).
- `tools/run_pair.sh`: `FAST=1` adds `UBWCOOP_SKIP_FLIGHT=1` to the host env, documented in the header.
- Build: no warnings. fnlen: 1454 functions (`hooks.cpp` is gone; count over `ubw/*.cpp`), 0 over 60, +2 (`read_skip_flight`, `auto_dropship_skip_flight`). The `hooks:` summary and skip list are identical to c89a; the `config:` lines gain only the new line.

### Result

The skip works. In every FAST run the host log shows `SkipFlight(..., to_landing 1): state 0 -> 1`, and 0.06 to 0.1 s later `dropship-watch: t=10s PC pawn is now ... BP_PlayerCharacterMI_C; watch ends`. Without the skip the watch ends 33 to 34 s after the unpause. The pawns get off; nothing else is needed.

The `SkipFlight` line comes 0.8 to 1.3 s after the `world unpaused` line, in the same function call, so `SkipFlight` itself takes about 1 s on the game thread (probably streaming at the landing zone). No hitch shows in the logs beyond that.

### Runs (DLL 1ddc46f17e72752c, all rc 0, ERROR/FATAL 0, WARN 0 on both sides, no new crash dumps)

| run | lane | route | FAST | run length | host on foot after unpause |
|---|---|---|---|---|---|
| c90a | 1 | host-morita (DEPLOY=1) | 1 | 75 s | +1.0 s |
| c90f | 2 | host-morita | 1 | 114 s | +1.3 s |
| c90c | 1 | host-morita | 0 | 131 s | +33.1 s |
| c90j | 2 | host-morita | 0 | 114 s | +33.1 s |
| c90b | 1 | client-zipline | 1 | 137 s | +1.3 s |
| c90i | 2 | client-zipline | 1 | 113 s | +1.1 s |
| c90h | 1 | client-zipline | 0 | 124 s | +34.0 s |
| c90e | 2 | client-zipline | 0 | 158 s | +34.1 s |
| c90d | 2 | c56-rejoin (DEPLOY=1) | 1 | 137 s | +1.0 s |
| c90g | 1 | c56-rejoin | 1 | 166 s | +1.0 s |

### Timing table

Times come from the mod logs. Both games log `GetTickCount64`, so host and client times share one clock. "Host-own" is the host's `dropship-watch ... watch ends` line, the moment the PC pawn becomes the trooper; that is what the state file's `pawn=host-own` reports. The drive's `wait state` lines only resolve to its poll, and it waits for the host first, so its client time is never earlier than its host time.

| route | FAST run | unpause to host-own | unpause to client-own | newest non-FAST run | unpause to host-own | unpause to client-own | saved |
|---|---|---|---|---|---|---|---|
| host-morita | c90a (L1) | 1.0 s | before the unpause (-3.1 s) | c90c (L1) | 33.1 s | before the unpause (-3.1 s) | 32.1 s |
| host-morita | c90f (L2) | 1.3 s | before the unpause (-3.1 s) | c90j (L2) | 33.1 s | +2.8 s | 31.8 s |
| client-zipline | c90b (L1) | 1.3 s | +0.7 s | c90h (L1) | 34.0 s | before the unpause (-3.1 s) | 32.7 s |
| client-zipline | c90i (L2) | 1.1 s | before the unpause (-0.1 s) | c90e (L2) | 34.1 s | before the unpause (-3.1 s) | 33.0 s |
| c56-rejoin | c90d (L2) | 1.0 s | before the unpause (-3.1 s) | c88f (L2) | 34.1 s | before the unpause (-3.1 s) | 33.1 s |
| c56-rejoin | c90g (L1) | 1.0 s | before the unpause (-1.6 s) | c88f (L2) | 34.1 s | before the unpause (-3.1 s) | 33.1 s |

The client-own column is the client log's first `client-own` line. The client's PC owns its on-foot trooper by the time the host unpauses, in FAST and non-FAST runs alike, so the flight never gated `pawn=client-own`; only the host-own wait did. Each route saves 32 to 33 s per map load, over the 25 s the card asks for.

Whole-run lengths are noisier than this, because map load time swings by up to 25 s with two lanes loading at once. Examples: c90b took 51 s from launch to the first pause, against 31 s in c89b, so it ran longer in total than c89b (137 s against 125 s) despite the skip. The best paired totals are c90a 75 s against c89a 102 s, and c90d 137 s against c88f 175 s.

### Client check

- No client log in the six FAST runs has a `WARN` or `ERROR` line; the non-FAST runs have none either.
- Client `collision:` line counts: c90d 88, c88f 89; host 55 and 55.
- c90d and c90g (c56-rejoin) disconnect, rejoin and reach `pawn=client-own` again, as c88f does.
- Screenshot after landing: `prefix/drive_c/users/steamuser/AppData/Local/StarshipTroopers_UBW/Saved/Screenshots/Windows/client-c90b-zipline.png`. The client trooper is on foot at the zipline tower, with the HUD and objectives normal. Lane 2's are in `prefix2/`, for example `client-c90i-zipline.png`.

### Not run

- The two `WARN config:` paths: `UBWCOOP_SKIP_FLIGHT=1` without autodropship, and a value other than "1".
- `FAST=1` with `UBWCOOP_DIAG=1`.

Report complete.

## Senior review

Senior, 2026-11-05 18:14. **Accepted.**

- Checked: the ten drive logs (c90a-j) end with `scenario passed`, and logsum gives ERROR/FATAL 0 and WARN 0 on both sides of each. The newest crash dump in both prefixes is still c79a's. c90a's host log has the `config:` line and `SkipFlight(..., to_landing 1): state 0 -> 1`.
- The call site in `ubw/dropship.cpp` runs only in the host's "it ticks from here, done" branch, once per map. `g_call_skip_flight` is a `kCallsUbw` row, so an unresolved symbol stops install before the flag can be read. The diag detour is unchanged.
- I looked at the client screenshot `client-c90b-zipline.png`: the trooper is on foot at the zipline tower, and the HUD and objectives are normal.
- Result: 32-33 s saved per map load, on every route. `pawn=client-own` never waited on the flight; only `host-own` did.
- Not run: the two `WARN config:` paths and FAST with DIAG. These are small, so no follow-up card is needed.
- Note: the `SkipFlight` call takes about 1 s on the game thread (landing-zone streaming). That is acceptable for a test switch. `FAST=1` should be the default for routes that do not test the ride.
