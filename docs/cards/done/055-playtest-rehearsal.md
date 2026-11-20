# Card 055: Rehearse the playtest end to end

Status: ready 2026-11-04 (senior, revised: launch step merged, working-set measure dropped). Send it right after 052 is accepted. This is the last gate before the user's 8 pm LAN playtest.

Read `docs/STATE.md`, `docs/PLAYTEST.md`, and the Reports of cards 050 and 052. Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why

Each card so far was proven with its own scripted run, which covers one feature. The playtest is one long session that uses all of them in the order a player does: start the game, set up through the panel, play the mission, die, restart, and go on to the next mission. This card runs that session once on this machine, using `tools/playtest.sh`, the same entry point the user will use. It finds what breaks between features before the user does.

## Do

1. **Launch the way the user will.** Start the host and the client with `tools/playtest.sh menu`, side by side at 640x360, against `127.0.0.1`. Use no `UBWCOOP_*` variables other than what the `menu` role sets. One script change is needed: `WINDOWED=1` is fixed at 1280x720 (line ~59), so add a `RES` override (for example `RES=640x360`) and a window position, as `run_pair.sh` passes `-WinX`/`-WinY`. Both games use the one prefix, with `PROTON_VERB=run` on the second launch, exactly as `run_pair.sh` does (proven over every two-player run so far). Do not create a second prefix; that is a first-launch setup of several minutes for no gain on one machine. Fix `docs/PLAYTEST.md` step 3 to match: two games on one machine share the prefix this way; on two machines each has its own.
2. **Set up through the panel.** On the host, turn host mode on and start the campaign's first mission through the game's menus. On the client, type the address and join. Scripted keys (card 031 script mode, or `autoinput`) are fine. Take shots of both panels, and of both machines once the client is in. Go through the real level select, not `TransitionToLevel`. Take one more panel shot of the host at `RES=1280x720` to check that the panel text scales and is readable (card 052 saw small text at 640x360).
3. **Play FED_C1M1 through its early beats.** Follow PLAYTEST.md's rules: the host does the interactions until card 054 lands. On a scripted route, cover:
   - The client picks up a weapon (card 050). The host's view of the client's sprite shows the weapon.
   - Both players shoot bugs. Bugs die on both screens.
   - The host completes the first objective. The client's panel shows it complete.
   - The client dies and is revived, or respawns (cards 044 and 046).
   - The host does RESTART CHECKPOINT. The client follows, and its weapon and objectives come back (cards 045, 049, 050).
   - Carried from card 050 (its Senior review); each is a pass or fail line in the Report:
     - The client holds a picked-up weapon through RESTART CHECKPOINT, or matches the save. The host log shows the forced `LoadAbilitySystemComponent` send, and the client log shows the held list applied again on the new pawn, with "matches the host".
     - After the client's death, the host walks into the next checkpoint zone. `Save_CheckPoint` runs, and the save file's time changes.
     - The host picks up a weapon of its own. The client's view of the pickup is noted.
     - The client fires the weapon it picked up at a bug, and the hit lands on the host (card 024).
     - For each pickup taken, a shot of the case on both screens, saying whether it is still shown.
4. **Leave and rejoin.** The client quits to the menu and joins again through the panel. Then the host quits to the menu. The client must end up at a working main menu with a readable status line.
5. **Measure.** Report the time from the client pressing Enter in the join box to its pawn being possessed in the mission. Use `python3 -I tools/logsum.py <tag> --grep 'card052|possess'` to read both logs as one timeline (the instances share a clock). The mod log is an event log, not a frame log, so do not report log gaps as hitches.
6. **Fix or card.** If something in this run breaks and the fix is 30 lines or fewer and inside a feature that is already done, fix it here and say so. Otherwise stop at the failure, report it with logs and shots, and do not work around it.

## Done when

- The session runs from step 1 to step 4 with no crash, no new dump, and rc 0 on both processes.
- The Report lists every step with its shot names and pass or fail, and every issue found. Any new issue that is not fixed is also added to STATE "Known issues".
- `docs/PLAYTEST.md` is correct for what the user will do at 8 pm: the menu flow is first, and the old roles are kept as a fallback.
- The build is clean with `/W4 /WX`, and every function is 60 lines or fewer.

## Report

Three sessions through `tools/playtest.sh menu` on this machine, one shared prefix (host first, client with `SECOND=1`), against `127.0.0.1`:
- **Run A** (old DLL): steps 1, 2 and 4. Found two QUIT bugs (below). Logs `logs/card055a-{host,client}.log`.
- **Run A3** (fixed DLL 9366a31fe21417eb): steps 1, 2 and 4 again. Logs `logs/card055a3-{host,client}.log`. (Run A2 was dropped: host mode was off, see "Shared ini".)
- **Run B** (same DLL): step 3, with the host at `RES=1280x720` and the card 043/045/049/050 test drivers set (`UBWCOOP_C43_TEST`, `C45_TEST`, `C50_TEST`, `C49_TEST=TacSupport_SO0`, `AUTODROPSHIP`). The panel host and join and the real level select were the same as in A3. Logs `logs/card055b-{host,client}.log`.

