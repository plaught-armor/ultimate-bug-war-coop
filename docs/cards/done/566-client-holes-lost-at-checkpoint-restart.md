# Card 566: client loses the range target bullet holes at a checkpoint restart

Playtest 2026-11-21, user report: "the bullet holes disappeared on the targets after host tried killing other
troopers and we checkpoint restarted". Asked which screen: client only (the host kept its holes).

## Cause (static, from logs)

- A checkpoint restart reloads the host in place but loads a fresh map on the client (`soak2m1b366b-client.log`:
  `UEngine::LoadMap enter` at the host restart, then `card132: client decal journal replay starts (J 79)`).
- The host's card 132 decal journal replays its live decals to the client after that load, but only gore decals were
  journaled (`decal_is_gore`: material name holds "Gore").
- Card 552 (b369) sent the host's bullet holes as card 136 effect rows (`c136_net_nested_decal`). A decal row is
  completed at once and no journal keeps it, so the replay after the client's reload has no holes. The host keeps
  its holes in place, so the screens differ.
- Run c551b370a also gave no client proof for card 552: the client plays decal rows through the original spawn
  (no card132 trace row), and `client_line` logs only with UBWCOOP_C136_LOG.

## Fix (b372 `84b30281df283193` = b371 + card 566, build-b5 copy kept, not run yet)

Card 132 relays every decal, not only gore (`decal_relayed`: any material). Its journal already has host-order FIFO,
own-spawn suppress on the client, E rows on release and the replay after a join or reload. So holes on targets,
walls, ground and props now follow the same path as gore, and the replay restores them after a checkpoint restart.
The card 552 nested c136 row (`c136_net_nested_decal`) and the client `inner_decal` block are removed: c136 skips
every decal (`counts.gore`), and a decal drawn inside a client cue replay is dropped by `c132_decal_suppress`.
Log texts: `host decal material N <path>: relayed`, `host decal caller ...: relayed / not relayed, local`, totals
`decals relayed`, `own decals suppressed`.

## Risks to check in the run

1. A decal the client draws that the host never draws is now lost on the client (counted in `own decals
   suppressed`). The client's own range shots must still leave holes: the host must draw holes for the client's
   shots.
2. Bytes: one D row per hole. Read the host totals' bytes and frames per minute of fire (smaller-packets rule).
3. The pool fills faster (holes plus gore, shared cap 500): same as single player.

## Predictions (C1M1, b372)

- Host: `card132: host decal material N .../MI_FX_BulletGround_Decal...: relayed`.
- Client: after the host fires at a range target, the client shows the holes (shot after the host fire). The client
  map totals show `own decals suppressed` > 0 and `spawned` near the host's relayed count.
- The client's own range shots leave holes on the client screen (shot), drawn from host rows.
- Checkpoint restart after holes exist: the client `decal journal replay starts (J N)` with N counting the holes, and
  a shot after the reload shows the same holes on both screens.
- 0 `WARN card132`.

### Run c551b376a predictions (junior, 2026-11-21, b376 `eb49926909111446` = b375 + card 568, moved from b372, scenario `tools/scenarios/c373-c1m1.txt`)

The host fires six bursts at range target C_1, then the client fires six of its own; then `host down host` and
`host travel checkpoint`.
- Host: `card132: host decal material N ...BulletGround...: relayed` rows (bullet holes now relayed, not only gore).
- Shots `c373-566-host-holes-client` and `c373-566-both-holes-*`: holes on C_1 on both screens.
- After the reload, the client logs `card132: client decal journal replay starts (J N)`, and the
  `c373-566-after-reload-*` shots show the same holes on both screens.
- Reported: client map totals (own decals suppressed / spawned), host totals bytes, WARN card132 count.

### Run c551b376a result and b378 fix (`3ed1d2468aee7c98`, build-b5 copy kept, not run yet)

The host relayed the hole material, but the client logged `WARN card132: client decal material 2
/Game/FX/Weapons/Aloft/Morita/MI_FX_BulletGround_Decal... not found` (x5) and `not spawned: no material`: the client
only looked the material up (StaticFindObject) and had never loaded it. b378: `client_material` falls back to
StaticLoadObject with the `UMaterialInterface` class, then registers the material in the game instance's
ReferencedObjects so GC keeps it (held for the session; only a few materials are relayed). Row:
`card132: client decal material N <path> loaded, held`.

Predictions (same scenario as run c551b376a): that row once for MI_FX_BulletGround_Decal, 0 `not spawned: no
material`, holes on C_1 on both screens before and after the checkpoint reload, 0 `WARN card132`.

## Closed 2026-11-21: passes in c551b380a

Logs `logs/c551b380a-host.log`, `logs/c551b380a-client.log`. Host relayed 165 decals (253 rows, 15210 bytes, 135 D
rows, 30 live). Client: 254 rows, 135 spawned, 108 released, 60 own suppressed, 27 live, 0 bad rows, 0 round
mismatches of 4, no `WARN card132`. After the checkpoint reload the journal replayed 36 entries and the client showed
30 of 30 live holes. Shots `host-c551b380a-c373-566-after-reload-host.png` and
`client-c551b380a-c373-566-after-reload-client.png` (lane 2 Screenshots dir) show the same holes on both screens.
