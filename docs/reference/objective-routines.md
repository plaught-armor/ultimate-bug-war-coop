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
36. **The now row's actor[0] first word is a name stem, not always a class** (soak3m1b366a, b366, senior 2026-11-20).
    FED_C3M1 Beach_SO1_A-C print `actor[0] BP_Switch_C (BP_PanelSwitch_WithoutScreen_C)`: the instances are named
    BP_Switch_C_4/_0/_6 but their class is the panel switch. `look actor` matches an exact name or an exact class, so
    `look actor BP_Switch_C` found nothing on the client (try 1-3 "no such actor") and the soak did 0 of 18 rows.
    routine_use now looks at the step's own instance from its card070 row (step_instance), as the satchel take does.
    A routine that looks at an actor by the now-row word must use the instance or the parenthesized class.
37. **Every routine with a go gate needs the same settle hold** (soak3m1b366b, b366, senior 2026-11-20). With lesson
    36 fixed, Beach_SO1_A completed, but the Beach_SO1_B stand settles 59 uu after the warp on every try. routine_use
    failed the go gate on that drift (3 of 3 tries, 48 rounds in 40 s), while routine_presence already held drift up
    to STAY_LEFT_UU. routine_use now holds drift up to USE_DRIFT_UU (200 uu) and lets the focus gate decide. A new
    routine that calls step_go must apply a drift hold too.
38. **A step's kind does not say what completes it; check the level script before trusting the kind routine**
    (soak3m1b366c, b366, senior 2026-11-20). C3M1 Resort_SO2 is kind marker, but the level script completes it on the
    third satchel pickup (card 242 row 8). The presence routine stood at its marker for 309 s with no Complete row,
    and the 600 s run cap ended the soak. Resort_SO2 completed only when the Resort_SO3 satchel routine took the third
    pickup. Resort_SO2 now has a recipe (gen_recipes.py LITERAL) that takes the live pickups. Any step whose
    completion is a count or a pickup, not a stand, needs a recipe or a routine of its own.
39. **The look scan's grid must cross the target's body, not its bounds face** (soak5m1b367b, b367, junior trace
    2026-11-20). The interactor is one 250 uu line trace from the eye, and a switch's interact collider is smaller
    than its bounds. From an eye 32 uu outside the bounds face (Opening_SO3), the face grid hit the panel at 1 of 26
    points and focus stayed none. ExcavatorB_SO2 BP_Switch_C_2 failed the same way. b368 samples the plane through
    the centre from either side. A use failure with an aim hit inside 250 uu and focus none is a scan miss, not reach.
40. **A client tac throw needs the charge on the host's server copy** (c559b371a, b371, junior 2026-11-21). The
    client's own key runs only the local throw (card 100, 174); the host's server copy runs `HandleTacticalSupportInput`
    with the copy's own charge. `client charge 0 20000 1` filled only the client's own pawn, the host logged
    `charge 0 -> 0`, no flare, no drop: the whole run void. Client throws use `host charge client <slot> 20000 1`, then
    `client wait log "card104: client t=\d+ TacCharge " since-mark` before the key (the b11 pattern). Host throws use
    `host charge <slot>`.
41. **Shake and other per-landing tests alternate jumps and wait on the landing row** (user correction for card 551,
    2026-11-21). Jumps on a sleep timer, or both players jumping in the same block, give no time window that belongs
    to one landing, so a watcher-side shake row can't be pinned to the other player. One player jumps, waits for its
    own `card490: <side> t=\d+ pc 0 BP_PlayerCharacterMI_C role <3 host, 2 client>: .*MovementMode 3` (airborne), then
    the same row with `MovementMode 1` (landed), both since-mark; a 1 s quiet gap; then the other player. Marks on both
    sides before each jump bound the window. Players about 350 uu apart.
42. **Copy the whole preamble, up to the first landing wait** (c560b371a, b371, junior 2026-11-21). The C3M1 run copied
    soak-c3m1.txt lines 1-21 and dropped line 22 `host wait state pawn=host-own timeout=90`: the host still sat in the
    transport dropship (about 47 s to landing), so the client pickup warp found no standing candidate, the whole 560
    block voided, and the host's `shot` read the dropship pawn as a trooper and crashed the DLL in `c264_aim_rows`
    (fixed by `pawn_interactor`, class guard). On maps that start in the dropship, the first step after `failsoft` is
    `host wait state pawn=host-own`.
43. **Copy wait regexes from a real log row, never from memory, and never wait on a capped row** (c560b371b, b371,
    junior 2026-11-21). Two waits failed on events that did happen. `GiveWeaponByClass\(TurretMorita\)` missed
    `GiveWeaponByClass(BP_WeaponDefinition_TurretMorita_C, ...)`, and the card303 possession rows stop after 8 per
    run, so the missile-to-trooper return had no card303 row. Grep a past log for the row first and paste its text. For
    pawn changes, wait on the uncapped `client-input: SetPawn(... <class>)` or `possess: AcknowledgePossession` rows.
44. **Quote every `wait log` regex** (c551b376a, b376, junior 2026-11-21). The 40 card490 jump waits had no quotes.
    The regex split into words, so each wait failed in about 0 s and the 20 jumps ran about 1.2 s apart instead of one
    landing at a time. `--parse` still reported 0 bad, because it compiled each word on its own. `lint_step` now
    rejects a `wait log` that does not hold exactly one regex. Run `--parse` after every scenario edit.