All three: no crash, `umu-run rc 0` on both processes, crash dumps still 20 (none new).

Shots: window captures are in `logs/card055-shots/` (`aNN-*` run A, `cNN-*` run A3, `b*` run B). The game's own shots are copied there as `ScreenShotNNNNN-<what>.png`.

### Step 1: launch — pass
- `playtest.sh` gained `RES=WxH`, `POS=X,Y`, `LOG_TAG` and `SECOND=1` (`PROTON_VERB=run`, no deploy, no already-running check). The launch inherits the environment.
- `PLAYTEST.md` step 3 now says: one machine shares the prefix with `SECOND=1`; two machines have one prefix each.

### Step 2: panel host and join through the real menus — pass
- Host: H turns the panel to ON and lists 192.168.0.150 (a03-host-panel-on, c01-host-panel-on). The real path is title, hangar Human Missions, mission select, difficulty, Enter (a05-host-missionselect, a06-host-difficulty, a07-host-go).
- The host log shows `listen` added to the Browse and `UWorld::Listen returned true` (Run B: 266 ms after the Browse).
- Client: J, type the address, Enter (a04-client-panel, a10-client-typed, a11-client-connecting, c03/c04/c05).
- Both in the mission: a12-host-mission, a13-client-mission, c08-host-onfoot, c09-client.
- Host panel at 1280x720: `b-host-panel-1280.png`. The text is readable.

### Step 3: FED_C1M1 early beats (run B)

| Item | Result | Evidence |
|---|---|---|
| The client picks up a weapon | **Partial pass.** The server copy takes the LaserMorita case (`ASTWeaponPickup::PickupWeapon`, host t=580398479). The client gets `apply #2 ... given 1, matches the host` (Unarmed + LaserMorita). The client's HUD still reads UNARMED: the weapon is not drawn. | ScreenShot01319-client-next-to-pickup, ScreenShot01321-client-hud-after-pickup (case open, HUD UNARMED) |
| Host sees the weapon on the client's sprite | Not shown: the client never drew it. | — |
| Bugs die on both screens | **Not covered.** The drivers do not fight. | — |
| Host completes an objective; the client's list shows it | **Pass (log).** Host `CompleteSubObjective(TacSupport_01, TacSupport_SO0)` at t=580502133. The client replayed it at 580502150 (17 ms). The completion starts a cutscene on the host. There is no client panel shot of this. | ScreenShot01322-host-objectives, ScreenShot01335-host-after-objective-complete |
| The client dies; revive or respawn | **Pass.** Client Downed, then Out, then `revived from Out after a 6004 ms hold` (host t=580549160). In the all-down stages both die, and the death screen disables everything except Exit on the client. The client respawns in place after the host's restart (`2 of 2 death screens removed, input GameOnly`). | ScreenShot01324-client-sees-host-downed, ScreenShot01336-host-sees-client-downed, ScreenShot01339-host-sees-client-standup, ScreenShot01340-host-sees-client-standing, ScreenShot01327-host-gameover, ScreenShot01328-client-deathscreen |
| RESTART CHECKPOINT; the client keeps its weapon and objectives | **Pass (matches the save).** `test restart 2: RESTART CHECKPOINT (LoadLastSave)`. Host `LoadAbilitySystemComponent` #1 (own) and #2 (server copy). The forced send is `Weapons to BP_PlayerController_C (copy ..., LoadAbilitySystemComponent): sent`. The client gets `apply #6 ... matches the host`. The checkpoint was saved after the earlier all-down reload, so its list is Unarmed only, and the LaserMorita is gone on both machines. The objectives are replayed after the reload (14 → 15 including Traitor_O1, card 053). | ScreenShot01347-host-after-checkpoint-restart, ScreenShot01348-client-after-checkpoint-restart |
| After the client's death, `Save_CheckPoint` runs and the save file changes | **Pass.** `Save_CheckPoint(FED_C1M1, flag 1)` at t=580463893, then flag 0 every 6 s. `Slot1_Checkpoint_{0,1}.sav` changed at 12:17:51 and `_2` at 12:17:38. The client's first death was at about 12:16:49. | save dir listing |
| The host's own pickup, as the client sees it | **Not covered.** | — |
| The client fires the picked-up weapon; the hit lands on the host (card 024) | **Not covered.** The weapon was never drawn. | — |
| A shot of each pickup case on both screens | **Client only.** The client's case is open after its pickup (01321). The host view was not captured. | ScreenShot01321 |

The uncovered items are added to STATE "Known issues" for the 8 pm playtest. In run A the client's objective list matched the host's (a20-client-esc).

