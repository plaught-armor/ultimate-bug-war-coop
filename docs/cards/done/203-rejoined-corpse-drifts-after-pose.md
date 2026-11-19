# Card 203: A corpse re-created on the client drifts after it was posed to the host's snap

Status: queued 2026-11-12, split from card 164 (its trace, item d). Budget: three runs. Static first.

Read: `docs/cards/done/164-gate-server-only-actors.md` ("Trace", item d), card 191 (frozen finals on a re-created
copy; same copy, may share the cause), `mod/src/ubw/pose_c132.cpp`.

## Why

c186-r1, client digest round 2: Pose mismatch on guid 0xa8 (frozen row, 269 uu, 5 of 22 bodies). The host did not
move the corpse (self-check "0 moved over 100 uu", no event on the guid between snap 2 at 98730823 and snap 3 at
98764361). On the client the corpse re-arrived at 98758448, was posed to snap 2 at 98759567 (0 uu after), and by the
compare at 98764028 had drifted 269 uu with "simulating 0". The player sees the corpse somewhere the host does not.

## Do

1. Trace, no run: what moves a posed, non-simulating copy on the client (a late ragdoll start, the arrival path
   re-posing it, the mesh following its capsule, an anim tick). Mark each line verified or open. State whether card
   191's cause is the same; if it is, fold this card into 191 and say so.
2. Fix at the layer that owns it, for every re-created frozen copy.
3. One run: the c186-r1 shape (a frozen corpse, client rejoin or re-arrival, digest round after it).

## Done when

- The trace names the mover; the run shows the re-created copy within the pose tolerance at the next digest round
  and no Pose mismatch. The Report ends with "Report complete."

Seen again, card 243 run 10 (FED_C3M2, 2026-11-13), guid 0x2ca: client arrival posed to snap 1 at 225574008
(0.0 uu), card 146 block at 225576224; digest rounds 7 to 13 (225587581 to 225767600) read 9 of 22 bodies over
100 uu (worst 298), `simulating 0`, same values every round. The host sent nothing for the guid until its freeze
snap 2 at 225795426; round 14 is clean. So the client copy stopped simulating by itself within 13 s of the pose
while the host's corpse was still live for 221 s. Same arrival path as card 269 fault 2; check after 269 is in.

Static, same guid: two candidates, not separated by run 10 rows.
- Host moved after snap 1 with body 0 inside 20 uu: `pose_host_row` (`pose_c132.cpp:703`) re-arms a sent row on
  body 0 only (`kPoseMoved`), so limbs that settle elsewhere send nothing (the hb36 guid 0xf0 class). The host has
  no stream row for 0x2ca in that window.
- Client moved: its mesh component went from (-43483, 48534, 555) at the ragdoll start to (-43488, 48540, 442) in
  round 7, and the copy reads `simulating 0` although the arrival pose started its ragdoll.
- Deciding row (diagnostic, next FED_C3M2 build): the round pose WARN adds body 0 on both sides and the host's
  largest body distance from its last sent snap. Host distance over 20 uu with body 0 under 20: fix the re-arm to
  the largest body distance. Host distance 0: the client copy moved; trace what stopped its simulation.

## Closed (2026-11-16)

Both candidate movers are gone: the host now streams samples while a corpse is live (card 152), so limbs settling
after snap 1 reach the client, and the arrival path was fixed by card 269. Run proof: `c423b163a` (two rejoins),
rounds 1 to 10: up to 24 corpses checked per round, 0 over 100 uu, 0 gave up, 0 frozen rows simulating, 0 Pose
mismatches. The re-arrival case more than 15 s after the final row is card 466 (found in `c460b170a`).
