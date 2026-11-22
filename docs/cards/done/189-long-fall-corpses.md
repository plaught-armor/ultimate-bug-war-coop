# Card 189: A corpse that falls more than 1000 uu is still handled as a corpse

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: queued 2026-11-11, from c186-r1 (junior findings 1 and 2). Static first.

## Why
- `kPoseFellUu` 1000 (`mod/src/ubw/pose_c132.cpp:99`, check at line 653): a corpse whose body 0 is 1000 uu below its
  start is put to sleep as "fell through the world". A kill high in the air or on a cliff is a legitimate fall of
  that size.
- New host WARN class in c186-r1, x2: "card132: host hit BP_AI_WarriorBug_C guid 0x31e: impact ... is 1041 uu from
  the NPC". The check measures from the actor, which stays at the death place while the mesh falls.

## Do
1. Trace: what "fell through the world" must catch (hb41 guid 0x1a4), and a test that tells it from a long fall
   (below the map's kill z, or no floor hit under it), with the highest legitimate drop on each map.
2. The hit-distance check measures from the ragdoll root (`c132_ragdoll_root`) when the NPC is a corpse.
3. Proof rides the next run that kills in the air: 0 WARN lines of that class, the corpse streams to its landing.

## Files
`mod/src/ubw/pose_c132.cpp`, the file holding the hit-distance WARN, this card.

## Evidence from c177-r1c (2026-11-12)

18 more host lines of this WARN in natural bug combat on FED_C2M1 (for example guid 0x116, impact 1203 uu from the
NPC). One line prints a garbage hit bone name: "hit bone ystem???IsNavigationBeingBuilt??IsNavigationBeingBuiltOrLockedf".
Added to this card: find which read produces that name (an FName or string read from a stale or wrong pointer) and
fix it; a bad read in a log line is a crash risk.

## Senior prep (2026-11-12, static, DLL `8edaffb0745a50fe`)

- Fell check: `pose_fell_check` (`pose_c132.cpp:649`), `kPoseFellUu` at line 99; it compares body 0's z with the
  first sample's z only. No kill-z or floor test exists yet.
- Hit-distance WARN: `hit_queue` (`hit_c132.cpp:144`), measured from `actor_location(npc)` at line 171; `r.host_at`
  (sent to the client, used by `hit_drift`, line 307) is the same actor location. If the measure moves to
  `c132_ragdoll_root` for a corpse, state whether `r.host_at` moves with it and what the client compares it to
  (the copy's `ReplicatedMovement`, which stays at the death place too): both ends must use the same point.
- Garbage hit bone: `hit_name_token` (line 129) already rejects non-printable text, so the logged text is what
  `fname_text` returned for the FName at `event + kEventHit + kHitBoneName`. Suspect: an FHitResult the game left
  unfilled (a radial or point damage event with no hit), so the FName index is not a valid name-pool entry. Check
  what `fname_text` validates before it reads the pool (block and offset bounds, entry length) and whether the
  event type says the hit result is valid; the fix belongs at the read (reject an index outside the pool), once,
  for every caller.
- `fname_text` (`mod/src/ue/reflect.cpp:117`) passes the FName to the engine's `g_name_entry` with no check of its
  own and reads up to the header's length from whatever comes back: a wild index gives garbage text or an access
  violation. That is the read to guard.

## Step 1 answers (senior, static plus logs, 2026-11-13)

- Garbage hit bone: closed by card 238 (`fname_entry_checked` guards `hit_name_token`, `hit_c132.cpp:132`). Not
  part of this card any more.
- The fell check cannot tell the two cases apart. Every `fell through the world` row in 15 host logs fires at the
  first sample 1000 uu under the start (`body 0 z 941, start 2021`; `8588, start 9589`), then
  `g_pose_mesh_sleep` stops the bodies where they are. No row in the present logs is below z -5000. The chapter
  kill z is -22000 (`card046 ... KillZ -22000`, `c46_kill_z`, `hooks_ubw.cpp:1287`).
- Play-visible effect: a corpse that falls off a ledge more than 1000 uu high stops in the air on every machine
  (the client gets one settled final there: `c242-c3m1` guid 0x742, `stream final ... snap 2 (settled)` 10 ms
  after the host row).
- Open, one row decides it: whether the five c242-c3m1 corpses (start z -329 to 175, asleep at -917 to -1460) were
  over a drop or went through the floor. No floor position is logged.

## Step 2 spec

- `pose_fell_check` (`pose_c132.cpp:649`): at the 1000 uu mark, trace once straight down from body 0 to the
  chapter kill z against world static. Floor found: a long fall; log `card189: host pose guid ... long fall, floor
  at z F (D uu below)`, do not sleep, keep streaming. No floor: fell through the world; sleep as now. In both
  cases, body 0 under the kill z sleeps the bodies (bound for every row).
- `hit_queue` (`hit_c132.cpp:194`): when the NPC is a corpse with a ragdoll root (`c132_ragdoll_root`), `at` and
  `r.host_at` are that root. The client's `hit_drift` then compares with its own copy's ragdoll root for a
  corpse, the actor location otherwise; both ends use the same point.
- Files: `pose_c132.cpp`, `hit_c132.cpp`. Trace binding: reuse the one the aim trace of card 264 uses.
- Predictions (rides a FED_C3M1 run, no extra run): each former `fell through the world` row becomes either a
  `long fall` row followed by a settled final at the floor z (within 100 uu), or stays `fell through` with `no
  floor`. If any stays `fell through`, card the floor collision of ragdolls as its own bug. 0 `uu from the NPC`
  WARN rows on corpses.

## Addendum: the client's copy keeps falling after the host's sleep (run c230-c3m1-d2, senior, 2026-11-13)

- Evidence: host `fell through the world: body 0 z -978, start 189; bodies put to sleep` for guid 0xe40 at
  235343491. The client got the final as "settled" (snap 2), freeze check `simulating 1; held 0`, error 0.0. Digest
  round 13 has the client's body at z -37769, round 14 at z -729516; the host's stays near z -1000. The other 5
  fell rows of this run did not diverge.
- Cause (source): `g_pose_mesh_sleep` on the host makes the next sample a settled final (`pose_c132.cpp:585`, flags
  0). A settled final on the client is posed, slept and left simulating with gravity on (`pose_try_apply`,
  `pose_stream_final`). With no floor under it, any wake sends it down without end. What woke 0xe40 is not logged.
- Fix by class (part of step 2, same file): a corpse the host stops in the air is sent as a frozen final, not a
  settled one. `pose_fell_check` stops the simulation (`g_orig_c132_all_bodies(mesh, false)`) where it now sleeps
  the bodies, for the "no floor" case and the kill z bound; the existing sample path then sends `kPoseFrozen` and the
  client freezes the copy (`pose_freeze_step`). Attack before coding: the host's bodies must not snap to the anim
  pose when simulation stops (card 140 run 13 saw that on the client in the apply tick); the row is logged with
  body 0 before and one tick after.
- Prediction: for every `fell through the world` guid, client row `card140: client pose freeze check ... simulating
  0; held 1`, and 0 `WARN card132: client round N pose guid` rows for those guids.

## Build 4 run c230-c3m1-b4e (DLL `5e2ce00acad8efa3`), senior verdict 2026-11-13

- Addendum fix held: 6 host `fell through the world` rows (guids 0x670, 0x7a0, 0x858, 0x89e, 0x8f0, 0xf60), each
  `simulation stopped and held`; the client logged `pose freeze check ... simulating 0; held 1` for each, and no
  round pose WARN for those guids.
- Not exercised: the long-fall branch (a corpse that falls over 1000 uu onto a floor). No such death in this run.
  It stays open until a run has one; no run is spent on it alone. Where it can be forced: a kill on the FED_C3M1
  monorail platform edge or the FED_C3M2 dam wall, in a soak of those maps.

## Closed (2026-11-17 11:11)

The no-floor and kill-z branches hold: 13 `card189` rows in 5 logs, each `simulation stopped and held`, and no round pose WARN for those guids. The long-fall branch (over 1000 uu onto a floor) has not occurred in any run. Reopen on a corpse pose WARN for a guid that fell more than 1000 uu. Evidence base: the 202 host and client logs of builds b150 to b180 written since 2026-11-16.
