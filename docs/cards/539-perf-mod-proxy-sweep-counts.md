# Card 539: perf mod, client NPC proxy capsule sweep counts (diag)

Family: card 527 (performance mod).

## Why

c263L356 (b351, card 534 rows, client): ComponentSweepMulti by BP_AI_WarriorBug_C is the largest client query:
82993 calls, 1550.5 ms (5.17% of the GameThread), 18.7 us mean. UpdateOverlaps by the same class is 2.84%. On the
client these come from simulated proxies (SimulateMovement -> MoveSmooth -> SafeMoveUpdatedComponent ->
UPrimitiveComponent::MoveComponentImpl with a sweep). The host's NPC is the authority.

## What (b352)

`mod/src/ubw/perf_move_c539.cpp` runs on card 461's MoveComponentImpl hook row, which is now installed when
diag is on or card 539 is on. Outside card 461's release window, detour_c461_move calls c539_move. It counts
client sweeps of components whose owner is an NPC simulated proxy, with a nonzero delta:
- how many were blocked (out hit Time < 1);
- how many followed an unblocked sweep of the same component;
- how many of those were blocked.
It changes no result. The `card539:` row comes every 60 s. `UBWCOOP_C539_OFF=1` turns it off.

## Decision rule

- If "blocked after an unblocked sweep" is below 2%: card the lever. A proxy whose last sweep was unblocked moves
  unswept for at most 3 calls, with a 1 in 256 verify as in card 535. That is about 50 ms late on first contact,
  within the 100 ms target.
- Also check UpdateOverlaps: an unswept move runs its own overlap query. The lever must show the sum of sweep and
  UpdateOverlaps ms dropping in the card 534 rows.
- If it is above 2%: drop the lever.

## Result (c263L359, b353): skip lever dropped

- Final client row: 312219 NPC proxy capsule sweeps, 46.2% blocked; after an unblocked sweep 163216, of those
  blocked 22613 (13.85%). The bar was below 2%, so a skip after unblocked sweeps would miss one blocking hit in
  seven. The skip lever is not carded.
- The counts cost under 0.1% of the client GameThread (detour_c461_move 4 of 9459 samples, c539_move 0).
- Mismatches 0, CRASH 0. b353 promoted.
- Lead, not carded: 46% of proxy sweeps block, so most of the 5.17% is proxies sweeping into each other or
  into the world. Whether proxy-against-proxy blocking matters on the client needs its own trace first: which
  component is hit, and whether host positions overwrite the result anyway.

## Blocker split (c263L361, b355, client, final row)

NPC proxy capsule sweeps 325004, blocked 166169 (51.1%):
- by NPC 143033 (86.1% of blocked);
- by world 22165 (13.3%);
- by pawn 971 (0.6%);
- start penetrating 154216 (92.8% of blocked, 47.4% of all sweeps).

Client proxies mostly sweep into other proxies that they already overlap. The host is the authority for NPC
positions and resolves NPC-to-NPC overlap itself, so on the client this blocking is wasted work. It can also hold a
proxy away from its replicated position. Follow-up: card 542.
