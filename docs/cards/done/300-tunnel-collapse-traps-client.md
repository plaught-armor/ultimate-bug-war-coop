# 300 The tunnel doorway collapses on the client; the game does not wait for every player

Playtest 2026-11-13 (user plays as client, FED_C2M1): "i am trapped in the collapsed tunnel door way. game didn't
wait until i got out."

The single-player game closes the tunnel when its one player is clear. In co-op the close ran while the client's
trooper was still inside the doorway.

## Trace (client log `UBWCoop-menu-20261113-184603.log`)

| Client t (ms) | Row |
|---|---|
| 257614726 | client's own level script reaches `CompleteObjective LOS_O05_SO04` and `AddSubObjective LOS_O05/LOS_O05_SO05` (both skipped locally, cards 100 and 258) |
| 257614783 to 257614788 | host rows: `ObjectiveActors LOS_O05_SO05 ActorReference_C_1`, `native CompleteObjective LOS_O05/LOS_O05_SO04` |
| 257619941 | own pawn at (137291, -41574, 9268) |
| 257621773 to 257622988 | `BP_HordeZone_C_3` at (155570, -43881, 9037): `FadeIn` and `LerpPostProcess` timelines, then `NODE_AddSplineMeshComponent-2` collision 0 -> 1 `by local sim` |
| 257629961, 257639970 | own pawn at (139822, -39655, 9505), then (145111, -37175, 10129) |
| 257639272, 257644324 | `GA_HumanJump_C` and `GA_Mantling_Search_C` on the own pawn |
| 257649970 to 257679979 | own pawn stays within 310 uu: (144761, -37113, 10070), (144744, -37119, 10069), (144640, -37411, 10070) |
| 257685399 | `world K BP_HordeZone_C_3`: `Health base 0 and OnDeath broadcast` |
| 257689983 | own pawn at (144884, -36984, 10202) |

User confirmed at about client t=257810000: still trapped. Own pawn then moves only between (144967, -37123,
10072) and (144901, -36842, 10293), 130 s after the first stuck sample. The play build has no way to free a
player; the fix must include one that needs no rebuild during play (the placement rule under Fix).

The trapped spot is near (144700, -37200, 10070). Between 257614000 and 257690000 the client log has no
`card098`, `card137`, `card145` or `card163` row for any actor within 2000 uu of that spot: no spawn, show,
collision change or timeline for a collapse, rock fall or door. The closing actor is not named by the log, and
its state is not in the card 132 digest (rounds 85 to 88 list the same 18 mismatches as before, none of them a
tunnel actor).

## Cause (static; candidate, to confirm in step 1)

