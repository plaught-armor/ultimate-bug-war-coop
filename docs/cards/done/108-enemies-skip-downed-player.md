# Card 108: Bugs pick a standing player over a downed one

Status: written 2026-11-06 (senior); sent 2026-11-06; redirect added at review; accepted 2026-11-06.

Read:

- card 103 Report (`c103_enemy_pc`, `c103_player_slot`, the primary-target ring);
- card 106 Report, the Downed row (bugs hit the downed client, 25 damage x3, bleedout -2.5 s each).

Repo root: `/path/to/ultimate-bug-war-coop`. DLL base: `bbee121209725fa4` (card 107, play build).

## Why

User decision (2026-11-06): when a bug falls back to the nearest player, it skips a downed player if another player
is standing. Today `c103_player_slot` (`mod/src/ubw/enemy_c103.cpp`) takes the primary target if it is a player pawn,
else the nearest player pawn, with no Downed check. Downed and Out are the mod's own states (card 043, `g_c43`), so
the game's AI does not know about them either.

## Do

1. **In `c103_player_slot`**, a player slot whose pawn is Downed or Out (`c43_find_pawn` or `c43_find(id)`, then
   `c43_down_state`, as `c43_up_count` in `death.cpp` does) is skipped while at least one other player is standing:
   - the nearest-player fallback picks the nearest standing player;
   - a primary target that is downed also falls through to the nearest standing player;
   - when every player is down, the current behavior stays (primary target, else nearest).
   Slot 0 still means "the original result stands", as in card 103.
2. **One short FED_C2M1 run** (c106-fight route, `UBWCOOP_C79_DOWN=2` or the existing downed step): the client goes
   Downed with the host standing near the fight. Log one line per change of an enemy's chosen slot (capped), and show
   that after the client goes Downed the bugs' slot moves to the host (0), and the host takes damage while the client's
   bleedout stops dropping from hits. rc 0, ERROR/FATAL 0, no new dump.

Stop when the fix builds and that run shows it; the user will playtest.

## Done when

The Report has the change, the run row (slot change after Downed, damage lines on both), the run length, rc,
ERROR/FATAL, dumps and the DLL hash. Ends with "Report complete."

## Report

DLL `426048623155d684` (`build/dwmapi-c108-426048623155d684.dll`).

### Change

`mod/src/ubw/enemy_c103.cpp`:

- New `c108_down(player)`: `c43_find(id)`, then `c43_down_state`, as in `c43_up_count`.
- New `c108_any_up(roster)`.
- `c103_player_slot`, while any player stands:
  - a Downed or Out primary target falls through to the nearest search;
  - the nearest search skips Downed and Out players.
- When everyone is down, both rules take any player, as before. Slot 0 still keeps the original result.
- Log (`UBWCOOP_C103_LOG`): `C103Pick.chosen` holds the last chosen slot, and `c108_note_slot` writes one line per
  change. Format: `card108: enemy <class> <ptr> slot A -> B (slot 0 up|down, slot 1 up|down)`, under the card 103
  line cap (196 lines in the run).

Scenario `tools/scenarios/c108-fight.txt`: the c106-fight route, plus `host warp actor BP_AI_WarriorBug_C 400` and
`sleep 15` after the Downed wait.

### Run row

| Run | Map | Length | Host rc | Client rc | ERROR/FATAL | WARN | Dumps | Drive |
|---|---|---|---|---|---|---|---|---|
| c108a | C2M1 | 175 s | 0 | 0 | 0 / 0 | 0 / 0 | none | rc 1: `client wait log own pawn Up -> Downed` timed out (see below) |

The run showed the mirror case: the **host** went Downed first, at 45776422. The client then went straight Up -> Out
at 45781010, because everyone was down.

- **Slot change after Downed: pass.**
  - 45776423-45777180: every listed enemy's slot went 0 -> 1, the standing client (`slot 0 down, slot 1 up`). That
    was 6 WarriorBugs and 4 Trooper_Morita.
  - 45781006 onward, everyone down: the slots go back to the current behavior. They flip 1 -> 0 and 0 -> 1 by
    primary target or nearest.
