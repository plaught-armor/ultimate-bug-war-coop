# Card 098: One mechanism for host world changes

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: report in 2026-11-05 (junior). DLL `7f93daf100d2ee0c`.

Read:

- `docs/STATE.md`, the sync rule and the engine facts (the game replicates nothing natively; `bNetStartup` on placed
  actors; `CPF_Net` auto-registration);
- `docs/playtest-2026-11-05.md`, items 5, 6, 7, 8 and the card queue;
- cards 056 (PickupGone relay), 042 (Gib relay), 054 (placed actors, roles local 0 remote 3 on the client), 095
  (CollisionOp relay and journal).

Repo root: `/path/to/ultimate-bug-war-coop`. Play DLL base: `b86475badfbc9520` (card 097).

## Why

User rule (2026-11-05): everything about the world and the objectives must be the same for every player. Today each
host-only world change gets its own relay (056 pickups, 042 gibs, 095 collision), and each missing one shows up as a
new playtest bug: bug holes destroyed on the host only (item 8), the bombing run seen by the host only (item 6), the
support flare grant lost (item 5), the capture-the-flag wall (item 7). Card 097 covers objectives. This card covers
the world, with one mechanism per class of change instead of one relay per actor.

## Do

1. **Measure (one host run, log only).** Play the FED_C1M1 tutorial through the bombing run and into capture the flag,
   then the first LOS bug hole in the second mission (use `objective complete` only to skip ahead where the real path
   is not under test). On the host, log every world change to placed actors and every spawned actor that matters to
   play: destroy, hide/show, collision, activate/deactivate, attach, Blueprint timeline or door state, and spawned
   actor classes (strike aircraft, bombs, explosions, flares). One line per (class, kind of change), with a count.
   Classify each as: already relayed (which card), missing.
   **Enemies too** (user, 2026-11-05: "the enemies also need to be synced"). Enemies already replicate as actors
   (cards 023, 037 health, 038 death, 039/042 gibs, 041 attack animation). In the same run, compare host and client for
   every enemy: spawned on both, position drift, animation (move, attack, hit reactions, death), health, death and
   corpse/gib, and enemies that exist on one side only. List each gap in the same table.
2. **Pick the mechanism, per class, and say why in the Report.** Two candidates to test, not to argue:
   - **Native replication:** on both machines, at map load and before the actors are used on the network, give the
     placed actors (and spawned classes) that need it `bReplicates`. Net-startup actors are matched by name, and a
     host destroy then reaches the client through the engine. Check what this costs (channels, relevance, the client's
     roles flipping) and whether the client's own copy of the script then fights it.
   - **Generic relay at choke points:** one host hook per kind of change (destroy, spawn of a listed class, hidden,
     collision as in 095), journalled per map and replayed on first Pong, keyed by placed name plus location.
   Prototype the chosen one on the two cases that fail today, plus the worst enemy gap found: a bug hole destroyed on the host disappears (in its
   destroyed state) on the client; the bombing run's strike shows on the client.
3. **Fold in** the existing per-actor relays (056, 095) where the new mechanism covers them, so there is one path per
   kind of change. Do not remove a relay the new path does not cover.
4. **Proof**, one combined scenario run (FAST=1, plus `objective complete` to reach the second mission if needed): the
   host destroys a bug hole and calls the bombing run; both show on the client; a client that joins after the bug
   hole died sees it destroyed. rc 0, ERROR/FATAL 0, no new dump.

Stop when the fix builds and that one run shows it; the user will playtest.

## Done when

- The Report has the measured table (class, change, count, covered or missing), the mechanism chosen per class and
  why, the fix, the proof run and the DLL hash.
- The Report ends with "Report complete."

## Report

### Measured table

From runs c98m2 (FED_C1M1, tutorial steps by command, capture the flag), c98m3 (FED_C2M1, first LOS bug hole, both
sides `UBWCOOP_ENEMYAUDIT`) and c98m4 (FED_C2M1, `kill actor` on the bug hole). Counts are the host's card098 tally
per map (`card098: host tally ...`); CDO (`Default__`) collision writes at map load are left out.

| Class | Change | Count | Placed / replicates | Covered |
|---|---|---|---|---|
| `BP_SatchelBuilding_Plasmabug_C` (the bug hole, named `BP_DestructibleSpawner_Bugnest_HumanGameplay_C_*`) | death: Blueprint event `ReceiveOnSatchelExploded` | 1 per hole | placed / no | **was missing; now card 098 WorldOp** |
| `BP_SatchelPlacementSite_C` | collision | 19 | placed / no | 095 CollisionOp |
| `BP_SatchelBuilding_Flakbug_C` | timeline `TurnInPlace` | 1 per turn | placed / no | missing (cosmetic turret turn; see open items) |
| KillableCrates, ammo, grenade and weapon pickups, `BP_SatchelItemPickup_C`, `BP_SecretPickup_C`, ExplosiveBarrel | collision; pickup disable | per item | placed / no | 095 CollisionOp; 056 PickupGone |
| `WarriorBug_Corpses_Destructible` | collision | 218 | placed / no | 095 CollisionOp |
| LandingBlocker, `BP_TacSuppPickup_*` | collision, hide | per item | placed / no | 095 CollisionOp; 056 PickupGone |
| Doors (`ASTDoor`) | open, close, active | per call | placed / no | 095 DoorOp |
| AI bugs and troopers | spawn, move, health, death | 53 on the host's map | spawned / yes | native replication (023, 037, 038, 039/042, 041) |
| `STGorePart*` | spawn, hide, collision | ~100 | spawned / no | local effect; 042 Gib relays the trigger |
| `BP_Projectile_Plasmabug_C` | spawn, destroy | per shot | spawned / no | local effect of a replicated bug's attack (041) |
| GrenadeVisuals, `ADProjectile` | spawn, explode | per throw | spawned / no | 074 |
| `ADAIController`, `BP_ObjectiveMarkerActor_C` | spawn | 50, 11 | spawned / no | server-only and local UI; no sync needed (objectives: 049/097) |
| Bombing-run strike | spawn | not measured | ? | **open**: the tutorial pickup never appeared in a scripted run (below) |

