# Card 555: radar blips for enemies that are not there (both players), refresh too slow

Playtest 2026-11-20 on b366 (`e62e76f63a05d88f`), FED_C2M1. User report with screenshot
(`Screenshot_20261120_190459.png`: screen-edge bug blips at the top, no bugs in view): "the on screen radar has screen
blips for enemies that aren't there, same for host. needs to update faster".

## Known from earlier cards

- Card 293 (closed 2026-11-17): the only host blips without an enemy in view were live bugs still underground after
  spawning (3 to 5 s, same on both screens). Dead markers leave within one tick. The reopen rule is blips on one
  screen only, or long dwell on dead markers; this report is both screens, so it is the "refresh" side, not a sync
  gap.
- Card 319: the bug proximity widget (`WB_Bug_Proximity_Area`) runs on a second `USTMinimapComponent` on the PC. New
  markers come from a registry walk every `MinimapScanFrequency` (comp +0xE0, countdown +0x118); the HUD gathers with
  a timer of HUD+0x400 seconds that calls `SetMinimapMarkersReadyForUpdate`.
- The card293 diagnostic wrote no rows in the playtest client log (`UBWCoop-menu-20261120-184517.log`): it is off
  in the play build, so this playtest has no per-blip data.

## Questions the trace answers (static, then one run)

1. The vanilla values of `MinimapScanFrequency` and HUD+0x400 on both components, and how long a marker stays after
   its bug goes out of range, burrows, or dies.
2. Whether the stale blips are burrowed live bugs (card 293's source, vanilla) or bugs the client has no copy of.

## Fix direction

If the stale blips are the vanilla refresh: shorten the proximity component's scan period and the HUD gather timer
on both sides (a co-op mod setting, logged), sized by the measured blip lag against the user's latency target (player-visible delays
under 100 ms) where the GameThread cost allows. If they are copies without a bug, that is a sync fault and the fix goes to
the owning card instead.

## Prediction

With the shorter period, the card293 dwell rows show no marker kept more than one period after its bug left range
or died, on both sides.

## Host log (`ultimate-bug-war-coop-public/logs/UBWCoop-menu-20261120-184651.log`)

The card 319 sampler ran on the host (every 5 s). FED_C2M1 summary at 343149518: 405 samples, 1349 live NPCs in
range, 1290 gathered, 1284 in ActiveMarkers. Hostile: 322 live in range, 298 gathered, 293 in ActiveMarkers. The HUD
gathers every 0.50 s, with range 2500.

The gathered hostiles never exceed the live hostiles in range, so the host data shows no markers for dead or gone
bugs. Some samples gather more markers than live NPCs in range (4 against 1), but these are non-hostile markers. The
blips the user saw are most likely live bugs the player cannot see: underground after spawning, or behind cover
(card 293). The "needs to update faster" part is the 0.50 s gather plus the scan countdown. The next step is the
static trace above: what the blip shows for a burrowed bug, and the vanilla scan values.

## b369 (`4d7adc330945a079`, build-b5 copy kept), not run yet

No fix yet: the play logs cannot name the cause. The trace run in "Next step" above is still needed.

## Static trace verdict (senior, 2026-11-21)

- Question 2 is answered by card 293's closing trace (`done/293-host-minimap-blips-without-enemies.md`, lines 181 to
  185): the only blips without a visible enemy were live bugs still in their spawn hole. The game itself puts a
  marker on a bug that spawns underground, for 3 to 5 s (4.7 s measured), on both screens. The mod neither adds nor
  keeps a marker, and the host sampler above shows no gathered hostile beyond the live ones in range. The report is
  on both screens, which matches this source and not a sync gap.
- Question 1, in part: the HUD gather period (HUD+0x400) is 0.50 s on the gameplay HUD (card319 rows). The
  proximity component's own scan period (+0xE0) is not sampled yet: the card 319 sampler reads the PC's main minimap
  only.
