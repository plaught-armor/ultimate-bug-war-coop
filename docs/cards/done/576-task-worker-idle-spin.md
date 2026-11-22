# Card 576: shorter idle spin for the UE task workers (card 573 finding B)

## Finding (static read, senior 2026-11-21)

Run c569b390b put about 3 CPU s per 20 s per side (10% of process CPU) in
`LowLevelTasks::Private::FWaitingQueue::CommitWait` (exe 0x14116c960), quiet and combat alike.

- `FScheduler::WorkerLoop` (0x14117cc00) calls `CommitWait(this, FWaitEvent*, FOutOfWork&, SpinCycles, WaitCycles)`.
- SpinCycles is a fixed 53: `mov r9d, 0x35` at 0x14117d0d7 (`41 B9 35 00 00 00`, imm32 at RVA 0x117d0d9).
- WaitCycles comes from a CreateWorker table indexed by `WorkerId & 7`: {719, 991, 1361, 1237, 1597, 953, 587, 1439}
  TSC ticks.
- Each spin round checks the event, then waits WaitCycles ticks with `tpause` (flag byte 0x148fa8e01) or a pause +
  rdtsc loop. The Ryzen 7700X has no waitpkg, so it is the pause loop. After 53 rounds the worker sleeps on its event.
- No console variable sets either value.

## b394 `1e26bc27dc1be5e2` (= b393 + this + the card 569 gate, build-b5 copy kept)

`perf_spin_c576.cpp`, `c576_patch_worker_spin(base)`, runs in `init_after_resolve` right after the card flags. It
checks the 6 original bytes, then writes the new count into the low imm32 byte (VirtualProtect, restored after). Log:
`card576: task worker SpinCycles 53 -> N at exe+0x117d0d7`, or a `WARN card576:` row when not patched.

- `UBWCOOP_C576_OFF=1`: exe unchanged (A side).
- `UBWCOOP_C576_SPIN=N` (0 to 53): the count. Default 8.

Lossless: a worker that stops spinning sleeps on its event and the same signal wakes it. Only the wake latency of an
idle worker changes.

## Proof (one run, A/B in matched quiet windows)

Same scenario, both sides, low footprint pinning, 60 cap through `console t.MaxFPS 60` (`FPS_CAP=0`):
1. `UBWCOOP_C576_OFF=1`, a 20 s task stat window plus a 20 s perf record.
2. Default (8), the same window.
3. `UBWCOOP_C576_SPIN=0`, the same window.

Rows per window: CommitWait samples, Background Work and Foreground Work CPU s, process CPU s, card 330 frames and
frames over 50 ms.

## Predictions

- CommitWait samples drop at least 70% with 8, and almost to 0 with 0.
- Background plus Foreground Work CPU drops by at least 2 s per 20 s per side.
- Card 330 frames stay 1788 to 1800 per 30 s, with no new frames over 50 ms. GameThread CPU s does not rise more than
  0.3 s (a slow worker wake would show as GT waiting on tasks).

Budget: one run for the three settings (separate launches in the same lane pair if the env must change per launch).

## Partial result, senior 2026-11-21

| Run (window) | Side | CommitWait samples | BG+FG work | Process | GT | card330 frames/30 s |
|---|---|---|---|---|---|---|
| c569b395d, OFF, clean | host | 1049 | 13.15 s | 29.84 s | 9.23 s | 1788 |
| c569b395d, OFF, clean | client | 1198 | 17.30 s | 33.14 s | 7.82 s | 1798, 1786 |
| c569b395a, SpinCycles 8, contended 2nd half | host | 219 | 11.20 s | 27.90 s | 9.41 s | 1798 |
| c569b395a, SpinCycles 8, contended 2nd half | client | 266 | 14.46 s | 30.05 s | 7.99 s | 1797, 1794 |

SpinCycles 8 cuts CommitWait samples by about 4.5x and worker CPU by 2-3 s per side. Frames hold. The host GT
difference (+0.18 s) is inside the 0.3 s bound. The contended a window cannot close the card. A clean SpinCycles 8
baseline (c569b395e) and the SPIN=0 run (c569b395c) decide the default.

c569b395c, SpinCycles 0, clean window (other-job gfx 0 on all 14 lines): host CommitWait 0, BG+FG 11.38 s, process
28.86 s, GT 10.02 s, 1792 frames; client 0 / 14.84 s / 31.33 s / 8.43 s, 1799 and 1796 frames.

Against OFF (d), SPIN=0 saves 1.8 s (host) and 2.5 s (client) of worker CPU, but GT rises 0.79 s and 0.61 s, past
the 0.3 s bound. The game thread now waits on worker wakes. SPIN=0 fails, so it is not the default. SpinCycles 8 kept
GT inside the bound in a (+0.18 / +0.17 s) with about the same worker saving. Its clean e run decides whether 8
stays the default.

## Result in run c569b395e (2026-11-21): SpinCycles 8 is the default

The run was clean, with other-job GPU 0 in every window. Columns: CommitWait samples, BG+FG Work s, process s,
GameThread s.

| Side | 8 | 0 | off (53) |
|---|---|---|---|
| host | 197 / 10.78 / 26.91 / 9.13 | 0 / 11.38 / 28.86 / 10.02 | 1049 / 13.15 / 29.84 / 9.23 |
| client | 288 / 14.38 / 29.56 / 7.84 | 0 / 14.84 / 31.33 / 8.43 | 1198 / 17.30 / 33.14 / 7.82 |

SpinCycles 8 is the lowest process CPU on both sides: host -2.93 s against off, client -3.58 s against off. Setting 0
removes the spin but costs more total CPU and the most host GameThread time. Frames stayed at the 60 cap in all
three. Each setting got one window, so the gap between 0 and 8 is indicative only. Card 576 is closed, with 8 as the
default.
