# Card 556: dropped pickups float, rifles stand upright (client view)

Playtest 2026-11-20 on b366 (`e62e76f63a05d88f`), FED_C2M1. User report with screenshot
(`Screenshot_20261120_190645.png`, client, about 100575716 ms in `UBWCoop-menu-20261120-184517.log`): "this should be
on the ground but is floating". A medium health pickup and Morita rifles stand in the air, the rifles upright.

## What the log and code show

- The copies are card 104 'P' row spawns (`c104_client_spawn`, `world_c98.cpp:1818`): spawned at the host's spawn
  point `r.at` with a zero rotation (`const Vec3 rot{}`), then `c317_stop_copy_movement` stops any projectile
  movement. Nothing later moves a pickup copy to where the host's came to rest. The zero rotation explains the
  upright rifles; the spawn point explains the height if the host's pickup fell after spawning.
- card341 round 9 rows: `BP_WeaponPickup_MoritaMk1_C` host z 6950, client z 7000, passed as "within tolerance". The
  tolerance hides a 50 uu float.
- Medkit, line 36877: `card104: client t=100566491 copy BP_HealthPickup_Medium_C swept 1 uu to the host's last point
  (173524, 24508, 7720): moved 0, still live`. The 'D' row's last point equals the copy's spawn point, so either
  the host's medkit also never fell (vanilla float on both screens) or the 'D' row's point is the spawn, not the
  rest.

## Question for the user

Does the host see the same medkit and rifles floating?

## Fix direction

If only the client floats: the host sends the pickup's settled transform (location and rotation) once its root
stops (the card 317 rest row the thrown objects already use), and the client copy takes it, by class for every
dropped pickup. card341 compares pickup z with a tolerance under the pickup's own height, so a float fails the check.
If both float, it is vanilla drop placement and out of scope for sync.

## Prediction

After the fix, card341 rows for every dropped pickup show host and client within 5 uu and the same yaw, and a
client screenshot shows them on the ground lying flat.

## Host log (`ultimate-bug-war-coop-public/logs/UBWCoop-menu-20261120-184651.log`, host t = client t + 241024698)

- Card 256 sends a host rest row for each pickup. The rows near the screenshot spot, at 341543281-284 (client
  100518583): 8 pickups at about (174000, 23800), with z from 7773 to 7902, each `after 0 ms, 1 rows sent,
  simulating 0, still`. The medkit 251014 rests at (174029, 23793, 7789), the same point as its spawn and destroy.
- These pickups do not simulate physics on the host. They stay where they spawned, stacked 130 uu apart in height.
  The host most likely sees them floating too (vanilla, same on both screens).
- The client applies rest rows with the host's rotation (`world_c98.cpp:2160`: `g_set_world_rotation(root, rot)`).
  Earlier rows show tilted rifles, for example Morita rot (75.00, 169.65, -0.00). So an upright rifle on the client
  matches the host's own rotation for a pickup that never simulated, not the card 104 zero spawn rotation.
- Elsewhere on the map, simulating pickups rest at z 6895-6975 after about 1.5 s (card 256 `asleep` rows).

Next: confirm that vanilla single player leaves these drops in the air (what spawns them with simulation off), and
answer the user question above. If vanilla floats them, the fix belongs in the perf/base-game mod family, not in
the sync.

## b369 (`4d7adc330945a079`, build-b5 copy kept), not run yet

No sync change. The host log shows these pickups never simulated and rest at their spawn points on the host too,
so the float is the same on both screens (likely vanilla). If vanilla single player floats them, this is a perf/base-game
mod card, not a sync card. The user question above still stands.

## User answer 2026-11-21: the host sees these pickups on the ground

So the float is a sync gap, not vanilla. The host rows (`simulating 0`, still at the spawn point, z 7773 to 7902)
read only the actor's root (`c104_settle_one`, `world_c98.cpp:2257`: `c104_root_simulates(root)` and
`actor_location(actor)`). The host draws the pickup on the ground, so the visible part is not where the root is.

Lead hypothesis (check statically first): the pickup's visible mesh is a child primitive that simulates physics on
the host. A simulating child leaves its parent in world space and falls, while the root stays at the spawn point. The
rest row then sends the root, and the client copy (no child simulation) shows the mesh at the spawn point, in the air.
Other path to rule out: a ground snap in the pickup BP (BeginPlay trace or timeline) that runs on the host only.

Next: list the components of the floating classes (the medkit and the Morita pickup), name the one that simulates or
moves, and send that component's world transform in the rest row (and moving samples), by class for every relayed
pickup. Prediction: host rest rows for these pickups then show z near the ground (about 6900 on that spot, the
`asleep` rows elsewhere), and a client screenshot at the spot shows them lying flat.

## Static check result (2026-11-21): the child-body lead is disproved; b434 dumps components

- The rack pickups' roots are StaticMeshComponent (the root is the mesh). There is no separate child body for a
  simulating mesh to leave behind.
- C2M1 logs: the pickups sat in two resupply racks (251379 at (173992, 23778, 7757) and 250607 at (173519, 24546,
  7753)). The host destroyed both racks, the pickups then simulated and fell on the host (Incendiary 'D' at z 7705,
  medkit 250475 'D' at z 7720). card341 #250517 has host (173450, 24550, 7700) and client (173500, 24550, 7700). Client
  destroy sweeps were within 1 uu, and the Moritas had no sweep rows.
