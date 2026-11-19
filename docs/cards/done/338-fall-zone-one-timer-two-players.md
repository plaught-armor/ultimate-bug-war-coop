# Card 338: a fall zone has one reposition timer, so a second player drops the first

**Status:** fixed in lane 2 build b19 `40e935ba1137d921` (`mod/src/ubw/fallzone_c338.cpp`). Proof run `c338-c337`
(scenario `tools/scenarios/c338-c337-c1m1.txt`, run 3) passed: two forced rows and two timer rows (rates 0.40 and
0.50 s), both 5 s rows read `input 1` and movement mode 1 (moved 538 and 529 uu), the client skipped two non-own
overlaps, and there were 0 WARN card338.
Found from card 336 (r6 cmd 144): the host's pawn and the client's server copy entered one `BP_FallZones_C` 5 ms
apart; only the client was moved.

## Trace (static, b16)

- `ASTFallZonesBase::OnBeginOverlap` (0x05161B40). Gates: no save load in progress, zone+0x310 enabled, the other
  actor IsA `ASTPlayerCharacter`, and its ASC has `Player.IsPlayer` (else InstaKillTarget). There is no authority
  gate and no movement-mode gate. It then runs `ForceInputEnabled(GetSTPlayerController(zone), false)`, cancels the
  abilities tagged at zone+0x330, and adds `Tag_Actor_InFallZone`. Last, it arms zone+0x3A8 (one `FTimerHandle`):
  `RepositionPlayer(weak GetSTPlayerCharacter(zone))` after zone+0x358 s. Inside the card 102 overlap scope,
  player 0 is the overlapper, so the weak pawn is right; the second overlap re-arms the same handle.
- `RepositionPlayer` (0x0516BA90) clears +0x3A8 and calls `USTCommonUtilityLibrary::RepositionPlayer` (0x0524BA90).
  That call adds a tag, sets the movement mode to 5 (flying), zeroes velocity, moves the pawn to the zone's point,
  and calls `SetControlRotation` on `UGameplayStatics::GetPlayerController(world, 0)`. It then arms zone+0x3B0:
  `EndReposition(weak pawn)` after zone+0x370 s.
- `EndReposition` (0x05155620) removes `Tag_Actor_InFallZone` and calls `PostReposition` (0x05249810). That call
  removes the reposition tag and, if the pawn is not dead, turns on input for `GetSTPlayerController(zone)` and sets
  movement mode 1 (walking).
- Both timer callbacks run with no overlap scope, so player 0 is the host's controller for every pawn: a client's
  reposition turns the host's camera, and the client's end turns on the host's input.

## State of the dropped player (build 5)

- **Host first, client second:** the host is never moved. Its input was switched off at its overlap, then switched
  back on by accident when the client's end called `PostReposition` (player 0 is the host). It keeps
  `Tag_Actor_InFallZone` and loses its cancelled abilities. It stands where it entered.
- **Client first, host second:** the client is never moved; the flag switched off is the client's server-side
  controller's, which does not stop the client's own input. It keeps the tag.
- **Second reposition inside zone+0x370 of the first:** the first pawn's end is dropped. It stays in flying mode
  with both tags and its input off (for the host, input comes back by the accident above).
- So input is not hard stuck in build 5. The risks are a pawn left in flying mode, the kept tags, a pawn not moved,
  and the host camera snapping on a client's reposition.

## Fix (host)

- `RepositionPlayer` and `EndReposition` run inside the pawn's card 102 acting scope, so player 0 is that pawn's
  controller (camera, input).
- A table per zone remembers the pawn each handle is for. An overlap by another player while a reposition is
  pending runs that pending reposition first; a reposition while another pawn's end is pending runs that end first.
- Client: the client's own copy of the zone also runs `OnBeginOverlap`. With another player's pawn as the
  overlapper, its player 0 is the client's own pawn, so the client skips the original; the host owns other players'
  fall zone reaction. The client's own pawn still runs it.
- Rates zone+0x358 and zone+0x370 are not known statically; the reposition row logs them.

## Rows

- `card338: host t=<ms> zone <zone> at (x, y, z): <pawn> repositioned to (x, y, z)[, pending, repositioned before <pawn2> armed], rates <a> <b> s`
- `card338: host t=<ms> <pawn> input <0|1>, moved <n> uu since the reposition, movement mode <m>` (5 s later)
- `card338: client t=<ms> zone <zone>: overlap by <pawn>, not the own pawn, skipped`

## Predictions (c338-c337 run)

- Two reposition rows within 10 s of the two warps, one per pawn; the first names `pending, repositioned before`.
- Both pawns end within 600 uu of the zone's recovery point; both 5 s rows read `input 1` and movement mode 1, and
  after a 1 s W press both move 100 uu or more.
- No host camera turn on the client's reposition; 0 WARN card338.
