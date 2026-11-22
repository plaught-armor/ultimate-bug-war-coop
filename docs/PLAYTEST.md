# Playtest procedure

Current for b465 (2026-11-22). Players install from the release zip and follow `docs/guide/playing.md` and
`HOW-TO-PLAY.txt`. This page is for a playtest from the working folder. Notes on builds up to 2026-11-13 and their
check lists are in `docs/reference/playtest-builds-2026-11-13.md`.

## Set up

1. Each machine needs the same `build/dwmapi.dll` and `build/ubwperf.dll`, `tools/playtest.sh` and the game. Either
   copy `game/` (7.3 GB) or extract the GOG 1.2.0 installer with `innoextract -d game <setup exe>`.
2. Install `umu-launcher`. The script runs GE-Proton and creates the prefix (`PREFIX`, default `<repo>/prefix`) on
   the first launch.
3. Compare the hash the script prints at launch. b465 is dwmapi `03df06297887feeb`, ubwperf `a59345153c6e5358`.
4. The host allows its port through the firewall. `PLAY-COOP.sh` hosts on UDP 47000. `tools/playtest.sh` alone hosts
   on UDP 7777 unless `PORT=<n>` is set.

## Play

1. Each machine runs `tools/playtest.sh menu` and clicks away "Authentication failed" and "controller disconnected".
2. The co-op panel is at the top left of the main menu (F8 hides it). **H** turns hosting on and lists the addresses,
   **I** toggles the host's infinite stamina, **J** joins.
3. The host starts a mission: Human Missions, then Enter three times. Movies advance on Enter. Space leaves the brief.
4. Each client presses J, types `address:port` and presses Enter. A failed join shows `Could not connect: <reason>`
   after about 20 seconds.

The address, host flag and stamina option are saved in
`<prefix>/drive_c/users/steamuser/AppData/Local/StarshipTroopers_UBW/Saved/UBWCoop.ini`. The difficulty values are in
`UBWCoop-Difficulty.ini` in the same folder. The host's file counts.

One machine: start the host first, then the second game with `SECOND=1`. `WINDOWED=1`, `RES=WxH` and `POS=X,Y` place
the windows. Both games share one ini, and a join saves `host=0`, so press H again after each launch.

If the panel does not work, `tools/playtest.sh host` starts FED_C1M1 listening and `tools/playtest.sh client <ip>`
joins it.

## Session rules

- The session persists across missions, restarts and the host's pause-menu QUIT (cards 345, 369).
- Only the host restarts or loads. A client's RESTART MISSION is dropped and its checkpoint load is skipped (card 376).
- A client's QUIT ends its session. It joins again with J.
- During the hold at a mission start, a client that leaves or does not confirm within 60 seconds sends the host to
  the main menu (cards 344, 355).
- Weapons are taken by the player's own Use press (card 559).

## Known limits on b465

- `CHANGELOG.md` lists the known issues of the release. If a player turns Traitor in FED_C1M1 after being downed,
  restart the mission (card 607).
- A remote player is drawn as a trooper sprite, not a 3D body.
- Most testing is with two players. Three were tested (card 502). Four were never run.
- No whole-map soak was run on b465.
- The audio fixes of cards 302, 304 and 314 have had no hearing check.

## What to note

Write down the clock time of anything odd, so it can be found in the logs. Look especially at:

1. Anything that only one screen shows or plays: bugs, doors, effects, pickups, objectives, music, barks.
2. The fixes listed for the release in `CHANGELOG.md`.
3. With sound: shots and own sounds of the other player that are too loud, missing or heard from the wrong place
   (cards 302, 304, 314).
4. A player who stays alive at 0 health after a lethal hit soon after a revive (card 610).
5. Checkpoint reload, mission change, and a client leaving and joining again.

## After the session

Collect every player's log, plus any dump from
`<prefix>/drive_c/users/steamuser/AppData/Local/StarshipTroopers_UBW/Saved/Crashes/`. The script copies the log to
`logs/playtest-<role>-<date>-<time>.log` at exit. Otherwise take the newest `UBWCoop-*.log` from
`game/StarshipTroopers_UBW/Binaries/Win64/`. The mod log ends with CRASH lines after a crash.
