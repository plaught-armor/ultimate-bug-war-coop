# 329 Heaviest network traffic

User, 2026-11-14: "We should look into the heaviest packets". Senior measurement and static trace, no code yet.
After the playtest blockers (soak-first); steps A and B are small enough to ride a batch build.

## Measured (playtest host log `client/playtest-menu-20261113-182510.log`, play build 4, 649 windows of 5 s)

- Peak window 96941 B/s of the 250000 B/s limit (39%); median 22495 B/s.
- `not net ready` 1175 of 280173 frames; 188 `stall frame` rows. No evidence yet that any playtest bug came from
  this; the cost is delay of reliable rows behind a burst.
- Replicated total 83.5 MB: `ClientMessage` (the mod channel) 53.1%, `BP_AI_Trooper_Morita_C` props 16.4%,
  `BP_AI_WarriorBug_C` props 11.9%, `BP_AI_Trooper_NonCombat_C` props 4.6%, the player pawn 3.3%.

Mod channel, 40.0 MB by op ("rows" in the netbudget row are frames):

| Op | Share | Frames | B/frame | Peak per 5 s |
|---|---|---|---|---|
| Fx | 40.3% | 117305 | 137 | 212870 |
| Pose | 11.1% | 13153 | 339 | 73560 |
| GibPart | 10.1% | 34472 | 117 | 81122 |
| Digest | 9.4% | 1856 | 2030 | 129052 |
| GrenadeSpawn + GrenadeExplode | 8.7% | 41209 | 84 | 15872 |
| Marker | 4.2% | 2963 | 567 | 32700 |
| WorldOp | 4.1% | 11797 | 141 | 115924 |
| CollisionOp | 3.9% | 10326 | 151 | 309934 |

## What the encoding already does (settled, static)

- Transport (`mod/src/ue/chan.h`): a frame is an FString in a reliable RPC. Fixed cost per frame: 6 prefix units,
  op, terminator, plus the RPC and bunch header. A blob packs bytes into 15-bit units, sent as UTF-16: 16 wire bits
  per 15 payload bits.
- Text ops are packed by card 150 (`chan_pack.h`: varint numbers, dictionary for strings). Playtest: text 25054441
  bytes went as 11351113 packed, 1987 of 122553 frames raw, dictionary never full. The codec is not the problem.
- Fx is binary rows with asset and name tables (`fx_c136_net.cpp`): about 32 B per row at 3.3 rows per frame.

So the weight is row count, not row format, except Digest and Marker.

## Findings by op

1. **Fx: count.** Largest map: 360331 host events, 279565 rows sent in 84628 frames. By point:
   `ExecuteGameplayCue_NonReplicated` 86376, `NiagaraDeactivate` 66040, `NiagaraDeactivateImmediate` 55604,
   `NiagaraActivate` 38914, `SpawnSoundAttached` 27841. No distance rule: an effect at any range is sent. Not
   settled: which assets and which owners make up the count (the totals row is by point only).
2. **Digest: 2030 B every round, always.** `digest_c132.cpp`: every 30 s the host sends the level table, the class
   table and all slot sums (`V`, `C`, `H` frames) to each PC even when nothing differs. Log only, nothing is applied.
3. **Pose: resnaps.** Largest map: 2496 snaps, 2009 of them resnaps, 1525 unsettled; 1047967 bytes. Four snaps in
   five repeat a corpse already sent.
4. **GibPart: samples.** Largest map: 5628 spawn rows, 53994 flight samples, 3190167 bytes: about 10 samples per gib.
5. **GrenadeSpawn: every relayed round** (player, mech and turret rounds, `c76_relay`), one text frame each, class
   path and 13 floats as text. 21729 frames.
6. **Marker: 567 B per hit** to the shooting client: the value map and tag names as text per hit.
7. **CollisionOp and WorldOp bursts** (309934 B in one window): join replay and map arm. One-off; leave.
8. **NPC props**: 20 B per update, about 671000 updates for `BP_AI_Trooper_Morita_C`. Engine replication; the rate
   per NPC by distance is not measured.

## Steps