### Step 4: leave and rejoin — pass after a fix
- **Bug 1 (run A):** the client's pause-menu QUIT auto-reconnected about 7 s later (a24–a26). The card 045 follow treated `FromLevelQuit` like a drop.
- **Bug 2 (run A):** the host's QUIT kept `listen` (card 045 added it). The client was pulled into the host's frontend as a net client, and the host's panel was hidden at net mode 2 (a30-client-after-hostquit, a31-host-menu).
- **Fix** in `mod/src/hooks.cpp`, 9 lines:
  - `c45_client_browse` (40 lines) returns early on `FromLevelQuit` and logs `left the session by the pause menu QUIT (FromLevelQuit): no reconnect`.
  - `c45_host_browse` (21 lines) leaves the URL unchanged on `FromLevelQuit`.
  - The build is clean with `/W4 /WX`: 84 exports, KERNEL32 only.
- **Run A3:**
  1. The client QUITs to the title, with no reconnect (c12-client-quit-menu, c13-client-title-panel).
  2. The client rejoins with J (c14-client-rejoin-typed, c15-client-rejoined, c16-host-after-rejoin).
  3. The host QUITs, with `Browse URL unchanged ... FromLevelQuit 1`. The host frontend comes up at net mode 0 with the panel on (c18-host-menu).
  4. The client retries 4 times, gives up after 63 s, and shows `Connection lost: ConnectionLost` (c17-client-hostquit-15s, c19-client-final). Its menu works (c20-client-menu-works).

### Step 5: join to possess
Join arm (Enter in the box) to the first `possess: OnRep_Pawn` with a pawn:

| Join | Time | LoadMap success after the arm |
|---|---|---|
| Run A | 3062 ms | 2594 ms |
| Run A3 first join | 2857 ms (579859267 → 579862124) | 2495 ms |
| Run A3 rejoin | 2865 ms (580118769 → 580121634) | 2544 ms |

### Other findings, for PLAYTEST.md
- **Shared ini:** on one machine both games share `UBWCoop.ini`, and the client's join saves `host=0`, so the host must press H again after each launch. This only happens on one machine.
- **Client paused:** while the host is in the intro movies or the brief, the client's world is paused and Esc does nothing there.
- **Movies and brief:** the intro movies skip one part per Enter. The brief takes Space.
- **Client pause menu:** it also lists RESTART CHECKPOINT and RESTART MISSION.
- **TRAITOR! objective:** it appeared on the host after the scripted all-down death (b01-host). It is added as Traitor_O1 and replayed to the client (card 053 covers dropping it).

### Docs
- `docs/PLAYTEST.md`: the prefix sharing, the real menu path, Enter and Space for the movies and brief, the client pause, both QUIT behaviours, the unequipped pickup, the shared-ini note, and `RES`/`POS`/`LOG_TAG`. The menu flow stays first, with the env-var roles as the fallback.
- `docs/STATE.md` "Known issues": the unequipped pickup, the uncovered step 3 items, and an update to the pickup-shot note.

## Senior review (2026-11-04)

Accepted.

- **Logs:** for runs a, a3 and b, both sides show 0 ERROR/FATAL and `rc 0` in every `.run`. There are still 20 dumps.
- **Build:** the DLL hash is `9366a31fe21417eb`, which matches the report. `fnlen` finds 1127 functions, 0 over 60 lines.
- **QUIT fix, read in `c45_host_browse` and `c45_client_browse` (hooks.cpp ~17271 and ~17295):**
  - A client QUIT returns before the follow is armed.
  - During an armed follow, a QUIT Browse to the frontend is not a fallback (it is not the landing map), so the follow stops. That is right.
  - `g_client_session` is cleared by the next LoadMap that has no pending net game (hooks.cpp ~3857), so a later solo mission is not taken as a drop.
  - The host keeps `listen` off for FromLevelQuit only, so a restart still adds it.
- **Shots checked:**
  - `b-host-panel-1280`: the panel is readable. The font is a fixed pixel size and does not scale, but at 720p it is readable, so this passes for the playtest.
  - `ScreenShot01321`: the case is open and the HUD reads UNARMED. This matches the report.
  - `c15-client-rejoined`: in the mission, with the objectives panel.
  - `c19-client-final`: the client is at the title with `Connection lost: ConnectionLost` in the panel.
- **The UNARMED pickup is now card 057 (playtest blocker).**
  - Neither the server copy nor the client equips a picked-up weapon. The list's equipped index stays 0 (host log line 948 onwards: no `EquipWeapon*` on the server copy after `PickupWeapon`).
  - Senior static: `CheckToAutoSwitch` is the out-of-ammo switch, not a pickup auto-equip.
  - Card 057 tests the client's switch key and fixes the path if needed.
- The items step 3 did not cover (bug kills on both screens, the host pickup, the card 024 hit) are in STATE Known issues. Card 057 step 3 covers the hit.
