# 348 The host acts on traffic from the client's old PC after a seamless travel

## Logged (b33, `c345-level-end` and `c344-no-confirm`)

1. Stale Pong. The host sent ping 11 in FED_C1M1 at 334166814. The client answered at 334168258 from its old
   `BP_PlayerController_C`, before its `Untitled` arrive. The host, armed in 3D_Frontend since 334167385, took it as
   the first Pong of the map (rtt 1462 ms) and sent the 3D_Frontend join replay at 334168276. The client was still
   in `Untitled` and skipped it (`WARN card095`, `WARN card132`). The gate was then open, so no second replay came:
   the client can miss the new map's join state.
2. Host `WARN card072`: AmmoMirror from the old `BP_PlayerController_C`, no server copy inventory, after the
   frontend travel.

## Cause

A hard travel gave the host a new connection per map, so anything arriving was from the new map. With card 346 the
connection and, until the client acks the swap, the old PC stay; frames and Pongs from before the client's arrive
are still delivered.

## Fix (not written)

- First Pong: a Pong opens the gate only when its ping was sent after the host's map arm (compare the ping's
  number with the number at the arm). Older Pongs are counted in one plain row.
- Frames: the host drops a frame that arrives on a PC that is not its connection's current PlayerController (the
  old PC waiting for the swap), counted per map in one plain row, no WARN per frame.

## Prediction (`c345-level-end`, `c344-no-confirm`)

- Host: the 3D_Frontend join replay row comes after the client's `seamless map change done: world 3D_Frontend`
  time, never before. 0 client `WARN card095` / `WARN card132` skips of that replay. 0 host `WARN card072`.
- One plain `card348` total row per map with the dropped counts. 0 new WARN kinds.

## Result (b34 `a4a709a1b9ed4c46`, 2026-11-14): accepted

- Code: `c348_ping_arm`, `c348_pong_current`, `c348_stale_pongs_take` (`fire.cpp`); `c26_host_pong` returns before
  the first-pong gate for a Pong whose n is not 1 to `kChanArgMax / 2` ahead of the ping counter at the map arm.
  Frame drop: `c348_host_frame_ok` (`sync_registry.cpp`) uses `APlayerController::HasClientLoadedCurrentWorld`
  (RVA 0x0404BE30), not "the connection's current PC" as the fix text above said (the old PC stays current until
  `ServerNotifyLoadedWorld`). `ChanOps::frame_ok` runs on both receive paths before `on_recv`.
- Runs `c345-level-end`, `c344-no-confirm`, `c346-persistent` passed. c345: the 3D_Frontend join replay at
  336202801, after the client's arrive at 336198974; 0 client card095/card132 skips; 0 host `WARN card072`.
- `card348` rows: frames dropped 2, 1 (c344) and 2 (c346); stale Pongs 0 in every row. The stale Pong path did not
  occur in any run; it is accepted from the code (counter wrap checked against `fire.cpp:697`).
- The row is written at the next map arm, so the last map's counts of a run are not logged.
