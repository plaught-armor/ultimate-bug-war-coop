# Sync design audit (started 2026-11-13, senior, static)

Each entry is true as of its date and build. A card named open here may have closed since: `docs/STATE.md` lists what is open.

User decision 2026-11-13: review each sync mechanism by class. Question per class: is the method right, or are
symptoms being patched. One entry per class: method now, verdict, what to change, what proves it. Defects get cards.
No runs are made for this audit; proofs ride runs that happen anyway.

All 11 classes have an entry; the table at the end ranks the findings.

## 1. Corpse pose (`pose_c132.cpp`, 34 cards)

- Method now: the client's corpse copy is a live ragdoll. The host's rows teleport its bodies; between and after
  rows the client's own physics owns them. The mod switches gravity, sleep, simulation and the bone update type
  around each row (stream: gravity off; settled final: gravity on, sleep; frozen final: simulation off one tick
  later, held).
- Verdict: two owners for one set of bodies. Every recurring bug of this class is the client's physics moving a
  copy after the host's last word: settled finals woken by the gravity switch (card 186), the copy that fell
  without end (card 189 addendum), frozen copies moved as a unit (card 251), bones apart from bodies after a freeze
  (card 191), re-arrived copies drifting (card 269 B). Each fix so far removed one mover and left the second owner.
- Change (one owner): after the first host row a copy's bodies move only by host rows, for its whole life. The
  stream state already does this (gravity off, velocities zeroed, teleport per tick). Proposed: the final row keeps
  that state instead of handing the copy back to physics; a host corpse that moves again is sent again
  (`kPoseMoved`, exists).
- Open before a card is written (card 191 decides it): how the bones follow the bodies of a copy whose simulation
  is off. The engine blends only simulating bodies into bones, so a frozen copy shows what its animation writes.
  Two candidates: keep the copy simulating with gravity off and asleep (no freeze on the client at all), or stop
  its animation at the freeze. Attack for the first: a player or blast pushing a copy without gravity sends it
  drifting; needs the bodies' collision response to players and impulses off on the client copy.
- Proves it: `Pose:0` in every digest round of a full map, 0 `freeze check` WARN rows, 0 bone check WARN rows.
- When: after the playtest, as one card replacing the open parts of 191 and 251. Build 4 carries their
  diagnostics; read those rows first.

## 2. Gib parts (`gib_c137.cpp`, 16 cards)

- Method now: the host launches a part and sends S, M (every 200 ms while it moves), R rows. The client launches
  its own physics part with the host's values, teleports it on each M row, snaps and sleeps it on the R row.
- Verdict: same two-owner pattern. Between M rows and in the last second before R the client's part flies by its
  own physics (card 279: off by a median 117 uu before the snap); sticky parts ran their own state machine
  (card 273). Unlike corpses the visible error is small and short, so the launch-by-physics method is acceptable
  for flight; the rest state is not.
- Change: at the R row the client's part stops being a physics object (simulation off, then placed), so nothing
  can move it afterwards; a G row or the pool's next launch turns it on again. This also removes the "asleep above
  the ground" class (a part that cannot fall needs no ground).
- Proves it: `rest checked N, within 2 uu N`; card 279's row shows first why today's snap does not hold.
- When: step 2 of card 279.

## 3. Hits and claims (`claim_c157.cpp`, `fire.cpp`, `hit_c132.cpp`; cards 072, 157, 175, 274, 276)

- Method now: the client decides what it hit and sends a HitClaim per round. The host deals the damage only by
  running the original `OnHit` of a round that the client's server copy fired itself. The copy fires from relayed
  key presses with its own ability state, magazine, reload and rate of fire.
- Verdict: the claim is the right owner of "what was hit" (the client sees its own target). The defect is the
  second condition: damage also needs a second, independently simulated weapon to have fired the same round.
  Every recurring bug of this class is the two weapons out of step: magazine (072, 274), reload start (274, 276
  item 4), a press lost in the tick of a release (276), held fire ended early (175). Each fix adds one more value
  the copy must follow; 276's re-press is a symptom patch of that kind.
- Change (one owner): the client's weapon owns the shot count. A claim that finds no round does not wait and
  drop; the host makes the round for it (the copy's weapon launches one round outside its ability, magazine and
  rate gates, and the claim resolves on it at once). The copy's own firing stays for what other players see and
  hear (muzzle, tracer, sound), and stops being a condition for damage.
- Settled statically (disassembly, 2026-11-13): a round can be made outside the fire ability.
  - `UADProjectilePooler::SpawnProjectile(init data class, transform, world context)` 0x04A363F0 is static and has
    no ability, magazine or rate gate (pool take, set transform, `InitProjectile`). Already bound and called
    directly by `grenades.cpp` (`g_c74_spawn_fn`). The task's `Activate` 0x04A33CB0 only calls it with the task's
    class (+0x100) and transform (+0xA0).
  - `AADProjectile::OnHit` 0x04A35990 applies no damage itself. It broadcasts the dynamic multicast delegate at
    projectile +0x2B0; `BindToProjectileImpact` 0x04A36B70 appends one `{object, function name}` pair (16 bytes)
    to that list. The damage is the weapon ability's blueprint event bound there per round.
  - So a host-made round needs that pair. Source: the pair and the init data class of the last round `round_add`
    noted for the same player and weapon (read at +0x2B0 when the round is parked).
- Open, only a run settles it: whether the ability's bound event applies its damage while the ability is not
  active. Prediction: it does (the event applies an effect to the hit actor from the hit result). Deciding rows:
  a claim served by a host-made round followed by the `card132` damage row for the same NPC; if the damage row
  is missing, the fallback is to hold one spare round of the copy per weapon instead of making one.
- Carded: 281.
- Attack: a claim per round with no host-side count check lets a client deal unlimited damage. Accepted by the
  trust model (friends only); keep the range and trace checks.
- Proves it: 0 `found no round` rows over a full map; client rounds that hit an NPC equal host `applied` rows.
- When: after the playtest if build 4's card 276 rows show 0 drops in the FED_C3M2 run; before it if drops
  remain, since lost client damage is play-visible.
