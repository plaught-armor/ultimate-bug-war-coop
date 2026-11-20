# 323 Join replay gaps and digest blind spots

Sync audit second pass, 2026-11-14 (`docs/SYNC_AUDIT.md` L3, L4). Do after card 320: if the world starts only when
both players have confirmed the title screen, the per-map late join goes away and only a mid-map rejoin is left.

## Settled

`c50_join_replay` (`weapons.cpp:1008`) has no entry for: a player already downed or dead (`Down`), mech and turret
seats (`MechEnter`, `TurretMount`), `Round`, the card 133 hold state, live gib parts, grenades in flight. The
digest compares no player state, no seats and no sublevel visibility.

## Status 2026-11-15 (senior, static, from `sync_registry.cpp` `kSyncMechs`)

The join replay now runs per mechanism from the registry, not from `c50_join_replay`. Closed since this card was
written: `Down` (`c43_join_replay`, card 327), mech seats (`c75_join_replay`, a log row only), gib parts
(`c137_gib_join_replay`), grenades in flight (`c74_join_replay`), the card 133 holds (`c133_join_replay`).
Still open: turret seats (`TurretMount`), `Round`, and the three digest kinds (`Pl:`, `Seat:`, `Lv:`; none in the
source). The "Settled" list above is kept as written.

## Status 2026-11-16 (senior, static)

`Round` is struck: it goes client to host only (`round_c181.cpp:164`), the client's side starts empty after a
join, and the host's row for the old server copy is cleared when that pawn is gone (`fire.cpp:824`, "pawn gone").
Nothing to replay. Mech seat: the card 447 run (build after b146) rejoins the client while the host sits in a mech
and takes a client screenshot; that fills the step 1 table row for seats (engine replication carries it, or a seat
op is needed). Turret seat and the three digest kinds stay open.

## Step 1 (senior, static)

For each gap: does engine replication carry it (seat attachment, health), and what does the client show if it
joins in that state. Table in this card.

## Fix

Each gap with a visible effect gets a replay entry in `c50_join_replay` from the class's own state table (no new
journal). The digest gets three entry kinds: `Pl:<slot>` (health, armour, downed, equipped slot, seat), `Seat:
<actor>` (occupant), `Lv:<level>` (visible). Log only, as now.

## Done when

A client that joins while the host is downed, in a mech and on a turret shows each state within 2 s, and a digest
round right after has 0 mismatches of the new kinds.

## Gap seen in b187 and b188: host score gains after the join replay never reach the client (2026-11-17)
Run c423b188a: after the card 440 rejoin, the host's card 475 join replay sent total 160000 and the client applied it (client "score 140000 -> 160000" at 584941978). The host's score then changed with no row to the client, and digest rounds 14 and 15 at the map 1 end show `mismatch Score Score: ext host 74e8 client 7100`. b187 showed the same entry at round 9. Card 475 sends the score only as the last join replay item, never when it changes mid-map. Next step: find which host events add FedScore after the replay (no host score row is printed between 584941501 and 585031136), then either send the host total on every change or replay the gain event on the client.

## Build b189 `d4181cdc840b944a` (2026-11-17)
The host now sends its FedScore total on every change. c423b189a: 13 host change rows, each sent, and 13 client rows all "equal", including 160000 -> 170000 after the rejoin. Digest Score 0 in every round of c423, c245 and c448. The score part of this card is fixed.

## Build b196 `d42815991b07ad70`: digest kinds (log only, 2026-11-17)
digest_c132.cpp adds these entries:
- `Pl:<guid>:hp`, `Pl:<guid>:ar` and `Pl:<guid>:wp`, per player soldier (MI pawn class). They hold Health and Armour in whole points, and the low 16 bits of the FNV of the equipped weapon class name. Each value is in the ext and compared exactly. They are keyed by NetGUID, which is the same on both sides. A pawn with no GUID is skipped.
- `Lv:<level>`, per level in the world's Levels. A level that is visible on one side only is a presence mismatch. Actor entries of a level missing on the other side are still skipped.

Downed stays `P:<slot>` (card 327). `Seat:<actor>` is not added: a remote player's seat cannot be read on the client, because controllers do not replicate there and no replicated occupant field is known. The seat row stays with the card 447 rejoin run.

One risk: Health under fire can differ between the host's build and the client's build. A Pl hp mismatch during a fight is timing until it repeats after the fight.

## b196 run results (junior, 2026-11-17)

