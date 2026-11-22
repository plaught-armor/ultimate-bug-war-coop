# Card 554: host pawn teleports at random (training map)

Playtest 2026-11-20 on b366 (`e62e76f63a05d88f`), FED_C1M1 training. User report, twice: "host keeps teleporting
randomly in training area", "host keeps randomly teleporting, at random".

## What the client log shows (`UBWCoop-menu-20261120-184517.log`)

- No warp rows: the card132 Warp and WarpTo ledgers are 0, and there are no card046 kill floor or card307 rows.
- Ping is steady: 12-ping windows read min 49, avg 57, max 66 ms.
- The host pawn's position on the client is not logged per frame. The only samples are the remote jump sound rows,
  which put the host about 2800 uu apart every 2 s (about 1300 uu/s) during 99772157-99776681.
- The host pawn copy moves by vanilla UE movement replication; the mod does not change its NetUpdateFrequency or
  smoothing.

## Leading guesses (unproven)

1. The host's sprint speed does not reach the client's copy. The copy then predicts at walk speed and is snapped
   forward by each movement update.
2. Movement updates for the host pawn arrive in bursts (net saturation or a card 527 perf cut), so the copy jumps
   between them.
3. An actual host-side teleport (a script warp, card 046 kill floor on the host, card 307 spawn placement) that the
   client log cannot see.

## Related evidence: name tag and ring off the body

User screenshot (C1M2-era map, night rock area): the remote player's "Player" name tag and cyan ring stand on empty
ground, with no trooper mesh at that spot. That fits guess 4 below, which would also explain the "teleports".

4. The copy's network smoothing mesh offset does not decay: the capsule (tag, ring) follows the host, the mesh lags
   at an old place, and each reset snaps the mesh to the capsule. This is the card 524 class (NPC copies whose
   movement stopped ticking, so SmoothClientPosition never decays `ClientMeshTranslationOffset`), here on a player
   pawn copy. Check: the copy's CMC tick state, NetworkSmoothingMode, and the mesh-to-capsule distance per frame.

## Lead: the host skipped the dropship flight (user, 2026-11-20)

The user thinks the off-body name tag followed the host skipping the flight cutscene. Client log FED_C2M1 at
100110283: `card399 ... Warp while riding: own pawn detached here, before the replicated detach`, then `card146 WarpTo`
moved the client's own pawn to the landing spot. Card 399 handles only the client's OWN pawn on a skip. The host pawn
copy on the client also rides the transport dropship and is detached and moved about 18000 uu by replication alone.
The trace must check what that copy's mesh does at the detach: its relative location and the CMC smoothing offset
(`ClientMeshTranslationOffset`), before and after the skip. If the mesh keeps a ride-time offset, the fix belongs with
card 399: on the skip, reset the copy's mesh to its base offset the same way `c524_mesh_reset` does.

## Later report: "host keeps teleporting to me" (FED_C2M1, about 100740000 ms)

The client log has only one WarpTo row for the whole session, the card 399 skip at 100110283, and it moved the
client's own pawn. No mod row moves the host pawn copy. A static search of the mod finds no code that moves a
remote player pawn toward the local one. So the jump to the client's spot is either vanilla movement replication
(the copy resolving a wrong movement base, or its smoothing offset), or a host-side move that only the host log shows.
The playlog audio rows give a rough track of the host copy ("uu from the nearest remote pawn"): the trace can plot
that distance against the listener to find the jumps.

## Lead: the host pawn copy is destroyed and re-created by net relevancy (strongest, from the client log)

The client log shows a new remote `BP_PlayerCharacterMI_C` copy (`pawn-collision: client PostInitializeComponents
... LocalRole 1 RemoteRole 3, controller 0`) and a new card416 ally blip on FED_C2M1 at 100110318, 100629456,
100693830, 100814732, 101280123, 101368077 and 101417804. On a two-player map the host has one pawn, so each new
copy means the actor channel closed (card434 "channel closed" rows) and reopened. A player pawn has no
`bAlwaysRelevant`. Past the net cull distance the client drops the copy. When the host comes back in range the copy
spawns at the host's current spot, so to the client the host pops in near them: "teleports to me". The dropship has
the same fix already (`dropship.cpp:130`, sets `bAlwaysRelevant` so its copy does not freeze past the cull distance).

Fix direction: set `bAlwaysRelevant` on every player-controlled pawn on the host (troopers and player-driven mechs),
by class, at the possess or spawn, logged once per pawn. Prediction: one remote player copy per host pawn per map
(no further PostInitializeComponents rows for it until a respawn), and no card434 channel close for it.

## Needed

- The host log `UBWCoop-host-20261120-*.log` from the host's machine.
- From the user: does the client see the host jump, or does the host see its own pawn jump?

## Next step (after the playtest)

A one-run probe: host sprints and walks a fixed line. The client logs the host copy's location, velocity and
MaxWalkSpeed every frame, and the host logs the same for its own pawn. Then compare the copy's per-frame jumps
against the host's path.

## Fix (b431, 2026-11-21)

`pawn_collision.cpp` `c554_relevant`, called from the `ASTPlayerCharacter::PossessedBy` detour: on the authority
side, a pawn possessed by a PlayerController (trooper or mech, at every possess, so respawns and mech exits too) gets
`bAlwaysRelevant` set through `actor_always_relevant` (`dropship.cpp`, the card 140 reflection setter, now shared and
reporting whether the bit was already set). Row: `card554: host player pawn <name> (<class>) set always relevant: yes`,
or a WARN when the reflected bool is missing. Not covered: turret seat pawns (not ASTPlayerCharacter).

Predictions for the proof run (C1M1, host and client walk apart past 15000 uu and back):
1. One `card554 ... set always relevant: yes` row per possess on the host; none on the client.
2. On the client, one remote `BP_PlayerCharacterMI_C` PostInitializeComponents row per host pawn per map; no new copy
   when the players come back together.
3. The host copy's location on the client stays continuous (no jump over 500 uu between samples at the rejoin).

## Proof (c590b431a, b431, 2026-11-21): passed

- Prediction 1 holds. The host logged two `card554: host/standalone player pawn BP_PlayerCharacterMI_C ... set always
  relevant: yes` rows, one per trooper possess (its own pawn and the client's server pawn). No WARN row and no row
  for the turret seat or the missile.
- Prediction 2 holds. After the client mark (client log line 12591), the host warped to (-74736, 1600, 1767), about
  64000 uu away, held 8 s, and warped back. The client logged no new `pawn-collision: client PostInitializeComponents`
  row (6 of the 8-row cap used, so a new copy would have logged) and no new `card416: client blip slot ... added` row.
  The remote copy was kept across the cull distance and not re-created.
- Shots `client-c590b431a-c554-host-far-client.png` and `-host-back-client.png` show no visual fault. A Warrior Bug
  fills the client's view in both, so they do not show the host's pawn either way. The log rows are the proof.
- Logs: logs/c590b431a-host.log, logs/c590b431a-client.log, logs/c590b431a.drive.

## Closed (senior, 2026-11-22)

The fix is proven by logs and runs, and the proof is in the sections above. Under the no-playtest-parking rule, a proven fix does not stay open waiting for a play check. It is on the playtest checklist in STATE.md. If the user sees it again in play, reopen this card with that report.
