# 251: Frozen corpse moves on the host after its freeze snap; client copy stays

Found by card 241 run 2 (c241-c2m1-r2, FED_C2M1, DLL `83cf0b9cdcfe0cb0`). Digest rounds 29, 31, 32 of 43:
`Pose:1`. Corpse guid 0xe22 (BP_AI_WarriorBug_C): host 170613624 `card137: host pose freeze guid 0xe22 snap 9
sent at the game's SetAllBodiesSimulatePhysics(0)` is the last host line for the guid; client 170613650 and
170621751 freeze check 0.0 uu; client 170629295 `round 29 pose guid 0xe22 snap 9: body 6 at (225769, -21771,
6898) is 366 uu from the host's (225497, -22013, 6860); 22 of 22 bodies over 100 uu, simulating 0`. Corpse
guid 0x766, rounds 31 and 32: body 6 412 uu off, 22 of 22 bodies, simulating 0. Both lie near the flak bug
satchel sites during the LOS_O03_SO03 steps. c238-c2m1 and c241 run 1 had 0 mismatches. Cosmetic (corpse
place), not a play blocker: after the playtest.

All 22 bodies off by one distance with physics off: the whole corpse was moved as a rigid unit after the
freeze, not re-simulated.

Step 1, static: who moves a frozen corpse on the host (actor transform set, base movement of what it lies on,
blast impulse path that re-enables and re-freezes without the game's SetAllBodiesSimulatePhysics(0), corpse
ticket relocation); take the host lines for 0xe22 and 0x766 between freeze and the digest round. Fix by
class: any transform change of a frozen corpse on the host sends a new snap (or the root transform), for
every corpse class, not only the warrior.

Files: decided in step 1 (corpse pose code, cards 137/140). Budget: 1 run (c241 scenario, O03 block).

Seen again in c241 run 3: digest round 20 (171824239) `Pose:6`; client `WARN card140` pose freeze check,
164 freezes, 2 bodies over 2 uu one tick later, worst 123.3 uu. Same class, use both runs in step 1.

## Step 1 answers (senior, logs of c241 runs 2 and 3 plus `pose_c132.cpp` and disassembly, 2026-11-13)

- The title is wrong: the host's corpse did not move. The check compares the client's bodies with the host's last
  sample (`against its last sample`). A host corpse that moves while frozen gets a new snap at once
  (`pose_host_row`: body 0 over `kPoseMoved` from `sent_root`, settled, so `snap_due`); no such snap was sent
  (host and client both end on snap 9 for 0xe22, snap 2 for 0xd88, snap 18 for 0xc32).
- The client's copy moved, and only a copy that came back into relevancy. All 9 cases have a `card137: client
  corpse arrival ... posed 0 ms after it arrived` row 0.5 to 8 s before the mismatch, a freeze check of 0.0 uu
  20 to 84 ms after the arrival, and then 22 of 22 bodies off by one distance. Run 3: 6 corpses arrive together at
  171823722 to 171823724 (client warp back to the satchel site) and all 6 are off at round 20 (171824239).
  Copies that never left relevancy pass every round (round 19: 14 checked, 0 over).
- Mover, predicted from the engine: `UCharacterMovementComponent::SmoothClientPosition_UpdateVisuals` 0x3A55FA0
  (simulated proxies, each movement tick). When the mesh does not simulate it writes the mesh's relative location
  back to the base offset under the capsule (virtual call +0x4E0 on the mesh, then the calls at 0x3A56588 and
  0x3A56AA8). A ragdoll leaves the mesh away from the capsule; the frozen copy is held with physics blend, so the
  bodies follow the mesh as a rigid unit. The WARN rows fit: the mesh component sits 100 to 300 uu from the checked
  body, the bodies are off in a different direction per corpse.
- Not settled statically: why the first copy is not moved the same way (the game's death path on a living copy
  must stop its movement tick or smoothing; a copy that arrives dead never runs that path). The diagnostic row
  below decides it.

## Fix spec (step 2, `mod/src/ubw/pose_c132.cpp` only, client side)

- Diagnostic, both at the card 140 freeze check and in `pose_check_warn`: capsule world location, mesh relative
  location, the movement component's tick enabled flag and movement mode. Row
  `card251: client corpse guid <g> capsule (...) mesh rel (...) cmc tick <0|1> mode <n> (<arrival|first copy>)`,
  capped per map.
- Fix by class, every frozen held copy (not only arrivals, not only the warrior): `pose_hold` turns the copy's
  movement component tick off, `pose_release` restores what it found. A frozen corpse has no movement to smooth.
  If the diagnostic shows the mesh relative location unchanged on a moved corpse, the mover is something else:
  report the row and stop, no second guess in the same card.
- Prediction (FED_C2M1 O03 block, the client warps away from the satchel site and back; batched with 253):
  `card251` rows show cmc tick 1 on arrival copies before the fix build's hold and 0 after it, 0 `WARN card132:
  client round N pose guid` rows for arrival copies, `Pose:0` in every digest round. Budget: 0 extra runs.
- Same class as card 269 part B (re-arrived corpse copies): there the copy arrives simulating and drifts from the
  stream, here it arrives frozen and is moved as a unit. Both diagnostics go in build 4 and are read in one run.

## More evidence (run c230-c3m1-d2, build 3, senior, 2026-11-13)

Guid 0x154 (FED_C3M1): frozen final snap 2, `simulating 0`, 625 uu off in digest round 3 and 439 uu off in round 4,
all 22 bodies, and the copy's position differs between the two rounds. A frozen copy moved as a unit: this class.
Not an arrival copy, which supports the "every frozen held copy" scope of the fix.
Same guid, bone rows: `card152: client bone check guid 0x154 ... (frozen final): bone 0 ... is 242.8 uu from body 0
and 170.7 uu from the mesh component ... simulating 0` at 235041831 and again at 235076887 (a second freeze of the
same snap). Bones and bodies of the frozen copy are apart, the card 191 symptom, on a copy that is not an arrival.

## Closed (2026-11-17 12:51)

The fix (frozen held copy: movement component tick off in `pose_hold`) and the `card251` diagnostic are in the
build. Evidence, 11 FED_C2M1 runs c423b178a to c423b182a (the map and satchel-site route the card was found on):
198 digest rounds, all `mismatches 0`; 0 `client round N pose guid` WARN rows; 634 `client corpse arrival` rows.
c423b182a alone: 102 `card251` rows, every one `cmc tick 0` (21 arrival copies, 81 first copies), as predicted.

Reopens on: a `Pose:` digest mismatch, or a `WARN card132: client round N pose guid ... simulating 0` row with all
bodies off by one distance.
