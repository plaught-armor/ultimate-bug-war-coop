# Card 177: A corpse with no ragdoll shows the host's end pose

Status: queued 2026-11-11, split from card 152 (one goal per card). Budget: three runs. Playtest 2026-11-08 finding 5.

Read: card 146 (c37 live-death fix, corpse arrival), card 152 (pose rows), `mod/src/ubw/pose_c132.cpp`
(`c132_pose_arrival`, `arrival_tick`).

## Why and scope (from card 146 r2, senior, 2026-11-08)

Playtest finding 5: the CTF laser troopers (BP_AI_Trooper_Laser_Blue_C / _Red_C) have no ragdoll, so the host sends
no pose row for them. The client's corpse arrival waits for a row that never comes and falls back to its own death
visual after 3 s. Card 146's c37 live-death fix does not cover it. Added scope: a corpse with no ragdoll gets its end
pose from the host (the death montage and its end time, or a final pose row once the death animation ends), so the
client shows the same pose. Added proof: a laser trooper killed on the CTF course shows its host end pose on the
client, with no "not posed" WARN.

## Do

1. Trace first, in this card: which classes have no ragdoll on every map (from the asset list: `IsAbleToRagdoll`
   false or no physics asset), what the host runs at their death (montage, its end time, any freeze), and what the
   client copy runs for a live death and for an arrival already dead. Mark each line verified or open.
2. The host's end pose reaches the client through the pose row mechanism (one manager): a final row once the death
   animation ends, or the montage and end time if the trace shows that is exact. No second relay.
3. One run on the CTF course: a laser trooper killed in view and one that arrives dead.

## Done when

- The trace, the class list covered and not covered, the run lines, a client screenshot of the corpse, no
  "not posed" WARN. The Report ends with "Report complete."

## Senior trace and scope (2026-11-11; verify each line, mark verified or open)

- Verified (card 146, playtest host log): laser trooper deaths run HandleDeathEvent and HandleDying on the host and
  never reach `detour_c132_set_simulate`, so no pose row exists. The client copy is a relevancy arrival
  ("corpse first seen dead"), waits `kArrivalWaitMs` in `arrival_tick` (pose_c132.cpp:902) and ends in the
  "not posed" WARN with `c38_corpse_death_play`.
- Open: why the host starts no ragdoll for these two classes (kis.py on BP_AI_Trooper_Laser_Blue_C / _Red_C and
  their parent: the OnIsDying path, a death montage, or a freeze), and the full class list with the same path.
- Open, the design point: a pose row holds physics body transforms (`kPoseBodies`). A mesh with no ragdoll may have
  no simulating bodies, so the row cannot be built the same way. Decide in the trace, before code, between:
  (1) a "no ragdoll" row: the host tells the client at HandleDying that no pose will come, with the death montage
  and its position at send time, and the arrival plays the montage to that position (or its last frame for a late
  arrival) with no wait and no WARN; (2) body transforms set kinematically from the host's bones once the montage
  ends. Take (1) unless the trace shows the montage end pose differs between sides (random pick, root motion).
- The row goes through the pose journal (one manager) so a rejoin replay gets the end state and replays no
  one-shot (STATE.md "Replays set state").
- Files expected: `pose_c132.cpp`, `enemies.cpp` (the HandleDying edge only), `hooks_internal.h`, one scenario, the
  card. The senior confirms the list after the trace.

## Trace (junior, 2026-11-11, step 1: static only, no code, build or run)

Addresses are in the shipping exe (image base 0x140000000), from `tools/pdb_rva.py lookup` and disassembly of the
exe. Package facts are from `tools/iostore/iox.py` extracts read with zen.py and `strings`. Log counts come from grep
over `logs/` (old builds included).

### 1. Why the host starts no ragdoll for the laser troopers

