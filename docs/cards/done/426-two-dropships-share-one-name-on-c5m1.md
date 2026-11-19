# 426: two dropships share one name on C5M1

## Finding
`WARN card159 ... dropship check` with totals `12 of 13 match` in every chain run that reaches C5M1 (c413b107a to
c420b117a, c399b122a). Host inventory: `BP_ProceduralDropshipTroops_C n=11`, names `_0` to `_9`: two actors named
`BP_ProceduralDropshipTroops_C_0` in different levels. The snapshot sends 13 rows, two keyed `_0` with different
rotations (yaw 135 and -145). The client's `ds_find_placed` (`mod/src/ubw/dropship.cpp:648`) returns one copy for
both, so the join round ('J') and its one check round 5 s later ('C', `kDsCheckMs`) both land on that copy
(`58846 uu off, rotation 80 deg off (set)`) and the other copy is never synced.

## Fix
Make the key unique per level: name plus a hash of the owning level's name (as the card 132 digest key does), inside
`kChanNameMax` 64, for snapshot ('J', 'C') and live ('L') rows; the client matches name and level. Sibling sweep:
every name-keyed mechanism that resolves with `c50_find_placed` and no location; list each with a verdict.

## Proof
Chain run: C5M1 dropship totals `13 of 13 match`, 0 `WARN card159` dropship rows.
