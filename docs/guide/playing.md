# Playing

[Back to the README](../../README.md)

## Requirements

- The GOG installer of *Starship Troopers: Ultimate Bug War!* version 1.2.0 (the `.exe` and its `.bin` files), or an
  already installed copy of that version.
- Linux with `umu-launcher` (runs the game under Proton) and `innoextract` (unpacks the GOG installer).
  On Arch: `sudo pacman -S umu-launcher innoextract`.
- All players must use the same release zip.

## Install and play

1. Download `UBW-Coop-<version>.zip` from the [Releases](https://github.com/plaught-armor/ultimate-bug-war-coop/releases) page and unzip it.
2. Put the GOG installer files into the unzipped folder, or skip this step if the game is already installed.
   The launcher looks for the game in `./game`, the parent folder, Heroic, Lutris, `~/Games` and Steam library
   paths, and unpacks the installer only if none of those has it.
3. Run `./PLAY-COOP.sh`. It checks the game executable, installs the mod and starts the game.

In game, at the main menu:

- **Host:** press **H**. The panel shows `Host co-op: ON` and your address. Then start a mission as usual:
  *Human Missions*, then Enter three times.
- **Join:** press **J**, type the host's address with the port (for example `203.0.113.5:47000`) and press Enter.
  The client loads straight into the host's mission.

Always type the port. An address without one uses 7777, and the launcher hosts on 47000.

The host can press **I** at the main menu to turn infinite stamina on or off for the sessions it hosts. It is off
by default and is remembered.

## During a session

- The session stays up across missions, restarts and the host's QUIT to the hangar.
- Only the host can restart a mission or load a checkpoint. The clients follow.
- A client that quits from the pause menu does not rejoin by itself. Join again with **J**.
- If a client leaves or does not finish loading within 60 seconds while the host waits for it at a mission start,
  the host returns to the main menu instead of starting alone.
- Weapons are picked up with **E** by the player who presses it.

`HOW-TO-PLAY.txt` in the zip has the full steps, the controls and fixes for the start-up messages.

## Playing over the internet

The host listens on **UDP port 47000** (set by `PLAY-COOP.sh`; change it with `PORT=<n> ./PLAY-COOP.sh`).

When hosting is turned on, the panel lists the addresses to join at:

- **Internet:** the host's public address. If the router supports UPnP, the mod opens the port itself and the
  panel says `UDP port 47000 opened by UPnP`; the mapping is removed when the game exits.
- If the router has no UPnP, or refuses the mapping, the panel's port line ends with
  `forward UDP port 47000 by hand`, and the public address is looked up from `api.ipify.org` instead. Forward UDP 47000 to the host's computer in the router
  settings, and allow it through the host's firewall (for example `sudo ufw allow 47000/udp`).
- Alternatively, all players can join the same VPN, such as [Tailscale](https://tailscale.com), and the clients
  join the host's VPN address. No port forwarding is needed.

- **LAN:** on the same local network, join the host's LAN address, which the panel also shows.

## Updating

On each start, `PLAY-COOP.sh` checks this repository's latest GitHub release against the `RELEASE.txt` in your
folder and asks before downloading a newer one. Set `UBW_NO_UPDATE=1` to skip the check. All players need the
same version.

## Troubleshooting

| Problem | Fix |
|---|---|
| "Authentication failed" or "controller disconnected" at start | Click them away. The game runs without GOG Galaxy. |
| Client shows `Could not connect` after about 20 seconds | The host is not reachable. Check the address and port, the port forward or VPN, and the host's firewall. |
| The launcher refuses the game executable | The game is not GOG 1.2.0. Use the 1.2.0 installer. |
| The other player sees different things | Make sure everyone runs the same release zip. |

Each launch writes a mod log next to the game executable (`UBWCoop-<role>-<date>-<time>.log`); include every
player's log when reporting a bug.
