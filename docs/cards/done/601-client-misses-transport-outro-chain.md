# Card 601: the client misses the transport dropship outro chain after card 595

## Finding

Found by the card 301 FED_C3M2 full pass (2026-11-22). Card 595 (b446) skips the client's own
`ASTPlayerTransportDropship::OnInteraction` at its press, so the client copy never broadcasts OnPlayerEntryRequested
(+0x728). Every level script effect bound to that broadcast now plays on the host only.

Log proof, c595b447d (C3M1 evac):

- After the release, the host has `card493 ... 'Trigger Flying Sequence MUS cue stop'` and `card322: host ... conversation list FED_C3M1_DROPSHIPOUTRO01_C`, then card260 EndLevel 13 s later.
- In the same window, the client has only `card595: client ... relayed, own original skipped` and `card260: client EndLevel (relay)`. It has no conversation row, no outro SpawnSound2D line and no flying music.

The C3M2 evac chain (ubergraph) carries more on top of that:

- `EDropship_Interacted` @12862 leads to Dialogue_Outro and to `NukeExplosion` @26203.
- `NukeExplosion` runs `CreateLevelSequencePlayer` @26243 and `Play` @26495.
- `NukeExplosion_Shake` @26528 runs STCommonUtilityLibrary::StartCameraShake.

The mid-level rides go through the same OnInteraction, so the client misses their local chains too: C3M2 C_0 @26665, C_1 @26598 and C_3 @26732. Those chains carry SetIsInteractable(false) and the flight StartAction calls.

This breaks the user rule that every host-visible effect reaches the client (all-effects-sent). It is a class gap: one path, every transport on every map.

## Fix (b455)

New op `ChanOp::DropshipBoard`, host to client. Its text is the dropship's placed level key.

- Host: `c601_send_board` (dropship.cpp) runs after every host run of the original OnInteraction. That covers both the card 595 release and the unheld WARN path. It sends to every ponged PC as a priority row and logs `card601: host t=<ms> boarding of <name> sent to N of M PCs`.
- Client: `c601_board_receive` finds the placed transport by name and runs its own copy's original OnInteraction once, with the detour bypassed. The original clears +0x748, sets +0x749 and broadcasts +0x728, so the client's level script outro chain plays. If the copy's +0x749 is already set, nothing runs.
- The client-side skips stay in force for the parts of that chain only the host may do:
  - EnterDropship is skipped on the proxy (card 015);
  - EndLevel from the own script is skipped (card 260);
  - SetObjectiveVisibility and callouts are skipped (card 140).

  Before card 595 the client ran this same original at its own press, so the chain is known to be safe to run on the client.

## Predictions (combined b455 run, C3M1 evac)

- Host: one `card601: host t=<ms> boarding of BP_PlayerTransportDropship_C... sent to 1 of 1 PCs` row, within 5 ms of the card595 release row.
- Client: one `card601: client t=<ms> boarding of ... from the host: own original ran (outro chain)` row, within 200 ms of the host row.
- The client also shows `card322: client ... conversation list FED_C3M1_DROPSHIPOUTRO01_C`, then outro SpawnSound2D lines.
- The client has exactly one `card260: client EndLevel (own script) skipped` row and one `card260: client EndLevel (relay)` row.
- No `WARN card601`. No new WARN overall. 0 card132 mismatches.

C3M2 nuke sequence and shake: the client has a card130 or LevelSequence row and a card099 StartCameraShake row in its outro. This is read on the next C3M2 evac run, not in this batch.

## b455 result, c601b455a (2026-11-22): all predictions met

- Host: `card601: host t=208242802 boarding of BP_PlayerTransportDropship_C_1@1e969787 sent to 1 of 1 PCs`. Its t is
  the same as the card595 release row (t=208242802).
- Client: `card601: client t=208242836 ... own original ran (outro chain)`, 34 ms after the host.
- The client log then shows:
  - `card322: client ... conversation list FED_C3M1_DROPSHIPOUTRO01_C`, the same row the host logs;
  - its own card493 rows for 'End the level' and 'Trigger Flying Sequence MUS cue stop';
  - five SpawnSound2D rows, SC_FED_C3M1_DROPSHIPOUTRO01_SEQ01-05.
- The client has one `card260: client EndLevel (relay)` row and one `card260: client EndLevel (own script) skipped`
  row.
- No WARN card601, and 0 card132 mismatches.
- The only new WARN in the run is the card140 hit on a ragdoll root during the bug wave. It is not on this path, and
  card 603 covers it.

Gate c448b455a: drive rc 0, 0 mismatches. The only try-fails are on the allowed lines 55, 155, 185, 186, 193 and 212.
The WARN set matches the b454 gate.

## Closed (senior, 2026-11-22)

Proven in c601b455a: the client runs its own outro chain 34 ms after the host release, with one EndLevel relay row and its own EndLevel skipped. Gate c448b455a passed. Ships on lane 1 as b455 (dwmapi 0c2e33933afbf1a3).
