> Historical snapshot: docs/PLAYTEST.md as it stood on 2026-11-22, with play build notes up to 2026-11-13. Not current. See docs/PLAYTEST.md.

# LAN playtest (two Linux machines, Proton)

The host starts any mission from the main menu as a listen server on UDP 7777; the client joins by IP from the co-op panel.

## Set up the second machine (once)

1. Copy the repo folder, or at least `tools/playtest.sh`, `build/dwmapi.dll` and the game:
   - either copy `game/` (7.3 GB) from this machine,
   - or extract the GOG installer there: `innoextract -d game "starship_troopers_ultimate_bug_war/setup_starship_troopers_ultimate_bug_war_1.2.0_(64bit)_(90228).exe"`. Keep the two `-1.bin` and `-2.bin` files next to the `.exe`.
2. Install `umu-run` and GE-Proton (the same as this machine: GE-Proton11-12).
3. The prefix is created on the first launch (`PREFIX`, default `<repo>/prefix`). On two machines each has its own. To run both games on one machine (rehearsal), they share the one prefix: start the host first, then the second game with `SECOND=1` (it runs inside the host's Proton session and skips the deploy and the already-running check).
4. On the host machine, allow inbound UDP 7777 if a firewall is on (for example `sudo ufw allow 7777/udp`).

Both machines must run the **same** `build/dwmapi.dll`. Compare the hash the script prints at launch.

**Play build: `5e2ce00acad8efa3`** (2026-11-13, `dist/UBW-Coop-20261113-1533.zip`): build 4. Over the build below it adds the fire press and automatic reload relay (card 276, fewer lost client hits), the client press-to-start screen (224), the name tag hidden under full-screen screens (225), frozen corpse position (251), counted objective totals on the client (253), dropped weapon pickup rest position (256), the FED_C3M2 dropship spline on the client (236), crate drop double credit (239) and long "parent/component" names in world rows (230). Driven through FED_C1M1 and FED_C3M1 with both players, no crash.

Please check while playing (not provable in the test harness):
1. Client, FED_C1M1 start: the title and the press-to-start prompt show, and Enter starts (card 224).
2. The other player's name tag does not draw over the press-to-start screen or a cutscene (card 225).
3. Client interaction prompts show on screen at objectives (card 220).
4. Known and not fixed: a bench the host destroys can stay whole for the client on FED_C3M1 (card 283); a few client hits can still be lost in long bursts (card 281).

Previous play build: `3d3f70b0bb6ba6de` (2026-11-12, `dist/UBW-Coop-20261112-2351.zip`): the build below plus card 255 (on FED_C2M1 either player can board the mech and "Enter the M7 Razorback" completes). Before it, `83cf0b9cdcfe0cb0` (`dist/UBW-Coop-20261112-1757.zip`): card 248 (in the tutorial, FED_C1M1, the host can place the satchel charge while the client stands near; before, the hold never completed and the mission stopped there). FED_C1M1 driven through every objective by its real trigger with both players.

Previous play build (not play-tested, holds the satchel hold blocker): `d561aa4a8628ec3f` (2026-11-12, `dist/UBW-Coop-20261112-1518.zip`): the build below plus card 238 (the host no longer crashes when a damage event carries a bad bone name; found on FED_C5M2) and card 235 (on FED_C3M2 the missile silo "Fire Bomb" timelines now reach the client). Soaked on the whole Trooper campaign.

Previous play build (not play-tested, holds the card 238 host crash): `20cf859dd191a8fe` (2026-11-12, `dist/UBW-Coop-20261112-1328.zip`): the build below plus card 229 (on the third mission, FED_C3M1, placed effects past the first 1024 names now reach the client).

Previous play build (not play-tested): `2928654b3a96090d` (2026-11-12, `dist/UBW-Coop-20261112-1155.zip`): the build below plus card 227. Check in the tutorial (FED_C1M1): at the capture-the-flag room, the door closes and the enemy wave starts only when both players are inside; one player alone inside changes nothing. A player who stays outside is waited for, not teleported.

Previous play build (not play-tested): `8edaffb0745a50fe` (2026-11-12, `dist/UBW-Coop-20261112-1035.zip`; every fix card closed after the 2026-11-08 playtest, list in `docs/STATE.md`; coverage soak on FED_C1M1 and FED_C2M1 passed with 0 digest mismatches). Both players see the press-to-start screen; the client leaves it when the host continues.

Previous play build: `7044ed8c10577a0c` (2026-11-08, cards 132, 136, 137, 138, 139, 140 with 135, 148 and parts of 142 and 150: every visible effect the host plays (blood, muzzle flashes, explosions, gore) shows on the client once, from the host; bugs that were dead before a friend joins are placed where they died instead of dying again at spawn; destroyed barrels and frozen corpses match; gib parts fly and land the same on both sides; a fix for the client crash in the effects code; the client's own game no longer picks up map pickups or restarts ragdolls on corpses the host owns; the send rate per client is 250000 B/s and the mod's messages are paced and smaller (binary), so a big fight or a rejoin no longer drops the client; cinematics are no longer skipped automatically in play (card 135); players skip them with the game's own button. Known issues: a corpse that is still falling can lie in a different spot on each screen until it settles (card 152); the catch-up at a rejoin can take about 1.3 s (card 142); a second nuke shot was not seen in tests (card 147). Saved read-only as `build/dwmapi-play-7044ed8c10577a0c.dll`; package `dist/UBW-Coop-20261108-1830.zip`). If `build/dwmapi.dll` has a different hash, copy that file over it, or pass `DLL=build/dwmapi-play-7044ed8c10577a0c.dll`.

Previous play build: `4cb036fa33c090bc` (card 131: bug nests, crates and barrels the host destroys are destroyed on the client too, also for a friend who joins later; dead bugs fall over on the client; card 133: a satchel the client plants on a flak bug or other satchel building explodes for both players, decided by the host; card 130, partial: no cinematic plays, host or client; boot is about 14 s faster on both sides (the GOG Galaxy sign-in wait is cut to 1 s, no game file changed); bug holes that die on the host die on the client; a downed client can move after RESTART CHECKPOINT; one log file per launch, named with the launch time; client log fixes; card 129: a client's turret aim no longer stalls every few seconds; card 128: a friend whose game freezes for up to 20 s stays in the session, and a friend who is dropped rejoins on their own with no clicks; card 125: a client in a mounted turret can aim and fire it, the host deals the damage; turret aim, heat and rest yaw are the same on both sides, and each side sees the other's turret rounds; card 124: test driver only, no change to play; card 123: a client can mount and leave the mounted turrets on FED_C3M1 and later maps without a crash, the host runs the mount; turret aim and fire for the client are not done yet (card 125); card 122: in co-op, bugs pick a player over NPC troopers more often, so more bugs attack the players at once (WarriorBug player preference 10 % -> 16 % with 2 players, host only); card 121: fix for the client crash at the checkpoint save after the client pulls the assault course lever; the client in co-op runs no checkpoint save or load at all; card 120: a client that joins while the host is paused can move, the host's pause lifts at the join and the menu stays open; the client waits for its minimap before the join replays, a join crash fix; card 119: fix for the client crash at the checkpoint save after a support flare, and the airstrike bombs now show on the client; card 118: crash trace, any fatal error on either side writes CRASH lines (code, address, stack) to the mod log; card 117: guard for a client crash at a rejoin (null mesh in `UADSkeletalAnimatorComponent::StartAnimationSequence`), healing read logged; card 114: difficulty values in the editable file UBWCoop-Difficulty.ini, host values sent to the client; card 113: after a mission change or restart the first waves are sized for the whole session, while the client reconnects (45 s grace); card 112: a join or leave mid-fight resizes the waves of that encounter that have not started; card 111: with two players bug waves are 1.6 times larger (host only); card 110: the other player has a name label above the head, shown through walls, with "DOWN <n>s" while downed; card 109: a downed player can shoot and reload; card 108: bugs leave a downed player for a standing one; card 107: enemy death drops and every other runtime pickup are the host's and show on the client at the same spot; card 103: bugs attack the client too (attack tickets by distance to the enemy's own target); card 105: resupply pod rack and pickups are the host's, relayed to the client, late joiners get live ones; card 101: the client's mantle is no longer pulled back, one activation per press; card 104: the client's flare throws on the host, the strike shows on the client, flare charge mirrored; card 100 client-local objective completions skipped; card 102 acting-player scope: client overlaps, shots and interactions count as "the player" for world actors; card 099 "the player" gate; on top of 098 bug hole relay, 097 objective completions, 096 pause, 095 collision and 094 jump/mantle; saved read-only as `build/dwmapi-play-4cb036fa33c090bc.dll`). If `build/dwmapi.dll` has a different hash, copy that file over it, or pass `DLL=build/dwmapi-play-0a98be3c89324a0d.dll`.

## Play (co-op panel, no env vars)

Both machines run `tools/playtest.sh menu`. Dismiss "Authentication failed" and "controller disconnected". The co-op panel sits in the top-left corner of the main menu (white text on a dark box; F8 hides and shows it):

```
UBW CO-OP   (F8 hides)
[H] Host co-op: OFF
[J] Join: [ 192.168.1.20:7777 ]
```

Host:
1. Press **H**. The line turns to `Host co-op: ON` and lists this machine's LAN addresses under "Players join at:".
2. Start a mission from the menu as usual: press any key at the title, then in the hangar choose **Human Missions**, Enter (mission select), Enter (difficulty), Enter. The mod adds `listen` to that mission's travel, so the mission is open on UDP 7777. Later missions (mission end, restart) keep listening.
3. The intro movies skip one part per Enter, so press Enter several times. At the mission brief ("PRESS ANY KEY") press Space. The dropship flight takes about a minute.

Client:
1. Wait until the host is in the mission.
2. Press **J**, type the host's address (`a.b.c.d`, or `a.b.c.d:port`; the default port is 7777) and press **Enter**. Esc leaves the box without joining.
3. The status line shows `Connecting to a.b.c.d:7777... (N s)`. On success the game loads the host's mission. On failure the menu comes back with `Could not connect: <reason>` after about 20 s (no host answering: `ConnectionTimeout`). Esc while connecting cancels.
4. A typo shows `Invalid address`; press J again. The box keeps the last text typed into it (not the saved address), so retype the whole address.

H and J do nothing while the box has focus (they are letters in the address). The address and the host flag are saved in `%LOCALAPPDATA%\StarshipTroopers_UBW\Saved\UBWCoop.ini` (in the prefix: `<prefix>/drive_c/users/steamuser/AppData/Local/StarshipTroopers_UBW/Saved/UBWCoop.ini`), so the box is filled in next time. A joining player's host flag is turned off.

Other notes:
- Movie cutscenes advance on Enter.
- While the host is in the intro movies or the brief, the client's game is paused and Esc does nothing there.
- `WINDOWED=1` runs a window instead of full screen, 1280x720 unless `RES=WxH` is set; `POS=X,Y` places it; `LOG_TAG` names the log (default: the role).
- **Leaving:** the client's pause menu QUIT (Esc, the last item, YES) ends its session; it does not rejoin by itself. Join again with J. When the host QUITs, the client tries to reconnect for about 60 s, then shows `Connection lost: ConnectionLost` at its menu.
- The client's pause menu also lists RESTART CHECKPOINT and RESTART MISSION; only the host's restart counts.
- **Switching weapons:** keys **1**, **2** and **3** pick a weapon slot; the mouse wheel cycles. A picked-up weapon does not equip by itself, on either machine: press **2** (or the wheel) to draw it. The client's switch shows on both screens (card 057). In FED_C1M1 the tactical support case is the exception: the level script equips the laser rifle for the **host's** player, and when the client opens the case the host's player gets one too.
  Client pickup (card 058): stand right at the case, press **E**, then **2**; the rifle draws and fires on both screens.
  Watch item: the host's own switch with 2. Card 062 found why card 060's 2 press did nothing: after the host picks up a placed Morita, the level script's GiveMorita leaves it one weapon (the Morita, slot 1, already drawn). The 2 key asks for slot 2, which is empty, so the game's own `ShouldAbilityRespondToEvent` refuses it, in solo play too. That is normal game behaviour, not a co-op fault. The host's own switches during the pickup do run under the ServerOnly policy. Still untested: a host switch between two real weapons in co-op (C1M1 gives the host no second weapon there). If the host ever holds two weapons and 2 does nothing, or the game faults, note the time.
- One machine only: both games share the one `UBWCoop.ini`, and the client's join saves `host=0`, so press H again on the host after each launch.
- Find the host's LAN address with `ip -4 addr` if the panel lists none.
- Keys: **E** interacts (cases, pickups, switches), **4** calls tactical support, **R** reloads, a quick tap of **Tab** shows the objective list.
- **Quitting the game:** from the hangar press Esc, then Enter on CONFIRM. Esc on that dialog backs out; make sure the dialog has closed before pressing J, or the next Enter quits the game.
- Keep hands off a game window while it has focus for typing: keys pressed in another window can land in the join box.
- Killing yourself with your own grenade shows "TRAITOR!" (the game's own message). In co-op, if it only downs the host, the tutorial then needs a restart (card 607).
- When the host starts the next mission, the client drops to the main menu for a moment and rejoins by itself (about 7–9 s in tests). Do not press J during that.

### Fallback: the env-var roles

If the panel does not work, the old roles still do: `tools/playtest.sh host` on the host (it goes straight to FED_C1M1 and listens), then, once the host is in the mission, `tools/playtest.sh client 192.168.1.20` on the client (it travels to the host 15 s after the main menu). With these roles the panel shows only the connection status.

## After the session

Each launch writes its own mod log next to the exe, `UBWCoop-<role>-<YYYYMMDD-HHMMSS>.log`, so a relaunch keeps the earlier one. The script copies it to `logs/playtest-<role>-<YYYYMMDD-HHMMSS>.log` when the game exits; launched any other way, take the newest `UBWCoop-*.log` from `game/StarshipTroopers_UBW/Binaries/Win64/`. Bring both logs back, plus any crash dump from `<prefix>/drive_c/users/steamuser/AppData/Local/StarshipTroopers_UBW/Saved/Crashes/`. After a crash, the mod log is the main record: the Shipping game writes no log of its own and often no dump, and the mod log ends with the CRASH lines.

## Known limits for this playtest

This list was last checked for play build `7044ed8c10577a0c` (2026-11-08). The open gaps are the open cards in
`docs/STATE.md` "Open queue"; the findings of that playtest are in `docs/playtest-2026-11-08.md`. Still true:
- The remote player is shown as a trooper sprite, not a 3D body.
- Only the host can restart the mission or load a checkpoint; the client follows.
- Mechs: one mech per pad, either player can pilot it. On FED_C2M1, after the mech self-destructs, "Fend off the Arachnid Forces" completes when a player walks about 10 m northeast of its marker (base game trigger position, card 257); AI mech call-ins are untested in co-op.
- The client's armor does not drop (card 170), and the other player's sounds are not placed at their pawn (card 162).

## What to try and note

Write down the time (clock on the wall is fine) for anything odd, so it can be found in the logs.

1. Join: how long from pressing Enter on the panel to being in the mission. Also try a wrong address once and check the menu still works.
2. Weapons: the client picks up a weapon, fires it at a bug, and checks that the bug dies on both screens.
3. Objectives: after the host completes one, check the client's list.
4. Death: the client dies once. Note whether it is downed and revived or respawned, and whether the host can still save at the next checkpoint.
5. RESTART CHECKPOINT (host): does the client follow, and does it keep its weapons and objectives?
6. Leave and rejoin: the client quits to the menu and joins again.
7. Anything that only one screen shows: bugs, doors, mechs, pickups.
8. Client fire next to the host (card 099): the host should hear the client's shots from the client's position, not as its own, with no camera shake. Note where the client's bullets land on the host's screen, whether a client kill shows a score popup on the host, and whether the client feels double rumble.
9. Bug hole (card 098): destroy one with a satchel on the host and check that it shows destroyed on the client.
10. Shooting range (card 102): the client shoots a range button; it should press on both screens. Also the client walks into a move-to step alone and checks that it completes for both.
11. Support flare (cards 104, 105): the client picks up the flare and presses 4; the bombing run should show on both screens and the client's flare slot should empty. The resupply pod should drop the same rack and pickups on both screens. The client takes one pod pickup: the client should get the item, and the pickup should disappear on the host too (not shown in a test run yet). Check the host's own flare throw still works. Watch the host log for "native CompleteObjective TacSupport_01" or "TacSupport_SO6" after the throw (the test route could not arm that step).
12. Enemies (card 103): the host stays back while the client fights a group of bugs. The bugs should attack the client and the client should take damage. With both players near, the bugs should split between them. When one player is downed and the other stands, the bugs should leave the downed player and go for the standing one (card 108).
13. Drops and mantle (cards 107, 101): kill bugs and check that a weapon dropped by a dead bug shows at the same spot on both screens, and that the client can pick it up (it should disappear on the host too). Mantle: the client vaults a waist-high ledge several times while holding W; it should not be pulled back. The test runner could not reach a ledge, so this is untested in a run. Downed (card 109): a downed player can still shoot and reload, but cannot walk, sprint or interact.
14. Player marker (card 110): each screen shows the other player's name above the head, also through walls, and no label over AI troopers. While that player is downed the label adds "DOWN" and the seconds left. Note whether the name is your Steam name or "Player", whether the text is readable at your resolution, and whether showing it through walls is wanted.
15. More bugs with two players (card 111): with both players in, bug waves should be about 1.6 times larger than in solo play. Waves the map set up before the client joined keep their solo size, so the first fights may look unchanged. After a mission change the client drops and reconnects by itself in about 7 s; the first waves of the new map should already be the larger size (card 113). After co-op, a solo game should have normal waves again. If the friend joins or drops in the middle of a fight, the later waves of that fight should resize (card 112; seen in a test run, 26 -> 16 at a drop and back at the rejoin). Note whether the fights feel harder but fair.
16. Difficulty file (card 114): the file `%LOCALAPPDATA%\StarshipTroopers_UBW\Saved\UBWCoop-Difficulty.ini` (in the wine prefix, under `drive_c/users/steamuser/AppData/Local`) is created at the first launch. Edit a number and relaunch to change it; delete the file to get the defaults back. In co-op the host's file counts. Untouched, the game should play exactly as before. A test run showed the host applying `AmmoReceivedMultiplier` to the client's pickups, and the game reading `HealingPeriodMultiplier` when a player's self heal is granted (a change takes effect from the next map or respawn). The faster or slower regeneration itself was not seen: if you change it, note whether healing feels different.
