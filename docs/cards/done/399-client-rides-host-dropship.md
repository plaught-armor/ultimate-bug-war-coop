# 399 — The client rides the host's transport dropship and lands beside the host

## Bug

User, 2026-11-15: "operation seeker the client falls through map", then "this proposed fix should be for all
missions". On a map that starts in the transport dropship the host's pawn rides it (EnterDropship), but the client's
pawn spawns on foot at the PlayerStart: card 307 keeps it there ("nobody Up", the host rides) and card 015 skips the
client proxy's EnterDropship. On FED_C5M1 and FED_C5M2 that PlayerStart (-33920, 41090, -437) has no ground under it,
so once the card 320 hold ends the client's pawn falls until card 389 (6 s Falling) warps it beside the landed host.

Option A (senior, card 399): the server puts every non-host player into the host's transport dropship for the ride
and lands them beside the host. Keyed off dropship state, never map names.

## All missions (host logs, EnterDropship of the transport dropship at map start)

| Map | Ride | PlayerStart | card 307 today |
|---|---|---|---|
| FED_C1M1 | yes | (-560, -1715, 128) | nobody Up |
| FED_C2M1 | yes | (232629, 43682, 9092) | nobody Up |
| FED_C3M1, FED_C3M2 | yes | (-61946, -49703, 499), (-51756, -20145, 2032) | nobody Up |
| FED_C5M1, FED_C5M2 | yes | (-33920, 41090, -437), no ground | nobody Up, then card 389 |
| FED_C4M1, FED_C4M2 | no | (20792, -94902, -875), (-64388, 45630, 4246) | within 1500 uu |

BUG campaign maps are unplayed; the fix keys off the dropship state, so they need no list.

## Trace (static)

What the game does to the player for the ride. Every step names player 0 (GetSTPlayerCharacter,
GetPlayerController(world, 0)): on the server that is only the host's pawn and PC, a single-player assumption.

EnterDropship 0x1451C56C0 (authority only since card 015):
1. `+0x749 = 1` (player inside).
2. PossessNewTarget(world, dropship) 0x1452490E0: PC 0 possesses the dropship (AController::Possess, needs
   authority), the character is hidden (`SetActorHiddenInGame(char != target)`).
3. TeleportPlayer 0x145252970 to the seat component `[+0x760]` ComponentToWorld (+0x1E0): SetActorTransform.
4. `[+0x758]` vfunc +0x410(true): a visual on the dropship's own component (class unresolved, dropship side only).
5. AttachToComponent(pawn, `[+0x760]`, rules @0x149020A00 = KeepWorld x3, weld 0, NAME_None).
6. `+0x74C = CMC GravityScale` (CMC +0x1A0), then GravityScale = 0.
7. SetActorEnableCollision(pawn, false); SetActorHiddenInGame(pawn, true) (vtable +0x368).
8. SetManualControl(dropship, false).

DescendToLandCompleted 0x1451C2D10 (if `+0x749` and an ST PC): DetachFromActor(rules @0x149020A0C = KeepWorld x3,
call modify), GravityScale = `+0x74C`, TeleportPlayer to the exit component `[+0x768]`, PossessNewTarget(pawn),
SetActorEnableCollision(true), SetActorHiddenInGame(false); then `+0x749 = 0`, the landing delegate, sound, state.
There is no separate ExitDropship in the transport's path; SkipFlight(to_landing) (FAST=1, card 090) ends in the
same DescendToLandCompleted.