- Verified: the laser troopers are 2D sprite NPCs, not skeletal meshes. The BP_AI_Trooper_Laser_Blue package exports
  SpriteRenderComponent, AnimatorComponent, SpriteColliderComponent, SpritePFXManagerComponent, CorpseCollider,
  CharacterMesh0, GoreComponent, CharMoveComp and CollisionCylinder. It has no BugAnimPhys or other physics
  component. Its art is a flipbook set under `/Game/Art/Animations/NPCs/LaserTrooperBlue/` (and `.../Red/`), with 8
  view directions (B, BL, BR, F, FL, FR, L, R). Laser_Red is the same.
- Verified: `ASTNonPlayableCharacterBase::IsNPC2D` (0x145197F80) is true when the animator at actor+0xA30 IsA
  `UADSpriteAnimatorComponent`. Client logs show "IsNPC2D 1" for both laser classes (40 Laser_Blue and 54 Laser_Red
  lines). For example, logs/playtest-menu-20261108-191343.log:6622 reads "card039: client edge
  BP_AI_Trooper_Laser_Blue_C ... IsNPC2D 1: overkilled 0 (no gib)".
- Verified: the BP has no OnIsDying override. Its only BP functions are "Add Impulse on Hit", GetFSpeed and GetRSpeed.
- Verified: the only ragdoll start in the PDB is `USTBugAnimPhysComponent::StartRagdoll`. With no BugAnimPhys there is
  nothing to start, so `detour_c132_set_simulate` never runs. Across all logs, "card132: host ragdoll start" appears
  only for BP_AI_WarriorBug_C (4956) and BP_AI_AphidBug_C (20). It never appears for a trooper.
- Verified, not a factor: `IsAllowedToRagdoll` (0x145196290) reads byte +0x34 of the significance settings (actor+0x760
  → `GetCurrentSignificanceLevelSettings`). There is no physics body to ragdoll either way.
- Open: the parent class of the laser BP. The kis.py import hash has no resolved.txt entry. Its first import is
  `/Game/Blueprints/Enemies/BP_AI_AttackSystem_Human_Sprite`. The answer does not depend on it, because the death path
  is decided by the animator type (IsNPC2D), not by the BP.

### 2. Classes with the same no-ragdoll (sprite) death path

- Verified IsNPC2D 1 from logs (covered by a fix keyed on IsNPC2D): BP_AI_Trooper_Laser_Blue_C,
  BP_AI_Trooper_Laser_Red_C, BP_AI_Trooper_Morita_C, BP_AI_Trooper_NonCombat_C, BP_AI_Trooper_SquadLeader_C,
  BP_AI_Trooper_Medic_C, BP_AI_Trooper_RadioTrooper_C, BP_AI_M7Mech_C.
- Verified IsNPC2D 0 from logs (3D, not this path): BP_AI_WarriorBug_C, BP_AI_AphidBug_C, BP_AI_ChariotBug_C,
  BP_AI_ArcherBug_C, BP_AI_JumperBug_C.
- Open, sprite art exists but no IsNPC2D log line: FemaleMoritaTrooper, MK2Trooper, ShotgunTrooper (and the
  ArachnidHumans Morita/Shotgun BPs), NonCombatTrooperFemale, TurretTrooper (3 log lines with no IsNPC2D value),
  Epulse88Trooper, FlamerFieldScientist, AloftOKHuman, 2DTargetHuman, 2DTargetWarriorBug and M7Razorback. These are
  the `/Game/Art/Animations/NPCs/*` sets in docs/reference/asset-paths.txt. A fix keyed on IsNPC2D covers them as well
  without a per-class list.
- Verified, the size of the gap: across client logs, "not posed after" WARNs come from Morita (98), Laser_Blue (4),
  Laser_Red (4), WarriorBug (96), AphidBug (12) and ChariotBug (8). Morita, a sprite NPC, has the most. This card's fix
  covers it.

### 3. What the host runs at a laser trooper's death

- Verified: `ASTNonPlayableCharacterBase::TryPlayDeathAnimation` (0x1451B11E0) builds FName(L"Death") from the string
  at 0x148148370 and calls `TryPlayAnimationSequence(name, true)` (0x1451B0F30). The name is fixed: no random pick,
  no hit direction, no montage.
