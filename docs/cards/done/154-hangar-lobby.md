# Card 154: The session survives the hangar (a real lobby)

Status: CLOSED, outcome in docs/CARDLOG.md. Was: queued 2026-11-08 (user, playtest: "connection gets lost between missions, this isn't a proper lobby").
Budget: three runs. Play feature.

Read: card 045 (listen on restart, client follow), card 052 (co-op panel, host-mode Browse), card 128 (rejoin),
docs/playtest-2026-11-08.md.

## Why

Playtest 2026-11-08, client log `UBWCoop-menu-20261108-191343.log`: at the mission end the client showed
WB_TrooperLevelEnd (t=258655012). 4 s later the host's game sent "Host closed the connection." (reason 5
FailureReceived, t=258659046) and the client went back to its own 3D_Frontend. Its follow tries to
38.175.72.240:47000 then timed out (t=258701195, 258726613): the host was in the hangar and not listening.

The cause is by design today. `c52_host_browse` (mod/src/ubw/lobby_ui.cpp:1351) leaves a Browse to the menu map
(`kC52MenuMap` "3D_Frontend") alone, and `c45_host_browse` (mod/src/ubw/restart.cpp:42) adds `listen` only for a
restart with 2+ players and never for FromLevelQuit. So the hangar is never a listen server, and every trip
through it ends the session.

## Do

1. Static first: from the host's log of this playtest (ask the user for it) or a test run, list the Browse the host
   makes at mission end and at pause-menu quit (map, options, caller), and which of the two rules above dropped
   `listen`.
2. With host mode on, the host's hangar (3D_Frontend) is a listen server too: the mission-end and quit Browses to
   the hangar keep `listen`, and pressing H in the hangar makes the hangar itself listen.
3. The client follows the host into the hangar (card 045 follow) and stays connected there. When the host starts a
   mission from the hangar, the client travels with it (server travel), with no J press.
4. Check what the client's own hangar does while connected: its menus, its mission select and its save writes stay
   off or follow the host; nothing the client clicks there starts a travel of its own.
5. Out of scope: lobby UI (ready states, player list), host migration.

## Done when

- A run: host and client finish a mission (FAST route or a test command that ends it), both land in the host's
  hangar connected (decided 2026-11-08, option A: every host Browse is a hard travel, so the client logs one
  FailureReceived per host travel and the card 045 follow reconnects it with no J press; no ConnectionTimeout, no
  failed follow try), the host starts the next mission,
  and the client loads it and gets its minimap origin and Pong with no J press.
- The same for the pause-menu quit to the hangar.
- The usual Report items (WARN diff, run length, rc, ERROR/FATAL, CRASH, dumps, DLL hash). It ends with "Report
  complete."

## Decision (senior, 2026-11-08)

Option A: with host mode on, every local host Browse on a listen server keeps `listen`, whatever the player count
and including FromLevelQuit; the client drops and the card 045 follow reconnects it into the hangar and into the
next mission. SERVERTRAVEL (option B) bypasses the game's OpenLevel path (loading screen, level-end save) and is
left out. Report the time from the host's Browse to the client's Pong for each travel.

## Run plan r1 (junior, 2026-11-08)