| Step on the pawn | Reaches the client by itself? | Card 399 does |
|---|---|---|
| Attach to the seat | yes: AttachmentReplication, `OnRep_AttachmentReplication` 0x1437EC680 has no role check, ACharacter has no override | server attaches; client follows |
| Teleport to the seat | yes, through the attach (relative transform replicates) | server teleports first (rules are KeepWorld) |
| bHidden | yes (replicated) | server hides and shows |
| bActorEnableCollision | no (not replicated) | server and client each call it |
| GravityScale | no (CMC local) | server and client each zero and restore it |
| Velocity | no | zeroed on both at board |
| Possession | the game moves PC 0 only; a remote PC keeps its pawn | no possession change (the roster, card 043, card 307 and every per-pawn system key off PC->Pawn) |
| Camera | the host's comes from possessing the dropship | client: SetViewTargetWithBlend(dropship) on its own PC |
| Input | the host's pawn is unpossessed | client: APawn DisableInput(pc) (vtable +0x340), EnableInput (+0x330) at release |
| Client move corrections | the client's CMC still sends ServerMoves while attached | server: CMC +0x555 bit 0x02 (bIgnoreClientMovementErrorChecksAndCorrection, from its NewProp SetBit 0x1439D22D0) set for the ride |

Possession stays: unpossessing the remote pawn would empty `PC->Pawn`, which the c43 roster, card 307 (a pawnless
player gets a new spawn), sprint sides and the digest all read.

## Change

One owner, the dropship code (`dropship.cpp`), card 307 and card 389 unchanged:

- Host step `c399_step` (per frame beside `c389_step`): the host rides when PC 0's pawn is a transport dropship with
  `+0x749` set. Each other player's pawn (server copy, player character, not attached to that dropship) is boarded:
  SetActorTransform to the seat's ComponentToWorld, AttachToComponent with the game's rules, GravityScale saved in a
  rider row (weak ref) and zeroed, velocity zeroed, collision off, hidden, error checks off. Covers the start, a
  client that joins during the ride, and a respawn during it.
