# Card 113: The wave size uses the lobby's player count, held across map travel

Status: written 2026-11-06 (senior); sent, fixed at review and accepted 2026-11-06.

Read:

- card 111 and card 112 Reports (`mod/src/ubw/bug_scale.cpp`: `c111_players`, the wave table, the join/leave point);
- `docs/STATE.md` line 34 (mission start and `TransitionToLevel` 0x05109340, `TransitionToNextLevel` 0x05109550,
  `RestartCurrentLevel` 0x050FFC30, `OpenLevel` 0x050FA520).

Repo root: `/path/to/ultimate-bug-war-coop`. DLL base: `822338c0bb9e6704` (card 112).

## Why

User decision (2026-11-06): the wave size comes from the lobby's player count, and a late join adjusts it on the
fly. Card 112 showed that on FED_C2M1, 18 of 20 waves are set up and started in the same tick, and the start waves
run at map load. Today `c111_players` counts the controllers present in the world. After a map change or a
checkpoint restart, a client that has not reconnected yet is not counted, so the start waves come out at solo size
even though the friend was in the session.

The fix: the host keeps a session player count, the players in the co-op session. It does not drop when a map
change or restart briefly disconnects a client. The wave size and the ticket limits use that count. The first map
of a session still starts from the players present, and a late join raises the count through the card 111/112 path.

Started waves are not grown on a join (answer to card 112's open question): a late joiner meets the current waves at
their size, and every wave after that is scaled.

## Do

1. **Measure (one log run, with both players in).**
   - Does the client follow the host on `TransitionToNextLevel` and on a checkpoint restart (`RestartCurrentLevel`),
     or must it rejoin? Use one of them, with the smallest test-only runner command that calls it on the host (for
     example `host travel next` or `host travel restart`).
   - Times on the new map: the host's first `InitialiseWave`, the client's reconnect or `PostLogin`, and its pawn.
   - Where the host already keeps the session's players across travel, if anywhere (card 052 lobby, card 043
     roster, the mod channel peers). Reuse that if it exists.
2. **Build (host only).**
   - Add a session count: the highest player count seen in the session. It holds across a map change or restart.
   - It drops for a player who leaves through a real disconnect (not a travel), or who does not come back within a
     grace time after the new map loads. Take the grace time from the measured reconnect time, plus margin.
   - `c111_players` returns the larger of the session count and the controllers present.
   - When a missing player's grace time ends, the count drops through the card 112 path: waves not started shrink,
     and `RefreshTicketLimits` runs.
   - The first map of a session behaves as it does today.
3. **Proof, one run:** start on FED_C1M1 with both players, FAST=1, then the test travel to the next mission or a
   restart with bugs.
   - The new map's start waves log `scaled ... (2 players)` before the client's pawn exists.
   - Both players arrive. rc 0, ERROR/FATAL 0, no new dump.

   If the client does not follow travel at all, stop after step 1, report that and how a client gets onto the new
   map today, and do not build step 2.

Stop when the fix builds and that run shows it; the user will playtest.

## Done when