Enemies (c98m3): the host listed 53 enemies, the client 4 (Morita troopers at the LZ). The client's `warp` sets its
own pawn's location, but the server corrects it back: the client log shows `warp ... SetActorLocation 1` and its
screenshots stay at the LZ (c98p1 `client-after`, `client-rejoin`). So the client never left the LZ, and the 4 are the
enemies relevant to its server copy there. This is net relevance working as designed, not a replication gap; the
enemy comparison must be redone with the client walking (or the host warping the server copy) to the hole. Health:
`CurrentHealth` read 0.0 on both sides, so that field offset is wrong for these classes and proves nothing.

### Mechanism per class, and why

- **Placed, non-replicated actors whose change is a Blueprint event (the bug hole):** generic relay at the native
  choke point. The bug hole is a satchel building; grenades, rifle fire and `InstaKillTarget` do nothing to it (c98m3,
  c98m4). It dies when `ASTSatchelPlacementSite::Explode` broadcasts `SatchelExplodedEvent`, bound to
  `ASTSatchelBuildingBase::OnSatchelExploded`, a thunk to the Blueprint event `ReceiveOnSatchelExploded(bool
  bIsPrematureExplosion)`. Native replication (`bReplicates` on the placed actor) would only carry replicated
  properties and the actor's destroy; the hole's destroyed state (meshes, effects, spawner off) is built by the
  Blueprint event on each machine, so replication cannot carry it. One hook on that event covers every satchel
  building (bug holes, Flakbug and Plasmabug buildings) on every map.
- **Placed, non-replicated collision, doors and pickups:** keep 095 and 056. They are already choke-point relays of
  the same shape (host hook, per-map journal, first-Pong replay, name plus location key); the new WorldOp reuses
  their fanout (`c95_send_all`) and lookup (`c95_find_nearest`). Nothing was removed: the WorldOp path does not carry
  collision or pickup state, so folding them would only rename the wire op.
- **Enemies:** native replication, unchanged. The measured gap was the client's warp being undone (above).
- **Spawned local effects (gore, projectiles):** stay local, driven by the replicated or relayed trigger.

### Fix

- `mod/src/ubw/world_c98.cpp`: `detour_c98_satchel` hooks `ASTSatchelBuildingBase::ReceiveOnSatchelExploded`
  (symbols.txt, hooks_ubw.cpp). On the listen server, for a placed, non-replicated building, it captures name and
  location before the original (the event may destroy the actor), calls the original, journals the row (64 per map,
  keyed by actor pointer and kind) and sends `WorldOp "S name x y z premature"` to every ponged client.
  `c98_receive_world` calls the original on the client's nearest same-named satchel building. `c98_replay` sends the
  journal on a client's first Pong (weapons.cpp, next to 056 and 095). `c98_arm_map` resets it and logs totals.
- `ChanOp::WorldOp` added (chan_ops.h/.cpp). `C95Fanout`, `c95_send_all` and `c95_find_nearest` moved out of
  door_c95.cpp's anonymous namespace and declared in hooks_internal.h.
- Test command `kill actor <name>` (cmdfile, drive.py, testrun.cpp): on a satchel building it calls
  `ReceiveOnSatchelExploded(false)` through the hooked path, as a satchel's explosion does; otherwise
  `InstaKillTarget`.

### Proof run

**c98p1** (FAST=1, FED_C2M1 via `UBWCOOP_TRAVEL`, tools/scenarios/c98-hole-relay.txt): host rc 0, client rc 0,
FATAL/ERROR 0, WARN 0, no new crash dumps.

- Host: `card098: host t=8399334 ReceiveOnSatchelExploded(0) on BP_DestructibleSpawner_Bugnest_HumanGameplay_C
  (BP_SatchelBuilding_Plasmabug_C), relay 1`, then `world S BP_DestructibleSpawner_Bugnest_HumanGameplay_C_7 111727
  2550 11170 0: sent to 1 of 1 clients`.
- Client, 26 ms later: `world S ... (BP_SatchelBuilding_Plasmabug_C): applied ReceiveOnSatchelExploded`.
- Late join: the client disconnected and reconnected (`card045: client t=8413353 reconnected`); the host logged
  `world journal replay to BP_PlayerController_C (first Pong of the map): 1 of 1` and the client applied the event on
  its fresh copy again (t=8416452).
- Screenshots: the client's could not frame the hole, because the client's warp is undone by the server (above), so
  the visual check of the destroyed hole on the client is left to the playtest.

### Open

- **Bombing run strike: not measured, not relayed.** Two scripted tries failed to make the tutorial support pickup
  available: c98m2 (E on the pickup, no `DisablePickup`; it is hidden from map load) and c98b1 (completing
  TacSupport_SO2..SO41 by command does not make the level script show it: no `SetObjectiveVisibility(TacSupport_SO5)
  bools 11` in 30 s). The playtest shows the script reveals it only on its own sequence. Next step: the user (or card
  100's run, which takes the real path) calls the bombing run once on the host with this DLL; the card075/098 spawn
  lines then name the strike classes, and the relay is a spawn-of-listed-class row on the same WorldOp path.
- `BP_SatchelBuilding_Flakbug_C` turret timeline: cosmetic, not relayed.
- Enemy comparison to be redone with the client actually at the hole.

Report complete.
