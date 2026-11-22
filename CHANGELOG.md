# Changelog

Player-facing changes per public release of UBW Co-op, newest first. All players in a game need the same release.

## b465 (2026-11-22)

### Fixed

- Crash: the host no longer crashes about a minute after a player leaves a mech.
- Checkpoint reloads: the client keeps the correct magazine, reserve ammo, grenades and score.
- Training: the scripted weapon pickup works for the host without waiting for the client. Bullet holes show on the
  range targets on both screens and stay after a checkpoint restart.
- Camera: another player's landing no longer shakes your view, and the client's own landings no longer use the heavy
  shake. The guided missile and the Door Gunner seat no longer break the camera, and the gunship view is smoother on
  the client.
- Players and troopers: fixes for flickering body animations, wrong trooper animations, a cause of host teleports on
  the training map, and a "Player" tag left on an empty mech.
- Weapons: the client keeps its supply drop weapon, and client grenades appear at the moment of release.
- Radar: bugs that are still underground no longer show as blips.
- Bugs: the hidden bug spawner that sat at a different place on the client after a map change (b366 known issue)
  now matches the host.
- Missions: one player boarding the evac dropship no longer ends the level for both, and the client gets the dropship
  outro. An area trigger stays active while another player is still inside. Fog walls of horde zones open on the
  client too.
- Effects on the client: the C3M2 generator glow now follows the host.
- Sound on the client: music and placed sounds started by mission scripts, trooper barks and hopper flight sounds.
- Launcher: the start script no longer refuses current builds, and updates now install every file in the release.

### Changed

- The performance work ships as its own `ubwperf.dll`. The launcher installs it.
- One-shot sounds too far away to matter are no longer sent to the client, which saves bandwidth.

### Known issues

- Friendly fire or a self-kill that only downs a player can still brand that player a traitor in the training
  mission and block it. A fix failed its test and is not in this release.
- A Razorback seen from outside slides with still legs. The base game does this too.
- Some dropped pickups can float. It looks the same on both screens and is probably base-game behaviour.

### Upgrading

- From b366: accept the update in the launcher and stay online for that first start. It downloads twice, because the
  b366 updater does not know `ubwperf.dll`.
- From b257: that version has no updater. Unzip the new release by hand.

## b366 (2026-11-20)

### Added

- Up to four players. Tested with three; four have never been run.

### Fixed

- Objectives: the client no longer completes the Brain Bug fight (C4M2) or the research objectives early, and
  sub-objectives no longer give the client extra score. The C5M2 Array waves end on both machines.
- Downed players: a player who leaves while downed no longer stays downed on the other machines.
- Bugs on the client: positions no longer lag behind the host, and bugs that die mid-fall no longer freeze or jump
  at the start of the fall.
- Effects on the client: effects attached to bugs and weapons are freed instead of piling up over a long mission.
- Score: changes reach the client in the right order.
- Allies step out of the player's line of fire again, as in the base game. Builds b364 and b365 had turned this off
  by mistake.

### Changed

- Smaller network packets for grenades.
- Less host game-thread work, limited to cuts with no effect on what players see.

### Known issues

- After a map change, one hidden bug spawner (C2M1 and C4M1) sits at a different place on the client than on the
  host.

## b257 (2026-11-19)

### Added

- First public release: online co-op for the Trooper campaign on GOG 1.2.0, for Linux through Proton. The Arachnid
  (bug) campaign is not supported.