- Hit blood and wounds (`hit_c132.cpp`): the host's `OnDamage` is the one source, the client replays it for
  effects only and changes no state. One owner; method right, no change.

## 4. Pickups and ammo (`weapons.cpp`, `ammo.cpp`, `world_c98.cpp`, `interact.cpp`; cards 056, 071, 072, 140, 145, 221, 239, 254, 256, 266)

- Method now: the host is the only source of a take. The client's server copy overlaps the pickup on the host;
  enabled state ('E', 'N' rows), FX, ammunition credit (`Ammo` row from the plural `AddAmmunitions` hook) and
  objective rows go to the client. On the client the game's own take is cut at its end: `DisablePickup`
  (card 140) and the base `Pickup_Implementation` (card 254) return early for a placed pickup or a host copy.
- Verdict: the owner is right, the cut is at the wrong depth. The subclass code before the base call still runs
  on the client (`ASTAmmoPickup` credit, `ASTWeaponPickup::PickupWeapon`), so a client below the maximum is
  credited twice (card 239), and the test "is this the host's pickup" has a hole while a copy is being spawned
  (card 239: not in `g_c104_copies` yet). Cards 140, 254 and 239 each closed one exit of the same function chain.
  Card 221 (respawn and take in one tick) and card 256 (dropped weapon rests elsewhere) are the same question
  asked of timers and physics: something on the client still acts on a pickup by itself.
- Change (one cut, at the entry): in a client session no pickup runs its own take, respawn or drop physics. The
  cut sits at the entries (list below, corrected by the trace) and returns before the subclass code for every
  pickup on a client; "the host's pickup" becomes "any pickup in a client
  session", which removes the table test and its hole. The card 140 and 254 detours stay as WARN-only backstops
  (a row there means an entry was missed).
- Settled statically (disassembly, 2026-11-13): `OnBeginOverlap` is not the one entry; there are four.
  - `OnBeginOverlap` 0x4C61750 only filters the actor class, stores the overlapping character (+0x3C8) and calls
    `TryPickup`. It drives no prompt and no outline.
  - `AADPickupBase::TryPickup` 0x4C628F0: enabled flag (+0x2AA), `CanBePickedUp` (blueprint event or vtable
    0x750), then the `Pickup` event (blueprint, or vtable 0x758 = `Pickup_Implementation` of the subclass), else
    the failed path (vtable 0x760, 0x788). Callers: `OnBeginOverlap` and `DeferredBeginPlay` (0x4C6111A, a pickup
    that appears on top of a player).
  - Key-taken pickups skip `TryPickup`: `ASTItemPickup::OnInteraction` 0x51D03F0 jumps to vtable 0x758;
    `ASTTacticalSupportPickup::OnInteraction` 0x51D05A0 and the secret pickup (0x51D0597) call
    `AADPickupBase::Pickup` 0x4C61940; `ASTWeaponPickup::OnInteraction` 0x51D0630 swaps the weapon (0x51D2FD0)
    and then calls the base `Pickup_Implementation` directly.
  - Why the card 254 cut leaks: it sits on the base `Pickup_Implementation`; a subclass override reached through
    vtable 0x758 or the blueprint event runs its credit first, and the weapon swap runs before the base call.
  - Corrected change: client-session cuts at `TryPickup` (covers overlap and deferred begin play) and at the three
    `OnInteraction` entries, which send the take to the host instead (interact relay). The client then shows no
    local "pickup failed" feedback; the host's result decides.
- Open: whether any pickup exists only on the client by design (none known; the digest's pickup rows would show
  one).
- Ammunition counts: the client owns reserve and magazine of its own weapons and mirrors them to its copy
  (cards 072, 274); the host owns credits. Two directions on one value, safe only because the mirror never lowers
  inside a burst and credits arrive as deltas. No change; class 3's change removes the reason the copy's magazine
  matters.
- Proves it: 0 `card140` and `card254` rows in a full map (nothing reaches the old cuts), client ammunition after
  a take equal to the host's `Ammo` row, digest pickup rows 0 mismatches.
- When: with card 239's fix (after the playtest; double credit is play-visible only below the maximum and in the
  client's favour).
- 2026-11-15: the double credit is the normal case below the maximum (`c276b67`: one pickup of 120 gave 240).
  Card 384 pairs the two credits per tag in b68 (interim; remove with the entry cut).

## 5. Objectives and level scripts (`objectives.cpp`, `world_c98.cpp`, `guards.cpp`; cards 006, 049, 097, 100, 121, 227, 254, 255, 258, 277, 278)

- Method now: the level script (`FED_*_Scripting` level Blueprint) runs on both machines from each machine's own
  triggers. Host to client, results travel twice: the objective library calls are mirrored (card 049), and engine
  primitives on actors that do not replicate are relayed as world ops (destroy, hide, timeline, attach, spawn,
  collision, door, damage). On the client the script's own results are cut sink by sink: `StartEncounter`
  (card 006), `CompleteObjective` and `IncrementObjectiveCount` (card 100), the Add family (card 258), the pickup
  chain (card 254), the lever's save (card 121).
- Verdict: two owners of the script. The host-to-client half is sound (finite primitives, journalled). The
  client half is the patch pattern: every client-side sink was found by a run or a crash, one card each, and the
  digest only sees a wrong end state, not a client that ran a chain the host never ran (card 254) or ran it at
  another time.
- Measured (senior, static, 2026-11-13; `kis.py` over the 8 Trooper `_Scripting` levels, tally in the session
  scratchpad): the scripts call 111 distinct functions. State-changing ones with no cut and no name in the mod
  source: `Trigger Encounter Manager` (263 call sites), `CleanUpEncounter` (161), `StopEncounter` (91),
  `End Encounter Manager` (68), `SetSaveZoneEnabled` (61), `ForceMoveNPCSToArena` (30),
  `SetSwitchExternallyEnabled` (25), `SetIsInteractable` on the dropship (11), `Begin/End KillEmAll` (14),
  `Drop In AI Mech` (8), `UpdateDamageable` (14), `Toggle Barrier` (20), `Do Movement` (22), `Fire Missile`,
  `Prepare For Launch`, `Jam/Open Hatch`, `Set Shielded`, `EnableInput`, `SetControlRotation`. Some are covered
  one level down (`Trigger Encounter Manager` ends in the card 006 `StartEncounter` cut; movers end in relayed
  timelines); which are not is unknown without tracing each. Local-only by nature and never relayed, so they play
  on the client only because its own script runs: `StartConversationFromList` (142), `TriggerAudio` (42), music
  (41), camera shake (9), `SetMaterial`, `Fade`, `Update Progress Bar`.
