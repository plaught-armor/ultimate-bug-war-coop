# Development

[Back to the README](../../README.md)

Paths and commands on this page are relative to the repository root.

## Building from source

The DLL is cross-compiled on Linux with `clang-cl` and `lld-link` against the MSVC runtime and Windows SDK
fetched by [xwin](https://github.com/Jake-Shadle/xwin). Only a Release build is possible, since xwin ships no
debug runtime.

Needs: `clang` (with `clang-cl` and `lld-link`), `cmake`, `ninja`, `python3` and `xwin`.

```sh
xwin --accept-license splat --output toolchain/xwin
python3 -I tools/gen_proxy_exports.py   # proxy export list; the output is checked in
python3 -I tools/pdb_rva.py gen         # needs the game's Shipping PDB; the output is checked in
cmake -G Ninja -S mod -B build -DCMAKE_TOOLCHAIN_FILE=$PWD/toolchain/clang-cl-xwin.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The outputs are `build/dwmapi.dll` and `build/ubwperf.dll`. The two generator steps can be skipped, since their outputs are in the
repository.

## Packaging a release

```sh
TAG=b123 tools/make-package.sh
```

This writes `dist/UBW-Coop-<date>-<time>.zip` with `PLAY-COOP.sh`, `HOW-TO-PLAY.txt`, `tools/playtest.sh`,
`build/dwmapi.dll`, `build/ubwperf.dll` and `build/VERSION.txt` (the short sha256 of `dwmapi.dll`). With `TAG` set, it also adds `RELEASE.txt`,
which the launcher's update check compares against the latest GitHub release tag.

## Test harness

`tools/run_pair.sh <tag> [seconds]` runs a host and a client side by side in windows on one machine and writes
both logs to `logs/`. With `SCENARIO=<file>`, `tools/drive.py` drives both games through a scripted route from
`tools/scenarios/`. The options are documented at the top of `run_pair.sh`.

`tools/playtest.sh menu` starts one normal game with the mod, as the launcher does.

## Repository layout

| Path | Contents |
|---|---|
| `mod/src/` | Proxy, loader, lobby panel, UPnP and input glue. |
| `mod/src/ue/` | Engine layer: reflection, actors, input, widgets, logging and the mod channel. |
| `mod/src/ubw/` | Game systems: objectives, enemies, doors, weapons, effects, poses, vehicles, save and restart. |
| `mod/src/perf/` | Performance code, built as `ubwperf.dll`. |
| `mod/third_party/minhook/` | Vendored MinHook. |
| `toolchain/` | CMake toolchain file for `clang-cl` with xwin. |
| `tools/` | Launcher helper, packaging, generators, test harness, scenarios and log tools. |
| `docs/` | Development notes, listed below. |

Development docs:

- `docs/STATE.md`: current build, rules and work queue. Older logs are in `docs/reference/`.
- `docs/cards/` and `docs/CARDLOG.md`: individual work items and their index.
- `docs/SYNC_AUDIT.md`: per-kind audit of synced state.
- `docs/design/`: design of the co-op channel, lobby and downed state.
- `docs/reference/`: engine and runtime facts, objective audit and routines, and dated snapshots of older docs.
- `docs/PLAYTEST.md`: playtest procedure.