- c448b196a (C1M1): one standing mismatch rounds 2-7, `PlayerPawn Pl:8e:wp ext host c9c4 client 1903`:
  host BP_Weapon_MoritaMk1_C, client BP_Weapon_Unarmed_C. Both players' own reads say Morita, so 0x8e is
  most likely the host's pawn as a simulated proxy on the client: the proxy's equipped weapon reads Unarmed.
  New finding, not chased: check whether the visible weapon on the proxy is right (receiver-side shot) before
  calling it a sync gap or a reader gap.
- c245b196a (C4M2): 99 PlayerPawn presence mismatches, 9 per round = 3 host-only MI pawns x 3 entries,
  keys rotating over the map. Fix queued for the next build: count a pawn only while APawn::PlayerState
  (+0x2C8, now `kPawnPlayerState` in ue/actor.h, shared with card 452) is set. Unproven guess that the extra
  pawns have none; if the mismatches stay, the next suspect is pawns not relevant to the client.
- Level entries: 0 mismatches in both runs.

## Senior note (2026-11-17 19:52): Pl:wp is a reader gap

Junior, b202: the client shot shows the host pawn holding the MoritaMk1; the client copy's weapon inventory holds only Unarmed because GiveMorita runs host-own. The digest Pl:<slot>:wp reads the inventory's equipped weapon. Fix here: read the attached (visible) weapon instead. Not a sync gap.

## Note (2026-11-18 09:58)
c244b219a: 7 of 64 digest rounds show `card132: mismatch Score Score: ext host 7cdc client 7700; no row sent`. The same row appears in c245b199a, c245b212a, c245b213a, c423b187a and c423b188a, so the b189 "FedScore total sent on every change" fix leaves a path where the host score changes with no row. Trace which score change skips the send.

## Score settle closed in b222 `de3cee44d5041dcc` (2026-11-18 10:59)
The score part is done. b220 measured the client's own adds landing 9 to 35 ms after the host's T send (9 samples, max 35). b221 applies a T above the client's own total at once and holds the difference as raise_left; own adds in the window are absorbed from it. kC323SettleMs is 80. b222 holds a set-down one more 80 ms when a frame is in flight. c245b222a: 18 digest rounds, mismatches 0, Score WARN 0, 0 double broadcasts, 0 dips; b221 raises applied 17 to 19 ms after the host send. The remaining +15000 client over-count at BrainBug_SO2 is card 491. Still open on this card: the join-while-downed, mech and turret proof under "Done when", and the Pl:wp reader fix.

## Join replay order: events before state, b257 `6b5faf2a31aafe19` (2026-11-19)
Finding (c245b256a, C4M2 rejoin): the host sent the CollisionOp state rows at t=703129333 and the world journal replay at 703129338. The client's local load set BlockingVolume_14..17 actor collision 1 -> 0 at 703129105 (host value 0). Then recv 328 WorldOp `7e033b35 F` (closure of BP_PlayerPresence_C_73) ran at 703129428, and its Blueprint set the four volumes back to 1, logged "by host row caller exe+0x3802287". The digest rounds then showed BlockingVolume mismatches 4 until the map end. The second rejoin repeated it at 703144871, 703145173 and 703145215. The card137 change row: "BlockingVolume actor collision by local sim (load): 4 / by host row (load): 4". Cause: an event row whose script has side effects was replayed after the final-state rows it changes.

Class fix (sync_registry.cpp, world_c98.cpp): a new registry entry "world events" runs right after "objective" and replays only the world journal's event rows: S satchel, K kill, F closure, G secret and C activate, plus the B play rates, which card 261 sends before every other row. "world" now replays only the state rows. Each part has its own R bracket and its own host row, "card098: host t=T world journal replay to <pc> (first Pong of the map): N of M event|state rows". The row keeps its old prefix, so the older scenario waits still match.

Replay senders in table order, with each one's class (b257):
- objective (c49_join_replay): events (objective steps and scripts). Already first.
- world events (c98_replay_events): events S, K, F, G, C and rates B. New, second.
- door (c95_replay): state (door open and closed, CollisionOp rows). Paced per tick, so it lands after the same-tick events.
- world (c98_replay): state (L timeline, X/x ext, M rest, N copy enable, H hidden, A placed, E enable, T turret, P/D spawn and destroy).
- levelend (c260_join_replay): state flag.
- ext (c132_ext_replay): state, idempotent.
- pose (c132_pose_replay), decal (c132_decal_replay), dropship (c132_dropship_replay): state.
- effects (c136_join_replay): transient visuals; no script side effects on world state.
- pause (pause_join): state flag, read live.
- bugform (c208): state (the current bug form).
- intro (c135): state flag.
- script (c310): absolute per-pawn state rows (tags, chargeable, default class).
- player (c43 life state, c75 seat rows): state.
- gib: transient visuals.
- grenades (c74): in-flight objects, not final state.
- holds (c133): sends nothing.
- score (c475): state; must stay last.