- Change (one owner of the trigger, not of every sink): the client's script runs an entry event only when the
  host ran it. The host relays each level-script entry (function name on the `_Scripting` script actor; 106 to
  251 entry functions per map), the client refuses its own entries and runs the relayed ones. Dialogue, audio and
  music then play at the host's moment; the sink cuts stay as backstops and should go quiet.
- Open before a card is written: (1) entry events with parameters that are object pointers (`ActorBoundEvent`
  delegates pass the actor) need the placed-name mapping the world ops already use; (2) delays and latent nodes
  inside a graph run on each machine's clock (acceptable: same start, same length); (3) branches that read local
  state (`GetNPCsInArena`, `IsComplete`) can still differ; (4) entries the client must run by itself (its own
  `BeginPlay` setup, anything driven by its own input or camera).
- Entry kinds counted statically (kis.py, 8 Trooper maps, 2026-11-13): 1263 events. 530 are actor-bound
  delegate events (`BndEvt__...`), 454 of them with a parameter, which is the bound placed actor or its
  argument (`FallZone`, `Switch`); 681 are custom events (151 with parameters: indexes, flags, an NPC), most of
  them called from the graph itself; 28 are lifecycle (`ReceiveBeginPlay`, `ReceiveLevelStart`,
  `ReceiveLoaded`, `ReceiveGameReady`); 24 are plain functions. Delay nodes: 17 to 42 per map.
  - Consequence for question 1: the entries to relay are the 530 bound delegates and the lifecycle four; their
    parameters are placed actors, covered by the placed-name mapping. Custom events follow from those and need
    no relay unless something outside the script calls them (card 280's depth-0 rows list exactly those).
  - Consequence for question 4: the lifecycle four run on the client by itself at load; they stay local.
- Measure first (cheap, rides the next build as a diagnostic): in `detour_process_event`, for an object whose
  class is a `_Scripting` level script, log `card280: <side> script entry <function>` once per call at depth 0
  (cap 400 per map). Diff of host and client rows per map says how far apart the two scripts run today and
  sizes the change. No behaviour change.
- Proves it: host and client `script entry` rows equal in name and order on a full map; 0 rows from the client
  cuts of cards 006, 100, 254, 258.
- When: diagnostic in build 5; the change after the playtest unless the diff shows client-only entries with
  play-visible results.

## 6. World ops (`world_c98.cpp`, `ext_c132.cpp`, `door_c95.cpp`, `door_c161.cpp`, `digest_c132.cpp`, `change_c137.cpp`)

- Method now: the host's changes to actors that do not replicate are caught at engine chokepoints (destroy,
  hide, collision, timeline and rate, attach, spawn, pickup enable, rest transform, door, turret, satchel), sent
  as rows, journalled per map and replayed to a joining client. The digest compares per-class state each round.
- Verdict: method right. One owner (the host), finite chokepoints instead of per-actor code, a journal for late
  join, and a measuring device (digest) that finds gaps by class. The 14 row kinds grew by discovery, but each is
  a primitive, not a symptom.
- Weak spot: the client-side counterpart is measured and not used. `change_c137.cpp` already counts every
  change the client's own game logic makes to a host-owned actor ("local sim"), but load-time setup dominates the
  table (FED_C3M1: `BP_AISpawnPoint_C` 324, `BP_AISpawner_C` 87 collision writes at load), so play-time rows are
  never read. Play-time examples in the same table: `BPHazardousZone_Shunt_C` 57, `BP_WarriorBug_Corpses_
  Destructible_01_C` 46, `BP_BeachTable_Destructible_*` 65, pickups 1 to 7 per class.
- Change: split the table at the first Pong of the map (load against play). A play-time "local sim" change on an
  actor whose class the host sends rows for becomes a WARN row with the caller; the WARN diff then lists every
  place the client still acts as a second owner, by class, without a run made for it. Diagnostic only.
- Also: journal cap `kC98WorldRows` 512 against 67 rows used at a FED_C3M1 join; no pressure. Keep.
- Proves it: play-time local-sim WARN rows per map listed once; each class gets "host rows cover it" or a card.
- When: build 5, with card 280 (same kind of diagnostic, same run).

## 7. Enemy copies (`enemies.cpp`, `enemy_c103.cpp`, `anim_c104.cpp`; cards 023, 037 to 041, 103, 122)

- Method now: enemies replicate by the engine (movement, health). The mod adds what the game never replicates:
  death edge from health, attack animations, targeting of both players (host only), and blocks the client's
  server-only encounter and spawn logic (card 006).
- Verdict: method right while the copy is alive (one owner: the host through engine replication; the client
  adds presentation). The ownership problem starts at death, where the copy becomes a physics object (entry 1).
  Judged from file headers and card history, not line by line.
- Change: none of its own. Entry 1 covers the corpse; entry 5 covers client-run encounter calls.

## 8. Vehicles and turrets (`vehicles.cpp`, `turret_sync.cpp`, `dropship.cpp`; cards 075, 076, 125, 129, 190, 233, 271)

- Method now: owner by rule: the driving side owns aim, the host owns state bytes, heat and fire results; rows
  per turret with the last row journalled.
- Verdict: method right; the owner is named per value in the file header, which is the pattern the other classes
  lack. Open cards are coverage (run-time turrets 233 and 271, dropship join state 190), not method.
- Change: none.

