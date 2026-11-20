# Card 115: Find what caps how many bugs attack at once, and scale it with the players

Status: written 2026-11-06 (senior); sent and accepted 2026-11-06 (no cap binds; AI pacing sets attackers at once; no scaling built).

Read:

- card 111 Report (ticket limits are 100 per type and never bind: peak 3 Melee holders and 4 points, at both
  limit 100 and 160);
- card 103 Report (`GetTicketPriorityScore` 0x05191D40, the ticket manager, the enemy's target player);
- `mod/src/ubw/bug_scale.cpp` (`kC111Scale`, `c111_players`, the `RequestTicket` peak log).

Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`. DLL base: `17a60609748a4770` (card 114).

## Why

User decision (2026-11-06): more players means more bugs, and more bugs attacking at once. Card 111 shows that the
second part comes only from the bigger waves today. The ticket limit is not what holds attackers back. Something
else keeps the peak at about 3 Melee attackers.

Candidates:

- **The active ticket set.** `GetActiveTicketSet` 0x050E8DF0 (`FSTTicketSet`) and `SetTicketAllocationOverride`
  0x051035B0 (`USTTicketAllocations` subclass). `BP_Tickets_Default` holds four identical blocks of floats (0.25,
  0.25, 0.5, 0.25), likely one per ticket type. These may be costs, cooldowns or per-target caps.
- **The `RequestTicket` arguments.** `RequestTicket(ASTEnemyBase*, ESTTicketType, int cost, bool)` 0x050FF6D0. The
  cost and the bool may limit attackers per target.
- **Before the request.** A per-target attacker count or slot system in the bug AI (behavior tree, EQS, an attack
  slot component), so most bugs never ask for a ticket.
- **Plain AI pacing:** cooldowns between attacks, and how many bugs reach melee range.

## Do

1. **Measure (static first, then one log run on FED_C2M1, both players, a big fight).**
   - Every `RequestTicket` call: enemy, type, cost, bool, granted or refused.
   - Read `FSTTicketSet` and `BP_Tickets_Default` by reflection: field names and values.
   - How many bugs are in combat (aggroed on a player) next to how many hold or ask for a ticket.
   - Name the cap that binds, with the numbers. If no single cap binds (AI pacing), say so, show it, and stop
     after step 1.
2. **Build (host only).** Scale the binding cap with the card 111 factor (`kC111Scale`, the session count from
   `c111_players`). Use the same rules as the limits: ceil, at least the original + 1 for 2+ players, re-applied on
   a count change. The cap's base comes from the game (and from the card 114 file, if the cap is in it).
3. **Proof, the same kind of fight, both players:** the peak attackers at once, solo factor next to the 2-player
   factor (a test-only override such as `UBWCOOP_C115_FORCE=1` is fine for the solo half, in the same run). rc 0,
   ERROR/FATAL 0, no new dump.

Stop when the fix builds and that run shows it; the user will playtest.

## Done when

The Report has the binding cap (what it is, where it lives, its values), the request and grant numbers, the scaling
applied, the peak attackers at each factor, the run length, rc, ERROR/FATAL, dumps and the DLL hash. Ends with
"Report complete."

## Report

**Result: no single cap binds. The number of bugs attacking at once is set by AI pacing, not by the ticket limit. I stopped after step 1 and built no scaling.**

### Static

- `ASTEnemyBase::CanPerformAttack` (0x051806D0; args enemy, ESTTicketType, dummy, cost) gates every attack. With cost -1 it takes the cost from the enemy's type-to-cost map (+0xAF0). It calls `ASTTicketManager::RequestTicket` (0x050FF6D0), or `DummyRequestTicket` when dummy is set.
- A refused ticket still lets the attack run in two cases: the enemy ignores tickets after damage (+0xF24, likely `bIgnoreTicketsDueToDamage`), or the attack data's bypass flag (+0x218) is set and the target is within its range (+0x21C).
- `RequestTicket` returns true when the enemy already holds a ticket. Otherwise it grants while `used(+0x8C) + cost <= limit(+0x90)`, and with steal set it tries `TryStealTicket`. That limit is the only cap in the ticket path.
- The limit comes from the active `FSTTicketSet`, which `RefreshTicketLimits` / `CreateTicketContainer` pass in. Card 111 already scales it.

### Reflection (host log, run c115a)

- `TicketAllocations_Default` = `Default__BP_Tickets_Default_C`. `TicketAllocations_Override` and `TicketAllocations_OverrideSubclass` are none.
- The Easy, Medium, Hard and Extreme sets are identical, and the active set (`GetActiveTicketSet`) matches them:

  | Type | Tickets | Cooldown period |
  |---|---|---|
  | Melee | 100 | 0.25 s |
  | WeakProjectile | 100 | 0.25 s |
  | DeadlyProjectile | 100 | 0.5 s |
  | AreaAttack | 100 | 0.25 s |

- Container `STTicketManagerContainer`: `ActiveTickets` TMap at +0x28, `CooldownTickets` TArray at +0x78.
- With 2 players all four limits were 160 (card 111 factor 1.6).

### Measurement (FED_C2M1, FAST=1, both players warped into the first WarriorBug group, 9 windows of 5 s)

| Window | Bugs making attack checks | Melee checks | Melee requests (all cost 1) | Granted | Refused | Peak Melee holders | Peak used / limit |
|---|---|---|---|---|---|---|---|
| 1 | 15 | 387 | 9 | 9 | 0 | 3 | 5 / 160 |
| 2 | 15 | 250 | 7 | 7 | 0 | 3 | 3 / 160 |
| 3 | 18 | 509 | 6 | 6 | 0 | 3 | 4 / 160 |
| 4 | 19 | 412 | 11 | 11 | 0 | 4 | 5 / 160 |
| 5 | 17 | 170 | 12 | 12 | 0 | 4 | 6 / 160 |
| 6 | 14 | 136 | 7 | 7 | 0 | 2 | 4 / 160 |
| 7 | 15 | 257 | 10 | 10 | 0 | 4 | 5 / 160 |
| 8 | 6 | 65 | 12 | 12 | 0 | 4 | 6 / 160 |
| 9 | 5 | 145 | 15 | 15 | 0 | 5 | 6 / 160 |

- **Totals over all ticket types:** 2928 attack checks, 89 requests, 89 granted, **0 refused**, 0 steals.
- **Peak holders:** 5 enemies held a Melee ticket at once; no WeakProjectile, DeadlyProjectile or AreaAttack ticket was held at all.
- **Peak points in use:** 6 of 160. With 1 player (limit 100) that is still far below the limit.
- **"ok on refused" was 0 in every window.** The damage-ignore and range bypass paths never rescued a refused ticket, because no ticket was refused.
- **Most passing checks issued no request** (for example, 376 of 383 in window 1). These are probably dummy evaluations through `DummyRequestTicket`, which is not hooked, so I did not split them out. Either way, none of them was refused.
- **Gap:** 14-19 bugs made attack checks per window while at most 5 held a ticket. The gap comes from the bugs' own pacing: how many reach attack range, and each attack's own cooldown. The ticket gate never stops them.
- **Caveat:** the `GetTicketPriorityScore` hook saw no calls (aware 0), so it did not count the bugs in the fight. The distinct enemies making attack checks stand in for that count.

### Scaling applied

None new. Card 111 already scales the ticket limits (100 to 160 with 2 players). The limit is never reached solo or in co-op, so raising it changes nothing. More attackers at once would mean changing AI behaviour, for example per-attack `CooldownTime` (`FSTEnemyAttackDataConfig`) or approach and slotting logic. The card does not ask for that.

### Run

- **c115a:** 94 s, host rc 0, client rc 0. FATAL/ERROR 0 on both sides. No new crash dumps. Scenario `tools/scenarios/c115-measure.txt`.
- **DLL:** `bc51f93993e70b6d` is the measurement build. It is the card 114 base plus `mod/src/ubw/attack_cap.cpp`: log only, host only, at most 400 lines. It hooks `CanPerformAttack`, `GetTicketPriorityScore`, and the `RequestTicket` detour, and calls `GetActiveTicketSet`. `c111_container` and `c111_players` are now exported from `bug_scale.cpp`.
- **Play DLL:** stays `17a60609748a4770` unless you want the logging kept.

Report complete.
