# Card 410: per-frame magazine text, once-a-second actor walk, dead-set prune every frame

Efficiency audit (card 329 rule). Build b102 `a685603c45e393f5`, lane 2 only. Measured with the card 330 step
timers on the fast chain (FED_C3M1, C3M2, C4M1, C4M2, C5M1), run `c325b101a` (b101) against `c410b102a` (b102).

## Changes

1. `c72_client_step` (`death.cpp`) built the magazine text with `snprintf` every frame only to read the round
   total. New `c274_magazine_rounds(pawn)` (`ammo.cpp`) sums the rounds without text.
2. `c22_step` (`sprint.cpp`) walked every actor of the map once a second for the player audit. It now uses the
   cached player list of card 368 (`c368_actors`).
3. `c37_scan` (`enemies.cpp`): `c37_last_seen` tries the same index first; `c38_prune` runs only when a pawn left,
   revived or the count changed, not every frame.

## Result (sum over the 5 maps)

| step | side | b101 | b102 |
|---|---|---|---|
| `c22_step` | host | 61.2 ms, max 1.93 ms, 13 calls over 1 ms | 6.5 ms, max 0.51 ms, 0 calls |
| `c22_step` | client | 59.2 ms, max 1.33 ms, 7 calls over 1 ms | 8.8 ms, max 0.94 ms, 0 calls |
| `c72_client_step` | client | 54.2 ms, max 0.98 ms | 31.6 ms, max 0.23 ms |
| `c37_step` | client | 92.5 ms | 86.3 ms (within run noise: no proven gain) |

Checks: 22 rounds `mismatches 0` on both runs; `card022 ... audit` lines 24 client and 30 host on both runs; first
dead-set prune line present on both; client and host WARN classes unchanged (host 15 lines both runs).

## Left open

- `c37_step` on the client still costs about 23 us a frame; the search and prune were not the main cost. Next read:
  the per-pawn work of `c37_scan` itself.
- `c28_step` on the client: one 128 ms call (145 ms in b101) in the transition world after FED_C5M1, during the load
  screen. Not traced.
- `auto_dropship_step` (host, 2827 ms, max 975 ms) is the test driver (`UBWCOOP_AUTODROPSHIP`): not in a play
  session.