- Second pass 2026-11-14 (classes 7 and 8): `enemy_c103.cpp`, `anim_c104.cpp`, `turret_sync.cpp` headers and tables read; `enemies.cpp`, `vehicles.cpp`, `dropship.cpp` by their table caps and the WARN rows of run 3 and the 2026-11-13 played log, not line by line. No new method flaw. Small: the card 103 target ring holds 96 enemies by address with oldest-out, so past 96 live enemies an enemy falls back to the nearest player; no peak row says how close a wave map gets (add the peak to the map-end totals). Turret table 32, dropship rows 64 against 22 placed: no full rows in the logs read. Played-log WARNs already queued: card 095 destructible collision names not sendable, card 309 decal D rows past 512 in a tick.

- Third pass 2026-11-14: `enemies.cpp` client branches read (Falling hold, z clamp, ragdoll and static-mesh refusals, client gore roll off) and `decal_c132.cpp` client branches and caps. One flaw: the z clamp (`c132_drift_clamp`) fires again and again on a standing copy, 508 times in one played map (card 328). The others hold: each refuses the client's own start of something the host sends.

## 9. Effects and decals (`fx_c136.cpp`, `fx_c136_net.cpp`, `decal_c132.cpp`, `proj_c132.cpp`)

- Method now: one manager catches every host effect at the engine's spawn entries, sends rows with asset and name
  tables, journals looping effects for late join, ends them by id.
- Verdict: method right (one chokepoint per effect kind, host the only source, by class). Remaining risk is the
  client still spawning the same effect from its own game logic (double effects); the manager's client-side
  suppression was not re-read in this audit.
  Re-read line by line 2026-11-14 (`fx_c136.cpp`, `fx_c136_net.cpp`). The block rule and the host's send filter are the same function (`filter_hits`) on the same inputs, so own/not-own agrees when owner and instigator agree on both sides; a client-driven turret or mech weapon is not the client's own (owner chain one level), so its effects come from the host with latency (queued item). Found: the journal reset at map arm drops every placed effect activated during the load (card 325), and 153 of 911 client rows in run 3 missed their target with no row naming them (card 325 diagnostic). Queue, journal and table overflows are counted and were 0 in run 3. Cue path re-read: the host's cue scope and the client's `CueHandle` decision both use the cue parameters' instigator, so they agree. Mounted weapons: a mech is possessed (card 303), so its effects are the client's own when the weapon's Instigator is the mech; a turret's are own only if the mount possesses it. Both are settled by `client fx own` rows in the first mech and turret run (C2M1 soak), not statically. For the next review: ask for the card 103 ring peak and per-opcode drop counts in the map-end totals. Table peak sweep (2026-11-14): every relay table logs a peak or a WARN at full except the card 103 pick ring (96, oldest-out, falls back to the nearest player, so a wrap degrades safely; wants a `ring wraps <n>` count in the `card103: map:` row). The pace queue drops frames only for a player controller that is gone. `PickupGone` (chan_ops.h) has no sender left, only the enum and a ledger name: dead, remove with the next opcode change.
- Change: none now. Check to add to entry 6's diagnostic: client effect spawns outside a host row, counted by
  asset, so doubles show in the WARN diff.

## 10. Join and travel (`restore_c143.cpp`, `save_c121.cpp`, `begin_c138.cpp`, journals in each class)

- Method now: each class keeps its own journal and replays it on the client's first Pong; resets at map load
  (card 199, card 200).
- Verdict: method acceptable, with one structural cost: N journals with N reset points and N caps (cards 199,
  200, 219, 223 are all "a journal was reset, capped or replayed at the wrong moment"). Not worth unifying before
  the playtest.
- 2026-11-14, user decision: the registry is built before the playtest (card 326), with the per-player row (card 327).
- Change (after the playtest, only if card 200's fix leaves more of this class): one journal registry: each class
  registers its journal with a reset hook and a cap, so reset and replay order are in one place.
- Done (batch 3, card 326): `kSyncMechs` in `mod/src/ubw/sync_registry.cpp` is the one place a state mechanism
  is added: one row with its map end, map arm, join replay, in-place load reconcile and live check. The five call
  sites (map end, map arm, first Pong, `LoadFromSlot`, quit) call only the `sync_*` functions. Card 327's "player"
  row replays every other player's life state through the `Down` frame.

- Second pass 2026-11-14 (`begin_c138.cpp`, `save_c121.cpp` read in full, `restore_c143.cpp` header): the structural cost is live. Card 325 is one more journal reset at the wrong moment (effects, at map arm after the load), and the card 137 reconcile after `LoadFromSlot` rebuilds three journals (c98, c145, c95) while the effect journal and the card 132 persistent-state rows have no reconcile. Traced the same day: neither needs one. After an in-place load every client rejoins (`c143_after_load`), `c132_ext_replay` sends each tracked actor's live value, and the effect journal is swept against live components each tick (`journal_sweep`). The frontend Continue route loads after the arm, so it depends only on card 325's fix. Card 325's sibling sweep produces the table of every journal with its reset point; the registry change waits for that table.

## 11. Pace queue (`ue/chan.cpp`; cards 139, 140, 223, 226)

- Method now: one FIFO per connection for every mod frame, drained per game tick under the engine's bit budget.
- Verdict: wrong shape for mixed traffic, already found in card 226: the budget is per frame (byte rate falls
  with frame rate) and live rows (hits, acks, pose finals) wait behind bulk (join replay, effects). Symptoms in
  cards 223 and 226 and part of the corpse fallbacks in entry 1.
- Change: card 226 step 2 as written (byte budget per second, two lanes). Add from this audit: rows that a newer
  row of the same key replaces (pose stream, turret aim, gib M) are dropped in the queue instead of sent late.
- When: after the playtest, from the card 226 diagnostic row in build 4.

## Summary (senior, 2026-11-13)

One pattern explains most recurring cards: a thing has two owners, and fixes removed symptoms of the second
owner one at a time. By cost of leaving it:

| # | Class | Second owner | Next step |
|---|---|---|---|
| 3 | Client hits | the server copy's own weapon simulation | build 4 card 276 rows decide before or after the playtest |
| 5 | Level scripts | the client's own triggers | card 280 diagnostic, build 5 |
| 1 | Corpses | client physics after the host's final row | one card after the playtest (replaces open parts of 191, 251) |
| 4 | Pickups | client take code before the cut | with card 239 |
| 2 | Gib parts | client physics after the R row | card 279 step 2 |
| 11 | Pace queue | (shape, not owner) | card 226 step 2 |

Method right, no change: hit effects, world ops, enemy copies while alive, vehicles and turrets, effects.
Classes 7 to 10 were judged from headers and card history; a line-by-line read is owed only if they produce a
new card.

# Second pass: leaks (senior, 2026-11-14)

The first pass asked "is each sync class built the right way". This pass asks "what does the game do that no sync
class carries". Static only: the game's 574 `AST*`/`UST*`/`AAD*`/`UAD*` classes (PDB publics) against the mod
source, every game caller of the "player 0" helpers (exe disassembly, 0x144500000 to 0x145B00000), the join replay
list (`c50_join_replay`) against the opcode list, and the digest's coverage. Status per row: **confirmed** (code
read), **candidate** (path exists, exposure on Trooper maps not shown).

