# 452: client is offered ENTER on a mech another player pilots

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: patch written (`c452_patch.py`), handed to the junior for b152; FED_C4M1 proof owed.
"E ENTER M7" on the mech the host pilots).

## Fault (static trace)

`ASTPlayerMechBase::CanInteract` (exe+0x50E0560) refuses when the mech's `APawn::Controller` (+0x2D8) is valid, the
health component is dead, or a timer is active. A controller is replicated to its owning client only, so on the client
a mech the host pilots has no controller and the check passes: the prompt shows, E sends `MechEnter`, and the host
ignores it (`WARN card075: host MechEnter ... ignored: ... already piloted`). No state breaks; the prompt lies.

## Fix

Client, always on: hook `CanInteract`; when the mech's `APawn::PlayerState` (+0x2C8, replicated to everyone, set while
a player possesses the pawn) is not null, return false.

## Proves it done

- Client row `card452: client CanInteract on BP_BaseMech_C_... refused: piloted (player state ...)` while the host
  pilots and the client looks at the mech (at most 4 rows per process).
- Client screenshot aimed at the host-piloted mech shows no ENTER prompt; after the host leaves, the prompt is back
  and the client can enter (`card075: host MechEnter ... OnInteraction`).
- The client's own mech: no `card452` row needed and leaving works as before (CanInteract is not on the leave path).

## Risk

If the mech keeps a PlayerState after an unpossess on the client (late replication), the prompt comes back a moment
late. A mech that never clears it would block entry: the second proof bullet catches that.
