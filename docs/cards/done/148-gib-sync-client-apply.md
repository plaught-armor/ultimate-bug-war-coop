
## 2026-11-16 12:55: client "asleep above the ground" WARN, cause of the client-only rows (senior)

Run `c423b130a` (C2M1): client `rest checked 363, above the ground 44 (host flagged too 31)`, 15 `WARN card148` lines,
all "(colliding bounds". Host `card148: host gib part ... rests with nothing within 40 uu below` rows: 24 of 24 "(all-component
bounds". The two sides probe from different boxes: the host's part at rest has collision off (low significance,
`ASTGorePartBase::SetPhysicsEnabled(false)`), so `gib_no_ground` falls back to all components; the client's copy has
physics and collision on (card 279 switches them on for the R snap), so it uses the colliding box. Same pose (card 201
rest check), different box, different flag.

Fix, patch `c148_same_bounds_patch.py` (gib_c137.cpp `gib_no_ground`): both sides probe from the all-component bounds.
Prediction on C2M1: every client `above the ground N (host flagged too M)` row has N == M and there are 0
`WARN card148` lines; a line that remains names a part whose pose differs from the host's.
Not changed: the client copy keeping collision on at rest where the host's has it off (card 279 needs physics on for
the snap to hold). Gore parts only collide with the world, so no player-visible effect is known.

## `WARN card148 ... asleep above the ground` in run `c418b158b` (senior, 2026-11-16 18:59)

Six client rows, host parts 28 to 34 at about (-73400, -53350, 217 to 332), about 70 000 uu from both players. The
host found ground under those parts, the client did not; host parts 17 and 20 at the same place were flagged on the
host too. The gibs come from the scenario step `host clear 3000 kill` run far from the client. Working conclusion,
not yet proven by a row: the client has no collision loaded that far from its own pawn, so its downward probe hits
nothing. The pieces sleep at the host's pose and no player sees them. Treat as known noise for far parts. Possible
diagnostic change when `gib_ground_check` (`mod/src/ubw/gib_c137.cpp:1114`) is next touched: add the distance to the
own pawn to the row, and do not WARN beyond the collision streaming distance. A WARN for a part near the client
(within about 15 000 uu) stays a real finding.

## Closed (2026-11-17 13:09)

The only open item was the client `WARN card148 ... asleep above the ground` rows. Every such row read so far
(run `c418b158b`) is for gib parts about 70 000 uu from both players, made by a scenario `clear ... kill` far from
the client; the pieces sleep at the host's pose and no player sees them. The card 148 apply path itself has no open
defect.

Reopen when: a `WARN card148 ... asleep above the ground` row names a part within about 15 000 uu of a client pawn
(compare the row's position with the nearest player position row), or a playtest shot shows a gib piece hanging
in the air on one screen. When `gib_ground_check` (`mod/src/ubw/gib_c137.cpp:1114`) is next touched, add the
distance to the own pawn to the row and drop the WARN beyond the collision streaming distance.
