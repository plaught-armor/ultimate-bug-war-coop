# 258 One `STLevelObjective` more on the client for two digest rounds (FED_C2M1 mech section)

Found by card 241 run 8 (`logs/c241-c2m1-r8-client.log`). Not a play blocker: the rounds after are clean and
both objective lists match. New digest class, so it gets a card (rule: new WARN class fixed or carded).

## Evidence

- Round 21 (client 189346877): `actors 29995 host, 29996 client, mismatches 1 (classes STLevelObjective:1)`.
  Round 22 (189376879): the same class, 1 mismatch. Rounds 23 on: 0 for this class.
- Host at that time: `LOS_O05_SO01` complete 189336762 (card 255 mirror, client pilots), `SO02` complete
  189389863. Run c257-c2m1 (same DLL, same route) has no such round.

## Step 1 (static plus the two logs, no run)

Name the objective actor from the round 21 and 22 detail lines, find the client row that created it and the
host row that should have removed or matched it (card049 journal 110 to 120), and say why c257 did not show it.

## Step 2

Fix by class in `mod/src/ubw/objectives.cpp` if a row is missing or late; no per-objective case.
Proof: c241 scenario mech section, digest rounds with 0 `STLevelObjective` mismatches. Budget: 1 run,
batched with another FED_C2M1 card.

## Step 1 answers (logs of run 8 plus `objectives.cpp`, 2026-11-13)

- The extra actor is `LOS_O05_SO03` (round 21 detail: `mismatch STLevelObjective O:LOS_O05_SO03 presence host 0
  client 1; no row sent`).
- The client's own copy of the level script created it. Client t=189346570, context `FED_C2M1_Scripting_C`, one
  script frame: `AddSubObjective(LOS_O05, LOS_O05_SO03)` ran natively (objectives 30 to 31), then
  `CompleteSubObjective(LOS_O05, LOS_O05_SO02)`, which card 100 skipped (`skipped local CompleteObjective
  LOS_O05_SO02`).
- The host ran the same script frame 43 s later (t=189389863, journal 115 and 116). The client then logged
  `ignored AddSubObjective for LOS_O05_SO03: the id exists already` (`c49_skip_add`, `objectives.cpp:719`).
  Between the two times the client held one objective the host did not: digest rounds 21 and 22.
- No host row is missing or late. The fault is that card 100 stops the client's script from completing or
  incrementing an objective, but nothing stops it from adding one. Run 8 has five such client-first adds
  (`LOS_O02_SO05`, `_SO05a`, `_SO05b`, `LOS_O05_SO03`, `LOS_O05_SO05`); only the one that led the host by more
  than a digest period showed up as a mismatch. That is also why c257 was clean: the client's script did not
  lead by a whole round there.
- What started the client's frame (level script bytecode, `kis.py`, plus run 8 rows):
  - The frame is ubergraph block 14805. Its only entries are the `VolumeTriggerDelegate` events of
    `BP_PlayerPresence_C_25`, `_27` and `_75`. The block has no condition on who entered. Besides the Add it calls
    `BP_EM_1FactionLoop_C_7.Trigger Encounter Manager()` and disables the three presences, so the client also ran
    those locally.
  - Host t=189346508: scenario command 303 warped the client's mech (server copy) to (196541, -45785, 6347), about
    1090 uu from presence `_25` at (197630, -45854, 6360). The client's frame ran 62 ms later.
  - The host's own frame ran at 189389863 with the host's pawn at (181443, -17435), beside presence `_75`. For
    43 s the host held the client's mech at presence `_25` and did not fire.
