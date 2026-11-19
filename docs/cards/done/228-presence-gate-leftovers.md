# 228 — Presence gate leftovers (from card 227)

Card 227 holds a gated `ASTPlayerPresence` until every living player overlaps it. Four gaps were recorded and not
fixed. Each needs its trace before any code; one goal per run.

1. **Player goes down while the others are inside.** The living count drops, but no new enter edge arrives, so the
   held presence fires only on the next entry by anyone. Wanted: re-check held presences when a player is downed,
   dies or leaves. Start: `c227_release` and the card 43 down state (`c43_is_down`) in `objectives.cpp`.
2. **Mech pilots.** On the host the pilot's roster pawn is not a player character, so the pilot is not waited for;
   on the client `c43_client_host_pawn` may still find the host's player character, so the two copies can disagree.
   Wanted: one rule for a piloting player on both machines (count the mech as the player's body).
3. **Client copy runs its own reactions.** When the client's set completes, its presence runs the door reaction
   locally (one `card161 ... client-only DoorState` WARN in c227c, set back by card 161). Wanted: client holds until
   the host's fire, or the reaction is host-only. Check what else the client copy's reactions do first.
4. **Not gated.** `LR_ActivateGeneric_C` range lanes (`BP_PlayerPresence_C_17` to `_23`), presences with only a
   level-script bind, and `ASTActivationVolume`, `ASTTeleporterVolumeBase`, `ASTEnemyPresence` (not dumped). Decide
   per class from a dump (`UBWCOOP_C65_LOG=1`) on each map which of them shut a player out; gate only those.

Done when: each item is fixed with one targeted run, or closed with the reason it needs no change.

## Step 1 answers (senior, static plus existing logs, 2026-11-13)

Gated presences seen on the Trooper maps so far: FED_C1M1 `BP_PlayerPresence_C_7` (`LR_Door_Toggle_C`, the CTF
door) and FED_C2M1 `BP_PlayerPresence_C_2` (`LR_SpawnAI_C`, at (229051, 41029, 8745)). FED_C3M2, FED_C4M2 and
FED_C5M1 log none. FED_C3M1, FED_C4M1 and FED_C5M2 have no run with the card 227 build yet; their soak runs
settle it (row `card227: ... gated (LTOnEnter ...)`).

1. Down while the others are inside: confirmed in code. `c227_release` (`objectives.cpp:2521`) runs only from
   `detour_c227_entered`; `c227_count` drops a down pawn from `living`, but nothing calls the gate again. The
   table (`C227Gated`, 2313) keeps the presence's name only, so a re-check has no presence to ask.
2. Mech pilots: confirmed in code. `c227_count` (2496) skips every roster pawn that is not a player character,
   so on the host a piloting player is not waited for. Whether the host's copy of a client-piloted mech can
   overlap a presence at all is the open prediction of card 258 (capsule collision 0 on the host's copy); the
   card 075 `piloted mech` row of FED_C2M1 run B decides it. The rule for pilots is written after that row.
3. Client's own reactions: `logs/c240-c1m1-r8`: host fired `_7` at 166899064, the client's copy fired at
   166899065 and its `LR_Door_Toggle_C` moved `BP_Door_Sliding_Large_C_0` 1 to 3 at 166899068; card 161 set it
   back and the host's DoorOp followed (3 set-backs, 7 DoorOps applied, 0 failed). No wrong end state. This is
   the class of cards 254 and 258 (the client runs level-script chains on its own events); it gets no fix of its
   own here and closes with that class.
4. Not gated classes: needs the `UBWCOOP_C65_LOG=1` dump per map; taken from the soak runs, no run of its own.

## Fix spec, item 1 (build 4 or later; one run on FED_C1M1)

- `objectives.cpp`: `C227Gated` gains the presence's object index (weak reference, resolved and checked with
  `is_a` at use). New `c227_recheck(world)`: for each entry with `holds > 0` and not fired (bound
  `kC227GatedMax`), run `c227_count(presence, nullptr)`; when `living > 0` and `inside == living`, call the
  original `OnTriggerEntered` with one living inside pawn and mark fired. Log `card227: <side> presence <name>
  fired by re-check: N of N players inside (H held)`.
- Called from `c43_step` on both sides at most every 250 ms, and only while some entry is held. One poll covers
  down, death, a player leaving and (after item 2) boarding; no call site per cause.
- Do not touch `c227_release`, the exit detour or the gate classes.
- Attack before coding: the original's `self` argument (what the game passes at 0x50F6880's caller) must be
  named from the disassembly, not guessed; a presence unloaded with its level must fail the weak reference.
