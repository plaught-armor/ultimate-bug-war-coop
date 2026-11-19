# 301 Audit every objective for co-op: no step may assume one player

User rule 2026-11-13 (during the playtest, after card 300): "need to research all objectives and make sure they
have coop in mind".

The level scripts were written for one player. Cards 288 (door waits for all players), 299 (hole open state) and
300 (tunnel closes on a player) are single finds of one class. This card finds the rest statically, before a
player does.

## Scope

Every objective and sub-objective of every Trooper (FED) map: C1M1, C2M1, C3M2, C4M1, C4M2, C5M1, C5M2 (bug
campaign later, per the Trooper-first rule). Source: each map's level script (`FED_*_Scripting_C`), the
objective actors behind each `ActorReference`, and the trigger volumes, doors, sequences and spawners they call.
Static only: no run, no build.

## Per objective, answer these (one table row each, in `docs/reference/objective-coop-audit.md`, one section per map)

| Question | Single-player assumption to look for |
|---|---|
| Start and complete condition: which pawn does it test | `GetPlayerPawn(0)`, `GetPlayerCharacter(0)`, first overlap only, the host's controller |
| Does it close, block, collapse or unload anything behind the player | closes with a player on the wrong side (card 300) |
| Does it open something | open state reaches every player as one state, picture and collision (card 299) |
| Does it move the player: teleport, cinematic, dropship, lift, forced possession | moves one pawn, leaves the other |
| Does it need every player in one place: level end, extraction, door entry | waits for all (card 288) or carries all |
| Counted or timed parts: kills, holds, defend timers, pickups | counted once, on the host, for any player's action (card 253, card 289) |
| Per-player state it grants or removes: weapon, ammo, mech, ability, input lock, HUD marker | given to every player |
| Fail and restart: death of the player, mission fail, checkpoint | all players dead, not player 0 dead (card 294); checkpoint placement for every player |
| Spawns aimed at the player, AI targets, kill volumes, out-of-bounds | aimed at or armed around player 0 only |
| Downed, respawning or late-joining player during the step | step still completes; player placed at the live step |

Each row ends with one verdict: `ok` (with the reason), `covered by card N`, or `gap`.

## Output

- `docs/reference/objective-coop-audit.md`: index at the top (map, objectives total, gaps), one table per map.
- Gaps grouped by class, not by objective: one fix card per class with the mechanism that owns it (the shared
  all-players test of cards 288 and 300, world rows of card 098, objective rows). No per-objective patches.
- Each gap class gets a row in the STATE queue; the gameplay test checklist in STATE gains "client stays behind
  at each closing step" and "client acts first at each counted step".

## Order

After step 1 of cards 299 and 300 (they name two of the actor classes). Start with FED_C2M1, the map in play.
Report with the 286-300 report if done by then, else as its own report.

## Done when

Every objective of the seven maps has a row and a verdict; every `gap` has a card; counts per map stated as N of
M audited.

## Senior note 2026-11-14

Class F is carded as 310 (cause settled: `bReplicates 0` on the three components in b11 soak run 1; playtest logs read). Class G waits for the class A all-players test.
