# Card 118: The client crashed running up a ramp; crashes must leave a trace in the mod log

Status: written 2026-11-06 (senior); sent and accepted 2026-11-06 (trace in; crash not reproduced; waits for the next playtest).

Read:

- card 101 Report (mantle: `GA_Mantling_Move`, the host's client-authoritative CMC window, +0x555 bits 0x2/0x4);
- card 094 Report (`UBWCOOP_C94_LOG=1`, jump and mantle log; scenarios `tools/scenarios/c94-*.txt`, `c101-measure.txt`);
- the playtest logs: host `logs/playtest-host-20261106-ramp.log` (copied from the game folder's `UBWCoop-menu.log`)
  and client `logs/playtest-client-20261106-ramp.log`.

Repo root: `/path/to/ultimate-bug-war-coop`. DLL base: `4ff26b46c39033ac` (card 117).

## Why

First internet playtest, 2026-11-06, FED_C1M1. Host on this machine, the friend's client on Linux/Proton, rtt about
160 ms. Both players ran up a ramp to test the vault, and the client got a UE "Fatal error". There was no crash folder
on the friend's machine and no Shipping log, and the mod log has no error line. The cause is unknown.

What the logs show (host clock; client clock + 81564494 ms):

- Client's last line: `card028: client t=4153976 ... key Run -> Walk (health 100.0, speed 710 ...)` = host 85718470.
  Its last answered ping is pong 11 at host 85715707; pings 12-14 got no reply.
- Host's view of the client, just before: steady Walk/Run near speed 750, then from 85718228 Idle <-> Run every
  100 ms at speed 1906-1972 (about twice a run). The server copy and the client fought over the position at that
  moment, as in card 101's mantle divergence.
- The host's player marker froze the client's head at (-544, 19515, 946) from 85722542. That is the ramp.
- The mantle log (`UBWCOOP_C94_LOG`) was off.

## Do

1. **Crash trace (both sides, always on).** When the game hits a fatal exception, write one block to the mod log
   and flush it: the exception code, the faulting address as `exe+0x...` (or module+offset), the thread, and a short
   return-address stack (module+offset per frame, at most 32 frames). Use a vectored handler, and log only the
   fatal codes (access violation, illegal instruction, stack overflow, int divide). It must never swallow the
   exception: return `EXCEPTION_CONTINUE_SEARCH`, so UE's own crash path still runs. Write at most a few blocks per
   run, with no allocation in the handler. If the game raises first-chance access violations of these kinds that it
   handles itself (check one normal run), filter those out by address and say so. A UE "Fatal error" from
   `check`/`ensure` is not an exception: hook the game's fatal-error path (`FDebug::AssertFailed` /
   `FError` / `RequestEngineExit` callers, whichever exists in the PDB publics) to log its message as well.
2. **Reproduce (one run, both players, FED_C1M1, FAST=1, `UBWCOOP_C94_LOG=1`).**
   - Warp both players to just below (-544, 19515, 946), facing yaw 91.
   - The client runs up the ramp and jumps/vaults at the top, several times, with the host next to it and running
     the same way.
   - A simulated 160 ms rtt, if the runner can add one cheaply (for example `tc netem` on loopback inside the test
     scope only). If it cannot, run without and say so.
3. **Fix** the crash if the run reproduces it, at the narrowest layer the trace names.

Stop when the trace is in and the run either shows the fix or the crash does not reproduce; the user will playtest.
If it does not reproduce, report the attempts, the c94 mantle lines at the ramp, and what the trace logs for a
forced test crash (a test-only command is fine).

## Done when

The Report has: the crash-trace design and a sample block from a forced test crash; the reproduction attempts with
the mantle lines; the cause and the fix, or "not reproduced"; the run length, rc, ERROR/FATAL, dumps and the DLL
hash (sha256 prefix, `sha256sum | cut -c1-16`). Ends with "Report complete."

## Report

DLL f0a52e0573cfefb1 (`build/dwmapi-c118-f0a52e0573cfefb1.dll`).

### Crash trace (always on, host and client)

`mod/src/ubw/crash_trace.cpp`, installed first in `init_after_resolve` ("c118: crash trace installed").

- Vectored handler (`AddVectoredExceptionHandler(1, ...)`), fatal codes only: access violation, illegal
  instruction, stack overflow, integer divide by zero. It always returns `EXCEPTION_CONTINUE_SEARCH`, so the game's
  handling is unchanged. At most 4 blocks per process, at most 32 frames per block, 4 frames per line.
- Nothing is allocated. Stack walk: `RtlLookupFunctionEntry` + `RtlVirtualUnwind`, leaf frames as rip = [rsp]; every
  step is checked against the thread's StackLimit/StackBase. Addresses print as `exe+0x...`, `ubwcoop+0x...`, or
  `<module>+0x...` with the module named from its PE export directory (no loader lock taken).
- Lines go through a new `log::crash_line` (fixed 512-byte buffer; tries the log lock and writes anyway if another
  thread holds it), then `log::flush` (FlushFileBuffers), so the block is on disk before the process dies.
- UE fatal path: hooks on `ReportCrash` (the GuardedMain `__except` entry; logs the code, the fault address and the
  thread, then `GErrorHist` and `GErrorExceptionDescription`, flushes, then calls the original) and on
  `FWindowsErrorOutputDevice::HandleError` (logs the same text). A UE `Fatal error` with no exception would show
  as the `CRASH ue:` lines with its message.
- Once `ReportCrash` is entered the handler stands down: UE's crash-reporting thread takes its own handled
  first-chance access violations in ntdll while it walks every thread's stack, and in the first forced test (c118c)
  those used up the 4-block cap. The original `ReportCrash` never returns (it writes the dump and ends the process),
  which is why the error text is logged before calling it.
- Normal runs: c118a and c118b (148 s and 118 s of play, both sides) logged no `CRASH` lines, so the game raises no
  first-chance fatal exceptions in normal play and no address filter is needed.

Sample block, forced client access violation (c118e, test command `crash av`, writes to address 0x118):

```
CRASH 1/4: EXCEPTION_ACCESS_VIOLATION (0xC0000005) at ubwcoop+0x60AAB thread 544 (first chance; the game's handling continues)
CRASH 1: writing 0x118
CRASH 1: rax=1 rbx=6B9A0150 rcx=6FFFFFEFF374 rdx=0 rsi=6FFFF25F0AA0 rdi=5445F25
CRASH 1: r8=0 r9=0 r10=7F49A50CD740 r11=202 rsp=C9F7E0 rbp=0
CRASH 1: stack #0 ubwcoop+0x60AAB #1 ubwcoop+0x61FF7 #2 ubwcoop+0x7EB45 #3 exe+0x444B00A
CRASH 1: stack #4 exe+0x444F72C #5 exe+0x444F7AA #6 exe+0x445049D #7 exe+0x4457724
CRASH 1: stack #8 exe+0x6EB7EC2 #9 KERNEL32.dll+0x116B9 #10 ntdll.dll+0x10E0F
CRASH 1: end
CRASH ue: ReportCrash code 0xC0000005 at ubwcoop+0x60AAB thread 544
CRASH ue: GErrorHist[0] Fatal error!
CRASH ue: GErrorExceptionDescription empty
```

The exe frames match `crash_stack.py` on the UE dump of the same crash (exe+0x444B00A is FEngineLoop::Tick, then
GuardedMain and WinMain). The playtest's "Fatal error" with no dump and no log line now leaves at least the
`CRASH ue:` lines, and a block if an exception caused it.

A `crash fatal` test (UE_LOG Fatal through `FMsg::LogfImpl`) was tried in c118d and dropped: in this shipping build
the call returns without crashing, so it could not show the fatal path. The `HandleError` hook is therefore not
exercised by a test; it is only a logging line before the original.

### Reproduction

Scenario `tools/scenarios/c118-ramp.txt`: FED_C1M1, FAST=1, UBWCOOP_C94_LOG=1. `host warp client point -544 15300 600`
and `host warp actor point -480 15300 600` (a client-side warp is corrected back by the server), then both players
face yaw 91 and run and jump up the ramp together: new drive.py step `both <command>` sends the command to host and
client at once and waits for both. Five runs with jumps (W 2500 with SpaceBar 1500/1200/800/1800), a ledge section
starting from (-544, 19515, 840) (W 3000/SpaceBar 1500, then 3x W 2000/SpaceBar 300), and a final long run (W 9000,
SpaceBar 6500).

- c118a: host and client ran one after the other, and the client warp was corrected back (client stayed near
  y -714). 148 s, rc 0/0, FATAL/ERROR 0, no dumps, no `CRASH` lines. Not a valid attempt for the client; redone as
  c118b.
- c118b: both players together, warps within 62 and 4 uu. 118 s, rc 0/0, FATAL/ERROR 0, WARN 0, no dumps, no
  `CRASH` lines on either side. Mantles fired: GA_Mantling_Move 11 times in the host log and 7 times in the client
  log, for example the client at (-629.9, 18701.0, 241.4), handle 45, with the host's "card101: mantle window opens"
  and the client lerp target (-630.6, 18741.0, 441.0); mantle searches at (-577.6, 16301.0, 836.6),
  (-605.8, 17916.6, 683.3) and (-628.1, 18682.9, 142.9). The host Idle<->Run flap showed 5 times, at the warps
  (speeds 168461, 3092, 104393).

No 160 ms rtt: `tc netem` needs root, and UE's PktLag is compiled out of the shipping build. The runs are on
loopback, so latency-dependent timing is not covered.

Result: not reproduced. No fix made; the next crash on a playtest should leave a block and the UE error text in
the log.

### Runs

| run | what | length | rc host/client | FATAL/ERROR | dumps |
|---|---|---|---|---|---|
| c118a | ramp, sequential (client warp failed) | 148 s | 0/0 | 0 | none |
| c118b | ramp, both together | 118 s | 0/0 | 0 | none |
| c118c | forced client av (DLL b733d214c93491c6, before the stand-down) | short | 0/0 | 0 | one UECC dump |
| c118d | forced client av + host `crash fatal` (DLL 75feeb2372da7fc4) | 113 s | 0/0 | 0 (host WARN 1: crash fatal returned) | UECC-Windows-444BAC4645C1899902BF94B13F470831_0000 |
| c118e | forced client av, final DLL f0a52e0573cfefb1 | 96 s | 0/0 | 0 | UECC-Windows-AF4FD9D74E829C481E2178932669BE0E_0000 |

Dumps in c118c to c118e are the intended forced crashes. c118a and c118b used the first trace DLL; the later
changes only touch the forced-crash path, the `ReportCrash` hook and the test commands.

Other changes: `tools/symbols.txt` (ReportCrash, FWindowsErrorOutputDevice_HandleError, GErrorHist,
GErrorExceptionDescription), `tools/pdb_rva.py` (allows data symbols, mangled names with "@@3"), `log.h/.cpp`
(`crash_line`, `flush`), the test command `crash av` (cmdfile, testrun), drive.py `both` and `crash`, scenarios
`c118-ramp.txt` and `c118-crash.txt`.

Report complete.