- Why the two sides differ (static):
  - `ASTPlayerPresence::OnBeginOverlap` 0x50F6880 has no player filter. It checks its enabled byte +0x30A, begun
    play, `USTSaveSystemLibrary::IsLoadInProgress`, and that the other component is valid and has begun play. Which
    actors reach it is decided by collision alone.
  - Card 227 is not involved: C2M1 has no gated presence (no `card227` row in run 8), so the detour passes through.
  - On foot, both sides fire for either player's pawn (c240-c1m1-r8: the host's presence `_7` logs entries by two
    different `BP_PlayerCharacterMI_C` pawns).
  - `BP_BaseMech` `Set3DMeshState` (called from `ReceivePossessed`) sets the capsule to collision enabled 3 and the
    mesh's overlap events on only when `USTCombatUtilityLibrary::IsPlayer(self)` 0x5243690 is true; otherwise the
    capsule gets collision 0 and overlap events off. `IsPlayer` tests a gameplay tag on the actor's ability system
    component (tag global 0x14943A860), the tag `ASTPlayerMechBase::OnBoardingComplete` 0x50F6950 adds.
  - Prediction for the proof run (one row per side at the card 075 `piloted mech` line): on the host, the
    client-piloted copy has capsule collision 0 or `IsPlayer` 0; on the client it has 3 and 1. If confirmed, a
    client-piloted mech trips no presence on the host: a new card (host copy of a client-piloted mech gets the
    same player tag and collision state as the client's), with a sweep of what else reads `IsPlayer`.
- The client running level script chains on its own events is the class of card 254. The fix below holds either
  way: the host's presence events are the only ones allowed to create objectives.

## Fix spec (step 2)

- `mod/src/ubw/objectives.cpp`: extend the card 100 rule to the Add kinds. On the client, a native Add call
  that is not a replay of a host row (all six Add kinds of `C49Kind`) returns without the original, through
  the same path as `c100_skip_local`. The host's journal row or checkpoint snapshot is then the only creator.
- Log: `card258: client skipped local <kind> <id> (only the host adds; N skips so far); caller exe+0x...`.
- Check before coding: the 14 adds the client's script makes at map load (t=188850956) must all arrive by
  snapshot or journal (`c49_emit` snapshot path, `objectives.cpp:445` to 511). If any id has no host row, report
  it and stop.
- Do not touch `c49_skip_add`; it stays as the guard for rows that still find the id.
- Prediction (FED_C2M1 run batched with 254 and 260): `card258: client skipped local` rows for the client-first
  frames, every objective on the client created by a `replayed` row, 0 `STLevelObjective` mismatch rows, the
  client's objective count equal to the host's at each digest round, and no `the id exists already` row outside
  a snapshot.

Diagnostic row in the same build (decides the mech prediction above): extend the card 075 `piloted mech` line
(`vehicles.cpp:1670`) on both sides with the capsule's collision enabled value, the mesh's overlap events flag and
the result of `USTCombatUtilityLibrary::IsPlayer` 0x5243690 for the mech. No behaviour change.

Pre-trace for the follow-up card (senior, static): native readers of `USTCombatUtilityLibrary::IsPlayer` besides
Blueprint (`execIsPlayer`): `USTImpactGameplayCueNotify::ExecuteImpact`, `DamageCauserIsPlayer` (2 calls),
`ASTWeapon::InitialiseWeaponItem`, `ASTEnemyProjectileBileBlob::IsAbleToAttachTo` and `ShouldExplodeOnImpact`,
`ASTEnemyHomingProjectileBase::IsOutsideTrackingLimitAngle`. If the host copy of a client-piloted mech lacks the
tag, each of these treats that mech as a non-player on the host.

## Run B (c241-c2m1-b1, build 3), senior verdict 2026-11-13

- Held: 22 client `card258: client skipped local` rows; 0 `STLevelObjective` mismatch rows in 32 digest rounds;
  `LOS_O05_SO03` created on the client by the host's row (replayed 233744390, local add skipped 233744391).
- `the id exists already`: 3 rows at 233430197 to 233430205 (`LOS_O02_SO05`, `_SO05a`, `_SO05b`). Not a client
  add: the host's own script ran the same three Adds twice (233369551 and 233430164, journal 87 to 91) and the
  client ignored the second set. `c49_skip_add` worked as the guard. Host double add goes to card 277.
- Broken: the mech prediction. `piloted mech` rows read `capsule collision 3, IsPlayer 1` on both sides (9 host,
  9 client). The host copy of a client-piloted mech has the player tag and collision. No follow-up card for the
  tag. `mesh overlap events -1` on both sides is a failed read of the flag, not a state.
- The run 8 lead did not recur: with the host on foot and the client's mech both standing at
  `ActorReference_C_23` (141 uu) for 21 s, then at `_25` for 21 s, neither machine ran block 14805. It ran on the
  host at 233744356, 3 ms before the host's warp to `ActorReference_C_27` logged, and on the client 35 ms later.
  So the standing points are outside all three presences, and run 8's client-first frame was not caused by the
  mech's collision state. Its cause stays open; with this card's fix it can no longer create an objective. What
  a client-first frame can still do locally (encounter manager trigger, presence disable) is card 254's class
  and card 228 item 2.
- Accepted. Card 205 closes with it.
