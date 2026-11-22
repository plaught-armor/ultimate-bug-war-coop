# Ultimate Bug War Co-op

An online co-op mod for up to four players, for the Trooper (Federation) campaign of *Starship Troopers: Ultimate Bug War!*
One player hosts, the others join, and all of them play the campaign in the same world.

This is an unofficial fan project, not affiliated with or endorsed by the game's developers or publishers.
No game files are included; you need your own copy of the game from GOG.

## Status

| | |
|---|---|
| Game version | GOG 1.2.0 (build 90228). Other versions or stores are not supported: the mod finds game functions by fixed addresses in this exact executable. |
| Campaign | Trooper (Federation) only. The Arachnid (bug) campaign is not supported. |
| Players | Up to four: one host and up to three clients. Most testing is with two players. Three have been tested. Four have never been run. |
| Platform | Linux, through Proton (`umu-launcher`). The launcher and installer scripts are Linux-only. |

What all players share:

- Enemies, their attacks, deaths, corpses and gibs. Bug AI runs on the host only.
- Objectives and mission scripts, doors, switches, turrets, vehicles and the dropship.
- Pickups, enemy drops, resupply pods and support flares.
- Weapon fire, hits and visual and sound effects.
- Downed and revive. A downed player can still shoot; when all players are down, the game reloads the last
  checkpoint.
- Difficulty: bug waves grow with the number of players. The host's difficulty settings are sent to the clients.

The session survives mission changes and checkpoint restarts. A client that loses the connection retries for
about 60 seconds. A client that quits joins again by hand.

## Quick start

1. Download the zip from the [Releases](../../releases) page and unzip it.
2. Run `./PLAY-COOP.sh`.
3. At the main menu, the host presses **H** and starts a mission. Each other player presses **J** and enters the
   host's address.

See [Playing](docs/guide/playing.md) for requirements and installing the game.

## Documentation

| Page | Contents |
|---|---|
| [Playing](docs/guide/playing.md) | Requirements, install, internet play, updating and troubleshooting. |
| [Changelog](CHANGELOG.md) | Changes and known issues per release. |
| [Technical overview](docs/guide/how-it-works.md) | How the mod loads, hooks the game and syncs the players. |
| [Development](docs/guide/development.md) | Building, packaging, test harness and repository layout. |

## License

GPL-3.0, see [LICENSE](LICENSE). MinHook keeps its own BSD 2-clause license (`mod/third_party/minhook`).