## L1. Host world logic that asks for "the player" outside an acting scope (card 321)

Cards 099 and 102 answer "the player" with a client's server copy only when the context belongs to the copy or the
call is inside an overlap, damage, interaction or enemy scope. Tick and timer code with a world context still gets
player 0, the host. Callers found (161 call sites; the ones below have no scope and change gameplay):

| Game function | Reads | Effect for the client | Status |
|---|---|---|---|
| `UADLevelStreamingSubsystem::ProcessVolumes` (0x0455F4B0) | `UWorld::GetFirstPlayerController`, its camera view target | Sublevel visibility (`SetShouldBeVisible`) and persistent actor activation (`UpdateActiveActorsInAllActiveLevels`) follow the host's view only. A client alone in a streamed area stands in a level the host has hidden: no floor collision for its copy, actors there inactive. | confirmed path; logs show `loaded on one side: 0 host, 0 client` in every C1M1 round, with both players together |
| `ASTSpawnPoint::IsOutOfSightOfPlayer` | `IsActorInSightOfPlayer` (player 0) | Bugs may spawn in the client's view. | confirmed path |
| `ASTDoor::MoveDoorToNewPostion` -> `HandleCharactersInMovePath` | `GetPlayerCharacter(0)` | The path sweep handles every player and character pawn (door blocks for a client too). Only the crush step (damage or kill inside the crush volume, door+0x360) reads player 0: a client is never hurt or killed by a door. | card 453 (open, low, after playtest) |
| `ASTTacticalSupportPickup::PickupTacticalSupport` (exe+0x51D2C30) | `GetPlayerCameraManager(pickup, 0)` view point places the dropped old support | Covered: `GetPlayerCameraManager` calls the hooked `GetPlayerController`; the swap runs inside the card 102 interaction scope (`c102_interact`), so the copy's own controller and camera answer. No client swap run has a `card306: host ... support swap` row yet. | covered (cards 099, 102); runtime row owed |
| `USTWeaponUtilityLibrary::RemoveWeaponFromOwnerInventory` (exe+0x524B890) | `GetPlayerCameraManager(weapon, 0)` view point places the dropped weapon pickup | Covered: the weapon's Owner chain reaches the server copy (`c99_copy_of`), so the copy's controller answers. | covered (card 099) |
| `USTCommonUtilityLibrary::StartCameraShakeAtLocation`, `USTCombatUtilityLibrary::PlayCameraShakeWithRadius` | world shakes via `PlayWorldCameraShake` and player 0 | The engine's world shake reaches every controller: client logs c133x6 and c99p3 have `Shake_Generic_Explosion*` rows on the client's camera. | covered (engine) |
| `ASTPlayerCharacter::HandleDamageEvent` -> `PlayHurtCameraShake` | runs where the damage event is raised | The game shakes the remote controller's camera on the host (`server copy` rows), which the client never sees. The client calls `PlayHurtCameraShake` itself on an own Armour + Health drop (shake and force feedback). | Fixed, card 454 (b154, run c207b154a: 7 client shakes, each `(local)`; none in the 6 s after a new pawn). |
| `ASTPlayerCharacter::HealthAttributeChanged` -> damage tint widget delegates | client attribute change | Card 037 calls `HealthAttributeChanged` on the client, so `USTDamageTintWidget::OnHealthLost` fires there. | Covered, card 037 (`card037: client notify` rows). |
| Player Blueprint damage event -> `PlaySound2DFName` (`MS_GORE_Human_Player_*`, `MS_PLAYERBARK_Pain*`) | runs on the host only | The host skips the sound for a server copy (card 160); the client plays nothing. | Fixed in b156/b157 `1e6244b6739bbcd0`, card 455: the host relays the skipped sound to the copy's PC (`ChanOp::HurtSound`), proven c207b156a (8 relays, 8 client plays) and c207b157a; item pickup sounds (`MS_ITEM_Pickup*`) ride the same row, proof run c418b157a. Was: gap (run c447b152a: 29 host skips, 0 client plays). |
| `ASTJumpPadBase::OnBeginOverlap` (exe+0x5162A40) | launches only when the overlapping actor equals `GetPlayerCharacter(0)` | Covered (static, 2026-11-16): the call runs inside `detour_c102_overlap` (`UPrimitiveComponent::BeginComponentOverlap`), so on the host a client copy's overlap answers with the copy; the client runs its own overlap for its own pawn. No run row yet: FED maps walked so far log no pad launch. | covered (static, 2026-11-16) |
| `USTAttackActionRangedFilteredTarget::SelectTarget` (exe+0x51A5C1C) | `GetPlayerCharacter(0)` with the action's owner as context | Covered if the context resolves to an enemy: `GetPlayerCharacter` calls the hooked `GetPlayerController`, which asks `c103_enemy_pc` (nearest player slot). Not confirmed by a `card103` row naming this site; check with `UBWCOOP_C103_LOG` on a map with ranged bugs. | covered if the context resolves to an enemy |
| `ASTPerformanceBenchmarkTool::DoPerfTestEndSampling` (exe+0x51F991F) | only game caller of `GetPlayerPawn(0)` | Not gameplay: benchmark tool. No action. | not gameplay |
| `ASTNonPlayableCharacterBase::HandleDamageEvent` hit-confirm sound (exe+0x51926E2..0x519273C) | host only: when `DamageCauserIsPlayer`, the causer is not yet in the NPC's list at +0xD48 and `USTGameUserSettings::IsHitIndicatorEnabled` (the host's setting), `PlaySound2DFName` -> `UADAudioManager::PlaySound2D` (caller exe+0x52487C4, context the NPC) | Candidate, low: for a client's hit the sound plays on the host unless the card 302 acting scope owns it, and the host's setting decides. No log has the row with an enemy context (one row, `BP_AI_Trooper_SquadLeader_C`, sound null). Check: card 099 log on, hit indicator on, client shoots a fresh bug; expect a host `card160 ... server copy` row for this caller, else card it. | candidate, low |
| `ASTPlayerCharacter::TrySetLastDamagingActor` -> `USTDeathQuoteSubsystem::SetLastDamagingActorClass` | host only (from `HandleDamageEvent`) | Candidate, low: the client's death screen quote cannot name what killed it. Not carded. | candidate, low |
| `ASTFallZonesBase::RepositionPlayer` (deferred, after the overlap scope) | `GetSTPlayerController` | A client that falls is repositioned through the host's controller. | covered: card 338 (`fallzone_c338.cpp`) runs the reposition and end timers in the pawn's acting scope |
| `ASTTeleporterVolumeBase::StartPlayerTeleport` / `EndPlayerTeleport` | `GetSTPlayerController` | Same, for teleporters. | not exposed: `card321: host map` rows show 0 teleporters (and 0 doors, 0 streaming volumes) on all eight FED maps |
| `ASTProceduralDropshipTroops::TickBehaviours` -> `CanTakeoff` | `GetSTPlayerCharacter` | Troop dropship takeoff check ignores the client; the client copy also decides its own takeoff. | card 449 (patch written) |
| `ASTGameplayGameMode::Tick_PlayerCombatTracker` -> `CalculateThreatLevel` | `GetSTPlayerController` | Threat level (combat state, music mode) is the host's alone. | no gameplay effect: the combat level (game mode +0x460) has three readers, all in `USTMusicManager` (`QueryCurrentCombatLevel` 0x05135910 called from `PostLoadedDataFromSave`, `SetToAuto`, `TickMusicModeManager`). Each side's music follows its own game mode; whether the client's own tracker rises in a fight is not shown by any log row |
| `ASTHazardousZoneBase::TickMoveToNewPostion` | `GetPlayerCharacter(0)` | Moving hazards track the host. | covered: card 361 detour `detour_c361_hazard_tick` (host), zone state by the card 132 ext row (`active/move`, +0x2F8/+0x2F9) |
| `USTNPCNavigationQueryFilter::InitializeFilter` | `GetSTPlayerCharacter` | Allies path around the host only. Traced 2026-11-16 (exe+0x5194890): for a pawn querier that passes the squad test of player 0 (interface at player +0x650), the filter takes one object from that player (+0x7E0, then +0x110). One player per query by design; a second player needs a second filter area. Low value: allied NPCs may walk through the client. | candidate, parked (low) |
| `ASTNonPlayableCharacterBase::PlayBark` -> `UADBarkSubsystem::RequestBark` | first player controller | Barks are ranged from the host; the client's NPC copies run no bark logic. | card 451 (patch written): sound already relayed by card 136 (`PlaySound` -> `SpawnSoundAttached`); the range check in `RequestBark` used the host view only |

