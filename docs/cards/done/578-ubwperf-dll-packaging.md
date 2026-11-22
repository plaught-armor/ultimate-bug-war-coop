# Card 578: package the performance mod as its own `ubwperf.dll`

Umbrella: card 573 (Packaging section). User 2026-11-21 chose its own dll, loaded by the co-op dll. This card is
scoping only. The move happens after the card 573 measurement step, as card 573 says.

## What moves

The `perf_*.cpp` files (cards 531, 536, 538, 539, 544, 548, 577), the card 568 part of enemies.cpp, and the
card 576 spin cut. Anything that reads co-op state (`g_client_session`, the session flags) gets that state through
a small init call from the co-op dll, never through a link to co-op globals.

## Loading

1. **With the co-op mod:** `dwmapi.dll` calls `LoadLibraryW(L"ubwperf.dll")` from its own init, before hooks
   install, and passes `ubwperf_init(const UbwPerfHost*)`: side, session flags, the shared MinHook state (or
   ubwperf brings its own MinHook, decided at the move), and the log sink. If the dll is missing, nothing changes.
2. **Without the co-op mod (single player):** a second proxy name loads it. It must be an exe import other than
   dwmapi, so the exe import table was read (2026-11-21, llvm-objdump -p). Candidates:

   | Dll | Exe imports | Wine export count | Notes |
   |---|---|---|---|
   | `WINHTTP.dll` | 2 (WinHttpGetIEProxyConfigForCurrentUser, WinHttpGetDefaultProxyConfiguration) | 49 | Not on any frame path. Recommended. |
   | `DSOUND.dll` | 6, by ordinal | 14 | Small, but it is the audio path, and other audio code loads it too. |
   | `UxTheme.dll` | 1 (SetWindowTheme) | 138 | comctl32 and user32 load it too, so every export must forward. |

   Recommendation: `winhttp.dll` as the standalone proxy name, using `tools/gen_proxy_exports.py`'s pattern
   (exports read from the GE-Proton11-11 builtin, which is forwarded to the system copy), with override
   `WINEDLLOVERRIDES="winhttp=n,b"`. The proxy body is `ubwperf.dll` itself, so there is one binary under two
   names. When dwmapi has already loaded it as ubwperf, the winhttp copy sees the init flag and stays passive.
3. **Both present:** one copy only. The init is guarded by a named mutex or a shared section flag, decided at the
   move.

## Proof (at the move)

- Single player with only winhttp.dll: `ubwperf:` rows log, the card 531/536 counts match the in-co-op numbers in a
  matched window, and there are no new WARN lines.