A. **Digest (fix, settled).** Send the `V` and `C` tables only when they changed since the last round sent to that
   PC, and one whole-digest sum first; the `H` slot sums go only when the client answers that its whole sum differs.
   Prediction: Digest falls from 2030 B per round to under 100 B on a round with no mismatch; a round with a
   mismatch logs the same mismatch rows as now.

B. **Fx step 1 (diagnostic, host, map end).** Top 20 assets by rows sent with bytes, and rows sent by distance from
   the receiving PC's pawn in bands (under 3000, 3000 to 8000, 8000 to 20000, over 20000 uu), split one-shot against
   journal rows. No fix before this row. Expected fix classes, by what it shows:
   - one-shots beyond the range at which the effect can be seen or heard are not sent to that PC (journal rows and
     loops always go: they are state, and a player can walk up to them);
   - a Deactivate of a row never sent to that PC is not sent;
   - a repeating asset from one owner (footsteps, idle loops) gets a per-owner rate.
   This must keep the user rule "every host visible effect reaches the client": the cut is only what the receiver
   cannot perceive, proven by a client row at the cut distance.

C. **Pose step 1 (static).** Trace why a settled corpse resnaps (`pose_c132.cpp`, the kPoseForceMs path and the
   card 189 floor trace) and whether a resnap can send only the bodies that moved.

D. **GibPart, GrenadeSpawn, Marker (after A to C, only if the soak peak still passes half the limit).** Gib samples
   at a lower rate with client interpolation; rounds as blob rows batched per tick; marker tags through a table.

E. **NPC props step 1 (diagnostic).** Per class: updates per second by distance band from the nearest player.
   A fix (update frequency by distance) only if the far bands carry most of the bytes.

## Sibling sweep

Every op that sends on a timer whether or not its value changed (the Digest pattern): list them from
`chan_ops.cpp` senders before step A is coded.

## Done when

On a C1M1 soak with the same fight load: peak window under 60000 B/s, `not net ready` 0 frames outside the join
and the map load, and every receiver-side effect row of the soak unchanged against the run before the change.

## Step C result (senior, static plus logs, 2026-11-14): no fix; one optional rate cap

Why a corpse already sent is snapped again, from `pose_c132.cpp` and the logged rows:

1. `c132_pose_sim_change` (line 3369 on): the game's `SetAllBodiesSimulatePhysics(1)` on a tracked corpse sends a
   snap at once, flagged unsettled, and a settled snap follows. This was the bulk of the playtest count: of the 81
   resnap rows the play build 4 host log kept (the row cap hides the rest), 51 are `UNSETTLED after 0 ms`, 15
   settled, 15 FROZEN. Example guid 0x28c: 9 snaps in 2.1 s.
2. `pose_host_row` (line 833): body 0 more than `kPoseMoved` (20 uu) from the sent root puts the row back to Moving;
   it streams (card 152) and one final snap follows.
3. `pose_host_row` (line 841): an unsettled snap is always followed by the settled one.
4. The `kPoseForceMs` (8 s) forced snap. No logged resnap row is of this kind (every unsettled one says 0 ms).

Card 339 (after play build 4) already removed the case where the `1 -> 1` call changes nothing. What is left is real
motion: in `logs/b11-soak-c2m1-host.log` (after card 339) 35 of 45 logged resnaps are still `UNSETTLED after 0 ms`,
and each sits on a `SetAllBodiesSimulatePhysics(1)` `1 -> 1` call with the corpse's health falling by 32 per call
(guid 0xd0: -262, -294, -326, ... at gaps of 71 to 781 ms): a player shooting a corpse, the blueprint waking the
ragdoll per hit, limbs moved since the last snap (`r->woke`), so the snap is wanted. That is about 3 snaps of 444 B
per second per corpse under fire, the same as the card 152 stream rate (one sample per 250 ms), so moving these to the
stream would save nothing.

Sending only the bodies that moved: not worth it here. A hit ragdoll moves most of its 22 bodies, and the row is
14 B per body after a 12 B head.