- A faster refresh does not remove these blips, because the bugs are really there, underground. A 0.50 s gather can
  keep a dead or departed bug's blip for up to 0.5 s, which is over the 100 ms latency target, but no log so far shows
  that case.
- Two fixes are possible, and the choice belongs to the user:
  1. Hide a bug's marker until it has emerged from its spawn hole. This changes vanilla behavior: the blip is the
     game's warning that bugs are coming, and co-op shows more of them because two players draw spawns in two areas.
  2. Keep the vanilla warning and only shorten the HUD gather to 0.10 s on both sides (5 times the overlap queries
     per second; the cost is measured with the card 330 step timer before it ships).
- No code and no run until the user picks one.

## Fix (senior, 2026-11-21, user choice "hide until emerged"; rides b414)

- New `radar_c555.cpp`, both sides, nothing sent. `detour_c99_begin_play` (AActor::DispatchBeginPlay) adds each
  `c37_enemy` to a 256-entry table, so its blip is hidden from its first frame.
- `c555_step` (ubw_post_tick, every 100 ms while an entry is listed) runs the card 293 ground test on each entry:
  a visibility trace straight down from 2000 uu above the bug, where a hit more than 300 uu above it means ground
  over the bug. The entry is dropped, and the blip shows, at the first test with no ground over the bug, when the bug
  is destroyed or dead, or after 6 s. The 6 s cap keeps a bug that spawns under a roof or bridge from hiding longer;
  card 293 measured 3.1 to 4.7 s underground. With no own pawn to trace from, entries wait for the cap.
- `detour_c416_show` (Execute_ShouldShowOnMinimap, always installed) returns false for a listed entry. Teammate
  slots are answered first, as before. An empty table costs one compare.
- Map end prints a `card555: <side> radar hide:` row with: added, emerged (longest), aged out, gone or dead, past the
  table, ground traces, still listed. A full table logs a WARN (those bugs keep their vanilla spawn blip).
- Cost check: the card 330 step timer covers `c555_step`. c293's every-marker traces cost about 1 s per C3M2 map, and
  this traces only bugs under 6 s old.
- Prediction for b414 on FED_C2M1: no blip shows during a bug's spawn-hole time, on either screen. Both sides' rows
  show emerged above 0, with the longest between 3000 and 5000 ms. Aged-out stays a small share of added (a large
  share means roofs or the trace misfires). No card555 WARN. The c555_step time stays under 0.1 ms per tick on
  average.
- Syntax checks for radar_c555.cpp, player_marker.cpp, player_c99.cpp and hooks_ubw.cpp returned rc 0. CMakeLists
  lists the new file.

## Quit row proof (junior, 2026-11-21, b430 `961c941ae5a47531`, c558b430a)

`sync_quit` now calls `c555_map_end`. With `client quit` and `host quit` at the end of the scenario, both rows print:
client "26 enemies added, 26 emerged (longest 2653 ms), 0 aged out", and host "38 enemies added, 36 emerged (longest
2772 ms), 2 aged out at 6000 ms, 0 past the 256-entry table". There are 0 card555 WARN rows. c558b429a had no row
because its scenario had no quit, and the harness kill skips `sync_quit`.

## Cost check from existing logs (2026-11-21, no run)

card330 `step c555_step` rows on gameplay maps in the b41x-b44x logs: mean 0.3 to 0.7 us per call, at most 0.39 ms
for a single call, 0 calls over 1 ms, at most 19.8 ms total over a whole FED_C3M1 map. The prediction (under 0.1 ms
per tick on average) holds. With the b430 quit rows (emerged 26 of 26 on the client and 36 of 38 on the host, longest
2653 and 2772 ms, 2 aged out, 0 WARN), every logged prediction holds. Only the on-screen part is left: no blip shows
during a bug's spawn-hole time. That is the user's playtest check.

## Closed (senior, 2026-11-22)

The fix is proven by logs and runs, and the proof is in the sections above. Under the no-playtest-parking rule, a proven fix does not stay open waiting for a play check. It is on the playtest checklist in STATE.md. If the user sees it again in play, reopen this card with that report.