Already covered, checked in this pass: significance (card 132 `UpdateSignificanceManager`), enemy tickets,
perception, line of sight and facing (enemy scope), wave budget (card 112), dropship boarding and level end,
checkpoint save, score (card 099).

## L2. Presentation the host's scripts start and no opcode carries (card 322)

No mention anywhere in the mod: `USTConversationSubsystem` (StartConversation, AddConversationToQueue,
StartConversationFromList: Blueprint callers only), `USTSubtitlesWidget`, `USTMusicManager::TriggerMusic`
(`ExecuteMusicTrigger` from Blueprint, and `TickMusicModeManager` from the combat state above),
`UADTimeDilationSubsystem::PlayTimeDilation` (ability and Blueprint), `USTDeathQuoteSubsystem`,
`USTDamageTintWidget`, `USTOutOfBoundTriggerComponent` (out-of-bounds countdown and kill), `ASTCompanion`. Each
runs per process. The client gets one only when its own copy of the level script or ability fires, which is the
"two owners" case of class 5: mission radio, subtitles and music can start at different times, twice, or never on
the client. Status: candidate; which of them the client fires itself is not traced.

## L3. Join replay gaps

`c50_join_replay` replays doors, world ops, EndLevel, ext values, corpse poses, decals, dropships, effects, pause,
bug forms, intro state and script rows; objectives, difficulty and weapons go at the same Pong. Opcodes with live
state and no replay found: `Down` (a player already downed or dead), `MechEnter` / `TurretMount` (who sits in
what), `Round`, `HoldDone` (card 133 hold state), live gib parts, grenades in flight. A client reaches every map
after the host, so each map start is a late join. Status: confirmed absent from the replay list; whether engine
replication covers the mech and turret seat is not read.

## L4. What the digest cannot see

The digest (card 132) compares placed non-replicated actors, child actors, relayed copies and objectives. It does
not compare: NPC state beyond engine replication (form, anim, corpse), player state (health, armour, ammo,
weapons, downed), level-script variables (class F, card 310), vehicle seats, sublevel visibility (a level loaded
on one side is skipped and only counted), sound, HUD and minimap. Every playtest bug since build 4 (314, 317
before the round, 318, 319, 297) sat in one of these. The digest is also log-only: nothing heals from it except
card 256's rest rows.

## L5. Transport

- A malformed frame, an opcode with no handler and a frame for a side with no table entry are dropped. A count
  per opcode and side at map end is owed, so a drop shows in the WARN diff.
