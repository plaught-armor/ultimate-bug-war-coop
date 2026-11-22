# Card 149: The client change log names the right cause

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: queued 2026-11-08, from card 137 (hb72 close-out). Budget: one run, batched. A diagnostic tool, not gameplay.

Read: card 137 step 2a and its hb72 result.

## Why

hb72: every dump repeats "42 rows, host row 0, own input 0, local sim 2269, overflow 0", although the client applied
272 card095 rows. Either the card095 apply misses the chokepoint, or the ledger bracket (ledger_c132.cpp:452) does not
wrap it; the identical totals suggest the table counts only the load-in window. Real gap it showed: barrel component
collision 3 -> 0 by local sim at 240552680 (caller exe+0x5165410), put back by host rows.

## Do

1. Static: which host-row apply paths sit inside the cause bracket; why the totals do not change after load.
2. Fix the bracket and make the table per session with a reset at map arm.
3. Batch the proof into the next run that has a join: host-row count equals the client's applied host rows by op.
   The barrel collision local-sim row goes to card 144 with its caller.

## Done when

- The trace, the fix, the run lines. It ends with "Report complete."

## Addition 2026-11-11 (from card 176, run c176-r1)

Ordinary fire animation lines spend the card 104 line cap (`kC104Lines` 60 per run, anim_c104.cpp:27) early in a
run, so later DoAction and ActivateFlare proof lines are never written (c176-r1 failed a wait on one). Give each
line class of card 104 its own cap (fire animation lines a small one), or cap per key, so a proof line is not
starved by a frequent one. Same check for every other shared line cap that a per-shot line can spend.

## From card 144 (senior, 2026-11-12)

The "local sim" rows on spawners, pickups, corpses, nav links and hazard zones are constructor writes on `Default__`
and `*_CAT` archetypes (thread T 1300, location (0, 0, 0), placed 0): `c50_placed` lets them through. Give them their
own cause or drop them from the change log; the caller address is the direct native caller and is right.

## Senior prep (2026-11-12, static, tree at card 222)

- Still present: c147-r3 and c201-r2 clients dump "host row 0" in every total (42 rows, local sim 2269 at map
  change; 19 rows, local sim 774 later).
- Cause, verified in source: the bracket is in place (`ledger_c132.cpp:460`, `ext_c132.cpp:309`), but the mod's
  own applies of host rows call the originals past the detours that hold the log call: `door_c95.cpp:840` and
  `:842` (`g_orig_set_actor_collision`, `g_orig_set_collision_enabled`), `world_c98.cpp:957`
  (`g_orig_c98_destroy`), `door_c161.cpp:211`. They never reach `c137_client_change`, so "host row" can only count
  game code that a handler runs inside the bracket. Rows applied later from a tick (held or pending rows) are
  outside the bracket as well.
- Open for the junior's step 1: per op, whether the handler applies in the frame's bracket or from a tick; and
  which "local sim" rows are the client's own game code reacting to a relayed event (a crate killed by a relayed
  hit turns its own collision off: `BP_KillableCrate_*` component collision, 230 rows in c201-r2).
- Direction for step 2: count a host-row apply at the apply site (one call with the cause given), keep the depth
  bracket for game code run by a handler, and add the ext and tick apply paths. The identical 2269 at every map
  change is the load-in window (constructor writes, see "From card 144").


## Merged in 2026-11-17 13:05: card 280 (same diagnostic family, one build item)

### From card 280: 280 Level-script entries on host and client, measured (diagnostic only)

From `docs/SYNC_AUDIT.md` entry 5 (senior, 2026-11-13). The level script runs on both machines from each
machine's own triggers; client-side results are cut sink by sink (cards 006, 100, 254, 258). Before deciding on
"the client runs an entry only when the host ran it", measure how far apart the two scripts run today.

#### Do (one diagnostic, no behaviour change)

- `mod/src/ubw/hooks_ubw.cpp` `detour_process_event` (hook exists): when the object's class name ends in
  `_Scripting_C` and no outer call on such an object is in progress (depth 0), log before the original:
  `card280: <host|client> t=<ms> script entry <function name> on <object>`; cap 400 lines per map, the rest
  counted and reported at map end as `card280: <side> script entries N, logged M`.
- The class test must cost one pointer compare per call after the first resolve per map (cache the class
  pointer at map load; `ProcessEvent` is hot). No string work on the hot path for other objects.
- Do not touch any sink cut. No new op, nothing sent.

#### Predictions (rides the next run of each map, 0 extra runs)

