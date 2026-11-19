# Card 462: after a checkpoint load each construction-component CollisionOp reaches the client twice, the second is skipped

## Evidence (run `c460b164a`, b164, FED_C3M1, travel checkpoint load at 522057439)

- Map start: host sent 30 `CollisionOp ... NODE_AddStaticMeshCompo 0` frames (BP_PylonSpline_C, caller
  exe+0x37C2C74), the client applied 30.
- After the load (client 522065490): 78 such frames received, 40 applied and 38 skipped (`WARN card132: recv N ...
  result skipped: no placed copy matches`). Per ledger hash the counts pair exactly: a7e7f418 7 applied / 7 skipped,
  a8e7f5ab 18 / 18, a9e7f73e 3 / 3, aae7f8d1 9 / 9, abe7fa64 1 / 1 (fb0c4d61 2 / 0).
- The client `WARN card095 ... collision <text>: no placed actor (or component) matches` detail row did not print
  (the `c95_line` cap was spent), so the text of a skipped frame is not in the log.
- `c418b158b` (same route, no load): 0 skips.

## Reading

The first copy of each row applies, so no collision state is lost; the cost is 38 wasted frames and 76 WARN rows
per load. Not known yet: whether the second copy is the same row journalled twice (pre-load row kept plus the
load's re-run of the construction script) or a row with another position or component name. `c95_map_end` resets
the journal at LoadMap enter only; a checkpoint load inside the same map may not pass there.

## Next step (b165, diagnostic only)

- The client skip row for a CollisionOp prints its full text outside the `c95_line` cap (own cap of 48 per map).
- The host join-replay row after a load prints the journal row count and how many rows share name + component.

## Prediction

In the c460 scenario after the load, each skipped text equals an applied text except for one field (position or
component suffix), or is identical; the host row shows about twice the map-start row count. The fix follows from
which field differs.


## b166 rows, run `b40b166a` on FED_C1M1 (2026-11-16 21:14)

No client skip row on C1M1 (the case needs FED_C3M1, scenario `c460-load-c3m1.txt`, not yet run on a build with the
rows). The host rows show a second defect in the same journal: two different placed pickups share the journal key.
`card462: host collision row 17 BP_AmmoPickup_MoritaSmall_C_0 PlayerCollider at (6424, 11495, 99) value 0 shares
name + component with row 16 at (13317, 13364, 208) value 0` at the first replay, and the same pair (value 1) at
the checkpoint load replay. Same object name in two sublevels, 7000 uu apart. A journal and a client lookup keyed
by name + component cannot tell them apart: one of the two can get the other's value on the client.

Next: (1) junior reports how the client resolves a CollisionOp name (first match, or by position); (2) run
`c460-load-c3m1.txt` on the next build for the skip rows; (3) fix both from the rows: key and resolve by name +
component + spot.


## Senior code read (2026-11-16 21:27): the "second defect" is void

`c95_collision_row` (door_c95.cpp:747) already keys a journal row by name + component + spot within `kC95RowUu`
500 uu, and the client resolves a frame with `c95_find_nearest(name, at)` (nearest same-named placed actor to the
frame's spot, door_c95.cpp:891). The card 421 pair path sends every row of a name hash (`c421_send_named`). So the
two `BP_AmmoPickup_MoritaSmall_C_0` rows 7000 uu apart are two correct rows that reach their own actors; the b166
"shares name + component" count only says the name repeats. No fix owed for it. On C3M1 (`c460b167a`, first
replay) the count is 0 of 156 rows.

Run `c460b167a` did not reach the load: it failed at the satchel C plant (line 127). Logged cause: the client's
hold started 427 ms after the warp while the pawn was still falling (`card456: client ... hold started ... speed
612.5`, z 322 -> 261), and the interactor trace lost the site 1.48 s in (`hold interrupted ... progress 1.48 ...
trace now null`). Scenario fault (look taken before landing); `client wait near` became `client wait stand`.
The post-load skip rows are still owed by the rerun.


## Skip texts (run `c460b167b`, b167, 2026-11-16 21:34): cause found

Passed (139 ok). Host journal 156 rows at map start, 1522 at the load replay. Client: 29 `WARN card095 ...
skipped`, all `BP_PylonSpline_C_<n> ... 0 NODE_AddStaticMeshComponent-3[_<k>]` (pylons 0, 4, 5, 6, 7).

Cause: these are writes by the pylon's user construction script on components it adds. The host's in-place
checkpoint load re-runs the script, which creates the components again under new numbered names and writes their
collision again; the host journals those as new rows. The client rejoins on a fresh map, runs the script once and
has only the first-run names, so the second-run names match nothing. Nothing is lost: each machine's own
construction script writes the same value on its own components.

The card 147 rule ("the constructor writes the same value on both machines", `kRfNeedInitialization` on the owner,
door_c95.cpp:1281) does not cover the construction script: the actor is past initialization then.

## Fix (b168)

Host `c95_relay_collision`: a write made while the owner runs its user construction script (the engine's
`AActor::IsRunningUserConstructionScript` bit), or on a component whose creation method is the user construction
script, is counted as `construction` and neither journalled nor sent. Predictions on `c460-load-c3m1.txt`: 0
client `WARN card095 ... skipped`; the 30 map-start `BP_PylonSpline_C ... NODE_AddStaticMeshCompo` frames are gone
(host tally `N construction` > 0, rows by name without BP_PylonSpline_C); pylon collision the same on both sides
(digest rounds 0 mismatches); journal rows at the load replay lower by the pylon rows.


## b168 fix is inert; corrected cause (2026-11-16 22:29, run `c460b168a`)

Same 29 client `WARN card095 ... no placed actor (or component) matches, skipped` rows after the load; the host's
construction tally is 0, so no write runs while `IsOwnerRunningUserConstructionScript` is true. Senior trace:

- The actor is found (`card421: client asks for hash ... 1 live copies (BP_PylonSpline_C ...)`); what fails is
  `c95_component` (`StaticFindObject` by the component's numbered name).
- The components are made by the construction script's AddStaticMeshComponent node: `NODE_AddStaticMeshComponent-3`,
  `-3_0` ... The number is a per-process counter. First replay: the host's rows name `-3_0`, `-3_6` .. `-3_12`. After
  the host's in-place load (journal 156 -> 1510 rows) they name `-3`, `-3_0` .. `-3_7`; the client's fresh map has
  its own numbers. So the name of a script-created component is not an identity across machines.
- The client makes the same 29 writes itself on its own load (`card137: client change row BP_PylonSpline_C component
  collision by local sim: 29`, 3 -> 0, same caller exe+0x37C2C74), so no collision state differs; the cost is the
  29 skipped rows and WARN lines per load.

Fix (b169): the host does not journal or send a collision row whose component is a construction-script node
component (name starts with `NODE_Add`): it cannot be resolved by name on the other machine and both machines write
it themselves. Counted in the totals row (`N script-node component writes kept local`). The inert
`g_owner_running_ucs` check and its offset entry go. Known limit, recorded: a gameplay write on such a component is
not synced (it never could be by name; none observed).

Prediction: client 0 `WARN card095 ... skipped` after the c460 load; the second replay row count drops below 1510;
the host totals row shows the new count above 0; 0 digest mismatches on the pylons.


## Closed 2026-11-16 22:48 (b169 `d99dcd4bd6123245`)

Run `c460b169a`: client 0 `WARN card095` (29 on b168); host rows `29` then `58 script-node component writes kept
local`; digest rounds 1 to 4 with 0 mismatches. The second replay still carries 1459 rows (destructibles the load
set again), sent as 1440 pair rows in 32 frames plus 19 whole rows: small on the wire, not carded.
