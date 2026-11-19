# 423 Script-destroyed level actors (`BP_DestroyableActor_*`) have no sync, no digest entry and no join replay

Found 2026-11-16 by the open-card sweep (card 299 candidate (b), set aside there as "a real gap of class C").
User rules: full world sync, all effects sent.

## Finding (static + host inventory rows)

- Every `card132: host inventory (map load)` row for a `BP_DestroyableActor_*` class says `mechanism=none`, on
  every map: BugBlocker (3-4), BugHoleOpen (3), HiveEntrance (1-6), Floor (1), C4M1BridgePart1/3/4,
  C4M1MetroFrontage, C4M2_BridgePart*, C4M2_MoveableRoad (2), C5M1_PlasmaBugNest01-04, PlasmabugShield (2).
- All derive from `BP_DestroyableActor_Base_C` (Blueprint; name table: functions `LD: Destroy`, `LD: Damage`,
  `Destruction FX`, `Setup Destroyed`, `Setup Alive`, `HideActor`, `ShowActor`; properties `IsDestroyed` (bool),
  `AliveMesh`, `DestroyedMesh`; delegate `Destroyed`; `SaveComponent` bound to `SaveFullyLoaded`).
- The level script destroys them by a Blueprint-to-Blueprint call of `LD: Destroy` (FED_C2M1: the three cave
  gates at the `LOS_O05` add). That call does not pass ProcessEvent, so no mod hook sees it. The client only gets
  the state when its own level script copy runs the same branch.
- Failing case: a client that joins or reconnects after the call loads the map with every such actor alive
  (`SaveFullyLoaded` with `IsDestroyed` false runs `Setup Alive`): closed cave gates, standing bridge parts,
  whole Plasma nests, shields up, while the host's are gone. Movement then disagrees with the picture (host
  corrections carry the client through a gate it sees closed), which is the wording of the card 299 report; the
  2026-11-13 client rejoined twice after the `LOS_O05` add. Live case: any script branch the client's copy does
  not reach (card 100 skips, per-player triggers) leaves the same split.

## Fix: one more row in the card 132 ext mechanism (`kC132Ext`, `ext_c132.cpp`)

That mechanism already owns "a state byte with no other mod mechanism": host poll with baseline, journalled
'X' row, join replay pack, client apply and enforce, digest field.

1. Row 11 `BP_DestroyableActor_Base_C`, field `destroyed`. The class is a Blueprint, so the row's class is the
   UClass found by name once per map (not a `StaticClassFn`) and the byte offset is `prop_find_prop(class,
   "IsDestroyed")`, resolved once per map and kept with the row. Match by super chain. `kC132ExtClasses` 10 -> 11;
   the inventory mechanism text for the class becomes the ext row's.
2. Client apply (`ext_apply`), host 1 and own 0: call the actor's own `LD: Destroy` by `g_find_function` +
   ProcessEvent for a live row; for a join replay pack row write `IsDestroyed` = 1 and call `Setup Destroyed`
   (no effects: a joiner must not get every past explosion at once). Equal values: nothing (the client's own
   script usually ran first). Host 0 and own 1: no write, one `WARN card423: client <actor> destroyed, host
   alive` (the enforce pass repeats the compare; cap the WARN at one per actor per map).
3. Rows: `card423: host t=<ms> <actor> destroyed 0 -> 1`, `card423: client t=<ms> <actor> destroyed <own> ->
   <host> by <LD: Destroy | Setup Destroyed | none>`.
4. Not in scope: `LD: Damage` (effects only, no state), the `Destroyed` delegate listeners (run by the actor's
   own function on the client).

Checks before coding (static, in the asset; a line each in this card): `LD: Destroy` guarded on `IsDestroyed`
or not (if not, a client script call after the row plays the effects twice: then the row's apply and the
script's call need the guard in the mod, by refusing nothing and accepting it, or by a note that the order makes
it impossible); what `C4M2_MoveableRoad` overrides; that `Setup Destroyed` takes no parameters.

Static check results (asset reads, before coding):

- `LD: Destroy` is not guarded on `IsDestroyed`. It runs ExecuteUbergraph (entry 62) into `Destruction FX`, then
  `Setup Destroyed`; `Destruction FX` sets `IsDestroyed`, broadcasts `Destroyed`, spawns the Niagara effect, plays
  the sound and the camera shake, so a second call replays every effect. Mod guard: the live apply waits 2 s
  (`kC423WaitMs`) after the host row and re-reads the copy's own byte before calling, so a client script call that
  ran first wins and the row logs `by none`. The join pack and the enforce pass call `Setup Destroyed` (no effects).
