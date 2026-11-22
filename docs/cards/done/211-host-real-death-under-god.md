# Card 211: The host's real death completes under test god mode

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: queued, static first. Carried from card 196 through card 208.

- c196-r2 and c208-r3: `WARN card043: host real death: 5 hits, Health 1.0` (c208-r3 host log line 7579; hits
  7282 to 7429, causer BP_AI_WarriorBug_C, `Health 1.0 -> 1.0`).
- c208-r2 passed: `Health 1.0 -> -3599.0`, `card043: host real death: 1 hits` with no WARN (host 5383 to 5388).
- In the tree: `g_c43.in_kill` around `c43_kill_host`'s `g_deal_damage`; `c132_god_block` and `c43_death`'s god
  test skip while it is set.

Do: trace every layer that can zero the kill's damage (god block, the downed state's own damage gate, an
Invulnerable tag, the causer chosen by `c43_kill_host`), and say what differed between the two runs. Fix it
without `GOD=0`. Proof rides in the next run that downs both players.

## Senior trace (2026-11-12, static plus the two logs): not the god layers

- `c43_kill_host` (`death.cpp:822`) deals the kill with `c30_nearest_enemy(world, host)` as the causer and falls
  back to the host itself only when that returns null.
- c208-r2 (passed): `real-death hit 1 ... (causer null): Health 1.0 -> -3599.0`; the playlog hit line shows
  damage 4000 with the pawn as its own causer.
- c208-r3 (failed): `real-death hit 1 ... (causer BP_AI_WarriorBug_C): Health 1.0 -> 1.0`; the playlog hit line
  shows `damage 0.00 ... instigator BP_AI_WarriorBug_C`. The host pawn is a bug, the causer is a bug NPC, and the
  game zeroes the hit (same side). c196-r2's WARN fits the same cause. The `in_kill` god tests are not involved.
- So the class is: the kill's causer is on the host pawn's side. On the bug maps every nearest "enemy" found by
  `c30_nearest_enemy` may be a bug.

Fix (one function, `c43_kill_host`): when the hit with a causer leaves Health unchanged, deal the same damage
again at once with the host as its own causer (the `bug == nullptr` form), inside the same `in_kill` window, and
log which form landed. Check what `c30_nearest_enemy` calls an enemy for a bug pawn and say so in the card; do
not change it here. Batch into card 210's build; proof is `card043: host real death: N hits, Health <= 0` with
no WARN in a run where an NPC bug stands near the downed host.

## What c30_nearest_enemy calls an enemy (junior, static)

`c30_nearest_enemy` (death.cpp, near `c30_` helpers) walks the actors of `g_enemy_class`, which is `ASTEnemyBase`,
and keeps those whose class name contains "Bug". It returns the one nearest the target. It does not test the
target's faction or side. So for a bug player pawn on a bug map, it returns allied bug NPCs (`BP_AI_WarriorBug_C`
in c208-r3). The game zeroes that same-side damage, so the hit leaves Health unchanged. The function is not
changed here.

## Fix (junior, in card 210's build, DLL a6a918954db931f2)

`c211_kill_hit` in death.cpp is called from `c43_kill_host` inside the same `g_c43.in_kill` window:

- No causer: the self-caused hit only, `g_deal_damage(host, host, damage, true)`.
- With a causer: the causer's hit first. When Health is unchanged after it, the same damage is dealt again at once
  with the host as its own causer.

One line per hit names which form landed: `card211: host t=... real-death hit N: landed <the causer's hit | the
self-caused hit (no causer found) | the self-caused hit (the causer's hit left Health unchanged) | none ...>`. It
has a WARN prefix only when neither changed Health. Proof rides in c210-r1: `card043: host real death: N hits,
Health <= 0` with no WARN prefix.

## Senior review (2026-11-12): accepted

Runs c210-r1 and c210-r2: `card211: ... landed the self-caused hit (the causer's hit left Health unchanged)`,
`card043: host real death: 1 hits`, no WARN. The causer-lands and no-causer branches ran in earlier runs
(c208-r2: no causer).
