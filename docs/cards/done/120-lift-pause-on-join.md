# Card 120: A client that joins while the host is paused can move

Status: written 2026-11-06 (senior); sent, fixed at review and accepted 2026-11-06.

Read:

- card 096 Report (`mod/src/ubw/pause_c96.cpp`, `detour_c96_set_game_paused`, the "Open" point under "Other
  pausing screens");
- `c50_pong` in `mod/src/ubw/weapons.cpp` (first Pong per map: `ponged`, replays).

Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`. DLL base: `4b162843fc154d5f` (card 119).

## Why

User (playtest 3, 2026-11-06): "client can't move while i'm paused still."

Host log (`logs/playtest-host-20261106-pause.log`, a copy taken mid-session):

```
91282742 card021: host AbilityInputTagPressed InputTag.Pause            (host opens the menu, client still loading)
91283369 card096: SetGamePaused(1) = 1 (world paused), 0 client(s) connected
91285539 card026: host pong 1 from pc BP_PlayerController_C              (client's first Pong)
91301916 card096: SetGamePaused(0) = 1 (unpaused)                         (host closes the menu)
```

This is card 096's open point. The skip only stops new pause calls. A pause already in force when the client's
first Pong arrives stays until the host closes the screen. The client was frozen for about 16 s. The same happens for
the mission brief and "PRESS ANY KEY" screens, which hold the same requester.

## Do

1. **Measure (static or one log line).** Does `USTGameInstance::UpdatePause` call `SetGamePaused(1)` every frame
   while a requester is held, or only on a change? How does the mod read whether the host world is paused now
   (`UGameplayStatics::IsGamePaused`, or the world's pause field)?
2. **Fix, host only, in `pause_c96.cpp`.** When at least one client is connected and the host world is paused, the
   world is unpaused once (the original with `paused` false), and a line is logged: `card096: host t=... pause
   lifted: client connected while paused`. Hook it where it fires for both cases:
   - the first Pong of a client (`c50_pong`), and
   - a `SetGamePaused(1)` call that is skipped while the world is still paused.
   Leave `RequestGamePause` / `RequestGameUnpause` and the requester arrays alone, so the menu stays open on the host
   and closes as before. Do not call the unpause every frame.
3. **Proof, one run:** FED_C1M1, FAST=1. The host opens its pause menu before the client joins (the runner already
   has `CLIENT_DELAY_MS`). The client joins with the menu still open.
   - The host log shows the pause, then the client's first Pong and `pause lifted`.
   - The client walks (one `goto`, distance and time) while the host's menu is still open.
   - The host closes the menu; both move. rc 0, ERROR/FATAL 0, no CRASH line, no new dump.

Stop when the fix builds and that run shows it; the user will playtest.

## Done when

The Report has the UpdatePause call pattern, how the paused state is read, where the lift runs, the run lines, the
client's walk during the host menu, the run length, rc, ERROR/FATAL, CRASH lines, dumps and the DLL hash (sha256
prefix). Ends with "Report complete."

## Report

### Step 1: measurements

- **UpdatePause call pattern.** USTGameInstance::UpdatePause (exe+0x510CDE0) calls UGameplayStatics::SetGamePaused
  from exe+0x510CE24 on every frame while a pause requester is held. It is not called once per edge: the card 096
  skip lines in c96-pause-host.log come 2-10 ms apart. The game instance (BP_GameInstance_C) is the world context.
- **How the paused state is read.** UWorld::IsPaused (`g_world_is_paused`) on `g_actor_get_world(pc)`, where the PC
  is `g_get_player_controller(ctx, 0)`. On the first Pong, it is the server copy of the client's PC.

### Step 2: fix (mod/src/ubw/pause_c96.cpp)

- `c96_lift(pc, why)`: if the PC's world is paused, it calls the original SetGamePaused(pc, false) once and logs
  `card096: host t=... pause lifted: client connected while paused (<why>), SetGamePaused(0) = <ok>`. If the
  world is not paused, it does nothing.
- It runs in two places:
  - The first Pong of a map. `c50_pong` (weapons.cpp) calls `c96_lift_on_join(pc)` after `c98_replay` (declared in
    hooks_internal.h).
  - The card 096 skip branch (a SetGamePaused(1) skipped while a client is connected), with the host PC 0. If the
    world is already unpaused, this is a no-op.
- RequestGamePause, RequestGameUnpause and the requester arrays are unchanged. There is no per-frame unpause: once
  the world is unpaused, the card 096 skip keeps it running.

### Late-join crash found by the proof (fixed: mod/src/ubw/chan_ops.cpp)

Run c120c (client joining at 150 s, host menu open) proved the lift. The **client** then crashed 7 ms after the lift.
Dump: UECC-Windows-D90734E143F40FD00E001E83269EB93C_0000. The user saw it as "fatal error".

- **Crash stack:** ClientMessage → card 049 journal replay AddSubObjective →
  USTLevelObjective::CreateNewLevelObjective → ASTLevelObjectiveMarker::ObjectiveUpdated+0x114 →
  USTMinimapComponent::ShowOnMinimap+0x55 → GetMinimapLocation+0x3B. The faulting instruction is
  `mov rax,[rdi+0x108]; mov r8,[rax+0x1B8]`, with rax 0.
- **Cause:** +0x108 is USTMinimapComponent::OriginActor (written by SetOriginActor; its only native caller is
  ASTPlayerController::OnPossess+0x252, through PC+0x7B0, `kPcMinimap`/`kMinimapOrigin`). ObjectiveUpdated reaches
  the minimap through GetSTPlayerController → FindComponentByClass. The journal arrived 71 ms after the client's
  ClientRestart with a null pawn, so the origin was still null. The earlier runs (c119g, c120a, c120b) were all
  joins where the replay came seconds after the pawn, which is why they did not crash.
- **Fix:** every host join replay starts at the client's first Pong (`c26_host_pong`):
  - the card 049 journal;
  - card 056;
  - card 098, through c50_pong;
  - card 114;
  - the card 096 lift.

  So the client now answers no Ping until its PC minimap has an origin (`c120_client_ready` in
  `c26_client_ping`). This is latched once per world; after that every Ping is answered. It logs
  `card120: client ... Pong held` once, `... minimap origin ... set` when it releases, and a WARN if the hold
  passes 30 s.
- **Sibling sweep:** these host sends are gated on `ponged` or `c49_synced`, so they now wait for the same gate:
  - c49 live sends;
  - c95 doors;
  - c98 world;
  - grenades (grenades.cpp:492);
  - c50 weapons.

  That makes one mechanism for the join class. The sends with no ponged gate were not changed: c41 anim, c42 gib,
  c43 down, c46 warp, c71-c74 ammo and grenades, and c104 charge. None of them goes through the objective/marker
  path, but I did not verify each one against a null client pawn.

### Step 3: proof run c120d

Command: `SCENARIO=tools/scenarios/c120-pause-join.txt FAST=1 DEPLOY=1 CLIENT_DELAY_MS=150000
HOST_EXTRA_ENV="UBWCOOP_AUTODROPSHIP=1" tools/run_pair.sh c120d 380`

Host log, in order:

```
[92817859] card096: host t=92817859 SetGamePaused(1) = 1 (world paused), 0 client(s) connected; context BP_GameInstance_C, caller exe+0x510CE24
[92835626] card049: host t=92835626 journal replay to BP_PlayerController_C (first Pong of the map): 40 of 40 frames in 0.119 ms
[92835627] card096: host t=92835627 pause lifted: client connected while paused (first Pong), SetGamePaused(0) = 1
[92835627] card096: host t=92835627 SetGamePaused(1) skipped: the world keeps running, 1 client(s) connected; ...
```

(The pause at 92817859 is the drive's `host key Escape` at +166.3 s; the menu stayed open until +204.3 s.)

Client log:

```
[92835598] possess: ClientRestart(..., arg=0000000012099CB0) ... Pawn 0000000012099CB0 BP_PlayerCharacterMI_C
[92835615] card120: client t=92835615 minimap origin 0000000012099CB0 (BP_PlayerCharacterMI_C) set: Pong to the host from now on (held 0 ms)
[92835685] card049: client t=92835685 replayed AddObjective(Assault_O1) flags 0xB count 0: objectives 0 -> 1
```

- **Client walk during the host menu:** `goto -3454 9600`, path 12231 uu in 16.9 s, reached, 0 stucks
  (92837802 → 92854685).
- **After the host closed its menu (+204.3 s):**
  - host goto: 8565 uu in 12.0 s, reached;
  - client goto: 810 uu in 1.5 s, reached.
- **Run:** ended by the scenario after 227 s, drive rc 0. Host rc 0, client rc 0.
  - ERROR/FATAL: 0 on both sides.
  - Host WARN 1 (`autodropship: 1 PlayerController(s); unpause action in 3000 ms`, expected for a solo start).
    Client WARN 0.
  - CRASH lines: 0 on both. New dumps: none.
- **Gate not exercised:** in c120d the first host Ping came 17 ms after the origin was set, so no hold was needed
  ("held 0 ms", no `Pong held` line). The hold branch did not run in this run. In c120c it would have held: the
  replay there came 71 ms after a null-pawn ClientRestart.

### Runs

| Run | Setup | Result |
|---|---|---|
| c120a | CLIENT_DELAY_MS 60000, no AUTODROPSHIP | Lift at the first Pong during the mission-brief pause; the client walked 12231 uu in 16.9 s; 186 s, rc 0/0, WARN 1 (SKIP_FLIGHT needs AUTODROPSHIP), no dumps |
| c120b | CLIENT_DELAY_MS 60000, AUTODROPSHIP=1 | Same mission-brief case; 166 s, rc 0/0, WARN 0, no dumps |
| c120c | CLIENT_DELAY_MS 150000, host menu open | Lift proven; client crashed in the journal replay (above); drive failed at line 11, 380 s |
| c120d | Same as c120c, Pong gate | Pass (above) |

**DLL:** 0ad371e8ce78ed8c (sha256 prefix), saved as build/dwmapi-play-0ad371e8ce78ed8c.dll. Base 4b162843fc154d5f +
pause_c96.cpp lift + chan_ops.cpp Pong gate.

Report complete.

## Senior review (2026-11-06): one fix before acceptance

The lift checks out: `c96_lift` runs only when the world is paused, it runs once per first Pong and is a no-op in the
skip path after that, and the requester arrays are untouched. The c120d lines and hash 0ad371e8ce78ed8c match.

Defect: the card 120 gate (`c120_client_ready` in `mod/src/ubw/chan_ops.cpp`) keys "ready" on the UWorld address
only. `g_c120.ready_world` is never cleared on a map change. When the client's new world after travel, restart
or a card 045 rejoin is allocated at the old world's address (the allocator reuses freed blocks of the same size),
the gate counts the new map as ready before its minimap has an origin. The first Pong then goes out at once, and
the c120c crash (journal replay to a null OriginActor) is back.

Do:

1. Reset `g_c120` to `C120Gate{}` at the client's per-map arm (`c50_arm_map` or the client LoadMap point the card
   048 per-map reset uses), so readiness is per map load and not per address. Keep the world check as a second guard.
2. Say in one line whether the client ever answers a host Ping while its PC is not an `ASTPlayerController`
   (3D_Frontend, a lobby). If it can, that PC is answered at once (no minimap, so no marker path), and you name
   which; if it cannot, say why.
3. No new run: the reset is shown by code. Rebuild and report the hash.

Append the result under this section and end with "Report complete." again.

### Fix for the senior review

1. **Per-map reset.** `c120_arm_map()` (chan_ops.cpp) sets `g_c120 = C120Gate{}`. It is called from `c50_arm_map`
   (sprint.cpp), which runs from `client_diag_load_map_return` on every real map load on both sides: travel,
   restart and the card 045 rejoin. So readiness is per map load. The world-address check in
   `c120_client_ready` stays as a second guard. Declared in hooks_internal.h next to `c50_arm_map`.
2. **PCs that are not ASTPlayerController.** While connected to a host, the client's only PC is
   BP_PlayerController_C, an ASTPlayerController: the client joins straight into the host's map, and 3D_Frontend
   has no host connection, so no host Ping reaches it there. As a guard, a PC that is not an ASTPlayerController
   is now answered at once and not latched. +0x7B0 is an ASTPlayerController field, and such a PC has no minimap,
   so there is no marker path.
3. Rebuilt, with no new run. DLL **acf99bc06558be8e** (sha256 prefix), saved as
   build/dwmapi-play-acf99bc06558be8e.dll.

Report complete.
