# Card 284: objective-driven test steps

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: senior review items 1-7 and step 5e coded (not compiled: playtest running). No run before the playtest; the play DLL (`5e2ce00acad8efa3`) does not change.

## Why

User, 2026-11-13, after watching the C3M2 run: "it seemed like the objectives aren't being done correctly. there
needs to be some sort of way to be exactly at the correct objective and also have the information necessary to
complete it".

The soak scenarios (`tools/gen_c243.py` and siblings) drive objectives from a hand-kept table: fixed stand
coordinates, fixed actor names, fixed order. The table does not know which objective the game has active. When a
step is late, early, or the level script reorders, the players stand at the wrong place and act on the wrong
thing; the run either voids on a stand check (`c243-c3m2-b6` line 316, 172 uu off) or, worse, carries on and
"tests" nothing. The game itself holds everything the table guesses at.

## What already exists (traced, no new reverse engineering needed for step 1)

- `mod/src/ubw/autoplay.cpp` (card 126 phase 1): `c126_active_parent` (the open top-level objective added
  first), `c126_pick` (its open sub-objective with the marker on), `c126_actor` (`ObjectiveActors[0]`, array at
  `kObjectiveActors = 0x88`), `c126_kind_of` (class name table `kC126Kinds`: `PlayerPresence` = go to, `Switch` =
  interact; all else `Other`), `c126_find_shape` (the presence volume's box, sphere or capsule in world space),
  `c126_project_goal` (navmesh point on the target's floor), `c126_pawn_inside` (pawn capsule against the shape).
- Objective fields: `kObjectiveId 0x58`, `kObjectiveMarker 0x68`, `kObjectiveComplete 0x69`,
  `kObjectiveParent 0x70`, `kObjectiveCountTarget 0x98`, `kObjectiveCountCurrent 0x9C` (count subclass only).
- Verbs today: `objectives` (dump: `c98_dump`, `c66_dump`, digest), `objective complete <id>` (forces
  completion; not for objective tests), `autoplay on|off` (local player walks to the active objective; blocks on
  any kind but presence and switch), `warp`, `goto`, `clear <radius> kill`.

Gap: autoplay acts only for the local player, only walks, only knows two kinds, and prints nothing a scenario can
gate on. The scenario cannot ask "which objective is active, where is it, what does it need".

## Design

All host-side reads (the host owns objective state); one picker shared with autoplay, not a second copy. Move
`c126_active_parent`, `c126_pick`, `c126_actor`, `c126_kind_of`, shape and projection helpers behind a small
header section so the new verbs and autoplay call the same functions.

### 1. `objective now` (host): the readout

One row per open sub-objective under the active parent, the picked one marked:

```
cmd: N objective now: parent <id>; active <id> kind <kind> count <cur>/<target|-> marker <0|1>
cmd: N objective now:   actor[k] <name> (<class>) at (x, y, z) alive <0|1>      (every ObjectiveActors entry)
cmd: N objective now:   stand (x, y, z) from <shape center|root|actor> ; host <d> uu, client <d> uu, client2 <d> uu
cmd: N objective now:   needs: <text from the kind row>
```

Kinds, by class of the actors and the objective's own class, as a table (rows are data):

| kind | recognised by | needs (printed, and what the scenario does) |
|---|---|---|
| presence | actor class has `PlayerPresence` | a player pawn capsule inside the volume |
| use | actor class has `Switch` | stand within use range, look at it, press E |
| nest | actor class of a bug hole or nest (name from the C1M1/C3M1 logs) | the real blast (grenade or charge) on it |
| kill | actors are live bug or enemy pawns, or the objective is the count subclass with enemy actors | kill those actors; here `kill actor` is allowed only when the card says the fight is not under test |
| marker | `ActorReference_C` actor[0], or no actor and an objective marker actor (`BP_ObjectiveMarkerActor_C`) exists (review 4, 5) | reach the marker; completed by the level script (`now` prints the nearest `BP_PlayerPresence_C` and whether the stand point is inside it) |
| count | count subclass, no actor, no children (review 6: renamed from defend; no `defend` kind until a map shows one) | the count advances by the level script; defend rounds use `host clear 3000 kill` |
| other | anything else | row prints the classes; scenario voids with that text |

Unknown is never guessed: `other` prints the class names so the table gets a new row, by code review, not at
run time. The junior fills the nest, kill and count rows from class names already in the C1M1, C3M1, C3M2 logs
(card 049 `AddSubObjective ... actors N` lines and card 070 frames) before coding, and lists in the card report
which objective ids on those three maps land in which kind. Any id landing in `other` is a finding to bring
back, not to patch around.

#### Step 5a result (static, from the card 049 and card 070 rows of the C1M1, C3M1, C3M2 logs)

Kind rules as coded (`c126_kind_of`, rows in `kC126Kinds`): `PlayerPresence` = presence, `Switch` (BP_Switch_C,
BP_WallSwitch_C, BP_PanelSwitch_WithoutScreen_C) = use, `Bugnest` = nest, an actor that is the enemy class = kill,
no actor and the count subclass with no child objectives = defend, anything else = other. The objective's own class
is `STLevelObjective`, or `STLevelObjectiveCount` for the AddCount* ones. A count objective with child objectives is
a container: its steps sit one level down, and the picker takes those grandchildren.

| map | presence | use | nest | kill | defend | container | other (findings) |
|---|---|---|---|---|---|---|---|
| C1M1 | Assault_SO0-3, Asteroid_O1, CTF_SO0, ShootingRange_SO0, ShootingRange_SO2, TacSupport_SO0 | Assault_SO4 (PanelSwitch) | - | - | - | - | WeaponPickup: CTF_SO1, ShootingRange_SO1, TacSupport_SO2; SatchelBuilding_CTF: CTF_SO2, CTF_SO4; SatchelItemPickup_CTF: CTF_SO3; ActorReference_C: ShootingRange_SO3 (count 10), TacSupport_SO4, SO41, SO5, SO6, SO7; EncounterManager: TacSupport_SO3 |
| C3M1 | - | Beach_SO1_A/B/C, Satcom_SO1 (BP_Switch_C); Monorail_SO2 (PanelSwitch) | Monorail_SO2_A-D | - | - | Beach_SO1 (count 3), Resort_SO3, Resort_SO4 | ActorReference_C: Beach_Marker1/2, Beach_SO2, SO3, SO5, Monorail_Marker, Monorail_SO3, Resort_Marker, Resort_SO2, Resort_SO4_A/B; 0 actors, not count: Beach_SO4, Monorail_SO1, Resort_SO1; SatchelBuilding_Flakbug: Resort_SO3_A/B/C; BP_Radar_C: Satcom_Marker; no class row in the logs: Satcom_SO2, Satcom_SO3 |
| C3M2 | - | Monorail_SO3 (BP_WallSwitch_C_1), PowerPlant_SO2 (BP_Switch_C_2); DoorSwitch1/2 (1 actor, no card 070 row; use only if the BP_Switch_C_6/_8 of the generator are those actors) | Monorail_SO2_A-D | - | - | Monorail_SO2 (count 4), PowerPlant_SO3 (count 2) | ActorReference_C: Monorail_Marker (_0), Monorail_SO4 (_9), PowerPlant_Marker and PowerPlant_SO1 (_1), SiloBase_Marker (_16); 0 actors, not count: Monorail_SO1, Silo_SO1; no card 070 row: PowerPlant_SO5 |

Findings for the senior (not patched around):

1. `ActorReference_C` is actor[0] of most C3 objectives, presence-completed ones included. They land in `other`,
   so prediction 1 fails on C3M2 as written. `objective now` prints the nearest `BP_PlayerPresence_C` on every
   `other` row as information; `objective go` fails on `other` per the design. A kind row for
   `ActorReference_C` needs the referenced actor (a field read not traced yet), not a guess.
2. Pickups and satchel objectives (WeaponPickup, SatchelBuilding*, SatchelItemPickup_CTF), EncounterManager and
   BP_Radar_C have no kind row.
3. Containers hold their steps one level down. C3M2 Monorail_SO2_A-C are added with markers off and the container
   with markers off, so under the marker rule only Monorail_SO2_D is pickable; `now` prints each step's marker bit.
4. Never added on the C3M2 logs: Silo_SO2-SO6, PowerPlant_SO4 (the map run ended before them).

### 2. `objective expect <id>` (host)

Passes when the active sub-objective's id equals `<id>`; else the scenario voids at once with
`VOID objective expect <id>: active is <actual> (<kind>)`. Every scenario step that acts on an objective starts
with this line. This is the "exactly at the correct objective" gate: a step can no longer act on a stale or
future objective.

### 3. `objective go [host|client|client2|all]` (host)

Places the named players at the active objective's stand point by warp (existing warp path, `warp client2`
included), not by walk; movement proof stays in the movement run. Stand point by kind: presence = navmesh point
under the shape center, verified with `c126_pawn_inside` after the warp for each placed pawn; use = navmesh
point within `kC126UseUu` of the switch, yaw set toward it; nest, kill = navmesh point at a stated standoff from
actor[0] with line of sight wanted, yaw toward it; count = as presence; marker = on the marker's own location. Result row per player:
`cmd: N objective go: <who> at (x, y, z), <d> uu from target, inside <0|1|->`. No navmesh point, a pawn not
inside after the warp, or no active objective: the command fails with the cause, the scenario voids.

Players offset from each other by a fixed small ring around the stand point so three pawns do not stack.

### 4. `objective wait <id> [seconds]` (host)

Ends when `<id>` is complete (pass) or the time is up (fail, printing count cur/target and the active id). Replaces
sleeps after an objective action, per the "scenario waits on logs" rule.

### 5. Scenario generators

`gen_c243.py` and the other soak generators emit, per objective: `host objective expect <id>`,
`host objective go all`, `host clear 1500 kill`, the action the kind needs, `host objective wait <id>`. The
hand-kept stand coordinates and actor names leave the generators; the id order stays (it is the map's script
and is the thing under test). Aim-proof screenshots follow `objective go` as today.

## Not in this card

- Autoplay phase 2 (fighting, full hands-off play). This card only gives scenarios live objective data.
- Any change to objective sync itself.

## Review attack list (for the senior at review)

- A map where the active parent is not the lowest open slot (side objectives): does `expect` name the wrong one?
- Marker blink between steps (C1M1 Assault_SO0/SO2 swap every 1.5 s): `now` and `expect` must use the same
  keep rule as autoplay, or `expect` flaps.
- `ObjectiveActors` entry destroyed (killed bug): null and pending-kill entries skipped, not dereferenced.
- Objective with zero actors: stand falls back to the marker actor (`BP_ObjectiveMarkerActor_C`,
  `kMarkerObjective 0x2B0`), else fails with the cause.
- Client pawns: positions read on the host copies; `inside` computed on the host copy.

## Predictions for the first run (C3M2, after the playtest)

1. Every objective step prints an `objective now` block; 0 ids in `other`.
2. 0 `VOID objective expect`; each `objective go` row has `inside 1` for presence and count kinds.
3. Objectives N of M complete by their real trigger, each closed by an `objective wait` pass, no sleeps.
4. Digest 0 mismatches; no new WARN classes against `c243-c3m2-b6r2`.

Budget: one build, three runs.

## Senior review of the 5a-5d diff (17:25, uncompiled): changes required before the build

Attacked: the keep rule against parallel and abandoned steps, the picker with `need_actor` false, the `go` result
against a pawn that moves after the warp, the kind fallbacks, the wait timer, the drive gate, the server copy warp
path (same `g_set_actor_location` call as `c75_warp`, accepted).

1. **One "active" step does not model parallel steps, and the keep rule can stick.** C3M2 Monorail_SO2_A-D are
   four open steps of one container, markers off on A-C (finding 4): `expect Monorail_SO2_A` would void though the
   step is legitimately doable. And a step picked once stays `current` while open: a higher-slot step the script
   hides without completing blocks every later `expect`; autoplay has `c126_hidden` for this, the verbs have
   nothing. Change: `expect <id>` passes when `<id>` is in the **open step set** and then makes it `current`;
   `go` and the stand row of `now` use `current`. Open step set = open, under the active parent (directly or under
   an open container), has no children, and either its marker is on or a sibling under the same container has its
   marker on. A step directly under the active parent with its marker off is a future step: not in the set. `now`
   marks every row `doable` or `open`, and the one row that is `current`. With no `expect` yet, `current` is the
   picker's choice as now. The void text lists the doable ids.
2. **A container can be picked.** With `need_actor` false, a container (children, no actor) passes the filter and
   wins when its slot is the highest marked one. Steps with children are never steps: skip them in the verbs' pick
   and print them in `now` as `container <id> count c/t`.
3. **`go` proves the position at the warp tick only.** Run b6r2 had three voids from pawns that slid after the
   warp, and the upside-down float at the water stairs. `go` must finish after a settle (1000 ms, polled in
   `c63_continue` like `wait`), with the rows and the `inside` test taken from the settled positions, plus per
   pawn `moved <d> uu since warp` and its movement mode if it is cheap to read. A pawn more than 50 uu from its
   landing, or not inside: fail with the cause. This also gives the float case a logged row to diagnose.
4. **`ActorReference_C` gets a kind row: `marker`.** It is a placed anchor the level script puts the objective
   marker on (card 140: TacSupport_SO4's follows the resupply rack). Its location is where the player is sent, so
   standing on it is "at the objective"; no referenced-actor read is needed. Stand = navmesh point at its
   location; needs = "reach the marker; completed by the level script (see the presence row)"; the row also prints
   the nearest `BP_PlayerPresence_C` and whether the stand point lies inside its shape. `go` places on it. This
   moves most finding 1 ids out of `other`. The remaining classes of finding 2 (WeaponPickup, Satchel*,
   EncounterManager, BP_Radar_C) stay `other` until a map that needs them is driven; C3M2 needs none of them
   except what its own table shows: list which C3M2 ids are still `other` after this change.
5. **Zero-actor, non-count steps (finding 3)**: stand falls back to the marker actor as coded; kind `marker` when
   a marker actor exists, else `other`.
6. **Defend rule is really "count step with no actor".** A kill-count step with no actors would print "stay
   inside". Rename the kind text to `count` with needs "the count advances by the level script; defend rounds use
   host clear 3000 kill", and keep `defend` out until a map shows a real one.
7. Small: `c284_fail` formats into one static buffer, fine for one result per command; keep it that way (no two
   calls in one expression). `objective wait` start: confirm `g_c63.until = now + c.ms` (testrun.cpp:2296) is
   reached on the `break` path; say so in the report.

Accepted as is: parser, header move, `c126_kind_of` table, `c126_project` and shape split (autoplay behaviour
unchanged apart from the two extra `c126_pick` arguments and the grandchild rule), 5d (`active != 0` returns),
`c265_clear_kill` comment, drive.py lines shown. Not yet seen: the drive.py gate code that turns `expect` into a
VOID (only the gate parse is in the diff); include it in the next diff.

Generator (5e): go, after 1-6 are coded. Parallel sets emit `expect <id>` per member.

## Review items 1-7 and 5e as coded (junior, uncompiled)

1. Steps and the doable set (`objective_c284.cpp`). A step is open, under the active parent (directly or under an
   open container) and has no children. A step is doable when its marker is on, or when it sits under a container
   and an open sibling under the same container has its marker on. `expect <id>` polls 3 s (longer than the 1.5 s
   marker blink); it passes once `<id>` is doable, or when `<id>` is already `current` and still a step (a blink
   off on the current step is no failure), and makes it `current`. On timeout: `VOID objective expect <id>:
   <state>; doable: <ids>`, the state being one of not in the map, complete, a container, not under the active
   parent, open marker off. `go`, `now` and `wait` use `current`; with none (or once it is no longer a step) the
   picker's choice (`c126_pick`, need_actor false) becomes `current`. The old keep rule is gone.
2. `c126_pick` with need_actor false skips steps with children; `now` prints them as `container <id> count c/t
   marker m`. Autoplay (need_actor true) is unchanged.
3. `go` warps (ring offsets, z = navmesh + half height + 2), then the command runs on for c.ms = 1000 and
   `c284_go_settle` rows each placed pawn from the settled position: `<who> at (...), D uu from target, moved M uu
   since warp, mode K, inside 1|0|-`. Moved over 50 uu, SetActorLocation false, a pawn changed during the settle,
   or (shaped kinds) not inside fails; the first failure is the result. The host view is turned onto the target
   (yaw and pitch from 60 uu over the pawn center) after the warp and again after the settle.
4. Kind `marker`: row `{"ActorReference", Marker}`; stand on its location. `now` prints, for marker and other, the
   nearest `BP_PlayerPresence_C` and whether a host pawn on the stand point would be inside its shape.
5. Zero-actor non-count steps: marker when `c126_marker_of` finds a marker actor whose +0x2B0 is the step, else
   other.
6. `defend` renamed `count` (row above).
7. `objective wait`, `expect` and `go` all `break` out of `c63_start` to `g_c63.until = now + c.ms`
   (testrun.cpp, after the switch) and are polled in `c63_continue`. The drive gate: `split_gates` marks
   `objective expect` with `gates["expect"]`, and `check_gates` turns a failed gated command into
   `VOID cmd <seq>: <detail>` even under try.

C3M2 ids still `other` after 4 and 5 (static): PowerPlant_SO5 (no card 070 class row in the logs). Monorail_SO1 and
Silo_SO1 (0 actors, not count) are marker only if a `BP_ObjectiveMarkerActor_C` points at them at run time; that is
not in the logs, so the first run's `objective now` decides. The Silo_SO3 locks, Silo_SO6 and the HydroDam steps
were never added on the logs (kinds unknown).

5e (`tools/gen_c243.py`): each objective step emits `host objective expect <id>`, `host objective go all`, the
aim-proof shot, `host clear 1500 kill`, the kind's action, `host objective wait <id>`, then the client's replay
row. Every line but the wait is gated on the step's done row (a cascade skips to the wait). Monorail_SO2_A-D: one
expect per member. Presence and marker steps have no action (go places the players inside); use steps press E
on the switch the view was turned onto (a second and third try go again first); count and defend steps run the
kill rounds where `go` put the players. Hand stands and actor names are gone for every objective step except the
actions whose geometry is the action itself, not the stand: the nest blasts (`nest_fire` stand and aim points,
the nuke launcher), the silo clasp fire (no `go` until `now` shows the lock rows) and the dropship boards
(no `go`: the dropship has no kind row). The secret pickup is no objective and keeps its hand stand. Assumed, to
be checked by the first run's `now`: the panel step is Silo_SO6 and the evacuation step is HydroDam_SO6 (both
completed by cascade from their O1). Other maps are not regenerated.

## Senior review of the second delivery (items 1-7 and 5e, uncompiled)

Read: `mod/src/ubw/objective_c284.cpp` in full (530 lines), the `testrun.cpp` cases and `c63_continue` branches,
`ostep`, `owait`, `use_e` and the four area tables in `tools/gen_c243.py`. Items 1-7 are in as asked.

Attacked and held: `expect` on an id that is absent, complete, a container, or not under the active parent (VOID
text names the state); the "kept" pass for the current step with its marker off (needs an open leaf step that was
already current, so a future step cannot pass by it); `go` with a missing host pawn, a missing client, no second
client (`all` skips client2, a named client2 fails before any warp); a pawn replaced during the settle (pointer
compared, old pointer never read); a world change during the settle; `wait` on an id not yet added (waits, then
fails with the state) and on a completed id (completed objectives stay in the map: `c95_complete` reads them
there); the single result buffer (one `c284_fail` per command).

Changes required before the build:

1. Ring slots. `c284_go_start` puts the clients 90 uu to the sides of the stand point without testing the slot,
   and falls back to the stand point without a row. Trace: a presence volume narrower than about 90 uu plus the
   capsule radius leaves a client outside, so `go` fails with "client not inside the presence volume" on a
   correct step, every time. For a stand with a shape, a slot counts only when `c126_capsule_inside` holds for the
   pawn at that slot; try the offsets 90, 270, 180 degrees in turn (the second client takes the next free one),
   then the stand point. Each pawn's settle row names its slot (`slot 90`, `slot stand point`).
2. Presence with no shape. When `c126_shape_find` fails on a presence actor, `go` passes with `inside -` and the
   step then times out with nothing pointing at the placement. `go` fails there: "presence actor has no shape".
   Count steps without a shape stay as they are.
3. Kind `other`. `go` no longer fails on it: it places the pawns as for `marker` (actor[0], else the marker
   actor), and every row carries `kind other`. A failing `go` stops the run at the first unknown kind, and the
   budget is three runs; the prediction "0 ids in `other`" is checked from the log after the run instead. No
   action is derived from the kind.
4. `ostep` emits `host objective now` right after the `expect`. The first prediction (an `objective now` block
   per step) is not produced otherwise, and it is the only source of the class rows item 3 depends on.

Watch item, no change: `c284_doable` accepts a marker-off leaf only through an open sibling with its marker on.
If run 1 shows `VOID objective expect` on a step whose container row prints `marker 1`, that rule is the cause.

## Second review changes 1-4 as coded (junior, uncompiled)

1. Ring slots. A new `c284_slot` places each client on the first free ring slot, tried at 90, 270 and 180 degrees
   from the stand yaw. A slot counts only when it has a navmesh point and, for a shaped stand, `c126_capsule_inside`
   holds for that pawn at the slot (pawn center = navmesh point + capsule half height). The second client takes
   the next free slot. With no slot left, the client stands on the stand point. The host is always on the stand
   point. Each settle row names the slot: `slot 90`, `slot 270`, `slot 180`, `slot stand point`.
2. Presence with no shape. `go` fails before any warp with `<id>: presence actor has no shape`. Count without a
   shape is unchanged.
3. Kind `other`. `go` no longer fails on it. `c284_stand` already places `other` as `marker` does: actor[0],
   else the marker actor, on its location. Every settle row now reads `<who> kind <kind> slot ...`, and the
   pawn-changed row also carries the kind. No action is derived from the kind.
4. `ostep` emits `host objective now` right after each `expect`. Regenerated scratchpad scenario: 758 steps,
   69 shots, 26 expects, 26 per-step `now` (27 matches of the text in all).

## Senior review of the third delivery (changes 1-4): accepted for the build

Read `c284-r3.diff` in full. Attacked: slot test center against the warp center (same navmesh point plus half
height; the warp adds 2 uu up); two clients on one slot (`taken` is shared across the loop); no ring slot inside
a narrow volume (stand point, named in the row); the presence check with a zeroed stand (`C126ShapeKind::None`
is 0); `objective now` after a passed `expect` (current is set, so it cannot return "no current step"). No
finding. Not compiled yet: the build and the three runs wait for the end of the playtest.

## Tool follow-ups from the b7b-2 probe (senior, 2026-11-13; code, no run, next build)

Probe `logs/b7b-2-probe-host.log`: `objective complete LOS_O01_SO01` on FED_C2M1 completed LOS_O01 too, and the level
script never added LOS_O01_SO02, so every later step failed. In real play the script adds SO02 and completes SO01 in
the same tick; a command completion skips the script's own add and ends the chain.

- (a) `objective complete` on a script-chained step logs a WARN that names the skip: the step's completion was
  done by command, so the level script's own next add (for example `AddSubObjective(LOS_O01, LOS_O01_SO02)`) did not
  run. A step counts as script-chained when the script adds its successor itself (C2M1 LOS chain).
- (b) The c126 projection for a presence centre searches the presence volume's full z extent (`C126Shape` ext z),
  not the fixed ±150 uu of `kC126ProjectDz`. The probe found stand none for SO01: the nearest presence,
  BP_PlayerPresence_C_2 at (229051, 41029, 8745), has its walkable floor outside ±150 uu of the centre; the hand
  stand that works is (227245, 39891, 8615) from `logs/c177-r2-host.log:4044`.

## Senior note 2026-11-14 (b12 run 2)

Run 2 void: the 150 uu use-stand cap rejected the nearest navmesh point (189 uu) at `BP_PanelSwitch_WithoutScreen_C`. b13 (2208026de2fee4a3): nearest try wins, no cap; the client `focus=` look gate decides the press. Open tool gap: an SO0 header is listed doable while its presence is dead.

## Senior note 2026-11-14 (b13 run 3): use press needs a focus scan, and the presser on the stand

Run 3 voided at Assault_SO4: stand row as predicted (yaw 189 uu), but `objective go all` put the client on ring slot
90 and the look at the bounds centre gave `focus none`. `TraceForInteractable` (exe+0x4A20780) is a single line trace
from the camera along its forward, range +0xF8, trace type byte +0xFD; the first hit actor must be the interactable.
The switch's interact collider is smaller than its bounds (past logs, same eye: pitch -11.8 focus 2 of 2, -23.x 1 of
3, -33 0 of 2). b14: kind use puts the first client on the stand point and the host on the ring; a `look` with
`focus=` sweeps aim points over the actor's bounds and keeps the centre of those that give the focus. Details in
STATE.md (run 3 entry).

## Senior note 2026-11-14 (b14 probe): focus scan proven at Assault_SO4

`logs/b14-probe-so4-*`: client on the stand point, scan hits 1 of 26 (top centre of the face, pitch 0.6), focus on
the switch, E completed Assault_SO4. Interactor range 250 uu, trace type 3. Margin is thin (1 hit); a site with 0
hits gets a finer second pass around the bounds top first.

## Closed (2026-11-17 13:05): objective steps are the standing test tool

`objective go / now / wait / expect / complete` steps: about 800 across the scenario files, used by every map soak
(cards 243 to 247) and the gate. The use-press focus scan (b14 probe) is in. Coordinate tables are gone from the
soak scenarios (generator `tools/gen_c245.py` reads the live objective).

Reopen: an objective step that places a player away from the live objective's site.
