# 398 — Direct join: the client's loading screen copy needs no travel op

## Bug

Sibling of card 397. In play the host starts the first mission from the main menu and the client joins by IP
(`docs/PLAYTEST.md`). That is a direct join: no channel exists before the client's LoadMap, so no `TravelIds` op,
so `c397_show` makes no copy. Run c397b84: FED_C3M1 (direct join) black for 3986 ms, the five travelled maps
covered from +1 ms.

## Trace (static)

- `c397_show` (`render_c337.cpp`) returns unless `g_c397.have` and the op's map equals the world's name.
- The copy needs only the map name (known at `c337_load_map_return`), the chapter and level IDs, and a PC.
- IDs by map, from every host `card135: host ... screen up` row since 2026-11-10 (438 rows): FED_C1M1 0/0,
  FED_C2M1 1/0, FED_C3M1 2/0, FED_C3M2 2/1, FED_C4M1 3/0, FED_C4M2 3/1, FED_C5M1 4/0, FED_C5M2 4/1. So
  FED_C<c>M<m> = chapter c-1, level m-1. BUG maps do not follow this (BUG_C1M1 1/1) and get no fallback.
- The game's own screen still takes the host's IDs from IntroStart (`c135` logs `chapter A->B level C->D`; A and C
  are what the copy used), so a wrong fallback shows as A != B there.

## Change

`c398_ids` in `render_c337.cpp`: the `TravelIds` IDs when the op names this map, else the IDs from a
`FED_C<c>M<m>` map name, else no copy (as before). `c397_show` uses it and logs the source in a `card398:` row.

## Predictions (written before the run)

1. Direct join on FED_C3M1: `card397: client ... loading screen copy up on FED_C3M1 ... (chapter 2 level 0 set`,
   followed by `card398: client copy IDs from map name`. Delay after the cover is the wait for the client's PC
   (unknown, expected under 2000 ms; the black that remains is this delay).
2. The later `card135` client row on FED_C3M1 reads `chapter 2->2 level 0->0`.
3. Travelled maps: unchanged, copy up within a few ms, `card398: client copy IDs from TravelIds`.
4. A client shot after the copy-up row on FED_C3M1 shows the loading screen with the mission's title.
5. Scenario passes, 0 digest mismatches, no WARN class new against `logs/b72-warn-classes.txt`.

Budget: 3 runs.

## Result (b85 `0bf0a7701e990303`, run c398b85, 1 of 3 runs) — accepted

- Prediction 1 holds: FED_C3M1 direct join, copy up +1 ms after the cover (chapter 2 level 0 set), IDs from map name.
  The PC already exists at the LoadMap return, so no black remains after it.
- Prediction 2 holds: `chapter 2->2 level 0->0` at GameStarted.
- Prediction 3 holds: 5 travelled maps, copy up +0 to +1 ms, IDs from TravelIds.
- Prediction 4 holds: `client-c398b85-c398-c-direct.png` shows "OPERATION RAZORWIRE", the level image, "LOADING...".
- Prediction 5 holds: scenario passed (drive rc 0), mismatches 0, WARN host 17 and client 8 (b84: 18 and 10).
- Not covered: BUG maps on a direct join (names do not carry the IDs), and the engine's LoadMap itself before the
  cover (about 2.4 s on FED_C3M1).