- Co-op with both: one init row per process, and card 330 mod ms drops by the perf share (perf time leaves the
  co-op dll's total and shows on its own row).
- Card 536 mismatches stay at 0.

## Budget

Scoping is done (this doc). The move is one build and 2 runs (single player, co-op pair).

## Move plan (junior static trace, 2026-11-21, before code)

Coupling found in the perf files (1241 lines in 8 files):

- Helpers: `read_ptr`, `class_name`, `object_name`, `is_a`, `object_listed`, `log::line`, `c31_weak_*` and the hook
  table all live in `mod/src/ue/` (reflect, log, hooktable) or are small. Plan: build `mod/src/ue/` as a static lib
  that both dlls link. Engine pointers the perf files read from co-op init (`g_npc_class`, `g_pawn_class`,
  `g_weak_get`, `g_get_game_world`, `g_pc_console_command`, `g_memory_free`, `g_exe_base`) are resolved again by
  ubwperf from `offsets.generated.h` (same exe, same RVAs).
- Co-op state: `g_client_session` (c536, c538, c539, c548, c577), `side()`, `g_c542_on` (c539),
  `g_c132_fx_log` (the c548 gate), `g_c540_on` (c538). These come in through `ubwperf_init(const UbwPerfHost*)`;
  standalone, the session is "standalone" and the flags read their env vars.
- Ticks: `c536_tick`, `c538_tick`, `c539_tick`, `c544_tick`, `c548_tick` run from co-op `c42_step`. Standalone has no
  co-op step, so ubwperf hooks `UGameEngine::Tick` itself and runs its ticks there in both modes (co-op drops the
  calls). Co-op also hooks that function; MinHook relocates a leading `jmp rel32`, so the two hooks chain.
- Shared hook targets (the only hard part):
  - `UPrimitiveComponent::MoveComponentImpl`: co-op card 461 row, with card 539 counts riding on it
    (`g_gate_c461_c539`). ubwperf owns its own row for c539; co-op keeps c461. Two chained hooks, as above.
  - Card 568 throttle: `c568_due` gates two co-op paths (the host game mode tick tail detour, and
    `client_significance_step`, which exists only because the client has no game mode). The throttle moves to
    ubwperf as an export `ubwperf_c568_due(int side_slot, const void* world)`; co-op calls it through the
    `UbwPerfApi` table returned by init, and with no ubwperf it runs every frame (the old `UBWCOOP_C568_OFF=1`
    behavior). Standalone, the host detour is needed for single player; ubwperf installs the game mode tick tail
    row itself only when no co-op host has called init by the first engine tick.
  - Card 576 spin patch: a byte patch at init, no hook; moves as is.
- Load: one binary. dwmapi first looks for an already loaded `winhttp.dll` that exports `ubwperf_init`; if absent it
  calls `LoadLibraryW(L"ubwperf.dll")`. So both names can never be active in one process, and no mutex is needed.
- Log: in co-op, `ubwperf:` rows go through the host's sink (one log file). Standalone, ubwperf opens its own
  `ubwperf.log` beside the exe, through the same ue log code.

Open choice for the senior: the c548 diag gate. Today it is `g_c132_fx_log` (a co-op flag). Plan: ubwperf reads
`UBWCOOP_C132_FX_LOG` itself, so the gate is unchanged in both modes.

Proof (unchanged from above): the perf rows (`card531`, `card536`, `card538`, `card539`, `card544`, `card548`,
`card568`, `card576`, `card577`) match before and after in a matched window, 0 card536 mismatches, no new WARN rows.

## Result (b425, 2026-11-21)

Build b425: `build-b5/dwmapi-b425-46eab80496a61ddd.dll` (exports `ubwcoop_marker`) and
`build-b5/ubwperf-b425-708e86055e437468.dll` (49 winhttp forwards plus `ubwperf_init`), each with its `.map`.
Shape as built:

- `ubwperf.dll` owns the perf rows (cards 531, 536, 537, 538, 539/542, 544, 548, 568, 576, 577) with its own MinHook
  copy. Co-op: `dwmapi.dll` calls `perf_link_init()` before `read_card_flags` (takes `ubwperf_init` from a loaded
  `winhttp.dll`, else `LoadLibraryW(L"ubwperf.dll")`), and `perf_link_install()` after its own install. The three
  co-op sites (c42_step tick, card 461 detour, the two card 568 sites) forward through `perf_link_*` and run the
  original path until `install()` succeeds (`g_perf_live`), or always when the dll is missing.
- Standalone: loaded as `winhttp.dll` (override `winhttp=n,b`), its worker sees no `ubwcoop_marker` export, installs
  alone (its own `UGameEngine::Tick` and `UpdateSignificanceManager` rows) and logs to `ubwperf-<tag>-*.log`.

Co-op pair `c578b425a` (C4M1, 148 s, scenario passed, rc 0/0, no dumps) against `c567b424a` (b424, 153 s):

| Row | b424 host/client | b425 host/client |
|---|---|---|
| card531 | 0/0 | 0/0 |
| card536 | 42/39 | 42/19 |
| card538 | 2/2 | 2/1 |
| card539 | 0/2 | 0/1 |
| card544 | 1/1 | 1/1 |
| card548 | 0/0 | 0/0 |
| card568 | 1/1 | 1/1 |
| card576 | 1/1 | 1/1 |
| card577 | 16/16 | 16/16 |

The client's lower card536/538/539 counts are one report window, not lost rows: the reports come every 60 s from
87 s after start. The b424 client log ran 151 s (reports at +87 s and +147 s); the b425 client log ran 145 s, so its
second report (+147 s) fell after the log ended. Row kinds per window are identical. card536 mismatches 0 on both
sides. WARN rows: the same classes as b424 (attach DisableThreadLibraryCalls err=126, card537 class table full) plus
two `card152` client bone check rows, a known class (seen in `c587b422b`), not perf. The b424 card132 hit-distance
and card467 rows did not recur. Both sides log `ubwperf: hooks installed (co-op)` and `card578: ubwperf linked`.

Single player through `winhttp.dll` (manual umu-run on lane 2, no `dwmapi` override, `UBWCOOP_LOG_TAG=c578sp2`,
420 s, menus driven by xdotool into Operation Slayer, `logs/c578sp2-ubwperf.log`): proxy resolved 49/49, 9 of 12
rows installed (the three card 548 rows gated off, as in co-op without `UBWCOOP_C132_FX`), `ubwperf: hooks installed
(standalone)`. Rows: card536 4 reports (97.5% hit, 2221 checks, 0 mismatches), card538 with the card 540 memo
serving, card568 throttle on the new world, card577 5 rebuilds, card576 and card544 once each. WARN rows: card537
class table full only. A first bare launch (`c578sp`, map passed on the command line) never left the front end: the
map argument is ignored without the co-op travel, so its counters never folded; that run proved the load and install
only. `winhttp.dll` was removed from lane 2 after the run; `ubwperf.dll` stays.
