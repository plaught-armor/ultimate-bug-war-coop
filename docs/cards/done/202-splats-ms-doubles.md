# Card 202: The client does not show a Splats_MS splat twice

Status: queued 2026-11-12, split from card 148 ("Out of scope"). Budget: three runs. Static first.

Read: `docs/cards/done/148-gib-sync-client-apply.md` (Trace, Splats_MS lines), `mod/src/ubw/fx_c136_net.cpp`
(`c136_client_own`).

## Why

hb72 client: 12 of 225 Splats_MS within 40 ms and 100 uu of another one. The client spawns its own splat and also
gets the host's relayed one. The filter that decides "the client makes this one itself" is `c136_client_own`.

## Do

1. Trace, no run: pair the client's doubles by host id against the host's Splats_MS lines; name which of the two is
   the client's own and why the filter lets both through. Check the sibling FX classes that go through the same
   filter.
2. Fix in the filter, for the class.
3. One run with gibs near both players: 0 doubled splats; relayed FX totals otherwise unchanged.

## Done when

- The trace, the fix, the run lines. The Report ends with "Report complete."

## Step 1 answers (run `c243-c3m2-a1` logs, senior, 2026-11-13): no mod defect, closed

- The doubles are the host's own. Host: 987 `Splats_MS` rows, 98 pairs within 40 ms and 100 uu. Client: the same
  987 host ids, each applied once (0 ids twice, 0 unknown ids), 105 pairs by arrival time. The 7 extra come from
  rows of one frame arriving together.
- The client keeps none of its own: 0 `client fx own` rows for `Splats_MS`; its 760 native calls are blocked by
  the gore part rule in `c136_client_own` (`fx_c136_net.cpp:1794`, card 137 step 6a).
- Why the host doubles: two gore parts land together (fx 3294 and 3299, 17 ms and 28 uu apart, two parts), and
  the game calls twice for one sticky part (fx 3903 and 3908: same position and rotation, 1 ms apart, one native
  audit row each). A standalone game does the same.
- The hb72 figure (12 of 225) is the same share as here (98 of 987). Nothing to fix in the filter.