Optional, small, not built: cap wake snaps to one per `kPoseStepMs` (250 ms) per row, the later call's snap
replacing an earlier one in the same step (guid 0xd0 had two calls 71 ms apart). Only if step B to E leave Pose among
the top ops; the largest-map Pose total (1047967 B) is a third of GibPart's (3190167 B).

## Step B: per-PC rows (junior, b48, 2026-11-14)

Build b48 `6225789a575f9809` (build-b5; also carries card 363's caller table row). Diagnostic only: no send rule
changed. The b11-era step B rows summed all PCs; b48 splits them by receiving PC and adds the end-row check.

Per-row cost: `fx_stat` (`mod/src/ubw/fx_c136_net.cpp`) is counter updates on fixed tables, with no allocation and no
log line. The pawn location is read once per PC per flush, and the band test compares squared distances. The asset
table is the host's existing asset rows (`kAssets` 2048, indexed by host asset row). A row with no asset row counts as
"no asset", which works as the overflow counter.

End-row check: each journal row's E or R send sets a bit for its peer slot in a 16384-slot table keyed by row id
(`id & 16383`). Each slot stores its id. A D or X row then reads the bit of the row it ends:
- bit set: sent to this PC;
- bit clear: never sent to this PC;
- slot taken by a later id: untracked.

A stop with no journal row (sent as a full E row) is counted on its own.

Predicted rows, host, at each map end and map arm. Per PC that was sent any row:

    card329: host fx map (<when>) pc <pc name> asset <i> <path>: <n> rows <b> bytes          (up to 20, most rows first)
    card329: host fx map (<when>) pc <pc name> no asset: <n> rows <b> bytes
    card329: host fx map (<when>) pc <pc name> one-shot rows by distance from the receiving pawn: under 3000 <n> rows <b> bytes, 3000 to 8000 ..., 8000 to 20000 ..., over 20000 ..., no location ...
    card329: host fx map (<when>) pc <pc name> journal rows by distance from the receiving pawn: (same bands)
    card329: host fx map (<when>) pc <pc name> end rows by distance from the receiving pawn: (same bands)
    card329: host fx map (<when>) pc <pc name> end rows: <a> of a journal row sent to this pc, <b> of a journal row never sent to this pc, <c> untracked; <d> stops with no journal row

Then, once per map end:

    card329: host fx map (<when>): <k> receiving pcs with rows

Predictions for b48 (FED_C2M1, one client, a few minutes of real fire and grenades):
- `1 receiving pcs with rows` at the C2M1 map end.
- The weapon fire and impact assets (cues and muzzle or impact Niagara) top the asset list.
- One-shots carry most rows, mostly under 3000, because both players fight side by side.
- `untracked` is 0.
- `never sent` is small, under 5% of the end rows. An X row uses its journal row's own filter, so a row the filter
  skips also skips its end. What is left: D rows whose stop event has a different filter from the spawn, rows the
  join replay skipped (inactive, or on a run-time target), and E rows that did not fit their frame.

### b48 result (run 1 of 3, scenario passed)

Run: `tools/scenarios/b48-c329b-fx.txt`, GOD=1, FAST=1. FED_C1M1 start, travel next to FED_C2M1, then 4 fight rounds
of 3 WarriorBugs each with real Morita fire from both players and 4 grenade throws (4 `card074` GrenadeSpawn rows),
then EndLevel and Next. The C2M1 map ran 86 s (map arm t=349416202 to map end t=349502279). Logs: `logs/b48-host.log`
and `logs/b48-client.log`.

The C2M1 map end rows, as logged (pc BP_PlayerController_C, the only receiving PC):

| asset (top 20 by rows) | rows | bytes |
|---|---:|---:|
| NS_FX_Gore_BloodHit_Green01 | 1154 | 16721 |
| NS_FX_Enemy_Morita_MuzzleFlash4 (AI trooper) | 1038 | 21798 |
| NS_Compute_DamageAndRadius (wound shading) | 757 | 19936 |
| NS_FX_Bugs_WalkGround | 526 | 13316 |
| MS_WPN_AI_Morita_Mk1 | 519 | 21279 |
| GameplayCue.Impacts.MoritaRound.AI | 508 | 15952 |
| GameplayCue.Impacts.Footsteps.BugWalk | 496 | 21095 |
| NS_FX_Morita_MuzzleFlash3 | 480 | 11520 |
| NS_FX_Weapon_Morita_Casing | 480 | 7680 |
| NS_FX_Weapons_MoritaHit_Ground | 469 | 9561 |
| NS_FX_Gore_BloodGib_Green | 352 | 5104 |
| NS_FX_FlakbugProjectile_AllInOne | 336 | 5040 |
| NS_FX_Flakbug_MuzzleFlash | 336 | 7392 |
| GameplayCue.Impacts.Footsteps.AIHumanRun | 257 | 11027 |
| MoritaMK1Fire_MS | 240 | 8400 |
| Splats_MS | 227 | 9253 |
| MS_EMIT_Ambient_Morita_Fire | 190 | 6460 |
| MS_ATK_FlakBug_Fire | 168 | 5880 |
| NS_FX_Viking_ThrustersCombined | 143 | 3594 |
| GameplayCue.Impacts.MoritaRound | 122 | 4538 |
| no asset | 38 | 527 |

| kind | under 3000 | 3000 to 8000 | 8000 to 20000 | over 20000 | no location | total |
|---|---|---|---|---|---|---|
| one-shot | 3163 / 115191 B | 210 / 8742 B | 61 / 2610 B | 424 / 15390 B | 0 | 3858 / 141933 B |
| journal | 1824 / 56098 B | 73 / 2667 B | 40 / 1024 B | 745 / 24270 B | 0 | 2682 / 84059 B |
| end | 2544 / 26764 B | 131 / 1986 B | 207 / 5296 B | 665 / 2854 B | 54 / 756 B | 3601 / 37656 B |

End rows: 2535 of a journal row sent to this pc, 0 of a journal row never sent to this pc, 0 untracked; 1066 stops
with no journal row. The `card136` totals row for the same map: 10758 rows sent in 3190 frames, 117 assets, 1 bad row
(the existing "event row does not fit" counter).

Against the predictions:
- 1 receiving PC: as predicted.
- Weapon and impact assets top the list: as predicted. Most of them come from the AI troopers' Morita fire
  (MuzzleFlash4, MS_WPN_AI, MoritaRound.AI), not from the players.
- One-shots mostly under 3000: as predicted (82% of rows).
- Untracked 0: as predicted.
- Never sent: 0, under the predicted 5%. The 1066 stops with no journal row are a separate case: stops of components
  that never had a journal row, sent as full E rows.

The numbers point to fix class 3, a per-owner rate for repeating assets. Bug and AI footsteps (BugWalk Niagara and
cue, AIHumanRun) add up to 1279 rows and 45438 B. The AI troopers' repeating fire (MuzzleFlash4, MS_WPN_AI_Morita_Mk1,
MoritaRound.AI, Ambient_Morita_Fire) adds up to 2255 rows and 65489 B. Together that is 42% of the 263648 B.