- So the root positions agree on both sides. The floating visual is another component, or a visibility difference.
- b434 (`8c555354aca54373`) logs a `card556:` row for each component of every `Pickup_` class (root first, then
  BlueprintCreatedComponents): world location, simulating, bVisible, bHiddenInGame. Rows come at spawn, at the
  simulating rest, and at destroy (host); and at rest applied and destroy (client). Scenario
  `tools/scenarios/c590-c3m1-b434.txt` calls a resupply rack, and the host shoots it until the pickups fall.

## b434 run c590b434a (FED_C3M1, 2026-11-21): rack pickups fall the same on both screens

Logs: `logs/c590b434a-host.log`, `logs/c590b434a-client.log`.

- Each pickup class has two components: the root `StaticMeshComponent` (the visible mesh) and
  `ADPickupsCustomSoundComponent` (no transform, no physics). There is no other visible part.
- At spawn in the rack, the host's pickups do not simulate (`simulating 0`). Card 256 sends one rest row at
  0 ms (`still`). The client copies simulate (`simulating 1`) but are put to sleep at the host point.
- The rack opened on the host at about 182714 s. The pickups simulated and settled at z 322-341. The card 287
  moving samples reached the client, and each copy glided to `at rest 0.0 uu from the host's`. Host and client
  destroy rows agree within 1 uu (for example, Pump 234844 at (-69569, 17084, 322) on both).
- So in C3M1 a rack's pickups fall and rest the same on both screens, and the C2M1 float is not reproduced.
  The rows still to compare are the C2M1 Morita drops, which had no moving samples in the playtest log.

## b437c run c556b437ca (FED_C2M1, 2026-11-21): trooper Morita drops rest the same on both screens

Logs: `logs/c556b437ca-host.log`, `logs/c556b437ca-client.log`, drive `logs/c556b437ca.drive` (passed, rc 0 both).
Scenario `tools/scenarios/c556-c2m1-b437c.txt`: three Moritas killed by code (`host kill actor`), each drop then
watched on both sides.

| host id | host spawn z | host rest (card256) | client rest applied (card556) | moved | card287 |
|---|---|---|---|---|---|
| 239622 | 8731 | (232047, 43332, 8628) after 1483 ms | (232048, 43333, 8632) | 4 uu | 8 samples, at rest 0.0 uu |
| 240927 | 8730 | (231803, 43119, 8628) after 1805 ms | (231803, 43118, 8629) | 1 uu | 9 samples, at rest 0.0 uu |
| 242085 | 8731 | (232146, 43935, 8628) after 1335 ms | (232147, 43935, 8631) | 3 uu | 8 samples, at rest 0.0 uu |

- Each drop spawns simulating on the host (`card107` runtime pickup, callers 0x3DCB50E < 0x51A71BD < 0x51A6E87), falls
  about 100 uu and sleeps. The client copy gets the moving samples (no "no moving samples" row) and the rest row,
  and card317 applies the host's rest rotation (actor yaw 152.9, 10.3, 75.8).
- Shots `c556-drop{1,2,3}-{host,client}` (prefix2 Saved/Screenshots/Windows): no rifle in the air on either side. The
  look gate's traces found no hit on the pickup (0 of 26) on either side, so the shots do not frame it closely.
- So a drop from a trooper killed by code does not float on the client. The playtest float is still not reproduced.
  The playtest drops came from troopers killed by bugs during the C2M1 landing, and that log had no moving samples
  for them (the card287 cap). The remaining differences are the kill path and the moving-sample cap; no fix has a
  log row naming a path yet.
- Correction (senior, 2026-11-21): kC287Lines (48 per map) caps the card287 log rows, not the samples. The playtest
  log's missing card287 rows say nothing about whether samples were sent.

## Late-copy trace (static, 2026-11-21)

- Q1, late creation: a copy made after the host pickup moved is built from the journalled 'P' row (the spawn-time
  point, zero rotation). The journal's latest 'M' row for that actor follows in replay pass 2 (c98_replay_part): the
  host's rest pose if the pickup is asleep, otherwise its last moving sample. So a late copy already ends at the host's
  current transform, unless the journal is full (card308 WARN; none in the playtest).
- Q2, an asleep pickup: it sends no more rows unless it moves more than kC104KickUu (5 uu) off its rest point, or a
  card341 digest mismatch fires c256_heal_rest. A non-simulating root is checked every tick (c104_settle_one): any game
  move gives moving samples, then a rest row.
- The playtest does not fit the late-copy case. The client was ponged on C2M1 long before the drops (about 400 s), so
  every copy was created live. The Morita trooper drops rested on the host at z 6975 (9 rows each). card341's host
  6950 against client 7000 is the 50 uu digest grid at z 6975, not a float.
