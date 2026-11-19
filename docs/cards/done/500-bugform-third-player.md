# 500: BugForm row names the right pawn with a third player

Status: open (written 2026-11-19, senior). Split from card 209 (Down and Warp proven in c209b257a).

## Why

Card 209 moved Down, Warp and BugForm onto pawn NetGUIDs. c209b257a proved Down and Warp with three instances. It
never reached BugForm: the scenario failed at the host-downed stand wait (line 110), then cascaded at the travel.

## Do

- Write a short three-instance scenario that starts on a bug-form map (no C2M1 leg, no Down steps, no travel) and
  switches each client's form once by the game's own path. Lint it, then pin it to the current build.
- Each step that depends on state waits for its log line. A stand wait gets enough slack, or uses the warp's own
  done row.

## Proof (written before the run)

- Each client logs the other client's BugForm row resolved to the other client's GUID and host slot (client: guid of
  slot 2; client2: guid of slot 1), or a `card209 ... applied: resolved to <pawn>` row for a held row.
- card132 BugForm ledger: sent equals applied on each receiver, refused 0.
- FATAL/EXCEPTION 0 on all three logs. A 16G scope needs the user's approval again (three games).

Reopen when: a client applies a BugForm row to the wrong pawn with three or more players.

## Dropped (2026-11-19 06:48, senior)

The user does not care about bug mode (2026-11-19). No run. Reopen when: the user asks for bug mode.
