# Card 122: More bugs attack at once in co-op

Status: written 2026-11-06 (senior); sent and accepted 2026-11-06.

Read:

- card 115 Report (`mod/src/ubw/attack_cap.cpp`: `CanPerformAttack` 0x051806D0, `RequestTicket` 0x050FF6D0, the
  per-window table; no ticket cap binds, AI pacing sets attackers at once);
- card 111 and card 113 Reports (`mod/src/ubw/bug_scale.cpp`: `c111_players`, the factor, the session count).

Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`. DLL base: `f47d4dba55177c72` (card 121).

## Why

User decision (2026-11-06): yes, co-op makes more bugs attack at once. Card 111 scales the wave sizes and the ticket
limits. Card 115 showed that the ticket limit is never reached: 14-19 bugs made attack checks per 5 s window, at most
5 held a Melee ticket at once, 0 requests were refused, and peak use was 6 of 160 points. So what holds attackers
back is the bugs' own pacing, and that is what this card scales.

## Do

1. **Measure (static, then one log run with both players in a WarriorBug fight, as in c115a).** Find what keeps a
   bug that is in the fight from attacking. Name each candidate with its offsets and values, and say which one binds
   (the one the per-window counts follow):
   - per-attack cooldown (`FSTEnemyAttackDataConfig` `CooldownTime`, card 115) and any global or per-enemy attack
     delay;
   - approach, slotting or surround logic that holds bugs in a ring, or in a queue, until a slot frees (behavior tree
     decorators, EQS, a target's attacker count);
   - target choice: whether bugs spread over both players, or most of them go for one;
   - the ticket cooldown period (0.25 s / 0.5 s per type, card 115).
2. **Build, host only, one mechanism.** Scale the binding lever with `c111_players` (the card 111 factor, so the
   session count from card 113 applies). With 1 player nothing changes. Follow the existing pattern: scale where the
   value is read or set, log the base and scaled values once per class per map, and refresh on a player count change
   through the card 111/112 path. If two levers bind together, scale both and name them. Keep the bugs' animations and
   attack rules as they are: more bugs attacking at once, not faster or stronger attacks from one bug.
3. **Proof, one run:** the same fight solo and with 2 players (one run with a solo phase before the join is fine,
   or two short runs from the same scenario). Use the card 115 per-window counts: peak attack holders, and attacks
   started per window. Co-op must show clearly more attackers at once than solo.
   - rc 0, ERROR/FATAL 0, no CRASH line, no new dump on either side.
   - The client sees the attacks (card 041 animations).

Stop when the fix builds and that run shows it; the user will playtest.

## Done when

The Report has the candidates with values and which one binds, the lever and how it is scaled, the solo and co-op
per-window counts, the run length, rc, ERROR/FATAL, CRASH lines, dumps and the DLL hash (sha256 prefix,
`sha256sum | cut -c1-16`). The play DLL is saved as `build/dwmapi-play-<hash>.dll`. Ends with "Report complete."

## Report

DLL: `4d058f899b394c59` (sha256 prefix), saved as `build/dwmapi-play-4d058f899b394c59.dll`. Built with no warnings.

### 1. Candidates (measured in c122f/c122g: 2 players, WarriorBug fight on FED_C2M1, `UBWCOOP_C122_LOG=1`)

| Candidate | Offsets and values | Binds? |
|---|---|---|
| Per-attack cooldown | `STAttackComponent` AttackStateMap +0x120, value Cooldown +0x1C. Bugs with every attack on cooldown, per sample: 0.00. | No |
| Global attack delay | `STAttackComponent` GlobalCooldown = 0.050 s, GlobalCooldownTimer +0x3B4. Bugs on the global cooldown, per sample: 0-1. | No |
| Approach or slotting | `BP_ArenaZone_C` ZoneCapacityMax +0x2C4 = 20, with 0 occupiers at the start. Zone fit checks refused: 0-5 per window. Melee ticket holders peak at 1-3. | No |
| Ticket cooldown | 0.25 s / 0.5 s per ticket type (card 115). Card 111 already scales the ticket limit, and it is never reached. | No |
| **Target choice** | `UADTargetComponent` AttackPlayerOverOtherTargetsPercentage +0xC4 = 10 for WarriorBug (5 for NPC troopers). AttackPlayerOverOtherTargetsThreatValue +0xC8 = 50 (troopers 25). TargetScoreDistanceValue = 500, SightRadius = 12500, MaxUpdateTargetTime = 1. | **Yes** |

About half the bugs in the fight target NPC troopers. In c122f the targets split as p0 0-4, p1 4-13, troopers 7-11. Most "ready" bugs (8-13 of about 14) are more than 1200 cm from any player, so the attacker count follows the number of bugs that pick a player.

How the lever works: BeginPlay copies NPCData +0x138 into component +0xC4. `SelectPrimaryTarget` (0x051A5940) rolls `rand()&0x7fff * 0.0030518 < +0xC4`. On a pass, it applies the +0xC8 threat bonus to the player pawn through `ModifyTargetThreat`. c122g shows the bonus landing: the player threat mean climbs from 35 to 98.

### 2. Lever and scaling

`c122_pref_scale` (`mod/src/ubw/bug_scale.cpp`), called from `detour_c103_select` (`mod/src/ubw/enemy_c103.cpp`). The `SelectPrimaryTarget` hook row is now ungated.

- Host only: a client session skips it.
- Before the original runs, +0xC4 is set to `min(base * c111_factor(c111_players), 100)`. After the original returns, `c122_pref_restore` puts the base value back.
- The player count is read on every call, so a player count change applies on the next target selection. No cached value needs a refresh.
- With 1 player the factor is 1.0 and nothing is written.
- Bugs only: owners whose class name holds "Trooper" keep their chance (same test as cards 025 and 122).
- The threat bonus +0xC8 is not scaled. Attack data, cooldowns and animations are untouched.
- Log, once per (class, player count) per map: `card122: BP_AI_WarriorBug_C player preference: AttackPlayerOverOtherTargetsPercentage base 10.0 -> 16.0 (players 2, factor 1.60), threat bonus 50.0 unchanged`.

### 3. Proof (per-window counts, 5 s windows, windows with bugs in the fight)

| Run | Players | Attacking peak | Attacks started (by bugs) | Melee ticket holders peak | Targets p0 / p1 / other |
|---|---|---|---|---|---|
| c122s (solo, `c122-solo.txt`) | 1 | 3, 3 | 12 by 3, 6 by 3 | 2, 2 | 10/0/0, 3/0/7 |
| c122q (co-op, `c122-measure.txt`) | 2 | 4, 4, 4, 4, 4, 5, **10, 9**, 6 | 9 by 5, 13 by 5, 7 by 5, 8 by 5, 7 by 5, 18 by 13, 24 by 12, 28 by 13, 5 by 2 | 1-3 | p1 4-18, other 0-13 |

Co-op has more attackers at once than solo: peak 4-10 against 3, and 5-13 different bugs starting attacks per window against 3. The solo run used DLL `c5ed414e5253474d`. Its solo path is the same as `4d058f899b394c59`, because with 1 player the factor is 1.0 and nothing is written; that only changed the trooper exclusion. An earlier co-op run, c122p on `c5ed414e5253474d`, also scaled troopers from 5 to 8. That is fixed in `4d058f899b394c59`: only WarriorBug is scaled in c122q.

| Run | Length | rc | ERROR/FATAL | CRASH lines | New dumps |
|---|---|---|---|---|---|
| c122s | 210 s | host 0 | 0 | none (only "crash trace installed") | none |
| c122q | 97 s | host 0, client 0 | 0 / 0 | none (only "crash trace installed") | none |

The client sees the attacks. The c122q client log has 69 `card041: client Anim ... (Attack): BP_AI_WarriorBug_C ... played` lines.

Caveat for the playtest: the chance goes from 10 % to only 16 % per target pick with 2 players. Bugs still split between the players and the NPC troopers. The rise in c122q is real but noisy from window to window.

Report complete.
