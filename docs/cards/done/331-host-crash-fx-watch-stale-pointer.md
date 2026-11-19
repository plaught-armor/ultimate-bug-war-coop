# 331 Host crash on checkpoint reload: effect watch reads a freed component

Batch 1 run 1 (`c319-played-c1m1`, build `c4ece6d9f559d959`), senior trace 2026-11-14. Play blocker: the same code
is in play build 4 `5e2ce00acad8efa3`, and the user's rule allows a host restart in play.

## What happened (host log `logs/c319-played-c1m1-host.log`)

- t=299787034 `card143: host Reload ... sent`, t=299787051 `card121: save engine load done`, t=299787180 respawn
  in place rows, then `WARN card130: host fx grenade explosion (host's own) +3991 ms: component gone`.
- Same ms: `CRASH 1/4: EXCEPTION_ACCESS_VIOLATION at ubwcoop+0x116335 thread 404`, `reading 0xC932791C`,
  `rcx=C9327910`; stack `ubwcoop+0x116335`, `+0xB9CBC`, `+0x11EABF`. The game's handler then ended the process
  (`Fatal error!`); the scenario's lines 134 and 135 failed with the host gone.

## Cause (settled, disassembly of `build-b5/dwmapi-b18r4-c4ece6d9f559d959.dll`)

- `+0x116330` is `object_index`: `test rcx; je; mov eax, [rcx+0xC]`. The fault is that read on a non-null pointer.
- The caller at `+0xB9CB7` is a loop over 0x50-byte rows that picks a 100 ms or 1000 ms due time by a check count
  and then compares `object_index(row pointer)` with the row's stored index: `fx_watch_step` / `fx_check`
  (`mod/src/ubw/fx_watch.cpp`, baseline line 38 `if (object_index(w.comp) != w.id)`).
- The row holds a raw component pointer. The reload destroyed the old world's components and the memory was
  released, so the "is it the same object" test itself read unmapped memory. While the memory stays mapped the
  test only reports `component gone`, which is why 5573 watch rows in the playtest passed without a crash.

## Fix (already in the junior's tree, 07:04; not yet reviewed in a build I accepted)

`FxWatch` keeps a `C31Weak` (`c31_weak_set` at add, `c31_weak_get` at check); a null result logs `component gone`.
The engine's weak pointer resolves through the object array and never touches the object's memory.

## Sibling sweep (owed with the fix, report before the run)

Class: a pointer kept across ticks and validated by reading the object. Every table in the mod that stores a
`void*` to a UObject past the tick it was read in, with how it is validated: weak pointer, re-found in a live scan,
cleared on map change, or raw. Raw ones become `C31Weak`. Start from: `g_fx` (fixed), the card 314 mute table
(weak already), `g_c37.seen` (re-scanned each step), `C39Probe`, gib and pose tables, decal pool, card 136 journal
live components, `g_c29` vehicle rows, `g_c92` remote body renderer, grenade tables. The 23 `object_index(x) ==
stored id` sites compare a pointer read from the engine in the same tick; confirm each, list any that does not.

## Proof

The same scenario step (host and client down, `host travel checkpoint` with an explosion effect under 1 s old on
the host): no `CRASH` row on either side, `reconnected (` on the client, card 294 `restart done` row. Prediction:
at most `component gone` WARN rows for the watches that were pending at the reload.

## Also from run 1 (not this card)

- Card 319: the client gathered every hostile in range on the played path before any death (client tally
  `hostile 9: gathered 9, in ActiveMarkers 9`; samples at t=299719868, 299769938, 299779953). The after-reload
  samples were not reached (crash). `pc pawn null` on 5 client samples while dead (t=299784966 to 299804982):
  expected while there is no pawn; what matters is the first sample after the respawn.
- Host line 82 timed out: the host's first sample had `hostile 3: gathered 1` (t=299716574), then no host sample
  matched in 30 s. Read the host samples between t=299716574 and t=299771642 before calling it spawn lag.
- Shot name `c319-played-c1m1-c319p-A-client-minimap-2` is over 40 chars: shorten.
- Line 95 `revive prompt shown` timed out although the revive then worked (line 98).
- The client log carries `card319: host ...` rows after the host died (side label wrong on the client once it is
  at the frontend): the known "host-labelled rows in client log" item, now with a concrete row.

## Status 2026-11-14 (after run 3)

- Fix accepted in review (b18r5b `e94555c8fd4ecc76`: `FxWatch`, `C54Track`, `C50Test.pickup`, `C130Revive`, SeqSkip
  on `C31Weak`). Proof not yet run: run 3 was void at line 52 from two harness faults (Wine `SetForegroundWindow`
  fails on Wayland; the card 052 host rows travelled to FED_C2M1 after 100 s).
- Junior sweep (static, to be verified in the batch 2 diff): 22 of 23 `object_index(x) == stored id` sites read x in
  the same tick; `remote_body.cpp:1089` (`g_c92.pawn`) does not. Raw stored pointers dereferenced past their tick:
  `claim_c157` Claim.pc and Round.proj/pc/hit_comp, `C75Client` soldier/pc/mech, `C76Mech` pilot_pc/vehicle,
  `C74Fx.zone`, `C169Held` actor/avatar, `C310Capture.asc`, `C284Stand.actor`, `C132Detail.pc`/`C132Client.pc`,
  `fx_c136_net` ClientNet.pc/pawn, `DecalTarget.component`. Six more to check and a group classified from comments
  only; each gets a verdict with its deref line before the conversion diff.
- Rule for the conversion: synced state uses a stable id re-found per use where one exists; weak pointers for
  local-only tracking.
- All of it rides the batch 2 build; the proof is the c319-played D step in the batch 2 runs.

## Proof 2026-11-14 (b19 `d781c1e804ad64d6`, run 1 `c319-played-c1m1`)

Host and client down, `card143: host t=303947869 Reload ... sent now`, then client `reconnected (1): try 1, 3551 ms
after the drop` (t=303951458) and `card294: client t=303955419 restart done in 10247 ms`. 0 `CRASH` rows and 0
`WARN card331` rows in both logs. Accepted; play build 5 must be cut from this tree or later.