- Proof: FED_C1M1 CTF door: host inside `_7`, client outside, client downed by the scenario. Predicted rows:
  `held: 1 of 2`, then `fired by re-check: 1 of 1 players inside` on both sides within 300 ms of the down row,
  door open on both, 0 `client-only DoorState` rows beyond the known three.

## Item 2 answers and fix spec (senior, 2026-11-13, after FED_C2M1 run B)

- Run B row, both sides: `card075: <side> piloted mech BP_BaseMech_C ... capsule collision 3, IsPlayer 1`. The
  host's copy of a client-piloted mech keeps its collision and counts as a player for the game, so it can overlap
  a presence. Card 258's "copy cannot overlap" prediction is broken; the mech is a body like any other.
- Fault stays as traced: `c227_count` (`objectives.cpp`) drops every roster pawn that is not a player character,
  so a pilot is not waited for on the host; on the client `c43_client_host_pawn` returns the host's soldier, not
  the mech the host sits in.
- Rule: a piloting player's body is the mech. Spec, `objectives.cpp` only:
  - New `c227_body(pawn)`: a mech (`g_mech_class`) is its own body; a soldier listed in `g_c75_pilots` as the
    soldier of a mech is replaced by that mech; any other player character is itself; else null.
  - `c227_count` uses `c227_body` for each roster pawn and for the client's host pawn, then the existing down
    check and overlap test on the body.
  - Do not touch `c43_client_host_pawn`, the roster or card 075.
- Attack before coding: `g_c75_pilots` must be filled on both machines for both players (EnterMech detour); a
  slot whose mech is destroyed must not be used (check the object is live).
- Not settled statically: whether the game calls `OnTriggerEntered` with the mech as the actor. Prediction for
  the next FED_C2M1 run with a gated presence and a pilot: `card227: ... held: N of 2 players inside (entered by
  BP_BaseMech_C...)`. If no such row, the presence ignores mechs in the unmodded game too and the pilot must not
  be counted in `living`; report it.
- Trooper maps with both a mech and a gated presence: FED_C2M1 only (`BP_PlayerPresence_C_2`). Build 5 or later;
  proof rides in a FED_C2M1 run, no run of its own.

## Item 2 patch written (senior, 2026-11-17)

Scratchpad `c228_patch.py` (`--check` ok, 3 edits, `objectives.cpp` only). Simpler than the spec above: no
`c227_body` and no pilot table. `g_c75_pilots` slots are never cleared at a leave (`c75_remember_pilot` only), so a
soldier-to-mech lookup would count a player who left the mech as the mech. The roster pawn is the controller's pawn,
which is the mech while it pilots, so `c227_count` now accepts a pawn of `g_mech_class` as a body. A mech has no
card 043 entry, so it is always living.

Attacked: `g_mech_class` null (guarded); `first_inside` can now be a mech and is passed to the re-run of
`OnTriggerEntered` (objectives.cpp:3440) and `c288_presence_fired`, the same actor the game would pass if it calls the
entry with a mech (still not settled, see above). Not covered: the client's count of another player's mech
(`c43_client_other_pawns` lists soldiers only); the client's row may read one player short while the host pilots.

Proof rides in the next FED_C2M1 run with a pilot at `BP_PlayerPresence_C_2`: host row
`card227: ... held: 1 of 2 players inside` while the pilot is outside, then fired with `2 of 2`; today the host
row reads `1 of 1` with a pilot in the world.

## Item 2 closed without a run (senior, 2026-11-17)

Patch is in b173 `dedb14d000d6ec36`. No run can prove it on a Trooper map: on FED_C2M1 `BP_PlayerPresence_C_2` is
the LOS_O01_SO01 gate at the map start and is spent before the mech spawns at LOS_O05 (junior, b7b-2); FED_C4M1
logs no gated presence. So a pilot cannot stand at a live gate in play, and the old fault could not show either.
The patch stays as hardening. Watch: a `card227 ... held` row naming `BP_BaseMech_C` in a playtest log is the first
real sample; a gate that never fires with a pilot in the world reopens this.

## Closed (2026-11-17 13:09)

The patches are in since b173 `dedb14d000d6ec36`. The last unproven item (a mech pilot at a live gate) cannot occur
on a Trooper map: on FED_C2M1 the only gated presence is spent before the mech spawns, and FED_C4M1 logs no gated
presence. The patch stays as hardening.

Reopen when: a playtest log has a `card227 ... held` row naming `BP_BaseMech_C` (the first real sample) and the gate
then does not fire with every player inside, or any gate never fires with every living player past it.
