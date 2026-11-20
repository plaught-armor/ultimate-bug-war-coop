# Card 078: Split S2: engine config readers to `ue/config`, card readers to `ubw/flags`

Status: sent 2026-11-05 13:21 to junior ultimate-bug-war-coop-bb, after card 077 was accepted. Drafted 2026-11-05 (senior). This is S2 in `docs/split-map.md` ("Move order"), with two changes to the map's S2 row, given under "Map changes" below.

Read `docs/split-map.md` ("Proposed files", "Move order", the `g_gate_diag` row and the `install` row), the card 077 Report and Senior review, `mod/src/config.h` and `mod/src/config.cpp`. Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why

`config.cpp` (631 lines) mixes two things:

- the environment primitives and the engine-generic settings (log tag, travel, diag switch);
- about 40 readers named after cards (`read_c28_shots` to `read_c76_log`, the card 030/031/032/049/052 structs, the grant weapon, autoinput).

`ue/` must not name a card or a UBW class, so the file splits in two. Later cards then add each new card switch to `ubw/flags.cpp` and leave `ue/config` alone.

## Map changes (senior)

- **`read_card_flags` stays in `hooks.cpp`.** It writes about 35 globals in the anonymous namespace of `hooks.cpp`. Moving it now would mean exporting all of them before the F cards move their owners. It moves in S5, as part of `ubw::init` (see the `install` row of the map).
- **`g_gate_diag` stays in `hooks.cpp`.** Its target, `ue/hooktable.cpp`, does not exist until S5. This card moves only its reader, `read_diag`.
- Update the S2 row of `docs/split-map.md` to say this, and add both items to the S5 row.

## Do

1. **`ue/config.h` and `ue/config.cpp`**, namespace `ue::config`:
   - The primitives, made public because `ubw/flags.cpp` uses them: `EnvStatus`, `EnvRead`, `read_env`, `parse_u32`, `read_delay_ms`, `read_switch`. `is_tag_char` stays internal.
   - The constants `kMaxTagLen`, `kMaxUrlLen`, `kDefaultDelayMs` and `kMaxDelayMs`, with `TagStatus`, `LogTag` and `Travel`.
   - The readers `read_log_tag`, `read_travel` and `read_diag`.
   - The header comment lists only the variables these readers read (`UBWCOOP_LOG_TAG`, `UBWCOOP_TRAVEL`, `UBWCOOP_TRAVEL_DELAY_MS`, `UBWCOOP_DIAG`).
2. **`ubw/flags.h` and `ubw/flags.cpp`**, namespace `ubw::config`, so the call sites in `hooks.cpp` and `dllmain.cpp` keep their `config::` and `ubw::config::` names:
   - Every other reader and its struct: `read_client_game_only`, `read_auto_dropship`, `AutoInput`, `GrantWeapon`, `C49Test`, `C52Test`, `KillPlayer`, `OverlayTest`, `SpawnMech`, `read_cmd`, `read_skip_movies`, `read_c66_cap` and all the `read_cNN_*` readers. Also `kMaxWeaponLen`.
   - The header comment keeps the rest of today's variable list, word for word.
   - They call the primitives as `ue::config::read_switch` and so on. One `using` line for each primitive at the top of `flags.cpp` is fine.
3. **Callers.**
   - `hooks.cpp` and `dllmain.cpp` include both headers.
   - `read_diag`, `read_log_tag` and `read_travel` calls become `ue::config::`. `Travel` and `LogTag` uses become `ue::config::Travel` and `ue::config::LogTag`, including the `install()` parameter.
   - Check every other file that includes `config.h` (grep first) and give it only the header it needs.
4. **CMake.** Replace `src/config.cpp` with `src/ue/config.cpp` and `src/ubw/flags.cpp` in the explicit source list. Delete `mod/src/config.h` and `config.cpp`.
5. **No behaviour change.**
   - Every log line keeps its exact text.
   - The order in which the readers run stays the same, because their log lines are the first lines of every game log.
   - Do not rename any reader, and do not merge or drop any.

## Proof

