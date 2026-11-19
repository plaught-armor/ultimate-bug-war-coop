# Ultimate Bug War Co-op

A two-player online co-op mod for the Trooper (Federation) campaign of *Starship Troopers: Ultimate Bug War!*
(GOG version 1.2.0). One player hosts, one joins, and the whole campaign plays with both troopers in the same
world: enemies, objectives, doors, pickups, downs and revives, and effects are kept in step between the two.

This is an unofficial fan project. It is not affiliated with or endorsed by the game's developers or publishers.
No game files are included: you need your own copy of the game from GOG.

## Play

1. Download `UBW-Coop-<version>.zip` from the [Releases](../../releases) page.
2. Follow `HOW-TO-PLAY.txt` inside the zip. In short: on Linux, install `umu-launcher` and `innoextract`, put the
   three GOG installer files into the unzipped `UBW-Coop` folder, and run `PLAY-COOP.sh`. The first start unpacks the
   game; after that one player hosts and the other joins.

The mod is a `dwmapi.dll` proxy that loads next to the game's executable. Only the Trooper campaign is supported;
the Arachnid (bug) campaign is not.

## Build

Cross-compiled from Linux with clang-cl and lld-link against the MSVC CRT and Windows SDK fetched by
[xwin](https://github.com/Jake-Shadle/xwin):

```sh
xwin --accept-license splat --output toolchain/xwin
python3 -I tools/gen_proxy_exports.py
python3 -I tools/pdb_rva.py gen      # needs the game's Shipping PDB; offsets.generated.h is checked in
cmake -G Ninja -S mod -B build -DCMAKE_TOOLCHAIN_FILE=$PWD/toolchain/clang-cl-xwin.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The output is `build/dwmapi.dll`. Hooks use [MinHook](https://github.com/TsudaKageyu/minhook) (BSD 2-clause,
vendored in `mod/third_party/minhook` with its license).

## Layout

- `mod/src`: the mod (`ue/` engine glue, `ubw/` game systems: objectives, enemies, doors, weapons, effects, sync).
- `tools/`: build generators, the two-instance test harness (`run_pair.sh`, `drive.py`) and scenario generators.
- `docs/`: development notes. `docs/STATE.md` is the running log, `docs/cards/` the work items, `docs/CARDLOG.md`
  their index.

## License

GPL-3.0, see `LICENSE`. MinHook keeps its own BSD 2-clause license.