No event replay needs a state row first. S, K and C act on a placed actor that the client's own load already has. F closes a placed presence. G credits a count. C still goes before L, because L is now in the later "world" state entry. The B rates go with the events, so a timeline that an event plays runs at the host's rate. V travel and Q/O are live only and are not journalled.

Proof run: c245b257a. Pass conditions:
- The host "event rows" line comes before the door and "state rows" lines.
- The rejoin digest rounds show 0 BlockingVolume mismatches.

## Status 2026-11-19 (senior, from existing logs)

- b257 replay order: proved. In c245b257a and c245b262a the host "event rows" line comes 3-4 ms before the "state
  rows" line (e.g. 729645943 then 729645947), and both client logs have 0 BlockingVolume mismatch rows.
- Pl:wp reader fix: done (c323_drawn_mesh, digest_c132.cpp:820, keys on the drawn weapon mesh).
- Culled player pawns (c323_pawn_culled) with three players: card 502 finding 1, b263.
- Still open, the only "Done when" left: a client that joins while the host is downed, in a mech and on a turret
  shows each state within 2 s, then a digest round with 0 mismatches of the new kinds.


## Plan for the last "Done when" item (senior, 2026-11-19, static and from c447b177c)

- Mech seat: proved, no op needed. In c447b177c the client rejoined while the host sat in BP_BaseMech_C (host
  566920489 "seat join replay ... 1 mech pilot row(s)"). The client's CanInteract on that mech was refused as piloted
  (client 566922897, "card452: ... refused: piloted (player state PlayerState)"). APawn::PlayerState replicates to
  everyone, so engine replication carries the seat. Digest rounds 2 to 4 after the rejoin show mismatches 0.
- Turret seat: same mechanism. MountTurret possesses the turret with the pawn's controller (vehicles.cpp:1986), and
  possession sets APawn::PlayerState on the turret. No op is expected to be needed. There is no client row yet: the
  card452 refusal reads mechs only.
- Downed: c43_join_replay (death.cpp:830) sends every g_c43 entry, Downed and Dead alike. Dead was proved in c375.
  Downed needs a third player: with two players, a downed host whose client is away is all down, so it becomes Dead.
  The card 502 three-player build (b263) makes this possible: client2 stays Up while the host is downed.
- The three digest kinds (`Pl:`, `Seat:`, `Lv:`): struck. Seats ride engine replication (above). Downed is covered by
  the card 043 rows on both sides. Sublevel visibility is out of scope for this card.

Proof needed (one run, FED_C4M1, CLIENT2=1):
1. Code (junior, small). A one-shot client row at the card135 "screen prompt ready" line, for each mech and turret
   pawn with APawn::PlayerState set: `card323: client seat seen: <class> occupied (player state <name>)`.
2. The host mounts BP_MountableTurret_C. The client disconnects and rejoins. Expect the card323 seat row naming
   BP_MountableTurret_C within 2 s of the prompt, and a client shot from 600 uu.
3. The host is downed (`host down host`) while client2 stays Up. The client disconnects and rejoins. Expect host
   "Down join replay ... 1 of 1", then the client row "card043: client ... other pawn ... -> Downed" within 2 s.
4. A digest round right after each rejoin, with mismatches 0.

Scenario: `tools/scenarios/c323-seat-down-c4m1.txt` (senior draft). The seat row wait is a try, so the run works on
b263 now, with the turret shown by shot only. The downed step in c209-three.txt never ran: c209b256a and c209b257a
ended at the bug-map travel before it, and the user does not care about bug mode, so that step moves here.

Done when: rows 2 and 3 are in the logs, the turret shot shows the host on the turret, and both digest rounds read 0.

## Closed 2026-11-19 (senior, c323b271b PASS)

The client logged `card323: client seat seen: BP_MountableTurret_C occupied` at 744128730, 1 ms after the screen prompt. The host logged "Down join replay ... 1 of 1 entries sent" at 744141216, and the client logged the host pawn Up -> Downed at 744141237. Client digest round 2 after the rejoin: mismatches 0.