- Verified: `TryPlayAnimationSequence` uses the animator interface at actor+0xA38. vtbl+0x18 is a can-play check and
  vtbl+0x48 returns the current sequence name (with force false, the call is skipped when that name is already
  playing). It binds "ASTNonPlayableCharacterBase::ProcessFrameAction" and
  "ASTNonPlayableCharacterBase::AnimationEnded" (strings at 0x148148270 and 0x1481482E0), then calls vtbl+0x8 to play
  the sequence at rate 1.0 (the float at 0x14704EB4C).
- Verified: the laser animation data (BP_CharacterAnimations_LaserTrooperBlue) has the sequences Attack01, Corpse,
  Death, DeathIdle, Idle, Overkill, Run, Taunt, TauntResponse and Walk. Death is 12 frames per direction, DeathIdle
  is 4, and Corpse is 1 (asset-paths.txt).
- Verified: it is a flipbook, so there is no root motion. The actor does not move from the animation.
- Verified: the rendered frame direction is chosen from the viewer's angle. The host and the client show different
  directions of the same frame by design, so "the same pose" means the same sequence and frame index, not the same
  image.
- Verified: `AnimationEnded` (0x14517CC10) is `add rcx,0x960; mov dl,2; jmp`. It broadcasts the actor+0x960 delegate
  with argument 2. `IsDeathComplete` (0x145196FD0) is `byte [actor+0xAE8] >= 4`.
- Open: what holds the end frame after Death (the animator's non-looping hold, or a listener on +0x960 that switches to
  DeathIdle or Corpse). The loop flags are in the compressed animation data and were not decoded. The only direct
  caller of `UADSpriteAnimatorComponent::GetLastFrame` (RVA 0x04A23770, called at 0x145168DE2) is
  `ASTDamageableActorCorpseBase::PostInitProperties`, a separate pre-placed corpse actor, not the NPC death path.
- Open: the end time in ms. It is 12 frames at the sequence's frame rate × 1.0, and the frame rate was not decoded.
- Open, for Morita only: its animation data has Death, Death01, Death02, Corpse01 and Corpse02. Whether "Death" picks
  Death01 or Death02 at random is not traced. If it does, the two sides can end on different variants. The laser data
  has only Death and Corpse, so laser troopers have no variant.

### 4. What the client copy runs

- Verified, live death (copy alive, then dies): `c38_death_edge` → `c38_corpse_death_play` (enemies.cpp:1461). With
  no BugAnimPhys it calls `g_orig_try_play_death(actor)`, the same `TryPlayDeathAnimation` as the host, so it plays
  "Death" from frame 0. playtest-menu-20261108-191343.log:6624 reads "card038: client death edge ... Health 50.0 ->
  0.0 ...: TryPlayDeathAnimation; ... card 131 base state 2 -> 3, HandleDying called". Line 6623 reads "card132:
  client HandleDying ... base state 3, movement mode 1, mesh hidden 0". For laser troopers a live death already
  matches the host, apart from the trip delay and the per-viewer direction.