Build: DLL e44f913fd2f97bbf (card 154 code, plus the batched card 146 items 1 and 2 from the senior's message).
Scenario `tools/scenarios/c154-r1.txt`, FED_C1M1 with FAST=1. It makes three host travels:

1. **Hangar trip, normal order.** The host quits from its pause menu (QUIT, then ConfirmQuitToMainMenu). The client
   follows into the listen hangar and connects. Then the host runs `travel restart` (TransitionToLevel, the same
   call the hangar's mission start makes) and the client follows into FED_C1M1.
2. **Race.** The host quits to the hangar again and runs `travel restart` as soon as its own 3D_Frontend world
   loads, before the client has reconnected there. The client follows straight into the mission (a late join).

The mission-end trip (WB_TrooperLevelEnd, then a Browse to 3D_Frontend with FromLevelComplete) has no test trigger.
The Browse and its options are built in Blueprint, the option strings are not in the exe, and the last objective
(Asteroid_O1, a presence objective) appears only near the end of the mission. On the mod side that Browse takes the
same path as the pause-menu quit's Browse: c45_host_browse now adds `listen` to every local Browse, without
checking FromLevelQuit or the player count. On the client it is the same FailureReceived and card 045 follow. This
run does not prove it. The friends' next mission end does.

The card 146 item 1 spot check (the rejoin pawn's spot at +2 and +5 s) does not fit this run: it has no all-down
rejoin. It is left out, not widened in.

### Predictions

- Host, each of the four Browses (two to 3D_Frontend, two to FED_C1M1): `card154: host t=... Browse to "..." with 2
  players (FromLevelQuit 1|0): listen added`. The FromLevelQuit one is the case the old code skipped.
- Hangar: `card154: H: hangar ...` is not expected, because the Browse URL carries `listen` and the hangar comes up
  as a listen server. The host's `card052: ... world 3D_Frontend` line shows net mode 2.
- Client, each host travel: exactly one FailureReceived, then the card 045 follow (Armed, then Retrying) and
  `card045: client ... reconnected`. There is no ConnectionTimeout, no `follow gave up`, no `follow stopped`, and no
  J press. The scenario never sends J.
- Race: the client's follow try to the hangar either fails or is overtaken by the host's mission Browse. The client
  ends connected in FED_C1M1 with its own pawn and `card120: client ... minimap origin ... set: Pong`. No
  `WARN card154: client ... own Browse ... refused` line is expected, because the client's frontend starts no travel
  of its own. If one appears, the refusal blocked a travel the frontend tried, and that is reported.
- Browse to Pong time: from the host's card154 Browse line to the first `card026: host pong 1` after it. Expected:
  under 15 s for the hangar trip and under 30 s for the mission trips. The mission trips include the client's map
  load and the card 120 minimap gate.
- Client hangar: no `card121: client ... save` line runs a save engine write (any save is skipped), and there is no
  client Browse to a map other than the host's.
- Usual: rc 0/0, 0 ERROR/FATAL, no CRASH, no new dump. The WARN diff is checked against c113a.

## Run r1 result (c154-r1, DLL e44f913fd2f97bbf)

The scenario passed in 84 s with rc 0/0, 0 ERROR/FATAL and no new dump. Every host Browse logged
`card154: ... listen added`, including both FromLevelQuit Browses. Each host travel with the client connected gave
one FailureReceived, and the follow reconnected on try 1: 4.6 s to the hangar, 8.0 s into the mission and 8.4 s in
the race. There was no ConnectionTimeout, no failed or stopped follow, no card154 client refusal, and no J press.
In the race, the host left the hangar 0.55 s after it loaded, before the client's first try. That try connected
straight into FED_C1M1 as a late join.

There was one defect. The client in the connected hangar never answered a host Ping. Card 138's BeginPlay hold
started on the 3D_Frontend GameState and waited for a BP_PlayerCharacterMI pawn that the hangar never has (its pawn
is BP_Pawn_3DFrontend_C). So the hangar's BeginPlay never ran on the client, and the Pong stayed held until the
next LoadMap ("WARN card138: client hold dropped at LoadMap after 5317 ms"). The second WARN came from card 156's
+5 s check: it was armed in the hangar, ran late during the mission load, and read that map's half-built HUD.

Fixes (DLL a141d8ada6376e54):
- begin_c138.cpp: no hold on the menu map. It logs `card154: client ... no hold: GameState ... on the menu map`.
- death.cpp and hooks_ubw.cpp: c156_arm_map drops a pending +5 s check at every new map.

### Predictions r2 (same scenario)

- The client in the hangar logs `card154: client ... no hold ... menu map`. The host logs a `card026: host pong N`
  during the hangar stay, so the hangar Browse-to-Pong time can be measured.
- No `WARN card138` line and no `WARN card156` line. Only the attach and god-mode WARNs remain on each side.
- Everything else as in r1.

## Report r2 (c154-r2, DLL a141d8ada6376e54)

Run: scenario `tools/scenarios/c154-r1.txt`, 83 s, host rc 0, client rc 0, 0 ERROR/FATAL, no CRASH line, no new
crash dump, drive rc 0. WARN diff: host 2 and client 2 (DisableThreadLibraryCalls and the god-mode-off line on each
side, the same baseline as c146-r2). The r1 card138 and card156 WARNs are gone.

What changed (code, all in this DLL):
- restart.cpp c45_host_browse: with host mode on, every local Browse keeps `listen`, whatever the player count and
  including FromLevelQuit. It logs `card154: host ... listen added`.
- restart.cpp c154_client_refuses, plus net.cpp/net.h browse_enter returning bool: while the client is in the host's
  session or following it, the client's own Browse to a mission map is refused (returns Failure) and logs a WARN.
- lobby_ui.cpp: the panel stays on in a listen hangar. c154_listen_here starts a listen on a standalone hangar when
  host mode is turned on there.
- begin_c138.cpp: no BeginPlay hold on the menu map (the r1 defect, see above).
- death.cpp and hooks_ubw.cpp: c156_arm_map drops a pending +5 s check at a new map.
- The batched card 146 items 1 and 2 (objectives.cpp side spot, death.cpp c156 Up-only check). Their proof is
  card 146's, not this run's.

Per host travel (host Browse to the host's `card026: host pong` for the client):

| Trip | Host Browse t | Client FailureReceived | Follow | Host Pong t | Browse to Pong |
|---|---|---|---|---|---|
| Mission to hangar (pause quit, FromLevelQuit 1) | 272090730 | 1 (272090749) | try 1, reconnected 4634 ms after the drop | 272099122 (pong 3, BP_PlayerController_3DFrontend_C) | 8.4 s |
| Hangar to mission (`travel restart`) | 272100753 | 1 (272100756) | try 1, reconnected 7975 ms after the drop | 272111083 (pong 4) | 10.3 s |
| Race: hangar Browse, then mission Browse 0.64 s after the hangar loaded | 272120912, 272121550 | 1 (272120929); none for the second Browse, which had 1 player | try 1 at +3 s went straight to FED_C1M1, reconnected 8397 ms after the drop | 272131674 (pong 7) | 10.8 s from the hangar Browse, 10.1 s from the mission Browse |

The hangar's 8.4 s is a 4.6 s reconnect plus a wait for the host's next 5 s Ping. In each mission trip the client
logged `card120: ... minimap origin ... set` within 15 ms of the Pong.

Checks:
- There is no ConnectionTimeout, no `follow gave up`, no `follow stopped`, and no J press: the scenario sends no J,
  and every try was try 1.
- No `card154: client ... own Browse refused` line. Between the hangar drop and the next host travel, the client's
  only Browses are the follow's. No client save engine write appears in the hangar. The scenario clicks nothing in
  the client's hangar, so the refusal's effect on a menu click is shown statically only (c154_client_refuses).
- Race: the host left its hangar before the client's first follow try, and that try joined the mission (a late
  join) with no extra drop.

Not proven in this run:
- The mission-end trip (WB_TrooperLevelEnd, then a Browse with FromLevelComplete) has no test trigger. On the mod
  side it takes the same c45_host_browse and client follow path as the pause quit. The friends' next mission end
  is its proof.
- The card 146 item 1 spot log (+2 and +5 s): there is no all-down rejoin in this scenario.

Runs used: 2 of 3. DLL a141d8ada6376e54 (build/dwmapi-c154-a141d8ada6376e54.dll).

Report complete.
