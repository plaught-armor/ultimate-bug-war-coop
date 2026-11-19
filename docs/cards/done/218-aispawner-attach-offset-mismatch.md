# Card 218: Dropship AI spawners sit at a different place on the client

Status: queued 2026-11-12, from c147-r1 (new WARN class, seen once; not seen in c147-r2 on the same route).
Static trace first, no code and no run until the cause is named. Budget after that: three runs.

## Seen

c147-r1 (FED_C2M1, DLL `29ed4b45f9d50ed1`), digest rounds 12 to 14: 5 mismatches, class `BP_AISpawner_C`,
field location. Spawners `_0`, `_17`, `_20`, `_21`, `_22`, each attached to a `BP_ProceduralDropshipTroops_C`.
Host offset from the parent (4272, 769, -2534), client (0, 0, 800). Rounds 12 to 14 in c147-r2 read 0 mismatches.
Logs: `logs/c147-r1-{host,client}.log`.

## Why it matters

A spawner's location is where its troops or bugs appear. If the client's spawner rides the dropship and the
host's does not (or the reverse), spawned actors start in different places on each machine.

## Do (static, each line marked verified or open)

1. Which side is right: is (0, 0, 800) the placed relative offset and (4272, 769, -2534) a detached or moved
   spawner, or the reverse. Name the game function that moves or detaches it (`MoveToArea`, caller
   exe+0x5143A0F, writes these actors' collision at map load on both sides).
2. Why r1 and not r2: what differed (dropship phase at the client's join, the save and load step, timing of
   the digest round against the dropship's flight). The digest compares world locations sampled on each side;
   state whether a moving parent alone can produce this mismatch with no state difference.
3. If it is real state: the event the host must relay (class rule for every actor attached to a procedural
   dropship, not these five). If it is a sampling artefact: the digest fix (compare relative location plus
   parent identity for attached actors).

## Done when

- Cause named with evidence from the r1 logs, fix at class level, one run with a written prediction:
  0 `BP_AISpawner_C` mismatches through a dropship arrival after a client join. Ends with "Report complete."

## New evidence, run B (c241-c2m1-b1), 2026-11-13

Digest rounds 30 to 32 (after `EndLevel`, 234145377 on): 3 location mismatches in level `FED_C2M1_Scripting`,
`BP_Dropship_LandingBlocker_C_19`, `STAISpawner_11`, `TriggerBox_0`. All three differ by the same vector: host
minus client = (25100, -43800, -650). `TriggerBox_0`: no row sent. So the host moved one group of script actors
(a landing zone) and the client did not. This is real state, not a sampling artefact of a moving parent: the
values are constant over three rounds. Step 1 now starts here: find the script call that moves the group
(`kis.py` on `FED_C2M1_Scripting`, calls that set a location on these three or on a shared parent) and the
event that runs it on the host only.

Parent found (same logs): the host destroyed a `BP_ProceduralDropshipTroops_C` at (151535, -68000, 14462) at
234114209. The blocker sits at exactly that point and the spawner 800 uu above it, which is the (0, 0, 800)
relative offset of c147-r1. So the three actors ride a troop dropship and stay where it was destroyed. The
client's copies stayed at (126450, -24200, 15100): its dropship was 50 000 uu from the host's when it went
away. Answers: item 1, (0, 0, 800) is the placed offset; item 2, a parent at a different place, not a
detach; item 3, the fault is the dropship's position on the client (card 159 class: `20 splines not resolved`
in the run B totals row), not the children.
Not settled by this log (card098 rows capped before the end): the client's dropship position and time at its
destroy. Deciding row, build 4 diagnostic in the card 159 code: at destroy of a tracked dropship, both sides log
`card218: <side> dropship <name> destroyed at (x, y, z), spline distance d, N attached actors`, not capped.
Prediction: the client's row for this dropship shows a spline distance behind the host's by the flight it did
not follow; the fix is then a final host position row applied before the destroy.

## Senior prep (2026-11-13): the deciding diagnostic is not in build 4; spec for build 5

`grep -rn card218 mod/src` returns nothing: the diagnostic row above was never coded, so no build 4 run can
settle this card. It goes into build 5 with this spec. Diagnostic only, no behaviour change, both sides.

Where: `detour_c98_destroy` (`world_c98.cpp:131`; every `UWorld::DestroyActor` on both sides passes it), before
the original call. Put the body in `dropship.cpp` as `void c218_destroy_line(const void* actor)` (declared in
`hooks_internal.h`) and call it from the detour; the dropship offsets are private to `dropship.cpp`.

Row, for an actor that `is_a` `g_troops_dropship_class()` (`hooks_internal.h:658`; null-check the getter as
`dropship.cpp:189` does), not capped (a map has a few tens of troop dropships):

`card218: <host|client> t=<ms> dropship <name> destroyed at (x, y, z), spline <spline name> distance <d> of
<length>, target point <0|1|2>, spline ended <0|1>, <N> attached actors, caller exe+0x<rva>`

- location: the actor's location, the same reader the card 159 check line uses (`dropship.cpp:478`);
- spline: `read_ptr(ds, kDropshipSpline)` (+0x400), distance: float at `kDropshipSplineDistance` (+0x484),
  length: `g_spline_length(spline)` (see `ds_client_hold`, `dropship.cpp:711`), "none" and 0 when the spline is
  null;
- target point: byte at `kDropshipTargetPoint` (+0x482; 1 means the spline end destroys the dropship), spline
  ended: byte at `kDropshipSplineEnded` (+0x483);
- attached actors: the count of distinct owners of the root component's `AttachChildren` (reflected property,
  read as `sprint.cpp:790-797` does), bound 32 per array;