The collapse is started by the level script (or a trigger volume next to the doorway) with the single-player
condition: one pawn, the host's, is past the doorway or one objective step is done. Nothing tests where the other
player is. The collapse geometry then blocks or encloses the client's trooper. Which machine started it on the
client (the client's own script copy, or a host row the log does not print) is open.

## Step 1 (static, junior, no run; host log after the playtest)

1. Name the actor and the function: class and instance of the tunnel collapse near (144700, -37200, 10070) in
   FED_C2M1, the function that closes it (level script event, trigger overlap, sequence, timeline), and what it
   changes (mesh shown, collision on, geometry collection, nav, kill or push volume).
2. Name the condition that starts it and which pawn it tests: `GetPlayerPawn(0)`, an overlap by any pawn, an
   objective step (`LOS_O05_SO04` complete at host t=83745552), a timer.
3. Say what the single-player game does to a player inside the doorway when it closes (push out, teleport, kill,
   nothing).
4. Say how the close reached the client: the client's own script, a replicated property, or a mod row; and why
   the log has no row for it.
5. From the host log: the host pawn's position and the rows for that actor between host t=83745000 and 83816000
   (client t minus 173869236).

Write the answers into this card before coding.

## Step 1 answers (junior, static, 2026-11-13)

1. **Actor and function.** `BP_DestroyableActor_HiveEntrance_C_1` in `FED_C2M1_Scripting`, `StaticMesh` at
   (145156, -37252, 9861), 460 uu from the trapped spot. Alive mesh `SM_BugHive_Cave_OpenedEntrance_01` (the open
   cave mouth), destroyed mesh `SM_BugSpawner_Cave_01_Destroyed` (the collapsed cave), destruction effect
   `NS_FX_CarpetBombing`. It is closed by the level script calling `LD: Destroy()` (ubergraph 22243 path:
   `Conversation_08_EndArea`, then `HiveEntrance_C_1.LD: Destroy`, `TriggerableAudioEmitter_C_3`, then at 21846
   `BP_HordeZone_C_3.Begin KillEmAll(false)`, `EM SetWaves C_72` and `C_73`, then presences `C_78/79/81`
   disabled). `LD: Destroy` runs `Destruction FX` (IsDestroyed true, `Destroyed` delegate, PFX, SFX, camera
   shake) and `Setup Destroyed` (`STMeshUpdate.ForceUpdateMesh(DestroyedMesh)`, a soft async mesh swap; the
   collapsed mesh's collision comes with it). `Begin KillEmAll` turns on the arena wall spline meshes' collision
   (`ToggleCollision(true)`: collision enabled, profile, channel 14 block) and the hazard shunts' PhysicalBlocker
   collision 3. No geometry collection, no nav change, no kill or push volume.
2. **Condition.** Any one pawn entering any of the three `BP_PlayerPresence_C_78/79/81` volumes, after
   `EthanFound?` is set (presence `C_748`, enabled at 21500 in `Conversation_09_EthanDeath_PartThree` from
   presence `C_76`). The test is the presence's own overlap, with no player filter. Volumes: `C_78` at (147667,
   -37092, 9790), `C_79` at (145930, -35260, 9630), `C_81` at (146080, -39150, 9790); all east of the doorway.
   These presences are not in the card 227 gate: that gate only holds presences whose `LTOnEnter` reactions are
   door or spawn reactions (`kC227GateClasses`); these are script-bound events.
3. **Single player.** Nothing moves, teleports or kills the player. The volumes sit past the doorway, so the one
   player is already through when the mesh swap and the wall collision happen.
4. **How it reached the client.** The client's own level-script copy ran the whole path: the client log has the
   HordeZone FadeIn and LerpPostProcess at 257621773 and the spline mesh collision 0 -> 1 "by local sim" at
   257622988, while the client's own pawn was at (137291, -41574) at 257619941, far west. The host's pawn
   entering the client's copy of a presence fired it there. No row: the mod has no row class for
   `BP_DestroyableActor_*` (the mesh swap is a component mesh change, not actor `bHidden`), and the script ran
   locally, so nothing was relayed.
5. **Host log (senior, 2026-11-13): confirms items 2 and 4; cause settled.** Presences `C_78/79/81` go enabled
   at host t=83745600 (`C_748` disabled, EthanFound). The host's pawn is at (148672, -38721, 9883) at 83752608,
   inside the `C_78` area; the three presences go disabled at 83752650 and the HordeZone shunts turn on at
   83753591. The client's pawn is at (137291, -41574) at client t=257619941 (host t=83750705), about 11k uu
   west of the doorway. One pawn (the host's) closed the tunnel with the client still outside.

## Fix

A one-way closure waits for every player. The start condition found in step 1 item 2 is evaluated on the host
over all player pawns (the mechanism of card 288, which makes the CTF door wait for all players): the close runs
when every living player is on the far side. The client never starts it from its own script; the close reaches
the client as a world row (card 098), with journal replay for join and reconnect, and the actor's closed flag
enters the card 132 digest (shared with card 299).

A player who can still end up on the wrong side or inside closed geometry (downed and revived there, respawn,
reconnect) is placed by the game's own respawn or checkpoint placement at the far side; the row says so.

- Rows: `card300: host t=<ms> closure <actor> wanted by <condition>: players past <n> of <m>, waiting`,
  `card300: <role> t=<ms> closure <actor> closed: players past <m> of <m>`,
  `card300: <role> t=<ms> player <id> moved out of closure <actor> to (<x>, <y>, <z>) by <game function>`,
  `WARN card300: <role> closure <actor> closed with player <id> not past it`.

## Sibling sweep (same class: a script step that closes, blocks or removes the way behind the player on a
single-player condition)

| Item | Check |
|---|---|
| Every tunnel collapse, cave-in, rock fall and one-way door on every FED map | static list by class and level script call; each waits for all players |
| Doors and gates that close behind the player (base gates, bunker doors, lifts) | same |
| Level streaming unload of the area behind (a client left there falls or sees nothing) | waits for all players |
| Kill volumes and out-of-bounds timers switched on by progress | not switched on with a player inside |
| Card 288 CTF door, card 163 script doors | one shared all-players test, not two |
| Dropship pickup and level end with a player left behind | all aboard or carried (card 285 class) |
| Downed, respawning or reconnecting player on the wrong side | placed past the closure |
| Digest | closure state counted; 0 mismatches |

## Predictions (one run, after the playtest, batched with 299)

- Host passes the doorway, client stays inside it 10 s: host log has `card300 ... waiting` rows with `players
  past 1 of 2` and no close; client screenshot aimed at the doorway shows it open.
- Client walks out: one `card300 ... closed` row on each machine within 1 s, zero `WARN card300`; both
  screenshots show the collapse; neither pawn is inside it.
- Client reconnects after the close: closed on arrival, pawn on the far side.
- Digest round after: 0 mismatches, the closure actor counted.

## Build 7 (coded, compiled; diff `b7-A-300.diff`)

- `objectives.cpp`: `kC300Closures` (FED_C2M1_Scripting `BP_PlayerPresence_C_78/79/81`) join the card 227 gated table
  at BeginPlay with `closure = true`. The three presences are one group: a player is past the closure when it
  overlaps any of them. The host runs the original entry edge once every living player is past (downed, out or dead
  players not past are counted and named in a WARN row); then every presence of the group is spent.
- Host: `c300_host_send` sends a journalled card 098 world row of kind `F` (presence name and place) to every
  client. A joining or reconnecting client gets it by the journal replay.
- Client: its own entries of a closure presence never run the edge ("left to the host's row"). The `F` row finds the
  presence by name and place and runs the original entry edge on the client's copy with the client's own pawn.
- Rows: `card300: host ... closure X wanted by entry of P: players past N of M, waiting`, `card300: <side> closure X
  closed: players past N of M`, `card300: host ... row sent to N of M clients`, `card300: client ... closure X from the
  host row[; journal replay]`, `WARN card300: client ... no presence matches, closure not run`.
- Not coded: the digest entry for `BP_DestroyableActor` state (class C, card 299) and the placement of a player left
  outside the closure. The client's script branch after the edge may depend on EthanFound being set on the client;
  the run reads the client's `card300` row and its arena state.

## Test status (senior decision, 2026-11-13): both closure cases unproven by test

Neither the not-past case (WARN closed with a player not past) nor the past case (`closed: players past 2 of 2` on
both sides) has a run. Cause: the closure sits behind the FED_C2M1 LOS chain (through LOS_O05_SO04), and that
chain cannot be completed by command: `objective complete LOS_O01_SO01` skips the level script's own
`AddSubObjective(LOS_O01, LOS_O01_SO02)` and ends the chain (probe log `logs/b7b-2-probe-host.log`). Reaching it by
real play costs a probe per step. Both cases go to the user's playtest; the rows to read there are listed above.

## Scenario proof step (senior, 2026-11-17 08:53, b177)

`tools/scenarios/c423-gates-c2m1.txt` now ends (before the map-end block) with a card 300 block: the host warps
into a closure presence (`BP_PlayerPresence_C_78`, then `_79`, `_81`) alone, then the client is warped in. All steps
are tries. Predictions: host `wanted by entry ... players past 1 of 2, waiting` and no close while alone; then host
`closed: players past 2 of 2`, `row sent to 1 of 1 clients`, client `closure ... from the host row`; shots
`c300-host-in-waiting` (tunnel open) and `c300-closed`. The presence may be switched off before LOS_O05_SO04; no
card300 row then means the step needs the LOS_O05 chain played first (next: extend the scenario through SO01 to SO04
with `objective go all`).

## Run c423b177a (2026-11-17 09:08): no entry fired

0 card300 rows. The warps put both players 119 to 160 uu from the three presence origins and no entry ran (no
card227 `held` row either). Next run: warp distance 0 (onto the origin). Still no row then: the presences' overlap
is off before LOS_O05_SO04 and the chain must be played first.

## c423b178c, 2026-11-17

Warps at 70-121 uu from the three presences: 0 card300 rows. The presences belong to LOS_O05_SO04; the host was at
LOS_O05_SO01, so they are not on yet. The proof needs the route driven through LOS_O05_SO01..SO03 first.

## c423b179b, 2026-11-17: mech pilot never counted (mod bug)

Route driven through LOS_O05_SO01..SO04. Host row `closure BP_PlayerPresence_C_78 wanted by entry of ...: players
past 1 of 2, waiting` fires; with the client piloting the mech it never reaches 2 of 2. Cause (`c227_count`,
objectives.cpp:3308): the host's roster pawn of a pilot is the mech, the entering actor is the pilot's soldier, and
the game's overlap does not report the mech, so a mech pilot is never inside. Applies to every card 227 presence, not
only closures: a pilot would hold every gated door, spawn and closure. Fix in b180: for a mech roster pawn also test
its soldier (`c75_soldier_of`), by identity, by `c300_past`, and by overlap.

## Closed

Closed 2026-11-17 on b180 `4c41c9c8455f774f`. `c227_count` also tests the soldier riding in a mech roster pawn
(`c75_pilot_body`), by identity, `c300_past` and overlap; the door count (`c288_count`, `c288_presence_fired`) counts a
mech pilot too. Run c423b180c with the client piloting the mech: host `held: 1 of 2 players inside (... mech
BP_BaseMech_C pilot body BP_PlayerCharacterMI_C)`, `wanted ... players past 1 of 2, waiting`, 4.2 s later `closed:
players past 2 of 2`, `row sent to 1 of 1 clients`; client `closure BP_PlayerPresence_C_78 from the host row`. Shots
c300-host-in-waiting (tunnel open, mech inside) and c300-closed (collapse on both sides, mech outside). Gate
c448b180a passes. Not changed, noted: c321 door crush and c361 hazard zone exclude mechs by class on both sides.
Playtest watch: a player left behind a collapse, or a closure that never fires with every player past, reopens.