The Report has the travel behavior (follows or rejoins, with times), where the session count lives, the grace time,
the run lines (start waves scaled for 2 before the client's pawn), the run length, rc, ERROR/FATAL, dumps and the
DLL hash. Ends with "Report complete."

## Report

DLL `build/dwmapi-c113-186137be75190fde.dll` (build 186137be75190fde).

Run `c113a`: FED_C1M1 with FAST=1 and both players in, then `host travel next` to FED_C2M1. Scenario
`tools/scenarios/c113-travel.txt`. The run ended by scenario after 63 s: host rc 0, client rc 0, FATAL/ERROR 0, WARN
0 on both sides, no new crash dumps, no SystemFunction036 abort.

### Travel behavior (measured)

The client does not follow the travel. It drops and rejoins through card 045's reconnect, with no player input. The
same thing happens for RESTART MISSON (card 045 Report). A checkpoint restart (`LoadLastSave`) reloads inside the
same world, keeps the controllers and does not affect the count. Times from c113a (host clock = client clock, one
machine):

| Event | ms | after the travel |
|---|---|---|
| Host `travel next`: TransitionToLevel(chapter 0, level 1) | 49709050 | 0 |
| Client network failures 5 and 3 ("Host closed the connection."), drop to `3D_Frontend` | 49709071 | +21 |
| Host's first InitialiseWave on FED_C2M1 (`Warriors_Medium`, scaled 26, 2 players) | 49711989 | +2939 |
| Host LoadMap returned (FED_C2M1, listen) | 49712605 | +3555 |
| Client reconnect LoadMap returned ("reconnected (1): try 1, 7116 ms after the drop") | 49716194 | +7144 |
| Client pawn seen by the runner (`state pawn=client-own`, drive +51.9 s) | about 49716900 | about +7.9 s |

The new map's start waves are set up about 4.9 s before the client has a pawn, and 1.2 s before the host's own
LoadMap returns.

### Where the session count lives

Nothing kept the session's players across travel. The card 043 roster and card 111's `c111_players` count the
controllers of the current world, and the card 052 panel keeps only host mode and the address. The count is new:
`C113Session g_c113` in `mod/src/ubw/bug_scale.cpp`, host only.

- `c113_step(world, now)` runs from `c111_step` on every host tick in a listen world.
  - A new world arms the grace (`kC113GraceMs` = 45000 ms) and logs `card113: new world, session players N held`.
  - A count above the session count raises it at once (`join`).
  - A count below the session count lowers it only after the grace ends (`left or not back after the grace`).
    Mid-map, outside the grace, a real disconnect lowers it at once.
- `c111_players` now returns the larger of the session count and the controllers present (`c111_present`, the old
  count). Waves, reinforcements and ticket limits all go through it.
- When the count drops, the value `c111_players` returns changes, so `c111_step` runs the card 112 path: waves that
  have not started shrink, then `RefreshTicketLimits` runs.
- **First map of a session:** the session count starts at 0, so the first map uses the controllers present, as
  before. c113a logged `session players 0 -> 1` at C1M1 load, then `1 -> 2 (join)` at the client's join.
- **Grace time:** 45 s. The measured reconnect is 7.1 s after the drop (cards 052 and 045: 6.4-9.1 s). The margin
  covers the 20 s connect timeout (card 052) plus a second try.

Also changed:

- The card 112 table no longer clears all entries on a world change. It drops only entries of other worlds, so
  waves the new world set up at load, before its first tick, are kept.
- New test command `travel next|restart` (host, listen only), in `cmdfile`, `testrun.cpp` (`c113_travel`) and
  `drive.py`. It calls the game's `TransitionToLevel` with the next level, or the current one again.

### Run lines (c113a host)

```
card113: new world, session players 0 held for 45000 ms (present 1)
card113: session players 0 -> 1 (present 1, join)
card113: session players 1 -> 2 (present 2, join)
card111: players 1 -> 2, refreshing ticket limits
card113: host t=49709050 travel next: TransitionToLevel(chapter 0, level 1)
card045: host t=49709065 restart with 2 players: listen added to the Browse URL
card111: wave budget ... BP_PWD_Warriors_Medium_C: base 16, difficulty 16, scaled 26 (2 players, caller exe+0x512940C)   [49711989]
card111: wave budget ... BP_PWD_Warriors_Medium_C: base 16, difficulty 16, scaled 26 (2 players, ...)                    [49711991]
card111: wave budget ... BP_PWD_Warriors_Small_C: base 8, difficulty 8, scaled 13 (2 players, ...)                       [49711993]
card113: new world, session players 2 held for 45000 ms (present 1)                                                      [49712693]
```

The client was back at 49716194, and both players reached FED_C2M1 (shots `c113-arrived`). Compare cards 111 and 112,
where the same three start waves came out at 16/16/8 for 1 player.

### Not run

The grace-end drop (a client that does not come back within 45 s) was not run. The run quit about 20 s after the new
map, inside the grace. The code path is the `present < players && now >= grace_end` branch of `c113_step`, followed
by the card 112 shrink.

Report complete.

## Senior review (2026-11-06): one fix before acceptance

The travel result and the start-wave lines check out (hash 186137be75190fde, c113a host log 49711989-93: 26/26/13
for 2 players before the client's pawn).

Defect: `g_c113.players` is never reset when the co-op session ends. `c111_step` returns early for a Standalone
world, so `c113_step` never runs there, but `detour_c111_factor` still scales every wave with `c111_players`, which
returns the larger of the present count and `g_c113.players`. Example: the host plays co-op with the friend (session
count 2), quits to the main menu, and starts a solo campaign. Every solo wave is then 1.6 times larger, and the count
never drops, because no listen tick runs.

Do:

1. In `c111_step`, on a world that has a player controller and is Standalone (the menu, solo play), reset `g_c113`
   (players 0, world 0, grace 0) before the early return, and log it once (`card113: session ended (standalone
   world)`).
2. Check the c113a host log: between `travel next` and the C2M1 LoadMap, no Standalone world with a player
   controller is ticked, so the reset cannot fire mid-travel. If one is, reset only after the grace instead, and say
   which.
3. No new run is needed if step 2 holds from the existing log: the reset is shown by code. Rebuild, and report the
   hash.

Append the result under this section and end with "Report complete." again.

### Fix result (2026-11-06)

1. **Reset in place.** `c111_step` (`mod/src/ubw/bug_scale.cpp`) now resets `g_c113` to `C113Session{}` (players 0, world 0, grace 0) before the Standalone early return. The reset runs only when the world has a player controller, its net mode is Standalone, and `g_c113.players` is not 0. It logs once: `card113: session ended (standalone world), session players N -> 0`. After co-op, a solo map goes back to `max(present, 0)` = 1 player.
2. **The c113a host log: no Standalone world ticks mid-travel.** The reset at once is kept (no grace-delayed variant).
   - `travel next` at 49709050. `UEngine::Browse` / LoadMap run synchronously on game thread T 360 and return at 49712606. No engine tick runs between them.
   - The first `c111_step` after the travel, at 49712693, sees FED_C2M1 with net mode 2 (listen): `card052: ... net mode 2` and then `card113: new world, session players 2 held for 45000 ms (present 1)`. Card 045 appends `listen` to the Browse URL when 2+ players are present.
   - The "host/standalone" text on the pause, diag and collision lines is the `side()` label, not the net mode.
   - A restart with 1 player does give a Standalone world. The reset is wanted there.
3. **Build.** DLL hash `71a9079ca720c324`, saved read-only at `build/dwmapi-c113-71a9079ca720c324.dll`. The interim `e36a9ec2a161f516` had the older log text and was removed. No new run.

Report complete.
