# Card 563: card081 hides and restores the downed host's interact trace in the same frame

Playtest 2026-11-20 host log (`ultimate-bug-war-coop-public/logs/UBWCoop-menu-20261120-184651.log`), host downed at
344458410 during the client's Door Gunner (card 561):

```
344458414 card081: own interact trace hidden: BP_PlayerCharacterMI_C is Downed, trace found null
344458414 card081: own interact trace restored: BP_PlayerCharacterMI_C is Up, trace found BP_MountableTurret_Gunner_C
```

The pair repeats every frame until the diag cap (344458604). In the same frame card 081 sees the host's trooper as
Downed and as Up, so the downed host's interact trace is live again each frame (here it found the gunner seat).

## Trace

Which state each of the two card081 steps reads (card043's Downed flag, the pawn's tag, the PC's pawn), and why the
restore step reads Up while card043 holds the pawn Downed.

## Fix

Both steps read one downed state (card043's). The trace stays hidden for the whole bleedout and is restored once on
revive.

## Prediction

Host downed: one `hidden` row, no `restored` row until the revive, and the interact trace finds nothing while
downed.

## b369 (`4d7adc330945a079`, build-b5 copy kept), not run yet

Fix: `detour_trace_interactable` (`interact.cpp`) checks for the local PC. Sibling sweep still open: 36 other
`kSideHostOwn` checks to audit for the same unpossessed-pawn misread (the card 560 class).

### Run c560b371b predictions (c560b371a crashed at 60 s, host still in the dropship; same predictions) (junior, 2026-11-21, b371 `baeebee09d3e7f59`, scenario `tools/scenarios/c372-c3m1.txt`)

The client sits in the Door Gunner seat; `host down host`. Host: one `card081: host t=... own interact trace hidden`
row, 0 `restored` rows while downed, and no `trace found BP_MountableTurret_Gunner_C` row after the down.

## Result (senior, 2026-11-21): passed, closed

- c560b371b (b371, C3M1, client in the Door Gunner seat, `host down host`): one `hidden` row (142908181, trace found
  null), 0 `restored` rows and no `trace found BP_MountableTurret_Gunner_C` row in the 6 s left before the quit.
- c570b389a and c397b398a (C1M1 checkpoint restarts): one `hidden` row at the down and one `restored` row at the
  checkpoint load (the new pawn is Up), 344 ms and 342 ms apart. No per-frame pair in any of the 3 runs.
- A teammate revive did not run in these logs. The restore uses the same `c43_is_down` read as the hide, so the
  revive path has no separate state left to flip.

### Sibling audit (static, the card 560 class: an unpossessed trooper reads host-own on the host)

Of the 37 `kSideHostOwn` uses, the misread needs a site that gets an arbitrary actor, because a seated client's
roster pawn is its turret or mech (a server copy), never its trooper.

- Roster or PlayerController based (safe): attributes.cpp:938 (c20), insta_c361.cpp:255, player0_c321.cpp:115,
  script_c310.cpp:557, weapons.cpp:328, death.cpp:928 and hooks_ubw.cpp:1905 (the down path returns at
  `slot < 0`, so a pawn outside the roster never reaches them).
- Both host sides accepted, so the misread changes nothing: anim_c104.cpp:102/128, spawn_c169.cpp:244,
  weapons.cpp:151 (c267_source).
- Weapon-owner based (a seated trooper does not fire): fire.cpp:173, fx_c136.cpp:1353, fx_c136_net.cpp:2830.
- Test or diag only: death.cpp:478/885 (god flag), death.cpp:2120-2205 (card 043 test), hooks_ubw.cpp:1779 (c30),
  input_diag.cpp:1884, weapons.cpp:3261 (card 062 diag weak ref; AI troopers also read host-own here, diag only).
- Not pawns: weapons.cpp:139 (level script actor), text helpers, pawn_collision.cpp:301, sprint.cpp:663.
- Arbitrary actor, checked: attributes.cpp:1204 (`c43_interact_edge`). A seated trooper's input goes to the seat,
  so the host's Interact runs only on the pawn its own local PC drives. objectives.cpp:3649 (`c220_pass`): a prompt
  trigger edge from an unpossessed trooper passes as the host's own, which is the single-player behavior. Not carded.

No further fix is needed.