- Verified, arrival already dead: `c38_death_edge` (corpse, not gibbed) → `c132_pose_arrival(actor)`
  (pose_c132.cpp:1837) hides the copy. `arrival_tick` (pose_c132.cpp:1618; the senior trace's line 902 is stale) then
  waits for a pose row. A sprite NPC never gets one, so after `kArrivalWaitMs` (`kPoseForceMs` 8000 + 1000 = 9000 ms,
  pose_c132.cpp:188, counted from the later of the arrival and the first Pong; this replaces the "3 s" in the Why
  section) it shows the copy, calls `c38_corpse_death_play` (Death from frame 0) and logs "WARN card137: client corpse
  arrival %s guid 0x%llx not posed after %llu ms ...".
- Where the two sides end differently:
  - Verified: an arrival is hidden for 9 s and then plays the whole death animation on a corpse the host shows as long
    since finished. This is the bug, and the WARN.
  - Open: on Morita, a variant pick can differ between the sides (line 3).
  - Verified: the client shows the death at the moment of the client edge, one trip after the host. This is accepted.
- Verified, possibly relevant: the Laser_Blue client log line shows its mesh at (-9836, 18511, 15), far from the
  actor at (-10415, 8186, 102). CharacterMesh0 is unused on a sprite NPC. `arrival_show` acts on the mesh, so hiding
  and showing it may do nothing visible for a sprite NPC (open: what `arrival_show` hides on a sprite copy).

### 5. Design verdict: (1), a "no ragdoll" (sprite death) row

- Reason:
  - Design (2) cannot be built: a sprite NPC has no physics bodies or bones, so there are no `kPoseBodies`
    transforms.
  - The laser death has no random pick and no root motion (line 3).
  - The only state to carry is the sequence name and how far into it the host is. The rendered direction is per
    viewer by design.
- Shape:
  - Host: at the HandleDying edge of an NPC with IsNPC2D true, after its death sequence starts, read the playing
    sequence name (animator vtbl+0x48) and add a "sprite death" row to the pose journal (one manager, the same journal
    and R row removal, so a rejoin replay gets it): guid, sequence name, host ms since start. Reading the playing name
    rather than assuming "Death" also covers a Morita variant pick, if it exists.
  - Client arrival: `arrival_apply` finds the sprite row and plays that sequence at its end frame, with no wait and no
    WARN. Open: whether the animator's play call (vtbl+0x8, or `UADSpriteAnimatorComponent::StartAnimationSequence`
    in symbols.txt) takes a start frame or time. If not, the fallback is to play it and let it run to its end; that is
    still one sequence, not the 9 s hide.
  - Client live death: leave it as it is for laser troopers. For a variant-pick class, play the row's sequence name
    if the row has come in by the edge. That is open, pending the Morita check.
- Files: `mod/src/ubw/pose_c132.cpp` (row kind, journal, `arrival_apply` / `arrival_tick`),
  `mod/src/ubw/enemies.cpp` (host HandleDying edge only), `mod/src/ubw/hooks_internal.h` (declarations and the
  animator vtbl offsets), one CTF-course scenario, and this card. Possibly `symbols.txt`, if the end-frame play needs
  a new function address.
- Predictions for the one CTF-course run (N = laser troopers killed on the host):
  - Host: N lines "card177: host sprite death <class> guid 0x... seq Death", and 0 "card132: host ragdoll start" for
    either laser class.
  - Client: one "card177: client sprite death <class> guid 0x... seq Death, arrival, end frame" for the trooper that
    arrives dead, applied within one tick of its row (no hidden wait). One live death with "card038: client death
    edge ... TryPlayDeathAnimation" as today.
  - 0 "WARN card137: client corpse arrival ... not posed" for any IsNPC2D class.
  - 0 new WARN lines against the previous run's WARN diff.
  - A client screenshot of the arrived corpse on its last Death or Corpse frame.

### Problems for the senior

- ChariotBug (8), WarriorBug (96) and AphidBug (12) "not posed" WARNs in older client logs. These are 3D classes
  (IsNPC2D 0). ChariotBug, ArcherBug and JumperBug have no "host ragdoll start" line in any log, so they may have a
  separate no-ragdoll 3D path outside this card.
- BP_AI_M7Mech_C logs IsNPC2D 1. A sprite mech will take the card 177 path too; its death set was not checked.
- The senior trace cites `arrival_tick` at pose_c132.cpp:902. It is at line 1618 now. The Why section says the
  fallback comes "after 3 s"; `kArrivalWaitMs` is 9000 ms.
- `arrival_show` / the arrival hide act on CharacterMesh0, which a sprite NPC does not render (line 4). Whether the
  sprite copy is visible during the 9 s "hidden" wait is open.
- The host row is added at `detour_try_play_death` (enemies.cpp, after `g_orig_try_play_death`), not at the
  HandleDying edge. TryPlayDeathAnimation is the call that resolves and starts the death sequence. At HandleDying the
  animator has not picked it yet, so the played name (C) is readable only after this call returns. The one added line
  is in the same death path, one call later.
