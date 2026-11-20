# Card 096: Host's pause menu freezes the client

Status: accepted 2026-11-05 (senior). Reviewed: skip only for pause=1 with a ponged client; unpause and solo run the original.

Read:

- `docs/playtest-2026-11-05.md`, item 4;
- `mod/src/ubw/hooks_internal.h` around `kPauseRequesters` (card 015 notes on `USTGameInstance` pause requests and
  `UpdatePause`), and `mod/src/ubw/dropship.cpp` `detour_request_pause` / `detour_request_unpause`.

Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`. Play DLL base: `96c41a875d2030cb` (card 095).

## Why

In the 2026-11-05 playtest, when the host opened the pause menu to go into settings, the client's game froze too.
The pause menu goes through `USTGameInstance::RequestGamePause`. While any requester is held,
`USTGameInstance::UpdatePause` (0x0510CDE0) calls `UGameplayStatics::SetGamePaused`, which on a listen server pauses
the server world, so every client stops.

The `RequestGamePause` hook exists, but it is installed only behind `g_gate_c67_diag` (UBWCOOP_CMD or UBWCOOP_DIAG),
and in the playtest it was in the skipped list on both sides.

## Do

1. **Fix:** while at least one client is connected (ponged) to the host, the host's menus must not pause the world. The
   menu still opens and works (settings, resume, quit). Pick the narrowest point (for example, skip the world pause in
   `UpdatePause`, or keep the requester bookkeeping and skip `SetGamePaused`) so requester arrays stay consistent and
   unpausing works the same. With no client connected (solo, or the host before anyone joins), the game pauses as
   before. The hook it needs must be installed in normal play, not only under the diag gate.
2. **Check the other screens that pause:** the mission-brief movie and the "PRESS ANY KEY TO CONTINUE" screen (card 015)
   hold the same pause. With a client connected they should not freeze the client either; confirm the host's own
   screen still closes and play continues. Also check what the client's own pause menu does on the client's game
   (it must not pause the host, and the client's menu should still open).
3. **Proof**, one combined scenario run (FAST=1): with the client connected, the host opens the pause menu (through the
   same widget call the menu makes) for a few seconds while the client moves; the client's position keeps changing and
   the host log shows the pause request skipped for the world. Then the host closes the menu and both play on.
   rc 0, ERROR/FATAL 0, no new dump.

Stop when the fix builds and that one run shows the client moving through the host's pause; the user will playtest.

## Done when

- The Report names the hook point, the fix, what the brief/any-key screens and the client's own menu do, the proof run
  and the DLL hash.
- The Report ends with "Report complete."

## Report

**Hook point.** `UGameplayStatics::SetGamePaused` (0x03C6BBD0, new symbol `UGameplayStatics_SetGamePaused`).
`USTGameInstance::UpdatePause` calls it directly (call at exe+0x510CE1F; every logged caller is exe+0x510CE24), and
it is the one place where the requester state turns into a world pause. The hook is installed ungated, in normal play.

**Fix (mod/src/ubw/pause_c96.cpp, `detour_c96_set_game_paused`).**
- Pause requests (`paused` = 1) on the host while at least one client is connected are skipped. A client counts as
  connected when its server copy PC has sent its first Pong (the same `C50Copy::ponged` test card 095 uses to fan out).
- A skipped call returns false. The original is not called, so the world keeps running.
- `RequestGamePause` / `RequestGameUnpause` and the requester arrays are untouched, so the menu opens, works and
  closes as before.
- Unpauses, a host with no client (solo, or before anyone joins) and every call on a client run the original.
- `UpdatePause` calls it every frame while a requester is held, so the card096 line is written only when the outcome
  changes (capped at 60).

**Other pausing screens.**
- Mission brief movie and "PRESS ANY KEY TO CONTINUE": they hold the same requester, so they take the same path. In
  the proof run they ran before the client's first Pong (0 clients connected), so the host world paused as before
  (6215892 to 6228155) and `ui continue` unpaused it. With a client connected, the same screens are skipped like the
  menu.
- Open: if a client's first Pong arrives while one of these pauses is already in force, the skip does not lift that
  pause. The world unpauses only when the host closes the screen. Not seen in a run.
- Client's own pause menu: the client's `UpdatePause` calls `SetGamePaused(1)`, which returns 0 on a client (no
  authority game mode), so the client's world does not pause and nothing reaches the host. The client's menu still
  opened. The host walked 740 uu in 0.8 s while the client's menu was open.

**Proof run c96-pause2** (FAST=1, tools/scenarios/c96-pause.txt): host rc 0, client rc 0, FATAL/ERROR 0, WARN 0, no
new crash dumps, drive rc 0 (21 of 21 lines ok).
- The host pressed Escape (PC InputKey, as a player does). Host log: "card096: host t=6233003 SetGamePaused(1)
  skipped: the world keeps running, 1 client(s) connected; context BP_GameInstance_C, caller exe+0x510CE24".
- With the host's menu open, the client walked from (-560, -1715) to the CTF door area: 12231 uu in 16.9 s, reached,
  0 stucks (6234462 to 6251409; the host closed its menu at 6251785).
- After the host closed its menu, both played on:
  - host goto, 8565 uu in 11.5 s;
  - client opened its own menu: "SetGamePaused(1) = 0 (pause refused)";
  - host goto during the client's menu, 740 uu in 0.8 s;
  - client closed its menu and walked 802 uu in 0.9 s.
- An earlier run (c96-pause, DLL 514f383c822db031) showed the same, but its per-call log used up the cap. That led to
  the log-on-change fix.

DLL hash: 6f8dc8e67527f37e

Report complete.
