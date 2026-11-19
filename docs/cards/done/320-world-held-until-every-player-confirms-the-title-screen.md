# 320 The world is held until every player confirms the map title screen

User idea, 2026-11-14: "I don't think the client should be added to the world until you press confirm on the map
title screen. Would that help with syncing?" Related user report: "when i spawned into the map i wasn't next to the
host."

Senior trace 2026-11-14, mod source and playtest logs. What happens today is settled. The hold design below is not
coded; step 1 settles the two open points first.

## What happens today (settled)

1. Host loads the map. `ASTGameplayGameMode::BeginPlay` calls `GameStarted` on the host HUD: the title screen
   (`WB_Loading_Screen_Final_C`) goes on the host's `WB_Stack_C`, which requests a game pause.
2. The pause reaches `UGameplayStatics::SetGamePaused` every frame while the screen is up. Card 096 skips it while a
   client is connected, and card 120 lifts a pause already in force at the client's first Pong
   (`pause_c96.cpp`, `c96_lift_on_join`). Host log `logs/playtest-host-20261107-1416-live.log` line 9874:
   `pause lifted: client connected while paused (first Pong)`, then `SetGamePaused(1) skipped` one ms later.
3. So from the client's first Pong the world runs while the host still reads its title screen: bugs spawn, move and
   attack, scripts and timers advance, the host's pawn stands unattended.
4. The client's pawn is spawned at login, before any screen. Its own title screen comes only after the host leaves
   its screen: `c135_note_unpause` sends `IntroStart` at the host's continue (or `c135_join_replay` for a later join).
   Client log `logs/playtest-menu-20261113-183002.log` line 2270: `client GameStarted ... world paused 0`. The client
   then reads its screen with the world running and its pawn in it.

Result: each player's screen covers a different stretch of live world, and whoever confirms last starts behind.
Everything the join replay must carry (card 323's gaps) exists only because the world moved before the client
looked.

## Does the idea help syncing

Yes, as a hold for both players, not as a late add of the client. Keeping the client out of the world until its
confirm would leave the host's world running without it and make every join a late join (the hardest sync case).
Holding the world until both confirmed makes the map start the same moment for both, with nothing to replay.

## Design (one rule: the title screen holds the world until every connected player has confirmed)

1. Host: at its screen's pause (`c135_note_pause`, screen seen), send `IntroStart` to every ponged client at once,
   and from `c50_join_replay` while the screen is still up. The client shows its screen as soon as its HUD and pawn
   are live (`c135_step` unchanged).
2. Hold: while the hold is on, card 096 lets the pause through and card 120 does not lift it. The hold is on from
   the host's screen pause until the host has continued and every connected client has sent a new op `IntroDone`
   (client: its remembered stack empties, the mirror of `c135_note_unpause`). The host keeps its own pause requester
   for the hold (it does not rely on the screen's requester, which goes at the host's continue).
3. Release: the last confirm, a client disconnect, or a client that never reaches its screen (the existing 60 s
   `kC135WaitMs` bound, reported by a `WARN card320` row). After release card 096 and 120 behave as now.
4. A client that joins after the release: unchanged (join replay, its own screen over a running world). Card 323
   covers that path.
5. Each player sees who is waited for: one line on the screen or the mod's status text, "waiting for <player>".
6. Rows: `card320: host t=<ms> hold on (<map>), waiting for host <0|1> clients <n>`, `card320: host t=<ms> confirm
   from <host|client slot>`, `card320: host t=<ms> hold off after <ms> ms (<all confirmed|disconnect|timeout>)`,
   client `card320: client t=<ms> IntroDone sent`.

## Step 1 findings (senior, 2026-11-14)

- The mod's RPCs run while the host is paused: the client's first Pong reached a paused host
  (`logs/playtest-host-20261107-1416-live.log` line 9874, the Pong is what lifted the pause).
- A paused listen server pauses the client's world: card 120 records the client frozen about 16 s under a host
  screen pause (`docs/cards/done/120-lift-pause-on-join.md`). That freeze is what the hold wants.
- The client's `c135_step` runs from the engine tick, not the world tick, so `GameStarted` is still called under
  the pause. The same screen takes input on the host under its own pause.
- Not settled statically: the client's screen prompt and its confirm under a replicated pause. The run's first
  prediction row (`client ... screen prompt ready` with `world paused 1`, then `IntroDone sent`) settles it; if it
  fails, the fallback below applies.

## Step 1, second pass (senior, 2026-11-14, mod source): settled, code may start

- Sends under a host pause: every mod step, the pace queue drain (`c140_pace_step`) and `cine_skip_step` /
  `c135_step` run from the `UGameEngine::Tick` detour (`hooks_ubw.cpp`), not from the world tick. So `IntroStart`,
  the join replay, Ping/Pong and the card 139 gate all run while the world is paused. The join replay then describes
  a world that does not move, which is what card 323 wants.
- A client can finish joining under the pause: in playtest 3 the client loaded in and stood frozen under the host's
  pause menu (card 120's log). Login, pawn spawn and possession do not need the world tick.
- Where the hold goes: `detour_c96_set_game_paused` is the only gate. With the hold on it calls the original for
  `paused` true even with clients connected, and `c96_lift` returns early. The hold's own pause is the mod calling
  `g_orig_c96_set_game_paused(pc0, true)` each engine tick while the hold is on and the host's screen has closed
  (`USTGameInstance::UpdatePause` stops asking once its requester list is empty), and
  `g_orig_c96_set_game_paused(pc0, false)` once at release, unless the game's own requesters still hold a pause
  (then card 096 behaves as now).
- The client's own `SetGamePaused` calls do nothing (no authority; client log `world paused 0` at `GameStarted`),
  so the client's screen closing cannot lift the hold. `IntroDone` is the only release from the client.
- Still decided by the run, not statically: the client screen's prompt and confirm key under a replicated pause.
  The host's same screen takes its key under its own pause, so the widget's input path is pause-safe on the host.
  Fallback if the client's confirm row is missing: the hold freezes the world by time dilation 0 on the host
  (replicated in `AWorldSettings`) instead of a pause; nothing else in the design changes.
- Test builds: with the screen skipped on both sides (`cine_skip_step`, no `UBWCOOP_C135_KEEP_SCREEN`) the host's
  stack empties at once and the client's `IntroDone` follows its own auto-continue; `host ui continue` is the
  host's confirm. The hold must log `hold on` and `hold off` in that case too, with a duration under 2 s.

## Sibling sweep (report before coding)

Every other screen that one player reads while the world runs for the other: the mission brief movie, movie
cutscenes (Enter to advance), the map-end screen, the death and respawn screen, the pause menu (card 096 keeps the
world running there on purpose; unchanged). For each: which side shows it, whether the world should hold, and what
the user sees now.

## Predictions (one run, C1M1 from the menu with the title screen kept on both sides)

- Host `hold on` row before any NPC `world` row of the map; zero NPC deaths, damage rows or objective rows between
  `hold on` and `hold off`.
- Client `GameStarted` within 3 s of the host's `screen up` row, with `world paused 1`.
- `hold off` only after both confirm rows; digest round 1 after the release: 0 mismatches.

## Done when

The predictions hold, both scenarios' auto-continue still passes, and in the next playtest both players leave the
title screen into the same untouched map start.

## Closed

Closed 2026-11-17 (senior), build b174 `4efbc7838d907625`: c460b170a hold rows; c469b174c on FED_C2M1: shot pair inside the hold (both on the load screen) and after `hold off after 9617 ms (all confirmed)` (both in the world). FED_C1M1 with clients 0 is a late join into a running map, not a gap.