- `C4M2_MoveableRoad` has 8 exports and 0 script functions: component template overrides only, no function
  overrides. Every other subclass of `BP_DestroyableActor_Base_C` also has 0 script functions.
- `Setup Destroyed` takes no parameters. With `DestroyedMesh` valid it swaps the mesh (soft reference load);
  otherwise it hides the actor (visibility off, collision 0). The base save load path (`SaveFullyLoaded`) runs
  `Setup Alive` or `Setup Destroyed` by `IsDestroyed`, so the pack's call is the same path a save load takes.

## Same batch: card 299 diagnostic row (never coded)

After a client kill of a placed nest (`c98_client_kill`) and after `c98_load_dead`, one row per side per nest:
`card299: <role> t=<ms> nest <actor> mesh <static mesh name> bound <+0xB9> thresholds <+0xC8> health <+0xD8
non-null> index <+0xE0> pending <+0x108 non-null> collision <enabled>`. Offsets from card 299 step 1b. Not gated
on `UBWCOOP_DIAG` (at most one per nest per map), so the next play log decides card 299.

## Predictions (one run, FED_C2M1, batched)

- Host reaches the `LOS_O05` add with the client connected: host log 3 `card423: host` rows (BugBlocker_C_3, _5,
  _7); client log 3 `card423: client` rows, each `by none` (own script ran) or `by LD: Destroy`; zero `WARN
  card423`.
- Client reconnects after: 3 `card423: client ... by Setup Destroyed` rows from the pack; client screenshot aimed
  at one gate (teleport and look) shows it open, same shot before the reconnect for comparison.
- Digest round after: 0 mismatches, the gates counted with their ext field.
- One `card299` row on each side for the nest the scenario kills, same mesh name on both.

## 2026-11-16 run c423b124a (b124 `931762d66b1e7d1d`, FED_C2M1): void for the gates

- `card423: <side> BP_DestroyableActor_Base_C found, IsDestroyed at +0x31C` on both sides at the map load.
- Three other destroyable actors went through the new row before the gates: `BP_DestroyableActor_BugHoleOpen_C`
  `StaticMeshActor_97`, `_95`, `_57` (object names, not class-based). Host `destroyed 0 -> 1` for each; client
  `by LD: Destroy` for 97 and 95, `1 -> 1 by none` for 57 (the client's own copy was already destroyed). After the
  client reconnect the pack set all three: `destroyed 0 -> 1 by Setup Destroyed`, then `1 -> 1 by none` for each
  when the live rows replayed. Zero `WARN card423`. Digest: 12 rounds, 0 mismatches each.
- The gates never opened: `LOS_O03_SO03b` was not completed, so `LOS_O03` stayed open and `LOS_O05` was never
  added. Cause from the host log: the site `CAT_1034` is on `BP_SatchelBuilding_Flakbug_C_0`, which turns every
  10 to 15 s for 3 to 4 s. A turn started at 488406074, 547 ms before the hold's `MS_WPN_SatchelCharge_Cancel` at
  488406621 (Prime at 488404354), and no `card418 ... fuse started` row followed. On the plants that worked, the
  Cancel sound comes in the same ms as the `card418` fuse row, so the Cancel sound alone does not mean a failed hold.
  Scenario fix: each plant waits for the carrying building's `card408 ... timeout` row (the turn's end), warps after
  it (the site moves with the building) and gets one more hold after the next turn's end if the first is cut short.
- Card 299: no rows. The `LOS_O02_SO05` nests (`Bugnest_HumanGameplay_C_0`, `_7`, class
  `BP_SatchelBuilding_Plasmabug_C`) die by the card 163 damage path (`relayed as S 2`), not `c98_host_kill`, so the
  row never ran. b125 adds the row to both ends of the card 163 path. The playtest nest (`_5`, card 299) dies at
  `LOS_O05`, which this scenario stops at.

## 2026-11-16 run c423b125a (b125 `4183cad44040c8fa`, FED_C2M1): gates pass, one digest race

- Drive: 1 failed line (277: the first `objective go all` for `LOS_O03_SO01` landed inside `BP_FallZones_C`; the
  retry at 279 completed it). All three satchel plants armed on the first hold after a turn end (card408 timeout
  rows for `Flakbug_C_0` and `_4` matched; `_2` did not turn within 20 s and the plant held anyway).