45. **FED_C4M1 start mechs stand on raised pods** (c558b376a, b376, junior 2026-11-21). Mech A sits at about z 1347
    on its pod, and a warp near it lands the watcher on the pod too. When the pilot walked off, the mech fell to about
    z -660 and left the client's view, so the mech shots showed sky. Walk the mech off the pod first. Wait for its
    card490 `MovementMode 3` row and then its `MovementMode 1` row. Only then warp the watcher. The warp result row
    reads `cmd: N warp: landed at (x, y, z) after N candidate(s): ...; own pawn from` (or `server copy from`). Lesson
    43 applied: 8 FAIL lines came from warp regexes written from memory.
46. **FED_C1M1 starts unarmed** (c551b378a, b378, junior 2026-11-21). The players hold no weapon until the "Collect
    your weapon" pickup, so the card 566 range-target fire made no holes and the checkpoint replay went untested.
    Any C1M1 step that fires needs a pickup first. Each player takes BP_WeaponPickup_MoritaMk1_C_0 by their own E
    press (cards 495, 559). Wait for the host `card495: host t=N held Pickup_Implementation on ..._C_0 (take): taken
    by 1 of 2` row after the host's E, and for the `released ... by take: taken by 2 of 2` row after the client's E.
    Both regexes were copied from soak5m1b367b, per lesson 43.