Class 1, the one-shot distance cut, would save 485 one-shot rows (18000 B, 7%) beyond 8000 in this layout. Class 2
in its card form, a Deactivate of a row never sent, reads 0. Its nearest case is the 1066 stops with no journal row.

## Step B senior review (2026-11-14): diagnostic accepted, no Fx fix now

b48 `6225789a575f9809` in game2 (lane 1 still `d20f47aa1b0383a5`), run b48: rows as the junior reported, FATAL 0.
C2M1 fight map, 86 s: Fx 263648 B to the one receiving PC, about 3066 B/s, against the card's 60000 B/s peak limit.

- Class 1 (one-shots beyond 8000 uu): 485 rows, 18000 B, 7%. Too small to act on.
- Class 2 (Deactivate of a row never sent): 0.
- Class 3 (repeating assets): footsteps and AI trooper fire are 42% of the bytes, but a per-owner rate drops muzzle
  flashes and steps the client can see and hear. That breaks the user rule "every host visible effect reaches the
  client", so it is not done on these numbers. If a playtest window shows Fx alone near the limit, the lossless
  route is one frame that starts a repeating effect on an owner and one that stops it, the client repeating locally.

The per-PC rows stay in the build and ride the next play build, so the next playtest log gives them for a real
fight. Decide from that log.

