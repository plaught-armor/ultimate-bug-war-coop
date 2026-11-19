# Card 403: channel receive handlers were outside the card 330 frame cost

Status: accepted 2026-11-16. Class: card 330 (per-frame cost), diagnostic.

Receive handlers run from the engine's `ClientMessage` dispatch, not from `ubw_post_tick`, so the card 330 window
row did not count them unless `UBWCOOP_C132_FX` was set. `c252_ledger_recv` (`chan_ops.cpp`) now always times the
handler and calls `c330_recv(op, ticks)` (`frame_c330.cpp`); the window row ends with
`recv (outside mod total) N frames X ms, <top 3 ops with frames, ms, max>`.

Build `c56d08282c73d7b3`, run `c403b94b` (fast chain FED_C3M1 to FED_C5M2, passed). Host: 5 to 47 ms per 30 s
window (Pong). Client: 2291 to 6537 ms per 30 s window, against a mod total of 351 to 803 ms: card 404.

Second measure, same run: `perf record -F 499` for 40 s on each game process, samples inside the DLL image
(base from `/proc/<pid>/maps`, names from an lld `/map` relink): host 0.71 %, client 6.59 %, 2/3 of the client's in
the CRT's printf code under `c50_name`. Detour bodies showed no hot spot (host `detour_process_event` 4 samples of
32278).
