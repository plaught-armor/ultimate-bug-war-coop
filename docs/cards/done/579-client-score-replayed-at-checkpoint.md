# Card 579: client score paid again by the checkpoint snapshot replay

Found in run c397b398a (2026-11-21, b398).

## Bug

After the checkpoint reload, the host restored a FedScore of 70000 from its save. The client's objective journal
replay (card 049 snapshot frames, flags 0x140) re-ran 9 sub-objective completions (ShootingRange_SO0-3,
Assault_SO0-4). Each one paid 10000 again, so the client went from 0 to 90000 and stacked "+10,000 Objective
Complete" toasts. The card 475 replay close saw 90000 against the host's 70000 and only logged a WARN. Round 11 of
the card 132 digest then showed a Score mismatch.

## Fix (b399 `3aecb88abac8f6f7`)

1. `c579_drop_award` (objectives.cpp), called first in both AddFedScore detours (player_c99.cpp). It drops an
   award on the client when it falls inside a subsystem ObjectiveComplete (`g_c491_window`) that runs inside a
   snapshot or join replay frame (`g_c435.join`). Live completions still pay on both sides. Row: `card579: client
   ... dropped replayed award <tag> <amount> inside a snapshot or join frame (N dropped)`.
2. `c475_close`: a client total that differs from the host's after the replay is set to the host's (raised or set
   down, no broadcast). Row: `card579: client ... total N set to the host's M after the replay`.

## Proof (one checkpoint restart run)

- The client logs `card579 ... dropped` rows, one per replayed sub-objective. No "+10,000" toasts show on the
  client after the reload (shot after the reload).
- After the reload, the client total equals the host's. Card 132 Score mismatches are 0.
- Live completions before and after the reload still log `card491 ... score A -> A+10000` on both sides.

## Budget

1 run, shared with the card 570 regression proof.

## Result c397b400a (b400 `9d4f79c052aea9a7`, junior 2026-11-21): passed, closed

- 9 `card579: client ... dropped replayed award FedScoreSystem.Reward.ObjectiveComplete 0` rows at
  160204020-021. The amount reads 0 because these rows come from the tag-only `detour_c99_score` path, which has no
  amount argument. The text path did not log a second row.
- No `set to the host's` row: the totals already matched. Client card475 `score 0 -> 70000 (1 rows applied)`, host
  `total 70000 sent to slot 1`. The toast shot shows 70,000 and no `+10,000` toasts.
- card132 Score mismatches are 0 in all 11 rounds, including round 11 after the reload. The client card132 and
  card475 WARNs are gone (2 and 1 in b398).