- **Build:** `systemd-run --user --scope -q -p MemoryMax=12G ninja -C build -j4`, with no warnings.
- **fnlen:** no function over 60 lines.
- **Routes:** the base routes `host-morita`, `client-zipline`, `c47-gates` and `c75-solo` (`DEPLOY=1` on the first run), plus `c47-bug`.
- **Log check:** diff the `config:` lines of the `host-morita` host and client logs, and of the `c47-gates` host log, against the card 077 runs of the same routes (c77d is client-zipline, c77e is c47-gates; for host-morita, run it once on the card 077 DLL before you change anything, because c77b failed and its scenario changed since). Strip the timestamps first. The lines must match, in the same order. The Report shows the command and its output.
- `logsum` gives ERROR/FATAL 0, rc 0, and there is no new dump.

## Done when

- `mod/src/config.*` no longer exist. `mod/src/ue/config.*` and `mod/src/ubw/flags.*` do.
- `grep -n 'c[0-9][0-9]\|card' mod/src/ue/config.*` finds no card name. Card numbers in comments copied from the old file also count as a hit: reword them, or move the reader to `ubw/flags`.
- All five routes pass, and the `config:` line diff is empty.
- `docs/split-map.md` S2 and S5 rows are updated as in "Map changes".

## Report

Done 2026-11-05 (junior). DLL `de5a11a144eaffa1` (sha256 prefix), deployed by c78b. The baseline c78a ran on the card 077 DLL `5c06bc142cac5fe4` before any change.

### What moved

- **`mod/src/ue/config.{h,cpp}`** (`ue::config`, 68 + 135 lines):
  - public primitives `EnvStatus`, `EnvRead`, `read_env`, `parse_u32`, `read_delay_ms`, `read_switch`;
  - the constants `kMaxTagLen`, `kMaxUrlLen`, `kDefaultDelayMs` and `kMaxDelayMs`, with `TagStatus`, `LogTag` and `Travel`;
  - the readers `read_log_tag` and `read_travel`.
  - The header comment lists `UBWCOOP_LOG_TAG`, `UBWCOOP_TRAVEL` and `UBWCOOP_TRAVEL_DELAY_MS`. The header includes `<windows.h>` for `DWORD`, because `EnvRead` is now public.
- **`mod/src/ubw/flags.{h,cpp}`** (`ubw::config`, 189 + 500 lines): every other reader and struct, plus `kMaxWeaponLen`. The function bodies are copied unchanged. The header keeps the rest of the variable list word for word, `UBWCOOP_DIAG` included. `flags.cpp` has one `using ue::config::X;` line for each primitive.
- **Callers:**
  - `dllmain.cpp` and `hooks.cpp` include both headers.
  - `hooks.h` includes `ubw/flags.h`, which includes `ue/config.h`.
  - `Travel`, `LogTag`, `TagStatus`, `kMaxTagLen`, `kMaxUrlLen`, `read_travel` and `read_log_tag` are now `ue::config::`. That includes the `install()` parameter in `hooks.h` and `hooks.cpp`, `g_travel`, and the card 063 tag and shot buffers.
  - No other file included `config.h` (grep).
- **CMake:** `src/config.cpp` became `src/ue/config.cpp` and `src/ubw/flags.cpp`, in the same place in the list. The old `mod/src/config.h` and `config.cpp` are deleted.
- `hooks.cpp` is 28064 lines; it was 28063, because there is one more include line.

### Two deviations from the card. Please check both.

1. **`read_diag` is in `ubw/flags`, not `ue/config`.** Its log texts name cards:
   - `"card 047 diagnostic hooks"`;
   - `"... card 038 and 048-050 and 057 audit logs"`.
   
   Keeping the exact text in `ue/config` would fail the `grep ... card` check. "Done when" allows moving the reader to `ubw/flags`, so I did that. The call site stays `config::read_diag()` in `read_card_flags`.
   - I noted this in the split-map S2 row and in the `g_gate_diag` row. S5 must pass the diag value into `ue::install`, because `ue` cannot call a `ubw` reader.
   - The alternative: a generic `ue::config::read_diag` that takes its two texts from the caller. Say if you want that instead.
