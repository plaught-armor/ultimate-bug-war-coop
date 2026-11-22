# Card 052: In-game co-op lobby (host and join from the main menu)

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: sent 2026-11-04. The user made multiplayer setup UX a priority.

Read `docs/STATE.md`, the card 031 Report and Senior review (the native UMG overlay), the card 045 Report (Browse URL `listen` in place; the client's reconnect uses `g_travel.url`), and `docs/PLAYTEST.md`. Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

Today a co-op game needs launch-time environment variables (`UBWCOOP_TRAVEL`), and the host is dropped straight into FED_C1M1, which skips the game's own campaign flow (the host logs chapter 0, level 0). Players should instead start the game normally and choose co-op at the main menu:
- **Host:** turn co-op hosting on, then pick a mission through the game's own menus. The mod adds `listen` to that Browse, as card 045 already does for a restart.
- **Join:** type the host's IP and press Enter.

## Static facts (senior, 2026-11-04)

- Card 031 built a native overlay at the main menu: a widget from `CreateWidgetInstance(pc)`, its canvas root cleared, then a `TextBlock` and an `EditableTextBox` built with the `WidgetTree` as outer, and `AddToViewport(z 100)`. F8 toggled it. Typed text reached the box, and Enter read it through `FText::ToString`. Card 031's open points: the box needs an explicit style (light grey on light grey today), the size write to `LayoutData` did not take, and F8 also reaches the title screen.
- The widget does not survive travel and rebuilds cleanly. Keep weak pointers only, as card 031 does.
- `c45_add_option(url, L"listen")` appends an option to the Browse URL copy with the engine allocator. Today `c45_host_browse` only adds it for a listen server with 2 or more players.
- `travel_tick` (hooks.cpp ~150) calls `g_set_client_travel(engine, world, g_travel.url, kTravelAbsolute)` once when `g_travel_armed` is set. Card 045's client reconnect reads `g_travel.url`.
- Failure notifications to hook for the join status: `UEngine::HandleNetworkFailure` 0x043E04F0 `(this, UWorld*, UNetDriver*, ENetworkFailure::Type, const FString&)` and `UEngine::HandleTravelFailure` 0x043E1200 `(this, UWorld*, ETravelFailure::Type, const FString&)`.
- For the host's own address: `FSocketSubsystemBSD::GetLocalHostAddr` 0x019DE4C0. A plain Win32 `GetAdaptersAddresses` (iphlpapi) also works; the DLL imports KERNEL32 only today, so resolve it with `LoadLibraryW` and `GetProcAddress` as card 031 does for user32.

## Do

1. **Panel.** Turn the card 031 overlay into the co-op panel. It is on by default, needs no environment variable, and appears only at the main menu (`3D_Frontend`) in net mode 0. F8 toggles it. Swallow F8 so the title screen does not see it.
   Content, top to bottom:
   - `UBW CO-OP`
   - `[H] Host co-op: OFF` / `ON`. When it is on, add a line `Players join at: 192.168.x.y` that lists the host's IPv4 LAN addresses (skip loopback; show at most 3), and a line `Now start a mission from the menu.`
   - `[J] Join:` then the IP box, prefilled with the last address. Accept `a.b.c.d` or `a.b.c.d:port`; the default port is 7777.
   - A status line: `Connecting to a.b.c.d:7777...`, `Could not connect: <reason>`, `Invalid address`.
   Give the text and the box readable colours (white text on a dark translucent backing). Fix the box size, with a `SizeBox` wrapper or a reflection-correct `LayoutData` write; say which one worked.
2. **Host.** While host mode is on, every Browse from net mode 0 to a local map other than `3D_Frontend` gets `listen` appended (`c45_add_option`). Log the URL before and after. Leave card 045's restart rule as it is. Turning host mode off stops the appends.
3. **Join.** Enter in the box (or J then Enter) validates the address, copies it into `g_travel.url` (so card 045's reconnect uses it), sets `g_client_session`-related state the same way the env-var path does, and arms the existing travel (`g_travel_armed`, delay 0). Set the client switches that `UBWCOOP_CLIENT_GAMEONLY=1` sets today, so a joined client behaves the same as the scripted client.
4. **Failure.** Hook `HandleNetworkFailure` and `HandleTravelFailure` (call the original first). While a join is pending or in session, log the type and message, and show `Could not connect: <type>` on the panel once the client is back at the menu. Rebuild the panel open after that return. A failed join must leave the game at a working main menu.
5. **Remember.** Save the last join address and the host flag to `UBWCoop.ini` in the game's `Saved` folder (`%LOCALAPPDATA%\StarshipTroopers_UBW\Saved\`). Read it at startup. Keep the format to two `key=value` lines and validate them on read; a bad file is logged and ignored.
6. **Keep the test path.** When `UBWCOOP_TRAVEL` is set, it wins and the panel only shows the status. `tools/run_pair.sh` must keep working unchanged.
7. **Runs.** All windowed 640x360 side by side.
   - `card052h`: host and client start with **no** `UBWCOOP_TRAVEL`. The host turns host mode on through the panel (scripted keys are fine), then starts the campaign's first mission through the game's menus. The client types `127.0.0.1` and joins. Take shots of both panels and of both machines in the mission. The host log shows the game's real chapter and level, not 0/0 from the direct travel.
   - `card052f`: the client joins with no host running. It shows `Could not connect`, and the menu still works (a shot after the failure).
   - `card052s`: solo with host mode off. Behaviour is unchanged, and the Browse URL is never touched.
   - Rerun `card049x` or `card050x` with `run_pair.sh` to show the env-var path still works.

## Done when

- A two-player game starts from the main menu on both machines with no environment variables, through the panel and the game's own mission select.
- A failed join returns a readable reason and a working menu.
- The last address and the host flag survive a restart of the game.
- `tools/playtest.sh` gets a `menu` role that launches with no `UBWCOOP_TRAVEL`, and `docs/PLAYTEST.md` describes the panel flow. Keep the old roles.
- rc 0 on all runs, no new dump, the build is clean with `/W4 /WX`, and every function is 60 lines or fewer.

## Risks

- Senior static note: the frontend's mission flow is `WB_SPCAMPOS_3_TrooperLevelSelect`, then `WB_HANGARCAMPOS_2_TrooperMissionBrief` (`WB_MissionBrief_Base`). `BP_GameInstance` references `WB_Loading_Screen_Final`, a loading widget, and no transition map is listed under `Content/Maps/3D_Frontend` other than the frontend's own sublevels. So a direct `OpenLevel` (which reaches `UEngine::Browse` through `SetClientTravel`) is the likely path. Confirm it from the log.
- The game's mission select may travel through a loading map, or use `ServerTravel` or `OpenLevel` rather than `Browse`. Log every Browse and LoadMap URL from the menu to the mission. If `listen` lands on a transition map and is lost on the way, add it to the URL that reaches the gameplay map, and report.
- A Browse with `listen` from the menu may open the socket before the mission's own map has loaded. That is fine as long as the client's join only connects after the host is in the mission. Note the time from host travel to `UWorld::Listen` returning true.
- Typing in the box while the title screen also takes keys. Card 031 used `SetInputMode_UIOnlyEx` with the box focused. Restore the game's recorded mode on close, as card 031 does.
- Senior static path (2026-11-04, disassembly): the mission brief's `OnStartGame` goes to `USTGameInstance::TransitionToLevel(chapter, level, FString options)` 0x05109340. That calls `ShowAsyncLoadingScreen`, then `USTGameInstance::OpenLevel(FString)` 0x050FA520. `OpenLevel` validates the map name, deactivates the secrets, and calls `UGameplayStatics::OpenLevel` at 0x050FA7B2, which reaches `UEngine::Browse` through `SetClientTravel`. So the mission start comes through Browse once, from net mode 0, and step 2's append is in the right place. `TransitionToNextLevel` 0x05109550 and `RestartCurrentLevel` 0x050FFC30 use the same path, so with host mode on, the next mission is also a listen server. Log what the client does on the host's next-mission travel; card 045's reconnect is the expected path.
- Senior code note (2026-11-04): card 031 reads keys by polling `GetAsyncKeyState` (`c31_poll_keys`, hooks.cpp ~14040) on the game thread, only while the game window is in front. Polling cannot swallow a key. To keep F8 from the title screen, drop F8 in the existing `UEnhancedPlayerInput::InputKey` detour (`detour_player_input_key`, `kHooks` ~19650), or show in a run that the frontend ignores F8 and say so. While the IP box has focus, the panel must ignore its own hotkeys (H, J), or typing in the box changes the host toggle. `c31_step` returns early outside net mode 0, which is right for the panel. Card 031's script mode (`c31_script_step`) can drive the `card052h` key presses.
- Senior code note (2026-11-04), two traps in the card 045 code for the menu flow:
  1. `c45_host_browse` (hooks.cpp ~17250) returns at once unless the world's net mode is listen server (2). From the main menu the net mode is 0, so step 2's append cannot just reuse it. Add a separate host-mode branch in `c45_browse_enter`. It runs only when host mode is on, net mode is 0, `!g_client_session`, the URL host is empty, and the map is not `3D_Frontend`. Leave `c45_host_browse` (the restart rule) as it is.
  2. `c45_client_browse` arms the reconnect only when `g_travel.enabled && g_travel.url[0]`. A panel join must set `g_travel.enabled = true` as well as the URL, or a dropped panel-joined client never reconnects. `travel_step` (~150) fires on the first tick after `g_travel_armed` is set again, because `g_world_seen` is already true and `delay_ms` is 0 for a panel join. Set `delay_ms = 0` explicitly, and log the arm.
- Senior code note (2026-11-04), step 3 made concrete: `g_client_session` needs no work from the panel. `detour_load_map` sets it from `pending != nullptr` on the join's LoadMap (~3822). The env-var client path differs from a panel join only in `install()`: `g_travel = travel; g_travel_armed = travel.enabled; g_client_game_only = client_game_only;`. So a panel join writes `g_travel.url`, then `g_travel.enabled = true` and `g_travel.delay_ms = 0`, then `g_client_game_only = true`, then `g_travel_armed = true`, all on the game thread. Nothing else from the environment is client-specific. The `UBWCOOP_C*_TEST` flags are test hooks and stay off.
- Senior note (2026-11-04), for `card052f`: with no host on the address, UDP gets no reply, so the failure only comes when the net driver's initial connect timeout runs out. The UE default for `IpNetDriver` is `InitialConnectTimeout=60` s, unless the game's config changes it. So expect `Connecting...` for about a minute before `HandleNetworkFailure` (`ConnectionTimeout`) fires. Log the time from the arm to the failure. Keep the status line on `Connecting to ...` (with elapsed seconds) during that minute so the player knows it is still trying. A Cancel key (Esc while the panel is open) that sends `disconnect` is optional; add it only if it is cheap.

## Report

**Result: done.** A two-player game starts from the main menu with no env vars (`card052h2`). A failed join shows a readable reason and leaves a working menu (`card052f`). The address and the host flag survive a restart (`card052h2`, `card052s2`). All runs ended with rc 0. The only crash dump came from `card052h`; it is fixed and did not come back in `card052h2`.

### Changes

- `mod/src/hooks.cpp`: the card 052 block, made up of:
  - the panel (`WB_TutorialPrompt_C` host, a SizeBox, the address box, and the status line);
  - H, J and F8 handling, with the panel's hotkeys ignored while the box has focus;
  - F8 dropped in `detour_player_input_key`;
  - a host-mode branch in `c45_browse_enter` (net mode 0, host on, no client session, empty URL host, map not `3D_Frontend`), which appends `listen`;
  - a panel join that sets `g_travel.url`, `enabled`, `delay_ms = 0`, `g_client_game_only` and `g_travel_armed` on the game thread;
  - failure reporting from `HandleNetworkFailure` and `HandleTravelFailure`, which rebuilds the panel at the menu with `Could not connect: <reason>`;
  - Esc while connecting cancels;
  - the `UBWCOOP_C52_TEST` script driver.
- `mod/src/lobby.cpp` and `lobby.h`: address parse and normalisation, `UBWCoop.ini` load and save, and LAN IPv4 listing through `GetAdaptersAddresses`, loaded at runtime so the DLL still imports KERNEL32 only.
- `mod/src/config.cpp`: reads the test flag.
- `tools/symbols.txt`: `USTCommonUtilityLibrary_SpawnSoundAttachedToComponentWithAIAwareness` (see the crash below).
- `tools/run_pair.sh`: `MENU=1` (no travel env on either side).
- `tools/playtest.sh`: the `menu` role.
- `docs/PLAYTEST.md`:
  - a new "Play (co-op panel, no env vars)" section;
  - the 20 s fail time;
  - a note on the next-mission rejoin;
  - the env-var roles kept as a fallback.
- Build: clean with `/W4 /WX`. 84 exports, KERNEL32 only. No function in `hooks.cpp`, `lobby.cpp` or `config.cpp` is over 60 lines (awk check).

### Answers the card asked for

- **SizeBox:** the setter wrappers work. `SetWidthOverride` and `SetHeightOverride` through ProcessEvent read back 300 x 40 (`card052s`).
- **Box text:** it was light gray on the light gray box. `SetForegroundColor` through ProcessEvent with `{0,0,0,1}` makes it black and readable (`card052s2`, ScreenShot01311). Only `execSetForegroundColor` exists in the PDB; no WrapperImpl.
- **Host travel to Listen:** `listen` is appended to the menu's single `UEngine::Browse` (from `TransitionToLevel` and `OpenLevel`, as the static note said). `UWorld::Listen` returned true **330 ms** after that Browse (`card052h2`) and 300 ms after it in `card052h`. The host was then in FED_C1M1 with net mode 2.
- **Join time:** LoadMap succeeded **2.8 s** after the panel armed the join (`card052h2`), and 2.5 s after it in `card052h`. The client was then in FED_C1M1 with net mode 3.
- **Host goes to the next mission:**
  1. `TransitionToLevel(0,1)` loads chapter 1, level 0, which is FED_C2M1. card045 adds `listen` (2 players).
  2. The client gets network failures 5 and 3 (FailureReceived, ConnectionLost) and drops to `3D_Frontend`; the menu loads 1.2 s after the drop.
  3. Card 045's reconnect fires on try 1. The client is back in FED_C2M1 with net mode 3 **9.1 s** after the drop (`card052h2`), and 7.0 s after it in `card052h`.
  4. The player does nothing; PLAYTEST.md says not to press J during this.
- **Fail timing:** with no host, `ConnectionTimeout` (failure 4) comes **20.0 s** after the arm (`card052f`, 20034 ms). The engine logs `UNetConnection::Tick: Connection TIMED OUT ... Threshold: 20.00`, so this game's timeout is 20 s, not the UE default of 60. The status line counts `Connecting to 127.0.0.1:7777... Ns (Esc cancels)`. Then the menu shows `Could not connect: ConnectionTimeout`. After that, host mode turned on, the IPs were listed, and F8 hid and showed the panel, so the menu still works.
- **F8:** the frontend did not send an InputKey event for the first F8 (hide). The show press's down and up events reached the detour and were dropped (`F8 InputKey event kept from the game`). The title screen never reacted to F8.

### Problems found and fixed

- **Empty `address=` line:** the ini saved before any join has `address=` with no value, and the loader rejected the whole file. `parse_line` now accepts it as "no address".
- **Client crash (`card052h`), dump `UECC-Windows-23D507D84706A3BE2A5CA783D40DD8F0_0000`:**
  - EXCEPTION_ACCESS_VIOLATION reading 0x1E0 in `USTCommonUtilityLibrary::SpawnSoundAttachedToComponentWithAIAwareness+0x31`, about 12 s after the client reconnected into FED_C2M1.
  - A Blueprint movement-updated delegate passed a null component, and the function does `movups 0x1e0(%r13)` with r13 = 0.
  - Fix: a guard detour that returns nullptr for a null component and logs `guard: ... skipped` with the caller.
  - In `card052h2` the crash did not come back and the guard did not fire, so the guard has not been seen working on a real null. Watch for that guard line in the 8 pm logs.
- **Test driver:** the reconnect's return to the menu set phase 10 again, which skipped the client's phase 2 rows. Now a menu world sets phase 10 only before any mission.
- **Phase 10 rows:** the host-off and Up H rows were removed from phase 10, so the run ends with host mode on. `card052s2` then proved the host flag survives a restart.

### Test-method note

The test starts the mission by calling `USTGameInstance::TransitionToLevel` directly, which is what the mission brief's `OnStartGame` calls. It does not click through the level select. The menu Browse that follows is the real one. The playtest will be the first time the real clicks are used.

### Runs

| Run | What | End | rc host/client | Dump |
|---|---|---|---|---|
| card052s | SizeBox and panel build | done 62 s | 0 / - | none |
| card052h | host + panel join + next mission | cap 303 s | 0 / 0 | **client: SpawnSound null (fixed)** |
| card052h2 | the same after the fixes | done 148 s | 0 / 0 | none |
| card052f | join with no host, then menu checks | done 73 s | 0 / - | none |
| card052s2 | restart persistence, box colour, host off leaves Browse untouched | done 60 s | 0 / - | none |
| card050x2 | env-var path regression (`HOST_EXTRA_ENV="UBWCOOP_AUTODROPSHIP=1 UBWCOOP_C50_TEST=1"`) | done 61 s | 0 / 0 | none |

`logsum` (tools/logsum.py) totals of ERROR/FATAL and WARN lines per run:

| Run | Host ERROR/FATAL | Host WARN | Client ERROR/FATAL | Client WARN |
|---|---|---|---|---|
| card052h2 | 0 | 0 | 0 | 0 |
| card052f | 0 | 0 | - | - |
| card052s2 | 0 | 0 | - | - |
| card050x2 | 0 | 0 | 0 | 0 |

In `card050x2` the panel shows only `UBWCOOP_TRAVEL set: status only`.

## Senior review (2026-11-04)

Accepted.

Checked:
- Runs: card052s, card052h (one dump, see below), card052h2, card052f, card052s2 and card050x2 all have rc 0 on both sides. The only new dump is `UECC-Windows-23D507D84706A3BE2A5CA783D40DD8F0_0000`, from card052h, before the guard.
- `fnlen.py`: 1127 functions in hooks.cpp and 9 in lobby.cpp, none over 60 lines.
- The `SpawnSoundAttachedToComponentWithAIAwareness` guard: the detour's parameter list matches the mangled name (component, FName, FName, FVector by value, bool, three floats, attenuation, concurrency, bool; the trailing `3` back-reference is the bool). `Vec3` is three doubles, so clang-cl passes it the same way the game's MSVC build does. A null component returns null without calling the original. It has not fired on a real null yet; the playtest logs must be checked for its `guard:` line.
- Shots (game folder `Saved/Screenshots/Windows`, ScreenShot01294 to 01317; the Report does not name them, and later Reports must): the panel with host ON and the LAN address, the join box, `Connecting to 127.0.0.1:7777... 1s (Esc cancels)`, `Could not connect: ConnectionTimeout` over a working menu, the hangar mission select with the panel, and both players in FED_C1M1. The join box text is readable black on light gray in the last runs (ScreenShot01312); in the card052f shots (ScreenShot01307) it was still white on gray, before the colour fix.

Notes for the playtest:
- The panel font is small at 640x360. It is fine at the user's resolutions if UMG's DPI scaling applies; card 055 must take one panel shot at 1280x720 to confirm.
- The test drives `TransitionToLevel` directly. Card 055 goes through the real level select.