47. **Diag row caps run out before the step under test** (c551b380a, b380, junior 2026-11-21). The card 551 rows
    (16 ExecuteImpact, 24 StartCameraShake per side) were all spent on warp landings and the walk to the pickup, so
    the 10 alternate jumps logged nothing. Before a run, count how many matching events come before the step a capped
    row is meant to cover. If they exceed the cap, ask for a mark-reset or a larger cap, or move the step first. Also:
    `goto actor BP_WeaponPickup_MoritaMk1_C_0 100` from the C1M1 start fails ("stuck on the leg past the navmesh
    path's end"), but the E press from where the player stops still takes the weapon.
48. **Chained runs must stop when one is skipped** (c569b383a, b383, junior 2026-11-21). A back-to-back launcher
    gave each run its own 10 min GPU wait. When the GPU stayed at 100 % past the first wait, that run returned
    without launching and the second ran anyway, so the order was lost and the skip only showed in the summary.
    Chain runs with `&&`, or check each launch line before trusting the summary. Also, a new build can fail on
    load (b381: `FATAL hooks: 513 hook rows, room for 512`). The drive then fails at `cmd: polling` after 120 s;
    read the live log's `worker:` row before blaming the scenario.
49. **After a shared pickup, the partner's pawn blocks the aim** (c569b383a, b383, junior 2026-11-21). Both players
    end the Morita take next to the range table, 51 uu apart. All 3 client `look ... hit=GrenadeRangeTarget_C_1` gates
    voided on `aim hit BP_PlayerCharacterMI_C` (the host pawn), so every fire and throw was skipped. Before a
    `hit=` look, warp the shooter to its own spot (`host warp client <target> 1200`) so no player stands in the line.
    Correction (c570b386a): that was not the main cause. BP_KillableBuilding_Base_GrenadeRangeTarget_C_1 sits at
    (5341, 23929, -494), 12.6 km from the range table, and stays sunk below the ground until ShootingRange activates it
    (`card065: host ... Activate(1) ...GrenadeRangeTarget_C_1`). Even with a clear line (1023 uu) the look found no hit.
    A grenade round needs that Activate row first, and the shooter must stand at the ShootingRange firing spot.
50. **Pickup steps: let the route do the overlap, and warps far from navmesh drop the pawn** (c570b386a, b386, junior
    2026-11-21). The Morita `goto ... 100` walks both players over all 4 range pickups next to the table. Every card570
    credit row was logged during that walk. Each `goto actor <pickup> 20` afterwards stopped 118-157 uu short ("goal
    off the navmesh"), so it never overlapped. Wait on the whole log after the Morita goto, and re-walk the same route
    for a repeat overlap. Rows print `(host-own)` and `(server-copy)`, with hyphens; check row text in a log before
    writing a regex. `host warp client <actor> 1200` with no seeing navmesh point falls back to SetActorLocation. At
    the grenade target that left the client at z -377, and it then fell to z -13796. Treat a fallback row as a failed
    placement. Tac pickups: a warp ring of 150 had no navmesh point that sees the pickup, and the look hit
    StaticMeshActor_5 at 222 uu. Use `goto actor <tac> 60` as b14-soak-c1m1.txt does.

51. **Morita gotos from a far spawn stop short, so the walk-over does not reach every range pickup (c570b389a).**
    All three `goto actor BP_WeaponPickup_MoritaMk1_C_0 100` steps failed with "stuck on the leg past the navmesh
    path's end". The client's route still credited Frag_C_0, Incendiary_C_0 and MoritaSmall_C_0, but never
    MoritaSmall_C_1, so the C_1 server-copy and "released 2 of 2" waits failed. A pickup that must be proven per
    player needs its own approach: a warp to 400 uu from that pickup, then a short goto through it. Do not rely on
    the table route for the far pickup.

52. **A sub-objective's AddCount row is not a progress guard (c569b390a).** On FED_C1M1 the host sends `AddCountSubObjective(ShootingRange_O1, ShootingRange_SO3)` at map start ("objectives now 4"), long before the grenade range is reached. A soak guard on that row skipped every round, and the run failed at the SO3 recipe. Guard on the previous sub-objective's completion row instead (`card049: host sent CompleteSubObjective(ShootingRange_O1, ShootingRange_SO2)`), and check a candidate guard row's first time in an earlier host log before using it.

53. **The G key throws the grenade in ability slot 0, which is not always the frag (c569b390b).** After the range pickups the client held one Incendiary and one Frag; its first G press threw the Incendiary (`GA_IncendiaryGrenadeThrowingAbility_C`), and only later presses threw Frags. A step that waits on Frag rows must first spend or not take the Incendiary, or accept either `Grenade_Throw_(Frag|Incendiary)` key. Also check that a per-row diag still has budget left at the step: card 569's DoAction rows stopped at 48 per side during the earlier Morita range fire, so the throw rounds logged none.

54. **A step the level script hides stays `current` until a test moves off it (c569b390b, soak1m1b366c).** On Assault_O1 entry the FED_C1M1 script hides Assault_SO0 (`SetObjectiveVisibility(Assault_SO0) bools 00`), adds Assault_SO1 and Assault_SO2, and completes Assault_SO1. Assault_SO0 is never completed. The DLL keeps `current` sticky so that a marker blink is not a failure, so `objective now` went on naming Assault_SO0 (marker 0) while its doable list held only Assault_SO2 (marker 1). The presence routine then stood in the hidden volume for 3 tries and the line went VOID. drive.py `objective do` now handles this: when the current row ends in `marker 0` and a `doable <id> ... marker 1` row exists, it sends `objective expect <id>` and reads `objective now` again before it picks the routine. ShootingRange_SO0 follows the same hide-and-add pattern.

55. **Entering a step's volume can make the script hand the step off instead of completing it (c569b392a).** The first `objective go` into the Assault_SO0 volume (marker 1 at that point) made the FED_C1M1 script hide Assault_SO0, add Assault_SO1 (the same presence C_2) and Assault_SO2, and complete Assault_SO1, all within 2 ms. Assault_SO0 never gets a Complete row, so the presence routine waited 10 s, stayed, and went VOID. drive.py `handed_off` now treats it as done when a `SetObjectiveVisibility(step) bools 00` row is followed within 50 ms by a sibling's `CompleteSubObjective(parent, sibling)` row. The 15 s marker blinks (Assault_SO2, ShootingRange_SO0) have no sibling Complete that close, and the live c569b392a log confirms they are not counted. Lesson 54 then moves `current` to the doable sibling.

56. **A script hand-off can add the next step instead of completing one (c569b392a).** Entering the ShootingRange_SO0 presence volume (C_24) hides ShootingRange_SO0 (`bools 00`) and adds ShootingRange_SO1, the Morita pickup, 2 ms later. ShootingRange_SO0 only completes at the end of the range (card097 `ObjectiveComplete`), so every earlier soak (soak1m1b366c, c569b390b) went VOID on it. `handed_off` now also accepts a sibling `AddSubObjective` or `AddCountSubObjective` under the same parent within 50 ms of the hide. Its blinks are `bools 11`/`01`, never `00`, so they still do not count. Checked on the live c569b392a log: Assault_SO0 gives Assault_SO1, ShootingRange_SO0 gives ShootingRange_SO1, and Assault_SO2, ShootingRange_SO1 and ShootingRange_SO3 give none.

57. **`grenade fill N` only raises each player to the class maximum (c569b392a).** The row reads `grenade fill: BP_PlayerCharacterMI_C BP_GrenadeItem_Frag_C 1 to 2`, so a Trooper holds at most 2 Frags whatever N is. Three fire-then-throw rounds (Incendiary, then two Frags) left 0, and all five nest throws after them were empty G presses: no `card569` row, no count change, nest stuck at 9/10. A throw sequence longer than the class maximum refills right before each throw, and a gate that counts `key G` presses does not prove a throw was made.
58. **A measurement window is keyed on a row the scenario writes at the point it must measure (c569b392b).** The card 573 window sat right after the map load in c569b392a and was unmatched against b390 quiet2. Now the scenario sends a second `host console t.MaxFPS 60` after round 00's first objective step (the b390 quiet2 point), then holds 34 s. `tools/c573_capture.sh` waits for row 2, gates on other-job card1 gfx under 30 % (drm-engine-gfx ns deltas from `/proc/*/fdinfo`, the two games excluded), then records 20 s. The hold must cover settle, gate and window (2 + up to 10 + 20 s). The gate runs once at the start, so a busy rise inside the window still needs its own cause from the logs.
59. **A one-shot GPU gate does not keep a window clean; log other jobs for the whole run and check each window afterwards (c569b395a).** The gate passed at 0 %. Then another session's ps-recomp job (play-21c) started mid-window, ran at 59-80 % card1 gfx on CPU 5 inside the 4-7 pin set, and voided 5 of the 6 GPU sizing pairs. `tools/gpu1s_sampler.sh` logs other-job gfx and the top other comm for the whole run, so each window is judged from its own lines. Under load one fdinfo scan takes about 1.6 s, so a 10 s phase holds only 4-6 samples. Confirm a render toggle took effect with a screenshot pair: in this shipping build the `ShowFlag.*` console vars changed nothing on screen, and `ShowFlag.PostProcessing 0` did not even drop the tonemapper.
60. **A measurement window waits for a clean window, never a fixed hold (c569b395b).** play-21c held card1 at 80-90 % gfx for the whole fixed 34 s quiet hold, both games fell to about 13 fps, the range missed its 8/10 must wait and the run died before its card 569 rounds. `tools/c573_capture.sh <out> logs/<tag>.release` now restarts its 20 s window whenever an other-job read hits 30 %, and writes the release file only after 20 clean seconds. The scenario waits with `host wait file logs/<tag>.release timeout=120`; no file means the run is void, not slow. Launch only when no play-* job is running, and watch run.gpu1s for other-job lines of 30 % or more during the run.
60. **A measurement hold waits on a clean window, not a clock (c569b395b).** play-21c held card1 gfx at 80-90 % for the whole fixed 34 s hold, both games dropped to about 13 fps, and the run hit no window and timed out at the range. Two contended runs in a row is a change of method. The hold is now `host wait file logs/<tag>.release timeout=120` (drive.py `wait file`: the file must be written after the replay started). The capture script writes the file after 20 clean seconds (other-job gfx < 30 %), and a run that never gets them is void while it runs. The launch gate alone (GPU < 30) is not enough.
61. **Copy wait regexes from a real log row, never from memory (c560b371b).** Step 63 waited on
   `GiveWeaponByClass\(TurretMorita\)`, but the row reads `card021: host GiveWeaponByClass(BP_WeaponDefinition_TurretMorita_C, ...) = 1 on ...`.
   The seat spawned and the step timed out anyway. Step 42 waited on a `card303 possession` row that logs a trooper
   only while a mount press is open. Before a run, grep each new wait regex against a past log that has the event.
62. **A cross-player proof keeps both players inside net cull (c560b371b).** The host warped to the Door Gunner
   pickup 43000 uu from the client, which is beyond the 15000 uu net cull. The client never got the seat
   (`WARN card467 ... no turret copy`), so its card561 proof could not run. Any step that proves what the other
   player sees warps that player within about 300 uu first (`warp client BP_PlayerCharacterMI_C 300`).
63. **A console A/B needs a restore phase, and only sizes what the scene shows (c573rb401a).** The DLL cannot read a
   cvar back, so each candidate runs on, then restore, with the UE5 sg-3 value. The restore phase is compared with
   the base phase in `run.gpu1s`; restores within about 1 point of base mean the restore value was right.
   `fx.MaxCPUParticlesPerEmitter` showed 0 change at the quiet C_2 point because there were few particles there.
   An FX cap is sized only in a window that has FX. Generate scenario blocks with `bash`, not zsh: zsh does not
   word-split `set -- $c`, and the first generated file had whole lines as cvar names.
64. **The second player's pickup warp ends in `live` (c372b402a).** A plain `warp client <class> 80` picks
   instance 0. The host had already taken that Door Gunner pickup, and it is disabled on both sides, so the
   client look found focus none and voided the client seat, plus card 563 after it. Whenever a pickup class was
   taken earlier in the scenario, use `warp ... <dist> live` to go to the nearest untaken instance.
65. **The `shot` verb does not capture a camera fade (c372b408a).** The host's own turret seat ran the game's
   own 5 s StartCameraFade (1.00 to 0.00), and its shot 2.4 s later showed no dimming. The client's forwarded
   fade looked the same. Never use a shot to prove a PlayerCameraManager fade. Prove it with the applied log
   rows, or with a log row that reads the camera manager's fade amount at the shot time.
66. **A class-name warp or look picks the nearest actor of that class, whatever its height (c551b376a, c565b409a).**
   `BP_AI_Trooper_Morita_C` from the C1M1 range resolved to a trooper on a tower at z 1101, and every look shot showed
   the sky. Before a scenario targets an NPC by class, read its positions in an earlier run's log (the `look: <class>
   at x y z` rows and the card041/card434 rows) and pick a class whose nearest instance stands at ground level
   (`BP_AI_Trooper_SquadLeader_C` at 5986 11856 117 from the range in c565b409a).
67. **A `focus=` gate on a look voids at an actor that has no focus (c582b411a).** `client look actor
   BP_Door_Sliding_Gigantic_C 0 bounds focus=BP_Door_Sliding_Gigantic_C` hit the door at 1321 uu, but a door never
   takes the focus, so the look voided and the fire key after it was skipped. For a shoot-at step on a building, a
   door or another non-interactive actor, use `bounds` without `focus=`, and gate on the aim hit row.
68. **A failed setup step fails its group, and every later wait in the group then runs 1 s (c582b412a).** A
   client warp beside the host found no landing with a sightline at the tower Door Gunner pickup ("no candidate
   held"). The group failure cut the seat waits to 1 s, so the scenario went on while the host was still seated, and
   `host down host` downed the seat (host crash, card 586). Mark a step `try` when nothing later depends on it. Make
   the wait that the next block depends on (here, the host back in its trooper) a strict wait.

69. **Check the dll against the newest source before a run (c582b414a).** A peer edit landed after the b414 build; the
    first run tested a dll without it and had to be redone. Before each run, compare the newest `mod/src` mtime with
    `build-b5/dwmapi.dll` and rebuild when the source is newer.
70. **A cached actor pointer needs the destroy hook (c586b418a).** The client cached its own seat for card 588 rows.
    The seat was destroyed in the same net frame that set the pawn back to the trooper, before post-tick saw the
    change, and the next post-tick read freed memory (crash in c588_row). Any actor kept across frames is cleared in
    `detour_c98_destroy`, never only when a later tick notices it is gone. The same run showed the matching roster
    lesson: after a call that changes a PC's pawn, a roster taken before it is stale for that slot.
71. **A guided missile slot is single use (c587b421a).** After one throw the slot loses its ability, so a later
    `host charge` fails with "no TacticalSupportComponent slot ability or charge functions". Every throw needs its
    own pickup. Card 581 rows print the attach parent as the turret's root component (CollisionCylinder), so match
    that, not the turret actor name. (b422 prints the seat actor instead: match BP_MountableTurret_Gunner_C.)
72. **Look flags go in the order `bounds hole live smooth focus=` (c587b422a).** The look parser strips flags from
    the end: focus= first, then smooth, live, hole, bounds. `0 live bounds focus=X` voids the look ("look takes
    <yaw> <pitch>..."), and the key E after it then does nothing. Write `0 bounds live focus=X`. Possession rows
    (`possess: ClientRestart`, `client-input: SetPawn`, host `PossessedBy`) stop at the diag cap after a few pawn
    changes (c587b422b: throws 2 and 3 and both seat returns "timed out" although the trooper came back). Wait on the
    own-pawn row instead: `card490: <role> t=\d+ pc 0 BP_PlayerCharacterMI_C role N: `, with a mark set after the
    ride starts. A goto near a seat's landing spot can stick on geometry.
73. **An empty mech self-destructs 60.0 s after a normal exit (c591b427a).** The game's abandoned-mech timer calls
    LeaveMech with no weak PC (mech+kMechController null), plays `SelfDestruct_Eject` and `_Buildup`, and a timer
    runs `ASTPlayerMechBase::DeathComplete` about 3.0 s later. A scenario that leaves a mech and keeps playing must
    expect that death (and its gibs) in the next minute. To tell the two leaves apart, test the weak PC, not
    c29_controller (which is null at a normal E-hold leave too). The `host warp actor` wait must also accept the
    fallback row `own pawn \S+ .* -> .*SetActorLocation 1`, not only `landed at`: c591b427a timed out on it.
74. **A scenario that needs quit-time rows must end with `client quit` and `host quit` (c558b429a).** Without
    them, run_pair.sh kills the games after 30 s, `sync_quit` never runs, and every "quit totals" row (card555,
    card161, card304, card408, card099) is missing. c558b430a added the two quits and both card555 rows printed.
75. **Check a component offset by name and class in the row before trusting it (c558b429a).** The card558 row read
    ACharacter::Mesh (+0x328) and +0xC20 and got null and a hidden SpriteComponent; the mech's shown body is its
    "Mech" StaticMeshComponent. A diag row that names each component's name and class catches a wrong offset in
    one run instead of two.
76. **A chord's second number is the press time, not a hold (c584b432a).** `key W 2000 LeftShift 2000` was skipped
    ("chord: at_ms must be below hold_ms"): the second key goes down at at_ms and up with the first. A sprint is
    `key W 2000 LeftShift 50`. A stuck goto also needs a clear right before it (c590b431a: a live Warrior Bug stood
    in the path 200 s after the only `host clear`), and its give-up traces run at the final spot along the view yaw,
    so read the stuck rows, not the give-up row, for the blocker.
77. **`focus=` only on actors with a use prompt (c590b434a).** `look actor BP_ResupplyRack_Level0_C ... focus=` voided:
    the aim hit the rack, but a rack has no use prompt, so focus stays none. The voided gate then skipped the next
    key (the host fire). For an actor that is only shot or watched, gate on the aim hit (no `focus=`), and run a code
    `clear` after the warp, since a bug can walk into the aim line (the client aim hit a Warrior Bug).
78. **The capture wait must be the last wait before its `{g}` use (c556b441a).** A `wait log` with groups replaces
    `last_groups`; the scenario put the end-row wait after the "destroyed at (x, y, z)" capture wait, so lines 69-73
    (warps, looks, shots at `{g1} {g2} {g3}`) failed with no groups. Any other wait goes before the capture wait.
    Also: a rack placed where bugs fight a player ends early from bug damage (both c556 runs: Warrior Bug hits on
    the client within 120 ms of the destroy); a rack end-path test clears bugs on a timer near the rack.
79. **A copied scenario carries its known bugs (c556b442a).** The b442 scenario was copied from the b441 one after
    lesson 78 was written, and the same capture-order bug failed the same post-destroy steps. When copying a scenario,
    re-read the lessons for the run it came from and fix them in the copy before the parse check. Also seen again:
    one code `clear` at the start does not keep bugs off a rack for its life; the rack died from Warrior Bug damage
    6.3 s after its spawn, before the client takes landed.
80. **A gate on a lane 2 build needs `LANE=2` (c448b443a).** run_pair.sh defaults to lane 1. The launch line copied
    from an older run had no LANE, so the gate started on lane 1 (the play build) and the "void unless dll" pin
    voided it at line 13. Check the runner.out line 2 (`lane 2: dll <new hash>`) right after any launch. The pin
    caught it; never drop the pins from a gate scenario.
81. **Read the whole script chain before predicting a completion time (c244b443a).** Card 514 predicted that Stadium_SO2
    would complete within 10 s of standing in C_5, based on the 10 s retry. That retry only runs when the players are
    outside. With them inside, the only path is the fight chain plus a fixed 20 + 18 + 5 s show. Predict times from
    every caller of the event (search for `'Event Name'` and its export offset), not from the nearest retry.
82. **A line trace cannot see a shape it starts inside; use an overlap at a stuck spot (c590b444a).** Three runs
    of Pawn-profile line probes read clear at the card 590 stuck spot. The engine's EncroachingBlockingGeometry for
    the live pawn named the blocker at once: a live Warrior Bug capsule 100-220 uu off, overlapping the trooper's
    capsule. Every goto after a long block (seat, cutscene, dropship ride) gets `host clear 4000 kill` right before
    it, per the clear-bugs-first rule; bugs gather around a player who stood still.
83. **Read every script block to its end before classifying it** (card 596 sweep, 2026-11-21). A kis sweep that
    printed only the first ops of each exit binding called FED_C4M1 C_5 "markers only", but `inStadium = false`
    sits after seven marker calls (@6119). Follow gotos to the block's `pop_flow` and filter the known-harmless
    calls, then classify.
84. **Check the lane's deployed DLL before a run, and keep players off an evac's landing pad** (c595b447a-c,
    2026-11-21). Run a voided at the `void unless dll` pin after 44 s: lane 2 still held b444, because a build in
    build-b5 is not copied to game2 by the harness (DEPLOY=1 copies build/dwmapi.dll only). Copy
    build-b5/dwmapi.dll and ubwperf.dll to game2's Win64 dir and check the hashes first. In run c, the card 596
    out-points were the floor under dropship C_1: ExecuteBeginDescentBehaviour ran, and DescendToLandCompleted
    never came in 100 s (run b, players away: 4.6 s). Also: a presence out-point 3000 uu from C_19's centre was
    still inside its shape, and the client's interactor found no target at 627 uu from the dropship (the host at
    400 uu did). Rule: out-points come from a warp row that is known outside, away from any landing pad, and an
    interact press stands at 400 uu or nearer.
85. **Cases that each end the level get one run each** (c595b448a/b, 2026-11-21). The two card 595 bleedout
    cases (revived, then press; left to bleed out) both release the evac, and the release ends the level. Two
    short runs from one shared preamble (about 180 s each) beat a mid-run `travel restart` after EndLevel, which
    no routine has proven. A revive inside a capped 10 s bleedout fits: `host down client`, then warp 120, look,
    and `key E 4500` revived in 4.1 s.
86. **Melee on a big actor needs a point warp near the look hit** (c582b449a, 2026-11-21). A warp ring is
    measured from the actor pivot. On BP_Door_Sliding_Gigantic_C (extent 200 1131 303) `warp actor ... 200` left
    the look hit 873-886 uu from the eye, and the knife trace (GA_HumanMelee) reaches only 64 uu, so both melees hit
    nothing. For melee, warp to a point about 100 uu short of a known look hit on the eye-to-hit line, then
    `look point` at that hit: `look actor ... bounds` aims at the bounds centre, which can then be behind the player
    (c582b449b: 0 of 26 samples hit, the aim went 8225 uu past the door). A wait for
    a periodic row (the 60 s totals) also needs `since-mark` after a `mark`, or it matches an older row at once.
87. **A capped diag row can be spent before the step that needs it** (c582b449c, 2026-11-21). The card455 attached
    diag (24 rows per map) was used up by a 1 Hz gate loop sound before the melee, so the predicted row never came
    although the melee hit. Before a run, count how many rows of the predicted kind the map logs ahead of the
    step (grep a past log); if near the cap, wait on an uncapped row of the same event instead (card161 door
    OnDamageTaken, card304 player sound, the card455 relay row).
88. **After `host warp client`, wait for the client's own pawn before a client look** (c582b450a, 2026-11-21).
    The client `look point` ran 25 ms after the warp and aimed from the client's old spot (-70879 17253): yaw 277.3
    where the door hit needs about 262, so the client melee missed and the run says nothing about melee damage on
    the server copy. Put `client wait near <warp point> 80 timeout=10` between the warp and the look. Also, the
    card582 rows print the class name without the `_N` instance suffix (object_name), so match
    `BP_Door_Sliding_Gigantic_C(?:_\d+)? `, not `..._C_1`.
89. **Read the Blueprint kismet dump before choosing what to fix in a camera or aim path** (card 599, 2026-11-21).
    The card said to write the server copy's camera cache, but GA_HumanMelee reads the camera manager's actor
    transform (`K2_GetActorLocation`, `GetActorForwardVector` after `GetPlayerCameraManager(0)`). Only local
    controllers move that actor (UpdateCamera); a copy's stays at the origin. Check build/kis_res/<asset>.txt for the
    exact getter first, then fix the value that getter reads.

## Proof status

| Type | Status |
|---|---|
| Nest | Proven (card 504, run c504n2, DLL 4c265abd554c8232): `host objective do <id>` in tools/drive.py. C3M2 Monorail_SO2_A–D all complete on try 1. Each nest was destroyed about 2.7 s after the throw, and the client replayed every complete. Hit-to-hole distance was 29–68 uu, at 351–447 uu range. In 3 aim shots the crosshair is on the yellow hole. In shot B it is on the rock edge at the left of the glow, and the nest still died at +2.8 s. Open item, no run for it now: a stand that leaves the hole half occluded (B, pitch -29.7). |
| Presence, use, pickup | Proven (card 504, run c504p1, DLL 4c265abd554c8232, FED_C1M1): `host objective do <id>`. Presence Assault_SO2 and SO3 and use Assault_SO4 (the client pressed E on BP_PanelSwitch_WithoutScreen_C) each completed on try 1 and the client replayed them. Pickup TacSupport_SO2: the host took it at 394408 (1 of 2), the client at 396063 (2 of 2), and the Complete came at 396070, only after both (card 495). 0 VOID, 0 FATAL/EXCEPTION. The SO4 and pickup shots were viewed. Note: TacSupport_SO2 has no AddSubObjective row; it shows by `SetObjectiveVisibility`. |
| Satchel site | Proven (card 504, run c504s3, DLL 4c265abd554c8232, FED_C4M1): `host objective do <id>` (routine_satchel; the client plants). Park_SO1_C, Park_SO1_A and LZ_SO1 completed on try 1, Park_SO1_B on try 2; the client replayed all 4. B's try 1 hold was interrupted at progress 1.88 (completed 0); the gate caught it and try 2 planted. Each Complete follows a client card133 "own hold completed ... HoldDone sent" row by about 3 s (the fuse). The Park_SO2_E–A nests passed in the same run, try 1 each. 0 VOID, 0 FATAL/EXCEPTION. Fixes before the pass (c504s2): the hold row's "completed N" is read from `last_groups` (wait_log cuts its detail at 160 chars); a retry first checks its step's Complete row (a late fuse otherwise sends the next try to another step's site); `objective do <id>` returns at once on an already-complete id and voids if the current step is not `<id>`. |
| Board (mech) | Proven (card 504, run c504b6, DLL b284 62e2e778930a4dd6, FED_C2M1 RallyPoint_Marker): `host objective do RallyPoint_Marker` (routine_board; the client boards) complete on try 1, client replayed. The focus came through the card 510 fallback: `aim scan: fallback origin hit, root CapsuleComponent`. BP_BaseMech_C whole-actor bounds sit about 1850 uu off the body (lesson 6). The c504b2/b4 passes were luck of side; c504b3 VOID. BOARD_DISTS (250, 200). |
| Marker (kind marker, actor[0] `ActorReference_C`) | Proven (card 504, DLL 4c265abd554c8232). routine_presence: go, presence wait 10 s, then a stay phase (`clear 4000 kill` every 5 s, cap 120 s) for markers that complete on a wave end or a hold. c504b5: LOS_O01_SO02, LOS_O02_SO02 try 1 (presence); LOS_O01_SO01 try 1 (stay, 26 s, 6 clears; wave end); LOS_O04_SO02 try 1 (stay, 52 s, 11 clears; hold, completed by its parent). Actorless kind other (LOS_O02_SO01, LOS_O03_SO01) proven as presence-actorless (c504b4, c504b5). Satchel pickups LOS_O03_SO02a-c: routine_satchel_take is written but waits on card 511 (not doable under a marked container). |
| Recipe (tools/recipes) | Written, not yet run (senior 2026-11-20): C1M1 TacSupport_SO3, SO4, SO41, SO5, SO6, SO7, ShootingRange_SO2, SO3, from c335-range-load lines 194-1000 (that scenario proved them by hand). The next C1M1 soak is its proof. Hand-written (gen_recipes.py LITERAL): C4M2 BrainBug_SO3, SO5, SO7 (c502-c4m2 boss block, c502b280a) and C5M2 Finale_SO1 (clear rounds, c247b311a); proof is the C4M2 and C5M2 soaks. C3M1 Resort_SO2 (three satchel pickups, lesson 38); proof is the next C3M1 soak. |
| Everything else | Not yet built as routines. The past soaks did these steps by hand-written scenario lines. |

## Exceptions (per objective, with reason)

None yet.
90. **Copy no objective-visibility wait from an older scenario without checking its bools in a recent log**
    (c602b456a, 2026-11-22). c263's `SetObjectiveVisibility(PowerPlant_SO2) bools 01` wait timed out after 60 s in
    b456: the script shows SO2 (`bools 11`) at map start, 47 s before presence C_52 runs, so the row never comes.
    Gate on the step's own run row instead (C_52: `card245 ... OnTriggerEntered by ...: original runs`).
91. **Match the placed-name rows with their `@<guid>` suffix** (c602b456b, 2026-11-22). The card602 rows print
    `BP_Switch_C_2@d7e2bc7e`, so a wait on `switch press BP_Switch_C_2 by` timed out although the host row and the
    client row both landed (46 ms apart). Write placed names in row waits as `BP_Switch_C_2(?:@[0-9a-f]+)? `.
92. **Check a run's wait task from its output file, never from a `ps | grep` of its command** (c602b456c, 2026-11-22).
    After a context break a GPU-wait task looked gone because `ps | grep run_pair` only matched the grep's own shell, so
    a second run was launched. When the GPU cleared, both started; run_pair.sh aborted the second (`ABORT: a lane 2 game
    instance is already running`), but its start truncated the live run's `.run.txt`. Read the task's output file, or
    stop the old task with TaskStop, before launching again. The `.drive` log stays the step record.
93. **A placed name can belong to more than one actor across sublevels** (c602b456c, 2026-11-22). `look actor
    StaticMeshActor_12` resolved to a BP_KillableCrate_05_C in another sublevel, not the level script's generator, so
    the proof shots showed a crate and a wall. Before a shot on a placed StaticMeshActor or other generic name, check the
    aim row's class (`aim: hit <name> (<class>)`) matches the expected one, or use `focus=`/`hit=` with the class gate.
    Warp to a point read from the right actor when the name is not unique.
94. **Disassemble an engine getter before calling it by its pdb address** (c604b457a, 2026-11-22). The pdb address
    for `UAudioComponent::IsPlaying` (exe+0x3A35C20) is a folded, this-adjusted body that reads `[rcx-0x1AA]`; the
    card 604 Stop detour called it with a component pointer and the host crashed during map load at 36 s. Before a new
    `g_*` binding to a small const getter, `objdump -d` its first instructions and check it reads `this` at a positive
    member offset. For a plain flag, read the bit directly at the offset the reflected `exec*` thunk uses
    (`UActorComponent::execIsActive`: byte +0x97 bit 6), as b458 does.
95. **No proof shot after an objective that ends the map** (c604b458a, 2026-11-22). Stadium_O1 ends FED_C4M1. The
    sparkler shots 6 s later showed the client on the next map's loading screen and the host facing a wall. The host's
    aim also hit the client pawn 77 uu away, because both were warped onto the same line to NiagaraActor_17 (client
    1200 uu, host 1300 uu). Take an end-of-map proof shot before the step that completes the last objective, or wait
    for a log row that the effect has started before that step. When two players are warped to look at one actor,
    give them different yaws or distances off the line, and check each aim row hits the target class, not a pawn.
96. **A quiet stay is not a stuck step while a scripted show runs** (c604b458a line 229, 2026-11-22). Stadium_SO2
    closes after the stadium fight chain plus a fixed 20 + 18 + 5 s show (lesson 81). That show logs no kill and no
    wave row, so the stay's quiet rule (3 periods with neither) ended the stay at 47 s, 3 s before the close. b459
    passed the same line only because more bugs died, which kept the stay going until the close at 50 s. Before a
    rule ends a wait on "nothing happened", check that the step has no fixed-time stretch longer than the rule's
    window. `drive.py` now ends a quiet stay only after `STAY_QUIET_MIN_S` (60 s).
97. **Copy the whole launch line from the last run of the same scenario** (c448b463a, 2026-11-22). The b463 proof
    was started as bare `tools/run_pair.sh c448b463a 720`. With LANE unset it ran lane 1, which still held the b462
    play build, and with SCENARIO unset no driver started. Both games sat at the menu until the 720 s cap, and the run
    proved nothing. A run line carries `LANE=2 SCENARIO=... FAST=1 GOD=0` plus the extra env. Take it verbatim from
    the previous run of that scenario (its card or the transcript), change only the tag, and check that the first
    line of `<tag>.run.out` says `lane 2` and the deployed build hash.
98. **A perf cut on a hot path gets its disassembly read before the run, and a missed prediction gets no cause without
    evidence.** b463 moved the ProcessEvent gates into a std::atomic word to save loads, and the compiler made each
    load a function call (up to 16 per call, b462 had 0). The run showed the miss, and the cause reported afterwards
    blamed two hooks without reading their code or their gate times. Run llvm-objdump on the changed functions after
    the build and count calls and loads against the prediction. Write "cause not measured" until a row or the
    disassembly shows one.
99. **A timing run records other projects' load on the run's CPUs, not only GPU busy.** The baseline c448b463b passed
    the GPU check while ps-recomp test jobs ran, so part of b464's drop against it (0.199/0.204 to 0.162/0.150 us per
    call) cannot be credited to the cut. Before a timing run read `ps -eo pid,pcpu,psr,comm --sort=-pcpu`, wait for
    foreign jobs on CPUs 4-7,12-15, log any that start during the run, and compare one row of unchanged code.
100. **A totals row printed at a map change uses names copied at insert, never a stored object pointer.** c448b464b
    crashed in the card 604 summary after LoadMap. The earlier passing runs had already printed "None" for those
    names in every map-time row (card 608). In a log check, a "None" name in a map-time row means a stale pointer
    until shown otherwise.
101. **A `health 0.0` row is rounded and is not a death, and `kill actor` from revive Health is not a reliable down.**
    c607b466a step 3 read 0.0 with `dead 0`, and the revived client was alive (card 610). Judge a down by its card043
    Downed row (Downed itself sets Health 1 and `dead 0`). A step that needs a down right after a revive uses another
    lethal source or first waits for regen.