## Step E: NPC props by distance band (junior, b49, 2026-11-14)

Diagnostic only, no send change. Site: the existing per-class props count, `detour_net_replicate_actor` in
`net_budget.cpp` (the `netbudget: host   props <class> <bytes>, <calls>` window rows that gave finding 8). The NPC
branch there (`is_a(actor, g_npc_class())`, already taken per update) now also adds the update's bits to a per-class
map table by band. No second hook.

- Band per NPC: a 1024-slot hash table keyed by the actor address (8 probes) holds the NPC's band, its class row and
  the next read time. The distance to the nearest player pawn (the host's and every server copy's, the pawn list
  `c140_pace_step` already fills each tick) is read only when 250 ms have passed since the last read for that actor.
  Per update: one hash probe, a class pointer compare and two adds. A slot unused for 5 s is free for a new actor.
- Bands: under 3000, 3000 to 8000, 8000 to 20000, over 20000 uu, no player pawn, and "no slot" (table full: counted,
  never banded by a per-update read).
- Class table: 48 rows, the class name copied at insert (a class can be freed by the travel before the print).
  A full table counts the rest as "other". Each full table prints one WARN row per map:
  `WARN card329: host npc props band table full (1024 slots): further NPCs count as no slot` /
  `WARN card329: host npc props class table full (48 classes): further classes count as other`.
- Print at `net_budget_arm_map` (map end and map arm, `c98_arm_map`), only when a row was counted, then reset.

Predicted row formats:

```
card329: host t=<ms> npc props map: <s> s, <n> classes, <B> B in <U> updates, other <B> B in <U> updates, no slot <U>
card329: host t=<ms> npc props <class>: <B> B in <U> updates (<b> B each); under 3000 <B> B <U> (<r>/s); 3000-8000 <B> B <U> (<r>/s); 8000-20000 <B> B <U> (<r>/s); over 20000 <B> B <U> (<r>/s); no pawn <B> B <U>; no slot <B> B <U>
```

Top 10 classes by bytes, one row each.

Prediction for the b48 fight scene (FED_C2M1, players at 229312 38956, 4 rounds of 3 WarriorBugs spawned 200 to
1100 uu away):
- `BP_AI_WarriorBug_C` puts more than 80% of its bytes under 3000. The spawned bugs fight next to the players, and
  the map's own bugs are mostly dead or idle (an idle NPC writes no bits).
- `BP_AI_Trooper_Morita_C` puts most of its bytes in 3000 to 20000. The b48 Fx rows put 745 of 2682 journal rows over
  20000 and the AI trooper fire was the second-largest Fx owner, so the troopers fight their own scripted battles away
  from the players. They move and aim constantly.
- Summed over all NPC classes, the under-3000 band carries the most bytes, but by under 60%. That is not "the far
  bands carry most of the bytes", so no fix follows from this scene. The deciding numbers would come from a playtest
  log, where troopers and bugs fight at every range.

Wire figures for the same run (netbudget `chan bytes by op`, 17 windows of 5 s, frames with their prefix and the
UTF-16 packing; the 263648 B above is row payload): Fx 411178 B, Pose 264170, GibPart 111706, Dropship 80066,
WorldOp 78538, Digest 53592, Hit 48512, Marker 40486, GrenadeSpawn 34874, CollisionOp 28044, Anim 24516,
GrenadeExplode 24148, Decal 20368; every other op under 600 B. `ClientMessage` peak window 97510 B in 5 s, about
19500 B/s, a third of the 60000 B/s limit. The conclusion stands.

## Sibling sweep result (senior, static plus b48 and b11 soak logs, 2026-11-14): no further fix

Ops that send on a timer, from the `k*StepMs`, `k*EveryMs`, `k*RoundMs` steps and the b48 op totals:

