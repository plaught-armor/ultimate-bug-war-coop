# Card 103: Enemies perceive, target and attack both players

Status: written 2026-11-05 (senior); sent 2026-11-06; accepted 2026-11-06.

Read:

- card 099, "The abstraction: "the player" is player index 0" (the `GetPlayerController` gate);
- card 102, Out of scope (the enemy functions this card owns);
- `docs/STATE.md`, the sync rule (enemies are the host's; card 098 enemy table).

Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`. Play DLL base: `00a35c0c5d36d47d` (card 105).

## Why

User rule (2026-11-05): the enemies must be the same for every player, which includes whom they chase and attack. The
game is single-player code: enemy code that means "the human" asks for "the player", which is
`GetPlayerController(ctx, 0)` (card 099). The context there is the enemy, so card 099's gate leaves it on the host's
pawn. So on the host, bugs most likely perceive, chase and attack only the host, and ignore the client's server copy
unless it is in their threat list for another reason.

Functions that mean "the player" (from the symbols; confirm in step 1 which call index 0):

- perception: `ASTEnemyBase::CanPerceiveTargets` (0x05180480), `ASTNonPlayableCharacterBase::CanPerceiveTargets`
  (0x051805D0), `ASTEnemyBase::HasLineOfSightToPlayer` (0x05193F30);
- attack choice: `ASTEnemyBase::CanPerformAttack` (0x051806D0), `GetTicketPriorityScore` (0x05191D40),
  `USTAttackActionBase::GetDistanceToPlayer` (0x051597D0);
- target list: `UADTargetComponent::AddTarget` (0x0517CC00), `FindTargetData` (0x05189F50), `ModifyTargetThreat`
  (0x0519A190), `SelectPrimaryTarget` (0x051A5940, gives "the player" a threat change), `SetPrimaryTarget`;
- `ASTAIAwarenessSystem::CalculateThreatLevel` (0x050DFF80), `ASTNonPlayableCharacterBase::FacePlayer` (0x051880E0).

## Do

1. **Measure (one run, log only).** The host stands back, out of sight, and the client walks up to a group of bugs and
   fights them. On the host, log per enemy and per second: its primary target (host pawn, server copy, other), the
   threat list (actor and threat), each call of the functions above with its result, and each index-0
   `GetPlayerController` call with an enemy context (caller, counted). Also log the enemy's attacks and whom they hit.
   Then the host walks in, so the run shows both players near the same bugs.
   Card 098's enemy comparison was invalid (the client's `warp` is undone by the server, so it never left the LZ).
   Redo it here with the client at the fight: every enemy near it exists on both sides, with matching position,
   animation, health and death. Move the client by walking, or have the host warp the server copy. Find a working
   health field first (card 098 read `CurrentHealth` as 0.0 on both sides).
2. **Fix: "the player" for an enemy is its chosen target.** Prefer one gate over a patch per function:
   - make sure both pawns are in every enemy's target list (`AddTarget` for the server copy wherever the host pawn is
     added);
   - make `SelectPrimaryTarget` treat both pawns as players (the threat change it gives "the player" goes to either,
     by its own rules: distance, damage taken, line of sight);
   - for the index-0 calls with an enemy context, extend the card 099 gate: return the controller of the enemy's
     current primary target when that target is a server copy, otherwise the host's.
   Use the measure to pick; say why in the Report. The client sees the result through the existing enemy sync (cards
   023, 041, 098); nothing runs on the client.
3. **Check** that enemy hits on the server copy damage the client (card 037 health, card 038 death), and that an enemy
   switching target does not jitter (switching every frame between two near players).
4. **Proof**, one scenario run (FAST=1): with the host out of sight, bugs chase and attack the client and the client
   takes damage; with both near, bugs split or switch by threat. rc 0, ERROR/FATAL 0, no new dump.

Stop when the fix builds and that one run shows it; the user will playtest.

## Done when

- The Report has the measured table (function, enemy class, result for host pawn and server copy, count), the gate
  chosen and why, the proof run and the DLL hash.
- The Report ends with "Report complete."

## Report
Written 2026-11-06 (junior). DLL `533e5cd2cd04a851` (copy: `build/dwmapi-c103-533e5cd2cd04a851.dll`).

Runs, both on FED_C2M1 with FAST=1 and `UBWCOOP_AUTODROPSHIP=1`:

- c103a: the measure, `tools/scenarios/c103-measure.txt`, DLL `4c974c571c74292f`. The host warps the server copy
  next to a WarriorBug group, the client looks at the bugs and fires, then the host warps in and fires too.
- c103p: the proof, `tools/scenarios/c103-proof.txt` (the same route), with the fix DLL.

Both runs ended with rc 0 on both sides, ERROR/FATAL 0 and no new dump.

### Measured (c103a, before the fix)

| Function | Enemy class | Host pawn | Server copy | Count |
|---|---|---|---|---|
| `SelectPrimaryTarget` threat list (`FindTargetData`, threat at +0x50) | `BP_AI_WarriorBug_C` | listed at 35.0 once the host is near; never above 35 | listed at 35.0 from the client's arrival | one row per enemy every 2 s |
| `SetPrimaryTarget` (caller exe+0x51A5A53, inside `SelectPrimaryTarget`) | `BP_AI_WarriorBug_C` | primary at times | primary from the client's arrival | about one switch per 1.01 s (bug 79BFBBC0: 35 switches) |
| `SetPrimaryTarget` | `BP_AI_Trooper_Morita_C` | n/a (troopers target bugs) | n/a | 8 to 15 switches each |
| `GetTicketPriorityScore`: index 0 via `GetSTPlayerCharacter` (caller exe+0x523E0FD) | `FSM_Enemy_Aware_C` | always the host pawn | never | per tick |
| index 0 via `GetSTPlayerController` (exe+0x523E19D) | `ADTargetComponent` | always the host's controller | never | per tick |
| index 0 then `GetPlayerViewPoint` (exe+0x517EFAC) | `BP_AI_WarriorBug_C` | always the host's view | never | per tick |
| index 0, `PlayerCameraManager` at PC+0x358 (exe+0x523DF5D) | `GoreComponent` | host | never | per tick |
| index 0 (exe+0x52023FD) | `STSpriteRenderComponent` | host | never | already excluded by card 102 |
| Attack sequences (card 041 `seq Attack`) | `BP_AI_WarriorBug_C` | - | - | 3 in the whole run |
| Player damage (card 028 health, card 037) | - | 100.0 | 100.0 | 0 hits |

The per-caller counts are missing: the counts are dumped at a map change, and both runs quit before one.

Conclusions:

- The server copy is in every nearby bug's target list and is often its primary target. Target selection was not the
  gap.
- The gap is attacks. `CanPerformAttack` (0x051806D0) needs an attack ticket. `GetTicketPriorityScore` (0x05191D40)
  scores a bug by its distance to `GetSTPlayerCharacter`, which is player index 0, the host pawn. A bug fighting the
  client far from the host scores low, rarely gets a ticket, and does not attack. In c103a a WarriorBug stood on the
  client (screenshot c103-client-late) and the client stayed at 100 health.
- The threat bonus that `SelectPrimaryTarget` gives "the player" never applied: the host pawn stayed at 35.0.
- The switch about every second is the engine's own rotation between targets. Troopers do the same among bugs. It is
  not a mod artifact and not per-frame jitter.

**Health field.** The working field is the card 037 attribute set: `UADAttributeSetHealth.Health` at +0x30 (current
value). Card 028 prints it, and card 043 reads it for the downed state. Card 098's +0xAC0 read 0 and is not a health
field.

**Enemy comparison (card 098 redo).** The host warped the server copy, so the client really was at the fight. Deaths
match: the host's card 038 `HandleDeathEvent` calls (1 to 7, bugs and troopers) and the client's death edges fall at
the same times. Attack sequences reach the client (card 041 "sent to 1 PCs"). Positions and animations come through
the card 023/041/098 sync as before, and nothing in the screenshots was out of place.

### Gate chosen

The gate is option 3, extending the card 099 gate to enemy contexts. Options 1 and 2 were not needed: both pawns are
already listed, and the threat bonus follows "the player" anyway.

On the host, an index-0 `GetPlayerController` call whose context belongs to an enemy returns the controller of that
enemy's player:

- The enemy is found by `c103_enemy_of`: the context, its outer chain or its Owner chain, at most 6 hops.
- The enemy's player is its primary target (recorded by the `SetPrimaryTarget` hook, now always installed on the host)
  when that is a roster player pawn. Otherwise it is the nearest roster player pawn.
- Slot 0 (the host) returns null, so the original result stands. The client and calls off the game thread are never
  changed.
- `0x523DF5D` (the camera manager for gore and camera effects) is added to card 102's exclusion list, so effects stay
  on the host's own camera.

Why this gate: one hook covers every "the player" call an enemy makes. That includes ticket priority, perception, line
of sight, `FacePlayer`, `GetDistanceToPlayer`, `CalculateThreatLevel` and `SelectPrimaryTarget`'s player bonus, which
now goes to the enemy's own player. A per-function patch would need one hook for each of those.

Code:

- `enemy_c103.cpp`: `c103_enemy_pc`, the per-enemy primary ring (`C103Picks`, 96 rows, reset per map) and
  `c103_player_slot`.
- `player_c99.cpp`: `detour_c99_get_pc` calls `c103_enemy_pc` when no server copy context matched. Remaps are counted
  under the new entry "enemy".
- `hooks_ubw.cpp`: the `SetPrimaryTarget` row is no longer gated on `UBWCOOP_C103_LOG`.

### Proof run c103p

Host 4689 lines, client 3144 lines.

| Check | Result |
|---|---|
| Enemy "the player" calls go to the client's controller | shown: card 099 `player 0 remapped (enemy)` from exe+0x523E0FD (`FSM_Enemy_Aware_C`), exe+0x523E19D (`TargetComponent`) and exe+0x517EFAC (`BP_AI_WarriorBug_C`) |
| Bugs attack the client | shown: 48 Attack sequences (3 in c103a) |
| The client takes damage | shown: server copy health 100.0, 95.8, 88.2, 75.0, 50.0, 25.0 with a Flinch at each hit. Then card 043: server copy (slot 1) Downed, and the client log shows `own pawn Up -> Downed, 30 s bleedout`. A bug hit it again while it was down (25.0 damage, bleedout -2.5 s). |
| The player bonus applies | shown: the server copy's threat reaches 285.0 and 475.0 where the host pawn stays at 35.0 |
| With both near, bugs split or switch | shown: `SetPrimaryTarget` changes to the server copy 104 times and to the host pawn 5 times. In the last threat rows, three bugs hold the server copy as primary and one holds the host pawn. |
| No per-frame jitter | shown: the most switches for one bug is 44 over about 100 s, the same engine rotation as c103a |
| rc 0, ERROR/FATAL 0, no new dump | shown |

Known gaps:

- `c103_player_slot` does not skip a downed or dead player when it falls back to the nearest pawn, and a downed pawn
  that is still a bug's primary target keeps it. In c103p a bug hit the downed client. Whether bugs should leave a
  downed player is a game rule question for the playtest.
- The card 099 and 103 per-caller counts were not dumped, because there was no map change before quit.

**Pickup drops (card 105 note).** Neither run logged a card 104 'P' row or a card 105 blocked spawn during the fight.
No bug or level script dropped a pickup through the Blueprint thunk, so nothing in these fights depends on the 'P'
relay. Whether bug drops reach the client is not shown, because none happened.

The fix builds, and the run shows bugs attacking and downing the client. The user playtests from here.

Report complete.
