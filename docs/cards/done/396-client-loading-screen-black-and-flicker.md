# Card 396: client sits on black, then the map flickers before the loading screen

**Status:** open 2026-11-15, code in the tree (not built). User report 2026-11-15: "the client just sits on a black
screen.. then it flickers to the map, the loading screen, then back to the map so it's jarring." Goal: the client
sits on the loading screen until the host is done loading and has sent its data.

## Trace (static, `logs/c395b82-*`, FED_C3M2)

| t (ms) | side | event |
|---|---|---|
| 430163886 | client | LoadMap return, card 337 world rendering off (black) |
| 430165314 | host | its own screen up, hold on |
| 430166177 | client | its HUD spawned |
| 430167965 | client | pawn, card 156 restore, first Pong |
| 430168025 | host | join replay; IntroStart queued behind about 416 replay rows |
| 430168866 | host | IntroStart on the wire |
| 430169066 | client | GameStarted; cover dropped the same tick (5182 ms black) |

- Black is the card 337 cover from LoadMap return to the client's own GameStarted. Its parts: the host's own load
  and the client's HUD and pawn (about 4.1 s here), then IntroStart waiting in the pace FIFO behind the join replay
  rows (0.8 to 2.5 s per map on b82).
- The flicker is the cover dropped on the tick the screen widget is pushed, before it has drawn.
- "Back to the map" is the screen closing; that is the normal end.
- The screen is opaque (`client-c135-r2-c135-r2-client-screen.png`), so the world behind it is not seen.

## Change (this card)

1. `c135_send` (intro_c135.cpp): IntroStart is a priority frame (`chan_send_client_now`), ahead of the queued replay
   rows. Row `card396: host t= IntroStart to <pc> ahead of <n> queued frame(s)`.
2. `c320_try_release`: with every confirm in, the hold stays on while frames are still queued to the clients that
   were sent the op (`c396_queued`, `chan_pace_depth`), at most `kC396DrainMs` 5 s (WARN past it). Tried each tick
   from `c320_step`. Without this the earlier op would let the hold release before the replay rows are sent.
3. `c337_step` (render_c337.cpp): the cover stays `kC396PaintMs` 500 ms after the screen is on its stack; a screen
   that closes sooner uncovers through `c320_client_done` ("screen confirmed", no WARN).

## Not in this card (card 397, trace first)

The rest of the black (LoadMap itself, then the wait for the host's load, the HUD and the pawn) needs a loading screen
that does not depend on the client's HUD: the game's own widget is pushed by `GameStarted` on a HUD with a pawn, and
its title needs the chapter and level IDs, which the op carries today.

## Predictions (b83; run 1 `c388-c3m1-to-c5m2`, run 2 a `UBWCOOP_C135_KEEP_SCREEN=1` run with client shots)

1. Host ledger: the `IntroStart` sent row is within 50 ms of the `join replay: IntroStart` row on all 6 maps (b82:
   841 to 2462 ms later). 6 `card396: host ... ahead of` rows.
2. Client `world rendering on` rows: 6, none WARN, each at least 500 ms shorter than b82's (3744, 5182, 6234, 6374,
   5807, 5805 ms); in run 1 the reason is "screen confirmed" (the test's continue comes about 110 ms after the pause).
3. Host: `card396: ... hold stays on: N frame(s) still queued` then `queued frames waited for X ms, 0 left`, X under
   5000, on the maps where the confirm beats the queue; 6 `hold off ... (all confirmed)` rows.
4. 0 `WARN card337`, `WARN card396`, `WARN card320`, `WARN card344`; no new WARN class against b82; digest
   mismatches 0; client FX replay counts as b82.
5. Run 2 (screen kept): client shots at the `client GameStarted` row, 250 ms after it, and after the `world rendering
   on` row show black or the loading screen, never the map; the `world rendering on ... (title screen up)` row is at
   least 500 ms after GameStarted.

## Result (b83 `c334b626d793c7b1`, runs c396b83, c396b83r2) — accepted

- Prediction 1 holds: IntroStart queue-to-wire 0 ms on all 6 maps (b82 761 to 2462 ms), ledger entry 77 to 96 (b82 374 to 445).
- Prediction 2 fails: black 4089 to 6809 ms (b82 3744 to 6374). The op now lands in the same client frame as the join
  replay burst (card303 frame 798 to 2512 ms), and GameStarted runs after that frame. The black itself is covered by
  card 397; the burst frame is a leftover (STATE, not carded).
- Prediction 3 not exercised (queue empty before the last confirm on every map). Prediction 4 holds.
- Shots (c396b83r2, kept screen): loading screen at +438, +557 and +661 ms after GameStarted; no black, no map, no
  mod widget over it. No new WARN class against b72.