- The host handler table has 21 opcodes; `PickupGone` is still in the enum and ledger table with no sender or
  handler (dead since card 239's rework): remove.

## L6. Blueprint callers of player helpers (card 582, 2026-11-21)

L1 swept native callers only. Card 582 swept all 87 Blueprint packages that call a player helper (346 call sites).
Most are covered: ability, weapon and ABP contexts resolve to the right player through the c99 Instigator, Owner and
acting-scope remap, and HUD, audio volume and minimap readers are local presentation on each side. Gaps: 11 placed
world actors with no player context read player 0 on the host (mortar zone target, EM marker threat, building hit
sound, pickup tracker binds, pylon line of sight, spawner initial overlap, plus four to verify). Fix class: one
condition table keyed by (class, function) giving the player-0 rule (any-overlap, nearest, fan-out, causer-player),
riding the card 302 acting scope. Level-script camera, sound and control-rotation calls still need verdicts.

## Practices from other games (senior, 2026-11-14, web research; sources in the chat report)

| Practice | Who | Ours | Action |
|---|---|---|---|
| State and events are separate. State (anything a late or newly relevant player must see) is a current value delivered eventually; events only explain a change and may drop. A one-off RPC is never the only carrier of a lasting result. | Halo: Reach, Unreal (replicated variable + OnRep, not multicast) | Many results travel as one-shot opcodes; the join replay re-sends a hand-kept subset (L3). | Card 323: class every opcode state or event. Every state opcode has a current-value row the join replay and the digest both read. Card 322 follows the same rule for presentation that lasts (music, weather, lights). |
| Match start waits for every player. | Unreal `ReadyToStartMatch`, `NumTravellingPlayers` | World runs from the client's first Pong (card 096/120). | Card 320 is this gate. |
| Screens that stop the game go to every player in one order. | Vampire Survivors online (host UI events as ordered commands) | Each side shows its own screen at its own time. | Card 320 sibling sweep. |
| One authority runs enemy AI; others show copies. | Most co-op games; Nitrox per-entity ownership, Skyrim Together and Vampire Survivors split ownership between clients | Host runs all AI. | Keep. Split ownership buys bandwidth we do not need with 2 players and costs hand-over bugs. |
| Movement is unreliable, newest wins, sent by priority; lost rows are not resent. | Halo: Reach, Gaffer on Games, Nitrox | See L5. | Check under L5 that no movement row waits behind a reliable-ordered row. |
| Slow periodic correction instead of exact tracking. | Vampire Survivors (local 60 Hz, 1 Hz network correction) | Digest detects mismatches; it does not repair. | Proposed: a digest mismatch triggers a resend of that class's state rows (after card 323 gives every class such rows). |
| Cosmetic physics is not synced past its start. | Halo: Reach (ragdolls: death state only, 10 to 12 % bandwidth freed), Nitrox (creature paths local, deaths synced) | Corpse pose (34 cards) and gib parts (16 cards) are synced in full by user rule. | No change; noted as the costliest class against practice. |
| A full state send exists and is used at connect and after heavy loss. | Source (full snapshot at game start or after seconds of loss; `cl_fullupdate`), Quake 3 | Join replay only. | Same proposal as the digest repair: one "send this class in full" path serves join, rejoin and repair. |
| Whole-state hash every second detects desync; lockstep games cannot recover. | Synchronous RTS engines (Demigod) | Digest by class; we are state-sync, so repair is possible. | Keep the digest per class (names the class, unlike a whole-state hash). |
| Cutscenes: no engine-level sync; games start them by one server order and add their own late-join position. | Unreal forum practice, `ALevelSequenceMediaController` server start time | Card 135 relays the start. | Card 320 sibling sweep: a joiner mid-cutscene starts at the host's position. |
| One player owns quest progress. | Skyrim Together Reborn (party leader) | Host owns objectives; clients act through the acting scope. | Keep; card 301 audit covers the gaps. |
| Rules that mean "any player" never read player 0. | Follows from the above in every engine | L1. | Card 321. |

## Ranked

| # | Leak | Card | When |
|---|---|---|---|
| 1 | L1 streaming, spawn sight, door path (host-only "the player" in tick code) | 321 | trace now, code behind run 3 |
| 2 | L2 radio, subtitles, music, time dilation | 322 | diagnostic rides the 321 build |
| 3 | L3 join gaps (Down, seats, Round, hold) | 323 | with card 320 (title-screen gate removes most late joins) |
| 4 | L4 digest coverage: player state and seat rows | in 323 | after 320 |
| 5 | L5 drop counters, dead opcode | in 321 build | small |

## Third pass: session lifecycle (2026-11-15, senior, static)

Scope: the states a session passes through (connect, lobby, map load, title-screen hold, mission, mission end,
next mission) against the events that can hit each one. Sources: `intro_c135.cpp`, `restart.cpp`, `menu_c350.cpp`,
`net_drop.cpp`, `chan_ops.cpp` (card 139 gate), `fire.cpp` (`detour_logout`), and the c346/c355 run logs.

| State | Event | Outcome | Owner | Verdict |
|---|---|---|---|---|
| Lobby (hangar) | host picks a mission | seamless travel, client kept | `c346_host_seamless` | ok, run |
| Map load after seamless travel | host continues before the client's first Pong | hold released, mission runs while the client loads | `c320_try_release` | **defect, card 364 (fix written)** |
| Title-screen hold | client does not confirm in 60 s | host to menu, seamless | `c344_no_confirm` | ok, run |
| Title-screen hold | confirmed or waited-for client leaves | host to menu | `c320_logout`, `c320_step` | ok, run |
| Title-screen hold | client leaves while still loading | host went on alone | `c320_logout` | **defect, fixed with card 364** |
| Mission | client stalls over 20 s | host closes it, client to menu with the reason, host plays on | `net_drop.cpp` | ok (rule "never alone" covers the hold only) |
| Mission | client pause-menu QUIT | client leaves, no reconnect; host plays on | `c45_client_browse` | ok |
| Mission | host pause-menu QUIT | hard path, `listen` kept, client drops and follows to the hangar | `c45_host_browse`, card 045 follow | ok, run before card 346; not rerun since |
| Mission | all down, RESTART CHECKPOINT | in-world reload, no travel | game, card 045 screens | ok, run |
| Mission | all down, RESTART MISSION | seamless travel to the loaded map | `c346_host_seamless` | ok, run `c373b57b`, again `c373b70` (card 365 closed) |
| Mission end | client continues first | its Browse is not run, it waits for the host | `c345_client_stays` | ok, run |
| Mission end | host continues | seamless travel to the hangar | `c346_host_seamless` | ok, run |
| Any | host process ends | client times out, menu with the reason | `net_drop.cpp` | ok |

Rows added 2026-11-15 after cards 365 to 373 (the table above missed every client-side local action on the death
screen and the in-place load):

| State | Event | Outcome | Owner | Verdict |
|---|---|---|---|---|
| Mission | all down, host RESTART MISSION | seamless travel, client kept | `c346_host_seamless` | ok, run `c373b57b` |
| Mission | all down, host Exit | host to the title screen, client carried | `c346_host_seamless`, card 350 options | ok, run `c373b57b`, Phase C |
| Mission | client dead, client Exit | client leaves, no reconnect; host not stranded; rejoin by the lobby join | `c45_client_browse` | fixed, card 373 (run `c374b58`) |
| Mission | in-place load, client pawn not in the save | Reload `none`, client rejoins, no Restore | `c143_send_reload` | fixed, card 371 (run `c371b57`) |
| Mission | client's own save load or save (pause menu) | skipped on the client | `detour_c121_load`, card 121 | ok, static |
| Mission | client's own local OpenLevel (pause-menu restart) | superseded by the card 376 row below (client RESTART MISSION): the button and confirm are dropped, a Browse that still comes is refused with Success | card 376, card 154 | pass, run `c376b63` phase C |
| Mission | host pause-menu QUIT | seamless, client kept (card 369) | `c346_host_seamless` | ok, run |
| Mission, host Dead (all down) | a player joins | its copy goes Out on the host, the client gets the death screen; the host's load stands both up | `c374_late_copies` | fixed, card 374 (run `c375b60`) |
| Mission | host pause-menu RESTART CHECKPOINT, both alive | in-place load, Reload, one client transition | card 294, card 143 | pass, run `c376b60` phase A |
| Mission | host pause-menu RESTART MISSION, both alive | seamless travel, client kept | `c346_host_seamless` | pass, run `c376b63` phase D |
| Mission | client pause-menu RESTART CHECKPOINT | load skipped on the client (`file_io_success` false); what its screen does after is Blueprint, not traced | `detour_c121_load` | pass, run `c376b60` phase B |
| Mission | client pause-menu RESTART MISSION | not run on the client (button and confirm dropped); a Browse that still came is refused with Success | card 376, card 154 | pass, run `c376b63` phase C |
| Mission | host pause-menu LOAD GAME (a save, any map), client connected | Load stores deferred params and travels with `?FromSave`; the save is applied only from `USTGameInstance::LoadComplete`, which a seamless travel never calls | `c346_host_seamless`, `c375_seamless_load_complete` | **defect (same cause as the hangar row below); fixed by card 375 in b65 (hangar route passed in `c377b65`); this pause-menu route itself never run; same-map load from the pause menu is the in-place path (run many times, last `c379b70`); other-map route is scenario `c378`, run `c378b65` proved nothing, 3-run budget not reopened** |
| Hangar | host CONTINUE / LOAD GAME, client in the session (persistent lobby) | Load runs in the hangar and only stores deferred params; after the seamless travel nothing applied them (run `c377b63`: host parked in the dropship, state 2, 150 s). Fix: host calls `LoadComplete` at the seamless return; Reload only to ponged copies | `c346_host_seamless`, `c375_seamless_load_complete`, `c143_after_load` | **fixed, card 375 (pass `c377b65`, b65); residual: the client's saved pawn state is not restored, it spawns beside the host** |
| Mission | host in-place load of a save taken with more players than now | the absent player's saved pawn had no PC at its saved list index and stayed, armed | `c380_orphan` | fixed, card 380 (run `c379b67`) |
| Hangar / any | client starts a level from its own menu (`TransitionToLevel`, `TransitionToLatestLevel`) | loading screen, then the card 154 refusal: stuck "LOADING..." (predicted, card 376 sibling); fix: not run on a client in the host's session | `detour_c381_transition` | fix in b67, static, never run (no driveable click path) |
| Any | host save while the PC list is [client, host] (after a seamless travel, before the client's PC swap) | host row saved with index 1, mispaired on a later load (predicted; 0.9 s margin in `c378b65`); fix: host PC to slot 0 before every save | `detour_c121_perform_save` | fix in b67, static; window not yet hit in a run |

One owner decides each row; no state has two writers. The hold is the only state whose rule depended on an op that
another gate (card 139) can refuse; that is the card 364 defect.

Earlier findings still open and unchanged in build 6: cards 226, 239, 276, 279, 280, 301, 321, 322 part B, 323. (2026-11-22: all of these have closed since, except 301.)
None was re-read line by line in this pass.

### Third pass, player state (card 043 family), static

| State | Event | Result | Verdict |
|---|---|---|---|
| Host Downed or Out, client Up | client Logout or timeout | `c43_logout` drops the client's entry; roster is 1; the all-down decision needed `players >= 2`, so the host stayed down for good | fixed, card 366 (accepted on b51 `f46ae0eb6b1386d6`, run b51b; not in play build 6) |
| Client Downed or Out, host Up | client Logout or timeout | entry and holds on that pawn removed (`c43_logout`; `c43_entries_step` "pawn gone" as the backstop) | ok |
| Any player Downed | map end, seamless travel, respawn in place | `c43_map_end` (registry "player" map end) clears entries and holds; no Logout needed | ok |
| Host Downed, client goes down | — | `c43_host_step` nobody Up, `c43_all_down`, host real death | ok (proven, card 043) |
| Client Downed | client's own `HandleDeathEvent` | skipped on the client once a Down frame was seen | ok |
| One player Downed or Out, the other Up | host pause-menu RESTART CHECKPOINT (in-place load, not all down) | `c45_respawn_check` runs no per-map reset (`stale` false), so `c43_map_end` is not called; the old pawns are gone, `c43_entries_step` drops each entry ("pawn gone"); a row saved at the held Health gets the revive Health (`c143_after_load_asc`); the client rejoins on the Reload and starts from a fresh map state | ok, run `c383b67` (client Downed, host Up; all 6 predictions held) |
