# Card 084: Split S5a and S6: the hook-table mechanism to `ue/hooktable.*`, the engine detours to `ue/net.*`

Status: sent 2026-11-05 15:45 to junior ultimate-bug-war-coop-bb, after card 083 was accepted. Drafted 2026-11-05 (senior).

**The 19:00 batch.** The user wants the split finished by 19:00, so cards 084-089 are batched and have lighter proof: the base routes plus one route per group, and no baseline runs. A log check compares against the previous card's run of the same route. This card is S5a plus S6.

S5 in `docs/split-map.md` has two parts that need different preconditions, so it is split into two cards:

- **S5a (this card)** moves the generic machinery: PE checks, symbol resolution, MinHook install, and the install gate, all working over spans. The tables stay in `hooks.cpp`.
- **S5b (later)** splits the tables (`kHooksUe`/`kHooksUbw`, `kCallsUe`/`kCallsUbw`), adds `InstallSet` and `ubw::init`, and moves `read_card_flags` and `set_hook_gates`. It needs the F cards to export the globals that `read_card_flags` writes (S2 "Map changes" in card 078), so it waits for them.

Read the card 080, 082 and 083 Reviews, the S5 row and the `prologue`, `hookspec` and `installer` rows of `docs/split-map.md`, and `hooks.cpp` lines 1-125 and ~27410-28135. Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

The install code is the last generic part of `hooks.cpp`'s prologue and epilogue. Once it is in `ue/`, S5b only has to cut the tables in two and pass both spans in.

## Scope: `ue/hooktable.{h,cpp}`, namespace `ue::hooktable`

