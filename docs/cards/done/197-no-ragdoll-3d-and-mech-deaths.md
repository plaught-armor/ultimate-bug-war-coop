# Card 197: Every NPC class with no ragdoll start shows the host's corpse state

Status: queued 2026-11-12, from card 177 ("Problems for the senior"). Budget: three runs. Static first.

## Why

Card 177 covers sprite NPCs (IsNPC2D). Left open: ChariotBug, ArcherBug and JumperBug are 3D classes with no "host
ragdoll start" line in any log, so their corpses may take neither the pose path nor the sprite path; and
`BP_AI_M7Mech_C` reads IsNPC2D 1, so it takes the sprite path with a death set that was never checked against
`sprite_death_name` ("Death", "OverKill").

## Do

1. From the asset list, one row per NPC class on every map: ragdoll (pose path), sprite (card 177), or neither; for
   "neither", what the host shows after death (montage end pose, destroy, gib) and what a joining client shows.
2. M7Mech: name its death sequences; extend the name rule only if a real name is refused.
3. Fix the "neither" classes in the manager that owns corpses (pose_c132), one mechanism.

## Done when

- The table is in the card, and one run shows a joining client with the host's end state for one class of each
  kind fixed. The Report ends with "Report complete."

## Step 1 answers (blueprint bytecode, disassembly, logs hb64 and c243-c3m2-a1; senior, 2026-11-13)

| Class | What the host shows at death | Client today | Gap |
|---|---|---|---|
| WarriorBug | Ragdoll (64 `host ragdoll start` in run A) | pose row | none |
| ArcherBug | Explosion, acid zone, `InstaKillTarget(self, true)`: gib, actor destroyed 119 ms after HandleDying | gib (cards 039/042), replicated destroy | none |
| JumperBug | `EnableRagdoll` is overridden to do nothing. Seen once: gib, destroyed 131 ms later | gib, destroy | a death that is not a gib leaves a corpse with no ragdoll: same class as Chariot |
| M7Mech | Sprite NPC. Animation set `BP_CharacterAnimations_M7Razorback`: Attack01, Corpse, Death, Flinch, Idle, Melee, Pickup, Walk | sprite path (card 177); `sprite_death_name` accepts `Death` | none; no M7Mech death in any log yet |
| ChariotBug | Visible mesh, no ragdoll: 20 HandleDying, 0 ragdoll starts in run A | corpse held hidden 9 s, then WARN | real bug, below |

- The cause is not the class. `ASTNonPlayableCharacterBase::IsAllowedToRagdoll` 0x5196290 returns the bool at +0x34
  of the NPC's current significance level (`FSTNPCSignificanceLevel::bIsAllowedToRagdoll`). The base graph
  (`BP_AI_AttackSystemEnemy_Base`, offset 3792) starts the ragdoll only when `CanRagdoll` and that bool hold;
  otherwise it sets `RagdollRequested` and retries on each significance change (offset 4089). Significance comes
  from the host's own view, so any 3D bug that dies far from the host player is a corpse without a ragdoll until
  the host comes near. The Chariots in run A died there; Warriors do the same at that range.
- Run A proof: client `card131: client corpse first seen dead BP_AI_ChariotBug_C` 23 rows; 17 of the positions
  equal host `HandleDying` positions 18 to 82 s earlier (for example (-43885, 49857, 810), host 230421975, client
  230440412). The arrivals are true host corpses. Then 11 `card137: client corpse arrival ... held` rows and 22
  `WARN card137: ... not posed after ~9000 ms (no host pose row yet)` rows over 19 guids. Play effect: these
  corpses are invisible on the client for 9 s, then appear in the client's own pose.
- Same root as card 207: a pose row is created only at a ragdoll start (`enemies.cpp:2695`).

## Fix spec (one mechanism with card 207, build 4)

- `pose_c132.cpp`: one function `c207_track_shown_corpse(npc)`: host only; when the NPC is dead, its mesh is
  visible and not simulating and `pose_find_actor` has no row, call `c132_pose_track(mesh)` and log the card 207
  row (`card207: host tracked a corpse shown without a ragdoll: <class> guid 0x..., bodies N, update type U`).
