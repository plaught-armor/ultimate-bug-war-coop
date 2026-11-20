# Ultimate Bug War Co-op

A two-player online co-op mod for the Trooper (Federation) campaign of *Starship Troopers: Ultimate Bug War!*
One player hosts, the other joins, and both troopers play the campaign in the same world.

This is an unofficial fan project, not affiliated with or endorsed by the game's developers or publishers.
No game files are included; you need your own copy of the game from GOG.

## Contents

- [Status](#status)
- [Requirements](#requirements)
- [Install and play](#install-and-play)
- [Playing over the internet](#playing-over-the-internet)
- [Updating](#updating)
- [Troubleshooting](#troubleshooting)
- [How it works](#how-it-works)
- [Building from source](#building-from-source)
- [Packaging a release](#packaging-a-release)
- [Test harness](#test-harness)
- [Repository layout](#repository-layout)
- [License](#license)

## Status

| | |
|---|---|
| Game version | GOG 1.2.0 (build 90228). Other versions or stores are not supported: the mod finds game functions by fixed addresses in this exact executable. |
| Campaign | Trooper (Federation) only. The Arachnid (bug) campaign is not supported. |
| Players | Two: one host, one client. |
| Platform | Linux, through Proton (`umu-launcher`). The launcher and installer scripts are Linux-only. |

What both players share:

- Enemies, their attacks, deaths, corpses and gibs. Bug AI runs on the host only.
- Objectives and mission scripts, doors, switches, turrets, vehicles and the dropship.
- Pickups, enemy drops, resupply pods and support flares.
- Weapon fire, hits and visual and sound effects.
- Downed and revive. A downed player can still shoot; when both players are down, the game reloads the last
  checkpoint.
- Difficulty: with two players, bug waves are larger. The host's difficulty settings are sent to the client.

The session survives mission changes and checkpoint restarts, and a client that drops reconnects on its own.

## Requirements

- The GOG installer of *Starship Troopers: Ultimate Bug War!* version 1.2.0 (the `.exe` and its `.bin` files), or an
  already installed copy of that version.
- Linux with `umu-launcher` (runs the game under Proton) and `innoextract` (unpacks the GOG installer).
  On Arch: `sudo pacman -S umu-launcher innoextract`.
- Both players must use the same release zip.

## Install and play

1. Download `UBW-Coop-<version>.zip` from the [Releases](../../releases) page and unzip it.
2. Put the GOG installer files into the unzipped folder, or skip this step if the game is already installed.
   The launcher looks for the game in `./game`, the parent folder, Heroic, Lutris, `~/Games` and Steam library
   paths, and unpacks the installer only if none of those has it.
3. Run `./PLAY-COOP.sh`. It checks the game executable, installs the mod and starts the game.

In game, at the main menu:

- **Host:** press **H**. The panel shows `Host co-op: ON` and your address. Then start a mission as usual:
  *Human Missions*, then Enter three times.
- **Join:** press **J**, type the host's address with the port (for example `203.0.113.5:47000`) and press Enter.
  The client loads straight into the host's mission.

`HOW-TO-PLAY.txt` in the zip has the full steps, the controls and fixes for the start-up messages.

## Playing over the internet

The host listens on **UDP port 47000** (set by `PLAY-COOP.sh`; change it with `PORT=<n> ./PLAY-COOP.sh`).

When hosting is turned on, the panel lists the addresses to join at:

- **Internet:** the host's public address. If the router supports UPnP, the mod opens the port itself and the
  panel says `UDP port 47000 opened by UPnP`; the mapping is removed when the game exits.
- If the router has no UPnP, or refuses the mapping, the panel says `forward UDP port 47000 by hand`, and the
  public address is looked up from `api.ipify.org` instead. Forward UDP 47000 to the host's computer in the router
  settings, and allow it through the host's firewall (for example `sudo ufw allow 47000/udp`).
- Alternatively, both players can join the same VPN, such as [Tailscale](https://tailscale.com), and the client
  joins the host's VPN address. No port forwarding is needed.

- **LAN:** on the same local network, join the host's LAN address, which the panel also shows.

## Updating

On each start, `PLAY-COOP.sh` checks this repository's latest GitHub release against the `RELEASE.txt` in your
folder and asks before downloading a newer one. Set `UBW_NO_UPDATE=1` to skip the check. Both players need the
same version.

## Troubleshooting

| Problem | Fix |
|---|---|
| "Authentication failed" or "controller disconnected" at start | Click them away. The game runs without GOG Galaxy. |
| Client shows `Could not connect` after about 20 seconds | The host is not reachable. Check the address and port, the port forward or VPN, and the host's firewall. |
| The launcher refuses the game executable | The game is not GOG 1.2.0. Use the 1.2.0 installer. |
| The other player sees different things | Make sure both of you run the same release zip. |

Each launch writes a mod log next to the game executable (`UBWCoop-<role>-<date>-<time>.log`); include both
players' logs when reporting a bug.

## How it works

The game is built on a custom branch of Unreal Engine 5. Its native code replicates nothing: none of its
properties or RPC functions are marked for networking, so stock Unreal multiplayer cannot carry the campaign.
The mod supplies the multiplayer itself.

- **Loading.** The mod is `dwmapi.dll`, placed next to the game executable. Windows loads it instead of the system
  `dwmapi.dll`; it forwards every real export to the system DLL and then installs its hooks. Its only load-time
  import is `kernel32`.
- **Hooks.** Game functions are found by their addresses (RVAs) in the 1.2.0 executable, taken from the game's
  Shipping PDB and checked in as `mod/src/offsets.generated.h`. [MinHook](https://github.com/TsudaKageyu/minhook)
  detours them.
- **Session.** The host runs the mission as an Unreal listen server and is authoritative: enemy AI, objectives,
  damage and pickups are decided there.
- **Mod channel.** Everything the engine does not replicate travels on the mod's own channel: compact binary
  frames packed into strings and carried by two engine RPCs that are reliable and ordered (`ServerChangeName`
  from client to host, `ClientMessage` from host to client). See `mod/src/ue/chan.h` for the frame format.
- **Port mapping.** `mod/src/upnp.cpp` is a small SSDP and SOAP client that asks the router for the UDP mapping
  and the public address, on a worker thread. Without UPnP, it reads the public address from `api.ipify.org`
  over HTTPS. It is skipped in test runs.

`docs/SYNC_AUDIT.md` lists each kind of synced state and how it is carried.

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

The output is `build/dwmapi.dll`. The two generator steps can be skipped, since their outputs are in the
repository.

## Packaging a release

```sh
TAG=b123 tools/make-package.sh
```

This writes `dist/UBW-Coop-<date>-<time>.zip` with `PLAY-COOP.sh`, `HOW-TO-PLAY.txt`, `tools/playtest.sh`,
`build/dwmapi.dll` and `build/VERSION.txt` (the DLL's short sha256). With `TAG` set, it also adds `RELEASE.txt`,
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
| `mod/third_party/minhook/` | Vendored MinHook. |
| `toolchain/` | CMake toolchain file for `clang-cl` with xwin. |
| `tools/` | Launcher helper, packaging, generators, test harness, scenarios and log tools. |
| `docs/` | Development notes, listed below. |

Development docs:

- `docs/STATE.md`: running development log and the current work queue.
- `docs/cards/` and `docs/CARDLOG.md`: individual work items and their index.
- `docs/SYNC_AUDIT.md`: per-kind audit of synced state.
- `docs/design/`: design of the co-op channel, lobby and downed state.
- `docs/reference/`: engine facts and milestones.
- `docs/PLAYTEST.md`: playtest procedure and build history.

## License

GPL-3.0, see `LICENSE`. MinHook keeps its own BSD 2-clause license (`mod/third_party/minhook`).