| Sender | Period | Sends when unchanged | Bytes |
|---|---|---|---|
| Digest (`digest_c132.cpp`) | 30 s | `B` and `E` only (step A works: "tables kept", 2 frames) | see below |
| Ping, Pong, LedgerAck | about 25 s | yes, by design (clock and ack) | 32 B each |
| Clock | once per map | - | 150 B |
| Dropship `L` (`dropship.cpp` `ds_host_sample`) | kDsLiveMs | no: only when it moved or changed state | 80066 B in 70 s, about 300 B per frame |
| Pose journal, `c208_step`, `c28_step`, `c22_step`, `c76_step`, `c54` track, card 319 | 100 ms to 5 s | no: change-gated or log only | - |

Digest leftover: the tables hash (`c132_tables_hash`) is over the class names in first-seen order, so a class that
appears or leaves during a fight makes the whole `V` and `C` tables go again. `b11-soak-c2m1-r3`: 6 of 12 rounds
"tables follow" (116 to 120 classes); b48: 2 of 2. Cost about 6400 B per such round (b48 round 2: 6410 B in 5
frames), 213 B/s at worst. Not worth a delta frame. b48 round 1 was 47058 B because 2 slots differed and their
detail went (the `LevelSequenceActor_1` replicate flag, adopted by card 341); that is the mismatch path, as designed.

Dropship `L` frames carry the mesh and spline names as text in every sample. About 1100 B/s while dropships fly;
left as is.

### b49 result (run 1 of 3, scenario passed)

b49 `aebfe1230e491c6d` in game2. Run b49 (`tools/scenarios/b49-c329e-npc.txt`, the b48 scene), drive rc 0, FATAL 0.
WARN: host 8 (b48 18), client 49 (b48 51). Host kinds by count: card157 13 to 2, card346 3 to 2, new card130 1
(`WB_Cutscenes_C still the top widget after 3 calls`) and card273 1 (`gib part 27 sticky state 2 not at rest`); both
kinds appear in b40 to b46 host logs. Client kinds: card132 6 to 8, card140 20 to 9, card152 3 to 2, card191 21 to 29.
None of them comes from this diagnostic, and there are no card329 WARN rows. Neither table filled.

FED_C2M1 map end, as logged:

```
card329: host t=349979755 npc props map: 82 s, 5 classes, 939098 B in 41515 updates, other 0 B in 0 updates, no slot 0
card329: host t=349979755 npc props BP_AI_WarriorBug_C: 553833 B in 23319 updates (23 B each); under 3000 213073 B 7905 (95.7/s); 3000-8000 133615 B 6122 (74.1/s); 8000-20000 207144 B 9292 (112.5/s); over 20000 0 B 0 (0.0/s); no pawn 0 B 0; no slot 0 B 0
card329: host t=349979755 npc props BP_AI_Trooper_Morita_C: 344523 B in 16327 updates (21 B each); under 3000 174061 B 8549 (103.5/s); 3000-8000 115902 B 5303 (64.2/s); 8000-20000 54559 B 2475 (30.0/s); over 20000 0 B 0 (0.0/s); no pawn 0 B 0; no slot 0 B 0
card329: host t=349979755 npc props BP_AI_Trooper_RadioTrooper_C: 15126 B in 690 updates (21 B each); under 3000 15126 B 690 (8.4/s); ...all other bands 0
card329: host t=349979755 npc props BP_AI_Trooper_SquadLeader_C: 13921 B in 634 updates (21 B each); under 3000 13921 B 634 (7.7/s); ...all other bands 0
card329: host t=349979755 npc props BP_AI_Trooper_Medic_C: 11694 B in 545 updates (21 B each); under 3000 7998 B 377 (4.6/s); 3000-8000 3696 B 168 (2.0/s); ...all other bands 0
```

(The FED_C1M1 row before the travel covers 3 s and 34 updates.)

Bytes summed over all NPC classes:

| Band | Bytes | Share |
|---|---:|---:|
| Under 3000 | 424179 | 45% |
| 3000 to 8000 | 253213 | 27% |
| 8000 to 20000 | 261703 | 28% |
| Over 20000 | 0 | 0% |

That is about 11450 B/s of NPC props to the one PC.

