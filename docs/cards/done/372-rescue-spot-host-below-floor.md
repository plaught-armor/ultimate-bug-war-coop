# 372: rescue spot fallback used a host pawn below the kill floor

Found by static audit 2026-11-15 (senior), after card 370. Low: needs the checkpoint anchor try to fail first.

## Problem

`c46_rescue_spot` (`mod/src/ubw/objectives.cpp`) falls back to "beside the host's pawn" when the anchor try fails
(no anchor on this world, FindTeleportSpot fails, or no free side spot). It did not look at where the host pawn is.
Since card 370 the pawn being rescued can be the host's own pawn, below KillZ: the fallback then returned a spot
beside the falling pawn itself, under the world. Each cooldown the pawn was "returned" there and hurt 25, until dead.
A falling copy was sent beside a falling host the same way.

## Fix (in source, senior, not built yet)

The fallback takes the host pawn only when it is at or above `c46_kill_z`; otherwise `c46_start_spot` (PlayerStart)
is used. A jump above the floor still counts, as before.

Sibling (same class, same file): `c46_note_checkpoint` falls back to the host's pawn as saver when no player is
grounded and Up (card 370 made `c46_up_target` stricter). A saver below `c46_kill_z` now leaves the anchor where it
was, with `WARN card146: host checkpoint save with the saver below the kill floor`.

## Proof

Rides the next lane 2 build. Static only unless a run can make the anchor try fail; no scenario planned. Accept on
build + review of the diff, and no new WARN rows in the next kill floor run.
