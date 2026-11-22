# Card 091: Post-split rehearsal: one playtest session on the new DLL

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: sent 2026-11-05 18:20 (senior). This is playtest prep, so no code changes unless a fault is found here.

Read:

- the card 058 Report and Review (the last rehearsal, and its findings);
- `docs/PLAYTEST.md`, especially its watch items;
- the card 089 and 090 Reviews;
- the `tools/run_pair.sh` header (LANE, FAST).

Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

Cards 077-089 moved all of `hooks.cpp` into `ue/` and `ubw/`. Each split card ran only the base routes plus one route per group. No card since 076 has run the whole scenario set on one DLL, and no session since card 058 has played the build without test flags. The playtest needs that session.

## Do

### Part A: dropped

The user cut the scenario sweep (2026-11-05 18:25). The split was byte-copy moves, and each split card already ran its routes. Build once with the card 090 DLL, deploy to lane 1, and give the hash.

### Part B: one playtest session (lane 1, no test flags)

1. Run one session exactly as card 058 did:
   - `tools/playtest.sh menu`, host first, then the client with `SECOND=1`, `WINDOWED=1 RES=640x360`, side by side, on `127.0.0.1`;
   - set no `UBWCOOP_*` variable by hand, and use real keys;
   - host and join through the F8 panel;
   - press Enter to advance movie cutscenes.
2. **Cover card 058's "not reached" items and its findings, in the order a player meets them:**
   - the client takes the Morita, presses 2 and fires;
   - the client minimap shows icons (card 070);
   - the shooting range targets show on the client (card 065);
   - the client rides the zipline and uses the satchel site (card 054);
   - the host takes the tactical support case and presses 2, if it is reachable;
   - the host dies, then RESTART CHECKPOINT, then one objective completes on both screens;
   - the client quits and rejoins through the panel.

   An item you cannot reach is listed with the reason; do not force it with a flag.
3. **Shots** of both screens at each item above.
4. Run `logsum` on every log: ERROR/FATAL 0, and every WARN explained.
5. Give the wall times from join to possess, against card 058's 5.2 s and 3.9 s.

### Part C: PLAYTEST.md

Update `docs/PLAYTEST.md`:

- remove the watch items this card settles;
- add what a player must know;
- add the play build hash, and save that DLL read-only as `build/dwmapi-play-<hash>.dll`, the way card 058's was saved.

## Done when

- Part B has one clean session, with rc 0 on both games, no new dump, and shots for every item reached.
- `docs/PLAYTEST.md` names the play build.
- The Report ends with "Report complete."

## Report

Junior, 2026-11-05 18:52. Part A was dropped by the senior. This is one `tools/playtest.sh` menu session on lane 1, driven by real key presses, with the card 090 DLL.

### Build

- Play DLL `1ddc46f17e72752c` (card 090 build). playtest.sh printed `deployed 1ddc46f17e72752c` (`logs/card091-host.run`).
- Saved read-only as `build/dwmapi-play-1ddc46f17e72752c.dll` (869888 bytes, `-r-xr-xr-x`).
- `docs/PLAYTEST.md` now names this hash and file under the "same `build/dwmapi.dll`" line.

### Steps reached (shots in `logs/card091-shots/`)

| step | result | shot |
|---|---|---|
| Host via the F8 panel | H at MISSION SELECT turned host mode on (`card052: H: host mode ON`, LAN 192.168.0.150); `listen added` for `/Game/Maps/FED/FED_C1M1`. | `full1c.png`, `s01-host-start.png` |
| Client join | J focused the box (75402690), Enter armed `127.0.0.1:7777` (75404666), LoadMap succeeded 2446 ms after the arm, own pawn on foot at 75407743, `AcknowledgePossession` at 75407752. Join to possess: **3.1 s** (card 058: 5.2 s). Client on foot in FED_C1M1, HUD normal. | `s03-joined.png` |
| Client quit | Esc opened the pause menu; QUIT, then YES on "QUIT TO TITLE"; the client reached the hangar with the panel back on (`world 3D_Frontend ... panel on`). Host stayed in the map. | `s04-client-esc.png`, `s05-client-quit-prompt.png`, `s06-client-quit.png` |
| Client rejoin | J, Enter armed again (75540239); LoadMap succeeded 3720 ms after the arm; own pawn on foot at 75547239 (**7.0 s**); host sent the Weapons list on the first Pong (75557051); `AcknowledgePossession` at 75557200 (**17.0 s** after the arm). Client on foot with HUD, objectives and minimap. | `s07-client-rejoin.png`, `s08-client-rejoined.png` |

The rejoin's `AcknowledgePossession` comes 10 s after the pawn is on foot, against 9 ms on the first join; it waits for the host's first Pong of the map. I did not look further (time limit). The pawn was playable from the on-foot line.

### logsum (`python3 -I tools/logsum.py card091`, logs copied to `logs/card091-{host,client}.log` while the games ran)

- Host: 1083 lines over 344.9 s, ERROR/FATAL 0, WARN 0.
- Client: 2247 lines over 317.0 s, ERROR/FATAL 0, WARN 0.
- Longest host gaps, about 20 s each, were the intro movies, the brief, and the client's quit (`left: journal sync slot freed`).
- Crash dumps: 40, the same count as before the session.

### Not reached (time limit 18:56)

- Client takes the Morita, presses 2 and fires. The client stayed unarmed; the armoury walk did not fit the time.
- Host past the brief: host dropship ride, on foot, minimap.
- Shooting range, zipline and satchel, tactical case.
- Host death and checkpoint.
- A clean quit of both games. They were left running; the logs were copied while live, so the end-of-run copies by playtest.sh are not part of this report.

Report complete.

## Senior review

Senior, 2026-11-05 18:53. **Accepted** in the 15-minute scope the user set (host, join, quit and rejoin).

- Checked: `build/dwmapi-play-1ddc46f17e72752c.dll` is read-only and has the same hash as the game's `dwmapi.dll`, and `docs/PLAYTEST.md` names it. Shot `s08-client-rejoined.png` shows both troopers on foot at base with a normal HUD and the same objectives.
- Finding: on the rejoin, `AcknowledgePossession` came 17.0 s after the arm, but the pawn was on foot at 7.0 s. The wait was for the host's first Pong. The first join took 3.1 s. This is not a blocker; it is a candidate card if the user minds the wait.
- Note: some shots are full-desktop grabs that include other windows (a chat app). Future shots should crop to the game windows.
- Not reached: the Morita with 2 and fire, the host past the brief, the range, the zipline and satchel site, the tactical case, and the host's death and checkpoint. These stay as PLAYTEST.md watch items.
