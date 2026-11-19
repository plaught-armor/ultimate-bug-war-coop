# Card 400: unconfirmed client trooper hidden on the host

User report (2026-11-15): "client player is on map before the client has pressed enter to continue", seen on the host
screen at map start. Wanted: the client's trooper stays hidden and cannot be hit until that client presses Enter, then
appears beside the host (or rides the dropship). User follow-up: "it will also prevent enemies from tracking onto them
before they have had a chance to get into the game".

## Design (build b89 `1c6a06618c02d9ac`)

- Confirm state: card 320 owns it. `C320Hold` gains a `done` set (IntroDone received on this map), kept next to
  `sent` (IntroStart sent on this map), cleared with `g_c320` at map end and on Logout. New export
  `c320_unconfirmed(pc)` = in `sent` and not in `done`. A PC never sent IntroStart is not unconfirmed, so it is never
  hidden.
- Client, intro_c135.cpp: an IntroStart refused because GameStarted already ran now sends IntroDone (no screen
  follows), so the host never keeps that PC hidden.
- Owner of the hidden state: card 400 rows in dropship.cpp (`g_c400`, one weak ref per pawn), stepped from
  `c399_step` every 100 ms on the host. A remote player's server copy with `c320_unconfirmed` gets a row and
  `SetActorHiddenInGame(true)`; when it is no longer unconfirmed the row goes and the pawn is shown, unless it rides
  (card 399 shows it at the release). Card 399's release shows a pawn only when it has no card 400 row. Collision is
  not changed (a rejoined pawn must stand on the ground).
- Damage: death.cpp `c400_damage_block` puts back any Health loss of a pawn with a row (the card 132 god way), before
  the god block.
- Enemy targeting: enemy_c103.cpp, the card 108 path that already keeps Downed players from being targeted. A pawn
  with a row never counts as standing, is skipped as primary target and in the nearest search
  (`c103_player_slot`, which also drives the card 103 GetPlayerController gate for perception, tickets, line of
  sight and facing), and SetPrimaryTarget onto it is redirected to the nearest other player pawn (none: no target).
- Card 110 marker: already hides a slot whose pawn is hidden (card 399 rider case); covers this too.

## Senior's points

1. Enemy AI: the card 108 Downed path, above. Rows: `card400: host ... hide` and `... show`, one each per pawn;
   `card400: host ... enemy ... primary target slot 1 (client not confirmed) -> slot 0` per redirect (first 12).
2. Every exit shows or ends the hidden state: confirm (`card320: ... confirm from client slot N`, then
   `card400: ... show ... client confirmed`); the card 344/355 hold timeout and a client leaving go to the menu and end
   the map (`card400: ... row N dropped: pawn gone`); Logout drops the PC from `sent` and `done` and destroys the pawn
   (same row); map end clears `g_c320`; a PC that never got IntroStart is not in `sent`, so no hide row is written.
3. Rejoin mid-mission: `c135_join_replay` sends IntroStart to the new PC (a new object index, so not in `done`), so
   `sent` is true and the pawn is hidden from that send until its IntroDone: while the client loads and sits on its
   screen. A PC that joins before the host's screen ran gets no op and is not hidden.
4. Card 399: one owner (card 400 rows); release shows only a pawn without a row; a rider that confirms mid-ride keeps
   hidden as a rider (`show ... rides: stays hidden until the card 399 release`); an unconfirmed rider released stays
   hidden (`released rider ... stays hidden`). Card 110 marker follows bHidden.
