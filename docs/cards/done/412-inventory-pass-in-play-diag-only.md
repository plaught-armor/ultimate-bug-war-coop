# Card 412: the +90 s actor inventory pass runs only with UBWCOOP_DIAG

Status: accepted 2026-11-16 (static change). Class: card 330 (per-frame cost). In b106 `e5a9c77af196f7d0` and later.

## Problem

`c132_inventory` (card 132 step 1, log only) walks every actor of every level and logs 150 to 220 rows. Its "+90 s"
call from `c49_world_check` ran on a host frame in play: pass 14, 16 and 20 ms in the playb72, playb71 and playb69
host logs, one hitch per map in every build.

## Change

`objectives.cpp`, `c49_world_check`: the "+90 s" call is behind `g_gate_diag`. The "map load" and "map end" passes
run behind a loading screen and stay in every build.

## Proof

Static: one caller, one gate. The fast chain cannot show it (its maps end before 90 s). A play log without
`UBWCOOP_DIAG` must hold no `+90 s` inventory row; check on the next playtest log.
