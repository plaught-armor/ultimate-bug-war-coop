# Card 215: A client corpse never simulates; it only shows the host's bodies

Status: queued 2026-11-12, from the senior design review (user request: repeat-offender sync mechanisms). Static
design trace first, no code and no run until the senior accepts the trace. Budget after that: three runs.

Holds behind it (not worked one by one until this trace is judged): cards 191, 194, 197, 203, 207. Card 189 is a
host-side check and stays its own card.

Read: the header of `mod/src/ubw/pose_c132.cpp` (lines 1 to 32), `pose_try_apply`, `pose_hold_bits`,
`pose_freeze_step`, `pose_stream_start`; cards 140 (runs 13, 14), 146, 152, 186 in `docs/cards/done/`; the five
held cards.

## Why

The pose journal's first line states the cause of the whole class: "A ragdoll simulates on each side on its own".
The client copy runs its own ragdoll and the game's own death code, and the mod corrects it: it starts the copy's
simulation to pose it, turns gravity off and on, blocks the game's ragdoll start (card 146) and freeze (card 152),
waits one tick so physics writes bodies into bones, then freezes and holds with `bBlendPhysics` and `SkipAllBones`.
Each transition between "simulating" and "held" has produced a card:

| Card | Transition that failed |
|---|---|
| 140 r13, r14 | freeze in the tick of the apply; bones moved the kinematic bodies back |
| 146 | the game's own ragdoll start on a posed copy |
| 152, 186 | gravity and sleep order around the streamed final |
| 191 (open) | bones off the bodies when the game's start was blocked shortly before a frozen final |
| 194 (open) | rejoin copy 785 uu off, row pending |
| 203 (open) | re-created copy drifts 269 uu after the pose |
| 207 (open) | copy runs its own ragdoll 2 s after an in-place load |
| 197 (open) | classes with no ragdoll start have no state at all |

The one state that has always read exact is the held one (0.0 to 0.1 uu on clean frozen finals).

## Design to trace

From its death edge to its removal, a client corpse is in one state: bodies kinematic, `bBlendPhysics` on,
`SkipAllBones` on, the game's ragdoll start and freeze both refused for it, anim instance not driving the mesh.
Every host row (stream sample, snap, join replay, in-place load) is the same operation: teleport the bodies. No
gravity switch, no simulation start or stop, no one-tick waits, no difference between frozen, settled and
unsettled rows on the client.

## Do (static, each line marked verified or open)

1. Can bodies be teleported and blended into bones on a mesh that never simulated? Name what the ragdoll start
   sets that the held state needs (physics state created, collision profile, `PhysicsBlendWeight`, the end-physics
   tick registration at +0xD70, `ShouldBlendPhysicsBones` slot 0xC80) and how to set exactly that without a start.
2. Every game path that starts, stops or re-wakes a corpse's simulation on the client (the callers cards 146 and
   152 block, significance changes, hits on a corpse, gib break-off of a limb, pooled reuse). One refusal point
   for all of them, keyed on "this actor is a corpse copy".
3. What the client loses with no local simulation: corpse reaction to local hits and explosions between host
   samples, gib limbs (card 042 relay), anything else. State how the host's stream covers each.
4. Classes with no ragdoll (card 197: Chariot, Archer, Jumper, M7Mech) and sprite NPCs (card 177): same state
   model or their own, and why.
5. For each held card (191, 194, 197, 203, 207): closed by this design, or still needed, with the reason.
6. Size: which functions of `pose_c132.cpp` go away, which stay.

## Done when

- The trace answers 1 to 6 and gives a verdict: redesign (with a step plan and predictions) or keep the present
  design (with the reason the held cards are then independent). It ends with "Report complete."

## Senior prep (2026-11-12, static, line numbers of `pose_c132.cpp` at DLL `dc5c09ae0ebc3835`)

Where the present design switches state, so the trace can go transition by transition:

- Simulation start by the mod: `pose_start_ragdoll` (1006, snap path) and `pose_stream_start` (1473), both through
  `detour_c132_set_simulate(mesh, true)`. Question 1 is answered by reading what that original sets.
- Gravity: `pose_grav_off` (464), `pose_grav_on` (470); callers in `pose_try_apply` (1037), `pose_stream_start`,
  `pose_freeze_check` (1222).
- Held state: `pose_blend_byte` (1150), `pose_hold_bits` (1168), `pose_hold` (1177), `pose_release` (1187); the
  one-tick waits are `pose_freeze_step` (1203), `pose_freeze_check`, `pose_bone_again` (1249), run from
  `pose_freeze_tick` (1267).
- Blocks on the game's own calls: `pose_wait_gate` (404), `c132_pose_game_ragdoll` (435), the wait table
  (`wait_find` 352 to `wait_add` 381, `pose_wait_tick` 1542).
- Blend preconditions already read by the mod: `pose_end_tick_registered` (1306), `pose_should_blend` (1311).

The trace must state for each of these: kept, reduced to one call at the death edge, or removed. A verdict of
"redesign" needs the single entry point named (the death edge on the client: which detour, which side sees it first
when the row arrives before the death and when it arrives after).

## Line numbers refreshed (2026-11-13, `pose_c132.cpp` at build 4, DLL `5e2ce00acad8efa3`)

`detour_c132_set_simulate` 54, `wait_find` 386, `wait_add` 415, `pose_wait_gate` 438, `c132_pose_game_ragdoll`
469, `pose_grav_off` 498, `pose_grav_on` 504, `pose_start_ragdoll` 1119, `pose_try_apply` 1150, `pose_blend_byte`
1277, `pose_hold_bits` 1295, `pose_freeze_step` 1380, `pose_freeze_check` 1403, `pose_bone_again` 1436,
`pose_freeze_tick` 1454, `pose_end_tick_registered` 1493, `pose_should_blend` 1498, `pose_stream_start` 1690,
`pose_wait_tick` 1760. The card 203 diagnostic rows (`card203`, 3315 to 3349) are in build 4; read their result
in the FED_C3M2 run `c243-c3m2-b4a` before item 5, since it decides whether card 203 is the simulate-and-drift
case this design removes.

## Closed (2026-11-17 13:02): verdict "keep the present design"; the redesign is not needed

The card was a proposal to replace the simulate-then-hold corpse model because each transition kept producing a
card. Since it was written the transitions were fixed one by one and measured:

- The held cards are closed: 191 (freeze waits for bone 0 on body 0), 194, 197, 203 (host streaming plus the card
  269 arrival fix). 189 closed. Also closed in this class: 251 (frozen corpse moves) and 269 (arrival pose).
- Measure: 11 FED_C2M1 runs `c423b178a` to `c423b182a`, 198 digest rounds with the `Pose:` class, 0 mismatches,
  0 pose WARN rows over the noise list, 634 corpse arrivals.

So the reason for the redesign (a repeat-offender mechanism) no longer holds, and items 1 to 6 were not traced: a
rewrite of a mechanism that now reads clean would add risk before the playtest for no measured gain. What is left
in the class stays on its own cards: 207 (own ragdoll after an in-place load), 359 (bone residue at a frozen
final), 291 (client death timing; its step 2 makes the client play the host's death sequence).

Reopen: two new cards for a failed simulate/held transition on the client after this date, or a `Pose:` digest
mismatch that 207, 359 and 291 do not explain.