- No Morita trooper is on FED_C1M1. Moritas die on FED_C2M1 (around 224321 40652 8527), so c177-r1 has no Morita
  step. A Morita variant pick, if one exists, can differ between the host and a live client death. Live deaths are out
  of this card's scope (unchanged).

## Closure A to D (junior, 2026-11-11)

- A, the rest state: verified. It is the last frame (count - 1) of the resolved death sequence. The name is the
  animator's FName at +0xE0; the sequence pointer is at +0x120. The non-loop stop holds that frame.
- B, how to set it: verified. `g_orig_c41_try_play(npc, FName(host name), true)` (TryPlayAnimationSequence, hooked
  ungated) selects the host's sequence. Then elapsed +0x110 = 0, frame +0x118 = count - 1, and frame time +0x11C = that
  frame's TimeToDisplay. Then `FADAnimationFrameDelegate::Broadcast(anim + 0xF0, &frames[count - 1])` (RVA
  0x04A22F10, added to symbols.txt) runs, as `UADSpriteAnimatorComponent::TickComponent` does on a frame change. Death
  is not played through.
- C, the read point: verified. The read is `fname_into(anim + 0xE0)` right after `g_orig_try_play_death` returns. It
  is the name actually played, and a variant is included: a nonzero FName number is written as "_<n>".
- D, arrival for IsNPC2D: `c132_pose_arrival` sends a sprite NPC to the sprite path before the mesh check. There is
  no CharacterMesh0 hide, no 9000 ms wait and no `c38_corpse_death_play` from frame 0. With the row present, it is
  applied at once. With no row, the copy waits for it (bounded at 3000 ms after the later of its arrival and the
  Pong), then gets a WARN fallback: the local "Death" set to its rest state.

## Run c177-r1 predictions (written before the run)

Method change (junior, 2026-11-12). Runs c177-r1 and c177-r1b (FED_C1M1) were stopped at the objective chain before
any card 177 code ran. In r1, "objective complete CaptureTheFlag_SO0" found no incomplete objective with that id. In
r1b, ShootingRange_SO3, SO0 and O1 completed by command, but the level script never added CaptureTheFlag_O1, so the
wait at line 29 timed out. The level script adds the CaptureTheFlag objective only through its own tutorial
sequence; c65p4 completed every range increment and O1 and also had no add. Two identical failures, so the method
changed: the run moves to FED_C2M1, where the two squad Morita troopers (BP_AI_Trooper_Morita_C, IsNPC2D 1) die
through the same TryPlayDeathAnimation path. This also covers the Morita step. The laser classes stay unproven in
a run; they share the code path (the class is not read). Run c177-r1c is the third and last run of the budget.

Scenario `tools/scenarios/c177-r1.txt`, FED_C2M1, FAST=1, host `UBWCOOP_AUTODROPSHIP=1
UBWCOOP_TRAVEL=/Game/Maps/FED/FED_C2M1?listen UBWCOOP_GOD=1`. Kills are InstaKillTarget (`kill actor`). Part 1 kills
one Morita in the client's view; part 2 kills the other with the client at 222858 -90404 (about 134000 uu away),
then brings the client in.

1. Host, each Morita kill: one line "card177: host sprite death BP_AI_Trooper_Morita_C guid 0x<g> seq Death
   (<n> frames, <ms> ms), sent to 1 PCs". The seq is "Death", with n > 0 and ms > 0. There are 0 "card132: host
   ragdoll start" for BP_AI_Trooper_Morita_C.
2. Host, rest read-back for each row, ms + 1000 after the death: "card177: host sprite rest BP_AI_Trooper_Morita_C
   guid 0x<g> seq Death frame <n-1> of <n>". Risk: AnimationEnded may switch the sprite to a Corpse/DeathIdle
   sequence. Then this line names that sequence, and prediction 5 FAILs with a WARN read-back on the client. Either
   way that is a finding, not a fix in this run.