- Two callers, nothing else: card 207's mesh-shown edge (`detour_c132_set_hidden`, hidden 1 to 0), and a death
  edge: the host's HandleDying site adds the actor (weak reference) to a small fixed list, and the pose host step
  calls the function for each entry 1000 ms after its death, then drops it. A gibbed and destroyed actor (Archer,
  Jumper) is gone by then and is skipped. A ragdoll that started in time already has its row.
- No class names. A later ragdoll start (significance rises) reuses the row; `c132_pose_track` already sets it
  back to Moving and sends again.
- Files: `pose_c132.cpp`, `enemies.cpp` (the two call sites), `hooks_internal.h` (one declaration).

## Predictions (FED_C3M2 rerun after build 4, no extra run)

- Host: one `card207: host tracked a corpse shown without a ragdoll: BP_AI_ChariotBug_C` row per Chariot
  HandleDying that has no `host ragdoll start` within 1000 ms (run A: 20).
- Client: 0 `WARN card137: client corpse arrival BP_AI_ChariotBug_C ... not posed` rows; each `held` row is
  followed by the pose apply of the same guid within one pose step after the row arrives.
- A Chariot the client watches die: the card 152 hold gets the row's sample 0, so 0 `WARN card152 ... its own
  ragdoll started` rows for Chariots. If such rows appear, the client's own significance starts a ragdoll the
  host does not have: new card.
- Open, only a run can settle: a Jumper death that is not a gib, and an M7Mech death (C2M1). Deciding rows:
  `card207 ... BP_AI_JumperBug_C`, and `card177` sprite death row naming `Death` for `BP_AI_M7Mech_C`.

## Senior ruling, build 4 review (2026-11-13)

See card 207 "one owner per corpse". Before coding: re-read this card's evidence against a build 4b log (card 269
rest rows); only classes with no rest row and no pose row stay in scope. Build 5, coded with 207.

## Re-read against a build after card 269 (senior, 2026-11-16, log `c243-c3m2-b6r2`)

- Chariot: covered by the card 269 rest rows. Client: 12 `card131: client corpse first seen dead
  BP_AI_ChariotBug_C`, 12 `card269: client rest arrival` (0 to 40 ms after the copy arrived), 0 `WARN card137 ...
  not posed` for the class. The `c207_track_shown_corpse` spec above is not needed for it and stays uncoded.
- Left in scope: a Jumper death that is not a gib and an M7Mech death; no log has either yet. Not worth a run of
  their own: read from the next C2M1 / C3M2 soak log (`card269` and `card177` rows for those classes).
- Open in card 269, not here: every rest arrival has a `WARN card269: client rest read-back ... IsPlayingAnimation
  1` one second later (13 in that log).

## Closed (senior, 2026-11-16)

- Chariot: covered by card 269 (re-read above).
- Jumper: a death that is not a gib does not exist. Every client death edge of the class reads `Health N
  OverkillThreshold 0.0 ... overkilled 1`: with a threshold of 0 every death is an overkill, so every death is a gib.
  Logs `c447b159b` to `c447b164a` (FED_C2M1): each `host HandleDying BP_AI_JumperBug_C` has its client `card038:
  client death edge ... card 039 gib` and `already gibbed by the host's Gib signal`, 0 WARN rows for the class.
- M7Mech: no death in any log (300 newest host logs, 0 `HandleDying BP_AI_M7Mech_C`). The sprite death path it
  would take is the card 177 path, proven for the other sprite classes; `sprite_death_name` accepts its `Death`
  animation. Left as a log watch, not a run: the first log with an M7Mech death is read for its `card177` row.
- The `c207_track_shown_corpse` spec stays uncoded; no class needs it.

Reopens on: a `WARN card137 ... not posed` row for any class, a Jumper death edge with `overkilled 0` on the
host, or an M7Mech death whose client copy has no `card177` sprite death row.

Report complete.

