# 232 — Digest timer round overlaps a command round

Soak c229-c1m1 (DLL `20cf859dd191a8fe`): `WARN card132: client round 16 cut by the next B frame`, `host digest
round 16 detail ask from BP_PlayerController_C dropped (not the current round)`, `host digest round 19 (timer) still
sending: command round not started`. The last class is also in c127-c3m1 (DLL `2928654b3a96090d`) and c140h.

The digest is the test instrument, not play: a scenario's command round starts while a timer round is still being
sent, so one round is lost from the check (18 rounds read instead of 19). No player-visible effect.

Static first: where the timer round and the command round start in `digest_c132.cpp`, and which should wait for
the other. Done when: a soak run shows none of the three lines and every round is read.

## Step 1 answers (senior, static plus `logs/c229-c1m1`, 2026-11-13)

- One cause for all three lines: `c132_host_start` (`digest_c132.cpp:799`) starts a round whenever the host is
  idle with no detail queued. It does not wait for the client's answer to the round just sent.
- Round 16 (command): host end frame 150371609; client compare and detail ask 150371631; host started round 17
  (timer, due while round 16 was sending) on its next step; the ask reached the host at 150371646 (`not the
  current round`); the client got the new B at 150371745 (`cut by the next B frame`).
- The reverse order gives the third line: a command that arrives while a timer round is sending is refused, not
  kept (`c132_digest_now`, 1773).
- Test instrument only; no play effect. A cut round is one unread round, so a real mismatch in it would be missed.

## Fix spec (batched with any build; no run of its own)

- `digest_c132.cpp` only. `C132Host` gains `settle_until_ms` and `command_due`.
- End phase (line 780): `settle_until_ms = now + kC132SettleMs` (2000 ms: the client's ask came 22 ms after the
  end frame; its snapshot wait is at most `kC132SnapWaitMs`, after which the round is void anyway).
- `c132_digest_step`: a round (timer or command) starts only when idle, no detail queued and `now >=
  settle_until_ms`. A detail ask inside the window keeps the host busy through the existing `details > 0` test.
- `c132_digest_now` sets `command_due`; the step starts the command round first when both are due and then
  pushes the timer by `kC132RoundMs`. `c132_host_start` keeps its WARN as the guard for any other caller.
- Do not touch the client side or the frame format.
- Prediction (next soak run of any map): 0 rows of the three classes; every `digest round N (command)` row is
  followed by the client's `digest round N:` compare row; round numbers on the client have no gap.

## Closed (2026-11-16 20:00, senior)

The fix spec is in `digest_c132.cpp` (`settle_until_ms`, `command_due`). Run `c423b159b`: host rounds 1 to 12 with two
command rounds (7 and 11), the client begins rounds 1 to 12 with no gap, 0 `WARN` digest rows on either side, 0
mismatches.