3. Client, part 1 (a live kill in view): "card177: client sprite row guid 0x<g> seq Death, host age <a> ms" with
   a < 1000. There is no "card177: client sprite death ... arrival" line for that guid before part 2; the live death
   is unchanged.
4. Client, part 2: one "card177: client sprite row" line while the client is far away. After the warp in, an
   arrival line "card177: client sprite death BP_AI_Trooper_Morita_C guid 0x<g> seq Death, arrival, rest state set
   (host age <a> ms, sequence <ms> ms; frame <n-1> of <n>, playing Death, <t> ms after it arrived)" for the part 2
   corpse, and a second for the part 1 corpse if its channel closed while the client was away. a is greater than ms
   and t < 100 (one or two ticks, no 9000 ms wait). Risk: if the squad troopers are always relevant, no arrival
   line comes; that is a finding.
5. Client read-back, 1000 ms after each rest state: "card177: client sprite read-back BP_AI_Trooper_Morita_C guid
   0x<g>: seq Death frame <n-1> of <n>, host seq Death frame <n-1> of <n>", with no WARN prefix. The values match the
   host's line 2 for the same guid.
6. Part 3 (the rejoin): host "card177: host sprite replay to a joining PC: 2 rows sent of 2, 0 without a NetGUID",
   plus 2 "card177: host sprite replay row" lines. Client: 2 "card177: client sprite row" lines after the reconnect,
   and arrival lines with "rest state set" for the corpses it sees.
7. Part 4 (the travel), both sides: "card177: <side> sprite death map totals: ... rejects: ..., table peak <p> of 256,
   wait peak <w> of 32", with no WARN prefix. The rejects are 0 except "second", the fallbacks are 0 and the
   mismatches are 0. The host shows p = 2 and the client w >= 1.
8. 0 "WARN card137: client corpse arrival ... not posed" for BP_AI_Trooper_Morita_C. 0 "WARN card177" lines.
   There are no new WARN classes per side against c186-r2.
9. Screenshots: client `arrived-corpse` and `rejoin-corpse` show the trooper on its last Death frame (lying), not
   standing and not hidden.

## Senior re-scope (2026-11-12, budget spent 3 of 3)

Runs r1 and r1b never reached card code (scenario objective chain on FED_C1M1); r1c on FED_C2M1 proved the host
side (rows, rest reads, replay count) and showed three design gaps and one scenario error. The goal is unchanged.
One more run (c177-r2) is granted, shared with card 192. Before it, in one build:
1. Revive: the host drops the row and sends its removal when the NPC's Health is above 0 again; a later death adds
   a new row. No "second death" rejects.
2. Sequence change after the row (OverKill): when the host's rest read finds another sequence than the row's, the
   row is updated and re-sent (same journal row, new name and frame count); the client applies the newest.
3. Frame and frame count are read from the same sequence (no "5 of 4").
4. The map totals line names its side from the mod's own side source (`side()` or what the other totals use), not
   from `g_client_session` and not from a row count.
5. Scenario: the client is warped to the corpse by point, within the corpse's lifetime (about 24 s on the host);
   the kill is out of the client's relevancy first. Predictions rewritten with exact lines before the run.
Laser troopers stay unproven by a run (they need the full tutorial); they are covered by the IsNPC2D key and are
listed for the soak (card 127). Out of goal, carded: 192 (crash), 193, 194; M7Mech and the 3D no-ragdoll classes
stay under "Problems for the senior".

## Build for c177-r2 (junior, 2026-11-12)

All five re-scope items and both peer review points are in one build (`mod/src/ubw/pose_c132.cpp`,
`hooks_internal.h`, `enemies.cpp` health offset moved to the header).
1. Revive: `sprite_host_step` reads `kNpcCurrentHealth` (0xAC0) for every held row; above 0 the row is removed and
   "R" is sent (`sprite_host_drop`), counted as `revived`. A later TryPlayDeathAnimation adds a new row. The
   "second death" reject is gone: a death start for a held row goes to `sprite_host_update`.
