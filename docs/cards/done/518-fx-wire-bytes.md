# 518: Fx wire bytes, halve the card 136 rows (efficiency)

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: Opened 2026-11-19 by senior from the c263b (b288 cc587891d7bf7846) FED_C3M2 run. User: "1.23mb over how long? we
should aim for smaller."

## Finding (c263b host log, one client, 305 s)

- Chan sent 2,865,062 B in total (about 9.4 KB/s). Fx is the top op: 1,231,901 B (about 4.0 KB/s average; the peak
  5 s window was 34,320 B, about 6.9 KB/s). Next: GibPart 400,012, GrenadeSpawn 306,454, Batch 251,620,
  GrenadeExplode 242,282, Pose 155,536.
- The card329 map end rows (host line 64876 and the asset rows before it) list 30003 E rows and 885,291 B over the
  28 assets. There were also 6850 end rows and 3750 stops. The remaining 346 KB is blob framing, A/N table rows and
  end/stop rows.
- The AI Morita trooper fire is the bulk. Each shot sends three or four rows:

| Asset | Rows | Bytes | B/row |
|---|---|---|---|
| NS_FX_Enemy_Morita_MuzzleFlash4 | 9679 | 209,914 | 21.7 |
| MS_WPN_AI_Morita_Mk1 (sound) | 4840 | 201,759 | 41.7 |
| GameplayCue.Impacts.MoritaRound.AI | 3966 | 153,396 | 38.7 |
| NS_FX_Weapons_MoritaHit_Ground_AI | 1922 | 49,379 | 25.7 |

  Together that is 20,407 rows and 614,448 B: 69 percent of the E bytes.
- An E row (`host_body`, `mod/src/ubw/fx_c136_net.cpp`) costs 9 B before any field: tag 1, id varint (3 at ids over
  16k), point 1, asset u16 2, mask u16 2. Every vector is 3 f32 (12 B): location, scale, look, world, and the placed
  location.

## Cuts (cheapest first; measure after each)

1. **Header.** Write the asset as a varint (under 128 is 1 B) and the id as a varint delta from the previous E row in
   the same blob (1 B). That saves about 3 B on every row: about 90 KB on c263b.
2. **Vectors.** Send locations and world locations as zigzag varint whole uu (3 B per component on a map under
   ±1,000,000 uu, against 4). Sub-uu precision is not visible on an effect spawn. Scale and look keep f32 only if a
   measured row needs them; otherwise use int16 per mille.
3. **Same-place bit.** A new mask bit says "location (and world) same as the previous E row in this blob". The
   muzzle flash, the shot sound and the casing of one AI shot share a spot. That saves 9 to 12 B on most sound rows.
   The mask is full at 16 bits, so the mask becomes a varint (1 to 3 B), which also saves 1 B on short masks.
4. **Framing.** Find what the 346 KB outside the E rows is: the blob frame header per send, A/N rows, X rows, and
   the 3750 stops with no journal row (a stop with no copy on the client should not be sent at all). Log a per-tag
   byte split in the card329 map-end rows (A/N/E/R/X/D rows and bytes) so this is a number, not a guess.

The wire format change is host and client in the same DLL (friends-only build, both sides always match). No compat
shim.

## In source (2026-11-19, senior; object compiles, not yet in a DLL)

All in `mod/src/ubw/fx_c136_net.cpp`; host and client change together. The pre-518 copy is
`build/fx_c136_net.cpp.pre518`.
- The asset is a varint of index + 1 (0 = none). `kNoAsset` is gone.
- The location, world location and placed location are zigzag varints of whole uu (`put_pos`, `get_pos`).
- Look (a sound's volume, pitch and start time; a decal's life span) is 3 zigzag varints of thousandths
  (`put_milli`). Every sound row paid 12 B of f32 here; 1.0, 1.0, 0 now takes 5 B.
- The E/R id is a zigzag delta from the frame's previous E/R row. `frame_add` rewrites it (`row_id_delta`), so the
  journal still keeps absolute ids for the join replay. Each frame starts from 0 on both sides
  (`Frame::last_id`, `g_client_frame_id`).
- `c136_step` flushes the queue at most every 30 ms, or at once when the queue is half full, so fewer frames pay the
  chan framing. A row waits under 47 ms (GetTickCount64 steps by about 15.6 ms); the latency target is 100 ms.
- New row at each map end: `card518: host fx bytes by tag (<when>): A ... N ... E ... R ... X ... D ...; <frames>
  frames <blob bytes> blob bytes`. `fx_stat` and card329 now count the bytes each row actually took in the frame.

Not done yet: cut 3 (same-place bit), and stopping the 3750 stops with no journal row. They wait for the card518
tag split.

## Result c263d (b290 ae05cba6d5406b5b, 424 s) and step b

- Chan Fx: 848,942 B in 424 s, about 2.0 KB/s (c263b: 1,231,901 B in 305 s of fight; the fight shapes differ).
  Prediction 1 (under 600,000 B) missed.
- `card518` map-end row (host line 66038): A 165 rows 15,047 B, N 576 / 16,565, E 24,310 / 602,113 (24.8 B/row),
  R 84 / 2,381, X 4,940 / 18,178, D 1,437 / 10,730; 8410 frames, 665,014 blob bytes. Chan framing is the other
  184 KB (about 22 B per frame). The rows on maps that sent nothing read 0, as they should.