- on the client also add `, held <0|1>` from the dropship's `DsLiveRow::held` when it has a row.

The caller RVA shows whether a client destroy came from the client's own game logic or from a host row.

What the row decides (write these in the run prediction):

| Client row against the host row for the same dropship name | Meaning | Fix |
|---|---|---|
| distance far below the host's, spline name equal | the client did not follow the flight (card 159 `splines not resolved` class) | host sends a final position row (location and distance) that the client applies before the destroy |
| spline "none" or a different spline name | the client never got the StartAction for this dropship | fix in the card 159 snapshot or live row, not here |
| no client row at all | the client's copy is destroyed by another path or never existed | trace that path first |
| equal distance and location within the card 159 tolerance | the dropship is not the cause | go back to step 1 for the attached actors |

Run: FED_C2M1 through `EndLevel` (run B's route, `c241-c2m1-b1`), batched with the other build 5 proofs; no
run of its own. The fix card follows the row; three-run budget as in the status line.

## 2026-11-15: repeatable route (runs c371b56, c371b57)

After a host in-place load from the death screen (LoadLastSave) on FED_C1M1, the digest round after the client's
rejoin reads 1 mismatch in 2 of 2 runs: `STAISpawner STAISpawner_5` (level FED_C1M1_Scripting, key 108da64c),
location host (69000,63250,-4250) against client (0,0,800). 0 in `c370b56` and `c143b55d` (no death-screen load;
`c143b55d` loaded a quicksave). The client's value is the same relative offset as in c147-r1, so the client's
spawner rides its dropship; the host's reads a world-size offset after the load. Open guess, not verified: the
game's load writes the saved world location into an attached spawner as a relative one on the host (the vanilla
load path; the client rejoins a fresh map). The host is the authority for what the spawner spawns, so the
visible risk is low. Route for the static trace or a run: `tools/scenarios/c371-restart-checkpoint.txt`.

## 2026-11-15: arithmetic from run c374a57 (host RESTART CHECKPOINT alone, client rejoined)

Third run with the same mismatch. Rows: host `card237` world location (100046, 75041, -1592), digest relative
(61900, 54600, -4750); client world (38287, 20470, 3979), relative (0, 0, 800). Host world minus host relative is
(38146, 20441, 3158); client world minus client relative is (38287, 20470, 3179). The two parents are within 145 uu
(the dropship in flight, rows 20 ms apart), so on both sides the spawner is still attached to the same dropship and
only the host's relative offset is wrong: it holds a world-size value after the load. This supports the guess above
(the game's load applies the saved world location as a relative one to an attached spawner) and puts the cause in
the vanilla load path, the same in single player. Not a sync defect: the client's value is the map's own. No fix
planned before the playtest; a fix would be host-side (restore the relative offset after `detour_c121_load`), and
only if spawned enemies are seen in the wrong place after a checkpoint load.

## Closed (senior, 2026-11-16)

The card held two faults.

1. Client dropship in a different place when it went away (c147-r1, c241-c2m1-b1), so its attached spawner,
   blocker and trigger box stayed behind. Cause was the card 159 / card 236 class (`20 splines not resolved` in
   run B). Fixed there. `c423b163a` (FED_C2M1 through `EndLevel`, with a client rejoin mid map): `card159: client
   dropship totals (map end): join check 24 of 24 match ... 0 splines not resolved`; 11 digest rounds, all
   `mismatches 0`, including rounds 9 and 10 after `EndLevel`; 0 `mismatch` rows for `STAISpawner`, `BP_AISpawner_C`
   or `BP_Dropship_LandingBlocker_C`. The `card218` destroy row is in the code (`dropship.cpp`
   `c218_destroy_line`); `c460b169a` shows host and client rows for the same dropship 94 uu apart (distance 263696
   against 263602 of 264589).
2. `STAISpawner_5` after a host death-screen load (c371b56, c371b57, c374a57, c374b76, c375b76): the host's relative
   offset holds a world-size value after the game's own load; the client's value is the map's. Vanilla load path,
   the same in single player; not a sync defect. No fix. It stays a known digest row on that route only.

Reopens on: a `STAISpawner` / `BP_AISpawner_C` / `BP_Dropship_LandingBlocker_C` mismatch on a run with no
death-screen load, any `splines not resolved` above 0 (card 236 watch), or enemies seen spawning in the wrong place
after a checkpoint load (then the host-side fix after `detour_c121_load` named above).

Report complete.