- New lead, the resupply rack pickups: BP_ResupplyRack_Level1_C is a deferred spawn (host 341543279) with six pickups
  (Morita 251256 and 251201, ammo 251227 and 251141, frags 251123 and 251067) at z 7773-7902. On the host they are
  `simulating 0, still`, one rest row at spawn. The client copies were built with `simulating 1`. The host rack was
  destroyed at host 341546869 (caller exe+0x37DC6BD). After that the host sent no row for 251256, 251201 or 251141, so
  the host pickups did not move: they stayed at rack height on the host too. The client game later tried to destroy
  those copies through the same caller exe+0x37DC6BD (client 100613723, refused by card132). The host destroyed them
  at client 100695996.
- So if the floating items in the playtest shot were the rack pickups, they float on the host as well (game logic, or
  the host's pickups have nothing under them after the rack goes). No fix yet: no row names a co-op path.

## b439 rack diag (2026-11-21)

- Build b439: dwmapi 49f59928ea04b534, ubwperf 13cc472aebdac59c. world_c98.cpp c556_rack_destroy (from
  detour_c98_destroy on the host, from c104_client_destroy on the client) logs `card556: <side> ... resupply rack ...
  destroyed at (...)`. It then logs one `rack pickup` row per relayed pickup (host: g_c104_relayed; client:
  g_c104_copies) whose owner is the rack, whose attach parent is the rack, or that lies within 400 uu of it. Each row
  gives the actor location, the root world location, simulating, IsAnyRigidBodyAwake and the attach parent. The host
  rows also give the last rest row z, rested and the row count. c556_rack_step logs the same row 2 s later, plus a
  c556_dump of every component.
- Scenario tools/scenarios/c556-c2m1-b439.txt, run c556b439a. The host calls Resupply at the playtest rack spot
  (host 173992 23778 7757).
- Prediction, written before the run: on the host, root z drops to the floor under the rack (about 6900 at the playtest
  spot) within 2 s. The host's `rows` and `last rest row at z` stay at their spawn values, so no 'M' or rest row is
  sent. The client rows stay at rack height. If the host z does not drop, the float is vanilla on both screens, and the
  user's answer was about other items. If the root z holds but a component row moves, the drawn mesh moved without the
  root.

### c556b439a result (2026-11-21)

- The game spawned BP_ResupplyRack_Level0_C, not Level1. The test profile's ResupplyLevel is 0, and the rack level
  follows the pawn's ResupplyLevel (FedScore upgrades). So the run did not reproduce the playtest's Level1 rack. The
  rack held 4 pickups (Morita Mk1, MoritaSmall ammo, Incendiary grenades, Medium health); all matched by "near"
  (owner and attach parent were not the rack). The scenario's card098 'P' wait timed out: the Level0 rack gives no
  such row. The destroy rows came anyway.
- Host, at the rack destroy (175300 23843 7727): every pickup was `simulating 1, awake 1`, one rest row at spawn.
  2 s later they had fallen 40-170 uu to z 7697-7716 and were `awake 0, rested 1`. The host sent 4-12 rows each.
- Client: copies were simulating 1. At the destroy they sat at the host's spawn rest z. 2 s later they were at the
  host's exact final locations (to 1 uu).
- So for a Level0 rack the fall syncs. The prediction (host z drops, no row sent) does not hold for Level0. The
  playtest Level1 rack differs: its host pickups were `simulating 0` (the b437 trace), so a fall there could not come
  from physics on that root. The Level1 case is still untested.
- The shots do not show the pickups. On the host, bugs block the view. On the client, a wall blocks it.
- Logs: logs/c556b439a-{host,client}.log.
- Shots: prefix2/.../Saved/Screenshots/Windows/*c556b439a*.

## b440 Level 1 run (2026-11-21)

Static read of BP_ResupplyRack_Level1 (build/kis/rack1.txt; parent BP_KillableBuilding_Base) and the native setter:
- SpawnPickups: each of the 9 scene comps with a class set gets a deferred spawn at the comp's transform, is added to
  RackedActors, then `SetPhysicsStateOverride(2)`. Level0 does the same (rack0.txt 917).
- ASTCustomPickupBase::SetPhysicsStateOverride (RVA 0x051DBB70): returns if +0x460 already holds the value, else
  stores it and calls the virtual OnUpdatePickupStatus (vtable +0x768, RVA 0x051D2220). That calls
  AADPickupBase::OnUpdatePickupStatus, then for 2 `SetPhysicsEnabled(false)`, for 1 `SetPhysicsEnabled(true)`
  (RVA 0x04C62370), for 0 nothing. SetPhysicsEnabled works on +0x2F0 (the pickup's root StaticMesh, the only
  primitive in the c556 dumps): it returns if the simulate state already matches; for true it sets collision, then
  SetSimulatePhysics(true) and one more call with true. No authority check on any of these.
- Destroy (the rack's function, ubergraph 4223 -> 2062): for each valid RackedActors entry `SetPhysicsStateOverride(1)`
  and Niagara at the rack, then NavModifier SetAreaClass and SetLifeSpan(0.1). The c98 destroy (caller exe+0x37DC6BD
  is AActor::Destroy) comes from the life span.
- Lifetime is `InstaKillTarget(self, true)`. CheckBelow, a looping 0.5 s box trace down from Pole, calls Lifetime when
  it hits nothing.
- No BP path detaches or moves the pickups. Nothing is host-only except where the Destroy function runs. On the client,
  the pickups are card 104 copies, not RackedActors.

Prediction for c556b440a, written before the run:
- At the host destroy row each Level1 pickup is `simulating 1, awake 1`. The override(1) ran 0.1 s before.
- By the "2 s after" row each pickup has fallen to the floor under the rack and rested with rows above 1.
  c104_settle_one clears `rested` once a simulating root leaves its rest point, so it sends moving and rest rows.
- The client rows match the host within 100 uu at 2 s, as Level0 did.
- If the host rows show `simulating 0` at the destroy, the rack's Destroy function did not run on that destroy path,
  which is the playtest case. Then the float is vanilla on the host too.

Build b440: dwmapi fa1b276bf1e392ba, ubwperf 0b01d4b16ba8f98d. New test command `resupplylevel <0-3>` (cmdfile.cpp,
testrun.cpp, score_c475.cpp c556_set_resupply_level): it writes the own pawn's ResupplyLevel by name and updates the
card 491 watch entry. No U row is sent. Scenario: tools/scenarios/c556-c2m1-b440.txt. It waits on the card098 spawn
row of BP_ResupplyRack_Level1_C, because rack spawns log no 'P' row.

### b440 result (c556b440a void, c556b440b pass)
- c556b440a: void for Level 1. tools/drive.py had no `resupplylevel` verb (`unknown step: host resupplylevel 1`), so the
  rack was Level0 again. Its Level0 rows repeat b439 (4 pickups, host and client within 4 uu at +2 s).
  Fix: `resupplylevel` added to the drive.py COMMANDS set.
- c556b440b: drive rc 0, host/client rc 0, no crash dumps, WARN host 4 / client 9 (no new kinds).
  - `card556: host/client own pawn ... ResupplyLevel 0 -> 1 (test command)` on both sides.
  - `card098: host spawn BP_ResupplyRack_Level1_C` at 190102249; 8 pickups spawn `simulating 0` (z 7743-7872).
  - Destroy at 190117384 (15.1 s after spawn, caller exe+0x37DC6BD like Level0), at (175439, 23874, 7728).
  - Host at destroy: all 8 `simulating 1, awake 1`, rows 1. The prediction held: Destroy turned physics on.
  - Host +2 s: all 8 at z 7689-7716, rows 5-13 (moving and rest rows sent); 3 rested, 5 still settling.
  - Client at destroy: all 8 `simulating 1, awake 0` at the spawn z. Client +2 s: all 8 at z 7692-7716; the largest
    host/client gap is 10 uu (240983 z 7703/7713; 240994 xy 9 uu).
  - Shots host-2s / client-2s: the pickups lie on the ground in the rack debris on both screens; none float.
- Verdict: Level 1 behaves like Level 0 and syncs. The scenario reproduces no float. The playtest float (rack lived
  3.6 s, no host rows after) took a path this run does not take, most likely an early destroy before
  SetPhysicsStateOverride(1) ran on the pickups (for example Lifetime from CheckBelow, or a kill), not the
  15 s timed Destroy.

## End paths of BP_ResupplyRack_Level1_C (static, 2026-11-21, build/kis/rack1.txt and BP_KillableBuilding_Base)

The pickups are not attached to the rack. SpawnPickups runs BeginDeferredActorSpawn, then FinishSpawning, then
Array_Add to RackedActors, then SetPhysicsStateOverride(2). There is no AttachToComponent or AttachToActor.

| End path | Route | Override(1) on RackedActors? |
|---|---|---|
| Timer (Do Lifetime Timer, Duration about 15 s) | Lifetime (4351), then InstaKillTarget(self, true), then the parent HealthComp death (bound event 0, 2085 then 1013): `if Destroyed goto 1027 else Destroy()`, then the child Destroy (4223, then 2062) | yes |
| CheckBelow (TraceBelow every 0.5 s, first at 0.23 s) | BoxTraceSingleByProfile (DamageProp, box 75) from Pole; on no hit it calls Lifetime(), then the same chain as the timer | yes |
| Damage to 0 (shots, bug attacks) | the parent HealthComp death, then the child Destroy | yes |
| Damage above 0 | bound event 1 (2090, then 1065): SFX and gore only, the rack is not destroyed | (no end) |
| SetLifeSpan(0.1) | set only at the end of the child Destroy, after the RackedActors loop | yes, already done |
| SaveComponent PreSave / FullyLoaded | store and rebuild SavedRackedActors GUIDs and Duration; no destroy | (no end) |

The child Destroy loop (2062 to 2577) runs over every RackedActors entry. For each entry that passes IsValid, it calls
SetPhysicsStateOverride(1) and spawns Niagara. Entries already picked up fail IsValid and are skipped. After the loop it
calls NavModifier SetAreaClass, then SetLifeSpan(0.1). No path removes the rack without that loop.

Playtest host log, the two early ends:

- Neither early destroy has a card132 Health row or an InstaKill row. The log has only one rack Health row in the whole
  session: Level0 at 340777882, caller exe+0x229F17D. The timed Level1 destroy at 340803175 has no Health row
  either. So the missing row does not tell a kill apart from a timer end; the row appears to come once per class.
- AI trooper Morita sprays (STAttackActionRangedSprayAt, shooter null) were firing near both racks just before
  341543234-341544398. A shot or bug damage to 0 HP, or a CheckBelow miss, could explain both early ends. All of
  these paths still run the override(1) loop.
- Rack 250607 (1.5 s): its remaining pickups simulated and fell on the host (Incendiary 'D' at z 7705, medkit 250475
  'D' at z 7720, first-rest settle rows at 341547138 and 341547755).
- Rack 251379 (3.6 s): five of its pickups were collected at 341545631-669 (card050 DisablePickup, character null,
  so the other player). For the three it still held (Moritas 251256 and 251201, ammo 251141), the host sent no row
  after the destroy at 341546869.

The step 2 prediction is that a skipping path leaves the pickups `simulating 0` at rack height on the host. The
static read finds no such path. Rack 250607 shows the override did make its pickups fall on the host. For rack 251379
the open question is whether override(1) ran and the bodies stayed asleep (they were resting on rack geometry that
SetLifeSpan(0.1) removed 0.1 s later, so nothing woke them), or whether override(1) never reached them.

## b440 run c556b440c (FED_C2M1): early kill after two client takes, prediction written before the run

Scenario `tools/scenarios/c556-c2m1-b440c.txt`. On the Level1 rack spawn, the client walks to the medkit and the
MoritaSmall ammo and presses E at each. Then `host kill actor BP_ResupplyRack_Level1_C` runs (InstaKillTarget, about
3 s after the spawn, matching the playtest's 3.6 s end). Shots are taken at the destroy, at the "2 s after" rows and
3 s after those.

Prediction (sleep hypothesis):
- At the host destroy row, the remaining pickups are `simulating 1` with awake 0.
- At the "2 s after" row they are still at shelf height (z within 10 uu of spawn), and the host `rows` count is
  unchanged (no 'M' or rest row).
- They float on both screens.
- If that holds, the fix is base-game and host side: after the override(1) loop, call WakeAllRigidBodies on each
  RackedActors root primitive, swept over every rack level and any other spawner that calls override(1) then
  SetLifeSpan.
- If instead they fall to the floor (rows above 1, client within 100 uu at 2 s), the early end alone does not float
  them, and the next thing to check is the playtest client copies of 251256 and 251201.

## c556b440c result (2026-11-21): the pickups fall on both sides, the sleep hypothesis is disproved

- Logs: logs/c556b440c-host.log, logs/c556b440c-client.log, logs/c556b440c.drive. Host rc 0, client rc 0, no dumps,
  drive rc 2 (the medkit and ammo card050 waits timed out, the medkit most likely refused at full health under god
  mode; `host kill actor` found no rack because the rack had already ended).
- The rack spawned at 190654538 at (175269, 23786, 7727), 8 pickups at z 7743-7872, all `simulating 0`. At 190656486
  the client walked into the rack and took Incendiary 241024 and Armour 241048 (card050 DisablePickup). A srv chain row
  shows the client pawn's last blocking hit on the rack Pole.
- The rack ended by itself at 190657740, 3.2 s after the spawn (not the kill command; CheckBelow or damage with the
  client on the Pole). This is the playtest's early end, reproduced without the code kill.
- Host, at the destroy: all six remaining pickups (medkit 241041, ammo 241016 and 240999, frag 241033, Morita 240988 and
  241007) `simulating 1, awake 1`. 2 s later: z 7694-7717, `rested 1`, rows 4-7, simulating rest rows sent.
- Client, at the destroy (190657774): the same six, `simulating 1, awake 0` (client copies are driven by rows). 2 s
  later (190659780): z 7694-7717, within 15 uu of the host positions, for example frag 241033 host and client both at
  (175221, 23786, 7700).
- Verdict: an early end with other-player takes does not float the remaining pickups on either screen. No
  WakeAllRigidBodies fix. Next step: the playtest client copies of 251256 and 251201.

## Playtest copies of 251256 and 251201 (2026-11-21, static log read)

- Client (`game/StarshipTroopers_UBW/Binaries/Win64/UBWCoop-menu-20261120-184517.log`): copies made at 100518717
  at shelf height (251256 z 7854, 251201 z 7855, 251141 z 7773), `rest applied, moved 0 uu, simulating 1`. After the
  rack 251379 destroy (client 100522300) there is no card287 moving sample and no rest row for any of the three. At
  100613723 the client game destroy was refused (kept host copy, caller exe+0x37DC6BD); the host destroy came at
  100695996. In c556b440c every leftover got `card287 ... first moving sample` within 1 ms of the destroy.
- Host (`ultimate-bug-war-coop-public/logs/UBWCoop-menu-20261120-184651.log`): only the 0 ms spawn rest rows
  (`simulating 0, still`). Nothing after the destroy at 341546869.
- Digest round 9 (host 341604941, client 100581448, 58 s after the destroy): 3923 actors on both, mismatches 0. Of the
  11 differing slots, all were within tolerance, and none were the three leftovers, though rack 250607's #250517 was
  listed. So at round 9 the host root of 251256, 251201 and 251141 was at the client's shelf position, within the
  digest tolerance. The host root did not fall either, which conflicts with the user's "host sees them on the ground"
  unless that view was of rack 250607's pickups (which did fall on the host).
- Native path (disassembly, this exe): `ASTCustomPickupBase::SetPhysicsStateOverride` (RVA 0x051DBB70) returns at
  once when the stored state (+0x460) already equals the argument. Otherwise it stores it and calls vtable +0x768,
  `OnUpdatePickupStatus` (0x051D2220). That calls the AADPickupBase base (0x04C61870: IsPickupEnabled, collision,
  then sim from +0x400), then for state 1 the sim setter 0x04C62370 with 1. The sim setter returns at once when the mesh
  already reports IsSimulatingPhysics (vtable +0x4E0) equal to the request, so it does not wake an already simulating
  body. It only enables (SetSimulatePhysics-type calls at +0x5E0, +0x6D8, +0x8F0) when the state changes.
- So override(1) does nothing for a leftover that is already override 1, or already simulating but asleep. The host
  rows cannot tell these apart from a skipped loop. The next host step is a diag at this point: per leftover, the
  override byte +0x460, IsSimulatingPhysics and awake, read just before and after the Destroy loop. Then run the
  playtest's take set: the client takes Ammo1, both grenades, health and armour (RackedActors indices 1 and 4 to 7),
  which leaves Weapon1, Weapon2 and Ammo2, as rack 251379 did.

## b441 diag run c556b441a: prediction (written before launch, 2026-11-21)

Build b441 (dwmapi e69c630445b89052, ubwperf 998535a7066b87ba) adds rack_c556.cpp: a SetPhysicsStateOverride row (stored
+0x460 before, mesh +0x2F0 IsSimulatingPhysics before and after, awake after, mesh is root), a TraceBelow row per rack
at its first BoxTraceSingleByProfile and at each hit/miss change, and an end-path row from c556_rack_destroy.
Scenario tools/scenarios/c556-c2m1-b441.txt: the playtest take set (client takes the Frag, the medkit and one
MoritaSmall, leaving the 2 MoritaMk1 weapons and 1 MoritaSmall), no kill command, client placed 250 uu from the
rack spot so the takes fit before an early end.

(a) answered from b440c: the Moritas 240988 and 241007 (z 7698) and MoritaSmall 240999 (z 7700) fell there, and every
rack pickup has a StaticMeshComponent root, so the pickup class does not pick a different override path. b440c's
takes never landed (the client walk ran 3.6 s, past the rack's 3.2 s end), so b440c did not test the take set.

Predictions:
1. End path: host row `end: TraceBelow miss (CheckBelow)`, first trace about 3 s ago, Duration about 15 s, last trace
   miss; the trace rows give the miss's start/end and hit none. A host miss where the client copy hits (or the
   reverse) names a side-only collider or our spawn height; a "Lifetime timer" or "other" path refutes the CheckBelow
   lead.
2. Leftovers: one `override ... state 1` row each. Falls: stored before 2, sim before 0, after 1, awake after 1. Float
   (the playtest): stored before 1 (the +0x460 early return) or sim before 1 with awake after 0 (the sim setter early
   return, no wake). Rows on both sides; the client rows decide the client float.
3. The `state 2` spawn rows give each pickup's start state; if those already leave sim 1 and the body sleeps on the
   shelf, prediction 2's float branch follows from them.

## b441 diag run c556b441a: results (2026-11-21)

Logs: logs/c556b441a-host.log, logs/c556b441a-client.log, logs/c556b441a.drive. Exit 0; lines 69-73 (post-destroy
warps, looks, shots) failed because the end-row wait came after the destroy-point capture wait and replaced its
groups (objective-routines lesson 78). The rows below are complete without those shots.

- Rack spawned 8 pickups: MoritaMk1 241082, 241101; MoritaSmall 241093, 241110; Frag 241118, 241126; Health 241134;
  Armour 241141.
- Prediction 3 (spawn rows): every pickup on both sides, state 2: stored before 0, sim 1 to 0, awake 0, mesh is
  root 1. Spawn leaves no simulating body on the shelf.
- Prediction 1 refuted: TraceBelow never missed (host 19 traces, client 18, all hit Landscape at (175294, 23831,
  7687)). The end row says `other (damage, or a kill command)`, 9.1 s after the first trace; Duration read -1.0
  (the diag's "Duration" lookup did not find the BP variable, likely a decorated name: diag defect). Destroy caller
  exe+0x37DC6BD, host destroy 191633983.
- Probable killer, not proven: a Warrior Bug hit the client's server copy at 191633881, the same ms as the destroy
  loop (191633880). b440c shows the same shape: Warrior Bug hits on the server copy at 190657237 and 190657622, rack
  destroyed 190657740. The rack's HealthComp death from bug damage fits "early end, no kill" in the playtest too.
  The diag does not log the damage causer, so this needs one more row.
- Prediction 2 refuted for this case: all 7 leftovers' state 1 rows read stored before 2, sim before 0, after 1,
  awake after 1, and fell to z 7693-7716 within 2 s. Client copies took host moving samples at 191634044 and glided
  to rest 0.0 uu from the host's (card287 rows). No float on either side.
- Take set not reproduced: the client's Use presses on Frag, Health and MoritaSmall took nothing (no DisablePickup
  either side; client was probably full). The pod landed on the client (server copy armour 100 to 20), and the
  client then walk-over took the Armour Medium (host DisablePickup 191626744 with character null, caller 0x4C61EE3).
  That walk-over take is a card 559 (no walk-over pickup) case to check.

Next diag proposed: a row on the rack's HealthComp death / InstaKill path with the damage causer and instigator;
fix the Duration lookup (walk the class properties for a name starting with "Duration"). The float itself still
needs the playtest's state: leftovers whose override row shows stored before 1, or sim before 1 with awake after 0.

## Static trace: every physics writer and caller (2026-11-21, senior step 1 and 2)

No 556 run until this trace names a path. It names one: `AADPickupBase::OnHit`.

### Step 1 table

| Writer or caller | What it does to physics | Can it run on a pickup still on the rack? |
|---|---|---|
| `SetPhysicsStateOverride` 0x051DBB70 (only store to +0x460, at 0x051DBB7B) | stores the state, then OnUpdatePickupStatus | yes, but only from the rack BP: SpawnPickups (state 2) and the Destroy loop (state 1). No other BP or native caller |
| `ASTCustomPickupBase::OnUpdatePickupStatus` 0x51D2220 | base status, then state 2 sets sim off, state 1 sets sim on, state 0 keeps the base result | yes, re-applies the stored state; harmless on the rack (state 2 means off) |
| `AADPickupBase::OnUpdatePickupStatus` 0x4C61870 | sim = enabled ? +0x400 : false | only through the callers below |
| OnUpdatePickupStatus callers: ctors, BeginPlay, `SetPickupEnabled`, `execRespawnPickup`, `OnSignificanceChanged` 0x51D1C40 | re-apply the status | yes. They change nothing on the rack, because the stored state is 2 |
| `DisablePickup`+0x17 (take path: sets +0x2a8 = 0 first) | sim off, hidden, no collision | only on the taken pickup. A leftover is never disabled, so the take path cannot leave a leftover floating. The five card050 takes with character null are this path on the taken pickups |
| `ASTCustomPickupBase::Tick` (dirty byte +0x461) | status re-run | +0x461 is set only in `ASTCustomPickupBase::ActorFullyLoaded` (0x51BB41C, SaveComponent load) and cleared in Tick. It runs only on a save load |
| `PersistentActorActivate` / `PersistentActorDeactivate` 0x51D2650 | status re-run | only on pickups the SaveComponent tracks; same result as the stored state |
| Failed Use on a full player | `TryPickup`: CanPickup false, so no status call (only vcall +0x788 when enabled) | yes, and it does not touch physics |
| `execSetPhysicsEnabled`+0x72 (BP) | direct sim setter | not found on a pickup in the rack BP calls read so far |
| **`execOnHit` 0x4C63100 (inlined OnHit)** | when +0x364 is 0, the mesh reports a blocking hit, the other actor is valid and the hit normal is more than 0.7 up: sets +0x400 = 0 (sim off for good), SetPhysicsEnabled(false), then `TryAttachToActor` 0x4C62870, which calls `AActor::AttachToComponent` (0x37D8390) on the other actor's root, with rules 0x01010101 (KeepWorld) | **yes, the path.** It runs on the first upward contact of a falling leftover after the Destroy loop's state 1 |
| Mod detours (card50, card145, card254, card287 rest, card495, card559, card570) | on the host, rows, take credit and the nested-take status re-run only; no physics writes. `PutRigidBodyToSleep` is called only on client copies (world_c98.cpp 2149, 2332, 2383) and gibs | no physics on the host's rack pickups |

### The float path

1. The rack's Destroy loop sets state 1 on each leftover, so sim turns on and the leftovers fall.
2. The rack itself lives 0.1 s more. A leftover whose first upward contact is the rack (a lower shelf), another
   pickup or the pod gets OnHit. That sets +0x400 to 0, turns physics off and attaches the pickup to that actor,
   keeping its world position.
3. The rack is destroyed, and UE detaches its attached actors keeping their world position. The pickup stays at shelf
   height with sim 0 and +0x400 0.
4. The client mirrors the host's rest position.

A pickup that lands first on the ground also runs OnHit and attaches to the ground actor. That is the vanilla rest, and
it is what b441 showed (z 7693 to 7716).

Why b441 did not float: in b441 the leftovers' first contact was the ground. Whether a leftover hits the rack first
depends on whether the rack's shelf collision still blocks pickups during the 0.1 s after the loop. That can differ
between end paths. In the playtest, rack 251379 probably ended from Warrior Bug damage; in b441 it ended from the
Lifetime timer. The next static item is the rack BP's damage and Lifetime end paths: does either one turn the rack's
collision off before the Destroy loop?

One counter-check. `ASTCustomPickupBase::OnUpdatePickupStatus` state 1 turns sim back on at any later status call
(`OnSignificanceChanged`, `SetPickupEnabled`). So the float lasts until the next status call on that pickup. A
floating pickup that later drops when a player comes near would confirm this path.

### Step 2: the walk-over take of Armour Medium

The caller 0x4C61EE3 is `AADPickupBase::Pickup_Implementation`+0x123, called from
`ASTArmourPickup::Pickup_Implementation`+0x2F3. `ASTArmourPickup`, `ASTHealthPickup` and `ASTAmmoPickup` have no
`OnInteraction` override. Only the item, secret, tactical support and weapon pickups have one. So their only native
take is `TryPickup` (0x4C628F0), called from `OnBeginOverlap`+0xF4 (when the overlapping actor's class is in the
pickup's class list at +0x2B8), `DeferredBeginPlay`+0x12A and BP `TryPickup`. `TryPickup` takes when the byte at
+0x2AA is set and CanPickup is true. Vanilla single player therefore takes armour, health and ammo by overlap. The
take is vanilla, and it is not a card 559 gap: the user's rule names weapons, and weapons go through
`ASTWeaponPickup::OnInteraction`.

## Step 1 log check: no attach rows exist for runtime pickups (2026-11-21)

- Host `ultimate-bug-war-coop-public/logs/UBWCoop-menu-20261120-184651.log` and client
  `game/StarshipTroopers_UBW/Binaries/Win64/UBWCoop-menu-20261120-184517.log`: card140 attach rows cover placed
  actors only (encounter managers, spawners). There is no card140 row for 251256, 251201 or 251141, and none for any
  rack pickup, on either side. So the log cannot name the attach parent: step 1 neither holds nor fails.
- What the log does show is consistent with the OnHit freeze:
  - After the destroy (host 341546869, client 100522300), none of the three has a single moving sample or rest row on
    either side. In c556b440c every falling leftover had a card287 moving sample within 1 ms of the destroy. A
    pickup frozen by OnHit at its first contact never moves, so it sends nothing.
  - Digest round 9 (58 s after the destroy) has the host roots at the client's shelf position, mismatches 0.
- Counter-check: nothing dropped them later. There is no row for any of the three from the destroy until the host
  destroy of the copies (client 100695996 and 100696179, about 174 s later). No later status call made them fall,
  even with players nearby.
- The sleep reading (override(1) ran but the body stayed asleep) fits the same rows. c556b440c disproved it for an
  early end with takes, but it is not excluded for this rack.

## b442 fix proof run c556b442a: prediction (written before launch, 2026-11-21)

Build b442 (dwmapi 5b3e5baa529670bb, ubwperf 2e9b24ea1183048c) ships the senior's option (a): the host execOnHit
detour skips the freeze and attach when the other actor is a holder (resupply rack, tac-support pod, killable crate)
or a pickup still simulating or attached to one, with a capped `skipped rack attach` row; the host wakes each
leftover root right after the Destroy loop's override(1); released-pickup hit rows, rack damage rows and the extended
end path row stay on as diagnostics. Scenario tools/scenarios/c556-c2m1-b442.txt (the b441 steps).

Predictions:
1. Every leftover's state 1 row reads woke 0 or 1, gets a card287 moving sample within 100 ms of the destroy, and
   rests at ground z (about 7690 to 7720) on the host.
2. Each client copy rests within 15 uu of the host's; digest mismatches 0.
3. Any skip row is followed by a fall of that pickup. With no shelf contact the run is the no-regression proof.
4. The OnHit parms row reads without WARN; no released pickup reads froze 1 with a holder parent.
5. No new WARN class against c556b441a.

## b442 fix proof run c556b442a: results (2026-11-21)

Logs: logs/c556b442a-host.log, logs/c556b442a-client.log, logs/c556b442a.drive. Exit 0 both, no crash dumps, drive
rc 2 only from the post-destroy warps, looks and shots (lines 63-67: the scenario carried lesson 78's capture-order
bug from the b441 copy; fixed in the file after the run).

- The rack ended by Warrior Bug damage (end row: "damage", 5 damage events, the last 103 ms before the end, causer
  and instigator BP_AI_WarriorBug_C), 6.3 s after its spawn. That is the playtest's probable end path, now reached
  with the fix in. The client takes did not land: all 7 pickups were leftovers.
- Prediction 1 holds. All 7 state 1 rows read woke 0 (already awake after the override; the wake was a no-op as
  intended). The "at the rack destroy" rows (103 ms after the override) already show each root moved down, simulating
  1, awake 1; 2 s later every leftover sits at z 7693 to 7700, attach parent null, 6 to 12 rows sent. Host card287
  rows do not exist in this log set (b441a has none either); the at-destroy rows stand in for the moving sample.
- Prediction 2 holds: every client copy is within 4 uu of the host's; session summary mismatches 0.
- Prediction 3: no shelf contact, so no skip row. The 4 released-pickup hit rows all hit Landscape 252 to 569 ms
  after release, froze 0, attach parent null. This run is the no-regression proof.
- Prediction 4 holds: `card556: host OnHit parms: OtherActor at 8`, no WARN.
- Prediction 5 holds: the WARN classes match c556b441a exactly (host 4, client 9).
- Nit: the client end row says "other (a kill command)" because damage broadcasts are host only; on the client the
  label means only that no damage was seen on that side.

The card now waits on the user's playtest check; no more runs.

## Closed (senior, 2026-11-22)

The fix is proven by logs and runs, and the proof is in the sections above. Under the no-playtest-parking rule, a proven fix does not stay open waiting for a play check. It is on the playtest checklist in STATE.md. If the user sees it again in play, reopen this card with that report.
