# Objective routines (card 504)

User rule (2026-11-19): every objective of one type is done the same way on every map ("each bughole gets killed
the same"). A test scenario does not hand-write objective steps. `objective go` reads the live active objective,
picks its type and target, and runs that type's routine below. The routine is code in the driver, shared by all
maps. A per-objective exception is allowed only with a written reason in the exceptions table at the end.

Sources:
- Completion mechanisms: `objective-coop-audit.md` (all 7 Trooper maps) and card 243 probe 3 (nest).
- Driver kinds: `C126Kind` in `mod/src/ubw/hooks_internal.h` (Presence, Use, Nest, Kill, Marker, Count, Other).
  The types below that have no `C126Kind` yet (satchel site, board, defend, pickup) need one.

## Rules shared by every routine

1. **Target from the live objective.** Use the objective's own actor or component. If it has none, use its marker
   actor (`c126_marker_of`). Never use a coordinate table.
2. **Stand, then prove the stand.** Warp to a stand off the target with a clear line to it. Then check that the
   landing is within tolerance (card 504: warp verifies its landing).
3. **Gate every action.** A fire, throw or press runs only when its gate passes:
   - crosshair ray hits the target, or
   - the interactor's focus is the target, or
   - the pawn is inside the target volume.
   A failed gate skips the action and logs `objective go: <type> gate failed: <why>`. Never act blind.
4. **Equipment first.** A routine that needs a weapon, grenade or item makes sure the acting player holds it, and
   takes it or fills its ammo first. An empty heavy weapon is dropped by the game (nuke after its 3rd shot); never
   assume it is still held.
5. **Bugs near the target are cleared by code** (`clear <radius> kill`) before the action, except in fire tests.
6. **Wait on the Complete row** (host `card049`/`card097`), then on the client replay row. Retry only after the gate
   passes again; at most 3 tries, then void with the last gate reason.
7. **All players where the game needs all players.** Presence, board and evac routines place every living player
   (host, client, client2), not only the host.

## Routines

| Type | How the game completes it | Routine |
|---|---|---|
| Nest (bug hole) | `BP_DestructibleSpawner_Bugnest_HumanGameplay_C` dies only to a blast inside its yellow hole volume (`USTHitVolumeGrenadeHoleComponent`); `BuildingDestroyed` delegate | Aim point: bounds centre of the nearest enabled GrenadeHole component (never the actor origin). Stand about 900 uu off with a clear line. Gate: crosshair ray hits the hole volume. Grenade count of 1 or more, else `ammo fill`. Throw a frag grenade (G). Proven: C3M2 nest A, probe 3. |
| Presence / reach | `BP_PlayerPresence_C` enter (some use `IsPointInBox(player0)` or an all-players hold, cards 227/288) | Warp every living player into the volume bounds (inside, not on the edge). Gate: each pawn is inside the box. Hold until the Complete row. |
| Marker | Objective marker with no actor of its own; completes by presence near it | Same as presence, using the marker actor's location and the presence volume it names. |
| Switch / use | `BP_PanelSwitch_*`, door switches; `SwitchPressedDelegate` | Stand at the switch's interact range, facing it. Gate: the interactor focus target is the switch. Press E (hold if the switch is a hold type). For switches the audit marks as a closes-behind (class H), every player is in place first. |
| Pickup | Pickup delegate (`ADPickupDelegate`, weapon or item pickup) | Stand at the pickup and look at it. Gate: the interactor focus is the pickup. Press E. Wait for the inventory or give row. Each player takes their own copy where the objective counts per player (card 495). |
| Satchel site | `BP_SatchelPlacementSite_C` placement, then the building's `BuildingDestroyed` (flak bug, satchel buildings) | Satchel pickup first (pickup routine). Stand at the site, look at it. Gate: the interactor focus is the site. Hold E to place. Move out of the blast radius, then wait for the destroy row. |
| Kill target | A named enemy, or a count of kills (flak bug, BrainBug, Stadium kills A-D) | Kills by code at the target (`clear <radius> kill` on that actor), per the clear-bugs-first rule. A fire test uses the crosshair gate on the target instead. BrainBug: start condition first (C4M2 C_176 presence). |
| Defend / waves / timer | `SetWaves` encounter delegates, `DefendArea`, delay timers (HydroDam_SO5 90 s) | Every player stands in the defend area. Clear the waves by code as they spawn. Wait for the encounter-finished or Complete row. Do not end the waves by command. |
| Board (dropship, mech, transport, evac) | `OnPlayerEntryRequested`, `MechBoarded`, evac presence | Stand at the entry, look at it. Gate: the interactor focus is the entry. Press E. Evac and mid-level transports: every living player boards. |
| Destroyable structure / eggs | `BP_DestroyableActor_*` (`LD: Destroy`), `EggDestroyed` | Damage the actor by the routine its class needs: blast (nest-like) or fire with the crosshair gate. Static trace per class before the first run. |
| Count parent | Parent completes when its parts are done | No action. Run the routines of its parts, then wait for the parent's Complete row. |

## Building blocks that already exist (senior trace 2026-11-19)

Most routine parts are built and proven one by one. The C3M2 soaks failed because their scenario lines did not use
them (c243b283a fired the Morita at nest C with no gate; `key LeftMouseButton 6000`). Build the routines by composing
these, not by writing new aim or stand code.

| Part | Existing piece | Where |
|---|---|---|
| Live step, kind, stand point | `objective now` / `expect` / `go` (stands per kind: use face stand, pickup stand, presence shape, actorless and marker presence tries, hub presence) | `objective_c284.cpp` `c284_stand`, `c284_go_start` |
| Kind of a step | `c126_kind_of`, `kC126Kinds` (Presence, Use incl. WeaponPickup, Nest, Marker, Kill, Count, Other) | `autoplay.cpp:50`, `:149` |
| Nest aim point | `look actor <name> [dz] hole`: nearest owned component whose class has `GrenadeHole` or object name `GrenadeDamageVolume` (b232/b233) | `testrun.cpp` `c64_hole_point` |
| Grenade supply | `host grenade fill <n>` (b234) | drive.py help line 23 |
| Throw | `key G <ms>` | b39-c353 scenario lines 849-853 |
| Hit gate | `hit=<prefix>` on a look (card 264); the step after is skipped on a void | drive.py `GATES`, line 281 |
| Focus gate for E | `look ... focus=<prefix>` with the b14 focus scan and sweep | testrun.cpp b14, drive.py line 32 |
| Press reach | `c284_step_in` (eye within the 250 uu interactor trace) | `objective_c284.cpp:1158` |
| Satchel site readiness | `c284_site_ready` (a player whose interactor passes CanInteract, so carries a charge); `objective next` skips a site no one can work | `objective_c284.cpp:1052` |
| Bugs by code | `host clear <radius> kill [max-left=N]` | card 265 |
| Completion | `objective wait <id> [s]` | `c284_wait_step` |

Gaps that need new code:
- The routine dispatcher itself: one command per step that runs stand, equipment, clear, aimed look with its gate,
  action and wait for the kind `objective now` reports.
- Kind rows for satchel site, board (dropship, mech), destroyable/eggs, and defend; these now fall to Other.

Recommended shape: the dispatcher in drive.py (`objective do [<id>]`). It reads the `objective now` row for kind and
actor name, then emits the existing gated steps above. No new DLL aim code, so no build is needed for the nest,
presence, use and pickup routines. DLL work only for the missing kind rows.

## Lessons learned (each from a run; apply before every scenario or routine change)

Every routine change, scenario edit and run review checks this list first. A new lesson goes here the same day.

1. **Know each step's kind from logs before routing it** (c504b1 review). Grep past host logs for the step's
   `objective now` rows (kind, actor[0] class) before writing `objective do <id>`. A guess of "presence" was really
   kind marker on 2 of 4 steps, which would have voided.
2. **Old scenario regexes go stale** (c504b1). Before reusing lines from an older scenario, check every `wait log`
   regex against the current DLL's log text (grep the source `log::line`). `warp: own pawn ... ->` became
   `warp: landed at (...)`; one failed wait capped every later wait in its group to 1 s, and the gate it carried
   (arrival) was lost, so the host took the wrong pickup and ended 1 satchel short.
3. **Gates read whole lines** (c504s2). `wait_log` detail is cut at 160 chars; read fields from `last_groups`,
   never by re-matching the detail.
4. **A retry first checks whether its step completed** (c504s2). Fuses and scripts complete late; a try after the
   Complete acts on the next step's target.
5. **`objective do <id>` runs only the named step** (c504s2). An already-complete id is a no-op pass; a different
   current step voids. Never let one line run another step's routine.
6. **The focus scan aims at the actor's whole bounds** (c504b3, corrects c504b2). `look ... focus=` traces to the
   bounds centre and 26 bounds points within the 250 uu interact range. BP_BaseMech_C bounds are inflated and off
   the body (centre about 1850 uu SW of it), so only a stand whose line to that centre crosses the body can focus.
   Stand distance was not the cause. Before a focus gate on a new class, compare its bounds centre with its origin.
7. **Find the cause in the log before naming it** (c504b2, c504b3). Two wrong causes in a row (drop, then range),
   both accepted by the senior. Quote the row that proves the cause, and check that it explains every try, the
   passing ones too.
8. **Actorless steps** (AddSubObjective `actors 0`, kind other, no actor[0] row) are presence steps on C2M1
   (LOS_O02_SO01, LOS_O03_SO01): the go's c284 actorless stand puts players in the presence volume.
9. **Pickups are consumed** (c504b1). Satchel pickups are shared, one charge each; a plant step needs one take per
   plant, gated per pickup by its own sub-step Complete.

10. **A kind label does not say what completes a step** (c504b4 trace). On C2M1, kind marker LOS_O01_SO01 completes
    on its wave end (card503 wave end, then native Complete in the same ms as the clear), and kind marker LOS_O04_SO02
    completes about 58-70 s after both players enter, through its parent (script caller). Kind other
    LOS_O03_SO02a-c are satchel pickups, not sites. Before routing, read the cause in the Complete row (wave end,
    script caller, take, presence enter), and check that a class override (for example "Satchel" in the class) cannot
    catch a different kind of actor.
11. **Gate focus on the instance when same-class actors sit close** (c504b6). The satchel-take gate used the class
    prefix `focus=BP_SatchelItemPickup_C`. On LOS_O03_SO02a try 1, the look at `_2` had focus `_3` (cmd 92). The gate
    passed, and E took `_3`, which completed LOS_O03_SO02c instead of SO02a. When pickups or sites of one class stand
    within the interact range of each other, the focus gate names the instance.
12. **Bounds also mislead the step-in, not only the focus scan** (c504k1, card 512). On FED_C4M1 Stadium_SO1_B–D
    (BP_PanelSwitch_WithoutScreen_C), the panel's bounds face was 100 uu away in 2D, so the b257 face gate skipped the
    step, while the eye was 369 uu from the switch, past the 250 uu reach. Every press missed, and 519 s were lost.
    Gate a move on the distance the game itself checks (eye to target), never on a box built from whole-actor bounds.
    Lesson 6 covers every helper that reads bounds.
13. **A guide marker is not the step** (c504k1). Bridge_Marker and Bridge_SO1 were guide markers; the hidden
    Bridge_SO2 was closed by presence `_12`, 13086 uu from the marker. A presence try that is not the last one moves on
    after its wait; only the last try runs the stay phase.
14. **Grep the host and client logs for `skipped:` after every run** (c504k1, card 513). A routine sent
    `clear 1500 kill max-left=99` raw; the DLL skipped it all 5 times, and the plant ran with bugs alive. Gate words
    (`max-left=`, `focus=`, ...) are stripped only on scenario lines, never on a routine's raw `send()`.
15. **Measure idle time on every run** (`tools/idle_report.py <tag>`). c504k1 spent 1109 of 1268 s waiting between
    commands. Each run report carries that output, and each wait without a game reason is a cut.
16. **A step can end the level with no Complete row** (c504k2). On FED_C4M1, Metro_SO2 (kind marker) is closed by
    presence `BP_PlayerPresence_C_70` at (-7489, -97839, 830), 7722 uu from its marker. Its overlap opens
    `WB_TrooperLevelEnd` at once, and Metro_SO2 and Metro_O1 never log a Complete row. The loop keyed its end on the
    Metro_O1 Complete row, so it ran 363 s past the level end: 125 s of stay, 39 capped rounds, and 150 s of
    FED_C4M2 waits that nobody pressed through. End the loop on the level-end widget row as well
    (`card018: ... WB_TrooperLevelEnd`). Then press the level-end continue and wait for the next map.
17. **Some marker steps close on a script timer longer than the stay cap** (c504k2). Stadium_SO2 closed 173 s after
    its Add, about 3 s after its VOID; Stadium_SO4 closed about 13 s after the Stadium_Marker VOID. Both closes came
    with a `StartEncounter` wave row, not with a player move. The presence was right; the routine gave up too early.
    Before raising the cap, take the timer from the level script.
18. **A step can close on a script flag that a presence volume sets** (c504k2, senior kis.py trace 2026-11-19).
    Stadium_SO2 has no timer and no encounter gate: FED_C4M1 ubergraph 8598 `if not (inStadium) goto 8891`, and 8891
    retries `CallFunctionWithDelay(10.0, 'Add Stadium_SO4')`. inStadium is set by BP_PlayerPresence_C_5's enter event
    (5729 -> 5911) and cleared by its leave event (5937 -> 6119). The k2 95 s of retries were players outside C_5. A
    routine whose step hangs on such a flag must stand in the volume that sets it; the stay alone cannot close it.
    Static source: `tools/kismet/kis.py` (README there); a `CallFunctionWithDelay` retry means a flag, not a timer.
19. **Gate a routine change to the step kind it was proven on** (c514a, senior 2026-11-19). The card 514 home go was
    written for presence steps but ran for every kind the routine serves, so the guide marker Bridge_Marker (it closes
    only with its parent) sat 311 s at its try-1 stand. Before a go, list every kind and step the changed routine
    serves and say why each is safe, or gate the change (card 516).
20. **A fight look counts only at fight range** (c263b, senior 2026-11-19). The host's `look actor
    BP_AI_WarriorBug_C` passed in 12 of 12 rounds, but at a warrior 15606 uu away; the client had none in range
    ("no such actor"). A look that succeeds is not a close fight. Read the distance in the look row (`... , N uu`)
    and treat a look over 3000 uu as a failed look; place the players at the bugs first (`spawn bug` near the players,
    or a warp to the nearest bug).
21. **An actorless step with no sibling marker can close far from the host** (c247b288a, senior 2026-11-19). On
    FED_C5M2, Assassin_SO2 closes on entering BP_PlayerPresence_C_6 (card 247 row S2), about 89000 uu from the host.
    The three nearest-presence tries never reached it: 50 failed waits, and the soak ended without Finale. Before a
    soak, check each actorless row in the map's script table: if its presence is not near the host or a sibling
    marker, add a `kC284PresenceHints` row (objective name, presence name; `objective_c284.cpp`). The go logs
    `c284: actorless step stands at hinted presence`.
22. **A damaged nest can let the look ray through** (c247b288a line 503). The client's look at nest C_9's hole from
    202 uu, pitch 11.6, logged `aim: no hit within 10000 uu` while the nest was at 397 health (it died 9 s later
    to a later grenade). The hit= gate then skipped the G. On a nest already hit, a no-hit from inside the hole's
    `within` range is not a miss of the nest; it costs a retry, not the step.
23. **A marker inside a presence stands in the presence** (c247b292a, senior 2026-11-19). FED_C5M2 Finale_SO0's
    marker ActorReference_C_8 sits at the centre of BP_PlayerPresence_C_23 (0 uu), and the script completes the step
    on entering C_23 (card 247 row F0). Try 0 stood on the marker's navmesh point 532 uu off, outside C_23; tries 1
    and 2 skip the nearest presence as already tried, so C_23 was never entered and the soak ended in Finale. Now
    `c284_stand` try 0 stands inside a switched-on presence within 1000 uu of a bare-location marker whose shape
    holds the marker (log `c284: marker ... lies inside ...; stand in the presence`). When a marker go logs
    `inside 0` and the `now` row says `nearest presence ... 0 uu, stand inside 0`, this is the case.
24. **A guide marker ends when the sibling that shares its actor completes** (c263e line 46, also c263b and c263d;
    senior 2026-11-19). FED_C3M2 PowerPlant_Marker and PowerPlant_SO1 share ActorReference_C_1. SO1 completed when
    the pawns entered BP_PlayerPresence_C_52; about 16 s later the script hid the marker and showed SO2..SO6. The
    marker never gets a Complete call, so a wait on it ran out and the run went VOID. Now `c284_wait_step` ends a
    marker wait once a completed sibling under the same parent has the same ObjectiveActors[0] (log `c284: objective
    wait <id>: guide marker ended by <sibling>`). The marker can stay "current" until the script hides it, so the
    next step waits on the next sub-objective (SO2), not on the current one.
    Amend (c263g, senior 2026-11-19): the script hides the marker and shows SO2..SO6 only after the players enter
    another presence volume (BP_PlayerPresence_C_16, by the marker routine's try 3; host lines 17555-17596, all in
    one ms). So `routine_presence` (drive.py `marker_held`) treats a marker wait that ended on its sibling as done
    only when a `SetObjectiveVisibility(<marker>)` row follows within 20 s (`MARKER_HIDE_S`); otherwise it goes on
    to its next presence try. This replaces the c263f stopgap (a second `objective do PowerPlant_Marker` in the
    C3M2 scenario).
25. **A presence stand counts only inside the shape** (c247b295a host lines 62110-62113 and 85773-85775, senior
    2026-11-19). Lesson 23's fix did not fire for Finale_SO0: the marker C_8 lies at (-65345, 50747, -2995), 404 uu
    from C_23's centre and outside C_23's shape (lesson 23's "0 uu" was the presence row measuring to the stand's
    target, which a presence stand sets to the shape centre; it is not the marker distance). The marker had no
    navmesh point either, so the off-navmesh branch stood in C_23 via `c284_presence_stand`, whose centre point
    projected to (-65280, 50864, -3282), 513 uu off the centre and outside the shape. That function took the
    centre point without an inside check, so go logged `inside 0` and tries 1 and 2 skipped C_23. Now the centre point
    counts only with the capsule inside; then the ring points, then the static floor under the centre off the
    navmesh (`presence centre (off the navmesh)`), and the outside centre only as the last resort. Lesson 23's test
    is nearness alone (presence centre within 1000 uu of the marker), logged as `lies inside` or `lies near`.
    Rule: any stand that is meant to enter a presence checks the capsule against the shape before it is accepted.
26. **A hidden next objective is shown by a presence entry, not by a timer** (c263f line 51, senior 2026-11-19;
    corrects lesson 24's "about 16 s later the script hid the marker"). FED_C3M2 shows SO2..SO6 and hides
    PowerPlant_Marker in the same tick as a pawn enters a later presence: in c263e BP_PlayerPresence_C_16 at
    (-18750, -5950, 1150), entered by the marker step's try 3 stand (show row 15446462, overlap row 15446463); in
    c263f BP_PlayerPresence_C_2 at (-62225, 4852, 3113), entered by the fight round's first warp (show 16855948,
    overlap 16855952). The show row logs 1 to 4 ms before its begin-overlap row. In c263f lesson 24 ended the marker
    wait at SO1 Complete, so no try 3 ran, nobody entered a presence, and the SO2 wait timed out (60 s); the 91 s
    gap was the scenario's own idle time, and a longer timeout cannot pass it. So the guide marker is done only when
    the script hides it (`SetObjectiveVisibility(<marker>) bools 10`), and until then the routine keeps standing in
    the marker's presences it has not entered. Rule: a hidden objective's show is a trigger to find (the row just
    before it, the overlap row just after), never a wait to lengthen.
27. **An actorless step with no sibling marker needs its script trigger named** (c247b297a, b297, senior
    2026-11-19). FED_C5M2 Assassin_SO3 has no actor, no marker actor and no sibling marker, so the nearest-presence
    try stood at BP_PlayerPresence_C_617 (-9076, -929), 38284 uu off (host line 57916). The script completes SO3 only
    when SetWaves C_7 ends (@8157 -> @8230), and C_7 starts on entering BP_PlayerPresence_C_21 (-75484, 50616)
    (@8152, card 247 row S3); C_21 has no overlap row in the whole run. SO3 stayed open for 260 s of 3 s waits,
    Assassin_O1 never completed, Finale_SO1 and SO2 were never shown, and the level did not end. Fix: the
    `kC284PresenceHints` row `Assassin_SO3 -> BP_PlayerPresence_C_21` (as Assassin_SO2 -> C_6, c247b288a). Rule:
    before a soak, every actorless step without a sibling marker has a hint row from the static script trace; the
    nearest-presence tries are a fallback only, and a step whose tries all fail gets a trace, not more tries.
28. **A mission end on a transport is not an objective step; the scenario end boards it** (c247b302a, b302, senior
    2026-11-19). FED_C5M2 Finale_SO7 completed and `objective now` read current none, but Finale_O1 completes only
    when a player enters transport BP_PlayerTransportDropship_C_2 (PlayerEntering @13949). It landed 3.5 s after SO7
    (DescendToLandCompleted flag 0 -> 0) and nobody boarded, so the level never ended. The quit also left out every
    map-end row (card523, card521 client). Rule: a map whose end is a boarding ends its scenario with the landing
    wait, the gen_c243 `board()` recipe gated on the O1 done row, then the level-end travel's card330 rows (or a
    `host travel restart`) before quit. Every scenario that needs map-end rows travels before it quits.
29. **A wait that is done without a Complete gets no client replay wait, and moves on** (c247b303a, b303, senior
    2026-11-19). `objective wait` returns done when another step that shares the guide marker's actor completed
    ("c284: objective wait Outpost_SO4: guide marker ended by Outpost_SO2"). The host then sends no Complete for that
    id, so the client's `replayed Complete` wait timed out (8 s each, six times), and the step stayed current for 50
    waits. Rule: the client replay wait runs only when the block's last host wait was done and no marker-ended row came
    after the block's `objective now` row.
30. **A marker step is a guide only if its sibling was open while it was** (c247b307a, b307, senior 2026-11-19).
    Lesson 29's `objective next` on a marker-ended row sent Outpost_SO4 round the loop faster (25 blocks, 121
    marker-ended rows) and the blocks ran out at Array. Outpost_SO4 shares Outpost_SO2's actor, but SO2 completed
    before SO4 was added; SO4 has its own trigger (both players in C_402, the C_5 waves cleared). Fix in
    objective_c284.cpp: a wait counts "guide marker ended" only when a wait saw the sibling still open while the
    marker was open. Rule: before moving a scenario past a step, check the step's trigger row in the card table; a
    "done" with no Complete row is not a pass.
31. **A presence try never stands in another step's presence** (c247b308a, b309, junior fix, senior 2026-11-19).
    Array_SO4's try 3 had no presence left near its marker and picked BP_PlayerPresence_C_17, 52826 uu off: Assassin_SO1's
    trigger. Its entry ran before Assassin_SO1 was added (CompleteSubObjective flags 0x200), C_17 switched off, and
    Assassin_SO1 could never complete. Fix in objective_c284.cpp: a next presence over 20000 uu
    (kC284NextPresenceMaxUu) from the marker actor is not entered, and the step stays open. Rule: a fallback stand
    must stay in the step's own area. A Complete row for a step that is not added yet means an earlier step's stand
    walked into its trigger.
32. **A zone guide hands its marker on; read the visibility rows** (c244b319a, b319, senior 2026-11-20). On FED_C4M1
    the script hides Bridge_Marker and shows Bridge_SO1 while the players stand in the bridge zone (and Stadium_Marker
    with Stadium_SO1..SO4), and flips them back as they leave. Neither guide completes on its own, so three waits on
    it are wasted. A marker hidden while a sibling's shows means the step was handed on (card 516 fix 3,
    `c284_handed_on`). Before calling a marker step stuck, grep `SetObjectiveVisibility(<step>)` and its siblings.
33. **A gate change must also cover the gate-failure path** (c244b319a). Card 516 fix 1 limited the home go to kind
    presence and dropped Stadium_SO2, a kind-marker flag step that needs it (lesson 18). Its try 3 go gate also failed,
    and `continue` skipped the home go and the stay, so it went VOID 35 s before it closed natively. When a routine
    change narrows who gets a phase, list the steps that needed that phase (lesson 19), and check what a failed go
    does on the last try.

34. **A tutorial step's kind does not say what completes it; give it a recipe** (soak1m1b366a, b366, senior
    2026-11-20). TacSupport_SO3 is kind other with actor[0] BP_EncounterManager_C, so `objective do` had no routine,
    VOIDed twice and was capped for the rest of the loop: 7 of 18 rows. SO4-SO7 are kind marker but need a tac call,
    a tac pickup or a flare, and ShootingRange_SO3 counts ten range targets. A step with `tools/recipes/<step>.txt`
    now runs those lines (the proven c335-range-load slice, written by `tools/gen_recipes.py`) in place of its kind
    routine. Before a soak of a new map, list each step's kind and actor[0] from a `objective now` row and check that
    a routine or a recipe covers it.
35. **One post is one DLL command; scenario words like `timeout=` are drive words** (soak1m1b366a). routine_pickup
    posted `goto actor X 120 timeout=30`, five words, which the DLL parses as a coordinate goto and skips ("bad
    coordinate or radius"). The pickup still landed from the go stand. A routine that walks calls run_step with the
    goto, then `wait goto timeout=S`.

## Proof status

| Type | Status |
|---|---|
| Nest | Proven (card 504, run c504n2, DLL 4c265abd554c8232): `host objective do <id>` in tools/drive.py. C3M2 Monorail_SO2_A–D all complete on try 1. Each nest was destroyed about 2.7 s after the throw, and the client replayed every complete. Hit-to-hole distance was 29–68 uu, at 351–447 uu range. In 3 aim shots the crosshair is on the yellow hole. In shot B it is on the rock edge at the left of the glow, and the nest still died at +2.8 s. Open item, no run for it now: a stand that leaves the hole half occluded (B, pitch -29.7). |
| Presence, use, pickup | Proven (card 504, run c504p1, DLL 4c265abd554c8232, FED_C1M1): `host objective do <id>`. Presence Assault_SO2 and SO3 and use Assault_SO4 (the client pressed E on BP_PanelSwitch_WithoutScreen_C) each completed on try 1 and the client replayed them. Pickup TacSupport_SO2: the host took it at 394408 (1 of 2), the client at 396063 (2 of 2), and the Complete came at 396070, only after both (card 495). 0 VOID, 0 FATAL/EXCEPTION. The SO4 and pickup shots were viewed. Note: TacSupport_SO2 has no AddSubObjective row; it shows by `SetObjectiveVisibility`. |
| Satchel site | Proven (card 504, run c504s3, DLL 4c265abd554c8232, FED_C4M1): `host objective do <id>` (routine_satchel; the client plants). Park_SO1_C, Park_SO1_A and LZ_SO1 completed on try 1, Park_SO1_B on try 2; the client replayed all 4. B's try 1 hold was interrupted at progress 1.88 (completed 0); the gate caught it and try 2 planted. Each Complete follows a client card133 "own hold completed ... HoldDone sent" row by about 3 s (the fuse). The Park_SO2_E–A nests passed in the same run, try 1 each. 0 VOID, 0 FATAL/EXCEPTION. Fixes before the pass (c504s2): the hold row's "completed N" is read from `last_groups` (wait_log cuts its detail at 160 chars); a retry first checks its step's Complete row (a late fuse otherwise sends the next try to another step's site); `objective do <id>` returns at once on an already-complete id and voids if the current step is not `<id>`. |
| Board (mech) | Proven (card 504, run c504b6, DLL b284 62e2e778930a4dd6, FED_C2M1 RallyPoint_Marker): `host objective do RallyPoint_Marker` (routine_board; the client boards) complete on try 1, client replayed. The focus came through the card 510 fallback: `aim scan: fallback origin hit, root CapsuleComponent`. BP_BaseMech_C whole-actor bounds sit about 1850 uu off the body (lesson 6). The c504b2/b4 passes were luck of side; c504b3 VOID. BOARD_DISTS (250, 200). |
| Marker (kind marker, actor[0] `ActorReference_C`) | Proven (card 504, DLL 4c265abd554c8232). routine_presence: go, presence wait 10 s, then a stay phase (`clear 4000 kill` every 5 s, cap 120 s) for markers that complete on a wave end or a hold. c504b5: LOS_O01_SO02, LOS_O02_SO02 try 1 (presence); LOS_O01_SO01 try 1 (stay, 26 s, 6 clears; wave end); LOS_O04_SO02 try 1 (stay, 52 s, 11 clears; hold, completed by its parent). Actorless kind other (LOS_O02_SO01, LOS_O03_SO01) proven as presence-actorless (c504b4, c504b5). Satchel pickups LOS_O03_SO02a-c: routine_satchel_take is written but waits on card 511 (not doable under a marked container). |
| Recipe (tools/recipes) | Written, not yet run (senior 2026-11-20): C1M1 TacSupport_SO3, SO4, SO41, SO5, SO6, SO7, ShootingRange_SO2, SO3, from c335-range-load lines 194-1000 (that scenario proved them by hand). The next C1M1 soak is its proof. Hand-written (gen_recipes.py LITERAL): C4M2 BrainBug_SO3, SO5, SO7 (c502-c4m2 boss block, c502b280a) and C5M2 Finale_SO1 (clear rounds, c247b311a); proof is the C4M2 and C5M2 soaks. |
| Everything else | Not yet built as routines. The past soaks did these steps by hand-written scenario lines. |

## Exceptions (per objective, with reason)

None yet.