- B/row for the AI Morita assets: MuzzleFlash4 21.7 to 13.9, the shot sound 41.7 to 27.8, the impact cue 38.7 to
  27.1, the ground hit 25.7 to 19.3. Prediction 2 (under 15) holds only for the muzzle flash. Client card136 WARN 0.
- Step b, in source (object compiles; backup `build/fx_c136_net.cpp.pre518b`): the world and placed locations are
  written as whole uu deltas from the row's own location (`put_pos_from`, `get_pos_from`), and a new map-end row
  `card518: host fx body bytes by field` counts the rows and bytes of each mask field. The next cut (same-place bit
  across rows, frame count, the seed) is chosen from that row.

## Step c: frame-local location delta (2026-11-20, in source, object compiles; for b316)

c263L315 field row: loc is the largest E field, 554 KB on the C3M2 map at 7.0 B/row (3 absolute zigzag varints of
whole uu). Rows in one frame cluster (one fight, one muzzle, its impacts), so the location now goes as a delta from
the frame's previous E or R row with a location, like the id delta:
- Host: `frame_add` calls `row_frame_delta` (replaces `row_id_delta`), which rewrites the id and, when the mask has
  `kBitLoc`, the 3 location varints as zigzag deltas from `Frame::last` (`FrameLast`, zero at each frame's start). The
  journal body stays absolute, so join replays are unchanged. World and placed stay deltas from the row's own
  location.
- Client: `client_parse` adds the deltas to `g_client_frame` (reset per frame, as before for the id).
- New map-end row `card518: host fx loc on the wire (<when>): N rows B (average x)`. The field row's loc entry still
  counts the absolute body bytes, so the two side by side give the saving.

Predictions (next c263 on b316): the wire loc average under 4.0 B/row against the field row's 7.0; E B/row down by at
least 2.5 against c263L315; 0 `client fx bad row` WARN rows; the client card329 received counts match the host sent
counts.

Result c263L316 (b316 5e4d5d4e5fec2ece, 249 s; host line 133589, the C3M2 map end): `loc on the wire 63271 rows
333999 B (average 5.3)` against the field row's loc 66818 rows 464,010 B (6.9): 130 KB (28 percent) saved on the
location. Prediction 1 (under 4.0) missed: rows in one frame are spread wider than one fight. E 71,456 rows 1,607,686
B (22.5 B/row) against c263L315's 24.1: down 1.6, prediction 2 (2.5) missed. `client fx bad row` 0; digest rounds 1-8
0 mismatches. The cut stays (it holds and costs nothing); the big win is step d.

## Step d: bug footsteps (2026-11-20, diagnostic in source after b316, object compiles)

c263L315 card329 asset rows (host, C3M2 map end): the bug footsteps are now the bulk of Fx.

| Asset | Rows | Bytes |
|---|---|---|
| GameplayCue.Impacts.Footsteps.BugWalk | 28,390 | 848,437 |
| NS_FX_Bugs_WalkGround | 29,760 | 538,254 |
| GameplayCue.Impacts.Footsteps.LargeBugWalk | 1,031 | 33,203 |
| GameplayCue.Impacts.Footsteps.SmallBugWalk | 489 | 14,717 |

That is 1.43 MB of the map's Fx bytes, plus most of the 38,104 end rows (32,102 stops with no journal row). The
client blocked 80,198 outermost effect calls on that map, so it very likely makes these footsteps itself from its own
bug animations and throws them away. Keeping them on the client (as card 438 does for the wound shading and run 6
fix C for trails) would cut about 70 percent of the Fx op, but only if the client's own count matches the host's.

Diagnostic: `c518_note_blocked` (`fx_c136.cpp`) counts the client's blocked outermost calls by asset or cue tag, with
the first owner's class and how many ran inside an anim notify. New map-end rows `card518: client fx blocked (<when>)
<asset>: N calls, M in an anim notify, first owner <class>` (the 12 largest).

Decision rule for the next C3M2 run: if the client's blocked BugWalk and WalkGround counts are each within 10 percent
of the host's card329 rows, and nearly all are in an anim notify on a bug class, the fix keeps those assets local on
both sides (never sent, the client's own spawn kept). The playtest check is footstep dust under the bugs on the
client screen. If the counts are far apart (client animation skipped by distance or URO), the assets stay sent and
the next cut is a shared bug-footstep row (guid plus foot index, no location).

Fix in b318 `faf4ca850e278c96` (2026-11-20, user: "if there is a bigger cut then go for that"; shipped before the
diagnostic run, so one run measures both sides): `footstep_local` in `fx_c136_net.cpp` matches NS_FX_Bugs_WalkGround
and every `GameplayCue.Impacts.Footsteps.*BugWalk*` cue (a verdict cached per asset name or tag, cache written on the
game thread only). The host never sends or journals them (`counts.steps`); the client keeps its own
(`c136_client_own`). Component ops of the asset are matched too, so their stop rows go as well. Rows: `card518: host
bug footstep calls kept local, not sent (<when>): N` and `card518: client bug footstep calls kept, its own (<when>): M`.

Predictions (next c263 C3M2 on b318): the client's M is within 10 percent of the host's N; card518 E rows drop by about
59,000 per C3M2 fight against c263L315 (from 84,734) and E bytes by at least 1.2 MB; the chan Fx op is under 1.0 KB/s;
X/D (end and stop) rows drop too; 0 bad rows; the client screenshot shows footstep dust under walking bugs. If M is far
under N (the client's far bugs skip animation), the cut stays only if the missing dust is at a distance nobody sees;
otherwise a compact shared footstep row (guid plus foot) replaces it.

## Predictions (the same C3M2 PowerPlant scenario as c263b, one run)

1. The chan Fx op is under 600,000 B for a comparable fight time (from 1,231,901). Report it as B/s, with the run
   length, because run lengths differ.
2. The four AI Morita assets average under 15 B/row (from 21.7 to 41.7).
3. Client fx apply: 0 decode failures, 0 card136 WARN rows that are new against the c263b WARN set, and the client
   card329 received row counts match the host sent counts.
4. A receiver-side shot shows the AI muzzle flashes and impacts at the right spots (per the playtest card rules).

## Done when

Predictions 1 to 3 hold in one lane 2 run; numbers quoted here. Cut 4's per-tag split is logged even if 1 holds
without it.

Result c263L318 (b318 faf4ca850e278c96, 275 s, rc 0/0, ERR 0, WARN host 39 / client 17; C3M2 map end):
- Host `bug footstep calls kept local, not sent` 46,888; client `kept, its own` 33,961 (72 percent; the client counts
  game-thread calls only). That is more than the WalkGround calls alone (24,620 in c263L317), so the client runs its
  own BugWalk cues too. Prediction 1 (within 10 percent) missed; each side makes the steps of the bugs it animates.
- `fx bytes by tag`: E 20,703 rows 493,162 B (c263L317: 75,487 rows 1,699,238 B); blob 542,423 B against 1,762,637
  (-69 percent). Stops with no journal row 1,963 (27,934). Chan Fx summed over the netbudget windows 609,540 B
  against 1,887,422 (-68 percent), about 2.2 KB/s over the run (prediction 3, under 1.0 KB/s, missed; prediction 2
  holds). `client fx bad row` 0.
- Dust shot (`client-c263L318-c263L-c-dust.png`): warriors at melee range, gore over the floor; dust not provable.
  A dust shot needs bugs walking in at 800 to 1500 uu.

b319 `4bbefdc500752011`: the same footstep scope as b318 (WalkGround plus the BugWalk cues), and both rows gain a cue
count (`... : N (C cues)`) so the next run splits each kind. If the client's cue count is far under the host's, the
missing cue is the footstep sound of a bug the client does not animate (out of view); a cue row then stays local only
if those bugs are out of hearing range.

Result c263L319 (b319 4bbefdc500752011, 255 s, rc 0/0, ERR 0, bad rows 0; C3M2 map end):
- Host footsteps kept 64,587 (32,583 cues, 32,004 WalkGround); client 30,072 (16,612 cues, 13,460 WalkGround): the
  client runs both kinds itself, at about half the host's rate. A bug's footstep notifies fire only where its mesh is
  animated, so each side makes the steps of the bugs it renders: the same set a single player hears and sees. Before
  b318 the client got the steps of the bugs near the host's view instead. The cut stays.
- Fx blob 590,098 B (L318 542,423; a busier fight: 64,587 footstep calls against 46,888). Chan Fx 674,022 B.
- WARN host 63 (L318 39): the new rows are HitClaim and gib kinds from the fight, not Fx; b319 changed only log rows.
- Dust shot `client-c263L319-c263L-c-dust.png`: a warrior pack walks in on concrete; no puffs visible at 640x360.
  Not proof either way. Playtest check: footstep dust under walking bugs on the client, at normal resolution.

Next: the AI Morita fire (muzzle flash, shot sound, impact cue, ground hit; about 180 KB per C3M2 map). The client
blocks more of these calls than the host sends (shot sound 6,920 against 2,357 rows), so a host count of each asset's
calls before the filters is needed before keeping them local.

b320 `7cdac67dbc4c443a` (step e diagnostic, log rows only): the host feeds `c518_note_blocked` from `c136_fx_enter`
at depth 1, before `c136_net_queue` and its filters, and prints the table as `card518: host fx calls (<map>) ...` beside
`card136: host fx map totals`. The table grew from 64 to 128 rows, and 16 lines are printed. Read it by comparing
host calls, client blocked calls and host sent rows for each Morita asset. If the client blocks about as many calls as
the host makes, the client already makes that effect itself, so both sides can keep it local. If the client blocks
fewer, the rows are still needed.

Result c263L320 (b320, 241 s; drive rc 3 from the 6 usual voidsoft VOIDs; gate c448b320a PASS). Fx blob 583,725 B,
E rows 22,483 / 527,849 B. Host calls against client blocked calls at map end:

| Asset | Host calls | Client blocked |
|---|---|---|
| MS_WPN_AI_Morita_Mk1 | 2,684 | 6,901 |
| NS_FX_Enemy_Morita_MuzzleFlash4 | 2,421 | 6,706 |
| GameplayCue.Impacts.MoritaRound.AI | 2,159 | 1,269 |
| NS_FX_Weapons_MoritaHit_Ground_AI | 872 | 857 |
| NS_FX_Archer_Needle_Impact | under 872 (not in the top 16) | 9,999 (owner STWorldSettings) |

- The Morita keep-local plan fails. The shot sound and muzzle flash are blocked 2.8 times more on the client than the
  host calls them, so keeping them local would show fire the host never made. The impact cue is blocked less, so its
  rows are needed. Only the ground hit matches, and keeping it local saves about 20 KB (3.5 percent of E): parked.
- The Archer needle impact count of 9,999 is real: `c518_note_blocked` has no clamp. It is client CPU work, not
  bytes. New card if it shows in a profile.
- Field bytes (E and R bodies): loc 120,476, world 78,261, look 57,544, owner class 49,662, rot 38,376, comp 31,902,
  guid 31,213. Header (id, point, asset, 2 B mask) is about 120 KB.

Step f (senior, bigger cut, in source for b322):
1. The owner class is not sent. The client read it only in its log rows (`card136: client fx`, `card325`). It cost
   49,662 B plus its N name rows. `name_refs` no longer names it. The bit stays in the format, so an old client still
   parses.
2. Look goes as the difference from the point's usual value: 1, 1, 0 for a sound (volume, pitch, start), zero for a
   decal. A sound at the usual volume and pitch sends no look field. Up to 57,544 B.
- Rejected: world as a frame-local delta. The SoundAttached path reads `r.world` for its attenuation on every row,
  not only on a key miss, and the delta would need `row_frame_delta` to parse each body through to the world field.
- Prediction for the next C3M2 run: Fx blob under 500,000 B at a similar fight size, owner class 0/0 in the field
  row, look rows well under 11,489, sound volume and pitch on the client unchanged (no `bad` rows, no WARN for sound
  settings).

Result c263L322 (b322, 237 s, rc 0/0, digest 10 rounds 0 mismatches, WARN host 34 / client 14, the lowest so far;
host lines 150441-150462):
- The owner field is 0/0 (L320: 24,831 rows / 49,662 B). The look field is 90 rows / 360 B (L320: 11,489 / 57,544).
  `client fx bad row` 0/0, no WARN for sound settings. Both cuts hold.
- E rows: 24,986 rows, 484,981 B, 19.4 B per row (L320 23.5). The blob is 545,278 B in 3,361 frames. That is over
  500,000 B, but the fight was 27 percent bigger (113,266 fx events against 89,267; 54,358 footstep calls kept local
  against 33,802). Scaled to L320's event count it is about 430 KB, so the prediction holds at a similar fight size.
- Field bytes now: loc 128,788 (19,047 rows), world 91,937 (13,000), rot 39,780, comp 35,306, guid 35,133, placed
  20,042, flags 16,604, conc 11,690.

Step g (senior, plan; diagnostic in b324 `258b49e5276c7075`): stop sending the world point of attached rows that have
a key.
- The host's world point is the attach component's translation plus the row's location (`fx_c136_net.cpp` host
  pending, `p.world`). When the client finds the key component, it can compute the same value itself, for the
  SoundAttached attenuation and the log rows. The client plays at the world point only on a key miss.
- Rows keyed by world (guid 0, no placed name) keep the world field: the client can never find their component (59
  host-only plays in c263L322).
- Open question: the client logged 15 key misses in c263L322, out of 27,354 rows. Without the field, those 15 would
  have no place to play. b324 splits them. `card518: client key miss ... actor found|missing` rows (first 16 per map)
  and `N key misses with the key actor here` in the client net totals. If the actor is found, it can stand in for the
  world point: actor root plus the row's location. If it is missing, those rows keep the world field only if the host
  can tell them apart; otherwise step g needs another plan.
- Saving: up to about 92 KB of 545 KB per C3M2 map (17 percent).
- Result c263L324 (b324): 7 key misses, all point 2 (SoundAttached), all "actor missing", 0 with the key actor here.
  5 are guid-keyed bug sounds (4 MS_BARK_JumperBug_Death, 1 HopperBug Flaps): the bug's actor is already gone on the
  client when its death bark arrives. 2 are world-keyed WarriorBug_Pain rows, which keep the world field anyway.
  Without the world field, the 5 guid rows would have no place to play, so dropping the field for every attached row
  would lose audible death barks. Revised plan: drop world only for attached Niagara and decal rows; attached sounds
  keep it. Before building, measure the world field's bytes per point so the saving is known (the 92 KB is the
  all-points ceiling).

## Step h (senior, plan from c263L322; 2026-11-20 sent/not-sent audit)

Audit of c263L322's host fx map table against the client's blocked-own-calls table. No missing effect was found:
every asset the client blocks either has host rows or comes from a relayed cue (the client's own AI makes extra calls,
as step e found). Two cuts follow.

- **h1, stops of spawned effects as D rows.** `host_stop_of` returns 0 when the component's journal row is a spawn, so
  a Deactivate of a spawned effect goes out as a full E row keyed by component (2224 "stops with no journal row" in
  c263L322). The client already keeps each spawned copy in `client_live` under the spawn row's id, so a D row naming
  that id stops the exact copy (no key lookup). Change: drop the `!spawn` condition in `host_stop_of`. Proof: next
  c263 shows 0 "stops with no journal row" (or only stops of components never journaled) and fewer E bytes; no new
  client "no component" results for Deactivate points.
- **h2, AI human footsteps kept local.** Host sent 939 `GameplayCue.Impacts.Footsteps.AIHumanRun` rows (24,645 B). The
  client blocked 936 of its own `NS_FX_Humans_WalkGround` calls and 341 own AIHumanRun cues: its trooper animations
  make the same steps, as with bug footsteps in step d. Change: `step_text` also matches the AIHumanRun cue and
  `NS_FX_Humans_WalkGround`. Proof: host footstep kept count rises by about 939, the AIHumanRun asset row is gone from
  the host fx map table, client footstep kept rows show the human kinds.
- Not cut: sounds by distance. Rows over 20,000 uu are 198 one-shots (5,587 B) and 386 end rows (6,459 B); too small
  to risk an audible miss.
- Still not sent (unchanged, known): card432 SetObjectiveVisibility 33, card095 run-time component writes 16.
- Client table: "156 calls not counted (table of 128 full)". Raise the blocked table to 256 so the next audit sees
  every asset.
- Built 2026-11-20: b325 `8f01436458363100` carries h1, h2 and the 256-row table, on top of b324's step g diagnostic.
  Proof run: c263L325.
- Result c263L325 (b325): bad rows 0/0, digest 0. Blob 510,525 B for 135,887 host fx events (3.76 B per event)
  against L322's 545,278 B for 113,266 (4.81 B per event): 22 percent less per event. h2 holds (no AIHumanRun row
  sent; 1075 host calls counted, kept local). Table full line gone. 0 client "no component" on Deactivate points.
  h1 partly: 1633 stops with no journal row remain (L322 2224, bigger fight). Those are stops of components never
  journaled (placed components that auto-activated with no sent Activate, held card 438 rows, spawns past a full
  journal); they still need the keyed E row, because the client blocks its own Deactivate.

## Step i (senior, built in b326 `3299fdd05b0dbeb6`)

From the c263L324 host field rows (map end): loc 23,101 rows / 153,299 B, world 13,540 / 93,664, rot 8,295 / 49,770,
comp 19,078 / 38,136, guid 21,733 / 37,708, placed 3,114 / 27,681, flags 17,994 / 17,994, conc 11,702 / 11,702. Every
row also paid a fixed 2-byte mask.

- i1, mask: a varint of the mask with bits reordered by frequency (`kMaskOrder`: loc, guid, comp, flags, world, conc,
  rot first). A row with only those fields takes 1 byte. Never more than 2 in practice (3 only with seed or owner
  class). New row `card518: host fx mask bytes (...)` counts rows per mask size.
- i2, cue Normal: octahedral 2 bytes (was 3 int16, 6 bytes); error up to about 1 degree. Niagara rotations keep 3
  angles. New count `cue normals not unit` on the same row; any nonzero value means some cue uses its Normal as more
  than a direction and needs a look.
- Predictions (next c263 against L325): E bytes per row down by about 1 to 2 B; rot field bytes per row about 2 for
  cue rows; mask 1 B on over half the rows; cue normals not unit 0; bad rows 0/0; digest 0; client impact decals and
  splashes face the same way (aim proof shot of a wall impact on both sides).
- Result c263L326 (b326): E bytes per row 18.02 (420,125 B / 23,310) vs 18.99 in L325, down 0.97 B: pass at the low
  end. Mask 1 B on 18,292 rows, 2 B on 8,599, 3 B on 0 (68 percent at 1 B): pass. Cue normals not unit 0: pass. Rot
  field 3.46 B per row (5,348 rows, Niagara 3-angle rows mixed in) vs 6.0: consistent with 2 B cue rows. Blob 474,694 B
  in 3,787 frames (L325 510,525). Bad rows 0, digest 0. The decal facing proof was void (the burst hit an NPC trooper);
  L328 repeats it with a floor decal.

## Step g, built (senior, b327 `b93272657acd0c48`, on top of b326)

- Attached Niagara spawns (NiagaraAttached, NiagaraAttachedScale, NiagaraAttachedParams) and DecalAttached rows no
  longer carry the world point (`world_unsent` in fx_c136_net.cpp). The client read it only on a key miss; c263L324
  had 0 such misses (all 7 were attached sounds). On a miss the client now plays nothing and logs
  `not played (key miss, no world point)` instead of placing it at the world point.
- Attached sounds keep the world point: attenuation and the playlog read it on every row. Activate rows keep it too
  (component miss and host-only plays).
- New count `world unsent` on the `card518: host fx mask bytes` row.
- Predictions (c263 on b327 against the b326 run): world field rows down by the `world unsent` count, at about
  6.9 B each (L324 average 93,664 B / 13,540 rows); client key misses on Niagara or decal points 0; no new client
  "spawn returned null"; bad rows 0/0; digest 0.
- Result c263L328 (b328): pass. World unsent 4,468. World field 9,570 rows / 69,053 B, against 13,089 / 92,469 in
  L326: 3,519 fewer rows and 23,416 B less (6.65 B per row). The row drop is about 950 short of the unsent count
  because the run mix differs; the unsent count is measured directly. All 8 client key misses are attached sounds
  (point 2), which keep the world point, so there are 0 Niagara or decal key misses. E bytes per row 16.73
  (447,851 B / 26,765), against 18.02 in L326. Mask 1 B on 19,635 rows, 2 B on 11,590, 3 B on 0. Cue normals not unit
  0. Rot 3.35 B per row. Bad rows 0; digest 0 in all 8 rounds.
- Decal facing proof: void again. The host burst pitched -84 at its own feet and made no `MoritaHit` impact row, so
  it hit nothing traceable. The next run aims at the ground about 600 uu ahead (pitch about -30). It waits for the
  host `fx audit: SpawnSystemAtLocation NS_FX_Weapons_MoritaHit_*` row after a mark, has both sides look at that
  point, and waits for the client `decal N spawned` row.
- Result c263L329 (b329): step i and g numbers hold (E 16.88 B per row, mask 1 B on 65 percent of rows, world unsent
  4,001, bad rows 0, digest 0). The decal proof was void a third time. The host floor hit came
  (`MoritaHit_Ground at (-25730, -8045, 1930)`), but the client `decal N spawned` row has an event cap of 30 lines
  (`kDecalLines`), which L328 and L329 both reached before the step. Bullet decals are also 10 uu across, too small to
  judge in a 640x360 shot.
- b330 `bd3dfa002168f64e` replaces the screenshot proof with a measurement. The host decodes the 2 bytes it just
  wrote for each cue Normal and logs the angle to the original Normal on the `card518: host fx mask bytes` row
  (`cue normal decode error over N rows: max X deg, mean Y deg`). The client decodes the same bytes with the same
  function (`octa_decode`), so this is the error the client sees. Prediction: max under 1.5 deg, mean under 0.5 deg.
  The decal screenshot step is dropped.
- Result c263L330 (b330): pass. Cue normal decode error over 3,702 rows: max 0.92 deg, mean 0.30 deg. E 16.76 B per
  row (402,667 B / 24,022); world unsent 3,742; mask 1 B on 65 percent of rows; bad rows 0; digest 0 in all 7 rounds.
  Steps i and g are done. The host `copy walk met a freed object` WARN in this run is the card 476 guard working
  (also once each in L325 and L326), not a new bug.

## Step j, measure only (senior, b331 `bbffc15a6b9171fd`, on top of b330)

- L330 field bytes: flags 16,255 rows / 16,255 B, conc 11,130 / 11,135 B, physmat 3,464 / 3,464 B, atten
  3,330 / 3,330 B. Together 34.2 KB of the 465 KB blob (7 percent), each about 1 B per row plus its mask bit.
- Idea: if these four are fixed per asset, the A row that announces an asset could carry its defaults once, and E
  rows would send a field only when it differs. That removes the field bytes and can drop rows from a 2 B mask to 1 B.
- b331 adds a host row `card518: host fx asset first values (...)`: E rows with an asset, and how many have each
  field present and equal to that asset's first value this map, plus rows where all four equal the first values.
- Prediction: conc and atten same on over 95 percent of their present rows (sound settings are per asset). Flags and
  physmat lower, because physmat is per surface hit. Cut decision: build the A row defaults for each field over
  90 percent; drop the rest.

### Result L331 (b331)

- 24,461 E rows with an asset. Present and same as the asset's first value: flags 13,494, physmat 1,779, atten 3,142,
  conc 10,427; all four same 20,694. Shares of present rows: atten 100 percent, conc 99.4, flags 89.5, physmat 61.6.
- Field bytes 31,593 B of a 416,833 B blob (E 21,432 rows / 364,971 B). Bad rows 0, digest 0.
- Decision: cut all four. A row that differs from the default costs the same as before (the field is sent, as now),
  so a lower share only lowers the saving; it never adds bytes beyond the A row's few default bytes per asset.

## Step j, cut (senior, b332 `1a7a2a29dd23ff50`, on top of b331)

- Host: each asset's defaults (flags, physmat, atten, conc) freeze from its first E row, or as none when its A row
  goes out first. The A row carries them after the kind and text: a byte of which are set (bit 0 flags, 1 physmat,
  2 atten, 3 conc), then flags u8 and the row values as varints.
- E rows set a field's mask bit only when the value differs from the asset's default. A sent physmat, atten or conc
  is now row + 1 with 0 for none, so a row can say "none" where the default has a value.
- Client: a field missing from the mask takes the asset's default (none when the asset has no defaults).
- Measure-only code from b331 is removed. New map-end host row: `card518: host fx asset defaults (...)`, counting rows
  that left each field to the default.
- Predictions (c263L332, same scenario): field bytes drop by about 28 KB; defaulted about 13.5K flags, 1.8K physmat,
  3.1K atten, 10.4K conc; the share of 1 B masks rises; bad rows 0; digest 0; client decal and sound misses unchanged
  from L331.
- Result c263L332 (b332): pass. Defaulted flags 14,884, physmat 830, atten 3,570, conc 11,395. The four fields now
  cost 6,800 B (flags 3,845, physmat 2,910, atten 0, conc 45) against 31,593 B in L331: 24.8 KB less. The shortfall
  against the 28 KB prediction is physmat. Its default is the asset's first value, and for a per-surface field that
  first surface is often not the common one, so physmat still sends 2,910 B, the same as before the cut and never
  more. Mask 1 B on 87.6 percent of rows (L331 65). E 15.42 B per row (L331 17.03). Cue normal error max 0.88 deg.
  Bad rows 0; digest 0 in all 7 rounds. Client misses 2 asset, 3 key, 3 target (L331 1, 16, 5). Step j is done.
  Not pursued: a physmat default chosen as the most common value instead of the first would save at most about 2 KB.

## Step k, measure only (senior, b333 `67ec5bae127b05c7`, on top of b332)

- The world field is now the largest after loc: L328 9,570 rows / 69,053 B (6.65 B per row), almost all attached
  sounds (step g dropped it for attached Niagara and decals). It is a delta from the row's location, which for an
  attached sound is a small relative offset, so it costs nearly an absolute position.
- On the client, an attached sound reads the world point for its attenuation pick (`c162_remote_atten`), two log
  rows, and the key-miss fallback (play at the world point). On a key hit the client already holds the parent
  component, whose own world location could stand in for the host's point.
- b333 adds a client map-end row `card518: client attached sound world gap (...)`: attached sounds with a parent,
  bucketed by the distance from the host's world point to the client parent's ComponentToWorld translation (under 10,
  10-50, 50-200, 200-1000, over 1000 uu, and the max).
- Prediction: over 95 percent under 200 uu (lag on a moving trooper is under about 100 uu), and the over-1000 bucket
  near 0. If so, the cut: the client takes the point from the parent on a hit, the host stops sending world for
  attached sounds (about 60 KB a run), and a key miss plays nothing (L328 had 8). Rows far over 1000 uu would mean the
  parent's translation is not the point the host sends (for example a socket offset), and the cut is dropped.
- Result c263L333 (b333): pass. 8,162 attached sounds with a parent: under 10 uu 6,586, 10-50 1,283, 50-200 283,
  200-1000 10, over 1000 0; max 458 uu (99.9 percent under 200 uu). Same run: the step j fields cost 4,899 B (physmat
  defaulted 2,242 here against 830 in L332, so L332's low physmat was the run's asset mix). World field 8,362 rows /
  60,380 B. Bad rows 0; digest 0.

## Step k, cut (senior, b334 `a4d4d521efcbba22`, on top of b333)

- `world_unsent` now includes SoundAttached: the host no longer sends the world point for attached sounds.
- Client `client_play_sound`: the key actor and, for an attached sound, the parent component are found first. The
  point for attenuation (`c162_remote_atten`) and the playlog rows is the parent's ComponentToWorld translation. A key
  miss returns `not played (key miss, no world point)`; before, it played at the host's world point. The b333 gap
  measure is removed.
- Trade: a key-miss attached sound is now silent (L332 3, L333 7 key misses in all). The effect it belonged to still
  played on the client from its own copy of the actor only if that actor exists, which on a key miss it does not.
- Predictions (c263L334): world unsent rises by about 8,000 (L333 3,960); world field down to the Activate rows,
  about 52 KB less than L333's 60,380 B; client played count close to L333 less the key misses; client key-miss rows
  for attached sounds read `not played (key miss, no world point)`; no new client "spawn returned null"; bad rows 0;
  digest 0.
- Result c263L334 (b334): bytes pass, behavior fail. World field 0 rows / 0 B (L333 60,380 B); E 12.74 B per row;
  bad rows 0; digest 0; spawn returned null 0. But 16 attached-sound key misses (L333 3). Three were AI barks keyed by
  the world point (`key world guid 0`): a row with no guid and no placed key finds no actor by design and always took
  the miss path, playing at the world point. With the point gone they went silent. The other 13 were HopperBug flaps
  whose guid actor is missing on the client (card 530; a sync gap, not caused by this step).
- Fix in b335 `9f07f49d391041f6` (a first b335 `d1e9adf824f3a67c` lacked the outcome text and was not run):
  `world_unsent` takes the row's guid and placed key and keeps the world point on
  world-keyed rows. On a miss, the client plays a world-keyed attached sound, Niagara spawn or decal at the world point
  (`played at world (key miss)`); guid- and placed-keyed misses still play nothing. The `card518: client key miss`
  row (16 per map) now ends with that outcome.
- Predictions (c263L335): world field a few dozen rows (world-keyed attached rows only), under 1 KB; world-keyed key
  misses read `played at world (key miss)`; 0 `not played` rows with `key world`; bad rows 0; digest 0.
- Result c263L335 (b335 `9f07f49d391041f6`): pass, step k done. 14 key misses (all logged): 5 world-keyed, all
  `played at world (key miss)`; 9 guid-keyed (8 HopperBug flaps, 1 JumperBug death), all `not played`; 0 placed.
  World field 5 rows / 37 B (fewer than predicted; the rest of the attached rows are keyed). Bad rows 0; digest 7
  rounds, 0 mismatches. E 24,321 rows / 310,706 B (12.78 B per row); blob 372,570 B in 3,603 frames (longer run than
  L334). Loc is now the largest field: 19,133 rows / 130,670 B body bytes (6.8 B per row), then comp 36,532 B and
  guid 36,339 B.
- Card 530 trace (2026-11-20): the 13 HopperBug misses are net relevancy, not a sync gap. Guid 316's flaps came from a
  HopperBug about 21,000 uu from the fight, past its 15000 uu net cull, so the client had no channel for it. The host
  still sends those rows; dropping attached rows for owners beyond the cull is a small optional cut (card 530).

## Step l: loc as a back reference (measure, b336 `b33e3862a8ab80f0`)

- Loc is the largest field left (about 6.7 B per row as a delta from the frame's previous located row). Idea: base
  each loc on the nearest of the frame's last 8 locations instead of only the previous one. The index (0 to 7) fits the
  point byte's 3 spare bits (C136Point::Count is 20), so it costs nothing on the wire.
- b336 adds a host-only measure, no wire change: `card518: host fx loc ring measure (...)` gives the bytes the locs
  would take under that scheme and how often each back index wins. It sits beside the existing `host fx loc on the
  wire` line, so one run gives both.
- Prediction: worth cutting only if the ring saves at least 1.5 B per loc row (about 30 KB a run). Index 0 should win
  most rows; a large share for indexes 1 to 7 means effects from a few places interleave in a frame (impacts beside
  muzzle flashes), which is what the ring would capture.
- Run it on the next c263 run after L335 (bytes only; behavior is unchanged from b335).

## Step m: comp and guid repeats (measure, b337 `a99cf4210938ca18`, includes step l)

- Comp (36,532 B in L335, 2 B per row: name rows past 127) and guid (36,339 B, about 1.75 B per row) are next.
- Comp idea: a per-asset default like step j (most assets attach to one component name). Caveat for a cut: the A row
  would then name a name row, so the name row must reach the peer before the A row.
- Guid idea: a mask bit meaning "same guid as the frame's previous guid row" (kBitOwnerClass is free since step f).
- b337 adds `card518: host fx repeat measure (...)`: E rows whose comp equals the asset's first comp, and E rows whose
  guid equals the previous guid row's (counted over all peers' rows in send order, so an estimate of the per-frame
  rule), each with the bytes they took. No wire change.
- Prediction: a cut is worth it for a field when its "same" bytes are 10 KB or more.
- Result c263L336 (b337): loc on the wire 12,387 rows / 70,253 B (5.7 B per row); the ring would take 62,311 B
  (5.0 B per row, back index 0 on 85.5% of rows). Saving 0.64 B per row, under the 1.5 B bar: **step l dropped**.
  Comp same as the asset's first: 11,566 rows / 23,132 B, over the bar: **cut**. Guid same as the previous guid row:
  4,677 rows / 7,705 B, under the bar: dropped. Bad rows 0; digest 7 rounds, 0 mismatches; 18 key misses, every
  world-keyed one played at world.

## Step m cut: comp as a per-asset default (b338 `310ae34fd8742ed0`)

- The step l and m measures are removed.
- `AssetDefaults` gains `comp` (a name row, -1 none), frozen with the others from the asset's first E row. The A row's
  set byte gains bit 4 (16): the comp default as a varint name row. frame_assets sends the A row through
  frame_add_named, so that name row goes ahead of the A row in the same frame (card 395 rule). Join replay resends A
  rows and names together, as before.
- An E row sends comp only when it differs from the default, as name row + 1 (0 none, so a row with no comp can sit
  over a default that has one). The client parse takes the default when the bit is clear; the A row parse accepts a
  set byte up to 31 and a comp under kNames.
- `host fx asset defaults` gains `comp N`: rows that left comp to the default.
- Predictions (c263L337): comp field bytes fall by about 20 KB on a run like L336 (most comp rows default); comp
  defaulted count near the L336 "same" count; A tag bytes up by under 1 KB; bad rows 0; digest 0; key misses with the
  key actor here 0; no new client target misses (a wrong comp would show as attach or target misses).

## Step m result (c263L337, b338) and the row size fix (b339 `53f764bcee5adf9d`)

- Comp field 36,532 B down to 11,346 B; 14,748 rows left comp to the default. Client bad rows 0, digest 0.
- New host WARN, 6 rows: `event row and its name rows do not fit one frame, row not sent` (totals 161 x4, 5 x2), each
  at a Morita bark (`SpawnSoundAttached MIBARK_..._Cue`). Asset misses rose from 2 to 6, key misses from 15 to 28.
- Cause: `kRowBytes` was 160 while an A or N row's string may be up to kStrMax (255). A bark cue's A row carries a long
  path; the comp varint step m added pushed it over 160, so put_raw marked it bad (161 = 160 + 1). Two E rows were lost
  because their comp name row overflowed the same way (total 5 = E row + 1, before the name row's bytes). frame_assets
  advanced table_sent past the lost A rows, so the client never had those assets: the asset misses.
- Fix: kRowBytes is now the longest A row (1 + 3 + 1 + 1 + kStrMax + 1 + 1 + 4 x 3 = 275), with a static_assert tied
  to kStrMax. Wire bytes are unchanged; host static memory grows by about 1.2 MB (queue and journal rows).
- Predictions (c263L338): 0 frame-fit WARNs; asset misses back to about 2; key misses near L336 (15); comp bytes near
  L337.
- c263L338 (b339) result: 0 frame-fit WARNs, 0 asset misses, comp 6,684 of 11,021 B in the field, bad rows 0, digest 0.
  Key misses 21 (11 of them HopperBug flaps, card 530). The fix holds.

## Closed (senior 2026-11-21)

Steps a to m are in and hold: bad rows 0, digest 0. On C3M2 the Fx blob went from 1,762,637 B (c263L317) to about
400 to 470 KB per fight, and E rows from 24.8 B to 15.4 B each. No cut is left that is worth more than a few KB
(physmat as the most common value is about 2 KB; the Morita ground hit kept local is about 20 KB, and the other
Morita assets cannot be kept local). Any new byte lever goes on a new card, under the smaller-packets rule.