- **Damage: fail. The downed host kept taking hits.**
  - The host took 10 hits while Downed: `card043: host … took 25.0 damage while Downed: bleedout -2.5 s`, at
    45777312-45780614. That drained its 30 s bleedout in 4.6 s.
  - The game's own `SetPrimaryTarget` (caller exe+0x51A5A53) put the downed host pawn back as primary. For example,
    `card103: primary BP_AI_WarriorBug_C 00000000A0176670 -> host pawn` came at 45777005, 580 ms after Downed.
  - The slot gate changes only the index-0 `GetPlayerController` result (perception and context). It does not change
    whom the bug's target component picks, so melee still goes to the primary target.
- Client damage while Up: card 040's trace has no single damage line for the server copy, so it is not counted here.
  The client went Up -> Out because the host was already down (`Down received 16`).

### Open

- Making bugs stop hitting a downed player needs the target component as well. Two options, either in
  `detour_c103_set` or before `SelectPrimaryTarget`:
  - refuse or redirect `SetPrimaryTarget` to a Downed or Out player pawn while another player stands;
  - zero that pawn's threat (FADTargetData +0x50).
  Not done: outside the card's change.
- No run with the client Downed and the host standing. The mirror case ran instead (host Downed, client standing).

Report complete.

## Senior review (2026-11-06): not accepted yet, finish in this card

The slot gate works, and the mirror case (host Downed, client standing) is a valid proof. The card's goal is that
bugs stop going for a downed player, and they still do, so the target fix belongs here.

Do:

1. **Redirect in `detour_c103_set`** (host only), before calling the original: when `actor` is a player pawn that is
   Downed or Out (`c108_down`) and another player stands (`c108_any_up`), pass the nearest standing player pawn to
   the enemy instead. When nobody stands, pass `actor` unchanged. The caller is
   `UADTargetComponent::SelectPrimaryTarget` (0x051A5940, return exe+0x51A5A53), so every re-selection goes through
   the same redirect; no threat edits. If the redirect alone does not stop the hits (for example an attack already
   committed keeps its target), log which attack ability or task still hits the downed pawn and stop there.
2. **One short C2M1 run** (c108-fight, either side Downed): after the Downed line, the `card103: primary` lines show
   the standing player, and the `took 25.0 damage while Downed` lines stop within about 1 s (an attack already in
   flight may land). rc 0, ERROR/FATAL 0, no new dump.

Append the result under this section and end with "Report complete." again.

### Result (2026-11-06)

DLL `a4fdd1e0a051c984` (`build/dwmapi-c108-a4fdd1e0a051c984.dll`).

Change in `mod/src/ubw/enemy_c103.cpp`:

- New `c108_target(component, actor)`. When `actor` is a roster player pawn that is Downed or Out and another player
  stands, it returns the pawn of `c103_player_slot` (the nearest standing player). Otherwise it returns `actor`
  unchanged.
- `detour_c103_set` (host only) passes that result to the original `SetPrimaryTarget`.
- Log line, under the card 103 cap: `card108: redirect <class> <ptr> primary slot A (down) -> slot B`.

Scenario `tools/scenarios/c108-fight.txt`: it now waits on the host log for either slot,
`host wait log "-> Downed .HandleDeathEvent"`, then `sleep 15`. The host warp after Downed is removed.

| Run | Map | Length | Host rc | Client rc | ERROR/FATAL | WARN | Dumps | Drive |
|---|---|---|---|---|---|---|---|---|
| c108b | C2M1 | 88 s | 0 | 0 | 0 / 0 | 1 / 0 | none | rc 0, scenario passed |

The host went Downed first, at 46139709. The client stood.

- **Primary lines show the standing player: pass.**
  - 46139735-46140408: 36 `card108: redirect … slot 0 (down) -> slot 1` lines.
  - Every bug's `card103: primary` went to the server copy, through both callers, exe+0x51A5A53
    (SelectPrimaryTarget) and exe+0x5199FF1.
  - Until the client went down, no bug's primary was the downed host pawn. The Trooper_Morita primaries are bugs.
- **Damage while Downed stops: pass.**
  - The run has 0 `took … damage while Downed` lines. c108a had 10.
  - The host's bleedout counted down 1 s per second, 30 s to 23 s, with no hit drops, over 46139709-46146706.
- **The standing player takes the hits.** The client (server copy) went Out at 46147086, 7.4 s after the host went
  Downed. Then `all down: nobody Up` made the host Dead.
- **Everyone down keeps the current behavior.** After 46147089, the WarriorBugs' primaries go back to the host pawn.
  The WARN is `card043: host real death: 5 hits`, at 46152109, which follows from the all-down death.

Report complete.