- Release, same step: a rider whose host is no longer on that dropship (DescendToLandCompleted, FAST's SkipFlight,
  anything else) is detached (the game's detach rules), GravityScale restored, error checks back on, collision on,
  shown, then `c46_warp` beside the host's landed pawn (Warp by NetGUID, or WarpTo by position, to the client). A
  rider row whose pawn is gone (client left) is dropped with a row; the pawn's destruction takes its attach along.
- Late join after the landing: the host does not ride, nothing boards, card 307 places the pawn beside the host.
- Client `c399_client_step` (own-pawn watch, every tick): its own pawn attached to a transport dropship is a ride.
  Edge on: GravityScale saved and zeroed, velocity zeroed, collision off, DisableInput, view target the dropship.
  While riding the view target is held on the dropship. Edge off: GravityScale, collision, input and the pawn's own
  view back.
- `holding_dropship` (card 015 guard and its users): a server copy or the client's own pawn attached to a transport
  dropship is held without `+0x749` (the client proxy never sets it; on the server only the host's pawn gets it).
  The host's own pawn and other proxies keep the flag test.
- Card 110: a label slot whose pawn is hidden is hidden (no label over the riders inside the dropship).

## What the client sees (card 320 hold)

The client's pawn is boarded under the hold (the world is paused, nothing falls). The client reads its title screen;
once it closes, its camera is the dropship's camera, the same actor and component the host's view comes from. If the
host has not confirmed yet the world is still held and the client sees that still frame; when the hold ends both
watch the same flight (full ride) or the same short skip to landing (FAST=1), then each is on foot at the exit, the
client beside the host.

## Predictions (written before the run)

1. FED_C5M1, FED_C5M2, FED_C3M1, FED_C3M2: a `card399: host ... board` row for the client's copy before the hold
   ends, a `card399: host ... release` row and a `card046 ... warp` row beside the host within 1000 ms of the host's
   `DescendToLandCompleted` row.
2. Same maps: 0 `card389` rows; no client own-pawn z under its PlayerStart z - 200 at any client row after the
   release.
3. FED_C4M1, FED_C4M2: no `card399` row, card 307 rows `within 1500 uu` as before.
4. Client rows: `card399: client ... ride on` with view target the dropship, `ride off` after the release.
5. A client shot during the ride shows the dropship view (the host's view), one after the landing shows the client on
   foot beside the host; both viewed.
6. 0 digest mismatches, no WARN class new against `logs/b72-warn-classes.txt`, 0 `pawn-collision: FIX` rows during
   the ride.

Leave and join (FED_C5M1, full ride):

7. The client disconnects during the ride: `card399: host ... rider row N dropped: pawn gone` within 1 s of the
   drop, no WARN. Back by the card 045 follow: if the host still rides, a new `board` row and a `release` at the
   landing; if it has landed, no `board` row and card 307 places it beside the host.
8. The client disconnects after the landing and comes back: card 307 `spawns beside`, 0 `card399` rows for it.

Budget: 3 runs. Run a: `tools/scenarios/c399-ride-chain.txt`, the c388 chain with the full ride (FAST=0), all four
ride maps and both C4 maps, predictions 1 to 6. Run b: `tools/scenarios/c399-c5m1-leave.txt`, predictions 7 and 8.
FAST=1 (SkipFlight ends in the same DescendToLandCompleted, and the release keys off PC 0's pawn, not the call) is
the third run if a and b leave it unproven: the c388 chain as it is (FAST=1, AUTODROPSHIP).

## Run a (c399b86a, b86, full ride, `c399-ride-chain.txt`)

357 s, rc 0/0, 0 FATAL/ERROR, WARN host 13 / client 17, no dumps. Board and release rows on all four ride maps, the
client view held on the dropship. User play on b86 then found three faults:

- FED_C3M2 (Obsidian): the client did not land beside the host.
- The client's crosshair (trooper HUD) showed during the flight.
- The client did not face the host's direction after landing.

## Fixes (b87 `8721e1e1ffc0aa1d`)

- Warp while riding (root cause of Obsidian, all maps). The release Warp reaches the client before the replicated
  detach. While the own pawn is attached, `c23_own_pawn` returns null (card 399's `holding_dropship`), so
  `c46_warp_apply` and `c46_receive_warp_to` dropped the warp on every map. C3M1 and C5M1 only landed near the host
  because the own pawn had stayed near the seat. New `c399_client_land(pc)`: when the warp finds no own pawn and
  the PC's pawn is the riding pawn, it detaches it there (the game's detach rules), runs the ride-off edge, and the
  warp applies.
- Facing: `c399_client_face(pc, target)` sets the control rotation to the host pawn's root yaw after a Warp landing.
  The host release row now logs the host yaw. WarpTo (position only) has no facing source and sets none.
- HUD: while riding, the HUD manager's (PC +0x358) `SpecificHUD` (WB_Gameplay_Trooper_HUD_UI on foot: crosshair,
  objectives, minimap, health, ammo) is collapsed, its visibility saved; restored at the ride end. The host's own
  SpecificHUD during the ride is WB_PlayerDropship_HUD (an empty overlay), so the client now matches it.
  PersistentHUD (location text, dialogue) stays, as on the host.
- Diagnostic: client `ride position` rows (own pawn, dropship, gap; once a second, only when the gap moved 500 uu,
  cap 32) to find why the C3M2 own pawn sat about 5400 uu from the seat.

## Predictions, run b (c399b87f, b87, FAST=1 SkipFlight, `c399-fast-chain.txt`)

9. Every ride map: a client `card399: client ... Warp while riding: own pawn detached here` row, then a `card046`
   client Warp row, no `WARN card046` row, then `ride off`.
10. Every ride map: a client `landing facing: control rotation yaw Y` row with Y within 1 degree of the host release
    row's `host ... yaw`.
11. Every ride map: a client `ride HUD ... hidden` row after `ride on` and a `ride HUD ... shown` row at the ride end.
12. FED_C3M2: the client's landed shot shows the host beside it; the first client position row after the landing is
    within 1000 uu of the host's landed pawn.
13. The client ride shots show no crosshair or trooper HUD; the landed shots show the client facing the host's way.
14. Predictions 1, 3, 4 and 6 hold as in run a (0 `card389` rows, 0 digest mismatches, no new WARN class).

## Run b (c399b87f, b87, FAST=1, `c399-fast-chain.txt`)

248 s, rc 0/0, 0 FATAL/ERROR, WARN host 20 / client 15 (no new class), 0 mismatches, no dumps, 0 `card389` rows,
0 `pawn-collision: FIX` rows.

- Prediction 9 holds on all four ride maps, through WarpTo, not Warp: off the dropship the host's pawn has no sendable
  NetGUID (as card 389 found), so the host sent `WarpTo` and the client applied it after `Warp while riding`.
  FED_C3M2: the client landed at (-51200, -21064, 2186), the host's pawn +100 uu. Obsidian is fixed.
- Prediction 10 failed: WarpTo carried only a position, so no `landing facing` row (the c3m2 landed shots face
  different ways).
- Prediction 11 holds (hidden after ride on, shown at the ride end, all four maps); the client ride shot shows no
  crosshair or trooper HUD.
- Ride position rows: one per ride, gap 450 (the seat over the dropship origin), no drift. The b86 C3M2 offset came
  from the dropped warp, not from the ride.

## Fix (b88 `0e0874efd403979f`)

`c46_warp`'s WarpTo now carries the target pawn's root yaw as a fourth field ("x y z yaw"). The client reads 3 or 4
fields; a ride landing (`c399_client_land`) with a yaw sets the control rotation to it (`landing facing ... (host
yaw in WarpTo)`), one without logs a WARN. The card 146 rescue WarpTo stays "x y z".

## Predictions, run c (c399b88l, b88, full ride, `c399-c5m1-leave.txt`)

7 and 8 as written; plus 15: the release after the rejoin has a client `landing facing: control rotation yaw Y (host
yaw in WarpTo)` row with Y equal to the host release row's yaw, and the landed shots face the same way.

## Run c (c399b88l, b88, full ride FED_C5M1, `c399-c5m1-leave.txt`)

110 s, rc 0/0, 0 FATAL/ERROR, WARN host 1 / client 4, no dumps, scenario passed.

- Prediction 7 holds: client drop during the ride, `rider row 0 dropped: pawn gone` 7 s after the board row, no
  WARN; the client came back 7.3 s after the drop while the host still rode, a new `board` row, then the `release`
  25 ms after the host's DescendToLandCompleted.
- Prediction 8 holds: the second drop after the landing, back by card 307 `spawns beside`, no card399 row.
- Prediction 15 holds through the Warp (by NetGUID) path: host release yaw -45.0, client `landing facing: control
  rotation yaw -45.0 (host pawn by guid)`; the landed shots look the same way. The WarpTo yaw path (FAST, a host
  without a sendable NetGUID) is built but not yet run.
- Ride shot: the dropship's camera, no HUD.
- Open nit: the client's landed shot has no objective list panel where the host's shows one (a new objective just
  added on the host). Not traced; check in the user test.

Budget: 3 of 3 runs used. Handed to the user test (FAST skip and full ride).

## Senior review (2026-11-15 20:30): accepted

Read: `dropship.cpp` card 399 block, `objectives.cpp` `c46_warp`, `c46_warp_apply`, `c46_receive_warp_to`. Checked in
the logs of `c399b88l` and `c399b87f`: 0 `card389` rows, 0 mismatch rows, 0 FATAL/ERROR, no `WARN card399/046/146`,
`Warp while riding` then `ride off` then the landing position on FED_C3M1, C3M2, C5M1, C5M2, landing facing yaw -45.0
equal to the host's on FED_C5M1 (Warp by guid).

Not proven, carried to card 400's run: the `WarpTo` yaw path (b88, FAST landing). Known edges, not fixed: a ride that
ends with no host pawn on foot leaves the rider at the seat with gravity back (card 389 is the rescue);
`kCmcIgnoreClientErrors` is cleared at release without its earlier value; the client's landed view lacks the objective
list panel the host shows (not traced).