The predictions against the log:
- WarriorBug: wrong. Only 38% of its bytes are under 3000, and 37% are at 8000 to 20000 (112.5 updates/s). The map's
  own bugs far from the slope are not idle: they move and write bits.
- Morita troopers: wrong. 51% are under 3000; the troopers fight near the players.
- All classes summed: under 3000 is the largest band at under 60%, as predicted.

Beyond 8000 is 28% of the bytes, and beyond 3000 is 55%. On this scene the far bands (8000 and up) do not carry most
of the bytes. No fix from this run.

## Step E senior review (2026-11-14): diagnostic accepted, no NPC props fix; card parked for the playtest log

b49 `aebfe1230e491c6d` in game2 (lane 1 still `d20f47aa1b0383a5`), run b49, FATAL 0 on both sides, rows as reported.
Code read (`net_budget.cpp` `npc_props_add`, `npc_band_slot`, `npc_band_read`): attacked slot reuse at a freed
address (class compare resets the slot), a live actor behind a stale slot in the same probe run (the whole run is
searched before a free slot is taken), table full (counted as no slot or other, one WARN each). No defect found.
Limit of the row: the band is the distance to the nearest of all pawns, not to the receiving PC; right for two
players only.

C2M1 fight, 82 s: 939098 B, about 11450 B/s. Under 3000 uu 45%, 3000 to 8000 27%, 8000 to 20000 28%, beyond 0.
The far band does not carry most of the bytes, so by the step's own rule there is no update-frequency fix.

Card state: A fixed, B and E diagnostics in the build, C closed, sibling sweep closed, D not started (its condition,
a peak over half the limit, is not met: b48 peak about 19500 B/s). Open only: "Done when", read from the next
playtest log's card329 and netbudget rows.

## 2026-11-16 13:07: C2M1 soak numbers on b131 (`c423b131a`, 243 s), senior: no fix

- NPC props (the engine's own replication): 2707568 B in 120318 updates, 11.1 kB/s. WarriorBug 1479624 B, of which
  992951 B at 8000 to 20000 uu (178.2 updates/s, 23 B each); Trooper_Morita 972127 B. NPCs read
  NetUpdateFrequency 100.0, yet a moving WarriorBug writes about 9 rows a second (card 140 run 7), so a far-band
  frequency cap at 10 Hz would save nothing and a cap at 5 Hz about 2 kB/s, at the price of coarser far positions
  for the hit checks (card 140). Not worth it at this volume.
- Mod channel, 5 s windows near the end: Fx 5.1 to 7.1 kB/s (132 to 148 B a frame, 2.2 rows a frame, 29 B a row:
  451858 B payload in 15676 rows on the map), Pose 1.6 to 5 kB/s, everything else under 1 kB/s. GrenadeSpawn and
  GrenadeExplode: 1587 and 1537 frames at 75 B, about 1 kB/s together; a binary row would save about a quarter of
  that. Not worth a format change.
- Top Fx assets by bytes: Morita muzzle flash 97048 B (4543 rows), AI Morita shot sound 93407 B (2267 rows),
  `GameplayCue.Impacts.MoritaRound.AI` 80240 B (2377 rows). All AI trooper fire, already at 21 to 41 B a row.
- Sum: about 25 kB/s to the one client against the 60 kB/s `ClientMessage` limit measured before; no row format or
  rate stands out. The byte side of the audit stays closed until a playtest log shows a different mix.

## Closed (2026-11-18 14:38, senior)

Evidence: the byte audit is done with no fix needed. C2M1 soak on b131 (`c423b131a`): about 25 kB/s to the one
client against the 60 kB/s `ClientMessage` limit, NPC props 11.1 kB/s, Fx 5.1 to 7.1 kB/s, no row format or rate
stands out. Part A fixed, B and E diagnostics stay in the build, C and the sibling sweep closed earlier.

Not done: part D (update-frequency cap), because its condition (a peak over half the limit) was never met; b48 peak
was about 19500 B/s.

Reopen when: a playtest or soak log shows a card329 or netbudget peak window over half the 250000 B/s limit, or the
op mix changes so that one op carries more than half the mod channel bytes.
