# 448: A rejoin is a full heal (Health and Armour are not kept)

Found 2026-11-16 by static trace (senior), sibling of cards 440, 445, 446. Coded as a patch script
(`/home/king/.cache/claude-tmp/claude-1000/-mnt-based-backup-Repos-ultimate-bug-war-coop/316f03d0-ef4a-41af-8d1d-90993a7b967c/scratchpad/c448_patch.py`, 5 edits, `--check` passes on the b146 tree); proof owed.

## Defect

A client that leaves and rejoins on the same map gets a new pawn with full Health and Armour. Card 440 keeps the
weapons and ammo as a held Restore but writes its Health field as `-1.0` ("a kept inventory carries no Health",
`restore_c143.cpp` `c440_hold`), because the kept entry is written on card 327 row changes, not when the player
leaves. A wounded player heals by reconnecting, and the host sees a full-health marine where a hurt one left.

## Fix

- `c448_logout(pc, pawn)` (`restore_c143.cpp`), called from `detour_logout` before `c43_logout`: reads the
  leaving marine's Health (`c21_health`) and Armour (`c48_armour`) into its `C440Keep`. Only for an Up marine on
  foot with Health above the card 043 held value; otherwise none is kept and the rejoin is as before.
- `c440_hold` writes the Restore head as `<health> <armour>`, `<health>` or `-1.0`. The apply, the Armour set
  and the card 198 read-back are the existing Restore path (`c143_apply`).
- With card 447 in the same build the pawn read at Logout is the soldier even when the player left in a seat.

## Done when (one run, the rejoin block of `c423-gates-c2m1.txt`; the client takes damage before the leave)

- Host: `card448: host t=... controller N leaves on BP_PlayerCharacterMI_C... (Up): kept Health H, Armour A` with H
  below the pawn's maximum.
- Host after the rejoin: `card143: host t=... Restore applied ... Health <full> -> H (want H), Armour ... (want A)`
  and no `WARN card198` read-back row for it.
- Client: its own Health row or HUD shot after the rejoin shows H, not full.
- 0 `WARN card440`, 0 EXCEPTION, digest 0 mismatches.

## Not covered

A player who leaves downed or dead rejoins Up with the game's values (unchanged; card 327 owns the Down state).

## 2026-11-16 17:15: b148 cannot pass

`c448_logout` is called from the Logout hook, where the PC's pawn is already null (card 447, run c447b147b), so it
keeps nothing. The card 447 second fix (`c447b_patch.py`) moves the call to the `PawnLeavingGame` hook. The proof
run is valid only on a build with that patch.