2. Sequence change: `sprite_host_update` (rest read and second death start) compares the animator's sequence with
   the row's; a different death name replaces name, frame count, frame and length, clears the rest read, re-sends
   the same row (`sprite_send` sends the fresh frame and count when the names match) and counts `updated`. The
   client's `sprite_receive` replaces the held row, counts `updates_rx`, and re-arms every wait for that guid
   (check_at 0), so the newest row applies.
3. Frame and count: `sprite_read` keeps the frame index only when it is inside this sequence's count, else 0 (an
   index past the count is the previous sequence's, the switch happened this tick).
4. Side: `c132_pose_step` latches `side()` per tick into `g_sprite.side`; the totals line uses it (at map end the
   session flag is already clear, which made r1c's client line say "host").
5. Scenario `tools/scenarios/c177-r2.txt` (below).

Names a row may carry. Host: `sprite_death_name`, a name starting "Death" (Death, Death01, Death02, ...) or
"OverKill" (the gib switch); any other name at a death start or a rest read is "not a death sequence" and the row
keeps its last death name. Client: `sprite_name_ok`, 1 to 23 characters of [A-Za-z0-9_] (the client does not judge
the name, the host did; the old strncmp "Death" check refused OverKill updates). A bad name is "bad head".

`sprite_set_rest` null and range proof, from the code (`ue/reflect.cpp:63`: `read_ptr` dereferences without a null
check, so every pointer is tested before it is used as a base):
- `sprite_read`: `anim = read_ptr(npc, kNpcSpriteAnimator)`; `seq` is read only when `anim` is non-null; a null
  `anim` returns `kSrNoAnimator`, a null `seq` returns `kSrNoSequence`, before any read through either. Frame count
  outside 1..512 (`kSpriteFramesMax`) returns `kSrFrameCount` (and a long name `kSrNameLong`) before the frame
  table is touched; the frame table pointer is tested for null in the time sum.
- `sprite_set_rest(npc)` calls `sprite_read(npc)` first and returns its reject before any write; the animator and
  sequence are re-read in the same call on the game thread (the same non-null pointers), a null frame table or an
  unresolved broadcast returns `kSrNoBroadcast`; the index written is `s.frames - 1` from that same read. Only then
  the three writes and the frame delegate broadcast (a local delegate, nothing goes on the wire).
- The caller (`sprite_apply`) counts every reject through `sprite_reject` (totals and a capped WARN line).
No case was missing; the index source changed from the row to the same-call read in this build.

## Run c177-r2 predictions (written before the run)

Route as r1c (FED_C2M1, the squad Morita troopers). Exact lines, `%` fields as logged:
1. Live kill (part 1): host `card177: host sprite death BP_AI_Trooper_Morita_C guid 0x.. seq Death0N (4 frames,
   N ms), sent to 1 PCs; Health H at X Y Z`, then `card177: host sprite rest ...`; client `card177: client sprite
   row guid 0x.. seq Death0N, host age N ms, ...`.
2. OverKill update: in r1c the first kill's rest read found `OverKill` (row Death01). Now: host `card177: host sprite
   row update BP_AI_Trooper_Morita_C guid 0x.. seq Death01 -> OverKill (N frames, N ms), at the rest read, sent to 1
   PCs`; client `card177: client sprite row guid 0x.. seq OverKill, ... (update, was Death01)`. Totals: host
   `N updated` >= 1, client `received (N updates)` equal to the host's updates sent while it was connected.
3. Arrival read-back (part 2): client far away, kill, client warped by point to the corpse from the host's death
   line (+300 x, +100 z) within seconds: client `card177: client sprite death BP_AI_Trooper_Morita_C guid 0x..
   seq S, arrival, rest state set` (or `played from the start` when it arrives inside the sequence), then
   `card177: client sprite read-back BP_AI_Trooper_Morita_C guid 0x..: seq S frame F-1 of F, host seq S frame F-1
   of F` with no WARN prefix. Frame never past the count (no "5 of 4").
4. Revive: none expected on this route (no revive mechanic in FED_C2M1); host totals `(0 revived)` and no
   "second death" in the reject list. A `host sprite row removed ... revived` line, if one appears, must be
   followed by "R" (client `removed by the host` +1).
5. Side label: host `card177: host sprite death map totals: ...`; client `card177: client sprite death map totals:
   ...` (r1c: "host" on the client).
6. Rejoin replay (part 3): host `card177: host sprite replay to a joining PC: N rows sent of N, 0 without a NetGUID`
   with N equal to the live rows (r1c: 21 of 21), client `card177: client sprite row guid ...` lines for them.
No WARN card177 lines on either side; table peak under 256, wait peak under 32.

## Run c177-r2 results (junior, 2026-11-12, DLL 583a464f36f76e1f)

Exit 0, all 81 steps ok, no crash dump. Lines quoted from logs/c177-r2-{host,client}.log.
1. Live kill: PASS. Host `card177: host sprite death BP_AI_Trooper_Morita_C guid 0xb2 seq Death02 (4 frames, 310
   ms), sent to 1 PCs; Health 0.0 at 231884 43207 8729`; client `card177: client sprite row guid 0xb2 seq Death02,
   host age 0 ms, host frame 2 of 4`.
2. OverKill update: PASS. Host `card177: host sprite row update BP_AI_Trooper_Morita_C guid 0xb2 seq Death02 ->
   OverKill (4 frames, 200 ms), at the rest read, sent to 1 PCs`; client `card177: client sprite row guid 0xb2 seq
   OverKill, host age 1422 ms, host frame 3 of 4 (update, was Death02)`; client totals `3 received (1 updates)`.
3. Arrival read-back: PASS. Client warped to `226945 39891 8515` (+300, +100) 0.1 s after the host death line:
   `card177: client sprite death BP_AI_Trooper_Morita_C guid 0xa4 seq Death01, arrival, played from the start (host
   age 189 ms, sequence 1060 ms; ...)`, then `card177: client sprite read-back BP_AI_Trooper_Morita_C guid 0xa4: seq
   Death01 frame 3 of 4, host seq Death01 frame 3 of 4`. Frame never past the count.
4. Revive: PASS as predicted (none on the route): host totals `0 removed (0 revived)`, no "second death" reject.
5. Side label: PASS. Client `card177: client sprite death map totals: ...` at both its map ends (rejoin and
   travel); host `card177: host sprite death map totals: 5 rows added, 2 sent, 3 owed (0 sent later), 0 removed (0
   revived), 1 updated, 5 replayed, 6 host rest reads; ...`.
6. Rejoin replay: PASS. Host `card177: host sprite replay to a joining PC: 5 rows sent of 5, 0 without a NetGUID`;
   client five `card177: client sprite row guid ...` (0xb2 as OverKill, host age 34829 ms) and four `arrival, rest
   state set`.
The three owed rows died while the client was away (no PC, no NetGUID yet); the replay carried them, so "0 sent
later" is correct. No WARN card177 lines. Table peaks: host 5 of 256 (wait 0 of 32); client 2 then 5 of 256 (wait
1 then 4 of 32).

## Senior review (2026-11-12)

Accepted on c177-r2 (run build `583a464f36f76e1f`; tree `f1c6127cacc511b1` adds only the card 192 totals side label,
not run). Diff against the pre-177 snapshot touches only the allowed files; rebuild is "no work to do". Host: 5 rows
added, 1 row update Death02 -> OverKill sent, 5 replayed, WARN classes equal the baseline. Client: 1 arrival played
from the start with a matching read-back; after the rejoin 4 arrivals set to the rest state, each read "frame 3 of 4,
playing Death01" in the same call; 0 fallbacks, 0 mismatches.

Not proven, on the card 127 watch list: the revive path (0 revived), the laser classes, and the read-back 1000 ms
after a rest-state set (the scenario left the map 1071 ms after the arrivals, so 0 of 4 read-backs ran).
Carded: 3D classes with no ragdoll start and the M7Mech death set (card 197).
