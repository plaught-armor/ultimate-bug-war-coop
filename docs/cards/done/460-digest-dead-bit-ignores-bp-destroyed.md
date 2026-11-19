# 460 Digest dead bit ignores the Blueprint Destroyed bool (satchel building mismatch after a checkpoint load)

Senior trace 2026-11-16, from run `b40` (FED_C1M1, 2026-11-14, the only run so far with a checkpoint load after the
CTF satchel blast).

## Evidence

- Host `341872622 card137: host load reconcile c98 K BP_SatchelBuilding_CTF_C BP_SatchelBuilding_CTF_C_0: dead with
  no row, row added`.
- Client `341878604 card098: client ... world K BP_SatchelBuilding_CTF_C_0 ...: Health base 0 ..., IsDead now 1;
  ... journal replay`.
- Client rounds 13 and 14: `WARN card132: mismatch BP_SatchelBuilding_CTF_C BP_SatchelBuilding_CTF_C_0 dead host 0
  client 1`.

## Cause

Two definitions of "dead" for a placed killable building:

- `c98_dead_now` (`world_c98.cpp:800`, used by the load reconcile): `c98_bp_destroyed(actor) || IsDead(health)`. A
  building loaded destroyed has its Blueprint `Destroyed` bool set while the load restores its health, so IsDead is 0.
- `c132_actor_state` (`digest_c132.cpp:329`): `IsDead(health)` only.

After the load the host's building is Destroyed 1, IsDead 0. The reconcile adds the K row, the client applies it and
has IsDead 1. The digest then reads host 0, client 1 in every round until the map ends. The building looks destroyed
on both sides (the client applied `ReceiveOnSatchelExploded` earlier); the fault is the digest's definition, and the
permanent mismatch row hides real ones behind a known one.

## Fix

`c132_actor_state`: the dead bit is `health != nullptr && (IsDead(health) || c98_bp_destroyed(actor))`. Call
`c98_bp_destroyed` only when a health component exists and IsDead is false, so the property lookup runs for alive
killable actors only. Same code on both sides. Do not change `c98_dead_now` or the K row.

## Sibling check (report with the fix)

Every other reader of "dead" for a placed building: the card 137 join replay, the card 098 K apply, the card 292
satchel site row. Name each one's test and say whether it uses `c98_dead_now`.

## Prediction

A FED_C1M1 run with a checkpoint load after the CTF satchel blast: the `load reconcile c98 K ... row added` row is
there and there are 0 `mismatch BP_SatchelBuilding_CTF_C` rows in every round after the load. Gate `c448`: 0
mismatches, unchanged.

## Done when

The prediction holds in one run.


## Closed 2026-11-16 21:14: run `b40b166a` (b166 `d88eca03b8b88da0`)

Host row after the checkpoint load: `card137: host load reconcile c98 K BP_SatchelBuilding_CTF_C
BP_SatchelBuilding_CTF_C_0: dead with no row, row added`. 0 `mismatch BP_SatchelBuilding_CTF_C` rows on either
side; digest rounds 1 to 13 all 0 mismatches.