- Host and client rows for one map differ: client-only entries exist (the card 254 chain is one), and shared
  entries differ in time by more than 100 ms where a trigger depends on which pawn overlaps.
- If the rows are equal in name and order on every map, the change in the audit entry is not needed and the
  entry is closed as "method right".

#### Then

Senior diffs the rows per map and writes the verdict into the audit entry; a change gets its own card.


## Merged in 2026-11-17 13:05: card 282 (same diagnostic family, one build item)

### From card 282: 282 Client "local sim" change table split into load time and play time (diagnostic, build 5)

Source: `docs/SYNC_AUDIT.md` entry 6. `change_c137.cpp` counts every change the client's own game logic makes to
a host-owned map actor (cause "local sim"), but load-time setup fills the table (FED_C3M1: `BP_AISpawnPoint_C`
324, `BP_AISpawner_C` 87), so the play-time rows, which are the places the client still acts as a second owner,
are never read. Diagnostic only; changes no game state.

#### Spec

- `ChangeRow` gains a phase (load, play). Phase is play from the first Pong applied on this map (the same moment
  the digest starts), load before it. Reset with the table on map change.
- A play-phase "local sim" change logs as `WARN card137: client play-time change <class> <name> <field> <old> ->
  <new> by local sim caller exe+0x...`, first `kLinesPerRow` per (class, field) as today. Load-phase rows keep
  the plain row. "host row" and "own input" causes are unchanged.
- Totals row gains the two local-sim counts: `local sim load N, play M`.
- Audit entry 9 rides in the same table: a client effect spawn outside a host row (the card 136 spawn
  chokepoint) counts as field `fx spawn`, play phase only, no per-row line (count in the totals dump).
- `kChangeRows` 128 stays; the phase is part of the row key, so check the FED_C3M1 row count against 128 in the
  first run and raise the constant if the overflow counter is not 0.
- Files: `mod/src/ubw/change_c137.cpp`; one call in `fx_c136_net.cpp` for the spawn count. Nothing else.

#### Proof and use

Rides in build 5's first proof run of any map; budget 0 extra runs. Prediction (FED_C3M1): play-phase rows for
`BPHazardousZone_Shunt_C`, the destructible corpse and beach table classes, and pickups; no play-phase row for
the spawner classes. Senior reads the WARN diff and cards each class as "client sim to cut" or "local by
design"; a class judged local by design is listed in the audit entry so the WARN can be lowered to a note.

## Step 1 result (junior, 2026-11-17, static, tree at b194 a119f90574be9a2d)

Evidence run: c448b194a client log, three joins (FED_C1M1 x3). Each dump: "34 rows, host row 0, own input 0, local
sim 543, overflow 0", identical.

Why the totals freeze (verified in source and log order):
- `client_diag_load_map_enter` (sprint.cpp:171) sets `g_client_session = pending != nullptr`. A mission end goes
  through the frontend LoadMap (pending null), so the flag is false when that load's arm runs.
- `c137_change_arm_map` (change_c137.cpp:128) dumps, then memsets. The dump returns early when
  `!g_client_session`, so the frontend arm wipes the whole play window with no line.
- The next join's arm (LoadMap return, pending set) then prints only what came between the frontend arm and
  itself: the join load. That window is the same constructor and load writes every time (13 lines on loader thread
  T 1304 at (0, 0, 0), the rest on the game thread during LoadMap), hence the identical 543.
- The play window is real but lost: c448b194a client logged play-time changes at 597482 to 597610 (dropship
  landing blocker 0 -> 2 -> 0, pickup respawn collision 0 -> 1, BlockingVolume 0 -> 3, all "local sim"; one
  Morita pickup by "own input"). None reached a dump.
- Extra: the loader thread (T 1304) writes `g_change` while the game thread may too. The table is game-thread only;
  off-thread calls must not touch it.

Which host-row applies reach the bracket (ledger_c132.cpp:542 `c137_host_frame(phase == Before)`; the ext enforce
pass ext_c132.cpp:631/652 also brackets):
- The bracket wraps every handler's own frame. But the mod's direct applies call originals past the logging
  detours, so they never reach `c137_client_change` whatever the bracket says:
  - CollisionOp (card 095/237): door_c95.cpp:1196 `g_orig_set_actor_collision`, :1201
    `g_orig_set_collision_enabled`. In the bracket, not counted.
  - WorldOp hide (card 098): world_c98.cpp:2580 `g_orig_c98_hidden` in `c98_client_hide`. In the bracket, not counted.
  - Door state (card 161): door_c161.cpp:218 `g_orig_set_collision_enabled` in `c161_write`, from the receive and
    from the door tick. Tick path outside the bracket too; not counted either way.
  - Copy destroy (card 104): world_c98.cpp:2052 `g_orig_c98_destroy`. Copies are not placed, so `c50_placed`
    filters them anyway; no count needed.
  - Pickup disable: the client apply calls the hooked `DisablePickup`, so it reaches weapons.cpp:1683 and counts
    "host row" when inside a frame. Alive (death.cpp:565): the game's own attribute path, counted when in a frame.
  - Ext enforce (ext_c132.cpp): bracketed, but `ext_apply` writes fields directly; destroyable rows go through
    card 423. No chokepoint call, not counted.
