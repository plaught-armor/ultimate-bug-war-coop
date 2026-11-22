# 454: the client gets no hurt camera shake

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: open, patch written (scratchpad `patch_c454.py`), not built.

## Fault

`ASTPlayerCharacter::HandleDamageEvent` (VA 0x1451ca290) calls `PlayHurtCameraShake(damage)` (VA 0x1451d6fb0) where the
damage event is raised. Damage is host side, and for a server copy the call plays nothing. Run c207b153a (b153, card
099 log on): the client's pawn took 9 drops (Armour 100 to 0, Health 100 to 1, `card157: client ... own pawn` rows,
host `OnDamageCaused ... (server copy)` rows) and neither log has a `StartCameraShake Shake_Generic_Damaged*` row. Host
logs of earlier runs have that row for the host's own hits. World shakes (explosion, fall, walk, fire) do reach the
client; only the hurt shake is missing.

## Fix

Client, in the per-tick own-pawn vitals poll (`c157_client_vitals`, `claim_c157.cpp`): when Armour plus Health dropped
since the last tick, call the game's `PlayHurtCameraShake(own pawn, loss)`; it picks the small, medium or large shake
by its own thresholds. A new pawn starts a new record and plays no shake for 6 s (the card 448 rejoin restore sets
Health about 4 s after the pawn appears).

## Proves it done

- Client log, card 099 log on: `card454: client ... PlayHurtCameraShake` rows next to the `card157` drop rows, each
  followed by `card099: client CameraManager::StartCameraShake Shake_Generic_Damaged*_C ... (local)`.
- No `card454` row within 6 s of a join, rejoin or respawn; none on a heal or armour pickup.
- Host count of `Shake_Generic_Damaged*` rows for its own hits unchanged; 0 EXCEPTION; digest 0 mismatches.

## Open

- `HandleDamageEvent` also calls `TrySetLastDamagingActor` and sets pawn +0xa20/+0xa24; whether a hit direction
  indicator hangs on those and is missing on the client is not traced.
- A scenario `health set` that lowers Health also shakes; harmless.
