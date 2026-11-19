# 465: Collision journal at 71 % of its cap after a FED_C3M1 checkpoint load

From card 219 (closed). Run `c460b169a` (b169): after the checkpoint load the host's collision journal holds 1459
rows of `kC95Collisions` 2048 (`door_c95.cpp:53`). Rows by first caller: exe+0x37C2C74 (`execSetCollisionEnabled`,
`ShowActor` from the `SaveFullyLoaded` event) 1033, exe+0x516835C 125, exe+0x51683D6 92, exe+0x4C618A1 74,
exe+0x5168326 37, exe+0x51491D9 34. Rows by name: benches 111, bins 89, killable crates 86 + 76, AI spawners 81.

A player sees (not seen yet): on a map with more placed destructibles, or a later save on this one, the journal
fills, `c95_collision_row` drops the write (`dropped` counter), and the client's copy of that prop keeps the wrong
collision: a crate that can be walked through, or an invisible wall where the host destroyed one.

Cost today: every join after a load replays 1459 reliable rows (128 per tick, 12 ticks); each journal write does a
linear name compare over the rows (`c95_collision_row`, `door_c95.cpp:750`), 8774 writes at that load.

## Do

1. Now (one constant, next build): `kC95Collisions` 2048 to 4096. `C421State::hash` is sized by the same constant.
   Add one WARN per map when the peak passes 75 % of the cap.
2. Later, only if a map passes 2048 rows or the join burst shows in `netbudget`: a per-class rule. The host sends one
   row per map load ("save loaded") and the client's `BP_KillableBuilding_Base` / `ASTDestroyableProp` copies run
   `ShowActor` themselves; the journal then holds only rows that differ from that. Needs the bytecode check that
   `ShowActor` on a client copy picks the same mesh.

## Done when

- A FED_C3M1 load run shows `peak N of 4096`, 0 dropped, 0 digest mismatches, no new WARN below 75 %.
- The whole-map soaks of FED_C3M2, C4M1, C4M2, C5M1 report their peak after a load (the table goes in this card).

## Closed

Closed 2026-11-17 (senior), build b173 `dedb14d000d6ec36`: cap 4096 with WARN past 75 %; c460b172b, c460b173a/b: 0 WARN card465.