- Gates: host 3 `card423: host ... BugBlocker_C_3/_5/_7 destroyed 0 -> 1` at t=489018763; client 3 `by LD: Destroy`
  at 489020780 (the 2 s own-script wait; the client's own level script did not destroy them); after the reconnect
  ("reconnected (1): try 1, 7704 ms after the drop") 3 `by Setup Destroyed` at 489033895, then 3 `1 -> 1 by none` from
  the enforce pass. 0 `WARN card423` on either side.
- Digest: 10 client rounds, 9 with 0 mismatches, round 4 with 1: `WARN card132: mismatch BP_DestroyableActor_BugHoleOpen_C
  StaticMeshActor_95 destroyed host 0001 client 0000; last row seq 8691 (WorldOp) sent t=488901507 applied
  t=488901524`. The client's `LD: Destroy` for it ran at 488903732, after the round's query (D4 at 488903290): the
  round fell inside the `kC423WaitMs` wait that a live X row starts. Not a lost row: the digest compares an ext row 10
  actor whose `c423_due` is still pending. Fix candidate: the client digest counts such actors as in flight (the
  "excluded (a ledger row after S=...)" path) until the wait ends. Round 10, after the reconnect: 0 mismatches.
- Shots (client): `client-c423b125a-c423-gate-before.png` and `-after.png` both show a cave wall filling the view at
  912 uu (look yaw -85.1, pitch -9.0); with no host picture from the same spot the gate's open state cannot be read
  from them. c423b126a adds a host shot from the same warp point.
- Card 299: still no row. The card 163 path ran on both sides (host 2 `relayed as S 2` for `_0`, `_7`; client 2 live
  and 2 on the reconnect replay), so `c299_nest_row` returned early: `g_find_first_object(nullptr,
  L"STMeshUpdateComponent")` found no class, or the nest has no such component. b126 resolves
  `/Script/Aloft.STMeshUpdateComponent` by `find_class` and logs `no STMeshUpdate component (class found|not found)`.

## Senior review finding, 2026-11-16 11:39 (run `c423b125a`, b125)

- One digest mismatch (gate is 0): client round 4, `mismatch BP_DestroyableActor_BugHoleOpen_C StaticMeshActor_95
  destroyed host 0001 client 0000`. The live X row was ledgered applied at t=488901524, the snapshot taken at
  488902357, the client's `LD: Destroy` called at 488903732: the `kC423WaitMs` wait is a state the ledger calls
  applied. Fix `c423_digest_patch.py` (for b127): `c423_waiting(actor)`; `c132_actor_ext` gives the host's value 1
  for a waiting copy. Done when a FED_C2M1 run has 0 such rows and playlog `mismatches 0`.
- The wait bought nothing in this run: 5 of 6 live rows ended `by LD: Destroy` about 2.2 s after the host, 1 row was
  `1 -> 1 by none` (the script ran before the row). The wait length stays until the junior's counts from
  `c423b126a` are in (rows by ending and own value at arrival).
- Checked and clean: key c30860a2 is shared by the cave gate (FED_C2M1_Scripting) and a cliff (FED_C2M1_Art), both
  named StaticMeshActor_95. The digest compares by (key, level); CollisionOp and X rows find the copy by name and
  nearest location, and the client's `card098` row shows the collision write landed on the cave gate.

- 2026-11-16 11:46, c423b126a (b126) counts: live rows with own 0 at arrival 5, all ended `by LD: Destroy`; own 1 at
  arrival 7 (1 before the reconnect, 6 enforce passes after the join pack). With b125: 10 of 10 own-0 rows waited
  2 s for nothing. Patch `c423_wait_patch.py` (b128): `kC423WaitMs` 250. Done when the client `card423` rows show the
  destroy within 500 ms of the host's and no doubled effect row.

- 2026-11-16 12:05, c423b127b (b127 74c1fdca882992af, still 2000 ms wait): host destroy to client row: _97 +2198 ms
  and _95 +2048 (`by LD: Destroy`), _57 +29 (`1 -> 1 by none`), gates _3/_5/_7 +2043 (`by LD: Destroy`). After the
  reconnect, 6 `by Setup Destroyed` then 6 enforce passes `1 -> 1 by none`. 0 WARN card423 on both sides, digest
  0 mismatches (rounds 8 and 9 after the join). Client shots before and after the reconnect at BugBlocker_C_3: no
  blocker in frame (open). The scenario now waits on the nuke reload before nest 2 (card 430) and ends the map with
  EndLevel and Next before the quit (card 428 host map totals).