2. **`is_tag_char` is public.** `read_grant_weapon` and `read_c49_test` in `ubw/flags.cpp` call it. The only other choice was a duplicate copy.

### Proof

- **Build:** `systemd-run --user --scope -q -p MemoryMax=12G ninja -C build -j4` after touching all four changed `.cpp` files: rc 0, 0 warnings.
- **fnlen:** `ue/config.cpp` 7/0, `ubw/flags.cpp` 38/0, `hooks.cpp` 1439/0.
- **No card names:** `grep -n 'c[0-9][0-9]\|card' mod/src/ue/config.*` gives no output (rc 1).
- **Routes:** all runs had FATAL/ERROR 0, rc 0 and no new dump.
  - c78a, host-morita baseline on the 077 DLL: passed in 105 s, WARN 0/0. This also proves the fixed scenario.
  - c78b, host-morita, `DEPLOY=1`: passed in 103 s, WARN 0/0.
  - c78c, client-zipline: passed in 123 s, WARN 0/0.
  - c78d, c47-gates: passed in 182 s. The client WARN 1 is the pre-existing `card044: client no CompleteDeath`.
  - c78e, c75-solo: passed in 77 s, WARN 0.
  - c78f, c47-bug: passed in 279 s, with 60 host `SpawnGoreResponse` lines.
    - The host WARN 1 is `card039: host probe table full (32)`. It is pre-existing; card039a, card057w/z, card059a and card062t logs have it too.
    - Gate lines: the host has 109 of 225 not installed with ENEMYAUDIT on; the client has 115 of 225.
- **`config:` line diff**, in order, with the timestamp prefix stripped by `grep -o`:
  ```
  for p in "c78a-host c78b-host" "c78a-client c78b-client" "c77d-host c78c-host" "c77d-client c78c-client" "c77e-host c78d-host" "c77e-client c78d-client"; do
    set -- ${=p}; diff <(grep -o 'config: .*' logs/$1.log) <(grep -o 'config: .*' logs/$2.log) && echo "$1 vs $2 same $(grep -c 'config: ' logs/$2.log)"; done
  c78a-host vs c78b-host same 34
  c78a-client vs c78b-client same 34
  c77d-host vs c78c-host same 34
  c77d-client vs c78c-client same 34
  c77e-host vs c78d-host same 34
  c77e-client vs c78d-client same 34
  ```
  `diff` printed nothing for any pair.
- **`docs/split-map.md`:**
  - the S2 row is rewritten as above;
  - the S5 row adds "`read_card_flags` moves to `ubw/flags.cpp` as part of `ubw::init`" and "`g_gate_diag` moves to `ue/hooktable.cpp`";
  - the `g_gate_diag` gate row notes where `read_diag` now lives.

## Senior review (2026-11-05 13:35): accepted

- **Runs.** I checked c78a-c78f myself:
  - Every drive ends with `scenario passed`. logsum gives ERROR/FATAL 0 for every log. The only WARNs are the pre-existing card044 client line in c78d and the card039 host line in c78f.
  - The newest crash dump is still the 11:31 one from c75a12.
  - The deployed DLL matches `build/dwmapi.dll` (de5a11a144eaffa1). It was deployed at 13:15:17, after c78a ended (13:14:26, on the card 077 DLL).
  - c78a and c78b pass the fixed `host-morita` scenario with the default flags, so the card 077 scenario fix is proven.
- **Code.** `mod/src/config.*` are gone. CMake lists `src/ue/config.cpp` and `src/ubw/flags.cpp`. The `ue/config.*` card grep is empty. fnlen gives `ue/config.cpp` 7/0, `ubw/flags.cpp` 38/0 and `hooks.cpp` 1439/0. I re-ran the c77e/c78d host `config:` diff, and it is empty.
- **Deviation 1 accepted.** `read_diag` belongs in `ubw/flags`. Its switch names the card 047 gate, so it is a card switch, not an engine setting. S5 passes the value into `ue::install`, as the map now says. A generic `ue::config::read_diag` is not wanted.
- **Deviation 2 accepted.** `is_tag_char` is a generic character test, so making it public in `ue::config` is correct.
