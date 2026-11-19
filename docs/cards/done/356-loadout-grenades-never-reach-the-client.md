# 356 The mission loadout's grenades never reach the client's own pawn

Found by the card 354 loadout run (b41 `9b2775ac3b120578`, run `b41`, FED_C2M1 by seamless travel, 2026-11-14).

## Cause (logged)

- Host 342256785 and 342256795: `ResetInventory` adds `BP_GrenadeItem_Frag_C` amount 2 on both pawns; the server
  copy's 1 s row shows `STInventoryItem=2`, grant h209.
- Client: its own pawn's `ResetInventory` (342259452) takes the Unarmed branch and adds nothing; 0 GrenadeItem
  frame rows; 1 s row `items 0 [ ], grenade specs [ GA_FragGrenadeThrowingAbility_C h209 ... ]`. Client count 0, host
  copy 2.
- The host has no `card073: host ... on server copy` row for the add at 342256795, sent or WARN: `c73_relay`
  (`mod/src/ubw/grenades.cpp`) returned early. The add ran during the seamless map change. Which test returned
  (`sprint_side(owner) != kSideServerCopy`, no controller yet) is not read from the log.
- Item counts have no join replay: the host sends only on a server copy's add or remove.
- Not from cards 353/354: no card354 row fired; b39 takes the same path.

## Fix direction

One sender, `c73_send_all(pc)`: every item definition and count in the inventory of pc's server copy, as GrenadeItem
frames. Called from the grenades mechanism's join replay (first Pong after each arrive, `sync_registry.cpp`). The
client applies them as today (`c73_apply`; a frame before its inventory exists is deferred by `c73_defer`).
State from the code why `c73_relay` returned at the loadout add; if the copy is not yet classed as a server copy at
its own `ResetInventory` on a respawn too, the same sender runs where the copy is registered.
Sibling sweep, same shape (state set on the copy before the client's pawn exists, sent only on change): tactical
support charges, satchel, ammo reserve gains, any other per-pawn count the join replay does not carry.

## Prediction (1 run: `b41-c354-c2m1` with `UBWCOOP_AUTODROPSHIP=1` on the host, client respawn mid-mission)

- First spawn: client frame row `BP_GrenadeItem_Frag_C 2: our count 0 to 2, writer the host frame`; grant bound; the
  client's 1 s row and HUD shot show 2, equal to the host copy.
- Respawn: the copy's loadout adds are sent (`card073: host ... sent`), the client's count equals the copy's again,
  grant bound; the time from the new pawn to the last loadout frame is stated. Card 354: a `card354` skip row from
  caller 0x51DA8EE only if the client's own `ResetInventory` adds an item.
- 0 `WARN card073`, `card353`, `card354`; 0 new WARN kinds.

## Result (accepted 2026-11-14, b42 `8965a327d495d442`, run `b42`, scenario `b42-c356-c2m1`)

Fix: `c73_send_all(pc)` runs first in the grenades mechanism's join replay (`c74_join_replay`) and sends one absolute
GrenadeItem frame per item of the server copy's inventory. Confirmed cause: the copy's spawn-time `ResetInventory` runs
before possession, so `sprint_side` reads host-own and `c73_relay` returns without sending.

- First spawn: host `card356` row "1 of 1 items sent" at 342805371; client `GrenadeItem BP_GrenadeItem_Frag_C 2: our
  count 0 to 2, writer the host frame` at 342807009. HUD shot `client-b42-c356-c2m1-first-hud.png` shows grenades x2
  (viewed by the senior).
- After the checkpoint reload: client `card354` skip of its own `ResetInventory` add (caller exe+0x51DA8EE) at
  342874695, host replay "1 of 1" at 342874927, client 0 to 2 at 342878011. This also proves the loadout half of
  card 354.
- C1M1 replay "0 of 0", front-end PC "none sent"; 0 `WARN card073/353/354/356` in both logs.
- Not shown by shot: the count after the respawn (the shot caught the mission title screen); logs only.
- Sibling sweep: satchel rides the same replay; tactical support charges are replayed by card 310; ammo reserve has no
  gap.
- New client WARN kinds in this run are not from this card: cards 357 (`WARN card038`) and 358 (`WARN card191`).
- Play build 5 `d20f47aa1b0383a5` is not affected: it has no card 354 skip, so the client's own `ResetInventory`
  still adds the loadout locally. The missing count appeared only once card 354 made the host frame the only writer.
