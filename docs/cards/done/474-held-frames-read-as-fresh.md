# 474 — Frames held in the host send queue read as fresh on the client (stale hit effects after a client hitch)

Found 2026-11-17 in `c447b173a` (junior trace). Client rows `WARN card140: client hit BP_AI_WarriorBug_C guid 0x3e8
... 520 uu from the copy's ReplicatedMovement.Location`, t=561984739..743, `card223: frame age 38 ms`.

Trace: the client hitched 1222 ms at its own TurretLeave (`card303 ... frame of 1222 ms (tick 2934)`, log silent
561983514..561984682). The host pacer held every send 561983591..561984721 (window 561984044: queue peak 101
frames, 15936 B, longest wait 1008 ms, 72 silent-client holds), then flushed. Four grenade-splash hit rows taken
during the hold were applied with frame age 38 ms: the frame is stamped at the actual send (`GetTickCount64()` in
`ue/chan.cpp` on_send), not at enqueue. The copy's position was current; the hit rows were 0.5 to 1 s old, so their
impact and gore effects played up to 520 uu off the body. Real play: any client hitch while a bug is under fire.

Fix:
(a) `ue/chan.cpp` + `ledger_c132.cpp`: stamp each frame with its enqueue time, carried through the pacer queue to
    on_send, so `c132_ledger_frame_age_ms` covers the host's send queue as its comment already says. Every late
    check (hit `kHitLateMs`, satchel card 408, others) then classes held rows as late. Offset and rtt are min-based
    and unaffected: confirm that no rtt or offset sample reads the frame stamp.
(b) `hit_c132.cpp` client apply: only for a hit classed late or over `kHitDrift`, on a live non-ragdoll copy, move
    the hit point by (copy ReplicatedMovement.Location - host_at) so the effect lands on the body. Fresh hits are
    not moved.

Done when: gate c448 passes (0 mismatches, clock rows unchanged: session offsets within 20 ms of each other, 0 WARN
card313); a run with a client stall (`stall` command, 1200 ms) while the host shoots a bug shows the held hit rows
with frame age near the stall length, classed late, and 0 `WARN card140: client hit` rows; shot of the impact on
the bug on the client.

## Closed

Closed 2026-11-17 on b175 `63689adfcb1ec80e` (2 of 3 runs used).

- Fix (a): the pacer stamps a queued frame with its enqueue time and sets the held flag (seq bit 29,
  `kChanStampHeld`, `ue/chan.cpp`); `c132l_recv_begin` skips held frames for the clock sample, so a queue wait
  never feeds the offset. rtt (`fire.cpp` `c26_pong`) never read the stamp.
- Fix (b): `hit_drift` (`hit_c132.cpp:349`) returns the shift `copy ReplicatedMovement.Location - host_at` for a
  hit classed late or at or over `kHitDrift` on a live copy (no ragdoll root, not posed); `hit_apply` adds it to
  the impact point. An on-time hit at or over `kHitDrift` still logs `WARN card140` (and is moved).
- Gate `c448b175a`: 0 mismatches (host 1 round, client 10), 0 `WARN card313`, no crash. Session offsets -11, -11,
  then +11 for one 30 s round after the reconnect (low rtt 0: the card 385 limit "off by the one-way latency at
  most", same window in `c448b174a` at +17), then -6. Steady-state spread 5 ms; not a regression.
- Stall run `c474b175a` (client `stall 1.2`): host queued 5 Hit frames 667 to 1065 ms; client
  `card474: client hit BP_AI_WarriorBug_C guid 0xbc: frame age 1153 ms (late)` and a second at 913 ms (before
  the fix such rows read as fresh); 0 `WARN card140`; the other 4 held hits landed on the ragdoll (not moved by
  design). Shot `client-c474b175a-c474-c-impact1`: blood on the bug and ground (junior viewed; senior viewed
  `impact2`).
- Limit of the proof: both moved hits had a shift of 4 uu or less (the targets stood still), so a large shift was
  not seen on screen; the sign and gating are read from the code. Playtest-log check kept: any
  `WARN card140: client hit` row reopens.