5. Host shots: `c400-h-start-before` (map start, before the client's Enter), `c400-h-rejoin-before`,
   `c400-h-rejoin-bugs` (no trooper beside the host), `c400-h-rejoin-after` (trooper visible).
6. WarpTo yaw: the FAST landing on FED_C3M1 waits on `landing facing ... (host yaw in WarpTo)`.

## Run a predictions (`tools/scenarios/c400-hidden.txt`, runid c400b89a, FED_C3M1 FAST=1 AUTODROPSHIP)

1. Map start: `card400: host ... hide ... (slot 1): client not confirmed yet (riding the dropship)` before the client's
   Enter; no client trooper in `c400-h-start-before`.
2. At the client's Enter: `confirm from client slot 1`, then `show ... client confirmed, rides: stays hidden until the
   card 399 release`; the release shows it (`card399: host ... release`, no `stays hidden` row).
3. Landing: client row `landing facing: control rotation yaw Y (host yaw in WarpTo)`, Y equal to the host release row's
   yaw within 1 degree.
4. Rejoin: `hide ... (on foot)` since the mark, card 307 spawns beside; `c400-h-rejoin-before` and `-bugs` show no
   client trooper beside the host.
5. Bugs: no client Health loss on the host (any `card400: ... damage ... blocked` row means a hit was stopped); any
   SetPrimaryTarget onto slot 1 shows as a redirect row; bugs fight the host.
6. Client Enter: `confirm from client slot 1 (not waited for ...)`, then `show ... client confirmed`;
   `c400-h-rejoin-after` shows the trooper.
7. 0 `card389`, 0 mismatches, no new WARN classes against c399b87f.

## Run a result (c400b89a, b89): two findings, one scenario fault

- Predictions 1, 3, 5 held: hide (riding) at map start 57 ms after the IntroStart; landing yaw 5.0 on the client,
  host yaw 5.0 in the release row; on the rejoin 12 SetPrimaryTarget redirects slot 1 -> slot 0 by four warrior bugs
  and 8 Health losses (8.8 to 25.0) put back to 100.0, 0 `card389`.
- Prediction 2 differed: FAST lands during the hold, so the release and the show fell in the same step
  (435497847): `released rider ... stays hidden`, then `show ... client confirmed`, no mid-ride confirm. The 2 s from
  the confirm (435495814) to that step is a game-thread stall after the unpause (no log row on any system between
  435495814 and 435497828), so the show came on the first host step; not a defect.
- Defect: on the rejoin the pawn spawned at 435515391 (card 307) but its IntroStart went out only at the join
  replay, 435520258, so the client's trooper stood visible and targetable on the host for 4.9 s while the client
  loaded. b90 `318b8510208b97e0`: `c320_unconfirmed` is also true while the join replay will send (the host's screen
  ran, the map is set, no IntroDone yet); a join replay whose send fails puts the PC in `done`, so it shows.
- Scenario fault: the `look` row is written before the look step returns, so `wait log ... new` missed it; the
  scenario now marks before the look and waits since the mark.

## Run b predictions (c400b90a, b90)

1. Rejoin: the `hide ... (on foot)` row at or before the `card307 ... spawns beside` row (t no later than the spawn
   plus one 100 ms step), not at the join replay.
2. Map start and the rest as run a: hide riding, release and show, landing facing yaw equal to host yaw, redirect and
   damage-blocked rows on the rejoin, `confirm from client slot 1 (not waited for ...)` then `show`.
3. Shots: `c400-h-start-before`, `c400-h-rejoin-before` and `-bugs` show no client trooper; `c400-h-rejoin-after`
   shows it.

## Run b result (c400b90a, b90 `318b8510208b97e0`): predictions held

- Rejoin: `card307 ... spawns beside` at 435758321, `hide ... (on foot)` at 435758344 (23 ms, was 4.9 s on b89).
- Host shots: `c400-h-rejoin-before` and `-bugs`, looking at the pawn's spot from 170 uu: warrior bugs only, no
  trooper, no marker; `c400-h-rejoin-after`: the trooper stands there. `c400-h-start-before` is the host's flight
  screen (FAST), so the map-start proof is the hide row 57 ms after the IntroStart, not a picture.
- Rows: hide 2, show 2, released-stays-hidden 1, 12 redirects slot 1 -> slot 0, 8 damage blocks (Health 100.0 put
  back), `confirm from client slot 1 (not waited for ...)` then `show` 101 ms later; landing yaw equal to host yaw.
- 0 `card389`. New WARN classes against c399b87f (a ride run with no kills): card132 CollisionOp
  NODE_AddStaticMeshComponent "no placed copy matches" at map load (before any card 400 row) and card137 gib/bone
  checks from the killed bugs; none on a card 400 path.

## Senior review (2026-11-15 20:45): accepted

Code read in full (dropship.cpp card 400 block and the card 399 release, intro_c135.cpp `c320_unconfirmed`,
`c320_receive_done`, `c320_logout`, the join replay and the client refuse path, death.cpp `c400_damage_block`,
enemy_c103.cpp `c103_player_slot` and `c108_target`). Run b rows checked in `logs/c400b90a-host.log`: hide 23 ms after
the card 307 spawn, 12 redirects, 8 damage blocks, confirm then show 101 ms later, landing yaw 5.0 on both sides,
0 mismatches, 0 `card389`. The CollisionOp "no placed copy matches" WARN is not new (in `c387b72b`, `c388b72`,
`c394b81` to `c396b83` client logs); the card 137 rows come from kills, which `c399b87f` had none of.

Attacked: host PC (never in the loop, slot 0 and the server-copy test); `c103_player_slot` returning -1 (the caller
keeps the game's own result); every player down with one unconfirmed (falls back to "any player", as card 108);
object index reuse after Logout (dropped from `done`); a pawn destroyed with a row (weak ref drops the row).

Leftovers, not blocking:
- Client IntroStart "frame unreadable" and "no weak assign" WARN paths send no IntroDone: that trooper would stay
  hidden for the map. Both need a build mismatch or a missing resolve.
- The widened `c320_unconfirmed` (screen seen, map set, not in `done`) also covers a PC whose map-start IntroStart
  send failed; not traced whether that path marks `done`.
- Not run: a confirm in mid-ride (FAST lands during the hold); host Downed while the client is unconfirmed.
- Map-start proof is the hide row, not a picture (FAST shows the flight screen).