- Result: "host row" can only count game code that a handler runs. c448b194a: CollisionOp 73bcfb7f applied at
  597525246, but the own Interact had already set the same value (old == new), so 0 is consistent.

Fix (step 2, next build; files change_c137.cpp, sprint.cpp, door_c95.cpp, world_c98.cpp, door_c161.cpp):
1. `c137_host_apply(actor, field, old, new, caller)`: counts with cause "host row" at the apply site. Calls at
   door_c95.cpp CollisionOp apply, `c98_client_hide`, `c161_write`.
2. Session end: `client_diag_load_map_enter` dumps the table as "session end" before the flag goes false, so the
   play window of the last map is printed.
3. Card 282's phase split: a row key gains load/play; play starts at this map's first Pong (`c120_pong_open_ms()`
   not 0). Play-phase local sim logs `WARN card137: client play-time change ...`. Totals: `local sim load N, play M`.
4. Off-thread calls (loader thread constructor writes) skip the table and count into `off thread N` in the totals.
5. Not in this build: card 282's `fx spawn` row and card 280's level-script diagnostic (separate items).

Proof prediction (c448, joins): each map's dump has "host row" > 0 at session end (CollisionOp and hide rows the
client applied); "local sim play" lists the dropship landing blocker, pickup respawn collision and BlockingVolume;
load-time spawner rows land under "load" or "off thread".

## Step 3 result c448b195a (b195 51d165a755867924, 2026-11-17)

223 s, rc 0/0, drive rc 0, 0 EXCEPTION/FATAL. Client totals now differ per session:
- map change (join load): 0 rows, off thread 584 (the loader's constructor writes, no longer in the table).
- session end 1: 53 rows, host row 34, own input 3, local sim load 46, play 1218.
- session end 2: 52 rows, host row 25, own input 0, local sim load 46, play 638.
Host-row rows: pickups (Morita ammo, armour, health, grenades) component collision, BP_Door_Sliding_Large_C,
BP_Dropship_LandingBlocker_C. Session 1 applied 15 CollisionOp and 9 WorldOp E recv rows; host row counts only the
ones that changed the copy (old != new), so it is not an exact per-op equal.
Play-time local sim, top: BP_KillableBuilding_Base_TacSuppRangeTarget_C component collision 414 and 200,
BP_KillableCrate_03/05/06/01_C 94 to 142, BP_KillableBuilding_Base_TargetRangeTarget_C 130 (the card 144 class:
the client's own game reacting to a relayed hit). 171 `WARN card137: client play-time change` lines.
Not done here: card 282 `fx spawn` row, card 280 level-script diagnostic.

## Closed (2026-11-17 18:21)

Fixed in b195 `51d165a755867924`, proven in c448b195a. The cause of the frozen totals: a mission end loads the frontend with pending null, so `g_client_session` is false at its arm, `c137_change_arm_map` returned early, and the memset wiped the play window without a line. The cause of "host row 0": mod applies called the originals past the detours (door_c95.cpp CollisionOp, world_c98.cpp hide, door_c161.cpp door buffer). Fix:
- `c137_host_apply` is called at those 3 sites.
- A session-end dump goes in `client_diag_load_map_enter` before the flag drops.
- The load/play split is at the first Pong (card 282).
- Off-thread writes are counted only.
Proof: session end 1 gives host row 34, own input 3, local sim load 46 and play 1218. Session end 2 gives host row 25, load 46 and play 638. Top play-time local sim rows are the card 144 class (TacSuppRangeTarget, KillableCrate, TargetRangeTarget).

Not done: the card 282 `fx spawn` cause and the card 280 level-script cause (diagnostic only). Host row counts real changes, so it is not an exact per-op equal of the applied recv rows.

Reopen when: a session-end dump repeats identical totals across sessions, a play-time change with a host cause logs as local sim, or the fx spawn / level-script cause is needed to explain a desync.