1. **The PE helpers:**
   - `nt_headers`, `CvInfoPdb70`/`kRsds` and `code_address`;
   - `image_matches_pdb`, changed to take the identity as parameters: `image_matches_pdb(base, nt, const uint8_t (&guid)[16], uint32_t age)`. `hooks.cpp` passes `offsets::kPdbGuid` and `offsets::kPdbAge`, so `ue/` does not include `offsets.generated.h` for the identity (the map's "PDB identity passed in").
   - `HookSpec`, `CallSpec` and `code_address` keep using `offsets::Symbol`. It is a two-field struct (`name`, `rva`) defined in `offsets.generated.h`, so `ue/hooktable.h` includes that header. The generated header is PDB data rather than mod logic, so this is fine. Do not add a second `Symbol` type.
2. **Install:**
   - `create_and_enable`;
   - `HookSpec` and `CallSpec`, unchanged in layout;
   - `hook_gated_off`;
   - `resolve_all(base, nt, std::span<const HookSpec>, std::span<const CallSpec>, std::span<void*> targets)`;
   - `install_hooks(std::span<const HookSpec>, std::span<void* const> targets)`;
   - `log_skipped_hooks`, taking the span.
3. **Base address:** `g_exe_base` and `exe_rva`. `install()` still sets `g_exe_base` at the same point as today.
4. **Stays in `hooks.cpp`:** `kHooks`, `kCalls`, every `g_gate_*`, `set_hook_gates`, `read_card_flags`, `install()` itself (it now calls the `ue::hooktable` functions with the spans), the site resolvers (`resolve_setup_input_sites` and the others), `install_card_logs` and `caller_chain`.


## Part 2 (S6): `ue/net.{h,cpp}`, namespace `ue::net`

The five engine detours (`detour_game_engine_init`, `detour_tick`, `detour_browse`, `detour_load_map`, `detour_listen`, hooks.cpp ~380-510) call card code directly. `ue/` must not call card code. So they call through a step table that `hooks.cpp` fills:

```cpp
struct EngineSteps {
    void (*init_return)();                          // after the original Init returns
    void (*pre_tick)(void* engine);                 // before the original Tick
    void (*post_tick)(void* engine);                // after it
    void (*browse_enter)(void* engine, void* url);
    void (*browse_return)(int result);
    void (*load_map_enter)(void* engine, void* pending);
    void (*load_map_return)(bool ok, void* pending);
    void (*listen_return)(bool ok);
};
void set_engine_steps(const EngineSteps& steps); // once, in install(), before the hooks are enabled
```

1. **The detours, their typedefs and `g_orig_*` go to `ue/net`.** Each one keeps its own engine log lines, word for word (`UGameEngine::Init fired ...`, `UEngine::Browse enter ...`, `UEngine::LoadMap ...`, `UWorld::Listen ...`), plus `browse_result_name` and `log_fstring`. The card calls in each body are replaced by one call through the table, at the same point.
2. **The card bodies stay in `hooks.cpp`** as the functions the table points to, holding today's calls in today's order:
   - `ubw_init_return`: the c64 and c21 calls;
   - `ubw_pre_tick`: `autoinput` and c63;
   - `ubw_post_tick`: every armed step and `cNN_step`, then `card_log_steps`;
   - `ubw_browse_enter` / `ubw_browse_return`: c45;
   - `ubw_load_map_enter`: `clear_pending_starting_mappings`, `g_last_hook`, `autoinput::load_map_enter` and `client_diag_load_map_enter`. These run before the `LoadMap enter` line today, so they must still run before it;
   - `ubw_load_map_return`: `client_diag`, c45 and c52;
   - `ubw_listen_return`: c52.
   
   The table rows in `kHooks` still name the detours, now through `extern`s.
3. **Net helpers to `ue/net`:**
   - `ActorNetModeFn` / `g_actor_net_mode`, `net_mode`, the `kNetMode*` values, and `kRole*` / `kActorRemoteRole` / `kActorLocalRole`;
   - `GetPlayerControllerFn` and its pointer;
   - `GetGameWorldFn` / `g_get_game_world`, `SetClientTravelFn` / `g_set_client_travel` and `kTravelAbsolute`.
   
   `SprintSide` / `sprint_side`, the auto-travel state (`g_travel`, `travel_step`) and anything else that reads `g_client_session` or logs a card name stay in `hooks.cpp`. List them in the Report.
4. **A null step.** A step that is null is skipped. The detours check each pointer, which costs one branch each.

## Log text: the one allowed change

`log_skipped_hooks` writes `hooks: card 047: %u of %zu hooks not installed (audit flag off), %zu installed` and `hooks: card 047 skipped: %s`. `ue/` must not name a card. Change them to:

- `hooks: %u of %zu hooks not installed (gate off), %zu installed`
- `hooks: skipped: %s`

No tool parses these lines: `logsum.py` and the scenarios do not match them (I checked; the only "card 047" hit in `tools/` is a comment in `host-morita.txt`). Grep again before you change them, and list any other hit in the Report. Every other line (`FATAL hooks: ...`, `ERROR hooks: MH_...`, `hooks: %s hooked at %p`, `hooks: exe base ...`, `hooks: build identity OK ...`) keeps its exact text.

## Rules

1. `ue/core.h` includes `ue/hooktable.h`. `hooks.cpp` gets `using namespace ue::hooktable;` next to the others. No `using namespace` goes in a header.
2. Do not reorder `kHooks` or `kCalls`, and keep the install order: resolve all, read flags, set gates, MinHook init, card logs, install.
3. `std::span` is fine (C++20 is set in CMake). Pass `kHooks` and `kCalls` as `std::span{kHooks}`. The `targets` array in `install()` keeps its size from `std::size(kHooks)`.
4. No behaviour change except the two log texts.

## Proof

- **Build:** `systemd-run --user --scope -q -p MemoryMax=12G ninja -C build -j4`, with no warnings.
- **fnlen:** run it on `hooks.cpp` and `ue/hooktable.cpp`. No function may be over 60 lines.
- **Routes:**
  - the base routes `host-morita`, `client-zipline`, `c47-gates` and `c75-solo` (`DEPLOY=1` on the first run);
  - `c47-bug`;
  - one `c75-solo` run with `UBWCOOP_DIAG=1`, so every diag row is installed;
  - for S6: `c66-late-join` and `c56-rejoin` (join, restart and travel).
- **Log check.** There are no baseline runs. Compare the new `c75-solo` run, with the default flags, against the card 083 `c75-solo` run.
  - First strip the timestamp prefix and the `@ 0x...`/`at 0x...` addresses.
  - The `hooks:` lines and the `UGameEngine`/`UEngine`/`UWorld` lines must match in order, except for the two renamed texts.
  - The Report shows the command and its output.
- `logsum` gives ERROR/FATAL 0, rc 0, and there is no new dump.

## Done when

- `ue/hooktable.*` and `ue/net.*` exist, and `ue/core.h` includes both headers.
- `grep -n 'c[0-9][0-9]\|card\|offsets::kPdb' mod/src/ue/hooktable.* mod/src/ue/net.*` finds nothing.
- `grep -n '^bool resolve_all\|^bool install_hooks\|^void\* code_address\|^bool create_and_enable' mod/src/hooks.cpp` finds nothing.
- Every route passes, and the `hooks:` diff is empty apart from the two renamed lines.
- The Report gives the `hooks.cpp` line counts before and after.
- The Report ends with "Report complete."

## Report

Junior, 2026-11-05 16:20.

### What moved

- **`ue/hooktable.{h,cpp}`** (54 + 175 lines), namespace `ue::hooktable`:
  - PE helpers `nt_headers`, `code_address`, and `image_matches_pdb(base, nt, const uint8_t (&guid)[16], uint32_t age)`. `CvInfoPdb70`, `kRsds` and `kMaxDebugEntries` are in an anonymous namespace in the .cpp.
  - `HookSpec`, `CallSpec`, `create_and_enable`, `hook_gated_off`.
  - `resolve_all(base, nt, span<const HookSpec>, span<const CallSpec>, span<void*>)`, `install_hooks(span<const HookSpec>, span<void* const>)`, `log_skipped_hooks(span<const HookSpec>, uint32_t)`.
  - `g_exe_base`, `exe_rva`.
- **`ue/net.{h,cpp}`** (77 + 95 lines), namespace `ue::net`:
  - `EngineSteps` (eight steps: `init_return`, `pre_tick`, `post_tick`, `browse_enter`, `browse_return`, `load_map_enter`, `load_map_return`, `listen_return`) and `set_engine_steps`.
  - The five detours, their typedefs and `g_orig_*` pointers. Every step is null-checked, so a null step is skipped. The engine log lines are kept word for word.
  - `browse_result_name`, public because card 045 in `hooks.cpp` uses it.
  - The net helpers: `ActorNetModeFn`/`g_actor_net_mode`, `net_mode`, the `kNetMode*` and `kRole*` constants, `kActorRemoteRole`/`kActorLocalRole`, `GetPlayerControllerFn`/`g_get_player_controller`, `GetGameWorldFn`/`g_get_game_world`, `SetClientTravelFn`/`g_set_client_travel`, `kTravelAbsolute`. `kRoleSimulatedProxy` came out of the card 028 constants block.
- **`hooks.cpp`:**
  - The card bodies became step functions at the old detour spot: `ubw_init_return`, `ubw_pre_tick`, `ubw_post_tick`, `ubw_load_map_enter`, `ubw_load_map_return`. Each calls the same functions in today's order.
  - The table is `constexpr EngineSteps kEngineSteps` (`hooks.cpp:398`). `install()` calls `set_engine_steps(kEngineSteps)` before `MH_Initialize`, so the install order is unchanged: resolve all, read flags, set gates, steps, MinHook init, card logs, install.
  - `targets` is `void* targets[std::size(kHooks)]`. The tables are passed as `std::span{kHooks}` and `std::span{kCalls}`. `kHooks` and `kCalls` are not reordered.
- **`core.h`** includes `ue/hooktable.h` and `ue/net.h`. `hooks.cpp` has `using namespace ue::hooktable;` and `using namespace ue::net;`. **`CMakeLists.txt`** gains the two .cpp files.

### Deviations and notes

1. `browse_enter`, `browse_return` and `listen_return` point straight at `c45_browse_enter`, `c45_browse_return` and `c52_listen`, which already have the step signatures, so there are no wrappers.
2. `HookSpec` and `CallSpec` use `const ubw::offsets::Symbol*`. The `ue/hooktable.h` header includes `offsets.generated.h`, whose namespace is `ubw::offsets`. That is the only `ubw` name in `ue/`. The grep below does not match it, because it looks for `offsets::kPdb`.
3. Added precondition: `resolve_all` and `install_hooks` first check `targets.size() == hooks.size()`. If the sizes differ, they log `FATAL hooks: %zu targets for %zu hooks` and return false. It cannot fire today, because `targets` is sized from `kHooks`.
4. `net.cpp` uses `ue::reflect::log_fstring`, which already lived in `ue/reflect`. Nothing was moved for it.
5. Kept in `hooks.cpp`:
   - `SprintSide`/`sprint_side`, `g_travel`, `g_travel_armed`, `g_world_seen*`, `travel_step`;
   - `is_net_client` (10 uses, reads card state);
   - `kHooks`, `kCalls`, the `g_gate_*` flags, `set_hook_gates`, `read_card_flags`, `install()`, the site resolvers, `install_card_logs`, `caller_chain`.
6. Grep for the renamed texts before the change found:
   - `tools/drive.py:48` `SKIP_RE` matches `skipped:`, but only in `WARN cmd: ... skipped:` lines, which are unrelated;
   - `tools/scenarios/host-morita.txt:14` names "card 047" in a comment;
   - `mod/src/ubw/flags.{h,cpp}` has "card 047" in `read_diag` texts, which are unchanged.

   None of these parse the two lines.

### Proof

- **Build:** `systemd-run --user --scope -q -p MemoryMax=12G ninja -C build -j4` is clean, with no warnings. `dwmapi.dll` sha256 starts `8e555892c9b627f1`, and every run deployed that hash.
- **`hooks.cpp` line count:** 27895 before, 27689 after.
- **fnlen:**

  | File | Functions | Over 60 lines |
  |---|---|---|
  | `hooks.cpp` | 1414 | 0 |
  | `ue/hooktable.cpp` | 9 | 0 |
  | `ue/net.cpp` | 8 | 0 |

  `install()` came out at 62 lines until the step table moved to `kEngineSteps`.
- **Done-when greps:** the `c[0-9][0-9]\|card\|offsets::kPdb` grep on `ue/hooktable.*` and `ue/net.*` gives rc 1. The `^bool resolve_all|...` grep on `hooks.cpp` gives rc 1.

**Routes** (`run65.sh`; logsum lines from each `.out`):

| Tag | Route | Result |
|---|---|---|
| c84c | host-morita, DEPLOY=1 | 109 s, drive rc 0, host/client ERROR/FATAL 0, no dumps |
| c84d | client-zipline | 128 s, drive rc 0, ERROR/FATAL 0, no dumps |
| c84e | c47-gates (C43_TEST) | 187 s, drive rc 0, ERROR/FATAL 0, no dumps; client WARN 1 `card044: client no CompleteDeath` (also in c80e and c83e) |
| c84f | c75-solo, default flags | 81 s, drive rc 0, ERROR/FATAL 0, no dumps |
| c84g | c47-bug (ENEMYAUDIT, SPAWN_BUG) | 291 s, drive rc 0, ERROR/FATAL 0, no dumps; host WARN 1 `card039: host probe table full (32)` (also in card047b/u/v and others) |
| c84h | c75-solo, `UBWCOOP_DIAG=1` | 108 s, drive rc 0, ERROR/FATAL 0, no dumps; `hooks: 28 of 226 hooks not installed (gate off), 198 installed` (default run: 115 of 226 not installed, 111 installed); diag rows log (`input:`, `jump:`, `dropship:`, card017/019/022 lines up) |
| c84i | c66-late-join (C66_CAP=64, TRAVEL_DELAY_MS=360000) | 409 s, drive rc 0, ERROR/FATAL 0, no dumps; host WARN 1 `autodropship: 1 PlayerController(s)` (client not joined yet, expected) |
| c84j | c56-rejoin | 181 s, drive rc 0, ERROR/FATAL 0, WARN 0, no dumps; client map loaded 5 (rejoin) |

c84h ended with the runner line `WARN: wineserver -k left game processes; sending SIGKILL`. That comes from the runner's teardown, after the scenario passed, and is not a game log line.

**Log check, c84f against c83f** (`$S/c84diff.sh`). It greps `hooks:|UGameEngine|UEngine::|UWorld::` and strips three things:

- the `[ms][T]` prefix;
- `@`/`at` addresses;
- `=<16 hex>` values.

The third strip is extra: the `this=`/`ctx=` heap pointers in `UGameEngine::Init fired` and `UEngine::Browse/LoadMap enter` change every run. Without it, those 5 lines differ on the pointer only. The text is the same.

```
n() { grep -E 'hooks:|UGameEngine|UEngine::|UWorld::' "$1" | sed -E 's/^(\[[^]]*\])+ *//; s/(@|at) (0x)?[0-9a-fA-F]{8,}/\1 ADDR/g; s/=[0-9A-F]{16}/=PTR/g'; }
n c83f-host.log > c83f.ord; n c84f-host.log > c84f.ord
diff c83f.ord c84f.ord | grep '^<' | cut -c1-60 | sort | uniq -c
sed -E 's/^hooks: card 047: (.*) \(audit flag off\)/hooks: \1 (gate off)/; s/^hooks: card 047 skipped:/hooks: skipped:/' c83f.ord | diff - c84f.ord
```

Output:

```
133
133
== differing lines (c83f side), first 60 chars:
      1 < hooks: card 047: 115 of 226 hooks not installed (audit fla
      1 < hooks: card 047 skipped: ASTNonPlayableCharacterBase_TryPl
      1 < hooks: card 047 skipped: ASTProceduralDropship_SkipFlight,
      1 < hooks: card 047 skipped: UAbilitySystemComponent_ClientAct
      1 < hooks: card 047 skipped: UADWeaponInventory_execGiveWeapon
      1 < hooks: card 047 skipped: USTAttackComponent_OnAnimationEnd
      1 < hooks: card 047 skipped: USTWeaponSwitchAbility_ShouldAbil
      1 < hooks: card 047 skipped: UWorld_Listen, AController_SetIgn
== after mapping the two old texts to the new ones:
diff rc=0
```

Both files have 133 lines, in the same order. Only the count line and the 7 `skipped:` lines differ, all of them the two renamed texts. With the old texts mapped to the new ones, the diff is empty.

Backups of the pre-084 files are at `$S/hooks.c83.cpp`, `$S/core.c83.h` and `$S/CMakeLists.c83.txt`.

Report complete.

## Senior review

Senior, 2026-11-05 16:25. **Accepted.**

- Checked: the eight drive logs end with `scenario passed`, logsum gives ERROR/FATAL 0 for every run, and the newest crash dump is still c79a's. The `ue/` grep gives rc 1. `ue/net.cpp` null-checks every step, and `install()` calls `set_engine_steps(kEngineSteps)` just before `MH_Initialize`.
- The extra `=<16 hex>` strip in the log check is fine: those are heap pointers.
- Deviations 1-4 are accepted. `ubw::offsets::Symbol` in `ue/hooktable.h` is the one `ubw` name in `ue/`. Card 089 lists it as an explained hit.
- The c84h runner line `wineserver -k left game processes; sending SIGKILL` was caused by the senior's lane 2 test pair (L2a), which was running at the same time. The old runner's `pgrep`/`pkill` matched every game. The runner now scopes both to its own lane (see `LANE` in `tools/run_pair.sh`).
